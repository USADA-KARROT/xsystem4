/* cg-deform: the flip of a v14 CG state (SpriteDeform: 1 left-right, 2
 * top-bottom), as the original keeps it (static disassembly only):
 *   - the CG object of each state keeps it in its byte +0x20; the setter
 *     0x59c5b0 (Parts_SetPartsCG 0x585a56 -> 0x598070 -> 0x565400) writes
 *     it with the CG name, also for an empty name, and not when the CG
 *     cannot be loaded; the CG is flipped when it is set (0x5aa250: 1 ->
 *     0x5aa2d0, 2 -> 0x5aa540), so only a CG state is flipped;
 *   - the CG part's pactex loader (0x59cd70) reads 変形 (key 0x7dd09c,
 *     default 0) beside ＣＧ名 and hands both to 0x59c5b0; it is the key's
 *     only reader: a ＣＧ判定部件 is another kind of object (0x5b77f0 type
 *     0x1b, constructor 0x59b600, no deform byte) whose loader (0x59bd90)
 *     reads ＣＧ名 and サーフェイスエリア only;
 *   - Parts_GetPartsCGDeform (0x585b89 -> 0x598120 -> 0x565540) returns the
 *     state's byte. Its parts lookup, 0x53e510 (SetPartsCG's as well), has
 *     no parts for a number of 0 or less (0x539c80: 0 is read) and makes
 *     one for any other number (0x578ee0 -> 0x578cb0); 0x565540, like the
 *     setter 0x565400, first makes the state a CG (0x5b77f0 with type
 *     0x13): a state of another type is replaced by a new CG object
 *     (0x59c1d0, +0x20 = 0), so it reads 0 and is an empty CG afterwards,
 *     and a state that becomes a CG again starts with 0;
 *   - the script's CCGParts: ReverseLR::get is Deform::get == 1 (and
 *     ReverseTB == 2), and CGName::set(v) is SetCGName(v, ReverseLR,
 *     ReverseTB), which passes GetDeform (1 for LR, 2 for TB, else 0) to
 *     SetPartsCG: a CG flipped by the pactex stays flipped when the script
 *     gives it another picture (the battle result's faces, an activity
 *     whose ＣＧ部件 state has 変形 1).
 * Before the fix the loader passed 0 and Parts_GetPartsCGDeform returned 0,
 * so every pactex flip was lost, and the next CGName::set undid a flip the
 * script had set; the flip was the parts' last one, drawn on every state.
 *
 * The CGs are PNG files made in memory (cg_assets_shadow.inc). The model of
 * a drawn flip is the CG with its columns (or rows) in the reverse order:
 * box pixel (x, y) shows texel (w - 1 - x, y) for 1 and (x, h - 1 - y) for
 * 2; drawn pixels are compared with a tolerance of 2 per channel.
 *
 * Cases (each in its own fork):
 *   CD1 the loader: a synthetic GBK activity of 低等級部件 with ＣＧ部件
 *       states; Parts_GetPartsCGDeform (AIN declaration) per state: 変形 1
 *       on the normal state only, 2, missing (0), 1 with an empty ＣＧ名,
 *       and 1 on a hover state with a CG; ＣＧ判定部件 states with 変形 1
 *       (a CG) and 2 (no CG) read 0. The port's legacy ordinal path
 *       (branches the low-level loader does not read; the game has none)
 *       keeps 変形 as the low-level path does: 1 with an empty ＣＧ名, 2
 *       with a CG, a ＣＧ判定部件's not; a ＣＧ部件 branch without ＣＧ名
 *       and 変形 leaves its state as it was (no CG is made).
 *   CD2 drawn: the normal states against the model (flipped left-right,
 *       top-bottom, not flipped), and the hover state of the parts whose
 *       normal state is flipped, drawn as it is.
 *   CD3 the script's path, real bytecode: AFL_Parts_WrapCG of the loaded
 *       parts, then CCGParts@CGName::set with another CG: still flipped
 *       (drawn and Parts_GetPartsCGDeform); ReverseLR::get true; the parts
 *       whose empty CG had 変形 1 gets its CG flipped; SetCGName(v, false,
 *       false) draws it as it is and a following CGName::set keeps that.
 *   CD4 the setter (AIN declaration Parts_SetPartsCG): a deform per state
 *       (state 2 set unflipped before state 1 flipped is drawn unflipped);
 *       an empty name keeps the deform; a CG that does not exist changes
 *       neither CG nor deform; a state that is no CG is not flipped (a
 *       construction built on state 1 right after a flipped CG is drawn as
 *       it is).
 *   CD5 Parts_IsCursorIn with PixelDecide on a CG whose left half is
 *       opaque: flipped (the hover state set unflipped after it), the
 *       opaque half is on the right; every box pixel against the model,
 *       also for a CG opaque on its top-left quarter flipped top-bottom
 *       (bottom-left) and left-right (top-right); a ＣＧ判定部件 whose
 *       normal state has 変形 1 is hit as it is.
 *   CD6 PE_Save / PE_Load keep each state's deform (getter and drawing);
 *       a save without a flipped state has no CG deform section (its last
 *       words are the controller IDs' as before); a save whose section is
 *       taken out loads every state with the parts' last deform.
 *   CD7 guard, AIN version 13: the activity loaded under it reads no
 *       変形 (every parts' deform 0, 変形 1 and 2 drawn as they are, the
 *       legacy branch without ＣＧ名 not made a CG), as before; the parts'
 *       last deform is drawn on every state, as before (state 2 set flipped
 *       after state 1: state 1 is drawn flipped); Parts_GetPartsCGDeform
 *       reads 0 for every state and makes no parts, as before.
 *   CD8 an alpha clipper (SetComponentAlphaClipper, hidden as the game's
 *       are) whose CG has 変形 1 masks flipped: of an opaque CG under the
 *       half CG's mask, the right half shows; the quarter CG's mask flipped
 *       top-bottom shows the bottom-left, not flipped the top-left.
 *   CD9 the getter makes the state a CG: a flipped CG state that became a
 *       construction reads 0 and is an empty CG afterwards (nothing drawn),
 *       and the getter asked for a redraw (it does not on a CG state);
 *       the script's CGName::set on such a state (a CG flipped top-bottom
 *       before) draws the new CG unflipped; a state that is made a CG again
 *       by a SetPartsCG whose CG does not exist reads 0; number 0 and -3
 *       read 0 and make no parts, a number above 0 without parts reads 0
 *       and gets one; in a horizontal layout box, a child whose normal
 *       state the getter made an empty CG is 0x0 and the next child moves
 *       to where it starts.
 * Only functions and fields that exist before the fix are used (the new
 * behaviour is seen through the AIN declarations, the drawing, the hit
 * test, the layout, the save, the redraw flag, sprite_deform and a state's
 * type), so the file builds before the fix.
 */
