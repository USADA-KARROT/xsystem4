/* aref-value: A_REF of a struct of plain numbers makes a copy.
 *
 * Original (static disassembly): A_REF is "PageCopy" (0x66b790 -> 0x6794d0).
 * For a struct page (case 4, 0x67959a) it takes the type (0x681f30),
 * allocates a new object without running a constructor (0x679b30(type, 0))
 * and copies the members; the new slot is pushed. Every A_REF of a struct is
 * a copy.
 *
 * This VM shares the page of a v14 struct instead (heap_ref, the same slot).
 * `return CASColor::BLACK;` (.GLOBALREF, A_REF, RETURN) then hands out the
 * global itself, and what the callers do with their "copy" reaches the
 * constant: in a battle PlayerFrameView@GetAddColor returned the acting
 * unit's focus colour for every other unit, and dozens of unrelated parts
 * were given that add colour. The fix copies a struct whose members are all
 * int, float, bool or enum (CASColor, CASPos, CASSize, ...), the types the
 * original's member copy moves as one word (0x656d60, table 0x657004: 10,
 * 11, 47 and 92; a long int, 55, and type 91 take its failing branch
 * 0x656fc8, and no struct of this game has such a member). A struct with
 * reference members keeps sharing its page, as before (the full value
 * semantics of A_REF, X_SET and the others is a separate change).
 *
 * Cases (each in its own fork, after the real start-up function "0"):
 *   AV1 real bytecode PlayerFrameView@GetAddColor(Waiting): the result is
 *       not the global CASColor::BLACK's page, has its members, and writing
 *       to it or releasing it leaves the constant and its reference count
 *       alone.
 *   AV2 500 rounds of GetAddColor and GetMulColor over the focus values:
 *       BLACK and WHITE keep their members and reference counts, each
 *       result is a page of its own, and the live slots do not grow.
 *   AV3 guard, the other side of the test: real bytecode
 *       FontProperty@GetCharSpriteProperty (`.STRUCTREF m_casFont; A_REF;
 *       RETURN`, the getters' shape with a member for the global) returns
 *       the very page of a struct with a struct member (its own CASFont),
 *       and, with the member replaced for the call, of one with a string
 *       member (FontProperty) and of one with a wrap member: one more
 *       reference, no new page.
 *   AV4 the same getter with a struct of an enum (BattleBonus) and of an
 *       int and an enum (ReachedInfo) in the member's place: a copy with
 *       the members, the source's reference count unchanged.
 *   AV5 real bytecode FontProperty@SetColorBody: `.LOCALREF color; A_REF;
 *       X_DUP 1; X_MOV 4 1; CALLMETHOD 1; DELETE` calls the property setter
 *       ColorBody::set, itself `A_REF; SR_ASSIGN; DELETE`. The colour ends
 *       in a page of the font's own, the source keeps its members and its
 *       reference count, and 300 calls of the two leave the live slots where
 *       they were (each copy is released by the one that consumed it).
 *   AV6 guard: a member typed long int (55) or 91 keeps the struct's page
 *       shared (the original does not copy such a member). No struct of
 *       this game has one, so ReachedInfo's int is retyped for the call.
 * Only functions that exist before the fix are called, so before-check
 * builds.
 */
static int av_failures;
#define AV_CHECK(c, ...) do { if (!(c)) { printf("  FAIL: "); printf(__VA_ARGS__); printf("\n"); av_failures++; } } while (0)

static bool av_is_color(int slot, int r, int g, int b, int a)
{
	if (!gi_live_struct(slot, gi_struct("CASColor")))
		return false;
	union vm_value *v = heap[slot].page->values;
	return v[0].i == r && v[1].i == g && v[2].i == b && v[3].i == a;
}

/* One call of a PlayerFrameView colour getter (it does not touch `this`);
 * the caller owns the result. */
static int av_get(const char *fn, int focus)
{
	int sp = stack_ptr;
	stack_push(focus);
	vm_call(gi_fn(fn), -1);
	int slot = stack_ptr - sp == 1 ? stack_pop().i : -1;
	stack_ptr = sp;
	return slot;
}

