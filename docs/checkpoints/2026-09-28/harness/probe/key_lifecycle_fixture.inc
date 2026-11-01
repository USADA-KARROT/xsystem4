/* Input lifecycle contracts: 0x546030, 0x5461a0, 0x577af0,
 * 0x574fd0. The port represents native activation with SDL input focus;
 * a synthetic gate transition here is not an OS activation equivalence. */
static void kl_begin_held(void)
{
	ke_setup(); ke_key(VK_RBUTTON, true); PE_BeginInput(); ke_read();
	struct ke_frame f = ke_frame(150, 140, 16);
	KE_CHECK(he_hll_int("GetActiveParts") == 0, "Begin-held mouse selected focus");
	KE_CHECK(ke_count(&f, 15, 0, VK_RBUTTON) == 1 && !ke_count(&f, 15, KE_A, VK_RBUTTON), "Begin-held key table wrongly seeded");
	ke_key(VK_RBUTTON, false); f = ke_frame(150, 140, 16);
	KE_CHECK(ke_count(&f, 4, 0, VK_RBUTTON) == 1 && !ke_count(&f, 4, KE_A, VK_RBUTTON), "Begin-held release routing");
}
static void kl_end_keys(void)
{
	ke_setup(); ke_focus_a(); ke_key('A', true); ke_frame(150, 140, 16);
	PE_EndInput(); struct ke_frame f = ke_read();
	KE_CHECK(ke_count(&f, 18, 0, 'A') == 1 && !ke_count(&f, 18, KE_A, 'A'), "End whole/local reset distinction");
	f = ke_frame(150, 140, 16); KE_CHECK(f.n == 0, "input updated outside scope");
	PE_BeginInput(); ke_read(); ke_key(VK_LBUTTON, true); f = ke_frame(150, 140, 16);
	KE_CHECK(ke_count(&f, 15, 0, 'A') == 1 && !ke_count(&f, 15, KE_A, 'A') && ke_count(&f, 16, KE_A, 'A') == 1, "Begin reset keys of non-slot focus");
}
static void kl_slot_reset(void)
{
	ke_setup(); ke_key(VK_LBUTTON, true); ke_frame(150, 140, 16);
	PE_EndInput(); struct ke_frame f = ke_read();
	KE_CHECK(ke_count(&f, 18, 0, VK_LBUTTON) == 1 && !ke_count(&f, 18, KE_A, VK_LBUTTON), "slot reset emitted local Up");
	KE_CHECK(!parts_get(KE_A)->is_hovered, "slot remained entered");
	PE_BeginInput(); ke_read(); ke_frame(150, 140, 16);
	ke_key(VK_LBUTTON, false); f = ke_frame(150, 140, 16);
	KE_CHECK(ke_count(&f, 4, 0, VK_LBUTTON) == 1 && !ke_count(&f, 4, KE_A, VK_LBUTTON), "slot reset retained local click");
}
static void kl_keyboard_outside(void)
{
	ke_setup(); ke_focus_a(); mouse_focus = false; ke_key('A', true);
	struct ke_frame f = ke_frame(640, 600, 16);
	KE_CHECK(ke_count(&f, 15, 0, 'A') == 1 && ke_count(&f, 15, KE_A, 'A') == 1, "no-position suppressed keyboard sampling");
	ke_key('A', false); f = ke_frame(640, 600, 16);
	KE_CHECK(ke_count(&f, 18, 0, 'A') == 1 && ke_count(&f, 18, KE_A, 'A') == 1, "no-position lost keyboard Up");
}
static void kl_mouse_press_gate(void)
{
	ke_setup(); ke_key(VK_LBUTTON, true); ke_key('A', true); ke_frame(150, 140, 16);
	struct ke_frame f = ke_frame(640, 600, 516);
	KE_CHECK(ke_count(&f, 17, 0, VK_LBUTTON) == 1 && ke_count(&f, 17, 0, 'A') == 1, "Whole repeat unexpectedly gated by hover");
	KE_CHECK(!ke_count(&f, 17, KE_A, VK_LBUTTON) && ke_count(&f, 17, KE_A, 'A') == 1, "local mouse/keyboard Press gate");
}
static void kl_layer_first_press(void)
{
	li_setup(); key_clear_flag(false); keyboard_focus = true;
	li_button(KE_A, 100, 100, 200, 80, 3); PE_BeginInput();
	ke_frame(640, 600, 0); ke_frame(640, 600, 0); ke_focus_a();
	/* During opening animation, before the new layer's BeginInput. */
	int layer = ci_add(-1); li_button(KE_B, 100, 100, 200, 80, 7);
	KE_CHECK(ci_active() == layer, "new layer not active");
	ke_key(VK_RBUTTON, true); struct ke_frame f = ke_frame(150, 140, 16);
	KE_CHECK(he_hll_int("GetActiveParts") == KE_B, "first press after layer switch missed active layer");
	KE_CHECK(ke_count(&f, 15, KE_B, VK_RBUTTON) == 1 && !ke_count(&f, 15, KE_A, VK_RBUTTON), "first key sent to inactive layer");
	ke_key(VK_RBUTTON, false); f = ke_frame(150, 140, 16);
	KE_CHECK(ke_count(&f, 4, KE_B, VK_RBUTTON) == 1 && !ke_count(&f, 4, KE_A, VK_RBUTTON), "layer release receiver");
}
static void kl_sdl_key(SDL_Scancode scan, bool down)
{
	SDL_Event e = {.type = down ? SDL_KEYDOWN : SDL_KEYUP};
	e.key.keysym.scancode = scan; e.key.state = down ? SDL_PRESSED : SDL_RELEASED;
	KE_CHECK(SDL_PushEvent(&e) == 1, "SDL synthetic key enqueue");
	au_pump();
}
static void kl_keypad(void)
{
	/* au_pump samples forced keys; keep inherited holds out of this fork. */
	unsetenv("XSYS4_HOLD_KEYS");
	ke_setup(); KE_CHECK(SDL_Init(SDL_INIT_EVENTS) == 0, "SDL events init");
	kl_sdl_key(SDL_SCANCODE_KP_ENTER, true); KE_CHECK(key_is_down(VK_RETURN), "v14 keypad Enter not Return");
	kl_sdl_key(SDL_SCANCODE_KP_ENTER, false); KE_CHECK(!key_is_down(VK_RETURN), "v14 keypad Enter held after release");
	/* Before v14, preserve the previous unmapped keypad Enter. */
	ain->version = 13;
	kl_sdl_key(SDL_SCANCODE_KP_ENTER, true); KE_CHECK(!key_is_down(VK_RETURN), "v13 keypad mapping changed");
	kl_sdl_key(SDL_SCANCODE_KP_ENTER, false);
	kl_sdl_key(SDL_SCANCODE_RETURN, true); KE_CHECK(key_is_down(VK_RETURN), "v13 normal Return changed");
	kl_sdl_key(SDL_SCANCODE_RETURN, false); KE_CHECK(!key_is_down(VK_RETURN), "v13 normal Return release changed");
}

/* The original has one manager per controller (0x53e690/0x53c67c).
 * The port shares one cancellation suppression among the layers; it must
 * not carry a cancelled source's suppression into a newly active layer
 * before BeginInput. */
static void kl_cancelled_layer_press(void)
{
	li_setup(); key_clear_flag(false); keyboard_focus = true;
	li_button(KE_A, 100, 100, 200, 80, 3); PE_BeginInput();
	ke_frame(150, 140, 0); ke_frame(150, 140, 0);
	ke_key(VK_LBUTTON, true); ke_frame(150, 140, 16);
	ke_key(VK_RBUTTON, true);
	struct ke_frame f = ke_frame(150, 140, 16);
	kc_cancel_events(&f, VK_LBUTTON, 2, false);
	KE_CHECK(he_hll_int("GetActiveParts") == KE_A, "cancellation lost old focus");
	int layer = ci_add(-1);
	li_button(KE_B, 100, 100, 200, 80, 7);
	KE_CHECK(ci_active() == layer, "new layer not active");
	ke_key(VK_MBUTTON, true); f = ke_frame(150, 140, 16);
	KE_CHECK(he_hll_int("GetActiveParts") == KE_B, "cancelled old layer blocked new layer's first press");
	KE_CHECK(ke_count(&f, 15, KE_B, VK_MBUTTON) == 1 &&
		!ke_count(&f, 15, KE_A, VK_MBUTTON), "new key reached cancelled inactive layer");
	ke_key(VK_LBUTTON, false); ke_key(VK_RBUTTON, false); ke_key(VK_MBUTTON, false);
	f = ke_frame(150, 140, 16);
	KE_CHECK(ke_count(&f, 4, KE_B, VK_MBUTTON) == 1 &&
		!ke_count(&f, 4, KE_A, VK_MBUTTON), "new layer release receiver");
}

/* A left press on the focus of a box's layer, SetActiveLayer to the scene's
 * layer with the box kept (one update held there), and the release in the
 * scene's layer (kl_layer_return_late_up) or after the switch back
 * (kl_layer_return_held). The box's layer keeps its own slots, left press
 * mark and focus over the switch, and the focus' own key table with them,
 * untouched while the scene's layer acts (KE81-KE123 cover the other
 * consequences). So the focus gets its KeyUp and MouseClick when
 * its layer acts again: late, at the first update after the switch back,
 * with no second whole-screen Click (the sampled buttons are shared), or
 * at the release.
 * While the scene's layer acts its button takes no press for the held
 * button: the port samples the buttons once for all layers (its own
 * strategy; layer-input's LI12 is alike in the port only, as AddLayer
 * there begins the new manager, 0x53d3a0 -> 0x546030).
 * The original's basis is inferred, with no native screen or dynamic
 * measurement: 0x578580 and 0x574d50 use the element's own key table
 * ([edi+8]), and while the box's manager does not run, its +0x40 and the
 * element's +0x1b4 stay. Some of these frames are inferred to differ
 * natively, and the expectations are the port's: SetActiveLayer does not
 * begin the scene's manager (0x57ba47), whose buttons were sampled up at
 * the last BeginInput, so natively the held button presumably is a new
 * press there (0x547f2d: whole-screen and button Trigger, the scene's
 * button as GetActiveParts and, at a release there, its Click and
 * GetClickNumber); and after a release under the scene's layer the box's
 * manager, which sampled the button down, presumably sends its
 * whole-screen KeyUp and Click again when it acts again. Neither is
 * followed here: both need each layer's own sampled buttons (the scene's
 * layer kept no focus here; KE115-KE119 have one, whose own table sees
 * such a press). Not compared with the original running. */
static int kl_layer_return_setup(void)
{
	li_setup(); key_clear_flag(false); keyboard_focus = true;
	li_button(KE_B, 100, 100, 200, 80, 7); PE_BeginInput();
	ke_frame(150, 140, 0); ke_frame(150, 140, 0);
	int box = ci_add(-1);
	li_button(KE_A, 100, 100, 200, 80, 3);
	KE_CHECK(ci_active() == box && box != li_scene, "box layer not active");
	PE_BeginInput(); ke_read();
	ke_frame(150, 140, 16);
	ke_key(VK_LBUTTON, true); struct ke_frame f = ke_frame(150, 140, 16);
	KE_CHECK(he_hll_int("GetActiveParts") == KE_A, "press did not focus the box's button");
	KE_EXPECT(&f, {VK_LBUTTON, KE_START, KE_START});
	f = ke_frame(150, 140, 16);
	KE_EXPECT(&f, {VK_LBUTTON, KE_DOWN, KE_DOWN});
	ci_set_active(li_scene); f = ke_read();
	KE_CHECK(ci_active() == li_scene, "scene layer not active");
	KE_EXPECT(&f, {VK_LBUTTON, 0, 0});
	f = ke_frame(150, 140, 16);
	KE_CHECK(he_hll_int("GetActiveParts") == 0, "scene layer reported the box's focus");
	KE_EXPECT(&f, {VK_LBUTTON, KE_DOWN, 0});
	return box;
}
static void kl_layer_return_late_up(void)
{
	int box = kl_layer_return_setup();
	ke_key(VK_LBUTTON, false); struct ke_frame f = ke_frame(150, 140, 16);
	KE_EXPECT(&f, {VK_LBUTTON, KE_UP | KE_CLICK, 0});
	KE_CHECK(he_hll_int("GetClickNumber") == 0, "release under the scene's layer set click number %d",
		he_hll_int("GetClickNumber"));
	f = ke_frame(150, 140, 16);
	KE_EXPECT(&f, {VK_LBUTTON, 0, 0});
	ci_set_active(box); f = ke_read();
	KE_CHECK(ci_active() == box, "box layer not active again");
	KE_EXPECT(&f, {VK_LBUTTON, 0, 0});
	f = ke_frame(150, 140, 16);
	KE_CHECK(he_hll_int("GetActiveParts") == KE_A, "focus not kept over the switch");
	KE_EXPECT(&f, {VK_LBUTTON, 0, KE_UP | KE_CLICK});
	KE_CHECK(he_hll_int("GetClickNumber") == KE_A, "late click number %d", he_hll_int("GetClickNumber"));
	f = ke_frame(150, 140, 16);
	KE_EXPECT(&f, {VK_LBUTTON, 0, 0});
	KE_CHECK(he_hll_int("GetClickNumber") == 0, "late click repeated");
}
static void kl_layer_return_held(void)
{
	int box = kl_layer_return_setup();
	ci_set_active(box); struct ke_frame f = ke_read();
	KE_CHECK(ci_active() == box, "box layer not active again");
	KE_EXPECT(&f, {VK_LBUTTON, 0, 0});
	f = ke_frame(150, 140, 16);
	KE_CHECK(he_hll_int("GetActiveParts") == KE_A, "focus not kept over the switch");
	KE_EXPECT(&f, {VK_LBUTTON, KE_DOWN, KE_DOWN});
	ke_key(VK_LBUTTON, false); f = ke_frame(150, 140, 16);
	KE_EXPECT(&f, {VK_LBUTTON, KE_UP | KE_CLICK, KE_UP | KE_CLICK});
	KE_CHECK(he_hll_int("GetClickNumber") == KE_A, "release after the switch back: click number %d",
		he_hll_int("GetClickNumber"));
	f = ke_frame(150, 140, 16);
	KE_EXPECT(&f, {VK_LBUTTON, 0, 0});
}

