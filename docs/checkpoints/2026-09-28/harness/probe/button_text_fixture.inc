/* button-text: the text a v14 按鈕 (EPartsType 0) draws on its own states, as
 * the original engine does it (static disassembly only):
 *   - the pactex loader (0x527910) reads the button's 文本 (+0x1e4), its font
 *     (フォントタイプ, フォントサイズ, フォント色, フォント太さ, フォント縁取り,
 *     フォント縁取り色; +0xc0), 文本位置 (an int kept as a byte, +0x1fc),
 *     文字間隔 (+0x200) and 行間隔 (+0x204); a key that is missing is 0;
 *   - the rebuild (0x528870) makes each state from its CG (／普通, ／オン,
 *     ／ダウン; ／無効 in all three while the button is not enabled,
 *     0x529280) by 0x528d80: the CG becomes the surface (command 2) and the
 *     text is drawn on it by command 7 (0x569410 -> 0x4f4b10: the colours
 *     blend by the glyph's alpha, the CG keeps its alpha), whatever the text
 *     is: an empty one, or "\n", draws nothing;
 *   - the place (0x5288f0) is on that state's CG, W x H (0x4642a0): with
 *     the text tw x th measured as the check box's (0x528aa0), 文本位置 1 .. 9
 *     is x = 3, (W - tw - 6) / 2 + 3 or W - tw - 3 and y = 3,
 *     (H - th - 6) / 2 + 3 or H - th - 3, the halves rounded towards 0 (cdq;
 *     sub; sar 1); any other value leaves (0, 0).
 * Before the fix the states were the CGs alone: the 初始化 buttons of the
 * settings' pages 3 and 4 had no text. Most of the game's other buttons with
 * a text have "\n" (their captions are inner parts of their own) or no CG.
 *
 * The CGs are PNG files made in memory (cg_assets_shadow.inc), the glyphs
 * come from backlog-text's pattern font, and the model lays the cells out
 * with that fixture's command 7. The model's places are the formula above,
 * worked out here; a few are also written out by hand.
 *
 * Cases (each in its own fork):
 *   BX1 the settings' 初始化 button: the three states against the model, the
 *       text at (41, 17) on ／普通 and ／オン (130 x 44) and at (40, 15) on
 *       ／ダウン (128 x 40); the CG's alpha is kept under the text; a font
 *       with an edge, a weight and a negative character spacing; the size
 *       and the click of the button.
 *   BX2 文本位置 1 .. 9 on a 60 x 40 CG; 0 and 10 at (0, 0); 261 kept as the
 *       byte 5; halves of -5 and -3 rounded towards 0 (x 1, y 2).
 *   BX3 texts that draw nothing: "\n" (the game's tabs and title buttons),
 *       "\n\n", "", no 文本: the states are the CGs; "\n" followed by a
 *       character draws it on the second line.
 *   BX4 SetButtonEnable through its AIN declaration: false shows the text on
 *       ／無効 in all three states, true the three CGs again; without a
 *       ／無効 the enabled CGs keep their text (this port's choice).
 *   BX5 a 文本 without the font's keys: size 0 draws nothing; with only
 *       フォントサイズ the text is black at (0, 0).
 *   BX6 PE_Save / PE_Load keep the pixels (XPE version 12).
 *   BX7 engines before v14 (guard): no text.
 *   BX8 a 文本 key spelled in SJIS (a guess; no SJIS pactex was read): no
 *       text (guard).
 * Functions the fix adds are not called: the file builds before the fix.
 */
static int bx_failures;
#define BX_CHECK(c, ...) do { if (!(c)) { printf("  FAIL: "); printf(__VA_ARGS__); printf("\n"); bx_failures++; } } while (0)