static int cd_failures;
#define CD_CHECK(c, ...) do { if (!(c)) { printf("  FAIL: "); printf(__VA_ARGS__); printf("\n"); cd_failures++; } } while (0)

#define CD_DEFORM "\x89\xe4\xd0\xce"							/* 変形 */
#define CD_KEYDOWN "\xa5\xad\xa9\x60\xa5\xc0\xa5\xa6\xa5\xf3\xa0\xee\x91\x42"	/* キーダウン狀態 */
#define CD_SLASH "\xa3\xaf"								/* ／ */

/* --- the CGs --- */
enum { CD_FACE, CD_FACE2, CD_HALF, CD_QUAD, CD_NR };
#define CD_W 16
#define CD_H 8
static const char *cd_names[CD_NR] = { "cd" CD_SLASH "face", "cd" CD_SLASH "face2", "cd" CD_SLASH "half", "cd" CD_SLASH "quad" };
static uint8_t *cd_px[CD_NR];	/* RGBA, CD_W x CD_H */
static struct probe_cg_file cd_files[CD_NR];

/* Every texel of the two faces differs from its mirror images; the half CG
 * is opaque on its left half only, the quarter CG on its top-left quarter
 * only (so a top-bottom flip moves it as well). */
static void cd_texel(int id, int x, int y, uint8_t *p)
{
	switch (id) {
	case CD_FACE:
		p[0] = 8 + x * 15; p[1] = 30 + y * 25; p[2] = 77; p[3] = 255;
		break;
	case CD_FACE2:
		p[0] = 240 - x * 14; p[1] = 210 - y * 22; p[2] = 150; p[3] = 255;
		break;
	case CD_HALF:
		p[0] = p[1] = p[2] = 255; p[3] = x < CD_W / 2 ? 255 : 0;
		break;
	default:
		p[0] = p[1] = p[2] = 255; p[3] = x < CD_W / 2 && y < CD_H / 2 ? 255 : 0;
		break;
	}
}

static void cd_install(void)
{
	for (int i = 0; i < CD_NR; i++) {
		uint8_t *px = cd_px[i] = calloc(CD_W * CD_H, 4);
		for (int k = 0; k < CD_W * CD_H; k++)
			cd_texel(i, k % CD_W, k / CD_W, px + k * 4);
		cd_files[i].name = cd_names[i];
		cd_files[i].data = cg_png(CD_W, CD_H, px, true, &cd_files[i].size);
	}
	probe_install_cg_assets(cd_files, CD_NR);
}

/* --- the pactex --- */
struct cd_state {
	const char *cg;		/* ＣＧ名 ("" for an empty one) */
	int deform;		/* 変形; -1: no key */
	const char *type;	/* 部件タイプ, NULL: ＣＧ部件 */
};
/* A 低等級部件 at (x, y) whose three states are ＣＧ部件 (or the type a
 * state names). */
static struct ex_tree cd_node(const char *name, int x, int y, const struct cd_state st[3])
{
	static const char *state_names[3] = { BU_NORMAL, BU_ONCURSOR, CD_KEYDOWN };
	struct lb_place pl = { x, y, 1 };
	struct ex_tree info = bu_branch(BU_INFO, 4);
	info.children[0] = bu_sleaf(BU_TYPE, BU_LOW);
	for (int i = 0; i < 3; i++) {
		const bool key = st[i].deform >= 0;
		struct ex_tree s = bu_branch(state_names[i], key ? 3 : 2);
		s.children[0] = bu_sleaf(BU_TYPE, st[i].type ? st[i].type : BU_CG);
		s.children[1] = bu_sleaf(BX_K_CG, st[i].cg);
		if (key)
			s.children[2] = bu_ileaf(CD_DEFORM, st[i].deform);
		info.children[1 + i] = s;
	}
	return lb_node(name, &pl, info, 0);
}

/* A 低等級部件 whose n branches (n <= 3) are no named states: the port's
 * legacy ordinal path takes them as states 1..n. */
static struct ex_tree cd_legacy_node(const char *name, int x, int y, int n, const struct cd_state *st)
{
	static const char *bnames[3] = { "LegacyA", "LegacyB", "LegacyC" };
	struct lb_place pl = { x, y, 1 };
	struct ex_tree info = bu_branch(BU_INFO, 1 + n);
	info.children[0] = bu_sleaf(BU_TYPE, BU_LOW);
	for (int i = 0; i < n; i++) {
		const bool key = st[i].deform >= 0;
		struct ex_tree s = bu_branch(bnames[i], key ? 3 : 2);
		s.children[0] = bu_sleaf(BU_TYPE, st[i].type ? st[i].type : BU_CG);
		s.children[1] = bu_sleaf(BX_K_CG, st[i].cg);
		if (key)
			s.children[2] = bu_ileaf(CD_DEFORM, st[i].deform);
		info.children[1 + i] = s;
	}
	return lb_node(name, &pl, info, 0);
}

/* The loaded activity's parts; positions are set afterwards. */
enum { CD_P_LR, CD_P_TB, CD_P_NONE, CD_P_EMPTY, CD_P_HOVER, CD_P_HALF, CD_P_DETECT, CD_P_LEGACY, CD_P_LEGACY_NOKEY, CD_NR_P };
static const char *cd_pnames[CD_NR_P] = { "FaceLR", "FaceTB", "FacePlain", "FaceEmpty", "FaceHover", "Half", "Detect", "Legacy", "LegacyNoKey" };
static int cd_no[CD_NR_P];

