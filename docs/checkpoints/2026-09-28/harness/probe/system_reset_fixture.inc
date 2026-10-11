/*
 * system-reset: system.Reset restarts the VM (headless).
 *
 * The original's handler 0x693e80 clears the VM's run flag and asks the host
 * for a restart (0x4b5f00); the instruction handler 0x667ef0 executes nothing
 * after that, and the host loop 0x4b5a60 releases the VM and every engine
 * (0x4b30f0) and builds them again (0x4b4bf0). The script hands the save to
 * load over the restart with SystemService.GameVariable_Set, which the host
 * keeps (0x695129 writes through an interface of the host object).
 *
 * The game calls system.Reset in three functions: the return to the title,
 * ResetLoadManager::Run (load a save) and the Game Over scene. SR1 runs the
 * game's own CALLHLL in each of them, with the instructions that follow it:
 * a frame of the function is entered as the engine enters it and execution
 * starts at that instruction.
 *
 * The probe does not go through vm_execute_ain (it would start the whole
 * game): it stands at the setjmp itself and then runs what vm_execute_ain
 * runs there, vm_reset_runtime_state and heap_init. That vm_execute_ain
 * does call them is not covered here; the GUI runs cover it.
 *
 * SR1 the three call sites restart the VM; nothing after the call runs
 * SR2 what the script hands over survives, IsResetOnce turns true
 * SR3 twelve restarts in a row; the counters a longjmp leaves are back
 * SR4 the heap of the old run is freed (v14); before v14 it is left alone
 * SR5 the parts engine keeps nothing: messages, free numbers, motion, parts,
 *     the message window's show flag, the activity parts numbers
 * SR6 the HashMap handles are handed back
 * SR7 guards: through the ffi alone the call only raises the flag; an
 *     ordinary library call leaves it down
 * SR8 the main EX file is not read again by a restart (its old copy cannot
 *     be freed and would be lost each time) and answers as the file on disk
 *     does; once AddEX has appended to it, a restart does read it again
 * SR9 a CG name shorter than the "<save>" prefix is not read past its end
 *     (the load screen sets such names; a sanitizer build stopped there)
 * SR10 the backlog (AnteaterADVLogList) of the old run is gone: the original
 *     frees the log object with the engines (0x4b3248, in 0x4b30f0)
 * SR11 no sound channel of the old run stays prepared or playing and the
 *     free IDs start over (guard: the sprite and parts engines' _ModuleFini
 *     reset the audio, KiwiSoundEngine has none of its own)
 * SR12 the restart itself runs the libraries' _ModuleFini, which SR5 and SR6
 *     call directly: parts and HashMap handles are gone after the CALLHLL
 *
 * SR10 to SR12 restart with the libraries counted as started, as
 * init_libraries leaves them: vm_free() then runs the _ModuleFini of every
 * library the AIN declares.
 *
 * Symbols this change adds are looked up with dlsym, so the fixture builds
 * against an engine without them and the cases fail there one by one.
 */
#include <setjmp.h>
#include <sanitizer/allocator_interface.h>

static int sr_failures;
#define SR_CHECK(c, ...) do { if (!(c)) { printf("  FAIL: "); printf(__VA_ARGS__); printf("\n"); sr_failures++; } } while (0)

enum { SR_TITLE = 20526, SR_LOAD = 30447, SR_GAMEOVER = 37104, SR_IS_RESET_ONCE = 20527 };

static void *sr_fn(struct static_library *lib, const char *name)
{
	for (int i = 0; lib->functions[i].name; i++)
		if (!strcmp(lib->functions[i].name, name))
			return lib->functions[i].fun;
	return NULL;
}

/* The loop after the title's system.Reset is `system.Peek; jump back`: an
 * engine that returns from the call spins there. */
static int sr_peeks;
static jmp_buf sr_stuck;
static void sr_peek(void)
{
	if (++sr_peeks >= 3)
		longjmp(sr_stuck, 1);
}

