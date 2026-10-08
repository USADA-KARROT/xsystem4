/* global-init: the alloc function "0" constructs every struct global itself,
 * and nothing constructs a global again before main.
 *
 * Original (static disassembly). NEW (opcode 0x76, dispatch 0x667ef0 ->
 * 0x66806d -> 0x66bb90) reads the struct index and the constructor operand.
 * A negative operand (0x66bbed) is 0x679b30(type, 1): allocate (0x679db0:
 * the page, members of type string/struct/delegate/array at -1 (0x656b82),
 * the others per 0x656970) and, when the struct table entry has a
 * constructor (entry +0x24 != -1), run it on the new object at once through
 * RunMethod 0x669cb0 (0x679be8); a struct without one gets its members
 * default-initialized and its <vtable> filled instead (0x679c2c..0x679d05).
 * A non-negative operand (0x66bc7f) is 0x679db0(type) followed by that one
 * constructor with the arguments the bytecode pushed (0x66a0e0 at 0x66bd37).
 * Those are the only ways a constructor is reached from C++ besides a
 * delegate Invoke (0x65aca0 -> 0x692bc0 -> 0x669cb0) and CALLMETHOD
 * (0x66d36c); the engine does not walk the global page after the alloc
 * function. So after "0" returns, a global built with NEW x, ctor holds what
 * that constructor made, and one filled from a function result holds what
 * the function returned.
 *
 * This VM used to leave NEW x, -1 unconstructed while the alloc function ran
 * and afterwards run the no-argument constructor of every struct global
 * (init_global_struct_v14, two passes), which reset the 30 globals the alloc
 * function had built with NEW x, ctor (BattleBackground::Size became (0, 0),
 * CameraCenter and ScreenCenter too; the 18 CASColor constants went black)
 * and the three it had filled from DamageNumber::CreateFont and
 * Motion::GetCompiled.
 *
 * GI1-GI6 and G3 run the real startup first (G1 and G2 are guards on a single
 * NEW): vm_init_globals (the initvals, the alloc function and the v14 repairs;
 * dlsym, so this file builds on an engine without it, where the older sequence
 * - the initvals, the alloc function, the two constructor passes and -1 to 0,
 * not the repairs - is reproduced with that engine's own
 * init_global_struct_v14). The alloc function's bytecode is scanned for the
 * globals it initializes and how (NEW x, -1; NEW x, ctor with literal
 * arguments; a CALLFUNC result), so the expectations come from the AIN, not
 * from this file. A step hook logs every constructor entry (function, this,
 * whether "0" is on the call stack) and a return hook snapshots the global
 * page the moment "0" returns.
 *
 *   GI1  BattleBackground::Size is (2560, 1024), BaseY 921, CameraCenter
 *        (1280, 701), PlayerSpan 100, ScreenCenter (640, 360): the values
 *        the alloc function computes (CASSize(2560, 1024), floor(Height *
 *        0.9), CASPosF(Width / 2, BaseY - 220), CASPosF(640, 360)).
 *   GI2  Every global the alloc function builds with NEW x, ctor and literal
 *        arguments holds those arguments (18 CASColor (r, g, b, a), 5
 *        CASVector3D (x, y, z), CASSize, the literal CASPosF, CycleTimer's
 *        cycle); the ones built with an explicit no-argument constructor
 *        (CustomerCreatorParams, SpecialCustomerCreatorParam,
 *        WorkerCreatorParams) were entered by it once, inside "0"; and no
 *        such global's STRT no-argument constructor ran on it afterwards.
 *   GI3  The globals filled from a function result keep it: the CASFont from
 *        DamageNumber::CreateFont has Type 257 and Size 56, each
 *        Motion::MotionSet from Motion::GetCompiled has a non-empty section
 *        array and its parameter object; their no-argument constructors did
 *        not run on them.
 *   GI4  Every NEW x, -1 global exists with the right struct, and its STRT
 *        constructor was entered on it exactly once, inside "0" (CDebug
 *        structs: never, as before).
 *   GI5  Nested: the value-struct members of those globals that their
 *        constructors build are live objects of the declared struct, each
 *        constructed at most once; CASFont's two CASColor members are live.
 *   GI6  The global page the moment "0" returns is the global page main
 *        sees: the same slot in every global and the same first-level
 *        member values in every struct global.
 *   G1   Guard, v13: with ain->version 13 and the alloc phase on, the real
 *        NEW x, -1 constructs through create_struct/init_struct only
 *        (constructor entered once, not resolved a second time).
 *   G2   Guard: NEW x, -1 of a struct without a constructor, in the alloc
 *        phase, allocates and runs nothing.
 *   G3   Guard: the CASTimerManager global's four arrays have the types and
 *        sizes the metadata mode pins (its constructor ran once).
 *
 * Every case runs in a child process. Exit code 1 is a failed check, exit
 * code 2 the environment (the AIN lacks a function, struct, member or
 * instruction a case looks up). A signal, a sanitizer abort, the alarm or
 * the instruction budget running out is that case failing.
 */
#include <dlfcn.h>
#include <sys/wait.h>
#include <unistd.h>

static int gi_failures;
#define GI_CHECK(c, ...) do { if (!(c)) { printf("  FAIL: "); printf(__VA_ARGS__); printf("\n"); fflush(stdout); gi_failures++; } } while (0)

/* --- lookups --- */

static int gi_global(const char *name)
{
	for (int i = 0; i < ain->nr_globals; i++)
		if (ain->globals[i].name && !strcmp(ain->globals[i].name, name))
			return i;
	printf("GLOBAL-INIT global %s not declared\n", name);
	exit(2);
}

