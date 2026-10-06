/* layout-box: v14 レイアウトボックス (EPartsType 8), after the original's box
 * widget (static disassembly only):
 *   - the pactex loader 0x5493a0 (from 0x5393ef) reads レイアウトタイプ,
 *     折り返し許可 (== 1), 折り返しサイズ (float), 配置 and パディング (top,
 *     bottom, left, right) into +0x44..+0x5c, each with the getter's default
 *     0; the component parser 0x553650 reads マージン (top, bottom, left,
 *     right) into +0x110..+0x11c (0x5542f9..0x55434e);
 *   - a vertical (1) / horizontal (2) box (0x549bd0 / 0x54a200) overwrites
 *     its children's positions: start = box origin offset - offset of the
 *     effective alignment (0x54b4c0: a vertical box treats 4/5/6 as 1, a
 *     horizontal box 2/5/8; with wrapping also 2/8 and 4/6) + padding
 *     (left/top +, right/bottom -, centre 0); the cursor adds the margins
 *     the same way, and the child's origin mode becomes the alignment;
 *   - only children shown (+0xab) and not hidden in the editor (+0xac)
 *     advance; the others are still placed;
 *   - a child's size is its normal state's (GetSize(1)); a child box reports
 *     its own size (0x54a9f0, 0x0 unless both are positive), a user
 *     component its first child's or 200x200 (0x4e5880), a free box the
 *     largest right/bottom edge of its shown children (0x54aaf0);
 *   - a free box (0) keeps its children's positions;
 *   - a text's width is its glyph textures plus the 字間隔 between them, one
 *     less than the glyphs (0x5bee50, 0x5bcd20: 0x5bcde8);
 *   - a box is never under the cursor (vtable 0x80f8ec +0x4c = 0x49c5b0).
 * The 春銷 count labels (CountBase: type 2, 配置 7, origin mode 5) were
 * spread out because xsystem4 kept the editor's snapshot positions.
 *
 * The positions and sizes follow from these rules. The original lays a
 * shown box out on every frame (0x549970); xsystem4 marks the box when
 * something the layout reads changes, so "after one update" (LB2, LB4, LB5,
 * LB6, LB9, LB10, LB12) checks that marking as well. The cases call
 * PE_UpdateComponent, the core of the v14 UpdateComponent.
 *
 *   LB1 box keys (GBK and SJIS), the getter defaults of a box without them,
 *       the v14 padding getters and setter (an unknown number is ignored)
 *       and a child's マージン.
 *   LB2 CountBase shape: horizontal, 配置 7, box origin 5, margins incl.
 *       negative ones; exact positions, forced origin mode, box size.
 *   LB3 配置 3 (right to left), a centred vertical box with padding, the
 *       effective alignment of 5 in both directions, the two commonest
 *       shapes left (horizontal 配置 7 with box origin 9, vertical 配置 3),
 *       and the box not being under the cursor while its child is.
 *   LB4 relayout after a child's size or margin changes; a hovered child
 *       still takes its normal state's size; a child moved or given another
 *       origin mode by a script is put back; a normal state reset by an
 *       empty CG name is 0x0; the box's own origin mode moves the start.
 *   LB5 a hidden (SetComponentShow false / 編輯上表示 0) child does not
 *       advance but is still placed; a released child leaves the box.
 *   LB6 nested boxes: an inner box's computed size, also a vertical box
 *       inside a 配置 7 one.
 *   LB7 a free box (the activity root, and a nested one) keeps positions.
 *   LB8 v13 keeps the previous layout (centre padding -right, hidden child
 *       advances, no box size), with ain->version set to 13 in the fork.
 *   LB9 sizes the loader does not build: a user component without and with
 *       content (its first child, a free root), and a 構築部件 whose steps
 *       are not run (its first step's canvas, 先矩形 W,H; the steps include a
 *       command the loader does not support, see the construction mode).
 *   LB10 text children: n characters have n - 1 spacings (differences
 *       between boxes that differ in 字間隔 or length, so the cell width does
 *       not matter); a 14 px line with 縁取り 2 is 18 high; relayout after
 *       Parts_SetText, an emptied text and a surface area; under the GBK
 *       grid also the cell widths and the
 *       CountBase shape with its real texts (人材 14/縁 2/-4, ／ 16/1.5/-4),
 *       which give the 30 px from 人材 to the digit measured in the original.
 *   LB11 wrapping: the shape of the game's only wrapping box (vertical,
 *       size 100, four buttons; 402x44 is derived from their snapshot
 *       positions: the second row ends exactly at 100 and stays), one pixel
 *       more wraps; a horizontal one, where 配置 4 becomes 1.
 *   LB12 a box is laid out the same after PE_Save / PE_Load: with an
 *       unbuilt 構築部件 (its canvas) and with a text whose 太さ widens the
 *       cells (both saved since XPE v6).
 *   LB13 numeral children drawn with a font (表示タイプ 2, 19 px, 縁取り 2):
 *       the width is the digit cells plus the 字間隔 between them (0x5b4df0:
 *       0x5b50d3, 0x5b5164..0x5b5182), so one digit is as wide with 字間隔 -5
 *       as with 0, and three digits are three cells and three cells less 10;
 *       the box follows Parts_SetNumeralNumber. Under the GBK grid a cell is
 *       10 + 2 * 2. A regression guard added after the fix: 8a1dcdc already
 *       has it, so this case passes on the commits that follow it and was
 *       never run on the tree before that fix.
 * Not covered: the marking after a gauge child's surface area changes (no
 * box in the game has a gauge child), after a CG is emptied by number, and
 * for a parent attached while loading a save.
 * The children need no GL (矩形部件 sized by their corners, boxes, user
 * components, unbuilt 構築部件), except the texts of LB10 and LB12 (the
 * windowless context and glyphless font of text-default; its setup reports
 * a failure under that mode's name) and the numerals of LB13, whose digits
 * are drawn into a texture (the shader context of the construction mode).
 * Each case runs in a fork; LB13's is the construction mode's, which tells
 * a failure of the GL environment from a failed case and runs the case once
 * more. Only functions that exist before the fix are
 * used (the padding getters are reached through their HLL declarations), so
 * before-check builds the old tree.
 */
static void cn_gl_setup(void);
static int cn_fork_case(const char *name, void (*fn)(void), int *failures);

static int lb_failures;
#define LB_CHECK(c, ...) do { if (!(c)) { printf("  FAIL: "); printf(__VA_ARGS__); printf("\n"); lb_failures++; } } while (0)

#define LB_LTYPE "\xa5\xec\xa5\xa4\xa5\xa2\xa5\xa6\xa5\xc8\xa5\xbf\xa5\xa4\xa5\xd7"	/* レイアウトタイプ */
#define LB_WRAP "\xd5\xdb\xa4\xea\xb7\xb5\xa4\xb7\xd4\x53\xbf\xc9"			/* 折り返し許可 */
#define LB_WSIZE "\xd5\xdb\xa4\xea\xb7\xb5\xa4\xb7\xa5\xb5\xa5\xa4\xa5\xba"		/* 折り返しサイズ */
#define LB_RATE "\xd5\xdb\xa4\xea\xb7\xb5\xa4\xb7\xa5\xb5\xa5\xa4\xa5\xba\xa4\xf2\xa5\xec\xa9\x60\xa5\xc8\xa4\xc8\xa4\xb7\xa4\xc6\xd5\x4a\xd7\x52\xa4\xb9\xa4\xeb"
#define LB_ALIGN "\xc5\xe4\xd6\xc3"							/* 配置 */
#define LB_PAD "\xa5\xd1\xa5\xc7\xa5\xa3\xa5\xf3\xa5\xb0"				/* パディング */
#define LB_LTYPE_S "\x83\x8c\x83\x43\x83\x41\x83\x45\x83\x67\x83\x5e\x83\x43\x83\x76"
#define LB_WRAP_S "\x90\xdc\x82\xe8\x95\xd4\x82\xb5\x8b\x96\x89\xc2"
#define LB_WSIZE_S "\x90\xdc\x82\xe8\x95\xd4\x82\xb5\x83\x54\x83\x43\x83\x59"
#define LB_RATE_S "\x90\xdc\x82\xe8\x95\xd4\x82\xb5\x83\x54\x83\x43\x83\x59\x82\xf0\x83\x8c\x81\x5b\x83\x67\x82\xc6\x82\xb5\x82\xc4\x94\x46\x8e\xaf\x82\xb7\x82\xe9"
#define LB_ALIGN_S "\x94\x7a\x92\x75"
#define LB_PAD_S "\x83\x70\x83\x66\x83\x42\x83\x93\x83\x4f"
#define LB_MARGIN "\xa5\xde\xa9\x60\xa5\xb8\xa5\xf3"					/* マージン */
#define LB_MARGIN_S "\x83\x7d\x81\x5b\x83\x57\x83\x93"
#define LB_POS "\xd7\xf9\x98\xcb"							/* 座標 */
#define LB_OM "\xd4\xad\xfc\x63\xd7\xf9\x98\xcb\xc4\xa3\xca\xbd"			/* 原點座標模式 */
#define LB_SHOW "\xb1\xed\xca\xbe"							/* 表示 */
#define LB_DEST "\xcf\xc8\xbe\xd8\xd0\xce"						/* 先矩形 */

