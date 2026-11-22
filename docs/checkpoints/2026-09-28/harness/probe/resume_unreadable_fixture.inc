/* resume-unreadable: engine images (system.ResumeSave / ResumeLoad) that are
 * written in an unusual shape, that cannot be written, and that cannot be
 * read back. Include after resume_image_fixture.inc, resume_frames_fixture.inc
 * and resume_c_entry_fixture.inc (it uses their helpers).
 *
 * What a read-back that fails has to do (the port): tell the player which
 * file could not be read and end the program; it must not return to the
 * script, crash, hang or come back with wrong data. In the original's
 * executable (the release this port runs), ResumeLoad (0x672960) pushes no
 * result when the read fails, and 0x668d30 shows a message box naming the
 * file and clears the VM's run flag unless a field of the VM is 2; for a
 * function checksum that does not match it shows system.Error's box instead,
 * after which it can go back to the script or stop (the port always ends
 * there, its own handling). The one failure the port returns from
 * is an rsm image of a version it does not read (the original's own images
 * are version 14): the VM is left as it was, nothing is pushed and the script
 * goes on (the port's own handling; it can only meet such a file left from
 * another run).
 * Here the box goes through the stand-in of vm.c (vm_ask_test.show), which
 * prints it and the VM's frames, stack and live slots at that moment, and
 * each load runs in a further process under a time limit: a failed load must
 * end that process with exit 1 after the box, with the reason in the log
 * ("Failed to read VM image" and the case's own words), no warning of a
 * double free, no sanitizer report and no return from the load.
 *
 * Cases (rsm is the default format, json the other):
 *   C entry  A frame entered from C (return address VM_RETURN) above the
 *            bottom frame, in the middle of the call stack and on top of it:
 *            the image reads back with every frame's function, return
 *            address, local page and struct page, and the instruction
 *            pointer (rsm and json).
 *   Save     ResumeSave through CALLHLL returns true and sets its third
 *            argument (the original's 0x6727e0): 1 when the image is
 *            written, then read back through CALLHLL the script goes on
 *            after the save with false; the same through the system calls
 *            of programs before v14; 0 in a folder that cannot be written,
 *            and true still. A write that fails part way (a file size
 *            limit) is reported as failed, leaves the previous image as it
 *            was and no temporary file (both formats); so does a file that
 *            cannot be put in place (a folder of its name). The writer
 *            refuses an image with no call frame and one with a frame of no
 *            function (rsm), and writes a string slot without a string as
 *            an empty string (json).
 *   Not read  An rsm image of version 14, read directly (the VM as it was),
 *            through CALLHLL and through the system call: no box, the
 *            script goes on after ResumeLoad with the value under its
 *            arguments still on top of the stack; through the system call
 *            its two strings are released once each.
 *   Unreadable  (each must end through the box) a file that is not there,
 *            through CALLHLL (the script must not go on) and directly, also
 *            with the box in Japanese and Chinese and with the box answered
 *            by XSYS4_AUTO_MSGBOX (not shown, only logged); a truncated
 *            file; another key; more call frames than the call stack holds;
 *            a return record count that does not fit the frames; a bottom
 *            return record that is not one; only the bottom frame; a single
 *            frame with as many return records as frames; a return address
 *            past the code and before its function; an instruction pointer
 *            that is not an instruction (a return address is only checked
 *            to lie in the code from its function's start on: images the
 *            other modes make return to offsets that are not instructions);
 *            a frame whose local page is not a live page, or has fewer slots
 *            than its function has variables, or keeps a struct pointer past
 *            the heap, and one whose struct page is past the heap (a local
 *            page of another type with as many slots as variables reads back:
 *            the page type is not checked); a string of length 0; a file
 *            with comments only; a globals page and a struct page with fewer
 *            slots than variables or members, the latter also through
 *            CALLHLL with a frame on the call stack (the half-read heap,
 *            the frames and the stack are dropped before the box) and
 *            through the system call (whose file name is the text of a heap
 *            string the load frees; the box still names the file); an array
 *            of a struct not in the AIN; a return record without a function
 *            (what an earlier build wrote for a frame entered from C above
 *            the bottom), with a checksum that does not match, of a function
 *            not in the AIN; more heap slots than the reader takes; data
 *            libsys4's parser stops on (a string it does not expect, which
 *            it would end the program on, and a globals page it gives up
 *            on, where its own free would read a NULL entry); json: a
 *            negative heap slot (would not end), a heap slot past the
 *            reader's bound, a heap slot given twice, a heap that is not an
 *            array, a page type that is not one, fewer than two stack values,
 *            a frame of no function, no frame, text that does not parse.
 *   Limits   Negative slot counts and negative string lengths: libsys4's
 *            parser writes past its allocation before the port sees them
 *            (the sanitizer reports it there). These cases only require
 *            that the load never comes back, and end through the box with no
 *            sanitizer report or with a report whose every sanitizer SUMMARY
 *            line names libsys4's savefile.c.
 */

#include <sys/resource.h>
#include <signal.h>

#define RU_KEY RI_KEY
#define RU_LIMIT 20	/* seconds a load may take */

static int ru_bad;
#define RU_EXPECT(c, ...) do { if (!(c)) { printf("  FAIL %s:%d: ", __func__, __LINE__); printf(__VA_ARGS__); \
		printf("\n"); fflush(stdout); ru_bad = 1; } } while (0)

static const char *ru_fmt(void) { return config.save_format == SAVE_FORMAT_RSM ? "rsm" : "json"; }

/* ---- the box and the marks of a program ---------------------------------- */

static int ru_show(const SDL_MessageBoxData *box, int *button)
{
	printf("RU BOX buttons=%d text=%s\n", box->numbuttons, box->message ? box->message : "(null)");
	printf("RU BOX state frames=%d stack=%d live=%zu\n", call_stack_ptr, stack_ptr, live_slots());
	fflush(stdout);
	*button = 1;
	return 0;
}

static void ru_box_hook(void)
{
	vm_ask_test.show = ru_show;
	vm_ask_test.language = "en";
	vm_ask_test.country = "";
	unsetenv("XSYS4_AUTO_MSGBOX");
}

/* Run fn in a further process (stdout and stderr in a file, the box hook
 * set, RU_LIMIT seconds); its output goes to out. Returns the wait status. */
static char ru_out[262144];
static int ru_elsewhere(void (*fn)(void))
{
	char log[PATH_MAX];
	ri_path(log, sizeof log, "ru-output.txt");
	fflush(stdout);
	fflush(stderr);
	pid_t pid = fork();
	if (pid == 0) {
		int fd = open(log, O_WRONLY | O_CREAT | O_TRUNC, 0600);
		if (fd < 0)
			_exit(3);
		dup2(fd, 1);
		dup2(fd, 2);
		alarm(RU_LIMIT);
		ru_box_hook();
		fn();
		printf("RU returned\n");
		fflush(stdout);
		_exit(0);
	}
	int st = 0;
	waitpid(pid, &st, 0);
	ru_out[0] = 0;
	FILE *fp = fopen(log, "rb");
	if (fp) {
		size_t n = fread(ru_out, 1, sizeof ru_out - 1, fp);
		ru_out[n] = 0;
		fclose(fp);
	}
	/* the further process's output, indented; a sanitizer report there is
	 * judged by the case, so its words are changed here and the harness's
	 * count of reports covers this process only */
	int shown = 0;
	for (char *line = ru_out; *line && shown < 40; shown++) {
		char *nl = strchr(line, '\n');
		int n = nl ? (int)(nl - line) : (int)strlen(line);
		char buf[512];
		snprintf(buf, sizeof buf, "%.*s", n, line);
		for (char *p; (p = strstr(buf, "Sanitizer")) || (p = strstr(buf, "runtime error")); )
			memcpy(p, *p == 'S' ? "SANITIZER" : "RUNTIME-ERROR", *p == 'S' ? 9 : 13);
		printf("    | %s\n", buf);
		if (!nl)
			break;
		line = nl + 1;
	}
	return st;
}

static void ru_print_status(const char *what, int st)
{
	if (WIFSIGNALED(st))
		printf("RU %s %s: %s %d\n", ru_fmt(), what, WTERMSIG(st) == SIGALRM ? "did not end, stopped by" : "signal",
		       WTERMSIG(st));
	else
		printf("RU %s %s: exit %d%s%s\n", ru_fmt(), what, WEXITSTATUS(st), strstr(ru_out, "RU BOX") ? ", box shown" : "",
		       strstr(ru_out, "RU returned") ? ", the load returned" : "");
}

static bool ru_sanitizer(void) { return strstr(ru_out, "Sanitizer") || strstr(ru_out, "runtime error"); }

/* The load ended as an unreadable image must: exit 1 after the box, which
 * names the file, with the reason in the log, no return, no sanitizer
 * report; want (if not NULL) is in the log. */
