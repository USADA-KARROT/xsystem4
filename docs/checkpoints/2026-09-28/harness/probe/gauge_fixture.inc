/* gauge: v14 typed HGauge/VGauge states, raw values and fill geometry.
 * Synthetic pactex and the real AIN HLL/wrapper declarations are used;
 * no CG assets, GL context or files are needed. New geometry helpers are
 * resolved by dlsym so the unfixed source remains buildable by before-check.
 */
#include <dlfcn.h>
#include <math.h>

static int hg_failures;
#define HG_CHECK(c, ...) do { if (!(c)) { printf("  FAIL: "); printf(__VA_ARGS__); printf("\n"); hg_failures++; } } while (0)

static const char *hg_rate_fn[] = { "Parts_SetHGaugeRate", "Parts_SetVGaugeRate" };
static const char *hg_num_fn[] = { "Parts_GetHGaugeNumerator", "Parts_GetVGaugeNumerator" };
static const char *hg_den_fn[] = { "Parts_GetHGaugeDenominator", "Parts_GetVGaugeDenominator" };
static const char *hg_rev_set_fn[] = { "Parts_SetHGaugeReverse", "Parts_SetVGaugeReverse" };
static const char *hg_rev_get_fn[] = { "Parts_IsHGaugeReverse", "Parts_IsVGaugeReverse" };
static const char *hg_area_set_fn[] = { "Parts_SetHGaugeSurfaceArea", "Parts_SetVGaugeSurfaceArea" };
static const char *hg_area_get_fn[] = { "GetHGaugeSurfaceArea", "GetVGaugeSurfaceArea" };
static const char *hg_cg_set_fn[] = { "Parts_SetHGaugeCG", "Parts_SetVGaugeCG" };
static const char *hg_cg_get_fn[] = { "Parts_GetHGaugeCG", "Parts_GetVGaugeCG" };

static void hg_rate(int n, int s, bool vertical, float num, float den)
{
	int ok = BU_PE(hg_rate_fn[vertical], BU_I(n), BU_F(num), BU_F(den), BU_I(s)).i;
	HG_CHECK(ok, "%s(%d,%g,%g,%d) returned false", hg_rate_fn[vertical], n, num, den, s);
}

static void hg_values(const char *where, int n, int s, bool vertical, float num, float den)
{
	float a = BU_PE(hg_num_fn[vertical], BU_I(n), BU_I(s)).f;
	float b = BU_PE(hg_den_fn[vertical], BU_I(n), BU_I(s)).f;
	HG_CHECK(a == num && b == den, "%s raw values=(%g,%g), want (%g,%g)", where, a, b, num, den);
}

static void hg_reverse(int n, int s, bool vertical, bool reverse)
{
	BU_PE(hg_rev_set_fn[vertical], BU_I(n), BU_I(reverse), BU_I(s));
}

static void hg_area(int n, int s, bool vertical, int x, int y, int w, int h)
{
	int ok = BU_PE(hg_area_set_fn[vertical], BU_I(n), BU_I(x), BU_I(y), BU_I(w), BU_I(h), BU_I(s)).i;
	HG_CHECK(ok, "%s returned false", hg_area_set_fn[vertical]);
}

static void hg_expect_area(const char *where, int n, int s, bool vertical, int x, int y, int w, int h)
{
	int page = heap_alloc_page(alloc_page(ARRAY_PAGE, AIN_ARRAY_INT, 4));
	for (int i = 0; i < 4; i++)
		heap[page].page->values[i].i = -987654;
	bu_call("PartsEngine", hg_area_get_fn[vertical], 6, 0, 10,
		(union vm_value[]){ BU_I(n), BU_I(page), BU_I(0), BU_I(page), BU_I(1),
			BU_I(page), BU_I(2), BU_I(page), BU_I(3), BU_I(s) });
	union vm_value *v = heap[page].page->values;
	HG_CHECK(v[0].i == x && v[1].i == y && v[2].i == w && v[3].i == h,
		"%s surface=(%d,%d,%d,%d), want (%d,%d,%d,%d)", where,
		v[0].i, v[1].i, v[2].i, v[3].i, x, y, w, h);
	heap_unref(page);
}

