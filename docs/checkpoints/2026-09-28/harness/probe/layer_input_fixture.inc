/* layer-input: the v14 input update reads the active layer (controller)
 * alone (G3b, 2026-10-25). Read statically from the original:
 *  - 0x53e690 (the parts engine's update) calls 0x53c5b0 once for every
 *    controller of the stack (+0x88..+0x8c) and once for the system overlay
 *    (+0x84). The input flag goes to one of them only: the designated
 *    active controller (+0x94) or, while none is designated, the top of the
 *    stack (0x53e640), and the overlay only while it is the designated one;
 *    with input enabled (+0xb4, SetEnableInput, which the game does not
 *    call). Without the flag 0x53c5b0 skips the input update (0x53c61f) and
 *    still updates the widgets (0x53c240, 0x53c490).
 *  - 0x53c5b0 hands the input update the controller's own input manager
 *    (ecx = controller + 0x18, 0x53c64a) and the controller's parts: 0x5385b0
 *    on its root (controller + 4) collects every parts below the root, pre
 *    order (0x538622..0x538650); 0x546520 turns them into the elements of
 *    the update (each parts whose +0x90 is set, SetEnableInputProcess
 *    0x5349d0, gives its widget's elements, vtable +0x44).
 *  - 0x547c90 does everything on that list: the time since entered grows for
 *    the listed elements only (0x546620), the slots whose element is not
 *    listed are emptied (0x547c00, 0x548530), the focus is looked up in the
 *    list (0x545c70), the press target and the cursor walk (0x546890,
 *    0x5471d0) and the local wheel (0x547000) take listed elements only.
 *    The parts of every other controller are not touched: not entered, not
 *    left, no message, their time stands still.
 *  - AddController (0x58a6b0 -> 0x53d480 -> 0x53d3a0) inserts the new
 *    controller after the active one and designates it (+0x94); inside an
 *    input scope (depth +0x1fc > 0) it begins the new controller's input at
 *    once (0x53c750, +0x23c = 1, 0x546030). RemoveController (0x53d500)
 *    designates the one below when the active one goes.
 *  - BeginInput (0x58a720) ends (0x53e8b0, within a scope) and begins
 *    (0x53e830) the input of every controller and of the overlay: 0x5461a0
 *    and 0x546030 reset each controller's manager, 0x53c750 -> 0x538060
 *    takes the focus from every parts of each tree (LostFocus).
 *  - GetActiveParts (0x57bc90) reads the active controller (+0x94, or
 *    0x53e640 while none is designated): its +0x280 (0x53c6c0, 0 when
 *    empty), which every controller's update refreshes whatever its input
 *    flag (0x53c240 at 0x53c654, after the input update at 0x53c64d): the
 *    first parts of the controller's own tree whose widget has the focus
 *    (0x538180 on the root, vtable +0x18).
 *  - SetActiveController with an ID that is not there stores 0x53d870's
 *    null at +0x94 (0x57ba47), so the top of the stack acts, also after a
 *    MoveController (0x53d740 does not write +0x94).
 * The script opens every child scene this way: SceneContext@0 (FUNC 27189)
 * calls AFL_Parts_AddLayer(-1), the scene's WaitForClick runs
 * BeginInput/EndInput (parts::detail::WaitForClick, FUNC 9196), and
 * SceneContext@1 (FUNC 27191) calls AFL_Parts_EraseLayer; the
 * confirmation box (SceneYesNoDialog) included.
 *
 * The port keeps one input manager for all the controllers (its stored
 * position, its sampled buttons, its left slot and its focus), a port's
 * strategy. Every BeginInput and EndInput resets all the original's managers
 * as it resets the port's one, so the frames here are compared whole after a
 * BeginInput or an EndInput; between an AddLayer and the next BeginInput
 * (the box's opening motion) only the messages of parts are compared: the
 * new manager's whole-screen MouseMove of the original (0x546030 at
 * 0x53d3a0) is not asked for.
 *
 *   LI1 the confirmation box, closed with the cursor still on the button
 *       that opened it: while the box's layer is active the button is
 *       neither left nor entered and keeps its hover state and its time;
 *       after the box's layer is erased it goes on (OnCursor), with no
 *       MouseEnter again (hand-computed, frame by frame). GetActiveParts is
 *       the button after its click and 0 under the opening box, whose layer
 *       holds no focus (the button's focus is the scene layer's until the
 *       box's BeginInput).
 *   LI2 the cursor moved off the button while the box is open: the button
 *       keeps its hover state under the box and is left (MouseLeave) at the
 *       first update after the box closes.
 *   LI3 a press through a gap of the active layer: the inactive layer's
 *       button under the cursor is neither entered, focused, pressed nor
 *       clicked (the whole-screen click only); the active layer's button is.
 *   LI4 the wheel: a wheelable parts of the inactive layer under the cursor
 *       gets no local wheel, neither does a focus held in the inactive layer
 *       (the box's opening motion, before its BeginInput); the active
 *       layer's root does, and the active layer's wheelable parts. (The
 *       root stands for the original's last resort, the manager's own root
 *       element, this+0xc, as in wheel-events WE13: a port's
 *       representation.)
 *   LI5 a lower layer designated active: its parts are walked and pressed
 *       although a blocker of the layer above covers them; designating the
 *       upper layer again walks it alone, the lower parts keep their state,
 *       and GetActiveParts is 0 at once (the upper layer holds no focus).
 *   LI6 the system overlay: not walked while a stacked layer acts; walked
 *       alone while it is designated.
 *   LI7 a layer's membership follows the parent chain: a parts re-hung
 *       under a parts of the active layer is walked with it, the parts left
 *       in the scene's layer are not.
 *   LI11 no layer designated (SetActiveLayer with an ID that is not there):
 *       the top of the stack acts and the layer below is not walked; after
 *       the top layer is moved to the bottom, the new top acts.
 *   LI12 a layer added inside the scope while the left button is held on
 *       the scene's button: the slots whose element is not listed are
 *       emptied silently (0x547c90 -> 0x547c00 -> 0x548530 for +0x1ec,
 *       +0x200, +0x208, +0x210, +0x218; in the original the new layer's own
 *       manager never had the press), so the release sends the whole-screen
 *       click alone: no MouseClick and no GetClickNumber for the button,
 *       which keeps its down state (nothing walks it), and the button, made
 *       draggable, does not follow the cursor (the port's drag starts on the
 *       left slot). Parts messages only between the AddLayer and the next
 *       BeginInput, as for the box.
 * Guards, which pass before the change as well:
 *   LI8 one layer: a blocker of the same layer leaves the button and enters
 *       it again (the shape of hover-events HE10, a cover within one layer,
 *       through the script's layer functions in the multi-controller mode).
 *   LI9 v13 with two controllers: the old walk over every parts of every
 *       controller, its messages on the old queue, unchanged.
 *   LI10 the string rules: LI1's messages are the same under the GBK rule,
 *       the SJIS rule and the GB18030 flag alone (the rule does not depend
 *       on the game's character set; the English version is SJIS).
 * Synthetic rectangles, the script's own layer functions (01_AFL\Parts)
 * through the VM, the production PE_UpdateInputState and the v14 message
 * queue read raw. No GL, no game assets. Each case runs in its own fork.
 */

