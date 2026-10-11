/* input-model: the v14 mouse buttons as the original's input update
 * 0x547c90 sees them (see src/parts/input.c, v14_update_buttons).
 *
 *   IM1 A press on a clickable parts sends nothing and shows its down
 *       state; the release sends the whole-screen click (parts 0, no event
 *       ID, the release's position, key 1) and then the parts' own
 *       MouseClick, and GetClickNumber is that parts for the update of the
 *       release only (0x547610; the next update clears it, 0x547c90).
 *   IM2 Dragged off the parts the release sends the whole-screen click
 *       alone and the parts returns to its default state (0x5789dd);
 *       dragged back, it is clicked. A parts that only blocks the cursor
 *       never shows a down state (0x578580 needs +0x1a4).
 *   IM3 GetActiveParts (0x53c6c0: the focus 0x546890 sets at a press) is
 *       the parts under the cursor at the last press: a clickable one, a
 *       blocker, 0 beside everything; it survives the release, a reset of
 *       the input clears it (0x53c750 -> 0x538060), so does releasing the
 *       parts (0x53c140). That it is 0 right after BeginInput and after
 *       EndInput is the port's choice: the original's value changes at the
 *       next parts update, and its outermost EndInput does not walk the
 *       widgets to clear the focus (see PE_GetActivePartsNumber).
 *   IM4 The right and the middle button do nothing to the parts' input.
 *       This is the port's state, not the original's: there a release
 *       sends the whole-screen click with key 2 or 4 (0x5480be, 0x5480fb)
 *       and the focused element's MouseClick (0x5789e8), and a press moves
 *       the focus; that is left for a later round (see v14_update_buttons).
 *       Guarded: no message at a press or a release (on a clickable parts,
 *       beside everything, held at BeginInput), no GetClickNumber (the
 *       original has none either: +5 is written for key 1 alone,
 *       0x5789cb), the focus stays where the left button put it. A right
 *       or middle click while the left button is held on a parts leaves
 *       the left click whole: the down state, both messages and
 *       GetClickNumber (0x5462b0 cancels a drag and drop there and does not
 *       touch the click). A drag goes on under a right click, which the
 *       original cancels. The script's own reading of the right button (a
 *       CASClick polling Key_IsDown, the real bytecode) still sees a right
 *       click.
 *   IM5 A button held at BeginInput releases into the whole-screen click
 *       alone (0x546030 samples the buttons); a disabled button gets the
 *       whole-screen click alone and is not GetClickNumber, which is the
 *       port's expedient (the original's input update does not stop that
 *       click: the inner parts is clickable every frame, 0x528690);
 *       system.Peek does not run the input.
 *   IM6 v13: the release path of parts_update_mouse is unchanged (the
 *       parts' MouseClick on the v13 queue, GetClickPartsNumber kept until
 *       EndInput, nothing on the v14 queue), a right click changes nothing
 *       of a held left press, releasing the parts under a held button
 *       leaves the press as it was, and system.Peek still runs the input
 *       there.
 *   IM7 A scene opened from within a wait for a message (the system menu
 *       and the settings over a line of dialogue) ends on one of its
 *       buttons. The button's click comes with the release, so the wait the
 *       script returns to finds the button up: its CASClick, the bytecode
 *       run unchanged, sees no click and the line stays. With the click
 *       sent on the press the scene ended while the button was down, and
 *       its release stepped the message.
 *
 * Synthetic clickable boxes, the production PE_UpdateInputState and the v14
 * message queue; GetActiveParts and GetClickNumber through their AIN
 * declarations. No GL, no game assets. Each case runs in its own fork.
 */

static int im_failures;
#define IM_CHECK(c, ...) do { if (!(c)) { printf("  FAIL: "); printf(__VA_ARGS__); printf("\n"); im_failures++; } } while (0)

enum { IM_BOX = 18601, IM_OTHER = 18602, IM_BLOCK = 18603 };

static int im_hll_int(const char *name)
{
	union vm_value r[1] = { BR_I(-1) };
	int n = br_call("PartsEngine", name, 0, 0, 0, NULL, r, 1);
	IM_CHECK(n == 1, "%s returned %d stack slots, want 1", name, n);
	return r[0].i;
}

/* One input update with the buttons as given: the queued messages, taken
 * off the queue. */
