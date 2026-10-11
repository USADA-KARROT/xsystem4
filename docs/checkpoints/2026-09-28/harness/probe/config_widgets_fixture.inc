/* config-widgets: the check boxes, the radio boxes and the sliders of the
 * settings pages (Scene/90_System/Config, config::detail::CConfigView), the
 * system menu and the dialogs, as the original engine does them (static
 * disassembly only):
 *   - a 檢查ボックス (EPartsType 1, 0x52d000) is a widget with two inner
 *     parts. The box shows a CG per state: ＣＧ名 (or 覆蓋ＣＧ名) with ／普通,
 *     ／オン, ／ダウン; for a checked box ＣＧ名 with ／檢查 (or 覆蓋檢查ＣＧ
 *     名) and the same; ／無効 in all three states while the box is not
 *     enabled; nothing without a ＣＧ名 (0x529c40 .., 0x529dc0 .., 0x52e8a0).
 *     The text is a surface of bw + tw by max(bh, th), cleared to alpha 0,
 *     with the text copied to (bw, th < bh ? (bh - th) / 2 : 0) by command 8
 *     (0x52e630); the box lies at H / 2 - bh / 2 on it (0x52d340). (tw, th)
 *     is the text's size by 0x528aa0: a line is the size made even plus 2e
 *     high, a character adds the character spacing and its cell, and at the
 *     end one character spacing is taken off the width and one line spacing
 *     off the height;
 *   - the one way to check a box (0x52f030): nothing while it is not
 *     enabled; a value that changes is kept and told to the radio boxes
 *     watching the box, which may set it back; if it then still differs from
 *     what it was, the scripts get ChangedFlg (message 23, one bool). The
 *     script's CheckBoxChecked and a click (0x52cdd0: the other value) both
 *     come here;
 *   - a ラジオ按鈕ボックス (EPartsType 9, 0x56c3c0) does not handle what it
 *     changes itself; a member that was unchecked is checked again; one that
 *     was checked sends the box's own Changed (22) and unchecks every other
 *     member that exists;
 *   - a 橫滾動條 (EPartsType 3) is the 豎滾動條's widget (0x573610), with the
 *     same amounts; SetHScrollbarScrollRate keeps the rate within 0 and 1 as
 *     it was given and the position is the largest position times it,
 *     truncated (0x5923e0 -> 0x542400); GetHScrollbarScrollRate is that rate
 *     (0x5924d0). SetHScrollbarScrollPos keeps the position within 0 and the
 *     largest one and the rate is the position over that (0x5923b0 ->
 *     0x542360); GetHScrollbarScrollPos is that position (0x5924b0). The bar
 *     is updated as the vertical one (0x5738e0): a position or a rate that
 *     is not the one last reported is sent as Scroll (message 21: the
 *     position, the largest position), and what the bar was loaded with
 *     counts as reported (0x574bc0);
 *   - all of these library functions take their widget by a lookup that
 *     makes what is missing (0x53da40, 0x53dba0 -> 0x53d920): a number of 0
 *     or less has none; any other number without a component gets one, and a
 *     component of another type becomes a check box, or a bar, with the
 *     constructor's values (0x529620: enabled, not checked; 0x541e30: 100 by
 *     1 at 0).
 * The settings write every box and slider of the open page from the
 * configuration before each update and read it back into the configuration
 * after it. Before the fix IsCheckBoxChecked and GetHScrollbarScrollRate
 * were stubs returning false and 0, so opening a page cleared its part of
 * the configuration; no box was drawn or could be clicked; and
 * SystemService kept one mouse cursor setting where the input page has
 * three (config::detail::SetHideStepMessageMouseCursor writes type 2).
 * SetHScrollbarScrollPos was a stub as well and a horizontal bar sent no
 * event, so the sound page's three voice sliders and their 初始化 buttons did
 * nothing; and SystemService numbered its window settings as older engines
 * do, so the page's check box for type 2 (minimize a deactivated full-screen
 * window) switched the vertical sync and saved that.
 *
 * Here the box and the text are drawn into the three states of the parts
 * itself. The CGs are PNG files made in memory (cg_assets_shadow.inc), the
 * glyphs come from backlog-text's pattern font, and the model lays the cells
 * out with that fixture's command 8.
 *
 * Cases (each in its own fork):
 *   W1 boxes without a text through the pactex loader: the three CGs, the
 *       size, that they can be clicked; 檢查狀態 1 loads checked and sends
 *       nothing; 覆蓋ＣＧ名 and 覆蓋檢查ＣＧ名; a state whose CG is missing
 *       shows the normal one (this port's choice); no ＣＧ名 builds nothing.
 *   W2 boxes with a text on the glyph grid: every pixel of the three states
 *       against the model, four by hand; a text higher than the box (the box
 *       at H / 2 - bh / 2, which is not (H - bh) / 2); two lines, a negative
 *       character spacing, an edge; 按鈕模式 leaves the text out; the font of
 *       the dialogs' "do not show again" (size 17, a weight of 0.3, a
 *       character spacing of -3).
 *   W3 CheckBoxChecked and IsCheckBoxChecked through their AIN
 *       declarations: the same value changes and sends nothing; another one
 *       shows the other CGs after the update and sends ChangedFlg with the
 *       parts' event ID; a parts without an event sends nothing;
 *       SetCheckBoxEnable(false) shows ／無効 (／檢查／無効 when checked),
 *       keeps the value against CheckBoxChecked and swallows a click.
 *   W4 a click: MouseClick, then ChangedFlg; on the text as well; the down
 *       state while the button is held; PE_Update draws the new value.
 *   W5 the settings' loop: for some frames the configuration is written to
 *       a box and a slider, the parts are updated, and it is read back: it
 *       stays what it was, a value or a rate is told once when it is new and
 *       never again, and a click in between is what is read.
 *   W6 a radio box loaded before its members: clicking another member,
 *       clicking the checked one, the script unchecking the checked one, the
 *       box's own Changed, a member that was released, a disabled member.
 *   W7 the 橫滾動條: loaded amounts, the rate rules, the position rules;
 *       Scroll for a rate or a position that changed, once; nothing for what
 *       was loaded; the sound page's order (the event, then a position); a
 *       bar without an event; the 豎滾動條 as it was (guard).
 *   W8 what is not there: numbers of 0 and below do nothing; another number
 *       is made a check box or a bar with the constructor's values (by a
 *       value, a rate or a position); a parts of another type becomes one.
 *   W9 PE_Save / PE_Load keep the pixels (XPE version 11); the values are
 *       not saved, and a click after loading does no harm.
 *   W10 engines before v14: the loader builds no check box, keeps no bar's
 *       amounts and no radio box's members, a bar's position is not set and
 *       it sends nothing, and SystemService has the one mouse cursor setting
 *       and its older window settings (2 is the vertical sync, written at
 *       every call); v14 has three cursor settings.
 *   W11 without the glyph grid (the SJIS rule): the box's pixels and the
 *       surface's size as command 8 draws the one line there.
 *   W12 SystemService's window settings in v14, as the original numbers them
 *       (0x4c10c0, 0x4c1160; defaults 0x4c5a30): five types that start as 1,
 *       each kept alone; type 2 leaves the vertical sync flag alone; the scaling
 *       type keeps its number, the others 0 or 1; other types are refused;
 *       a value that does not change writes no file; the values come back
 *       from the file; an older file's "wait_vsync" is not read.
 *   W13 a click that was not applied yet is dropped when its box is
 *       released (and the number taken again), stops being a check box, has
 *       another event ID, or the input is reset; nine presses before one
 *       update.
 * Functions the fix adds are reached through the AIN declarations or by
 * dlsym, so the file builds before the fix.
 */
#include <dlfcn.h>

static int cw_failures;
#define CW_CHECK(c, ...) do { if (!(c)) { printf("  FAIL: "); printf(__VA_ARGS__); printf("\n"); cw_failures++; } } while (0)

#define CW_CHECK_BOX "\x99\x7a\xb2\xe9\xa5\xdc\xa5\xc3\xa5\xaf\xa5\xb9"		/* 檢查ボックス */
#define CW_RADIO_BOX "\xa5\xe9\xa5\xb8\xa5\xaa\xb0\xb4\xe2\x6f\xa5\xdc\xa5\xc3\xa5\xaf\xa5\xb9" /* ラジオ按鈕ボックス */
#define CW_HSCROLL "\x99\x4d\x9d\x4c\x84\xd3\x97\x6c"				/* 橫滾動條 */
#define CW_K_ENABLE "\xd3\xd0\x84\xbf\xa0\xee\x91\x42"				/* 有効狀態 */
#define CW_K_CHECKED "\x99\x7a\xb2\xe9\xa0\xee\x91\x42"				/* 檢查狀態 */
#define CW_K_CG "\xa3\xc3\xa3\xc7\xc3\xfb"					/* ＣＧ名 */
#define CW_K_OVER "\xb8\xb2\xc9\x77"						/* 覆蓋 */
#define CW_K_OVER_CG CW_K_OVER CW_K_CG						/* 覆蓋ＣＧ名 */
#define CW_K_OVER_CHECKED_CG CW_K_OVER "\x99\x7a\xb2\xe9" CW_K_CG		/* 覆蓋檢查ＣＧ名 */
#define CW_K_TEXT "\xce\xc4\xb1\xbe"						/* 文本 */
#define CW_K_BUTTON_MODE "\xb0\xb4\xe2\x6f\xc4\xa3\xca\xbd"			/* 按鈕模式 */
#define CW_K_CHAR_SPACE "\xce\xc4\xd7\xd6\xe9\x67\xb8\xf4"			/* 文字間隔 */
#define CW_K_LINE_SPACE "\xd0\xd0\xe9\x67\xb8\xf4"				/* 行間隔 */
#define CW_K_FONT_SIZE "\xa5\xd5\xa5\xa9\xa5\xf3\xa5\xc8\xa5\xb5\xa5\xa4\xa5\xba" /* フォントサイズ */
#define CW_K_FONT_COLOR "\xa5\xd5\xa5\xa9\xa5\xf3\xa5\xc8\xc9\xab"		/* フォント色 */
#define CW_K_FONT_EDGE "\xa5\xd5\xa5\xa9\xa5\xf3\xa5\xc8\xbf\x46\xc8\xa1\xa4\xea" /* フォント縁取り */
#define CW_K_FONT_BOLD "\xa5\xd5\xa5\xa9\xa5\xf3\xa5\xc8\xcc\xab\xa4\xb5"		/* フォント太さ */
#define CW_K_FONT_EDGE_COLOR CW_K_FONT_EDGE "\xc9\xab"				/* フォント縁取り色 */
#define CW_K_RADIO_LIST "\x99\x7a\xb2\xe9\xa5\xdc\xa5\xc3\xa5\xaf\xa5\xb9\xa5\xea\xa5\xb9\xa5\xc8" /* 檢查ボックスリスト */

#define CW_S_N "\xa3\xaf\xc6\xd5\xcd\xa8"		/* ／普通 */
#define CW_S_ON "\xa3\xaf\xa5\xaa\xa5\xf3"		/* ／オン */
#define CW_S_DOWN "\xa3\xaf\xa5\xc0\xa5\xa6\xa5\xf3"	/* ／ダウン */
#define CW_S_OFF "\xa3\xaf\x9f\x6f\x84\xbf"		/* ／無効 */
#define CW_S_CHK "\xa3\xaf\x99\x7a\xb2\xe9"		/* ／檢查 */

/* --- the CGs --- */
enum {
	CW_N, CW_ON, CW_DOWN, CW_OFF,		/* cwbox */
	CW_CN, CW_CON, CW_CDOWN, CW_COFF,	/* cwbox／檢查 */
	CW_ALT_N, CW_ALT_OFF,			/* cwalt: no ／オン and no ／ダウン */
	CW_ALTC_N, CW_ALTC_ON, CW_ALTC_DOWN,	/* cwaltc */
	CW_SMALL_N, CW_SMALL_ON, CW_SMALL_DOWN,	/* cwsmall, 9 x 9 */
	CW_NR_CG
};
static struct {
	const char *name;
	int w, h;
	uint8_t *px;	/* RGBA */
} cw_cg[CW_NR_CG] = {
	[CW_N] = { "cwbox" CW_S_N, 23, 23 }, [CW_ON] = { "cwbox" CW_S_ON, 23, 23 },
	[CW_DOWN] = { "cwbox" CW_S_DOWN, 23, 23 }, [CW_OFF] = { "cwbox" CW_S_OFF, 23, 23 },
	[CW_CN] = { "cwbox" CW_S_CHK CW_S_N, 23, 23 }, [CW_CON] = { "cwbox" CW_S_CHK CW_S_ON, 23, 23 },
	[CW_CDOWN] = { "cwbox" CW_S_CHK CW_S_DOWN, 23, 23 }, [CW_COFF] = { "cwbox" CW_S_CHK CW_S_OFF, 23, 23 },
	[CW_ALT_N] = { "cwalt" CW_S_N, 23, 23 }, [CW_ALT_OFF] = { "cwalt" CW_S_OFF, 23, 23 },
	[CW_ALTC_N] = { "cwaltc" CW_S_N, 23, 23 }, [CW_ALTC_ON] = { "cwaltc" CW_S_ON, 23, 23 },
	[CW_ALTC_DOWN] = { "cwaltc" CW_S_DOWN, 23, 23 },
	[CW_SMALL_N] = { "cwsmall" CW_S_N, 9, 9 }, [CW_SMALL_ON] = { "cwsmall" CW_S_ON, 9, 9 },
	[CW_SMALL_DOWN] = { "cwsmall" CW_S_DOWN, 9, 9 },
};
static struct probe_cg_file cw_files[CW_NR_CG];