static void ru_expect_unreadable(const char *what, int st, const char *file, const char *want)
{
	ru_print_status(what, st);
	RU_EXPECT(WIFEXITED(st) && WEXITSTATUS(st) == 1, "%s %s: the load ended with %s %d, want exit 1", ru_fmt(), what,
		  WIFSIGNALED(st) ? "signal" : "exit", WIFSIGNALED(st) ? WTERMSIG(st) : WEXITSTATUS(st));
	const char *box = strstr(ru_out, "RU BOX buttons=1 text=");
	const char *nl = box ? strchr(box, '\n') : NULL;
	const char *name = box ? strstr(box, file) : NULL;
	RU_EXPECT(box && name && (!nl || name < nl), "%s %s: no box with one button naming %s", ru_fmt(), what, file);
	RU_EXPECT(strstr(ru_out, "Failed to read VM image"), "%s %s: the log does not give the reason", ru_fmt(), what);
	RU_EXPECT(!want || strstr(ru_out, want), "%s %s: the log does not say \"%s\"", ru_fmt(), what, want ? want : "");
	RU_EXPECT(!strstr(ru_out, "RU returned") && !strstr(ru_out, "RU MARK after-load"),
		  "%s %s: the load came back to its caller", ru_fmt(), what);
	RU_EXPECT(!strstr(ru_out, "double free"), "%s %s: a double free at the end", ru_fmt(), what);
	RU_EXPECT(!ru_sanitizer(), "%s %s: a sanitizer report", ru_fmt(), what);
}

/* The load was not made (an image of a version not read): no box, no
 * failure, the reason in the log, the program went on to its end. */
static void ru_expect_not_read(const char *what, int st)
{
	ru_print_status(what, st);
	RU_EXPECT(WIFEXITED(st) && WEXITSTATUS(st) == 0 && strstr(ru_out, "RU returned"),
		  "%s %s: the program did not go on to its end", ru_fmt(), what);
	RU_EXPECT(!strstr(ru_out, "RU BOX"), "%s %s: a box", ru_fmt(), what);
	RU_EXPECT(strstr(ru_out, "Unsupported save format") && !strstr(ru_out, "Failed to read VM image"),
		  "%s %s: the log does not say the image is of a version not read", ru_fmt(), what);
	RU_EXPECT(!ru_sanitizer(), "%s %s: a sanitizer report", ru_fmt(), what);
}

/* ---- programs that call ResumeSave and ResumeLoad through CALLHLL --------- */

static struct { uint32_t after_save, after_load, saved, loaded; int fn; bool on; } ru_prog;
static int ru_marks[8], ru_mark_top[8], ru_mark_var0[8], ru_nmarks;

static void ru_mark(const char *name, int id)
{
	int top = stack_ptr > 0 ? stack[stack_ptr - 1].i : -77777;
	int v0 = call_stack_ptr > 0 ? rf_member(call_stack[call_stack_ptr - 1].page_slot, 0) : -77777;
	printf("RU MARK %s top=%d var0=%d depth=%d\n", name, top, v0, stack_ptr);
	fflush(stdout);
	if (ru_nmarks < 8) {
		ru_mark_top[ru_nmarks] = top;
		ru_mark_var0[ru_nmarks] = v0;
		ru_marks[ru_nmarks++] = id;
	}
}

static void resume_unreadable_probe_step(void)
{
	if (!ru_prog.on)
		return;
	uint32_t ip = instr_ptr;
	if (ip == ru_prog.after_save)
		ru_mark("after-save", 1);
	else if (ip == ru_prog.after_load)
		ru_mark("after-load", 2);
	else if (ip == ru_prog.saved)
		ru_mark("saved", 3);
	else if (ip == ru_prog.loaded)
		ru_mark("loaded", 4);
}

static int ru_lib_fn(const char *name)
{
	int lib = ain_get_library(ain, "system");
	if (lib < 0)
		return -1;
	for (int i = 0; i < ain->libraries[lib].nr_functions; i++) {
		if (ain->libraries[lib].functions[i].name && !strcmp(ain->libraries[lib].functions[i].name, name))
			return i;
	}
	return -1;
}

static int ru_string(const char *s)
{
	int slot = heap_alloc_string(make_string(s, strlen(s)));
	heap_ref(slot);
	heap_ref(slot);
	return slot;
}

static int ru_sentinel;	/* if not 0, pushed under ResumeLoad's arguments and popped after it */
static bool ru_callsys;	/* ResumeSave and ResumeLoad as the system calls of programs before v14 */
static int ru_key_slot, ru_lpath_slot;	/* ResumeLoad's two strings in the last ru_program */

static void ru_emit_load(int key, int path)
{
	if (ru_sentinel)
		rf_emit(PUSH, 1, ru_sentinel);
	rf_emit(PUSH, 1, key);
	rf_emit(PUSH, 1, path);
	if (ru_callsys)
		rf_emit(CALLSYS, 1, SYS_RESUME_LOAD);
	else
		rf_emit(CALLHLL, 3, ain_get_library(ain, "system"), ru_lib_fn("ResumeLoad"), 0);
	ru_prog.after_load = rf_here();
	rf_emit(PUSH, 1, 0);
	rf_emit(POP, 0);
	if (ru_sentinel)
		rf_emit(POP, 0);
}

/* A function of the game (int variables only) whose body becomes:
 *   save (with_save): ResumeSave(key, file, &var0); after-save; IFZ loaded;
 *     saved: [ResumeLoad(key, load_file); after-load;] RETURN
 *     loaded: RETURN
 *   load only: ResumeLoad(key, load_file); after-load; RETURN
 * (with ru_callsys the system calls instead; with ru_sentinel that value is
 * pushed before ResumeLoad's arguments and popped after after-load) */
static int ru_program(bool with_save, const char *file, const char *load_file)
{
	int fn = rf_pick(AIN_VOID, 1, NULL, 0);
	int key = ru_string(RU_KEY), path = ru_string(file ? file : ""), lpath = ru_string(load_file ? load_file : "");
	ru_key_slot = key;
	ru_lpath_slot = lpath;
	rf_len = 0;
	uint32_t start = rf_here();
	if (with_save) {
		rf_emit(PUSH, 1, key);
		rf_emit(PUSH, 1, path);
		rf_emit(PUSHLOCALPAGE, 0);
		rf_emit(PUSH, 1, 0);
		if (ru_callsys)
			rf_emit(CALLSYS, 1, SYS_RESUME_SAVE);
		else
			rf_emit(CALLHLL, 3, ain_get_library(ain, "system"), ru_lib_fn("ResumeSave"), 0);
		ru_prog.after_save = rf_here();
		uint32_t ifz = rf_emit(IFZ, 1, 0);
		ru_prog.saved = rf_here();
		rf_marker();
		if (load_file)
			ru_emit_load(key, lpath);
		rf_emit(RETURN, 0);
		ru_prog.loaded = rf_here();
		LittleEndian_putDW(rf_code, ifz + 2 - ain->code_size, (int)ru_prog.loaded);
		rf_marker();
		rf_emit(RETURN, 0);
	} else {
		ru_emit_load(key, lpath);
		rf_emit(RETURN, 0);
	}
	uint8_t *code = xmalloc(ain->code_size + rf_len);
	memcpy(code, ain->code, ain->code_size);
	memcpy(code + ain->code_size, rf_code, rf_len);
	ain->code = code;
	ain->code_size += rf_len;
	ain->functions[fn].address = start;
	ru_prog.fn = fn;
	return fn;
}

static void ru_run_program(void)
{
	stack_ptr = 0;
	call_stack_ptr = 0;
	ru_prog.on = true;
	if (_function_call(ru_prog.fn, VM_RETURN) <= 1) {
		printf("RU the program could not be called\n");
		exit(2);
	}
	vm_execute();
	ru_prog.on = false;
}

/* ---- images ---------------------------------------------------------------- */

static int ru_pick_n(int n, int *fnos)
{
	for (int i = 0; i < n; i++)
		fnos[i] = rf_pick(AIN_VOID, 0, fnos, i);
	return n;
}

/* The address just after the first instruction of fno: a place a call
 * returns to. */
static uint32_t ru_ret_in(int fno)
{
	uint32_t a = ain->functions[fno].address;
	return a + instruction_width((uint16_t)LittleEndian_getW(ain->code, a));
}

/* Frames of fnos[0..n), frame c_entry (if > 0) entered from C; the
 * instruction pointer at the start of the top function. */
static void ru_push_frames(const int *fnos, int n, int c_entry)
{
	stack_ptr = 0;
	call_stack_ptr = 0;
	for (int i = 0; i < n; i++) {
		uint32_t ret = i == 0 || i == c_entry ? VM_RETURN : ru_ret_in(fnos[i - 1]);
		if (_function_call(fnos[i], ret) <= 1) {
			printf("RU no frame for function %d\n", fnos[i]);
			exit(2);
		}
	}
	instr_ptr = ain->functions[fnos[n - 1]].address;
}

static struct { int str, obj, arr, st; } ru_objs;

/* Two frames (the second returning into the first), a string, an object of
 * a struct with two or more int members and an int array of three. */
static bool ru_image(const char *file)
{
	int fnos[2];
	ru_pick_n(2, fnos);
	ru_push_frames(fnos, 2, -1);
	ru_objs.str = ru_string("a string");
	ru_objs.st = rf_struct_two_ints();
	ru_objs.obj = alloc_struct(ru_objs.st);
	heap_ref(ru_objs.obj);
	union vm_value dims = { .i = 3 };
	ru_objs.arr = heap_alloc_page(alloc_array(1, &dims, AIN_ARRAY_INT, -1, false));
	heap_ref(ru_objs.arr);
	return ri_save(file);
}

static bool ru_file_exists(const char *file)
{
	char path[PATH_MAX];
	ri_path(path, sizeof path, file);
	return access(path, F_OK) == 0;
}

