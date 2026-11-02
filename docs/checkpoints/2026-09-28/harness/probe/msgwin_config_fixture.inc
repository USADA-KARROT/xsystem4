/* msgwin_config_fixture.inc — the message window's brightness and opacity,
 * its pactex text, and the font a text style caches (2026-11-02). Modes:
 * msgwin-rates (WR), msgwin-sample (WS), font-cache (FN).
 *
 * Original (static disassembly of the one executable):
 *   - UpdateComponent (native case 12, 0x57bb82) hands its last two
 *     arguments, MessageWindowMulColorRate and MessageWindowAlphaRate, down
 *     0x53fe80 -> 0x53e690 -> 0x53c5b0 -> 0x53c490, which puts them in the
 *     update context (+0xa4, +0xa8; the context constructor 0x579350 has
 *     1.0 there) for 0x535260. The context is copied to each child
 *     (0x579460 copies +0xa1, +0xa4, +0xa8) and each parts' update gets its
 *     own copy. UpdateMatrix (native case 13, 0x57bbe9 -> 0x53e780) runs
 *     the same 0x53c490 for every layer with both times 0.
 *   - A message window's update (vtable slot 5, 0x4f25e0) calls 0x4f2660
 *     with that context: while the context's visible flag (+0xa0) is set,
 *     the window's private background (+0xa0 of the window, 0x4f1d30)
 *     gets, per channel, the background multiply colour
 *       trunc((bright - base) * (1 - mul_rate) + base)
 *     in single precision, clamped to 0..255 (0x550ce0), where base is
 *     white for an active window (+0xbc) and the pactex
 *     非アクティブ時の乗算カラー otherwise, and bright is the EX list
 *     Ｅ＿信息窗口.明るさ乗算色 (read at 0x4f1c90 with the default 0); and the
 *     background alpha
 *       trunc(alpha_rate * 255), clamped to 0..255.
 *     Both are as read in the disassembly; the truncations are cvttss2si,
 *     which gives 0x80000000 for NaN or a value out of range. The text
 *     (+0xa4) and the key wait mark (+0xa8) keep their colour and alpha;
 *     0x4f2790, which 0x4f2660 calls last, only places the three. The
 *     background is combined with the window's own colour and alpha
 *     (0x4f32c0 -> 0x535260 -> 0x579bb0); the window's children are
 *     combined with the window's, which has no rates.
 *   - 0x535260: a parts whose MessageWindowEffectLink flag (+0xae; set by
 *     SetComponentMessageWindowEffectLink, native case 107 -> 0x58cb30; the
 *     pactex 信息窗口視覚效果連動 == 1 is stored at 0x553f14) has its multiply
 *     colour times mul_rate and its alpha times alpha_rate (each truncated
 *     to an int) before they are combined with its parent's (0x579bb0), so
 *     its children have them too. IsComponentMessageWindowEffectLink is
 *     native case 108 -> 0x58cbc0. Both take the component from the
 *     get-or-create lookup (0x540250 -> 0x56d450 -> 0x53d920 -> 0x53a0b0
 *     -> 0x539c80): none for a number of 0 or less (0x539cb0).
 *     UpdateMatrix's 0x53c490 also writes the show (+0xa1), which a parts
 *     linked to the message window's show takes (0x5352de).
 *   - The message window's pactex loader (0x4f0510) reads 文本 (SJIS
 *     テキスト in the other executable) at 0x4f0d0f into the window's
 *     text string, the one SetMessageWindowText sets and
 *     GetMessageWindowText (0x595650) returns; 信息番號 defaults to -1 and
 *     the window is marked changed (+0x150), so its next update builds the
 *     text. SetMessageWindowText with the same text under the same number
 *     changes nothing (0x4f2a60).
 *
 * WR1  guard: rates 1.0 and 1.0: the background is drawn as its CG, the
 *      text as without UpdateComponent's rates (the drawing's hash is
 *      printed for the before/after record).
 * WR2  opacity 0.5: the background's alpha is 127 (truncated), its colour
 *      scaled by it; the text is drawn as at 1.0.
 * WR3  brightness 0.5: the background's colour is the CG's times 127/255,
 *      opaque; the text as at 1.0.
 * WR4  0: opacity 0 leaves nothing of the background; brightness 0 makes
 *      it black and opaque.
 * WR5  the truncation of both at 0.34 on a white background: colour and
 *      alpha 86, where rounding gives 87.
 * WR6  the last UpdateComponent decides: 0.5 then 1.0 draws as WR1.
 * WR7  guard: a parts that is no message window (a CG, a text) is not
 *      touched by the rates.
 * WR8  guard, AIN v13: after a v14 update with rates 0.5, PE_Update (no
 *      rates) under v13 draws the window as its CG.
 * WR9  windows loaded from a pactex (SJIS and GBK spellings of the type):
 *      the rates reach them as well.
 * WR10 MessageWindowEffectLink: unset on a new parts (the port's; no write
 *      of +0xae in the original's constructor was found); set, a CG parts
 *      gets the rates on its colour and alpha, and so does its child; an
 *      unlinked sibling does not; a change of only one rate is followed;
 *      a linked child of a linked parts has them twice (an alpha of
 *      127 * 127 / 255, truncated: 63), also when it is set below it
 *      afterwards; unset again, the parts does not either. A linked
 *      controller root (no parent) gives the top-level parts the rates. A
 *      number of 0 or less gets no component; Is makes one for another
 *      number.
 * WR11 UpdateMatrix gives the window the rates as UpdateComponent does, and
 *      its show hides a parts linked to the message window's show.
 * WR12 the background has the window's own alpha and colour as well:
 *      window alpha 128 and opacity 0.5 give alpha 63 (the original's
 *      integer product) or 64 (presumably the port's float one); window
 *      multiply 128 and brightness 0.5 give a quarter of the CG's colour
 *      (+-1).
 * WR13 guard: a CG parts below a message window is not touched.
 * WR14 rates out of range: 2.0 clamps to 255, -1.0 to 0; NaN, 1e10 and
 *      -1e10 give cvttss2si's 0x80000000, so 0. With an add colour on the
 *      window, brightness -1.0, NaN and -1e10 draw as brightness 0.
 * WR15 rates passed while the show is false reach a window that is not
 *      linked to the show: 0x53c490 writes them whatever the show, and
 *      0x4f2660 looks at the visible flag (+0xa0), not at the show (+0xa1).
 * WR16 the brightness leaves the background's add colour: with an add
 *      colour of 60 on the window, brightness 0.5 and 0 draw the CG's colour
 *      times the colour byte and then 60 added (the port's v14 order,
 *      multiply then add); 0x4f2660 writes the multiply colour and the
 *      alpha only.
 *
 * WS1  文本 (GBK): the window has the text (GetMessageWindowText) and lays
 *      it out, two lines, four characters.
 * WS2  テキスト (SJIS): the same.
 * WS3  guard: no text key, an empty one, or one that is no string: no
 *      text.
 * WS4  guard: 文本エリア, 文本位置 and a 文本 leaf inside the key wait block
 *      are not the window's text.
 * WS5  the same text under the window's number (-1) changes nothing (the
 *      text stays built); another number builds it again.
 * WS6  with no 字速度 the text is at full alpha after the first update.
 *
 * FN1  PE_SetFont with another size after the text was laid out: the
 *      glyphs, their advances and the lines are those of the new size (as
 *      a parts given that size before its text).
 * FN2  PE_SetPartsFontSize: the same.
 * FN3  PE_SetPartsFontType: the glyphs of the other face.
 * FN4  PE_SetMessageWindowTextFont with another size after the window laid
 *      out its text: the next layout is in the new size.
 * FN5  guard: the same font again keeps the cached size and its glyphs (no
 *      glyph is rendered again).
 * FN6  guard, AIN v13: a font given before the text is used.
 * FN7  guard, AIN v13: after a change of the size the cached size of
 *      before is kept, as it was (the port keeps engines before v14 as
 *      they are).
 *
 * Not covered: a window with アクティブ = 0 and a 非アクティブ時の乗算カラー
 * other than white (the game's five windows all have white), an EX
 * 明るさ乗算色 (the game's EX has none; the path rule of 0x442fc0 was not
 * read), the context's visible flag (a hidden window is not drawn), the
 * reentrant UpdateComponent, the key wait mark under the rates, the
 * pactex's 信息番號 and 已讀判定 (not read), and for the effect link: its
 * pactex key, a save and load, and rates outside 0 and 1 (the script's
 * rates lie within them). A message window's flat is not drawn by the
 * port (its functions are stubs), so it takes no rates.
 *
 * Synthetic data only: two fonts of the probe's own, a CG made in memory,
 * texts of ASCII letters. No game assets. Requires forked_cases.inc, the
 * construction, base_ui, layer_root, dialogue_fade, gbk_chars and
 * cg_assets fixtures.
 */
