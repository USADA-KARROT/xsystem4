/* click-permission: the v14 pactex loader reads 點擊許可 into the parts'
 * clickable flag, and a press on a parts that is clickable through that key
 * alone and has no event stays the whole-screen click.
 *
 * Original (static disassembly): the component parser (0x553650, called with
 * the block at parts +0x84) reads the key at 0x5547be with the default 0 and
 * stores "== 1" at block +0x120 (0x5547e9), i.e. parts +0x1a4, the flag
 * Parts_SetClickable (0x58f830) writes and the input target predicate
 * (0x546e20) reads. The getter (0x418230 -> 0x488a60) returns the default for
 * a missing key and for a value that is not a number; the field starts as 0
 * (0x550a2f). (A float counts as a number there; the loader here takes an
 * int only, and every value in the game's data is an int.) Every one of the
 * game's 2706 components has the key: 2668 are 0 and 38 are 1 (18 rects, 7
 * ＣＧ判定部件, 5 CG parts, 4 panels, 2 buttons, 1 text, 1 construction; all
 * with オン指針透過 0). A button widget makes its own inner parts clickable
 * (0x528690), so the 190 buttons marked 0 are clickable all the same.
 *
 *   CP1 Rect, construction, CG, text and panel components with 點擊許可 1, 0,
 *       no key and 2: only 1 is clickable. A string "1", クリップ許可 1 and
 *       オン指針透過 do not count; a nested component and one that is loaded
 *       hidden are read like the others; Parts_SetClickable still overrides.
 *   CP2 SceneWorkResult's shape: ClickGuard (z 606) under ClickTarget (z 607),
 *       both full-screen rects marked 1, the Footer's button above (z 608).
 *       ClickTarget, which the script gives an event, takes the press alone.
 *       Hidden, ClickGuard is the target: it has no event, so the press is
 *       the whole-screen click and nothing reaches the button under it.
 *   CP3 DungeonSelector's Area as the script draws it (alpha 1 where it can
 *       be clicked, 0 elsewhere, pixel decision) and the CG mode's side
 *       panel: marked 1 they take the press, marked 0 they only block the
 *       cursor from the button behind them. Needs GL.
 *   CP4 Guards, passing before the fix as well: which parts is clickable
 *       (buttons and ＣＧ判定部件 without the key, a button with 0) and which
 *       is the target (a pactex parts that only blocks the cursor, one that
 *       passes it), a disabled button, and v13 input.
 *   CP5 The flag survives PE_Save / PE_Load (loading does not run the pactex
 *       loader again), as does a script's Parts_SetClickable(false).
 *   CP6 SceneFeelEventResultDialog's shape, which closes on the whole-screen
 *       click alone: over PartySkillView's Button (a CG parts marked 1 that
 *       the script never touches) the press is still the whole-screen click.
 *       With an event, or made clickable by the script itself, a parts takes
 *       the press. The case asserts what the engine queues for the script
 *       and g_EndPartsBusyLoop; the script's own dispatch of that message
 *       (Update, CallDelegate, LClickEvent, Close) is not run here.
 *
 * What a press sends is xsystem4's present dispatch (src/parts/input.c), and
 * every press of this file except the v13 ones is checked against it by
 * cp_expect_press or cp_expect_swallowed (CP2 5, CP3 7, CP4 12, CP5 3,
 * CP6 7):
 *   - a clickable target with an event, or one made clickable by the script
 *     or by a button or ＣＧ判定部件: its MouseClick (type 4) on the press,
 *     nothing else queued, g_EndPartsBusyLoop left at 0;
 *   - a target that only blocks the cursor, a target clickable through 點擊
 *     許可 alone without an event, or no target: the whole-screen click
 *     (parts 0) and g_EndPartsBusyLoop set to 1;
 *   - a disabled button: nothing.
 * The original differs and that is not reproduced here: it sends on the
 * release; the whole-screen MouseClick goes out on every release whatever is
 * under the cursor (0x547c90 at 0x548064, into the manager's own emitter,
 * which masks other types but not 4: 0x54562a), before the target's own,
 * which is sent only when the target is clickable (0x5788d0); and the engine
 * does not write g_EndPartsBusyLoop. So the whole-screen click over
 * ClickGuard and over PartySkillView's Button is the original's result as
 * well (they also get a MouseClick nobody handles), while "nothing else
 * queued" for ClickTarget, Area and the buttons is not. When input.c follows
 * the original, the message counts, the order and g_EndPartsBusyLoop of all
 * those presses change with it, CP4's included. What does not change, and
 * what CP4 guards as behaviour that must stay, is the flag of each parts,
 * which parts is the target, and the v13 path.
 *
 * CP3 uses the construction mode's GL setup and fork (environment failures
 * give the mode rc 2); the others need no GL. Each case runs in its own
 * fork. Only functions and fields that exist before the fix are referenced.
 */

static int cp_failures;
#define CP_CHECK(c, ...) do { if (!(c)) { printf("  FAIL: "); printf(__VA_ARGS__); printf("\n"); cp_failures++; } } while (0)

#define CP_CLICK "\xfc\x63\x93\xf4\xd4\x53\xbf\xc9"				/* 點擊許可 */
#define CP_CLIP_OK "\xa5\xaf\xa5\xea\xa5\xc3\xa5\xd7\xd4\x53\xbf\xc9"		/* クリップ許可 */
#define CP_ALPHA "\xa5\xa2\xa5\xeb\xa5\xd5\xa5\xa1"				/* アルファ */
#define CP_PIXEL "\xca\xf3\x98\xcb\xd6\xb8\xe1\x98\xa5\xd4\xa5\xaf\xa5\xbb\xa5\xeb\xc5\xd0\xb6\xa8" /* 鼠標指針ピクセル判定 */
#define CP_PANEL "\xa5\xd1\xa5\xcd\xa5\xeb"					/* パネル */
#define CP_SIZE "\xa5\xb5\xa5\xa4\xa5\xba"					/* サイズ */
#define CP_COLOR "\xc9\xab"							/* 色 */
#define CP_NONE (-1)								/* the key is absent */

