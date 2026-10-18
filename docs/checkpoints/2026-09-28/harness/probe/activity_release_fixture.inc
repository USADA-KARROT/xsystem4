/* activity-release: ReleaseActivity releases the parts of the activity's own
 * table, not their subtrees.
 *
 * The original (PartsEngine jump table 26, 0x540050 -> 0x55e400) walks the
 * activity's table (0x55e450..0x55e4ed): for each number it writes the
 * delegate index (0x56d620) to the erase list, skips a controller root
 * (0x53d660 reads +0x230) and releases that number alone (0x539c80,
 * 0x579160). Releasing a parts (0x579160 -> 0x53aa80 -> 0x53b270) moves each
 * of its children under the root at the top of its tree (0x53b180: +0x13c
 * becomes the root's number, the root's child list +0xbc gets the child) and
 * takes it out of its parent's list (0x550df0). So a parts somebody else hung
 * under an activity's parts -- the root of another activity, a parts the
 * script made -- outlives that activity's release, with its own subtree.
 *
 * The script relies on it in battle: the action panel's base hangs the skill
 * panel activity under itself (BattleActionPanel@ShowCurrentPanel); when the
 * battle ends, SceneBattle releases the action panel first and the skill
 * panel after it. AFL_Activity_Release walks the skill panel's tree
 * (activity::detail::CallUserComponentReleaseEventWithChild) for its user
 * components (the five SkillSelector) and removes them from their manager;
 * only then does activity::detail::Release free its name. Before: the action
 * panel's ReleaseActivity released the whole subtree, the skill panel's own
 * walk found an empty tree, its five CUserComponentSet entries stayed, and
 * the next battle's skill panel, loaded under the name that was free again,
 * got no SkillSelector at all (CUserComponentManager@SetComponent skips an
 * activity and parts name it knows, with a warning): the script used the old
 * battle's selectors, whose base rectangle was gone (number 0), and their
 * buttons hung at the layer's origin, above everything and outside the
 * tutorial's scaled scene.
 *
 *   AR1 two activities, the second's root hung under a parts of the first,
 *       and a parts the script made (with its own child) under another:
 *       ReleaseActivity of the first releases exactly its own parts; the
 *       others stay, each whose parent went hangs under the layer's root
 *       (with its subtree; placed from the root after an update), the rest
 *       keep their parents; the erase list holds the first's delegate indices
 *       only. Then the second's release takes its own parts.
 *   AR2 real bytecode: activity::detail::AddUserComponent, two
 *       activity::detail::Load (the inner one with two SkillSelector user
 *       components), the inner root hung under a parts of the outer one,
 *       AFL_Activity_Release of the outer then the inner one (the battle's
 *       order): the SkillSelector manager has no entry left and the inner
 *       name is free; loading it again under that name creates two new
 *       components whose base rectangle hangs under its placeholder and is
 *       placed through the placeholder's parents (offset and scale, as the
 *       tutorial's scene parent moves them).
 * Guards, which pass before the change as well:
 *   AR3 a single activity: all its parts go, nested ones too; the erase list
 *       holds each existing table parts once (the root's name aliases are
 *       not counted twice); the name is free.
 *   AR4 the parts of an activity released with their layer (EraseLayer)
 *       are not made again by ReleaseActivity, which still frees the name.
 * Expected values come from a model of the tree built here (owner and parent
 * of each node, local offsets and scales), not from the engine. The
 * activities are synthetic pactex fed to the production loader by a
 * probe-only ReadActivityFile. No game assets or GL are used.
 */

static int ar_failures;
#define AR_CHECK(c, ...) do { if (!(c)) { printf("  FAIL: "); printf(__VA_ARGS__); printf("\n"); ar_failures++; } } while (0)

#define AR_ROOT "\xa5\xeb\xa9\x60\xa5\xc8\xb2\xbf\xbc\xfe"	/* ルート部件 (CN pactex root name) */

/* AROuter: root { Panel, Side }; ARInner: root { Base { Slot1, Slot2 } },
 * the slots SkillSelector user components; ARSolo: root { A { B }, C }. */
