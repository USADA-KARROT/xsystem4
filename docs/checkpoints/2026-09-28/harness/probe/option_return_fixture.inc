/* option-return: a function that returns an option of an interface wrap
 * returns three slots.
 *
 * Original (static disassembly). RETURN (opcode 0x2f, 0x66cb20) pops the
 * frame, releases the local page (0x66ada0) and sets the return address; it
 * neither reads nor moves the value stack and never looks at the function's
 * return type. Whatever the callee left on the stack is the result. The
 * slots of a type are those of 0x653420: an option is the slots of its
 * payload plus one, and a wrap of an interface wrap is two (0x6537e0), so
 * option<wrap<iwrap<T>>> is (object, vtable offset, flag) with none as
 * (-1, -1, 1). The bytecode agrees: PlayerAction@InnerAction::get ends in
 * "X_REF 3; ...; RETURN", AchievementCollection@Create in "PUSH 0; PUSH 0;
 * X_MOV 3 2; ...; RETURN", and their callers store the result with
 * "X_MOV 5 2; X_ASSIGN 3".
 *
 * This VM checks the stack on return (function_return) against a slot count
 * it derives from the return type. That count was two for every option, so
 * the object of a three-slot result was dropped, the caller's X_ASSIGN 3
 * took one slot too many, and each call left the caller's stack one slot
 * short. AchievementCollection@LoadAchievementsFromEx does it once per
 * achievement at startup, as soon as MainEXFile.GetNodeNameList returns the
 * achievement names (ex-name-list): the stack underflows and the game never
 * reaches its first message.
 *
 *   OR1  The real AchievementCollection@LoadAchievementsFromEx on the work
 *        copy's EX file: the caller's stack is intact, and the collection
 *        holds one achievement object per node of the EX tree the function
 *        names, as two-slot elements.
 *   OR2  The real AchievementCollection@Create (a method with an argument):
 *        three slots (object, 0, 0) for the first achievement, with the
 *        object the caller's alone, and (-1, -1, 1) for a name that is not
 *        there.
 *   OR3  The real PlayerAction@InnerAction::get (36 direct calls in the
 *        battle code) returns the member's three slots and retains the
 *        object; none comes back as (-1, -1, 1).
 *   OR4  A call that cannot be made (the object is gone) leaves three
 *        slots as well.
 *   OR5  The 13 functions declared to return such an option are counted as
 *        three slots, and they are the only ones.
 *   OR6  PlayerAction@InnerAction::get entered with one more value on the
 *        stack than it should have: function_return drops the extra value
 *        and keeps all three result slots.
 *   G1   Every other function has the slot count it had: the numbers of
 *        functions with 0, 1 and 2 result slots are those recorded before
 *        this change, and every option whose payload is one slot is two.
 *        No delegate returns a three-slot option: the 64 that return an
 *        option are two slots, as before.
 *   G2   An AIN older than version 14 returns one slot whatever the type.
 *   G3   The real WorkerCollection@GetShowCaseWorker (an option of a plain
 *        wrap) still returns two slots, with and without a value.
 *
 * Where the expectations are xsystem4's, not the original's:
 *   - OR4: natively a call on a missing object is an error; this VM pushes
 *     a default result of the declared size and goes on. Its last slot is
 *     0, which a caller reads as "has a value" (none is 1); that is how a
 *     two-slot option was already answered and is not changed here. Only
 *     the number of slots is checked.
 *   - OR6: the original RETURN leaves the stack as it is, so a value the
 *     callee did not consume would stay there. Trimming it is this VM's
 *     own guard; the case checks that the guard moves three slots.
 *   - Functions returning wrap<iwrap<T>> (483 of them) are two slots
 *     natively and counted as one here; function_return does not check
 *     wrap results, so they are not cut. G1 keeps that as it is.
 *   - No delegate returns a three-slot option in this AIN (G1 checks the
 *     declarations), so neither DG_CALL's result buffer nor the delegate
 *     branch of function_return's trimming is run with three slots; OR6
 *     covers the trimming of an ordinary call only.
 *   - OR1 needs the achievement names, that is ex-name-list's fix of
 *     MainEXFile.GetNodeNameList: with an empty list the collection stays
 *     empty and the case fails for that reason. The two fixes go together.
 *
 * Every case runs in a child process. Exit code 1 is a failed check, exit
 * code 2 the environment: XS4_GAME has no EX file, the EX file lacks the
 * achievement tree, or the AIN lacks a function, struct or member a case
 * looks up. A signal, a sanitizer abort or the instruction budget running
 * out is that case failing.
 */
