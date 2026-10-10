/* construction-cmds: the construction commands the dungeon map is built from,
 * as the original runs them (static disassembly only):
 *   - 25 / 26 アルファマップグラデーション (0x4ff740 / 0x4ff8d0): over 先矩形
 *     X,Y,W,H (全體 = the surface), row i (25) or column i (26) of n gets
 *     a_i = A1 + (i * (A2 - A1)) / n (idiv, towards zero; A1 = 色１.A, A2 =
 *     色２.A) and 0x5ac8d0 multiplies: A' = A * a_i / 255 (0x5aca40);
 *   - 27 / 28 横 / 縦ブラー (0x4ffa60 / 0x4ffba0 -> 0x493790 / 0x493b10): a
 *     sliding box of 2r + 1 pixels over the rectangle's rows / columns, r =
 *     ブラー clamped to [1, n/2 - 1], the ends repeated beyond the edges, RGB
 *     only; for the last r pixels the original drops p[n - 2r + k] instead
 *     of p[n - 2r - 1 + k] (0x493a60, 0x493e01);
 *   - 52 円描画（アルファのみ）(0x502570 -> 0x4cb910): a ring from 半径 -
 *     線の幅 / 2 (at least 0) to 半径 + 線の幅 / 2 around (X + 0.5, Y + 0.5);
 *     the positions start at the centre minus the outer radius, the
 *     coverage (0x4e5f40) counts the subsamples between the two radii;
 *   - 102 円塗りつぶし（アルファのみ）(0x507640 -> 0x4cfb40): the pixels
 *     trunc(X + 0.5 - r + k), k = 0..2r, on both axes; the coverage
 *     (0x4e6530) counts the 8x8 subsamples (k - r + i/8, ...) within r,
 *     scaled to n * 255 / 64; the alpha becomes A * cov / 255 (0x5b0a10);
 *   - 106 円塗りつぶし（アルファブレンド）(0x507d60): the same disc as
 *     (R, G, B, A * cov / 255) on a transparent surface, composed by
 *     0x5acc00 -> 0x494420: c += ((src - c) * sa) >> 8, a = max(a, sa);
 *   - 97 多角形塗りつぶし（アルファブレンド）(0x506610 -> 0x5af7d0 ->
 *     0x4d0820): ArrayPos's polygon, vertices at pixel centres (0x4e9400),
 *     8 sub-rows per row (0x4d09c0), each pair of crossings (x1, x2) adding
 *     the span [x1, x2 + 1) in 255ths of a column (0x4d0a80), composed like
 *     106;
 *   - 129 ＣＧ並べてコピー (0x50a220): the CG copied over the rectangle in
 *     steps of its own size.
 * PartsEngine.AddPartsConstructionProcess reads them from ArrayInt as
 * 0x59a730 copies it: [0] command, [6] [7] [10] [11] 先矩形 X,Y,W,H, [12..15]
 * 色１, [16..19] 色２, [30] 全體, [31] ブラー, [32] 半径, [34] 線の幅, [36]
 * 丸め, [37] 回転; ArrayPos is x0, y0, x1, y1, ...
 * Before the fix 25 / 26 replaced the alpha, 27 / 28 did nothing, 52, 97,
 * 102 and 106 added no operation, and the pactex loader built no state with
 * 25, 26, 102 or 129.
 *
 * Cases (each in its own fork with the construction fixture's GL setup):
 *   CC1 gradations: 26 over the whole surface, 26 over a rectangle of an
 *       alpha-100 base (multiplied, not replaced), 25 over rows, through the
 *       AIN declaration; BattleInformationText's two fades through the
 *       pactex loader, and a gradation over no column or row, after which
 *       the next step is still done. Hand-computed alphas and every pixel
 *       against a model.
 *   CC2 disc 102: MapNodeView's base (64x64, r 24) through the pactex loader
 *       and the same through the AIN declaration; r 1 against hand-computed
 *       coverages (the disc is not symmetric: its centre is a pixel corner);
 *       a disc cut by the left and top edges; r 0 draws nothing.
 *   CC3 disc 106 through the AIN declaration: composed on a transparent and
 *       on an opaque base; discs cut by the edges, 102 beside 106, among
 *       them a centre at -(r + 1), whose last row or column is the position
 *       -0.5 and lands on pixel 0 (one pixel, checked by hand).
 *   CC4 polygon 97 through the AIN declaration: a rectangle's coverage
 *       computed by hand (the half columns at both ends, 4 of 8 sub-rows on
 *       the first row and 5 of 8 on the last, the column after the last
 *       vertex), a slanted bar's sum, the composition on black, a run of
 *       horizontal edges that folds back (one row by hand), and what adds
 *       nothing (丸め, a rotation, one vertex) or draws nothing (two).
 *   CC5 blurs 27 / 28 through the AIN declaration: hand-computed rows that
 *       show the last pixel's sum, the radius clamps, a rectangle, alpha
 *       untouched.
 *   CC6 PE_Save / PE_Load rebuild the same pixels from the new operations.
 *   CC7 what stays unbuilt: pactex steps with 色２ or 半徑 missing, a
 *       gradation beyond the surface, a CG that does not exist (129), and
 *       the AIN commands 29 and 129 with such a CG add no operation.
 *   CC8 the tiling itself (parts_cp_tile, by dlsym): a 3x2 tile over a
 *       rectangle, cut at its right and bottom edges and by the surface;
 *       rectangles INT_MAX wide and high, from far outside, and a tile
 *       larger than the surface.
 *   CC9 ring 52 through the AIN declaration: r 2 with width 1 against
 *       hand-computed coverages (the hole in the middle, the lopsided ends
 *       of the centre row), a width that leaves no hole, a ring cut by the
 *       edges, r 0; every pixel against the model.
 *   CC10 numbers at the ends of int in every command (far outside, sizes
 *       that are not positive, INT_MAX wide): nothing outside the surface
 *       is touched and the result is that of the clipped rectangle; a save
 *       with a circle beyond this build's limit loads it without a radius.
 *
 * The polygon fills that write (2026-10-11; before, the pactex loader built
 * no state with them):
 *   - 94 多角形塗りつぶし（ピクセル＆アルファ）(0x506210 -> 0x5af7d0 ->
 *     0x5afa40): the scan of 97 straight on the surface, its writer
 *     (vtable 0x80ecec, 0x4d13a0 -> 0x5b0b80) replacing every covered pixel
 *     by (R, G, B, A * cov / 255), each clamped to 0..255; nothing is
 *     blended;
 *   - 95 多角形塗りつぶし（アルファのみ）(0x506330 -> 0x5af720 -> 0x5af970):
 *     the same scan, the writer (vtable 0x80ecb4, 0x4d1420 -> 0x5b0a10)
 *     replacing the alpha by A * cov / 255 and leaving the colour.
 *   The pactex step keeps its vertices in 座標總覽 (0x4f9410, key 0x7d2e3c:
 *   x0, y0, x1, y1, ...; step +0xe4), and the handlers read 丸め's second
 *   item (+0xd5) and 旋轉角度 (+0xd8); a state with either is not built.
 *   CC11 the two commands through the pactex loader beside 97 through the
 *        AIN declaration, the same rectangle (CC4's coverages) and colour on
 *        an opaque coloured base: replaced, alpha only, blended; an alpha
 *        of 1, whose covered pixels are written with alpha 0; the three
 *        states of the game: WorkResultScreen's ResultBase (rows 0 and 135
 *        by hand, every pixel against a model for convex polygons),
 *        SceneSendResult's ProfBase (two vertices one past the surface; row
 *        64 by hand) and BattleResultPlayerView's NewSkillBase (95 among
 *        the commands 5 and 122; the triangle's edges on rows 40, 43 and
 *        47 by hand); the SJIS keys.
 *   CC12 what stays unbuilt (rounded corners, a rotation, two vertices,
 *        also as five ints, a float among the points, the colour or 丸め,
 *        no list, a polygon outside the surface, a repeated vertex that
 *        leaves two, an alpha whose product does not fit 32 bits) and what
 *        is built as the original runs it: the first item of 丸め, 丸め or
 *        旋轉角度 missing, any colour with 95, an odd number of ints (the
 *        last is dropped), a colour and an alpha outside 0..255 (clamped
 *        where they are written, the alpha after its division), no 色１
 *        (transparent black), an angle that is a multiple of 360.
 *   CC13 PE_Save writes XPE version 11, in which the two operations are
 *        the types 27 and 28; a save without the new operations loads as
 *        version 10, 9, 8 and 7 wrote it; the three states come back with
 *        the same pixels and operations.
 *   CC14 under an AIN before version 14 the loader builds neither command
 *        and a save is version 3.
 * Functions and types the fix adds are reached through the AIN declaration,
 * the pactex loader or dlsym, so the file builds before the fix.
 */
#include <dlfcn.h>

static int cc_failures;
#define CC_CHECK(c, ...) do { if (!(c)) { printf("  FAIL: "); printf(__VA_ARGS__); printf("\n"); cc_failures++; } } while (0)

#define CC_COLOR2 "\xc9\xab\xa3\xb2"					/* 色２ */

/* --- pactex steps --- */
struct cc_step {
	int cmd;
	int dest[6];	/* 先矩形 */
	int color[4];	/* 色１ */
	int color2[4];	/* 色２ */
	int full;	/* 全體 */
	int radius[2];	/* 半徑 */
	const char *cg;	/* ＣＧ名 */
	int omit;	/* CC_OMIT_* */
};
enum { CC_OMIT_COLOR2 = 1, CC_OMIT_RADIUS = 2 };
#define CC_CREATE(w, h) { 0, { 0, 0, 0, 0, w, h } }
#define CC_FILL(cmd, x, y, w, h, r, g, b, a) { cmd, { x, y, 0, 0, w, h }, { r, g, b, a } }
#define CC_FILL_ALL(cmd, r, g, b, a) { cmd, { 0 }, { r, g, b, a }, { 0 }, 1 }
#define CC_GRAD(cmd, x, y, w, h, a1, a2, full) { cmd, { x, y, 0, 0, w, h }, { 0, 0, 0, a1 }, { 0, 0, 0, a2 }, full }
#define CC_CIRCLE(cmd, x, y, r, a) { cmd, { x, y }, { 0, 0, 0, a }, { 0 }, 0, { r, r } }
#define CC_N(steps) (int)(sizeof(steps) / sizeof((steps)[0])), steps

static struct ex_tree cc_parts(const char *name, int n, const struct cc_step *steps)
{
	struct lb_place pl = { 0, 0, 1 };
	struct ex_tree info = bu_branch(BU_INFO, 2);
	info.children[0] = bu_sleaf(BU_TYPE, BU_LOW);
	info.children[1] = bu_branch(BU_NORMAL, 3);
	struct ex_tree *st = &info.children[1];
	st->children[0] = bu_sleaf(BU_TYPE, BR_CONS);
	st->children[1] = lb_ilist(CN_AREA, 4, (int[]){ 0, 0, 0, 0 });
	st->children[2] = bu_branch(BR_STEPS, n);
	for (int i = 0; i < n; i++) {
		const struct cc_step *s = &steps[i];
		char key[32];
		snprintf(key, sizeof key, "Step%d", i + 1);
		unsigned k = 0;
		struct ex_tree t = bu_branch(key, 9);
		t.children[k++] = bu_ileaf(BR_COMMAND, s->cmd);
		t.children[k++] = lb_ilist(LB_DEST, 6, s->dest);
		t.children[k++] = lb_ilist(CN_COLOR1, 4, s->color);
		if (!(s->omit & CC_OMIT_COLOR2))
			t.children[k++] = lb_ilist(CC_COLOR2, 4, s->color2);
		t.children[k++] = bu_ileaf(CN_FULL, s->full);
		if (!(s->omit & CC_OMIT_RADIUS))
			t.children[k++] = lb_ilist(CN_RADIUS, 2, s->radius);
		t.children[k++] = lb_ilist(CN_ARC, 2, (int[]){ 0, 0 });
		t.children[k++] = bu_ileaf(CN_ROTATE, 0);
		t.children[k++] = bu_sleaf(BR_CG_NAME, s->cg ? s->cg : "");
		t.nr_children = k;
		st->children[2].children[i] = t;
	}
	return lb_node(name, &pl, info, 0);
}

/* --- the AIN declaration --- */
struct cc_cmd {
	int cmd;
	int x, y, w, h;
	int c1[4], c2[4];
	int full, blur, radius, round, angle, width;
	const char *cg;
	int nr_pos;
	const int *pos;
};

static int cc_int_array(int n, const int *v)
{
	struct page *a = alloc_page(ARRAY_PAGE, AIN_ARRAY_INT, n);
	for (int i = 0; i < n; i++)
		a->values[i].i = v[i];
	int slot = heap_alloc_slot(VM_PAGE);
	heap_set_page(slot, a);
	return slot;
}

/* PartsEngine.AddPartsConstructionProcess(Number, ArrayInt, ArrayFloat,
 * ArrayString, ArrayPos, State) for the normal state. */
static void cc_hll(int no, const struct cc_cmd *c)
{
	int v[40] = { 0 };
	v[0] = c->cmd;
	v[6] = c->x; v[7] = c->y; v[10] = c->w; v[11] = c->h;
	for (int i = 0; i < 4; i++) {
		v[12 + i] = c->c1[i];
		v[16 + i] = c->c2[i];
	}
	v[30] = c->full; v[31] = c->blur; v[32] = c->radius; v[33] = c->radius; v[34] = c->width;
	v[36] = c->round; v[37] = c->angle;
	int ints = cc_int_array(40, v);
	struct page *fa = alloc_page(ARRAY_PAGE, AIN_ARRAY_FLOAT, 2);
	int floats = heap_alloc_slot(VM_PAGE);
	heap_set_page(floats, fa);
	struct page *sa = alloc_page(ARRAY_PAGE, AIN_ARRAY_STRING, 2);
	sa->values[0].i = sf_str("", -1);
	sa->values[1].i = sf_str(c->cg ? c->cg : "", -1);
	int strs = heap_alloc_slot(VM_PAGE);
	heap_set_page(strs, sa);
	int pos = cc_int_array(c->nr_pos * 2, c->pos);
	ri_hll("AddPartsConstructionProcess", 6, (union vm_value[]){
		RI_I(no), RI_I(ints), RI_I(floats), RI_I(strs), RI_I(pos), RI_I(1) });
}

/* A w x h surface filled with (r, g, b, a): commands 0 and 6. */
static void cc_new(int no, int w, int h, int r, int g, int b, int a)
{
	parts_get(no);
	cc_hll(no, &(struct cc_cmd){ .cmd = 0, .w = w, .h = h });
	cc_hll(no, &(struct cc_cmd){ .cmd = 6, .full = 1, .c1 = { r, g, b, a } });
}
/* One pixel's colour: command 3. */
static void cc_dot(int no, int x, int y, int v)
{
	cc_hll(no, &(struct cc_cmd){ .cmd = 3, .x = x, .y = y, .w = 1, .h = 1, .c1 = { v, v, v, 0 } });
}
static int cc_nr_ops(int no)
{
	struct parts *p = parts_try_get(no);
	return p && p->states[0].type == PARTS_CONSTRUCTION_PROCESS ? cn_nr_ops(p) : -1;
}

/* The built pixels of parts `no`, w x h (to be freed). */
static uint8_t *cc_pixels(int no, int w, int h, const char *what)
{
	PE_BuildPartsConstructionProcess(no, 1);
	struct parts *p = parts_try_get(no);
	struct parts_state *s = p ? &p->states[0] : NULL;
	bool built = s && s->type == PARTS_CONSTRUCTION_PROCESS && s->common.texture.handle
		&& s->common.texture.w == w && s->common.texture.h == h;
	CC_CHECK(built, "%s: not a built %dx%d construction", what, w, h);
	return built ? gfx_get_pixels(&s->common.texture) : NULL;
}

/* `got` is NULL when the state was not built: cn_pixels reports that to the
 * construction fixture's counter, so it is counted here. */
static void cc_compare(uint8_t *got, int w, int h, const uint8_t *model, const char *what)
{
	if (!got) {
		CC_CHECK(0, "%s: no pixels to compare", what);
		return;
	}
	int bad = 0;
	for (int i = 0; i < w * h && bad < 4; i++) {
		if (memcmp(got + i * 4, model + i * 4, 4)) {
			CC_CHECK(0, "%s (%d,%d) = (%d,%d,%d,%d), want (%d,%d,%d,%d)", what, i % w, i / w,
				 got[i * 4], got[i * 4 + 1], got[i * 4 + 2], got[i * 4 + 3],
				 model[i * 4], model[i * 4 + 1], model[i * 4 + 2], model[i * 4 + 3]);
			bad++;
		}
	}
	free(got);
}
static void cc_expect_all(int no, int w, int h, const uint8_t *model, const char *what)
{
	cc_compare(cc_pixels(no, w, h, what), w, h, model, what);
}
/* The alpha of row y, columns x0.. against `want` (n values). */
static void cc_expect_alpha_row(const uint8_t *got, int w, int y, int x0, int n, const int *want, const char *what)
{
	for (int i = 0; got && i < n; i++) {
		int a = got[(y * w + x0 + i) * 4 + 3];
		CC_CHECK(a == want[i], "%s: alpha (%d,%d) = %d, want %d", what, x0 + i, y, a, want[i]);
	}
}