/* The address of the CALLHLL system.Reset in function fno; 0 when it has none. */
static size_t sr_reset_addr(int fno)
{
	int lib = sp_lib("system"), fn = sp_libfn(lib, "Reset");
	size_t start = ain->functions[fno].address, end = ain->code_size;
	for (int i = 0; i < ain->nr_functions; i++) {
		size_t a = ain->functions[i].address;
		if (a > start && a < end)
			end = a;
	}
	for (size_t a = start; a + 2 <= end; ) {
		int op = get_opcode(a);
		if (op < 0 || op >= NR_OPCODES)
			break;
		if (op == CALLHLL && LittleEndian_getDW(ain->code, a + 2) == lib
				&& LittleEndian_getDW(ain->code, a + 6) == fn)
			return a;
		a += instruction_width(op);
	}
	return 0;
}

/* Enter fno as a call does and run it from its system.Reset on. */
static void sr_run_from_reset(int fno)
{
	size_t at = sr_reset_addr(fno);
	int slot = _function_call(fno, VM_RETURN);
	/* nobody pushed arguments: no slot for the RETURN to release */
	for (int i = 0; i < ain->functions[fno].nr_args; i++)
		heap[slot].page->values[i].i = -1;
	instr_ptr = at;
	vm_execute();
}

/* What vm_execute_ain runs after its setjmp, up to the libraries. */
static bool sr_restart_vm(void)
{
	void (*state)(void) = (void (*)(void))dlsym(RTLD_DEFAULT, "vm_reset_runtime_state");
	if (state) {
		state();
	} else {
		/* the engine before this change */
		stack_ptr = 0;
		call_stack_ptr = 0;
		vm_call_depth = 0;
	}
	heap_init();
	heap[0].ref = heap[global_page_slot].ref = 1;
	heap[0].type = heap[global_page_slot].type = VM_PAGE;
	heap[0].seq = heap_next_seq++;
	heap[global_page_slot].seq = heap_next_seq++;
	heap[0].page = alloc_page(ARRAY_PAGE, AIN_VOID, 64);
	heap[global_page_slot].page = alloc_page(GLOBAL_PAGE, 0, ain->nr_globals);
	for (int i = 0; i < ain->nr_globals; i++)
		heap[global_page_slot].page->values[i].i = -1;
	instr_ptr = VM_RETURN;
	return state != NULL;
}

/* 1: the VM restarted. 2: the function returned. 3: stuck in the Peek loop. */
static int sr_reset_from(int fno)
{
	static volatile int how;
	how = 0;
	sr_peeks = 0;
	if (setjmp(sr_stuck)) {
		how = 3;
	} else if (setjmp(reset_buf)) {
		how = 1;
	} else {
		sr_run_from_reset(fno);
		how = 2;
	}
	return how;
}

static bool *sr_pending(void)
{
	return (bool *)dlsym(RTLD_DEFAULT, "vm_reset_pending");
}

static void sr_setup(void)
{
	gi_override_hll("system", "Peek", sr_peek);
}

/* A restart with the libraries counted as started (the probe links them
 * without init_libraries): vm_free() runs every library's _ModuleFini. */
static int sr_reset_with_libraries(void)
{
	extern bool libraries_initialized;
	sr_setup();
	libraries_initialized = true;
	int how = sr_reset_from(SR_LOAD);
	libraries_initialized = false;
	if (how == 1)
		sr_restart_vm();
	return how;
}

/* ---- SR1: the three call sites ------------------------------------------------ */

