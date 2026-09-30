/* third-review: the third review of 6d39915..7cb6ba3
 * (research/gui-visual/base-ui.md §12).
 *
 *   RV1 The v14 pactex loader reads オン指針透過 into the parts' pass-cursor
 *       flag, as the native parser does (0x553650, called with the block at
 *       parts +0x84, reads it at 0x5547f4 as "== 1" into block +0x121, i.e.
 *       parts +0x1a5, the flag SetPassCursor 0x58f700 writes). Before: never
 *       read, every pactex parts blocked the cursor.
 *   RV2 A pactex decoration marked オン指針透過 1 in front of a button blocks
 *       neither its click nor its hover (the input target predicate 0x546e20:
 *       clickable || swiping || !pass); one marked 0 still blocks and the
 *       click becomes the whole-screen click. Before: the title characters,
 *       the worker card's texts and the choice cards' portraits swallowed
 *       the clicks of the buttons behind them.
 *   RV3 One hover target: the original's input update (0x546890) keeps the
 *       element found by 0x545e10 as the only hovered one, so a clickable
 *       parts that passes the cursor takes the hover and the click from the
 *       clickable parts behind it. Before: every parts under the cursor up
 *       to the first one blocking it was hovered.
 *   RV4 A destructor is not blacklisted for being long. Frees during a
 *       destructor are nested (heap_set_defer_frees), so the old
 *       "> 400K instructions" count included the nested destructors; after
 *       one long teardown the struct type's destructor was never run again.
 *       (A long nested teardown is simulated by advancing the instruction
 *       counter inside the nested destructor.)
 *   RV5 The same for real activity::detail::CActivityWrap: after one wrap's
 *       teardown took long (SceneWorkerList's took 487,655 instructions in
 *       the GUI), the next wrap's destructor must still call Release, or its
 *       activity and user components are never released (SceneHome's Footer
 *       stayed in CUserComponentManager; Executer.jaf:55 asserted).
 * Each case runs in its own fork. Only functions that exist before the fix
 * are referenced. No game assets or GL are used.
 */

static int rv_failures;
#define RV_CHECK(c, ...) do { if (!(c)) { printf("  FAIL: "); printf(__VA_ARGS__); printf("\n"); rv_failures++; } } while (0)

#define RV_PASS "\xa5\xaa\xa5\xf3\xd6\xb8\xe1\x98\xcd\xb8\xdf\x5e"	/* オン指針透過 */

/* A low-level component with an optional オン指針透過 leaf (value < 0: none). */
static struct ex_tree rv_comp(const char *name, int pass)
{
	struct ex_tree t = bu_branch(name, pass >= 0 ? 2 : 1);
	t.children[0] = bu_branch(BU_INFO, 1);
	t.children[0].children[0] = bu_sleaf(BU_TYPE, BU_LOW);
	if (pass >= 0)
		t.children[1] = bu_ileaf(RV_PASS, pass);
	return t;
}

static const char *rv_names[] = { "Pass1", "Pass0", "PassNone", "Pass2" };
static const int rv_values[] = { 1, 0, -1, 2 };

static struct ex *rv_resource(void)
{
	struct ex *e = xcalloc(1, sizeof(*e));
	e->nr_blocks = 1;
	e->blocks = xcalloc(1, sizeof(*e->blocks));
	e->blocks[0].name = cstr_to_string("PassActivity");
	e->blocks[0].val.type = EX_TREE;
	struct ex_tree *top = xcalloc(1, sizeof(*top));
	*top = bu_branch("PassActivity", 1);
	e->blocks[0].val.tree = top;
	top->children[0] = bu_comp("PassRoot", BU_LAYOUT, 0, 4);
	struct ex_tree *kids = &top->children[0].children[1];
	for (int i = 0; i < 4; i++)
		kids->children[i] = rv_comp(rv_names[i], rv_values[i]);
	return e;
}