/* A component's own leaves. click and pass: the value, or CP_NONE. */
struct cp_attr {
	int x, y, z;
	int click, pass;
	int alpha;		/* アルファ; 0: no key (255) */
	bool pixel;		/* 鼠標指針ピクセル判定 = 1 */
	bool click_string;	/* 點擊許可 = "1", a string */
	bool hidden;		/* 表示 = 0 */
};

/* name { 座標, [點擊許可], [オン指針透過], [アルファ], [鼠標指針ピクセル判定],
 * [表示 0], <info>, [子部件 x nkids] } */
static struct ex_tree cp_node_kids(const char *name, const struct cp_attr *a, struct ex_tree info, unsigned nkids)
{
	struct ex_tree t = bu_branch(name, 8);
	unsigned k = 0;
	t.children[k++] = lb_ilist(LB_POS, 3, (int[]){ a->x, a->y, a->z });
	if (a->click_string)
		t.children[k++] = bu_sleaf(CP_CLICK, "1");
	else if (a->click != CP_NONE)
		t.children[k++] = bu_ileaf(CP_CLICK, a->click);
	if (a->pass != CP_NONE)
		t.children[k++] = bu_ileaf(RV_PASS, a->pass);
	if (a->alpha)
		t.children[k++] = bu_ileaf(CP_ALPHA, a->alpha);
	if (a->pixel)
		t.children[k++] = bu_ileaf(CP_PIXEL, 1);
	if (a->hidden)
		t.children[k++] = bu_ileaf(LB_SHOW, 0);
	t.children[k++] = info;
	if (nkids)
		t.children[k++] = bu_branch(BU_KIDS, nkids);
	t.nr_children = k;
	return t;
}
static struct ex_tree cp_node(const char *name, const struct cp_attr *a, struct ex_tree info)
{
	return cp_node_kids(name, a, info, 0);
}

/* 種類別情報 of a low-level rect of w x h. */
static struct ex_tree cp_rect_info(int w, int h)
{
	struct ex_tree info = bu_branch(BU_INFO, 2);
	info.children[0] = bu_sleaf(BU_TYPE, BU_LOW);
	info.children[1] = lb_rect_state(BU_NORMAL, w, h);
	return info;
}
/* A ＣＧ部件 state; cg "": without a CG. */
static struct ex_tree cp_cg_state(const char *state, const char *cg)
{
	struct ex_tree st = bu_branch(state, 3);
	st.children[0] = bu_sleaf(BU_TYPE, BU_CG);
	st.children[1] = lb_ilist(CN_AREA, 4, (int[]){ 0, 0, 0, 0 });
	st.children[2] = bu_sleaf(BR_CG_NAME, cg);
	return st;
}
/* DungeonSelector's Area: a construction normal state with an empty 手順リスト
 * (the script draws it), CG states without a CG for on-cursor and key-down. */
static struct ex_tree cp_area_info(int clip_ok)
{
	struct ex_tree info = bu_branch(BU_INFO, 5);
	info.children[0] = bu_sleaf(BU_TYPE, BU_LOW);
	info.children[1] = bu_ileaf(CP_CLIP_OK, clip_ok);
	info.children[2] = bu_branch(BU_NORMAL, 3);
	struct ex_tree *st = &info.children[2];
	st->children[0] = bu_sleaf(BU_TYPE, BR_CONS);
	st->children[1] = lb_ilist(CN_AREA, 4, (int[]){ 0, 0, 0, 0 });
	st->children[2] = bu_branch(BR_STEPS, 0);
	info.children[3] = cp_cg_state(BU_ONCURSOR, "");
	info.children[4] = cp_cg_state(TD_DOWN, "");
	return info;
}
/* A CG parts; cg "": none. PartySkillView's Button has a CG in its normal
 * state only (it does not exist here: no assets are loaded). */
static struct ex_tree cp_cg_info(const char *cg)
{
	struct ex_tree info = bu_branch(BU_INFO, 5);
	info.children[0] = bu_sleaf(BU_TYPE, BU_LOW);
	info.children[1] = bu_ileaf(CP_CLIP_OK, 0);
	info.children[2] = cp_cg_state(BU_NORMAL, cg);
	info.children[3] = cp_cg_state(BU_ONCURSOR, "");
	info.children[4] = cp_cg_state(TD_DOWN, "");
	return info;
}
/* A text state without 文本: nothing is laid out. */
static struct ex_tree cp_text_info(void)
{
	struct ex_tree info = bu_branch(BU_INFO, 2);
	info.children[0] = bu_sleaf(BU_TYPE, BU_LOW);
	info.children[1] = td_state(BU_NORMAL, 0);
	return info;
}
/* パネル { サイズ, 色 }: opaque black. The loader builds it unless it is 0x0. */
static struct ex_tree cp_panel_info(int w, int h)
{
	struct ex_tree info = bu_branch(BU_INFO, 3);
	info.children[0] = bu_sleaf(BU_TYPE, CP_PANEL);
	info.children[1] = lb_ilist(CP_SIZE, 2, (int[]){ w, h });
	info.children[2] = lb_ilist(CP_COLOR, 4, (int[]){ 0, 0, 0, 255 });
	return info;
}
/* 按鈕 with a ＣＧ名 (the CG does not exist). */
static struct ex_tree cp_button_info(void)
{
	struct ex_tree info = bu_branch(BU_INFO, 2);
	info.children[0] = bu_sleaf(BU_TYPE, BU_BUTTON);
	info.children[1] = bu_sleaf(BR_CG_NAME, "ClickPermissionMissingButton");
	return info;
}
/* FooterButton's Detector: a ＣＧ判定部件 normal state, a CG on-cursor state. */
static struct ex_tree cp_detector_info(void)
{
	struct ex_tree info = bu_branch(BU_INFO, 3);
	info.children[0] = bu_sleaf(BU_TYPE, BU_LOW);
	info.children[1] = bu_branch(BU_NORMAL, 1);
	info.children[1].children[0] = bu_sleaf(BU_TYPE, BU_CGDETECT);
	info.children[2] = bu_branch(BU_ONCURSOR, 1);
	info.children[2].children[0] = bu_sleaf(BU_TYPE, BU_CG);
	return info;
}

