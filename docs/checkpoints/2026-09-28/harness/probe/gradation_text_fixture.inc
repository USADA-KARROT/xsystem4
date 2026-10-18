/* gradation-text: the pactex 構築部件 states that draw a text's alpha and fill
 * the rows with a gradation of colour (construction commands 24 and 9), as
 * the original engine builds them while the pactex loads (static
 * disassembly only):
 *   - the state loader runs the steps at once (0x5a64e0 -> 0x5a73c0 ->
 *     0x4fa1d0, table 0x4fb7d8: 9 -> 0x4fdbf0, 24 -> 0x4fd720); a step that
 *     fails skips the rest (0x4fb675);
 *   - the step parser (0x4f82a0) keeps 文字間隔 at +0x58, 行間隔 at +0x5c,
 *     the font at +0x60 (フォントタイプ +0x64, フォントサイズ +0x68,
 *     フォント色 +0x6c, フォント太さ +0x7c and フォント縁取り +0x80 as
 *     floats, フォント縁取り色 +0x84; both colours clamped to 0..255), 文本
 *     at +0x94, 色１ at +0x34, 色２ at +0x44 and 全體 at +0xc4; a missing
 *     key is 0 or "";
 *   - 24 runs the text loop of commands 7, 8 and 23 (see backlog-text) with
 *     the step's font and spacings at 先矩形 X,Y; each character's cell
 *     copies its alpha onto the surface and leaves the colours (0x5acea0 ->
 *     0x494270);
 *   - 9 fills row i of the rectangle's H rows (全體: the whole surface) as
 *     command 3 fills (0x5ac230: the RGB replaced and the alpha kept, the
 *     row normalized and clipped by 0x5abed0; a row with nothing left fails
 *     the step, 0x5abde0) in 色１ + i * (色２ - 色１) / H, each channel's
 *     quotient truncated (idiv, 0x4fdcb1..0x4fdccb); 色２'s alpha is not
 *     read; without a row (H <= 0) the step succeeds (0x4fdca4 -> 0x4fdd09).
 * The game has four such states, each 0 (create), 5 (alpha 0 over all), 24,
 * 9 (over all): a yellow-to-red text over a dark text part with an edge that
 * lies under it (the achievement banner's title, two level-up captions and
 * the game over caption). xsystem4 did not build them (the loader did not
 * take 9 and 24): only the dark text with its edge was seen.
 *
 * The expected pixels: the alpha from backlog-text's model of the text loop
 * (its pattern font, the cells drawn one character at a time) and, for some
 * pixels, from the pattern's own formula by hand; the colours from the
 * formula above written out here, several rows also by hand (where a
 * truncated quotient differs from a floored one).
 *
 * Cases (each in its own fork):
 *   GT1 the achievement banner's shape (331 x 58; four full-width characters
 *       of 40 at (4, 4), character spacing 1, line spacing 15; 9 from
 *       (255, 255, 64) to (255, 90, 90)): every pixel against the model;
 *       glyph pixels by hand, among them the column between two cells that
 *       no cell covers; rows 0, 29 and 57 by hand; the operations: 3 and one
 *       fill per row (this port's choice).
 *   GT2 the level-up (128 x 32, 25 at (5, 0), spacing -1) and game over
 *       (330 x 66, 51) shapes with half-width characters: every pixel; a
 *       column where a cell's first column replaces the last one of the
 *       cell before it; rows by hand.
 *   GT3 9 on its own over an opaque coloured base: a rectangle, one past the
 *       right edge, a negative width, H 0 and H -3 (nothing drawn, the step
 *       after it runs), 全體 2 (not the whole surface); the alphas of 色１
 *       and 色２ are not read and the base's alpha stays.
 *   GT4 24's font: an edge and a weight (cells 2e wider and higher, the edge
 *       in the alpha), a line break and 行間隔, a negative spacing; the font's
 *       colours clamped where they are kept; only フォントサイズ given (the
 *       other keys 0); 24 on an opaque coloured base changes the alpha alone.
 *   GT5 states that are not built and keep the previous behaviour (no
 *       texture, type 26, the first step's canvas): 9 with a row above or
 *       below the surface or nothing left in X, a colour outside 0..255, no
 *       色２, no 全體; 24 without a 文本 or with it under an SJIS spelling, a
 *       size of 0 or none, a float size, a negative edge, a pen left of the
 *       surface, the bytes 0x81 0x5C in the text. They pass before the fix
 *       as well (guards).
 *   GT6 PE_Save / PE_Load: XPE version 11, the same pixels and operations.
 *   GT7 off the CN glyph grid (the SJIS string rule) the banner's shape is
 *       not built (guard) and a state of 0, 5 and 9 is (9 does not depend on
 *       the encoding); before v14 neither is built (guard).
 * Only functions that exist before the fix are called: the file builds
 * before it.
 */
static int gt_failures;
#define GT_CHECK(c, ...) do { if (!(c)) { printf("  FAIL: "); printf(__VA_ARGS__); printf("\n"); gt_failures++; } } while (0)

#define GT_COLOR2 "\xc9\xab\xa3\xb2"	/* 色２ */
/* Four GBK characters: U+4E00, U+4E8C, U+4E09, U+56DB. */
#define GT_TEXT4 "\xd2\xbb\xb6\xfe\xc8\xfd\xcb\xc4"
static const uint32_t gt_text4_codes[4] = { 0x4e00, 0x4e8c, 0x4e09, 0x56db };