static bool ar_read_activity_file(struct string *name, struct string *filename, possibly_unused bool edit)
{
	struct ex *e = xcalloc(1, sizeof(*e));
	e->nr_blocks = 1;
	e->blocks = xcalloc(1, sizeof(*e->blocks));
	e->blocks[0].name = cstr_to_string("ActivityRelease");
	e->blocks[0].val.type = EX_TREE;
	struct ex_tree *top = xcalloc(1, sizeof(*top));
	*top = bu_branch("ActivityRelease", 1);
	e->blocks[0].val.tree = top;
	if (!strcmp(filename->text, "AROuter")) {
		top->children[0] = bu_comp(AR_ROOT, BU_LAYOUT, 0, 2);
		struct ex_tree *k = &top->children[0].children[1];
		k->children[0] = bu_comp("Panel", BU_LOW, 0, 0);
		k->children[1] = bu_comp("Side", BU_LOW, 0, 0);
	} else if (!strcmp(filename->text, "ARInner")) {
		top->children[0] = bu_comp(AR_ROOT, BU_LAYOUT, 0, 1);
		struct ex_tree *k = &top->children[0].children[1];
		k->children[0] = bu_comp("Base", BU_LOW, 0, 2);
		struct ex_tree *s = &k->children[0].children[1];
		for (int i = 0; i < 2; i++) {
			s->children[i] = bu_comp(i ? "Slot2" : "Slot1", BU_UC, 2, 0);
			struct ex_tree *info = &s->children[i].children[0];
			info->children[1] = bu_sleaf(BU_UCNAME, "SkillSelector");
			info->children[2] = bu_leaf(BU_DATA, (struct ex_value){ .type = EX_LIST,
				.list = bu_slist(2, (const char *[]){ "LineHeight", i ? "111" : "53" }) });
		}
	} else {
		top->children[0] = bu_comp(AR_ROOT, BU_LAYOUT, 0, 2);
		struct ex_tree *k = &top->children[0].children[1];
		k->children[0] = bu_comp("A", BU_LOW, 0, 1);
		k->children[0].children[1].children[0] = bu_comp("B", BU_LOW, 0, 0);
		k->children[1] = bu_comp("C", BU_LOW, 0, 0);
	}
	bool ok = probe_load_activity_tree(name, e);
	ex_free(e);
	return ok;
}

static void ar_setup(void)
{
	lr_setup();
	lr_replace_hll("PartsEngine", "ReadActivityFile", (void *)ar_read_activity_file);
}

static int ar_num(const char *act, const char *parts)
{
	return BU_PE("GetActivityPartsNumber", BU_I(sf_str(act, -1)), BU_I(sf_str(parts, -1))).i;
}

static bool ar_load_hll(const char *name, const char *file)
{
	return BU_PE("ReadActivityFile", BU_I(sf_str(name, -1)), BU_I(sf_str(file, -1)), BU_I(0)).i;
}

static int ar_int_list(void)
{
	int list = heap_alloc_page(alloc_page(ARRAY_PAGE, AIN_ARRAY_INT, 0));
	heap[list].page->array.rank = 1;
	return list;
}

static int ar_list_len(int list)
{
	struct page *p = heap[list].page;
	return p ? p->nr_vars : 0;
}

static int ar_list_at(int list, int i)
{
	struct page *p = heap[list].page;
	return p && i < p->nr_vars ? p->values[i].i : -999;
}

static Point ar_gpos(int no)
{
	struct parts *p = parts_try_get(no);
	return p ? p->global.pos : (Point){ -9999, -9999 };
}

static bool ar_near(float a, float b) { return fabsf(a - b) < 0.01f; }

/* --- AR1 --- */

/* A node of the model: its number, the activity that owns it (0 none,
 * 1 outer, 2 inner) and the number of its parent before the release. */
struct ar_node { const char *what; int no, owner, parent; };

