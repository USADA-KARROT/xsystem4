/* reverse-inherit: SetComponentReverseLR/TB act on the whole parts subtree,
 * the way the original composes them.
 *
 * Original (static disassembly only): the per-parts update 0x535260 copies the
 * parent's accumulated sprite parameters (0x579460) and applies the parts' own
 * position, scale, rotation, origin and reverse flags with 0x579bb0 ->
 * 0x4e6d80. 0x4e6d80 XORs the flags into the accumulated state (+0x49 LR,
 * +0x48 TB) and multiplies the accumulated matrix by
 *     S(reverse ? -1 : 1) * T(-origin) * S(scale) * R * T(pos)
 * (row vectors, applied left to right; 0x72e730 is A*B), so a flip mirrors
 * the parts' own box around its anchor (its position) and a parent's flip
 * mirrors the children's positions and drawings around the parent's anchor.
 * The box starts at the origin-mode offset (0x5b71b0 / 0x5b7200: 0, -w/2, -w).
 * FLAT parts collect the same records along the parent chain (0x537b00,
 * called from 0x525e83 and 0x51ad4f) and apply them with the same 0x4e6d80.
 * The hit test 0x57a6b0 transforms the box corners with the same matrix, and
 * Parts_GetPartsUpperLeftPos (0x534bb0) is the box's top-left corner through it.
 *
 * Before the fix only a CG-like parts flipped itself, within its own box;
 * TEXT, FLAT, rect detection and every child ignored the flags.
 *
 * Cases (each in its own fork):
 *   RI1 real AIN declarations (hll_call): a parent's LR/TB flip mirrors the
 *       children's global positions around the parent's anchor, flips
 *       combine by XOR down the chain (a grandchild of a flipped child of a
 *       flipped parent is not mirrored against its parent), the box used by
 *       Parts_IsCursorIn and Parts_GetPartsUpperLeftPosX follow, the getters
 *       keep returning the parts' own flag, and unflipping restores every
 *       value. Uses no function added by the fix.
 *   RI2 the new transform functions (dlsym): parts_box_transform maps the
 *       parts' box to the screen as anchor + Dparent*R*S*Downn*(p + origin)
 *       for every flip combination with rotation and scale, the TEXT path
 *       (parts_anchor_transform without rotation/scale) and the FLAT path
 *       follow the same flips, parts_screen_to_box (the pixel hit test's
 *       inverse) returns the same box point, and a surface-area CG stays at
 *       its anchor when flipped (side review D6).
 *   RI3 AdvStand through real bytecode: NEW AdvStand, AdvStand@Move across
 *       sides and Motion::EndAll set ReverseLR on the stand's rect root
 *       (AdvStandImage.m_parent) through Motion::Executer@SetPartsValue ->
 *       IParts vt135 -> PartsEngine.SetComponentReverseLR; the stand image
 *       (a child of that rect) must be drawn mirrored around its anchor.
 */
#include <dlfcn.h>
#include <math.h>
#include <sys/wait.h>
#include <unistd.h>
#include "parts_internal.h"
extern void probe_init_parts_table(void);

static int ri_failures;
#define RI_CHECK(c, ...) do { if (!(c)) { printf("  FAIL: "); printf(__VA_ARGS__); printf("\n"); ri_failures++; } } while (0)

typedef void (*ri_box_fn)(struct parts *, struct parts_common *, mat4);
typedef void (*ri_anchor_fn)(struct parts *, float, bool, mat4);
typedef bool (*ri_inv_fn)(struct parts *, struct parts_common *, float, float, float *, float *);

static void ri_postlink(void)
{
	probe_init_parts_table();
	extern struct static_library lib_PartsEngine;
	for (int i = 0; lib_PartsEngine.functions[i].name; i++) {
		if (!strcmp(lib_PartsEngine.functions[i].name, "_PostLink")) {
			((void (*)(void))lib_PartsEngine.functions[i].fun)();
			break;
		}
	}
}