#include <limits.h>
#include <sys/wait.h>
#include <unistd.h>

static int or_failures;
#define OR_CHECK(c, ...) do { if (!(c)) { printf("  FAIL: "); printf(__VA_ARGS__); printf("\n"); fflush(stdout); or_failures++; } } while (0)

static void or_collect(void)
{
	or_failures += da_failures + wc_failures + oa_failures + bu_failures;
	da_failures = wc_failures = oa_failures = bu_failures = 0;
}

/* The slots the declaration of a function result has natively (0x653420),
 * for the types a result has in this AIN. */
static bool or_is_three(struct ain_type *t)
{
	if (t->data != AIN_OPTION)
		return false;
	const struct ain_type *in = t;
	while (in && in->data == AIN_OPTION)
		in = in->array_type;
	if (!in)
		return false;
	if (in->data == AIN_IFACE)
		return true;
	return in->data == AIN_WRAP && in->array_type
		&& (in->array_type->data == AIN_IFACE_WRAP || in->array_type->data == AIN_INT
		    || in->array_type->data == AIN_FLOAT || in->array_type->data == AIN_BOOL
		    || in->array_type->data == AIN_ENUM || in->array_type->data == AIN_WRAP
		    || in->array_type->data == AIN_OPTION);
}

/* Sentinels under a call: the callee must leave them alone. */
enum { OR_GUARDS = 6, OR_GUARD = 0x5EED0000 };
static int or_guard_push(void)
{
	int sp = stack_ptr;
	for (int i = 0; i < OR_GUARDS; i++)
		stack_push(OR_GUARD + i);
	return sp;
}

/* Checks the sentinels and that exactly want slots are above them; copies
 * the slots, restores the stack and returns how many there were. */
static int or_guard_pop(const char *what, int sp, int want, int *out)
{
	int n = stack_ptr - (sp + OR_GUARDS);
	bool intact = stack_ptr >= sp + OR_GUARDS;
	for (int i = 0; intact && i < OR_GUARDS; i++)
		intact = stack[sp + i].i == OR_GUARD + i;
	OR_CHECK(intact, "%s: the caller's stack below the call was changed (%d slot(s) above it)", what, n);
	OR_CHECK(n == want, "%s: %d result slot(s), want %d", what, n, want);
	for (int i = 0; out && i < want; i++)
		out[i] = i < n && n <= 8 ? stack[sp + OR_GUARDS + i].i : -999;
	stack_ptr = sp;
	return n;
}

/* object.fno(args...) the way CALLMETHOD enters it: the object under the
 * arguments. A string argument that is not a temporary stays the caller's. */
static void or_method(int fno, int object, int nargs, const int *args)
{
	size_t ip = instr_ptr;
	int csp = call_stack_ptr;
	stack_push(object);
	for (int i = 0; i < nargs; i++)
		stack_push(args[i]);
	method_call(fno, VM_RETURN);
	vm_execute();
	instr_ptr = ip;
	OR_CHECK(call_stack_ptr == csp, "%s: call stack %d != %d", ain->functions[fno].name, call_stack_ptr, csp);
}

