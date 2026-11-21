/* resume-frames: the call frames of an engine image (system.ResumeSave /
 * ResumeLoad) go on as they would have gone on had the image not been
 * written and read back.
 *
 * An image keeps four fields of a frame (function, return address, local
 * page, struct page). The VM's own fields (the stack base of a frame,
 * whether it is a method or a delegate call and the slots a delegate call
 * returns, a closure's environment, the two references a delegate call
 * holds) and the cursor and limit of a delegate being called are made again
 * after a load (vm_resume_frames_loaded): the flags from the call that ends
 * at the return address, the environment, references, cursor and limit from
 * the image (rsm: a comment; json: members of the call-stack entries), and
 * from rules when the image has none (an image of an earlier build).
 *
 * A small program is put after the game's code, in the bodies of some of
 * the game's functions (only their addresses change; their variables and
 * types stay, so the images name and check them as any other):
 *
 *   O (void, bottom frame): sets its variable 0 to 500, pushes 1001 1002
 *     1003, calls method M on object A (CALLMETHOD), and at O_END has
 *     1001 1002 1003 and M's result on the stack; it takes them off and
 *     returns.
 *   M (int, method): pushes 2001 2002 and calls the delegate D (DG_CALLBEGIN,
 *     DG_CALL, JUMP); then adds and returns the sum (4003; with a delegate
 *     that returns a value, 2001 + 2002 + that value).
 *   D's entries: L on A with O's local page as its environment, then H on
 *     A, then H on B.
 *   L (delegate entry): pushes its environment's variable 1 as the place to
 *     assign to, then 3001, calls G (CALLFUNC), adds, adds its environment's
 *     variable 0 (X_GETENV) and assigns: 3001 + 4242 + 500 into O's
 *     variable 1.
 *   G (int): the image is written at its first instruction (SNAP); returns
 *     4242.
 *   H (delegate entry): adds 10 to its object's member 0.
 *
 * At SNAP the frames are O, M (method), L (delegate call) and G, with values
 * of each of them on the stack. After SNAP every instruction is recorded
 * (address, frames, stack depth, top value) up to O's RETURN, with the
 * results (objects, O's variable 1, reference counts, live slots). A case
 * runs the program and writes the image (the reference), loads the image in
 * another process and runs it from there; both records must be equal.
 *
 * Cases (each once per format):
 *   RF1  The image as this build writes it: equal records (the values under
 *        each returning frame stay; the method's object is taken off; L reads
 *        and writes O's page, not A; H runs for A and for B; the references
 *        the delegate call held are given back once).
 *   RF2  The same with a delegate that returns a value (one slot): equal
 *        records, M returns 2001 + 2002 + the last handler's value.
 * In each the frames' flags after the load (method, delegate call, slots a
 * delegate call returns) are those of the frames where the image was
 * written.
 *   RF3  An image without the frame fields (the comment, or the members,
 *        taken out: what an earlier build wrote): equal records by the rules.
 *   RF4  A second image written while frames read back from the first are
 *        still running (in H, the frames O and M from the first image), then
 *        read back: equal records from there; the run from the first image
 *        gives the reference's results.
 *   RF5  The image as written: the rsm file has one comment, the frame
 *        fields of its four frames; the json call-stack entries have the
 *        members. The game's comments of the image (vm_load_image_comments)
 *        do not include it, and writing comments keeps it (rsm).
 *   RF6  (where the rule stops) An image without the frame fields whose
 *        delegates hold two entries for L's object and function with
 *        different environments (the rule finds none and leaves the
 *        environment out, as before): the load goes through, the program runs
 *        to its end, the values under the frames stay and H runs for A and B.
 *   RF7  As RF4, the second image written in L after its assignment, while
 *        L's delegate call (a frame of the first image) runs: the image
 *        names L's delegate and its cursor, and the records are equal.
 */

#include <stdarg.h>

#define RF_KEY "resume-frames"
#define RF_MAX_TRACE 4096

struct rf_step { int ip, csp, sp, top; };
struct rf_result {
	int a_m0, b_m0, o_var1, top4[4];
	int ref_a, ref_b, ref_env, ref_dg, live, csp_end, sp_end;
	int nflags, flags[8];	/* per frame: method | delegate call << 1 | delegate return slots << 2 */
	bool ended;
};

enum { RF_NONE, RF_SAVE_FIRST, RF_SAVE_SECOND, RF_SAVE_SECOND_L };

static struct {
	/* program */
	int fO, fM, fL, fH, fG, dg_type, ret_slots;
	uint32_t aO, aM, aL, aH, aG;
	uint32_t at_setup, at_o_end, at_o_ret, at_snap, at_hsnap, at_lsnap;
	uint32_t imm_obj, imm_dg;	/* where the object / delegate PUSH operands are */
	int st, a, b, dg, dg2, env, other_env;
	bool conflict;			/* RF6: a second delegate, other environment */
	/* run */
	int mode;
	bool setup_done, saved, recording, exit_at_end;
	const char *file1, *file2, *trace_out;
	struct rf_step trace[RF_MAX_TRACE];
	int ntrace;
	struct rf_result res;
} rf;

