/* construction-shapes: the pactex 構築部件 states that draw discs, a rectangle's
 * alpha and a text (construction commands 100, 101, 106, 90 and 7), as the
 * original engine builds them while the pactex loads (static disassembly
 * only):
 *   - table 0x4fb7d8: 7 -> 0x4fc7e0, 90 -> 0x506ae0, 100 -> 0x507350, 101 ->
 *     0x5074b0, 106 -> 0x507d60; a step that fails skips the rest (0x4fb675);
 *     the step parser (0x4f82a0) keeps 先矩形 X,Y,W,H at +0x1c/+0x20/+0x2c/
 *     +0x30, 色１ at +0x34..+0x40, 半徑 at +0xc8/+0xcc, 丸め's items as
 *     "== 1" at +0xd4/+0xd5 and 旋轉角度 at +0xd8; a missing key is 0;
 *   - 100 (円塗りつぶし（ピクセル＆アルファ）) runs command 102's scan
 *     (0x4cfb40: the 2r + 1 positions from the centre X + 0.5 - r on both
 *     axes, truncated towards zero, so that -1 lands on 0; the coverage
 *     0x4e6530 counts the 8 x 8 subsamples of the cell from the position's
 *     offset to the centre within r, n * 255 / 64) with the writer 0x4cbb20
 *     -> 0x5b0b80: a covered pixel becomes (R, G, B, A * cov / 255), each
 *     clamped; a radius that is not positive succeeds with nothing drawn;
 *   - 101 (円塗りつぶし：矩形指定) is the same with D the larger of W and H:
 *     the centre X + 0.5 + D / 2, Y + 0.5 + D / 2, the radius D / 2
 *     (0x507506..0x5075a9); W or H not positive succeed with nothing drawn;
 *   - 106 (アルファブレンド) stages the same writes on a transparent surface
 *     and composes them (0x494420: c += ((s - c) * a) >> 8, the alpha the
 *     larger), see construction-cmds;
 *   - 90 (矩形塗りつぶし（アルファのみ）) builds the polygon (x0, y0), (x1,
 *     y0), (x1, y1), (x0, y1), x1 = X + W - 1, y1 = Y + H - 1 (0x4e97b0),
 *     each vertex at its pixel's centre (0x4e9400), or with 丸め (0x4e98a0 ->
 *     0x4ea2e0 -> 0x4e5cf0) every edge's two ends moved towards each other
 *     by d = min(4, (int)(length / 4)) and offset by +0.5 on an axis the edge
 *     goes forward along and -0.5 on the others (0 included), then scans it
 *     as command 95 does (0x5af970): a covered pixel's alpha becomes 色１.A *
 *     cov / 255;
 *   - 7 is the text loop of 24 (see gradation-text) whose cells blend the
 *     colours by their alpha and leave the surface's alpha (0x5aca90).
 * The game has eleven such states: the round bases of the battle result's and
 * the stock panel's portraits and of the mini map's nodes (101), the skill
 * slots of the member screen (100), the dungeon selector's new mark (100,
 * 106), the home screen's caution mark (106, 7), the button shadow (7, hidden
 * in the editor), the bracket of the party screen's vanguard (90 x 5 and 102
 * x 4) and the two detail screens' dimmed overlays with a rounded hole (90
 * with 丸め). Before the fix the loader did not take these commands and none
 * of the states was built.
 *
 * The expected pixels: the formulas above written out a second time here (an
 * integer disc model; the rectangle's coverage from construction-cmds' hand
 * values: the left and the one-past-right columns 127 a line, the others 255;
 * the top row 4 of 8 lines, the bottom row 5, the others 8); the rounded
 * rectangle's vertices worked out by hand from the disassembly and compared
 * with command 95 given them; several pixels by hand.
 *
 * Cases (each in its own fork):
 *   SH1 101: the battle result's portrait base (128 x 128, with the extra
 *       keys its pactex has), the mini map node (24 at (4, 4) on 32), a
 *       rectangle wider than high (the larger side): every pixel, hand
 *       pixels, the operations (one fill a row and 102's alpha).
 *   SH2 100: the skill slot (r 12 on 24), its disabled variant, discs cut
 *       by the top and left edges on an opaque base (the row and the column
 *       at -1 land on 0, also where nothing nearer to the centre does;
 *       outside the disc the base stays).
 *   SH3 100 then 106: the new mark.
 *   SH4 106 then 7: the caution mark (the text replaced).
 *   SH5 90 and 102: the vanguard bracket; 90 against 95 with the corners; an
 *       alpha of 200 on a coloured base (the colours stay); a turn of 360.
 *   SH6 90 with 丸め: the two overlays (alpha 0 on 128), against 95 with the
 *       hand-worked vertices, hand pixels at the edges and the corners; a
 *       small rectangle whose d is 2 and 1.
 *   SH7 7, 27 and 28: the button shadow's shape (the text replaced).
 *   SH8 what stays unbuilt, as before (guards: they pass before the fix).
 *   SH9 what is built with nothing drawn: 101 with W 0, 100 and 106 with r
 *       0 (106 also with its centre outside), 100 with r INT_MIN (0x507395
 *       takes any radius not positive), 90 with H 0; the step after it runs.
 *   SH10 PE_Save / PE_Load: XPE version 13 (no new operation), the same
 *       pixels and operations.
 *   SH11 off the CN glyph grid the text states stay unbuilt and the others
 *       are built (they do not depend on the encoding); before v14 none is.
 * Only functions that exist before the fix are called: the file builds before
 * it.
 */
static int sh_failures;
#define SH_CHECK(c, ...) do { if (!(c)) { printf("  FAIL: "); printf(__VA_ARGS__); printf("\n"); sh_failures++; } } while (0)

#define SH_ROUND "\xcd\xe8\xa4\xe1"				/* 丸め */
#define SH_ROUND_S "\x8a\xdb\x82\xdf"
#define SH_BLUR "\xa5\xd6\xa5\xe9\xa9\x60"			/* ブラー */
#define SH_POINTS "\xd7\xf9\x98\xcb\xbf\x82\xd3\x5b"		/* 座標總覽 */
#define SH_INTERP "\xd1\x61\xe9\x67\xa5\xbf\xa5\xa4\xa5\xd7"	/* 補間タイプ */
/* Two GBK characters: U+4E00, U+4E8C. */
#define SH_TEXT2 "\xd2\xbb\xb6\xfe"

enum {
	SH_SJIS = 1 << 0,	/* the SJIS keys */
	SH_NO_COLOR = 1 << 1,	/* no 色１ */
	SH_NO_RADIUS = 1 << 2,	/* no 半徑 */
	SH_NO_TEXT = 1 << 3,	/* no 文本 */
	SH_SJIS_TEXT = 1 << 4,	/* 文本 under an SJIS spelling */
	SH_FLOAT_ROUND = 1 << 5,	/* 丸め's second item is a float */
	SH_EXTRA = 1 << 6,	/* the keys every step of BattleResultPlayerView has */
	SH_FLOAT_COLOR = 1 << 7,	/* 色１'s last item is a float */
};