/* --- the models, from the disassembly --- */
static int cc_grad_a(int a1, int a2, int i, int n) { return a1 + (i * (a2 - a1)) / n; }
static int cc_mul255(int a, int b) { return (a * b) / 255; }

static void cc_fill_model(uint8_t *px, int w, int h, int r, int g, int b, int a)
{
	for (int i = 0; i < w * h; i++) {
		px[i * 4] = r; px[i * 4 + 1] = g; px[i * 4 + 2] = b; px[i * 4 + 3] = a;
	}
}

static int cc_circle_cov(int dx, int dy, int r)
{
	int n = 0;
	for (int j = 0; j < 8; j++) {
		for (int i = 0; i < 8; i++) {
			double x = dx + i / 8.0, y = dy + j / 8.0;
			if (x * x + y * y <= (double)r * r)
				n++;
		}
	}
	return n ? n * 255 / 64 : 0;
}

/* Walks the disc's cells as 0x4cfb40 does and calls fn(pixel, coverage). */
static void cc_circle_walk(int X, int Y, int r, int w, int h, void (*fn)(uint8_t *, int), uint8_t *px)
{
	if (r <= 0)
		return;
	for (int ky = 0; ky <= 2 * r; ky++) {
		for (int kx = 0; kx <= 2 * r; kx++) {
			double x = X + 0.5 - r + kx, y = Y + 0.5 - r + ky;
			if (x <= -1.0 || y <= -1.0)
				continue;
			int ix = (int)x, iy = (int)y;	/* truncated towards zero */
			int cov = cc_circle_cov(kx - r, ky - r, r);
			if (!cov || ix >= w || iy >= h)
				continue;
			fn(px + (iy * w + ix) * 4, cov);
		}
	}
}

static int cc_model_a;
static int cc_model_rgb[3];
static void cc_write_alpha(uint8_t *p, int cov) { p[3] = cc_mul255(cc_model_a, cov); }
static void cc_stage(uint8_t *p, int cov) { p[3] = cc_mul255(cc_model_a, cov); }
/* 0x494420 with the staged alphas of `staged` (same size as px). */
static void cc_compose_all(uint8_t *px, const uint8_t *staged, int w, int h)
{
	for (int i = 0; i < w * h; i++) {
		int sa = staged[i * 4 + 3];
		uint8_t *p = px + i * 4;
		for (int c = 0; c < 3; c++) {
			int d = (cc_model_rgb[c] - p[c]) * sa;
			p[c] = p[c] + (d >= 0 ? d / 256 : -((-d + 255) / 256));
		}
		if (sa > p[3])
			p[3] = sa;
	}
}

/* One line as 0x493790 / 0x493b10 sum it. */
static void cc_blur_line(uint8_t *out, const uint8_t *in, int n, int step, int r)
{
	for (int c = 0; c < 3; c++) {
		int sum = in[c] * r;
		for (int j = 0; j <= r; j++)
			sum += in[j * step + c];
		out[c] = sum / (2 * r + 1);
		for (int i = 1; i < n; i++) {
			int add, drop;
			if (i <= r) { add = r + i; drop = 0; }
			else if (i < n - r) { add = i + r; drop = i - r - 1; }
			else { add = n - 1; drop = n - 2 * r + (i - (n - r)); }
			sum += in[add * step + c] - in[drop * step + c];
			out[i * step + c] = sum / (2 * r + 1);
		}
	}
}
static void cc_blur_model(uint8_t *px, int w, int h, int x0, int y0, int bw, int bh, int radius, bool vertical)
{
	int n = vertical ? bh : bw;
	int r = radius;
	if (r < 1) r = 1;
	else if (r >= n / 2) r = n / 2 - 1;
	if (r < 1 || 2 * r + 1 > n)
		return;
	uint8_t *src = malloc((size_t)w * h * 4);
	memcpy(src, px, (size_t)w * h * 4);
	if (vertical) {
		for (int x = x0; x < x0 + bw; x++)
			cc_blur_line(px + (y0 * w + x) * 4, src + (y0 * w + x) * 4, bh, w * 4, r);
	} else {
		for (int y = y0; y < y0 + bh; y++)
			cc_blur_line(px + (y * w + x0) * 4, src + (y * w + x0) * 4, bw, 4, r);
	}
	free(src);
}

/* --- CC1 --- */
static const struct cc_step cc_fades[] = { CC_CREATE(30, 4), CC_GRAD(26, 0, 0, 8, 4, 0, 255, 0),
	CC_GRAD(26, 22, 0, 8, 4, 255, 0, 0), CC_FILL(3, 0, 0, 30, 1, 255, 255, 255, 0) };
static const struct cc_step cc_rows[] = { CC_CREATE(8, 4), CC_FILL_ALL(6, 9, 8, 7, 200), CC_GRAD(25, 0, 0, 0, 0, 255, 0, 1) };
/* Nothing to go over: the original's loop does not run, the command succeeds
 * (0x4ff973 -> 0x4ff9ac, 0x4ff7df -> 0x4ff812) and the step after it is done. */
static const struct cc_step cc_no_cols[] = { CC_CREATE(8, 4), CC_FILL_ALL(6, 9, 8, 7, 200), CC_GRAD(26, 2, 0, 0, 4, 0, 255, 0),
	CC_FILL(3, 0, 0, 8, 1, 255, 255, 255, 0) };
static const struct cc_step cc_no_rows[] = { CC_CREATE(8, 4), CC_FILL_ALL(6, 9, 8, 7, 200), CC_GRAD(25, 0, 1, 8, -3, 0, 255, 0),
	CC_FILL(3, 0, 0, 8, 1, 255, 255, 255, 0) };

static void cc_gradations(void)
{
	cn_gl_setup();
	ri_postlink();
	const int COLS = 9401, RECT = 9402, ROWS = 9403;
	uint8_t model[30 * 4 * 4];

	/* 26 over everything, 255 -> 0 in 8 columns: 255 - (i * 255) / 8. */
	cc_new(COLS, 8, 4, 255, 255, 255, 255);
	cc_hll(COLS, &(struct cc_cmd){ .cmd = 26, .full = 1, .c1 = { 0, 0, 0, 255 }, .c2 = { 0, 0, 0, 0 } });
	uint8_t *got = cc_pixels(COLS, 8, 4, "CC1 26 whole");
	cc_expect_alpha_row(got, 8, 2, 0, 8, (int[]){ 255, 224, 192, 160, 128, 96, 64, 32 }, "CC1 26 whole");
	cc_fill_model(model, 8, 4, 255, 255, 255, 255);
	for (int i = 0; i < 32; i++)
		model[i * 4 + 3] = cc_mul255(255, cc_grad_a(255, 0, i % 8, 8));
	cc_compare(got, 8, 4, model, "CC1 26 whole");

	/* 26 over (2,1) 4x2 of an alpha-100 base, 0 -> 255: multiplied. */
	cc_new(RECT, 8, 4, 40, 50, 60, 100);
	cc_hll(RECT, &(struct cc_cmd){ .cmd = 26, .x = 2, .y = 1, .w = 4, .h = 2, .c1 = { 0, 0, 0, 0 }, .c2 = { 0, 0, 0, 255 } });
	got = cc_pixels(RECT, 8, 4, "CC1 26 rectangle");
	/* a = 0, 63, 127, 191 -> 100 * a / 255 */
	cc_expect_alpha_row(got, 8, 1, 0, 8, (int[]){ 100, 100, 0, 24, 49, 74, 100, 100 }, "CC1 26 rectangle");
	cc_expect_alpha_row(got, 8, 3, 0, 8, (int[]){ 100, 100, 100, 100, 100, 100, 100, 100 }, "CC1 26 rectangle, row below");
	cc_fill_model(model, 8, 4, 40, 50, 60, 100);
	for (int y = 1; y < 3; y++)
		for (int x = 2; x < 6; x++)
			model[(y * 8 + x) * 4 + 3] = cc_mul255(100, cc_grad_a(0, 255, x - 2, 4));
	cc_compare(got, 8, 4, model, "CC1 26 rectangle");

	/* 25 over rows, 100 -> 200 in 4 rows: 100, 125, 150, 175. */
	cc_new(ROWS, 8, 4, 1, 2, 3, 255);
	cc_hll(ROWS, &(struct cc_cmd){ .cmd = 25, .full = 1, .c1 = { 0, 0, 0, 100 }, .c2 = { 0, 0, 0, 200 } });
	got = cc_pixels(ROWS, 8, 4, "CC1 25 whole");
	for (int y = 0; got && y < 4; y++)
		CC_CHECK(got[(y * 8 + 5) * 4 + 3] == 100 + 25 * y && got[(y * 8 + 5) * 4] == 1,
			 "CC1 25: (5,%d) = (%d,.,.,%d), want (1,.,.,%d)", y, got[(y * 8 + 5) * 4], got[(y * 8 + 5) * 4 + 3], 100 + 25 * y);
	free(got);

	/* The pactex loader: BattleInformationText's base (a fade at each end,
	 * then a white line), and rows over everything. */
	struct ex_tree *kids;
	struct ex *e = lb_activity("ConstructionGrad", 4, &kids);
	kids->children[0] = cc_parts("Fades", CC_N(cc_fades));
	kids->children[1] = cc_parts("Rows", CC_N(cc_rows));
	kids->children[2] = cc_parts("NoColumns", CC_N(cc_no_cols));
	kids->children[3] = cc_parts("NoRows", CC_N(cc_no_rows));
	int act = cn_load("ConstructionGrad", e);
	int fades = cn_no(act, "Fades"), rows = cn_no(act, "Rows");
	int no_cols = cn_no(act, "NoColumns"), no_rows = cn_no(act, "NoRows");
	heap_unref(act);
	cc_fill_model(model, 30, 4, 0, 0, 0, 255);
	for (int y = 0; y < 4; y++) {
		for (int x = 0; x < 8; x++) {
			model[(y * 30 + x) * 4 + 3] = cc_mul255(255, cc_grad_a(0, 255, x, 8));
			model[(y * 30 + 22 + x) * 4 + 3] = cc_mul255(255, cc_grad_a(255, 0, x, 8));
		}
	}
	for (int x = 0; x < 30; x++)
		model[x * 4] = model[x * 4 + 1] = model[x * 4 + 2] = 255;
	CC_CHECK(model[(30 + 0) * 4 + 3] == 0 && model[(30 + 4) * 4 + 3] == 127 && model[(30 + 8) * 4 + 3] == 255
		 && model[(30 + 22) * 4 + 3] == 255 && model[(30 + 29) * 4 + 3] == 32,
		 "the model itself: columns 0/4/8/22/29 = %d/%d/%d/%d/%d", model[(30 + 0) * 4 + 3], model[(30 + 4) * 4 + 3],
		 model[(30 + 8) * 4 + 3], model[(30 + 22) * 4 + 3], model[(30 + 29) * 4 + 3]);
	cc_compare(cn_pixels(fades, 30, 4, "CC1 pactex fades"), 30, 4, model, "CC1 pactex fades");
	cc_fill_model(model, 8, 4, 9, 8, 7, 200);
	for (int i = 0; i < 32; i++)
		model[i * 4 + 3] = cc_mul255(200, cc_grad_a(255, 0, i / 8, 4));
	cc_compare(cn_pixels(rows, 8, 4, "CC1 pactex rows"), 8, 4, model, "CC1 pactex rows");
	/* A gradation over no column or no row: built, the alpha untouched, the
	 * white line of the step after it drawn. */
	cc_fill_model(model, 8, 4, 9, 8, 7, 200);
	for (int x = 0; x < 8; x++)
		model[x * 4] = model[x * 4 + 1] = model[x * 4 + 2] = 255;
	cc_compare(cn_pixels(no_cols, 8, 4, "CC1 pactex 26 over no column"), 8, 4, model, "CC1 pactex 26 over no column");
	cc_compare(cn_pixels(no_rows, 8, 4, "CC1 pactex 25 over no row"), 8, 4, model, "CC1 pactex 25 over no row");
	cc_failures += cn_failures;
	printf("CC1 checked\n");
}

/* --- CC2 --- */
static const struct cc_step cc_circ[] = { CC_CREATE(64, 64), CC_FILL_ALL(6, 255, 255, 255, 0), CC_CIRCLE(102, 32, 32, 24, 255) };
static const struct cc_step cc_circ_edge[] = { CC_CREATE(16, 16), CC_FILL_ALL(6, 0, 0, 0, 7), CC_CIRCLE(102, 2, 2, 4, 128) };
static const struct cc_step cc_circ_none[] = { CC_CREATE(8, 8), CC_FILL_ALL(6, 1, 2, 3, 9), CC_CIRCLE(102, 4, 4, 0, 255) };

static void cc_circles(void)
{
	cn_gl_setup();
	ri_postlink();
	struct ex_tree *kids;
	struct ex *e = lb_activity("ConstructionCircle", 3, &kids);
	kids->children[0] = cc_parts("Circle", CC_N(cc_circ));
	kids->children[1] = cc_parts("Edge", CC_N(cc_circ_edge));
	kids->children[2] = cc_parts("None", CC_N(cc_circ_none));
	int act = cn_load("ConstructionCircle", e);
	int circle = cn_no(act, "Circle"), edge = cn_no(act, "Edge"), none = cn_no(act, "None");
	heap_unref(act);
	uint8_t *model = malloc(64 * 64 * 4);
	cc_fill_model(model, 64, 64, 255, 255, 255, 0);
	cc_model_a = 255;
	cc_circle_walk(32, 32, 24, 64, 64, cc_write_alpha, model);
	/* The centre row: column 8's cell starts at -24, where only one
	 * subsample of its first column lies within 24 (57 of 64); columns
	 * 9..55 are inside; 56's cell starts at +24 and holds one subsample. */
	CC_CHECK(model[(32 * 64 + 32) * 4 + 3] == 255 && model[(32 * 64 + 7) * 4 + 3] == 0 && model[(32 * 64 + 8) * 4 + 3] == 227
		 && model[(32 * 64 + 9) * 4 + 3] == 255
		 && model[(32 * 64 + 55) * 4 + 3] == 255 && model[(32 * 64 + 56) * 4 + 3] == 3 && model[(32 * 64 + 57) * 4 + 3] == 0,
		 "the model itself: row 32, columns 7/8/55/56/57 = %d/%d/%d/%d/%d", model[(32 * 64 + 7) * 4 + 3],
		 model[(32 * 64 + 8) * 4 + 3], model[(32 * 64 + 55) * 4 + 3], model[(32 * 64 + 56) * 4 + 3], model[(32 * 64 + 57) * 4 + 3]);
	cc_compare(cn_pixels(circle, 64, 64, "CC2 pactex r 24"), 64, 64, model, "CC2 pactex r 24");
	/* The same through the AIN declaration. */
	const int A24 = 9411, R1 = 9412;
	cc_new(A24, 64, 64, 255, 255, 255, 0);
	cc_hll(A24, &(struct cc_cmd){ .cmd = 102, .x = 32, .y = 32, .radius = 24, .c1 = { 0, 0, 0, 255 } });
	cc_expect_all(A24, 64, 64, model, "CC2 AIN r 24");

	cc_fill_model(model, 16, 16, 0, 0, 0, 7);
	cc_model_a = 128;
	cc_circle_walk(2, 2, 4, 16, 16, cc_write_alpha, model);
	cc_compare(cn_pixels(edge, 16, 16, "CC2 cut by the edges"), 16, 16, model, "CC2 cut by the edges, A 128");
	cc_fill_model(model, 8, 8, 1, 2, 3, 9);
	cc_compare(cn_pixels(none, 8, 8, "CC2 r 0"), 8, 8, model, "CC2 r 0");

	/* r 1 around (4,4), by hand: the cells' corners are -1, 0 and 1 from the
	 * centre, the subsamples k/8 right of and below them. 41, 49 and 56 of
	 * 64 lie within 1 of the centre in the upper left four, one (the corner
	 * itself) in the cells right of and below the centre. */
	cc_new(R1, 8, 8, 0, 0, 0, 7);
	cc_hll(R1, &(struct cc_cmd){ .cmd = 102, .x = 4, .y = 4, .radius = 1, .c1 = { 0, 0, 0, 255 } });
	uint8_t *got = cc_pixels(R1, 8, 8, "CC2 r 1");
	cc_expect_alpha_row(got, 8, 2, 2, 5, (int[]){ 7, 7, 7, 7, 7 }, "CC2 r 1");
	cc_expect_alpha_row(got, 8, 3, 2, 5, (int[]){ 7, 163, 195, 7, 7 }, "CC2 r 1");
	cc_expect_alpha_row(got, 8, 4, 2, 5, (int[]){ 7, 195, 223, 3, 7 }, "CC2 r 1");
	cc_expect_alpha_row(got, 8, 5, 2, 5, (int[]){ 7, 7, 3, 7, 7 }, "CC2 r 1");
	free(got);
	/* A 999 is scaled by the coverage first and clamped after (0x5b0a10):
	 * the single-subsample cells get 999 * 3 / 255 = 11, not 3. */
	const int R1A = 9413;
	cc_new(R1A, 8, 8, 0, 0, 0, 7);
	cc_hll(R1A, &(struct cc_cmd){ .cmd = 102, .x = 4, .y = 4, .radius = 1, .c1 = { 0, 0, 0, 999 } });
	got = cc_pixels(R1A, 8, 8, "CC2 r 1, A 999");
	cc_expect_alpha_row(got, 8, 3, 2, 5, (int[]){ 7, 255, 255, 7, 7 }, "CC2 r 1, A 999");
	cc_expect_alpha_row(got, 8, 4, 2, 5, (int[]){ 7, 255, 255, 11, 7 }, "CC2 r 1, A 999");
	cc_expect_alpha_row(got, 8, 5, 2, 5, (int[]){ 7, 7, 11, 7, 7 }, "CC2 r 1, A 999");
	free(got);
	free(model);
	cc_failures += cn_failures;
	printf("CC2 checked\n");
}