static void cd_setup(void)
{
	ts_render_setup();
	cd_install();
	gk_enable();
	const char *F = cd_names[CD_FACE];
	const struct cd_state lr[3] = { { F, 1 }, { "", 0 }, { "", 0 } }, tb[3] = { { F, 2 }, { "", 0 }, { "", 0 } },
		none[3] = { { F, -1 }, { "", -1 }, { "", -1 } }, empty[3] = { { "", 1 }, { "", 0 }, { "", 0 } },
		hover[3] = { { F, 0 }, { F, 1 }, { "", 0 } }, half[3] = { { cd_names[CD_HALF], 1 }, { "", 0 }, { "", 0 } },
		/* ＣＧ判定部件 with 変形: the original has no deform there. */
		detect[3] = { { cd_names[CD_HALF], 1, BU_CGDETECT }, { "", 2, BU_CGDETECT }, { "", 0 } },
		/* Legacy: an empty ＣＧ名 with 変形 1, a CG with 2, a ＣＧ判定部件
		 * without a CG with 1; LegacyNoKey: an empty ＣＧ名 without 変形. */
		legacy[3] = { { "", 1 }, { F, 2 }, { "", 1, BU_CGDETECT } }, nokey[1] = { { "", -1 } };
	const struct cd_state *all[CD_NR_P] = { lr, tb, none, empty, hover, half, detect, NULL, NULL };
	struct ex_tree *kids;
	struct ex *e = lb_activity("CgDeform", CD_NR_P, &kids);
	for (int i = 0; i < CD_NR_P; i++)
		kids->children[i] = all[i] ? cd_node(cd_pnames[i], 10, 10, all[i])
			: i == CD_P_LEGACY ? cd_legacy_node(cd_pnames[i], 10, 10, 3, legacy)
			: cd_legacy_node(cd_pnames[i], 10, 10, 1, nokey);
	int act = cn_load("CgDeform", e);
	cd_failures += cn_failures;
	cn_failures = 0;
	for (int i = 0; i < CD_NR_P; i++) {
		cd_no[i] = bu_number(act, cd_pnames[i]);
		CD_CHECK(cd_no[i] > 0 && parts_try_get(cd_no[i]), "%s has no parts", cd_pnames[i]);
		if (cd_no[i] <= 0)
			continue;
		/* Out of the layout box, at (20, 30) on the 96x128 view. */
		ri_parent(cd_no[i], -1);
		ri_pos(cd_no[i], 20, 30);
	}
	heap_unref(act);
	PE_UpdateComponent(0);
}

static int cd_deform(int no, int state)
{
	return ri_hll("Parts_GetPartsCGDeform", 2, (union vm_value[]){ RI_I(no), RI_I(state) }).i;
}
static bool cd_set(int no, const char *cg, int deform, int state)
{
	int s = sf_str(cg, -1);
	bool r = ri_hll("Parts_SetPartsCG", 4, (union vm_value[]){ RI_I(no), RI_I(s), RI_I(deform), RI_I(state) }).i;
	heap_unref(s);
	return r;
}

/* Draws parts no (its current state) and compares the box at (20, 30) with
 * CG id under deform d; outside the box nothing is drawn. */
static void cd_expect_drawn(const char *what, int no, int id, int d)
{
	uint8_t *px = ts_draw(parts_get(no));
	int bad = 0, outside = 0;
	for (int y = 0; y < TS_VIEW_H; y++) {
		for (int x = 0; x < TS_VIEW_W; x++) {
			const uint8_t *g = px + (y * TS_VIEW_W + x) * 4;
			const int bx = x - 20, by = y - 30;
			if (bx < 0 || by < 0 || bx >= CD_W || by >= CD_H) {
				outside += g[3] != 0;
				continue;
			}
			const int sx = d == 1 ? CD_W - 1 - bx : bx, sy = d == 2 ? CD_H - 1 - by : by;
			const uint8_t *w = cd_px[id] + (sy * CD_W + sx) * 4;
			if (abs(g[0] - w[0]) > 2 || abs(g[1] - w[1]) > 2 || abs(g[2] - w[2]) > 2 || abs(g[3] - w[3]) > 2) {
				if (bad++ < 3)
					CD_CHECK(0, "%s: box (%d,%d) = (%d,%d,%d,%d), want (%d,%d,%d,%d) (texel %d,%d)", what, bx, by,
						 g[0], g[1], g[2], g[3], w[0], w[1], w[2], w[3], sx, sy);
			}
		}
	}
	free(px);
	CD_CHECK(bad <= 3, "%s: %d box pixels differ from the model in all", what, bad);
	CD_CHECK(outside == 0, "%s: %d pixels drawn outside the box", what, outside);
}

static void cd_expect_deform(const char *what, int no, int d1, int d2, int d3)
{
	int g1 = cd_deform(no, 1), g2 = cd_deform(no, 2), g3 = cd_deform(no, 3);
	CD_CHECK(g1 == d1 && g2 == d2 && g3 == d3, "%s: Parts_GetPartsCGDeform states 1..3 = %d %d %d, want %d %d %d",
		 what, g1, g2, g3, d1, d2, d3);
}

/* CD1 */
static void cd_loader(void)
{
	cd_setup();
	/* Before any getter (which makes a state a CG): a legacy ＣＧ部件 branch
	 * without ＣＧ名 and 変形 leaves its state as it was. */
	struct parts *nk = parts_try_get(cd_no[CD_P_LEGACY_NOKEY]);
	CD_CHECK(nk && nk->states[0].type != PARTS_CG, "CD1 a legacy branch without ＣＧ名 and 変形 made its state a CG");
	cd_expect_deform("CD1 変形 1 on the normal state", cd_no[CD_P_LR], 1, 0, 0);
	cd_expect_deform("CD1 変形 2", cd_no[CD_P_TB], 2, 0, 0);
	cd_expect_deform("CD1 no 変形 key", cd_no[CD_P_NONE], 0, 0, 0);
	cd_expect_deform("CD1 変形 1 with an empty ＣＧ名", cd_no[CD_P_EMPTY], 1, 0, 0);
	cd_expect_deform("CD1 変形 1 on the hover state", cd_no[CD_P_HOVER], 0, 1, 0);
	cd_expect_deform("CD1 the legacy ordinal path: 1 with an empty ＣＧ名, 2 with a CG, a ＣＧ判定部件's 1 not read",
			 cd_no[CD_P_LEGACY], 1, 2, 0);
	/* A ＣＧ判定部件 is no CG object in the original (no deform): its 変形 is
	 * not read, with a CG (1) or without (2). */
	cd_expect_deform("CD1 ＣＧ判定部件 states with 変形 1 and 2", cd_no[CD_P_DETECT], 0, 0, 0);
	printf("CD1 checked\n");
}