struct im_msg { int type, no, delegate, x, y, key; };
struct im_frame { int n; struct im_msg m[3]; };
static struct im_frame im_update(bool left, bool right, bool middle)
{
	extern bool key_state[];
	int (*delegate)(void) = (int (*)(void))br_pe_fn("GetMessageDelegateIndex");
	int (*var)(int) = (int (*)(int))br_pe_fn("GetMessageVariableInt");
	assert(delegate && var);
	key_state[VK_LBUTTON] = left;
	key_state[VK_RBUTTON] = right;
	key_state[VK_MBUTTON] = middle;
	PE_UpdateInputState(16);
	struct im_frame f = { 0 };
	for (int i = 0; i < 3; i++)
		f.m[i] = (struct im_msg){ -1, -1, -2, 0, 0, 0 };
	for (int t; (t = br_msg_type()) >= 0; br_msg_pop()) {
		if (f.n < 3)
			f.m[f.n] = (struct im_msg){ t, br_msg_parts(), delegate(), var(0), var(1), var(2) };
		f.n++;
	}
	return f;
}
static bool im_is(const struct im_msg *m, int no, int delegate, int x, int y, int key)
{
	return m->type == 4 && m->no == no && m->delegate == delegate && m->x == x && m->y == y && m->key == key;
}
#define IM_FRAME(f, what, want_n) \
	IM_CHECK((f).n == (want_n), "%s: %d message(s): (%d,%d,%d,(%d,%d,%d)) (%d,%d,%d,(%d,%d,%d)) (%d,%d,%d,(%d,%d,%d)); want %d", what, (f).n, \
		 (f).m[0].type, (f).m[0].no, (f).m[0].delegate, (f).m[0].x, (f).m[0].y, (f).m[0].key, \
		 (f).m[1].type, (f).m[1].no, (f).m[1].delegate, (f).m[1].x, (f).m[1].y, (f).m[1].key, \
		 (f).m[2].type, (f).m[2].no, (f).m[2].delegate, (f).m[2].x, (f).m[2].y, (f).m[2].key, (want_n))

static void im_setup(void)
{
	br_input_setup();
	br_box(IM_BOX, 100, 100, 200, 80, true);
	br_box(IM_OTHER, 500, 100, 200, 80, true);
	struct parts *block = br_box(IM_BLOCK, 100, 400, 200, 80, true);
	PE_SetClickable(IM_BLOCK, false);
	IM_CHECK(!block->clickable && !block->pass_cursor, "the blocker is clickable %d, passes the cursor %d", block->clickable, block->pass_cursor);
	cp_register_event(IM_BOX, 3, 30);
	extern bool key_state[];
	key_state[VK_LBUTTON] = key_state[VK_RBUTTON] = key_state[VK_MBUTTON] = false;
	mouse_set_pos(640, 600);
	PE_UpdateComponent(0);
	PE_BeginInput();
	im_update(false, false, false);
}