static int gi_struct(const char *name)
{
	for (int i = 0; i < ain->nr_structures; i++)
		if (ain->structures[i].name && !strcmp(ain->structures[i].name, name))
			return i;
	printf("GLOBAL-INIT struct %s not declared\n", name);
	exit(2);
}

static int gi_member(int st, const char *name)
{
	struct ain_struct *s = &ain->structures[st];
	for (int i = 0; i < s->nr_members; i++)
		if (s->members[i].name && !strcmp(s->members[i].name, name))
			return i;
	printf("GLOBAL-INIT struct %s has no member %s\n", s->name, name);
	exit(2);
}

static int gi_fn(const char *name)
{
	for (int i = 0; i < ain->nr_functions; i++)
		if (ain->functions[i].name && !strcmp(ain->functions[i].name, name))
			return i;
	printf("GLOBAL-INIT function %s not declared\n", name);
	exit(2);
}

static const char *gi_gname(int i)
{
	return i >= 0 && i < ain->nr_globals && ain->globals[i].name ? ain->globals[i].name : "?";
}

static const char *gi_sname(int st)
{
	return st >= 0 && st < ain->nr_structures && ain->structures[st].name ? ain->structures[st].name : "?";
}

static int gi_gslot(int i)
{
	return heap[global_page_slot].page->values[i].i;
}

/* A live struct page of this struct. */
static bool gi_live_struct(int slot, int st)
{
	return da_live_page(slot) && heap[slot].page->type == STRUCT_PAGE && heap[slot].page->index == st;
}

static bool gi_is_cdebug(int st)
{
	return st >= 0 && st < ain->nr_structures && (struct_flags[st] & STRUCT_FLAG_CDEBUG);
}

/* --- the alloc function's bytecode --- */

enum { GI_NEW_DEFAULT, GI_NEW_CTOR, GI_CALLFUNC };
struct gi_init {
	int global;		/* the global it is stored into */
	int kind;
	int struct_type;	/* NEW */
	int ctor;		/* NEW: the operand */
	int fno;		/* CALLFUNC */
	int nargs;		/* NEW x, ctor: literals pushed right before it, -1 when not all literal */
	int args[8];
	uint32_t at;		/* the NEW / CALLFUNC instruction */
};
static struct gi_init gi_inits[512];
static int gi_ninits;
static uint32_t gi_first_new_default;	/* the first NEW x, -1 in "0" */

/* Walk "0" and record what it stores into which global: after PUSHGLOBALPAGE;
 * PUSH i the next NEW or CALLFUNC, up to the X_ASSIGN that stores it. The
 * arguments of NEW x, ctor are taken only when the instructions right before
 * it are that many PUSHes (CameraCenter's are computed and skipped). */
static void gi_scan_alloc(void)
{
	if (ain->alloc < 0 || ain->alloc >= ain->nr_functions) {
		printf("GLOBAL-INIT the AIN has no alloc function\n");
		exit(2);
	}
	gi_ninits = 0;
	gi_first_new_default = 0;
	int target = -1;	/* the global PUSHGLOBALPAGE; PUSH i names as a store target */
	int cand = -1;		/* PUSHGLOBALPAGE; PUSH i just seen: a read if X_REF follows */
	bool after_gpage = false;
	int pushes[8], npush = 0;	/* consecutive PUSH immediates */
	bool pending = false;
	struct gi_init cur;
	for (size_t addr = ain->functions[ain->alloc].address; addr + 2 <= ain->code_size;) {
		int16_t op = get_opcode(addr);
		if (op < 0 || op >= NR_OPCODES) {
			printf("GLOBAL-INIT bad opcode %d at 0x%zx in the alloc function\n", op, addr);
			exit(2);
		}
		int32_t a0 = instructions[op].nr_args > 0 ? LittleEndian_getDW(ain->code, addr + 2) : 0;
		int32_t a1 = instructions[op].nr_args > 1 ? LittleEndian_getDW(ain->code, addr + 6) : 0;
		if (op == ENDFUNC)
			break;
		if (cand >= 0) {
			if (op != X_REF)
				target = cand;	/* a store (X_DUP 2; X_REF 1; DELETE ... or PUSH args) */
			cand = -1;
		}
		if (op == PUSHGLOBALPAGE) {
			after_gpage = true;
		} else if (op == PUSH) {
			if (after_gpage)
				cand = a0;
			if (npush < 8)
				pushes[npush++] = a0;
			after_gpage = false;
		} else {
			after_gpage = false;
		}
		if (op == NEW) {
			memset(&cur, 0, sizeof cur);
			cur.global = target;
			cur.struct_type = a0;
			cur.ctor = a1;
			cur.at = (uint32_t)addr;
			if (a1 < 0) {
				cur.kind = GI_NEW_DEFAULT;
				if (!gi_first_new_default)
					gi_first_new_default = (uint32_t)addr;
			} else {
				cur.kind = GI_NEW_CTOR;
				int n = a1 < ain->nr_functions ? ain->functions[a1].nr_args : -1;
				/* PUSH i (the target) is one of the pushes when it was the last one */
				int lit = npush - (target >= 0 && npush > 0 && pushes[0] == target && npush == n + 1 ? 1 : 0);
				if (n >= 0 && n <= 8 && lit >= n) {
					cur.nargs = n;
					for (int k = 0; k < n; k++)
						cur.args[k] = pushes[npush - n + k];
				} else {
					cur.nargs = -1;
				}
			}
			pending = true;
		} else if (op == CALLFUNC) {
			memset(&cur, 0, sizeof cur);
			cur.global = target;
			cur.kind = GI_CALLFUNC;
			cur.fno = a0;
			cur.at = (uint32_t)addr;
			pending = true;
		} else if (op == X_ASSIGN) {
			if (pending && cur.global >= 0 && cur.global < ain->nr_globals && gi_ninits < 512)
				gi_inits[gi_ninits++] = cur;
			pending = false;
			target = -1;
		}
		if (op != PUSH)
			npush = 0;
		addr += instruction_width(op);
	}
	if (!gi_ninits) {
		printf("GLOBAL-INIT no global initialization found in the alloc function\n");
		exit(2);
	}
}

