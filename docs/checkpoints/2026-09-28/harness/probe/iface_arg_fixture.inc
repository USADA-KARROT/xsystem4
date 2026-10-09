/* iface-arg: an interface argument (AIN_IFACE, type 89, two slots [object,
 * vtable offset]), and an option argument holding a wrap/interface value,
 * keep their own reference for as long as the callee's local page holds them.
 *
 * v14 bytecode passes interface arguments borrowed: the caller pushes the pair
 * with X_REF 2 and keeps any temporary alive in a <dummy> local that it
 * releases with .LOCALDELETE after the call (AdvStand@MoveIn/Move/MoveOut ->
 * Motion::Create(IParts) -> NEW Motion::Executer(IParts)). The callee's local
 * page releases every AIN_IFACE/AIN_OPTION variable when it is freed (page.c
 * variable_fini), and named interface locals are assigned with SP_INC and never
 * .LOCALDELETE'd, so the page teardown is the owner. The original call routine
 * (0x66a020) moves the arguments into the new page with 0x657430 (call at
 * 0x66a082); its type table (0x657508 -> 0x6574fc) retains the value with
 * 0x679f10 (inc [page+0x1c]) for types 18-21, 51, 67, 80, 82 (WRAP), 87,
 * 89 (IFACE) and 93, and stores strings, structs, delegates and arrays without
 * a retain (the caller hands those over with A_REF). An option (86) that holds
 * a value (discriminant slot 0) is dispatched on its payload type (0x6535e0).
 * The typed copy/release pair 0x656d60 / 0x656c10 treats 89 the same way.
 *
 * Before the fix function_call (CALLFUNC/CALLFUNC2/CALLMETHOD/NEW) retained
 * REF/STRUCT/DELEGATE/ARRAY/WRAP arguments but not AIN_IFACE or an option's
 * wrap payload, and delegate_copy_argument retained only REF/WRAP. Every such
 * call therefore dropped one reference of a borrowed object. In the game
 * AdvStand@MoveIn calls Motion::Create, whose IParts parameter and the one of
 * Motion::Executer@0 each drop one: the sprite held by AdvStand.m_parent is
 * freed on its first entrance, its slot is reused (for example by a string),
 * later MoveOut/Move read a vtable from the reused slot, get function number
 * -1 and the VM skips the call, so stands never leave or move.
 *
 * Cases (each in its own fork so a use-after-free cannot hide the others):
 *   IA1 CALLFUNC path: real AIN activity::detail::CUserComponentActivity::
 *       ReleaseComponent(IUserComponent) (body RETURN) must leave the
 *       borrowed object's refcount unchanged.
 *   IA2 CALLMETHOD path + member ownership: real CUserComponentSet@
 *       UserComponent::set(IUserComponent) stores the argument with SP_INC;
 *       after the caller drops its own reference the member must still own a
 *       live object, and releasing the set must release it exactly once.
 *   IA3 delegate path: DG_ReleaseUserComponentHandler(IUserComponent) invoking
 *       the same real function through delegate_call.
 *   IA4 AdvStand: real constructor (AFL_Parts_CreateSprite, NEW AdvStandImage),
 *       then AdvStand@MoveIn, @Move, @MoveOut. After each call the object in
 *       m_parent must be the same live CSpriteParts with refcount 1 and its
 *       vtable entry vtoff+33 must still resolve to CSpriteParts@Core::get.
 *       Releasing the stand then releases the sprite.
 *   IA5 option argument: real CustomerEffect@Personality::set(
 *       option<wrap<Personality>>) stores the value with X_OP_SET; the
 *       caller's own reference and the member's must both remain.
 *   IA7 HLL callback option argument: Array.Where (arg3 196610, the game's
 *       ItemStock@ToItem call shape) calls the real lambda
 *       ItemStock@<lambda : ItemStock@ToItem()(52, 20)>(option<wrap<Item>>)
 *       through vm_call_nopop; the element's refcount must balance (caller +
 *       array + the result's copy). IA6 below covers delegate_call only.
 *   IA6 delegate option argument: <ArrayFindFunc@Item&?>(option<wrap<Item>>)
 *       invoking the real lambda ItemStock@<lambda : ItemStock@ToItem()>
 *       (OptionalExtensions::HasValue) through delegate_call must return true
 *       and leave the borrowed Item's refcount unchanged.
 *
 * Only static VM functions that exist before the fix are used (function_call,
 * method_call, delegate_call, vm_execute), so before-check.sh can build it.
 * Needs no GL, assets or globals beyond PartsEngine's _PostLink registration.
 */
