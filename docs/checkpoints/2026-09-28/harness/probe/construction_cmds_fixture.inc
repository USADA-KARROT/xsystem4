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
 *       pactex loader. Hand-computed alphas and every pixel against a model.
 *   CC2 disc 102: MapNodeView's base (64x64, r 24) through the pactex loader
 *       and the same through the AIN declaration; r 1 against hand-computed
 *       coverages (the disc is not symmetric: its centre is a pixel corner);
 *       a disc cut by the left and top edges; r 0 draws nothing.
 *   CC3 disc 106 through the AIN declaration: composed on a transparent and
 *       on an opaque base.
 *   CC4 polygon 97 through the AIN declaration: a rectangle's coverage
 *       computed by hand (the half columns at both ends, 4 of 8 sub-rows on
 *       the first row and 5 of 8 on the last, the column after the last
 *       vertex), a slanted bar's sum, the composition on black, and what
 *       adds nothing (丸め, a rotation, one vertex) or draws nothing (two).
 *   CC5 blurs 27 / 28 through the AIN declaration: hand-computed rows that
 *       show the last pixel's sum, the radius clamps, a rectangle, alpha
 *       untouched.
 *   CC6 PE_Save / PE_Load rebuild the same pixels from the new operations.
 *   CC7 what stays unbuilt: pactex steps with 色２ or 半徑 missing, a
 *       gradation beyond the surface, a CG that does not exist (129), and
 *       the AIN commands 29 and 129 with such a CG add no operation.
 *   CC8 the tiling itself (parts_cp_tile, by dlsym): a 3x2 tile over a
 *       rectangle, cut at its right and bottom edges and by the surface.
 *   CC9 ring 52 through the AIN declaration: r 2 with width 1 against
 *       hand-computed coverages (the hole in the middle, the lopsided ends
 *       of the centre row), a width that leaves no hole, a ring cut by the
 *       edges, r 0; every pixel against the model.
 * Functions and types the fix adds are reached through the AIN declaration
 * or dlsym, so the file builds before the fix.
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
	struct ex *e = lb_activity("ConstructionGrad", 2, &kids);
	kids->children[0] = cc_parts("Fades", CC_N(cc_fades));
	kids->children[1] = cc_parts("Rows", CC_N(cc_rows));
	int act = cn_load("ConstructionGrad", e);
	int fades = cn_no(act, "Fades"), rows = cn_no(act, "Rows");
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

static void cc_tiles_case(void)
{
	cn_gl_setup();	/* reports the environment ready to cn_fork_case */
	cc_tiles();
}

static int test_construction_cmds(void)
{
	void (*cases[])(void) = { cc_gradations, cc_circles, cc_circle_blend, cc_polygon, cc_blurs, cc_save, cc_unbuilt, cc_tiles_case,
		cc_rings };
	const char *names[] = { "CC1", "CC2", "CC3", "CC4", "CC5", "CC6", "CC7", "CC8", "CC9" };
	int failed = 0, env = 0;
	for (int i = 0; i < 9; i++) {
		int r = cn_fork_case(names[i], cases[i], &cc_failures);
		failed += r == 1;
		env += r == 2;
	}
	if (env) {
		printf("CONSTRUCTION-CMDS ENVIRONMENT FAILURE: %d of 9 cases could not run, %d failed\n", env, failed);
		return 2;
	}
	printf("CONSTRUCTION-CMDS %s: %d of 9 cases failed\n", failed ? "FAIL" : "PASS", failed);
	return failed ? 1 : 0;
}
