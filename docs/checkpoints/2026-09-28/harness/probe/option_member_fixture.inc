/* option-member: the option members of a newly allocated object are none.
 *
 * Original (static disassembly only). Every object is allocated by 0x679db0
 * (NEW goes through 0x679b30, which calls it first), which initializes the
 * members with 0x656b10: a string, struct, delegate or array member is -1
 * (0x656b82) and any other goes by its type through 0x656970. There an int,
 * float, bool or enum is 0 (0x6569a9), a reference, wrap or interface -1 with
 * zeros behind it (0x6569b8), a void member is left alone (0x6569a0), and an
 * option (type 86, 0x656a44) gets -1 in every slot but its last and
 * 0x653610(type) + 1 there: the number of option layers, 1 for option<T>.
 * The slots of an option after the first are void members of their own, so
 * the loop does not touch them again. A discriminant of 0 means "has a
 * value", 1 and up none (see option-array).
 *
 * Before the fix alloc_struct left the discriminant slot at 0: a member no
 * script code had written yet read as "has the value -1" wherever the
 * discriminant alone is read: an if-let over a value payload (int, enum),
 * `?.` and `??`. An if-let, HasValue or Unwrap over a reference payload also
 * compares the value with -1 and read none before the fix too. The battle relies
 * on the default: DamageFrameMotionController@2 does not initialize
 * m_deadFrame (option<int>), LoadMotions only sets it when the target dies,
 * and Update fires OnDeadMotionStartEvent once the frame it holds is reached.
 * With "-1" there, every hit fired it on its first update, and
 * PlayerActionView's handler (PlayerViewPartsLayer@HideInterface) faded the
 * target's interface layer out: its gauge and the damage numerals under it.
 *
 * Cases (each in its own fork):
 *   OM1 every struct of the AIN: each option member of a new object is
 *       none (two-slot and three-slot ones; the slots are counted from the
 *       member table, the option and the void members behind it, not by the
 *       allocator's own slot count), and the other members are what they
 *       were (a hash over their types and value classes, taken before the
 *       fix). This AIN has no option of an option: the layer count above
 *       is not tested.
 *   OM2 real bytecode whose result the discriminant decides:
 *       WorkerCollection@Limit::get (FUNC 28074, `m_limit ?? 2147483647`) of
 *       a new object is 2147483647 (-1 before the fix) and
 *       ShopDisplayUnit@PriceRatio::get (FUNC 27914, an if-let over
 *       DiscountPer) is 1.0 (1.01 before); with a value there they give it.
 *       ActionViewControllerSet@Playing::get (HasValue on two reference
 *       options) is false, as it was before the fix: a guard only.
 *   OM3 real bytecode: DamageFrameMotionController@2 and @Update. With the
 *       default m_deadFrame no update calls the death event; with a frame
 *       there it is called once that frame is reached, not before.
 *   OM4 guards: an AIN before version 14 keeps its member defaults; a copy of
 *       an object keeps none as none (a value option, and a three-slot
 *       option of a reference), and releasing both frees nothing twice.
 *   OM5 real bytecode: FeelEventResult@Apply (FUNC 27808) of a new object
 *       takes none of its `if (p = X)` branches (Feel, VoltageBase,
 *       VoltageMax, MpMax, BadgeCount): the player it is given, an object
 *       that only exists, is the same afterwards (a hash over it and the
 *       structs under it). Before the fix each branch added -1 to it.
 */
#include "serialize_struct.h"

static int om_failures;
#define OM_CHECK(c, ...) do { if (!(c)) { printf("  FAIL: "); printf(__VA_ARGS__); printf("\n"); om_failures++; } } while (0)

static void om_env(const char *what)
{
	printf("OPTION-MEMBER environment: %s\n", what);
	fflush(stdout);
	_exit(3);
}

static int om_fn(const char *name)
{
	char copy[512];
	snprintf(copy, sizeof copy, "%s", name);
	int f = ain_get_function(ain, copy);
	if (f < 0)
		om_env("a function the case needs is not in this AIN");
	return f;
}

