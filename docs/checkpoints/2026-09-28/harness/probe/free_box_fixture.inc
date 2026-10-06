/* free-box: a v14 free レイアウトボックス moves its children by its own origin
 * mode, and a user component hands its origin mode to its content's root
 * (static disassembly only):
 *   - the user component widget (vtable 0x80f0b4, type 17) writes its parts'
 *     origin mode (+0xa8) to its first child on every frame (+0x14 =
 *     0x4e57d0: 0x4e5780 takes children[0], 0x4e5815..0x4e581e write it); it
 *     does nothing else to its children, and it is never under the cursor
 *     (+0x4c = 0x49c5b0);
 *   - a free box (type 0, 0x5499e0) takes its size (GetSize(1), 0x54a9f0 ->
 *     0x54aaf0: the largest right and bottom edge of the children that are
 *     shown, 0x0 unless both are positive) and its own origin mode, and
 *     writes (0 | -W/2 | -W, 0 | -H/2 | -H) (jump tables 0x549ba4, 0x549bbc;
 *     0 for a mode outside 1..9) to +0x238/+0x23c of every child, shown or
 *     not (0x549b28, 0x549b37), on every frame the box is shown (0x549970);
 *   - a parts' position is read in two places only, the per-frame update
 *     (0x53564f, 0x535672) and the chain of transform records (0x537b00), and
 *     both add +0x238/+0x23c to it (0x53567a/0x535659, 0x537b2f/0x537b4f):
 *     the offset is in the parent's space like the position itself (scaled
 *     and mirrored by the box and its ancestors, not by the child), it moves
 *     the child's subtree with it, and the hit test (0x57a6b0) and
 *     Parts_GetPartsUpperLeftPos (0x534bb0) follow, as they use that matrix;
 *   - the size does not read the offset (0x54aaf0 adds the child's +0x94 /
 *     +0x98, its origin offset and its size), so there is no feedback;
 *   - the fields are 0 in a new parts (0x533ec8) and nothing else writes
 *     them for the components of the game (a vertical/horizontal box does
 *     not; the only other writer is the フォーム widget, 0x4da23f): a child
 *     that leaves the box keeps its last offset. The save does not hold them.
 * Every free box in the game's pactex has origin mode 1 (217 boxes, 194 of
 * them roots), but 30 user components have another one: SceneGarage's
 * GarageInfo (3, at (1258,19)) extended to the right of the screen instead
 * of to the left.
 *
 * The expected positions follow from these rules. xsystem4 marks the box
 * when something its size reads changes instead of laying it out on every
 * frame, so "after one update" checks that marking as well.
 *
 *   FB1 user components with origin mode 3, 5, 8, 9 and 1 and a content
 *       root (free, 201x41: the halves are truncated): the root takes the
 *       mode, its children are moved on screen, their own positions and the
 *       root's are not; origin mode 1 changes nothing.
 *   FB2 only the box's own children get the offset: a child's child and the
 *       children of a nested free box (origin mode 1) move with their parent
 *       and no further; the nested box's child grows and the outer box, two
 *       levels up, follows.
 *   FB3 a free box's own origin mode, all nine and two outside 1..9, set by
 *       a script and in the pactex; back to 1 the children are back.
 *   FB4 the offset follows the box's size after one update: a child grows,
 *       is hidden (and is still moved), shown, moved, gets another origin
 *       mode, is released; a child is attached and detached (and keeps its
 *       last offset); a box 0 high has size 0x0 and moves nothing.
 *   FB5 the shape of the garage's panel: the content root holds a horizontal
 *       box whose children are hidden and shown, so the size comes from a
 *       box below (362 wide with the third one hidden: x = 896).
 *   FB6 a user component in a vertical/horizontal box: with 配置 1 (all 7 in
 *       the game) nothing moves; with 配置 3 the box gives the component
 *       origin mode 3 and the content ends at the component's position.
 *   FB7 v13: a free box's origin mode moves nothing and a parts' origin mode
 *       stays its own, with ain->version set to 13 in the fork.
 *   FB8 the positions are the same after PE_Save / PE_Load, and the offset
 *       still follows the size; a save that holds the roots with origin mode
 *       1, as one written before this was implemented does (made here with
 *       the current writer, by setting the field back before saving), loads
 *       to the same, also for a top-level component, which no box lays out
 *       again after loading.
 *   FB9 the cursor: Parts_IsCursorIn and a click land where the child is
 *       drawn, not where it would be without the offset; the component and
 *       its root are not under the cursor.
 *   FB10 when the component passes its mode on: its own mode changes, a
 *       script changes the root's (put back), a second child (not touched),
 *       the first child leaves or is released (the next one takes the mode,
 *       the old root keeps it), a first child that is not a box (its own
 *       box moves, its children do not) and one that is a horizontal box
 *       (its start moves), and a parts that becomes a user component.
 *   FB11 the offset is scaled and mirrored with the child's position, in x
 *       and in y, by the component above the box and by the box's own scale;
 *       the hit box of a mirrored child.
 *   FB12 a size that changes below another layer: the content of a user
 *       component inside the box, and an inner box whose layout type a
 *       script changes (a vertical box counts its padding, a free one does
 *       not).
 * FB7, the origin mode 1 parts of FB1 and FB6 and the boxes of FB9 pass
 * before the fix: guards of what must not change. No GL is needed (矩形部件 sized by their corners).
 * Each case runs in a fork; only functions that exist before the fix are
 * used, so before-check builds the old tree.
 */
#define FB_CHECK LB_CHECK

static void fb_set_origin(int no, int mode) { BU_PE("SetComponentOriginPosMode", BU_I(no), BU_I(mode)); }
static void fb_attach(int no, int parent) { BU_PE("Parts_SetParentPartsNumber", BU_I(no), BU_I(parent)); }
static void fb_show(int no, int show) { BU_PE("SetComponentShow", BU_I(no), BU_I(show)); }
static void fb_resize(int no, int w, int h) { BU_PE("Parts_SetPartsRectangleDetectionSize", BU_I(no), BU_I(w), BU_I(h), BU_I(1)); }
static void fb_move(int no, int x, int y) { BU_PE("SetComponentPos", BU_I(no), BU_F(x), BU_F(y)); }

/* The anchor on screen, where the parts is drawn from (the point (0,0)
 * through the accumulated matrix; the v14 GetComponentAbsolutePos is not
 * bound, so this reads the engine's field). */