enum {
	GT_NO_TEXT = 1 << 0,	/* no 文本 */
	GT_SJIS_TEXT = 1 << 1,	/* 文本 under an SJIS spelling (a guess; no SJIS pactex was read) */
	GT_NO_SIZE = 1 << 2,	/* no フォントサイズ */
	GT_FLOAT_SIZE = 1 << 3,	/* フォントサイズ is a float */
	GT_SIZE_ONLY = 1 << 4,	/* of the font's keys and the spacings, only フォントサイズ */
	GT_NO_COLOR2 = 1 << 5,	/* no 色２ */
	GT_NO_FULL = 1 << 6,	/* no 全體 */
};

struct gt_step {
	int cmd;
	int dest[6];		/* 先矩形: X, Y, X2, Y2, W, H */
	int c1[4], c2[4];	/* 色１, 色２ */
	int full;		/* 全體 */
	const char *text;	/* 文本 */
	int size, type;		/* フォントサイズ, フォントタイプ */
	int rgb[3], edge_rgb[3];
	float bold, edge;	/* written as ints when they are whole, as the game's are */
	int char_space, line_space;
	int flags;
};
#define GT_CREATE(w, h) { .cmd = 0, .dest = { 0, 0, 0, 0, w, h } }
#define GT_CLEAR { .cmd = 5, .full = 1 }
#define GT_BASE(r, g, b, a) { .cmd = 6, .c1 = { r, g, b, a }, .full = 1 }
#define GT_TEXT(x, y, t, sz, cs, ls) { .cmd = 24, .dest = { x, y }, .text = t, .size = sz, .char_space = cs, .line_space = ls }
#define GT_GRAD_ALL(r1, g1, b1, r2, g2, b2) { .cmd = 9, .c1 = { r1, g1, b1, 0 }, .c2 = { r2, g2, b2, 0 }, .full = 1 }
#define GT_N(steps) (int)(sizeof(steps) / sizeof((steps)[0])), steps

static struct ex_value gt_number(float v)
{
	if (v == (float)(int)v)
		return (struct ex_value){ .type = EX_INT, .i = (int)v };
	return (struct ex_value){ .type = EX_FLOAT, .f = v };
}

/* A 構築部件 whose normal state's 手順リスト holds the steps, each with every
 * key the game's steps have (unless a flag leaves one out). */
static struct ex_tree gt_parts(const char *name, int x, int y, int n, const struct gt_step *steps)
{
	struct lb_place pl = { x, y, 1 };
	struct ex_tree info = bu_branch(BU_INFO, 2);
	info.children[0] = bu_sleaf(BU_TYPE, BU_LOW);
	info.children[1] = bu_branch(BU_NORMAL, 3);
	struct ex_tree *st = &info.children[1];
	st->children[0] = bu_sleaf(BU_TYPE, BR_CONS);
	st->children[1] = lb_ilist(CN_AREA, 4, (int[]){ 0, 0, 0, 0 });
	st->children[2] = bu_branch(BR_STEPS, n);
	for (int i = 0; i < n; i++) {
		const struct gt_step *s = &steps[i];
		char key[32];
		snprintf(key, sizeof key, "Step%d", i + 1);
		struct ex_tree t = bu_branch(key, 16);
		unsigned k = 0;
		t.children[k++] = bu_ileaf(BR_COMMAND, s->cmd);
		t.children[k++] = lb_ilist(LB_DEST, 6, s->dest);
		t.children[k++] = lb_ilist(CN_COLOR1, 4, s->c1);
		if (!(s->flags & GT_NO_COLOR2))
			t.children[k++] = lb_ilist(GT_COLOR2, 4, s->c2);
		if (!(s->flags & GT_NO_FULL))
			t.children[k++] = bu_ileaf(CN_FULL, s->full);
		if (!(s->flags & GT_NO_TEXT))
			t.children[k++] = bu_sleaf((s->flags & GT_SJIS_TEXT) ? BX_K_TEXT_SJIS : BX_K_TEXT, s->text ? s->text : "");
		if (s->flags & GT_FLOAT_SIZE)
			t.children[k++] = bu_leaf(BX_K_FONT_SIZE, (struct ex_value){ .type = EX_FLOAT, .f = s->size });
		else if (!(s->flags & GT_NO_SIZE))
			t.children[k++] = bu_ileaf(BX_K_FONT_SIZE, s->size);
		if (!(s->flags & GT_SIZE_ONLY)) {
			t.children[k++] = bu_ileaf(BX_K_FONT_TYPE, s->type);
			t.children[k++] = lb_ilist(BX_K_FONT_COLOR, 3, s->rgb);
			t.children[k++] = bu_leaf(BX_K_FONT_BOLD, gt_number(s->bold));
			t.children[k++] = bu_leaf(BX_K_FONT_EDGE, gt_number(s->edge));
			t.children[k++] = lb_ilist(BX_K_FONT_EDGE_COLOR, 3, s->edge_rgb);
			t.children[k++] = bu_ileaf(BX_K_CHAR_SPACE, s->char_space);
			t.children[k++] = bu_ileaf(BX_K_LINE_SPACE, s->line_space);
		}
		t.children[k++] = bu_sleaf(BR_CG_NAME, "");
		assert(k <= 16);
		t.nr_children = k;
		st->children[2].children[i] = t;
	}
	return lb_node(name, &pl, info, 0);
}

static int gt_load(const char *name, struct ex *e)
{
	int act = cn_load(name, e);
	gt_failures += cn_failures;
	cn_failures = 0;
	return act;
}

/* --- the model --- */

/* c1 + i * (c2 - c1) / n with the quotient truncated towards 0, written out. */
static int gt_grad(int c1, int c2, int i, int n)
{
	const int v = i * (c2 - c1);
	return c1 + (v >= 0 ? v / n : -((-v) / n));
}