/* --- constructor entries --- */

struct gi_ev { int fno; int page; bool in_alloc; };
static struct gi_ev *gi_evs;
static int gi_nevs, gi_cap;
static bool gi_watch;
static bool *gi_is_ctor;

static void gi_ctor_table(void)
{
	if (gi_is_ctor)
		return;
	gi_is_ctor = xcalloc(ain->nr_functions, sizeof(bool));
	for (int i = 0; i < ain->nr_structures; i++) {
		int c = ain->structures[i].constructor;
		if (c > 0 && c < ain->nr_functions)
			gi_is_ctor[c] = true;
	}
	/* explicit constructors named by NEW (CASColor@0#1 etc.) */
	for (int i = 0; i < ain->nr_functions; i++) {
		const char *n = ain->functions[i].name;
		if (n && strstr(n, "@0"))
			gi_is_ctor[i] = true;
	}
}

static void global_init_probe_step(void)
{
	if (!gi_watch || !call_stack_ptr)
		return;
	struct function_call *f = &call_stack[call_stack_ptr - 1];
	if (f->fno < 0 || f->fno >= ain->nr_functions || !gi_is_ctor[f->fno])
		return;
	if (instr_ptr != ain->functions[f->fno].address)
		return;
	bool in_alloc = false;
	for (int k = 0; k < call_stack_ptr; k++)
		if (call_stack[k].fno == ain->alloc)
			in_alloc = true;
	if (gi_nevs == gi_cap) {
		gi_cap = gi_cap ? gi_cap * 2 : 4096;
		gi_evs = xrealloc(gi_evs, gi_cap * sizeof(*gi_evs));
	}
	gi_evs[gi_nevs].fno = f->fno;
	gi_evs[gi_nevs].page = f->struct_page;
	gi_evs[gi_nevs].in_alloc = in_alloc;
	gi_nevs++;
}

/* How often fno was entered with this object: inside "0" and outside. */
static void gi_entries(int fno, int page, int *inside, int *outside)
{
	*inside = *outside = 0;
	for (int i = 0; i < gi_nevs; i++) {
		if (gi_evs[i].fno != fno || gi_evs[i].page != page)
			continue;
		if (gi_evs[i].in_alloc)
			(*inside)++;
		else
			(*outside)++;
	}
}

/* --- the global page when "0" returns --- */

static union vm_value *gi_snap_globals;
static int gi_snap_nglobals;
struct gi_snap_struct { int slot; int index; int nr_vars; union vm_value *values; };
static struct gi_snap_struct *gi_snap_structs;
static int gi_snap_nstructs;
static bool gi_snap_taken;

static void global_init_probe_return(int fno)
{
	if (!gi_watch || gi_snap_taken || fno != ain->alloc)
		return;
	gi_snap_taken = true;
	struct page *g = heap[global_page_slot].page;
	gi_snap_nglobals = g->nr_vars;
	gi_snap_globals = xmalloc(gi_snap_nglobals * sizeof(union vm_value));
	memcpy(gi_snap_globals, g->values, gi_snap_nglobals * sizeof(union vm_value));
	gi_snap_structs = xcalloc(ain->nr_globals, sizeof(*gi_snap_structs));
	gi_snap_nstructs = 0;
	for (int i = 0; i < ain->nr_globals && i < g->nr_vars; i++) {
		if (ain->globals[i].type.data != AIN_STRUCT)
			continue;
		int slot = g->values[i].i;
		if (!da_live_page(slot))
			continue;
		struct gi_snap_struct *s = &gi_snap_structs[gi_snap_nstructs++];
		s->slot = slot;
		s->index = heap[slot].page->index;
		s->nr_vars = heap[slot].page->nr_vars;
		s->values = xmalloc((s->nr_vars > 0 ? s->nr_vars : 1) * sizeof(union vm_value));
		memcpy(s->values, heap[slot].page->values, s->nr_vars * sizeof(union vm_value));
	}
}

/* --- the real startup --- */

static bool gi_started;

/* The alloc function asks IbisInputEngine.Key_IsDown about other keys than
 * the click fixture's stand-in accepts: no key is down here. The library is
 * linked already, so the linked table (ffi.c, included) is patched. */
static int gi_key_is_down(int key)
{
	(void)key;
	return 0;
}

static void gi_override_hll(const char *lib, const char *name, void *fun)
{
	for (int i = 0; i < ain->nr_libraries; i++) {
		if (strcmp(ain->libraries[i].name, lib) || !libraries[i])
			continue;
		for (int j = 0; j < ain->libraries[i].nr_functions; j++)
			if (!strcmp(ain->libraries[i].functions[j].name, name))
				libraries[i][j].fun = fun;
	}
}