/* A CG's pixel: the red tells the CGs apart, the frame is opaque and the
 * inside has an alpha that is neither 0 nor 255. */
static void cw_cg_pixel(int id, int x, int y, uint8_t *p)
{
	const int w = cw_cg[id].w, h = cw_cg[id].h;
	p[0] = 20 + id * 14;
	p[1] = 10 + x * 9;
	p[2] = 10 + y * 9;
	p[3] = (!x || !y || x == w - 1 || y == h - 1) ? 255 : 40 + (x * 7 + y * 13) % 200;
}

static void cw_install_cgs(void)
{
	for (int i = 0; i < CW_NR_CG; i++) {
		const int w = cw_cg[i].w, h = cw_cg[i].h;
		uint8_t *px = cw_cg[i].px = calloc((size_t)w * h, 4);
		for (int k = 0; k < w * h; k++)
			cw_cg_pixel(i, k % w, k / w, px + k * 4);
		cw_files[i].name = cw_cg[i].name;
		cw_files[i].data = cg_png(w, h, px, true, &cw_files[i].size);
	}
	probe_install_cg_assets(cw_files, CW_NR_CG);
}

/* The GL, the pattern font, the string rule, the input and the CGs. */
static void cw_setup(bool gbk)
{
	bt_gl_setup(gbk);
	br_input_setup();
	cw_install_cgs();
}

/* --- the pactex --- */
struct cw_box {
	const char *cg;		/* ＣＧ名; NULL: no key */
	const char *over, *over_checked;
	const char *text;	/* 文本; NULL: no key */
	int size;		/* フォントサイズ; 0: no key */
	int line_space, char_space;
	float bold;		/* フォント太さ */
	int edge;		/* フォント縁取り */
	int checked;		/* 檢查狀態 */
	bool disabled;		/* 有効狀態 = 0 */
	bool button_mode;
};

static struct ex_tree cw_box_node(const char *name, int x, int y, const struct cw_box *b)
{
	struct lb_place pl = { x, y, 1 };
	struct ex_tree info = bu_branch(BU_INFO, 15);
	unsigned k = 0;
	info.children[k++] = bu_sleaf(BU_TYPE, CW_CHECK_BOX);
	if (b->cg)
		info.children[k++] = bu_sleaf(CW_K_CG, b->cg);
	if (b->over)
		info.children[k++] = bu_sleaf(CW_K_OVER_CG, b->over);
	if (b->over_checked)
		info.children[k++] = bu_sleaf(CW_K_OVER_CHECKED_CG, b->over_checked);
	if (b->text)
		info.children[k++] = bu_sleaf(CW_K_TEXT, b->text);
	if (b->size)
		info.children[k++] = bu_ileaf(CW_K_FONT_SIZE, b->size);
	info.children[k++] = bu_ileaf(CW_K_LINE_SPACE, b->line_space);
	info.children[k++] = bu_ileaf(CW_K_CHAR_SPACE, b->char_space);
	if (b->bold != 0.0f)
		info.children[k++] = bu_leaf(CW_K_FONT_BOLD, (struct ex_value){ .type = EX_FLOAT, .f = b->bold });
	info.children[k++] = bu_ileaf(CW_K_FONT_EDGE, b->edge);
	info.children[k++] = lb_ilist(CW_K_FONT_COLOR, 3, (int[]){ 250, 240, 230 });
	info.children[k++] = lb_ilist(CW_K_FONT_EDGE_COLOR, 3, (int[]){ 9, 8, 7 });
	info.children[k++] = bu_ileaf(CW_K_CHECKED, b->checked);
	if (b->disabled)
		info.children[k++] = bu_ileaf(CW_K_ENABLE, 0);
	if (b->button_mode)
		info.children[k++] = bu_ileaf(CW_K_BUTTON_MODE, 1);
	info.nr_children = k;
	return lb_node(name, &pl, info, 0);
}

/* A radio box naming its check boxes. */
static struct ex_tree cw_radio_node(const char *name, int n, const char **members)
{
	struct lb_place pl = { 0, 0, 1 };
	struct ex_tree info = bu_branch(BU_INFO, 2);
	info.children[0] = bu_sleaf(BU_TYPE, CW_RADIO_BOX);
	info.children[1] = bu_leaf(CW_K_RADIO_LIST, (struct ex_value){ .type = EX_LIST, .list = bu_slist(n, members) });
	return lb_node(name, &pl, info, 0);
}

/* A scroll bar of either kind with the four amounts. */
static struct ex_tree cw_bar_node(const char *name, const char *type, int total, int view, int pos, float rate)
{
	struct lb_place pl = { 0, 0, 1 };
	struct ex_tree info = bu_branch(BU_INFO, 5);
	info.children[0] = bu_sleaf(BU_TYPE, type);
	info.children[1] = bu_ileaf(BT_TOTAL, total);
	info.children[2] = bu_ileaf(BT_VIEW, view);
	info.children[3] = bu_ileaf(BT_POS, pos);
	info.children[4] = bu_leaf(BT_RATE, (struct ex_value){ .type = EX_FLOAT, .f = rate });
	return lb_node(name, &pl, info, 0);
}

/* --- the model --- */
#define CW_FONT_RGB { 250, 240, 230 }
#define CW_EDGE_RGB { 9, 8, 7 }

/* The text's size on the glyph grid (0x528aa0). e is the larger of the
 * weight and the edge, each rounded up and at most the size. */
static int cw_edge(const struct cw_box *b)
{
	const int size = b->size ? b->size : 16;
	int bold = (int)b->bold;
	if ((float)bold < b->bold)
		bold++;
	bold = bold > size ? size : bold;
	const int edge = b->edge > size ? size : b->edge;
	return bold > edge ? bold : edge;
}

static void cw_text_size(const struct cw_box *b, int *w, int *h)
{
	*w = *h = 0;
	if (!b->text || !b->text[0] || b->button_mode)
		return;
	const int size = b->size ? b->size : 16;
	const int e = cw_edge(b);
	const int line = size + (size & 1) + 2 * e;
	int width = 0, row = 0, height = line;
	for (const unsigned char *p = (const unsigned char *)b->text; *p; p++) {
		if (*p == '\n') {
			height += b->line_space + line;
			width = row > width ? row : width;
			row = 0;
			continue;
		}
		const bool wide = *p >= 0x81 && *p <= 0xfe;
		row += b->char_space + (wide ? size : (size + 1) / 2) + 2 * e;
		if (wide && p[1])
			p++;
	}
	*w = (row > width ? row : width) - b->char_space;
	*h = height - b->line_space;
}

/* The surface of a box with a text in one state: alpha 0, the state's CG at
 * (0, H / 2 - bh / 2) and the text by command 8 at (bw, y), bw x bh being
 * the normal CG. To be freed. */
static uint8_t *cw_model(const struct cw_box *b, int cg_normal, int cg_state, int *w, int *h)
{
	int tw, th;
	cw_text_size(b, &tw, &th);
	const int bw = cw_cg[cg_normal].w, bh = cw_cg[cg_normal].h;
	*w = bw + tw;
	*h = bh > th ? bh : th;
	uint8_t *px = calloc((size_t)*w * *h, 4);
	const int by = *h / 2 - bh / 2;
	for (int y = 0; y < cw_cg[cg_state].h; y++)
		for (int x = 0; x < cw_cg[cg_state].w; x++)
			memcpy(px + ((size_t)(by + y) * *w + x) * 4, cw_cg[cg_state].px + (y * cw_cg[cg_state].w + x) * 4, 4);
	const struct bt_style style = { .size = b->size ? b->size : 16, .rgb = CW_FONT_RGB, .bold = b->bold,
		.edge = b->edge, .edge_rgb = CW_EDGE_RGB };
	bt_model_grid(px, *w, *h, true, 8, bw, th < bh ? (bh - th) / 2 : 0, b->text, &style, b->char_space,
		      b->line_space);
	return px;
}

/* The pixels of a state that is w x h (to be freed), or NULL. */
static uint8_t *cw_state_pixels(int no, int state, int w, int h, const char *what)
{
	struct parts *p = no > 0 ? parts_try_get(no) : NULL;
	struct parts_common *c = p ? &p->states[state].common : NULL;
	bool ok = c && c->texture.handle && c->texture.w == w && c->texture.h == h && c->w == w && c->h == h;
	CW_CHECK(ok, "%s: state %d is %dx%d with a %dx%d texture, want %dx%d", what, state + 1, c ? c->w : -1,
		 c ? c->h : -1, c && c->texture.handle ? c->texture.w : 0, c && c->texture.handle ? c->texture.h : 0, w, h);
	return ok ? gfx_get_pixels(&c->texture) : NULL;
}
static void cw_compare(uint8_t *got, int w, int h, const uint8_t *model, const char *what)
{
	int bad = 0;
	for (int i = 0; got && i < w * h && bad < 3; i++) {
		if (memcmp(got + i * 4, model + i * 4, 4)) {
			CW_CHECK(0, "%s (%d,%d) = (%d,%d,%d,%d), want (%d,%d,%d,%d)", what, i % w, i / w, got[i * 4],
				 got[i * 4 + 1], got[i * 4 + 2], got[i * 4 + 3], model[i * 4], model[i * 4 + 1],
				 model[i * 4 + 2], model[i * 4 + 3]);
			bad++;
		}
	}
	free(got);
}
/* A box without a text: its three states are these CGs. */
static void cw_expect_cgs(int no, int n, int on, int down, const char *what)
{
	const int ids[3] = { n, on, down };
	for (int i = 0; i < 3; i++) {
		char label[128];
		snprintf(label, sizeof label, "%s, state %d", what, i + 1);
		const int w = cw_cg[ids[i]].w, h = cw_cg[ids[i]].h;
		cw_compare(cw_state_pixels(no, i, w, h, label), w, h, cw_cg[ids[i]].px, label);
	}
}
/* A box with a text: its three states are the model's. */
static void cw_expect_text_states(int no, const struct cw_box *b, int n, int on, int down, const char *what)
{
	const int ids[3] = { n, on, down };
	for (int i = 0; i < 3; i++) {
		char label[128];
		snprintf(label, sizeof label, "%s, state %d", what, i + 1);
		int w, h;
		uint8_t *model = cw_model(b, n, ids[i], &w, &h);
		cw_compare(cw_state_pixels(no, i, w, h, label), w, h, model, label);
		free(model);
	}
}
static void cw_expect_px(const uint8_t *got, int w, int x, int y, int r, int g, int b, int a, const char *what)
{
	if (!got)
		return;
	const uint8_t *p = got + (y * w + x) * 4;
	CW_CHECK(p[0] == r && p[1] == g && p[2] == b && p[3] == a, "%s (%d,%d) = (%d,%d,%d,%d), want (%d,%d,%d,%d)", what,
		 x, y, p[0], p[1], p[2], p[3], r, g, b, a);
}
/* True when the normal state shows nothing. */
static bool cw_unbuilt(int no)
{
	struct parts *p = no > 0 ? parts_try_get(no) : NULL;
	return p && !p->states[0].common.texture.handle && !p->states[0].common.w && !p->states[0].common.h;
}

/* --- the library, through the AIN declarations --- */
static bool cw_checked(int no) { return BU_PE("IsCheckBoxChecked", BU_I(no)).i; }
static void cw_check(int no, bool v) { BU_PE("CheckBoxChecked", BU_I(no), BU_I(v)); }
static bool cw_enabled(int no) { return BU_PE("IsCheckBoxEnable", BU_I(no)).i; }
static void cw_enable(int no, bool v) { BU_PE("SetCheckBoxEnable", BU_I(no), BU_I(v)); }
static float cw_rate(int no) { return BU_PE("GetHScrollbarScrollRate", BU_I(no)).f; }
static void cw_set_rate(int no, float v) { BU_PE("SetHScrollbarScrollRate", BU_I(no), BU_F(v)); }
static int cw_pos(int no) { return BU_PE("GetHScrollbarScrollPos", BU_I(no)).i; }
static void cw_set_pos(int no, int v) { BU_PE("SetHScrollbarScrollPos", BU_I(no), BU_I(v)); }
static int cw_type(int no) { return BU_PE("GetComponentType", BU_I(no), BU_I(1)).i; }

/* The boxes whose values changed are drawn again (the engine does this in
 * PE_Update, after the input). */