/* Loads the synthetic activity and returns the four components' numbers. */
static void rv_load(int nums[4])
{
	struct ex *e = rv_resource();
	struct string *name = cstr_to_string("PassFixture");
	bool loaded = probe_load_activity_tree(name, e);
	free_string(name); ex_free(e);
	RV_CHECK(loaded, "synthetic activity did not load");
	int act = sf_str("PassFixture", -1);
	for (int i = 0; i < 4; i++)
		nums[i] = bu_number(act, rv_names[i]);
	heap_unref(act);
}

/* --- RV1 --- */

static void rv_loader(void)
{
	bu_postlink();
	int nums[4];
	rv_load(nums);
	const bool want[] = { true, false, false, false };
	for (int i = 0; i < 4; i++) {
		RV_CHECK(nums[i] > 0, "%s has no parts number", rv_names[i]);
		if (nums[i] <= 0)
			continue;
		int got = BU_PE("Parts_GetPartsPassCursor", BU_I(nums[i])).i;
		RV_CHECK(!!got == want[i], "Parts_GetPartsPassCursor(%s, オン指針透過 %d) = %d, want %d",
			 rv_names[i], rv_values[i], got, want[i]);
	}
	/* The HLL setter still overrides the loaded value. */
	if (nums[0] > 0) {
		BU_PE("Parts_SetPassCursor", BU_I(nums[0]), BU_I(0));
		int got = BU_PE("Parts_GetPartsPassCursor", BU_I(nums[0])).i;
		RV_CHECK(got == 0, "Parts_SetPassCursor(Pass1, false) then Get = %d", got);
	}
	printf("RV1 pactex オン指針透過: %s\n", rv_failures ? "FAIL" : "ok");
}

/* --- RV2 --- */

/* Makes a loaded component a top-level, shown, non-clickable rectangle. */
static struct parts *rv_decoration(int no, int x, int y, int w, int h, int z)
{
	PE_SetParentPartsNumber(no, 0);
	struct parts *p = br_box(no, x, y, w, h, true);
	PE_SetClickable(no, false);
	PE_SetZ(no, z);
	return p;
}

/* Moves the cursor to (x, y) and runs one input frame without a press. */
static void rv_hover(int x, int y)
{
	extern bool key_state[];
	mouse_set_pos(x, y);
	PE_UpdateComponent(0);
	PE_BeginInput();
	key_state[VK_LBUTTON] = false;
	PE_UpdateInputState(0);
	PE_EndInput();
}

static void rv_decorations(void)
{
	br_input_setup();
	int nums[4];
	rv_load(nums);
	int type, no;
	/* A button behind a decoration marked オン指針透過 1 (a title character,
	 * a worker card's name). */
	const int B = 9961, C = 9962;
	struct parts *b = br_box(B, 100, 100, 200, 80, true);
	struct parts *d1 = rv_decoration(nums[0], 50, 50, 400, 300, 50);
	int n = br_click(150, 140, &type, &no);
	RV_CHECK(n == 1 && type == 4 && no == B, "button behind a pass-through decoration: %d messages, first type %d parts %d; want 1, 4, %d",
		 n, type, no, B);
	rv_hover(160, 150);
	RV_CHECK(b->is_hovered && b->state == PARTS_STATE_HOVERED,
		 "button behind a pass-through decoration: hovered %d state %d; want 1, %d", b->is_hovered, b->state, PARTS_STATE_HOVERED);
	RV_CHECK(!d1->is_hovered, "the pass-through decoration is hovered");
	/* A decoration marked 0 blocks the cursor: the click becomes the
	 * whole-screen click and the button behind it is not hovered. */
	struct parts *c = br_box(C, 700, 100, 200, 80, true);
	struct parts *d0 = rv_decoration(nums[1], 650, 50, 400, 300, 50);
	heap[global_page_slot].page->values[2].i = 0;
	n = br_click(750, 140, &type, &no);
	RV_CHECK(n == 1 && type == 4 && no == 0, "button behind a blocking decoration: %d messages, first type %d parts %d; want 1, 4, 0",
		 n, type, no);
	rv_hover(760, 150);
	RV_CHECK(!c->is_hovered && d0->is_hovered, "blocking decoration: button hovered %d, decoration hovered %d; want 0, 1",
		 c->is_hovered, d0->is_hovered);
	/* Moving back over the first button leaves the blocker. */
	rv_hover(160, 150);
	RV_CHECK(b->is_hovered && !d0->is_hovered && !c->is_hovered, "after moving back: %d %d %d; want 1 0 0",
		 b->is_hovered, d0->is_hovered, c->is_hovered);
	printf("RV2 pass-through decorations: %s\n", rv_failures ? "FAIL" : "ok");
}