/* --- CC3 --- */
static void cc_circle_blend(void)
{
	cn_gl_setup();
	ri_postlink();
	const int WHITE = 9421, HALF = 9422;
	uint8_t model[16 * 16 * 4], staged[16 * 16 * 4];
	cc_new(WHITE, 16, 16, 10, 20, 30, 0);
	cc_hll(WHITE, &(struct cc_cmd){ .cmd = 106, .x = 8, .y = 8, .radius = 5, .c1 = { 255, 255, 255, 255 } });
	cc_fill_model(model, 16, 16, 10, 20, 30, 0);
	memset(staged, 0, sizeof staged);
	cc_model_a = 255; cc_model_rgb[0] = cc_model_rgb[1] = cc_model_rgb[2] = 255;
	cc_circle_walk(8, 8, 5, 16, 16, cc_stage, staged);
	cc_compose_all(model, staged, 16, 16);
	/* 10 + ((255 - 10) * 255 >> 8) = 254 */
	CC_CHECK(model[(8 * 16 + 8) * 4] == 254 && model[(8 * 16 + 8) * 4 + 3] == 255 && model[0] == 10 && model[3] == 0,
		 "the model itself: centre (%d,.,.,%d), corner (%d,.,.,%d)", model[(8 * 16 + 8) * 4], model[(8 * 16 + 8) * 4 + 3], model[0], model[3]);
	cc_expect_all(WHITE, 16, 16, model, "CC3 white on transparent");

	cc_new(HALF, 16, 16, 200, 100, 0, 255);
	cc_hll(HALF, &(struct cc_cmd){ .cmd = 106, .x = 8, .y = 8, .radius = 5, .c1 = { 0, 0, 255, 128 } });
	cc_fill_model(model, 16, 16, 200, 100, 0, 255);
	memset(staged, 0, sizeof staged);
	cc_model_a = 128; cc_model_rgb[0] = 0; cc_model_rgb[1] = 0; cc_model_rgb[2] = 255;
	cc_circle_walk(8, 8, 5, 16, 16, cc_stage, staged);
	cc_compose_all(model, staged, 16, 16);
	/* 200 + ((0 - 200) * 128 >> 8) = 100, 100 - 50, 0 + (255 * 128 >> 8) = 127 */
	const uint8_t *c = model + (8 * 16 + 8) * 4;
	CC_CHECK(c[0] == 100 && c[1] == 50 && c[2] == 127 && c[3] == 255, "the model itself: centre (%d,%d,%d,%d)", c[0], c[1], c[2], c[3]);
	cc_expect_all(HALF, 16, 16, model, "CC3 blue A 128 on opaque");

	/* Cut by the top and the left edge. The position -0.5 is pixel 0
	 * (truncated towards zero, 0x4cfc39), one more than the position below
	 * it. With the centre at -(r + 1) the disc's last row or column is that
	 * position and has one subsample on the radius: one pixel of row 0 or
	 * column 0 with A * 3 / 255. (The staged buffer was one pixel short for
	 * it: a write beyond it for the row, a pixel left out for the column.) */
	static const struct { int x, y, r, px, py; } edge[] = {
		{ 5, -6, 5, 5, 0 }, { -6, 5, 5, 0, 5 }, { 5, -2, 1, 5, 0 }, { -2, 5, 1, 0, 5 }, { -9, -9, 8, -1, -1 },
		{ 5, -25, 24, 5, 0 }, { -25, 5, 24, 0, 5 }, { 0, 0, 3, -1, -1 }, { -1, -1, 6, -1, -1 }, { 15, 15, 4, -1, -1 },
		{ 5, -7, 5, -1, -1 }, { -7, 5, 5, -1, -1 }, { 16, 5, 5, -1, -1 }, { 5, 21, 5, -1, -1 },
	};
	for (int i = 0; i < (int)(sizeof edge / sizeof edge[0]); i++) {
		char what[64];
		snprintf(what, sizeof what, "CC3 106 centre (%d,%d) r %d", edge[i].x, edge[i].y, edge[i].r);
		for (int cmd = 102; cmd <= 106; cmd += 4) {
			const int no = 9423 + i * 2 + (cmd == 106);
			cc_new(no, 16, 16, 200, 100, 0, 0);
			cc_hll(no, &(struct cc_cmd){ .cmd = cmd, .x = edge[i].x, .y = edge[i].y, .radius = edge[i].r, .c1 = { 0, 0, 255, 255 } });
			cc_fill_model(model, 16, 16, 200, 100, 0, 0);
			cc_model_a = 255; cc_model_rgb[0] = 0; cc_model_rgb[1] = 0; cc_model_rgb[2] = 255;
			if (cmd == 106) {
				memset(staged, 0, sizeof staged);
				cc_circle_walk(edge[i].x, edge[i].y, edge[i].r, 16, 16, cc_stage, staged);
				cc_compose_all(model, staged, 16, 16);
			} else {
				cc_circle_walk(edge[i].x, edge[i].y, edge[i].r, 16, 16, cc_write_alpha, model);
			}
			if (edge[i].px >= 0) {
				/* The one pixel, and nothing else, by hand. */
				uint8_t hand[16 * 16 * 4];
				cc_fill_model(hand, 16, 16, 200, 100, 0, 0);
				uint8_t *h = hand + (edge[i].py * 16 + edge[i].px) * 4;
				h[3] = 3;
				if (cmd == 106) {	/* 200 + (-200 * 3 >> 8), 100 + (-100 * 3 >> 8), 0 + (255 * 3 >> 8) */
					h[0] = 197; h[1] = 98; h[2] = 2;
				}
				CC_CHECK(!memcmp(hand, model, sizeof hand), "%s, command %d: the model itself is not the one pixel (%d,%d,%d,%d) at (%d,%d)",
					 what, cmd, h[0], h[1], h[2], h[3], edge[i].px, edge[i].py);
			}
			cc_expect_all(no, 16, 16, model, what);
		}
	}
	printf("CC3 checked\n");
}

/* --- CC4 --- */
static void cc_polygon(void)
{
	cn_gl_setup();
	ri_postlink();
	const int RECT = 9431, BAR = 9432, OVER = 9433, NONE = 9434;
	static const int rect[] = { 2, 2, 11, 2, 11, 7, 2, 7 };
	/* The vertices are (2.5,2.5) .. (11.5,7.5). A sub-row inside adds
	 * 255 - 127 to column 2, 255 to columns 3..11 and 127 to column 12
	 * (the span [2.5, 12.5)); row 2 has the sub-rows 2.5 .. 2.875 (4 of 8),
	 * rows 3..6 all 8, row 7 the sub-rows 7.0 .. 7.5 (5 of 8). */
	cc_new(RECT, 20, 10, 255, 255, 255, 0);
	cc_hll(RECT, &(struct cc_cmd){ .cmd = 97, .c1 = { 255, 255, 255, 255 }, .nr_pos = 4, .pos = rect });
	CC_CHECK(cc_nr_ops(RECT) == 3, "the polygon added no operation (%d operations)", cc_nr_ops(RECT));
	uint8_t *got = cc_pixels(RECT, 20, 10, "CC4 rectangle");
	for (int y = 0; y < 10; y++) {
		int edge = 0, in = 0;
		if (y == 2) { edge = 63; in = 127; }
		else if (y >= 3 && y <= 6) { edge = 127; in = 255; }
		else if (y == 7) { edge = 79; in = 159; }
		int want[20] = { 0 };
		want[2] = want[12] = edge;
		for (int x = 3; x <= 11; x++)
			want[x] = in;
		cc_expect_alpha_row(got, 20, y, 0, 20, want, "CC4 rectangle");
	}
	CC_CHECK(!got || (got[(4 * 20 + 5) * 4] == 255 && got[(4 * 20 + 5) * 4 + 1] == 255), "CC4 rectangle: white on white changed the colour");
	free(got);

	/* A slanted bar (8,25) (48,5) (52,15) (12,35), area 480 over 31 rows:
	 * every sub-row's span is one column longer, so the coverage sums to
	 * about (480 + 31) * 255. */
	static const int bar[] = { 8, 25, 48, 5, 52, 15, 12, 35 };
	cc_new(BAR, 64, 40, 255, 255, 255, 0);
	cc_hll(BAR, &(struct cc_cmd){ .cmd = 97, .c1 = { 255, 255, 255, 255 }, .nr_pos = 4, .pos = bar });
	got = cc_pixels(BAR, 64, 40, "CC4 slanted bar");
	if (got) {
		long sum = 0;
		for (int i = 0; i < 64 * 40; i++)
			sum += got[i * 4 + 3];
		CC_CHECK(labs(sum - 511L * 255) <= 6 * 255, "CC4 slanted bar: coverage sum %ld (%.1f pixels), want about 511", sum, sum / 255.0);
		CC_CHECK(got[(20 * 64 + 30) * 4 + 3] == 255 && got[(5 * 64 + 10) * 4 + 3] == 0 && got[(36 * 64 + 12) * 4 + 3] == 0
			 && got[(4 * 64 + 48) * 4 + 3] == 0,
			 "CC4 slanted bar: (30,20) %d want 255, (10,5) %d, (12,36) %d and (48,4) %d want 0", got[(20 * 64 + 30) * 4 + 3],
			 got[(5 * 64 + 10) * 4 + 3], got[(36 * 64 + 12) * 4 + 3], got[(4 * 64 + 48) * 4 + 3]);
		free(got);
	}

	/* Composed on opaque black: 255 * 255 >> 8 = 254 and 186 * 255 >> 8 =
	 * 185 inside; the edge columns' coverage 127 gives 126 and 92. */
	cc_new(OVER, 20, 10, 0, 0, 0, 255);
	cc_hll(OVER, &(struct cc_cmd){ .cmd = 97, .c1 = { 255, 0, 186, 255 }, .nr_pos = 4, .pos = rect });
	got = cc_pixels(OVER, 20, 10, "CC4 on black");
	if (got) {
		const uint8_t *p = got + (4 * 20 + 5) * 4, *q = got + (4 * 20 + 2) * 4, *o = got + (4 * 20 + 14) * 4;
		CC_CHECK(p[0] == 254 && p[1] == 0 && p[2] == 185 && p[3] == 255, "CC4 on black: inside (%d,%d,%d,%d), want (254,0,185,255)", p[0], p[1], p[2], p[3]);
		CC_CHECK(q[0] == 126 && q[1] == 0 && q[2] == 92 && q[3] == 255, "CC4 on black: edge (%d,%d,%d,%d), want (126,0,92,255)", q[0], q[1], q[2], q[3]);
		CC_CHECK(o[0] == 0 && o[2] == 0 && o[3] == 255, "CC4 on black: outside (%d,%d,%d,%d), want (0,0,0,255)", o[0], o[1], o[2], o[3]);
		free(got);
	}

	/* A run of two horizontal edges that folds back, (5,2) (0,2) (10,2),
	 * then down to (10,8) and (0,8): on the sub-row y = 2.5 the run gives
	 * its smallest x, 0.5 (the vertex after it, (10.5,8.5), is not right of
	 * its largest; 0x4d0f10), next to 5.5 from the closing edge and 10.5
	 * from the right one. Sorted, the first two pair up and the third is
	 * left over: that sub-row covers [0.5, 6.5). The three sub-rows below
	 * it in row 2 pair the closing edge (5.396, 5.292, 5.188) with 10.5.
	 * Sums in 255ths: column 0: 127; 1..4: 255; 5: 255 + 154 + 180 + 207;
	 * 6: 127 + 3 * 255; 7..10: 3 * 255; 11: 3 * 127; coverage sum / 8.
	 * (Without the rule for the run that sub-row would cover [5.5, 11.5)
	 * and the row would be 0 0 0 0 0 83 127 127 127 127 127 63.) */
	static const int fold[] = { 5, 2, 0, 2, 10, 2, 10, 8, 0, 8 };
	const int FOLD = 9435;
	cc_new(FOLD, 16, 10, 255, 255, 255, 0);
	cc_hll(FOLD, &(struct cc_cmd){ .cmd = 97, .c1 = { 255, 255, 255, 255 }, .nr_pos = 5, .pos = fold });
	got = cc_pixels(FOLD, 16, 10, "CC4 folded run");
	cc_expect_alpha_row(got, 16, 2, 0, 16, (int[]){ 15, 31, 31, 31, 31, 99, 111, 95, 95, 95, 95, 47, 0, 0, 0, 0 }, "CC4 folded run");
	cc_expect_alpha_row(got, 16, 1, 0, 16, (int[]){ 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 }, "CC4 folded run");
	free(got);

	/* 丸め, a rotation and a single vertex add nothing; two vertices add an
	 * operation that draws nothing. */
	cc_new(NONE, 20, 10, 5, 6, 7, 8);
	cc_hll(NONE, &(struct cc_cmd){ .cmd = 97, .c1 = { 255, 255, 255, 255 }, .round = 1, .nr_pos = 4, .pos = rect });
	cc_hll(NONE, &(struct cc_cmd){ .cmd = 97, .c1 = { 255, 255, 255, 255 }, .angle = 30, .nr_pos = 4, .pos = rect });
	cc_hll(NONE, &(struct cc_cmd){ .cmd = 97, .c1 = { 255, 255, 255, 255 }, .nr_pos = 1, .pos = rect });
	CC_CHECK(cc_nr_ops(NONE) == 2, "CC4: %d operations after the unsupported polygons, want 2", cc_nr_ops(NONE));
	cc_hll(NONE, &(struct cc_cmd){ .cmd = 97, .c1 = { 255, 255, 255, 255 }, .nr_pos = 2, .pos = rect });
	uint8_t model[20 * 10 * 4];
	cc_fill_model(model, 20, 10, 5, 6, 7, 8);
	cc_expect_all(NONE, 20, 10, model, "CC4 two vertices");
	printf("CC4 checked\n");
}