static void im_release_sends(void)
{
	im_setup();
	struct parts *box = parts_get(IM_BOX);
	mouse_set_pos(150, 140);
	struct im_frame f = im_update(true, false, false);
	IM_FRAME(f, "the press", 0);
	IM_CHECK(box->state == PARTS_STATE_CLICKED, "the pressed parts is in state %d, want the down state", box->state);
	IM_CHECK(im_hll_int("GetClickNumber") == 0, "GetClickNumber after the press is %d, want 0", im_hll_int("GetClickNumber"));
	f = im_update(true, false, false);
	IM_FRAME(f, "held", 0);
	mouse_set_pos(160, 150);
	f = im_update(false, false, false);
	IM_FRAME(f, "the release", 2);
	IM_CHECK(im_is(&f.m[0], 0, -1, 160, 150, 1), "the first message is not the whole-screen click (4, 0, -1, (160,150,1))");
	IM_CHECK(im_is(&f.m[1], IM_BOX, 3, 160, 150, 1), "the second message is not the parts' own click (4, %d, 3, (160,150,1))", IM_BOX);
	IM_CHECK(box->state == PARTS_STATE_HOVERED, "the released parts is in state %d, want the on-cursor state", box->state);
	IM_CHECK(im_hll_int("GetClickNumber") == IM_BOX, "GetClickNumber at the release is %d, want %d", im_hll_int("GetClickNumber"), IM_BOX);
	f = im_update(false, false, false);
	IM_FRAME(f, "the update after the release", 0);
	IM_CHECK(im_hll_int("GetClickNumber") == 0, "GetClickNumber after the next update is %d, want 0", im_hll_int("GetClickNumber"));
	/* beside everything: the whole-screen click alone */
	mouse_set_pos(640, 600);
	im_update(true, false, false);
	f = im_update(false, false, false);
	IM_FRAME(f, "a click beside everything", 1);
	IM_CHECK(im_is(&f.m[0], 0, -1, 640, 600, 1), "the message is not the whole-screen click");
	IM_CHECK(im_hll_int("GetClickNumber") == 0, "GetClickNumber after a click beside everything is %d", im_hll_int("GetClickNumber"));
	/* on the blocker: the whole-screen click alone, no down state */
	mouse_set_pos(150, 440);
	f = im_update(true, false, false);
	IM_CHECK(parts_get(IM_BLOCK)->state == PARTS_STATE_HOVERED, "the pressed blocker is in state %d, want the on-cursor state", parts_get(IM_BLOCK)->state);
	f = im_update(false, false, false);
	IM_FRAME(f, "a click on a parts that only blocks the cursor", 1);
	IM_CHECK(im_is(&f.m[0], 0, -1, 150, 440, 1), "the message is not the whole-screen click");
	PE_EndInput();
	printf("  IM1 the release sends the whole-screen click, then the pressed parts' own\n");
}

static void im_drag(void)
{
	im_setup();
	struct parts *box = parts_get(IM_BOX), *other = parts_get(IM_OTHER);
	/* off and released there */
	mouse_set_pos(150, 140);
	im_update(true, false, false);
	mouse_set_pos(640, 600);
	struct im_frame f = im_update(true, false, false);
	IM_FRAME(f, "dragged off", 0);
	IM_CHECK(box->state == PARTS_STATE_DEFAULT, "the parts the cursor left is in state %d, want the default state", box->state);
	f = im_update(false, false, false);
	IM_FRAME(f, "released off the parts", 1);
	IM_CHECK(im_is(&f.m[0], 0, -1, 640, 600, 1), "the message is not the whole-screen click");
	IM_CHECK(im_hll_int("GetClickNumber") == 0, "GetClickNumber after a release off the parts is %d", im_hll_int("GetClickNumber"));
	/* off and onto another clickable parts: the other is not clicked */
	mouse_set_pos(150, 140);
	im_update(true, false, false);
	mouse_set_pos(550, 140);
	f = im_update(true, false, false);
	IM_CHECK(other->state != PARTS_STATE_CLICKED, "the parts dragged onto is in state %d, not the down state", other->state);
	f = im_update(false, false, false);
	IM_FRAME(f, "released on another parts", 1);
	IM_CHECK(im_is(&f.m[0], 0, -1, 550, 140, 1), "the message is not the whole-screen click");
	/* off and back */
	mouse_set_pos(150, 140);
	im_update(false, false, false);
	im_update(true, false, false);
	mouse_set_pos(640, 600);
	im_update(true, false, false);
	mouse_set_pos(150, 140);
	f = im_update(true, false, false);
	IM_CHECK(box->state == PARTS_STATE_CLICKED, "the parts dragged back to is in state %d, want the down state", box->state);
	f = im_update(false, false, false);
	IM_FRAME(f, "released back on the parts", 2);
	IM_CHECK(im_is(&f.m[1], IM_BOX, 3, 150, 140, 1), "the second message is not the parts' own click");
	PE_EndInput();
	printf("  IM2 dragged off, the release is the whole-screen click alone; dragged back, a click\n");
}

