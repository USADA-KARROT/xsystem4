/* msgwin_showlink_fixture.inc — the v14 message window show link: a parts
 * linked to the message windows' show is hidden together with its whole
 * subtree while the windows are hidden, for the drawing and for the input;
 * the pactex loader and SetMessageWindowActive do not link a parts, and
 * SetMessageWindowActive(false) does not hide the window (2026-11-07).
 * Mode: msgwin-showlink (SL).
 *
 * Original (static disassembly of the Chinese executable; the English one
 * has the same instructions at the same addresses, 0x53548e..0x5354d4):
 *   - 0x535260 updates one parts with the update context of its parent and
 *     then its children with the context it made. At 0x53548e..0x5354d4 it
 *     takes the show of this parts' context as
 *       Show (+0xab) && edit show (+0xac) && link && 0x537f10()
 *     where link is the context's message window show (+0xa1) when the
 *     parts' MessageWindowShowLink (+0xad) is set, else true; 0x579bb0
 *     (called at 0x535752) ANDs that into the context's show (+0xa0) and
 *     0x579690 (0x535761) keeps the result as the parts' context
 *     (+0x250), which its children start from. So the link hides the
 *     linked parts and its whole subtree, every update (0x53c490 writes the
 *     update's message window show into +0xa1 before the walk).
 *   - The input takes only parts whose context show (+0x2f0, that is
 *     +0x250 + 0xa0) is set and whose context alpha (+0x2ec) is above 0:
 *     the cursor walk 0x5471d0 (0x54725d) and the searches 0x545e10
 *     (0x545f11) and 0x547000 (0x547089). So a hidden linked subtree is
 *     neither entered nor pressed (see hover-events for the walk, the press
 *     and the release). The update 0x53c5b0 runs the input (0x53c64d)
 *     before the walk (0x53c67c), so the input presumably sees a change of
 *     the windows' show one update after the drawing does.
 *   - SetComponentMessageWindowShowLink (native case 105 -> 0x58ca00)
 *     writes +0xad of the get-or-create component; IsComponent... (case
 *     106 -> 0x58ca90) reads it. SetComponentShow (0x58c7a0) writes +0xab;
 *     IsComponentShow (0x58c830) reads the parts' own Show.
 *   - The pactex loader reads 信息窗口表示連動 as an integer (no key: 0) and
 *     keeps == 1 in its record (0x553e62..0x553ead, +0x29); nothing links a
 *     window because its type is the message window.
 *   - SetMessageWindowActive (case 499 -> 0x5950d0) takes the window of
 *     the number as the other message window calls do (0x53e010: a number
 *     above 0 gets its parts made when it has none, 0x53d920 -> 0x53a0b0
 *     -> 0x539c80 -> 0x578ee0, and a component of another kind is replaced
 *     by a message window, 0x536c20) and then only writes its active flag
 *     (+0xbc) and marks it changed (+0x208); it neither shows nor hides
 *     the window nor links it. The flag picks the base colour of the
 *     background (0x4f2678: white when active, else the window's inactive
 *     multiply colour).
 * Script (AIN, read only): the scripts link the root of the name plate's
 * activity (AdvNamePlate@Create), the root of the system buttons' activity
 * (a lambda of AdvSystemButton) and the message window parts of a message
 * window (CMessageWindow@Create); the window show is
 * CMessageWindowManager@IsShowAll, cleared while the player hides the
 * windows.
 *
 * SL1  a linked root without a visual: its child and grandchild are not
 *      drawn while the windows are hidden, and drawn as before when they
 *      are shown again; an unlinked sibling root is drawn throughout. A
 *      linked parts that hangs under nothing (on the system overlay) is
 *      drawn when linked, hidden with the windows and drawn again.
 * SL2  a linked message window: the window and a CG parts below it are
 *      not drawn while hidden, and drawn as before afterwards.
 * SL3  a linked parts below an unlinked one: the unlinked parent and the
 *      linked parts' sibling are drawn while hidden, the linked parts and
 *      its child are not; moved below that sibling while hidden (the
 *      update of a component whose parent changed), the sibling is drawn
 *      and the linked parts and its child are not.
 * SL4  changes while hidden, each seen at the next update: a parts made
 *      below the linked root is hidden; unlinked, the subtree is drawn
 *      (and the scene is marked to be drawn again); linked again, hidden;
 *      linked with the link it has, nothing is marked to be drawn again;
 *      a child moved below an unlinked root is drawn.
 * SL5  UpdateMatrix(false) hides the linked root's child, (true) shows it;
 *      the scene is marked to be drawn again when the show changes and not
 *      when it stays (the port draws a scene only when it is marked; the
 *      original draws every frame); UpdateParts (the GUIEngine binding)
 *      takes the show as well. Restarted (PartsEngine's _ModuleFini, as
 *      system.Reset) while hidden, the engine starts with the windows
 *      shown: a parts linked before the first update is drawn (the port's
 *      own start, kept; the original builds a new parts manager, 0x4b4bf0,
 *      and writes the show at each update).
 * SL6  input: a clickable parts below an unlinked parts below a linked
 *      root is entered while shown; while hidden it is not entered and a
 *      press and release on it do not click it; shown again, it is entered
 *      and clicked. Each change is checked two updates after it, so that
 *      the original's order (the input one update behind the drawing) and
 *      the port's (both in the same update) both pass.
 * SL7  input: pressed while hidden and released after the windows are
 *      shown again, the parts is not clicked (no element took the press);
 *      the next press and release click it. This holds in either order.
 * SL8  windows loaded from a pactex (SJIS and GBK spellings of the type,
 *      no 信息窗口表示連動 key) are not linked and are drawn while hidden.
 * SL9  SetMessageWindowActive(false) and (true): the window is drawn as
 *      before, not linked, its own show kept. "As before" is the port's:
 *      it keeps white as the base of an inactive window (the game's five
 *      windows have a white inactive colour; the default of a window made
 *      without a pactex was not found in the original). The port's, not
 *      the original's: on a CG parts nothing changes and a number with no
 *      parts gets none (the original's lookup would make the parts or put
 *      a window in place of the CG, see above; the port keeps its lookup,
 *      and the game calls it on its windows only).
 * SL10 guard: with no link, hiding the windows changes no drawing, also
 *      for parts shown again or moved while the windows are hidden, and
 *      showing them marks nothing to be drawn again.
 * SL11 guard: the windows shown, the drawing of a scene with links does
 *      not change over updates (its hash is printed for the before/after
 *      record).
 * SL12 guard: below a linked root, a child with its own show false and
 *      one hidden in the editor (編輯上表示 0) stay hidden after the windows
 *      are shown again; IsComponentShow reads the parts' own show, also
 *      while the windows are hidden; a parts linked two levels below a
 *      root with its own show false stays hidden when it is linked and
 *      when the windows are shown again; the child hidden in the editor,
 *      moved below a shown sibling, stays hidden.
 * SL13 guard, AIN v13: PE_Update(false) hides only the linked parts
 *      itself, its child is drawn (the port's behaviour before v14 kept),
 *      also after the linked parts is moved below a new parent while the
 *      windows are hidden (the update of a component whose parent
 *      changed); a change of the windows' show or of a link alone marks
 *      nothing to be drawn again (the render check reads them when the
 *      scene is drawn).
 * SL14 guard: a clickable parts with no link is entered and clicked while
 *      the windows are hidden.
 *
 * Not covered: the original's screen of the backlog (the one of the right
 * click's hide was captured, not kept in this repository); a window's
 * inactive multiply colour other than white and its default (the port reads
 * neither the pactex colour nor SetMessageWindowInactiveMultipleColor; the
 * game's windows are white); the pactex key 信息窗口表示連動 = 1 (the
 * port does not read it; that the parser's +0x29 is the parts' +0xad is
 * inferred from the offsets of 表示 and 編輯上表示; the game's pactex has
 * 0 everywhere);
 * 0x537f10's own part of the context show (a link to a check box's state,
 * SetComponentCheckBoxShowLinkNumber, +0x134; not implemented in the
 * port); a save and load while hidden; the multi-controller layers.
 *
 * Synthetic data only: a CG made in memory, ASCII text, the probe's own
 * fonts and pactex trees. Uses the msgwin_config, hover_events and
 * base_ui_review helpers and forked_cases.inc.
 */