#include <dlfcn.h>
#include <math.h>

#define MC_VIEW_W 128
#define MC_VIEW_H 64
enum { MC_W = 9861, MC_W2 = 9862, MC_PLAIN = 9863, MC_TEXT = 9864, MC_LINK = 9865, MC_CHILD = 9866,
	MC_SIB = 9867, MC_A = 9868, MC_B = 9869, MC_FRESH = 9870, MC_CHILD2 = 9871 };
/* The CG of the windows' background: 24x16, one colour; and a white one. */
#define MC_BG "mcbg"
#define MC_WHITE "mcwh"
#define MC_BG_W 24
#define MC_BG_H 16
static const uint8_t mc_bg_rgb[3] = { 200, 160, 120 };

/* --- two fonts of the probe's own --- */
static struct font mc_font[2];
static struct font_size mc_sizes[2][8];
static int mc_nr_sizes[2];
static int mc_renders;

static int mc_face_of(const struct font *f) { return f == &mc_font[1]; }
static struct font_size *mc_get_size(struct font *font, float size)
{
	int k = mc_face_of(font);
	size = roundf(size);
	for (int i = 0; i < mc_nr_sizes[k]; i++)
		if (mc_sizes[k][i].size == size)
			return &mc_sizes[k][i];
	assert(mc_nr_sizes[k] < 8);
	struct font_size *fs = &mc_sizes[k][mc_nr_sizes[k]++];
	*fs = (struct font_size) { .size = size, .y_offset = 0, .font = font };
	return fs;
}
static float mc_actual_size(struct font *font, float size) { (void)font; return roundf(size); }
/* Face 0: ASCII half the size wide; face 1: three quarters of that. */
static int mc_glyph_width(int face, int size, uint32_t code)
{
	int w = code < 0x80 ? (size + 1) / 2 : size;
	return face ? (w * 3 + 3) / 4 : w;
}
static bool mc_get_glyph(struct font_size *size, struct glyph *dst, uint32_t code, enum font_weight weight)
{
	const int face = mc_face_of(size->font), s = size->size, w = mc_glyph_width(face, s, code);
	uint8_t *bm = xmalloc((size_t)w * s);
	for (int row = 0; row < s; row++)
		for (int col = 0; col < w; col++)
			bm[row * w + col] = face ? 150 + (col * 7 + row) % 50 : 255 - (col + row * 3) % 40;
	gfx_init_texture_rmap(&dst->t[weight], w, s, bm);
	free(bm);
	dst->rect = (Rectangle) { 0, 0, w, s };
	dst->advance = w;
	mc_renders++;
	return true;
}
static float mc_size_char(struct font_size *size, uint32_t code)
{
	return mc_glyph_width(mc_face_of(size->font), size->size, code);
}
static float mc_kerning(struct font_size *size, uint32_t code, uint32_t next)
{
	(void)next;
	return mc_size_char(size, code);
}

static bool mc_gbk(void)
{
	const char *e = getenv("XS4_PROBE_GBK");
	return e && !strcmp(e, "1");
}

/* --- the CGs --- */
static struct probe_cg_file mc_files[2];
static void mc_install_cg(void)
{
	uint8_t *px = malloc(MC_BG_W * MC_BG_H * 4);
	for (int i = 0; i < MC_BG_W * MC_BG_H; i++) {
		px[i * 4] = mc_bg_rgb[0];
		px[i * 4 + 1] = mc_bg_rgb[1];
		px[i * 4 + 2] = mc_bg_rgb[2];
		px[i * 4 + 3] = 255;
	}
	mc_files[0].name = MC_BG;
	mc_files[0].data = cg_png(MC_BG_W, MC_BG_H, px, true, &mc_files[0].size);
	memset(px, 255, MC_BG_W * MC_BG_H * 4);
	mc_files[1].name = MC_WHITE;
	mc_files[1].data = cg_png(MC_BG_W, MC_BG_H, px, true, &mc_files[1].size);
	free(px);
	probe_install_cg_assets(mc_files, 2);
}

/* --- setup: GL, the parts engine and its v14 forms, the script's config,
 * the two fonts, the string rule of the run, the CG --- */
static void mc_setup(void)
{
	cn_gl_setup();
	lr_setup();
	int g = -1;
	for (int i = 0; i < ain->nr_globals; i++)
		if (!strcmp(ain->globals[i].name, "g_AFConfig"))
			g = i;
	if (g < 0)
		xc_env_fail("the AIN has no g_AFConfig");
	int s = alloc_struct(ain->globals[g].type.struc);
	heap[global_page_slot].page->values[g].i = s;
	/* CASConfigData: 1 MessageSpeedRate, 14 and 15 the two rates. */
	heap[s].page->values[1].f = 1.0f;
	heap[s].page->values[14].f = 1.0f;
	heap[s].page->values[15].f = 1.0f;
	extern void audio_reset(void);
	audio_reset();
	parts_render_init();
	config.view_width = MC_VIEW_W;
	config.view_height = MC_VIEW_H;
	for (int k = 0; k < 2; k++) {
		mc_font[k] = (struct font) {
			.charmap = CHARMAP_UNICODE,
			.get_size = mc_get_size,
			.get_actual_size = mc_actual_size,
			.get_actual_size_round_down = mc_actual_size,
			.get_glyph = mc_get_glyph,
			.size_char = mc_size_char,
			.size_char_kerning = mc_kerning,
		};
		mc_nr_sizes[k] = 0;
		font_ttf[k] = &mc_font[k];
	}
	if (mc_gbk())
		gk_enable();
	else
		gk_force_sjis();
	if (gfx_text_cn_gdi() != mc_gbk())
		xc_env_fail("the glyph grid does not follow XS4_PROBE_GBK");
	mc_install_cg();
}

static uint8_t *mc_draw(int no)
{
	Texture dst = { 0 };
	gfx_init_texture_rgba(&dst, MC_VIEW_W, MC_VIEW_H, (SDL_Color){ 0, 0, 0, 0 });
	GLuint fbo = gfx_set_framebuffer(GL_DRAW_FRAMEBUFFER, &dst, 0, 0, dst.w, dst.h);
	parts_render(parts_get(no));
	gfx_reset_framebuffer(GL_DRAW_FRAMEBUFFER, fbo);
	uint8_t *px = gfx_get_pixels(&dst);
	gfx_delete_texture(&dst);
	return px;
}

static const uint8_t *mc_px(const uint8_t *px, int x, int y)
{
	return px + ((size_t)y * MC_VIEW_W + x) * 4;
}

