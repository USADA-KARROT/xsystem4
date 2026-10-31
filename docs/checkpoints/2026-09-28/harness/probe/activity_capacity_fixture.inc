/* activity-capacity: bounded registry growth through the real HLLs.
 *
 * Native CreateActivity (0x5633c0) and ReadActivityFile (0x5634a0) insert
 * into a dynamic container (0x563ec0 -> 0x564200; 0x564450 rehashes).
 * This tests 257 distinct names, not the native allocation policy or OOM.
 * Release and controller lifetime follow activity-release and layer-root;
 * reset follows system-reset's real CALLHLL/ModuleFini path.
 *
 * The synthetic EX tree is FixtureTree { FixtureRoot (layout) {
 * FixtureIcon (low-level: normal CG, down rectangle) } }. All resource
 * names are invented, with no images. The Pact provider supplies encoded
 * EX bytes to the real ReadActivityFile; that HLL is never replaced.
 * Empty CG and rectangle states need no graphics context. The independent
 * model records each new root/icon number once, plus delegate/end-key
 * markers, and later checks every name against that recorded identity.
 *
 * AC1 Create 257; AC2 Read 257; AC3 first/middle/last release;
 * AC4 release/recreate and a complete second generation; AC5 controller
 * removal retains the name until Release; AC6 two real VM resets;
 * AC7 explicit SJIS rule; AC8 non-v14 capacity bounds (v11/v13/v15)
 * and duplicate guards.
 * The duplicate Create=true guard preserves the port's existing behavior;
 * native Create rejects duplicates. It is not a native semantic claim.
 *
 * Not covered: real asset pixels, wrapper/user-component gameplay, AFA
 * parsing, long/duplicate names, failed-read rollback, native iteration
 * order, OOM, allocator movement, or allocation leak accounting. Provider
 * ownership/restoration fault injection and exhaustive scenario-milestone
 * or assertion-removal checks are also not covered. A pass here alone
 * does not establish the cause of a gameplay failure.
 */
#include "asset_manager.h"
#include "system4/archive.h"

extern void probe_install_pact_asset(const uint8_t *bytes, size_t size);
extern void probe_uninstall_pact_asset(void);

#define ACTCAP_N 257
#define ACTCAP_ROOT "FixtureRoot"
#define ACTCAP_ICON "FixtureIcon"
#define ACTCAP_CLOSE "FixtureClose"
#define ACTCAP_DOWN "\xa5\xad\xa9\x60\xa5\xc0\xa5\xa6\xa5\xf3\xa0\xee\x91\x42"
#define ACTCAP_RECT "\xbe\xd8\xd0\xce\xb2\xbf\xbc\xfe"
#define ACTCAP_ALIAS_SJIS "\x83\x8b\x81\x5b\x83\x67\x83\x70\x81\x5b\x83\x63"

static int actcap_failures, actcap_checks;
#define ACTCAP_CHECK(c, ...) do { actcap_checks++; if (!(c)) { actcap_failures++; \
	if (actcap_failures <= 16) { printf("  FAIL: "); printf(__VA_ARGS__); printf("\n"); } } } while (0)

struct actcap_model {
	char name[40];
	int root, icon, marker;
	bool registered, parts_alive;
};
static struct sg_buf actcap_bytes;

/* A deliberately small writer: branches and string leaves only. The EX
 * envelope and primitive byte writers are shared with sound-groups. */
static void actcap_write_tree(struct sg_buf *b, const struct ex_tree *t)
{
	sgb_str(b, t->name->text);
	sgb_u32(b, t->is_leaf);
	if (!t->is_leaf) {
		sgb_u32(b, t->nr_children);
		for (unsigned i = 0; i < t->nr_children; i++)
			actcap_write_tree(b, &t->children[i]);
		return;
	}
	assert(t->leaf.value.type == EX_STRING);
	sgb_u32(b, EX_STRING);
	size_t at = b->n;
	sgb_u32(b, 0);
	size_t start = b->n;
	sgb_str(b, t->leaf.name->text);
	sgb_str(b, t->leaf.value.s->text);
	sgb_patch(b, at, b->n - start);
	sgb_u32(b, 0);
}