enum { SL_R = 19601, SL_C, SL_G, SL_S, SL_W, SL_WC, SL_P, SL_L, SL_L2, SL_K, SL_N, SL_U, SL_B, SL_X,
	SL_D, SL_E, SL_M, SL_NONE = 19699 };

static void sl_update(bool show)
{
	mc_update_view();
	BU_PE("UpdateComponent", BU_I(16), BU_I(16), BU_I(show), BU_F(1.0f), BU_F(1.0f));
}
static void sl_link(int no, bool on) { BU_PE("SetComponentMessageWindowShowLink", BU_I(no), BU_I(on)); }
static bool sl_is_linked(int no) { return BU_PE("IsComponentMessageWindowShowLink", BU_I(no)).i != 0; }
static bool sl_is_show(int no) { return BU_PE("IsComponentShow", BU_I(no)).i != 0; }
static void sl_active(int no, bool on) { BU_PE("SetMessageWindowActive", BU_I(no), BU_I(on)); }

/* The given parts drawn one after another into one view, in that order. */
static uint8_t *sl_draw(const int *nos, int n)
{
	Texture dst = { 0 };
	gfx_init_texture_rgba(&dst, MC_VIEW_W, MC_VIEW_H, (SDL_Color){ 0, 0, 0, 0 });
	GLuint fbo = gfx_set_framebuffer(GL_DRAW_FRAMEBUFFER, &dst, 0, 0, dst.w, dst.h);
	for (int i = 0; i < n; i++)
		parts_render(parts_get(nos[i]));
	gfx_reset_framebuffer(GL_DRAW_FRAMEBUFFER, fbo);
	uint8_t *px = gfx_get_pixels(&dst);
	gfx_delete_texture(&dst);
	return px;
}
static bool sl_same(const uint8_t *a, const uint8_t *b)
{
	return !memcmp(a, b, (size_t)MC_VIEW_W * MC_VIEW_H * 4);
}