static void cw_update(void)
{
	static bool reported;
	void (*update)(void) = (void (*)(void))dlsym(RTLD_DEFAULT, "parts_checkbox_update");
	if (update)
		update();
	else if (!reported)
		CW_CHECK(0, "no check box update (parts_checkbox_update)");
	reported = true;
}

/* The queued messages, taken off the queue: their number, the first `max`
 * of them in m. v is the first variable. */
struct cw_msg { int type, no, delegate, unique, v; };
static int cw_drain(struct cw_msg *m, int max)
{
	int (*delegate)(void) = (int (*)(void))br_pe_fn("GetMessageDelegateIndex");
	int (*var)(int) = (int (*)(int))br_pe_fn("GetMessageVariableInt");
	int n = 0;
	for (int t; (t = br_msg_type()) >= 0; br_msg_pop()) {
		if (n < max)
			m[n] = (struct cw_msg){ t, br_msg_parts(), delegate(), cp_message_unique(), var(0) };
		n++;
	}
	return n;
}
static void cw_expect_quiet(const char *what)
{
	struct cw_msg m[1] = { { -1, -1 } };
	int n = cw_drain(m, 1);
	CW_CHECK(n == 0, "%s: %d message(s), the first of type %d for parts %d; want none", what, n, m[0].type, m[0].no);
}
/* Exactly these messages, as (type, parts, first variable) triples. */
static void cw_expect_messages(const char *what, int n, const int (*want)[3])
{
	struct cw_msg m[8];
	int got = cw_drain(m, 8);
	bool ok = got == n;
	for (int i = 0; ok && i < n; i++)
		ok = m[i].type == want[i][0] && m[i].no == want[i][1] && (want[i][0] == 22 || m[i].v == want[i][2]);
	if (ok)
		return;
	printf("  FAIL: %s: %d message(s):", what, got);
	for (int i = 0; i < got && i < 8; i++)
		printf(" (%d, %d, %d)", m[i].type, m[i].no, m[i].v);
	printf("; want %d:", n);
	for (int i = 0; i < n; i++)
		printf(" (%d, %d, %d)", want[i][0], want[i][1], want[i][2]);
	printf("\n");
	cw_failures++;
}
#define CW_MESSAGES(what, ...) cw_expect_messages(what, sizeof((int[][3]){ __VA_ARGS__ }) / sizeof(int[3]), \
		(const int[][3]){ __VA_ARGS__ })

/* The bars' part of the update of the parts (in PE_Update, after the input). */
static void cw_bars_update(void)
{
	void (*update)(void) = (void (*)(void))dlsym(RTLD_DEFAULT, "parts_scrollbar_update");
	if (update)
		update();
}

/* Exactly one Scroll (21) is queued, for that bar, with the position and
 * the largest position; with a number of 0, nothing is queued at all. */
static void cw_expect_scroll(const char *what, int no, int pos, int max)
{
	int (*var)(int) = (int (*)(int))br_pe_fn("GetMessageVariableInt");
	int n = 0, type = -1, parts = -1, v0 = -1, v1 = -1;
	for (int t; (t = br_msg_type()) >= 0; br_msg_pop()) {
		if (n++ == 0) {
			type = t;
			parts = br_msg_parts();
			v0 = var(0);
			v1 = var(1);
		}
	}
	if (no <= 0)
		CW_CHECK(n == 0, "%s: %d message(s), the first (%d, %d, %d, %d); want none", what, n, type, parts, v0, v1);
	else
		CW_CHECK(n == 1 && type == 21 && parts == no && v0 == pos && v1 == max,
			 "%s: %d message(s), the first (%d, %d, %d, %d); want one (21, %d, %d, %d)", what, n, type, parts,
			 v0, v1, no, pos, max);
}

/* The left button pressed at (x, y) after a released frame, and released
 * again, as the input sees it outside an update of the parts (system.Peek).
 * The messages stay queued. */
static void cw_press(int x, int y)
{
	extern bool key_state[];
	mouse_set_pos(x, y);
	PE_UpdateComponent(0);
	PE_BeginInput();
	key_state[VK_LBUTTON] = false;
	PE_UpdateInputState(0);
	key_state[VK_LBUTTON] = true;
	PE_UpdateInputState(0);
	key_state[VK_LBUTTON] = false;
	PE_UpdateInputState(0);
	PE_EndInput();
}
/* A press and the update of the parts that follows it. */
static void cw_click(int x, int y)
{
	cw_press(x, y);
	cw_update();
}

static int cw_load(const char *name, struct ex *e)
{
	int act = cn_load(name, e);
	cw_failures += cn_failures;
	cn_failures = 0;
	return act;
}
static int cw_no(int act, const char *name)
{
	int n = bu_number(act, name);
	CW_CHECK(n > 0 && parts_try_get(n), "%s has no parts", name);
	return n > 0 && parts_try_get(n) ? n : 0;
}

#define CW_TEXT_A " \xd2\xbb\xb6\xfe\xc8\xfd"	/* a space and three GBK characters */
#define CW_TEXT_ONE "\xd2\xbb"
#define CW_TEXT_LINES "\xd2\xbb\n\xb6\xfe\xc8\xfd"
/* a space and nine GBK characters, as long as a dialog's "do not show again" */
#define CW_TEXT_DIALOG " \xd2\xbb\xb6\xfe\xc8\xfd\xd2\xbb\xb6\xfe\xc8\xfd\xd2\xbb\xb6\xfe\xc8\xfd"

/* --- W1 --- */
static void cw_plain(void)
{
	cw_setup(true);
	static const struct cw_box plain = { .cg = "cwbox", .text = "" },
		checked = { .cg = "cwbox", .text = "", .checked = 1 },
		over = { .cg = "cwbox", .over = "cwalt", .text = "" },
		over_checked = { .cg = "cwbox", .over = "cwalt", .over_checked = "cwaltc", .checked = 1 },
		over_only = { .cg = "", .over = "cwalt", .over_checked = "cwaltc" },
		no_keys = { 0 },
		two = { .cg = "cwbox", .checked = 2 },
		missing = { .cg = "cwnone" };
	static const struct { const char *name; const struct cw_box *box; } boxes[] = {
		{ "Plain", &plain }, { "Checked", &checked }, { "Over", &over }, { "OverChecked", &over_checked },
		{ "OverOnly", &over_only }, { "NoKeys", &no_keys }, { "Two", &two }, { "Missing", &missing } };
	enum { PLAIN, CHECKED, OVER, OVER_CHECKED, OVER_ONLY, NO_KEYS, TWO, MISSING, NR };
	struct ex_tree *kids;
	struct ex *e = lb_activity("ConfigWidgetsPlain", NR, &kids);
	for (int i = 0; i < NR; i++)
		kids->children[i] = cw_box_node(boxes[i].name, 40 + 60 * i, 100, boxes[i].box);
	int act = cw_load("ConfigWidgetsPlain", e);
	int no[NR];
	bool all = true;
	for (int i = 0; i < NR; i++)
		all = (no[i] = cw_no(act, boxes[i].name)) && all;
	heap_unref(act);
	if (!all)
		return;
	cw_expect_quiet("W1 loading");
	for (int i = 0; i < NR; i++) {
		CW_CHECK(cw_type(no[i]) == 1, "W1 %s: GetComponentType %d, want 1", boxes[i].name, cw_type(no[i]));
		CW_CHECK(cp_clickable(no[i]), "W1 %s cannot be clicked", boxes[i].name);
		CW_CHECK(cw_enabled(no[i]), "W1 %s is not enabled", boxes[i].name);
		bool want = i == CHECKED || i == OVER_CHECKED;
		CW_CHECK(cw_checked(no[i]) == want, "W1 %s: IsCheckBoxChecked %d, want %d", boxes[i].name, cw_checked(no[i]), want);
	}
	cw_expect_cgs(no[PLAIN], CW_N, CW_ON, CW_DOWN, "W1 a box");
	cw_expect_cgs(no[CHECKED], CW_CN, CW_CON, CW_CDOWN, "W1 a checked box");
	/* cwalt has neither ／オン nor ／ダウン: the normal CG stands in. */
	cw_expect_cgs(no[OVER], CW_ALT_N, CW_ALT_N, CW_ALT_N, "W1 the override of a box that is not checked");
	cw_expect_cgs(no[OVER_CHECKED], CW_ALTC_N, CW_ALTC_ON, CW_ALTC_DOWN, "W1 the checked override");
	cw_expect_cgs(no[TWO], CW_N, CW_ON, CW_DOWN, "W1 a checked key of 2");
	CW_CHECK(cw_unbuilt(no[OVER_ONLY]), "W1: a box without a CG name shows its overrides");
	CW_CHECK(cw_unbuilt(no[NO_KEYS]), "W1: a box without keys shows something");
	CW_CHECK(cw_unbuilt(no[MISSING]), "W1: a box whose CG does not exist shows something");
	struct parts *p = parts_try_get(no[PLAIN]);
	CW_CHECK(p->states[0].common.w == 23 && p->states[0].common.h == 23, "W1: the box is %dx%d, want 23x23",
		 p->states[0].common.w, p->states[0].common.h);
	cw_expect_quiet("W1 reading the boxes");
	printf("W1 checked\n");
}

/* --- W2 --- */
static int cw_row_alpha(const uint8_t *px, int w, int y, int x0, int x1)
{
	int n = 0;
	for (int x = x0; px && x < x1; x++)
		n += px[(y * w + x) * 4 + 3] != 0;
	return n;
}