static void actcap_make_bytes(void)
{
	struct ex *e = xcalloc(1, sizeof(*e));
	e->nr_blocks = 1;
	e->blocks = xcalloc(1, sizeof(*e->blocks));
	e->blocks[0].name = cstr_to_string("FixtureTree");
	e->blocks[0].val.type = EX_TREE;
	struct ex_tree *top = xcalloc(1, sizeof(*top));
	*top = bu_branch("FixtureTree", 1);
	e->blocks[0].val.tree = top;
	top->children[0] = bu_comp(ACTCAP_ROOT, BU_LAYOUT, 0, 1);
	struct ex_tree *icon = &top->children[0].children[1].children[0];
	*icon = bu_comp(ACTCAP_ICON, BU_LOW, 2, 0);
	struct ex_tree *info = &icon->children[0];
	info->children[1] = bu_branch(BU_NORMAL, 1);
	info->children[1].children[0] = bu_sleaf(BU_TYPE, BU_CG);
	info->children[2] = bu_branch(ACTCAP_DOWN, 1);
	info->children[2].children[0] = bu_sleaf(BU_TYPE, ACTCAP_RECT);
	struct sg_buf block = {0};
	sgb_u32(&block, EX_TREE);
	size_t at = block.n;
	sgb_u32(&block, 0);
	size_t start = block.n;
	sgb_str(&block, "FixtureTree");
	actcap_write_tree(&block, top);
	sgb_patch(&block, at, block.n - start);
	sgx_file(&actcap_bytes, &block, 1);
	free(block.p);
	ex_free(e);
	/* Read back the serialized shape before testing the engine loader. */
	e = ex_read(actcap_bytes.p, actcap_bytes.n);
	ACTCAP_CHECK(e && e->nr_blocks == 1 && e->blocks[0].val.type == EX_TREE,
		"serialized fixture block");
	assert(e && e->nr_blocks == 1 && e->blocks[0].val.type == EX_TREE);
	top = e->blocks[0].val.tree;
	ACTCAP_CHECK(!top->is_leaf && top->nr_children == 1
		&& !strcmp(top->children[0].name->text, ACTCAP_ROOT), "serialized root");
	assert(top->nr_children == 1 && top->children[0].nr_children == 2);
	icon = &top->children[0].children[1].children[0];
	ACTCAP_CHECK(!strcmp(icon->name->text, ACTCAP_ICON) && icon->nr_children == 1,
		"serialized icon");
	info = &icon->children[0];
	ACTCAP_CHECK(info->nr_children == 3
		&& !strcmp(info->children[1].children[0].leaf.value.s->text, BU_CG)
		&& !strcmp(info->children[2].children[0].leaf.value.s->text, ACTCAP_RECT),
		"serialized state types");
	ex_free(e);
}

static int actcap_string_call(const char *fn, const char *name)
{
	int s = sf_str(name, -1);
	int r = BU_PE(fn, BU_I(s)).i;
	heap_unref(s);
	return r;
}

static int actcap_name_call(const char *fn, const char *name, const char *part)
{
	int a = sf_str(name, -1), b = sf_str(part, -1);
	int r = BU_PE(fn, BU_I(a), BU_I(b)).i;
	heap_unref(a); heap_unref(b);
	return r;
}

static bool actcap_read(const char *name)
{
	int a = sf_str(name, -1), f = sf_str("CapacityFixture", -1);
	bool r = BU_PE("ReadActivityFile", BU_I(a), BU_I(f), BU_I(0)).i;
	heap_unref(a); heap_unref(f);
	return r;
}

static void actcap_key(const char *name, int key)
{
	int a = sf_str(name, -1);
	BU_PE("SetActivityEndKey", BU_I(a), BU_I(key));
	heap_unref(a);
}

