/* construction-preview: the pactex 構築部件 states that use construction
 * commands 13, 12, 1 and 89, as the original engine builds them while the
 * pactex loads (static disassembly only):
 *   - table 0x4fb7d8: 1 -> 0x4fa398 -> 0x4fbe40, 12 -> 0x4fa579 -> 0x4fe050,
 *     13 -> 0x4fa59e -> 0x4fe330, 89 -> 0x4fb075 -> 0x506960; a step that
 *     fails skips the rest (0x4fb675); the step parser (0x4f82a0) keeps
 *     補間タイプ's low byte at +8, 元矩形 X,Y,W,H at +0xc..+0x18, 先矩形
 *     X,Y,W,H at +0x1c/+0x20/+0x2c/+0x30, 色１ at +0x34..+0x40, ＣＧ名 at
 *     +0xac, 丸め's second item at +0xd5 and 旋轉角度 at +0xd8; a missing key
 *     is 0;
 *   - 13 (CutCGScaleBlend, 0x4fe330): no surface fails; 0x50b350 loads the
 *     cut 元矩形 of ＣＧ名 (all of the CG when its W or H is not positive,
 *     0x5ab3b0) and for 補間タイプ 2 scales it to 先矩形 W x H (0x5aa9c0 ->
 *     0x4929e0, see cg-scale-copy); 0x5aca90 then blends 先矩形 W x H of it,
 *     from its (0, 0), at 先矩形 X,Y (cut to the surface by 0x5abed0, an
 *     empty rectangle fails, 0x5abde0): on a surface with an alpha the
 *     backend's 0x491a50 takes 0x494320, c += ((s - c) * sa) sar 8 for R, G
 *     and B, the surface's alpha kept;
 *   - 12 (CutCGCopy, 0x4fe050): no surface fails; 補間タイプ is not read;
 *     0x50b160 loads the cut (元矩形 X, Y, max(元矩形 W, 先矩形 W), max(元矩形
 *     H, 先矩形 H)) (the cmovge at 0x4fe0ce and 0x4fe0e5) and that many
 *     pixels of it, from its (0, 0), are copied to 先矩形 X,Y: on a surface
 *     with an alpha all four bytes (0x5abb80 -> 0x5acd70 -> 0x4918e0);
 *   - 1 (0x4fbe40) makes the surface with 0x5a9680(W, H): the image
 *     manager's 0x7009e0 marks it as pixels without an alpha (command 0's
 *     0x5a94d0 -> 0x700960 marks both and fills it opaque black); W or H not
 *     positive fails;
 *   - 89 (矩形塗りつぶし（ピクセル＆アルファ）, 0x506960): no surface fails;
 *     先矩形 W or H not positive succeeds with nothing drawn (0x5069a2,
 *     0x5069aa); otherwise the rectangle's polygon (0x4e97b0, as command
 *     90's) turned by 旋轉角度 (0x4e9940) is scanned as command 94's
 *     (0x5afa40): a covered pixel becomes (R, G, B, A * cov / 255), each
 *     clamped; unlike 90 it does not require a surface with an alpha.
 * The game has eight such states: config page 3's preview background (0,
 * 13: the lower half of a 1280 x 720 background without an alpha scaled into
 * 640 x 180), the base of the 290-high scroll bar (0, 12, 12), the event
 * hint's stripe (0, 12), the backgrounds of config pages 8 and 9 (four times
 * 1, 3) and a screen-sized mask (0, 89) that is not shown once built: its
 * 編輯上表示 is 0, which the port, after where the original's parser stores
 * it (0x553dfa, parts +0xac), takes as hiding it and its subtree at run time
 * (parts->edit_hidden); what the original uses the mask for was not read and
 * is only inferred. Before the fix the loader took none of these commands:
 * the CG states showed their CG at its own size (the preview's covered, and
 * took the clicks of, the three buttons at the bottom right of page 3), the
 * others nothing.
 *
 * The expected pixels: the formulas above written out a second time here
 * (cg-scale-copy's cs_scale_model for the scaling; the rectangle's coverage
 * of construction-shapes' sh_rect, with the colour), command 94 given the
 * four corners, and pixels worked out by hand. A CG without an alpha is
 * blended with an alpha of 255: the QNT decoder (CDecodeQNT, entry
 * 0x4761a0) makes such a CG an image of pixels only and writes 0xff into the
 * fourth byte of every pixel (0x4764b4; the alpha decoder 0x476530 runs only
 * for a CG with one), the cut (0x5a9ea0 -> 0x5a98d0 -> 0x4918e0) and the
 * scale (0x4929e0) keep all four bytes, and 0x494320 reads that byte as sa.
 * The original's page 3 (a JPEG screenshot) shows its preview about one
 * level darker than the background, which agrees.
 *
 * Cases (each in its own fork):
 *   PV1 13, the game's shape: built 640 x 180, every pixel the background's
 *       pixel (2x, 360 + 2y) moved from opaque black by 255 / 256 (one level
 *       darker), the alpha 255; at (512, 414) the parts is not under the
 *       cursor at the centres of the three buttons below it.
 *   PV2 13's blend: a 3 x 2 CG with an alpha scaled to 5 x 3 on a coloured
 *       base (the colours move by the CG's alpha, floored also when they go
 *       down; the base's alpha kept; alpha 0 changes nothing); a cut scaled;
 *       補間タイプ 258 (low byte 2); the keys in SJIS; 元矩形 W INT_MIN, not
 *       positive, takes all of the CG.
 *   PV3 12: the scroll bar's base (two cuts of a 14 x 460 CG, the alpha
 *       copied), the stripe (the top left of a larger pattern), the size
 *       copied is the larger of 元矩形's and 先矩形's; the keys in SJIS.
 *   PV4 1: a surface without an alpha (its texture has no alpha bits), the
 *       pages' fills, opaque; 1 alone; a fill of a part of it. No alpha bits
 *       is the port's representation (PARTS_CP_CREATE_PIXEL_ONLY, which the
 *       script path's command 1 already uses); that such a surface is drawn
 *       opaque is inferred (the renderer was not read for its fourth byte).
 *   PV5 89: the mask (the hole (0, 0, 0, 0), the rest opaque black, the
 *       column past the right edge covered half), a colour on a coloured
 *       base against 94 with the corners and by hand, a turn of 360, the
 *       keys in SJIS, W 0 and H INT_MIN built with nothing drawn and the
 *       next step run.
 *   PV6 what stays unbuilt, as before (guards: they pass before the fix).
 *       Most of these are the port's choices, narrower than the original,
 *       which builds them: 0x5aca90 and 0x5acd70 cut only the destination
 *       to the surface (0x5abed0: cmovs / cmovl at 0x5abf7b..0x5abf9a) and
 *       read the source from (0, 0) whatever was cut off, so a 先矩形
 *       outside the surface is blended or copied in part; 0x5ab3b0 cuts a
 *       元矩形 partly outside the CG (X -1 included) to their intersection
 *       (cmovl), after which more is read than was loaded; 13 on a surface
 *       without an alpha is blended by 0x491c10 (d += ((s - d) * (sa >> 1))
 *       >> 7, the alpha kept: psrld 0x19, pmullw, psraw 7); 補間タイプ 1
 *       scales by 0x5aa7b0 (not read) and 0 or 3 does not scale; 12 and 13
 *       after a command 2 surface (0x5ad010 writes the RGB alone on one
 *       without an alpha) or after 1, and 89 after 1, write what the
 *       surface takes; command 1 after a surface frees it (0x5a8df0) and
 *       makes a new one (0x4fbe40 needs none before); 0x4e9940 turns 89 by
 *       30 degrees; 1 then 5 is what 5 does on a surface without an alpha
 *       (0x4fc378 skips it). And where a step fails (a CG that cannot be
 *       loaded, 1 of width 0, 12, 13 or 89 as the first step) the original
 *       keeps what the steps before it drew (0x4fb675), while here the whole
 *       state stays unbuilt. Values near INT_MAX are not built either: the
 *       original adds them in 32 bits and wraps around.
 *   PV7 PE_Save / PE_Load: XPE version 13 (the two operation types of 12
 *       and 13 are new), each of their operations saved as its type and
 *       the fields of 7, 8 and 29 (checked before the load), the same pixels
 *       and operations, the surface of pixels only still without an alpha;
 *       a save rewritten as version 12 loads the same.
 *   PV8 guards: before v14 none of these is built (the save is XPE v3);
 *       states of commands built before (14, 90) load as before next to the
 *       new ones.
 * Only functions that exist before the fix are called: the file builds before
 * it.
 */
static int pv_failures;
#define PV_CHECK(c, ...) do { if (!(c)) { printf("  FAIL: "); printf(__VA_ARGS__); printf("\n"); pv_failures++; } } while (0)

#define PV_POINTS_S "\x8d\xc0\x95\x57\x88\xea\x97\x97"			/* 座標一覧 */