/* CD2 */
static void cd_drawn(void)
{
	cd_setup();
	cd_expect_drawn("CD2 変形 1", cd_no[CD_P_LR], CD_FACE, 1);
	cd_expect_drawn("CD2 変形 2", cd_no[CD_P_TB], CD_FACE, 2);
	cd_expect_drawn("CD2 no 変形", cd_no[CD_P_NONE], CD_FACE, 0);
	cd_expect_drawn("CD2 normal state 0, hover 1: the normal state", cd_no[CD_P_HOVER], CD_FACE, 0);
	PE_SetInputState(cd_no[CD_P_HOVER], 2);
	PE_UpdateComponent(0);
	cd_expect_drawn("CD2 normal state 0, hover 1: the hover state", cd_no[CD_P_HOVER], CD_FACE, 1);
	printf("CD2 checked\n");
}

/* CD3: the script's CCGParts, real bytecode. */
static int cd_wrap_cg(int no, int *vtoff)
{
	int args[] = { no, -1, 1 };	/* partsNumber, option<string> comment = none */
	union vm_value r[2] = {{ .i = -1 }, { .i = -1 }};
	int n = da_callfunc(da_fn("AFL_Parts_WrapCG"), 3, args, r);
	cd_failures += da_failures;
	da_failures = 0;
	*vtoff = r[1].i;
	CD_CHECK(n == 2 && da_live_page(r[0].i), "AFL_Parts_WrapCG(%d) returned %d slot(s) [%d, %d]", no, n, r[0].i, r[1].i);
	return n == 2 && da_live_page(r[0].i) ? r[0].i : -1;
}
/* A method of obj as CALLMETHOD leaves it (v14: the object below the
 * arguments); the caller keeps its references to the arguments, as with
 * vm_call (which pushes the object last, so only for methods without
 * arguments). Returns the first result slot, -1 without one. */
static int cd_method(const char *fn, int obj, int nargs, const int *args)
{
	const int sp = stack_ptr;
	const size_t ip = instr_ptr;
	stack_push(obj);
	for (int i = 0; i < nargs; i++)
		stack_push(args[i]);
	function_call_args_borrowed = true;
	method_call(da_fn(fn), VM_RETURN);
	function_call_args_borrowed = false;
	vm_execute();
	instr_ptr = ip;
	int r = stack_ptr > sp ? stack[sp].i : -1;
	stack_ptr = sp;
	return r;
}
static void cd_cgname_set(int obj, const char *cg)
{
	int s = sf_str(cg, -1);
	cd_method("parts::detail::CCGParts@CGName::set", obj, 1, (int[]){ s });
	heap_unref(s);
}
static void cd_set_cgname(int obj, const char *cg, bool lr, bool tb)
{
	int s = sf_str(cg, -1);
	cd_method("parts::detail::CCGParts@SetCGName", obj, 3, (int[]){ s, lr, tb });
	heap_unref(s);
}
static int cd_reverse_lr(int obj)
{
	return cd_method("parts::detail::CCGParts@ReverseLR::get", obj, 0, NULL);
}
/* SetPartsCG asks advengine whether the parts are delayed (frame skip):
 * they are not. */
static bool cd_no_frame_skip(void)
{
	int skip = ain_get_global(ain, "advengine::detail::g_ADVEngineFrameSkip");
	int speed = ain_get_global(ain, "advengine::detail::g_ADVEngineFrameSkipSpeedUp");
	CD_CHECK(skip >= 0 && speed >= 0, "the frame skip globals are not declared");
	if (skip < 0 || speed < 0)
		return false;
	heap[global_page_slot].page->values[skip].i = 0;
	heap[global_page_slot].page->values[speed].i = 0;
	return true;
}
static void cd_script(void)
{
	cd_setup();
	if (!cd_no_frame_skip())
		return;
	int vt, obj = cd_wrap_cg(cd_no[CD_P_LR], &vt);
	if (obj < 0)
		return;
	CD_CHECK(cd_reverse_lr(obj) == 1, "CD3 ReverseLR::get of the loaded 変形 1 is %d, want 1", cd_reverse_lr(obj));
	cd_cgname_set(obj, cd_names[CD_FACE2]);
	PE_UpdateComponent(0);
	cd_expect_deform("CD3 after CGName::set", cd_no[CD_P_LR], 1, 0, 0);
	cd_expect_drawn("CD3 after CGName::set: the new CG, flipped", cd_no[CD_P_LR], CD_FACE2, 1);
	cd_set_cgname(obj, cd_names[CD_FACE2], false, false);
	PE_UpdateComponent(0);
	cd_expect_drawn("CD3 SetCGName(v, false, false)", cd_no[CD_P_LR], CD_FACE2, 0);
	cd_cgname_set(obj, cd_names[CD_FACE]);
	PE_UpdateComponent(0);
	cd_expect_drawn("CD3 CGName::set after SetCGName(v, false, false)", cd_no[CD_P_LR], CD_FACE, 0);
	CD_CHECK(cd_reverse_lr(obj) == 0, "CD3 ReverseLR::get after it is %d, want 0", cd_reverse_lr(obj));
	int obj2 = cd_wrap_cg(cd_no[CD_P_EMPTY], &vt);
	if (obj2 < 0)
		return;
	cd_cgname_set(obj2, cd_names[CD_FACE]);
	PE_UpdateComponent(0);
	cd_expect_deform("CD3 the empty CG with 変形 1 given a CG", cd_no[CD_P_EMPTY], 1, 0, 0);
	cd_expect_drawn("CD3 the empty CG with 変形 1 given a CG", cd_no[CD_P_EMPTY], CD_FACE, 1);
	heap_unref(obj);
	heap_unref(obj2);
	printf("CD3 checked\n");
}

