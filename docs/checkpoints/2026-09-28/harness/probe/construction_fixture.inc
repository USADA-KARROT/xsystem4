/* construction: a v14 構築部件 state is built while its pactex loads, as the
 * original does (static disassembly only):
 *   - the construction state loader 0x5a64e0 reads 手順リスト (0x5a6569), each
 *     step with 0x4f82a0 (コマンド +4; 先矩形 X,Y,X2,Y2,W,H +0x1c..+0x30; 色１
 *     +0x34..+0x40; 全體 +0xc4; 半徑 +0xc8/+0xcc; 旋轉角度 +0xd8; 円弧角度
 *     +0xdc/+0xe0) and runs them at once (0x5a66ed -> 0x5a73c0 -> 0x4fa1d0; a
 *     failed step skips the rest, 0x4fb675);
 *   - command 0 (0x4fbd30) creates a W x H surface, opaque black;
 *   - 3 / 5 / 6 (0x4fc040 / 0x4fc2f0 / 0x4fc490) replace RGB / A / RGBA in
 *     先矩形 X,Y,W,H, or in the whole surface with 全體. The rectangle is
 *     normalized (a negative size extends the other way) and clipped to the
 *     surface (0x5abed0); an empty one fails the step (0x5abde0);
 *   - 122 (0x5093d0) replaces the alpha in a sector of the ellipse 半徑
 *     around 先矩形 X,Y. 0x4d0280 walks the 2r x 2r pixels around the centre
 *     (r the larger radius); 0x4eb0c0 keeps those whose top-left corner lies
 *     at an angle in [start, start + sweep] (atan2 in 1/65536 turns,
 *     truncated, both ends included, clockwise from +x); 0x4e6680 counts the
 *     n of 8x8 subsamples at offsets k/8 inside the ellipse; a pixel with
 *     n > 0 gets alpha a * (n * 255 / 64) / 255 (0x4d05c0, 0x5b0a10), the
 *     others keep theirs.
 * The 春銷 count labels' grey rounded base (128x40: a cross of alpha 255 and
 * four r 16 corners) and the timer gauge's base (262x22 black with a
 * (80,80,80,160) inside) are such states; xsystem4 did not build them.
 *
 * The expected pixels come from a model of these routines written from the
 * disassembly (not from xsystem4): whole surfaces as an FNV-1a hash of their
 * RGBA bytes, plus the pixels that show the rule.
 *
 *   CN1 the count label base: a construction state with a 128x40 texture;
 *       centre opaque white, corners transparent, the antialiased edge
 *       ((16,0) and (0,16) are 227, (11,0) 67 but (116,0) 75: the subsamples
 *       lean right and down), the alpha histogram; the same steps under the
 *       SJIS keys.
 *   CN2 other sectors: r 12 and r 9, an ellipse, a sector cut by the
 *       left/top edge, a scaled alpha, sweeps of 180/270/360/0 degrees, radii
 *       and angles that draw nothing; r 10 and r 13, where a subsample lies
 *       exactly on the circle (6-8-10, 5-12-13) and the original's arithmetic
 *       decides it: it multiplies by 1/r (0x4e6738) and rounds the two
 *       squares before adding them, which leaves that subsample out; a
 *       sector left of the surface, whose last column lands on x 0 because
 *       the pixel position is truncated towards zero (0x4d03ba), not floored.
 *   CN3 fills: the timer base, a fill reaching past the surface, negative
 *       sizes, RGB only, an alpha hole, and 全體 2, which is not the whole
 *       surface (only 1 is, 0x4f8f76); values a command does not write may
 *       be anything (command 3's A, command 5's RGB); a single command 0 is
 *       built as well.
 *   CN4 states that are not built keep the previous behaviour (no
 *       construction state, no texture, type 26, the first step's canvas):
 *       an unsupported command, a CG that does not exist, a rotated or
 *       oblique sector, angles beyond one turn, a step the original fails
 *       (an empty rectangle, a surface without width or height), a second
 *       command 0, written values outside 0..255, missing keys (コマンド,
 *       色１, 全體, 半徑, 円弧角度, 旋轉角度), a short or non-integer 先矩形,
 *       a non-integer 色１, a サーフェイスエリア that is not all integer 0;
 *       also a surface above 8192 and a radius above 1024, which are limits
 *       of xsystem4's loader, not of the original, and a rectangle whose
 *       X + W leaves 32 bits (the original wraps around, 0x5abef3; not
 *       reproduced). The variants made with a flag (a missing key, a short or
 *       non-integer list) differ in that one thing from Plain (CN3) or
 *       PieOnly (CN2), which are built; each other shape is stopped by one
 *       condition.
 *   CN5 an empty 手順リスト stays an empty construction state, which the
 *       game then fills through the construction API. It passes on the tree
 *       before the fix: there this GL setup already builds a surface. With
 *       the fix it also checks that the sector operation guards its own
 *       ranges (no alpha, a radius above the limit) and scales an alpha of
 *       999 by the coverage before clamping it (0x4d05c0, 0x5b0a10); it says
 *       so when the new function is not there.
 *   CN6 PE_Save / PE_Load rebuild the same pixels; an unbuilt state keeps
 *       its canvas; a save with a sector operation is version 7 or later; a
 *       version 6 page still loads. That page is made from the current
 *       writer's (no sector operation, no parts hidden in the editor: the
 *       number, and the empty list at the end removed), not written by a
 *       version 6 engine, so it would not notice a field added to both the
 *       writer and the reader.
 *   CN7 in a layout box a built state takes its real size (the same as the
 *       canvas an unbuilt one reports) and is under the cursor over its
 *       128x40.
 *   CN8 parts hidden in the editor (編輯上表示 0, themselves or through a
 *       parent) stay hidden after PE_Save / PE_Load, which does not run the
 *       pactex loader again: a built screen-sized mask (1480x920 at
 *       (-100,-100), a shape several activities of the game have) is not shown and does
 *       not take the click of the button under it.
 * CN4 and CN5 pass before the fix as well; they guard the fallback.
 *
 * Building needs FBOs and the fill shaders: each case runs in a fork with a
 * windowless 3.2 core CGL context, GLEW, the engine's gl_initialize (through
 * build_probe.py's probe_gl_initialize) and gfx_draw_init, in the source
 * root so that shaders/ is found. The setup ends with one fill of each blend
 * state read back (the driver once dropped gfx_fill's draws in a freshly
 * linked probe: "buildPipelineState failed"). A child that ends before its
 * setup reported ready, or whose self-check fails, is an environment failure,
 * not a failed case: it is run once more, and a second failure ends the mode
 * with exit code 2 and ENVIRONMENT FAILURE. Only functions that exist before
 * the fix are used, so before-check builds the old tree.
 */
#include <OpenGL/OpenGL.h>
extern void probe_gl_initialize(void);

static int cn_failures;
#define CN_CHECK(c, ...) do { if (!(c)) { printf("  FAIL: "); printf(__VA_ARGS__); printf("\n"); cn_failures++; } } while (0)

/* A case's child exits with 0 (passed), 1 (failed) or CN_ENV_EXIT (the GL
 * environment failed); the engine's ERROR, e.g. for a shader, exits with 1,
 * so the setup also tells the parent through cn_ready_fd that it completed. */
#define CN_ENV_EXIT 3
static int cn_ready_fd = -1;

static void cn_env_fail(const char *what)
{
	printf("CONSTRUCTION GL environment: %s\n", what);
	fflush(stdout);
	_exit(CN_ENV_EXIT);
}

#define CN_COLOR1 "\xc9\xab\xa3\xb1"					/* 色１ */
#define CN_FULL "\xc8\xab\xf3\x77"					/* 全體 */
#define CN_RADIUS "\xb0\xeb\x8f\xbd"					/* 半徑 */
#define CN_ARC "\x83\xd2\xbb\xa1\xbd\xc7\xb6\xc8"			/* 円弧角度 */
#define CN_ROTATE "\xd0\xfd\xde\x44\xbd\xc7\xb6\xc8"			/* 旋轉角度 */
#define CN_AREA "\xa5\xb5\xa9\x60\xa5\xd5\xa5\xa7\xa5\xa4\xa5\xb9\xa5\xa8\xa5\xea\xa5\xa2" /* サーフェイスエリア */
#define CN_STEPS_S "\x8e\xe8\x8f\x87\x83\x8a\x83\x58\x83\x67"		/* 手順リスト */
#define CN_COMMAND_S "\x83\x52\x83\x7d\x83\x93\x83\x68"			/* コマンド */
#define CN_DEST_S "\x90\xe6\x8b\xe9\x8c\x60"				/* 先矩形 */
#define CN_COLOR1_S "\x90\x46\x82\x50"					/* 色１ */
#define CN_FULL_S "\x91\x53\x91\xcc"					/* 全体 */
#define CN_RADIUS_S "\x94\xbc\x8c\x61"					/* 半径 */
#define CN_ARC_S "\x89\x7e\x8c\xca\x8a\x70\x93\x78"			/* 円弧角度 */
#define CN_ROTATE_S "\x89\xf1\x93\x5d\x8a\x70\x93\x78"			/* 回転角度 */