static struct ex_tree lb_ilist(const char *k, int n, const int *v)
{
	struct ex_list *l = xcalloc(1, sizeof(*l));
	l->nr_items = n;
	l->items = xcalloc(n, sizeof(*l->items));
	for (int i = 0; i < n; i++)
		l->items[i].value = (struct ex_value){ .type = EX_INT, .i = v[i] };
	return bu_leaf(k, (struct ex_value){ .type = EX_LIST, .list = l });
}

/* Where a component sits in its parent. */
struct lb_place {
	int x, y, om;
	int margin[4];	/* top, bottom, left, right */
	bool hidden;	/* 表示 = 0 */
	bool sjis;	/* SJIS マージン key */
};

/* name { 座標, 原點座標模式, マージン, [表示 0], <info>, [子部件 x nkids] } */
static struct ex_tree lb_node(const char *name, const struct lb_place *pl, struct ex_tree info, unsigned nkids)
{
	unsigned n = 3 + (pl->hidden ? 1 : 0) + 1 + (nkids ? 1 : 0), k = 0;
	struct ex_tree t = bu_branch(name, n);
	t.children[k++] = lb_ilist(LB_POS, 3, (int[]){ pl->x, pl->y, 1 });
	t.children[k++] = bu_ileaf(LB_OM, pl->om);
	t.children[k++] = lb_ilist(pl->sjis ? LB_MARGIN_S : LB_MARGIN, 4, pl->margin);
	if (pl->hidden)
		t.children[k++] = bu_ileaf(LB_SHOW, 0);
	t.children[k++] = info;
	if (nkids)
		t.children[k++] = bu_branch(BU_KIDS, nkids);
	assert(k == n);
	return t;
}
static struct ex_tree *lb_kids(struct ex_tree *node)
{
	return &node->children[node->nr_children - 1];
}

/* 種類別情報 of a box. wsize is a float unless int_size. */
static struct ex_tree lb_box_info(bool sjis, int type, int wrap, float wsize, bool int_size, int align,
		int pt, int pb, int pl, int pr)
{
	struct ex_tree info = bu_branch(BU_INFO, 7);
	info.children[0] = bu_sleaf(BU_TYPE, BU_LAYOUT);
	info.children[1] = bu_ileaf(sjis ? LB_LTYPE_S : LB_LTYPE, type);
	info.children[2] = bu_ileaf(sjis ? LB_WRAP_S : LB_WRAP, wrap);
	info.children[3] = int_size ? bu_ileaf(sjis ? LB_WSIZE_S : LB_WSIZE, (int)wsize)
		: bu_leaf(sjis ? LB_WSIZE_S : LB_WSIZE, (struct ex_value){ .type = EX_FLOAT, .f = wsize });
	info.children[4] = bu_ileaf(sjis ? LB_RATE_S : LB_RATE, 0);
	info.children[5] = bu_ileaf(sjis ? LB_ALIGN_S : LB_ALIGN, align);
	info.children[6] = lb_ilist(sjis ? LB_PAD_S : LB_PAD, 4, (int[]){ pt, pb, pl, pr });
	return info;
}
static struct ex_tree lb_box(const char *name, const struct lb_place *pl, int type, int align, unsigned nkids)
{
	return lb_node(name, pl, lb_box_info(false, type, 0, 200.0f, false, align, 0, 0, 0, 0), nkids);
}

/* A 矩形部件 state: its corners (0,0) .. (w,h). */
static struct ex_tree lb_rect_state(const char *state, int w, int h)
{
	struct ex_tree st = bu_branch(state, 6);
	st.children[0] = bu_sleaf(BU_TYPE, BR_RECT);
	st.children[1] = bu_ileaf(BR_RECT_MODE, 1);
	st.children[2] = br_point("\xd7\xf3\xc9\xcf", 0, 0);	/* 左上 */
	st.children[3] = br_point("\xd3\xd2\xc9\xcf", w, 0);	/* 右上 */
	st.children[4] = br_point("\xd7\xf3\xcf\xc2", 0, h);	/* 左下 */
	st.children[5] = br_point("\xd3\xd2\xcf\xc2", w, h);	/* 右下 */
	return st;
}
/* A low-level rect; hover_w > 0 adds an on-cursor rect of another size. */
static struct ex_tree lb_rect(const char *name, const struct lb_place *pl, int w, int h, int hover_w, int hover_h)
{
	struct ex_tree info = bu_branch(BU_INFO, hover_w ? 3 : 2);
	info.children[0] = bu_sleaf(BU_TYPE, BU_LOW);
	info.children[1] = lb_rect_state(BU_NORMAL, w, h);
	if (hover_w)
		info.children[2] = lb_rect_state(BU_ONCURSOR, hover_w, hover_h);
	return lb_node(name, pl, info, 0);
}
#define LB_SPACE "\xd7\xd6\xe9\x67\xb8\xf4"						/* 字間隔 */
#define LB_EDGE "\xa5\xd5\xa5\xa9\xa5\xf3\xa5\xc8\xbf\x46\xc8\xa1\xa4\xea"		/* フォント縁取り */
static struct ex_tree lb_fleaf(const char *k, float v)
{
	return bu_leaf(k, (struct ex_value){ .type = EX_FLOAT, .f = v });
}
#define LB_WEIGHT "\xa5\xd5\xa5\xa9\xa5\xf3\xa5\xc8\xcc\xab\xa4\xb5"			/* フォント太さ */
/* A 文本部件 with 文本 and 文本裝飾 { フォントサイズ, フォント太さ, フォント縁取り, 字間隔 }. */
static struct ex_tree lb_text_w(const char *name, const struct lb_place *pl, const char *text,
		int size, float weight, float edge, int space)
{
	struct ex_tree info = bu_branch(BU_INFO, 2);
	info.children[0] = bu_sleaf(BU_TYPE, BU_LOW);
	struct ex_tree st = td_state(BU_NORMAL, 2);
	st.children[1] = bu_sleaf(TD_TEXT, text);
	st.children[2] = bu_branch(TD_DECO, 4);
	st.children[2].children[0] = bu_ileaf(TD_SIZE, size);
	st.children[2].children[1] = lb_fleaf(LB_WEIGHT, weight);
	st.children[2].children[2] = lb_fleaf(LB_EDGE, edge);
	st.children[2].children[3] = bu_ileaf(LB_SPACE, space);
	info.children[1] = st;
	return lb_node(name, pl, info, 0);
}
static struct ex_tree lb_text(const char *name, const struct lb_place *pl, const char *text,
		int size, float edge, int space)
{
	return lb_text_w(name, pl, text, size, 0, edge, space);
}
/* A 構築部件 the loader leaves unbuilt: its 手順リスト creates a w x h canvas
 * (コマンド 0) and goes on with a command the loader does not support (102). */
static struct ex_tree lb_construction(const char *name, const struct lb_place *pl, int w, int h)
{
	struct ex_tree info = bu_branch(BU_INFO, 2);
	info.children[0] = bu_sleaf(BU_TYPE, BU_LOW);
	info.children[1] = bu_branch(BU_NORMAL, 2);
	struct ex_tree *st = &info.children[1];
	st->children[0] = bu_sleaf(BU_TYPE, BR_CONS);
	st->children[1] = bu_branch(BR_STEPS, 2);
	struct ex_tree *step = &st->children[1].children[0];
	*step = bu_branch(BR_STEP1, 2);
	step->children[0] = bu_ileaf(BR_COMMAND, 0);
	step->children[1] = lb_ilist(LB_DEST, 6, (int[]){ 0, 0, 0, 0, w, h });
	step[1] = bu_branch("Step2", 2);
	step[1].children[0] = bu_ileaf(BR_COMMAND, 102);
	step[1].children[1] = lb_ilist(LB_DEST, 6, (int[]){ 0, 0, 0, 0, 0, 0 });
	return lb_node(name, pl, info, 0);
}
/* The cases with lb_construction measure the canvas path and LB9 has no GL
 * context: say so when the loader starts to build that shape. */
