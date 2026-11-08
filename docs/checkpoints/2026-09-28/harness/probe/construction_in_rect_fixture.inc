/* construction-in-rect: the construction commands 103, 107 and 121, which the
 * script adds through PartsEngine.AddPartsConstructionProcess (only from
 * PositionView@BuildCursor and PositionView@BuildParts: the position dots of
 * the status and the worker operation screens), as the original runs them
 * (static disassembly only; executor 0x4fa1d0, table 0x4fb7d8; 0x59a730
 * copies ArrayInt [6] [7] [10] [11] to 先矩形 X, Y, W, H (+0x1c, +0x20,
 * +0x2c, +0x30), [12..15] to 色１ (+0x34..+0x40), [37] to 回転角度 (+0xd8),
 * [38] and [39] to 円弧角度 (+0xdc, +0xe0)):
 *   - 103 円塗りつぶし：矩形指定（アルファのみ）(0x507790): fails on a surface
 *     without alpha (0x5abb80); succeeds and draws nothing when W or H is not
 *     positive (0x5077db, 0x5077e3); otherwise the diameter D is the larger
 *     of W and H (0x5077ec, cmovge), the centre (X + 0.5 + D/2,
 *     Y + 0.5 + D/2) and the radius D/2, in doubles; the scan of command 102
 *     (0x4cfb40: positions centre - r .. centre + r in steps of 1.0 on both
 *     axes, the pixel the position truncated towards zero, the coverage
 *     0x4e6530 of the 8x8 subsamples (k/8 right of and below the position)
 *     within the radius, as n * 255 / 64) and 102's writer (vtable 0x80eabc
 *     -> 0x4cbbd0 -> 0x5b0a10): the alpha becomes A * cov / 255, clamped.
 *   - 107 円塗りつぶし：矩形指定（アルファブレンド）(0x507f80): command 106's
 *     staging and composition (a transparent surface of the same size,
 *     0x5a94d0; vtable 0x80ead8 -> 0x4cbb20 -> 0x5b0b80 writes (R, G, B,
 *     A * cov / 255), each clamped; 0x5acc00 -> 0x494420 composes:
 *     c += ((src - c) * sa) >> 8, a = max(a, sa)) on 103's geometry. No
 *     surface: fails; W or H not positive: succeeds, draws nothing; nothing
 *     written: fails.
 *   - 121 扇形塗りつぶし：矩形指定（ピクセル＆アルファ）(0x5092a0 -> 0x5b0760):
 *     command 122's sector scan (0x4d0150, 0x4d0280: the 2r x 2r positions
 *     centre - r + k, r the larger radius, kept when the angle of the
 *     position from the centre, in 1/65536 turns and truncated, lies in
 *     [start, start + sweep] (0x4eb0c0; 0x4eae00 / 0x4e5e50: degrees modulo
 *     360, a range that ends before it starts is the union of its two parts,
 *     a negative start or sweep keeps nothing), the coverage 0x4e6680 of the
 *     8x8 subsamples inside the ellipse, each axis multiplied by the
 *     reciprocal of its radius) on the ellipse rx = W/2, ry = H/2 around
 *     (X + 0.5 + W/2, Y + 0.5 + H/2), in doubles, with command 100's writer
 *     (vtable 0x80ec98 -> 0x4d0510 -> 0x5b0b80): every covered pixel becomes
 *     (R, G, B, A * cov / 255), each clamped, the alpha only on a surface
 *     with one. The handler does not ask for alpha; W or H not positive:
 *     succeeds, draws nothing.
 * Before the fix the three commands added no operation (the default branch).
 * The expected pixels come from a model of these routines written here from
 * the disassembly (doubles, as the original computes them), checked against
 * hand-computed pixels.
 *
 * Cases (each in its own fork, with the construction fixture's GL setup):
 *   IR1 103: PositionView's cursor (12x12, (255,255,64,0), the rectangle
 *       (0,0,12,12), A 200), which adds one operation, of its own type;
 *       hand-computed edges (the subsamples lean right and down: the left
 *       column's middle 178, the right column's 200); W not H: (2,3,10,6)
 *       and (2,3,6,10) give the same disc of D 10 around (7.5,8.5), which
 *       reaches rows below the rectangle; a disc cut by the left and top
 *       edges; the colour is not touched.
 *   IR2 107: one of PositionView's dots ((6,6,12,12), (200,200,200,128) on
 *       (0,0,0,80): the centre (100,100,100,128), the left edge
 *       (88,88,88,113)), which adds one operation, of its own type; on
 *       transparent white; W not H; cut by the edges.
 *   IR3 121: the two half discs of PositionView's ends ((0,0,30,30) with 90 +
 *       180 and 270 + 180; the first adds one operation, of its own type),
 *       the centre pixel (15,15) belongs to the right half only; the
 *       quarter 0 + 90 on an opaque base: the colour and the alpha are
 *       replaced, not blended; W not H: an ellipse (rx 10, ry 5),
 *       not a disc of the larger size; 360 + 90 is 0 + 90 (degrees modulo
 *       360) and a sweep of 0 keeps the positions on its start's angle; the
 *       start in 1/65536 turns is computed in doubles (166 is 30219, not
 *       166 * 182).
 *   IR4 PositionView as the game builds it: the bar with five dots (114x30)
 *       in the status screen's colours and in the worker operation screen's
 *       (the steps a run on the game logged), with three (78x30), and the
 *       cursor, every pixel against the model.
 *   IR5 odd sizes: D 7 (centre on a pixel corner, half-integer radius) for
 *       103 and 107, W 7 / H 5 for 121.
 *   IR6 values outside 0..255: clamped where they are written (the alpha
 *       after the division; A 999 scales before it is clamped, A -50 is 0).
 *   IR7 121 on a surface without alpha (command 1): the colour of the
 *       covered pixels is written, the alpha is not; 103 there writes
 *       nothing (the original fails the step, 0x5abb80).
 *   IR8 PE_Save / PE_Load rebuild the same pixels; the save is XPE version
 *       14 (the operations of the three commands are new types).
 *   IR9 (guard) what draws nothing: W or H 0 or negative (and the step
 *       after it is still done, as the original succeeds), a figure far
 *       outside the surface, a negative start or sweep (121); and limits of
 *       xsystem4's, not of the original: sizes above the circle and sector
 *       operations' radius limit (1024; D, W or H 2049 and more), a start or
 *       sweep above 360 and a rotation (121) draw nothing, as 122 does.
 *   IR10 (guard) under an AIN before version 14 the three commands add no
 *       operation.
 *   IR11 (guard) the neighbours are unchanged: 102 and 106 add one operation
 *       of their own type and draw the model's disc, 97 adds its polygon
 *       operation, and 100, 101, 104, 105, 108, 118, 119, 120, 122, 123
 *       and 200 still add no operation through the AIN declaration. Also
 *       the model's sector scan against xsystem4's 122 (added straight):
 *       with the base's colour as 121's, the model writes the alpha 122
 *       writes (six sectors, an ellipse among them).
 *   IR12 the positions half a pixel left of or above the surface: they are
 *       truncated towards zero onto its first column or row (0x4cfc39,
 *       0x4d03ba). 103 and 107 at (-9,-1,8,8) and (-1,-9,8,8) write only
 *       the column (row) 0 from their last position, -0.5; 121 centred on
 *       x = 0.5 (y = 0.5) with a wedge pointing left (up) leaves the
 *       position 0.5 outside the wedge, so its column (row) 0 holds only the
 *       writes of the positions -0.5. 121 with one radius a half and the
 *       other whole has whole positions on that axis: -1 and the surface's
 *       width (height) are left out as any pixel outside it (0x5b0b80).
 *   IR13 the largest figures xsystem4 draws: D, W and H 2048 (the original
 *       draws any size).
 * IR1-IR8, IR12 and IR13 fail before the fix (the commands add nothing);
 * IR9-IR11 pass. The commands read no string, so every case expects the same under
 * the default (SJIS) string rule and under GBK.
 *
 * Not covered: the step failures that skip the later steps in the original
 * (103 on a surface without alpha, 107 that writes no pixel): xsystem4 skips
 * such an operation and goes on, as it does for 102 and 106; 107 on a surface
 * without alpha (0x5acc00 there was not read); a rotation that is a multiple
 * of 360 (the original draws it as 0); sweeps and starts beyond one turn; an
 * angle on an exact boundary decided by the last bit of atan2 (the model
 * uses the host's atan2, as xsystem4 does); figures larger than xsystem4's
 * limits (a D, W or H above 2048), which the original draws and IR9 pins as
 * drawing nothing.
 * Only functions that exist before the fix are used, so before-check.sh
 * builds the file on the parent.
 */

#define IR_CHECK XC_CHECK

/* --- setting up a construction through the AIN declaration --- */
struct ir_cmd {
	int cmd;
	int x, y, w, h;
	int c[4];
	int full, radius, angle, start, sweep;
	int nr_pos;
	const int *pos;
};