/* The original's manager per controller for its slots, its left press
 * mark (+4) and its focus (+0x1b4), followed by the port (KE81-KE123).
 * Read statically: 0x53e690 runs the input update 0x547c90 with ctrl+0x18
 * for the active controller alone (0x53c5b0), so another controller's
 * manager keeps its five slots while it does not act; 0x545c70 looks the
 * focus up in the update's own list, so a press in another controller
 * does not take it (0x546a4e..0x546aa2); GetActiveParts reads the active
 * controller's +0x280, which 0x53c240 refreshes in every controller's
 * update; a BeginInput within a scope and every EndInput reset every
 * manager's slot elements silently (0x58a720, 0x58a750 -> 0x53e8b0 ->
 * 0x5461a0 -> 0x577af0: all 80 key bits, +0x1ac, +0x1b0, +0x1b4, +4), and
 * a BeginInput or an EndInput that resumes a scope then sends LostFocus in
 * every controller, the overlay first and the stack from its bottom
 * (0x53e830 -> 0x53c750). The port keeps the three for each controller
 * that acted and lets the sampled buttons, the whole-screen key table,
 * the stored position and the cancellation suppression stay shared.
 * Inferred, with no native screen or dynamic measurement; synthetic
 * rectangles only. Three kinds of frames are inferred to differ natively;
 * there the expectations are the port's, as for KE79 and KE80:
 *  - a button held over SetActiveLayer is presumably a new press for the
 *    manager it designates (0x57ba47 does not begin it; 0x547f2d compares
 *    with the buttons that manager sampled last), which the port follows
 *    only for a focus that manager kept, through its own key table
 *    (KE115-KE119; with no element of that layer under the cursor the
 *    native press presumably finds nothing, with another one there that
 *    one presumably takes the focus), and one the old manager sampled down,
 *    released elsewhere and pressed again at the update of the switch back
 *    is presumably no press for it (KE112);
 *  - when the old manager acts again after a release elsewhere, it
 *    presumably sends its whole-screen KeyUp and Click again (its +0x40
 *    still has the button down);
 *  - a BeginInput or an EndInput presumably releases each manager's own
 *    whole-screen key table (0x5461a0 -> 0x547ad0), one whole-screen
 *    KeyUp from every manager that holds the key, where the port sends
 *    one. */

/* A press of `key` on A in the box's layer, one held update, SetActiveLayer
 * to the scene's layer (B under A, at the same place), one held update
 * there: kl_layer_return_setup for any mouse button. */
static int kl_kept_held_switch(int key)
{
	li_setup(); key_clear_flag(false); keyboard_focus = true;
	li_button(KE_B, 100, 100, 200, 80, 7); PE_BeginInput();
	ke_frame(150, 140, 0); ke_frame(150, 140, 0);
	int box = ci_add(-1);
	li_button(KE_A, 100, 100, 200, 80, 3);
	KE_CHECK(ci_active() == box && box != li_scene, "box layer not active");
	PE_BeginInput(); ke_read();
	ke_frame(150, 140, 16);
	ke_key(key, true); struct ke_frame f = ke_frame(150, 140, 16);
	KE_CHECK(he_hll_int("GetActiveParts") == KE_A, "press %d did not focus the box's button", key);
	KE_EXPECT(&f, {key, KE_START, KE_START});
	f = ke_frame(150, 140, 16);
	KE_EXPECT(&f, {key, KE_DOWN, KE_DOWN});
	ci_set_active(li_scene); f = ke_read();
	KE_CHECK(ci_active() == li_scene && f.n == 0, "scene layer not active, %d queued", f.n);
	/* Expectations are the port's; inferred native: a new press there. */
	f = ke_frame(150, 140, 16);
	KE_CHECK(he_hll_int("GetActiveParts") == 0, "scene layer reported the box's focus");
	KE_EXPECT(&f, {key, KE_DOWN, 0});
	return box;
}

/* KE81, KE82 (from KE79): the release in the scene's layer, a press on B
 * there and its release, then the switch back. B's press leaves the box's
 * focus (0x546a4e compares with the scene's own focus, 0x545c70), so A
 * gets no LostFocus; GetActiveParts is A as soon as the box is the active
 * layer (+0x280, 0x53c240), and the box's first update reads A's own
 * table (0x578580 -> 0x574d50): the left button went up meanwhile, so A
 * gets its KeyUp and MouseClick late, with GetClickNumber (0x5788d0), and
 * no focus message. Expectations are the port's; inferred native: B was
 * the scene's focus since the switch (a new press there), so it has no
 * Focus at its press and its Click at the scene's first release, and the
 * box's first update sends the whole-screen KeyUp and Click again. */
static void kl_kept_scene_press(int box)
{
	ke_key(VK_LBUTTON, false); struct ke_frame f = ke_frame(150, 140, 16);
	KE_EXPECT(&f, {VK_LBUTTON, KE_UP | KE_CLICK, 0});
	f = ke_frame(150, 140, 16);
	KE_EXPECT(&f, {VK_LBUTTON, 0, 0});
	ke_key(VK_LBUTTON, true); f = ke_frame(150, 140, 16);
	KE_CHECK(he_hll_int("GetActiveParts") == KE_B, "scene press: GetActiveParts %d", he_hll_int("GetActiveParts"));
	KE_CHECK(kc_count(&f, 19, KE_B) == 1, "scene press: B Focus %d", kc_count(&f, 19, KE_B));
	KE_CHECK(!kc_count(&f, 20, KE_A), "scene press: the box's focus got LostFocus");
	KE_CHECK(ke_count(&f, 15, KE_B, VK_LBUTTON) == 1 && ke_count(&f, 15, 0, VK_LBUTTON) == 1, "scene press: Trigger");
	KE_CHECK(!ke_count(&f, 18, KE_A, -1) && !ke_count(&f, 4, KE_A, -1), "scene press: the box's focus released");
	ke_key(VK_LBUTTON, false); f = ke_frame(150, 140, 16);
	KE_CHECK(he_hll_int("GetClickNumber") == KE_B && ke_count(&f, 4, KE_B, VK_LBUTTON) == 1,
		"scene release: click number %d", he_hll_int("GetClickNumber"));
	KE_CHECK(!ke_count(&f, 18, KE_A, -1) && !ke_count(&f, 4, KE_A, -1), "scene release: the box's focus released");
	f = ke_frame(150, 140, 16);
	KE_EXPECT(&f, {VK_LBUTTON, 0, 0});
	ci_set_active(box); f = ke_read();
	KE_CHECK(f.n == 0, "SetActiveLayer queued %d", f.n);
	KE_CHECK(he_hll_int("GetActiveParts") == KE_A, "back, before the box's update: GetActiveParts %d", he_hll_int("GetActiveParts"));
	f = ke_frame(150, 140, 16);
	KE_CHECK(he_hll_int("GetActiveParts") == KE_A, "back: GetActiveParts %d", he_hll_int("GetActiveParts"));
	KE_EXPECT(&f, {VK_LBUTTON, 0, KE_UP | KE_CLICK});
	KE_CHECK(he_hll_int("GetClickNumber") == KE_A, "back: late click number %d", he_hll_int("GetClickNumber"));
	KE_CHECK(!kc_count(&f, 19, KE_A) && !kc_count(&f, 20, KE_A) && !kc_count(&f, 20, KE_B), "back: focus messages");
	f = ke_frame(150, 140, 16);
	KE_EXPECT(&f, {VK_LBUTTON, 0, 0});
	KE_CHECK(he_hll_int("GetClickNumber") == 0, "back: late click repeated");
}
/* KE81: then a left press on A: a full Trigger, Down and first Press (A's
 * table was read at the switch back), no Focus (A stays the focus).
 * Inferred. */
static void kl_kept_moved_left(void)
{
	int box = kl_layer_return_setup(); kl_kept_scene_press(box);
	ke_key(VK_LBUTTON, true); struct ke_frame f = ke_frame(150, 140, 16);
	KE_EXPECT(&f, {VK_LBUTTON, KE_START, KE_START});
	KE_CHECK(!kc_count(&f, 19, KE_A), "left press: A focused again");
	ke_key(VK_LBUTTON, false); f = ke_frame(150, 140, 16);
	KE_EXPECT(&f, {VK_LBUTTON, KE_UP | KE_CLICK, KE_UP | KE_CLICK});
	KE_CHECK(he_hll_int("GetClickNumber") == KE_A, "left release: click number %d", he_hll_int("GetClickNumber"));
}
/* KE82: then a right press on A: the right button's events alone, no left
 * KeyUp or Click and no GetClickNumber. Inferred. */
static void kl_kept_moved_right(void)
{
	int box = kl_layer_return_setup(); kl_kept_scene_press(box);
	ke_key(VK_RBUTTON, true); struct ke_frame f = ke_frame(150, 140, 16);
	KE_EXPECT(&f, {VK_RBUTTON, KE_START, KE_START});
	KE_CHECK(he_hll_int("GetClickNumber") == 0, "right press: click number %d", he_hll_int("GetClickNumber"));
	ke_key(VK_RBUTTON, false); f = ke_frame(150, 140, 16);
	KE_EXPECT(&f, {VK_RBUTTON, KE_UP | KE_CLICK, KE_UP | KE_CLICK});
	KE_CHECK(he_hll_int("GetClickNumber") == 0, "right release: click number %d", he_hll_int("GetClickNumber"));
}

/* KE83, KE84: a BeginInput (KE83) or a nested EndInput (KE84) in the
 * scene's layer with the left button still held: the box's manager still
 * holds A in its left slot, so 0x53e8b0 -> 0x5461a0 -> 0x577af0 clears A
 * silently (not entered, no time, no focus) before 0x53e830 sends
 * LostFocus (none for A). Back in the box A is entered anew (MouseEnter);
 * a right press on A focuses it and has the right button's events alone.
 * Expectations are the port's; inferred native: the reset releases the
 * scene's and the box's whole-screen tables (two whole KeyUps), and the
 * box's manager, begun with the button held, sees a release when it acts
 * again (a second whole-screen Click). */
static void kl_kept_slot_reset(bool end)
{
	int box = kl_layer_return_setup();
	struct parts *a = parts_get(KE_A);
	KE_CHECK(a->is_hovered, "setup: A not entered");
	if (end) PE_EndInput(); else PE_BeginInput();
	struct ke_frame f = ke_read();
	KE_CHECK(!kc_count(&f, 20, KE_A), "the scene's %s: LostFocus to the box's slot element", end ? "End" : "Begin");
	KE_EXPECT(&f, {VK_LBUTTON, KE_UP, 0});
	KE_CHECK(!a->is_hovered && a->hover_time == 0, "the box's slot element: entered %d time %d", a->is_hovered, a->hover_time);
	f = ke_frame(150, 140, 16);
	KE_EXPECT(&f, {VK_LBUTTON, KE_START, 0});
	ke_key(VK_LBUTTON, false); f = ke_frame(150, 140, 16);
	KE_EXPECT(&f, {VK_LBUTTON, KE_UP | KE_CLICK, 0});
	KE_CHECK(he_hll_int("GetClickNumber") == 0, "scene release: click number %d", he_hll_int("GetClickNumber"));
	f = ke_frame(150, 140, 16);
	ci_set_active(box); f = ke_read();
	KE_CHECK(he_hll_int("GetActiveParts") == 0, "back, before the update: GetActiveParts %d", he_hll_int("GetActiveParts"));
	f = ke_frame(150, 140, 16);
	KE_CHECK(he_hll_int("GetActiveParts") == 0, "back: GetActiveParts %d", he_hll_int("GetActiveParts"));
	KE_EXPECT(&f, {VK_LBUTTON, 0, 0});
	KE_CHECK(kc_count(&f, 0, KE_A) == 1, "back: A MouseEnter %d, want 1", kc_count(&f, 0, KE_A));
	f = ke_frame(150, 140, 16);
	ke_key(VK_RBUTTON, true); f = ke_frame(150, 140, 16);
	KE_CHECK(he_hll_int("GetActiveParts") == KE_A && kc_count(&f, 19, KE_A) == 1,
		"right press: focus %d Focus %d", he_hll_int("GetActiveParts"), kc_count(&f, 19, KE_A));
	KE_EXPECT(&f, {VK_RBUTTON, KE_START, KE_START});
	KE_CHECK(he_hll_int("GetClickNumber") == 0, "right press: click number %d", he_hll_int("GetClickNumber"));
	ke_key(VK_RBUTTON, false); f = ke_frame(150, 140, 16);
	KE_EXPECT(&f, {VK_RBUTTON, KE_UP | KE_CLICK, KE_UP | KE_CLICK});
}
static void kl_kept_begin_reset(void) { kl_kept_slot_reset(false); }
static void kl_kept_end_reset(void) { kl_kept_slot_reset(true); }