static void hg_expect_empty_cg(const char *where, int n, int s, bool vertical)
{
	int str = BU_PE(hg_cg_get_fn[vertical], BU_I(n), BU_I(s)).i;
	HG_CHECK(!strcmp(bu_str(str), ""), "%s CG name is not empty", where);
	if (str > 0)
		heap_unref(str);
}

#define HG_H_TYPE "\x99\x4d\xa5\xb2\xa9\x60\xa5\xb8\xb2\xbf\xbc\xfe"
#define HG_V_TYPE "\xd8\x51\xa5\xb2\xa9\x60\xa5\xb8\xb2\xbf\xbc\xfe"
#define HG_NUM "\xb7\xd6\xd7\xd3"
#define HG_DEN "\xb7\xd6\xc4\xb8"
#define HG_REV "\xb7\xb4\xde\x44"
#define HG_SURFACE "\xa5\xb5\xa9\x60\xa5\xd5\xa5\xa7\xa5\xa4\xa5\xb9\xa5\xa8\xa5\xea\xa5\xa2"
#define HG_DOWN "\xa5\xad\xa9\x60\xa5\xc0\xa5\xa6\xa5\xf3\xa0\xee\x91\x42"

static struct ex_tree hg_surface_leaf(const char *key, int x, int y, int w, int h)
{
	struct ex_list *l = xcalloc(1, sizeof(*l));
	l->nr_items = 4;
	l->items = xcalloc(4, sizeof(*l->items));
	int values[] = { x, y, w, h };
	for (int i = 0; i < 4; i++)
		l->items[i].value = (struct ex_value){ .type = EX_INT, .i = values[i] };
	return bu_leaf(key, (struct ex_value){ .type = EX_LIST, .list = l });
}

static struct ex_tree hg_state(const char *name, bool vertical, bool values)
{
	struct ex_tree t = bu_branch(name, values ? 5 : 1);
	t.children[0] = bu_sleaf(BU_TYPE, vertical ? HG_V_TYPE : HG_H_TYPE);
	if (values) {
		t.children[1] = bu_leaf(HG_NUM, (struct ex_value){ .type = EX_FLOAT, .f = vertical ? 12.5f : 37.5f });
		t.children[2] = bu_leaf(HG_DEN, (struct ex_value){ .type = EX_FLOAT, .f = vertical ? 50.0f : 150.0f });
		t.children[3] = bu_ileaf(HG_REV, !vertical);
		t.children[4] = hg_surface_leaf(HG_SURFACE, vertical ? 3 : 7, vertical ? 5 : 9,
			vertical ? 30 : 60, vertical ? 70 : 40);
	}
	return t;
}