static void av_constant(void)
{
	gi_startup();
	int black = gi_gslot(gi_global("CASColor::BLACK"));
	AV_CHECK(av_is_color(black, 0, 0, 0, 255), "CASColor::BLACK is not (0,0,0,255) after the start-up");
	if (av_failures)
		return;
	int ref = heap[black].ref;
	int got = av_get("PlayerFrameView@GetAddColor", 6);	/* Waiting: return CASColor::BLACK */
	AV_CHECK(got != black, "GetAddColor returned the page of the global CASColor::BLACK (slot %d)", black);
	AV_CHECK(av_is_color(got, 0, 0, 0, 255), "the result is not a CASColor (0,0,0,255)");
	AV_CHECK(heap[black].ref == ref, "the global has %d reference(s) while the result lives, had %d", heap[black].ref, ref);
	if (got > 0 && da_live_page(got)) {
		/* What a caller does with its copy (a focus colour written into it). */
		heap[got].page->values[0].i = 219;
		heap[got].page->values[1].i = 136;
		AV_CHECK(av_is_color(black, 0, 0, 0, 255), "writing to the result changed CASColor::BLACK to (%d,%d,%d)",
			 heap[black].page->values[0].i, heap[black].page->values[1].i, heap[black].page->values[2].i);
		heap_unref(got);
	}
	AV_CHECK(da_live_page(black) && heap[black].ref == ref, "after releasing the result the global has %d reference(s), had %d",
		 da_live_page(black) ? heap[black].ref : 0, ref);
	AV_CHECK(av_is_color(black, 0, 0, 0, 255), "CASColor::BLACK changed");
	printf("AV1 checked\n");
}

static void av_rounds(void)
{
	gi_startup();
	int black = gi_gslot(gi_global("CASColor::BLACK")), white = gi_gslot(gi_global("CASColor::WHITE"));
	AV_CHECK(av_is_color(black, 0, 0, 0, 255) && av_is_color(white, 255, 255, 255, 255), "the constants after the start-up");
	if (av_failures)
		return;
	int bref = heap[black].ref, wref = heap[white].ref;
	/* One warm-up round, then the slot count must stay. */
	int w = av_get("PlayerFrameView@GetMulColor", 0);
	if (w > 0 && w != white && w != black)
		heap_unref(w);
	size_t live = live_slots();
	int shared = 0, wrong = 0;
	for (int i = 0; i < 500; i++) {
		int focus = i % 7;
		int add = av_get("PlayerFrameView@GetAddColor", focus);
		int mul = av_get("PlayerFrameView@GetMulColor", focus);
		shared += add == black || add == white || mul == black || mul == white || add == mul;
		/* Exclude (3): add (255,0,186), multiply BLACK; Waiting (6):
		 * multiply (200,200,200); otherwise add BLACK, multiply WHITE. */
		wrong += !(focus == 3 ? av_is_color(add, 255, 0, 186, 255) : av_is_color(add, 0, 0, 0, 255));
		wrong += !(focus == 3 ? av_is_color(mul, 0, 0, 0, 255)
			   : focus == 6 ? av_is_color(mul, 200, 200, 200, 255) : av_is_color(mul, 255, 255, 255, 255));
		/* The caller scribbles on both and lets them go. */
		for (int k = 0; k < 2; k++) {
			int s = k ? mul : add;
			if (s > 0 && s != black && s != white && da_live_page(s)) {
				heap[s].page->values[k].i = 77 + i % 100;
				heap_unref(s);
			}
		}
	}
	AV_CHECK(!shared, "%d result(s) of 1000 were the page of a constant or of each other", shared);
	AV_CHECK(!wrong, "%d result(s) of 1000 did not hold the colour the function returns", wrong);
	AV_CHECK(av_is_color(black, 0, 0, 0, 255), "CASColor::BLACK is (%d,%d,%d) after the rounds",
		 heap[black].page->values[0].i, heap[black].page->values[1].i, heap[black].page->values[2].i);
	AV_CHECK(av_is_color(white, 255, 255, 255, 255), "CASColor::WHITE is (%d,%d,%d) after the rounds",
		 heap[white].page->values[0].i, heap[white].page->values[1].i, heap[white].page->values[2].i);
	AV_CHECK(heap[black].ref == bref && heap[white].ref == wref, "reference counts BLACK %d (was %d), WHITE %d (was %d)",
		 heap[black].ref, bref, heap[white].ref, wref);
	AV_CHECK(live_slots() == live, "live slots %zu after the rounds, %zu before", live_slots(), live);
	printf("AV2 checked\n");
}

/* FontProperty@GetCharSpriteProperty on `obj`, with `value` (> 0) standing
 * in for its m_casFont for the call. The caller owns the result. */