/* Command 9 on px: the rows of the rectangle (the whole surface with 全體 1)
 * in the gradation, the alpha left. Only rectangles whose rows all lie in the
 * surface are modelled (the others are not built). */
static void gt_model_grad(uint8_t *px, int w, int h, const struct gt_step *s)
{
	int x = s->dest[0], y = s->dest[1], rw = s->dest[4], rh = s->dest[5];
	if (s->full == 1) {
		x = 0; y = 0; rw = w; rh = h;
	}
	if (rw < 0) {
		x += rw;
		rw = -rw;
	}
	for (int i = 0; i < rh; i++) {
		for (int c = x; c < x + rw; c++) {
			if (c < 0 || c >= w || y + i < 0 || y + i >= h)
				continue;
			uint8_t *p = px + ((y + i) * w + c) * 4;
			for (int k = 0; k < 3; k++)
				p[k] = gt_grad(s->c1[k], s->c2[k], i, rh);
		}
	}
}

/* What the steps leave on a w x h surface (to be freed): 0 makes it opaque
 * black, 5 and 6 (only over all here) replace the alpha or everything, 24 the
 * alpha under its cells, 9 the colours of its rows. */
static uint8_t *gt_model(int w, int h, int n, const struct gt_step *steps)
{
	uint8_t *px = malloc((size_t)w * h * 4);
	bt_fill(px, w, h, 0, 0, 0, 255);
	for (int i = 0; i < n; i++) {
		const struct gt_step *s = &steps[i];
		switch (s->cmd) {
		case 5:
			assert(s->full == 1);
			for (int k = 0; k < w * h; k++)
				px[k * 4 + 3] = s->c1[3];
			break;
		case 6:
			assert(s->full == 1);
			bt_fill(px, w, h, s->c1[0], s->c1[1], s->c1[2], s->c1[3]);
			break;
		case 24: {
			const bool font = !(s->flags & GT_SIZE_ONLY);
			const struct bt_style st = { .size = s->size, .rgb = { s->rgb[0], s->rgb[1], s->rgb[2] },
				.bold = font ? s->bold : 0, .edge = font ? s->edge : 0,
				.edge_rgb = { s->edge_rgb[0], s->edge_rgb[1], s->edge_rgb[2] } };
			bt_model_grid(px, w, h, true, 24, s->dest[0], s->dest[1], s->text, &st,
				font ? s->char_space : 0, font ? s->line_space : 0);
			break;
		}
		case 9:
			gt_model_grad(px, w, h, s);
			break;
		}
	}
	return px;
}

/* The pattern font's glyph alpha (backlog-text) at a glyph's (col, row). */
static int gt_glyph(uint32_t code, int col, int row) { return bt_glyph_alpha(code, col, row); }

static void gt_compare(uint8_t *got, int w, int h, const uint8_t *model, const char *what)
{
	int bad = 0;
	for (int i = 0; got && i < w * h; i++) {
		if (!memcmp(got + i * 4, model + i * 4, 4))
			continue;
		if (bad < 3)
			GT_CHECK(0, "%s (%d,%d) = (%d,%d,%d,%d), want (%d,%d,%d,%d)", what, i % w, i / w, got[i * 4],
				 got[i * 4 + 1], got[i * 4 + 2], got[i * 4 + 3], model[i * 4], model[i * 4 + 1],
				 model[i * 4 + 2], model[i * 4 + 3]);
		bad++;
	}
	if (bad > 3)
		GT_CHECK(0, "%s: %d pixels differ in all", what, bad);
}

static void gt_px(const uint8_t *px, int w, int x, int y, int r, int g, int b, int a, const char *what)
{
	const uint8_t *p = px ? px + (y * w + x) * 4 : NULL;
	GT_CHECK(p && p[0] == r && p[1] == g && p[2] == b && p[3] == a, "%s (%d,%d) = (%d,%d,%d,%d), want (%d,%d,%d,%d)",
		 what, x, y, p ? p[0] : -1, p ? p[1] : -1, p ? p[2] : -1, p ? p[3] : -1, r, g, b, a);
}

/* The built state against the model; returns its pixels (to be freed). */
static uint8_t *gt_expect(int no, int w, int h, int n, const struct gt_step *steps, const char *what)
{
	uint8_t *got = cn_pixels(no, w, h, what);
	gt_failures += cn_failures;
	cn_failures = 0;
	uint8_t *model = gt_model(w, h, n, steps);
	gt_compare(got, w, h, model, what);
	free(model);
	return got;
}

static int gt_ops(int no)
{
	struct parts *p = parts_try_get(no);
	return p ? cn_nr_ops(p) : -1;
}

static int gt_no(int act, const char *name)
{
	int n = bu_number(act, name);
	GT_CHECK(n > 0 && parts_try_get(n), "%s has no parts", name);
	return n > 0 && parts_try_get(n) ? n : 0;
}

/* --- the game's shapes --- */
static const struct gt_step gt_banner[] = {
	GT_CREATE(331, 58), GT_CLEAR, GT_TEXT(4, 4, GT_TEXT4, 40, 1, 15), GT_GRAD_ALL(255, 255, 64, 255, 90, 90),
};
static const struct gt_step gt_levelup[] = {
	GT_CREATE(128, 32), GT_CLEAR, GT_TEXT(5, 0, "AbCdEfGh!", 25, -1, 9), GT_GRAD_ALL(255, 255, 64, 255, 64, 64),
};
static const struct gt_step gt_gameover[] = {
	GT_CREATE(330, 66), GT_CLEAR, GT_TEXT(5, 0, "ABCD EFGH", 51, -1, 19), GT_GRAD_ALL(255, 255, 128, 255, 0, 0),
};