/* --- CC5 --- */
static void cc_blurs(void)
{
	cn_gl_setup();
	ri_postlink();
	const int H1 = 9441, H10 = 9442, V1 = 9443, HR = 9444, SHORT = 9445, ZERO = 9446;
	uint8_t model[8 * 2 * 4];

	/* 0 0 0 0 0 0 90 30 with r 1: 30 and 40 are plain box sums; the last
	 * pixel is (p5 + p7 + p7) / 3 = 20, where a box that repeats the end
	 * would give (90 + 30 + 30) / 3 = 50. */
	cc_new(H1, 8, 1, 0, 0, 0, 200);
	cc_dot(H1, 6, 0, 90); cc_dot(H1, 7, 0, 30);
	cc_hll(H1, &(struct cc_cmd){ .cmd = 27, .full = 1, .blur = 1 });
	uint8_t *got = cc_pixels(H1, 8, 1, "CC5 27 r 1");
	for (int x = 0; got && x < 8; x++) {
		static const int want[8] = { 0, 0, 0, 0, 0, 30, 40, 20 };
		const uint8_t *p = got + x * 4;
		CC_CHECK(p[0] == want[x] && p[1] == want[x] && p[2] == want[x] && p[3] == 200,
			 "CC5 27 r 1: (%d) = (%d,%d,%d,%d), want (%d,.,.,200)", x, p[0], p[1], p[2], p[3], want[x]);
	}
	free(got);

	/* ブラー 10 on 8 pixels is r 3; against the model. */
	cc_new(H10, 8, 1, 255, 255, 255, 200);
	cc_dot(H10, 4, 0, 0); cc_dot(H10, 7, 0, 100);
	cc_hll(H10, &(struct cc_cmd){ .cmd = 27, .full = 1, .blur = 10 });
	cc_fill_model(model, 8, 1, 255, 255, 255, 200);
	model[4 * 4] = model[4 * 4 + 1] = model[4 * 4 + 2] = 0;
	model[7 * 4] = model[7 * 4 + 1] = model[7 * 4 + 2] = 100;
	cc_blur_model(model, 8, 1, 0, 0, 8, 1, 10, false);
	/* Pixel 0's window is p0 four times and p1..p3; those of pixels 1..3
	 * hold the black pixel once. */
	CC_CHECK(model[0] == 255 && model[1 * 4] == 218 && model[3 * 4] == 218,
		 "the model itself: (0) %d (1) %d (3) %d", model[0], model[4], model[12]);
	cc_expect_all(H10, 8, 1, model, "CC5 27 r 10 clamped to 3");

	/* 28 down a column, the same numbers as the first row. */
	cc_new(V1, 1, 8, 0, 0, 0, 200);
	cc_dot(V1, 0, 6, 90); cc_dot(V1, 0, 7, 30);
	cc_hll(V1, &(struct cc_cmd){ .cmd = 28, .full = 1, .blur = 1 });
	got = cc_pixels(V1, 1, 8, "CC5 28 r 1");
	for (int y = 0; got && y < 8; y++) {
		static const int want[8] = { 0, 0, 0, 0, 0, 30, 40, 20 };
		CC_CHECK(got[y * 4] == want[y] && got[y * 4 + 3] == 200, "CC5 28 r 1: (%d) = (%d,.,.,%d), want (%d,.,.,200)",
			 y, got[y * 4], got[y * 4 + 3], want[y]);
	}
	free(got);

	/* 27 over the rectangle (2,0) 4x1 of 8x2: its ends repeat, the row
	 * below and the columns beside it stay. */
	cc_new(HR, 8, 2, 255, 255, 255, 200);
	cc_dot(HR, 4, 0, 0); cc_dot(HR, 4, 1, 0); cc_dot(HR, 1, 0, 9);
	cc_hll(HR, &(struct cc_cmd){ .cmd = 27, .x = 2, .y = 0, .w = 4, .h = 1, .blur = 1 });
	cc_fill_model(model, 8, 2, 255, 255, 255, 200);
	for (int y = 0; y < 2; y++)
		model[(y * 8 + 4) * 4] = model[(y * 8 + 4) * 4 + 1] = model[(y * 8 + 4) * 4 + 2] = 0;
	model[1 * 4] = model[1 * 4 + 1] = model[1 * 4 + 2] = 9;
	cc_blur_model(model, 8, 2, 2, 0, 4, 1, 1, false);
	CC_CHECK(model[2 * 4] == 255 && model[3 * 4] == 170 && model[4 * 4] == 170 && model[1 * 4] == 9 && model[(8 + 4) * 4] == 0,
		 "the model itself: %d %d %d, beside %d, below %d", model[8], model[12], model[16], model[4], model[(8 + 4) * 4]);
	cc_expect_all(HR, 8, 2, model, "CC5 27 over a rectangle");

	/* Three pixels allow no radius (3 / 2 - 1 = 0): unchanged. ブラー 0 is 1. */
	cc_new(SHORT, 3, 1, 0, 0, 0, 255);
	cc_dot(SHORT, 1, 0, 90);
	cc_hll(SHORT, &(struct cc_cmd){ .cmd = 27, .full = 1, .blur = 5 });
	got = cc_pixels(SHORT, 3, 1, "CC5 three pixels");
	CC_CHECK(!got || (got[0] == 0 && got[4] == 90 && got[8] == 0), "CC5 three pixels: %d %d %d, want 0 90 0", got[0], got[4], got[8]);
	free(got);
	cc_new(ZERO, 8, 1, 0, 0, 0, 200);
	cc_dot(ZERO, 6, 0, 90); cc_dot(ZERO, 7, 0, 30);
	cc_hll(ZERO, &(struct cc_cmd){ .cmd = 27, .full = 1, .blur = 0 });
	got = cc_pixels(ZERO, 8, 1, "CC5 27 r 0");
	CC_CHECK(!got || (got[5 * 4] == 30 && got[6 * 4] == 40 && got[7 * 4] == 20), "CC5 27 r 0: %d %d %d, want 30 40 20 (r 1)",
		 got[5 * 4], got[6 * 4], got[7 * 4]);
	free(got);
	printf("CC5 checked\n");
}

/* --- CC6 --- */
static void cc_save(void)
{
	cn_gl_setup();
	ri_postlink();
	const int MIX = 9451;
	static const int tri[] = { 1, 1, 14, 3, 6, 13 };
	cc_new(MIX, 16, 16, 255, 255, 255, 255);
	cc_hll(MIX, &(struct cc_cmd){ .cmd = 26, .full = 1, .c1 = { 0, 0, 0, 255 }, .c2 = { 0, 0, 0, 40 } });
	cc_hll(MIX, &(struct cc_cmd){ .cmd = 25, .x = 0, .y = 8, .w = 16, .h = 8, .c1 = { 0, 0, 0, 255 }, .c2 = { 0, 0, 0, 100 } });
	cc_hll(MIX, &(struct cc_cmd){ .cmd = 102, .x = 8, .y = 8, .radius = 5, .c1 = { 0, 0, 0, 200 } });
	cc_hll(MIX, &(struct cc_cmd){ .cmd = 106, .x = 4, .y = 4, .radius = 3, .c1 = { 255, 0, 0, 255 } });
	cc_hll(MIX, &(struct cc_cmd){ .cmd = 97, .c1 = { 0, 90, 200, 180 }, .nr_pos = 3, .pos = tri });
	cc_hll(MIX, &(struct cc_cmd){ .cmd = 27, .full = 1, .blur = 2 });
	cc_hll(MIX, &(struct cc_cmd){ .cmd = 28, .x = 2, .y = 2, .w = 9, .h = 11, .blur = 1 });
	cc_hll(MIX, &(struct cc_cmd){ .cmd = 52, .x = 11, .y = 11, .radius = 3, .width = 2, .c1 = { 0, 0, 0, 90 } });
	CC_CHECK(cc_nr_ops(MIX) == 10, "%d operations before the save, want 10", cc_nr_ops(MIX));
	uint8_t *before = cc_pixels(MIX, 16, 16, "CC6 before");
	if (!before)
		return;
	/* Not a flat surface: the operations did something. */
	CC_CHECK(memcmp(before, before + 5 * 16 * 4 + 20, 4) != 0, "CC6: the surface looks flat");
	PE_UpdateComponent(0);
	struct page *saved = NULL;
	CC_CHECK(PE_Save(&saved), "PE_Save failed");
	if (!saved) {
		free(before);
		return;
	}
	CC_CHECK(ca_save_version(saved) >= 8, "a save with the new operations is XPE v%d, want 8 or later", ca_save_version(saved));
	/* PE_Load releases every parts and builds them again from the page. */
	CC_CHECK(PE_Load(&saved), "PE_Load failed");
	PE_UpdateComponent(0);
	CC_CHECK(cc_nr_ops(MIX) == 10, "after PE_Load: %d operations, want 10", cc_nr_ops(MIX));
	struct parts *p = parts_try_get(MIX);
	bool built = p && p->states[0].type == PARTS_CONSTRUCTION_PROCESS && p->states[0].common.texture.handle
		&& p->states[0].common.texture.w == 16 && p->states[0].common.texture.h == 16;
	CC_CHECK(built, "after PE_Load the parts has no 16x16 surface");
	if (built) {
		uint8_t *after = gfx_get_pixels(&p->states[0].common.texture);
		CC_CHECK(!memcmp(before, after, 16 * 16 * 4), "the pixels differ after PE_Save / PE_Load");
		free(after);
	}
	free(before);
	printf("CC6 checked\n");
}

/* --- CC7 --- */
static const struct cc_step cc_no_color2[] = { CC_CREATE(8, 4), { 25, { 0 }, { 0, 0, 0, 255 }, { 0 }, 1, { 0 }, NULL, CC_OMIT_COLOR2 } };
static const struct cc_step cc_no_radius[] = { CC_CREATE(8, 4), { 102, { 4, 2 }, { 0, 0, 0, 255 }, { 0 }, 0, { 0 }, NULL, CC_OMIT_RADIUS } };
static const struct cc_step cc_beyond[] = { CC_CREATE(8, 4), CC_GRAD(26, 4, 0, 8, 4, 0, 255, 0) };
static const struct cc_step cc_no_cg[] = { CC_CREATE(8, 4), { 129, { 0 }, { 0 }, { 0 }, 1, { 0 }, "NoSuchCG_g12" } };
static const struct cc_step cc_big_radius[] = { CC_CREATE(8, 4), CC_CIRCLE(102, 4, 2, 5000, 255) };
static const struct cc_step cc_alpha_range[] = { CC_CREATE(8, 4), CC_GRAD(26, 0, 0, 8, 4, 0, 256, 0) };

static void cc_unbuilt(void)
{
	cn_gl_setup();
	ri_postlink();
	struct ex_tree *kids;
	struct ex *e = lb_activity("ConstructionCmdUnbuilt", 6, &kids);
	const char *names[] = { "NoColor2", "NoRadius", "Beyond", "NoCG", "BigRadius", "AlphaRange" };
	kids->children[0] = cc_parts(names[0], CC_N(cc_no_color2));
	kids->children[1] = cc_parts(names[1], CC_N(cc_no_radius));
	kids->children[2] = cc_parts(names[2], CC_N(cc_beyond));
	kids->children[3] = cc_parts(names[3], CC_N(cc_no_cg));
	kids->children[4] = cc_parts(names[4], CC_N(cc_big_radius));
	kids->children[5] = cc_parts(names[5], CC_N(cc_alpha_range));
	int act = cn_load("ConstructionCmdUnbuilt", e);
	for (int i = 0; i < 6; i++) {
		int no = cn_no(act, names[i]);
		struct parts *p = no > 0 ? parts_try_get(no) : NULL;
		if (!p)
			continue;
		CC_CHECK(p->states[0].type != PARTS_CONSTRUCTION_PROCESS && !p->states[0].common.texture.handle,
			 "%s was built (state type %d)", names[i], p->states[0].type);
		int type = BU_PE("GetComponentType", BU_I(no), BU_I(1)).i;
		CC_CHECK(type == 26, "%s: GetComponentType %d, want 26", names[i], type);
		CC_CHECK(p->pactex_canvas_w == 8 && p->pactex_canvas_h == 4, "%s: canvas %dx%d, want 8x4",
			 names[i], p->pactex_canvas_w, p->pactex_canvas_h);
	}
	heap_unref(act);

	/* The AIN commands that name a CG add nothing for one that does not
	 * exist; a gradation that leaves the surface stops at its first line
	 * outside (the columns before it are done). */
	const int CG = 9461, OUT = 9462;
	cc_new(CG, 8, 4, 1, 2, 3, 4);
	cc_hll(CG, &(struct cc_cmd){ .cmd = 29, .cg = "NoSuchCG_g12" });
	cc_hll(CG, &(struct cc_cmd){ .cmd = 129, .full = 1, .cg = "NoSuchCG_g12" });
	CC_CHECK(cc_nr_ops(CG) == 2, "%d operations after 29 and 129 with a missing CG, want 2", cc_nr_ops(CG));
	cc_new(OUT, 8, 4, 0, 0, 0, 255);
	cc_hll(OUT, &(struct cc_cmd){ .cmd = 26, .x = 6, .y = 0, .w = 4, .h = 4, .c1 = { 0, 0, 0, 0 }, .c2 = { 0, 0, 0, 200 } });
	uint8_t *got = cc_pixels(OUT, 8, 4, "CC7 gradation beyond the surface");
	/* columns 6 and 7 are lines 0 and 1 of 4: 0 and 50 */
	cc_expect_alpha_row(got, 8, 1, 4, 4, (int[]){ 255, 255, 0, 50 }, "CC7 gradation beyond the surface");
	free(got);
	cc_failures += cn_failures;
	printf("CC7 checked\n");
}

/* --- CC8 --- */
typedef void (*cc_tile_fn)(uint8_t *, int, int, int, int, int, int, const uint8_t *, int, int);
static void cc_tiles(void)
{
	cc_tile_fn tile = (cc_tile_fn)dlsym(RTLD_DEFAULT, "parts_cp_tile");
	CC_CHECK(tile, "parts_cp_tile is missing");
	if (!tile)
		return;
	/* A 3x2 tile whose pixels name their position. */
	uint8_t src[3 * 2 * 4];
	for (int i = 0; i < 6; i++) {
		src[i * 4] = 100 + i % 3; src[i * 4 + 1] = 200 + i / 3; src[i * 4 + 2] = 50; src[i * 4 + 3] = 10 * (i + 1);
	}
	uint8_t px[7 * 5 * 4], want[7 * 5 * 4];
	/* Over (1,1) 5x3: columns 1..5 repeat 0 1 2 0 1, rows 1..3 repeat 0 1 0. */
	cc_fill_model(px, 7, 5, 1, 2, 3, 4);
	cc_fill_model(want, 7, 5, 1, 2, 3, 4);
	for (int y = 1; y < 4; y++)
		for (int x = 1; x < 6; x++)
			memcpy(want + (y * 7 + x) * 4, src + (((y - 1) % 2) * 3 + (x - 1) % 3) * 4, 4);
	tile(px, 7, 5, 1, 1, 5, 3, src, 3, 2);
	CC_CHECK(!memcmp(px, want, sizeof px), "a 3x2 tile over (1,1) 5x3 of 7x5 is not the expected pattern");
	CC_CHECK(px[(1 * 7 + 1) * 4] == 100 && px[(1 * 7 + 4) * 4] == 100 && px[(2 * 7 + 5) * 4 + 3] == 50 && px[(3 * 7 + 3) * 4 + 3] == 30
		 && px[(4 * 7 + 1) * 4] == 1 && px[(1 * 7 + 6) * 4] == 1,
		 "tile pixels: (1,1) %d (4,1) %d, alpha (5,2) %d (3,3) %d, outside %d %d", px[(1 * 7 + 1) * 4], px[(1 * 7 + 4) * 4],
		 px[(2 * 7 + 5) * 4 + 3], px[(3 * 7 + 3) * 4 + 3], px[(4 * 7 + 1) * 4], px[(1 * 7 + 6) * 4]);
	/* The whole surface, as 全體 gives it. */
	for (int y = 0; y < 5; y++)
		for (int x = 0; x < 7; x++)
			memcpy(want + (y * 7 + x) * 4, src + ((y % 2) * 3 + x % 3) * 4, 4);
	tile(px, 7, 5, 0, 0, 7, 5, src, 3, 2);
	CC_CHECK(!memcmp(px, want, sizeof px), "a 3x2 tile over all of 7x5 is not the expected pattern");
	/* A rectangle that starts left of and above the surface and ends beyond
	 * it: the tiles keep their phase from the rectangle's corner. */
	cc_fill_model(px, 7, 5, 1, 2, 3, 4);
	for (int y = 0; y < 5; y++)
		for (int x = 0; x < 7; x++)
			memcpy(want + (y * 7 + x) * 4, src + (((y + 1) % 2) * 3 + (x + 2) % 3) * 4, 4);
	tile(px, 7, 5, -2, -1, 20, 20, src, 3, 2);
	CC_CHECK(!memcmp(px, want, sizeof px), "a rectangle from (-2,-1) does not tile from its own corner");
	/* An empty rectangle and an empty tile draw nothing. */
	cc_fill_model(px, 7, 5, 1, 2, 3, 4);
	cc_fill_model(want, 7, 5, 1, 2, 3, 4);
	tile(px, 7, 5, 1, 1, 0, 3, src, 3, 2);
	tile(px, 7, 5, 1, 1, 5, -3, src, 3, 2);
	tile(px, 7, 5, 1, 1, 5, 3, src, 0, 2);
	CC_CHECK(!memcmp(px, want, sizeof px), "an empty rectangle or tile drew something");
	/* A size far beyond the surface is cut to it before anything is copied
	 * (the loop went over every tile of the rectangle; a script's or a
	 * save's width and height have no limit). Same pattern as 20x20. */
	for (int y = 0; y < 5; y++)
		for (int x = 0; x < 7; x++)
			memcpy(want + (y * 7 + x) * 4, src + (((y + 1) % 2) * 3 + (x + 2) % 3) * 4, 4);
	tile(px, 7, 5, -2, -1, INT_MAX, INT_MAX, src, 3, 2);
	CC_CHECK(!memcmp(px, want, sizeof px), "a rectangle from (-2,-1) of INT_MAX x INT_MAX does not tile like 20x20");
	/* From far left and above, reaching the surface's first 4 columns and
	 * 3 rows: x + w = 4, y + h = 3; the phase is ((X - x) mod 3, (Y - y) mod 2)
	 * with -x = 2147483643 = 0 (mod 3) and -y = 2147483644 = 0 (mod 2). */
	cc_fill_model(px, 7, 5, 1, 2, 3, 4);
	cc_fill_model(want, 7, 5, 1, 2, 3, 4);
	for (int y = 0; y < 3; y++)
		for (int x = 0; x < 4; x++)
			memcpy(want + (y * 7 + x) * 4, src + ((y % 2) * 3 + x % 3) * 4, 4);
	tile(px, 7, 5, -2147483643, -2147483644, INT_MAX, INT_MAX, src, 3, 2);
	CC_CHECK(!memcmp(px, want, sizeof px), "a rectangle from (-2147483643,-2147483644) of INT_MAX x INT_MAX is not the 4x3 corner");
	/* Nothing of these reaches the surface. */
	cc_fill_model(px, 7, 5, 1, 2, 3, 4);
	cc_fill_model(want, 7, 5, 1, 2, 3, 4);
	tile(px, 7, 5, INT_MIN, INT_MIN, INT_MAX, INT_MAX, src, 3, 2);
	tile(px, 7, 5, INT_MAX, 0, INT_MAX, INT_MAX, src, 3, 2);
	tile(px, 7, 5, 0, INT_MAX, INT_MAX, INT_MAX, src, 3, 2);
	tile(px, 7, 5, 7, 0, INT_MAX, 5, src, 3, 2);
	tile(px, 7, 5, 0, 5, 7, INT_MAX, src, 3, 2);
	tile(px, 7, 5, 0, 0, INT_MIN, INT_MIN, src, 3, 2);
	CC_CHECK(!memcmp(px, want, sizeof px), "a rectangle outside the surface drew something");
	/* A tile larger than the surface: its top-left part. */
	uint8_t big[9 * 6 * 4];
	for (int i = 0; i < 9 * 6; i++) {
		big[i * 4] = i; big[i * 4 + 1] = 2 * i; big[i * 4 + 2] = 7; big[i * 4 + 3] = 255 - i;
	}
	for (int y = 0; y < 5; y++)
		memcpy(want + y * 7 * 4, big + y * 9 * 4, 7 * 4);
	tile(px, 7, 5, 0, 0, INT_MAX, INT_MAX, big, 9, 6);
	CC_CHECK(!memcmp(px, want, sizeof px), "a 9x6 tile over a 7x5 surface is not its top-left part");
	printf("CC8 checked\n");
}

