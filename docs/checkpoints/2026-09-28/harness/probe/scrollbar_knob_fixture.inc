/* scrollbar-knob: the knob and the track of a v14 scroll bar (豎滾動條,
 * EPartsType 2; 橫滾動條, 3), as the original engine draws and moves them
 * (static disassembly only):
 *   - the knob is the CG 覆蓋バーＣＧ名 (or ＣＧ名 with ／バー) with ／普通,
 *     ／オン or ／ダウン, the track the CG 覆蓋背景ＣＧ名 (or ＣＧ名 with ／背
 *     景); nothing without a ＣＧ名 (0x542a00, 0x542b40, 0x5748a0);
 *   - the knob is as long as its CG along the bar (0x573e30 -> 0x4e0870) and
 *     lies at the track's corner plus max(0, trunc(((float)(長さ − 前 − 次) −
 *     (float)length) × rate)) along the bar (0x573bf0); the parts' origin
 *     mode moves both by its size (長さ along the bar, 幅 across), halved
 *     towards 0; no knob while nothing scrolls (0x5738e0);
 *   - a press of the left button on the knob (event 15, 0x572c30) holds it
 *     down (／ダウン) and keeps the cursor along the bar and the rate; a press
 *     on the track first puts the knob's centre on the cursor: rate =
 *     ((float)cursor − track's corner − (float)trunc(length / 2)) /
 *     (float)(長さ − 前 − 次 − length) (0x572d90); every update the button is
 *     held (16, 0x572e00, also at the press) the rate is the kept rate plus
 *     (float)(cursor − kept cursor) / (float)(長さ − length − 次 − 前),
 *     wherever the cursor is (the key events go to the focus, which only a
 *     press changes); the release (18) lets the knob go; nothing of it while
 *     nothing scrolls; the rate is kept within 0 and 1 (0x542400);
 *   - the knob shows ／オン while the cursor is on it and the button is not
 *     held down on the bar, ／普通 else (0x577b90, 0x577cf0);
 *   - the input update comes before the widgets' update (0x53c5b0), so the
 *     Scroll event (message 21) of a move goes out in the same update.
 * Before the fix nothing of the knob or the track was drawn and nothing
 * moved it.
 *
 * Here the knob and the track are drawn by the bar's parts (parts_render)
 * and found by its input. The CGs are PNG files made in memory: the knob's
 * are opaque with a red of their own per state and a gradient, the track's
 * are transparent but for their first and last row or column. The views are
 * drawn offscreen and read back.
 *
 * Cases (each in its own fork):
 *   K1 a 橫滾動條 loaded at 1.0: the knob at the track's corner plus
 *      trunc(359 × rate) for 1, 0, 0.5, 0.8, 0.999, 7 and −1; the track's
 *      ends; a bar that scrolls nothing has no knob; a bar without ＣＧ名
 *      shows nothing.
 *   K2 a 豎滾動條 as the backlog's (614 x 26, 500 by 16): the knob at 24 +
 *      trunc(514 × pos / 484) for four positions.
 *   K3 origin mode 5: the knob and the track move by (−195, −16).
 *   K4 a press on the track: the rate, the position, ／ダウン while held,
 *      Scroll in the same update and the whole-screen click at the release
 *      alone; ／オン after the release (the knob came under the cursor);
 *      presses at the two ends and beside the bar.
 *   K5 a drag of the knob: no move at the press; the rate follows the cursor
 *      from the press, also off the bar; ／ダウン until the release, ／オン
 *      after it on the knob, ／普通 off it; nothing moves it after the
 *      release.
 *   K6 the backlog's bar: a press on its track and a drag, each with its
 *      Scroll (position, 484).
 *   K7 the settings' loop: the rate written before every update and read
 *      after it follows a press and a drag and is kept.
 *   K8 a bar that scrolls nothing does not move; a clickable parts in front
 *      of the knob takes the press.
 *   K9 an engine before v14: nothing is drawn and nothing moves.
 *   K10 a pactex with SJIS spellings of the keys: nothing is drawn and
 *      nothing moves (the SJIS keys are not known).
 *   K11 a bar released while dragged, a bar made another widget and back: no
 *      knob, no event, no crash.
 * Functions the fix adds are not called: the file builds before the fix.
 */
static int sk_failures;
#define SK_CHECK(c, ...) do { if (!(c)) { printf("  FAIL: "); printf(__VA_ARGS__); printf("\n"); sk_failures++; } } while (0)

#define SK_K_LENGTH "\xe9\x4c\xa4\xb5"							/* 長さ */
#define SK_K_WIDTH "\xb7\xf9"								/* 幅 */
#define SK_K_PREV "\xc7\xb0\xa5\xb5\xa5\xa4\xa5\xba"					/* 前サイズ */
#define SK_K_NEXT "\xb4\xce\xa5\xb5\xa5\xa4\xa5\xba"					/* 次サイズ */
#define SK_K_MOVE "\xb0\xb4\xe2\x6f\xfc\x63\x93\xf4\xd2\xc6\x84\xd3\xc1\xbf"		/* 按鈕點擊移動量 */
#define SK_K_OVER_BAR "\xb8\xb2\xc9\x77\xa5\xd0\xa9\x60" CW_K_CG			/* 覆蓋バーＣＧ名 */
#define SK_S_BAR "\xa3\xaf\xa5\xd0\xa9\x60"						/* ／バー */
#define SK_S_BG "\xa3\xaf\xb1\xb3\xbe\xb0"						/* ／背景 */
/* SJIS spellings (a guess at what an SJIS pactex would have) */
#define SK_K_CG_SJIS "\x82\x62\x82\x66\x96\xbc"						/* ＣＧ名 */
#define SK_K_LENGTH_SJIS "\x92\xb7\x82\xb3"						/* 長さ */
#define SK_K_WIDTH_SJIS "\x95\x9d"							/* 幅 */