static int actcap_key_at(const char *name, int at)
{
	int a = sf_str(name, -1);
	int r = BU_PE("GetActivityEndKey", BU_I(a), BU_I(at)).i;
	heap_unref(a);
	return r;
}

static void actcap_setup(void)
{
	ci_setup(); /* Unlike lr_setup, this leaves ReadActivityFile intact. */
	actcap_make_bytes();
	probe_install_pact_asset(actcap_bytes.p, actcap_bytes.n);
	int id = -9;
	ACTCAP_CHECK(asset_exists_by_name(ASSET_PACT, "CapacityFixture.pactex", &id)
		&& id == 0, "provider positive name/id");
	const char *bad[] = { "CapacityFixture", "capacityFixture.pactex", "CapacityFixture.pactexX" };
	for (unsigned i = 0; i < sizeof(bad) / sizeof(bad[0]); i++) {
		ACTCAP_CHECK(!asset_exists_by_name(ASSET_PACT, bad[i], NULL), "provider miss %u", i);
		ACTCAP_CHECK(!asset_get_by_name(ASSET_PACT, bad[i], NULL), "provider get miss %u", i);
	}
	struct archive_data *a = asset_get_by_name(ASSET_PACT, "CapacityFixture.pactex", NULL);
	struct archive_data *b = asset_get_by_name(ASSET_PACT, "CapacityFixture.pactex", &id);
	ACTCAP_CHECK(a && b && a != b && a->data != b->data && id == 0
		&& a->size == actcap_bytes.n && b->size == actcap_bytes.n
		&& !memcmp(a->data, actcap_bytes.p, actcap_bytes.n)
		&& !memcmp(b->data, actcap_bytes.p, actcap_bytes.n), "provider independent copies");
	if (a) archive_free_data(a);
	if (b) archive_free_data(b);
}

static void actcap_cleanup(void)
{
	probe_uninstall_pact_asset();
	free(actcap_bytes.p);
	actcap_bytes = (struct sg_buf){0};
}

static void actcap_model_init(struct actcap_model *m)
{
	memset(m, 0, sizeof(*m) * ACTCAP_N);
	for (int i = 0; i < ACTCAP_N; i++)
		snprintf(m[i].name, sizeof(m[i].name), "CapacityItem%03d", i);
}

static bool actcap_load_one(struct actcap_model *m, int i, int generation)
{
	bool ok = actcap_read(m[i].name);
	ACTCAP_CHECK(ok, "Read index=%d name=%s returned=%d (prior successful=%d)",
		i, m[i].name, ok, i);
	if (!ok) {
		printf("  boundary evidence: activity=%d icon=%d\n",
			actcap_string_call("IsExistActivity", m[i].name),
			actcap_name_call("IsExistActivityPartsByName", m[i].name, ACTCAP_ICON));
		return false;
	}
	m[i].root = actcap_name_call("GetActivityPartsNumber", m[i].name, ACTCAP_ROOT);
	m[i].icon = actcap_name_call("GetActivityPartsNumber", m[i].name, ACTCAP_ICON);
	m[i].registered = m[i].parts_alive = true;
	m[i].marker = 10000 + generation * 1000 + i * 2;
	ACTCAP_CHECK(m[i].root > 0 && m[i].icon > 0 && m[i].root != m[i].icon,
		"Read index=%d real root/icon %d/%d", i, m[i].root, m[i].icon);
	if (m[i].root <= 0 || m[i].icon <= 0)
		return false;
	ACTCAP_CHECK(actcap_string_call("IsExistActivity", m[i].name)
		&& actcap_name_call("IsExistActivityPartsByName", m[i].name, ACTCAP_ICON)
		&& lr_parent(m[i].icon) == m[i].root
		&& BU_PE("GetComponentType", BU_I(m[i].root), BU_I(1)).i == 8
		&& BU_PE("GetComponentType", BU_I(m[i].icon), BU_I(1)).i == 19
		&& BU_PE("GetComponentType", BU_I(m[i].icon), BU_I(3)).i == 25,
		"Read index=%d root/icon names, parent or state types", i);
	ACTCAP_CHECK(actcap_string_call("NumofActivityEndKey", m[i].name) == 0,
		"new activity %d has old keys", i);
	ACTCAP_CHECK(!actcap_name_call("IsExistActivityCloseParts", m[i].name, ACTCAP_CLOSE),
		"new activity %d has old close markers", i);
	actcap_name_call("AddActivityCloseParts", m[i].name, ACTCAP_CLOSE);
	PE_SetDelegateIndex(m[i].root, m[i].marker);
	PE_SetDelegateIndex(m[i].icon, m[i].marker + 1);
	actcap_key(m[i].name, m[i].marker);
	for (int j = 0; j < i; j++)
		if (m[j].parts_alive)
			ACTCAP_CHECK(m[i].root != m[j].root && m[i].root != m[j].icon
				&& m[i].icon != m[j].root && m[i].icon != m[j].icon,
				"distinct identities %d and %d", i, j);
	return true;
}