static void sr_call_sites(void)
{
	static const int sites[] = { SR_TITLE, SR_LOAD, SR_GAMEOVER };
	static const char *const names[] = { "the return to the title", "ResetLoadManager::Run", "the Game Over scene" };
	sr_setup();
	for (int i = 0; i < 3; i++) {
		int fno = sites[i];
		SR_CHECK(sr_reset_addr(fno), "%s (function %d) has no CALLHLL system.Reset", names[i], fno);
		if (!sr_reset_addr(fno))
			continue;
		unsigned long ret0 = returns[fno];
		int how = sr_reset_from(fno);
		SR_CHECK(how == 1, "%s: %s", names[i], how == 2 ? "system.Reset returned and the function went on to its RETURN"
				: how == 3 ? "system.Reset returned and the script waits in its Peek loop" : "nothing happened");
		SR_CHECK(sr_peeks == 0, "%s: system.Peek ran %d time(s) after the reset", names[i], sr_peeks);
		SR_CHECK(returns[fno] == ret0, "%s: the RETURN after the reset ran", names[i]);
		SR_CHECK(sr_pending() && !*sr_pending(), "%s: the request is still up after the restart", names[i]);
		sr_restart_vm();
		SR_CHECK(live_slots() == 0, "%s: %zu slot(s) alive in the new heap", names[i], live_slots());
	}
	printf("  SR1 the three call sites restart the VM\n");
}

/* ---- SR2: what crosses the restart -------------------------------------------- */

static void sr_handover(void)
{
	extern struct static_library lib_SystemService;
	void (*set)(struct string *, struct string *) = sr_fn(&lib_SystemService, "GameVariable_Set");
	bool (*exist)(struct string *) = sr_fn(&lib_SystemService, "GameVariable_IsExist");
	struct string *(*get)(struct string *) = sr_fn(&lib_SystemService, "GameVariable_Get");
	bool (*once)(void) = sr_fn(&lib_SystemService, "IsResetOnce");
	assert(set && exist && get && once);
	sr_setup();
	struct string *index = make_string("ResetLoadTargetIndex", 20), *type = make_string("ResetLoadType", 13);
	struct string *seven = make_string("7", 1), *one = make_string("1", 1);
	SR_CHECK(!once() && !vm_reset_once, "IsResetOnce is true before any reset");
	/* The script's own check, FUNC 20527: SystemService.IsResetOnce. */
	vm_call(SR_IS_RESET_ONCE, -1);
	SR_CHECK(stack_pop().i == 0, "the script's check says restarted before any reset");
	set(index, seven);
	set(type, one);
	int how = sr_reset_from(SR_LOAD);
	SR_CHECK(how == 1, "ResetLoadManager::Run did not restart the VM (%d)", how);
	sr_restart_vm();
	SR_CHECK(exist(index) && exist(type), "the load target is gone after the restart");
	struct string *v = get(index), *w = get(type);
	SR_CHECK(!strcmp(v->text, "7") && !strcmp(w->text, "1"), "the load target reads back as '%s', '%s'; want '7', '1'", v->text, w->text);
	free_string(v);
	free_string(w);
	SR_CHECK(once() && vm_reset_once, "IsResetOnce is false after the restart");
	unsigned long steps0 = steps[SR_IS_RESET_ONCE];
	vm_call(SR_IS_RESET_ONCE, -1);
	SR_CHECK(stack_pop().i == 1 && steps[SR_IS_RESET_ONCE] > steps0, "the script's check does not see the restart");
	free_string(index);
	free_string(type);
	free_string(seven);
	free_string(one);
	printf("  SR2 the load target and IsResetOnce cross the restart\n");
}

/* ---- SR3: twelve restarts ------------------------------------------------------ */