/* --- CC9 --- */

/* The ring's cells as 0x4cb910 walks them: positions from centre - outer in
 * steps of 1, the coverage of the subsamples between the two radii. */
static void cc_ring_walk(int X, int Y, int r, int width, int a, int w, int h, uint8_t *px)
{
	if (r <= 0)
		return;
	const double half = width * 0.5, outer = r + half;
	double inner = r - half;
	if (inner < 0.0)
		inner = 0.0;
	for (double y = Y + 0.5 - outer; y <= Y + 0.5 + outer; y += 1.0) {
		for (double x = X + 0.5 - outer; x <= X + 0.5 + outer; x += 1.0) {
			int n = 0;
			for (int j = 0; j < 8; j++) {
				for (int i = 0; i < 8; i++) {
					const double dx = x - (X + 0.5) + i / 8.0, dy = y - (Y + 0.5) + j / 8.0;
					const double d2 = dx * dx + dy * dy;
					if (inner * inner <= d2 && d2 <= outer * outer)
						n++;
				}
			}
			if (!n || x <= -1.0 || y <= -1.0 || (int)x >= w || (int)y >= h)
				continue;
			px[((int)y * w + (int)x) * 4 + 3] = cc_mul255(a, n * 255 / 64);
		}
	}
}

static void cc_rings(void)
{
	cn_gl_setup();
	ri_postlink();
	const int R2 = 9471, WIDE = 9472, EDGE = 9473, NONE = 9474;
	uint8_t model[12 * 12 * 4];

	/* r 2, width 1 around (4,4): between 1.5 and 2.5 from the corner of
	 * pixel (4,4); the cells start at -2.5, -1.5, ... from it. On the centre
	 * row (cells from -0.5 down): the cell from -2.5 has 57 of 64 (its first
	 * column lies on the outer radius), the one from -1.5 only its first
	 * column (8), the two around the centre nothing, the one from +1.5 all
	 * 64 and the one from +2.5 the single subsample at 2.5. */
	cc_new(R2, 8, 8, 0, 0, 0, 7);
	cc_hll(R2, &(struct cc_cmd){ .cmd = 52, .x = 4, .y = 4, .radius = 2, .width = 1, .c1 = { 0, 0, 0, 255 } });
	CC_CHECK(cc_nr_ops(R2) == 3, "command 52 added no operation (%d operations)", cc_nr_ops(R2));
	uint8_t *got = cc_pixels(R2, 8, 8, "CC9 r 2, width 1");
	cc_expect_alpha_row(got, 8, 4, 0, 8, (int[]){ 7, 7, 227, 31, 7, 7, 255, 3 }, "CC9 r 2, width 1");
	cc_fill_model(model, 8, 8, 0, 0, 0, 7);
	cc_ring_walk(4, 4, 2, 1, 255, 8, 8, model);
	CC_CHECK(model[(4 * 8 + 2) * 4 + 3] == 227 && model[(4 * 8 + 3) * 4 + 3] == 31 && model[(4 * 8 + 4) * 4 + 3] == 7
		 && model[(4 * 8 + 6) * 4 + 3] == 255 && model[(4 * 8 + 7) * 4 + 3] == 3,
		 "the model itself: row 4 = %d %d %d . %d %d", model[(4 * 8 + 2) * 4 + 3], model[(4 * 8 + 3) * 4 + 3],
		 model[(4 * 8 + 4) * 4 + 3], model[(4 * 8 + 6) * 4 + 3], model[(4 * 8 + 7) * 4 + 3]);
	cc_compare(got, 8, 8, model, "CC9 r 2, width 1");

	/* r 1, width 4: the inner radius would be -1 and is 0, a disc of 3. */
	cc_new(WIDE, 12, 12, 9, 9, 9, 0);
	cc_hll(WIDE, &(struct cc_cmd){ .cmd = 52, .x = 6, .y = 6, .radius = 1, .width = 4, .c1 = { 0, 0, 0, 200 } });
	cc_fill_model(model, 12, 12, 9, 9, 9, 0);
	cc_ring_walk(6, 6, 1, 4, 200, 12, 12, model);
	CC_CHECK(model[(6 * 12 + 6) * 4 + 3] == 200 && model[(6 * 12 + 2) * 4 + 3] == 0 && model[(6 * 12 + 3) * 4 + 3] > 0,
		 "the model itself: centre %d, column 2 %d, column 3 %d", model[(6 * 12 + 6) * 4 + 3], model[(6 * 12 + 2) * 4 + 3],
		 model[(6 * 12 + 3) * 4 + 3]);
	cc_expect_all(WIDE, 12, 12, model, "CC9 r 1, width 4");

	/* An odd width puts the positions on whole numbers; cut by the edges. */
	cc_new(EDGE, 12, 12, 0, 0, 0, 5);
	cc_hll(EDGE, &(struct cc_cmd){ .cmd = 52, .x = 1, .y = 2, .radius = 4, .width = 3, .c1 = { 0, 0, 0, 255 } });
	cc_fill_model(model, 12, 12, 0, 0, 0, 5);
	cc_ring_walk(1, 2, 4, 3, 255, 12, 12, model);
	cc_expect_all(EDGE, 12, 12, model, "CC9 cut by the edges");

	/* r 0 draws nothing. */
	cc_new(NONE, 8, 8, 1, 2, 3, 9);
	cc_hll(NONE, &(struct cc_cmd){ .cmd = 52, .x = 4, .y = 4, .radius = 0, .width = 3, .c1 = { 0, 0, 0, 255 } });
	cc_fill_model(model, 8, 8, 1, 2, 3, 9);
	cc_expect_all(NONE, 8, 8, model, "CC9 r 0");
	printf("CC9 checked\n");
}

/* --- CC10 --- */
/* How many words of a save are `v`. */
static int cc_save_count(const struct page *saved, int v)
{
	int n = 0;
	for (int i = 0; saved && i < saved->nr_vars; i++)
		n += saved->values[i].i == v;
	return n;
}

/* Numbers a script or a save may hold: nothing is read or written outside
 * the surface and no loop runs over more than the surface (ASan, UBSan and
 * the case's alarm are the judges). */
static void cc_extremes(void)
{
	cn_gl_setup();
	ri_postlink();
	const int P_OUT = 9481, P_HUGE = 9482, P_MID = 9483, P_BLUR = 9484, P_ALL = 9485, P_GRAD = 9486, P_SAVE = 9487;
	static const int far_a[] = { INT_MIN, INT_MIN, INT_MIN + 5, INT_MIN, INT_MIN, INT_MIN + 5 };
	static const int far_b[] = { INT_MAX, INT_MAX, INT_MAX - 5, INT_MAX, INT_MAX, INT_MAX - 5 };
	static const int far_c[] = { INT_MIN, 3, INT_MIN + 9, 3, INT_MIN, 12 };
	static const int around_all[] = { INT_MIN, INT_MIN, INT_MAX, INT_MIN, INT_MAX, INT_MAX, INT_MIN, INT_MAX };
	static const int around[] = { -100, -100, 100, -100, 100, 100, -100, 100 };

	/* Everything far outside, and sizes that are not positive: nothing is
	 * drawn on a surface that is not flat (a red disc on grey). */
	cc_new(P_OUT, 16, 16, 90, 80, 70, 200);
	cc_hll(P_OUT, &(struct cc_cmd){ .cmd = 106, .x = 8, .y = 8, .radius = 5, .c1 = { 255, 0, 0, 255 } });
	uint8_t *before = cc_pixels(P_OUT, 16, 16, "CC10 before");
	const struct cc_cmd outside[] = {
		{ .cmd = 25, .x = INT_MIN, .y = INT_MIN, .w = INT_MAX, .h = INT_MAX, .c2 = { 0, 0, 0, 255 } },
		{ .cmd = 26, .x = INT_MAX, .y = INT_MAX, .w = INT_MAX, .h = INT_MAX, .c2 = { 0, 0, 0, 255 } },
		{ .cmd = 25, .w = INT_MIN, .h = INT_MIN, .c2 = { 0, 0, 0, 255 } },
		{ .cmd = 26, .w = INT_MIN, .h = INT_MIN, .c2 = { 0, 0, 0, 255 } },
		{ .cmd = 25, .x = 16, .w = INT_MAX, .h = INT_MAX, .c2 = { 0, 0, 0, 255 } },
		{ .cmd = 26, .y = 16, .w = INT_MAX, .h = INT_MAX, .c2 = { 0, 0, 0, 255 } },
		{ .cmd = 27, .x = INT_MIN, .w = INT_MAX, .h = INT_MAX, .blur = INT_MAX },
		{ .cmd = 28, .y = INT_MAX, .w = INT_MAX, .h = INT_MAX, .blur = INT_MAX },
		{ .cmd = 27, .x = 16, .w = INT_MAX, .h = 16, .blur = 3 },
		{ .cmd = 28, .w = INT_MIN, .h = INT_MIN, .blur = INT_MIN },
		{ .cmd = 102, .x = INT_MAX, .y = INT_MAX, .radius = 1024, .c1 = { 0, 0, 0, 255 } },
		{ .cmd = 102, .x = INT_MIN, .y = INT_MIN, .radius = 1024, .c1 = { 0, 0, 0, 255 } },
		{ .cmd = 106, .x = INT_MAX, .y = INT_MIN, .radius = 1024, .c1 = { 0, 255, 0, 255 } },
		{ .cmd = 106, .x = INT_MIN, .y = INT_MAX, .radius = 1024, .c1 = { 0, 255, 0, 255 } },
		{ .cmd = 102, .x = 8, .y = 8, .radius = INT_MIN, .c1 = { 0, 0, 0, 255 } },
		{ .cmd = 106, .x = 8, .y = 8, .radius = -1, .c1 = { 0, 255, 0, 255 } },
		{ .cmd = 106, .x = 8, .y = 8, .radius = INT_MAX, .c1 = { 0, 255, 0, 255 } },
		{ .cmd = 52, .x = INT_MAX, .y = INT_MAX, .radius = 1024, .width = 1024, .c1 = { 0, 0, 0, 255 } },
		{ .cmd = 52, .x = INT_MIN, .y = INT_MIN, .radius = 1024, .width = 1024, .c1 = { 0, 0, 0, 255 } },
		{ .cmd = 52, .x = 8, .y = 8, .radius = 3, .width = INT_MIN, .c1 = { 0, 0, 0, 255 } },
		{ .cmd = 52, .x = 8, .y = 8, .radius = 3, .width = INT_MAX, .c1 = { 0, 0, 0, 255 } },
		{ .cmd = 52, .x = 8, .y = 8, .radius = INT_MAX, .width = INT_MAX, .c1 = { 0, 0, 0, 255 } },
		{ .cmd = 97, .c1 = { 0, 255, 0, 255 }, .nr_pos = 3, .pos = far_a },
		{ .cmd = 97, .c1 = { 0, 255, 0, 255 }, .nr_pos = 3, .pos = far_b },
		{ .cmd = 97, .c1 = { 0, 255, 0, 255 }, .nr_pos = 3, .pos = far_c },
	};
	for (int i = 0; i < (int)(sizeof outside / sizeof outside[0]); i++)
		cc_hll(P_OUT, &outside[i]);
	CC_CHECK(cc_nr_ops(P_OUT) == 3 + (int)(sizeof outside / sizeof outside[0]), "CC10: %d operations, want %d", cc_nr_ops(P_OUT),
		 3 + (int)(sizeof outside / sizeof outside[0]));
	uint8_t *after = cc_pixels(P_OUT, 16, 16, "CC10 far outside");
	if (before && after) {
		CC_CHECK(before[(8 * 16 + 8) * 4] == 254 && before[0] == 90, "CC10: the surface before is not a red disc on grey");
		CC_CHECK(!memcmp(before, after, 16 * 16 * 4), "CC10: operations far outside the surface changed it");
	}
	free(before);
	free(after);

	/* A polygon around everything is the polygon around the surface. */
	cc_new(P_HUGE, 16, 16, 90, 80, 70, 0);
	cc_hll(P_HUGE, &(struct cc_cmd){ .cmd = 97, .c1 = { 0, 0, 255, 255 }, .nr_pos = 4, .pos = around_all });
	cc_new(P_MID, 16, 16, 90, 80, 70, 0);
	cc_hll(P_MID, &(struct cc_cmd){ .cmd = 97, .c1 = { 0, 0, 255, 255 }, .nr_pos = 4, .pos = around });
	before = cc_pixels(P_MID, 16, 16, "CC10 polygon around the surface");
	after = cc_pixels(P_HUGE, 16, 16, "CC10 polygon around everything");
	if (before && after) {
		/* 90 + ((0 - 90) * 255 >> 8) = 0, 70 + ((255 - 70) * 255 >> 8) = 254 */
		const uint8_t *c = before + (7 * 16 + 9) * 4;
		CC_CHECK(c[0] == 0 && c[1] == 0 && c[2] == 254 && c[3] == 255, "CC10 polygon around the surface: (9,7) = (%d,%d,%d,%d), want (0,0,254,255)",
			 c[0], c[1], c[2], c[3]);
		CC_CHECK(!memcmp(before, after, 16 * 16 * 4), "CC10: the polygon around everything differs from the one around the surface");
	}
	free(before);
	free(after);

	/* A blur over a rectangle that reaches across the surface with a radius
	 * beyond every line is the blur of everything with the largest radius a
	 * line of 16 allows (16 / 2 - 1). */
	cc_new(P_BLUR, 16, 16, 90, 80, 70, 200);
	cc_hll(P_BLUR, &(struct cc_cmd){ .cmd = 106, .x = 6, .y = 9, .radius = 4, .c1 = { 255, 0, 0, 255 } });
	cc_hll(P_BLUR, &(struct cc_cmd){ .cmd = 27, .x = -5, .y = -5, .w = INT_MAX, .h = INT_MAX, .blur = INT_MAX });
	cc_hll(P_BLUR, &(struct cc_cmd){ .cmd = 28, .x = -5, .y = -5, .w = INT_MAX, .h = INT_MAX, .blur = INT_MAX });
	cc_new(P_ALL, 16, 16, 90, 80, 70, 200);
	cc_hll(P_ALL, &(struct cc_cmd){ .cmd = 106, .x = 6, .y = 9, .radius = 4, .c1 = { 255, 0, 0, 255 } });
	cc_hll(P_ALL, &(struct cc_cmd){ .cmd = 27, .full = 1, .blur = 7 });
	cc_hll(P_ALL, &(struct cc_cmd){ .cmd = 28, .full = 1, .blur = 7 });
	before = cc_pixels(P_ALL, 16, 16, "CC10 blur of everything");
	after = cc_pixels(P_BLUR, 16, 16, "CC10 blur across the surface");
	if (before && after) {
		CC_CHECK(before[(9 * 16 + 6) * 4] < 254 && before[0] > 90, "CC10: the blur of everything left the disc as it was");
		CC_CHECK(!memcmp(before, after, 16 * 16 * 4), "CC10: the blur across the surface differs from the blur of everything");
	}
	free(before);
	free(after);

	/* Columns of a gradation INT_MAX wide: i * (255 - 128) / INT_MAX is 0 for
	 * the 16 columns there are, so every alpha is 200 * 128 / 255 = 100. */
	cc_new(P_GRAD, 16, 16, 90, 80, 70, 200);
	cc_hll(P_GRAD, &(struct cc_cmd){ .cmd = 26, .w = INT_MAX, .h = INT_MAX, .c1 = { 0, 0, 0, 128 }, .c2 = { 0, 0, 0, 255 } });
	uint8_t model[16 * 16 * 4];
	cc_fill_model(model, 16, 16, 90, 80, 70, 100);
	cc_expect_all(P_GRAD, 16, 16, model, "CC10 gradation INT_MAX wide");

	/* A save: a circle larger than this build draws is read as one without
	 * a radius (it was never built); the blur's radius, which is cut to the
	 * line when it is built, stays. */
	cc_new(P_SAVE, 16, 16, 90, 80, 70, 200);
	cc_hll(P_SAVE, &(struct cc_cmd){ .cmd = 106, .x = 8, .y = 8, .radius = 5, .c1 = { 255, 0, 0, 255 } });
	cc_hll(P_SAVE, &(struct cc_cmd){ .cmd = 102, .x = 8, .y = 8, .radius = 5000, .c1 = { 0, 0, 0, 255 } });
	cc_hll(P_SAVE, &(struct cc_cmd){ .cmd = 52, .x = 8, .y = 8, .radius = 3, .width = 5000, .c1 = { 0, 0, 0, 255 } });
	cc_hll(P_SAVE, &(struct cc_cmd){ .cmd = 27, .full = 1, .blur = 5000 });
	CC_CHECK(cc_nr_ops(P_SAVE) == 6, "CC10: %d operations before the save, want 6", cc_nr_ops(P_SAVE));
	before = cc_pixels(P_SAVE, 16, 16, "CC10 before the save");
	PE_UpdateComponent(0);
	struct page *saved = NULL;
	CC_CHECK(PE_Save(&saved) && saved, "PE_Save failed");
	if (!saved || !before) {
		free(before);
		return;
	}
	/* The radius, the width and the blur as declared. */
	CC_CHECK(cc_save_count(saved, 5000) == 3, "CC10: the first save holds 5000 %d time(s), want 3", cc_save_count(saved, 5000));
	CC_CHECK(PE_Load(&saved), "PE_Load failed");
	PE_UpdateComponent(0);
	CC_CHECK(cc_nr_ops(P_SAVE) == 6, "CC10 after PE_Load: %d operations, want 6", cc_nr_ops(P_SAVE));
	after = cc_pixels(P_SAVE, 16, 16, "CC10 after the load");
	if (after)
		CC_CHECK(!memcmp(before, after, 16 * 16 * 4), "CC10: the pixels differ after PE_Save / PE_Load");
	free(before);
	free(after);
	/* Saved again: only the blur's 5000 is left, the disc and the ring have
	 * no radius and no width. */
	PE_UpdateComponent(0);
	struct page *again = NULL;
	CC_CHECK(PE_Save(&again) && again, "the second PE_Save failed");
	if (again) {
		CC_CHECK(cc_save_count(again, 5000) == 1, "CC10: the save after PE_Load holds 5000 %d time(s), want 1 (the blur)",
			 cc_save_count(again, 5000));
		CC_CHECK(again->nr_vars == saved->nr_vars, "CC10: the second save has %d words, the first %d", again->nr_vars, saved->nr_vars);
	}
	printf("CC10 checked\n");
}

