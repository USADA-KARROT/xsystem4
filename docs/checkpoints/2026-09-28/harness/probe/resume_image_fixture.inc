/* resume-image: a VM image (system.ResumeSave / ResumeLoad, the engine
 * snapshot behind the retry and replay buttons) comes back as it was written,
 * in both formats: rsm (the default, symbols by name) and json (numbers).
 *
 * Only files of this engine (rsm 9) are used; the original game writes rsm
 * 14, not readable by libsys4. What has to hold is that the reader takes back
 * exactly what the writer put there, and leaves the heap in the state the rest
 * of the VM expects (free list, empty slots, the delegate target table, the
 * guard page in slot 0, the fixed-size call stack).
 *
 * Each case runs in its own process (sp_run). A case that expects the load to
 * be refused runs the load in a further process and reads what it printed.
 *
 * Cases (each once per format unless it says otherwise):
 *   RI1  An empty wrap box and a wrap box holding an object (struct pages
 *        with no struct number, one slot): type, number -1, size, value,
 *        reference count and sequence number come back, and the object keeps
 *        its count. rsm writes such a page with an empty struct name.
 *   RI2  (rsm only; json stores numbers) A struct page whose struct name is
 *        not in the AIN, and a local page whose function name is not: the
 *        load is refused with a message naming it, without a sanitizer report.
 *        Also a local page whose symbol is "name#99" for a name with fewer
 *        functions: refused, the message giving the whole symbol.
 *   RI3  Overloads (functions sharing a name), the first one of each kind in
 *        function-number order: a later overload with more variables than the
 *        first, one with fewer, one with the same number but other types, and
 *        one with the same variables and hash (CRC) as the first but another
 *        address. Two frames of it are pushed (the second returning into the
 *        first at offset 10): after the load both frames, both local pages
 *        and the instruction pointer belong to that function again.
 *   RI4  (rsm only) Symbols in the file: the first function of a name is
 *        written by its bare name (guard), a later overload as "name#n" with
 *        n its place among the functions of that name (in the local page and
 *        in the return records).
 *   RI5  (rsm only, guard) A file whose symbols have no "#n" (as an earlier
 *        engine wrote them) reads every such symbol as the first function of
 *        that name: the same-shape overload of RI3 comes back as the first.
 *   RI6  A heap grown past 10000 slots, nearly full: after the image is
 *        written, strings take slots that are empty in the image; then the
 *        image is loaded. Every slot with no reference has no type and no
 *        pointer, the free list holds exactly the unreferenced slots (count
 *        and walk), the strings of the image are back, and 2000 further
 *        allocations (which run the collector) go through.
 *   RI7  (both formats in one process) The free count after an rsm load and
 *        after a json load of the same state are equal.
 *   RI8  Slot 0 after a load is the guard page the VM starts with: a live
 *        array page, element type void, 64 slots, rank 0, all zero; also when
 *        writes it absorbed before the image left values in it, and when the
 *        load freed another 64-slot page (rank 2) the page cache can hand out.
 *   RI9  Delegate targets: an object and a delegate on it are in the image,
 *        the object is released before the load; after the load releasing
 *        the object again drops the delegate's entry for it.
 *   RI10 (guard) A delegate registered on an object only after the image was
 *        written: after the load, releasing that object leaves the delegates
 *        of the image alone, and releasing an object of the image drops the
 *        entry for it.
 *   RI11 Call frames: an image with one frame more than the call stack holds
 *        is refused with a message, without writing past the call stack; an
 *        image with exactly as many frames as it holds loads (guard).
 */

#include <fcntl.h>
#include <system4/savefile.h>
#include "cJSON.h"

#define RI_KEY "resume-image"
#define RI_CALL_STACK_MAX ((int)(sizeof call_stack / sizeof call_stack[0]))

static int ri_bad;

#define RI_EXPECT(c, ...) do {                                                  \
		if (!(c)) {                                                     \
			printf("  FAIL %s:%d: ", __func__, __LINE__);            \
			printf(__VA_ARGS__);                                    \
			printf("\n");                                           \
			fflush(stdout);                                         \
			ri_bad = 1;                                             \
		}                                                               \
	} while (0)

static const char *ri_fmt(void) { return config.save_format == SAVE_FORMAT_RSM ? "rsm" : "json"; }

static void ri_path(char *buf, size_t n, const char *file) { snprintf(buf, n, "%s/%s", sp_dir, file); }

/* Write the image the way system.ResumeSave does: from inside a function (a
 * frame of main when the case has pushed none; the rsm writer takes the
 * instruction pointer relative to the innermost frame), the two arguments of
 * the call on top of the stack. */
static bool ri_save(const char *file)
{
	if (call_stack_ptr == 0 && _function_call(ain->main, VM_RETURN) <= 1)
		return false;
	stack_push(0);
	stack_push(0);
	int ok = vm_save_image(RI_KEY, file);
	stack_pop();
	stack_pop();
	return ok;
}

/* The first struct whose members are all int (alloc_struct makes it without
 * pages of its own); used for objects the cases only count and release. */
static int ri_plain_struct(void)
{
	for (int s = 0; s < ain->nr_structures; s++) {
		struct ain_struct *as = &ain->structures[s];
		if (as->nr_members < 1)
			continue;
		bool ok = true;
		for (int i = 0; i < as->nr_members && ok; i++)
			ok = as->members[i].type.data == AIN_INT;
		if (ok)
			return s;
	}
	printf("RESUME-IMAGE no struct has int members only\n");
	exit(2);
}

/* Run vm_load_image in another process with its output in a file; print the
 * output (indented) and return the wait status. */