static int ir_int_array(int n, const int *v)
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
static void ir_add(int no, const struct ir_cmd *c)
{
	int v[40] = { 0 };
	v[0] = c->cmd;
	v[6] = c->x; v[7] = c->y; v[10] = c->w; v[11] = c->h;
	for (int i = 0; i < 4; i++)
		v[12 + i] = c->c[i];
	v[30] = c->full; v[32] = c->radius; v[33] = c->radius;
	v[37] = c->angle; v[38] = c->start; v[39] = c->sweep;
	int ints = ir_int_array(40, v);
	struct page *fa = alloc_page(ARRAY_PAGE, AIN_ARRAY_FLOAT, 2);
	int floats = heap_alloc_slot(VM_PAGE);
	heap_set_page(floats, fa);
	struct page *sa = alloc_page(ARRAY_PAGE, AIN_ARRAY_STRING, 2);
	sa->values[0].i = sf_str("", -1);
	sa->values[1].i = sf_str("", -1);
	int strs = heap_alloc_slot(VM_PAGE);
	heap_set_page(strs, sa);
	int pos = ir_int_array(c->nr_pos * 2, c->pos);
	ri_hll("AddPartsConstructionProcess", 6, (union vm_value[]){
		RI_I(no), RI_I(ints), RI_I(floats), RI_I(strs), RI_I(pos), RI_I(1) });
}

/* Commands 0 (w x h) and 6 over the whole surface. */
static void ir_new(int no, int w, int h, int r, int g, int b, int a)
{
	parts_get(no);
	ir_add(no, &(struct ir_cmd){ .cmd = 0, .w = w, .h = h });
	ir_add(no, &(struct ir_cmd){ .cmd = 6, .full = 1, .c = { r, g, b, a } });
}

static void ir_circle(int no, int cmd, int x, int y, int w, int h, int r, int g, int b, int a)
{
	ir_add(no, &(struct ir_cmd){ .cmd = cmd, .x = x, .y = y, .w = w, .h = h, .c = { r, g, b, a } });
}

static void ir_pie(int no, int x, int y, int w, int h, int start, int sweep, int r, int g, int b, int a)
{
	ir_add(no, &(struct ir_cmd){ .cmd = 121, .x = x, .y = y, .w = w, .h = h, .c = { r, g, b, a },
				    .start = start, .sweep = sweep });
}

/* The type of the last operation of the normal state, or -1. */
static int ir_last_type(struct parts *p)
{
	if (!p || p->states[0].type != PARTS_CONSTRUCTION_PROCESS)
		return -1;
	struct parts_cp_op *op, *last = NULL;
	TAILQ_FOREACH(op, &p->states[0].cproc.ops, entry)
		last = op;
	return last ? (int)last->type : -1;
}

static int ir_nr_ops(int no)
{
	struct parts *p = parts_try_get(no);
	if (!p || p->states[0].type != PARTS_CONSTRUCTION_PROCESS)
		return -1;
	int n = 0;
	struct parts_cp_op *op;
	TAILQ_FOREACH(op, &p->states[0].cproc.ops, entry)
		n++;
	return n;
}

/* Adds `c` and checks that it added exactly one operation, of the type
 * `type`: the operations of 103, 107 and 121 are 32, 33 and 34 (numbers,
 * since the tree before the fix has no names for them). */
static void ir_one_op(int no, const struct ir_cmd *c, int type, const char *what)
{
	const int n = ir_nr_ops(no);
	ir_add(no, c);
	const int added = ir_nr_ops(no) - n;
	const int last = ir_last_type(parts_try_get(no));
	IR_CHECK(added == 1 && last == type,
		 "%s: %d operation(s) added, the last of type %d, not one of type %d", what, added, last,
		 type);
}

/* The built pixels of parts `no`, w x h (to be freed), or NULL with a failure. */
static uint8_t *ir_pixels(int no, int w, int h, const char *what)
{
	PE_BuildPartsConstructionProcess(no, 1);
	struct parts *p = parts_try_get(no);
	struct parts_state *s = p ? &p->states[0] : NULL;
	bool built = s && s->type == PARTS_CONSTRUCTION_PROCESS && s->common.texture.handle
		&& s->common.texture.w == w && s->common.texture.h == h;
	IR_CHECK(built, "%s: not a built %dx%d construction", what, w, h);
	return built ? gfx_get_pixels(&s->common.texture) : NULL;
}

/* Every pixel of `got` (freed here) against `model`; at most 4 reported. */
static void ir_compare(uint8_t *got, int w, int h, const uint8_t *model, const char *what)
{
	if (!got)
		return;
	int bad = 0;
	for (int i = 0; i < w * h; i++) {
		if (!memcmp(got + i * 4, model + i * 4, 4))
			continue;
		if (bad++ < 4)
			IR_CHECK(0, "%s (%d,%d) = (%d,%d,%d,%d), want (%d,%d,%d,%d)", what, i % w, i / w,
				 got[i * 4], got[i * 4 + 1], got[i * 4 + 2], got[i * 4 + 3],
				 model[i * 4], model[i * 4 + 1], model[i * 4 + 2], model[i * 4 + 3]);
	}
	IR_CHECK(bad == 0, "%s: %d of %d pixels differ from the model", what, bad, w * h);
	free(got);
}

static void ir_expect(int no, int w, int h, const uint8_t *model, const char *what)
{
	ir_compare(ir_pixels(no, w, h, what), w, h, model, what);
}

/* One pixel of a model or of built pixels, by hand. */
static void ir_px(const uint8_t *px, int w, int x, int y, int r, int g, int b, int a, const char *what)
{
	if (!px)
		return;
	const uint8_t *p = px + (y * w + x) * 4;
	IR_CHECK(p[0] == r && p[1] == g && p[2] == b && p[3] == a, "%s: (%d,%d) = (%d,%d,%d,%d), want (%d,%d,%d,%d)",
		 what, x, y, p[0], p[1], p[2], p[3], r, g, b, a);
}

/* --- the model, from the disassembly --- */
static int ir_clamp(int v) { return v < 0 ? 0 : v > 255 ? 255 : v; }

static void ir_fill(uint8_t *px, int w, int h, int x, int y, int fw, int fh, int r, int g, int b, int a)
{
	for (int j = y < 0 ? 0 : y; j < y + fh && j < h; j++)
		for (int i = x < 0 ? 0 : x; i < x + fw && i < w; i++) {
			uint8_t *p = px + (j * w + i) * 4;
			p[0] = r; p[1] = g; p[2] = b; p[3] = a;
		}
}

/* 0x4e6530: the subsamples (ox + i/8, oy + j/8) within the radius
 * (x * x + y * y <= r * r), as n * 255 / 64. */
static int ir_circle_cov(double ox, double oy, double rr)
{
	int n = 0;
	for (int j = 0; j < 8; j++) {
		double y = (double)j * 0.125 + oy;
		y = y * y;
		for (int i = 0; i < 8; i++) {
			const double x = ox + (double)i * 0.125;
			if (x * x + y <= rr)
				n++;
		}
	}
	return n > 0 ? n * 255 / 64 : 0;
}

typedef void (*ir_write_fn)(uint8_t *px, int w, int x, int y, int cov);

/* 0x4cfb40 on a w x h surface; the writers drop pixels outside it (0x5b0a10,
 * 0x5b0b80 check 0 <= x < w, 0 <= y < h). */
static void ir_circle_walk(double cx, double cy, double r, int w, int h, ir_write_fn fn, uint8_t *px)
{
	if (!(r > 0.0))
		return;
	const double rr = r * r;
	for (double y = cy - r; y <= cy + r; y += 1.0) {
		for (double x = cx - r; x <= cx + r; x += 1.0) {
			const int cov = ir_circle_cov(x - cx, y - cy, rr);
			if (cov <= 0)
				continue;
			const int ix = (int)x, iy = (int)y;	/* truncated towards zero */
			if (ix < 0 || ix >= w || iy < 0 || iy >= h)
				continue;
			fn(px, w, ix, iy, cov);
		}
	}
}

/* 103 / 107's geometry: D = max(W, H), centre X + 0.5 + D/2. */
static void ir_in_rect_walk(int X, int Y, int W, int H, int w, int h, ir_write_fn fn, uint8_t *px)
{
	if (W <= 0 || H <= 0)
		return;
	const int D = W >= H ? W : H;
	const double r = (double)D * 0.5;
	ir_circle_walk((double)X + 0.5 + r, (double)Y + 0.5 + r, r, w, h, fn, px);
}

static int ir_m_rgba[4];
static void ir_w_alpha(uint8_t *px, int w, int x, int y, int cov)
{
	px[(y * w + x) * 4 + 3] = ir_clamp(ir_m_rgba[3] * cov / 255);
}
static void ir_w_with_alpha(uint8_t *px, int w, int x, int y, int cov)
{
	uint8_t *p = px + (y * w + x) * 4;
	p[0] = ir_clamp(ir_m_rgba[0]); p[1] = ir_clamp(ir_m_rgba[1]); p[2] = ir_clamp(ir_m_rgba[2]);
	p[3] = ir_clamp(ir_m_rgba[3] * cov / 255);
}
static bool ir_m_no_alpha;
static void ir_w_rgb_maybe_alpha(uint8_t *px, int w, int x, int y, int cov)
{
	uint8_t *p = px + (y * w + x) * 4;
	p[0] = ir_clamp(ir_m_rgba[0]); p[1] = ir_clamp(ir_m_rgba[1]); p[2] = ir_clamp(ir_m_rgba[2]);
	if (!ir_m_no_alpha)
		p[3] = ir_clamp(ir_m_rgba[3] * cov / 255);
}

static void ir_m103(uint8_t *px, int w, int h, int X, int Y, int W, int H, int a)
{
	ir_m_rgba[3] = a;
	ir_in_rect_walk(X, Y, W, H, w, h, ir_w_alpha, px);
}

/* 107: staged on a transparent surface (a later write replaces an earlier
 * one), then 0x494420 over the pixels written. */