/* --- CC11 .. CC14: the polygon fills that write (commands 94 and 95) --- */
#define CQ_ROUND "\xcd\xe8\xa4\xe1"				/* 丸め */
#define CQ_ROUND_S "\x8a\xdb\x82\xdf"
#define CQ_POINTS "\xd7\xf9\x98\xcb\xbf\x82\xd3\x5b"		/* 座標總覽 */
#define CQ_POINTS_S "\x8d\xc0\x95\x57\x88\xea\x97\x97"		/* 座標一覧 */
#define CQ_COLOR2_S "\x90\x46\x82\x51"				/* 色２ */

/* A step with the keys the polygon commands read besides those of cc_step. */
struct cq_step {
	int cmd;
	int dest[6];	/* 先矩形 */
	int color[4];	/* 色１ */
	int full;	/* 全體 */
	int radius[2];	/* 半徑 */
	int arc[2];	/* 円弧角度 */
	int round[2];	/* 丸め */
	int rotate;	/* 旋轉角度 */
	int nr_xy;	/* 座標總覽's items; the key is left out without xy */
	const int *xy;
	int flags;	/* CQ_* */
};
enum {
	CQ_FLOAT_POINT = 1 << 0,	/* 座標總覽's third item is a float */
	CQ_NO_ROUND = 1 << 1,		/* no 丸め, */
	CQ_NO_ROTATE = 1 << 2,		/* no 旋轉角度 */
	CQ_FLOAT_ROUND = 1 << 3,	/* 丸め's second item is a float */
	CQ_NO_COLOR = 1 << 4,		/* no 色１ */
	CQ_FLOAT_COLOR = 1 << 5,	/* 色１'s first item is a float */
};
#define CQ_CREATE(w, h) { 0, { 0, 0, 0, 0, w, h } }
#define CQ_FILL(cmd, x, y, w, h, r, g, b, a) { cmd, { x, y, 0, 0, w, h }, { r, g, b, a } }
#define CQ_FILL_ALL(cmd, r, g, b, a) { cmd, { 0 }, { r, g, b, a }, 1 }
#define CQ_PIE(x, y, rx, ry, start, sweep, a) { 122, { x, y }, { 0, 0, 0, a }, 0, { rx, ry }, { start, sweep } }
#define CQ_XY(pts) .nr_xy = (int)(sizeof(pts) / sizeof((pts)[0])), .xy = pts
#define CQ_POLY(cmd, r, g, b, a, pts) { cmd, { 0 }, { r, g, b, a }, CQ_XY(pts) }
#define CQ_N(steps) (int)(sizeof(steps) / sizeof((steps)[0])), steps

static struct ex_tree cq_parts(const char *name, int n, const struct cq_step *steps, bool sjis)
{
	struct lb_place pl = { 0, 0, 1 };
	struct ex_tree info = bu_branch(BU_INFO, 2);
	info.children[0] = bu_sleaf(BU_TYPE, BU_LOW);
	info.children[1] = bu_branch(BU_NORMAL, 3);
	struct ex_tree *st = &info.children[1];
	st->children[0] = bu_sleaf(BU_TYPE, BR_CONS);
	st->children[1] = lb_ilist(CN_AREA, 4, (int[]){ 0, 0, 0, 0 });
	st->children[2] = bu_branch(sjis ? CN_STEPS_S : BR_STEPS, n);
	for (int i = 0; i < n; i++) {
		const struct cq_step *s = &steps[i];
		char key[32];
		snprintf(key, sizeof key, "Step%d", i + 1);
		unsigned k = 0;
		struct ex_tree t = bu_branch(key, 11);
		t.children[k++] = bu_ileaf(sjis ? CN_COMMAND_S : BR_COMMAND, s->cmd);
		t.children[k++] = lb_ilist(sjis ? CN_DEST_S : LB_DEST, 6, s->dest);
		if (!(s->flags & CQ_NO_COLOR)) {
			t.children[k++] = lb_ilist(sjis ? CN_COLOR1_S : CN_COLOR1, 4, s->color);
			if (s->flags & CQ_FLOAT_COLOR)
				t.children[k - 1].leaf.value.list->items[0].value = (struct ex_value){ .type = EX_FLOAT, .f = s->color[0] };
		}
		t.children[k++] = lb_ilist(sjis ? CQ_COLOR2_S : CC_COLOR2, 4, (int[]){ 0, 0, 0, 0 });
		t.children[k++] = bu_ileaf(sjis ? CN_FULL_S : CN_FULL, s->full);
		t.children[k++] = lb_ilist(sjis ? CN_RADIUS_S : CN_RADIUS, 2, s->radius);
		t.children[k++] = lb_ilist(sjis ? CN_ARC_S : CN_ARC, 2, s->arc);
		if (!(s->flags & CQ_NO_ROUND)) {
			t.children[k++] = lb_ilist(sjis ? CQ_ROUND_S : CQ_ROUND, 2, s->round);
			if (s->flags & CQ_FLOAT_ROUND)
				t.children[k - 1].leaf.value.list->items[1].value = (struct ex_value){ .type = EX_FLOAT, .f = s->round[1] };
		}
		if (!(s->flags & CQ_NO_ROTATE))
			t.children[k++] = bu_ileaf(sjis ? CN_ROTATE_S : CN_ROTATE, s->rotate);
		t.children[k++] = bu_sleaf(BR_CG_NAME, "");
		if (s->xy) {
			t.children[k++] = lb_ilist(sjis ? CQ_POINTS_S : CQ_POINTS, s->nr_xy, s->xy);
			if (s->flags & CQ_FLOAT_POINT)
				t.children[k - 1].leaf.value.list->items[2].value = (struct ex_value){ .type = EX_FLOAT, .f = s->xy[2] };
		}
		t.nr_children = k;
		st->children[2].children[i] = t;
	}
	return lb_node(name, &pl, info, 0);
}

/*
 * The coverage (0..255) of every pixel of a w x h surface by a convex polygon
 * that has no horizontal edge, as the scan 0x4d0820 sums it: the vertices at
 * their pixels' centres, the rows of their bounding box (clipped to the
 * surface), 8 lines y + k/8 a row; on a line the polygon reaches from its
 * leftmost crossing to its rightmost one (each x = (xh - xl) * (y - yl) /
 * (yh - yl) + xl from the edge's upper end, clamped to the scanned columns)
 * and adds the span [x1, x2 + 1) in 255ths of a column, or (x2 - x1) to the
 * one column both lie in; a line that only touches a vertex adds nothing. A
 * column's coverage is its sum / 8. Written for this shape alone: it does not
 * sort crossings or pair them.
 */
static void cq_cover(int w, int h, int n, const int *xy, int *cov)
{
	double xs[8], ys[8];
	int min_x = INT_MAX, min_y = INT_MAX, max_x = INT_MIN, max_y = INT_MIN;
	memset(cov, 0, (size_t)w * h * sizeof(int));
	assert(n >= 3 && n <= 8);
	for (int i = 0; i < n; i++) {
		xs[i] = xy[i * 2] + 0.5;
		ys[i] = xy[i * 2 + 1] + 0.5;
		if ((int)xs[i] < min_x) min_x = (int)xs[i];
		if ((int)xs[i] > max_x) max_x = (int)xs[i];
		if ((int)ys[i] < min_y) min_y = (int)ys[i];
		if ((int)ys[i] > max_y) max_y = (int)ys[i];
	}
	const int x0 = min_x < 0 ? 0 : min_x, x1 = max_x >= w ? w - 1 : max_x;
	const int y0 = min_y < 0 ? 0 : min_y, y1 = max_y >= h ? h - 1 : max_y;
	const int col0 = x0 - 1 < 0 ? 0 : x0 - 1, col1 = x1 + 2 > w ? w : x1 + 2;
	int *sums = xcalloc(w + 2, sizeof(int));
	for (int row = y0; row <= y1; row++) {
		memset(sums, 0, (w + 2) * sizeof(int));
		for (int k = 0; k < 8; k++) {
			const double y = row + k / 8.0;
			double lo = 0, hi = 0;
			int nr = 0;
			for (int i = 0; i < n; i++) {
				const int j = (i + 1) % n;
				assert(ys[i] != ys[j]);
				const int u = ys[i] < ys[j] ? i : j, d = u == i ? j : i;
				if (y < ys[u] || y > ys[d])
					continue;
				double x = (xs[d] - xs[u]) * (y - ys[u]);
				x = x / (ys[d] - ys[u]);
				x = x + xs[u];
				if (x > col1) x = col1;
				if (x < col0) x = col0;
				if (!nr || x < lo) lo = x;
				if (!nr || x > hi) hi = x;
				nr++;
			}
			if (nr < 2 || lo == hi)
				continue;
			const int ca = (int)lo, cz = (int)hi;
			if (ca == cz) {
				sums[ca] += (int)((hi - lo) * 255.0);
				continue;
			}
			sums[ca] += (int)(255.0 - (lo - ca) * 255.0);
			for (int c = ca + 1; c <= cz; c++)
				sums[c] += 255;
			sums[cz + 1] += (int)((hi - cz) * 255.0);
		}
		for (int c = col0; c < col1; c++)
			cov[row * w + c] = sums[c] / 8;
	}
	free(sums);
}

/* What the two commands write for a coverage (0x4d13a0 -> 0x5b0b80: the
 * colour and A * cov / 255 replace the pixel; 0x4d1420 -> 0x5b0a10: the
 * alpha alone): every covered pixel, also one whose alpha comes to 0. Each
 * value is clamped to 0..255 as it is written, the alpha after the division
 * (which truncates towards zero). */
static int cq_clamp(int v) { return v < 0 ? 0 : v > 255 ? 255 : v; }
static void cq_write(uint8_t *px, int npx, const int *cov, int cmd, int r, int g, int b, int a)
{
	for (int i = 0; i < npx; i++) {
		if (cov[i] <= 0)
			continue;
		if (cmd == 94) {
			px[i * 4] = cq_clamp(r);
			px[i * 4 + 1] = cq_clamp(g);
			px[i * 4 + 2] = cq_clamp(b);
		}
		px[i * 4 + 3] = cq_clamp(cc_mul255(a, cov[i]));
	}
}

/* CC4's rectangle (2,2) (11,2) (11,7) (2,7) on 20x10, computed there by hand:
 * columns 2 and 12 are the half columns, 3..11 whole; row 2 has 4 of the 8
 * lines, rows 3..6 all, row 7 has 5. */
static const int cq_rect[] = { 2, 2, 11, 2, 11, 7, 2, 7 };
static void cq_rect_cover(int *cov)
{
	memset(cov, 0, 20 * 10 * sizeof(int));
	for (int y = 2; y <= 7; y++) {
		const int edge = y == 2 ? 63 : y == 7 ? 79 : 127, in = y == 2 ? 127 : y == 7 ? 159 : 255;
		cov[y * 20 + 2] = cov[y * 20 + 12] = edge;
		for (int x = 3; x <= 11; x++)
			cov[y * 20 + x] = in;
	}
}

static const struct cq_step cq_rect94[] = { CQ_CREATE(20, 10), CQ_FILL_ALL(6, 10, 200, 30, 255), CQ_POLY(94, 255, 0, 186, 128, cq_rect) };
static const struct cq_step cq_rect95[] = { CQ_CREATE(20, 10), CQ_FILL_ALL(6, 10, 200, 30, 255), CQ_POLY(95, 255, 0, 186, 128, cq_rect) };
static const struct cq_step cq_faint94[] = { CQ_CREATE(20, 10), CQ_FILL_ALL(6, 10, 200, 30, 255), CQ_POLY(94, 255, 0, 186, 1, cq_rect) };
static const struct cq_step cq_faint95[] = { CQ_CREATE(20, 10), CQ_FILL_ALL(6, 10, 200, 30, 255), CQ_POLY(95, 0, 0, 0, 1, cq_rect) };
/* WorkResultScreen's ResultBase, SceneSendResult's ProfBase and
 * BattleResultPlayerView's NewSkillBase, as their pactex has them. */
static const int cq_result_xy[] = { 2, 0, 415, 15, 409, 266, 11, 254 };
static const int cq_prof_xy[] = { 0, 0, 211, 12, 240, 64, 14, 80 };
static const int cq_skill_xy[] = { 64, 48, 60, 38, 68, 38 };
static const struct cq_step cq_result[] = { CQ_CREATE(420, 270), CQ_FILL_ALL(6, 0, 0, 0, 0), CQ_POLY(94, 0, 0, 0, 255, cq_result_xy) };
static const struct cq_step cq_prof[] = { CQ_CREATE(240, 80), CQ_FILL_ALL(5, 0, 0, 0, 0), CQ_POLY(94, 255, 0, 186, 255, cq_prof_xy) };
static const struct cq_step cq_skill[] = { CQ_CREATE(128, 48), CQ_FILL_ALL(6, 255, 255, 0, 0), CQ_POLY(95, 0, 0, 0, 255, cq_skill_xy),
	CQ_FILL(5, 16, 0, 96, 40, 0, 0, 0, 255), CQ_FILL(5, 0, 16, 128, 8, 0, 0, 0, 255),
	CQ_PIE(16, 16, 16, 16, 180, 90, 255), CQ_PIE(112, 16, 16, 16, 270, 90, 255),
	CQ_PIE(112, 24, 16, 16, 0, 90, 255), CQ_PIE(16, 24, 16, 16, 90, 90, 255) };

static void cq_expect_px(const uint8_t *got, int w, int x, int y, int r, int g, int b, int a, const char *what)
{
	if (!got)
		return;
	const uint8_t *p = got + (y * w + x) * 4;
	CC_CHECK(p[0] == r && p[1] == g && p[2] == b && p[3] == a, "%s (%d,%d) = (%d,%d,%d,%d), want (%d,%d,%d,%d)", what, x, y,
		 p[0], p[1], p[2], p[3], r, g, b, a);
}

