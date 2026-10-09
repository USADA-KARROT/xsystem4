/* parts-transform: a parts' rotation and scale act on its whole subtree, the
 * add colour accumulates by addition, and 減算色モード subtracts it.
 *
 * Original (static disassembly only): the per-parts update 0x535260 copies
 * the parent's accumulated sprite parameters and applies the parts' own
 * with 0x579bb0: the multiply colour and alpha are multiplied (+0x80..+0x88,
 * +0x9c, x/255 truncated), the add colour is ADDED (+0x8c..+0x94), and
 * 0x4e6d80 multiplies the accumulated matrix by
 *     S(reverse) * T(-pivot) * S(scale) * R * T(pos)
 * (row vectors; 0x5c3ae0 builds S * Ry * Rx * Rz and writes the translation
 * last), so a child's anchor and drawing turn and scale around the parent's
 * anchor. The hit test 0x57a6b0 and Parts_GetPartsUpperLeftPos (0x534bb0)
 * go through the same matrix. 減算色モード is read by the pactex parser as
 * (value == 1) (0x553fdc -> 0x554020); 0x535260 multiplies the parts' own
 * add colour by -1 when the flag (+0xc4) is set (0x53544d..0x5356f7), the
 * sum is signed and not clamped (0x579c5c), and 0x5aac60 / 0x5b2330 draw a
 * sum whose three components are all below 1 as a subtraction of its
 * negation, anything else as an addition with negative components at 0.
 * The sprite gets the multiply colour (with the alpha) and the add colour
 * as two colours (0x46a460); that the add comes after the multiply follows
 * from PlayerFrameView's silhouette (multiply black, add (255,0,186), pink
 * in the original), the pixel shader was not found.
 *
 * Before the fix a parent's rotation moved nothing (the children's anchors
 * stayed at parent pos + child pos, their drawings upright), a parent's
 * scale moved the children but did not scale their drawings through the
 * accumulated scale when the parent rotated, the add colour was multiplied
 * (0 under any parent) and the sub colour mode did not exist.
 *
 * Cases (each in its own fork):
 *   PT1 real AIN declarations: a parent rotated 315 degrees (the battle
 *       GaugeBase) with a child at (2,2) (GaugeHp) and one at (10,0): the
 *       children's global positions, the corners of the child's box through
 *       parts_box_transform, the inverse parts_screen_to_box, Parts_IsCursorIn
 *       at two points where the upright box kept here and the original's
 *       turned one agree, and Parts_GetPartsUpperLeftPosX; then the parent
 *       scaled 0.5 as well, and the rotation removed again.
 *   PT2 a TEXT child under a rotated rectangle (MapNodeView's MainText/Text):
 *       the glyph transform (parts_anchor_transform without the parts' own
 *       rotation) turns with the parent; a grandchild's level composes.
 *   PT3 the add colour: a child under a parent with the default (0,0,0)
 *       keeps its own (255,0,186) (PlayerFrameView's pink silhouette), a
 *       parent's and a child's add, and the sum is clamped at 255; the
 *       multiply colour still multiplies.
 *   PT4 SetComponentSubColorMode / IsComponentSubColorMode through their AIN
 *       declarations, the pactex key 減算色模式 through the loader, and the
 *       flag after PE_Save / PE_Load (XPE version 8).
 *   PT5 drawn pixels (offscreen GL, like text-size): multiply black plus
 *       add (255,0,186) is pink; grey * 128/255 + (10,20,30); a white parts
 *       in 減算色モード with (0,0,128) is (255,255,127); under a parent that
 *       adds (50,0,0) the same child is drawn added, its negative blue as 0;
 *       a plain child under a parent in the mode inherits the negative sum
 *       (subtracted) until its own colour makes a component positive.
 *   PT6 Parts_GetPartsUpperLeftPos of a parts with a surface area (the box's
 *       corner is then not its (0,0)) under a rotated, then a scaled parent:
 *       the corner goes through the whole matrix (0x534bb0), it is not the
 *       corner's offset added to the transformed anchor.
 *   PT7 with layers (controller-id, layer-root). A layer's root parts is the
 *       identity in the transform and in the add colour sum: a top-level
 *       parts under it is placed and coloured as without it, its child
 *       turns around it, also after SetGlobalPos. The save of a stack whose
 *       IDs are not the positions' (controller-id CI11) with a parts in
 *       減算色モード: the list of such parts comes after the hidden list and
 *       before the controller ID section, which stays the end of the save;
 *       the layers' IDs, the parts' layers and the flag come back.
 *   PT8 guard, AIN version 13: the engines before v14 keep the rule they
 *       had (no original of theirs was read): a parent's rotation moves
 *       nothing, a child's position is the parent's plus its own scaled by
 *       the parent's accumulated scale and rounded at each level, a box is
 *       T(global pos) * Rz(own) * S(accumulated scale), the upper-left
 *       corner is the anchor plus the box corner's unscaled offset, and
 *       SetGlobalPos places every parts from the new position.
 *   PT9 a parent released on its own (ReleaseParts): its child and that
 *       child's child are placed from the child's own position at the next
 *       update, without the released parts' rotation and add colour.
 *   PT10 the same with layers: the child hangs under its layer's root
 *       again; erasing the layer afterwards leaves no parts and no root.
 *   PT11 PE_Load links the children and combines their transforms itself:
 *       before any update the child of a rotated parent is where it was
 *       saved (anchor, box, upper-left corner, add colour sum).
 * Only functions that exist before the fix are called by name (the new
 * HLL functions go through the AIN declarations), so before-check builds.
 */
static int pt_failures;
#define PT_CHECK(c, ...) do { if (!(c)) { printf("  FAIL: "); printf(__VA_ARGS__); printf("\n"); pt_failures++; } } while (0)
#define PT_NEAR(a, b) (fabsf((a) - (b)) < 0.02f)

