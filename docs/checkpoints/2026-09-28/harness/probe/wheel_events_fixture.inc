/* wheel-events: v14 wheel counts and dispatch (G5).
 * Static native evidence: 0x4be830 accumulates signed deltas at +/-120,
 * 0x4c33a0 clears the two counts but keeps the remainder, 0x547c90 sends
 * Whole wheel before hover and 0x547000 sends one local wheel afterwards.
 * Each event has exactly two integers (forward, back). 0x5350c0 checks
 * type/focus, then internal component owner, then the flag set through
 * 0x590770. The owner (+0x7c) is distinct from the visual parent (+0x13c);
 * ordinary named parts have no such owner and must not inherit that flag.
 *
 * The SDL fractional conversion and first-Get snapshot are port policies,
 * NOT properties observed in the native executable. A first Get, even an
 * empty one, freezes this cycle; Clear ends it without eating pending input.
 * These cases use only symbols present before G5, real SDL event pumping,
 * production HLL declarations, raw message access and synthetic rectangles.
 * No GL or game assets are needed. Each case is isolated by fork.
 */

static int we_failures;
#define WE_CHECK(c, ...) do { if (!(c)) { printf("  FAIL: "); printf(__VA_ARGS__); printf("\n"); we_failures++; } } while (0)
enum { WE_A = 19401, WE_B, WE_C, WE_D, WE_P, WE_Q };

static void we_init(void)
{
	he_env();
	WE_CHECK(SDL_Init(SDL_INIT_EVENTS) == 0, "SDL events init: %s", SDL_GetError());
	br_input_setup();
	input_modal_closed();
	SDL_FlushEvents(SDL_FIRSTEVENT, SDL_LASTEVENT);
}

static void we_push(int y, float precise, Uint32 direction, Uint32 which)
{
	SDL_Event e = { .type = SDL_MOUSEWHEEL };
	e.wheel.y = y;
	e.wheel.direction = direction;
	e.wheel.which = which;
#if SDL_VERSION_ATLEAST(2, 0, 18)
	e.wheel.preciseY = precise;
#else
	(void)precise;
#endif
	WE_CHECK(SDL_PushEvent(&e) == 1, "SDL rejected wheel event");
}

static void we_notch(int y)
{
	we_push(y, (float)y, SDL_MOUSEWHEEL_NORMAL, AU_AUTO_MOUSEID);
}

static void we_pump(void)
{
	SDL_Delay(2);
	handle_events();
}

static void we_counts(const char *label, int want_f, int want_b)
{
	int f = -1, b = -1;
	mouse_get_wheel(&f, &b);
	WE_CHECK(f == want_f && b == want_b, "%s: forward/back %d/%d, want %d/%d", label, f, b, want_f, want_b);
}

static void we_hll_counts(const char *label, int want_f, int want_b)
{
	int pg = heap_alloc_page(alloc_page(ARRAY_PAGE, AIN_ARRAY_INT, 2));
	heap[pg].page->values[0].i = heap[pg].page->values[1].i = -77;
	bu_call("IbisInputEngine", "MouseWheel_GetCount", 2, 0, 4,
		(union vm_value[]){ BU_I(pg), BU_I(0), BU_I(pg), BU_I(1) });
	int f = heap[pg].page->values[0].i, b = heap[pg].page->values[1].i;
	WE_CHECK(f == want_f && b == want_b, "%s: HLL wrap writes %d/%d, want %d/%d", label, f, b, want_f, want_b);
	heap_unref(pg);
}

static void we_hll_clear(void)
{
	bu_call("IbisInputEngine", "MouseWheel_ClearCount", 0, 0, 0, NULL);
}

static void we_set(int no, bool enabled)
{
	BU_PE("Parts_SetWheelable", BU_I(no), BU_I(enabled));
}

static void we_box(int no, int x, int y, int z, bool enabled)
{
	br_box(no, x, y, 160, 80, true);
	PE_SetZ(no, z);
	cp_register_event(no, no - WE_A + 41, no - WE_A + 401);
	we_set(no, enabled);
}

static struct he_frame we_update(int x, int y)
{
	return he_update(x, y, false, 16);
}

static void we_scope(int x, int y)
{
	PE_BeginInput();
	we_update(x, y);
	we_update(x, y);
	/* The settling updates may read an empty snapshot. */
	mouse_clear_wheel();
}

static struct he_frame we_tick(int x, int y, int f, int b)
{
	mouse_clear_wheel();
	if (f) we_notch(f);
	if (b) we_notch(-b);
	we_pump();
	return we_update(x, y);
}

/* Inspect raw messages, never the reader that skips hover. Unknown root
 * fallback mapping is deliberately not assigned a synthetic parts number. */
static void we_target(const char *label, const struct he_frame *frame, int target, int f, int b)
{
	int n = 0, whole = -1, local = -1;
	for (int i = 0; i < frame->n && i < HE_MAXMSG; i++) {
		const struct he_msg *m = &frame->m[i];
		if (m->type != 3) continue;
		n++;
		if (m->no == 0) whole = i;
		if (m->no == target) local = i;
		WE_CHECK(m->n == 2 && m->v[0] == f && m->v[1] == b,
			"%s: wheel parts %d has %d vars [%d,%d], want 2 [%d,%d]", label, m->no, m->n, m->v[0], m->v[1], f, b);
		WE_CHECK(m->delegate == he_delegate_of(m->no) && m->unique == he_unique_of(m->no),
			"%s: wheel parts %d metadata %d/%d", label, m->no, m->delegate, m->unique);
	}
	WE_CHECK(n == 2 && whole >= 0 && local > whole,
		"%s: %d wheel messages, Whole index %d, target %d index %d; want Whole then target", label, n, whole, target, local);
	if (n != 2 || local <= whole) he_print(label, frame);
}