/* vm_execute_ain's global initialization: the engine's own vm_init_globals,
 * or on an engine without it (before this change) the sequence that engine
 * ran: initvals, the alloc function in the alloc phase, init_global_struct_v14
 * in two passes, the -1 -> 0 pass. */
static void gi_startup(void)
{
	if (gi_started)
		return;
	gi_started = true;
	gi_ctor_table();
	gi_scan_alloc();
	da_parts_init();	/* the alloc function reaches PartsEngine (parts table, _PostLink) */
	gi_override_hll("IbisInputEngine", "Key_IsDown", gi_key_is_down);
	/* WorkerCreatorParams@0 and others read the EX file (MainEXFile) */
	{
		const char *game = getenv("XS4_GAME");
		char path[PATH_MAX];
		snprintf(path, sizeof path, "%s/dohnadohnaEx.ex", game ? game : ".");
		struct ex *e = game ? ex_read_file(path) : NULL;
		if (!e) {
			printf("GLOBAL-INIT no EX file in XS4_GAME\n");
			exit(2);
		}
		probe_mainex_set(e);
	}
	probe_step_budget = 400000000UL;
	gi_watch = true;
	void (*init)(void) = (void (*)(void))dlsym(RTLD_DEFAULT, "vm_init_globals");
	if (init) {
		init();
	} else {
		void (*igs)(int, int) = (void (*)(int, int))dlsym(RTLD_DEFAULT, "init_global_struct_v14");
		if (!igs) {
			printf("GLOBAL-INIT neither vm_init_globals nor init_global_struct_v14 in this engine\n");
			exit(2);
		}
		for (int i = 0; i < ain->nr_initvals; i++) {
			struct ain_initval *v = &ain->global_initvals[i];
			if (v->data_type == AIN_STRING) {
				int index = heap_alloc_slot(VM_STRING);
				heap[global_page_slot].page->values[v->global_index].i = index;
				heap[index].s = make_string(v->string_value, strlen(v->string_value));
			} else {
				heap[global_page_slot].page->values[v->global_index].i = v->int_value;
			}
		}
		vm_in_alloc_phase = true;
		vm_call(ain->alloc, -1);
		vm_in_alloc_phase = false;
		bool *ginited = xcalloc(ain->nr_globals, sizeof(bool));
		for (int i = 0; i < ain->nr_globals; i++) {
			if (ain->globals[i].type.data != AIN_STRUCT)
				continue;
			int slot = gi_gslot(i);
			if (slot <= 0 || !heap_index_valid(slot))
				continue;
			int st = ain->globals[i].type.struc;
			if (st < 0 || st >= ain->nr_structures)
				continue;
			const char *sn = ain->structures[st].name;
			if (sn && (strstr(sn, "Manager") || strstr(sn, "Collection") || strstr(sn, "Map<") || strstr(sn, "IdArray"))) {
				igs(st, slot);
				ginited[i] = true;
			}
		}
		for (int i = ain->nr_globals - 1; i >= 0; i--) {
			if (ginited[i] || ain->globals[i].type.data != AIN_STRUCT)
				continue;
			int slot = gi_gslot(i);
			if (slot > 0 && heap_index_valid(slot))
				igs(ain->globals[i].type.struc, slot);
		}
		free(ginited);
		for (int i = 0; i < ain->nr_globals; i++) {
			enum ain_data_type t = ain->globals[i].type.data;
			if ((t == AIN_INT || t == AIN_BOOL || t == AIN_LONG_INT || t == AIN_FLOAT || t == AIN_ENUM || t == AIN_ENUM2)
			    && gi_gslot(i) == -1)
				heap[global_page_slot].page->values[i].i = 0;
		}
	}
	gi_watch = false;
	GI_CHECK(gi_snap_taken, "the alloc function did not return through function_return");
	printf("  startup: %s, %d constructor entries logged, %d initializations scanned in \"0\" (fno %d)\n",
	       init ? "vm_init_globals" : "pre-change sequence", gi_nevs, gi_ninits, ain->alloc);
}

/* --- value readers --- */

static int gi_int_member(int slot, int m)
{
	return da_live_page(slot) && m >= 0 && m < heap[slot].page->nr_vars ? heap[slot].page->values[m].i : INT_MIN;
}

static float gi_float_member(int slot, int m)
{
	union vm_value v = { .i = gi_int_member(slot, m) };
	return v.f;
}

/* --- cases --- */

static void gi_battle_background(void)
{
	gi_startup();
	int st_size = gi_struct("CASSize"), st_posf = gi_struct("CASPosF");
	int gsize = gi_global("BattleBackground::Size"), gbase = gi_global("BattleBackground::BaseY");
	int gcam = gi_global("BattleBackground::CameraCenter"), gspan = gi_global("BattleBackground::PlayerSpan");
	int gscr = gi_global("BattleBackground::ScreenCenter");
	int x = gi_member(st_posf, "X"), y = gi_member(st_posf, "Y");
	int size = gi_gslot(gsize), cam = gi_gslot(gcam), scr = gi_gslot(gscr);
	GI_CHECK(gi_live_struct(size, st_size), "Size: global %d is slot %d, not a live CASSize", gsize, size);
	GI_CHECK(gi_live_struct(cam, st_posf), "CameraCenter: slot %d is not a live CASPosF", cam);
	GI_CHECK(gi_live_struct(scr, st_posf), "ScreenCenter: slot %d is not a live CASPosF", scr);
	if (gi_failures)
		return;
	/* CASSize@Width::set / Height::set store into members 0 and 1 */
	int w = gi_int_member(size, 0), h = gi_int_member(size, 1);
	int base = gi_gslot(gbase), span = gi_gslot(gspan);
	float cx = gi_float_member(cam, x), cy = gi_float_member(cam, y);
	float sx = gi_float_member(scr, x), sy = gi_float_member(scr, y);
	printf("  Size=(%d, %d) BaseY=%d CameraCenter=(%g, %g) PlayerSpan=%d ScreenCenter=(%g, %g)\n",
	       w, h, base, cx, cy, span, sx, sy);
	GI_CHECK(w == 2560 && h == 1024, "Size is (%d, %d), want (2560, 1024)", w, h);
	GI_CHECK(base == 921, "BaseY is %d, want 921", base);
	GI_CHECK(cx == 1280.0f && cy == 701.0f, "CameraCenter is (%g, %g), want (1280, 701)", cx, cy);
	GI_CHECK(span == 100, "PlayerSpan is %d, want 100", span);
	GI_CHECK(sx == 640.0f && sy == 360.0f, "ScreenCenter is (%g, %g), want (640, 360)", sx, sy);
}