static void ir_m107(uint8_t *px, int w, int h, int X, int Y, int W, int H, int r, int g, int b, int a)
{
	uint8_t *st = calloc((size_t)w * h, 4);
	uint8_t *mark = calloc((size_t)w * h, 1);
	ir_m_rgba[0] = r; ir_m_rgba[1] = g; ir_m_rgba[2] = b; ir_m_rgba[3] = a;
	ir_in_rect_walk(X, Y, W, H, w, h, ir_w_with_alpha, st);
	for (int i = 0; i < w * h; i++)
		mark[i] = st[i * 4 + 3] || st[i * 4] || st[i * 4 + 1] || st[i * 4 + 2];
	for (int i = 0; i < w * h; i++) {
		if (!mark[i])
			continue;
		const int sa = st[i * 4 + 3];
		uint8_t *p = px + i * 4;
		for (int c = 0; c < 3; c++) {
			const int d = (st[i * 4 + c] - p[c]) * sa;
			p[c] = p[c] + (d >= 0 ? d / 256 : -((-d + 255) / 256));	/* sar 8 */
		}
		if (sa > p[3])
			p[3] = sa;
	}
	free(st);
	free(mark);
}

/* 0x4e5e50: degrees (not negative here) to 1/65536 turns. */
static int ir_turn(int deg)
{
	double t = (double)(deg % 360) * 182.04444444444445;
	while (t < 0.0)
		t += 65536.0;
	while (t > 65536.0)
		t -= 65536.0;
	return (int)t;
}

/* 0x4eb0c0 */
static bool ir_angle_in(int a, int start, int end)
{
	if (start < 0 || end < 0)
		return false;
	if (start > end)
		return a >= start || a <= end;
	return a >= start && a <= end;
}

/* 121: 0x4d0280 with 0x4e6680 (rotation 0: cos 1, sin 0) and 0x4d0510. */
static void ir_m121(uint8_t *px, int w, int h, int X, int Y, int W, int H, int start, int sweep,
		int r, int g, int b, int a, bool no_alpha)
{
	if (W <= 0 || H <= 0)
		return;
	const double cx = (double)X + 0.5 + (double)W * 0.5, cy = (double)Y + 0.5 + (double)H * 0.5;
	const double rx = (double)W * 0.5, ry = (double)H * 0.5;
	int st = -1, en = -1;
	if (start >= 0 && sweep >= 0) {
		st = ir_turn(start);
		en = ir_turn(start + sweep);
	}
	const double rr = ry > rx ? ry : rx;
	const int size = (int)(rr + rr);
	const double irx = 1.0 / rx, iry = 1.0 / ry;
	ir_m_rgba[0] = r; ir_m_rgba[1] = g; ir_m_rgba[2] = b; ir_m_rgba[3] = a;
	ir_m_no_alpha = no_alpha;
	for (int row = 0; row < size; row++) {
		const double dy = (double)row - rr;
		const double y = cy + dy;
		for (int col = 0; col < size; col++) {
			const double dx = (double)col - rr;
			const double x = cx + dx;
			double t = atan2(y - cy, x - cx) * 10430.378350470453;
			while (t < 0.0)
				t += 65536.0;
			while (t > 65536.0)
				t -= 65536.0;
			if (!ir_angle_in((int)t, st, en))
				continue;
			int n = 0;
			for (int j = 0; j < 8; j++) {
				const double v = ((double)j * 0.125 + dy) * iry;
				const double vv = v * v;
				for (int i = 0; i < 8; i++) {
					const double u = (dx + (double)i * 0.125) * irx;
					const double uu = u * u;
					if (!(1.0 < vv + uu))
						n++;
				}
			}
			if (n <= 0)
				continue;
			const int ix = (int)x, iy = (int)y;
			if (ix < 0 || ix >= w || iy < 0 || iy >= h)
				continue;
			ir_w_rgb_maybe_alpha(px, w, ix, iy, n * 255 / 64);
		}
	}
}

static uint8_t *ir_model_new(int w, int h, int r, int g, int b, int a)
{
	uint8_t *px = malloc((size_t)w * h * 4);
	ir_fill(px, w, h, 0, 0, w, h, r, g, b, a);
	return px;
}

static void ir_setup(void)
{
	cn_gl_setup();
	ri_postlink();
	xc_ready();
}

/* --- IR1 --- */
static void ir1_amap(void)
{
	ir_setup();
	/* PositionView@BuildCursor: Create(12, 12), Fill(255, 255, 64, 0),
	 * FillCircle(CASRect(0, 0, 12, 12), 200). */
	const int CUR = 10701;
	ir_new(CUR, 12, 12, 255, 255, 64, 0);
	ir_one_op(CUR, &(struct ir_cmd){ .cmd = 103, .w = 12, .h = 12, .c = { 0, 0, 0, 200 } }, 32,
		  "IR1 103");
	uint8_t *m = ir_model_new(12, 12, 255, 255, 64, 0);
	ir_m103(m, 12, 12, 0, 0, 12, 12, 200);
	/* By hand: the centre is (6.5, 6.5), r 6. Pixel 0's position 0.5 is
	 * 6 left of it: of its subsamples (-6 + i/8, j/8) the row j = 0 lies
	 * within 6 (8) and the others without i = 0 (7 each): 57, 227, 178.
	 * Pixel 11's (+5 + i/8) are all within: 200. Pixel 12 is outside. */
	ir_px(m, 12, 6, 6, 255, 255, 64, 200, "IR1 the model itself, centre");
	ir_px(m, 12, 0, 6, 255, 255, 64, 178, "IR1 the model itself, left");
	ir_px(m, 12, 11, 6, 255, 255, 64, 200, "IR1 the model itself, right");
	ir_px(m, 12, 6, 0, 255, 255, 64, 178, "IR1 the model itself, top");
	ir_px(m, 12, 6, 11, 255, 255, 64, 200, "IR1 the model itself, bottom");
	ir_px(m, 12, 0, 0, 255, 255, 64, 0, "IR1 the model itself, corner");
	uint8_t *got = ir_pixels(CUR, 12, 12, "IR1 the cursor");
	ir_px(got, 12, 0, 6, 255, 255, 64, 178, "IR1 the cursor, left");
	ir_px(got, 12, 11, 6, 255, 255, 64, 200, "IR1 the cursor, right");
	ir_compare(got, 12, 12, m, "IR1 the cursor");
	free(m);

	/* W not H: D is the larger, for both coordinates of the centre. */
	const int WIDE = 10702, TALL = 10703;
	ir_new(WIDE, 16, 16, 10, 20, 30, 255);
	ir_circle(WIDE, 103, 2, 3, 10, 6, 0, 0, 0, 128);
	ir_new(TALL, 16, 16, 10, 20, 30, 255);
	ir_circle(TALL, 103, 2, 3, 6, 10, 0, 0, 0, 128);
	m = ir_model_new(16, 16, 10, 20, 30, 255);
	ir_m103(m, 16, 16, 2, 3, 10, 6, 128);
	/* Centre (7.5, 8.5), r 5: (2,8) as the cursor's left edge (113);
	 * (7,12) is inside although 先矩形 ends at row 8; (7,13) holds one
	 * subsample (1); (7,2) is outside. */
	ir_px(m, 16, 2, 8, 10, 20, 30, 113, "IR1 the model itself, W > H");
	ir_px(m, 16, 7, 12, 10, 20, 30, 128, "IR1 the model itself, W > H");
	ir_px(m, 16, 7, 13, 10, 20, 30, 1, "IR1 the model itself, W > H");
	ir_px(m, 16, 7, 2, 10, 20, 30, 255, "IR1 the model itself, W > H");
	got = ir_pixels(WIDE, 16, 16, "IR1 (2,3,10,6)");
	ir_px(got, 16, 7, 12, 10, 20, 30, 128, "IR1 (2,3,10,6): below the rectangle");
	ir_compare(got, 16, 16, m, "IR1 (2,3,10,6)");
	ir_expect(TALL, 16, 16, m, "IR1 (2,3,6,10)");
	free(m);

	/* Cut by the left and top edges. */
	const int CUT = 10704;
	ir_new(CUT, 8, 8, 1, 2, 3, 77);
	ir_circle(CUT, 103, -3, -4, 8, 8, 0, 0, 0, 255);
	m = ir_model_new(8, 8, 1, 2, 3, 77);
	ir_m103(m, 8, 8, -3, -4, 8, 8, 255);
	ir_expect(CUT, 8, 8, m, "IR1 cut by the edges");
	free(m);
}