static void pt_rot(int n, float deg) { ri_hll("SetComponentRotateZ", 2, (union vm_value[]){ RI_I(n), RI_F(deg) }); }
static void pt_mag(int n, float sx, float sy)
{
	ri_hll("SetComponentMagX", 2, (union vm_value[]){ RI_I(n), RI_F(sx) });
	ri_hll("SetComponentMagY", 2, (union vm_value[]){ RI_I(n), RI_F(sy) });
}
static void pt_add(int n, int r, int g, int b) { ri_hll("SetComponentAddColor", 4, (union vm_value[]){ RI_I(n), RI_I(r), RI_I(g), RI_I(b) }); }
static void pt_mul(int n, int r, int g, int b) { ri_hll("SetComponentMulColor", 4, (union vm_value[]){ RI_I(n), RI_I(r), RI_I(g), RI_I(b) }); }
static float pt_upper_left_y(int n) { return ri_hll("Parts_GetPartsUpperLeftPosY", 2, (union vm_value[]){ RI_I(n), RI_I(1) }).f; }

static void pt_expect_pos(const char *step, int n, int x, int y)
{
	struct parts *p = parts_get(n);
	PT_CHECK(p->global.pos.x == x && p->global.pos.y == y,
		 "%s: parts %d global pos (%d,%d), want (%d,%d)", step, n, p->global.pos.x, p->global.pos.y, x, y);
}

/* The box point (bx,by) of parts n must land at (ex,ey) and come back. */
static void pt_expect_box(const char *step, int n, float bx, float by, float ex, float ey)
{
	struct parts *p = parts_get(n);
	mat4 m;
	parts_box_transform(p, &p->states[0].common, m);
	float gx, gy;
	ri_apply(m, bx, by, &gx, &gy);
	PT_CHECK(PT_NEAR(gx, ex) && PT_NEAR(gy, ey), "%s: parts %d box (%g,%g) -> (%g,%g), want (%g,%g)",
		 step, n, bx, by, gx, gy, ex, ey);
	float ix = -1, iy = -1;
	bool ok = parts_screen_to_box(p, &p->states[0].common, ex, ey, &ix, &iy);
	PT_CHECK(ok && PT_NEAR(ix, bx) && PT_NEAR(iy, by), "%s: parts %d screen (%g,%g) -> box (%g,%g), want (%g,%g)",
		 step, n, ex, ey, ix, iy, bx, by);
}

/* PT1: a rotated (then scaled) parent through the AIN declarations. */
static void pt_rotated_parent(void)
{
	ri_postlink();
	const int P = 9301, C = 9302, N = 9303;
	parts_get(P); parts_get(C); parts_get(N);
	ri_pos(P, 400, 300); ri_rect(P, 104, 22); pt_rot(P, 315);
	ri_parent(C, P); ri_pos(C, 2, 2); ri_rect(C, 100, 18);
	ri_parent(N, P); ri_pos(N, 10, 0); ri_rect(N, 20, 10);
	PE_UpdateComponent(0);
	const float c = cosf(-45.0f * (float)M_PI / 180.0f), s = sinf(-45.0f * (float)M_PI / 180.0f);
	/* R(315) * (2,2) = (2c - 2s, 2s + 2c) = (2.83, 0): the anchor rounds to (403,300). */
	pt_expect_pos("rotated", C, 403, 300);
	pt_expect_pos("rotated", N, 400 + (int)lroundf(10 * c), 300 + (int)lroundf(10 * s));
	pt_expect_pos("rotated", P, 400, 300);
	/* The child's box turns with the parent around the parent's anchor. */
	float ax = 400 + 2 * c - 2 * s, ay = 300 + 2 * s + 2 * c;
	pt_expect_box("rotated", C, 0, 0, ax, ay);
	pt_expect_box("rotated", C, 100, 0, ax + 100 * c, ay + 100 * s);
	pt_expect_box("rotated", C, 0, 18, ax - 18 * s, ay + 18 * c);
	pt_expect_box("rotated", C, 50, 9, ax + 50 * c - 9 * s, ay + 50 * s + 9 * c);
	/* The parent's own box turns around its anchor as before. */
	pt_expect_box("rotated", P, 104, 0, 400 + 104 * c, 300 + 104 * s);
	/* The rectangle hit test here keeps an upright box at the moved anchor,
	 * (403,300) 100x18; the original transforms the box's corners with the
	 * matrix (0x57a6b0), a band from the anchor up to the right. Only points
	 * are asserted on which the two agree (and the corners' bounding box,
	 * should that be what the original tests): (410,302) lies in all of
	 * them, (450,319) in none; before the fix it was inside (the box was
	 * (402,302) 100x18). (450,301) is inside the upright box only: the
	 * known difference, not a requirement. */
	PT_CHECK(ri_in(C, 410, 302) == 1, "rotated: Parts_IsCursorIn(C, 410, 302) must be 1 (next to the anchor (403,300))");
	PT_CHECK(ri_in(C, 450, 319) == 0, "rotated: Parts_IsCursorIn(C, 450, 319) must be 0 (below the box and the turned band)");
	/* The upper-left corner is the box's (0,0) through the matrix (0x534bb0). */
	float ul = ri_upper_left_x(C), uly = pt_upper_left_y(C);
	PT_CHECK(ul == 403.0f && uly == 300.0f, "rotated: Parts_GetPartsUpperLeftPos(C) = (%g,%g), want (403,300)", ul, uly);
	ul = ri_upper_left_x(N);
	PT_CHECK(ul == (float)(400 + (int)lroundf(10 * c)), "rotated: Parts_GetPartsUpperLeftPosX(N) = %g, want %d", ul, 400 + (int)lroundf(10 * c));

	/* The parent scaled by 0.5 as well: S then R (0x5c3ae0 builds S * R). */
	pt_mag(P, 0.5f, 0.5f);
	PE_UpdateComponent(0);
	ax = 400 + 0.5f * (2 * c - 2 * s); ay = 300 + 0.5f * (2 * s + 2 * c);
	pt_expect_pos("scaled", C, (int)lroundf(ax), (int)lroundf(ay));
	pt_expect_box("scaled", C, 100, 0, ax + 50 * c, ay + 50 * s);
	pt_expect_box("scaled", C, 0, 18, ax - 9 * s, ay + 9 * c);
	struct parts *cp = parts_get(C);
	PT_CHECK(PT_NEAR(cp->global.scale.x, 0.5f) && PT_NEAR(cp->global.scale.y, 0.5f),
		 "scaled: C global scale (%g,%g), want (0.5,0.5)", cp->global.scale.x, cp->global.scale.y);

	/* Rotation and scale removed: the layout from before. */
	pt_rot(P, 0); pt_mag(P, 1, 1);
	PE_UpdateComponent(0);
	pt_expect_pos("restored", C, 402, 302);
	pt_expect_pos("restored", N, 410, 300);
	pt_expect_box("restored", C, 100, 0, 502, 302);
	PT_CHECK(ri_in(C, 450, 319) == 1, "restored: Parts_IsCursorIn(C, 450, 319) must be 1 again");
	printf("PT1 checked\n");
}