static void sr_twelve(void)
{
	sr_setup();
	for (int round = 0; round < 12; round++) {
		/* what a restart out of the middle of things can leave behind */
		vm_call_insn_limit = 5;
		vm_in_alloc_phase = true;
		cas_next_handle = 99;
		cas_timers[7].active = true;
		heap_set_defer_frees(true);
		int how = sr_reset_from(round % 2 ? SR_LOAD : SR_GAMEOVER);
		SR_CHECK(how == 1, "restart %d did not happen (%d)", round + 1, how);
		if (how != 1)
			break;
		bool state = sr_restart_vm();
		SR_CHECK(state, "the engine has no vm_reset_runtime_state");
		SR_CHECK(call_stack_ptr == 0 && stack_ptr == 0 && vm_call_depth == 0, "restart %d: call stack %d, stack %d, call depth %d",
			 round + 1, call_stack_ptr, stack_ptr, vm_call_depth);
		SR_CHECK(vm_execute_depth == 0, "restart %d: vm_execute_depth is %d", round + 1, vm_execute_depth);
		SR_CHECK(vm_call_insn_limit == 0 && !vm_in_alloc_phase, "restart %d: instruction limit %llu, alloc phase %d",
			 round + 1, vm_call_insn_limit, vm_in_alloc_phase);
		SR_CHECK(cas_next_handle == 1 && !cas_timers[7].active, "restart %d: the timer handles go on from %d", round + 1, cas_next_handle);
		SR_CHECK(!heap_set_defer_frees(false), "restart %d: frees are still deferred", round + 1);
	}
	/* The VM still runs. */
	unsigned long steps0 = steps[SR_IS_RESET_ONCE];
	vm_call(SR_IS_RESET_ONCE, -1);
	SR_CHECK(steps[SR_IS_RESET_ONCE] > steps0, "after twelve restarts the VM executes nothing");
	SR_CHECK(stack_ptr == 1 && stack_pop().i == 1, "after twelve restarts a call leaves %d slot(s)", stack_ptr);
	printf("  SR3 twelve restarts, the VM state is back each time\n");
}

/* ---- SR4: the old heap --------------------------------------------------------- */

/* About 2.4 MB that nothing refers to but the heap slots. */
static void sr_fill_heap(void)
{
	char text[300];
	memset(text, 'x', sizeof text);
	for (int i = 0; i < 5000; i++)
		heap_alloc_string(make_string(text, sizeof text));
	for (int i = 0; i < 1000; i++) {
		int slot = heap_alloc_slot(VM_PAGE);
		heap[slot].page = alloc_page(ARRAY_PAGE, AIN_ARRAY_INT, 200);
	}
}

static void sr_heap(void)
{
	sr_setup();
	size_t after[6];
	for (int round = 0; round < 6; round++) {
		sr_fill_heap();
		int how = sr_reset_from(SR_LOAD);
		SR_CHECK(how == 1, "restart %d did not happen (%d)", round + 1, how);
		if (how != 1)
			return;
		sr_restart_vm();
		SR_CHECK(live_slots() == 0, "restart %d: %zu slot(s) alive", round + 1, live_slots());
		after[round] = __sanitizer_get_current_allocated_bytes();
	}
	long grew = (long)after[5] - (long)after[1];
	SR_CHECK(grew < 256 * 1024, "four more restarts left %ld bytes allocated (the old heap is not freed)", grew);

	/* Before v14 the slots still alive are forgotten, as they always were. */
	sr_fill_heap();
	int how = sr_reset_from(SR_LOAD);
	SR_CHECK(how == 1, "the v13 restart did not happen (%d)", how);
	if (how != 1)
		return;
	size_t before = __sanitizer_get_current_allocated_bytes();
	ain->version = 13;
	sr_restart_vm();
	ain->version = 14;
	long kept = (long)__sanitizer_get_current_allocated_bytes() - (long)before;
	SR_CHECK(kept > -256 * 1024, "a v13 restart freed %ld bytes of the old heap", -kept);
	printf("  SR4 the old heap is freed for v14 (%ld bytes over four restarts) and left alone before it\n", grew);
}

/* ---- SR5: the parts engine ----------------------------------------------------- */