static void im_active_parts(void)
{
	im_setup();
	IM_CHECK(im_hll_int("GetActiveParts") == 0, "GetActiveParts before any press is %d", im_hll_int("GetActiveParts"));
	mouse_set_pos(150, 140);
	im_update(true, false, false);
	IM_CHECK(im_hll_int("GetActiveParts") == IM_BOX, "GetActiveParts at a press on a parts is %d, want %d", im_hll_int("GetActiveParts"), IM_BOX);
	im_update(false, false, false);
	IM_CHECK(im_hll_int("GetActiveParts") == IM_BOX, "GetActiveParts after the release is %d, want %d (the focus stays)", im_hll_int("GetActiveParts"), IM_BOX);
	mouse_set_pos(640, 600);
	im_update(false, false, false);
	IM_CHECK(im_hll_int("GetActiveParts") == IM_BOX, "GetActiveParts after moving away is %d, want %d (a move is not a press)", im_hll_int("GetActiveParts"), IM_BOX);
	im_update(true, false, false);
	IM_CHECK(im_hll_int("GetActiveParts") == 0, "GetActiveParts at a press beside everything is %d, want 0", im_hll_int("GetActiveParts"));
	im_update(false, false, false);
	mouse_set_pos(150, 440);
	im_update(true, false, false);
	IM_CHECK(im_hll_int("GetActiveParts") == IM_BLOCK, "GetActiveParts at a press on a blocker is %d, want %d", im_hll_int("GetActiveParts"), IM_BLOCK);
	im_update(false, false, false);
	/* The port's choice, here and after EndInput below: 0 at once. The
	 * original's value changes at the next parts update, and its
	 * outermost EndInput leaves the focus (PE_GetActivePartsNumber). */
	PE_BeginInput();
	IM_CHECK(im_hll_int("GetActiveParts") == 0, "GetActiveParts after BeginInput is %d, want 0", im_hll_int("GetActiveParts"));
	mouse_set_pos(150, 140);
	im_update(false, false, false);
	im_update(true, false, false);
	IM_CHECK(im_hll_int("GetActiveParts") == IM_BOX, "GetActiveParts at a press in the inner scope is %d", im_hll_int("GetActiveParts"));
	PE_EndInput();
	IM_CHECK(im_hll_int("GetActiveParts") == 0, "GetActiveParts after EndInput is %d, want 0", im_hll_int("GetActiveParts"));
	im_update(false, false, false);
	im_update(true, false, false);
	im_update(false, false, false);
	IM_CHECK(im_hll_int("GetActiveParts") == IM_BOX, "GetActiveParts after a click in the outer scope is %d", im_hll_int("GetActiveParts"));
	PE_ReleaseParts(IM_BOX);
	IM_CHECK(im_hll_int("GetActiveParts") == 0, "GetActiveParts after the parts was released is %d, want 0", im_hll_int("GetActiveParts"));
	PE_EndInput();
	printf("  IM3 GetActiveParts is the parts of the last press\n");
}