/* --- IR2 --- */
static void ir2_blend(void)
{
	ir_setup();
	const int DOT = 10711;
	ir_new(DOT, 24, 24, 0, 0, 0, 80);
	ir_one_op(DOT, &(struct ir_cmd){ .cmd = 107, .x = 6, .y = 6, .w = 12, .h = 12,
				       .c = { 200, 200, 200, 128 } }, 33, "IR2 107");
	uint8_t *m = ir_model_new(24, 24, 0, 0, 0, 80);
	ir_m107(m, 24, 24, 6, 6, 12, 12, 200, 200, 200, 128);
	/* Centre: staged 128, 0 + (200 * 128 >> 8) = 100, max(80, 128).
	 * Left edge (6,12): coverage 227, staged 128 * 227 / 255 = 113,
	 * 200 * 113 >> 8 = 88. */
	ir_px(m, 24, 12, 12, 100, 100, 100, 128, "IR2 the model itself, centre");
	ir_px(m, 24, 6, 12, 88, 88, 88, 113, "IR2 the model itself, left");
	ir_px(m, 24, 17, 12, 100, 100, 100, 128, "IR2 the model itself, right");
	ir_px(m, 24, 18, 12, 0, 0, 0, 80, "IR2 the model itself, beyond");
	uint8_t *got = ir_pixels(DOT, 24, 24, "IR2 a dot");
	ir_px(got, 24, 12, 12, 100, 100, 100, 128, "IR2 a dot, centre");
	ir_compare(got, 24, 24, m, "IR2 a dot");
	free(m);

	const int WHITE = 10712;
	ir_new(WHITE, 16, 16, 255, 255, 255, 0);
	ir_circle(WHITE, 107, 1, 2, 12, 12, 0, 64, 128, 200);
	m = ir_model_new(16, 16, 255, 255, 255, 0);
	ir_m107(m, 16, 16, 1, 2, 12, 12, 0, 64, 128, 200);
	/* 255 + ((0 - 255) * 200 >> 8) = 255 - 200 = 55 (sar rounds down). */
	ir_px(m, 16, 7, 8, 55, 105, 155, 200, "IR2 the model itself, on white");
	ir_expect(WHITE, 16, 16, m, "IR2 on transparent white");
	free(m);

	const int WIDE = 10713, TALL = 10714;
	ir_new(WIDE, 16, 16, 30, 30, 30, 255);
	ir_circle(WIDE, 107, 2, 3, 10, 6, 250, 0, 0, 255);
	ir_new(TALL, 16, 16, 30, 30, 30, 255);
	ir_circle(TALL, 107, 2, 3, 6, 10, 250, 0, 0, 255);
	m = ir_model_new(16, 16, 30, 30, 30, 255);
	ir_m107(m, 16, 16, 2, 3, 10, 6, 250, 0, 0, 255);
	/* 30 + ((250 - 30) * 255 >> 8) = 249, 30 + (-30 * 255 >> 8) = 0. */
	ir_px(m, 16, 7, 12, 249, 0, 0, 255, "IR2 the model itself, W > H");
	ir_expect(WIDE, 16, 16, m, "IR2 (2,3,10,6)");
	ir_expect(TALL, 16, 16, m, "IR2 (2,3,6,10)");
	free(m);

	const int CUT = 10715;
	ir_new(CUT, 8, 8, 9, 9, 9, 40);
	ir_circle(CUT, 107, -5, -2, 8, 8, 255, 0, 186, 255);
	m = ir_model_new(8, 8, 9, 9, 9, 40);
	ir_m107(m, 8, 8, -5, -2, 8, 8, 255, 0, 186, 255);
	ir_expect(CUT, 8, 8, m, "IR2 cut by the edges");
	free(m);
}

/* --- IR3 --- */
static void ir3_pie(void)
{
	ir_setup();
	/* PositionView@BuildParts' ends: FillPie(CASRect(0, 0, 30, 30), 90,
	 * 180, colour, 0) and the same at the right with 270, 180. */
	const int LEFT = 10721, RIGHT = 10722;
	ir_new(LEFT, 30, 30, 0, 0, 0, 0);
	ir_one_op(LEFT, &(struct ir_cmd){ .cmd = 121, .w = 30, .h = 30, .c = { 0, 0, 0, 80 },
					.start = 90, .sweep = 180 }, 34, "IR3 121");
	ir_new(RIGHT, 30, 30, 0, 0, 0, 0);
	ir_pie(RIGHT, 0, 0, 30, 30, 270, 180, 0, 0, 0, 80);
	uint8_t *ml = ir_model_new(30, 30, 0, 0, 0, 0), *mr = ir_model_new(30, 30, 0, 0, 0, 0);
	ir_m121(ml, 30, 30, 0, 0, 30, 30, 90, 180, 0, 0, 0, 80, false);
	ir_m121(mr, 30, 30, 0, 0, 30, 30, 270, 180, 0, 0, 0, 80, false);
	/* The centre (15.5, 15.5), r 15. Pixel (0,15) is 15 left of it: the
	 * subsamples (-15 + i/8, j/8), 57 inside, 227, 80 * 227 / 255 = 71.
	 * (15,15) is at angle 0: the right half (270 .. 450, i.e. .. 90) has it,
	 * the left (90 .. 270) not. */
	ir_px(ml, 30, 0, 15, 0, 0, 0, 71, "IR3 the model itself, left end");
	ir_px(ml, 30, 15, 15, 0, 0, 0, 0, "IR3 the model itself, left half at (15,15)");
	ir_px(ml, 30, 14, 15, 0, 0, 0, 80, "IR3 the model itself, left half at (14,15)");
	ir_px(ml, 30, 29, 15, 0, 0, 0, 0, "IR3 the model itself, left half at (29,15)");
	ir_px(mr, 30, 15, 15, 0, 0, 0, 80, "IR3 the model itself, right half at (15,15)");
	ir_px(mr, 30, 14, 15, 0, 0, 0, 0, "IR3 the model itself, right half at (14,15)");
	uint8_t *got = ir_pixels(LEFT, 30, 30, "IR3 the left half");
	ir_px(got, 30, 0, 15, 0, 0, 0, 71, "IR3 the left half's end");
	ir_compare(got, 30, 30, ml, "IR3 the left half");
	ir_expect(RIGHT, 30, 30, mr, "IR3 the right half");
	free(ml);
	free(mr);

	/* The quarter 0 + 90 (clockwise from +x: right and down) on an opaque
	 * base: the colour and the alpha are replaced. */
	const int QUARTER = 10723;
	ir_new(QUARTER, 16, 16, 10, 200, 30, 255);
	ir_pie(QUARTER, 0, 0, 16, 16, 0, 90, 255, 0, 186, 128);
	uint8_t *m = ir_model_new(16, 16, 10, 200, 30, 255);
	ir_m121(m, 16, 16, 0, 0, 16, 16, 0, 90, 255, 0, 186, 128, false);
	ir_px(m, 16, 8, 8, 255, 0, 186, 128, "IR3 the model itself, quarter");
	ir_px(m, 16, 15, 8, 255, 0, 186, 128, "IR3 the model itself, quarter");
	ir_px(m, 16, 7, 8, 10, 200, 30, 255, "IR3 the model itself, quarter");
	ir_px(m, 16, 8, 7, 10, 200, 30, 255, "IR3 the model itself, quarter");
	ir_px(m, 16, 14, 14, 10, 200, 30, 255, "IR3 the model itself, quarter");
	got = ir_pixels(QUARTER, 16, 16, "IR3 a quarter");
	ir_px(got, 16, 12, 12, 255, 0, 186, 128, "IR3 a quarter, replaced");
	ir_compare(got, 16, 16, m, "IR3 a quarter");

	/* The ends of one turn: 360 + 90 is the quarter of 0 + 90 (degrees
	 * modulo 360, 0x4e5e50); a sweep of 0 keeps the positions on the angle
	 * of its start (0: the centre's row, right of it). */
	const int Q360 = 10727, RAY = 10728;
	ir_new(Q360, 16, 16, 10, 200, 30, 255);
	ir_pie(Q360, 0, 0, 16, 16, 360, 90, 255, 0, 186, 128);
	ir_expect(Q360, 16, 16, m, "IR3 360 + 90");
	free(m);
	ir_new(RAY, 16, 16, 10, 200, 30, 255);
	ir_pie(RAY, 0, 0, 16, 16, 0, 0, 255, 0, 186, 128);
	m = ir_model_new(16, 16, 10, 200, 30, 255);
	ir_m121(m, 16, 16, 0, 0, 16, 16, 0, 0, 255, 0, 186, 128, false);
	ir_px(m, 16, 12, 8, 255, 0, 186, 128, "IR3 the model itself, a sweep of 0");
	ir_px(m, 16, 12, 9, 10, 200, 30, 255, "IR3 the model itself, a sweep of 0");
	ir_px(m, 16, 7, 8, 10, 200, 30, 255, "IR3 the model itself, a sweep of 0");
	ir_expect(RAY, 16, 16, m, "IR3 0 + 0");
	free(m);

	/* W not H: an ellipse of rx 10, ry 5 around (12.5, 9.5), in two halves. */
	const int OVAL = 10724;
	ir_new(OVAL, 24, 16, 10, 200, 30, 255);
	ir_pie(OVAL, 2, 4, 20, 10, 0, 180, 255, 0, 186, 128);
	ir_pie(OVAL, 2, 4, 20, 10, 180, 180, 255, 0, 186, 128);
	m = ir_model_new(24, 16, 10, 200, 30, 255);
	ir_m121(m, 24, 16, 2, 4, 20, 10, 0, 180, 255, 0, 186, 128, false);
	ir_m121(m, 24, 16, 2, 4, 20, 10, 180, 180, 255, 0, 186, 128, false);
	/* Row 3 is above the ellipse (a disc of the larger size reaches it);
	 * row 14 holds one subsample; column 22 is right of it. */
	ir_px(m, 24, 12, 9, 255, 0, 186, 128, "IR3 the model itself, ellipse");
	ir_px(m, 24, 12, 3, 10, 200, 30, 255, "IR3 the model itself, ellipse");
	ir_px(m, 24, 12, 14, 255, 0, 186, 1, "IR3 the model itself, ellipse");
	ir_px(m, 24, 22, 9, 10, 200, 30, 255, "IR3 the model itself, ellipse");
	got = ir_pixels(OVAL, 24, 16, "IR3 an ellipse");
	ir_px(got, 24, 12, 3, 10, 200, 30, 255, "IR3 an ellipse: row 3 untouched");
	ir_compare(got, 24, 16, m, "IR3 an ellipse");
	free(m);

	/* A sector with edges off the axes, and one cut by the edges. */
	const int SLICE = 10725, CUT = 10726;
	ir_new(SLICE, 20, 20, 0, 0, 0, 0);
	ir_pie(SLICE, 1, 1, 18, 18, 30, 100, 90, 80, 70, 255);
	m = ir_model_new(20, 20, 0, 0, 0, 0);
	ir_m121(m, 20, 20, 1, 1, 18, 18, 30, 100, 90, 80, 70, 255, false);
	ir_expect(SLICE, 20, 20, m, "IR3 30 + 100");
	free(m);
	ir_new(CUT, 10, 10, 5, 5, 5, 5);
	ir_pie(CUT, -6, -3, 12, 12, 0, 360, 200, 100, 50, 255);
	ir_pie(CUT, -6, -3, 12, 12, 1, 358, 200, 100, 50, 255);
	m = ir_model_new(10, 10, 5, 5, 5, 5);
	ir_m121(m, 10, 10, -6, -3, 12, 12, 0, 360, 200, 100, 50, 255, false);
	ir_m121(m, 10, 10, -6, -3, 12, 12, 1, 358, 200, 100, 50, 255, false);
	ir_expect(CUT, 10, 10, m, "IR3 cut by the edges");
	free(m);

	/* The start in 1/65536 turns is computed in doubles, then truncated:
	 * 166 is 30219 (166 * 65536 / 360 = 30219.4), not 166 * 182 = 30212.
	 * The position (-4, 1) from the centre (4.5, 4.5), at the angle
	 * 30212.8 (42 subsamples inside), lies before that start: pixel (0, 5)
	 * keeps the base; (-4, -1), at 35323.2, is in the sector (55). */
	const int START = 10729;
	ir_new(START, 8, 8, 10, 20, 30, 255);
	ir_pie(START, 0, 0, 8, 8, 166, 90, 200, 100, 50, 255);
	m = ir_model_new(8, 8, 10, 20, 30, 255);
	ir_m121(m, 8, 8, 0, 0, 8, 8, 166, 90, 200, 100, 50, 255, false);
	ir_px(m, 8, 0, 5, 10, 20, 30, 255, "IR3 the model itself, start 166");
	ir_px(m, 8, 0, 3, 200, 100, 50, 219, "IR3 the model itself, start 166");
	ir_expect(START, 8, 8, m, "IR3 166 + 90");
	free(m);
}