static void fb_expect_anchor(int no, int x, int y, const char *what)
{
	struct parts *p = no > 0 ? parts_try_get(no) : NULL;
	if (!p) {
		FB_CHECK(0, "%s: no parts", what);
		return;
	}
	FB_CHECK(p->global.pos.x == x && p->global.pos.y == y, "%s: drawn from (%d,%d), want (%d,%d)", what,
		 p->global.pos.x, p->global.pos.y, x, y);
}
/* The box's top-left corner on screen (Parts_GetPartsUpperLeftPos, 0x534bb0). */
static void fb_expect_corner(int no, int x, int y, const char *what)
{
	float ux = BU_PE("Parts_GetPartsUpperLeftPosX", BU_I(no), BU_I(1)).f;
	float uy = BU_PE("Parts_GetPartsUpperLeftPosY", BU_I(no), BU_I(1)).f;
	FB_CHECK(ux == x && uy == y, "%s: top-left corner at (%g,%g), want (%d,%d)", what, (double)ux, (double)uy, x, y);
}
/* A rect with origin mode 1: its corner is its anchor. */
static void fb_expect_rect(int no, int x, int y, const char *what)
{
	fb_expect_anchor(no, x, y, what);
	fb_expect_corner(no, x, y, what);
}
/* The parts' own position and origin mode, as a script reads them. */
static void fb_expect_own(int no, int x, int y, int om, const char *what)
{
	float px = BU_PE("GetComponentPosX", BU_I(no)).f, py = BU_PE("GetComponentPosY", BU_I(no)).f;
	int mode = BU_PE("GetComponentOriginPosMode", BU_I(no)).i;
	FB_CHECK(px == x && py == y && mode == om, "%s: own position (%g,%g) origin %d, want (%d,%d) origin %d", what,
		 (double)px, (double)py, mode, x, y, om);
}

/* A scene: a free root with n user components U0.. at pl[]. */
static void fb_scene(const char *act_name, int n, const struct lb_place *pl, int *uc)
{
	struct ex_tree *kids;
	struct ex *e = lb_activity(act_name, n, &kids);
	char name[8];
	for (int i = 0; i < n; i++) {
		snprintf(name, sizeof name, "U%d", i);
		kids->children[i] = lb_uc(name, &pl[i]);
	}
	int act = lb_load(act_name, e);
	for (int i = 0; i < n; i++) {
		snprintf(name, sizeof name, "U%d", i);
		uc[i] = lb_no(act, name);
	}
	heap_unref(act);
}

/* A content activity as the pactex ones are: a free root with origin mode 1
 * at (0,0), here with a w x h rect Base at (0,0) and a 30x10 rect Mark at
 * (150,20). out: root, Base, Mark. */
static void fb_panel(const char *act_name, int w, int h, int out[3])
{
	struct ex_tree *kids;
	struct ex *e = lb_activity(act_name, 2, &kids);
	struct lb_place p0 = { 0, 0, 1 }, pm = { 150, 20, 1 };
	kids->children[0] = lb_rect("Base", &p0, w, h, 0, 0);
	kids->children[1] = lb_rect("Mark", &pm, 30, 10, 0, 0);
	int act = lb_load(act_name, e);
	out[0] = lb_no(act, "");
	out[1] = lb_no(act, "Base");
	out[2] = lb_no(act, "Mark");
	heap_unref(act);
}

/* A user component at (x,y) with a panel as its content; what
 * <component>@Parent::set does with IActivity@Root. out: component, root,
 * Base, Mark. */
static void fb_component(const char *scene, const char *content, int x, int y, int om, int w, int h, int out[4])
{
	struct lb_place pl = { x, y, om };
	fb_scene(scene, 1, &pl, &out[0]);
	fb_panel(content, w, h, &out[1]);
	fb_attach(out[1], out[0]);
	PE_UpdateComponent(0);
}

/* --- FB1 --- */
static void fb_user_components(void)
{
	bu_postlink();
	/* The origin modes and positions of GarageInfo, ResultImage, a
	 * DungeonSelector and Staff2; W = max(201, 150 + 30), H = max(41, 30). */
	static const struct { int x, y, om, ox, oy; } u[5] = {
		{ 1258, 19, 3, -201, 0 }, { 210, 135, 5, -100, -20 }, { 876, 329, 8, -100, -41 },
		{ 1144, 508, 9, -201, -41 }, { 400, 600, 1, 0, 0 },
	};
	struct lb_place pl[5];
	int uc[5], c[5][3];
	for (int i = 0; i < 5; i++)
		pl[i] = (struct lb_place){ u[i].x, u[i].y, u[i].om };
	fb_scene("FreeBoxScene", 5, pl, uc);
	for (int i = 0; i < 5; i++) {
		char name[24];
		snprintf(name, sizeof name, "FreeBoxPanel%d", i);
		fb_panel(name, 201, 41, c[i]);
	}
	PE_UpdateComponent(0);
	/* Not attached yet: a top-level free root with origin mode 1. */
	fb_expect_own(c[0][0], 0, 0, 1, "root before it is attached");
	fb_expect_rect(c[0][1], 0, 0, "Base before its root is attached");
	fb_expect_rect(c[0][2], 150, 20, "Mark before its root is attached");
	for (int i = 0; i < 5; i++)
		fb_attach(c[i][0], uc[i]);
	PE_UpdateComponent(0);
	for (int i = 0; i < 5; i++) {
		char what[64];
		snprintf(what, sizeof what, "origin %d component", u[i].om);
		fb_expect_own(uc[i], u[i].x, u[i].y, u[i].om, what);
		fb_expect_anchor(uc[i], u[i].x, u[i].y, what);
		snprintf(what, sizeof what, "origin %d: content root", u[i].om);
		fb_expect_own(c[i][0], 0, 0, u[i].om, what);
		fb_expect_anchor(c[i][0], u[i].x, u[i].y, what);
		snprintf(what, sizeof what, "origin %d: Base", u[i].om);
		fb_expect_own(c[i][1], 0, 0, 1, what);
		fb_expect_rect(c[i][1], u[i].x + u[i].ox, u[i].y + u[i].oy, what);
		snprintf(what, sizeof what, "origin %d: Mark", u[i].om);
		fb_expect_own(c[i][2], 150, 20, 1, what);
		fb_expect_rect(c[i][2], u[i].x + 150 + u[i].ox, u[i].y + 20 + u[i].oy, what);
	}
	/* Laid out again (a size set and set back marks the root), the result
	 * is the same. */
	fb_resize(c[0][2], 31, 10);
	fb_resize(c[0][2], 30, 10);
	fb_resize(c[2][2], 31, 10);
	fb_resize(c[2][2], 30, 10);
	PE_UpdateComponent(0);
	fb_expect_rect(c[0][1], 1057, 19, "origin 3: Base laid out again");
	fb_expect_rect(c[2][2], 926, 308, "origin 8: Mark laid out again");
	printf("FB1 user components: %s\n", lb_failures ? "FAIL" : "ok");
}

