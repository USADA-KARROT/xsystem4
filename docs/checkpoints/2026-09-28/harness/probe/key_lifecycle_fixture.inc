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
 * The port's one-manager policy must not carry a cancelled source's
 * input suppression into a newly active layer before BeginInput. */
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
 * (kl_layer_return_held). These cases pin the port's current behavior:
 * the scope switch empties the slots only (v14_drop_slots_out_of_scope);
 * the focus and its own key table stay, untouched while the scene's layer
 * acts. So the focus gets its KeyUp and MouseClick when its layer acts
 * again: late, at the first update after the switch back, with no second
 * whole-screen Click (the sampled buttons are shared), or at the release.
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
 * whole-screen KeyUp and Click again when it acts again. Not compared
 * with the original running. */
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
