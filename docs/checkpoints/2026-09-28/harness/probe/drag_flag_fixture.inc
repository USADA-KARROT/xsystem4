/* drag-flag: the v14 pactex loader reads ドラッグ into the parts' movable
 * (draggable) flag, and the left drag the script consumes.
 *
 * Original (static disassembly of the CN executable; the English executable
 * has the same code at the same addresses with the SJIS key):
 *  - the component parser (0x553650, called with the block at parts +0x84,
 *    0x5390e1) reads the key at 0x55482a (CN: GBK string 0x7daf78; English:
 *    SJIS string 0x7c0194) with the default 0 (0x554837) through the getter
 *    0x418230 (English 0x418190), and stores "== 1" at block +0x122
 *    (0x554848..0x554855), i.e. parts +0x1a6. The block starts as 0
 *    (0x550a2f clears +0x120..+0x123). The store is unconditional;
 *  - Parts_SetDrag (case 0x57e222 -> 0x58fb20) writes the same byte
 *    (0x58fb66), Parts_GetPartsDrag reads it (0x58fbf8);
 *  - the input reads it for the left source's movement (0x546c6b) and for
 *    the left cancellation's restore (0x5462cf); events are sent whatever
 *    it is (see key-events).
 * The getter returns the default for a missing key and for a value that is
 * not a number (see click-permission).
 *
 * The consumer (AIN, the game's own code, run here unchanged):
 * PartsStateEventProvider@Attach with useDrag registers its KeyDown, KeyUp,
 * MouseLClick and the three left drag handlers through the IParts event
 * slots; CPartsMessageManager@Update dispatches the queue through
 * CallDelegate. OnKeyDown records the start position (Pos::get) while the
 * key is not yet down; OnDragStart raises Z to 1073741823 and sets
 * m_dragging; OnDragEnd restores Z and Pos and, for the modes 0, 1 and 2,
 * clears m_dragging and sets m_nextClickDisable; OnClick swallows the click
 * when that flag is set (and clears it); a lambda on the end-update event
 * list clears it at every frame end. So the release's DragEnd and the
 * parts' MouseClick must come in the same dispatch, DragEnd first.
 * InsertPositionView@Update (on the begin-update list while a card is
 * dragged) reads the cursor through AFL_Mouse_GetPos and tests its targets
 * with AFL_Parts_IsPointIn (HLL Parts_IsCursorIn; native 0x59b290 makes a
 * virtual call on the parts' component at 0x59b2fd, presumably its own hit
 * test; it looks at no Z order), then stores the swap or insert index it
 * found (InsertPositionView@Set). The lists' drag-end handlers read those two
 * values when the DragEnd is dispatched.
 *
 *   DG1 GBK key: rect, CG detection, CG, panel and construction components
 *       with ドラッグ 1, 0, no key and 2: only 1 is draggable; a string "1",
 *       -1 and 257 (low byte 1) are not; a nested component and a hidden one
 *       are read like the others; the key does not change the clickable or
 *       pass-cursor flags, and 點擊許可 / オン指針透過 do not make a parts
 *       draggable.
 *   DG2 SJIS key: the same integer matrix with the SJIS spelling; the
 *       string-valued case still uses the GBK key.
 *   DG3 A loaded component with ドラッグ 1 (GBK and SJIS spellings) follows
 *       the cursor in a left drag; its twin with 0 gets the same events and
 *       does not move. Per parts the raw queue has 16 at the press, 7 then
 *       8 (five integers) when the drag begins, and at the release 10
 *       (key 1; mode 1, as this port sends it with no drop target, see
 *       key-events), then its MouseClick, then 18. A component with
 *       ドラッグ 1 the script makes not clickable after the load (it does not
 *       pass the cursor, so the press still takes it) follows the cursor as
 *       well; its release has 10, then 18, and no MouseClick (only a
 *       clickable parts gets one, see click-permission).
 *   DG4 (guard) The script's Parts_SetDrag after the load wins both ways
 *       (the loader runs once, at the load), also over PE_Save / PE_Load;
 *       a tree without the key leaves every parts not draggable.
 *   DG5 (guard) Under an AIN before version 14 the loader does not read the
 *       key (either spelling).
 *   DG6 The left drag through the game's own consumer: a rect from
 *       AFL_Parts_CreateRect made draggable by CParts@Draggable::set, a
 *       PartsStateEventProvider attached with useDrag through the original
 *       Attach, the real v14 input, the original pump. The start position is
 *       the press-time one, Z is raised while dragging, the release restores
 *       Pos and Z, and the click of the release is swallowed in that same
 *       dispatch; a plain click is not swallowed.
 *   DG7 While a dragged card is held over a target (both shown, the card on
 *       top), the original AFL_Mouse_GetPos gives the cursor and
 *       AFL_Parts_IsPointIn answers true for the target under it and false
 *       for the other one, before any release.
 *   DG8 The residue while no DragEnd comes: the card's layer left with the
 *       drag held (another layer made active, the button released there):
 *       the provider stays dragging, the card on top where it was dragged;
 *       back in its layer, the kept DragEnd puts it back.
 *   DG9 InsertPositionView itself (its constructor, Start, Update on the
 *       begin-update list, End) over a synthetic activity holding the
 *       targets its Reset walks, while the DG6 card is dragged as the first
 *       of five: held on a swap target, past the five cards, on an insert
 *       target, on the dragged card's own hidden swap target and on no
 *       target, it holds the swap index, the insert index at the end, the
 *       insert index, none and none; released on a swap target, the
 *       dispatch that ends the drag finds that swap index set by the same
 *       frame's begin-update. After End the values stay.
 *
 * Not covered: the two lists' own drag-end handlers (the reorder, the
 * click or cancel sound); the drop target, the mode 0, right and middle
 * drags (no consumer in this game); a parts the loader is handed again
 * after loading a save (restored, not read again); a ドラッグ that is a
 * float (the original's getter truncates it to an integer, this loader
 * reads integers only; no component of the game has one).
 *
 * DG9 holds each target for two frames and checks the options after the
 * release frame; moving to a different target and releasing in one frame,
 * and observing the options inside a list's drag-end callback, are not
 * covered. A string-valued SJIS drag key is not covered.
 *
 * Synthetic data only; no assets, no script text. Each case runs in its own
 * fork (forked_cases.inc). DG1-DG5 use the probe's activity loader; DG6-DG9
 * run the AIN's own functions as key-consumer and controller-id do (DG9
 * takes its target names from InsertPositionView's own code).
 */

#define DG_DRAG "\xa5\xc9\xa5\xe9\xa5\xc3\xa5\xb0"		/* ドラッグ (GBK) */
#define DG_DRAG_SJIS "\x83\x68\x83\x89\x83\x62\x83\x4f"	/* ドラッグ (SJIS) */
#define DG_NONE (-1000)						/* the key is absent */
#define DG_TOP 1073741823

struct dg_attr {
	int x, y, z;
	int click, pass;	/* 點擊許可, オン指針透過: the value or DG_NONE */
	int gbk, sjis;		/* ドラッグ in either spelling: the value or DG_NONE */
	bool drag_string;	/* ドラッグ (GBK) = "1", a string */
	bool hidden;		/* 表示 = 0 */
};