static bool actcap_fill(struct actcap_model *m, int n, int generation)
{
	for (int i = 0; i < n; i++)
		if (!actcap_load_one(m, i, generation))
			return false;
	printf("  loaded %d activities through ReadActivityFile\n", n);
	return true;
}

static void actcap_check_model(const struct actcap_model *m, int n)
{
	for (int i = 0; i < n; i++) {
		bool exists = actcap_string_call("IsExistActivity", m[i].name);
		ACTCAP_CHECK(exists == m[i].registered, "name %d exists=%d expected=%d",
			i, exists, m[i].registered);
		if (!m[i].registered) {
			ACTCAP_CHECK(!parts_try_get(m[i].root) && !parts_try_get(m[i].icon),
				"released own parts %d remain", i);
			continue;
		}
		ACTCAP_CHECK(actcap_name_call("IsExistActivityPartsByName", m[i].name, ACTCAP_ICON)
			&& actcap_name_call("GetActivityPartsNumber", m[i].name, ACTCAP_ROOT) == m[i].root
			&& actcap_name_call("GetActivityPartsNumber", m[i].name, "") == m[i].root
			&& actcap_name_call("GetActivityPartsNumber", m[i].name, ACTCAP_ICON) == m[i].icon,
			"name %d identity changed", i);
		ACTCAP_CHECK(actcap_string_call("NumofActivityEndKey", m[i].name) == 1
			&& actcap_key_at(m[i].name, 0) == m[i].marker, "name %d keys changed", i);
		ACTCAP_CHECK(actcap_name_call("IsExistActivityCloseParts", m[i].name, ACTCAP_CLOSE),
			"name %d close marker changed", i);
		struct parts *root = parts_try_get(m[i].root), *icon = parts_try_get(m[i].icon);
		ACTCAP_CHECK(!!root == m[i].parts_alive && !!icon == m[i].parts_alive,
			"name %d part lifetime changed", i);
		if (!root || !icon || !m[i].parts_alive)
			continue;
		ACTCAP_CHECK(root->delegate_index == m[i].marker && icon->delegate_index == m[i].marker + 1,
			"name %d delegate markers changed", i);
		ACTCAP_CHECK(lr_parent(m[i].icon) == m[i].root, "name %d icon parent changed", i);
		ACTCAP_CHECK(BU_PE("GetComponentType", BU_I(m[i].root), BU_I(1)).i == 8
			&& BU_PE("GetComponentType", BU_I(m[i].icon), BU_I(1)).i == 19
			&& BU_PE("GetComponentType", BU_I(m[i].icon), BU_I(3)).i == 25,
			"name %d native state types changed", i);
	}
	ACTCAP_CHECK(!actcap_string_call("IsExistActivity", "CapacityItem257")
		&& !actcap_string_call("IsExistActivity", "capacityItem000")
		&& !actcap_string_call("IsExistActivity", "CapacityItem000X"), "exact name misses");
}