/* --- CGs made in memory --- */
#define PVN_BACK "pv/back"
#define PVN_TINY "pv/tiny"
#define PVN_GRID "pv/grid"
#define PVN_BAR "pv/bar"
#define PVN_PATTERN "pv/pattern"
#define PVN_TALL "pv/tall"
enum { PV_BACK, PV_TINY, PV_GRID, PV_BAR, PV_PATTERN, PV_TALL, PV_NR };
static struct {
	const char *name;
	int w, h;
	bool alpha;
	uint8_t *px;	/* RGBA; 255 in the alpha of a CG without one */
} pv_src[PV_NR] = {
	[PV_BACK] = { PVN_BACK, 1280, 720, false },
	[PV_TINY] = { PVN_TINY, 3, 2, true },
	[PV_GRID] = { PVN_GRID, 6, 4, true },
	[PV_BAR] = { PVN_BAR, 14, 460, true },
	[PV_PATTERN] = { PVN_PATTERN, 600, 400, true },
	[PV_TALL] = { PVN_TALL, 2, 1100, true },	/* for a scale whose product leaves 32 bits */
};
static struct probe_cg_file pv_files[PV_NR];

/* cg-scale-copy's 3 x 2 CG, rows from the top (its hand values hold). */
static const uint8_t pv_tiny_px[6][4] = {
	{ 0, 100, 200, 255 }, { 255, 50, 0, 128 }, { 10, 10, 10, 0 },
	{ 100, 0, 50, 64 }, { 0, 255, 255, 255 }, { 200, 200, 200, 200 },
};

static void pv_set(uint8_t *p, int r, int g, int b, int a) { p[0] = r; p[1] = g; p[2] = b; p[3] = a; }
static uint8_t *pv_at(int cg, int x, int y) { return pv_src[cg].px + ((size_t)y * pv_src[cg].w + x) * 4; }

static void pv_install(void)
{
	unsigned seed = 20261025;
	for (int i = 0; i < PV_NR; i++) {
		const int w = pv_src[i].w, h = pv_src[i].h;
		uint8_t *px = pv_src[i].px = calloc((size_t)w * h, 4);
		for (int k = 0; k < w * h; k++) {
			uint8_t *p = px + (size_t)k * 4;
			if (i == PV_TINY) {
				memcpy(p, pv_tiny_px[k], 4);
				continue;
			}
			for (int c = 0; c < 4; c++) {
				seed = seed * 1103515245u + 12345u;
				p[c] = seed >> 16;
			}
			if (!pv_src[i].alpha)
				p[3] = 255;
		}
		if (i == PV_BACK) {
			/* The preview's first row takes the even columns of row 360. */
			pv_set(pv_at(i, 0, 360), 0, 1, 255, 255);
			pv_set(pv_at(i, 1, 360), 255, 255, 255, 255);
			pv_set(pv_at(i, 2, 360), 128, 200, 10, 255);
			pv_set(pv_at(i, 1278, 718), 77, 0, 3, 255);
			pv_set(pv_at(i, 1279, 719), 255, 255, 255, 255);
		}
		pv_files[i].name = pv_src[i].name;
		pv_files[i].data = cg_png(w, h, px, pv_src[i].alpha, &pv_files[i].size);
	}
	probe_install_cg_assets(pv_files, PV_NR);
}

/* --- pactex steps --- */
#define PV_LINE_WIDTH "\xbe\x80\xa4\xce\xb7\xf9"		/* 線の幅 */
enum {
	PV_SJIS = 1 << 0,
	/* The keys the game's steps have besides those the loader reads, with
	 * the values of config page 8's (行間隔 4, フォントサイズ 12, white
	 * フォント色, 線の幅 1; the others 0 or ""). */
	PV_EXTRA = 1 << 1,
};
struct pv_step {
	int cmd;
	int dest[6];		/* 先矩形: X, Y, X2, Y2, W, H */
	int src[4];		/* 元矩形 */
	int interp;		/* 補間タイプ */
	const char *cg;		/* ＣＧ名 */
	int color[4];		/* 色１ */
	int full;		/* 全體 */
	int rotate;		/* 旋轉角度 */
	int nr_xy;		/* 座標總覽 (command 94) */
	const int *xy;
};
#define PV_CREATE(w, h) { .cmd = 0, .dest = { 0, 0, 0, 0, w, h } }
#define PV_PIXELS(w, h) { .cmd = 1, .interp = 1, .dest = { 0, 0, 0, 0, w, h } }
#define PV_FILL_ALL(r, g, b, a) { .cmd = 6, .color = { r, g, b, a }, .full = 1 }
#define PV_ALPHA_ALL(a) { .cmd = 5, .color = { 0, 0, 0, a }, .full = 1 }
#define PV_FILL(x, y, w, h, r, g, b) { .cmd = 3, .interp = 1, .dest = { x, y, 0, 0, w, h }, .color = { r, g, b, 0 } }
#define PV_RECT89(x, y, w, h, r, g, b, a) { .cmd = 89, .interp = 1, .dest = { x, y, 0, 0, w, h }, .color = { r, g, b, a } }
#define PV_N(steps) (int)(sizeof(steps) / sizeof((steps)[0])), steps

static struct ex_tree pv_parts(const char *name, int x, int y, int n, const struct pv_step *steps, int flags)
{
	const bool sjis = flags & PV_SJIS;
	struct lb_place pl = { x, y, 1 };
	struct ex_tree info = bu_branch(BU_INFO, 2);
	info.children[0] = bu_sleaf(BU_TYPE, BU_LOW);
	info.children[1] = bu_branch(BU_NORMAL, 3);
	struct ex_tree *st = &info.children[1];
	st->children[0] = bu_sleaf(BU_TYPE, BR_CONS);
	st->children[1] = lb_ilist(CN_AREA, 4, (int[]){ 0, 0, 0, 0 });
	st->children[2] = bu_branch(sjis ? CN_STEPS_S : BR_STEPS, n);
	for (int i = 0; i < n; i++) {
		const struct pv_step *s = &steps[i];
		char key[32];
		snprintf(key, sizeof key, "Step%d", i + 1);
		struct ex_tree t = bu_branch(key, 28);
		unsigned k = 0;
		t.children[k++] = bu_ileaf(sjis ? CN_COMMAND_S : BR_COMMAND, s->cmd);
		t.children[k++] = bu_ileaf(sjis ? CS_INTERP_S : CS_INTERP, s->interp);
		t.children[k++] = lb_ilist(sjis ? CS_SRC_S : CS_SRC, 4, s->src);
		t.children[k++] = lb_ilist(sjis ? CN_DEST_S : LB_DEST, 6, s->dest);
		t.children[k++] = lb_ilist(sjis ? CN_COLOR1_S : CN_COLOR1, 4, s->color);
		t.children[k++] = bu_sleaf(sjis ? CS_CG_NAME_S : BR_CG_NAME, s->cg ? s->cg : "");
		t.children[k++] = bu_ileaf(sjis ? CN_FULL_S : CN_FULL, s->full);
		t.children[k++] = lb_ilist(sjis ? SH_ROUND_S : SH_ROUND, 2, (int[]){ 0, 0 });
		t.children[k++] = bu_ileaf(sjis ? CN_ROTATE_S : CN_ROTATE, s->rotate);
		if (s->xy)
			t.children[k++] = lb_ilist(sjis ? PV_POINTS_S : SH_POINTS, s->nr_xy, s->xy);
		if (flags & PV_EXTRA) {
			t.children[k++] = lb_ilist(CC_COLOR2, 4, (int[]){ 0, 0, 0, 0 });
			t.children[k++] = bu_ileaf(BX_K_CHAR_SPACE, 0);
			t.children[k++] = bu_ileaf(BX_K_LINE_SPACE, 4);
			t.children[k++] = bu_ileaf(BX_K_FONT_TYPE, 0);
			t.children[k++] = bu_ileaf(BX_K_FONT_SIZE, 12);
			t.children[k++] = lb_ilist(BX_K_FONT_COLOR, 3, (int[]){ 255, 255, 255 });
			t.children[k++] = bu_ileaf(BX_K_FONT_BOLD, 0);
			t.children[k++] = bu_ileaf(BX_K_FONT_EDGE, 0);
			t.children[k++] = lb_ilist(BX_K_FONT_EDGE_COLOR, 3, (int[]){ 0, 0, 0 });
			t.children[k++] = bu_sleaf(BX_K_TEXT, "");
			t.children[k++] = bu_ileaf(SH_BLUR, 0);
			t.children[k++] = lb_ilist(CN_RADIUS, 2, (int[]){ 0, 0 });
			t.children[k++] = bu_ileaf(PV_LINE_WIDTH, 1);
			t.children[k++] = lb_ilist(CN_ARC, 2, (int[]){ 0, 0 });
		}
		assert(k <= 28);
		t.nr_children = k;
		st->children[2].children[i] = t;
	}
	return lb_node(name, &pl, info, 0);
}