#define BX_BUTTON "\xb0\xb4\xe2\x6f"						/* 按鈕 */
#define BX_K_CG "\xa3\xc3\xa3\xc7\xc3\xfb"					/* ＣＧ名 */
#define BX_K_TEXT "\xce\xc4\xb1\xbe"						/* 文本 */
#define BX_K_TEXT_SJIS "\x83\x65\x83\x4c\x83\x58\x83\x67"			/* テキスト (SJIS) */
#define BX_K_TEXT_POS "\xce\xc4\xb1\xbe\xce\xbb\xd6\xc3"			/* 文本位置 */
#define BX_K_CHAR_SPACE "\xce\xc4\xd7\xd6\xe9\x67\xb8\xf4"			/* 文字間隔 */
#define BX_K_LINE_SPACE "\xd0\xd0\xe9\x67\xb8\xf4"				/* 行間隔 */
#define BX_K_FONT_TYPE "\xa5\xd5\xa5\xa9\xa5\xf3\xa5\xc8\xa5\xbf\xa5\xa4\xa5\xd7" /* フォントタイプ */
#define BX_K_FONT_SIZE "\xa5\xd5\xa5\xa9\xa5\xf3\xa5\xc8\xa5\xb5\xa5\xa4\xa5\xba" /* フォントサイズ */
#define BX_K_FONT_COLOR "\xa5\xd5\xa5\xa9\xa5\xf3\xa5\xc8\xc9\xab"		/* フォント色 */
#define BX_K_FONT_BOLD "\xa5\xd5\xa5\xa9\xa5\xf3\xa5\xc8\xcc\xab\xa4\xb5"	/* フォント太さ */
#define BX_K_FONT_EDGE "\xa5\xd5\xa5\xa9\xa5\xf3\xa5\xc8\xbf\x46\xc8\xa1\xa4\xea" /* フォント縁取り */
#define BX_K_FONT_EDGE_COLOR BX_K_FONT_EDGE "\xc9\xab"				/* フォント縁取り色 */

/* The base names have the GBK ／ in them, as the game's do: the suffixes
 * follow the encoding of the name (parts_button_apply_cg). */
#define BX_SLASH "\xa3\xaf"			/* ／ */
#define BX_S_N "\xa3\xaf\xc6\xd5\xcd\xa8"		/* ／普通 */
#define BX_S_ON "\xa3\xaf\xa5\xaa\xa5\xf3"		/* ／オン */
#define BX_S_DOWN "\xa3\xaf\xa5\xc0\xa5\xa6\xa5\xf3"	/* ／ダウン */
#define BX_S_OFF "\xa3\xaf\x9f\x6f\x84\xbf"		/* ／無効 */

#define BX_TEXT3 "\xd2\xbb\xb6\xfe\xc8\xfd"	/* three GBK characters */
#define BX_TEXT2 "\xd2\xbb\xb6\xfe"		/* two */

/* --- the CGs --- */
enum {
	BX_N, BX_ON, BX_DOWN, BX_OFF,		/* bxbtn, as the game's 128-wide button CG */
	BX_G_N, BX_G_ON, BX_G_DOWN,		/* bxgrid 60 x 40 */
	BX_R_N, BX_R_ON, BX_R_DOWN,		/* bxnarrow 21 x 13 */
	BX_NO_N, BX_NO_ON, BX_NO_DOWN,		/* bxnooff: no ／無効 */
	BX_NR_CG
};
static struct {
	const char *name;
	int w, h;
	uint8_t *px;	/* RGBA */
} bx_cg[BX_NR_CG] = {
	[BX_N] = { "bx" BX_SLASH "btn" BX_S_N, 130, 44 }, [BX_ON] = { "bx" BX_SLASH "btn" BX_S_ON, 130, 44 },
	[BX_DOWN] = { "bx" BX_SLASH "btn" BX_S_DOWN, 128, 40 }, [BX_OFF] = { "bx" BX_SLASH "btn" BX_S_OFF, 130, 44 },
	[BX_G_N] = { "bx" BX_SLASH "grid" BX_S_N, 60, 40 }, [BX_G_ON] = { "bx" BX_SLASH "grid" BX_S_ON, 60, 40 },
	[BX_G_DOWN] = { "bx" BX_SLASH "grid" BX_S_DOWN, 60, 40 },
	[BX_R_N] = { "bx" BX_SLASH "narrow" BX_S_N, 21, 13 }, [BX_R_ON] = { "bx" BX_SLASH "narrow" BX_S_ON, 21, 13 },
	[BX_R_DOWN] = { "bx" BX_SLASH "narrow" BX_S_DOWN, 21, 13 },
	[BX_NO_N] = { "bx" BX_SLASH "nooff" BX_S_N, 50, 30 }, [BX_NO_ON] = { "bx" BX_SLASH "nooff" BX_S_ON, 50, 30 },
	[BX_NO_DOWN] = { "bx" BX_SLASH "nooff" BX_S_DOWN, 50, 30 },
};
static struct probe_cg_file bx_files[BX_NR_CG];

/* A CG's pixel: the red tells the CGs apart; the frame is opaque and the
 * inside has an alpha between 120 and 239, which a text drawn by command 7
 * keeps and command 8 would not. */
static void bx_cg_pixel(int id, int x, int y, uint8_t *p)
{
	const int w = bx_cg[id].w, h = bx_cg[id].h;
	p[0] = 30 + id * 15;
	p[1] = 200 - (x * 3) % 150;
	p[2] = 40 + (y * 5) % 180;
	p[3] = (!x || !y || x == w - 1 || y == h - 1) ? 255 : 120 + (x * 7 + y * 11) % 120;
}