static int cp_load(const char *act_name, struct ex *e)
{
	struct string *name = cstr_to_string(act_name);
	bool loaded = probe_load_activity_tree(name, e);
	free_string(name);
	ex_free(e);
	CP_CHECK(loaded, "%s did not load", act_name);
	return sf_str(act_name, -1);
}
static int cp_no(int act, const char *name)
{
	int n = bu_number(act, name);
	CP_CHECK(n > 0, "%s has no parts number", name);
	return n;
}
/* Parts_GetPartsClickable through its AIN declaration. */
static bool cp_clickable(int no)
{
	return no > 0 && BU_PE("Parts_GetPartsClickable", BU_I(no)).i;
}
static void cp_expect_flag(int no, bool want, const char *what)
{
	if (no > 0)
		CP_CHECK(cp_clickable(no) == want, "%s: Parts_GetPartsClickable = %d, want %d", what, cp_clickable(no), want);
}
/* A component without a CG has no size: the hit area its CG would give. */
static void cp_hit_area(int no, int w, int h)
{
	if (no > 0)
		PE_SetPartsRectangleDetectionSize(no, w, h, 1);
}

/* GetMessageUniqueID and SetEventID are v14 functions registered at
 * _PostLink: called through their AIN declarations. The replaced queue
 * accessors come from the export table (see br_input_setup). */
static int cp_message_unique(void)
{
	union vm_value r[1] = { BU_I(-999) };
	br_call("PartsEngine", "GetMessageUniqueID", 0, 0, 0, NULL, r, 1);
	return r[0].i;
}
/* What the script's first XxxEvent::add for a parts does on the engine's
 * side: CPartsMessageManager@GetFunctionSet allocates the index and stores
 * it with SetEventID. Nothing else gives a parts a delegate index. */
static void cp_register_event(int no, int delegate, int unique)
{
	if (no > 0)
		BU_PE("SetEventID", BU_I(no), BU_I(delegate), BU_I(unique));
}

/* One press at (x, y) after a released frame: the queued messages, the
 * first one's fields and g_EndPartsBusyLoop (cleared before the press). */
struct cp_press {
	int n, type, no, delegate, unique, x, y, key, busy;
};
static struct cp_press cp_press(int x, int y)
{
	extern bool key_state[];
	int (*delegate)(void) = (int (*)(void))br_pe_fn("GetMessageDelegateIndex");
	int (*var)(int) = (int (*)(int))br_pe_fn("GetMessageVariableInt");
	assert(delegate && var);
	heap[global_page_slot].page->values[2].i = 0;
	mouse_set_pos(x, y);
	PE_UpdateComponent(0);
	PE_BeginInput();
	key_state[VK_LBUTTON] = false;
	PE_UpdateInputState(0);
	key_state[VK_LBUTTON] = true;
	PE_UpdateInputState(0);
	key_state[VK_LBUTTON] = false;
	PE_UpdateInputState(0);
	struct cp_press m = { .type = -1, .no = -1 };
	for (int t; (t = br_msg_type()) >= 0; br_msg_pop()) {
		if (!m.n++) {
			m.type = t;
			m.no = br_msg_parts();
			m.delegate = delegate();
			m.unique = cp_message_unique();
			m.x = var(0);
			m.y = var(1);
			m.key = var(2);
		}
	}
	PE_EndInput();
	m.busy = heap[global_page_slot].page->values[2].i;
	return m;
}
/* The shape of the present dispatch is defined by the next two functions and
 * nowhere else in this file (see the head comment for what the original
 * sends instead). The press went to one parts (no > 0): one MouseClick for
 * it, g_EndPartsBusyLoop 0. Or it is the whole-screen click (no 0): one
 * MouseClick for parts 0 without an event ID, g_EndPartsBusyLoop 1. */
static struct cp_press cp_expect_press(int x, int y, int no, const char *what)
{
	struct cp_press m = cp_press(x, y);
	int busy = no ? 0 : 1;
	CP_CHECK(m.n == 1 && m.type == 4 && m.no == no && m.busy == busy && m.x == x && m.y == y && m.key == 1
		 && (no || (m.delegate == -1 && m.unique == -1)),
		 "%s: %d messages, first type %d parts %d delegate %d unique %d (%d,%d,%d), g_EndPartsBusyLoop %d; want 1, 4, %d%s (%d,%d,1), %d",
		 what, m.n, m.type, m.no, m.delegate, m.unique, m.x, m.y, m.key, m.busy, no, no ? "" : " -1 -1", x, y, busy);
	return m;
}
/* A disabled button under the cursor: nothing is sent. */
static void cp_expect_swallowed(int x, int y, const char *what)
{
	struct cp_press m = cp_press(x, y);
	CP_CHECK(m.n == 0 && m.busy == 0, "%s: %d messages (first type %d parts %d), g_EndPartsBusyLoop %d; want none and 0",
		 what, m.n, m.type, m.no, m.busy);
}
/* The one hover target at (x, y), or none (NULL), among the given parts. */
static void cp_expect_hover(int x, int y, struct parts *want, struct parts **all, int n, const char *what)
{
	PE_BeginInput();
	rv_hover(x, y);
	for (int i = 0; i < n; i++)
		CP_CHECK(all[i]->is_hovered == (all[i] == want), "%s: parts %d hovered %d, want %d", what, all[i]->no,
			 all[i]->is_hovered, all[i] == want);
	PE_EndInput();
}

/* --- CP1 --- */