struct pv_def { const char *name; int x, y; int n; const struct pv_step *steps; int flags; };

/* Loads one activity with the states and gives their parts numbers. */
static bool pv_load(const char *act_name, int nr, const struct pv_def *defs, int *no)
{
	struct ex_tree *kids;
	struct ex *e = lb_activity(act_name, nr, &kids);
	for (int i = 0; i < nr; i++)
		kids->children[i] = pv_parts(defs[i].name, defs[i].x, defs[i].y, defs[i].n, defs[i].steps, defs[i].flags);
	int act = cn_load(act_name, e);
	bool all = true;
	for (int i = 0; i < nr; i++) {
		no[i] = bu_number(act, defs[i].name);
		PV_CHECK(no[i] > 0 && parts_try_get(no[i]), "%s has no parts", defs[i].name);
		all = all && no[i] > 0 && parts_try_get(no[i]);
	}
	heap_unref(act);
	pv_failures += cn_failures;
	cn_failures = 0;
	return all;
}

static int pv_ops(int no)
{
	struct parts *p = parts_try_get(no);
	return p && p->states[0].type == PARTS_CONSTRUCTION_PROCESS ? cn_nr_ops(p) : -1;
}

/* The built state's pixels (to be freed; NULL with a failure when it is not
 * a built w x h construction). */
static uint8_t *pv_pixels(int no, int w, int h, const char *what)
{
	uint8_t *got = cn_pixels(no, w, h, what);
	pv_failures += cn_failures;
	cn_failures = 0;
	return got;
}

static void pv_compare(const uint8_t *got, int w, int h, const uint8_t *model, const char *what)
{
	int bad = 0;
	for (int i = 0; got && i < w * h; i++) {
		if (!memcmp(got + (size_t)i * 4, model + (size_t)i * 4, 4))
			continue;
		if (bad < 3)
			PV_CHECK(0, "%s (%d,%d) = (%d,%d,%d,%d), want (%d,%d,%d,%d)", what, i % w, i / w, got[i * 4],
				 got[i * 4 + 1], got[i * 4 + 2], got[i * 4 + 3], model[i * 4], model[i * 4 + 1],
				 model[i * 4 + 2], model[i * 4 + 3]);
		bad++;
	}
	if (bad > 3)
		PV_CHECK(0, "%s: %d pixels differ in all", what, bad);
}

static void pv_px(const uint8_t *px, int w, int x, int y, int r, int g, int b, int a, const char *what)
{
	const uint8_t *p = px ? px + ((size_t)y * w + x) * 4 : NULL;
	PV_CHECK(p && p[0] == r && p[1] == g && p[2] == b && p[3] == a, "%s (%d,%d) = (%d,%d,%d,%d), want (%d,%d,%d,%d)",
		 what, x, y, p ? p[0] : -1, p ? p[1] : -1, p ? p[2] : -1, p ? p[3] : -1, r, g, b, a);
}
static void pv_px_cg(const uint8_t *px, int w, int x, int y, int cg, int cx, int cy, const char *what)
{
	const uint8_t *c = pv_at(cg, cx, cy);
	pv_px(px, w, x, y, c[0], c[1], c[2], c[3], what);
}

/* Two built states with the same pixels. */
static void pv_same(int a, int b, int w, int h, const char *what)
{
	uint8_t *pa = pv_pixels(a, w, h, what), *pb = pv_pixels(b, w, h, what);
	if (pa && pb)
		pv_compare(pa, w, h, pb, what);
	free(pa);
	free(pb);
}

/* The bits of alpha the state's texture has (-1 without one). */
static int pv_alpha_bits(int no)
{
	struct parts *p = no > 0 ? parts_try_get(no) : NULL;
	if (!p || !p->states[0].common.texture.handle)
		return -1;
	GLint bits = -1;
	glBindTexture(GL_TEXTURE_2D, p->states[0].common.texture.handle);
	glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_ALPHA_SIZE, &bits);
	glBindTexture(GL_TEXTURE_2D, 0);
	return bits;
}

/* The state as the legacy fallback leaves it: not a construction, the CG at
 * its own size. */
static void pv_legacy(int no, int cg, const char *what)
{
	struct parts *p = no > 0 ? parts_try_get(no) : NULL;
	PV_CHECK(p, "%s: no parts", what);
	if (!p)
		return;
	struct parts_state *s = &p->states[0];
	PV_CHECK(s->type != PARTS_CONSTRUCTION_PROCESS && s->common.texture.handle && s->common.texture.w == pv_src[cg].w
		 && s->common.texture.h == pv_src[cg].h,
		 "%s: state type %d, texture %s %dx%d; want it unbuilt, the CG at its size %dx%d", what, s->type,
		 s->common.texture.handle ? "present" : "missing", s->common.texture.w, s->common.texture.h,
		 pv_src[cg].w, pv_src[cg].h);
}

/* --- the model, from the disassembly --- */

/* A copy of the cut 元矩形 of a CG (all of it when W or H is not positive). */
static uint8_t *pv_cut(int cg, const int src[4], int *cw, int *ch)
{
	const int W = pv_src[cg].w, H = pv_src[cg].h;
	int x = 0, y = 0, w = W, h = H;
	if (src[2] > 0 && src[3] > 0) {
		x = src[0];
		y = src[1];
		w = src[2];
		h = src[3];
	}
	assert(x >= 0 && y >= 0 && x + w <= W && y + h <= H);
	uint8_t *out = malloc((size_t)w * h * 4);
	for (int j = 0; j < h; j++)
		memcpy(out + (size_t)j * w * 4, pv_at(cg, x, y + j), (size_t)w * 4);
	*cw = w;
	*ch = h;
	return out;
}

/* 13: the cut scaled to 先矩形 W x H (0x4929e0) and blended at X,Y
 * (0x494320: c += ((s - c) * sa) sar 8, the alpha kept). */
static void pv_model13(uint8_t *px, int w, int h, const struct pv_step *s, int cg)
{
	int cw, ch;
	uint8_t *cut = pv_cut(cg, s->src, &cw, &ch);
	const int dw = s->dest[4], dh = s->dest[5];
	uint8_t *sc = cs_scale_model(cut, cw, ch, dw, dh);
	for (int y = 0; y < dh; y++) {
		for (int x = 0; x < dw; x++) {
			const int X = s->dest[0] + x, Y = s->dest[1] + y;
			assert(X >= 0 && Y >= 0 && X < w && Y < h);
			const uint8_t *c = sc + ((size_t)y * dw + x) * 4;
			uint8_t *d = px + ((size_t)Y * w + X) * 4;
			for (int k = 0; k < 3; k++)
				d[k] = d[k] + cs_floor256((c[k] - d[k]) * c[3]);
		}
	}
	free(sc);
	free(cut);
}

/* 12: max(元矩形 W, 先矩形 W) x max(元矩形 H, 先矩形 H) pixels of the CG
 * from 元矩形 X,Y copied to 先矩形 X,Y, all four bytes. */
static void pv_model12(uint8_t *px, int w, int h, const struct pv_step *s, int cg)
{
	const int cw = s->src[2] > s->dest[4] ? s->src[2] : s->dest[4];
	const int ch = s->src[3] > s->dest[5] ? s->src[3] : s->dest[5];
	for (int y = 0; y < ch; y++) {
		for (int x = 0; x < cw; x++) {
			const int X = s->dest[0] + x, Y = s->dest[1] + y;
			assert(X >= 0 && Y >= 0 && X < w && Y < h);
			memcpy(px + ((size_t)Y * w + X) * 4, pv_at(cg, s->src[0] + x, s->src[1] + y), 4);
		}
	}
}

static int pv_clamp(int v) { return v < 0 ? 0 : v > 255 ? 255 : v; }

/* 89 (and 94 given the four corners) for W >= 1 and H >= 2: construction-cmds'
 * coverage (CC4, construction-shapes' sh_rect): the columns X .. X + W, X and
 * X + W at 127 a line, the others 255; the rows Y .. Y + H - 1 with 4, 8 ..
 * 8, 5 lines; cov = lines * column / 8; a covered pixel becomes (R, G, B,
 * A * cov / 255), each clamped. */
static void pv_model89(uint8_t *px, int w, int h, const struct pv_step *s)
{
	const int X = s->dest[0], Y = s->dest[1], W = s->dest[4], H = s->dest[5];
	assert(W >= 1 && H >= 2);
	for (int y = Y; y <= Y + H - 1; y++) {
		const int lines = y == Y ? 4 : y == Y + H - 1 ? 5 : 8;
		for (int x = X; x <= X + W; x++) {
			if (x < 0 || y < 0 || x >= w || y >= h)
				continue;
			const int cov = lines * (x == X || x == X + W ? 127 : 255) / 8;
			if (cov)
				pv_set(px + ((size_t)y * w + x) * 4, pv_clamp(s->color[0]), pv_clamp(s->color[1]),
				       pv_clamp(s->color[2]), pv_clamp(s->color[3] * cov / 255));
		}
	}
}

