/* parts-rotate-xy: SetComponentRotateX / SetComponentRotateY turn a parts
 * in depth, and the original draws and hit-tests the result through a
 * perspective camera.
 *
 * Original (static disassembly only): the setters store the degrees at +0xe0
 * / +0xe4 of the parts (0x58d7a0 / 0x58d840), the getters read them back
 * (0x58d980 / 0x58da20). The per-parts update 0x535260 passes them with the
 * Z angle (+0xe8) and the scales (+0xd8 / +0xdc) to 0x579bb0 -> 0x4e6d80 ->
 * 0x5c3ae0, which builds S(scale) * Ry * Rx * Rz (D3DX row-vector matrices
 * 0x5c3e70 / 0x5c3de0 / 0x5c3d60, each angle the degrees times -pi/180 at
 * 0x81338c) and writes as translation the parts' pixel position unprojected
 * onto the camera's plane; 0x4e6d80 puts S(reverse) in front and the parent's
 * matrix behind. The camera (0x579970 -> 0x5b93f0, screen size and 30.0;
 * projection 0x5b9130) has cot(15 deg) on x, (W/H) cot(15 deg) on y, depth
 * 1.5 z - 15 over w = z, and its plane at z = 20, whose depth 0.75 is used
 * to unproject. The hit test 0x57a6b0 sends the box's four corners through
 * 0x5c3910 (unproject the pixel point, the parts' matrix, + 20 in depth, the
 * projection, x * W/2 and -y * H/2) and tests the point in the triangles
 * (corner 0, 1, 2) and (2, 1, 3) with 0x57aa60.
 *
 * The model here follows those steps literally: row vectors, the D3DX
 * matrices with the negated angles, a y-up view space, the projection and
 * its numerical inverse. It assumes what this port also assumes and the
 * disassembly does not show (where the root moves the origin): pixel
 * positions are relative to the centre of the screen, the camera's axis.
 *
 * Before the fix the X and Y angles were stored and printed "unimplemented"
 * but drew and hit-tested nothing; the getters were not implemented (0).
 *
 * Cases (each in its own fork):
 *   RX1 the AIN declarations: Set / GetComponentRotateX and Y read back.
 *   RX2 drawn coverage (offscreen GL, 96x128 view): a box turned in Y at
 *       the centre, in X, off the centre, with scale, Z angle and a flip,
 *       a child of a turned parent, 180 degrees in Y (its back: nothing is
 *       drawn), and 1000 degrees in X with scale 4 in y (AnimateText's
 *       start); every pixel the model puts 0.75 px inside is drawn and
 *       every one 0.75 px outside is not.
 *   RX3 Parts_IsCursorIn through the AIN declaration (1280x720, the game's
 *       camera): a 400x240 rectangle turned 60 degrees in Y, a grid of
 *       points against the model's projected box (points within 0.5 px of
 *       an edge skipped); a child of a turned parent likewise; with
 *       PixelDecide on a texture whose left half is opaque, turned 180
 *       degrees in Y, the opaque half is on the right.
 *   RX4 the corners of the box through parts_box_transform and the new
 *       projection (dlsym parts_perspective) against the model to 0.05 px:
 *       scale, Z, X and Y angles and both flips at once; a three-level
 *       chain with angles at every level.
 *   RX5 PE_Save / PE_Load keeps the angles (the getters) and the drawing.
 *   RX6 guard, AIN version 13: the X and Y angles change no matrix.
 *   RX7 no "unimplemented" on stderr for SetComponentRotateX / Y,
 *       StopSoundWithoutSystemSound and CrayfishLogViewer.SetSaveFolderName.
 *   RX8 a clip area of the parent cuts a child turned in Y on the screen
 *       rectangle: nothing drawn outside it, and inside it the model's box.
 *   RX9 the back of a card: under a parent spinning in Y, a child at 0
 *       degrees is drawn while it faces the camera and a child at 180 when
 *       the parent has turned it round (then reading the right way); the
 *       culled one is still hit-tested (0x57aa60 does not look at the
 *       side); a ReverseLR child and one with scale x -1 count as fronts.
 *       The original's caption (a Wine screenshot of the result screen at
 *       rest) shows the front without its back; why the original keeps
 *       flipped parts visible under culling was not found.
 * Only functions that exist before the fix are called by name (the new one
 * through dlsym), so before-check builds.
 */
#include <dlfcn.h>
static int rx_failures;
#define RX_CHECK(c, ...) do { if (!(c)) { printf("  FAIL: "); printf(__VA_ARGS__); printf("\n"); rx_failures++; } } while (0)

/* --- the model (row vectors: v' = v * M) --- */
typedef double rx_m4[4][4];