#include <sys/wait.h>
#include <unistd.h>
extern void probe_init_parts_table(void);

/* Records every decrease of one watched slot with the instruction just executed. */
static int ia_watch = -1, ia_last_ref, ia_nevents;
static unsigned ia_watch_seq;
static char ia_events[8][200];
static size_t ia_prev_ip;
static int ia_prev_op = -1, ia_prev_fno = -1;

static const char *ia_fname(int fno)
{
	return fno >= 0 && fno < ain->nr_functions ? ain->functions[fno].name : "?";
}

static void iface_arg_probe_step(void)
{
	int fno = call_stack_ptr > 0 ? call_stack[call_stack_ptr-1].fno : -1;
	if (ia_watch > 0) {
		int r = HEAP_REF(ia_watch);
		bool gone = heap[ia_watch].seq != ia_watch_seq;
		if (r < ia_last_ref || (gone && ia_last_ref > 0)) {
			if (ia_nevents < 8)
				snprintf(ia_events[ia_nevents], sizeof(ia_events[0]), "ref %d->%d%s after %s at 0x%zx in %s",
					 ia_last_ref, r, gone ? " (slot released)" : "",
					 ia_prev_op >= 0 ? instructions[ia_prev_op].name : "-", ia_prev_ip, ia_fname(ia_prev_fno));
			ia_nevents++;
		}
		ia_last_ref = gone ? 0 : r;
	}
	ia_prev_ip = instr_ptr;
	ia_prev_op = get_opcode(instr_ptr);
	ia_prev_fno = fno;
}

static void ia_watch_begin(int slot)
{
	ia_watch = slot;
	ia_watch_seq = heap[slot].seq;
	ia_last_ref = HEAP_REF(slot);
	ia_nevents = 0;
}

static void ia_dump_events(void)
{
	for (int i = 0; i < ia_nevents && i < 8; i++)
		printf("    watched slot %d: %s\n", ia_watch, ia_events[i]);
}

/* The watched object is still the one that was allocated: same generation, a page. */
static bool ia_same_object(int slot, unsigned seq)
{
	return HEAP_REF(slot) > 0 && heap[slot].seq == seq && heap[slot].type == VM_PAGE && heap[slot].page;
}

static int ia_failures;
#define IA_CHECK(c, ...) do { if (!(c)) { printf("  FAIL: "); printf(__VA_ARGS__); printf("\n"); ia_dump_events(); ia_failures++; } } while (0)

static int ia_fn(const char *name)
{
	int f = ain_get_function(ain, (char *)name);
	if (f < 0) { printf("IFACE-ARG %s not declared\n", name); exit(2); }
	return f;
}

static int ia_object(void)
{
	int st = ain_get_struct(ain, "CASPos");
	assert(st >= 0);
	int obj = vm_construct_struct(st);
	assert(obj > 0 && HEAP_REF(obj) == 1);
	return obj;
}

/* Runs fno with the given argument slots, as CALLFUNC (obj < 0) or CALLMETHOD. */
static void ia_call(int fno, int obj, int nargs, const int *args)
{
	int sp = stack_ptr, csp = call_stack_ptr;
	size_t ip = instr_ptr;
	if (obj >= 0)
		stack_push(obj);
	for (int i = 0; i < nargs; i++)
		stack_push(args[i]);
	if (obj >= 0)
		method_call(fno, VM_RETURN);
	else
		function_call(fno, VM_RETURN);
	vm_execute();
	instr_ptr = ip;
	IA_CHECK(call_stack_ptr == csp, "%s: call stack %d != %d", ia_fname(fno), call_stack_ptr, csp);
	int ret = ain_return_slots_type(&ain->functions[fno].return_type);
	IA_CHECK(stack_ptr == sp + ret, "%s: stack %d != %d", ia_fname(fno), stack_ptr, sp + ret);
	stack_ptr = sp;
}