static struct ex *or_real_ex(void)
{
	const char *game = getenv("XS4_GAME");
	char path[PATH_MAX];
	snprintf(path, sizeof path, "%s/dohnadohnaEx.ex", game ? game : ".");
	struct ex *e = game ? ex_read_file(path) : NULL;
	if (!e) {
		printf("OPTION-RETURN no EX file in XS4_GAME\n");
		exit(2);
	}
	probe_mainex_set(e);
	return e;
}

/* The tree whose nodes fno lists: its first string constant. */
static struct ex_tree *or_tree_of(struct ex *e, int fno)
{
	for (uint32_t ip = ain->functions[fno].address; ip + 6 <= ain->code_size;) {
		enum opcode op = get_opcode(ip);
		if (op == S_PUSH) {
			int idx = LittleEndian_getDW(ain->code, ip + 2);
			struct ex_tree *t = idx >= 0 && idx < ain->nr_strings ? ex_get_tree(e, ain->strings[idx]->text) : NULL;
			if (t && !t->is_leaf)
				return t;
			break;
		}
		if (op == ENDFUNC)
			break;
		ip += instruction_width(op);
	}
	printf("OPTION-RETURN the EX file lacks the tree %s reads\n", ain->functions[fno].name);
	exit(2);
}

static bool or_is_achievement(int slot)
{
	if (!da_live_page(slot) || heap[slot].page->type != STRUCT_PAGE)
		return false;
	int s = heap[slot].page->index;
	return s >= 0 && s < ain->nr_structures && !strncmp(ain->structures[s].name, "Achievement", 11);
}

/* A collection whose members are initialized as its constructor does first. */
static int or_collection(void)
{
	int obj = alloc_struct(sp_sno("AchievementCollection"));
	int sp = or_guard_push();
	vm_call(da_fn("AchievementCollection@2"), obj);
	or_guard_pop("AchievementCollection@2", sp, 0, NULL);
	return obj;
}

/* --- OR1 --- */

static void or_load_achievements(void)
{
	struct ex *e = or_real_ex();
	int fno = da_fn("AchievementCollection@LoadAchievementsFromEx");
	struct ex_tree *tree = or_tree_of(e, fno);
	int want = 0;
	for (unsigned i = 0; i < tree->nr_children; i++)
		if (!tree->children[i].is_leaf)
			want++;
	int st = sp_sno("AchievementCollection"), obj = or_collection();
	int sp = or_guard_push();
	vm_call(fno, obj);
	or_guard_pop("LoadAchievementsFromEx", sp, 0, NULL);
	or_collect();

	int arr = heap[obj].page->values[sp_mem(st, "m_achievements")].i;
	struct page *p = da_live_page(arr) && heap[arr].page->type == ARRAY_PAGE ? heap[arr].page : NULL;
	int n = p ? wc_size(arr) : -1, good = 0;
	for (int i = 0; p && p->nr_vars == 2 * n && i < n; i++)
		if (or_is_achievement(p->values[2 * i].i) && HEAP_REF(p->values[2 * i].i) == 1)
			good++;
	printf("OR1 %d node(s) in the EX tree, %d element(s) in the collection (%d slot(s)), %d achievement object(s)\n",
	       want, n, p ? p->nr_vars : -1, good);
	OR_CHECK(want > 0, "the EX tree has no nodes");
	OR_CHECK(p && n == want && p->nr_vars == 2 * want, "want %d two-slot element(s)", want);
	OR_CHECK(good == want, "%d of %d element(s) are achievement objects with one reference", good, want);
	if (p && n > 0)
		printf("OR1 first element: struct %s, vtable offset %d\n",
		       or_is_achievement(p->values[0].i) ? ain->structures[heap[p->values[0].i].page->index].name : "(none)",
		       p->values[1].i);
}

/* --- OR2 --- */

