/* hll-param-handover: a value an object-typed hll_param argument carries is
 * handed over to the HLL call, which gives it back when the call returns.
 *
 * Original (static disassembly). CALLHLL (0x66d470) builds the argument
 * vector with 0x66a5e0 (called at 0x66d5e8), which pops each argument with
 * 0x66a700 -> 0x659970. For an hll_param (type 0x4a, the packed type operand
 * at +0x18) 0x659970 retains the popped value (0x679f10, at 0x659aa7) only
 * when 0x6597f0 and 0x659670 both say so (0x659a7e..0x659aa4): 0x6597f0
 * answers the wrap bit (bit 16) of the operand (0x659829), 0x659670 answers
 * "an object" (low word 2 or 3, or a wrapped plain value; 0x6596a5). The
 * value is moved into the vector (0x659b70 leaves the source without a type,
 * so the temporary's destructor 0x659d20 releases nothing). After the call
 * the vector is destroyed (0x6734b0 at 0x66d782; 0x673720 -> 0x659d20 per
 * element), and 0x659d20 releases the value (0x679f90, at 0x659d46) when
 * 0x659670 says "an object" (0x659d2d..0x659d43). So:
 *   - low word 2 without the wrap bit (2, and the option 0x20002): the call
 *     releases the value once: handed over. A_REF's value is a new page
 *     with the one reference the stack owns (0x66b790 -> 0x6794d0); that
 *     NEW, a function's result and a string expression leave a reference of
 *     their own as well is inferred, not read at an address.
 *   - with the wrap bit (0x10002, the interface 0x10003, 0x30002, 0x30003):
 *     retained on the way in and released on the way out: borrowed.
 * Storing an element retains it on its own (0x646d70 -> 0x67f290: retain the
 * new value, release the old one), except in an array of value structs,
 * whose new element holds a copy (0x646d70 -> 0x646e38; array-struct-copy).
 *
 * This VM never gave the handed-over reference back (ffi.c skips an hll_param
 * in its release loop), so every A_REF (or NEW) handed to Array.PushBack,
 * Insert, Add or IsExist with type operand 2 left one reference behind. In
 * the game that is the spring-sales screen: PlayerWorkerViewCollection
 * CreateView pushes each new card with ".LOCALREF view; A_REF; CALLHLL Array
 * PushBack 2" and later removes a dispatched card with Erase#1; the card kept
 * a reference, was never destroyed, its activity was never released, and it
 * stayed drawn at the front of the carousel.
 *
 *   HP1  The game's own bytecode, in two frames of the real CreateView: the
 *        push sequence (from the m_uiView reference through the release of
 *        the view local) on a card whose m_act comes from the real
 *        AFL_Activity_Create of a synthetic activity, then the real loop
 *        over deleted ids with Erase#1 and its real predicate. m_uiView is
 *        an array<PlayerWorkerView>, an array of value structs: the push
 *        stores a new element holding a copy of the card (0x67f5c0, then
 *        0x646d70 -> 0x646e38; array-struct-copy), whose wrap members
 *        share the card's objects. After the push the array holds that
 *        element, with one reference (the array's) and the card's m_act;
 *        the card itself is gone with the view local. After the erase the
 *        element is gone, the CActivityWrap destructor and
 *        AFL_Activity_Release ran once each, PartsEngine.ReleaseActivity
 *        ran once for its name and the name is free. Nothing more is
 *        released when the collection goes.
 *   HP2  The real ffi path (hll_call) with type operand 2 and a struct value
 *        as this VM's A_REF leaves it (the owner's page with one more
 *        reference: A_REF shares a struct with reference members here):
 *        PushBack, Add and Insert leave the owner's reference alone, and
 *        the array holds a new element of its own (array-struct-copy);
 *        IsExist leaves the owner's. After the owner and the array go the
 *        struct and the element are destroyed, once each.
 *   HP3  Other pages. Shared, as in HP2: an array of structs (A_REF shares
 *        it here) and a delegate another holder keeps (synthetic: one
 *        reference handed over with it). Copied, as A_REF leaves them here
 *        (a number array, a delegate, a struct of plain numbers; vm.c
 *        A_REF): the copy is the array's alone after PushBack and is gone
 *        after IsExist; into an array of value structs, the struct's copy
 *        is gone after PushBack too, the array holding a new element of
 *        its own (array-struct-copy).
 *   HP4  The option 0x20002 (an option of an object without the wrap bit)
 *        holding a struct: handed over too. A none option releases nothing.
 *        No call site of this AIN passes a page through 0x20002 (its two Add
 *        sites carry strings); the expectation is the disassembly's alone.
 * Guards, which pass before the change as well:
 *   HP5  Borrowed: 0x10002 (a wrap), 0x10003 (an interface, two slots) and
 *        0x30002 (an option of a wrap) keep the caller's references and add
 *        the array's; type operand 1 (a plain value) touches no slot.
 *   HP6  Strings with type operand 2 keep this VM's handling (temporary
 *        flag; the array shares the slot): a temporary and an owned string
 *        pushed, an owned one looked up with IsExist. These counts are this
 *        branch's at 8f9cd6d, not the original's (natively the call releases
 *        a string value too and the array holds its own copy).
 *   HP7  Version 13 (and 11): the struct of HP2 keeps this branch's v11-v13
 *        rule at 8f9cd6d (nothing given back). Not a v13 game comparison.
 *   HP8  Static premise: in this AIN every CALLHLL whose last argument is a
 *        value hll_param of type operand 2 gets that argument from a
 *        hand-over instruction (A_REF, NEW, a string expression, a call's
 *        result, the end of a DG_CALL), found on the control-flow graph
 *        (jumps to a label in front of the call are followed back). A call
 *        producer's callee is read where the code names it (CALLFUNC,
 *        CALLHLL, the delegate of a DG_CALL, and a CALLMETHOD whose method
 *        number is pushed right before its arguments) and returns a value,
 *        not a reference. Every such site calls a bound function, and every
 *        hll_param argument any library declares is the last argument of an
 *        Array function.
 *
 * Where the expectations are xsystem4's, not the original's: the array
 * shares the struct page with the caller (natively the element is a copy and
 * the A_REF copy is freed by the call), strings (HP6), and v11-v13 (HP7).
 * HP1 needs what the game's start-up would have made (the parts engine's
 * _PreLink forms, the user component manager list as an empty array) and a
 * synthetic activity; the card is allocated without its constructor (its
 * worker, an id string and the m_act of the real AFL_Activity_Create are put
 * in by the probe; the user component member is cleared because no user
 * component is made).
 * Not covered: Fill#1 and Find with a page value (the AIN passes strings
 * there; Find of a struct array is not supported by this VM); Realloc#1,
 * FindLast, LowerBound, UpperBound and BinarySearch with type operand 2 (no
 * call site); a CALLHLL of another library with a value hll_param (none
 * declared); an interface without the wrap bit (low word 3, 0x3 or 0x20003:
 * natively handed over as well, but no call site of this AIN has one); a
 * function without a binding (its arguments are dropped unreleased; HP8
 * finds none at a type-2 site); the callee of a CALLMETHOD whose method
 * number comes from a vtable (HP8 counts these and does not resolve them).
 *
 * Every case runs in its own child (forked_cases.inc). Exit code 1 is a
 * failed check, 2 the environment (the AIN lacks a function, struct, member
 * or instruction pattern a case looks for).
 */