static uint8_t *ru_bytes(const char *file, size_t *n)
{
	char path[PATH_MAX];
	ri_path(path, sizeof path, file);
	*n = 0;
	return file_read(path, n);
}

static bool ru_put_bytes(const char *file, const uint8_t *data, size_t n)
{
	char path[PATH_MAX];
	ri_path(path, sizeof path, file);
	FILE *fp = fopen(path, "wb");
	bool ok = fp && fwrite(data, 1, n, fp) == n;
	if (fp && fclose(fp))
		ok = false;
	return ok;
}

static cJSON *ru_json(const char *file)
{
	size_t n;
	char *text = (char *)ru_bytes(file, &n);
	cJSON *j = text ? cJSON_Parse(text) : NULL;
	free(text);
	return j;
}

static bool ru_put_json(const char *file, cJSON *j)
{
	char *out = cJSON_PrintUnformatted(j);
	bool ok = out && ru_put_bytes(file, (uint8_t *)out, strlen(out));
	free(out);
	cJSON_Delete(j);
	return ok;
}

/* ---- C entry ---------------------------------------------------------------- */

static struct { int n, fno[4], page[4], st[4]; uint32_t ret[4], ip; const char *file; } ru_want;

static void ru_load_and_compare(void)
{
	vm_load_image(RU_KEY, ru_want.file);
	bool ok = call_stack_ptr == ru_want.n && instr_ptr == ru_want.ip;
	for (int i = 0; ok && i < ru_want.n; i++)
		ok = call_stack[i].fno == ru_want.fno[i] && call_stack[i].return_address == ru_want.ret[i]
			&& call_stack[i].page_slot == ru_want.page[i] && call_stack[i].struct_page == ru_want.st[i];
	printf("RU loaded %d frames (want %d), ip %u (want %u)\n", call_stack_ptr, ru_want.n, (unsigned)instr_ptr, ru_want.ip);
	for (int i = 0; i < call_stack_ptr && i < 4; i++)
		printf("RU frame %d: function %d return %u page %d struct %d; want %d %u %d %d\n", i, call_stack[i].fno,
		       call_stack[i].return_address, call_stack[i].page_slot, call_stack[i].struct_page,
		       i < ru_want.n ? ru_want.fno[i] : -1, i < ru_want.n ? ru_want.ret[i] : 0,
		       i < ru_want.n ? ru_want.page[i] : -1, i < ru_want.n ? ru_want.st[i] : -1);
	fflush(stdout);
	call_stack_ptr = 0; /* nothing below may unwind these */
	_exit(ok ? 0 : 3);
}

static int ru_c_entry(int n, int c_entry)
{
	ru_bad = 0;
	int fnos[4];
	ru_pick_n(n, fnos);
	ru_push_frames(fnos, n, c_entry);
	ru_want.n = n;
	for (int i = 0; i < n; i++) {
		ru_want.fno[i] = call_stack[i].fno;
		ru_want.ret[i] = call_stack[i].return_address;
		ru_want.page[i] = call_stack[i].page_slot;
		ru_want.st[i] = call_stack[i].struct_page;
	}
	ru_want.ip = instr_ptr;
	ru_want.file = "ru-centry.dat";
	SP_CHECK(ri_save(ru_want.file), "the %s image was not written", ru_fmt());
	int st = ru_elsewhere(ru_load_and_compare);
	ru_print_status(c_entry == n - 1 ? "C entry on top" : "C entry in the middle", st);
	RU_EXPECT(WIFEXITED(st) && WEXITSTATUS(st) == 0, "%s: the image did not read back as written", ru_fmt());
	RU_EXPECT(!ru_sanitizer(), "%s: a sanitizer report", ru_fmt());
	return ru_bad;
}

static int ru_centry_mid_rsm(void) { config.save_format = SAVE_FORMAT_RSM; return ru_c_entry(4, 2); }
static int ru_centry_top_rsm(void) { config.save_format = SAVE_FORMAT_RSM; return ru_c_entry(3, 2); }
static int ru_centry_mid_json(void) { config.save_format = SAVE_FORMAT_JSON; return ru_c_entry(4, 2); }
static int ru_centry_top_json(void) { config.save_format = SAVE_FORMAT_JSON; return ru_c_entry(3, 2); }

/* ---- save ---------------------------------------------------------------- */

static void ru_run_quiet(void) { ru_run_program(); }

/* Save, then read back through CALLHLL: the script goes on after the save
 * with false (the success path, before and after the change). */
static int ru_save_then_load(void)
{
	ru_bad = 0;
	ru_program(true, "ru-save.dat", "ru-save.dat");
	int st = ru_elsewhere(ru_run_quiet);
	ru_print_status(ru_callsys ? "save, then read back by system calls" : "save, then read back through CALLHLL", st);
	const char *a = strstr(ru_out, "RU MARK after-save top=1 var0=1");
	const char *b = a ? strstr(a + 1, "RU MARK after-save top=0") : NULL;
	const char *c = b ? strstr(b, "RU MARK loaded") : NULL;
	RU_EXPECT(a && b && c, "%s: want after-save (true, result 1), after-save (false) and the loaded branch", ru_fmt());
	RU_EXPECT(!strstr(ru_out, "RU MARK after-load"), "%s: the script went on after ResumeLoad", ru_fmt());
	RU_EXPECT(WIFEXITED(st) && WEXITSTATUS(st) == 0 && strstr(ru_out, "RU returned"), "%s: the program did not end",
		  ru_fmt());
	RU_EXPECT(!ru_sanitizer(), "%s: a sanitizer report", ru_fmt());
	return ru_bad;
}

/* ResumeSave in a folder that cannot be written: true, result 0. */
static int ru_save_unwritable(void)
{
	ru_bad = 0;
	ru_program(true, "ru-save.dat", NULL);
	chmod(sp_dir, 0500);
	ru_run_program();
	chmod(sp_dir, 0700);
	RU_EXPECT(ru_nmarks >= 2 && ru_marks[0] == 1 && ru_marks[1] == 3,
		  "%s: after a failed save the script took the %s branch", ru_fmt(),
		  ru_nmarks >= 2 && ru_marks[1] == 4 ? "read-back" : "unknown");
	RU_EXPECT(ru_nmarks >= 1 && ru_mark_top[0] == 1 && ru_mark_var0[0] == 0,
		  "%s: ResumeSave returned %d with result %d, want true and 0", ru_fmt(),
		  ru_nmarks ? ru_mark_top[0] : -1, ru_nmarks ? ru_mark_var0[0] : -1);
	RU_EXPECT(!ru_file_exists("ru-save.dat") && !ru_file_exists("ru-save.dat.xs4tmp"), "%s: a file was left", ru_fmt());
	return ru_bad;
}

/* A write that fails part way (RLIMIT_FSIZE): reported as failed, the
 * previous image whole, no temporary file. */
static int ru_save_cut(void)
{
	ru_bad = 0;
	SP_CHECK(ru_image("ru-cut.dat"), "the first %s image was not written", ru_fmt());
	size_t n0;
	uint8_t *before = ru_bytes("ru-cut.dat", &n0);
	SP_CHECK(before && n0 > 512, "the first %s image is %zu bytes", ru_fmt(), n0);
	signal(SIGXFSZ, SIG_IGN);
	struct rlimit lim = { .rlim_cur = 256, .rlim_max = RLIM_INFINITY };
	SP_CHECK(!setrlimit(RLIMIT_FSIZE, &lim), "no file size limit");
	heap_ref(ru_string("another string"));	/* the second image differs */
	bool ok = ri_save("ru-cut.dat");
	lim.rlim_cur = RLIM_INFINITY;
	setrlimit(RLIMIT_FSIZE, &lim);
	size_t n1;
	uint8_t *after = ru_bytes("ru-cut.dat", &n1);
	printf("RU %s cut write: save %s, file %zu bytes (before %zu), temporary file %s\n", ru_fmt(),
	       ok ? "reported written" : "reported failed", n1, n0, ru_file_exists("ru-cut.dat.xs4tmp") ? "left" : "none");
	RU_EXPECT(!ok, "%s: a write that failed was reported written", ru_fmt());
	RU_EXPECT(after && n1 == n0 && !memcmp(before, after, n0), "%s: the previous image was not kept", ru_fmt());
	RU_EXPECT(!ru_file_exists("ru-cut.dat.xs4tmp"), "%s: the temporary file was left", ru_fmt());
	free(before);
	free(after);
	return ru_bad;
}

static void ru_save_no_frame(void)
{
	stack_ptr = 0;
	call_stack_ptr = 0;
	stack_push(0);
	stack_push(0);
	int ok = vm_save_image(RU_KEY, "ru-noframe.dat");
	printf("RU save without a frame: %d, file %s\n", ok, ru_file_exists("ru-noframe.dat") ? "written" : "none");
	fflush(stdout);
	_exit(ok || ru_file_exists("ru-noframe.dat") ? 4 : 0);
}

static void ru_save_bad_frame(void)
{
	int fnos[2];
	ru_pick_n(2, fnos);
	ru_push_frames(fnos, 2, -1);
	call_stack[1].fno = -3;
	stack_push(0);
	stack_push(0);
	int ok = vm_save_image(RU_KEY, "ru-badframe.dat");
	printf("RU save with a frame of no function: %d, file %s\n", ok, ru_file_exists("ru-badframe.dat") ? "written" : "none");
	fflush(stdout);
	call_stack_ptr = 0;
	_exit(ok || ru_file_exists("ru-badframe.dat") ? 4 : 0);
}

