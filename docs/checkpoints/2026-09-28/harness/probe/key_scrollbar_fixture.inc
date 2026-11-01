/* Synthetic track and knob recipients, native 0x572ba0 dispatcher:
 * 15/16/17/18 are forwarded before internal work; an internal Click, if
 * generated, is forwarded too. Native 0x574648 makes the track clickable;
 * the ordinary knob keeps the default false (0x550a2f). Children inherit
 * pass_cursor, not clickable (0x573baa). The knob is walked before the
 * track (0x4f3370, 0x5471d0), so an overlapping knob blocks the track only
 * when pass_cursor is false. Release requires that focused inner element
 * to remain entered (0x5788d0). R/M do not move the knob.
 * New local Click checks preserve key/payload/order without asserting the
 * native queue's unresolved numeric parts/delegate/unique mapping.
 * Not covered: KS1's left press/release geometry timing, inner keyboard
 * focus changes, external listeners, drop-hover and swipe exceptions.
 * The existing scrollbar-knob mode guards geometry independently. */
static int ks_bar(bool empty)
{
	sk_setup(); he_env(); key_clear_flag(false); keyboard_focus = true;
	struct sk_bar desc = sk_hbar;
	if (empty) { desc.total = 1; desc.view = 1; desc.pos = 0; desc.rate = 0; }
	const char *names[] = {"SyntheticSlider"}; const struct sk_bar *bars[] = {&desc}; int no[1] = {0};
	KE_CHECK(sk_load("SyntheticKeySlider", 1, names, bars, no), "synthetic bar load");
	if (!no[0]) return 0;
	cp_register_event(no[0], 7, 70); cw_set_rate(no[0], 0); cw_bars_update(); ke_read();
	PE_BeginInput(); mouse_set_pos(600, 600); PE_Update(16, true); ke_read();
	PE_Update(16, true); ke_read();
	return no[0];
}
static struct ke_frame ks_frame(int x, int y)
{
	mouse_set_pos(x, y); PE_Update(16, true); return ke_read();
}
static void ks_local_clicks(const struct ke_frame *f, int key, int x, int y, int expected)
{
	int clicks = 0, click_index = -1, up_index = -1;
	for (int i = 0; i < f->n && i < KE_CAP; i++) {
		const struct ke_msg *m = &f->m[i];
		if (!m->no) continue;
		if (m->type == 18 && m->n == 1 && m->v[0] == key)
			up_index = i;
		if (m->type != 4) continue;
		clicks++; click_index = i;
		KE_CHECK(m->n == 3 && m->v[0] == x && m->v[1] == y && m->v[2] == key,
			"local bar Click changed payload for key %d", key);
	}
	KE_CHECK(clicks == expected, "local bar Click count %d, want %d for key %d", clicks, expected, key);
	if (expected)
		KE_CHECK(click_index >= 0 && up_index > click_index, "local bar Click did not precede KeyUp for key %d", key);
}
static void ks_track(void)
{
	int no = ks_bar(false); if (!no) return;
	ke_key(VK_LBUTTON, true); struct ke_frame f = ks_frame(170, 46);
	float rate = 134.0f / 359.0f;
	KE_CHECK(cw_rate(no) == rate, "track press rate");
	for (int t = 15; t <= 17; t++) KE_CHECK(ke_count(&f, t, no, VK_LBUTTON) == 1, "track did not forward type %d", t);
	ke_key(VK_LBUTTON, false); f = ks_frame(170, 46);
	/* The track press moves the knob center to the cursor; the track need
	 * not remain entered on release. Do not infer a local Click from this. */
	KE_CHECK(cw_rate(no) == rate && ke_count(&f, 4, 0, VK_LBUTTON) == 1 && ke_count(&f, 18, no, VK_LBUTTON) == 1, "track release rate/WholeClick/localUp");
}
static void ks_knob(void)
{
	int no = ks_bar(false); if (!no) return;
	ke_key(VK_LBUTTON, true); struct ke_frame f = ks_frame(30, 46);
	KE_CHECK(cw_rate(no) == 0 && ke_count(&f, 15, no, VK_LBUTTON) == 1, "knob initial press");
	f = ks_frame(130, 46);
	KE_CHECK(cw_rate(no) == 100.0f / 359.0f && ke_count(&f, 16, no, VK_LBUTTON) == 1, "knob drag/down");
	ke_key(VK_LBUTTON, false); f = ks_frame(130, 46);
	KE_CHECK(ke_count(&f, 4, 0, VK_LBUTTON) == 1 && ke_count(&f, 18, no, VK_LBUTTON) == 1, "knob release WholeClick/localUp");
}
static void ks_other(void)
{
	int no = ks_bar(false); if (!no) return;
	PE_SetClickable(no, false); PE_SetPassCursor(no, false);
	int keys[] = {VK_RBUTTON, VK_MBUTTON};
	for (int i = 0; i < 2; i++) {
		ke_key(keys[i], true); struct ke_frame f = ks_frame(170, 46);
		KE_CHECK(cw_rate(no) == 0 && ke_count(&f, 15, no, keys[i]) == 1, "other key press changed rate or was swallowed");
		ke_key(keys[i], false); f = ks_frame(170, 46);
		KE_CHECK(cw_rate(no) == 0 && ke_count(&f, 4, 0, keys[i]) == 1 && ke_count(&f, 18, no, keys[i]) == 1, "other release rate/WholeClick/localUp");
		/* At rate zero the knob spans x=20..51; x=170 stays on
		 * the track through both frames because R/M do not move it. */
		ks_local_clicks(&f, keys[i], 170, 46, 1);
		KE_CHECK(!he_hll_int("GetClickNumber"), "other bar click changed click-number");
	}
}
static void ks_empty(void)
{
	int no = ks_bar(true); if (!no) return;
	ke_key(VK_LBUTTON, true); struct ke_frame f = ks_frame(170, 46);
	KE_CHECK(cw_rate(no) == 0 && ke_count(&f, 15, no, VK_LBUTTON) == 1, "empty range lost forwarded trigger");
	f = ks_frame(230, 46); KE_CHECK(cw_rate(no) == 0, "empty range moved");
	ke_key(VK_LBUTTON, false); f = ks_frame(230, 46);
	KE_CHECK(ke_count(&f, 18, no, VK_LBUTTON) == 1, "empty range lost forwarded Up");
}
static void ks_held_left_after_other_press(void)
{
	int no = ks_bar(false); if (!no) return;
	ke_key(VK_LBUTTON, true); ks_frame(30, 46); ks_frame(130, 46);
	ke_key(VK_RBUTTON, true); ks_frame(130, 46);
	struct ke_frame f = ks_frame(146, 46);
	/* No script listener changes the layer. Only the frame after the right
	 * press is checked: held-left Down and its rate update still reach the
	 * bar. The discarded press frame does not prove generic cancellation. */
	KE_CHECK(cw_rate(no) == 116.0f / 359.0f && ke_count(&f, 16, no, VK_LBUTTON) == 1, "held left lost bar KeyDown/rate after right press");
}
static void ks_knob_other(void)
{
	int no = ks_bar(false); if (!no) return;
	PE_SetPassCursor(no, false);
	int keys[] = {VK_RBUTTON, VK_MBUTTON};
	for (int outer_clickable = 0; outer_clickable < 2; outer_clickable++) {
		PE_SetClickable(no, outer_clickable);
		for (int i = 0; i < 2; i++) {
			for (int move_to_track = 0; move_to_track < 2; move_to_track++) {
				ke_key(keys[i], true); struct ke_frame f = ks_frame(30, 46);
				KE_CHECK(ke_count(&f, 15, no, keys[i]) == 1, "knob other key lost trigger");
				int x = move_to_track ? 170 : 30;
				if (move_to_track) ks_frame(x, 46);
				ke_key(keys[i], false); f = ks_frame(x, 46);
				/* Click permission belongs to the focused inner knob,
				 * even after moving over the clickable track. */
				ks_local_clicks(&f, keys[i], x, 46, 0);
				KE_CHECK(cw_rate(no) == 0 && ke_count(&f, 18, no, keys[i]) == 1,
					"knob other release changed rate or lost KeyUp");
			}
		}
	}
}
static void ks_track_over_knob(bool pass)
{
	int no = ks_bar(false); if (!no) return;
	PE_SetClickable(no, false); PE_SetPassCursor(no, pass);
	int keys[] = {VK_RBUTTON, VK_MBUTTON};
	for (int i = 0; i < 2; i++) {
		ke_key(keys[i], true); ks_frame(170, 46);
		ks_frame(30, 46);
		ke_key(keys[i], false); struct ke_frame f = ks_frame(30, 46);
		/* The old focus is still the track. A normal hover walk has
		 * already visited this overlap while the same key was held. */
		ks_local_clicks(&f, keys[i], 30, 46, pass ? 1 : 0);
		KE_CHECK(cw_rate(no) == 0 && ke_count(&f, 18, no, keys[i]) == 1,
			"track overlap release changed rate or lost KeyUp");
		KE_CHECK(!he_hll_int("GetClickNumber"), "track overlap changed click-number");
		ke_key(keys[i], true); ks_frame(170, 46);
		ks_frame(500, 46);
		ke_key(keys[i], false); f = ks_frame(500, 46);
		ks_local_clicks(&f, keys[i], 500, 46, 0);
	}
}
static void ks_track_blocked(void) { ks_track_over_knob(false); }
static void ks_track_passed(void) { ks_track_over_knob(true); }
static int test_key_scrollbar(void)
{
	void (*cases[])(void) = {ks_track, ks_knob, ks_other, ks_empty, ks_held_left_after_other_press,
		ks_knob_other, ks_track_blocked, ks_track_passed};
	int nr_cases = sizeof(cases) / sizeof(*cases);
	int failed = 0, env = 0;
	for (int i = 0; i < nr_cases; i++) {
		char name[16]; snprintf(name, sizeof name, "KS%d", i + 1);
		int fd[2];
		if (pipe(fd)) { env++; continue; }
		fflush(NULL); pid_t pid = fork();
		if (pid < 0) { close(fd[0]); close(fd[1]); env++; continue; }
		if (!pid) {
			close(fd[0]); cn_ready_fd = fd[1]; alarm(60);
			ke_failures = sk_failures = cw_failures = bt_failures = cn_failures = cc_failures = 0;
			cases[i](); fflush(NULL);
			_exit(ke_failures + sk_failures + cw_failures + bt_failures + cn_failures + cc_failures ? 1 : 0);
		}
		close(fd[1]); int status = 0; pid_t got;
		do { got = waitpid(pid, &status, 0); } while (got < 0 && errno == EINTR);
		char ready = 0; ssize_t count;
		do { count = read(fd[0], &ready, 1); } while (count < 0 && errno == EINTR);
		close(fd[0]);
		bool environment = got != pid || count != 1 || ready != 'r' || (WIFEXITED(status) && WEXITSTATUS(status) == CN_ENV_EXIT);
		bool bad = !environment && (!WIFEXITED(status) || WEXITSTATUS(status));
		env += environment; failed += bad;
		printf("%s %s (status %d)\n", name, environment ? "ENVIRONMENT FAILURE" : bad ? "FAIL" : "PASS", status);
	}
	printf("KEY-SCROLLBAR %s: %d of %d cases failed; environment %d\n", env ? "ENVIRONMENT FAILURE" : failed ? "FAIL" : "PASS", failed, nr_cases, env);
	return env ? 2 : failed ? 1 : 0;
}
