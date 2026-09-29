/* base-ui: the base screen's (SceneAzito/SceneHome) missing widgets.
 *
 * activity::detail::Load walks a freshly read activity with NumofChild /
 * GetChild, turns every EPartsType 17 into its user component
 * (GetUserComponentName -> CUserComponentManager), and the components look
 * their parts up by type (CompParts) and name (GetActivityParts). Original
 * semantics (static disassembly only):
 *   - Parts_SetParentPartsNumber links at once (0x58f060 -> 0x58f310 ->
 *     0x550d90); 0 detaches (0x53ab60); an unknown parent is ignored.
 *   - 部件タイプ is indexed in the EPartsType name table (0x4eda70,
 *     0x5b8f90); a low-level widget reports each state's type (0x58b660 ->
 *     widget vt+8), a user component's name is ユーザコンポーネント名
 *     (0x4e50c0, case 694) and its 數據 pairs are Get/SetUserComponentData
 *     (cases 695/696 -> 0x597ff0 / 0x598020).
 *   - GetActivityParts writes the index-th name and number (case 36 ->
 *     0x58a9c0).
 *   - SetButtonEnable / IsButtonEnable (cases 212/213 -> 0x590b70 /
 *     0x590ba0): the widget flag +0xb0, enabled by default (0x5260c0), false
 *     for a missing parts.
 *   - A parent's accumulated scale scales its children's positions (0x4e6d80).
 *   - Array.Add and PushBack share one handler (0x644455): a two-slot
 *     element (0x10003, an interface) keeps both slots.
 *   - MainEXFile.Col answers a list's element count before a table's column
 *     count (0x4b00e0: EX vt+0x2c list, vt+8 = 0x478e50).
 *
 * Cases (each in its own fork):
 *   BU1 a synthetic GBK pactex through the production loader, then the real
 *       HLL declarations: the tree is walkable right after loading, native
 *       types, UC name and data, GetActivityParts outputs.
 *   BU2 Parts_SetParentPartsNumber attach/detach/cycle/unknown parent, and a
 *       scaled parent's child position.
 *   BU3 SetButtonEnable / IsButtonEnable.
 *   BU4 Array.Add/PopBack with 0x10003 elements on an X_A_INIT 0 array, and
 *       the real SceneParentStack@Push/@Pop bytecode.
 *   BU5 MainEXFile.Col on a flat list and a table, and the real
 *       EXHelper::GetStringArray bytecode (Tutorial::GetCgs).
 * Only functions that exist before the fix are referenced; the MainEXFile
 * entry probe_mainex_set is appended by build_probe.py to a copy of the
 * checked-out MainEXFile.c. No game assets or GL are used.
 */
#include <sys/wait.h>
#include <unistd.h>
#include "system4/ex.h"
#include "parts_internal.h"
extern void probe_init_parts_table(void);
extern bool probe_load_activity_tree(struct string *name, struct ex *ex);
extern void probe_mainex_set(struct ex *e);

static int bu_failures;
#define BU_CHECK(c, ...) do { if (!(c)) { printf("  FAIL: "); printf(__VA_ARGS__); printf("\n"); bu_failures++; } } while (0)

static void bu_postlink(void)
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

static int bu_lib(const char *name)
{
	int lib = ain_get_library(ain, name);
	assert(lib >= 0);
	return lib;
}

/* Calls lib.fn(nargs) through its AIN declaration. slots holds the argument
 * stack slots in order (a two-slot argument takes two); string arguments are
 * retained here because the ffi releases by-value strings. Returns the first
 * result slot, or -999 for void. */