static void ar_own_parts_only(void)
{
	ar_setup();
	int layer = ci_add(-1);
	AR_CHECK(ar_load_hll("AR1O", "AROuter") && ar_load_hll("AR1I", "ARInner"), "loading failed");
	int o_root = ar_num("AR1O", AR_ROOT), o_panel = ar_num("AR1O", "Panel"), o_side = ar_num("AR1O", "Side");
	int i_root = ar_num("AR1I", AR_ROOT), i_base = ar_num("AR1I", "Base");
	int i_s1 = ar_num("AR1I", "Slot1"), i_s2 = ar_num("AR1I", "Slot2");
	const int X = 9711, Y = 9712;	/* parts the script makes by number */
	parts_get(X);
	parts_get(Y);
	lr_set_parent(X, o_side);
	lr_set_parent(Y, X);
	lr_set_parent(i_root, o_panel);	/* BattleActionPanel@ShowCurrentPanel: under PanelBase */
	PE_SetPos(o_panel, 100, 200);
	PE_SetPos(i_root, 10, 20);
	PE_SetPos(i_base, 1, 2);
	PE_SetPos(X, 5, 6);
	PE_SetDelegateIndex(o_root, 31);
	PE_SetDelegateIndex(o_panel, 32);
	PE_SetDelegateIndex(o_side, 33);
	PE_SetDelegateIndex(i_root, 41);
	PE_SetDelegateIndex(i_base, 42);
	PE_SetDelegateIndex(X, 51);
	PE_UpdateComponent(0);
	Point g = ar_gpos(i_base);
	AR_CHECK(g.x == 111 && g.y == 222, "before: the inner base at (%d,%d), want (111,222)", g.x, g.y);

	struct ar_node nodes[] = {
		{ "outer root", o_root, 1, layer }, { "outer Panel", o_panel, 1, o_root }, { "outer Side", o_side, 1, o_root },
		{ "inner root", i_root, 2, o_panel }, { "inner Base", i_base, 2, i_root },
		{ "inner Slot1", i_s1, 2, i_base }, { "inner Slot2", i_s2, 2, i_base },
		{ "script X", X, 0, o_side }, { "script Y", Y, 0, X },
	};
	const int n = sizeof(nodes) / sizeof(nodes[0]);
	for (int i = 0; i < n; i++)
		AR_CHECK(lr_parent(nodes[i].no) == nodes[i].parent, "before: %s's parent %d, want %d",
			 nodes[i].what, lr_parent(nodes[i].no), nodes[i].parent);

	int list = ar_int_list();
	bool ok = BU_PE("ReleaseActivity", BU_I(sf_str("AR1O", -1)), BU_I(list)).i;
	AR_CHECK(ok, "ReleaseActivity(AR1O) failed");
	/* The model: the outer activity's own parts go; a node whose parent
	 * went hangs under the root at the top of its tree (the layer's). */
	for (int i = 0; i < n; i++) {
		bool owned = nodes[i].owner == 1;
		bool exists = parts_try_get(nodes[i].no) != NULL;
		AR_CHECK(exists == !owned, "%s (%d) %s, want %s", nodes[i].what, nodes[i].no,
			 exists ? "stayed" : "went", owned ? "gone" : "kept");
		if (owned || !exists)
			continue;
		int want = nodes[i].parent;
		for (int j = 0; j < n; j++)
			if (nodes[j].no == want && nodes[j].owner == 1)
				want = layer;
		AR_CHECK(lr_parent(nodes[i].no) == want && lr_layer_of(nodes[i].no) == layer,
			 "after: %s's parent %d, layer %d; want %d, %d", nodes[i].what, lr_parent(nodes[i].no),
			 lr_layer_of(nodes[i].no), want, layer);
	}
	AR_CHECK(lr_nchild(i_root) == 1 && lr_nchild(i_base) == 2 && lr_nchild(X) == 1,
		 "after: children of the inner root %d, base %d, X %d; want 1, 2, 1", lr_nchild(i_root), lr_nchild(i_base), lr_nchild(X));
	AR_CHECK(ar_list_len(list) == 3 && ar_list_at(list, 0) == 31 && ar_list_at(list, 1) == 32 && ar_list_at(list, 2) == 33,
		 "erase list %d entries (%d %d %d ...); want 31 32 33", ar_list_len(list), ar_list_at(list, 0),
		 ar_list_at(list, 1), ar_list_at(list, 2));
	PE_UpdateComponent(0);
	Point gr = ar_gpos(i_root), gb = ar_gpos(i_base), gx = ar_gpos(X);
	AR_CHECK(gr.x == 10 && gr.y == 20 && gb.x == 11 && gb.y == 22 && gx.x == 5 && gx.y == 6,
		 "after an update: inner root (%d,%d), base (%d,%d), X (%d,%d); want (10,20) (11,22) (5,6)",
		 gr.x, gr.y, gb.x, gb.y, gx.x, gx.y);
	AR_CHECK(!lr_activity_exists("AR1O") && lr_activity_exists("AR1I"), "names: AR1O %d, AR1I %d; want 0 1",
		 lr_activity_exists("AR1O"), lr_activity_exists("AR1I"));
	/* the inner activity's own release */
	int list2 = ar_int_list();
	BU_PE("ReleaseActivity", BU_I(sf_str("AR1I", -1)), BU_I(list2));
	AR_CHECK(!parts_try_get(i_root) && !parts_try_get(i_base) && !parts_try_get(i_s1) && !parts_try_get(i_s2)
		 && parts_try_get(X) && parts_try_get(Y), "the inner release: root %d base %d slots %d %d, X %d Y %d; want 0 0 0 0 1 1",
		 !!parts_try_get(i_root), !!parts_try_get(i_base), !!parts_try_get(i_s1), !!parts_try_get(i_s2),
		 !!parts_try_get(X), !!parts_try_get(Y));
	AR_CHECK(ar_list_len(list2) == 4 && ar_list_at(list2, 0) == 41 && ar_list_at(list2, 1) == 42,
		 "inner erase list %d entries (%d %d); want 4 (41 42 and the slots')", ar_list_len(list2),
		 ar_list_at(list2, 0), ar_list_at(list2, 1));
	printf("AR1 ReleaseActivity takes its own parts only: %s\n", ar_failures ? "FAIL" : "ok");
}