static void hg_loader(void)
{
	bu_postlink();
	struct ex *e = xcalloc(1, sizeof(*e));
	e->nr_blocks = 1;
	e->blocks = xcalloc(1, sizeof(*e->blocks));
	e->blocks[0].name = cstr_to_string("GaugeActivity");
	e->blocks[0].val.type = EX_TREE;
	struct ex_tree *top = xcalloc(1, sizeof(*top));
	*top = bu_branch("GaugeActivity", 1);
	e->blocks[0].val.tree = top;
	top->children[0] = bu_comp("GaugeRoot", BU_LAYOUT, 0, 3);
	struct ex_tree *kids = &top->children[0].children[1];
	kids->children[0] = bu_comp("Mixed", BU_LOW, 4, 0);
	struct ex_tree *info = &kids->children[0].children[0];
	/* Named states intentionally disagree with the legacy ordinal order.
	 * An unrelated surface also collides with a recognized named state. */
	info->children[1] = bu_branch(HG_DOWN, 1);
	info->children[1].children[0] = bu_sleaf(BU_TYPE, BU_CG);
	info->children[2] = bu_branch("UnrelatedSurface", 1);
	info->children[2].children[0] = hg_surface_leaf(HG_SURFACE, 1, 2, 3, 4);
	info->children[3] = hg_state(BU_NORMAL, false, true);
	info->children[4] = hg_state(BU_ONCURSOR, true, true);
	kids->children[1] = bu_comp("Defaults", BU_LOW, 2, 0);
	kids->children[1].children[0].children[1] = hg_state(BU_NORMAL, false, false);
	kids->children[1].children[0].children[2] = hg_state(BU_ONCURSOR, true, false);
	kids->children[2] = bu_comp("Unknown", "UnknownGaugeShape", 0, 0);
	struct string *name = cstr_to_string("GaugeFixture");
	HG_CHECK(probe_load_activity_tree(name, e), "synthetic gauge activity failed to load");
	free_string(name);
	ex_free(e);
	int act = sf_str("GaugeFixture", -1), mixed = bu_number(act, "Mixed"), defaults = bu_number(act, "Defaults");
	int unknown = bu_number(act, "Unknown");
	HG_CHECK(mixed > 0 && defaults > 0 && unknown > 0, "named synthetic gauge components missing");
	if (mixed <= 0 || defaults <= 0 || unknown <= 0) {
		heap_unref(act);
		return;
	}
	struct parts *p = parts_try_get(mixed);
	int labels[] = { 22, 23, 19 };
	enum parts_type types[] = { PARTS_HGAUGE, PARTS_VGAUGE, PARTS_CG };
	for (int i = 0; i < 3; i++) {
		int got = BU_PE("GetComponentType", BU_I(mixed), BU_I(i + 1)).i;
		HG_CHECK(got == labels[i] && p->states[i].type == types[i],
			"mixed state %d: public/internal type %d/%d, want %d/%d", i + 1,
			got, p->states[i].type, labels[i], types[i]);
	}
	/* Inspect storage before getter dispatch could create/coerce a state. */
	HG_CHECK(p->states[0].common.surface_area.x == 7 && p->states[0].common.surface_area.w == 60
		&& p->states[1].common.surface_area.y == 5 && p->states[1].common.surface_area.h == 70,
		"typed loader failed to retain each gauge's surface");
	hg_values("loaded horizontal", mixed, 1, false, 37.5f, 150.0f);
	hg_values("loaded vertical", mixed, 2, true, 12.5f, 50.0f);
	HG_CHECK(BU_PE(hg_rev_get_fn[0], BU_I(mixed), BU_I(1)).i == 1
		&& BU_PE(hg_rev_get_fn[1], BU_I(mixed), BU_I(2)).i == 0, "loaded reverse flags differ");
	hg_expect_area("loaded H", mixed, 1, false, 7, 9, 60, 40);
	hg_expect_area("loaded V", mixed, 2, true, 3, 5, 30, 70);
	hg_values("loader H missing values", defaults, 1, false, 0, 0);
	hg_values("loader V missing values", defaults, 2, true, 0, 0);
	HG_CHECK(BU_PE("GetComponentType", BU_I(unknown), BU_I(1)).i == 0,
		"unknown widget fallback changed");
	heap_unref(act);
	printf("  HG1 named mixed states build real gauges with raw pactex values and surfaces\n");
}