static void cw_text(void)
{
	cw_setup(true);
	static const struct cw_box text = { .cg = "cwbox", .text = CW_TEXT_A, .size = 19, .line_space = 7 },
		tall = { .cg = "cwsmall", .text = CW_TEXT_ONE, .size = 14 },
		odd = { .cg = "cwbox", .text = CW_TEXT_ONE, .size = 14 },
		lines = { .cg = "cwbox", .text = CW_TEXT_LINES, .size = 16, .line_space = 3, .char_space = -2, .edge = 1 },
		button = { .cg = "cwbox", .text = CW_TEXT_A, .size = 19, .line_space = 7, .button_mode = true },
		chk = { .cg = "cwbox", .text = CW_TEXT_A, .size = 19, .line_space = 7, .checked = 1 },
		def = { .cg = "cwbox", .text = CW_TEXT_ONE },
		dialog = { .cg = "cwbox", .text = CW_TEXT_DIALOG, .size = 17, .bold = 0.3f, .line_space = 6,
			.char_space = -3 };
	static const struct { const char *name; const struct cw_box *box; } boxes[] = {
		{ "Text", &text }, { "Tall", &tall }, { "Odd", &odd }, { "Lines", &lines }, { "Button", &button },
		{ "Chk", &chk }, { "Def", &def }, { "Dialog", &dialog } };
	enum { TEXT, TALL, ODD, LINES, BUTTON, CHK, DEF, DIALOG, NR };
	struct ex_tree *kids;
	struct ex *e = lb_activity("ConfigWidgetsText", NR, &kids);
	for (int i = 0; i < NR; i++)
		kids->children[i] = cw_box_node(boxes[i].name, 20, 20 + 60 * i, boxes[i].box);
	int act = cw_load("ConfigWidgetsText", e);
	int no[NR];
	bool all = true;
	for (int i = 0; i < NR; i++)
		all = (no[i] = cw_no(act, boxes[i].name)) && all;
	heap_unref(act);
	if (!all)
		return;

	/* The game's caption: size 19 with a line spacing of 7 is 13 high, so
	 * the text is put at (23 - 13) / 2 = 5 and its cells, 19 high, lose
	 * their last row to the surface of 23. The width is the space's 10 and
	 * three cells of 19. */
	int tw, th;
	cw_text_size(&text, &tw, &th);
	CW_CHECK(tw == 67 && th == 13, "W2: the model's text is %dx%d, want 67x13", tw, th);
	cw_expect_text_states(no[TEXT], &text, CW_N, CW_ON, CW_DOWN, "W2 a caption");
	uint8_t *px = cw_state_pixels(no[TEXT], 0, 90, 23, "W2 a caption");
	cw_expect_px(px, 90, 0, 0, 20, 10, 10, 255, "W2 the box's corner");
	cw_expect_px(px, 90, 1, 1, 20, 19, 19, 60, "W2 inside the box");
	cw_expect_px(px, 90, 22, 22, 20, 208, 208, 255, "W2 the box's last pixel");
	cw_expect_px(px, 90, 23, 0, 0, 0, 0, 0, "W2 above the text");
	CW_CHECK(cw_row_alpha(px, 90, 4, 23, 90) == 0 && cw_row_alpha(px, 90, 5, 23, 90) > 0,
		 "W2: the text does not start at row 5 (%d pixels in row 4, %d in row 5)", cw_row_alpha(px, 90, 4, 23, 90),
		 cw_row_alpha(px, 90, 5, 23, 90));
	CW_CHECK(cw_row_alpha(px, 90, 22, 23, 90) > 0, "W2: the last row has nothing of the text");
	free(px);

	/* A text higher than the box: 14 over a box of 9. The box lies at
	 * 14 / 2 - 9 / 2 = 3 (not (14 - 9) / 2 = 2) and the text at 0. */
	cw_expect_text_states(no[TALL], &tall, CW_SMALL_N, CW_SMALL_ON, CW_SMALL_DOWN, "W2 a high text");
	px = cw_state_pixels(no[TALL], 0, 23, 14, "W2 a high text");
	cw_expect_px(px, 23, 0, 2, 0, 0, 0, 0, "W2 above the small box");
	cw_expect_px(px, 23, 0, 3, 202, 10, 10, 255, "W2 the small box's corner");
	cw_expect_px(px, 23, 0, 11, 202, 10, 82, 255, "W2 the small box's last row");
	cw_expect_px(px, 23, 0, 12, 0, 0, 0, 0, "W2 below the small box");
	CW_CHECK(cw_row_alpha(px, 23, 0, 9, 23) > 0, "W2: a high text does not start at row 0");
	free(px);

	/* 14 under a box of 23: (23 - 14) / 2 = 4. */
	cw_expect_text_states(no[ODD], &odd, CW_N, CW_ON, CW_DOWN, "W2 an odd difference");
	px = cw_state_pixels(no[ODD], 0, 37, 23, "W2 an odd difference");
	CW_CHECK(cw_row_alpha(px, 37, 3, 23, 37) == 0 && cw_row_alpha(px, 37, 4, 23, 37) > 0,
		 "W2: the text does not start at row 4");
	free(px);

	/* Two lines, a character spacing of -2 and an edge of 1: lines of 18,
	 * 18 + (3 + 18) - 3 = 36 high; the longer line is two cells of 18
	 * less 2 each, and one spacing is taken off again: 34 wide. */
	cw_text_size(&lines, &tw, &th);
	CW_CHECK(tw == 34 && th == 36, "W2: the model's two lines are %dx%d, want 34x36", tw, th);
	cw_expect_text_states(no[LINES], &lines, CW_N, CW_ON, CW_DOWN, "W2 two lines");

	cw_expect_cgs(no[BUTTON], CW_N, CW_ON, CW_DOWN, "W2 the button mode");
	cw_expect_text_states(no[CHK], &chk, CW_CN, CW_CON, CW_CDOWN, "W2 a checked caption");
	/* No font size in the pactex: 16. */
	cw_expect_text_states(no[DEF], &def, CW_N, CW_ON, CW_DOWN, "W2 the default font size");

	/* The dialogs' "do not show again" is the one box of the game whose
	 * e comes from the weight, with a negative character spacing: size 17
	 * and a weight of 0.3 make e 1 and lines of 17 + 1 + 2 = 20, less the
	 * line spacing of 6: 14 high. The space is 9 + 2 - 3 = 8 and each of
	 * the nine characters 17 + 2 - 3 = 16; one spacing back: 155 wide. On
	 * a box of 23 the surface is 178 x 23 and the text lies at
	 * (23 - 14) / 2 = 4. */
	cw_text_size(&dialog, &tw, &th);
	CW_CHECK(tw == 155 && th == 14, "W2: the model's dialog text is %dx%d, want 155x14", tw, th);
	cw_expect_text_states(no[DIALOG], &dialog, CW_N, CW_ON, CW_DOWN, "W2 a dialog's box");
	px = cw_state_pixels(no[DIALOG], 0, 178, 23, "W2 a dialog's box");
	CW_CHECK(cw_row_alpha(px, 178, 3, 23, 178) == 0, "W2: the dialog's text has %d pixel(s) in row 3, above its place",
		 cw_row_alpha(px, 178, 3, 23, 178));
	int rows = 0;
	for (int y = 4; y < 23; y++)
		rows += cw_row_alpha(px, 178, y, 23, 178) > 0;
	CW_CHECK(rows > 0, "W2: the dialog's text has no pixel from row 4 on");
	free(px);
	cw_failures += bt_failures;
	printf("W2 checked\n");
}

/* --- W3 --- */
static void cw_values(void)
{
	cw_setup(true);
	static const struct cw_box plain = { .cg = "cwbox", .text = "" },
		text = { .cg = "cwbox", .text = CW_TEXT_A, .size = 19, .line_space = 7 };
	struct ex_tree *kids;
	static const struct cw_box off = { .cg = "cwbox", .text = "", .checked = 1, .disabled = true };
	struct ex *e = lb_activity("ConfigWidgetsValues", 4, &kids);
	kids->children[0] = cw_box_node("Plain", 100, 100, &plain);
	kids->children[1] = cw_box_node("Text", 100, 200, &text);
	kids->children[2] = cw_box_node("Silent", 100, 300, &plain);
	kids->children[3] = cw_box_node("Off", 100, 400, &off);
	int act = cw_load("ConfigWidgetsValues", e);
	int box = cw_no(act, "Plain"), cap = cw_no(act, "Text"), silent = cw_no(act, "Silent"), dis = cw_no(act, "Off");
	heap_unref(act);
	if (!box || !cap || !silent || !dis)
		return;
	cp_register_event(box, 3, 30);
	cp_register_event(cap, 4, 40);
	cp_register_event(dis, 5, 50);

	/* a box the pactex disables: checked as it says, and it stays so */
	CW_CHECK(!cw_enabled(dis) && cw_checked(dis), "W3: the box loaded disabled is enabled %d, checked %d; want 0, 1",
		 cw_enabled(dis), cw_checked(dis));
	cw_expect_cgs(dis, CW_COFF, CW_COFF, CW_COFF, "W3 a box loaded disabled");
	cw_click(110, 410);
	cw_check(dis, false);
	CW_CHECK(cw_checked(dis), "W3: the box loaded disabled was unchecked");
	cw_expect_quiet("W3 a box loaded disabled");

	/* the value it has: nothing */
	cw_check(box, false);
	cw_update();
	cw_expect_quiet("W3 the same value");
	cw_expect_cgs(box, CW_N, CW_ON, CW_DOWN, "W3 the same value");

	/* another value: kept, told, and drawn at the update. This is also
	 * what a screen gets that adds the event first and sets the value
	 * afterwards: the system menu sends one ChangedFlg(true) for every
	 * shortcut that is checked, each time it opens (its handler looks
	 * whether the shortcut is already there). */
	cw_check(box, true);
	CW_CHECK(cw_checked(box), "W3: CheckBoxChecked(true) is not read back");
	struct cw_msg m[2];
	int n = cw_drain(m, 2);
	CW_CHECK(n == 1 && m[0].type == 23 && m[0].no == box && m[0].delegate == 3 && m[0].unique == 30 && m[0].v == 1,
		 "W3 ChangedFlg: %d message(s), type %d, parts %d, delegate %d, unique %d, value %d; want 1, 23, %d, 3, 30, 1",
		 n, m[0].type, m[0].no, m[0].delegate, m[0].unique, m[0].v, box);
	cw_expect_cgs(box, CW_N, CW_ON, CW_DOWN, "W3 before the update");
	cw_update();
	cw_expect_cgs(box, CW_CN, CW_CON, CW_CDOWN, "W3 after the update");
	cw_check(box, true);
	cw_expect_quiet("W3 checked twice");
	cw_check(box, false);
	CW_MESSAGES("W3 unchecked by the script", { 23, box, 0 });
	CW_CHECK(!cw_checked(box), "W3: CheckBoxChecked(false) is not read back");
	cw_update();
	cw_expect_cgs(box, CW_N, CW_ON, CW_DOWN, "W3 unchecked");

	/* a parts no script gave an event: the value, no message */
	cw_check(silent, true);
	CW_CHECK(cw_checked(silent), "W3: a box without an event does not keep its value");
	cw_expect_quiet("W3 a box without an event");

	/* a caption follows its box */
	cw_check(cap, true);
	CW_MESSAGES("W3 a caption checked", { 23, cap, 1 });
	cw_update();
	const struct cw_box text_checked = { .cg = "cwbox", .text = CW_TEXT_A, .size = 19, .line_space = 7, .checked = 1 };
	cw_expect_text_states(cap, &text_checked, CW_CN, CW_CON, CW_CDOWN, "W3 a caption checked");

	/* not enabled: the three states are the disabled CG, the value stays,
	 * a click is swallowed */
	CW_CHECK(cw_enabled(box), "W3: a loaded box is not enabled");
	cw_enable(box, false);
	CW_CHECK(!cw_enabled(box), "W3: SetCheckBoxEnable(false) is not read back");
	cw_update();
	cw_expect_cgs(box, CW_OFF, CW_OFF, CW_OFF, "W3 a disabled box");
	cw_check(box, true);
	CW_CHECK(!cw_checked(box), "W3: a disabled box was checked by the script");
	cw_expect_quiet("W3 a disabled box and the script");
	cw_click(110, 110);
	CW_CHECK(!cw_checked(box), "W3: a disabled box was checked by a click");
	cw_expect_quiet("W3 a click on a disabled box");
	cw_enable(box, true);
	cw_update();
	cw_expect_cgs(box, CW_N, CW_ON, CW_DOWN, "W3 enabled again");
	cw_check(box, true);
	cw_drain(m, 2);
	cw_enable(box, false);
	cw_update();
	cw_expect_cgs(box, CW_COFF, CW_COFF, CW_COFF, "W3 a checked box disabled");
	cw_check(box, false);
	CW_CHECK(cw_checked(box), "W3: a disabled box was unchecked by the script");
	cw_expect_quiet("W3 the end");
	cw_failures += bt_failures;
	printf("W3 checked\n");
}

/* --- W4 --- */
static void cw_clicks(void)
{
	cw_setup(true);
	static const struct cw_box plain = { .cg = "cwbox", .text = "" },
		text = { .cg = "cwbox", .text = CW_TEXT_A, .size = 19, .line_space = 7 };
	struct ex_tree *kids;
	struct ex *e = lb_activity("ConfigWidgetsClicks", 2, &kids);
	kids->children[0] = cw_box_node("Plain", 100, 100, &plain);
	kids->children[1] = cw_box_node("Text", 300, 100, &text);
	int act = cw_load("ConfigWidgetsClicks", e);
	int box = cw_no(act, "Plain"), cap = cw_no(act, "Text");
	heap_unref(act);
	if (!box || !cap)
		return;
	cp_register_event(box, 3, 30);
	cp_register_event(cap, 4, 40);

	/* MouseClick at the press; the value changes at the update of the
	 * parts that follows, and ChangedFlg comes then */
	cw_press(110, 110);
	CW_CHECK(!cw_checked(box), "W4: the press changed the box before the update");
	CW_MESSAGES("W4 a press", { 4, box, 110 });
	cw_update();
	CW_MESSAGES("W4 the update after a press", { 23, box, 1 });
	CW_CHECK(cw_checked(box), "W4: a click did not check the box");
	cw_update();
	cw_expect_quiet("W4 a second update");
	CW_CHECK(cw_checked(box), "W4: a second update changed the box again");
	cw_click(110, 110);
	CW_MESSAGES("W4 a click", { 4, box, 110 }, { 23, box, 0 });
	cw_click(110, 110);
	CW_MESSAGES("W4 a click", { 4, box, 110 }, { 23, box, 1 });
	cw_click(122, 122);
	CW_MESSAGES("W4 a second click", { 4, box, 122 }, { 23, box, 0 });
	CW_CHECK(!cw_checked(box), "W4: a second click did not uncheck the box");
	/* beside it: the whole-screen click, and nothing changes */
	cw_click(123, 110);
	CW_MESSAGES("W4 a click beside the box", { 4, 0, 123 });
	CW_CHECK(!cw_checked(box), "W4: a click beside the box changed it");
	heap[global_page_slot].page->values[2].i = 0;

	/* the caption is the box: 90 wide */
	cw_click(300 + 80, 110);
	CW_MESSAGES("W4 a click on the caption", { 4, cap, 380 }, { 23, cap, 1 });
	CW_CHECK(cw_checked(cap), "W4: a click on the caption did not check the box");
	cw_click(300 + 90, 110);
	CW_MESSAGES("W4 a click right of the caption", { 4, 0, 390 });
	heap[global_page_slot].page->values[2].i = 0;

	/* held: the down state; PE_Update draws the value the click gave */
	extern bool key_state[];
	mouse_set_pos(110, 110);
	PE_BeginInput();
	key_state[VK_LBUTTON] = false;
	PE_Update(16, true);
	cw_expect_cgs(box, CW_N, CW_ON, CW_DOWN, "W4 before the press");
	key_state[VK_LBUTTON] = true;
	PE_Update(16, true);
	struct parts *p = parts_try_get(box);
	CW_CHECK(p->state == PARTS_STATE_CLICKED, "W4: the held box is in state %d, want the down state", p->state);
	CW_CHECK(cw_checked(box), "W4: the press under PE_Update did not check the box");
	cw_expect_cgs(box, CW_CN, CW_CON, CW_CDOWN, "W4 drawn by PE_Update");
	key_state[VK_LBUTTON] = false;
	PE_Update(16, true);
	CW_CHECK(p->state == PARTS_STATE_HOVERED, "W4: the released box is in state %d, want the on-cursor state", p->state);
	PE_EndInput();
	CW_MESSAGES("W4 the press under PE_Update", { 4, box, 110 }, { 23, box, 1 });
	printf("W4 checked\n");
}