static void rx_identity(rx_m4 m)
{
	for (int r = 0; r < 4; r++)
		for (int c = 0; c < 4; c++)
			m[r][c] = r == c;
}
static void rx_mul(rx_m4 a, rx_m4 b, rx_m4 out)
{
	rx_m4 t;
	for (int r = 0; r < 4; r++)
		for (int c = 0; c < 4; c++) {
			t[r][c] = 0;
			for (int k = 0; k < 4; k++)
				t[r][c] += a[r][k] * b[k][c];
		}
	memcpy(out, t, sizeof t);
}
static void rx_vmul(const double v[4], rx_m4 m, double out[4])
{
	double t[4];
	for (int c = 0; c < 4; c++)
		t[c] = v[0] * m[0][c] + v[1] * m[1][c] + v[2] * m[2][c] + v[3] * m[3][c];
	memcpy(out, t, sizeof t);
}
/* D3DXMatrixRotationX/Y/Z and Scaling (row vectors, left-handed). */
static void rx_rot_x(rx_m4 m, double a) { rx_identity(m); m[1][1] = cos(a); m[1][2] = sin(a); m[2][1] = -sin(a); m[2][2] = cos(a); }
static void rx_rot_y(rx_m4 m, double a) { rx_identity(m); m[0][0] = cos(a); m[0][2] = -sin(a); m[2][0] = sin(a); m[2][2] = cos(a); }
static void rx_rot_z(rx_m4 m, double a) { rx_identity(m); m[0][0] = cos(a); m[0][1] = sin(a); m[1][0] = -sin(a); m[1][1] = cos(a); }
static void rx_scaling(rx_m4 m, double x, double y) { rx_identity(m); m[0][0] = x; m[1][1] = y; }

/* Gauss-Jordan inverse. */
static bool rx_inverse(rx_m4 m, rx_m4 out)
{
	double a[4][8];
	for (int r = 0; r < 4; r++)
		for (int c = 0; c < 8; c++)
			a[r][c] = c < 4 ? m[r][c] : (c - 4 == r);
	for (int c = 0; c < 4; c++) {
		int p = c;
		for (int r = c + 1; r < 4; r++)
			if (fabs(a[r][c]) > fabs(a[p][c]))
				p = r;
		if (fabs(a[p][c]) < 1e-12)
			return false;
		for (int k = 0; k < 8; k++) { double t = a[c][k]; a[c][k] = a[p][k]; a[p][k] = t; }
		double d = a[c][c];
		for (int k = 0; k < 8; k++)
			a[c][k] /= d;
		for (int r = 0; r < 4; r++) {
			if (r == c)
				continue;
			double f = a[r][c];
			for (int k = 0; k < 8; k++)
				a[r][k] -= f * a[c][k];
		}
	}
	for (int r = 0; r < 4; r++)
		for (int c = 0; c < 4; c++)
			out[r][c] = a[r][c + 4];
	return true;
}

struct rx_cam {
	double w, h;
	rx_m4 proj, inv;
	double plane_ndc;	/* the depth of (0, 0, 20) after the projection */
};

static void rx_cam_init(struct rx_cam *c, double w, double h)
{
	double t = tan(30.0 * 0.5 * M_PI / 180.0);
	c->w = w; c->h = h;
	memset(c->proj, 0, sizeof c->proj);
	c->proj[0][0] = 1.0 / t;
	c->proj[1][1] = w / (h * t);
	c->proj[2][2] = 1.5;
	c->proj[2][3] = 1.0;
	c->proj[3][2] = -15.0;
	rx_inverse(c->proj, c->inv);
	double v[4] = { 0, 0, 20, 1 };
	rx_vmul(v, c->proj, v);
	c->plane_ndc = v[2] / v[3];
}

/* A pixel point (relative to the screen's centre) onto the camera's plane,
 * as 0x5c3910 and 0x5c3ae0 do (only x and y are kept). */
static void rx_unproject(struct rx_cam *c, double x, double y, double out[2])
{
	double v[4] = { x / (c->w / 2), -(y / (c->h / 2)), c->plane_ndc, 1 };
	rx_vmul(v, c->inv, v);
	if (v[3] != 1.0 && v[3] != 0.0) {
		v[0] /= v[3];
		v[1] /= v[3];
	}
	out[0] = v[0];
	out[1] = v[1];
}

struct rx_level {
	double x, y;		/* position (pixels, in the parent) */
	double mx, my;		/* scale */
	double ax, ay, az;	/* degrees */
	bool lr, tb;		/* flips */
};
#define RX_LEVEL(x, y) ((struct rx_level){ x, y, 1, 1, 0, 0, 0, false, false })

/* One level's matrix (0x4e6d80 with 0x5c3ae0). */
static void rx_level_matrix(struct rx_cam *c, const struct rx_level *l, rx_m4 out)
{
	const double k = -M_PI / 180.0;
	rx_m4 m, r;
	rx_scaling(m, l->mx, l->my);
	if (l->ay != 0) { rx_rot_y(r, l->ay * k); rx_mul(m, r, m); }
	if (l->ax != 0) { rx_rot_x(r, l->ax * k); rx_mul(m, r, m); }
	if (l->az != 0) { rx_rot_z(r, l->az * k); rx_mul(m, r, m); }
	double t[2];
	rx_unproject(c, l->x, l->y, t);
	m[3][0] = t[0];
	m[3][1] = t[1];
	rx_scaling(r, l->lr ? -1 : 1, l->tb ? -1 : 1);
	rx_mul(r, m, out);
}

/* A box point (pixels from the innermost level's anchor) of a chain (the
 * top level first, its position on the screen) to the screen. */
static void rx_model_point(struct rx_cam *c, const struct rx_level *chain, int n,
		double bx, double by, double *sx, double *sy)
{
	rx_m4 acc, lm;
	rx_identity(acc);
	for (int i = 0; i < n; i++) {
		struct rx_level l = chain[i];
		if (i == 0) {
			l.x -= c->w / 2;
			l.y -= c->h / 2;
		}
		rx_level_matrix(c, &l, lm);
		rx_mul(lm, acc, acc);
	}
	double p[2];
	rx_unproject(c, bx, by, p);
	double v[4] = { p[0], p[1], 0, 1 };
	rx_vmul(v, acc, v);
	if (v[3] != 1.0 && v[3] != 0.0) {
		v[0] /= v[3]; v[1] /= v[3]; v[2] /= v[3];
	}
	double q[4] = { v[0], v[1], v[2] + 20.0, 1 };
	rx_vmul(q, c->proj, q);
	if (q[3] != 0.0) {
		q[0] /= q[3];
		q[1] /= q[3];
	}
	*sx = q[0] * (c->w / 2) + c->w / 2;
	*sy = -q[1] * (c->h / 2) + c->h / 2;
}