struct sh_step {
	int cmd;
	int dest[6];		/* 先矩形 */
	int c1[4];		/* 色１ */
	int full;		/* 全體 */
	int radius[2];		/* 半徑 */
	int round[2];		/* 丸め */
	int rotate;		/* 旋轉角度 */
	int blur;		/* ブラー */
	const char *text;	/* 文本 */
	int size;		/* フォントサイズ */
	int rgb[3];		/* フォント色 */
	int char_space, line_space;
	int nr_xy;		/* 座標總覽 (command 95) */
	const int *xy;
	int flags;
};
#define SH_CREATE(w, h) { .cmd = 0, .dest = { 0, 0, 0, 0, w, h } }
#define SH_ALPHA_ALL(a) { .cmd = 5, .c1 = { 0, 0, 0, a }, .full = 1 }
#define SH_BASE(r, g, b, a) { .cmd = 6, .c1 = { r, g, b, a }, .full = 1 }
#define SH_DISC(c, x, y, rad, r, g, b, a) { .cmd = c, .dest = { x, y }, .c1 = { r, g, b, a }, .radius = { rad, rad } }
#define SH_SQUARE(x, y, w, h, r, g, b, a) { .cmd = 101, .dest = { x, y, 0, 0, w, h }, .c1 = { r, g, b, a } }
#define SH_RECT(x, y, w, h, a, rnd) { .cmd = 90, .dest = { x, y, 0, 0, w, h }, .c1 = { 0, 0, 0, a }, .round = { 0, rnd } }
#define SH_POLY(a, pts) { .cmd = 95, .c1 = { 0, 0, 0, a }, .nr_xy = (int)(sizeof(pts) / sizeof((pts)[0])), .xy = pts }
#define SH_N(steps) (int)(sizeof(steps) / sizeof((steps)[0])), steps

/* A 構築部件 whose normal state's 手順リスト holds the steps. */
static struct ex_tree sh_parts(const char *name, int x, int y, int n, const struct sh_step *steps)
{
	struct lb_place pl = { x, y, 1 };
	struct ex_tree info = bu_branch(BU_INFO, 2);
	info.children[0] = bu_sleaf(BU_TYPE, BU_LOW);
	info.children[1] = bu_branch(BU_NORMAL, 3);
	struct ex_tree *st = &info.children[1];
	st->children[0] = bu_sleaf(BU_TYPE, BR_CONS);
	st->children[1] = lb_ilist(CN_AREA, 4, (int[]){ 0, 0, 0, 0 });
	const bool sjis = n && (steps[0].flags & SH_SJIS);
	st->children[2] = bu_branch(sjis ? CN_STEPS_S : BR_STEPS, n);
	for (int i = 0; i < n; i++) {
		const struct sh_step *s = &steps[i];
		const bool sj = s->flags & SH_SJIS;
		char key[32];
		snprintf(key, sizeof key, "Step%d", i + 1);
		struct ex_tree t = bu_branch(key, 24);
		unsigned k = 0;
		t.children[k++] = bu_ileaf(sj ? CN_COMMAND_S : BR_COMMAND, s->cmd);
		t.children[k++] = lb_ilist(sj ? CN_DEST_S : LB_DEST, 6, s->dest);
		if (!(s->flags & SH_NO_COLOR)) {
			t.children[k++] = lb_ilist(sj ? CN_COLOR1_S : CN_COLOR1, 4, s->c1);
			if (s->flags & SH_FLOAT_COLOR)
				t.children[k - 1].leaf.value.list->items[3].value = (struct ex_value){ .type = EX_FLOAT, .f = s->c1[3] };
		}
		t.children[k++] = bu_ileaf(sj ? CN_FULL_S : CN_FULL, s->full);
		if (!(s->flags & SH_NO_RADIUS))
			t.children[k++] = lb_ilist(sj ? CN_RADIUS_S : CN_RADIUS, 2, s->radius);
		t.children[k++] = lb_ilist(sj ? SH_ROUND_S : SH_ROUND, 2, s->round);
		if (s->flags & SH_FLOAT_ROUND)
			t.children[k - 1].leaf.value.list->items[1].value = (struct ex_value){ .type = EX_FLOAT, .f = s->round[1] };
		t.children[k++] = bu_ileaf(sj ? CN_ROTATE_S : CN_ROTATE, s->rotate);
		t.children[k++] = lb_ilist(sj ? CN_ARC_S : CN_ARC, 2, (int[]){ 0, 0 });
		t.children[k++] = bu_ileaf(SH_BLUR, s->blur);
		t.children[k++] = bu_sleaf(BR_CG_NAME, "");
		if (!(s->flags & SH_NO_TEXT))
			t.children[k++] = bu_sleaf((s->flags & SH_SJIS_TEXT) ? BX_K_TEXT_SJIS : BX_K_TEXT, s->text ? s->text : "");
		if (s->text || (s->flags & SH_EXTRA)) {
			t.children[k++] = bu_ileaf(BX_K_FONT_SIZE, s->text ? s->size : 12);
			t.children[k++] = bu_ileaf(BX_K_FONT_TYPE, 0);
			t.children[k++] = lb_ilist(BX_K_FONT_COLOR, 3, s->text ? s->rgb : (int[]){ 255, 255, 255 });
			t.children[k++] = bu_ileaf(BX_K_FONT_BOLD, 0);
			t.children[k++] = bu_ileaf(BX_K_FONT_EDGE, 0);
			t.children[k++] = lb_ilist(BX_K_FONT_EDGE_COLOR, 3, (int[]){ 0, 0, 0 });
			t.children[k++] = bu_ileaf(BX_K_CHAR_SPACE, s->char_space);
			t.children[k++] = bu_ileaf(BX_K_LINE_SPACE, s->text ? s->line_space : 4);
		}
		if (s->flags & SH_EXTRA)
			t.children[k++] = bu_ileaf(SH_INTERP, 1);
		if (s->xy)
			t.children[k++] = lb_ilist(SH_POINTS, s->nr_xy, s->xy);
		assert(k <= 24);
		t.nr_children = k;
		st->children[2].children[i] = t;
	}
	return lb_node(name, &pl, info, 0);
}

struct sh_def { const char *name; int n; const struct sh_step *steps; };

/* Loads one activity with the states and gives their parts numbers. */
static bool sh_load(const char *act_name, int nr, const struct sh_def *defs, int *no)
{
	struct ex_tree *kids;
	struct ex *e = lb_activity(act_name, nr, &kids);
	for (int i = 0; i < nr; i++)
		kids->children[i] = sh_parts(defs[i].name, 10 + 40 * (i % 16), 10 + 40 * (i / 16), defs[i].n, defs[i].steps);
	int act = cn_load(act_name, e);
	bool all = true;
	for (int i = 0; i < nr; i++) {
		no[i] = bu_number(act, defs[i].name);
		SH_CHECK(no[i] > 0 && parts_try_get(no[i]), "%s has no parts", defs[i].name);
		all = all && no[i] > 0 && parts_try_get(no[i]);
	}
	heap_unref(act);
	sh_failures += cn_failures;
	cn_failures = 0;
	return all;
}

static int sh_ops(int no)
{
	struct parts *p = parts_try_get(no);
	return p ? cn_nr_ops(p) : -1;
}
/* How many operations of a type the state has. */
static int sh_ops_of(int no, int type)
{
	struct parts *p = parts_try_get(no);
	int n = 0;
	struct parts_cp_op *op;
	if (p)
		TAILQ_FOREACH(op, &p->states[0].cproc.ops, entry)
			n += (int)op->type == type;
	return n;
}

/* --- the models --- */
static void sh_set(uint8_t *p, int r, int g, int b, int a) { p[0] = r; p[1] = g; p[2] = b; p[3] = a; }
static uint8_t *sh_canvas(int w, int h, int r, int g, int b, int a)
{
	uint8_t *px = malloc((size_t)w * h * 4);
	for (int i = 0; i < w * h; i++)
		sh_set(px + i * 4, r, g, b, a);
	return px;
}
static int sh_clamp(int v) { return v < 0 ? 0 : v > 255 ? 255 : v; }

/* The subsamples (cx + i/8, cy + j/8) of the cell at (cx, cy) from the centre
 * within r, counted in eighths, scaled as 0x4e6530 scales them. */
static int sh_cov(int cx, int cy, int r)
{
	int n = 0;
	for (int j = 0; j < 8; j++)
		for (int i = 0; i < 8; i++) {
			const long u = 8L * cx + i, v = 8L * cy + j;
			n += u * u + v * v <= 64L * r * r;
		}
	return n * 255 / 64;
}
/* The pixel position X - r + k lands on, -1 for none: -1 lands on 0 (its
 * position is -0.5), anything further out or at the size and beyond on none. */