/* CD4 */
static void cd_setter(void)
{
	cd_setup();
	const int A = cd_no[CD_P_NONE];
	/* State 2 first, state 1 last: the last deform set is 1, the state 2
	 * keeps its own 0. */
	CD_CHECK(cd_set(A, cd_names[CD_FACE2], 0, 2), "CD4 Parts_SetPartsCG state 2 failed");
	CD_CHECK(cd_set(A, cd_names[CD_FACE], 1, 1), "CD4 Parts_SetPartsCG state 1 failed");
	cd_expect_deform("CD4 state 2 deform 0, then state 1 deform 1", A, 1, 0, 0);
	PE_UpdateComponent(0);
	cd_expect_drawn("CD4 state 1 (deform 1)", A, CD_FACE, 1);
	PE_SetInputState(A, 2);
	PE_UpdateComponent(0);
	cd_expect_drawn("CD4 state 2 (deform 0, set before state 1's 1)", A, CD_FACE2, 0);
	PE_SetInputState(A, 1);
	CD_CHECK(cd_set(A, "", 2, 3), "CD4 an empty name failed");
	cd_expect_deform("CD4 an empty name with deform 2", A, 1, 0, 2);
	CD_CHECK(!cd_set(A, "cd" CD_SLASH "missing", 0, 1), "CD4 a CG that does not exist was set");
	cd_expect_deform("CD4 a CG that does not exist", A, 1, 0, 2);
	PE_UpdateComponent(0);
	cd_expect_drawn("CD4 a CG that does not exist: the old CG, flipped", A, CD_FACE, 1);
	/* State 1 becomes a construction right after a flipped CG (its own
	 * surface; the original flips the CG's surface only): the left half
	 * red, the right half blue, drawn as it is. */
	CD_CHECK(cd_set(A, cd_names[CD_FACE], 1, 1), "CD4 Parts_SetPartsCG state 1 failed");
	PE_AddCreateToPartsConstructionProcess(A, CD_W, CD_H, 1);
	PE_AddFillToPartsConstructionProcess(A, 0, 0, CD_W / 2, CD_H, 250, 10, 20, 1);
	PE_AddFillToPartsConstructionProcess(A, CD_W / 2, 0, CD_W / 2, CD_H, 15, 25, 240, 1);
	CD_CHECK(PE_BuildPartsConstructionProcess(A, 1), "CD4 the construction was not built");
	PE_UpdateComponent(0);
	uint8_t *px = ts_draw(parts_get(A));
	const uint8_t *l = px + (33 * TS_VIEW_W + 22) * 4, *r = px + (33 * TS_VIEW_W + 33) * 4;
	CD_CHECK(l[0] > 200 && l[2] < 60 && r[2] > 200 && r[0] < 60,
		 "CD4 a construction after a flipped CG: left (%d,%d,%d), right (%d,%d,%d), want red then blue",
		 l[0], l[1], l[2], r[0], r[1], r[2]);
	free(px);
	printf("CD4 checked\n");
}

/* CD5 */
/* Parts_IsCursorIn on every pixel of the box at (20, 30) against CG id
 * under deform d: a pixel is hit where the texel it shows is opaque. */
static void cd_expect_hits(const char *what, int no, int id, int d)
{
	int bad = 0;
	for (int by = 0; by < CD_H; by++) {
		for (int bx = 0; bx < CD_W; bx++) {
			const int sx = d == 1 ? CD_W - 1 - bx : bx, sy = d == 2 ? CD_H - 1 - by : by;
			const bool want = cd_px[id][(sy * CD_W + sx) * 4 + 3] > 0;
			const bool hit = ri_in(no, 20 + bx, 30 + by) != 0;
			if (hit != want && bad++ < 3)
				CD_CHECK(0, "%s: box (%d,%d) %s, want %s (texel %d,%d)", what, bx, by, hit ? "hit" : "not hit",
					 want ? "hit" : "no hit", sx, sy);
		}
	}
	CD_CHECK(bad == 0, "%s: %d box pixels hit the other way", what, bad);
}
static void cd_hit(void)
{
	cd_setup();
	const int A = cd_no[CD_P_HALF];
	ri_hll("Parts_SetPartsPixelDecide", 2, (union vm_value[]){ RI_I(A), RI_I(1) });
	/* The hover state set unflipped after the loader's flipped normal one:
	 * the normal state stays flipped. */
	cd_set(A, cd_names[CD_HALF], 0, 2);
	PE_UpdateComponent(0);
	/* The CG's opaque half is texels 0..7; flipped, box columns 8..15. */
	CD_CHECK(!ri_in(A, 22, 33), "CD5 box column 2 (texel 13, transparent) is hit");
	CD_CHECK(ri_in(A, 33, 33), "CD5 box column 13 (texel 2, opaque) is not hit");
	cd_expect_hits("CD5 the loader's 変形 1", A, CD_HALF, 1);
	cd_set(A, cd_names[CD_HALF], 0, 1);
	PE_UpdateComponent(0);
	CD_CHECK(ri_in(A, 22, 33) && !ri_in(A, 33, 33), "CD5 not flipped: the opaque half is not on the left");
	cd_expect_hits("CD5 not flipped", A, CD_HALF, 0);
	/* Top-bottom (2): the quarter CG, opaque on its top-left only, is hit on
	 * its bottom-left; left-right (1) on its top-right. */
	cd_set(A, cd_names[CD_QUAD], 2, 1);
	PE_UpdateComponent(0);
	cd_expect_hits("CD5 the quarter CG flipped top-bottom", A, CD_QUAD, 2);
	cd_set(A, cd_names[CD_QUAD], 1, 1);
	PE_UpdateComponent(0);
	cd_expect_hits("CD5 the quarter CG flipped left-right", A, CD_QUAD, 1);
	/* The ＣＧ判定部件 whose normal state has 変形 1 (the half CG): no deform,
	 * the opaque half is hit on the left. */
	const int D = cd_no[CD_P_DETECT];
	ri_hll("Parts_SetPartsPixelDecide", 2, (union vm_value[]){ RI_I(D), RI_I(1) });
	PE_UpdateComponent(0);
	cd_expect_hits("CD5 a ＣＧ判定部件 with 変形 1", D, CD_HALF, 0);
	printf("CD5 checked\n");
}

