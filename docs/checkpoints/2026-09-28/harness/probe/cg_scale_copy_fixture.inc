/* cg-scale-copy: the pactex 構築部件 states that scale a CG into a rectangle
 * (construction command 14, CutCGScaleCopy), as the original engine builds
 * them while the pactex loads (static disassembly only):
 *   - table 0x4fb7d8: 14 -> 0x4fe5a0; the step parser (0x4f82a0) keeps
 *     補間タイプ's low byte at +8 (0x4f87e4) and 元矩形 X,Y,W,H at
 *     +0xc..+0x18; a step that fails skips the rest (0x4fb675);
 *   - 0x50b350 loads ＣＧ名 onto a surface of its own (0x5a9ea0); a 元矩形
 *     without a positive width and height takes all of the CG (0x5ab3b0);
 *     for 補間タイプ 2 it scales that to 先矩形 W x H (0x5aa9c0, which runs
 *     the backend's 0x4929e0);
 *   - 0x4929e0 is bilinear in 8-bit fixed point from the pixels' corners:
 *     column x reads the source at u = (x * sw << 8) / dw, columns u >> 8 and
 *     the next one weighted by u & 0xff, the last column alone once u >> 8
 *     reaches sw - 1; rows likewise; each byte is t = p + ((q - p) * fx >> 8)
 *     along the upper row, t' the same along the lower one, and t + ((t' - t)
 *     * fy >> 8), every shift arithmetic (it floors);
 *   - the scaled CG is copied to 先矩形 X,Y with its alpha on a surface that
 *     has one (0x5abb80 -> 0x5acd70 -> 0x4918e0), the colours alone on one
 *     that has none (0x5ad010 -> 0x491860 -> 0x4940f0).
 * The game has two such states, both (0, 14): a 370 x 70 surface with a
 * 628 x 94 button frame scaled into it (the map result's box when no item
 * was found, and the box of a found item). Before the fix the loader did not
 * take 14 and the legacy CG fallback showed the frame at its own size.
 *
 * The expected pixels: the formula above written out a second time here
 * (cs_scale_model), and, for a 3 x 2 CG scaled up to 5 x 3 and down to 2 x 1,
 * pixels worked out by hand.
 *
 * Cases (each in its own fork):
 *   CS1 the game's shape (a 628 x 94 noise CG with an alpha into 370 x 70):
 *       built, two operations, every pixel against the model, three by hand.
 *   CS2 the 3 x 2 CG: 5 x 3 and 2 x 1 by hand, among them pixels where a
 *       truncating shift or a rounded weight would differ, and against the
 *       model; a 1 x 1 CG into 4 x 3; the same size (3 x 2) is the CG.
 *   CS3 within a larger opaque surface: what lies outside the rectangle is
 *       kept, the alpha inside is the CG's; the steps' keys spelled in SJIS;
 *       補間タイプ 258 (low byte 2); a 元矩形 with no width (all of the CG).
 *   CS4 what stays unbuilt (the legacy CG, as before): 補間タイプ 0, 1 and 3,
 *       a 元矩形 with a width and a height, a CG that does not exist, one
 *       without an alpha, a width or height of 0, a rectangle that leaves the
 *       surface or starts left of it, 14 as the first step, 14 after a CG
 *       surface (2), no 補間タイプ, no 元矩形; at the 32-bit limit of the
 *       fixed point a 4096-wide CG is built into 2048 columns, not into 2049.
 *       They pass before the fix as well (guards).
 *   CS5 PE_Save / PE_Load: XPE version 12 (13 since construction-preview),
 *       the same pixels and operations;
 *       a save without the operation rewritten as version 11 loads the same.
 *   CS6 before v14 nothing of this is built (guard); the save is version 3.
 *   CS7 the AIN's command 14 (AddPartsConstructionProcess) still adds the
 *       copy-cut-CG operation it added before (guard: this fix leaves it).
 * Only functions that exist before the fix are called: the file builds before
 * it (the new operation's type, 29, is written as a number).
 */
static int cs_failures;
#define CS_CHECK(c, ...) do { if (!(c)) { printf("  FAIL: "); printf(__VA_ARGS__); printf("\n"); cs_failures++; } } while (0)

#define CS_OP_SCALE_COPY 29
#define CS_INTERP "\xd1\x61\xe9\x67\xa5\xbf\xa5\xa4\xa5\xd7"		/* 補間タイプ */
#define CS_INTERP_S "\x95\xe2\x8a\xd4\x83\x5e\x83\x43\x83\x76"
#define CS_SRC "\xd4\xaa\xbe\xd8\xd0\xce"				/* 元矩形 */
#define CS_SRC_S "\x8c\xb3\x8b\xe9\x8c\x60"
#define CS_CG_NAME_S "\x82\x62\x82\x66\x96\xbc"				/* ＣＧ名 */