static int sh_cell_pixel(long p, int size) { return p == -1 ? 0 : p < -1 || p >= size ? -1 : (int)p; }

/* Command 100's disc (and 101's) on px: every covered cell, in the scan's
 * order, writes the colour and A * cov / 255. stage: 106's, staged on a
 * transparent copy and composed (0x494420). */
static void sh_disc(uint8_t *px, int w, int h, int X, int Y, int r, const int c[4], bool stage)
{
	if (r <= 0)
		return;
	uint8_t *st = stage ? sh_canvas(w, h, 0, 0, 0, 0) : px;
	for (int ky = 0; ky <= 2 * r; ky++) {
		const int py = sh_cell_pixel((long)Y - r + ky, h);
		for (int kx = 0; kx <= 2 * r; kx++) {
			const int pxl = sh_cell_pixel((long)X - r + kx, w);
			const int cov = sh_cov(kx - r, ky - r, r);
			if (py < 0 || pxl < 0 || !cov)
				continue;
			sh_set(st + (py * w + pxl) * 4, sh_clamp(c[0]), sh_clamp(c[1]), sh_clamp(c[2]), sh_clamp(c[3] * cov / 255));
		}
	}
	if (!stage)
		return;
	for (int i = 0; i < w * h; i++) {
		const int sa = st[i * 4 + 3];
		uint8_t *d = px + i * 4;
		for (int k = 0; k < 3; k++) {
			const int v = (st[i * 4 + k] - d[k]) * sa;
			d[k] += v >= 0 ? v / 256 : -((-v + 255) / 256);
		}
		if (sa > d[3])
			d[3] = sa;
	}
	free(st);
}

/* 102 on px: the alpha alone. */
static void sh_disc_alpha(uint8_t *px, int w, int h, int X, int Y, int r, int a)
{
	for (int ky = 0; r > 0 && ky <= 2 * r; ky++) {
		const int py = sh_cell_pixel((long)Y - r + ky, h);
		for (int kx = 0; kx <= 2 * r; kx++) {
			const int pxl = sh_cell_pixel((long)X - r + kx, w);
			const int cov = sh_cov(kx - r, ky - r, r);
			if (py >= 0 && pxl >= 0 && cov)
				px[(py * w + pxl) * 4 + 3] = sh_clamp(a * cov / 255);
		}
	}
}

/* 90 without 丸め on px, for W >= 1 and H >= 2 within the surface: the
 * coverage from construction-cmds' hand values (CC4): the columns X .. X + W,
 * X and X + W at 127 a line, the rows Y .. Y + H - 1 with 4, 8 .. 8, 5 lines;
 * cov = lines * column / 8; the alpha of a covered pixel becomes A * cov /
 * 255. */
static void sh_rect(uint8_t *px, int w, int h, int X, int Y, int W, int H, int a)
{
	for (int y = Y; y <= Y + H - 1; y++) {
		const int lines = y == Y ? 4 : y == Y + H - 1 ? 5 : 8;
		for (int x = X; x <= X + W; x++) {
			if (x < 0 || y < 0 || x >= w || y >= h)
				continue;
			const int col = x == X || x == X + W ? 127 : 255;
			const int cov = lines * col / 8;
			if (cov)
				px[(y * w + x) * 4 + 3] = sh_clamp(a * cov / 255);
		}
	}
}

/* What the steps leave on a w x h surface (to be freed): 0 makes it opaque
 * black, 5 and 6 (over all) replace the alpha or everything, 100, 101, 102,
 * 106 and 90 (without 丸め) as above, 7 as backlog-text's model, 27 and 28
 * as construction-cmds' (over all). */
static uint8_t *sh_model(int w, int h, int n, const struct sh_step *steps)
{
	uint8_t *px = sh_canvas(w, h, 0, 0, 0, 255);
	for (int i = 0; i < n; i++) {
		const struct sh_step *s = &steps[i];
		switch (s->cmd) {
		case 0:
			break;
		case 5:
			assert(s->full == 1);
			for (int k = 0; k < w * h; k++)
				px[k * 4 + 3] = s->c1[3];
			break;
		case 6:
			assert(s->full == 1);
			for (int k = 0; k < w * h; k++)
				sh_set(px + k * 4, s->c1[0], s->c1[1], s->c1[2], s->c1[3]);
			break;
		case 100:
		case 106:
			sh_disc(px, w, h, s->dest[0], s->dest[1], s->radius[0], s->c1, s->cmd == 106);
			break;
		case 101: {
			const int d = s->dest[4] > s->dest[5] ? s->dest[4] : s->dest[5];
			assert(!(d & 1));
			if (s->dest[4] > 0 && s->dest[5] > 0)
				sh_disc(px, w, h, s->dest[0] + d / 2, s->dest[1] + d / 2, d / 2, s->c1, false);
			break;
		}
		case 102:
			sh_disc_alpha(px, w, h, s->dest[0], s->dest[1], s->radius[0], s->c1[3]);
			break;
		case 90:
			assert(!s->round[1] && s->dest[4] >= 1 && s->dest[5] >= 2);
			sh_rect(px, w, h, s->dest[0], s->dest[1], s->dest[4], s->dest[5], s->c1[3]);
			break;
		case 7: {
			const struct bt_style st = { .size = s->size, .rgb = { s->rgb[0], s->rgb[1], s->rgb[2] } };
			bt_model_grid(px, w, h, true, 7, s->dest[0], s->dest[1], s->text, &st, s->char_space, s->line_space);
			break;
		}
		case 27:
		case 28:
			assert(s->full == 1);
			cc_blur_model(px, w, h, 0, 0, w, h, s->blur, s->cmd == 28);
			break;
		default:
			assert(!"a command the model does not have");
		}
	}
	return px;
}

static void sh_compare(const uint8_t *got, int w, int h, const uint8_t *model, const char *what)
{
	int bad = 0;
	for (int i = 0; got && i < w * h; i++) {
		if (!memcmp(got + i * 4, model + i * 4, 4))
			continue;
		if (bad < 3)
			SH_CHECK(0, "%s (%d,%d) = (%d,%d,%d,%d), want (%d,%d,%d,%d)", what, i % w, i / w, got[i * 4],
				 got[i * 4 + 1], got[i * 4 + 2], got[i * 4 + 3], model[i * 4], model[i * 4 + 1],
				 model[i * 4 + 2], model[i * 4 + 3]);
		bad++;
	}
	if (bad > 3)
		SH_CHECK(0, "%s: %d pixels differ in all", what, bad);
}

static void sh_px(const uint8_t *px, int w, int x, int y, int r, int g, int b, int a, const char *what)
{
	const uint8_t *p = px ? px + (y * w + x) * 4 : NULL;
	SH_CHECK(p && p[0] == r && p[1] == g && p[2] == b && p[3] == a, "%s (%d,%d) = (%d,%d,%d,%d), want (%d,%d,%d,%d)",
		 what, x, y, p ? p[0] : -1, p ? p[1] : -1, p ? p[2] : -1, p ? p[3] : -1, r, g, b, a);
}
static void sh_alpha(const uint8_t *px, int w, int x, int y, int a, const char *what)
{
	const uint8_t *p = px ? px + (y * w + x) * 4 : NULL;
	SH_CHECK(p && p[3] == a, "%s: alpha (%d,%d) = %d, want %d", what, x, y, p ? p[3] : -1, a);
}