static void hg_type_and_hll(void)
{
	bu_postlink();
	const int n = 18601;
	struct parts *p = parts_get(n);
	BU_PE("SetComponentType", BU_I(n), BU_I(18), BU_I(1));
	BU_PE("SetComponentType", BU_I(n), BU_I(19), BU_I(3));
	for (int v = 0; v < 2; v++) {
		int s = v + 1;
		BU_PE("SetComponentType", BU_I(n), BU_I(22 + v), BU_I(s));
		HG_CHECK(p->states[v].type == (v ? PARTS_VGAUGE : PARTS_HGAUGE),
			"SetComponentType(%d) only changed a label", 22 + v);
		hg_values("native constructor", n, s, v, 100, 100);
		HG_CHECK(BU_PE(hg_rev_get_fn[v], BU_I(n), BU_I(s)).i == 0, "constructor reverse is true");
		hg_expect_empty_cg("constructor", n, s, v);
		hg_rate(n, s, v, 37.5f + v, 150.0f + v);
		hg_values("texture-free SetRate", n, s, v, 37.5f + v, 150.0f + v);
		hg_reverse(n, s, v, true);
		HG_CHECK(BU_PE(hg_rev_get_fn[v], BU_I(n), BU_I(s)).i == 1, "SetReverse(true) was not retained");
		hg_area(n, s, v, -7, 9, -13, 40);
		hg_expect_area("raw negative surface", n, s, v, -7, 9, -13, 40);
		int empty = sf_str("", -1);
		HG_CHECK(BU_PE(hg_cg_set_fn[v], BU_I(n), BU_I(empty), BU_I(s)).i == 1,
			"setting the already-empty CG name must succeed without loading an asset");
		heap_unref(empty);
		hg_expect_empty_cg("same-name setter", n, s, v);
		hg_values("same CG preserves raw values", n, s, v, 37.5f + v, 150.0f + v);
		HG_CHECK(BU_PE(hg_rev_get_fn[v], BU_I(n), BU_I(s)).i == 1,
			"same CG setter lost the reverse flag");
		hg_expect_area("same CG preserves surface", n, s, v, -7, 9, -13, 40);
		hg_rate(n, s, v, -3.25f, 0.0f);
		hg_values("unclamped numerator and zero denominator", n, s, v, -3.25f, 0);
		hg_reverse(n, s, v, false);
		HG_CHECK(BU_PE(hg_rev_get_fn[v], BU_I(n), BU_I(s)).i == 0, "SetReverse(false) was not retained");
	}
	HG_CHECK(p->states[2].type == PARTS_CG
		&& BU_PE("GetComponentType", BU_I(n), BU_I(3)).i == 19, "gauge setters changed the unrelated CG state");
	hg_values("H unaffected by V setters", n, 1, false, -3.25f, 0);
	/* Native getters coerce an existing state's low-level implementation,
	 * while a missing component must not be created just to answer a query. */
	hg_values("H getter coerces an existing CG state", n, 3, false, 100, 100);
	HG_CHECK(p->states[2].type == PARTS_HGAUGE
		&& BU_PE("GetComponentType", BU_I(n), BU_I(3)).i == 22,
		"H getter did not create the proper low-level state");
	hg_values("V getter coerces an existing H state", n, 3, true, 100, 100);
	HG_CHECK(p->states[2].type == PARTS_VGAUGE
		&& BU_PE("GetComponentType", BU_I(n), BU_I(3)).i == 23,
		"V getter did not create the proper low-level state");
	hg_values("coercion leaves another state unchanged", n, 1, false, -3.25f, 0);
	const int missing = 18609;
	HG_CHECK(!parts_try_get(missing), "missing-number fixture already exists");
	for (int v = 0; v < 2; v++) {
		hg_values("missing component", missing, 1, v, 0, 0);
		hg_expect_empty_cg("missing component", missing, 1, v);
		HG_CHECK(BU_PE(hg_rev_get_fn[v], BU_I(missing), BU_I(1)).i == 0,
			"missing component reverse is not false");
		hg_expect_area("missing component preserves out values", missing, 1, v,
			-987654, -987654, -987654, -987654);
	}
	HG_CHECK(!parts_try_get(missing), "a gauge getter created the missing component");
	printf("  HG2 type construction, float raw HLL values, reverse and wrap<int> surface outputs checked\n");
}