static void bx_setup(bool gbk)
{
	bt_gl_setup(gbk);
	br_input_setup();
	for (int i = 0; i < BX_NR_CG; i++) {
		const int w = bx_cg[i].w, h = bx_cg[i].h;
		uint8_t *px = bx_cg[i].px = calloc((size_t)w * h, 4);
		for (int k = 0; k < w * h; k++)
			bx_cg_pixel(i, k % w, k / w, px + k * 4);
		bx_files[i].name = bx_cg[i].name;
		bx_files[i].data = cg_png(w, h, px, true, &bx_files[i].size);
	}
	probe_install_cg_assets(bx_files, BX_NR_CG);
}

/* --- the pactex --- */
struct bx_btn {
	const char *cg;		/* ＣＧ名 */
	const char *text;	/* 文本; NULL: no key */
	bool sjis_text_key;	/* the text under an SJIS spelling */
	bool no_font;		/* no font keys, no 文本位置, no spacings */
	bool size_only;		/* of those, only フォントサイズ */
	int size;		/* フォントサイズ */
	int pos;		/* 文本位置 */
	int char_space, line_space;
	float bold;		/* フォント太さ */
	int edge;		/* フォント縁取り */
	int rgb[3];		/* フォント色 */
};

static struct ex_tree bx_node(const char *name, int x, int y, const struct bx_btn *b)
{
	struct lb_place pl = { x, y, 1 };
	struct ex_tree info = bu_branch(BU_INFO, 12);
	unsigned k = 0;
	info.children[k++] = bu_sleaf(BU_TYPE, BX_BUTTON);
	info.children[k++] = bu_sleaf(BX_K_CG, b->cg);
	if (b->text)
		info.children[k++] = bu_sleaf(b->sjis_text_key ? BX_K_TEXT_SJIS : BX_K_TEXT, b->text);
	if (!b->no_font)
		info.children[k++] = bu_ileaf(BX_K_FONT_SIZE, b->size);
	if (!b->no_font && !b->size_only) {
		info.children[k++] = bu_ileaf(BX_K_FONT_TYPE, 0);
		info.children[k++] = lb_ilist(BX_K_FONT_COLOR, 3, b->rgb);
		info.children[k++] = bu_leaf(BX_K_FONT_BOLD, (struct ex_value){ .type = EX_FLOAT, .f = b->bold });
		info.children[k++] = bu_leaf(BX_K_FONT_EDGE, (struct ex_value){ .type = EX_FLOAT, .f = b->edge });
		info.children[k++] = lb_ilist(BX_K_FONT_EDGE_COLOR, 3, (int[]){ 9, 8, 7 });
		info.children[k++] = bu_ileaf(BX_K_TEXT_POS, b->pos);
		info.children[k++] = bu_ileaf(BX_K_CHAR_SPACE, b->char_space);
		info.children[k++] = bu_ileaf(BX_K_LINE_SPACE, b->line_space);
	}
	info.nr_children = k;
	return lb_node(name, &pl, info, 0);
}

static int bx_load(const char *name, struct ex *e)
{
	int act = cn_load(name, e);
	bx_failures += cn_failures;
	cn_failures = 0;
	return act;
}
static int bx_no(int act, const char *name)
{
	int n = bu_number(act, name);
	BX_CHECK(n > 0 && parts_try_get(n), "%s has no parts", name);
	return n > 0 && parts_try_get(n) ? n : 0;
}

/* --- the model --- */
static int bx_size(const struct bx_btn *b) { return b->no_font ? 0 : b->size; }

/* e: the larger of the weight and the edge, each rounded up, at most the size. */
static int bx_edge(const struct bx_btn *b)
{
	const int size = bx_size(b);
	if (b->no_font || b->size_only)
		return 0;
	int bold = (int)b->bold;
	if ((float)bold < b->bold)
		bold++;
	bold = bold > size ? size : bold;
	const int edge = b->edge > size ? size : b->edge;
	return bold > edge ? bold : edge;
}

/* The text's size on the glyph grid (0x528aa0). */
static void bx_text_size(const struct bx_btn *b, int *w, int *h)
{
	*w = *h = 0;
	if (!b->text || !b->text[0])
		return;
	const int size = bx_size(b), e = bx_edge(b);
	const int cs = b->no_font || b->size_only ? 0 : b->char_space;
	const int ls = b->no_font || b->size_only ? 0 : b->line_space;
	const int line = size + (size & 1) + 2 * e;
	int width = 0, row = 0, height = line;
	for (const unsigned char *p = (const unsigned char *)b->text; *p; p++) {
		if (*p == '\n') {
			height += ls + line;
			width = row > width ? row : width;
			row = 0;
			continue;
		}
		const bool wide = *p >= 0x81 && *p <= 0xfe;
		row += cs + (wide ? size : (size + 1) / 2) + 2 * e;
		if (wide && p[1])
			p++;
	}
	*w = (row > width ? row : width) - cs;
	*h = height - ls;
}