/* Windowless GL as the engine sets it up (gfx_init), and the text-metrics
 * font whose glyphs never load. */
static void cn_gl_setup(void)
{
	CGLPixelFormatAttribute attrs[] = {
		kCGLPFAOpenGLProfile, (CGLPixelFormatAttribute)kCGLOGLPVersion_3_2_Core,
		kCGLPFAColorSize, (CGLPixelFormatAttribute)24, kCGLPFAAlphaSize, (CGLPixelFormatAttribute)8, 0 };
	CGLPixelFormatObj pf = NULL;
	GLint npf = 0;
	CGLContextObj ctx = NULL;
	if (CGLChoosePixelFormat(attrs, &pf, &npf) || !pf || CGLCreateContext(pf, NULL, &ctx) || CGLSetCurrentContext(ctx))
		cn_env_fail("cannot create an offscreen context");
	glewExperimental = GL_TRUE;
	if (glewInit() != GLEW_OK)
		cn_env_fail("glewInit failed");
	const char *src = getenv("XS4_SRC");
	if (!src || chdir(src))
		cn_env_fail("XS4_SRC is needed (the shaders are read from the source root)");
	probe_gl_initialize();
	gfx_draw_init();
	/* One fill of each blend state the commands 6, 5 and 3 use, read back. */
	Texture check = { 0 };
	gfx_init_texture_rgba(&check, 4, 4, (SDL_Color){ 0, 0, 0, 255 });
	gfx_fill_with_alpha(&check, 0, 0, 2, 2, 10, 20, 30, 40);
	gfx_fill_amap(&check, 2, 0, 2, 2, 50);
	gfx_fill(&check, 0, 2, 2, 2, 60, 70, 80);
	uint8_t *got = gfx_get_pixels(&check);
	static const struct { int x, y; uint8_t c[4]; } want[] = {
		{ 0, 0, { 10, 20, 30, 40 } }, { 2, 0, { 0, 0, 0, 50 } }, { 0, 2, { 60, 70, 80, 255 } }, { 3, 3, { 0, 0, 0, 255 } } };
	for (int i = 0; i < 4; i++) {
		const uint8_t *g = got + (want[i].y * 4 + want[i].x) * 4;
		if (memcmp(g, want[i].c, 4)) {
			printf("CONSTRUCTION GL self-check: (%d,%d) = (%d,%d,%d,%d), want (%d,%d,%d,%d)\n", want[i].x, want[i].y,
			       g[0], g[1], g[2], g[3], want[i].c[0], want[i].c[1], want[i].c[2], want[i].c[3]);
			cn_env_fail("a fill was not drawn");
		}
	}
	free(got);
	gfx_delete_texture(&check);
	tm_noglyph = (struct font) {
		.charmap = CHARMAP_UNICODE,
		.get_size = tm_noglyph_get_size,
		.get_actual_size = tm_noglyph_actual_size,
		.get_actual_size_round_down = tm_noglyph_actual_size,
		.get_glyph = tm_noglyph_get_glyph,
		.size_char = tm_noglyph_size_char,
		.size_char_kerning = tm_noglyph_kerning,
	};
	font_ttf[0] = font_ttf[1] = &tm_noglyph;
	bu_postlink();
	if (cn_ready_fd >= 0 && write(cn_ready_fd, "r", 1) != 1)
		cn_env_fail("cannot report the setup to the parent");
}

struct cn_step {
	int cmd;
	int dest[6];	/* 先矩形: X, Y, X2, Y2, W, H */
	int color[4];	/* 色１ */
	int full;	/* 全體 */
	int radius[2];	/* 半徑 */
	int arc[2];	/* 円弧角度: start, sweep */
	int rotate;	/* 旋轉角度 */
	const char *cg;	/* ＣＧ名 */
};
#define CN_CREATE(w, h) { 0, { 0, 0, 0, 0, w, h } }
#define CN_FILL(cmd, x, y, w, h, r, g, b, a) { cmd, { x, y, 0, 0, w, h }, { r, g, b, a } }
#define CN_FILL_ALL(cmd, r, g, b, a) { cmd, { 0 }, { r, g, b, a }, 1 }
#define CN_PIE(x, y, rx, ry, start, sweep, a) { 122, { x, y }, { 0, 0, 0, a }, 0, { rx, ry }, { start, sweep } }
#define CN_N(steps) (int)(sizeof(steps) / sizeof((steps)[0])), steps

enum {
	CN_SJIS = 1 << 0,		/* SJIS step keys */
	CN_NO_COMMAND = 1 << 1,		/* the steps have no コマンド, */
	CN_NO_COLOR = 1 << 2,		/* no 色１, */
	CN_NO_FULL = 1 << 3,		/* no 全體, */
	CN_NO_RADIUS = 1 << 4,		/* no 半徑, */
	CN_NO_ARC = 1 << 5,		/* no 円弧角度, */
	CN_NO_ROTATE = 1 << 6,		/* no 旋轉角度 */
	CN_SHORT_DEST = 1 << 7,		/* 先矩形 has only X, Y, X2, Y2 */
	CN_FLOAT_DEST = 1 << 8,		/* 先矩形 holds floats */
	CN_FLOAT_COLOR = 1 << 9,	/* 色１'s first item is a float */
	CN_SURFACE_AREA = 1 << 10,	/* サーフェイスエリア = (1, 1, 5, 5) */
	CN_FLOAT_AREA = 1 << 11,	/* サーフェイスエリア = (0.0, 0, 0, 0) */
};

/* A 構築部件 normal state whose 手順リスト holds the steps. */
static struct ex_tree cn_parts(const char *name, const struct lb_place *pl, int n, const struct cn_step *steps, int flags)
{
	bool sjis = flags & CN_SJIS;
	struct ex_tree info = bu_branch(BU_INFO, 2);
	info.children[0] = bu_sleaf(BU_TYPE, BU_LOW);
	info.children[1] = bu_branch(BU_NORMAL, 3);
	struct ex_tree *st = &info.children[1];
	st->children[0] = bu_sleaf(BU_TYPE, BR_CONS);
	st->children[1] = lb_ilist(CN_AREA, 4, (flags & CN_SURFACE_AREA) ? (int[]){ 1, 1, 5, 5 } : (int[]){ 0, 0, 0, 0 });
	if (flags & CN_FLOAT_AREA)
		st->children[1].leaf.value.list->items[0].value = (struct ex_value){ .type = EX_FLOAT, .f = 0 };
	st->children[2] = bu_branch(sjis ? CN_STEPS_S : BR_STEPS, n);
	for (int i = 0; i < n; i++) {
		const struct cn_step *s = &steps[i];
		char key[32];
		snprintf(key, sizeof key, "Step%d", i + 1);
		unsigned k = 0;
		struct ex_tree t = bu_branch(key, 8);
		if (!(flags & CN_NO_COMMAND))
			t.children[k++] = bu_ileaf(sjis ? CN_COMMAND_S : BR_COMMAND, s->cmd);
		t.children[k++] = lb_ilist(sjis ? CN_DEST_S : LB_DEST, (flags & CN_SHORT_DEST) ? 4 : 6, s->dest);
		if (flags & CN_FLOAT_DEST) {
			struct ex_list *l = t.children[k - 1].leaf.value.list;
			for (unsigned j = 0; j < l->nr_items; j++)
				l->items[j].value = (struct ex_value){ .type = EX_FLOAT, .f = s->dest[j] };
		}
		if (!(flags & CN_NO_COLOR)) {
			t.children[k++] = lb_ilist(sjis ? CN_COLOR1_S : CN_COLOR1, 4, s->color);
			if (flags & CN_FLOAT_COLOR)
				t.children[k - 1].leaf.value.list->items[0].value = (struct ex_value){ .type = EX_FLOAT, .f = s->color[0] };
		}
		if (!(flags & CN_NO_FULL))
			t.children[k++] = bu_ileaf(sjis ? CN_FULL_S : CN_FULL, s->full);
		if (!(flags & CN_NO_RADIUS))
			t.children[k++] = lb_ilist(sjis ? CN_RADIUS_S : CN_RADIUS, 2, s->radius);
		if (!(flags & CN_NO_ARC))
			t.children[k++] = lb_ilist(sjis ? CN_ARC_S : CN_ARC, 2, s->arc);
		if (!(flags & CN_NO_ROTATE))
			t.children[k++] = bu_ileaf(sjis ? CN_ROTATE_S : CN_ROTATE, s->rotate);
		t.children[k++] = bu_sleaf(BR_CG_NAME, s->cg ? s->cg : "");
		t.nr_children = k;
		st->children[2].children[i] = t;
	}
	return lb_node(name, pl, info, 0);
}

static int cn_load(const char *act_name, struct ex *e)
{
	struct string *name = cstr_to_string(act_name);
	bool loaded = probe_load_activity_tree(name, e);
	free_string(name);
	ex_free(e);
	CN_CHECK(loaded, "%s did not load", act_name);
	return sf_str(act_name, -1);
}
static int cn_no(int act, const char *name)
{
	int n = bu_number(act, name);
	CN_CHECK(n > 0, "%s has no parts number", name);
	return n;
}