/* --- IR4 --- */
/* PositionView@BuildParts for `n` positions: width 30 + 12n + 6(n - 1). */
static void ir_bar(int no, int n, const int base[4], const int dot[4])
{
	const int w = 30 + n * 12 + (n - 1) * 6;
	ir_new(no, w, 30, base[0], base[1], base[2], 0);
	ir_pie(no, 0, 0, 30, 30, 90, 180, base[0], base[1], base[2], base[3]);
	ir_pie(no, w - 30, 0, 30, 30, 270, 180, base[0], base[1], base[2], base[3]);
	ir_add(no, &(struct ir_cmd){ .cmd = 6, .x = 15, .y = 0, .w = w - 30, .h = 30, .c = { base[0], base[1], base[2], base[3] } });
	for (int i = 0; i < n; i++)
		ir_circle(no, 107, 15 + i * 18, 9, 12, 12, dot[0], dot[1], dot[2], dot[3]);
}

static uint8_t *ir_bar_model(int n, const int base[4], const int dot[4])
{
	const int w = 30 + n * 12 + (n - 1) * 6;
	uint8_t *m = ir_model_new(w, 30, base[0], base[1], base[2], 0);
	ir_m121(m, w, 30, 0, 0, 30, 30, 90, 180, base[0], base[1], base[2], base[3], false);
	ir_m121(m, w, 30, w - 30, 0, 30, 30, 270, 180, base[0], base[1], base[2], base[3], false);
	ir_fill(m, w, 30, 15, 0, w - 30, 30, base[0], base[1], base[2], base[3]);
	for (int i = 0; i < n; i++)
		ir_m107(m, w, 30, 15 + i * 18, 9, 12, 12, dot[0], dot[1], dot[2], dot[3]);
	return m;
}

static void ir4_position_view(void)
{
	ir_setup();
	/* SceneStatus' PositionView: BaseColor 0,0,0,80, PositionColor
	 * 200,200,200,128; five positions. */
	static const int base5[4] = { 0, 0, 0, 80 }, dot5[4] = { 200, 200, 200, 128 };
	const int BAR5 = 10731;
	ir_bar(BAR5, 5, base5, dot5);
	uint8_t *m = ir_bar_model(5, base5, dot5);
	ir_px(m, 114, 57, 2, 0, 0, 0, 80, "IR4 the model itself, the bar");
	ir_px(m, 114, 21, 15, 100, 100, 100, 128, "IR4 the model itself, the first dot");
	ir_px(m, 114, 93, 15, 100, 100, 100, 128, "IR4 the model itself, the last dot");
	ir_px(m, 114, 30, 15, 0, 0, 0, 80, "IR4 the model itself, between the dots");
	ir_px(m, 114, 0, 15, 0, 0, 0, 71, "IR4 the model itself, the left end");
	ir_px(m, 114, 0, 0, 0, 0, 0, 0, "IR4 the model itself, the corner");
	ir_px(m, 114, 113, 0, 0, 0, 0, 0, "IR4 the model itself, the corner");
	uint8_t *got = ir_pixels(BAR5, 114, 30, "IR4 the status screen's bar");
	ir_px(got, 114, 21, 15, 100, 100, 100, 128, "IR4 the status screen's bar, the first dot");
	ir_px(got, 114, 0, 15, 0, 0, 0, 71, "IR4 the status screen's bar, the left end");
	ir_compare(got, 114, 30, m, "IR4 the status screen's bar");
	free(m);

	/* SceneWorkerOperation's: 0,0,0,140 and 100,100,100,200; five (the
	 * steps a run on the game logged) and three. */
	static const int baseW[4] = { 0, 0, 0, 140 }, dotW[4] = { 100, 100, 100, 200 };
	const int BAR5W = 10734, BAR3 = 10732;
	ir_bar(BAR5W, 5, baseW, dotW);
	m = ir_bar_model(5, baseW, dotW);
	/* 0 + (100 * 200 >> 8) = 78, max(140, 200). */
	ir_px(m, 114, 21, 15, 78, 78, 78, 200, "IR4 the model itself, worker operation");
	ir_px(m, 114, 57, 2, 0, 0, 0, 140, "IR4 the model itself, worker operation");
	ir_expect(BAR5W, 114, 30, m, "IR4 the worker operation screen's bar of five");
	free(m);
	ir_bar(BAR3, 3, baseW, dotW);
	m = ir_bar_model(3, baseW, dotW);
	ir_px(m, 78, 21, 15, 78, 78, 78, 200, "IR4 the model itself, worker operation");
	ir_expect(BAR3, 78, 30, m, "IR4 the worker operation screen's bar of three");
	free(m);

	/* The cursor: Create(12, 12), Fill(255, 255, 64, 0), 103. */
	const int CUR = 10733;
	ir_new(CUR, 12, 12, 255, 255, 64, 0);
	ir_circle(CUR, 103, 0, 0, 12, 12, 0, 0, 0, 200);
	m = ir_model_new(12, 12, 255, 255, 64, 0);
	ir_m103(m, 12, 12, 0, 0, 12, 12, 200);
	ir_expect(CUR, 12, 12, m, "IR4 the cursor");
	free(m);
}

/* --- IR5 --- */
static void ir5_odd(void)
{
	ir_setup();
	/* D 7: centre (1 + 0.5 + 3.5) = 5, on a pixel corner; r 3.5. */
	const int A = 10741, A2 = 10742, B = 10743, P = 10744, P2 = 10745;
	ir_new(A, 10, 10, 50, 60, 70, 255);
	ir_circle(A, 103, 1, 1, 7, 7, 0, 0, 0, 160);
	ir_new(A2, 10, 10, 50, 60, 70, 255);
	ir_circle(A2, 103, 1, 1, 7, 4, 0, 0, 0, 160);
	uint8_t *m = ir_model_new(10, 10, 50, 60, 70, 255);
	ir_m103(m, 10, 10, 1, 1, 7, 7, 160);
	/* Pixel 5's position 5.5 is 0.5 right of the centre: its subsamples
	 * (0.5 + i/8, 0.5 + j/8) all lie within 3.5 (the farthest 1.375). */
	ir_px(m, 10, 5, 5, 50, 60, 70, 160, "IR5 the model itself, D 7");
	ir_expect(A, 10, 10, m, "IR5 103 (1,1,7,7)");
	ir_expect(A2, 10, 10, m, "IR5 103 (1,1,7,4)");
	free(m);
	ir_new(B, 10, 10, 50, 60, 70, 100);
	ir_circle(B, 107, 1, 1, 7, 7, 250, 250, 0, 220);
	m = ir_model_new(10, 10, 50, 60, 70, 100);
	ir_m107(m, 10, 10, 1, 1, 7, 7, 250, 250, 0, 220);
	ir_expect(B, 10, 10, m, "IR5 107 (1,1,7,7)");
	free(m);
	ir_new(P, 10, 10, 0, 0, 0, 0);
	ir_pie(P, 1, 1, 7, 5, 0, 180, 0, 128, 255, 255);
	ir_pie(P, 1, 1, 7, 5, 180, 180, 0, 128, 255, 255);
	m = ir_model_new(10, 10, 0, 0, 0, 0);
	ir_m121(m, 10, 10, 1, 1, 7, 5, 0, 180, 0, 128, 255, 255, false);
	ir_m121(m, 10, 10, 1, 1, 7, 5, 180, 180, 0, 128, 255, 255, false);
	ir_expect(P, 10, 10, m, "IR5 121 (1,1,7,5)");
	free(m);
	ir_new(P2, 10, 10, 0, 0, 0, 0);
	ir_pie(P2, 1, 1, 5, 7, 45, 90, 0, 128, 255, 255);
	m = ir_model_new(10, 10, 0, 0, 0, 0);
	ir_m121(m, 10, 10, 1, 1, 5, 7, 45, 90, 0, 128, 255, 255, false);
	ir_expect(P2, 10, 10, m, "IR5 121 (1,1,5,7)");
	free(m);
}