/* --- FB2 --- */
static void fb_direct_children(void)
{
	bu_postlink();
	int uc;
	struct lb_place pu = { 500, 400, 9 };
	fb_scene("FreeBoxScene2", 1, &pu, &uc);
	/* Group 60x50 at (10,5) with Leaf 8x8 at (3,4); Inner, a free box at
	 * (100,0), with InnerLeaf 20x20 at (5,6): Inner is 25x26, the root
	 * max(70, 125) x max(55, 26) = 125x55. */
	struct ex_tree *kids;
	struct ex *e = lb_activity("FreeBoxNested", 2, &kids);
	struct lb_place pg = { 10, 5, 1 }, pleaf = { 3, 4, 1 }, pi = { 100, 0, 1 }, pil = { 5, 6, 1 };
	struct ex_tree ginfo = bu_branch(BU_INFO, 2);
	ginfo.children[0] = bu_sleaf(BU_TYPE, BU_LOW);
	ginfo.children[1] = lb_rect_state(BU_NORMAL, 60, 50);
	kids->children[0] = lb_node("Group", &pg, ginfo, 1);
	lb_kids(&kids->children[0])->children[0] = lb_rect("Leaf", &pleaf, 8, 8, 0, 0);
	kids->children[1] = lb_box("Inner", &pi, 0, 1, 1);
	lb_kids(&kids->children[1])->children[0] = lb_rect("InnerLeaf", &pil, 20, 20, 0, 0);
	int act = lb_load("FreeBoxNested", e);
	int root = lb_no(act, ""), group = lb_no(act, "Group"), leaf = lb_no(act, "Leaf");
	int inner = lb_no(act, "Inner"), inner_leaf = lb_no(act, "InnerLeaf");
	heap_unref(act);
	fb_attach(root, uc);
	PE_UpdateComponent(0);
	fb_expect_own(root, 0, 0, 9, "content root");
	fb_expect_rect(group, 500 + 10 - 125, 400 + 5 - 55, "Group");
	fb_expect_rect(leaf, 385 + 3, 350 + 4, "Group's child Leaf (moved once, with Group)");
	fb_expect_own(inner, 100, 0, 1, "nested free box");
	fb_expect_anchor(inner, 500 + 100 - 125, 400 - 55, "nested free box");
	fb_expect_rect(inner_leaf, 475 + 5, 345 + 6, "the nested box's child (no offset of its own)");
	/* InnerLeaf 20 -> 60 wide: Inner is 65x26 and the root, two levels up,
	 * max(70, 165) x 55. */
	fb_resize(inner_leaf, 60, 20);
	PE_UpdateComponent(0);
	fb_expect_rect(group, 500 + 10 - 165, 350, "Group after the nested box's child grows");
	fb_expect_rect(leaf, 345 + 3, 350 + 4, "Leaf after the nested box's child grows");
	fb_expect_anchor(inner, 500 + 100 - 165, 345, "nested free box after its child grows");
	fb_expect_rect(inner_leaf, 435 + 5, 345 + 6, "the nested box's child after it grows");
	printf("FB2 direct children only: %s\n", lb_failures ? "FAIL" : "ok");
}

/* --- FB3 --- */
static void fb_box_origin(void)
{
	bu_postlink();
	/* Box and Box9 at (300,200) / (700,500): A 101x51 at (0,0), B 10x10 at
	 * (20,70): 101x80. */
	struct ex_tree *kids;
	struct ex *e = lb_activity("FreeBoxOrigin", 2, &kids);
	struct lb_place pb = { 300, 200, 1 }, pb9 = { 700, 500, 9 }, pa = { 0, 0, 1 }, pbb = { 20, 70, 1 };
	kids->children[0] = lb_box("Box", &pb, 0, 1, 2);
	lb_kids(&kids->children[0])->children[0] = lb_rect("A", &pa, 101, 51, 0, 0);
	lb_kids(&kids->children[0])->children[1] = lb_rect("B", &pbb, 10, 10, 0, 0);
	kids->children[1] = lb_box("Box9", &pb9, 0, 1, 2);
	lb_kids(&kids->children[1])->children[0] = lb_rect("A9", &pa, 101, 51, 0, 0);
	lb_kids(&kids->children[1])->children[1] = lb_rect("B9", &pbb, 10, 10, 0, 0);
	int act = lb_load("FreeBoxOrigin", e);
	int box = lb_no(act, "Box"), a = lb_no(act, "A"), b = lb_no(act, "B");
	int box9 = lb_no(act, "Box9"), a9 = lb_no(act, "A9"), b9 = lb_no(act, "B9");
	heap_unref(act);
	PE_UpdateComponent(0);
	fb_expect_rect(a, 300, 200, "A with box origin 1");
	fb_expect_rect(b, 320, 270, "B with box origin 1");
	/* The origin mode in the pactex acts as well: (-101, -80). */
	fb_expect_own(box9, 700, 500, 9, "Box9");
	fb_expect_anchor(box9, 700, 500, "Box9");
	fb_expect_rect(a9, 599, 420, "A9 (box origin 9 from the pactex)");
	fb_expect_rect(b9, 619, 490, "B9 (box origin 9 from the pactex)");
	/* 0x549ba4 / 0x549bbc; a mode outside 1..9 gives 0 (0x549a2e, 0x549a5c). */
	static const struct { int om, ox, oy; } m[] = {
		{ 2, -50, 0 }, { 3, -101, 0 }, { 4, 0, -40 }, { 5, -50, -40 }, { 6, -101, -40 },
		{ 7, 0, -80 }, { 8, -50, -80 }, { 9, -101, -80 }, { 10, 0, 0 }, { 6, -101, -40 },
		{ 0, 0, 0 }, { 9, -101, -80 }, { 1, 0, 0 },
	};
	for (unsigned i = 0; i < sizeof(m) / sizeof(m[0]); i++) {
		char what[48];
		fb_set_origin(box, m[i].om);
		PE_UpdateComponent(0);
		snprintf(what, sizeof what, "A with box origin %d", m[i].om);
		fb_expect_rect(a, 300 + m[i].ox, 200 + m[i].oy, what);
		fb_expect_own(a, 0, 0, 1, what);
		snprintf(what, sizeof what, "B with box origin %d", m[i].om);
		fb_expect_rect(b, 320 + m[i].ox, 270 + m[i].oy, what);
		fb_expect_anchor(box, 300, 200, "Box itself");
	}
	printf("FB3 a free box's own origin mode: %s\n", lb_failures ? "FAIL" : "ok");
}