/* The built state's pixels (to be freed; NULL when not built). */
static uint8_t *sh_pixels(int no, int w, int h, const char *what)
{
	uint8_t *got = cn_pixels(no, w, h, what);
	sh_failures += cn_failures;
	cn_failures = 0;
	return got;
}
/* The built state against the model; returns its pixels (to be freed). */
static uint8_t *sh_expect(int no, int w, int h, int n, const struct sh_step *steps, const char *what)
{
	uint8_t *got = sh_pixels(no, w, h, what);
	uint8_t *model = sh_model(w, h, n, steps);
	sh_compare(got, w, h, model, what);
	free(model);
	return got;
}
/* Two built states with the same pixels (to compare 90 with 95). */
static void sh_same(int a, int b, int w, int h, const char *what)
{
	uint8_t *pa = sh_pixels(a, w, h, what), *pb = sh_pixels(b, w, h, what);
	if (pa && pb)
		sh_compare(pa, w, h, pb, what);
	free(pa);
	free(pb);
}

/* --- the game's shapes --- */
static const struct sh_step sh_result[] = {
	{ .cmd = 0, .dest = { 0, 0, 0, 0, 128, 128 }, .flags = SH_EXTRA }, { .cmd = 5, .full = 1, .flags = SH_EXTRA },
	{ .cmd = 101, .dest = { 0, 0, 0, 0, 128, 128 }, .c1 = { 255, 0, 186, 255 }, .flags = SH_EXTRA },
};
static const struct sh_step sh_stock[] = { SH_CREATE(128, 128), SH_ALPHA_ALL(0), SH_SQUARE(0, 0, 128, 128, 255, 0, 186, 255) };
static const struct sh_step sh_node[] = { SH_CREATE(32, 32), SH_ALPHA_ALL(0), SH_SQUARE(4, 4, 24, 24, 255, 255, 255, 255) };
static const struct sh_step sh_wide[] = { SH_CREATE(30, 30), SH_ALPHA_ALL(0), SH_SQUARE(2, 3, 20, 8, 10, 20, 30, 200) };
static const struct sh_step sh_slot[] = { SH_CREATE(24, 24), SH_ALPHA_ALL(0), SH_DISC(100, 12, 12, 12, 255, 0, 186, 255) };
static const struct sh_step sh_slot_off[] = { SH_CREATE(24, 24), SH_ALPHA_ALL(0), SH_DISC(100, 12, 12, 12, 0, 0, 0, 255) };
static const struct sh_step sh_cut[] = { SH_CREATE(12, 10), SH_BASE(90, 90, 90, 250), SH_DISC(100, 2, 1, 4, 10, 200, 30, 128) };
/* Only the column -1 (dx 3) lands on the surface; row -1 (dy 4) is wider than
 * row 0 (dy 5). */
static const struct sh_step sh_far_left[] = { SH_CREATE(8, 10), SH_BASE(90, 90, 90, 250), SH_DISC(100, -4, 5, 3, 5, 6, 7, 255) };
static const struct sh_step sh_far_up[] = { SH_CREATE(16, 8), SH_BASE(90, 90, 90, 250), SH_DISC(100, 8, -5, 6, 5, 6, 7, 255) };
static const struct sh_step sh_newmark[] = {
	SH_CREATE(16, 16), SH_BASE(0, 0, 0, 0), SH_DISC(100, 8, 8, 6, 0, 0, 0, 255), SH_DISC(106, 8, 8, 4, 255, 255, 0, 255),
};
static const struct sh_step sh_caution[] = {
	SH_CREATE(24, 24), SH_BASE(255, 0, 0, 0), SH_DISC(106, 12, 12, 12, 255, 0, 0, 255),
	{ .cmd = 7, .dest = { 9, 1 }, .text = "x", .size = 17, .rgb = { 255, 255, 255 }, .line_space = 6 },
};
static const struct sh_step sh_vanguard[] = {
	SH_CREATE(406, 100), SH_ALPHA_ALL(0),
	SH_RECT(10, 0, 4, 55, 255, 0), SH_RECT(138, 0, 4, 55, 255, 0), SH_RECT(266, 0, 4, 55, 255, 0),
	SH_RECT(394, 0, 4, 55, 255, 0), SH_RECT(10, 0, 384, 4, 255, 0),
	SH_DISC(102, 13, 54, 8, 0, 0, 0, 255), SH_DISC(102, 141, 54, 8, 0, 0, 0, 255),
	SH_DISC(102, 269, 54, 8, 0, 0, 0, 255), SH_DISC(102, 397, 54, 8, 0, 0, 0, 255),
};
static const struct sh_step sh_shadow[] = {
	SH_CREATE(76, 42), SH_BASE(255, 0, 185, 255),
	{ .cmd = 7, .dest = { 5, 5 }, .text = SH_TEXT2, .size = 25, .rgb = { 128, 0, 92 }, .char_space = -1, .line_space = 9 },
	{ .cmd = 27, .full = 1, .blur = 3 }, { .cmd = 28, .full = 1, .blur = 3 },
};
static const struct sh_step sh_hole1[] = {
	SH_CREATE(1280, 720), SH_ALPHA_ALL(128),
	{ .cmd = 90, .dest = { 1068, 540, 0, 0, 132, 105 }, .round = { 0, 1 }, .flags = SH_NO_COLOR },
};
static const struct sh_step sh_hole2[] = {
	SH_CREATE(1280, 720), SH_ALPHA_ALL(128),
	{ .cmd = 90, .dest = { 1077, 326, 0, 0, 124, 44 }, .round = { 0, 1 }, .flags = SH_NO_COLOR },
};

/* --- SH1 --- */
static void sh_squares(void)
{
	bt_gl_setup(true);
	static const struct sh_def d[] = {
		{ "ResultBase", SH_N(sh_result) }, { "StockBase", SH_N(sh_stock) }, { "NodeBase", SH_N(sh_node) },
		{ "Wide", SH_N(sh_wide) },
	};
	int no[4];
	if (!sh_load("ShapesSquares", 4, d, no))
		return;
	uint8_t *px = sh_expect(no[0], 128, 128, SH_N(sh_result), "SH1 the portrait base");
	/* By hand: the centre (64, 64), r 64. (64, 0) is the cell (0, -64):
	 * its subsamples' v are -512 .. -505, so only (0, -512) of the first
	 * line lies within 512 and the 7 lines below wholly: 57 of 64, 227.
	 * (63, 0) is (-1, -64): none on the first line, 56, 223. (0, 64) is
	 * (-64, 0): 57 again. (127, 64) is (63, 0): u up to 511, all 64. */
	sh_px(px, 128, 64, 0, 255, 0, 186, 227, "SH1 the top");
	sh_px(px, 128, 63, 0, 255, 0, 186, 223, "SH1 left of the top");
	sh_px(px, 128, 0, 64, 255, 0, 186, 227, "SH1 the left");
	sh_px(px, 128, 127, 64, 255, 0, 186, 255, "SH1 the right");
	sh_px(px, 128, 64, 64, 255, 0, 186, 255, "SH1 the centre");
	sh_px(px, 128, 0, 0, 0, 0, 0, 0, "SH1 the corner");
	sh_px(px, 128, 127, 127, 0, 0, 0, 0, "SH1 the far corner");
	free(px);
	/* 0, 5, a fill for each of the 128 rows (the 129th lies below) and the
	 * alpha. */
	SH_CHECK(sh_ops(no[0]) == 2 + 128 + 1 && sh_ops_of(no[0], PARTS_CP_FILL) == 128
		 && sh_ops_of(no[0], PARTS_CP_FILL_CIRCLE_AMAP) == 1,
		 "SH1: %d operations (%d fills, %d alphas), want 131 (128, 1)", sh_ops(no[0]),
		 sh_ops_of(no[0], PARTS_CP_FILL), sh_ops_of(no[0], PARTS_CP_FILL_CIRCLE_AMAP));
	free(sh_expect(no[1], 128, 128, SH_N(sh_stock), "SH1 the stock portrait base"));
	px = sh_expect(no[2], 32, 32, SH_N(sh_node), "SH1 the mini map node");
	/* The centre (16, 16), r 12: (16, 4) 57 of 64; (28, 16) is the cell
	 * (12, 0), only (96, 0) within 96: 3, in white; 3 and 29 are left. */
	sh_px(px, 32, 16, 4, 255, 255, 255, 227, "SH1 the node's top");
	sh_px(px, 32, 4, 16, 255, 255, 255, 227, "SH1 the node's left");
	sh_px(px, 32, 28, 16, 255, 255, 255, 3, "SH1 the node's last column");
	sh_px(px, 32, 3, 16, 0, 0, 0, 0, "SH1 left of the node");
	sh_px(px, 32, 29, 16, 0, 0, 0, 0, "SH1 right of the node");
	sh_px(px, 32, 16, 29, 0, 0, 0, 0, "SH1 below the node");
	free(px);
	/* 20 x 8: the diameter 20, the centre (12, 13): (12, 3) 57, 200 * 227 / 255. */
	px = sh_expect(no[3], 30, 30, SH_N(sh_wide), "SH1 the larger side");
	sh_px(px, 30, 12, 3, 10, 20, 30, 200 * 227 / 255, "SH1 the larger side's top");
	sh_px(px, 30, 12, 23, 10, 20, 30, 200 * 3 / 255, "SH1 the larger side's bottom");
	free(px);
	printf("SH1 checked\n");
}