static int ri_load_elsewhere(const char *file, char *out, size_t outn)
{
	char log[PATH_MAX];
	ri_path(log, sizeof log, "load-output.txt");
	fflush(stdout);
	fflush(stderr);
	pid_t pid = fork();
	if (pid == 0) {
		int fd = open(log, O_WRONLY | O_CREAT | O_TRUNC, 0600);
		if (fd < 0)
			_exit(3);
		dup2(fd, 1);
		dup2(fd, 2);
		vm_load_image(RI_KEY, file);
		printf("RI load returned\n");
		fflush(stdout);
		_exit(0);
	}
	int st = 0;
	waitpid(pid, &st, 0);
	out[0] = 0;
	FILE *fp = fopen(log, "rb");
	if (fp) {
		size_t n = fread(out, 1, outn - 1, fp);
		out[n] = 0;
		fclose(fp);
	}
	for (char *line = out; *line; ) {
		char *nl = strchr(line, '\n');
		printf("    | %.*s\n", nl ? (int)(nl - line) : (int)strlen(line), line);
		if (!nl)
			break;
		line = nl + 1;
	}
	return st;
}

/* The load of file is refused: the process exits with 1 (not a signal), the
 * output names want, and no sanitizer report is in it. */
static void ri_expect_refused(const char *file, const char *want)
{
	static char out[65536];
	int st = ri_load_elsewhere(file, out, sizeof out);
	RI_EXPECT(WIFEXITED(st) && WEXITSTATUS(st) == 1, "%s: the load ended with %s %d, want exit 1", ri_fmt(),
		  WIFSIGNALED(st) ? "signal" : "exit", WIFSIGNALED(st) ? WTERMSIG(st) : WEXITSTATUS(st));
	RI_EXPECT(strstr(out, want) != NULL, "%s: the output does not say \"%s\"", ri_fmt(), want);
	RI_EXPECT(!strstr(out, "Sanitizer") && !strstr(out, "runtime error"), "%s: a sanitizer report", ri_fmt());
}

static struct rsave *ri_read_rsave(const char *file)
{
	char path[PATH_MAX];
	ri_path(path, sizeof path, file);
	enum savefile_error error;
	struct rsave *rs = rsave_read(path, RSAVE_READ_ALL, &error);
	if (error != SAVEFILE_SUCCESS) {
		printf("  rsave_read %s: %s\n", file, savefile_strerror(error));
		return NULL;
	}
	return rs;
}

static bool ri_write_rsave(struct rsave *rs, const char *file)
{
	char path[PATH_MAX];
	ri_path(path, sizeof path, file);
	FILE *fp = fopen(path, "wb");
	if (!fp)
		return false;
	enum savefile_error error = rsave_write(rs, fp, true, 1);
	fclose(fp);
	return error == SAVEFILE_SUCCESS;
}

/* ---- RI1 wrap boxes ------------------------------------------------------ */

static bool ri_is_box(int slot, int value)
{
	struct page *p = da_live_page(slot) ? heap[slot].page : NULL;
	return p && p->type == STRUCT_PAGE && p->index == -1 && p->nr_vars == 1 && p->values[0].i == value;
}

static int ri1_boxes(void)
{
	ri_bad = 0;
	int empty = variable_initval(AIN_WRAP).i;
	int full = variable_initval(AIN_WRAP).i;
	int obj = alloc_struct(ri_plain_struct());
	heap[full].page->values[0].i = obj;
	heap_ref(obj);
	heap_ref(full);
	SP_CHECK(ri_is_box(empty, -1) && ri_is_box(full, obj), "variable_initval(AIN_WRAP) did not make a box");
	int refs[3] = { HEAP_REF(empty), HEAP_REF(full), HEAP_REF(obj) };
	uint32_t seqs[2] = { heap[empty].seq, heap[full].seq };
	SP_CHECK(ri_save("ri1.dat"), "the %s image was not written", ri_fmt());
	vm_load_image(RI_KEY, "ri1.dat");
	printf("RI1 %s: empty box %d %s ref %d; box %d %s ref %d holding %d ref %d\n", ri_fmt(), empty,
	       ri_is_box(empty, -1) ? "back" : "NOT BACK", HEAP_REF(empty), full, ri_is_box(full, obj) ? "back" : "NOT BACK",
	       HEAP_REF(full), obj, HEAP_REF(obj));
	RI_EXPECT(ri_is_box(empty, -1), "%s: the empty box did not come back", ri_fmt());
	RI_EXPECT(ri_is_box(full, obj), "%s: the box holding %d did not come back", ri_fmt(), obj);
	RI_EXPECT(HEAP_REF(empty) == refs[0] && HEAP_REF(full) == refs[1] && HEAP_REF(obj) == refs[2],
		  "%s: reference counts %d %d %d, were %d %d %d", ri_fmt(), HEAP_REF(empty), HEAP_REF(full), HEAP_REF(obj),
		  refs[0], refs[1], refs[2]);
	RI_EXPECT(heap[empty].seq == seqs[0] && heap[full].seq == seqs[1], "%s: sequence numbers changed", ri_fmt());
	RI_EXPECT(da_live_page(obj) && heap[obj].page->type == STRUCT_PAGE, "%s: the object in the box did not come back",
		  ri_fmt());
	return ri_bad;
}

static int ri1_rsm(void) { config.save_format = SAVE_FORMAT_RSM; return ri1_boxes(); }
static int ri1_json(void) { config.save_format = SAVE_FORMAT_JSON; return ri1_boxes(); }

/* ---- RI2 names that are not in the AIN (rsm) ----------------------------- */

