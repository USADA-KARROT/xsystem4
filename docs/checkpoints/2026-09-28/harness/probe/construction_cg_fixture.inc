/* construction-cg: the construction states whose first step is command 2,
 * as the original builds them (static disassembly only):
 *   - 2 ＣＧサーフェイス (0x4fbf40 -> 0x50ac80): the surface it had is freed
 *     (0x4fbf72) and the CG named by ＣＧ名 becomes the surface, with its
 *     size and its channels; a CG that cannot be loaded fails the command
 *     and with it every later step (0x4fb675);
 *   - 4 ブレンド塗りつぶし (0x4fc190 -> 0x5ac510 -> 0x4922a0): over 先矩形
 *     X,Y,W,H (全體 = the surface), each colour becomes T[255 - A][d] +
 *     T[A][c], T[a][x] = (2 * a * x + 1) / 510 (the table 0x491660 fills);
 *     the colour is clamped to 0..255, the alpha is not written;
 *   - 15 グレースケールフィルタ (0x4fe870 -> 0x5ad7b0): R = G = B =
 *     (306 * R + 601 * G + 117 * B) >> 10 (0x5ad8f0), alpha not written;
 *   - 27 / 28, the blurs (construction-cmds has their rows by hand).
 * The game's states are (2, 28, 27) (SceneAzito's four backgrounds,
 * SceneWork, SceneHitokariSelect, SceneOmake), (2, 27, 28) (SceneSendResult),
 * (2, 27, 28, 4) (the seven pages of the settings, darkened by (0, 0, 0,
 * 180)), (2, 15, 28, 27) (SceneGameOver) and (2, 3) (CustomerInfoView's
 * BasePresent). Before the fix the pactex loader built none of them and
 * showed the CG as it is; 4 and 15 had no operation on the surface's pixels
 * (the older blending fill rounds on the GPU, and the AIN's 15 adds
 * nothing), and a CG that failed to load ended the engine on an assertion.
 * The new operations make the save XPE version 10.
 *
 * The probe has no archives: build_probe.py appends cg_assets_shadow.inc to
 * a copy of asset_manager.c, and the CGs are PNG files made here.
 *
 * Cases (each in its own fork with the construction fixture's GL setup):
 *   G1 the surface of command 2 through the pactex loader: the CG's size,
 *       colours and alpha; (2, 3) keeps the alpha and replaces the colours;
 *       the same CG through the AIN declaration.
 *   G2 (2, 28, 27) and (2, 27, 28): a white dot's blur by hand (the last
 *       row keeps the dot, as 0x493e01 leaves it), a noise CG against the
 *       model in both orders (which differ), the keys spelled in SJIS.
 *   G3 command 4: the settings' (2, 27, 28, 4) by hand and against the
 *       model; a rectangle whose quotients are truncated ((106, 171, 157),
 *       where rounding gives 172); a rectangle given by a negative size; a
 *       CG with an alpha, which is kept.
 *   G4 command 15 by hand on five colours, the alpha kept; SceneGameOver's
 *       (2, 15, 28, 27) against the model.
 *   G5 what stays unbuilt (the legacy CG is shown, as before): a CG that
 *       does not exist, one that cannot be decoded, one wider than the
 *       limit, command 2 as a later step, a blur without ブラー, a blur
 *       outside the surface, an alpha of 256, a line too short to blur; and
 *       what is built at the limit (a line of 2 with radius 1, of 3 with
 *       radius 0). Through the AIN declaration a CG that cannot be decoded
 *       leaves no surface, the later operations are not run and nothing
 *       crashes.
 *   G6 PE_Save / PE_Load: a state with every command above is version 10
 *       and comes back with the same pixels; a save without the new
 *       operations rewritten as versions 9, 8 and 7 loads the same.
 *   G7 engines before v14: the loader builds no state that starts with a
 *       CG (the legacy CG is shown), still builds (0, 6), and the save is
 *       version 3.
 *   G8 the new operations with numbers at the ends of int: nothing outside
 *       the surface is touched, a rectangle from far outside is the clipped
 *       one, an alpha outside 0..255 blends nothing; saved and loaded.
 * Functions the fix adds are reached by dlsym, so the file builds before
 * the fix.
 */
#include <dlfcn.h>
#include <zlib.h>

static int cg_failures;
#define CG_CHECK(c, ...) do { if (!(c)) { printf("  FAIL: "); printf(__VA_ARGS__); printf("\n"); cg_failures++; } } while (0)

#define CG_BLUR_KEY "\xa5\xd6\xa5\xe9\xa9\x60"		/* ブラー */
#define CG_BLUR_KEY_S "\x83\x75\x83\x89\x81\x5b"

/* --- CGs made in memory --- */
struct probe_cg_file {
	const char *name;
	const uint8_t *data;
	size_t size;
};
void probe_install_cg_assets(const struct probe_cg_file *files, int n);

#define CGN_BG "g16/bg"
#define CGN_MARK "g16/mark"
#define CGN_DOT "g16/dot"
#define CGN_FLAT "g16/flat"
#define CGN_COLORS "g16/colors"
#define CGN_BROKEN "g16/broken"
#define CGN_WIDE "g16/wide"
#define CGN_ONE "g16/one"
#define CGN_TWO "g16/two"
#define CGN_THREE "g16/three"
enum { CG_BG, CG_MARK, CG_DOT, CG_FLAT, CG_COLORS, CG_BROKEN, CG_WIDE, CG_ONE, CG_TWO, CG_THREE, CG_NR };
static struct {
	const char *name;
	int w, h;
	bool alpha;
	uint8_t *px;	/* RGBA */
} cg_src[CG_NR] = {
	[CG_BG] = { CGN_BG, 16, 12 },
	[CG_MARK] = { CGN_MARK, 6, 5, true },
	[CG_DOT] = { CGN_DOT, 8, 4 },
	[CG_FLAT] = { CGN_FLAT, 4, 2 },
	[CG_COLORS] = { CGN_COLORS, 5, 1, true },
	[CG_BROKEN] = { CGN_BROKEN, 0, 0 },
	[CG_WIDE] = { CGN_WIDE, 8193, 1 },
	[CG_ONE] = { CGN_ONE, 1, 1 },
	[CG_TWO] = { CGN_TWO, 2, 2 },
	[CG_THREE] = { CGN_THREE, 3, 3 },
};
static struct probe_cg_file cg_files[CG_NR];