/* name { 座標, [點擊許可], [オン指針透過], [ドラッグ], [ドラッグ SJIS], [表示 0], <info>, [子部件 x nkids] } */
static struct ex_tree dg_node_kids(const char *name, const struct dg_attr *a, struct ex_tree info, unsigned nkids)
{
	struct ex_tree t = bu_branch(name, 8);
	unsigned k = 0;
	t.children[k++] = lb_ilist(LB_POS, 3, (int[]){ a->x, a->y, a->z });
	if (a->click != DG_NONE)
		t.children[k++] = bu_ileaf(CP_CLICK, a->click);
	if (a->pass != DG_NONE)
		t.children[k++] = bu_ileaf(RV_PASS, a->pass);
	if (a->drag_string)
		t.children[k++] = bu_sleaf(DG_DRAG, "1");
	else if (a->gbk != DG_NONE)
		t.children[k++] = bu_ileaf(DG_DRAG, a->gbk);
	if (a->sjis != DG_NONE)
		t.children[k++] = bu_ileaf(DG_DRAG_SJIS, a->sjis);
	if (a->hidden)
		t.children[k++] = bu_ileaf(LB_SHOW, 0);
	t.children[k++] = info;
	if (nkids)
		t.children[k++] = bu_branch(BU_KIDS, nkids);
	t.nr_children = k;
	return t;
}
static struct ex_tree dg_node(const char *name, const struct dg_attr *a, struct ex_tree info)
{
	return dg_node_kids(name, a, info, 0);
}

static int dg_load(const char *act_name, struct ex *e)
{
	struct string *name = cstr_to_string(act_name);
	bool loaded = probe_load_activity_tree(name, e);
	free_string(name);
	ex_free(e);
	if (!loaded)
		xc_env_fail("the synthetic activity did not load");
	return sf_str(act_name, -1);
}
static int dg_no(int act, const char *name)
{
	int n = bu_number(act, name);
	XC_CHECK(n > 0, "%s has no parts number", name);
	return n;
}
static bool dg_flag(int no)
{
	struct parts *p = no > 0 ? parts_try_get(no) : NULL;
	return p && p->draggable;
}
static void dg_expect(int no, bool want, const char *what)
{
	if (no > 0)
		XC_CHECK(dg_flag(no) == want, "%s: draggable %d, want %d", what, dg_flag(no), want);
}

/* The 5 x 4 matrix of DG1/DG2 and the extra components, in one activity. */
static const char *dg_kinds[] = { "Rect", "Detect", "Cg", "Panel", "Cons" };
static const char *dg_suffix[] = { "1", "0", "None", "2" };
static const int dg_values[] = { 1, 0, DG_NONE, 2 };
static struct ex_tree dg_info(int kind)
{
	return kind == 0 ? cp_rect_info(40, 30) : kind == 1 ? cp_detector_info() : kind == 2 ? cp_cg_info("")
		: kind == 3 ? cp_panel_info(0, 0) : cp_area_info(0);
}

static void dg_matrix(bool sjis)
{
	bu_postlink();
	xc_ready();
	struct ex_tree *kids;
	struct ex *e = lb_activity("DragFlagLoader", 27, &kids);
	char name[32];
	for (int k = 0; k < 5; k++) {
		for (int v = 0; v < 4; v++) {
			struct dg_attr a = { 10 * v, 10 * k, 1, DG_NONE, DG_NONE, DG_NONE, DG_NONE };
			if (sjis)
				a.sjis = dg_values[v];
			else
				a.gbk = dg_values[v];
			snprintf(name, sizeof name, "%s%s", dg_kinds[k], dg_suffix[v]);
			kids->children[k * 4 + v] = dg_node(name, &a, dg_info(k));
		}
	}
	/* A string "1"; 點擊許可 and オン指針透過 without the key; the key with
	 * 點擊許可 0 and オン指針透過 0 (a rect stays not clickable, not passing). */
	struct dg_attr string = { 0, 100, 1, DG_NONE, DG_NONE, DG_NONE, DG_NONE, .drag_string = true };
	struct dg_attr others = { 0, 110, 1, 1, 1, DG_NONE, DG_NONE };
	struct dg_attr only = { 0, 120, 1, 0, 0, sjis ? DG_NONE : 1, sjis ? 1 : DG_NONE };
	kids->children[20] = dg_node("RectString", &string, cp_rect_info(40, 30));
	kids->children[21] = dg_node("ClickPass", &others, cp_rect_info(40, 30));
	kids->children[22] = dg_node("DragOnly", &only, cp_rect_info(40, 30));
	/* A component nested below a free box (marked 0) and a hidden one. */
	struct dg_attr box_at = { 0, 150, 1, 0, 0, sjis ? DG_NONE : 0, sjis ? 0 : DG_NONE };
	struct dg_attr nested = { 0, 0, 1, DG_NONE, DG_NONE, sjis ? DG_NONE : 1, sjis ? 1 : DG_NONE };
	kids->children[23] = dg_node_kids("Box", &box_at, lb_box_info(false, 0, 0, 200.0f, false, 1, 0, 0, 0, 0), 1);
	lb_kids(&kids->children[23])->children[0] = dg_node("Nested", &nested, cp_rect_info(40, 30));
	struct dg_attr hidden = { 0, 0, 63, DG_NONE, DG_NONE, sjis ? DG_NONE : 1, sjis ? 1 : DG_NONE, .hidden = true };
	kids->children[24] = dg_node("Hidden", &hidden, cp_rect_info(1280, 720));
	/* -1, and 257 whose low byte is 1: neither counts. */
	struct dg_attr other = { 0, 160, 1, DG_NONE, DG_NONE, sjis ? DG_NONE : -1, sjis ? -1 : DG_NONE };
	struct dg_attr big = { 0, 170, 1, DG_NONE, DG_NONE, sjis ? DG_NONE : 257, sjis ? 257 : DG_NONE };
	kids->children[25] = dg_node("Minus", &other, cp_rect_info(40, 30));
	kids->children[26] = dg_node("Big", &big, cp_rect_info(40, 30));
	int act = dg_load("DragFlagLoader", e);
	for (int k = 0; k < 5; k++) {
		for (int v = 0; v < 4; v++) {
			snprintf(name, sizeof name, "%s%s", dg_kinds[k], dg_suffix[v]);
			dg_expect(dg_no(act, name), dg_values[v] == 1, name);
		}
	}
	dg_expect(dg_no(act, "RectString"), false, "RectString (= \"1\")");
	dg_expect(dg_no(act, "Minus"), false, "Minus");
	dg_expect(dg_no(act, "Big"), false, "Big");
	int cp = dg_no(act, "ClickPass"), only_no = dg_no(act, "DragOnly");
	if (cp > 0) {
		struct parts *p = parts_get(cp);
		XC_CHECK(!p->draggable && p->clickable && p->pass_cursor,
			 "ClickPass (點擊許可 1, オン指針透過 1, no key): draggable %d clickable %d pass %d; want 0, 1, 1",
			 p->draggable, p->clickable, p->pass_cursor);
	}
	if (only_no > 0) {
		struct parts *p = parts_get(only_no);
		XC_CHECK(p->draggable && !p->clickable && !p->pass_cursor,
			 "DragOnly (ドラッグ 1, 點擊許可 0, オン指針透過 0): draggable %d clickable %d pass %d; want 1, 0, 0",
			 p->draggable, p->clickable, p->pass_cursor);
	}
	dg_expect(dg_no(act, "Box"), false, "Box (0)");
	dg_expect(dg_no(act, "Nested"), true, "Nested (1, below Box)");
	int hidden_no = dg_no(act, "Hidden");
	dg_expect(hidden_no, true, "Hidden (1, 表示 0)");
	if (hidden_no > 0)
		XC_CHECK(!parts_get(hidden_no)->local.show, "Hidden is shown");
	heap_unref(act);
}
static void dg1_gbk(void) { dg_matrix(false); }
static void dg2_sjis(void) { dg_matrix(true); }