/* --- RV3 --- */

static void rv_one_hover(void)
{
	br_input_setup();
	int type, no;
	/* A clickable parts that passes the cursor in front of another
	 * clickable parts is the one input target. */
	const int F = 9971, K = 9972;
	struct parts *k = br_box(K, 100, 100, 200, 80, true);
	struct parts *f = br_box(F, 120, 110, 100, 50, true);
	PE_SetZ(F, 50);
	PE_SetPassCursor(F, true);
	rv_hover(150, 130);
	RV_CHECK(f->is_hovered && !k->is_hovered, "clickable pass-cursor parts in front: front hovered %d, back hovered %d; want 1, 0",
		 f->is_hovered, k->is_hovered);
	RV_CHECK(k->state == PARTS_STATE_DEFAULT, "the parts behind the target entered state %d", k->state);
	int n = br_click(150, 130, &type, &no);
	RV_CHECK(n == 1 && type == 4 && no == F, "click: %d messages, first type %d parts %d; want 1, 4, %d", n, type, no, F);
	/* Not clickable and passing the cursor: the parts behind is the target. */
	PE_SetClickable(F, false);
	rv_hover(150, 130);
	RV_CHECK(!f->is_hovered && k->is_hovered, "non-clickable pass-cursor parts in front: front hovered %d, back hovered %d; want 0, 1",
		 f->is_hovered, k->is_hovered);
	/* Beside both: nothing is hovered. */
	rv_hover(1000, 600);
	RV_CHECK(!f->is_hovered && !k->is_hovered, "beside both: %d %d", f->is_hovered, k->is_hovered);
	printf("RV3 one hover target: %s\n", rv_failures ? "FAIL" : "ok");
}

/* --- RV4 / RV5 --- */

static struct {
	bool armed;
	int unref_fno, unref_page, unref_slot;	/* on entering this frame, drop unref_slot */
	int bump_fno, bump_page;		/* on entering this frame, add bump instructions */
	bool unrefd, bumped;
	unsigned long long bump;
} rv_hook;

static void third_review_probe_step(void)
{
	if (!rv_hook.armed || !call_stack_ptr)
		return;
	struct function_call *top = &call_stack[call_stack_ptr - 1];
	if (!rv_hook.unrefd && rv_hook.unref_slot > 0 && top->fno == rv_hook.unref_fno
	    && top->struct_page == rv_hook.unref_page) {
		rv_hook.unrefd = true;
		heap_unref(rv_hook.unref_slot);
		return;
	}
	if (!rv_hook.bumped && top->fno == rv_hook.bump_fno && top->struct_page == rv_hook.bump_page) {
		rv_hook.bumped = true;
		insn_count += rv_hook.bump;
	}
}