static void or_create(void)
{
	struct ex *e = or_real_ex();
	struct ex_tree *tree = or_tree_of(e, da_fn("AchievementCollection@LoadAchievementsFromEx"));
	const char *first = NULL;
	for (unsigned i = 0; i < tree->nr_children && !first; i++)
		if (!tree->children[i].is_leaf)
			first = tree->children[i].name->text;
	if (!first) {
		printf("OPTION-RETURN the achievement tree has no nodes\n");
		exit(2);
	}
	int fno = da_fn("AchievementCollection@Create"), obj = or_collection();
	int r[3];

	int id = sf_str(first, -1);
	int sp = or_guard_push();
	or_method(fno, obj, 1, &id);
	or_guard_pop("Create(first achievement)", sp, 3, r);
	printf("OR2 Create(first achievement) = (%s, %d, %d), references %d\n",
	       or_is_achievement(r[0]) ? ain->structures[heap[r[0]].page->index].name : "not an achievement", r[1], r[2],
	       or_is_achievement(r[0]) ? HEAP_REF(r[0]) : -1);
	OR_CHECK(or_is_achievement(r[0]) && r[1] == 0 && r[2] == 0, "want (achievement object, 0, 0)");
	OR_CHECK(or_is_achievement(r[0]) && HEAP_REF(r[0]) == 1, "the object is not the caller's alone");
	OR_CHECK(HEAP_REF(id) == 1, "the argument has %d reference(s), want 1", HEAP_REF(id));
	heap_unref(id);
	if (or_is_achievement(r[0]))
		heap_unref(r[0]);

	id = sf_str("NoSuchAchievement", -1);
	sp = or_guard_push();
	or_method(fno, obj, 1, &id);
	or_guard_pop("Create(missing)", sp, 3, r);
	printf("OR2 Create(missing) = (%d, %d, %d)\n", r[0], r[1], r[2]);
	OR_CHECK(r[0] == -1 && r[1] == -1 && r[2] == 1, "want none (-1, -1, 1)");
	heap_unref(id);
	or_collect();
}

/* --- OR3 --- */

static void or_inner_action(void)
{
	int st = sp_sno("PlayerAction"), m = sp_mem(st, "m_innerAction");
	int fno = da_fn("PlayerAction@InnerAction::get");
	struct ain_type *mt = &ain->structures[st].members[m].type;
	if (!or_is_three(mt) || !or_is_three(&ain->functions[fno].return_type)) {
		printf("OPTION-RETURN PlayerAction.m_innerAction is not a three-slot option\n");
		exit(2);
	}
	int pa = alloc_struct(st), inner = oa_obj();
	union vm_value *v = heap[pa].page->values;
	int r[3];

	/* none, as the member is before anything is stored */
	v[m].i = -1;
	v[m + 1].i = -1;
	v[m + 2].i = 1;
	int sp = or_guard_push();
	vm_call(fno, pa);
	or_guard_pop("InnerAction::get (none)", sp, 3, r);
	printf("OR3 InnerAction::get on none = (%d, %d, %d)\n", r[0], r[1], r[2]);
	OR_CHECK(r[0] == -1 && r[1] == -1 && r[2] == 1, "want none (-1, -1, 1)");

	/* (object, vtable offset 7, has a value); the member owns one reference */
	heap[pa].page->values[m].i = inner;
	heap[pa].page->values[m + 1].i = 7;
	heap[pa].page->values[m + 2].i = 0;
	sp = or_guard_push();
	vm_call(fno, pa);
	or_guard_pop("InnerAction::get", sp, 3, r);
	printf("OR3 InnerAction::get = (%s, %d, %d), object references %d\n", r[0] == inner ? "the object" : "something else",
	       r[1], r[2], HEAP_REF(inner));
	OR_CHECK(r[0] == inner && r[1] == 7 && r[2] == 0, "want (the object, 7, 0), got (%d, %d, %d)", r[0], r[1], r[2]);
	OR_CHECK(HEAP_REF(inner) == 2, "the result does not hold its own reference (%d, want 2)", HEAP_REF(inner));
	heap_unref(inner);	/* the result's */
	heap_unref(pa);		/* releases the member's */
	OR_CHECK(HEAP_REF(inner) == 0, "the object has %d reference(s) after its owner went", HEAP_REF(inner));
	or_collect();
}