static int li_failures;
static bool li_quiet;	/* LI10's runs: record, do not check */
#define LI_CHECK(c, ...) do { if (!li_quiet && !(c)) { printf("  FAIL: "); printf(__VA_ARGS__); printf("\n"); li_failures++; } } while (0)
#define LI_FRAME(what, f, ...) do { if (!li_quiet) HE_FRAME(what, f, __VA_ARGS__); } while (0)
#define LI_NONE(what, f) do { if (!li_quiet) HE_NONE(what, f); } while (0)

enum { LI_B = 19501, LI_DIM, LI_Y, LI_P, LI_X, LI_O, LI_W, LI_R, LI_PD, LI_C, LI_L, LI_U, LI_T };

/* Every message the case reads, folded (LI10). */
static uint64_t li_digest = 1469598103934665603ull;
static int li_nmsg;
static void li_mix(int64_t v)
{
	li_digest = (li_digest ^ (uint64_t)v) * 1099511628211ull;
}
static void li_record(const struct he_frame *f)
{
	li_mix(-1000 - f->n);
	for (int i = 0; i < f->n && i < HE_MAXMSG; i++) {
		const struct he_msg *m = &f->m[i];
		li_mix(m->type); li_mix(m->no); li_mix(m->delegate); li_mix(m->unique); li_mix(m->n);
		for (int j = 0; j < m->n && j < 3; j++)
			li_mix(m->v[j]);
		li_nmsg++;
	}
}
static void li_record_parts(int no)
{
	struct parts *p = parts_try_get(no);
	li_mix(p ? 2000 + p->is_hovered * 10 + p->state : -2000);
	li_mix(p ? p->hover_time : -1);
}

static struct he_frame li_update(int x, int y, bool left)
{
	struct he_frame f = he_update(x, y, left, 16);
	li_record(&f);
	return f;
}

static struct he_frame li_drain(void)
{
	struct he_frame f = he_drain();
	li_record(&f);
	return f;
}

/* The frame without its whole-screen messages (parts 0). */
static struct he_frame li_parts_only(const struct he_frame *f)
{
	struct he_frame r = { 0 };
	for (int i = 0; i < f->n && i < HE_MAXMSG; i++)
		if (f->m[i].no != 0)
			r.m[r.n++] = f->m[i];
	return r;
}

/* Hover and focus messages (MouseEnter/Move/Leave, OnCursor, Focus,
 * LostFocus) in the frame. */
static int li_hover_count(const struct he_frame *f)
{
	int n = 0;
	for (int i = 0; i < f->n && i < HE_MAXMSG; i++)
		n += br_msg_is_hover(f->m[i].type);
	return n;
}

static const char *li_label(const char *c, const char *what)
{
	static char buf[8][160];
	static int k;
	k = (k + 1) % 8;
	snprintf(buf[k], sizeof buf[k], "%s %s", c, what);
	return buf[k];
}

static void li_state(const char *c, const char *what, int no, bool entered, int state, int time)
{
	struct parts *p = parts_get(no);
	LI_CHECK(p->is_hovered == entered && p->state == state && (time < 0 || p->hover_time == time),
		 "%s %s: parts %d entered %d state %d time %d; want %d, %d, %d", c, what, no,
		 p->is_hovered, p->state, p->hover_time, entered, state, time);
	li_record_parts(no);
}

/* The multi-controller mode the game runs in, the script's layer functions
 * ready (controller-id's setup), one layer: the scene's. */
static int li_scene;
static void li_setup(void)
{
	he_env();
	ci_setup();
	extern bool key_state[];
	key_state[VK_LBUTTON] = key_state[VK_RBUTTON] = key_state[VK_MBUTTON] = false;
	li_scene = ci_active();
}

static void li_button(int no, int x, int y, int w, int h, int delegate)
{
	br_box(no, x, y, w, h, true);
	cp_register_event(no, delegate, delegate * 10);
}

/* A dimmer over the screen: not clickable, does not pass the cursor. */
static void li_blocker(int no, int delegate)
{
	br_box(no, 0, 0, 1280, 720, true);
	PE_SetClickable(no, false);
	PE_SetPassCursor(no, false);
	cp_register_event(no, delegate, delegate * 10);
}

/* BeginInput and two updates beside everything: the stored position settles. */
static void li_scope(void)
{
	PE_BeginInput();
	li_update(640, 600, false);
	li_update(640, 600, false);
	li_drain();
}

/* LI1 and LI2 up to the open box: B in the scene's layer is entered,
 * pressed and clicked; the click's handler opens the box (a new layer with a
 * dimmer and a button), one update of its opening motion, the box's
 * BeginInput and three updates with the cursor where B is. Returns the box's
 * layer. */