static uint32_t cn_fnv(const uint8_t *p, size_t n)
{
	uint32_t h = 0x811c9dc5;
	for (size_t i = 0; i < n; i++)
		h = (h ^ p[i]) * 0x01000193;
	return h;
}

static int cn_nr_ops(struct parts *p)
{
	int n = 0;
	struct parts_cp_op *op;
	TAILQ_FOREACH(op, &p->states[0].cproc.ops, entry)
		n++;
	return n;
}

/* The RGBA pixels of a built normal state (to be freed), or NULL with a
 * failure when the state is not a w x h construction with a texture. */
static uint8_t *cn_pixels(int no, int w, int h, const char *what)
{
	struct parts *p = no > 0 ? parts_try_get(no) : NULL;
	if (!p) {
		CN_CHECK(0, "%s: no parts", what);
		return NULL;
	}
	struct parts_state *s = &p->states[0];
	bool built = s->type == PARTS_CONSTRUCTION_PROCESS && s->common.texture.handle
		&& s->common.texture.w == w && s->common.texture.h == h && s->common.w == w && s->common.h == h;
	CN_CHECK(built, "%s: state type %d, texture %s %dx%d, size %dx%d; want a built %dx%d construction",
		 what, s->type, s->common.texture.handle ? "present" : "missing", s->common.texture.w,
		 s->common.texture.h, s->common.w, s->common.h, w, h);
	int type = BU_PE("GetComponentType", BU_I(no), BU_I(1)).i;
	CN_CHECK(type == 26, "%s: GetComponentType %d, want 26", what, type);
	return built ? gfx_get_pixels(&s->common.texture) : NULL;
}

struct cn_px { int x, y; uint8_t c[4]; };

static void cn_expect(int no, int w, int h, uint32_t fnv, int npx, const struct cn_px *px, const char *what)
{
	uint8_t *got = cn_pixels(no, w, h, what);
	if (!got)
		return;
	for (int i = 0; i < npx; i++) {
		const uint8_t *g = got + (px[i].y * w + px[i].x) * 4;
		CN_CHECK(!memcmp(g, px[i].c, 4), "%s (%d,%d) = (%d,%d,%d,%d), want (%d,%d,%d,%d)", what,
			 px[i].x, px[i].y, g[0], g[1], g[2], g[3], px[i].c[0], px[i].c[1], px[i].c[2], px[i].c[3]);
	}
	uint32_t f = cn_fnv(got, (size_t)w * h * 4);
	CN_CHECK(f == fnv, "%s: pixels hash 0x%08x, want 0x%08x", what, (unsigned)f, (unsigned)fnv);
	free(got);
}
#define CN_PX(...) (int)(sizeof((struct cn_px[]){ __VA_ARGS__ }) / sizeof(struct cn_px)), (struct cn_px[]){ __VA_ARGS__ }

/* The previous behaviour for a state with steps: no construction state, no
 * texture or size, type 26 and the first step's canvas for layout boxes. */
static void cn_expect_unbuilt(int no, int canvas_w, int canvas_h, const char *what)
{
	struct parts *p = no > 0 ? parts_try_get(no) : NULL;
	if (!p) {
		CN_CHECK(0, "%s: no parts", what);
		return;
	}
	struct parts_state *s = &p->states[0];
	CN_CHECK(s->type != PARTS_CONSTRUCTION_PROCESS && !s->common.texture.handle && !s->common.w && !s->common.h,
		 "%s: state type %d, texture %s, size %dx%d; want it left unbuilt", what, s->type,
		 s->common.texture.handle ? "present" : "missing", s->common.w, s->common.h);
	int type = BU_PE("GetComponentType", BU_I(no), BU_I(1)).i;
	CN_CHECK(type == 26, "%s: GetComponentType %d, want 26", what, type);
	CN_CHECK(p->pactex_canvas_w == canvas_w && p->pactex_canvas_h == canvas_h,
		 "%s: canvas %dx%d, want %dx%d", what, p->pactex_canvas_w, p->pactex_canvas_h, canvas_w, canvas_h);
}

/* Two shapes the loader builds; CN4's variants differ from them in one thing. */
static const struct cn_step cn_plain[] = {
	CN_CREATE(20, 20),
	CN_FILL(5, 2, 2, 5, 5, 0, 0, 0, 100),
};
static const struct cn_step cn_pieonly[] = {
	CN_CREATE(40, 40),
	CN_PIE(20, 20, 16, 16, 0, 90, 255),
};
static const struct cn_step cn_round128[] = {
	CN_CREATE(128, 40),
	CN_FILL_ALL(6, 255, 255, 255, 0),
	CN_FILL(5, 16, 0, 96, 40, 0, 0, 0, 255),
	CN_FILL(5, 0, 16, 128, 8, 0, 0, 0, 255),
	CN_PIE(16, 16, 16, 16, 180, 90, 255),
	CN_PIE(112, 16, 16, 16, 270, 90, 255),
	CN_PIE(112, 24, 16, 16, 0, 90, 255),
	CN_PIE(16, 24, 16, 16, 90, 90, 255),
};
#define CN_ROUND128_FNV 0xa3a5a459
static const struct cn_step cn_timer[] = {
	CN_CREATE(262, 22),
	CN_FILL(6, 3, 3, 256, 16, 80, 80, 80, 160),
};
#define CN_TIMER_FNV 0xd5c01565

/* --- CN1 --- */
static void cn_round(void)
{
	cn_gl_setup();
	struct ex_tree *kids;
	struct ex *e = lb_activity("ConstructionRound", 2, &kids);
	struct lb_place pl = { 10, 20, 1 };
	kids->children[0] = cn_parts("Round", &pl, CN_N(cn_round128), 0);
	kids->children[1] = cn_parts("RoundSjis", &pl, CN_N(cn_round128), CN_SJIS);
	int act = cn_load("ConstructionRound", e);
	int round = cn_no(act, "Round"), sjis = cn_no(act, "RoundSjis");
	heap_unref(act);
	cn_expect(round, 128, 40, CN_ROUND128_FNV, CN_PX(
		{ 64, 20, { 255, 255, 255, 255 } },
		{ 0, 0, { 255, 255, 255, 0 } }, { 127, 0, { 255, 255, 255, 0 } },
		{ 0, 39, { 255, 255, 255, 0 } }, { 127, 39, { 255, 255, 255, 0 } },
		{ 16, 0, { 255, 255, 255, 227 } }, { 0, 16, { 255, 255, 255, 227 } },
		{ 11, 0, { 255, 255, 255, 67 } }, { 116, 0, { 255, 255, 255, 75 } }), "CN1 Round");
	struct parts *p = round > 0 ? parts_try_get(round) : NULL;
	if (p && p->states[0].type == PARTS_CONSTRUCTION_PROCESS && p->states[0].common.texture.handle
			&& p->states[0].common.texture.w == 128 && p->states[0].common.texture.h == 40) {
		uint8_t *px = gfx_get_pixels(&p->states[0].common.texture);
		int a0 = 0, a255 = 0, mid = 0, rgb = 0;
		for (int i = 0; i < 128 * 40; i++) {
			uint8_t *c = px + i * 4;
			if (c[3] == 0) a0++;
			else if (c[3] == 255) a255++;
			else mid++;
			rgb += c[0] == 255 && c[1] == 255 && c[2] == 255;
		}
		free(px);
		CN_CHECK(a0 == 168 && a255 == 4836 && mid == 116, "CN1 alpha histogram 0:%d 255:%d between:%d, want 168, 4836, 116", a0, a255, mid);
		CN_CHECK(rgb == 128 * 40, "CN1 %d of %d pixels are white", rgb, 128 * 40);
		CN_CHECK(cn_nr_ops(p) == 8, "CN1 %d operations, want 8", cn_nr_ops(p));
		CN_CHECK(!p->pactex_canvas_w && !p->pactex_canvas_h, "CN1 a built state recorded a canvas %dx%d",
			 p->pactex_canvas_w, p->pactex_canvas_h);
		int w = BU_PE("Parts_GetPartsWidth", BU_I(round), BU_I(1)).i, h = BU_PE("Parts_GetPartsHeight", BU_I(round), BU_I(1)).i;
		CN_CHECK(w == 128 && h == 40, "CN1 Parts_GetPartsWidth/Height %dx%d, want 128x40", w, h);
	}
	cn_expect(sjis, 128, 40, CN_ROUND128_FNV, 0, NULL, "CN1 SJIS keys");
	printf("CN1 count label base: %s\n", cn_failures ? "FAIL" : "ok");
}