static union vm_value bu_call(const char *libname, const char *fn, int nargs, int arg3,
		int nslots, const union vm_value *slots)
{
	int lib = bu_lib(libname), f = sf_find(lib, fn, nargs);
	if (f < 0) { printf("BASE-UI %s.%s/%d not declared\n", libname, fn, nargs); exit(2); }
	struct ain_hll_function *decl = &ain->libraries[lib].functions[f];
	int sp = stack_ptr, k = 0;
	for (int i = 0; i < nargs; i++) {
		if (decl->arguments[i].type.data == AIN_STRING)
			heap_ref(slots[k].i);
		k++;
		if (decl->arguments[i].type.data == AIN_WRAP && decl->arguments[i].type.array_type
		    && decl->arguments[i].type.array_type->data == AIN_INT)
			k++;
		else if (decl->arguments[i].type.data == AIN_HLL_PARAM && (arg3 & 0xFFFF) == 3)
			k++;
	}
	assert(k == nslots);
	for (int i = 0; i < nslots; i++)
		stack_push(slots[i]);
	hll_call(lib, f, arg3);
	union vm_value r = { .i = -999 };
	if (stack_ptr > sp)
		r = stack[sp];
	stack_ptr = sp;
	return r;
}

#define BU_I(v) ((union vm_value){ .i = (v) })
#define BU_F(v) ((union vm_value){ .f = (v) })
#define BU_PE(fn, ...) bu_call("PartsEngine", fn, \
	sizeof((union vm_value[]){ __VA_ARGS__ }) / sizeof(union vm_value), 0, \
	sizeof((union vm_value[]){ __VA_ARGS__ }) / sizeof(union vm_value), (union vm_value[]){ __VA_ARGS__ })

static const char *bu_str(int slot)
{
	return slot > 0 && heap_index_valid(slot) && heap[slot].type == VM_STRING && heap[slot].s
		? heap[slot].s->text : "(null)";
}

/* --- synthetic pactex (GBK spellings; no game data) --- */
#define BU_TYPE "\xb2\xbf\xbc\xfe\xa5\xbf\xa5\xa4\xa5\xd7"          /* 部件タイプ */
#define BU_INFO "\xb7\x4e\xee\x90\x84\x65\xc7\xe9\x88\xf3"          /* 種類別情報 */
#define BU_KIDS "\xd7\xd3\xb2\xbf\xbc\xfe"                          /* 子部件 */
#define BU_LAYOUT "\xa5\xec\xa5\xa4\xa5\xa2\xa5\xa6\xa5\xc8\xa5\xdc\xa5\xc3\xa5\xaf\xa5\xb9"
#define BU_UC "\xa5\xe6\xa9\x60\xa5\xb6\xa5\xb3\xa5\xf3\xa5\xdd\xa9\x60\xa5\xcd\xa5\xf3\xa5\xc8"
#define BU_UCNAME BU_UC "\xc3\xfb"                                   /* ユーザコンポーネント名 */
#define BU_DATA "\x94\xb5\x93\xfe"                                   /* 數據 */
#define BU_BUTTON "\xb0\xb4\xe2\x6f"                                 /* 按鈕 */
#define BU_LOW "\xb5\xcd\xb5\xc8\xbc\x89\xb2\xbf\xbc\xfe"            /* 低等級部件 */
#define BU_NUMERAL "\x94\xb5\xd7\xd6\xb2\xbf\xbc\xfe"                /* 數字部件 */
#define BU_CGDETECT "\xa3\xc3\xa3\xc7\xc5\xd0\xb6\xa8\xb2\xbf\xbc\xfe" /* ＣＧ判定部件 */
#define BU_CG "\xa3\xc3\xa3\xc7\xb2\xbf\xbc\xfe"                     /* ＣＧ部件 */
#define BU_NORMAL "\xc6\xd5\xcd\xa8\xa0\xee\x91\x42"                 /* 普通狀態 */
#define BU_ONCURSOR "\xa5\xaa\xa5\xf3\xd6\xb8\xe1\x98\xa0\xee\x91\x42" /* オン指針狀態 */