/* --- SH2 --- */
static void sh_slots(void)
{
	bt_gl_setup(true);
	static const struct sh_def d[] = { { "SlotBase", SH_N(sh_slot) }, { "SlotOff", SH_N(sh_slot_off) }, { "Cut", SH_N(sh_cut) },
		{ "FarLeft", SH_N(sh_far_left) }, { "FarUp", SH_N(sh_far_up) } };
	int no[5];
	if (!sh_load("ShapesSlots", 5, d, no))
		return;
	uint8_t *px = sh_expect(no[0], 24, 24, SH_N(sh_slot), "SH2 the skill slot");
	sh_px(px, 24, 12, 0, 255, 0, 186, 227, "SH2 the slot's top");
	sh_px(px, 24, 23, 12, 255, 0, 186, 255, "SH2 the slot's right");
	sh_px(px, 24, 0, 0, 0, 0, 0, 0, "SH2 the slot's corner");
	free(px);
	SH_CHECK(sh_ops(no[0]) == 2 + 24 + 1, "SH2: %d operations, want 27", sh_ops(no[0]));
	free(sh_expect(no[1], 24, 24, SH_N(sh_slot_off), "SH2 the disabled slot"));
	px = sh_expect(no[2], 12, 10, SH_N(sh_cut), "SH2 cut by the edges");
	/* (0, 0) takes the cells (-3, -2), (-2, -2), (-3, -1), (-2, -1) (the
	 * row and the column at -1 and 0); the last, (-2, -1), lies wholly
	 * within 4: 128. (7, 1) is the cell (5, 0): outside, the base stays.
	 * (6, 1) is (4, 0): only (32, 0) within 32, 128 * 3 / 255 = 1. */
	sh_px(px, 12, 0, 0, 10, 200, 30, 128, "SH2 the corner the edges meet in");
	sh_px(px, 12, 6, 1, 10, 200, 30, 1, "SH2 the disc's last column");
	sh_px(px, 12, 7, 1, 90, 90, 90, 250, "SH2 right of the disc");
	sh_px(px, 12, 2, 6, 90, 90, 90, 250, "SH2 below the disc");
	free(px);
	/* Centre (-4, 5), r 3: column -1 is the cell (3, dy), which only row 5
	 * (dy 0) covers, with one subsample (24, 0): 3. */
	px = sh_expect(no[3], 8, 10, SH_N(sh_far_left), "SH2 only the column at -1");
	sh_px(px, 8, 0, 5, 5, 6, 7, 3, "SH2 the column at -1 on 0");
	sh_px(px, 8, 0, 4, 90, 90, 90, 250, "SH2 above it");
	sh_px(px, 8, 1, 5, 90, 90, 90, 250, "SH2 right of it");
	free(px);
	/* Centre (8, -5), r 6: row 0 takes dy 4 (row -1: columns 3..12) and
	 * dy 5 (4..11); (3, 0) only from the cell (-5, 4), 23 of 255; (4, 0)
	 * last from (-4, 5), 7. */
	px = sh_expect(no[4], 16, 8, SH_N(sh_far_up), "SH2 the row at -1");
	sh_px(px, 16, 3, 0, 5, 6, 7, 23, "SH2 row -1's end on row 0");
	sh_px(px, 16, 4, 0, 5, 6, 7, 7, "SH2 row 0's end");
	sh_px(px, 16, 2, 0, 90, 90, 90, 250, "SH2 left of row 0's runs");
	free(px);
	printf("SH2 checked\n");
}

/* --- SH3 --- */
static void sh_newmark_case(void)
{
	bt_gl_setup(true);
	static const struct sh_def d[] = { { "NewMark", SH_N(sh_newmark) } };
	int no[1];
	if (!sh_load("ShapesNewMark", 1, d, no))
		return;
	uint8_t *px = sh_expect(no[0], 16, 16, SH_N(sh_newmark), "SH3 the new mark");
	/* The centre: black 255, then yellow 255 composed: 255 * 255 >> 8 = 254.
	 * (8, 4): 100's r 6 covers it wholly; 106's r 4 has 57 of 64, 227:
	 * 255 * 227 >> 8 = 226. (8, 2): 100's top, 227; 106 does not reach it. */
	sh_px(px, 16, 8, 8, 254, 254, 0, 255, "SH3 the centre");
	sh_px(px, 16, 8, 4, 226, 226, 0, 255, "SH3 the yellow's top");
	sh_px(px, 16, 8, 2, 0, 0, 0, 227, "SH3 the black's top");
	sh_px(px, 16, 8, 1, 0, 0, 0, 0, "SH3 above the mark");
	free(px);
	SH_CHECK(sh_ops_of(no[0], PARTS_CP_FILL_CIRCLE_BLEND) == 1 && sh_ops_of(no[0], PARTS_CP_FILL_CIRCLE_AMAP) == 1,
		 "SH3: %d blended discs and %d alphas, want 1 and 1", sh_ops_of(no[0], PARTS_CP_FILL_CIRCLE_BLEND),
		 sh_ops_of(no[0], PARTS_CP_FILL_CIRCLE_AMAP));
	printf("SH3 checked\n");
}

/* --- SH4 --- */
static void sh_caution_case(void)
{
	bt_gl_setup(true);
	static const struct sh_def d[] = { { "Caution", SH_N(sh_caution) } };
	int no[1];
	if (!sh_load("ShapesCaution", 1, d, no))
		return;
	uint8_t *px = sh_expect(no[0], 24, 24, SH_N(sh_caution), "SH4 the caution mark");
	/* The red disc on a red base of alpha 0 changes the alpha alone; the
	 * white glyph ("x", 9 x 17 at (9, 1)) blends by its alpha: at (12, 5),
	 * the glyph's (3, 4), the green and blue are 255 * a >> 8. */
	const int ga = bt_glyph_alpha('x', 3, 4);
	sh_px(px, 24, 12, 5, 255, 255 * ga >> 8, 255 * ga >> 8, 255, "SH4 a glyph pixel");
	sh_px(px, 24, 12, 20, 255, 0, 0, 255, "SH4 below the glyph");
	sh_px(px, 24, 12, 0, 255, 0, 0, 227, "SH4 the disc's top");
	sh_px(px, 24, 0, 0, 255, 0, 0, 0, "SH4 the corner");
	free(px);
	SH_CHECK(sh_ops_of(no[0], PARTS_CP_DRAW_TEXT) == 1, "SH4: %d text operations, want 1", sh_ops_of(no[0], PARTS_CP_DRAW_TEXT));
	printf("SH4 checked\n");
}