static uint32_t mc_hash(const uint8_t *px)
{
	uint32_t h = 2166136261u;
	for (size_t i = 0; i < (size_t)MC_VIEW_W * MC_VIEW_H * 4; i++)
		h = (h ^ px[i]) * 16777619u;
	return h;
}

static void mc_update_view(void)
{
	void (*update_view)(void) = (void (*)(void))dlsym(RTLD_DEFAULT, "frame_pacing_update_view");
	if (!update_view)
		xc_env_fail("no frame_pacing_update_view");
	update_view();	/* UpdateView presents the frame, not UpdateComponent */
}

static void mc_update(float mul, float alpha)
{
	mc_update_view();
	BU_PE("UpdateComponent", BU_I(16), BU_I(16), BU_I(1), BU_F(mul), BU_F(alpha));
}

static struct string *mc_s(const char *t)
{
	return cstr_to_string(t);
}

/* A window at (4, 4): the background CG at its corner, the text "AB" in
 * an area right of it (x 40), so no glyph lies on the background. */
static void mc_window_cg(int no, const char *cg)
{
	struct string *bg = mc_s(cg);
	PE_SetMessageWindowCGName(no, bg);
	free_string(bg);
	PE_SetMessageWindowTextArea(no, 40, 0, 60, 40);
	PE_SetMessageWindowTextFont(no, 0, 16, 255, 255, 255, 0, 0, 0, 0, 0);
	PE_SetMessageWindowTextSpeed(no, 0);
	df_text(no, "AB", 1);
	PE_SetPos(no, 4, 4);
	PE_UpdateComponent(0);
	parts_message_window_update(0);
}
static void mc_window(int no) { mc_window_cg(no, MC_BG); }

/* The background region (window x 0..23, y 0..15 at view 4..27, 4..19). */
#define MC_BG_X0 4
#define MC_BG_Y0 4
/* The text region: x 44 .. 100, y 4 .. 30. */
static bool mc_same_text(const uint8_t *a, const uint8_t *b)
{
	for (int y = 4; y < 31; y++)
		for (int x = 44; x < 101; x++)
			if (memcmp(mc_px(a, x, y), mc_px(b, x, y), 4))
				return false;
	return true;
}
static int mc_text_ink(const uint8_t *px)
{
	int n = 0;
	for (int y = 4; y < 31; y++)
		for (int x = 44; x < 101; x++)
			n += mc_px(px, x, y)[3] != 0;
	return n;
}

/* Every pixel of the background region is (r, g, b, a), each within tol
 * (the alpha exactly). */
static bool mc_bg_is(const uint8_t *px, int r, int g, int b, int a, int tol, const char *what)
{
	for (int y = MC_BG_Y0; y < MC_BG_Y0 + MC_BG_H; y++) {
		for (int x = MC_BG_X0; x < MC_BG_X0 + MC_BG_W; x++) {
			const uint8_t *p = mc_px(px, x, y);
			if (abs(p[0] - r) > tol || abs(p[1] - g) > tol || abs(p[2] - b) > tol || p[3] != a) {
				printf("  %s: background (%d,%d) is (%d,%d,%d,%d), want (%d,%d,%d,%d) +-%d\n", what, x, y,
				       p[0], p[1], p[2], p[3], r, g, b, a, tol);
				return false;
			}
		}
	}
	return true;
}

/* The original's two values (0x4f2660 with base white and bright 0). */
static int mc_clamp(int v) { return v < 0 ? 0 : v > 255 ? 255 : v; }
static int mc_alpha_byte(float rate) { return mc_clamp((int)(rate * 255.0f)); }
static int mc_mul_byte(float rate)
{
	const float base = 255.0f, diff = (float)(0 - 255), f = 1.0f - rate;
	return mc_clamp((int)(diff * f + base));
}
/* What the drawing of the background holds for those two bytes, drawn
 * over transparent black (colour times multiply, times alpha). */
static int mc_drawn(int c, int mul, int alpha)
{
	return (int)lroundf((float)c * (float)mul / 255.0f * (float)alpha / 255.0f);
}
static bool mc_bg_rated(const uint8_t *px, float mul, float alpha, const char *what)
{
	int m = mc_mul_byte(mul), a = mc_alpha_byte(alpha);
	if (!a)
		return mc_bg_is(px, 0, 0, 0, 0, 0, what);
	return mc_bg_is(px, mc_drawn(mc_bg_rgb[0], m, a), mc_drawn(mc_bg_rgb[1], m, a),
			mc_drawn(mc_bg_rgb[2], m, a), a, 1, what);
}
#define MC_BG_CG(px, what) mc_bg_is(px, mc_bg_rgb[0], mc_bg_rgb[1], mc_bg_rgb[2], 255, 0, what)

/* --- WR1 --- */
static void wr1_default(void)
{
	mc_setup();
	mc_window(MC_W);
	xc_ready();
	uint8_t *before = mc_draw(MC_W);
	mc_update(1.0f, 1.0f);
	uint8_t *after = mc_draw(MC_W);
	printf("  WR1 drawing hash before %08x after %08x\n", mc_hash(before), mc_hash(after));
	XC_CHECK(MC_BG_CG(after, "WR1 rates 1.0"), "WR1: the background is not its CG at rates 1.0");
	XC_CHECK(mc_text_ink(after) > 20, "WR1: no text drawn (%d pixels)", mc_text_ink(after));
	XC_CHECK(mc_same_text(before, after), "WR1: the text differs from the drawing before UpdateComponent");
	XC_CHECK(mc_hash(before) == mc_hash(after), "WR1: the drawing changed with rates 1.0");
	free(before);
	free(after);
}

/* One rate pair against the drawing at 1.0. */
static void mc_rated_case(float mul, float alpha, const char *what)
{
	mc_setup();
	mc_window(MC_W);
	xc_ready();
	mc_update(1.0f, 1.0f);
	uint8_t *ref = mc_draw(MC_W);
	mc_update(mul, alpha);
	uint8_t *px = mc_draw(MC_W);
	XC_CHECK(mc_bg_rated(px, mul, alpha, what), "%s: the background is not drawn with multiply %d and alpha %d", what,
		 mc_mul_byte(mul), mc_alpha_byte(alpha));
	XC_CHECK(mc_text_ink(px) > 20 && mc_same_text(ref, px), "%s: the text is not drawn as at 1.0", what);
	free(ref);
	free(px);
}

static void wr2_alpha(void) { mc_rated_case(1.0f, 0.5f, "WR2 opacity 0.5"); }
static void wr3_mul(void) { mc_rated_case(0.5f, 1.0f, "WR3 brightness 0.5"); }
static void wr4_zero(void)
{
	mc_rated_case(1.0f, 0.0f, "WR4 opacity 0");
	mc_update(0.0f, 1.0f);
	uint8_t *px = mc_draw(MC_W);
	XC_CHECK(mc_bg_is(px, 0, 0, 0, 255, 0, "WR4 brightness 0"), "WR4: brightness 0 is not black and opaque");
	free(px);
}
static void wr5_truncation(void)
{
	/* 0.34: alpha 86.7 and colour 86.7, so 86 truncated, 87 rounded. */
	XC_CHECK(mc_alpha_byte(0.34f) == 86 && mc_mul_byte(0.34f) == 86, "WR5: the model gives %d and %d",
		 mc_alpha_byte(0.34f), mc_mul_byte(0.34f));
	mc_setup();
	mc_window_cg(MC_W, MC_WHITE);
	xc_ready();
	mc_update(0.34f, 1.0f);
	uint8_t *px = mc_draw(MC_W);
	XC_CHECK(mc_bg_is(px, 86, 86, 86, 255, 0, "WR5 brightness 0.34"), "WR5: brightness 0.34: the multiply is not 86");
	free(px);
	mc_update(1.0f, 0.34f);
	px = mc_draw(MC_W);
	XC_CHECK(mc_bg_is(px, 86, 86, 86, 86, 0, "WR5 opacity 0.34"), "WR5: opacity 0.34: the alpha is not 86");
	free(px);
}