/* --- AR2 --- */

/* The CUserComponentSet entries of the manager of a user component name,
 * read from activity::detail::g_UserComponentManagerList; -1 without one. */
static int ar_uc_entries(const char *ucname, const char *activity, int *nr_named)
{
	int g = ci_global("activity::detail::g_UserComponentManagerList");
	int slot = heap[global_page_slot].page->values[g].i;
	struct page *list = heap_index_valid(slot) ? heap[slot].page : NULL;
	if (nr_named)
		*nr_named = 0;
	if (!list)
		return -1;
	int step = list->array.elem_slots > 0 ? list->array.elem_slots : 1;
	for (int i = 0; i < list->nr_vars; i += step) {
		int ms = list->values[i].i;
		struct page *m = heap_index_valid(ms) ? heap[ms].page : NULL;
		if (!m || m->type != STRUCT_PAGE)
			continue;
		struct ain_struct *st = &ain->structures[m->index];
		int name_i = -1, list_i = -1;
		for (int k = 0; k < st->nr_members; k++) {
			if (!strcmp(st->members[k].name, "userComponentName")) name_i = k;
			if (!strcmp(st->members[k].name, "userComponentList")) list_i = k;
		}
		if (name_i < 0 || list_i < 0)
			continue;
		struct string *s = heap_get_string(m->values[name_i].i);
		if (!s || strcmp(s->text, ucname))
			continue;
		int ls = m->values[list_i].i;
		struct page *l = heap_index_valid(ls) ? heap[ls].page : NULL;
		if (!l)
			return 0;
		int lstep = l->array.elem_slots > 0 ? l->array.elem_slots : 1;
		int count = 0;
		for (int j = 0; j < l->nr_vars; j += lstep) {
			count++;
			struct page *set = heap_index_valid(l->values[j].i) ? heap[l->values[j].i].page : NULL;
			if (!nr_named || !set || set->type != STRUCT_PAGE)
				continue;
			struct string *an = heap_get_string(set->values[0].i);	/* <ActivityName> */
			if (an && !strcmp(an->text, activity))
				(*nr_named)++;
		}
		return count;
	}
	return -1;
}

static int ar_load_script(const char *file, char *name_out, size_t size)
{
	int dg = heap_alloc_page(alloc_page(DELEGATE_PAGE, 0, 0));
	int r = ci_call("activity::detail::Load", 2, (int[]){ sf_str(file, -1), dg });
	struct string *s = heap_get_string(r);
	snprintf(name_out, size, "%s", s ? s->text : "");
	return r;
}