static void we_no_local(const char *label, const struct he_frame *frame, int no)
{
	WE_CHECK(he_count(frame, 3, 0) >= 1, "%s: no Whole wheel", label);
	WE_CHECK(he_count(frame, 3, no) == 0, "%s: ineligible parts %d received wheel", label, no);
}

/* WE1: separate positive and negative counts, not last direction or net. */
static void we_bidirectional(void)
{
	we_init();
	we_notch(3); we_notch(-2); we_notch(0); we_pump();
	we_counts("WE1 +3,-2,0", 3, 2);
	we_counts("WE1 repeated Get", 3, 2);
	mouse_clear_wheel();
	we_counts("WE1 Clear", 0, 0);
	mouse_clear_wheel();
	we_notch(1); we_notch(1); we_notch(-1); we_pump();
	we_counts("WE1 +1,+1,-1", 2, 1);
}

/* WE2: snapshot adaptation, including freezing the first empty read. */
static void we_snapshot(void)
{
	we_init();
	we_notch(2); we_pump(); we_counts("WE2 first", 2, 0);
	we_notch(-1); we_pump(); we_counts("WE2 pump between Gets", 2, 0);
	mouse_clear_wheel(); we_counts("WE2 next cycle", 0, 1);
	input_modal_closed();
	we_notch(1); we_pump(); mouse_clear_wheel(); mouse_clear_wheel();
	we_counts("WE2 Clear before first Get preserves pending", 1, 0);
	input_modal_closed();
	we_counts("WE2b empty first Get", 0, 0);
	we_notch(1); we_pump(); we_counts("WE2b empty snapshot stays empty", 0, 0);
	mouse_clear_wheel(); we_counts("WE2b pending appears after Clear", 1, 0);
}

/* WE3: SDL policies; binary fractions avoid decimal rounding ambiguity. */
static void we_fractional(void)
{
	we_init();
	for (int i = 0; i < 4; i++) {
		we_push(0, .25f, SDL_MOUSEWHEEL_NORMAL, AU_AUTO_MOUSEID); we_pump();
		we_counts("WE3 positive quarters", i == 3, 0); mouse_clear_wheel();
	}
	for (int i = 0; i < 4; i++) {
		we_push(0, -.25f, SDL_MOUSEWHEEL_NORMAL, AU_AUTO_MOUSEID); we_pump();
		we_counts("WE3 negative quarters", 0, i == 3); mouse_clear_wheel();
	}
	we_push(2, 2.5f, SDL_MOUSEWHEEL_NORMAL, AU_AUTO_MOUSEID); we_pump();
	we_counts("WE3 +2.5", 2, 0); mouse_clear_wheel();
	we_push(0, .5f, SDL_MOUSEWHEEL_NORMAL, AU_AUTO_MOUSEID); we_pump();
	we_counts("WE3 remainder survives Clear", 1, 0);
	input_modal_closed();
	we_push(0, .5f, SDL_MOUSEWHEEL_NORMAL, AU_AUTO_MOUSEID);
	we_push(0, -.5f, SDL_MOUSEWHEEL_NORMAL, AU_AUTO_MOUSEID); we_pump();
	we_counts("WE3 opposite halves cancel", 0, 0); mouse_clear_wheel();
	we_push(0, .5f, SDL_MOUSEWHEEL_NORMAL, AU_AUTO_MOUSEID);
	we_push(-1, -1.f, SDL_MOUSEWHEEL_NORMAL, AU_AUTO_MOUSEID); we_pump();
	we_counts("WE3 +half,-one leaves -half", 0, 0); mouse_clear_wheel();
	we_push(0, -.5f, SDL_MOUSEWHEEL_NORMAL, AU_AUTO_MOUSEID); we_pump();
	we_counts("WE3 remaining -half", 0, 1);
	input_modal_closed();
	we_notch(1); we_push(1, 1.f, SDL_MOUSEWHEEL_FLIPPED, AU_AUTO_MOUSEID);
	we_push(0, 0, SDL_MOUSEWHEEL_NORMAL, AU_AUTO_MOUSEID); we_pump();
	we_counts("WE3 normal and flipped, then zero", 1, 1);
	input_modal_closed();
	we_push(2, 0, SDL_MOUSEWHEEL_NORMAL, AU_AUTO_MOUSEID); we_pump();
	we_counts("WE3 y-only producer fallback", 2, 0);
}

/* WE4: exact payload/metadata and Whole -> OnCursor -> local ordering. */
static void we_dispatch(void)
{
	we_init();
	we_box(WE_A, 100, 100, 1, true);
	we_box(WE_B, 100, 100, 2, true);
	we_scope(150, 140);
	struct he_frame f = we_tick(150, 140, 2, 1);
	we_target("WE4 topmost", &f, WE_B, 2, 1);
	int whole = -1, hover = -1, local = -1;
	for (int i = 0; i < f.n && i < HE_MAXMSG; i++) {
		if (f.m[i].type == 3 && !f.m[i].no) whole = i;
		if (f.m[i].type == 6 && f.m[i].no == WE_B) hover = i;
		if (f.m[i].type == 3 && f.m[i].no == WE_B) local = i;
	}
	WE_CHECK(whole >= 0 && hover > whole && local > hover,
		"WE4 order Whole %d, OnCursor %d, local %d", whole, hover, local);
	we_counts("WE4 dispatch does not clear", 2, 1);
	f = we_update(150, 140);
	we_target("WE4 no Clear permits another dispatch", &f, WE_B, 2, 1);
	mouse_clear_wheel(); f = we_update(150, 140);
	WE_CHECK(he_count(&f, 3, -1) == 0, "WE4 cleared frame sends a wheel");
}