/* --- CN2 --- */
static const struct cn_step cn_pill92[] = {
	CN_CREATE(92, 24),
	CN_FILL_ALL(6, 255, 0, 186, 0),
	CN_FILL(5, 12, 0, 68, 24, 0, 0, 0, 255),
	CN_PIE(12, 12, 12, 12, 180, 90, 255),
	CN_PIE(80, 12, 12, 12, 270, 90, 255),
	CN_PIE(80, 12, 12, 12, 0, 90, 255),
	CN_PIE(12, 12, 12, 12, 90, 90, 255),
};
static const struct cn_step cn_pill41[] = {
	CN_CREATE(41, 18),
	CN_FILL_ALL(6, 0, 64, 255, 0),
	CN_FILL(5, 9, 0, 23, 18, 0, 0, 0, 255),
	CN_PIE(9, 9, 9, 9, 180, 90, 255),
	CN_PIE(32, 9, 9, 9, 270, 90, 255),
	CN_PIE(32, 9, 9, 9, 0, 90, 255),
	CN_PIE(9, 9, 9, 9, 90, 90, 255),
};
static const struct cn_step cn_ellipse[] = {
	CN_CREATE(40, 30),
	CN_FILL_ALL(6, 9, 9, 9, 0),
	CN_PIE(20, 15, 10, 6, 0, 90, 255),
	CN_PIE(20, 15, 10, 6, 180, 90, 255),
};
static const struct cn_step cn_edge[] = {
	CN_CREATE(24, 24),
	CN_FILL_ALL(6, 7, 7, 7, 0),
	CN_PIE(4, 4, 8, 8, 180, 90, 255),
	CN_PIE(20, 20, 8, 8, 0, 90, 255),
};
static const struct cn_step cn_alpha128[] = {
	CN_CREATE(32, 32),
	CN_FILL_ALL(6, 1, 1, 1, 0),
	CN_PIE(16, 16, 16, 16, 0, 90, 128),
	CN_PIE(16, 16, 16, 16, 180, 90, 0),
};
static const struct cn_step cn_sweeps[] = {
	CN_CREATE(48, 48),
	CN_FILL_ALL(6, 2, 2, 2, 0),
	CN_PIE(24, 24, 10, 10, 90, 180, 255),
	CN_PIE(24, 24, 20, 20, 270, 270, 77),
};
static const struct cn_step cn_sweep360[] = {
	CN_CREATE(32, 32),
	CN_FILL_ALL(6, 3, 3, 3, 0),
	CN_PIE(16, 16, 12, 12, 90, 360, 255),
	CN_PIE(16, 16, 12, 12, 0, 0, 99),
};
static const struct cn_step cn_r10[] = {
	CN_CREATE(20, 20),
	CN_FILL_ALL(6, 5, 5, 5, 0),
	CN_PIE(10, 10, 10, 10, 0, 90, 255),
	CN_PIE(10, 10, 10, 10, 90, 90, 255),
	CN_PIE(10, 10, 10, 10, 180, 90, 255),
	CN_PIE(10, 10, 10, 10, 270, 90, 255),
};
static const struct cn_step cn_r13[] = {
	CN_CREATE(26, 26),
	CN_FILL_ALL(6, 6, 6, 6, 0),
	CN_PIE(13, 13, 13, 13, 0, 90, 255),
	CN_PIE(13, 13, 13, 13, 90, 90, 255),
	CN_PIE(13, 13, 13, 13, 180, 90, 255),
	CN_PIE(13, 13, 13, 13, 270, 90, 255),
};
static const struct cn_step cn_trunc[] = {
	CN_CREATE(16, 16),
	CN_FILL_ALL(6, 7, 7, 7, 0),
	CN_PIE(-1, 8, 8, 8, 90, 90, 255),
};
static const struct cn_step cn_noop[] = {
	CN_CREATE(16, 16),
	CN_FILL_ALL(6, 4, 4, 4, 0),
	CN_PIE(8, 8, 0, 8, 0, 90, 255),
	CN_PIE(8, 8, 8, 0, 0, 90, 255),
	CN_PIE(8, 8, 8, 8, -90, 90, 255),
	CN_PIE(8, 8, 8, 8, 0, -90, 255),
};

static void cn_sectors(void)
{
	cn_gl_setup();
	struct ex_tree *kids;
	struct ex *e = lb_activity("ConstructionSectors", 12, &kids);
	struct lb_place pl = { 0, 0, 1 };
	kids->children[0] = cn_parts("Pill92", &pl, CN_N(cn_pill92), 0);
	kids->children[1] = cn_parts("Pill41", &pl, CN_N(cn_pill41), 0);
	kids->children[2] = cn_parts("Ellipse", &pl, CN_N(cn_ellipse), 0);
	kids->children[3] = cn_parts("Edge", &pl, CN_N(cn_edge), 0);
	kids->children[4] = cn_parts("Alpha128", &pl, CN_N(cn_alpha128), 0);
	kids->children[5] = cn_parts("Sweeps", &pl, CN_N(cn_sweeps), 0);
	kids->children[6] = cn_parts("Sweep360", &pl, CN_N(cn_sweep360), 0);
	kids->children[7] = cn_parts("Noop", &pl, CN_N(cn_noop), 0);
	kids->children[8] = cn_parts("R10", &pl, CN_N(cn_r10), 0);
	kids->children[9] = cn_parts("R13", &pl, CN_N(cn_r13), 0);
	kids->children[10] = cn_parts("Trunc", &pl, CN_N(cn_trunc), 0);
	kids->children[11] = cn_parts("PieOnly", &pl, CN_N(cn_pieonly), 0);
	int act = cn_load("ConstructionSectors", e);
	/* A pill: two half discs of r 12 (r 9) around a 68 (23) wide bar. */
	cn_expect(cn_no(act, "Pill92"), 92, 24, 0x40ba6905, CN_PX(
		{ 46, 12, { 255, 0, 186, 255 } }, { 0, 0, { 255, 0, 186, 0 } }, { 91, 23, { 255, 0, 186, 0 } }), "CN2 r 12");
	cn_expect(cn_no(act, "Pill41"), 41, 18, 0x21ca7ab1, 0, NULL, "CN2 r 9");
	/* Radii 10 and 6: (29,15) is inside, (30,15) outside; below the centre
	 * the edge is at y 21 (alpha 3). */
	cn_expect(cn_no(act, "Ellipse"), 40, 30, 0xbfc16ba0, CN_PX(
		{ 20, 15, { 9, 9, 9, 255 } }, { 29, 15, { 9, 9, 9, 255 } }, { 30, 15, { 9, 9, 9, 0 } },
		{ 20, 20, { 9, 9, 9, 255 } }, { 20, 21, { 9, 9, 9, 3 } }, { 10, 15, { 9, 9, 9, 227 } },
		{ 19, 9, { 9, 9, 9, 223 } }, { 20, 9, { 9, 9, 9, 227 } }), "CN2 ellipse");
	/* A sector reaching past the left/top (and right/bottom) edge is cut. */
	cn_expect(cn_no(act, "Edge"), 24, 24, 0x6cd415f5, CN_PX(
		{ 0, 0, { 7, 7, 7, 255 } }, { 3, 3, { 7, 7, 7, 255 } }, { 4, 4, { 7, 7, 7, 0 } },
		{ 20, 20, { 7, 7, 7, 255 } }, { 23, 23, { 7, 7, 7, 255 } }), "CN2 cut by the edge");
	/* Alpha 128 scales with the coverage; alpha 0 writes 0. */
	cn_expect(cn_no(act, "Alpha128"), 32, 32, 0x87f49e4a, CN_PX(
		{ 16, 16, { 1, 1, 1, 128 } }, { 27, 27, { 1, 1, 1, 33 } }, { 15, 15, { 1, 1, 1, 0 } }), "CN2 alpha 128");
	/* 90+180 is the left half; 270+270 ends at 180, so it leaves out the
	 * upper left quarter, which keeps the first sector's 255. */
	cn_expect(cn_no(act, "Sweeps"), 48, 48, 0xe05b4562, CN_PX(
		{ 24, 24, { 2, 2, 2, 77 } }, { 20, 28, { 2, 2, 2, 77 } }, { 20, 20, { 2, 2, 2, 255 } },
		{ 28, 28, { 2, 2, 2, 77 } }, { 40, 10, { 2, 2, 2, 0 } }), "CN2 sweeps 180 and 270");
	/* 360 ends where it starts: only the pixels exactly on that axis. */
	cn_expect(cn_no(act, "Sweep360"), 32, 32, 0x4771e768, CN_PX(
		{ 16, 16, { 3, 3, 3, 99 } }, { 16, 20, { 3, 3, 3, 255 } }, { 15, 20, { 3, 3, 3, 0 } },
		{ 20, 16, { 3, 3, 3, 99 } }, { 20, 17, { 3, 3, 3, 0 } }), "CN2 sweeps 360 and 0");
	cn_expect(cn_no(act, "Noop"), 16, 16, 0x12378dc5, 0, NULL, "CN2 radius 0 and negative angles");
	/* (4,2) and (18,16) hold the subsample (-6,-8) resp. (8,6) from the
	 * centre: 63 of 64 and none with x * (1/10), 64 and one with x / 10. */
	cn_expect(cn_no(act, "R10"), 20, 20, 0xd0ca0cec, CN_PX(
		{ 4, 2, { 5, 5, 5, 251 } }, { 18, 16, { 5, 5, 5, 0 } }), "CN2 r 10 (reciprocal)");
	/* (8,1) and (18,25): (-5,-12) resp. (5,12); a fused multiply-add of the
	 * squares would count them. */
	cn_expect(cn_no(act, "R13"), 26, 26, 0x3747c5c1, CN_PX(
		{ 8, 1, { 6, 6, 6, 251 } }, { 18, 25, { 6, 6, 6, 0 } }), "CN2 r 13 (separate rounding)");
	/* Centre x -1, the quarter below left: only the column at -0.5 is near
	 * the surface, and truncation puts it on x 0 (a floor would drop it). */
	cn_expect(cn_no(act, "Trunc"), 16, 16, 0x45848110, CN_PX(
		{ 0, 8, { 7, 7, 7, 0 } }, { 0, 9, { 7, 7, 7, 255 } }, { 0, 15, { 7, 7, 7, 255 } }, { 1, 9, { 7, 7, 7, 0 } }), "CN2 left of the surface");
	cn_expect(cn_no(act, "PieOnly"), 40, 40, 0xa8cedbc1, CN_PX(
		{ 20, 20, { 0, 0, 0, 255 } }, { 31, 31, { 0, 0, 0, 67 } }), "CN2 a sector alone");
	heap_unref(act);
	printf("CN2 sectors: %s\n", cn_failures ? "FAIL" : "ok");
}