/* --- IR6 --- */
static void ir6_ranges(void)
{
	ir_setup();
	const int A = 10751, A0 = 10752, B = 10753, P = 10754;
	/* A 999 at a coverage of 3 (one subsample): 999 * 3 / 255 = 11 first,
	 * then clamped; inside, 999 * 255 / 255 is clamped to 255. */
	ir_new(A, 12, 12, 1, 2, 3, 40);
	ir_circle(A, 103, 1, 1, 10, 10, 0, 0, 0, 999);
	uint8_t *m = ir_model_new(12, 12, 1, 2, 3, 40);
	ir_m103(m, 12, 12, 1, 1, 10, 10, 999);
	ir_px(m, 12, 6, 6, 1, 2, 3, 255, "IR6 the model itself, A 999");
	ir_px(m, 12, 6, 11, 1, 2, 3, 11, "IR6 the model itself, A 999");
	ir_expect(A, 12, 12, m, "IR6 103 A 999");
	free(m);
	ir_new(A0, 12, 12, 1, 2, 3, 40);
	ir_circle(A0, 103, 1, 1, 10, 10, 0, 0, 0, -50);
	m = ir_model_new(12, 12, 1, 2, 3, 40);
	ir_m103(m, 12, 12, 1, 1, 10, 10, -50);
	ir_px(m, 12, 6, 6, 1, 2, 3, 0, "IR6 the model itself, A -50");
	ir_expect(A0, 12, 12, m, "IR6 103 A -50");
	free(m);
	ir_new(B, 12, 12, 100, 100, 100, 255);
	ir_circle(B, 107, 1, 1, 10, 10, 300, -5, 128, 999);
	m = ir_model_new(12, 12, 100, 100, 100, 255);
	ir_m107(m, 12, 12, 1, 1, 10, 10, 300, -5, 128, 999);
	/* Staged (255, 0, 128, 255): 100 + (155 * 255 >> 8) = 254,
	 * 100 + (-100 * 255 >> 8) = 0, 100 + (28 * 255 >> 8) = 127. */
	ir_px(m, 12, 6, 6, 254, 0, 127, 255, "IR6 the model itself, 107");
	ir_expect(B, 12, 12, m, "IR6 107 (300, -5, 128, 999)");
	free(m);
	ir_new(P, 12, 12, 100, 100, 100, 255);
	ir_pie(P, 1, 1, 10, 10, 0, 180, 300, -5, 128, 999);
	m = ir_model_new(12, 12, 100, 100, 100, 255);
	ir_m121(m, 12, 12, 1, 1, 10, 10, 0, 180, 300, -5, 128, 999, false);
	ir_px(m, 12, 6, 7, 255, 0, 128, 255, "IR6 the model itself, 121");
	ir_expect(P, 12, 12, m, "IR6 121 (300, -5, 128, 999)");
	free(m);
	const int P0 = 10755;
	ir_new(P0, 12, 12, 100, 100, 100, 255);
	ir_pie(P0, 1, 1, 10, 10, 0, 180, 10, 20, 30, -50);
	m = ir_model_new(12, 12, 100, 100, 100, 255);
	ir_m121(m, 12, 12, 1, 1, 10, 10, 0, 180, 10, 20, 30, -50, false);
	ir_px(m, 12, 6, 7, 10, 20, 30, 0, "IR6 the model itself, 121 A -50");
	ir_expect(P0, 12, 12, m, "IR6 121 A -50");
	free(m);
	const int PB = 10756;
	ir_new(PB, 12, 12, 100, 100, 100, 255);
	ir_pie(PB, 1, 1, 10, 10, 0, 180, 40, 50, 300, 200);
	m = ir_model_new(12, 12, 100, 100, 100, 255);
	ir_m121(m, 12, 12, 1, 1, 10, 10, 0, 180, 40, 50, 300, 200, false);
	ir_px(m, 12, 6, 7, 40, 50, 255, 200, "IR6 the model itself, 121 B 300");
	ir_expect(PB, 12, 12, m, "IR6 121 B 300");
	free(m);
}

/* --- IR7 --- */
static void ir7_no_alpha(void)
{
	ir_setup();
	/* Command 1: a surface without alpha. 121's writer (0x5b0b80) writes
	 * the colour of a covered pixel and the alpha only where the surface
	 * has one; the handler does not ask for it. */
	const int P = 10761;
	parts_get(P);
	ir_add(P, &(struct ir_cmd){ .cmd = 1, .w = 12, .h = 12 });
	ir_add(P, &(struct ir_cmd){ .cmd = 3, .full = 1, .c = { 40, 50, 60, 0 } });
	uint8_t *before = ir_pixels(P, 12, 12, "IR7 the surface before 121");
	if (!before)
		return;
	ir_pie(P, 0, 0, 12, 12, 0, 180, 255, 0, 186, 99);
	uint8_t *m = malloc(12 * 12 * 4);
	memcpy(m, before, 12 * 12 * 4);
	ir_m121(m, 12, 12, 0, 0, 12, 12, 0, 180, 255, 0, 186, 99, true);
	IR_CHECK(m[(8 * 12 + 6) * 4] == 255 && m[(8 * 12 + 6) * 4 + 3] == before[(8 * 12 + 6) * 4 + 3],
		 "IR7 the model itself: (6,8) = (%d,..,%d)", m[(8 * 12 + 6) * 4], m[(8 * 12 + 6) * 4 + 3]);
	uint8_t *got = ir_pixels(P, 12, 12, "IR7 121 without alpha");
	ir_px(got, 12, 6, 8, 255, 0, 186, before[(8 * 12 + 6) * 4 + 3], "IR7 121 without alpha");
	ir_compare(got, 12, 12, m, "IR7 121 without alpha");
	free(m);
	free(before);

	/* 103 there: the original fails the step (0x5abb80), nothing is
	 * written. */
	const int Q = 10762;
	parts_get(Q);
	ir_add(Q, &(struct ir_cmd){ .cmd = 1, .w = 12, .h = 12 });
	ir_add(Q, &(struct ir_cmd){ .cmd = 3, .full = 1, .c = { 40, 50, 60, 0 } });
	uint8_t *q0 = ir_pixels(Q, 12, 12, "IR7 the surface before 103");
	if (!q0)
		return;
	ir_circle(Q, 103, 0, 0, 12, 12, 0, 0, 0, 99);
	ir_expect(Q, 12, 12, q0, "IR7 103 without alpha");
	free(q0);
}

/* --- IR8 --- */
static void ir8_save(void)
{
	ir_setup();
	const int MIX = 10771;
	ir_new(MIX, 24, 16, 10, 20, 30, 255);
	ir_circle(MIX, 103, 1, 1, 9, 9, 0, 0, 0, 100);
	ir_circle(MIX, 107, 12, 2, 10, 6, 250, 10, 10, 200);
	ir_pie(MIX, 4, 6, 15, 9, 200, 250, 0, 255, 0, 180);
	uint8_t *m = ir_model_new(24, 16, 10, 20, 30, 255);
	ir_m103(m, 24, 16, 1, 1, 9, 9, 100);
	ir_m107(m, 24, 16, 12, 2, 10, 6, 250, 10, 10, 200);
	ir_m121(m, 24, 16, 4, 6, 15, 9, 200, 250, 0, 255, 0, 180, false);
	const int n_ops = ir_nr_ops(MIX);
	uint8_t *before = ir_pixels(MIX, 24, 16, "IR8 before the save");
	if (!before) {
		free(m);
		return;
	}
	IR_CHECK(!memcmp(before, m, 24 * 16 * 4), "IR8: the pixels before the save differ from the model");
	PE_UpdateComponent(0);
	struct page *saved = NULL;
	IR_CHECK(PE_Save(&saved) && saved, "IR8: PE_Save failed");
	if (saved) {
		IR_CHECK(ca_save_version(saved) == 14, "IR8: the save is XPE v%d, want 14 (three new operation types)",
			 ca_save_version(saved));
		IR_CHECK(PE_Load(&saved), "IR8: PE_Load failed");
		PE_UpdateComponent(0);
		ca_free_buffer(saved);
		IR_CHECK(ir_nr_ops(MIX) == n_ops, "IR8: %d operations after PE_Load, want %d", ir_nr_ops(MIX), n_ops);
		ir_expect(MIX, 24, 16, m, "IR8 after PE_Save / PE_Load");
	}
	free(before);
	free(m);
}

