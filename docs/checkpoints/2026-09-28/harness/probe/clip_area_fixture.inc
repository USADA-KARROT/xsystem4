/* clip-area: v14 ClipArea values, synthetic pactex loading, effective geometry
 * and in-memory XPE persistence. All HLL calls use the game's declarations;
 * generated EX trees contain no game data. Each case runs in a child process.
 * Newly added geometry is resolved with dlsym so before-check can build the
 * same fixture against the old engine. No GL context or save files are used.
 */
#include <dlfcn.h>
#include <math.h>
#include "hll/iarray.h"

static int ca_failures;
#define CA_CHECK(c, ...) do { if (!(c)) { printf("  FAIL: "); printf(__VA_ARGS__); printf("\n"); ca_failures++; } } while (0)

static void ca_area(int n, int x, int y, int w, int h)
{
	BU_PE("SetComponentClipArea", BU_I(n), BU_I(x), BU_I(y), BU_I(w), BU_I(h));
}

static void ca_enable(int n, bool enable)
{
	BU_PE("SetComponentEnableClipArea", BU_I(n), BU_I(enable));
}

static int ca_enabled(int n)
{
	return BU_PE("IsComponentEnableClipArea", BU_I(n)).i;
}

static bool ca_expect_area(const char *step, int n, int x, int y, int w, int h)
{
	int got[] = {
		BU_PE("GetComponentClipAreaPosX", BU_I(n)).i,
		BU_PE("GetComponentClipAreaPosY", BU_I(n)).i,
		BU_PE("GetComponentClipAreaPosWidth", BU_I(n)).i,
		BU_PE("GetComponentClipAreaPosHeight", BU_I(n)).i
	};
	bool ok = got[0] == x && got[1] == y && got[2] == w && got[3] == h;
	CA_CHECK(ok, "%s: rect (%d,%d,%d,%d), want (%d,%d,%d,%d)", step,
		got[0], got[1], got[2], got[3], x, y, w, h);
	return ok;
}

static void ca_hll(void)
{
	bu_postlink();
	const int n = 18401, unknown = 18499;
	parts_get(n);
	/* A rectangle is not clamped to the component's box. Before the fix this
	 * first readback fails with (0,0,0,0), before the formerly unbound Is API. */
	ca_area(n, -20, 30, 400, 70);
	if (!ca_expect_area("set/readback", n, -20, 30, 400, 70))
		return;
	CA_CHECK(ca_enabled(n) == 1, "changing a rectangle must enable clipping");
	ca_enable(n, false);
	CA_CHECK(ca_enabled(n) == 0, "disable did not clear the flag");
	ca_expect_area("disabled retains rect", n, -20, 30, 400, 70);
	ca_area(n, -20, 30, 400, 70);
	CA_CHECK(ca_enabled(n) == 0, "setting the same rectangle re-enabled clipping");
	ca_area(n, -20, 30, 401, 70);
	CA_CHECK(ca_enabled(n) == 1, "changing only width did not enable clipping");
	ca_enable(n, false);
	ca_enable(n, true);
	CA_CHECK(ca_enabled(n) == 1, "explicit enable did not set the flag");
	ca_expect_area("explicit enable retains rect", n, -20, 30, 401, 70);
	ca_area(n, 4, -8, 0, -1);
	ca_expect_area("degenerate stored verbatim", n, 4, -8, 0, -1);
	CA_CHECK(ca_enabled(n) == 1, "a changed degenerate rectangle must still enable the flag");

	CA_CHECK(!parts_try_get(unknown), "unknown fixture number already exists");
	ca_area(unknown, 1, 2, 3, 4);
	ca_enable(unknown, true);
	ca_expect_area("unknown", unknown, 0, 0, 0, 0);
	CA_CHECK(ca_enabled(unknown) == 0, "unknown flag must be false");
	CA_CHECK(!parts_try_get(unknown), "ClipArea setter/getter created an unknown component");
	printf("  CA1 true HLL readback, enable transitions and unknown no-create checked\n");
}

#define CA_KEY_GBK "\xa5\xaf\xa5\xea\xa5\xc3\xa5\xd7\xee\x49\xd3\xf2"
#define CA_KEY_SJIS "\x83\x4e\x83\x8a\x83\x62\x83\x76\x97\xcc\x88\xe6"