static int li_open_box(const char *c)
{
	li_setup();
	li_button(LI_B, 100, 100, 200, 80, 3);
	li_scope();
	struct he_frame f = li_update(150, 140, false);
	LI_FRAME(li_label(c, "entering the button"), f,
		{ 1, 0, 2, 150, 140 }, { 0, LI_B, 2, 150, 140 }, { 6, LI_B, 3, 150, 140, 0 }, { 1, LI_B, 2, 150, 140 });
	f = li_update(150, 140, true);
	LI_FRAME(li_label(c, "the press"), f, { 19, LI_B, 0 }, { 6, LI_B, 3, 150, 140, 16 });
	f = li_update(150, 140, false);
	LI_FRAME(li_label(c, "the click"), f, { 4, 0, 3, 150, 140, 1 }, { 6, LI_B, 3, 150, 140, 32 }, { 4, LI_B, 3, 150, 140, 1 });
	LI_CHECK(he_hll_int("GetActiveParts") == LI_B, "%s GetActiveParts %d after the click, want %d", c,
		 he_hll_int("GetActiveParts"), LI_B);

	/* the click's handler: SceneContext@0 -> AFL_Parts_AddLayer(-1) */
	int box = ci_add(-1);
	LI_CHECK(box != li_scene && ci_active() == box, "%s AddLayer: layer %d, active %d (scene %d)", c, box, ci_active(), li_scene);
	li_blocker(LI_DIM, 5);
	li_button(LI_Y, 700, 400, 200, 80, 6);
	LI_CHECK(parts_get(LI_DIM)->controller_no == box && parts_get(LI_B)->controller_no == li_scene,
		 "%s the box's parts are on layer %d, the button on %d", c, parts_get(LI_DIM)->controller_no,
		 parts_get(LI_B)->controller_no);
	f = li_drain();
	LI_NONE(li_label(c, "AddLayer"), f);
	/* the opening motion (Motion::Join) updates before the box's
	 * WaitForClick: the box's layer is active and has begun (0x53d3a0) */
	f = li_update(150, 140, false);
	struct he_frame g = li_parts_only(&f);
	LI_FRAME(li_label(c, "the box's opening update (parts messages)"), g,
		{ 0, LI_DIM, 2, 150, 140 }, { 6, LI_DIM, 3, 150, 140, 0 }, { 1, LI_DIM, 2, 150, 140 });
	li_state(c, "the button under the opening box", LI_B, true, PARTS_STATE_HOVERED, 32);
	/* the box's layer has no focused parts (0x53c240 refreshed its +0x280
	 * in this update); the button is the scene layer's focus */
	LI_CHECK(he_hll_int("GetActiveParts") == 0, "%s GetActiveParts %d under the opening box, want 0", c,
		 he_hll_int("GetActiveParts"));
	/* the box's WaitForClick: BeginInput within the scope takes the focus */
	PE_BeginInput();
	f = li_drain();
	LI_FRAME(li_label(c, "the box's BeginInput"), f, { 20, LI_B, 0 });
	f = li_update(150, 140, false);
	LI_FRAME(li_label(c, "under the box"), f, { 1, 0, 2, 150, 140 }, { 6, LI_DIM, 3, 150, 140, 16 });
	f = li_update(150, 140, false);
	LI_FRAME(li_label(c, "under the box (2)"), f, { 1, 0, 2, 150, 140 }, { 6, LI_DIM, 3, 150, 140, 32 });
	f = li_update(150, 140, false);
	LI_FRAME(li_label(c, "under the box (3)"), f, { 6, LI_DIM, 3, 150, 140, 48 });
	li_state(c, "the button under the box", LI_B, true, PARTS_STATE_HOVERED, 32);
	return box;
}

/* The box closes (its WaitForClick's EndInput, then SceneContext@1 ->
 * AFL_Parts_EraseLayer); the scene's layer is active again. */
static void li_close_box(const char *c, int box)
{
	PE_EndInput();
	struct he_frame f = li_drain();
	LI_NONE(li_label(c, "the box's EndInput"), f);
	ci_erase(box);
	f = li_drain();
	/* The port's own behaviour, kept as a guard: the original's
	 * RemoveController (0x53d500) destroys the controller (vtable [0] with
	 * 1) with its elements; whether that sends an entered or focused
	 * element anything was not followed. */
	LI_CHECK(li_hover_count(&f) == 0, "%s EraseLayer queued %d hover or focus message(s)", c, li_hover_count(&f));
	LI_CHECK(ci_active() == li_scene && ci_len() == 1, "%s after EraseLayer: active %d, %d layer(s); want %d, 1",
		 c, ci_active(), ci_len(), li_scene);
}

static int li_enters(const struct he_frame *f, int no)
{
	return he_count(f, 0, no);
}

/* LI1: closed with the cursor still on the button (Esc or the right
 * button): no MouseEnter again, the time goes on from where it stood. */
static void li_box_cursor_stays(void)
{
	int box = li_open_box("LI1");
	li_close_box("LI1", box);
	struct he_frame f = li_update(150, 140, false);
	int enters = li_enters(&f, LI_B);
	LI_FRAME("LI1 closed, the cursor on the button", f, { 1, 0, 2, 150, 140 }, { 6, LI_B, 3, 150, 140, 48 });
	f = li_update(150, 140, false);
	enters += li_enters(&f, LI_B);
	LI_FRAME("LI1 closed (2)", f, { 1, 0, 2, 150, 140 }, { 6, LI_B, 3, 150, 140, 64 });
	f = li_update(150, 140, false);
	enters += li_enters(&f, LI_B);
	LI_FRAME("LI1 closed (3)", f, { 6, LI_B, 3, 150, 140, 80 });
	LI_CHECK(enters == 0, "LI1 the button got %d MouseEnter after the box closed, want 0", enters);
	li_state("LI1", "after the box", LI_B, true, PARTS_STATE_HOVERED, 80);
	PE_EndInput();
}