static void sr_parts(void)
{
	extern struct static_library lib_PartsEngine;
	extern void parts_enqueue_message_vars(int type, int parts_no, int delegate_index, int unique_id, int nr_vars, const int *vars);
	enum { BOX = 18701 };
	br_input_setup();
	br_box(BOX, 100, 100, 200, 80, true);
	SR_CHECK(PE_IsExist(BOX), "the parts was not made");
	int first = PE_GetFreeNumber();
	PE_GetFreeNumber();
	PE_AddMotionPos(BOX, 0, 0, 10, 10, 0, 1000);
	PE_BeginMotion();
	SR_CHECK(PE_IsMotion(), "no motion in progress");
	PE_BeginInput();
	PE_BeginInput();
	static const int vars[3] = { 1, 2, 3 };
	parts_enqueue_message_vars(4, BOX, 3, 30, 3, vars);
	parts_enqueue_message_vars(4, 0, -1, 0, 3, vars);
	SR_CHECK(br_msg_type() == 4, "no message queued");
	/* The script hid the message window; three activity parts got numbers. */
	extern bool parts_message_window_show;
	extern int probe_activity_parts_no(bool take);
	bool show = parts_message_window_show;
	parts_message_window_show = !show;
	int activity_first = probe_activity_parts_no(false);
	for (int i = 0; i < 3; i++)
		probe_activity_parts_no(true);

	library_run(&lib_PartsEngine, "_ModuleFini");

	SR_CHECK(parts_message_window_show == show, "the message window's show flag stays %d; a new engine starts with %d",
		 parts_message_window_show, show);
	SR_CHECK(probe_activity_parts_no(false) == activity_first, "the activity parts numbers go on from %d; a new engine starts at %d",
		 probe_activity_parts_no(false), activity_first);
	SR_CHECK(!PE_IsExist(BOX), "the parts is still there");
	SR_CHECK(br_msg_type() < 0, "a message of the old run is still queued (type %d, parts %d)", br_msg_type(), br_msg_parts());
	SR_CHECK(!PE_IsMotion(), "the motion of the old run is still in progress");
	int again = PE_GetFreeNumber();
	SR_CHECK(again == first, "the free numbers go on from %d; a new engine starts at %d", again, first);
	/* A new scope starts at depth one: its EndInput closes it. */
	extern bool parts_began_click;
	PE_BeginInput();
	PE_EndInput();
	SR_CHECK(!parts_began_click, "an input scope of the old run is still open");
	printf("  SR5 the parts engine keeps nothing of the old run\n");
}

/* ---- SR6: HashMap -------------------------------------------------------------- */

static void sr_hashmap(void)
{
	extern struct static_library lib_HashMap;
	int (*create)(void) = sr_fn(&lib_HashMap, "Create");
	assert(create);
	int n = 0;
	while (n < 1000 && create() >= 0)
		n++;
	SR_CHECK(n > 0 && n < 1000, "HashMap.Create handed out %d handles", n);
	library_run(&lib_HashMap, "_ModuleFini");
	int id = create();
	SR_CHECK(id == 0, "after the library stopped HashMap.Create returns %d, want 0", id);
	printf("  SR6 the %d HashMap handles are handed back\n", n);
}

/* ---- SR7: guards --------------------------------------------------------------- */

static void sr_guards(void)
{
	bool *pending = sr_pending();
	SR_CHECK(pending, "the engine has no vm_reset_pending");
	if (!pending)
		return;
	/* Through the ffi alone (no CALLHLL instruction) the call only asks. */
	int lib = sp_lib("system"), f = sp_libfn(lib, "Reset"), sp = stack_ptr;
	SR_CHECK(!*pending, "a restart is requested before anything ran");
	hll_call(lib, f, 0);
	SR_CHECK(stack_ptr == sp, "system.Reset left %d slot(s)", stack_ptr - sp);
	SR_CHECK(*pending, "system.Reset did not ask for the restart");
	*pending = false;
	/* An ordinary library call in bytecode does not restart anything. */
	static volatile int how;
	how = 0;
	if (setjmp(reset_buf)) {
		how = 1;
	} else {
		vm_call(SR_IS_RESET_ONCE, -1);
		how = 2;
	}
	SR_CHECK(how == 2 && stack_ptr == sp + 1 && stack_pop().i == 0 && !*pending,
		 "a call of SystemService.IsResetOnce restarted the VM or left the request up (%d)", how);
	printf("  SR7 the request is raised by the call and taken by the VM only\n");
}

/* ---- SR8: the main EX file ----------------------------------------------------- */

static void sr_ex_restart(void)
{
	extern struct static_library lib_MainEXFile;
	library_run(&lib_MainEXFile, "_ModuleFini");
	library_run(&lib_MainEXFile, "_ModuleInit");
}