/* --- CN3 --- */
static const struct cn_step cn_clip256[] = {
	CN_CREATE(256, 40),
	CN_FILL_ALL(6, 0, 0, 0, 0),
	CN_FILL(5, 16, 0, 224, 40, 0, 0, 0, 255),
	CN_FILL(5, 0, 16, 400, 8, 0, 0, 0, 255),
	CN_PIE(16, 16, 16, 16, 180, 90, 255),
	CN_PIE(240, 16, 16, 16, 270, 90, 255),
	CN_PIE(240, 24, 16, 16, 0, 90, 255),
	CN_PIE(16, 24, 16, 16, 90, 90, 255),
};
static const struct cn_step cn_neg[] = {
	CN_CREATE(40, 20),
	CN_FILL_ALL(6, 10, 20, 30, 40),
	CN_FILL(5, 20, 10, -10, -5, 0, 0, 0, 200),
	CN_FILL(3, 30, 2, -40, 6, 1, 2, 3, 99),
};
static const struct cn_step cn_rgb[] = {
	CN_CREATE(99, 18),
	CN_FILL(3, 3, 3, 93, 12, 255, 255, 255, 0),
};
static const struct cn_step cn_hole[] = {
	CN_CREATE(60, 40),
	CN_FILL(5, 10, 10, 40, 20, 0, 0, 0, 0),
};
static const struct cn_step cn_a999[] = {
	CN_CREATE(20, 10),
	CN_FILL(3, 2, 2, 6, 4, 1, 2, 3, 999),
};
static const struct cn_step cn_rgb999[] = {
	CN_CREATE(20, 10),
	CN_FILL(5, 2, 2, 6, 4, 999, 999, 999, 77),
};
static const struct cn_step cn_createonly[] = {
	CN_CREATE(12, 7),
};
static const struct cn_step cn_full2[] = {
	CN_CREATE(20, 20),
	{ 6, { 2, 2, 0, 0, 5, 5 }, { 10, 20, 30, 40 }, 2 },
};

static void cn_fills(void)
{
	cn_gl_setup();
	struct ex_tree *kids;
	struct ex *e = lb_activity("ConstructionFills", 10, &kids);
	struct lb_place pl = { 0, 0, 1 };
	kids->children[0] = cn_parts("Timer", &pl, CN_N(cn_timer), 0);
	kids->children[1] = cn_parts("Clip", &pl, CN_N(cn_clip256), 0);
	kids->children[2] = cn_parts("Negative", &pl, CN_N(cn_neg), 0);
	kids->children[3] = cn_parts("Rgb", &pl, CN_N(cn_rgb), 0);
	kids->children[4] = cn_parts("Hole", &pl, CN_N(cn_hole), 0);
	kids->children[5] = cn_parts("Full2", &pl, CN_N(cn_full2), 0);
	kids->children[6] = cn_parts("A999", &pl, CN_N(cn_a999), 0);
	kids->children[7] = cn_parts("Rgb999", &pl, CN_N(cn_rgb999), 0);
	kids->children[8] = cn_parts("Plain", &pl, CN_N(cn_plain), 0);
	kids->children[9] = cn_parts("CreateOnly", &pl, CN_N(cn_createonly), 0);
	int act = cn_load("ConstructionFills", e);
	/* A 3 px opaque black frame around the translucent grey. */
	cn_expect(cn_no(act, "Timer"), 262, 22, CN_TIMER_FNV, CN_PX(
		{ 0, 0, { 0, 0, 0, 255 } }, { 2, 2, { 0, 0, 0, 255 } }, { 3, 3, { 80, 80, 80, 160 } },
		{ 258, 18, { 80, 80, 80, 160 } }, { 259, 19, { 0, 0, 0, 255 } }, { 261, 21, { 0, 0, 0, 255 } }), "CN3 timer base");
	/* (0,16,400,8) on a 256 wide surface fills to its right edge. */
	cn_expect(cn_no(act, "Clip"), 256, 40, 0x4b1ac1a1, CN_PX(
		{ 255, 16, { 0, 0, 0, 255 } }, { 240, 0, { 0, 0, 0, 227 } }, { 255, 0, { 0, 0, 0, 0 } }), "CN3 clipped fill");
	/* (20,10,-10,-5) is (10,5)..(20,10); (30,2,-40,6) is x 0..30, y 2..8. */
	cn_expect(cn_no(act, "Negative"), 40, 20, 0x9a8acbd5, CN_PX(
		{ 0, 0, { 10, 20, 30, 40 } }, { 10, 5, { 1, 2, 3, 200 } }, { 19, 9, { 10, 20, 30, 200 } },
		{ 20, 10, { 10, 20, 30, 40 } }, { 0, 2, { 1, 2, 3, 40 } }, { 29, 7, { 1, 2, 3, 40 } },
		{ 30, 2, { 10, 20, 30, 40 } }, { 0, 8, { 10, 20, 30, 40 } }), "CN3 negative sizes");
	/* Command 3 keeps the alpha the surface was created with. */
	cn_expect(cn_no(act, "Rgb"), 99, 18, 0x5312b6a5, CN_PX(
		{ 0, 0, { 0, 0, 0, 255 } }, { 3, 3, { 255, 255, 255, 255 } }, { 95, 14, { 255, 255, 255, 255 } },
		{ 96, 15, { 0, 0, 0, 255 } }), "CN3 RGB fill");
	cn_expect(cn_no(act, "Hole"), 60, 40, 0x839b01c5, CN_PX(
		{ 9, 9, { 0, 0, 0, 255 } }, { 10, 10, { 0, 0, 0, 0 } }, { 49, 29, { 0, 0, 0, 0 } }, { 50, 30, { 0, 0, 0, 255 } }), "CN3 alpha hole");
	/* Only 全體 == 1 is the whole surface (0x4f8f76): 2 keeps (2,2,5,5). */
	cn_expect(cn_no(act, "Full2"), 20, 20, 0xc64ddaf8, CN_PX(
		{ 1, 1, { 0, 0, 0, 255 } }, { 2, 2, { 10, 20, 30, 40 } }, { 6, 6, { 10, 20, 30, 40 } },
		{ 7, 7, { 0, 0, 0, 255 } }), "CN3 全體 2");
	/* Command 3 does not write A and command 5 does not write RGB: a value
	 * outside 0..255 there does not keep the state from being built. */
	cn_expect(cn_no(act, "A999"), 20, 10, 0x9f8d2055, CN_PX(
		{ 2, 2, { 1, 2, 3, 255 } }, { 7, 5, { 1, 2, 3, 255 } }, { 8, 6, { 0, 0, 0, 255 } }), "CN3 command 3 with A 999");
	cn_expect(cn_no(act, "Rgb999"), 20, 10, 0x33a82b85, CN_PX(
		{ 2, 2, { 0, 0, 0, 77 } }, { 7, 5, { 0, 0, 0, 77 } }, { 8, 6, { 0, 0, 0, 255 } }), "CN3 command 5 with RGB 999");
	cn_expect(cn_no(act, "Plain"), 20, 20, 0xd537c564, CN_PX(
		{ 1, 1, { 0, 0, 0, 255 } }, { 2, 2, { 0, 0, 0, 100 } }, { 6, 6, { 0, 0, 0, 100 } }, { 7, 7, { 0, 0, 0, 255 } }), "CN3 a fill alone");
	cn_expect(cn_no(act, "CreateOnly"), 12, 7, 0x879d7de5, CN_PX(
		{ 0, 0, { 0, 0, 0, 255 } }, { 11, 6, { 0, 0, 0, 255 } }), "CN3 command 0 alone");
	heap_unref(act);
	printf("CN3 fills: %s\n", cn_failures ? "FAIL" : "ok");
}