static int hp_struct(const char *name)
{
	for (int i = 0; i < ain->nr_structures; i++)
		if (ain->structures[i].name && !strcmp(ain->structures[i].name, name))
			return i;
	printf("  ENVIRONMENT: struct %s not declared\n", name);
	xc_env_fail("struct");
	return -1;
}

static int hp_member(int sno, const char *name)
{
	struct ain_struct *s = &ain->structures[sno];
	for (int i = 0; i < s->nr_members; i++)
		if (s->members[i].name && !strcmp(s->members[i].name, name))
			return i;
	printf("  ENVIRONMENT: member %s.%s not declared\n", s->name, name);
	xc_env_fail("member");
	return -1;
}

static int hp_fn(const char *name, int nargs)
{
	for (int i = 0; i < ain->nr_functions; i++)
		if (ain->functions[i].name && !strcmp(ain->functions[i].name, name)
		    && (nargs < 0 || ain->functions[i].nr_args == nargs))
			return i;
	printf("  ENVIRONMENT: function %s/%d not declared\n", name, nargs);
	xc_env_fail("function");
	return -1;
}

static int hp_var(int fno, const char *name)
{
	struct ain_function *f = &ain->functions[fno];
	int found = -1;
	for (int i = 0; i < f->nr_vars; i++)
		if (f->vars[i].name && !strcmp(f->vars[i].name, name)) {
			if (found >= 0) {
				printf("  ENVIRONMENT: two locals named %s\n", name);
				xc_env_fail("local");
			}
			found = i;
		}
	if (found < 0) {
		printf("  ENVIRONMENT: local %s not declared\n", name);
		xc_env_fail("local");
	}
	return found;
}

static bool hp_alive(int slot, uint32_t seq)
{
	return slot > 1 && heap_index_valid(slot) && heap[slot].seq == seq && HEAP_REF(slot) > 0;
}

static int hp_refs(int slot, uint32_t seq)
{
	return hp_alive(slot, seq) ? HEAP_REF(slot) : 0;
}

static int hp_array(enum ain_data_type type, int struct_type)
{
	union vm_value zero = { .i = 0 };
	return heap_alloc_page(alloc_array(1, &zero, type, struct_type, false));
}

/* Elements of an array slot; an emptied array keeps its slot without a page
 * in this VM (Array_Erase frees the page). */
static int hp_numof(int arr)
{
	if (arr <= 0 || !heap_index_valid(arr))
		return -1;
	struct page *p = heap[arr].page;
	return p ? p->nr_vars : 0;
}

static int hp_elem(int arr, int i)
{
	struct page *p = heap_index_valid(arr) ? heap[arr].page : NULL;
	return p && i < p->nr_vars ? p->values[i].i : -999;
}

/* Array.name#ordinal through the real ffi with the given type operand. */
static int hp_hll(const char *name, int ordinal, int arg3, int nslots, const int *slots)
{
	int out[2];
	return oa_hll(name, ordinal, arg3, nslots, slots, out, 2);
}

/* --- decoding the game's own code --- */

struct hp_ins { uint32_t ip; enum opcode op; };
#define HP_MAX_INS 4096
static struct hp_ins hp_code[HP_MAX_INS];
static int hp_ncode;

static int32_t hp_arg(int k, int n)
{
	return LittleEndian_getDW(ain->code, hp_code[k].ip + 2 + 4 * n);
}

/* The instructions of [functions[fno].address, functions[next].address). */
static void hp_decode(int fno, int next)
{
	uint32_t lo = ain->functions[fno].address, hi = ain->functions[next].address;
	if (hi <= lo || hi > ain->code_size) {
		printf("  ENVIRONMENT: %s does not end where %s begins\n", ain->functions[fno].name, ain->functions[next].name);
		xc_env_fail("range");
	}
	hp_ncode = 0;
	for (uint32_t ip = lo; ip < hi && hp_ncode < HP_MAX_INS;) {
		enum opcode op = get_opcode(ip);
		int w = instruction_width(op);
		if (w <= 0)
			xc_env_fail("an instruction without a width");
		hp_code[hp_ncode++] = (struct hp_ins){ ip, op };
		ip += w;
	}
}

static int hp_find_callhll(const char *fn, int ordinal, int arg3)
{
	int lib = bu_lib("Array"), f = oa_fn(fn, ordinal), at = -1;
	for (int k = 0; k < hp_ncode; k++) {
		if (hp_code[k].op == CALLHLL && hp_arg(k, 0) == lib && hp_arg(k, 1) == f && hp_arg(k, 2) == arg3) {
			if (at >= 0)
				xc_env_fail("two such CALLHLL");
			at = k;
		}
	}
	if (at < 0) {
		printf("  ENVIRONMENT: no CALLHLL Array %s#%d %d\n", fn, ordinal, arg3);
		xc_env_fail("callhll");
	}
	return at;
}

/* Prints instructions [from, to) (opcode names and first operands) for a
 * pattern that did not match. */
static void hp_dump(const char *what, int from, int to)
{
	printf("  %s:", what);
	for (int k = from < 0 ? 0 : from; k < to && k < hp_ncode; k++) {
		printf(" %s", instructions[hp_code[k].op].name);
		for (int a = 0; a < instructions[hp_code[k].op].nr_args && a < 3; a++)
			printf(a ? ",%d" : " %d", hp_arg(k, a));
		printf(";");
	}
	printf("\n");
}

static bool hp_is(int k, enum opcode op, int arg)
{
	return k >= 0 && k < hp_ncode && hp_code[k].op == op && (arg == INT32_MIN || hp_arg(k, 0) == arg);
}

/* A local load: PUSHLOCALPAGE; PUSH var; X_REF 1 (.LOCALREF in v14). */
static bool hp_localref(int k, int var)
{
	return hp_is(k, PUSHLOCALPAGE, INT32_MIN) && hp_is(k + 1, PUSH, var) && hp_is(k + 2, X_REF, 1);
}

/* Executes the real instructions from instr_ptr until it reaches stop (or
 * VM_RETURN), as vm_execute does. */
static void hp_run_to(uint32_t stop)
{
	for (unsigned n = 0; instr_ptr != stop && instr_ptr != VM_RETURN; n++) {
		if (n > 200000)
			xc_env_fail("the bytecode did not reach its stop");
		enum opcode op = get_opcode(instr_ptr);
		op = execute_instruction(op);
		instr_ptr += instructions[op].ip_inc;
	}
}

/* --- HP1 --- */

#define HP_CARD_FILE "HPCard"

static bool (*hp_release_real)(struct string *, int);
static int hp_release_n;
static char hp_release_name[96];

static bool hp_release_activity(struct string *name, int erase_list)
{
	hp_release_n++;
	snprintf(hp_release_name, sizeof hp_release_name, "%s", name ? name->text : "(null)");
	return hp_release_real(name, erase_list);
}