static int av_member_getter(int obj, int value)
{
	int m = gi_member(gi_struct("FontProperty"), "m_casFont");
	int saved = heap[obj].page->values[m].i;
	if (value > 0)
		heap[obj].page->values[m].i = value;
	int sp = stack_ptr;
	vm_call(gi_fn("FontProperty@GetCharSpriteProperty"), obj);
	int slot = stack_ptr - sp == 1 ? stack_pop().i : -1;
	stack_ptr = sp;
	heap[obj].page->values[m].i = saved;
	return slot;
}

/* The getter must hand out `value`'s own page with one more reference. */
static void av_expect_shared(const char *what, int obj, int value)
{
	if (!da_live_page(value)) {
		AV_CHECK(0, "%s: no live struct to return (slot %d)", what, value);
		return;
	}
	int ref = heap[value].ref;
	size_t live = live_slots();
	int got = av_member_getter(obj, value);
	AV_CHECK(got == value, "%s: the getter returned slot %d, want the member's own page (slot %d)", what, got, value);
	AV_CHECK(heap[value].ref == ref + (got == value), "%s: the page has %d reference(s) while the result lives, had %d", what, heap[value].ref, ref);
	AV_CHECK(live_slots() == live + (got != value && got > 0), "%s: %zu live slots, %zu before", what, live_slots(), live);
	if (got > 0 && da_live_page(got))
		heap_unref(got);
	AV_CHECK(da_live_page(value) && heap[value].ref == ref, "%s: %d reference(s) after releasing the result, had %d", what,
		 da_live_page(value) ? heap[value].ref : 0, ref);
}

/* The first struct whose members are all plain numbers or wraps, with at
 * least one wrap, and which has no constructor to run. */
static int av_wrap_struct(void)
{
	for (int i = 0; i < ain->nr_structures; i++) {
		struct ain_struct *t = &ain->structures[i];
		bool wrap = false, other = false;
		for (int k = 0; k < t->nr_members; k++) {
			switch (t->members[k].type.data) {
			case AIN_INT: case AIN_FLOAT: case AIN_BOOL: case AIN_ENUM:
				break;
			case AIN_WRAP:
				wrap = true;
				break;
			default:
				other = true;
			}
		}
		if (wrap && !other && t->nr_members <= 4 && t->constructor <= 0)
			return i;
	}
	return -1;
}

static void av_shared(void)
{
	gi_startup();
	int st = gi_struct("FontProperty"), m = gi_member(st, "m_casFont");
	int obj = vm_construct_struct(st);
	AV_CHECK(obj > 0 && gi_live_struct(obj, st), "no FontProperty object (slot %d)", obj);
	if (av_failures)
		return;
	/* As the script calls it: its own CASFont, which has two CASColor members. */
	int font = heap[obj].page->values[m].i;
	AV_CHECK(gi_live_struct(font, gi_struct("CASFont")), "the FontProperty has no CASFont (slot %d)", font);
	av_expect_shared("a struct with struct members (CASFont)", obj, font);
	/* One with a string member. */
	int other = alloc_struct(st);
	av_expect_shared("a struct with a string member (FontProperty)", obj, other);
	/* One with a wrap member. */
	int wst = av_wrap_struct();
	AV_CHECK(wst >= 0, "no struct of plain numbers and wraps");
	if (wst >= 0) {
		printf("  a struct with a wrap member: %s\n", gi_sname(wst));
		int w = alloc_struct(wst);
		av_expect_shared("a struct with a wrap member", obj, w);
		heap_unref(w);
	}
	heap_unref(other);
	heap_unref(obj);
	printf("AV3 checked\n");
}

/* The getter must hand out a copy of `value` (a struct of `st` with the
 * members v0, v1): another page, the same members, the source untouched. */
static void av_expect_copy(const char *what, int obj, int st, int value, int v0, int v1)
{
	int n = ain->structures[st].nr_members;
	int ref = heap[value].ref;
	int got = av_member_getter(obj, value);
	AV_CHECK(got != value, "%s: the getter returned the member's own page (slot %d)", what, value);
	AV_CHECK(gi_live_struct(got, st) && heap[got].page->nr_vars == n && heap[got].page->values[0].i == v0
		 && (n < 2 || heap[got].page->values[1].i == v1), "%s: the result is not a %s with the source's members", what, gi_sname(st));
	AV_CHECK(heap[value].ref == ref, "%s: the source has %d reference(s) while the result lives, had %d", what, heap[value].ref, ref);
	if (got > 0 && got != value && da_live_page(got)) {
		heap[got].page->values[0].i = v0 + 1000;
		AV_CHECK(heap[value].page->values[0].i == v0, "%s: writing to the result changed the source to %d", what, heap[value].page->values[0].i);
		heap_unref(got);
	} else if (got == value) {
		heap_unref(got);
	}
	AV_CHECK(da_live_page(value) && heap[value].ref == ref && heap[value].page->values[0].i == v0,
		 "%s: after releasing the result the source has %d reference(s), had %d", what, da_live_page(value) ? heap[value].ref : 0, ref);
}