static int ri_fn(const char *fn_name, int *lib_out)
{
	for (int l = 0; l < ain->nr_libraries; l++) {
		if (strcmp(ain->libraries[l].name, "PartsEngine"))
			continue;
		for (int f = 0; f < ain->libraries[l].nr_functions; f++) {
			if (!strcmp(ain->libraries[l].functions[f].name, fn_name)) {
				*lib_out = l;
				return f;
			}
		}
	}
	printf("REVERSE-INHERIT PartsEngine.%s not declared\n", fn_name);
	exit(2);
}

/* Calls a PartsEngine function through its AIN declaration. args are ints or
 * floats as the declaration says (a float argument is passed as its bits). */
static union vm_value ri_hll(const char *fn, int nargs, const union vm_value *args)
{
	int lib, f = ri_fn(fn, &lib);
	struct ain_hll_function *decl = &ain->libraries[lib].functions[f];
	assert(decl->nr_arguments == nargs);
	int sp = stack_ptr;
	for (int i = 0; i < nargs; i++)
		stack_push(args[i]);
	hll_call(lib, f, 0);
	union vm_value r = { .i = -999 };
	if (stack_ptr > sp)
		r = stack_pop();
	stack_ptr = sp;
	return r;
}

#define RI_I(v) ((union vm_value){ .i = (v) })
#define RI_F(v) ((union vm_value){ .f = (v) })

static void ri_pos(int n, float x, float y) { ri_hll("SetComponentPos", 3, (union vm_value[]){ RI_I(n), RI_F(x), RI_F(y) }); }
static void ri_parent(int n, int p) { ri_hll("Parts_SetParentPartsNumber", 2, (union vm_value[]){ RI_I(n), RI_I(p) }); }
static void ri_rect(int n, int w, int h) { ri_hll("Parts_SetPartsRectangleDetectionSize", 4, (union vm_value[]){ RI_I(n), RI_I(w), RI_I(h), RI_I(1) }); }
static void ri_origin(int n, int mode) { ri_hll("SetComponentOriginPosMode", 2, (union vm_value[]){ RI_I(n), RI_I(mode) }); }
static void ri_lr(int n, int v) { ri_hll("SetComponentReverseLR", 2, (union vm_value[]){ RI_I(n), RI_I(v) }); }
static void ri_tb(int n, int v) { ri_hll("SetComponentReverseTB", 2, (union vm_value[]){ RI_I(n), RI_I(v) }); }
static int ri_get_lr(int n) { return ri_hll("GetComponentReverseLR", 1, (union vm_value[]){ RI_I(n) }).i; }
static int ri_in(int n, int x, int y) { return ri_hll("Parts_IsCursorIn", 4, (union vm_value[]){ RI_I(n), RI_I(x), RI_I(y), RI_I(1) }).i; }
static float ri_upper_left_x(int n) { return ri_hll("Parts_GetPartsUpperLeftPosX", 2, (union vm_value[]){ RI_I(n), RI_I(1) }).f; }

static void ri_expect_pos(const char *step, int n, int x, int y)
{
	struct parts *p = parts_get(n);
	RI_CHECK(p->global.pos.x == x && p->global.pos.y == y,
		 "%s: parts %d global pos (%d,%d), want (%d,%d)", step, n, p->global.pos.x, p->global.pos.y, x, y);
}

static void ri_expect_in(const char *step, int n, int x, int y, int want)
{
	int got = ri_in(n, x, y);
	RI_CHECK(got == want, "%s: Parts_IsCursorIn(%d, %d, %d) = %d, want %d", step, n, x, y, got, want);
}