static void cp_loader(void)
{
	bu_postlink();
	static const char *kinds[] = { "Rect", "Cons", "Cg", "Text", "Panel" };
	static const char *suffix[] = { "1", "0", "None", "2" };
	static const int values[] = { 1, 0, CP_NONE, 2 };
	struct ex_tree *kids;
	struct ex *e = lb_activity("ClickPermissionLoader", 27, &kids);
	char name[32];
	for (int k = 0; k < 5; k++) {
		for (int v = 0; v < 4; v++) {
			struct cp_attr a = { 10 * v, 10 * k, 1, values[v], CP_NONE };
			struct ex_tree info = k == 0 ? cp_rect_info(40, 30) : k == 1 ? cp_area_info(0)
				: k == 2 ? cp_cg_info("") : k == 3 ? cp_text_info() : cp_panel_info(0, 0);
			snprintf(name, sizeof name, "%s%s", kinds[k], suffix[v]);
			kids->children[k * 4 + v] = cp_node(name, &a, info);
		}
	}
	/* Values and keys that are not 點擊許可 1. */
	struct cp_attr string = { 0, 100, 1, CP_NONE, CP_NONE, .click_string = true };
	struct cp_attr zero = { 0, 110, 1, 0, CP_NONE };
	struct cp_attr pass_only = { 0, 120, 1, 0, 1 }, both = { 0, 130, 1, 1, 1 }, click_only = { 0, 140, 1, 1, 0 };
	kids->children[20] = cp_node("RectString", &string, cp_rect_info(40, 30));
	kids->children[21] = cp_node("ConsClipOk", &zero, cp_area_info(1));
	kids->children[22] = cp_node("PassOnly", &pass_only, cp_rect_info(40, 30));
	kids->children[23] = cp_node("PassAndClick", &both, cp_rect_info(40, 30));
	kids->children[24] = cp_node("ClickOnly", &click_only, cp_rect_info(40, 30));
	/* The CG mode's UiBase/Left: a panel marked 1 below a free box marked 0
	 * (5 of the 38 are not direct children of their root). */
	struct cp_attr base_at = { 0, 150, 1, 0, 0 }, one = { 0, 0, 1, 1, 0 };
	kids->children[25] = cp_node_kids("UiBase", &base_at, lb_box_info(false, 0, 0, 200.0f, false, 1, 0, 0, 0, 0), 1);
	lb_kids(&kids->children[25])->children[0] = cp_node("NestedLeft", &one, cp_panel_info(0, 0));
	/* SceneSendResult's ClickTarget: 表示 0. */
	struct cp_attr hidden_at = { 0, 0, 63, 1, 0, .hidden = true };
	kids->children[26] = cp_node("HiddenTarget", &hidden_at, cp_rect_info(1280, 720));
	int act = cp_load("ClickPermissionLoader", e);
	int rect1 = 0, rect0 = 0;
	for (int k = 0; k < 5; k++) {
		for (int v = 0; v < 4; v++) {
			snprintf(name, sizeof name, "%s%s", kinds[k], suffix[v]);
			int no = cp_no(act, name);
			cp_expect_flag(no, values[v] == 1, name);
			if (k == 0 && v == 0)
				rect1 = no;
			if (k == 0 && v == 1)
				rect0 = no;
		}
	}
	cp_expect_flag(cp_no(act, "RectString"), false, "RectString (點擊許可 = \"1\")");
	cp_expect_flag(cp_no(act, "ConsClipOk"), false, "ConsClipOk (クリップ許可 1, 點擊許可 0)");
	static const struct { const char *name; bool click, pass; } flags[] = {
		{ "PassOnly", false, true }, { "PassAndClick", true, true }, { "ClickOnly", true, false } };
	for (int i = 0; i < 3; i++) {
		int no = cp_no(act, flags[i].name);
		if (no <= 0)
			continue;
		bool pass = BU_PE("Parts_GetPartsPassCursor", BU_I(no)).i;
		CP_CHECK(cp_clickable(no) == flags[i].click && pass == flags[i].pass, "%s: clickable %d, pass cursor %d; want %d, %d",
			 flags[i].name, cp_clickable(no), pass, flags[i].click, flags[i].pass);
	}
	cp_expect_flag(cp_no(act, "UiBase"), false, "UiBase");
	cp_expect_flag(cp_no(act, "NestedLeft"), true, "NestedLeft (below UiBase)");
	int hidden = cp_no(act, "HiddenTarget");
	cp_expect_flag(hidden, true, "HiddenTarget (表示 0)");
	if (hidden > 0)
		CP_CHECK(!parts_get(hidden)->local.show, "HiddenTarget is shown");
	heap_unref(act);
	/* The script's setter overrides the loaded value, both ways. */
	if (rect1 > 0 && rect0 > 0) {
		BU_PE("Parts_SetClickable", BU_I(rect1), BU_I(0));
		BU_PE("Parts_SetClickable", BU_I(rect0), BU_I(1));
		CP_CHECK(!cp_clickable(rect1) && cp_clickable(rect0), "after Parts_SetClickable(Rect1, false) and (Rect0, true): %d, %d",
			 cp_clickable(rect1), cp_clickable(rect0));
	}
	printf("CP1 pactex 點擊許可: %s\n", cp_failures ? "FAIL" : "ok");
}

/* --- CP2 --- */