/* WE5: wheel candidates ignore hover blockers and use global/default state. */
static void we_filters(void)
{
	we_init();
	we_box(WE_A, 100, 100, 1, true);
	we_box(WE_B, 100, 100, 2, true); PE_SetAlpha(WE_B, 0);
	we_box(WE_C, 100, 100, 3, true); PE_SetShow(WE_C, false);
	we_box(WE_D, 100, 100, 4, false); PE_SetPassCursor(WE_D, false);
	we_scope(150, 140);
	struct he_frame f = we_tick(150, 140, 1, 0);
	we_target("WE5 skip blocker, hidden and transparent", &f, WE_A, 1, 0);
	we_set(WE_D, true); PE_SetPassCursor(WE_D, true);
	f = we_tick(150, 140, 1, 0); we_target("WE5 eligible pass-through wins", &f, WE_D, 1, 0);
	we_set(WE_D, false); PE_SetShow(WE_C, true);
	f = we_tick(150, 140, 1, 0); we_target("WE5 visible front wins once", &f, WE_C, 1, 0);
	parts_get(WE_P); PE_SetParentPartsNumber(WE_C, WE_P); PE_SetShow(WE_P, false);
	f = we_tick(150, 140, 1, 0); we_target("WE5 hidden parent", &f, WE_A, 1, 0);
	PE_SetShow(WE_P, true); PE_SetAlpha(WE_P, 0);
	f = we_tick(150, 140, 1, 0); we_target("WE5 transparent parent", &f, WE_A, 1, 0);
	PE_SetShow(WE_C, false);
	/* Current hover-state detector is too small; normal-state still hits. */
	PE_SetPartsRectangleDetectionSize(WE_A, 1, 1, 2);
	parts_get(WE_A)->state = PARTS_STATE_HOVERED;
	f = we_tick(150, 140, 1, 0); we_target("WE5 default-state hit area", &f, WE_A, 1, 0);
}

/* WE6: own flag, visual-parent isolation, direct type rules and IDs. */
static void we_eligibility(void)
{
	we_init(); we_box(WE_A, 100, 100, 1, true); we_scope(150, 140);
	struct he_frame f = we_tick(150, 140, 1, 0); we_target("WE6 own true", &f, WE_A, 1, 0);
	we_set(WE_A, false); f = we_tick(150, 140, 1, 0); we_no_local("WE6 own false", &f, WE_A);
	we_set(WE_P, true); PE_SetParentPartsNumber(WE_A, WE_P);
	f = we_tick(150, 140, 1, 0); we_no_local("WE6 visual-parent flag does not qualify child", &f, WE_A);
	/* A hit on both ordinary parts must go to the parent's own handler,
	 * not the front child. Distinct metadata detects the stolen event. */
	br_box(WE_P, 0, 0, 320, 240, true); PE_SetZ(WE_P, 0);
	cp_register_event(WE_P, 81, 801);
	f = we_tick(150, 140, 1, 0); we_target("WE6 eligible visual parent receives own hit", &f, WE_P, 1, 0);
	we_set(WE_A, true);
	f = we_tick(150, 140, 1, 0); we_target("WE6 child own flag wins without bubbling", &f, WE_A, 1, 0);
	we_set(WE_A, false); we_set(WE_P, false);
	f = we_tick(150, 140, 1, 0);
	we_no_local("WE6 child own flag revoked", &f, WE_A);
	we_no_local("WE6 parent own flag revoked", &f, WE_P);
	we_set(WE_Q, true); PE_SetParentPartsNumber(WE_A, WE_Q);
	f = we_tick(150, 140, 1, 0); we_no_local("WE6 reparent does not qualify child", &f, WE_A);
	we_set(WE_Q, false);
	int parent_type = parts_get(WE_Q)->component_type;
	parts_get(WE_Q)->component_type = 5;
	f = we_tick(150, 140, 1, 0); we_no_local("WE6 visual-parent automatic type does not qualify child", &f, WE_A);
	parts_get(WE_Q)->component_type = parent_type;
	parts_release(WE_Q); f = we_tick(150, 140, 1, 0); we_no_local("WE6 released parent", &f, WE_A);
	parts_release(WE_P);
	/* These integer tags name original component types; no widget drawing
	 * or internal focus model is claimed by this generic detector test. */
	int old_type = parts_get(WE_A)->component_type;
	for (int i = 0; i < 3; i++) {
		parts_get(WE_A)->component_type = (int[]){ 5, 7, 15 }[i];
		f = we_tick(150, 140, 1, 0); we_target("WE6 automatic type", &f, WE_A, 1, 0);
	}
	parts_get(WE_A)->component_type = old_type;
	/* Single-element focus is the port's current mapping for composite
	 * controls. Check all native focus-gated tags, without claiming their
	 * not-yet-modelled internal children have been exercised. */
	we_box(WE_B, 100, 100, 0, true);
	for (int i = 0; i < 6; i++) {
		parts_get(WE_A)->component_type = (int[]){ 2, 3, 6, 11, 12, 13 }[i];
		PE_BeginInput(); he_drain();
		f = we_tick(150, 140, 1, 0); we_target("WE6 focus-gated type without focus", &f, WE_B, 1, 0);
		mouse_clear_wheel();
		he_update(150, 140, true, 16); he_update(150, 140, false, 16);
		WE_CHECK(he_hll_int("GetActiveParts") == WE_A, "WE6 type %d did not gain focus", parts_get(WE_A)->component_type);
		f = we_tick(150, 140, 1, 0); we_target("WE6 focused type", &f, WE_A, 1, 0);
		PE_EndInput(); he_drain();
	}
	parts_get(WE_A)->component_type = old_type;
	parts_release(WE_B);
	we_set(-1, true); we_set(0, true);
	WE_CHECK(!parts_try_get(-1) && !parts_try_get(0), "WE6 nonpositive setter created a parts");
	WE_CHECK(!parts_try_get(19499), "WE6 fresh positive ID already exists");
	we_set(19499, true);
	WE_CHECK(parts_try_get(19499), "WE6 positive setter failed to create a parts");
	br_box(19499, 500, 100, 160, 80, true); cp_register_event(19499, 49, 499);
	f = we_tick(550, 140, 1, 0); we_target("WE6 newly created positive ID", &f, 19499, 1, 0);
	parts_release(19499); br_box(19499, 500, 100, 160, 80, true);
	f = we_tick(550, 140, 1, 0); we_no_local("WE6 reused ID starts false", &f, 19499);
}