/* --- the v14 input on loaded components (DG3, DG4) --- */

struct dg_msg { int type, no, n, v[5]; };
struct dg_frame { int n; struct dg_msg m[128]; };
static struct dg_frame dg_read(void)
{
	int (*count)(void) = (int (*)(void))br_pe_fn("GetMessageVariableCount");
	int (*var)(int) = (int (*)(int))br_pe_fn("GetMessageVariableInt");
	struct dg_frame f = { 0 };
	for (int t; (t = br_msg_type_raw()) >= 0; br_msg_pop()) {
		if (f.n < 128) {
			struct dg_msg *m = &f.m[f.n];
			*m = (struct dg_msg){ t, br_msg_parts(), count() };
			for (int j = 0; j < m->n && j < 5; j++)
				m->v[j] = var(j);
			f.n++;
		}
	}
	return f;
}
static struct dg_frame dg_frame(int x, int y, bool left)
{
	extern bool key_state[];
	key_state[VK_LBUTTON] = left;
	mouse_set_pos(x, y);
	PE_UpdateComponent(0);
	PE_UpdateInputState(16);
	return dg_read();
}
/* The types of parts no's messages in the frame, in order, into out. */
static int dg_types(const struct dg_frame *f, int no, int *out, int max)
{
	int n = 0;
	for (int i = 0; i < f->n; i++)
		if (f->m[i].no == no && n < max)
			out[n++] = f->m[i].type;
	return n;
}
static int dg_index(const int *t, int n, int type)
{
	for (int i = 0; i < n; i++)
		if (t[i] == type)
			return i;
	return -1;
}
static const struct dg_msg *dg_find(const struct dg_frame *f, int no, int type)
{
	for (int i = 0; i < f->n; i++)
		if (f->m[i].no == no && f->m[i].type == type)
			return &f->m[i];
	return NULL;
}

/* The multi-controller mode the game runs in (controller-id's setup, which
 * includes the input setup); PE_Save keeps the draggable flag only in that
 * mode (src/parts/save.c). Made before the load: it makes the parts table
 * anew. */
static void dg_input_env(void)
{
	he_env();
	ci_setup();
	keyboard_focus = true;
	key_clear_flag(false);
}

/* One left drag of a component at (x0, y0) by (dx, dy) in two moves; its
 * position along the way and the events, against the component's own
 * draggable flag (want_move) and whether the release on it is a click
 * (want_click: it is clickable). */
static void dg_drag_one(int no, const char *what, int x0, int y0, bool want_move, bool want_click)
{
	if (no <= 0)
		return;
	struct parts *p = parts_get(no);
	Point start = p->local.pos;
	int t[64], n;
	PE_BeginInput();
	dg_frame(640, 680, false);
	dg_frame(x0, y0, false);
	struct dg_frame f = dg_frame(x0, y0, true);
	n = dg_types(&f, no, t, 64);
	XC_CHECK(dg_index(t, n, 16) >= 0 && dg_index(t, n, 7) < 0, "%s press: KeyDown at %d, DragBegin at %d", what,
		 dg_index(t, n, 16), dg_index(t, n, 7));
	f = dg_frame(x0 + 20, y0 + 5, true);
	n = dg_types(&f, no, t, 64);
	int i7 = dg_index(t, n, 7), i8 = dg_index(t, n, 8);
	XC_CHECK(i7 >= 0 && i8 > i7, "%s first move: DragBegin at %d, Draging at %d; want both, Begin first", what, i7, i8);
	const struct dg_msg *d = dg_find(&f, no, 8);
	XC_CHECK(d && d->n == 5 && d->v[0] == x0 && d->v[1] == y0 && d->v[2] == x0 + 20 && d->v[3] == y0 + 5 && d->v[4] == 1,
		 "%s Draging payload n=%d (%d,%d,%d,%d,%d)", what, d ? d->n : -1, d ? d->v[0] : 0, d ? d->v[1] : 0,
		 d ? d->v[2] : 0, d ? d->v[3] : 0, d ? d->v[4] : 0);
	Point want = want_move ? (Point){ start.x + 20, start.y + 5 } : start;
	XC_CHECK(p->local.pos.x == want.x && p->local.pos.y == want.y, "%s after the first move at (%d,%d), want (%d,%d)%s",
		 what, p->local.pos.x, p->local.pos.y, want.x, want.y, want_move ? " (it does not follow the cursor)" : "");
	f = dg_frame(x0 + 60, y0 + 30, true);
	want = want_move ? (Point){ start.x + 60, start.y + 30 } : start;
	XC_CHECK(p->local.pos.x == want.x && p->local.pos.y == want.y, "%s after the second move at (%d,%d), want (%d,%d)",
		 what, p->local.pos.x, p->local.pos.y, want.x, want.y);
	/* Released where the moved component is (the follower is still under the cursor). */
	f = dg_frame(x0 + 60, y0 + 30, false);
	n = dg_types(&f, no, t, 64);
	int i10 = dg_index(t, n, 10), i4 = dg_index(t, n, 4), i18 = dg_index(t, n, 18);
	const struct dg_msg *e = dg_find(&f, no, 10);
	XC_CHECK(e && e->n == 2 && e->v[0] == 1 && e->v[1] == 1, "%s DragEnd n=%d (%d,%d), want (1,1)", what,
		 e ? e->n : -1, e ? e->v[0] : 0, e ? e->v[1] : 0);
	if (want_click)
		XC_CHECK(i10 >= 0 && i4 > i10 && i18 > i4, "%s release: DragEnd at %d, MouseClick at %d, KeyUp at %d; want this order",
			 what, i10, i4, i18);
	else
		XC_CHECK(i10 >= 0 && i4 < 0 && i18 > i10,
			 "%s release: DragEnd at %d, MouseClick at %d, KeyUp at %d; want DragEnd, then KeyUp, no MouseClick",
			 what, i10, i4, i18);
	PE_EndInput();
}