static void sr_ex_file(void)
{
	extern struct static_library lib_MainEXFile;
	int (*row)(struct string *, int) = sr_fn(&lib_MainEXFile, "Row");
	int (*num)(struct string *, int, int) = sr_fn(&lib_MainEXFile, "Int");
	bool (*add)(struct string *) = sr_fn(&lib_MainEXFile, "AddEX");
	assert(row && num && add);
	const char *game = getenv("XS4_GAME");
	char ex_path[PATH_MAX];
	snprintf(ex_path, sizeof ex_path, "%s/dohnadohnaEx.ex", game ? game : ".");
	config.ex_path = strdup(ex_path);
	/* the file is read four times in here */
	alarm(600);
	library_run(&lib_MainEXFile, "_ModuleInit");
	size_t loaded = __sanitizer_get_current_allocated_bytes();
	for (int i = 0; i < 3; i++)
		sr_ex_restart();
	long grew = (long)__sanitizer_get_current_allocated_bytes() - (long)loaded;
	SR_CHECK(grew < 1024 * 1024, "three restarts of the library left %ld more bytes allocated (the file was read again)", grew);

	/* The tables answer after the restarts as the file does: its first
	 * table with rows and its first integer, from a second reading. */
	struct ex *disk = ex_read_file(ex_path);
	struct ex_block *table = NULL, *integer = NULL;
	for (unsigned i = 0; disk && i < disk->nr_blocks; i++) {
		struct ex_block *b = &disk->blocks[i];
		if (!table && b->val.type == EX_TABLE && b->val.t->nr_rows)
			table = b;
		if (!integer && b->val.type == EX_INT)
			integer = b;
	}
	SR_CHECK(table, "the EX file has no table with rows to ask for");
	if (!table)
		return;
	int rows = table->val.t->nr_rows;
	SR_CHECK(row(table->name, 0) == rows, "a table of %d rows has %d after the restarts", rows, row(table->name, 0));
	if (integer) {
		int want = integer->val.i, got = num(integer->name, want ^ 1, 0);
		SR_CHECK(got == want, "an integer of the file reads %d after the restarts, want %d", got, want);
	}

	/* AddEX appends to the main tables (here the file to itself: every
	 * table has its rows twice) and nothing takes that back, so the next
	 * restart reads the file again and the rows are the file's. */
	struct string *self = make_string(ex_path, strlen(ex_path));
	SR_CHECK(add(self), "AddEX did not take the file");
	SR_CHECK(row(table->name, 0) == 2 * rows, "AddEX left the table with %d rows, want %d", row(table->name, 0), 2 * rows);
	size_t appended = __sanitizer_get_current_allocated_bytes();
	sr_ex_restart();
	long reread = (long)__sanitizer_get_current_allocated_bytes() - (long)appended;
	SR_CHECK(row(table->name, 0) == rows, "a restart after AddEX leaves the table with %d rows, want %d (the appended copy was kept)",
		 row(table->name, 0), rows);
	SR_CHECK(reread > 1024 * 1024, "a restart after AddEX allocated %ld bytes: the file was not read again", reread);
	/* The new copy is kept in its turn. */
	size_t fresh = __sanitizer_get_current_allocated_bytes();
	sr_ex_restart();
	long kept = (long)__sanitizer_get_current_allocated_bytes() - (long)fresh;
	SR_CHECK(kept < 1024 * 1024, "the restart after that one read the file once more (%ld bytes)", kept);
	SR_CHECK(row(table->name, 0) == rows, "the table has %d rows after that restart, want %d", row(table->name, 0), rows);
	free_string(self);
	printf("  SR8 a restart keeps the main EX file (%ld bytes more after three; a table of %d rows%s checked) and reads it again after AddEX (%ld bytes)\n",
	       grew, rows, integer ? " and an integer" : "", reread);
}

/* ---- SR9: a short CG name ------------------------------------------------------ */