static void rv_long_nested_teardown(void)
{
	int st = tr_struct("backlog::detail::CBackLogUnit");
	int dtor = tr_fn("backlog::detail::CBackLogUnit@1");
	/* The outer destructor lets go of the inner object; the inner one's
	 * teardown is long. */
	int outer = alloc_struct(st), inner = alloc_struct(st);
	rv_hook = (__typeof__(rv_hook)){ .armed = true, .unref_fno = dtor, .unref_page = outer, .unref_slot = inner,
		.bump_fno = dtor, .bump_page = inner, .bump = 450000 };
	unsigned long before = calls[dtor];
	heap_unref(outer);
	RV_CHECK(rv_hook.unrefd && rv_hook.bumped, "the nested teardown did not happen (%d %d)", rv_hook.unrefd, rv_hook.bumped);
	RV_CHECK(calls[dtor] - before == 2, "%lu destructor calls for the outer and inner objects, want 2", calls[dtor] - before);
	rv_hook.armed = false;
	/* The next objects of the type are destroyed with their destructor. */
	for (int i = 0; i < 2; i++) {
		int next = alloc_struct(st);
		before = calls[dtor];
		heap_unref(next);
		RV_CHECK(calls[dtor] - before == 1, "after a long teardown, object %d: %lu destructor calls, want 1 (blacklisted)",
			 i + 1, calls[dtor] - before);
	}
	/* The known-bad destructor stays listed (sound/3D layer manager). */
	int lm = ain_get_struct(ain, "parts::detail::CParts3DLayerManager");
	if (lm >= 0 && ain->structures[lm].destructor > 0) {
		int lmd = ain->structures[lm].destructor;
		int obj = alloc_struct(lm);
		before = calls[lmd];
		heap_unref(obj);
		RV_CHECK(calls[lmd] == before, "CParts3DLayerManager's destructor ran");
	}
	printf("RV4 long nested teardown: %s\n", rv_failures ? "FAIL" : "ok");
}

static void rv_activity_wrap(void)
{
	bu_postlink();
	int st = tr_struct("activity::detail::CActivityWrap");
	int dtor = tr_fn("activity::detail::CActivityWrap@1");
	int release = tr_fn("activity::detail::CActivityWrap@Release");
	RV_CHECK(ain->structures[st].destructor == dtor, "CActivityWrap destructor is %d, want %d",
		 ain->structures[st].destructor, dtor);
	/* A scene's wrap whose Release (its user components' teardown) is long. */
	int first = vm_construct_struct(st);
	RV_CHECK(heap_index_valid(first), "no CActivityWrap (%d)", first);
	rv_hook = (__typeof__(rv_hook)){ .armed = true, .bump_fno = release, .bump_page = first, .bump = 450000 };
	unsigned long d0 = calls[dtor], r0 = calls[release];
	heap_unref(first);
	RV_CHECK(rv_hook.bumped && calls[dtor] - d0 == 1 && calls[release] - r0 == 1,
		 "first wrap: destructor %lu, Release %lu (bumped %d); want 1, 1", calls[dtor] - d0, calls[release] - r0, rv_hook.bumped);
	rv_hook.armed = false;
	/* The next scene's wrap must still release its activity. */
	int second = vm_construct_struct(st);
	d0 = calls[dtor]; r0 = calls[release];
	heap_unref(second);
	RV_CHECK(calls[dtor] - d0 == 1 && calls[release] - r0 == 1,
		 "next wrap: destructor %lu, Release %lu; want 1, 1 (its activity and user components would stay)",
		 calls[dtor] - d0, calls[release] - r0);
	printf("RV5 activity wrap after a long teardown: %s\n", rv_failures ? "FAIL" : "ok");
}

static int rv_fork(const char *name, void (*fn)(void))
{
	fflush(stdout);
	pid_t pid = fork();
	if (pid == 0) {
		rv_failures = 0;
		alarm(60);
		fn();
		fflush(stdout);
		_exit(rv_failures ? 1 : 0);
	}
	int status = 0;
	waitpid(pid, &status, 0);
	bool ok = WIFEXITED(status) && WEXITSTATUS(status) == 0;
	if (!ok)
		printf("%s FAILED (%s %d)\n", name, WIFEXITED(status) ? "exit" : "signal",
		       WIFEXITED(status) ? WEXITSTATUS(status) : WTERMSIG(status));
	return ok ? 0 : 1;
}

static int test_third_review(void)
{
	int failed = 0;
	failed += rv_fork("RV1", rv_loader);
	failed += rv_fork("RV2", rv_decorations);
	failed += rv_fork("RV3", rv_one_hover);
	failed += rv_fork("RV4", rv_long_nested_teardown);
	failed += rv_fork("RV5", rv_activity_wrap);
	printf("THIRD-REVIEW %s: %d of 5 cases failed\n", failed ? "FAIL" : "PASS", failed);
	return failed ? 89 : 0;
}