/* --- CGs made in memory --- */
#define CSN_FRAME "cs/frame"
#define CSN_TINY "cs/tiny"
#define CSN_ONE "cs/one"
#define CSN_RGB "cs/rgb"
#define CSN_WIDE "cs/wide"
enum { CS_FRAME, CS_TINY, CS_ONE, CS_RGB, CS_WIDE, CS_NR };
static struct {
	const char *name;
	int w, h;
	bool alpha;
	uint8_t *px;	/* RGBA */
} cs_src[CS_NR] = {
	[CS_FRAME] = { CSN_FRAME, 628, 94, true },
	[CS_TINY] = { CSN_TINY, 3, 2, true },
	[CS_ONE] = { CSN_ONE, 1, 1, true },
	[CS_RGB] = { CSN_RGB, 3, 2, false },
	[CS_WIDE] = { CSN_WIDE, 4096, 1, true },
};
static struct probe_cg_file cs_files[CS_NR];

/* The 3 x 2 CG, rows from the top. */
static const uint8_t cs_tiny_px[6][4] = {
	{ 0, 100, 200, 255 }, { 255, 50, 0, 128 }, { 10, 10, 10, 0 },
	{ 100, 0, 50, 64 }, { 0, 255, 255, 255 }, { 200, 200, 200, 200 },
};

static void cs_install(void)
{
	unsigned seed = 20261019;
	for (int i = 0; i < CS_NR; i++) {
		const int w = cs_src[i].w, h = cs_src[i].h;
		uint8_t *px = cs_src[i].px = calloc((size_t)w * h, 4);
		for (int k = 0; k < w * h; k++) {
			uint8_t *p = px + (size_t)k * 4;
			if (i == CS_TINY || i == CS_RGB) {
				memcpy(p, cs_tiny_px[k], 4);
				if (i == CS_RGB)
					p[3] = 255;
			} else if (i == CS_ONE) {
				p[0] = 30; p[1] = 60; p[2] = 90; p[3] = 120;
			} else {
				for (int c = 0; c < 4; c++) {
					seed = seed * 1103515245u + 12345u;
					p[c] = seed >> 16;
				}
			}
		}
		cs_files[i].name = cs_src[i].name;
		cs_files[i].data = cg_png(w, h, px, cs_src[i].alpha, &cs_files[i].size);
	}
	probe_install_cg_assets(cs_files, CS_NR);
}

/* --- pactex steps --- */
enum {
	CS_SJIS = 1 << 0,	/* the step keys and 手順リスト in SJIS */
	CS_NO_INTERP = 1 << 1,	/* no 補間タイプ */
	CS_NO_SRC = 1 << 2,	/* no 元矩形 */
};
struct cs_step {
	int cmd;
	int dest[6];		/* 先矩形: X, Y, X2, Y2, W, H */
	int src[4];		/* 元矩形 */
	int interp;		/* 補間タイプ */
	const char *cg;		/* ＣＧ名 */
	int color[4];		/* 色１ */
	int full;		/* 全體 */
};
#define CS_CREATE(w, h) { .cmd = 0, .dest = { 0, 0, 0, 0, w, h } }
#define CS_SCALE(name, x, y, w, h) { .cmd = 14, .dest = { x, y, 0, 0, w, h }, .interp = 2, .cg = name }
#define CS_FILL_ALL(r, g, b, a) { .cmd = 6, .color = { r, g, b, a }, .full = 1 }
#define CS_N(steps) (int)(sizeof(steps) / sizeof((steps)[0])), steps

static struct ex_tree cs_parts(const char *name, int n, const struct cs_step *steps, int flags)
{
	const bool sjis = flags & CS_SJIS;
	struct lb_place pl = { 0, 0, 1 };
	struct ex_tree info = bu_branch(BU_INFO, 2);
	info.children[0] = bu_sleaf(BU_TYPE, BU_LOW);
	info.children[1] = bu_branch(BU_NORMAL, 3);
	struct ex_tree *st = &info.children[1];
	st->children[0] = bu_sleaf(BU_TYPE, BR_CONS);
	st->children[1] = lb_ilist(CN_AREA, 4, (int[]){ 0, 0, 0, 0 });
	st->children[2] = bu_branch(sjis ? CN_STEPS_S : BR_STEPS, n);
	for (int i = 0; i < n; i++) {
		const struct cs_step *s = &steps[i];
		char key[32];
		snprintf(key, sizeof key, "Step%d", i + 1);
		struct ex_tree t = bu_branch(key, 8);
		unsigned k = 0;
		t.children[k++] = bu_ileaf(sjis ? CN_COMMAND_S : BR_COMMAND, s->cmd);
		t.children[k++] = lb_ilist(sjis ? CN_DEST_S : LB_DEST, 6, s->dest);
		if (!(flags & CS_NO_SRC))
			t.children[k++] = lb_ilist(sjis ? CS_SRC_S : CS_SRC, 4, s->src);
		if (!(flags & CS_NO_INTERP))
			t.children[k++] = bu_ileaf(sjis ? CS_INTERP_S : CS_INTERP, s->interp);
		t.children[k++] = lb_ilist(sjis ? CN_COLOR1_S : CN_COLOR1, 4, s->color);
		t.children[k++] = bu_ileaf(sjis ? CN_FULL_S : CN_FULL, s->full);
		t.children[k++] = bu_sleaf(sjis ? CS_CG_NAME_S : BR_CG_NAME, s->cg ? s->cg : "");
		assert(k <= 8);
		t.nr_children = k;
		st->children[2].children[i] = t;
	}
	return lb_node(name, &pl, info, 0);
}

