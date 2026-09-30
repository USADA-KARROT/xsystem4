/* logo-gloss: the v14 pactex loader reads the CN spellings of 描画フィルタ and
 * 加算色.
 *
 *   LG1 The Chinese pactex files spell the draw filter 描畫フィルタ (GBK
 *       C3 E8 AE 8B ...) and the add colour 加算色 in GBK; the loader only knew
 *       the SJIS keys, so SceneLogo's Light (描畫フィルタ 3, screen) was drawn
 *       with the normal blend: its two pale yellow panels became a yellow band
 *       across the white logo screen, parked at the bottom right after the
 *       sweep. The same keys gave the title background layers their screen /
 *       multiply filters. A synthetic GBK activity is loaded through the real
 *       loader; the GBK keys, the SJIS key and a component without the keys
 *       are checked with PE_GetPartsDrawFilter / PE_GetAddColor.
 *   LG2 アルファクリッパー (GBK key ending in A9 60, and the SJIS key) names
 *       another parts of the activity; the loader resolves it after the whole
 *       activity is built and sets alpha_clipper_parts_no (SceneLogo's gloss
 *       panels are masked by the badge and the ALICESOFT text, so no gloss
 *       shows during the fade-in). An unknown name leaves 0. Before: ignored.
 *   LG3 GetComponentMulColorR/G/B and AddColorR/G/B read back the parts'
 *       colours (Motion sets one channel at a time and reads the others; the
 *       constant 255 stubs turned the warning screen's fade to black into
 *       white -> yellow). Unknown parts read 255 / 0.
 * The child-inherits-the-filter rule and the clipper draw need GL and are
 * covered by the GUI frame comparison (research/gui-visual/logo-gloss.md).
 * Only functions that exist before the fix are referenced.
 */

static int lg_failures;
#define LG_CHECK(c, ...) do { if (!(c)) { printf("  FAIL: "); printf(__VA_ARGS__); printf("\n"); lg_failures++; } } while (0)

#define LG_FILTER_GBK "\xc3\xe8\xae\x8b\xa5\xd5\xa5\xa3\xa5\xeb\xa5\xbf"	/* 描畫フィルタ */
#define LG_FILTER_SJIS "\x95\x60\x89\xe6\x83\x74\x83\x42\x83\x8b\x83\x5e"	/* 描画フィルタ */
#define LG_ADD_GBK "\xbc\xd3\xcb\xe3\xc9\xab"	/* 加算色 */

extern int PE_GetPartsDrawFilter(int parts_no);
extern void PE_GetAddColor(int parts_no, int *r, int *g, int *b);

static struct ex_tree lg_ilist(const char *name, int a, int b, int c)
{
	struct ex_list *l = xcalloc(1, sizeof(*l));
	l->nr_items = 3;
	l->items = xcalloc(3, sizeof(*l->items));
	l->items[0].value = (struct ex_value){ .type = EX_INT, .i = a };
	l->items[1].value = (struct ex_value){ .type = EX_INT, .i = b };
	l->items[2].value = (struct ex_value){ .type = EX_INT, .i = c };
	return bu_leaf(name, (struct ex_value){ .type = EX_LIST, .list = l });
}

/* A low-level component with an optional filter key and add colour. */
static struct ex_tree lg_comp(const char *name, const char *filter_key, int filter, bool add)
{
	int n = 1 + (filter_key ? 1 : 0) + (add ? 1 : 0), k = 1;
	struct ex_tree t = bu_branch(name, n);
	t.children[0] = bu_branch(BU_INFO, 1);
	t.children[0].children[0] = bu_sleaf(BU_TYPE, BU_LOW);
	if (filter_key)
		t.children[k++] = bu_ileaf(filter_key, filter);
	if (add)
		t.children[k++] = lg_ilist(LG_ADD_GBK, 10, 20, 30);
	return t;
}

static const char *lg_names[] = { "Gloss", "SjisFilter", "Plain" };