/* A component with nothing but its type. */
static struct ex_tree cw_bare_node(const char *name, const char *type)
{
	struct lb_place pl = { 0, 0, 1 };
	struct ex_tree info = bu_branch(BU_INFO, 1);
	info.children[0] = bu_sleaf(BU_TYPE, type);
	return lb_node(name, &pl, info, 0);
}

/* --- W5 --- */
/* One frame of parts::detail::Update with the settings' two events around
 * it: CConfigView@BeginUpdateEvent writes the configuration to the parts
 * (SetCheckStatForParts, SetHScrollRateForParts), PartsEngine.UpdateComponent
 * runs, and CConfigView@EndUpdateEvent reads the parts back into it
 * (SetCheckStatForAsra, SetHScrollRateForAsra). The script's wrappers are
 * not run here: the four library functions are called as they call them. */
static void cw_frame(int box, int bar, bool *flag, float *rate)
{
	cw_check(box, *flag);
	cw_set_rate(bar, *rate);
	PE_Update(16, true);
	*flag = cw_checked(box);
	*rate = cw_rate(bar);
}

static void cw_loop(void)
{
	cw_setup(true);
	static const struct cw_box plain = { .cg = "cwbox", .text = "" };
	struct ex_tree *kids;
	struct ex *e = lb_activity("ConfigWidgetsLoop", 2, &kids);
	kids->children[0] = cw_box_node("Box", 100, 100, &plain);
	kids->children[1] = cw_bar_node("Bar", CW_HSCROLL, 100, 1, 99, 1.0f);
	int act = cw_load("ConfigWidgetsLoop", e);
	int box = cw_no(act, "Box"), bar = cw_no(act, "Bar");
	heap_unref(act);
	if (!box || !bar)
		return;
	cp_register_event(box, 3, 30);
	cp_register_event(bar, 5, 50);
	extern bool key_state[];
	mouse_set_pos(600, 600);
	key_state[VK_LBUTTON] = false;
	PE_BeginInput();

	/* The configuration says checked and 0.5; the pactex says neither. */
	bool flag = true;
	float rate = 0.5f;
	for (int frame = 0; frame < 4; frame++) {
		cw_frame(box, bar, &flag, &rate);
		CW_CHECK(flag && rate == 0.5f, "W5 frame %d: the configuration became %d and %g, want 1 and 0.5", frame,
			 flag, rate);
	}
	/* the box's value and the slider's rate were new in the first frame:
	 * one ChangedFlg and one Scroll (position 49 of 99), and no more */
	CW_MESSAGES("W5 four frames", { 23, box, 1 }, { 21, bar, 49 });
	cw_expect_cgs(box, CW_CN, CW_CON, CW_CDOWN, "W5 the box of a checked setting");

	/* a click during a frame is what the end of the frame reads */
	mouse_set_pos(110, 110);
	cw_frame(box, bar, &flag, &rate);
	key_state[VK_LBUTTON] = true;
	cw_frame(box, bar, &flag, &rate);
	CW_CHECK(!flag && rate == 0.5f, "W5 the frame of the click: %d and %g, want 0 and 0.5", flag, rate);
	CW_MESSAGES("W5 the frame of the click", { 4, box, 110 }, { 23, box, 0 });
	key_state[VK_LBUTTON] = false;
	for (int frame = 0; frame < 3; frame++)
		cw_frame(box, bar, &flag, &rate);
	CW_CHECK(!flag && rate == 0.5f, "W5 after the click: %d and %g, want 0 and 0.5", flag, rate);
	cw_expect_quiet("W5 after the click");
	cw_expect_cgs(box, CW_N, CW_ON, CW_DOWN, "W5 the box after the click");

	/* The press seen before the frame, as system.Peek sees it: the box
	 * keeps its value until the update, so the frame's first half does not
	 * write the old configuration over the click, and its second half
	 * reads the new value. */
	mouse_set_pos(110, 110);
	key_state[VK_LBUTTON] = true;
	PE_UpdateInputState(0);
	CW_CHECK(!cw_checked(box), "W5: a press outside the frame changed the box at once");
	cw_frame(box, bar, &flag, &rate);
	CW_CHECK(flag, "W5: a press outside the frame is not what the frame reads (%d, want 1)", flag);
	key_state[VK_LBUTTON] = false;
	PE_UpdateInputState(0);
	for (int frame = 0; frame < 3; frame++)
		cw_frame(box, bar, &flag, &rate);
	CW_CHECK(flag && cw_checked(box), "W5 after a press outside the frame: %d, want 1", flag);
	CW_MESSAGES("W5 a press outside the frame", { 4, box, 110 }, { 23, box, 1 });
	cw_expect_cgs(box, CW_CN, CW_CON, CW_CDOWN, "W5 the box after a press outside the frame");

	/* every rate comes back as it was written */
	static const float rates[] = { 0.0f, 0.25f, 0.999f, 1.0f, 0.3f };
	for (unsigned i = 0; i < sizeof rates / sizeof rates[0]; i++) {
		rate = rates[i];
		cw_frame(box, bar, &flag, &rate);
		cw_frame(box, bar, &flag, &rate);
		CW_CHECK(rate == rates[i], "W5: a rate of %g became %g", rates[i], rate);
	}
	PE_EndInput();
	/* each new rate is reported once, as its position */
	CW_MESSAGES("W5 the rates", { 21, bar, 0 }, { 21, bar, 24 }, { 21, bar, 98 }, { 21, bar, 99 }, { 21, bar, 29 });
	printf("W5 checked\n");
}

/* --- W6 --- */
static void cw_radio(void)
{
	cw_setup(true);
	static const struct cw_box off = { .cg = "cwbox", .text = "" }, on = { .cg = "cwbox", .text = "", .checked = 1 };
	struct ex_tree *kids;
	struct ex *e = lb_activity("ConfigWidgetsRadio", 6, &kids);
	/* the boxes come after the radio box that names them */
	kids->children[0] = cw_radio_node("Radio", 3, (const char *[]){ "A", "B", "C" });
	kids->children[1] = cw_radio_node("Nobody", 2, (const char *[]){ "NoSuchBox", "" });
	kids->children[2] = cw_box_node("A", 100, 100, &on);
	kids->children[3] = cw_box_node("B", 200, 100, &off);
	kids->children[4] = cw_box_node("C", 300, 100, &off);
	kids->children[5] = cw_box_node("Lone", 400, 100, &off);
	int act = cw_load("ConfigWidgetsRadio", e);
	int radio = cw_no(act, "Radio"), a = cw_no(act, "A"), b = cw_no(act, "B"), c = cw_no(act, "C"),
		lone = cw_no(act, "Lone");
	heap_unref(act);
	if (!radio || !a || !b || !c || !lone)
		return;
	CW_CHECK(cw_type(radio) == 9, "W6: the radio box has type %d, want 9", cw_type(radio));
	cp_register_event(a, 1, 10);
	cp_register_event(b, 2, 20);
	cp_register_event(c, 3, 30);
	cp_register_event(lone, 4, 40);
#define CW_RADIO_IS(wa, wb, wc, what) CW_CHECK(cw_checked(a) == wa && cw_checked(b) == wb && cw_checked(c) == wc, \
		"W6 %s: A %d, B %d, C %d; want %d, %d, %d", what, cw_checked(a), cw_checked(b), cw_checked(c), wa, wb, wc)
	CW_RADIO_IS(1, 0, 0, "as loaded");
	cw_expect_quiet("W6 loading");

	/* another member: the checked one is unchecked first, then this one's
	 * ChangedFlg */
	cw_click(210, 110);
	CW_MESSAGES("W6 a click on B", { 4, b, 210 }, { 23, a, 0 }, { 23, b, 1 });
	CW_RADIO_IS(0, 1, 0, "after a click on B");
	cw_update();
	cw_expect_cgs(a, CW_N, CW_ON, CW_DOWN, "W6 A unchecked by the box");
	cw_expect_cgs(b, CW_CN, CW_CON, CW_CDOWN, "W6 B checked");
	/* the checked member: the box checks it again and the scripts hear
	 * that, not the uncheck */
	cw_click(210, 110);
	CW_MESSAGES("W6 a click on the checked B", { 4, b, 210 }, { 23, b, 1 });
	CW_RADIO_IS(0, 1, 0, "after a click on the checked B");
	cw_check(b, false);
	CW_MESSAGES("W6 the script unchecking B", { 23, b, 1 });
	CW_RADIO_IS(0, 1, 0, "after the script unchecked B");
	cw_check(c, true);
	CW_MESSAGES("W6 the script checking C", { 23, b, 0 }, { 23, c, 1 });
	CW_RADIO_IS(0, 0, 1, "after the script checked C");
	cw_check(c, true);
	cw_expect_quiet("W6 the script checking C again");

	/* the box's own Changed, before the members it unchecks; none when
	 * the checked member is clicked (the box does not handle what it
	 * changes itself) */
	cp_register_event(radio, 9, 90);
	cw_click(110, 110);
	CW_MESSAGES("W6 a click on A", { 4, a, 110 }, { 22, radio, 0 }, { 23, c, 0 }, { 23, a, 1 });
	CW_RADIO_IS(1, 0, 0, "after a click on A");
	cw_click(110, 110);
	CW_MESSAGES("W6 a click on the checked A", { 4, a, 110 }, { 23, a, 1 });
	CW_RADIO_IS(1, 0, 0, "after a click on the checked A");

	/* a box of no group */
	cw_click(410, 110);
	CW_MESSAGES("W6 a click on a box of no group", { 4, lone, 410 }, { 23, lone, 1 });
	CW_RADIO_IS(1, 0, 0, "after a click on a box of no group");

	/* a member that is not enabled keeps its value: nothing unchecks it */
	cw_enable(a, false);
	cw_click(310, 110);
	CW_MESSAGES("W6 a click on C while A is disabled", { 4, c, 310 }, { 22, radio, 0 }, { 23, c, 1 });
	CW_RADIO_IS(1, 0, 1, "while A is disabled");
	cw_enable(a, true);

	/* a member that was released is passed over */
	parts_release(a);
	cw_click(210, 110);
	CW_MESSAGES("W6 a click on B once A is gone", { 4, b, 210 }, { 22, radio, 0 }, { 23, c, 0 }, { 23, b, 1 });
	CW_CHECK(cw_checked(b) && !cw_checked(c), "W6 once A is gone: B %d, C %d; want 1, 0", cw_checked(b), cw_checked(c));
	parts_release(radio);
	cw_click(310, 110);
	CW_MESSAGES("W6 a click on C once the radio box is gone", { 4, c, 310 }, { 23, c, 1 });
	CW_CHECK(cw_checked(b) && cw_checked(c), "W6 once the radio box is gone: B %d, C %d; want 1, 1", cw_checked(b),
		 cw_checked(c));
#undef CW_RADIO_IS
	printf("W6 checked\n");
}

/* --- W7 --- */
static void cw_expect_bar(int no, int total, int view, int pos, float rate, const char *what)
{
	struct parts *p = parts_try_get(no);
	float got = cw_rate(no);
	CW_CHECK(p && p->scrollbar.total == total && p->scrollbar.view == view && p->scrollbar.pos == pos && got == rate,
		 "%s: total %d, view %d, position %d, rate %g; want %d, %d, %d, %g", what, p ? p->scrollbar.total : -1,
		 p ? p->scrollbar.view : -1, p ? p->scrollbar.pos : -1, got, total, view, pos, rate);
}