/* --- the CGs --- */
enum {
	SK_KN, SK_KON, SK_KDOWN,	/* skknob, 32 x 32: the horizontal knob */
	SK_HBG,				/* skbar／背景, 391 x 32 */
	SK_VN, SK_VON, SK_VDOWN,	/* skvbar／バー, 26 x 100 */
	SK_VBG,				/* skvbar／背景, 26 x 601 */
	SK_NR_CG
};
static struct {
	const char *name;
	int w, h, red;
	bool track;
	uint8_t *px;
} sk_cg[SK_NR_CG] = {
	[SK_KN] = { "skknob" CW_S_N, 32, 32, 200 },
	[SK_KON] = { "skknob" CW_S_ON, 32, 32, 150 },
	[SK_KDOWN] = { "skknob" CW_S_DOWN, 32, 32, 100 },
	[SK_HBG] = { "skbar" SK_S_BG, 391, 32, 5, true },
	[SK_VN] = { "skvbar" SK_S_BAR CW_S_N, 26, 100, 210 },
	[SK_VON] = { "skvbar" SK_S_BAR CW_S_ON, 26, 100, 160 },
	[SK_VDOWN] = { "skvbar" SK_S_BAR CW_S_DOWN, 26, 100, 110 },
	[SK_VBG] = { "skvbar" SK_S_BG, 26, 601, 6, true },
};
static struct probe_cg_file sk_files[SK_NR_CG];

static void sk_install_cgs(void)
{
	for (int i = 0; i < SK_NR_CG; i++) {
		const int w = sk_cg[i].w, h = sk_cg[i].h;
		uint8_t *px = sk_cg[i].px = calloc((size_t)w * h, 4);
		for (int y = 0; y < h; y++) {
			for (int x = 0; x < w; x++) {
				uint8_t *p = px + ((size_t)y * w + x) * 4;
				if (sk_cg[i].track) {
					/* the ends are marked: a horizontal track's first and
					 * last column, a vertical one's first and last row */
					bool end = w > h ? (x == 0 || x == w - 1) : (y == 0 || y == h - 1);
					if (end) {
						p[0] = sk_cg[i].red; p[1] = 5; p[2] = 250; p[3] = 255;
					}
					continue;
				}
				p[0] = sk_cg[i].red;
				p[1] = (x * 7) & 0xff;
				p[2] = (y * 2) & 0xff;
				p[3] = 255;
			}
		}
		sk_files[i].name = sk_cg[i].name;
		sk_files[i].data = cg_png(w, h, px, true, &sk_files[i].size);
	}
	probe_install_cg_assets(sk_files, SK_NR_CG);
}

/* --- the view, drawn offscreen --- */
#define SK_VIEW_W 512
#define SK_VIEW_H 700

static uint8_t *sk_draw(int no)
{
	struct parts *p = parts_try_get(no);
	if (!p)
		return NULL;
	Texture dst = { 0 };
	gfx_init_texture_rgba(&dst, SK_VIEW_W, SK_VIEW_H, (SDL_Color){ 0, 0, 0, 0 });
	GLuint fbo = gfx_set_framebuffer(GL_DRAW_FRAMEBUFFER, &dst, 0, 0, dst.w, dst.h);
	parts_render(p);
	gfx_reset_framebuffer(GL_DRAW_FRAMEBUFFER, fbo);
	uint8_t *px = gfx_get_pixels(&dst);
	gfx_delete_texture(&dst);
	return px;
}

/* cw_setup, the parts shader and one parts that is no bar drawn with it,
 * before the setup is reported (as text-size's TS10): a shader that does not
 * build, or a draw that comes back empty, is the environment's failure. */
static void sk_setup(void)
{
	int fd = cn_ready_fd;
	cn_ready_fd = -1;
	cw_setup(true);
	sk_install_cgs();
	parts_render_init();
	config.view_width = SK_VIEW_W;
	config.view_height = SK_VIEW_H;
	const int C = 9898;
	struct parts *check = parts_get(C);
	struct parts_cg *cg = parts_get_cg(check, 0);
	gfx_init_texture_rgba(&cg->common.texture, 8, 8, (SDL_Color){ 255, 255, 255, 255 });
	parts_set_dims(check, &cg->common, 8, 8);
	PE_SetPos(C, 4, 4);
	PE_UpdateComponent(0);
	uint8_t *px = sk_draw(C);
	int any = 0;
	for (int i = 0; px && i < SK_VIEW_W * SK_VIEW_H; i++)
		any += px[i * 4 + 3] != 0;
	free(px);
	if (!any)
		cn_env_fail("parts_render drew nothing of a parts with a filled texture");
	PE_SetShow(C, false);
	cn_ready_fd = fd;
	if (cn_ready_fd >= 0 && write(cn_ready_fd, "r", 1) != 1)
		cn_env_fail("cannot report the setup to the parent");
}

/* The box of the pixels of one CG of the knob (opaque, that red, not the
 * track's blue); the number of them. */