static void dg3_follow(void)
{
	/* The input setup makes the parts table anew: before the load. */
	dg_input_env();
	struct ex_tree *kids;
	struct ex *e = lb_activity("DragFlagInput", 4, &kids);
	struct dg_attr on = { 100, 100, 1, 1, DG_NONE, 1, DG_NONE };
	struct dg_attr on_s = { 100, 300, 1, 1, DG_NONE, DG_NONE, 1 };
	struct dg_attr off = { 700, 100, 1, 1, DG_NONE, 0, DG_NONE };
	/* 點擊許可 1, オン指針透過 0, ドラッグ 1; the script then makes it not
	 * clickable (as it does for a view it does not let be clicked). */
	struct dg_attr unclick = { 700, 300, 1, 1, 0, 1, DG_NONE };
	kids->children[0] = dg_node("On", &on, cp_rect_info(200, 80));
	kids->children[1] = dg_node("OnSjis", &on_s, cp_rect_info(200, 80));
	kids->children[2] = dg_node("Off", &off, cp_rect_info(200, 80));
	kids->children[3] = dg_node("Unclickable", &unclick, cp_rect_info(200, 80));
	int act = dg_load("DragFlagInput", e);
	int a = dg_no(act, "On"), b = dg_no(act, "OnSjis"), c = dg_no(act, "Off"), d = dg_no(act, "Unclickable");
	if (d > 0)
		PE_SetClickable(d, false);
	xc_ready();
	if (a > 0) cp_register_event(a, 3, 30);
	if (b > 0) cp_register_event(b, 4, 40);
	if (c > 0) cp_register_event(c, 5, 50);
	if (d > 0) cp_register_event(d, 6, 60);
	dg_drag_one(a, "On (ドラッグ 1)", 150, 140, true, true);
	dg_drag_one(b, "OnSjis (ドラッグ 1, SJIS)", 150, 340, true, true);
	dg_drag_one(c, "Off (ドラッグ 0)", 750, 140, false, true);
	dg_drag_one(d, "Unclickable (ドラッグ 1, made not clickable)", 750, 340, true, false);
	heap_unref(act);
}

static void dg4_script(void)
{
	dg_input_env();
	struct ex_tree *kids;
	struct ex *e = lb_activity("DragFlagScript", 4, &kids);
	struct dg_attr one = { 100, 100, 1, 1, DG_NONE, 1, DG_NONE };
	struct dg_attr zero = { 100, 300, 1, 1, DG_NONE, 0, DG_NONE };
	struct dg_attr none = { 700, 100, 1, 1, DG_NONE, DG_NONE, DG_NONE };
	struct dg_attr none2 = { 700, 300, 1, 1, DG_NONE, DG_NONE, DG_NONE };
	kids->children[0] = dg_node("One", &one, cp_rect_info(200, 80));
	kids->children[1] = dg_node("Zero", &zero, cp_rect_info(200, 80));
	kids->children[2] = dg_node("NoKey", &none, cp_rect_info(200, 80));
	kids->children[3] = dg_node("NoKey2", &none2, cp_detector_info());
	int act = dg_load("DragFlagScript", e);
	int a = dg_no(act, "One"), b = dg_no(act, "Zero"), c = dg_no(act, "NoKey"), d = dg_no(act, "NoKey2");
	xc_ready();
	dg_expect(c, false, "NoKey (no ドラッグ)");
	dg_expect(d, false, "NoKey2 (CG detection, no ドラッグ)");
	/* As WorkerView@0 does after the load (Parts_SetDrag through the
	 * declaration), both ways. */
	if (a > 0) BU_PE("Parts_SetDrag", BU_I(a), BU_I(0));
	if (b > 0) BU_PE("Parts_SetDrag", BU_I(b), BU_I(1));
	dg_expect(a, false, "One after Parts_SetDrag(false)");
	dg_expect(b, true, "Zero after Parts_SetDrag(true)");
	if (a > 0) cp_register_event(a, 3, 30);
	if (b > 0) cp_register_event(b, 4, 40);
	dg_drag_one(a, "One (script false)", 150, 140, false, true);
	dg_drag_one(b, "Zero (script true)", 150, 340, true, true);
	/* PE_Save / PE_Load keep the script's values (the loader does not run). */
	struct page *saved = NULL;
	XC_CHECK(PE_Save(&saved) && saved, "PE_Save failed");
	if (saved) {
		/* Loading has to bring every flag back from the page. */
		if (a > 0) PE_SetDrag(a, true);
		if (b > 0) PE_SetDrag(b, false);
		if (c > 0) PE_SetDrag(c, true);
		XC_CHECK(PE_Load(&saved), "PE_Load failed");
		PE_UpdateComponent(0);
		dg_expect(a, false, "One after PE_Save / PE_Load");
		dg_expect(b, true, "Zero after PE_Save / PE_Load");
		dg_expect(c, false, "NoKey after PE_Save / PE_Load");
	}
	heap_unref(act);
}

/* DG5: the same keys under an AIN before version 14. */
static void dg5_v13(void)
{
	bu_postlink();
	struct ex_tree *kids;
	struct ex *e = lb_activity("DragFlagV13", 4, &kids);
	struct dg_attr g = { 0, 0, 1, DG_NONE, DG_NONE, 1, DG_NONE };
	struct dg_attr s = { 0, 50, 1, DG_NONE, DG_NONE, DG_NONE, 1 };
	struct dg_attr gd = { 0, 100, 1, DG_NONE, DG_NONE, 1, DG_NONE };
	struct dg_attr sd = { 0, 150, 1, DG_NONE, DG_NONE, DG_NONE, 1 };
	kids->children[0] = dg_node("Gbk", &g, cp_rect_info(40, 30));
	kids->children[1] = dg_node("Sjis", &s, cp_rect_info(40, 30));
	kids->children[2] = dg_node("GbkDetect", &gd, cp_detector_info());
	kids->children[3] = dg_node("SjisDetect", &sd, cp_detector_info());
	xc_ready();
	const int version = ain->version;
	ain->version = 13;
	struct string *name = cstr_to_string("DragFlagV13");
	bool loaded = probe_load_activity_tree(name, e);
	free_string(name);
	ex_free(e);
	ain->version = version;
	XC_CHECK(loaded, "the v13 load failed");
	if (!loaded)
		return;
	int act = sf_str("DragFlagV13", -1);
	dg_expect(dg_no(act, "Gbk"), false, "v13 Gbk");
	dg_expect(dg_no(act, "Sjis"), false, "v13 Sjis");
	dg_expect(dg_no(act, "GbkDetect"), false, "v13 GbkDetect");
	dg_expect(dg_no(act, "SjisDetect"), false, "v13 SjisDetect");
	heap_unref(act);
}

/* --- the game's own consumer (DG6-DG8) --- */

static int dg_fn(const char *name)
{
	int f = ain_get_function(ain, (char *)name);
	if (f < 0) {
		printf("  ENVIRONMENT: function %s not declared\n", name);
		fflush(stdout);
		_exit(XC_ENV_EXIT);
	}
	return f;
}
/* A method call; copies up to two result slots into ret, returns their
 * count (a wrap of an interface leaves two). */