/* PT2: TEXT under a rotated rectangle, and three levels composed. */
static void pt_text_child(void)
{
	ri_postlink();
	const int P = 9311, T = 9312, G = 9313;
	parts_get(P); parts_get(T); parts_get(G);
	ri_pos(P, 500, 400); ri_rect(P, 160, 28); pt_rot(P, 323.75f);
	ri_parent(T, P); ri_pos(T, 0, -14); ri_origin(T, 4); ri_rect(T, 60, 19);
	ri_parent(G, T); ri_pos(G, 30, 5); ri_rect(G, 8, 8); pt_rot(G, 10); pt_mag(G, 2, 2);
	PE_UpdateComponent(0);
	const float th = 323.75f * (float)M_PI / 180.0f, c = cosf(th), s = sinf(th);
	/* T's anchor: P's anchor + R(323.75) * (0,-14). */
	float ax = 500 + 14 * s, ay = 400 - 14 * c;
	pt_expect_pos("text", T, (int)lroundf(ax), (int)lroundf(ay));
	/* The glyph frame (no own rotation or scale) turns with the parent. */
	mat4 m;
	parts_anchor_transform(parts_get(T), 0.0f, false, m);
	float gx, gy;
	ri_apply(m, 7, 3, &gx, &gy);
	PT_CHECK(PT_NEAR(gx, ax + 7 * c - 3 * s) && PT_NEAR(gy, ay + 7 * s + 3 * c),
		 "text: glyph offset (7,3) -> (%g,%g), want (%g,%g)", gx, gy, ax + 7 * c - 3 * s, ay + 7 * s + 3 * c);
	/* G: T's level (no rotation, no scale) then its own R(10) * S(2). */
	float gax = ax + 30 * c - 5 * s, gay = ay + 30 * s + 5 * c;
	pt_expect_pos("grandchild", G, (int)lroundf(gax), (int)lroundf(gay));
	const float t2 = (323.75f + 10.0f) * (float)M_PI / 180.0f, c2 = cosf(t2), s2 = sinf(t2);
	pt_expect_box("grandchild", G, 8, 0, gax + 16 * c2, gay + 16 * s2);
	struct parts *g = parts_get(G);
	PT_CHECK(PT_NEAR(g->global.rotation.z, 333.75f), "grandchild: global rotation %g, want 333.75", g->global.rotation.z);
	printf("PT2 checked\n");
}

/* PT3: the add colour adds along the chain; the multiply colour multiplies. */
static void pt_add_color(void)
{
	ri_postlink();
	const int P = 9321, C = 9322, G = 9323, S = 9324;
	parts_get(P); parts_get(C); parts_get(G); parts_get(S);
	ri_parent(C, P); ri_parent(G, C); ri_parent(S, P);
	PE_UpdateComponent(0);
	/* A child's own add colour under a parent with the default (0,0,0). */
	pt_add(S, 255, 0, 186);
	SDL_Color sc = parts_get(S)->global.add_color;
	PT_CHECK(sc.r == 255 && sc.g == 0 && sc.b == 186, "add: child of a default parent (%d,%d,%d), want (255,0,186)", sc.r, sc.g, sc.b);
	pt_add(P, 10, 20, 30);
	pt_add(C, 100, 0, 0);
	pt_add(G, 200, 250, 0);
	SDL_Color cc = parts_get(C)->global.add_color, gc = parts_get(G)->global.add_color;
	PT_CHECK(cc.r == 110 && cc.g == 20 && cc.b == 30, "add: C (%d,%d,%d), want (110,20,30)", cc.r, cc.g, cc.b);
	PT_CHECK(gc.r == 255 && gc.g == 255 && gc.b == 30, "add: G (%d,%d,%d), want (255,255,30) clamped", gc.r, gc.g, gc.b);
	sc = parts_get(S)->global.add_color;
	PT_CHECK(sc.r == 255 && sc.g == 20 && sc.b == 216, "add: S after the parent's (%d,%d,%d), want (255,20,216)", sc.r, sc.g, sc.b);
	/* The same through the per-frame recombination. */
	PE_UpdateComponent(0);
	gc = parts_get(G)->global.add_color;
	PT_CHECK(gc.r == 255 && gc.g == 255 && gc.b == 30, "add after update: G (%d,%d,%d), want (255,255,30)", gc.r, gc.g, gc.b);
	/* Multiply still multiplies: black under white is black. */
	pt_mul(S, 0, 0, 0);
	SDL_Color sm = parts_get(S)->global.multiply_color;
	PT_CHECK(sm.r == 0 && sm.g == 0 && sm.b == 0, "mul: S (%d,%d,%d), want (0,0,0)", sm.r, sm.g, sm.b);
	pt_mul(P, 128, 128, 128); pt_mul(C, 255, 128, 0);
	SDL_Color cm = parts_get(C)->global.multiply_color;
	PT_CHECK(cm.r == 128 && cm.g == 64 && cm.b == 0, "mul: C (%d,%d,%d), want (128,64,0)", cm.r, cm.g, cm.b);
	printf("PT3 checked\n");
}