struct sk_box { int n, x0, y0, x1, y1; };
static struct sk_box sk_find(const uint8_t *px, int red)
{
	struct sk_box b = { 0, INT_MAX, INT_MAX, -1, -1 };
	for (int y = 0; px && y < SK_VIEW_H; y++) {
		for (int x = 0; x < SK_VIEW_W; x++) {
			const uint8_t *p = px + ((size_t)y * SK_VIEW_W + x) * 4;
			if (p[3] != 255 || p[0] != red || p[2] == 250)
				continue;
			b.n++;
			b.x0 = x < b.x0 ? x : b.x0;
			b.y0 = y < b.y0 ? y : b.y0;
			b.x1 = x > b.x1 ? x : b.x1;
			b.y1 = y > b.y1 ? y : b.y1;
		}
	}
	return b;
}

/* The knob of that CG is drawn whole at (x, y), and nothing of the other
 * two states is. */
static void sk_expect_knob(int no, int cg, int x, int y, const char *what)
{
	uint8_t *px = sk_draw(no);
	const int w = sk_cg[cg].w, h = sk_cg[cg].h;
	struct sk_box b = sk_find(px, sk_cg[cg].red);
	SK_CHECK(b.n == w * h && b.x0 == x && b.y0 == y && b.x1 == x + w - 1 && b.y1 == y + h - 1,
		 "%s: %d pixels of CG %d at (%d,%d)-(%d,%d); want %d at (%d,%d)-(%d,%d)", what, b.n, cg,
		 b.x0, b.y0, b.x1, b.y1, w * h, x, y, x + w - 1, y + h - 1);
	if (px && b.n == w * h) {
		/* the gradient: the CG is not mirrored or stretched */
		const uint8_t *p = px + ((size_t)(y + h - 1) * SK_VIEW_W + x + w - 1) * 4;
		SK_CHECK(p[1] == (((w - 1) * 7) & 0xff) && p[2] == (((h - 1) * 2) & 0xff),
			 "%s: the knob's far corner is (%d,%d,%d,%d)", what, p[0], p[1], p[2], p[3]);
	}
	const int first = cg >= SK_VN ? SK_VN : SK_KN;
	for (int s = first; s < first + 3; s++) {
		if (s == cg)
			continue;
		struct sk_box o = sk_find(px, sk_cg[s].red);
		SK_CHECK(o.n == 0, "%s: %d pixels of CG %d", what, o.n, s);
	}
	free(px);
}

/* Nothing of the knob's three CGs. */
static void sk_expect_no_knob(int no, bool vertical, const char *what)
{
	uint8_t *px = sk_draw(no);
	const int first = vertical ? SK_VN : SK_KN;
	for (int s = first; s < first + 3; s++) {
		struct sk_box o = sk_find(px, sk_cg[s].red);
		SK_CHECK(o.n == 0, "%s: %d pixels of CG %d at (%d,%d)", what, o.n, s, o.x0, o.y0);
	}
	free(px);
}

/* A pixel of the track's marked ends, or not. */
static void sk_expect_track_px(int no, int x, int y, bool marked, const char *what)
{
	uint8_t *px = sk_draw(no);
	const uint8_t *p = px ? px + ((size_t)y * SK_VIEW_W + x) * 4 : NULL;
	bool got = p && p[3] == 255 && p[2] == 250;
	SK_CHECK(got == marked, "%s: (%d,%d) is (%d,%d,%d,%d); want %s", what, x, y, p ? p[0] : -1, p ? p[1] : -1,
		 p ? p[2] : -1, p ? p[3] : -1, marked ? "the track's end" : "no track");
	free(px);
}

/* --- the pactex --- */
struct sk_bar {
	const char *type;	/* CW_HSCROLL or BT_VSCROLL */
	int x, y, om;
	int length, width;
	int total, view, pos;
	float rate;
	const char *cg, *over_bar;	/* NULL: no key */
	bool sjis;		/* the SJIS spellings of ＣＧ名, 長さ and 幅 */
};
static struct ex_tree sk_bar_node(const char *name, const struct sk_bar *b)
{
	struct lb_place pl = { b->x, b->y, b->om ? b->om : 1 };
	struct ex_tree info = bu_branch(BU_INFO, 12);
	unsigned k = 0;
	info.children[k++] = bu_sleaf(BU_TYPE, b->type);
	info.children[k++] = bu_ileaf(BT_TOTAL, b->total);
	info.children[k++] = bu_ileaf(BT_VIEW, b->view);
	info.children[k++] = bu_ileaf(BT_POS, b->pos);
	info.children[k++] = bu_leaf(BT_RATE, (struct ex_value){ .type = EX_FLOAT, .f = b->rate });
	info.children[k++] = bu_ileaf(b->sjis ? SK_K_LENGTH_SJIS : SK_K_LENGTH, b->length);
	info.children[k++] = bu_ileaf(b->sjis ? SK_K_WIDTH_SJIS : SK_K_WIDTH, b->width);
	info.children[k++] = bu_ileaf(SK_K_PREV, 0);
	info.children[k++] = bu_ileaf(SK_K_NEXT, 0);
	info.children[k++] = bu_ileaf(SK_K_MOVE, 10);
	if (b->cg)
		info.children[k++] = bu_sleaf(b->sjis ? SK_K_CG_SJIS : CW_K_CG, b->cg);
	if (b->over_bar)
		info.children[k++] = bu_sleaf(SK_K_OVER_BAR, b->over_bar);
	info.nr_children = k;
	return lb_node(name, &pl, info, 0);
}