static int pv_cg_of(const char *name)
{
	for (int i = 0; name && i < PV_NR; i++)
		if (!strcmp(name, pv_src[i].name))
			return i;
	assert(!"a CG the model does not have");
	return -1;
}

/* What the steps leave on a w x h surface (to be freed): 0 and 1 leave
 * opaque black (1 has no alpha, which reads back as 255), 3 replaces the
 * colours of a rectangle, 5 and 6 (over all) the alpha or everything, 12, 13
 * and 89 as above, 14 as cg-scale-copy's model. */
static uint8_t *pv_model(int w, int h, int n, const struct pv_step *steps)
{
	uint8_t *px = malloc((size_t)w * h * 4);
	for (int i = 0; i < w * h; i++)
		pv_set(px + (size_t)i * 4, 0, 0, 0, 255);
	for (int i = 0; i < n; i++) {
		const struct pv_step *s = &steps[i];
		switch (s->cmd) {
		case 0:
		case 1:
			break;
		case 3:
			for (int y = s->dest[1]; y < s->dest[1] + s->dest[5]; y++)
				for (int x = s->dest[0]; x < s->dest[0] + s->dest[4]; x++)
					memcpy(px + ((size_t)y * w + x) * 4, (uint8_t[]){ s->color[0], s->color[1], s->color[2] }, 3);
			break;
		case 5:
			assert(s->full == 1);
			for (int k = 0; k < w * h; k++)
				px[(size_t)k * 4 + 3] = s->color[3];
			break;
		case 6:
			assert(s->full == 1);
			for (int k = 0; k < w * h; k++)
				pv_set(px + (size_t)k * 4, s->color[0], s->color[1], s->color[2], s->color[3]);
			break;
		case 12:
			pv_model12(px, w, h, s, pv_cg_of(s->cg));
			break;
		case 13:
			pv_model13(px, w, h, s, pv_cg_of(s->cg));
			break;
		case 14: {
			const int cg = pv_cg_of(s->cg);
			uint8_t *sc = cs_scale_model(pv_src[cg].px, pv_src[cg].w, pv_src[cg].h, s->dest[4], s->dest[5]);
			for (int y = 0; y < s->dest[5]; y++)
				memcpy(px + ((size_t)(s->dest[1] + y) * w + s->dest[0]) * 4, sc + (size_t)y * s->dest[4] * 4,
				       (size_t)s->dest[4] * 4);
			free(sc);
			break;
		}
		case 89:
			if (s->dest[4] > 0 && s->dest[5] > 0)
				pv_model89(px, w, h, s);
			break;
		default:
			assert(!"a command the model does not have");
		}
	}
	return px;
}

/* The built state against the model; returns its pixels (to be freed). */
static uint8_t *pv_expect(int no, int w, int h, int n, const struct pv_step *steps, const char *what)
{
	uint8_t *got = pv_pixels(no, w, h, what);
	uint8_t *model = pv_model(w, h, n, steps);
	pv_compare(got, w, h, model, what);
	free(model);
	return got;
}

static void pv_setup(void)
{
	cn_gl_setup();
	ri_postlink();
	pv_install();
}

/* --- the game's shapes --- */
static const struct pv_step pv_preview[] = { PV_CREATE(640, 180),
	{ .cmd = 13, .interp = 2, .src = { 0, 360, 1280, 360 }, .dest = { 0, 0, 0, 0, 640, 180 }, .cg = PVN_BACK } };
static const struct pv_step pv_bar[] = { PV_CREATE(14, 290),
	{ .cmd = 12, .interp = 1, .dest = { 0, 0, 0, 0, 14, 270 }, .cg = PVN_BAR },
	{ .cmd = 12, .interp = 1, .src = { 0, 440, 0, 0 }, .dest = { 0, 270, 0, 0, 14, 20 }, .cg = PVN_BAR } };
static const struct pv_step pv_stripe[] = { PV_CREATE(400, 320),
	{ .cmd = 12, .interp = 1, .dest = { 0, 0, 0, 0, 400, 320 }, .cg = PVN_PATTERN } };
static const struct pv_step pv_page[] = { PV_PIXELS(1024, 720), PV_FILL(0, 0, 1024, 720, 100, 100, 100) };
static const struct pv_step pv_item[] = { PV_PIXELS(500, 200), PV_FILL(0, 0, 500, 200, 50, 50, 50) };
static const struct pv_step pv_mask[] = { PV_CREATE(1480, 920), PV_RECT89(100, 100, 1280, 720, 0, 0, 0, 0) };

/* --- PV1 --- */
static void pv_preview_case(void)
{
	pv_setup();
	static const struct pv_def d[] = { { "Preview", 512, 414, PV_N(pv_preview), PV_EXTRA } };
	int no[1];
	if (!pv_load("PreviewGame", 1, d, no))
		return;
	uint8_t *px = pv_expect(no[0], 640, 180, PV_N(pv_preview), "PV1 the preview");
	/* By hand: column x, row y is the CG's (2x, 360 + 2y) (u = 512x, v =
	 * 512y: no weight); on opaque black s * 255 sar 8: 1 -> 0, 255 -> 254,
	 * 128 -> 127, 200 -> 199, 10 -> 9, 77 -> 76, 3 -> 2. (1, 0) takes (2,
	 * 360), not (1, 360), which is white. */
	pv_px(px, 640, 0, 0, 0, 0, 254, 255, "PV1 (0, 0)");
	pv_px(px, 640, 1, 0, 127, 199, 9, 255, "PV1 (1, 0)");
	pv_px(px, 640, 639, 179, 76, 0, 2, 255, "PV1 (639, 179)");
	free(px);
	struct parts *p = parts_try_get(no[0]);
	PV_CHECK(p && p->states[0].common.w == 640 && p->states[0].common.h == 180, "PV1: the parts is %dx%d, want 640x180",
		 p ? p->states[0].common.w : -1, p ? p->states[0].common.h : -1);
	PE_UpdateComponent(0);
	/* At (512, 414) it covers (512, 414)..(1151, 593); the buttons below it
	 * are centred on y 680 (end the game x 580, back to the title 857, back
	 * to the game 1132). */
	static const struct { int x, y, in; } at[] = {
		{ 512, 414, 1 }, { 1151, 593, 1 }, { 1152, 593, 0 }, { 1151, 594, 0 },
		{ 580, 680, 0 }, { 857, 680, 0 }, { 1132, 680, 0 },
	};
	for (unsigned i = 0; i < sizeof(at) / sizeof(at[0]); i++) {
		int in = BU_PE("Parts_IsCursorIn", BU_I(no[0]), BU_I(at[i].x), BU_I(at[i].y), BU_I(1)).i;
		PV_CHECK(!in == !at[i].in, "PV1: the preview %s under the cursor at (%d,%d)", in ? "is" : "is not", at[i].x, at[i].y);
	}
	printf("PV1 checked\n");
}

/* --- PV2 --- */
static const struct pv_step pv_blend[] = { PV_CREATE(12, 8), PV_FILL_ALL(10, 20, 30, 240),
	{ .cmd = 13, .interp = 2, .dest = { 3, 2, 0, 0, 5, 3 }, .cg = PVN_TINY } };
static const struct pv_step pv_i258[] = { PV_CREATE(12, 8), PV_FILL_ALL(10, 20, 30, 240),
	{ .cmd = 13, .interp = 258, .dest = { 3, 2, 0, 0, 5, 3 }, .cg = PVN_TINY } };
/* 元矩形 W INT_MIN: not positive, so all of the CG (0x5ab3b0), X and Y
 * ignored. */
static const struct pv_step pv_minblend[] = { PV_CREATE(12, 8), PV_FILL_ALL(10, 20, 30, 240),
	{ .cmd = 13, .interp = 2, .src = { 1, 1, INT_MIN, 2 }, .dest = { 3, 2, 0, 0, 5, 3 }, .cg = PVN_TINY } };
static const struct pv_step pv_cutblend[] = { PV_CREATE(6, 5), PV_FILL_ALL(200, 150, 100, 60),
	{ .cmd = 13, .interp = 2, .src = { 2, 1, 3, 2 }, .dest = { 1, 1, 0, 0, 4, 3 }, .cg = PVN_GRID } };