/* C's division of int, towards 0, written out. */
static int bx_half(int v) { return v >= 0 ? v / 2 : -((-v) / 2); }

/* The place of the text on a W x H CG (0x5288f0). */
static void bx_place(const struct bx_btn *b, int w, int h, int *x, int *y)
{
	int tw, th;
	bx_text_size(b, &tw, &th);
	const int pos = b->no_font || b->size_only ? 0 : (b->pos & 0xff);
	*x = *y = 0;
	if (pos < 1 || pos > 9)
		return;
	const int dx = w - tw - 6, dy = h - th - 6;
	static const int col[9] = { 0, 1, 2, 0, 1, 2, 0, 1, 2 };
	*x = col[pos - 1] == 0 ? 3 : col[pos - 1] == 1 ? bx_half(dx) + 3 : dx + 3;
	*y = pos <= 3 ? 3 : pos <= 6 ? bx_half(dy) + 3 : dy + 3;
}

/* One state: the CG with the text drawn on it by command 7. To be freed. */
static uint8_t *bx_model(const struct bx_btn *b, int cg)
{
	const int w = bx_cg[cg].w, h = bx_cg[cg].h;
	uint8_t *px = malloc((size_t)w * h * 4);
	memcpy(px, bx_cg[cg].px, (size_t)w * h * 4);
	if (!b->text || bx_size(b) <= 0)
		return px;
	int x, y;
	bx_place(b, w, h, &x, &y);
	const bool font = !b->no_font && !b->size_only;
	const struct bt_style style = { .size = bx_size(b),
		.rgb = { font ? b->rgb[0] : 0, font ? b->rgb[1] : 0, font ? b->rgb[2] : 0 },
		.bold = font ? b->bold : 0, .edge = font ? b->edge : 0, .edge_rgb = { font ? 9 : 0, font ? 8 : 0, font ? 7 : 0 } };
	bt_model_grid(px, w, h, true, 7, x, y, b->text, &style, font ? b->char_space : 0, font ? b->line_space : 0);
	return px;
}

static uint8_t *bx_state_pixels(int no, int state, int w, int h, const char *what)
{
	struct parts *p = no > 0 ? parts_try_get(no) : NULL;
	struct parts_common *c = p ? &p->states[state].common : NULL;
	bool ok = c && c->texture.handle && c->texture.w == w && c->texture.h == h && c->w == w && c->h == h;
	BX_CHECK(ok, "%s: state %d is %dx%d with a %dx%d texture, want %dx%d", what, state + 1, c ? c->w : -1,
		 c ? c->h : -1, c && c->texture.handle ? c->texture.w : 0, c && c->texture.handle ? c->texture.h : 0, w, h);
	return ok ? gfx_get_pixels(&c->texture) : NULL;
}
/* The number of pixels that differ (the first three are reported). */
static int bx_compare(uint8_t *got, int w, int h, const uint8_t *model, const char *what)
{
	int bad = 0;
	for (int i = 0; got && i < w * h; i++) {
		if (!memcmp(got + i * 4, model + i * 4, 4))
			continue;
		if (bad < 3)
			BX_CHECK(0, "%s (%d,%d) = (%d,%d,%d,%d), want (%d,%d,%d,%d)", what, i % w, i / w, got[i * 4],
				 got[i * 4 + 1], got[i * 4 + 2], got[i * 4 + 3], model[i * 4], model[i * 4 + 1],
				 model[i * 4 + 2], model[i * 4 + 3]);
		bad++;
	}
	if (bad > 3)
		BX_CHECK(0, "%s: %d pixels differ in all", what, bad);
	free(got);
	return bad;
}
/* The three states are these CGs with the button's text (none for a text
 * that draws nothing). */
