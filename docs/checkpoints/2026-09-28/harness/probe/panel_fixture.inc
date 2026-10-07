/* panel: the colour of a v14 パネル (component type 14) keeps its alpha, and
 * the script's construction commands reach the operation the original runs
 * (static disassembly only):
 *   - the panel widget (constructor 0x4dc6f0: 200x200, (220,220,220,255),
 *     marked for a rebuild) keeps its size at +0xa8/+0xac, its colour at
 *     +0xb0..+0xbc and アルファグラデーション at +0xc0..+0xcc. The pactex
 *     loader (0x4dc440), SetPanelSize (0x5971a0) and SetPanelColor (0x5971f0)
 *     store the values and mark the widget when one changed; the next update
 *     (0x4dc940) rebuilds it (0x4dcc10): a width or height that is not
 *     positive ends the rebuild at once (0x4dcc69, nothing is touched);
 *     otherwise the steps of its inner state are cleared (0x568f40),
 *     command 0 creates the surface (0x568fa0 -> 0x5a6980), command 6 fills
 *     all of it with the colour (0x569200 -> 0x5a6df0, which writes 6 at
 *     0x5a6e73) and the steps are run (0x569350 -> 0x5a73c0).
 *     Command 6 (0x4fc490) replaces RGBA, so the surface is (r, g, b, a).
 *     The getters (0x597260..) return the stored values, 0 without a parts;
 *     every panel function turns an existing parts of another type into a
 *     panel (0x53e290 -> 0x536ff0) and does nothing for an unknown number;
 *   - PartsEngine.AddPartsConstructionProcess (0x59a730) copies ArrayInt
 *     0..39 into a step: [0] コマンド, [6],[7] 先矩形 X,Y, [10],[11] W,H,
 *     [12..15] 色１, [20],[21] the character and line spacing, [22],[23] the
 *     font type and size, [24..26] the font colour, [27..29] the edge
 *     colour, [30] != 0 全體 (0x59aab4). The executor's commands (0x4fa1d0,
 *     table 0x4fb7d8) are the script's: each handler names itself in its
 *     error message as CASConstructionProcess's Set method does. Commands 3,
 *     4, 5 and 6 take the whole surface with 全體 and 先矩形 X,Y,W,H
 *     otherwise, normalized (a negative size extends the other way,
 *     0x5abed0); an empty rectangle fails the step (0x5abde0).
 * xsystem4 drew a panel with the blending fill (command 4) on Create's
 * opaque black, so its alpha was always 255: the dimmer of a dialogue
 * ((0,0,0,128)) and the base of the system menu ((0,0,0,160)) were opaque
 * black. It also handed the script's command 6 to that fill, took a missing
 * width for the whole surface and read the text commands' font from the
 * wrong items.
 *
 * The expected pixels follow the original's rules, worked out by hand.
 *
 *   PN1 panels from a pactex: opaque colours (unchanged), alpha 128, 160, 0
 *       and a coloured (255,255,130,200); the same under the SJIS keys;
 *       values outside 0..255 are clamped in the surface (0x5ac764) while
 *       GetPanelR.. return what the pactex says.
 *   PN2 SetPanelColor / SetPanelSize / GetPanelR.. through their AIN
 *       declarations: the surface is the colour (an alpha of 0 included),
 *       calling again does not add operations, the size keeps the colour
 *       and the colour the size, a size that is not positive keeps the
 *       surface and the colour; a parts that is not a panel yet, a
 *       construction parts included, starts as 200x200 (220,220,220,255),
 *       and a getter makes it a panel as a setter does; an unknown number is
 *       not created; the real CPanelParts@ColorA::set, which reads the other
 *       three back, changes only the alpha (to 0 as well).
 *   PN3 the script's command 6, as DungeonSelector@BuildAreaParts draws its
 *       Area with the real CASConstructionProcess::Create / Fill (r,g,b,a) /
 *       Fill (x,y,w,h,r,g,b,a) and parts::detail::AddConstructProcess:
 *       160x80, (255,128,0) with alpha 0, the name's box and (58,30,44,44)
 *       (0,0,0) with alpha 1. With the pixel decision an alpha of 1 takes
 *       the click and an alpha of 0 lets it through (click-permission's CP3
 *       draws the same surface with the engine's own calls).
 *   PN4 全體 and the rectangle of the script's fills: all of the surface
 *       with [30] 1 or 2 whatever the rectangle says, nothing for a width of
 *       0, a negative size extends the other way for each of the four
 *       commands; command 3, 4 and 5 keep what they write (RGB, a blend, the
 *       alpha).
 *   PN5 the text commands 7, 8, 23 and 24 take the font size from [23], the
 *       colours from [24..26] and [27..29] (the operation's fields; nothing
 *       is drawn).
 *   PN6 guards: what every other command adds is unchanged (a table taken
 *       on the tree before the fix: xsystem4's present bindings, several of
 *       which are approximations and not the original's result). Of its 45
 *       commands 12 add an operation whose fields are compared; 23 are
 *       filters or have no binding and add none; the 10 that take a CG (2,
 *       11 to 14, 19 to 22, 29) are added with an empty CG name, for which
 *       nothing is added before the binding is reached: those are not
 *       guarded.
 *   PN7 PE_Save / PE_Load: the same pixels and operations, and the loaded
 *       panel keeps its colour for a new size and its size for a new colour
 *       (the values are not saved but taken from the two operations); a
 *       page written before the fix (a panel saved as Create + the blending
 *       fill, also with the fills SetPanelColor used to pile up) loads as
 *       the panel's colour; a construction parts that is not a panel (with
 *       no type, or a low-level parts, type 18 with a state of type 26),
 *       and a panel whose fill does not cover it, load as they were saved.
 *   PN8 guards: a pactex 構築部件 is built as before and a script's command
 *       4 still blends; under an AIN before version 14 the construction API
 *       and a load are unchanged. Such an AIN writes XPE version 3, which
 *       has no component type, so every parts loads with type 0: the
 *       `ain->version >= 14` beside the conversion in save.c cannot be
 *       reached and no case guards it.
 * PN6 and PN8 pass before the fix. So do the pixels, sizes and operations of
 * PN1's opaque panels; their getters returned 0 there.
 *
 * PN3's four presses are checked against xsystem4's present dispatch, like
 * click-permission's (see that file): they change with it.
 *
 * Not covered:
 *   - アルファグラデーション (commands 25 and 26 after the fill, 0x569600 /
 *     0x569670) is not implemented;
 *   - the drawing of a panel over the scene;
 *   - the size the original's widget reports for its parts (its width and
 *     height fields, 0x4dce10; here the surface's): a parts a script makes a
 *     panel and never sizes or colours shows the 200x200 default there and
 *     nothing here, and setting only its width or only its height (which
 *     reads the other one back) builds nothing here;
 *   - the growth the original's update adds to the size (0x4dc94d);
 *   - what the original replaces when a parts becomes a panel (the whole
 *     widget, 0x536ff0): here the other states stay; a parts that
 *     SetComponentType made a panel before the first panel call takes the
 *     values of its own operations, and one that was a panel before keeps
 *     its values (PN2's construction parts go through a panel call first);
 *   - a rectangle past the surface for commands 3, 4 and 5 (command 6 has a
 *     case; nothing of the fix clips, the drawing does);
 *   - the pixels the text commands draw. 23 and 24 are drawn as 7, which on
 *     a base of alpha 0 cannot be seen.
 *
 * Uses the construction mode's GL setup and its fork (an environment failure
 * is retried once and ends the mode with exit code 2). An AIN without what a
 * case needs (a factory, a member, 40 items in ArrayInt) is reported the same
 * way, as the environment and not as a failed case. Only functions that
 * exist before the fix are used, so before-check builds the old tree.
 */
static int pn_failures;
#define PN_CHECK(c, ...) do { if (!(c)) { printf("  FAIL: "); printf(__VA_ARGS__); printf("\n"); pn_failures++; } } while (0)

#define PN_TYPE_S "\x83\x70\x81\x5b\x83\x63\x83\x5e\x83\x43\x83\x76"	/* パーツタイプ (SJIS) */
#define PN_PANEL_S "\x83\x70\x83\x6c\x83\x8b"				/* パネル */
#define PN_SIZE_S "\x83\x54\x83\x43\x83\x59"				/* サイズ */
#define PN_COLOR_S "\x90\x46"						/* 色 */