/* --- GT1 --- */
static void gt_banner_case(void)
{
	bt_gl_setup(true);
	struct ex_tree *kids;
	struct ex *e = lb_activity("GradationTextBanner", 1, &kids);
	kids->children[0] = gt_parts("CaptionGrd", 475, 302, GT_N(gt_banner));
	int act = gt_load("GradationTextBanner", e);
	int no = gt_no(act, "CaptionGrd");
	heap_unref(act);
	if (!no)
		return;
	uint8_t *px = gt_expect(no, 331, 58, GT_N(gt_banner), "GT1 the banner's shape");
	if (px) {
		/* Rows by hand: 255 + 29 * -165 / 58 = 255 - 82 (not 83), 64 + 29 * 26
		 * / 58 = 77; row 57: 255 - 9405 / 58 = 255 - 162 = 93 (a floored
		 * quotient gives 92), 64 + 1482 / 58 = 89. (0, y) is no glyph's. */
		gt_px(px, 331, 0, 0, 255, 255, 64, 0, "GT1 row 0");
		gt_px(px, 331, 0, 29, 255, 173, 77, 0, "GT1 row 29");
		gt_px(px, 331, 0, 57, 255, 93, 89, 0, "GT1 row 57");
		gt_px(px, 331, 330, 57, 255, 93, 89, 0, "GT1 row 57, last column");
		/* The glyphs (e 0): the first at (4, 4), the next at 4 + 40 + 1 =
		 * 45; column 44 lies between two cells and keeps the alpha 0. Rows
		 * 4 + 7 = 11 (gradation 255 + 11 * -165 / 58 = 224, 64 + 286 / 58 =
		 * 68) and 4 + 30 = 34 (255 - 96 = 159, 64 + 15 = 79). */
		gt_px(px, 331, 4 + 3, 11, 255, 224, 68, gt_glyph(gt_text4_codes[0], 3, 7), "GT1 the first glyph");
		gt_px(px, 331, 45 + 1, 11, 255, 224, 68, gt_glyph(gt_text4_codes[1], 1, 7), "GT1 the second glyph");
		gt_px(px, 331, 44, 11, 255, 224, 68, 0, "GT1 between two cells");
		gt_px(px, 331, 4 + 3 * 41 + 39, 34, 255, 159, 79, gt_glyph(gt_text4_codes[3], 39, 30),
		      "GT1 the last glyph's last column");
		gt_px(px, 331, 4 + 4 * 41 - 1, 34, 255, 159, 79, 0, "GT1 right of the text");
		gt_px(px, 331, 10, 44, 255, gt_grad(255, 90, 44, 58), gt_grad(64, 90, 44, 58), 0, "GT1 below the cells");
		int lit = 0;
		for (int i = 0; i < 331 * 58; i++)
			lit += px[i * 4 + 3] > 0;
		GT_CHECK(lit > 4 * 40 * 40 / 2, "GT1: %d pixels with an alpha, want the glyphs'", lit);
		free(px);
	}
	/* One operation for 0, 5 and 24 each and one fill for each of the 58 rows. */
	GT_CHECK(gt_ops(no) == 3 + 58, "GT1: %d operations, want 61", gt_ops(no));
	printf("GT1 checked\n");
}

/* --- GT2 --- */
static void gt_captions(void)
{
	bt_gl_setup(true);
	struct ex_tree *kids;
	struct ex *e = lb_activity("GradationTextCaptions", 2, &kids);
	kids->children[0] = gt_parts("LevelUp", 10, 10, GT_N(gt_levelup));
	kids->children[1] = gt_parts("GameOver", 10, 100, GT_N(gt_gameover));
	int act = gt_load("GradationTextCaptions", e);
	int lu = gt_no(act, "LevelUp"), go = gt_no(act, "GameOver");
	heap_unref(act);
	if (!lu || !go)
		return;
	uint8_t *px = gt_expect(lu, 128, 32, GT_N(gt_levelup), "GT2 the level-up shape");
	if (px) {
		/* Half-width cells of (25 + 1) / 2 = 13, a pen moving by 12: column
		 * 5 + 12 = 17 is the second cell's first, which replaces the first
		 * cell's last. Row 31: 255 + 31 * -191 / 32 = 255 - 185 = 70 (a
		 * floored quotient gives 69); row 16: 255 - 95 = 160. */
		gt_px(px, 128, 17, 3, 255, gt_grad(255, 64, 3, 32), 64, gt_glyph('b', 0, 3), "GT2 the overlap");
		gt_px(px, 128, 16, 3, 255, gt_grad(255, 64, 3, 32), 64, gt_glyph('A', 11, 3), "GT2 the first cell");
		gt_px(px, 128, 0, 31, 255, 70, 64, 0, "GT2 level-up row 31");
		gt_px(px, 128, 0, 16, 255, 160, 64, 0, "GT2 level-up row 16");
		free(px);
	}
	px = gt_expect(go, 330, 66, GT_N(gt_gameover), "GT2 the game over shape");
	if (px) {
		/* Row 65: 255 + 65 * -255 / 66 = 255 - 251 = 4, 128 + 65 * -128 / 66
		 * = 128 - 126 = 2; row 33: 255 - 127 = 128, 128 - 64 = 64. */
		gt_px(px, 330, 329, 65, 255, 4, 2, 0, "GT2 game over row 65");
		gt_px(px, 330, 329, 33, 255, 128, 64, 0, "GT2 game over row 33");
		free(px);
	}
	GT_CHECK(gt_ops(lu) == 3 + 32 && gt_ops(go) == 3 + 66, "GT2: %d and %d operations, want 35 and 69", gt_ops(lu),
		 gt_ops(go));
	printf("GT2 checked\n");
}