/* LI2: the cursor moved off the button while the box is open. */
static void li_box_cursor_moves(void)
{
	int box = li_open_box("LI2");
	struct he_frame f = li_update(900, 300, false);
	LI_FRAME("LI2 the cursor moves under the box", f, { 1, 0, 2, 900, 300 }, { 6, LI_DIM, 3, 900, 300, 64 }, { 1, LI_DIM, 2, 900, 300 });
	f = li_update(900, 300, false);
	LI_FRAME("LI2 the cursor moved under the box", f, { 6, LI_DIM, 3, 900, 300, 80 });
	li_state("LI2", "the button with the cursor away under the box", LI_B, true, PARTS_STATE_HOVERED, 32);
	li_close_box("LI2", box);
	f = li_update(900, 300, false);
	LI_FRAME("LI2 closed, the cursor elsewhere", f, { 1, 0, 2, 900, 300 }, { 2, LI_B, 2, 900, 300 });
	li_state("LI2", "left after the box", LI_B, false, PARTS_STATE_DEFAULT, -1);
	f = li_update(900, 300, false);
	LI_FRAME("LI2 closed (2)", f, { 1, 0, 2, 900, 300 });
	f = li_update(900, 300, false);
	LI_NONE("LI2 closed (3)", f);
	/* back on it: entered anew where it last saw the cursor (no MouseMove) */
	f = li_update(150, 140, false);
	LI_FRAME("LI2 back on the button", f, { 1, 0, 2, 150, 140 }, { 0, LI_B, 2, 150, 140 }, { 6, LI_B, 3, 150, 140, 0 });
	PE_EndInput();
}

/* LI3: the box's layer has a button P away from B and nothing over B. */
static void li_press_gap(void)
{
	li_setup();
	li_button(LI_B, 100, 100, 200, 80, 3);
	li_scope();
	int box = ci_add(-1);
	li_button(LI_P, 700, 400, 200, 80, 7);
	PE_BeginInput();
	struct he_frame f = li_drain();
	LI_NONE("LI3 the box's BeginInput", f);
	f = li_update(150, 140, false);
	LI_FRAME("LI3 the cursor over the scene's button", f, { 1, 0, 2, 150, 140 });
	f = li_update(150, 140, false);
	LI_FRAME("LI3 (2)", f, { 1, 0, 2, 150, 140 });
	f = li_update(150, 140, false);
	LI_NONE("LI3 (3)", f);
	f = li_update(150, 140, true);
	LI_NONE("LI3 the press there", f);
	f = li_update(150, 140, false);
	LI_FRAME("LI3 the release there: the whole-screen click alone", f, { 4, 0, 3, 150, 140, 1 });
	LI_CHECK(he_hll_int("GetClickNumber") == 0 && he_hll_int("GetActiveParts") == 0,
		 "LI3 GetClickNumber %d, GetActiveParts %d; want 0, 0", he_hll_int("GetClickNumber"), he_hll_int("GetActiveParts"));
	li_state("LI3", "the scene's button", LI_B, false, PARTS_STATE_DEFAULT, -1);
	/* the box's own button works */
	f = li_update(750, 440, false);
	LI_FRAME("LI3 entering the box's button", f,
		{ 1, 0, 2, 750, 440 }, { 0, LI_P, 2, 750, 440 }, { 6, LI_P, 3, 750, 440, 0 }, { 1, LI_P, 2, 750, 440 });
	f = li_update(750, 440, true);
	LI_FRAME("LI3 pressing the box's button", f, { 19, LI_P, 0 }, { 6, LI_P, 3, 750, 440, 16 });
	f = li_update(750, 440, false);
	LI_FRAME("LI3 clicking the box's button", f, { 4, 0, 3, 750, 440, 1 }, { 6, LI_P, 3, 750, 440, 32 }, { 4, LI_P, 3, 750, 440, 1 });
	LI_CHECK(he_hll_int("GetClickNumber") == LI_P, "LI3 GetClickNumber %d after the box's button, want %d", he_hll_int("GetClickNumber"), LI_P);
	(void)box;
	PE_EndInput();
	PE_EndInput();
}

/* LI4: W, wheelable, in the scene's layer; P, wheelable, in the box's. */
static void li_wheel(void)
{
	he_env();
	LI_CHECK(SDL_Init(SDL_INIT_EVENTS) == 0, "LI4 SDL events init: %s", SDL_GetError());
	li_setup();
	input_modal_closed();
	SDL_FlushEvents(SDL_FIRSTEVENT, SDL_LASTEVENT);
	li_button(LI_W, 100, 100, 160, 80, 8);
	we_set(LI_W, true);
	if (!parts_get(LI_W)->wheelable) {
		printf("LAYER-INPUT LI4 setup: Parts_SetWheelable did not set the flag\n");
		exit(2);
	}
	PE_BeginInput();
	we_update(640, 600);
	we_update(640, 600);
	mouse_clear_wheel();
	/* W becomes the focus */
	he_update(150, 140, false, 16);
	he_update(150, 140, true, 16);
	he_update(150, 140, false, 16);
	he_drain();
	int box = ci_add(-1);
	li_button(LI_P, 700, 400, 160, 80, 9);
	we_set(LI_P, true);
	int root = box;
	LI_CHECK(parts_try_get(root) && parts_is_controller_root(parts_try_get(root)), "LI4 the box's layer %d has no root parts", box);
	int to_w = 0;
	/* the opening motion: W is still the focus, held in the scene's layer */
	struct he_frame f = we_tick(150, 140, 1, 0);
	to_w += he_count(&f, 3, LI_W);
	if (!li_quiet) we_target("LI4 the opening box, over the scene's wheelable focus", &f, root, 1, 0);
	PE_BeginInput();
	f = he_drain();
	LI_FRAME("LI4 the box's BeginInput", f, { 20, LI_W, 0 });
	we_update(150, 140);
	we_update(150, 140);
	mouse_clear_wheel();
	f = we_tick(150, 140, 0, 2);
	to_w += he_count(&f, 3, LI_W);
	if (!li_quiet) we_target("LI4 under the box, over the scene's wheelable parts", &f, root, 0, 2);
	f = we_tick(750, 440, 1, 0);
	to_w += he_count(&f, 3, LI_W);
	if (!li_quiet) we_target("LI4 over the box's wheelable parts", &f, LI_P, 1, 0);
	LI_CHECK(to_w == 0, "LI4 the scene's wheelable parts got %d local wheel message(s) while the box was active", to_w);
	PE_EndInput();
	PE_EndInput();
}

