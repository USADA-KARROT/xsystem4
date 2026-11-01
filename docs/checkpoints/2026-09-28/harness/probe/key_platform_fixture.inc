/* SDL policy checks, not proof of identical OS activation behavior.
 * Native 0x574ded reads generic GetKeyState; 0x4bdaf0 gates activation
 * against minimization and clears keyboard state on activation. */
static void kp_modifiers(void)
{
	unsetenv("XSYS4_HOLD_KEYS");
	ke_setup(); KE_CHECK(SDL_Init(SDL_INIT_EVENTS) == 0, "SDL events init");
	const SDL_Scancode scans[][2] = {
		{SDL_SCANCODE_LSHIFT, SDL_SCANCODE_RSHIFT},
		{SDL_SCANCODE_LCTRL, SDL_SCANCODE_RCTRL},
		{SDL_SCANCODE_RETURN, SDL_SCANCODE_KP_ENTER}
	};
	const int keys[] = {VK_SHIFT, VK_CONTROL, VK_RETURN};
	for (int k = 0; k < 3; k++) {
		for (int first = 0; first < 2; first++) {
			kl_sdl_key(scans[k][first], true);
			kl_sdl_key(scans[k][1-first], true);
			kl_sdl_key(scans[k][first], false);
			KE_CHECK(key_is_down(keys[k]), "key %d lost other held source", keys[k]);
			kl_sdl_key(scans[k][1-first], false);
			KE_CHECK(!key_is_down(keys[k]), "key %d remained down", keys[k]);
		}
	}
	kl_sdl_key(SDL_SCANCODE_LCTRL, true);
	kl_sdl_key(SDL_SCANCODE_RCTRL, true);
	kl_sdl_key(SDL_SCANCODE_LSHIFT, true);
	key_clear_flag(true);
	KE_CHECK(key_is_down(VK_CONTROL) && !key_is_down(VK_SHIFT), "no_ctrl clear changed preserved control");
	kl_sdl_key(SDL_SCANCODE_LCTRL, false);
	KE_CHECK(key_is_down(VK_CONTROL), "no_ctrl clear discarded right control source");
	key_clear_flag(false);
	kl_sdl_key(SDL_SCANCODE_LCTRL, false);
	KE_CHECK(!key_is_down(VK_CONTROL), "clear resurrected stale right control");
	/* Keep the earlier last-event mapping, including its overlap limit. */
	ain->version = 13;
	kl_sdl_key(SDL_SCANCODE_LSHIFT, true);
	kl_sdl_key(SDL_SCANCODE_RSHIFT, true);
	kl_sdl_key(SDL_SCANCODE_LSHIFT, false);
	KE_CHECK(!key_is_down(VK_SHIFT), "v13 modifier mapping changed");
}
static void kp_window(Uint8 event)
{
	au_window_event(event); au_pump();
}
static void kp_activation(void)
{
	unsetenv("XSYS4_HOLD_KEYS");
	ke_setup(); KE_CHECK(SDL_Init(SDL_INIT_EVENTS) == 0, "SDL events init");
	kl_sdl_key(SDL_SCANCODE_A, true);
	struct ke_frame f = ke_frame(150, 140, 16);
	KE_CHECK(ke_count(&f, 15, 0, 'A') == 1, "initial key missing");
	kp_window(SDL_WINDOWEVENT_MINIMIZED);
	f = ke_frame(150, 140, 16);
	KE_CHECK(ke_count(&f, 18, 0, 'A') == 1 && !ke_count(&f, 16, 0, 'A'), "minimize did not release sampled key");
	kp_window(SDL_WINDOWEVENT_FOCUS_GAINED);
	kl_sdl_key(SDL_SCANCODE_A, true);
	f = ke_frame(150, 140, 16);
	KE_CHECK(!ke_count(&f, 15, 0, 'A') && !ke_count(&f, 16, 0, 'A'), "focus gain bypassed minimized gate");
	kp_window(SDL_WINDOWEVENT_RESTORED);
	f = ke_frame(150, 140, 16);
	KE_CHECK(!ke_count(&f, 15, 0, 'A'), "restore retained stale key");
	kl_sdl_key(SDL_SCANCODE_A, true);
	f = ke_frame(150, 140, 16);
	KE_CHECK(ke_count(&f, 15, 0, 'A') == 1, "restored fresh key missing");
	kp_window(SDL_WINDOWEVENT_FOCUS_LOST);
	f = ke_frame(150, 140, 16);
	KE_CHECK(ke_count(&f, 18, 0, 'A') == 1, "focus loss did not release sampled key");
	kp_window(SDL_WINDOWEVENT_RESTORED);
	f = ke_frame(150, 140, 16);
	KE_CHECK(!ke_count(&f, 15, 0, 'A'), "restore granted focus");
	kp_window(SDL_WINDOWEVENT_FOCUS_GAINED);
	f = ke_frame(150, 140, 16);
	KE_CHECK(!ke_count(&f, 15, 0, 'A') && !key_is_down('A'), "activation did not clear stale key");
	ain->version = 13; ke_key('A', true);
	kp_window(SDL_WINDOWEVENT_FOCUS_LOST); kp_window(SDL_WINDOWEVENT_FOCUS_GAINED);
	KE_CHECK(key_is_down('A'), "v13 activation clearing changed");
}