static void lg_loader(void)
{
	bu_postlink();
	struct ex *e = xcalloc(1, sizeof(*e));
	e->nr_blocks = 1;
	e->blocks = xcalloc(1, sizeof(*e->blocks));
	e->blocks[0].name = cstr_to_string("GlossActivity");
	e->blocks[0].val.type = EX_TREE;
	struct ex_tree *top = xcalloc(1, sizeof(*top));
	*top = bu_branch("GlossActivity", 1);
	e->blocks[0].val.tree = top;
	top->children[0] = bu_comp("GlossRoot", BU_LAYOUT, 0, 3);
	struct ex_tree *kids = &top->children[0].children[1];
	kids->children[0] = lg_comp(lg_names[0], LG_FILTER_GBK, 3, true);
	kids->children[1] = lg_comp(lg_names[1], LG_FILTER_SJIS, 1, false);
	kids->children[2] = lg_comp(lg_names[2], NULL, 0, false);

	struct string *name = cstr_to_string("GlossFixture");
	bool loaded = probe_load_activity_tree(name, e);
	free_string(name);
	ex_free(e);
	LG_CHECK(loaded, "synthetic activity did not load");
	int act = sf_str("GlossFixture", -1);
	int nums[3];
	for (int i = 0; i < 3; i++)
		nums[i] = bu_number(act, lg_names[i]);
	heap_unref(act);

	const int want_filter[] = { 3, 1, 0 };
	const int want_add[3][3] = { { 10, 20, 30 }, { 0, 0, 0 }, { 0, 0, 0 } };
	for (int i = 0; i < 3; i++) {
		LG_CHECK(nums[i] > 0, "%s has no parts number", lg_names[i]);
		if (nums[i] <= 0)
			continue;
		int f = PE_GetPartsDrawFilter(nums[i]);
		int r = -1, g = -1, b = -1;
		PE_GetAddColor(nums[i], &r, &g, &b);
		LG_CHECK(f == want_filter[i], "%s: draw filter %d, want %d", lg_names[i], f, want_filter[i]);
		LG_CHECK(r == want_add[i][0] && g == want_add[i][1] && b == want_add[i][2],
			 "%s: add colour (%d,%d,%d), want (%d,%d,%d)", lg_names[i], r, g, b,
			 want_add[i][0], want_add[i][1], want_add[i][2]);
		printf("  LG1 %s: filter %d add (%d,%d,%d)\n", lg_names[i], f, r, g, b);
	}
}

#define LG_CLIP_GBK "\xa5\xa2\xa5\xeb\xa5\xd5\xa5\xa1\xa5\xaf\xa5\xea\xa5\xc3\xa5\xd1\xa9\x60"	/* アルファクリッパー */
#define LG_CLIP_SJIS "\x83\x41\x83\x8b\x83\x74\x83\x40\x83\x4e\x83\x8a\x83\x62\x83\x70\x81\x5b"

static struct ex_tree lg_clip_comp(const char *name, const char *key, const char *clipper)
{
	struct ex_tree t = bu_branch(name, key ? 2 : 1);
	t.children[0] = bu_branch(BU_INFO, 1);
	t.children[0].children[0] = bu_sleaf(BU_TYPE, BU_LOW);
	if (key)
		t.children[1] = bu_sleaf(key, clipper);
	return t;
}

static void lg_clipper(void)
{
	bu_postlink();
	const char *names[] = { "Mask", "Glow", "GlowSjis", "Lost", "Plain" };
	struct ex *e = xcalloc(1, sizeof(*e));
	e->nr_blocks = 1;
	e->blocks = xcalloc(1, sizeof(*e->blocks));
	e->blocks[0].name = cstr_to_string("ClipActivity");
	e->blocks[0].val.type = EX_TREE;
	struct ex_tree *top = xcalloc(1, sizeof(*top));
	*top = bu_branch("ClipActivity", 1);
	e->blocks[0].val.tree = top;
	top->children[0] = bu_comp("ClipRoot", BU_LAYOUT, 0, 5);
	struct ex_tree *kids = &top->children[0].children[1];
	/* the clipped parts come before the mask, as LightA/LightB before Logo */
	kids->children[0] = lg_clip_comp(names[1], LG_CLIP_GBK, "Mask");
	kids->children[1] = lg_clip_comp(names[2], LG_CLIP_SJIS, "Mask");
	kids->children[2] = lg_clip_comp(names[3], LG_CLIP_GBK, "Nope");
	kids->children[3] = lg_clip_comp(names[4], NULL, NULL);
	kids->children[4] = lg_clip_comp(names[0], LG_CLIP_GBK, "");
	struct string *name = cstr_to_string("ClipFixture");
	bool loaded = probe_load_activity_tree(name, e);
	free_string(name);
	ex_free(e);
	LG_CHECK(loaded, "synthetic activity did not load");
	int act = sf_str("ClipFixture", -1);
	int nums[5];
	for (int i = 0; i < 5; i++)
		nums[i] = bu_number(act, names[i]);
	heap_unref(act);
	const int want[] = { 0, nums[0], nums[0], 0, 0 };
	for (int i = 0; i < 5; i++) {
		struct parts *p = nums[i] > 0 ? parts_try_get(nums[i]) : NULL;
		LG_CHECK(p, "%s has no parts", names[i]);
		if (!p)
			continue;
		LG_CHECK(p->alpha_clipper_parts_no == want[i], "%s: alpha clipper %d, want %d",
			 names[i], p->alpha_clipper_parts_no, want[i]);
		printf("  LG2 %s: alpha clipper %d\n", names[i], p->alpha_clipper_parts_no);
	}
}