/* Scene B, one BeginInput, the box added inside the scope (0x53d3a0 begins
 * its manager at once) with A, A entered. */
static int kl_kept_one_scope_setup(void)
{
	li_setup(); key_clear_flag(false); keyboard_focus = true;
	li_button(KE_B, 100, 100, 200, 80, 7); PE_BeginInput();
	ke_frame(150, 140, 0); ke_frame(150, 140, 0);
	int box = ci_add(-1);
	li_button(KE_A, 100, 100, 200, 80, 3);
	KE_CHECK(ci_active() == box && box != li_scene, "box layer not active");
	ke_read(); ke_frame(150, 140, 16);
	KE_CHECK(parts_get(KE_A)->is_hovered, "setup: A not entered");
	return box;
}
/* KE85: the outermost EndInput (0x58a750 at depth 1 runs 0x53e8b0 alone)
 * with A in the box's left slot: A is reset silently and keeps no focus,
 * so the next BeginInput sends it no LostFocus; back in the box a right
 * press has no left KeyUp or Click. Inferred. */
static void kl_kept_outer_end(void)
{
	int box = kl_kept_one_scope_setup();
	ke_key(VK_LBUTTON, true); struct ke_frame f = ke_frame(150, 140, 16);
	KE_EXPECT(&f, {VK_LBUTTON, KE_START, KE_START});
	f = ke_frame(150, 140, 16);
	ci_set_active(li_scene); ke_read();
	/* Expectations are the port's; inferred native: a new press there, so
	 * B (under the cursor) has the scene's focus and its Click at the
	 * release. */
	f = ke_frame(150, 140, 16);
	KE_EXPECT(&f, {VK_LBUTTON, KE_DOWN, 0});
	ke_key(VK_LBUTTON, false); f = ke_frame(150, 140, 16);
	KE_EXPECT(&f, {VK_LBUTTON, KE_UP | KE_CLICK, 0});
	f = ke_frame(150, 140, 16);
	PE_EndInput(); f = ke_read();
	/* Expectations are the port's; inferred native: the box's manager
	 * still holds the left button in its whole-screen table, so a whole
	 * KeyUp goes out here (0x5461a0 -> 0x547ad0). */
	KE_CHECK(f.n == 0, "outermost End queued %d", f.n);
	KE_CHECK(!parts_get(KE_A)->is_hovered, "outermost End: the box's slot element kept entered");
	PE_BeginInput(); f = ke_read();
	KE_CHECK(!kc_count(&f, 20, KE_A), "Begin after the outermost End: LostFocus to the reset element");
	f = ke_frame(150, 140, 16);
	ci_set_active(box); ke_read();
	f = ke_frame(150, 140, 16);
	KE_CHECK(he_hll_int("GetActiveParts") == 0, "back: GetActiveParts %d", he_hll_int("GetActiveParts"));
	KE_EXPECT(&f, {VK_LBUTTON, 0, 0});
	f = ke_frame(150, 140, 16);
	ke_key(VK_RBUTTON, true); f = ke_frame(150, 140, 16);
	KE_EXPECT(&f, {VK_RBUTTON, KE_START, KE_START});
	KE_CHECK(he_hll_int("GetClickNumber") == 0, "right press: click number %d", he_hll_int("GetClickNumber"));
	ke_key(VK_RBUTTON, false); ke_frame(150, 140, 16);
}
/* KE86 (guard): a focus outside the slots (clicked and released, then the
 * 'A' key held on it) is not reset by the outermost EndInput, which
 * releases the whole-screen table alone; the next BeginInput sends it
 * LostFocus (0x53c750) and its table keeps 'A' (0x577af0 resets slot
 * elements only), so the next left press on it has the late 'A' KeyUp.
 * Expectations are the port's; inferred native: the scene's manager has
 * 'A' up in its whole-screen table (a whole-screen 'A' Trigger at its
 * first update), and the EndInput releases both managers' tables (two
 * whole-screen 'A' KeyUps). */
static void kl_kept_focus_outside_slots(void)
{
	int box = kl_kept_one_scope_setup();
	ke_focus_a();
	ke_key('A', true); struct ke_frame f = ke_frame(150, 140, 16);
	KE_EXPECT(&f, {'A', KE_START, KE_START});
	ci_set_active(li_scene); ke_read();
	f = ke_frame(150, 140, 16);
	KE_EXPECT(&f, {'A', KE_DOWN, 0});
	f = ke_frame(150, 140, 16);
	PE_EndInput(); f = ke_read();
	KE_EXPECT(&f, {'A', KE_UP, 0});
	KE_CHECK(!kc_count(&f, 20, KE_A) && parts_get(KE_A)->is_hovered, "outermost End touched the focus outside the slots");
	PE_BeginInput(); f = ke_read();
	KE_CHECK(kc_count(&f, 20, KE_A) == 1, "Begin: the box's focus LostFocus %d, want 1", kc_count(&f, 20, KE_A));
	ke_key('A', false);
	f = ke_frame(150, 140, 16);
	ci_set_active(box); ke_read();
	f = ke_frame(150, 140, 16);
	KE_CHECK(he_hll_int("GetActiveParts") == 0, "back: GetActiveParts %d", he_hll_int("GetActiveParts"));
	KE_EXPECT(&f, {'A', 0, 0});
	ke_key(VK_LBUTTON, true); f = ke_frame(150, 140, 16);
	KE_EXPECT(&f, {VK_LBUTTON, KE_START, KE_START}, {'A', 0, KE_UP});
	ke_key(VK_LBUTTON, false); ke_frame(150, 140, 16);
}
/* KE87: the box's A and the scene's B each the focus of its own layer: the
 * scene's press does not send A LostFocus (0x545c70), and the next
 * BeginInput sends LostFocus to both in 0x53e830's order (the stack from
 * its bottom: B, then A). Inferred. GetActiveParts is 0 after it, with no
 * update between: the port's (PE_GetActivePartsNumber shows the focus
 * cleared by BeginInput at once); natively +0x280 is refreshed at the
 * next parts update (0x53c240), so it presumably reads A until then. */
static void kl_kept_two_focus_begin(void)
{
	li_setup(); key_clear_flag(false); keyboard_focus = true;
	li_button(KE_B, 100, 100, 200, 80, 7); PE_BeginInput();
	ke_frame(640, 600, 0); ke_frame(640, 600, 0);
	int box = ci_add(-1);
	li_button(KE_A, 100, 100, 200, 80, 3);
	PE_BeginInput(); ke_read();
	ke_frame(150, 140, 16);
	ke_focus_a();
	ke_frame(150, 140, 16);
	ci_set_active(li_scene); ke_read();
	ke_frame(150, 140, 16);
	ke_key(VK_LBUTTON, true); struct ke_frame f = ke_frame(150, 140, 16);
	KE_CHECK(kc_count(&f, 19, KE_B) == 1 && !kc_count(&f, 20, KE_A), "scene press: Focus B %d, LostFocus A %d",
		kc_count(&f, 19, KE_B), kc_count(&f, 20, KE_A));
	ke_key(VK_LBUTTON, false); ke_frame(150, 140, 16);
	ke_frame(150, 140, 16);
	PE_BeginInput(); f = ke_read();
	int ib = -1, ia = -1;
	for (int i = 0; i < f.n && i < KE_CAP; i++) {
		if (f.m[i].type != 20) continue;
		if (f.m[i].no == KE_B) ib = i;
		if (f.m[i].no == KE_A) ia = i;
	}
	KE_CHECK(kc_count(&f, 20, KE_B) == 1 && kc_count(&f, 20, KE_A) == 1 && ib < ia,
		"Begin: LostFocus B %d at %d, A %d at %d (the bottom layer first)", kc_count(&f, 20, KE_B), ib, kc_count(&f, 20, KE_A), ia);
	ci_set_active(box); ke_read();
	KE_CHECK(he_hll_int("GetActiveParts") == 0, "after the Begin: GetActiveParts %d", he_hll_int("GetActiveParts"));
}
/* KE88: back with the left button still held: the box's left slot and A's
 * press mark are kept, so A is entered in its down state (+4, 0x577c28);
 * a later box update without a position leaves A (0x5476b0: the left
 * slot's element is the focus), the next one with a position enters it
 * again (MouseEnter), still down; the release is an ordinary KeyUp and
 * Click (KE80). Inferred. */
static void kl_kept_return_held_look(void)
{
	int box = kl_layer_return_setup();
	struct parts *a = parts_get(KE_A);
	ci_set_active(box); ke_read();
	struct ke_frame f = ke_frame(150, 140, 16);
	KE_EXPECT(&f, {VK_LBUTTON, KE_DOWN, KE_DOWN});
	KE_CHECK(a->is_hovered && a->state == PARTS_STATE_CLICKED, "back, held: A entered %d state %d, want pressed",
		a->is_hovered, a->state);
	mouse_focus = false; f = ke_frame(150, 140, 16);
	KE_CHECK(kc_count(&f, 2, KE_A) == 1 && !a->is_hovered, "held, no position: A MouseLeave %d entered %d",
		kc_count(&f, 2, KE_A), a->is_hovered);
	mouse_focus = true; f = ke_frame(150, 140, 16);
	KE_CHECK(kc_count(&f, 0, KE_A) == 1 && a->is_hovered && a->state == PARTS_STATE_CLICKED,
		"held, the position again: A MouseEnter %d entered %d state %d", kc_count(&f, 0, KE_A), a->is_hovered, a->state);
	f = ke_frame(150, 140, 16);
	ke_key(VK_LBUTTON, false); f = ke_frame(150, 140, 16);
	KE_EXPECT(&f, {VK_LBUTTON, KE_UP | KE_CLICK, KE_UP | KE_CLICK});
}
/* KE89 (guard): two layers, nothing held over the switches: the box's
 * focus is GetActiveParts again at once when the box is active (+0x280),
 * nothing late, and the next BeginInput sends it one LostFocus.
 * Inferred. */
static void kl_kept_no_hold(void)
{
	li_setup(); key_clear_flag(false); keyboard_focus = true;
	li_button(KE_B, 100, 100, 200, 80, 7); PE_BeginInput();
	ke_frame(640, 600, 0); ke_frame(640, 600, 0);
	int box = ci_add(-1);
	li_button(KE_A, 100, 100, 200, 80, 3);
	PE_BeginInput(); ke_read();
	ke_frame(150, 140, 16);
	ke_focus_a();
	ke_frame(150, 140, 16);
	ci_set_active(li_scene); ke_read();
	struct ke_frame f = ke_frame(150, 140, 16);
	KE_EXPECT(&f, {VK_LBUTTON, 0, 0});
	KE_CHECK(he_hll_int("GetActiveParts") == 0, "scene: GetActiveParts %d", he_hll_int("GetActiveParts"));
	f = ke_frame(150, 140, 16);
	ci_set_active(box); ke_read();
	KE_CHECK(he_hll_int("GetActiveParts") == KE_A, "back, before the update: GetActiveParts %d", he_hll_int("GetActiveParts"));
	f = ke_frame(150, 140, 16);
	KE_EXPECT(&f, {VK_LBUTTON, 0, 0});
	KE_CHECK(he_hll_int("GetActiveParts") == KE_A && he_hll_int("GetClickNumber") == 0,
		"back: focus %d click number %d", he_hll_int("GetActiveParts"), he_hll_int("GetClickNumber"));
	f = ke_frame(150, 140, 16);
	PE_BeginInput(); f = ke_read();
	KE_CHECK(kc_count(&f, 20, KE_A) == 1, "Begin: LostFocus A %d, want 1", kc_count(&f, 20, KE_A));
}
/* KE90 (guard): the box's kept A released (0x53c140 drops it), and a new
 * parts with its number in the box's layer: not the box's focus, not
 * pressed, not in its slot (Begin does not reset it or send it
 * LostFocus), and the release does not click it. Inferred. */
static void kl_kept_released(void)
{
	int box = kl_layer_return_setup();
	PE_ReleaseParts(KE_A);
	struct ke_frame f = ke_frame(150, 140, 16);
	ci_set_active(box); ke_read();
	li_button(KE_A, 100, 100, 200, 80, 3);
	KE_CHECK(parts_get(KE_A)->controller_no == box, "the new A is on %d", parts_get(KE_A)->controller_no);
	KE_CHECK(he_hll_int("GetActiveParts") == 0, "the new A is the box's focus");
	f = ke_frame(150, 140, 16);
	KE_CHECK(he_hll_int("GetActiveParts") == 0, "back, held: focus %d", he_hll_int("GetActiveParts"));
	KE_CHECK(parts_get(KE_A)->is_hovered && parts_get(KE_A)->state != PARTS_STATE_CLICKED,
		"back, held: the new A entered %d state %d", parts_get(KE_A)->is_hovered, parts_get(KE_A)->state);
	f = ke_frame(150, 140, 16);
	PE_BeginInput(); f = ke_read();
	KE_CHECK(!kc_count(&f, 20, KE_A) && parts_get(KE_A)->is_hovered, "Begin: the new A reset or LostFocus");
	f = ke_frame(150, 140, 16);
	ke_key(VK_LBUTTON, false); f = ke_frame(150, 140, 16);
	KE_CHECK(he_hll_int("GetClickNumber") == 0 && !ke_count(&f, 18, KE_A, -1) && !ke_count(&f, 4, KE_A, -1),
		"the new A got the old one's release");
}
/* KE91 (guard): v15 keeps the earlier one focus for every layer: the
 * scene's press sends the box's A LostFocus and B Focus; back in the box
 * GetActiveParts is 0; in the scene again, before its update, it is B;
 * BeginInput sends B alone LostFocus.
 * The port's earlier behavior, pinned. */