static void bx_expect(int no, const struct bx_btn *b, int n, int on, int down, const char *what)
{
	const int ids[3] = { n, on, down };
	for (int i = 0; i < 3; i++) {
		char label[160];
		snprintf(label, sizeof label, "%s, state %d", what, i + 1);
		const int w = bx_cg[ids[i]].w, h = bx_cg[ids[i]].h;
		uint8_t *model = bx_model(b, ids[i]);
		bx_compare(bx_state_pixels(no, i, w, h, label), w, h, model, label);
		free(model);
	}
}
/* The bounding box of the pixels where the model's state is not the bare CG. */
static void bx_text_box(const struct bx_btn *b, int cg, int *x0, int *y0, int *x1, int *y1)
{
	const int w = bx_cg[cg].w, h = bx_cg[cg].h;
	uint8_t *px = bx_model(b, cg);
	*x0 = *y0 = 1 << 20;
	*x1 = *y1 = -1;
	for (int i = 0; i < w * h; i++) {
		if (!memcmp(px + i * 4, bx_cg[cg].px + i * 4, 4))
			continue;
		const int x = i % w, y = i / w;
		*x0 = x < *x0 ? x : *x0; *y0 = y < *y0 ? y : *y0;
		*x1 = x > *x1 ? x : *x1; *y1 = y > *y1 ? y : *y1;
	}
	free(px);
}

/* --- BX1 --- */
static void bx_game(void)
{
	bx_setup(true);
	static const struct bx_btn init = { .cg = "bx" BX_SLASH "btn", .text = BX_TEXT3, .size = 16, .pos = 5, .line_space = 6 },
		lit = { .cg = "bx" BX_SLASH "btn", .text = BX_TEXT2, .size = 20, .pos = 5, .char_space = -1, .line_space = 12,
			.bold = 0.5f, .edge = 2, .rgb = { 250, 240, 230 } };
	struct ex_tree *kids;
	struct ex *e = lb_activity("ButtonTextGame", 2, &kids);
	kids->children[0] = bx_node("Init", 968, 218, &init);
	kids->children[1] = bx_node("Lit", 400, 300, &lit);
	int act = bx_load("ButtonTextGame", e);
	int no = bx_no(act, "Init"), lno = bx_no(act, "Lit");
	heap_unref(act);
	if (!no || !lno)
		return;
	/* Three characters of 16 with a line spacing of 6: 48 x (16 - 6) = 48
	 * x 10, the spacing being taken off the one line (0x528aa0). On 130 x
	 * 44 the text lies at ((130 - 48 - 6) / 2 + 3, (44 - 10 - 6) / 2 + 3)
	 * = (41, 17); on the pressed CG, 128 x 40, at (40, 15). Its cells are
	 * 16 high: rows 17 to 32. */
	int tw, th, x, y;
	bx_text_size(&init, &tw, &th);
	BX_CHECK(tw == 48 && th == 10, "BX1: the model's text is %dx%d, want 48x10", tw, th);
	bx_place(&init, 130, 44, &x, &y);
	BX_CHECK(x == 41 && y == 17, "BX1: the model's place on 130x44 is (%d,%d), want (41,17)", x, y);
	bx_place(&init, 128, 40, &x, &y);
	BX_CHECK(x == 40 && y == 15, "BX1: the model's place on 128x40 is (%d,%d), want (40,15)", x, y);
	int x0, y0, x1, y1;
	bx_text_box(&init, BX_N, &x0, &y0, &x1, &y1);
	BX_CHECK(x0 >= 41 && y0 >= 17 && x1 < 41 + 48 && y1 <= 32 && x1 > x0,
		 "BX1: the model's text is at (%d,%d)-(%d,%d), not in its cells from (41,17)", x0, y0, x1, y1);
	bx_expect(no, &init, BX_N, BX_ON, BX_DOWN, "BX1 the settings' button");
	/* Under a glyph the CG's alpha stays (command 7, not 8). */
	uint8_t *px = bx_state_pixels(no, 0, 130, 44, "BX1 the settings' button");
	if (px) {
		int kept = 0, changed = 0;
		for (int i = 0; i < 130 * 44; i++) {
			if (memcmp(px + i * 4, bx_cg[BX_N].px + i * 4, 3))
				changed++, kept += px[i * 4 + 3] == bx_cg[BX_N].px[i * 4 + 3];
		}
		BX_CHECK(changed > 0 && kept == changed, "BX1: %d of %d pixels under the text kept the CG's alpha", kept, changed);
		free(px);
	}
	struct parts *p = parts_try_get(no);
	BX_CHECK(p->states[0].common.w == 130 && p->states[0].common.h == 44, "BX1: the button is %dx%d, want 130x44",
		 p->states[0].common.w, p->states[0].common.h);
	BX_CHECK(cp_clickable(no), "BX1: the button cannot be clicked");
	/* An edge of 2 and a weight of 0.5: e is 2, the cells 24 wide and the
	 * line 24 high; two cells with a spacing of -1, one spacing back, and
	 * the line spacing of 12 off: 47 x 12, at (41, 16) and (40, 14). */
	bx_text_size(&lit, &tw, &th);
	BX_CHECK(tw == 47 && th == 12, "BX1: the model's lit text is %dx%d, want 47x12", tw, th);
	bx_place(&lit, 130, 44, &x, &y);
	BX_CHECK(x == 41 && y == 16, "BX1: the model's lit place on 130x44 is (%d,%d), want (41,16)", x, y);
	bx_expect(lno, &lit, BX_N, BX_ON, BX_DOWN, "BX1 a font with an edge");
	printf("BX1 checked\n");
}