static int ri2_struct(void)
{
	ri_bad = 0;
	config.save_format = SAVE_FORMAT_RSM;
	int sno = ri_plain_struct();
	int obj = alloc_struct(sno);
	SP_CHECK(ri_save("ri2.dat"), "the image was not written");
	struct rsave *rs = ri_read_rsave("ri2.dat");
	SP_CHECK(rs && obj < rs->nr_heap_objs && *(enum rsave_heap_tag *)rs->heap[obj] == RSAVE_STRUCT,
		 "the object is not a struct in the file");
	struct rsave_heap_struct *s = rs->heap[obj];
	SP_CHECK(s->struct_type.name && !strcmp(s->struct_type.name, ain->structures[sno].name),
		 "the struct name in the file is not the object's");
	free(s->struct_type.name);
	s->struct_type.name = strdup("NoSuchStructInTheAin");
	SP_CHECK(ri_write_rsave(rs, "ri2.dat"), "the changed image was not written");
	rsave_free(rs);
	ri_expect_refused("ri2.dat", "NoSuchStructInTheAin");
	return ri_bad;
}

static int ri2_function(void)
{
	ri_bad = 0;
	config.save_format = SAVE_FORMAT_RSM;
	int fno = ain->main;
	int csp = call_stack_ptr;
	int slot = _function_call(fno, VM_RETURN);
	SP_CHECK(slot > 1 && call_stack_ptr == csp + 1, "no frame for function %d", fno);
	SP_CHECK(ri_save("ri2f.dat"), "the image was not written");
	struct rsave *rs = ri_read_rsave("ri2f.dat");
	SP_CHECK(rs && slot < rs->nr_heap_objs && *(enum rsave_heap_tag *)rs->heap[slot] == RSAVE_LOCALS,
		 "the frame's page is not a local page in the file");
	struct rsave_heap_frame *f = rs->heap[slot];
	free(f->func.name);
	f->func.name = strdup("NoSuchFunctionInTheAin");
	SP_CHECK(ri_write_rsave(rs, "ri2f.dat"), "the changed image was not written");
	rsave_free(rs);
	ri_expect_refused("ri2f.dat", "NoSuchFunctionInTheAin");
	return ri_bad;
}

/* A local page whose symbol names an overload number its name does not have:
 * refused, and the message gives the whole symbol. */
static int ri2_number(void)
{
	ri_bad = 0;
	config.save_format = SAVE_FORMAT_RSM;
	int fno = ain->main;
	int slot = _function_call(fno, VM_RETURN);
	SP_CHECK(slot > 1, "no frame for function %d", fno);
	SP_CHECK(ri_save("ri2n.dat"), "the image was not written");
	struct rsave *rs = ri_read_rsave("ri2n.dat");
	SP_CHECK(rs && slot < rs->nr_heap_objs && *(enum rsave_heap_tag *)rs->heap[slot] == RSAVE_LOCALS,
		 "the frame's page is not a local page in the file");
	struct rsave_heap_frame *f = rs->heap[slot];
	char sym[512];
	snprintf(sym, sizeof sym, "%s#99", f->func.name);
	free(f->func.name);
	f->func.name = strdup(sym);
	SP_CHECK(ri_write_rsave(rs, "ri2n.dat"), "the changed image was not written");
	rsave_free(rs);
	ri_expect_refused("ri2n.dat", sym);
	return ri_bad;
}

/* ---- RI3..RI5 overloads ---------------------------------------------------- */

enum { RI_MORE, RI_FEWER, RI_TYPES, RI_SAME, RI_NR_KINDS };
static const char *ri_kind_name[] = { "more variables", "fewer variables", "other types", "same shape" };

static int ri_first_of_name(int fno)
{
	char *copy = strdup(ain->functions[fno].name);
	int first = ain_get_function(ain, copy);
	free(copy);
	return first;
}

static bool ri_callable(struct ain_function *f)
{
	return f->address < ain->code_size && f->nr_vars >= 0 && f->nr_vars < 100000;
}

static bool ri_same_types(struct ain_function *a, struct ain_function *b)
{
	for (int i = 0; i < a->nr_vars; i++) {
		if (a->vars[i].type.data != b->vars[i].type.data)
			return false;
	}
	return true;
}

/* The first later overload (in function-number order) of the kind. */
static int ri_overload(int kind)
{
	for (int fno = 0; fno < ain->nr_functions; fno++) {
		struct ain_function *f = &ain->functions[fno];
		if (!f->name || !ri_callable(f))
			continue;
		int first = ri_first_of_name(fno);
		if (first < 0 || first == fno)
			continue;
		struct ain_function *g = &ain->functions[first];
		if (!ri_callable(g))
			continue;
		bool hit = false;
		switch (kind) {
		case RI_MORE:  hit = f->nr_vars > g->nr_vars; break;
		case RI_FEWER: hit = f->nr_vars < g->nr_vars; break;
		case RI_TYPES: hit = f->nr_vars == g->nr_vars && !ri_same_types(f, g); break;
		case RI_SAME:  hit = f->nr_vars == g->nr_vars && ri_same_types(f, g) && f->crc == g->crc
				    && f->address != g->address; break;
		}
		if (hit)
			return fno;
	}
	printf("RESUME-IMAGE no overload of the kind \"%s\"\n", ri_kind_name[kind]);
	exit(2);
}

/* Two frames of fno, the second returning into the first at offset 10. */
static bool ri_push_two(int fno, int *pages)
{
	int csp = call_stack_ptr;
	pages[0] = _function_call(fno, VM_RETURN);
	pages[1] = _function_call(fno, ain->functions[fno].address + 10);
	return pages[0] > 1 && pages[1] > 1 && call_stack_ptr == csp + 2;
}

