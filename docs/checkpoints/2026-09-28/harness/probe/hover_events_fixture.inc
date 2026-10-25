/* hover-events: the v14 cursor walk and its messages, as the original's
 * input update sends them (src/parts/input.c, v14_update_input and
 * v14_cursor_walk; G3, 2026-10-20). Read statically:
 *  - 0x5471d0, called at the end of 0x546890 (0x546dc7) with the enter
 *    functor 0x80f7a0 (0x548cf0 -> 0x577b90) and the leave functor
 *    0x80f6c8 (0x548bc0 -> 0x577cf0), walks the elements from the end of
 *    the list: shown (+0x2f0), alpha > 0.0 ([0x813018]) and under the cursor
 *    (vtable +0x4c, state 1) is entered, until an entered one does not pass
 *    the cursor (+0x1a5 0, 0x5472f7); all others are left. Clickable or not
 *    does not count.
 *  - 0x577b90: MouseEnter (vtable 0x80f1dc, type 0x426ae0 -> 0) when +0x1ac
 *    was 0, then 0x564f20 and +0x1b0 = 0; the state 3 with +4, else 2;
 *    OnCursor (0x80f208 -> 6) with x, y, +0x1b0; MouseMove (0x80f6f0 -> 1)
 *    when (x, y) is not +0x1b8/+0x1bc, which it then becomes; +0x1ac = 1.
 *  - 0x577cf0: MouseLeave (0x80f1e8 -> 2) when +0x1ac was 1; the state 1;
 *    +0x1ac = 0.
 *  - 0x546620: every element's +0x1b0 grows by the passed time, every
 *    update, before the walk.
 *  - 0x547c90: the whole-screen MouseMove (0x548054, parts 0) when the
 *    position is not the stored one (+0x18/+0x1c), and the whole-screen
 *    click at a release (0x548064), both only with a position; without one
 *    (0x547e52) no press, no walk (0x5482a7), and the left slot's element
 *    is left when it is the focus (0x5476b0), that is while the left button
 *    is held on it (a release without a position empties the slot first,
 *    0x5481cb -> 0x5462b0); the position is stored only when this and the
 *    last update both had one (0x548381, +0x43).
 *  - 0x546890 at a press: the left slot (+0x1ec) and the focus become the
 *    parts under the cursor; LostFocus (0x80f5a4 -> 20) to the old focus and
 *    Focus (0x80f580 -> 19) to the new one when the focus changes
 *    (0x546a4e..0x546aa2). Without a press and with the left button up, the
 *    update of the release included, the left slot is emptied (0x5468d6 ->
 *    0x547460). The focus is no slot: it is the element's +0x1b4, found
 *    anew every update (0x545c70).
 *  - 0x578580 / 0x5788d0 after the walk: the focus' down state at a press
 *    (clickable and entered), its MouseClick at the release (clickable,
 *    entered, a position; entered without one keeps its state), +4 cleared.
 *  - 0x53e8b0 -> 0x5461a0 at the end of a scope (EndInput, and BeginInput
 *    within a scope): 0x577af0 resets the elements in the manager's slots,
 *    here the left one, which holds a parts only while the button is held:
 *    +0x1ac, +0x1b0, +0x1b8/+0x1bc, +4, +0x1b4; no message. Every other
 *    element keeps its own, a parts just clicked too. The stored position
 *    becomes (0, 0) (0x5461a0, 0x546030).
 *  - 0x53e830 at the beginning (BeginInput, and an EndInput that resumes an
 *    outer scope): 0x53c750 -> 0x538060 -> 0x564cc0 sends LostFocus to the
 *    element whose +0x1b4 is still set and clears it (not +0x1ac); 0x546030
 *    clears +0x40..+0x43, so the first two updates of a scope both send the
 *    whole-screen MouseMove. The outermost EndInput keeps the focus.
 * The messages' variables: CPartsMessageManager@CallDelegate (FUNC 8724)
 * calls CallFunctionMouseEnter / Move / Leave (mouseX, mouseY) through
 * CallEvent2 and CallFunctionMouseOnCursor (mouseX, mouseY, time) through
 * CallEvent3; Focus and LostFocus through CallEvent0.
 *
 *   HE1 one parts, frame by frame (hand-computed): enter, stay, move,
 *       leave, enter again where it last saw the cursor (no MouseMove),
 *       a scope boundary keeps it entered (the whole-screen MouseMove twice
 *       after BeginInput), a press and a release (Focus, down state, the
 *       clicks after the walk), and a scope boundary after the click keeps
 *       it entered too: no MouseEnter again, LostFocus at BeginInput.
 *   HE2 several parts under the cursor (hand-computed): a non-clickable
 *       one that passes, a clickable one that passes, a blocker, a
 *       clickable one behind the blocker, a hidden one and a transparent
 *       one in front; the messages front to back, the states, the press
 *       target (the first clickable or blocking parts) and its click.
 *   HE3 no position (the pointer outside the window): nothing is sent and
 *       nothing changes, except the focus held down, which is left; no click
 *       at a release outside; back inside, the stored-position rule (the
 *       whole-screen MouseMove twice); after a click the pointer leaving
 *       sends nothing (the slot is empty); a release in the update the
 *       pointer leaves: no click, the parts stays entered in its down state.
 *   HE4 outside an input scope nothing is sent and nothing changes; a nested
 *       BeginInput while the button is held on a parts resets that parts
 *       (entered anew, no click at the release); the time since entered of
 *       a parts in no slot goes on.
 *   HE5 Focus and LostFocus at presses (and none for a press on the focus
 *       itself, none after the focus was released); LostFocus at BeginInput
 *       and at an EndInput that resumes a scope, none at the outermost
 *       EndInput (the next BeginInput sends it), none for a focus held down
 *       across a BeginInput (reset with the slot).
 *   HE6 v13 unchanged: the old queue gets the old numbers (enter 1, on 6,
 *       move 2, leave 3) only with the multi controller, the v14 queue
 *       nothing.
 *   HE7 Parts_GetPartsPassCursor through the AIN declaration returns the
 *       flag (true and false): the game calls PE_GetPartsPassCursor (the
 *       unregistered stub pe_v14_stubs.h had is gone).
 *   HE8 a reference model of the walk written from the list above (not
 *       from the port's code) against the engine over 6000 seeded frames:
 *       six overlapping parts of varied kinds, moves, presses, releases,
 *       the pointer leaving the window, scope boundaries; every message
 *       (type, parts, delegate, event ID, variables), every state, the
 *       entered flags, GetClickNumber and GetActiveParts, every frame;
 *       nested scopes too.
 *   HE9 the queue keeps every message while nobody takes them (300
 *       OnCursor in a row; the original's queue is a vector, 0x58aff0).
 *   HE10 a button clicked, then covered within its own layer (a nested
 *       scope and a blocker of the same layer in front): LostFocus at the
 *       BeginInput, MouseLeave at the first walk under the cover; closed
 *       with the cursor elsewhere, the button stays left (no hover look
 *       left behind); with the cursor back on it, entered anew. The game's
 *       confirmation box opens a layer of its own instead, under which the
 *       button is not walked at all: layer-input LI1 and LI2 (G3b).
 * The scroll bar's stop of the walk is checked by scrollbar-knob (K12),
 * which has the GL a bar's look needs.
 * Synthetic rectangles, the production PE_UpdateInputState and the v14
 * message queue, read raw (br_msg_type_raw). No GL, no game assets. Each
 * case runs in its own fork.
 */

static int he_failures;
#define HE_CHECK(c, ...) do { if (!(c)) { printf("  FAIL: "); printf(__VA_ARGS__); printf("\n"); he_failures++; } } while (0)