/* --- GT3 --- */
#define GT_W 40
#define GT_H 20
static const struct gt_step gt_rect[] = {
	GT_CREATE(GT_W, GT_H), GT_BASE(10, 20, 30, 77),
	{ .cmd = 9, .dest = { 5, 3, 0, 0, 20, 10 }, .c1 = { 0, 100, 200, 255 }, .c2 = { 200, 0, 50, 255 } },
};
static const struct gt_step gt_right[] = {
	GT_CREATE(GT_W, GT_H), GT_BASE(10, 20, 30, 77),
	{ .cmd = 9, .dest = { 30, 0, 0, 0, 50, 4 }, .c1 = { 255, 0, 0, 0 }, .c2 = { 0, 0, 255, 0 } },
};
static const struct gt_step gt_negw[] = {
	GT_CREATE(GT_W, GT_H), GT_BASE(10, 20, 30, 77),
	{ .cmd = 9, .dest = { 20, 15, 0, 0, -10, 5 }, .c1 = { 0, 0, 0, 0 }, .c2 = { 250, 250, 250, 0 } },
};
static const struct gt_step gt_h0[] = {
	GT_CREATE(GT_W, GT_H), GT_BASE(10, 20, 30, 77),
	{ .cmd = 9, .dest = { 5, 5, 0, 0, 10, 0 }, .c1 = { 200, 200, 200, 0 }, .c2 = { 0, 0, 0, 0 } },
	{ .cmd = 5, .c1 = { 0, 0, 0, 200 }, .full = 1 },
};
static const struct gt_step gt_hneg[] = {
	GT_CREATE(GT_W, GT_H), GT_BASE(10, 20, 30, 77),
	{ .cmd = 9, .dest = { 5, 5, 0, 0, 10, -3 }, .c1 = { 200, 200, 200, 0 }, .c2 = { 0, 0, 0, 0 } },
	{ .cmd = 5, .c1 = { 0, 0, 0, 200 }, .full = 1 },
};
static const struct gt_step gt_full2[] = {
	GT_CREATE(GT_W, GT_H), GT_BASE(10, 20, 30, 77),
	{ .cmd = 9, .dest = { 0, 0, 0, 0, 10, 2 }, .c1 = { 90, 90, 90, 0 }, .c2 = { 30, 30, 30, 0 }, .full = 2 },
};

static void gt_rows(void)
{
	bt_gl_setup(true);
	static const struct { const char *name; int n; const struct gt_step *steps; } st[] = {
		{ "Rect", GT_N(gt_rect) }, { "Right", GT_N(gt_right) }, { "NegW", GT_N(gt_negw) },
		{ "H0", GT_N(gt_h0) }, { "HNeg", GT_N(gt_hneg) }, { "Full2", GT_N(gt_full2) },
	};
	enum { NR = sizeof(st) / sizeof(st[0]) };
	struct ex_tree *kids;
	struct ex *e = lb_activity("GradationTextRows", NR, &kids);
	for (int i = 0; i < NR; i++)
		kids->children[i] = gt_parts(st[i].name, 10 + 50 * i, 10, st[i].n, st[i].steps);
	int act = gt_load("GradationTextRows", e);
	int no[NR];
	bool all = true;
	for (int i = 0; i < NR; i++)
		all = (no[i] = gt_no(act, st[i].name)) && all;
	heap_unref(act);
	if (!all)
		return;
	uint8_t *px[NR];
	for (int i = 0; i < NR; i++) {
		char what[64];
		snprintf(what, sizeof what, "GT3 %s", st[i].name);
		px[i] = gt_expect(no[i], GT_W, GT_H, st[i].n, st[i].steps, what);
	}
	/* By hand. Rect: rows 3..12, columns 5..24, the base's alpha; row 12 is
	 * i 9 of 10: (0 + 180, 100 - 90, 200 - 135). */
	gt_px(px[0], GT_W, 5, 3, 0, 100, 200, 77, "GT3 Rect's first pixel");
	gt_px(px[0], GT_W, 24, 12, 180, 10, 65, 77, "GT3 Rect's last pixel");
	gt_px(px[0], GT_W, 25, 12, 10, 20, 30, 77, "GT3 right of Rect");
	gt_px(px[0], GT_W, 5, 13, 10, 20, 30, 77, "GT3 below Rect");
	gt_px(px[0], GT_W, 5, 2, 10, 20, 30, 77, "GT3 above Rect");
	/* Right: columns 30..39; row 3 is (255 - 191, 0, 191). */
	gt_px(px[1], GT_W, 39, 3, 64, 0, 191, 77, "GT3 Right at the edge");
	gt_px(px[1], GT_W, 29, 3, 10, 20, 30, 77, "GT3 left of Right");
	/* NegW: columns 10..19, rows 15..19; row 19 is i 4: 4 * 250 / 5 = 200. */
	gt_px(px[2], GT_W, 10, 15, 0, 0, 0, 77, "GT3 NegW's first pixel");
	gt_px(px[2], GT_W, 19, 19, 200, 200, 200, 77, "GT3 NegW's last pixel");
	gt_px(px[2], GT_W, 9, 15, 10, 20, 30, 77, "GT3 left of NegW");
	gt_px(px[2], GT_W, 20, 15, 10, 20, 30, 77, "GT3 right of NegW");
	/* H 0 and H -3 draw nothing and the step after them runs. */
	gt_px(px[3], GT_W, 7, 5, 10, 20, 30, 200, "GT3 H 0");
	gt_px(px[4], GT_W, 7, 5, 10, 20, 30, 200, "GT3 H -3");
	/* 全體 2: the rectangle 10 x 2 only. */
	gt_px(px[5], GT_W, 9, 1, 60, 60, 60, 77, "GT3 Full2's last pixel");
	gt_px(px[5], GT_W, 10, 1, 10, 20, 30, 77, "GT3 right of Full2");
	gt_px(px[5], GT_W, 0, 2, 10, 20, 30, 77, "GT3 below Full2");
	for (int i = 0; i < NR; i++)
		free(px[i]);
	printf("GT3 checked\n");
}