/* --- WR6 --- */
static void wr6_last(void)
{
	mc_setup();
	mc_window(MC_W);
	xc_ready();
	mc_update(0.5f, 0.5f);
	uint8_t *px = mc_draw(MC_W);
	XC_CHECK(mc_bg_rated(px, 0.5f, 0.5f, "WR6 0.5"), "WR6: the background at 0.5 and 0.5");
	free(px);
	mc_update(1.0f, 1.0f);
	px = mc_draw(MC_W);
	XC_CHECK(MC_BG_CG(px, "WR6 back to 1.0"), "WR6: the background after rates 1.0 again");
	free(px);
}

/* --- WR7 --- */
static void wr7_others(void)
{
	mc_setup();
	struct string *bg = mc_s(MC_BG);
	PE_SetPartsCG(MC_PLAIN, bg, 0, 1);
	free_string(bg);
	PE_SetPos(MC_PLAIN, 4, 4);
	PE_SetFont(MC_TEXT, 0, 16, 255, 255, 255, 0, 0, 0, 0, 0, 1);
	struct string *t = mc_s("AB");
	PE_SetText(MC_TEXT, t, 1);
	free_string(t);
	PE_SetPos(MC_TEXT, 44, 4);
	PE_UpdateComponent(0);
	xc_ready();
	mc_update(1.0f, 1.0f);
	uint8_t *ref = mc_draw(MC_TEXT);
	mc_update(0.3f, 0.3f);
	uint8_t *px = mc_draw(MC_PLAIN);
	XC_CHECK(MC_BG_CG(px, "WR7 a CG parts"), "WR7: a CG parts changed under the rates");
	free(px);
	px = mc_draw(MC_TEXT);
	XC_CHECK(mc_text_ink(px) > 20 && mc_same_text(ref, px), "WR7: a text parts changed under the rates");
	free(px);
	free(ref);
}

/* --- WR8 --- */
static void wr8_v13(void)
{
	mc_setup();
	mc_window(MC_W);
	xc_ready();
	mc_update(0.5f, 0.5f);
	ain->version = 13;
	PE_Update(16, true);
	uint8_t *px = mc_draw(MC_W);
	XC_CHECK(MC_BG_CG(px, "WR8 v13"), "WR8: v13: the background is not its CG after rates of a v14 update");
	free(px);
	ain->version = 14;
}

/* --- pactex windows --- */
#define MC_K_TEXT_GBK "\xce\xc4\xb1\xbe"				/* 文本 */
#define MC_K_TEXT_SJIS "\x83\x65\x83\x4c\x83\x58\x83\x67"		/* テキスト */
#define MC_K_AREA "\xce\xc4\xb1\xbe\xa5\xa8\xa5\xea\xa5\xa2"		/* 文本エリア */
#define MC_K_ORIGIN "\xce\xc4\xb1\xbe\xce\xbb\xd6\xc3"			/* 文本位置 */
#define MC_K_SIZE "\xa5\xd5\xa5\xa9\xa5\xf3\xa5\xc8\xa5\xb5\xa5\xa4\xa5\xba" /* フォントサイズ */
#define MC_T_MSGWIN_GBK "\xd0\xc5\xcf\xa2\xb4\xb0\xbf\xda"		/* 信息窗口 */
#define MC_T_MSGWIN_SJIS "\x83\x81\x83\x62\x83\x5a\x81\x5b\x83\x57\x83\x45\x83\x42\x83\x93\x83\x68\x83\x45"

static struct ex_value mc_ilist(int n, const int *v)
{
	struct ex_list *l = xcalloc(1, sizeof(*l));
	l->nr_items = n;
	l->items = xcalloc(n, sizeof(*l->items));
	for (int i = 0; i < n; i++)
		l->items[i].value = (struct ex_value){ .type = EX_INT, .i = v[i] };
	return (struct ex_value){ .type = EX_LIST, .list = l };
}

/* One window component in the activity. kind: 0 plain, with what is set. */
struct mc_win {
	const char *name, *type;
	const char *text_key, *text;	/* NULL: no key */
	bool cg;			/* ＣＧ名 = MC_BG */
	bool nested_text;		/* a 文本 leaf inside the キー待ちマーク block */
	bool int_text;			/* the text key holds an integer */
};
static const struct mc_win *mc_wins;
static int mc_nr_wins;

static bool mc_read_activity_file(struct string *name, possibly_unused struct string *filename, possibly_unused bool edit)
{
	struct ex *e = xcalloc(1, sizeof(*e));
	e->nr_blocks = 1;
	e->blocks = xcalloc(1, sizeof(*e->blocks));
	e->blocks[0].name = cstr_to_string("McActivity");
	e->blocks[0].val.type = EX_TREE;
	struct ex_tree *top = xcalloc(1, sizeof(*top));
	*top = bu_branch("McActivity", 1);
	e->blocks[0].val.tree = top;
	top->children[0] = bu_comp("\xa5\xeb\xa9\x60\xa5\xc8\xb2\xbf\xbc\xfe", BU_LAYOUT, 0, mc_nr_wins);
	struct ex_tree *kids = &top->children[0].children[1];
	for (int i = 0; i < mc_nr_wins; i++) {
		const struct mc_win *w = &mc_wins[i];
		unsigned n = 3 + !!w->text_key + !!w->cg + !!w->nested_text;
		kids->children[i] = bu_comp(w->name, w->type, n, 0);
		struct ex_tree *info = &kids->children[i].children[0];
		unsigned k = 1;
		info->children[k++] = bu_leaf(MC_K_AREA, mc_ilist(4, (const int[]){ 40, 0, 60, 40 }));
		info->children[k++] = bu_ileaf(MC_K_ORIGIN, 1);
		info->children[k++] = bu_ileaf(MC_K_SIZE, 16);
		if (w->text_key)
			info->children[k++] = w->int_text ? bu_ileaf(w->text_key, 7) : bu_sleaf(w->text_key, w->text);
		if (w->cg)
			info->children[k++] = bu_sleaf(KW_CGNAME, MC_BG);
		if (w->nested_text) {
			info->children[k] = bu_branch(KW_BLOCK, 1);
			info->children[k++].children[0] = bu_sleaf(MC_K_TEXT_GBK, "XY");
		}
		assert(k == n + 1);
	}
	bool ok = probe_load_activity_tree(name, e);
	ex_free(e);
	return ok;
}

/* Loads the windows; their parts numbers into nos. */
static bool mc_load(const struct mc_win *wins, int n, int *nos)
{
	mc_wins = wins;
	mc_nr_wins = n;
	lr_replace_hll("PartsEngine", "ReadActivityFile", (void *)mc_read_activity_file);
	int a = sf_str("McActivity", -1), f = sf_str("McActivity", -1);
	bool ok = BU_PE("ReadActivityFile", BU_I(a), BU_I(f), BU_I(0)).i;
	for (int i = 0; i < n; i++) {
		nos[i] = BU_PE("GetActivityPartsNumber", BU_I(a), BU_I(sf_str(wins[i].name, -1))).i;
		ok = ok && parts_try_get(nos[i]);
	}
	PE_UpdateComponent(0);
	return ok;
}