struct he_msg { int type, no, delegate, unique, n, v[3]; };
#define HE_MAXMSG 64
struct he_frame { int n; struct he_msg m[HE_MAXMSG]; };

static int he_unique(void)
{
	union vm_value r[1] = { BU_I(-999) };
	br_call("PartsEngine", "GetMessageUniqueID", 0, 0, 0, NULL, r, 1);
	return r[0].i;
}

static int he_hll_int(const char *name)
{
	union vm_value r[1] = { BR_I(-1) };
	br_call("PartsEngine", name, 0, 0, 0, NULL, r, 1);
	return r[0].i;
}

/* Every message queued, raw (hover messages included). */
static struct he_frame he_drain(void)
{
	int (*delegate)(void) = (int (*)(void))br_pe_fn("GetMessageDelegateIndex");
	int (*count)(void) = (int (*)(void))br_pe_fn("GetMessageVariableCount");
	int (*var)(int) = (int (*)(int))br_pe_fn("GetMessageVariableInt");
	assert(delegate && count && var);
	struct he_frame f = { 0 };
	for (int t; (t = br_msg_type_raw()) >= 0; br_msg_pop()) {
		if (f.n < HE_MAXMSG) {
			struct he_msg *m = &f.m[f.n];
			*m = (struct he_msg){ t, br_msg_parts(), delegate(), he_unique(), count(), { var(0), var(1), var(2) } };
		}
		f.n++;
	}
	return f;
}

static struct he_frame he_update(int x, int y, bool left, int passed)
{
	extern bool key_state[];
	mouse_set_pos(x, y);
	key_state[VK_LBUTTON] = left;
	PE_UpdateComponent(0);
	PE_UpdateInputState(passed);
	return he_drain();
}

static void he_print(const char *what, const struct he_frame *f)
{
	printf("    %s: %d message(s):", what, f->n);
	for (int i = 0; i < f->n && i < HE_MAXMSG; i++) {
		const struct he_msg *m = &f->m[i];
		printf(" (%d,%d,%d,%d,[", m->type, m->no, m->delegate, m->unique);
		for (int j = 0; j < m->n && j < 3; j++)
			printf(j ? ",%d" : "%d", m->v[j]);
		printf("])");
	}
	printf("\n");
}

/* want: rows of { type, parts, nvars, v0, v1, v2 }; the delegate and the
 * event ID are those of the parts (-1 for parts 0). */
struct he_want { int type, no, n, v0, v1, v2; };
static int he_delegate_of(int no) { return no ? parts_get(no)->delegate_index : -1; }
static int he_unique_of(int no) { return no ? parts_get(no)->unique_id : -1; }
static bool he_frame_is(const char *what, const struct he_frame *f, int n, const struct he_want *w)
{
	bool ok = f->n == n;
	for (int i = 0; ok && i < n; i++) {
		const struct he_msg *m = &f->m[i];
		ok = m->type == w[i].type && m->no == w[i].no && m->n == w[i].n
			&& m->delegate == he_delegate_of(w[i].no) && m->unique == he_unique_of(w[i].no)
			&& (m->n < 1 || m->v[0] == w[i].v0) && (m->n < 2 || m->v[1] == w[i].v1)
			&& (m->n < 3 || m->v[2] == w[i].v2);
	}
	if (!ok) {
		printf("  FAIL: %s\n", what);
		he_print("got", f);
		printf("    want %d:", n);
		for (int i = 0; i < n; i++)
			printf(" (%d,%d,[%d,%d,%d]/%d)", w[i].type, w[i].no, w[i].v0, w[i].v1, w[i].v2, w[i].n);
		printf("\n");
		he_failures++;
	}
	return ok;
}
#define HE_FRAME(what, f, ...) do { const struct he_want he_w_[] = { __VA_ARGS__ }; \
	he_frame_is(what, &(f), sizeof(he_w_) / sizeof(he_w_[0]), he_w_); } while (0)
#define HE_NONE(what, f) HE_CHECK((f).n == 0, "%s: %d message(s), want none", what, (f).n)
/* The messages of that type (and parts, -1 for any) in the frame. */
static int he_count(const struct he_frame *f, int type, int no)
{
	int n = 0;
	for (int i = 0; i < f->n && i < HE_MAXMSG; i++)
		n += f->m[i].type == type && (no < 0 || f->m[i].no == no);
	return n;
}

enum { HE_A = 19301, HE_B = 19302 };

static void he_env(void)
{
	/* The pointer of mouse_set_pos is the warp's here, whose being in the
	 * window is SDL's mouse focus (mouse_in_window). */
	unsetenv("XSYS4_NO_DESKTOP_WARP");
	unsetenv("XSYS4_AUTO_CLICK_SEQ");
	unsetenv("XSYS4_AUTO_CLICK");
	extern bool mouse_focus;
	mouse_focus = true;
}

static void he_setup(void)
{
	he_env();
	br_input_setup();
	br_box(HE_A, 100, 100, 200, 80, true);
	cp_register_event(HE_A, 3, 30);
	extern bool key_state[];
	key_state[VK_LBUTTON] = key_state[VK_RBUTTON] = key_state[VK_MBUTTON] = false;
	PE_BeginInput();
	/* two updates beside everything: the stored position settles */
	he_update(640, 600, false, 0);
	he_update(640, 600, false, 0);
}

