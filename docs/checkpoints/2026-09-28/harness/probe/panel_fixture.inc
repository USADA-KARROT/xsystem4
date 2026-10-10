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
 *     The getters (0x597260..) return the stored values. Every panel
 *     function takes the widget through 0x53e290, which looks the component
 *     up as 0x53d920 does (-> 0x53a0b0 -> 0x539c80, 0x578ee0): a number of 0
 *     or less has none (0x539cb0: the setters do nothing, the getters
 *     return 0), any other number without a component gets one (0x578f0e ->
 *     0x578cb0), and a component of another type, a new one included,
 *     becomes a panel with the constructor's values (0x536ff0);
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
 *       and a getter makes it a panel as a setter does; a number above 0
 *       without a parts gets one, for a setter and a getter alike, and 0 or
 *       a negative number none; the real CPanelParts@ColorA::set, which reads the other
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
 *       commands 10 add an operation whose fields are compared; 18 are
 *       filters or have no binding and add none; the 10 that take a CG (2,
 *       11 to 14, 19 to 22, 29) are added with an empty CG name, for which
 *       nothing is added before the binding is reached: those are not
 *       guarded. Seven were given their own operation with construction-cmds
 *       (2026-10-09), which checks what they draw: 25 and 26 (they were a
 *       fill of the alpha; the original multiplies it by a gradation,
 *       0x4ff740 / 0x4ff8d0), 27 and 28 (nothing; the blurs 0x4ffa60 /
 *       0x4ffba0), 52 (nothing; the ring 0x502570), 102 and 106 (nothing;
 *       the discs 0x507640 / 0x507d60).
 *       For those the table holds the operation's type as a number, since
 *       the tree before that change has no name for it.
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
 * アルファグラデーション (2026-10-11; before, a panel's surface was flat and
 * the eight functions were empty):
 *   - the widget keeps the widths of the fades of its top, bottom, left and
 *     right edges at +0xc0, +0xc4, +0xc8, +0xcc. The pactex loader stores
 *     the four items of アルファグラデーション there as they are (0x4dc67b,
 *     0x4dc691, 0x4dc6a7, 0x4dc6bd; key 0x7d120c), 0 for a missing key;
 *   - the rebuild 0x4dcc10 takes each width as at most the surface's height
 *     or width (cmovl, 0x4dccf4..0x4dcd29) and, after the fill, adds a step
 *     for every one that is not 0, in the order top, bottom, left, right:
 *     command 25 (0x569600 -> 0x5a7240) over the rows 0 .. top with the
 *     alphas 0 and A, over the last `bottom` rows with A and 0; command 26
 *     (0x569670 -> 0x5a7300) over the columns 0 .. left with 0 and A, over
 *     the last `right` columns with A and 0; A is the colour's alpha
 *     (+0xbc), not 255. Commands 25 and 26 multiply the alpha (see
 *     construction-cmds);
 *   - SetPanelAlphaGradationTop / Bottom / Left / Right (0x5972e0,
 *     0x597340, 0x5973a0, 0x597400) do nothing for the width the widget has
 *     and store one that is not positive as 0; the getters (0x597460,
 *     0x597480, 0x5974a0, 0x5974c0) return the stored width. All eight take
 *     the widget as the other panel functions do (0x53e290, above): a number
 *     above 0 without a component gets a panel, 0 or less does nothing.
 *   PN9 panels with fades from a pactex. Five are the game's, with its
 *       sizes, colours and widths: SceneMap's MapClip (the mask of the
 *       dungeon map, left 64), SceneLogo's LightA (left and right 20 of
 *       64), the two page-turn shades of the CG viewer (all of their 80
 *       columns), SceneScriptMovie's MessageFade (top and bottom 50).
 *       Every panel of the game with a fade has a colour whose alpha is 255
 *       (the shades' 128 is their component's アルファ), for which a far end
 *       of 255 and one of the colour's alpha are the same pixels: that it
 *       is the colour's alpha (0x4dcc81, 0x4dcd1d: +0xbc is what the four
 *       calls push) shows only in the made-up Edges (alpha 200) and Wide
 *       (128), and no picture of the original has such a panel.
 *       Pixels by hand and every pixel against a model; the order of the
 *       operations; a width beyond the size; a negative width (no step
 *       here; the getter returns it); the SJIS key; a panel without the
 *       key.
 *   PN10 the eight functions through their AIN declarations: the fades are
 *       rebuilt in the fixed order whatever the order of the calls, the
 *       same width does nothing, a width that is not positive is 0, one
 *       beyond the size is kept, SetPanelColor and SetPanelSize keep the
 *       fades (the far end follows the colour's alpha); a number without
 *       a parts (made a panel, by a getter too), 0 and a negative number
 *       (nothing), a parts of another type; the real CPanelParts@GradationLeft::set
 *       and the 0 that ScrollBase@CreateClipperParts sets on every list.
 *   PN11 PE_Save / PE_Load: the same pixels under every XPE version since
 *       8 (the fades are operations those have), the widths come back from
 *       the operations (as built: at most the size) on the edges they were
 *       set on, also for a colour whose alpha is 0 (each operation keeps
 *       its edge); a loaded panel keeps them for a new colour, size or
 *       width; fades that name no edge are told apart by where they start;
 *       a panel saved without fades loads flat; under an AIN before version
 *       14 the loader reads none.
 * PN9 to PN11 fail before the change.
 *
 * Not covered:
 *   - a fade with a negative width: the original adds a step over no line
 *     for it, here none is added (the pixels are the same);
 *   - the widths of a loaded panel beyond its size (they come back as the
 *     size);
 *   - a fade that names no edge (not one of this build's panels) over all of
 *     a panel whose colour has alpha 0: it loads as the top or the left
 *     one, and a later colour with another alpha fades from that edge;
 *   - the surface of a panel that was only made (by a getter, or by
 *     SetComponentType): the original builds 200x200 at its next update,
 *     here it has none until a size, a colour or a fade is set;
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
 *   - the pixels the text commands draw (backlog-text does: 23 and 24 have
 *     operations of their own since).
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
	/* A number above 0 that has no parts gets one, a panel with the
	 * constructor's values (0x53e290 -> 0x53d920 -> 0x53a0b0 -> 0x578ee0
	 * makes the component, 0x536ff0 the widget), for a getter as for a
	 * setter. */
	const int UNKNOWN3 = 9980;
	PN_CHECK(!parts_try_get(UNKNOWN) && !parts_try_get(UNKNOWN2) && !parts_try_get(UNKNOWN3), "PN2: the unknown numbers are in use");
	pn_set_color(UNKNOWN, 1, 2, 3, 4);
	type = parts_try_get(UNKNOWN) ? BU_PE("GetComponentType", BU_I(UNKNOWN), BU_I(1)).i : -1;
	PN_CHECK(type == 14, "PN2 SetPanelColor on a number without a parts: %s, GetComponentType %d; want a panel (14)",
		 parts_try_get(UNKNOWN) ? "created" : "not created", type);
	pn_expect_color(UNKNOWN, 1, 2, 3, 4, "PN2 SetPanelColor on a number without a parts");
	pn_expect_flat(UNKNOWN, 200, 200, 1, 2, 3, 4, "PN2 SetPanelColor on a number without a parts");
	pn_set_size(UNKNOWN2, 5, 5);
	type = parts_try_get(UNKNOWN2) ? BU_PE("GetComponentType", BU_I(UNKNOWN2), BU_I(1)).i : -1;
	PN_CHECK(type == 14, "PN2 SetPanelSize on a number without a parts: GetComponentType %d, want 14", type);
	pn_expect_flat(UNKNOWN2, 5, 5, 220, 220, 220, 255, "PN2 SetPanelSize on a number without a parts");
	int alpha3 = BU_PE("GetPanelA", BU_I(UNKNOWN3)).i;
	type = parts_try_get(UNKNOWN3) ? BU_PE("GetComponentType", BU_I(UNKNOWN3), BU_I(1)).i : -1;
	PN_CHECK(alpha3 == 255 && type == 14, "PN2 GetPanelA on a number without a parts: %d, then GetComponentType %d; want 255, 14",
		 alpha3, type);
	pn_expect_color(UNKNOWN3, 220, 220, 220, 255, "PN2 a getter on a number without a parts");
	/* A number of 0 or less has no component (0x539cb0): nothing is made
	 * or changed and the getters return 0. */
	const bool had0 = parts_try_get(0) != NULL, had_neg = parts_try_get(-7) != NULL;
	pn_set_color(0, 1, 2, 3, 4);
	pn_set_size(0, 5, 5);
	pn_set_color(-7, 1, 2, 3, 4);
	pn_set_size(-7, 5, 5);
	pn_expect_color(0, 0, 0, 0, 0, "PN2 number 0");
	pn_expect_color(-7, 0, 0, 0, 0, "PN2 a negative number");
	PN_CHECK((parts_try_get(0) != NULL) == had0 && (parts_try_get(-7) != NULL) == had_neg && !had_neg,
		 "PN2: a panel call made a parts for number 0 (%d -> %d) or -7 (%d -> %d)", had0, parts_try_get(0) != NULL, had_neg,
		 parts_try_get(-7) != NULL);

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
		/* 23 and 24 have operations of their own, numbered as the commands
		 * (0x494420, 0x494270; backlog-text checks what they draw) */
		{ 7, PARTS_CP_DRAW_TEXT }, { 8, PARTS_CP_COPY_TEXT }, { 23, (enum parts_cp_op_type)23 }, { 24, (enum parts_cp_op_type)24 },
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
	/* PARTS_CP_MUL_AMAP_GRADATION_ROWS / _COLUMNS, PARTS_CP_BLUR_H / _V */
	{ 25, 1, 14 }, { 26, 1, 15 }, { 27, 1, 16 }, { 28, 1, 17 },
	{ 29, 0 },
	{ 30, 1, PARTS_CP_FILL_ALPHA_COLOR, { 5, 6, 26, 26, 10, 20, 30, 40 } },
	{ 31, 0 }, { 52, 1, 22 }, { 56, 0 },	/* PARTS_CP_DRAW_CIRCLE_AMAP */
	{ 57, 1, PARTS_CP_FILL_ALPHA_COLOR, { 5, 6, 12, 8, 10, 20, 30, 40 } },
	/* 97 has no vertices here; PARTS_CP_FILL_CIRCLE_AMAP / _BLEND */
	{ 58, 0 }, { 89, 0 }, { 95, 0 }, { 97, 0 }, { 102, 1, 18 }, { 103, 0 }, { 106, 1, 19 }, { 107, 0 }, { 120, 0 }, { 121, 0 },
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
		if (g->type > PARTS_CP_FILL_PIE_AMAP) {
			/* the pixel commands: construction-cmds checks their fields */
		} else if (g->type == PARTS_CP_CREATE || g->type == PARTS_CP_CREATE_PIXEL_ONLY) {
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

/* --- PN9 .. PN11: アルファグラデーション --- */
#define PN_GRAD "\xa5\xa2\xa5\xeb\xa5\xd5\xa5\xa1\xa5\xb0\xa5\xe9\xa5\xc7\xa9\x60\xa5\xb7\xa5\xe7\xa5\xf3"
#define PN_GRAD_S "\x83\x41\x83\x8b\x83\x74\x83\x40\x83\x4f\x83\x89\x83\x66\x81\x5b\x83\x56\x83\x87\x83\x93"

/* パネル { サイズ, 色, アルファグラデーション = (top, bottom, left, right) }. */
static struct ex_tree pn_grad_panel(const char *name, int w, int h, const int c[4], const int grad[4], bool sjis)
{
	struct ex_tree info = bu_branch(BU_INFO, 4);
	info.children[0] = bu_sleaf(sjis ? PN_TYPE_S : BU_TYPE, sjis ? PN_PANEL_S : CP_PANEL);
	info.children[1] = lb_ilist(sjis ? PN_SIZE_S : CP_SIZE, 2, (int[]){ w, h });
	info.children[2] = lb_ilist(sjis ? PN_COLOR_S : CP_COLOR, 4, c);
	info.children[3] = lb_ilist(sjis ? PN_GRAD_S : PN_GRAD, 4, grad);
	struct lb_place pl = { 0, 0, 1 };
	return lb_node(name, &pl, info, 0);
}

/* The surface the rebuild 0x4dcc10 leaves, w x h: the colour, then the alpha
 * multiplied over the rows of the top and the bottom fade and over the
 * columns of the left and the right one, in that order (commands 25 and 26:
 * line i of n by a1 + i * (a2 - a1) / n, the quotient truncated, as
 * A * a / 255). A fade is at most as wide as the surface, starts or ends
 * with 0 at the edge and has the colour's alpha at its other end. */
static void pn_grad_model(uint8_t *px, int w, int h, const int c[4], const int grad[4])
{
	for (int i = 0; i < w * h; i++) {
		px[i * 4] = c[0]; px[i * 4 + 1] = c[1]; px[i * 4 + 2] = c[2]; px[i * 4 + 3] = c[3];
	}
	const int a = c[3];
	const int top = grad[0] < h ? grad[0] : h, bottom = grad[1] < h ? grad[1] : h;
	const int left = grad[2] < w ? grad[2] : w, right = grad[3] < w ? grad[3] : w;
	for (int i = 0; i < top; i++) {
		for (int x = 0; x < w; x++)
			px[(i * w + x) * 4 + 3] = px[(i * w + x) * 4 + 3] * (0 + i * (a - 0) / top) / 255;
	}
	for (int i = 0; i < bottom; i++) {
		for (int x = 0; x < w; x++)
			px[((h - bottom + i) * w + x) * 4 + 3] = px[((h - bottom + i) * w + x) * 4 + 3] * (a + i * (0 - a) / bottom) / 255;
	}
	for (int i = 0; i < left; i++) {
		for (int y = 0; y < h; y++)
			px[(y * w + i) * 4 + 3] = px[(y * w + i) * 4 + 3] * (0 + i * (a - 0) / left) / 255;
	}
	for (int i = 0; i < right; i++) {
		for (int y = 0; y < h; y++)
			px[(y * w + w - right + i) * 4 + 3] = px[(y * w + w - right + i) * 4 + 3] * (a + i * (0 - a) / right) / 255;
	}
}

static void pn_expect_model(int no, int w, int h, const uint8_t *model, const char *what)
{
	uint8_t *px = pn_pixels(no, w, h, what);
	if (!px)
		return;
	int bad = 0;
	for (int i = 0; i < w * h; i++) {
		if (memcmp(px + i * 4, model + i * 4, 4) && bad++ < 3)
			PN_CHECK(0, "%s: (%d,%d) = (%d,%d,%d,%d), want (%d,%d,%d,%d)", what, i % w, i / w, px[i * 4], px[i * 4 + 1],
				 px[i * 4 + 2], px[i * 4 + 3], model[i * 4], model[i * 4 + 1], model[i * 4 + 2], model[i * 4 + 3]);
	}
	PN_CHECK(bad <= 3, "%s: %d pixels differ", what, bad);
	free(px);
}
static void pn_expect_grad(int no, int w, int h, const int c[4], const int grad[4], const char *what)
{
	uint8_t *model = xmalloc((size_t)w * h * 4);
	pn_grad_model(model, w, h, c, grad);
	pn_expect_model(no, w, h, model, what);
	free(model);
}

static const char *const pn_grad_setters[4] = { "SetPanelAlphaGradationTop", "SetPanelAlphaGradationBottom",
	"SetPanelAlphaGradationLeft", "SetPanelAlphaGradationRight" };
static const char *const pn_grad_getters[4] = { "GetPanelAlphaGradationTop", "GetPanelAlphaGradationBottom",
	"GetPanelAlphaGradationLeft", "GetPanelAlphaGradationRight" };
static void pn_set_grad(int no, int side, int size)
{
	BU_PE(pn_grad_setters[side], BU_I(no), BU_I(size));
}
static void pn_expect_grad_values(int no, int top, int bottom, int left, int right, const char *what)
{
	int got[4];
	for (int i = 0; i < 4; i++)
		got[i] = BU_PE(pn_grad_getters[i], BU_I(no)).i;
	PN_CHECK(got[0] == top && got[1] == bottom && got[2] == left && got[3] == right,
		 "%s: GetPanelAlphaGradationTop/Bottom/Left/Right (%d,%d,%d,%d), want (%d,%d,%d,%d)", what, got[0], got[1], got[2],
		 got[3], top, bottom, left, right);
}
/* The types of the operations after Create and the fill, e.g. "RRCC" for the
 * four fades (R: rows, command 25; C: columns, command 26). */
static void pn_expect_fades(int no, const char *want, const char *what)
{
	char got[16] = "";
	int n = 0;
	struct parts_cp_op *op = pn_op_at(no, 2);
	for (; op && n < 15; op = TAILQ_NEXT(op, entry))
		got[n++] = op->type == PARTS_CP_MUL_AMAP_GRADATION_ROWS ? 'R' : op->type == PARTS_CP_MUL_AMAP_GRADATION_COLUMNS ? 'C' : '?';
	got[n] = '\0';
	PN_CHECK(!strcmp(got, want) && pn_nr_ops(no) == 2 + (int)strlen(want), "%s: the operations after the fill are \"%s\" (%d in all), want \"%s\"",
		 what, got, pn_nr_ops(no), want);
}
static void pn_expect_alpha(const uint8_t *px, int w, int x, int y, int a, const char *what)
{
	if (px)
		PN_CHECK(px[(y * w + x) * 4 + 3] == a, "%s: alpha (%d,%d) = %d, want %d", what, x, y, px[(y * w + x) * 4 + 3], a);
}

/* --- PN9 --- */
static void pn_gradation(void)
{
	cn_gl_setup();
	static const struct { const char *name; int w, h, c[4], grad[4]; const char *fades; bool sjis; } panels[] = {
		{ "Edges", 16, 8, { 10, 20, 30, 200 }, { 2, 3, 4, 5 }, "RRCC" },
		{ "MapClip", 1024, 720, { 255, 255, 255, 255 }, { 0, 0, 64, 0 }, "C" },		/* SceneMap */
		{ "LightA", 64, 400, { 255, 255, 128, 255 }, { 0, 0, 20, 20 }, "CC" },		/* SceneLogo */
		{ "Left", 80, 720, { 0, 0, 0, 255 }, { 0, 0, 0, 80 }, "C" },			/* SceneCgModeDetail */
		{ "Right", 80, 720, { 0, 0, 0, 255 }, { 0, 0, 80, 0 }, "C" },
		{ "MessageFade", 1280, 206, { 220, 220, 220, 255 }, { 50, 50, 0, 0 }, "RR" },	/* SceneScriptMovie */
		{ "Wide", 8, 4, { 0, 0, 0, 128 }, { 0, 0, 20, 0 }, "C" },
		{ "Flat", 9, 5, { 1, 2, 3, 4 }, { 0, 0, 0, 0 }, "" },
		{ "Negative", 9, 5, { 1, 2, 3, 200 }, { -3, 0, 0, 0 }, "" },
		{ "EdgesSjis", 16, 8, { 10, 20, 30, 200 }, { 2, 3, 4, 5 }, "RRCC", true },
	};
	enum { N = sizeof(panels) / sizeof(panels[0]) };
	struct ex_tree *kids;
	struct ex *e = lb_activity("PanelGradation", N, &kids);
	for (int i = 0; i < N; i++)
		kids->children[i] = pn_grad_panel(panels[i].name, panels[i].w, panels[i].h, panels[i].c, panels[i].grad, panels[i].sjis);
	int act = cn_load("PanelGradation", e);
	pn_failures += cn_failures;
	int no[N];
	for (int i = 0; i < N; i++) {
		no[i] = bu_number(act, panels[i].name);
		PN_CHECK(no[i] > 0, "%s has no parts number", panels[i].name);
		if (no[i] <= 0)
			continue;
		char what[64];
		snprintf(what, sizeof what, "PN9 %s", panels[i].name);
		pn_expect_grad(no[i], panels[i].w, panels[i].h, panels[i].c, panels[i].grad, what);
		pn_expect_fades(no[i], panels[i].fades, what);
		/* The values as the pactex has them, neither clamped to the size
		 * nor to 0 (0x4dc67b). */
		pn_expect_grad_values(no[i], panels[i].grad[0], panels[i].grad[1], panels[i].grad[2], panels[i].grad[3], what);
		pn_expect_color(no[i], panels[i].c[0], panels[i].c[1], panels[i].c[2], panels[i].c[3], what);
	}
	heap_unref(act);

	/* By hand. Edges (made up: an alpha that is not 255 shows which alpha
	 * the far end of a fade has), alpha 200: the top fade's rows are 0 and 200 / 2 = 100
	 * (200 * 100 / 255 = 78), the bottom's 200, 200 - 66 = 134 and 200 - 133
	 * = 67 (-200 / 3 and -400 / 3 truncated towards zero: 156, 105, 52), the
	 * left columns 0, 50, 100, 150 (0, 39, 78, 117), the right ones 200, 160,
	 * 120, 80, 40 (156, 125, 94, 62, 31); a corner is multiplied twice. */
	uint8_t *px = no[0] > 0 ? pn_pixels(no[0], 16, 8, "PN9 Edges") : NULL;
	if (px) {
		static const struct { int x, y, a; } want[] = {
			{ 8, 3, 200 }, { 8, 0, 0 }, { 8, 1, 78 }, { 8, 5, 156 }, { 8, 6, 105 }, { 8, 7, 52 },
			{ 0, 3, 0 }, { 1, 3, 39 }, { 2, 3, 78 }, { 3, 3, 117 }, { 4, 3, 200 },
			{ 10, 3, 200 }, { 11, 3, 156 }, { 12, 3, 125 }, { 13, 3, 94 }, { 14, 3, 62 }, { 15, 3, 31 },
			{ 1, 1, 15 }, { 15, 7, 8 }, { 12, 6, 65 },
		};
		for (unsigned i = 0; i < sizeof want / sizeof want[0]; i++)
			pn_expect_alpha(px, 16, want[i].x, want[i].y, want[i].a, "PN9 Edges");
		PN_CHECK(px[(3 * 16 + 1) * 4] == 10 && px[(3 * 16 + 1) * 4 + 1] == 20 && px[(3 * 16 + 1) * 4 + 2] == 30,
			 "PN9 Edges: a fade changed the colour");
		free(px);
	}
	/* MapClip: column i of the 64 is i * 255 / 64. */
	px = no[1] > 0 ? pn_pixels(no[1], 1024, 720, "PN9 MapClip") : NULL;
	if (px) {
		static const int col[] = { 0, 1, 2, 16, 32, 48, 63, 64, 500, 1023 }, a[] = { 0, 3, 7, 63, 127, 191, 251, 255, 255, 255 };
		for (unsigned i = 0; i < sizeof col / sizeof col[0]; i++) {
			pn_expect_alpha(px, 1024, col[i], 0, a[i], "PN9 MapClip");
			pn_expect_alpha(px, 1024, col[i], 719, a[i], "PN9 MapClip");
		}
		free(px);
	}
	/* LightA: 20 columns from each side, 24 whole ones between them; the
	 * right fade starts with 255 and ends with 255 - 242 = 13. */
	px = no[2] > 0 ? pn_pixels(no[2], 64, 400, "PN9 LightA") : NULL;
	if (px) {
		static const int col[] = { 0, 10, 19, 20, 43, 44, 45, 63 }, a[] = { 0, 127, 242, 255, 255, 255, 243, 13 };
		for (unsigned i = 0; i < sizeof col / sizeof col[0]; i++)
			pn_expect_alpha(px, 64, col[i], 200, a[i], "PN9 LightA");
		free(px);
	}
	/* The page-turn shades, black with alpha 255 (the game draws them with
	 * a component alpha of 128): all of the width is the fade. Left (its
	 * right edge fades): 255 at column 0, 255 - 127 = 128 at 40, 255 - 251 =
	 * 4 at 79; Right: 0, 127 at column 40, 251 at 79. */
	px = no[3] > 0 ? pn_pixels(no[3], 80, 720, "PN9 Left") : NULL;
	pn_expect_alpha(px, 80, 0, 5, 255, "PN9 Left");
	pn_expect_alpha(px, 80, 40, 5, 128, "PN9 Left");
	pn_expect_alpha(px, 80, 79, 5, 4, "PN9 Left");
	free(px);
	px = no[4] > 0 ? pn_pixels(no[4], 80, 720, "PN9 Right") : NULL;
	pn_expect_alpha(px, 80, 0, 5, 0, "PN9 Right");
	pn_expect_alpha(px, 80, 40, 5, 127, "PN9 Right");
	pn_expect_alpha(px, 80, 79, 5, 251, "PN9 Right");
	free(px);
	/* MessageFade: 50 rows from the top (row 25 is 25 * 255 / 50 = 127) and
	 * from the bottom (row 156 is 255, row 181 is 255 - 127 = 128, the last
	 * row 255 - 249 = 6); the 106 rows between them are whole. */
	px = no[5] > 0 ? pn_pixels(no[5], 1280, 206, "PN9 MessageFade") : NULL;
	pn_expect_alpha(px, 1280, 640, 0, 0, "PN9 MessageFade");
	pn_expect_alpha(px, 1280, 640, 25, 127, "PN9 MessageFade");
	pn_expect_alpha(px, 1280, 640, 50, 255, "PN9 MessageFade");
	pn_expect_alpha(px, 1280, 640, 155, 255, "PN9 MessageFade");
	pn_expect_alpha(px, 1280, 640, 156, 255, "PN9 MessageFade");
	pn_expect_alpha(px, 1280, 640, 181, 128, "PN9 MessageFade");
	pn_expect_alpha(px, 1280, 640, 205, 6, "PN9 MessageFade");
	free(px);
	/* Wider than the panel (made up, alpha 128): the fade is the panel's 8
	 * columns, 16 a step (128 * 16 / 255 = 8, 128 * 112 / 255 = 56). */
	px = no[6] > 0 ? pn_pixels(no[6], 8, 4, "PN9 Wide") : NULL;
	pn_expect_alpha(px, 8, 0, 2, 0, "PN9 Wide");
	pn_expect_alpha(px, 8, 1, 2, 8, "PN9 Wide");
	pn_expect_alpha(px, 8, 7, 2, 56, "PN9 Wide");
	free(px);
	if (no[7] > 0)
		pn_expect_flat(no[7], 9, 5, 1, 2, 3, 4, "PN9 Flat");
	if (no[8] > 0)
		pn_expect_flat(no[8], 9, 5, 1, 2, 3, 200, "PN9 Negative");

	/* A panel without the key, as before. */
	e = lb_activity("PanelNoGradation", 1, &kids);
	kids->children[0] = pn_panel("Dimmer", 64, 32, 0, 0, 0, 128, false);
	act = cn_load("PanelNoGradation", e);
	pn_failures += cn_failures;
	int dim = bu_number(act, "Dimmer");
	heap_unref(act);
	if (dim > 0) {
		pn_expect_flat(dim, 64, 32, 0, 0, 0, 128, "PN9 no key");
		pn_expect_fades(dim, "", "PN9 no key");
		pn_expect_grad_values(dim, 0, 0, 0, 0, "PN9 no key");
	}
	printf("PN9 alpha gradations from a pactex: %s\n", pn_failures ? "FAIL" : "ok");
}

/* --- PN10 --- */
static GLuint pn_handle(int no)
{
	struct parts *p = parts_try_get(no);
	return p ? p->states[0].common.texture.handle : 0;
}

static void pn_gradation_hll(void)
{
	cn_gl_setup();
	struct ex_tree *kids;
	struct ex *e = lb_activity("PanelGradationCalls", 1, &kids);
	kids->children[0] = pn_panel("Plain", 16, 8, 10, 20, 30, 200, false);
	int act = cn_load("PanelGradationCalls", e);
	pn_failures += cn_failures;
	int no = bu_number(act, "Plain");
	heap_unref(act);
	PN_CHECK(no > 0, "Plain has no parts number");
	if (no <= 0)
		return;
	const int c[4] = { 10, 20, 30, 200 };

	pn_set_grad(no, 2, 4);
	pn_expect_grad(no, 16, 8, c, (int[]){ 0, 0, 4, 0 }, "PN10 left 4");
	pn_expect_fades(no, "C", "PN10 left 4");
	pn_expect_grad_values(no, 0, 0, 4, 0, "PN10 left 4");
	/* The same width again does nothing (0x5973c2). */
	GLuint handle = pn_handle(no);
	struct parts_cp_op *first = pn_op_at(no, 0);
	pn_set_grad(no, 2, 4);
	PN_CHECK(pn_handle(no) == handle && pn_op_at(no, 0) == first, "PN10: the same width rebuilt the panel");
	/* The fades are added top, bottom, left, right, whatever the order of
	 * the calls. */
	pn_set_grad(no, 0, 2);
	pn_expect_fades(no, "RC", "PN10 top after left");
	pn_expect_grad(no, 16, 8, c, (int[]){ 2, 0, 4, 0 }, "PN10 top after left");
	pn_set_grad(no, 3, 5);
	pn_set_grad(no, 1, 3);
	pn_expect_fades(no, "RRCC", "PN10 all four");
	pn_expect_grad(no, 16, 8, c, (int[]){ 2, 3, 4, 5 }, "PN10 all four");
	pn_expect_grad_values(no, 2, 3, 4, 5, "PN10 all four");
	/* A width that is not positive is stored as 0 (0x5973e3). */
	pn_set_grad(no, 2, -5);
	pn_expect_fades(no, "RRC", "PN10 left -5");
	pn_expect_grad(no, 16, 8, c, (int[]){ 2, 3, 0, 5 }, "PN10 left -5");
	pn_expect_grad_values(no, 2, 3, 0, 5, "PN10 left -5");
	/* One beyond the size is kept and fades all of the panel. */
	pn_set_grad(no, 2, 100);
	pn_expect_grad(no, 16, 8, c, (int[]){ 2, 3, 100, 5 }, "PN10 left 100");
	pn_expect_grad_values(no, 2, 3, 100, 5, "PN10 left 100");
	uint8_t *px = pn_pixels(no, 16, 8, "PN10 left 100");
	/* column 8 of 16: 8 * 200 / 16 = 100, 200 * 100 / 255 = 78 */
	pn_expect_alpha(px, 16, 8, 3, 78, "PN10 left 100");
	free(px);
	pn_set_grad(no, 2, 4);
	/* The colour and the size keep the fades; the far end follows the
	 * colour's alpha. */
	pn_set_color(no, 40, 50, 60, 100);
	pn_expect_grad(no, 16, 8, (int[]){ 40, 50, 60, 100 }, (int[]){ 2, 3, 4, 5 }, "PN10 after SetPanelColor");
	px = pn_pixels(no, 16, 8, "PN10 after SetPanelColor");
	/* column 2 of the left 4: 2 * 100 / 4 = 50, 100 * 50 / 255 = 19 */
	pn_expect_alpha(px, 16, 2, 3, 19, "PN10 after SetPanelColor");
	free(px);
	pn_set_size(no, 6, 4);
	pn_expect_grad(no, 6, 4, (int[]){ 40, 50, 60, 100 }, (int[]){ 2, 3, 4, 5 }, "PN10 after SetPanelSize");
	pn_expect_fades(no, "RRCC", "PN10 after SetPanelSize");
	pn_expect_grad_values(no, 2, 3, 4, 5, "PN10 after SetPanelSize");
	for (int i = 0; i < 4; i++)
		pn_set_grad(no, i, 0);
	pn_expect_flat(no, 6, 4, 40, 50, 60, 100, "PN10 all four back to 0");
	pn_expect_fades(no, "", "PN10 all four back to 0");

	/* A number above 0 without a parts gets one, a panel with the
	 * constructor's values, and the width is set on it; a getter makes
	 * the panel as well (0x53e290 -> 0x53d920 -> ... -> 0x578cb0, then
	 * 0x536ff0; see PN2). A number of 0 or less has no component
	 * (0x539cb0): nothing is made, the getters return 0. A parts of
	 * another type becomes a panel. */
	const int UNKNOWN = 9981, PLAIN = 9982, GETTER = 9983, UNKNOWN_GET = 9985;
	PN_CHECK(!parts_try_get(UNKNOWN) && !parts_try_get(UNKNOWN_GET), "PN10: the unknown numbers are in use");
	pn_set_grad(UNKNOWN, 0, 3);
	int type = parts_try_get(UNKNOWN) ? BU_PE("GetComponentType", BU_I(UNKNOWN), BU_I(1)).i : -1;
	PN_CHECK(type == 14, "PN10 SetPanelAlphaGradationTop on a number without a parts: %s, GetComponentType %d; want a panel (14)",
		 parts_try_get(UNKNOWN) ? "created" : "not created", type);
	pn_expect_grad_values(UNKNOWN, 3, 0, 0, 0, "PN10 a number without a parts");
	pn_expect_color(UNKNOWN, 220, 220, 220, 255, "PN10 a number without a parts");
	if (parts_try_get(UNKNOWN))
		pn_expect_grad(UNKNOWN, 200, 200, (int[]){ 220, 220, 220, 255 }, (int[]){ 3, 0, 0, 0 }, "PN10 a number without a parts");
	int left = BU_PE("GetPanelAlphaGradationLeft", BU_I(UNKNOWN_GET)).i;
	type = parts_try_get(UNKNOWN_GET) ? BU_PE("GetComponentType", BU_I(UNKNOWN_GET), BU_I(1)).i : -1;
	PN_CHECK(left == 0 && type == 14, "PN10 GetPanelAlphaGradationLeft on a number without a parts: %d, then GetComponentType %d; want 0, 14",
		 left, type);
	const bool had0 = parts_try_get(0) != NULL, had_neg = parts_try_get(-3) != NULL;
	pn_set_grad(0, 2, 9);
	pn_set_grad(-3, 1, 9);
	pn_expect_grad_values(0, 0, 0, 0, 0, "PN10 number 0");
	pn_expect_grad_values(-3, 0, 0, 0, 0, "PN10 a negative number");
	PN_CHECK((parts_try_get(0) != NULL) == had0 && (parts_try_get(-3) != NULL) == had_neg && !had_neg,
		 "PN10: a gradation call made a parts for number 0 (%d -> %d) or -3 (%d -> %d)", had0, parts_try_get(0) != NULL,
		 had_neg, parts_try_get(-3) != NULL);
	parts_get(PLAIN);
	pn_set_grad(PLAIN, 0, 50);
	type = BU_PE("GetComponentType", BU_I(PLAIN), BU_I(1)).i;
	PN_CHECK(type == 14, "PN10 a plain parts after SetPanelAlphaGradationTop: GetComponentType %d, want 14", type);
	pn_expect_grad(PLAIN, 200, 200, (int[]){ 220, 220, 220, 255 }, (int[]){ 50, 0, 0, 0 }, "PN10 a plain parts");
	parts_get(GETTER);
	int right = BU_PE("GetPanelAlphaGradationRight", BU_I(GETTER)).i;
	type = BU_PE("GetComponentType", BU_I(GETTER), BU_I(1)).i;
	PN_CHECK(right == 0 && type == 14, "PN10 GetPanelAlphaGradationRight on a plain parts: %d, then GetComponentType %d; want 0, 14",
		 right, type);

	/* The real CPanelParts. */
	const int REAL = 9984;
	int create = ain_get_function(ain, "AFL_Parts_CreatePanel");
	int set_left = ain_get_function(ain, "parts::detail::CPanelParts@GradationLeft::set");
	int set_bottom = ain_get_function(ain, "parts::detail::CPanelParts@GradationBottom::set");
	PN_CHECK(create >= 0 && set_left >= 0 && set_bottom >= 0, "AFL_Parts_CreatePanel or CPanelParts@GradationLeft/Bottom::set is not in the AIN");
	if (create >= 0 && set_left >= 0 && set_bottom >= 0) {
		int args[] = { REAL, -1, 1 };	/* option<string> comment = none */
		union vm_value r[2] = {{ .i = -1 }, { .i = -1 }};
		int n = da_callfunc(create, 3, args, r);
		pn_failures += da_failures;
		PN_CHECK(n == 2 && da_live_page(r[0].i), "AFL_Parts_CreatePanel returned %d slot(s) [%d, %d]", n, r[0].i, r[1].i);
		if (n == 2 && da_live_page(r[0].i)) {
			pn_set_size(REAL, 12, 6);
			int v = 3;
			call_method_args(set_left, r[0].i, 1, &v);
			v = 2;
			call_method_args(set_bottom, r[0].i, 1, &v);
			pn_expect_grad_values(REAL, 0, 2, 3, 0, "PN10 CPanelParts@GradationLeft/Bottom::set");
			pn_expect_grad(REAL, 12, 6, (int[]){ 220, 220, 220, 255 }, (int[]){ 0, 2, 3, 0 }, "PN10 CPanelParts@GradationLeft/Bottom::set");
			/* What ScrollBase@CreateClipperParts does for every list of
			 * the game: a width of 0 on a sized panel changes nothing. */
			handle = pn_handle(REAL);
			v = 0;
			call_method_args(ain_get_function(ain, "parts::detail::CPanelParts@GradationTop::set"), r[0].i, 1, &v);
			PN_CHECK(pn_handle(REAL) == handle, "PN10: CPanelParts@GradationTop::set(0) rebuilt the panel");
		}
	}
	printf("PN10 alpha gradation calls: %s\n", pn_failures ? "FAIL" : "ok");
}

/* --- PN11 --- */
static int pn_version_pos(struct page *saved)
{
	struct iarray_reader r;
	return iarray_init_reader(&r, saved, "XPE") ? (int)r.pos : -1;
}

static void pn_gradation_save(void)
{
	cn_gl_setup();
	static const struct { const char *name; int w, h, c[4], grad[4], loaded[4]; } panels[] = {
		{ "Edges", 16, 8, { 10, 20, 30, 200 }, { 2, 3, 4, 5 }, { 2, 3, 4, 5 } },
		{ "LeftFade", 96, 12, { 255, 255, 255, 255 }, { 0, 0, 64, 0 }, { 0, 0, 64, 0 } },
		{ "Bottom", 16, 8, { 10, 20, 30, 200 }, { 0, 8, 0, 0 }, { 0, 8, 0, 0 } },
		{ "RightOnly", 16, 8, { 10, 20, 30, 200 }, { 0, 0, 0, 16 }, { 0, 0, 0, 16 } },
		/* The values come back from the operations: clamped to the size. */
		{ "Wide", 8, 4, { 0, 0, 0, 128 }, { 0, 0, 20, 0 }, { 0, 0, 8, 0 } },
		{ "Flat", 9, 5, { 1, 2, 3, 4 }, { 0, 0, 0, 0 }, { 0, 0, 0, 0 } },
	};
	enum { N = sizeof(panels) / sizeof(panels[0]) };
	struct ex_tree *kids;
	struct ex *e = lb_activity("PanelGradationSave", N, &kids);
	for (int i = 0; i < N; i++)
		kids->children[i] = pn_grad_panel(panels[i].name, panels[i].w, panels[i].h, panels[i].c, panels[i].grad, false);
	int act = cn_load("PanelGradationSave", e);
	pn_failures += cn_failures;
	int no[N];
	bool all = true;
	for (int i = 0; i < N; i++) {
		no[i] = bu_number(act, panels[i].name);
		PN_CHECK(no[i] > 0, "%s has no parts number", panels[i].name);
		all = all && no[i] > 0;
	}
	heap_unref(act);
	if (!all)
		return;
	pn_expect_fades(no[0], "RRCC", "PN11 Edges before saving");
	PE_UpdateComponent(0);
	struct page *saved = NULL;
	PN_CHECK(PE_Save(&saved), "PE_Save failed");
	if (!saved)
		return;
	/* The fades are operations every XPE since version 8 has: the same
	 * page loads under each of those numbers. */
	const int current = ca_save_version(saved), pos = pn_version_pos(saved);
	PN_CHECK(current >= 8 && pos >= 0, "XPE v%d, want 8 or later", current);
	for (int version = current; pos >= 0 && version >= 8; version--) {
		struct page *page = copy_page(saved);
		page->values[pos].i = version;
		PN_CHECK(PE_Load(&page), "PE_Load failed (version %d)", version);
		PE_UpdateComponent(0);
		for (int i = 0; i < N; i++) {
			char what[64];
			snprintf(what, sizeof what, "PN11 %s after loading version %d", panels[i].name, version);
			pn_expect_grad(no[i], panels[i].w, panels[i].h, panels[i].c, panels[i].grad, what);
			pn_expect_grad_values(no[i], panels[i].loaded[0], panels[i].loaded[1], panels[i].loaded[2], panels[i].loaded[3], what);
			pn_expect_color(no[i], panels[i].c[0], panels[i].c[1], panels[i].c[2], panels[i].c[3], what);
		}
		ca_free_buffer(page);
	}
	pn_expect_fades(no[0], "RRCC", "PN11 Edges after loading");
	/* Saving what was loaded gives the same page. */
	struct page *again = NULL;
	PN_CHECK(PE_Save(&again), "the second PE_Save failed");
	if (again) {
		bool same = again->nr_vars == saved->nr_vars;
		for (int i = 0; same && i < again->nr_vars; i++)
			same = again->values[i].i == saved->values[i].i;
		PN_CHECK(same, "saving the loaded panels gave another page (%d and %d values)", saved->nr_vars, again->nr_vars);
		ca_free_buffer(again);
	}
	ca_free_buffer(saved);
	/* A loaded panel keeps its fades for a new colour and a new size, and
	 * takes a new width. */
	pn_set_color(no[0], 40, 50, 60, 100);
	pn_expect_grad(no[0], 16, 8, (int[]){ 40, 50, 60, 100 }, (int[]){ 2, 3, 4, 5 }, "PN11 Edges recoloured after loading");
	pn_set_size(no[2], 10, 12);
	pn_expect_grad(no[2], 10, 12, panels[2].c, (int[]){ 0, 8, 0, 0 }, "PN11 Bottom resized after loading");
	pn_set_grad(no[3], 3, 2);
	pn_expect_grad(no[3], 16, 8, panels[3].c, (int[]){ 0, 0, 0, 2 }, "PN11 RightOnly narrowed after loading");
	pn_expect_grad_values(no[3], 0, 0, 0, 2, "PN11 RightOnly narrowed after loading");

	/* A panel saved before the fades were built (Create and the fill
	 * alone) loads flat, with no widths. */
	const int OLD = 10401;
	parts_get(OLD)->component_type = 14;
	PE_AddCreateToPartsConstructionProcess(OLD, 16, 8, 1);
	PE_AddFillWithAlphaToPartsConstructionProcess(OLD, 0, 0, 16, 8, 10, 20, 30, 200, 1);
	PE_BuildPartsConstructionProcess(OLD, 1);
	/* A fill whose alpha is 0: every fade multiplies by 0, and a bottom
	 * fade over all of the height is the same rectangle and the same two
	 * alphas as a top one would be (a right one over all of the width as a
	 * left one). Each comes back on its own edge, so that a colour set
	 * after the load fades the way it did before. */
	const int ZERO = 10402, ZERO_RIGHT = 10403;
	parts_get(ZERO);
	pn_set_size(ZERO, 6, 4);
	pn_set_color(ZERO, 9, 8, 7, 0);
	pn_set_grad(ZERO, 1, 4);
	parts_get(ZERO_RIGHT);
	pn_set_size(ZERO_RIGHT, 6, 4);
	pn_set_color(ZERO_RIGHT, 9, 8, 7, 0);
	pn_set_grad(ZERO_RIGHT, 3, 6);
	/* Fades that name no edge (added as a script would add them, or by a
	 * build without the mark): the four of Edges on a panel's operations.
	 * They are told apart by where they start and with which alpha. */
	const int UNMARKED = 10404;
	parts_get(UNMARKED)->component_type = 14;
	PE_AddCreateToPartsConstructionProcess(UNMARKED, 16, 8, 1);
	PE_AddFillWithAlphaToPartsConstructionProcess(UNMARKED, 0, 0, 16, 8, 10, 20, 30, 200, 1);
	PE_AddMulAMapGradationToPartsConstructionProcess(UNMARKED, false, 0, 0, 16, 2, false, 0, 200, 1);
	PE_AddMulAMapGradationToPartsConstructionProcess(UNMARKED, false, 0, 5, 16, 3, false, 200, 0, 1);
	PE_AddMulAMapGradationToPartsConstructionProcess(UNMARKED, true, 0, 0, 4, 8, false, 0, 200, 1);
	PE_AddMulAMapGradationToPartsConstructionProcess(UNMARKED, true, 11, 0, 5, 8, false, 200, 0, 1);
	PE_BuildPartsConstructionProcess(UNMARKED, 1);
	pn_expect_grad_values(UNMARKED, 2, 3, 4, 5, "PN11 fades that name no edge");
	pn_expect_grad(UNMARKED, 16, 8, (int[]){ 10, 20, 30, 200 }, (int[]){ 2, 3, 4, 5 }, "PN11 fades that name no edge");
	saved = NULL;
	PN_CHECK(PE_Save(&saved), "PE_Save failed (old panel)");
	if (saved) {
		PN_CHECK(PE_Load(&saved), "PE_Load failed (old panel)");
		PE_UpdateComponent(0);
		pn_expect_flat(OLD, 16, 8, 10, 20, 30, 200, "PN11 a panel saved without fades");
		pn_expect_grad_values(OLD, 0, 0, 0, 0, "PN11 a panel saved without fades");
		pn_expect_flat(ZERO, 6, 4, 9, 8, 7, 0, "PN11 a transparent panel with a bottom fade");
		pn_expect_fades(ZERO, "R", "PN11 a transparent panel with a bottom fade");
		pn_expect_grad_values(ZERO, 0, 4, 0, 0, "PN11 a transparent panel with a bottom fade");
		pn_set_color(ZERO, 9, 8, 7, 200);
		pn_expect_grad(ZERO, 6, 4, (int[]){ 9, 8, 7, 200 }, (int[]){ 0, 4, 0, 0 }, "PN11 the bottom fade after a colour");
		/* the last row of a bottom fade over 4 rows: 200 - 150 = 50, 200 * 50 / 255 = 39; the first is 200 * 200 / 255 = 156 */
		uint8_t *zp = pn_pixels(ZERO, 6, 4, "PN11 the bottom fade after a colour");
		pn_expect_alpha(zp, 6, 2, 0, 156, "PN11 the bottom fade after a colour");
		pn_expect_alpha(zp, 6, 2, 3, 39, "PN11 the bottom fade after a colour");
		free(zp);
		pn_expect_grad_values(ZERO_RIGHT, 0, 0, 0, 6, "PN11 a transparent panel with a right fade");
		pn_set_color(ZERO_RIGHT, 9, 8, 7, 200);
		pn_expect_grad(ZERO_RIGHT, 6, 4, (int[]){ 9, 8, 7, 200 }, (int[]){ 0, 0, 0, 6 }, "PN11 the right fade after a colour");
		pn_expect_grad_values(UNMARKED, 2, 3, 4, 5, "PN11 fades that name no edge, loaded");
		ca_free_buffer(saved);
	}

	/* An AIN before version 14 has no panels; its loader reads no fades. */
	ain->version = 13;
	e = lb_activity("PanelGradationV13", 1, &kids);
	kids->children[0] = pn_grad_panel("Edges", 16, 8, panels[0].c, panels[0].grad, false);
	act = cn_load("PanelGradationV13", e);
	pn_failures += cn_failures;
	int v13 = bu_number(act, "Edges");
	heap_unref(act);
	if (v13 > 0) {
		pn_expect_flat(v13, 16, 8, 10, 20, 30, 200, "PN11 version 13");
		PN_CHECK(pn_nr_ops(v13) == 2, "PN11 version 13: %d operations, want 2", pn_nr_ops(v13));
	}
	ain->version = 14;
	printf("PN11 alpha gradations saved and loaded: %s\n", pn_failures ? "FAIL" : "ok");
}

static int test_panel(void)
{
	void (*cases[])(void) = { pn_pactex, pn_hll, pn_area, pn_full, pn_text, pn_other_commands, pn_save, pn_guards_other,
		pn_gradation, pn_gradation_hll, pn_gradation_save };
	const char *names[] = { "PN1", "PN2", "PN3", "PN4", "PN5", "PN6", "PN7", "PN8", "PN9", "PN10", "PN11" };
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