/* A layout root with one low-level parts, through the production loader. */
static bool hp_read_activity_file(struct string *name, possibly_unused struct string *filename,
		possibly_unused bool edit)
{
	struct ex *e = xcalloc(1, sizeof(*e));
	e->nr_blocks = 1;
	e->blocks = xcalloc(1, sizeof(*e->blocks));
	e->blocks[0].name = cstr_to_string("HandoverCard");
	e->blocks[0].val.type = EX_TREE;
	struct ex_tree *top = xcalloc(1, sizeof(*top));
	*top = bu_branch("HandoverCard", 1);
	e->blocks[0].val.tree = top;
	top->children[0] = bu_comp(AR_ROOT, BU_LAYOUT, 0, 1);
	top->children[0].children[1].children[0] = bu_comp("Mark", BU_LOW, 0, 0);
	bool ok = probe_load_activity_tree(name, e);
	ex_free(e);
	return ok;
}

static void hp_card_setup(void)
{
	lr_setup();
	lr_replace_hll("PartsEngine", "ReadActivityFile", (void *)hp_read_activity_file);
	int lib = bu_lib("PartsEngine");
	for (int j = 0; j < ain->libraries[lib].nr_functions; j++)
		if (!strcmp(ain->libraries[lib].functions[j].name, "ReleaseActivity"))
			hp_release_real = (bool (*)(struct string *, int))libraries[lib][j].fun;
	if (!hp_release_real)
		xc_env_fail("PartsEngine.ReleaseActivity is not linked");
	lr_replace_hll("PartsEngine", "ReleaseActivity", (void *)hp_release_activity);
	int g = ci_global("activity::detail::g_UserComponentManagerList");
	if (heap[global_page_slot].page->values[g].i < 0)
		heap[global_page_slot].page->values[g] = variable_initval(ain->globals[g].type.data);
	probe_step_budget = 50000000UL;
}

/* Calls a script function with one-slot arguments and copies up to nout
 * result slots; returns how many it left. */
static int hp_call(int fno, int nargs, const int *args, int *out, int nout)
{
	int sp = stack_ptr, csp = call_stack_ptr;
	size_t ip = instr_ptr;
	for (int i = 0; i < nargs; i++)
		stack_push(args[i]);
	function_call(fno, VM_RETURN);
	vm_execute();
	instr_ptr = ip;
	int n = stack_ptr - sp;
	if (call_stack_ptr != csp || n < 0)
		xc_env_fail("a script call left the stack unbalanced");
	for (int i = 0; i < n && i < nout; i++)
		out[i] = stack[sp + i].i;
	stack_ptr = sp;
	return n;
}

/* Enters CreateView as a method of coll (its frame and locals as the VM makes
 * them); the caller runs a stretch of its code and then hp_leave. */
static int hp_enter(int fno, int coll, int *sp_out)
{
	*sp_out = stack_ptr;
	stack_push(coll);	/* the frame's this (set_struct_page keeps it alive) */
	method_call(fno, VM_RETURN);
	return call_stack[call_stack_ptr - 1].page_slot;
}

/* Runs the function's own last RETURN. */
static void hp_leave(int sp, int csp)
{
	int last = -1;
	for (int k = 0; k < hp_ncode; k++)
		if (hp_code[k].op == RETURN)
			last = k;
	if (last < 0)
		xc_env_fail("no RETURN");
	instr_ptr = hp_code[last].ip;
	hp_run_to(VM_RETURN);
	if (stack_ptr != sp || call_stack_ptr != csp)
		xc_env_fail("the frame did not return cleanly");
}

static void hp_set_local(int page_slot, int var, int value)
{
	struct page *lp = heap[page_slot].page;
	int old = lp->values[var].i;
	lp->values[var].i = value;
	if (old > 0 && heap_index_valid(old) && HEAP_REF(old) > 0)
		heap_unref(old);
}