static void sr_short_cg_name(void)
{
	enum { BOX = 18711 };
	br_input_setup();
	br_box(BOX, 0, 0, 10, 10, false);
	struct string *name = make_string("ab", 2);
	bool ok = PE_SetPartsCG(BOX, name, 0, 1);
	SR_CHECK(!ok, "a CG that does not exist was set");
	free_string(name);
	printf("  SR9 a CG name shorter than the <save> prefix is compared safely\n");
}

/* ---- SR10: the backlog ---------------------------------------------------------- */

static void sr_backlog(void)
{
	extern struct static_library lib_AnteaterADVEngine;
	void (*add)(struct string *, struct string *) = sr_fn(&lib_AnteaterADVEngine, "AddText");
	void (*page)(void) = sr_fn(&lib_AnteaterADVEngine, "AddNewPage");
	int (*count)(void) = sr_fn(&lib_AnteaterADVEngine, "GetNumofADVLog");
	void (*enable)(bool) = sr_fn(&lib_AnteaterADVEngine, "SetEnable");
	bool (*enabled)(void) = sr_fn(&lib_AnteaterADVEngine, "IsEnable");
	assert(add && page && count && enable && enabled);
	br_input_setup();
	SR_CHECK(count() == 0 && enabled(), "the backlog starts with %d page(s), enabled %d", count(), enabled());
	struct string *line = make_string("a line", 6), *window = make_string("", 0);
	for (int i = 0; i < 3; i++) {
		add(line, window);
		page();
	}
	SR_CHECK(count() == 3, "three pages were written and the backlog has %d", count());
	enable(false);

	int how = sr_reset_with_libraries();
	SR_CHECK(how == 1, "the VM did not restart (%d)", how);
	SR_CHECK(count() == 0, "the backlog still has %d page(s) of the old run", count());
	SR_CHECK(enabled(), "the backlog is still switched off");
	/* The new run writes its own first page. */
	add(line, window);
	page();
	SR_CHECK(count() == 1, "a page written after the restart makes %d", count());
	free_string(line);
	free_string(window);
	printf("  SR10 the backlog of the old run is gone\n");
}

/* ---- SR11: sound ---------------------------------------------------------------- */

/* A second of silence: 8-bit mono PCM at 8000 Hz. */
static bool sr_write_wav(const char *path)
{
	static const uint8_t head[44] = {
		'R', 'I', 'F', 'F', 0x64, 0x1f, 0, 0, 'W', 'A', 'V', 'E', 'f', 'm', 't', ' ', 16, 0, 0, 0, 1, 0, 1, 0,
		0x40, 0x1f, 0, 0, 0x40, 0x1f, 0, 0, 1, 0, 8, 0, 'd', 'a', 't', 'a', 0x40, 0x1f, 0, 0 };
	uint8_t data[8000];
	memset(data, 0x80, sizeof data);
	FILE *f = fopen(path, "wb");
	if (!f)
		return false;
	bool ok = fwrite(head, sizeof head, 1, f) == 1 && fwrite(data, sizeof data, 1, f) == 1;
	return !fclose(f) && ok;
}