static struct ex_tree bu_branch(const char *name, unsigned n)
{
	struct ex_tree t = { .name = cstr_to_string(name), .nr_children = n };
	t.children = xcalloc(n ? n : 1, sizeof(*t.children));
	return t;
}
static struct ex_tree bu_leaf(const char *name, struct ex_value v)
{
	struct ex_tree t = { .name = cstr_to_string(name), .is_leaf = true };
	t.leaf.name = cstr_to_string(name);
	t.leaf.value = v;
	return t;
}
static struct ex_tree bu_sleaf(const char *k, const char *v)
{
	return bu_leaf(k, (struct ex_value){ .type = EX_STRING, .s = cstr_to_string(v) });
}
static struct ex_tree bu_ileaf(const char *k, int v)
{
	return bu_leaf(k, (struct ex_value){ .type = EX_INT, .i = v });
}
static struct ex_list *bu_slist(int n, const char **items)
{
	struct ex_list *l = xcalloc(1, sizeof(*l));
	l->nr_items = n;
	l->items = xcalloc(n, sizeof(*l->items));
	for (int i = 0; i < n; i++)
		l->items[i].value = (struct ex_value){ .type = EX_STRING, .s = cstr_to_string(items[i]) };
	return l;
}
/* A component: name { 種類別情報 { 部件タイプ = type, extra... }, [子部件] }. */
static struct ex_tree bu_comp(const char *name, const char *type, unsigned nextra, unsigned nkids)
{
	struct ex_tree t = bu_branch(name, nkids ? 2 : 1);
	t.children[0] = bu_branch(BU_INFO, 1 + nextra);
	t.children[0].children[0] = bu_sleaf(BU_TYPE, type);
	if (nkids)
		t.children[1] = bu_branch(BU_KIDS, nkids);
	return t;
}

static const char *bu_names[] = { "Footer", "Button1", "NumDay", "Detector", "Legacy", "Group" };

static struct ex *bu_resource(void)
{
	struct ex *e = xcalloc(1, sizeof(*e));
	e->nr_blocks = 1;
	e->blocks = xcalloc(1, sizeof(*e->blocks));
	e->blocks[0].name = cstr_to_string("BaseUiActivity");
	e->blocks[0].val.type = EX_TREE;
	struct ex_tree *top = xcalloc(1, sizeof(*top));
	*top = bu_branch("BaseUiActivity", 1);
	e->blocks[0].val.tree = top;
	top->children[0] = bu_comp("BaseUiRoot", BU_LAYOUT, 0, 6);
	struct ex_tree *kids = &top->children[0].children[1];
	/* Footer: a user component with its own name and data pairs. */
	kids->children[0] = bu_comp("Footer", BU_UC, 2, 0);
	struct ex_tree *info = &kids->children[0].children[0];
	info->children[1] = bu_sleaf(BU_UCNAME, "Fotter");
	info->children[2] = bu_leaf(BU_DATA, (struct ex_value){ .type = EX_LIST,
		.list = bu_slist(4, (const char *[]){ "Button", "A,B,C,D", "Key2", "V2" }) });
	/* Button1: a button without an existing CG. */
	kids->children[1] = bu_comp("Button1", BU_BUTTON, 0, 0);
	/* NumDay: a low-level numeral state. */
	kids->children[2] = bu_comp("NumDay", BU_LOW, 1, 0);
	info = &kids->children[2].children[0];
	info->children[1] = bu_branch(BU_NORMAL, 2);
	info->children[1].children[0] = bu_sleaf(BU_TYPE, BU_NUMERAL);
	info->children[1].children[1] = bu_ileaf("\xe8\xec\x94\xb5", 2); /* 桁數 */
	/* Detector: a CG detection normal state and a CG hover state. */
	kids->children[3] = bu_comp("Detector", BU_LOW, 2, 0);
	info = &kids->children[3].children[0];
	info->children[1] = bu_branch(BU_NORMAL, 1);
	info->children[1].children[0] = bu_sleaf(BU_TYPE, BU_CGDETECT);
	info->children[2] = bu_branch(BU_ONCURSOR, 1);
	info->children[2].children[0] = bu_sleaf(BU_TYPE, BU_CG);
	/* Legacy: a name outside the native table keeps the structural guess. */
	kids->children[4] = bu_comp("Legacy", "UnknownType", 0, 0);
	/* Group: a nested layout box with one child. */
	kids->children[5] = bu_comp("Group", BU_LAYOUT, 0, 1);
	kids->children[5].children[1].children[0] = bu_comp("Inner", BU_LOW, 0, 0);
	return e;
}