static void cp_result(void)
{
	br_input_setup();
	struct ex_tree *kids;
	struct ex *e = lb_activity("ClickPermissionResult", 2, &kids);
	struct cp_attr guard_at = { 0, 0, 606, 1, 0 }, target_at = { 0, 0, 607, 1, 0 };
	kids->children[0] = cp_node("ClickGuard", &guard_at, cp_rect_info(1280, 720));
	kids->children[1] = cp_node("ClickTarget", &target_at, cp_rect_info(1280, 720));
	int act = cp_load("ClickPermissionResult", e);
	int guard = cp_no(act, "ClickGuard"), target = cp_no(act, "ClickTarget");
	heap_unref(act);
	if (guard <= 0 || target <= 0)
		return;
	/* A button under both (z 10) and one in the Footer's place: in the game
	 * the three are z 606, 607 and 608 below one root, so one above
	 * ClickTarget. */
	const int UNDER = 9931, FOOTER = 9932;
	struct parts *under = br_box(UNDER, 500, 300, 200, 80, true);
	struct parts *footer = br_box(FOOTER, 32, 652, 142, 50, true);
	PE_SetZ(UNDER, 10);
	PE_UpdateComponent(0);
	struct parts *g = parts_get(guard), *t = parts_get(target);
	PE_SetZ(FOOTER, t->global.z + 1);
	PE_UpdateComponent(0);
	CP_CHECK(under->global.z < g->global.z && g->global.z < t->global.z && t->global.z < footer->global.z,
		 "z order: under %d, ClickGuard %d, ClickTarget %d, footer %d", under->global.z, g->global.z, t->global.z,
		 footer->global.z);
	cp_expect_flag(guard, true, "ClickGuard");
	cp_expect_flag(target, true, "ClickTarget");
	/* SceneWorkResult@0 adds a MouseLClickEvent to ClickTarget; no script
	 * refers to ClickGuard. */
	cp_register_event(target, 7, 70);
	struct parts *all[] = { under, g, t, footer };
	struct cp_press m = cp_expect_press(600, 340, target, "ClickTarget");
	CP_CHECK(m.delegate == 7 && m.unique == 70, "the press on ClickTarget carries delegate %d unique %d, want 7, 70", m.delegate, m.unique);
	cp_expect_hover(600, 340, t, all, 4, "over ClickTarget");
	cp_expect_press(100, 676, FOOTER, "the footer button above ClickTarget");
	/* ClickTarget hidden: ClickGuard is the target and keeps the press from
	 * the button under it. It has no event, so the press is the whole-screen
	 * click (the original sends that one for every release, and ClickGuard a
	 * MouseClick the script drops). */
	PE_SetShow(target, false);
	cp_expect_hover(600, 340, g, all, 4, "over ClickGuard");
	cp_expect_press(600, 340, 0, "ClickGuard, which has no event");
	PE_SetShow(guard, false);
	cp_expect_press(600, 340, UNDER, "the button once both are hidden");
	cp_expect_press(640, 600, 0, "beside every parts");
	printf("CP2 ClickTarget over ClickGuard: %s\n", cp_failures ? "FAIL" : "ok");
}

/* --- CP3 --- */

/* The surface DungeonSelector@BuildAreaParts makes in the original:
 * Create(160, 80), Fill(255, 128, 0, 0) over the whole of it, then
 * Fill(.., 0, 0, 0, 1) over the place name's box (here 96x16, centred on
 * (80, 22)) and over (58, 30, 44, 44). The script's Fill is command 6, which
 * replaces RGBA (0x4fc490, as in a pactex 手順リスト): where the place can be
 * clicked the alpha is 1, not 255, and 0 everywhere else.
 *
 * This is not what the GUI has today: PartsEngine.AddPartsConstructionProcess
 * hands the script's command 6 to the blending fill (FillAlphaColor), which
 * with an alpha of 0 or 1 leaves Create's opaque black, so the whole 160x80
 * takes the click there. The case draws the original's surface and pins the
 * pixel decision at an alpha of 1. */
static void cp_draw_area(int no)
{
	if (no <= 0)
		return;
	PE_AddCreateToPartsConstructionProcess(no, 160, 80, 1);
	PE_AddFillWithAlphaToPartsConstructionProcess(no, 0, 0, 160, 80, 255, 128, 0, 0, 1);
	PE_AddFillWithAlphaToPartsConstructionProcess(no, 32, 14, 96, 16, 0, 0, 0, 1, 1);
	PE_AddFillWithAlphaToPartsConstructionProcess(no, 58, 30, 44, 44, 0, 0, 0, 1, 1);
	PE_BuildPartsConstructionProcess(no, 1);
}
static void cp_expect_built(int no, int w, int h, const char *what)
{
	struct parts *p = no > 0 ? parts_try_get(no) : NULL;
	struct parts_state *s = p ? &p->states[0] : NULL;
	CP_CHECK(s && s->type == PARTS_CONSTRUCTION_PROCESS && s->common.texture.handle && s->common.w == w && s->common.h == h,
		 "%s: state type %d, texture %s, size %dx%d; want a built %dx%d construction", what, s ? (int)s->type : -1,
		 s && s->common.texture.handle ? "present" : "missing", s ? s->common.w : 0, s ? s->common.h : 0, w, h);
}