/* --- GT4 --- */
static const struct gt_step gt_font[] = {
	GT_CREATE(120, 60), GT_CLEAR,
	{ .cmd = 24, .dest = { 2, 1 }, .text = "\xd2\xbb" "A\n" "\xb6\xfe", .size = 16, .rgb = { 300, -5, 7 },
	  .edge_rgb = { 9, 400, -1 }, .bold = 0.5f, .edge = 2, .char_space = -1, .line_space = 3 },
	GT_GRAD_ALL(10, 20, 30, 40, 50, 60),
};
static const struct gt_step gt_sizeonly[] = {
	GT_CREATE(60, 20), GT_CLEAR,
	{ .cmd = 24, .dest = { 0, 0 }, .text = "Ab", .size = 12, .char_space = 5, .line_space = 5, .flags = GT_SIZE_ONLY },
	GT_GRAD_ALL(0, 0, 0, 100, 100, 100),
};

/* 24 on an opaque coloured base: only the alpha changes (7 and 23 would blend
 * the colours, 8 copy them; on the game's shapes, a base of alpha 0 whose
 * colours 9 then replaces, 8 gives the same pixels as 24, and 23 differs only
 * where two cells overlap). */
static const struct gt_step gt_onbase[] = {
	GT_CREATE(60, 20), GT_BASE(10, 20, 30, 200),
	{ .cmd = 24, .dest = { 1, 1 }, .text = "Ab", .size = 12, .rgb = { 250, 0, 0 }, .edge_rgb = { 0, 250, 0 } },
};

static void gt_font_case(void)
{
	bt_gl_setup(true);
	struct ex_tree *kids;
	struct ex *e = lb_activity("GradationTextFont", 3, &kids);
	kids->children[0] = gt_parts("Font", 10, 10, GT_N(gt_font));
	kids->children[1] = gt_parts("SizeOnly", 10, 100, GT_N(gt_sizeonly));
	kids->children[2] = gt_parts("OnBase", 10, 150, GT_N(gt_onbase));
	int act = gt_load("GradationTextFont", e);
	int no = gt_no(act, "Font"), so = gt_no(act, "SizeOnly"), ob = gt_no(act, "OnBase");
	heap_unref(act);
	if (!no || !so || !ob)
		return;
	uint8_t *px = gt_expect(no, 120, 60, GT_N(gt_font), "GT4 an edge, a weight and two lines");
	if (px) {
		/* e = max(ceil(0.5), 2) = 2: the cells are 16 + 4 and 8 + 4 wide and
		 * 16 + 6 high, each glyph at (e, e) in its cell. The first glyph is
		 * at (2 + 2, 1 + 2) = (4, 3), columns 4..19; the pen moves by 20 - 1,
		 * so "A" is at 2 + 19 + 2 = 23 (without e it would be at 17); the
		 * second line is 1 + (16 + 4) + 3 = 24 down, its glyph from row 26.
		 * The pattern glyph fills its box but for a lattice of 0s, which the
		 * edge covers: (6, 3) is the first glyph's (2, 0), 0 without an edge.
		 * The colours: rows 3, 10, 25 and 26 of 60 from (10, 20, 30) to
		 * (40, 50, 60). */
		GT_CHECK(gt_glyph(gt_text4_codes[0], 2, 0) == 0, "GT4: the pattern's (2, 0) is not 0");
		gt_px(px, 120, 6, 3, 11, 21, 31, 255, "GT4 a lattice point under the edge");
		gt_px(px, 120, 19, 10, 15, 25, 35, 255, "GT4 the first glyph's last column");
		gt_px(px, 120, 20, 10, 15, 25, 35, 0, "GT4 right of the first glyph");
		gt_px(px, 120, 22, 10, 15, 25, 35, 0, "GT4 left of A");
		gt_px(px, 120, 23, 10, 15, 25, 35, 255, "GT4 A's first column");
		gt_px(px, 120, 4, 25, 22, 32, 42, 0, "GT4 above the second line's glyph");
		gt_px(px, 120, 4, 26, 23, 33, 43, 255, "GT4 the second line's glyph");
		free(px);
	}
	/* The font as it was kept: the colours clamped, the floats as they are. */
	struct parts *p = parts_try_get(no);
	struct parts_cp_op *op, *text = NULL;
	TAILQ_FOREACH(op, &p->states[0].cproc.ops, entry)
		if (op->type == PARTS_CP_ONLY_ALPHA_TEXT)
			text = op;
	GT_CHECK(text, "GT4: no text operation");
	if (text) {
		const struct text_style *s = &text->text.style;
		GT_CHECK(s->color.r == 255 && s->color.g == 0 && s->color.b == 7 && s->edge_color.r == 9
			 && s->edge_color.g == 255 && s->edge_color.b == 0,
			 "GT4: the colours are (%d,%d,%d) and (%d,%d,%d), want (255,0,7) and (9,255,0)", s->color.r,
			 s->color.g, s->color.b, s->edge_color.r, s->edge_color.g, s->edge_color.b);
		GT_CHECK(s->size == 16 && s->bold_width == 0.5f && s->edge_left == 2 && s->font_spacing == -1
			 && text->text.line_space == 3 && text->text.x == 2 && text->text.y == 1,
			 "GT4: size %g, weight %g, edge %g, spacing %g, line spacing %d at (%d,%d)", s->size, s->bold_width,
			 s->edge_left, s->font_spacing, text->text.line_space, text->text.x, text->text.y);
	}
	/* Only フォントサイズ: the spacings and the rest are 0. */
	px = gt_expect(so, 60, 20, GT_N(gt_sizeonly), "GT4 only フォントサイズ");
	if (px) {
		/* "A" is 6 wide (12 + 1) / 2: "b" at 6, not 6 + 5. */
		gt_px(px, 60, 6, 2, gt_grad(0, 100, 2, 20), gt_grad(0, 100, 2, 20), gt_grad(0, 100, 2, 20),
		      gt_glyph('b', 0, 2), "GT4 the second cell");
		free(px);
	}
	px = gt_expect(ob, 60, 20, GT_N(gt_onbase), "GT4 on an opaque base");
	if (px) {
		/* The base's colours everywhere; the alpha 200 outside the cells,
		 * the glyph's inside them ("A" at (1, 1), 6 x 12; "b" at (7, 1)). */
		gt_px(px, 60, 1 + 3, 1 + 4, 10, 20, 30, gt_glyph('A', 3, 4), "GT4 a glyph on the base");
		gt_px(px, 60, 7 + 5, 1 + 11, 10, 20, 30, gt_glyph('b', 5, 11), "GT4 the second cell on the base");
		gt_px(px, 60, 13, 5, 10, 20, 30, 200, "GT4 right of the cells");
		gt_px(px, 60, 4, 13, 10, 20, 30, 200, "GT4 below the cells");
		gt_px(px, 60, 0, 0, 10, 20, 30, 200, "GT4 above the cells");
		free(px);
	}
	printf("GT4 checked\n");
}