static void cg_put32(uint8_t *p, uint32_t v)
{
	p[0] = v >> 24; p[1] = v >> 16; p[2] = v >> 8; p[3] = v;
}
static uint8_t *cg_chunk(uint8_t *o, const char *type, const uint8_t *data, size_t n)
{
	cg_put32(o, n);
	memcpy(o + 4, type, 4);
	if (n)
		memcpy(o + 8, data, n);
	cg_put32(o + 8 + n, crc32(crc32(0, NULL, 0), o + 4, n + 4));
	return o + 12 + n;
}
/* An 8-bit RGB or RGBA PNG of the pixels. */
static uint8_t *cg_png(int w, int h, const uint8_t *rgba, bool alpha, size_t *size)
{
	const int bpp = alpha ? 4 : 3;
	size_t raw_n = (size_t)h * (1 + (size_t)w * bpp);
	uint8_t *raw = malloc(raw_n), *q = raw;
	for (int y = 0; y < h; y++) {
		*q++ = 0;
		for (int x = 0; x < w; x++, q += bpp)
			memcpy(q, rgba + ((size_t)y * w + x) * 4, bpp);
	}
	uLongf zn = compressBound(raw_n);
	uint8_t *z = malloc(zn);
	compress(z, &zn, raw, raw_n);
	uint8_t *out = malloc(8 + 25 + 12 + zn + 12), *o = out;
	memcpy(o, "\x89PNG\r\n\x1a\n", 8);
	o += 8;
	uint8_t ihdr[13] = { 0 };
	cg_put32(ihdr, w);
	cg_put32(ihdr + 4, h);
	ihdr[8] = 8;
	ihdr[9] = alpha ? 6 : 2;
	o = cg_chunk(o, "IHDR", ihdr, 13);
	o = cg_chunk(o, "IDAT", z, zn);
	o = cg_chunk(o, "IEND", NULL, 0);
	free(raw);
	free(z);
	*size = o - out;
	return out;
}

static void cg_install(void)
{
	unsigned seed = 20261010;
	for (int i = 0; i < CG_NR; i++) {
		const int w = cg_src[i].w, h = cg_src[i].h;
		cg_files[i].name = cg_src[i].name;
		if (i == CG_BROKEN) {
			cg_files[i].data = (const uint8_t *)"this is not a CG";
			cg_files[i].size = 16;
			continue;
		}
		uint8_t *px = cg_src[i].px = calloc((size_t)w * h, 4);
		for (int k = 0; k < w * h; k++) {
			uint8_t *p = px + k * 4;
			const int x = k % w, y = k / w;
			p[3] = 255;
			switch (i) {
			case CG_MARK:
				p[0] = x * 40; p[1] = y * 50; p[2] = 200; p[3] = (x + y * 6) * 8;
				break;
			case CG_DOT:
				p[0] = p[1] = p[2] = (x == 3 && y == 1) ? 255 : 0;
				break;
			case CG_FLAT:
				p[0] = 10; p[1] = 200; p[2] = 255;
				break;
			case CG_COLORS: {
				static const uint8_t c[5][4] = { { 255, 0, 0, 255 }, { 0, 255, 0, 255 }, { 0, 0, 255, 255 },
					{ 255, 255, 255, 255 }, { 10, 200, 100, 77 } };
				memcpy(p, c[x], 4);
				break;
			}
			default:
				for (int c = 0; c < 3; c++) {
					seed = seed * 1103515245u + 12345u;
					p[c] = seed >> 16;
				}
			}
		}
		cg_files[i].data = cg_png(w, h, px, cg_src[i].alpha, &cg_files[i].size);
	}
	probe_install_cg_assets(cg_files, CG_NR);
}

/* --- pactex steps --- */
struct cg_step {
	int cmd;
	const char *cg;		/* ＣＧ名 */
	int full;		/* 全體 */
	int blur;		/* ブラー */
	int dest[6];		/* 先矩形 */
	int color[4];		/* 色１ */
	int flags;
};
enum { CG_NO_BLUR_KEY = 1, CG_SJIS_KEYS = 2 };
#define CG_NAMED(name) { 2, name }
#define CG_BLUR(cmd, r) { cmd, NULL, 1, r }
#define CG_BLUR_S(cmd, r) { cmd, NULL, 1, r, { 0 }, { 0 }, CG_SJIS_KEYS }
#define CG_BLUR_AT(cmd, r, x, y, w, h) { cmd, NULL, 0, r, { x, y, 0, 0, w, h } }
#define CG_BLEND(r, g, b, a) { 4, NULL, 1, 0, { 0 }, { r, g, b, a } }
#define CG_BLEND_AT(x, y, w, h, r, g, b, a) { 4, NULL, 0, 0, { x, y, 0, 0, w, h }, { r, g, b, a } }
#define CG_GRAY { 15, NULL, 1 }
#define CG_FILL(r, g, b) { 3, NULL, 1, 0, { 0 }, { r, g, b, 0 } }
#define CG_CREATE(w, h) { 0, NULL, 0, 0, { 0, 0, 0, 0, w, h } }
#define CG_N(steps) (int)(sizeof(steps) / sizeof((steps)[0])), steps

static struct ex_tree cg_parts(const char *name, int n, const struct cg_step *steps)
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
		const struct cg_step *s = &steps[i];
		const bool sjis = s->flags & CG_SJIS_KEYS;
		char key[32];
		snprintf(key, sizeof key, "Step%d", i + 1);
		unsigned k = 0;
		struct ex_tree t = bu_branch(key, 10);
		t.children[k++] = bu_ileaf(BR_COMMAND, s->cmd);
		t.children[k++] = lb_ilist(LB_DEST, 6, s->dest);
		t.children[k++] = lb_ilist(CN_COLOR1, 4, s->color);
		t.children[k++] = lb_ilist(CC_COLOR2, 4, (int[]){ 0, 0, 0, 0 });
		t.children[k++] = bu_ileaf(sjis ? CN_FULL_S : CN_FULL, s->full);
		if (!(s->flags & CG_NO_BLUR_KEY))
			t.children[k++] = bu_ileaf(sjis ? CG_BLUR_KEY_S : CG_BLUR_KEY, s->blur);
		t.children[k++] = lb_ilist(CN_RADIUS, 2, (int[]){ 0, 0 });
		t.children[k++] = lb_ilist(CN_ARC, 2, (int[]){ 0, 0 });
		t.children[k++] = bu_ileaf(CN_ROTATE, 0);
		t.children[k++] = bu_sleaf(BR_CG_NAME, s->cg ? s->cg : "");
		t.nr_children = k;
		st->children[2].children[i] = t;
	}
	return lb_node(name, &pl, info, 0);
}