static void hp1_card(void)
{
	hp_card_setup();
	int sv = hp_struct("PlayerWorkerView"), sc = hp_struct("PlayerWorkerViewCollection");
	int sw = hp_struct("Worker"), sa = hp_struct("activity::detail::CActivityWrap");
	int create_view = hp_fn("PlayerWorkerViewCollection@CreateView", 0);
	int after = hp_fn("PlayerWorkerViewCollection@SetViewPos", 0);
	int dtor_wrap = ain->structures[sa].destructor;
	int afl_release = hp_fn("AFL_Activity_Release", 1);
	if (dtor_wrap <= 0)
		xc_env_fail("CActivityWrap has no destructor");
	hp_decode(create_view, after);
	int v_view = hp_var(create_view, "view"), v_deleted = hp_var(create_view, "deleted");
	int v_added = hp_var(create_view, "added");
	int m_ui = hp_member(sc, "m_uiView"), m_worker = hp_member(sv, "m_worker");
	int m_act = hp_member(sv, "m_act"), m_wv = hp_member(sv, "m_workerView"), m_id = hp_member(sw, "Id");
	/* the push: .STRUCTREF m_uiView; .LOCALREF view; A_REF; CALLHLL Array PushBack 2
	 * (PUSHSTRUCTPAGE; PUSH m; X_REF 1; PUSHLOCALPAGE; PUSH v; X_REF 1; A_REF) */
	int kp = hp_find_callhll("PushBack", 0, 2);
	if (!(hp_is(kp - 7, PUSHSTRUCTPAGE, INT32_MIN) && hp_is(kp - 6, PUSH, m_ui) && hp_is(kp - 5, X_REF, 1)
	      && hp_localref(kp - 4, v_view) && hp_is(kp - 1, A_REF, INT32_MIN))) {
		printf("  m_uiView is member %d, view is local %d\n", m_ui, v_view);
		hp_dump("before the push", kp - 12, kp + 14);
		xc_env_fail("CreateView's push is not .STRUCTREF m_uiView; .LOCALREF view; A_REF");
	}
	int ps = -1, nlocal = 0;	/* the end: past the releases of the two locals after the call */
	for (int k = kp + 1; k < hp_ncode && ps < 0; k++) {
		if (hp_code[k].op != PUSHLOCALPAGE || !hp_is(k + 1, PUSH, INT32_MIN))
			continue;
		int v = hp_arg(k + 1, 0);
		if (nlocal++ == 2)
			ps = k;
		else if (v != v_view && v != v_view + 1)
			xc_env_fail("CreateView does not release the view local right after the push");
	}
	/* the erase: from .LOCALREF deleted (the foreach start, which then releases
	 * the loop's hidden ref local: PUSHLOCALPAGE; PUSH v; X_DUP 2; X_REF 1;
	 * DELETE) to .LOCALREF added */
	int ke = hp_find_callhll("Erase", 1, 2), es = -1, ee = -1;
	for (int k = ke - 1; k >= 0 && es < 0; k--)
		if (hp_localref(k, v_deleted) && hp_is(k + 3, PUSHLOCALPAGE, INT32_MIN) && hp_is(k + 4, PUSH, INT32_MIN)
		    && hp_is(k + 5, X_DUP, 2) && hp_is(k + 6, X_REF, 1) && hp_is(k + 7, DELETE, INT32_MIN))
			es = k;
	for (int k = ke + 1; k < hp_ncode && ee < 0; k++)
		if (hp_localref(k, v_added))
			ee = k;
	if (ps < 0 || es < 0 || ee < 0) {
		printf("  push end %d, erase start %d, erase end %d (deleted is local %d, added %d)\n", ps, es, ee, v_deleted, v_added);
		hp_dump("before the erase", ke - 110, ke - 38);
		xc_env_fail("CreateView's push or erase loop not found");
	}
	printf("HP1 push %#x..%#x (%d instructions), erase loop %#x..%#x (%d instructions)\n", hp_code[kp - 7].ip,
	       hp_code[ps].ip, ps - (kp - 7), hp_code[es].ip, hp_code[ee].ip, ee - es);
	xc_ready();

	/* the card, without its constructor */
	int coll = alloc_struct(sc);
	uint32_t cseq = heap[coll].seq;
	int arr = heap[coll].page->values[m_ui].i;
	XC_CHECK(hp_numof(arr) == 0, "precondition: m_uiView of a new collection has %d slot(s)", hp_numof(arr));
	int view = alloc_struct(sv);
	uint32_t vseq = heap[view].seq;
	int worker = heap[view].page->values[m_worker].i;
	if (!(worker > 0 && heap_index_valid(worker) && heap[worker].page && heap[worker].page->index == sw))
		xc_env_fail("the card's worker is not a Worker object");
	hp_set_local(worker, m_id, sf_str("hp-card", -1));
	hp_set_local(view, m_wv, -1);
	int fname = sf_str(HP_CARD_FILE, -1), act[2] = { -1, 0 };
	int nact = hp_call(hp_fn("AFL_Activity_Create", 1), 1, (int[]){ fname }, act, 2);
	heap_unref(fname);
	if (!(nact >= 1 && act[0] > 0 && heap_index_valid(act[0]) && heap[act[0]].page && heap[act[0]].page->index == sa))
		xc_env_fail("AFL_Activity_Create did not return a CActivityWrap");
	struct page *vp = heap[view].page;
	vp->values[m_act].i = act[0];
	if (nact == 2 && m_act + 1 < vp->nr_vars)
		vp->values[m_act + 1].i = act[1];
	XC_CHECK(lr_activity_exists(HP_CARD_FILE), "precondition: the activity %s was not created", HP_CARD_FILE);
	unsigned long d_wrap = calls[dtor_wrap], d_rel = calls[afl_release];
	uint32_t aseq = heap[act[0]].seq;

	/* frame 1: the push */
	int sp, csp = call_stack_ptr;
	int lp = hp_enter(create_view, coll, &sp);
	hp_set_local(lp, v_view, view);	/* NEW's reference, as X_ASSIGN leaves it in the local */
	instr_ptr = hp_code[kp - 7].ip;
	hp_run_to(hp_code[ps].ip);
	/* the element: a new PlayerWorkerView holding the card's members */
	int el = hp_elem(arr, 0);
	uint32_t elseq = el > 0 && heap_index_valid(el) ? heap[el].seq : 0;
	XC_CHECK(hp_numof(arr) == 1 && el != view && hp_alive(el, elseq) && heap[el].page && heap[el].page->index == sv
		 && heap[el].page->values[m_act].i == act[0],
		 "after the push m_uiView holds %d slot(s), first %d; want a new PlayerWorkerView (not the card %d) holding the "
		 "card's m_act %d", hp_numof(arr), el, view, act[0]);
	XC_CHECK(hp_refs(el, elseq) == 1 && !hp_alive(view, vseq),
		 "after the push and the release of the view local the element has %d reference(s) and the card %d; want 1 "
		 "(the array's) and the card gone with its local", hp_refs(el, elseq), hp_refs(view, vseq));
	/* the card went, the element still shares its m_act: the wrap lives on
	 * and nothing of it has been destroyed yet */
	XC_CHECK(hp_alive(act[0], aseq), "after the card went its CActivityWrap %d is %s; want it alive (the element's m_act)",
		 act[0], hp_alive(act[0], aseq) ? "alive" : "gone");
	XC_CHECK(calls[dtor_wrap] == d_wrap, "after the card went CActivityWrap's destructor ran %lu time(s); want 0 (the "
		 "element still holds the wrap)", calls[dtor_wrap] - d_wrap);
	hp_leave(sp, csp);
	XC_CHECK(hp_refs(el, elseq) == 1 && hp_elem(arr, 0) == el,
		 "after CreateView returned the element has %d reference(s) and m_uiView's first element is %d; want 1 and %d",
		 hp_refs(el, elseq), hp_elem(arr, 0), el);

	/* frame 2: the loop over deleted ids */
	lp = hp_enter(create_view, coll, &sp);
	struct page *del = alloc_page(ARRAY_PAGE, AIN_ARRAY_STRING, 1);
	del->array.rank = 1;
	del->array.struct_type = -1;
	del->values[0].i = sf_str("hp-card", -1);
	hp_set_local(lp, v_deleted, heap_alloc_page(del));
	instr_ptr = hp_code[es].ip;
	hp_run_to(hp_code[ee].ip);
	XC_CHECK(heap[coll].page->values[m_ui].i == arr && hp_numof(arr) == 0,
		 "after Erase#1 m_uiView is slot %d (was %d) with %d element(s), want the same slot and 0",
		 heap[coll].page->values[m_ui].i, arr, hp_numof(arr));
	XC_CHECK(!hp_alive(el, elseq), "after Erase#1 the element is still alive with %d reference(s): it is never destroyed",
		 hp_refs(el, elseq));
	XC_CHECK(calls[dtor_wrap] == d_wrap + 1, "CActivityWrap's destructor ran %lu time(s) after Erase#1, want 1",
		 calls[dtor_wrap] - d_wrap);
	XC_CHECK(calls[afl_release] == d_rel + 1, "AFL_Activity_Release ran %lu time(s) after Erase#1, want 1",
		 calls[afl_release] - d_rel);
	XC_CHECK(hp_release_n == 1 && !strcmp(hp_release_name, HP_CARD_FILE) && !lr_activity_exists(HP_CARD_FILE),
		 "PartsEngine.ReleaseActivity ran %d time(s) (last %s) and the activity %s, want once for %s and gone",
		 hp_release_n, hp_release_n ? hp_release_name : "-", lr_activity_exists(HP_CARD_FILE) ? "exists" : "is gone",
		 HP_CARD_FILE);
	hp_leave(sp, csp);
	heap_unref(coll);
	XC_CHECK(!hp_alive(coll, cseq), "the collection outlived its last reference");
	XC_CHECK(hp_release_n == 1 && calls[dtor_wrap] == d_wrap + 1,
		 "after the collection went: ReleaseActivity %d time(s), CActivityWrap destructor %lu, want 1 and 1",
		 hp_release_n, calls[dtor_wrap] - d_wrap);
	printf("HP1 the game's CreateView push and Erase#1 of a card: %s\n", xc_failures ? "FAIL" : "ok");
}

/* --- HP2..HP7: the real ffi --- */

static int hp_dtor_struct;
static unsigned long hp_dtor_calls(void)
{
	return calls[ain->structures[hp_dtor_struct].destructor];
}

/* The value the real A_REF leaves on the stack for slot (vm.c A_REF): the
 * same page with one more reference, or a copy with one of its own. */
static int hp_aref(int slot)
{
	int sp = stack_ptr;
	stack_push(slot);
	execute_instruction(A_REF);
	if (stack_ptr != sp + 1)
		xc_env_fail("A_REF left the stack unbalanced");
	return stack_pop().i;
}