static void actcap_release_one(struct actcap_model *m, int i)
{
	int s = sf_str(m[i].name, -1), list = ar_int_list();
	bool ok = BU_PE("ReleaseActivity", BU_I(s), BU_I(list)).i;
	heap_unref(s);
	ACTCAP_CHECK(ok, "release %d failed", i);
	int n = ar_list_len(list), r = 0, c = 0;
	for (int j = 0; j < n; j++) {
		r += ar_list_at(list, j) == m[i].marker;
		c += ar_list_at(list, j) == m[i].marker + 1;
	}
	ACTCAP_CHECK(m[i].parts_alive ? n == 2 && r == 1 && c == 1 : n == 0,
		"release %d erase list n=%d root=%d icon=%d", i, n, r, c);
	heap_unref(list);
	m[i].registered = m[i].parts_alive = false;
}

static void actcap1_create(void)
{
	actcap_setup();
	struct actcap_model m[ACTCAP_N]; actcap_model_init(m);
	for (int i = 0; i < ACTCAP_N; i++) {
		bool ok = actcap_string_call("CreateActivity", m[i].name);
		ACTCAP_CHECK(ok, "Create index=%d returned=%d (prior successful=%d)", i, ok, i);
		if (!ok) break;
		int a = sf_str(m[i].name, -1), b = sf_str("Marker", -1);
		ACTCAP_CHECK(BU_PE("AddActivityParts", BU_I(a), BU_I(b), BU_I(17000 + i)).i,
			"Create marker %d", i);
		ACTCAP_CHECK(BU_PE("IsExistActivityPartsByNumber", BU_I(a), BU_I(17000 + i)).i
			&& !BU_PE("IsExistActivityPartsByNumber", BU_I(a), BU_I(18000 + i)).i,
			"Create number membership %d", i);
		heap_unref(a); heap_unref(b);
		for (int j = 0; j <= i; j++)
			ACTCAP_CHECK(actcap_string_call("IsExistActivity", m[j].name)
				&& actcap_name_call("GetActivityPartsNumber", m[j].name, "Marker") == 17000 + j,
				"Create identity %d after insertion %d", j, i);
		if (i == 127 || i == 128 || i == 255 || i == 256)
			printf("  Create boundary completed=%d\n", i + 1);
	}
	actcap_cleanup();
}

static void actcap2_read(void)
{
	actcap_setup();
	struct actcap_model m[ACTCAP_N]; actcap_model_init(m);
	if (actcap_fill(m, ACTCAP_N, 0)) actcap_check_model(m, ACTCAP_N);
	actcap_cleanup();
}

static void actcap3_release(void)
{
	actcap_setup();
	struct actcap_model m[ACTCAP_N]; actcap_model_init(m);
	if (actcap_fill(m, ACTCAP_N, 0)) {
		/* Last first, so no assumption about the container's iteration
		 * order is needed to cover the current last-slot branch. */
		int gone[] = { 256, 63, 0 };
		for (unsigned j = 0; j < sizeof(gone) / sizeof(gone[0]); j++) {
			actcap_release_one(m, gone[j]);
			actcap_check_model(m, ACTCAP_N);
		}
	}
	actcap_cleanup();
}

static void actcap4_reuse(void)
{
	actcap_setup();
	struct actcap_model m[ACTCAP_N]; actcap_model_init(m);
	if (!actcap_fill(m, ACTCAP_N, 0)) { actcap_cleanup(); return; }
	int gone[] = { 63, 0, 256 };
	for (unsigned j = 0; j < sizeof(gone) / sizeof(gone[0]); j++) {
		int i = gone[j], old_root = m[i].root, old_icon = m[i].icon;
		actcap_release_one(m, i);
		if (!actcap_load_one(m, i, 1)) break;
		ACTCAP_CHECK(m[i].root != old_root && m[i].icon != old_icon
			&& !parts_try_get(old_root) && !parts_try_get(old_icon), "reuse %d kept old parts", i);
		actcap_check_model(m, ACTCAP_N);
	}
	for (int i = 0; i < ACTCAP_N; i++)
		if (m[i].registered) actcap_release_one(m, i);
	actcap_check_model(m, ACTCAP_N);
	if (actcap_fill(m, ACTCAP_N, 2)) actcap_check_model(m, ACTCAP_N);
	actcap_cleanup();
}