static void kl_kept_v15(void)
{
	li_setup(); key_clear_flag(false); keyboard_focus = true; ain->version = 15;
	li_button(KE_B, 100, 100, 200, 80, 7); PE_BeginInput();
	ke_frame(640, 600, 0); ke_frame(640, 600, 0);
	int box = ci_add(-1);
	li_button(KE_A, 100, 100, 200, 80, 3);
	PE_BeginInput(); ke_read();
	ke_frame(150, 140, 16);
	ke_key(VK_LBUTTON, true); struct ke_frame f = ke_frame(150, 140, 16);
	KE_CHECK(kc_count(&f, 19, KE_A) == 1, "v15 press: Focus A %d", kc_count(&f, 19, KE_A));
	ke_frame(150, 140, 16);
	ci_set_active(li_scene); ke_read();
	ke_frame(150, 140, 16);
	ke_key(VK_LBUTTON, false); ke_frame(150, 140, 16);
	ke_frame(150, 140, 16);
	ke_key(VK_LBUTTON, true); f = ke_frame(150, 140, 16);
	KE_CHECK(kc_count(&f, 20, KE_A) == 1 && kc_count(&f, 19, KE_B) == 1, "v15 scene press: LostFocus A %d Focus B %d",
		kc_count(&f, 20, KE_A), kc_count(&f, 19, KE_B));
	ke_key(VK_LBUTTON, false); ke_frame(150, 140, 16);
	ke_frame(150, 140, 16);
	ci_set_active(box); ke_read();
	f = ke_frame(150, 140, 16);
	KE_CHECK(he_hll_int("GetActiveParts") == 0, "v15 back: GetActiveParts %d", he_hll_int("GetActiveParts"));
	f = ke_frame(150, 140, 16);
	ci_set_active(li_scene); ke_read();
	KE_CHECK(he_hll_int("GetActiveParts") == KE_B, "v15 scene again, before its update: GetActiveParts %d", he_hll_int("GetActiveParts"));
	f = ke_frame(150, 140, 16);
	PE_BeginInput(); f = ke_read();
	KE_CHECK(kc_count(&f, 20, KE_B) == 1 && !kc_count(&f, 20, KE_A), "v15 Begin: LostFocus B %d A %d",
		kc_count(&f, 20, KE_B), kc_count(&f, 20, KE_A));
}
/* KE92: the system overlay's O and the scene's B each the focus of its own
 * layer: the scene's press does not send O LostFocus, and the next
 * BeginInput sends O's first (0x53e830: +0x84 before the stack).
 * Inferred. */
static void kl_kept_overlay_order(void)
{
	enum { KE_O = 20303 };
	li_setup(); key_clear_flag(false); keyboard_focus = true;
	li_button(KE_B, 100, 100, 200, 80, 7); PE_BeginInput();
	ke_frame(640, 600, 0); ke_frame(640, 600, 0);
	int overlay = PE_get_system_controller();
	ci_set_active(overlay);
	li_button(KE_O, 700, 400, 200, 80, 9);
	KE_CHECK(parts_get(KE_O)->controller_no == overlay, "O on %d", parts_get(KE_O)->controller_no);
	ke_frame(750, 440, 16);
	ke_key(VK_LBUTTON, true); struct ke_frame f = ke_frame(750, 440, 16);
	KE_CHECK(kc_count(&f, 19, KE_O) == 1, "overlay press: Focus O %d", kc_count(&f, 19, KE_O));
	ke_key(VK_LBUTTON, false); ke_frame(750, 440, 16);
	ke_frame(750, 440, 16);
	ci_set_active(li_scene); ke_read();
	ke_frame(150, 140, 16);
	ke_key(VK_LBUTTON, true); f = ke_frame(150, 140, 16);
	KE_CHECK(kc_count(&f, 19, KE_B) == 1 && !kc_count(&f, 20, KE_O), "scene press: Focus B %d LostFocus O %d",
		kc_count(&f, 19, KE_B), kc_count(&f, 20, KE_O));
	ke_key(VK_LBUTTON, false); ke_frame(150, 140, 16);
	ke_frame(150, 140, 16);
	PE_BeginInput(); f = ke_read();
	int ib = -1, io = -1;
	for (int i = 0; i < f.n && i < KE_CAP; i++) {
		if (f.m[i].type != 20) continue;
		if (f.m[i].no == KE_B) ib = i;
		if (f.m[i].no == KE_O) io = i;
	}
	KE_CHECK(kc_count(&f, 20, KE_B) == 1 && kc_count(&f, 20, KE_O) == 1 && io < ib,
		"Begin: LostFocus O %d at %d, B %d at %d (the overlay first)", kc_count(&f, 20, KE_O), io, kc_count(&f, 20, KE_B), ib);
}
/* KE93: the right slot kept over the switch: the scene's BeginInput clears
 * A's whole table silently (0x577af0, all 80 keys) with no LostFocus, so a
 * later left press on A has no right KeyUp or Click. Expectations are the
 * port's; inferred native: two whole-screen KeyUps at the BeginInput, and
 * the box's manager, begun with the button held, sends a second
 * whole-screen Click when it acts again. */
static void kl_kept_right_slot_reset(void)
{
	int box = kl_kept_held_switch(VK_RBUTTON);
	PE_BeginInput(); struct ke_frame f = ke_read();
	KE_CHECK(!kc_count(&f, 20, KE_A) && !parts_get(KE_A)->is_hovered, "the scene's Begin: A LostFocus %d entered %d",
		kc_count(&f, 20, KE_A), parts_get(KE_A)->is_hovered);
	ke_frame(150, 140, 16);
	ke_key(VK_RBUTTON, false); ke_frame(150, 140, 16);
	ke_frame(150, 140, 16);
	ci_set_active(box); ke_read();
	f = ke_frame(150, 140, 16);
	KE_EXPECT(&f, {VK_RBUTTON, 0, 0});
	f = ke_frame(150, 140, 16);
	ke_key(VK_LBUTTON, true); f = ke_frame(150, 140, 16);
	KE_EXPECT(&f, {VK_LBUTTON, KE_START, KE_START});
	KE_CHECK(!ke_count(&f, 18, KE_A, VK_RBUTTON) && !ke_count(&f, 4, KE_A, VK_RBUTTON), "left press: A's kept right bit released");
	ke_key(VK_LBUTTON, false); ke_frame(150, 140, 16);
}
/* KE94: a left drag begun on A, the switch with the button held and the
 * release in the scene's layer: the box's left slot still holds the begun
 * drag, so when the box acts again its release ends it (0x547460: DragEnd,
 * mode 1 as for every release without a drop target here), once.
 * Inferred. */
static void kl_kept_drag_end(void)
{
	li_setup(); key_clear_flag(false); keyboard_focus = true;
	li_button(KE_B, 400, 100, 200, 80, 7); PE_BeginInput();
	ke_frame(640, 600, 0); ke_frame(640, 600, 0);
	int box = ci_add(-1);
	li_button(KE_A, 100, 100, 200, 80, 3);
	PE_BeginInput(); ke_read();
	ke_frame(150, 140, 16);
	ke_key(VK_LBUTTON, true); ke_frame(150, 140, 16);
	struct ke_frame f = ke_frame(170, 145, 16);
	KE_CHECK(kc_count(&f, 7, KE_A) == 1 && kc_count(&f, 8, KE_A) == 1, "drag: DragBegin %d Dragging %d",
		kc_count(&f, 7, KE_A), kc_count(&f, 8, KE_A));
	ci_set_active(li_scene); ke_read();
	f = ke_frame(170, 145, 16);
	KE_CHECK(!kc_count(&f, 10, KE_A), "scene: the box's drag ended");
	ke_key(VK_LBUTTON, false); f = ke_frame(170, 145, 16);
	KE_CHECK(!kc_count(&f, 10, KE_A), "scene release: the box's drag ended");
	f = ke_frame(170, 145, 16);
	ci_set_active(box); ke_read();
	f = ke_frame(170, 145, 16);
	int mode = -1;
	for (int i = 0; i < f.n && i < KE_CAP; i++)
		if (f.m[i].type == 10 && f.m[i].no == KE_A && f.m[i].n == 2 && f.m[i].v[0] == VK_LBUTTON) mode = f.m[i].v[1];
	KE_CHECK(kc_count(&f, 10, KE_A) == 1 && mode == 1, "back: DragEnd %d mode %d, want 1, 1", kc_count(&f, 10, KE_A), mode);
	f = ke_frame(170, 145, 16);
	KE_CHECK(!kc_count(&f, 10, KE_A), "back: DragEnd repeated");
}

/* KE95, KE96: the switch back with another mouse button held, pressed in
 * the scene's layer. The box's manager sampled that button up when it
 * last ran (it went down only in the scene's layer), so at its first
 * update the held button is a new press there (0x547f2d), which cancels
 * the kept slot (0x548153..0x5481b0: 0x5462b0 for the left slot,
 * 0x546470 for the right one, +0x208; DragEnd's mode from 0x577660). A
 * button that manager sampled down, held since before the slot was
 * filled, is no new press there and cancels nothing (KE107). A movable
 * left source goes back where it was, then DragBegin (unless the drag
 * began), Draging back at the press point and DragEnd mode 2, and the
 * suppression is set. The slot's element stays the focus. Expectations
 * are the port's; inferred
 * native: that button also starts in the box's whole-screen table (a
 * whole-screen Trigger, Down and first Press there). */
static void kl_kept_return_cancel_left(void)
{
	li_setup(); key_clear_flag(false); keyboard_focus = true;
	li_button(KE_B, 400, 100, 200, 80, 7); PE_BeginInput();
	ke_frame(640, 600, 0); ke_frame(640, 600, 0);
	int box = ci_add(-1);
	li_button(KE_A, 100, 100, 200, 80, 3);
	struct parts *a = parts_get(KE_A); a->draggable = true;
	PE_BeginInput(); ke_read();
	ke_frame(150, 140, 16);
	ke_key(VK_LBUTTON, true); ke_frame(150, 140, 16);
	struct ke_frame f = ke_frame(170, 145, 16);
	KE_CHECK(kc_count(&f, 7, KE_A) == 1 && kc_count(&f, 8, KE_A) == 1 && a->local.pos.x == 120 && a->local.pos.y == 105,
		"drag: DragBegin %d Dragging %d at (%d, %d)", kc_count(&f, 7, KE_A), kc_count(&f, 8, KE_A), a->local.pos.x, a->local.pos.y);
	ci_set_active(li_scene); ke_read();
	ke_frame(170, 145, 16);
	ke_key(VK_RBUTTON, true); f = ke_frame(170, 145, 16);
	KE_CHECK(!kc_count(&f, 10, KE_A) && !kc_count(&f, 20, KE_A), "scene right press: A DragEnd %d LostFocus %d",
		kc_count(&f, 10, KE_A), kc_count(&f, 20, KE_A));
	ke_frame(170, 145, 16);
	ci_set_active(box); f = ke_read();
	KE_CHECK(f.n == 0, "SetActiveLayer queued %d", f.n);
	f = ke_frame(170, 145, 16);
	kc_cancel_events(&f, VK_LBUTTON, 2, true);
	KE_CHECK(a->local.pos.x == 100 && a->local.pos.y == 100, "back: A at (%d, %d), want (100, 100)", a->local.pos.x, a->local.pos.y);
	KE_CHECK(he_hll_int("GetActiveParts") == KE_A && he_hll_int("GetClickNumber") == 0,
		"back: focus %d click number %d", he_hll_int("GetActiveParts"), he_hll_int("GetClickNumber"));
	KE_CHECK(!kc_count(&f, 19, KE_A) && !kc_count(&f, 20, KE_A), "back: focus messages");
	KE_EXPECT(&f, {VK_LBUTTON, KE_DOWN, KE_DOWN}, {VK_RBUTTON, KE_DOWN, KE_START});
	f = ke_frame(180, 150, 16);
	kc_no_drag_events(&f);
	KE_CHECK(a->local.pos.x == 100 && a->local.pos.y == 100, "after the cancel: A at (%d, %d)", a->local.pos.x, a->local.pos.y);
	KE_EXPECT(&f, {VK_LBUTTON, KE_DOWN, KE_DOWN}, {VK_RBUTTON, KE_DOWN, KE_DOWN});
	ke_key(VK_RBUTTON, false); ke_key(VK_LBUTTON, false); f = ke_frame(180, 150, 16);
	kc_no_drag_events(&f);
}
static void kl_kept_return_cancel_right(void)
{
	li_setup(); key_clear_flag(false); keyboard_focus = true;
	li_button(KE_B, 400, 100, 200, 80, 7); PE_BeginInput();
	ke_frame(150, 140, 0); ke_frame(150, 140, 0);
	int box = ci_add(-1);
	li_button(KE_A, 100, 100, 200, 80, 3);
	PE_BeginInput(); ke_read();
	/* Two updates before the press: the BeginInput clears the stored
	 * position and the first update does not store one (0x546030,
	 * 0x548381), so the press is no move and the right drag (no
	 * threshold) has not begun when it is cancelled. */
	ke_frame(150, 140, 16); ke_frame(150, 140, 16);
	ke_key(VK_RBUTTON, true); struct ke_frame f = ke_frame(150, 140, 16);
	KE_EXPECT(&f, {VK_RBUTTON, KE_START, KE_START});
	f = ke_frame(150, 140, 16);
	ci_set_active(li_scene); ke_read();
	/* Expectations are the port's; inferred native: a new press there. */
	f = ke_frame(150, 140, 16);
	KE_EXPECT(&f, {VK_RBUTTON, KE_DOWN, 0});
	ke_key(VK_LBUTTON, true); f = ke_frame(150, 140, 16);
	KE_EXPECT(&f, {VK_LBUTTON, KE_START, 0}, {VK_RBUTTON, KE_DOWN, 0});
	KE_CHECK(!kc_count(&f, 10, KE_A) && !kc_count(&f, 20, KE_A), "scene left press: A DragEnd %d LostFocus %d",
		kc_count(&f, 10, KE_A), kc_count(&f, 20, KE_A));
	f = ke_frame(150, 140, 16);
	ci_set_active(box); f = ke_read();
	KE_CHECK(f.n == 0, "SetActiveLayer queued %d", f.n);
	f = ke_frame(150, 140, 16);
	kc_cancel_events(&f, VK_RBUTTON, 2, false);
	KE_CHECK(he_hll_int("GetActiveParts") == KE_A, "back: focus %d", he_hll_int("GetActiveParts"));
	KE_CHECK(!kc_count(&f, 19, KE_A) && !kc_count(&f, 20, KE_A), "back: focus messages");
	KE_EXPECT(&f, {VK_LBUTTON, KE_DOWN, KE_START}, {VK_RBUTTON, KE_DOWN, KE_DOWN});
	f = ke_frame(150, 140, 16);
	kc_no_drag_events(&f);
	ke_key(VK_RBUTTON, false); ke_key(VK_LBUTTON, false); f = ke_frame(150, 140, 16);
	kc_no_drag_events(&f);
}