/* パネル { サイズ, 色 } under the GBK or the SJIS keys. */
static struct ex_tree pn_panel(const char *name, int w, int h, int r, int g, int b, int a, bool sjis)
{
	struct ex_tree info = bu_branch(BU_INFO, 3);
	info.children[0] = bu_sleaf(sjis ? PN_TYPE_S : BU_TYPE, sjis ? PN_PANEL_S : CP_PANEL);
	info.children[1] = lb_ilist(sjis ? PN_SIZE_S : CP_SIZE, 2, (int[]){ w, h });
	info.children[2] = lb_ilist(sjis ? PN_COLOR_S : CP_COLOR, 4, (int[]){ r, g, b, a });
	struct lb_place pl = { 0, 0, 1 };
	return lb_node(name, &pl, info, 0);
}

static int pn_nr_ops(int no)
{
	struct parts *p = no > 0 ? parts_try_get(no) : NULL;
	return p && p->states[0].type == PARTS_CONSTRUCTION_PROCESS ? cn_nr_ops(p) : -1;
}

/* The RGBA pixels of a built w x h normal state (to be freed), or NULL with
 * a failure. */
static uint8_t *pn_pixels(int no, int w, int h, const char *what)
{
	struct parts *p = no > 0 ? parts_try_get(no) : NULL;
	struct parts_state *s = p ? &p->states[0] : NULL;
	bool built = s && s->type == PARTS_CONSTRUCTION_PROCESS && s->common.texture.handle
		&& s->common.texture.w == w && s->common.texture.h == h && s->common.w == w && s->common.h == h;
	PN_CHECK(built, "%s: state type %d, texture %s %dx%d, size %dx%d; want a built %dx%d surface", what,
		 s ? (int)s->type : -1, s && s->common.texture.handle ? "present" : "missing", s ? s->common.texture.w : 0,
		 s ? s->common.texture.h : 0, s ? s->common.w : 0, s ? s->common.h : 0, w, h);
	return built ? gfx_get_pixels(&s->common.texture) : NULL;
}

/* Every pixel of the w x h surface is (r, g, b, a). */
static void pn_expect_flat(int no, int w, int h, int r, int g, int b, int a, const char *what)
{
	uint8_t *px = pn_pixels(no, w, h, what);
	if (!px)
		return;
	const uint8_t want[4] = { r, g, b, a };
	int bad = 0, first = -1;
	for (int i = 0; i < w * h; i++) {
		if (memcmp(px + i * 4, want, 4) && !bad++)
			first = i;
	}
	if (bad) {
		const uint8_t *c = px + first * 4;
		PN_CHECK(0, "%s: %d of %d pixels differ, (%d,%d) = (%d,%d,%d,%d), want (%d,%d,%d,%d)", what, bad, w * h,
			 first % w, first / w, c[0], c[1], c[2], c[3], r, g, b, a);
	}
	free(px);
}

struct pn_rect { int x, y, w, h; uint8_t c[4]; };
/* Every pixel is `base`, or the colour of the last rectangle that holds it. */
static void pn_expect_rects(int no, int w, int h, const uint8_t base[4], int n, const struct pn_rect *rects, const char *what)
{
	uint8_t *px = pn_pixels(no, w, h, what);
	if (!px)
		return;
	int bad = 0;
	for (int y = 0; y < h; y++) {
		for (int x = 0; x < w; x++) {
			const uint8_t *want = base;
			for (int i = 0; i < n; i++) {
				if (x >= rects[i].x && x < rects[i].x + rects[i].w && y >= rects[i].y && y < rects[i].y + rects[i].h)
					want = rects[i].c;
			}
			const uint8_t *c = px + (y * w + x) * 4;
			if (memcmp(c, want, 4) && !bad++)
				PN_CHECK(0, "%s: (%d,%d) = (%d,%d,%d,%d), want (%d,%d,%d,%d)", what, x, y, c[0], c[1], c[2], c[3],
					 want[0], want[1], want[2], want[3]);
		}
	}
	PN_CHECK(bad <= 1, "%s: %d pixels differ", what, bad);
	free(px);
}
#define PN_RECTS(...) (int)(sizeof((struct pn_rect[]){ __VA_ARGS__ }) / sizeof(struct pn_rect)), (struct pn_rect[]){ __VA_ARGS__ }

static void pn_expect_color(int no, int r, int g, int b, int a, const char *what)
{
	int got[4] = { BU_PE("GetPanelR", BU_I(no)).i, BU_PE("GetPanelG", BU_I(no)).i, BU_PE("GetPanelB", BU_I(no)).i,
		BU_PE("GetPanelA", BU_I(no)).i };
	PN_CHECK(got[0] == r && got[1] == g && got[2] == b && got[3] == a, "%s: GetPanelR/G/B/A (%d,%d,%d,%d), want (%d,%d,%d,%d)",
		 what, got[0], got[1], got[2], got[3], r, g, b, a);
}

/* --- PN1 --- */
static void pn_pactex(void)
{
	cn_gl_setup();
	static const struct { const char *name; int w, h, c[4], surface[4]; bool sjis; } panels[] = {
		{ "Opaque", 64, 32, { 220, 220, 220, 255 }, { 220, 220, 220, 255 } },
		{ "OpaqueColor", 9, 5, { 255, 255, 128, 255 }, { 255, 255, 128, 255 } },
		{ "Dimmer", 64, 32, { 0, 0, 0, 128 }, { 0, 0, 0, 128 } },		/* SceneYesNoDialog */
		{ "Base", 48, 16, { 0, 0, 0, 160 }, { 0, 0, 0, 160 } },			/* SceneAdvButtonMenu */
		{ "Clear", 7, 3, { 10, 20, 30, 0 }, { 10, 20, 30, 0 } },
		{ "Web", 41, 7, { 255, 255, 130, 200 }, { 255, 255, 130, 200 } },
		{ "Yakata", 4, 4, { 0, 0, 0, 128 }, { 0, 0, 0, 128 } },			/* SceneYakataMenu's 4x4 */
		{ "Clamped", 6, 6, { 300, -5, 128, 400 }, { 255, 0, 128, 255 } },
		{ "DimmerSjis", 64, 32, { 0, 0, 0, 128 }, { 0, 0, 0, 128 }, true },
		{ "OpaqueSjis", 9, 5, { 255, 255, 128, 255 }, { 255, 255, 128, 255 }, true },
	};
	enum { N = sizeof(panels) / sizeof(panels[0]) };
	struct ex_tree *kids;
	struct ex *e = lb_activity("PanelColors", N, &kids);
	for (int i = 0; i < N; i++)
		kids->children[i] = pn_panel(panels[i].name, panels[i].w, panels[i].h, panels[i].c[0], panels[i].c[1],
					     panels[i].c[2], panels[i].c[3], panels[i].sjis);
	int act = cn_load("PanelColors", e);
	pn_failures += cn_failures;
	for (int i = 0; i < N; i++) {
		int no = bu_number(act, panels[i].name);
		PN_CHECK(no > 0, "%s has no parts number", panels[i].name);
		if (no <= 0)
			continue;
		char what[64];
		snprintf(what, sizeof what, "PN1 %s", panels[i].name);
		pn_expect_flat(no, panels[i].w, panels[i].h, panels[i].surface[0], panels[i].surface[1], panels[i].surface[2],
			       panels[i].surface[3], what);
		PN_CHECK(pn_nr_ops(no) == 2, "%s: %d operations, want 2", what, pn_nr_ops(no));
		int w = BU_PE("Parts_GetPartsWidth", BU_I(no), BU_I(1)).i, h = BU_PE("Parts_GetPartsHeight", BU_I(no), BU_I(1)).i;
		PN_CHECK(w == panels[i].w && h == panels[i].h, "%s: Parts_GetPartsWidth/Height %dx%d, want %dx%d", what, w, h,
			 panels[i].w, panels[i].h);
		if (!panels[i].sjis) {
			int type = BU_PE("GetComponentType", BU_I(no), BU_I(1)).i;
			PN_CHECK(type == 14, "%s: GetComponentType %d, want 14", what, type);
		}
		pn_expect_color(no, panels[i].c[0], panels[i].c[1], panels[i].c[2], panels[i].c[3], what);
	}
	heap_unref(act);
	printf("PN1 panels from a pactex: %s\n", pn_failures ? "FAIL" : "ok");
}

/* --- PN2 --- */
static void pn_set_color(int no, int r, int g, int b, int a)
{
	BU_PE("SetPanelColor", BU_I(no), BU_I(r), BU_I(g), BU_I(b), BU_I(a));
}
static void pn_set_size(int no, int w, int h)
{
	BU_PE("SetPanelSize", BU_I(no), BU_I(w), BU_I(h));
}