/* --- WR9 --- */
static void wr9_pactex(void)
{
	mc_setup();
	static const struct mc_win wins[] = {
		{ "WinG", MC_T_MSGWIN_GBK, MC_K_TEXT_GBK, "AB", true, false },
		{ "WinS", MC_T_MSGWIN_SJIS, MC_K_TEXT_GBK, "AB", true, false },
	};
	int no[2];
	xc_ready();
	bool ok = mc_load(wins, 2, no);
	XC_CHECK(ok, "WR9: the activity did not load");
	if (!ok)
		return;
	for (int i = 0; i < 2; i++) {
		PE_SetPos(no[i], 4, 4);
		PE_UpdateComponent(0);
		mc_update(1.0f, 1.0f);
		uint8_t *px = mc_draw(no[i]);
		XC_CHECK(MC_BG_CG(px, wins[i].name), "WR9 %s: the background is not its CG at 1.0", wins[i].name);
		free(px);
		mc_update(0.5f, 0.5f);
		px = mc_draw(no[i]);
		XC_CHECK(mc_bg_rated(px, 0.5f, 0.5f, wins[i].name), "WR9 %s: the rates do not reach the window",
			 wins[i].name);
		free(px);
	}
}

/* --- WR10 --- */
static bool mc_is_linked(int no) { return BU_PE("IsComponentMessageWindowEffectLink", BU_I(no)).i != 0; }
static void mc_link(int no, bool on) { BU_PE("SetComponentMessageWindowEffectLink", BU_I(no), BU_I(on)); }
static void mc_cg_parts(int no, int x, int y)
{
	struct string *bg = mc_s(MC_BG);
	PE_SetPartsCG(no, bg, 0, 1);
	free_string(bg);
	PE_SetPos(no, x, y);
}
static void wr10_link(void)
{
	mc_setup();
	mc_cg_parts(MC_LINK, 4, 4);
	mc_cg_parts(MC_CHILD, 0, 0);
	PE_SetParentPartsNumber(MC_CHILD, MC_LINK);
	mc_cg_parts(MC_SIB, 4, 4);
	PE_UpdateComponent(0);
	xc_ready();
	XC_CHECK(!mc_is_linked(MC_LINK), "WR10: a new parts is linked");
	mc_link(MC_LINK, true);
	XC_CHECK(mc_is_linked(MC_LINK) && !mc_is_linked(MC_SIB), "WR10: linked %d, sibling %d; want 1, 0",
		 mc_is_linked(MC_LINK), mc_is_linked(MC_SIB));
	PE_UpdateComponent(0);
	mc_update(0.5f, 0.5f);
	uint8_t *px = mc_draw(MC_LINK);
	XC_CHECK(mc_bg_rated(px, 0.5f, 0.5f, "WR10 linked"), "WR10: the linked parts does not get the rates");
	free(px);
	px = mc_draw(MC_CHILD);
	XC_CHECK(mc_bg_rated(px, 0.5f, 0.5f, "WR10 child"), "WR10: the linked parts' child does not get them");
	free(px);
	px = mc_draw(MC_SIB);
	XC_CHECK(MC_BG_CG(px, "WR10 sibling"), "WR10: an unlinked parts got the rates");
	free(px);
	/* Only the opacity changes, then only the brightness. */
	mc_update(0.5f, 1.0f);
	px = mc_draw(MC_LINK);
	XC_CHECK(mc_bg_rated(px, 0.5f, 1.0f, "WR10 opacity alone"), "WR10: a change of the opacity alone is not followed");
	free(px);
	mc_update(1.0f, 1.0f);
	px = mc_draw(MC_LINK);
	XC_CHECK(MC_BG_CG(px, "WR10 brightness alone"), "WR10: a change of the brightness alone is not followed");
	free(px);
	/* A linked child of a linked parts: an alpha of 127 * 127 / 255,
	 * truncated, 63. */
	mc_link(MC_CHILD, true);
	mc_update(0.5f, 0.5f);
	px = mc_draw(MC_CHILD);
	XC_CHECK(mc_bg_is(px, mc_drawn(mc_bg_rgb[0], 63, 63), mc_drawn(mc_bg_rgb[1], 63, 63), mc_drawn(mc_bg_rgb[2], 63, 63), 63, 1,
			  "WR10 two levels"),
		 "WR10: a linked child of a linked parts does not have the rates twice");
	free(px);
	mc_link(MC_CHILD, false);
	/* A linked parts set below a linked one afterwards, combined in the
	 * update, with two rates that differ (brightness 0.25, opacity 0.5):
	 * above it alpha 127 and colour 63, so an alpha of 127 * 127 / 255 and
	 * a colour of 63 * 63 / 255, truncated, 63 and 15. */
	mc_cg_parts(MC_CHILD2, 0, 0);
	mc_link(MC_CHILD2, true);
	PE_SetParentPartsNumber(MC_CHILD2, MC_LINK);
	mc_update(0.25f, 0.5f);
	px = mc_draw(MC_CHILD2);
	XC_CHECK(mc_bg_is(px, mc_drawn(mc_bg_rgb[0], 15, 63), mc_drawn(mc_bg_rgb[1], 15, 63), mc_drawn(mc_bg_rgb[2], 15, 63), 63, 1,
			  "WR10 combined below"),
		 "WR10: a linked parts set below a linked one does not have the rates twice");
	free(px);
	mc_link(MC_LINK, false);
	PE_UpdateComponent(0);
	mc_update(0.5f, 0.5f);
	px = mc_draw(MC_LINK);
	XC_CHECK(!mc_is_linked(MC_LINK) && MC_BG_CG(px, "WR10 unlinked again"), "WR10: unlinked again, the rates stay");
	free(px);
	/* The numbers: none for 0 or less; Is makes the component of another. */
	mc_link(0, true);
	mc_link(-1, true);
	const bool linked0 = mc_is_linked(0), linked_neg = mc_is_linked(-1);
	XC_CHECK(!linked0 && !linked_neg && !PE_IsExist(0) && !PE_IsExist(-1),
		 "WR10: numbers 0 and -1: linked %d %d, exist %d %d", linked0, linked_neg, PE_IsExist(0), PE_IsExist(-1));
	mc_link(1, true);
	XC_CHECK(mc_is_linked(1), "WR10: number 1 is not linked");
	XC_CHECK(!PE_IsExist(MC_FRESH) && !mc_is_linked(MC_FRESH) && PE_IsExist(MC_FRESH),
		 "WR10: IsComponentMessageWindowEffectLink did not make the component");
	/* The root of the controller has no parent: it combines with the alpha
	 * 255 and the multiply 255 of a new update context (0x579350), and its
	 * children, the sibling among them, get the rates; when they change
	 * while it is linked, so do they. */
	struct parts *root = parts_get(MC_SIB)->parent;
	XC_CHECK(root && parts_is_controller_root(root) && !root->parent, "WR10: the sibling is not below its controller's root");
	if (!root)
		return;
	mc_link(root->no, true);
	px = mc_draw(MC_SIB);
	XC_CHECK(mc_is_linked(root->no) && mc_bg_rated(px, 0.5f, 0.5f, "WR10 root linked"), "WR10: the linked root does not give its children the rates");
	free(px);
	mc_update(1.0f, 1.0f);
	px = mc_draw(MC_SIB);
	XC_CHECK(MC_BG_CG(px, "WR10 root, rates 1.0"), "WR10: the linked root does not follow the rates");
	free(px);
	mc_link(root->no, false);
}