/* KE97: the release in the scene's layer, then a BeginInput there: the
 * box's manager has not run since, so A is still in its left slot and is
 * reset silently (0x53e8b0 -> 0x5461a0 -> 0x577af0), with no LostFocus;
 * back in the box nothing is late and a right press has no left KeyUp or
 * Click. Expectations are the port's; inferred native: the box's manager
 * still has the left button in its whole-screen table, released at the
 * BeginInput (a whole KeyUp). */
static void kl_kept_begin_after_release(void)
{
	int box = kl_layer_return_setup();
	struct parts *a = parts_get(KE_A);
	ke_key(VK_LBUTTON, false); struct ke_frame f = ke_frame(150, 140, 16);
	KE_EXPECT(&f, {VK_LBUTTON, KE_UP | KE_CLICK, 0});
	f = ke_frame(150, 140, 16);
	PE_BeginInput(); f = ke_read();
	KE_CHECK(!kc_count(&f, 20, KE_A), "Begin after the release: LostFocus to the box's slot element");
	KE_EXPECT(&f, {VK_LBUTTON, 0, 0});
	KE_CHECK(!a->is_hovered && a->hover_time == 0, "Begin after the release: A entered %d time %d", a->is_hovered, a->hover_time);
	f = ke_frame(150, 140, 16);
	ci_set_active(box); ke_read();
	f = ke_frame(150, 140, 16);
	KE_EXPECT(&f, {VK_LBUTTON, 0, 0});
	KE_CHECK(kc_count(&f, 0, KE_A) == 1 && he_hll_int("GetActiveParts") == 0,
		"back: A MouseEnter %d focus %d", kc_count(&f, 0, KE_A), he_hll_int("GetActiveParts"));
	f = ke_frame(150, 140, 16);
	ke_key(VK_RBUTTON, true); f = ke_frame(150, 140, 16);
	KE_EXPECT(&f, {VK_RBUTTON, KE_START, KE_START});
	KE_CHECK(he_hll_int("GetClickNumber") == 0, "right press: click number %d", he_hll_int("GetClickNumber"));
	ke_key(VK_RBUTTON, false); ke_frame(150, 140, 16);
}
/* KE98-KE100: back with `key` still held (KE80), then a BeginInput in the
 * box's layer: the box's manager still holds A in that key's slot, so A is
 * reset silently (0x577af0) with no LostFocus; the release clicks nothing
 * local and another button's press on A has no late KeyUp or Click of
 * `key`. Expectations are the port's; inferred native: the scene's
 * manager also holds `key` in its whole-screen table (a new press there),
 * so two whole KeyUps go out at the BeginInput. */
static void kl_kept_return_held_begin(int key)
{
	int box = kl_kept_held_switch(key);
	struct parts *a = parts_get(KE_A);
	ci_set_active(box); ke_read();
	struct ke_frame f = ke_frame(150, 140, 16);
	KE_CHECK(he_hll_int("GetActiveParts") == KE_A, "back, held: focus %d", he_hll_int("GetActiveParts"));
	KE_EXPECT(&f, {key, KE_DOWN, KE_DOWN});
	f = ke_frame(150, 140, 16);
	PE_BeginInput(); f = ke_read();
	KE_CHECK(!kc_count(&f, 20, KE_A), "the box's Begin: LostFocus to the element in its %d slot", key);
	KE_EXPECT(&f, {key, KE_UP, 0});
	KE_CHECK(!a->is_hovered && a->hover_time == 0, "the box's Begin: A entered %d time %d", a->is_hovered, a->hover_time);
	f = ke_frame(150, 140, 16);
	KE_EXPECT(&f, {key, KE_START, 0});
	ke_key(key, false); f = ke_frame(150, 140, 16);
	KE_EXPECT(&f, {key, KE_UP | KE_CLICK, 0});
	KE_CHECK(he_hll_int("GetClickNumber") == 0, "release: click number %d", he_hll_int("GetClickNumber"));
	f = ke_frame(150, 140, 16);
	int other = key == VK_LBUTTON ? VK_RBUTTON : VK_LBUTTON;
	ke_key(other, true); f = ke_frame(150, 140, 16);
	KE_EXPECT(&f, {other, KE_START, KE_START});
	KE_CHECK(!ke_count(&f, 18, KE_A, key) && !ke_count(&f, 4, KE_A, key) && he_hll_int("GetClickNumber") == 0,
		"other press: late KeyUp/Click of %d, click number %d", key, he_hll_int("GetClickNumber"));
	ke_key(other, false); ke_frame(150, 140, 16);
}
static void kl_kept_return_held_begin_left(void) { kl_kept_return_held_begin(VK_LBUTTON); }
static void kl_kept_return_held_begin_right(void) { kl_kept_return_held_begin(VK_RBUTTON); }
static void kl_kept_return_held_begin_middle(void) { kl_kept_return_held_begin(VK_MBUTTON); }
/* KE101 (guard): KE79's late click, then a BeginInput in the box's layer:
 * the release ended the box's slot (0x547460), so A is a focus outside the
 * slots and gets its LostFocus (0x53c750), not a reset. Expectations are
 * the port's; inferred native: the whole-screen KeyUp and Click again at
 * the switch back, as for KE79. */
static void kl_kept_late_click_begin(void)
{
	int box = kl_layer_return_setup();
	ke_key(VK_LBUTTON, false); struct ke_frame f = ke_frame(150, 140, 16);
	f = ke_frame(150, 140, 16);
	ci_set_active(box); ke_read();
	f = ke_frame(150, 140, 16);
	KE_EXPECT(&f, {VK_LBUTTON, 0, KE_UP | KE_CLICK});
	f = ke_frame(150, 140, 16);
	PE_BeginInput(); f = ke_read();
	KE_CHECK(kc_count(&f, 20, KE_A) == 1 && parts_get(KE_A)->is_hovered,
		"Begin after the late click: LostFocus A %d entered %d", kc_count(&f, 20, KE_A), parts_get(KE_A)->is_hovered);
	KE_EXPECT(&f, {VK_LBUTTON, 0, 0});
}
/* KE102, KE103 (guard): another button pressed in the scene's layer and
 * held with the left one over the switch back: the box's manager cancels
 * A's slot then (KE95), so a BeginInput in the box does not reset A, which
 * stays entered (no MouseEnter after it). Inferred. */
static void kl_kept_other_held(int other)
{
	int box = kl_layer_return_setup();
	ke_key(other, true); struct ke_frame f = ke_frame(150, 140, 16);
	f = ke_frame(150, 140, 16);
	ci_set_active(box); ke_read();
	f = ke_frame(150, 140, 16);
	f = ke_frame(150, 140, 16);
	PE_BeginInput(); f = ke_read();
	f = ke_frame(150, 140, 16);
	KE_CHECK(!kc_count(&f, 0, KE_A) && parts_get(KE_A)->is_hovered,
		"after the box's Begin: A MouseEnter %d entered %d (its slot was cancelled)", kc_count(&f, 0, KE_A), parts_get(KE_A)->is_hovered);
	ke_key(VK_LBUTTON, false); ke_key(other, false); ke_frame(150, 140, 16);
}
static void kl_kept_other_held_right(void) { kl_kept_other_held(VK_RBUTTON); }
static void kl_kept_other_held_middle(void) { kl_kept_other_held(VK_MBUTTON); }
/* KE104: the 'A' key held on A with the left button, both released in the
 * scene's layer and a press on B there: back in the box A's own table goes
 * up at its first update (0x578580: late left KeyUp and Click, 'A' KeyUp,
 * GetClickNumber), and a right press on A then has the right button's
 * events alone. Expectations are the port's; inferred native: a new press
 * of both keys for the scene's manager at the switch, and the box's
 * whole-screen left KeyUp, Click and 'A' KeyUp again at the switch back. */
static void kl_kept_moved_keys(void)
{
	li_setup(); key_clear_flag(false); keyboard_focus = true;
	li_button(KE_B, 100, 100, 200, 80, 7); PE_BeginInput();
	ke_frame(150, 140, 0); ke_frame(150, 140, 0);
	int box = ci_add(-1);
	li_button(KE_A, 100, 100, 200, 80, 3);
	PE_BeginInput(); ke_read();
	ke_frame(150, 140, 16);
	ke_key(VK_LBUTTON, true); struct ke_frame f = ke_frame(150, 140, 16);
	KE_EXPECT(&f, {VK_LBUTTON, KE_START, KE_START});
	ke_key('A', true); f = ke_frame(150, 140, 16);
	KE_EXPECT(&f, {VK_LBUTTON, KE_DOWN, KE_DOWN}, {'A', KE_START, KE_START});
	ci_set_active(li_scene); ke_read();
	f = ke_frame(150, 140, 16);
	KE_EXPECT(&f, {VK_LBUTTON, KE_DOWN, 0}, {'A', KE_DOWN, 0});
	ke_key(VK_LBUTTON, false); ke_key('A', false); f = ke_frame(150, 140, 16);
	KE_EXPECT(&f, {VK_LBUTTON, KE_UP | KE_CLICK, 0}, {'A', KE_UP, 0});
	f = ke_frame(150, 140, 16);
	ke_key(VK_LBUTTON, true); f = ke_frame(150, 140, 16);
	KE_CHECK(he_hll_int("GetActiveParts") == KE_B && !kc_count(&f, 20, KE_A),
		"scene press: focus %d, A LostFocus %d", he_hll_int("GetActiveParts"), kc_count(&f, 20, KE_A));
	ke_key(VK_LBUTTON, false); ke_frame(150, 140, 16);
	ke_frame(150, 140, 16);
	ci_set_active(box); ke_read();
	f = ke_frame(150, 140, 16);
	KE_EXPECT(&f, {VK_LBUTTON, 0, KE_UP | KE_CLICK}, {'A', 0, KE_UP});
	KE_CHECK(he_hll_int("GetClickNumber") == KE_A, "back: late click number %d", he_hll_int("GetClickNumber"));
	f = ke_frame(150, 140, 16);
	KE_EXPECT(&f, {VK_LBUTTON, 0, 0});
	ke_key(VK_RBUTTON, true); f = ke_frame(150, 140, 16);
	KE_EXPECT(&f, {VK_RBUTTON, KE_START, KE_START});
	KE_CHECK(he_hll_int("GetClickNumber") == 0, "right press: click number %d", he_hll_int("GetClickNumber"));
	ke_key(VK_RBUTTON, false); ke_frame(150, 140, 16);
}
/* KE105 (guard): the scene's BeginInput resets A (KE83) and empties the
 * box's slots (0x5461a0), so after the switch back with the button still
 * held a BeginInput in the box does not reset A again: A stays entered.
 * Inferred. */