static void pn_hll(void)
{
	cn_gl_setup();
	struct ex_tree *kids;
	struct ex *e = lb_activity("PanelCalls", 1, &kids);
	kids->children[0] = pn_panel("Dimmer", 64, 32, 0, 0, 0, 128, false);
	int act = cn_load("PanelCalls", e);
	pn_failures += cn_failures;
	int dim = bu_number(act, "Dimmer");
	heap_unref(act);
	PN_CHECK(dim > 0, "Dimmer has no parts number");
	if (dim <= 0)
		return;

	/* A pactex panel: the colour replaces the surface. */
	pn_set_color(dim, 10, 20, 30, 160);
	pn_expect_flat(dim, 64, 32, 10, 20, 30, 160, "PN2 SetPanelColor");
	pn_expect_color(dim, 10, 20, 30, 160, "PN2 SetPanelColor");
	PN_CHECK(pn_nr_ops(dim) == 2, "PN2 SetPanelColor: %d operations, want 2", pn_nr_ops(dim));
	pn_set_color(dim, 10, 20, 30, 160);
	pn_set_color(dim, 1, 2, 3, 4);
	pn_set_color(dim, 1, 2, 3, 4);
	pn_expect_flat(dim, 64, 32, 1, 2, 3, 4, "PN2 SetPanelColor four times");
	PN_CHECK(pn_nr_ops(dim) == 2, "PN2 SetPanelColor four times: %d operations, want 2", pn_nr_ops(dim));
	/* An alpha of 0 is a colour like any other: a transparent surface. */
	pn_set_color(dim, 1, 2, 3, 0);
	pn_expect_flat(dim, 64, 32, 1, 2, 3, 0, "PN2 SetPanelColor with alpha 0");
	pn_expect_color(dim, 1, 2, 3, 0, "PN2 SetPanelColor with alpha 0");
	PN_CHECK(pn_nr_ops(dim) == 2, "PN2 SetPanelColor with alpha 0: %d operations, want 2", pn_nr_ops(dim));
	pn_set_color(dim, 1, 2, 3, 4);
	/* The size keeps the colour. */
	pn_set_size(dim, 20, 10);
	pn_expect_flat(dim, 20, 10, 1, 2, 3, 4, "PN2 SetPanelSize");
	pn_expect_color(dim, 1, 2, 3, 4, "PN2 SetPanelSize");
	PN_CHECK(pn_nr_ops(dim) == 2, "PN2 SetPanelSize: %d operations, want 2", pn_nr_ops(dim));
	/* A size that is not positive is not built (0x4dcc69): the surface
	 * stays, and so does the colour for the next size. */
	pn_set_size(dim, 0, 5);
	pn_expect_flat(dim, 20, 10, 1, 2, 3, 4, "PN2 SetPanelSize(0, 5)");
	pn_expect_color(dim, 1, 2, 3, 4, "PN2 SetPanelSize(0, 5)");
	pn_set_color(dim, 9, 8, 7, 6);
	pn_expect_flat(dim, 20, 10, 1, 2, 3, 4, "PN2 SetPanelColor on a panel without width");
	pn_expect_color(dim, 9, 8, 7, 6, "PN2 SetPanelColor on a panel without width");
	pn_set_size(dim, 5, -1);
	pn_expect_flat(dim, 20, 10, 1, 2, 3, 4, "PN2 SetPanelSize(5, -1)");
	pn_set_size(dim, 6, 4);
	pn_expect_flat(dim, 6, 4, 9, 8, 7, 6, "PN2 SetPanelSize after an empty size");
	PN_CHECK(pn_nr_ops(dim) == 2, "PN2 after an empty size: %d operations, want 2", pn_nr_ops(dim));

	/* A parts the script makes a panel of (CPanelParts@0: SetComponentType
	 * 14): 200x200 and (220,220,220,255) until told otherwise (0x4dc7a9). */
	const int COLOR_FIRST = 9971, SIZE_FIRST = 9972, PLAIN = 9973, UNKNOWN = 9974, UNKNOWN2 = 9975;
	parts_get(COLOR_FIRST);
	BU_PE("SetComponentType", BU_I(COLOR_FIRST), BU_I(14), BU_I(1));
	pn_expect_color(COLOR_FIRST, 220, 220, 220, 255, "PN2 a new panel");
	pn_set_color(COLOR_FIRST, 50, 60, 70, 80);
	pn_expect_flat(COLOR_FIRST, 200, 200, 50, 60, 70, 80, "PN2 colour before size");
	pn_set_size(COLOR_FIRST, 8, 6);
	pn_expect_flat(COLOR_FIRST, 8, 6, 50, 60, 70, 80, "PN2 size after colour");
	parts_get(SIZE_FIRST);
	BU_PE("SetComponentType", BU_I(SIZE_FIRST), BU_I(14), BU_I(1));
	pn_set_size(SIZE_FIRST, 8, 6);
	pn_expect_flat(SIZE_FIRST, 8, 6, 220, 220, 220, 255, "PN2 size before colour");
	pn_set_color(SIZE_FIRST, 0, 0, 0, 128);
	pn_expect_flat(SIZE_FIRST, 8, 6, 0, 0, 0, 128, "PN2 colour after size");
	/* A parts of another type becomes a panel (0x536ff0). */
	parts_get(PLAIN);
	pn_set_size(PLAIN, 3, 2);
	pn_expect_flat(PLAIN, 3, 2, 220, 220, 220, 255, "PN2 SetPanelSize on a plain parts");
	int type = BU_PE("GetComponentType", BU_I(PLAIN), BU_I(1)).i;
	PN_CHECK(type == 14, "PN2 a plain parts after SetPanelSize: GetComponentType %d, want 14", type);
	/* The getters take the widget the same way (0x597260 -> 0x53e290). */
	const int GETTER = 9979;
	parts_get(GETTER);
	int red = BU_PE("GetPanelR", BU_I(GETTER)).i;
	type = BU_PE("GetComponentType", BU_I(GETTER), BU_I(1)).i;
	PN_CHECK(red == 220 && type == 14, "PN2 GetPanelR on a plain parts: %d, then GetComponentType %d; want 220, 14", red, type);
	/* So does a construction parts, whatever it had drawn: the widget is
	 * replaced, with the constructor's values. */
	const int CONS_SIZE = 9977, CONS_COLOR = 9978;
	for (int no = CONS_SIZE; no <= CONS_COLOR; no++) {
		PE_AddCreateToPartsConstructionProcess(no, 5, 5, 1);
		PE_AddFillWithAlphaToPartsConstructionProcess(no, 0, 0, 5, 5, 1, 2, 3, 4, 1);
		PE_BuildPartsConstructionProcess(no, 1);
	}
	pn_set_size(CONS_SIZE, 3, 2);
	pn_expect_flat(CONS_SIZE, 3, 2, 220, 220, 220, 255, "PN2 SetPanelSize on a construction parts");
	pn_set_color(CONS_COLOR, 9, 9, 9, 9);
	pn_expect_flat(CONS_COLOR, 200, 200, 9, 9, 9, 9, "PN2 SetPanelColor on a construction parts");
	/* An unknown number is not created (0x53d920 finds nothing). */
	pn_set_color(UNKNOWN, 1, 2, 3, 4);
	pn_set_size(UNKNOWN2, 5, 5);
	pn_expect_color(UNKNOWN, 0, 0, 0, 0, "PN2 an unknown number");
	PN_CHECK(!parts_try_get(UNKNOWN) && !parts_try_get(UNKNOWN2), "PN2 a panel call created parts %d: %d, %d: %d", UNKNOWN,
		 !!parts_try_get(UNKNOWN), UNKNOWN2, !!parts_try_get(UNKNOWN2));

	/* The real CPanelParts: ColorA::set reads R, G and B back and calls
	 * SetPanelColor with them. */
	const int REAL = 9976;
	int create = ain_get_function(ain, "AFL_Parts_CreatePanel");
	int set_a = ain_get_function(ain, "parts::detail::CPanelParts@ColorA::set");
	PN_CHECK(create >= 0 && set_a >= 0, "AFL_Parts_CreatePanel or CPanelParts@ColorA::set is not in the AIN");
	if (create >= 0 && set_a >= 0) {
		int args[] = { REAL, -1, 1 };	/* option<string> comment = none */
		union vm_value r[2] = {{ .i = -1 }, { .i = -1 }};
		int n = da_callfunc(create, 3, args, r);
		pn_failures += da_failures;
		PN_CHECK(n == 2 && da_live_page(r[0].i), "AFL_Parts_CreatePanel returned %d slot(s) [%d, %d]", n, r[0].i, r[1].i);
		if (n == 2 && da_live_page(r[0].i)) {
			type = BU_PE("GetComponentType", BU_I(REAL), BU_I(1)).i;
			PN_CHECK(type == 14, "PN2 the real panel: GetComponentType %d, want 14", type);
			int alpha = 96;
			call_method_args(set_a, r[0].i, 1, &alpha);
			pn_expect_flat(REAL, 200, 200, 220, 220, 220, 96, "PN2 CPanelParts@ColorA::set");
			pn_set_color(REAL, 30, 40, 50, 255);
			alpha = 17;
			call_method_args(set_a, r[0].i, 1, &alpha);
			pn_expect_flat(REAL, 200, 200, 30, 40, 50, 17, "PN2 CPanelParts@ColorA::set after a colour");
			alpha = 0;
			call_method_args(set_a, r[0].i, 1, &alpha);
			pn_expect_flat(REAL, 200, 200, 30, 40, 50, 0, "PN2 CPanelParts@ColorA::set(0)");
			PN_CHECK(pn_nr_ops(REAL) == 2, "PN2 the real panel: %d operations, want 2", pn_nr_ops(REAL));
		}
	}
	printf("PN2 panel calls: %s\n", pn_failures ? "FAIL" : "ok");
}