static int ri3_overload(int kind)
{
	ri_bad = 0;
	int fno = ri_overload(kind), first = ri_first_of_name(fno);
	struct ain_function *f = &ain->functions[fno];
	printf("RI3 %s %s: function %d (%d variables), first of its name %d (%d variables)\n", ri_fmt(), ri_kind_name[kind],
	       fno, f->nr_vars, first, ain->functions[first].nr_vars);
	int pages[2];
	SP_CHECK(ri_push_two(fno, pages), "no frames for function %d", fno);
	int top = call_stack_ptr - 1;
	int ip = instr_ptr;
	SP_CHECK(ri_save("ri3.dat"), "the %s image was not written", ri_fmt());
	vm_load_image(RI_KEY, "ri3.dat");
	printf("RI3 %s %s: frames %d %d, local pages %d %d, return address %d (want %d), ip %d (want %d)\n", ri_fmt(),
	       ri_kind_name[kind], call_stack[top - 1].fno, call_stack[top].fno,
	       da_live_page(pages[0]) ? heap[pages[0]].page->index : -2, da_live_page(pages[1]) ? heap[pages[1]].page->index : -2,
	       call_stack[top].return_address, f->address + 10, instr_ptr, ip);
	RI_EXPECT(call_stack_ptr == top + 1, "%s: %d frames, want %d", ri_fmt(), call_stack_ptr, top + 1);
	RI_EXPECT(call_stack[top - 1].fno == fno && call_stack[top].fno == fno, "%s: the frames came back as %d %d, want %d",
		  ri_fmt(), call_stack[top - 1].fno, call_stack[top].fno, fno);
	for (int i = 0; i < 2; i++) {
		struct page *p = da_live_page(pages[i]) ? heap[pages[i]].page : NULL;
		RI_EXPECT(p && p->type == LOCAL_PAGE && p->index == fno && p->nr_vars == f->nr_vars,
			  "%s: local page %d came back as function %d with %d slot(s), want %d with %d", ri_fmt(), pages[i],
			  p ? p->index : -2, p ? p->nr_vars : -1, fno, f->nr_vars);
	}
	RI_EXPECT(call_stack[top].return_address == f->address + 10, "%s: the second frame returns to %d, want %d",
		  ri_fmt(), call_stack[top].return_address, f->address + 10);
	RI_EXPECT(instr_ptr == ip, "%s: the instruction pointer came back as %d, want %d", ri_fmt(), instr_ptr, ip);
	return ri_bad;
}

static int ri3_more_rsm(void) { config.save_format = SAVE_FORMAT_RSM; return ri3_overload(RI_MORE); }
static int ri3_more_json(void) { config.save_format = SAVE_FORMAT_JSON; return ri3_overload(RI_MORE); }
static int ri3_fewer_rsm(void) { config.save_format = SAVE_FORMAT_RSM; return ri3_overload(RI_FEWER); }
static int ri3_fewer_json(void) { config.save_format = SAVE_FORMAT_JSON; return ri3_overload(RI_FEWER); }
static int ri3_types_rsm(void) { config.save_format = SAVE_FORMAT_RSM; return ri3_overload(RI_TYPES); }
static int ri3_types_json(void) { config.save_format = SAVE_FORMAT_JSON; return ri3_overload(RI_TYPES); }
static int ri3_same_rsm(void) { config.save_format = SAVE_FORMAT_RSM; return ri3_overload(RI_SAME); }
static int ri3_same_json(void) { config.save_format = SAVE_FORMAT_JSON; return ri3_overload(RI_SAME); }

/* The symbols the file holds for the local page in slot and in return record i. */
static const char *ri_page_symbol(struct rsave *rs, int slot)
{
	if (slot >= rs->nr_heap_objs || *(enum rsave_heap_tag *)rs->heap[slot] != RSAVE_LOCALS)
		return NULL;
	return ((struct rsave_heap_frame *)rs->heap[slot])->func.name;
}

static const char *ri_record_symbol(struct rsave *rs, int frame)
{
	/* frame k of the VM (0-based) is return record k + 1; the last one is ip */
	if (frame + 1 < rs->nr_return_records)
		return rs->return_records[frame + 1].caller_func;
	return frame + 1 == rs->nr_return_records ? rs->ip.caller_func : NULL;
}

static int ri4_bare_first(void)
{
	ri_bad = 0;
	config.save_format = SAVE_FORMAT_RSM;
	int fno = ri_overload(RI_SAME), first = ri_first_of_name(fno);
	int pages[2];
	SP_CHECK(ri_push_two(first, pages), "no frames for function %d", first);
	int top = call_stack_ptr - 1;
	SP_CHECK(ri_save("ri4.dat"), "the image was not written");
	struct rsave *rs = ri_read_rsave("ri4.dat");
	SP_CHECK(rs, "the image cannot be read");
	const char *name = ain->functions[first].name;
	const char *p0 = ri_page_symbol(rs, pages[0]), *r0 = ri_record_symbol(rs, top);
	printf("RI4 first overload %d: local page \"%s\", return record \"%s\"\n", first, p0 ? p0 : "(none)", r0 ? r0 : "(none)");
	RI_EXPECT(p0 && !strcmp(p0, name), "the local page symbol is not the bare name");
	RI_EXPECT(r0 && !strcmp(r0, name), "the return record symbol is not the bare name");
	rsave_free(rs);
	return ri_bad;
}

