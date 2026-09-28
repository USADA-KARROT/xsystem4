/* Synthetic pactex resource (no copyrighted game data). Exercise the production
 * static loader through probe-only append wrappers, then the real HLL ffi. */
#include "system4/ex.h"
#include "parts_internal.h"
extern void probe_init_parts_table(void);
extern void pe_v14_activity_prelink(void);
extern bool probe_load_activity_tree(struct string *name, struct ex *ex);

static struct ex_tree at_branch(const char *name, unsigned children)
{
	struct ex_tree t = { .name = cstr_to_string(name), .nr_children = children };
	t.children = xcalloc(children, sizeof(*t.children));
	return t;
}
static struct ex_tree at_leaf(const char *name, struct ex_value value)
{
	struct ex_tree t = { .name = cstr_to_string(name), .is_leaf = true };
	t.leaf.name = cstr_to_string(name);
	t.leaf.value = value;
	return t;
}
static struct ex_tree at_string(const char *key, const char *value)
{
	return at_leaf(key, (struct ex_value){ .type = EX_STRING, .s = cstr_to_string(value) });
}
static struct ex_tree at_int(const char *key, int value)
{
	return at_leaf(key, (struct ex_value){ .type = EX_INT, .i = value });
}
static struct ex_tree at_zero_surface(const char *key)
{
	struct ex_list *area = xcalloc(1, sizeof(*area));
	area->nr_items = 4;
	area->items = xcalloc(area->nr_items, sizeof(*area->items));
	for (unsigned i = 0; i < area->nr_items; i++)
		area->items[i].value = (struct ex_value){ .type = EX_INT, .i = 0 };
	return at_leaf(key, (struct ex_value){ .type = EX_LIST, .list = area });
}
static struct ex_tree at_style(const char *key, int size, int char_space, int line_space)
{
	struct ex_tree t = at_branch(key, 3);
	t.children[0] = at_int("\xa5\xd5\xa5\xa9\xa5\xf3\xa5\xc8\xa5\xb5\xa5\xa4\xa5\xba", size);
	t.children[1] = at_int("\xd7\xd6\xe9\x67\xb8\xf4", char_space);
	t.children[2] = at_int("\xd0\xd0\xe9\x67\xb8\xf4", line_space);
	return t;
}
#define AT_TYPE "\xb2\xbf\xbc\xfe\xa5\xbf\xa5\xa4\xa5\xd7"
#define AT_INFO "\xb7\x4e\xee\x90\x84\x65\xc7\xe9\x88\xf3"
static struct ex_tree at_component(void)
{
	struct ex_tree t = at_branch("FixtureText", 1);
	struct ex_tree *info = &t.children[0];
	*info = at_branch(AT_INFO, 5);
	info->children[0] = at_string(AT_TYPE, "\xb5\xcd\xb5\xc8\xbc\x89\xb2\xbf\xbc\xfe");
	/* Deliberately out of order, with an unrelated branch before normal. */
	info->children[1] = at_branch("\xa5\xaa\xa5\xf3\xd6\xb8\xe1\x98\xa0\xee\x91\x42", 1);
	info->children[1].children[0] = at_string(AT_TYPE, "\xa3\xc3\xa3\xc7\xb2\xbf\xbc\xfe");
	info->children[2] = at_branch("UnrelatedMetadata", 0);
	info->children[3] = at_branch("\xa5\xad\xa9\x60\xa5\xc0\xa5\xa6\xa5\xf3\xa0\xee\x91\x42", 1);
	info->children[3].children[0] = at_string(AT_TYPE, "\xa3\xc3\xa3\xc7\xb2\xbf\xbc\xfe");
	info->children[4] = at_branch("\xc6\xd5\xcd\xa8\xa0\xee\x91\x42", 5);
	struct ex_tree *normal = &info->children[4];
	normal->children[0] = at_string(AT_TYPE, "\xce\xc4\xb1\xbe\xb2\xbf\xbc\xfe");
	normal->children[1] = at_string("\xce\xc4\xb1\xbe", "");
	/* Ruby comes first to detect an accidental recursive font-size lookup. */
	normal->children[2] = at_style("\xa5\xeb\xa5\xd3\xd1\x62\xef\x97", 8, 0, 3);
	normal->children[3] = at_style("\xce\xc4\xb1\xbe\xd1\x62\xef\x97", 32, -2, 12);
	/* A surface setter must preserve the newly constructed text state. */
	normal->children[4] = at_zero_surface("\xa5\xb5\xa9\x60\xa5\xd5\xa5\xa7\xa5\xa4\xa5\xb9\xa5\xa8\xa5\xea\xa5\xa2");
	return t;
}
static struct ex_tree at_collision_component(void)
{
	struct ex_tree t = at_branch("FixtureStateCollision", 1);
	struct ex_tree *info = &t.children[0];
	*info = at_branch(AT_INFO, 4);
	info->children[0] = at_string(AT_TYPE, "\xb5\xcd\xb5\xc8\xbc\x89\xb2\xbf\xbc\xfe");
	/* The named down state appears first, while legacy ordinal slot 3 later
	 * contains a CG surface. A one-pass mixed loader destroys the text. */
	info->children[1] = at_branch("\xa5\xad\xa9\x60\xa5\xc0\xa5\xa6\xa5\xf3\xa0\xee\x91\x42", 3);
	struct ex_tree *down = &info->children[1];
	down->children[0] = at_string(AT_TYPE, "\xce\xc4\xb1\xbe\xb2\xbf\xbc\xfe");
	down->children[1] = at_style("\xa5\xeb\xa5\xd3\xd1\x62\xef\x97", 8, 0, 3);
	down->children[2] = at_style("\xce\xc4\xb1\xbe\xd1\x62\xef\x97", 28, -1, 10);
	info->children[2] = at_branch("UnrelatedSecond", 0);
	info->children[3] = at_branch("UnknownThirdWithSurface", 1);
	info->children[3].children[0] = at_zero_surface("\x83\x54\x81\x5b\x83\x74\x83\x46\x83\x43\x83\x58\x83\x47\x83\x8a\x83\x41");
	return t;
}
static struct ex *at_resource(void)
{
	struct ex *e = xcalloc(1, sizeof(*e));
	e->nr_blocks = 1;
	e->blocks = xcalloc(1, sizeof(*e->blocks));
	e->blocks[0].name = cstr_to_string("FixtureActivity");
	e->blocks[0].val.type = EX_TREE;
	struct ex_tree *top = xcalloc(1, sizeof(*top));
	*top = at_branch("FixtureActivity", 1);
	e->blocks[0].val.tree = top;
	top->children[0] = at_branch("FixtureRoot", 1);
	struct ex_tree *parts = &top->children[0].children[0];
	*parts = at_branch("\xb2\xbf\xbc\xfe", 3);
	parts->children[0] = at_component();
	parts->children[1] = at_branch("UnknownWidget", 1);
	parts->children[1].children[0] = at_branch(AT_INFO, 1);
	parts->children[1].children[0].children[0] = at_string(AT_TYPE, "UnknownType");
	parts->children[2] = at_collision_component();
	return e;
}
static int at_call(int lib, const char *name, int nargs, const int *args)
{
	int f = sf_find(lib, name, nargs); assert(f >= 0);
	int sp = stack_ptr;
	for (int i = 0; i < nargs; i++) {
		/* The real ffi consumes by-value string argument owners. */
		if (ain->libraries[lib].functions[f].arguments[i].type.data == AIN_STRING) heap_ref(args[i]);
		stack_push(args[i]);
	}
	hll_call(lib, f, 0);
	assert(stack_ptr == sp + 1);
	return stack_pop().i;
}
static int test_activity_text(void)
{
	probe_init_parts_table();
	/* init_probe links HLLs without module startup; production PostLink registers
	 * the v14 activity functions at this point. Run that same registration. */
	pe_v14_activity_prelink();
	struct ex *e = at_resource();
	struct string *name = cstr_to_string("ActivityTextFixture");
	bool loaded = probe_load_activity_tree(name, e);
	free_string(name); ex_free(e);
	if (!loaded) { puts("ACTIVITY-TEXT FAIL synthetic loader returned false"); return 86; }
	int lib = ain_get_library(ain, "PartsEngine"), fails = 0;
	int as = sf_str("ActivityTextFixture", -1), ns = sf_str("FixtureText", -1);
	int args[] = {as, ns};
	int exists = at_call(lib, "IsExistActivityPartsByName", 2, args);
	int number = at_call(lib, "GetActivityPartsNumber", 2, args);
	printf("ACTIVITY-TEXT lookup exists=%d number=%d expected=present\n", exists, number);
	if (!exists || number < 0) { puts("ACTIVITY-TEXT FAIL named lookup did not reach type check"); return 85; }
	for (int state = 1; state <= 3; state++) {
		int type_args[] = {number, state};
		int got = at_call(lib, "GetComponentType", 2, type_args);
		int want = state == 1 ? 21 : 19;
		printf("ACTIVITY-TEXT state=%d type=%d want=%d %s\n", state, got, want, got == want ? "ok" : "FAIL");
		fails += got != want;
	}
	struct parts *p = parts_try_get(number);
	if (p && p->states[0].type == PARTS_TEXT) {
		struct parts_text *t = &p->states[0].text;
		bool ok = t->ts.size == 32 && t->ts.font_spacing == -2 && t->line_space == 12;
		printf("ACTIVITY-TEXT style size=%g space=%g line=%d want=32,-2,12 %s\n", t->ts.size, t->ts.font_spacing, t->line_space, ok ? "ok" : "FAIL");
		fails += !ok;
	} else { puts("ACTIVITY-TEXT style normal engine state is not PARTS_TEXT FAIL"); fails++; }
	int collision_name = sf_str("FixtureStateCollision", -1);
	int collision_lookup[] = {as, collision_name};
	int collision_number = at_call(lib, "GetActivityPartsNumber", 2, collision_lookup);
	int collision_args[] = {collision_number, 3};
	int collision_type = at_call(lib, "GetComponentType", 2, collision_args);
	struct parts *collision = collision_number >= 0 ? parts_try_get(collision_number) : NULL;
	bool collision_ok = collision_type == 21 && collision && collision->states[2].type == PARTS_TEXT;
	if (collision_ok) {
		struct parts_text *text = &collision->states[2].text;
		collision_ok = text->ts.size == 28 && text->ts.font_spacing == -1 && text->line_space == 10;
	}
	printf("ACTIVITY-TEXT ordinal collision down type=%d want=21 main-style=28,-1,10 %s\n",
		collision_type, collision_ok ? "ok" : "FAIL");
	fails += !collision_ok;
	heap_unref(collision_name);
	int unknown = sf_str("UnknownWidget", -1); args[1] = unknown;
	int un = at_call(lib, "GetActivityPartsNumber", 2, args);
	int unknown_args[] = {un, 1};
	int raw = at_call(lib, "GetComponentType", 2, unknown_args);
	printf("ACTIVITY-TEXT unknown type=%d want=0 %s\n", raw, raw == 0 ? "ok" : "FAIL");
	fails += raw != 0;
	/* Preserve the old v14 raw fallback for invalid states. This tests our
	 * compatibility boundary, not an asserted original-EXE definition. */
	for (int state = 0; state <= 4; state += 4) {
		int invalid_args[] = {number, state};
		int got = at_call(lib, "GetComponentType", 2, invalid_args);
		int want = p->component_type;
		printf("ACTIVITY-TEXT invalid-state compatibility state=%d type=%d raw=%d %s\n", state, got, want, got == want ? "ok" : "FAIL");
		fails += got != want;
	}

	/* A loader-recognized CG state must follow its actual engine transition. */
	struct string *empty = cstr_to_string("");
	bool set_text = PE_SetText(number, empty, 2);
	free_string(empty);
	int changed_args[] = {number, 2};
	int changed_type = at_call(lib, "GetComponentType", 2, changed_args);
	bool changed_ok = set_text && p->states[1].type == PARTS_TEXT && changed_type == 21;
	printf("ACTIVITY-TEXT recognized CG->empty-text type=%d want=21 %s\n", changed_type, changed_ok ? "ok" : "FAIL");
	fails += !changed_ok;

	/* An explicit raw widget keeps its taxonomy even if a style setter
	 * incidentally allocates a text state. It was never loader-recognized. */
	PE_SetComponentType(un, 19, 1);
	bool set_style = PE_SetTextCharSpace(un, 5, 1);
	int raw_after_style = at_call(lib, "GetComponentType", 2, unknown_args);
	bool raw_ok = set_style && parts_try_get(un)->states[0].type == PARTS_TEXT && raw_after_style == 19;
	printf("ACTIVITY-TEXT unmarked raw19 after text-style type=%d want=19 %s\n", raw_after_style, raw_ok ? "ok" : "FAIL");
	fails += !raw_ok;

	/* The prior-version component taxonomy stays on its existing code path.
	 * No AIN is reinterpreted/executed while this selector is changed. */
	int saved_version = ain->version;
	ain->version = 13;
	PE_SetComponentType(un, 11, 1);
	int legacy_cg = PE_GetComponentType(un, 1);
	PE_SetComponentType(un, 13, 1);
	int legacy_text = PE_GetComponentType(un, 1);
	int legacy_invalid = PE_GetComponentType(un, 0);
	ain->version = saved_version;
	bool legacy_ok = legacy_cg == 11 && legacy_text == 13 && legacy_invalid == -1;
	printf("ACTIVITY-TEXT legacy-v13 CG=%d text=%d invalid=%d want=11,13,-1 %s\n", legacy_cg, legacy_text, legacy_invalid, legacy_ok ? "ok" : "FAIL");
	fails += !legacy_ok;
	int setter = sf_find(lib, "SetComponentType", 3);
	assert(setter >= 0);
	int saved_sp = stack_ptr;
	stack_push(number); stack_push(7); stack_push(2);
	hll_call(lib, setter, 0);
	assert(stack_ptr == saved_sp);
	for (int state = 1; state <= 3; state++) {
		int explicit_args[] = {number, state};
		int explicit_type = at_call(lib, "GetComponentType", 2, explicit_args);
		printf("ACTIVITY-TEXT explicit setter clears state=%d type=%d want=7 %s\n", state, explicit_type, explicit_type == 7 ? "ok" : "FAIL");
		fails += explicit_type != 7;
	}
	heap_unref(as); heap_unref(ns); heap_unref(unknown);
	printf("ACTIVITY-TEXT %s failures=%d (production loader + real ffi, no GL/assets)\n", fails ? "FAIL" : "PASS", fails);
	return fails ? 86 : 0;
}