/* --- IR9 --- */
static void ir9_nothing(void)
{
	ir_setup();
	/* Each draws nothing; a fill of (1,1,2,2) after it is still done (the
	 * original's step succeeds), except after 107 far outside, which the
	 * original fails (nothing written) and xsystem4 does not stop at. */
	static const struct { int cmd, x, y, w, h, start, sweep, angle; bool then_fill; const char *what; } c[] = {
		{ 103, 2, 2, 0, 6, 0, 0, 0, true, "103 W 0" },
		{ 103, 2, 2, 6, -6, 0, 0, 0, true, "103 H -6" },
		{ 103, 2, 2, 6, 0, 0, 0, 0, true, "103 H 0" },
		{ 107, 2, 2, 0, 6, 0, 0, 0, true, "107 W 0" },
		{ 107, 2, 2, -6, 6, 0, 0, 0, true, "107 W -6" },
		{ 121, 2, 2, 6, 0, 0, 360, 0, true, "121 H 0" },
		{ 121, 2, 2, -6, 6, 0, 360, 0, true, "121 W -6" },
		{ 121, 2, 2, 0, 6, 0, 359, 0, true, "121 W 0" },
		{ 103, -1000, 3, 12, 12, 0, 0, 0, true, "103 far left" },
		{ 103, 3, 100000, 12, 12, 0, 0, 0, true, "103 far below" },
		{ 107, 100000, 3, 12, 12, 0, 0, 0, false, "107 far right" },
		{ 121, 3, -100000, 12, 12, 0, 360, 0, true, "121 far above" },
		{ 121, 0, 0, 8, 8, -90, 180, 0, true, "121 start -90" },
		{ 121, 0, 0, 8, 8, 0, -90, 0, true, "121 sweep -90" },
		/* xsystem4's limits, as for 102, 106 and 122 (radius 1024): */
		{ 103, -2000, -2000, 5000, 5000, 0, 0, 0, true, "103 D 5000" },
		{ 107, -2000, -2000, 5000, 5000, 0, 0, 0, true, "107 D 5000" },
		{ 121, -2000, -2000, 5000, 5000, 180, 90, 0, true, "121 5000 x 5000" },
		{ 103, 0, 0, INT_MAX, INT_MAX, 0, 0, 0, true, "103 INT_MAX" },
		{ 121, 0, 0, INT_MAX, 8, 0, 360, 0, true, "121 INT_MAX" },
		{ 121, INT_MIN, INT_MIN, 8, 8, 0, 360, 0, true, "121 at INT_MIN" },
		{ 103, INT_MAX, INT_MAX, 8, 8, 0, 0, 0, true, "103 at INT_MAX" },
		/* the first sizes and angles beyond those limits */
		{ 103, -1000, -1000, 2049, 8, 0, 0, 0, true, "103 W 2049" },
		{ 107, -1000, -1000, 8, 2049, 0, 0, 0, true, "107 H 2049" },
		{ 121, -1000, 0, 2049, 8, 0, 359, 0, true, "121 W 2049" },
		{ 121, 0, -1000, 8, 2049, 0, 359, 0, true, "121 H 2049" },
		{ 121, 0, 0, 8, 8, 361, 90, 0, true, "121 start 361" },
		{ 121, 0, 0, 8, 8, 0, 361, 0, true, "121 sweep 361" },
		/* a rotation is not implemented (as for 122): nothing is drawn */
		{ 121, 0, 0, 8, 8, 0, 359, 30, true, "121 rotated 30" },
	};
	for (int i = 0; i < (int)(sizeof c / sizeof c[0]); i++) {
		const int no = 10781 + i;
		ir_new(no, 8, 8, 50, 60, 70, 90);
		ir_add(no, &(struct ir_cmd){ .cmd = c[i].cmd, .x = c[i].x, .y = c[i].y, .w = c[i].w, .h = c[i].h,
					    .c = { 255, 255, 255, 255 }, .start = c[i].start, .sweep = c[i].sweep,
					    .angle = c[i].angle });
		if (c[i].then_fill)
			ir_add(no, &(struct ir_cmd){ .cmd = 6, .x = 1, .y = 1, .w = 2, .h = 2, .c = { 1, 2, 3, 4 } });
		uint8_t *m = ir_model_new(8, 8, 50, 60, 70, 90);
		if (c[i].then_fill)
			ir_fill(m, 8, 8, 1, 1, 2, 2, 1, 2, 3, 4);
		char what[64];
		snprintf(what, sizeof what, "IR9 %s", c[i].what);
		ir_expect(no, 8, 8, m, what);
		free(m);
	}
}

/* --- IR10 --- */
static void ir10_v13(void)
{
	ir_setup();
	/* The base is made under v14; only the command is added under 13. */
	const int version = ain->version;
	static const int cmds[] = { 103, 107, 121 };
	for (int i = 0; i < 3; i++) {
		const int no = 10801 + i;
		ir_new(no, 8, 8, 50, 60, 70, 90);
		const int n = ir_nr_ops(no);
		ain->version = 13;
		ir_add(no, &(struct ir_cmd){ .cmd = cmds[i], .x = 0, .y = 0, .w = 8, .h = 8, .c = { 255, 255, 255, 255 },
					    .start = 0, .sweep = 359 });
		ain->version = version;
		IR_CHECK(ir_nr_ops(no) == n, "IR10 v13: command %d added %d operation(s)", cmds[i], ir_nr_ops(no) - n);
		uint8_t *m = ir_model_new(8, 8, 50, 60, 70, 90);
		char what[64];
		snprintf(what, sizeof what, "IR10 v13 command %d", cmds[i]);
		ir_expect(no, 8, 8, m, what);
		free(m);
	}
}

/* --- IR11 --- */
static void ir11_neighbours(void)
{
	ir_setup();
	/* 102 / 106 around (7,6) r 5: the disc 103 / 107 draw for (2,1,10,10). */
	const int C102 = 10811, C106 = 10812;
	ir_new(C102, 16, 16, 10, 20, 30, 255);
	int n = ir_nr_ops(C102);
	ir_add(C102, &(struct ir_cmd){ .cmd = 102, .x = 7, .y = 6, .radius = 5, .c = { 0, 0, 0, 150 } });
	struct parts *p = parts_try_get(C102);
	IR_CHECK(ir_nr_ops(C102) == n + 1 && ir_last_type(p) == PARTS_CP_FILL_CIRCLE_AMAP,
		 "IR11 102: %d operation(s) added, not one FILL_CIRCLE_AMAP", ir_nr_ops(C102) - n);
	uint8_t *m = ir_model_new(16, 16, 10, 20, 30, 255);
	ir_m103(m, 16, 16, 2, 1, 10, 10, 150);
	ir_expect(C102, 16, 16, m, "IR11 102");
	free(m);
	ir_new(C106, 16, 16, 10, 20, 30, 255);
	n = ir_nr_ops(C106);
	ir_add(C106, &(struct ir_cmd){ .cmd = 106, .x = 7, .y = 6, .radius = 5, .c = { 250, 200, 0, 150 } });
	p = parts_try_get(C106);
	IR_CHECK(ir_nr_ops(C106) == n + 1 && ir_last_type(p) == PARTS_CP_FILL_CIRCLE_BLEND,
		 "IR11 106: %d operation(s) added, not one FILL_CIRCLE_BLEND", ir_nr_ops(C106) - n);
	m = ir_model_new(16, 16, 10, 20, 30, 255);
	ir_m107(m, 16, 16, 2, 1, 10, 10, 250, 200, 0, 150);
	ir_expect(C106, 16, 16, m, "IR11 106");
	free(m);

	const int C97 = 10813;
	static const int tri[] = { 1, 1, 14, 3, 6, 13 };
	ir_new(C97, 16, 16, 10, 20, 30, 255);
	n = ir_nr_ops(C97);
	ir_add(C97, &(struct ir_cmd){ .cmd = 97, .c = { 0, 90, 200, 180 }, .nr_pos = 3, .pos = tri });
	p = parts_try_get(C97);
	IR_CHECK(ir_nr_ops(C97) == n + 1 && ir_last_type(p) == PARTS_CP_FILL_POLYGON_BLEND,
		 "IR11 97: %d operation(s) added, not one FILL_POLYGON_BLEND", ir_nr_ops(C97) - n);

	/* The sector scan of the 121 model against xsystem4's command 122
	 * (the same scan, 0x4d0280, alpha only; added straight, as its
	 * pactex loader does): with the base's colour as 121's colour, the
	 * model writes the alpha 122 writes. Integer radii only (122's). */
	bool (*add_pie)(int, int, int, int, int, int, int, int, int, int) =
		dlsym(RTLD_DEFAULT, "PE_AddFillPieAMapToPartsConstructionProcess");
	IR_CHECK(add_pie != NULL, "IR11: PE_AddFillPieAMapToPartsConstructionProcess not found");
	static const struct { int x, y, rx, ry, start, sweep, a; } pie[] = {
		{ 15, 15, 15, 15, 90, 180, 80 }, { 15, 15, 15, 15, 270, 180, 140 }, { 12, 9, 10, 5, 0, 180, 255 },
		{ 10, 10, 9, 9, 30, 100, 200 }, { 4, 3, 6, 6, 0, 360, 255 }, { 9, 9, 5, 8, 200, 250, 999 },
	};
	for (int i = 0; add_pie && i < (int)(sizeof pie / sizeof pie[0]); i++) {
		const int no = 10850 + i;
		ir_new(no, 30, 30, 40, 50, 60, 255);
		add_pie(no, pie[i].x, pie[i].y, pie[i].rx, pie[i].ry, pie[i].start, pie[i].sweep, pie[i].a, 0, 1);
		uint8_t *mm = ir_model_new(30, 30, 40, 50, 60, 255);
		ir_m121(mm, 30, 30, pie[i].x - pie[i].rx, pie[i].y - pie[i].ry, 2 * pie[i].rx, 2 * pie[i].ry, pie[i].start,
			pie[i].sweep, 40, 50, 60, pie[i].a, false);
		char what[80];
		snprintf(what, sizeof what, "IR11 122 against the 121 model's scan, sector %d", i);
		ir_expect(no, 30, 30, mm, what);
		free(mm);
	}

	static const int none[] = { 100, 101, 104, 105, 108, 118, 119, 120, 122, 123, 200 };
	for (int i = 0; i < (int)(sizeof none / sizeof none[0]); i++) {
		const int no = 10820 + i;
		ir_new(no, 8, 8, 50, 60, 70, 90);
		n = ir_nr_ops(no);
		ir_add(no, &(struct ir_cmd){ .cmd = none[i], .x = 0, .y = 0, .w = 8, .h = 8, .radius = 3,
					    .c = { 255, 255, 255, 255 }, .start = 0, .sweep = 360 });
		IR_CHECK(ir_nr_ops(no) == n, "IR11 command %d added %d operation(s), want none", none[i], ir_nr_ops(no) - n);
		uint8_t *mm = ir_model_new(8, 8, 50, 60, 70, 90);
		char what[64];
		snprintf(what, sizeof what, "IR11 command %d", none[i]);
		ir_expect(no, 8, 8, mm, what);
		free(mm);
	}
}