static int ri4_numbered(void)
{
	ri_bad = 0;
	config.save_format = SAVE_FORMAT_RSM;
	int fno = ri_overload(RI_MORE);
	int n = ain_get_function_index(ain, &ain->functions[fno]);
	char want[512];
	snprintf(want, sizeof want, "%s#%d", ain->functions[fno].name, n);
	int pages[2];
	SP_CHECK(ri_push_two(fno, pages), "no frames for function %d", fno);
	int top = call_stack_ptr - 1;
	SP_CHECK(ri_save("ri4n.dat"), "the image was not written");
	struct rsave *rs = ri_read_rsave("ri4n.dat");
	SP_CHECK(rs, "the image cannot be read");
	const char *p0 = ri_page_symbol(rs, pages[0]), *p1 = ri_page_symbol(rs, pages[1]);
	const char *r0 = ri_record_symbol(rs, top - 1), *r1 = ri_record_symbol(rs, top);
	printf("RI4 overload %d (#%d): local pages \"%s\" \"%s\", return records \"%s\" \"%s\"\n", fno, n, p0 ? p0 : "(none)",
	       p1 ? p1 : "(none)", r0 ? r0 : "(none)", r1 ? r1 : "(none)");
	RI_EXPECT(n > 0, "function %d is the first of its name", fno);
	RI_EXPECT(p0 && p1 && !strcmp(p0, want) && !strcmp(p1, want), "the local page symbols are not \"%s\"", want);
	RI_EXPECT(r0 && r1 && !strcmp(r0, want) && !strcmp(r1, want), "the return record symbols are not \"%s\"", want);
	rsave_free(rs);
	return ri_bad;
}

/* Cut "#n" off every function symbol of the file. */
static void ri_strip(char *s)
{
	char *h = s ? strchr(s, '#') : NULL;
	if (h)
		*h = 0;
}

static int ri5_bare_reads_first(void)
{
	ri_bad = 0;
	config.save_format = SAVE_FORMAT_RSM;
	int fno = ri_overload(RI_SAME), first = ri_first_of_name(fno);
	int pages[2];
	SP_CHECK(ri_push_two(fno, pages), "no frames for function %d", fno);
	int top = call_stack_ptr - 1;
	SP_CHECK(ri_save("ri5.dat"), "the image was not written");
	struct rsave *rs = ri_read_rsave("ri5.dat");
	SP_CHECK(rs, "the image cannot be read");
	for (int i = 0; i < rs->nr_heap_objs; i++) {
		if (*(enum rsave_heap_tag *)rs->heap[i] == RSAVE_LOCALS)
			ri_strip(((struct rsave_heap_frame *)rs->heap[i])->func.name);
	}
	for (int i = 0; i < rs->nr_return_records; i++)
		ri_strip(rs->return_records[i].caller_func);
	ri_strip(rs->ip.caller_func);
	SP_CHECK(ri_write_rsave(rs, "ri5.dat"), "the changed image was not written");
	rsave_free(rs);
	vm_load_image(RI_KEY, "ri5.dat");
	printf("RI5 same-shape overload %d written bare: frames %d %d, local pages %d %d (first of the name %d)\n", fno,
	       call_stack[top - 1].fno, call_stack[top].fno, da_live_page(pages[0]) ? heap[pages[0]].page->index : -2,
	       da_live_page(pages[1]) ? heap[pages[1]].page->index : -2, first);
	RI_EXPECT(call_stack_ptr == top + 1 && call_stack[top - 1].fno == first && call_stack[top].fno == first,
		  "the frames did not come back as the first of the name");
	RI_EXPECT(da_live_page(pages[0]) && heap[pages[0]].page->index == first && da_live_page(pages[1])
		  && heap[pages[1]].page->index == first, "the local pages did not come back as the first of the name");
	return ri_bad;
}

/* ---- RI6, RI7 free list and empty slots ---------------------------------- */

/* Every slot with no reference has no type and no pointer; the free list
 * holds exactly those of slot 2 and above. Prints and returns the count. */
static long ri_free_state(const char *label)
{
	size_t zero = 0, typed = 0;
	for (size_t i = 0; i < heap_size; i++) {
		if (heap[i].ref)
			continue;
		if (i >= 2)
			zero++;
		if (heap[i].type != 0 || heap[i].page != NULL)
			typed++;
	}
	size_t walk = 0, bad = 0;
	for (int32_t s = heap_free_head; s >= 0 && walk <= heap_size; s = (int32_t)heap[s].seq) {
		if ((size_t)s >= heap_size || s < 2 || heap[s].ref) {
			bad++;
			break;
		}
		walk++;
	}
	printf("RI %s %s: heap %zu, unreferenced %zu (with a type or pointer %zu), free count %zu, list walk %zu%s\n",
	       ri_fmt(), label, heap_size, zero, typed, heap_free_count, walk, bad ? ", list broken" : "");
	RI_EXPECT(typed == 0, "%s %s: %zu unreferenced slot(s) still have a type or a pointer", ri_fmt(), label, typed);
	RI_EXPECT(heap_free_count == zero && walk == zero && !bad, "%s %s: free count %zu, walk %zu, unreferenced %zu",
		  ri_fmt(), label, heap_free_count, walk, zero);
	return (long)heap_free_count;
}

#define RI6_LEFT 200     /* free slots left when the image is written */
#define RI6_INTRUDERS 120 /* strings that take image-empty slots before the load */

static int *ri6_strings;
static int ri6_nstrings;

/* Grow the heap to 16384 slots and fill it to RI6_LEFT free slots with
 * strings, without the collector (it would otherwise run before the load). */