static int bu_number(int act, const char *name)
{
	int n = sf_str(name, -1);
	int r = BU_PE("GetActivityPartsNumber", BU_I(act), BU_I(n)).i;
	heap_unref(n);
	return r;
}

/* BU1 */
static void bu_activity(void)
{
	bu_postlink();
	struct ex *e = bu_resource();
	struct string *name = cstr_to_string("BaseUiFixture");
	bool loaded = probe_load_activity_tree(name, e);
	free_string(name); ex_free(e);
	BU_CHECK(loaded, "synthetic activity did not load");
	int act = sf_str("BaseUiFixture", -1);
	int root = bu_number(act, "");
	int nums[6];
	for (int i = 0; i < 6; i++)
		nums[i] = bu_number(act, bu_names[i]);
	/* The tree must be walkable before any UpdateComponent. */
	int kids = BU_PE("NumofChild", BU_I(root)).i;
	BU_CHECK(kids == 6, "NumofChild(root) right after loading = %d, want 6", kids);
	for (int i = 0; i < 6 && i < kids; i++) {
		int c = BU_PE("GetChild", BU_I(root), BU_I(i)).i;
		BU_CHECK(c == nums[i], "GetChild(root, %d) = %d, want %s = %d", i, c, bu_names[i], nums[i]);
	}
	int inner = BU_PE("NumofChild", BU_I(nums[5])).i;
	BU_CHECK(inner == 1, "NumofChild(Group) = %d, want 1", inner);
	/* Native widget types. */
	struct { const char *what; int n, state, want; } types[] = {
		{ "root (layout box)", root, 1, 8 }, { "Footer (user component)", nums[0], 1, 17 },
		{ "Button1 (button)", nums[1], 1, 0 }, { "NumDay normal (numeral)", nums[2], 1, 24 },
		{ "Detector normal (CG detection)", nums[3], 1, 27 }, { "Detector hover (CG)", nums[3], 2, 19 },
		{ "Legacy (unknown name)", nums[4], 1, 0 }, { "Group (layout box)", nums[5], 1, 8 },
	};
	for (unsigned i = 0; i < sizeof(types) / sizeof(types[0]); i++) {
		int got = BU_PE("GetComponentType", BU_I(types[i].n), BU_I(types[i].state)).i;
		BU_CHECK(got == types[i].want, "GetComponentType %s = %d, want %d", types[i].what, got, types[i].want);
	}
	/* A state-type wrapper (CCGDetectionParts@0 sets 27 on state 1) keeps the
	 * low-level widget: the other states keep their types (0x535e20 ->
	 * 0x5653d0). */
	BU_PE("SetComponentType", BU_I(nums[3]), BU_I(27), BU_I(1));
	int det1 = BU_PE("GetComponentType", BU_I(nums[3]), BU_I(1)).i;
	int det2 = BU_PE("GetComponentType", BU_I(nums[3]), BU_I(2)).i;
	BU_CHECK(det1 == 27 && det2 == 19, "SetComponentType(Detector, 27, 1): states %d/%d, want 27/19", det1, det2);
	/* User component name and data. */
	int ucname = BU_PE("GetUserComponentName", BU_I(nums[0])).i;
	BU_CHECK(!strcmp(bu_str(ucname), "Fotter"), "GetUserComponentName(Footer) = \"%s\", want \"Fotter\"", bu_str(ucname));
	heap_unref(ucname);
	struct { const char *key, *want; } data[] = { { "Button", "A,B,C,D" }, { "Key2", "V2" }, { "Missing", "" } };
	for (unsigned i = 0; i < 3; i++) {
		int k = sf_str(data[i].key, -1);
		int v = BU_PE("GetUserComponentData", BU_I(nums[0]), BU_I(k)).i;
		BU_CHECK(!strcmp(bu_str(v), data[i].want), "GetUserComponentData(Footer, %s) = \"%s\", want \"%s\"",
			 data[i].key, bu_str(v), data[i].want);
		heap_unref(v); heap_unref(k);
	}
	int k2 = sf_str("Key2", -1), w = sf_str("W", -1);
	BU_PE("SetUserComponentData", BU_I(nums[0]), BU_I(k2), BU_I(w));
	int v2 = BU_PE("GetUserComponentData", BU_I(nums[0]), BU_I(k2)).i;
	BU_CHECK(!strcmp(bu_str(v2), "W"), "SetUserComponentData then Get = \"%s\", want \"W\"", bu_str(v2));
	heap_unref(v2); heap_unref(k2); heap_unref(w);
	/* GetActivityParts(index, name, wrap<string>, wrap<int>) writes both. */
	int count = BU_PE("NumofActivityParts", BU_I(act)).i;
	int seen = 0;
	for (int i = 0; i < count; i++) {
		int out_name = sf_str("unset", -1);
		int page = heap_alloc_slot(VM_PAGE);
		heap[page].page = alloc_page(ARRAY_PAGE, AIN_ARRAY_INT, 1);
		heap[page].page->values[0].i = -12345;
		int ok = bu_call("PartsEngine", "GetActivityParts", 4, 0, 5,
			(union vm_value[]){ BU_I(i), BU_I(act), BU_I(out_name), BU_I(page), BU_I(0) }).i;
		int number = heap[page].page->values[0].i;
		int by_name = bu_number(act, bu_str(out_name));
		BU_CHECK(ok && number != -12345 && strcmp(bu_str(out_name), "unset") && number == by_name,
			 "GetActivityParts(%d) = %d name \"%s\" number %d, GetActivityPartsNumber(name) = %d",
			 i, ok, bu_str(out_name), number, by_name);
		for (int j = 0; j < 6; j++)
			seen += !strcmp(bu_str(out_name), bu_names[j]);
		heap_unref(out_name); heap_unref(page);
	}
	BU_CHECK(seen == 6, "GetActivityParts listed %d of the 6 named components", seen);
	heap_unref(act);
	printf("BU1 activity tree: %s\n", bu_failures ? "FAIL" : "ok");
}