static void sr_sound(void)
{
	extern struct static_library lib_KiwiSoundEngine;
	int (*free_id)(int, int) = sr_fn(&lib_KiwiSoundEngine, "GetFreeSeID");
	bool (*exist)(int) = sr_fn(&lib_KiwiSoundEngine, "IsExistID");
	bool (*prepare)(int, struct string *) = sr_fn(&lib_KiwiSoundEngine, "Sound_PrepareFromFile");
	bool (*play)(int) = sr_fn(&lib_KiwiSoundEngine, "Play");
	bool (*loop)(int, int) = sr_fn(&lib_KiwiSoundEngine, "SetLoopCount");
	bool (*fade)(int, int, float, bool, int) = sr_fn(&lib_KiwiSoundEngine, "Fade");
	bool (*fading)(int) = sr_fn(&lib_KiwiSoundEngine, "IsFade");
	assert(free_id && exist && prepare && play && loop && fade && fading);
	const char *tmp = getenv("TMPDIR");
	char path[PATH_MAX];
	snprintf(path, sizeof path, "%s/xs4-system-reset-%d.wav", tmp && *tmp ? tmp : "/tmp", (int)getpid());
	SR_CHECK(sr_write_wav(path), "cannot write the sound file");
	SDL_setenv("SDL_AUDIODRIVER", "dummy", 1);
	library_run(&lib_KiwiSoundEngine, "_ModuleInit");
	br_input_setup();
	struct string *file = make_string(path, strlen(path));
	/* A sound that loops for ever and one in a long fade, as a scene
	 * leaves them when the script resets from its menu. */
	int first = free_id(0, 0), a = first;
	SR_CHECK(prepare(a, file) && loop(a, 0) && play(a), "cannot play the looping sound");
	int b = free_id(0, 0);
	SR_CHECK(b != a, "the ID of a prepared sound is handed out again");
	SR_CHECK(prepare(b, file) && loop(b, 0) && play(b) && fade(b, 60000, 0.5f, false, 0), "cannot play the fading sound");
	SR_CHECK(exist(a) && exist(b) && fading(b), "the two sounds are not playing (%d, %d) or not fading (%d)",
		 exist(a), exist(b), fading(b));

	int how = sr_reset_with_libraries();
	SR_CHECK(how == 1, "the VM did not restart (%d)", how);
	SR_CHECK(!exist(a) && !exist(b), "a sound of the old run is still playing (%d, %d)", exist(a), exist(b));
	SR_CHECK(!loop(a, 0) && !loop(b, 0), "a channel of the old run is still prepared");
	SR_CHECK(!fading(b), "a fade of the old run is still running");
	SR_CHECK(free_id(0, 0) == first, "the free sound IDs go on from %d; a new engine starts at %d", free_id(0, 0), first);
	unlink(path);
	free_string(file);
	printf("  SR11 the restart leaves no sound channel of the old run\n");
}

/* ---- SR12: the restart runs the libraries' _ModuleFini --------------------------- */

static void sr_library_fini(void)
{
	extern struct static_library lib_HashMap;
	int (*create)(void) = sr_fn(&lib_HashMap, "Create");
	assert(create);
	enum { BOX = 18721 };
	br_input_setup();
	br_box(BOX, 100, 100, 200, 80, true);
	SR_CHECK(PE_IsExist(BOX), "the parts was not made");
	int n = 0;
	while (n < 1000 && create() >= 0)
		n++;
	SR_CHECK(n > 0 && n < 1000, "HashMap.Create handed out %d handles", n);

	int how = sr_reset_with_libraries();
	SR_CHECK(how == 1, "the VM did not restart (%d)", how);
	SR_CHECK(!PE_IsExist(BOX), "the restart did not stop the parts engine: the parts is still there");
	int id = create();
	SR_CHECK(id == 0, "the restart did not stop HashMap: Create returns %d, want 0", id);
	printf("  SR12 the restart runs the libraries' _ModuleFini\n");
}

static int test_system_reset(void)
{
	void (*cases[])(void) = { sr_call_sites, sr_handover, sr_twelve, sr_heap, sr_parts, sr_hashmap, sr_guards, sr_ex_file,
				  sr_short_cg_name, sr_backlog, sr_sound, sr_library_fini };
	const char *names[] = { "SR1", "SR2", "SR3", "SR4", "SR5", "SR6", "SR7", "SR8", "SR9", "SR10", "SR11", "SR12" };
	int failed = 0;
	for (unsigned i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
		fflush(stdout);
		pid_t pid = fork();
		if (pid == 0) {
			alarm(120);
			sr_failures = 0;
			cases[i]();
			fflush(stdout);
			_exit(sr_failures ? 1 : 0);
		}
		int st = 0;
		bool ok = pid > 0 && waitpid(pid, &st, 0) == pid && WIFEXITED(st) && WEXITSTATUS(st) == 0;
		printf("%s %s\n", names[i], ok ? "ok" : "FAIL");
		failed += !ok;
	}
	printf("SYSTEM-RESET %s: %d of %zu cases failed\n", failed ? "FAIL" : "PASS", failed, sizeof(cases) / sizeof(cases[0]));
	return failed ? 1 : 0;
}