static void lb_expect_unbuilt(int no, const char *what)
{
	struct parts *p = parts_try_get(no);
	LB_CHECK(p && p->states[0].type != PARTS_CONSTRUCTION_PROCESS && !p->states[0].common.texture.handle,
		 "%s: the fixture's unbuilt 構築部件 is built now (command 102?); it needs another shape", what);
}
#define LB_SHOW_TYPE "\xb1\xed\xca\xbe\xa5\xbf\xa5\xa4\xa5\xd7"				/* 表示タイプ */
/* A 數字部件 drawn with a font: 表示タイプ 2, フォントサイズ, フォント縁取り, 字間隔. */
static struct ex_tree lb_numeral(const char *name, const struct lb_place *pl, int size, float edge, int space)
{
	struct ex_tree info = bu_branch(BU_INFO, 2);
	info.children[0] = bu_sleaf(BU_TYPE, BU_LOW);
	info.children[1] = bu_branch(BU_NORMAL, 5);
	struct ex_tree *st = &info.children[1];
	st->children[0] = bu_sleaf(BU_TYPE, BU_NUMERAL);
	st->children[1] = bu_ileaf(LB_SHOW_TYPE, 2);
	st->children[2] = bu_ileaf(TD_SIZE, size);
	st->children[3] = lb_fleaf(LB_EDGE, edge);
	st->children[4] = bu_ileaf(LB_SPACE, space);
	return lb_node(name, pl, info, 0);
}
static struct ex_tree lb_uc(const char *name, const struct lb_place *pl)
{
	struct ex_tree info = bu_branch(BU_INFO, 2);
	info.children[0] = bu_sleaf(BU_TYPE, BU_UC);
	info.children[1] = bu_sleaf(BU_UCNAME, "LayoutBoxUc");
	return lb_node(name, pl, info, 0);
}

/* An activity whose root is a free box with nkids children (filled by the caller). */
static struct ex *lb_activity(const char *name, unsigned nkids, struct ex_tree **kids)
{
	struct ex *e = xcalloc(1, sizeof(*e));
	e->nr_blocks = 1;
	e->blocks = xcalloc(1, sizeof(*e->blocks));
	e->blocks[0].name = cstr_to_string(name);
	e->blocks[0].val.type = EX_TREE;
	struct ex_tree *top = xcalloc(1, sizeof(*top));
	*top = bu_branch(name, 1);
	e->blocks[0].val.tree = top;
	struct ex_tree root = bu_branch("LayoutBoxRoot", 2);
	root.children[0] = lb_box_info(false, 0, 0, 200.0f, false, 1, 0, 0, 0, 0);
	root.children[1] = bu_branch(BU_KIDS, nkids);
	top->children[0] = root;
	*kids = &top->children[0].children[1];
	return e;
}

static int lb_load(const char *act_name, struct ex *e)
{
	struct string *name = cstr_to_string(act_name);
	bool loaded = probe_load_activity_tree(name, e);
	free_string(name);
	ex_free(e);
	LB_CHECK(loaded, "%s did not load", act_name);
	return sf_str(act_name, -1);
}
static int lb_no(int act, const char *name)
{
	int n = bu_number(act, name);
	LB_CHECK(n > 0, "%s has no parts number", name);
	return n;
}

static void lb_expect_pos(int no, int x, int y, int om, const char *what)
{
	struct parts *p = parts_try_get(no);
	if (!p) {
		LB_CHECK(0, "%s: no parts", what);
		return;
	}
	LB_CHECK(p->local.pos.x == x && p->local.pos.y == y && (om < 0 || p->origin_mode == om),
		 "%s at (%d,%d) origin %d, want (%d,%d) origin %d", what,
		 p->local.pos.x, p->local.pos.y, p->origin_mode, x, y, om);
}
static void lb_expect_size(int no, int w, int h, const char *what)
{
	int gw = BU_PE("Parts_GetPartsWidth", BU_I(no), BU_I(1)).i;
	int gh = BU_PE("Parts_GetPartsHeight", BU_I(no), BU_I(1)).i;
	LB_CHECK(gw == w && gh == h, "%s size %dx%d, want %dx%d", what, gw, gh, w, h);
}

static int lb_x(int no)
{
	struct parts *p = parts_try_get(no);
	return p ? p->local.pos.x : -99999;
}
static int lb_width(int no)
{
	return BU_PE("Parts_GetPartsWidth", BU_I(no), BU_I(1)).i;
}
static int lb_y(int no)
{
	struct parts *p = parts_try_get(no);
	return p ? p->local.pos.y : -99999;
}

/* --- LB1: box keys, defaults, padding getters, margins --- */
static void lb_attributes(void)
{
	bu_postlink();
	struct ex_tree *kids;
	struct ex *e = lb_activity("LayoutBoxAttr", 3, &kids);
	struct lb_place p0 = { 0 };
	kids->children[0] = lb_node("BoxG", &p0, lb_box_info(false, 2, 1, 150.0f, false, 9, 1, 2, 3, 4), 1);
	struct lb_place pm = { .margin = { 5, 6, 7, 8 } };
	lb_kids(&kids->children[0])->children[0] = lb_rect("BoxGChild", &pm, 10, 10, 0, 0);
	kids->children[1] = lb_node("BoxS", &p0, lb_box_info(true, 1, 1, 200.0f, true, 3, 11, 12, 13, 14), 1);
	struct lb_place ps = { .margin = { 9, 10, 11, 12 }, .sjis = true };
	lb_kids(&kids->children[1])->children[0] = lb_rect("BoxSChild", &ps, 10, 10, 0, 0);
	struct ex_tree bare = bu_branch(BU_INFO, 1);
	bare.children[0] = bu_sleaf(BU_TYPE, BU_LAYOUT);
	kids->children[2] = lb_node("BoxM", &p0, bare, 0);
	int act = lb_load("LayoutBoxAttr", e);
	int root = lb_no(act, ""), g = lb_no(act, "BoxG"), s = lb_no(act, "BoxS"), m = lb_no(act, "BoxM");
	int gc = lb_no(act, "BoxGChild"), sc = lb_no(act, "BoxSChild");
	struct { const char *what; int no, type, wrap; float size; int align; } want[] = {
		{ "root", root, 0, 0, 200.0f, 1 },
		{ "BoxG (GBK keys)", g, 2, 1, 150.0f, 9 },
		{ "BoxS (SJIS keys, int size)", s, 1, 1, 200.0f, 3 },
		{ "BoxM (no keys: getter defaults 0)", m, 0, 0, 0.0f, 0 },
	};
	for (unsigned i = 0; i < sizeof(want) / sizeof(want[0]); i++) {
		int type = BU_PE("GetLayoutBoxLayoutType", BU_I(want[i].no)).i;
		int wrap = BU_PE("IsLayoutBoxReturn", BU_I(want[i].no)).i;
		float size = BU_PE("GetLayoutBoxReturnSize", BU_I(want[i].no)).f;
		int align = BU_PE("GetLayoutBoxAlign", BU_I(want[i].no)).i;
		LB_CHECK(type == want[i].type && !!wrap == want[i].wrap && size == want[i].size && align == want[i].align,
			 "%s: type %d return %d size %g align %d, want %d %d %g %d", want[i].what,
			 type, wrap, (double)size, align, want[i].type, want[i].wrap, (double)want[i].size, want[i].align);
	}
	const char *pad[4] = { "GetLayoutBoxPaddingTop", "GetLayoutBoxPaddingBottom",
		"GetLayoutBoxPaddingLeft", "GetLayoutBoxPaddingRight" };
	for (int i = 0; i < 4; i++) {
		int v = BU_PE(pad[i], BU_I(g)).i, w = BU_PE(pad[i], BU_I(s)).i;
		LB_CHECK(v == i + 1, "BoxG %s = %d, want %d", pad[i], v, i + 1);
		LB_CHECK(w == 11 + i, "BoxS (SJIS key) %s = %d, want %d", pad[i], w, 11 + i);
	}
	/* The v14 SetLayoutBoxPadding reaches the same fields. */
	BU_PE("SetLayoutBoxPadding", BU_I(s), BU_I(21), BU_I(22), BU_I(23), BU_I(24));
	for (int i = 0; i < 4; i++) {
		int v = BU_PE(pad[i], BU_I(s)).i;
		LB_CHECK(v == 21 + i, "BoxS %s after SetLayoutBoxPadding = %d, want %d", pad[i], v, 21 + i);
	}
	/* 0x594ec0 finds no parts for an unknown number and returns. */
	BU_PE("SetLayoutBoxPadding", BU_I(987654), BU_I(1), BU_I(2), BU_I(3), BU_I(4));
	LB_CHECK(!parts_try_get(987654), "SetLayoutBoxPadding created parts 987654");
	const char *mg[4] = { "GetComponentMarginTop", "GetComponentMarginBottom",
		"GetComponentMarginLeft", "GetComponentMarginRight" };
	for (int i = 0; i < 4; i++) {
		int v = BU_PE(mg[i], BU_I(gc)).i, w = BU_PE(mg[i], BU_I(sc)).i;
		LB_CHECK(v == 5 + i && w == 9 + i, "%s: GBK child %d (want %d), SJIS child %d (want %d)",
			 mg[i], v, 5 + i, w, 9 + i);
	}
	heap_unref(act);
	printf("LB1 box keys and margins: %s\n", lb_failures ? "FAIL" : "ok");
}