/* RI1: global positions, hit boxes and UpperLeftPos through the AIN declarations. */
static void ri_hierarchy(void)
{
	ri_postlink();
	const int P = 9101, C1 = 9102, C2 = 9103, G = 9104;
	int all[] = { P, C1, C2, G };
	for (int i = 0; i < 4; i++)
		parts_get(all[i]);
	ri_pos(P, 400, 300);  ri_rect(P, 200, 100);
	ri_parent(C1, P); ri_pos(C1, 50, 20);  ri_rect(C1, 40, 30);
	ri_parent(C2, P); ri_origin(C2, 5); ri_pos(C2, -60, 10); ri_rect(C2, 20, 10);
	ri_parent(G, C1); ri_pos(G, 10, 5);   ri_rect(G, 6, 4);
	PE_UpdateComponent(0);

	/* Unflipped layout (true before and after the fix). */
	ri_expect_pos("unflipped", C1, 450, 320);
	ri_expect_pos("unflipped", C2, 340, 310);
	ri_expect_pos("unflipped", G, 460, 325);
	ri_expect_in("unflipped", C1, 455, 325, 1);
	ri_expect_in("unflipped", C1, 345, 325, 0);
	ri_expect_in("unflipped", P, 450, 350, 1);
	float ul = ri_upper_left_x(C2);
	RI_CHECK(ul == 330.0f, "unflipped: Parts_GetPartsUpperLeftPosX(C2) = %g, want 330", ul);
	if (ri_failures) {
		printf("RI1 setup failed\n");
		return;
	}

	/* Parent flipped left-right: children mirrored around x = 400. */
	ri_lr(P, 1);
	PE_UpdateComponent(0);
	RI_CHECK(ri_get_lr(P) == 1 && ri_get_lr(C1) == 0 && ri_get_lr(G) == 0,
		 "GetComponentReverseLR must return the parts' own flag: P=%d C1=%d G=%d",
		 ri_get_lr(P), ri_get_lr(C1), ri_get_lr(G));
	ri_expect_pos("P LR", C1, 350, 320);
	ri_expect_pos("P LR", C2, 460, 310);
	ri_expect_pos("P LR", G, 340, 325);
	ri_expect_in("P LR", C1, 345, 325, 1);	/* box [310,350) x [320,350) */
	ri_expect_in("P LR", C1, 311, 325, 1);
	ri_expect_in("P LR", C1, 350, 325, 0);
	ri_expect_in("P LR", C1, 455, 325, 0);
	ri_expect_in("P LR", P, 250, 350, 1);	/* P's own box [200,400) */
	ri_expect_in("P LR", P, 450, 350, 0);
	ri_expect_in("P LR", G, 336, 327, 1);	/* [334,340) */
	ri_expect_in("P LR", G, 342, 327, 0);
	ul = ri_upper_left_x(C2);	/* box corner (-10,-5) mirrored: 460 + 10 */
	RI_CHECK(ul == 470.0f, "P LR: Parts_GetPartsUpperLeftPosX(C2) = %g, want 470", ul);

	/* Child flipped as well: C1's drawing is mirrored twice (unflipped), its
	 * anchor stays mirrored by P, and G is no longer mirrored against C1. */
	ri_lr(C1, 1);
	PE_UpdateComponent(0);
	ri_expect_pos("P LR + C1 LR", C1, 350, 320);
	ri_expect_pos("P LR + C1 LR", G, 360, 325);
	ri_expect_in("P LR + C1 LR", C1, 360, 325, 1);	/* [350,390) */
	ri_expect_in("P LR + C1 LR", C1, 345, 325, 0);
	ri_expect_in("P LR + C1 LR", G, 362, 327, 1);	/* [360,366) */
	RI_CHECK(ri_get_lr(G) == 0, "G's own flag changed: %d", ri_get_lr(G));

	/* Parent flipped top-bottom too: mirrored around y = 300. */
	ri_tb(P, 1);
	PE_UpdateComponent(0);
	ri_expect_pos("P LR+TB + C1 LR", C1, 350, 280);
	ri_expect_pos("P LR+TB + C1 LR", G, 360, 275);
	ri_expect_in("P LR+TB + C1 LR", C1, 360, 270, 1);	/* [350,390) x [250,280) */
	ri_expect_in("P LR+TB + C1 LR", C1, 360, 285, 0);

	/* Unflip everything: the original layout comes back exactly. */
	ri_lr(C1, 0); ri_tb(P, 0); ri_lr(P, 0);
	PE_UpdateComponent(0);
	ri_expect_pos("restored", C1, 450, 320);
	ri_expect_pos("restored", C2, 340, 310);
	ri_expect_pos("restored", G, 460, 325);
	ri_expect_in("restored", C1, 455, 325, 1);
	ri_expect_in("restored", P, 450, 350, 1);
	ul = ri_upper_left_x(C2);
	RI_CHECK(ul == 330.0f, "restored: Parts_GetPartsUpperLeftPosX(C2) = %g, want 330", ul);
	printf("RI1 checked\n");
}