/* The settings' slider (391 x 32 at (20, 30), loaded at 1.0) and the
 * backlog's bar (614 x 26 at (450, 40), 500 by 16). */
static const struct sk_bar sk_hbar = { CW_HSCROLL, 20, 30, 1, 391, 32, 100, 1, 99, 1.0f, "skbar", "skknob" };
static const struct sk_bar sk_vbar = { BT_VSCROLL, 450, 40, 1, 614, 26, 500, 16, 0, 0.0f, "skvbar", NULL };

/* --- the model --- */
static int sk_trunc(float f)
{
	return (int)f;
}
static float sk_clamp(float r)
{
	if (!(r < 1.0f))
		return 1.0f;
	return r > 0.0f ? r : 0.0f;
}
/* The knob's offset along the bar (0x573bf0). */
static int sk_off(int track, int length, float rate)
{
	int off = sk_trunc(((float)track - (float)length) * rate);
	return off > 0 ? off : 0;
}

/* --- the input --- */
/* One update of the parts with the cursor at (x, y) and the left button
 * down or up, within the input scope the case began. */
static void sk_frame(int x, int y, bool down)
{
	extern bool key_state[];
	mouse_set_pos(x, y);
	key_state[VK_LBUTTON] = down;
	PE_Update(16, true);
}
static void sk_begin(int x, int y)
{
	extern bool key_state[];
	key_state[VK_LBUTTON] = false;
	mouse_set_pos(x, y);
	PE_BeginInput();
	sk_frame(x, y, false);
}

/* The queued messages, taken off the queue: (type, parts, var0, var1). */
struct sk_msg { int type, no, v0, v1; };
static int sk_drain(struct sk_msg *m, int max)
{
	int (*var)(int) = (int (*)(int))br_pe_fn("GetMessageVariableInt");
	int n = 0;
	for (int t; (t = br_msg_type()) >= 0; br_msg_pop()) {
		if (n < max)
			m[n] = (struct sk_msg){ t, br_msg_parts(), var(0), t == 21 ? var(1) : 0 };
		n++;
	}
	return n;
}
/* Exactly these messages, as (type, parts, var0, var1) quadruples (var1 is
 * compared for Scroll only). */
static void sk_expect_messages(const char *what, int n, const int (*want)[4])
{
	struct sk_msg m[8];
	int got = sk_drain(m, 8);
	bool ok = got == n;
	for (int i = 0; ok && i < n; i++)
		ok = m[i].type == want[i][0] && m[i].no == want[i][1] && m[i].v0 == want[i][2]
			&& (want[i][0] != 21 || m[i].v1 == want[i][3]);
	if (ok)
		return;
	printf("  FAIL: %s: %d message(s):", what, got);
	for (int i = 0; i < got && i < 8; i++)
		printf(" (%d, %d, %d, %d)", m[i].type, m[i].no, m[i].v0, m[i].v1);
	printf("; want %d:", n);
	for (int i = 0; i < n; i++)
		printf(" (%d, %d, %d, %d)", want[i][0], want[i][1], want[i][2], want[i][3]);
	printf("\n");
	sk_failures++;
}
#define SK_MESSAGES(what, ...) sk_expect_messages(what, sizeof((int[][4]){ __VA_ARGS__ }) / sizeof(int[4]), \
		(const int[][4]){ __VA_ARGS__ })
#define SK_QUIET(what) sk_expect_messages(what, 0, NULL)

static int sk_vpos(int no) { return BU_PE("GetVScrollbarScrollPos", BU_I(no)).i; }
static void sk_set_vpos(int no, int v) { BU_PE("SetVScrollbarScrollPos", BU_I(no), BU_I(v)); }

static int sk_load(const char *act_name, int n, const char **names, const struct sk_bar *const *bars, int *nos)
{
	struct ex_tree *kids;
	struct ex *e = lb_activity(act_name, n, &kids);
	for (int i = 0; i < n; i++)
		kids->children[i] = sk_bar_node(names[i], bars[i]);
	int act = cw_load(act_name, e);
	bool ok = true;
	for (int i = 0; i < n; i++) {
		nos[i] = cw_no(act, names[i]);
		ok = ok && nos[i];
	}
	heap_unref(act);
	return ok;
}

/* --- K1 --- */
static void sk_hdraw(void)
{
	sk_setup();
	struct sk_bar full = sk_hbar, bare = sk_hbar;
	full.y = 200; full.total = 1; full.view = 1; full.pos = 0; full.rate = 0.0f;
	bare.y = 300; bare.cg = NULL;
	const char *names[] = { "Slider", "Full", "Bare" };
	const struct sk_bar *bars[] = { &sk_hbar, &full, &bare };
	int no[3];
	if (!sk_load("ScrollbarKnobH", 3, names, bars, no))
		return;
	const int h = no[0];
	PE_UpdateComponent(0);
	sk_expect_knob(h, SK_KN, 20 + 359, 30, "K1 loaded at 1.0");
	static const float rates[] = { 0.0f, 0.5f, 0.8f, 0.999f, 7.0f, -1.0f, 1.0f };
	for (unsigned i = 0; i < sizeof rates / sizeof rates[0]; i++) {
		cw_set_rate(h, rates[i]);
		char what[64];
		snprintf(what, sizeof what, "K1 rate %g", rates[i]);
		sk_expect_knob(h, SK_KN, 20 + sk_off(391, 32, sk_clamp(rates[i])), 30, what);
	}
	/* the track's two ends, where the knob is not */
	cw_set_rate(h, 0.5f);
	sk_expect_track_px(h, 20, 30, true, "K1 the track's first column");
	sk_expect_track_px(h, 20, 61, true, "K1 the track's first column, last row");
	sk_expect_track_px(h, 410, 45, true, "K1 the track's last column");
	sk_expect_track_px(h, 19, 30, false, "K1 left of the track");
	sk_expect_track_px(h, 411, 30, false, "K1 right of the track");
	/* nothing scrolls: the track and no knob */
	sk_expect_no_knob(no[1], false, "K1 a bar of 1 by 1");
	sk_expect_track_px(no[1], 20, 200, true, "K1 a bar of 1 by 1 has its track");
	/* no ＣＧ名: nothing */
	sk_expect_no_knob(no[2], false, "K1 a bar without a CG name");
	sk_expect_track_px(no[2], 20, 300, false, "K1 a bar without a CG name has no track");
	printf("K1 checked\n");
}