static void he_one(void)
{
	he_setup();
	struct parts *a = parts_get(HE_A);
	struct he_frame f = he_update(150, 140, false, 16);
	HE_FRAME("HE1 entering", f,
		{ 1, 0, 2, 150, 140 }, { 0, HE_A, 2, 150, 140 }, { 6, HE_A, 3, 150, 140, 0 }, { 1, HE_A, 2, 150, 140 });
	HE_CHECK(a->is_hovered && a->state == PARTS_STATE_HOVERED, "HE1 entered %d state %d", a->is_hovered, a->state);
	f = he_update(150, 140, false, 16);
	HE_FRAME("HE1 staying", f, { 6, HE_A, 3, 150, 140, 16 });
	f = he_update(160, 150, false, 20);
	HE_FRAME("HE1 moving on it", f, { 1, 0, 2, 160, 150 }, { 6, HE_A, 3, 160, 150, 36 }, { 1, HE_A, 2, 160, 150 });
	f = he_update(640, 600, false, 16);
	HE_FRAME("HE1 leaving", f, { 1, 0, 2, 640, 600 }, { 2, HE_A, 2, 640, 600 });
	HE_CHECK(!a->is_hovered && a->state == PARTS_STATE_DEFAULT, "HE1 left: entered %d state %d", a->is_hovered, a->state);
	f = he_update(640, 600, false, 16);
	HE_NONE("HE1 beside it", f);
	/* back where it last saw the cursor: no MouseMove of its own */
	f = he_update(160, 150, false, 16);
	HE_FRAME("HE1 entering again where it last saw the cursor", f,
		{ 1, 0, 2, 160, 150 }, { 0, HE_A, 2, 160, 150 }, { 6, HE_A, 3, 160, 150, 0 });
	/* a scope boundary: A is in no slot, it stays entered; the stored
	 * position is (0, 0) again, and BeginInput clears the last update's
	 * "had a position" (+0x43, 0x546030): the whole-screen MouseMove twice */
	PE_EndInput();
	HE_CHECK(a->is_hovered && a->state == PARTS_STATE_HOVERED, "HE1 after EndInput: entered %d state %d", a->is_hovered, a->state);
	PE_BeginInput();
	f = he_update(160, 150, false, 16);
	HE_FRAME("HE1 the next scope", f, { 1, 0, 2, 160, 150 }, { 6, HE_A, 3, 160, 150, 16 });
	f = he_update(160, 150, false, 16);
	HE_FRAME("HE1 the next scope's second update", f, { 1, 0, 2, 160, 150 }, { 6, HE_A, 3, 160, 150, 32 });
	f = he_update(160, 150, false, 16);
	HE_FRAME("HE1 the next scope's third update", f, { 6, HE_A, 3, 160, 150, 48 });
	/* a press and a release */
	f = he_update(160, 150, true, 16);
	HE_FRAME("HE1 the press", f, { 19, HE_A, 0 }, { 6, HE_A, 3, 160, 150, 64 });
	HE_CHECK(a->state == PARTS_STATE_CLICKED, "HE1 pressed: state %d", a->state);
	f = he_update(160, 150, false, 16);
	HE_FRAME("HE1 the release", f, { 4, 0, 3, 160, 150, 1 }, { 6, HE_A, 3, 160, 150, 80 }, { 4, HE_A, 3, 160, 150, 1 });
	HE_CHECK(a->state == PARTS_STATE_HOVERED && he_hll_int("GetClickNumber") == HE_A, "HE1 released: state %d, GetClickNumber %d",
		 a->state, he_hll_int("GetClickNumber"));
	/* the end of the scope after the click: the release emptied the left
	 * slot (0x547460) and the focus is no slot, so A keeps its entered flag,
	 * its time and its state; GetActiveParts is 0 outside a scope (the
	 * port's choice) */
	PE_EndInput();
	HE_CHECK(a->is_hovered && a->state == PARTS_STATE_HOVERED && he_hll_int("GetActiveParts") == 0,
		 "HE1 after EndInput past a click: entered %d state %d GetActiveParts %d (want 1, the state kept, 0)",
		 a->is_hovered, a->state, he_hll_int("GetActiveParts"));
	/* the next BeginInput takes the focus: LostFocus (0x564cc0), queued
	 * ahead of the update's messages */
	PE_BeginInput();
	f = he_drain();
	HE_FRAME("HE1 BeginInput after a click", f, { 20, HE_A, 0 });
	HE_CHECK(a->is_hovered && he_hll_int("GetActiveParts") == 0, "HE1 BeginInput after a click: entered %d GetActiveParts %d (want 1, 0)",
		 a->is_hovered, he_hll_int("GetActiveParts"));
	/* no MouseEnter again, no MouseMove of its own: it last saw the cursor here */
	f = he_update(160, 150, false, 16);
	HE_FRAME("HE1 after a click and a scope boundary", f, { 1, 0, 2, 160, 150 }, { 6, HE_A, 3, 160, 150, 96 });
	f = he_update(160, 150, false, 16);
	HE_FRAME("HE1 after a click and a scope boundary (2)", f, { 1, 0, 2, 160, 150 }, { 6, HE_A, 3, 160, 150, 112 });
	f = he_update(160, 150, false, 16);
	HE_FRAME("HE1 after a click and a scope boundary (3)", f, { 6, HE_A, 3, 160, 150, 128 });
	PE_EndInput();
	printf("  HE1 one parts, frame by frame\n");
}

/* HE2: P passes, not clickable (z 50); Q clickable, passes (40); R blocks,
 * not clickable (30); S clickable behind R (20); H hidden (60) and T alpha
 * 0 (55) over everything. */
enum { HE_P = 19311, HE_Q, HE_R, HE_S, HE_H, HE_T };
static void he_several(void)
{
	he_env();
	br_input_setup();
	struct { int no, x, y, w, h, z; bool clickable, pass; } k[] = {
		{ HE_P, 100, 100, 300, 200, 50, false, true },
		{ HE_Q, 120, 120, 200, 150, 40, true, true },
		{ HE_R, 140, 140, 100, 60, 30, false, false },
		{ HE_S, 100, 100, 300, 200, 20, true, false },
		{ HE_H, 0, 0, 1280, 720, 60, true, false },
		{ HE_T, 0, 0, 1280, 720, 55, true, false },
	};
	for (unsigned i = 0; i < sizeof(k) / sizeof(k[0]); i++) {
		br_box(k[i].no, k[i].x, k[i].y, k[i].w, k[i].h, true);
		PE_SetZ(k[i].no, k[i].z);
		PE_SetClickable(k[i].no, k[i].clickable);
		PE_SetPassCursor(k[i].no, k[i].pass);
		cp_register_event(k[i].no, 10 + (int)i, 100 + (int)i);
	}
	PE_SetShow(HE_H, false);
	PE_SetAlpha(HE_T, 0);
	extern bool key_state[];
	key_state[VK_LBUTTON] = false;
	PE_BeginInput();
	he_update(640, 600, false, 0);
	he_update(640, 600, false, 0);
	/* on P, Q and R (and S behind R): P, Q, R entered, front to back */
	struct he_frame f = he_update(150, 150, false, 10);
	HE_FRAME("HE2 on P, Q, R", f, { 1, 0, 2, 150, 150 },
		{ 0, HE_P, 2, 150, 150 }, { 6, HE_P, 3, 150, 150, 0 }, { 1, HE_P, 2, 150, 150 },
		{ 0, HE_Q, 2, 150, 150 }, { 6, HE_Q, 3, 150, 150, 0 }, { 1, HE_Q, 2, 150, 150 },
		{ 0, HE_R, 2, 150, 150 }, { 6, HE_R, 3, 150, 150, 0 }, { 1, HE_R, 2, 150, 150 });
	struct parts *p = parts_get(HE_P), *q = parts_get(HE_Q), *r = parts_get(HE_R), *s = parts_get(HE_S),
		*h = parts_get(HE_H), *t = parts_get(HE_T);
	HE_CHECK(p->state == PARTS_STATE_HOVERED && q->state == PARTS_STATE_HOVERED && r->state == PARTS_STATE_HOVERED
		 && s->state == PARTS_STATE_DEFAULT && !s->is_hovered && !h->is_hovered && !t->is_hovered,
		 "HE2 states P %d Q %d R %d S %d (S entered %d, H %d, T %d)", p->state, q->state, r->state, s->state,
		 s->is_hovered, h->is_hovered, t->is_hovered);
	/* off R, still on P, Q and S: R leaves between Q and S */
	f = he_update(300, 250, false, 10);
	HE_FRAME("HE2 off the blocker", f, { 1, 0, 2, 300, 250 },
		{ 6, HE_P, 3, 300, 250, 10 }, { 1, HE_P, 2, 300, 250 },
		{ 6, HE_Q, 3, 300, 250, 10 }, { 1, HE_Q, 2, 300, 250 },
		{ 2, HE_R, 2, 300, 250 },
		{ 0, HE_S, 2, 300, 250 }, { 6, HE_S, 3, 300, 250, 0 }, { 1, HE_S, 2, 300, 250 });
	/* a press there: the target is Q (the first clickable or blocking
	 * parts), which shows its down state; P and S stay on-cursor */
	f = he_update(300, 250, true, 10);
	HE_FRAME("HE2 the press", f, { 19, HE_Q, 0 },
		{ 6, HE_P, 3, 300, 250, 20 }, { 6, HE_Q, 3, 300, 250, 20 }, { 6, HE_S, 3, 300, 250, 10 });
	HE_CHECK(p->state == PARTS_STATE_HOVERED && q->state == PARTS_STATE_CLICKED && s->state == PARTS_STATE_HOVERED,
		 "HE2 pressed: P %d Q %d S %d", p->state, q->state, s->state);
	f = he_update(300, 250, false, 10);
	HE_FRAME("HE2 the release", f, { 4, 0, 3, 300, 250, 1 },
		{ 6, HE_P, 3, 300, 250, 30 }, { 6, HE_Q, 3, 300, 250, 30 }, { 6, HE_S, 3, 300, 250, 20 },
		{ 4, HE_Q, 3, 300, 250, 1 });
	/* Q made a blocker: S leaves */
	PE_SetPassCursor(HE_Q, false);
	f = he_update(300, 250, false, 10);
	HE_FRAME("HE2 Q blocks", f, { 6, HE_P, 3, 300, 250, 40 }, { 6, HE_Q, 3, 300, 250, 40 }, { 2, HE_S, 2, 300, 250 });
	/* the hidden one shown: everything else leaves */
	PE_SetShow(HE_H, true);
	f = he_update(300, 250, false, 10);
	HE_FRAME("HE2 the cover shown", f, { 0, HE_H, 2, 300, 250 }, { 6, HE_H, 3, 300, 250, 0 }, { 1, HE_H, 2, 300, 250 },
		{ 2, HE_P, 2, 300, 250 }, { 2, HE_Q, 2, 300, 250 });
	PE_EndInput();
	printf("  HE2 several parts under the cursor\n");
}