/* BU2 */
static void bu_parent(void)
{
	bu_postlink();
	const int P = 9301, C = 9302;
	parts_get(P); parts_get(C);
	BU_PE("Parts_SetParentPartsNumber", BU_I(C), BU_I(P));
	int n = BU_PE("NumofChild", BU_I(P)).i, c0 = BU_PE("GetChild", BU_I(P), BU_I(0)).i;
	BU_CHECK(n == 1 && c0 == C, "attach: NumofChild(P) = %d, GetChild(P, 0) = %d, want 1 and %d", n, c0, C);
	BU_PE("Parts_SetParentPartsNumber", BU_I(P), BU_I(C));	/* would make a cycle */
	n = BU_PE("NumofChild", BU_I(C)).i;
	int pp = BU_PE("Parts_GetParentPartsNumber", BU_I(P)).i;
	BU_CHECK(n == 0 && pp == 0, "cycle: NumofChild(C) = %d, parent of P = %d, want 0 and 0", n, pp);
	BU_PE("Parts_SetParentPartsNumber", BU_I(C), BU_I(987654));	/* unknown parent */
	pp = BU_PE("Parts_GetParentPartsNumber", BU_I(C)).i;
	BU_CHECK(pp == P, "unknown parent: parent of C = %d, want %d", pp, P);
	BU_PE("Parts_SetParentPartsNumber", BU_I(C), BU_I(0));
	n = BU_PE("NumofChild", BU_I(P)).i;
	pp = BU_PE("Parts_GetParentPartsNumber", BU_I(C)).i;
	BU_CHECK(n == 0 && pp == 0, "detach: NumofChild(P) = %d, parent of C = %d, want 0 and 0", n, pp);
	/* A scaled parent scales the child's offset from the parent's anchor. */
	BU_PE("Parts_SetParentPartsNumber", BU_I(C), BU_I(P));
	BU_PE("SetComponentPos", BU_I(P), BU_F(100), BU_F(100));
	BU_PE("SetComponentPos", BU_I(C), BU_F(40), BU_F(20));
	BU_PE("SetComponentMagX", BU_I(P), BU_F(0.5f));
	BU_PE("SetComponentMagY", BU_I(P), BU_F(0.5f));
	PE_UpdateComponent(0);
	struct parts *c = parts_get(C);
	BU_CHECK(c->global.pos.x == 120 && c->global.pos.y == 110,
		 "scaled parent: child global pos (%d,%d), want (120,110)", c->global.pos.x, c->global.pos.y);
	printf("BU2 parent links: %s\n", bu_failures ? "FAIL" : "ok");
}