/* A struct the caller owns (one reference) plus the one A_REF hands over
 * with it: A_REF shares the page of this struct in this VM. */
static int hp_handed(uint32_t *seq)
{
	int s = alloc_struct(hp_dtor_struct);
	*seq = heap[s].seq;
	if (hp_aref(s) != s || HEAP_REF(s) != 2) {
		printf("  ENVIRONMENT: A_REF of %s did not share its page\n", ain->structures[hp_dtor_struct].name);
		xc_env_fail("A_REF copies the struct");
	}
	return s;
}

/* A copy as A_REF makes it in this VM (one reference, nobody else's). */
static int hp_aref_copy(int src, const char *what, uint32_t *seq)
{
	int c = hp_aref(src);
	if (c == src || !hp_alive(c, heap_get_seq(c)) || HEAP_REF(c) != 1) {
		printf("  ENVIRONMENT: A_REF of %s did not copy it\n", what);
		xc_env_fail("A_REF shares the page");
	}
	*seq = heap[c].seq;
	return c;
}

static void hp_ffi_setup(void)
{
	hp_dtor_struct = oa_dtor_struct();
	xc_ready();
}

static void hp2_struct(void)
{
	hp_ffi_setup();
	static const struct { const char *fn; int ord; bool stores; } calls_[] = {
		{ "PushBack", 0, true }, { "Add", 0, true }, { "Insert", 0, true }, { "IsExist", 0, false },
	};
	for (unsigned i = 0; i < sizeof calls_ / sizeof *calls_; i++) {
		uint32_t seq;
		int arr = hp_array(AIN_ARRAY_STRUCT, hp_dtor_struct), s = hp_handed(&seq);
		unsigned long d0 = hp_dtor_calls();
		int left;
		if (!strcmp(calls_[i].fn, "Insert"))
			left = hp_hll("Insert", 0, 2, 3, (int[]){ arr, 0, s });
		else
			left = hp_hll(calls_[i].fn, calls_[i].ord, 2, 2, (int[]){ arr, s });
		XC_CHECK(left == (calls_[i].stores ? 0 : 1), "Array.%s left %d result slot(s)", calls_[i].fn, left);
		XC_CHECK(hp_refs(s, seq) == 1, "Array.%s(type 2) of a struct: %d reference(s) after the call, want 1 "
			 "(the caller's): the handed-over reference was not given back, or the array holds the struct itself",
			 calls_[i].fn, hp_refs(s, seq));
		/* an array of value structs holds a new element of its own (array-struct-copy) */
		int el = hp_elem(arr, 0);
		uint32_t elseq = el > 0 && heap_index_valid(el) ? heap[el].seq : 0;
		XC_CHECK(hp_numof(arr) == (calls_[i].stores ? 1 : 0)
			 && (!calls_[i].stores || (el != s && hp_alive(el, elseq) && HEAP_REF(el) == 1)),
			 "Array.%s stored %d slot(s), the first %d (the struct %d)", calls_[i].fn, hp_numof(arr), el, s);
		heap_unref(s);		/* the caller's */
		heap_unref(arr);	/* the array, with its element */
		unsigned long want = calls_[i].stores ? 2 : 1;
		XC_CHECK(!hp_alive(s, seq) && (!calls_[i].stores || !hp_alive(el, elseq)) && hp_dtor_calls() == d0 + want,
			 "after Array.%s the caller and the array went: the struct %s with %d reference(s), destructor ran %lu time(s); "
			 "want each object destroyed once (%lu)", calls_[i].fn, hp_alive(s, seq) ? "alive" : "gone", hp_refs(s, seq),
			 hp_dtor_calls() - d0, want);
		while (hp_alive(s, seq))
			heap_unref(s);
	}
	printf("HP2 a handed-over struct through PushBack, Add, Insert and IsExist: %s\n", xc_failures ? "FAIL" : "ok");
}

/* PushBack(type 2) of a copy A_REF made of src into a fresh array of
 * elem_type: the copy is the array's alone and goes with it; IsExist(type 2)
 * of another copy frees it. src keeps its one reference throughout. */
static void hp3_copied(const char *what, int src, enum ain_data_type elem_type, int struct_type)
{
	uint32_t sseq = heap[src].seq, seq;
	int arr = hp_array(elem_type, struct_type);
	int c = hp_aref_copy(src, what, &seq);
	hp_hll("PushBack", 0, 2, 2, (int[]){ arr, c });
	if (elem_type == AIN_ARRAY_STRUCT) {
		/* an array of value structs: a new element of its own; the copy
		 * A_REF handed over is given back and goes (array-struct-copy) */
		int el = hp_elem(arr, 0);
		XC_CHECK(!hp_alive(c, seq) && el != c && el > 0 && heap_index_valid(el) && HEAP_REF(el) == 1,
			 "PushBack(type 2) of A_REF's copy of %s: the copy %s, element %d; want the copy gone and a new element "
			 "with one reference", what, hp_alive(c, seq) ? "alive" : "gone", el);
	} else {
		XC_CHECK(hp_refs(c, seq) == 1 && hp_elem(arr, 0) == c,
			 "PushBack(type 2) of A_REF's copy of %s: %d reference(s), element %d; want 1 (the array's) and the copy %d",
			 what, hp_refs(c, seq), hp_elem(arr, 0), c);
	}
	heap_unref(arr);
	XC_CHECK(!hp_alive(c, seq), "the copy of %s outlived its array (%d reference(s))", what, hp_refs(c, seq));
	while (hp_alive(c, seq))
		heap_unref(c);
	arr = hp_array(elem_type, struct_type);
	c = hp_aref_copy(src, what, &seq);
	int left = hp_hll("IsExist", 0, 2, 2, (int[]){ arr, c });
	XC_CHECK(left == 1 && !hp_alive(c, seq), "IsExist(type 2) left A_REF's copy of %s alive with %d reference(s)", what,
		 hp_refs(c, seq));
	while (hp_alive(c, seq))
		heap_unref(c);
	heap_unref(arr);
	XC_CHECK(hp_refs(src, sseq) == 1, "the source of the copies of %s has %d reference(s), want its owner's 1", what,
		 hp_refs(src, sseq));
	while (hp_alive(src, sseq))
		heap_unref(src);
}