/* --- K2 --- */
static void sk_vdraw(void)
{
	sk_setup();
	const char *names[] = { "VBar" };
	const struct sk_bar *bars[] = { &sk_vbar };
	int no[1];
	if (!sk_load("ScrollbarKnobV", 1, names, bars, no))
		return;
	const int v = no[0];
	PE_UpdateComponent(0);
	sk_expect_knob(v, SK_VN, 450, 40, "K2 loaded at 0");
	static const int positions[] = { 484, 242, 100, 1000, 0 };
	for (unsigned i = 0; i < sizeof positions / sizeof positions[0]; i++) {
		sk_set_vpos(v, positions[i]);
		const int pos = positions[i] > 484 ? 484 : positions[i];
		char what[64];
		snprintf(what, sizeof what, "K2 position %d", positions[i]);
		sk_expect_knob(v, SK_VN, 450, 40 + sk_off(614, 100, sk_clamp((float)pos / 484.0f)), what);
	}
	/* the track's ends where the knob is not: the last row with the knob
	 * at the top, the first with it at the bottom */
	sk_expect_track_px(v, 460, 40 + 600, true, "K2 the track's last row (601 high)");
	sk_expect_track_px(v, 460, 40 + 601, false, "K2 below the track");
	sk_set_vpos(v, 484);
	sk_expect_track_px(v, 460, 40, true, "K2 the track's first row");
	/* the backlog with fewer lines than it shows: no knob */
	BU_PE("SetVScrollbarTotalSize", BU_I(v), BU_I(12));
	sk_expect_no_knob(v, true, "K2 12 lines shown by 16");
	printf("K2 checked\n");
}

/* --- K3 --- */
static void sk_origin(void)
{
	sk_setup();
	struct sk_bar centre = sk_hbar;
	centre.x = 300; centre.y = 300; centre.om = 5; centre.rate = 0.5f; centre.pos = 49;
	const char *names[] = { "Centre" };
	const struct sk_bar *bars[] = { &centre };
	int no[1];
	if (!sk_load("ScrollbarKnobOrigin", 1, names, bars, no))
		return;
	PE_UpdateComponent(0);
	/* 391 / 2 = 195 (towards 0), 32 / 2 = 16 */
	sk_expect_knob(no[0], SK_KN, 300 - 195 + 179, 300 - 16, "K3 origin mode 5");
	sk_expect_track_px(no[0], 300 - 195, 300 - 16, true, "K3 the track's corner");
	/* a press on the track counts from its corner on the screen */
	cp_register_event(no[0], 3, 30);
	sk_begin(200, 300);
	sk_frame(200, 300, true);
	const float rate = sk_clamp(((float)200 - (float)(300 - 195) - (float)(32 / 2)) / (float)(391 - 32));
	SK_CHECK(cw_rate(no[0]) == rate, "K3 a press at 200: rate %.9g, want %.9g", cw_rate(no[0]), rate);
	sk_frame(200, 300, false);
	PE_EndInput();
	printf("K3 checked\n");
}