/* BU3 */
static void bu_button(void)
{
	bu_postlink();
	const int B = 9401, U = 9402, MISSING = 9499;
	parts_get(B); parts_get(U);
	int miss = BU_PE("IsButtonEnable", BU_I(MISSING)).i;
	BU_PE("SetButtonEnable", BU_I(MISSING), BU_I(0));
	BU_CHECK(miss == 0 && !parts_try_get(MISSING), "missing parts: IsButtonEnable = %d (want 0), created = %d",
		 miss, !!parts_try_get(MISSING));
	int def = BU_PE("IsButtonEnable", BU_I(B)).i;
	BU_CHECK(def == 1, "default IsButtonEnable = %d, want 1", def);
	BU_PE("SetButtonEnable", BU_I(B), BU_I(0));
	int off = BU_PE("IsButtonEnable", BU_I(B)).i;
	BU_PE("SetButtonEnable", BU_I(B), BU_I(1));
	int on = BU_PE("IsButtonEnable", BU_I(B)).i;
	BU_CHECK(off == 0 && on == 1, "SetButtonEnable false/true -> IsButtonEnable %d/%d, want 0/1", off, on);
	/* The native lookup makes an existing parts a button widget (0x53d990). */
	BU_PE("SetComponentType", BU_I(U), BU_I(17), BU_I(1));
	BU_PE("IsButtonEnable", BU_I(U));
	int t = BU_PE("GetComponentType", BU_I(U), BU_I(1)).i;
	BU_CHECK(t == 0, "IsButtonEnable on a user component widget left type %d, want 0 (button)", t);
	printf("BU3 button enable: %s\n", bu_failures ? "FAIL" : "ok");
}

/* An empty generic array as X_A_INIT 0 leaves it (one slot per element). */
static int bu_empty_generic_array(void)
{
	struct page *a = alloc_page(ARRAY_PAGE, AIN_ARRAY, 0);
	a->array.rank = 1;
	a->array.struct_type = 1;
	int slot = heap_alloc_slot(VM_PAGE);
	heap_set_page(slot, a);
	return slot;
}