static void ia_callfunc(void)
{
	int fno = ia_fn("activity::detail::CUserComponentActivity::ReleaseComponent");
	assert(ain->functions[fno].nr_args == 2 && ain->functions[fno].vars[0].type.data == AIN_IFACE);
	int obj = ia_object();
	unsigned seq = heap[obj].seq;
	ia_watch_begin(obj);
	int args[] = { obj, 0 };
	ia_call(fno, -1, 2, args);
	IA_CHECK(ia_same_object(obj, seq) && HEAP_REF(obj) == 1,
		 "IA1 CALLFUNC ReleaseComponent(IUserComponent): borrowed argument ref=%d (want 1)%s",
		 HEAP_REF(obj), ia_same_object(obj, seq) ? "" : ", object released");
	if (ia_same_object(obj, seq))
		heap_unref(obj);
	printf("IA1 CALLFUNC path: ref after call %s\n", ia_failures ? "dropped" : "unchanged");
}

static void ia_callmethod_member(void)
{
	int fno = ia_fn("CUserComponentSet@UserComponent::set");
	int st = ain_get_struct(ain, "CUserComponentSet");
	assert(st >= 0);
	int set = vm_construct_struct(st);
	assert(set > 0 && heap[set].page && heap[set].page->nr_vars >= 6);
	int obj = ia_object();
	unsigned seq = heap[obj].seq;
	ia_watch_begin(obj);
	int args[] = { obj, 0 };
	ia_call(fno, set, 2, args);
	IA_CHECK(heap[set].page->values[4].i == obj, "IA2 member UserComponent=%d, want %d", heap[set].page->values[4].i, obj);
	IA_CHECK(ia_same_object(obj, seq) && HEAP_REF(obj) == 2,
		 "IA2 CALLMETHOD UserComponent::set: ref=%d after the call (want 2: caller + member)", HEAP_REF(obj));
	heap_unref(obj);	/* the caller's own reference */
	IA_CHECK(ia_same_object(obj, seq) && HEAP_REF(obj) == 1,
		 "IA2 member still holds slot %d but the object was released (ref=%d)",
		 obj, HEAP_REF(obj));
	bool alive = ia_same_object(obj, seq);
	heap_unref(set);
	if (alive)
		IA_CHECK(HEAP_REF(obj) == 0 && heap[obj].seq != seq,
			 "IA2 releasing the set left the member object alive (ref=%d)", HEAP_REF(obj));
	printf("IA2 CALLMETHOD path: member keeps its own reference: %s\n", alive ? "yes" : "no");
}

static void ia_delegate(void)
{
	int fno = ia_fn("activity::detail::CUserComponentActivity::ReleaseComponent");
	int dg = -1;
	for (int i = 0; i < ain->nr_delegates; i++)
		if (!strcmp(ain->delegates[i].name, "activity::detail::DG_ReleaseUserComponentHandler"))
			dg = i;
	assert(dg >= 0 && ain->delegates[dg].variables[0].type.data == AIN_IFACE);
	int obj = ia_object();
	unsigned seq = heap[obj].seq;
	int dgslot = heap_alloc_page(delegate_new_from_method(-1, fno));
	ia_watch_begin(obj);
	/* The stack DG_CALLBEGIN leaves for a void handler: [arg pair, dg page, index].
	 * delegate_call returns to instr_ptr + width(DG_CALL); point that at VM_RETURN. */
	int sp = stack_ptr, csp = call_stack_ptr;
	size_t ip = instr_ptr;
	stack_push(obj);
	stack_push(0);
	stack_push(dgslot);
	stack_push(0);
	instr_ptr = VM_RETURN - instruction_width(DG_CALL);
#ifdef DELEGATE_CALL_STATE_IN_PAGE	/* v14: DG_CALLBEGIN also starts the call in the delegate (0x66daf3) */
	delegate_call_begin(heap_get_delegate_page(dgslot));
#endif
	delegate_call(dg, 0);
	vm_execute();
	instr_ptr = ip;
	IA_CHECK(call_stack_ptr == csp && stack_ptr == sp + 4 && stack[stack_ptr-1].i == 1,
		 "IA3 delegate frame: csp=%d/%d sp=%d/%d index=%d", call_stack_ptr, csp, stack_ptr, sp + 4, stack[stack_ptr-1].i);
	stack_ptr = sp;
	IA_CHECK(ia_same_object(obj, seq) && HEAP_REF(obj) == 1,
		 "IA3 delegate DG_ReleaseUserComponentHandler(IUserComponent): borrowed argument ref=%d (want 1)%s",
		 HEAP_REF(obj), ia_same_object(obj, seq) ? "" : ", object released");
	if (ia_same_object(obj, seq))
		heap_unref(obj);
	heap_unref(dgslot);
	printf("IA3 delegate path checked\n");
}