static int om_struct(const char *name)
{
	int s = ain_get_struct(ain, (char *)name);
	if (s < 0)
		om_env("a struct the case needs is not in this AIN");
	return s;
}

/* Calls fno as a method of obj with int arguments; returns the first result
 * slot (or -999). */
static int om_call(int fno, int obj, int nargs, const int *args)
{
	int sp = stack_ptr, csp = call_stack_ptr;
	size_t ip = instr_ptr;
	stack_push(obj);
	for (int i = 0; i < nargs; i++)
		stack_push(args[i]);
	method_call(fno, VM_RETURN);
	vm_execute();
	instr_ptr = ip;
	OM_CHECK(call_stack_ptr == csp, "%s: call stack %d != %d", ain->functions[fno].name, call_stack_ptr, csp);
	int r = stack_ptr > sp ? stack[sp].i : -999;
	stack_ptr = sp;
	return r;
}

static int om_member(int sno, const char *name)
{
	struct ain_struct *s = &ain->structures[sno];
	for (int i = 0; i < s->nr_members; i++)
		if (!strcmp(s->members[i].name, name))
			return i;
	om_env("a member the case needs is not in this AIN");
	return -1;
}

/* The slots of the option member m as the member table has them: itself and
 * the void members behind it. */
static int om_member_slots(struct ain_struct *s, int m)
{
	int n = 1;
	while (m + n < s->nr_members && s->members[m + n].type.data == AIN_VOID)
		n++;
	return n;
}

/* The members that follow an option as its own slots are void. */
static bool om_option_shape(struct ain_struct *s, int m, int slots)
{
	if (slots < 2 || m + slots > s->nr_members)
		return false;
	for (int k = 1; k < slots; k++)
		if (s->members[m + k].type.data != AIN_VOID)
			return false;
	return true;
}

/* --- OM1 --- */
#define OM_OTHER_MEMBERS_HASH 0xb16e0152u	/* the same before the fix (95e2da0) */
static void om_scan(void)
{
	int objects = 0, options = 0, two = 0, three = 0, nested = 0, odd = 0, wrong = 0;
	uint32_t hash = 2166136261u;
	for (int no = 0; no < ain->nr_structures; no++) {
		struct ain_struct *s = &ain->structures[no];
		int slot = alloc_struct(no);
		struct page *p = slot > 0 && heap_index_valid(slot) ? heap[slot].page : NULL;
		if (!p || p->nr_vars != s->nr_members) {
			/* alloc_struct's depth guard: not an object to look at. */
			continue;
		}
		objects++;
		for (int m = 0; m < s->nr_members; m++) {
			const struct ain_type *t = &s->members[m].type;
			if (t->data == AIN_OPTION) {
				int slots = om_member_slots(s, m);
				const int layers = 1;
				if (slots < 2 || slots > 3) {
					odd++;
					m += slots - 1;
					continue;
				}
				options++;
				two += slots == 2;
				three += slots == 3;
				if (t->array_type && (t->array_type->data == AIN_OPTION || t->array_type->data == AIN_UNKNOWN_TYPE_87)) {
					/* An option of an option: not judged here. */
					nested++;
					m += slots - 1;
					continue;
				}
				bool none = p->values[m + slots - 1].i == layers;
				for (int k = 0; k < slots - 1; k++)
					none = none && p->values[m + k].i == -1;
				if (!none && wrong++ < 5)
					printf("  FAIL: %s.%s (%d slots, %d layer(s)) is (%d, .., %d), want (-1, .., %d)\n", s->name,
					       s->members[m].name, slots, layers, p->values[m].i, p->values[m + slots - 1].i, layers);
				m += slots - 1;
				continue;
			}
			int v = p->values[m].i;
			int cls = v == -1 ? 1 : v == 0 ? 2 : v > 0 && heap_index_valid(v) ? 3 : 4;
			hash = (hash ^ (uint32_t)(t->data * 8 + cls)) * 16777619u;
		}
		/* Not released: these objects were never constructed, and their
		 * destructors are script code. */
	}
	om_failures += wrong;
	printf("  OM1 %d objects, %d option members (%d two-slot, %d three-slot; %d nested, not judged), %d not none, "
	       "%d of another shape; other members' hash %08x\n", objects, options, two, three, nested, wrong, odd, hash);
	/* 171 and 37 are also what the decompiled struct list has (counted
	 * there by the void members after each option). */
	OM_CHECK(two == 171 && three == 37, "OM1 %d two-slot and %d three-slot option members, want 171 and 37", two, three);
	OM_CHECK(odd == 0, "OM1 %d option member(s) have another number of slots than 2 or 3", odd);
	OM_CHECK(hash == OM_OTHER_MEMBERS_HASH, "OM1 the members that are no options changed: hash %08x, want %08x", hash,
		 OM_OTHER_MEMBERS_HASH);
	/* The one the battle depends on. */
	int dc = om_struct("DamageFrameMotionController");
	int obj = alloc_struct(dc);
	union vm_value *v = heap[obj].page->values;
	OM_CHECK(ain->structures[dc].nr_members == 12 && ain->structures[dc].members[6].type.data == AIN_OPTION,
		 "OM1 DamageFrameMotionController is not the struct this case was written for");
	OM_CHECK(v[6].i == -1 && v[7].i == 1, "OM1 DamageFrameMotionController.m_deadFrame is (%d, %d), want none (-1, 1)", v[6].i, v[7].i);
	printf("OM1 option members of new objects: %s\n", om_failures ? "FAIL" : "ok");
}