/* --- SH5 --- */
static const int sh_rect_xy[] = { 20, 3, 31, 3, 31, 8, 20, 8 };
static const struct sh_step sh_r90[] = { SH_CREATE(40, 16), SH_BASE(30, 60, 90, 40), SH_RECT(20, 3, 12, 6, 200, 0) };
static const struct sh_step sh_r95[] = { SH_CREATE(40, 16), SH_BASE(30, 60, 90, 40), SH_POLY(200, sh_rect_xy) };
static const struct sh_step sh_r360[] = { SH_CREATE(40, 16), SH_BASE(30, 60, 90, 40),
	{ .cmd = 90, .dest = { 20, 3, 0, 0, 12, 6 }, .c1 = { 0, 0, 0, 200 }, .rotate = 360 } };
static const struct sh_step sh_rsjis[] = { { .cmd = 0, .dest = { 0, 0, 0, 0, 40, 16 }, .flags = SH_SJIS },
	{ .cmd = 6, .c1 = { 30, 60, 90, 40 }, .full = 1, .flags = SH_SJIS },
	{ .cmd = 90, .dest = { 20, 3, 0, 0, 12, 6 }, .c1 = { 0, 0, 0, 200 }, .flags = SH_SJIS } };

static void sh_vanguard_case(void)
{
	bt_gl_setup(true);
	static const struct sh_def d[] = {
		{ "Vanguard", SH_N(sh_vanguard) }, { "R90", SH_N(sh_r90) }, { "R95", SH_N(sh_r95) }, { "R360", SH_N(sh_r360) },
		{ "RSjis", SH_N(sh_rsjis) },
	};
	int no[5];
	if (!sh_load("ShapesVanguard", 5, d, no))
		return;
	uint8_t *px = sh_expect(no[0], 406, 100, SH_N(sh_vanguard), "SH5 the vanguard bracket");
	/* The first bar (10, 0, 4, 55): columns 10 and 14 half, 11..13 whole;
	 * the top bar (10, 0, 384, 4), drawn after it, rows 0 (4 lines), 1..2,
	 * 3 (5 lines); the disc r 8 at (13, 54) over the bar's foot. */
	sh_px(px, 406, 12, 30, 0, 0, 0, 255, "SH5 inside a bar");
	sh_alpha(px, 406, 10, 30, 127, "SH5 a bar's left column");
	sh_alpha(px, 406, 14, 30, 127, "SH5 a bar's column past its right");
	sh_alpha(px, 406, 9, 30, 0, "SH5 left of a bar");
	sh_alpha(px, 406, 15, 30, 0, "SH5 right of a bar");
	sh_alpha(px, 406, 200, 0, 127, "SH5 the top bar's first row");
	sh_alpha(px, 406, 200, 2, 255, "SH5 inside the top bar");
	sh_alpha(px, 406, 200, 3, 159, "SH5 the top bar's last row");
	sh_alpha(px, 406, 200, 4, 0, "SH5 below the top bar");
	sh_alpha(px, 406, 394, 2, 127, "SH5 the top bar's column past its right");
	sh_alpha(px, 406, 13, 54, 255, "SH5 the disc's centre");
	free(px);
	SH_CHECK(sh_ops_of(no[0], PARTS_CP_FILL_POLYGON_AMAP) == 5 && sh_ops_of(no[0], PARTS_CP_FILL_CIRCLE_AMAP) == 4,
		 "SH5: %d rectangles and %d discs, want 5 and 4", sh_ops_of(no[0], PARTS_CP_FILL_POLYGON_AMAP),
		 sh_ops_of(no[0], PARTS_CP_FILL_CIRCLE_AMAP));
	/* 90 is 95 with the corners (x, y), (x + W - 1, y), ...: the alpha 200,
	 * the colours of the base. */
	px = sh_expect(no[1], 40, 16, SH_N(sh_r90), "SH5 90 on a coloured base");
	sh_px(px, 40, 25, 5, 30, 60, 90, 200, "SH5 90 inside");
	sh_px(px, 40, 20, 5, 30, 60, 90, 200 * 127 / 255, "SH5 90's left column");
	sh_px(px, 40, 33, 5, 30, 60, 90, 40, "SH5 right of 90");
	free(px);
	sh_same(no[1], no[2], 40, 16, "SH5 90 against 95");
	sh_same(no[1], no[3], 40, 16, "SH5 a turn of 360");
	sh_same(no[1], no[4], 40, 16, "SH5 the SJIS keys");
	printf("SH5 checked\n");
}

/* --- SH6 --- */
/* The rounded rectangles' vertices by hand (pactex_rect_polygon's comment):
 * (1068, 540, 132, 105): x1 = 1199, y1 = 644, d = min(4, 131 / 4) = 4 and
 * min(4, 104 / 4) = 4: (1072, 539), (1194, 539), (1198, 544), (1198, 639),
 * (1194, 643), (1072, 643), (1067, 639), (1067, 544). */
static const int sh_hole1_xy[] = { 1072, 539, 1194, 539, 1198, 544, 1198, 639, 1194, 643, 1072, 643, 1067, 639, 1067, 544 };
/* (1077, 326, 124, 44): x1 = 1200, y1 = 369, d 4 and min(4, 43 / 4) = 4. */
static const int sh_hole2_xy[] = { 1081, 325, 1195, 325, 1199, 330, 1199, 364, 1195, 368, 1081, 368, 1076, 364, 1076, 330 };
/* (20, 20, 9, 7): x1 = 28, y1 = 26, d = 8 / 4 = 2 and 6 / 4 = 1. */
static const int sh_small_xy[] = { 22, 19, 25, 19, 27, 21, 27, 24, 25, 25, 22, 25, 19, 24, 19, 21 };
static const struct sh_step sh_hole1_95[] = { SH_CREATE(1280, 720), SH_ALPHA_ALL(128), SH_POLY(0, sh_hole1_xy) };
static const struct sh_step sh_hole2_95[] = { SH_CREATE(1280, 720), SH_ALPHA_ALL(128), SH_POLY(0, sh_hole2_xy) };
static const struct sh_step sh_small[] = { SH_CREATE(48, 40), SH_ALPHA_ALL(0), SH_RECT(20, 20, 9, 7, 255, 1) };
static const struct sh_step sh_small_95[] = { SH_CREATE(48, 40), SH_ALPHA_ALL(0), SH_POLY(255, sh_small_xy) };