static int ru_writer_refuses(void (*fn)(void), const char *what)
{
	ru_bad = 0;
	int st = ru_elsewhere(fn);
	ru_print_status(what, st);
	RU_EXPECT(WIFEXITED(st) && WEXITSTATUS(st) == 0 && !ru_sanitizer(), "%s %s: want no image and no crash", ru_fmt(), what);
	return ru_bad;
}

static void ru_save_null_string(void)
{
	int fnos[1];
	ru_pick_n(1, fnos);
	ru_push_frames(fnos, 1, -1);
	int slot = heap_alloc_slot(VM_STRING);
	heap[slot].s = NULL;
	int ok = ri_save("ru-nullstr.dat");
	cJSON *j = ok ? ru_json("ru-nullstr.dat") : NULL;
	cJSON *heapj = j ? cJSON_GetObjectItem(j, "heap") : NULL, *item;
	bool found = false;
	cJSON_ArrayForEach(item, heapj) {
		cJSON *s = cJSON_GetArrayItem(item, 0), *v = cJSON_GetArrayItem(item, 2);
		if (s && s->valueint == slot)
			found = cJSON_IsString(v) && !strcmp(v->valuestring, "");
	}
	cJSON_Delete(j);
	printf("RU json save with a string slot without a string: %d, empty string %s\n", ok, found ? "written" : "not written");
	fflush(stdout);
	call_stack_ptr = 0;
	_exit(ok && found ? 0 : 4);
}

static int ru_save_unwritable_rsm(void) { config.save_format = SAVE_FORMAT_RSM; return ru_save_unwritable(); }
static int ru_save_unwritable_json(void) { config.save_format = SAVE_FORMAT_JSON; return ru_save_unwritable(); }
static int ru_save_then_load_rsm(void) { config.save_format = SAVE_FORMAT_RSM; return ru_save_then_load(); }
static int ru_save_then_load_json(void) { config.save_format = SAVE_FORMAT_JSON; return ru_save_then_load(); }
static int ru_save_then_load_callsys(void) { config.save_format = SAVE_FORMAT_RSM; ru_callsys = true; return ru_save_then_load(); }

/* A file that cannot be put in place (a folder has its name): reported as
 * failed, no temporary file, the folder as it was. */
static int ru_save_rename(void)
{
	ru_bad = 0;
	config.save_format = SAVE_FORMAT_RSM;
	char path[PATH_MAX];
	ri_path(path, sizeof path, "ru-folder.dat");
	SP_CHECK(!mkdir(path, 0700), "no folder %s", path);
	bool ok = ru_image("ru-folder.dat");
	struct stat sb;
	bool folder = !stat(path, &sb) && S_ISDIR(sb.st_mode);
	bool tmp = ru_file_exists("ru-folder.dat.xs4tmp");
	printf("RU rsm save over a folder: %s, folder %s, temporary file %s\n", ok ? "reported written" : "reported failed",
	       folder ? "kept" : "gone", tmp ? "left" : "none");
	RU_EXPECT(!ok && folder && !tmp, "rsm: want a failed save, the folder kept and no temporary file");
	rmdir(path);
	return ru_bad;
}

static int ru_save_cut_rsm(void) { config.save_format = SAVE_FORMAT_RSM; return ru_save_cut(); }
static int ru_save_cut_json(void) { config.save_format = SAVE_FORMAT_JSON; return ru_save_cut(); }
static int ru_noframe_rsm(void) { config.save_format = SAVE_FORMAT_RSM; return ru_writer_refuses(ru_save_no_frame, "save without a frame"); }
static int ru_badframe_rsm(void) { config.save_format = SAVE_FORMAT_RSM; return ru_writer_refuses(ru_save_bad_frame, "save with a frame of no function"); }
static int ru_nullstr_json(void) { config.save_format = SAVE_FORMAT_JSON; return ru_writer_refuses(ru_save_null_string, "save of a string slot without a string"); }

/* ---- unreadable ------------------------------------------------------------- */

static const char *ru_file;
static void ru_load_file(void) { vm_load_image(RU_KEY, ru_file); }

static int ru_unreadable(const char *what, const char *file, const char *want)
{
	ru_file = file;
	int st = ru_elsewhere(ru_load_file);
	ru_expect_unreadable(what, st, file, want);
	return ru_bad;
}

static int ru_missing_callhll(void)
{
	ru_bad = 0;
	ru_program(false, NULL, "ru-none.dat");
	int st = ru_elsewhere(ru_run_quiet);
	ru_expect_unreadable("no file, through CALLHLL", st, "ru-none.dat", "No such file");
	return ru_bad;
}

static int ru_missing(void) { ru_bad = 0; return ru_unreadable("no file", "ru-none.dat", "No such file"); }

/* The box in another language, and the box answered by itself. */
static const char *ru_lang, *ru_country;
static void ru_load_in_lang(void)
{
	vm_ask_test.language = ru_lang;
	vm_ask_test.country = ru_country;
	vm_load_image(RU_KEY, ru_file);
}
static int ru_missing_lang(const char *lang, const char *country)
{
	ru_bad = 0;
	ru_lang = lang;
	ru_country = country;
	ru_file = "ru-none.dat";
	int st = ru_elsewhere(ru_load_in_lang);
	char what[64];
	snprintf(what, sizeof what, "no file, box in %s-%s", lang, country);
	ru_expect_unreadable(what, st, "ru-none.dat", "No such file");
	RU_EXPECT(!strstr(ru_out, "could not be read"), "%s: the box is in English", what);
	return ru_bad;
}
static int ru_missing_ja(void) { config.save_format = SAVE_FORMAT_RSM; return ru_missing_lang("ja", "JP"); }
static int ru_missing_zh_tw(void) { config.save_format = SAVE_FORMAT_RSM; return ru_missing_lang("zh", "TW"); }
static int ru_missing_zh_cn(void) { config.save_format = SAVE_FORMAT_RSM; return ru_missing_lang("zh", "CN"); }

static void ru_load_auto(void)
{
	setenv("XSYS4_AUTO_MSGBOX", "ok", 1);
	vm_load_image(RU_KEY, ru_file);
}
static int ru_missing_auto(void)
{
	ru_bad = 0;
	config.save_format = SAVE_FORMAT_RSM;
	ru_file = "ru-none.dat";
	int st = ru_elsewhere(ru_load_auto);
	ru_print_status("no file, box answered by itself", st);
	const char *line = strstr(ru_out, "Message box not shown");
	const char *nl = line ? strchr(line, '\n') : NULL;
	const char *name = line ? strstr(line, "ru-none.dat") : NULL;
	RU_EXPECT(WIFEXITED(st) && WEXITSTATUS(st) == 1, "rsm: want exit 1");
	RU_EXPECT(!strstr(ru_out, "RU BOX") && line && name && (!nl || name < nl),
		  "rsm: want no box and the text with the file in the log");
	RU_EXPECT(!strstr(ru_out, "RU returned") && !ru_sanitizer(), "rsm: the load came back, or a sanitizer report");
	return ru_bad;
}

static int ru_truncated(void)
{
	ru_bad = 0;
	SP_CHECK(ru_image("ru-trunc.dat"), "the %s image was not written", ru_fmt());
	size_t n;
	uint8_t *b = ru_bytes("ru-trunc.dat", &n);
	SP_CHECK(b && n > 64 && ru_put_bytes("ru-trunc.dat", b, n / 2), "the %s image was not cut", ru_fmt());
	free(b);
	return ru_unreadable("truncated file", "ru-trunc.dat", "Invalid save file");
}

/* An rsm image edited in memory, as file; edit returns false if it found
 * nothing to edit. 0 when the file is there. */
static int ru_rsm_prepare(const char *what, bool (*edit)(struct rsave *), const char *file)
{
	config.save_format = SAVE_FORMAT_RSM;
	SP_CHECK(ru_image(file), "the rsm image was not written");
	struct rsave *rs = ri_read_rsave(file);
	SP_CHECK(rs, "the rsm image does not read");
	bool edited = edit(rs);
	SP_CHECK(edited, "rsm %s: nothing to edit", what);
	SP_CHECK(ri_write_rsave(rs, file), "the edited rsm image was not written");
	/* rsave_free walks what the edit left; the edits keep it consistent */
	rsave_free(rs);
	return 0;
}

static int ru_rsm_edit(const char *what, bool (*edit)(struct rsave *), const char *want)
{
	ru_bad = 0;
	if (ru_rsm_prepare(what, edit, "ru-edit.dat"))
		return 1;
	return ru_unreadable(what, "ru-edit.dat", want);
}

static bool ru_e_version(struct rsave *rs) { rs->version = 14; return true; }

/* Not read: directly (the VM as it was), through CALLHLL and through the
 * system call (the value under the arguments still on top, nothing pushed,
 * the script goes on). */