/* --- OM2 --- */
static void om_value_getters(void)
{
	/* m_limit ?? 2147483647: `X_REF 2; PUSH 1; GTE` on the discriminant. */
	int wc = om_struct("WorkerCollection"), lim = om_member(wc, "m_limit");
	int limit_get = om_fn("WorkerCollection@Limit::get");
	int c = alloc_struct(wc);
	int limit = om_call(limit_get, c, 0, NULL);
	OM_CHECK(limit == 2147483647, "OM2 Limit::get of a new WorkerCollection is %d, want 2147483647 (no limit)", limit);
	heap[c].page->values[lim].i = 7;
	heap[c].page->values[lim + 1].i = 0;
	limit = om_call(limit_get, c, 0, NULL);
	OM_CHECK(limit == 7, "OM2 Limit::get with the limit 7 is %d", limit);
	/* if (discount = DiscountPer) return 1.0 - discount / 100.0; return 1.0; */
	int su = om_struct("ShopDisplayUnit"), dis = om_member(su, "DiscountPer");
	int ratio_get = om_fn("ShopDisplayUnit@PriceRatio::get");
	int u = alloc_struct(su);
	union vm_value ratio = { .i = om_call(ratio_get, u, 0, NULL) };
	OM_CHECK(ratio.f == 1.0f, "OM2 PriceRatio::get of a new ShopDisplayUnit is %g, want 1 (no discount)", ratio.f);
	heap[u].page->values[dis].i = 20;
	heap[u].page->values[dis + 1].i = 0;
	ratio.i = om_call(ratio_get, u, 0, NULL);
	OM_CHECK(fabsf(ratio.f - 0.8f) < 1e-6f, "OM2 PriceRatio::get with a discount of 20 is %g, want 0.8", ratio.f);

	/* Unchanged by the fix (HasValue on a reference option also compares
	 * the value with -1): the slots, and that the getter still says no. */
	int set = alloc_struct(om_struct("ActionViewControllerSet"));
	union vm_value *v = heap[set].page->values;
	/* m_controller and m_overlayController: option<wrap<iwrap<IFrameController>>>. */
	OM_CHECK(v[0].i == -1 && v[1].i == -1 && v[2].i == 1 && v[3].i == -1 && v[4].i == -1 && v[5].i == 1,
		 "OM2 the two controllers are (%d,%d,%d) and (%d,%d,%d), want none (-1,-1,1)", v[0].i, v[1].i, v[2].i, v[3].i,
		 v[4].i, v[5].i);
	int playing = om_call(om_fn("ActionViewControllerSet@Playing::get"), set, 0, NULL);
	OM_CHECK(playing == 0, "OM2 Playing::get of an object without controllers is %d, want false", playing);
	printf("OM2 Limit::get, PriceRatio::get and Playing::get on new objects: %s\n", om_failures ? "FAIL" : "ok");
}