static struct ex_tree ca_clip_component(const char *name, const char *key,
		int enable, int x, int y, int w, int h)
{
	struct ex_tree t = bu_branch(name, key ? 2 : 1);
	t.children[0] = bu_branch(BU_INFO, 1);
	t.children[0].children[0] = bu_sleaf(BU_TYPE, BU_LOW);
	if (key) {
		int values[] = { enable, x, y, w, h };
		struct ex_list *list = xcalloc(1, sizeof(*list));
		list->nr_items = 5;
		list->items = xcalloc(5, sizeof(*list->items));
		for (int i = 0; i < 5; i++)
			list->items[i].value = (struct ex_value){ .type = EX_INT, .i = values[i] };
		t.children[1] = bu_leaf(key, (struct ex_value){ .type = EX_LIST, .list = list });
	}
	return t;
}

static void ca_loader(void)
{
	bu_postlink();
	const char *names[] = { "GbkOff", "SjisOff", "GbkOn", "SjisOn", "Plain", "Partial" };
	const char *keys[] = { CA_KEY_GBK, CA_KEY_SJIS, CA_KEY_GBK, CA_KEY_SJIS, NULL, CA_KEY_GBK };
	const int enable[] = { 0, 0, 1, 2, 0, 1 };
	const int rects[6][4] = { { -8, 9, 180, 45 }, { 11, -12, 40, 50 },
		{ 4, 0, 414, 108 }, { 320, 0, 320, 180 }, { 0, 0, 0, 0 }, { 0, 0, 100, 0 } };
	struct ex *e = xcalloc(1, sizeof(*e));
	e->nr_blocks = 1;
	e->blocks = xcalloc(1, sizeof(*e->blocks));
	e->blocks[0].name = cstr_to_string("ClipAreaActivity");
	e->blocks[0].val.type = EX_TREE;
	struct ex_tree *top = xcalloc(1, sizeof(*top));
	*top = bu_branch("ClipAreaActivity", 1);
	e->blocks[0].val.tree = top;
	top->children[0] = bu_comp("ClipAreaRoot", BU_LAYOUT, 0, 6);
	struct ex_tree *kids = &top->children[0].children[1];
	for (int i = 0; i < 6; i++)
		kids->children[i] = ca_clip_component(names[i], keys[i], enable[i],
			rects[i][0], rects[i][1], rects[i][2], rects[i][3]);
	/* [1,0,0,100] has no height item: each missing field defaults to zero. */
	kids->children[5].children[1].leaf.value.list->nr_items = 4;
	struct string *name = cstr_to_string("ClipAreaFixture");
	bool loaded = probe_load_activity_tree(name, e);
	free_string(name);
	ex_free(e);
	CA_CHECK(loaded, "synthetic clip activity did not load");
	int act = sf_str("ClipAreaFixture", -1);
	for (int i = 0; i < 6; i++) {
		int n = bu_number(act, names[i]);
		CA_CHECK(n > 0, "%s has no parts number", names[i]);
		if (n <= 0 || !ca_expect_area(names[i], n, rects[i][0], rects[i][1], rects[i][2], rects[i][3]))
			continue;
		if (ca_failures)
			continue; /* Old getter stubs already prove failure; Is was unbound. */
		CA_CHECK(ca_enabled(n) == !!enable[i], "%s: enable flag differs from the pactex", names[i]);
		printf("  CA2 %s: enabled %d rect (%d,%d,%d,%d)\n", names[i], enable[i],
			rects[i][0], rects[i][1], rects[i][2], rects[i][3]);
	}
	heap_unref(act);
}

typedef int (*ca_transform_fn)(struct parts *, mat4);

static void ca_expect_clip(ca_transform_fn transform, const char *step, int n,
		int left, int top, int width, int height)
{
	mat4 m;
	int result = transform(parts_try_get(n), m);
	CA_CHECK(result == 1, "%s: clip result %d, want 1", step, result);
	if (result != 1)
		return;
	const float points[5][4] = {
		{ left, top, 0, 0 },
		{ left + width, top, 1, 0 },
		{ left, top + height, 0, 1 },
		{ left + width, top + height, 1, 1 },
		{ left + width * 0.5f, top + height * 0.5f, 0.5f, 0.5f }
	};
	for (int i = 0; i < 5; i++) {
		float x = points[i][0], y = points[i][1];
		float u = m[0][0] * x + m[1][0] * y + m[3][0];
		float v = m[0][1] * x + m[1][1] * y + m[3][1];
		CA_CHECK(isfinite(u) && isfinite(v) && fabsf(u - points[i][2]) < 0.0001f
			&& fabsf(v - points[i][3]) < 0.0001f,
			"%s point %d: (%g,%g), want (%g,%g)", step, i, u, v, points[i][2], points[i][3]);
	}
}