static void ru_load_unchanged(void)
{
	int frames = call_stack_ptr, sp = stack_ptr;
	size_t live = live_slots();
	uint32_t ip = instr_ptr;
	vm_load_image(RU_KEY, ru_file);
	bool same = call_stack_ptr == frames && stack_ptr == sp && live_slots() == live && instr_ptr == ip;
	printf("RU after the load: frames %d (before %d), stack %d (%d), live %zu (%zu); %s\n", call_stack_ptr, frames,
	       stack_ptr, sp, live_slots(), live, same ? "the VM as it was" : "the VM changed");
	fflush(stdout);
	call_stack_ptr = 0;
}
static int ru_version(void)
{
	ru_bad = 0;
	if (ru_rsm_prepare("rsm version 14", ru_e_version, "ru-v14.dat"))
		return 1;
	ru_file = "ru-v14.dat";
	int st = ru_elsewhere(ru_load_unchanged);
	ru_expect_not_read("version 14", st);
	RU_EXPECT(strstr(ru_out, "the VM as it was"), "rsm version 14: the VM changed");
	return ru_bad;
}
static int ru_version_script(bool callsys)
{
	ru_bad = 0;
	if (ru_rsm_prepare("rsm version 14", ru_e_version, "ru-v14.dat"))
		return 1;
	ru_sentinel = 4242;
	ru_callsys = callsys;
	ru_program(false, NULL, "ru-v14.dat");
	int st = ru_elsewhere(ru_run_quiet);
	const char *what = callsys ? "version 14, through the system call" : "version 14, through CALLHLL";
	ru_expect_not_read(what, st);
	RU_EXPECT(strstr(ru_out, "RU MARK after-load top=4242 var0=0 depth=1"),
		  "rsm %s: want the script after ResumeLoad with the value under its arguments on top, nothing pushed", what);
	return ru_bad;
}
static int ru_version_callhll(void) { return ru_version_script(false); }
static int ru_version_callsys(void) { return ru_version_script(true); }

/* Through CALLHLL with a frame on the call stack, the heap read part way
 * (a struct page short of slots, after the globals and the frames' pages):
 * what was read and the frames and the stack are dropped before the box, and
 * the end releases nothing twice. */
static bool ru_e_struct_short(struct rsave *rs);
static int ru_half_callhll(void)
{
	ru_bad = 0;
	if (ru_rsm_prepare("struct page short of slots", ru_e_struct_short, "ru-half.dat"))
		return 1;
	ru_program(false, NULL, "ru-half.dat");
	int st = ru_elsewhere(ru_run_quiet);
	ru_expect_unreadable("heap read part way, through CALLHLL", st, "ru-half.dat", "slots for");
	RU_EXPECT(strstr(ru_out, "RU BOX state frames=0 stack=0 live=0"),
		  "rsm: the heap, the frames or the stack were not dropped before the box");
	return ru_bad;
}

/* The same through the system call of programs before v14, whose file name
 * is the text of a string in the heap that the load frees: the box still
 * names the file, with no sanitizer report. */
static int ru_half_callsys(void)
{
	ru_bad = 0;
	if (ru_rsm_prepare("struct page short of slots", ru_e_struct_short, "ru-half.dat"))
		return 1;
	ru_callsys = true;
	ru_program(false, NULL, "ru-half.dat");
	int st = ru_elsewhere(ru_run_quiet);
	ru_expect_unreadable("heap read part way, by the system call", st, "ru-half.dat", "slots for");
	RU_EXPECT(strstr(ru_out, "RU BOX state frames=0 stack=0 live=0"),
		  "rsm: the heap, the frames or the stack were not dropped before the box");
	return ru_bad;
}

/* Not read through the system call: ResumeLoad's two strings each lose the
 * one reference the call was given, as other system calls release theirs. */
static void ru_run_refs(void)
{
	int k = heap[ru_key_slot].ref, p = heap[ru_lpath_slot].ref;
	ru_run_program();
	int k2 = heap[ru_key_slot].ref, p2 = heap[ru_lpath_slot].ref;
	printf("RU refs key %d -> %d, file %d -> %d: %s\n", k, k2, p, p2,
	       k2 == k - 1 && p2 == p - 1 ? "released once each" : "not released once each");
	fflush(stdout);
}
static int ru_version_callsys_refs(void)
{
	ru_bad = 0;
	if (ru_rsm_prepare("rsm version 14", ru_e_version, "ru-v14.dat"))
		return 1;
	ru_callsys = true;
	ru_program(false, NULL, "ru-v14.dat");
	int st = ru_elsewhere(ru_run_refs);
	ru_expect_not_read("version 14 by the system call, its strings", st);
	RU_EXPECT(strstr(ru_out, "RU refs") && strstr(ru_out, ": released once each"),
		  "rsm: ResumeLoad's two strings were not released once each");
	return ru_bad;
}
static bool ru_e_key(struct rsave *rs) { free(rs->key); rs->key = strdup("another-key"); return true; }
static bool ru_e_records(struct rsave *rs)
{
	if (rs->nr_return_records < 2)
		return false;
	free(rs->return_records[rs->nr_return_records - 1].caller_func);
	rs->return_records[rs->nr_return_records - 1].caller_func = NULL;
	rs->nr_return_records--;
	return true;
}
static bool ru_e_offset(struct rsave *rs)
{
	if (rs->nr_return_records < 2)
		return false;
	rs->return_records[1].local_addr = (int32_t)ain->code_size;	/* past the code */
	return true;
}
static bool ru_e_ip(struct rsave *rs) { rs->ip.local_addr += 1; return true; }
static bool ru_e_no_function(struct rsave *rs)
{
	if (rs->nr_return_records < 2)
		return false;
	rs->return_records[1].return_addr = -1;	/* written without its function */
	return true;
}
static bool ru_e_crc(struct rsave *rs)
{
	if (rs->nr_return_records < 2)
		return false;
	rs->return_records[1].crc ^= 1;
	return true;
}
static bool ru_e_unknown_function(struct rsave *rs)
{
	if (rs->nr_return_records < 2)
		return false;
	free(rs->return_records[1].caller_func);
	rs->return_records[1].caller_func = strdup("NoSuchFunctionInTheAin");
	return true;
}
static bool ru_e_empty_string(struct rsave *rs)
{
	if (ru_objs.str >= rs->nr_heap_objs || *(enum rsave_heap_tag *)rs->heap[ru_objs.str] != RSAVE_STRING)
		return false;
	((struct rsave_heap_string *)rs->heap[ru_objs.str])->len = 0;
	return true;
}
static bool ru_e_globals_short(struct rsave *rs)
{
	if (global_page_slot >= rs->nr_heap_objs || *(enum rsave_heap_tag *)rs->heap[global_page_slot] != RSAVE_GLOBALS)
		return false;
	struct rsave_heap_frame *f = rs->heap[global_page_slot];
	if (f->nr_slots < 1)
		return false;
	f->nr_slots--;
	return true;
}
static bool ru_e_struct_short(struct rsave *rs)
{
	if (ru_objs.obj >= rs->nr_heap_objs || *(enum rsave_heap_tag *)rs->heap[ru_objs.obj] != RSAVE_STRUCT)
		return false;
	struct rsave_heap_struct *s = rs->heap[ru_objs.obj];
	if (s->nr_slots < 2)
		return false;
	s->nr_slots--;
	return true;
}
static bool ru_e_negative_slots(struct rsave *rs)
{
	if (ru_objs.arr >= rs->nr_heap_objs || *(enum rsave_heap_tag *)rs->heap[ru_objs.arr] != RSAVE_ARRAY)
		return false;
	((struct rsave_heap_array *)rs->heap[ru_objs.arr])->nr_slots = -1;
	return true;
}
static bool ru_e_array_struct(struct rsave *rs)
{
	if (ru_objs.arr >= rs->nr_heap_objs || *(enum rsave_heap_tag *)rs->heap[ru_objs.arr] != RSAVE_ARRAY)
		return false;
	struct rsave_heap_array *a = rs->heap[ru_objs.arr];
	free(a->struct_type.name);
	a->struct_type.name = strdup("NoSuchStructInTheAin");
	return true;
}
static bool ru_e_offset_before(struct rsave *rs)
{
	if (rs->nr_return_records < 2)
		return false;
	rs->return_records[1].local_addr = -2;	/* before its function */
	return true;
}
static bool ru_e_bottom_record(struct rsave *rs)
{
	if (rs->nr_return_records < 2)
		return false;
	struct rsave_return_record *b = &rs->return_records[0], *r = &rs->return_records[1];
	b->return_addr = 5;	/* written as a record of a function */
	b->caller_func = strdup(r->caller_func);
	b->local_addr = r->local_addr;
	b->crc = r->crc;
	return true;
}
static bool ru_e_drop_records(struct rsave *rs)
{
	if (rs->nr_call_frames < 2 || rs->nr_return_records < 2)
		return false;
	for (int i = 1; i < rs->nr_return_records; i++) {
		free(rs->return_records[i].caller_func);
		rs->return_records[i].caller_func = NULL;
	}
	rs->nr_return_records = 1;	/* the bottom record */
	return true;
}
/* the bottom frame and the bottom record only */
static bool ru_e_bottom_only(struct rsave *rs)
{
	if (!ru_e_drop_records(rs))
		return false;
	rs->nr_call_frames = 1;
	return true;
}
/* one frame that is not the bottom one, and one record */
static bool ru_e_one_record(struct rsave *rs)
{
	if (!ru_e_drop_records(rs))
		return false;
	rs->call_frames[0] = rs->call_frames[1];
	rs->nr_call_frames = 1;
	return true;
}
static bool ru_e_frame_local(struct rsave *rs)
{
	if (rs->nr_call_frames < 2)
		return false;
	rs->call_frames[rs->nr_call_frames - 1].local_ptr = ru_objs.str;	/* the top frame's local page */
	return true;
}