/* --- OM3 --- */
static void om_dead_frame(void)
{
	int g = -1;
	for (int i = 0; i < ain->nr_globals; i++)
		if (!strcmp(ain->globals[i].name, "MotionContext::FRAME_TIME"))
			g = i;
	if (g < 0)
		om_env("MotionContext::FRAME_TIME is not in this AIN");
	heap[global_page_slot].page->values[g].f = 1000.0f / 60.0f;	/* as the alloc function sets it */
	int obj = alloc_struct(om_struct("DamageFrameMotionController"));
	om_call(om_fn("DamageFrameMotionController@2"), obj, 0, NULL);
	union vm_value *v = heap[obj].page->values;
	int sub = v[9].i;	/* m_subFrames, allocated with the object */
	if (sub <= 0 || !heap_index_valid(sub) || !heap[sub].page)
		om_env("DamageFrameMotionController.m_subFrames was not allocated");
	om_call(om_fn("DamageSubFrameController@2"), sub, 0, NULL);
	int update = om_fn("DamageFrameMotionController@Update");
	/* The target survives: nothing wrote m_deadFrame. */
	OM_CHECK(v[6].i == -1 && v[7].i == 1 && v[8].i == 0, "OM3 after @2: m_deadFrame (%d, %d), m_deadEventCalled %d", v[6].i,
		 v[7].i, v[8].i);
	om_call(update, obj, 1, (int[]){ 0 });
	OM_CHECK(heap[obj].page->values[8].i == 0, "OM3 Update(0) with no death frame called the death event");
	om_call(update, obj, 1, (int[]){ 5000 });
	OM_CHECK(heap[obj].page->values[8].i == 0, "OM3 Update(5000) with no death frame called the death event");
	/* The target dies at frame 3 (LoadMotions would set this). */
	heap[obj].page->values[6].i = 3;
	heap[obj].page->values[7].i = 0;
	om_call(update, obj, 1, (int[]){ 33 });
	OM_CHECK(heap[obj].page->values[8].i == 0, "OM3 Update(33): frame 1 is before the death frame 3, yet the event was called");
	om_call(update, obj, 1, (int[]){ 60 });
	OM_CHECK(heap[obj].page->values[8].i == 1, "OM3 Update(60): frame 3 is the death frame, the event was not called");
	printf("OM3 DamageFrameMotionController@Update and m_deadFrame: %s\n", om_failures ? "FAIL" : "ok");
}