static int dg_method(int fno, int obj, int nargs, const int *args, union vm_value *ret)
{
	int sp = stack_ptr, csp = call_stack_ptr;
	size_t ip = instr_ptr;
	assert(ain->functions[fno].nr_args == nargs);
	stack_push(obj);
	for (int i = 0; i < nargs; i++)
		stack_push(args[i]);
	method_call(fno, VM_RETURN);
	vm_execute();
	instr_ptr = ip;
	if (call_stack_ptr != csp)
		xc_env_fail("a method call did not return");
	int n = stack_ptr - sp;
	for (int i = 0; i < n && i < 2; i++)
		ret[i] = stack[sp + i];
	stack_ptr = sp;
	return n;
}
/* A function call with one-slot arguments (as ci_call does); its first result slot. */
static int dg_call(int fno, int nargs, const int *args)
{
	int sp = stack_ptr, csp = call_stack_ptr;
	size_t ip = instr_ptr;
	for (int i = 0; i < nargs; i++)
		stack_push(args[i]);
	function_call(fno, VM_RETURN);
	vm_execute();
	instr_ptr = ip;
	assert(call_stack_ptr == csp);
	int r = stack_ptr > sp ? stack[sp].i : 0;
	stack_ptr = sp;
	return r;
}

struct dg_chain {
	int scene, box;		/* the scene's layer; the layer the card was made in (-1: the scene's) */
	int rect, rect_vt, core, core_vt, no;
	int provider, st;
	int m_dragging, m_down, m_disable, m_start, state;
	int sink;
	int f_begin, f_end, f_pump;
};

/* One frame as the game runs it: the begin-update events, the parts update
 * (the v14 input is inside it, see PartsEngine.UpdateComponent), the
 * end-update events, then the message pump (parts::detail::UpdateMessage). */
static void dg_game_frame(struct dg_chain *c, int x, int y, bool left)
{
	extern bool key_state[];
	dg_call(c->f_begin, 2, (int[]){ 16, 16 });
	key_state[VK_LBUTTON] = left;
	mouse_set_pos(x, y);
	PE_UpdateComponent(0);
	PE_UpdateInputState(16);
	dg_call(c->f_end, 2, (int[]){ 16, 16 });
	dg_call(c->f_pump, 0, NULL);
}

static float dg_posf(struct dg_chain *c, int member, int k)
{
	int s = heap[c->provider].page->values[member].i;
	return s > 0 && heap_index_valid(s) && heap[s].page && heap[s].page->nr_vars > k ? heap[s].page->values[k].f : -9999.0f;
}
static int dg_pm(struct dg_chain *c, int member) { return heap[c->provider].page->values[member].i; }

/* The game's setup of a draggable card with a provider (StandView@0 and
 * StandView@Draggable::set do the same calls on their CG detection parts). */
static void dg_chain_setup(struct dg_chain *c, int x, int y, int w, int h, bool in_box)
{
	he_env();
	ci_setup();
	c->scene = ci_active();
	c->box = -1;
	keyboard_focus = true;
	key_clear_flag(false);
	/* The v14 accessors the game's message functions are bound to at
	 * _PreLink (the probe links without it). */
	static const char *msg[] = { "GetMessageType", "GetMessagePartsNumber", "GetMessageDelegateIndex",
		"GetMessageVariableInt", "PopMessage", "ReleaseMessage" };
	for (unsigned i = 0; i < sizeof msg / sizeof *msg; i++)
		kc_override(msg[i], br_pe_fn(msg[i]));
	c->f_begin = dg_fn("parts::detail::CallBeginUpdateEvent");
	c->f_end = dg_fn("parts::detail::CallEndUpdateEvent");
	c->f_pump = dg_fn("parts::detail::UpdateMessage");
	PE_BeginInput();
	if (in_box) {
		/* A layer over the scene, as a screen opened on top makes one; the
		 * card is made in it. */
		c->box = ci_add(-1);
		if (c->box == c->scene || ci_active() != c->box)
			xc_env_fail("the layer was not added");
	}
	da_create_rect(&c->rect, &c->rect_vt);
	if (da_failures || !da_live_page(c->rect))
		xc_env_fail("AFL_Parts_CreateRect gave no rect");
	c->no = heap[c->rect].page->values[1].i;	/* CRectParts.number */
	union vm_value r[2] = {{ .i = -1 }, { .i = -1 }};
	int n = dg_method(dg_fn("parts::detail::CRectParts@Core::get"), c->rect, 0, NULL, r);
	if (n != 2 || !da_live_page(r[0].i) || c->no <= 0)
		xc_env_fail("CRectParts@Core::get gave no IParts");
	c->core = r[0].i;
	c->core_vt = r[1].i;
	br_box(c->no, x, y, w, h, true);
	/* IParts@Draggable::set(true), as StandView@Draggable::set does. */
	call_method_args(dg_fn("parts::detail::CParts@Draggable::set"), c->core, 1, (int[]){ 1 });
	c->st = ain_get_struct(ain, "PartsStateEventProvider");
	if (c->st < 0)
		xc_env_fail("PartsStateEventProvider missing");
	c->provider = vm_construct_struct(c->st);
	if (!da_live_page(c->provider))
		xc_env_fail("PartsStateEventProvider not constructed");
	c->m_dragging = br_member(c->st, "m_dragging");
	c->m_down = br_member(c->st, "m_isKeyDown");
	c->m_disable = br_member(c->st, "m_nextClickDisable");
	c->m_start = br_member(c->st, "m_dragStartPos");
	c->state = br_member(c->st, "<State>");
	/* The click event's subscriber: an empty void() of the AIN. */
	c->sink = kc_empty_sink(0);
	int click = br_member(c->st, "<OnClickEvent>");
	struct page *pp = heap[c->provider].page;
	if (pp->values[click].i > 0)
		heap_unref(pp->values[click].i);
	pp->values[click].i = heap_alloc_page(delegate_new_from_method(-1, c->sink));
	heap_ref(c->core);
	call_method_args(dg_fn("PartsStateEventProvider@Attach"), c->provider, 3, (int[]){ c->core, c->core_vt, 1 });
	if (parts_get(c->no)->delegate_index < 0)
		xc_env_fail("Attach gave the parts no event ID");
	if (in_box && parts_get(c->no)->controller_no != c->box)
		xc_env_fail("the card is not in the added layer");
	XC_CHECK(parts_get(c->no)->draggable, "CParts@Draggable::set(true) left the parts not draggable");
	dg_game_frame(c, 640, 680, false);
	dg_game_frame(c, 640, 680, false);
}