/* --- CN4 --- */
static void cn_unbuilt(void)
{
	cn_gl_setup();
	/* 90 is a command the loader does not build (26, which stood here, is
	 * built since construction-cmds; these two had no rectangle). */
	static const struct cn_step unsupported[] = { CN_CREATE(30, 12), { 90 }, { 90 }, CN_FILL_ALL(3, 1, 2, 3, 0), CN_FILL_ALL(3, 4, 5, 6, 0) };
	static const struct cn_step missing_cg[] = { { 2, .cg = "ConstructionMissingCg" }, { 28 }, { 27 } };
	static const struct cn_step rotated[] = { CN_CREATE(40, 40), CN_FILL_ALL(6, 0, 0, 0, 0),
		{ 122, { 20, 20 }, { 0, 0, 0, 255 }, 0, { 16, 16 }, { 0, 90 }, 30 } };
	static const struct cn_step oblique[] = { CN_CREATE(40, 40), CN_PIE(20, 20, 16, 16, 45, 90, 255) };
	static const struct cn_step oblique_sweep[] = { CN_CREATE(40, 40), CN_PIE(20, 20, 16, 16, 0, 45, 255) };
	static const struct cn_step zero_size[] = { CN_CREATE(0, 12) };
	static const struct cn_step no_create[] = { CN_FILL_ALL(6, 0, 0, 0, 0) };
	static const struct cn_step empty_rect[] = { CN_CREATE(20, 20), CN_FILL(5, 30, 0, 5, 5, 0, 0, 0, 255) };
	static const struct cn_step zero_rect[] = { CN_CREATE(20, 20), CN_FILL(6, 2, 2, 0, 5, 0, 0, 0, 255) };
	static const struct cn_step two_creates[] = { CN_CREATE(20, 20), CN_CREATE(10, 10) };
	static const struct cn_step color_range[] = { CN_CREATE(20, 20), CN_FILL_ALL(6, 300, 0, 0, 0) };
	static const struct cn_step alpha_range[] = { CN_CREATE(20, 20), CN_PIE(10, 10, 8, 8, 0, 90, 256) };
	static const struct cn_step huge[] = { CN_CREATE(20000, 20000) };
	static const struct cn_step big_radius[] = { CN_CREATE(20, 20), CN_PIE(10, 10, 5000, 5000, 0, 90, 255) };
	static const struct cn_step zero_height[] = { CN_CREATE(12, 0) };
	static const struct cn_step alpha_range5[] = { CN_CREATE(20, 20), CN_FILL(5, 2, 2, 5, 5, 0, 0, 0, 256) };
	static const struct cn_step red_range3[] = { CN_CREATE(20, 20), CN_FILL(3, 2, 2, 5, 5, -1, 0, 0, 0) };
	static const struct cn_step start450[] = { CN_CREATE(40, 40), CN_PIE(20, 20, 16, 16, 450, 90, 255) };
	static const struct cn_step sweep720[] = { CN_CREATE(40, 40), CN_PIE(20, 20, 16, 16, 0, 720, 255) };
	static const struct cn_step start_minus450[] = { CN_CREATE(40, 40), CN_PIE(20, 20, 16, 16, -450, 90, 255) };
	static const struct cn_step alpha_range6[] = { CN_CREATE(20, 20), CN_FILL(6, 2, 2, 5, 5, 0, 0, 0, 256) };
	static const struct cn_step zero_height_rect[] = { CN_CREATE(20, 20), CN_FILL(5, 2, 2, 5, 0, 0, 0, 0, 100) };
	static const struct cn_step wide_create[] = { CN_CREATE(8193, 10) };
	/* X + W is 2^31 + 4: clipped in 64 bits it would be (5,2)..(20,7) */
	static const struct cn_step wrap_width[] = { CN_CREATE(20, 20), CN_FILL(5, 5, 2, 2147483647, 5, 0, 0, 0, 100) };
	const struct { const char *name; int n; const struct cn_step *steps; int flags, cw, ch; } c[] = {
		{ "Unsupported", CN_N(unsupported), 0, 30, 12 },
		{ "MissingCg", CN_N(missing_cg), 0, 0, 0 },
		{ "Rotated", CN_N(rotated), 0, 40, 40 },
		{ "Oblique", CN_N(oblique), 0, 40, 40 },
		{ "ObliqueSweep", CN_N(oblique_sweep), 0, 40, 40 },
		{ "ZeroSize", CN_N(zero_size), 0, 0, 0 },
		{ "NoCreate", CN_N(no_create), 0, 0, 0 },
		{ "EmptyRect", CN_N(empty_rect), 0, 20, 20 },
		{ "ZeroRect", CN_N(zero_rect), 0, 20, 20 },
		{ "TwoCreates", CN_N(two_creates), 0, 20, 20 },
		{ "ColorRange", CN_N(color_range), 0, 20, 20 },
		{ "AlphaRange", CN_N(alpha_range), 0, 20, 20 },
		{ "Huge", CN_N(huge), 0, 20000, 20000 },
		{ "NoColorKey", CN_N(cn_plain), CN_NO_COLOR, 20, 20 },
		{ "FloatDest", CN_N(cn_plain), CN_FLOAT_DEST, 20, 20 },
		{ "SurfaceArea", CN_N(cn_plain), CN_SURFACE_AREA, 20, 20 },
		{ "BigRadius", CN_N(big_radius), 0, 20, 20 },
		/* a missing コマンド is 0 for the canvas (as in the original) */
		{ "NoCommandKey", CN_N(cn_plain), CN_NO_COMMAND, 20, 20 },
		{ "NoFullKey", CN_N(cn_plain), CN_NO_FULL, 20, 20 },
		{ "NoRadiusKey", CN_N(cn_pieonly), CN_NO_RADIUS, 40, 40 },
		{ "NoArcKey", CN_N(cn_pieonly), CN_NO_ARC, 40, 40 },
		{ "NoRotateKey", CN_N(cn_pieonly), CN_NO_ROTATE, 40, 40 },
		{ "ShortDest", CN_N(cn_plain), CN_SHORT_DEST, 0, 0 },
		{ "FloatColor", CN_N(cn_plain), CN_FLOAT_COLOR, 20, 20 },
		{ "FloatArea", CN_N(cn_plain), CN_FLOAT_AREA, 20, 20 },
		{ "ZeroHeight", CN_N(zero_height), 0, 0, 0 },
		{ "AlphaRange5", CN_N(alpha_range5), 0, 20, 20 },
		{ "RedRange3", CN_N(red_range3), 0, 20, 20 },
		{ "Start450", CN_N(start450), 0, 40, 40 },
		{ "Sweep720", CN_N(sweep720), 0, 40, 40 },
		{ "StartMinus450", CN_N(start_minus450), 0, 40, 40 },
		{ "AlphaRange6", CN_N(alpha_range6), 0, 20, 20 },
		{ "ZeroHeightRect", CN_N(zero_height_rect), 0, 20, 20 },
		{ "WideCreate", CN_N(wide_create), 0, 8193, 10 },
		{ "WrapWidth", CN_N(wrap_width), 0, 20, 20 },
	};
	const int n = sizeof c / sizeof c[0];
	struct ex_tree *kids;
	struct ex *e = lb_activity("ConstructionUnbuilt", n, &kids);
	struct lb_place pl = { 0, 0, 1 };
	for (int i = 0; i < n; i++)
		kids->children[i] = cn_parts(c[i].name, &pl, c[i].n, c[i].steps, c[i].flags);
	int act = cn_load("ConstructionUnbuilt", e);
	PE_UpdateComponent(0);
	for (int i = 0; i < n; i++) {
		char what[64];
		snprintf(what, sizeof what, "CN4 %s", c[i].name);
		cn_expect_unbuilt(cn_no(act, c[i].name), c[i].cw, c[i].ch, what);
	}
	heap_unref(act);
	printf("CN4 unbuilt states: %s\n", cn_failures ? "FAIL" : "ok");
}