static void ia_stand_check(const char *step, int stand, int sprite, unsigned seq, int sprite_type, int core_get)
{
	struct page *sp = heap[stand].page;
	IA_CHECK(sp->values[3].i == sprite, "IA4 %s: m_parent changed to %d", step, sp->values[3].i);
	bool same = ia_same_object(sprite, seq) && heap[sprite].page->index == sprite_type;
	IA_CHECK(same && HEAP_REF(sprite) == 1,
		 "IA4 %s: sprite held by AdvStand.m_parent: ref=%d (want 1) type=%d page_index=%d (want CSpriteParts %d)%s",
		 step, HEAP_REF(sprite), heap[sprite].type,
		 heap[sprite].type == VM_PAGE && heap[sprite].page ? heap[sprite].page->index : -1, sprite_type,
		 ia_same_object(sprite, seq) ? "" : ", slot released or reused");
	if (!same)
		return;
	/* MoveOut/Move/IsMotion read <vtable>[vtoff + 33] of m_parent (ISpriteParts@Core::get). */
	int vt = heap[sprite].page->values[0].i, vtoff = sp->values[4].i, fno = -1;
	if (vt > 0 && heap_index_valid(vt) && heap[vt].page && vtoff + 33 < heap[vt].page->nr_vars)
		fno = heap[vt].page->values[vtoff + 33].i;
	IA_CHECK(fno == core_get, "IA4 %s: m_parent vtable[vtoff+33] = %d, want CSpriteParts@Core::get %d", step, fno, core_get);
}

static void ia_stand(void)
{
	probe_init_parts_table();
	extern struct static_library lib_PartsEngine;
	for (int i = 0; lib_PartsEngine.functions[i].name; i++) {
		if (!strcmp(lib_PartsEngine.functions[i].name, "_PostLink")) {
			((void (*)(void))lib_PartsEngine.functions[i].fun)();
			break;
		}
	}
	int st = ain_get_struct(ain, "AdvStand"), sprite_type = ain_get_struct(ain, "parts::detail::CSpriteParts");
	assert(st >= 0 && sprite_type >= 0);
	int core_get = ia_fn("parts::detail::CSpriteParts@Core::get");
	int stand = vm_construct_struct(st);	/* NEW AdvStand 31625: AFL_Parts_CreateSprite into m_parent */
	assert(stand > 0 && heap[stand].page && heap[stand].page->nr_vars == 6);
	int sprite = heap[stand].page->values[3].i;
	IA_CHECK(sprite > 0 && heap_index_valid(sprite) && heap[sprite].type == VM_PAGE && heap[sprite].page,
		 "IA4 constructor left m_parent=%d", sprite);
	if (ia_failures)
		return;
	unsigned seq = heap[sprite].seq;
	ia_stand_check("after constructor", stand, sprite, seq, sprite_type, core_get);
	ia_watch_begin(sprite);
	static const struct { const char *fn; int arg; } steps[] = {
		{ "AdvStand@MoveIn", 1 }, { "AdvStand@Move", 3 }, { "AdvStand@MoveOut", 1 },
	};
	for (int i = 0; i < 3; i++) {
		int fno = ia_fn(steps[i].fn), arg = steps[i].arg;
		ia_call(fno, stand, 1, &arg);
		char label[64];
		snprintf(label, sizeof(label), "after %s", steps[i].fn);
		ia_stand_check(label, stand, sprite, seq, sprite_type, core_get);
		printf("IA4 %s: sprite slot %d ref=%d\n", label, sprite, ia_same_object(sprite, seq) ? HEAP_REF(sprite) : 0);
	}
	bool alive = ia_same_object(sprite, seq);
	heap_unref(stand);
	if (alive)
		IA_CHECK(heap[sprite].seq != seq, "IA4 releasing the stand left its sprite alive (ref=%d)", HEAP_REF(sprite));
}