/* --- the model, from the disassembly --- */

/* v >> 8 with v's sign, as sar does: the quotient rounded down. */
static int cs_floor256(int v)
{
	return v >= 0 ? v / 256 : -((-v + 255) / 256);
}

/* The sw x sh RGBA source scaled to dw x dh (to be freed), as 0x4929e0 does
 * it. */
static uint8_t *cs_scale_model(const uint8_t *src, int sw, int sh, int dw, int dh)
{
	uint8_t *out = malloc((size_t)dw * dh * 4);
	for (int y = 0; y < dh; y++) {
		const long long v = (long long)y * sh * 256 / dh;
		const int fy = (int)(v % 256);
		const int y0 = (int)(v / 256) < sh - 1 ? (int)(v / 256) : sh - 1;
		const int y1 = (int)(v / 256) < sh - 1 ? y0 + 1 : y0;
		for (int x = 0; x < dw; x++) {
			const long long u = (long long)x * sw * 256 / dw;
			const int fx = (int)(u % 256);
			const int x0 = (int)(u / 256) < sw - 1 ? (int)(u / 256) : sw - 1;
			const int x1 = (int)(u / 256) < sw - 1 ? x0 + 1 : x0;
			for (int c = 0; c < 4; c++) {
				const int p = src[((size_t)y0 * sw + x0) * 4 + c], q = src[((size_t)y0 * sw + x1) * 4 + c];
				const int p2 = src[((size_t)y1 * sw + x0) * 4 + c], q2 = src[((size_t)y1 * sw + x1) * 4 + c];
				const int top = p + cs_floor256((q - p) * fx);
				const int bottom = p2 + cs_floor256((q2 - p2) * fx);
				out[((size_t)y * dw + x) * 4 + c] = top + cs_floor256((bottom - top) * fy);
			}
		}
	}
	return out;
}

/* --- helpers --- */
static int cs_load(const char *name, struct ex *e)
{
	int act = cn_load(name, e);
	cs_failures += cn_failures;
	cn_failures = 0;
	return act;
}

static int cs_no(int act, const char *name)
{
	int n = bu_number(act, name);
	CS_CHECK(n > 0 && parts_try_get(n), "%s has no parts", name);
	return n > 0 && parts_try_get(n) ? n : 0;
}

static uint8_t *cs_built(int no, int w, int h, const char *what)
{
	uint8_t *got = cn_pixels(no, w, h, what);
	cs_failures += cn_failures;
	cn_failures = 0;
	return got;
}

static void cs_compare(const uint8_t *got, int w, int h, const uint8_t *model, const char *what)
{
	int bad = 0;
	for (int i = 0; got && i < w * h; i++) {
		if (!memcmp(got + (size_t)i * 4, model + (size_t)i * 4, 4))
			continue;
		if (bad < 3)
			CS_CHECK(0, "%s (%d,%d) = (%d,%d,%d,%d), want (%d,%d,%d,%d)", what, i % w, i / w, got[i * 4],
				 got[i * 4 + 1], got[i * 4 + 2], got[i * 4 + 3], model[i * 4], model[i * 4 + 1],
				 model[i * 4 + 2], model[i * 4 + 3]);
		bad++;
	}
	if (bad > 3)
		CS_CHECK(0, "%s: %d pixels differ in all", what, bad);
}

static void cs_px(const uint8_t *px, int w, int x, int y, int r, int g, int b, int a, const char *what)
{
	const uint8_t *p = px ? px + ((size_t)y * w + x) * 4 : NULL;
	CS_CHECK(p && p[0] == r && p[1] == g && p[2] == b && p[3] == a, "%s (%d,%d) = (%d,%d,%d,%d), want (%d,%d,%d,%d)",
		 what, x, y, p ? p[0] : -1, p ? p[1] : -1, p ? p[2] : -1, p ? p[3] : -1, r, g, b, a);
}

static int cs_last_op(int no)
{
	struct parts *p = parts_try_get(no);
	if (!p || p->states[0].type != PARTS_CONSTRUCTION_PROCESS)
		return -1;
	int type = -1;
	struct parts_cp_op *op;
	TAILQ_FOREACH(op, &p->states[0].cproc.ops, entry)
		type = op->type;
	return type;
}

/* The state as the legacy fallback leaves it: not a construction, the CG at
 * its own size. */
static void cs_legacy(int no, int cg, const char *what)
{
	struct parts *p = no > 0 ? parts_try_get(no) : NULL;
	CS_CHECK(p, "%s: no parts", what);
	if (!p)
		return;
	struct parts_state *s = &p->states[0];
	const bool cg_shown = cg >= 0 && s->common.texture.handle && s->common.texture.w == cs_src[cg].w
		&& s->common.texture.h == cs_src[cg].h;
	CS_CHECK(s->type != PARTS_CONSTRUCTION_PROCESS && (cg < 0 ? !s->common.texture.handle : cg_shown),
		 "%s: state type %d, texture %s %dx%d; want it unbuilt%s", what, s->type,
		 s->common.texture.handle ? "present" : "missing", s->common.texture.w, s->common.texture.h,
		 cg < 0 ? " with no texture" : ", the CG at its size");
}