/* WE7: wheel ignores click-disabled state; invalid position uses last XY. */
static void we_disabled_outside(void)
{
	we_init(); we_box(WE_A, 100, 100, 1, true); we_scope(150, 140);
	PE_SetClickable(WE_A, false); PE_SetButtonEnable(WE_A, false);
	struct he_frame f = we_tick(150, 140, 1, 0); we_target("WE7 disabled nonclickable", &f, WE_A, 1, 0);
	mouse_clear_wheel();
	f = he_update(150, 140, true, 16); f = he_update(150, 140, false, 16);
	WE_CHECK(he_count(&f, 4, WE_A) == 0, "WE7 disabled click sent local click");
	extern bool mouse_focus;
	mouse_focus = false;
	f = we_tick(900, 600, 1, 0); we_target("WE7 no position retains old XY", &f, WE_A, 1, 0);
	WE_CHECK(he_count(&f, 6, -1) == 0, "WE7 no-position update ran hover walk");
	mouse_focus = true;
	/* Focus is a fallback even when it is not hit or wheelable. */
	PE_SetClickable(WE_A, true); PE_SetButtonEnable(WE_A, true);
	mouse_clear_wheel(); he_update(150, 140, false, 16);
	he_update(150, 140, true, 16); he_update(150, 140, false, 16);
	WE_CHECK(he_hll_int("GetActiveParts") == WE_A, "WE7 could not establish focus");
	we_set(WE_A, false); PE_SetAlpha(WE_A, 0);
	f = we_tick(900, 600, 1, 0); we_target("WE7 no-hit fallback ignores eligibility", &f, WE_A, 1, 0);
	mouse_focus = false;
	f = we_tick(950, 650, 1, 0); we_target("WE7 focus fallback without position", &f, WE_A, 1, 0);
	mouse_focus = true;
}

/* WE8: input scopes preserve counts and queued messages; reused metadata. */
static void we_scopes(void)
{
	we_init(); we_box(WE_A, 100, 100, 1, true);
	we_notch(1); we_pump(); struct he_frame f = we_update(150, 140);
	WE_CHECK(he_count(&f, 3, -1) == 0, "WE8 outside input scope sent wheel");
	we_counts("WE8 outside scope preserves pending", 1, 0);
	PE_BeginInput(); f = we_update(150, 140); we_target("WE8 Begin preserves snapshot", &f, WE_A, 1, 0);
	br_msg_release(); PE_UpdateInputState(16);
	PE_BeginInput(); PE_EndInput(); f = he_drain();
	we_target("WE8 nested boundaries preserve queue", &f, WE_A, 1, 0);
	we_notch(-2); we_pump(); PE_EndInput();
	we_counts("WE8 End preserves frozen snapshot", 1, 0);
	mouse_clear_wheel(); we_counts("WE8 End preserves pending", 0, 2);
	mouse_clear_wheel();
	parts_release(WE_A); we_box(WE_A, 100, 100, 1, true); cp_register_event(WE_A, 79, 799);
	we_scope(150, 140); f = we_tick(150, 140, 1, 0);
	we_target("WE8 replacement metadata", &f, WE_A, 1, 0);
	for (int i = 0; i < f.n && i < HE_MAXMSG; i++)
		if (f.m[i].type == 3 && f.m[i].no == WE_A)
			WE_CHECK(f.m[i].delegate == 79 && f.m[i].unique == 799, "WE8 stale replacement metadata");
}

/* WE9: the AIN wait loop's ordering, exercised at its production HLL and
 * parts boundary (not a claim that this runs the entire script loop). */
static void we_callback_order(void)
{
	we_init(); we_box(WE_A, 100, 100, 1, true); we_scope(150, 140);
	we_notch(-1); we_pump();
	we_hll_counts("WE9 callback before View update", 0, 1);
	struct he_frame f = we_update(150, 140); we_target("WE9 View uses callback snapshot", &f, WE_A, 0, 1);
	we_notch(2); we_pump(); we_hll_counts("WE9 mid-View pump", 0, 1);
	we_hll_clear();
	we_hll_counts("WE9 following callback", 2, 0);
	f = we_update(150, 140); we_target("WE9 following View", &f, WE_A, 2, 0);
}

/* WE10: scheduled producer and physical source keep their pointer rules;
 * two-finger threshold events feed the same counting path. AU2/AU11 retain
 * the exhaustive one-notch-per-input-update scheduler assertions. */