static void kl_kept_begin_twice(void)
{
	int box = kl_layer_return_setup();
	PE_BeginInput(); ke_read();
	struct ke_frame f = ke_frame(150, 140, 16);
	ci_set_active(box); ke_read();
	f = ke_frame(150, 140, 16);
	f = ke_frame(150, 140, 16);
	PE_BeginInput(); f = ke_read();
	f = ke_frame(150, 140, 16);
	KE_CHECK(!kc_count(&f, 0, KE_A) && parts_get(KE_A)->is_hovered,
		"after the second Begin: A MouseEnter %d entered %d", kc_count(&f, 0, KE_A), parts_get(KE_A)->is_hovered);
	ke_key(VK_LBUTTON, false); ke_frame(150, 140, 16);
}
/* KE106 (guard): v13 with two controllers walks every parts of every
 * controller on the old queue (layer-input LI9): a left press held over a
 * switch of the active controller and released after it clicks the parts
 * as before, with nothing on the v14 queue; no kept manager is involved.
 * The port's earlier behavior, pinned. */
static void kl_kept_v13(void)
{
	he_env(); br_input_setup(); ain->version = 13;
	extern bool parts_multi_controller; parts_multi_controller = true;
	probe_init_parts_table();
	extern bool key_state[];
	key_state[VK_LBUTTON] = key_state[VK_RBUTTON] = key_state[VK_MBUTTON] = false;
	br_box(KE_B, 400, 100, 200, 80, true);
	PE_AddController(-1);
	br_box(KE_A, 100, 100, 200, 80, true);
	KE_CHECK(parts_get(KE_B)->controller_no == 0 && parts_get(KE_A)->controller_no == 1 && PE_get_active_controller() == 1,
		"v13 setup: B on %d, A on %d, active %d", parts_get(KE_B)->controller_no, parts_get(KE_A)->controller_no,
		PE_get_active_controller());
	PE_BeginInput(); PE_ReleaseMessage();
	he_update(640, 600, false, 16); he_update(640, 600, false, 16); PE_ReleaseMessage();
	he_update(150, 140, false, 16); PE_ReleaseMessage();
	int types[16], nos[16];
	struct he_frame q = he_update(150, 140, true, 16);
	int n = li_old_msgs(types, nos, 16);
	KE_CHECK(li_old_count(n, types, nos, 14, KE_A) == 1 && q.n == 0, "v13 press: Trigger %d (%d messages, %d on the v14 queue)",
		li_old_count(n, types, nos, 14, KE_A), n, q.n);
	he_update(150, 140, true, 16); PE_ReleaseMessage();
	PE_set_active_controller(0);
	q = he_update(150, 140, true, 16);
	n = li_old_msgs(types, nos, 16);
	KE_CHECK(li_old_count(n, types, nos, 15, KE_A) == 1 && q.n == 0, "v13 held over the switch: Down %d (%d messages, %d on the v14 queue)",
		li_old_count(n, types, nos, 15, KE_A), n, q.n);
	q = he_update(150, 140, false, 16);
	n = li_old_msgs(types, nos, 16);
	KE_CHECK(li_old_count(n, types, nos, 5, KE_A) == 1 && li_old_count(n, types, nos, 17, KE_A) == 1 && q.n == 0,
		"v13 release after the switch: Click %d KeyUp %d (%d messages, %d on the v14 queue)", li_old_count(n, types, nos, 5, KE_A),
		li_old_count(n, types, nos, 17, KE_A), n, q.n);
	KE_CHECK(he_hll_int("GetClickNumber") == KE_A, "v13 click number %d", he_hll_int("GetClickNumber"));
	he_update(150, 140, false, 16); PE_ReleaseMessage();
	PE_set_active_controller(1);
	q = he_update(150, 140, false, 16);
	n = li_old_msgs(types, nos, 16);
	KE_CHECK(!li_old_count(n, types, nos, 5, KE_A) && !li_old_count(n, types, nos, 17, KE_A) && q.n == 0,
		"v13 back: Click %d KeyUp %d (%d on the v14 queue)", li_old_count(n, types, nos, 5, KE_A),
		li_old_count(n, types, nos, 17, KE_A), q.n);
	he_update(150, 140, false, 16); PE_ReleaseMessage();
	PE_BeginInput();
	KE_CHECK(PE_GetMessageType() == 0 && br_msg_type_raw() < 0, "v13 Begin queued a message");
	PE_EndInput(); PE_EndInput();
}
/* KE107, KE110, KE111: another mouse button held since the box's
 * BeginInput (sampled down by 0x546030, so it has no slot and sets no
 * suppression: 0x548153..0x5481b0 finds that button's slot empty, as in
 * kc_outside_other_release), and a press of `key` on A under it fills
 * that key's slot (left for KE107 with the right button held and KE110
 * with the middle one, right for KE111 with the left one). Both held over
 * the switch to the scene's layer and back: the box's manager sampled the
 * other button down when it last ran, so at its first update that button
 * is no new press (0x547f2d) and A's slot is not cancelled (no DragBegin,
 * Draging or DragEnd); a BeginInput in the box then resets A silently, as
 * it is still in the slot (0x53e8b0 -> 0x5461a0 -> 0x577af0): no
 * LostFocus, not entered. Inferred. */
static int kl_kept_since_begin_setup(int held, int key)
{
	li_setup(); key_clear_flag(false); keyboard_focus = true;
	li_button(KE_B, 400, 100, 200, 80, 7); PE_BeginInput();
	ke_frame(640, 600, 0); ke_frame(640, 600, 0);
	int box = ci_add(-1);
	li_button(KE_A, 100, 100, 200, 80, 3);
	ke_key(held, true);
	PE_BeginInput(); ke_read();
	/* Two updates before the press: the BeginInput clears the stored
	 * position and the first update does not store one (0x546030,
	 * 0x548381), so the press is no move. */
	ke_frame(150, 140, 16); ke_frame(150, 140, 16);
	ke_key(key, true); struct ke_frame f = ke_frame(150, 140, 16);
	KE_CHECK(he_hll_int("GetActiveParts") == KE_A, "press %d under the held %d: focus %d", key, held,
		he_hll_int("GetActiveParts"));
	KE_CHECK(!kc_count(&f, 7, KE_A) && !kc_count(&f, 8, KE_A) && !kc_count(&f, 10, KE_A),
		"press %d under the held %d: drag events %d %d %d", key, held, kc_count(&f, 7, KE_A),
		kc_count(&f, 8, KE_A), kc_count(&f, 10, KE_A));
	ke_frame(150, 140, 16);
	ci_set_active(li_scene); ke_read();
	ke_frame(150, 140, 16); ke_frame(150, 140, 16);
	return box;
}
static void kl_kept_other_held_since_begin(int held, int key)
{
	int box = kl_kept_since_begin_setup(held, key);
	ci_set_active(box); ke_read();
	struct ke_frame f = ke_frame(150, 140, 16);
	KE_CHECK(!kc_count(&f, 7, KE_A) && !kc_count(&f, 8, KE_A) && !kc_count(&f, 10, KE_A),
		"back: A's %d slot cancelled: DragBegin %d Draging %d DragEnd %d", key, kc_count(&f, 7, KE_A),
		kc_count(&f, 8, KE_A), kc_count(&f, 10, KE_A));
	KE_CHECK(he_hll_int("GetActiveParts") == KE_A, "back: focus %d", he_hll_int("GetActiveParts"));
	ke_frame(150, 140, 16);
	PE_BeginInput(); f = ke_read();
	KE_CHECK(!kc_count(&f, 20, KE_A) && !parts_get(KE_A)->is_hovered,
		"the box's Begin: A LostFocus %d entered %d (still in its %d slot)", kc_count(&f, 20, KE_A),
		parts_get(KE_A)->is_hovered, key);
	ke_key(key, false); ke_key(held, false); ke_frame(150, 140, 16);
}
static void kl_kept_right_held_since_begin(void) { kl_kept_other_held_since_begin(VK_RBUTTON, VK_LBUTTON); }
static void kl_kept_middle_held_since_begin(void) { kl_kept_other_held_since_begin(VK_MBUTTON, VK_LBUTTON); }
static void kl_kept_left_held_since_begin(void) { kl_kept_other_held_since_begin(VK_LBUTTON, VK_RBUTTON); }
/* KE112: KE107's start, then the right button released in the scene's
 * layer and pressed again in the update of the switch back. For the
 * port's shared sampled buttons that is a new right press, which cancels
 * A's left slot there (KE95's messages, the drag not begun); A stays the
 * focus. Expectations are the port's; inferred native: the box's manager
 * sampled the right button down when it last ran and sees it down again,
 * so it has no press and cancels nothing. */
static void kl_kept_return_repress(void)
{
	int box = kl_kept_since_begin_setup(VK_RBUTTON, VK_LBUTTON);
	ke_key(VK_RBUTTON, false); ke_frame(150, 140, 16);
	ke_frame(150, 140, 16);
	ci_set_active(box); ke_read();
	ke_key(VK_RBUTTON, true); struct ke_frame f = ke_frame(150, 140, 16);
	kc_cancel_events(&f, VK_LBUTTON, 2, false);
	KE_CHECK(he_hll_int("GetActiveParts") == KE_A && !kc_count(&f, 19, KE_A) && !kc_count(&f, 20, KE_A),
		"back with the press: focus %d, focus messages", he_hll_int("GetActiveParts"));
	f = ke_frame(150, 140, 16);
	kc_no_drag_events(&f);
	ke_key(VK_LBUTTON, false); ke_key(VK_RBUTTON, false); ke_frame(150, 140, 16);
}
/* KE108 (guard): kc_end_cancel_residue across a switch. A right press
 * cancels A's left slot in the box (0x5462b0 drops the reference only,
 * 0x548530), so A stays the box's focus in no slot with both buttons in
 * its own table; held over the switch to the scene's layer, a BeginInput
 * there sends A LostFocus (0x53c750) and does not reset it (0x5461a0
 * resets slot elements only): A stays entered and its table keeps both
 * buttons. Back in the box a left press focuses A with no left Trigger and
 * releases the kept right button (KeyUp and Click). Inferred: the original
 * keeps this residue as well. Expectations are the port's for the first
 * update back; inferred native: the box's manager, begun again by that
 * BeginInput with both buttons down (0x546030), sends their whole-screen
 * KeyUp and Click there. */
static void kl_kept_cancelled_residue(void)
{
	li_setup(); key_clear_flag(false); keyboard_focus = true;
	li_button(KE_B, 400, 100, 200, 80, 7); PE_BeginInput();
	ke_frame(640, 600, 0); ke_frame(640, 600, 0);
	int box = ci_add(-1);
	li_button(KE_A, 100, 100, 200, 80, 3);
	PE_BeginInput(); ke_read();
	ke_frame(150, 140, 16);
	ke_key(VK_LBUTTON, true); ke_frame(150, 140, 16);
	ke_key(VK_RBUTTON, true); struct ke_frame f = ke_frame(150, 140, 16);
	KE_CHECK(kc_count(&f, 10, KE_A) == 1, "right press: A's left slot cancelled %d", kc_count(&f, 10, KE_A));
	ke_frame(150, 140, 16);
	ci_set_active(li_scene); ke_read();
	ke_frame(150, 140, 16);
	PE_BeginInput(); f = ke_read();
	KE_CHECK(kc_count(&f, 20, KE_A) == 1 && parts_get(KE_A)->is_hovered,
		"the scene's Begin: A LostFocus %d entered %d, want 1, 1 (in no slot)", kc_count(&f, 20, KE_A),
		parts_get(KE_A)->is_hovered);
	ke_frame(150, 140, 16);
	ke_key(VK_LBUTTON, false); ke_key(VK_RBUTTON, false); ke_frame(150, 140, 16);
	ke_frame(150, 140, 16);
	ci_set_active(box); ke_read();
	f = ke_frame(150, 140, 16);
	KE_EXPECT(&f, {VK_LBUTTON, 0, 0});
	ke_frame(150, 140, 16);
	ke_key(VK_LBUTTON, true); f = ke_frame(150, 140, 16);
	KE_CHECK(he_hll_int("GetActiveParts") == KE_A && kc_count(&f, 19, KE_A) == 1,
		"left press: focus %d Focus %d", he_hll_int("GetActiveParts"), kc_count(&f, 19, KE_A));
	KE_EXPECT(&f, {VK_LBUTTON, KE_START, KE_DOWN}, {VK_RBUTTON, 0, KE_UP | KE_CLICK});
	ke_key(VK_LBUTTON, false); ke_frame(150, 140, 16);
}
/* KE109: a right press cancels A's left slot in the box (A stays the box's
 * focus in no slot, both buttons in its table, KE108), both held over the
 * switch to the scene's layer and released there, then a press on B
 * there: B's press leaves the box's focus (0x546a4e), so A gets no
 * LostFocus, and the box's first update reads A's own table: the late left
 * KeyUp and Click, with GetClickNumber, and the late right KeyUp and Click.
 * A right press on A then has the right button's events alone.
 * Expectations are the port's; inferred native: the box's manager, which
 * sampled both buttons down, also sends their whole-screen KeyUp and Click
 * again at its first update. */