static void ri6_fill(void)
{
	heap_gc_inhibit();
	ri6_strings = xcalloc(heap_size * 8, sizeof(int));
	while (heap_size < 16384 || heap_free_count > RI6_LEFT) {
		char text[32];
		snprintf(text, sizeof text, "ri6-%d", ri6_nstrings);
		ri6_strings[ri6_nstrings++] = heap_alloc_string(make_string(text, strlen(text)));
	}
}

static void ri6_intrude(void)
{
	for (int i = 0; i < RI6_INTRUDERS; i++)
		heap_alloc_string(make_string("ri6-intruder", 12));
	heap_gc_allow();
}

static bool ri6_strings_back(void)
{
	for (int i = 0; i < ri6_nstrings; i += 97) {
		char text[32];
		snprintf(text, sizeof text, "ri6-%d", i);
		int s = ri6_strings[i];
		if (!heap[s].ref || heap[s].type != VM_STRING || !heap[s].s || strcmp(heap[s].s->text, text))
			return false;
	}
	return true;
}

static int ri6_free_list(void)
{
	ri_bad = 0;
	ri6_fill();
	SP_CHECK(ri_save("ri6.dat"), "the %s image was not written", ri_fmt());
	ri6_intrude();
	vm_load_image(RI_KEY, "ri6.dat");
	ri_free_state("after the load");
	RI_EXPECT(ri6_strings_back(), "%s: the strings of the image did not come back", ri_fmt());
	size_t before = heap_size;
	for (int i = 0; i < 2000; i++)
		heap_alloc_string(make_string("ri6-after", 9));
	printf("RI6 %s: 2000 allocations after the load done, heap %zu -> %zu\n", ri_fmt(), before, heap_size);
	RI_EXPECT(ri6_strings_back(), "%s: the strings of the image changed after the allocations", ri_fmt());
	ri_free_state("after the allocations");
	return ri_bad;
}

static int ri6_rsm(void) { config.save_format = SAVE_FORMAT_RSM; return ri6_free_list(); }
static int ri6_json(void) { config.save_format = SAVE_FORMAT_JSON; return ri6_free_list(); }

static int ri7_both(void)
{
	ri_bad = 0;
	ri6_fill();
	config.save_format = SAVE_FORMAT_RSM;
	SP_CHECK(ri_save("ri7.rsm"), "the rsm image was not written");
	config.save_format = SAVE_FORMAT_JSON;
	SP_CHECK(ri_save("ri7.json"), "the json image was not written");
	ri6_intrude();
	heap_gc_inhibit(); /* the counts compared are the loads', not a collection's */
	config.save_format = SAVE_FORMAT_RSM;
	vm_load_image(RI_KEY, "ri7.rsm");
	long rsm = (long)heap_free_count;
	vm_load_image(RI_KEY, "ri7.json");
	long json = (long)heap_free_count;
	printf("RI7 free count after the rsm load %ld, after the json load %ld\n", rsm, json);
	RI_EXPECT(rsm == json, "the free counts differ: rsm %ld, json %ld", rsm, json);
	heap_gc_allow();
	return ri_bad;
}

/* ---- RI8 the guard page -------------------------------------------------- */

static bool ri_guard_ok(void)
{
	struct page *p = heap[0].page;
	if (heap[0].ref != 1 || heap[0].type != VM_PAGE || !p || p->type != ARRAY_PAGE || p->a_type != AIN_VOID
	    || p->nr_vars != 64 || p->array.rank != 0 || p->array.struct_type != 0 || p->array.elem_slots != 0)
		return false;
	for (int i = 0; i < 64; i++) {
		if (p->values[i].i)
			return false;
	}
	return true;
}

static void ri_guard_describe(const char *label)
{
	struct page *p = heap[0].page;
	int nonzero = 0;
	for (int i = 0; p && i < p->nr_vars; i++)
		nonzero += p->values[i].i != 0;
	printf("RI8 %s %s: slot 0 ref %d type %d page %s", ri_fmt(), label, heap[0].ref, heap[0].type, p ? "yes" : "NULL");
	if (p)
		printf(" (type %d, element %d, %d slots, rank %d, struct %d, %d nonzero)", p->type, p->a_type, p->nr_vars,
		       p->array.rank, p->array.struct_type, nonzero);
	printf("\n");
}

static int ri8_guard(bool written)
{
	ri_bad = 0;
	SP_CHECK(ri_guard_ok(), "slot 0 is not the guard page before the image");
	if (written) {
		for (int i = 0; i < 64; i++)
			heap[0].page->values[i].i = 7;
	}
	SP_CHECK(ri_save("ri8.dat"), "the %s image was not written", ri_fmt());
	vm_load_image(RI_KEY, "ri8.dat");
	ri_guard_describe(written ? "after writes it absorbed" : "untouched");
	RI_EXPECT(ri_guard_ok(), "%s: slot 0 is not the guard page after the load", ri_fmt());
	if (heap[0].page && heap[0].page->nr_vars == 64)
		heap[0].page->values[5].i = 1; /* it still absorbs a write */
	return ri_bad;
}

/* A 64-slot two-dimensional array page that is live at the load but not in
 * the image: the load frees it, and the page cache may hand it out again. */
static int ri8_reused(void)
{
	ri_bad = 0;
	SP_CHECK(ri_save("ri8r.dat"), "the %s image was not written", ri_fmt());
	struct page *p = alloc_page(ARRAY_PAGE, AIN_ARRAY_INT, 64);
	p->array.rank = 2;
	p->array.struct_type = 5;
	heap_alloc_page(p);
	vm_load_image(RI_KEY, "ri8r.dat");
	ri_guard_describe("after a load that freed another 64-slot page");
	RI_EXPECT(ri_guard_ok(), "%s: slot 0 is not the guard page after the load", ri_fmt());
	return ri_bad;
}