static void ca_geometry(void)
{
	bu_postlink();
	ca_transform_fn transform = (ca_transform_fn)dlsym(RTLD_DEFAULT, "parts_clip_area_transform");
	CA_CHECK(transform, "parts_clip_area_transform symbol is absent");
	if (!transform)
		return;
	const int p = 18411, c = 18412, g = 18413;
	parts_get(p);
	parts_get(c);
	parts_get(g);
	ri_rect(p, 200, 100);
	ri_origin(p, 5);
	ri_pos(p, 100, 200);
	ri_parent(c, p);
	ri_pos(c, 50, 30);
	ri_rect(c, 80, 60);
	ri_origin(c, 9);
	ri_parent(g, c);
	ri_pos(g, 5, 7);
	PE_UpdateComponent(0);
	mat4 m;
	CA_CHECK(transform(parts_get(g), m) == 0, "a subtree with no enabled ClipArea must not clip");

	/* Rect-detection parents draw nothing, but their clip still reaches every
	 * descendant. The native clip uses the anchor, without box origin offset. */
	ca_area(p, -10, 20, 100, 80);
	ca_expect_clip(transform, "ancestor only", g, 90, 220, 100, 80);
	ca_area(c, -20, 0, 100, 50);
	ca_expect_clip(transform, "self and ancestor intersection", c, 130, 230, 60, 50);
	ca_expect_clip(transform, "grandchild intersection", g, 130, 230, 60, 50);
	ca_enable(c, false);
	ca_expect_clip(transform, "disabled child retains ancestor", g, 90, 220, 100, 80);
	ca_enable(c, true);
	ca_area(c, 300, 0, 20, 20);
	CA_CHECK(transform(parts_get(g), m) == -1, "disjoint ancestor clips must suppress the subtree");
	ca_area(c, 0, 0, 0, 20);
	CA_CHECK(transform(parts_get(g), m) == -1, "zero width must suppress the subtree");
	ca_area(c, 0, 0, 20, -1);
	CA_CHECK(transform(parts_get(g), m) == -1, "negative height must suppress the subtree");
	ca_enable(c, false);
	ri_origin(p, 1);
	PE_UpdateComponent(0);
	ca_expect_clip(transform, "box origin does not move clip", g, 90, 220, 100, 80);

	/* Original screen rectangles use local scale and truncate each component
	 * to an integer, including negative coordinates. They are axis aligned. */
	ca_area(p, -3, 4, 11, 13);
	BU_PE("SetComponentMagX", BU_I(p), BU_F(1.5f));
	BU_PE("SetComponentMagY", BU_I(p), BU_F(0.5f));
	PE_UpdateComponent(0);
	ca_expect_clip(transform, "fractional local scale", p, 95, 202, 16, 6);
	ri_lr(p, 1);
	ri_tb(p, 1);
	PE_UpdateComponent(0);
	ca_expect_clip(transform, "reverse flags preserve clip extent", p, 95, 202, 16, 6);
	ri_pos(p, -2, -3);
	ca_area(p, -3, -4, 11, 13);
	PE_UpdateComponent(0);
	ca_expect_clip(transform, "negative coordinate truncation", p, -6, -5, 16, 6);
	ca_area(p, 0, 0, 1, 1);
	CA_CHECK(transform(parts_get(p), m) == -1, "height truncated to zero must suppress drawing");

	/* A separate hierarchy distinguishes local scale from accumulated scale.
	 * The screen-aligned rectangle is not the child's rotated bounding box. */
	const int p2 = 18414, c2 = 18415;
	parts_get(p2);
	parts_get(c2);
	ri_pos(p2, 100, 200);
	BU_PE("SetComponentMagX", BU_I(p2), BU_F(2.0f));
	BU_PE("SetComponentMagY", BU_I(p2), BU_F(3.0f));
	ca_enable(p2, false);
	ri_parent(c2, p2);
	ri_pos(c2, 0, 0);
	BU_PE("SetComponentMagX", BU_I(c2), BU_F(1.5f));
	BU_PE("SetComponentMagY", BU_I(c2), BU_F(0.5f));
	ca_area(c2, 4, 6, 20, 14);
	PE_UpdateComponent(0);
	ca_expect_clip(transform, "child uses local rather than inherited scale", c2, 106, 203, 30, 7);
	BU_PE("SetComponentRotateZ", BU_I(c2), BU_F(90.0f));
	PE_UpdateComponent(0);
	ca_expect_clip(transform, "rotated child clip stays axis aligned", c2, 106, 203, 30, 7);

	int version = ain->version;
	ain->version = 13;
	CA_CHECK(transform(parts_get(p), m) == 0, "legacy engine must ignore ClipArea");
	ain->version = version;
	printf("  CA3 anchor rectangles, ancestor intersections, local-scale truncation and empty clips checked\n");
}