/* --- the script's construction commands --- */

/* A function of the AIN by its name and number of arguments (the overloads
 * of CASConstructionProcess::Fill share the name). An AIN without it, like
 * the other shapes the helpers below insist on, is the environment's failure
 * (CN_ENV_EXIT), not the case's. */
static int pn_fn(const char *name, int nargs)
{
	for (int i = 0; i < ain->nr_functions; i++) {
		if (ain->functions[i].nr_args == nargs && !strcmp(ain->functions[i].name, name))
			return i;
	}
	printf("PANEL %s/%d is not in the AIN\n", name, nargs);
	fflush(stdout);
	_exit(CN_ENV_EXIT);
}

/* A CASConstructionProcess from one of its real factories; to be released. */
static int pn_process(const char *factory, int nargs, const int *args)
{
	union vm_value r[2] = {{ .i = -1 }, { .i = -1 }};
	int n = da_callfunc(pn_fn(factory, nargs), nargs, args, r);
	pn_failures += da_failures;
	da_failures = 0;
	if (n != 1 || !da_live_page(r[0].i)) {
		printf("PANEL %s/%d returned %d slot(s) [%d]\n", factory, nargs, n, r[0].i);
		fflush(stdout);
		_exit(CN_ENV_EXIT);
	}
	return r[0].i;
}

/* parts::detail::AddConstructProcess(no, process, state): the real bytecode
 * around the one CALLHLL PartsEngine.AddPartsConstructionProcess. Keeps the
 * caller's reference whether or not the callee released its argument. */
static void pn_add(int no, int process)
{
	static int add = -1;
	if (add < 0)
		add = pn_fn("parts::detail::AddConstructProcess", 3);
	int ref = HEAP_REF(process);
	heap_ref(process);
	int args[] = { no, process, 1 };
	union vm_value r[2];
	int n = da_callfunc(add, 3, args, r);
	pn_failures += da_failures;
	da_failures = 0;
	PN_CHECK(n == 0, "parts::detail::AddConstructProcess left %d slot(s)", n);
	if (HEAP_REF(process) > ref)
		heap_unref(process);
	PN_CHECK(HEAP_REF(process) == ref, "the process has %d references after AddConstructProcess, had %d", HEAP_REF(process), ref);
}
#define PN_ADD(no, factory, ...) do { \
	int pn_args_[] = { __VA_ARGS__ }; \
	int pn_proc_ = pn_process(factory, (int)(sizeof pn_args_ / sizeof pn_args_[0]), pn_args_); \
	pn_add(no, pn_proc_); \
	heap_unref(pn_proc_); \
} while (0)

static void pn_build(int no)
{
	bool built = BU_PE("Parts_BuildPartsConstructionProcess", BU_I(no), BU_I(1)).i;
	PN_CHECK(built, "Parts_BuildPartsConstructionProcess(%d) returned false", no);
}

/* A member array of a CASConstructionProcess: the wrap<array<T>> slot holds
 * the array page, or a struct whose first member does. */
static struct page *pn_member_array(int process, const char *member)
{
	int st = ain_get_struct(ain, "CASConstructionProcess");
	int slot = st >= 0 ? heap[process].page->values[br_member(st, member)].i : -1;
	struct page *p = slot > 0 && heap_index_valid(slot) && heap[slot].type == VM_PAGE ? heap[slot].page : NULL;
	if (p && p->type != ARRAY_PAGE && p->nr_vars > 0) {
		int inner = p->values[0].i;
		p = inner > 0 && heap_index_valid(inner) && heap[inner].type == VM_PAGE ? heap[inner].page : NULL;
	}
	if (!p || p->type != ARRAY_PAGE) {
		printf("PANEL CASConstructionProcess.%s is not an array\n", member);
		fflush(stdout);
		_exit(CN_ENV_EXIT);
	}
	return p;
}

/* The script's Area as DungeonSelector@BuildAreaParts draws it; the name's
 * box is (80 - w / 2, 22 - h / 2, w, h) for the rendered size of the name,
 * here 96x16. */
static void pn_draw_area(int no)
{
	PN_ADD(no, "CASConstructionProcess::Create", 160, 80);
	PN_ADD(no, "CASConstructionProcess::Fill", 255, 128, 0, 0);
	PN_ADD(no, "CASConstructionProcess::Fill", 32, 14, 96, 16, 0, 0, 0, 1);
	PN_ADD(no, "CASConstructionProcess::Fill", 58, 30, 44, 44, 0, 0, 0, 1);
	pn_build(no);
}
static void pn_expect_area(int no, const char *what)
{
	static const uint8_t base[4] = { 255, 128, 0, 0 };
	pn_expect_rects(no, 160, 80, base, PN_RECTS({ 32, 14, 96, 16, { 0, 0, 0, 1 } }, { 58, 30, 44, 44, { 0, 0, 0, 1 } }), what);
	PN_CHECK(pn_nr_ops(no) == 4, "%s: %d operations, want 4", what, pn_nr_ops(no));
}

/* --- PN3 --- */
static void pn_area(void)
{
	cn_gl_setup();
	br_input_setup();
	struct ex_tree *kids;
	struct ex *e = lb_activity("PanelArea", 1, &kids);
	/* Area: z 7, アルファ 128, the pixel decision, 點擊許可 1. */
	struct cp_attr area_at = { 800, 250, 7, 1, 0, .alpha = 128, .pixel = true };
	kids->children[0] = cp_node("Area", &area_at, cp_area_info(0));
	int act = cn_load("PanelArea", e);
	pn_failures += cn_failures;
	int area = bu_number(act, "Area");
	heap_unref(act);
	PN_CHECK(area > 0, "Area has no parts number");
	if (area <= 0)
		return;
	cp_register_event(area, 7, 70);		/* DungeonSelector@0 adds a MouseLClickEvent */
	pn_draw_area(area);
	const int UNDER = 9961;
	br_box(UNDER, 790, 240, 180, 100, true);
	PE_UpdateComponent(0);
	pn_expect_area(area, "PN3 Area");
	uint8_t *px = pn_pixels(area, 160, 80, "PN3 Area");
	if (px) {
		int a0 = 0, a1 = 0, other = 0;
		for (int i = 0; i < 160 * 80; i++) {
			uint8_t a = px[i * 4 + 3];
			if (a == 0) a0++;
			else if (a == 1) a1++;
			else other++;
		}
		free(px);
		/* 96 * 16 + 44 * 44 pixels of alpha 1. */
		PN_CHECK(a0 == 9328 && a1 == 3472 && !other, "PN3 Area alpha 0: %d, 1: %d, other: %d; want 9328, 3472, 0", a0, a1, other);
	}
	int type = BU_PE("GetComponentType", BU_I(area), BU_I(1)).i;
	PN_CHECK(type == 26, "PN3 Area: GetComponentType %d, want 26", type);
	/* An alpha of 1 is hit, an alpha of 0 is not. */
	cp_expect_press(878, 300, area, "PN3 Area, alpha 1 in the box");
	cp_expect_press(840, 270, area, "PN3 Area, alpha 1 in the name's box");
	cp_expect_press(810, 320, UNDER, "PN3 the button behind Area, through alpha 0");
	cp_expect_press(950, 255, UNDER, "PN3 the button behind Area, through its corner");
	pn_failures += cp_failures;
	printf("PN3 the script's command 6: %s\n", pn_failures ? "FAIL" : "ok");
}

/* The operation at `index` (0 the first) of the normal state, or NULL. */
static struct parts_cp_op *pn_op_at(int no, int index)
{
	struct parts *p = parts_try_get(no);
	struct parts_cp_op *op = NULL;
	if (p && p->states[0].type == PARTS_CONSTRUCTION_PROCESS) {
		TAILQ_FOREACH(op, &p->states[0].cproc.ops, entry) {
			if (!index--)
				break;
		}
	}
	return op;
}
static struct parts_cp_op *pn_last_op(int no)
{
	struct parts *p = parts_try_get(no);
	struct parts_cp_op *op, *last = NULL;
	if (p && p->states[0].type == PARTS_CONSTRUCTION_PROCESS) {
		TAILQ_FOREACH(op, &p->states[0].cproc.ops, entry)
			last = op;
	}
	return last;
}