static void cs_setup(void)
{
	cn_gl_setup();
	ri_postlink();
	cs_install();
}

/* --- the shapes --- */
static const struct cs_step cs_game[] = { CS_CREATE(370, 70), CS_SCALE(CSN_FRAME, 0, 0, 370, 70) };
static const struct cs_step cs_up[] = { CS_CREATE(5, 3), CS_SCALE(CSN_TINY, 0, 0, 5, 3) };
static const struct cs_step cs_down[] = { CS_CREATE(2, 1), CS_SCALE(CSN_TINY, 0, 0, 2, 1) };
static const struct cs_step cs_same[] = { CS_CREATE(3, 2), CS_SCALE(CSN_TINY, 0, 0, 3, 2) };
static const struct cs_step cs_one[] = { CS_CREATE(4, 3), CS_SCALE(CSN_ONE, 0, 0, 4, 3) };

/* --- CS1 --- */
static void cs_game_case(void)
{
	cs_setup();
	struct ex_tree *kids;
	struct ex *e = lb_activity("CgScaleCopyGame", 1, &kids);
	kids->children[0] = cs_parts("NoItemBase", CS_N(cs_game), 0);
	int act = cs_load("CgScaleCopyGame", e);
	int no = cs_no(act, "NoItemBase");
	heap_unref(act);
	if (!no)
		return;
	uint8_t *got = cs_built(no, 370, 70, "CS1 the game's shape");
	struct parts *p = parts_try_get(no);
	CS_CHECK(p && cn_nr_ops(p) == 2 && cs_last_op(no) == CS_OP_SCALE_COPY,
		 "CS1: %d operation(s), the last of type %d; want 2, the last %d", p ? cn_nr_ops(p) : -1, cs_last_op(no),
		 CS_OP_SCALE_COPY);
	uint8_t *model = cs_scale_model(cs_src[CS_FRAME].px, 628, 94, 370, 70);
	cs_compare(got, 370, 70, model, "CS1 the game's shape");
	if (got) {
		/* (0, 0) is the CG's (0, 0). (1, 0): u = 628 * 256 / 370 = 434
		 * (434.5), column 1 weighted by 434 - 256 = 178 between 1 and 2.
		 * (369, 69): u = 369 * 628 * 256 / 370 = 160333 (160333.5),
		 * columns 626 and 627 by 77; v = 69 * 94 * 256 / 70 = 23720
		 * (23720.2), rows 92 and 93 by 168. */
		const uint8_t *s = cs_src[CS_FRAME].px;
		const uint8_t *c00 = s;
		cs_px(got, 370, 0, 0, c00[0], c00[1], c00[2], c00[3], "CS1 (0, 0)");
		int want[4];
		for (int c = 0; c < 4; c++) {
			const int a = s[1 * 4 + c], b = s[2 * 4 + c];
			want[c] = a + cs_floor256((b - a) * 178);
		}
		cs_px(got, 370, 1, 0, want[0], want[1], want[2], want[3], "CS1 (1, 0)");
		for (int c = 0; c < 4; c++) {
			const int a = s[(92 * 628 + 626) * 4 + c], b = s[(92 * 628 + 627) * 4 + c];
			const int a2 = s[(93 * 628 + 626) * 4 + c], b2 = s[(93 * 628 + 627) * 4 + c];
			const int t = a + cs_floor256((b - a) * 77), t2 = a2 + cs_floor256((b2 - a2) * 77);
			want[c] = t + cs_floor256((t2 - t) * 168);
		}
		cs_px(got, 370, 369, 69, want[0], want[1], want[2], want[3], "CS1 (369, 69)");
	}
	CS_CHECK(p && p->states[0].common.w == 370 && p->states[0].common.h == 70, "CS1: the parts is %dx%d, want 370x70",
		 p ? p->states[0].common.w : -1, p ? p->states[0].common.h : -1);
	free(got);
	free(model);
	printf("CS1 checked\n");
}