static void ri_apply(mat4 m, float x, float y, float *ox, float *oy)
{
	*ox = m[0][0] * x + m[1][0] * y + m[3][0];
	*oy = m[0][1] * x + m[1][1] * y + m[3][1];
}

static void *ri_sym(const char *name)
{
	void *p = dlsym(RTLD_DEFAULT, name);
	if (!p) {
		printf("  FAIL: %s is missing (the transform and inverse used by drawing and the pixel hit test)\n", name);
		ri_failures++;
	}
	return p;
}

/* RI2: the drawing transform, its inverse, and the TEXT/FLAT paths. */
static void ri_transform(void)
{
	ri_box_fn box = (ri_box_fn)ri_sym("parts_box_transform");
	ri_anchor_fn anchor_tf = (ri_anchor_fn)ri_sym("parts_anchor_transform");
	ri_inv_fn inv = (ri_inv_fn)ri_sym("parts_screen_to_box");
	if (!box || !anchor_tf || !inv)
		return;
	ri_postlink();
	const int P = 9201, C = 9202, S = 9203;
	parts_get(P); parts_get(C); parts_get(S);
	ri_pos(P, 400, 300); ri_rect(P, 200, 100);
	ri_parent(C, P); ri_origin(C, 5); ri_pos(C, 50, 20); ri_rect(C, 40, 30);
	ri_hll("SetComponentMagX", 2, (union vm_value[]){ RI_I(C), RI_F(1.5f) });
	ri_hll("SetComponentMagY", 2, (union vm_value[]){ RI_I(C), RI_F(0.5f) });
	ri_hll("SetComponentRotateZ", 2, (union vm_value[]){ RI_I(C), RI_F(30.0f) });
	PE_UpdateComponent(0);
	struct parts *c = parts_get(C);
	const float th = 30.0f * (float)M_PI / 180.0f, sx = 1.5f, sy = 0.5f, ox = -20, oy = -15;
	const float pts[][2] = { { 0, 0 }, { 40, 0 }, { 0, 30 }, { 40, 30 }, { 13, 7 } };
	int checked = 0;
	for (int pf = 0; pf < 4; pf++) {		/* parent flips: none, LR, TB, both */
		for (int cf = 0; cf < 4; cf++) {	/* own flips */
			ri_lr(P, pf & 1); ri_tb(P, !!(pf & 2));
			ri_lr(C, cf & 1); ri_tb(C, !!(cf & 2));
			PE_UpdateComponent(0);
			float dpx = (pf & 1) ? -1 : 1, dpy = (pf & 2) ? -1 : 1;
			float dcx = (cf & 1) ? -1 : 1, dcy = (cf & 2) ? -1 : 1;
			/* anchor: parent position + parent flip * local position */
			float ax = 400 + dpx * 50, ay = 300 + dpy * 20;
			mat4 m;
			box(c, &c->states[0].common, m);
			for (unsigned k = 0; k < sizeof(pts) / sizeof(pts[0]); k++) {
				float lx = dcx * (pts[k][0] + ox) * sx, ly = dcy * (pts[k][1] + oy) * sy;
				float rx = cosf(th) * lx - sinf(th) * ly, ry = sinf(th) * lx + cosf(th) * ly;
				float ex = ax + dpx * rx, ey = ay + dpy * ry, gx, gy;
				ri_apply(m, pts[k][0], pts[k][1], &gx, &gy);
				RI_CHECK(fabsf(gx - ex) < 1e-3f && fabsf(gy - ey) < 1e-3f,
					 "parent flip %d own flip %d: box (%g,%g) -> (%g,%g), want (%g,%g)",
					 pf, cf, pts[k][0], pts[k][1], gx, gy, ex, ey);
				float bx = -1, by = -1;
				bool ok = inv(c, &c->states[0].common, ex, ey, &bx, &by);
				RI_CHECK(ok && fabsf(bx - pts[k][0]) < 1e-3f && fabsf(by - pts[k][1]) < 1e-3f,
					 "parent flip %d own flip %d: screen (%g,%g) -> box (%g,%g), want (%g,%g)",
					 pf, cf, ex, ey, bx, by, pts[k][0], pts[k][1]);
				checked++;
			}
			/* TEXT path: no rotation or scale, the same flips around the anchor. */
			mat4 t;
			anchor_tf(c, 0.0f, false, t);
			float tx, ty;
			ri_apply(t, 7, 3, &tx, &ty);
			RI_CHECK(fabsf(tx - (ax + dpx * dcx * 7)) < 1e-3f && fabsf(ty - (ay + dpy * dcy * 3)) < 1e-3f,
				 "parent flip %d own flip %d: text offset (7,3) -> (%g,%g), want (%g,%g)",
				 pf, cf, tx, ty, ax + dpx * dcx * 7, ay + dpy * dcy * 3);
			/* FLAT path: rotation and scale, no origin offset. */
			anchor_tf(c, th, true, t);
			float lx = dcx * 7 * sx, ly = dcy * 3 * sy;
			float fx = ax + dpx * (cosf(th) * lx - sinf(th) * ly), fy = ay + dpy * (sinf(th) * lx + cosf(th) * ly);
			ri_apply(t, 7, 3, &tx, &ty);
			RI_CHECK(fabsf(tx - fx) < 1e-3f && fabsf(ty - fy) < 1e-3f,
				 "parent flip %d own flip %d: flat point (7,3) -> (%g,%g), want (%g,%g)",
				 pf, cf, tx, ty, fx, fy);
		}
	}
	/* D6: a CG showing only its surface area stays centred on its anchor when
	 * mirrored by the parent (100x50 texture, area (60,10) 30x20, origin 5). */
	ri_lr(P, 1); ri_tb(P, 0); ri_lr(C, 0); ri_tb(C, 0);
	ri_parent(S, P); ri_origin(S, 5); ri_pos(S, -100, 0); ri_rect(S, 100, 50);
	PE_UpdateComponent(0);
	struct parts *s = parts_get(S);
	s->states[0].common.surface_area = (Rectangle){ 60, 10, 30, 20 };
	parts_recalculate_hitbox(s);
	mat4 m;
	box(s, &s->states[0].common, m);
	float x0, y0, x1, y1;
	ri_apply(m, 60, 10, &x0, &y0);	/* the area's top-left texel corner */
	ri_apply(m, 90, 30, &x1, &y1);	/* its bottom-right corner */
	/* anchor 400 - (-100) = 500: area [-15,15) x [-10,10) mirrored in x */
	RI_CHECK(fabsf(x0 - 515) < 1e-3f && fabsf(y0 - 290) < 1e-3f && fabsf(x1 - 485) < 1e-3f && fabsf(y1 - 310) < 1e-3f,
		 "surface area under a flipped parent: corners (%g,%g)-(%g,%g), want (515,290)-(485,310)", x0, y0, x1, y1);
	printf("RI2 checked %d box points\n", checked);
}