/* --- CC11 --- */
static void cc_polygon_write(void)
{
	cn_gl_setup();
	ri_postlink();
	struct ex_tree *kids;
	struct ex *e = lb_activity("ConstructionPolygon", 8, &kids);
	kids->children[0] = cq_parts("Rect94", CQ_N(cq_rect94), false);
	kids->children[1] = cq_parts("Rect95", CQ_N(cq_rect95), false);
	kids->children[2] = cq_parts("Faint94", CQ_N(cq_faint94), false);
	kids->children[3] = cq_parts("Faint95", CQ_N(cq_faint95), false);
	kids->children[4] = cq_parts("ResultBase", CQ_N(cq_result), false);
	kids->children[5] = cq_parts("ProfBase", CQ_N(cq_prof), false);
	kids->children[6] = cq_parts("NewSkillBase", CQ_N(cq_skill), false);
	kids->children[7] = cq_parts("Rect94Sjis", CQ_N(cq_rect94), true);
	int act = cn_load("ConstructionPolygon", e);
	int rect94 = cn_no(act, "Rect94"), rect95 = cn_no(act, "Rect95"), faint94 = cn_no(act, "Faint94");
	int faint95 = cn_no(act, "Faint95"), result = cn_no(act, "ResultBase"), prof = cn_no(act, "ProfBase");
	int skill = cn_no(act, "NewSkillBase"), sjis = cn_no(act, "Rect94Sjis");
	heap_unref(act);

	/* The same rectangle and colour, (255,0,186) with alpha 128, on an
	 * opaque (10,200,30): 94 replaces what it covers by the colour and
	 * 128 * cov / 255 (128, and 63, 31, 39, 79 for the coverages 127, 63,
	 * 79, 159), 95 writes that alpha and keeps the colour, 97 blends
	 * ((255 - 10) * 128 >> 8 = 122 and so on) and keeps the larger alpha. */
	int cov[20 * 10];
	uint8_t model[20 * 10 * 4];
	cq_rect_cover(cov);
	cc_fill_model(model, 20, 10, 10, 200, 30, 255);
	cq_write(model, 20 * 10, cov, 94, 255, 0, 186, 128);
	uint8_t *got = cn_pixels(rect94, 20, 10, "CC11 94");
	cq_expect_px(got, 20, 5, 4, 255, 0, 186, 128, "CC11 94 inside");
	cq_expect_px(got, 20, 2, 4, 255, 0, 186, 63, "CC11 94 half column");
	cq_expect_px(got, 20, 12, 2, 255, 0, 186, 31, "CC11 94 corner");
	cq_expect_px(got, 20, 5, 7, 255, 0, 186, 79, "CC11 94 last row");
	cq_expect_px(got, 20, 2, 7, 255, 0, 186, 39, "CC11 94 last row's half column");
	cq_expect_px(got, 20, 13, 4, 10, 200, 30, 255, "CC11 94 beside");
	cq_expect_px(got, 20, 5, 1, 10, 200, 30, 255, "CC11 94 above");
	cc_compare(got, 20, 10, model, "CC11 94");
	struct parts *p = parts_try_get(rect94);
	CC_CHECK(p && cn_nr_ops(p) == 3, "CC11 94: not 3 operations");
	cc_compare(cn_pixels(sjis, 20, 10, "CC11 94 under the SJIS keys"), 20, 10, model, "CC11 94 under the SJIS keys");

	cc_fill_model(model, 20, 10, 10, 200, 30, 255);
	cq_write(model, 20 * 10, cov, 95, 0, 0, 0, 128);
	got = cn_pixels(rect95, 20, 10, "CC11 95");
	cq_expect_px(got, 20, 5, 4, 10, 200, 30, 128, "CC11 95 inside");
	cq_expect_px(got, 20, 2, 4, 10, 200, 30, 63, "CC11 95 half column");
	cq_expect_px(got, 20, 13, 4, 10, 200, 30, 255, "CC11 95 beside");
	cc_compare(got, 20, 10, model, "CC11 95");

	const int BLEND = 9471;
	cc_new(BLEND, 20, 10, 10, 200, 30, 255);
	cc_hll(BLEND, &(struct cc_cmd){ .cmd = 97, .c1 = { 255, 0, 186, 128 }, .nr_pos = 4, .pos = cq_rect });
	got = cc_pixels(BLEND, 20, 10, "CC11 97");
	cq_expect_px(got, 20, 5, 4, 132, 100, 108, 255, "CC11 97 inside");
	cq_expect_px(got, 20, 13, 4, 10, 200, 30, 255, "CC11 97 beside");
	uint8_t staged[20 * 10 * 4] = { 0 };
	for (int i = 0; i < 20 * 10; i++)
		staged[i * 4 + 3] = cc_mul255(128, cov[i]);
	cc_fill_model(model, 20, 10, 10, 200, 30, 255);
	cc_model_rgb[0] = 255; cc_model_rgb[1] = 0; cc_model_rgb[2] = 186;
	cc_compose_all(model, staged, 20, 10);
	cc_compare(got, 20, 10, model, "CC11 97");

	/* An alpha of 1: 1 * cov / 255 is 0 unless the pixel is whole, and the
	 * pixel is written all the same (0x4d13a0 does not look at it). */
	cc_fill_model(model, 20, 10, 10, 200, 30, 255);
	cq_write(model, 20 * 10, cov, 94, 255, 0, 186, 1);
	got = cn_pixels(faint94, 20, 10, "CC11 94 with alpha 1");
	cq_expect_px(got, 20, 5, 4, 255, 0, 186, 1, "CC11 94 with alpha 1, inside");
	cq_expect_px(got, 20, 2, 4, 255, 0, 186, 0, "CC11 94 with alpha 1, half column");
	cq_expect_px(got, 20, 5, 2, 255, 0, 186, 0, "CC11 94 with alpha 1, first row");
	cc_compare(got, 20, 10, model, "CC11 94 with alpha 1");
	cc_fill_model(model, 20, 10, 10, 200, 30, 255);
	cq_write(model, 20 * 10, cov, 95, 0, 0, 0, 1);
	got = cn_pixels(faint95, 20, 10, "CC11 95 with alpha 1");
	cq_expect_px(got, 20, 2, 4, 10, 200, 30, 0, "CC11 95 with alpha 1, half column");
	cc_compare(got, 20, 10, model, "CC11 95 with alpha 1");

	/* ResultBase: black on a transparent 420x270. By hand: row 0 holds the
	 * top vertex (2.5, 0.5), only its lines 0.625, 0.75 and 0.875 cross
	 * the two edges from it ([2.504, 6.942), [2.509, 10.383), [2.513,
	 * 13.825) with the last column): sums 375, 765 x3, 750, 510 x3, 352,
	 * 255 x2, 210. Row 135: the left edge runs from 7.266 to 7.297 (sum
	 * 1462, coverage 182), the right one from 412.643 to 412.623, which
	 * adds its fraction to column 413 (sum 1288, coverage 161). */
	int *big = xcalloc(420 * 270, sizeof(int));
	uint8_t *bm = xcalloc(420 * 270, 4);
	cq_cover(420, 270, 4, cq_result_xy, big);
	static const int row0[14] = { 0, 46, 95, 95, 95, 93, 63, 63, 63, 44, 31, 31, 26, 0 };
	for (int x = 1; x <= 14; x++)
		CC_CHECK(big[x] == row0[x - 1], "the model itself: coverage (%d,0) = %d, want %d", x, big[x], row0[x - 1]);
	CC_CHECK(big[135 * 420 + 6] == 0 && big[135 * 420 + 7] == 182 && big[135 * 420 + 8] == 255 && big[135 * 420 + 412] == 255
		 && big[135 * 420 + 413] == 161 && big[135 * 420 + 414] == 0,
		 "the model itself: row 135 has %d %d %d .. %d %d %d, want 0 182 255 .. 255 161 0", big[135 * 420 + 6],
		 big[135 * 420 + 7], big[135 * 420 + 8], big[135 * 420 + 412], big[135 * 420 + 413], big[135 * 420 + 414]);
	cq_write(bm, 420 * 270, big, 94, 0, 0, 0, 255);
	got = cn_pixels(result, 420, 270, "CC11 ResultBase");
	cq_expect_px(got, 420, 210, 135, 0, 0, 0, 255, "CC11 ResultBase centre");
	cq_expect_px(got, 420, 0, 0, 0, 0, 0, 0, "CC11 ResultBase corner");
	cq_expect_px(got, 420, 419, 0, 0, 0, 0, 0, "CC11 ResultBase corner");
	cq_expect_px(got, 420, 0, 269, 0, 0, 0, 0, "CC11 ResultBase corner");
	cq_expect_px(got, 420, 419, 269, 0, 0, 0, 0, "CC11 ResultBase corner");
	cq_expect_px(got, 420, 7, 135, 0, 0, 0, 182, "CC11 ResultBase left edge");
	cq_expect_px(got, 420, 413, 135, 0, 0, 0, 161, "CC11 ResultBase right edge");
	cq_expect_px(got, 420, 2, 0, 0, 0, 0, 46, "CC11 ResultBase top vertex");
	cq_expect_px(got, 420, 13, 0, 0, 0, 0, 26, "CC11 ResultBase top row");
	cc_compare(got, 420, 270, bm, "CC11 ResultBase");
	free(big);
	free(bm);

	/* ProfBase: magenta on a 240x80 whose alpha command 5 cleared. Two of
	 * its vertices, (240,64) and (14,80), lie one past the surface. By
	 * hand, row 64: the lines 64 .. 64.5 reach the last column (their
	 * right crossing is clamped to 240), 64.625 ends at 238.734 and adds
	 * 187 to column 239, 64.75 ends at 236.969: sums 1462, 1530, 1777. */
	big = xcalloc(240 * 80, sizeof(int));
	bm = xcalloc(240 * 80, 4);
	cq_cover(240, 80, 4, cq_prof_xy, big);
	CC_CHECK(big[64 * 240 + 239] == 182 && big[64 * 240 + 238] == 191 && big[64 * 240 + 237] == 222,
		 "the model itself: row 64 ends with %d %d %d, want 222 191 182", big[64 * 240 + 237], big[64 * 240 + 238],
		 big[64 * 240 + 239]);
	cq_write(bm, 240 * 80, big, 94, 255, 0, 186, 255);
	got = cn_pixels(prof, 240, 80, "CC11 ProfBase");
	cq_expect_px(got, 240, 120, 40, 255, 0, 186, 255, "CC11 ProfBase inside");
	cq_expect_px(got, 240, 239, 64, 255, 0, 186, 182, "CC11 ProfBase right vertex");
	cq_expect_px(got, 240, 239, 40, 0, 0, 0, 0, "CC11 ProfBase beside");
	cq_expect_px(got, 240, 20, 79, 255, 0, 186, 255, "CC11 ProfBase last row");
	cc_compare(got, 240, 80, bm, "CC11 ProfBase");
	free(big);
	free(bm);

	/* NewSkillBase: a yellow whose alpha is the triangle of command 95,
	 * two rectangles and four sectors; the colour stays. Row 44 of the
	 * triangle (64,48) (60,38) (68,38) spans [62.7, 67.3) at its top line
	 * and [63.05, 66.95) at its last: columns 64 and 65 are whole.
	 * Its edges, by hand (the rows below the rectangle of command 5; the
	 * left edge is at 60.5 + 0.4 (y - 38.5), the right one at 68.5 less
	 * that, and a product that falls on a whole or a half is the double's):
	 * row 40, lines 40 .. 40.875: the left edge runs from 61.1 to 61.45 and
	 * leaves column 61 229, 216, 203, 191, 178, 165, 153, 140 of 255 (1475,
	 * coverage 184); the right one runs from 67.9 to 67.55 and adds 229,
	 * 216, 203, 191, 178, 165, 152, 140 to column 68 (1474, 184).
	 * row 43: 178, 165, 153, 140, 127, 114, 101, 89 in column 62 (1067,
	 * 133) and as much in column 67.
	 * row 47, the last of the surface (the tip (64,48) lies one below it):
	 * its first two lines span [63.9, 66.1) and [63.95, 66.05), 25 and 12
	 * for columns 63 and 66 (37, coverage 4) and both of 64 and 65 whole;
	 * the third spans [64, 66): 64 and 65 whole; the other five stay within
	 * column 64, 229, 204, 178, 152 and 127. Column 64: 3 * 255 + 890 =
	 * 1655 (206); column 65: 3 * 255 (95). */
	got = cn_pixels(skill, 128, 48, "CC11 NewSkillBase");
	p = parts_try_get(skill);
	CC_CHECK(p && cn_nr_ops(p) == 9, "CC11 NewSkillBase: not 9 operations");
	cq_expect_px(got, 128, 64, 44, 255, 255, 0, 255, "CC11 NewSkillBase triangle");
	cq_expect_px(got, 128, 65, 42, 255, 255, 0, 255, "CC11 NewSkillBase triangle");
	cq_expect_px(got, 128, 61, 40, 255, 255, 0, 184, "CC11 NewSkillBase left edge");
	cq_expect_px(got, 128, 68, 40, 255, 255, 0, 184, "CC11 NewSkillBase right edge");
	cq_expect_px(got, 128, 60, 40, 255, 255, 0, 0, "CC11 NewSkillBase beside the left edge");
	cq_expect_px(got, 128, 62, 43, 255, 255, 0, 133, "CC11 NewSkillBase left edge");
	cq_expect_px(got, 128, 67, 43, 255, 255, 0, 133, "CC11 NewSkillBase right edge");
	cq_expect_px(got, 128, 63, 47, 255, 255, 0, 4, "CC11 NewSkillBase last row");
	cq_expect_px(got, 128, 64, 47, 255, 255, 0, 206, "CC11 NewSkillBase last row");
	cq_expect_px(got, 128, 65, 47, 255, 255, 0, 95, "CC11 NewSkillBase last row");
	cq_expect_px(got, 128, 66, 47, 255, 255, 0, 4, "CC11 NewSkillBase last row");
	cq_expect_px(got, 128, 40, 44, 255, 255, 0, 0, "CC11 NewSkillBase below the box");
	cq_expect_px(got, 128, 64, 20, 255, 255, 0, 255, "CC11 NewSkillBase box");
	cq_expect_px(got, 128, 2, 2, 255, 255, 0, 0, "CC11 NewSkillBase corner");
	cq_expect_px(got, 128, 127, 47, 255, 255, 0, 0, "CC11 NewSkillBase corner");
	free(got);
	cc_failures += cn_failures;
	printf("CC11 checked\n");
}

/* --- CC12 --- */
static const int cq_nine[] = { 2, 2, 11, 2, 11, 7, 2, 7, 5 };
static const int cq_five[] = { 2, 2, 11, 2, 11 };
static const int cq_outside[] = { 30, 30, 40, 30, 40, 40 };
static const int cq_repeated[] = { 2, 2, 2, 2, 11, 7 };
#define CQ_BASE CQ_CREATE(20, 10), CQ_FILL_ALL(6, 10, 200, 30, 255)
static const struct cq_step cq_round1[] = { CQ_BASE, { 94, { 0 }, { 255, 0, 186, 128 }, .round = { 0, 1 }, CQ_XY(cq_rect) } };
static const struct cq_step cq_rotated[] = { CQ_BASE, { 94, { 0 }, { 255, 0, 186, 128 }, .rotate = 30, CQ_XY(cq_rect) } };
static const struct cq_step cq_odd_two[] = { CQ_BASE, CQ_POLY(94, 255, 0, 186, 128, cq_five) };
static const struct cq_step cq_two[] = { CQ_BASE, { 94, { 0 }, { 255, 0, 186, 128 }, .nr_xy = 4, .xy = cq_rect } };
static const struct cq_step cq_float[] = { CQ_BASE, { 95, { 0 }, { 0, 0, 0, 128 }, CQ_XY(cq_rect), .flags = CQ_FLOAT_POINT } };
static const struct cq_step cq_no_xy[] = { CQ_BASE, { 94, { 0 }, { 255, 0, 186, 128 } } };
static const struct cq_step cq_far[] = { CQ_BASE, CQ_POLY(94, 255, 0, 186, 128, cq_outside) };
static const struct cq_step cq_same[] = { CQ_BASE, CQ_POLY(94, 255, 0, 186, 128, cq_repeated) };
static const struct cq_step cq_round_float[] = { CQ_BASE, { 94, { 0 }, { 255, 0, 186, 128 }, CQ_XY(cq_rect), .flags = CQ_FLOAT_ROUND } };
static const struct cq_step cq_color_float[] = { CQ_BASE, { 94, { 0 }, { 255, 0, 186, 128 }, CQ_XY(cq_rect), .flags = CQ_FLOAT_COLOR } };
/* 255 * 8421505 does not fit 32 bits: the original's product wraps around. */
static const struct cq_step cq_alpha_wrap[] = { CQ_BASE, CQ_POLY(95, 0, 0, 0, 8421505, cq_rect) };
/* Built as the original runs them: 丸め's first item is another flag (the
 * handlers read +0xd5, the second); a missing 丸め or 旋轉角度 is 0
 * (0x4f82a0); 95 reads only 色１'s alpha; a last int without a partner is
 * dropped (0x4eaab4); the colour and A * cov / 255 are clamped when they are
 * written (0x5b0b80, 0x5b0a10); a missing 色１ is (0, 0, 0, 0); the angle is
 * taken modulo 360 (0x4e996d). */