/* --- WR11 --- */
static void wr11_matrix(void)
{
	mc_setup();
	mc_window(MC_W);
	PE_SetPartsMessageWindowShowLink(MC_W, true);
	xc_ready();
	mc_update(1.0f, 1.0f);
	BU_PE("UpdateMatrix", BU_I(1), BU_F(1.0f), BU_F(0.5f));
	uint8_t *px = mc_draw(MC_W);
	XC_CHECK(mc_bg_rated(px, 1.0f, 0.5f, "WR11 UpdateMatrix"), "WR11: UpdateMatrix does not give the window the rates");
	free(px);
	BU_PE("UpdateMatrix", BU_I(0), BU_F(1.0f), BU_F(1.0f));
	px = mc_draw(MC_W);
	XC_CHECK(mc_bg_is(px, 0, 0, 0, 0, 0, "WR11 shown 0") && !mc_text_ink(px),
		 "WR11: UpdateMatrix(false) does not hide a parts linked to the message window's show");
	free(px);
	BU_PE("UpdateMatrix", BU_I(1), BU_F(1.0f), BU_F(1.0f));
	px = mc_draw(MC_W);
	XC_CHECK(MC_BG_CG(px, "WR11 shown again"), "WR11: UpdateMatrix(true) does not show it again");
	free(px);
}

/* --- WR12 --- */
/* Every pixel of the background with an alpha in a_lo..a_hi and the colour
 * within tol of (r, g, b). */
static bool mc_bg_near(const uint8_t *px, int r, int g, int b, int a_lo, int a_hi, int tol, const char *what)
{
	for (int y = MC_BG_Y0; y < MC_BG_Y0 + MC_BG_H; y++) {
		for (int x = MC_BG_X0; x < MC_BG_X0 + MC_BG_W; x++) {
			const uint8_t *p = mc_px(px, x, y);
			if (abs(p[0] - r) > tol || abs(p[1] - g) > tol || abs(p[2] - b) > tol || p[3] < a_lo || p[3] > a_hi) {
				printf("  %s: background (%d,%d) is (%d,%d,%d,%d), want (%d,%d,%d)+-%d and alpha %d..%d\n", what, x,
				       y, p[0], p[1], p[2], p[3], r, g, b, tol, a_lo, a_hi);
				return false;
			}
		}
	}
	return true;
}
static void wr12_window_own(void)
{
	mc_setup();
	mc_window(MC_W);
	PE_SetAlpha(MC_W, 128);
	xc_ready();
	mc_update(1.0f, 1.0f);
	uint8_t *ref = mc_draw(MC_W);
	mc_update(1.0f, 0.5f);
	uint8_t *px = mc_draw(MC_W);
	XC_CHECK(mc_bg_near(px, mc_bg_rgb[0] / 4, mc_bg_rgb[1] / 4, mc_bg_rgb[2] / 4, 63, 64, 1, "WR12 alpha 128, opacity 0.5"),
		 "WR12: the background does not have the window's alpha times the opacity");
	XC_CHECK(mc_text_ink(px) > 20 && mc_same_text(ref, px), "WR12: the text is not drawn as at 1.0");
	free(px);
	free(ref);
	PE_SetAlpha(MC_W, 255);
	PE_SetMultiplyColor(MC_W, 128, 128, 128);
	mc_update(0.5f, 1.0f);
	px = mc_draw(MC_W);
	XC_CHECK(mc_bg_near(px, mc_bg_rgb[0] / 4, mc_bg_rgb[1] / 4, mc_bg_rgb[2] / 4, 255, 255, 1, "WR12 multiply 128, brightness 0.5"),
		 "WR12: the background does not have the window's multiply colour times the brightness");
	free(px);
}

/* --- WR13 --- */
static void wr13_child(void)
{
	mc_setup();
	mc_window(MC_W);
	mc_cg_parts(MC_CHILD, 0, 0);
	PE_SetParentPartsNumber(MC_CHILD, MC_W);
	PE_UpdateComponent(0);
	xc_ready();
	mc_update(0.5f, 0.5f);
	uint8_t *px = mc_draw(MC_CHILD);
	XC_CHECK(MC_BG_CG(px, "WR13 child of a window"), "WR13: a parts below a message window got the rates");
	free(px);
}

/* --- WR14 --- */
static void wr14_range(void)
{
	mc_setup();
	mc_window(MC_W);
	xc_ready();
	/* cg: the CG's colour (multiply 255); otherwise black. alpha 0: nothing. */
	static const struct {
		float mul, alpha;
		bool cg;
		int a;
		const char *what;
	} steps[] = {
		{ 2.0f, 2.0f, true, 255, "WR14 2.0 and 2.0" },
		{ -1.0f, 1.0f, false, 255, "WR14 brightness -1.0" },
		{ 1.0f, -1.0f, false, 0, "WR14 opacity -1.0" },
		{ NAN, 1.0f, false, 255, "WR14 brightness NaN" },
		{ 1.0f, NAN, false, 0, "WR14 opacity NaN" },
		{ 1e10f, 1.0f, false, 255, "WR14 brightness 1e10" },
		{ 1.0f, 1e10f, false, 0, "WR14 opacity 1e10" },
		{ -1e10f, 1.0f, false, 255, "WR14 brightness -1e10" },
		{ 1.0f, -1e10f, false, 0, "WR14 opacity -1e10" },
	};
	for (size_t i = 0; i < sizeof steps / sizeof steps[0]; i++) {
		mc_update(steps[i].mul, steps[i].alpha);
		uint8_t *px = mc_draw(MC_W);
		bool ok = !steps[i].a ? mc_bg_is(px, 0, 0, 0, 0, 0, steps[i].what)
			: steps[i].cg ? MC_BG_CG(px, steps[i].what) : mc_bg_is(px, 0, 0, 0, 255, 0, steps[i].what);
		XC_CHECK(ok, "%s: the background is not %s", steps[i].what,
			 !steps[i].a ? "gone" : steps[i].cg ? "its CG" : "black and opaque");
		free(px);
	}
	/* With an add colour on the window, the colour byte of brightness -1.0
	 * and NaN is the 0 of brightness 0. */
	PE_SetAddColor(MC_W, 60, 60, 60);
	mc_update(0.0f, 1.0f);
	uint8_t *ref = mc_draw(MC_W);
	static const float low[] = { -1.0f, NAN, -1e10f };
	for (size_t i = 0; i < sizeof low / sizeof low[0]; i++) {
		mc_update(low[i], 1.0f);
		uint8_t *px = mc_draw(MC_W);
		bool same = true;
		for (int y = MC_BG_Y0; y < MC_BG_Y0 + MC_BG_H; y++)
			same = same && !memcmp(mc_px(px, MC_BG_X0, y), mc_px(ref, MC_BG_X0, y), MC_BG_W * 4);
		const uint8_t *a = mc_px(px, MC_BG_X0, MC_BG_Y0), *b = mc_px(ref, MC_BG_X0, MC_BG_Y0);
		XC_CHECK(same, "WR14: add colour, brightness %g: (%d,%d,%d,%d), at brightness 0 (%d,%d,%d,%d)", (double)low[i], a[0],
			 a[1], a[2], a[3], b[0], b[1], b[2], b[3]);
		free(px);
	}
	free(ref);
}

/* --- WR15 --- */
static void wr15_show_false(void)
{
	mc_setup();
	mc_window(MC_W);
	xc_ready();
	/* The window is not linked to the message window's show, so it is drawn
	 * with the show false. */
	mc_update_view();
	BU_PE("UpdateComponent", BU_I(16), BU_I(16), BU_I(0), BU_F(0.5f), BU_F(0.5f));
	uint8_t *px = mc_draw(MC_W);
	XC_CHECK(mc_bg_rated(px, 0.5f, 0.5f, "WR15 show false"),
		 "WR15: the rates passed with the show false do not reach the window");
	free(px);
}

/* --- WR16 --- */
/* The CG's colour c times the colour byte m, then the add colour (the v14
 * parts shader's order), drawn opaque over transparent black. */