static void pv_blend_case(void)
{
	pv_setup();
	static const struct pv_def d[] = {
		{ "Blend", 0, 0, PV_N(pv_blend), 0 }, { "Interp258", 0, 0, PV_N(pv_i258), 0 },
		{ "Cut", 0, 0, PV_N(pv_cutblend), 0 }, { "Sjis", 0, 0, PV_N(pv_blend), PV_SJIS },
		{ "CutWidthMin", 0, 0, PV_N(pv_minblend), 0 },
	};
	int no[5];
	if (!pv_load("PreviewBlend", 5, d, no))
		return;
	uint8_t *px = pv_expect(no[0], 12, 8, PV_N(pv_blend), "PV2 the blend");
	/* The scaled 5 x 3 of cg-scale-copy at (3, 2) on (10, 20, 30, 240).
	 * (3, 2) is the CG's (0, 0), (0, 100, 200, 255): R 10 + (-10 * 255 sar
	 * 8) = 10 - 10 = 0 (truncated: 1), G 20 + 79 = 99, B 30 + 169 = 199.
	 * (4, 3) is the scaled (1, 1), (77, 124, 141, 178): 10 + 46 = 56, 20 + 72
	 * = 92, 30 + 77 = 107. (7, 2) is the CG's (2, 0), alpha 0: unchanged.
	 * (7, 4) is its (2, 1), (200, 200, 200, 200): 10 + 148 = 158, 20 + 140 =
	 * 160, 30 + 132 = 162. The alpha stays 240. */
	pv_px(px, 12, 3, 2, 0, 99, 199, 240, "PV2 (3, 2)");
	pv_px(px, 12, 4, 3, 56, 92, 107, 240, "PV2 (4, 3)");
	pv_px(px, 12, 7, 2, 10, 20, 30, 240, "PV2 (7, 2)");
	pv_px(px, 12, 7, 4, 158, 160, 162, 240, "PV2 (7, 4)");
	pv_px(px, 12, 2, 2, 10, 20, 30, 240, "PV2 left of it");
	pv_px(px, 12, 8, 4, 10, 20, 30, 240, "PV2 right of it");
	pv_px(px, 12, 3, 5, 10, 20, 30, 240, "PV2 below it");
	free(px);
	free(pv_expect(no[1], 12, 8, PV_N(pv_i258), "PV2 補間タイプ 258"));
	free(pv_expect(no[2], 6, 5, PV_N(pv_cutblend), "PV2 a cut"));
	free(pv_expect(no[3], 12, 8, PV_N(pv_blend), "PV2 the keys in SJIS"));
	pv_same(no[0], no[4], 12, 8, "PV2 元矩形 W INT_MIN is all of the CG");
	printf("PV2 checked\n");
}

/* --- PV3 --- */
/* 元矩形 3 wide, 先矩形 2: three columns, two rows copied. */
static const struct pv_step pv_wider[] = { PV_CREATE(8, 5), PV_FILL_ALL(9, 9, 9, 99),
	{ .cmd = 12, .src = { 1, 0, 3, 2 }, .dest = { 2, 1, 0, 0, 2, 1 }, .cg = PVN_GRID } };
/* 元矩形 1 x 1, 先矩形 2 x 2: two by two. */
static const struct pv_step pv_narrower[] = { PV_CREATE(8, 5), PV_FILL_ALL(9, 9, 9, 99),
	{ .cmd = 12, .src = { 3, 2, 1, 1 }, .dest = { 5, 3, 0, 0, 2, 2 }, .cg = PVN_GRID } };

static void pv_copy_case(void)
{
	pv_setup();
	static const struct pv_def d[] = {
		{ "Bar", 0, 0, PV_N(pv_bar), PV_EXTRA }, { "Stripe", 0, 0, PV_N(pv_stripe), PV_EXTRA },
		{ "Wider", 0, 0, PV_N(pv_wider), 0 }, { "Narrower", 0, 0, PV_N(pv_narrower), 0 },
		{ "BarSjis", 0, 0, PV_N(pv_bar), PV_SJIS },
	};
	int no[5];
	if (!pv_load("PreviewCopy", 5, d, no))
		return;
	uint8_t *px = pv_expect(no[0], 14, 290, PV_N(pv_bar), "PV3 the scroll bar's base");
	/* Rows 0..269 are the CG's, 270..289 its 440..459, alpha and all. */
	pv_px_cg(px, 14, 0, 0, PV_BAR, 0, 0, "PV3 (0, 0)");
	pv_px_cg(px, 14, 5, 269, PV_BAR, 5, 269, "PV3 (5, 269)");
	pv_px_cg(px, 14, 5, 270, PV_BAR, 5, 440, "PV3 (5, 270)");
	pv_px_cg(px, 14, 13, 289, PV_BAR, 13, 459, "PV3 (13, 289)");
	free(px);
	px = pv_expect(no[1], 400, 320, PV_N(pv_stripe), "PV3 the stripe");
	pv_px_cg(px, 400, 399, 319, PV_PATTERN, 399, 319, "PV3 the stripe's last pixel");
	free(px);
	px = pv_expect(no[2], 8, 5, PV_N(pv_wider), "PV3 元矩形 wider than 先矩形");
	pv_px_cg(px, 8, 4, 2, PV_GRID, 3, 1, "PV3 the third column of the second row");
	pv_px(px, 8, 5, 1, 9, 9, 9, 99, "PV3 right of it");
	pv_px(px, 8, 2, 3, 9, 9, 9, 99, "PV3 below it");
	free(px);
	px = pv_expect(no[3], 8, 5, PV_N(pv_narrower), "PV3 元矩形 narrower than 先矩形");
	pv_px_cg(px, 8, 6, 4, PV_GRID, 4, 3, "PV3 the second column of the second row");
	free(px);
	free(pv_expect(no[4], 14, 290, PV_N(pv_bar), "PV3 the keys in SJIS"));
	printf("PV3 checked\n");
}

/* --- PV4 --- */
static const struct pv_step pv_pixels_only[] = { PV_PIXELS(8, 4) };
static const struct pv_step pv_pixels_part[] = { PV_PIXELS(8, 4), PV_FILL(2, 1, 3, 2, 50, 60, 70) };

static void pv_pixels_case(void)
{
	pv_setup();
	static const struct pv_def d[] = {
		{ "Page", 0, 0, PV_N(pv_page), PV_EXTRA }, { "Item", 0, 0, PV_N(pv_item), PV_EXTRA },
		{ "Plain", 0, 0, PV_N(pv_pixels_only), 0 }, { "Part", 0, 0, PV_N(pv_pixels_part), 0 },
		{ "PageSjis", 0, 0, PV_N(pv_page), PV_SJIS },
	};
	static const int wh[5][2] = { { 1024, 720 }, { 500, 200 }, { 8, 4 }, { 8, 4 }, { 1024, 720 } };
	int no[5];
	if (!pv_load("PreviewPixels", 5, d, no))
		return;
	for (int i = 0; i < 5; i++) {
		char what[64];
		snprintf(what, sizeof what, "PV4 %s", d[i].name);
		free(pv_expect(no[i], wh[i][0], wh[i][1], d[i].n, d[i].steps, what));
		const int bits = pv_alpha_bits(no[i]);
		PV_CHECK(bits == 0, "%s: the texture has %d bits of alpha, want 0 (a surface of pixels only)", what, bits);
	}
	uint8_t *px = pv_pixels(no[0], 1024, 720, "PV4 the page");
	pv_px(px, 1024, 0, 0, 100, 100, 100, 255, "PV4 the page's first pixel");
	pv_px(px, 1024, 1023, 719, 100, 100, 100, 255, "PV4 the page's last pixel");
	free(px);
	px = pv_pixels(no[3], 8, 4, "PV4 a part filled");
	pv_px(px, 8, 2, 1, 50, 60, 70, 255, "PV4 the fill's first pixel");
	pv_px(px, 8, 1, 1, 0, 0, 0, 255, "PV4 left of the fill");
	pv_px(px, 8, 4, 3, 0, 0, 0, 255, "PV4 below the fill");
	free(px);
	printf("PV4 checked\n");
}

/* --- PV5 --- */
static const int pv_r_xy[] = { 20, 3, 31, 3, 31, 8, 20, 8 };
static const struct pv_step pv_r89[] = { PV_CREATE(40, 16), PV_FILL_ALL(30, 60, 90, 40),
	PV_RECT89(20, 3, 12, 6, 200, 100, 50, 200) };
static const struct pv_step pv_r94[] = { PV_CREATE(40, 16), PV_FILL_ALL(30, 60, 90, 40),
	{ .cmd = 94, .color = { 200, 100, 50, 200 }, .nr_xy = 8, .xy = pv_r_xy } };
static const struct pv_step pv_r360[] = { PV_CREATE(40, 16), PV_FILL_ALL(30, 60, 90, 40),
	{ .cmd = 89, .dest = { 20, 3, 0, 0, 12, 6 }, .color = { 200, 100, 50, 200 }, .rotate = 360 } };
static const struct pv_step pv_rw0[] = { PV_CREATE(40, 16), PV_RECT89(2, 2, 0, 10, 1, 2, 3, 4), PV_ALPHA_ALL(200) };
static const struct pv_step pv_rhmin[] = { PV_CREATE(40, 16), PV_RECT89(2, 2, 10, INT_MIN, 1, 2, 3, 4), PV_ALPHA_ALL(200) };