static void ar_user_components(void)
{
	ar_setup();
	int g = ci_global("activity::detail::g_UserComponentManagerList");
	if (heap[global_page_slot].page->values[g].i < 0)
		heap[global_page_slot].page->values[g] = variable_initval(ain->globals[g].type.data);
	/* AddUserComponent scans every function name of the AIN */
	probe_step_budget = 200000000UL;
	ci_call("activity::detail::AddUserComponent", 0, NULL);
	AR_CHECK(ar_uc_entries("SkillSelector", "", NULL) == 0, "the SkillSelector manager: %d entries, want 0",
		 ar_uc_entries("SkillSelector", "", NULL));
	int layer = ci_add(-1);
	char outer[64], inner[64];
	ar_load_script("AROuter", outer, sizeof(outer));
	ar_load_script("ARInner", inner, sizeof(inner));
	int named = 0;
	int entries = ar_uc_entries("SkillSelector", inner, &named);
	AR_CHECK(!strcmp(outer, "AROuter") && !strcmp(inner, "ARInner") && entries == 2 && named == 2,
		 "after loading: names %s %s, %d SkillSelector entries (%d of %s); want AROuter ARInner, 2 (2)",
		 outer, inner, entries, named, inner);
	int panel = ar_num(outer, "Panel"), i_root = ar_num(inner, AR_ROOT), slot1 = ar_num(inner, "Slot1");
	AR_CHECK(lr_nchild(slot1) == 1, "the first placeholder has %d children after loading, want 1 (the base rectangle)",
		 lr_nchild(slot1));
	lr_set_parent(i_root, panel);	/* the battle hangs the skill panel under the action panel */
	/* SceneBattle's teardown: the action panel, then the skill panel */
	ci_call("AFL_Activity_Release", 1, (int[]){ sf_str(outer, -1) });
	ci_call("AFL_Activity_Release", 1, (int[]){ sf_str(inner, -1) });
	entries = ar_uc_entries("SkillSelector", inner, &named);
	AR_CHECK(entries == 0, "after both releases: %d SkillSelector entries (%d of %s); want 0", entries, named, inner);
	AR_CHECK(!strcmp(lr_free_name("ARInner"), "ARInner"), "the free name after both releases: %s, want ARInner",
		 lr_free_name("ARInner"));
	/* the next battle */
	char again[64];
	ar_load_script("ARInner", again, sizeof(again));
	entries = ar_uc_entries("SkillSelector", again, &named);
	int root2 = ar_num(again, AR_ROOT), base2 = ar_num(again, "Base"), s1 = ar_num(again, "Slot1"), s2 = ar_num(again, "Slot2");
	AR_CHECK(!strcmp(again, "ARInner") && entries == 2 && named == 2,
		 "loading again: name %s, %d entries (%d of it); want ARInner, 2 (2)", again, entries, named);
	int r1 = lr_nchild(s1) ? lr_child(s1, 0) : 0, r2 = lr_nchild(s2) ? lr_child(s2, 0) : 0;
	AR_CHECK(r1 > 0 && r2 > 0 && r1 != r2 && lr_parent(r1) == s1 && lr_parent(r2) == s2,
		 "the new placeholders' base rectangles %d %d (parents %d %d); want two, under %d %d",
		 r1, r2, r1 > 0 ? lr_parent(r1) : -1, r2 > 0 ? lr_parent(r2) : -1, s1, s2);
	/* placed through the scene parent, as Tutorial::MoveSceneParts moves it */
	PE_SetPos(root2, 96, 28);
	PE_SetPartsMagX(root2, 0.85f);
	PE_SetPartsMagY(root2, 0.85f);
	PE_SetPos(base2, 118, 411);
	PE_SetPos(s2, 0, 58);
	PE_UpdateComponent(0);
	struct parts *p2 = r2 > 0 ? parts_try_get(r2) : NULL;
	/* model: root (96,28) scale 0.85, then base (118,411), slot (0,58) */
	float wx = 96 + 0.85f * (118 + 0), wy = 28 + 0.85f * (411 + 58);
	AR_CHECK(p2 && abs(p2->global.pos.x - (int)lroundf(wx)) <= 1 && abs(p2->global.pos.y - (int)lroundf(wy)) <= 1
		 && ar_near(p2->global.scale.x, 0.85f) && ar_near(p2->global.scale.y, 0.85f) && lr_layer_of(r2) == layer,
		 "the second rectangle at (%d,%d) scale %.3f,%.3f layer %d; want (%.0f,%.0f) 0.85 on %d",
		 p2 ? p2->global.pos.x : -1, p2 ? p2->global.pos.y : -1, p2 ? p2->global.scale.x : 0.f,
		 p2 ? p2->global.scale.y : 0.f, r2 > 0 ? lr_layer_of(r2) : -1, wx, wy, layer);
	printf("AR2 user components survive the battle's teardown order: %s\n", ar_failures ? "FAIL" : "ok");
}

/* --- AR3 --- */