/* LI5: B in the scene's layer; X, a blocker over the screen, in the layer
 * above; the scene's layer designated active. */
static void li_lower_active(void)
{
	li_setup();
	li_button(LI_B, 100, 100, 200, 80, 3);
	li_scope();
	int top = ci_add(-1);
	li_blocker(LI_X, 5);
	int prev = ci_set_active(li_scene);
	LI_CHECK(prev == top && ci_active() == li_scene, "LI5 SetActiveLayer: previous %d, active %d", prev, ci_active());
	PE_BeginInput();
	struct he_frame f = li_drain();
	LI_NONE("LI5 BeginInput", f);
	f = li_update(150, 140, false);
	LI_FRAME("LI5 the lower layer active, the cursor on its button under the upper blocker", f,
		{ 1, 0, 2, 150, 140 }, { 0, LI_B, 2, 150, 140 }, { 6, LI_B, 3, 150, 140, 0 }, { 1, LI_B, 2, 150, 140 });
	f = li_update(150, 140, false);
	LI_FRAME("LI5 (2)", f, { 1, 0, 2, 150, 140 }, { 6, LI_B, 3, 150, 140, 16 });
	f = li_update(150, 140, false);
	LI_FRAME("LI5 (3)", f, { 6, LI_B, 3, 150, 140, 32 });
	f = li_update(150, 140, true);
	LI_FRAME("LI5 the press", f, { 19, LI_B, 0 }, { 6, LI_B, 3, 150, 140, 48 });
	f = li_update(150, 140, false);
	LI_FRAME("LI5 the click", f, { 4, 0, 3, 150, 140, 1 }, { 6, LI_B, 3, 150, 140, 64 }, { 4, LI_B, 3, 150, 140, 1 });
	LI_CHECK(he_hll_int("GetClickNumber") == LI_B, "LI5 GetClickNumber %d, want %d", he_hll_int("GetClickNumber"), LI_B);
	li_state("LI5", "the upper blocker while the lower layer acts", LI_X, false, PARTS_STATE_DEFAULT, -1);
	LI_CHECK(he_hll_int("GetActiveParts") == LI_B, "LI5 GetActiveParts %d with the lower layer active, want %d",
		 he_hll_int("GetActiveParts"), LI_B);
	/* the upper layer designated again: GetActiveParts reads its +0x280,
	 * which holds no focus (B's is the lower layer's) */
	ci_set_active(top);
	LI_CHECK(he_hll_int("GetActiveParts") == 0, "LI5 GetActiveParts %d with the upper layer designated, want 0",
		 he_hll_int("GetActiveParts"));
	PE_BeginInput();
	f = li_drain();
	LI_FRAME("LI5 BeginInput, the upper layer active", f, { 20, LI_B, 0 });
	f = li_update(150, 140, false);
	LI_FRAME("LI5 the upper layer active", f, { 1, 0, 2, 150, 140 }, { 0, LI_X, 2, 150, 140 }, { 6, LI_X, 3, 150, 140, 0 }, { 1, LI_X, 2, 150, 140 });
	f = li_update(150, 140, false);
	LI_FRAME("LI5 the upper layer active (2)", f, { 1, 0, 2, 150, 140 }, { 6, LI_X, 3, 150, 140, 16 });
	li_state("LI5", "the lower button while the upper layer acts", LI_B, true, PARTS_STATE_HOVERED, 64);
	PE_EndInput();
	PE_EndInput();
	PE_EndInput();
}

/* LI6: O, a blocker over the screen, on the system overlay. */
static void li_overlay(void)
{
	li_setup();
	li_button(LI_B, 100, 100, 200, 80, 3);
	li_scope();
	int overlay = PE_get_system_controller();
	ci_set_active(overlay);
	li_blocker(LI_O, 4);
	ci_set_active(li_scene);
	if (parts_get(LI_O)->controller_no != overlay || ci_active() != li_scene) {
		printf("LAYER-INPUT LI6 setup: O on controller %d, active %d\n", parts_get(LI_O)->controller_no, ci_active());
		exit(2);
	}
	struct he_frame f = li_drain();
	LI_NONE("LI6 creating the overlay's parts", f);
	f = li_update(150, 140, false);
	LI_FRAME("LI6 the scene's layer active, the cursor on its button under the overlay", f,
		{ 1, 0, 2, 150, 140 }, { 0, LI_B, 2, 150, 140 }, { 6, LI_B, 3, 150, 140, 0 }, { 1, LI_B, 2, 150, 140 });
	f = li_update(150, 140, false);
	LI_FRAME("LI6 (2)", f, { 6, LI_B, 3, 150, 140, 16 });
	li_state("LI6", "the overlay's blocker while the scene's layer acts", LI_O, false, PARTS_STATE_DEFAULT, -1);
	/* the overlay designated */
	ci_set_active(overlay);
	PE_BeginInput();
	f = li_drain();
	LI_NONE("LI6 BeginInput, the overlay active", f);
	f = li_update(150, 140, false);
	LI_FRAME("LI6 the overlay active", f, { 1, 0, 2, 150, 140 }, { 0, LI_O, 2, 150, 140 }, { 6, LI_O, 3, 150, 140, 0 }, { 1, LI_O, 2, 150, 140 });
	f = li_update(150, 140, false);
	LI_FRAME("LI6 the overlay active (2)", f, { 1, 0, 2, 150, 140 }, { 6, LI_O, 3, 150, 140, 16 });
	f = li_update(150, 140, false);
	LI_FRAME("LI6 the overlay active (3)", f, { 6, LI_O, 3, 150, 140, 32 });
	li_state("LI6", "the scene's button while the overlay acts", LI_B, true, PARTS_STATE_HOVERED, 16);
	/* the scene's layer again */
	ci_set_active(li_scene);
	PE_BeginInput();
	f = li_drain();
	LI_NONE("LI6 BeginInput, the scene's layer active again", f);
	f = li_update(150, 140, false);
	LI_FRAME("LI6 the scene's layer again", f, { 1, 0, 2, 150, 140 }, { 6, LI_B, 3, 150, 140, 32 });
	li_state("LI6", "the overlay's blocker after it acted", LI_O, true, PARTS_STATE_HOVERED, 32);
	PE_EndInput();
	PE_EndInput();
	PE_EndInput();
}