static void pv_rect_case(void)
{
	pv_setup();
	static const struct pv_def d[] = {
		{ "Mask", -100, -100, PV_N(pv_mask), PV_EXTRA }, { "R89", 0, 0, PV_N(pv_r89), 0 }, { "R94", 0, 0, PV_N(pv_r94), 0 },
		{ "R360", 0, 0, PV_N(pv_r360), 0 }, { "RSjis", 0, 0, PV_N(pv_r89), PV_SJIS }, { "Width0", 0, 0, PV_N(pv_rw0), 0 },
		{ "HeightMin", 0, 0, PV_N(pv_rhmin), 0 },
	};
	int no[7];
	if (!pv_load("PreviewRect", 7, d, no))
		return;
	uint8_t *px = pv_expect(no[0], 1480, 920, PV_N(pv_mask), "PV5 the mask");
	/* With A 0 every covered pixel becomes (0, 0, 0, 0): the columns 100 ..
	 * 1380 (1380 half covered) and the rows 100 .. 819. */
	pv_px(px, 1480, 99, 400, 0, 0, 0, 255, "PV5 left of the hole");
	pv_px(px, 1480, 100, 400, 0, 0, 0, 0, "PV5 the hole's left column");
	pv_px(px, 1480, 1380, 400, 0, 0, 0, 0, "PV5 the hole's half column on the right");
	pv_px(px, 1480, 1381, 400, 0, 0, 0, 255, "PV5 right of the hole");
	pv_px(px, 1480, 640, 99, 0, 0, 0, 255, "PV5 above the hole");
	pv_px(px, 1480, 640, 100, 0, 0, 0, 0, "PV5 the hole's top row");
	pv_px(px, 1480, 640, 819, 0, 0, 0, 0, "PV5 the hole's bottom row");
	pv_px(px, 1480, 640, 820, 0, 0, 0, 255, "PV5 below the hole");
	pv_px(px, 1480, 1479, 919, 0, 0, 0, 255, "PV5 the last pixel");
	free(px);
	px = pv_expect(no[1], 40, 16, PV_N(pv_r89), "PV5 a colour on a coloured base");
	/* Inside 200; the left column and the one past the right 127 a line:
	 * 200 * 127 / 255 = 99; the top row 4 lines (127): 99; the bottom row 5
	 * (159): 124; the top left corner 4 * 127 / 8 = 63: 49. The colour is
	 * replaced wherever the alpha is written. */
	pv_px(px, 40, 25, 5, 200, 100, 50, 200, "PV5 inside");
	pv_px(px, 40, 20, 5, 200, 100, 50, 99, "PV5 the left column");
	pv_px(px, 40, 32, 5, 200, 100, 50, 99, "PV5 the column past the right");
	pv_px(px, 40, 33, 5, 30, 60, 90, 40, "PV5 right of it");
	pv_px(px, 40, 25, 3, 200, 100, 50, 99, "PV5 the top row");
	pv_px(px, 40, 25, 8, 200, 100, 50, 124, "PV5 the bottom row");
	pv_px(px, 40, 25, 9, 30, 60, 90, 40, "PV5 below it");
	pv_px(px, 40, 20, 3, 200, 100, 50, 49, "PV5 the top left corner");
	free(px);
	pv_same(no[1], no[2], 40, 16, "PV5 89 against 94 with the corners");
	pv_same(no[1], no[3], 40, 16, "PV5 a turn of 360");
	pv_same(no[1], no[4], 40, 16, "PV5 the keys in SJIS");
	/* W 0 and H INT_MIN: nothing drawn (0x5069a2, 0x5069aa), the alpha
	 * step after it runs. */
	for (int c = 5; c <= 6; c++) {
		const char *what = c == 5 ? "PV5 W 0" : "PV5 H INT_MIN";
		px = pv_pixels(no[c], 40, 16, what);
		for (int i = 0; px && i < 40 * 16; i++) {
			if (memcmp(px + i * 4, (uint8_t[]){ 0, 0, 0, 200 }, 4)) {
				pv_px(px, 40, i % 40, i / 40, 0, 0, 0, 200, what);
				break;
			}
		}
		free(px);
	}
	printf("PV5 checked\n");
}

/* --- PV6 --- */
#define PV_SCALE(name, i, x, y, w, h) { .cmd = 13, .interp = i, .dest = { x, y, 0, 0, w, h }, .cg = name }
static const struct pv_step pv_u_i0[] = { PV_CREATE(5, 3), PV_SCALE(PVN_TINY, 0, 0, 0, 5, 3) };
static const struct pv_step pv_u_i1[] = { PV_CREATE(5, 3), PV_SCALE(PVN_TINY, 1, 0, 0, 5, 3) };
static const struct pv_step pv_u_i3[] = { PV_CREATE(5, 3), PV_SCALE(PVN_TINY, 3, 0, 0, 5, 3) };
static const struct pv_step pv_u_srcout[] = { PV_CREATE(5, 3),
	{ .cmd = 13, .interp = 2, .src = { 1, 0, 3, 2 }, .dest = { 0, 0, 0, 0, 5, 3 }, .cg = PVN_TINY } };
static const struct pv_step pv_u_destout[] = { PV_CREATE(5, 3), PV_SCALE(PVN_TINY, 2, 1, 0, 5, 3) };
static const struct pv_step pv_u_missing13[] = { PV_CREATE(5, 3), PV_SCALE("pv/none", 2, 0, 0, 5, 3) };
static const struct pv_step pv_u_first13[] = { PV_SCALE(PVN_TINY, 2, 0, 0, 5, 3) };
static const struct pv_step pv_u_pixels13[] = { PV_PIXELS(5, 3), PV_SCALE(PVN_TINY, 2, 0, 0, 5, 3) };
static const struct pv_step pv_u_srcout12[] = { PV_CREATE(14, 30),
	{ .cmd = 12, .src = { 0, 440, 0, 0 }, .dest = { 0, 0, 0, 0, 14, 30 }, .cg = PVN_BAR } };
static const struct pv_step pv_u_destout12[] = { PV_CREATE(14, 20), { .cmd = 12, .dest = { 0, 0, 0, 0, 14, 30 }, .cg = PVN_BAR } };
static const struct pv_step pv_u_negsrc12[] = { PV_CREATE(14, 20),
	{ .cmd = 12, .src = { -1, 0, 0, 0 }, .dest = { 0, 0, 0, 0, 14, 20 }, .cg = PVN_BAR } };
static const struct pv_step pv_u_missing12[] = { PV_CREATE(14, 20), { .cmd = 12, .dest = { 0, 0, 0, 0, 14, 20 }, .cg = "pv/none" } };
static const struct pv_step pv_u_first12[] = { { .cmd = 12, .dest = { 0, 0, 0, 0, 14, 20 }, .cg = PVN_BAR } };
static const struct pv_step pv_u_pixels5[] = { PV_PIXELS(8, 4), PV_ALPHA_ALL(0) };
static const struct pv_step pv_u_pixels0[] = { PV_PIXELS(0, 4) };
static const struct pv_step pv_u_second1[] = { PV_CREATE(8, 4), PV_PIXELS(8, 4) };
static const struct pv_step pv_u_turn89[] = { PV_CREATE(40, 16),
	{ .cmd = 89, .dest = { 20, 3, 0, 0, 12, 6 }, .color = { 1, 2, 3, 4 }, .rotate = 30 } };
static const struct pv_step pv_u_first89[] = { PV_RECT89(2, 2, 10, 10, 1, 2, 3, 4) };
static const struct pv_step pv_u_out89[] = { PV_CREATE(40, 16), PV_RECT89(50, 2, 10, 10, 1, 2, 3, 4) };
/* Values near the ends of int (review M2): sums and products that leave 32
 * bits must be checked without overflowing (UBSan stops the probe). */
static const struct pv_step pv_u_maxsrcw12[] = { PV_CREATE(14, 20),
	{ .cmd = 12, .src = { 0, 0, INT_MAX, 0 }, .dest = { 0, 0, 0, 0, 14, 20 }, .cg = PVN_BAR } };
static const struct pv_step pv_u_maxdest12[] = { PV_CREATE(14, 20),
	{ .cmd = 12, .dest = { INT_MAX - 1, 0, 0, 0, 14, 20 }, .cg = PVN_BAR } };
static const struct pv_step pv_u_maxsrcx12[] = { PV_CREATE(14, 20),
	{ .cmd = 12, .src = { INT_MAX, 0, 0, 0 }, .dest = { 0, 0, 0, 0, 14, 20 }, .cg = PVN_BAR } };
static const struct pv_step pv_u_maxsrcx13[] = { PV_CREATE(5, 3),
	{ .cmd = 13, .interp = 2, .src = { INT_MAX, 0, 2, 2 }, .dest = { 0, 0, 0, 0, 5, 3 }, .cg = PVN_TINY } };