/* PT4: the sub colour mode: HLL, pactex key, save. */
#define PT_SUB_GBK "\x9c\x70\xcb\xe3\xc9\xab\xc4\xa3\xca\xbd"	/* 減算色模式 */
/* Through the AIN declaration: the flag is not a member before the fix. */
static bool pt_is_sub(int n) { return ri_hll("IsComponentSubColorMode", 1, (union vm_value[]){ RI_I(n) }).i == 1; }
static void pt_sub_color_mode(void)
{
	ri_postlink();
	const int N = 9331;
	parts_get(N);
	ri_hll("SetComponentSubColorMode", 2, (union vm_value[]){ RI_I(N), RI_I(1) });
	int is = ri_hll("IsComponentSubColorMode", 1, (union vm_value[]){ RI_I(N) }).i;
	PT_CHECK(is == 1, "IsComponentSubColorMode after set 1 = %d, want 1", is);
	ri_hll("SetComponentSubColorMode", 2, (union vm_value[]){ RI_I(N), RI_I(0) });
	is = ri_hll("IsComponentSubColorMode", 1, (union vm_value[]){ RI_I(N) }).i;
	PT_CHECK(is == 0, "IsComponentSubColorMode after set 0 = %d, want 0", is);

	/* The pactex key on a component: Sub = 1, Plain = 0, Missing without it. */
	struct ex *e = xcalloc(1, sizeof(*e));
	e->nr_blocks = 1;
	e->blocks = xcalloc(1, sizeof(*e->blocks));
	e->blocks[0].name = cstr_to_string("SubColorFixture");
	e->blocks[0].val.type = EX_TREE;
	struct ex_tree *top = xcalloc(1, sizeof(*top));
	*top = bu_branch("SubColorFixture", 1);
	e->blocks[0].val.tree = top;
	top->children[0] = bu_comp("SubColorRoot", BU_LAYOUT, 0, 3);
	struct ex_tree *kids = &top->children[0].children[1];
	const char *names[] = { "Sub", "Plain", "Missing" };
	for (int i = 0; i < 3; i++) {
		struct ex_tree t = bu_branch(names[i], i < 2 ? 2 : 1);
		t.children[0] = bu_branch(BU_INFO, 1);
		t.children[0].children[0] = bu_sleaf(BU_TYPE, BU_LOW);
		if (i < 2)
			t.children[1] = bu_ileaf(PT_SUB_GBK, i == 0 ? 1 : 0);
		kids->children[i] = t;
	}
	struct string *name = cstr_to_string("SubColorFixture");
	bool loaded = probe_load_activity_tree(name, e);
	free_string(name); ex_free(e);
	PT_CHECK(loaded, "the synthetic activity did not load");
	if (!loaded)
		return;
	int act = sf_str("SubColorFixture", -1);
	int sub = bu_number(act, "Sub"), plain = bu_number(act, "Plain"), missing = bu_number(act, "Missing");
	PT_CHECK(sub > 0 && plain > 0 && missing > 0, "parts numbers %d %d %d", sub, plain, missing);
	if (sub <= 0 || plain <= 0 || missing <= 0)
		return;
	PT_CHECK(pt_is_sub(sub), "Sub (減算色模式 = 1) must have the flag");
	PT_CHECK(!pt_is_sub(plain), "Plain (減算色模式 = 0) must not");
	PT_CHECK(!pt_is_sub(missing), "Missing (no key) must not");

	/* PE_Save / PE_Load keep it (XPE version 8). */
	PE_UpdateComponent(0);
	struct page *saved = NULL;
	PT_CHECK(PE_Save(&saved), "PE_Save failed");
	if (!saved)
		return;
	PT_CHECK(ca_save_version(saved) >= 8, "XPE version %d, want 8 or later", ca_save_version(saved));
	/* PE_Load releases every parts and builds them again from the page. */
	PT_CHECK(PE_Load(&saved), "PE_Load failed");
	PE_UpdateComponent(0);
	PT_CHECK(pt_is_sub(sub), "Sub lost the flag over PE_Save / PE_Load");
	PT_CHECK(!pt_is_sub(plain), "Plain gained the flag over PE_Save / PE_Load");
	printf("PT4 checked\n");
}

/* PT5: what is drawn. One 8x8 texture of one grey per parts, drawn alone
 * into a view-sized target (ts_draw); the pixel 4 right and below its
 * corner. */
static void pt_tex_parts(int n, int x, int y, int grey)
{
	struct parts *p = parts_get(n);
	struct parts_cg *cg = parts_get_cg(p, 0);
	gfx_init_texture_rgba(&cg->common.texture, 8, 8, (SDL_Color){ grey, grey, grey, 255 });
	parts_set_dims(p, &cg->common, 8, 8);
	PE_SetPos(n, x, y);
}
static void pt_expect_pixel(const char *what, int n, int x, int y, int r, int g, int b)
{
	uint8_t *px = ts_draw(parts_get(n));
	const uint8_t *c = px + ((y + 4) * TS_VIEW_W + x + 4) * 4;
	PT_CHECK(abs(c[0] - r) <= 1 && abs(c[1] - g) <= 1 && abs(c[2] - b) <= 1 && c[3] == 255,
		 "%s: pixel (%d,%d,%d,%d), want (%d,%d,%d,255)", what, c[0], c[1], c[2], c[3], r, g, b);
	free(px);
}
static void pt_sub_mode(int n) { ri_hll("SetComponentSubColorMode", 2, (union vm_value[]){ RI_I(n), RI_I(1) }); }
static void pt_drawn_colour(void)
{
	ts_render_setup();	/* GL, the parts shader, a 96x128 view */
	const int A = 9341, B = 9342, S = 9343, P = 9344, T = 9345, Q = 9346, U = 9347, V = 9348;
	/* PlayerFrameView's silhouette (Exclude): multiply black, add pink. */
	pt_tex_parts(A, 4, 4, 255);
	pt_mul(A, 0, 0, 0); pt_add(A, 255, 0, 186);
	/* Multiply, then add: 100 * 128/255 + (10, 20, 30). */
	pt_tex_parts(B, 20, 4, 100);
	pt_mul(B, 128, 128, 128); pt_add(B, 10, 20, 30);
	/* MapNodeView's white name in the mode, blinking with (0, 0, 128). */
	pt_tex_parts(S, 40, 4, 255);
	pt_sub_mode(S); pt_add(S, 0, 0, 128);
	/* The same kind of child under a parent that adds: the sum (50, 0, -40)
	 * has a positive component, so it is added and the blue counts 0. */
	pt_tex_parts(P, 4, 40, 100); pt_add(P, 50, 0, 0);
	pt_tex_parts(T, 16, 0, 100); ri_parent(T, P);
	pt_sub_mode(T); pt_add(T, 0, 0, 40);
	/* A parent in the mode: its children inherit the negative sum, not the
	 * mode. U's (10,10,10) leaves (-20,-20,-20), V's (40,30,30) (10,0,0). */
	pt_tex_parts(Q, 4, 60, 100);
	pt_sub_mode(Q); pt_add(Q, 30, 30, 30);
	pt_tex_parts(U, 16, 0, 100); ri_parent(U, Q); pt_add(U, 10, 10, 10);
	pt_tex_parts(V, 32, 0, 100); ri_parent(V, Q); pt_add(V, 40, 30, 30);
	PE_UpdateComponent(0);
	pt_expect_pixel("multiply black + add (255,0,186)", A, 4, 4, 255, 0, 186);
	pt_expect_pixel("grey 100 * 128/255 + (10,20,30)", B, 20, 4, 60, 70, 80);
	pt_expect_pixel("white in the mode, (0,0,128)", S, 40, 4, 255, 255, 127);
	pt_expect_pixel("parent adding (50,0,0)", P, 4, 40, 150, 100, 100);
	pt_expect_pixel("child in the mode under it, sum (50,0,-40)", T, 20, 40, 150, 100, 100);
	pt_expect_pixel("parent in the mode, (30,30,30)", Q, 4, 60, 70, 70, 70);
	pt_expect_pixel("plain child, sum (-20,-20,-20)", U, 20, 60, 80, 80, 80);
	pt_expect_pixel("plain child, sum (10,0,0)", V, 36, 60, 110, 100, 100);
	/* Leaving the mode puts the parts' colour back on the plus side. */
	ri_hll("SetComponentSubColorMode", 2, (union vm_value[]){ RI_I(Q), RI_I(0) });
	PE_UpdateComponent(0);
	pt_expect_pixel("the parent out of the mode", Q, 4, 60, 130, 130, 130);
	pt_expect_pixel("its child then, sum (40,40,40)", U, 20, 60, 140, 140, 140);
	printf("PT5 checked\n");
}