/* --- FB4 --- */
static void fb_size_changes(void)
{
	bu_postlink();
	int n[4];
	fb_component("FreeBoxScene4", "FreeBoxPanel4", 1258, 19, 3, 201, 41, n);
	int root = n[1], base = n[2], mark = n[3];
	fb_expect_rect(base, 1057, 19, "Base");
	/* Mark 30 -> 100 wide: its right edge 250. */
	fb_resize(mark, 100, 10);
	PE_UpdateComponent(0);
	fb_expect_rect(base, 1008, 19, "Base after Mark grows");
	fb_expect_rect(mark, 1158, 39, "Mark after it grows");
	/* Hidden, Mark does not count (W = 201) but is moved like the others. */
	fb_show(mark, 0);
	PE_UpdateComponent(0);
	fb_expect_rect(base, 1057, 19, "Base with Mark hidden");
	fb_expect_anchor(mark, 1207, 39, "hidden Mark");
	fb_show(mark, 1);
	PE_UpdateComponent(0);
	fb_expect_rect(base, 1008, 19, "Base with Mark shown again");
	/* Mark at (300,20): W = 400. */
	fb_move(mark, 300, 20);
	PE_UpdateComponent(0);
	fb_expect_rect(base, 858, 19, "Base after Mark moves");
	fb_expect_rect(mark, 1158, 39, "Mark after it moves");
	/* Mark with origin mode 3 ends at its position: W = 300. */
	fb_set_origin(mark, 3);
	PE_UpdateComponent(0);
	fb_expect_rect(base, 958, 19, "Base after Mark gets origin 3");
	fb_expect_anchor(mark, 1258, 39, "Mark with origin 3");
	fb_expect_corner(mark, 1158, 39, "Mark with origin 3");
	/* Base 0 high and Mark hidden: 201x0 is 0x0 (0x54aab1), no offset in x
	 * either. */
	fb_show(mark, 0);
	fb_resize(base, 201, 0);
	PE_UpdateComponent(0);
	fb_expect_anchor(base, 1258, 19, "Base 0 high (box size 0x0)");
	fb_expect_anchor(mark, 1558, 39, "hidden Mark of a 0x0 box");
	fb_resize(base, 201, 41);
	fb_show(mark, 1);
	PE_UpdateComponent(0);
	fb_expect_rect(base, 958, 19, "Base restored");
	BU_PE("Release", BU_I(mark));
	PE_UpdateComponent(0);
	fb_expect_rect(base, 1057, 19, "Base after Mark is released");
	/* A rect made by a script joins the root at (400,0): W = 450. */
	const int N = 9911;
	parts_get(N);
	fb_resize(N, 50, 10);
	fb_move(N, 400, 0);
	fb_attach(N, root);
	PE_UpdateComponent(0);
	fb_expect_rect(base, 808, 19, "Base after a rect joins");
	fb_expect_rect(N, 1208, 19, "the rect that joined");
	/* It leaves: the box is 201 wide again, and nothing resets the rect's
	 * +0x238 (only the box it left wrote it): top-level at (401,7) - 450.
	 * (It is moved because xsystem4 places a detached parts again only
	 * when its position is set.) */
	fb_attach(N, 0);
	fb_move(N, 401, 7);
	PE_UpdateComponent(0);
	fb_expect_rect(base, 1057, 19, "Base after the rect leaves");
	fb_expect_own(N, 401, 7, 1, "the rect that left");
	fb_expect_rect(N, -49, 7, "the rect that left (keeps its last offset)");
	printf("FB4 the offset follows the size: %s\n", lb_failures ? "FAIL" : "ok");
}

/* The garage panel's shape: a free root with Base, a horizontal 配置 1 box
 * at (0,0), holding A 83x45, B 279x45 and C 300x45. out: root, Base, A, B, C. */
static void fb_garage_panel(const char *act_name, int out[5])
{
	struct ex_tree *kids;
	struct ex *e = lb_activity(act_name, 1, &kids);
	struct lb_place p0 = { 0, 0, 1 }, pa = { 0, 0, 1 }, pb = { 83, 0, 1 }, pc = { 362, 0, 1 };
	kids->children[0] = lb_box("Base", &p0, 2, 1, 3);
	struct ex_tree *k = lb_kids(&kids->children[0]);
	k->children[0] = lb_rect("A", &pa, 83, 45, 0, 0);
	k->children[1] = lb_rect("B", &pb, 279, 45, 0, 0);
	k->children[2] = lb_rect("C", &pc, 300, 45, 0, 0);
	int act = lb_load(act_name, e);
	const char *names[5] = { "", "Base", "A", "B", "C" };
	for (int i = 0; i < 5; i++)
		out[i] = lb_no(act, names[i]);
	heap_unref(act);
}

/* --- FB5 --- */
static void fb_garage(void)
{
	bu_postlink();
	int uc, n[5];
	struct lb_place pu = { 1258, 19, 3 };
	fb_scene("FreeBoxScene5", 1, &pu, &uc);
	fb_garage_panel("FreeBoxGarage", n);
	fb_attach(n[0], uc);
	PE_UpdateComponent(0);
	/* 83 + 279 + 300 = 662. */
	fb_expect_anchor(n[1], 596, 19, "Base with A, B and C");
	fb_expect_rect(n[2], 596, 19, "A");
	fb_expect_rect(n[3], 679, 19, "B");
	fb_expect_rect(n[4], 958, 19, "C");
	/* What GarageInformation@SetMode does: C hidden, 362 wide. */
	fb_show(n[4], 0);
	PE_UpdateComponent(0);
	fb_expect_anchor(n[1], 896, 19, "Base with C hidden");
	fb_expect_rect(n[2], 896, 19, "A with C hidden");
	fb_expect_rect(n[3], 979, 19, "B with C hidden");
	lb_expect_size(n[1], 362, 45, "Base with C hidden");
	/* A hidden as well: B alone, 279. */
	fb_show(n[2], 0);
	PE_UpdateComponent(0);
	fb_expect_anchor(n[1], 979, 19, "Base with only B");
	fb_expect_rect(n[3], 979, 19, "B alone");
	fb_show(n[2], 1);
	fb_show(n[4], 1);
	PE_UpdateComponent(0);
	fb_expect_rect(n[3], 679, 19, "B with all three again");
	/* B 279 -> 300: 683. */
	fb_resize(n[3], 300, 45);
	PE_UpdateComponent(0);
	fb_expect_anchor(n[1], 575, 19, "Base after B grows");
	fb_expect_rect(n[4], 958, 19, "C after B grows");
	printf("FB5 a box below sets the size: %s\n", lb_failures ? "FAIL" : "ok");
}