/* The members the explicit constructors with literal arguments store them
 * in, in argument order: CASColor@0#1(r,g,b,a) -> 0..3, CASVector3D@0#1 ->
 * 0..2, CASSize@0#1 -> Width::set/Height::set (members 0, 1), CASPosF@0#1 ->
 * X, Y, CycleTimer@0#1(cycle) -> member 1 (its @0 and @0#1 both X_ASSIGN it). */
static int gi_arg_member(int st, int k)
{
	const char *n = gi_sname(st);
	if (!strcmp(n, "CASColor") || !strcmp(n, "CASVector3D") || !strcmp(n, "CASSize") || !strcmp(n, "CASPosF"))
		return k;
	if (!strcmp(n, "CycleTimer"))
		return k == 0 ? 1 : -1;
	return -1;
}

static void gi_explicit_ctors(void)
{
	gi_startup();
	int seen = 0, literal = 0, noarg = 0;
	for (int i = 0; i < gi_ninits; i++) {
		struct gi_init *in = &gi_inits[i];
		if (in->kind != GI_NEW_CTOR)
			continue;
		seen++;
		int slot = gi_gslot(in->global);
		const char *gname = gi_gname(in->global), *sname = gi_sname(in->struct_type);
		GI_CHECK(gi_live_struct(slot, in->struct_type), "%s: slot %d is not a live %s", gname, slot, sname);
		if (!gi_live_struct(slot, in->struct_type))
			continue;
		int inside, outside;
		gi_entries(in->ctor, slot, &inside, &outside);
		GI_CHECK(inside == 1 && outside == 0, "%s: %s entered %d time(s) inside \"0\" and %d outside, want 1 and 0",
			 gname, ain->functions[in->ctor].name, inside, outside);
		int strt = ain->structures[in->struct_type].constructor;
		if (strt > 0 && strt != in->ctor) {
			gi_entries(strt, slot, &inside, &outside);
			GI_CHECK(inside == 0 && outside == 0, "%s: the STRT constructor %s ran on it %d time(s) (%d outside \"0\")",
				 gname, ain->functions[strt].name, inside + outside, outside);
		}
		if (in->nargs > 0) {
			literal++;
			printf("  %s = %s(", gname, ain->functions[in->ctor].name);
			bool ok = true;
			for (int k = 0; k < in->nargs; k++) {
				int m = gi_arg_member(in->struct_type, k);
				int have = m >= 0 ? gi_int_member(slot, m) : INT_MIN;
				bool is_float = ain->functions[in->ctor].vars[k].type.data == AIN_FLOAT;
				union vm_value w = { .i = in->args[k] }, hv = { .i = have };
				if (is_float)
					printf("%s%g", k ? ", " : "", w.f);
				else
					printf("%s%d", k ? ", " : "", in->args[k]);
				if (m < 0) {
					printf("[?]");
					continue;
				}
				if (have != in->args[k]) {
					ok = false;
					if (is_float)
						printf("[has %g]", hv.f);
					else
						printf("[has %d]", have);
				}
			}
			printf(")%s\n", ok ? "" : "  <- MISMATCH");
			GI_CHECK(ok, "%s does not hold the arguments the alloc function passed", gname);
		} else if (in->nargs == 0) {
			noarg++;
			printf("  %s = %s() once inside \"0\"\n", gname, ain->functions[in->ctor].name);
		} else {
			printf("  %s = %s(computed arguments, checked by GI1)\n", gname, ain->functions[in->ctor].name);
		}
	}
	printf("  %d NEW x, ctor globals: %d with literal arguments, %d with none\n", seen, literal, noarg);
	GI_CHECK(seen >= 30 && literal >= 26 && noarg >= 3, "expected the 30 NEW x, ctor globals of this AIN (26 literal, 3 no-arg)");
}