static void kl_kept_cancelled_moved(void)
{
	li_setup(); key_clear_flag(false); keyboard_focus = true;
	li_button(KE_B, 100, 100, 200, 80, 7); PE_BeginInput();
	ke_frame(150, 140, 0); ke_frame(150, 140, 0);
	int box = ci_add(-1);
	li_button(KE_A, 100, 100, 200, 80, 3);
	PE_BeginInput(); ke_read();
	ke_frame(150, 140, 16);
	ke_key(VK_LBUTTON, true); ke_frame(150, 140, 16);
	ke_key(VK_RBUTTON, true); struct ke_frame f = ke_frame(150, 140, 16);
	KE_CHECK(kc_count(&f, 10, KE_A) == 1, "right press: A's left slot cancelled %d", kc_count(&f, 10, KE_A));
	ke_frame(150, 140, 16);
	ci_set_active(li_scene); ke_read();
	ke_frame(150, 140, 16);
	ke_key(VK_LBUTTON, false); ke_key(VK_RBUTTON, false); f = ke_frame(150, 140, 16);
	KE_CHECK(!ke_count(&f, 18, KE_A, -1) && !ke_count(&f, 4, KE_A, -1), "scene release: the box's focus released");
	ke_frame(150, 140, 16);
	ke_key(VK_LBUTTON, true); f = ke_frame(150, 140, 16);
	KE_CHECK(he_hll_int("GetActiveParts") == KE_B && !kc_count(&f, 20, KE_A),
		"scene press: focus %d, A LostFocus %d", he_hll_int("GetActiveParts"), kc_count(&f, 20, KE_A));
	ke_key(VK_LBUTTON, false); ke_frame(150, 140, 16);
	ke_frame(150, 140, 16);
	ci_set_active(box); ke_read();
	f = ke_frame(150, 140, 16);
	KE_EXPECT(&f, {VK_LBUTTON, 0, KE_UP | KE_CLICK}, {VK_RBUTTON, 0, KE_UP | KE_CLICK});
	KE_CHECK(he_hll_int("GetClickNumber") == KE_A && he_hll_int("GetActiveParts") == KE_A,
		"back: click number %d focus %d", he_hll_int("GetClickNumber"), he_hll_int("GetActiveParts"));
	f = ke_frame(150, 140, 16);
	KE_EXPECT(&f, {VK_LBUTTON, 0, 0});
	ke_key(VK_RBUTTON, true); f = ke_frame(150, 140, 16);
	KE_EXPECT(&f, {VK_RBUTTON, KE_START, KE_START});
	KE_CHECK(he_hll_int("GetClickNumber") == 0, "right press: click number %d", he_hll_int("GetClickNumber"));
	ke_key(VK_RBUTTON, false); ke_frame(150, 140, 16);
}
/* KE113: A clicked in the box (the box's focus, in no slot), the switch
 * to the scene's layer and a left press held on B there (the scene's
 * focus and left slot): the press does not take A's focus (0x545c70), and
 * a BeginInput with the button still held resets B silently (it is in the
 * scene's left slot, 0x577af0) while A, a focus outside every slot, gets
 * its LostFocus (0x53c750) and stays entered. Expectations are the
 * port's; inferred native: the scene's manager also holds the left button
 * in its own whole-screen table (two whole-screen KeyUps at the
 * BeginInput). */
static void kl_kept_slot_and_focus_begin(void)
{
	li_setup(); key_clear_flag(false); keyboard_focus = true;
	li_button(KE_B, 100, 100, 200, 80, 7); PE_BeginInput();
	ke_frame(640, 600, 0); ke_frame(640, 600, 0);
	int box = ci_add(-1);
	li_button(KE_A, 100, 100, 200, 80, 3);
	PE_BeginInput(); ke_read();
	ke_frame(150, 140, 16);
	ke_focus_a();
	ke_frame(150, 140, 16);
	ci_set_active(li_scene); ke_read();
	ke_frame(150, 140, 16);
	ke_key(VK_LBUTTON, true); struct ke_frame f = ke_frame(150, 140, 16);
	KE_CHECK(kc_count(&f, 19, KE_B) == 1 && !kc_count(&f, 20, KE_A), "scene press: Focus B %d, LostFocus A %d",
		kc_count(&f, 19, KE_B), kc_count(&f, 20, KE_A));
	ke_frame(150, 140, 16);
	PE_BeginInput(); f = ke_read();
	KE_CHECK(kc_count(&f, 20, KE_A) == 1 && !kc_count(&f, 20, KE_B),
		"Begin: LostFocus A %d (outside the slots), B %d (in the scene's slot)", kc_count(&f, 20, KE_A),
		kc_count(&f, 20, KE_B));
	KE_CHECK(parts_get(KE_A)->is_hovered && !parts_get(KE_B)->is_hovered && parts_get(KE_B)->hover_time == 0,
		"Begin: A entered %d, B entered %d time %d", parts_get(KE_A)->is_hovered, parts_get(KE_B)->is_hovered,
		parts_get(KE_B)->hover_time);
	ke_key(VK_LBUTTON, false); ke_frame(150, 140, 16);
	ci_set_active(box); ke_read();
	KE_CHECK(he_hll_int("GetActiveParts") == 0, "back after the Begin: GetActiveParts %d", he_hll_int("GetActiveParts"));
}
/* KE114: back with the left button still held (KE80) and no position at
 * the box's first update: the box's left slot holds A, its focus, so that
 * update leaves A (0x548339 -> 0x5476b0 -> 0x577cf0: MouseLeave, the
 * default state); the next update with a position enters A anew, in its
 * down state, and the release is an ordinary KeyUp and Click. Inferred. */
static void kl_kept_return_held_no_pos(void)
{
	int box = kl_layer_return_setup();
	struct parts *a = parts_get(KE_A);
	ci_set_active(box); ke_read();
	mouse_focus = false; struct ke_frame f = ke_frame(150, 140, 16);
	KE_CHECK(kc_count(&f, 2, KE_A) == 1 && !a->is_hovered && a->state == PARTS_STATE_DEFAULT,
		"back, held, no position: A MouseLeave %d entered %d state %d", kc_count(&f, 2, KE_A), a->is_hovered,
		a->state);
	mouse_focus = true; f = ke_frame(150, 140, 16);
	KE_CHECK(kc_count(&f, 0, KE_A) == 1 && a->is_hovered && a->state == PARTS_STATE_CLICKED,
		"held, the position again: A MouseEnter %d entered %d state %d", kc_count(&f, 0, KE_A), a->is_hovered,
		a->state);
	ke_key(VK_LBUTTON, false); f = ke_frame(150, 140, 16);
	KE_EXPECT(&f, {VK_LBUTTON, KE_UP | KE_CLICK, KE_UP | KE_CLICK});
	KE_CHECK(he_hll_int("GetClickNumber") == KE_A, "release: click number %d", he_hll_int("GetClickNumber"));
}
/* KE115-KE119: a mouse button pressed in another layer and held over the
 * return to a layer whose kept focus A has it up in its own table (A was
 * clicked and released there), with no BeginInput between: SetActiveLayer
 * from the scene's layer back to the box's (KE115-KE117), or the box's
 * layer, added inside the scope over the scene's focus A, removed while the
 * button is held on its own button (KE118, KE119; 0x53d500 leaves the
 * layer below to act and does not begin its manager). The
 * returning layer's manager has no slot for the press and the shared
 * sampled buttons see no new press (no whole-screen Trigger, no slot, the
 * focus not chosen again), but A's own table reads the button down at that
 * layer's first update (0x578580 -> 0x574d50): A gets its Trigger and Down,
 * and when entered its first Press and, for the left button, its pressed
 * look (0x578650); the release sends A's KeyUp and, when it is entered with
 * a position, its MouseClick (0x5788d0; GetClickNumber for the left
 * button). Expectations are the port's. Inferred native: that manager
 * sampled the button up when it last ran, so the held button is a new press
 * for it (0x547f2d) on the element under the cursor (0x546890). With the
 * cursor on A (KE115, KE117, KE118) that gives the same element's messages
 * (A is already the focus: no Focus, 0x546a4e), with a whole-screen Trigger
 * and a slot filled (a drag or cancellation source), which the port does
 * not have. With the cursor away from A, on no element of that layer
 * (KE116, KE119: it leaves two updates before the switch back, while the
 * other layer acts, so the switch back is the only change at that update),
 * it is a press on nothing: A presumably gets its LostFocus at that update
 * and GetActiveParts reads 0, where the port keeps A the focus, sends it
 * the Trigger, Down and KeyUp (no Click) and its LostFocus at the next
 * BeginInput. Not compared with the original running. */
static void kl_kept_focus_held_return(int key, bool on)
{
	struct parts *a = parts_get(KE_A);
	int x = on ? 150 : 500, y = on ? 140 : 400;
	struct ke_frame f = ke_frame(x, y, 16);
	if (on) {
		KE_EXPECT(&f, {key, KE_DOWN, KE_START});
		KE_CHECK(a->is_hovered && a->state == (key == VK_LBUTTON ? PARTS_STATE_CLICKED : PARTS_STATE_HOVERED),
			"back, %d held on A: entered %d state %d", key, a->is_hovered, a->state);
	} else {
		KE_EXPECT(&f, {key, KE_DOWN, KE_TRIGGER | KE_DOWN});
		KE_CHECK(kc_count(&f, 2, KE_A) == 1 && !a->is_hovered && a->state == PARTS_STATE_DEFAULT,
			"back, %d held away from A: MouseLeave %d entered %d state %d", key, kc_count(&f, 2, KE_A),
			a->is_hovered, a->state);
	}
	KE_CHECK(!kc_count(&f, 19, KE_A) && !kc_count(&f, 20, KE_A) && he_hll_int("GetActiveParts") == KE_A,
		"back, %d held: A Focus %d LostFocus %d, focus %d", key, kc_count(&f, 19, KE_A), kc_count(&f, 20, KE_A),
		he_hll_int("GetActiveParts"));
	f = ke_frame(x, y, 16);
	KE_EXPECT(&f, {key, KE_DOWN, KE_DOWN});
	ke_key(key, false); f = ke_frame(x, y, 16);
	KE_EXPECT(&f, {key, KE_UP | KE_CLICK, on ? KE_UP | KE_CLICK : KE_UP});
	int clicked = on && key == VK_LBUTTON ? KE_A : 0;
	KE_CHECK(he_hll_int("GetClickNumber") == clicked && he_hll_int("GetActiveParts") == KE_A,
		"release %d: click number %d, want %d, focus %d", key, he_hll_int("GetClickNumber"), clicked,
		he_hll_int("GetActiveParts"));
	ke_frame(x, y, 16);
	PE_BeginInput(); f = ke_read();
	KE_CHECK(kc_count(&f, 20, KE_A) == 1 && he_hll_int("GetActiveParts") == 0,
		"Begin: A LostFocus %d, focus %d", kc_count(&f, 20, KE_A), he_hll_int("GetActiveParts"));
}
/* KE115 (left, on A), KE116 (left, away), KE117 (right, on A): A is the
 * box's focus, the switch to the scene's layer and a press on B there,
 * which leaves A's focus (0x545c70), held over the switch back (away: the
 * cursor moves off A and B in the scene's layer, an update before its
 * last). */
static void kl_kept_focus_held_back(int key, bool on)
{
	li_setup(); key_clear_flag(false); keyboard_focus = true;
	li_button(KE_B, 100, 100, 200, 80, 7); PE_BeginInput();
	ke_frame(640, 600, 0); ke_frame(640, 600, 0);
	int box = ci_add(-1);
	li_button(KE_A, 100, 100, 200, 80, 3);
	KE_CHECK(ci_active() == box && box != li_scene, "box layer not active");
	PE_BeginInput(); ke_read();
	ke_frame(150, 140, 16);
	ke_focus_a();
	ke_frame(150, 140, 16);
	ci_set_active(li_scene); ke_read();
	ke_frame(150, 140, 16);
	ke_key(key, true); struct ke_frame f = ke_frame(150, 140, 16);
	KE_CHECK(he_hll_int("GetActiveParts") == KE_B && kc_count(&f, 19, KE_B) == 1 && !kc_count(&f, 20, KE_A),
		"scene press %d: focus %d, B Focus %d, A LostFocus %d", key, he_hll_int("GetActiveParts"),
		kc_count(&f, 19, KE_B), kc_count(&f, 20, KE_A));
	KE_CHECK(ke_count(&f, 15, KE_B, key) == 1 && !ke_count(&f, 15, KE_A, -1),
		"scene press %d: B Trigger %d, A Trigger %d", key, ke_count(&f, 15, KE_B, key), ke_count(&f, 15, KE_A, -1));
	ke_frame(150, 140, 16);
	if (!on) {
		ke_frame(500, 400, 16);
		ke_frame(500, 400, 16);
	}
	ci_set_active(box); f = ke_read();
	KE_CHECK(ci_active() == box && f.n == 0 && he_hll_int("GetActiveParts") == KE_A,
		"back, before the box's update: %d queued, focus %d", f.n, he_hll_int("GetActiveParts"));
	kl_kept_focus_held_return(key, on);
}
static void kl_kept_focus_held_back_left(void) { kl_kept_focus_held_back(VK_LBUTTON, true); }
static void kl_kept_focus_held_back_away(void) { kl_kept_focus_held_back(VK_LBUTTON, false); }
static void kl_kept_focus_held_back_right(void) { kl_kept_focus_held_back(VK_RBUTTON, true); }
/* KE118 (on A), KE119 (away): A is the scene's focus, the box's layer
 * added inside the scope over it and a left press held on the box's B,
 * which leaves A's focus, then the box's layer erased with the button
 * held (away: the cursor moves off A and B in the box's layer, an update
 * before its last). */