/* --- K4 --- */
static void sk_track(void)
{
	sk_setup();
	const char *names[] = { "Slider" };
	const struct sk_bar *bars[] = { &sk_hbar };
	int no[1];
	if (!sk_load("ScrollbarKnobTrack", 1, names, bars, no))
		return;
	const int h = no[0];
	cp_register_event(h, 7, 70);
	cw_set_rate(h, 0.0f);
	cw_bars_update();
	struct sk_msg drop[8];
	sk_drain(drop, 8);
	sk_begin(170, 46);
	SK_QUIET("K4 the cursor on the track");
	sk_expect_knob(h, SK_KN, 20, 30, "K4 the knob off the cursor is normal");
	/* the press: the knob's centre to the cursor, Scroll in this update */
	sk_frame(170, 46, true);
	float rate = sk_clamp(((float)170 - 20.0f - (float)(32 / 2)) / (float)(391 - 32));
	int pos = sk_trunc(99.0f * rate);
	SK_CHECK(cw_rate(h) == rate && cw_pos(h) == pos, "K4 a press at 170: rate %.9g, position %d; want %.9g, %d",
		 cw_rate(h), cw_pos(h), rate, pos);
	SK_MESSAGES("K4 the press on the track", { 21, h, pos, 99 });
	const int x = 20 + sk_off(391, 32, rate);
	sk_expect_knob(h, SK_KN + 2, x, 30, "K4 held down after the press");
	/* the release: the whole-screen click only, the knob under the cursor */
	sk_frame(170, 46, false);
	SK_CHECK(cw_rate(h) == rate, "K4 the release moved the knob (%.9g)", cw_rate(h));
	SK_MESSAGES("K4 the release", { 4, 0, 170, 0 });
	sk_expect_knob(h, SK_KN + 1, x, 30, "K4 on-cursor after the release, the knob under the cursor");
	/* the ends of the track and beside it */
	sk_frame(25, 40, true);
	SK_CHECK(cw_rate(h) == 0.0f && cw_pos(h) == 0, "K4 a press at 25: rate %.9g, position %d; want 0, 0",
		 cw_rate(h), cw_pos(h));
	sk_frame(25, 40, false);
	sk_frame(405, 40, true);
	SK_CHECK(cw_rate(h) == 1.0f && cw_pos(h) == 99, "K4 a press at 405: rate %.9g, position %d; want 1, 99",
		 cw_rate(h), cw_pos(h));
	sk_frame(405, 40, false);
	sk_drain(drop, 8);
	sk_frame(15, 40, true);
	sk_frame(15, 40, false);
	sk_frame(200, 62, true);
	sk_frame(200, 62, false);
	SK_CHECK(cw_rate(h) == 1.0f, "K4 presses beside the track moved the knob (%.9g)", cw_rate(h));
	SK_MESSAGES("K4 presses beside the track", { 4, 0, 15, 0 }, { 4, 0, 200, 0 });
	PE_EndInput();
	printf("K4 checked\n");
}

/* --- K5 --- */
static void sk_drag(void)
{
	sk_setup();
	const char *names[] = { "Slider" };
	const struct sk_bar *bars[] = { &sk_hbar };
	int no[1];
	if (!sk_load("ScrollbarKnobDrag", 1, names, bars, no))
		return;
	const int h = no[0];
	cp_register_event(h, 7, 70);
	cw_set_rate(h, 0.5f);
	cw_bars_update();
	struct sk_msg drop[8];
	sk_drain(drop, 8);
	const int x = 20 + 179;	/* the knob at 0.5 */
	sk_begin(x + 11, 40);
	sk_expect_knob(h, SK_KN + 1, x, 30, "K5 the cursor on the knob: on-cursor");
	sk_frame(x + 11, 40, true);
	SK_CHECK(cw_rate(h) == 0.5f, "K5 the press on the knob moved it (%.9g)", cw_rate(h));
	SK_QUIET("K5 the press on the knob");
	sk_expect_knob(h, SK_KN + 2, x, 30, "K5 held down");
	/* the cursor 90 to the right */
	sk_frame(x + 11 + 90, 40, true);
	float rate = sk_clamp((float)90 / (float)(391 - 32) + 0.5f);
	SK_CHECK(cw_rate(h) == rate, "K5 dragged by 90: rate %.9g, want %.9g", cw_rate(h), rate);
	SK_MESSAGES("K5 dragged by 90", { 21, h, sk_trunc(99.0f * rate), 99 });
	sk_expect_knob(h, SK_KN + 2, 20 + sk_off(391, 32, rate), 30, "K5 dragged by 90, held down");
	/* off the bar: the same x, then 40 to the right of the press */
	sk_frame(x + 11 + 90, 400, true);
	SK_CHECK(cw_rate(h) == rate, "K5 off the bar at the same x: rate %.9g, want %.9g", cw_rate(h), rate);
	sk_frame(x + 11 + 40, 400, true);
	rate = sk_clamp((float)40 / (float)(391 - 32) + 0.5f);
	SK_CHECK(cw_rate(h) == rate, "K5 off the bar, 40 from the press: rate %.9g, want %.9g", cw_rate(h), rate);
	sk_expect_knob(h, SK_KN + 2, 20 + sk_off(391, 32, rate), 30, "K5 off the bar, held down");
	sk_frame(-1000, 40, true);
	SK_CHECK(cw_rate(h) == 0.0f && cw_pos(h) == 0, "K5 dragged far left: rate %.9g, position %d", cw_rate(h),
		 cw_pos(h));
	sk_frame(5000, 40, true);
	SK_CHECK(cw_rate(h) == 1.0f && cw_pos(h) == 99, "K5 dragged far right: rate %.9g, position %d", cw_rate(h),
		 cw_pos(h));
	/* back to the press: the rate of the press, not a sum of moves */
	sk_frame(x + 11, 40, true);
	SK_CHECK(cw_rate(h) == 0.5f && cw_pos(h) == 49, "K5 back at the press: rate %.9g, position %d; want 0.5, 49",
		 cw_rate(h), cw_pos(h));
	sk_drain(drop, 8);
	/* the release on the knob, then the cursor away */
	sk_frame(x + 11, 40, false);
	SK_MESSAGES("K5 the release", { 4, 0, x + 11, 0 });
	sk_expect_knob(h, SK_KN + 1, x, 30, "K5 released on the knob: on-cursor");
	sk_frame(x + 11 + 120, 40, false);
	SK_CHECK(cw_rate(h) == 0.5f, "K5 a move after the release moved the knob (%.9g)", cw_rate(h));
	SK_QUIET("K5 a move after the release");
	sk_expect_knob(h, SK_KN, x, 30, "K5 the cursor off the knob: normal");
	PE_EndInput();
	printf("K5 checked\n");
}