static int mc_added(int c, int m, int add)
{
	return mc_clamp((int)lroundf((float)c * (float)m / 255.0f + (float)add));
}
static void wr16_add_colour(void)
{
	mc_setup();
	mc_window(MC_W);
	PE_SetAddColor(MC_W, 60, 60, 60);
	xc_ready();
	static const float rates[] = { 0.5f, 0.0f };
	for (size_t i = 0; i < sizeof rates / sizeof rates[0]; i++) {
		mc_update(rates[i], 1.0f);
		uint8_t *px = mc_draw(MC_W);
		const int m = mc_mul_byte(rates[i]);
		XC_CHECK(mc_bg_is(px, mc_added(mc_bg_rgb[0], m, 60), mc_added(mc_bg_rgb[1], m, 60),
				  mc_added(mc_bg_rgb[2], m, 60), 255, 1, rates[i] ? "WR16 brightness 0.5" : "WR16 brightness 0"),
			 "WR16: brightness %g with an add colour of 60: the background is not the CG times %d plus 60",
			 (double)rates[i], m);
		free(px);
	}
}

/* --- WS --- */
static const char *mc_window_text(int no)
{
	return bu_str(BU_PE("GetMessageWindowText", BU_I(no)).i);
}
static int mc_chars(int no, unsigned *lines)
{
	Point p;
	struct parts_text *t = parts_message_window_render_text(parts_get(no), &p);
	int n = 0;
	*lines = t ? t->nr_lines : 0;
	for (unsigned l = 0; t && l < t->nr_lines; l++)
		n += t->lines[l].nr_chars;
	return n;
}

static void ws_text(const char *key, const char *what)
{
	mc_setup();
	const struct mc_win wins[] = { { "WinT", MC_T_MSGWIN_GBK, key, "AB\nCD", false, false } };
	int no;
	xc_ready();
	bool ok = mc_load(wins, 1, &no);
	XC_CHECK(ok, "%s: the activity did not load", what);
	if (!ok)
		return;
	const char *t = mc_window_text(no);
	XC_CHECK(!strcmp(t, "AB\nCD"), "%s: GetMessageWindowText is \"%s\"", what, t);
	parts_message_window_update(0);
	unsigned lines = 0;
	int chars = mc_chars(no, &lines);
	XC_CHECK(lines == 2 && chars == 4, "%s: %u line(s), %d character(s); want 2 and 4", what, lines, chars);
}
static void ws1_gbk(void) { ws_text(MC_K_TEXT_GBK, "WS1 GBK key"); }
static void ws2_sjis(void) { ws_text(MC_K_TEXT_SJIS, "WS2 SJIS key"); }

static void ws_none(const struct mc_win *wins, int n, const char *what)
{
	int no[4];
	xc_ready();
	bool ok = mc_load(wins, n, no);
	XC_CHECK(ok, "%s: the activity did not load", what);
	if (!ok)
		return;
	parts_message_window_update(0);
	for (int i = 0; i < n; i++) {
		unsigned lines = 0;
		int chars = mc_chars(no[i], &lines);
		const char *t = mc_window_text(no[i]);
		XC_CHECK(!t[0] && !chars, "%s %s: text \"%s\", %d character(s)", what, wins[i].name, t, chars);
	}
}
static void ws3_absent(void)
{
	mc_setup();
	static const struct mc_win wins[] = {
		{ "WinN", MC_T_MSGWIN_GBK, NULL, NULL, false, false },
		{ "WinE", MC_T_MSGWIN_GBK, MC_K_TEXT_GBK, "", false, false },
		{ "WinI", MC_T_MSGWIN_GBK, MC_K_TEXT_GBK, NULL, false, false, true },
	};
	ws_none(wins, 3, "WS3");
}
static void ws4_other_keys(void)
{
	mc_setup();
	static const struct mc_win wins[] = { { "WinK", MC_T_MSGWIN_GBK, NULL, NULL, false, true } };
	ws_none(wins, 1, "WS4");
}

static float mc_glyph_alpha(int no, int i)
{
	float (*fn)(struct parts *, struct parts_text *, int) = dlsym(RTLD_DEFAULT, "parts_message_window_glyph_alpha");
	if (!fn)
		xc_env_fail("no parts_message_window_glyph_alpha");
	Point p;
	struct parts_text *t = parts_message_window_render_text(parts_get(no), &p);
	return t ? fn(parts_get(no), t, i) : -1;
}

static void ws5_same(void)
{
	mc_setup();
	const struct mc_win wins[] = { { "WinT", MC_T_MSGWIN_GBK, MC_K_TEXT_GBK, "AB\nCD", false, false } };
	int no;
	xc_ready();
	bool ok = mc_load(wins, 1, &no);
	XC_CHECK(ok, "WS5: the activity did not load");
	if (!ok)
		return;
	parts_message_window_update(0);
	unsigned lines = 0;
	XC_CHECK(mc_chars(no, &lines) == 4 && mc_glyph_alpha(no, 3) == 1.0f, "WS5: the pactex text is not shown");
	BU_PE("SetMessageWindowText", BU_I(no), BU_I(sf_str("AB\nCD", -1)), BU_I(-1), BU_I(sf_str("", -1)), BU_I(0),
	      BU_I(0));
	XC_CHECK(mc_chars(no, &lines) == 4 && mc_glyph_alpha(no, 3) == 1.0f && df_fixed(no),
		 "WS5: the same text under -1 changed the window (alpha %g, fixed %d)", mc_glyph_alpha(no, 3), df_fixed(no));
	BU_PE("SetMessageWindowText", BU_I(no), BU_I(sf_str("AB\nCD", -1)), BU_I(5), BU_I(sf_str("", -1)), BU_I(0),
	      BU_I(0));
	XC_CHECK(!df_fixed(no), "WS5: another number did not build the text again");
}

static void ws6_speed(void)
{
	mc_setup();
	const struct mc_win wins[] = { { "WinT", MC_T_MSGWIN_GBK, MC_K_TEXT_GBK, "AB", false, false } };
	int no;
	xc_ready();
	bool ok = mc_load(wins, 1, &no);
	XC_CHECK(ok, "WS6: the activity did not load");
	if (!ok)
		return;
	parts_message_window_update(0);
	unsigned lines = 0;
	int chars = mc_chars(no, &lines);
	XC_CHECK(lines == 1 && chars == 2, "WS6: %u line(s), %d character(s); want 1 and 2", lines, chars);
	XC_CHECK(chars == 2 && mc_glyph_alpha(no, 0) == 1.0f && mc_glyph_alpha(no, 1) == 1.0f && df_fixed(no),
		 "WS6: after the first update %d character(s), alpha %g, %g, fixed %d; want 2, 1, 1, 1", chars,
		 mc_glyph_alpha(no, 0), mc_glyph_alpha(no, 1), df_fixed(no));
}

/* --- FN --- */
/* What the glyphs of a text are: per character its texture's size, its
 * advance, offset and pixels; per line its height and width. */