/* --- BX2 --- */
static void bx_places(void)
{
	bx_setup(true);
	enum { NR = 13 };
	static struct bx_btn b[NR];
	static const int pos[NR] = { 1, 2, 3, 4, 5, 6, 7, 8, 9, 0, 10, 261, 5 };
	static char names[NR][8];
	struct ex_tree *kids;
	struct ex *e = lb_activity("ButtonTextPlaces", NR, &kids);
	for (int i = 0; i < NR; i++) {
		b[i] = (struct bx_btn){ .cg = i < 12 ? "bx" BX_SLASH "grid" : "bx" BX_SLASH "narrow", .text = BX_TEXT2, .size = 10, .pos = pos[i],
			.rgb = { 10, 20, 30 } };
		snprintf(names[i], sizeof names[i], "P%d", i);
		kids->children[i] = bx_node(names[i], 10 + 70 * (i % 7), 10 + 60 * (i / 7), &b[i]);
	}
	int act = bx_load("ButtonTextPlaces", e);
	int no[NR];
	bool all = true;
	for (int i = 0; i < NR; i++)
		all = (no[i] = bx_no(act, names[i])) && all;
	heap_unref(act);
	if (!all)
		return;
	/* 20 x 10 on 60 x 40: x 3, 20, 37 and y 3, 15, 27. */
	static const int want[12][2] = { { 3, 3 }, { 20, 3 }, { 37, 3 }, { 3, 15 }, { 20, 15 }, { 37, 15 },
		{ 3, 27 }, { 20, 27 }, { 37, 27 }, { 0, 0 }, { 0, 0 }, { 20, 15 } };
	for (int i = 0; i < 12; i++) {
		int x, y;
		bx_place(&b[i], 60, 40, &x, &y);
		BX_CHECK(x == want[i][0] && y == want[i][1], "BX2: the model's place for %d is (%d,%d), want (%d,%d)", pos[i],
			 x, y, want[i][0], want[i][1]);
		char what[64];
		snprintf(what, sizeof what, "BX2 文本位置 %d", pos[i]);
		bx_expect(no[i], &b[i], BX_G_N, BX_G_ON, BX_G_DOWN, what);
	}
	/* On 21 x 13 the halves are of 21 - 20 - 6 = -5 and 13 - 10 - 6 = -3:
	 * -2 and -1 towards 0 (not -3 and -2), so (1, 2). */
	int x, y;
	bx_place(&b[12], 21, 13, &x, &y);
	BX_CHECK(x == 1 && y == 2, "BX2: the model's place on 21x13 is (%d,%d), want (1,2)", x, y);
	bx_expect(no[12], &b[12], BX_R_N, BX_R_ON, BX_R_DOWN, "BX2 halves below 0");
	printf("BX2 checked\n");
}

/* --- BX3 --- */
static void bx_nothing(void)
{
	bx_setup(true);
	static const struct bx_btn nl = { .cg = "bx" BX_SLASH "btn", .text = "\n", .size = 19, .pos = 5, .line_space = 4 },
		nl2 = { .cg = "bx" BX_SLASH "btn", .text = "\n\n", .size = 19, .pos = 5 },
		empty = { .cg = "bx" BX_SLASH "btn", .text = "", .size = 16, .pos = 5 },
		none = { .cg = "bx" BX_SLASH "btn", .size = 16, .pos = 5 },
		second = { .cg = "bx" BX_SLASH "grid", .text = "\n" BX_TEXT2, .size = 10, .pos = 5, .line_space = 3, .rgb = { 200, 0, 0 } };
	struct ex_tree *kids;
	struct ex *e = lb_activity("ButtonTextNothing", 5, &kids);
	kids->children[0] = bx_node("NewLine", 40, 40, &nl);
	kids->children[1] = bx_node("NewLines", 40, 100, &nl2);
	kids->children[2] = bx_node("Empty", 40, 160, &empty);
	kids->children[3] = bx_node("None", 40, 220, &none);
	kids->children[4] = bx_node("Second", 40, 280, &second);
	int act = bx_load("ButtonTextNothing", e);
	int a = bx_no(act, "NewLine"), b = bx_no(act, "NewLines"), c = bx_no(act, "Empty"), d = bx_no(act, "None"),
		s = bx_no(act, "Second");
	heap_unref(act);
	if (!a || !b || !c || !d || !s)
		return;
	static const struct bx_btn bare = { .cg = "bx" BX_SLASH "btn" };
	bx_expect(a, &bare, BX_N, BX_ON, BX_DOWN, "BX3 \"\\n\"");
	bx_expect(b, &bare, BX_N, BX_ON, BX_DOWN, "BX3 \"\\n\\n\"");
	bx_expect(c, &bare, BX_N, BX_ON, BX_DOWN, "BX3 an empty text");
	bx_expect(d, &bare, BX_N, BX_ON, BX_DOWN, "BX3 no 文本");
	/* Two lines of 10 and a spacing of 3: 10 + (3 + 10) - 3 = 20 high, 20
	 * wide, at (20, (40 - 20 - 6) / 2 + 3 = 10); the characters on the
	 * second line, from row 10 + 10 + 3 = 23. */
	int tw, th, x, y, x0, y0, x1, y1;
	bx_text_size(&second, &tw, &th);
	bx_place(&second, 60, 40, &x, &y);
	BX_CHECK(tw == 20 && th == 20 && x == 20 && y == 10, "BX3: the model's text is %dx%d at (%d,%d), want 20x20 at (20,10)",
		 tw, th, x, y);
	bx_text_box(&second, BX_G_N, &x0, &y0, &x1, &y1);
	BX_CHECK(y0 >= 23 && x1 > x0, "BX3: the model draws from row %d, above the second line (23)", y0);
	bx_expect(s, &second, BX_G_N, BX_G_ON, BX_G_DOWN, "BX3 a character after \"\\n\"");
	printf("BX3 checked\n");
}