/* --- PN4 --- */

/* One fill added with its ArrayInt set by hand after a real factory made
 * the process: [6],[7],[10],[11] the rectangle, [12..15] the colour, [30]
 * 全體 (what CASConstructionProcess@SetFill.. write). */
static void pn_add_fill(int no, int cmd, int x, int y, int w, int h, int r, int g, int b, int a, int full)
{
	int proc = pn_process("CASConstructionProcess::Create", 2, (int[]){ 0, 0 });
	struct page *ints = pn_member_array(proc, "ArrayInt");
	if (ints->nr_vars < 40) {
		printf("PANEL ArrayInt has %d items\n", ints->nr_vars);
		fflush(stdout);
		_exit(CN_ENV_EXIT);
	}
	const int v[][2] = { { 0, cmd }, { 6, x }, { 7, y }, { 10, w }, { 11, h }, { 12, r }, { 13, g }, { 14, b }, { 15, a }, { 30, full } };
	for (unsigned i = 0; i < sizeof v / sizeof v[0]; i++)
		ints->values[v[i][0]].i = v[i][1];
	pn_add(no, proc);
	heap_unref(proc);
}

static void pn_full(void)
{
	cn_gl_setup();
	static const uint8_t black[4] = { 0, 0, 0, 255 };
	int no = 9980;
	/* Command 6, 全體 with a rectangle: all of the surface. */
	PN_ADD(no, "CASConstructionProcess::Create", 32, 24);
	pn_add_fill(no, 6, 5, 6, 12, 8, 10, 20, 30, 40, 1);
	pn_build(no);
	pn_expect_flat(no, 32, 24, 10, 20, 30, 40, "PN4 command 6 with [30] 1 and a rectangle");
	/* [30] 2 is 全體 as well: the HLL stores [30] != 0 (0x59aab4). */
	no++;
	PN_ADD(no, "CASConstructionProcess::Create", 32, 24);
	pn_add_fill(no, 6, 5, 6, 12, 8, 10, 20, 30, 40, 2);
	pn_build(no);
	pn_expect_flat(no, 32, 24, 10, 20, 30, 40, "PN4 command 6 with [30] 2");
	/* Without 全體 a width or height of 0 is an empty rectangle. */
	no++;
	PN_ADD(no, "CASConstructionProcess::Create", 32, 24);
	pn_add_fill(no, 6, 5, 6, 0, 8, 10, 20, 30, 40, 0);
	pn_add_fill(no, 6, 5, 6, 12, 0, 10, 20, 30, 40, 0);
	pn_add_fill(no, 3, 5, 6, 0, 8, 10, 20, 30, 0, 0);
	pn_add_fill(no, 5, 5, 6, 0, 8, 0, 0, 0, 40, 0);
	pn_add_fill(no, 4, 5, 6, 0, 8, 10, 20, 30, 255, 0);
	pn_build(no);
	pn_expect_flat(no, 32, 24, 0, 0, 0, 255, "PN4 fills without width or height");
	/* A negative size extends the other way (0x5abed0): x 8..19, y 2..5. */
	no++;
	PN_ADD(no, "CASConstructionProcess::Create", 32, 24);
	pn_add_fill(no, 6, 20, 6, -12, -4, 10, 20, 30, 40, 0);
	pn_build(no);
	pn_expect_rects(no, 32, 24, black, PN_RECTS({ 8, 2, 12, 4, { 10, 20, 30, 40 } }), "PN4 command 6 with a negative size");
	struct parts_cp_op *op = pn_last_op(no);
	PN_CHECK(op && op->fill.x == 8 && op->fill.y == 2 && op->fill.w == 12 && op->fill.h == 4,
		 "PN4 a negative size is kept as (%d,%d,%d,%d), want (8,2,12,4)", op ? op->fill.x : 0, op ? op->fill.y : 0,
		 op ? op->fill.w : 0, op ? op->fill.h : 0);
	/* So do the other fills: command 3 over x 8..19, y 2..5, command 5
	 * over x 24..29, y 16..19, command 4 over x 4..9, y 16..19. */
	no++;
	PN_ADD(no, "CASConstructionProcess::Create", 32, 24);
	pn_add_fill(no, 3, 20, 6, -12, -4, 10, 20, 30, 0, 0);
	pn_add_fill(no, 5, 30, 20, -6, -4, 0, 0, 0, 40, 0);
	pn_add_fill(no, 4, 10, 20, -6, -4, 100, 50, 200, 128, 0);
	pn_build(no);
	pn_expect_rects(no, 32, 24, black, PN_RECTS({ 8, 2, 12, 4, { 10, 20, 30, 255 } }, { 24, 16, 6, 4, { 0, 0, 0, 40 } },
						   { 4, 16, 6, 4, { 50, 25, 100, 255 } }), "PN4 commands 3, 5 and 4 with a negative size");
	static const int negative[3][5] = { { 3, 8, 2, 12, 4 }, { 5, 24, 16, 6, 4 }, { 4, 4, 16, 6, 4 } };
	for (int i = 0; i < 3; i++) {
		op = pn_op_at(no, i + 1);
		PN_CHECK(op && op->fill.x == negative[i][1] && op->fill.y == negative[i][2] && op->fill.w == negative[i][3]
			 && op->fill.h == negative[i][4], "PN4 command %d: a negative size is kept as (%d,%d,%d,%d), want (%d,%d,%d,%d)",
			 negative[i][0], op ? op->fill.x : 0, op ? op->fill.y : 0, op ? op->fill.w : 0, op ? op->fill.h : 0,
			 negative[i][1], negative[i][2], negative[i][3], negative[i][4]);
	}
	/* A rectangle reaching past the surface is clipped. */
	no++;
	PN_ADD(no, "CASConstructionProcess::Create", 32, 24);
	pn_add_fill(no, 6, 28, 20, 100, 100, 10, 20, 30, 40, 0);
	pn_add_fill(no, 6, -3, -3, 5, 5, 1, 2, 3, 4, 0);
	pn_build(no);
	pn_expect_rects(no, 32, 24, black, PN_RECTS({ 28, 20, 4, 4, { 10, 20, 30, 40 } }, { 0, 0, 2, 2, { 1, 2, 3, 4 } }),
			"PN4 command 6 past the surface");
	/* Commands 3 (RGB), 5 (alpha) and 4 (a blend that keeps the alpha), in
	 * a rectangle and with 全體; (100,50,200) at 128/255 over black is
	 * (50,25,100). */
	no++;
	PN_ADD(no, "CASConstructionProcess::Create", 32, 24);
	pn_add_fill(no, 3, 2, 2, 6, 4, 10, 20, 30, 0, 0);
	pn_add_fill(no, 5, 10, 2, 6, 4, 0, 0, 0, 40, 0);
	pn_add_fill(no, 4, 18, 2, 6, 4, 100, 50, 200, 128, 0);
	pn_build(no);
	pn_expect_rects(no, 32, 24, black, PN_RECTS({ 2, 2, 6, 4, { 10, 20, 30, 255 } }, { 10, 2, 6, 4, { 0, 0, 0, 40 } },
						   { 18, 2, 6, 4, { 50, 25, 100, 255 } }), "PN4 commands 3, 5 and 4 in a rectangle");
	no++;
	PN_ADD(no, "CASConstructionProcess::Create", 32, 24);
	pn_add_fill(no, 3, 2, 2, 6, 4, 10, 20, 30, 0, 1);
	pn_add_fill(no, 5, 2, 2, 6, 4, 0, 0, 0, 40, 1);
	pn_build(no);
	pn_expect_flat(no, 32, 24, 10, 20, 30, 40, "PN4 commands 3 and 5 with [30] 1 and a rectangle");
	no++;
	PN_ADD(no, "CASConstructionProcess::Create", 32, 24);
	pn_add_fill(no, 4, 2, 2, 6, 4, 100, 50, 200, 128, 1);
	pn_build(no);
	pn_expect_flat(no, 32, 24, 50, 25, 100, 255, "PN4 command 4 with [30] 1 and a rectangle");
	/* The real factories: Fill (r,g,b), Fill (a) and Fill (r,g,b,a) set
	 * 全體 and no rectangle; the ones with a rectangle do not. */
	no++;
	PN_ADD(no, "CASConstructionProcess::Create", 32, 24);
	PN_ADD(no, "CASConstructionProcess::Fill", 10, 20, 30);
	PN_ADD(no, "CASConstructionProcess::Fill", 40);
	pn_build(no);
	pn_expect_flat(no, 32, 24, 10, 20, 30, 40, "PN4 Fill (r,g,b) and Fill (a)");
	no++;
	PN_ADD(no, "CASConstructionProcess::Create", 32, 24);
	PN_ADD(no, "CASConstructionProcess::Fill", 2, 2, 6, 4, 10, 20, 30);
	PN_ADD(no, "CASConstructionProcess::Fill", 10, 2, 6, 4, 40);
	PN_ADD(no, "CASConstructionProcess::FillAlphaColor", 18, 2, 6, 4, 100, 50, 200, 128);
	pn_build(no);
	pn_expect_rects(no, 32, 24, black, PN_RECTS({ 2, 2, 6, 4, { 10, 20, 30, 255 } }, { 10, 2, 6, 4, { 0, 0, 0, 40 } },
						   { 18, 2, 6, 4, { 50, 25, 100, 255 } }), "PN4 the factories with a rectangle");
	/* On a surface without alpha command 6 writes the pixels only
	 * (0x4fc490: "Ａ要素を無視して"); read back, such a surface is opaque. */
	no++;
	PN_ADD(no, "CASConstructionProcess::CreatePixelOnly", 32, 24);
	PN_ADD(no, "CASConstructionProcess::Fill", 10, 20, 30, 40);
	pn_build(no);
	pn_expect_flat(no, 32, 24, 10, 20, 30, 255, "PN4 command 6 on a surface without alpha");
	printf("PN4 the rectangle of the script's fills: %s\n", pn_failures ? "FAIL" : "ok");
}