/* The CountBase shape: horizontal, 配置 7, box origin 5 at (192,135); the
 * pactex positions are a different snapshot. Sizes and margins (top, bottom,
 * left, right): R1 20x10 (0,2,0,-2), R2 12x14, R3 8x10 (0,2,-5,-6), R4 12x14
 * (R2 also has a 30x30 on-cursor state). */
static void lb_count_base(int out[5])
{
	struct ex_tree *kids;
	struct ex *e = lb_activity("LayoutBoxCount", 1, &kids);
	struct lb_place pb = { 192, 135, 5 };
	kids->children[0] = lb_box("CountBox", &pb, 2, 7, 4);
	struct ex_tree *k = lb_kids(&kids->children[0]);
	struct lb_place p1 = { -59, 12, 7, { 0, 2, 0, -2 } }, p2 = { -5, 14, 7 },
		p3 = { 19, 12, 7, { 0, 2, -5, -6 } }, p4 = { 31, 14, 7 };
	k->children[0] = lb_rect("R1", &p1, 20, 10, 0, 0);
	k->children[1] = lb_rect("R2", &p2, 12, 14, 30, 30);
	k->children[2] = lb_rect("R3", &p3, 8, 10, 0, 0);
	k->children[3] = lb_rect("R4", &p4, 12, 14, 0, 0);
	int act = lb_load("LayoutBoxCount", e);
	out[0] = lb_no(act, "CountBox");
	out[1] = lb_no(act, "R1"); out[2] = lb_no(act, "R2"); out[3] = lb_no(act, "R3"); out[4] = lb_no(act, "R4");
	heap_unref(act);
	PE_UpdateComponent(0);
}

/* --- LB2 --- */
static void lb_horizontal(void)
{
	bu_postlink();
	int n[5];
	lb_count_base(n);
	/* W = 18 + 12 - 3 + 12 = 39, H = 14: start (-19, 7), y up. */
	lb_expect_pos(n[1], -19, 5, 7, "R1");
	lb_expect_pos(n[2], -1, 7, 7, "R2");
	lb_expect_pos(n[3], 6, 5, 7, "R3");
	lb_expect_pos(n[4], 8, 7, 7, "R4");
	lb_expect_size(n[0], 39, 14, "CountBox");
	lb_expect_pos(n[0], 192, 135, 5, "CountBox itself");
	/* Laid out again (a margin set and set back marks the box), the result
	 * is the same. */
	BU_PE("SetComponentMargin", BU_I(n[3]), BU_I(0), BU_I(2), BU_I(-5), BU_I(0));
	BU_PE("SetComponentMargin", BU_I(n[3]), BU_I(0), BU_I(2), BU_I(-5), BU_I(-6));
	PE_UpdateComponent(0);
	lb_expect_pos(n[1], -19, 5, 7, "R1 laid out again");
	lb_expect_pos(n[2], -1, 7, 7, "R2 laid out again");
	lb_expect_pos(n[3], 6, 5, 7, "R3 laid out again");
	lb_expect_pos(n[4], 8, 7, 7, "R4 laid out again");
	lb_expect_size(n[0], 39, 14, "CountBox laid out again");
	printf("LB2 horizontal 配置 7: %s\n", lb_failures ? "FAIL" : "ok");
}

/* --- LB3 --- */
static void lb_alignments(void)
{
	bu_postlink();
	struct ex_tree *kids;
	struct ex *e = lb_activity("LayoutBoxAlign", 6, &kids);
	struct lb_place p0 = { 0, 0, 1 }, pa = { 50, 50, 1, { 0, 0, 1, 2 } }, pb = { 60, 60, 1 };
	/* BoxR: horizontal, 配置 3, padding right 4: right to left. */
	kids->children[0] = lb_node("BoxR", &p0, lb_box_info(false, 2, 0, 200.0f, false, 3, 0, 0, 0, 4), 2);
	lb_kids(&kids->children[0])->children[0] = lb_rect("RA", &pa, 10, 6, 0, 0);
	lb_kids(&kids->children[0])->children[1] = lb_rect("RB", &pb, 8, 4, 0, 0);
	/* BoxC: vertical, 配置 2 (centred column), padding (5,6,7,8). */
	struct lb_place ca = { 50, 50, 1, { 1, 2, 3, 4 } };
	kids->children[1] = lb_node("BoxC", &p0, lb_box_info(false, 1, 0, 200.0f, false, 2, 5, 6, 7, 8), 2);
	lb_kids(&kids->children[1])->children[0] = lb_rect("CA", &ca, 10, 6, 0, 0);
	lb_kids(&kids->children[1])->children[1] = lb_rect("CB", &pb, 20, 4, 0, 0);
	/* BoxV5 / BoxH5: 配置 5 is 1 in both directions. */
	kids->children[2] = lb_box("BoxV5", &p0, 1, 5, 2);
	lb_kids(&kids->children[2])->children[0] = lb_rect("VA", &pb, 10, 6, 0, 0);
	lb_kids(&kids->children[2])->children[1] = lb_rect("VB", &pb, 8, 4, 0, 0);
	kids->children[3] = lb_box("BoxH5", &p0, 2, 5, 2);
	lb_kids(&kids->children[3])->children[0] = lb_rect("HA", &pb, 10, 6, 0, 0);
	lb_kids(&kids->children[3])->children[1] = lb_rect("HB", &pb, 8, 4, 0, 0);
	/* BoxO9: horizontal, 配置 7, box origin 9 (the commonest shape). */
	struct lb_place po9 = { 300, 300, 9 };
	kids->children[4] = lb_box("BoxO9", &po9, 2, 7, 2);
	lb_kids(&kids->children[4])->children[0] = lb_rect("OA", &pb, 10, 6, 0, 0);
	lb_kids(&kids->children[4])->children[1] = lb_rect("OB", &pb, 8, 4, 0, 0);
	/* BoxV3: vertical, 配置 3, children with a right and a top margin. */
	struct lb_place pv3 = { 400, 0, 1 }, va3 = { 50, 50, 1, { 0, 0, 0, 2 } }, vb3 = { 60, 60, 1, { 1, 0, 0, 0 } };
	kids->children[5] = lb_box("BoxV3", &pv3, 1, 3, 2);
	lb_kids(&kids->children[5])->children[0] = lb_rect("WA", &va3, 10, 6, 0, 0);
	lb_kids(&kids->children[5])->children[1] = lb_rect("WB", &vb3, 20, 4, 0, 0);
	int act = lb_load("LayoutBoxAlign", e);
	const char *names[] = { "RA", "RB", "CA", "CB", "VA", "VB", "HA", "HB", "BoxR", "BoxC",
		"OA", "OB", "BoxO9", "WA", "WB", "BoxV3" };
	int n[16];
	for (int i = 0; i < 16; i++)
		n[i] = lb_no(act, names[i]);
	heap_unref(act);
	PE_UpdateComponent(0);
	/* BoxR: W = 13 + 8 + 4 = 25, start 25 - 4 = 21, leftwards. */
	lb_expect_pos(n[0], 19, 0, 3, "BoxR RA");
	lb_expect_pos(n[1], 8, 0, 3, "BoxR RB");
	lb_expect_size(n[8], 25, 6, "BoxR");
	/* BoxC: W = 20 + 7 + 8 = 35, H = 13 + 5 + 6 = 24; x = 35/2, no padding. */
	lb_expect_pos(n[2], 17, 6, 2, "BoxC CA");
	lb_expect_pos(n[3], 17, 14, 2, "BoxC CB");
	lb_expect_size(n[9], 35, 24, "BoxC");
	lb_expect_pos(n[4], 0, 0, 1, "BoxV5 VA");
	lb_expect_pos(n[5], 0, 6, 1, "BoxV5 VB");
	lb_expect_pos(n[6], 0, 0, 1, "BoxH5 HA");
	lb_expect_pos(n[7], 10, 0, 1, "BoxH5 HB");
	/* BoxO9: W = 18, H = 6; start (-18 - 0, -6 + 6). */
	lb_expect_pos(n[10], -18, 0, 7, "BoxO9 OA");
	lb_expect_pos(n[11], -8, 0, 7, "BoxO9 OB");
	lb_expect_size(n[12], 18, 6, "BoxO9");
	/* BoxV3: W = max(10 + 2, 20) = 20, H = 6 + 5; from x = 20, right edges. */
	lb_expect_pos(n[13], 18, 0, 3, "BoxV3 WA");
	lb_expect_pos(n[14], 20, 7, 3, "BoxV3 WB");
	lb_expect_size(n[15], 20, 11, "BoxV3");
	/* BoxC covers (0,0)..(35,24) but is not under the cursor (0x49c5b0);
	 * its child CB, (7,14)..(27,18), is. */
	int in_box = BU_PE("Parts_IsCursorIn", BU_I(n[9]), BU_I(2), BU_I(2), BU_I(1)).i;
	int in_child = BU_PE("Parts_IsCursorIn", BU_I(n[3]), BU_I(10), BU_I(15), BU_I(1)).i;
	LB_CHECK(!in_box, "BoxC is under the cursor at (2,2)");
	LB_CHECK(in_child, "BoxC's child CB is not under the cursor at (10,15)");
	printf("LB3 alignments: %s\n", lb_failures ? "FAIL" : "ok");
}