static void av_enum_members(void)
{
	gi_startup();
	int st = gi_struct("FontProperty");
	int obj = vm_construct_struct(st);
	AV_CHECK(obj > 0 && gi_live_struct(obj, st), "no FontProperty object (slot %d)", obj);
	if (av_failures)
		return;
	int bst = gi_struct("BattleBonus"), rst = gi_struct("ReachedInfo");
	AV_CHECK(ain->structures[bst].nr_members == 1 && ain->structures[bst].members[0].type.data == AIN_ENUM,
		 "BattleBonus is not a struct of one enum");
	AV_CHECK(ain->structures[rst].nr_members == 2 && ain->structures[rst].members[0].type.data == AIN_INT
		 && ain->structures[rst].members[1].type.data == AIN_ENUM, "ReachedInfo is not a struct of an int and an enum");
	if (av_failures)
		return;
	int bb = alloc_struct(bst), ri = alloc_struct(rst);
	heap[bb].page->values[0].i = 3;
	heap[ri].page->values[0].i = 41;
	heap[ri].page->values[1].i = 2;
	av_expect_copy("a struct of an enum (BattleBonus)", obj, bst, bb, 3, 0);
	av_expect_copy("a struct of an int and an enum (ReachedInfo)", obj, rst, ri, 41, 2);
	size_t live = live_slots();
	for (int i = 0; i < 200; i++) {
		int got = av_member_getter(obj, i % 2 ? bb : ri);
		if (got > 0 && da_live_page(got))
			heap_unref(got);
	}
	AV_CHECK(live_slots() == live, "%zu live slots after 200 rounds, %zu before", live_slots(), live);
	heap_unref(bb);
	heap_unref(ri);
	heap_unref(obj);
	printf("AV4 checked\n");
}

/* A method of `obj` with one argument, which stays the caller's. vm_call
 * pushes the value it is given last, where a method's own last argument
 * belongs, so the object goes on the stack first. A result that is the
 * object itself (wrap<FontProperty>) carries a reference and is let go. */
static void av_method(const char *fn, int obj, int arg)
{
	int fno = gi_fn(fn), sp = stack_ptr;
	stack_push(obj);
	vm_call(fno, arg);
	if (ain->functions[fno].return_type.data != AIN_VOID && stack_ptr == sp + 1 && stack_peek(0).i == obj)
		heap_unref(obj);
	AV_CHECK(stack_ptr == sp + (ain->functions[fno].return_type.data != AIN_VOID), "%s left %d stack slot(s)", fn, stack_ptr - sp);
	stack_ptr = sp;
}
static void av_set_color(int obj, int color) { av_method("FontProperty@SetColorBody", obj, color); }