static void ia_option(void)
{
	int fno = ia_fn("CustomerEffect@Personality::set");
	assert(ain->functions[fno].nr_args == 2 && ain->functions[fno].vars[0].type.data == AIN_OPTION);
	int ce_type = ain_get_struct(ain, "CustomerEffect"), pe_type = ain_get_struct(ain, "Personality");
	assert(ce_type >= 0 && pe_type >= 0);
	int ce = alloc_struct(ce_type), pe = alloc_struct(pe_type);	/* members only; constructors are not needed */
	assert(ce > 0 && pe > 0 && heap[ce].page->nr_vars >= 4 && HEAP_REF(pe) == 1);
	unsigned seq = heap[pe].seq;
	ia_watch_begin(pe);
	int args[] = { pe, 0 };	/* option<wrap<Personality>> holding pe: [value, discriminant 0] */
	ia_call(fno, ce, 2, args);
	IA_CHECK(heap[ce].page->values[2].i == pe && heap[ce].page->values[3].i == 0,
		 "IA5 member <Personality> = [%d, %d], want [%d, 0]", heap[ce].page->values[2].i, heap[ce].page->values[3].i, pe);
	IA_CHECK(ia_same_object(pe, seq) && HEAP_REF(pe) == 2,
		 "IA5 CALLMETHOD Personality::set(option<wrap<Personality>>): ref=%d after the call (want 2: caller + member)%s",
		 HEAP_REF(pe), ia_same_object(pe, seq) ? "" : ", object released");
	heap_unref(pe);	/* the caller's own reference */
	bool alive = ia_same_object(pe, seq);
	IA_CHECK(alive && HEAP_REF(pe) == 1, "IA5 member still holds slot %d but the object was released (ref=%d)", pe, HEAP_REF(pe));
	heap_unref(ce);
	if (alive)
		IA_CHECK(HEAP_REF(pe) == 0 && heap[pe].seq != seq, "IA5 releasing CustomerEffect left the member object alive (ref=%d)", HEAP_REF(pe));
	printf("IA5 option argument path: member keeps its own reference: %s\n", alive ? "yes" : "no");
}

static void ia_delegate_option(void)
{
	int fno = ia_fn("ItemStock@<lambda : ItemStock@ToItem()(52, 20)>");
	int dg = -1;
	for (int i = 0; i < ain->nr_delegates; i++)
		if (!strcmp(ain->delegates[i].name, "<ArrayFindFunc@Item&?>"))
			dg = i;
	assert(dg >= 0 && ain->delegates[dg].variables[0].type.data == AIN_OPTION);
	int item_type = ain_get_struct(ain, "Item");
	assert(item_type >= 0);
	int item = alloc_struct(item_type);
	unsigned seq = heap[item].seq;
	int dgslot = heap_alloc_page(delegate_new_from_method(-1, fno));
	ia_watch_begin(item);
	/* [option value, discriminant 0, dg page, index, bool return placeholder] */
	int sp = stack_ptr, csp = call_stack_ptr;
	size_t ip = instr_ptr;
	stack_push(item);
	stack_push(0);
	stack_push(dgslot);
	stack_push(0);
	stack_push(0);
	instr_ptr = VM_RETURN - instruction_width(DG_CALL);
#ifdef DELEGATE_CALL_STATE_IN_PAGE	/* v14: DG_CALLBEGIN also starts the call in the delegate (0x66daf3) */
	delegate_call_begin(heap_get_delegate_page(dgslot));
#endif
	delegate_call(dg, 0);
	vm_execute();
	instr_ptr = ip;
	IA_CHECK(call_stack_ptr == csp && stack_ptr == sp + 5 && stack[stack_ptr-1].i == 1,
		 "IA6 delegate frame: csp=%d/%d sp=%d/%d result=%d (want HasValue true)",
		 call_stack_ptr, csp, stack_ptr, sp + 5, stack_ptr > sp ? stack[stack_ptr-1].i : -99);
	stack_ptr = sp;
	IA_CHECK(ia_same_object(item, seq) && HEAP_REF(item) == 1,
		 "IA6 delegate <ArrayFindFunc@Item&?>(option<wrap<Item>>): borrowed value ref=%d (want 1)%s",
		 HEAP_REF(item), ia_same_object(item, seq) ? "" : ", object released");
	if (ia_same_object(item, seq))
		heap_unref(item);
	heap_unref(dgslot);
	printf("IA6 delegate option path checked\n");
}