/* --- FB6 --- */
static void fb_in_boxes(void)
{
	bu_postlink();
	struct ex_tree *kids;
	struct ex *e = lb_activity("FreeBoxInBoxes", 2, &kids);
	struct lb_place pv = { 100, 50, 1 }, ph = { 500, 100, 1 }, px = { 33, 44, 1 };
	/* VBox: vertical, 配置 1 (the shape of all 7 in the game). */
	kids->children[0] = lb_box("VBox", &pv, 1, 1, 2);
	lb_kids(&kids->children[0])->children[0] = lb_uc("UV", &px);
	lb_kids(&kids->children[0])->children[1] = lb_rect("R", &px, 4, 4, 0, 0);
	/* HBox: horizontal, 配置 3. */
	kids->children[1] = lb_box("HBox", &ph, 2, 3, 1);
	lb_kids(&kids->children[1])->children[0] = lb_uc("UH", &px);
	int act = lb_load("FreeBoxInBoxes", e);
	int uv = lb_no(act, "UV"), r = lb_no(act, "R"), uh = lb_no(act, "UH");
	heap_unref(act);
	/* UV's content: a 50x20 rect at (5,5), 55x25. UH's: one at (0,0), 50x20. */
	struct lb_place pc5 = { 5, 5, 1 }, pc0 = { 0, 0, 1 };
	e = lb_activity("FreeBoxContentV", 1, &kids);
	kids->children[0] = lb_rect("Content", &pc5, 50, 20, 0, 0);
	act = lb_load("FreeBoxContentV", e);
	int rootv = lb_no(act, ""), cv = lb_no(act, "Content");
	heap_unref(act);
	e = lb_activity("FreeBoxContentH", 1, &kids);
	kids->children[0] = lb_rect("Content", &pc0, 50, 20, 0, 0);
	act = lb_load("FreeBoxContentH", e);
	int rooth = lb_no(act, ""), ch = lb_no(act, "Content");
	heap_unref(act);
	fb_attach(rootv, uv);
	fb_attach(rooth, uh);
	PE_UpdateComponent(0);
	/* 配置 1: the component keeps origin mode 1, and so does its root. */
	fb_expect_own(uv, 0, 0, 1, "UV in the vertical box");
	fb_expect_own(rootv, 0, 0, 1, "UV's content root");
	fb_expect_rect(cv, 105, 55, "UV's content");
	fb_expect_rect(r, 100, 75, "the rect below UV");
	/* 配置 3: the box is 50 wide and starts at its right edge; the component
	 * gets origin mode 3 from the box (0x549fa0), the root from it. */
	fb_expect_own(uh, 50, 0, 3, "UH in the 配置 3 box");
	fb_expect_anchor(uh, 550, 100, "UH in the 配置 3 box");
	fb_expect_own(rooth, 0, 0, 3, "UH's content root");
	fb_expect_rect(ch, 500, 100, "UH's content (ends at the component)");
	printf("FB6 user components in boxes: %s\n", lb_failures ? "FAIL" : "ok");
}

/* --- FB7 --- */
static void fb_v13(void)
{
	bu_postlink();
	ain->version = 13;
	const int B = 9921, A = 9922, P = 9923, C = 9924;
	parts_get(B); parts_get(A); parts_get(P); parts_get(C);
	PE_SetLayoutBoxLayoutType(B, 0);
	PE_SetPos(B, 300, 200);
	PE_SetParentPartsNumber(A, B);
	PE_SetPartsRectangleDetectionSize(A, 100, 40, 1);
	PE_SetPos(A, 10, 10);
	PE_SetPartsOriginPosMode(B, 9);
	/* A parts marked as a user component does not pass its mode on either. */
	parts_get(P)->component_type = 17;
	PE_SetPos(P, 600, 300);
	PE_SetPartsOriginPosMode(P, 9);
	PE_SetParentPartsNumber(C, P);
	PE_SetPartsRectangleDetectionSize(C, 100, 40, 1);
	PE_UpdateComponent(0);
	PE_UpdateComponent(0);
	struct parts *a = parts_get(A), *c = parts_get(C);
	FB_CHECK(a->global.pos.x == 310 && a->global.pos.y == 210 && a->origin_mode == 1,
		 "v13 free box child drawn at (%d,%d) origin %d, want (310,210) origin 1",
		 a->global.pos.x, a->global.pos.y, a->origin_mode);
	FB_CHECK(c->global.pos.x == 600 && c->global.pos.y == 300 && c->origin_mode == 1,
		 "v13 child of a type 17 parts drawn at (%d,%d) origin %d, want (600,300) origin 1",
		 c->global.pos.x, c->global.pos.y, c->origin_mode);
	ain->version = 14;
	printf("FB7 v13 unchanged: %s\n", lb_failures ? "FAIL" : "ok");
}