/* --- OM4 --- */
static void om_guards(void)
{
	int dc = om_struct("DamageFrameMotionController");
	/* An AIN before version 14 has no such members; its defaults stay. */
	ain->version = 13;
	int old = alloc_struct(dc);
	OM_CHECK(heap[old].page->values[7].i == 0, "OM4 version 13: the member after the option is %d, want 0 as before",
		 heap[old].page->values[7].i);
	ain->version = 14;
	/* A struct without a destructor that holds an option of a value type:
	 * copy it, release both. */
	int no = -1, m = -1;
	for (int s = 0; s < ain->nr_structures && no < 0; s++) {
		struct ain_struct *st = &ain->structures[s];
		if (st->destructor > 0)
			continue;
		bool plain = true;
		int at = -1;
		for (int i = 0; i < st->nr_members; i++) {
			const struct ain_type *t = &st->members[i].type;
			if (t->data == AIN_OPTION && t->array_type && (t->array_type->data == AIN_INT || t->array_type->data == AIN_ENUM
			    || t->array_type->data == AIN_FLOAT || t->array_type->data == AIN_BOOL) && om_option_shape(st, i, 2)) {
				at = at < 0 ? i : at;
				i++;
			} else if (t->data != AIN_INT && t->data != AIN_FLOAT && t->data != AIN_BOOL && t->data != AIN_ENUM
				   && t->data != AIN_VOID && t->data != AIN_STRING) {
				plain = false;
			}
		}
		if (plain && at >= 0) {
			no = s;
			m = at;
		}
	}
	if (no < 0)
		om_env("no plain struct with an option of a value type in this AIN");
	int a = alloc_struct(no);
	OM_CHECK(heap[a].page->values[m].i == -1 && heap[a].page->values[m + 1].i == 1, "OM4 %s.%s is (%d, %d), want none",
		 ain->structures[no].name, ain->structures[no].members[m].name, heap[a].page->values[m].i,
		 heap[a].page->values[m + 1].i);
	int b = vm_copy_page(heap[a].page);
	OM_CHECK(b > 0 && heap[b].page && heap[b].page->values[m].i == -1 && heap[b].page->values[m + 1].i == 1,
		 "OM4 a copy of %s does not keep none", ain->structures[no].name);
	/* A value in the copy does not reach the original. */
	heap[b].page->values[m].i = 42;
	heap[b].page->values[m + 1].i = 0;
	OM_CHECK(heap[a].page->values[m].i == -1 && heap[a].page->values[m + 1].i == 1, "OM4 the original changed with its copy");
	heap_unref(b);
	heap_unref(a);
	printf("  OM4 copied and released %s (option member %s)\n", ain->structures[no].name, ain->structures[no].members[m].name);
	/* The same with a three-slot option of a reference (option<wrap<..>>):
	 * a struct without a destructor whose other members hold no pages. */
	int no3 = -1, m3 = -1;
	for (int s = 0; s < ain->nr_structures && no3 < 0; s++) {
		struct ain_struct *st = &ain->structures[s];
		if (st->destructor > 0)
			continue;
		bool plain = true;
		int at = -1;
		for (int i = 0; i < st->nr_members; i++) {
			const struct ain_type *t = &st->members[i].type;
			if (t->data == AIN_OPTION) {
				int slots = om_member_slots(st, i);
				if (slots == 3 && at < 0)
					at = i;
				else if (slots != 2 && slots != 3)
					plain = false;
				i += slots - 1;
			} else if (t->data != AIN_INT && t->data != AIN_FLOAT && t->data != AIN_BOOL && t->data != AIN_ENUM
				   && t->data != AIN_VOID && t->data != AIN_STRING) {
				plain = false;
			}
		}
		if (plain && at >= 0) {
			no3 = s;
			m3 = at;
		}
	}
	if (no3 < 0)
		om_env("no plain struct with a three-slot option in this AIN");
#define OM_NONE3(pg) (heap[pg].page->values[m3].i == -1 && heap[pg].page->values[m3 + 1].i == -1 && heap[pg].page->values[m3 + 2].i == 1)
	int a3 = alloc_struct(no3);
	OM_CHECK(OM_NONE3(a3), "OM4 %s.%s is (%d, %d, %d), want none (-1, -1, 1)", ain->structures[no3].name,
		 ain->structures[no3].members[m3].name, heap[a3].page->values[m3].i, heap[a3].page->values[m3 + 1].i,
		 heap[a3].page->values[m3 + 2].i);
	int b3 = vm_copy_page(heap[a3].page);
	OM_CHECK(b3 > 0 && heap[b3].page && OM_NONE3(b3), "OM4 a copy of %s does not keep its three-slot none", ain->structures[no3].name);
	OM_CHECK(OM_NONE3(a3), "OM4 %s changed when it was copied", ain->structures[no3].name);
	heap_unref(b3);
	OM_CHECK(heap[a3].page && OM_NONE3(a3), "OM4 releasing the copy of %s reached the original", ain->structures[no3].name);
	heap_unref(a3);
#undef OM_NONE3
	printf("  OM4 copied and released %s (three-slot option member %s)\n", ain->structures[no3].name,
	       ain->structures[no3].members[m3].name);
	printf("OM4 guards (version 13, copy and release): %s\n", om_failures ? "FAIL" : "ok");
}