static int rf_bad;
#define RF_EXPECT(c, ...) do { if (!(c)) { printf("  FAIL %s:%d: ", __func__, __LINE__); printf(__VA_ARGS__); \
		printf("\n"); fflush(stdout); rf_bad = 1; } } while (0)

static const char *rf_fmt(void) { return config.save_format == SAVE_FORMAT_RSM ? "rsm" : "json"; }

/* ---- the program --------------------------------------------------------- */

static uint8_t *rf_code;
static size_t rf_len, rf_cap;

static uint32_t rf_here(void) { return (uint32_t)(ain->code_size + rf_len); }

static uint32_t rf_emit(enum opcode op, int nargs, ...)
{
	uint32_t at = rf_here();
	if (rf_len + 2 + 4 * nargs > rf_cap) {
		rf_cap = rf_cap ? rf_cap * 2 : 1024;
		rf_code = xrealloc(rf_code, rf_cap);
	}
	if (instructions[op].nr_args != nargs) {
		printf("RESUME-FRAMES opcode %d takes %d arguments, not %d\n", op, instructions[op].nr_args, nargs);
		exit(2);
	}
	LittleEndian_putW(rf_code, rf_len, op);
	rf_len += 2;
	va_list ap;
	va_start(ap, nargs);
	for (int i = 0; i < nargs; i++) {
		LittleEndian_putDW(rf_code, rf_len, va_arg(ap, int));
		rf_len += 4;
	}
	va_end(ap);
	return at;
}

/* The code of a function of the game whose variables are ints only: its
 * body becomes `at`. */
static bool rf_int_vars(struct ain_function *f, int min_vars)
{
	if (f->nr_args != 0 || f->nr_vars < min_vars || f->nr_vars > 8)
		return false;
	for (int i = 0; i < f->nr_vars; i++)
		if (f->vars[i].type.data != AIN_INT)
			return false;
	return true;
}

static int rf_pick(enum ain_data_type ret, int min_vars, const int *taken, int ntaken)
{
	for (int i = 1; i < ain->nr_functions; i++) {
		struct ain_function *f = &ain->functions[i];
		if (!f->name || !f->name[0] || i == ain->main || i == ain->alloc || func_flags[i]
		    || f->is_lambda || f->return_type.data != ret || !rf_int_vars(f, min_vars)
		    || f->address >= ain->code_size)
			continue;
		bool used = false;
		for (int k = 0; k < ntaken; k++)
			used |= taken[k] == i;
		/* a name shared with no other function: one symbol, one function */
		if (used || ain_get_function_index(ain, f) != 0)
			continue;
		char *copy = strdup(f->name);
		int first = ain_get_function(ain, copy);
		free(copy);
		if (first != i)
			continue;
		return i;
	}
	printf("RESUME-FRAMES no function returning %d with %d int variables\n", ret, min_vars);
	exit(2);
}

static int rf_delegate_type(enum ain_data_type ret)
{
	for (int i = 0; i < ain->nr_delegates; i++) {
		struct ain_function_type *d = &ain->delegates[i];
		if (d->nr_arguments == 0 && d->return_type.data == ret && delegate_param_slots(d) == 0)
			return i;
	}
	printf("RESUME-FRAMES no delegate type returning %d without arguments\n", ret);
	exit(2);
}

static int rf_struct_two_ints(void)
{
	for (int s = 0; s < ain->nr_structures; s++) {
		struct ain_struct *as = &ain->structures[s];
		if (as->nr_members < 2 || as->constructor >= 0 || as->destructor >= 0)
			continue;
		bool ok = true;
		for (int i = 0; i < as->nr_members && ok; i++)
			ok = as->members[i].type.data == AIN_INT;
		if (ok)
			return s;
	}
	printf("RESUME-FRAMES no struct of two or more int members without constructor\n");
	exit(2);
}

static void rf_marker(void) { rf_emit(PUSH, 1, 0); rf_emit(POP, 0); }

/* Builds the program; `valued` makes D a delegate type that returns one slot
 * (and L and H return 7 and 9). */