static int ca_save_version(struct page *buffer)
{
	struct iarray_reader r;
	bool ok = iarray_init_reader(&r, buffer, "XPE");
	CA_CHECK(ok, "PE_Save produced no XPE header");
	if (!ok)
		return -1;
	return iarray_read(&r);
}

static void ca_free_buffer(struct page *buffer)
{
	if (buffer) {
		delete_page_vars(buffer);
		free_page(buffer);
	}
}

static void ca_persistence(void)
{
	bu_postlink();
	const int a = 18421, b = 18422;
	parts_get(a);
	parts_get(b);
	ca_area(a, -4, 3, 120, 80);
	ca_area(b, 20, 30, 40, 50);
	ca_enable(b, false);
	struct page *v4 = NULL;
	CA_CHECK(PE_Save(&v4), "v14 PE_Save failed");
	if (!v4)
		return;
	CA_CHECK(ca_save_version(v4) >= 4, "v14 save predates ClipArea support");
	ca_area(a, 0, 0, 9, 9);
	ca_area(b, 0, 0, 8, 8);
	CA_CHECK(PE_Load(&v4), "v14 PE_Load failed");
	if (!ca_expect_area("loaded enabled", a, -4, 3, 120, 80)
		|| !ca_expect_area("loaded disabled", b, 20, 30, 40, 50)) {
		ca_free_buffer(v4);
		return;
	}
	CA_CHECK(ca_enabled(a) == 1 && ca_enabled(b) == 0, "v14 load did not retain each enable flag");
	ca_free_buffer(v4);

	/* The legacy writer must ignore all ClipArea values. Comparing complete
	 * serialized integer arrays also guards against an accidental v3 field. */
	int version = ain->version;
	ain->version = 13;
	struct page *v3a = NULL, *v3b = NULL;
	CA_CHECK(PE_Save(&v3a), "legacy PE_Save failed");
	ca_area(a, 77, 88, 99, 111);
	ca_enable(b, true);
	CA_CHECK(PE_Save(&v3b), "legacy second PE_Save failed");
	CA_CHECK(v3a && v3b, "legacy save returned no page");
	if (v3a && v3b) {
		CA_CHECK(ca_save_version(v3a) == 3 && ca_save_version(v3b) == 3,
			"legacy writer changed the v3 version");
		bool equal = v3a->nr_vars == v3b->nr_vars;
		for (int i = 0; equal && i < v3a->nr_vars; i++)
			equal = v3a->values[i].i == v3b->values[i].i;
		CA_CHECK(equal, "legacy serialized bytes changed when only ClipArea changed");
	}
	ain->version = version;
	if (v3a) {
		CA_CHECK(PE_Load(&v3a), "v14 reader rejected a legacy v3 page");
		ca_expect_area("legacy default a", a, 0, 0, 0, 0);
		ca_expect_area("legacy default b", b, 0, 0, 0, 0);
		CA_CHECK(ca_enabled(a) == 0 && ca_enabled(b) == 0, "legacy load enabled ClipArea");
	}
	ca_free_buffer(v3a);
	ca_free_buffer(v3b);
	printf("  CA4 in-memory v14 roundtrip, unchanged v3 layout and old-page defaults checked\n");
}

static int test_clip_area(void)
{
	void (*cases[])(void) = { ca_hll, ca_loader, ca_geometry, ca_persistence };
	const char *names[] = { "CA1", "CA2", "CA3", "CA4" };
	int failed = 0;
	for (unsigned i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
		fflush(stdout);
		pid_t pid = fork();
		if (pid == 0) {
			alarm(60);
			ca_failures = 0;
			cases[i]();
			fflush(stdout);
			_exit(ca_failures ? 1 : 0);
		}
		int st = 0;
		bool ok = pid > 0 && waitpid(pid, &st, 0) == pid
			&& WIFEXITED(st) && WEXITSTATUS(st) == 0;
		printf("%s %s\n", names[i], ok ? "ok" : "FAIL");
		failed += !ok;
	}
	printf("CLIP-AREA %s: %d of %zu cases failed\n", failed ? "FAIL" : "PASS", failed,
		sizeof(cases) / sizeof(cases[0]));
	return failed ? 1 : 0;
}