/* --- FB8 --- */
static void fb_save_load(void)
{
	bu_postlink();
	int uc[2], g[5], c[3];
	struct lb_place pl[2] = { { 1258, 19, 3 }, { 876, 329, 8 } };
	fb_scene("FreeBoxScene8", 2, pl, uc);
	fb_garage_panel("FreeBoxGarage8", g);
	fb_panel("FreeBoxPanel8", 201, 41, c);
	fb_attach(g[0], uc[0]);
	fb_attach(c[0], uc[1]);
	fb_show(g[4], 0);
	/* P, a top-level user component with origin mode 6, and Q 40x20. */
	const int P = 9951, Q = 9952;
	parts_get(P); parts_get(Q);
	fb_move(P, 100, 100);
	fb_set_origin(P, 6);
	BU_PE("SetComponentType", BU_I(P), BU_I(17), BU_I(1));
	fb_resize(Q, 40, 20);
	fb_attach(Q, P);
	PE_UpdateComponent(0);
	fb_expect_rect(g[2], 896, 19, "A before saving");
	fb_expect_rect(c[1], 776, 288, "Base before saving");
	fb_expect_corner(Q, 60, 90, "Q before saving");
	struct page *saved = NULL;
	FB_CHECK(PE_Save(&saved), "PE_Save failed");
	if (!saved)
		return;
	FB_CHECK(PE_Load(&saved), "PE_Load failed");
	PE_UpdateComponent(0);
	fb_expect_own(g[0], 0, 0, 3, "the garage root after loading");
	fb_expect_anchor(g[1], 896, 19, "the horizontal box after loading");
	fb_expect_rect(g[2], 896, 19, "A after loading");
	fb_expect_rect(g[3], 979, 19, "B after loading");
	fb_expect_own(c[0], 0, 0, 8, "the panel root after loading");
	fb_expect_rect(c[1], 776, 288, "Base after loading");
	fb_expect_rect(c[2], 926, 308, "Mark after loading");
	fb_expect_own(c[2], 150, 20, 1, "Mark after loading");
	/* Still laid out when the size changes: B 300 wide, 383; Mark hidden,
	 * 201x41. */
	fb_resize(g[3], 300, 45);
	fb_show(c[2], 0);
	fb_resize(c[1], 301, 41);
	PE_UpdateComponent(0);
	fb_expect_rect(g[2], 875, 19, "A after loading and B growing");
	fb_expect_rect(c[1], 726, 288, "Base after loading and growing");
	ca_free_buffer(saved);
	/* A save from before the components passed their mode on holds the
	 * roots with their own origin mode 1 (made here by setting the field
	 * back; the rest of the record is the current writer's): loaded, they
	 * take it again, also under P, which has no box above it that lays out
	 * after loading. P's case depends on the order: parts are saved and
	 * updated in drawing order, P before Q, so P is already updated when Q
	 * is attached to it; in the other order P would still be marked. */
	parts_get(g[0])->origin_mode = 1;
	parts_get(c[0])->origin_mode = 1;
	parts_get(Q)->origin_mode = 1;
	saved = NULL;
	FB_CHECK(PE_Save(&saved), "PE_Save failed");
	if (!saved)
		return;
	FB_CHECK(PE_Load(&saved), "PE_Load failed");
	PE_UpdateComponent(0);
	fb_expect_own(g[0], 0, 0, 3, "the garage root loaded with origin 1");
	fb_expect_rect(g[2], 875, 19, "A loaded under a root with origin 1");
	fb_expect_own(c[0], 0, 0, 8, "the panel root loaded with origin 1");
	fb_expect_rect(c[1], 726, 288, "Base loaded under a root with origin 1");
	fb_expect_own(Q, 0, 0, 6, "Q loaded with origin 1");
	fb_expect_corner(Q, 60, 90, "Q loaded with origin 1");
	ca_free_buffer(saved);
	printf("FB8 save and load: %s\n", lb_failures ? "FAIL" : "ok");
}

/* --- FB9 --- */
static void fb_cursor(void)
{
	br_input_setup();
	int n[4];
	fb_component("FreeBoxScene9", "FreeBoxPanel9", 1258, 19, 3, 201, 41, n);
	int base = n[2];
	PE_SetClickable(base, true);
	PE_UpdateComponent(0);
	/* Base covers (1057,19)..(1258,60); without the offset it would start
	 * at x = 1258. */
	static const struct { int x, y, want; } at[] = {
		{ 1057, 19, 1 }, { 1257, 59, 1 }, { 1056, 30, 0 }, { 1258, 30, 0 }, { 1270, 30, 0 }, { 1100, 60, 0 },
	};
	for (unsigned i = 0; i < sizeof(at) / sizeof(at[0]); i++) {
		int in = BU_PE("Parts_IsCursorIn", BU_I(base), BU_I(at[i].x), BU_I(at[i].y), BU_I(1)).i;
		FB_CHECK(!!in == at[i].want, "Parts_IsCursorIn(Base, %d, %d) = %d, want %d", at[i].x, at[i].y, in, at[i].want);
	}
	/* Neither the component nor its root is under the cursor (0x49c5b0). */
	int in_uc = BU_PE("Parts_IsCursorIn", BU_I(n[0]), BU_I(1100), BU_I(40), BU_I(1)).i;
	int in_root = BU_PE("Parts_IsCursorIn", BU_I(n[1]), BU_I(1100), BU_I(40), BU_I(1)).i;
	FB_CHECK(!in_uc && !in_root, "Parts_IsCursorIn at (1100,40): component %d, root %d; want 0, 0", in_uc, in_root);
	int type, no;
	int count = br_click(1100, 40, &type, &no);
	FB_CHECK(count == 1 && type == 4 && no == base, "click at (1100,40): %d messages, first type %d parts %d; want 1, 4, %d",
		 count, type, no, base);
	heap[global_page_slot].page->values[2].i = 0;
	count = br_click(1270, 40, &type, &no);
	FB_CHECK(!(type == 4 && no == base), "click at (1270,40) reached Base (%d messages)", count);
	printf("FB9 the cursor follows: %s\n", lb_failures ? "FAIL" : "ok");
}