/* The CG's 24x16 region at (x, y): 1 when every pixel is the CG, 0 when
 * every pixel is empty, -1 otherwise. */
static int sl_region(const uint8_t *px, int x, int y)
{
	bool cg = true, empty = true;
	for (int j = 0; j < MC_BG_H; j++) {
		for (int i = 0; i < MC_BG_W; i++) {
			const uint8_t *p = mc_px(px, x + i, y + j);
			if (p[0] != mc_bg_rgb[0] || p[1] != mc_bg_rgb[1] || p[2] != mc_bg_rgb[2] || p[3] != 255)
				cg = false;
			if (p[3])
				empty = false;
		}
	}
	return cg ? 1 : empty ? 0 : -1;
}
#define SL_AT(px, x, y, want, ...) XC_CHECK(sl_region(px, x, y) == (want), __VA_ARGS__)

/* --- SL1 --- */
static void sl1_root(void)
{
	mc_setup();
	PE_SetPos(SL_R, 4, 4);
	mc_cg_parts(SL_C, 0, 0);
	PE_SetParentPartsNumber(SL_C, SL_R);
	mc_cg_parts(SL_G, 28, 0);
	PE_SetParentPartsNumber(SL_G, SL_C);
	mc_cg_parts(SL_S, 60, 4);
	PE_UpdateComponent(0);
	xc_ready();
	sl_link(SL_R, true);
	XC_CHECK(sl_is_linked(SL_R) && !sl_is_linked(SL_C), "SL1: linked root %d, child %d; want 1, 0",
		 sl_is_linked(SL_R), sl_is_linked(SL_C));
	static const int all[] = { SL_R, SL_C, SL_G, SL_S };
	sl_update(true);
	uint8_t *shown = sl_draw(all, 4);
	SL_AT(shown, 4, 4, 1, "SL1: shown: the child is not drawn");
	SL_AT(shown, 32, 4, 1, "SL1: shown: the grandchild is not drawn");
	SL_AT(shown, 60, 4, 1, "SL1: shown: the sibling is not drawn");
	sl_update(false);
	uint8_t *hidden = sl_draw(all, 4);
	SL_AT(hidden, 4, 4, 0, "SL1: hidden: the linked root's child is drawn");
	SL_AT(hidden, 32, 4, 0, "SL1: hidden: the linked root's grandchild is drawn");
	SL_AT(hidden, 60, 4, 1, "SL1: hidden: the unlinked sibling root is not drawn");
	sl_update(true);
	uint8_t *again = sl_draw(all, 4);
	XC_CHECK(sl_same(shown, again), "SL1: shown again, the drawing differs from the first one");
	free(shown);
	free(hidden);
	free(again);
	/* a linked parts that hangs under nothing (made on the system overlay,
	 * which has no root here) */
	const int layer = ci_active();
	ci_set_active(PARTS_CONTROLLER_SYSTEM_OVERLAY);
	mc_cg_parts(SL_X, 4, 44);
	ci_set_active(layer);
	if (parts_get(SL_X)->parent)
		xc_env_fail("the overlay parts has a parent");
	sl_link(SL_X, true);
	static const int top[] = { SL_X };
	uint8_t *px = sl_draw(top, 1);
	SL_AT(px, 4, 44, 1, "SL1: linked, with no parent: the parts is not drawn");
	free(px);
	sl_update(false);
	px = sl_draw(top, 1);
	SL_AT(px, 4, 44, 0, "SL1: hidden: the linked parts with no parent is drawn");
	free(px);
	sl_update(true);
	px = sl_draw(top, 1);
	SL_AT(px, 4, 44, 1, "SL1: shown again: the linked parts with no parent is not drawn");
	free(px);
}

/* --- SL2 --- */
static void sl2_window(void)
{
	mc_setup();
	mc_window(SL_W);
	mc_cg_parts(SL_WC, 0, 24);
	PE_SetParentPartsNumber(SL_WC, SL_W);
	PE_UpdateComponent(0);
	xc_ready();
	sl_link(SL_W, true);
	static const int all[] = { SL_W, SL_WC };
	sl_update(true);
	uint8_t *shown = sl_draw(all, 2);
	XC_CHECK(MC_BG_CG(shown, "SL2 shown") && mc_text_ink(shown) > 20, "SL2: shown: the window is not drawn");
	SL_AT(shown, 4, 28, 1, "SL2: shown: the parts below the window is not drawn");
	sl_update(false);
	uint8_t *hidden = sl_draw(all, 2);
	XC_CHECK(mc_bg_is(hidden, 0, 0, 0, 0, 0, "SL2 hidden") && !mc_text_ink(hidden),
		 "SL2: hidden: the linked window is drawn");
	SL_AT(hidden, 4, 28, 0, "SL2: hidden: the parts below the linked window is drawn");
	sl_update(true);
	uint8_t *again = sl_draw(all, 2);
	XC_CHECK(sl_same(shown, again), "SL2: shown again, the drawing differs from the first one");
	free(shown);
	free(hidden);
	free(again);
}