/* PT6: the upper-left corner of a box that does not start at (0,0). */
static void pt_expect_upper_left(const char *step, int n, int x, int y)
{
	float ux = ri_upper_left_x(n), uy = pt_upper_left_y(n);
	PT_CHECK(ux == (float)x && uy == (float)y, "%s: Parts_GetPartsUpperLeftPos(%d) = (%g,%g), want (%d,%d)",
		 step, n, ux, uy, x, y);
}
static void pt_surface_area_corner(void)
{
	ri_postlink();
	const int P = 9351, C = 9352;
	parts_get(P); parts_get(C);
	ri_pos(P, 300, 200); ri_rect(P, 40, 40);
	/* C: 16x12, origin at its centre, showing the 8x6 at (2,3) of its surface.
	 * The origin offset is then (-4,-3) - (2,3) = (-6,-6), the box's corner
	 * (2,3), and the corner sits at (-4,-3) from the anchor. */
	ri_parent(C, P); ri_pos(C, 10, 0); ri_rect(C, 16, 12); ri_origin(C, 5);
	struct parts *c = parts_get(C);
	parts_set_surface_area(c, &c->states[0].common, 2, 3, 8, 6);
	PE_UpdateComponent(0);
	pt_expect_upper_left("plain", C, 306, 197);
	/* The parent turned 90 degrees ((x,y) -> (-y,x) with y down): the anchor
	 * goes to (300,210) and the corner's (-4,-3) to (3,-4). Adding the box
	 * corner (2,3) to the transformed origin (300,210) + R(-6,-6) = (306,204)
	 * would give (308,207). */
	pt_rot(P, 90);
	PE_UpdateComponent(0);
	pt_expect_pos("rotated", C, 300, 210);
	pt_expect_upper_left("rotated", C, 303, 206);
	/* 315 degrees: R(x,y) = ((x + y) / sqrt 2, (y - x) / sqrt 2). */
	pt_rot(P, 315);
	PE_UpdateComponent(0);
	const float q = 0.70710678f;
	pt_expect_upper_left("rotated 315", C, (int)lroundf(300 + 10 * q - 7 * q), (int)lroundf(200 - 10 * q + 1 * q));
	/* Scaled instead: the corner's offset doubles with everything else. */
	pt_rot(P, 0); pt_mag(P, 2, 2);
	PE_UpdateComponent(0);
	pt_expect_pos("scaled", C, 320, 200);
	pt_expect_upper_left("scaled", C, 312, 194);
	/* The parts' own scale acts on it as well (the anchor stays). */
	pt_mag(P, 1, 1); pt_mag(C, 3, 1);
	PE_UpdateComponent(0);
	pt_expect_upper_left("own scale", C, 310 - 12, 197);
	printf("PT6 checked\n");
}

/* PT7: XPE version 8 with layers. The controller ID section is found as the
 * last nr + 3 words of a save (save.c load_controller_ids), so the list of
 * sub colour parts has to be written before it. */