/* Where the CG deform section starts in a save ("PCDF"), -1 without one. */
static int cd_section_pos(struct page *saved)
{
	for (int i = 0; saved && i < saved->nr_vars; i++)
		if (saved->values[i].i == 0x46444350)
			return i;
	return -1;
}

/* CD6 */
static void cd_save(void)
{
	cd_setup();
	const int A = cd_no[CD_P_HOVER], B = cd_no[CD_P_LR], C = cd_no[CD_P_NONE];
	cd_set(A, cd_names[CD_FACE], 1, 2);
	cd_set(B, cd_names[CD_FACE], 1, 1);
	cd_set(C, cd_names[CD_FACE2], 2, 1);
	cd_set(C, cd_names[CD_FACE], 0, 2);	/* C's last deform is 0, state 1 has 2 */
	PE_UpdateComponent(0);
	struct page *saved = NULL;
	CD_CHECK(PE_Save(&saved) && saved, "CD6 PE_Save failed");
	if (!saved)
		return;
	CD_CHECK(PE_Load(&saved), "CD6 PE_Load failed");
	PE_UpdateComponent(0);
	cd_expect_deform("CD6 loaded: normal 0, hover 1", A, 0, 1, 0);
	cd_expect_deform("CD6 loaded: normal 1", B, 1, 0, 0);
	cd_expect_deform("CD6 loaded: normal 2, hover 0 set last", C, 2, 0, 0);
	cd_expect_drawn("CD6 loaded: normal 1 drawn", B, CD_FACE, 1);
	cd_expect_drawn("CD6 loaded: normal 2 drawn", C, CD_FACE2, 2);
	cd_expect_drawn("CD6 loaded: normal 0 drawn", A, CD_FACE, 0);
	/* Without the section (a save of an engine that did not write it):
	 * every state has the parts' last deform. */
	int pos = cd_section_pos(saved);
	CD_CHECK(pos > 0 && pos + 2 < saved->nr_vars, "CD6 the save has no CG deform section");
	if (pos > 0 && pos + 2 < saved->nr_vars) {
		int words = 2 + saved->values[pos + 1].i * 4;
		CD_CHECK(pos + words <= saved->nr_vars, "CD6 the section runs past the save");
		if (pos + words <= saved->nr_vars) {
			/* The words are ints: taken out in place. */
			memmove(saved->values + pos, saved->values + pos + words,
				(saved->nr_vars - pos - words) * sizeof(saved->values[0]));
			saved->nr_vars -= words;
			CD_CHECK(PE_Load(&saved), "CD6 PE_Load of the save without the section failed");
			PE_UpdateComponent(0);
			cd_expect_deform("CD6 no section: the hover state's 1 was the last", A, 1, 1, 1);
			cd_expect_deform("CD6 no section: the normal state's 1 was the last", B, 1, 1, 1);
			cd_expect_deform("CD6 no section: the hover state's 0 was the last", C, 0, 0, 0);
			cd_expect_drawn("CD6 no section: normal 2 now drawn with 0", C, CD_FACE2, 0);
		}
	}
	ca_free_buffer(saved);
	/* Parts whose states all have their last deform (flipped or not): no
	 * section, the save is what it was. */
	probe_init_parts_table();
	const int P1 = 9791, P2 = 9792;
	cd_set(P1, cd_names[CD_FACE], 0, 1);
	cd_set(P2, cd_names[CD_FACE], 1, 1);
	cd_set(P2, cd_names[CD_FACE], 0, 1);
	struct page *plain = NULL;
	CD_CHECK(PE_Save(&plain) && plain, "CD6 PE_Save of the plain parts failed");
	CD_CHECK(cd_section_pos(plain) < 0, "CD6 a save without a flipped state has a CG deform section");
	ca_free_buffer(plain);
	printf("CD6 checked\n");
}

/* CD7 */
static void cd_v13(void)
{
	/* The activity loaded under AIN version 13 (an engine before v14 that
	 * declares the Activity API): 変形 is not read, as before. */
	ain->version = 13;
	cd_setup();
	for (int k = 0; k < CD_NR_P; k++) {
		struct parts *p = parts_try_get(cd_no[k]);
		CD_CHECK(p && p->sprite_deform == 0, "CD7 v13 loader: %s has the deform %d, want 0 (変形 not read)",
			 cd_pnames[k], p ? p->sprite_deform : -1);
	}
	struct parts *lg = parts_try_get(cd_no[CD_P_LEGACY]);
	CD_CHECK(lg && lg->states[0].type != PARTS_CG, "CD7 v13 loader: the legacy branch without ＣＧ名 made its state a CG");
	PE_UpdateComponent(0);
	cd_expect_drawn("CD7 v13 loader: 変形 1 drawn as it is", cd_no[CD_P_LR], CD_FACE, 0);
	cd_expect_drawn("CD7 v13 loader: 変形 2 drawn as it is", cd_no[CD_P_TB], CD_FACE, 0);
	cd_expect_drawn("CD7 v13 loader: the normal state of hover 変形 1 drawn as it is", cd_no[CD_P_HOVER], CD_FACE, 0);
	const int A = 9781;
	PE_SetPos(A, 20, 30);
	struct string *f = cstr_to_string(cd_names[CD_FACE]);
	PE_SetPartsCG(A, f, 0, 1);
	PE_SetPartsCG(A, f, 1, 2);
	free_string(f);
	PE_UpdateComponent(0);
	cd_expect_drawn("CD7 v13: state 1 drawn with the last deform (1)", A, CD_FACE, 1);
	/* The getter before v14 reads 0 and makes no parts, as before the
	 * states had their own deform. */
	cd_expect_deform("CD7 v13: Parts_GetPartsCGDeform", A, 0, 0, 0);
	const int B = 9782;
	CD_CHECK(!parts_exists(B), "CD7 v13: parts %d exists before the getter", B);
	CD_CHECK(cd_deform(B, 1) == 0, "CD7 v13: Parts_GetPartsCGDeform of a number without parts is not 0");
	CD_CHECK(!parts_exists(B), "CD7 v13: Parts_GetPartsCGDeform made parts %d", B);
	printf("CD7 checked\n");
}