/* --- BX4 --- */
static void bx_enable(void)
{
	bx_setup(true);
	static const struct bx_btn init = { .cg = "bx" BX_SLASH "btn", .text = BX_TEXT3, .size = 16, .pos = 5, .line_space = 6 },
		nooff = { .cg = "bx" BX_SLASH "nooff", .text = BX_TEXT2, .size = 12, .pos = 5, .rgb = { 255, 255, 0 } };
	struct ex_tree *kids;
	struct ex *e = lb_activity("ButtonTextEnable", 2, &kids);
	kids->children[0] = bx_node("Init", 100, 100, &init);
	kids->children[1] = bx_node("NoOff", 100, 200, &nooff);
	int act = bx_load("ButtonTextEnable", e);
	int no = bx_no(act, "Init"), n2 = bx_no(act, "NoOff");
	heap_unref(act);
	if (!no || !n2)
		return;
	BU_PE("SetButtonEnable", BU_I(no), BU_I(0));
	BU_PE("SetButtonEnable", BU_I(n2), BU_I(0));
	BX_CHECK(!BU_PE("IsButtonEnable", BU_I(no)).i, "BX4: the button is still enabled");
	bx_expect(no, &init, BX_OFF, BX_OFF, BX_OFF, "BX4 a disabled button");
	bx_expect(n2, &nooff, BX_NO_N, BX_NO_ON, BX_NO_DOWN, "BX4 a disabled button without ／無効");
	BU_PE("SetButtonEnable", BU_I(no), BU_I(1));
	bx_expect(no, &init, BX_N, BX_ON, BX_DOWN, "BX4 enabled again");
	printf("BX4 checked\n");
}

/* --- BX5 --- */
static void bx_keys(void)
{
	bx_setup(true);
	static const struct bx_btn nofont = { .cg = "bx" BX_SLASH "btn", .text = BX_TEXT3, .no_font = true },
		sizeonly = { .cg = "bx" BX_SLASH "grid", .text = BX_TEXT2, .size = 10, .size_only = true };
	struct ex_tree *kids;
	struct ex *e = lb_activity("ButtonTextKeys", 2, &kids);
	kids->children[0] = bx_node("NoFont", 100, 100, &nofont);
	kids->children[1] = bx_node("SizeOnly", 100, 200, &sizeonly);
	int act = bx_load("ButtonTextKeys", e);
	int a = bx_no(act, "NoFont"), b = bx_no(act, "SizeOnly");
	heap_unref(act);
	if (!a || !b)
		return;
	static const struct bx_btn bare = { .cg = "bx" BX_SLASH "btn" };
	bx_expect(a, &bare, BX_N, BX_ON, BX_DOWN, "BX5 a text of size 0");
	/* Black (0, 0, 0), at (0, 0): the missing 文本位置 is 0. */
	int x, y;
	bx_place(&sizeonly, 60, 40, &x, &y);
	BX_CHECK(x == 0 && y == 0, "BX5: the model's place without 文本位置 is (%d,%d), want (0,0)", x, y);
	bx_expect(b, &sizeonly, BX_G_N, BX_G_ON, BX_G_DOWN, "BX5 only フォントサイズ");
	printf("BX5 checked\n");
}