static int ri8_reused_rsm(void) { config.save_format = SAVE_FORMAT_RSM; return ri8_reused(); }
static int ri8_reused_json(void) { config.save_format = SAVE_FORMAT_JSON; return ri8_reused(); }
static int ri8_rsm(void) { config.save_format = SAVE_FORMAT_RSM; return ri8_guard(false); }
static int ri8_json(void) { config.save_format = SAVE_FORMAT_JSON; return ri8_guard(false); }
static int ri8_written_rsm(void) { config.save_format = SAVE_FORMAT_RSM; return ri8_guard(true); }
static int ri8_written_json(void) { config.save_format = SAVE_FORMAT_JSON; return ri8_guard(true); }

/* ---- RI9, RI10 delegate targets ------------------------------------------ */

static int ri_entries_for(int dg, int obj)
{
	struct page *p = da_live_page(dg) ? heap[dg].page : NULL;
	int n = 0;
	for (int i = 0; p && i + 2 < p->nr_vars; i += 3)
		n += p->values[i].i == obj;
	return n;
}

static int ri9_released_before_load(void)
{
	ri_bad = 0;
	int c = alloc_struct(ri_plain_struct());
	int d = heap_alloc_page(delegate_new_from_method(c, ain->main));
	SP_CHECK(ri_entries_for(d, c) == 1, "the delegate has no entry for the object");
	SP_CHECK(ri_save("ri9.dat"), "the %s image was not written", ri_fmt());
	heap_unref(c);
	SP_CHECK(ri_entries_for(d, c) == 0, "releasing the object before the load left the delegate's entry");
	vm_load_image(RI_KEY, "ri9.dat");
	SP_CHECK(da_live_page(c) && ri_entries_for(d, c) == 1, "the object or the delegate's entry did not come back");
	heap_unref(c);
	printf("RI9 %s: after the load, releasing object %d leaves %d entr%s for it in delegate %d\n", ri_fmt(), c,
	       ri_entries_for(d, c), ri_entries_for(d, c) == 1 ? "y" : "ies", d);
	RI_EXPECT(ri_entries_for(d, c) == 0, "%s: the delegate still has an entry for the released object", ri_fmt());
	return ri_bad;
}

static int ri9_rsm(void) { config.save_format = SAVE_FORMAT_RSM; return ri9_released_before_load(); }
static int ri9_json(void) { config.save_format = SAVE_FORMAT_JSON; return ri9_released_before_load(); }

static int ri10_unrelated(void)
{
	ri_bad = 0;
	int sno = ri_plain_struct();
	int b = alloc_struct(sno), g = alloc_struct(sno);
	int f = heap_alloc_page(delegate_new_from_method(g, ain->main));
	SP_CHECK(ri_save("ri10.dat"), "the %s image was not written", ri_fmt());
	heap_set_page(f, delegate_append(heap[f].page, b, ain->main));
	SP_CHECK(ri_entries_for(f, b) == 1 && ri_entries_for(f, g) == 1, "the delegate did not take the second entry");
	vm_load_image(RI_KEY, "ri10.dat");
	SP_CHECK(ri_entries_for(f, b) == 0 && ri_entries_for(f, g) == 1, "the delegate did not come back as written");
	heap_unref(b);
	int left_g = ri_entries_for(f, g);
	heap_unref(g);
	printf("RI10 %s: releasing %d (registered only after the image) leaves %d entry for %d; releasing %d leaves %d\n",
	       ri_fmt(), b, left_g, g, g, ri_entries_for(f, g));
	RI_EXPECT(left_g == 1, "%s: releasing an object the image does not target dropped another entry", ri_fmt());
	RI_EXPECT(ri_entries_for(f, g) == 0, "%s: releasing the image's target left its entry", ri_fmt());
	return ri_bad;
}

static int ri10_rsm(void) { config.save_format = SAVE_FORMAT_RSM; return ri10_unrelated(); }
static int ri10_json(void) { config.save_format = SAVE_FORMAT_JSON; return ri10_unrelated(); }

/* ---- RI11 call frames ----------------------------------------------------- */

/* An image of one frame of ain->main, rewritten to hold n frames of it. */
static bool ri11_rsm_frames(const char *file, int n)
{
	int slot = _function_call(ain->main, VM_RETURN);
	if (slot <= 1 || !ri_save(file))
		return false;
	struct rsave *rs = ri_read_rsave(file);
	if (!rs || rs->nr_call_frames != 2 || rs->nr_return_records != 1)
		return false;
	struct rsave_return_record bottom = rs->return_records[0];
	struct rsave_return_record ip = rs->ip;
	free(rs->call_frames);
	free(rs->return_records);
	rs->nr_call_frames = n + 1;
	rs->call_frames = xcalloc(n + 1, sizeof(struct rsave_call_frame));
	rs->call_frames[0].type = RSAVE_CALL_STACK_BOTTOM;
	rs->call_frames[0].local_ptr = -1;
	rs->nr_return_records = n;
	rs->return_records = xcalloc(n, sizeof(struct rsave_return_record));
	rs->return_records[0] = bottom;
	for (int i = 1; i <= n; i++) {
		rs->call_frames[i].type = RSAVE_FUNCTION_CALL;
		rs->call_frames[i].local_ptr = slot;
		rs->call_frames[i].struct_ptr = -1;
		if (i < n) {
			rs->return_records[i] = ip;
			rs->return_records[i].caller_func = strdup(ip.caller_func);
			rs->return_records[i].local_addr = 10;
		}
	}
	bool ok = ri_write_rsave(rs, file);
	rsave_free(rs);
	return ok;
}