static void dg6_consumer(void)
{
	struct dg_chain c;
	dg_chain_setup(&c, 100, 100, 200, 80, false);
	xc_ready();
	struct parts *p = parts_get(c.no);
	int z0 = p->local.z;
	/* A plain click first: OnClickEvent fires once (the subscriber works). */
	unsigned long sink0 = calls[c.sink];
	dg_game_frame(&c, 150, 140, false);
	dg_game_frame(&c, 150, 140, true);
	dg_game_frame(&c, 150, 140, false);
	XC_CHECK(calls[c.sink] == sink0 + 1, "plain click: OnClickEvent fired %lu times, want 1", calls[c.sink] - sink0);
	dg_game_frame(&c, 150, 140, false);
	/* Press: KeyDown records the press-time position. */
	dg_game_frame(&c, 150, 140, true);
	XC_CHECK(dg_pm(&c, c.m_down) == 1 && dg_pm(&c, c.m_dragging) == 0 && dg_posf(&c, c.m_start, 0) == 100.0f
		 && dg_posf(&c, c.m_start, 1) == 100.0f,
		 "press: m_isKeyDown %d m_dragging %d m_dragStartPos (%.1f,%.1f); want 1, 0, (100,100)", dg_pm(&c, c.m_down),
		 dg_pm(&c, c.m_dragging), dg_posf(&c, c.m_start, 0), dg_posf(&c, c.m_start, 1));
	/* Past the threshold: DragBegin raises Z, the card follows. */
	dg_game_frame(&c, 170, 145, true);
	XC_CHECK(dg_pm(&c, c.m_dragging) == 1 && p->local.z == DG_TOP,
		 "drag begun: m_dragging %d, z %d; want 1, %d", dg_pm(&c, c.m_dragging), p->local.z, DG_TOP);
	dg_game_frame(&c, 250, 190, true);
	XC_CHECK(p->local.pos.x == 200 && p->local.pos.y == 150, "dragging: card at (%d,%d), want (200,150)",
		 p->local.pos.x, p->local.pos.y);
	XC_CHECK(dg_posf(&c, c.m_start, 0) == 100.0f && dg_posf(&c, c.m_start, 1) == 100.0f,
		 "held: m_dragStartPos moved to (%.1f,%.1f)", dg_posf(&c, c.m_start, 0), dg_posf(&c, c.m_start, 1));
	/* Release on the moved card: one dispatch with DragEnd, then the click. */
	unsigned long sink1 = calls[c.sink];
	dg_game_frame(&c, 250, 190, false);
	XC_CHECK(dg_pm(&c, c.m_dragging) == 0, "release: m_dragging %d, want 0", dg_pm(&c, c.m_dragging));
	XC_CHECK(p->local.pos.x == 100 && p->local.pos.y == 100 && p->local.z == z0,
		 "release: card at (%d,%d) z %d, want (100,100) z %d (OnDragEnd restores)", p->local.pos.x, p->local.pos.y,
		 p->local.z, z0);
	XC_CHECK(calls[c.sink] == sink1, "release: the click after the drag fired OnClickEvent %lu time(s), want 0",
		 calls[c.sink] - sink1);
	XC_CHECK(dg_pm(&c, c.m_disable) == 0,
		 "release: m_nextClickDisable %d after the dispatch, want 0 (cleared by the swallowed click of the same dispatch)",
		 dg_pm(&c, c.m_disable));
	XC_CHECK(dg_pm(&c, c.m_down) == 0, "release: m_isKeyDown %d, want 0", dg_pm(&c, c.m_down));
	/* The next plain click is not swallowed. */
	dg_game_frame(&c, 150, 140, false);
	unsigned long sink2 = calls[c.sink];
	dg_game_frame(&c, 150, 140, true);
	dg_game_frame(&c, 150, 140, false);
	XC_CHECK(calls[c.sink] == sink2 + 1, "click after the drag: OnClickEvent fired %lu times, want 1", calls[c.sink] - sink2);
}

/* DG7: the targets of InsertPositionView, as its pactex has them (rects
 * that are neither clickable nor passing the cursor, shown), under the
 * dragged card. */
static void dg7_targets(void)
{
	struct dg_chain c;
	dg_chain_setup(&c, 100, 100, 200, 80, false);
	int swap = 26101, insert = 26102;
	br_box(swap, 400, 100, 100, 200, true);
	br_box(insert, 520, 100, 100, 200, true);
	PE_SetClickable(swap, false);
	PE_SetClickable(insert, false);
	int f_mouse = dg_fn("AFL_Mouse_GetPos"), f_in = dg_fn("AFL_Parts_IsPointIn");
	/* AFL_Mouse_GetPos writes InsertPositionView@Update's own x and y
	 * (its variables 2 and 3): a local page of that function. */
	int f_update = dg_fn("InsertPositionView@Update");
	if (ain->functions[f_update].nr_vars < 4 || ain->functions[f_update].vars[2].type.data != AIN_INT
	    || ain->functions[f_update].vars[3].type.data != AIN_INT)
		xc_env_fail("InsertPositionView@Update has no int x, y at 2, 3");
	struct page *xy = alloc_page(LOCAL_PAGE, f_update, ain->functions[f_update].nr_vars);
	int xyslot = heap_alloc_page(xy);
	xc_ready();
	struct parts *p = parts_get(c.no);
	dg_game_frame(&c, 150, 140, false);
	dg_game_frame(&c, 150, 140, true);
	dg_game_frame(&c, 170, 145, true);
	XC_CHECK(dg_pm(&c, c.m_dragging) == 1 && p->local.z == DG_TOP, "drag begun: m_dragging %d z %d",
		 dg_pm(&c, c.m_dragging), p->local.z);
	static const struct { int x, y; bool on_swap, on_insert; } at[] = {
		{ 450, 200, true, false }, { 570, 150, false, true }, { 700, 200, false, false } };
	for (int i = 0; i < 3; i++) {
		dg_game_frame(&c, at[i].x, at[i].y, true);
		XC_CHECK(p->local.pos.x == 100 + at[i].x - 150 && p->local.pos.y == 100 + at[i].y - 140,
			 "held at (%d,%d): card at (%d,%d)", at[i].x, at[i].y, p->local.pos.x, p->local.pos.y);
		heap[xyslot].page->values[2].i = heap[xyslot].page->values[3].i = -1;
		int ok = dg_call(f_mouse, 4, (int[]){ xyslot, 2, xyslot, 3 });
		int mx = heap[xyslot].page->values[2].i, my = heap[xyslot].page->values[3].i;
		XC_CHECK(ok && mx == at[i].x && my == at[i].y, "held at (%d,%d): AFL_Mouse_GetPos %d (%d,%d)", at[i].x, at[i].y,
			 ok, mx, my);
		int in_s = dg_call(f_in, 4, (int[]){ swap, mx, my, 1 });
		int in_i = dg_call(f_in, 4, (int[]){ insert, mx, my, 1 });
		int in_c = dg_call(f_in, 4, (int[]){ c.no, mx, my, 1 });
		XC_CHECK(in_s == at[i].on_swap && in_i == at[i].on_insert && in_c,
			 "held at (%d,%d) under the card: AFL_Parts_IsPointIn swap %d insert %d card %d; want %d, %d, 1",
			 at[i].x, at[i].y, in_s, in_i, in_c, at[i].on_swap, at[i].on_insert);
		XC_CHECK(dg_pm(&c, c.m_dragging) == 1, "held at (%d,%d): the drag ended before the release", at[i].x, at[i].y);
	}
	dg_game_frame(&c, 700, 200, false);
	XC_CHECK(dg_pm(&c, c.m_dragging) == 0, "released: m_dragging %d", dg_pm(&c, c.m_dragging));
	/* The local page is left to the fork's exit. */
}

/* DG8 (the residue while no DragEnd comes): the card made in a layer over
 * the scene, its drag begun, then the scene's layer made active with the
 * left button held and the button released there. The card's layer does
 * not run while it is not active (G8b: each layer keeps its slot), so no
 * DragEnd reaches the card: the game's provider stays dragging, the card
 * keeps the raised Z and the place it was dragged to. Made active again,
 * the card's layer sends the kept DragEnd (mode 1, key-events KE94), and the
 * provider puts the card back. The original keeps a slot per layer too
 * (inferred from the disassembly, see layer-switch); a layer erased while
 * its card is held releases its parts with it here, so no card is left. */