static void gi_function_results(void)
{
	gi_startup();
	int seen = 0;
	for (int i = 0; i < gi_ninits; i++) {
		struct gi_init *in = &gi_inits[i];
		if (in->kind != GI_CALLFUNC)
			continue;
		seen++;
		const char *fname = ain->functions[in->fno].name, *gname = gi_gname(in->global);
		int st = ain->globals[in->global].type.struc;
		int slot = gi_gslot(in->global);
		GI_CHECK(gi_live_struct(slot, st), "%s = %s(): slot %d is not a live %s", gname, fname, slot, gi_sname(st));
		if (!gi_live_struct(slot, st))
			continue;
		int strt = ain->structures[st].constructor, inside, outside;
		if (strt > 0) {
			gi_entries(strt, slot, &inside, &outside);
			GI_CHECK(outside == 0, "%s: %s ran on the returned object %d time(s) after \"0\"", gname,
				 ain->functions[strt].name, outside);
		}
		if (strstr(fname, "CreateFont")) {
			int type = gi_int_member(slot, gi_member(st, "Type")), size = gi_int_member(slot, gi_member(st, "Size"));
			printf("  %s = %s(): CASFont Type=%d Size=%d\n", gname, fname, type, size);
			GI_CHECK(type == 257 && size == 56, "%s: CASFont is Type %d Size %d, want 257 and 56 (CASFont@0 resets it to 0, 16)",
				 gname, type, size);
		} else if (strstr(fname, "GetCompiled")) {
			int sections = gi_int_member(slot, 0), param = gi_int_member(slot, 1);
			int n = da_live_page(sections) && heap[sections].page->type == ARRAY_PAGE ? wc_size(sections) : -1;
			printf("  %s = %s(): MotionSet sections=%d (array slot %d) param slot %d\n", gname, fname, n, sections, param);
			GI_CHECK(n > 0, "%s: the compiled MotionSet has %d section(s), want some (MotionSet@0 empties it)", gname, n);
			GI_CHECK(da_live_page(param) && heap[param].page->type == STRUCT_PAGE, "%s: no parameter object", gname);
		} else {
			printf("  %s = %s(): live %s (unchecked)\n", gname, fname, gi_sname(st));
		}
	}
	printf("  %d globals filled from function results\n", seen);
	GI_CHECK(seen >= 3, "expected DamageNumber::CreateFont and two Motion::GetCompiled results");
}

static void gi_default_ctors(void)
{
	gi_startup();
	int seen = 0, constructed = 0, cdebug = 0;
	for (int i = 0; i < gi_ninits; i++) {
		struct gi_init *in = &gi_inits[i];
		if (in->kind != GI_NEW_DEFAULT)
			continue;
		seen++;
		int slot = gi_gslot(in->global);
		const char *gname = gi_gname(in->global), *sname = gi_sname(in->struct_type);
		GI_CHECK(gi_live_struct(slot, in->struct_type), "%s: slot %d is not a live %s", gname, slot, sname);
		if (!gi_live_struct(slot, in->struct_type))
			continue;
		int strt = ain->structures[in->struct_type].constructor;
		if (strt <= 0) {
			printf("  %s: %s has no constructor\n", gname, sname);
			continue;
		}
		int inside, outside;
		gi_entries(strt, slot, &inside, &outside);
		if (gi_is_cdebug(in->struct_type)) {
			cdebug++;
			GI_CHECK(inside + outside == 0, "%s: %s ran %d time(s), CDebug constructors are skipped",
				 gname, ain->functions[strt].name, inside + outside);
			continue;
		}
		constructed++;
		GI_CHECK(inside == 1 && outside == 0, "%s: %s entered %d time(s) inside \"0\" and %d outside, want 1 and 0",
			 gname, ain->functions[strt].name, inside, outside);
	}
	printf("  %d NEW x, -1 globals: %d constructed once inside \"0\", %d CDebug skipped\n", seen, constructed, cdebug);
	GI_CHECK(seen >= 46 && constructed + cdebug == seen && cdebug == 8,
		 "expected the 46 NEW x, -1 globals of this AIN (38 constructed, 8 CDebug)");
}

static void gi_nested(void)
{
	gi_startup();
	int members = 0, live = 0, unset = 0, twice = 0;
	for (int i = 0; i < gi_ninits; i++) {
		struct gi_init *in = &gi_inits[i];
		if (in->kind != GI_NEW_DEFAULT || gi_is_cdebug(in->struct_type))
			continue;
		int slot = gi_gslot(in->global);
		if (!gi_live_struct(slot, in->struct_type))
			continue;
		struct ain_struct *s = &ain->structures[in->struct_type];
		for (int m = 0; m < s->nr_members && m < heap[slot].page->nr_vars; m++) {
			if (s->members[m].type.data != AIN_STRUCT)
				continue;
			members++;
			int mst = s->members[m].type.struc, child = heap[slot].page->values[m].i;
			if (child == -1) {
				unset++;
				printf("  %s.%s (%s) left -1 by its constructor\n", gi_gname(in->global), s->members[m].name, gi_sname(mst));
				continue;
			}
			bool ok = gi_live_struct(child, mst);
			GI_CHECK(ok, "%s.%s: slot %d is not a live %s", gi_gname(in->global), s->members[m].name, child, gi_sname(mst));
			if (!ok)
				continue;
			live++;
			int c = ain->structures[mst].constructor;
			if (c > 0) {
				int inside, outside;
				gi_entries(c, child, &inside, &outside);
				if (inside + outside > 1)
					twice++;
				GI_CHECK(inside + outside <= 1, "%s.%s: %s ran %d time(s) on it (%d outside \"0\")", gi_gname(in->global),
					 s->members[m].name, ain->functions[c].name, inside + outside, outside);
				GI_CHECK(outside == 0, "%s.%s: %s ran on it after \"0\"", gi_gname(in->global), s->members[m].name,
					 ain->functions[c].name);
			}
		}
	}
	printf("  %d value-struct members of NEW x, -1 globals: %d live, %d left -1, %d constructed twice\n",
	       members, live, unset, twice);
	GI_CHECK(members > 0 && live > 0, "no nested struct member found");
	GI_CHECK(unset == 0, "%d nested struct member(s) left -1 by their constructors", unset);
	/* CASFont from CreateFont: color members are CASColor objects */
	int st_font = gi_struct("CASFont"), st_color = gi_struct("CASColor");
	for (int i = 0; i < gi_ninits; i++) {
		if (gi_inits[i].kind != GI_CALLFUNC || !strstr(ain->functions[gi_inits[i].fno].name, "CreateFont"))
			continue;
		int slot = gi_gslot(gi_inits[i].global);
		if (!gi_live_struct(slot, st_font))
			continue;
		struct ain_struct *s = &ain->structures[st_font];
		int colors = 0;
		for (int m = 0; m < s->nr_members; m++) {
			if (s->members[m].type.data != AIN_STRUCT || s->members[m].type.struc != st_color)
				continue;
			int child = heap[slot].page->values[m].i;
			bool ok = gi_live_struct(child, st_color);
			GI_CHECK(ok, "CASFont.%s: slot %d is not a live CASColor", s->members[m].name, child);
			if (ok) {
				colors++;
				printf("  CASFont.%s = (%d, %d, %d, %d)\n", s->members[m].name, gi_int_member(child, 0),
				       gi_int_member(child, 1), gi_int_member(child, 2), gi_int_member(child, 3));
			}
		}
		GI_CHECK(colors == 2, "CASFont has %d live CASColor members, want 2", colors);
	}
}