/* --- PN5, PN6 --- */

/* The ArrayInt the commands of PN5 and PN6 are added with. */
static const int pn_ints[40] = {
	0, 1, 3, 4, 20, 10,		/* コマンド, 補間, the source rectangle */
	5, 6, 31, 32, 12, 8,		/* 先矩形 X, Y, X2, Y2, W, H */
	10, 20, 30, 40, 50, 60, 70, 80,	/* 色１, 色２ */
	2, 3,				/* character and line spacing */
	1, 24, 11, 12, 13, 14, 15, 16,	/* font type, size, colour, edge colour */
	0, 4, 7, 7, 2, 0, 0, 0, 0, 90,	/* 全體, ブラー, 半徑, 線の幅, .. */
};

/* Adds command `cmd` with pn_ints, ArrayFloat (1.5, 2.5) and "Aa" as the
 * text. The CG name stays empty, so the commands that take a CG add nothing
 * before their binding is reached. Returns the operations the parts then
 * has. */
static int pn_add_command(int no, int cmd)
{
	int proc = pn_process("CASConstructionProcess::Create", 2, (int[]){ 0, 0 });
	struct page *ints = pn_member_array(proc, "ArrayInt"), *floats = pn_member_array(proc, "ArrayFloat");
	struct page *strs = pn_member_array(proc, "ArrayString");
	if (ints->nr_vars < 40 || floats->nr_vars < 2 || strs->nr_vars < 2) {
		printf("PANEL CASConstructionProcess arrays have %d, %d and %d items\n", ints->nr_vars, floats->nr_vars, strs->nr_vars);
		fflush(stdout);
		_exit(CN_ENV_EXIT);
	}
	for (int i = 0; i < 40; i++)
		ints->values[i].i = pn_ints[i];
	ints->values[0].i = cmd;
	floats->values[0].f = 1.5f;
	floats->values[1].f = 2.5f;
	int old = strs->values[0].i;
	strs->values[0].i = sf_str("Aa", -1);
	if (old > 0)
		heap_unref(old);
	pn_add(no, proc);
	heap_unref(proc);
	struct parts *p = parts_try_get(no);
	return p && p->states[0].type == PARTS_CONSTRUCTION_PROCESS ? cn_nr_ops(p) : 0;
}

static void pn_text(void)
{
	cn_gl_setup();
	static const struct { int cmd; enum parts_cp_op_type type; } cmds[] = {
		{ 7, PARTS_CP_DRAW_TEXT }, { 8, PARTS_CP_COPY_TEXT }, { 23, PARTS_CP_DRAW_TEXT }, { 24, PARTS_CP_DRAW_TEXT },
	};
	for (unsigned i = 0; i < sizeof cmds / sizeof cmds[0]; i++) {
		int no = 9990 + i;
		int n = pn_add_command(no, cmds[i].cmd);
		struct parts_cp_op *op = n == 1 ? TAILQ_FIRST(&parts_get(no)->states[0].cproc.ops) : NULL;
		PN_CHECK(op && op->type == cmds[i].type, "PN5 command %d: %d operation(s), type %d; want 1 of type %d", cmds[i].cmd, n,
			 op ? (int)op->type : -1, cmds[i].type);
		if (!op || op->type != cmds[i].type)
			continue;
		struct parts_cp_text *t = &op->text;
		PN_CHECK(t->x == 5 && t->y == 6 && t->text && !strcmp(t->text->text, "Aa") && t->line_space == 3
			 && t->style.font_spacing == 2 && t->style.face == 1,
			 "PN5 command %d: at (%d,%d), text %s, line space %d, spacing %g, face %d; want (5,6), Aa, 3, 2, 1", cmds[i].cmd,
			 t->x, t->y, t->text ? t->text->text : "(none)", t->line_space, t->style.font_spacing, t->style.face);
		PN_CHECK(t->style.size == 24, "PN5 command %d: font size %g, want 24 ([23])", cmds[i].cmd, t->style.size);
		PN_CHECK(t->style.color.r == 11 && t->style.color.g == 12 && t->style.color.b == 13,
			 "PN5 command %d: colour (%d,%d,%d), want (11,12,13) ([24..26])", cmds[i].cmd, t->style.color.r,
			 t->style.color.g, t->style.color.b);
		PN_CHECK(t->style.edge_color.r == 14 && t->style.edge_color.g == 15 && t->style.edge_color.b == 16,
			 "PN5 command %d: edge colour (%d,%d,%d), want (14,15,16) ([27..29])", cmds[i].cmd, t->style.edge_color.r,
			 t->style.edge_color.g, t->style.edge_color.b);
		PN_CHECK(t->style.bold_width == 1.5f && t->style.edge_left == 2.5f, "PN5 command %d: weight %g, edge %g; want 1.5, 2.5",
			 cmds[i].cmd, t->style.bold_width, t->style.edge_left);
	}
	printf("PN5 the font of the text commands: %s\n", pn_failures ? "FAIL" : "ok");
}

/* What a command adds for pn_ints: no operation (nr 0), or one of `type`
 * with these fields (the rectangle and colour of a fill, or w, h of a
 * surface). Taken on the tree before the fix. */
struct pn_guard {
	int cmd, nr;
	enum parts_cp_op_type type;
	int v[8];
};
static const struct pn_guard pn_guards[] = {
	{ 0, 1, PARTS_CP_CREATE, { 12, 8 } },
	{ 1, 1, PARTS_CP_CREATE_PIXEL_ONLY, { 12, 8 } },
	{ 2, 0 },	/* the CG name is empty (so for 11 to 14, 19 to 22 and 29) */
	{ 3, 1, PARTS_CP_FILL, { 5, 6, 12, 8, 10, 20, 30, 255 } },
	{ 4, 1, PARTS_CP_FILL_ALPHA_COLOR, { 5, 6, 12, 8, 10, 20, 30, 40 } },
	{ 5, 1, PARTS_CP_FILL_AMAP, { 5, 6, 12, 8, 0, 0, 0, 40 } },
	{ 9, 1, PARTS_CP_FILL_ALPHA_COLOR, { 5, 6, 12, 8, 10, 20, 30, 40 } },
	{ 10, 1, PARTS_CP_DRAW_RECT, { 5, 6, 12, 8, 10, 20, 30, 255 } },
	{ 11, 0 }, { 12, 0 }, { 13, 0 }, { 14, 0 },
	{ 15, 0 }, { 16, 0 }, { 17, 0 },
	{ 18, 1, PARTS_CP_FILL_ALPHA_COLOR, { 5, 6, 26, 26, 10, 20, 30, 40 } },
	{ 19, 0 }, { 20, 0 }, { 21, 0 }, { 22, 0 },
	{ 25, 1, PARTS_CP_FILL_AMAP, { 5, 6, 12, 8, 0, 0, 0, 40 } },
	{ 26, 1, PARTS_CP_FILL_AMAP, { 5, 6, 12, 8, 0, 0, 0, 40 } },
	{ 27, 0 }, { 28, 0 }, { 29, 0 },
	{ 30, 1, PARTS_CP_FILL_ALPHA_COLOR, { 5, 6, 26, 26, 10, 20, 30, 40 } },
	{ 31, 0 }, { 52, 0 }, { 56, 0 },
	{ 57, 1, PARTS_CP_FILL_ALPHA_COLOR, { 5, 6, 12, 8, 10, 20, 30, 40 } },
	{ 58, 0 }, { 89, 0 }, { 95, 0 }, { 97, 0 }, { 102, 0 }, { 103, 0 }, { 106, 0 }, { 107, 0 }, { 120, 0 }, { 121, 0 },
	{ 122, 0 }, { 128, 0 }, { 132, 0 }, { 133, 0 }, { 200, 0 },
};