static void he_outside(void)
{
	he_setup();
	extern bool mouse_focus;
	struct parts *a = parts_get(HE_A);
	he_update(150, 140, false, 16);
	mouse_focus = false;
	HE_CHECK(!mouse_in_window(), "HE3 the pointer is still in the window");
	struct he_frame f = he_update(640, 600, false, 16);
	HE_NONE("HE3 outside", f);
	HE_CHECK(a->is_hovered && a->state == PARTS_STATE_HOVERED, "HE3 outside: entered %d state %d (want both kept)", a->is_hovered, a->state);
	mouse_focus = true;
	f = he_update(150, 140, false, 16);
	HE_FRAME("HE3 back at the same point", f, { 6, HE_A, 3, 150, 140, 32 });
	/* pressed inside, released outside: the focus is left, no click */
	f = he_update(150, 140, true, 16);
	HE_FRAME("HE3 the press", f, { 19, HE_A, 0 }, { 6, HE_A, 3, 150, 140, 48 });
	mouse_focus = false;
	f = he_update(700, 600, true, 16);
	HE_FRAME("HE3 held outside", f, { 2, HE_A, 2, 0, 0 });
	HE_CHECK(!a->is_hovered && a->state == PARTS_STATE_DEFAULT, "HE3 held outside: entered %d state %d", a->is_hovered, a->state);
	f = he_update(700, 600, false, 16);
	HE_NONE("HE3 released outside", f);
	HE_CHECK(he_hll_int("GetClickNumber") == 0, "HE3 released outside: GetClickNumber %d", he_hll_int("GetClickNumber"));
	/* back inside elsewhere: the first update has no stored position of the
	 * last one, so the whole-screen MouseMove comes twice */
	mouse_focus = true;
	f = he_update(170, 150, false, 16);
	HE_FRAME("HE3 back (1)", f, { 1, 0, 2, 170, 150 }, { 0, HE_A, 2, 170, 150 }, { 6, HE_A, 3, 170, 150, 0 }, { 1, HE_A, 2, 170, 150 });
	f = he_update(170, 150, false, 16);
	HE_FRAME("HE3 back (2)", f, { 1, 0, 2, 170, 150 }, { 6, HE_A, 3, 170, 150, 16 });
	f = he_update(170, 150, false, 16);
	HE_FRAME("HE3 back (3)", f, { 6, HE_A, 3, 170, 150, 32 });
	/* a click (A is the focus still: no Focus), then the pointer leaves:
	 * the release emptied the left slot, so 0x5476b0 leaves nothing */
	f = he_update(170, 150, true, 16);
	HE_FRAME("HE3 the press on the focus", f, { 6, HE_A, 3, 170, 150, 48 });
	f = he_update(170, 150, false, 16);
	HE_FRAME("HE3 the release", f, { 4, 0, 3, 170, 150, 1 }, { 6, HE_A, 3, 170, 150, 64 }, { 4, HE_A, 3, 170, 150, 1 });
	mouse_focus = false;
	f = he_update(700, 600, false, 16);
	HE_NONE("HE3 outside after a click", f);
	HE_CHECK(a->is_hovered && a->state == PARTS_STATE_HOVERED, "HE3 outside after a click: entered %d state %d (want both kept)",
		 a->is_hovered, a->state);
	mouse_focus = true;
	f = he_update(170, 150, false, 16);
	HE_FRAME("HE3 back after a click", f, { 6, HE_A, 3, 170, 150, 96 });
	/* released in the update the pointer leaves: 0x5462b0 empties the slot
	 * before 0x5476b0, and 0x5788d0 sends no click without a position and
	 * leaves an entered parts in its state */
	f = he_update(170, 150, true, 16);
	HE_FRAME("HE3 another press", f, { 6, HE_A, 3, 170, 150, 112 });
	mouse_focus = false;
	f = he_update(700, 600, false, 16);
	HE_NONE("HE3 released as the pointer leaves", f);
	HE_CHECK(a->is_hovered && a->state == PARTS_STATE_CLICKED && he_hll_int("GetClickNumber") == 0,
		 "HE3 released as the pointer leaves: entered %d state %d GetClickNumber %d (want 1, the down state, 0)",
		 a->is_hovered, a->state, he_hll_int("GetClickNumber"));
	mouse_focus = true;
	f = he_update(170, 150, false, 16);
	HE_FRAME("HE3 back after that", f, { 6, HE_A, 3, 170, 150, 144 });
	HE_CHECK(a->state == PARTS_STATE_HOVERED, "HE3 back after that: state %d", a->state);
	PE_EndInput();
	printf("  HE3 no position\n");
}

static void he_scopes(void)
{
	he_setup();
	struct parts *a = parts_get(HE_A);
	br_box(HE_B, 500, 100, 200, 80, true);
	PE_SetZ(HE_B, 5);	/* in front of A: its messages come first */
	cp_register_event(HE_B, 4, 40);
	struct parts *b = parts_get(HE_B);
	he_update(150, 140, false, 16);
	PE_EndInput();
	/* outside a scope: nothing, whatever the cursor does */
	struct he_frame f = he_update(550, 140, false, 16);
	HE_NONE("HE4 outside a scope", f);
	f = he_update(550, 140, true, 16);
	f = he_update(550, 140, false, 16);
	HE_NONE("HE4 a click outside a scope", f);
	HE_CHECK(a->is_hovered && a->state == PARTS_STATE_HOVERED && !b->is_hovered, "HE4 outside a scope: A %d/%d B %d",
		 a->is_hovered, a->state, b->is_hovered);
	PE_BeginInput();
	f = he_update(550, 140, false, 16);
	HE_FRAME("HE4 the scope again", f, { 1, 0, 2, 550, 140 },
		{ 0, HE_B, 2, 550, 140 }, { 6, HE_B, 3, 550, 140, 0 }, { 1, HE_B, 2, 550, 140 }, { 2, HE_A, 2, 550, 140 });
	/* B pressed (the focus and the left slot) and held across a nested
	 * BeginInput: 0x5461a0 resets B with the slot (its +0x1b4 too: no
	 * LostFocus); B is entered anew, and the release clicks nothing */
	f = he_update(550, 140, true, 16);
	HE_FRAME("HE4 the press on B", f, { 1, 0, 2, 550, 140 }, { 19, HE_B, 0 }, { 6, HE_B, 3, 550, 140, 16 });
	PE_BeginInput();
	f = he_drain();
	HE_NONE("HE4 nested BeginInput with B held", f);
	HE_CHECK(!b->is_hovered && b->state == PARTS_STATE_CLICKED && he_hll_int("GetActiveParts") == 0,
		 "HE4 nested BeginInput with B held: entered %d state %d GetActiveParts %d (want 0, the state kept, 0)",
		 b->is_hovered, b->state, he_hll_int("GetActiveParts"));
	f = he_update(550, 140, true, 16);
	HE_FRAME("HE4 held in the nested scope", f, { 1, 0, 2, 550, 140 },
		{ 0, HE_B, 2, 550, 140 }, { 6, HE_B, 3, 550, 140, 0 }, { 1, HE_B, 2, 550, 140 });
	HE_CHECK(b->state == PARTS_STATE_HOVERED, "HE4 held in the nested scope: B state %d", b->state);
	f = he_update(550, 140, false, 16);
	HE_FRAME("HE4 the release in the nested scope", f, { 1, 0, 2, 550, 140 }, { 4, 0, 3, 550, 140, 1 }, { 6, HE_B, 3, 550, 140, 16 });
	HE_CHECK(he_hll_int("GetClickNumber") == 0, "HE4 the release in the nested scope: GetClickNumber %d", he_hll_int("GetClickNumber"));
	PE_EndInput();
	f = he_drain();
	HE_NONE("HE4 the nested EndInput (no focus)", f);
	/* A last saw the cursor here: no MouseMove of its own */
	f = he_update(150, 140, false, 16);
	HE_FRAME("HE4 onto A", f, { 1, 0, 2, 150, 140 }, { 2, HE_B, 2, 150, 140 },
		{ 0, HE_A, 2, 150, 140 }, { 6, HE_A, 3, 150, 140, 0 });
	f = he_update(150, 140, false, 16);
	HE_FRAME("HE4 A, the second update after the EndInput", f, { 1, 0, 2, 150, 140 }, { 6, HE_A, 3, 150, 140, 16 });
	/* a nested BeginInput with A in no slot: A's time goes on */
	PE_BeginInput();
	f = he_update(150, 140, false, 16);
	HE_FRAME("HE4 nested scope", f, { 1, 0, 2, 150, 140 }, { 6, HE_A, 3, 150, 140, 32 });
	PE_EndInput();
	PE_EndInput();
	printf("  HE4 scopes\n");
}