static void im_other_buttons(void)
{
	im_setup();
	extern bool key_state[];
	struct parts *box = parts_get(IM_BOX), *other = parts_get(IM_OTHER);
	/* a right click on a clickable parts: nothing (the original sends the
	 * whole-screen click with key 2 and the parts' own, and no
	 * GetClickNumber) */
	mouse_set_pos(150, 140);
	struct im_frame f = im_update(false, true, false);
	IM_FRAME(f, "a right press", 0);
	IM_CHECK(box->state == PARTS_STATE_HOVERED, "the parts under a right press is in state %d, want the on-cursor state", box->state);
	IM_CHECK(im_hll_int("GetActiveParts") == 0, "a right press made parts %d the focus, want none", im_hll_int("GetActiveParts"));
	f = im_update(false, false, false);
	IM_FRAME(f, "a right release on a clickable parts", 0);
	IM_CHECK(im_hll_int("GetClickNumber") == 0, "GetClickNumber after a right click of a parts is %d, want 0", im_hll_int("GetClickNumber"));
	/* a middle click on it */
	f = im_update(false, false, true);
	IM_FRAME(f, "a middle press", 0);
	IM_CHECK(box->state == PARTS_STATE_HOVERED, "the parts under a middle press is in state %d, want the on-cursor state", box->state);
	f = im_update(false, false, false);
	IM_FRAME(f, "a middle release on a clickable parts", 0);
	IM_CHECK(im_hll_int("GetClickNumber") == 0 && im_hll_int("GetActiveParts") == 0,
		 "a middle click of a parts: GetClickNumber %d, GetActiveParts %d; want 0 and 0", im_hll_int("GetClickNumber"), im_hll_int("GetActiveParts"));
	/* beside everything, both at once */
	mouse_set_pos(640, 600);
	im_update(false, true, true);
	f = im_update(false, false, false);
	IM_FRAME(f, "a right and middle click beside everything", 0);
	/* the focus stays where the left button put it (in the original a
	 * press of any button moves it) */
	mouse_set_pos(150, 140);
	im_update(false, false, false);
	im_update(true, false, false);
	im_update(false, false, false);
	mouse_set_pos(640, 600);
	im_update(false, false, false);
	im_update(false, true, false);
	im_update(false, false, false);
	im_update(false, false, true);
	im_update(false, false, false);
	IM_CHECK(im_hll_int("GetActiveParts") == IM_BOX, "GetActiveParts after a right and a middle click beside everything is %d, want %d", im_hll_int("GetActiveParts"), IM_BOX);
	/* A right click while the left button is held on the parts: the parts
	 * keeps its down state and gets its click (0x5462b0 cancels a drag
	 * and drop and leaves the focused element's click alone). */
	mouse_set_pos(150, 140);
	im_update(false, false, false);
	im_update(true, false, false);
	IM_CHECK(box->state == PARTS_STATE_CLICKED, "the left-pressed parts is in state %d, want the down state", box->state);
	f = im_update(true, true, false);
	IM_FRAME(f, "a right press while the left button is held", 0);
	IM_CHECK(box->state == PARTS_STATE_CLICKED, "a right press took the down state off the left-pressed parts (state %d)", box->state);
	f = im_update(true, false, false);
	IM_FRAME(f, "the right release while the left button is held", 0);
	IM_CHECK(box->state == PARTS_STATE_CLICKED, "a right release took the down state off the left-pressed parts (state %d)", box->state);
	f = im_update(false, false, false);
	IM_FRAME(f, "the left release after a right click", 2);
	IM_CHECK(im_is(&f.m[0], 0, -1, 150, 140, 1) && im_is(&f.m[1], IM_BOX, 3, 150, 140, 1),
		 "want the whole-screen click and the parts' own, both with key 1");
	IM_CHECK(im_hll_int("GetClickNumber") == IM_BOX, "GetClickNumber at the left release after a right click is %d, want %d", im_hll_int("GetClickNumber"), IM_BOX);
	/* the middle button, still down when the left one comes up */
	im_update(true, false, false);
	f = im_update(true, false, true);
	IM_CHECK(box->state == PARTS_STATE_CLICKED, "a middle press took the down state off the left-pressed parts (state %d)", box->state);
	f = im_update(false, false, true);
	IM_FRAME(f, "the left release while the middle button is held", 2);
	IM_CHECK(im_is(&f.m[0], 0, -1, 150, 140, 1) && im_is(&f.m[1], IM_BOX, 3, 150, 140, 1),
		 "want the whole-screen click and the parts' own, both with key 1");
	f = im_update(false, false, false);
	IM_FRAME(f, "the middle release after the left one", 0);
	/* left and right released in one update: the left button's two */
	im_update(true, true, false);
	f = im_update(false, false, false);
	IM_FRAME(f, "left and right released together", 2);
	IM_CHECK(im_is(&f.m[0], 0, -1, 150, 140, 1) && im_is(&f.m[1], IM_BOX, 3, 150, 140, 1),
		 "want the whole-screen click and the parts' own, both with key 1");
	/* The script reads the right button itself: its CASClick (the bytecode
	 * run unchanged, polling Key_IsDown as the ADV engine's RClick does at
	 * every parts update) takes a right click for which the input update
	 * sends nothing. */
	lr_replace_hll("IbisInputEngine", "Key_IsDown", (void *)df_key_is_down);
	int rclick = new_object_from_ain(ain_get_struct(ain, "CASClick"));
	const int any = INT_MIN, right_cancel[] = { VK_RBUTTON, 1 };
	df_click_call(rclick, "Init", 2, right_cancel);
	im_update(false, false, false);
	IM_CHECK(!df_click_call(rclick, "IsClick", 1, &any), "the script's right-button CASClick has a click before any press");
	int msgs = 0, clicks = 0;
	for (int i = 0; i < 4; i++) {
		f = im_update(false, i < 2, false);
		msgs += f.n;
		clicks += df_click_call(rclick, "IsClick", 1, &any) == 1;
	}
	IM_CHECK(clicks == 1 && !msgs, "a right click: %d click(s) of the script's CASClick and %d message(s); want 1 and none", clicks, msgs);
	/* held at BeginInput: its release sends nothing either */
	PE_EndInput();
	key_state[VK_RBUTTON] = key_state[VK_MBUTTON] = true;
	PE_BeginInput();
	im_update(false, true, true);
	f = im_update(false, false, false);
	IM_FRAME(f, "the release of a right and a middle button held at BeginInput", 0);
	/* A drag goes on under a right click (the port's state: the original
	 * cancels the drag there, 0x5462b0, which is not done). */
	PE_SetDrag(IM_OTHER, true);
	mouse_set_pos(550, 140);
	im_update(false, false, false);
	im_update(true, false, false);
	mouse_set_pos(560, 150);
	im_update(true, false, false);
	IM_CHECK(other->local.pos.x == 510 && other->local.pos.y == 110, "the dragged parts is at (%d,%d), want (510,110)", other->local.pos.x, other->local.pos.y);
	im_update(true, true, false);
	im_update(true, false, false);
	mouse_set_pos(570, 160);
	im_update(true, false, false);
	IM_CHECK(other->local.pos.x == 520 && other->local.pos.y == 120, "the dragged parts is at (%d,%d) after a right click, want (520,120): the drag was dropped", other->local.pos.x, other->local.pos.y);
	im_update(false, false, false);
	PE_EndInput();
	printf("  IM4 the right and the middle button do nothing to the parts' input\n");
}