static void hp3_pages(void)
{
	hp_ffi_setup();
	int spf = hp_struct("CASPosF");
	/* shared: an array of structs, whose page A_REF shares in this VM */
	{
		int arr = hp_array(AIN_ARRAY, -1);
		int v = hp_array(AIN_ARRAY_STRUCT, spf);
		uint32_t seq = heap[v].seq;
		if (hp_aref(v) != v || HEAP_REF(v) != 2)
			xc_env_fail("A_REF copies an array of structs");
		hp_hll("PushBack", 0, 2, 2, (int[]){ arr, v });
		XC_CHECK(hp_refs(v, seq) == 2, "PushBack(type 2) of an array of structs: %d reference(s), want 2 (the caller's and the array's)",
			 hp_refs(v, seq));
		heap_unref(v);
		heap_unref(arr);
		XC_CHECK(!hp_alive(v, seq), "the array value outlived the caller and the array (%d reference(s))", hp_refs(v, seq));
		while (hp_alive(v, seq))
			heap_unref(v);
	}
	/* shared: a delegate another holder keeps (synthetic: one reference
	 * handed over with it, the shape of HP2; A_REF copies a delegate) */
	{
		int arr = hp_array(AIN_ARRAY, -1);
		int d = heap_alloc_page(alloc_page(DELEGATE_PAGE, 0, 0));
		uint32_t seq = heap[d].seq;
		heap_ref(d);
		hp_hll("PushBack", 0, 2, 2, (int[]){ arr, d });
		XC_CHECK(hp_refs(d, seq) == 2, "PushBack(type 2) of a shared delegate: %d reference(s), want 2", hp_refs(d, seq));
		heap_unref(d);
		heap_unref(arr);
		XC_CHECK(!hp_alive(d, seq), "the delegate outlived the caller and the array (%d reference(s))", hp_refs(d, seq));
		while (hp_alive(d, seq))
			heap_unref(d);
	}
	/* copied: a number array, a delegate and a struct of plain numbers */
	union vm_value one = { .i = 1 };
	int ints = heap_alloc_page(alloc_array(1, &one, AIN_ARRAY_INT, -1, false));
	heap[ints].page->values[0].i = 5;
	hp3_copied("a number array", ints, AIN_ARRAY, -1);
	hp3_copied("a delegate", heap_alloc_page(alloc_page(DELEGATE_PAGE, 0, 0)), AIN_ARRAY, -1);
	hp3_copied("a struct of plain numbers", alloc_struct(spf), AIN_ARRAY_STRUCT, spf);
	printf("HP3 handed-over arrays, delegates and plain-struct copies: %s\n", xc_failures ? "FAIL" : "ok");
}

static void hp4_option(void)
{
	hp_ffi_setup();
	uint32_t seq;
	int arr = hp_array(AIN_ARRAY, -1), s = hp_handed(&seq);
	unsigned long d0 = hp_dtor_calls();
	hp_hll("PushBack", 0, 0x20002, 3, (int[]){ arr, s, 0 });
	XC_CHECK(hp_refs(s, seq) == 2, "PushBack(0x20002) of an option holding a struct: %d reference(s), want 2 "
		 "(the caller's and the array's; natively the payload is handed over, 0x6597f0 has no wrap bit)", hp_refs(s, seq));
	XC_CHECK(hp_numof(arr) == 2 && hp_elem(arr, 0) == s && hp_elem(arr, 1) == 0, "the option element is %d,%d (%d slots)",
		 hp_elem(arr, 0), hp_elem(arr, 1), hp_numof(arr));
	heap_unref(s);
	heap_unref(arr);
	XC_CHECK(!hp_alive(s, seq) && hp_dtor_calls() == d0 + 1, "the option's struct %s (%d reference(s)), destructor %lu time(s)",
		 hp_alive(s, seq) ? "alive" : "gone", hp_refs(s, seq), hp_dtor_calls() - d0);
	while (hp_alive(s, seq))
		heap_unref(s);
	/* none: nothing to give back */
	arr = hp_array(AIN_ARRAY, -1);
	size_t live = live_slots();
	hp_hll("PushBack", 0, 0x20002, 3, (int[]){ arr, -1, 1 });
	XC_CHECK(live_slots() == live && hp_numof(arr) == 2 && hp_elem(arr, 0) == -1 && hp_elem(arr, 1) == 1,
		 "PushBack(0x20002) of none: live slots %zu -> %zu, element %d,%d", live, live_slots(), hp_elem(arr, 0), hp_elem(arr, 1));
	heap_unref(arr);
	printf("HP4 an option of an object without the wrap bit: %s\n", xc_failures ? "FAIL" : "ok");
}

static void hp5_borrowed(void)
{
	hp_ffi_setup();
	static const int ops[] = { 0x10002, 0x30002 };
	for (unsigned i = 0; i < 2; i++) {
		int arr = hp_array(AIN_ARRAY, -1);
		int s = alloc_struct(hp_dtor_struct);
		uint32_t seq = heap[s].seq;	/* .LOCALREF: the caller's one reference, nothing handed over */
		if (ops[i] == 0x30002)
			hp_hll("PushBack", 0, ops[i], 3, (int[]){ arr, s, 0 });
		else
			hp_hll("PushBack", 0, ops[i], 2, (int[]){ arr, s });
		XC_CHECK(hp_refs(s, seq) == 2, "PushBack(%#x) of a borrowed struct: %d reference(s), want 2 (the caller's and the array's)",
			 ops[i], hp_refs(s, seq));
		heap_unref(arr);
		XC_CHECK(hp_refs(s, seq) == 1, "after the array went (%#x) the caller's struct has %d reference(s), want 1",
			 ops[i], hp_refs(s, seq));
		while (hp_alive(s, seq))
			heap_unref(s);
	}
	/* an interface: [object, vtable offset] */
	{
		int arr = hp_array(AIN_ARRAY, -1);
		int s = alloc_struct(hp_dtor_struct);
		uint32_t seq = heap[s].seq;
		hp_hll("PushBack", 0, 0x10003, 3, (int[]){ arr, s, 0 });
		hp_hll("Insert", 0, 0x10003, 4, (int[]){ arr, 0, s, 0 });
		XC_CHECK(hp_refs(s, seq) == 3, "PushBack and Insert(0x10003) of a borrowed interface: %d reference(s), want 3 "
			 "(the caller's and the array's two)", hp_refs(s, seq));
		heap_unref(arr);
		XC_CHECK(hp_refs(s, seq) == 1, "after the interface array went the object has %d reference(s), want 1", hp_refs(s, seq));
		while (hp_alive(s, seq))
			heap_unref(s);
	}
	/* a plain value: a number that happens to equal a live slot is not touched */
	{
		int arr = hp_array(AIN_ARRAY_INT, -1);
		int s = alloc_struct(hp_dtor_struct);
		uint32_t seq = heap[s].seq;
		hp_hll("PushBack", 0, 1, 2, (int[]){ arr, s });
		XC_CHECK(hp_refs(s, seq) == 1, "PushBack(1) of a number equal to slot %d changed its count to %d", s, hp_refs(s, seq));
		heap_unref(arr);
		while (hp_alive(s, seq))
			heap_unref(s);
	}
	printf("HP5 borrowed wraps, interfaces, wrapped options and plain values: %s\n", xc_failures ? "FAIL" : "ok");
}