static void rf_build(bool valued)
{
	enum ain_data_type hret = valued ? AIN_INT : AIN_VOID;
	int taken[5];
	rf.fO = taken[0] = rf_pick(AIN_VOID, 2, taken, 0);
	rf.fM = taken[1] = rf_pick(AIN_INT, 0, taken, 1);
	rf.fG = taken[2] = rf_pick(AIN_INT, 0, taken, 2);
	rf.fL = taken[3] = rf_pick(hret, 0, taken, 3);
	rf.fH = taken[4] = rf_pick(hret, 0, taken, 4);
	rf.dg_type = rf_delegate_type(valued ? AIN_BOOL : AIN_VOID);
	if (valued && delegate_return_slots(&ain->delegates[rf.dg_type].return_type) != 1) {
		printf("RESUME-FRAMES the valued delegate type returns %d slots\n",
		       delegate_return_slots(&ain->delegates[rf.dg_type].return_type));
		exit(2);
	}
	rf.ret_slots = valued ? 1 : 0;
	rf_len = 0;

	/* O */
	rf.aO = rf_here();
	rf.at_setup = rf_here();
	rf_marker();
	rf_emit(PUSHLOCALPAGE, 0); rf_emit(PUSH, 1, 0); rf_emit(PUSH, 1, 500); rf_emit(ASSIGN, 0); rf_emit(POP, 0);
	rf_emit(PUSH, 1, 1001); rf_emit(PUSH, 1, 1002); rf_emit(PUSH, 1, 1003);
	rf.imm_obj = rf_emit(PUSH, 1, 0) + 2 - (uint32_t)ain->code_size;
	rf_emit(PUSH, 1, rf.fM);
	rf_emit(CALLMETHOD, 1, 0);
	rf.at_o_end = rf_here();
	rf_marker();
	for (int i = 0; i < 4; i++)
		rf_emit(POP, 0);
	rf.at_o_ret = rf_emit(RETURN, 0);

	/* M */
	rf.aM = rf_here();
	rf_emit(PUSH, 1, 2001); rf_emit(PUSH, 1, 2002);
	rf.imm_dg = rf_emit(PUSH, 1, 0) + 2 - (uint32_t)ain->code_size;
	rf_emit(DG_CALLBEGIN, 1, rf.dg_type);
	uint32_t loop = rf_here();
	uint32_t call = rf_emit(DG_CALL, 2, rf.dg_type, 0);
	rf_emit(JUMP, 1, (int)loop);
	uint32_t end = rf_here();
	LittleEndian_putDW(rf_code, call + 6 - ain->code_size, (int)end);
	if (valued)
		rf_emit(ADD, 0);
	rf_emit(ADD, 0);
	rf_emit(RETURN, 0);

	/* L */
	rf.aL = rf_here();
	rf_emit(PUSHLOCALPAGE, 0); rf_emit(X_GETENV, 0); rf_emit(PUSH, 1, 1);
	rf_emit(PUSH, 1, 3001); rf_emit(CALLFUNC, 1, rf.fG); rf_emit(ADD, 0);
	rf_emit(PUSHLOCALPAGE, 0); rf_emit(X_GETENV, 0); rf_emit(PUSH, 1, 0); rf_emit(REF, 0);
	rf_emit(ADD, 0); rf_emit(ASSIGN, 0); rf_emit(POP, 0);
	rf.at_lsnap = rf_here();
	rf_marker();
	if (valued)
		rf_emit(PUSH, 1, 7);
	rf_emit(RETURN, 0);

	/* G */
	rf.aG = rf_here();
	rf.at_snap = rf_here();
	rf_marker();
	rf_emit(PUSH, 1, 4242);
	rf_emit(RETURN, 0);

	/* H */
	rf.aH = rf_here();
	rf_emit(PUSHSTRUCTPAGE, 0); rf_emit(PUSH, 1, 0);
	rf_emit(PUSHSTRUCTPAGE, 0); rf_emit(PUSH, 1, 0); rf_emit(REF, 0);
	rf_emit(PUSH, 1, 10); rf_emit(ADD, 0); rf_emit(ASSIGN, 0); rf_emit(POP, 0);
	rf.at_hsnap = rf_here();
	rf_marker();
	if (valued)
		rf_emit(PUSH, 1, 9);
	rf_emit(RETURN, 0);

	uint8_t *code = xmalloc(ain->code_size + rf_len);
	memcpy(code, ain->code, ain->code_size);
	memcpy(code + ain->code_size, rf_code, rf_len);
	ain->code = code;	/* the old buffer is left; nothing else holds it */
	ain->code_size += rf_len;
	ain->functions[rf.fO].address = rf.aO;
	ain->functions[rf.fM].address = rf.aM;
	ain->functions[rf.fL].address = rf.aL;
	ain->functions[rf.fG].address = rf.aG;
	ain->functions[rf.fH].address = rf.aH;

	rf.st = rf_struct_two_ints();
	rf.a = alloc_struct(rf.st);
	rf.b = alloc_struct(rf.st);
	LittleEndian_putDW(ain->code, rf.imm_obj + (ain->code_size - rf_len), rf.a);
}

/* D, made when O starts (its environment is O's local page). */
static int rf_delegate(int env_l)
{
	int entries[3][3] = { { rf.a, rf.fL, env_l }, { rf.a, rf.fH, 0 }, { rf.b, rf.fH, 0 } };
	struct page *d = alloc_page(DELEGATE_PAGE, 0, 9);
	for (int e = 0; e < 3; e++) {
		d->values[e*3].i = entries[e][0];
		d->values[e*3+1].i = entries[e][1];
		d->values[e*3+2].i = entries[e][2];
		if (heap_index_valid(entries[e][2]))
			heap_ref(entries[e][2]);
	}
	int slot = heap_alloc_page(d);
	delegate_page_register(slot);
	return slot;
}

static void rf_setup_delegates(void)
{
	rf.env = call_stack[call_stack_ptr-1].page_slot;
	rf.dg = rf_delegate(rf.env);
	LittleEndian_putDW(ain->code, rf.imm_dg + (ain->code_size - rf_len), rf.dg);
	if (rf.conflict) {
		/* the same entry for L, another environment, in another delegate */
		rf.other_env = alloc_struct(rf.st);
		struct page *d = alloc_page(DELEGATE_PAGE, 0, 3);
		d->values[0].i = rf.a;
		d->values[1].i = rf.fL;
		d->values[2].i = rf.other_env;
		heap_ref(rf.other_env);
		rf.dg2 = heap_alloc_page(d);
		delegate_page_register(rf.dg2);
	}
}