/* RI3: AdvStand across sides through the game's own bytecode. */
static int ri_func(const char *name)
{
	int f = ain_get_function(ain, (char *)name);
	if (f < 0) { printf("REVERSE-INHERIT %s not declared\n", name); exit(2); }
	return f;
}

static void ri_call(int fno, int obj, int nargs, const int *args)
{
	int sp = stack_ptr, csp = call_stack_ptr;
	size_t ip = instr_ptr;
	if (obj >= 0)
		stack_push(obj);
	for (int i = 0; i < nargs; i++)
		stack_push(args[i]);
	if (obj >= 0)
		method_call(fno, VM_RETURN);
	else
		function_call(fno, VM_RETURN);
	vm_execute();
	instr_ptr = ip;
	RI_CHECK(call_stack_ptr == csp, "%s: call stack %d != %d", ain->functions[fno].name, call_stack_ptr, csp);
	stack_ptr = sp;
}

static struct parts *ri_find_rect_root(void)
{
	struct parts *p, *found = NULL;
	PARTS_LIST_FOREACH(p) {
		int n = 0;
		struct parts *child;
		PARTS_FOREACH_CHILD(child, p) n++;
		if (n == 2)
			found = p;
	}
	return found;
}

/* Box corner (0,0) of the stand image on screen: the anchor minus half the
 * width when drawn as is, plus half the width when mirrored. */