static void cp_area(void)
{
	cn_gl_setup();
	br_input_setup();
	struct ex_tree *kids;
	struct ex *e = lb_activity("ClickPermissionArea", 4, &kids);
	/* Area: z 7, アルファ 128, pixel decision. Left and Right: z 1 and 3. */
	struct cp_attr area_at = { 800, 250, 7, 1, 0, .alpha = 128, .pixel = true };
	struct cp_attr off_at = { 300, 250, 7, 0, 0, .alpha = 128, .pixel = true };
	struct cp_attr left_at = { 0, 0, 1, 1, 0 }, right_at = { 1204, 0, 3, 0, 0 };
	kids->children[0] = cp_node("Area", &area_at, cp_area_info(0));
	kids->children[1] = cp_node("AreaOff", &off_at, cp_area_info(0));
	kids->children[2] = cp_node("Left", &left_at, cp_panel_info(80, 720));
	kids->children[3] = cp_node("RightOff", &right_at, cp_panel_info(80, 720));
	int act = cp_load("ClickPermissionArea", e);
	int area = cp_no(act, "Area"), off = cp_no(act, "AreaOff"), left = cp_no(act, "Left"), right = cp_no(act, "RightOff");
	heap_unref(act);
	struct parts *p = area > 0 ? parts_try_get(area) : NULL;
	if (!p || off <= 0 || left <= 0 || right <= 0)
		return;
	CP_CHECK(p->states[0].type == PARTS_CONSTRUCTION_PROCESS && !p->states[0].common.texture.handle && p->pixel_hittest
		 && p->local.alpha == 128 && !p->pass_cursor,
		 "Area as loaded: state type %d, texture %s, pixel decision %d, alpha %d, pass cursor %d; want an empty construction, 1, 128, 0",
		 p->states[0].type, p->states[0].common.texture.handle ? "present" : "missing", p->pixel_hittest, p->local.alpha,
		 p->pass_cursor);
	cp_expect_flag(area, true, "Area");
	cp_expect_flag(off, false, "AreaOff");
	cp_expect_flag(left, true, "Left");
	cp_expect_flag(right, false, "RightOff");
	/* DungeonSelector@0 and SceneCgModeDetail@0 add a MouseLClickEvent. */
	cp_register_event(area, 7, 70);
	cp_register_event(left, 8, 80);
	cp_draw_area(area);
	cp_draw_area(off);
	/* A button behind each of them (below their z). */
	const int UNDER_AREA = 9961, UNDER_OFF = 9962, UNDER_RIGHT = 9963;
	struct parts *under[3] = { br_box(UNDER_AREA, 790, 240, 180, 100, true), br_box(UNDER_OFF, 290, 240, 180, 100, true),
		br_box(UNDER_RIGHT, 1204, 300, 76, 120, true) };
	PE_UpdateComponent(0);
	cp_expect_built(area, 160, 80, "Area drawn by the script");
	cp_expect_built(off, 160, 80, "AreaOff drawn by the script");
	cp_expect_built(left, 80, 720, "Left");
	cp_expect_built(right, 80, 720, "RightOff");
	struct parts *a = parts_get(area), *o = parts_get(off), *r = parts_get(right);
	CP_CHECK(under[0]->global.z < a->global.z && under[1]->global.z < o->global.z && under[2]->global.z < r->global.z,
		 "z order: buttons %d %d %d behind Area %d, AreaOff %d, RightOff %d", under[0]->global.z, under[1]->global.z,
		 under[2]->global.z, a->global.z, o->global.z, r->global.z);
	uint8_t *px = cn_pixels(area, 160, 80, "Area");
	CP_CHECK(px, "Area has no pixels to read back");
	if (px) {
		const uint8_t *in_box = px + (50 * 160 + 78) * 4, *in_name = px + (20 * 160 + 40) * 4, *out = px + (70 * 160 + 10) * 4;
		CP_CHECK(in_box[3] == 1 && in_name[3] == 1 && out[3] == 0 && out[0] == 255 && out[1] == 128 && out[2] == 0,
			 "Area pixels: alpha %d at (78,50), %d at (40,20), (%d,%d,%d,%d) at (10,70); want 1, 1, (255,128,0,0)",
			 in_box[3], in_name[3], out[0], out[1], out[2], out[3]);
		free(px);
	}
	/* An alpha of 1 is hit, an alpha of 0 is not: the press passes on. */
	cp_expect_press(878, 300, area, "Area, alpha 1 in the box");
	cp_expect_press(840, 270, area, "Area, alpha 1 in the name's box");
	cp_expect_press(810, 320, UNDER_AREA, "the button behind Area, through alpha 0");
	/* The same shape marked 0 only blocks the cursor: the GUI's
	 * "S2 click target=0 blocked_by=<Area>" before 點擊許可 was read. */
	struct parts *all[] = { o, under[1] };
	cp_expect_hover(378, 300, o, all, 2, "over AreaOff");
	cp_expect_press(378, 300, 0, "AreaOff, alpha 1");
	cp_expect_press(310, 320, UNDER_OFF, "the button behind AreaOff, through alpha 0");
	cp_expect_press(40, 360, left, "the Left panel");
	cp_expect_press(1240, 360, 0, "the panel marked 0, in front of a button");
	printf("CP3 Area and side panel: %s\n", cp_failures ? "FAIL" : "ok");
}

/* --- CP4 --- */

/* A v13 click: the clickable parts under the cursor is recorded on the
 * release (PE_GetClickPartsNumber); nothing enters the v14 queue. */
static int cp_v13_click(int x, int y, const char *what)
{
	extern bool key_state[];
	heap[global_page_slot].page->values[2].i = 0;
	mouse_set_pos(x, y);
	PE_UpdateComponent(0);
	PE_BeginInput();
	key_state[VK_LBUTTON] = false;
	PE_UpdateInputState(0);
	key_state[VK_LBUTTON] = true;
	PE_UpdateInputState(0);
	key_state[VK_LBUTTON] = false;
	PE_UpdateInputState(0);
	int clicked = PE_GetClickPartsNumber();
	int queued = br_msg_type(), busy = heap[global_page_slot].page->values[2].i;
	CP_CHECK(queued < 0 && busy == 0, "%s: v14 queue holds type %d, g_EndPartsBusyLoop %d; want none and 0", what, queued, busy);
	br_msg_release();
	PE_EndInput();
	return clicked;
}