/* LI7: R in the scene's layer is re-hung under PD of the box's layer. */
static void li_membership(void)
{
	li_setup();
	li_button(LI_B, 100, 100, 200, 80, 3);
	li_button(LI_R, 400, 100, 200, 80, 11);
	li_scope();
	int box = ci_add(-1);
	br_box(LI_PD, 0, 0, 10, 10, true);
	PE_SetClickable(LI_PD, false);
	PE_SetPassCursor(LI_PD, true);
	PE_SetParentPartsNumber(LI_R, LI_PD);
	if (parts_get(LI_R)->controller_no != box || parts_get(LI_B)->controller_no != li_scene) {
		printf("LAYER-INPUT LI7 setup: R on %d, B on %d (box %d, scene %d)\n", parts_get(LI_R)->controller_no,
		       parts_get(LI_B)->controller_no, box, li_scene);
		exit(2);
	}
	PE_BeginInput();
	struct he_frame f = li_drain();
	LI_NONE("LI7 the box's BeginInput", f);
	f = li_update(450, 140, false);
	LI_FRAME("LI7 the re-hung parts", f, { 1, 0, 2, 450, 140 }, { 0, LI_R, 2, 450, 140 }, { 6, LI_R, 3, 450, 140, 0 }, { 1, LI_R, 2, 450, 140 });
	f = li_update(150, 140, false);
	LI_FRAME("LI7 the scene's button", f, { 1, 0, 2, 150, 140 }, { 2, LI_R, 2, 150, 140 });
	f = li_update(150, 140, false);
	LI_NONE("LI7 the scene's button (2)", f);
	li_state("LI7", "the scene's button", LI_B, false, PARTS_STATE_DEFAULT, -1);
	PE_EndInput();
	PE_EndInput();
}

/* LI11: B in the scene's layer, T in the layer above; no layer designated
 * (0x57ba47 stores 0x53d870's null): the top acts (0x53e690 -> 0x53e640). */
static void li_none_designated(void)
{
	li_setup();
	li_button(LI_B, 100, 100, 200, 80, 3);
	li_scope();
	int top = ci_add(-1);
	li_button(LI_T, 700, 400, 200, 80, 12);
	ci_set_active(4711);
	LI_CHECK(ci_active() == top, "LI11 none designated: active %d, want the top layer %d", ci_active(), top);
	PE_BeginInput();
	struct he_frame f = li_drain();
	LI_NONE("LI11 BeginInput", f);
	f = li_update(150, 140, false);
	LI_FRAME("LI11 none designated, the cursor on the lower layer's button", f, { 1, 0, 2, 150, 140 });
	f = li_update(150, 140, false);
	LI_FRAME("LI11 (2)", f, { 1, 0, 2, 150, 140 });
	f = li_update(750, 440, false);
	LI_FRAME("LI11 none designated, the top layer's button", f,
		{ 1, 0, 2, 750, 440 }, { 0, LI_T, 2, 750, 440 }, { 6, LI_T, 3, 750, 440, 0 }, { 1, LI_T, 2, 750, 440 });
	li_state("LI11", "the lower layer's button", LI_B, false, PARTS_STATE_DEFAULT, -1);
	/* the top layer moved to the bottom: still none designated, the
	 * scene's layer is the top now (a BeginInput resets the managers) */
	ci_move(top, 0);
	LI_CHECK(ci_active() == li_scene, "LI11 after the move: active %d, want the new top layer %d", ci_active(), li_scene);
	PE_BeginInput();
	f = li_drain();
	LI_NONE("LI11 BeginInput after the move", f);
	f = li_update(750, 440, false);
	LI_FRAME("LI11 after the move, the cursor on the moved layer's button", f, { 1, 0, 2, 750, 440 });
	f = li_update(150, 140, false);
	LI_FRAME("LI11 after the move, the scene's button", f,
		{ 1, 0, 2, 150, 140 }, { 0, LI_B, 2, 150, 140 }, { 6, LI_B, 3, 150, 140, 0 }, { 1, LI_B, 2, 150, 140 });
	li_state("LI11", "the moved layer's button", LI_T, true, PARTS_STATE_HOVERED, 0);
	PE_EndInput();
	PE_EndInput();
	PE_EndInput();
}

/* LI12: the left button held on B in the scene's layer when a layer is
 * added inside the scope (0x53d3a0 begins it at once). B is draggable: the
 * port's drag (input.c, the drag messages are not sent in v14) starts on the
 * left slot and goes with it, so B no longer follows the cursor. */