/* --- GT5 --- */
#define GT_SMALL GT_CREATE(GT_W, GT_H), GT_CLEAR
static const struct gt_step gt_u_above[] = { GT_SMALL, { .cmd = 9, .dest = { 0, -1, 0, 0, 10, 5 } } };
static const struct gt_step gt_u_below[] = { GT_SMALL, { .cmd = 9, .dest = { 0, 18, 0, 0, 10, 5 } } };
static const struct gt_step gt_u_xout[] = { GT_SMALL, { .cmd = 9, .dest = { GT_W, 0, 0, 0, 5, 5 } } };
static const struct gt_step gt_u_c256[] = { GT_SMALL, { .cmd = 9, .c1 = { 256, 0, 0, 0 }, .full = 1 } };
static const struct gt_step gt_u_cneg[] = { GT_SMALL, { .cmd = 9, .c2 = { 0, -1, 0, 0 }, .full = 1 } };
static const struct gt_step gt_u_noc2[] = { GT_SMALL, { .cmd = 9, .full = 1, .flags = GT_NO_COLOR2 } };
static const struct gt_step gt_u_nofull[] = { GT_SMALL, { .cmd = 9, .dest = { 0, 0, 0, 0, 5, 5 }, .flags = GT_NO_FULL } };
static const struct gt_step gt_u_notext[] = { GT_SMALL, { .cmd = 24, .size = 12, .flags = GT_NO_TEXT } };
static const struct gt_step gt_u_sjistext[] = { GT_SMALL, { .cmd = 24, .text = "Ab", .size = 12, .flags = GT_SJIS_TEXT } };
static const struct gt_step gt_u_size0[] = { GT_SMALL, { .cmd = 24, .text = "Ab", .size = 0 } };
static const struct gt_step gt_u_nosize[] = { GT_SMALL, { .cmd = 24, .text = "Ab", .flags = GT_NO_SIZE } };
static const struct gt_step gt_u_fsize[] = { GT_SMALL, { .cmd = 24, .text = "Ab", .size = 12, .flags = GT_FLOAT_SIZE } };
static const struct gt_step gt_u_negedge[] = { GT_SMALL, { .cmd = 24, .text = "Ab", .size = 12, .edge = -1 } };
static const struct gt_step gt_u_negx[] = { GT_SMALL, { .cmd = 24, .dest = { -1, 0 }, .text = "Ab", .size = 12 } };
static const struct gt_step gt_u_dash[] = { GT_SMALL, { .cmd = 24, .text = "A\x81\x5c", .size = 12 } };

static void gt_unbuilt(void)
{
	bt_gl_setup(true);
	static const struct { const char *name; int n; const struct gt_step *steps; } st[] = {
		{ "RowAbove", GT_N(gt_u_above) }, { "RowBelow", GT_N(gt_u_below) }, { "XOut", GT_N(gt_u_xout) },
		{ "Colour256", GT_N(gt_u_c256) }, { "ColourNeg", GT_N(gt_u_cneg) }, { "NoColour2", GT_N(gt_u_noc2) },
		{ "NoFull", GT_N(gt_u_nofull) }, { "NoText", GT_N(gt_u_notext) }, { "SjisText", GT_N(gt_u_sjistext) },
		{ "Size0", GT_N(gt_u_size0) }, { "NoSize", GT_N(gt_u_nosize) }, { "FloatSize", GT_N(gt_u_fsize) },
		{ "NegEdge", GT_N(gt_u_negedge) }, { "NegX", GT_N(gt_u_negx) }, { "Dash", GT_N(gt_u_dash) },
	};
	enum { NR = sizeof(st) / sizeof(st[0]) };
	struct ex_tree *kids;
	struct ex *e = lb_activity("GradationTextUnbuilt", NR, &kids);
	for (int i = 0; i < NR; i++)
		kids->children[i] = gt_parts(st[i].name, 10 + 45 * (i % 8), 10 + 30 * (i / 8), st[i].n, st[i].steps);
	int act = gt_load("GradationTextUnbuilt", e);
	for (int i = 0; i < NR; i++) {
		int no = gt_no(act, st[i].name);
		char what[64];
		snprintf(what, sizeof what, "GT5 %s", st[i].name);
		if (no)
			cn_expect_unbuilt(no, GT_W, GT_H, what);
	}
	heap_unref(act);
	gt_failures += cn_failures;
	cn_failures = 0;
	printf("GT5 checked\n");
}