/* --- LB4 --- */
static void lb_relayout(void)
{
	bu_postlink();
	int n[5];
	lb_count_base(n);
	lb_expect_pos(n[3], 6, 5, 7, "R3 before");
	/* R2 12 -> 40 wide: W = 67, start -33. */
	BU_PE("Parts_SetPartsRectangleDetectionSize", BU_I(n[2]), BU_I(40), BU_I(14), BU_I(1));
	PE_UpdateComponent(0);
	lb_expect_pos(n[1], -33, 5, 7, "R1 after R2 grows");
	lb_expect_pos(n[2], -15, 7, 7, "R2 after R2 grows");
	lb_expect_pos(n[3], 20, 5, 7, "R3 after R2 grows");
	lb_expect_pos(n[4], 22, 7, 7, "R4 after R2 grows");
	lb_expect_size(n[0], 67, 14, "CountBox after R2 grows");
	/* Hovered R2 (on-cursor 30x30) still counts 40x14; R4 13 wide: W = 68. */
	parts_set_state(parts_get(n[2]), PARTS_STATE_HOVERED);
	LB_CHECK(parts_get(n[2])->state == PARTS_STATE_HOVERED, "R2 is not in its on-cursor state");
	BU_PE("Parts_SetPartsRectangleDetectionSize", BU_I(n[4]), BU_I(13), BU_I(14), BU_I(1));
	PE_UpdateComponent(0);
	lb_expect_pos(n[2], -16, 7, 7, "hovered R2");
	lb_expect_pos(n[3], 19, 5, 7, "R3 with R2 hovered");
	lb_expect_size(n[0], 68, 14, "CountBox with R2 hovered");
	/* A margin change: R1 left margin 3, W = 71, start -35. */
	BU_PE("SetComponentMargin", BU_I(n[1]), BU_I(0), BU_I(2), BU_I(3), BU_I(-2));
	PE_UpdateComponent(0);
	lb_expect_pos(n[1], -32, 5, 7, "R1 after its margin changes");
	lb_expect_pos(n[2], -14, 7, 7, "R2 after R1's margin changes");
	lb_expect_size(n[0], 71, 14, "CountBox after R1's margin changes");
	lb_expect_pos(n[3], 21, 5, 7, "R3 after R1's margin changes");
	/* A script moves R3: the box puts it back. */
	BU_PE("SetComponentPos", BU_I(n[3]), BU_F(99), BU_F(99));
	PE_UpdateComponent(0);
	lb_expect_pos(n[3], 21, 5, 7, "R3 after SetComponentPos(99, 99)");
	/* R4's normal state is reset by an empty CG name (0x59c5b0): 0x0.
	 * W = 21 + 40 - 3 + 0 = 58, start -29. */
	int none = heap_alloc_string(make_string("", 0));
	BU_PE("Parts_SetPartsCG", BU_I(n[4]), BU_I(none), BU_I(0), BU_I(1));
	heap_unref(none);
	PE_UpdateComponent(0);
	lb_expect_pos(n[1], -26, 5, 7, "R1 after R4 is emptied");
	lb_expect_pos(n[2], -8, 7, 7, "R2 after R4 is emptied");
	lb_expect_pos(n[3], 27, 5, 7, "R3 after R4 is emptied");
	lb_expect_pos(n[4], 29, 7, 7, "emptied R4");
	lb_expect_size(n[0], 58, 14, "CountBox after R4 is emptied");
	/* The box's own origin mode 5 -> 1: start (0, 14). */
	BU_PE("SetComponentOriginPosMode", BU_I(n[0]), BU_I(1));
	PE_UpdateComponent(0);
	lb_expect_pos(n[1], 3, 12, 7, "R1 with box origin 1");
	lb_expect_pos(n[2], 21, 14, 7, "R2 with box origin 1");
	lb_expect_pos(n[3], 56, 12, 7, "R3 with box origin 1");
	/* A child's origin mode set by a script goes back to the alignment. */
	BU_PE("SetComponentOriginPosMode", BU_I(n[2]), BU_I(1));
	PE_UpdateComponent(0);
	lb_expect_pos(n[2], 21, 14, 7, "R2 after SetComponentOriginPosMode(1)");
	printf("LB4 relayout: %s\n", lb_failures ? "FAIL" : "ok");
}

/* --- LB5 --- */
static void lb_hidden(void)
{
	bu_postlink();
	int n[5];
	lb_count_base(n);
	/* Without R2: W = 18 - 3 + 12 = 27, start -13; R2 placed at the cursor. */
	BU_PE("SetComponentShow", BU_I(n[2]), BU_I(0));
	PE_UpdateComponent(0);
	lb_expect_pos(n[1], -13, 5, 7, "R1 with R2 hidden");
	lb_expect_pos(n[2], 5, 7, 7, "hidden R2");
	lb_expect_pos(n[3], 0, 5, 7, "R3 with R2 hidden");
	lb_expect_pos(n[4], 2, 7, 7, "R4 with R2 hidden");
	lb_expect_size(n[0], 27, 14, "CountBox with R2 hidden");
	BU_PE("SetComponentShow", BU_I(n[2]), BU_I(1));
	parts_set_edit_hidden(parts_get(n[2]), true);
	PE_UpdateComponent(0);
	lb_expect_pos(n[3], 0, 5, 7, "R3 with R2 hidden in the editor");
	parts_set_edit_hidden(parts_get(n[2]), false);
	PE_UpdateComponent(0);
	lb_expect_pos(n[3], 6, 5, 7, "R3 with R2 shown again");
	/* A released child leaves the box like a hidden one. */
	BU_PE("Release", BU_I(n[2]));
	PE_UpdateComponent(0);
	lb_expect_pos(n[1], -13, 5, 7, "R1 after R2 is released");
	lb_expect_pos(n[3], 0, 5, 7, "R3 after R2 is released");
	lb_expect_size(n[0], 27, 14, "CountBox after R2 is released");
	printf("LB5 hidden children: %s\n", lb_failures ? "FAIL" : "ok");
}

/* --- LB6 --- */
static void lb_nested(void)
{
	bu_postlink();
	struct ex_tree *kids;
	struct ex *e = lb_activity("LayoutBoxNested", 2, &kids);
	struct lb_place po = { 100, 50, 1 }, p0 = { 30, 30, 1 }, pi2 = { 30, 30, 1, { 0, 0, 2, 0 } };
	/* Outer: Inner1 {a 10x5, b 6x8}, Inner2 (left 2) {c 7x3}, d 4x4. */
	kids->children[0] = lb_box("Outer", &po, 2, 1, 3);
	struct ex_tree *ok = lb_kids(&kids->children[0]);
	ok->children[0] = lb_box("Inner1", &p0, 2, 1, 2);
	lb_kids(&ok->children[0])->children[0] = lb_rect("a", &p0, 10, 5, 0, 0);
	lb_kids(&ok->children[0])->children[1] = lb_rect("b", &p0, 6, 8, 0, 0);
	ok->children[1] = lb_box("Inner2", &pi2, 2, 1, 1);
	lb_kids(&ok->children[1])->children[0] = lb_rect("c", &p0, 7, 3, 0, 0);
	ok->children[2] = lb_rect("d", &p0, 4, 4, 0, 0);
	/* Outer2 (配置 7): Inner3 vertical {e 5x4, f 3x6}, g 6x12. */
	kids->children[1] = lb_box("Outer2", &po, 2, 7, 2);
	struct ex_tree *o2 = lb_kids(&kids->children[1]);
	o2->children[0] = lb_box("Inner3", &p0, 1, 1, 2);
	lb_kids(&o2->children[0])->children[0] = lb_rect("e", &p0, 5, 4, 0, 0);
	lb_kids(&o2->children[0])->children[1] = lb_rect("f", &p0, 3, 6, 0, 0);
	o2->children[1] = lb_rect("g", &p0, 6, 12, 0, 0);
	int act = lb_load("LayoutBoxNested", e);
	const char *names[] = { "Outer", "Inner1", "Inner2", "a", "b", "c", "d", "Outer2", "Inner3", "e", "f", "g" };
	int n[12];
	for (int i = 0; i < 12; i++)
		n[i] = lb_no(act, names[i]);
	heap_unref(act);
	PE_UpdateComponent(0);
	lb_expect_size(n[1], 16, 8, "Inner1");
	lb_expect_size(n[2], 7, 3, "Inner2");
	lb_expect_size(n[0], 29, 8, "Outer");
	lb_expect_pos(n[1], 0, 0, 1, "Inner1");
	lb_expect_pos(n[2], 18, 0, 1, "Inner2");
	lb_expect_pos(n[6], 25, 0, 1, "d");
	lb_expect_pos(n[3], 0, 0, 1, "a");
	lb_expect_pos(n[4], 10, 0, 1, "b");
	lb_expect_pos(n[5], 0, 0, 1, "c");
	/* Outer2: W = 5 + 6, H = 12, children's bottom at 12; Inner3 now has
	 * origin 7, so its own start is (0, -10). */
	lb_expect_size(n[8], 5, 10, "Inner3");
	lb_expect_pos(n[8], 0, 12, 7, "Inner3");
	lb_expect_pos(n[11], 5, 12, 7, "g");
	lb_expect_pos(n[9], 0, -10, 1, "e");
	lb_expect_pos(n[10], 0, -6, 1, "f");
	/* An inner child grows: the outer box follows. */
	BU_PE("Parts_SetPartsRectangleDetectionSize", BU_I(n[5]), BU_I(17), BU_I(3), BU_I(1));
	PE_UpdateComponent(0);
	lb_expect_size(n[2], 17, 3, "Inner2 after c grows");
	lb_expect_pos(n[6], 35, 0, 1, "d after c grows");
	printf("LB6 nested boxes: %s\n", lb_failures ? "FAIL" : "ok");
}