/* --- IR12 --- */
/* The pixels of the rectangle (x0,y0)-(x1,y1) of a w x h surface that are
 * not (r,g,b,a). */
static int ir_changed(const uint8_t *m, int w, int h, int r, int g, int b, int a, int x0, int y0, int x1, int y1)
{
	int n = 0;
	for (int y = y0; y <= y1 && y < h; y++)
		for (int x = x0; x <= x1 && x < w; x++) {
			const uint8_t *p = m + (y * w + x) * 4;
			n += p[0] != r || p[1] != g || p[2] != b || p[3] != a;
		}
	return n;
}

static void ir12_edges(void)
{
	ir_setup();
	/* D 8 left of the surface: the positions -8.5 .. -0.5, the last one
	 * (4 right of the centre -4.5) on column 0; and the same above it. */
	static const struct { int cmd, x, y; } c[] = {
		{ 103, -9, -1 }, { 103, -1, -9 }, { 107, -9, -1 }, { 107, -1, -9 },
	};
	for (int i = 0; i < 4; i++) {
		const int no = 10891 + i;
		ir_new(no, 8, 8, 1, 2, 3, 40);
		ir_circle(no, c[i].cmd, c[i].x, c[i].y, 8, 8, 250, 0, 0, 255);
		uint8_t *m = ir_model_new(8, 8, 1, 2, 3, 40);
		if (c[i].cmd == 103)
			ir_m103(m, 8, 8, c[i].x, c[i].y, 8, 8, 255);
		else
			ir_m107(m, 8, 8, c[i].x, c[i].y, 8, 8, 250, 0, 0, 255);
		/* Only the edge's middle pixel holds a subsample (alpha 3). */
		IR_CHECK(ir_changed(m, 8, 8, 1, 2, 3, 40, 0, 0, 7, 7) > 0,
			 "IR12 the model itself draws %d at (%d,%d)", c[i].cmd, c[i].x, c[i].y);
		char what[64];
		snprintf(what, sizeof what, "IR12 %d at (%d,%d)", c[i].cmd, c[i].x, c[i].y);
		ir_expect(no, 8, 8, m, what);
		free(m);
	}
	/* 121 of 12 x 12 centred on x = 0.5 with 120 + 120 (pointing left), and
	 * on y = 0.5 with 210 + 120 (pointing up): the column (row) dx = 0 is
	 * outside the wedge, so column (row) 0 holds only the writes of the
	 * positions -0.5. */
	static const struct { int x, y, start; } p[] = { { -6, 1, 120 }, { 1, -6, 210 } };
	for (int i = 0; i < 2; i++) {
		const int no = 10895 + i;
		ir_new(no, 8, 8, 5, 5, 5, 5);
		ir_pie(no, p[i].x, p[i].y, 12, 12, p[i].start, 120, 200, 100, 50, 255);
		uint8_t *m = ir_model_new(8, 8, 5, 5, 5, 5);
		ir_m121(m, 8, 8, p[i].x, p[i].y, 12, 12, p[i].start, 120, 200, 100, 50, 255, false);
		IR_CHECK(ir_changed(m, 8, 8, 5, 5, 5, 5, 0, 0, i ? 7 : 0, i ? 0 : 7) > 0,
			 "IR12 the model itself draws on the edge (%d,%d)", p[i].x, p[i].y);
		char what[64];
		snprintf(what, sizeof what, "IR12 121 at (%d,%d) %d + 120", p[i].x, p[i].y, p[i].start);
		ir_expect(no, 8, 8, m, what);
		free(m);
	}
	/* 121 with rx 2.5 and ry 4 (ry 2.5 and rx 4): the positions across are
	 * whole numbers; -1 (left, top) and the width or height (right, bottom)
	 * are outside the surface and left out (0x5b0b80), the edge next to
	 * them is drawn. */
	static const struct { int x, y, w, h, sw, sh; } q[] = {
		{ -4, 1, 5, 8, 8, 10 }, { 4, 1, 5, 8, 8, 10 }, { 1, -4, 8, 5, 10, 8 }, { 1, 4, 8, 5, 10, 8 },
	};
	for (int i = 0; i < 4; i++) {
		const int no = 10897 + i, sw = q[i].sw, sh = q[i].sh;
		ir_new(no, sw, sh, 5, 5, 5, 5);
		ir_pie(no, q[i].x, q[i].y, q[i].w, q[i].h, 0, 180, 200, 100, 50, 255);
		ir_pie(no, q[i].x, q[i].y, q[i].w, q[i].h, 180, 180, 200, 100, 50, 255);
		uint8_t *m = ir_model_new(sw, sh, 5, 5, 5, 5);
		ir_m121(m, sw, sh, q[i].x, q[i].y, q[i].w, q[i].h, 0, 180, 200, 100, 50, 255, false);
		ir_m121(m, sw, sh, q[i].x, q[i].y, q[i].w, q[i].h, 180, 180, 200, 100, 50, 255, false);
		const int x0 = i == 1 ? sw - 1 : 0, x1 = i == 0 ? 0 : sw - 1;
		const int y0 = i == 3 ? sh - 1 : 0, y1 = i == 2 ? 0 : sh - 1;
		IR_CHECK(ir_changed(m, sw, sh, 5, 5, 5, 5, x0, y0, x1, y1) > 0,
			 "IR12 the model itself draws on the edge (%d,%d,%d,%d)", q[i].x, q[i].y, q[i].w, q[i].h);
		char what[64];
		snprintf(what, sizeof what, "IR12 121 (%d,%d,%d,%d)", q[i].x, q[i].y, q[i].w, q[i].h);
		ir_expect(no, sw, sh, m, what);
		free(m);
	}
}

/* --- IR13 --- */
static void ir13_largest(void)
{
	ir_setup();
	/* D, W and H 2048 around an 8 x 8 surface (the centre 4.5, 4.5). */
	const int A = 10901, B = 10902, P = 10903;
	ir_new(A, 8, 8, 10, 20, 30, 40);
	ir_circle(A, 103, -1020, -1020, 2048, 8, 0, 0, 0, 200);
	uint8_t *m = ir_model_new(8, 8, 10, 20, 30, 40);
	ir_m103(m, 8, 8, -1020, -1020, 2048, 8, 200);
	ir_px(m, 8, 0, 0, 10, 20, 30, 200, "IR13 the model itself, 103");
	ir_expect(A, 8, 8, m, "IR13 103 D 2048");
	free(m);
	ir_new(B, 8, 8, 10, 20, 30, 40);
	ir_circle(B, 107, -1020, -1020, 8, 2048, 250, 0, 0, 255);
	m = ir_model_new(8, 8, 10, 20, 30, 40);
	ir_m107(m, 8, 8, -1020, -1020, 8, 2048, 250, 0, 0, 255);
	/* 10 + ((250 - 10) * 255 >> 8) = 249; 20 and 30 go to 0. */
	ir_px(m, 8, 7, 7, 249, 0, 0, 255, "IR13 the model itself, 107");
	ir_expect(B, 8, 8, m, "IR13 107 D 2048");
	free(m);
	ir_new(P, 8, 8, 10, 20, 30, 40);
	ir_pie(P, -1020, -1020, 2048, 2048, 0, 359, 200, 100, 50, 255);
	m = ir_model_new(8, 8, 10, 20, 30, 40);
	ir_m121(m, 8, 8, -1020, -1020, 2048, 2048, 0, 359, 200, 100, 50, 255, false);
	ir_px(m, 8, 7, 7, 200, 100, 50, 255, "IR13 the model itself, 121");
	ir_expect(P, 8, 8, m, "IR13 121 2048 x 2048");
	free(m);
}

static int test_construction_in_rect(void)
{
	static const struct xc_case cases[] = {
		{ "IR1", ir1_amap }, { "IR2", ir2_blend }, { "IR3", ir3_pie }, { "IR4", ir4_position_view },
		{ "IR5", ir5_odd }, { "IR6", ir6_ranges }, { "IR7", ir7_no_alpha }, { "IR8", ir8_save },
		{ "IR9", ir9_nothing }, { "IR10", ir10_v13 }, { "IR11", ir11_neighbours }, { "IR12", ir12_edges },
		{ "IR13", ir13_largest },
	};
	return xc_run("CONSTRUCTION-IN-RECT", cases, sizeof cases / sizeof *cases, 60);
}