static void actcap5_controller(void)
{
	actcap_setup();
	struct actcap_model m[ACTCAP_N]; actcap_model_init(m);
	if (!actcap_fill(m, 127, 0)) { actcap_cleanup(); return; }
	int layer = ci_add(-1);
	if (!actcap_load_one(m, 127, 0)) { actcap_cleanup(); return; }
	ci_erase(layer);
	m[127].parts_alive = false;
	actcap_check_model(m, 128);
	for (int i = 128; i < ACTCAP_N; i++)
		if (!actcap_load_one(m, i, 0)) { actcap_cleanup(); return; }
	actcap_check_model(m, ACTCAP_N);
	actcap_release_one(m, 127);
	actcap_check_model(m, ACTCAP_N);
	actcap_cleanup();
}

static void actcap6_reset(void)
{
	actcap_setup();
	struct actcap_model m[ACTCAP_N]; actcap_model_init(m);
	for (int cycle = 0; cycle < 2; cycle++) {
		if (!actcap_fill(m, 129, cycle)) { actcap_cleanup(); return; }
		int layer = ci_add(-1);
		if (!actcap_load_one(m, 129, cycle)) { actcap_cleanup(); return; }
		ci_erase(layer);
		m[129].parts_alive = false;
		actcap_check_model(m, 130);
		probe_uninstall_pact_asset();
		int how = sr_reset_with_libraries();
		ACTCAP_CHECK(how == 1, "real reset cycle %d result=%d", cycle, how);
		if (how != 1) { free(actcap_bytes.p); actcap_bytes = (struct sg_buf){0}; return; }
		for (int i = 0; i < 130; i++)
			ACTCAP_CHECK(!actcap_string_call("IsExistActivity", m[i].name), "reset retained name %d", i);
		ci_setup();
		probe_install_pact_asset(actcap_bytes.p, actcap_bytes.n);
		actcap_model_init(m);
	}
	if (actcap_fill(m, 129, 2)) actcap_check_model(m, 129);
	actcap_cleanup();
}

static void actcap7_sjis(void)
{
	li_charset(1);
	actcap_setup();
	struct actcap_model m[ACTCAP_N]; actcap_model_init(m);
	if (actcap_fill(m, ACTCAP_N, 0)) {
		actcap_check_model(m, ACTCAP_N);
		for (int i = 0; i < ACTCAP_N; i++)
			ACTCAP_CHECK(actcap_name_call("GetActivityPartsNumber", m[i].name, ACTCAP_ALIAS_SJIS) == m[i].root,
				"SJIS root alias %d", i);
	}
	actcap_cleanup();
}