static bool ri11_json_frames(const char *file, int n)
{
	int slot = _function_call(ain->main, VM_RETURN);
	if (slot <= 1 || !ri_save(file))
		return false;
	char path[PATH_MAX];
	ri_path(path, sizeof path, file);
	char *text = file_read(path, NULL);
	cJSON *image = text ? cJSON_Parse(text) : NULL;
	free(text);
	cJSON *frames = image ? cJSON_GetObjectItem(image, "call-stack") : NULL;
	if (!frames || cJSON_GetArraySize(frames) != 1)
		return false;
	cJSON *one = cJSON_GetArrayItem(frames, 0);
	cJSON *many = cJSON_CreateArray();
	for (int i = 0; i < n; i++)
		cJSON_AddItemToArray(many, cJSON_Duplicate(one, 1));
	cJSON_ReplaceItemInObject(image, "call-stack", many);
	char *out = cJSON_PrintUnformatted(image);
	cJSON_Delete(image);
	FILE *fp = fopen(path, "wb");
	bool ok = fp && fputs(out, fp) >= 0;
	if (fp)
		fclose(fp);
	free(out);
	return ok;
}

static int ri11_frames(int n)
{
	ri_bad = 0;
	bool rsm = config.save_format == SAVE_FORMAT_RSM;
	SP_CHECK(rsm ? ri11_rsm_frames("ri11.dat", n) : ri11_json_frames("ri11.dat", n), "the %s image of %d frames was "
		 "not made", ri_fmt(), n);
	printf("RI11 %s: an image of %d call frames (the call stack holds %d)\n", ri_fmt(), n, RI_CALL_STACK_MAX);
	if (n > RI_CALL_STACK_MAX) {
		ri_expect_refused("ri11.dat", "too many call frames");
	} else {
		vm_load_image(RI_KEY, "ri11.dat");
		RI_EXPECT(call_stack_ptr == n, "%s: %d frames after the load, want %d", ri_fmt(), call_stack_ptr, n);
		call_stack_ptr = 0; /* nothing below may unwind these */
	}
	return ri_bad;
}

static int ri11_over_rsm(void) { config.save_format = SAVE_FORMAT_RSM; return ri11_frames(RI_CALL_STACK_MAX + 1); }
static int ri11_over_json(void) { config.save_format = SAVE_FORMAT_JSON; return ri11_frames(RI_CALL_STACK_MAX + 1); }
static int ri11_full_rsm(void) { config.save_format = SAVE_FORMAT_RSM; return ri11_frames(RI_CALL_STACK_MAX); }
static int ri11_full_json(void) { config.save_format = SAVE_FORMAT_JSON; return ri11_frames(RI_CALL_STACK_MAX); }

static int test_resume_image(void)
{
	sp_setup_dir();
	sp_run("RI1 rsm wrap boxes", ri1_rsm);
	sp_run("RI1 json wrap boxes", ri1_json);
	sp_run("RI2 rsm struct name not in the AIN", ri2_struct);
	sp_run("RI2 rsm function name not in the AIN", ri2_function);
	sp_run("RI2 rsm overload number past its name", ri2_number);
	sp_run("RI3 rsm overload, more variables", ri3_more_rsm);
	sp_run("RI3 json overload, more variables", ri3_more_json);
	sp_run("RI3 rsm overload, fewer variables", ri3_fewer_rsm);
	sp_run("RI3 json overload, fewer variables", ri3_fewer_json);
	sp_run("RI3 rsm overload, other types", ri3_types_rsm);
	sp_run("RI3 json overload, other types", ri3_types_json);
	sp_run("RI3 rsm overload, same shape", ri3_same_rsm);
	sp_run("RI3 json overload, same shape", ri3_same_json);
	sp_run("RI4 rsm first overload written bare", ri4_bare_first);
	sp_run("RI4 rsm later overload written name#n", ri4_numbered);
	sp_run("RI5 rsm bare symbol reads the first", ri5_bare_reads_first);
	sp_run("RI6 rsm free list and empty slots", ri6_rsm);
	sp_run("RI6 json free list and empty slots", ri6_json);
	sp_run("RI7 rsm and json free counts agree", ri7_both);
	sp_run("RI8 rsm guard page", ri8_rsm);
	sp_run("RI8 json guard page", ri8_json);
	sp_run("RI8 rsm guard page after absorbed writes", ri8_written_rsm);
	sp_run("RI8 json guard page after absorbed writes", ri8_written_json);
	sp_run("RI8 rsm guard page from a reused page", ri8_reused_rsm);
	sp_run("RI8 json guard page from a reused page", ri8_reused_json);
	sp_run("RI9 rsm target released before the load", ri9_rsm);
	sp_run("RI9 json target released before the load", ri9_json);
	sp_run("RI10 rsm registration after the image", ri10_rsm);
	sp_run("RI10 json registration after the image", ri10_json);
	sp_run("RI11 rsm one frame over the call stack", ri11_over_rsm);
	sp_run("RI11 json one frame over the call stack", ri11_over_json);
	sp_run("RI11 rsm frames filling the call stack", ri11_full_rsm);
	sp_run("RI11 json frames filling the call stack", ri11_full_json);
	sp_remove_dir();
	printf("RESUME-IMAGE summary: %d/%d case(s) failed\n", sp_failed_cases, sp_cases);
	return sp_failed_cases ? 88 : 0;
}