static void we_sources(void)
{
	we_init();
	if (!au_symbols()) { we_failures++; return; }
	uint32_t t = SDL_GetTicks() + 1000000;
	WE_CHECK(au_load(au_spec("wheel,%u,150,140,3;wheel,%u,150,140,-2", t, t + 100)) == 2, "WE10 schedule parse");
	for (int i = 0; i < 5; i++) {
		au_update(i < 3 ? t + i : t + 100 + i);
		we_pump(); we_counts("WE10 scheduled one notch", i < 3, i >= 3);
		int x = 0, y = 0;
		WE_CHECK(au_peek(&x, &y) == AU_AUTO && x == 150 && y == 140, "WE10 synthetic wheel lost scheduled pointer");
		mouse_clear_wheel(); PE_UpdateInputState(16);
		if (i == 2) au_update(t + 50); /* finish the first step */
	}
	au_load(NULL); input_modal_closed();
	/* A two-finger motion starts at y=0 in a fresh input process. The
	 * production threshold is 0.02; small binary fractions stay below it. */
	SDL_Event e = { .type = SDL_MULTIGESTURE };
	e.mgesture.numFingers = 2; e.mgesture.touchId = 1000;
	e.mgesture.y = .0078125f; SDL_PushEvent(&e); we_pump();
	we_counts("WE10 gesture below threshold", 0, 0); mouse_clear_wheel();
	e.mgesture.y = .125f; SDL_PushEvent(&e);
	e.mgesture.y = .25f; SDL_PushEvent(&e); we_pump();
	we_counts("WE10 two threshold crossings", 2, 0); mouse_clear_wheel();
	e.mgesture.y = .125f; SDL_PushEvent(&e); we_pump();
	we_counts("WE10 reverse gesture", 0, 1);
}

static void we_sentinel(int no, bool value) { (void)no; (void)value; }

/* WE11: real HLL wrap writes; malformed declarations retain old bindings. */
static void we_hll_shapes(void)
{
	we_init(); we_notch(2); we_notch(-1); we_pump();
	we_hll_counts("WE11 two wrap outputs", 2, 1);
	we_notch(-2); we_pump(); we_hll_clear();
	we_hll_counts("WE11 Clear preserves pending", 0, 2);
	int lib = bu_lib("PartsEngine"), fn = sf_find(lib, "Parts_SetWheelable", 2);
	WE_CHECK(fn >= 0, "WE11 setter declaration missing"); if (fn < 0) return;
	struct ain_hll_function *decl = &ain->libraries[lib].functions[fn];
	struct ain_hll_function saved = *decl;
	void *args_copy = xmalloc(2 * sizeof(*decl->arguments));
	memcpy(args_copy, decl->arguments, 2 * sizeof(*decl->arguments));
	for (int bad = 0; bad < 8; bad++) {
		*decl = saved;
		memcpy(decl->arguments, args_copy, 2 * sizeof(*decl->arguments));
		switch (bad) {
		case 0: decl->return_type.data = AIN_INT; break;
		case 1: decl->nr_arguments = 1; break;
		case 2: decl->arguments[0].type.data = AIN_FLOAT; break;
		case 3: decl->arguments[1].type.data = AIN_INT; break;
		case 4: decl->arguments = NULL; break;
		case 5: ain->version = 13; break;
		case 6: decl->arguments[0].type.array_type = &decl->arguments[1].type; break;
		case 7: decl->arguments[1].type.array_type = &decl->arguments[0].type; break;
		}
		libraries[lib][fn].fun = (void *)we_sentinel;
		bu_postlink();
		WE_CHECK(libraries[lib][fn].fun == (void *)we_sentinel, "WE11 shape/version guard %d overwrote existing binding", bad);
		bu_postlink();
		WE_CHECK(libraries[lib][fn].fun == (void *)we_sentinel, "WE11 repeated installer guard %d overwrote binding", bad);
		ain->version = 14;
	}
	*decl = saved; memcpy(decl->arguments, args_copy, 2 * sizeof(*decl->arguments)); free(args_copy);
	libraries[lib][fn].fun = NULL;
	bu_postlink();
	WE_CHECK(libraries[lib][fn].fun != NULL && libraries[lib][fn].fun != (void *)we_sentinel, "WE11 known shape did not bind");
}

/* WE12: pre-v14 reads remain last integer direction, not accumulated. */
static void we_legacy(void)
{
	we_init(); ain->version = 13;
	for (int multi = 0; multi <= 1; multi++) {
		extern bool parts_multi_controller;
		parts_multi_controller = multi;
		input_modal_closed();
		we_notch(3); we_notch(-2); we_pump(); we_counts("WE12 legacy last direction", 0, 1);
		we_notch(0); we_pump(); we_counts("WE12 legacy zero clears direction", 0, 0);
		we_push(1, 1, SDL_MOUSEWHEEL_FLIPPED, AU_AUTO_MOUSEID); we_pump();
		we_counts("WE12 legacy ignores FLIPPED", 1, 0); mouse_clear_wheel();
		we_counts("WE12 legacy Clear", 0, 0);
		we_push(0, .5f, SDL_MOUSEWHEEL_NORMAL, AU_AUTO_MOUSEID); we_pump();
		we_counts("WE12 legacy ignores preciseY", 0, 0);
		PE_BeginInput(); we_notch(1); we_pump();
		struct he_frame f = we_update(640, 600);
		WE_CHECK(he_count(&f, 3, -1) == 0, "WE12 v13 multi=%d populated v14 wheel queue", multi);
		PE_EndInput();
	}
}

/* WE13: a behavioral save round trip. Cross-reader format and controller
 * root cases are additional group-level compatibility checks. */