static void gi_snapshot(void)
{
	gi_startup();
	if (!gi_snap_taken)
		return;
	struct page *g = heap[global_page_slot].page;
	int changed = 0, members_changed = 0, compared = 0;
	GI_CHECK(g->nr_vars == gi_snap_nglobals, "global page has %d variables now, %d when \"0\" returned", g->nr_vars, gi_snap_nglobals);
	for (int i = 0; i < ain->nr_globals && i < g->nr_vars && i < gi_snap_nglobals; i++) {
		enum ain_data_type t = ain->globals[i].type.data;
		int was = gi_snap_globals[i].i, now = g->values[i].i;
		if (was == now)
			continue;
		/* the -1 -> 0 pass for value globals the alloc function never set */
		if (was == -1 && now == 0 && (t == AIN_INT || t == AIN_BOOL || t == AIN_LONG_INT || t == AIN_FLOAT || t == AIN_ENUM || t == AIN_ENUM2))
			continue;
		changed++;
		if (changed <= 10)
			printf("  global %d %s: %d when \"0\" returned, %d now\n", i, gi_gname(i), was, now);
	}
	for (int k = 0; k < gi_snap_nstructs; k++) {
		struct gi_snap_struct *s = &gi_snap_structs[k];
		compared++;
		if (!da_live_page(s->slot) || heap[s->slot].page->index != s->index || heap[s->slot].page->nr_vars != s->nr_vars) {
			members_changed++;
			if (members_changed <= 10)
				printf("  struct global slot %d (%s) is not the page it was\n", s->slot, gi_sname(s->index));
			continue;
		}
		struct page *p = heap[s->slot].page;
		for (int m = 0; m < s->nr_vars; m++) {
			if (p->values[m].i == s->values[m].i)
				continue;
			members_changed++;
			if (members_changed <= 10)
				printf("  %s member %d (%s): %d when \"0\" returned, %d now\n", gi_sname(s->index), m,
				       m < ain->structures[s->index].nr_members ? ain->structures[s->index].members[m].name : "?",
				       s->values[m].i, p->values[m].i);
		}
	}
	printf("  %d struct globals compared: %d global slots changed, %d member values changed after \"0\"\n",
	       compared, changed, members_changed);
	GI_CHECK(compared >= 70, "only %d struct globals were live when \"0\" returned", compared);
	GI_CHECK(changed == 0, "%d global(s) changed between \"0\" returning and main", changed);
	GI_CHECK(members_changed == 0, "%d member value(s) of struct globals changed between \"0\" returning and main", members_changed);
}

/* The real NEW instruction at `at` in the alloc phase. */
static int gi_run_new(uint32_t at, bool alloc_phase)
{
	size_t ip = instr_ptr;
	int sp = stack_ptr;
	bool was = vm_in_alloc_phase;
	vm_in_alloc_phase = alloc_phase;
	instr_ptr = at;
	execute_instruction(NEW);
	instr_ptr = ip;
	vm_in_alloc_phase = was;
	int slot = stack_pop().i;
	GI_CHECK(stack_ptr == sp, "NEW left %d extra stack slot(s)", stack_ptr - sp);
	stack_ptr = sp;
	return slot;
}

static void gi_guard_v13(void)
{
	gi_ctor_table();
	gi_scan_alloc();
	GI_CHECK(gi_first_new_default != 0, "no NEW x, -1 in the alloc function");
	if (!gi_first_new_default)
		return;
	int st = LittleEndian_getDW(ain->code, gi_first_new_default + 2);
	int trivial = gi_fn("CASPadding@2");	/* RETURN only: runs under any version */
	int saved_ctor = ain->structures[st].constructor, saved_version = ain->version;
	ain->structures[st].constructor = trivial;
	gi_is_ctor[trivial] = true;
	ain->version = 13;
	gi_watch = true;
	int slot = gi_run_new(gi_first_new_default, true);
	gi_watch = false;
	ain->version = saved_version;
	ain->structures[st].constructor = saved_ctor;
	GI_CHECK(gi_live_struct(slot, st), "v13 NEW %s, -1 in the alloc phase did not make a live object (slot %d)", gi_sname(st), slot);
	int inside, outside;
	gi_entries(trivial, slot, &inside, &outside);
	printf("  v13 alloc-phase NEW %s, -1: constructor entered %d time(s)\n", gi_sname(st), inside + outside);
	GI_CHECK(inside + outside == 1, "v13: the constructor was entered %d time(s), want exactly once (init_struct)", inside + outside);
	if (gi_live_struct(slot, st))
		heap_unref(slot);
}