/* The model's box on the screen: corners top-left, top-right, bottom-left,
 * bottom-right of the box (ox, oy) .. (ox + w, oy + h) from the anchor. */
static void rx_model_box(struct rx_cam *c, const struct rx_level *chain, int n,
		double ox, double oy, double w, double h, double q[4][2])
{
	const double b[4][2] = { { ox, oy }, { ox + w, oy }, { ox, oy + h }, { ox + w, oy + h } };
	for (int i = 0; i < 4; i++)
		rx_model_point(c, chain, n, b[i][0], b[i][1], &q[i][0], &q[i][1]);
}

/* The signed distance of (x, y) to the box's outline: > 0 inside. */
static double rx_inside(double q[4][2], double x, double y)
{
	const int ring[4] = { 0, 1, 3, 2 };
	double area = 0;
	for (int i = 0; i < 4; i++) {
		const double *a = q[ring[i]], *b = q[ring[(i + 1) % 4]];
		area += a[0] * b[1] - b[0] * a[1];
	}
	double s = area > 0 ? 1 : -1, d = 1e9;
	for (int i = 0; i < 4; i++) {
		const double *a = q[ring[i]], *b = q[ring[(i + 1) % 4]];
		double ex = b[0] - a[0], ey = b[1] - a[1], len = sqrt(ex * ex + ey * ey);
		if (len < 1e-9)
			continue;
		double cross = (ex * (y - a[1]) - ey * (x - a[0])) / len * s;
		if (cross < d)
			d = cross;
	}
	return d;
}

/* --- engine side --- */
static void rx_set(int n, double x, double y)
{
	ri_hll("SetComponentRotateX", 2, (union vm_value[]){ RI_I(n), RI_F((float)x) });
	ri_hll("SetComponentRotateY", 2, (union vm_value[]){ RI_I(n), RI_F((float)y) });
}
static float rx_get_x(int n) { return ri_hll("GetComponentRotateX", 1, (union vm_value[]){ RI_I(n) }).f; }
static float rx_get_y(int n) { return ri_hll("GetComponentRotateY", 1, (union vm_value[]){ RI_I(n) }).f; }
static void rx_z(int n, double z) { ri_hll("SetComponentRotateZ", 2, (union vm_value[]){ RI_I(n), RI_F((float)z) }); }
static void rx_mag(int n, double x, double y)
{
	ri_hll("SetComponentMagX", 2, (union vm_value[]){ RI_I(n), RI_F((float)x) });
	ri_hll("SetComponentMagY", 2, (union vm_value[]){ RI_I(n), RI_F((float)y) });
}
static void rx_level_apply(int n, const struct rx_level *l)
{
	ri_pos(n, (float)l->x, (float)l->y);
	rx_mag(n, l->mx, l->my);
	rx_z(n, l->az);
	rx_set(n, l->ax, l->ay);
	ri_lr(n, l->lr);
	ri_tb(n, l->tb);
}

/* A parts with a w x h texture: the left half (r1,g1,b1,a1), the right half
 * (r2,g2,b2,a2). */
static void rx_tex(int n, int w, int h, SDL_Color left, SDL_Color right)
{
	struct parts *p = parts_get(n);
	struct parts_cg *cg = parts_get_cg(p, 0);
	gfx_init_texture_rgba(&cg->common.texture, w, h, left);
	gfx_fill_with_alpha(&cg->common.texture, w / 2, 0, w - w / 2, h, right.r, right.g, right.b, right.a);
	parts_set_dims(p, &cg->common, w, h);
}
#define RX_WHITE ((SDL_Color){ 255, 255, 255, 255 })

/* Draws parts n alone and compares its coverage with the model's box. */
static void rx_expect_drawn(const char *what, int n, struct rx_cam *c, double q[4][2])
{
	uint8_t *px = ts_draw(parts_get(n));
	int missing = 0, extra = 0, drawn = 0, first_m = -1, first_e = -1;
	for (int y = 0; y < TS_VIEW_H; y++) {
		for (int x = 0; x < TS_VIEW_W; x++) {
			bool a = px[(y * TS_VIEW_W + x) * 4 + 3] != 0;
			double d = rx_inside(q, x + 0.5, y + 0.5);
			drawn += a;
			if (d > 0.75 && !a) { if (first_m < 0) first_m = y * TS_VIEW_W + x; missing++; }
			if (d < -0.75 && a) { if (first_e < 0) first_e = y * TS_VIEW_W + x; extra++; }
		}
	}
	free(px);
	(void)c;
	RX_CHECK(missing == 0 && extra == 0 && drawn > 0,
		 "%s: %d pixel(s) inside the model's box not drawn (first (%d,%d)), %d outside drawn (first (%d,%d)), %d drawn;"
		 " box (%.1f,%.1f) (%.1f,%.1f) (%.1f,%.1f) (%.1f,%.1f)", what, missing,
		 first_m < 0 ? -1 : first_m % TS_VIEW_W, first_m < 0 ? -1 : first_m / TS_VIEW_W, extra,
		 first_e < 0 ? -1 : first_e % TS_VIEW_W, first_e < 0 ? -1 : first_e / TS_VIEW_W, drawn,
		 q[0][0], q[0][1], q[1][0], q[1][1], q[2][0], q[2][1], q[3][0], q[3][1]);
}