/* --- BX6 --- */
static void bx_save(void)
{
	bx_setup(true);
	static const struct bx_btn init = { .cg = "bx" BX_SLASH "btn", .text = BX_TEXT3, .size = 16, .pos = 5, .line_space = 6 };
	struct ex_tree *kids;
	struct ex *e = lb_activity("ButtonTextSave", 1, &kids);
	kids->children[0] = bx_node("Init", 100, 100, &init);
	int act = bx_load("ButtonTextSave", e);
	int no = bx_no(act, "Init");
	heap_unref(act);
	if (!no)
		return;
	bx_expect(no, &init, BX_N, BX_ON, BX_DOWN, "BX6 before the save");
	PE_UpdateComponent(0);
	struct page *saved = NULL;
	BX_CHECK(PE_Save(&saved) && saved, "BX6: PE_Save failed");
	if (!saved)
		return;
	BX_CHECK(ca_save_version(saved) == 12, "BX6: the save is XPE v%d, want 12", ca_save_version(saved));
	BX_CHECK(PE_Load(&saved), "BX6: PE_Load failed");
	PE_UpdateComponent(0);
	ca_free_buffer(saved);
	bx_expect(no, &init, BX_N, BX_ON, BX_DOWN, "BX6 after PE_Load");
	/* Nothing of the button's values is saved: enabling it again changes
	 * nothing and breaks nothing. */
	BU_PE("SetButtonEnable", BU_I(no), BU_I(0));
	BU_PE("SetButtonEnable", BU_I(no), BU_I(1));
	bx_expect(no, &init, BX_N, BX_ON, BX_DOWN, "BX6 a loaded button disabled and enabled");
	printf("BX6 checked\n");
}

/* --- BX7 --- */
static void bx_v13(void)
{
	ain->version = 13;
	bx_setup(true);
	static const struct bx_btn init = { .cg = "bx" BX_SLASH "btn", .text = BX_TEXT3, .size = 16, .pos = 5, .line_space = 6 };
	struct ex_tree *kids;
	struct ex *e = lb_activity("ButtonTextOld", 1, &kids);
	kids->children[0] = bx_node("Init", 100, 100, &init);
	int act = bx_load("ButtonTextOld", e);
	int no = bx_no(act, "Init");
	heap_unref(act);
	if (!no)
		return;
	static const struct bx_btn bare = { .cg = "bx" BX_SLASH "btn" };
	bx_expect(no, &bare, BX_N, BX_ON, BX_DOWN, "BX7 an engine before v14");
	printf("BX7 checked\n");
}

/* --- BX8 --- */
static void bx_sjis_key(void)
{
	bx_setup(true);
	static const struct bx_btn init = { .cg = "bx" BX_SLASH "btn", .text = BX_TEXT3, .sjis_text_key = true, .size = 16, .pos = 5 };
	struct ex_tree *kids;
	struct ex *e = lb_activity("ButtonTextSjisKey", 1, &kids);
	kids->children[0] = bx_node("Init", 100, 100, &init);
	int act = bx_load("ButtonTextSjisKey", e);
	int no = bx_no(act, "Init");
	heap_unref(act);
	if (!no)
		return;
	static const struct bx_btn bare = { .cg = "bx" BX_SLASH "btn" };
	bx_expect(no, &bare, BX_N, BX_ON, BX_DOWN, "BX8 an SJIS 文本 key");
	printf("BX8 checked\n");
}

static void (*bx_case)(void);
static void bx_case_body(void)
{
	bx_case();
	bx_failures += bt_failures + cn_failures + cc_failures;
}

static int test_button_text(void)
{
	static const struct { const char *name; void (*fn)(void); } cases[] = {
		{ "BX1", bx_game }, { "BX2", bx_places }, { "BX3", bx_nothing }, { "BX4", bx_enable },
		{ "BX5", bx_keys }, { "BX6", bx_save }, { "BX7", bx_v13 }, { "BX8", bx_sjis_key },
	};
	const int n = sizeof(cases) / sizeof(cases[0]);
	int failed = 0, env = 0;
	for (int i = 0; i < n; i++) {
		bx_case = cases[i].fn;
		int r = cn_fork_case(cases[i].name, bx_case_body, &bx_failures);
		failed += r == 1;
		env += r == 2;
	}
	if (env) {
		printf("BUTTON-TEXT ENVIRONMENT FAILURE: %d of %d cases could not run, %d failed\n", env, n, failed);
		return 2;
	}
	printf("BUTTON-TEXT %s: %d of %d cases failed\n", failed ? "FAIL" : "PASS", failed, n);
	return failed ? 1 : 0;
}