static bool we_section_has(struct page *p, int no);

static void we_save_flags(void)
{
	we_init(); we_box(WE_A, 100, 100, 1, false);
	br_box(WE_P, 0, 0, 320, 240, true); PE_SetZ(WE_P, 0);
	cp_register_event(WE_P, 81, 801);
	we_set(WE_P, true); PE_SetParentPartsNumber(WE_A, WE_P);
	we_box(WE_B, 400, 100, 1, true);
	PE_UpdateComponent(0);
	struct page *saved = NULL;
	WE_CHECK(PE_Save(&saved) && saved, "WE13 Save failed"); if (!saved) return;
	WE_CHECK(we_section_has(saved, WE_P) && we_section_has(saved, WE_B) && !we_section_has(saved, WE_A),
		"WE13 Save must store own flags, not visual-parent eligibility");
	WE_CHECK(PE_Load(&saved), "WE13 Load failed");
	we_scope(150, 140);
	struct he_frame f = we_tick(150, 140, 1, 0);
	we_target("WE13 visual parent retains own flag after Load", &f, WE_P, 1, 0);
	we_no_local("WE13 child does not inherit after Load", &f, WE_A);
	f = we_tick(450, 140, 1, 0); we_target("WE13 own flag after Load", &f, WE_B, 1, 0);
	we_set(WE_P, false);
	f = we_tick(150, 140, 1, 0); we_no_local("WE13 child still has no own flag", &f, WE_A);
	we_set(WE_A, true); PE_EndInput();
	struct page *own = NULL;
	WE_CHECK(PE_Save(&own) && own, "WE13 child own flag Save failed");
	if (own) {
		WE_CHECK(we_section_has(own, WE_A) && we_section_has(own, WE_B) && !we_section_has(own, WE_P),
			"WE13 child own flag and revoked parent serialized incorrectly");
		WE_CHECK(PE_Load(&own), "WE13 child own flag Load failed");
		we_scope(150, 140);
		f = we_tick(150, 140, 1, 0); we_target("WE13 child own flag survives round trip", &f, WE_A, 1, 0);
	}
	ca_free_buffer(own); ca_free_buffer(saved);
}

/* Optional XPE extension, a port format choice: PWHE/count/own-true IDs.
 * PCID must remain the final nr+3 words so the previous reader finds it. */