static const struct cq_step cq_round0[] = { CQ_BASE, { 94, { 0 }, { 255, 0, 186, 128 }, .round = { 1, 0 }, CQ_XY(cq_rect) } };
static const struct cq_step cq_no_round[] = { CQ_BASE, { 94, { 0 }, { 255, 0, 186, 128 }, CQ_XY(cq_rect), .flags = CQ_NO_ROUND } };
static const struct cq_step cq_no_rotate[] = { CQ_BASE, { 94, { 0 }, { 255, 0, 186, 128 }, CQ_XY(cq_rect), .flags = CQ_NO_ROTATE } };
static const struct cq_step cq_color95[] = { CQ_BASE, CQ_POLY(95, 999, -1, 300, 128, cq_rect) };
static const struct cq_step cq_odd[] = { CQ_BASE, CQ_POLY(94, 255, 0, 186, 128, cq_nine) };
static const struct cq_step cq_red256[] = { CQ_BASE, CQ_POLY(94, 256, -1, 186, 128, cq_rect) };
static const struct cq_step cq_alpha300[] = { CQ_BASE, CQ_POLY(95, 0, 0, 0, 300, cq_rect) };
static const struct cq_step cq_alpha_neg[] = { CQ_BASE, CQ_POLY(94, 255, 0, 186, -40, cq_rect) };
static const struct cq_step cq_no_color[] = { CQ_BASE, { 94, { 0 }, { 255, 0, 186, 128 }, CQ_XY(cq_rect), .flags = CQ_NO_COLOR } };
static const struct cq_step cq_rotate720[] = { CQ_BASE, { 94, { 0 }, { 255, 0, 186, 128 }, .rotate = 720, CQ_XY(cq_rect) } };
static const struct cq_step cq_rotate_m360[] = { CQ_BASE, { 95, { 0 }, { 0, 0, 0, 128 }, .rotate = -360, CQ_XY(cq_rect) } };

static void cc_polygon_unbuilt(void)
{
	cn_gl_setup();
	ri_postlink();
	static const struct { const char *name; int n; const struct cq_step *steps; } unbuilt[] = {
		{ "RoundCorner", CQ_N(cq_round1) }, { "Rotated", CQ_N(cq_rotated) }, { "FiveInts", CQ_N(cq_odd_two) },
		{ "TwoVertices", CQ_N(cq_two) }, { "FloatPoint", CQ_N(cq_float) }, { "NoPoints", CQ_N(cq_no_xy) },
		{ "Outside", CQ_N(cq_far) }, { "Repeated", CQ_N(cq_same) }, { "FloatRound", CQ_N(cq_round_float) },
		{ "FloatColor", CQ_N(cq_color_float) }, { "AlphaWrap", CQ_N(cq_alpha_wrap) },
	};
	/* What each of the built ones writes on the rectangle's pixels. */
	static const struct { const char *name; int n; const struct cq_step *steps; int c[4]; } built[] = {
		{ "RoundFirst", CQ_N(cq_round0), { 255, 0, 186, 128 } }, { "NoRound", CQ_N(cq_no_round), { 255, 0, 186, 128 } },
		{ "NoRotate", CQ_N(cq_no_rotate), { 255, 0, 186, 128 } }, { "Color95", CQ_N(cq_color95), { 0, 0, 0, 128 } },
		{ "NineInts", CQ_N(cq_odd), { 255, 0, 186, 128 } }, { "Red256", CQ_N(cq_red256), { 256, -1, 186, 128 } },
		{ "Alpha300", CQ_N(cq_alpha300), { 0, 0, 0, 300 } }, { "AlphaNegative", CQ_N(cq_alpha_neg), { 255, 0, 186, -40 } },
		{ "NoColor", CQ_N(cq_no_color), { 0, 0, 0, 0 } }, { "Rotate720", CQ_N(cq_rotate720), { 255, 0, 186, 128 } },
		{ "RotateMinus360", CQ_N(cq_rotate_m360), { 0, 0, 0, 128 } },
	};
	enum { NU = sizeof(unbuilt) / sizeof(unbuilt[0]), NB = sizeof(built) / sizeof(built[0]) };
	struct ex_tree *kids;
	struct ex *e = lb_activity("ConstructionPolygonUnbuilt", NU + NB, &kids);
	for (int i = 0; i < NU; i++)
		kids->children[i] = cq_parts(unbuilt[i].name, unbuilt[i].n, unbuilt[i].steps, false);
	for (int i = 0; i < NB; i++)
		kids->children[NU + i] = cq_parts(built[i].name, built[i].n, built[i].steps, false);
	int act = cn_load("ConstructionPolygonUnbuilt", e);
	for (int i = 0; i < NU; i++)
		cn_expect_unbuilt(cn_no(act, unbuilt[i].name), 20, 10, unbuilt[i].name);
	int cov[20 * 10];
	uint8_t model[20 * 10 * 4];
	cq_rect_cover(cov);
	for (int i = 0; i < NB; i++) {
		cc_fill_model(model, 20, 10, 10, 200, 30, 255);
		cq_write(model, 20 * 10, cov, built[i].steps[2].cmd, built[i].c[0], built[i].c[1], built[i].c[2], built[i].c[3]);
		cc_compare(cn_pixels(cn_no(act, built[i].name), 20, 10, built[i].name), 20, 10, model, built[i].name);
	}
	/* By hand: R 256 and G -1 are written as 255 and 0; an alpha of 300
	 * gives 300 -> 255 inside and 300 * 127 / 255 = 149 on the half column
	 * (an alpha clamped to 255 before the division would give 127); a
	 * negative alpha is 0; without 色１ the covered pixels are transparent
	 * black. */
	uint8_t *got = cn_pixels(cn_no(act, "Red256"), 20, 10, "Red256");
	cq_expect_px(got, 20, 5, 4, 255, 0, 186, 128, "CC12 Red256 inside");
	free(got);
	got = cn_pixels(cn_no(act, "Alpha300"), 20, 10, "Alpha300");
	cq_expect_px(got, 20, 5, 4, 10, 200, 30, 255, "CC12 Alpha300 inside");
	cq_expect_px(got, 20, 2, 4, 10, 200, 30, 149, "CC12 Alpha300 half column");
	free(got);
	got = cn_pixels(cn_no(act, "AlphaNegative"), 20, 10, "AlphaNegative");
	cq_expect_px(got, 20, 5, 4, 255, 0, 186, 0, "CC12 AlphaNegative inside");
	free(got);
	got = cn_pixels(cn_no(act, "NoColor"), 20, 10, "NoColor");
	cq_expect_px(got, 20, 5, 4, 0, 0, 0, 0, "CC12 NoColor inside");
	cq_expect_px(got, 20, 13, 4, 10, 200, 30, 255, "CC12 NoColor beside");
	free(got);
	heap_unref(act);
	cc_failures += cn_failures;
	printf("CC12 checked\n");
}

/* --- CC13 --- */
static int cq_version_pos(struct page *saved)
{
	struct iarray_reader r;
	return iarray_init_reader(&r, saved, "XPE") ? (int)r.pos : -1;
}
static uint8_t *cq_state_pixels(int no, int w, int h)
{
	struct parts *p = no > 0 ? parts_try_get(no) : NULL;
	struct parts_state *s = p ? &p->states[0] : NULL;
	return s && s->type == PARTS_CONSTRUCTION_PROCESS && s->common.texture.handle && s->common.texture.w == w
		&& s->common.texture.h == h ? gfx_get_pixels(&s->common.texture) : NULL;
}

static void cc_polygon_save(void)
{
	cn_gl_setup();
	ri_postlink();
	/* A save without the new operations is version 11 too, and loads as
	 * the versions before it wrote it: 8 to 10 differ by the number alone,
	 * 7 has no list of sub colour parts at the end. */
	const int PLAIN = 9481;
	static const int tri[] = { 1, 1, 14, 3, 6, 13 };
	cc_new(PLAIN, 16, 16, 10, 20, 30, 200);
	cc_hll(PLAIN, &(struct cc_cmd){ .cmd = 97, .c1 = { 0, 90, 200, 180 }, .nr_pos = 3, .pos = tri });
	cc_hll(PLAIN, &(struct cc_cmd){ .cmd = 27, .full = 1, .blur = 2 });
	uint8_t *plain = cc_pixels(PLAIN, 16, 16, "CC13 plain");
	PE_UpdateComponent(0);
	struct page *saved = NULL;
	CC_CHECK(PE_Save(&saved), "CC13: PE_Save failed");
	if (!saved || !plain)
		return;
	int pos = cq_version_pos(saved);
	CC_CHECK(pos >= 0 && ca_save_version(saved) == 11, "CC13: XPE v%d, want 11", ca_save_version(saved));
	CC_CHECK(saved->values[saved->nr_vars - 1].i == 0, "CC13: the save does not end with an empty list of sub colour parts");
	for (int version = 10; pos >= 0 && version >= 7; version--) {
		struct page *old = copy_page(saved);
		old->values[pos].i = version;
		if (version == 7)
			old->nr_vars--;
		CC_CHECK(ca_save_version(old) == version && PE_Load(&old), "CC13: a version %d save did not load", version);
		PE_UpdateComponent(0);
		uint8_t *got = cq_state_pixels(PLAIN, 16, 16);
		CC_CHECK(got && !memcmp(got, plain, 16 * 16 * 4) && cc_nr_ops(PLAIN) == 4,
			 "CC13: a version %d save loads other pixels (%d operations)", version, cc_nr_ops(PLAIN));
		free(got);
		if (version == 7)
			old->nr_vars++;
		ca_free_buffer(old);
	}
	ca_free_buffer(saved);
	free(plain);

	/* The three states of the game, saved and loaded. */
	struct ex_tree *kids;
	struct ex *e = lb_activity("ConstructionPolygonSave", 3, &kids);
	kids->children[0] = cq_parts("Rect94", CQ_N(cq_rect94), false);
	kids->children[1] = cq_parts("ProfBase", CQ_N(cq_prof), false);
	kids->children[2] = cq_parts("NewSkillBase", CQ_N(cq_skill), false);
	int act = cn_load("ConstructionPolygonSave", e);
	const int no[3] = { cn_no(act, "Rect94"), cn_no(act, "ProfBase"), cn_no(act, "NewSkillBase") };
	heap_unref(act);
	static const int size[3][2] = { { 20, 10 }, { 240, 80 }, { 128, 48 } }, nr_ops[3] = { 3, 3, 9 };
	uint8_t *before[3];
	for (int i = 0; i < 3; i++)
		before[i] = cn_pixels(no[i], size[i][0], size[i][1], "CC13 before the save");
	int cov[20 * 10];
	uint8_t model[20 * 10 * 4];
	cq_rect_cover(cov);
	cc_fill_model(model, 20, 10, 10, 200, 30, 255);
	cq_write(model, 20 * 10, cov, 94, 255, 0, 186, 128);
	CC_CHECK(before[0] && !memcmp(before[0], model, sizeof model), "CC13: the rectangle of command 94 is not built before the save");
	/* The numbers a version 11 save holds for the two operations: the
	 * third of Rect94 (command 94) and of NewSkillBase (command 95). */
	int types[2] = { -1, -1 };
	for (int i = 0; i < 2; i++) {
		struct parts *p = parts_try_get(no[i * 2]);
		struct parts_cp_op *op = p && p->states[0].type == PARTS_CONSTRUCTION_PROCESS ? TAILQ_FIRST(&p->states[0].cproc.ops) : NULL;
		for (int k = 0; op && k < 2; k++)
			op = TAILQ_NEXT(op, entry);
		if (op)
			types[i] = op->type;
	}
	CC_CHECK(types[0] == 27 && types[1] == 28, "CC13: the operations of commands 94 and 95 have the types %d and %d, want 27 and 28",
		 types[0], types[1]);
	PE_UpdateComponent(0);
	saved = NULL;
	CC_CHECK(PE_Save(&saved), "CC13: PE_Save failed");
	if (saved) {
		CC_CHECK(ca_save_version(saved) == 11, "CC13: a save with the new operations is XPE v%d, want 11", ca_save_version(saved));
		int words = saved->nr_vars;
		CC_CHECK(PE_Load(&saved), "CC13: PE_Load failed");
		PE_UpdateComponent(0);
		for (int i = 0; i < 3; i++) {
			uint8_t *after = cq_state_pixels(no[i], size[i][0], size[i][1]);
			CC_CHECK(before[i] && after && !memcmp(before[i], after, (size_t)size[i][0] * size[i][1] * 4)
				 && cc_nr_ops(no[i]) == nr_ops[i], "CC13: state %d after PE_Load: %s, %d operations (want %d)", i,
				 after ? "other pixels" : "not built", cc_nr_ops(no[i]), nr_ops[i]);
			free(after);
		}
		struct page *again = NULL;
		CC_CHECK(PE_Save(&again) && again && again->nr_vars == words, "CC13: the save after PE_Load has %d words, the first %d",
			 again ? again->nr_vars : -1, words);
		ca_free_buffer(again);
		ca_free_buffer(saved);
	}
	for (int i = 0; i < 3; i++)
		free(before[i]);
	cc_failures += cn_failures;
	printf("CC13 checked\n");
}

/* --- CC14 --- */
static const struct cq_step cq_old[] = { CQ_CREATE(20, 10), CQ_FILL_ALL(6, 10, 200, 30, 255) };

static void cc_polygon_v13(void)
{
	cn_gl_setup();
	ri_postlink();
	int version = ain->version;
	ain->version = 13;
	struct ex_tree *kids;
	struct ex *e = lb_activity("ConstructionPolygonV13", 3, &kids);
	kids->children[0] = cq_parts("Rect94", CQ_N(cq_rect94), false);
	kids->children[1] = cq_parts("Rect95", CQ_N(cq_rect95), false);
	kids->children[2] = cq_parts("Old", CQ_N(cq_old), false);
	int act = cn_load("ConstructionPolygonV13", e);
	int r94 = cn_no(act, "Rect94"), r95 = cn_no(act, "Rect95"), old = cn_no(act, "Old");
	heap_unref(act);
	uint8_t *a = cq_state_pixels(r94, 20, 10), *b = cq_state_pixels(r95, 20, 10), *c = cq_state_pixels(old, 20, 10);
	CC_CHECK(!a && !b, "CC14: v13 built a state with command 94 or 95");
	CC_CHECK(c, "CC14: v13 did not build (0, 6)");
	free(a);
	free(b);
	free(c);
	PE_UpdateComponent(0);
	struct page *saved = NULL;
	CC_CHECK(PE_Save(&saved) && saved && ca_save_version(saved) == 3, "CC14: a v13 save is XPE v%d, want 3",
		 saved ? ca_save_version(saved) : -1);
	ca_free_buffer(saved);
	ain->version = version;

	/* The same activity on v14. */
	e = lb_activity("ConstructionPolygonV14", 1, &kids);
	kids->children[0] = cq_parts("Rect94", CQ_N(cq_rect94), false);
	act = cn_load("ConstructionPolygonV14", e);
	r94 = cn_no(act, "Rect94");
	heap_unref(act);
	a = cq_state_pixels(r94, 20, 10);
	CC_CHECK(a, "CC14: v14 did not build the state with command 94");
	free(a);
	cc_failures += cn_failures;
	printf("CC14 checked\n");
}

static void cc_tiles_case(void)
{
	cn_gl_setup();	/* reports the environment ready to cn_fork_case */
	cc_tiles();
}

static int test_construction_cmds(void)
{
	void (*cases[])(void) = { cc_gradations, cc_circles, cc_circle_blend, cc_polygon, cc_blurs, cc_save, cc_unbuilt, cc_tiles_case,
		cc_rings, cc_extremes, cc_polygon_write, cc_polygon_unbuilt, cc_polygon_save, cc_polygon_v13 };
	const char *names[] = { "CC1", "CC2", "CC3", "CC4", "CC5", "CC6", "CC7", "CC8", "CC9", "CC10", "CC11", "CC12", "CC13", "CC14" };
	enum { N = sizeof(cases) / sizeof(cases[0]) };
	int failed = 0, env = 0;
	for (int i = 0; i < N; i++) {
		int r = cn_fork_case(names[i], cases[i], &cc_failures);
		failed += r == 1;
		env += r == 2;
	}
	if (env) {
		printf("CONSTRUCTION-CMDS ENVIRONMENT FAILURE: %d of %d cases could not run, %d failed\n", env, N, failed);
		return 2;
	}
	printf("CONSTRUCTION-CMDS %s: %d of %d cases failed\n", failed ? "FAIL" : "PASS", failed, N);
	return failed ? 1 : 0;
}