static void cp_guards(void)
{
	br_input_setup();
	struct ex_tree *kids;
	struct ex *e = lb_activity("ClickPermissionGuards", 8, &kids);
	static const char *widgets[] = { "ButtonNone", "Button0", "DetectorNone", "Detector1" };
	static const int widget_click[] = { CP_NONE, 0, CP_NONE, 1 };
	for (int i = 0; i < 4; i++) {
		struct cp_attr a = { 1100, 20 + 100 * i, 1, widget_click[i], 0 };
		kids->children[i] = cp_node(widgets[i], &a, i < 2 ? cp_button_info() : cp_detector_info());
	}
	/* Decorations in front (z 50) of four script buttons. */
	static const char *blockers[] = { "Block0", "Block2", "BlockNone", "Through" };
	static const struct cp_attr blocker_at[] = {
		{ 50, 50, 50, 0, 0 }, { 650, 50, 50, 2, 0 }, { 50, 400, 50, CP_NONE, 0 }, { 650, 400, 50, 0, 1 } };
	for (int i = 0; i < 4; i++)
		kids->children[4 + i] = cp_node(blockers[i], &blocker_at[i], cp_rect_info(400, 300));
	int act = cp_load("ClickPermissionGuards", e);
	int widget[4], blocker[4];
	for (int i = 0; i < 4; i++) {
		widget[i] = cp_no(act, widgets[i]);
		blocker[i] = cp_no(act, blockers[i]);
	}
	heap_unref(act);
	/* Buttons (190 of the game's 192 are marked 0) and ＣＧ判定部件 (all 7
	 * are marked 1) are clickable as before, and take the press with or
	 * without an event. They have no CG here: give them a hit area. */
	for (int i = 0; i < 4; i++) {
		cp_expect_flag(widget[i], true, widgets[i]);
		if (widget[i] <= 0)
			continue;
		cp_hit_area(widget[i], 142, 50);
		cp_expect_flag(widget[i], true, widgets[i]);
		cp_expect_press(1150, 45 + 100 * i, widget[i], widgets[i]);
	}
	/* A disabled button swallows the press. */
	for (int i = 0; i < 2 && widget[i] > 0; i++) {
		BU_PE("SetButtonEnable", BU_I(widget[i]), BU_I(0));
		cp_hit_area(widget[i], 142, 50);
		cp_expect_swallowed(1150, 45 + 100 * i, widgets[i]);
		BU_PE("SetButtonEnable", BU_I(widget[i]), BU_I(1));
		cp_hit_area(widget[i], 142, 50);
		cp_expect_press(1150, 45 + 100 * i, widget[i], widgets[i]);
	}
	const int B = 9941;
	for (int i = 0; i < 4; i++) {
		br_box(B + i, blocker_at[i].x + 50, blocker_at[i].y + 50, 200, 80, true);
		PE_SetZ(B + i, 10);
	}
	for (int i = 0; i < 3; i++) {
		cp_expect_flag(blocker[i], false, blockers[i]);
		cp_expect_press(blocker_at[i].x + 100, blocker_at[i].y + 90, 0, blockers[i]);
	}
	cp_expect_flag(blocker[3], false, blockers[3]);
	cp_expect_press(blocker_at[3].x + 100, blocker_at[3].y + 90, B + 3, "the button behind a pass-through decoration");

	/* v13: the release-time path, in a free corner of the same scene. */
	int version = ain->version;
	ain->version = 13;
	const int V = 9951, W = 9952, N = 9953;
	br_box(V, 470, 420, 150, 60, true);
	br_box(W, 470, 520, 150, 60, true);
	struct parts *n = br_box(N, 460, 510, 170, 80, true);
	PE_SetClickable(N, false);
	PE_SetZ(N, 50);
	CP_CHECK(!n->clickable && !n->pass_cursor, "v13 blocker: clickable %d, pass cursor %d", n->clickable, n->pass_cursor);
	int clicked = cp_v13_click(500, 440, "v13 button");
	CP_CHECK(clicked == V, "v13 button: clicked parts %d, want %d", clicked, V);
	clicked = cp_v13_click(500, 550, "v13 button behind a blocker");
	CP_CHECK(clicked == 0, "v13 button behind a parts that blocks the cursor: clicked parts %d, want 0", clicked);
	PE_SetClickable(N, true);
	clicked = cp_v13_click(500, 550, "v13 clickable blocker");
	CP_CHECK(clicked == N, "v13 clickable parts in front: clicked parts %d, want %d", clicked, N);
	ain->version = version;
	printf("CP4 guards: %s\n", cp_failures ? "FAIL" : "ok");
}

/* --- CP5 --- */

static void cp_save(void)
{
	br_input_setup();
	struct ex_tree *kids;
	struct ex *e = lb_activity("ClickPermissionSave", 3, &kids);
	struct cp_attr target_at = { 100, 100, 10, 1, 0 }, off_at = { 500, 100, 10, 1, 0 }, plain_at = { 900, 100, 10, 0, 0 };
	kids->children[0] = cp_node("Target", &target_at, cp_rect_info(300, 200));
	kids->children[1] = cp_node("SwitchedOff", &off_at, cp_rect_info(300, 200));
	kids->children[2] = cp_node("Plain", &plain_at, cp_rect_info(300, 200));
	int act = cp_load("ClickPermissionSave", e);
	int target = cp_no(act, "Target"), off = cp_no(act, "SwitchedOff"), plain = cp_no(act, "Plain");
	heap_unref(act);
	if (target <= 0 || off <= 0 || plain <= 0)
		return;
	cp_expect_flag(target, true, "Target before saving");
	cp_expect_flag(off, true, "SwitchedOff as loaded");
	cp_register_event(target, 7, 70);
	BU_PE("Parts_SetClickable", BU_I(off), BU_I(0));
	PE_UpdateComponent(0);
	struct page *saved = NULL;
	CP_CHECK(PE_Save(&saved), "PE_Save failed");
	if (!saved)
		return;
	/* Loading has to bring every flag back from the page. */
	PE_SetClickable(target, false);
	PE_SetClickable(off, true);
	PE_SetClickable(plain, true);
	CP_CHECK(PE_Load(&saved), "PE_Load failed");
	PE_UpdateComponent(0);
	cp_expect_flag(target, true, "Target after loading");
	cp_expect_flag(off, false, "SwitchedOff after loading (the script's false)");
	cp_expect_flag(plain, false, "Plain after loading");
	struct cp_press m = cp_expect_press(250, 200, target, "Target after loading");
	CP_CHECK(m.delegate == 7, "the press on Target after loading carries delegate %d, want 7", m.delegate);
	cp_expect_press(650, 200, 0, "SwitchedOff after loading");
	cp_expect_press(1050, 200, 0, "Plain after loading");
	ca_free_buffer(saved);
	printf("CP5 save and load: %s\n", cp_failures ? "FAIL" : "ok");
}