#define WE_SAVE_WHEEL 0x45485750
#define WE_SAVE_IDS 0x44494350
static int we_section(struct page *p, int mark)
{
	for (int i = 0; p && i < p->nr_vars; i++)
		if (p->values[i].i == mark) return i;
	return -1;
}
static bool we_section_has(struct page *p, int no)
{
	int pos = we_section(p, WE_SAVE_WHEEL);
	if (pos < 0 || pos + 1 >= p->nr_vars) return false;
	int count = p->values[pos + 1].i;
	if (count < 0 || count > p->nr_vars - pos - 2) return false;
	for (int i = 0; i < count; i++)
		if (p->values[pos + 2 + i].i == no) return true;
	return false;
}
static void we_remove_section(struct page *p, int mark, int stride)
{
	int pos = we_section(p, mark);
	if (pos < 0 || pos + 1 >= p->nr_vars) return;
	int count = p->values[pos + 1].i;
	WE_CHECK(count >= 0 && count <= (p->nr_vars - pos - 2) / stride, "WE13 malformed optional section in own save");
	if (count < 0 || count > (p->nr_vars - pos - 2) / stride) return;
	int words = 2 + stride * count;
	memmove(p->values + pos, p->values + pos + words, (p->nr_vars - pos - words) * sizeof(*p->values));
	p->nr_vars -= words;
}
static void we_save_format(void)
{
	we_init(); parts_multi_controller = true;
	we_box(WE_A, 100, 100, 1, true);
	we_box(WE_B, 400, 100, 1, true); PE_SetShow(WE_B, false);
	we_box(WE_C, 700, 100, 1, true); parts_get(WE_C)->want_save = false;
	int root = PE_get_active_controller();
	WE_CHECK(parts_try_get(root) && parts_is_controller_root(parts_try_get(root)), "WE13 active root missing");
	we_set(root, true);
	/* Empty CG states need no assets or GL but exercise the existing PCDF
	 * extension alongside PWHE. The last CG state is unflipped. */
	struct string *empty = cstr_to_string("");
	PE_SetPartsCG(WE_Q, empty, 1, 1); PE_SetPartsCG(WE_Q, empty, 0, 2); free_string(empty);
	struct page *all = NULL, *shown = NULL;
	WE_CHECK(PE_Save(&all) && all, "WE13 Save format failed");
	WE_CHECK(PE_SaveWithoutHideParts(&shown) && shown, "WE13 SaveWithoutHideParts failed");
	if (!all || !shown) return;
	WE_CHECK(we_section_has(all, WE_A) && we_section_has(all, WE_B) && we_section_has(all, root)
		&& !we_section_has(all, WE_C), "WE13 save own flags and root, exclude want_save=false");
	WE_CHECK(we_section_has(shown, WE_A) && we_section_has(shown, root)
		&& !we_section_has(shown, WE_B) && !we_section_has(shown, WE_C), "WE13 hidden/want_save filtering");
	int deform = we_section(all, 0x46444350), wheel = we_section(all, WE_SAVE_WHEEL), ids = we_section(all, WE_SAVE_IDS);
	WE_CHECK(deform > 0 && wheel > deform && ids > wheel && ids == all->nr_vars - ctrl_stack.nr_controllers - 3,
		"WE13 PCDF/PWHE/PCID order: %d/%d/%d of %d words", deform, wheel, ids, all->nr_vars);
	WE_CHECK(PE_Load(&all), "WE13 Load combined extensions failed");
	WE_CHECK(parts_try_get(WE_Q) && parts_try_get(WE_Q)->cg_deform[0] == 1 && parts_try_get(WE_Q)->cg_deform[1] == 0,
		"WE13 PWHE damaged PCDF state");
	WE_CHECK(PE_get_active_controller() == root && parts_try_get(root), "WE13 PWHE damaged controller IDs");
	/* A save from the old writer has no PWHE. Every own flag then defaults
	 * false, including a root with no parts record of its own. */
	we_remove_section(all, WE_SAVE_WHEEL, 1);
	WE_CHECK(PE_Load(&all), "WE13 Load old payload without PWHE failed");
	we_scope(150, 140);
	struct he_frame f = we_tick(150, 140, 1, 0); we_no_local("WE13 old payload defaults false", &f, WE_A);
	we_set(WE_A, true);
	struct page *unknown = NULL; PE_Save(&unknown);
	int at = we_section(unknown, WE_SAVE_WHEEL);
	WE_CHECK(at >= 0 && at + 2 < unknown->nr_vars, "WE13 missing section for unknown-ID test");
	if (at >= 0 && at + 2 < unknown->nr_vars) {
		unknown->values[at + 2].i = 19998;
		WE_CHECK(!parts_try_get(19998), "WE13 unknown-ID control already exists");
		WE_CHECK(PE_Load(&unknown), "WE13 unknown-ID payload failed to load");
		WE_CHECK(!parts_try_get(19998), "WE13 optional section created an unknown parts");
	}
	ca_free_buffer(unknown); ca_free_buffer(all); ca_free_buffer(shown);
}
static void we_save_root_single(void)
{
	we_init(); parts_multi_controller = false;
	int root = PE_get_active_controller();
	WE_CHECK(parts_try_get(root), "WE13 root-only control has no root");
	we_set(root, true);
	struct page *saved = NULL;
	WE_CHECK(PE_Save(&saved) && saved, "WE13 single root-only Save failed"); if (!saved) return;
	WE_CHECK(we_section_has(saved, root), "WE13 root-only flag not serialized");
	WE_CHECK(PE_Load(&saved), "WE13 single root-only Load failed");
	/* Check before any parts_get could incidentally rebuild the root. */
	WE_CHECK(parts_try_get(root) && parts_is_controller_root(parts_try_get(root)), "WE13 single root was not restored");
	/* Fallback ignores wheelability, so its target alone cannot prove the
	 * root's own flag survived Load. Check the next writer's PWHE first. */
	struct page *resaved = NULL;
	WE_CHECK(PE_Save(&resaved) && resaved, "WE13 single root-only second Save failed");
	WE_CHECK(we_section_has(resaved, root), "WE13 root-only Load lost the own flag");
	we_box(WE_A, 100, 100, 1, false); we_scope(150, 140);
	WE_CHECK(PE_GetParentPartsNumber(WE_A) == root, "WE13 child is not under restored root");
	struct he_frame f = we_tick(150, 140, 1, 0);
	we_target("WE13 restored root remains fallback", &f, root, 1, 0);
	we_no_local("WE13 restored root flag does not qualify child", &f, WE_A);
	WE_CHECK(PE_Save(&resaved) && resaved && we_section_has(resaved, root) && !we_section_has(resaved, WE_A),
		"WE13 root own flag must remain distinct from child's own flag");
	ca_free_buffer(resaved); ca_free_buffer(saved);
}
static void we_save_legacy(void)
{
	we_init(); ain->version = 13; probe_init_parts_table();
	br_box(WE_A, 100, 100, 160, 80, true);
	struct page *before = NULL, *after = NULL;
	WE_CHECK(PE_Save(&before) && before, "WE13 v13 control Save failed");
	we_set(WE_A, true);
	WE_CHECK(PE_Save(&after) && after, "WE13 v13 guarded Save failed");
	WE_CHECK(before && after && before->nr_vars == after->nr_vars
		&& !memcmp(before->values, after->values, before->nr_vars * sizeof(*before->values)),
		"WE13 v13 setter changed serialized bytes");
	WE_CHECK(we_section(after, WE_SAVE_WHEEL) < 0, "WE13 v13 acquired a wheel extension");
	ca_free_buffer(before); ca_free_buffer(after);
}
static void we_save(void)
{
	void (*cases[])(void) = { we_save_flags, we_save_format, we_save_root_single, we_save_legacy };
	for (int i = 0; i < 4; i++) {
		fflush(stdout); pid_t pid = fork();
		if (!pid) { we_failures = 0; cases[i](); fflush(stdout); _exit(we_failures ? 1 : 0); }
		int status = 0;
		if (pid < 0 || waitpid(pid, &status, 0) != pid) { we_failures++; continue; }
		WE_CHECK(WIFEXITED(status) && WEXITSTATUS(status) == 0, "WE13 subcase %d status %d", i + 1, status);
	}
}

/* WE14: modal close discards the committed count, pending and remainder. */
static void we_modal_reset(void)
{
	we_init();
	we_push(2, 2.5f, SDL_MOUSEWHEEL_NORMAL, AU_AUTO_MOUSEID); we_pump();
	we_counts("WE14 committed two and half remainder", 2, 0);
	we_notch(-2); we_pump(); input_modal_closed();
	we_counts("WE14 modal cleared every count", 0, 0); mouse_clear_wheel();
	we_push(0, .5f, SDL_MOUSEWHEEL_NORMAL, AU_AUTO_MOUSEID); we_pump();
	we_counts("WE14 old fractional residue was discarded", 0, 0); mouse_clear_wheel();
	we_push(0, .5f, SDL_MOUSEWHEEL_NORMAL, AU_AUTO_MOUSEID); we_pump();
	we_counts("WE14 two new half notches", 1, 0);
}