static void he_focus(void)
{
	he_setup();
	br_box(HE_B, 500, 100, 200, 80, true);
	PE_SetZ(HE_B, 5);
	cp_register_event(HE_B, 4, 40);
	he_update(150, 140, false, 16);
	struct he_frame f = he_update(150, 140, true, 16);
	HE_FRAME("HE5 press on A", f, { 19, HE_A, 0 }, { 6, HE_A, 3, 150, 140, 16 });
	he_update(150, 140, false, 16);
	f = he_update(150, 140, true, 16);
	HE_FRAME("HE5 press on A again (no focus change)", f, { 6, HE_A, 3, 150, 140, 48 });
	he_update(150, 140, false, 16);
	he_update(550, 140, false, 16);
	f = he_update(550, 140, true, 16);
	HE_FRAME("HE5 press on B", f, { 20, HE_A, 0 }, { 19, HE_B, 0 }, { 6, HE_B, 3, 550, 140, 16 });
	HE_CHECK(he_hll_int("GetActiveParts") == HE_B, "HE5 GetActiveParts %d", he_hll_int("GetActiveParts"));
	he_update(550, 140, false, 16);
	he_update(640, 600, false, 16);
	f = he_update(640, 600, true, 16);
	HE_FRAME("HE5 press beside everything", f, { 20, HE_B, 0 });
	f = he_update(640, 600, false, 16);
	HE_FRAME("HE5 its release", f, { 4, 0, 3, 640, 600, 1 });
	f = he_update(640, 600, true, 16);
	HE_NONE("HE5 press beside everything again", f);
	he_update(640, 600, false, 16);
	/* the focus released: no LostFocus for it */
	he_update(550, 140, false, 16);
	he_update(550, 140, true, 16);
	he_update(550, 140, false, 16);
	PE_ReleaseParts(HE_B);
	he_drain();
	f = he_update(150, 140, false, 16);
	f = he_update(150, 140, true, 16);
	HE_FRAME("HE5 press on A after the focus was released", f, { 19, HE_A, 0 }, { 6, HE_A, 3, 150, 140, 16 });
	he_update(150, 140, false, 16);
	/* a nested BeginInput takes the focus (0x53e830 -> 0x564cc0) */
	PE_BeginInput();
	f = he_drain();
	HE_FRAME("HE5 nested BeginInput", f, { 20, HE_A, 0 });
	HE_CHECK(he_hll_int("GetActiveParts") == 0, "HE5 nested BeginInput: GetActiveParts %d", he_hll_int("GetActiveParts"));
	he_update(150, 140, false, 16);
	f = he_update(150, 140, true, 16);
	HE_CHECK(he_count(&f, 19, HE_A) == 1 && he_count(&f, 20, -1) == 0, "HE5 press in the nested scope: Focus %d LostFocus %d (want 1, 0)",
		 he_count(&f, 19, HE_A), he_count(&f, 20, -1));
	he_update(150, 140, false, 16);
	/* so does an EndInput that resumes the outer scope */
	PE_EndInput();
	f = he_drain();
	HE_FRAME("HE5 nested EndInput", f, { 20, HE_A, 0 });
	/* the outermost EndInput keeps the focus; the next BeginInput takes it */
	he_update(150, 140, false, 16);
	f = he_update(150, 140, true, 16);
	HE_CHECK(he_count(&f, 19, HE_A) == 1, "HE5 press in the outer scope again: Focus %d", he_count(&f, 19, HE_A));
	he_update(150, 140, false, 16);
	PE_EndInput();
	f = he_drain();
	HE_NONE("HE5 the outermost EndInput", f);
	HE_CHECK(he_hll_int("GetActiveParts") == 0, "HE5 outside a scope: GetActiveParts %d", he_hll_int("GetActiveParts"));
	PE_BeginInput();
	f = he_drain();
	HE_FRAME("HE5 BeginInput after the outermost EndInput", f, { 20, HE_A, 0 });
	/* the focus held down across a BeginInput: reset with the left slot
	 * (0x577af0 clears its +0x1b4), no LostFocus; the release in the new
	 * scope is the whole-screen click alone */
	he_update(150, 140, false, 16);
	f = he_update(150, 140, true, 16);
	HE_CHECK(he_count(&f, 19, HE_A) == 1, "HE5 press before the held BeginInput: Focus %d", he_count(&f, 19, HE_A));
	PE_BeginInput();
	f = he_drain();
	HE_NONE("HE5 BeginInput with the focus held down", f);
	f = he_update(150, 140, false, 16);
	HE_CHECK(he_count(&f, 4, 0) == 1 && he_count(&f, 4, HE_A) == 0 && he_count(&f, 20, -1) == 0,
		 "HE5 the release in the new scope: whole-screen click %d, A's click %d, LostFocus %d (want 1, 0, 0)",
		 he_count(&f, 4, 0), he_count(&f, 4, HE_A), he_count(&f, 20, -1));
	PE_EndInput();
	f = he_drain();
	HE_NONE("HE5 the EndInput after it (no focus)", f);
	PE_EndInput();
	printf("  HE5 focus\n");
}