/* --- CS2 --- */
static void cs_tiny_case(void)
{
	cs_setup();
	struct ex_tree *kids;
	struct ex *e = lb_activity("CgScaleCopyTiny", 4, &kids);
	kids->children[0] = cs_parts("Up", CS_N(cs_up), 0);
	kids->children[1] = cs_parts("Down", CS_N(cs_down), 0);
	kids->children[2] = cs_parts("Same", CS_N(cs_same), 0);
	kids->children[3] = cs_parts("One", CS_N(cs_one), 0);
	int act = cs_load("CgScaleCopyTiny", e);
	int up = cs_no(act, "Up"), down = cs_no(act, "Down"), same = cs_no(act, "Same"), one = cs_no(act, "One");
	heap_unref(act);

	/* 3 x 2 into 5 x 3. Columns: u = x * 768 / 5 = 0, 153, 307, 460, 614,
	 * i.e. column 0 by 0, 0 by 153, 1 by 51, 1 by 204, and 2 alone (614 >> 8
	 * = 2 = sw - 1). Rows: v = y * 512 / 3 = 0, 170, 341: row 0 by 0, row 0
	 * by 170, row 1 alone.
	 * (1, 1): upper R 0 + (255 * 153 >> 8) = 0 + 152 = 152, lower R 100 +
	 * (-100 * 153 >> 8) = 100 - 60 = 40 (a truncating shift gives -59), then
	 * 152 + (-112 * 170 >> 8) = 152 - 75 = 77 (truncated: 78); G 100 - 30 =
	 * 70, 0 + 152 = 152, 70 + (82 * 170 >> 8) = 70 + 54 = 124; B 200 - 120
	 * = 80, 50 + 122 = 172, 80 + 61 = 141; A 255 - 76 = 179, 64 + 114 =
	 * 178, 179 - 1 = 178.
	 * (0, 1): row 0 by 170 alone: R 0 + 66 = 66, G 100 - 67 = 33, B 200 -
	 * 100 = 100, A 255 - 127 = 128.
	 * (3, 0): column 1 by 204: R 255 + (-245 * 204 >> 8) = 255 - 196 = 59,
	 * G 50 - 32 = 18, B 0 + 7 = 7, A 128 - 102 = 26.
	 * (2, 2): row 1, column 1 by 51: R 0 + 39 = 39, G 255 - 11 = 244, B
	 * 244, A 244. (4, 2) is the CG's (2, 1), (4, 0) its (2, 0). */
	uint8_t *got = cs_built(up, 5, 3, "CS2 5 x 3");
	cs_px(got, 5, 1, 1, 77, 124, 141, 178, "CS2 5 x 3");
	cs_px(got, 5, 0, 1, 66, 33, 100, 128, "CS2 5 x 3");
	cs_px(got, 5, 3, 0, 59, 18, 7, 26, "CS2 5 x 3");
	cs_px(got, 5, 2, 2, 39, 244, 244, 244, "CS2 5 x 3");
	cs_px(got, 5, 4, 2, 200, 200, 200, 200, "CS2 5 x 3");
	cs_px(got, 5, 4, 0, 10, 10, 10, 0, "CS2 5 x 3");
	cs_px(got, 5, 0, 0, 0, 100, 200, 255, "CS2 5 x 3");
	uint8_t *model = cs_scale_model(cs_src[CS_TINY].px, 3, 2, 5, 3);
	cs_compare(got, 5, 3, model, "CS2 5 x 3");
	free(model);
	free(got);

	/* 3 x 2 into 2 x 1: u = 0, 384 (column 1 by 128), v = 0. (1, 0): R 255 +
	 * (-245 * 128 >> 8) = 255 - 123 = 132 (truncated: 133), G 50 - 20 = 30,
	 * B 0 + 5 = 5, A 128 - 64 = 64. */
	got = cs_built(down, 2, 1, "CS2 2 x 1");
	cs_px(got, 2, 0, 0, 0, 100, 200, 255, "CS2 2 x 1");
	cs_px(got, 2, 1, 0, 132, 30, 5, 64, "CS2 2 x 1");
	free(got);

	/* The same size: u = x * 256, the CG itself. */
	got = cs_built(same, 3, 2, "CS2 3 x 2");
	cs_compare(got, 3, 2, cs_src[CS_TINY].px, "CS2 3 x 2");
	free(got);

	/* One pixel everywhere. */
	got = cs_built(one, 4, 3, "CS2 1 x 1 into 4 x 3");
	for (int y = 0; got && y < 3; y++)
		for (int x = 0; x < 4; x++)
			cs_px(got, 4, x, y, 30, 60, 90, 120, "CS2 1 x 1 into 4 x 3");
	free(got);
	printf("CS2 checked\n");
}

/* --- CS3 --- */
static const struct cs_step cs_inside[] = { CS_CREATE(12, 8), CS_FILL_ALL(10, 20, 30, 240),
	CS_SCALE(CSN_TINY, 3, 2, 5, 3), CS_SCALE(CSN_ONE, 11, 7, 1, 1) };
static const struct cs_step cs_interp258[] = { CS_CREATE(5, 3),
	{ .cmd = 14, .dest = { 0, 0, 0, 0, 5, 3 }, .interp = 258, .cg = CSN_TINY } };
static const struct cs_step cs_src_nowidth[] = { CS_CREATE(5, 3),
	{ .cmd = 14, .dest = { 0, 0, 0, 0, 5, 3 }, .src = { 1, 1, 0, 1 }, .interp = 2, .cg = CSN_TINY } };