static void pn_other_commands(void)
{
	cn_gl_setup();
	for (unsigned i = 0; i < sizeof pn_guards / sizeof pn_guards[0]; i++) {
		const struct pn_guard *g = &pn_guards[i];
		int no = 10000 + g->cmd;
		int n = pn_add_command(no, g->cmd);
		PN_CHECK(n == g->nr, "PN6 command %d added %d operation(s), want %d", g->cmd, n, g->nr);
		if (n != 1 || g->nr != 1)
			continue;
		struct parts_cp_op *op = TAILQ_FIRST(&parts_get(no)->states[0].cproc.ops);
		PN_CHECK(op->type == g->type, "PN6 command %d: operation type %d, want %d", g->cmd, op->type, g->type);
		if (op->type != g->type)
			continue;
		if (g->type == PARTS_CP_CREATE || g->type == PARTS_CP_CREATE_PIXEL_ONLY) {
			PN_CHECK(op->create.w == g->v[0] && op->create.h == g->v[1], "PN6 command %d: %dx%d, want %dx%d", g->cmd,
				 op->create.w, op->create.h, g->v[0], g->v[1]);
		} else {
			const struct parts_cp_fill *f = &op->fill;
			const int got[8] = { f->x, f->y, f->w, f->h, f->r, f->g, f->b, f->a };
			PN_CHECK(!memcmp(got, g->v, sizeof got),
				 "PN6 command %d: (%d,%d,%d,%d) colour (%d,%d,%d,%d), want (%d,%d,%d,%d) colour (%d,%d,%d,%d)", g->cmd,
				 got[0], got[1], got[2], got[3], got[4], got[5], got[6], got[7], g->v[0], g->v[1], g->v[2], g->v[3],
				 g->v[4], g->v[5], g->v[6], g->v[7]);
		}
	}
	printf("PN6 the other commands: %s\n", pn_failures ? "FAIL" : "ok");
}

/* --- PN7 --- */

/* A parts as the tree before the fix built a panel: Create and the blending
 * fill over all of it, once for each colour SetPanelColor was given. */
static void pn_old_panel(int no, int w, int h, int ncolors, const int (*colors)[4])
{
	parts_get(no)->component_type = 14;
	PE_AddCreateToPartsConstructionProcess(no, w, h, 1);
	for (int i = 0; i < ncolors; i++)
		PE_AddFillAlphaColorToPartsConstructionProcess(no, 0, 0, w, h, colors[i][0], colors[i][1], colors[i][2], colors[i][3], 1);
	PE_BuildPartsConstructionProcess(no, 1);
}

static void pn_save(void)
{
	cn_gl_setup();
	struct ex_tree *kids;
	struct ex *e = lb_activity("PanelSave", 4, &kids);
	struct lb_place pl = { 5, 6, 1 };
	struct cp_attr area_at = { 800, 250, 7, 1, 0, .alpha = 128, .pixel = true };
	kids->children[0] = pn_panel("Dimmer", 64, 32, 0, 0, 0, 128, false);
	kids->children[1] = pn_panel("Web", 41, 7, 255, 255, 130, 200, false);
	kids->children[2] = cp_node("Area", &area_at, cp_area_info(0));
	kids->children[3] = cn_parts("Timer", &pl, CN_N(cn_timer), 0);
	int act = cn_load("PanelSave", e);
	pn_failures += cn_failures;
	int dim = bu_number(act, "Dimmer"), web = bu_number(act, "Web"), area = bu_number(act, "Area"), timer = bu_number(act, "Timer");
	heap_unref(act);
	PN_CHECK(dim > 0 && web > 0 && area > 0 && timer > 0, "PanelSave: parts numbers %d, %d, %d, %d", dim, web, area, timer);
	if (dim <= 0 || web <= 0 || area <= 0 || timer <= 0)
		return;
	pn_draw_area(area);
	pn_set_color(web, 255, 255, 130, 100);
	PE_UpdateComponent(0);
	struct page *saved = NULL;
	PN_CHECK(PE_Save(&saved), "PE_Save failed");
	if (!saved)
		return;
	PN_CHECK(PE_Load(&saved), "PE_Load failed");
	PE_UpdateComponent(0);
	pn_expect_flat(dim, 64, 32, 0, 0, 0, 128, "PN7 Dimmer after loading");
	pn_expect_flat(web, 41, 7, 255, 255, 130, 100, "PN7 Web after loading");
	pn_expect_color(web, 255, 255, 130, 100, "PN7 Web after loading");
	pn_expect_area(area, "PN7 Area after loading");
	cn_expect(timer, 262, 22, CN_TIMER_FNV, 0, NULL, "PN7 Timer after loading");
	pn_failures += cn_failures;
	PN_CHECK(pn_nr_ops(dim) == 2 && pn_nr_ops(web) == 2, "PN7 %d and %d operations after loading, want 2 and 2", pn_nr_ops(dim),
		 pn_nr_ops(web));
	/* The loaded panel still takes a size and keeps its colour, and a
	 * colour and keeps its size. */
	pn_set_size(web, 10, 3);
	pn_expect_flat(web, 10, 3, 255, 255, 130, 100, "PN7 Web resized after loading");
	pn_set_size(web, 41, 7);
	pn_set_color(dim, 1, 2, 3, 4);
	pn_expect_flat(dim, 64, 32, 1, 2, 3, 4, "PN7 Dimmer recoloured after loading");
	pn_set_color(dim, 0, 0, 0, 128);
	struct page *again = NULL;
	PN_CHECK(PE_Save(&again), "the second PE_Save failed");
	if (again) {
		bool same = again->nr_vars == saved->nr_vars;
		for (int i = 0; same && i < again->nr_vars; i++)
			same = again->values[i].i == saved->values[i].i;
		PN_CHECK(same, "saving the loaded parts gave another page (%d and %d values)", saved->nr_vars, again->nr_vars);
		ca_free_buffer(again);
	}
	ca_free_buffer(saved);

	/* A page from before the fix. The writer has not changed, so the page
	 * is the current writer's with the operations the old tree made. */
	const int LOW_LEVEL = 10208;
	const int OLD_DIM = 10201, OLD_WEB = 10202, OLD_PILED = 10203, OLD_OPAQUE = 10204, NOT_PANEL = 10205, PARTIAL = 10206,
		SIZE_ONLY = 10207;
	pn_old_panel(OLD_DIM, 64, 32, 1, (const int[][4]){ { 0, 0, 0, 128 } });
	pn_old_panel(OLD_WEB, 41, 7, 1, (const int[][4]){ { 255, 255, 130, 200 } });
	pn_old_panel(OLD_PILED, 10, 10, 3, (const int[][4]){ { 0, 0, 0, 128 }, { 200, 100, 50, 64 }, { 9, 8, 7, 160 } });
	pn_old_panel(OLD_OPAQUE, 10, 10, 1, (const int[][4]){ { 220, 220, 220, 255 } });
	/* The old SetPanelSize left Create alone. */
	pn_old_panel(SIZE_ONLY, 10, 10, 0, NULL);
	/* A construction parts with the blending fill, and a panel whose fill
	 * is not all of it: not the old panel's shape. */
	PE_AddCreateToPartsConstructionProcess(NOT_PANEL, 10, 10, 1);
	PE_AddFillAlphaColorToPartsConstructionProcess(NOT_PANEL, 0, 0, 10, 10, 100, 50, 200, 128, 1);
	PE_BuildPartsConstructionProcess(NOT_PANEL, 1);
	parts_get(PARTIAL)->component_type = 14;
	PE_AddCreateToPartsConstructionProcess(PARTIAL, 10, 10, 1);
	PE_AddFillAlphaColorToPartsConstructionProcess(PARTIAL, 2, 2, 4, 4, 100, 50, 200, 128, 1);
	PE_BuildPartsConstructionProcess(PARTIAL, 1);
	/* A low-level parts whose normal state is a construction (types 18
	 * and 26: a pactex 構築部件 the script fills), with that same shape. */
	parts_get(LOW_LEVEL)->component_type = 18;
	parts_get(LOW_LEVEL)->component_state_type[0] = 26;
	PE_AddCreateToPartsConstructionProcess(LOW_LEVEL, 10, 10, 1);
	PE_AddFillAlphaColorToPartsConstructionProcess(LOW_LEVEL, 0, 0, 10, 10, 100, 50, 200, 128, 1);
	PE_BuildPartsConstructionProcess(LOW_LEVEL, 1);
	static const uint8_t black[4] = { 0, 0, 0, 255 };
	pn_expect_flat(OLD_DIM, 64, 32, 0, 0, 0, 255, "PN7 the old Dimmer before saving");
	struct page *old = NULL;
	PN_CHECK(PE_Save(&old), "PE_Save failed (old page)");
	if (!old)
		return;
	PN_CHECK(ca_save_version(old) >= 7, "XPE v%d, want 7 or later", ca_save_version(old));
	PN_CHECK(PE_Load(&old), "PE_Load failed (old page)");
	PE_UpdateComponent(0);
	pn_expect_flat(OLD_DIM, 64, 32, 0, 0, 0, 128, "PN7 an old Dimmer");
	pn_expect_flat(OLD_WEB, 41, 7, 255, 255, 130, 200, "PN7 an old Web");
	pn_expect_flat(OLD_PILED, 10, 10, 9, 8, 7, 160, "PN7 an old panel with three colours");
	pn_expect_flat(OLD_OPAQUE, 10, 10, 220, 220, 220, 255, "PN7 an old opaque panel");
	pn_expect_flat(SIZE_ONLY, 10, 10, 0, 0, 0, 255, "PN7 an old panel with a size only");
	PN_CHECK(pn_nr_ops(OLD_DIM) == 2 && pn_nr_ops(OLD_PILED) == 2 && pn_nr_ops(SIZE_ONLY) == 1,
		 "PN7 old panels: %d, %d and %d operations, want 2, 2 and 1", pn_nr_ops(OLD_DIM), pn_nr_ops(OLD_PILED),
		 pn_nr_ops(SIZE_ONLY));
	pn_expect_color(OLD_PILED, 9, 8, 7, 160, "PN7 an old panel with three colours");
	pn_set_size(OLD_DIM, 8, 8);
	pn_expect_flat(OLD_DIM, 8, 8, 0, 0, 0, 128, "PN7 an old Dimmer resized");
	pn_set_color(OLD_WEB, 5, 6, 7, 8);
	pn_expect_flat(OLD_WEB, 41, 7, 5, 6, 7, 8, "PN7 an old Web recoloured");
	pn_expect_flat(NOT_PANEL, 10, 10, 50, 25, 100, 255, "PN7 a construction parts with the blending fill");
	pn_expect_rects(PARTIAL, 10, 10, black, PN_RECTS({ 2, 2, 4, 4, { 50, 25, 100, 255 } }), "PN7 a panel with a partial blending fill");
	struct parts_cp_op *op = pn_last_op(NOT_PANEL);
	PN_CHECK(op && op->type == PARTS_CP_FILL_ALPHA_COLOR, "PN7 the construction parts' fill became type %d", op ? (int)op->type : -1);
	pn_expect_flat(LOW_LEVEL, 10, 10, 50, 25, 100, 255, "PN7 a low-level construction parts with the blending fill");
	op = pn_last_op(LOW_LEVEL);
	int low_type = BU_PE("GetComponentType", BU_I(LOW_LEVEL), BU_I(1)).i;
	PN_CHECK(op && op->type == PARTS_CP_FILL_ALPHA_COLOR && low_type == 26,
		 "PN7 the low-level construction parts: fill type %d, GetComponentType %d; want %d, 26", op ? (int)op->type : -1,
		 low_type, PARTS_CP_FILL_ALPHA_COLOR);
	ca_free_buffer(old);
	printf("PN7 save and load: %s\n", pn_failures ? "FAIL" : "ok");
}