/* --- LB7 --- */
static void lb_free(void)
{
	bu_postlink();
	struct ex_tree *kids;
	struct ex *e = lb_activity("LayoutBoxFree", 2, &kids);
	struct lb_place p1 = { 11, 22, 1, { 3, 3, 3, 3 } }, p2 = { 40, 50, 5 }, pf = { 7, 8, 1 };
	kids->children[0] = lb_rect("Top", &p1, 10, 10, 0, 0);
	kids->children[1] = lb_box("FreeBox", &pf, 0, 7, 2);
	lb_kids(&kids->children[1])->children[0] = lb_rect("FA", &p2, 10, 10, 0, 0);
	lb_kids(&kids->children[1])->children[1] = lb_rect("FB", &p1, 4, 4, 0, 0);
	int act = lb_load("LayoutBoxFree", e);
	int root = lb_no(act, ""), top = lb_no(act, "Top"), fbox = lb_no(act, "FreeBox");
	int fa = lb_no(act, "FA"), fb = lb_no(act, "FB");
	heap_unref(act);
	PE_UpdateComponent(0);
	PE_UpdateComponent(0);
	int type = BU_PE("GetLayoutBoxLayoutType", BU_I(root)).i;
	LB_CHECK(type == 0, "root layout type %d, want 0", type);
	lb_expect_pos(top, 11, 22, 1, "root child");
	lb_expect_pos(fbox, 7, 8, 1, "free box");
	lb_expect_pos(fa, 40, 50, 5, "free box child FA");
	lb_expect_pos(fb, 11, 22, 1, "free box child FB");
	printf("LB7 free boxes keep positions: %s\n", lb_failures ? "FAIL" : "ok");
}

/* --- LB8 --- */
static void lb_v13(void)
{
	bu_postlink();
	ain->version = 13;
	const int B = 9801, A = 9802, H = 9803, C = 9804;
	parts_get(B); parts_get(A); parts_get(H); parts_get(C);
	PE_SetLayoutBoxLayoutType(B, 2);
	PE_SetLayoutBoxAlign(B, 5);
	PE_set_layoutbox_padding(B, 1, 2, 3, 4);
	PE_SetParentPartsNumber(A, B);
	PE_SetParentPartsNumber(H, B);
	PE_SetParentPartsNumber(C, B);
	PE_SetPartsRectangleDetectionSize(A, 10, 6, 1);
	PE_SetPartsRectangleDetectionSize(H, 8, 4, 1);
	PE_SetPartsRectangleDetectionSize(C, 12, 8, 1);
	PE_SetComponentMargin(A, 0, 0, 1, 2);
	PE_SetComponentMargin(C, 1, 1, 0, 0);
	PE_SetShow(H, false);
	PE_UpdateComponent(0);
	PE_UpdateComponent(0);
	/* The previous code: centre padding -right/-bottom, every child advances. */
	lb_expect_pos(A, 16, 4, 5, "v13 A");
	lb_expect_pos(H, 29, 4, 5, "v13 hidden child");
	lb_expect_pos(C, 37, 4, 5, "v13 C");
	int w = PE_GetPartsWidth(B, 1);
	LB_CHECK(w == 0, "v13 box width %d, want 0 (not written back)", w);
	ain->version = 14;
	printf("LB8 v13 layout unchanged: %s\n", lb_failures ? "FAIL" : "ok");
}

/* --- LB9 --- */
static void lb_unbuilt_sizes(void)
{
	bu_postlink();
	struct ex_tree *kids;
	struct ex *e = lb_activity("LayoutBoxSizes", 1, &kids);
	struct lb_place p0 = { 0, 0, 1 }, px = { 33, 44, 1 };
	kids->children[0] = lb_box("VBox", &p0, 1, 1, 4);
	struct ex_tree *k = lb_kids(&kids->children[0]);
	k->children[0] = lb_uc("U1", &px);
	k->children[1] = lb_construction("K1", &px, 30, 12);
	k->children[2] = lb_uc("U2", &px);
	k->children[3] = lb_rect("R", &px, 4, 4, 0, 0);
	int act = lb_load("LayoutBoxSizes", e);
	int vbox = lb_no(act, "VBox"), u1 = lb_no(act, "U1"), k1 = lb_no(act, "K1");
	int u2 = lb_no(act, "U2"), r = lb_no(act, "R");
	heap_unref(act);
	lb_expect_unbuilt(k1, "LB9 K1");
	/* U2's content: a free root with a 50x20 rect at (5,5) and a hidden one. */
	struct ex *c = lb_activity("LayoutBoxUcContent", 2, &kids);
	struct lb_place pc = { 5, 5, 1 }, ph = { 0, 0, 1, .hidden = true };
	kids->children[0] = lb_rect("Content", &pc, 50, 20, 0, 0);
	kids->children[1] = lb_rect("HiddenContent", &ph, 100, 100, 0, 0);
	int cact = lb_load("LayoutBoxUcContent", c);
	int croot = lb_no(cact, "");
	heap_unref(cact);
	PE_UpdateComponent(0);
	/* U1 and U2 without content 200x200, K1 its canvas 30x12. */
	lb_expect_pos(u1, 0, 0, 1, "U1");
	lb_expect_pos(k1, 0, 200, 1, "K1");
	lb_expect_pos(u2, 0, 212, 1, "U2");
	lb_expect_pos(r, 0, 412, 1, "R before U2 has content");
	BU_PE("Parts_SetParentPartsNumber", BU_I(croot), BU_I(u2));
	PE_UpdateComponent(0);
	lb_expect_pos(r, 0, 237, 1, "R after U2's content (55x25)");
	lb_expect_size(vbox, 200, 241, "VBox");
	printf("LB9 unbuilt sizes: %s\n", lb_failures ? "FAIL" : "ok");
}

/* A horizontal 配置 1 box at (0,0) with a text and a 5x5 rect. */
static void lb_pair(struct ex_tree *slot, const char *box, struct ex_tree first, const char *rect)
{
	struct lb_place p0 = { 0, 0, 1 }, pr = { 77, 88, 1 };
	*slot = lb_box(box, &p0, 2, 1, 2);
	lb_kids(slot)->children[0] = first;
	lb_kids(slot)->children[1] = lb_rect(rect, &pr, 5, 5, 0, 0);
}