static void gi_guard_no_ctor(void)
{
	gi_ctor_table();
	/* a NEW x, -1 anywhere in the code whose struct has no constructor */
	uint32_t at = 0;
	int st = -1;
	for (size_t addr = 0; addr + 2 <= ain->code_size && !at;) {
		int16_t op = get_opcode(addr);
		if (op < 0 || op >= NR_OPCODES)
			break;
		if (op == NEW) {
			int s = LittleEndian_getDW(ain->code, addr + 2), c = LittleEndian_getDW(ain->code, addr + 6);
			if (c < 0 && s >= 0 && s < ain->nr_structures && ain->structures[s].constructor <= 0
			    && ain->structures[s].nr_members > 0) {
				at = (uint32_t)addr;
				st = s;
			}
		}
		addr += instruction_width(op);
	}
	if (!at) {
		printf("GLOBAL-INIT no NEW x, -1 of a struct without a constructor in this AIN\n");
		exit(2);
	}
	int before = gi_nevs;
	gi_watch = true;
	int slot = gi_run_new(at, true);
	gi_watch = false;
	printf("  alloc-phase NEW %s, -1 (no constructor) at 0x%x: slot %d, %d constructor entries\n", gi_sname(st), at, slot, gi_nevs - before);
	GI_CHECK(gi_live_struct(slot, st), "did not make a live %s", gi_sname(st));
	GI_CHECK(gi_nevs == before, "%d constructor(s) ran for a struct without one", gi_nevs - before);
	if (gi_live_struct(slot, st))
		heap_unref(slot);
}

static void gi_guard_manager(void)
{
	gi_startup();
	int st = gi_struct("CASTimerManager"), g = -1;
	for (int i = 0; i < ain->nr_globals; i++)
		if (ain->globals[i].type.data == AIN_STRUCT && ain->globals[i].type.struc == st)
			g = i;
	GI_CHECK(g >= 0, "no CASTimerManager global");
	if (g < 0)
		return;
	int slot = gi_gslot(g);
	GI_CHECK(gi_live_struct(slot, st), "%s: slot %d is not a live CASTimerManager", gi_gname(g), slot);
	if (!gi_live_struct(slot, st))
		return;
	const int expected_types[] = { AIN_ARRAY_STRUCT, AIN_ARRAY_INT, AIN_ARRAY_STRUCT, AIN_ARRAY_STRUCT };
	const int expected_structs[] = { 8, -1, 531, 531 };
	for (int i = 0; i < 4 && i < heap[slot].page->nr_vars; i++) {
		int a = heap[slot].page->values[i].i;
		bool ok = da_live_page(a) && heap[a].page->type == ARRAY_PAGE && heap[a].page->a_type == expected_types[i]
			&& heap[a].page->array.struct_type == expected_structs[i] && heap[a].page->array.rank == 1;
		printf("  %s member %d: slot %d%s\n", gi_gname(g), i, a, ok ? "" : "  <- not the array metadata pins");
		GI_CHECK(ok, "%s member %d is not the array the metadata mode pins", gi_gname(g), i);
	}
	int inside, outside;
	gi_entries(ain->structures[st].constructor, slot, &inside, &outside);
	GI_CHECK(inside == 1 && outside == 0, "%s: CASTimerManager@0 entered %d inside \"0\", %d outside", gi_gname(g), inside, outside);
}

static int test_global_init(void)
{
	static const struct { const char *name; void (*run)(void); } cases[] = {
		{ "GI1", gi_battle_background }, { "GI2", gi_explicit_ctors }, { "GI3", gi_function_results },
		{ "GI4", gi_default_ctors }, { "GI5", gi_nested }, { "GI6", gi_snapshot },
		{ "G1", gi_guard_v13 }, { "G2", gi_guard_no_ctor }, { "G3", gi_guard_manager },
	};
	int failed = 0, environment = 0;
	for (unsigned i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
		fflush(stdout);
		fflush(stderr);
		pid_t pid = fork();
		if (pid == 0) {
			alarm(300);
			gi_failures = 0;
			cases[i].run();
			fflush(stdout);
			_exit(gi_failures ? 1 : 0);
		}
		int st = 0;
		if (pid < 0 || waitpid(pid, &st, 0) != pid) {
			printf("%s ENVIRONMENT (fork/wait failed)\n", cases[i].name);
			environment++;
		} else if (WIFSIGNALED(st)) {
			printf("%s FAIL (terminated by signal %d)\n", cases[i].name, WTERMSIG(st));
			failed++;
		} else if (WEXITSTATUS(st) == 0) {
			printf("%s ok\n", cases[i].name);
		} else if (WEXITSTATUS(st) == 2) {
			printf("%s ENVIRONMENT (the AIN lacks something this case looks up)\n", cases[i].name);
			environment++;
		} else {
			printf("%s FAIL (exit %d)\n", cases[i].name, WEXITSTATUS(st));
			failed++;
		}
	}
	printf("GLOBAL-INIT %s: %d of %zu cases failed, %d environment error(s)\n",
	       failed || environment ? "FAIL" : "PASS", failed, sizeof(cases) / sizeof(cases[0]), environment);
	return environment ? 2 : failed ? 1 : 0;
}