static void cw_hbar(void)
{
	cw_setup(true);
	struct ex_tree *kids;
	static const struct cw_box wide_box = { .cg = "cwbox", .text = CW_TEXT_A, .size = 19, .line_space = 7 };
	struct ex *e = lb_activity("ConfigWidgetsBars", 7, &kids);
	kids->children[6] = cw_box_node("Wide", 400, 400, &wide_box);
	kids->children[0] = cw_bar_node("H1", CW_HSCROLL, 100, 1, 99, 1.0f);
	kids->children[1] = cw_bare_node("H2", CW_HSCROLL);
	kids->children[2] = cw_bar_node("V1", BT_VSCROLL, 40, 10, 7, 0.25f);
	kids->children[3] = cw_bar_node("S1", CW_HSCROLL, 100, 1, 99, 1.0f);
	kids->children[4] = cw_bar_node("S2", CW_HSCROLL, 100, 1, 99, 1.0f);
	kids->children[5] = cw_bar_node("S3", CW_HSCROLL, 100, 1, 99, 1.0f);
	int act = cw_load("ConfigWidgetsBars", e);
	int h1 = cw_no(act, "H1"), h2 = cw_no(act, "H2"), v1 = cw_no(act, "V1");
	int s1 = cw_no(act, "S1"), s2 = cw_no(act, "S2"), s3 = cw_no(act, "S3"), wide = cw_no(act, "Wide");
	heap_unref(act);
	if (!h1 || !h2 || !v1 || !s1 || !s2 || !s3 || !wide)
		return;
	CW_CHECK(cw_type(h1) == 3 && cw_type(h2) == 3 && cw_type(v1) == 2, "W7: types %d, %d, %d; want 3, 3, 2", cw_type(h1),
		 cw_type(h2), cw_type(v1));
	/* the settings' sliders: 100 by 1 at 99, rate 1 */
	cw_expect_bar(h1, 100, 1, 99, 1.0f, "W7 as loaded");
	cw_set_rate(h1, 0.5f);
	cw_expect_bar(h1, 100, 1, 49, 0.5f, "W7 a rate of 0.5");
	cw_set_rate(h1, 0.999f);
	cw_expect_bar(h1, 100, 1, 98, 0.999f, "W7 a rate that is truncated");
	cw_set_rate(h1, 7.0f);
	cw_expect_bar(h1, 100, 1, 99, 1.0f, "W7 a rate above 1");
	cw_set_rate(h1, -1.0f);
	cw_expect_bar(h1, 100, 1, 0, 0.0f, "W7 a rate below 0");
	/* a bar without the keys scrolls nothing and still keeps its rate */
	cw_expect_bar(h2, 0, 0, 0, 0.0f, "W7 without the keys");
	cw_set_rate(h2, 0.5f);
	cw_expect_bar(h2, 0, 0, 0, 0.5f, "W7 a rate without the keys");
	/* A bar somebody listens to reports a position or a rate that is not
	 * the one it last reported, once, as the vertical bar does: here the
	 * rate of 0 it was left with, against the 99 and 1 it was loaded
	 * with. */
	cp_register_event(h1, 5, 50);
	cw_expect_quiet("W7 before the bars' update");
	cw_bars_update();
	cw_expect_scroll("W7 a rate of 0", h1, 0, 99);
	cw_set_rate(h1, 0.3f);
	cw_bars_update();
	cw_expect_scroll("W7 a rate of 0.3", h1, 29, 99);
	cw_bars_update();
	cw_expect_scroll("W7 a second update", 0, 0, 0);
	cw_set_rate(h1, 0.3f);
	cw_bars_update();
	cw_expect_scroll("W7 the same rate again", 0, 0, 0);

	/* The settings' sound page (UserConfig::SetSoundEvent) adds the event
	 * to a slider and then sets its position to a volume; the volume is
	 * written from the position the event carries. What the slider was
	 * loaded with counts as reported: nothing is sent before it is moved. */
	cp_register_event(s1, 6, 60);
	cw_bars_update();
	cw_expect_scroll("W7 a slider as it was loaded", 0, 0, 0);
	CW_CHECK(cw_pos(s1) == 99, "W7: the loaded position reads as %d, want 99", cw_pos(s1));
	cw_set_pos(s1, 80);
	cw_expect_bar(s1, 100, 1, 80, 80.0f / 99.0f, "W7 a position of 80");
	CW_CHECK(cw_pos(s1) == 80, "W7: GetHScrollbarScrollPos gives %d, want 80", cw_pos(s1));
	cw_expect_quiet("W7 a position that was set, before the update");
	cw_bars_update();
	cw_expect_scroll("W7 a position of 80", s1, 80, 99);
	cw_set_pos(s1, 80);
	cw_bars_update();
	cw_expect_scroll("W7 the same position again", 0, 0, 0);
	/* the page's 初始化 button: the default volume */
	cw_set_pos(s1, 60);
	cw_bars_update();
	cw_expect_scroll("W7 the default position", s1, 60, 99);
	/* the position is kept within 0 and the largest one */
	cw_set_pos(s1, 100);
	cw_expect_bar(s1, 100, 1, 99, 1.0f, "W7 a position above the largest");
	cw_bars_update();
	cw_expect_scroll("W7 a position above the largest", s1, 99, 99);
	cw_set_pos(s1, -5);
	cw_expect_bar(s1, 100, 1, 0, 0.0f, "W7 a position below 0");
	cw_bars_update();
	cw_expect_scroll("W7 a position below 0", s1, 0, 99);
	/* a rate that differs is reported though the position is the same */
	cw_set_pos(s1, 49);
	cw_bars_update();
	cw_expect_scroll("W7 a position of 49", s1, 49, 99);
	cw_set_rate(s1, 0.5f);
	cw_expect_bar(s1, 100, 1, 49, 0.5f, "W7 a rate of 0.5 at 49");
	cw_bars_update();
	cw_expect_scroll("W7 another rate at the same position", s1, 49, 99);

	/* a volume of 100 on a slider loaded at 99: the position it has, and
	 * no event that would write 99 as the volume */
	cp_register_event(s2, 7, 70);
	cw_set_pos(s2, 100);
	cw_bars_update();
	cw_expect_scroll("W7 a volume of 100 on a slider loaded at 99", 0, 0, 0);

	/* a bar nobody listens to remembers what it would have reported */
	cw_set_pos(s3, 30);
	cw_bars_update();
	cw_expect_scroll("W7 a bar without an event", 0, 0, 0);
	cp_register_event(s3, 8, 80);
	cw_bars_update();
	cw_expect_scroll("W7 an event added after the move", 0, 0, 0);
	cw_set_pos(s3, 31);
	cw_bars_update();
	cw_expect_scroll("W7 a move after the event was added", s3, 31, 99);
	/* the vertical bar as it was */
	struct parts *v = parts_try_get(v1);
	float vrate = BU_PE("GetVScrollbarScrollRate", BU_I(v1)).f;
	CW_CHECK(v->scrollbar.total == 40 && v->scrollbar.view == 10 && v->scrollbar.pos == 7 && vrate == 0.25f,
		 "W7 the vertical bar: %d, %d, %d, %g; want 40, 10, 7, 0.25", v->scrollbar.total, v->scrollbar.view,
		 v->scrollbar.pos, vrate);

	/* An amount that follows another parts' size (全體スクロール量サイズ連動):
	 * a horizontal bar takes the width of what that parts shows, a
	 * vertical one the height (0x574a8b). The parts is 90 x 23. No bar of
	 * the game's settings is linked; the link is set here as the loader
	 * keeps it. */
	struct parts *linked_h = parts_try_get(s3);
	linked_h->scrollbar.total_link = wide;
	v->scrollbar.total_link = wide;
	cw_bars_update();
	CW_CHECK(linked_h->scrollbar.total == 90, "W7: a horizontal bar's linked total is %d, want the width 90",
		 linked_h->scrollbar.total);
	CW_CHECK(v->scrollbar.total == 23, "W7: a vertical bar's linked total is %d, want the height 23", v->scrollbar.total);
	cw_expect_scroll("W7 a linked total", s3, 31, 89);
	printf("W7 checked\n");
}

/* --- SystemService's window settings (W10, W12) --- */
static bool cw_set_window(int type, int value)
{
	return bu_call("SystemService", "SetWindowSetting", 2, 0, 2, (union vm_value[]){ BU_I(type), BU_I(value) }).i;
}

/* GetWindowSetting(type, wrap<int>): whether it answered; *value stays -77
 * when nothing was written. */
static bool cw_get_window(int type, int *value)
{
	int pg = heap_alloc_page(alloc_page(ARRAY_PAGE, AIN_ARRAY_INT, 1));
	heap[pg].page->values[0].i = -77;
	bool ok = bu_call("SystemService", "GetWindowSetting", 2, 0, 3,
			  (union vm_value[]){ BU_I(type), BU_I(pg), BU_I(0) }).i;
	*value = heap[pg].page->values[0].i;
	heap_unref(pg);
	return ok;
}

/* The save folder becomes an empty temporary one, so that the settings file
 * of the case is its own. */
static char cw_dir[1024];
static char *cw_window_path;
static void cw_window_dir(void)
{
	const char *base = getenv("TMPDIR");
	if (!base || !base[0])
		base = "/tmp";
	snprintf(cw_dir, sizeof cw_dir, "%s/xsys4-config-widgets-XXXXXX", base);
	if (!mkdtemp(cw_dir) || sp_under(cw_dir, "XS4_SRC") || sp_under(cw_dir, "XS4_MASTER_GAME")) {
		printf("CONFIG-WIDGETS cannot use a temporary folder under %s\n", base);
		fflush(stdout);
		_exit(CN_ENV_EXIT);
	}
	config.save_dir = strdup(cw_dir);
	cw_window_path = mj_join(cw_dir, "WindowSetting.json");
}
static void cw_window_rmdir(void)
{
	remove(cw_window_path);
	if (rmdir(cw_dir))
		printf("  (could not remove %s)\n", cw_dir);
}

/* The settings file's text (to be freed), NULL when there is none. */
static char *cw_window_file(void)
{
	FILE *f = fopen(cw_window_path, "rb");
	if (!f)
		return NULL;
	char *s = calloc(1, 4096);
	size_t n = fread(s, 1, 4095, f);
	s[n] = 0;
	fclose(f);
	return s;
}
static void cw_window_write(const char *text)
{
	FILE *f = fopen(cw_window_path, "wb");
	if (f) {
		fputs(text, f);
		fclose(f);
	}
	CW_CHECK(f, "W12: cannot write %s", cw_window_path);
}
/* As at the start of the engine: SystemService reads its settings file. */
static void cw_window_reload(void)
{
	extern struct static_library lib_SystemService;
	library_run(&lib_SystemService, "_ModuleInit");
}

/* The vertical sync flag of the older table, read as an engine before v14
 * reads its type 2. The probe has no window, so the swap interval itself
 * cannot be looked at; this is what SetWindowSetting(2, ...) and the start
 * of the engine set along with it there. */
static int cw_older_vsync(void)
{
	const int version = ain->version;
	int v = -1;
	ain->version = 13;
	const bool ok = cw_get_window(2, &v);
	ain->version = version;
	return ok ? v : -1;
}

/* The five settings are these values. */
static void cw_expect_window(const char *what, int v0, int v1, int v2, int v3, int v4)
{
	const int want[5] = { v0, v1, v2, v3, v4 };
	int got[5];
	bool ok = true;
	for (int t = 0; t < 5; t++)
		ok = cw_get_window(t, &got[t]) && got[t] == want[t] && ok;
	CW_CHECK(ok, "%s: the window settings are %d, %d, %d, %d, %d; want %d, %d, %d, %d, %d", what, got[0], got[1], got[2],
		 got[3], got[4], v0, v1, v2, v3, v4);
}

/* --- W8 --- */
static bool cw_set_cursor_config(int type, int value)
{
	return bu_call("SystemService", "SetMouseCursorConfig", 2, 0, 2, (union vm_value[]){ BU_I(type), BU_I(value) }).i;
}