/* CD8 */
/* Draws parts no (the face CG, at (20, 30)) under its alpha clipper, whose
 * CG mask_id has deform d: the face shows where the mask's texel under the
 * pixel is opaque, nothing elsewhere. */
static void cd_expect_masked(const char *what, int no, int mask_id, int d)
{
	uint8_t *px = ts_draw(parts_get(no));
	int bad = 0;
	for (int by = 0; by < CD_H; by++) {
		for (int bx = 0; bx < CD_W; bx++) {
			const uint8_t *g = px + ((30 + by) * TS_VIEW_W + 20 + bx) * 4;
			const int sx = d == 1 ? CD_W - 1 - bx : bx, sy = d == 2 ? CD_H - 1 - by : by;
			const bool shown = cd_px[mask_id][(sy * CD_W + sx) * 4 + 3] > 0;
			const uint8_t *w = cd_px[CD_FACE] + (by * CD_W + bx) * 4;
			bool ok = shown ? abs(g[0] - w[0]) <= 2 && abs(g[1] - w[1]) <= 2 && g[3] >= 253 : g[3] <= 2;
			if (!ok && bad++ < 3)
				CD_CHECK(0, "%s: box (%d,%d) = (%d,%d,%d,%d), want %s", what, bx, by, g[0], g[1], g[2], g[3],
					 shown ? "the CG" : "nothing");
		}
	}
	free(px);
	CD_CHECK(bad == 0, "%s: %d box pixels masked the other way", what, bad);
}
static void cd_clipper(void)
{
	cd_setup();
	const int A = cd_no[CD_P_NONE], M = cd_no[CD_P_HALF];
	cd_set(M, cd_names[CD_HALF], 0, 2);	/* not the last deform: the state's own counts */
	PE_SetShow(M, false);
	ri_hll("SetComponentAlphaClipper", 2, (union vm_value[]){ RI_I(A), RI_I(M) });
	PE_UpdateComponent(0);
	/* The half CG's 変形 1: of the face, the right half shows. */
	cd_expect_masked("CD8 the half CG's mask, 変形 1", A, CD_HALF, 1);
	/* Top-bottom (2): the quarter CG (opaque top-left) masks to its
	 * bottom-left; then not flipped, to its top-left. */
	cd_set(M, cd_names[CD_QUAD], 2, 1);
	PE_UpdateComponent(0);
	cd_expect_masked("CD8 the quarter CG's mask flipped top-bottom", A, CD_QUAD, 2);
	cd_set(M, cd_names[CD_QUAD], 0, 1);
	PE_UpdateComponent(0);
	cd_expect_masked("CD8 the quarter CG's mask not flipped", A, CD_QUAD, 0);
	printf("CD8 checked\n");
}

/* CD9 */
static void cd_expect_nothing_drawn(const char *what, int no)
{
	uint8_t *px = ts_draw(parts_get(no));
	int drawn = 0;
	for (int i = 0; i < TS_VIEW_W * TS_VIEW_H; i++)
		drawn += px[i * 4 + 3] > 2;
	free(px);
	CD_CHECK(drawn == 0, "%s: %d pixels drawn, want none", what, drawn);
}
/* State 1 of parts no becomes a construction: the left half red, the right
 * half blue, drawn as it is. */
static void cd_construction(const char *what, int no)
{
	PE_AddCreateToPartsConstructionProcess(no, CD_W, CD_H, 1);
	PE_AddFillToPartsConstructionProcess(no, 0, 0, CD_W / 2, CD_H, 250, 10, 20, 1);
	PE_AddFillToPartsConstructionProcess(no, CD_W / 2, 0, CD_W / 2, CD_H, 15, 25, 240, 1);
	CD_CHECK(PE_BuildPartsConstructionProcess(no, 1), "%s: the construction was not built", what);
	PE_UpdateComponent(0);
	uint8_t *px = ts_draw(parts_get(no));
	const uint8_t *l = px + (33 * TS_VIEW_W + 22) * 4, *r = px + (33 * TS_VIEW_W + 33) * 4;
	CD_CHECK(l[0] > 200 && l[2] < 60 && l[3] > 250 && r[2] > 200 && r[0] < 60 && r[3] > 250,
		 "%s: the construction: left (%d,%d,%d,%d), right (%d,%d,%d,%d), want red then blue", what,
		 l[0], l[1], l[2], l[3], r[0], r[1], r[2], r[3]);
	free(px);
}
/* Whether the getter on (no, state) asks for the scene to be drawn again
 * (parts_dirty, as SetPartsCG with an empty name: every parts sprite is
 * marked on the next update). */
static bool cd_getter_redraws(int no, int state, int *d)
{
	parts_engine_clean();
	scene_is_dirty = false;
	*d = cd_deform(no, state);
	parts_render_update();
	const bool r = scene_is_dirty;
	scene_is_dirty = true;
	return r;
}
/* The getter's relayout: a horizontal layout box with two CG children
 * (16 wide); the first one's normal state becomes a construction of the
 * same size, then the getter makes it an empty CG, 0x0, and the box puts
 * the second child where the first starts. */