/* --- CP6 --- */

static void cp_whole_screen(void)
{
	br_input_setup();
	struct ex_tree *kids;
	struct ex *e = lb_activity("ClickPermissionDialog", 5, &kids);
	/* The dialog's strip (Inside, 1280x256 at y 232, z 9) blocks the cursor;
	 * the skill rows lie on it (PartySkillView's Button at about z 41). */
	struct cp_attr inside_at = { 0, 232, 9, 0, 0 };
	struct cp_attr button_at = { 600, 250, 41, 1, 0 }, event_at = { 100, 250, 41, 1, 0 };
	struct cp_attr script_at = { 600, 380, 41, 0, 0 }, disabled_at = { 100, 380, 41, 1, 0 };
	kids->children[0] = cp_node("Inside", &inside_at, cp_rect_info(1280, 256));
	kids->children[1] = cp_node("Button", &button_at, cp_cg_info("ClickPermissionMissingCg"));
	kids->children[2] = cp_node("ButtonEvent", &event_at, cp_cg_info("ClickPermissionMissingCg"));
	kids->children[3] = cp_node("ButtonScript", &script_at, cp_cg_info("ClickPermissionMissingCg"));
	kids->children[4] = cp_node("ButtonDisabled", &disabled_at, cp_cg_info("ClickPermissionMissingCg"));
	int act = cp_load("ClickPermissionDialog", e);
	int inside = cp_no(act, "Inside"), button = cp_no(act, "Button"), with_event = cp_no(act, "ButtonEvent");
	int by_script = cp_no(act, "ButtonScript"), disabled = cp_no(act, "ButtonDisabled");
	heap_unref(act);
	if (inside <= 0 || button <= 0 || with_event <= 0 || by_script <= 0 || disabled <= 0)
		return;
	const int rows[] = { button, with_event, by_script, disabled };
	for (int i = 0; i < 4; i++)
		cp_hit_area(rows[i], 470, 90);
	PE_UpdateComponent(0);
	struct parts *all[] = { parts_get(inside), parts_get(button) };
	CP_CHECK(all[0]->global.z < all[1]->global.z, "z order: Inside %d, Button %d", all[0]->global.z, all[1]->global.z);
	/* The script never refers to Button: it is clickable and the target, it
	 * has no event, and the press is the whole-screen click, on which the
	 * dialog closes (SceneContext's LClickEvent). */
	cp_expect_flag(button, true, "Button");
	CP_CHECK(all[1]->delegate_index < 0, "Button has delegate index %d", all[1]->delegate_index);
	cp_expect_hover(700, 290, all[1], all, 2, "over Button");
	cp_expect_press(700, 290, 0, "Button, which has no event");
	cp_expect_press(1200, 480, 0, "the dialog's strip beside the rows");
	/* With an event the parts takes the press (in the original the
	 * whole-screen click is sent as well). */
	cp_register_event(with_event, 5, 50);
	struct cp_press m = cp_expect_press(200, 290, with_event, "a marked parts with an event");
	CP_CHECK(m.delegate == 5 && m.unique == 50, "its press carries delegate %d unique %d, want 5, 50", m.delegate, m.unique);
	/* A parts the script itself makes clickable takes the press as before,
	 * with or without an event; so does a marked one after the script's
	 * setter (which can also take the flag away). */
	cp_expect_flag(by_script, false, "ButtonScript as loaded");
	BU_PE("Parts_SetClickable", BU_I(by_script), BU_I(1));
	cp_expect_press(700, 420, by_script, "a parts made clickable by the script");
	BU_PE("Parts_SetClickable", BU_I(button), BU_I(0));
	cp_expect_press(700, 290, 0, "Button after Parts_SetClickable(false)");
	BU_PE("Parts_SetClickable", BU_I(button), BU_I(1));
	cp_expect_press(700, 290, button, "Button after Parts_SetClickable(true)");
	/* A disabled button swallows the press whatever made it clickable. */
	BU_PE("SetButtonEnable", BU_I(disabled), BU_I(0));
	cp_hit_area(disabled, 470, 90);
	cp_expect_flag(disabled, true, "ButtonDisabled");
	cp_expect_swallowed(200, 420, "a marked parts disabled as a button");
	printf("CP6 closing on the whole-screen click: %s\n", cp_failures ? "FAIL" : "ok");
}

static int cp_fork(const char *name, void (*fn)(void))
{
	fflush(stdout);
	pid_t pid = fork();
	if (pid == 0) {
		cp_failures = 0;
		alarm(60);
		fn();
		fflush(stdout);
		_exit(cp_failures ? 1 : 0);
	}
	int status = 0;
	waitpid(pid, &status, 0);
	bool ok = WIFEXITED(status) && WEXITSTATUS(status) == 0;
	if (!ok)
		printf("%s FAILED (%s %d)\n", name, WIFEXITED(status) ? "exit" : "signal",
		       WIFEXITED(status) ? WEXITSTATUS(status) : WTERMSIG(status));
	return ok ? 0 : 1;
}

static int test_click_permission(void)
{
	int failed = 0;
	failed += cp_fork("CP1", cp_loader);
	failed += cp_fork("CP2", cp_result);
	/* CP3 draws: the construction mode's GL setup and its environment check. */
	int r = cn_fork_case("CP3", cp_area, &cp_failures);
	failed += r == 1;
	failed += cp_fork("CP4", cp_guards);
	failed += cp_fork("CP5", cp_save);
	failed += cp_fork("CP6", cp_whole_screen);
	if (r == 2) {
		printf("CLICK-PERMISSION ENVIRONMENT FAILURE: CP3 could not run, %d of 6 cases failed\n", failed);
		return 2;
	}
	printf("CLICK-PERMISSION %s: %d of 6 cases failed\n", failed ? "FAIL" : "PASS", failed);
	return failed ? 1 : 0;
}