static void lg_colors(void)
{
	bu_postlink();
	int n = 4242;
	PE_SetShow(n, true);
	BU_PE("SetComponentMulColor", BU_I(n), BU_I(10), BU_I(20), BU_I(30));
	BU_PE("SetComponentAddColor", BU_I(n), BU_I(1), BU_I(2), BU_I(3));
	int mr = BU_PE("GetComponentMulColorR", BU_I(n)).i, mg = BU_PE("GetComponentMulColorG", BU_I(n)).i,
	    mb = BU_PE("GetComponentMulColorB", BU_I(n)).i;
	int ar = BU_PE("GetComponentAddColorR", BU_I(n)).i, ag = BU_PE("GetComponentAddColorG", BU_I(n)).i,
	    ab = BU_PE("GetComponentAddColorB", BU_I(n)).i;
	LG_CHECK(mr == 10 && mg == 20 && mb == 30, "mul colour read back (%d,%d,%d)", mr, mg, mb);
	LG_CHECK(ar == 1 && ag == 2 && ab == 3, "add colour read back (%d,%d,%d)", ar, ag, ab);
	/* Motion's MulColorR then G then B, each reading the other two back */
	BU_PE("SetComponentMulColor", BU_I(n), BU_I(255), BU_I(255), BU_I(255));
	for (int c = 0; c < 3; c++) {
		int r = BU_PE("GetComponentMulColorR", BU_I(n)).i, g = BU_PE("GetComponentMulColorG", BU_I(n)).i,
		    b = BU_PE("GetComponentMulColorB", BU_I(n)).i;
		if (c == 0) r = 0; else if (c == 1) g = 0; else b = 0;
		BU_PE("SetComponentMulColor", BU_I(n), BU_I(r), BU_I(g), BU_I(b));
	}
	int fr, fg, fb;
	PE_GetMultiplyColor(n, &fr, &fg, &fb);
	LG_CHECK(fr == 0 && fg == 0 && fb == 0, "fade to black ends at (%d,%d,%d), want (0,0,0)", fr, fg, fb);
	int ur = BU_PE("GetComponentMulColorR", BU_I(99999)).i, ua = BU_PE("GetComponentAddColorR", BU_I(99999)).i;
	LG_CHECK(ur == 255 && ua == 0, "unknown parts read (%d, %d), want (255, 0)", ur, ua);
	LG_CHECK(!parts_try_get(99999), "reading an unknown parts created it");
	printf("  LG3 mul (%d,%d,%d) add (%d,%d,%d) fade end (%d,%d,%d)\n", mr, mg, mb, ar, ag, ab, fr, fg, fb);
}

static int test_logo_gloss(void)
{
	void (*cases[])(void) = { lg_loader, lg_clipper, lg_colors };
	const char *names[] = { "LG1", "LG2", "LG3" };
	int failed = 0;
	for (int i = 0; i < 3; i++) {
		fflush(stdout);
		pid_t pid = fork();
		if (pid == 0) {
			alarm(60);
			lg_failures = 0;
			cases[i]();
			fflush(stdout);
			_exit(lg_failures ? 1 : 0);
		}
		int st = 0;
		waitpid(pid, &st, 0);
		bool ok = WIFEXITED(st) && WEXITSTATUS(st) == 0;
		printf("%s %s\n", names[i], ok ? "ok" : "FAIL");
		failed += !ok;
	}
	printf("LOGO-GLOSS %s: %d of 3 cases failed\n", failed ? "FAIL" : "PASS", failed);
	return failed ? 1 : 0;
}