typedef void (*hg_fill_fn)(struct parts_gauge *, bool, Rectangle *);
typedef void (*hg_surface_fn)(struct parts_gauge *, Rectangle *);
typedef void (*hg_render_geometry_fn)(struct parts *, struct parts_gauge *, bool, mat4, Rectangle *);

static void hg_rect(const char *where, Rectangle r, int x, int y, int w, int h)
{
	HG_CHECK(r.x == x && r.y == y && r.w == w && r.h == h,
		"%s rect=(%d,%d,%d,%d), want (%d,%d,%d,%d)", where, r.x, r.y, r.w, r.h, x, y, w, h);
}

static void hg_render_quad(hg_render_geometry_fn render_geometry, struct parts *p,
		struct parts_gauge *g, bool vertical, bool reverse, int tx, int ty, float sx, float sy)
{
	PE_SetPos(p->no, tx, ty);
	PE_SetPartsMagX(p->no, sx);
	PE_SetPartsMagY(p->no, sy);
	PE_UpdateComponent(0);
	mat4 matrix;
	Rectangle source;
	render_geometry(p, g, vertical, matrix, &source);
	int x = !vertical && reverse ? 30 : 0, y = vertical && !reverse ? 20 : 0;
	int w = vertical ? 60 : 30, h = vertical ? 20 : 40;
	hg_rect("render absolute CG crop", source, 7 + x, 9 + y, w, h);
	HG_CHECK(g->common.origin_offset.x == 0 && g->common.origin_offset.y == 0,
		"top-left origin fixture unexpectedly changed");
	const int corners[4][2] = { {0, 0}, {1, 0}, {0, 1}, {1, 1} };
	for (int i = 0; i < 4; i++) {
		/* The shader discards whole-image UVs outside the source rectangle;
		 * it does not renormalize that rectangle to UV [0,1]. Transforming
		 * those exact UV corners must yield the requested half-sized region.
		 * Shrinking the quad AND cropping UVs would shrink it a second time. */
		float u = (source.x + corners[i][0] * source.w) / (float)g->cg.w;
		float v = (source.y + corners[i][1] * source.h) / (float)g->cg.h;
		float got_x = matrix[0][0] * u + matrix[1][0] * v + matrix[3][0];
		float got_y = matrix[0][1] * u + matrix[1][1] * v + matrix[3][1];
		float want_x = tx + sx * (x + corners[i][0] * w);
		float want_y = ty + sy * (y + corners[i][1] * h);
		HG_CHECK(isfinite(got_x) && isfinite(got_y)
			&& fabsf(got_x - want_x) < 0.0001f && fabsf(got_y - want_y) < 0.0001f,
			"%s reverse=%d render corner %d: (%g,%g), want (%g,%g) at scale (%g,%g)",
			vertical ? "V" : "H", reverse, i, got_x, got_y, want_x, want_y, sx, sy);
	}
}