/* The top frame's local page becomes a page of another type: an int array
 * of as many slots as the top function has variables (read back), or of one
 * slot fewer (not read). The top function has two or more variables. */
static struct { int fno, n, arr_n, arr_short; } ru_pages;
static bool ru_image_pages(const char *file)
{
	int fnos[2];
	fnos[0] = rf_pick(AIN_VOID, 0, NULL, 0);
	fnos[1] = rf_pick(AIN_VOID, 2, fnos, 1);
	ru_push_frames(fnos, 2, -1);
	ru_pages.fno = fnos[1];
	ru_pages.n = ain->functions[fnos[1]].nr_vars;
	union vm_value dims = { .i = ru_pages.n };
	ru_pages.arr_n = heap_alloc_page(alloc_array(1, &dims, AIN_ARRAY_INT, -1, false));
	heap_ref(ru_pages.arr_n);
	dims.i = ru_pages.n - 1;
	ru_pages.arr_short = heap_alloc_page(alloc_array(1, &dims, AIN_ARRAY_INT, -1, false));
	heap_ref(ru_pages.arr_short);
	return ri_save(file);
}
static bool ru_pages_short;
static bool ru_pages_edit(const char *file)
{
	struct rsave *rs = ri_read_rsave(file);
	if (!rs || rs->nr_call_frames < 2) {
		if (rs)
			rsave_free(rs);
		return false;
	}
	rs->call_frames[rs->nr_call_frames - 1].local_ptr = ru_pages_short ? ru_pages.arr_short : ru_pages.arr_n;
	bool ok = ri_write_rsave(rs, file);
	rsave_free(rs);
	return ok;
}
static void ru_load_pages(void)
{
	vm_load_image(RU_KEY, "ru-pages.dat");
	int top = call_stack_ptr - 1;
	int want = ru_pages_short ? ru_pages.arr_short : ru_pages.arr_n;
	bool ok = call_stack_ptr == 2 && call_stack[top].fno == ru_pages.fno && call_stack[top].page_slot == want
		&& heap[want].page && heap[want].page->type == ARRAY_PAGE && heap[want].page->nr_vars == ru_pages.n;
	printf("RU loaded %d frames, top function %d (want %d), local page %d (want %d), %s\n", call_stack_ptr,
	       call_stack_ptr ? call_stack[top].fno : -1, ru_pages.fno, call_stack_ptr ? call_stack[top].page_slot : -1,
	       want, ok ? "as written" : "not as written");
	fflush(stdout);
	call_stack_ptr = 0;
	_exit(ok ? 0 : 3);
}
static int ru_frame_other_page(bool short_of_slots)
{
	ru_bad = 0;
	config.save_format = SAVE_FORMAT_RSM;
	ru_pages_short = short_of_slots;
	SP_CHECK(ru_image_pages("ru-pages.dat") && ru_pages_edit("ru-pages.dat"), "the rsm image was not made");
	printf("RU rsm top function with %d variables, array pages of %d and %d slots\n", ru_pages.n, ru_pages.n,
	       ru_pages.n - 1);
	if (short_of_slots)
		return ru_unreadable("frame whose local page is short of slots", "ru-pages.dat", "slots for");
	int st = ru_elsewhere(ru_load_pages);
	ru_print_status("frame whose local page is an array of as many slots", st);
	RU_EXPECT(WIFEXITED(st) && WEXITSTATUS(st) == 0 && strstr(ru_out, "as written"),
		  "rsm: the image did not read back with that page");
	RU_EXPECT(!strstr(ru_out, "RU BOX") && !ru_sanitizer(), "rsm: a box or a sanitizer report");
	return ru_bad;
}
static int ru_frame_array_enough(void) { return ru_frame_other_page(false); }
static int ru_frame_array_short(void) { return ru_frame_other_page(true); }

/* the struct pointer kept in the top frame's local page, past the heap */
static bool ru_e_struct_ptr(struct rsave *rs)
{
	if (rs->nr_call_frames < 2)
		return false;
	int slot = rs->call_frames[rs->nr_call_frames - 1].local_ptr;
	if (slot < 0 || slot >= rs->nr_heap_objs || *(enum rsave_heap_tag *)rs->heap[slot] != RSAVE_LOCALS)
		return false;
	((struct rsave_heap_frame *)rs->heap[slot])->struct_ptr = 1 << 30;
	return true;
}
static bool ru_e_heap_slots(struct rsave *rs)
{
	int n = (1 << 24) + 1;	/* one more than the reader takes (resume.c) */
	if (rs->nr_heap_objs >= n)
		return false;
	rs->heap = xrealloc(rs->heap, n * sizeof(void *));
	for (int i = rs->nr_heap_objs; i < n; i++)
		rs->heap[i] = rsave_null;
	rs->nr_heap_objs = n;
	return true;
}
/* a string libsys4's parser does not expect (its unknown word is not 0 or
 * 1), on which it would end the program */
static bool ru_e_string_word(struct rsave *rs)
{
	if (ru_objs.str >= rs->nr_heap_objs || *(enum rsave_heap_tag *)rs->heap[ru_objs.str] != RSAVE_STRING)
		return false;
	((struct rsave_heap_string *)rs->heap[ru_objs.str])->uk = 2;
	return true;
}
/* a globals page the parser gives up on (not written as one): it returns
 * with a NULL heap entry, which rsave_free would read */
static bool ru_e_globals_given_up(struct rsave *rs)
{
	if (global_page_slot >= rs->nr_heap_objs || *(enum rsave_heap_tag *)rs->heap[global_page_slot] != RSAVE_GLOBALS)
		return false;
	((struct rsave_heap_frame *)rs->heap[global_page_slot])->func.id = 5;
	return true;
}

static int ru_offset_before_rsm(void) { return ru_rsm_edit("return offset before its function", ru_e_offset_before, "not in its code"); }
static int ru_bottom_record(void) { return ru_rsm_edit("bottom return record that is not one", ru_e_bottom_record, "no bottom return record"); }
static int ru_bottom_only(void) { return ru_rsm_edit("only the bottom frame", ru_e_bottom_only, "no call frame"); }
static int ru_one_record(void) { return ru_rsm_edit("one frame, as many records as frames", ru_e_one_record, "has no return record"); }
static int ru_frame_string(void) { return ru_rsm_edit("frame whose local page is a string", ru_e_frame_local, "is not a page"); }
static int ru_struct_ptr(void) { return ru_rsm_edit("local page whose struct pointer is past the heap", ru_e_struct_ptr, "struct pointer"); }
static int ru_heap_slots(void) { return ru_rsm_edit("more heap slots than the reader takes", ru_e_heap_slots, "16777217 heap slots"); }
static int ru_string_word(void) { return ru_rsm_edit("string libsys4 does not expect", ru_e_string_word, "unexpected"); }
static int ru_globals_given_up(void) { return ru_rsm_edit("globals page libsys4 gives up on", ru_e_globals_given_up, "Invalid save file"); }
static int ru_key_rsm(void) { return ru_rsm_edit("another key", ru_e_key, "Key doesn't match"); }
static int ru_records(void) { return ru_rsm_edit("return records do not fit the frames", ru_e_records, "return records"); }
static int ru_offset_rsm(void) { return ru_rsm_edit("return offset past the code", ru_e_offset, "not in its code"); }
static int ru_ip_rsm(void) { return ru_rsm_edit("instruction pointer not an instruction", ru_e_ip, "not an instruction"); }
static int ru_no_function(void) { return ru_rsm_edit("return record without a function", ru_e_no_function, "has no function"); }
static int ru_crc(void) { return ru_rsm_edit("checksum that does not match", ru_e_crc, "CRC mismatch"); }
static int ru_unknown_function(void) { return ru_rsm_edit("return record of no function", ru_e_unknown_function, "NoSuchFunctionInTheAin"); }
static int ru_empty_string(void) { return ru_rsm_edit("string of length 0", ru_e_empty_string, "string of length 0"); }
static int ru_globals_short(void) { return ru_rsm_edit("globals page short of slots", ru_e_globals_short, "slots for"); }
static int ru_struct_short(void) { return ru_rsm_edit("struct page short of slots", ru_e_struct_short, "slots for"); }
static int ru_array_struct(void) { return ru_rsm_edit("array of a struct not in the AIN", ru_e_array_struct, "NoSuchStructInTheAin"); }

/* A file with comments only (what the comment writer makes when there is no
 * image). */
static int ru_comments_only(void)
{
	ru_bad = 0;
	config.save_format = SAVE_FORMAT_RSM;
	struct rsave *rs = xcalloc(1, sizeof *rs);
	rs->version = 9;
	rs->key = strdup(RU_KEY);
	rs->comments_only = true;
	rs->nr_comments = 1;
	rs->comments = xcalloc(1, sizeof(char *));
	rs->comments[0] = strdup("a comment");
	SP_CHECK(ri_write_rsave(rs, "ru-comments.dat"), "the file was not written");
	rsave_free(rs);
	return ru_unreadable("file with comments only", "ru-comments.dat", "holds no image");
}