static void dg8_residue(void)
{
	struct dg_chain c;
	dg_chain_setup(&c, 100, 100, 200, 80, true);
	xc_ready();
	struct parts *p = parts_get(c.no);
	int z0 = p->local.z;
	dg_game_frame(&c, 150, 140, false);
	dg_game_frame(&c, 150, 140, true);
	dg_game_frame(&c, 170, 145, true);
	dg_game_frame(&c, 200, 160, true);
	XC_CHECK(dg_pm(&c, c.m_dragging) == 1 && p->local.z == DG_TOP && p->local.pos.x == 150 && p->local.pos.y == 120,
		 "drag begun: m_dragging %d z %d at (%d,%d)", dg_pm(&c, c.m_dragging), p->local.z, p->local.pos.x, p->local.pos.y);
	ci_set_active(c.scene);
	dg_game_frame(&c, 200, 160, true);
	dg_game_frame(&c, 200, 160, false);
	dg_game_frame(&c, 200, 160, false);
	XC_CHECK(dg_pm(&c, c.m_dragging) == 1 && p->local.z == DG_TOP && p->local.pos.x == 150 && p->local.pos.y == 120,
		 "released in the scene's layer: m_dragging %d z %d at (%d,%d); want 1, %d, (150,120) (no DragEnd yet)",
		 dg_pm(&c, c.m_dragging), p->local.z, p->local.pos.x, p->local.pos.y, DG_TOP);
	ci_set_active(c.box);
	dg_game_frame(&c, 200, 160, false);
	XC_CHECK(dg_pm(&c, c.m_dragging) == 0 && p->local.z == z0 && p->local.pos.x == 100 && p->local.pos.y == 100,
		 "the card's layer active again: m_dragging %d z %d at (%d,%d); want 0, %d, (100,100)",
		 dg_pm(&c, c.m_dragging), p->local.z, p->local.pos.x, p->local.pos.y, z0);
}

/* --- DG9: InsertPositionView itself --- */

/* The k-th non-empty string constant (S_PUSH) of a function, or NULL. */
static const char *dg_code_string(int fno, int k)
{
	for (uint32_t ip = ain->functions[fno].address; ip + 6 <= ain->code_size;) {
		enum opcode op = get_opcode(ip);
		if (op == ENDFUNC)
			break;
		if (op == S_PUSH) {
			int idx = LittleEndian_getDW(ain->code, ip + 2);
			if (idx >= 0 && idx < ain->nr_strings && ain->strings[idx]->size > 0 && k-- == 0)
				return ain->strings[idx]->text;
		}
		ip += instruction_width(op);
	}
	return NULL;
}

/* The names InsertPositionView looks its parts up by: the prefixes of its
 * swap and insert targets (GetSwapParts, GetInsertParts) and its two
 * cursors (the first strings of Reset). */
static struct { const char *swap, *insert, *cursor[2]; } dg_ipv;

/* The targets: eleven swap and eleven insert rects (the indexes Reset
 * walks), 90 x 80 in two rows, neither clickable nor passing the cursor nor
 * draggable, shown, as InsertPositionView's own activity has them. */
#define DG_TARGETS 11
#define DG_TX(n) (40 + 100 * (n))
#define DG_SWAP_Y 400
#define DG_INSERT_Y 520

/* The reader InsertPositionView's AFL_Activity_Create reaches (whatever
 * file it names); the root has the pactex root name CActivityWrap@Load
 * looks up, so IActivity@Root::get is a real parts. */
static char dg_ipv_act[256];
static bool dg_read_targets(struct string *name, possibly_unused struct string *filename, possibly_unused bool edit)
{
	struct ex_tree *kids;
	struct ex *e = lb_activity(name->text, 2 + 2 * DG_TARGETS, &kids);
	struct ex_tree *root = &e->blocks[0].val.tree->children[0];
	free_string(root->name);
	root->name = cstr_to_string(AR_ROOT);
	snprintf(dg_ipv_act, sizeof dg_ipv_act, "%s", name->text);
	char buf[96];
	struct dg_attr cursor = { 0, 650, 2, 0, 0, 0, DG_NONE };
	kids->children[0] = dg_node(dg_ipv.cursor[0], &cursor, cp_rect_info(90, 80));
	kids->children[1] = dg_node(dg_ipv.cursor[1], &cursor, cp_rect_info(10, 80));
	for (int n = 0; n < DG_TARGETS; n++) {
		struct dg_attr s = { DG_TX(n), DG_SWAP_Y, 1, 0, 0, 0, DG_NONE };
		struct dg_attr i = { DG_TX(n), DG_INSERT_Y, 1, 0, 0, 0, DG_NONE };
		snprintf(buf, sizeof buf, "%s%d", dg_ipv.swap, n);
		kids->children[2 + n] = dg_node(buf, &s, cp_rect_info(90, 80));
		snprintf(buf, sizeof buf, "%s%d", dg_ipv.insert, n);
		kids->children[2 + DG_TARGETS + n] = dg_node(buf, &i, cp_rect_info(90, 80));
	}
	bool ok = probe_load_activity_tree(name, e);
	ex_free(e);
	return ok;
}

/* An option<int> member: (value, 0) holds the value, (-1, 1) is none (the
 * pairs InsertPositionView@Set is called with). */
struct dg_opt { int v, tag; };
static struct dg_opt dg_option(int obj, int member)
{
	struct page *p = heap[obj].page;
	return (struct dg_opt){ p->values[member].i, p->values[member + 1].i };
}
static bool dg_opt_is(struct dg_opt o, bool has, int v)
{
	return has ? o.tag == 0 && o.v == v : o.tag == 1 && o.v == -1;
}