/* --- PN8 --- */
static void pn_guards_other(void)
{
	cn_gl_setup();
	/* A pactex 構築部件 and a script's command 4 are not panels. */
	struct ex_tree *kids;
	struct ex *e = lb_activity("PanelGuards", 2, &kids);
	struct lb_place pl = { 5, 6, 1 };
	kids->children[0] = cn_parts("Timer", &pl, CN_N(cn_timer), 0);
	kids->children[1] = cn_parts("Round", &pl, CN_N(cn_round128), 0);
	int act = cn_load("PanelGuards", e);
	int timer = cn_no(act, "Timer"), round = cn_no(act, "Round");
	heap_unref(act);
	cn_expect(timer, 262, 22, CN_TIMER_FNV, 0, NULL, "PN8 a pactex construction parts");
	cn_expect(round, 128, 40, CN_ROUND128_FNV, 0, NULL, "PN8 the count label base");
	pn_failures += cn_failures;
	const int BLEND = 10301;
	PN_ADD(BLEND, "CASConstructionProcess::Create", 16, 8);
	PN_ADD(BLEND, "CASConstructionProcess::FillAlphaColor", 0, 0, 16, 8, 100, 50, 200, 128);
	PN_ADD(BLEND, "CASConstructionProcess::FillAlphaColor", 4, 2, 4, 4, 255, 255, 255, 255);
	pn_build(BLEND);
	pn_expect_rects(BLEND, 16, 8, (const uint8_t[4]){ 50, 25, 100, 255 }, PN_RECTS({ 4, 2, 4, 4, { 255, 255, 255, 255 } }),
			"PN8 the script's command 4");

	/* An AIN before version 14: the construction API as it was, and a
	 * load that leaves a Create + blending fill as it was saved. Such an
	 * AIN writes XPE version 3, which has no component type: every parts
	 * loads with type 0, whatever it had, so this does not reach the
	 * `ain->version >= 14` beside the conversion in save.c (a precaution
	 * no case guards). */
	ain->version = 13;
	const int V13 = 10311, V13_FULL = 10312;
	PE_AddCreateToPartsConstructionProcess(V13, 16, 8, 1);
	PE_AddFillAlphaColorToPartsConstructionProcess(V13, 0, 0, 16, 8, 100, 50, 200, 128, 1);
	PE_AddFillWithAlphaToPartsConstructionProcess(V13, 4, 2, 4, 4, 1, 2, 3, 4, 1);
	PE_AddFillAMapToPartsConstructionProcess(V13, 10, 2, 4, 4, 77, 1);
	PE_AddFillToPartsConstructionProcess(V13, 0, 7, 16, 1, 9, 9, 9, 1);
	PE_BuildPartsConstructionProcess(V13, 1);
	PE_AddCreateToPartsConstructionProcess(V13_FULL, 10, 10, 1);
	PE_AddFillAlphaColorToPartsConstructionProcess(V13_FULL, 0, 0, 10, 10, 100, 50, 200, 128, 1);
	PE_BuildPartsConstructionProcess(V13_FULL, 1);
	static const struct pn_rect v13_rects[] = { { 4, 2, 4, 4, { 1, 2, 3, 4 } }, { 10, 2, 4, 4, { 50, 25, 100, 77 } },
		{ 0, 7, 16, 1, { 9, 9, 9, 255 } } };
	pn_expect_rects(V13, 16, 8, (const uint8_t[4]){ 50, 25, 100, 255 }, 3, v13_rects, "PN8 version 13 construction");
	struct page *saved = NULL;
	PN_CHECK(PE_Save(&saved), "PE_Save failed (version 13)");
	if (saved) {
		PN_CHECK(PE_Load(&saved), "PE_Load failed (version 13)");
		pn_expect_rects(V13, 16, 8, (const uint8_t[4]){ 50, 25, 100, 255 }, 3, v13_rects, "PN8 version 13 after loading");
		pn_expect_flat(V13_FULL, 10, 10, 50, 25, 100, 255, "PN8 version 13 Create + blending fill after loading");
		struct parts_cp_op *op = pn_last_op(V13_FULL);
		PN_CHECK(op && op->type == PARTS_CP_FILL_ALPHA_COLOR, "PN8 version 13: the fill became type %d", op ? (int)op->type : -1);
		ca_free_buffer(saved);
	}
	ain->version = 14;
	printf("PN8 guards: %s\n", pn_failures ? "FAIL" : "ok");
}

static int test_panel(void)
{
	void (*cases[])(void) = { pn_pactex, pn_hll, pn_area, pn_full, pn_text, pn_other_commands, pn_save, pn_guards_other };
	const char *names[] = { "PN1", "PN2", "PN3", "PN4", "PN5", "PN6", "PN7", "PN8" };
	enum { N = sizeof(cases) / sizeof(cases[0]) };
	int failed = 0, env = 0;
	for (int i = 0; i < N; i++) {
		int r = cn_fork_case(names[i], cases[i], &pn_failures);
		failed += r == 1;
		env += r == 2;
	}
	if (env) {
		printf("PANEL ENVIRONMENT FAILURE: %d of %d cases could not run, %d failed\n", env, N, failed);
		return 2;
	}
	printf("PANEL %s: %d of %d cases failed\n", failed ? "FAIL" : "PASS", failed, N);
	return failed ? 1 : 0;
}