static void sh_holes(void)
{
	bt_gl_setup(true);
	static const struct sh_def d[] = {
		{ "Hole1", SH_N(sh_hole1) }, { "Hole1By95", SH_N(sh_hole1_95) }, { "Hole2", SH_N(sh_hole2) },
		{ "Hole2By95", SH_N(sh_hole2_95) }, { "Small", SH_N(sh_small) }, { "SmallBy95", SH_N(sh_small_95) },
	};
	int no[6];
	if (!sh_load("ShapesHoles", 6, d, no))
		return;
	sh_same(no[0], no[1], 1280, 720, "SH6 the first overlay against 95");
	sh_same(no[2], no[3], 1280, 720, "SH6 the second overlay against 95");
	sh_same(no[4], no[5], 48, 40, "SH6 d 2 and 1 against 95");
	uint8_t *px = sh_pixels(no[0], 1280, 720, "SH6 the first overlay");
	/* By hand, with A 0 a covered pixel is 0 and any other 128. The
	 * vertical edges are at 1067.5 and 1198.5: the span [1067.5, 1199.5)
	 * covers half of 1067 and of 1199. The horizontal ones at 539.5 and
	 * 643.5: row 539 has lines 4..7, row 643 lines 0..4. The edge from
	 * (1067.5, 544.5) to (1072.5, 539.5): on row 540 its x is 1072 - k/8,
	 * so column 1071 gets 31 + 63 + ... + 223 = 889 and 1070 nothing; on row
	 * 539 it stays right of 1072. The plain rectangle would lie one pixel
	 * down and right (1068..1200, 540..644). */
	sh_alpha(px, 1280, 1100, 600, 0, "SH6 inside the hole");
	sh_alpha(px, 1280, 1066, 600, 128, "SH6 left of the hole");
	sh_alpha(px, 1280, 1067, 600, 0, "SH6 the hole's left column");
	sh_alpha(px, 1280, 1199, 600, 0, "SH6 the hole's half column on the right");
	sh_alpha(px, 1280, 1200, 600, 128, "SH6 right of the hole");
	sh_alpha(px, 1280, 1100, 538, 128, "SH6 above the hole");
	sh_alpha(px, 1280, 1100, 539, 0, "SH6 the hole's top row");
	sh_alpha(px, 1280, 1100, 643, 0, "SH6 the hole's bottom row");
	sh_alpha(px, 1280, 1100, 644, 128, "SH6 below the hole");
	sh_alpha(px, 1280, 1070, 540, 128, "SH6 the corner, row 540, column 1070");
	sh_alpha(px, 1280, 1071, 540, 0, "SH6 the corner, row 540, column 1071");
	sh_alpha(px, 1280, 1071, 539, 128, "SH6 the corner, row 539, column 1071");
	sh_alpha(px, 1280, 1072, 539, 0, "SH6 the corner, row 539, column 1072");
	sh_px(px, 1280, 10, 10, 0, 0, 0, 128, "SH6 the overlay");
	free(px);
	SH_CHECK(sh_ops_of(no[0], PARTS_CP_FILL_POLYGON_AMAP) == 1, "SH6: %d polygons, want 1", sh_ops_of(no[0], PARTS_CP_FILL_POLYGON_AMAP));
	printf("SH6 checked\n");
}

/* --- SH7 --- */
static void sh_shadow_case(void)
{
	bt_gl_setup(true);
	static const struct sh_def d[] = { { "Shadow", SH_N(sh_shadow) } };
	int no[1];
	if (!sh_load("ShapesShadow", 1, d, no))
		return;
	uint8_t *px = sh_expect(no[0], 76, 42, SH_N(sh_shadow), "SH7 the button shadow");
	/* The alpha stays 255; away from the text the base is unchanged. */
	sh_px(px, 76, 74, 40, 255, 0, 185, 255, "SH7 away from the text");
	free(px);
	printf("SH7 checked\n");
}

/* --- SH8 --- */
#define SH_SMALL SH_CREATE(40, 30), SH_ALPHA_ALL(0)
static const struct sh_step sh_u_odd[] = { SH_SMALL, SH_SQUARE(0, 0, 25, 25, 255, 255, 255, 255) };
static const struct sh_step sh_u_big[] = { SH_SMALL, SH_SQUARE(0, 0, 2050, 10, 255, 255, 255, 255) };
static const struct sh_step sh_u_c256[] = { SH_SMALL, SH_DISC(100, 10, 10, 5, 256, 0, 0, 255) };
static const struct sh_step sh_u_aneg[] = { SH_SMALL, SH_DISC(101, 0, 0, 0, 0, 0, 0, -1) };
static const struct sh_step sh_u_nocolor[] = { SH_SMALL, { .cmd = 100, .dest = { 10, 10 }, .radius = { 5, 5 }, .flags = SH_NO_COLOR } };
static const struct sh_step sh_u_noradius[] = { SH_SMALL, { .cmd = 100, .dest = { 10, 10 }, .c1 = { 1, 2, 3, 4 }, .flags = SH_NO_RADIUS } };
static const struct sh_step sh_u_bigr[] = { SH_SMALL, SH_DISC(100, 10, 10, 5000, 1, 2, 3, 4) };
static const struct sh_step sh_u_106out[] = { SH_SMALL, SH_DISC(106, -1, 10, 5, 1, 2, 3, 255) };
static const struct sh_step sh_u_106below[] = { SH_SMALL, SH_DISC(106, 10, 30, 5, 1, 2, 3, 255) };
static const struct sh_step sh_u_106c[] = { SH_SMALL, SH_DISC(106, 10, 10, 5, 1, -2, 3, 255) };
static const struct sh_step sh_u_near[] = { SH_SMALL, SH_RECT(15, 16, 10, 10, 255, 1) };
static const struct sh_step sh_u_short[] = { SH_CREATE(60, 60), SH_ALPHA_ALL(0), SH_RECT(20, 20, 4, 10, 255, 1) };
static const struct sh_step sh_u_turn[] = { SH_SMALL, { .cmd = 90, .dest = { 2, 2, 0, 0, 10, 10 }, .c1 = { 0, 0, 0, 255 }, .rotate = 30 } };
static const struct sh_step sh_u_outside[] = { SH_SMALL, SH_RECT(50, 2, 10, 10, 255, 0) };
static const struct sh_step sh_u_fround[] = { SH_SMALL, { .cmd = 90, .dest = { 2, 2, 0, 0, 10, 10 }, .c1 = { 0, 0, 0, 255 },
	.round = { 0, 1 }, .flags = SH_FLOAT_ROUND } };
static const struct sh_step sh_u_falpha[] = { SH_SMALL, { .cmd = 90, .dest = { 2, 2, 0, 0, 10, 10 }, .c1 = { 0, 0, 0, 255 },
	.flags = SH_FLOAT_COLOR } };
static const struct sh_step sh_u_notext[] = { SH_SMALL, { .cmd = 7, .text = "Ab", .size = 12, .flags = SH_NO_TEXT } };
static const struct sh_step sh_u_sjistext[] = { SH_SMALL, { .cmd = 7, .text = "Ab", .size = 12, .flags = SH_SJIS_TEXT } };
static const struct sh_step sh_u_size0[] = { SH_SMALL, { .cmd = 7, .text = "Ab", .size = 0 } };
static const struct sh_step sh_u_first[] = { SH_DISC(100, 10, 10, 5, 1, 2, 3, 4) };
/* Built, drawing nothing; the alpha 200 after them shows the step ran. */
static const struct sh_step sh_n_w0[] = { SH_SMALL, SH_SQUARE(2, 2, 0, 10, 255, 255, 255, 255), SH_ALPHA_ALL(200) };
static const struct sh_step sh_n_r0[] = { SH_SMALL, SH_DISC(100, 10, 10, 0, 255, 255, 255, 255), SH_ALPHA_ALL(200) };
static const struct sh_step sh_n_rmin[] = { SH_SMALL, SH_DISC(100, 10, 10, INT_MIN, 255, 255, 255, 255), SH_ALPHA_ALL(200) };
static const struct sh_step sh_n_106r0[] = { SH_SMALL, SH_DISC(106, -5, 10, 0, 255, 255, 255, 255), SH_ALPHA_ALL(200) };
static const struct sh_step sh_n_h0[] = { SH_SMALL, SH_RECT(2, 2, 10, 0, 255, 1), SH_ALPHA_ALL(200) };