/* --- SL3 --- */
static void sl3_nested(void)
{
	mc_setup();
	mc_cg_parts(SL_P, 4, 4);
	mc_cg_parts(SL_L, 28, 0);
	PE_SetParentPartsNumber(SL_L, SL_P);
	mc_cg_parts(SL_L2, 28, 0);
	PE_SetParentPartsNumber(SL_L2, SL_L);
	mc_cg_parts(SL_K, 0, 20);
	PE_SetParentPartsNumber(SL_K, SL_P);
	PE_UpdateComponent(0);
	xc_ready();
	sl_link(SL_L, true);
	static const int all[] = { SL_P, SL_L, SL_L2, SL_K };
	sl_update(true);
	uint8_t *px = sl_draw(all, 4);
	XC_CHECK(sl_region(px, 4, 4) == 1 && sl_region(px, 32, 4) == 1 && sl_region(px, 60, 4) == 1
		 && sl_region(px, 4, 24) == 1, "SL3: shown: not all four are drawn");
	free(px);
	sl_update(false);
	px = sl_draw(all, 4);
	SL_AT(px, 4, 4, 1, "SL3: hidden: the unlinked parent is not drawn");
	SL_AT(px, 4, 24, 1, "SL3: hidden: the linked parts' unlinked sibling is not drawn");
	SL_AT(px, 32, 4, 0, "SL3: hidden: the linked parts is drawn");
	SL_AT(px, 60, 4, 0, "SL3: hidden: the linked parts' child is drawn");
	free(px);
	/* the linked parts moved below its unlinked sibling while hidden */
	PE_SetParentPartsNumber(SL_L, SL_K);
	sl_update(false);
	px = sl_draw(all, 4);
	SL_AT(px, 4, 24, 1, "SL3: moved while hidden: the new unlinked parent is not drawn");
	SL_AT(px, 32, 24, 0, "SL3: moved while hidden: the linked parts is drawn");
	SL_AT(px, 60, 24, 0, "SL3: moved while hidden: the linked parts' child is drawn");
	free(px);
}

/* --- SL4 --- */
static void sl4_changes(void)
{
	mc_setup();
	PE_SetPos(SL_R, 4, 4);
	mc_cg_parts(SL_C, 0, 0);
	PE_SetParentPartsNumber(SL_C, SL_R);
	PE_SetPos(SL_U, 60, 4);
	PE_UpdateComponent(0);
	xc_ready();
	sl_link(SL_R, true);
	static const int all[] = { SL_R, SL_C, SL_N, SL_U };
	sl_update(true);
	sl_update(false);
	/* a parts made below the linked root while hidden */
	mc_cg_parts(SL_N, 28, 0);
	PE_SetParentPartsNumber(SL_N, SL_R);
	sl_update(false);
	uint8_t *px = sl_draw(all, 4);
	SL_AT(px, 4, 4, 0, "SL4: hidden: the linked root's child is drawn");
	SL_AT(px, 32, 4, 0, "SL4: hidden: a parts made below the linked root while hidden is drawn");
	free(px);
	/* unlinked while hidden; the scene is to be drawn again */
	parts_engine_clean();
	scene_is_dirty = false;
	sl_link(SL_R, false);
	parts_render_update();
	XC_CHECK(scene_is_dirty, "SL4: unlinked while hidden, the scene is not marked to be drawn again");
	scene_is_dirty = true;
	sl_update(false);
	px = sl_draw(all, 4);
	SL_AT(px, 4, 4, 1, "SL4: unlinked while hidden: the child is not drawn");
	SL_AT(px, 32, 4, 1, "SL4: unlinked while hidden: the new child is not drawn");
	free(px);
	/* linked again while hidden */
	sl_link(SL_R, true);
	sl_update(false);
	px = sl_draw(all, 4);
	SL_AT(px, 4, 4, 0, "SL4: linked again while hidden: the child is drawn");
	SL_AT(px, 32, 4, 0, "SL4: linked again while hidden: the new child is drawn");
	free(px);
	/* linked again with the link it has: nothing to draw again */
	parts_engine_clean();
	scene_is_dirty = false;
	sl_link(SL_R, true);
	parts_render_update();
	XC_CHECK(!scene_is_dirty, "SL4: linked with the link it has, the scene is marked to be drawn again");
	scene_is_dirty = true;
	/* the child moved below an unlinked root while hidden */
	PE_SetParentPartsNumber(SL_C, SL_U);
	sl_update(false);
	px = sl_draw(all, 4);
	SL_AT(px, 60, 4, 1, "SL4: moved below an unlinked root while hidden, the child is not drawn");
	SL_AT(px, 32, 4, 0, "SL4: the parts left below the linked root is drawn");
	free(px);
	sl_update(true);
	px = sl_draw(all, 4);
	SL_AT(px, 60, 4, 1, "SL4: shown: the moved child is not drawn");
	SL_AT(px, 32, 4, 1, "SL4: shown: the new child is not drawn");
	free(px);
}