static void he_v13(void)
{
	he_setup();
	PE_EndInput();
	he_drain();
	int version = ain->version;
	ain->version = 13;
	extern bool parts_multi_controller;
	bool multi = parts_multi_controller;
	struct parts *a = parts_get(HE_A);
	/* without the multi controller (before Rance 9): no message at all */
	parts_multi_controller = false;
	PE_BeginInput();
	PE_ReleaseMessage();
	he_update(640, 600, false, 16);
	he_update(150, 140, false, 16);
	HE_CHECK(PE_GetMessageType() == 0, "HE6 v13 without the multi controller: old queue type %d", PE_GetMessageType());
	HE_CHECK(br_msg_type_raw() < 0, "HE6 v13: v14 queue type %d", br_msg_type_raw());
	HE_CHECK(a->is_hovered && a->state == PARTS_STATE_HOVERED, "HE6 v13 hovered %d state %d", a->is_hovered, a->state);
	he_update(640, 600, false, 16);
	PE_EndInput();
	/* with it: the old numbers on the old queue */
	parts_multi_controller = true;
	PE_BeginInput();
	PE_ReleaseMessage();
	he_update(640, 600, false, 16);
	PE_ReleaseMessage();
	he_update(150, 140, false, 16);
	int got[8], n = 0;
	for (int t; n < 8 && (t = PE_GetMessageType()) != 0; PE_PopMessage())
		got[n++] = t;
	HE_CHECK(n == 3 && got[0] == 1 && got[1] == 6 && got[2] == 2, "HE6 v13 entering: %d old message(s) %d %d %d; want 1 6 2",
		 n, n > 0 ? got[0] : -1, n > 1 ? got[1] : -1, n > 2 ? got[2] : -1);
	he_update(640, 600, false, 16);
	n = 0;
	for (int t; n < 8 && (t = PE_GetMessageType()) != 0; PE_PopMessage())
		got[n++] = t;
	HE_CHECK(n == 1 && got[0] == 3, "HE6 v13 leaving: %d old message(s), first %d; want 3", n, n ? got[0] : -1);
	HE_CHECK(br_msg_type_raw() < 0, "HE6 v13: v14 queue type %d", br_msg_type_raw());
	PE_EndInput();
	PE_ReleaseMessage();
	parts_multi_controller = multi;
	ain->version = version;
	printf("  HE6 v13 unchanged\n");
}

static void he_pass_getter(void)
{
	he_setup();
	PE_SetPassCursor(HE_A, true);
	int got = BU_PE("Parts_GetPartsPassCursor", BU_I(HE_A)).i;
	HE_CHECK(got == 1, "HE7 Parts_GetPartsPassCursor after SetPassCursor(true) = %d", got);
	BU_PE("Parts_SetPassCursor", BU_I(HE_A), BU_I(0));
	got = BU_PE("Parts_GetPartsPassCursor", BU_I(HE_A)).i;
	HE_CHECK(got == 0 && !parts_get(HE_A)->pass_cursor, "HE7 after Parts_SetPassCursor(false) = %d", got);
	BU_PE("Parts_SetPassCursor", BU_I(HE_A), BU_I(1));
	HE_CHECK(parts_get(HE_A)->pass_cursor && BU_PE("Parts_GetPartsPassCursor", BU_I(HE_A)).i == 1, "HE7 the HLL setter");
	PE_EndInput();
	printf("  HE7 Parts_GetPartsPassCursor\n");
}

/* HE9: the queue keeps every message while nobody takes them (the
 * original's is a vector, 0x58aff0; this ring was 64 long and dropped the
 * rest). */
static void he_queue(void)
{
	he_setup();
	he_update(150, 140, false, 16);
	extern bool key_state[];
	key_state[VK_LBUTTON] = false;
	for (int i = 0; i < 300; i++) {
		mouse_set_pos(150, 140);
		PE_UpdateComponent(0);
		PE_UpdateInputState(1);
	}
	struct he_frame f = he_drain();
	HE_CHECK(f.n == 300, "HE9 %d message(s) after 300 updates without taking them, want 300", f.n);
	bool times = f.n >= 3 && f.m[0].type == 6 && f.m[0].v[2] == 1 && f.m[2].type == 6 && f.m[2].v[2] == 3;
	HE_CHECK(times, "HE9 the first messages are not OnCursor with the times 1, 2, 3");
	PE_EndInput();
	printf("  HE9 the queue keeps every message\n");
}

/* HE10: a button clicked, then covered within its own layer: a nested
 * scope and a blocker over the screen in the button's layer (the game's
 * confirmation box is a layer of its own: layer-input). The clicked button is in no slot after the release, so it stays
 * entered over the scope boundary and the first walk under the cover
 * leaves it (MouseLeave: the script's hover look goes); closed with the
 * cursor elsewhere, nothing more comes for it; back on it, it is entered
 * anew. */
enum { HE_C = 19303 };
static void he_cover(void)
{
	he_setup();
	struct parts *a = parts_get(HE_A);
	br_box(HE_C, 0, 0, 1280, 720, true);
	PE_SetZ(HE_C, 100);
	PE_SetShow(HE_C, false);
	cp_register_event(HE_C, 5, 50);
	struct parts *c = parts_get(HE_C);
	struct he_frame f = he_update(150, 140, false, 16);
	HE_FRAME("HE10 entering the button", f,
		{ 1, 0, 2, 150, 140 }, { 0, HE_A, 2, 150, 140 }, { 6, HE_A, 3, 150, 140, 0 }, { 1, HE_A, 2, 150, 140 });
	f = he_update(150, 140, true, 16);
	HE_FRAME("HE10 the press", f, { 19, HE_A, 0 }, { 6, HE_A, 3, 150, 140, 16 });
	f = he_update(150, 140, false, 16);
	HE_FRAME("HE10 the click", f, { 4, 0, 3, 150, 140, 1 }, { 6, HE_A, 3, 150, 140, 32 }, { 4, HE_A, 3, 150, 140, 1 });
	/* the click's handler opens the box */
	PE_BeginInput();
	PE_SetShow(HE_C, true);
	f = he_drain();
	HE_FRAME("HE10 the box's BeginInput", f, { 20, HE_A, 0 });
	HE_CHECK(a->is_hovered && a->state == PARTS_STATE_HOVERED, "HE10 the box's BeginInput: entered %d state %d (want 1, kept)",
		 a->is_hovered, a->state);
	f = he_update(150, 140, false, 16);
	HE_FRAME("HE10 under the box", f, { 1, 0, 2, 150, 140 },
		{ 0, HE_C, 2, 150, 140 }, { 6, HE_C, 3, 150, 140, 0 }, { 1, HE_C, 2, 150, 140 }, { 2, HE_A, 2, 150, 140 });
	HE_CHECK(!a->is_hovered && a->state == PARTS_STATE_DEFAULT, "HE10 under the box: entered %d state %d", a->is_hovered, a->state);
	/* the box closed (its cancel at (818, 467)), the cursor moved off */
	he_update(818, 467, false, 16);
	PE_SetShow(HE_C, false);
	PE_EndInput();
	f = he_drain();
	HE_NONE("HE10 the box's EndInput", f);
	f = he_update(1100, 300, false, 16);
	HE_FRAME("HE10 closed, the cursor elsewhere", f, { 1, 0, 2, 1100, 300 }, { 2, HE_C, 2, 1100, 300 });
	f = he_update(1100, 300, false, 16);
	HE_FRAME("HE10 closed, the second update", f, { 1, 0, 2, 1100, 300 });
	HE_CHECK(!a->is_hovered && a->state == PARTS_STATE_DEFAULT && !c->is_hovered, "HE10 closed: A entered %d state %d, box entered %d",
		 a->is_hovered, a->state, c->is_hovered);
	f = he_update(150, 140, false, 16);
	HE_FRAME("HE10 back on the button", f, { 1, 0, 2, 150, 140 }, { 0, HE_A, 2, 150, 140 }, { 6, HE_A, 3, 150, 140, 0 });
	PE_EndInput();
	printf("  HE10 a clicked button covered by a child scene\n");
}

/* --- HE8: the reference model --- */