static int rx_drawn_count(int n)
{
	uint8_t *px = ts_draw(parts_get(n));
	int drawn = 0;
	for (int i = 0; i < TS_VIEW_W * TS_VIEW_H; i++)
		drawn += px[i * 4 + 3] != 0;
	free(px);
	return drawn;
}

/* The colour drawn where the model puts the box point (bx, by). */
static void rx_expect_colour(const char *what, int n, struct rx_cam *c, const struct rx_level *chain,
		int depth, double bx, double by, int r, int g, int b)
{
	double sx, sy;
	rx_model_point(c, chain, depth, bx, by, &sx, &sy);
	uint8_t *px = ts_draw(parts_get(n));
	int x = (int)floor(sx), y = (int)floor(sy);
	const uint8_t *p = (x >= 0 && y >= 0 && x < TS_VIEW_W && y < TS_VIEW_H) ? px + (y * TS_VIEW_W + x) * 4 : NULL;
	RX_CHECK(p && abs(p[0] - r) <= 2 && abs(p[1] - g) <= 2 && abs(p[2] - b) <= 2 && p[3] == 255,
		 "%s: at (%d,%d) the colour (%d,%d,%d,%d), want (%d,%d,%d,255)", what, x, y,
		 p ? p[0] : -1, p ? p[1] : -1, p ? p[2] : -1, p ? p[3] : -1, r, g, b);
	free(px);
}

/* RX1 */
static void rx_getters(void)
{
	ri_postlink();
	const int A = 9701;
	parts_get(A);
	rx_set(A, 37.5, -120.25);
	RX_CHECK(rx_get_x(A) == 37.5f && rx_get_y(A) == -120.25f,
		 "GetComponentRotateX/Y = (%g, %g), want (37.5, -120.25)", rx_get_x(A), rx_get_y(A));
	rx_set(A, 1000, 0);
	RX_CHECK(rx_get_x(A) == 1000.0f && rx_get_y(A) == 0.0f,
		 "after (1000, 0): (%g, %g)", rx_get_x(A), rx_get_y(A));
	printf("RX1 checked\n");
}

/* RX2 */
static void rx_drawn(void)
{
	ts_render_setup();	/* GL, the parts shader, a 96x128 view */
	struct rx_cam cam;
	rx_cam_init(&cam, TS_VIEW_W, TS_VIEW_H);
	const SDL_Color red = { 255, 0, 0, 255 }, blue = { 0, 0, 255, 255 };
	double q[4][2];

	/* (a) 40x24 centred on the screen's centre, 60 degrees in Y. */
	const int A = 9711;
	rx_tex(A, 40, 24, red, blue);
	ri_origin(A, 5);
	struct rx_level la = RX_LEVEL(48, 64); la.ay = 60;
	rx_level_apply(A, &la);
	PE_UpdateComponent(0);
	rx_model_box(&cam, &la, 1, -20, -12, 40, 24, q);
	rx_expect_drawn("(a) Y 60 at the centre", A, &cam, q);
	rx_expect_colour("(a) left quarter", A, &cam, &la, 1, -10, 0, 255, 0, 0);
	rx_expect_colour("(a) right quarter", A, &cam, &la, 1, 10, 0, 0, 0, 255);

	/* (b) the same in X, 50 degrees. */
	la.ay = 0; la.ax = 50;
	rx_level_apply(A, &la);
	PE_UpdateComponent(0);
	rx_model_box(&cam, &la, 1, -20, -12, 40, 24, q);
	rx_expect_drawn("(b) X 50 at the centre", A, &cam, q);

	/* (c) 24x16 at (8, 20), top-left origin, 45 degrees in Y: far from the
	 * camera's axis the box leans toward the centre. */
	const int C = 9712;
	rx_tex(C, 24, 16, RX_WHITE, RX_WHITE);
	struct rx_level lc = RX_LEVEL(8, 20); lc.ay = 45;
	rx_level_apply(C, &lc);
	PE_UpdateComponent(0);
	rx_model_box(&cam, &lc, 1, 0, 0, 24, 16, q);
	rx_expect_drawn("(c) Y 45 off the centre", C, &cam, q);

	/* (d) scale, Z, X, Y and a left-right flip at once. */
	const int D = 9713;
	rx_tex(D, 32, 20, red, blue);
	ri_origin(D, 5);
	struct rx_level ld = { 50, 70, 1.25, 0.75, 30, 35, 20, true, false };
	rx_level_apply(D, &ld);
	PE_UpdateComponent(0);
	rx_model_box(&cam, &ld, 1, -16, -10, 32, 20, q);
	rx_expect_drawn("(d) scale, Z 20, X 30, Y 35, flipped", D, &cam, q);
	rx_expect_colour("(d) left half", D, &cam, &ld, 1, -8, 0, 255, 0, 0);

	/* (e) a child of a parent turned in Y: drawn through the parent's depth. */
	const int P = 9714, E = 9715;
	parts_get(P);
	struct rx_level lp = RX_LEVEL(48, 64); lp.ay = 40; lp.az = 10;
	rx_level_apply(P, &lp);
	rx_tex(E, 16, 12, RX_WHITE, RX_WHITE);
	ri_parent(E, P);
	struct rx_level le = RX_LEVEL(10, -8); le.ax = 30;
	rx_level_apply(E, &le);
	PE_UpdateComponent(0);
	struct rx_level chain[2] = { lp, le };
	rx_model_box(&cam, chain, 2, 0, 0, 16, 12, q);
	rx_expect_drawn("(e) child of a parent turned in Y", E, &cam, q);

	/* (f) 180 degrees in Y: the box shows its back, which is not drawn. */
	la.ax = 0; la.ay = 180;
	rx_level_apply(A, &la);
	PE_UpdateComponent(0);
	RX_CHECK(rx_drawn_count(A) == 0, "(f) Y 180: %d pixel(s) of the back drawn", rx_drawn_count(A));

	/* (g) AnimateText's start: 1000 degrees in X, scale 4 in y. */
	la.ay = 0; la.ax = 1000; la.my = 4;
	rx_level_apply(A, &la);
	PE_UpdateComponent(0);
	rx_model_box(&cam, &la, 1, -20, -12, 40, 24, q);
	rx_expect_drawn("(g) X 1000, scale y 4", A, &cam, q);

	/* (h) back to no angle: the plain rectangle. */
	la = RX_LEVEL(48, 64);
	rx_level_apply(A, &la);
	PE_UpdateComponent(0);
	rx_model_box(&cam, &la, 1, -20, -12, 40, 24, q);
	rx_expect_drawn("(h) no angle", A, &cam, q);
	printf("RX2 checked\n");
}