/* ---- the run -------------------------------------------------------------- */

static int rf_live(void)
{
	int n = 0;
	for (size_t i = 0; i < heap_size; i++)
		n += heap[i].ref > 0;
	return n;
}

static int rf_member(int obj, int i)
{
	return heap_index_valid(obj) && heap[obj].page && heap[obj].page->nr_vars > i ? heap[obj].page->values[i].i : -999;
}

/* The frames' flags: where an image is written, and right after a load. */
static void rf_take_flags(void)
{
	rf.res.nflags = call_stack_ptr < 8 ? call_stack_ptr : 8;
	for (int i = 0; i < rf.res.nflags; i++)
		rf.res.flags[i] = call_stack[i].is_method | call_stack[i].is_delegate_call << 1
			| call_stack[i].dg_return_slots << 2;
}

static bool rf_save(const char *file)
{
	stack_push(0);
	stack_push(0);
	int ok = vm_save_image(RF_KEY, file);
	stack_pop();
	stack_pop();
	return ok;
}

static void rf_write_trace(void)
{
	char path[PATH_MAX];
	snprintf(path, sizeof path, "%s/%s", sp_dir, rf.trace_out);
	FILE *fp = fopen(path, "wb");
	if (fp) {
		fwrite(&rf.ntrace, sizeof rf.ntrace, 1, fp);
		fwrite(rf.trace, sizeof rf.trace[0], rf.ntrace, fp);
		fwrite(&rf.res, sizeof rf.res, 1, fp);
		fclose(fp);
	}
}

static void resume_frames_probe_step(void)
{
	if (!rf.aO)
		return;
	uint32_t ip = instr_ptr;
	if (ip == rf.at_setup && !rf.setup_done) {
		rf.setup_done = true;
		rf_setup_delegates();
	}
	if (ip == rf.at_snap && rf.mode == RF_SAVE_FIRST && !rf.saved) {
		rf.saved = true;
		rf_take_flags();
		if (!rf_save(rf.file1)) {
			printf("RESUME-FRAMES the image was not written\n");
			exit(2);
		}
		rf.recording = true;
	}
	if (((ip == rf.at_hsnap && rf.mode == RF_SAVE_SECOND) || (ip == rf.at_lsnap && rf.mode == RF_SAVE_SECOND_L))
	    && !rf.saved) {
		rf.saved = true;
		rf_take_flags();
		if (!rf_save(rf.file2)) {
			printf("RESUME-FRAMES the second image was not written\n");
			exit(2);
		}
		rf.recording = true;
	}
	if (!rf.recording)
		return;
	if (rf.ntrace < RF_MAX_TRACE)
		rf.trace[rf.ntrace++] = (struct rf_step) { (int)ip, call_stack_ptr, stack_ptr,
			stack_ptr > 0 ? stack[stack_ptr-1].i : -77777 };
	if (ip == rf.at_o_end) {
		for (int i = 0; i < 4; i++)
			rf.res.top4[i] = stack_ptr >= 4 ? stack[stack_ptr - 4 + i].i : -77777;
	}
	if (ip == rf.at_o_ret) {
		int env = call_stack_ptr > 0 ? call_stack[call_stack_ptr-1].page_slot : -1;
		rf.res.a_m0 = rf_member(rf.a, 0);
		rf.res.b_m0 = rf_member(rf.b, 0);
		rf.res.o_var1 = rf_member(env, 1);
		rf.res.ref_a = heap_index_valid(rf.a) ? heap[rf.a].ref : -1;
		rf.res.ref_b = heap_index_valid(rf.b) ? heap[rf.b].ref : -1;
		rf.res.ref_env = heap_index_valid(env) ? heap[env].ref : -1;
		rf.res.ref_dg = heap_index_valid(rf.dg) ? heap[rf.dg].ref : -1;
		rf.res.live = rf_live();
		rf.res.csp_end = call_stack_ptr;
		rf.res.sp_end = stack_ptr;
		rf.res.ended = true;
		if (rf.exit_at_end) {
			rf_write_trace();
			fflush(stdout);
			_exit(0);
		}
	}
}

/* Runs the program from O's start (the reference: the image is written at
 * SNAP and the run goes on). */
static void rf_run_reference(void)
{
	rf.mode = RF_SAVE_FIRST;
	rf.saved = rf.recording = rf.exit_at_end = false;
	rf.ntrace = 0;
	memset(&rf.res, 0, sizeof rf.res);
	stack_ptr = 0;
	if (_function_call(rf.fO, VM_RETURN) <= 1) {
		printf("RESUME-FRAMES O could not be called\n");
		exit(2);
	}
	vm_execute();
}

struct rf_record { int n; struct rf_step *steps; struct rf_result res; int status; };

/* In another process: load `file` and run from there, recording; with
 * `second` (RF_SAVE_SECOND: in H; RF_SAVE_SECOND_L: in L, after its
 * assignment), write the second image there and record from there. */