#define HM_N 6
struct hm_box { int no, x, y, w, h, z; bool clickable, pass, show; int alpha; int delegate, unique; };
struct hm_elem { bool entered, down, focus; int time, lx, ly, state; };	/* state 1, 2, 3 */
static struct {
	struct hm_box b[HM_N];
	struct hm_elem e[HM_N];
	int order[HM_N];	/* front to back */
	int sx, sy;		/* +0x18 / +0x1c */
	bool prev_avail;	/* +0x43 */
	bool prev_left;		/* +0x40 */
	int left_slot, focus;	/* box indices or -1: +0x1ec, and the element with +0x1b4 */
	int clicked;		/* GetClickNumber, a parts number */
	bool scope;
	int depth;		/* BeginInput minus EndInput, at least 0 */
	/* what the run covered: LostFocus at a beginning, nested BeginInput,
	 * a focus entered and in no slot kept over a boundary, a release
	 * without a position with a parts in the left slot */
	int n_begin_lost, n_nested, n_kept, n_release_out;
	struct he_frame out;
} hm;

static void hm_emit(int type, int box, int n, int v0, int v1, int v2)
{
	if (hm.out.n < HE_MAXMSG) {
		struct he_msg *m = &hm.out.m[hm.out.n];
		*m = (struct he_msg){ type, box >= 0 ? hm.b[box].no : 0, box >= 0 ? hm.b[box].delegate : -1,
			box >= 0 ? hm.b[box].unique : -1, n, { n > 0 ? v0 : 0, n > 1 ? v1 : 0, n > 2 ? v2 : 0 } };
	}
	hm.out.n++;
}

static bool hm_hit(int i, int x, int y)
{
	const struct hm_box *b = &hm.b[i];
	return b->show && b->alpha > 0 && x >= b->x && x < b->x + b->w && y >= b->y && y < b->y + b->h;
}

static void hm_leave(int i, int x, int y)
{
	if (hm.e[i].entered)
		hm_emit(2, i, 2, x, y, 0);
	hm.e[i].entered = false;
	hm.e[i].state = 1;
}

static void hm_reset_elem(int i)
{
	if (i < 0)
		return;
	hm.e[i].entered = hm.e[i].down = hm.e[i].focus = false;
	hm.e[i].time = hm.e[i].lx = hm.e[i].ly = 0;
}

/* the end of a scope (0x5461a0: the slots' elements reset, the left one
 * here; the focus with it when it is that element), then optionally the
 * beginning (0x53e830: LostFocus to the element still focused; 0x546030: the
 * buttons sampled, +0x43 cleared, the stored position 0). The messages go
 * to hm.out ahead of the next update's. */
static void hm_scope(bool begin, bool left_down)
{
	if (hm.left_slot >= 0) {
		if (hm.focus == hm.left_slot)
			hm.focus = -1;
		hm_reset_elem(hm.left_slot);
	}
	hm.left_slot = -1;
	if (hm.focus >= 0 && hm.e[hm.focus].entered)
		hm.n_kept++;
	if (begin) {
		if (hm.focus >= 0 && hm.e[hm.focus].focus) {
			hm_emit(20, hm.focus, 0, 0, 0, 0);
			hm.n_begin_lost++;
		}
		if (hm.focus >= 0)
			hm.e[hm.focus].focus = false;
		hm.focus = -1;
		hm.prev_avail = false;
	}
	hm.sx = hm.sy = 0;
	hm.clicked = 0;
	hm.prev_left = begin && left_down;
	hm.scope = begin;
}

/* PE_BeginInput and PE_EndInput, both sides */
static void hm_begin(bool left_down)
{
	PE_BeginInput();
	/* a BeginInput within a scope ends it first (0x58a720: 0x53e8b0) */
	hm_scope(true, left_down);
	hm.n_nested += hm.depth > 0;
	hm.depth++;
}
static void hm_end(bool left_down)
{
	PE_EndInput();
	if (hm.depth)
		hm.depth--;
	/* 0x58a750: 0x53e8b0, and 0x53e830 when an outer scope is left */
	hm_scope(hm.depth > 0, left_down);
}

static void hm_update(int x, int y, bool left, bool avail, int passed)
{
	if (!hm.scope)
		return;
	for (int i = 0; i < HM_N; i++)
		hm.e[i].time += passed;
	hm.clicked = 0;
	if (!avail) {
		x = hm.sx;
		y = hm.sy;
	}
	bool moved = x != hm.sx || y != hm.sy;
	bool press = left && !hm.prev_left, release = !left && hm.prev_left;
	hm.prev_left = left;
	/* a release without a position empties the left slot first (0x5481cb
	 * -> 0x5462b0) */
	if (!avail && release) {
		hm.n_release_out += hm.left_slot >= 0;
		hm.left_slot = -1;
	}
	if (avail) {
		if (moved)
			hm_emit(1, -1, 2, x, y, 0);
		if (release)
			hm_emit(4, -1, 3, x, y, 1);
		if (press) {
			int t = -1;
			for (int k = 0; k < HM_N && t < 0; k++) {
				int i = hm.order[k];
				if (hm_hit(i, x, y) && (hm.b[i].clickable || !hm.b[i].pass))
					t = i;
			}
			if (t != hm.focus) {
				if (hm.focus >= 0 && hm.e[hm.focus].focus) {
					hm_emit(20, hm.focus, 0, 0, 0, 0);
					hm.e[hm.focus].focus = false;
				}
				if (t >= 0 && !hm.e[t].focus) {
					hm_emit(19, t, 0, 0, 0, 0);
					hm.e[t].focus = true;
				}
			}
			hm.focus = hm.left_slot = t;
		} else if (!left) {
			/* 0x5468d6 -> 0x547460: no press, the button up */
			hm.left_slot = -1;
		}
		bool stopped = false;
		for (int k = 0; k < HM_N; k++) {
			int i = hm.order[k];
			struct hm_elem *e = &hm.e[i];
			if (!stopped && hm_hit(i, x, y)) {
				if (!e->entered) {
					hm_emit(0, i, 2, x, y, 0);
					e->time = 0;
				}
				e->state = e->down ? 3 : 2;
				hm_emit(6, i, 3, x, y, e->time);
				if (x != e->lx || y != e->ly)
					hm_emit(1, i, 2, x, y, 0);
				e->lx = x;
				e->ly = y;
				e->entered = true;
				if (!hm.b[i].pass)
					stopped = true;
			} else {
				hm_leave(i, x, y);
			}
		}
	} else if (hm.left_slot >= 0 && hm.left_slot == hm.focus) {
		hm_leave(hm.left_slot, 0, 0);
	}
	/* the focus' left button (0x578580, 0x5788d0) */
	if (hm.focus >= 0) {
		struct hm_elem *e = &hm.e[hm.focus];
		const struct hm_box *b = &hm.b[hm.focus];
		if (press && e->entered && b->clickable) {
			e->state = 3;
			e->down = true;
		}
		if (release && b->clickable) {
			e->down = false;
			if (!e->entered) {
				e->state = 1;
			} else if (avail) {
				hm_emit(4, hm.focus, 3, x, y, 1);
				e->state = 2;
				hm.clicked = b->no;
			}
		}
	}
	if (avail && hm.prev_avail) {
		hm.sx = x;
		hm.sy = y;
	}
	hm.prev_avail = avail;
}

static unsigned hm_rand_state;
static unsigned hm_rand(void)
{
	hm_rand_state = hm_rand_state * 1103515245u + 12345u;
	return (hm_rand_state >> 16) & 0x7fff;
}

static bool hm_same_msg(const struct he_msg *a, const struct he_msg *b)
{
	if (a->type != b->type || a->no != b->no || a->delegate != b->delegate || a->unique != b->unique || a->n != b->n)
		return false;
	for (int j = 0; j < a->n && j < 3; j++)
		if (a->v[j] != b->v[j])
			return false;
	return true;
}