static void hg_geometry(void)
{
	bu_postlink();
	hg_fill_fn fill = (hg_fill_fn)dlsym(RTLD_DEFAULT, "parts_v14_gauge_fill_rect");
	hg_surface_fn surface = (hg_surface_fn)dlsym(RTLD_DEFAULT, "parts_v14_gauge_surface");
	hg_render_geometry_fn render_geometry = (hg_render_geometry_fn)dlsym(RTLD_DEFAULT, "parts_v14_gauge_render_geometry");
	HG_CHECK(fill && surface && render_geometry, "v14 gauge geometry helpers unavailable");
	if (!fill || !surface || !render_geometry)
		return;
	for (int v = 0; v < 2; v++) {
		const int n = 18610 + v;
		hg_rate(n, 1, v, 1, 2);
		struct parts_gauge *g = &parts_get(n)->states[0].gauge;
		/* Dimensions only: all texture handles stay zero. */
		g->cg.w = 100; g->cg.h = 80;
		hg_area(n, 1, v, 7, 9, 60, 40);
		Rectangle r;
		surface(g, &r);
		hg_rect("effective source surface", r, 7, 9, 60, 40);
		for (int reverse = 0; reverse < 2; reverse++) {
			hg_reverse(n, 1, v, reverse);
			hg_rate(n, 1, v, 1, 2);
			fill(g, v, &r);
			hg_rect("half fill relative to surface", r, !v && reverse ? 30 : 0,
				v && !reverse ? 20 : 0, v ? 60 : 30, v ? 20 : 40);
			hg_render_quad(render_geometry, parts_get(n), g, v, reverse, 0, 0, 1, 1);
			hg_render_quad(render_geometry, parts_get(n), g, v, reverse, 100, 200, 2, 3);
			hg_rate(n, 1, v, 1, 3);
			fill(g, v, &r);
			hg_rect("fraction truncation", r, !v && reverse ? 40 : 0,
				v && !reverse ? 27 : 0, v ? 60 : 20, v ? 13 : 40);
			hg_rate(n, 1, v, 150, 100);
			fill(g, v, &r);
			hg_rect("overshoot displays full", r, 0, 0, 60, 40);
			hg_values("display clamp preserves raw values", n, 1, v, 150, 100);
			hg_rate(n, 1, v, -1, 2);
			fill(g, v, &r);
			HG_CHECK(r.w == 0 || r.h == 0, "negative numerator is not empty");
			hg_rate(n, 1, v, 0, 0);
			fill(g, v, &r);
			hg_rect("zero denominator displays full", r, 0, 0, 60, 40);
			hg_rate(n, 1, v, 1, -1);
			fill(g, v, &r);
			hg_rect("negative denominator displays full", r, 0, 0, 60, 40);
		}
		hg_area(n, 1, v, 0, 0, 0, 0);
		surface(g, &r);
		hg_rect("zero surface uses complete CG", r, 0, 0, 100, 80);
		hg_area(n, 1, v, 7, 9, 0, 40);
		surface(g, &r);
		HG_CHECK(r.w == 0, "a single zero surface axis incorrectly expands to full CG");
	}
	printf("  HG3 effective surface, fill direction, clamp and full-quad UV geometry checked without GL\n");
}

static void hg_ain_wrappers(void)
{
	bu_postlink();
	const int n = 18621;
	hg_rate(n, 1, false, 0, 1);
	int st = ain_get_struct(ain, "parts::detail::CHGaugeParts");
	int init = ain_get_function(ain, "parts::detail::CHGaugeParts@2");
	int set_num = ain_get_function(ain, "parts::detail::CHGaugeParts@Numerator::set");
	int set_den = ain_get_function(ain, "parts::detail::CHGaugeParts@Denominator::set");
	HG_CHECK(st >= 0 && init >= 0 && set_num >= 0 && set_den >= 0, "real CHGaugeParts declarations missing");
	if (st < 0 || init < 0 || set_num < 0 || set_den < 0)
		return;
	int object = alloc_struct(st);
	call_method0(init, object);
	heap[object].page->values[br_member(st, "number")].i = n;
	heap[object].page->values[br_member(st, "state")].i = 1;
	int sp = stack_ptr;
	union vm_value num = BU_F(0.375f), den = BU_F(4.0f);
	call_method_args(set_num, object, 1, &num.i);
	hg_values("true Numerator::set virtual dispatch", n, 1, false, 0.375f, 1);
	call_method_args(set_den, object, 1, &den.i);
	hg_values("true Denominator::set virtual dispatch", n, 1, false, 0.375f, 4);
	HG_CHECK(stack_ptr == sp, "real gauge setter wrappers grew the VM stack");
	heap_unref(object);
	printf("  HG4 real CHGaugeParts virtual Numerator/Denominator setters preserve the other value\n");
}