/* --- the models, from the disassembly --- */
static uint8_t *cg_model(int i)
{
	size_t n = (size_t)cg_src[i].w * cg_src[i].h * 4;
	uint8_t *m = malloc(n);
	memcpy(m, cg_src[i].px, n);
	return m;
}
/* 0x491660's table. */
static int cg_table(int a, int x) { return (2 * a * x + 1) / 510; }
static void cg_blend_model(uint8_t *px, int w, int x0, int y0, int bw, int bh, int r, int g, int b, int a)
{
	const int c[3] = { r, g, b };
	for (int y = y0; y < y0 + bh; y++) {
		for (int x = x0; x < x0 + bw; x++) {
			uint8_t *p = px + (y * w + x) * 4;
			for (int k = 0; k < 3; k++)
				p[k] = cg_table(255 - a, p[k]) + cg_table(a, c[k]);
		}
	}
}
static void cg_gray_model(uint8_t *px, int n)
{
	for (int i = 0; i < n; i++) {
		uint8_t *p = px + i * 4;
		unsigned v = (p[1] * 0x259u + p[0] * 0x132u + p[2] * 0x75u) >> 10;
		p[0] = p[1] = p[2] = v;
	}
}

static void cg_expect(int no, int w, int h, const uint8_t *model, const char *what)
{
	cc_compare(cn_pixels(no, w, h, what), w, h, model, what);
}
static void cg_expect_px(const uint8_t *got, int w, int x, int y, int r, int g, int b, int a, const char *what)
{
	if (!got)
		return;
	const uint8_t *p = got + (y * w + x) * 4;
	CG_CHECK(p[0] == r && p[1] == g && p[2] == b && p[3] == a, "%s (%d,%d) = (%d,%d,%d,%d), want (%d,%d,%d,%d)",
		 what, x, y, p[0], p[1], p[2], p[3], r, g, b, a);
}
/* True when the normal state is a construction that was built. */
static bool cg_built(int no)
{
	struct parts *p = no > 0 ? parts_try_get(no) : NULL;
	return p && p->states[0].type == PARTS_CONSTRUCTION_PROCESS && p->states[0].common.texture.handle
		&& cn_nr_ops(p) > 0;
}
/* The pixels of the normal state whatever its type (to be freed). */
static uint8_t *cg_state_pixels(int no, int w, int h, const char *what)
{
	struct parts *p = no > 0 ? parts_try_get(no) : NULL;
	bool ok = p && p->states[0].common.texture.handle && p->states[0].common.texture.w == w
		&& p->states[0].common.texture.h == h;
	CG_CHECK(ok, "%s: no %dx%d texture", what, w, h);
	return ok ? gfx_get_pixels(&p->states[0].common.texture) : NULL;
}