static void cs_place_case(void)
{
	cs_setup();
	struct ex_tree *kids;
	struct ex *e = lb_activity("CgScaleCopyPlace", 5, &kids);
	kids->children[0] = cs_parts("Inside", CS_N(cs_inside), 0);
	kids->children[1] = cs_parts("Sjis", CS_N(cs_inside), CS_SJIS);
	kids->children[2] = cs_parts("Interp258", CS_N(cs_interp258), 0);
	kids->children[3] = cs_parts("NoWidth", CS_N(cs_src_nowidth), 0);
	kids->children[4] = cs_parts("Game", CS_N(cs_game), CS_SJIS);
	int act = cs_load("CgScaleCopyPlace", e);
	int inside = cs_no(act, "Inside"), sjis = cs_no(act, "Sjis"), i258 = cs_no(act, "Interp258");
	int nowidth = cs_no(act, "NoWidth"), game = cs_no(act, "Game");
	heap_unref(act);

	/* The base (10, 20, 30, 240), the 5 x 3 of CS2 at (3, 2), the 1 x 1 CG
	 * at the last pixel; the alpha inside is the CG's (the copy writes all
	 * four bytes on a surface with an alpha). */
	uint8_t *model = malloc(12 * 8 * 4);
	for (int i = 0; i < 12 * 8; i++)
		memcpy(model + i * 4, (uint8_t[]){ 10, 20, 30, 240 }, 4);
	uint8_t *up = cs_scale_model(cs_src[CS_TINY].px, 3, 2, 5, 3);
	for (int y = 0; y < 3; y++)
		memcpy(model + ((2 + y) * 12 + 3) * 4, up + y * 5 * 4, 5 * 4);
	memcpy(model + (7 * 12 + 11) * 4, cs_src[CS_ONE].px, 4);
	free(up);
	for (int k = 0; k < 2; k++) {
		const char *what = k ? "CS3 the keys in SJIS" : "CS3 within a larger surface";
		uint8_t *got = cs_built(k ? sjis : inside, 12, 8, what);
		cs_px(got, 12, 2, 2, 10, 20, 30, 240, what);
		cs_px(got, 12, 8, 4, 10, 20, 30, 240, what);
		cs_px(got, 12, 4, 3, 77, 124, 141, 178, what);
		cs_px(got, 12, 7, 2, 10, 10, 10, 0, what);
		cs_px(got, 12, 11, 7, 30, 60, 90, 120, what);
		cs_compare(got, 12, 8, model, what);
		free(got);
	}
	free(model);

	/* 補間タイプ's low byte; a 元矩形 without a width takes all of the CG. */
	model = cs_scale_model(cs_src[CS_TINY].px, 3, 2, 5, 3);
	uint8_t *got = cs_built(i258, 5, 3, "CS3 補間タイプ 258");
	cs_compare(got, 5, 3, model, "CS3 補間タイプ 258");
	free(got);
	got = cs_built(nowidth, 5, 3, "CS3 元矩形 (1, 1, 0, 1)");
	cs_compare(got, 5, 3, model, "CS3 元矩形 (1, 1, 0, 1)");
	free(got);
	free(model);

	model = cs_scale_model(cs_src[CS_FRAME].px, 628, 94, 370, 70);
	got = cs_built(game, 370, 70, "CS3 the game's shape in SJIS");
	cs_compare(got, 370, 70, model, "CS3 the game's shape in SJIS");
	free(got);
	free(model);
	printf("CS3 checked\n");
}

/* --- CS4 --- */
#define CS_SCALE_I(name, w, h, i) { .cmd = 14, .dest = { 0, 0, 0, 0, w, h }, .interp = i, .cg = name }
static const struct cs_step cs_i0[] = { CS_CREATE(5, 3), CS_SCALE_I(CSN_TINY, 5, 3, 0) };
static const struct cs_step cs_i1[] = { CS_CREATE(5, 3), CS_SCALE_I(CSN_TINY, 5, 3, 1) };
static const struct cs_step cs_i3[] = { CS_CREATE(5, 3), CS_SCALE_I(CSN_TINY, 5, 3, 3) };
static const struct cs_step cs_part[] = { CS_CREATE(5, 3),
	{ .cmd = 14, .dest = { 0, 0, 0, 0, 5, 3 }, .src = { 0, 0, 2, 2 }, .interp = 2, .cg = CSN_TINY } };
static const struct cs_step cs_missing[] = { CS_CREATE(5, 3), CS_SCALE("cs/none", 0, 0, 5, 3) };
static const struct cs_step cs_rgb[] = { CS_CREATE(5, 3), CS_SCALE(CSN_RGB, 0, 0, 5, 3) };
static const struct cs_step cs_w0[] = { CS_CREATE(5, 3), CS_SCALE(CSN_TINY, 0, 0, 0, 3) };
static const struct cs_step cs_h0[] = { CS_CREATE(5, 3), CS_SCALE(CSN_TINY, 0, 0, 5, 0) };
static const struct cs_step cs_right[] = { CS_CREATE(5, 3), CS_SCALE(CSN_TINY, 1, 0, 5, 3) };
static const struct cs_step cs_below[] = { CS_CREATE(5, 3), CS_SCALE(CSN_TINY, 0, 1, 5, 3) };
static const struct cs_step cs_left[] = { CS_CREATE(5, 3), CS_SCALE(CSN_TINY, -1, 0, 3, 3) };
static const struct cs_step cs_first[] = { CS_SCALE(CSN_TINY, 0, 0, 5, 3) };
static const struct cs_step cs_after_cg[] = { { .cmd = 2, .cg = CSN_ONE }, CS_SCALE(CSN_TINY, 0, 0, 1, 1) };
static const struct cs_step cs_limit[] = { CS_CREATE(2048, 1), CS_SCALE(CSN_WIDE, 0, 0, 2048, 1) };
static const struct cs_step cs_over[] = { CS_CREATE(2049, 1), CS_SCALE(CSN_WIDE, 0, 0, 2049, 1) };