static const struct pv_step pv_u_maxdest13[] = { PV_CREATE(5, 3), PV_SCALE(PVN_TINY, 2, INT_MAX - 1, 0, 5, 3) };
/* (8192 - 1) * 1280 * 256 and (8192 - 1) * 1100 * 256 leave 32 bits. */
static const struct pv_step pv_u_wrapw13[] = { PV_CREATE(8192, 2), PV_SCALE(PVN_BACK, 2, 0, 0, 8192, 2) };
static const struct pv_step pv_u_wraph13[] = { PV_CREATE(2, 8192), PV_SCALE(PVN_TALL, 2, 0, 0, 2, 8192) };
static const struct pv_step pv_u_maxx89[] = { PV_CREATE(40, 16), PV_RECT89(INT_MAX - 5, 2, 10, 10, 1, 2, 3, 4) };
static const struct pv_step pv_u_maxy89[] = { PV_CREATE(40, 16), PV_RECT89(2, INT_MAX - 5, 10, 10, 1, 2, 3, 4) };
static const struct pv_step pv_u_minx89[] = { PV_CREATE(40, 16), PV_RECT89(INT_MIN, 2, 10, 10, 1, 2, 3, 4) };
/* The steps after a surface of a CG (command 2) or of pixels only (1), which
 * the original writes in other ways (review L1). */
#define PV_CREATE_CG(name) { .cmd = 2, .cg = name }
static const struct pv_step pv_u_cg13[] = { PV_CREATE_CG(PVN_TINY), PV_SCALE(PVN_TINY, 2, 0, 0, 3, 2) };
static const struct pv_step pv_u_cg12[] = { PV_CREATE_CG(PVN_BAR), { .cmd = 12, .dest = { 0, 0, 0, 0, 14, 20 }, .cg = PVN_BAR } };
static const struct pv_step pv_u_pixels12[] = { PV_PIXELS(14, 20), { .cmd = 12, .dest = { 0, 0, 0, 0, 14, 20 }, .cg = PVN_BAR } };
static const struct pv_step pv_u_pixels89[] = { PV_PIXELS(40, 16), PV_RECT89(2, 2, 10, 10, 1, 2, 3, 4) };

static void pv_unbuilt_case(void)
{
	pv_setup();
	/* cg: the CG the legacy fallback shows, or -1 for none (then the canvas
	 * is the first step's when it makes one). */
	static const struct { struct pv_def d; int cg; } u[] = {
		{ { "Interp0", 0, 0, PV_N(pv_u_i0), 0 }, PV_TINY }, { { "Interp1", 0, 0, PV_N(pv_u_i1), 0 }, PV_TINY },
		{ { "Interp3", 0, 0, PV_N(pv_u_i3), 0 }, PV_TINY }, { { "CutOutsideTheCg", 0, 0, PV_N(pv_u_srcout), 0 }, PV_TINY },
		{ { "OutsideTheSurface", 0, 0, PV_N(pv_u_destout), 0 }, PV_TINY },
		{ { "Missing13", 0, 0, PV_N(pv_u_missing13), 0 }, -1 }, { { "First13", 0, 0, PV_N(pv_u_first13), 0 }, PV_TINY },
		{ { "PixelsThen13", 0, 0, PV_N(pv_u_pixels13), 0 }, PV_TINY },
		{ { "CopyOutsideTheCg", 0, 0, PV_N(pv_u_srcout12), 0 }, PV_BAR },
		{ { "CopyOutsideTheSurface", 0, 0, PV_N(pv_u_destout12), 0 }, PV_BAR },
		{ { "CopyFromMinus1", 0, 0, PV_N(pv_u_negsrc12), 0 }, PV_BAR },
		{ { "Missing12", 0, 0, PV_N(pv_u_missing12), 0 }, -1 }, { { "First12", 0, 0, PV_N(pv_u_first12), 0 }, PV_BAR },
		{ { "PixelsThen5", 0, 0, PV_N(pv_u_pixels5), 0 }, -1 }, { { "PixelsWidth0", 0, 0, PV_N(pv_u_pixels0), 0 }, -1 },
		{ { "SecondSurface", 0, 0, PV_N(pv_u_second1), 0 }, -1 }, { { "Turn30", 0, 0, PV_N(pv_u_turn89), 0 }, -1 },
		{ { "First89", 0, 0, PV_N(pv_u_first89), 0 }, -1 }, { { "Outside89", 0, 0, PV_N(pv_u_out89), 0 }, -1 },
		{ { "CopyCutWidthMax", 0, 0, PV_N(pv_u_maxsrcw12), 0 }, PV_BAR },
		{ { "CopyToNearMax", 0, 0, PV_N(pv_u_maxdest12), 0 }, PV_BAR },
		{ { "CopyFromXMax", 0, 0, PV_N(pv_u_maxsrcx12), 0 }, PV_BAR },
		{ { "BlendFromXMax", 0, 0, PV_N(pv_u_maxsrcx13), 0 }, PV_TINY },
		{ { "BlendToNearMax", 0, 0, PV_N(pv_u_maxdest13), 0 }, PV_TINY },
		{ { "BlendWidthWraps", 0, 0, PV_N(pv_u_wrapw13), 0 }, PV_BACK },
		{ { "BlendHeightWraps", 0, 0, PV_N(pv_u_wraph13), 0 }, PV_TALL },
		{ { "RectXNearMax", 0, 0, PV_N(pv_u_maxx89), 0 }, -1 }, { { "RectYNearMax", 0, 0, PV_N(pv_u_maxy89), 0 }, -1 },
		{ { "RectXMin", 0, 0, PV_N(pv_u_minx89), 0 }, -1 },
		{ { "CgThen13", 0, 0, PV_N(pv_u_cg13), 0 }, PV_TINY }, { { "CgThen12", 0, 0, PV_N(pv_u_cg12), 0 }, PV_BAR },
		{ { "PixelsThen12", 0, 0, PV_N(pv_u_pixels12), 0 }, PV_BAR },
		{ { "PixelsThen89", 0, 0, PV_N(pv_u_pixels89), 0 }, -1 },
	};
	enum { NU = sizeof(u) / sizeof(u[0]) };
	struct pv_def d[NU];
	for (int i = 0; i < NU; i++)
		d[i] = u[i].d;
	int no[NU];
	if (!pv_load("PreviewUnbuilt", NU, d, no))
		return;
	for (int i = 0; i < NU; i++) {
		char what[64];
		snprintf(what, sizeof what, "PV6 %s", d[i].name);
		if (u[i].cg >= 0) {
			pv_legacy(no[i], u[i].cg, what);
			continue;
		}
		const struct pv_step *s0 = &d[i].steps[0];
		const bool canvas = (s0->cmd == 0 || s0->cmd == 1) && s0->dest[4] > 0 && s0->dest[5] > 0;
		cn_expect_unbuilt(no[i], canvas ? s0->dest[4] : 0, canvas ? s0->dest[5] : 0, what);
	}
	pv_failures += cn_failures;
	cn_failures = 0;
	printf("PV6 checked\n");
}

/* --- PV7 --- */
/* The operations of the two types new in XPE version 13 (30 and 31, the
 * port's for 12 and 13; parts_internal.h pins the numbers) in the states
 * `nos`, and how many of them the save does not hold as their type followed
 * by the fields of 7, 8 and 29 (the CG's number, dx, dy, dw, dh, sx, sy, sw,
 * sh, 補間タイプ; save.c). The layout is what later builds read back, and a
 * save without it reads the rest out of step (checked before PE_Load). The
 * numbers, not the names: this file builds before the fix. */
static int pv_saved_cut_ops(struct page *saved, const int *nos, int n, int *found)
{
	int missing = 0;
	*found = 0;
	for (int k = 0; k < n; k++) {
		struct parts *p = parts_try_get(nos[k]);
		if (!p || p->states[0].type != PARTS_CONSTRUCTION_PROCESS)
			continue;
		struct parts_cp_op *op;
		TAILQ_FOREACH(op, &p->states[0].cproc.ops, entry) {
			if ((int)op->type != 30 && (int)op->type != 31)
				continue;
			(*found)++;
			const struct parts_cp_cut_cg *c = &op->cut_cg;
			const int want[11] = { (int)op->type, c->cg_no, c->dx, c->dy, c->dw, c->dh,
				c->sx, c->sy, c->sw, c->sh, c->interp_type };
			bool hit = false;
			for (int i = 0; !hit && i + 11 <= saved->nr_vars; i++) {
				int j = 0;
				while (j < 11 && saved->values[i + j].i == want[j])
					j++;
				hit = j == 11;
			}
			missing += !hit;
		}
	}
	return missing;
}