static void cw_missing(void)
{
	cw_setup(true);
	static const struct cw_box plain = { .cg = "cwbox", .text = "" };
	struct ex_tree *kids;
	struct ex *e = lb_activity("ConfigWidgetsMissing", 3, &kids);
	kids->children[0] = cw_box_node("Box", 100, 100, &plain);
	kids->children[1] = cw_bare_node("Other", BU_UC);
	kids->children[2] = cw_bar_node("V1", BT_VSCROLL, 40, 10, 7, 0.25f);
	int act = cw_load("ConfigWidgetsMissing", e);
	int box = cw_no(act, "Box"), other = cw_no(act, "Other"), v1 = cw_no(act, "V1");
	heap_unref(act);
	if (!box || !other || !v1)
		return;
	const int N = 9801;
	bool had0 = parts_try_get(0);

	/* numbers of 0 and below: no widget */
	cw_check(0, true);
	cw_check(-3, true);
	cw_enable(-3, false);
	cw_set_rate(0, 0.5f);
	cw_set_rate(-3, 0.5f);
	CW_CHECK(!cw_checked(0) && !cw_checked(-3) && !cw_enabled(0) && !cw_enabled(-3) && cw_rate(0) == 0.0f
		 && cw_rate(-3) == 0.0f, "W8: a number of 0 or below gives %d, %d, %d, %d, %g, %g; want 0 for all",
		 cw_checked(0), cw_checked(-3), cw_enabled(0), cw_enabled(-3), cw_rate(0), cw_rate(-3));
	CW_CHECK(!parts_try_get(-3) && !!parts_try_get(0) == had0, "W8: a number of 0 or below was made a parts");

	/* any other number without a parts is made a check box: enabled, not
	 * checked */
	for (int i = 0; i < 10; i++)
		CW_CHECK(!parts_try_get(N + i), "W8: parts %d is there before the test", N + i);
	CW_CHECK(!cw_checked(N), "W8: a new check box is checked");
	CW_CHECK(parts_try_get(N) && cw_type(N) == 1, "W8: IsCheckBoxChecked did not make parts %d a check box (type %d)", N,
		 parts_try_get(N) ? cw_type(N) : -1);
	CW_CHECK(cw_enabled(N), "W8: a new check box is not enabled");
	cw_check(N + 1, true);
	CW_CHECK(parts_try_get(N + 1) && cw_type(N + 1) == 1 && cw_checked(N + 1),
		 "W8: CheckBoxChecked did not make and check parts %d", N + 1);
	cw_enable(N + 2, false);
	CW_CHECK(parts_try_get(N + 2) && cw_type(N + 2) == 1 && !cw_enabled(N + 2),
		 "W8: SetCheckBoxEnable did not make and disable parts %d", N + 2);
	CW_CHECK(cw_enabled(N + 3) && parts_try_get(N + 3) && cw_type(N + 3) == 1,
		 "W8: IsCheckBoxEnable did not make parts %d an enabled check box", N + 3);
	cw_expect_quiet("W8 new check boxes");

	/* a parts of another type becomes a check box */
	CW_CHECK(cw_type(other) == 17, "W8: the user component has type %d", cw_type(other));
	cw_check(other, true);
	CW_CHECK(cw_type(other) == 1 && cw_checked(other), "W8: a user component is type %d, checked %d; want 1, 1",
		 cw_type(other), cw_checked(other));
	/* and a check box that became something else starts over */
	cw_check(box, true);
	BU_PE("SetComponentType", BU_I(box), BU_I(0), BU_I(1));
	CW_CHECK(cw_type(box) == 0, "W8: SetComponentType left type %d, want 0", cw_type(box));
	CW_CHECK(!cw_checked(box) && cw_type(box) == 1, "W8: a box that was a button is checked %d, type %d; want 0, 1",
		 cw_checked(box), cw_type(box));

	/* the same for the horizontal bar: 100 by 1 at 0 */
	CW_CHECK(cw_rate(N + 4) == 0.0f, "W8: a new bar has a rate");
	CW_CHECK(parts_try_get(N + 4) && cw_type(N + 4) == 3, "W8: GetHScrollbarScrollRate did not make parts %d a bar", N + 4);
	if (parts_try_get(N + 4)) {
		cw_expect_bar(N + 4, 100, 1, 0, 0.0f, "W8 a new bar");
		cw_set_rate(N + 4, 0.5f);
		cw_expect_bar(N + 4, 100, 1, 49, 0.5f, "W8 a new bar at 0.5");
	}
	cw_set_rate(N + 5, 0.25f);
	if (parts_try_get(N + 5))
		cw_expect_bar(N + 5, 100, 1, 24, 0.25f, "W8 a bar made by its rate");
	else
		CW_CHECK(0, "W8: SetHScrollbarScrollRate did not make parts %d", N + 5);
	BU_PE("SetComponentType", BU_I(N + 6), BU_I(3), BU_I(1));
	cw_set_rate(N + 6, 0.5f);
	cw_expect_bar(N + 6, 100, 1, 49, 0.5f, "W8 a bar made by SetComponentType");
	/* and by its position */
	CW_CHECK(cw_pos(N + 7) == 0, "W8: a new bar has a position");
	CW_CHECK(parts_try_get(N + 7) && cw_type(N + 7) == 3, "W8: GetHScrollbarScrollPos did not make parts %d a bar", N + 7);
	cw_set_pos(N + 8, 30);
	if (parts_try_get(N + 8))
		cw_expect_bar(N + 8, 100, 1, 30, 30.0f / 99.0f, "W8 a bar made by its position");
	else
		CW_CHECK(0, "W8: SetHScrollbarScrollPos did not make parts %d", N + 8);
	cw_set_pos(0, 5);
	cw_set_pos(-3, 5);
	CW_CHECK(cw_pos(0) == 0 && cw_pos(-3) == 0 && !parts_try_get(-3) && !!parts_try_get(0) == had0,
		 "W8: a position for a number of 0 or below gives %d, %d", cw_pos(0), cw_pos(-3));
	/* a vertical bar asked for its horizontal rate becomes a horizontal one */
	CW_CHECK(cw_rate(v1) == 0.0f && cw_type(v1) == 3, "W8: a vertical bar gives %g and is type %d; want 0, 3", cw_rate(v1),
		 cw_type(v1));
	cw_expect_bar(v1, 100, 1, 0, 0.0f, "W8 a vertical bar made horizontal");

	/* the input page's three mouse cursor settings */
	CW_CHECK(cw_set_cursor_config(0, 1) && cw_set_cursor_config(1, 1) && cw_set_cursor_config(2, 1),
		 "W8: SystemService.SetMouseCursorConfig refuses one of the types 0, 1, 2");
	CW_CHECK(!cw_set_cursor_config(3, 1) && !cw_set_cursor_config(-1, 1), "W8: SetMouseCursorConfig takes type 3 or -1");
	printf("W8 checked\n");
}

/* --- W9 --- */
static void cw_save(void)
{
	cw_setup(true);
	static const struct cw_box plain = { .cg = "cwbox", .text = "" },
		text = { .cg = "cwbox", .text = CW_TEXT_A, .size = 19, .line_space = 7 };
	struct ex_tree *kids;
	struct ex *e = lb_activity("ConfigWidgetsSave", 2, &kids);
	kids->children[0] = cw_box_node("Plain", 100, 100, &plain);
	kids->children[1] = cw_box_node("Text", 300, 100, &text);
	int act = cw_load("ConfigWidgetsSave", e);
	int box = cw_no(act, "Plain"), cap = cw_no(act, "Text");
	heap_unref(act);
	if (!box || !cap)
		return;
	cw_check(box, true);
	cw_update();
	cw_expect_cgs(box, CW_CN, CW_CON, CW_CDOWN, "W9 before the save");
	uint8_t *before[3];
	for (int i = 0; i < 3; i++)
		before[i] = cw_state_pixels(cap, i, 90, 23, "W9 before the save");
	PE_UpdateComponent(0);
	struct page *saved = NULL;
	CW_CHECK(PE_Save(&saved) && saved, "W9: PE_Save failed");
	if (!saved || !before[0] || !before[1] || !before[2])
		return;
	CW_CHECK(ca_save_version(saved) == 11, "W9: the save is XPE v%d, want 11", ca_save_version(saved));
	CW_CHECK(PE_Load(&saved), "W9: PE_Load failed");
	PE_UpdateComponent(0);
	ca_free_buffer(saved);
	cw_expect_cgs(box, CW_CN, CW_CON, CW_CDOWN, "W9 after PE_Load");
	for (int i = 0; i < 3; i++)
		cw_compare(cw_state_pixels(cap, i, 90, 23, "W9 after PE_Load"), 90, 23, before[i], "W9 the caption after PE_Load");
	CW_CHECK(cw_type(box) == 1 && cw_type(cap) == 1, "W9: types %d and %d after PE_Load, want 1 and 1", cw_type(box),
		 cw_type(cap));
	/* The values are not saved: a loaded box reads as the constructor
	 * leaves it until its activity is loaded again. Clicks do no harm, and
	 * what the save showed stays. */
	CW_CHECK(!cw_checked(box) && cw_enabled(box), "W9: a loaded box is checked %d, enabled %d; want 0, 1", cw_checked(box),
		 cw_enabled(box));
	cw_click(110, 110);
	cw_click(310, 110);
	cw_update();
	PE_UpdateComponent(0);
	cw_expect_cgs(box, CW_CN, CW_CON, CW_CDOWN, "W9 a loaded box after a click");
	for (int i = 0; i < 3; i++) {
		cw_compare(cw_state_pixels(cap, i, 90, 23, "W9 a loaded caption after a click"), 90, 23, before[i],
			   "W9 a loaded caption after a click");
		free(before[i]);
	}
	printf("W9 checked\n");
}

/* --- W10 --- */
static void cw_v13(void)
{
	/* Before the setup: _PostLink then links the library as for an engine
	 * before v14. */
	ain->version = 13;
	cw_setup(true);
	static const struct cw_box plain = { .cg = "cwbox", .text = "" },
		text = { .cg = "cwbox", .text = CW_TEXT_A, .size = 19, .line_space = 7, .checked = 1 };
	struct ex_tree *kids;
	struct ex *e = lb_activity("ConfigWidgetsOld", 5, &kids);
	kids->children[0] = cw_radio_node("Radio", 2, (const char *[]){ "Plain", "Text" });
	kids->children[1] = cw_box_node("Plain", 100, 100, &plain);
	kids->children[2] = cw_box_node("Text", 300, 100, &text);
	kids->children[3] = cw_bar_node("H1", CW_HSCROLL, 100, 1, 99, 1.0f);
	kids->children[4] = cw_bar_node("V1", BT_VSCROLL, 40, 10, 7, 0.25f);
	int act = cw_load("ConfigWidgetsOld", e);
	int box = cw_no(act, "Plain"), cap = cw_no(act, "Text"), h1 = cw_no(act, "H1"), v1 = cw_no(act, "V1");
	heap_unref(act);
	if (!box || !cap || !h1 || !v1)
		return;
	/* the loader: no check box is built or can be clicked, a horizontal
	 * bar's amounts are not read, the vertical bar's are as before */
	CW_CHECK(cw_unbuilt(box) && cw_unbuilt(cap), "W10: a check box was built for an engine before v14");
	CW_CHECK(!cp_clickable(box) && !cp_clickable(cap), "W10: a check box can be clicked");
	struct parts *h = parts_try_get(h1), *v = parts_try_get(v1);
	CW_CHECK(h->scrollbar.total == 0 && h->scrollbar.pos == 0 && h->scrollbar.rate == 0.0f,
		 "W10: the horizontal bar has %d, %d, %g", h->scrollbar.total, h->scrollbar.pos, h->scrollbar.rate);
	CW_CHECK(v->scrollbar.total == 40 && v->scrollbar.pos == 7 && v->scrollbar.rate == 0.25f,
		 "W10: the vertical bar has %d, %d, %g; want 40, 7, 0.25", v->scrollbar.total, v->scrollbar.pos, v->scrollbar.rate);
	/* the library: the functions are not bound, the stubs answer */
	cw_check(box, true);
	cw_set_rate(h1, 0.5f);
	CW_CHECK(!cw_checked(box) && !cw_checked(cap) && !cw_enabled(box) && cw_rate(h1) == 0.0f,
		 "W10: the library answers %d, %d, %d, %g; want the stubs' 0", cw_checked(box), cw_checked(cap),
		 cw_enabled(box), cw_rate(h1));
	CW_CHECK(h->scrollbar.rate == 0.0f, "W10: SetHScrollbarScrollRate wrote a rate");
	cw_set_pos(h1, 30);
	CW_CHECK(cw_pos(h1) == 0 && h->scrollbar.pos == 0, "W10: SetHScrollbarScrollPos wrote a position (%d, %d)", cw_pos(h1),
		 h->scrollbar.pos);
	void (*bars)(void) = (void (*)(void))dlsym(RTLD_DEFAULT, "parts_scrollbar_update");
	h->delegate_index = 5;
	h->scrollbar.pos = 7;
	if (bars)
		bars();
	cw_expect_quiet("W10 a bar's update");
	h->delegate_index = -1;
	h->scrollbar.pos = 0;
	PE_Update(16, true);
	CW_CHECK(cw_unbuilt(box) && cw_unbuilt(cap), "W10: an update built a check box");
	/* SystemService keeps its one mouse cursor setting */
	CW_CHECK(cw_set_cursor_config(0, 1) && !cw_set_cursor_config(1, 1) && !cw_set_cursor_config(2, 1),
		 "W10: SetMouseCursorConfig takes more than type 0");
	/* and its window settings keep the older numbering: 2 is the vertical
	 * sync, written under its own name at every call; 5 and 6 exist */
	cw_window_dir();
	int ws = -1;
	CW_CHECK(cw_get_window(5, &ws) && ws == 1 && cw_get_window(6, &ws) && ws == 1 && !cw_get_window(7, &ws),
		 "W10: the older table's types 5 and 6 are not as they were");
	CW_CHECK(cw_get_window(0, &ws) && ws == 0 && cw_get_window(3, &ws) && ws == 0,
		 "W10: the older table's types 0 and 3 do not start as 0");
	CW_CHECK(cw_set_window(2, 0) && cw_get_window(2, &ws) && ws == 0, "W10: the older table's type 2 is not kept");
	char *older = cw_window_file();
	CW_CHECK(older && strstr(older, "wait_vsync") && !strstr(older, "v14"), "W10: the older table wrote %s",
		 older ? older : "no file");
	free(older);
	remove(cw_window_path);
	CW_CHECK(cw_set_window(2, 0) && mj_exists(cw_window_path), "W10: the older table did not write at a call");
	cw_window_rmdir();
	PE_UpdateComponent(0);
	struct page *saved = NULL;
	CW_CHECK(PE_Save(&saved) && saved && ca_save_version(saved) == 3, "W10: the save is XPE v%d, want 3",
		 saved ? ca_save_version(saved) : -1);
	ca_free_buffer(saved);
	printf("W10 checked\n");
}