static void kl_kept_erased_layer_held(bool on)
{
	li_setup(); key_clear_flag(false); keyboard_focus = true;
	li_button(KE_A, 100, 100, 200, 80, 3); PE_BeginInput();
	ke_frame(640, 600, 0); ke_frame(640, 600, 0);
	ke_frame(150, 140, 16);
	ke_focus_a();
	KE_CHECK(he_hll_int("GetActiveParts") == KE_A, "the scene's click: focus %d", he_hll_int("GetActiveParts"));
	ke_frame(150, 140, 16);
	int box = ci_add(-1);
	li_button(KE_B, 100, 100, 200, 80, 7);
	KE_CHECK(ci_active() == box && box != li_scene, "box layer not active");
	ke_read(); ke_frame(150, 140, 16);
	ke_key(VK_LBUTTON, true); struct ke_frame f = ke_frame(150, 140, 16);
	KE_CHECK(he_hll_int("GetActiveParts") == KE_B && kc_count(&f, 19, KE_B) == 1 && !kc_count(&f, 20, KE_A),
		"box press: focus %d, B Focus %d, A LostFocus %d", he_hll_int("GetActiveParts"), kc_count(&f, 19, KE_B),
		kc_count(&f, 20, KE_A));
	KE_CHECK(ke_count(&f, 15, KE_B, VK_LBUTTON) == 1 && !ke_count(&f, 15, KE_A, -1),
		"box press: B Trigger %d, A Trigger %d", ke_count(&f, 15, KE_B, VK_LBUTTON), ke_count(&f, 15, KE_A, -1));
	ke_frame(150, 140, 16);
	if (!on) {
		ke_frame(500, 400, 16);
		ke_frame(500, 400, 16);
	}
	ci_erase(box); f = ke_read();
	KE_CHECK(ci_active() == li_scene && he_hll_int("GetActiveParts") == KE_A,
		"box layer erased: active %d (scene %d), focus %d", ci_active(), li_scene, he_hll_int("GetActiveParts"));
	kl_kept_focus_held_return(VK_LBUTTON, on);
}
static void kl_kept_erased_layer_held_on(void) { kl_kept_erased_layer_held(true); }
static void kl_kept_erased_layer_held_away(void) { kl_kept_erased_layer_held(false); }
/* KE120 (guard): A clicked and released in the scene's layer (its focus,
 * in no slot), the outermost EndInput, the box's layer added outside the
 * scope and an update there, also outside it: the switch keeps A as the
 * scene's focus, and the next BeginInput sends it one LostFocus. The
 * outermost EndInput (0x58a750 at depth 1) runs 0x53e8b0 alone, which
 * resets only the elements in the slots (0x5461a0 -> 0x577af0), and an
 * update outside a scope reads nothing (0x547cba), so A keeps its +0x1b4
 * until the BeginInput's 0x53e830 -> 0x53c750 sends it LostFocus
 * (0x564cc0). Inferred. */
static void kl_kept_outside_switch(void)
{
	li_setup(); key_clear_flag(false); keyboard_focus = true;
	li_button(KE_A, 100, 100, 200, 80, 3); PE_BeginInput();
	ke_frame(640, 600, 0); ke_frame(640, 600, 0);
	ke_frame(150, 140, 16);
	ke_focus_a();
	ke_frame(150, 140, 16);
	PE_EndInput(); struct ke_frame f = ke_read();
	KE_CHECK(!kc_count(&f, 20, KE_A), "outermost End: A LostFocus %d", kc_count(&f, 20, KE_A));
	ke_frame(150, 140, 16);
	int box = ci_add(-1);
	li_button(KE_B, 400, 100, 200, 80, 7);
	KE_CHECK(ci_active() == box && box != li_scene, "box layer not active");
	ke_read();
	f = ke_frame(150, 140, 16);
	KE_CHECK(!kc_count(&f, 19, KE_A) && !kc_count(&f, 20, KE_A), "outside the scope, the switch: A Focus %d LostFocus %d",
		kc_count(&f, 19, KE_A), kc_count(&f, 20, KE_A));
	f = ke_frame(150, 140, 16);
	PE_BeginInput(); f = ke_read();
	KE_CHECK(kc_count(&f, 20, KE_A) == 1 && !kc_count(&f, 20, KE_B), "Begin: LostFocus A %d B %d, want 1, 0",
		kc_count(&f, 20, KE_A), kc_count(&f, 20, KE_B));
	f = ke_frame(150, 140, 16);
	KE_CHECK(he_hll_int("GetActiveParts") == 0 && !kc_count(&f, 20, KE_A), "after the Begin: focus %d, A LostFocus %d",
		he_hll_int("GetActiveParts"), kc_count(&f, 20, KE_A));
}
/* KE121: the scene's A and the box's B each the focus of its own layer
 * (the box's press leaves A, 0x545c70), the scene's layer active again,
 * then SetActiveLayer to the box's and a BeginInput with no update
 * between, so the last update was the scene's. 0x57ba47 writes the
 * designation alone and 0x53e830 walks the stack from its bottom whatever
 * is designated: the BeginInput sends A (the scene's, below) its
 * LostFocus first, then B. Inferred. */
static void kl_kept_begin_after_designation(void)
{
	li_setup(); key_clear_flag(false); keyboard_focus = true;
	li_button(KE_A, 100, 100, 200, 80, 3); PE_BeginInput();
	ke_frame(640, 600, 0); ke_frame(640, 600, 0);
	ke_frame(150, 140, 16);
	ke_focus_a();
	ke_frame(150, 140, 16);
	ke_frame(450, 140, 16);
	int box = ci_add(-1);
	li_button(KE_B, 400, 100, 200, 80, 7);
	KE_CHECK(ci_active() == box && box != li_scene, "box layer not active");
	ke_read(); ke_frame(450, 140, 16);
	ke_key(VK_LBUTTON, true); struct ke_frame f = ke_frame(450, 140, 16);
	KE_CHECK(he_hll_int("GetActiveParts") == KE_B && kc_count(&f, 19, KE_B) == 1 && !kc_count(&f, 20, KE_A),
		"box press: focus %d, B Focus %d, A LostFocus %d", he_hll_int("GetActiveParts"), kc_count(&f, 19, KE_B),
		kc_count(&f, 20, KE_A));
	ke_key(VK_LBUTTON, false); ke_frame(450, 140, 16);
	ke_frame(450, 140, 16);
	ci_set_active(li_scene); ke_read();
	f = ke_frame(450, 140, 16);
	KE_CHECK(he_hll_int("GetActiveParts") == KE_A && !kc_count(&f, 20, KE_A) && !kc_count(&f, 20, KE_B),
		"the scene again: focus %d, LostFocus A %d B %d", he_hll_int("GetActiveParts"), kc_count(&f, 20, KE_A),
		kc_count(&f, 20, KE_B));
	ke_frame(450, 140, 16);
	ci_set_active(box); f = ke_read();
	KE_CHECK(ci_active() == box && f.n == 0 && he_hll_int("GetActiveParts") == KE_B,
		"SetActiveLayer to the box: %d queued, focus %d", f.n, he_hll_int("GetActiveParts"));
	PE_BeginInput(); f = ke_read();
	int ia = -1, ib = -1;
	for (int i = 0; i < f.n && i < KE_CAP; i++) {
		if (f.m[i].type != 20) continue;
		if (f.m[i].no == KE_A) ia = i;
		if (f.m[i].no == KE_B) ib = i;
	}
	KE_CHECK(kc_count(&f, 20, KE_A) == 1 && kc_count(&f, 20, KE_B) == 1 && ia < ib,
		"Begin: LostFocus A %d at %d, B %d at %d (the bottom layer first)", kc_count(&f, 20, KE_A), ia,
		kc_count(&f, 20, KE_B), ib);
}
/* KE122 (guard): the scene's focus A moved under the box's B
 * (SetParentPartsNumber) while the scene's layer acts: after the scene's
 * next update GetActiveParts is 0, as 0x53c240 then finds no parts with
 * the focus in the scene's own tree (0x538180 from its root), and A gets
 * no message until the next BeginInput sends it one LostFocus (0x53afb0
 * only moves the parts; 0x53e830 -> 0x53c750 -> 0x564cc0, in the box's
 * tree). GetActiveParts is not read between the move and that update,
 * where the original's +0x280 still has A. Inferred. */
static void kl_kept_moved_focus(void)
{
	li_setup(); key_clear_flag(false); keyboard_focus = true;
	li_button(KE_A, 100, 100, 200, 80, 3); PE_BeginInput();
	ke_frame(640, 600, 0); ke_frame(640, 600, 0);
	ke_frame(150, 140, 16);
	ke_focus_a();
	ke_frame(150, 140, 16);
	int box = ci_add(-1);
	li_button(KE_B, 400, 100, 200, 80, 7);
	KE_CHECK(ci_active() == box && box != li_scene, "box layer not active");
	ci_set_active(li_scene); ke_read();
	struct ke_frame f = ke_frame(640, 600, 16);
	KE_CHECK(he_hll_int("GetActiveParts") == KE_A, "the scene: focus %d", he_hll_int("GetActiveParts"));
	f = ke_frame(640, 600, 16);
	PE_SetParentPartsNumber(KE_A, KE_B);
	KE_CHECK(parts_get(KE_A)->controller_no == box, "A moved to %d, want the box's %d", parts_get(KE_A)->controller_no,
		box);
	for (int i = 0; i < 2; i++) {
		f = ke_frame(640, 600, 16);
		KE_CHECK(he_hll_int("GetActiveParts") == 0 && !kc_count(&f, 19, KE_A) && !kc_count(&f, 20, KE_A),
			"moved, the scene's update %d: focus %d, A Focus %d LostFocus %d", i + 1,
			he_hll_int("GetActiveParts"), kc_count(&f, 19, KE_A), kc_count(&f, 20, KE_A));
	}
	PE_BeginInput(); f = ke_read();
	KE_CHECK(kc_count(&f, 20, KE_A) == 1, "Begin: A LostFocus %d, want 1", kc_count(&f, 20, KE_A));
}
/* KE123: KE96 for the middle slot: a middle press on A in the box held
 * over the switch, a left press in the scene's layer, and the switch back
 * with both held. The box's manager sampled the left button up when it
 * last ran, so it is a new press there (0x547f2d), which cancels the
 * middle slot (+0x200, cancelled by a left or right press: 0x548179..
 * 0x548191 -> 0x5463c0): DragBegin (the drag had not begun), Draging back
 * at the press point and DragEnd mode 2. Expectations are the port's;
 * inferred native, as for KE96: the left button also starts in the box's
 * whole-screen table. */
static void kl_kept_return_cancel_middle(void)
{
	li_setup(); key_clear_flag(false); keyboard_focus = true;
	li_button(KE_B, 400, 100, 200, 80, 7); PE_BeginInput();
	ke_frame(150, 140, 0); ke_frame(150, 140, 0);
	int box = ci_add(-1);
	li_button(KE_A, 100, 100, 200, 80, 3);
	PE_BeginInput(); ke_read();
	/* Two updates before the press, as in KE96: the middle drag (no
	 * threshold) has not begun when it is cancelled. */
	ke_frame(150, 140, 16); ke_frame(150, 140, 16);
	ke_key(VK_MBUTTON, true); struct ke_frame f = ke_frame(150, 140, 16);
	KE_EXPECT(&f, {VK_MBUTTON, KE_START, KE_START});
	f = ke_frame(150, 140, 16);
	ci_set_active(li_scene); ke_read();
	/* Expectations are the port's; inferred native: a new press there. */
	f = ke_frame(150, 140, 16);
	KE_EXPECT(&f, {VK_MBUTTON, KE_DOWN, 0});
	ke_key(VK_LBUTTON, true); f = ke_frame(150, 140, 16);
	KE_EXPECT(&f, {VK_LBUTTON, KE_START, 0}, {VK_MBUTTON, KE_DOWN, 0});
	KE_CHECK(!kc_count(&f, 10, KE_A) && !kc_count(&f, 20, KE_A), "scene left press: A DragEnd %d LostFocus %d",
		kc_count(&f, 10, KE_A), kc_count(&f, 20, KE_A));
	f = ke_frame(150, 140, 16);
	ci_set_active(box); f = ke_read();
	KE_CHECK(f.n == 0, "SetActiveLayer queued %d", f.n);
	f = ke_frame(150, 140, 16);
	kc_cancel_events(&f, VK_MBUTTON, 2, false);
	KE_CHECK(he_hll_int("GetActiveParts") == KE_A, "back: focus %d", he_hll_int("GetActiveParts"));
	KE_CHECK(!kc_count(&f, 19, KE_A) && !kc_count(&f, 20, KE_A), "back: focus messages");
	KE_EXPECT(&f, {VK_LBUTTON, KE_DOWN, KE_START}, {VK_MBUTTON, KE_DOWN, KE_DOWN});
	f = ke_frame(150, 140, 16);
	kc_no_drag_events(&f);
	ke_key(VK_MBUTTON, false); ke_key(VK_LBUTTON, false); f = ke_frame(150, 140, 16);
	kc_no_drag_events(&f);
}