static float ri_image_left_edge(ri_box_fn box, struct parts *img)
{
	mat4 m;
	box(img, &img->states[0].common, m);
	float x, y;
	ri_apply(m, 0, 0, &x, &y);
	return x;
}

static void ri_stand(void)
{
	ri_box_fn box = (ri_box_fn)ri_sym("parts_box_transform");
	ri_postlink();
	int st = ain_get_struct(ain, "AdvStand");
	assert(st >= 0);
	int move = ri_func("AdvStand@Move"), pos_set = ri_func("AdvStand@PosType::set"), end_all = ri_func("Motion::EndAll");
	/* The game's alloc function (FUNC 0) stores NEW Motion::ExecuterCollection
	 * in Motion::Instances::executer; the probe leaves every global at -1, and
	 * Motion::Create would then call Add on nothing. */
	int g = -1, ec = ain_get_struct(ain, "Motion::ExecuterCollection");
	for (int i = 0; i < ain->nr_globals; i++)
		if (!strcmp(ain->globals[i].name, "Motion::Instances::executer"))
			g = i;
	assert(g >= 0 && ec >= 0);
	heap[global_page_slot].page->values[g].i = vm_construct_struct(ec);
	int stand = vm_construct_struct(st);	/* NEW AdvStand: sprite m_parent, AdvStandImage (rect + 2 images), PosType 4 */
	assert(stand > 0);
	PE_UpdateComponent(0);
	struct parts *rect = ri_find_rect_root();
	RI_CHECK(rect != NULL, "no rect with two children (AdvStandImage.m_parent) after NEW AdvStand");
	if (!rect)
		return;
	struct parts *img[2], *child;
	int k = 0;
	PARTS_FOREACH_CHILD(child, rect) img[k++] = child;
	/* Stand CGs are not loaded headless: give both images a 400x600 box
	 * (origin mode 8, bottom centre, as AdvStandImage@CreateImage sets). */
	for (k = 0; k < 2; k++) {
		RI_CHECK(img[k]->origin_mode == 8, "stand image origin mode %d, want 8", img[k]->origin_mode);
		img[k]->states[0].common.w = 400;
		img[k]->states[0].common.h = 600;
		parts_recalculate_hitbox(img[k]);
	}
	printf("RI3 rect %d images %d %d\n", rect->no, img[0]->no, img[1]->no);
	int arg;
	/* Right (constructor) -> left: "Section:AdvStand [Time:200|ReverseLR:0 1]" on the rect. */
	arg = 1;	/* LeftCenter */
	ri_call(move, stand, 1, &arg);
	ri_call(end_all, -1, 0, NULL);
	PE_UpdateComponent(0);
	RI_CHECK(ri_get_lr(rect->no) == 1, "after Move(LeftCenter)+EndAll the rect's ReverseLR is %d, want 1", ri_get_lr(rect->no));
	RI_CHECK(ri_get_lr(img[0]->no) == 0 && ri_get_lr(img[1]->no) == 0, "the images' own flags changed");
	float ax = img[0]->global.pos.x;
	printf("RI3 after Move(LeftCenter): anchor x %g\n", ax);
	if (box) {
		for (k = 0; k < 2; k++) {
			float e = ri_image_left_edge(box, img[k]);
			RI_CHECK(fabsf(e - (ax + 200)) < 1e-3f,
				 "left side, rect flipped: image %d texture x=0 at %g, want %g (mirrored)", img[k]->no, e, ax + 200);
		}
	}
	/* Back to the right: "ReverseLR:1 0". */
	arg = 4;	/* RightCenter */
	ri_call(move, stand, 1, &arg);
	ri_call(end_all, -1, 0, NULL);
	PE_UpdateComponent(0);
	RI_CHECK(ri_get_lr(rect->no) == 0, "after Move(RightCenter)+EndAll the rect's ReverseLR is %d, want 0", ri_get_lr(rect->no));
	ax = img[0]->global.pos.x;
	if (box) {
		float e = ri_image_left_edge(box, img[0]);
		RI_CHECK(fabsf(e - (ax - 200)) < 1e-3f, "right side: texture x=0 at %g, want %g (as drawn)", e, ax - 200);
	}
	/* A stand placed on the left flips its images itself (PosType::set ->
	 * AdvStandImage@Reverse::postset). Moving it to the right clears only
	 * the rect's flag ("ReverseLR:1 0"), so the flips still XOR to one: the
	 * original keeps such a stand mirrored on the right. */
	arg = 0;	/* LeftLeft */
	ri_call(pos_set, stand, 1, &arg);
	PE_UpdateComponent(0);
	RI_CHECK(ri_get_lr(img[0]->no) == 1 && ri_get_lr(img[1]->no) == 1, "PosType::set(LeftLeft) did not flip the images");
	arg = 4;
	ri_call(move, stand, 1, &arg);
	ri_call(end_all, -1, 0, NULL);
	PE_UpdateComponent(0);
	RI_CHECK(ri_get_lr(rect->no) == 0, "after Move(RightCenter) from the left the rect's ReverseLR is %d", ri_get_lr(rect->no));
	ax = img[0]->global.pos.x;
	if (box) {
		float e = ri_image_left_edge(box, img[0]);
		RI_CHECK(fabsf(e - (ax + 200)) < 1e-3f, "left-placed stand moved right: texture x=0 at %g, want %g (still mirrored)", e, ax + 200);
	}
	printf("RI3 checked\n");
}

static int ri_run(const char *name, void (*fn)(void))
{
	fflush(stdout);
	pid_t pid = fork();
	if (pid == 0) {
		ri_failures = 0;
		fn();
		fflush(stdout);
		_exit(ri_failures ? 1 : 0);
	}
	int status = 0;
	waitpid(pid, &status, 0);
	bool ok = WIFEXITED(status) && WEXITSTATUS(status) == 0;
	printf("%s %s%s\n", name, ok ? "PASS" : "FAIL", WIFSIGNALED(status) ? " (crashed)" : "");
	fflush(stdout);
	return ok ? 0 : 1;
}

static int test_reverse_inherit(void)
{
	int failed = 0;
	failed += ri_run("RI1 parent flip mirrors children (positions, hit boxes, UpperLeftPos)", ri_hierarchy);
	failed += ri_run("RI2 drawing transform, its inverse, TEXT/FLAT paths, surface area", ri_transform);
	failed += ri_run("RI3 AdvStand@Move across sides flips the stand image", ri_stand);
	printf("REVERSE-INHERIT summary: %d/3 case(s) failed\n", failed);
	return failed ? 91 : 0;
}