static void ar_single(void)
{
	ar_setup();
	ci_add(-1);
	AR_CHECK(ar_load_hll("AR3", "ARSolo"), "loading failed");
	int root = ar_num("AR3", AR_ROOT), a = ar_num("AR3", "A"), b = ar_num("AR3", "B"), c = ar_num("AR3", "C");
	AR_CHECK(lr_parent(b) == a && lr_parent(a) == root && lr_parent(c) == root, "the tree: %d %d %d", lr_parent(b),
		 lr_parent(a), lr_parent(c));
	PE_SetDelegateIndex(root, 7);
	PE_SetDelegateIndex(a, 8);
	PE_SetDelegateIndex(b, 9);
	PE_SetDelegateIndex(c, 10);
	int list = ar_int_list();
	AR_CHECK(BU_PE("ReleaseActivity", BU_I(sf_str("AR3", -1)), BU_I(list)).i, "ReleaseActivity failed");
	AR_CHECK(!parts_try_get(root) && !parts_try_get(a) && !parts_try_get(b) && !parts_try_get(c),
		 "left: root %d A %d B %d C %d", !!parts_try_get(root), !!parts_try_get(a), !!parts_try_get(b), !!parts_try_get(c));
	bool seen[4] = { 0 };
	for (int i = 0; i < ar_list_len(list); i++) {
		int v = ar_list_at(list, i);
		if (v >= 7 && v <= 10) seen[v - 7] = true;
	}
	AR_CHECK(ar_list_len(list) == 4 && seen[0] && seen[1] && seen[2] && seen[3],
		 "erase list %d entries; want 7 8 9 10 once each", ar_list_len(list));
	AR_CHECK(!lr_activity_exists("AR3") && !strcmp(lr_free_name("AR3"), "AR3"), "the name stays taken");
	printf("AR3 a single activity goes whole: %s\n", ar_failures ? "FAIL" : "ok");
}

/* --- AR4 --- */

static void ar_after_layer(void)
{
	ar_setup();
	int layer = ci_add(-1);
	AR_CHECK(ar_load_hll("AR4", "ARSolo"), "loading failed");
	int root = ar_num("AR4", AR_ROOT), b = ar_num("AR4", "B");
	ci_erase(layer);
	AR_CHECK(!parts_try_get(root) && !parts_try_get(b) && lr_activity_exists("AR4"),
		 "after EraseLayer: root %d B %d, name %d; want 0 0 1", !!parts_try_get(root), !!parts_try_get(b),
		 lr_activity_exists("AR4"));
	int list = ar_int_list();
	AR_CHECK(BU_PE("ReleaseActivity", BU_I(sf_str("AR4", -1)), BU_I(list)).i, "ReleaseActivity failed");
	AR_CHECK(!parts_try_get(root) && !parts_try_get(b) && ar_list_len(list) == 0 && !lr_activity_exists("AR4"),
		 "after ReleaseActivity: root %d B %d, %d list entries, name %d; want 0 0 0 0", !!parts_try_get(root),
		 !!parts_try_get(b), ar_list_len(list), lr_activity_exists("AR4"));
	printf("AR4 an activity gone with its layer: %s\n", ar_failures ? "FAIL" : "ok");
}

static int ar_fork(const char *name, void (*fn)(void))
{
	fflush(stdout);
	pid_t pid = fork();
	if (pid == 0) {
		ar_failures = 0;
		alarm(120);
		fn();
		fflush(stdout);
		_exit(ar_failures ? 1 : 0);
	}
	int status = 0;
	waitpid(pid, &status, 0);
	bool ok = WIFEXITED(status) && WEXITSTATUS(status) == 0;
	if (!ok)
		printf("%s FAILED (%s %d)\n", name, WIFEXITED(status) ? "exit" : "signal",
		       WIFEXITED(status) ? WEXITSTATUS(status) : WTERMSIG(status));
	return ok ? 0 : 1;
}

static int test_activity_release(void)
{
	static const struct { const char *name; void (*fn)(void); } cases[] = {
		{ "AR1", ar_own_parts_only }, { "AR2", ar_user_components },
		{ "AR3", ar_single }, { "AR4", ar_after_layer },
	};
	int failed = 0, n = sizeof(cases) / sizeof(cases[0]);
	for (int i = 0; i < n; i++)
		failed += ar_fork(cases[i].name, cases[i].fn);
	printf("ACTIVITY-RELEASE %s: %d of %d cases failed\n", failed ? "FAIL" : "PASS", failed, n);
	return failed ? 1 : 0;
}