/* --- FB10 --- */
static void fb_passing(void)
{
	bu_postlink();
	int n[4];
	fb_component("FreeBoxScene10", "FreeBoxPanel10", 600, 300, 3, 80, 30, n);
	int uc = n[0], root = n[1], base = n[2];
	/* Base 80x30 and Mark, 30x10 at (150,20): 180x30. */
	fb_expect_own(root, 0, 0, 3, "root");
	fb_expect_rect(base, 420, 300, "Base");
	fb_set_origin(uc, 9);
	PE_UpdateComponent(0);
	fb_expect_own(root, 0, 0, 9, "root after the component gets origin 9");
	fb_expect_rect(base, 420, 270, "Base after the component gets origin 9");
	/* A script sets the root's mode: the component writes its own again. */
	fb_set_origin(root, 1);
	PE_UpdateComponent(0);
	fb_expect_own(root, 0, 0, 9, "root after SetComponentOriginPosMode(root, 1)");
	fb_expect_rect(base, 420, 270, "Base after SetComponentOriginPosMode(root, 1)");
	/* A second child, S 40x40 at (7,8) with T 5x5 at (1,2): not the first,
	 * so it keeps its mode; a user component is no box, so no offset. */
	const int S = 9931, T = 9932;
	parts_get(S); parts_get(T);
	fb_resize(S, 40, 40);
	fb_move(S, 7, 8);
	fb_resize(T, 5, 5);
	fb_move(T, 1, 2);
	fb_attach(T, S);
	fb_attach(S, uc);
	PE_UpdateComponent(0);
	fb_expect_own(S, 7, 8, 1, "the component's second child");
	fb_expect_rect(S, 607, 308, "the component's second child");
	fb_expect_rect(T, 608, 310, "its child T");
	/* The root leaves: S is the first child now and takes origin mode 9,
	 * which moves its own box and not its child. The root keeps the mode it
	 * was given and, top-level at (0,0), still moves its children. (Its
	 * position is set for xsystem4 to place the detached parts again.) */
	fb_attach(root, 0);
	fb_move(root, 0, 0);
	PE_UpdateComponent(0);
	fb_expect_own(S, 7, 8, 9, "S as the first child");
	fb_expect_anchor(S, 607, 308, "S as the first child");
	fb_expect_corner(S, 567, 268, "S as the first child");
	fb_expect_rect(T, 608, 310, "T under S with origin 9");
	fb_expect_own(root, 0, 0, 9, "the root that left");
	fb_expect_rect(base, -180, -30, "Base of the root that left");
	/* The first child is released: the next one, S2 10x10, takes the mode. */
	const int S2 = 9933;
	parts_get(S2);
	fb_resize(S2, 10, 10);
	fb_attach(S2, uc);
	PE_UpdateComponent(0);
	fb_expect_own(S2, 0, 0, 1, "S2 behind S");
	BU_PE("Release", BU_I(S));
	PE_UpdateComponent(0);
	fb_expect_own(S2, 0, 0, 9, "S2 after S is released");
	fb_expect_corner(S2, 590, 290, "S2 after S is released");
	/* Without a child the component has nothing to do. */
	BU_PE("Release", BU_I(S2));
	fb_set_origin(uc, 5);
	PE_UpdateComponent(0);
	fb_expect_own(uc, 600, 300, 5, "the component without a child");
	/* A first child that is a horizontal 配置 1 box (X 40x10, Y 20x10): its
	 * own origin mode moves its start, 60 to the left and 10 up. */
	struct ex_tree *kids;
	struct ex *e = lb_activity("FreeBoxRow", 1, &kids);
	struct lb_place p0 = { 0, 0, 1 }, px = { 33, 44, 1 };
	kids->children[0] = lb_box("Row", &p0, 2, 1, 2);
	lb_kids(&kids->children[0])->children[0] = lb_rect("X", &px, 40, 10, 0, 0);
	lb_kids(&kids->children[0])->children[1] = lb_rect("Y", &px, 20, 10, 0, 0);
	int act = lb_load("FreeBoxRow", e);
	int row = lb_no(act, "Row"), x = lb_no(act, "X"), y = lb_no(act, "Y");
	heap_unref(act);
	fb_set_origin(uc, 9);
	fb_attach(row, uc);
	PE_UpdateComponent(0);
	fb_expect_own(row, 0, 0, 9, "the horizontal box as the first child");
	fb_expect_rect(x, 540, 290, "X in the horizontal box");
	fb_expect_rect(y, 580, 290, "Y in the horizontal box");
	/* A parts with a child becomes a user component (SetComponentType 17,
	 * as SetUserComponentData does through 0x5372c0): from then on Q, 40x20,
	 * takes P's origin mode 6. */
	const int P = 9941, Q = 9942;
	parts_get(P); parts_get(Q);
	fb_move(P, 100, 100);
	fb_set_origin(P, 6);
	fb_resize(Q, 40, 20);
	fb_attach(Q, P);
	PE_UpdateComponent(0);
	fb_expect_own(Q, 0, 0, 1, "Q under a parts that is no user component");
	BU_PE("SetComponentType", BU_I(P), BU_I(17), BU_I(1));
	PE_UpdateComponent(0);
	fb_expect_own(Q, 0, 0, 6, "Q once P is a user component");
	fb_expect_corner(Q, 60, 90, "Q once P is a user component");
	printf("FB10 when the mode is passed on: %s\n", lb_failures ? "FAIL" : "ok");
}

/* --- FB11 --- */
static void fb_transformed(void)
{
	bu_postlink();
	int n[4];
	fb_component("FreeBoxScene11", "FreeBoxPanel11", 600, 300, 3, 200, 40, n);
	int uc = n[0], root = n[1], base = n[2], mark = n[3];
	fb_expect_rect(base, 400, 300, "Base");
	fb_expect_rect(mark, 550, 320, "Mark");
	/* The component at half size: 600 + (0 - 200) / 2, 600 + (150 - 200) / 2;
	 * the box is still 200 wide. */
	BU_PE("SetComponentMagX", BU_I(uc), BU_F(0.5f));
	BU_PE("SetComponentMagY", BU_I(uc), BU_F(0.5f));
	PE_UpdateComponent(0);
	fb_expect_anchor(root, 600, 300, "root under a half-size component");
	fb_expect_anchor(base, 500, 300, "Base under a half-size component");
	fb_expect_anchor(mark, 575, 310, "Mark under a half-size component");
	BU_PE("SetComponentMagX", BU_I(uc), BU_F(1.0f));
	BU_PE("SetComponentMagY", BU_I(uc), BU_F(1.0f));
	/* Mirrored: 600 - (0 - 200), 600 - (150 - 200). */
	BU_PE("SetComponentReverseLR", BU_I(uc), BU_I(1));
	PE_UpdateComponent(0);
	fb_expect_anchor(base, 800, 300, "Base under a mirrored component");
	fb_expect_anchor(mark, 650, 320, "Mark under a mirrored component");
	/* Mirrored, Base covers (600,300)..(800,340), left of its anchor. */
	int in = BU_PE("Parts_IsCursorIn", BU_I(base), BU_I(700), BU_I(310), BU_I(1)).i;
	int out = BU_PE("Parts_IsCursorIn", BU_I(base), BU_I(850), BU_I(310), BU_I(1)).i;
	FB_CHECK(in && !out, "mirrored Base under the cursor at (700,310) %d, (850,310) %d; want 1, 0", in, out);
	BU_PE("SetComponentReverseLR", BU_I(uc), BU_I(0));
	PE_UpdateComponent(0);
	fb_expect_rect(base, 400, 300, "Base restored");
	/* In y as well: origin mode 9, (-200,-40). */
	fb_set_origin(uc, 9);
	PE_UpdateComponent(0);
	fb_expect_rect(base, 400, 260, "Base with origin 9");
	fb_expect_rect(mark, 550, 280, "Mark with origin 9");
	BU_PE("SetComponentMagX", BU_I(uc), BU_F(0.5f));
	BU_PE("SetComponentMagY", BU_I(uc), BU_F(0.5f));
	PE_UpdateComponent(0);
	fb_expect_anchor(base, 500, 280, "Base (origin 9) under a half-size component");
	fb_expect_anchor(mark, 575, 290, "Mark (origin 9) under a half-size component");
	BU_PE("SetComponentMagX", BU_I(uc), BU_F(1.0f));
	BU_PE("SetComponentMagY", BU_I(uc), BU_F(1.0f));
	/* Upside down: 300 - (0 - 40), 300 - (20 - 40). */
	BU_PE("SetComponentReverseTB", BU_I(uc), BU_I(1));
	PE_UpdateComponent(0);
	fb_expect_anchor(base, 400, 340, "Base under a component turned upside down");
	fb_expect_anchor(mark, 550, 320, "Mark under a component turned upside down");
	BU_PE("SetComponentReverseTB", BU_I(uc), BU_I(0));
	/* The box's own scale acts on where its children are placed, like any
	 * parent's: 600 + 2 * (0 - 200), 300 + 2 * (0 - 40); its size is still
	 * 200x40. */
	BU_PE("SetComponentMagX", BU_I(root), BU_F(2.0f));
	BU_PE("SetComponentMagY", BU_I(root), BU_F(2.0f));
	PE_UpdateComponent(0);
	fb_expect_anchor(root, 600, 300, "root at twice its size");
	fb_expect_anchor(base, 200, 220, "Base in a box at twice its size");
	fb_expect_anchor(mark, 500, 260, "Mark in a box at twice its size");
	BU_PE("SetComponentMagX", BU_I(root), BU_F(1.0f));
	BU_PE("SetComponentMagY", BU_I(root), BU_F(1.0f));
	PE_UpdateComponent(0);
	fb_expect_rect(base, 400, 260, "Base restored with origin 9");
	printf("FB11 scaled and mirrored: %s\n", lb_failures ? "FAIL" : "ok");
}