/* --- SL5 --- */
static void sl5_matrix(void)
{
	mc_setup();
	PE_SetPos(SL_R, 4, 4);
	mc_cg_parts(SL_C, 0, 0);
	PE_SetParentPartsNumber(SL_C, SL_R);
	PE_UpdateComponent(0);
	xc_ready();
	sl_link(SL_R, true);
	static const int all[] = { SL_R, SL_C };
	sl_update(true);
	BU_PE("UpdateMatrix", BU_I(0), BU_F(1.0f), BU_F(1.0f));
	uint8_t *px = sl_draw(all, 2);
	SL_AT(px, 4, 4, 0, "SL5: UpdateMatrix(false): the linked root's child is drawn");
	free(px);
	BU_PE("UpdateMatrix", BU_I(1), BU_F(1.0f), BU_F(1.0f));
	px = sl_draw(all, 2);
	SL_AT(px, 4, 4, 1, "SL5: UpdateMatrix(true): the linked root's child is not drawn");
	free(px);
	/* the scene is drawn again when the show changes, not when it stays */
	parts_engine_clean();
	scene_is_dirty = false;
	BU_PE("UpdateMatrix", BU_I(1), BU_F(1.0f), BU_F(1.0f));
	parts_render_update();
	const bool same = scene_is_dirty;
	parts_engine_clean();
	scene_is_dirty = false;
	BU_PE("UpdateMatrix", BU_I(0), BU_F(1.0f), BU_F(1.0f));
	parts_render_update();
	const bool changed = scene_is_dirty;
	scene_is_dirty = true;
	XC_CHECK(!same && changed, "SL5: the scene is marked to be drawn again %d with the show kept, %d with it changed; want 0, 1",
		 same, changed);
	BU_PE("UpdateMatrix", BU_I(1), BU_F(1.0f), BU_F(1.0f));
	/* UpdateParts (the GUIEngine binding) takes the show too */
	PE_UpdateParts(16, false, false);
	px = sl_draw(all, 2);
	SL_AT(px, 4, 4, 0, "SL5: UpdateParts(false): the linked root's child is drawn");
	free(px);
	PE_UpdateParts(16, false, true);
	px = sl_draw(all, 2);
	SL_AT(px, 4, 4, 1, "SL5: UpdateParts(true): the linked root's child is not drawn");
	free(px);
	/* a restart (system.Reset runs PartsEngine's _ModuleFini) while the
	 * windows are hidden: the new engine starts with them shown, so a parts
	 * linked before its first update is drawn */
	sl_update(false);
	extern struct static_library lib_PartsEngine;
	void (*fini)(void) = sr_fn(&lib_PartsEngine, "_ModuleFini");
	if (!fini)
		xc_env_fail("PartsEngine has no _ModuleFini");
	fini();
	mc_cg_parts(SL_C, 4, 4);
	sl_link(SL_C, true);
	static const int restarted[] = { SL_C };
	px = sl_draw(restarted, 1);
	SL_AT(px, 4, 4, 1, "SL5: restarted while hidden: a parts linked before the first update is not drawn");
	free(px);
}

/* --- input: SL6, SL7, SL14 (no GL, as hover-events) --- */
static void sl_input_setup(bool link)
{
	he_env();
	br_input_setup();
	PE_SetPos(SL_R, 0, 0);
	PE_SetPos(SL_M, 0, 0);
	PE_SetParentPartsNumber(SL_M, SL_R);
	br_box(SL_B, 100, 100, 200, 80, true);
	PE_SetParentPartsNumber(SL_B, SL_M);
	extern bool key_state[];
	key_state[VK_LBUTTON] = key_state[VK_RBUTTON] = key_state[VK_MBUTTON] = false;
	PE_UpdateComponent(0);
	if (link)
		sl_link(SL_R, true);
	PE_BeginInput();
}
/* One update through the v14 UpdateComponent (which runs the input). */
static struct he_frame sl_in(int x, int y, bool left, bool show)
{
	extern bool key_state[];
	mouse_set_pos(x, y);
	key_state[VK_LBUTTON] = left;
	BU_PE("UpdateComponent", BU_I(16), BU_I(16), BU_I(show), BU_F(1.0f), BU_F(1.0f));
	return he_drain();
}
static bool sl_clicked(const struct he_frame *f)
{
	return he_count(f, 4, SL_B) > 0;
}

static void sl6_input(void)
{
	sl_input_setup(true);
	xc_ready();
	struct parts *b = parts_get(SL_B);
	sl_in(640, 600, false, true);
	sl_in(640, 600, false, true);
	sl_in(150, 140, false, true);
	XC_CHECK(b->is_hovered, "SL6: shown: the parts is not entered");
	sl_in(150, 140, false, false);
	sl_in(150, 140, false, false);
	XC_CHECK(!b->is_hovered, "SL6: hidden: the parts below the linked root is entered");
	struct he_frame press = sl_in(150, 140, true, false);
	struct he_frame release = sl_in(150, 140, false, false);
	XC_CHECK(!sl_clicked(&press) && !sl_clicked(&release) && he_hll_int("GetClickNumber") != SL_B,
		 "SL6: hidden: a press and release on the parts below the linked root clicked it");
	sl_in(150, 140, false, true);
	sl_in(150, 140, false, true);
	XC_CHECK(b->is_hovered, "SL6: shown again: the parts is not entered");
	sl_in(150, 140, true, true);
	release = sl_in(150, 140, false, true);
	XC_CHECK(sl_clicked(&release) && he_hll_int("GetClickNumber") == SL_B,
		 "SL6: shown again: a press and release do not click the parts");
	PE_EndInput();
}