static void av_setter(void)
{
	gi_startup();
	int white = gi_gslot(gi_global("CASColor::WHITE"));
	int st = gi_struct("FontProperty"), cst = gi_struct("CASFont");
	int m = gi_member(st, "m_casFont"), cm = gi_member(cst, "Color");
	int obj = vm_construct_struct(st);
	AV_CHECK(obj > 0 && gi_live_struct(obj, st), "no FontProperty object (slot %d)", obj);
	AV_CHECK(av_is_color(white, 255, 255, 255, 255), "CASColor::WHITE is not (255,255,255,255) after the start-up");
	if (av_failures)
		return;
	int font = heap[obj].page->values[m].i;
	AV_CHECK(gi_live_struct(font, cst), "the FontProperty has no CASFont (slot %d)", font);
	if (av_failures)
		return;
	int mine = alloc_struct(gi_struct("CASColor"));
	union vm_value *mv = heap[mine].page->values;
	mv[0].i = 12; mv[1].i = 34; mv[2].i = 56; mv[3].i = 78;
	av_set_color(obj, white);			/* warm-up */
	int wref = heap[white].ref, mref = heap[mine].ref, oref = heap[obj].ref;
	size_t live = live_slots();

	av_set_color(obj, mine);
	int col = heap[font].page->values[cm].i;
	AV_CHECK(col != mine && av_is_color(col, 12, 34, 56, 78), "after SetColorBody(mine): the font's colour is slot %d (mine is %d), (12,34,56,78) %s",
		 col, mine, av_is_color(col, 12, 34, 56, 78) ? "yes" : "no");
	AV_CHECK(heap[mine].ref == mref, "the source has %d reference(s) after the call, had %d", heap[mine].ref, mref);
	av_set_color(obj, white);
	col = heap[font].page->values[cm].i;
	AV_CHECK(col != white && av_is_color(col, 255, 255, 255, 255), "after SetColorBody(WHITE): the font's colour is slot %d (WHITE is %d)", col, white);
	if (col != white && da_live_page(col)) {
		/* What the font does with its colour stays the font's. */
		heap[col].page->values[1].i = 1;
		AV_CHECK(av_is_color(white, 255, 255, 255, 255), "writing the font's colour changed CASColor::WHITE");
		heap[col].page->values[1].i = 255;
	}
	/* The setter on its own: `.LOCALREF value; A_REF; SR_ASSIGN; DELETE`. */
	av_method("FontProperty@ColorBody::set", obj, mine);
	col = heap[font].page->values[cm].i;
	AV_CHECK(col != mine && av_is_color(col, 12, 34, 56, 78), "after ColorBody::set(mine): the font's colour is slot %d (mine is %d)", col, mine);
	for (int i = 0; i < 300; i++) {
		if (i % 5 == 4)
			av_method("FontProperty@ColorBody::set", obj, i % 2 ? white : mine);
		else
			av_set_color(obj, i % 3 ? white : mine);
	}
	AV_CHECK(heap[white].ref == wref && heap[mine].ref == mref && heap[obj].ref == oref,
		 "reference counts after 300 calls: WHITE %d (was %d), the source %d (was %d), the object %d (was %d)",
		 heap[white].ref, wref, heap[mine].ref, mref, heap[obj].ref, oref);
	AV_CHECK(live_slots() == live, "%zu live slots after 300 calls, %zu before", live_slots(), live);
	AV_CHECK(av_is_color(white, 255, 255, 255, 255) && av_is_color(mine, 12, 34, 56, 78), "a source changed");
	heap_unref(mine);
	heap_unref(obj);
	printf("AV5 checked\n");
}

static void av_long_members(void)
{
	gi_startup();
	int st = gi_struct("FontProperty"), rst = gi_struct("ReachedInfo");
	int obj = vm_construct_struct(st);
	AV_CHECK(obj > 0 && gi_live_struct(obj, st), "no FontProperty object (slot %d)", obj);
	if (av_failures)
		return;
	int ri = alloc_struct(rst);
	heap[ri].page->values[0].i = 41;
	heap[ri].page->values[1].i = 2;
	struct ain_type *t = &ain->structures[rst].members[0].type;
	enum ain_data_type was = t->data;
	t->data = AIN_LONG_INT;
	av_expect_shared("a struct with a long int member (55)", obj, ri);
	t->data = AIN_ENUM2;
	av_expect_shared("a struct with a member of type 91", obj, ri);
	t->data = was;
	/* The fixture itself: as declared it is copied. */
	av_expect_copy("ReachedInfo as declared", obj, rst, ri, 41, 2);
	heap_unref(ri);
	heap_unref(obj);
	printf("AV6 checked\n");
}

static int av_run(const char *name, void (*fn)(void))
{
	fflush(stdout);
	pid_t pid = fork();
	if (pid == 0) {
		av_failures = 0;
		alarm(120);
		fn();
		fflush(stdout);
		_exit(av_failures ? 1 : 0);
	}
	int status = 0;
	waitpid(pid, &status, 0);
	bool ok = WIFEXITED(status) && WEXITSTATUS(status) == 0;
	printf("%s %s%s\n", name, ok ? "PASS" : "FAIL", WIFSIGNALED(status) ? " (crashed)" : "");
	fflush(stdout);
	return ok ? 0 : 1;
}

static int test_aref_value(void)
{
	int failed = 0;
	failed += av_run("AV1 a getter returning CASColor::BLACK gives a copy", av_constant);
	failed += av_run("AV2 500 rounds leave the colour constants alone", av_rounds);
	failed += av_run("AV3 guard: a struct with struct, string or wrap members keeps its page", av_shared);
	failed += av_run("AV4 a struct of an enum, and of an int and an enum, is copied", av_enum_members);
	failed += av_run("AV5 a property setter's bytecode releases the copies it is given", av_setter);
	failed += av_run("AV6 guard: a long int or type 91 member keeps the page shared", av_long_members);
	printf("AREF-VALUE %s: %d of 6 cases failed\n", failed ? "FAIL" : "PASS", failed);
	return failed ? 93 : 0;
}