static void hp6_strings(void)
{
	hp_ffi_setup();
	/* a temporary (S_PUSH): one reference and the temporary flag */
	{
		int arr = hp_array(AIN_ARRAY_STRING, -1);
		int t = sf_str("hp-temp", -1);
		heap[t].ref |= HEAP_TEMP_FLAG;
		uint32_t seq = heap[t].seq;
		hp_hll("PushBack", 0, 2, 2, (int[]){ arr, t });
		XC_CHECK(hp_alive(t, seq) && heap[t].ref == (2 | HEAP_TEMP_FLAG) && hp_elem(arr, 0) == t,
			 "PushBack(2) of a temporary string: count word %#x, element %d; this branch keeps %#x (shared slot, flag kept)",
			 hp_alive(t, seq) ? (unsigned)heap[t].ref : 0u, hp_elem(arr, 0), 2u | HEAP_TEMP_FLAG);
		heap_unref(arr);
		XC_CHECK(hp_alive(t, seq) && HEAP_REF(t) == 1, "after the string array went the temporary has %d reference(s), want 1",
			 hp_refs(t, seq));
		while (hp_alive(t, seq))
			heap_unref(t);
	}
	/* an owned string (A_REF of a string member makes a temporary too; this
	 * one is the caller's) */
	{
		int arr = hp_array(AIN_ARRAY_STRING, -1);
		int o = sf_str("hp-owned", -1);
		uint32_t seq = heap[o].seq;
		hp_hll("PushBack", 0, 2, 2, (int[]){ arr, o });
		XC_CHECK(hp_refs(o, seq) == 2, "PushBack(2) of an owned string: %d reference(s); this branch keeps 2", hp_refs(o, seq));
		int left = hp_hll("IsExist", 0, 2, 2, (int[]){ arr, o });
		XC_CHECK(left == 1 && hp_refs(o, seq) == 2, "IsExist(2) of that string: %d reference(s); this branch keeps 2", hp_refs(o, seq));
		heap_unref(arr);
		XC_CHECK(hp_refs(o, seq) == 1, "after the array went the string has %d reference(s), want 1", hp_refs(o, seq));
		while (hp_alive(o, seq))
			heap_unref(o);
	}
	printf("HP6 strings keep this branch's handling: %s\n", xc_failures ? "FAIL" : "ok");
}

static void hp7_legacy(void)
{
	hp_ffi_setup();
	static const int versions[] = { 13, 11 };
	for (unsigned i = 0; i < 2; i++) {
		int arr = hp_array(AIN_ARRAY_STRUCT, hp_dtor_struct);
		uint32_t seq;
		int s = hp_handed(&seq);
		int saved = ain->version;
		ain->version = versions[i];
		hp_hll("PushBack", 0, 2, 2, (int[]){ arr, s });
		ain->version = saved;
		XC_CHECK(hp_refs(s, seq) == 3, "v%d PushBack(2) of a struct: %d reference(s); this branch's v11-v13 rule keeps 3 "
			 "(nothing given back)", versions[i], hp_refs(s, seq));
		heap_unref(arr);
		while (hp_alive(s, seq))
			heap_unref(s);
	}
	printf("HP7 v13 and v11 keep their rule: %s\n", xc_failures ? "FAIL" : "ok");
}

/* --- HP8: the producers of every type-2 value hll_param in this AIN --- */

struct hp_edge { uint32_t to; uint32_t from; };
static int hp_edge_cmp(const void *a, const void *b)
{
	const struct hp_edge *x = a, *y = b;
	return x->to < y->to ? -1 : x->to > y->to ? 1 : (x->from < y->from ? -1 : x->from > y->from);
}

static bool hp_is_value_param(const struct ain_hll_function *f)
{
	return f->nr_arguments >= 2 && f->arguments[f->nr_arguments - 1].type.data == AIN_HLL_PARAM;
}

static bool hp_handover_op(enum opcode op)
{
	switch (op) {
	case A_REF: case NEW: case S_PUSH: case S_ADD: case S_MOD: case CALLFUNC: case CALLMETHOD:
	case CALLHLL: case DG_CALL: case DG_NEW: case DG_NEW_FROM_METHOD: case CALLFUNC2:
		return true;
	default:
		return false;
	}
}

/* What a call producer may return for a type-2 value hll_param: a value
 * (an object the call's result owns), not a reference or a wrap. */
static bool hp_value_type(enum ain_data_type t)
{
	switch (t) {
	case AIN_STRING: case AIN_STRUCT: case AIN_ARRAY_TYPE: case AIN_ARRAY: case AIN_DELEGATE:
		return true;
	default:
		return false;
	}
}

/* The function a CALLMETHOD (instruction index k) calls when its method
 * number is pushed right before its arguments and every argument is a PUSH,
 * an S_PUSH or a local load (.LOCALREF); -1 otherwise, as for a method number
 * read from a vtable. */
static int hp_method_fno(const uint32_t *ips, size_t k)
{
	int n = LittleEndian_getDW(ain->code, ips[k] + 2);
	size_t at = k;
	for (int a = 0; a < n; a++) {
		if (at >= 3 && get_opcode(ips[at - 3]) == PUSHLOCALPAGE && get_opcode(ips[at - 2]) == PUSH
		    && get_opcode(ips[at - 1]) == X_REF && LittleEndian_getDW(ain->code, ips[at - 1] + 2) == 1)
			at -= 3;
		else if (at >= 1 && (get_opcode(ips[at - 1]) == PUSH || get_opcode(ips[at - 1]) == S_PUSH))
			at -= 1;
		else
			return -1;
	}
	if (at < 1 || get_opcode(ips[at - 1]) != PUSH)
		return -1;
	int fno = LittleEndian_getDW(ain->code, ips[at - 1] + 2);
	if (fno < 0 || fno >= ain->nr_functions || ain->functions[fno].nr_args != n)
		return -1;
	return fno;
}

/* The return type of the call producer at instruction index k, or AIN_VOID
 * with *resolved false when the code does not name its callee. */
static enum ain_data_type hp_call_result(const uint32_t *ips, size_t k, bool *resolved, int *callee_out)
{
	enum opcode op = get_opcode(ips[k]);
	int a0 = LittleEndian_getDW(ain->code, ips[k] + 2), a1 = LittleEndian_getDW(ain->code, ips[k] + 6);
	*resolved = true;
	*callee_out = a0;
	switch (op) {
	case CALLFUNC:
		if (a0 >= 0 && a0 < ain->nr_functions)
			return ain->functions[a0].return_type.data;
		break;
	case CALLHLL:
		if (a0 >= 0 && a0 < ain->nr_libraries && a1 >= 0 && a1 < ain->libraries[a0].nr_functions)
			return ain->libraries[a0].functions[a1].return_type.data;
		break;
	case DG_CALL:
		if (a0 >= 0 && a0 < ain->nr_delegates)
			return ain->delegates[a0].return_type.data;
		break;
	case CALLMETHOD: {
		int fno = hp_method_fno(ips, k);
		*callee_out = fno;
		if (fno >= 0)
			return ain->functions[fno].return_type.data;
		break;
	}
	default:
		break;
	}
	*resolved = false;
	return AIN_VOID;
}