/* --- W11 --- */
static void cw_no_grid(void)
{
	cw_setup(false);
	static const struct cw_box cap = { .cg = "cwbox", .text = " abc", .size = 16 };
	struct ex_tree *kids;
	struct ex *e = lb_activity("ConfigWidgetsLine", 1, &kids);
	kids->children[0] = cw_box_node("Text", 100, 100, &cap);
	int act = cw_load("ConfigWidgetsLine", e);
	int no = cw_no(act, "Text");
	heap_unref(act);
	if (!no)
		return;
	/* Without the glyph grid the text is the one line command 8 draws
	 * there: as wide as the engine measures it and the font's size high. */
	const struct bt_style style = { .size = 16, .rgb = CW_FONT_RGB, .edge_rgb = CW_EDGE_RGB };
	struct text_style ts = bt_text_style(&style, 0);
	const int tw = ceilf(gfx_size_text(&ts, cap.text)), w = 23 + tw;
	CW_CHECK(tw > 0, "W11: the line has no width");
	for (int checked = 0; checked < 2; checked++) {
		const int ids[3] = { checked ? CW_CN : CW_N, checked ? CW_CON : CW_ON, checked ? CW_CDOWN : CW_DOWN };
		for (int i = 0; i < 3; i++) {
			uint8_t *model = calloc((size_t)w * 23, 4);
			for (int y = 0; y < 23; y++)
				memcpy(model + (size_t)y * w * 4, cw_cg[ids[i]].px + y * 23 * 4, 23 * 4);
			bt_model_line(model, w, 23, true, 8, 23, (23 - 16) / 2, cap.text, &style, 0);
			cw_compare(cw_state_pixels(no, i, w, 23, "W11 a caption"), w, 23, model,
				   checked ? "W11 a checked caption" : "W11 a caption");
			free(model);
		}
		cw_check(no, true);
		cw_update();
	}
	CW_CHECK(cw_checked(no), "W11: the box is not checked");
	printf("W11 checked\n");
}

/* --- W12 --- */
/* v14 numbers SystemService's window settings as the game's scripts do
 * (config::detail): 0 keeps the aspect ratio, 1 is the scaling type, 2
 * minimizes a deactivated full-screen window, 3 and 4 ask before "back to
 * title" and before closing. The original (0x4c10c0, 0x4c1160) takes these
 * five and no other, starts each as 1 (0x4c5a30), keeps value != 0 for all
 * but the scaling type, which is kept as the number it is. */
static void cw_window(void)
{
	cw_setup(true);
	cw_window_dir();
	int v;

	/* as the engine starts without a settings file */
	cw_window_reload();
	cw_expect_window("W12 at the start", 1, 1, 1, 1, 1);
	CW_CHECK(!mj_exists(cw_window_path), "W12: reading the settings made a file");

	/* the settings page writes every value back each frame: nothing is
	 * written while nothing changes */
	for (int frame = 0; frame < 3; frame++)
		for (int t = 0; t < 5; t++)
			CW_CHECK(cw_set_window(t, 1), "W12: SetWindowSetting(%d, 1) was refused", t);
	CW_CHECK(!mj_exists(cw_window_path), "W12: setting the values they have wrote the file");

	/* each type alone: the other four stay, and the file follows */
	for (int t = 0; t < 5; t++) {
		CW_CHECK(cw_set_window(t, 0), "W12: SetWindowSetting(%d, 0) was refused", t);
		cw_expect_window("W12 one type cleared", t != 0, t != 1, t != 2, t != 3, t != 4);
		CW_CHECK(mj_exists(cw_window_path), "W12: a value that changed (type %d) was not written", t);
		remove(cw_window_path);
		CW_CHECK(cw_set_window(t, 0) && !mj_exists(cw_window_path), "W12: type %d set to 0 again wrote the file", t);
		CW_CHECK(cw_set_window(t, 1), "W12: SetWindowSetting(%d, 1) was refused", t);
	}
	cw_expect_window("W12 all set again", 1, 1, 1, 1, 1);

	/* type 2 is the minimizing, not the vertical sync of the older table:
	 * that flag is left alone and no "wait_vsync" is written */
	CW_CHECK(cw_older_vsync() == 0, "W12: the older table's vertical sync flag is %d before type 2 is set", cw_older_vsync());
	CW_CHECK(cw_set_window(2, 0) && cw_set_window(2, 1), "W12: type 2 was refused");
	char *file = cw_window_file();
	CW_CHECK(cw_older_vsync() == 0, "W12: setting type 2 set the older table's vertical sync flag");
	CW_CHECK(file && !strstr(file, "wait_vsync"), "W12: setting type 2 wrote a vertical sync setting: %s",
		 file ? file : "(no file)");
	free(file);

	/* a flag is 0 or 1; the scaling type is the number it was given */
	CW_CHECK(cw_set_window(0, 7) && cw_set_window(1, 7) && cw_set_window(2, -1) && cw_set_window(3, 7)
		 && cw_set_window(4, 2), "W12: a value other than 0 or 1 was refused");
	cw_expect_window("W12 values other than 0 and 1", 1, 7, 1, 1, 1);

	/* any other type: refused, nothing written to the output */
	static const int bad[] = { 5, 6, -1, 100 };
	for (unsigned i = 0; i < sizeof(bad) / sizeof(bad[0]); i++) {
		v = -1;
		CW_CHECK(!cw_set_window(bad[i], 1), "W12: SetWindowSetting took type %d", bad[i]);
		CW_CHECK(!cw_get_window(bad[i], &v) && v == -77, "W12: GetWindowSetting(%d) answered, with %d", bad[i], v);
	}
	cw_expect_window("W12 after the refused types", 1, 7, 1, 1, 1);

	/* written and read back as at the next start */
	CW_CHECK(cw_set_window(0, 0) && cw_set_window(1, 2) && cw_set_window(2, 0) && cw_set_window(4, 0),
		 "W12: a setting was refused");
	cw_expect_window("W12 a set of values", 0, 2, 0, 1, 0);
	char *kept = cw_window_file();
	CW_CHECK(kept && strstr(kept, "\"v14\""), "W12: the file has no \"v14\" object: %s", kept ? kept : "(no file)");
	CW_CHECK(cw_set_window(0, 1) && cw_set_window(1, 1) && cw_set_window(2, 1) && cw_set_window(3, 0)
		 && cw_set_window(4, 1), "W12: a setting was refused");
	cw_expect_window("W12 another set of values", 1, 1, 1, 0, 1);
	if (kept) {
		cw_window_write(kept);
		cw_window_reload();
		cw_expect_window("W12 read back", 0, 2, 0, 1, 0);
	}
	free(kept);

	/* A file from before this numbering: what the older table wrote for
	 * its type 2 is neither a value of this type 2 nor a vertical sync
	 * setting here, and is left out of the next file. */
	cw_window_write("{\n\t\"wait_vsync\":\ttrue\n}");
	cw_window_reload();
	cw_expect_window("W12 an older file", 0, 2, 0, 1, 0);
	CW_CHECK(cw_older_vsync() == 0, "W12: an older file's wait_vsync was read as the vertical sync flag");
	CW_CHECK(cw_set_window(2, 1), "W12: type 2 was refused");
	file = cw_window_file();
	CW_CHECK(file && strstr(file, "\"v14\"") && !strstr(file, "wait_vsync"),
		 "W12: the file written over an older one is %s", file ? file : "(no file)");
	free(file);

	/* a file with some of the values, a wrong one and an unknown one */
	cw_window_write("{\"v14\": {\"scaling_type\": 3, \"close_game_confirm\": 1, \"aspect_ratio\": \"x\", \"other\": 9}}");
	cw_window_reload();
	cw_expect_window("W12 a file with two of the values", 0, 3, 1, 1, 1);
	cw_window_write("not a settings file");
	cw_window_reload();
	cw_expect_window("W12 a file that is not one", 0, 3, 1, 1, 1);

	cw_window_rmdir();
	printf("W12 checked\n");
}

/* --- W13 --- */
/* A click is applied at the next update of the parts (W4, W5). What it must
 * not do in between: reach another parts that took the number, a parts that
 * is no longer the check box it was, or survive a reset of the input. */
static void cw_click_record(void)
{
	cw_setup(true);
	static const struct cw_box plain = { .cg = "cwbox", .text = "" };
	struct ex_tree *kids;
	struct ex *e = lb_activity("ConfigWidgetsRecord", 4, &kids);
	static const char *names[4] = { "A", "B", "C", "D" };
	for (int i = 0; i < 4; i++)
		kids->children[i] = cw_box_node(names[i], 100 + 200 * i, 100, &plain);
	int act = cw_load("ConfigWidgetsRecord", e);
	int a = cw_no(act, "A"), b = cw_no(act, "B"), c = cw_no(act, "C"), d = cw_no(act, "D");
	heap_unref(act);
	if (!a || !b || !c || !d)
		return;
	cp_register_event(a, 3, 30);
	cp_register_event(b, 4, 40);
	cp_register_event(c, 5, 50);
	cp_register_event(d, 6, 60);

	/* released between the press and the update, and its number taken by
	 * a new check box */
	cw_press(110, 110);
	CW_MESSAGES("W13 a press on the first box", { 4, a, 110 });
	PE_ReleaseParts(a);
	CW_CHECK(!parts_try_get(a), "W13: the box was not released");
	cw_check(a, false);
	CW_CHECK(parts_try_get(a) && cw_type(a) == 1 && !cw_checked(a), "W13: no new check box took the number");
	/* the same event ID again, so that only the release can have dropped
	 * the click (a parts with another ID is skipped by the update anyway) */
	cp_register_event(a, 3, 30);
	cw_update();
	CW_CHECK(!cw_checked(a), "W13: the click on a released box checked the box that took its number");

	/* no longer a check box, then one again: the constructor's values */
	cw_press(310, 110);
	CW_MESSAGES("W13 a press on the second box", { 4, b, 310 });
	PE_SetComponentType(b, 0, 1);
	PE_SetComponentType(b, 1, 1);
	cw_update();
	CW_CHECK(cw_type(b) == 1 && !cw_checked(b), "W13: the click outlived the box's widget (type %d, checked %d)",
		 cw_type(b), cw_checked(b));
	cw_expect_quiet("W13 a box that was made again");

	/* another event ID by the time of the update */
	cw_press(510, 110);
	CW_MESSAGES("W13 a press on the third box", { 4, c, 510 });
	cp_register_event(c, 5, 51);
	cw_update();
	CW_CHECK(!cw_checked(c), "W13: the click was applied to a box with another event ID");
	cw_expect_quiet("W13 a box with another event ID");

	/* the input is reset (PE_Init, PE_Reset) */
	cw_press(710, 110);
	CW_MESSAGES("W13 a press on the fourth box", { 4, d, 710 });
	parts_reset_input();
	cw_update();
	CW_CHECK(!cw_checked(d), "W13: the click outlived a reset of the input");
	cw_expect_quiet("W13 after a reset of the input");

	/* and a click nothing happened to is applied, once */
	cw_click(710, 110);
	CW_MESSAGES("W13 a click", { 4, d, 710 }, { 23, d, 1 });
	cw_update();
	cw_expect_quiet("W13 a second update");
	CW_CHECK(cw_checked(d), "W13: the box is not checked");

	/* Nine presses before one update: eight are kept. The ninth is
	 * dropped with a warning, which leaves the value where it was. */
	for (int i = 0; i < 9; i++)
		cw_press(710, 110);
	cw_update();
	struct cw_msg m[1];
	int n = cw_drain(m, 1);
	CW_CHECK(n == 17, "W13: nine presses and an update sent %d message(s), want 9 MouseClick and 8 ChangedFlg", n);
	CW_CHECK(cw_checked(d), "W13: nine presses before an update left the box unchecked");
	printf("W13 checked\n");
}

static void (*cw_case)(void);
static void cw_case_body(void)
{
	cw_case();
	cw_failures += bt_failures + cn_failures + cc_failures;
}

static int test_config_widgets(void)
{
	static const struct { const char *name; void (*fn)(void); } cases[] = {
		{ "W1", cw_plain }, { "W2", cw_text }, { "W3", cw_values }, { "W4", cw_clicks }, { "W5", cw_loop },
		{ "W6", cw_radio }, { "W7", cw_hbar }, { "W8", cw_missing }, { "W9", cw_save }, { "W10", cw_v13 },
		{ "W11", cw_no_grid }, { "W12", cw_window }, { "W13", cw_click_record },
	};
	const int n = sizeof(cases) / sizeof(cases[0]);
	int failed = 0, env = 0;
	for (int i = 0; i < n; i++) {
		cw_case = cases[i].fn;
		int r = cn_fork_case(cases[i].name, cw_case_body, &cw_failures);
		failed += r == 1;
		env += r == 2;
	}
	if (env) {
		printf("CONFIG-WIDGETS ENVIRONMENT FAILURE: %d of %d cases could not run, %d failed\n", env, n, failed);
		return 2;
	}
	printf("CONFIG-WIDGETS %s: %d of %d cases failed\n", failed ? "FAIL" : "PASS", failed, n);
	return failed ? 1 : 0;
}