static void he_model(void)
{
	he_env();
	br_input_setup();
	extern bool key_state[], mouse_focus;
	hm_rand_state = 20261020;
	const int z[HM_N] = { 60, 50, 40, 30, 20, 10 };
	for (int i = 0; i < HM_N; i++) {
		struct hm_box *b = &hm.b[i];
		b->no = 19401 + i;
		b->x = 80 + (int)(hm_rand() % 160);
		b->y = 80 + (int)(hm_rand() % 120);
		b->w = 120 + (int)(hm_rand() % 200);
		b->h = 80 + (int)(hm_rand() % 140);
		b->z = z[i];
		b->clickable = hm_rand() % 3 != 0;
		b->pass = hm_rand() % 2;
		b->show = true;
		b->alpha = 255;
		b->delegate = 20 + i;
		b->unique = 200 + i;
		br_box(b->no, b->x, b->y, b->w, b->h, true);
		PE_SetZ(b->no, b->z);
		PE_SetClickable(b->no, b->clickable);
		PE_SetPassCursor(b->no, b->pass);
		cp_register_event(b->no, b->delegate, b->unique);
		hm.order[i] = i;
	}
	for (int i = 0; i < HM_N; i++) {
		hm.e[i] = (struct hm_elem){ .state = 1 };
	}
	hm.left_slot = hm.focus = -1;
	hm.prev_avail = false;
	hm.depth = 0;
	hm.out.n = 0;
	key_state[VK_LBUTTON] = false;
	mouse_focus = true;
	hm_begin(false);
	bool left = false, avail = true;
	int x = 640, y = 600, bad = 0, frames = 6000, nmsg = 0, nenter = 0, nclick = 0, nfocus = 0;
	for (int frame = 0; frame < frames && bad < 5; frame++) {
		unsigned r = hm_rand() % 100;
		if (r < 3) {
			/* a scope boundary: EndInput (and BeginInput) */
			hm_end(left);
			if (hm_rand() % 4)
				hm_begin(left);
		} else if (r < 5 && !hm.scope) {
			hm_begin(left);
		} else if (r < 7 && hm.scope) {
			/* a child scene opened or closed: a nested BeginInput, or
			 * the EndInput that resumes the outer scope */
			if (hm.depth > 1 && hm_rand() % 2)
				hm_end(left);
			else if (hm.depth < 4)
				hm_begin(left);
		}
		/* fewer moves while the button is held, so that a release finds
		 * the pressed parts under the cursor often enough */
		if (hm_rand() % 10 < (left ? 2 : 7)) {
			x = 70 + (int)(hm_rand() % 450);
			y = 70 + (int)(hm_rand() % 330);
		}
		/* A press is taken where the pointer is in the window only: SDL
		 * reports none outside it, and a press there (no 0x546890, the
		 * focus' own key table) is not followed here. A release may come
		 * outside (the button was held out of the window). */
		if (hm_rand() % 25 == 0)
			avail = !avail;
		if (hm_rand() % (left ? 3 : 8) == 0 && (left || avail))
			left = !left;
		/* a parts shown, hidden, made transparent or opaque */
		if (hm_rand() % 40 == 0) {
			int i = (int)(hm_rand() % HM_N);
			unsigned what = hm_rand() % 4;
			if (what == 0) { hm.b[i].show = !hm.b[i].show; PE_SetShow(hm.b[i].no, hm.b[i].show); }
			else if (what == 1) { hm.b[i].alpha = hm.b[i].alpha ? 0 : 255; PE_SetAlpha(hm.b[i].no, hm.b[i].alpha); }
			else if (what == 2) { hm.b[i].pass = !hm.b[i].pass; PE_SetPassCursor(hm.b[i].no, hm.b[i].pass); }
			else if (!left) {
				/* not while the button is held: the original's down state
				 * (+4) is set at the press by what was clickable then,
				 * the port asks for clickable now (not followed here) */
				hm.b[i].clickable = !hm.b[i].clickable;
				PE_SetClickable(hm.b[i].no, hm.b[i].clickable);
			}
		}
		int passed = (int)(hm_rand() % 40);
		mouse_focus = avail;
		struct he_frame got = he_update(x, y, left, passed);
		hm_update(x, y, left, avail, passed);
		struct he_frame want = hm.out;
		hm.out.n = 0;
		bool ok = got.n == want.n && got.n <= HE_MAXMSG;
		for (int i = 0; ok && i < got.n; i++)
			ok = hm_same_msg(&got.m[i], &want.m[i]);
		for (int i = 0; ok && i < HM_N; i++) {
			struct parts *p = parts_get(hm.b[i].no);
			int want = hm.e[i].state == 3 ? PARTS_STATE_CLICKED : hm.e[i].state == 2 ? PARTS_STATE_HOVERED : PARTS_STATE_DEFAULT;
			ok = p->state == want && p->is_hovered == hm.e[i].entered;
			if (!ok)
				printf("  FAIL: HE8 frame %d: parts %d state %d entered %d; model %d entered %d\n", frame, hm.b[i].no,
				       p->state, p->is_hovered, want, hm.e[i].entered);
		}
		int click = he_hll_int("GetClickNumber"), active = he_hll_int("GetActiveParts");
		/* 0 outside a scope: the port's choice (PE_GetActivePartsNumber) */
		int want_active = hm.scope && hm.focus >= 0 ? hm.b[hm.focus].no : 0;
		if (ok && (click != hm.clicked || active != want_active)) {
			ok = false;
			printf("  FAIL: HE8 frame %d: GetClickNumber %d GetActiveParts %d; model %d %d\n", frame, click, active,
			       hm.clicked, want_active);
		}
		if (!ok) {
			bad++;
			he_failures++;
			printf("  FAIL: HE8 frame %d at (%d,%d) left %d avail %d scope %d depth %d passed %d\n", frame, x, y, left, avail,
			       hm.scope, hm.depth, passed);
			he_print("engine", &got);
			he_print("model", &want);
			/* go on from the engine's state is not possible: stop */
			break;
		}
		nmsg += got.n;
		for (int i = 0; i < got.n && i < HE_MAXMSG; i++) {
			nenter += got.m[i].type == 0;
			nclick += got.m[i].type == 4 && got.m[i].no;
			nfocus += got.m[i].type == 19 || got.m[i].type == 20;
		}
	}
	printf("  HE8 the model: %d messages (%d MouseEnter, %d parts clicks, %d focus messages); %d LostFocus at a beginning, "
	       "%d nested BeginInput, %d focus kept entered over a boundary, %d releases outside with a pressed parts\n",
	       nmsg, nenter, nclick, nfocus, hm.n_begin_lost, hm.n_nested, hm.n_kept, hm.n_release_out);
	HE_CHECK(nenter > 100 && nclick >= 10 && nfocus > 30 && hm.n_begin_lost >= 5 && hm.n_nested >= 10 && hm.n_kept >= 5
		 && hm.n_release_out >= 3, "HE8 the run covered too little");
}

static int test_hover_events(void)
{
	void (*cases[])(void) = { he_one, he_several, he_outside, he_scopes, he_focus, he_v13, he_pass_getter, he_model, he_queue,
		he_cover };
	const char *names[] = { "HE1", "HE2", "HE3", "HE4", "HE5", "HE6", "HE7", "HE8", "HE9", "HE10" };
	int n = sizeof(cases) / sizeof(cases[0]), failed = 0;
	for (int i = 0; i < n; i++) {
		fflush(stdout);
		pid_t pid = fork();
		if (pid == 0) {
			he_failures = 0;
			cases[i]();
			fflush(stdout);
			_exit(he_failures ? 1 : 0);
		}
		int status = 0;
		waitpid(pid, &status, 0);
		bool ok = WIFEXITED(status) && WEXITSTATUS(status) == 0;
		if (!ok) {
			failed++;
			printf("%s FAIL (status %d)\n", names[i], status);
		}
	}
	printf("HOVER-EVENTS %s: %d of %d cases failed\n", failed ? "FAIL" : "PASS", failed, n);
	return failed ? 1 : 0;
}