static void pt_save_with_layers(void)
{
	ci_setup();
	int a, b, c;
	ci_inserted(&a, &b, &c);		/* [0, a, c, b], active c */
	/* A layer's root (layer-root LR1) is the identity in the transform and
	 * in the add colour sum: a top-level parts is placed and coloured as
	 * without it, and its child turns around it. */
	struct parts *root = parts_try_get(b), *pb = parts_try_get(CI_PB);
	PT_CHECK(root && pb && pb->parent == root, "CI_PB does not hang under the root of its layer %d", b);
	parts_get(CI_PO);
	ri_parent(CI_PO, CI_PB); ri_pos(CI_PO, 10, 0);
	pt_rot(CI_PB, 90); pt_add(CI_PB, 10, 20, 30); pt_add(CI_PO, 1, 2, 3);
	PE_UpdateComponent(0);
	pt_expect_pos("top-level parts under a layer root", CI_PB, 100, 100);
	pt_expect_pos("child of a rotated top-level parts", CI_PO, 100, 110);
	SDL_Color tc = pb->global.add_color, oc = parts_get(CI_PO)->global.add_color;
	PT_CHECK(tc.r == 10 && tc.g == 20 && tc.b == 30 && oc.r == 11 && oc.g == 22 && oc.b == 33,
		 "add colours under a layer root (%d,%d,%d) (%d,%d,%d), want (10,20,30) (11,22,33)",
		 tc.r, tc.g, tc.b, oc.r, oc.g, oc.b);
	parts_set_global_pos((Point){ 7, 9 });
	PE_UpdateComponent(0);
	pt_expect_pos("top-level parts after SetGlobalPos", CI_PB, 107, 109);
	pt_expect_pos("its child after SetGlobalPos", CI_PO, 107, 119);
	parts_set_global_pos((Point){ 0, 0 });
	pt_rot(CI_PB, 0); pt_add(CI_PB, 0, 0, 0); pt_add(CI_PO, 0, 0, 0);
	PE_UpdateComponent(0);
	pt_expect_pos("child after the rotation is removed", CI_PO, 110, 100);
	pt_sub_mode(CI_PA);
	pt_add(CI_PA, 0, 0, 128);
	PE_UpdateComponent(0);
	PT_CHECK(pt_is_sub(CI_PA) && !pt_is_sub(CI_P0), "SetComponentSubColorMode did not take");
	struct page *saved = NULL;
	PT_CHECK(PE_Save(&saved) && saved, "PE_Save failed");
	if (!saved)
		return;
	int n = 4, section = ci_id_section(saved, n), end = saved->nr_vars - section;
	PT_CHECK(ca_save_version(saved) >= 8, "XPE version %d, want 8 or later", ca_save_version(saved));
	PT_CHECK(section == n + 3, "the save does not end with the controller ID section of %d layers", n);
	PT_CHECK(end >= 3 && saved->values[end - 3].i == 0 && saved->values[end - 2].i == 1 && saved->values[end - 1].i == CI_PA,
		 "the words before the ID section are %d %d %d; want 0 (no hidden parts), 1 and %d (one sub colour parts)",
		 end >= 3 ? saved->values[end - 3].i : -1, end >= 2 ? saved->values[end - 2].i : -1,
		 end >= 1 ? saved->values[end - 1].i : -1, CI_PA);
	/* something else in between */
	ci_erase(c);
	ci_erase(b);
	ci_erase(a);
	ci_part(CI_PX);
	PT_CHECK(PE_Load(&saved), "PE_Load failed");
	PE_UpdateComponent(0);
	ci_layers("PT7 after PE_Load", 4, (int[]){ CI_ID(0), a, c, b });
	PT_CHECK(ci_active() == c, "active %d after PE_Load, want %d", ci_active(), c);
	PT_CHECK(ci_owner(CI_P0) == CI_ID(0) && ci_owner(CI_PA) == a && ci_owner(CI_PB) == b && ci_owner(CI_PC) == c
		 && !parts_try_get(CI_PX), "owners after PE_Load %d %d %d %d, want %d %d %d %d",
		 ci_owner(CI_P0), ci_owner(CI_PA), ci_owner(CI_PB), ci_owner(CI_PC), CI_ID(0), a, b, c);
	PT_CHECK(pt_is_sub(CI_PA), "the parts lost 減算色モード over PE_Save / PE_Load with layers");
	PT_CHECK(!pt_is_sub(CI_P0) && !pt_is_sub(CI_PB) && !pt_is_sub(CI_PC), "another parts gained 減算色モード over PE_Save / PE_Load");
	pt_failures += ci_failures;
	ca_free_buffer(saved);
	printf("PT7 checked\n");
}

/* PT8: the engines before v14 keep their rule. The values are those of the
 * formulas from before the matrix (parts_child_pos, the earlier
 * parts_anchor_transform and parts_screen_upper_left). */
static void pt_expect_ul(const char *step, int n, int x, int y)
{
	int ux = PE_GetPartsUpperLeftPosX(n, 1), uy = PE_GetPartsUpperLeftPosY(n, 1);
	PT_CHECK(ux == x && uy == y, "%s: PE_GetPartsUpperLeftPos(%d) = (%d,%d), want (%d,%d)", step, n, ux, uy, x, y);
}
static void pt_v13_guard(void)
{
	ri_postlink();
	ain->version = 13;
	const int P = 9361, C = 9362, N = 9363, S = 9364, Q = 9365, D = 9366, F = 9367, E = 9368;
	parts_get(P); parts_get(C); parts_get(N);
	PE_SetPos(P, 400, 300); PE_SetPartsRectangleDetectionSize(P, 104, 22, 1); PE_SetPartsRotateZ(P, 315);
	PE_SetParentPartsNumber(C, P); PE_SetPos(C, 2, 2); PE_SetPartsRectangleDetectionSize(C, 100, 18, 1);
	PE_SetParentPartsNumber(N, P); PE_SetPos(N, 10, 0); PE_SetPartsRectangleDetectionSize(N, 20, 10, 1);
	PE_UpdateComponent(0);
	const float c = cosf(-45.0f * (float)M_PI / 180.0f), s = sinf(-45.0f * (float)M_PI / 180.0f);
	/* The parent's rotation moves nothing below it; the children's boxes
	 * stay upright at parent pos + child pos. */
	pt_expect_pos("v13 rotated", P, 400, 300);
	pt_expect_pos("v13 rotated", C, 402, 302);
	pt_expect_pos("v13 rotated", N, 410, 300);
	pt_expect_box("v13 rotated", C, 0, 0, 402, 302);
	pt_expect_box("v13 rotated", C, 100, 0, 502, 302);
	pt_expect_box("v13 rotated", C, 0, 18, 402, 320);
	pt_expect_ul("v13 rotated", C, 402, 302);
	pt_expect_ul("v13 rotated", N, 410, 300);
	PT_CHECK(PE_IsCursorIn(C, 450, 319, 1) && !PE_IsCursorIn(C, 450, 301, 1) && !PE_IsCursorIn(C, 401, 310, 1),
		 "v13 rotated: the child's hit box is not (402,302) 100x18");
	/* The parent's own box turns around its anchor. */
	pt_expect_box("v13 rotated", P, 104, 0, 400 + 104 * c, 300 + 104 * s);
	/* The rotation changed afterwards: still nothing moves. */
	PE_SetPartsRotateZ(P, 90);
	pt_expect_pos("v13 rotated again, no update", C, 402, 302);
	PE_UpdateComponent(0);
	pt_expect_pos("v13 rotated again", C, 402, 302);
	pt_expect_box("v13 rotated again", C, 100, 0, 502, 302);

	/* The parent scaled 0.5: 400 + lround(0.5 * 2), 400 + lround(0.5 * 10);
	 * the child's box is scaled by the accumulated scale. */
	PE_SetPartsMagX(P, 0.5f); PE_SetPartsMagY(P, 0.5f);
	pt_expect_pos("v13 scaled, no update", C, 401, 301);
	PE_UpdateComponent(0);
	pt_expect_pos("v13 scaled", C, 401, 301);
	pt_expect_pos("v13 scaled", N, 405, 300);
	pt_expect_box("v13 scaled", C, 100, 0, 451, 301);
	pt_expect_box("v13 scaled", C, 0, 18, 401, 310);
	pt_expect_ul("v13 scaled", C, 401, 301);

	/* Three levels, each rounded on its own: Q (100,100) scaled 0.5, F at
	 * (3,3) -> 100 + lround(1.5) = 102, E at (3,3) under F -> 102 +
	 * lround(1.5) = 104 (one matrix would give 100 + 1.5 + 1.5 = 103). */
	parts_get(Q); parts_get(F); parts_get(E);
	PE_SetPos(Q, 100, 100); PE_SetPartsMagX(Q, 0.5f); PE_SetPartsMagY(Q, 0.5f);
	PE_SetParentPartsNumber(F, Q); PE_SetPos(F, 3, 3);
	PE_SetParentPartsNumber(E, F); PE_SetPos(E, 3, 3);
	PE_UpdateComponent(0);
	pt_expect_pos("v13 three levels", F, 102, 102);
	pt_expect_pos("v13 three levels", E, 104, 104);

	/* A parent scaled (2,1) and a child turned 90 degrees: the box point
	 * (10,0) is scaled first, then turned, (0,20) from the anchor (the
	 * matrix of v14 turns first: (0,10)). */
	PE_SetPartsMagX(Q, 2.0f); PE_SetPartsMagY(Q, 1.0f);
	parts_get(D);
	PE_SetParentPartsNumber(D, Q); PE_SetPartsRectangleDetectionSize(D, 20, 20, 1); PE_SetPartsRotateZ(D, 90);
	PE_UpdateComponent(0);
	pt_expect_pos("v13 turned child", D, 100, 100);
	pt_expect_box("v13 turned child", D, 10, 0, 100, 120);

	/* The upper-left corner of a scaled parts whose origin is its centre:
	 * the anchor plus the unscaled offset (-50,-25), not twice that. */
	parts_get(S);
	PE_SetPos(S, 300, 200); PE_SetPartsRectangleDetectionSize(S, 100, 50, 1); PE_SetPartsOriginPosMode(S, 5);
	PE_SetPartsMagX(S, 2.0f); PE_SetPartsMagY(S, 2.0f);
	PE_UpdateComponent(0);
	pt_expect_pos("v13 own scale", S, 300, 200);
	pt_expect_ul("v13 own scale", S, 250, 175);

	/* SetGlobalPos places every parts in the list from the new position,
	 * a child as well: one that comes after its parent in the list ends at
	 * its own position plus the offset (kept as it was, right or not). */
	bool seen_p = false, c_after_p = false;
	struct parts *q;
	PARTS_LIST_FOREACH(q) {
		if (q->no == P)
			seen_p = true;
		else if (q->no == C)
			c_after_p = seen_p;
	}
	parts_set_global_pos((Point){ 7, 9 });
	pt_expect_pos("v13 SetGlobalPos", P, 407, 309);
	pt_expect_pos("v13 SetGlobalPos", S, 307, 209);
	if (c_after_p)
		pt_expect_pos("v13 SetGlobalPos, a child after its parent in the list", C, 9, 11);
	else
		pt_expect_pos("v13 SetGlobalPos, a child before its parent in the list", C, 408, 310);
	/* A parts made afterwards is at (0,0) until it is given a position. */
	parts_get(9369);
	pt_expect_pos("v13 a new parts after SetGlobalPos", 9369, 0, 0);
	printf("PT8 checked\n");
}