/* --- OR4 --- */

static void or_missing_object(void)
{
	int fno = da_fn("PlayerAction@InnerAction::get");
	int gone = alloc_struct(sp_sno("PlayerAction"));
	heap_unref(gone);
	int csp = call_stack_ptr;
	int sp = or_guard_push();
	vm_call(fno, gone);
	int n = or_guard_pop("InnerAction::get on a released object", sp, 3, NULL);
	printf("OR4 a call on a released object left %d slot(s)\n", n);
	OR_CHECK(call_stack_ptr == csp, "call stack %d != %d", call_stack_ptr, csp);
}

/* --- OR6 --- */

static void or_stray_value(void)
{
	int st = sp_sno("PlayerAction"), m = sp_mem(st, "m_innerAction");
	int fno = da_fn("PlayerAction@InnerAction::get");
	if (!or_is_three(&ain->structures[st].members[m].type)) {
		printf("OPTION-RETURN PlayerAction.m_innerAction is not a three-slot option\n");
		exit(2);
	}
	int pa = alloc_struct(st), inner = oa_obj(), r[3];
	heap[pa].page->values[m].i = inner;
	heap[pa].page->values[m + 1].i = 7;
	heap[pa].page->values[m + 2].i = 0;
	int sp = or_guard_push(), csp = call_stack_ptr;
	size_t ip = instr_ptr;
	stack_push(pa);
	method_call(fno, VM_RETURN);
	stack_push(0x0BAD);	/* above the frame's base, under the result to come */
	vm_execute();
	instr_ptr = ip;
	or_guard_pop("InnerAction::get with a stray value", sp, 3, r);
	printf("OR6 with a stray value under the result = (%s, %d, %d)\n", r[0] == inner ? "the object" : "something else",
	       r[1], r[2]);
	OR_CHECK(call_stack_ptr == csp, "call stack %d != %d", call_stack_ptr, csp);
	OR_CHECK(r[0] == inner && r[1] == 7 && r[2] == 0, "want (the object, 7, 0), got (%d, %d, %d)", r[0], r[1], r[2]);
	or_collect();
}

/* --- OR5, G1, G2 --- */

static void or_three_slot_functions(void)
{
	int three = 0, bad = 0;
	for (int f = 0; f < ain->nr_functions; f++) {
		struct ain_type *t = &ain->functions[f].return_type;
		if (!or_is_three(t))
			continue;
		three++;
		int s = ain_return_slots_type(t);
		if (s != 3) {
			if (bad++ < 3)
				printf("  f%d is counted as %d slot(s)\n", f, s);
		}
	}
	printf("OR5 %d function(s) return an option of an interface wrap; %d not counted as three slots\n", three, bad);
	OR_CHECK(three == 13, "this AIN has 13 such functions, found %d", three);
	OR_CHECK(bad == 0, "%d of them are not three slots", bad);
}