/* RX3 */
static int rx_grid_mismatch(const char *what, int n, struct rx_cam *c, double q[4][2],
		int x0, int y0, int x1, int y1, int step)
{
	int bad = 0, in = 0, out = 0;
	for (int y = y0; y <= y1; y += step) {
		for (int x = x0; x <= x1; x += step) {
			double d = rx_inside(q, x, y);
			if (fabs(d) < 0.5)
				continue;
			int want = d > 0, got = ri_in(n, x, y);
			in += want; out += !want;
			if (got != want) {
				if (bad < 3)
					printf("  %s: Parts_IsCursorIn(%d, %d) = %d, want %d\n", what, x, y, got, want);
				bad++;
			}
		}
	}
	RX_CHECK(bad == 0 && in > 0 && out > 0, "%s: %d of %d points differ from the model's box", what, bad, in + out);
	return bad;
}

static void rx_hit(void)
{
	ri_postlink();
	config.view_width = 1280;
	config.view_height = 720;
	struct rx_cam cam;
	rx_cam_init(&cam, 1280, 720);
	double q[4][2];
	/* A 400x240 rectangle centred at (500, 300), 60 degrees in Y. */
	const int A = 9721;
	parts_get(A);
	ri_rect(A, 400, 240);
	ri_origin(A, 5);
	struct rx_level la = RX_LEVEL(500, 300); la.ay = 60;
	rx_level_apply(A, &la);
	PE_UpdateComponent(0);
	rx_model_box(&cam, &la, 1, -200, -120, 400, 240, q);
	rx_grid_mismatch("Y 60", A, &cam, q, 280, 160, 720, 440, 8);
	/* The point (650, 300) is in the upright rectangle, not in the turned one. */
	RX_CHECK(ri_in(A, 650, 300) == 0, "Y 60: (650, 300) is under the cursor (the upright box)");
	RX_CHECK(ri_in(A, 550, 300) == 1, "Y 60: (550, 300) is not under the cursor");

	/* A child of a parent turned in X and Z. */
	const int P = 9722, C = 9723;
	parts_get(P); parts_get(C);
	struct rx_level lp = RX_LEVEL(640, 360); lp.ax = 50; lp.az = 30;
	rx_level_apply(P, &lp);
	ri_parent(C, P);
	ri_rect(C, 200, 100);
	struct rx_level lc = RX_LEVEL(-100, -50); lc.ay = -35;
	rx_level_apply(C, &lc);
	PE_UpdateComponent(0);
	struct rx_level chain[2] = { lp, lc };
	rx_model_box(&cam, chain, 2, 0, 0, 200, 100, q);
	rx_grid_mismatch("child of X 50, Z 30", C, &cam, q, 380, 160, 900, 560, 8);
	printf("RX3 checked\n");
}

/* RX3 (pixels): PixelDecide on a half-transparent texture turned 180 in Y. */
static void rx_hit_pixels(void)
{
	ts_render_setup();
	struct rx_cam cam;
	rx_cam_init(&cam, TS_VIEW_W, TS_VIEW_H);
	const int A = 9724;
	rx_tex(A, 40, 24, (SDL_Color){ 255, 255, 255, 255 }, (SDL_Color){ 0, 0, 0, 0 });
	ri_origin(A, 5);
	ri_hll("Parts_SetPartsPixelDecide", 2, (union vm_value[]){ RI_I(A), RI_I(1) });
	struct rx_level la = RX_LEVEL(48, 64);
	rx_level_apply(A, &la);
	PE_UpdateComponent(0);
	RX_CHECK(ri_in(A, 38, 64) == 1 && ri_in(A, 58, 64) == 0, "unturned: the opaque half is not on the left");
	la.ay = 180;
	rx_level_apply(A, &la);
	PE_UpdateComponent(0);
	RX_CHECK(ri_in(A, 58, 64) == 1, "Y 180: (58, 64) on the opaque half is not under the cursor");
	RX_CHECK(ri_in(A, 38, 64) == 0, "Y 180: (38, 64) on the transparent half is under the cursor");
	/* 60 degrees: the opaque half is the near, larger one on the left. */
	la.ay = 60;
	rx_level_apply(A, &la);
	PE_UpdateComponent(0);
	double sx, sy;
	rx_model_point(&cam, &la, 1, -10, 0, &sx, &sy);
	RX_CHECK(ri_in(A, (int)sx, (int)sy) == 1, "Y 60: the opaque half's middle (%d,%d) is not under the cursor", (int)sx, (int)sy);
	rx_model_point(&cam, &la, 1, 10, 0, &sx, &sy);
	RX_CHECK(ri_in(A, (int)sx, (int)sy) == 0, "Y 60: the transparent half's middle (%d,%d) is under the cursor", (int)sx, (int)sy);
	printf("RX3 pixels checked\n");
}