static void sl7_press_hidden(void)
{
	sl_input_setup(true);
	xc_ready();
	sl_in(640, 600, false, true);
	sl_in(640, 600, false, true);
	sl_in(150, 140, false, true);
	sl_in(150, 140, false, false);
	struct he_frame press = sl_in(150, 140, true, false);
	struct he_frame release = sl_in(150, 140, false, true);
	XC_CHECK(!sl_clicked(&press) && !sl_clicked(&release) && he_hll_int("GetClickNumber") != SL_B,
		 "SL7: pressed while hidden and released when shown, the parts was clicked");
	sl_in(150, 140, true, true);
	release = sl_in(150, 140, false, true);
	XC_CHECK(sl_clicked(&release), "SL7: the next press and release do not click the parts");
	PE_EndInput();
}

/* --- SL8 --- */
static void sl8_pactex(void)
{
	mc_setup();
	static const struct mc_win wins[] = {
		{ "WinG", MC_T_MSGWIN_GBK, MC_K_TEXT_GBK, "AB", true, false },
		{ "WinS", MC_T_MSGWIN_SJIS, MC_K_TEXT_GBK, "AB", true, false },
	};
	int no[2];
	xc_ready();
	bool ok = mc_load(wins, 2, no);
	XC_CHECK(ok, "SL8: the activity did not load");
	if (!ok)
		return;
	for (int i = 0; i < 2; i++) {
		XC_CHECK(!sl_is_linked(no[i]) && !PE_GetPartsMessageWindowShowLink(no[i]),
			 "SL8 %s: the window loaded from the pactex is linked to the message windows' show", wins[i].name);
		PE_SetPos(no[i], 4, 4);
		PE_UpdateComponent(0);
		sl_update(false);
		uint8_t *px = mc_draw(no[i]);
		XC_CHECK(MC_BG_CG(px, wins[i].name), "SL8 %s: hidden: the window loaded from the pactex is not drawn",
			 wins[i].name);
		free(px);
		PE_SetPos(no[i], 200, 200);
		PE_UpdateComponent(0);
	}
}

/* --- SL9 --- */
static void sl9_active(void)
{
	mc_setup();
	mc_window(SL_W);
	mc_cg_parts(SL_X, 4, 44);
	PE_UpdateComponent(0);
	xc_ready();
	static const int win[] = { SL_W }, cg[] = { SL_X };
	sl_update(true);
	uint8_t *ref = sl_draw(win, 1), *ref_cg = sl_draw(cg, 1);
	sl_active(SL_W, false);
	sl_update(true);
	uint8_t *px = sl_draw(win, 1);
	XC_CHECK(sl_same(ref, px), "SL9: SetMessageWindowActive(false) changed the window's drawing");
	free(px);
	XC_CHECK(!sl_is_linked(SL_W), "SL9: SetMessageWindowActive linked the window to the message windows' show");
	XC_CHECK(sl_is_show(SL_W), "SL9: SetMessageWindowActive(false) cleared the window's own show");
	sl_update(false);
	px = sl_draw(win, 1);
	XC_CHECK(sl_same(ref, px), "SL9: the inactive, unlinked window is not drawn while the windows are hidden");
	free(px);
	sl_active(SL_W, true);
	sl_update(true);
	px = sl_draw(win, 1);
	XC_CHECK(sl_same(ref, px) && !sl_is_linked(SL_W), "SL9: SetMessageWindowActive(true): drawing or link changed");
	free(px);
	sl_active(SL_X, false);
	sl_update(true);
	px = sl_draw(cg, 1);
	XC_CHECK(sl_same(ref_cg, px) && !sl_is_linked(SL_X) && sl_is_show(SL_X),
		 "SL9: SetMessageWindowActive(false) on a CG parts changed it (the port's lookup)");
	free(px);
	sl_active(SL_NONE, false);
	XC_CHECK(!PE_IsExist(SL_NONE), "SL9: SetMessageWindowActive made a parts for a number with none (the port's lookup)");
	free(ref);
	free(ref_cg);
}