static void im_scope_and_peek(void)
{
	im_setup();
	PE_EndInput();
	extern bool key_state[];
	struct parts *box = parts_get(IM_BOX);
	/* held at BeginInput */
	mouse_set_pos(150, 140);
	key_state[VK_LBUTTON] = true;
	PE_BeginInput();
	struct im_frame f = im_update(true, false, false);
	IM_FRAME(f, "held at BeginInput", 0);
	IM_CHECK(box->state != PARTS_STATE_CLICKED, "a parts under a button held at BeginInput is in state %d, not the down state", box->state);
	f = im_update(false, false, false);
	IM_FRAME(f, "the release of a button held at BeginInput", 1);
	IM_CHECK(im_is(&f.m[0], 0, -1, 150, 140, 1), "the message is not the whole-screen click");
	IM_CHECK(im_hll_int("GetClickNumber") == 0 && im_hll_int("GetActiveParts") == 0,
		 "a button held at BeginInput clicked parts %d / focused %d on its release", im_hll_int("GetClickNumber"), im_hll_int("GetActiveParts"));
	/* a disabled button: no MouseClick of its own and no GetClickNumber.
	 * The port's expedient, without a counterpart in the original's
	 * input update (see v14_update_buttons). */
	BU_PE("SetButtonEnable", BU_I(IM_BOX), BU_I(0));
	br_box(IM_BOX, 100, 100, 200, 80, true);
	im_update(true, false, false);
	f = im_update(false, false, false);
	IM_FRAME(f, "a click on a disabled button", 1);
	IM_CHECK(im_is(&f.m[0], 0, -1, 150, 140, 1), "the message is not the whole-screen click alone");
	IM_CHECK(im_hll_int("GetClickNumber") == 0, "a disabled button is GetClickNumber %d", im_hll_int("GetClickNumber"));
	BU_PE("SetButtonEnable", BU_I(IM_BOX), BU_I(1));
	br_box(IM_BOX, 100, 100, 200, 80, true);
	/* a press beside everything takes the focus away (a press on the
	 * disabled button gave it) */
	mouse_set_pos(640, 600);
	im_update(true, false, false);
	im_update(false, false, false);
	mouse_set_pos(150, 140);
	im_update(false, false, false);
	IM_CHECK(im_hll_int("GetActiveParts") == 0, "GetActiveParts after a press beside everything is %d", im_hll_int("GetActiveParts"));
	/* system.Peek: the events only, no input update */
	key_state[VK_LBUTTON] = true;
	br_call("system", "Peek", 0, 0, 0, NULL, NULL, 0);
	IM_CHECK(box->state != PARTS_STATE_CLICKED, "system.Peek pressed the parts (state %d)", box->state);
	key_state[VK_LBUTTON] = false;
	br_call("system", "PeekAll", 0, 0, 0, NULL, NULL, 0);
	IM_CHECK(br_msg_type() < 0, "system.PeekAll queued a message of type %d", br_msg_type());
	f = im_update(false, false, false);
	IM_FRAME(f, "the update after a click seen by system.Peek alone", 0);
	IM_CHECK(im_hll_int("GetClickNumber") == 0 && im_hll_int("GetActiveParts") == 0,
		 "a click seen by system.Peek alone: GetClickNumber %d, GetActiveParts %d", im_hll_int("GetClickNumber"), im_hll_int("GetActiveParts"));
	PE_EndInput();
	printf("  IM5 a button held at BeginInput, a disabled button and system.Peek\n");
}