static void dg9_insert_position(void)
{
	struct dg_chain c;
	dg_chain_setup(&c, 100, 100, 200, 80, false);
	int st = ain_get_struct(ain, "InsertPositionView");
	int st_wrap = ain_get_struct(ain, "activity::detail::CActivityWrap");
	if (st < 0 || st_wrap < 0)
		xc_env_fail("InsertPositionView or CActivityWrap missing");
	int f_ctor = dg_fn("InsertPositionView@0"), f_start = dg_fn("InsertPositionView@Start");
	int f_end = dg_fn("InsertPositionView@End"), f_reset = dg_fn("InsertPositionView@Reset");
	dg_ipv.swap = dg_code_string(dg_fn("InsertPositionView@GetSwapParts"), 0);
	dg_ipv.insert = dg_code_string(dg_fn("InsertPositionView@GetInsertParts"), 0);
	dg_ipv.cursor[0] = dg_code_string(f_reset, 0);
	dg_ipv.cursor[1] = dg_code_string(f_reset, 1);
	const char *walk_s = dg_code_string(f_reset, 2), *walk_i = dg_code_string(f_reset, 3);
	if (!dg_ipv.swap || !dg_ipv.insert || !dg_ipv.cursor[0] || !dg_ipv.cursor[1] || !walk_s || !walk_i
	    || strcmp(walk_s, dg_ipv.swap) || strcmp(walk_i, dg_ipv.insert))
		xc_env_fail("InsertPositionView's part names are not where they were looked for");
	/* After the input setup (it links the parts engine anew). */
	lr_replace_hll("PartsEngine", "ReadActivityFile", (void *)dg_read_targets);
	lr_replace_hll("MainEXFile", "Exists", (void *)lr_ex_exists);
	lr_replace_hll("MainEXFile", "AExists", (void *)lr_ex_aexists);
	lr_replace_hll("MainEXFile", "Type", (void *)lr_ex_type);
	lr_replace_hll("MainEXFile", "Int", (void *)lr_ex_int);
	lr_replace_hll("MainEXFile", "Float", (void *)lr_ex_float);
	lr_replace_hll("MainEXFile", "String", (void *)lr_ex_string);
	int g = ci_global("activity::detail::g_UserComponentManagerList");
	if (heap[global_page_slot].page->values[g].i < 0)
		heap[global_page_slot].page->values[g] = variable_initval(ain->globals[g].type.data);
	probe_step_budget = 50000000UL;
	/* NEW InsertPositionView(false), as the two lists make theirs. */
	int view = alloc_struct(st);
	union vm_value r[2];
	dg_method(f_ctor, view, 1, (int[]){ 0 }, r);
	int m_act = br_member(st, "m_act"), m_swap = br_member(st, "<Swap>"), m_insert = br_member(st, "<Insert>");
	int act = heap[view].page->values[m_act].i;
	if (!da_live_page(act) || heap[act].page->index != st_wrap)
		xc_env_fail("InsertPositionView@0 made no activity");
	int root = dg_ipv_act[0] ? bu_number(sf_str(dg_ipv_act, -1), AR_ROOT) : -1;
	if (root <= 0 || !parts_try_get(root))
		xc_env_fail("the activity InsertPositionView@0 made has no root parts");
	xc_ready();
	/* Reset (the constructor's last call) hides the root. */
	XC_CHECK(!parts_get(root)->local.show, "after the constructor the root is shown");
	struct parts *p = parts_get(c.no);
	dg_game_frame(&c, 150, 140, false);
	dg_game_frame(&c, 150, 140, true);
	dg_game_frame(&c, 170, 145, true);
	XC_CHECK(dg_pm(&c, c.m_dragging) == 1 && p->local.z == DG_TOP, "drag begun: m_dragging %d z %d",
		 dg_pm(&c, c.m_dragging), p->local.z);
	/* As a list's drag-start handler does: the dragged card is the first of five. */
	dg_method(f_start, view, 2, (int[]){ 0, 5 }, r);
	struct dg_opt s = dg_option(view, m_swap), in = dg_option(view, m_insert);
	XC_CHECK(dg_opt_is(s, false, 0) && dg_opt_is(in, false, 0), "after Start: swap (%d,%d) insert (%d,%d), want none, none",
		 s.v, s.tag, in.v, in.tag);
	XC_CHECK(parts_get(root)->local.show, "after Start the root is hidden");
	static const struct { const char *what; int x, y; bool has_s; int sv; bool has_i; int iv; } at[] = {
		{ "on swap target 3", DG_TX(3) + 45, DG_SWAP_Y + 40, true, 2, false, 0 },
		{ "on swap target 8 (past the five cards)", DG_TX(8) + 45, DG_SWAP_Y + 40, false, 0, true, 5 },
		{ "on insert target 3", DG_TX(3) + 45, DG_INSERT_Y + 40, false, 0, true, 3 },
		{ "on swap target 1 (the dragged card's own, hidden by Start)", DG_TX(1) + 45, DG_SWAP_Y + 40, false, 0, false, 0 },
		{ "on no target", 640, 300, false, 0, false, 0 },
		{ "on swap target 3 again", DG_TX(3) + 45, DG_SWAP_Y + 40, true, 2, false, 0 },
	};
	for (unsigned i = 0; i < sizeof at / sizeof *at; i++) {
		/* Update reads the cursor at the frame's begin-update: held two frames. */
		dg_game_frame(&c, at[i].x, at[i].y, true);
		dg_game_frame(&c, at[i].x, at[i].y, true);
		s = dg_option(view, m_swap);
		in = dg_option(view, m_insert);
		XC_CHECK(dg_opt_is(s, at[i].has_s, at[i].sv) && dg_opt_is(in, at[i].has_i, at[i].iv),
			 "held %s: swap (%d,%d) insert (%d,%d); want %s%d, %s%d (value, 0 = set; -1, 1 = none)", at[i].what,
			 s.v, s.tag, in.v, in.tag, at[i].has_s ? "" : "none ", at[i].sv, at[i].has_i ? "" : "none ", at[i].iv);
		XC_CHECK(dg_pm(&c, c.m_dragging) == 1, "held %s: the drag ended before the release", at[i].what);
	}
	/* Released there: the begin-update of this frame runs Update before the
	 * input queues the DragEnd, and the pump of the same frame dispatches it. */
	dg_game_frame(&c, DG_TX(3) + 45, DG_SWAP_Y + 40, false);
	s = dg_option(view, m_swap);
	in = dg_option(view, m_insert);
	XC_CHECK(dg_pm(&c, c.m_dragging) == 0 && p->local.pos.x == 100 && p->local.pos.y == 100,
		 "released: m_dragging %d card at (%d,%d); want 0, (100,100)", dg_pm(&c, c.m_dragging), p->local.pos.x,
		 p->local.pos.y);
	XC_CHECK(dg_opt_is(s, true, 2) && dg_opt_is(in, false, 0),
		 "released on swap target 3: swap (%d,%d) insert (%d,%d) at the DragEnd's dispatch; want 2, none", s.v, s.tag,
		 in.v, in.tag);
	/* End hides the root and takes Update off the begin-update list: the
	 * values stay. */
	dg_method(f_end, view, 0, NULL, r);
	XC_CHECK(!parts_get(root)->local.show, "after End the root is shown");
	dg_game_frame(&c, DG_TX(3) + 45, DG_INSERT_Y + 40, false);
	dg_game_frame(&c, DG_TX(3) + 45, DG_INSERT_Y + 40, false);
	s = dg_option(view, m_swap);
	in = dg_option(view, m_insert);
	XC_CHECK(dg_opt_is(s, true, 2) && dg_opt_is(in, false, 0),
		 "after End, on insert target 3: swap (%d,%d) insert (%d,%d); want 2, none (Update still runs)", s.v, s.tag,
		 in.v, in.tag);
	/* The view and its activity are left to the fork's exit. */
}

static int test_drag_flag(void)
{
	static const struct xc_case cases[] = {
		{ "DG1", dg1_gbk }, { "DG2", dg2_sjis }, { "DG3", dg3_follow }, { "DG4", dg4_script },
		{ "DG5", dg5_v13 }, { "DG6", dg6_consumer }, { "DG7", dg7_targets }, { "DG8", dg8_residue },
		{ "DG9", dg9_insert_position },
	};
	return xc_run("DRAG-FLAG", cases, sizeof cases / sizeof *cases, 60);
}