/* --- SL10 --- */
static void sl10_no_link(void)
{
	mc_setup();
	PE_SetPos(SL_R, 4, 4);
	mc_cg_parts(SL_C, 0, 20);
	PE_SetParentPartsNumber(SL_C, SL_R);
	mc_cg_parts(SL_G, 28, 0);
	PE_SetParentPartsNumber(SL_G, SL_C);
	mc_window(SL_W);
	PE_UpdateComponent(0);
	xc_ready();
	static const int all[] = { SL_W, SL_R, SL_C, SL_G };
	sl_update(true);
	uint8_t *shown = sl_draw(all, 4);
	XC_CHECK(MC_BG_CG(shown, "SL10 shown") && sl_region(shown, 4, 24) == 1 && sl_region(shown, 32, 24) == 1,
		 "SL10: shown: not everything is drawn");
	sl_update(false);
	uint8_t *hidden = sl_draw(all, 4);
	XC_CHECK(sl_same(shown, hidden), "SL10: with no link, hiding the windows changed the drawing");
	free(shown);
	free(hidden);
	/* while hidden: an unlinked parts shown again, and one moved (the
	 * update of a component whose parent changed) */
	PE_SetShow(SL_C, false);
	PE_SetShow(SL_C, true);
	PE_SetParentPartsNumber(SL_G, SL_R);
	sl_update(false);
	static const int changed[] = { SL_C, SL_G };
	uint8_t *px = sl_draw(changed, 2);
	SL_AT(px, 4, 24, 1, "SL10: hidden: an unlinked parts shown again is not drawn");
	SL_AT(px, 32, 4, 1, "SL10: hidden: an unlinked parts moved is not drawn");
	free(px);
	/* with no link, a change of the windows' show marks nothing to draw
	 * again */
	parts_engine_clean();
	scene_is_dirty = false;
	BU_PE("UpdateMatrix", BU_I(1), BU_F(1.0f), BU_F(1.0f));
	parts_render_update();
	XC_CHECK(!scene_is_dirty, "SL10: with no link, showing the windows marked the scene to be drawn again");
	scene_is_dirty = true;
}

/* --- SL11 --- */
static uint32_t sl_hash(const uint8_t *px) { return mc_hash(px); }
static void sl11_default(void)
{
	mc_setup();
	mc_window(SL_W);
	PE_SetPos(SL_R, 4, 4);
	mc_cg_parts(SL_C, 0, 20);
	PE_SetParentPartsNumber(SL_C, SL_R);
	mc_cg_parts(SL_G, 28, 0);
	PE_SetParentPartsNumber(SL_G, SL_C);
	mc_cg_parts(SL_S, 88, 44);
	PE_UpdateComponent(0);
	xc_ready();
	sl_link(SL_W, true);
	sl_link(SL_R, true);
	static const int all[] = { SL_W, SL_R, SL_C, SL_G, SL_S };
	sl_update(true);
	uint8_t *first = sl_draw(all, 5);
	sl_update(true);
	sl_update(true);
	uint8_t *last = sl_draw(all, 5);
	printf("  SL11 drawing hash first %08x last %08x\n", sl_hash(first), sl_hash(last));
	XC_CHECK(MC_BG_CG(first, "SL11") && mc_text_ink(first) > 20 && sl_region(first, 4, 24) == 1
		 && sl_region(first, 32, 24) == 1 && sl_region(first, 88, 44) == 1, "SL11: the scene is not drawn");
	XC_CHECK(sl_same(first, last), "SL11: the drawing changed over updates with the windows shown");
	free(first);
	free(last);
}

/* --- SL12 --- */
static void sl12_own_show(void)
{
	mc_setup();
	PE_SetPos(SL_R, 4, 4);
	mc_cg_parts(SL_C, 0, 0);
	PE_SetParentPartsNumber(SL_C, SL_R);
	mc_cg_parts(SL_D, 28, 0);
	PE_SetParentPartsNumber(SL_D, SL_R);
	mc_cg_parts(SL_E, 56, 0);
	PE_SetParentPartsNumber(SL_E, SL_R);
	PE_SetShow(SL_C, false);
	parts_set_edit_hidden(parts_get(SL_E), true);
	/* a CG two levels below a root with its own show false */
	PE_SetPos(SL_P, 0, 40);
	PE_SetPos(SL_K, 0, 0);
	PE_SetParentPartsNumber(SL_K, SL_P);
	mc_cg_parts(SL_L, 4, 4);
	PE_SetParentPartsNumber(SL_L, SL_K);
	PE_SetShow(SL_P, false);
	PE_UpdateComponent(0);
	xc_ready();
	sl_link(SL_R, true);
	static const int all[] = { SL_R, SL_C, SL_D, SL_E };
	sl_update(false);
	XC_CHECK(sl_is_show(SL_D) && !sl_is_show(SL_C) && sl_is_show(SL_R),
		 "SL12: hidden: IsComponentShow of the shown child %d, the hidden one %d, the root %d; want 1, 0, 1",
		 sl_is_show(SL_D), sl_is_show(SL_C), sl_is_show(SL_R));
	sl_update(true);
	uint8_t *px = sl_draw(all, 4);
	SL_AT(px, 4, 4, 0, "SL12: shown again: the child with its own show false is drawn");
	SL_AT(px, 32, 4, 1, "SL12: shown again: the child is not drawn");
	SL_AT(px, 60, 4, 0, "SL12: shown again: the child hidden in the editor is drawn");
	free(px);
	XC_CHECK(sl_is_show(SL_D) && !sl_is_show(SL_C), "SL12: shown again: IsComponentShow changed");
	/* linked below the hidden root, and the windows hidden and shown again */
	static const int below[] = { SL_L };
	sl_link(SL_L, true);
	px = sl_draw(below, 1);
	SL_AT(px, 4, 44, 0, "SL12: linked below a hidden root: the parts is drawn");
	free(px);
	sl_update(false);
	sl_update(true);
	px = sl_draw(below, 1);
	SL_AT(px, 4, 44, 0, "SL12: below a hidden root, shown again: the linked parts is drawn");
	free(px);
	/* the child hidden in the editor moved below its shown sibling (the
	 * update of a component whose parent changed) */
	static const int moved[] = { SL_E };
	PE_SetParentPartsNumber(SL_E, SL_D);
	sl_update(true);
	px = sl_draw(moved, 1);
	SL_AT(px, 88, 4, 0, "SL12: moved below its sibling: the child hidden in the editor is drawn");
	free(px);
}