static void cs_unbuilt_case(void)
{
	cs_setup();
	static const struct { const char *name; int n; const struct cs_step *steps; int flags; int cg; } v[] = {
		{ "Interp0", CS_N(cs_i0), 0, CS_TINY }, { "Interp1", CS_N(cs_i1), 0, CS_TINY },
		{ "Interp3", CS_N(cs_i3), 0, CS_TINY }, { "Part", CS_N(cs_part), 0, CS_TINY },
		{ "Missing", CS_N(cs_missing), 0, -1 }, { "Rgb", CS_N(cs_rgb), 0, CS_RGB },
		{ "Width0", CS_N(cs_w0), 0, CS_TINY }, { "Height0", CS_N(cs_h0), 0, CS_TINY },
		{ "Right", CS_N(cs_right), 0, CS_TINY }, { "Below", CS_N(cs_below), 0, CS_TINY },
		{ "Left", CS_N(cs_left), 0, CS_TINY }, { "First", CS_N(cs_first), 0, CS_TINY },
		{ "AfterCg", CS_N(cs_after_cg), 0, CS_ONE }, { "NoInterp", CS_N(cs_up), CS_NO_INTERP, CS_TINY },
		{ "NoSrc", CS_N(cs_up), CS_NO_SRC, CS_TINY }, { "Over", CS_N(cs_over), 0, CS_WIDE },
	};
	const int n = sizeof(v) / sizeof(v[0]);
	struct ex_tree *kids;
	struct ex *e = lb_activity("CgScaleCopyUnbuilt", n + 1, &kids);
	for (int i = 0; i < n; i++)
		kids->children[i] = cs_parts(v[i].name, v[i].n, v[i].steps, v[i].flags);
	kids->children[n] = cs_parts("Limit", CS_N(cs_limit), 0);
	int act = cs_load("CgScaleCopyUnbuilt", e);
	int nos[32];
	for (int i = 0; i < n; i++)
		nos[i] = cs_no(act, v[i].name);
	int limit = cs_no(act, "Limit");
	heap_unref(act);
	for (int i = 0; i < n; i++) {
		char what[64];
		snprintf(what, sizeof what, "CS4 %s", v[i].name);
		if (nos[i])
			cs_legacy(nos[i], v[i].cg, what);
	}
	/* 2047 * 4096 * 256 = 2146435072 fits in 32 bits, 2048 * 4096 * 256 does
	 * not. */
	uint8_t *got = cs_built(limit, 2048, 1, "CS4 2048 columns from 4096");
	uint8_t *model = cs_scale_model(cs_src[CS_WIDE].px, 4096, 1, 2048, 1);
	cs_compare(got, 2048, 1, model, "CS4 2048 columns from 4096");
	free(got);
	free(model);
	printf("CS4 checked\n");
}

/* --- CS5 --- */
static void cs_save_case(void)
{
	cs_setup();
	struct ex_tree *kids;
	struct ex *e = lb_activity("CgScaleCopySave", 1, &kids);
	kids->children[0] = cs_parts("Inside", CS_N(cs_inside), 0);
	int act = cs_load("CgScaleCopySave", e);
	int no = cs_no(act, "Inside");
	heap_unref(act);
	uint8_t *before = cs_built(no, 12, 8, "CS5 before the save");
	PE_UpdateComponent(0);
	struct page *saved = NULL;
	CS_CHECK(PE_Save(&saved) && saved, "CS5: PE_Save failed");
	if (!saved || !before)
		return;
	CS_CHECK(ca_save_version(saved) == 13, "CS5: the save is XPE v%d, want 13", ca_save_version(saved));
	const int words = saved->nr_vars;
	CS_CHECK(PE_Load(&saved), "CS5: PE_Load failed");
	PE_UpdateComponent(0);
	struct parts *p = parts_try_get(no);
	CS_CHECK(p && cn_nr_ops(p) == 4 && cs_last_op(no) == CS_OP_SCALE_COPY,
		 "CS5: after PE_Load %d operation(s), the last of type %d", p ? cn_nr_ops(p) : -1, cs_last_op(no));
	uint8_t *got = cs_built(no, 12, 8, "CS5 after PE_Load");
	cs_compare(got, 12, 8, before, "CS5 after PE_Load");
	free(got);
	struct page *again = NULL;
	CS_CHECK(PE_Save(&again) && again && again->nr_vars == words, "CS5: the save after PE_Load has %d words, the first %d",
		 again ? again->nr_vars : -1, words);
	ca_free_buffer(again);
	ca_free_buffer(saved);
	free(before);

	/* A save without the operation, as version 11 wrote it (the number
	 * alone differs). */
	const int PLAIN = 9661;
	cc_new(PLAIN, 8, 4, 10, 20, 30, 200);
	cc_dot(PLAIN, 2, 1, 250);
	uint8_t *plain = cc_pixels(PLAIN, 8, 4, "CS5 plain");
	cs_failures += cc_failures;
	cc_failures = 0;
	PE_UpdateComponent(0);
	saved = NULL;
	CS_CHECK(PE_Save(&saved) && saved, "CS5: PE_Save failed");
	int pos = saved ? cg_version_pos(saved) : -1;
	CS_CHECK(pos >= 0, "CS5: no version in the save");
	if (saved && plain && pos >= 0) {
		saved->values[pos].i = 11;
		CS_CHECK(ca_save_version(saved) == 11 && PE_Load(&saved), "CS5: a version 11 save did not load");
		PE_UpdateComponent(0);
		struct parts *pp = parts_try_get(PLAIN);
		uint8_t *g = pp && pp->states[0].common.texture.handle ? gfx_get_pixels(&pp->states[0].common.texture) : NULL;
		CS_CHECK(g && !memcmp(g, plain, 8 * 4 * 4) && cc_nr_ops(PLAIN) == 3,
			 "CS5: the version 11 save loads other pixels (%d operations)", cc_nr_ops(PLAIN));
		free(g);
	}
	ca_free_buffer(saved);
	free(plain);
	printf("CS5 checked\n");
}