static void ia_hll_option(void)
{
	int lib = fo_find_lib("Array");
	int fn = fo_find_fn(lib, "Where", 2, 1, 0);
	int fno = ain_get_function(ain, "ItemStock@<lambda : ItemStock@ToItem()(52, 20)>");
	int item_type = ain_get_struct(ain, "Item");
	if (lib < 0 || fn < 0 || fno < 0 || item_type < 0 || ain->functions[fno].vars[0].type.data != AIN_OPTION) {
		printf("  IA7 setup: lib=%d Where=%d lambda=%d Item=%d\n", lib, fn, fno, item_type);
		ia_failures++;
		return;
	}
	int item = alloc_struct(item_type);
	unsigned seq = heap[item].seq;
	struct page *arr = alloc_page(ARRAY_PAGE, AIN_ARRAY, 1);
	arr->array.rank = 1;
	arr->values[0].i = item;
	heap_ref(item); /* the array's own reference */
	int arr_slot = heap_alloc_page(arr);
	int before = heap[item].ref;
	int sp = stack_ptr;
	stack_push(arr_slot); stack_push(-1); stack_push(fno);
	hll_call(lib, fn, 196610);
	int res = stack_pop().i;
	int copies = 0;
	if (res > 0 && heap_index_valid(res) && heap[res].type == VM_PAGE && heap[res].page)
		for (int i = 0; i < heap[res].page->nr_vars; i++)
			if (heap[res].page->values[i].i == item)
				copies++;
	bool same = heap[item].seq == seq;
	int after = heap[item].ref;
	if (!same || after != before + copies || stack_ptr != sp) {
		printf("  IA7 item ref before=%d after=%d result copies=%d same_object=%d stack_ok=%d (want after=%d)\n",
		       before, after, copies, same, stack_ptr == sp, before + copies);
		ia_failures++;
	}
}

static int ia_run(const char *name, void (*fn)(void))
{
	fflush(stdout);
	pid_t pid = fork();
	if (pid == 0) {
		ia_failures = 0;
		fn();
		fflush(stdout);
		_exit(ia_failures ? 1 : 0);
	}
	int status = 0;
	waitpid(pid, &status, 0);
	bool ok = WIFEXITED(status) && WEXITSTATUS(status) == 0;
	printf("%s %s%s\n", name, ok ? "PASS" : "FAIL", WIFSIGNALED(status) ? " (crashed)" : "");
	fflush(stdout);
	return ok ? 0 : 1;
}

static int test_iface_arg(void)
{
	int failed = 0;
	failed += ia_run("IA1 CALLFUNC borrowed interface argument", ia_callfunc);
	failed += ia_run("IA2 CALLMETHOD interface argument stored in a member", ia_callmethod_member);
	failed += ia_run("IA3 delegate interface argument", ia_delegate);
	failed += ia_run("IA4 AdvStand m_parent across MoveIn/Move/MoveOut", ia_stand);
	failed += ia_run("IA5 option<wrap<T>> argument stored in a member", ia_option);
	failed += ia_run("IA6 delegate option<wrap<T>> argument", ia_delegate_option);
	failed += ia_run("IA7 HLL callback option<wrap<T>> argument (Array.Where)", ia_hll_option);
	printf("IFACE-ARG summary: %d/7 case(s) failed\n", failed);
	return failed ? 89 : 0;
}