#define CG_CASE(name) \
	static void name##_body(void); \
	static void name(void) { cn_gl_setup(); ri_postlink(); cg_install(); name##_body(); cg_failures += cc_failures + cn_failures; } \
	static void name##_body(void)

/* --- G1 --- */
static const struct cg_step cg_plain[] = { CG_NAMED(CGN_MARK) };
static const struct cg_step cg_present[] = { CG_NAMED(CGN_MARK), CG_FILL(32, 95, 255) };
static const struct cg_step cg_opaque[] = { CG_NAMED(CGN_FLAT) };

CG_CASE(cg_surface)
{
	struct ex_tree *kids;
	struct ex *e = lb_activity("ConstructionCgSurface", 3, &kids);
	kids->children[0] = cg_parts("Plain", CG_N(cg_plain));
	kids->children[1] = cg_parts("Present", CG_N(cg_present));
	kids->children[2] = cg_parts("Opaque", CG_N(cg_opaque));
	int act = cn_load("ConstructionCgSurface", e);
	int plain = cn_no(act, "Plain"), present = cn_no(act, "Present"), opaque = cn_no(act, "Opaque");
	heap_unref(act);

	/* (2, 1) of the mark: (2 * 40, 1 * 50, 200), alpha (2 + 6) * 8. */
	uint8_t *got = cn_pixels(plain, 6, 5, "G1 the CG");
	cg_expect_px(got, 6, 2, 1, 80, 50, 200, 64, "G1 the CG");
	cg_expect_px(got, 6, 0, 0, 0, 0, 200, 0, "G1 the CG");
	cg_expect_px(got, 6, 5, 4, 200, 200, 200, 232, "G1 the CG");
	cc_compare(got, 6, 5, cg_src[CG_MARK].px, "G1 the CG");

	/* CustomerInfoView's BasePresent: the colours replaced, the alpha kept. */
	uint8_t *model = cg_model(CG_MARK);
	for (int i = 0; i < 30; i++) {
		model[i * 4] = 32; model[i * 4 + 1] = 95; model[i * 4 + 2] = 255;
	}
	got = cn_pixels(present, 6, 5, "G1 (2, 3)");
	cg_expect_px(got, 6, 2, 1, 32, 95, 255, 64, "G1 (2, 3)");
	cg_expect_px(got, 6, 0, 0, 32, 95, 255, 0, "G1 (2, 3)");
	cc_compare(got, 6, 5, model, "G1 (2, 3)");
	free(model);

	/* A CG without an alpha is opaque. */
	got = cn_pixels(opaque, 4, 2, "G1 a CG without alpha");
	cg_expect_px(got, 4, 3, 1, 10, 200, 255, 255, "G1 a CG without alpha");
	cc_compare(got, 4, 2, cg_src[CG_FLAT].px, "G1 a CG without alpha");

	/* The same through the AIN declaration. */
	const int AIN = 9501;
	parts_get(AIN);
	cc_hll(AIN, &(struct cc_cmd){ .cmd = 2, .cg = cg_src[CG_MARK].name });
	CG_CHECK(cc_nr_ops(AIN) == 1, "G1: %d operation(s) after the AIN's command 2, want 1", cc_nr_ops(AIN));
	cc_expect_all(AIN, 6, 5, cg_src[CG_MARK].px, "G1 the AIN's command 2");
	printf("G1 checked\n");
}

/* --- G2 --- */
static const struct cg_step cg_dot_vh[] = { CG_NAMED(CGN_DOT), CG_BLUR(28, 4), CG_BLUR(27, 5) };
static const struct cg_step cg_azito[] = { CG_NAMED(CGN_BG), CG_BLUR(28, 4), CG_BLUR(27, 5) };
static const struct cg_step cg_send[] = { CG_NAMED(CGN_BG), CG_BLUR(27, 10), CG_BLUR(28, 10) };
static const struct cg_step cg_send_sjis[] = { CG_NAMED(CGN_BG), CG_BLUR_S(27, 10), CG_BLUR_S(28, 10) };
static const struct cg_step cg_blur_rect[] = { CG_NAMED(CGN_BG), CG_BLUR_AT(27, 2, 3, 2, 9, 7) };

CG_CASE(cg_blurs)
{
	struct ex_tree *kids;
	struct ex *e = lb_activity("ConstructionCgBlur", 5, &kids);
	kids->children[0] = cg_parts("Dot", CG_N(cg_dot_vh));
	kids->children[1] = cg_parts("Azito", CG_N(cg_azito));
	kids->children[2] = cg_parts("Send", CG_N(cg_send));
	kids->children[3] = cg_parts("SendSjis", CG_N(cg_send_sjis));
	kids->children[4] = cg_parts("Rect", CG_N(cg_blur_rect));
	int act = cn_load("ConstructionCgBlur", e);
	int dot = cn_no(act, "Dot"), azito = cn_no(act, "Azito"), send = cn_no(act, "Send");
	int sjis = cn_no(act, "SendSjis"), rect = cn_no(act, "Rect");
	heap_unref(act);

	/* The white dot at (3, 1) of 8x4. Down the columns the radius 4 is
	 * clamped to 4 / 2 - 1 = 1: column 3 is 85 (255 / 3) in rows 0 to 2
	 * and also in row 3, whose sum drops p[2] where p[1] left the window.
	 * Along the rows the radius 5 is clamped to 3: 85 / 7 = 12 in columns
	 * 0 to 5 (the sum drops p[2 + k] for the last three, so column 5
	 * still holds the dot) and 0 in the last two. */
	uint8_t *got = cn_pixels(dot, 8, 4, "G2 the dot");
	for (int y = 0; y < 4; y++) {
		cg_expect_px(got, 8, 0, y, 12, 12, 12, 255, "G2 the dot");
		cg_expect_px(got, 8, 5, y, 12, 12, 12, 255, "G2 the dot");
		cg_expect_px(got, 8, 6, y, 0, 0, 0, 255, "G2 the dot");
		cg_expect_px(got, 8, 7, y, 0, 0, 0, 255, "G2 the dot");
	}
	uint8_t *model = cg_model(CG_DOT);
	cc_blur_model(model, 8, 4, 0, 0, 8, 4, 4, true);
	cc_blur_model(model, 8, 4, 0, 0, 8, 4, 5, false);
	cc_compare(got, 8, 4, model, "G2 the dot");
	free(model);

	/* SceneAzito's order and radii, SceneSendResult's. */
	uint8_t *vh = cg_model(CG_BG), *hv = cg_model(CG_BG);
	cc_blur_model(vh, 16, 12, 0, 0, 16, 12, 4, true);
	cc_blur_model(vh, 16, 12, 0, 0, 16, 12, 5, false);
	cc_blur_model(hv, 16, 12, 0, 0, 16, 12, 10, false);
	cc_blur_model(hv, 16, 12, 0, 0, 16, 12, 10, true);
	CG_CHECK(memcmp(vh, hv, 16 * 12 * 4) && memcmp(vh, cg_src[CG_BG].px, 16 * 12 * 4),
		 "G2: the two models do not tell the orders apart");
	cg_expect(azito, 16, 12, vh, "G2 (2, 28, 27)");
	cg_expect(send, 16, 12, hv, "G2 (2, 27, 28)");
	cg_expect(sjis, 16, 12, hv, "G2 (2, 27, 28), SJIS keys");
	free(vh);
	free(hv);

	/* A rectangle: the rest of the CG is as it was. */
	model = cg_model(CG_BG);
	cc_blur_model(model, 16, 12, 3, 2, 9, 7, 2, false);
	cg_expect(rect, 16, 12, model, "G2 a rectangle");
	free(model);
	printf("G2 checked\n");
}

/* --- G3 --- */
static const struct cg_step cg_config_dot[] = { CG_NAMED(CGN_DOT), CG_BLUR(27, 10), CG_BLUR(28, 10), CG_BLEND(0, 0, 0, 180) };
static const struct cg_step cg_config[] = { CG_NAMED(CGN_BG), CG_BLUR(27, 10), CG_BLUR(28, 10), CG_BLEND(0, 0, 0, 180) };
static const struct cg_step cg_trunc[] = { CG_NAMED(CGN_FLAT), CG_BLEND_AT(1, 0, 2, 2, 255, 128, 7, 100) };
static const struct cg_step cg_negative[] = { CG_NAMED(CGN_FLAT), CG_BLEND_AT(3, 2, -2, -2, 255, 128, 7, 100) };
static const struct cg_step cg_keep_alpha[] = { CG_NAMED(CGN_MARK), CG_BLEND(9, 8, 7, 255) };
static const struct cg_step cg_mix[] = { CG_NAMED(CGN_BG), CG_BLEND(200, 100, 50, 77) };

CG_CASE(cg_blend)
{
	struct ex_tree *kids;
	struct ex *e = lb_activity("ConstructionCgBlend", 6, &kids);
	kids->children[0] = cg_parts("ConfigDot", CG_N(cg_config_dot));
	kids->children[1] = cg_parts("Config", CG_N(cg_config));
	kids->children[2] = cg_parts("Trunc", CG_N(cg_trunc));
	kids->children[3] = cg_parts("Negative", CG_N(cg_negative));
	kids->children[4] = cg_parts("KeepAlpha", CG_N(cg_keep_alpha));
	kids->children[5] = cg_parts("Mix", CG_N(cg_mix));
	int act = cn_load("ConstructionCgBlend", e);
	int cdot = cn_no(act, "ConfigDot"), config = cn_no(act, "Config"), trunc = cn_no(act, "Trunc");
	int negative = cn_no(act, "Negative"), keep = cn_no(act, "KeepAlpha"), mix = cn_no(act, "Mix");
	heap_unref(act);

	/* The dot again, rows first: row 1 is 255 / 7 = 36 in columns 0 to 5,
	 * then every column is 36 / 3 = 12 in all four rows. (0, 0, 0, 180)
	 * leaves 75 * 12 / 255 = 3. */
	uint8_t *got = cn_pixels(cdot, 8, 4, "G3 the settings' dot");
	cg_expect_px(got, 8, 0, 0, 3, 3, 3, 255, "G3 the settings' dot");
	cg_expect_px(got, 8, 5, 3, 3, 3, 3, 255, "G3 the settings' dot");
	cg_expect_px(got, 8, 6, 1, 0, 0, 0, 255, "G3 the settings' dot");
	free(got);

	uint8_t *model = cg_model(CG_BG);
	cc_blur_model(model, 16, 12, 0, 0, 16, 12, 10, false);
	cc_blur_model(model, 16, 12, 0, 0, 16, 12, 10, true);
	cg_blend_model(model, 16, 0, 0, 16, 12, 0, 0, 0, 180);
	cg_expect(config, 16, 12, model, "G3 (2, 27, 28, 4)");
	free(model);

	/* (10, 200, 255) under (255, 128, 7) at 100: 155 * 10 / 255 = 6 and
	 * 100 * 255 / 255 = 100; 155 * 200 / 255 = 121 (121.57) and 100 * 128
	 * / 255 = 50 (50.2); 155 * 255 / 255 = 155 and 100 * 7 / 255 = 2. */
	got = cn_pixels(trunc, 4, 2, "G3 truncation");
	cg_expect_px(got, 4, 1, 0, 106, 171, 157, 255, "G3 truncation");
	cg_expect_px(got, 4, 2, 1, 106, 171, 157, 255, "G3 truncation");
	cg_expect_px(got, 4, 0, 0, 10, 200, 255, 255, "G3 truncation");
	cg_expect_px(got, 4, 3, 1, 10, 200, 255, 255, "G3 truncation");
	free(got);
	/* (3, 2) with a size of (-2, -2) is the same rectangle. */
	got = cn_pixels(negative, 4, 2, "G3 a negative size");
	cg_expect_px(got, 4, 1, 0, 106, 171, 157, 255, "G3 a negative size");
	cg_expect_px(got, 4, 2, 1, 106, 171, 157, 255, "G3 a negative size");
	cg_expect_px(got, 4, 0, 1, 10, 200, 255, 255, "G3 a negative size");
	cg_expect_px(got, 4, 3, 0, 10, 200, 255, 255, "G3 a negative size");
	free(got);

	/* Alpha 255 replaces the colours; the CG's alpha stays. */
	got = cn_pixels(keep, 6, 5, "G3 a CG with alpha");
	cg_expect_px(got, 6, 2, 1, 9, 8, 7, 64, "G3 a CG with alpha");
	cg_expect_px(got, 6, 0, 0, 9, 8, 7, 0, "G3 a CG with alpha");
	free(got);

	model = cg_model(CG_BG);
	cg_blend_model(model, 16, 0, 0, 16, 12, 200, 100, 50, 77);
	cg_expect(mix, 16, 12, model, "G3 a colour at 77");
	free(model);
	printf("G3 checked\n");
}

/* --- G4 --- */
static const struct cg_step cg_gray[] = { CG_NAMED(CGN_COLORS), CG_GRAY };
static const struct cg_step cg_gameover[] = { CG_NAMED(CGN_BG), CG_GRAY, CG_BLUR(28, 5), CG_BLUR(27, 5) };

CG_CASE(cg_grays)
{
	struct ex_tree *kids;
	struct ex *e = lb_activity("ConstructionCgGray", 2, &kids);
	kids->children[0] = cg_parts("Gray", CG_N(cg_gray));
	kids->children[1] = cg_parts("GameOver", CG_N(cg_gameover));
	int act = cn_load("ConstructionCgGray", e);
	int gray = cn_no(act, "Gray"), over = cn_no(act, "GameOver");
	heap_unref(act);

	/* 306 * 255 >> 10 = 76, 601 * 255 >> 10 = 149, 117 * 255 >> 10 = 29,
	 * 1024 * 255 >> 10 = 255, (3060 + 120200 + 11700) >> 10 = 131. */
	uint8_t *got = cn_pixels(gray, 5, 1, "G4 gray");
	cg_expect_px(got, 5, 0, 0, 76, 76, 76, 255, "G4 red");
	cg_expect_px(got, 5, 1, 0, 149, 149, 149, 255, "G4 green");
	cg_expect_px(got, 5, 2, 0, 29, 29, 29, 255, "G4 blue");
	cg_expect_px(got, 5, 3, 0, 255, 255, 255, 255, "G4 white");
	cg_expect_px(got, 5, 4, 0, 131, 131, 131, 77, "G4 (10, 200, 100, 77)");
	free(got);

	uint8_t *model = cg_model(CG_BG);
	cg_gray_model(model, 16 * 12);
	cc_blur_model(model, 16, 12, 0, 0, 16, 12, 5, true);
	cc_blur_model(model, 16, 12, 0, 0, 16, 12, 5, false);
	cg_expect(over, 16, 12, model, "G4 (2, 15, 28, 27)");
	free(model);
	printf("G4 checked\n");
}

/* --- G5 --- */
static const struct cg_step cg_none[] = { CG_NAMED("g16/none"), CG_BLUR(28, 4), CG_BLUR(27, 5) };
static const struct cg_step cg_broken[] = { CG_NAMED(CGN_BROKEN), CG_BLUR(28, 4), CG_BLUR(27, 5) };
static const struct cg_step cg_wide[] = { CG_NAMED(CGN_WIDE), CG_BLUR(27, 5) };
static const struct cg_step cg_second[] = { CG_CREATE(8, 4), CG_NAMED(CGN_MARK) };
static const struct cg_step cg_no_key[] = { CG_NAMED(CGN_BG), { 27, NULL, 1, 5, { 0 }, { 0 }, CG_NO_BLUR_KEY } };
static const struct cg_step cg_outside[] = { CG_NAMED(CGN_BG), CG_BLUR_AT(27, 2, 40, 0, 4, 4) };
static const struct cg_step cg_alpha256[] = { CG_NAMED(CGN_BG), CG_BLEND(0, 0, 0, 256) };
static const struct cg_step cg_one[] = { CG_NAMED(CGN_ONE), CG_BLUR(27, 1) };
static const struct cg_step cg_two_zero[] = { CG_NAMED(CGN_TWO), CG_BLUR(27, 0) };
static const struct cg_step cg_two_one[] = { CG_NAMED(CGN_TWO), CG_BLUR(27, 1) };
static const struct cg_step cg_three[] = { CG_NAMED(CGN_THREE), CG_BLUR(28, 0) };

CG_CASE(cg_unbuilt)
{
	static const struct { const char *name; int n; const struct cg_step *steps; } states[] = {
		{ "NoCG", CG_N(cg_none) }, { "Broken", CG_N(cg_broken) }, { "Wide", CG_N(cg_wide) },
		{ "Second", CG_N(cg_second) }, { "NoBlurKey", CG_N(cg_no_key) }, { "Outside", CG_N(cg_outside) },
		{ "Alpha256", CG_N(cg_alpha256) }, { "OnePixel", CG_N(cg_one) }, { "TwoZero", CG_N(cg_two_zero) },
		{ "TwoOne", CG_N(cg_two_one) }, { "Three", CG_N(cg_three) },
	};
	enum { NR = sizeof states / sizeof states[0], NR_UNBUILT = 9 };
	struct ex_tree *kids;
	struct ex *e = lb_activity("ConstructionCgUnbuilt", NR, &kids);
	for (int i = 0; i < NR; i++)
		kids->children[i] = cg_parts(states[i].name, states[i].n, states[i].steps);
	int act = cn_load("ConstructionCgUnbuilt", e);
	int no[NR];
	for (int i = 0; i < NR; i++)
		no[i] = cn_no(act, states[i].name);
	heap_unref(act);
	for (int i = 0; i < NR_UNBUILT; i++) {
		CG_CHECK(!cg_built(no[i]), "G5: %s was built", states[i].name);
		int type = BU_PE("GetComponentType", BU_I(no[i]), BU_I(1)).i;
		CG_CHECK(type == 26, "G5: %s: GetComponentType %d, want 26", states[i].name, type);
	}
	/* The legacy CG of a state that is not built: the CG as it is. */
	uint8_t *got = cg_state_pixels(no[4], 16, 12, "G5 the legacy CG");
	cc_compare(got, 16, 12, cg_src[CG_BG].px, "G5 the legacy CG");

	/* A line of 2 with radius 1 is clamped to 0 and stays as it is; a line
	 * of 3 with radius 0 is blurred with 1. */
	cg_expect(no[9], 2, 2, cg_src[CG_TWO].px, "G5 a line of 2");
	uint8_t *model = cg_model(CG_THREE);
	cc_blur_model(model, 3, 3, 0, 0, 3, 3, 0, true);
	CG_CHECK(memcmp(model, cg_src[CG_THREE].px, 36), "G5: the model of a line of 3 is not blurred");
	cg_expect(no[10], 3, 3, model, "G5 a line of 3");
	free(model);

	/* Through the AIN declaration: a name that is no CG adds nothing; a
	 * CG that cannot be decoded is added, and its build frees the surface
	 * and stops there. */
	const int MISSING = 9551, BAD = 9552;
	parts_get(MISSING);
	cc_hll(MISSING, &(struct cc_cmd){ .cmd = 2, .cg = "g16/none" });
	CG_CHECK(cc_nr_ops(MISSING) <= 0, "G5: %d operation(s) for a CG that does not exist, want none", cc_nr_ops(MISSING));
	cc_new(BAD, 8, 4, 1, 2, 3, 4);
	PE_BuildPartsConstructionProcess(BAD, 1);
	struct parts *p = parts_get(BAD);
	CG_CHECK(p->states[0].common.texture.handle && p->states[0].common.w == 8, "G5: no 8x4 surface before the broken CG");
	cc_hll(BAD, &(struct cc_cmd){ .cmd = 2, .cg = cg_src[CG_BROKEN].name });
	cc_hll(BAD, &(struct cc_cmd){ .cmd = 3, .full = 1, .c1 = { 9, 9, 9, 0 } });
	CG_CHECK(cc_nr_ops(BAD) == 4, "G5: %d operations with the broken CG, want 4", cc_nr_ops(BAD));
	PE_BuildPartsConstructionProcess(BAD, 1);
	CG_CHECK(!p->states[0].common.texture.handle && p->states[0].common.w == 0 && p->states[0].common.h == 0,
		 "G5: after the broken CG the state has %s texture and is %dx%d; want none, 0x0",
		 p->states[0].common.texture.handle ? "a" : "no", p->states[0].common.w, p->states[0].common.h);
	printf("G5 checked\n");
}

/* --- G6 --- */
static const struct cg_step cg_every[] = { CG_NAMED(CGN_BG), CG_GRAY, CG_BLUR(28, 2), CG_BLUR_AT(27, 3, 1, 1, 12, 9),
	CG_BLEND_AT(2, 2, 9, 6, 200, 30, 90, 140), CG_BLEND(0, 0, 0, 60) };

static int cg_version_pos(struct page *saved)
{
	struct iarray_reader r;
	return iarray_init_reader(&r, saved, "XPE") ? (int)r.pos : -1;
}

CG_CASE(cg_save)
{
	/* A save without the new operations, as the older versions wrote it:
	 * 7 has neither the pixel operations nor the list of sub colour parts
	 * at the end; 8 and 9 differ from 10 by the number alone. */
	const int PLAIN = 9561, PIXEL = 9562;
	cc_new(PLAIN, 8, 4, 10, 20, 30, 200);
	cc_dot(PLAIN, 2, 1, 250);
	uint8_t *plain = cc_pixels(PLAIN, 8, 4, "G6 plain");
	PE_UpdateComponent(0);
	struct page *saved = NULL;
	CG_CHECK(PE_Save(&saved), "G6: PE_Save failed");
	if (!saved || !plain)
		return;
	int pos = cg_version_pos(saved);
	CG_CHECK(pos >= 0 && ca_save_version(saved) == 10, "G6: XPE v%d, want 10", ca_save_version(saved));
	CG_CHECK(saved->values[saved->nr_vars - 1].i == 0, "G6: the save does not end with an empty list of sub colour parts");
	struct page *v7 = copy_page(saved);
	v7->values[pos].i = 7;
	v7->nr_vars--;
	CG_CHECK(PE_Load(&v7), "G6: a version 7 save did not load");
	PE_UpdateComponent(0);
	uint8_t *got = cg_state_pixels(PLAIN, 8, 4, "G6 version 7");
	CG_CHECK(got && !memcmp(got, plain, 8 * 4 * 4) && cc_nr_ops(PLAIN) == 3, "G6: a version 7 save loads other pixels (%d operations)",
		 cc_nr_ops(PLAIN));
	free(got);
	v7->nr_vars++;
	ca_free_buffer(v7);
	ca_free_buffer(saved);

	cc_new(PIXEL, 16, 16, 255, 255, 255, 255);
	cc_hll(PIXEL, &(struct cc_cmd){ .cmd = 106, .x = 6, .y = 7, .radius = 4, .c1 = { 255, 0, 0, 255 } });
	cc_hll(PIXEL, &(struct cc_cmd){ .cmd = 27, .full = 1, .blur = 2 });
	uint8_t *pixel = cc_pixels(PIXEL, 16, 16, "G6 pixel operations");
	PE_UpdateComponent(0);
	saved = NULL;
	CG_CHECK(PE_Save(&saved), "G6: PE_Save failed");
	if (!saved || !pixel)
		return;
	for (int version = 9; version >= 8; version--) {
		struct page *old = copy_page(saved);
		old->values[pos].i = version;
		CG_CHECK(ca_save_version(old) == version && PE_Load(&old), "G6: a version %d save did not load", version);
		PE_UpdateComponent(0);
		uint8_t *a = cg_state_pixels(PLAIN, 8, 4, "G6 an older version"), *b = cg_state_pixels(PIXEL, 16, 16, "G6 an older version");
		CG_CHECK(a && b && !memcmp(a, plain, 8 * 4 * 4) && !memcmp(b, pixel, 16 * 16 * 4) && cc_nr_ops(PIXEL) == 4,
			 "G6: a version %d save loads other pixels (%d operations)", version, cc_nr_ops(PIXEL));
		free(a);
		free(b);
		ca_free_buffer(old);
	}
	ca_free_buffer(saved);
	free(plain);
	free(pixel);

	/* Every command of this fixture in one state. */
	struct ex_tree *kids;
	struct ex *e = lb_activity("ConstructionCgSave", 1, &kids);
	kids->children[0] = cg_parts("Every", CG_N(cg_every));
	int act = cn_load("ConstructionCgSave", e);
	int every = cn_no(act, "Every");
	heap_unref(act);
	uint8_t *model = cg_model(CG_BG);
	cg_gray_model(model, 16 * 12);
	cc_blur_model(model, 16, 12, 0, 0, 16, 12, 2, true);
	cc_blur_model(model, 16, 12, 1, 1, 12, 9, 3, false);
	cg_blend_model(model, 16, 2, 2, 9, 6, 200, 30, 90, 140);
	cg_blend_model(model, 16, 0, 0, 16, 12, 0, 0, 0, 60);
	cg_expect(every, 16, 12, model, "G6 before the save");
	PE_UpdateComponent(0);
	saved = NULL;
	CG_CHECK(PE_Save(&saved), "G6: PE_Save failed");
	if (saved) {
		CG_CHECK(ca_save_version(saved) == 10, "G6: a save with the new operations is XPE v%d, want 10", ca_save_version(saved));
		int words = saved->nr_vars;
		CG_CHECK(PE_Load(&saved), "G6: PE_Load failed");
		PE_UpdateComponent(0);
		struct parts *p = parts_try_get(every);
		CG_CHECK(p && cg_built(every) && cn_nr_ops(p) == 6, "G6: after PE_Load the state is not a built construction of 6 operations");
		got = cg_state_pixels(every, 16, 12, "G6 after PE_Load");
		cc_compare(got, 16, 12, model, "G6 after PE_Load");
		struct page *again = NULL;
		CG_CHECK(PE_Save(&again) && again && again->nr_vars == words, "G6: the save after PE_Load has %d words, the first %d",
			 again ? again->nr_vars : -1, words);
		ca_free_buffer(again);
		ca_free_buffer(saved);
	}
	free(model);
	printf("G6 checked\n");
}

/* --- G7 --- */
static const struct cc_step cg_old[] = { CC_CREATE(8, 4), CC_FILL_ALL(6, 9, 8, 7, 200) };

CG_CASE(cg_v13)
{
	int version = ain->version;
	ain->version = 13;
	struct ex_tree *kids;
	struct ex *e = lb_activity("ConstructionCgV13", 2, &kids);
	kids->children[0] = cg_parts("Bg", CG_N(cg_azito));
	kids->children[1] = cc_parts("Old", CC_N(cg_old));
	int act = cn_load("ConstructionCgV13", e);
	int bg = cn_no(act, "Bg"), old = cn_no(act, "Old");
	heap_unref(act);
	CG_CHECK(!cg_built(bg), "G7: v13 built the state that starts with a CG");
	uint8_t *got = cg_state_pixels(bg, 16, 12, "G7 v13: the legacy CG");
	cc_compare(got, 16, 12, cg_src[CG_BG].px, "G7 v13: the legacy CG");
	CG_CHECK(cg_built(old), "G7: v13 did not build (0, 6)");
	PE_UpdateComponent(0);
	struct page *saved = NULL;
	CG_CHECK(PE_Save(&saved) && saved && ca_save_version(saved) == 3, "G7: a v13 save is XPE v%d, want 3",
		 saved ? ca_save_version(saved) : -1);
	ca_free_buffer(saved);
	ain->version = version;

	/* The same activity on v14. */
	e = lb_activity("ConstructionCgV14", 1, &kids);
	kids->children[0] = cg_parts("Bg", CG_N(cg_azito));
	act = cn_load("ConstructionCgV14", e);
	bg = cn_no(act, "Bg");
	heap_unref(act);
	CG_CHECK(cg_built(bg), "G7: v14 did not build the state that starts with a CG");
	printf("G7 checked\n");
}

/* --- G8 --- */
typedef bool (*cg_blend_fn)(int, int, int, int, int, bool, int, int, int, int, int);
typedef bool (*cg_gray_fn)(int, int, int, int, int, bool, int);

CG_CASE(cg_extremes)
{
	cg_blend_fn blend = (cg_blend_fn)dlsym(RTLD_DEFAULT, "PE_AddBlendFillToPartsConstructionProcess");
	cg_gray_fn gray = (cg_gray_fn)dlsym(RTLD_DEFAULT, "PE_AddGrayScaleToPartsConstructionProcess");
	CG_CHECK(blend && gray, "G8: the operations of commands 4 and 15 do not exist");
	if (!blend || !gray)
		return;
	const int P_OUT2 = 9581, P_FAR = 9582, P_ALL = 9583;
	static const int outside[][4] = {
		{ -5, -5, 3, 3 }, { 20, 20, 5, 5 }, { 4, 4, 0, 6 }, { 4, 4, 6, 0 }, { INT_MIN, INT_MIN, 10, 10 },
		{ INT_MAX, 0, INT_MAX, 4 }, { 0, INT_MIN, 16, -1 }, { 16, 0, 1, 16 }, { 0, -1, 16, 1 },
	};
	cc_new(P_OUT2, 16, 16, 90, 40, 200, 123);
	cc_dot(P_OUT2, 5, 6, 250);
	uint8_t *before = cc_pixels(P_OUT2, 16, 16, "G8 before");
	for (unsigned i = 0; i < sizeof outside / sizeof outside[0]; i++) {
		blend(P_OUT2, outside[i][0], outside[i][1], outside[i][2], outside[i][3], false, 255, 255, 255, 255, 1);
		gray(P_OUT2, outside[i][0], outside[i][1], outside[i][2], outside[i][3], false, 1);
	}
	/* An alpha that is no table index blends nothing. */
	blend(P_OUT2, 0, 0, 16, 16, true, 255, 255, 255, 256, 1);
	blend(P_OUT2, 0, 0, 16, 16, true, 255, 255, 255, -1, 1);
	blend(P_OUT2, 2, 2, 4, 4, false, 255, 255, 255, 300, 1);
	CG_CHECK(cc_nr_ops(P_OUT2) == 3 + 2 * 9 + 3, "G8: %d operations, want 24", cc_nr_ops(P_OUT2));
	uint8_t *after = cc_pixels(P_OUT2, 16, 16, "G8 outside");
	if (before && after) {
		CG_CHECK(before[(6 * 16 + 5) * 4] == 250 && before[0] == 90, "G8: the surface before is not a dot on a colour");
		CG_CHECK(!memcmp(before, after, 16 * 16 * 4), "G8: operations outside the surface, or with such an alpha, changed it");
	}
	free(after);

	/* A rectangle from far outside is the clipped one; the colour is
	 * clamped to 0..255. */
	cc_new(P_FAR, 16, 16, 90, 40, 200, 123);
	cc_dot(P_FAR, 5, 6, 250);
	blend(P_FAR, -4, -4, INT_MAX, INT_MAX, false, 300, -20, 128, 100, 1);
	gray(P_FAR, 8, -1000, 1000000, 1008, false, 1);
	cc_new(P_ALL, 16, 16, 90, 40, 200, 123);
	cc_dot(P_ALL, 5, 6, 250);
	blend(P_ALL, 0, 0, 0, 0, true, 255, 0, 128, 100, 1);
	gray(P_ALL, 8, 0, 8, 8, false, 1);
	uint8_t *wide = cc_pixels(P_FAR, 16, 16, "G8 from far outside"), *all = cc_pixels(P_ALL, 16, 16, "G8 the clipped ones");
	if (wide && all && before) {
		uint8_t *model = malloc(16 * 16 * 4);
		memcpy(model, before, 16 * 16 * 4);
		cg_blend_model(model, 16, 0, 0, 16, 16, 255, 0, 128, 100);
		for (int y = 0; y < 8; y++)
			cg_gray_model(model + (y * 16 + 8) * 4, 8);
		/* 155 * 90 / 255 + 100 = 154, 155 * 40 / 255 = 24, 155 * 200 / 255 + 50 = 171 */
		cg_expect_px(wide, 16, 0, 15, 154, 24, 171, 123, "G8 from far outside");
		cc_compare(wide, 16, 16, model, "G8 from far outside");
		cc_compare(all, 16, 16, model, "G8 the clipped ones");
		wide = all = NULL;
		/* The fields of both operations in a save. */
		PE_UpdateComponent(0);
		struct page *saved = NULL;
		CG_CHECK(PE_Save(&saved) && saved && ca_save_version(saved) == 10, "G8: no XPE v10 save");
		if (saved) {
			CG_CHECK(PE_Load(&saved), "G8: PE_Load failed");
			PE_UpdateComponent(0);
			uint8_t *got = cg_state_pixels(P_FAR, 16, 16, "G8 after PE_Load");
			cc_compare(got, 16, 16, model, "G8 after PE_Load");
			got = cg_state_pixels(P_OUT2, 16, 16, "G8 after PE_Load");
			CG_CHECK(got && !memcmp(got, before, 16 * 16 * 4) && cc_nr_ops(P_OUT2) == 24, "G8: the operations outside the surface after PE_Load");
			free(got);
			ca_free_buffer(saved);
		}
		free(model);
	}
	free(wide);
	free(all);
	free(before);
	printf("G8 checked\n");
}

static int test_construction_cg(void)
{
	void (*cases[])(void) = { cg_surface, cg_blurs, cg_blend, cg_grays, cg_unbuilt, cg_save, cg_v13, cg_extremes };
	const char *names[] = { "G1", "G2", "G3", "G4", "G5", "G6", "G7", "G8" };
	int failed = 0, env = 0;
	for (int i = 0; i < 8; i++) {
		int r = cn_fork_case(names[i], cases[i], &cg_failures);
		failed += r == 1;
		env += r == 2;
	}
	if (env) {
		printf("CONSTRUCTION-CG ENVIRONMENT FAILURE: %d of 8 cases could not run, %d failed\n", env, failed);
		return 2;
	}
	printf("CONSTRUCTION-CG %s: %d of 8 cases failed\n", failed ? "FAIL" : "PASS", failed);
	return failed ? 1 : 0;
}