static void hg_persistence(void)
{
	bu_postlink();
	const int n = 18631;
	hg_rate(n, 1, false, 37.5f, 150.0f);
	hg_rate(n, 2, true, 12.5f, 50.0f);
	hg_reverse(n, 1, false, true);
	hg_reverse(n, 2, true, false);
	hg_area(n, 1, false, 7, 9, 60, 40);
	hg_area(n, 2, true, 3, 5, 30, 70);
	struct page *saved = NULL;
	HG_CHECK(PE_Save(&saved), "v14 PE_Save failed");
	if (!saved)
		return;
	HG_CHECK(ca_save_version(saved) == 6, "v14 gauge save is not XPE v6");
	hg_rate(n, 1, false, 9, 8);
	hg_reverse(n, 1, false, false);
	HG_CHECK(PE_Load(&saved), "v14 PE_Load failed");
	hg_values("v5 reloaded H", n, 1, false, 37.5f, 150);
	hg_values("v5 reloaded V", n, 2, true, 12.5f, 50);
	hg_expect_area("v5 reloaded H", n, 1, false, 7, 9, 60, 40);
	hg_expect_area("v5 reloaded V", n, 2, true, 3, 5, 30, 70);
	HG_CHECK(BU_PE(hg_rev_get_fn[0], BU_I(n), BU_I(1)).i == 1
		&& BU_PE(hg_rev_get_fn[1], BU_I(n), BU_I(2)).i == 0, "v5 reverse flags not retained");
	hg_expect_empty_cg("v5 H", n, 1, false);
	hg_expect_empty_cg("v5 V", n, 2, true);
	ca_free_buffer(saved);
	printf("  HG5 in-memory v5 retains H/V raw values, reverse, surface and empty CG names\n");
}

/* A one-part, zero-font, zero-motion v3 fixture becomes a genuine old v4
 * page by adding its five ClipArea words before controller/motion fields.
 * Gauge state payloads remain the old cg_no/rate pair in both versions. */
static struct page *hg_legacy_v4(struct page *v3)
{
	struct iarray_reader r;
	if (!iarray_init_reader(&r, v3, "XPE")) {
		HG_CHECK(false, "cannot parse generated v3 header");
		return NULL;
	}
	unsigned version_pos = r.pos;
	int version = iarray_read(&r), fonts = iarray_read(&r);
	if (parts_multi_controller) {
		iarray_read(&r);
		iarray_read(&r);
	}
	int count = iarray_read(&r);
	if (version != 3 || fonts || count != 1 || v3->values[v3->nr_vars - 1].i) {
		HG_CHECK(false, "old-format fixture shape changed: version=%d fonts=%d parts=%d", version, fonts, count);
		return NULL;
	}
	int insert = v3->nr_vars - (parts_multi_controller ? 9 : 1);
	struct page *v4 = alloc_page(ARRAY_PAGE, AIN_ARRAY_INT, v3->nr_vars + 5);
	v4->array = v3->array;
	for (int i = 0; i < insert; i++)
		v4->values[i] = v3->values[i];
	int clip[] = { 1, 4, 5, 60, 70 };
	for (int i = 0; i < 5; i++)
		v4->values[insert + i].i = clip[i];
	for (int i = insert; i < v3->nr_vars; i++)
		v4->values[i + 5] = v3->values[i];
	v4->values[version_pos].i = 4;
	return v4;
}