static void li_held_layer(void)
{
	li_setup();
	li_button(LI_B, 100, 100, 200, 80, 3);
	PE_SetDrag(LI_B, true);
	li_scope();
	struct he_frame f = li_update(150, 140, false);
	LI_FRAME("LI12 entering the button", f,
		{ 1, 0, 2, 150, 140 }, { 0, LI_B, 2, 150, 140 }, { 6, LI_B, 3, 150, 140, 0 }, { 1, LI_B, 2, 150, 140 });
	f = li_update(150, 140, true);
	LI_FRAME("LI12 the press", f, { 19, LI_B, 0 }, { 6, LI_B, 3, 150, 140, 16 });
	int box = ci_add(-1);
	li_button(LI_Y, 700, 400, 200, 80, 6);
	LI_CHECK(ci_active() == box, "LI12 AddLayer: active %d, want %d", ci_active(), box);
	f = li_drain();
	LI_NONE("LI12 AddLayer while the button is held", f);
	f = li_update(150, 140, true);
	struct he_frame g = li_parts_only(&f);
	LI_NONE("LI12 held under the new layer (parts messages)", g);
	li_state("LI12", "the held button under the new layer", LI_B, true, PARTS_STATE_CLICKED, 16);
	f = li_update(170, 160, true);
	g = li_parts_only(&f);
	LI_NONE("LI12 held and moved under the new layer (parts messages)", g);
	struct parts *b = parts_get(LI_B);
	LI_CHECK(b->local.pos.x == 100 && b->local.pos.y == 100, "LI12 the held button moved to (%d, %d) under the new layer, want (100, 100)",
		 b->local.pos.x, b->local.pos.y);
	f = li_update(170, 160, false);
	g = li_parts_only(&f);
	LI_NONE("LI12 the release under the new layer (parts messages)", g);
	LI_CHECK(he_count(&f, 4, 0) == 1, "LI12 the release: %d whole-screen click(s), want 1", he_count(&f, 4, 0));
	LI_CHECK(he_hll_int("GetClickNumber") == 0, "LI12 GetClickNumber %d after the release, want 0", he_hll_int("GetClickNumber"));
	li_state("LI12", "the button after the release", LI_B, true, PARTS_STATE_CLICKED, 16);
	PE_EndInput();
}

/* LI8: hover-events HE10 in the multi-controller mode, all in one layer. */
static void li_one_layer(void)
{
	li_setup();
	li_button(LI_B, 100, 100, 200, 80, 3);
	br_box(LI_C, 0, 0, 1280, 720, true);
	PE_SetZ(LI_C, 100);
	PE_SetShow(LI_C, false);
	cp_register_event(LI_C, 5, 50);
	li_scope();
	struct he_frame f = li_update(150, 140, false);
	LI_FRAME("LI8 entering the button", f,
		{ 1, 0, 2, 150, 140 }, { 0, LI_B, 2, 150, 140 }, { 6, LI_B, 3, 150, 140, 0 }, { 1, LI_B, 2, 150, 140 });
	f = li_update(150, 140, true);
	LI_FRAME("LI8 the press", f, { 19, LI_B, 0 }, { 6, LI_B, 3, 150, 140, 16 });
	f = li_update(150, 140, false);
	LI_FRAME("LI8 the click", f, { 4, 0, 3, 150, 140, 1 }, { 6, LI_B, 3, 150, 140, 32 }, { 4, LI_B, 3, 150, 140, 1 });
	PE_BeginInput();
	PE_SetShow(LI_C, true);
	f = li_drain();
	LI_FRAME("LI8 the cover's BeginInput", f, { 20, LI_B, 0 });
	f = li_update(150, 140, false);
	LI_FRAME("LI8 under the cover of the same layer", f, { 1, 0, 2, 150, 140 },
		{ 0, LI_C, 2, 150, 140 }, { 6, LI_C, 3, 150, 140, 0 }, { 1, LI_C, 2, 150, 140 }, { 2, LI_B, 2, 150, 140 });
	li_state("LI8", "under the cover", LI_B, false, PARTS_STATE_DEFAULT, -1);
	li_update(818, 467, false);
	PE_SetShow(LI_C, false);
	PE_EndInput();
	f = li_drain();
	LI_NONE("LI8 the cover's EndInput", f);
	f = li_update(1100, 300, false);
	LI_FRAME("LI8 closed, the cursor elsewhere", f, { 1, 0, 2, 1100, 300 }, { 2, LI_C, 2, 1100, 300 });
	f = li_update(1100, 300, false);
	LI_FRAME("LI8 closed (2)", f, { 1, 0, 2, 1100, 300 });
	f = li_update(150, 140, false);
	LI_FRAME("LI8 back on the button", f, { 1, 0, 2, 150, 140 }, { 0, LI_B, 2, 150, 140 }, { 6, LI_B, 3, 150, 140, 0 });
	PE_EndInput();
}

/* LI9: v13, two controllers pushed: L on the lower one, U on the upper
 * (the active) one. The old walk goes over every parts. */
static int li_old_msgs(int *types, int *nos, int max)
{
	int n = 0;
	for (int t; (t = PE_GetMessageType()) != 0; PE_PopMessage()) {
		if (n < max) {
			types[n] = t;
			nos[n] = PE_GetMessagePartsNumber();
		}
		n++;
	}
	return n;
}
static int li_old_count(int n, const int *types, const int *nos, int type, int no)
{
	int k = 0;
	for (int i = 0; i < n && i < 16; i++)
		k += types[i] == type && nos[i] == no;
	return k;
}
static void li_v13(void)
{
	he_env();
	br_input_setup();
	int version = ain->version;
	ain->version = 13;
	extern bool parts_multi_controller;
	parts_multi_controller = true;
	probe_init_parts_table();
	extern bool key_state[];
	key_state[VK_LBUTTON] = false;
	br_box(LI_L, 100, 100, 200, 80, true);
	PE_AddController(-1);
	br_box(LI_U, 700, 400, 200, 80, true);
	if (parts_get(LI_L)->controller_no != 0 || parts_get(LI_U)->controller_no != 1 || PE_get_active_controller() != 1) {
		printf("LAYER-INPUT LI9 setup: L on %d, U on %d, active %d\n", parts_get(LI_L)->controller_no,
		       parts_get(LI_U)->controller_no, PE_get_active_controller());
		exit(2);
	}
	PE_BeginInput();
	PE_ReleaseMessage();
	he_update(640, 600, false, 16);
	he_update(640, 600, false, 16);
	PE_ReleaseMessage();
	int types[16], nos[16];
	he_update(150, 140, false, 16);
	int n = li_old_msgs(types, nos, 16);
	LI_CHECK(n == 3 && types[0] == 1 && types[1] == 6 && types[2] == 2 && nos[0] == LI_L && nos[1] == LI_L && nos[2] == LI_L,
		 "LI9 v13 the lower controller's parts: %d old message(s) %d/%d %d/%d %d/%d; want enter, on, move of %d",
		 n, n > 0 ? types[0] : -1, n > 0 ? nos[0] : -1, n > 1 ? types[1] : -1, n > 1 ? nos[1] : -1,
		 n > 2 ? types[2] : -1, n > 2 ? nos[2] : -1, LI_L);
	LI_CHECK(parts_get(LI_L)->is_hovered && br_msg_type_raw() < 0, "LI9 v13 L hovered %d, v14 queue type %d",
		 parts_get(LI_L)->is_hovered, br_msg_type_raw());
	/* the upper controller's parts over it */
	PE_SetPos(LI_U, 100, 100);
	he_update(150, 140, false, 16);
	n = li_old_msgs(types, nos, 16);
	LI_CHECK(li_old_count(n, types, nos, 1, LI_U) == 1 && li_old_count(n, types, nos, 3, LI_L) == 1
		 && !parts_get(LI_L)->is_hovered && parts_get(LI_U)->is_hovered,
		 "LI9 v13 covered: enter of U %d, leave of L %d, L hovered %d, U hovered %d (%d messages)",
		 li_old_count(n, types, nos, 1, LI_U), li_old_count(n, types, nos, 3, LI_L),
		 parts_get(LI_L)->is_hovered, parts_get(LI_U)->is_hovered, n);
	PE_EndInput();
	PE_ReleaseMessage();
	ain->version = version;
}