static void hp8_static(void)
{
	xc_ready();
	/* premise of the fix's scope: an hll_param argument is only ever the last
	 * argument of an Array function */
	int lib = bu_lib("Array"), declared = 0;
	for (int l = 0; l < ain->nr_libraries; l++) {
		for (int f = 0; f < ain->libraries[l].nr_functions; f++) {
			struct ain_hll_function *fn = &ain->libraries[l].functions[f];
			for (int a = 0; a < fn->nr_arguments; a++) {
				if (fn->arguments[a].type.data != AIN_HLL_PARAM)
					continue;
				declared++;
				if (l != lib || a != fn->nr_arguments - 1 || !hp_is_value_param(fn)) {
					printf("  ENVIRONMENT: %s.%s takes an hll_param as argument %d of %d\n", ain->libraries[l].name,
					       fn->name, a + 1, fn->nr_arguments);
					xc_env_fail("an hll_param argument that is not an Array function's last");
				}
			}
		}
	}
	/* every instruction start, the address operands' sources */
	size_t n = 0, cap = 1 << 20;
	uint32_t *ips = xmalloc(cap * sizeof(*ips));
	for (uint32_t ip = 0; ip + 2 <= ain->code_size;) {
		enum opcode op = get_opcode(ip);
		int w = instruction_width(op);
		if (w <= 0)
			xc_env_fail("an instruction without a width");
		if (n == cap)
			ips = xrealloc(ips, (cap *= 2) * sizeof(*ips));
		ips[n++] = ip;
		ip += w;
	}
	/* jump sources by target: (target, source index) pairs */
	struct hp_edge *edges = NULL;
	size_t ne = 0, ecap = 0;
	for (size_t i = 0; i < n; i++) {
		enum opcode op = get_opcode(ips[i]);
		struct instruction *in = &instructions[op];
		for (int a = 0; a < in->nr_args; a++) {
			if (in->args[a] != T_ADDR)
				continue;
			if (ne == ecap)
				edges = xrealloc(edges, (ecap = ecap ? ecap * 2 : 4096) * sizeof(*edges));
			edges[ne++] = (struct hp_edge){ LittleEndian_getDW(ain->code, ips[i] + 2 + 4 * a), (uint32_t)i };
		}
	}
	qsort(edges, ne, sizeof(*edges), hp_edge_cmp);
	int sites = 0, producers = 0, bad = 0, unresolved = 0, options = 0, by_op[NR_OPCODES] = { 0 };
	int calls_named = 0, calls_virtual = 0, by_ret[128] = { 0 };
	for (size_t i = 1; i < n; i++) {
		if (get_opcode(ips[i]) != CALLHLL)
			continue;
		int l = LittleEndian_getDW(ain->code, ips[i] + 2), f = LittleEndian_getDW(ain->code, ips[i] + 6);
		int a3 = LittleEndian_getDW(ain->code, ips[i] + 10);
		if (l != lib || f < 0 || f >= ain->libraries[l].nr_functions || !hp_is_value_param(&ain->libraries[l].functions[f]))
			continue;
		if (a3 == 0x20002)
			options++;	/* two slots; reported, not traced (see the header) */
		if (a3 != 2)
			continue;
		sites++;
		if (!libraries[l] || !libraries[l][f].fun) {
			bad++;
			printf("  HP8 site %#x: Array.%s has no binding (its arguments would be dropped unreleased)\n", ips[i],
			       ain->libraries[l].functions[f].name);
		}
		/* a worklist of instruction indices whose stack top is the argument;
		 * each entry is a producer found (k) or a place to look before (at) */
		enum { HP_WORK = 64 };
		size_t work[HP_WORK], prods[HP_WORK]; int nw = 0, np = 0, found = 0, guard = 0;
		work[nw++] = i;
		for (; nw > 0 && guard < HP_WORK; guard++) {
			size_t at = work[--nw];
			/* jumps to this address */
			size_t lo = 0, hi = ne;
			while (lo < hi) {
				size_t mid = (lo + hi) / 2;
				if (edges[mid].to < ips[at]) lo = mid + 1; else hi = mid;
			}
			for (size_t e = lo; e < ne && edges[e].to == ips[at]; e++) {
				enum opcode jop = get_opcode(ips[edges[e].from]);
				if (jop == JUMP) {
					if (nw == HP_WORK)
						xc_env_fail("HP8's worklist is full");
					work[nw++] = edges[e].from;
				} else if (jop == DG_CALL) {
					if (np == HP_WORK)
						xc_env_fail("HP8's producer list is full");
					prods[np++] = edges[e].from;
				} else {
					bad++;
					printf("  HP8 site %#x: reached from a conditional jump at %#x (%s)\n", ips[i], ips[edges[e].from],
					       instructions[jop].name);
				}
			}
			enum opcode prev = get_opcode(ips[at - 1]);
			if (prev == JUMP || prev == RETURN || prev == FUNC || prev == ENDFUNC)
				continue;	/* no fall-through into here */
			if (np == HP_WORK)
				xc_env_fail("HP8's producer list is full");
			prods[np++] = at - 1;
		}
		if (nw > 0)
			xc_env_fail("HP8 gave up following the jumps to a site");
		for (int p = 0; p < np; p++) {
			size_t k = prods[p];
			enum opcode op = get_opcode(ips[k]);
			producers++; found++;
			by_op[op]++;
			if (!hp_handover_op(op)) {
				bad++;
				printf("  HP8 site %#x: argument made by %s at %#x\n", ips[i], instructions[op].name, ips[k]);
				continue;
			}
			if (op != CALLFUNC && op != CALLHLL && op != DG_CALL && op != CALLMETHOD && op != CALLFUNC2)
				continue;
			bool resolved;
			int callee;
			enum ain_data_type rt = hp_call_result(ips, k, &resolved, &callee);
			if (!resolved) {
				calls_virtual++;
				printf("  HP8 site %#x: the callee of %s at %#x is not named in the code (not resolved)\n", ips[i],
				       instructions[op].name, ips[k]);
				continue;
			}
			calls_named++;
			if ((int)rt >= 0 && (int)rt < (int)(sizeof by_ret / sizeof *by_ret))
				by_ret[rt]++;
			if (!hp_value_type(rt)) {
				bad++;
				printf("  HP8 site %#x: %s at %#x (callee %d) returns type %d, not a value\n", ips[i], instructions[op].name,
				       ips[k], callee, (int)rt);
			}
		}
		if (!found) {
			unresolved++;
			printf("  HP8 site %#x: no producer found\n", ips[i]);
		}
	}
	printf("HP8 %d hll_param argument(s) declared, each an Array function's last; %d Array call site(s) pass a value hll_param "
	       "with type operand 2; %d producer(s):", declared, sites, producers);
	for (int op = 0; op < NR_OPCODES; op++)
		if (by_op[op])
			printf(" %s %d", instructions[op].name, by_op[op]);
	printf("; %d site(s) with 0x20002 not traced\n", options);
	printf("HP8 call producers: %d with a named callee, returning", calls_named);
	for (int t = 0; t < (int)(sizeof by_ret / sizeof *by_ret); t++)
		if (by_ret[t])
			printf(" type %d x%d", t, by_ret[t]);
	printf("; %d whose callee is not named (not resolved)\n", calls_virtual);
	XC_CHECK(sites > 0 && calls_named > 0, "no such call site or call producer: the premise cannot be checked");
	XC_CHECK(!bad && !unresolved, "%d producer(s) that do not hand a reference over, %d site(s) without a producer", bad, unresolved);
	free(edges);
	free(ips);
}

static int test_hll_param_handover(void)
{
	static const struct xc_case cases[] = {
		{ "HP1", hp1_card }, { "HP2", hp2_struct }, { "HP3", hp3_pages }, { "HP4", hp4_option },
		{ "HP5", hp5_borrowed }, { "HP6", hp6_strings }, { "HP7", hp7_legacy }, { "HP8", hp8_static },
	};
	return xc_run("HLL-PARAM-HANDOVER", cases, sizeof cases / sizeof *cases, 120);
}