/* --- FB12 --- */
static void fb_through_layers(void)
{
	bu_postlink();
	int uc;
	struct lb_place pu = { 500, 400, 9 };
	fb_scene("FreeBoxScene12", 1, &pu, &uc);
	/* The root: Fix 40x30 at (0,0); InUc, a user component at (60,10); VIn,
	 * a vertical 配置 1 box at (0,50) with padding (5,6,7,8) and a 10x5,
	 * b 6x8: (10 + 7 + 8) x (13 + 5 + 6) = 25x24. */
	struct ex_tree *kids;
	struct ex *e = lb_activity("FreeBoxLayers", 3, &kids);
	struct lb_place p0 = { 0, 0, 1 }, pi = { 60, 10, 1 }, pv = { 0, 50, 1 }, px = { 33, 44, 1 };
	kids->children[0] = lb_rect("Fix", &p0, 40, 30, 0, 0);
	kids->children[1] = lb_uc("InUc", &pi);
	kids->children[2] = lb_node("VIn", &pv, lb_box_info(false, 1, 0, 200.0f, false, 1, 5, 6, 7, 8), 2);
	lb_kids(&kids->children[2])->children[0] = lb_rect("a", &px, 10, 5, 0, 0);
	lb_kids(&kids->children[2])->children[1] = lb_rect("b", &px, 6, 8, 0, 0);
	int act = lb_load("FreeBoxLayers", e);
	int root = lb_no(act, ""), fix = lb_no(act, "Fix"), in_uc = lb_no(act, "InUc"), vin = lb_no(act, "VIn");
	int a = lb_no(act, "a");
	heap_unref(act);
	/* InUc's content: K 50x20 at (0,0). */
	e = lb_activity("FreeBoxLayersContent", 1, &kids);
	kids->children[0] = lb_rect("K", &p0, 50, 20, 0, 0);
	act = lb_load("FreeBoxLayersContent", e);
	int kroot = lb_no(act, ""), k = lb_no(act, "K");
	heap_unref(act);
	fb_attach(kroot, in_uc);
	fb_attach(root, uc);
	PE_UpdateComponent(0);
	/* max(40, 60 + 50, 25) x max(30, 10 + 20, 50 + 24) = 110x74. */
	fb_expect_rect(fix, 390, 326, "Fix");
	fb_expect_rect(k, 450, 336, "K in the inner component");
	fb_expect_anchor(vin, 390, 376, "VIn");
	fb_expect_rect(a, 397, 381, "a in VIn");
	/* K 50 -> 150 wide: the inner component is 150x20, the root 210x74. */
	fb_resize(k, 150, 20);
	PE_UpdateComponent(0);
	fb_expect_rect(fix, 290, 326, "Fix after the inner component's content grows");
	fb_expect_rect(k, 350, 336, "K after it grows");
	/* A script makes VIn a free box: its children stay at (7,5) and (7,10)
	 * and it is max(17, 13) x max(10, 18) = 17x18, the root 210 x 68. */
	BU_PE("SetLayoutBoxLayoutType", BU_I(vin), BU_I(0));
	PE_UpdateComponent(0);
	fb_expect_rect(fix, 290, 332, "Fix after VIn becomes a free box");
	fb_expect_anchor(vin, 290, 382, "VIn as a free box");
	fb_expect_rect(a, 297, 387, "a in VIn as a free box");
	/* And vertical again: 25x24, the root 210x74. */
	BU_PE("SetLayoutBoxLayoutType", BU_I(vin), BU_I(1));
	PE_UpdateComponent(0);
	fb_expect_rect(fix, 290, 326, "Fix after VIn is vertical again");
	fb_expect_rect(a, 297, 381, "a after VIn is vertical again");
	printf("FB12 sizes through other layers: %s\n", lb_failures ? "FAIL" : "ok");
}

static int test_free_box(void)
{
	void (*cases[])(void) = { fb_user_components, fb_direct_children, fb_box_origin, fb_size_changes,
		fb_garage, fb_in_boxes, fb_v13, fb_save_load, fb_cursor, fb_passing, fb_transformed,
		fb_through_layers };
	const char *names[] = { "FB1", "FB2", "FB3", "FB4", "FB5", "FB6", "FB7", "FB8", "FB9", "FB10", "FB11",
		"FB12" };
	const int n = sizeof(cases) / sizeof(cases[0]);
	int failed = 0;
	for (int i = 0; i < n; i++)
		failed += lb_fork(names[i], cases[i]);
	printf("FREE-BOX %s: %d of %d cases failed\n", failed ? "FAIL" : "PASS", failed, n);
	return failed ? 1 : 0;
}