/* PT9: a parent released on its own. */
static void pt_released_parent(void)
{
	ri_postlink();
	const int P = 9371, C = 9372, G = 9373;
	parts_get(P); parts_get(C); parts_get(G);
	ri_pos(P, 400, 300); ri_rect(P, 40, 40); pt_rot(P, 90); pt_add(P, 10, 20, 30);
	ri_parent(C, P); ri_pos(C, 10, 0); ri_rect(C, 20, 10);
	ri_parent(G, C); ri_pos(G, 5, 0); ri_rect(G, 8, 8); pt_add(G, 1, 2, 3);
	PE_UpdateComponent(0);
	pt_expect_pos("under the parent", C, 400, 310);
	pt_expect_pos("under the parent", G, 400, 315);
	pt_expect_box("under the parent", G, 8, 0, 400, 323);
	SDL_Color gc = parts_get(G)->global.add_color;
	PT_CHECK(gc.r == 11 && gc.g == 22 && gc.b == 33, "under the parent: G's add colour (%d,%d,%d), want (11,22,33)", gc.r, gc.g, gc.b);
	PE_ReleaseParts(P);
	PE_UpdateComponent(0);
	struct parts *cp = parts_try_get(C), *gp = parts_try_get(G);
	/* C hangs under nothing, or under its layer's root where there is one
	 * (PT10); either places it like a parts without a parent. */
	PT_CHECK(!parts_try_get(P) && cp && gp && (!cp->parent || parts_is_controller_root(cp->parent)) && gp->parent == cp,
		 "after ReleaseParts(parent): the parent %s, C %s, G %s", parts_try_get(P) ? "is there" : "is gone",
		 cp ? (cp->parent ? "has a parent" : "has no parent") : "is gone",
		 gp ? (gp->parent == cp ? "hangs under C" : "does not hang under C") : "is gone");
	if (!cp || !gp)
		return;
	/* Both from C's own position now, upright. */
	pt_expect_pos("parent released", C, 10, 0);
	pt_expect_pos("parent released", G, 15, 0);
	pt_expect_box("parent released", C, 20, 0, 30, 0);
	pt_expect_box("parent released", G, 8, 0, 23, 0);
	pt_expect_box("parent released", G, 0, 8, 15, 8);
	pt_expect_upper_left("parent released", G, 15, 0);
	gc = gp->global.add_color;
	PT_CHECK(gc.r == 1 && gc.g == 2 && gc.b == 3, "parent released: G's add colour (%d,%d,%d), want its own (1,2,3)", gc.r, gc.g, gc.b);
	printf("PT9 checked\n");
}