static void rf_run_loaded(const char *file, int second, const char *trace_name, struct rf_record *out)
{
	fflush(stdout);
	fflush(stderr);
	pid_t pid = fork();
	if (pid == 0) {
		alarm(60);
		rf.mode = second ? second : RF_NONE;
		rf.saved = false;
		rf.recording = !second;
		rf.exit_at_end = true;
		rf.trace_out = trace_name;
		rf.ntrace = 0;
		memset(&rf.res, 0, sizeof rf.res);
		vm_load_image(RF_KEY, file);
		if (!second)
			rf_take_flags();
		vm_execute();
		rf_write_trace();
		fflush(stdout);
		_exit(3);
	}
	int st = 0;
	waitpid(pid, &st, 0);
	out->status = st;
	out->n = 0;
	out->steps = NULL;
	memset(&out->res, 0, sizeof out->res);
	char path[PATH_MAX];
	snprintf(path, sizeof path, "%s/%s", sp_dir, trace_name);
	FILE *fp = fopen(path, "rb");
	if (!fp)
		return;
	if (fread(&out->n, sizeof out->n, 1, fp) == 1 && out->n >= 0 && out->n <= RF_MAX_TRACE) {
		out->steps = xcalloc(out->n + 1, sizeof *out->steps);
		if (fread(out->steps, sizeof *out->steps, out->n, fp) != (size_t)out->n
		    || fread(&out->res, sizeof out->res, 1, fp) != 1)
			out->n = -1;
	}
	fclose(fp);
}

static void rf_compare(const char *what, const struct rf_step *ref, int nref, const struct rf_result *rres,
		       const struct rf_record *got)
{
	RF_EXPECT(WIFEXITED(got->status) && WEXITSTATUS(got->status) == 0, "%s %s: the loaded run ended with %s %d",
		  rf_fmt(), what, WIFSIGNALED(got->status) ? "signal" : "exit",
		  WIFSIGNALED(got->status) ? WTERMSIG(got->status) : WEXITSTATUS(got->status));
	RF_EXPECT(got->res.ended, "%s %s: the loaded run did not reach O's return", rf_fmt(), what);
	int n = nref < got->n ? nref : got->n;
	int diff = -1;
	for (int i = 0; i < n && diff < 0; i++) {
		if (memcmp(&ref[i], &got->steps[i], sizeof ref[i]))
			diff = i;
	}
	if (diff < 0 && nref != got->n)
		diff = n;
	if (diff >= 0) {
		const struct rf_step *r = diff < nref ? &ref[diff] : NULL, *g = diff < got->n ? &got->steps[diff] : NULL;
		RF_EXPECT(false, "%s %s: the runs part at step %d of %d/%d: reference ip %d frames %d sp %d top %d; "
			  "loaded ip %d frames %d sp %d top %d", rf_fmt(), what, diff, nref, got->n,
			  r ? r->ip : -1, r ? r->csp : -1, r ? r->sp : -1, r ? r->top : -1,
			  g ? g->ip : -1, g ? g->csp : -1, g ? g->sp : -1, g ? g->top : -1);
	}
	const struct rf_result *a = rres, *b = &got->res;
	RF_EXPECT(a->nflags == b->nflags && !memcmp(a->flags, b->flags, sizeof a->flags),
		  "%s %s: the frames' flags after the load %d: %d %d %d %d; where written %d: %d %d %d %d", rf_fmt(), what,
		  b->nflags, b->flags[0], b->flags[1], b->flags[2], b->flags[3], a->nflags, a->flags[0], a->flags[1],
		  a->flags[2], a->flags[3]);
	RF_EXPECT(!memcmp(a->top4, b->top4, sizeof a->top4), "%s %s: at O_END %d %d %d %d, reference %d %d %d %d",
		  rf_fmt(), what, b->top4[0], b->top4[1], b->top4[2], b->top4[3], a->top4[0], a->top4[1], a->top4[2],
		  a->top4[3]);
	RF_EXPECT(a->a_m0 == b->a_m0 && a->b_m0 == b->b_m0 && a->o_var1 == b->o_var1,
		  "%s %s: A %d B %d O.var1 %d, reference %d %d %d", rf_fmt(), what, b->a_m0, b->b_m0, b->o_var1,
		  a->a_m0, a->b_m0, a->o_var1);
	RF_EXPECT(a->ref_a == b->ref_a && a->ref_b == b->ref_b && a->ref_env == b->ref_env && a->ref_dg == b->ref_dg
		  && a->live == b->live, "%s %s: references A %d B %d env %d D %d, live %d; reference %d %d %d %d, %d",
		  rf_fmt(), what, b->ref_a, b->ref_b, b->ref_env, b->ref_dg, b->live, a->ref_a, a->ref_b, a->ref_env,
		  a->ref_dg, a->live);
}

/* What the reference itself must show. */
static void rf_expect_reference(bool valued)
{
	int m = valued ? 2001 + 2002 + 9 : 4003;
	RF_EXPECT(rf.res.ended, "%s: the reference did not reach O's return", rf_fmt());
	RF_EXPECT(rf.res.top4[0] == 1001 && rf.res.top4[1] == 1002 && rf.res.top4[2] == 1003 && rf.res.top4[3] == m,
		  "%s: reference at O_END %d %d %d %d, want 1001 1002 1003 %d", rf_fmt(), rf.res.top4[0], rf.res.top4[1],
		  rf.res.top4[2], rf.res.top4[3], m);
	RF_EXPECT(rf.res.o_var1 == 3001 + 4242 + 500 && rf.res.a_m0 == 10 && rf.res.b_m0 == 10,
		  "%s: reference O.var1 %d A %d B %d, want %d 10 10", rf_fmt(), rf.res.o_var1, rf.res.a_m0, rf.res.b_m0,
		  3001 + 4242 + 500);
}