static void sh_unbuilt(void)
{
	bt_gl_setup(true);
	static const struct sh_def u[] = {
		{ "OddDiameter", SH_N(sh_u_odd) }, { "BigDiameter", SH_N(sh_u_big) }, { "Colour256", SH_N(sh_u_c256) },
		{ "AlphaNeg", SH_N(sh_u_aneg) }, { "NoColour", SH_N(sh_u_nocolor) }, { "NoRadius", SH_N(sh_u_noradius) },
		{ "BigRadius", SH_N(sh_u_bigr) }, { "Blend106Left", SH_N(sh_u_106out) }, { "Blend106Below", SH_N(sh_u_106below) },
		{ "Blend106Colour", SH_N(sh_u_106c) }, { "RoundNear0", SH_N(sh_u_near) }, { "RoundShort", SH_N(sh_u_short) },
		{ "Turn30", SH_N(sh_u_turn) }, { "Outside", SH_N(sh_u_outside) }, { "FloatRound", SH_N(sh_u_fround) },
		{ "FloatAlpha", SH_N(sh_u_falpha) }, { "NoText", SH_N(sh_u_notext) }, { "SjisText", SH_N(sh_u_sjistext) },
		{ "Size0", SH_N(sh_u_size0) }, { "DiscFirst", SH_N(sh_u_first) },
	};
	enum { NU = sizeof(u) / sizeof(u[0]) };
	int no[NU];
	if (!sh_load("ShapesUnbuilt", NU, u, no))
		return;
	for (int i = 0; i < NU; i++) {
		char what[64];
		snprintf(what, sizeof what, "SH8 %s", u[i].name);
		const bool first_creates = u[i].steps[0].cmd == 0;
		cn_expect_unbuilt(no[i], first_creates ? u[i].steps[0].dest[4] : 0, first_creates ? u[i].steps[0].dest[5] : 0, what);
	}
	sh_failures += cn_failures;
	cn_failures = 0;
	printf("SH8 checked\n");
}

/* --- SH9 --- */
static void sh_nothing(void)
{
	bt_gl_setup(true);
	static const struct sh_def b[] = {
		{ "Width0", SH_N(sh_n_w0) }, { "Radius0", SH_N(sh_n_r0) }, { "Blend106Radius0", SH_N(sh_n_106r0) },
		{ "Height0", SH_N(sh_n_h0) }, { "RadiusIntMin", SH_N(sh_n_rmin) },
	};
	int nb[5];
	if (!sh_load("ShapesNothingDrawn", 5, b, nb))
		return;
	for (int i = 0; i < 5; i++) {
		char what[64];
		snprintf(what, sizeof what, "SH9 %s", b[i].name);
		uint8_t *px = sh_pixels(nb[i], 40, 30, what);
		uint8_t *model = sh_canvas(40, 30, 0, 0, 0, 200);
		sh_compare(px, 40, 30, model, what);
		free(model);
		free(px);
		SH_CHECK(sh_ops(nb[i]) == 3, "%s: %d operations, want 3 (nothing for the step)", what, sh_ops(nb[i]));
	}
	printf("SH9 checked\n");
}

/* --- SH10 --- */
static void sh_save(void)
{
	bt_gl_setup(true);
	static const struct sh_def d[] = {
		{ "SlotBase", SH_N(sh_slot) }, { "Vanguard", SH_N(sh_vanguard) }, { "Caution", SH_N(sh_caution) },
		{ "NewMark", SH_N(sh_newmark) }, { "NodeBase", SH_N(sh_node) }, { "Small", SH_N(sh_small) },
	};
	int no[6];
	if (!sh_load("ShapesSave", 6, d, no))
		return;
	static const int wh[6][2] = { { 24, 24 }, { 406, 100 }, { 24, 24 }, { 16, 16 }, { 32, 32 }, { 48, 40 } };
	uint8_t *before[6];
	int ops[6];
	for (int i = 0; i < 6; i++) {
		before[i] = sh_pixels(no[i], wh[i][0], wh[i][1], "SH10 before the save");
		ops[i] = sh_ops(no[i]);
	}
	PE_UpdateComponent(0);
	struct page *saved = NULL;
	SH_CHECK(PE_Save(&saved) && saved, "SH10: PE_Save failed");
	if (saved) {
		SH_CHECK(ca_save_version(saved) == 13, "SH10: the save is XPE v%d, want 13", ca_save_version(saved));
		SH_CHECK(PE_Load(&saved), "SH10: PE_Load failed");
		PE_UpdateComponent(0);
		ca_free_buffer(saved);
	}
	for (int i = 0; i < 6; i++) {
		char what[64];
		snprintf(what, sizeof what, "SH10 %s after PE_Load", d[i].name);
		uint8_t *after = sh_pixels(no[i], wh[i][0], wh[i][1], what);
		if (before[i] && after)
			sh_compare(after, wh[i][0], wh[i][1], before[i], what);
		SH_CHECK(sh_ops(no[i]) == ops[i], "%s: %d operations, want %d", what, sh_ops(no[i]), ops[i]);
		free(after);
		free(before[i]);
	}
	printf("SH10 checked\n");
}

/* --- SH11 --- */
static void sh_guards_load(const char *act_name, bool v14, bool grid, const char *what)
{
	static const struct sh_def d[] = {
		{ "Caution", SH_N(sh_caution) }, { "Shadow", SH_N(sh_shadow) }, { "SlotBase", SH_N(sh_slot) },
		{ "Hole2", SH_N(sh_hole2) }, { "NodeBase", SH_N(sh_node) }, { "NewMark", SH_N(sh_newmark) },
		{ "Vanguard", SH_N(sh_vanguard) },
	};
	enum { N = sizeof(d) / sizeof(d[0]) };
	int no[N];
	if (!sh_load(act_name, N, d, no))
		return;
	for (int i = 0; i < N; i++) {
		char label[96];
		snprintf(label, sizeof label, "SH11 %s, %s", what, d[i].name);
		const int w = d[i].steps[0].dest[4], h = d[i].steps[0].dest[5];
		const bool text = i < 2;
		if (v14 && (grid || !text)) {
			if (d[i].steps == sh_hole2) {
				/* Rounded: the model has no such polygon (SH6 compares it). */
				uint8_t *px = sh_pixels(no[i], w, h, label);
				sh_alpha(px, w, 1100, 345, 0, label);
				sh_alpha(px, w, 1100, 324, 128, label);
				free(px);
			} else {
				free(sh_expect(no[i], w, h, d[i].n, d[i].steps, label));
			}
			continue;
		}
		if (v14) {
			cn_expect_unbuilt(no[i], w, h, label);
			continue;
		}
		struct parts *p = parts_try_get(no[i]);
		const struct parts_state *st = p ? &p->states[0] : NULL;
		SH_CHECK(st && st->type != PARTS_CONSTRUCTION_PROCESS && !st->common.texture.handle && !st->common.w,
			 "%s: state type %d, texture %s, width %d; want it left unbuilt", label, st ? (int)st->type : -1,
			 st && st->common.texture.handle ? "present" : "missing", st ? st->common.w : -1);
	}
	sh_failures += cn_failures;
	cn_failures = 0;
}

static void sh_guards(void)
{
	bt_gl_setup(false);
	sh_guards_load("ShapesOffGrid", true, false, "off the glyph grid");
	ain->version = 13;
	sh_guards_load("ShapesV13", false, false, "before v14");
	printf("SH11 checked\n");
}

static void (*sh_case)(void);
static void sh_case_body(void)
{
	sh_case();
	sh_failures += bt_failures + cn_failures + cc_failures + ca_failures;
}

static int test_construction_shapes(void)
{
	static const struct { const char *name; void (*fn)(void); } cases[] = {
		{ "SH1", sh_squares }, { "SH2", sh_slots }, { "SH3", sh_newmark_case }, { "SH4", sh_caution_case },
		{ "SH5", sh_vanguard_case }, { "SH6", sh_holes }, { "SH7", sh_shadow_case }, { "SH8", sh_unbuilt },
		{ "SH9", sh_nothing }, { "SH10", sh_save }, { "SH11", sh_guards },
	};
	const int n = sizeof(cases) / sizeof(cases[0]);
	int failed = 0, env = 0;
	for (int i = 0; i < n; i++) {
		sh_case = cases[i].fn;
		int r = cn_fork_case(cases[i].name, sh_case_body, &sh_failures);
		failed += r == 1;
		env += r == 2;
	}
	if (env) {
		printf("CONSTRUCTION-SHAPES ENVIRONMENT FAILURE: %d of %d cases could not run, %d failed\n", env, n, failed);
		return 2;
	}
	printf("CONSTRUCTION-SHAPES %s: %d of %d cases failed\n", failed ? "FAIL" : "PASS", failed, n);
	return failed ? 1 : 0;
}