/* RX4 */
typedef void (*rx_persp_fn)(mat4);
/* out = m * v for a column-major mat4 (the engine's). */
static void rx_glm_mulv(mat4 m, const float v[4], float out[4])
{
	for (int r = 0; r < 4; r++)
		out[r] = m[0][r] * v[0] + m[1][r] * v[1] + m[2][r] * v[2] + m[3][r] * v[3];
}
static void rx_expect_corners(const char *what, int n, struct rx_cam *cam, const struct rx_level *chain, int depth,
		double ox, double oy, double w, double h)
{
	rx_persp_fn persp_fn = (rx_persp_fn)dlsym(RTLD_DEFAULT, "parts_perspective");
	RX_CHECK(persp_fn != NULL, "%s: parts_perspective is not there", what);
	if (!persp_fn)
		return;
	struct parts *p = parts_get(n);
	mat4 m, persp;
	parts_box_transform(p, &p->states[0].common, m);
	persp_fn(persp);
	double q[4][2];
	rx_model_box(cam, chain, depth, ox, oy, w, h, q);
	const float b[4][2] = { { 0, 0 }, { (float)w, 0 }, { 0, (float)h }, { (float)w, (float)h } };
	for (int i = 0; i < 4; i++) {
		float v[4], s[4];
		rx_glm_mulv(m, (float[4]){ b[i][0], b[i][1], 0, 1 }, v);
		rx_glm_mulv(persp, v, s);
		float x = s[0] / s[3], y = s[1] / s[3];
		RX_CHECK(fabs(x - q[i][0]) < 0.05 && fabs(y - q[i][1]) < 0.05,
			 "%s: corner %d at (%.3f, %.3f), the model's (%.3f, %.3f)", what, i, x, y, q[i][0], q[i][1]);
	}
}

static void rx_corners(void)
{
	ri_postlink();
	config.view_width = 1280;
	config.view_height = 720;
	struct rx_cam cam;
	rx_cam_init(&cam, 1280, 720);
	const int A = 9731;
	parts_get(A);
	ri_rect(A, 300, 180);
	ri_origin(A, 5);
	struct rx_level la = { 300, 500, 1.5, 0.5, 40, 25, 30, true, true };
	rx_level_apply(A, &la);
	PE_UpdateComponent(0);
	rx_expect_corners("one level, everything", A, &cam, &la, 1, -150, -90, 300, 180);
	/* Three levels, angles at every level. */
	const int P = 9732, Q = 9733, R = 9734;
	parts_get(P); parts_get(Q); parts_get(R);
	ri_rect(R, 120, 60);
	struct rx_level l1 = { 640, 360, 0.8, 0.8, 35, 0, 15, false, false };
	struct rx_level l2 = { 50, -30, 1, 1.2, 0, -50, 0, false, true };
	struct rx_level l3 = { -20, 10, 1, 1, 20, 10, -40, true, false };
	rx_level_apply(P, &l1);
	ri_parent(Q, P); rx_level_apply(Q, &l2);
	ri_parent(R, Q); rx_level_apply(R, &l3);
	PE_UpdateComponent(0);
	struct rx_level chain[3] = { l1, l2, l3 };
	rx_expect_corners("three levels", R, &cam, chain, 3, 0, 0, 120, 60);
	printf("RX4 checked\n");
}

/* RX5 */
static void rx_save(void)
{
	ts_render_setup();
	struct rx_cam cam;
	rx_cam_init(&cam, TS_VIEW_W, TS_VIEW_H);
	const int A = 9741;
	rx_tex(A, 40, 24, RX_WHITE, RX_WHITE);
	ri_origin(A, 5);
	struct rx_level la = RX_LEVEL(48, 64); la.ax = 33; la.ay = -47;
	rx_level_apply(A, &la);
	PE_UpdateComponent(0);
	struct page *saved = NULL;
	RX_CHECK(PE_Save(&saved), "PE_Save failed");
	if (!saved)
		return;
	RX_CHECK(PE_Load(&saved), "PE_Load failed");
	PE_UpdateComponent(0);
	RX_CHECK(rx_get_x(A) == 33.0f && rx_get_y(A) == -47.0f, "after PE_Load: (%g, %g), want (33, -47)", rx_get_x(A), rx_get_y(A));
	/* The loaded parts has no texture of its own (no CG name): give it one. */
	rx_tex(A, 40, 24, RX_WHITE, RX_WHITE);
	PE_UpdateComponent(0);
	double q[4][2];
	rx_model_box(&cam, &la, 1, -20, -12, 40, 24, q);
	rx_expect_drawn("after PE_Load", A, &cam, q);
	printf("RX5 checked\n");
}