/* --- LB10 --- */
static void lb_text_children(void)
{
	td_setup();
	bool grid = gfx_text_cn_gdi();
	struct ex_tree *kids;
	struct ex *e = lb_activity("LayoutBoxText", grid ? 7 : 6, &kids);
	struct lb_place pt = { 33, 44, 1 };
	lb_pair(&kids->children[0], "TA", lb_text("ta", &pt, "ABC", 14, 0, 0), "ra");
	lb_pair(&kids->children[1], "TB", lb_text("tb", &pt, "ABC", 14, 0, -4), "rb");
	lb_pair(&kids->children[2], "TC", lb_text("tc", &pt, "A", 14, 0, 0), "rc");
	lb_pair(&kids->children[3], "TD", lb_text("td", &pt, "A", 14, 0, -4), "rd");
	lb_pair(&kids->children[4], "TE", lb_text("te", &pt, "AB", 14, 2, -4), "re");
	/* TV: vertical; a 14 px text with 縁取り 2 is 14 + 2 * 2 high (0x5bce00). */
	struct lb_place pv = { 500, 0, 1 }, pr = { 77, 88, 1 };
	kids->children[5] = lb_box("TV", &pv, 1, 1, 2);
	lb_kids(&kids->children[5])->children[0] = lb_text("tv", &pt, "AB", 14, 2, 0);
	lb_kids(&kids->children[5])->children[1] = lb_rect("rv", &pr, 5, 5, 0, 0);
	if (grid) {
		/* CountBase with its texts; 14x24 rects stand for the one-digit
		 * numerals. GBK text, so only under the GBK rule. */
		struct lb_place pb = { 192, 135, 5 };
		kids->children[6] = lb_box("CountText", &pb, 2, 7, 4);
		struct ex_tree *k = lb_kids(&kids->children[6]);
		struct lb_place p1 = { -59, 12, 7, { 0, 2, 0, -2 } }, p2 = { -5, 14, 7 },
			p3 = { 19, 12, 7, { 0, 2, -5, -6 } }, p4 = { 31, 14, 7 };
		k->children[0] = lb_text("Caption", &p1, "\xc8\xcb\xb2\xc4", 14, 2, -4);	/* 人材 */
		k->children[1] = lb_rect("Digit1", &p2, 14, 24, 0, 0);
		k->children[2] = lb_text("Slash", &p3, "\xa3\xaf", 16, 1.5f, -4);		/* ／ */
		k->children[3] = lb_rect("Digit2", &p4, 14, 24, 0, 0);
	}
	int act = lb_load("LayoutBoxText", e);
	int ta = lb_no(act, "ta"), tc = lb_no(act, "tc"), boxa = lb_no(act, "TA"), boxb = lb_no(act, "TB"),
		boxc = lb_no(act, "TC");
	int ra = lb_no(act, "ra"), rb = lb_no(act, "rb"), rc = lb_no(act, "rc"), rd = lb_no(act, "rd"),
		re = lb_no(act, "re"), rv = lb_no(act, "rv"), boxv = lb_no(act, "TV");
	int cap = 0, d1 = 0, sl = 0, d2 = 0, cbox = 0;
	if (grid) {
		cap = lb_no(act, "Caption"); d1 = lb_no(act, "Digit1"); sl = lb_no(act, "Slash");
		d2 = lb_no(act, "Digit2"); cbox = lb_no(act, "CountText");
	}
	heap_unref(act);
	PE_UpdateComponent(0);
	int xa = lb_x(ra), xb = lb_x(rb), xc = lb_x(rc), xd = lb_x(rd);
	/* One character: no spacing. Three: three cells, and two spacings. */
	LB_CHECK(xc > 0, "the rect after \"A\" is at x %d", xc);
	LB_CHECK(xd == xc, "after \"A\" with 字間隔 -4: x %d, want %d (no spacing after the only character)", xd, xc);
	LB_CHECK(xa == 3 * xc, "after \"ABC\": x %d, want 3 cells = %d", xa, 3 * xc);
	LB_CHECK(xb == xa - 8, "after \"ABC\" with 字間隔 -4: x %d, want %d (two spacings)", xb, xa - 8);
	LB_CHECK(lb_width(boxa) == xa + 5 && lb_width(boxb) == xa - 8 + 5, "box widths %d and %d, want %d and %d",
		 lb_width(boxa), lb_width(boxb), xa + 5, xa - 3);
	int hv = BU_PE("Parts_GetPartsHeight", BU_I(boxv), BU_I(1)).i;
	LB_CHECK(lb_y(rv) == 18 && hv == 23, "under a 14 px text with 縁取り 2: y %d box height %d, want 18 and 23",
		 lb_y(rv), hv);
	if (grid) {
		/* 0x69b9d0: a half-width cell is (size + 1) / 2 + 2 * ceil(縁取り). */
		LB_CHECK(xc == 7 && lb_x(re) == 18, "GBK grid: \"A\" %d (want 7), \"AB\" 縁 2 字間隔 -4 %d (want 18)",
			 xc, lb_x(re));
		/* 人材 2 * 18 - 4 = 32, ／ 16 + 4 = 20: W = 30 + 14 + 9 + 14 = 67,
		 * start -33; H = 24 (the rects), bottoms at 12 less the bottom
		 * margin. The original shows 30 px from 人材 to the first digit
		 * and 23 px between the digits. */
		lb_expect_pos(cap, -33, 10, 7, "Caption");
		lb_expect_pos(d1, -3, 12, 7, "Digit1");
		lb_expect_pos(sl, 6, 10, 7, "Slash");
		lb_expect_pos(d2, 20, 12, 7, "Digit2");
		LB_CHECK(lb_width(cbox) == 67, "CountText width %d, want 67", lb_width(cbox));
	} else {
		printf("  (cell widths and the CountBase texts need the GBK grid: XS4_PROBE_GBK=1)\n");
	}
	/* The script replaces the text: the box follows after one update. */
	int abc = heap_alloc_string(make_string("ABC", 3)), none = heap_alloc_string(make_string("", 0));
	BU_PE("Parts_SetText", BU_I(tc), BU_I(abc), BU_I(1));
	PE_UpdateComponent(0);
	LB_CHECK(lb_x(rc) == xa, "after Parts_SetText \"ABC\": x %d, want %d", lb_x(rc), xa);
	BU_PE("Parts_SetText", BU_I(tc), BU_I(none), BU_I(1));
	PE_UpdateComponent(0);
	LB_CHECK(lb_x(rc) == 0 && lb_width(boxc) == 5, "after an empty text: x %d box width %d, want 0 and 5",
		 lb_x(rc), lb_width(boxc));
	heap_unref(abc);
	heap_unref(none);
	/* 0x5bf8c0: the surface area limits the size. */
	BU_PE("Parts_SetPartsTextSurfaceArea", BU_I(ta), BU_I(0), BU_I(0), BU_I(10), BU_I(100), BU_I(1));
	PE_UpdateComponent(0);
	LB_CHECK(lb_x(ra) == 10, "with a 10 px wide surface area: x %d, want 10", lb_x(ra));
	printf("LB10 text children: %s\n", lb_failures ? "FAIL" : "ok");
}

/* --- LB11 --- */
static void lb_wrap(void)
{
	bu_postlink();
	struct ex_tree *kids;
	struct ex *e = lb_activity("LayoutBoxWrap", 3, &kids);
	struct lb_place pb = { 640, 137, 5 }, p0 = { 0, 0, 1 };
	struct lb_place m[4] = { { 9, 9, 1 }, { 9, 9, 1, { 12, 0, 0, 0 } }, { 9, 9, 1, { 0, 0, 20, 0 } },
		{ 9, 9, 1, { 12, 0, 20, 0 } } };
	const char *va[4] = { "V1", "V2", "V3", "V4" }, *vb[4] = { "X1", "X2", "X3", "X4" };
	/* WrapV: SceneReplaySelection's Layout. Rows 44 and 44 + 12: 100, not
	 * above 100. WrapX: the second button one pixel taller. */
	kids->children[0] = lb_node("WrapV", &pb, lb_box_info(false, 1, 1, 100.0f, false, 1, 0, 0, 0, 0), 4);
	kids->children[1] = lb_node("WrapX", &pb, lb_box_info(false, 1, 1, 100.0f, false, 1, 0, 0, 0, 0), 4);
	for (int i = 0; i < 4; i++) {
		lb_kids(&kids->children[0])->children[i] = lb_rect(va[i], &m[i], 402, 44, 0, 0);
		lb_kids(&kids->children[1])->children[i] = lb_rect(vb[i], &m[i], 402, i == 1 ? 45 : 44, 0, 0);
	}
	/* WrapH: horizontal, 配置 4, size 30: wrapping makes 4 the alignment 1. */
	kids->children[2] = lb_node("WrapH", &p0, lb_box_info(false, 2, 1, 30.0f, false, 4, 0, 0, 0, 0), 3);
	const char *h[3] = { "H1", "H2", "H3" };
	for (int i = 0; i < 3; i++)
		lb_kids(&kids->children[2])->children[i] = lb_rect(h[i], &m[0], 12, 10, 0, 0);
	int act = lb_load("LayoutBoxWrap", e);
	int nv[4], nx[4], nh[3];
	for (int i = 0; i < 4; i++) {
		nv[i] = lb_no(act, va[i]);
		nx[i] = lb_no(act, vb[i]);
	}
	for (int i = 0; i < 3; i++)
		nh[i] = lb_no(act, h[i]);
	int wv = lb_no(act, "WrapV"), wx = lb_no(act, "WrapX"), wh = lb_no(act, "WrapH");
	heap_unref(act);
	PE_UpdateComponent(0);
	/* W = 402 + 422, H = 100: start (-412, -50). */
	lb_expect_pos(nv[0], -412, -50, 1, "WrapV 1");
	lb_expect_pos(nv[1], -412, 6, 1, "WrapV 2 (ends at 100: same column)");
	lb_expect_pos(nv[2], 10, -50, 1, "WrapV 3");
	lb_expect_pos(nv[3], 10, 6, 1, "WrapV 4");
	lb_expect_size(wv, 824, 100, "WrapV");
	/* 44 + 57 = 101: columns {1}, {2}, {3, 4}; W = 402 + 402 + 422. */
	lb_expect_pos(nx[0], -613, -50, 1, "WrapX 1");
	lb_expect_pos(nx[1], -211, -38, 1, "WrapX 2 (ends at 101: next column)");
	lb_expect_pos(nx[2], 211, -50, 1, "WrapX 3");
	lb_expect_pos(nx[3], 211, 6, 1, "WrapX 4");
	lb_expect_size(wx, 1226, 100, "WrapX");
	lb_expect_pos(nh[0], 0, 0, 1, "WrapH 1");
	lb_expect_pos(nh[1], 12, 0, 1, "WrapH 2");
	lb_expect_pos(nh[2], 0, 10, 1, "WrapH 3 (36 > 30: next row)");
	lb_expect_size(wh, 24, 20, "WrapH");
	printf("LB11 wrapping: %s\n", lb_failures ? "FAIL" : "ok");
}