/* --- OM5 --- */
/* A hash over an object and the struct members under it, a few levels down. */
static uint32_t om_object_hash(int slot, int depth, uint32_t h)
{
	if (depth > 3 || slot <= 0 || !heap_index_valid(slot) || heap[slot].type != VM_PAGE || !heap[slot].page
	    || heap[slot].page->type != STRUCT_PAGE)
		return (h ^ 0x5bu) * 16777619u;
	struct page *p = heap[slot].page;
	struct ain_struct *s = &ain->structures[p->index];
	for (int i = 0; i < p->nr_vars && i < s->nr_members; i++) {
		h = (h ^ (uint32_t)p->values[i].i) * 16777619u;
		if (s->members[i].type.data == AIN_STRUCT)
			h = om_object_hash(p->values[i].i, depth + 1, h);
	}
	return h;
}

static void om_feel_result(void)
{
	int fr = om_struct("FeelEventResult");
	int obj = alloc_struct(fr);
	static const char *const opts[] = { "Feel", "VoltageBase", "VoltageMax", "MpMax", "BadgeCount" };
	for (int i = 0; i < 5; i++) {
		int m = om_member(fr, opts[i]);
		OM_CHECK(heap[obj].page->values[m].i == -1 && heap[obj].page->values[m + 1].i == 1, "OM5 FeelEventResult.%s is (%d, %d), want none",
			 opts[i], heap[obj].page->values[m].i, heap[obj].page->values[m + 1].i);
	}
	/* Its player is one that only exists. A branch that is taken reads a
	 * value of the player, adds p and writes it back. */
	int pl = alloc_struct(om_struct("Player"));
	heap[obj].page->values[om_member(fr, "<Player>")].i = pl;
	uint32_t before = om_object_hash(pl, 0, 2166136261u);
	printf("  OM5 calling FeelEventResult@Apply (player hash %08x)\n", before);
	fflush(stdout);
	om_call(om_fn("FeelEventResult@Apply"), obj, 0, NULL);
	uint32_t after = om_object_hash(pl, 0, 2166136261u);
	OM_CHECK(before == after, "OM5 FeelEventResult@Apply with nothing set changed the player (hash %08x -> %08x)", before, after);
	printf("OM5 FeelEventResult@Apply with nothing set: %s\n", om_failures ? "FAIL" : "ok");
}

/* 0 passed, 1 failed, 2 could not run. */
static int om_fork_case(const char *name, void (*fn)(void))
{
	fflush(stdout);
	pid_t pid = fork();
	if (pid < 0) {
		printf("%s ENVIRONMENT FAILURE (fork)\n", name);
		return 2;
	}
	if (pid == 0) {
		om_failures = 0;
		alarm(120);
		fn();
		fflush(stdout);
		_exit(om_failures ? 1 : 0);
	}
	int status = 0;
	waitpid(pid, &status, 0);
	if (WIFEXITED(status) && WEXITSTATUS(status) == 0)
		return 0;
	if (WIFEXITED(status) && WEXITSTATUS(status) == 3) {
		printf("%s ENVIRONMENT FAILURE\n", name);
		return 2;
	}
	printf("%s FAILED (%s %d)\n", name, WIFEXITED(status) ? "exit" : "signal",
	       WIFEXITED(status) ? WEXITSTATUS(status) : WTERMSIG(status));
	return 1;
}

static int test_option_member(void)
{
	static const struct { const char *name; void (*fn)(void); } cases[] = {
		{ "OM1", om_scan }, { "OM2", om_value_getters }, { "OM3", om_dead_frame }, { "OM4", om_guards },
		{ "OM5", om_feel_result },
	};
	const int n = sizeof cases / sizeof cases[0];
	int failed = 0, env = 0;
	for (int i = 0; i < n; i++) {
		int r = om_fork_case(cases[i].name, cases[i].fn);
		failed += r == 1;
		env += r == 2;
	}
	printf("OPTION-MEMBER %s: %d of %d cases failed, %d environment error(s)\n", failed || env ? "FAIL" : "PASS", failed, n, env);
	return failed ? 1 : env ? 2 : 0;
}