/* --- K6 --- */
static void sk_backlog(void)
{
	sk_setup();
	const char *names[] = { "VBar" };
	const struct sk_bar *bars[] = { &sk_vbar };
	int no[1];
	if (!sk_load("ScrollbarKnobBacklog", 1, names, bars, no))
		return;
	const int v = no[0];
	cp_register_event(v, 2, 20);
	sk_begin(460, 340);
	struct sk_msg drop[8];
	sk_drain(drop, 8);
	sk_frame(460, 340, true);
	float rate = sk_clamp(((float)340 - 40.0f - (float)(100 / 2)) / (float)(614 - 100));
	int pos = sk_trunc(484.0f * rate);
	SK_CHECK(sk_vpos(v) == pos, "K6 a press at 340: position %d, want %d", sk_vpos(v), pos);
	SK_MESSAGES("K6 the press on the track", { 21, v, pos, 484 });
	sk_expect_knob(v, SK_VDOWN, 450, 40 + sk_off(614, 100, rate), "K6 held down");
	sk_frame(470, 440, true);
	const float rate2 = sk_clamp((float)100 / (float)(614 - 100) + rate);
	const int pos2 = sk_trunc(484.0f * rate2);
	SK_CHECK(sk_vpos(v) == pos2, "K6 dragged by 100: position %d, want %d", sk_vpos(v), pos2);
	SK_MESSAGES("K6 dragged by 100", { 21, v, pos2, 484 });
	sk_frame(470, 440, false);
	SK_MESSAGES("K6 the release", { 4, 0, 470, 0 });
	sk_expect_knob(v, SK_VON, 450, 40 + sk_off(614, 100, rate2), "K6 released on the knob");
	/* below the track (601 of 614 high): nothing */
	sk_frame(460, 40 + 605, true);
	sk_frame(460, 40 + 605, false);
	SK_CHECK(sk_vpos(v) == pos2, "K6 a press below the track moved the knob (%d)", sk_vpos(v));
	PE_EndInput();
	printf("K6 checked\n");
}

/* --- K7 --- */
static void sk_loop(void)
{
	sk_setup();
	const char *names[] = { "Slider" };
	const struct sk_bar *bars[] = { &sk_hbar };
	int no[1];
	if (!sk_load("ScrollbarKnobLoop", 1, names, bars, no))
		return;
	const int h = no[0];
	float config = 0.5f;
	sk_begin(100, 46);
	/* a frame of the settings: the rate written, the update, the rate read */
#define SK_LOOP_FRAME(x, down) do { cw_set_rate(h, config); sk_frame((x), 46, (down)); config = cw_rate(h); } while (0)
	SK_LOOP_FRAME(100, false);
	SK_CHECK(config == 0.5f, "K7 a frame without a press: %.9g", config);
	SK_LOOP_FRAME(100, true);
	const float pressed = sk_clamp(((float)100 - 20.0f - 16.0f) / 359.0f);
	SK_CHECK(config == pressed, "K7 the press: %.9g, want %.9g", config, pressed);
	SK_LOOP_FRAME(150, true);
	const float dragged = sk_clamp((float)50 / 359.0f + pressed);
	SK_CHECK(config == dragged, "K7 the drag: %.9g, want %.9g", config, dragged);
	SK_LOOP_FRAME(150, true);
	SK_CHECK(config == dragged, "K7 held still: %.9g, want %.9g", config, dragged);
	SK_LOOP_FRAME(150, false);
	SK_LOOP_FRAME(300, false);
	SK_CHECK(config == dragged, "K7 after the release: %.9g, want %.9g", config, dragged);
#undef SK_LOOP_FRAME
	sk_expect_knob(h, SK_KN, 20 + sk_off(391, 32, dragged), 30, "K7 the knob where the setting is");
	PE_EndInput();
	printf("K7 checked\n");
}

/* --- K8 --- */
static void sk_blocked(void)
{
	sk_setup();
	struct sk_bar full = sk_hbar;
	full.y = 200; full.total = 1; full.view = 1; full.pos = 0; full.rate = 0.0f;
	const char *names[] = { "Slider", "Full" };
	const struct sk_bar *bars[] = { &sk_hbar, &full };
	int no[2];
	if (!sk_load("ScrollbarKnobBlocked", 2, names, bars, no))
		return;
	const int h = no[0], f = no[1];
	sk_begin(100, 210);
	sk_frame(100, 210, true);
	sk_frame(300, 210, true);
	sk_frame(300, 210, false);
	SK_CHECK(cw_rate(f) == 0.0f && cw_pos(f) == 0, "K8 a bar of 1 by 1 moved (%.9g, %d)", cw_rate(f), cw_pos(f));
	/* a clickable parts in front of the knob (at 1.0, x 379 .. 410) */
	const int B = 9897;
	br_box(B, 370, 20, 60, 60, false);
	PE_SetZ(B, 1000);
	PE_UpdateComponent(0);
	sk_frame(390, 46, true);
	sk_frame(250, 46, true);
	sk_frame(250, 46, false);
	SK_CHECK(cw_rate(h) == 1.0f, "K8 a press on the parts in front of the knob dragged it (%.9g)", cw_rate(h));
	/* beside the parts the track takes the press */
	sk_frame(250, 46, true);
	sk_frame(250, 46, false);
	SK_CHECK(cw_rate(h) < 1.0f, "K8 a press on the track beside the parts did nothing (%.9g)", cw_rate(h));
	PE_EndInput();
	printf("K8 checked\n");
}