/* --- LB12 --- */
static void lb_save_load(void)
{
	td_setup();
	struct ex_tree *kids;
	struct ex *e = lb_activity("LayoutBoxSave", 2, &kids);
	struct lb_place p0 = { 0, 0, 1 }, px = { 33, 44, 1 };
	kids->children[0] = lb_box("SBox", &p0, 1, 1, 2);
	lb_kids(&kids->children[0])->children[0] = lb_construction("SK", &px, 30, 12);
	lb_kids(&kids->children[0])->children[1] = lb_rect("SR", &px, 4, 4, 0, 0);
	/* 太さ 0.4 without 縁取り: under the GBK grid a cell is 7 + 2 * 1. */
	lb_pair(&kids->children[1], "SBoxT", lb_text_w("st", &px, "AB", 14, 0.4f, 0, 0), "srt");
	int act = lb_load("LayoutBoxSave", e);
	int box = lb_no(act, "SBox"), k = lb_no(act, "SK"), r = lb_no(act, "SR"), rt = lb_no(act, "srt");
	heap_unref(act);
	lb_expect_unbuilt(k, "LB12 SK");
	PE_UpdateComponent(0);
	lb_expect_pos(r, 0, 12, 1, "SR under the 30x12 canvas");
	lb_expect_size(box, 30, 16, "SBox");
	int xt = lb_x(rt);
	LB_CHECK(xt > 0, "the rect after the 太さ 0.4 text is at x %d", xt);
	if (gfx_text_cn_gdi())
		LB_CHECK(xt == 18, "GBK grid: \"AB\" with 太さ 0.4 is %d wide, want 2 * (7 + 2) = 18", xt);
	struct page *saved = NULL;
	LB_CHECK(PE_Save(&saved), "PE_Save failed");
	if (!saved)
		return;
	LB_CHECK(ca_save_version(saved) >= 6, "the save is XPE v%d: no canvas size before v6", ca_save_version(saved));
	LB_CHECK(PE_Load(&saved), "PE_Load failed");
	PE_UpdateComponent(0);
	lb_expect_pos(k, 0, 0, 1, "SK after loading");
	lb_expect_pos(r, 0, 12, 1, "SR after loading");
	lb_expect_size(box, 30, 16, "SBox after loading");
	LB_CHECK(lb_x(rt) == xt, "the rect after the 太さ 0.4 text is at x %d after loading, want %d", lb_x(rt), xt);
	ca_free_buffer(saved);
	printf("LB12 save and load: %s\n", lb_failures ? "FAIL" : "ok");
}

/* --- LB13 --- */
static void lb_numeral_children(void)
{
	cn_gl_setup();
	bool grid = gfx_text_cn_gdi();
	struct ex_tree *kids;
	struct ex *e = lb_activity("LayoutBoxNumeral", 2, &kids);
	struct lb_place pn = { 33, 44, 1 };
	lb_pair(&kids->children[0], "NA", lb_numeral("na", &pn, 19, 2, 0), "rna");
	lb_pair(&kids->children[1], "NB", lb_numeral("nb", &pn, 19, 2, -5), "rnb");
	int act = lb_load("LayoutBoxNumeral", e);
	int box_a = lb_no(act, "NA"), box_b = lb_no(act, "NB"), na = lb_no(act, "na"), nb = lb_no(act, "nb");
	int ra = lb_no(act, "rna"), rb = lb_no(act, "rnb");
	heap_unref(act);
	BU_PE("Parts_SetNumeralNumber", BU_I(na), BU_I(3), BU_I(1));
	BU_PE("Parts_SetNumeralNumber", BU_I(nb), BU_I(3), BU_I(1));
	PE_UpdateComponent(0);
	/* One digit has no spacing: the same cell with 字間隔 0 and -5. */
	int cell = lb_x(ra);
	LB_CHECK(cell > 0 && lb_x(rb) == cell, "after \"3\": rects at x %d (字間隔 0) and %d (-5), want the same cell",
		 cell, lb_x(rb));
	if (grid)
		LB_CHECK(cell == 14, "GBK grid: a 19 px digit with 縁取り 2 is %d wide, want 10 + 2 * 2 = 14", cell);
	LB_CHECK(lb_width(box_a) == cell + 5 && lb_width(box_b) == cell + 5, "after \"3\": boxes %d and %d wide, want %d",
		 lb_width(box_a), lb_width(box_b), cell + 5);
	BU_PE("Parts_SetNumeralNumber", BU_I(na), BU_I(100), BU_I(1));
	BU_PE("Parts_SetNumeralNumber", BU_I(nb), BU_I(100), BU_I(1));
	PE_UpdateComponent(0);
	/* Three cells, and two spacings of -5 between them. */
	LB_CHECK(lb_x(ra) == 3 * cell, "after \"100\" (字間隔 0): rect at x %d, want 3 * %d", lb_x(ra), cell);
	LB_CHECK(lb_x(rb) == 3 * cell - 10, "after \"100\" (字間隔 -5): rect at x %d, want 3 * %d - 10", lb_x(rb), cell);
	LB_CHECK(lb_width(box_a) == lb_x(ra) + 5 && lb_width(box_b) == lb_x(rb) + 5,
		 "after \"100\": boxes %d and %d wide, want %d and %d", lb_width(box_a), lb_width(box_b),
		 lb_x(ra) + 5, lb_x(rb) + 5);
	printf("LB13 numeral children: %s\n", lb_failures ? "FAIL" : "ok");
}

static int lb_fork(const char *name, void (*fn)(void))
{
	fflush(stdout);
	pid_t pid = fork();
	if (pid == 0) {
		lb_failures = 0;
		alarm(60);
		fn();
		fflush(stdout);
		_exit(lb_failures ? 1 : 0);
	}
	int status = 0;
	waitpid(pid, &status, 0);
	bool ok = WIFEXITED(status) && WEXITSTATUS(status) == 0;
	if (!ok)
		printf("%s FAILED (%s %d)\n", name, WIFEXITED(status) ? "exit" : "signal",
		       WIFEXITED(status) ? WEXITSTATUS(status) : WTERMSIG(status));
	return ok ? 0 : 1;
}

static int test_layout_box(void)
{
	void (*cases[])(void) = { lb_attributes, lb_horizontal, lb_alignments, lb_relayout, lb_hidden,
		lb_nested, lb_free, lb_v13, lb_unbuilt_sizes, lb_text_children, lb_wrap, lb_save_load };
	const char *names[] = { "LB1", "LB2", "LB3", "LB4", "LB5", "LB6", "LB7", "LB8", "LB9", "LB10",
		"LB11", "LB12" };
	int failed = 0;
	for (int i = 0; i < 12; i++)
		failed += lb_fork(names[i], cases[i]);
	/* LB13 draws: the construction mode's GL setup and its environment check. */
	int r = cn_fork_case("LB13", lb_numeral_children, &lb_failures);
	failed += r == 1;
	if (r == 2) {
		printf("LAYOUT-BOX ENVIRONMENT FAILURE: LB13 could not run, %d of 13 cases failed\n", failed);
		return 2;
	}
	printf("LAYOUT-BOX %s: %d of 13 cases failed\n", failed ? "FAIL" : "PASS", failed);
	return failed ? 1 : 0;
}