static int ru_frames_over(void)
{
	ru_bad = 0;
	SP_CHECK(config.save_format == SAVE_FORMAT_RSM ? ri11_rsm_frames("ru-frames.dat", RI_CALL_STACK_MAX + 1)
		 : ri11_json_frames("ru-frames.dat", RI_CALL_STACK_MAX + 1), "the %s image was not made", ru_fmt());
	return ru_unreadable("more frames than the call stack holds", "ru-frames.dat", "too many call frames");
}
static int ru_frames_over_rsm(void) { config.save_format = SAVE_FORMAT_RSM; return ru_frames_over(); }
static int ru_frames_over_json(void) { config.save_format = SAVE_FORMAT_JSON; return ru_frames_over(); }

/* Limits: the load must not come back; a sanitizer report is accepted only
 * in libsys4's parser (savefile.c), which reads the value first: there must
 * be one, and every sanitizer SUMMARY line must name savefile.c. A box is
 * accepted with no sanitizer report. */
static bool ru_report_in_parser(void)
{
	bool found = false;
	for (const char *s = ru_out; (s = strstr(s, "SUMMARY: ")); s++) {
		const char *nl = strchr(s, '\n');
		int n = nl ? (int)(nl - s) : (int)strlen(s);
		char line[512];
		snprintf(line, sizeof line, "%.*s", n, s);
		if (!strstr(line, "Sanitizer"))
			continue;
		if (!strstr(line, "savefile.c"))
			return false;
		found = true;
	}
	return found;
}
static int ru_limit(const char *what, bool (*edit)(struct rsave *))
{
	ru_bad = 0;
	config.save_format = SAVE_FORMAT_RSM;
	SP_CHECK(ru_image("ru-edit.dat"), "the rsm image was not written");
	struct rsave *rs = ri_read_rsave("ru-edit.dat");
	SP_CHECK(rs && edit(rs), "rsm %s: nothing to edit", what);
	SP_CHECK(ri_write_rsave(rs, "ru-edit.dat"), "the edited rsm image was not written");
	ru_file = "ru-edit.dat";
	int st = ru_elsewhere(ru_load_file);
	ru_print_status(what, st);
	bool boxed = WIFEXITED(st) && WEXITSTATUS(st) == 1 && strstr(ru_out, "RU BOX buttons=1") && !ru_sanitizer();
	bool in_parser = ru_report_in_parser();
	RU_EXPECT(!strstr(ru_out, "RU returned"), "rsm %s: the load came back", what);
	RU_EXPECT(boxed || in_parser, "rsm %s: neither the box nor a report in libsys4's parser", what);
	printf("RU rsm %s: %s\n", what, boxed ? "box" : in_parser ? "report in libsys4's parser" : "other");
	return ru_bad;
}
static int ru_negative_slots(void) { return ru_limit("negative slot count", ru_e_negative_slots); }

/* libsys4's writer cannot write a negative string length, so the length of
 * the image's string is changed in the decompressed file. */
static bool ru_e_string_raw(struct rsave *rs)
{
	(void)rs;
	char path[PATH_MAX];
	ri_path(path, sizeof path, "ru-edit.dat");
	enum savefile_error error;
	struct savefile *sf = savefile_read(path, &error);
	if (!sf)
		return false;
	static const char text[] = "a string";
	uint8_t want[4 + sizeof text];
	int32_t len = sizeof text;
	memcpy(want, &len, 4);
	memcpy(want + 4, text, sizeof text);
	bool found = false;
	for (size_t i = 0; i + sizeof want <= sf->len && !found; i++) {
		if (!memcmp(sf->buf + i, want, sizeof want)) {
			int32_t bad = -1;
			memcpy(sf->buf + i, &bad, 4);
			found = true;
		}
	}
	FILE *fp = found ? fopen(path, "wb") : NULL;
	sf->encrypted = true;
	sf->compression_level = 1;
	bool ok = fp && savefile_write(sf, fp) == SAVEFILE_SUCCESS;
	if (fp && fclose(fp))
		ok = false;
	savefile_free(sf);
	return found && ok;
}

static int ru_negative_string(void)
{
	ru_bad = 0;
	config.save_format = SAVE_FORMAT_RSM;
	SP_CHECK(ru_image("ru-edit.dat") && ru_e_string_raw(NULL), "rsm negative string length: nothing to edit");
	ru_file = "ru-edit.dat";
	int st = ru_elsewhere(ru_load_file);
	ru_print_status("negative string length", st);
	bool boxed = WIFEXITED(st) && WEXITSTATUS(st) == 1 && strstr(ru_out, "RU BOX buttons=1") && !ru_sanitizer();
	bool in_parser = ru_report_in_parser();
	RU_EXPECT(!strstr(ru_out, "RU returned"), "rsm negative string length: the load came back");
	RU_EXPECT(boxed || in_parser, "rsm negative string length: neither the box nor a report in libsys4's parser");
	printf("RU rsm negative string length: %s\n", boxed ? "box" : in_parser ? "report in libsys4's parser" : "other");
	return ru_bad;
}

/* json images edited as text */
static int ru_json_edit(const char *what, bool (*edit)(cJSON *), const char *want)
{
	ru_bad = 0;
	config.save_format = SAVE_FORMAT_JSON;
	SP_CHECK(ru_image("ru-edit.json"), "the json image was not written");
	cJSON *j = ru_json("ru-edit.json");
	SP_CHECK(j && edit(j), "json %s: nothing to edit", what);
	SP_CHECK(ru_put_json("ru-edit.json", j), "the edited json image was not written");
	return ru_unreadable(what, "ru-edit.json", want);
}

static bool ru_j_key(cJSON *j) { cJSON_ReplaceItemInObject(j, "key", cJSON_CreateString("another-key")); return true; }
static bool ru_j_negative_slot(cJSON *j)
{
	cJSON *h = cJSON_GetObjectItem(j, "heap");
	cJSON *item = h ? cJSON_GetArrayItem(h, cJSON_GetArraySize(h) - 1) : NULL;
	if (!item)
		return false;
	cJSON *copy = cJSON_Duplicate(item, 1);
	cJSON_ReplaceItemInArray(copy, 0, cJSON_CreateNumber(-1));
	cJSON_AddItemToArray(h, copy);
	return true;
}
static bool ru_j_twice(cJSON *j)
{
	cJSON *h = cJSON_GetObjectItem(j, "heap");
	cJSON *item = h ? cJSON_GetArrayItem(h, cJSON_GetArraySize(h) - 1) : NULL;
	if (!item)
		return false;
	cJSON_AddItemToArray(h, cJSON_Duplicate(item, 1));
	return true;
}
static bool ru_j_heap_number(cJSON *j) { cJSON_ReplaceItemInObject(j, "heap", cJSON_CreateNumber(5)); return true; }
static bool ru_j_stack_short(cJSON *j) { cJSON_ReplaceItemInObject(j, "stack", cJSON_CreateArray()); return true; }
static bool ru_j_frame_function(cJSON *j)
{
	cJSON *f = cJSON_GetArrayItem(cJSON_GetObjectItem(j, "call-stack"), 1);
	if (!f)
		return false;
	cJSON_ReplaceItemInObject(f, "function", cJSON_CreateNumber(ain->nr_functions + 5));
	return true;
}
static bool ru_j_no_frame(cJSON *j) { cJSON_ReplaceItemInObject(j, "call-stack", cJSON_CreateArray()); return true; }
static bool ru_j_offset(cJSON *j)
{
	cJSON *f = cJSON_GetArrayItem(cJSON_GetObjectItem(j, "call-stack"), 1);
	cJSON *r = f ? cJSON_GetObjectItem(f, "return-address") : NULL;
	if (!r)
		return false;
	cJSON_ReplaceItemInObject(f, "return-address", cJSON_CreateNumber((double)ain->code_size + 4));
	return true;
}
static bool ru_j_ip(cJSON *j)
{
	cJSON *ip = cJSON_GetObjectItem(j, "ip");
	if (!ip)
		return false;
	cJSON_ReplaceItemInObject(j, "ip", cJSON_CreateNumber(ip->valuedouble + 1));
	return true;
}

static bool ru_j_huge_slot(cJSON *j)
{
	cJSON *h = cJSON_GetObjectItem(j, "heap");
	cJSON *item = h ? cJSON_GetArrayItem(h, cJSON_GetArraySize(h) - 1) : NULL;
	if (!item)
		return false;
	cJSON *copy = cJSON_Duplicate(item, 1);
	cJSON_ReplaceItemInArray(copy, 0, cJSON_CreateNumber(1 << 24));	/* the reader's bound (resume.c) */
	cJSON_AddItemToArray(h, copy);
	return true;
}
static bool ru_j_page_type(cJSON *j)
{
	cJSON *item;
	cJSON_ArrayForEach(item, cJSON_GetObjectItem(j, "heap")) {
		cJSON *v = cJSON_GetArrayItem(item, 2);
		if (cJSON_IsObject(v) && cJSON_GetObjectItem(v, "type")) {
			cJSON_ReplaceItemInObject(v, "type", cJSON_CreateString("no-such-type"));
			return true;
		}
	}
	return false;
}
static bool ru_j_offset_before(cJSON *j)
{
	cJSON *fs = cJSON_GetObjectItem(j, "call-stack");
	cJSON *f0 = cJSON_GetArrayItem(fs, 0), *f1 = cJSON_GetArrayItem(fs, 1);
	cJSON *fno = f0 ? cJSON_GetObjectItem(f0, "function") : NULL;
	if (!f1 || !fno || fno->valueint < 0 || fno->valueint >= ain->nr_functions || !ain->functions[fno->valueint].address)
		return false;
	/* before the start of the function below it */
	cJSON_ReplaceItemInObject(f1, "return-address", cJSON_CreateNumber(ain->functions[fno->valueint].address - 2));
	return true;
}
static int ru_frame_field_value;
static const char *ru_frame_field;
static bool ru_j_frame_field(cJSON *j)
{
	cJSON *fs = cJSON_GetObjectItem(j, "call-stack");
	cJSON *f = cJSON_GetArrayItem(fs, cJSON_GetArraySize(fs) - 1);
	if (!f || !cJSON_GetObjectItem(f, ru_frame_field))
		return false;
	cJSON_ReplaceItemInObject(f, ru_frame_field, cJSON_CreateNumber(ru_frame_field_value));
	return true;
}
static int ru_frame_field_case(const char *what, const char *field, int value, const char *want)
{
	ru_frame_field = field;
	ru_frame_field_value = value;
	return ru_json_edit(what, ru_j_frame_field, want);
}