/* RX6 */
static void rx_v13(void)
{
	ri_postlink();
	ain->version = 13;
	config.view_width = 1280;
	config.view_height = 720;
	const int A = 9751;
	struct parts *p = parts_get(A);
	PE_SetPos(A, 400, 300);
	PE_SetPartsRectangleDetectionSize(A, 100, 40, 1);
	PE_UpdateComponent(0);
	mat4 before, after;
	parts_box_transform(p, &p->states[0].common, before);
	PE_SetPartsRotateX(A, 40);
	PE_SetPartsRotateY(A, 60);
	PE_UpdateComponent(0);
	parts_box_transform(p, &p->states[0].common, after);
	RX_CHECK(!memcmp(before, after, sizeof before), "v13: the X and Y angles changed the box transform");
	RX_CHECK(PE_IsCursorIn(A, 495, 335, 1) && !PE_IsCursorIn(A, 505, 335, 1), "v13: the hit box is not (400,300) 100x40");
	printf("RX6 checked\n");
}

/* RX7 */
static void rx_quiet(void)
{
	ri_postlink();
	const int A = 9761;
	parts_get(A);
	fflush(stderr);
	FILE *capture = tmpfile();
	int saved = dup(STDERR_FILENO);
	RX_CHECK(capture && saved >= 0 && dup2(fileno(capture), STDERR_FILENO) >= 0, "cannot capture stderr");
	if (!capture || saved < 0)
		return;
	rx_set(A, 12, 34);
	ri_hll("StopSoundWithoutSystemSound", 0, NULL);
	int s = heap_alloc_string(make_string("SaveFolder", 10));
	bu_call("CrayfishLogViewer", "SetSaveFolderName", 1, 0, 1, (union vm_value[]){ BU_I(s) });
	heap_unref(s);
	fflush(stderr);
	dup2(saved, STDERR_FILENO);
	close(saved);
	char buf[4096] = { 0 };
	rewind(capture);
	size_t len = fread(buf, 1, sizeof buf - 1, capture);
	fclose(capture);
	buf[len] = 0;
	RX_CHECK(!strcasestr(buf, "unimplemented") && !strstr(buf, "UNIMPL"), "stderr: %s", buf);
	printf("RX7 checked\n");
}

/* RX8 */
static void rx_clip(void)
{
	ts_render_setup();
	struct rx_cam cam;
	rx_cam_init(&cam, TS_VIEW_W, TS_VIEW_H);
	const int P = 9771, C = 9772;
	parts_get(P);
	ri_pos(P, 30, 40);
	BU_PE("SetComponentClipArea", BU_I(P), BU_I(0), BU_I(0), BU_I(36), BU_I(50));
	BU_PE("SetComponentEnableClipArea", BU_I(P), BU_I(1));
	rx_tex(C, 60, 40, RX_WHITE, RX_WHITE);
	ri_parent(C, P);
	ri_origin(C, 5);
	struct rx_level lc = RX_LEVEL(18, 25); lc.ay = 40;
	rx_level_apply(C, &lc);
	PE_UpdateComponent(0);
	struct rx_level chain[2] = { RX_LEVEL(30, 40), lc };
	double q[4][2];
	rx_model_box(&cam, chain, 2, -30, -20, 60, 40, q);
	uint8_t *px = ts_draw(parts_get(C));
	int outside = 0, missing = 0, inside_drawn = 0;
	for (int y = 0; y < TS_VIEW_H; y++) {
		for (int x = 0; x < TS_VIEW_W; x++) {
			bool a = px[(y * TS_VIEW_W + x) * 4 + 3] != 0;
			bool in_clip = x >= 30 && x < 66 && y >= 40 && y < 90;
			if (a && !in_clip)
				outside++;
			if (in_clip && rx_inside(q, x + 0.5, y + 0.5) > 0.75 && !a)
				missing++;
			inside_drawn += a && in_clip;
		}
	}
	free(px);
	RX_CHECK(outside == 0 && missing == 0 && inside_drawn > 0,
		 "clip: %d pixel(s) drawn outside the clip area, %d of the model's box inside it not drawn (%d drawn)",
		 outside, missing, inside_drawn);
	printf("RX8 checked\n");
}

/* RX9: the back of a card (the battle result's caption: a child turned 180
 * degrees in Y under a parent that spins in Y). */