/* --- K9 --- */
static void sk_v13(void)
{
	ain->version = 13;
	sk_setup();
	const char *names[] = { "Slider", "VBar" };
	const struct sk_bar *bars[] = { &sk_hbar, &sk_vbar };
	int no[2];
	if (!sk_load("ScrollbarKnobOld", 2, names, bars, no))
		return;
	struct parts *v = parts_try_get(no[1]);
	PE_UpdateComponent(0);
	sk_expect_no_knob(no[0], false, "K9 v13, the horizontal bar");
	sk_expect_no_knob(no[1], true, "K9 v13, the vertical bar");
	sk_expect_track_px(no[1], 460, 40, false, "K9 v13, the vertical track");
	sk_begin(460, 340);
	sk_frame(460, 340, true);
	sk_frame(460, 440, true);
	sk_frame(460, 440, false);
	SK_CHECK(v->scrollbar.pos == 0 && v->scrollbar.rate == 0.0f, "K9 v13: the vertical bar moved to %d, %g",
		 v->scrollbar.pos, v->scrollbar.rate);
	PE_EndInput();
	printf("K9 checked\n");
}

/* --- K10 --- */
static void sk_sjis(void)
{
	sk_setup();
	struct sk_bar h = sk_hbar, v = sk_vbar;
	h.sjis = v.sjis = true;
	const char *names[] = { "Slider", "VBar" };
	const struct sk_bar *bars[] = { &h, &v };
	int no[2];
	if (!sk_load("ScrollbarKnobSjis", 2, names, bars, no))
		return;
	PE_UpdateComponent(0);
	sk_expect_no_knob(no[0], false, "K10 SJIS keys, the horizontal bar");
	sk_expect_no_knob(no[1], true, "K10 SJIS keys, the vertical bar");
	sk_expect_track_px(no[0], 20, 30, false, "K10 SJIS keys, the horizontal track");
	sk_begin(460, 340);
	sk_frame(460, 340, true);
	sk_frame(460, 340, false);
	sk_frame(170, 46, true);
	sk_frame(170, 46, false);
	SK_CHECK(sk_vpos(no[1]) == 0 && cw_rate(no[0]) == 1.0f, "K10 SJIS keys: the bars moved (%d, %.9g)",
		 sk_vpos(no[1]), cw_rate(no[0]));
	PE_EndInput();
	printf("K10 checked\n");
}

/* --- K11 --- */
static void sk_release(void)
{
	sk_setup();
	struct sk_bar other = sk_hbar;
	other.y = 200;
	const char *names[] = { "Slider", "Other" };
	const struct sk_bar *bars[] = { &sk_hbar, &other };
	int no[2];
	if (!sk_load("ScrollbarKnobRelease", 2, names, bars, no))
		return;
	const int h = no[0], o = no[1];
	cp_register_event(h, 7, 70);
	cp_register_event(o, 8, 80);
	sk_begin(395, 46);
	struct sk_msg drop[8];
	sk_drain(drop, 8);
	sk_frame(395, 46, true);
	PE_ReleaseParts(h);
	SK_CHECK(!parts_try_get(h), "K11 the bar was not released");
	sk_frame(200, 46, true);
	sk_frame(200, 46, false);
	SK_MESSAGES("K11 dragged while released", { 4, 0, 200, 0 });
	/* another widget: no knob; then made a 橫滾動條 again by the library */
	BU_PE("SetComponentType", BU_I(o), BU_I(0), BU_I(1));
	sk_expect_no_knob(o, false, "K11 made a button");
	sk_frame(395, 216, true);
	sk_frame(300, 216, true);
	sk_frame(300, 216, false);
	cw_set_rate(o, 0.5f);
	SK_CHECK(cw_type(o) == 3, "K11 the library did not make it a bar again (%d)", cw_type(o));
	sk_expect_no_knob(o, false, "K11 made a bar again (no CG name)");
	sk_frame(250, 216, true);
	sk_frame(250, 216, false);
	SK_CHECK(cw_rate(o) == 0.5f, "K11 the remade bar moved (%.9g)", cw_rate(o));
	PE_EndInput();
	printf("K11 checked\n");
}

static void (*sk_case)(void);
static void sk_case_body(void)
{
	sk_case();
	sk_failures += cw_failures + bt_failures + cn_failures + cc_failures;
}

static int test_scrollbar_knob(void)
{
	static const struct { const char *name; void (*fn)(void); } cases[] = {
		{ "K1", sk_hdraw }, { "K2", sk_vdraw }, { "K3", sk_origin }, { "K4", sk_track }, { "K5", sk_drag },
		{ "K6", sk_backlog }, { "K7", sk_loop }, { "K8", sk_blocked }, { "K9", sk_v13 }, { "K10", sk_sjis },
		{ "K11", sk_release },
	};
	const int n = sizeof(cases) / sizeof(cases[0]);
	int failed = 0, env = 0;
	for (int i = 0; i < n; i++) {
		sk_case = cases[i].fn;
		int r = cn_fork_case(cases[i].name, sk_case_body, &sk_failures);
		failed += r == 1;
		env += r == 2;
	}
	if (env) {
		printf("SCROLLBAR-KNOB ENVIRONMENT FAILURE: %d of %d cases could not run, %d failed\n", env, n, failed);
		return 2;
	}
	printf("SCROLLBAR-KNOB %s: %d of %d cases failed\n", failed ? "FAIL" : "PASS", failed, n);
	return failed ? 1 : 0;
}