static void hg_legacy(void)
{
	bu_postlink();
	const int n = 18641;
	int version = ain->version;
	ain->version = 13;
	PE_SetComponentType(n, 14, 1);
	PE_SetComponentType(n, 15, 2);
	struct parts *p = parts_get(n);
	HG_CHECK(p->states[0].type == PARTS_HGAUGE && p->states[1].type == PARTS_VGAUGE
		&& PE_GetComponentType(n, 1) == 14 && PE_GetComponentType(n, 2) == 15,
		"v13 gauge type numbers changed");
	HG_CHECK(p->states[0].gauge.rate == 0 && p->states[1].gauge.rate == 0,
		"v13 texture-free defaults changed");
	PE_SetHGaugeRate(n, 1, 2, 1);
	PE_SetVGaugeRate(n, 1, 2, 2);
	HG_CHECK(p->states[0].gauge.rate == 0 && p->states[1].gauge.rate == 0,
		"v13 texture-free setter behavior changed");
	p->states[0].gauge.rate = 0.25f;
	p->states[1].gauge.rate = 0.75f;
	struct page *v3 = NULL;
	HG_CHECK(PE_Save(&v3), "legacy v3 PE_Save failed");
	HG_CHECK(v3 && ca_save_version(v3) == 3, "legacy writer no longer emits v3");
	ain->version = version;
	if (v3) {
		struct page *v4 = hg_legacy_v4(v3);
		HG_CHECK(PE_Load(&v3), "v14 rejected a legacy v3 gauge page");
		/* Gauge getters may coerce a state and repair its type metadata.
		 * Query the ordinary type API first so migration must be complete
		 * at load time, before any such side effect can hide a missing type. */
		HG_CHECK(BU_PE("GetComponentType", BU_I(n), BU_I(1)).i == 22
			&& BU_PE("GetComponentType", BU_I(n), BU_I(2)).i == 23,
			"v3 load did not restore H/V component types before gauge getters");
		hg_values("v3 H ratio migration", n, 1, false, 0.25f, 1);
		hg_values("v3 V ratio migration", n, 2, true, 0.75f, 1);
		HG_CHECK(BU_PE(hg_rev_get_fn[0], BU_I(n), BU_I(1)).i == 0
			&& BU_PE(hg_rev_get_fn[1], BU_I(n), BU_I(2)).i == 0, "v3 migration invents reverse flags");
		hg_expect_empty_cg("v3 H", n, 1, false);
		if (v4) {
			HG_CHECK(PE_Load(&v4), "v14 rejected a legacy v4 gauge page");
			HG_CHECK(BU_PE("GetComponentType", BU_I(n), BU_I(1)).i == 22
				&& BU_PE("GetComponentType", BU_I(n), BU_I(2)).i == 23,
				"v4 load did not restore H/V component types before gauge getters");
			hg_values("v4 H ratio migration", n, 1, false, 0.25f, 1);
			hg_values("v4 V ratio migration", n, 2, true, 0.75f, 1);
			HG_CHECK(BU_PE("IsComponentEnableClipArea", BU_I(n)).i == 1
				&& BU_PE("GetComponentClipAreaPosX", BU_I(n)).i == 4
				&& BU_PE("GetComponentClipAreaPosHeight", BU_I(n)).i == 70,
				"v4 gauge payload misaligned the following ClipArea data");
			ca_free_buffer(v4);
		}
		ca_free_buffer(v3);
	}
	printf("  HG6 v13 type/default/setter behavior and legacy v3/v4 ratio migration checked\n");
}

static int test_gauge(void)
{
	void (*cases[])(void) = { hg_loader, hg_type_and_hll, hg_geometry, hg_ain_wrappers,
		hg_persistence, hg_legacy };
	const char *names[] = { "HG1", "HG2", "HG3", "HG4", "HG5", "HG6" };
	int failed = 0;
	for (unsigned i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
		fflush(stdout);
		pid_t pid = fork();
		if (pid == 0) {
			alarm(60);
			hg_failures = 0;
			cases[i]();
			fflush(stdout);
			_exit(hg_failures ? 1 : 0);
		}
		int st = 0;
		bool ok = pid > 0 && waitpid(pid, &st, 0) == pid
			&& WIFEXITED(st) && WEXITSTATUS(st) == 0;
		printf("%s %s\n", names[i], ok ? "ok" : "FAIL");
		failed += !ok;
	}
	printf("GAUGE %s: %d of %zu cases failed\n", failed ? "FAIL" : "PASS", failed,
		sizeof(cases) / sizeof(cases[0]));
	return failed ? 1 : 0;
}