static void cd_getter_layout(void)
{
	const struct cd_state plain[3] = { { cd_names[CD_FACE], -1 }, { "", -1 }, { "", -1 } };
	struct ex_tree *kids;
	struct ex *e = lb_activity("CgDeformBox", 1, &kids);
	struct lb_place p0 = { 0, 0, 1 };
	kids->children[0] = lb_box("CdBox", &p0, 2, 1, 2);
	lb_kids(&kids->children[0])->children[0] = cd_node("CdBoxA", 0, 0, plain);
	lb_kids(&kids->children[0])->children[1] = cd_node("CdBoxB", 0, 0, plain);
	int act = cn_load("CgDeformBox", e);
	cd_failures += cn_failures;
	cn_failures = 0;
	const int a = bu_number(act, "CdBoxA"), b = bu_number(act, "CdBoxB");
	heap_unref(act);
	struct parts *pb = b > 0 ? parts_try_get(b) : NULL;
	CD_CHECK(a > 0 && pb, "CD9 the layout box children have no parts");
	if (a <= 0 || !pb)
		return;
	PE_UpdateComponent(0);
	CD_CHECK(pb->local.pos.x == CD_W && pb->local.pos.y == 0, "CD9 the second child at (%d,%d), want (%d,0)",
		 pb->local.pos.x, pb->local.pos.y, CD_W);
	PE_AddCreateToPartsConstructionProcess(a, CD_W, CD_H, 1);
	CD_CHECK(PE_BuildPartsConstructionProcess(a, 1), "CD9 the layout box child's construction was not built");
	PE_UpdateComponent(0);
	CD_CHECK(pb->local.pos.x == CD_W, "CD9 the second child after the construction at x %d, want %d", pb->local.pos.x, CD_W);
	const int d = cd_deform(a, 1);
	PE_UpdateComponent(0);
	const int w = BU_PE("Parts_GetPartsWidth", BU_I(a), BU_I(1)).i;
	CD_CHECK(d == 0 && w == 0, "CD9 the layout box child reads %d, %d wide, want 0 and 0", d, w);
	CD_CHECK(pb->local.pos.x == 0 && pb->local.pos.y == 0,
		 "CD9 after the getter made the first child 0x0, the second at (%d,%d), want (0,0) (the box laid out again)",
		 pb->local.pos.x, pb->local.pos.y);
}
static void cd_getter_state(void)
{
	cd_setup();
	if (!cd_no_frame_skip())
		return;
	/* The loader's 変形 1 CG, then a construction on the state: the getter
	 * replaces it by a new CG (0x5b77f0), which has 0 and no picture, and
	 * asks for a redraw; on a CG state it changes nothing. */
	int d;
	const bool redraw_cg = cd_getter_redraws(cd_no[CD_P_NONE], 1, &d);
	CD_CHECK(d == 0 && !redraw_cg, "CD9 the getter on a CG state reads %d, redraw %d, want 0 and no redraw", d, redraw_cg);
	const int A = cd_no[CD_P_LR];
	cd_construction("CD9 a construction after a flipped CG", A);
	const bool redraw = cd_getter_redraws(A, 1, &d);
	CD_CHECK(d == 0, "CD9 a construction after a flipped CG reads %d, want 0", d);
	CD_CHECK(redraw, "CD9 the getter made the construction an empty CG without asking for a redraw");
	PE_UpdateComponent(0);
	cd_expect_nothing_drawn("CD9 the construction after the getter (an empty CG)", A);
	d = cd_deform(A, 1);
	CD_CHECK(d == 0, "CD9 the empty CG the getter made reads %d, want 0", d);
	/* The script's CGName::set (which reads the getter) on a construction
	 * that was a CG flipped top-bottom: the new CG is not flipped. */
	const int B = cd_no[CD_P_TB];
	cd_construction("CD9 a construction after a CG flipped top-bottom", B);
	int vt, obj = cd_wrap_cg(B, &vt);
	if (obj >= 0) {
		cd_cgname_set(obj, cd_names[CD_FACE2]);
		PE_UpdateComponent(0);
		cd_expect_drawn("CD9 CGName::set on it: the new CG, not flipped", B, CD_FACE2, 0);
		cd_expect_deform("CD9 CGName::set on it", B, 0, 0, 0);
		heap_unref(obj);
	}
	/* Made a CG again by a SetPartsCG whose CG does not exist (0x565400
	 * makes the state a new CG before 0x59c5b0 fails): it reads 0. */
	const int C = cd_no[CD_P_HALF];
	cd_construction("CD9 a construction after the flipped half CG", C);
	CD_CHECK(!cd_set(C, "cd" CD_SLASH "missing", 1, 1), "CD9 a CG that does not exist was set");
	d = cd_deform(C, 1);
	CD_CHECK(d == 0, "CD9 a state made a CG by a SetPartsCG that failed reads %d, want 0", d);
	/* The lookup (0x53e510): no parts for 0 or less, one for any other
	 * number. */
	const bool had0 = parts_exists(0), had_m3 = parts_exists(-3);
	const int d0 = cd_deform(0, 1), dm3 = cd_deform(-3, 1);
	CD_CHECK(d0 == 0 && dm3 == 0, "CD9 number 0 reads %d, -3 reads %d, want 0", d0, dm3);
	CD_CHECK(parts_exists(0) == had0 && parts_exists(-3) == had_m3, "CD9 the getter made parts 0 or -3");
	const int N = 9771;
	CD_CHECK(!parts_exists(N), "CD9 parts %d exists before the getter", N);
	d = cd_deform(N, 2);
	CD_CHECK(d == 0, "CD9 a number without parts reads %d, want 0", d);
	CD_CHECK(parts_exists(N), "CD9 the getter made no parts %d (0x578ee0 makes one)", N);
	cd_getter_layout();
	printf("CD9 checked\n");
}

static int test_cg_deform(void)
{
	int failed = 0, env = 0, n = 0;
	static const struct { const char *name; void (*fn)(void); } cases[] = {
		{ "CD1 the loader reads 変形 per state", cd_loader },
		{ "CD2 a flipped state is drawn flipped", cd_drawn },
		{ "CD3 the script's CGName::set keeps the flip", cd_script },
		{ "CD4 Parts_SetPartsCG keeps a deform per state", cd_setter },
		{ "CD5 the pixel hit test follows the flip", cd_hit },
		{ "CD6 PE_Save / PE_Load keep the deforms", cd_save },
		{ "CD7 guard: AIN version 13", cd_v13 },
		{ "CD8 a flipped alpha clipper masks flipped", cd_clipper },
		{ "CD9 the getter makes the state a CG", cd_getter_state },
	};
	for (unsigned i = 0; i < sizeof cases / sizeof cases[0]; i++, n++) {
		int r = cn_fork_case(cases[i].name, cases[i].fn, &cd_failures);
		printf("%s %s\n", cases[i].name, r == 0 ? "PASS" : r == 1 ? "FAIL" : "ENVIRONMENT FAILURE");
		failed += r == 1;
		env += r == 2;
	}
	if (env) {
		printf("CG-DEFORM ENVIRONMENT FAILURE: %d case(s) could not run, %d of %d cases failed\n", env, failed, n);
		return 2;
	}
	printf("CG-DEFORM %s: %d of %d cases failed\n", failed ? "FAIL" : "PASS", failed, n);
	return failed ? 92 : 0;
}