/* Takes the frame fields out of an image (an image of an earlier build). */
static bool rf_strip(const char *file)
{
	char path[PATH_MAX];
	snprintf(path, sizeof path, "%s/%s", sp_dir, file);
	if (config.save_format == SAVE_FORMAT_RSM) {
		enum savefile_error error;
		struct rsave *rs = rsave_read(path, RSAVE_READ_ALL, &error);
		if (error != SAVEFILE_SUCCESS)
			return false;
		for (int i = 0; i < rs->nr_comments; i++)
			free(rs->comments[i]);
		free(rs->comments);
		rs->comments = NULL;
		rs->nr_comments = 0;
		FILE *fp = fopen(path, "wb");
		bool ok = fp && rsave_write(rs, fp, true, 1) == SAVEFILE_SUCCESS;
		if (fp)
			fclose(fp);
		rsave_free(rs);
		return ok;
	}
	char *text = file_read(path, NULL);
	cJSON *image = text ? cJSON_Parse(text) : NULL;
	free(text);
	cJSON *frames = image ? cJSON_GetObjectItem(image, "call-stack") : NULL;
	if (!frames)
		return false;
	static const char *const keys[] = { "env-page", "delegate-obj-ref", "delegate-env-ref", "delegate-page",
		"delegate-cursor", "delegate-limit" };
	cJSON *item;
	cJSON_ArrayForEach(item, frames) {
		for (int k = 0; k < 6; k++)
			cJSON_DeleteItemFromObject(item, keys[k]);
	}
	char *out = cJSON_PrintUnformatted(image);
	cJSON_Delete(image);
	FILE *fp = fopen(path, "wb");
	bool ok = fp && fputs(out, fp) >= 0;
	if (fp)
		fclose(fp);
	free(out);
	return ok;
}

static int rf_case(bool valued, bool strip)
{
	rf_bad = 0;
	rf_build(valued);
	rf.file1 = "rf1.dat";
	rf_run_reference();
	rf_expect_reference(valued);
	if (strip)
		RF_EXPECT(rf_strip(rf.file1), "%s: the frame fields could not be taken out", rf_fmt());
	struct rf_step *ref = xcalloc(rf.ntrace + 1, sizeof *ref);
	memcpy(ref, rf.trace, rf.ntrace * sizeof *ref);
	int nref = rf.ntrace;
	struct rf_result rres = rf.res;
	struct rf_record got;
	rf_run_loaded(rf.file1, RF_NONE, "rf1.trace", &got);
	rf_compare(strip ? "image without frame fields" : valued ? "delegate with a value" : "image", ref, nref, &rres,
		   &got);
	printf("RF %s %s%s: %d steps after the image, loaded run %d steps\n", rf_fmt(), valued ? "valued delegate" : "nested frames",
	       strip ? " (no frame fields)" : "", nref, got.n);
	free(ref);
	free(got.steps);
	return rf_bad;
}

static int rf1_rsm(void) { config.save_format = SAVE_FORMAT_RSM; return rf_case(false, false); }
static int rf1_json(void) { config.save_format = SAVE_FORMAT_JSON; return rf_case(false, false); }
static int rf2_rsm(void) { config.save_format = SAVE_FORMAT_RSM; return rf_case(true, false); }
static int rf2_json(void) { config.save_format = SAVE_FORMAT_JSON; return rf_case(true, false); }
static int rf3_rsm(void) { config.save_format = SAVE_FORMAT_RSM; return rf_case(false, true); }
static int rf3_json(void) { config.save_format = SAVE_FORMAT_JSON; return rf_case(false, true); }

/* The frame fields of frame i of an image (rsm: the comment; json: the
 * entry): delegate page and cursor; false if the image has none. */
static bool rf_image_frame(const char *file, int i, int *dg_page, int *cursor)
{
	char path[PATH_MAX];
	snprintf(path, sizeof path, "%s/%s", sp_dir, file);
	bool found = false;
	if (config.save_format == SAVE_FORMAT_RSM) {
		enum savefile_error error;
		struct rsave *rs = rsave_read(path, RSAVE_READ_ALL, &error);
		if (error != SAVEFILE_SUCCESS)
			return false;
		for (int c = 0; c < rs->nr_comments && !found; c++) {
			const char *t = rs->comments[c];
			if (strncmp(t, "xsystem4-frames 1 ", 18))
				continue;
			for (int k = 0; k <= i && t; k++)
				t = strchr(t + 1, '|');
			int v[7];
			found = t && sscanf(t, "|%d %d %d %d %d %d %d", &v[0], &v[1], &v[2], &v[3], &v[4], &v[5], &v[6]) == 7;
			if (found) {
				*dg_page = v[4];
				*cursor = v[5];
			}
		}
		rsave_free(rs);
		return found;
	}
	char *text = file_read(path, NULL);
	cJSON *image = text ? cJSON_Parse(text) : NULL;
	free(text);
	cJSON *frames = image ? cJSON_GetObjectItem(image, "call-stack") : NULL;
	cJSON *e = frames ? cJSON_GetArrayItem(frames, i) : NULL;
	cJSON *p = e ? cJSON_GetObjectItem(e, "delegate-page") : NULL, *c = e ? cJSON_GetObjectItem(e, "delegate-cursor") : NULL;
	if (p && c) {
		*dg_page = p->valueint;
		*cursor = c->valueint;
		found = true;
	}
	cJSON_Delete(image);
	return found;
}