/* LI10: LI1 under three string rules, each in its own process. */
static void li_charset(int k)
{
	if (k == 0) {
		ain_is_gb18030 = true;
		sys4_set_string_charset(SYS4_CHARSET_GBK);
	} else if (k == 1) {
		ain_is_gb18030 = false;
		sys4_set_string_charset(SYS4_CHARSET_SJIS);
	} else {
		ain_is_gb18030 = true;
		sys4_set_string_charset(SYS4_CHARSET_SJIS);
	}
}
static void li_strings(void)
{
	const char *names[3] = { "the GBK rule", "the SJIS rule", "the GB18030 flag alone" };
	uint64_t got[3][2] = { { 0 } };
	for (int k = 0; k < 3; k++) {
		int fd[2];
		if (pipe(fd)) { printf("LAYER-INPUT LI10 pipe failed\n"); exit(2); }
		fflush(stdout);
		pid_t pid = fork();
		if (pid == 0) {
			close(fd[0]);
			li_charset(k);
			li_quiet = true;
			li_box_cursor_stays();
			uint64_t out[2] = { li_digest, (uint64_t)li_nmsg };
			ssize_t w = write(fd[1], out, sizeof out);
			_exit(w == (ssize_t)sizeof out ? 0 : 3);
		}
		close(fd[1]);
		ssize_t r = read(fd[0], got[k], sizeof got[k]);
		close(fd[0]);
		int status = 0;
		waitpid(pid, &status, 0);
		LI_CHECK(r == (ssize_t)sizeof got[k] && WIFEXITED(status) && WEXITSTATUS(status) == 0,
			 "LI10 the run under %s did not finish (status %d)", names[k], status);
	}
	printf("    LI10 %llu messages, digests %016llx %016llx %016llx\n", (unsigned long long)got[0][1],
	       (unsigned long long)got[0][0], (unsigned long long)got[1][0], (unsigned long long)got[2][0]);
	LI_CHECK(got[0][1] >= 20, "LI10 the run under %s read %llu messages, want 20 or more", names[0], (unsigned long long)got[0][1]);
	LI_CHECK(got[0][0] == got[1][0] && got[1][0] == got[2][0] && got[0][1] == got[1][1] && got[1][1] == got[2][1],
		 "LI10 the messages differ between the string rules (%llu, %llu, %llu messages)", (unsigned long long)got[0][1],
		 (unsigned long long)got[1][1], (unsigned long long)got[2][1]);
}

static int test_layer_input(void)
{
	void (*cases[])(void) = { li_box_cursor_stays, li_box_cursor_moves, li_press_gap, li_wheel, li_lower_active,
		li_overlay, li_membership, li_one_layer, li_v13, li_strings, li_none_designated, li_held_layer };
	const char *names[] = { "LI1", "LI2", "LI3", "LI4", "LI5", "LI6", "LI7", "LI8", "LI9", "LI10", "LI11", "LI12" };
	const char *what[] = {
		"the box closed with the cursor on the button: no MouseEnter again",
		"the cursor off the button under the box: the hover kept, left after the box",
		"a press through a gap of the active layer",
		"the wheel takes the active layer's parts, root and focus",
		"a lower layer designated active",
		"the system overlay",
		"membership follows the parent chain",
		"guard: one layer, a blocker of the same layer",
		"guard: v13, the old walk over every controller",
		"guard: the same under the GBK, SJIS and flag-only string rules",
		"no layer designated: the top of the stack acts",
		"a layer added with the button held: no click for the scene's button",
	};
	int n = sizeof(cases) / sizeof(cases[0]), failed = 0;
	for (int i = 0; i < n; i++) {
		fflush(stdout);
		pid_t pid = fork();
		if (pid == 0) {
			li_failures = he_failures = we_failures = 0;
			cases[i]();
			fflush(stdout);
			_exit(li_failures + he_failures + we_failures ? 1 : 0);
		}
		int status = 0;
		waitpid(pid, &status, 0);
		bool ok = WIFEXITED(status) && WEXITSTATUS(status) == 0;
		if (!ok)
			failed++;
		printf("%s %s (status %d): %s\n", names[i], ok ? "PASS" : "FAIL", status, what[i]);
	}
	printf("LAYER-INPUT %s: %d of %d cases failed\n", failed ? "FAIL" : "PASS", failed, n);
	return failed ? 1 : 0;
}