static void pv_save_case(void)
{
	pv_setup();
	static const struct pv_def d[] = {
		{ "Preview", 512, 414, PV_N(pv_preview), PV_EXTRA }, { "Bar", 0, 0, PV_N(pv_bar), PV_EXTRA },
		{ "Item", 0, 0, PV_N(pv_item), PV_EXTRA }, { "R89", 0, 0, PV_N(pv_r89), 0 }, { "Blend", 0, 0, PV_N(pv_blend), 0 },
	};
	static const int wh[5][2] = { { 640, 180 }, { 14, 290 }, { 500, 200 }, { 40, 16 }, { 12, 8 } };
	int no[5];
	if (!pv_load("PreviewSave", 5, d, no))
		return;
	uint8_t *before[5];
	int ops[5];
	for (int i = 0; i < 5; i++) {
		before[i] = pv_pixels(no[i], wh[i][0], wh[i][1], "PV7 before the save");
		ops[i] = pv_ops(no[i]);
	}
	PE_UpdateComponent(0);
	struct page *saved = NULL;
	PV_CHECK(PE_Save(&saved) && saved, "PV7: PE_Save failed");
	int words = -1;
	bool layout = false;
	if (saved) {
		words = saved->nr_vars;
		PV_CHECK(ca_save_version(saved) == 13, "PV7: the save is XPE v%d, want 13 (two new operation types)",
			 ca_save_version(saved));
		/* Preview and Blend have one 13 each, Bar two 12. */
		int found = 0;
		const int missing = pv_saved_cut_ops(saved, no, 5, &found);
		layout = found == 4 && !missing;
		PV_CHECK(layout, "PV7: the save holds %d of %d operations of 12 and 13 as their type and fields, want 4 of 4",
			 found - missing, found);
		if (layout) {
			PV_CHECK(PE_Load(&saved), "PV7: PE_Load failed");
			PE_UpdateComponent(0);
		}
		ca_free_buffer(saved);
	}
	if (!layout) {
		/* Nothing to load (it would read the rest out of step). */
		for (int i = 0; i < 5; i++)
			free(before[i]);
		printf("PV7 checked\n");
		return;
	}
	for (int i = 0; i < 5; i++) {
		char what[64];
		snprintf(what, sizeof what, "PV7 %s after PE_Load", d[i].name);
		uint8_t *after = pv_pixels(no[i], wh[i][0], wh[i][1], what);
		if (before[i] && after)
			pv_compare(after, wh[i][0], wh[i][1], before[i], what);
		PV_CHECK(pv_ops(no[i]) == ops[i] && ops[i] > 0, "%s: %d operations, want %d", what, pv_ops(no[i]), ops[i]);
		free(after);
		free(before[i]);
	}
	/* The surface of pixels only stays one (review L3). */
	const int bits = pv_alpha_bits(no[2]);
	PV_CHECK(bits == 0, "PV7 Item after PE_Load: the texture has %d bits of alpha, want 0", bits);
	struct page *again = NULL;
	PV_CHECK(PE_Save(&again) && again && again->nr_vars == words, "PV7: the save after PE_Load has %d words, the first %d",
		 again ? again->nr_vars : -1, words);
	ca_free_buffer(again);

	/* A save as version 12 wrote it (the number alone differs) loads the
	 * same. */
	const int PLAIN = 9761;
	cc_new(PLAIN, 8, 4, 10, 20, 30, 200);
	cc_dot(PLAIN, 2, 1, 250);
	uint8_t *plain = cc_pixels(PLAIN, 8, 4, "PV7 plain");
	pv_failures += cc_failures;
	cc_failures = 0;
	PE_UpdateComponent(0);
	saved = NULL;
	PV_CHECK(PE_Save(&saved) && saved, "PV7: PE_Save failed");
	const int pos = saved ? cg_version_pos(saved) : -1;
	PV_CHECK(pos >= 0, "PV7: no version in the save");
	if (saved && plain && pos >= 0) {
		saved->values[pos].i = 12;
		PV_CHECK(ca_save_version(saved) == 12 && PE_Load(&saved), "PV7: a version 12 save did not load");
		PE_UpdateComponent(0);
		struct parts *pp = parts_try_get(PLAIN);
		uint8_t *g = pp && pp->states[0].common.texture.handle ? gfx_get_pixels(&pp->states[0].common.texture) : NULL;
		PV_CHECK(g && !memcmp(g, plain, 8 * 4 * 4) && cc_nr_ops(PLAIN) == 3,
			 "PV7: the version 12 save loads other pixels (%d operations)", cc_nr_ops(PLAIN));
		free(g);
	}
	ca_free_buffer(saved);
	free(plain);
	printf("PV7 checked\n");
}

/* --- PV8 --- */
static const struct pv_step pv_old14[] = { PV_CREATE(5, 3), { .cmd = 14, .interp = 2, .dest = { 0, 0, 0, 0, 5, 3 }, .cg = PVN_TINY } };
static const struct pv_step pv_old90[] = { PV_CREATE(40, 16), PV_FILL_ALL(30, 60, 90, 40),
	{ .cmd = 90, .dest = { 20, 3, 0, 0, 12, 6 }, .color = { 0, 0, 0, 200 } } };

static void pv_guards_case(void)
{
	pv_setup();
	/* Next to the new states, two states of commands built before: 14
	 * (cg-scale-copy's model) and 90 (its alpha as 89's, the colours kept). */
	static const struct pv_def d[] = {
		{ "Preview", 512, 414, PV_N(pv_preview), 0 }, { "Old14", 0, 0, PV_N(pv_old14), 0 },
		{ "Old90", 0, 0, PV_N(pv_old90), 0 }, { "Bar", 0, 0, PV_N(pv_bar), 0 },
	};
	int no[4];
	if (!pv_load("PreviewOld", 4, d, no))
		return;
	free(pv_expect(no[1], 5, 3, PV_N(pv_old14), "PV8 14 next to them"));
	uint8_t *px = pv_pixels(no[2], 40, 16, "PV8 90 next to them");
	pv_px(px, 40, 25, 5, 30, 60, 90, 200, "PV8 90 inside");
	pv_px(px, 40, 20, 5, 30, 60, 90, 99, "PV8 90's left column");
	pv_px(px, 40, 33, 5, 30, 60, 90, 40, "PV8 right of 90");
	free(px);

	/* Before v14: none of the eight shapes is a construction; the CG ones
	 * keep their CG (the legacy fallback), and the save is XPE v3. */
	const int version = ain->version;
	ain->version = 13;
	static const struct pv_def v[] = {
		{ "Preview", 512, 414, PV_N(pv_preview), 0 }, { "Bar", 0, 0, PV_N(pv_bar), 0 },
		{ "Stripe", 0, 0, PV_N(pv_stripe), 0 }, { "Page", 0, 0, PV_N(pv_page), 0 }, { "Mask", -100, -100, PV_N(pv_mask), 0 },
	};
	static const int cg[5] = { PV_BACK, PV_BAR, PV_PATTERN, -1, -1 };
	int nv[5];
	if (pv_load("PreviewV13", 5, v, nv)) {
		for (int i = 0; i < 5; i++) {
			char what[64];
			snprintf(what, sizeof what, "PV8 v13 %s", v[i].name);
			if (cg[i] >= 0) {
				pv_legacy(nv[i], cg[i], what);
				continue;
			}
			struct parts *p = parts_try_get(nv[i]);
			PV_CHECK(p && p->states[0].type != PARTS_CONSTRUCTION_PROCESS && !p->states[0].common.texture.handle,
				 "%s: state type %d, texture %s; want it unbuilt", what, p ? (int)p->states[0].type : -1,
				 p && p->states[0].common.texture.handle ? "present" : "missing");
		}
	}
	PE_UpdateComponent(0);
	struct page *saved = NULL;
	PV_CHECK(PE_Save(&saved) && saved && ca_save_version(saved) == 3, "PV8: a v13 save is XPE v%d, want 3",
		 saved ? ca_save_version(saved) : -1);
	ca_free_buffer(saved);
	ain->version = version;
	printf("PV8 checked\n");
}

static void (*pv_case)(void);
static void pv_case_body(void)
{
	pv_case();
	pv_failures += cn_failures + cc_failures + ca_failures;
}

static int test_construction_preview(void)
{
	static const struct { const char *name; void (*fn)(void); } cases[] = {
		{ "PV1", pv_preview_case }, { "PV2", pv_blend_case }, { "PV3", pv_copy_case }, { "PV4", pv_pixels_case },
		{ "PV5", pv_rect_case }, { "PV6", pv_unbuilt_case }, { "PV7", pv_save_case }, { "PV8", pv_guards_case },
	};
	const int n = sizeof(cases) / sizeof(cases[0]);
	int failed = 0, env = 0;
	for (int i = 0; i < n; i++) {
		pv_case = cases[i].fn;
		int r = cn_fork_case(cases[i].name, pv_case_body, &pv_failures);
		failed += r == 1;
		env += r == 2;
	}
	if (env) {
		printf("CONSTRUCTION-PREVIEW ENVIRONMENT FAILURE: %d of %d cases could not run, %d failed\n", env, n, failed);
		return 2;
	}
	printf("CONSTRUCTION-PREVIEW %s: %d of %d cases failed\n", failed ? "FAIL" : "PASS", failed, n);
	return failed ? 1 : 0;
}