/* BU4 */
static void bu_array(void)
{
	int st = ain_get_struct(ain, "CASPos");
	assert(st >= 0);
	int A = vm_construct_struct(st), B = vm_construct_struct(st);
	int arr = bu_empty_generic_array();
	bu_call("Array", "Add", 2, 0x10003, 3, (union vm_value[]){ BU_I(arr), BU_I(A), BU_I(5) });
	bu_call("Array", "Add", 2, 0x10003, 3, (union vm_value[]){ BU_I(arr), BU_I(B), BU_I(7) });
	struct page *p = heap[arr].page;
	BU_CHECK(p->nr_vars == 4 && p->values[0].i == A && p->values[1].i == 5 && p->values[2].i == B && p->values[3].i == 7,
		 "Add x2: %d slots [%d %d %d %d], want 4 [%d 5 %d 7]", p->nr_vars,
		 p->nr_vars > 0 ? p->values[0].i : 0, p->nr_vars > 1 ? p->values[1].i : 0,
		 p->nr_vars > 2 ? p->values[2].i : 0, p->nr_vars > 3 ? p->values[3].i : 0, A, B);
	int n = bu_call("Array", "Numof", 1, 0x10003, 1, (union vm_value[]){ BU_I(arr) }).i;
	BU_CHECK(n == 2, "Numof = %d, want 2", n);
	int refB = HEAP_REF(B);
	bu_call("Array", "PopBack", 1, 0x10003, 1, (union vm_value[]){ BU_I(arr) });
	p = heap[arr].page;
	n = bu_call("Array", "Numof", 1, 0x10003, 1, (union vm_value[]){ BU_I(arr) }).i;
	BU_CHECK(p->nr_vars == 2 && p->values[0].i == A && p->values[1].i == 5 && n == 1 && HEAP_REF(B) == refB - 1,
		 "PopBack: %d slots, Numof %d, ref(B) %d -> %d; want 2 slots [A 5], Numof 1, one ref released",
		 p->nr_vars, n, refB, HEAP_REF(B));
	/* Real bytecode: SceneParentStack@Push / @Pop over its X_A_INIT 0 member. */
	int ss = ain_get_struct(ain, "SceneParentStack");
	int push = ain_get_function(ain, "SceneParentStack@Push"), pop = ain_get_function(ain, "SceneParentStack@Pop");
	assert(ss >= 0 && push >= 0 && pop >= 0);
	int stack_obj = vm_construct_struct(ss);
	for (int i = 0; i < 2; i++) {
		int obj = i ? B : A;
		int sp = stack_ptr; size_t ip = instr_ptr;
		stack_push(stack_obj); stack_push(obj); stack_push(0);
		method_call(push, VM_RETURN);
		vm_execute();
		instr_ptr = ip; stack_ptr = sp;
	}
	int member = heap[stack_obj].page->values[0].i;
	p = heap[member].page;
	BU_CHECK(p && p->nr_vars == 4 && p->values[0].i == A && p->values[2].i == B,
		 "SceneParentStack@Push x2: m_parts has %d slots, want 4 [A 0 B 0]", p ? p->nr_vars : -1);
	n = bu_call("Array", "Numof", 1, 0x10003, 1, (union vm_value[]){ BU_I(member) }).i;
	BU_CHECK(n == 2, "SceneParentStack m_parts Numof = %d, want 2", n);
	{
		int sp = stack_ptr; size_t ip = instr_ptr;
		stack_push(stack_obj);
		method_call(pop, VM_RETURN);
		vm_execute();
		instr_ptr = ip; stack_ptr = sp;
	}
	member = heap[stack_obj].page->values[0].i;
	p = heap[member].page;
	BU_CHECK(p && p->nr_vars == 2 && p->values[0].i == A, "SceneParentStack@Pop: m_parts has %d slots, want 2 [A 0]",
		 p ? p->nr_vars : -1);
	printf("BU4 two-slot Add/PopBack: %s\n", bu_failures ? "FAIL" : "ok");
}