static int ru_huge_slot_json(void) { return ru_json_edit("heap slot past the reader's bound", ru_j_huge_slot, "heap slot 16777216"); }
static int ru_page_type_json(void) { return ru_json_edit("page type that is not one", ru_j_page_type, "Invalid page type"); }
static int ru_offset_before_json(void) { return ru_json_edit("return address before its function", ru_j_offset_before, "not in its code"); }
static int ru_local_negative_json(void) { return ru_frame_field_case("negative local page", "local-page", -5, "is not a page"); }
static int ru_local_past_json(void) { return ru_frame_field_case("local page past the heap", "local-page", 1 << 30, "is not a page"); }
static int ru_struct_past_json(void) { return ru_frame_field_case("struct page past the heap", "struct-page", 1 << 30, "past the heap"); }
static int ru_key_json(void) { return ru_json_edit("another key", ru_j_key, "Key doesn't match"); }
static int ru_negative_slot_json(void) { return ru_json_edit("negative heap slot", ru_j_negative_slot, "heap slot -1"); }
static int ru_twice_json(void) { return ru_json_edit("heap slot given twice", ru_j_twice, "given twice"); }
static int ru_heap_number_json(void) { return ru_json_edit("heap that is not an array", ru_j_heap_number, "Expected array"); }
static int ru_stack_short_json(void) { return ru_json_edit("fewer than two stack values", ru_j_stack_short, "stack values"); }
static int ru_frame_function_json(void) { return ru_json_edit("frame of no function", ru_j_frame_function, "no function"); }
static int ru_no_frame_json(void) { return ru_json_edit("no call frame", ru_j_no_frame, "no call frame"); }
static int ru_offset_json(void) { return ru_json_edit("return address past the code", ru_j_offset, "not in its code"); }
static int ru_ip_json(void) { return ru_json_edit("instruction pointer not an instruction", ru_j_ip, "not an instruction"); }

static int ru_unparsable_json(void)
{
	ru_bad = 0;
	config.save_format = SAVE_FORMAT_JSON;
	SP_CHECK(ru_image("ru-text.json"), "the json image was not written");
	size_t n;
	uint8_t *b = ru_bytes("ru-text.json", &n);
	SP_CHECK(b && n > 64 && ru_put_bytes("ru-text.json", b, n / 2), "the json image was not cut");
	free(b);
	return ru_unreadable("text that does not parse", "ru-text.json", "Expected string but got NULL");
}

static int ru_missing_rsm(void) { config.save_format = SAVE_FORMAT_RSM; return ru_missing(); }
static int ru_missing_callhll_rsm(void) { config.save_format = SAVE_FORMAT_RSM; return ru_missing_callhll(); }
static int ru_truncated_rsm(void) { config.save_format = SAVE_FORMAT_RSM; return ru_truncated(); }

static int test_resume_unreadable(void)
{
	sp_setup_dir();
	sp_run("RU rsm C entry in the middle", ru_centry_mid_rsm);
	sp_run("RU rsm C entry on top", ru_centry_top_rsm);
	sp_run("RU json C entry in the middle", ru_centry_mid_json);
	sp_run("RU json C entry on top", ru_centry_top_json);
	sp_run("RU rsm save, then read back", ru_save_then_load_rsm);
	sp_run("RU json save, then read back", ru_save_then_load_json);
	sp_run("RU rsm save, then read back by system calls", ru_save_then_load_callsys);
	sp_run("RU rsm save in a folder not writable", ru_save_unwritable_rsm);
	sp_run("RU json save in a folder not writable", ru_save_unwritable_json);
	sp_run("RU rsm write cut short", ru_save_cut_rsm);
	sp_run("RU json write cut short", ru_save_cut_json);
	sp_run("RU rsm save over a folder", ru_save_rename);
	sp_run("RU rsm save without a frame", ru_noframe_rsm);
	sp_run("RU rsm save with a frame of no function", ru_badframe_rsm);
	sp_run("RU json save of a string slot without a string", ru_nullstr_json);
	sp_run("RU rsm no file, through CALLHLL", ru_missing_callhll_rsm);
	sp_run("RU rsm no file", ru_missing_rsm);
	sp_run("RU rsm no file, box in Japanese", ru_missing_ja);
	sp_run("RU rsm no file, box in Chinese (TW)", ru_missing_zh_tw);
	sp_run("RU rsm no file, box in Chinese (CN)", ru_missing_zh_cn);
	sp_run("RU rsm no file, box answered by itself", ru_missing_auto);
	sp_run("RU rsm truncated file", ru_truncated_rsm);
	sp_run("RU rsm version 14, not read", ru_version);
	sp_run("RU rsm version 14 through CALLHLL", ru_version_callhll);
	sp_run("RU rsm version 14 through the system call", ru_version_callsys);
	sp_run("RU rsm another key", ru_key_rsm);
	sp_run("RU json another key", ru_key_json);
	sp_run("RU rsm too many frames", ru_frames_over_rsm);
	sp_run("RU json too many frames", ru_frames_over_json);
	sp_run("RU rsm return records do not fit", ru_records);
	sp_run("RU rsm bottom return record not one", ru_bottom_record);
	sp_run("RU rsm only the bottom frame", ru_bottom_only);
	sp_run("RU rsm one frame, one record each", ru_one_record);
	sp_run("RU rsm return offset past the code", ru_offset_rsm);
	sp_run("RU rsm return offset before its function", ru_offset_before_rsm);
	sp_run("RU json return address past the code", ru_offset_json);
	sp_run("RU json return address before its function", ru_offset_before_json);
	sp_run("RU rsm instruction pointer not an instruction", ru_ip_rsm);
	sp_run("RU json instruction pointer not an instruction", ru_ip_json);
	sp_run("RU rsm string of length 0", ru_empty_string);
	sp_run("RU rsm comments only", ru_comments_only);
	sp_run("RU rsm globals page short of slots", ru_globals_short);
	sp_run("RU rsm struct page short of slots", ru_struct_short);
	sp_run("RU rsm heap read part way, through CALLHLL", ru_half_callhll);
	sp_run("RU rsm array of a struct not in the AIN", ru_array_struct);
	sp_run("RU rsm return record without a function", ru_no_function);
	sp_run("RU rsm checksum does not match", ru_crc);
	sp_run("RU rsm return record of no function", ru_unknown_function);
	sp_run("RU rsm frame with a string as local page", ru_frame_string);
	sp_run("RU rsm frame with an array of as many slots as local page", ru_frame_array_enough);
	sp_run("RU rsm frame with a local page short of slots", ru_frame_array_short);
	sp_run("RU rsm local page with a struct pointer past the heap", ru_struct_ptr);
	sp_run("RU rsm more heap slots than read", ru_heap_slots);
	sp_run("RU rsm string libsys4 does not expect", ru_string_word);
	sp_run("RU rsm globals page libsys4 gives up on", ru_globals_given_up);
	sp_run("RU json negative heap slot", ru_negative_slot_json);
	sp_run("RU json heap slot twice", ru_twice_json);
	sp_run("RU json heap slot past the bound", ru_huge_slot_json);
	sp_run("RU json page type not one", ru_page_type_json);
	sp_run("RU json heap not an array", ru_heap_number_json);
	sp_run("RU json fewer than two stack values", ru_stack_short_json);
	sp_run("RU json frame of no function", ru_frame_function_json);
	sp_run("RU json no call frame", ru_no_frame_json);
	sp_run("RU json negative local page", ru_local_negative_json);
	sp_run("RU json local page past the heap", ru_local_past_json);
	sp_run("RU json struct page past the heap", ru_struct_past_json);
	sp_run("RU json text does not parse", ru_unparsable_json);
	sp_run("RU rsm limit: negative slot count", ru_negative_slots);
	sp_run("RU rsm limit: negative string length", ru_negative_string);
	sp_run("RU rsm heap read part way, by the system call", ru_half_callsys);
	sp_run("RU rsm version 14 by the system call, its strings released", ru_version_callsys_refs);
	sp_remove_dir();
	printf("RESUME-UNREADABLE summary: %d/%d case(s) failed\n", sp_failed_cases, sp_cases);
	return sp_failed_cases ? 88 : 0;
}