static void im_v13(void)
{
	im_setup();
	int version = ain->version;
	ain->version = 13;
	struct parts *box = parts_get(IM_BOX);
	mouse_set_pos(150, 140);
	struct im_frame f = im_update(true, false, false);
	IM_FRAME(f, "v13: a press", 0);
	IM_CHECK(box->state == PARTS_STATE_CLICKED, "v13: the pressed parts is in state %d", box->state);
	f = im_update(false, false, false);
	IM_FRAME(f, "v13: the release (nothing on the v14 queue)", 0);
	IM_CHECK(PE_GetClickPartsNumber() == IM_BOX, "v13: GetClickPartsNumber after the release is %d, want %d", PE_GetClickPartsNumber(), IM_BOX);
	f = im_update(false, false, false);
	IM_CHECK(PE_GetClickPartsNumber() == IM_BOX, "v13: GetClickPartsNumber was cleared by the next update (%d)", PE_GetClickPartsNumber());
	f = im_update(false, true, false);
	f = im_update(false, false, false);
	IM_FRAME(f, "v13: a right click (nothing on the v14 queue)", 0);
	/* v13: a right click while the left button is held changes nothing of
	 * the left press (EndInput clears GetClickPartsNumber there) */
	PE_EndInput();
	PE_BeginInput();
	IM_CHECK(PE_GetClickPartsNumber() == 0, "v13: GetClickPartsNumber in a new scope is %d", PE_GetClickPartsNumber());
	im_update(false, false, false);
	im_update(true, false, false);
	im_update(true, true, false);
	IM_CHECK(box->state == PARTS_STATE_CLICKED, "v13: a right press took the down state off the left-pressed parts (state %d)", box->state);
	im_update(true, false, false);
	f = im_update(false, false, false);
	IM_FRAME(f, "v13: the left release after a right click (nothing on the v14 queue)", 0);
	IM_CHECK(PE_GetClickPartsNumber() == IM_BOX, "v13: GetClickPartsNumber at the left release after a right click is %d, want %d", PE_GetClickPartsNumber(), IM_BOX);
	/* v13: releasing the parts under a held button leaves the press as it
	 * was (parts_input_release returns early, as the function did before
	 * the v14 input model): a parts made anew under that number is the
	 * pressed one and takes the click. */
	PE_EndInput();
	PE_BeginInput();
	im_update(false, false, false);
	im_update(true, false, false);
	PE_ReleaseParts(IM_BOX);
	box = br_box(IM_BOX, 100, 100, 200, 80, true);
	PE_UpdateComponent(0);
	im_update(true, false, false);
	IM_CHECK(box->state == PARTS_STATE_CLICKED, "v13: a parts made anew under a held press is in state %d, want the down state (the press is kept)", box->state);
	im_update(false, false, false);
	IM_CHECK(PE_GetClickPartsNumber() == IM_BOX, "v13: GetClickPartsNumber after releasing the button on a parts made anew is %d, want %d", PE_GetClickPartsNumber(), IM_BOX);
	/* v13: system.Peek runs the input as before (only v14 leaves it to
	 * the update of the parts): the press and the release it sees alone
	 * click the parts. */
	PE_EndInput();
	PE_BeginInput();
	im_update(false, false, false);
	extern bool key_state[];
	key_state[VK_LBUTTON] = true;
	br_call("system", "Peek", 0, 0, 0, NULL, NULL, 0);
	IM_CHECK(box->state == PARTS_STATE_CLICKED, "v13: system.Peek did not run the input (the parts is in state %d)", box->state);
	key_state[VK_LBUTTON] = false;
	br_call("system", "PeekAll", 0, 0, 0, NULL, NULL, 0);
	IM_CHECK(box->state != PARTS_STATE_CLICKED, "v13: system.PeekAll did not run the input (the parts is still in state %d)", box->state);
	IM_CHECK(PE_GetClickPartsNumber() == IM_BOX, "v13: the click seen by system.Peek and PeekAll alone left GetClickPartsNumber at %d, want %d", PE_GetClickPartsNumber(), IM_BOX);
	PE_EndInput();
	IM_CHECK(PE_GetClickPartsNumber() == 0, "v13: EndInput left GetClickPartsNumber at %d", PE_GetClickPartsNumber());
	ain->version = version;
	printf("  IM6 v13 unchanged\n");
}