/* BU5 */
static void bu_ex(void)
{
	struct ex *e = xcalloc(1, sizeof(*e));
	e->nr_blocks = 2;
	e->blocks = xcalloc(2, sizeof(*e->blocks));
	e->blocks[0].name = cstr_to_string("BaseUiList");
	e->blocks[0].val = (struct ex_value){ .type = EX_LIST,
		.list = bu_slist(3, (const char *[]){ "cg/one", "cg/two", "cg/three" }) };
	struct ex_table *t = xcalloc(1, sizeof(*t));
	t->nr_columns = 2; t->nr_rows = 1;
	t->rows = xcalloc(1, sizeof(*t->rows));
	t->rows[0] = xcalloc(2, sizeof(**t->rows));
	t->rows[0][0] = (struct ex_value){ .type = EX_INT, .i = 1 };
	t->rows[0][1] = (struct ex_value){ .type = EX_INT, .i = 2 };
	e->blocks[1].name = cstr_to_string("BaseUiTable");
	e->blocks[1].val = (struct ex_value){ .type = EX_TABLE, .t = t };
	probe_mainex_set(e);
	int ls = sf_str("BaseUiList", -1), ts = sf_str("BaseUiTable", -1);
	int lc = bu_call("MainEXFile", "Col", 2, 0, 2, (union vm_value[]){ BU_I(ls), BU_I(0) }).i;
	int tc = bu_call("MainEXFile", "Col", 2, 0, 2, (union vm_value[]){ BU_I(ts), BU_I(0) }).i;
	BU_CHECK(lc == 3, "Col(flat list of 3) = %d, want 3", lc);
	BU_CHECK(tc == 2, "Col(table of 2 columns) = %d, want 2", tc);
	/* Real bytecode: EXHelper::GetStringArray sizes the list with EX_Width. */
	int fno = ain_get_function(ain, "EXHelper::GetStringArray");
	assert(fno >= 0);
	int sp = stack_ptr, csp = call_stack_ptr; size_t ip = instr_ptr;
	int arg = sf_str("BaseUiList", -1);
	stack_push(arg);
	function_call(fno, VM_RETURN);
	vm_execute();
	instr_ptr = ip;
	BU_CHECK(call_stack_ptr == csp && stack_ptr == sp + 1, "GetStringArray: stack %d/%d", stack_ptr - sp, call_stack_ptr - csp);
	int result = stack_pop().i;
	struct page *r = heap_index_valid(result) ? heap[result].page : NULL;
	int nr = r ? r->nr_vars : -1;
	BU_CHECK(nr == 3 && !strcmp(bu_str(r->values[0].i), "cg/one") && !strcmp(bu_str(r->values[2].i), "cg/three"),
		 "EXHelper::GetStringArray(list) returned %d strings, want the 3 list items", nr);
	heap_unref(ls); heap_unref(ts);
	printf("BU5 MainEXFile.Col: %s\n", bu_failures ? "FAIL" : "ok");
}

static int bu_fork(const char *name, void (*fn)(void))
{
	fflush(stdout);
	pid_t pid = fork();
	if (pid == 0) {
		bu_failures = 0;
		alarm(60);	/* a parent cycle can hang the pre-fix update loop */
		fn();
		fflush(stdout);
		_exit(bu_failures ? 1 : 0);
	}
	int status = 0;
	waitpid(pid, &status, 0);
	bool ok = WIFEXITED(status) && WEXITSTATUS(status) == 0;
	if (!ok)
		printf("%s FAILED (%s %d)\n", name, WIFEXITED(status) ? "exit" : "signal",
		       WIFEXITED(status) ? WEXITSTATUS(status) : WTERMSIG(status));
	return ok ? 0 : 1;
}

static int test_base_ui(void)
{
	int failed = 0;
	failed += bu_fork("BU1", bu_activity);
	failed += bu_fork("BU2", bu_parent);
	failed += bu_fork("BU3", bu_button);
	failed += bu_fork("BU4", bu_array);
	failed += bu_fork("BU5", bu_ex);
	printf("BASE-UI %s: %d of 5 cases failed\n", failed ? "FAIL" : "PASS", failed);
	return failed ? 88 : 0;
}