static void or_guard_table(void)
{
	/* recorded with ain_return_slots_type before this change */
	static const int want[3] = { 18898, 17555, 1276 };
	int count[4] = { 0, 0, 0, 0 }, option_two = 0, option_two_bad = 0;
	for (int f = 0; f < ain->nr_functions; f++) {
		struct ain_type *t = &ain->functions[f].return_type;
		if (or_is_three(t))
			continue;
		int s = ain_return_slots_type(t);
		count[s >= 0 && s < 3 ? s : 3]++;
		if (t->data == AIN_OPTION) {
			option_two++;
			if (s != 2)
				option_two_bad++;
		}
	}
	printf("G1 other functions by result slots: 0: %d, 1: %d, 2: %d, other: %d; options of a one-slot payload: %d\n",
	       count[0], count[1], count[2], count[3], option_two);
	OR_CHECK(count[0] == want[0] && count[1] == want[1] && count[2] == want[2] && count[3] == 0,
		"want 0: %d, 1: %d, 2: %d, other: 0", want[0], want[1], want[2]);
	OR_CHECK(option_two == 810 && option_two_bad == 0, "%d of %d two-slot options are not two slots", option_two_bad,
		option_two);

	/* delegates: DG_CALL takes its result by delegate_return_slots */
	int dg_option = 0, dg_three = 0, dg_bad = 0;
	for (int i = 0; i < ain->nr_delegates; i++) {
		struct ain_type *t = &ain->delegates[i].return_type;
		if (t->data != AIN_OPTION)
			continue;
		dg_option++;
		if (or_is_three(t))
			dg_three++;
		if (delegate_return_slots(t) != 2)
			dg_bad++;
	}
	printf("G1 delegates returning an option: %d; of an interface wrap: %d; not counted as two slots: %d\n", dg_option,
	       dg_three, dg_bad);
	OR_CHECK(dg_option == 64 && dg_three == 0 && dg_bad == 0, "want 64 delegates, none three-slot, all two slots");
}

static void or_guard_version(void)
{
	int version = ain->version, bad = 0;
	ain->version = 13;
	for (int f = 0; f < ain->nr_functions; f++) {
		struct ain_type *t = &ain->functions[f].return_type;
		if (ain_return_slots_type(t) != (t->data == AIN_VOID ? 0 : 1))
			bad++;
	}
	ain->version = version;
	printf("G2 version 13: %d function(s) not counted as void 0, anything else 1\n", bad);
	OR_CHECK(bad == 0, "%d function(s) differ", bad);
}

/* --- G3 --- */

static void or_guard_two_slot(void)
{
	const char *ids[2] = { "worker_a", NULL };
	for (int i = 0; i < 2; i++) {
		int worker, shop = oa_shop(ids[i], &worker), r[2] = { -999, -999 };
		or_collect();
		int sp = or_guard_push();
		vm_call(da_fn("WorkerCollection@GetShowCaseWorker"), shop);
		or_guard_pop("GetShowCaseWorker", sp, 2, r);
		printf("G3 GetShowCaseWorker %s = (%s, %d)\n", ids[i] ? "with a showcase" : "without one",
		       r[0] == -1 ? "-1" : r[0] == worker ? "the worker" : "something else", r[1]);
		OR_CHECK(r[0] == worker && r[1] == (worker > 0 ? 0 : 1), "want (%s)", worker > 0 ? "the worker, 0" : "-1, 1");
	}
}

static int test_option_return(void)
{
	static const struct { const char *name; void (*run)(void); } cases[] = {
		{ "OR1", or_load_achievements }, { "OR2", or_create }, { "OR3", or_inner_action },
		{ "OR4", or_missing_object }, { "OR5", or_three_slot_functions }, { "OR6", or_stray_value },
		{ "G1", or_guard_table }, { "G2", or_guard_version }, { "G3", or_guard_two_slot },
	};
	int failed = 0, environment = 0;
	for (unsigned i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
		fflush(stdout);
		fflush(stderr);
		pid_t pid = fork();
		if (pid == 0) {
			alarm(60);
			or_failures = da_failures = wc_failures = oa_failures = bu_failures = 0;
			cases[i].run();
			or_collect();
			fflush(stdout);
			_exit(or_failures ? 1 : 0);
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
			printf("%s ENVIRONMENT (no EX file, or the AIN lacks something this case looks up)\n", cases[i].name);
			environment++;
		} else {
			printf("%s FAIL (exit %d)\n", cases[i].name, WEXITSTATUS(st));
			failed++;
		}
	}
	printf("OPTION-RETURN %s: %d of %zu cases failed, %d environment error(s)\n",
		failed || environment ? "FAIL" : "PASS", failed, sizeof(cases) / sizeof(cases[0]), environment);
	return environment ? 2 : failed ? 1 : 0;
}