/* PT10: the same in a layer; the orphan goes back under the layer's root. */
static void pt_released_parent_layer(void)
{
	ci_setup();
	int a, b, c;
	ci_inserted(&a, &b, &c);		/* [0, a, c, b], active c; CI_PB in b */
	const int K = 9381, G = 9382;
	struct parts *root = parts_try_get(b);
	parts_get(K); parts_get(G);
	ri_parent(K, CI_PB); ri_pos(K, 10, 0); ri_rect(K, 20, 10);
	ri_parent(G, K); ri_pos(G, 5, 0); ri_rect(G, 8, 8);
	pt_rot(CI_PB, 90);
	PE_UpdateComponent(0);
	PT_CHECK(root && ci_owner(K) == b && ci_owner(G) == b, "K and G are in layers %d %d, want %d (their parent's)", ci_owner(K), ci_owner(G), b);
	pt_expect_pos("under the parent", K, 100, 110);
	pt_expect_pos("under the parent", G, 100, 115);
	size_t before = 0, after = 0;
	struct parts *q;
	PARTS_LIST_FOREACH(q) before++;
	PE_ReleaseParts(CI_PB);
	PARTS_LIST_FOREACH(q) after++;
	PT_CHECK(after == before - 1, "ReleaseParts(parent) changed the number of parts from %zu to %zu", before, after);
	struct parts *kp = parts_try_get(K);
	PT_CHECK(kp && kp->parent == root && parts_try_get(b) == root, "the orphan does not hang under the root of its layer %d", b);
	PE_UpdateComponent(0);
	pt_expect_pos("parent released", K, 10, 0);
	pt_expect_pos("parent released", G, 15, 0);
	pt_expect_box("parent released", G, 8, 0, 23, 0);
	/* The layer erased: its parts and its root go, and no root is made for
	 * a child whose parent went first. */
	parts_get(9383);
	ri_parent(9383, K);
	ci_erase(b);
	PT_CHECK(!parts_try_get(K) && !parts_try_get(G) && !parts_try_get(9383) && !parts_try_get(b),
		 "after EraseLayer: K %s, G %s, 9383 %s, the root %s", parts_try_get(K) ? "stayed" : "went",
		 parts_try_get(G) ? "stayed" : "went", parts_try_get(9383) ? "stayed" : "went", parts_try_get(b) ? "stayed" : "went");
	ci_layers("PT10 after EraseLayer", 3, (int[]){ CI_ID(0), a, c });
	PE_UpdateComponent(0);
	pt_failures += ci_failures;
	printf("PT10 checked\n");
}

/* PT11: PE_Load leaves nothing for the next update to place. */
static void pt_loaded_children(void)
{
	ri_postlink();
	const int P = 9391, C = 9392, G = 9393;
	parts_get(P); parts_get(C); parts_get(G);
	ri_pos(P, 400, 300); ri_rect(P, 40, 40); pt_rot(P, 90); pt_add(P, 10, 20, 30);
	ri_parent(C, P); ri_pos(C, 10, 0); ri_rect(C, 20, 10); pt_add(C, 1, 2, 3);
	ri_parent(G, C); ri_pos(G, 5, 2); ri_rect(G, 8, 8);
	PE_UpdateComponent(0);
	pt_expect_pos("before the save", C, 400, 310);
	pt_expect_pos("before the save", G, 398, 315);
	struct page *saved = NULL;
	PT_CHECK(PE_Save(&saved) && saved, "PE_Save failed");
	if (!saved)
		return;
	PT_CHECK(PE_Load(&saved), "PE_Load failed");
	/* No update here. */
	struct parts *pp = parts_try_get(P), *cp = parts_try_get(C), *gp = parts_try_get(G);
	PT_CHECK(pp && cp && gp && cp->parent == pp && gp->parent == cp, "after PE_Load, before any update: the children are not linked to their parents");
	if (!pp || !cp || !gp)
		return;
	pt_expect_pos("loaded", P, 400, 300);
	pt_expect_pos("loaded", C, 400, 310);
	pt_expect_pos("loaded", G, 398, 315);
	pt_expect_box("loaded", C, 20, 0, 400, 330);
	pt_expect_box("loaded", G, 8, 0, 398, 323);
	pt_expect_upper_left("loaded", C, 400, 310);
	PT_CHECK(PT_NEAR(gp->global.rotation.z, 90.0f), "loaded: G's global rotation %g, want 90", gp->global.rotation.z);
	SDL_Color gc = gp->global.add_color;
	PT_CHECK(gc.r == 11 && gc.g == 22 && gc.b == 33, "loaded: G's add colour (%d,%d,%d), want (11,22,33)", gc.r, gc.g, gc.b);
	/* The update changes nothing more. */
	PE_UpdateComponent(0);
	pt_expect_pos("loaded and updated", C, 400, 310);
	pt_expect_pos("loaded and updated", G, 398, 315);
	printf("PT11 checked\n");
}

static int pt_run(const char *name, void (*fn)(void))
{
	fflush(stdout);
	pid_t pid = fork();
	if (pid == 0) {
		pt_failures = 0;
		alarm(60);
		fn();
		fflush(stdout);
		_exit(pt_failures ? 1 : 0);
	}
	int status = 0;
	waitpid(pid, &status, 0);
	bool ok = WIFEXITED(status) && WEXITSTATUS(status) == 0;
	printf("%s %s%s\n", name, ok ? "PASS" : "FAIL", WIFSIGNALED(status) ? " (crashed)" : "");
	fflush(stdout);
	return ok ? 0 : 1;
}

static int test_parts_transform(void)
{
	int failed = 0;
	failed += pt_run("PT1 a rotated, scaled parent moves and turns its children", pt_rotated_parent);
	failed += pt_run("PT2 TEXT under a rotated rectangle, three levels", pt_text_child);
	failed += pt_run("PT3 the add colour adds along the chain", pt_add_color);
	failed += pt_run("PT4 SetComponentSubColorMode, pactex 減算色模式, save", pt_sub_color_mode);
	failed += pt_run("PT5 drawn: multiply then add, a negative sum subtracts", pt_drawn_colour);
	failed += pt_run("PT6 the upper-left corner of a surface area under a rotated parent", pt_surface_area_corner);
	failed += pt_run("PT7 a save with layers: the sub colour list and the controller ID section", pt_save_with_layers);
	failed += pt_run("PT8 guard: AIN version 13 keeps the rule from before the matrix", pt_v13_guard);
	failed += pt_run("PT9 a released parent's child and grandchild are placed anew", pt_released_parent);
	failed += pt_run("PT10 in a layer the orphan hangs under the root again", pt_released_parent_layer);
	failed += pt_run("PT11 PE_Load links and places the children itself", pt_loaded_children);
	printf("PARTS-TRANSFORM %s: %d of 11 cases failed\n", failed ? "FAIL" : "PASS", failed);
	return failed ? 92 : 0;
}