/* IM7 */
static void im_scene_button(void)
{
	im_setup();
	lr_replace_hll("IbisInputEngine", "Key_IsDown", (void *)df_key_is_down);
	int click = new_object_from_ain(ain_get_struct(ain, "CASClick"));
	const int any = INT_MIN, left_cancel[] = { VK_LBUTTON, 1 };
	df_click_call(click, "Init", 2, left_cancel);
	im_update(false, false, false);
	IM_CHECK(!df_click_call(click, "IsClick", 1, &any), "the wait has a click before any press");
	/* The scene's scope, its button under the cursor; the left button is
	 * down for two updates. The scene ends where its script gets the
	 * button's MouseClick. */
	PE_BeginInput();
	mouse_set_pos(150, 140);
	im_update(false, false, false);
	int frame = 0, clicked_at = -1;
	for (; frame < 6 && clicked_at < 0; frame++) {
		struct im_frame f = im_update(frame < 2, false, false);
		for (int i = 0; i < f.n && i < 3; i++) {
			if (f.m[i].no == IM_BOX)
				clicked_at = frame;
		}
	}
	IM_CHECK(clicked_at == 2, "the scene's button got its click at update %d, want 2 (the release)", clicked_at);
	IM_CHECK(!key_is_down(VK_LBUTTON), "the button is still down when the scene ends");
	PE_EndInput();
	/* Back in the wait, the same mouse going on. */
	int msgs = 0, clicks = 0;
	for (int i = 0; i < 4; i++, frame++) {
		struct im_frame f = im_update(frame < 2, false, false);
		msgs += f.n;
		clicks += df_click_call(click, "IsClick", 1, &any) == 1;
	}
	IM_CHECK(!msgs && !clicks, "back in the wait: %d message(s) and %d click(s) of the script's CASClick; want none", msgs, clicks);
	/* The wait takes its next click. */
	mouse_set_pos(640, 600);
	im_update(true, false, false);
	IM_CHECK(!df_click_call(click, "IsClick", 1, &any), "the next press alone is a click for the script");
	struct im_frame f = im_update(false, false, false);
	IM_FRAME(f, "the wait's next click", 1);
	IM_CHECK(df_click_call(click, "IsClick", 1, &any) == 1, "the script's CASClick does not take the wait's next click");
	PE_EndInput();
	printf("  IM7 a scene's button ends the scene with the button up\n");
}

static int test_input_model(void)
{
	void (*cases[])(void) = { im_release_sends, im_drag, im_active_parts, im_other_buttons, im_scope_and_peek, im_v13,
				  im_scene_button };
	const char *names[] = { "IM1", "IM2", "IM3", "IM4", "IM5", "IM6", "IM7" };
	int failed = 0;
	for (unsigned i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
		fflush(stdout);
		pid_t pid = fork();
		if (pid == 0) {
			alarm(60);
			im_failures = 0;
			cases[i]();
			fflush(stdout);
			_exit(im_failures ? 1 : 0);
		}
		int st = 0;
		bool ok = pid > 0 && waitpid(pid, &st, 0) == pid && WIFEXITED(st) && WEXITSTATUS(st) == 0;
		printf("%s %s\n", names[i], ok ? "ok" : "FAIL");
		failed += !ok;
	}
	printf("INPUT-MODEL %s: %d of %zu cases failed\n", failed ? "FAIL" : "PASS", failed, sizeof(cases) / sizeof(cases[0]));
	return failed ? 1 : 0;
}