/* --- CN5 --- */
static void cn_empty(void)
{
	cn_gl_setup();
	struct ex_tree *kids;
	struct ex *e = lb_activity("ConstructionEmpty", 1, &kids);
	struct lb_place pl = { 0, 0, 1 };
	kids->children[0] = cn_parts("Empty", &pl, 0, NULL, 0);
	int act = cn_load("ConstructionEmpty", e);
	int no = cn_no(act, "Empty");
	heap_unref(act);
	struct parts *p = no > 0 ? parts_try_get(no) : NULL;
	if (p) {
		struct parts_state *s = &p->states[0];
		CN_CHECK(s->type == PARTS_CONSTRUCTION_PROCESS && !cn_nr_ops(p) && !s->common.texture.handle
			 && !s->common.w && !s->common.h,
			 "CN5 state type %d, %d operations, texture %s, size %dx%d; want an empty construction state",
			 s->type, s->type == PARTS_CONSTRUCTION_PROCESS ? cn_nr_ops(p) : -1,
			 s->common.texture.handle ? "present" : "missing", s->common.w, s->common.h);
		CN_CHECK(BU_PE("GetComponentType", BU_I(no), BU_I(1)).i == 26, "CN5 GetComponentType is not 26");
		PE_AddCreateToPartsConstructionProcess(no, 8, 6, 1);
		PE_AddFillWithAlphaToPartsConstructionProcess(no, 2, 1, 3, 2, 10, 20, 30, 40, 1);
		PE_BuildPartsConstructionProcess(no, 1);
		cn_expect(no, 8, 6, 0x08a6d54d, CN_PX({ 0, 0, { 0, 0, 0, 255 } }, { 2, 1, { 10, 20, 30, 40 } },
			{ 4, 2, { 10, 20, 30, 40 } }, { 5, 3, { 0, 0, 0, 255 } }), "CN5 filled by the game");
		/* The sector operation checks its own ranges, since operations also
		 * come from save data: a surface without alpha is left alone (the
		 * original fails the step, 0x50940e), a radius above the limit
		 * draws nothing; an alpha of 999 is scaled by the coverage and
		 * then clamped (0x4d05c0, 0x5b0a10), e.g. (15,11) with coverage 71
		 * is 255 and not 71. The function is new (dlsym): the tree before
		 * the fix skips this and says so. */
		bool (*add_pie)(int, int, int, int, int, int, int, int, int, int) =
			dlsym(RTLD_DEFAULT, "PE_AddFillPieAMapToPartsConstructionProcess");
		if (add_pie) {
			const int rgb = 9951, big = 9952, over = 9953;
			PE_AddCreatePixelOnlyToPartsConstructionProcess(rgb, 16, 16, 1);
			add_pie(rgb, 8, 8, 8, 8, 0, 90, 0, 0, 1);
			PE_BuildPartsConstructionProcess(rgb, 1);
			PE_AddCreateToPartsConstructionProcess(big, 16, 16, 1);
			add_pie(big, 8, 8, 1025, 1025, 0, 90, 0, 0, 1);
			PE_BuildPartsConstructionProcess(big, 1);
			PE_AddCreateToPartsConstructionProcess(over, 16, 16, 1);
			add_pie(over, 8, 8, 8, 8, 0, 90, 999, 0, 1);
			PE_BuildPartsConstructionProcess(over, 1);
			const int nos[3] = { rgb, big, over };
			const uint32_t fnv[3] = { 0x0c3705c5, 0x0c3705c5, 0xf2bd6175 };
			const char *what[3] = { "CN5 sector on a surface without alpha", "CN5 sector with radius 1025", "CN5 sector with alpha 999" };
			for (int i = 0; i < 3; i++) {
				struct parts_state *ps = &parts_get(nos[i])->states[0];
				uint8_t *px = ps->common.texture.handle ? gfx_get_pixels(&ps->common.texture) : NULL;
				uint32_t f = px ? cn_fnv(px, 16 * 16 * 4) : 0;
				CN_CHECK(f == fnv[i], "%s: pixels hash 0x%08x, want 0x%08x", what[i], (unsigned)f, (unsigned)fnv[i]);
				free(px);
			}
		} else {
			printf("CN5 sector guards: skipped (function not present)\n");
		}
	}
	printf("CN5 empty step list: %s\n", cn_failures ? "FAIL" : "ok");
}

/* --- CN6 --- */
static void cn_save(void)
{
	cn_gl_setup();
	/* A command the loader does not build (102, which stood here, is built
	 * since construction-cmds). */
	static const struct cn_step unsupported[] = { CN_CREATE(30, 12), { 90 } };
	struct ex_tree *kids;
	struct lb_place pl = { 5, 6, 1 };
	/* Version 6 had the create and fill operations. This page is made from
	 * the current writer's, not written by a version 6 engine: without a
	 * sector operation and without parts hidden in the editor, version 7
	 * differs by its number and by the empty list of such parts at the end. */
	struct ex *e6 = lb_activity("ConstructionSaveV6", 1, &kids);
	kids->children[0] = cn_parts("Old", &pl, CN_N(cn_timer), 0);
	int act6 = cn_load("ConstructionSaveV6", e6);
	int old = cn_no(act6, "Old");
	heap_unref(act6);
	struct page *v6 = NULL;
	CN_CHECK(PE_Save(&v6), "PE_Save failed (version 6 page)");
	struct iarray_reader r6;
	if (v6 && iarray_init_reader(&r6, v6, "XPE")) {
		unsigned version_pos = r6.pos;
		int version = iarray_read(&r6);
		CN_CHECK(version <= 10, "XPE v%d: this case only knows how versions 7 to 10 differ from 6", version);
		if (version >= 8) {
			// Version 8 ends with one more list (the parts in 減算色モード);
			// versions 9 and 10 only have more operation types, none here.
			CN_CHECK(v6->values[v6->nr_vars - 1].i == 0, "the page does not end with an empty list of sub colour parts");
			v6->nr_vars--;
		}
		if (version >= 7) {
			CN_CHECK(v6->values[v6->nr_vars - 1].i == 0, "the page does not end with an empty list of hidden parts");
			v6->values[version_pos].i = 6;
			v6->nr_vars--;
		}
		CN_CHECK(PE_Load(&v6), "a version 6 page did not load");
		cn_expect(old, 262, 22, CN_TIMER_FNV, 0, NULL, "CN6 a version 6 page");
	}
	ca_free_buffer(v6);

	struct ex *e = lb_activity("ConstructionSave", 3, &kids);
	kids->children[0] = cn_parts("Round", &pl, CN_N(cn_round128), 0);
	kids->children[1] = cn_parts("Timer", &pl, CN_N(cn_timer), 0);
	kids->children[2] = cn_parts("Unbuilt", &pl, CN_N(unsupported), 0);
	int act = cn_load("ConstructionSave", e);
	int round = cn_no(act, "Round"), timer = cn_no(act, "Timer"), unbuilt = cn_no(act, "Unbuilt");
	heap_unref(act);
	PE_UpdateComponent(0);
	struct page *saved = NULL;
	CN_CHECK(PE_Save(&saved), "PE_Save failed");
	if (!saved)
		return;
	/* A reader before version 7 reads no fields for an operation it does not
	 * know and would lose its place: it has to reject the page. */
	CN_CHECK(ca_save_version(saved) >= 7, "a save with a sector operation is XPE v%d, want 7 or later", ca_save_version(saved));
	CN_CHECK(PE_Load(&saved), "PE_Load failed");
	PE_UpdateComponent(0);
	cn_expect(round, 128, 40, CN_ROUND128_FNV, CN_PX({ 16, 0, { 255, 255, 255, 227 } }), "CN6 Round after loading");
	cn_expect(timer, 262, 22, CN_TIMER_FNV, 0, NULL, "CN6 Timer after loading");
	cn_expect_unbuilt(unbuilt, 30, 12, "CN6 Unbuilt after loading");
	struct parts *p = round > 0 ? parts_try_get(round) : NULL;
	if (p && p->states[0].type == PARTS_CONSTRUCTION_PROCESS)
		CN_CHECK(cn_nr_ops(p) == 8, "CN6 %d operations after loading, want 8", cn_nr_ops(p));
	/* A second round trip writes the same page. */
	struct page *again = NULL;
	CN_CHECK(PE_Save(&again), "the second PE_Save failed");
	if (again) {
		bool same = again->nr_vars == saved->nr_vars;
		for (int i = 0; same && i < again->nr_vars; i++)
			same = again->values[i].i == saved->values[i].i;
		CN_CHECK(same, "saving the loaded parts gave another page (%d and %d values)", saved->nr_vars, again->nr_vars);
		ca_free_buffer(again);
	}
	ca_free_buffer(saved);
	printf("CN6 save and load: %s\n", cn_failures ? "FAIL" : "ok");
}