/* RF4: a second image while frames of the first are running (in H: O and M
 * from the first image, H's delegate call a new frame); RF7: the same in L,
 * whose delegate call is itself a frame of the first image. */
static int rf4(int where)
{
	rf_bad = 0;
	rf_build(false);
	rf.file1 = "rf1.dat";
	rf.file2 = "rf2.dat";
	rf_run_reference();
	rf_expect_reference(false);
	struct rf_record first, second;
	rf_run_loaded(rf.file1, where, "rf4a.trace", &first);
	RF_EXPECT(WIFEXITED(first.status) && WEXITSTATUS(first.status) == 0 && first.res.ended,
		  "%s: the run from the first image did not end (status %d)", rf_fmt(), first.status);
	if (where == RF_SAVE_SECOND_L) {
		int dgp = -1, cur = -1;
		RF_EXPECT(rf_image_frame(rf.file2, 2, &dgp, &cur) && dgp == rf.dg && cur == 1,
			  "%s: in the second image L's delegate is %d, cursor %d; want %d and 1", rf_fmt(), dgp, cur, rf.dg);
	}
	rf_run_loaded(rf.file2, RF_NONE, "rf4b.trace", &second);
	rf_compare("second image", first.steps, first.n, &first.res, &second);
	RF_EXPECT(first.res.a_m0 == 10 && first.res.b_m0 == 10 && first.res.o_var1 == 3001 + 4242 + 500,
		  "%s: the run from the first image: A %d B %d O.var1 %d", rf_fmt(), first.res.a_m0, first.res.b_m0,
		  first.res.o_var1);
	RF_EXPECT(first.res.top4[0] == 1001 && first.res.top4[1] == 1002 && first.res.top4[2] == 1003 && first.res.top4[3] == 4003,
		  "%s: the run from the first image at O_END %d %d %d %d, want 1001 1002 1003 4003", rf_fmt(), first.res.top4[0],
		  first.res.top4[1], first.res.top4[2], first.res.top4[3]);
	printf("RF %s second image: %d steps after it, loaded run %d steps\n", rf_fmt(), first.n, second.n);
	free(first.steps);
	free(second.steps);
	return rf_bad;
}
static int rf4_rsm(void) { config.save_format = SAVE_FORMAT_RSM; return rf4(RF_SAVE_SECOND); }
static int rf4_json(void) { config.save_format = SAVE_FORMAT_JSON; return rf4(RF_SAVE_SECOND); }
static int rf7_rsm(void) { config.save_format = SAVE_FORMAT_RSM; return rf4(RF_SAVE_SECOND_L); }
static int rf7_json(void) { config.save_format = SAVE_FORMAT_JSON; return rf4(RF_SAVE_SECOND_L); }