/* --- SL13 --- */
static void sl13_v13(void)
{
	mc_setup();
	xc_ready();
	ain->version = 13;
	mc_cg_parts(SL_R, 4, 4);
	mc_cg_parts(SL_C, 28, 0);
	PE_SetParentPartsNumber(SL_C, SL_R);
	PE_SetPartsMessageWindowShowLink(SL_R, true);
	PE_UpdateComponent(0);
	static const int all[] = { SL_R, SL_C };
	PE_Update(16, true);
	uint8_t *px = sl_draw(all, 2);
	XC_CHECK(sl_region(px, 4, 4) == 1 && sl_region(px, 32, 4) == 1, "SL13: v13: shown: not both drawn");
	free(px);
	PE_Update(16, false);
	px = sl_draw(all, 2);
	SL_AT(px, 4, 4, 0, "SL13: v13: hidden: the linked parts is drawn");
	SL_AT(px, 32, 4, 1, "SL13: v13: hidden: the linked parts' child is not drawn (v13 kept)");
	free(px);
	PE_Update(16, true);
	px = sl_draw(all, 2);
	SL_AT(px, 4, 4, 1, "SL13: v13: shown again: the linked parts is not drawn");
	free(px);
	/* the linked parts moved below a new parent while hidden */
	PE_Update(16, false);
	mc_cg_parts(SL_P, 4, 44);
	PE_SetParentPartsNumber(SL_R, SL_P);
	PE_Update(16, false);
	static const int moved[] = { SL_P, SL_R, SL_C };
	px = sl_draw(moved, 3);
	SL_AT(px, 4, 44, 1, "SL13: v13: moved while hidden: the new parent is not drawn");
	SL_AT(px, 36, 48, 1, "SL13: v13: moved while hidden: the linked parts' child is not drawn (v13 kept)");
	free(px);
	/* v13: a change of the windows' show or of a link alone marks nothing
	 * to draw again (the render check reads them when the scene is
	 * drawn), as before */
	parts_engine_clean();
	scene_is_dirty = false;
	PE_UpdateParts(16, false, true);
	const bool by_show = scene_is_dirty;
	parts_engine_clean();
	scene_is_dirty = false;
	PE_SetPartsMessageWindowShowLink(SL_C, true);
	parts_render_update();
	const bool by_link = scene_is_dirty;
	scene_is_dirty = true;
	XC_CHECK(!by_show && !by_link,
		 "SL13: v13: the scene is marked to be drawn again %d by the show, %d by a link; want 0, 0 (v13 kept)",
		 by_show, by_link);
	ain->version = 14;
}

/* --- SL14 --- */
static void sl14_unlinked_input(void)
{
	sl_input_setup(false);
	xc_ready();
	struct parts *b = parts_get(SL_B);
	sl_in(640, 600, false, true);
	sl_in(640, 600, false, true);
	sl_in(150, 140, false, false);
	sl_in(150, 140, false, false);
	XC_CHECK(b->is_hovered, "SL14: hidden: the unlinked parts is not entered");
	sl_in(150, 140, true, false);
	struct he_frame release = sl_in(150, 140, false, false);
	XC_CHECK(sl_clicked(&release) && he_hll_int("GetClickNumber") == SL_B,
		 "SL14: hidden: a press and release do not click the unlinked parts");
	PE_EndInput();
}

static int test_msgwin_showlink(void)
{
	static const struct xc_case cases[] = {
		{ "SL1", sl1_root }, { "SL2", sl2_window }, { "SL3", sl3_nested }, { "SL4", sl4_changes },
		{ "SL5", sl5_matrix }, { "SL6", sl6_input }, { "SL7", sl7_press_hidden }, { "SL8", sl8_pactex },
		{ "SL9", sl9_active }, { "SL10", sl10_no_link }, { "SL11", sl11_default }, { "SL12", sl12_own_show },
		{ "SL13", sl13_v13 }, { "SL14", sl14_unlinked_input },
	};
	return xc_run("MSGWIN-SHOWLINK", cases, sizeof cases / sizeof cases[0], 60);
}