/* --- CS6 --- */
static void cs_v13_case(void)
{
	cs_setup();
	const int version = ain->version;
	ain->version = 13;
	struct ex_tree *kids;
	struct ex *e = lb_activity("CgScaleCopyV13", 1, &kids);
	kids->children[0] = cs_parts("Game", CS_N(cs_game), 0);
	int act = cs_load("CgScaleCopyV13", e);
	int no = cs_no(act, "Game");
	heap_unref(act);
	if (no) {
		struct parts_state *s = &parts_try_get(no)->states[0];
		CS_CHECK(s->type != PARTS_CONSTRUCTION_PROCESS || cs_last_op(no) != CS_OP_SCALE_COPY,
			 "CS6: v13 built command 14 (state type %d)", s->type);
	}
	PE_UpdateComponent(0);
	struct page *saved = NULL;
	CS_CHECK(PE_Save(&saved) && saved && ca_save_version(saved) == 3, "CS6: a v13 save is XPE v%d, want 3",
		 saved ? ca_save_version(saved) : -1);
	ca_free_buffer(saved);
	ain->version = version;
	printf("CS6 checked\n");
}

/* --- CS7 --- */
static void cs_ain_case(void)
{
	cs_setup();
	const int NO = 9671;
	parts_get(NO);
	cc_hll(NO, &(struct cc_cmd){ .cmd = 0, .w = 5, .h = 3 });
	/* AddPartsConstructionProcess with ArrayInt [0] 14, [1] 2 (補間タイプ),
	 * [2..5] 元矩形 (0, 0, 3, 2), [6, 7] X, Y and [10, 11] W, H. */
	int v[40] = { 0 };
	v[0] = 14; v[1] = 2; v[4] = 3; v[5] = 2; v[10] = 5; v[11] = 3;
	int ints = cc_int_array(40, v);
	struct page *fa = alloc_page(ARRAY_PAGE, AIN_ARRAY_FLOAT, 2);
	int floats = heap_alloc_slot(VM_PAGE);
	heap_set_page(floats, fa);
	struct page *sa = alloc_page(ARRAY_PAGE, AIN_ARRAY_STRING, 2);
	sa->values[0].i = sf_str("", -1);
	sa->values[1].i = sf_str(CSN_TINY, -1);
	int strs = heap_alloc_slot(VM_PAGE);
	heap_set_page(strs, sa);
	int pos = cc_int_array(0, NULL);
	ri_hll("AddPartsConstructionProcess", 6, (union vm_value[]){
		RI_I(NO), RI_I(ints), RI_I(floats), RI_I(strs), RI_I(pos), RI_I(1) });
	CS_CHECK(cc_nr_ops(NO) == 2 && cs_last_op(NO) == PARTS_CP_COPY_CUT_CG,
		 "CS7: the AIN's 14 left %d operation(s), the last of type %d; want 2, the last %d", cc_nr_ops(NO),
		 cs_last_op(NO), PARTS_CP_COPY_CUT_CG);
	printf("CS7 checked\n");
}

static void (*cs_case)(void);
static void cs_case_body(void)
{
	cs_case();
	cs_failures += cn_failures + cc_failures + ca_failures;
}

static int test_cg_scale_copy(void)
{
	static const struct { const char *name; void (*fn)(void); } cases[] = {
		{ "CS1", cs_game_case }, { "CS2", cs_tiny_case }, { "CS3", cs_place_case }, { "CS4", cs_unbuilt_case },
		{ "CS5", cs_save_case }, { "CS6", cs_v13_case }, { "CS7", cs_ain_case },
	};
	const int n = sizeof(cases) / sizeof(cases[0]);
	int failed = 0, env = 0;
	for (int i = 0; i < n; i++) {
		cs_case = cases[i].fn;
		int r = cn_fork_case(cases[i].name, cs_case_body, &cs_failures);
		failed += r == 1;
		env += r == 2;
	}
	if (env) {
		printf("CG-SCALE-COPY ENVIRONMENT FAILURE: %d of %d cases could not run, %d failed\n", env, n, failed);
		return 2;
	}
	printf("CG-SCALE-COPY %s: %d of %d cases failed\n", failed ? "FAIL" : "PASS", failed, n);
	return failed ? 1 : 0;
}