static void mc_sig(const struct parts_text *t, char *out, size_t size)
{
	size_t used = snprintf(out, size, "lines=%u", t ? t->nr_lines : 0);
	for (unsigned l = 0; t && l < t->nr_lines && used < size; l++) {
		const struct parts_text_line *line = &t->lines[l];
		used += snprintf(out + used, size - used, " [h=%u w=%g", line->height, line->width);
		for (int c = 0; c < line->nr_chars && used < size; c++) {
			const struct parts_text_char *ch = &line->chars[c];
			uint32_t h = 0;
			if (ch->t.handle && ch->t.w > 0 && ch->t.h > 0) {
				uint8_t *px = gfx_get_pixels((Texture *)&ch->t);
				h = 2166136261u;
				for (int i = 0; i < ch->t.w * ch->t.h * 4; i++)
					h = (h ^ px[i]) * 16777619u;
				free(px);
			}
			used += snprintf(out + used, size - used, " %dx%d a=%g o=%d,%d #%08x", ch->t.w, ch->t.h,
					 ch->advance, ch->off.x, ch->off.y, h);
		}
		used += snprintf(out + used, size - used, "]");
	}
}
static struct parts_text *mc_parts_text(int no)
{
	struct parts *p = parts_get(no);
	return p->states[0].type == PARTS_TEXT ? &p->states[0].text : NULL;
}
static void mc_text_parts(int no, int face, int size, const char *s)
{
	PE_SetFont(no, face, size, 255, 255, 255, 0, 0, 0, 0, 0, 1);
	struct string *t = mc_s(s);
	PE_SetText(no, t, 1);
	free_string(t);
}
static void mc_expect_same(int a, int b, const char *what)
{
	char sa[2048], sb[2048];
	mc_sig(mc_parts_text(a), sa, sizeof sa);
	mc_sig(mc_parts_text(b), sb, sizeof sb);
	XC_CHECK(!strcmp(sa, sb), "%s:\n    changed: %s\n    fresh:   %s", what, sa, sb);
}

static void fn1_set_font(void)
{
	mc_setup();
	xc_ready();
	mc_text_parts(MC_A, 0, 20, "AB\nC");
	mc_text_parts(MC_B, 0, 40, "AB\nC");
	PE_SetFont(MC_A, 0, 40, 255, 255, 255, 0, 0, 0, 0, 0, 1);
	mc_expect_same(MC_A, MC_B, "FN1 SetFont 20 -> 40");
}
static void fn2_font_size(void)
{
	mc_setup();
	xc_ready();
	mc_text_parts(MC_A, 0, 20, "AB\nC");
	mc_text_parts(MC_B, 0, 34, "AB\nC");
	PE_SetPartsFontSize(MC_A, 34, 1);
	mc_expect_same(MC_A, MC_B, "FN2 SetPartsFontSize 20 -> 34");
}
static void fn3_font_type(void)
{
	mc_setup();
	xc_ready();
	mc_text_parts(MC_A, 0, 24, "AB");
	mc_text_parts(MC_B, 1, 24, "AB");
	PE_SetPartsFontType(MC_A, 1, 1);
	mc_expect_same(MC_A, MC_B, "FN3 SetPartsFontType 0 -> 1");
}
static void fn4_window(void)
{
	mc_setup();
	xc_ready();
	int nos[2] = { MC_W, MC_W2 }, sizes[2] = { 20, 40 };
	for (int i = 0; i < 2; i++) {
		PE_SetMessageWindowTextFont(nos[i], 0, sizes[i], 255, 255, 255, 0, 0, 0, 0, 0);
		PE_SetMessageWindowTextSpeed(nos[i], 0);
		df_text(nos[i], "AB", 1);
		parts_message_window_update(0);
	}
	unsigned lines;
	mc_chars(MC_W, &lines);
	PE_SetMessageWindowTextFont(MC_W, 0, 40, 255, 255, 255, 0, 0, 0, 0, 0);
	parts_message_window_update(0);
	char sa[2048], sb[2048];
	Point p;
	mc_sig(parts_message_window_render_text(parts_get(MC_W), &p), sa, sizeof sa);
	mc_sig(parts_message_window_render_text(parts_get(MC_W2), &p), sb, sizeof sb);
	XC_CHECK(!strcmp(sa, sb) && strcmp(sa, "lines=0"), "FN4 SetMessageWindowTextFont 20 -> 40:\n    changed: %s\n    fresh:   %s",
		 sa, sb);
}
static void fn5_same_font(void)
{
	mc_setup();
	xc_ready();
	mc_text_parts(MC_A, 0, 24, "AB");
	struct parts_text *t = mc_parts_text(MC_A);
	struct font_size *fs = t ? t->ts.font_size : NULL;
	int renders = mc_renders;
	PE_SetFont(MC_A, 0, 24, 255, 255, 255, 0, 0, 0, 0, 0, 1);
	PE_SetPartsFontSize(MC_A, 24, 1);
	PE_SetPartsFontType(MC_A, 0, 1);
	t = mc_parts_text(MC_A);
	XC_CHECK(fs && t && t->ts.font_size == fs && mc_renders == renders,
		 "FN5: the same font: size %p -> %p, %d glyph(s) rendered again", (void *)fs, t ? (void *)t->ts.font_size : NULL,
		 mc_renders - renders);
	mc_text_parts(MC_B, 0, 24, "AB");
	mc_expect_same(MC_A, MC_B, "FN5 the same font");
}
static void fn6_v13(void)
{
	mc_setup();
	xc_ready();
	ain->version = 13;
	PE_SetFont(MC_A, 0, 20, 255, 255, 255, 0, 0, 0, 0, 0, 1);
	PE_SetFont(MC_A, 0, 40, 255, 255, 255, 0, 0, 0, 0, 0, 1);
	struct string *t = mc_s("AB");
	PE_SetText(MC_A, t, 1);
	free_string(t);
	mc_text_parts(MC_B, 0, 40, "AB");
	mc_expect_same(MC_A, MC_B, "FN6 v13 a font before the text");
	struct parts_text *pt = mc_parts_text(MC_A);
	XC_CHECK(pt && pt->ts.font_size && pt->ts.font_size->size == 40, "FN6: v13: the text's size is not 40");
	ain->version = 14;
}
static void fn7_v13_kept(void)
{
	mc_setup();
	xc_ready();
	ain->version = 13;
	mc_text_parts(MC_A, 0, 20, "AB");
	PE_SetFont(MC_A, 0, 40, 255, 255, 255, 0, 0, 0, 0, 0, 1);
	struct parts_text *pt = mc_parts_text(MC_A);
	XC_CHECK(pt && pt->ts.font_size && pt->ts.font_size->size == 20, "FN7: v13: the cached size is %g, not the 20 of before",
		 pt && pt->ts.font_size ? (double)pt->ts.font_size->size : -1.0);
	ain->version = 14;
}

static int test_msgwin_rates(void)
{
	static const struct xc_case cases[] = {
		{ "WR1", wr1_default }, { "WR2", wr2_alpha }, { "WR3", wr3_mul }, { "WR4", wr4_zero },
		{ "WR5", wr5_truncation }, { "WR6", wr6_last }, { "WR7", wr7_others }, { "WR8", wr8_v13 },
		{ "WR9", wr9_pactex }, { "WR10", wr10_link }, { "WR11", wr11_matrix }, { "WR12", wr12_window_own },
		{ "WR13", wr13_child }, { "WR14", wr14_range }, { "WR15", wr15_show_false },
		{ "WR16", wr16_add_colour },
	};
	return xc_run("MSGWIN-RATES", cases, sizeof cases / sizeof cases[0], 60);
}
static int test_msgwin_sample(void)
{
	static const struct xc_case cases[] = {
		{ "WS1", ws1_gbk }, { "WS2", ws2_sjis }, { "WS3", ws3_absent }, { "WS4", ws4_other_keys },
		{ "WS5", ws5_same }, { "WS6", ws6_speed },
	};
	return xc_run("MSGWIN-SAMPLE", cases, sizeof cases / sizeof cases[0], 60);
}
static int test_font_cache(void)
{
	static const struct xc_case cases[] = {
		{ "FN1", fn1_set_font }, { "FN2", fn2_font_size }, { "FN3", fn3_font_type }, { "FN4", fn4_window },
		{ "FN5", fn5_same_font }, { "FN6", fn6_v13 }, { "FN7", fn7_v13_kept },
	};
	return xc_run("FONT-CACHE", cases, sizeof cases / sizeof cases[0], 60);
}