static void rx_card(void)
{
	ts_render_setup();
	struct rx_cam cam;
	rx_cam_init(&cam, TS_VIEW_W, TS_VIEW_H);
	const SDL_Color red = { 255, 0, 0, 255 }, blue = { 0, 0, 255, 255 };
	const SDL_Color green = { 0, 255, 0, 255 }, white = { 255, 255, 255, 255 };
	const int P = 9781, F = 9782, B = 9783;
	parts_get(P);
	struct rx_level lp = RX_LEVEL(48, 64);
	rx_level_apply(P, &lp);
	rx_tex(F, 40, 24, red, blue);
	ri_parent(F, P); ri_origin(F, 5);
	struct rx_level lf = RX_LEVEL(0, 0);
	rx_level_apply(F, &lf);
	rx_tex(B, 40, 24, green, white);
	ri_parent(B, P); ri_origin(B, 5);
	struct rx_level lb = RX_LEVEL(0, 0); lb.ay = 180;
	rx_level_apply(B, &lb);
	ri_rect(P, 1, 1);
	double q[4][2];
	/* The parent at rest: the front only. */
	PE_UpdateComponent(0);
	struct rx_level cf[2] = { lp, lf }, cb[2] = { lp, lb };
	rx_model_box(&cam, cf, 2, -20, -12, 40, 24, q);
	rx_expect_drawn("parent at 0: the front", F, &cam, q);
	RX_CHECK(rx_drawn_count(B) == 0, "parent at 0: %d pixel(s) of the back drawn", rx_drawn_count(B));
	/* Half a turn: the back only, reading the right way (360 degrees). */
	lp.ay = 180; cf[0] = lp; cb[0] = lp;
	rx_level_apply(P, &lp);
	PE_UpdateComponent(0);
	RX_CHECK(rx_drawn_count(F) == 0, "parent at 180: %d pixel(s) of the front drawn", rx_drawn_count(F));
	rx_model_box(&cam, cb, 2, -20, -12, 40, 24, q);
	rx_expect_drawn("parent at 180: the back", B, &cam, q);
	rx_expect_colour("parent at 180: the back's left half on the left", B, &cam, cb, 2, -10, 0, 0, 255, 0);
	RX_CHECK(q[0][0] < q[1][0], "parent at 180: the back's left edge (%.1f) is not left of its right edge (%.1f)", q[0][0], q[1][0]);
	/* The hit test does not look at the side (0x57aa60). */
	RX_CHECK(ri_in(F, 48, 64) == 1, "parent at 180: the culled front is not under the cursor at its centre");
	/* 100 degrees: the front turned away, the back toward the camera. */
	lp.ay = 100; cf[0] = lp; cb[0] = lp;
	rx_level_apply(P, &lp);
	PE_UpdateComponent(0);
	RX_CHECK(rx_drawn_count(F) == 0, "parent at 100: %d pixel(s) of the front drawn", rx_drawn_count(F));
	rx_model_box(&cam, cb, 2, -20, -12, 40, 24, q);
	rx_expect_drawn("parent at 100: the back", B, &cam, q);
	/* A flipped front is still a front: ReverseLR at 30 degrees is drawn
	 * (mirrored), at 150 it is not. */
	lp.ay = 30; lf.lr = true; cf[0] = lp; cf[1] = lf;
	rx_level_apply(P, &lp);
	rx_level_apply(F, &lf);
	PE_UpdateComponent(0);
	rx_model_box(&cam, cf, 2, -20, -12, 40, 24, q);
	rx_expect_drawn("flipped front at 30", F, &cam, q);
	rx_expect_colour("flipped front at 30: red on the right", F, &cam, cf, 2, -10, 0, 255, 0, 0);
	lp.ay = 150;
	rx_level_apply(P, &lp);
	PE_UpdateComponent(0);
	RX_CHECK(rx_drawn_count(F) == 0, "flipped front at 150: %d pixel(s) drawn", rx_drawn_count(F));
	/* A negative scale is a flip as well. */
	lp.ay = 30; lf.lr = false; lf.mx = -1;
	rx_level_apply(P, &lp);
	rx_level_apply(F, &lf);
	PE_UpdateComponent(0);
	cf[0] = lp; cf[1] = lf;
	rx_model_box(&cam, cf, 2, -20, -12, 40, 24, q);
	rx_expect_drawn("scale x -1 at 30", F, &cam, q);
	printf("RX9 checked\n");
}

static int rx_run(const char *name, void (*fn)(void))
{
	fflush(stdout);
	pid_t pid = fork();
	if (pid == 0) {
		rx_failures = 0;
		alarm(60);
		fn();
		fflush(stdout);
		_exit(rx_failures ? 1 : 0);
	}
	int status = 0;
	waitpid(pid, &status, 0);
	bool ok = WIFEXITED(status) && WEXITSTATUS(status) == 0;
	printf("%s %s%s\n", name, ok ? "PASS" : "FAIL", WIFSIGNALED(status) ? " (crashed)" : "");
	fflush(stdout);
	return ok ? 0 : 1;
}

static int test_parts_rotate_xy(void)
{
	int failed = 0, env = 0, n = 0;
	static const struct { const char *name; void (*fn)(void); bool gl; } cases[] = {
		{ "RX1 Set/GetComponentRotateX and Y read back", rx_getters, false },
		{ "RX2 drawn through the camera", rx_drawn, true },
		{ "RX3 Parts_IsCursorIn on the projected box", rx_hit, false },
		{ "RX3p the pixel hit test through the projection", rx_hit_pixels, true },
		{ "RX4 the box corners against the model", rx_corners, false },
		{ "RX5 PE_Save / PE_Load keep the angles", rx_save, true },
		{ "RX6 guard: AIN version 13", rx_v13, false },
		{ "RX7 no unimplemented warnings", rx_quiet, false },
		{ "RX8 a clip area cuts a turned child on the screen", rx_clip, true },
		{ "RX9 the back of a card is not drawn", rx_card, true },
	};
	for (unsigned i = 0; i < sizeof cases / sizeof cases[0]; i++, n++) {
		if (cases[i].gl) {
			int r = cn_fork_case(cases[i].name, cases[i].fn, &rx_failures);
			printf("%s %s\n", cases[i].name, r == 0 ? "PASS" : r == 1 ? "FAIL" : "ENVIRONMENT FAILURE");
			failed += r == 1;
			env += r == 2;
		} else {
			failed += rx_run(cases[i].name, cases[i].fn);
		}
	}
	if (env) {
		printf("PARTS-ROTATE-XY ENVIRONMENT FAILURE: %d case(s) could not run, %d of %d cases failed\n", env, failed, n);
		return 2;
	}
	printf("PARTS-ROTATE-XY %s: %d of %d cases failed\n", failed ? "FAIL" : "PASS", failed, n);
	return failed ? 92 : 0;
}