static void actcap8_guards(void)
{
	actcap_setup();
	struct actcap_model m[ACTCAP_N]; actcap_model_init(m);
	if (!actcap_fill(m, 2, 0)) { actcap_cleanup(); return; }
	for (int i = 0; i < 2; i++)
		ACTCAP_CHECK(actcap_string_call("CreateActivity", m[i].name), "existing duplicate return %d", i);
	actcap_check_model(m, 2);
	int s = sf_str("CapacityAbsent", -1), list = ar_int_list();
	ACTCAP_CHECK(!BU_PE("ReleaseActivity", BU_I(s), BU_I(list)).i && ar_list_len(list) == 0,
		"release absent name");
	heap_unref(s); heap_unref(list);
	actcap_check_model(m, 2);
	int version = ain->version;
	for (int v = 11; v <= 13; v += 2) {
		/* Native C selectors only: no v14 bytecode runs as an older VM. */
		ain->version = v;
		PE_SetComponentType(m[0].icon, 11, 1);
		ACTCAP_CHECK(PE_GetComponentType(m[0].icon, 1) == 11, "v%d raw CG taxonomy", v);
		PE_SetComponentType(m[0].icon, 13, 1);
		ACTCAP_CHECK(PE_GetComponentType(m[0].icon, 1) == 13
			&& PE_GetComponentType(m[0].icon, 0) == -1, "v%d raw text/invalid taxonomy", v);
	}
	ain->version = version;
	/* The existing ModuleFini boundary preserves the registry before
	 * v14. Parts themselves may be released by PE_Reset in either case. */
	extern struct static_library lib_PartsEngine;
	void (*fini)(void) = sr_fn(&lib_PartsEngine, "_ModuleFini");
	assert(fini);
	for (int v = 11; v <= 13; v += 2) {
		ain->version = v;
		fini();
		ain->version = version;
		for (int i = 0; i < 2; i++)
			ACTCAP_CHECK(actcap_string_call("IsExistActivity", m[i].name)
				&& actcap_name_call("GetActivityPartsNumber", m[i].name, ACTCAP_ROOT) == m[i].root
				&& actcap_name_call("GetActivityPartsNumber", m[i].name, ACTCAP_ICON) == m[i].icon,
				"v%d ModuleFini changed registry entry %d", v, i);
	}
	fini();
	for (int i = 0; i < 2; i++)
		ACTCAP_CHECK(!actcap_string_call("IsExistActivity", m[i].name),
			"v14 ModuleFini retained registry entry %d", i);
	/* Activity APIs are registered into the runtime table by _PostLink,
	 * not exported in lib_PartsEngine.functions. Call those C entries
	 * directly, without running v14 bytecode as another VM version. The
	 * former fixed bound remains for non-v14 versions, including v15. */
	int lib = bu_lib("PartsEngine");
	int create_fn = sf_find(lib, "CreateActivity", 1);
	int exists_fn = sf_find(lib, "IsExistActivity", 1);
	assert(create_fn >= 0 && exists_fn >= 0 && libraries && libraries[lib]);
	bool (*create)(struct string *) = (bool (*)(struct string *))libraries[lib][create_fn].fun;
	bool (*exists)(struct string *) = (bool (*)(struct string *))libraries[lib][exists_fn].fun;
	assert(create && exists);
	for (int v = 11; v <= 15; v += 2) {
		ain->version = v;
		for (int i = 0; i <= 128; i++) {
			char name[40];
			snprintf(name, sizeof(name), "PriorItem%03d", i);
			struct string *key = cstr_to_string(name);
			ACTCAP_CHECK(create(key) == (i < 128), "v%d capacity index=%d", v, i);
			ACTCAP_CHECK(exists(key) == (i < 128), "v%d capacity existence index=%d", v, i);
			free_string(key);
		}
		struct string *first = cstr_to_string("PriorItem000");
		ACTCAP_CHECK(create(first), "v%d duplicate at old bound", v);
		free_string(first);
		ain->version = version;
		fini();
	}
	actcap_cleanup();
}

static int test_activity_capacity(void)
{
	void (*cases[])(void) = { actcap1_create, actcap2_read, actcap3_release, actcap4_reuse,
		actcap5_controller, actcap6_reset, actcap7_sjis, actcap8_guards };
	int failed = 0;
	for (unsigned i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
		fflush(stdout);
		pid_t pid = fork();
		if (pid == 0) {
			alarm(120);
			actcap_failures = actcap_checks = 0;
			cases[i]();
			printf("  AC%u checks=%d failures=%d\n", i + 1, actcap_checks, actcap_failures);
			fflush(stdout);
			_exit(actcap_failures ? 1 : 0);
		}
		int st = 0;
		bool ok = pid > 0 && waitpid(pid, &st, 0) == pid && WIFEXITED(st) && WEXITSTATUS(st) == 0;
		printf("AC%u %s status=%d\n", i + 1, ok ? "ok" : "FAIL", st);
		failed += !ok;
	}
	printf("ACTIVITY-CAPACITY %s: %d of %zu cases failed\n", failed ? "FAIL" : "PASS",
		failed, sizeof(cases) / sizeof(cases[0]));
	return failed ? 1 : 0;
}