/* --- CN7 --- */
static void cn_layout(void)
{
	cn_gl_setup();
	static const struct cn_step unsupported[] = { CN_CREATE(128, 40), { 90 } };	/* not built; was 102 */
	struct ex_tree *kids;
	struct ex *e = lb_activity("ConstructionLayout", 2, &kids);
	struct lb_place p0 = { 100, 50, 1 }, px = { 33, 44, 1 };
	kids->children[0] = lb_box("BuiltBox", &p0, 2, 1, 2);
	lb_kids(&kids->children[0])->children[0] = cn_parts("Built", &px, CN_N(cn_round128), 0);
	lb_kids(&kids->children[0])->children[1] = lb_rect("AfterBuilt", &px, 5, 5, 0, 0);
	kids->children[1] = lb_box("UnbuiltBox", &p0, 2, 1, 2);
	lb_kids(&kids->children[1])->children[0] = cn_parts("Unbuilt", &px, CN_N(unsupported), 0);
	lb_kids(&kids->children[1])->children[1] = lb_rect("AfterUnbuilt", &px, 5, 5, 0, 0);
	int act = cn_load("ConstructionLayout", e);
	int built = cn_no(act, "Built"), after_built = cn_no(act, "AfterBuilt"), after_unbuilt = cn_no(act, "AfterUnbuilt");
	int box = cn_no(act, "BuiltBox"), ubox = cn_no(act, "UnbuiltBox");
	heap_unref(act);
	PE_UpdateComponent(0);
	struct parts *r1 = parts_try_get(after_built), *r2 = parts_try_get(after_unbuilt);
	if (r1 && r2) {
		CN_CHECK(r1->local.pos.x == 128 && r1->local.pos.y == 0, "CN7 the rect after the built state is at (%d,%d), want (128,0)",
			 r1->local.pos.x, r1->local.pos.y);
		CN_CHECK(r2->local.pos.x == 128 && r2->local.pos.y == 0, "CN7 the rect after the unbuilt state is at (%d,%d), want (128,0)",
			 r2->local.pos.x, r2->local.pos.y);
	}
	int bw = BU_PE("Parts_GetPartsWidth", BU_I(box), BU_I(1)).i, uw = BU_PE("Parts_GetPartsWidth", BU_I(ubox), BU_I(1)).i;
	CN_CHECK(bw == 133 && uw == 133, "CN7 box widths %d and %d, want 133", bw, uw);
	/* The box is at (100,50): the built state covers (100,50)..(228,90). */
	int in = BU_PE("Parts_IsCursorIn", BU_I(built), BU_I(227), BU_I(89), BU_I(1)).i;
	int right = BU_PE("Parts_IsCursorIn", BU_I(built), BU_I(228), BU_I(89), BU_I(1)).i;
	int below = BU_PE("Parts_IsCursorIn", BU_I(built), BU_I(227), BU_I(90), BU_I(1)).i;
	CN_CHECK(in && !right && !below, "CN7 the built state under the cursor at (227,89) %d, (228,89) %d, (227,90) %d; want 1, 0, 0",
		 in, right, below);
	printf("CN7 layout and cursor: %s\n", cn_failures ? "FAIL" : "ok");
}

/* --- CN8 --- */

static void cn_expect_hidden(int no, bool own_flag, const char *what)
{
	struct parts *p = no > 0 ? parts_try_get(no) : NULL;
	if (!p) {
		CN_CHECK(0, "%s: no parts", what);
		return;
	}
	CN_CHECK(p->edit_hidden == own_flag, "%s: hidden in the editor %d, want %d", what, p->edit_hidden, own_flag);
	CN_CHECK(!p->global.show, "%s is shown", what);
}

static void cn_expect_mask(int no, const char *what)
{
	struct parts *p = no > 0 ? parts_try_get(no) : NULL;
	if (!p)
		return;
	struct parts_state *s = &p->states[0];
	CN_CHECK(s->type == PARTS_CONSTRUCTION_PROCESS && s->common.texture.handle && s->common.w == 1480 && s->common.h == 920,
		 "%s: state type %d, texture %s, size %dx%d; want a built 1480x920 construction", what, s->type,
		 s->common.texture.handle ? "present" : "missing", s->common.w, s->common.h);
}

static void cn_edit_hidden(void)
{
	cn_gl_setup();
	br_input_setup();
	/* Opaque black around a transparent 1280x720: covers the screen from
	 * (-100,-100). */
	static const struct cn_step screen[] = { CN_CREATE(1480, 920), CN_FILL(5, 100, 100, 1280, 720, 0, 0, 0, 0) };
	struct ex_tree *kids;
	struct ex *e = lb_activity("ConstructionHidden", 2, &kids);
	struct lb_place pm = { -100, -100, 1 }, p0 = { 0, 0, 1 };
	kids->children[0] = cn_parts("Mask", &pm, CN_N(screen), 0);
	kids->children[1] = lb_box("Guide", &p0, 0, 1, 1);
	lb_kids(&kids->children[1])->children[0] = cn_parts("GuideMask", &pm, CN_N(screen), 0);
	int act = cn_load("ConstructionHidden", e);
	int mask = cn_no(act, "Mask"), guide = cn_no(act, "Guide"), guide_mask = cn_no(act, "GuideMask");
	heap_unref(act);
	/* 編輯上表示 = 0, as the pactex loader sets it: Mask itself, GuideMask
	 * through its parent. Both in front of a button. */
	parts_set_edit_hidden(parts_get(mask), true);
	parts_set_edit_hidden(parts_get(guide), true);
	PE_SetZ(mask, 50);
	PE_SetZ(guide, 60);
	const int B = 9901;
	br_box(B, 100, 100, 200, 80, true);
	PE_SetZ(B, 10);
	PE_UpdateComponent(0);
	cn_expect_mask(mask, "CN8 Mask");
	cn_expect_mask(guide_mask, "CN8 GuideMask");
	cn_expect_hidden(mask, true, "CN8 Mask");
	cn_expect_hidden(guide_mask, false, "CN8 GuideMask");
	int type, no;
	int n = br_click(150, 140, &type, &no);
	CN_CHECK(n == 1 && type == 4 && no == B, "CN8 click under the hidden masks: %d messages, first type %d parts %d; want 1, 4, %d",
		 n, type, no, B);

	struct page *saved = NULL;
	CN_CHECK(PE_Save(&saved), "PE_Save failed");
	if (!saved)
		return;
	CN_CHECK(PE_Load(&saved), "PE_Load failed");
	PE_UpdateComponent(0);
	cn_expect_mask(mask, "CN8 Mask after loading");
	cn_expect_mask(guide_mask, "CN8 GuideMask after loading");
	cn_expect_hidden(mask, true, "CN8 Mask after loading");
	cn_expect_hidden(guide, true, "CN8 Guide after loading");
	cn_expect_hidden(guide_mask, false, "CN8 GuideMask after loading");
	heap[global_page_slot].page->values[2].i = 0;
	n = br_click(150, 140, &type, &no);
	CN_CHECK(n == 1 && type == 4 && no == B, "CN8 click after loading: %d messages, first type %d parts %d; want 1, 4, %d",
		 n, type, no, B);
	ca_free_buffer(saved);
	printf("CN8 hidden in the editor: %s\n", cn_failures ? "FAIL" : "ok");
}

/* Runs a case in a child: 0 passed, 1 failed, 2 the GL environment failed
 * twice. A child that reports CN_ENV_EXIT, or ends in any way (also with 0)
 * before its setup reported ready (the engine's ERROR for a shader exits
 * with 1), and a fork that fails, are environment failures: the case is run
 * once more. A failed case is not. */
static int cn_fork_case(const char *name, void (*fn)(void), int *failures)
{
	for (int attempt = 0; attempt < 2; attempt++) {
		int fd[2];
		if (pipe(fd)) {
			printf("%s ENVIRONMENT FAILURE (pipe)\n", name);
			return 2;
		}
		fflush(stdout);
		pid_t pid = fork();
		if (pid < 0) {
			close(fd[0]);
			close(fd[1]);
			printf("%s ENVIRONMENT FAILURE (fork)%s\n", name, attempt ? "" : "; running it once more");
			continue;
		}
		if (pid == 0) {
			close(fd[0]);
			cn_ready_fd = fd[1];
			*failures = 0;
			alarm(60);
			fn();
			fflush(stdout);
			_exit(*failures ? 1 : 0);
		}
		close(fd[1]);
		int status = 0;
		waitpid(pid, &status, 0);
		char c;
		bool ready = read(fd[0], &c, 1) == 1;
		close(fd[0]);
		bool passed = WIFEXITED(status) && WEXITSTATUS(status) == 0;
		if (passed && ready)
			return 0;
		const char *how = WIFEXITED(status) ? "exit" : "signal";
		int code = WIFEXITED(status) ? WEXITSTATUS(status) : WTERMSIG(status);
		if (!passed && ready && !(WIFEXITED(status) && code == CN_ENV_EXIT)) {
			printf("%s FAILED (%s %d)\n", name, how, code);
			return 1;
		}
		printf("%s ENVIRONMENT FAILURE (%s %d%s)%s\n", name, how, code,
		       ready ? "" : ", before the GL setup completed", attempt ? "" : "; running it once more");
	}
	return 2;
}

static int test_construction(void)
{
	void (*cases[])(void) = { cn_round, cn_sectors, cn_fills, cn_unbuilt, cn_empty, cn_save, cn_layout,
		cn_edit_hidden };
	const char *names[] = { "CN1", "CN2", "CN3", "CN4", "CN5", "CN6", "CN7", "CN8" };
	int failed = 0, env = 0;
	for (int i = 0; i < 8; i++) {
		int r = cn_fork_case(names[i], cases[i], &cn_failures);
		failed += r == 1;
		env += r == 2;
	}
	if (env) {
		printf("CONSTRUCTION ENVIRONMENT FAILURE: %d of 8 cases could not run, %d failed\n", env, failed);
		return 2;
	}
	printf("CONSTRUCTION %s: %d of 8 cases failed\n", failed ? "FAIL" : "PASS", failed);
	return failed ? 1 : 0;
}