/* WE15: the byte spelling verified in the CN executable, through the
 * production pactex loader. No unverified alternate encoding is invented. */
#define WE_WHEEL_KEY "\xa5\xdb\xa5\xa4\xa9\x60\xa5\xeb\xd3\xd0\x84\xbf"
static struct ex *we_pactex_resource(const char *actname)
{
	struct ex_tree *kids;
	struct ex *e = lb_activity(actname, 6, &kids);
	for (int i = 0; i < 6; i++) {
		char name[20]; snprintf(name, sizeof name, "Wheel%d", i);
		struct ex_tree info = bu_branch(BU_INFO, 1);
		info.children[0] = bu_sleaf(BU_TYPE, BU_LOW);
		struct ex_tree node = lb_node(name, &(struct lb_place){ .x = 200 * i, .y = 100, .om = 1 }, info, 0);
		if (i != 3) {
			node.children = xrealloc(node.children, (node.nr_children + 1) * sizeof(*node.children));
			node.children[node.nr_children++] = i == 4 ? bu_sleaf(WE_WHEEL_KEY, "1")
				: bu_ileaf(i == 5 ? "WheelEnabled" : WE_WHEEL_KEY, (int[]){ 1, 0, 2, 0, 0, 1 }[i]);
		}
		kids->children[i] = node;
	}
	return e;
}
static void we_pactex(void)
{
	we_init();
	struct ex *e = we_pactex_resource("WheelFlags");
	struct string *name = cstr_to_string("WheelFlags");
	bool loaded = probe_load_activity_tree(name, e); free_string(name); ex_free(e);
	WE_CHECK(loaded, "WE15 synthetic pactex did not load"); if (!loaded) return;
	int act = sf_str("WheelFlags", -1), no[6];
	for (int i = 0; i < 6; i++) {
		char item[20]; snprintf(item, sizeof item, "Wheel%d", i);
		no[i] = bu_number(act, item);
		WE_CHECK(no[i] > 0, "WE15 missing synthetic parts %d", i);
		cp_register_event(no[i], 100 + i, 1000 + i);
		/* Avoid layout position being a separate part of this fixture. */
		PE_SetPos(no[i], 200 * i, 100);
		for (int state = 1; state <= 3; state++)
			PE_SetPartsRectangleDetectionSize(no[i], 160, 80, state);
	}
	we_scope(50, 140);
	for (int i = 0; i < 6; i++) {
		struct he_frame f = we_tick(200 * i + 50, 140, 1, 0);
		if (i == 0) we_target("WE15 key value one", &f, no[i], 1, 0);
		else we_no_local("WE15 zero/two/missing/wrong-type/unknown key", &f, no[i]);
	}
	struct page *saved = NULL;
	WE_CHECK(PE_Save(&saved) && saved && PE_Load(&saved), "WE15 pactex Save/Load failed");
	if (saved) {
		struct he_frame f = we_tick(50, 140, 1, 0);
		we_target("WE15 pactex flag persists", &f, no[0], 1, 0);
		ca_free_buffer(saved);
	}
	PE_EndInput(); heap_unref(act);
	/* Load under the old version, then inspect through v14 dispatch. This
	 * guards the loader, without changing old-version queue expectations. */
	ain->version = 13;
	e = we_pactex_resource("WheelLegacy"); name = cstr_to_string("WheelLegacy");
	loaded = probe_load_activity_tree(name, e); free_string(name); ex_free(e);
	WE_CHECK(loaded, "WE15 v13 synthetic pactex did not load");
	ain->version = 14;
	act = sf_str("WheelLegacy", -1);
	int old = bu_number(act, "Wheel0");
	WE_CHECK(old > 0, "WE15 v13 parts missing");
	if (old > 0) {
		PE_SetPos(old, 20, 300);
		for (int state = 1; state <= 3; state++) PE_SetPartsRectangleDetectionSize(old, 160, 80, state);
		we_scope(50, 340);
		struct he_frame f = we_tick(50, 340, 1, 0); we_no_local("WE15 v13 ignored wheel property", &f, old);
	}
	heap_unref(act);
}

static int test_wheel_events(void)
{
	void (*cases[])(void) = { we_bidirectional, we_snapshot, we_fractional, we_dispatch, we_filters,
		we_eligibility, we_disabled_outside, we_scopes, we_callback_order, we_sources, we_hll_shapes,
		we_legacy, we_save, we_modal_reset, we_pactex };
	int failed = 0, n = sizeof(cases) / sizeof(cases[0]);
	for (int i = 0; i < n; i++) {
		fflush(stdout);
		pid_t pid = fork();
		if (pid == 0) {
			we_failures = 0; cases[i](); fflush(stdout); _exit(we_failures ? 1 : 0);
		}
		if (pid < 0) { printf("WE%d fork failed\n", i + 1); failed++; continue; }
		int status = 0; waitpid(pid, &status, 0);
		bool ok = WIFEXITED(status) && WEXITSTATUS(status) == 0;
		if (!ok) failed++;
		printf("WE%d %s (status %d)\n", i + 1, ok ? "PASS" : "FAIL", status);
	}
	printf("WHEEL-EVENTS %s: %d of %d cases failed\n", failed ? "FAIL" : "PASS", failed, n);
	return failed ? 1 : 0;
}