/* --- GT6 --- */
static void gt_save(void)
{
	bt_gl_setup(true);
	struct ex_tree *kids;
	struct ex *e = lb_activity("GradationTextSave", 1, &kids);
	kids->children[0] = gt_parts("CaptionGrd", 475, 302, GT_N(gt_banner));
	int act = gt_load("GradationTextSave", e);
	int no = gt_no(act, "CaptionGrd");
	heap_unref(act);
	if (!no)
		return;
	free(gt_expect(no, 331, 58, GT_N(gt_banner), "GT6 before the save"));
	PE_UpdateComponent(0);
	struct page *saved = NULL;
	GT_CHECK(PE_Save(&saved) && saved, "GT6: PE_Save failed");
	if (!saved)
		return;
	GT_CHECK(ca_save_version(saved) == 11, "GT6: the save is XPE v%d, want 11", ca_save_version(saved));
	GT_CHECK(PE_Load(&saved), "GT6: PE_Load failed");
	PE_UpdateComponent(0);
	ca_free_buffer(saved);
	free(gt_expect(no, 331, 58, GT_N(gt_banner), "GT6 after PE_Load"));
	GT_CHECK(gt_ops(no) == 3 + 58, "GT6: %d operations after PE_Load, want 61", gt_ops(no));
	printf("GT6 checked\n");
}

/* --- GT7 --- */
static const struct gt_step gt_rows_only[] = { GT_CREATE(GT_W, GT_H), GT_CLEAR, GT_GRAD_ALL(255, 255, 64, 255, 90, 90) };

/* Not built: cn_expect_unbuilt's checks, the component type only from v14
 * (an older engine reports the types of its own). */
static void gt_expect_none(int no, int canvas_w, int canvas_h, const char *what)
{
	if (ain->version >= 14) {
		cn_expect_unbuilt(no, canvas_w, canvas_h, what);
		return;
	}
	struct parts *p = parts_try_get(no);
	const struct parts_state *st = p ? &p->states[0] : NULL;
	GT_CHECK(st && st->type != PARTS_CONSTRUCTION_PROCESS && !st->common.texture.handle && !st->common.w,
		 "%s: state type %d, texture %s, width %d; want it left unbuilt", what, st ? (int)st->type : -1,
		 st && st->common.texture.handle ? "present" : "missing", st ? st->common.w : -1);
}

static void gt_guards_load(const char *act_name, bool expect_rows, const char *what)
{
	struct ex_tree *kids;
	struct ex *e = lb_activity(act_name, 2, &kids);
	kids->children[0] = gt_parts("CaptionGrd", 475, 302, GT_N(gt_banner));
	kids->children[1] = gt_parts("RowsOnly", 10, 10, GT_N(gt_rows_only));
	int act = gt_load(act_name, e);
	int no = gt_no(act, "CaptionGrd"), ro = gt_no(act, "RowsOnly");
	heap_unref(act);
	char label[96];
	snprintf(label, sizeof label, "GT7 %s, the banner's shape", what);
	if (no)
		gt_expect_none(no, 331, 58, label);
	snprintf(label, sizeof label, "GT7 %s, 0, 5 and 9", what);
	if (ro && expect_rows)
		free(gt_expect(ro, GT_W, GT_H, GT_N(gt_rows_only), label));
	else if (ro)
		gt_expect_none(ro, GT_W, GT_H, label);
	gt_failures += cn_failures;
	cn_failures = 0;
}

static void gt_guards(void)
{
	bt_gl_setup(false);
	gt_guards_load("GradationTextOffGrid", true, "off the glyph grid");
	ain->version = 13;
	gt_guards_load("GradationTextV13", false, "before v14");
	printf("GT7 checked\n");
}

static void (*gt_case)(void);
static void gt_case_body(void)
{
	gt_case();
	gt_failures += bt_failures + cn_failures + cc_failures + ca_failures;
}

static int test_gradation_text(void)
{
	static const struct { const char *name; void (*fn)(void); } cases[] = {
		{ "GT1", gt_banner_case }, { "GT2", gt_captions }, { "GT3", gt_rows }, { "GT4", gt_font_case },
		{ "GT5", gt_unbuilt }, { "GT6", gt_save }, { "GT7", gt_guards },
	};
	const int n = sizeof(cases) / sizeof(cases[0]);
	int failed = 0, env = 0;
	for (int i = 0; i < n; i++) {
		gt_case = cases[i].fn;
		int r = cn_fork_case(cases[i].name, gt_case_body, &gt_failures);
		failed += r == 1;
		env += r == 2;
	}
	if (env) {
		printf("GRADATION-TEXT ENVIRONMENT FAILURE: %d of %d cases could not run, %d failed\n", env, n, failed);
		return 2;
	}
	printf("GRADATION-TEXT %s: %d of %d cases failed\n", failed ? "FAIL" : "PASS", failed, n);
	return failed ? 1 : 0;
}