/* RF5: what the image holds. */
static int rf5(void)
{
	rf_bad = 0;
	rf_build(false);
	rf.file1 = "rf1.dat";
	rf_run_reference();
	char path[PATH_MAX];
	snprintf(path, sizeof path, "%s/%s", sp_dir, rf.file1);
	if (config.save_format == SAVE_FORMAT_RSM) {
		enum savefile_error error;
		struct rsave *rs = rsave_read(path, RSAVE_READ_ALL, &error);
		RF_EXPECT(error == SAVEFILE_SUCCESS, "rsm: the image does not read");
		if (error != SAVEFILE_SUCCESS)
			return rf_bad;
		RF_EXPECT(rs->nr_comments == 1 && !strncmp(rs->comments[0], "xsystem4-frames 1 4|", 20),
			  "rsm: %d comment(s), the first \"%.40s\"; want one, \"xsystem4-frames 1 4|...\"", rs->nr_comments,
			  rs->nr_comments ? rs->comments[0] : "");
		rsave_free(rs);
		/* the game's comments of the image: none */
		int ok = 0;
		struct page *c = vm_load_image_comments(RF_KEY, rf.file1, &ok);
		RF_EXPECT(ok && !c, "rsm: the image's comments read as %d entries (ok %d), want none",
			  c ? c->nr_vars : -1, ok);
		/* writing the game's comments keeps the frame fields */
		union vm_value dim = { .i = 1 };
		struct page *mine = alloc_array(1, &dim, AIN_ARRAY_STRING, 0, false);
		int s = heap_alloc_slot(VM_STRING);
		heap[s].s = make_string("a comment", 9);
		mine->values[0].i = s;
		RF_EXPECT(vm_write_image_comments(RF_KEY, rf.file1, mine), "rsm: the comments were not written");
		rs = rsave_read(path, RSAVE_READ_ALL, &error);
		RF_EXPECT(error == SAVEFILE_SUCCESS && rs->nr_comments == 2 && !strcmp(rs->comments[0], "a comment")
			  && !strncmp(rs->comments[1], "xsystem4-frames 1 4|", 20),
			  "rsm: after writing a comment the image has %d comment(s)", error == SAVEFILE_SUCCESS ? rs->nr_comments : -1);
		if (error == SAVEFILE_SUCCESS)
			rsave_free(rs);
		c = vm_load_image_comments(RF_KEY, rf.file1, &ok);
		RF_EXPECT(ok && c && c->nr_vars == 1 && !strcmp(heap_get_string(c->values[0].i)->text, "a comment"),
			  "rsm: the comments read back as %d entries, want the one written", c ? c->nr_vars : -1);
	} else {
		char *text = file_read(path, NULL);
		cJSON *image = text ? cJSON_Parse(text) : NULL;
		free(text);
		cJSON *frames = image ? cJSON_GetObjectItem(image, "call-stack") : NULL;
		RF_EXPECT(frames && cJSON_GetArraySize(frames) == 4, "json: %d call-stack entries, want 4",
			  frames ? cJSON_GetArraySize(frames) : -1);
		static const char *const keys[] = { "env-page", "delegate-obj-ref", "delegate-env-ref", "delegate-page",
			"delegate-cursor", "delegate-limit" };
		cJSON *item;
		int i = 0;
		cJSON_ArrayForEach(item, frames) {
			for (int k = 0; k < 6; k++)
				RF_EXPECT(cJSON_IsNumber(cJSON_GetObjectItem(item, keys[k])), "json: entry %d has no \"%s\"", i, keys[k]);
			i++;
		}
		cJSON *l = frames ? cJSON_GetArrayItem(frames, 2) : NULL;
		cJSON *env = l ? cJSON_GetObjectItem(l, "env-page") : NULL, *cur = l ? cJSON_GetObjectItem(l, "delegate-cursor") : NULL;
		RF_EXPECT(env && env->valueint == rf.env && cur && cur->valueint == 1,
			  "json: L's entry has environment %d and cursor %d, want %d and 1", env ? env->valueint : -1,
			  cur ? cur->valueint : -1, rf.env);
		cJSON_Delete(image);
	}
	printf("RF5 %s: the frame fields in the image\n", rf_fmt());
	return rf_bad;
}
static int rf5_rsm(void) { config.save_format = SAVE_FORMAT_RSM; return rf5(); }
static int rf5_json(void) { config.save_format = SAVE_FORMAT_JSON; return rf5(); }

/* RF6: no unique environment for the rule. */
static int rf6(void)
{
	rf_bad = 0;
	rf_build(false);
	rf.conflict = true;
	rf.file1 = "rf1.dat";
	rf_run_reference();
	rf_expect_reference(false);
	RF_EXPECT(rf_strip(rf.file1), "%s: the frame fields could not be taken out", rf_fmt());
	struct rf_record got;
	rf_run_loaded(rf.file1, RF_NONE, "rf6.trace", &got);
	RF_EXPECT(WIFEXITED(got.status) && WEXITSTATUS(got.status) == 0 && got.res.ended,
		  "%s: the loaded run did not end (status %d)", rf_fmt(), got.status);
	RF_EXPECT(got.res.a_m0 == 10 && got.res.b_m0 == 10, "%s: H ran for A %d, B %d after the load, want 10 and 10",
		  rf_fmt(), got.res.a_m0, got.res.b_m0);
	RF_EXPECT(got.res.top4[0] == 1001 && got.res.top4[1] == 1002 && got.res.top4[2] == 1003 && got.res.top4[3] == 4003,
		  "%s: at O_END %d %d %d %d, want 1001 1002 1003 4003", rf_fmt(), got.res.top4[0], got.res.top4[1],
		  got.res.top4[2], got.res.top4[3]);
	printf("RF6 %s: no unique environment: O.var1 %d (reference %d)\n", rf_fmt(), got.res.o_var1, rf.res.o_var1);
	free(got.steps);
	return rf_bad;
}
static int rf6_rsm(void) { config.save_format = SAVE_FORMAT_RSM; return rf6(); }
static int rf6_json(void) { config.save_format = SAVE_FORMAT_JSON; return rf6(); }

static int test_resume_frames(void)
{
	sp_setup_dir();
	sp_run("RF1 rsm nested frames", rf1_rsm);
	sp_run("RF1 json nested frames", rf1_json);
	sp_run("RF2 rsm delegate with a value", rf2_rsm);
	sp_run("RF2 json delegate with a value", rf2_json);
	sp_run("RF3 rsm image without frame fields", rf3_rsm);
	sp_run("RF3 json image without frame fields", rf3_json);
	sp_run("RF4 rsm second image over read frames", rf4_rsm);
	sp_run("RF4 json second image over read frames", rf4_json);
	sp_run("RF5 rsm frame fields in the image", rf5_rsm);
	sp_run("RF5 json frame fields in the image", rf5_json);
	sp_run("RF6 rsm no unique environment", rf6_rsm);
	sp_run("RF6 json no unique environment", rf6_json);
	sp_run("RF7 rsm second image in a read delegate call", rf7_rsm);
	sp_run("RF7 json second image in a read delegate call", rf7_json);
	sp_remove_dir();
	printf("RESUME-FRAMES summary: %d/%d case(s) failed\n", sp_failed_cases, sp_cases);
	return sp_failed_cases ? 88 : 0;
}
