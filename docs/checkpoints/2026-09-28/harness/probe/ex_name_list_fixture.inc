/* ex-name-list: MainEXFile.GetNodeNameList, GetEXNameList and
 * GetFormatNameList fill the array they are given.
 *
 * Original (static disassembly). The library's dispatcher (0x4ade70, jump
 * table 0x4ae864) reads the "wrap<array<string>>" argument of cases 31, 32
 * and 33 with 0x65a620 -> 0x67aba0: the argument is a heap slot, the slot
 * must hold a page of type 3 (an array), and the callee gets that page's
 * IVMArray (page + 0x20, vtable 0x811c28) or NULL. The array is the caller's
 * own; nothing is boxed.
 *
 * GetNodeNameList (0x4b0ef0 -> CEXReader vt+0xc = 0x488550) and
 * GetEXNameList (0x4b0f60 -> vt+0x10 = 0x4886e0): false for a NULL array;
 * the path is split at '.', its first part must name a tree (type 6,
 * 0x487990 at 0x4879f6), and a path that is not found is false. The children
 * of that node are collected in their stored order, those without a value
 * for the node list (0x48dd40) and those with one for the EX list
 * (0x48dde0). Then: n = the array's size (vt+0xc, 0x67ee60), Realloc(n +
 * count) (vt+0x50, 0x67f4d0), and name i is stored at n + i (vt+0x3c,
 * 0x67f1e0, which assigns a copy of the text to the element's own string).
 * The names are appended; the result is true also when there are none. The
 * script frees the array first (EX_GetNodeNameList, EX_GetEXNameList).
 *
 * GetFormatNameList (0x4b0fd0): the name must be a table (EX vt+0x28); then
 * Alloc(count) (vt+0x4c, 0x67f4a0: the array is cleared first) and name i is
 * stored at i. The names replace what the array held.
 *
 *   EL1  The real EXHelper::GetNodes and EXHelper::GetParams (X_A_INIT 0,
 *        EX_GetNodeNameList / EX_GetEXNameList, Array.Free, CALLHLL) on the
 *        work copy's EX file: the motion tree, one character, one motion
 *        and two of its nodes. Counts and names are those of one EX file
 *        (126 top-level blocks, 70 nodes in the motion tree); another file
 *        is an environment error, not a failure. A call leaves no more
 *        slots behind than one for a path that is missing.
 *   EL2  Each function through its AIN declaration with an empty array: the
 *        one a real X_A_INIT 0 makes for an array<string> member, one the
 *        real Array.Free emptied, a variable that was never initialized
 *        (no page) and an empty page without an element type. The slot and
 *        its references stay, the page becomes a string array of the names,
 *        every name has one reference, and nothing else is allocated.
 *   EL3  With an array that has two strings: the node and EX lists append
 *        (the old elements keep their slots and references, twice in a
 *        row), and after the real Array.Free the list is the names alone.
 *        The format list replaces: an old element nobody else holds is
 *        released, one that is held elsewhere loses one reference.
 *   EL4  With something that is not a string array (no slot, the guard
 *        slot, a slot past the heap, a released slot, a string, a struct, a
 *        wrap box around an array, an int array, a two-dimensional array, a
 *        generic page with elements, a delegate): false, and neither the
 *        target nor the heap changes. The released slot is still on the
 *        free list when the library looks at it: a second string is
 *        released after it, so the call's own path string takes that one.
 *   EL5  300 calls (the node and EX lists after Array.Free, as the script
 *        calls them, the format list directly) and 600 calls that fail
 *        leave the number of live slots where it was.
 *   EL6  The result for 19 paths: nodes with and without children, a path
 *        that is missing, a leaf, a block that is not a tree, a table, a
 *        list of tables, the empty path and a reader id other than 0.
 *   EL7  The real EX_GetFormatNameList (no function calls it; it is the one
 *        CALLHLL of GetFormatNameList) returns the table's field names.
 *   G1   Sixteen calls of the library's other functions print the same
 *        lines as before this change.
 *   G2   The list functions do not read the AIN version: the same call with
 *        ain->version 13. (Only the v14 name-based MainEXFile declares
 *        them; the handle-based library of older games has no list
 *        function, so there is no older call shape to keep.)
 *
 * Where the expectations are xsystem4's, not the original's:
 *   - An int array is left alone (EL4). Natively the array is resized
 *     first and the first store fails (0x67f1e0 checks the element type),
 *     so the result is false after the array has grown. No declaration can
 *     pass one.
 *   - GetFormatNameList with something that is not an array is false
 *     (EL4). 0x4b0fd0 does not test the pointer.
 *   - A path that ends at a leaf is false (EL6); what 0x48de80 answers for
 *     it was not read. The empty path is false (EL6); natively it lists
 *     the top-level trees (0x487d70) or the other top-level blocks
 *     (0x487c70).
 *   - The reader id is ignored (EL6): every file is merged into one EX
 *     here. Natively an id that is not registered is false (0x442fc0).
 *   - A list whose first item is a table gives that table's field names
 *     (EL6); natively only a table does.
 *   - An empty page without an element type becomes a string array (EL2).
 *     The original array always knows its element type.
 *   - A name shares the EX file's string object (one more reference on it)
 *     instead of being copied; the heap slot is the element's own.
 *   - EL1 compares the slots a call leaves behind with what the same
 *     bytecode leaves for a path that is not there, not with zero. This VM
 *     gives the wrap local of EX_GetNodeNameList / EX_GetEXNameList a box
 *     that the function's X_ASSIGN overwrites, one slot per call. That is
 *     not this library's and is left as it is.
 *
 * Every case runs in a child process. Exit code 1 is a failed check. Exit
 * code 2 is the environment: XS4_GAME has no EX file, the EX file lacks the
 * tree EL1 reads or is not the file EL1's counts were taken from, or the
 * AIN lacks a declaration, a function or a struct a case looks up. A signal
 * or a sanitizer abort is that case failing.
 */
#include <limits.h>
#include <sys/wait.h>
#include <unistd.h>

/* GBK spellings of the EX keys EL1 reads. */
#define XL_MOTION "\xa5\xe2\xa9\x60\xa5\xb7\xa5\xe7\xa5\xf3\xc7\xe9\x88\xf3" /* モーション情報 */
#define XL_CHARA "\xbe\x5f\xc7\x89\xbe\x5f\xc7\x89" /* 綺菈綺菈 */
#define XL_IDLE "\xb4\xfd\x99\x43" /* 待機 */
#define XL_F000 "\xa3\xb0\xa3\xb0\xa3\xb0" /* ０００ */
#define XL_F001 "\xa3\xb0\xa3\xb0\xa3\xb1" /* ００１ */
#define XL_F002 "\xa3\xb0\xa3\xb0\xa3\xb2" /* ００２ */
#define XL_F012 "\xa3\xb0\xa3\xb1\xa3\xb2" /* ０１２ */
#define XL_DEBUG "\x5f\xa5\xc7\xa5\xd0\xa5\xc3\xa5\xb0\xd3\xc3" /* _デバッグ用 */
#define XL_ITEM "\xce\xef\xc6\xb7" /* 物品 */
#define XL_FIXED "\xcd\xa8\xd3\xc3\xb9\xcc\xb6\xa8\x82\xfb\xba\xa6" /* 通用固定傷害 */
/* The EX file those keys, and EL1's counts, belong to. */
enum { XL_EX_BLOCKS = 126, XL_EX_MOTIONS = 70 };

static int xl_failures;
#define XL_CHECK(c, ...) do { if (!(c)) { printf("  FAIL: "); printf(__VA_ARGS__); printf("\n"); fflush(stdout); xl_failures++; } } while (0)

static const char *const xl_nodes[] = { "NodeA", "NodeB", "NodeC" };
static const char *const xl_leaves[] = { "leafA", "leafB", "leafC" };
static const char *const xl_fields[] = { "FieldA", "FieldB", "FieldC" };
static const struct xl_fn {
	const char *fn, *path;
	const char *const *names;
	bool append;
} xl_fns[3] = {
	{ "GetNodeNameList", "XlTree", xl_nodes, true },
	{ "GetEXNameList", "XlTree", xl_leaves, true },
	{ "GetFormatNameList", "XlTable", xl_fields, false },
};

static void xl_collect(void)
{
	xl_failures += da_failures + wc_failures + oa_failures + bu_failures;
	da_failures = wc_failures = oa_failures = bu_failures = 0;
}

/* --- a synthetic EX file (no game data) --- */

static struct ex_tree xl_branch(const char *name, unsigned n)
{
	struct ex_tree t = bu_branch(name, n);
	t._children = xcalloc(n ? n : 1, sizeof(*t._children));
	return t;
}

/* The value view of the children, as libsys4's reader builds it. */
static void xl_link(struct ex_tree *t)
{
	if (t->is_leaf)
		return;
	for (unsigned i = 0; i < t->nr_children; i++) {
		t->_children[i] = (struct ex_value){ .type = EX_TREE, .tree = &t->children[i] };
		xl_link(&t->children[i]);
	}
}

static struct ex_table *xl_table(int n, const char *const *fields, int first)
{
	struct ex_table *t = xcalloc(1, sizeof(*t));
	t->nr_fields = n;
	t->fields = xcalloc(n, sizeof(*t->fields));
	t->nr_columns = n;
	t->nr_rows = 1;
	t->rows = xcalloc(1, sizeof(*t->rows));
	t->rows[0] = xcalloc(n, sizeof(**t->rows));
	for (int i = 0; i < n; i++) {
		t->fields[i].type = EX_INT;
		t->fields[i].name = cstr_to_string(fields[i]);
		t->rows[0][i] = (struct ex_value){ .type = EX_INT, .i = first + 10 * i };
	}
	return t;
}

/* XlTree { leafA, NodeA { inner }, leafB, NodeB { }, NodeC { Deep { x },
 * deepLeaf }, leafC }, XlTable (FieldA, FieldB, FieldC), XlList (one item, a
 * table with ColX, ColY), XlLeaves { a, b }, XlInt. */
static void xl_install(void)
{
	struct ex *e = xcalloc(1, sizeof(*e));
	e->nr_blocks = 5;
	e->blocks = xcalloc(5, sizeof(*e->blocks));

	struct ex_tree *top = xcalloc(1, sizeof(*top));
	*top = xl_branch("XlTree", 6);
	top->children[0] = bu_ileaf("leafA", 1);
	top->children[1] = xl_branch("NodeA", 1);
	top->children[1].children[0] = bu_ileaf("inner", 1);
	top->children[2] = bu_sleaf("leafB", "s");
	top->children[3] = xl_branch("NodeB", 0);
	top->children[4] = xl_branch("NodeC", 2);
	top->children[4].children[0] = xl_branch("Deep", 1);
	top->children[4].children[0].children[0] = bu_ileaf("x", 2);
	top->children[4].children[1] = bu_ileaf("deepLeaf", 3);
	top->children[5] = bu_leaf("leafC", (struct ex_value){ .type = EX_FLOAT, .f = 2.5f });
	xl_link(top);
	e->blocks[0].name = cstr_to_string("XlTree");
	e->blocks[0].val = (struct ex_value){ .type = EX_TREE, .tree = top };

	e->blocks[1].name = cstr_to_string("XlTable");
	e->blocks[1].val = (struct ex_value){ .type = EX_TABLE, .t = xl_table(3, xl_fields, 10) };

	struct ex_list *l = xcalloc(1, sizeof(*l));
	l->nr_items = 1;
	l->items = xcalloc(1, sizeof(*l->items));
	l->items[0].value = (struct ex_value){ .type = EX_TABLE,
		.t = xl_table(2, (const char *const[]){ "ColX", "ColY" }, 1) };
	e->blocks[2].name = cstr_to_string("XlList");
	e->blocks[2].val = (struct ex_value){ .type = EX_LIST, .list = l };

	struct ex_tree *leaves = xcalloc(1, sizeof(*leaves));
	*leaves = xl_branch("XlLeaves", 2);
	leaves->children[0] = bu_ileaf("a", 1);
	leaves->children[1] = bu_ileaf("b", 2);
	xl_link(leaves);
	e->blocks[3].name = cstr_to_string("XlLeaves");
	e->blocks[3].val = (struct ex_value){ .type = EX_TREE, .tree = leaves };

	e->blocks[4].name = cstr_to_string("XlInt");
	e->blocks[4].val = (struct ex_value){ .type = EX_INT, .i = 7 };
	probe_mainex_set(e);
}

/* --- arrays and calls --- */

/* A string array as X_A_INIT leaves it for array<string>, holding texts. */
static int xl_array(int n, const char *const *texts)
{
	struct page *p = alloc_page(ARRAY_PAGE, AIN_ARRAY_STRING, n);
	p->array.rank = 1;
	p->array.struct_type = -1;
	for (int i = 0; i < n; i++)
		p->values[i].i = sf_str(texts[i], -1);
	return heap_alloc_page(p);
}

/* The array a real X_A_INIT 0 installs in a member declared array<string>. */
static int xl_real_empty(void)
{
	for (int s = 0; s < ain->nr_structures; s++) {
		struct ain_struct *st = &ain->structures[s];
		/* keep to plain structs: no destructor, no interface, no base */
		if (st->destructor > 0 || st->nr_interfaces > 0
		    || (st->nr_members > 0 && st->members[0].type.data == AIN_WRAP))
			continue;
		for (int m = 0; m < st->nr_members; m++) {
			struct ain_type *t = &st->members[m].type;
			if (t->data != AIN_ARRAY || !t->array_type || t->array_type->data != AIN_STRING)
				continue;
			int owner = alloc_struct(s);
			int slot = wc_init_member(owner, m, 0, oa_find("EXHelper::GetNodes", X_A_INIT, 0, 0));
			heap_ref(slot);
			heap_unref(owner);
			xl_collect();
			struct page *p = da_live_page(slot) ? heap[slot].page : NULL;
			if (!p || HEAP_REF(slot) != 1 || p->type != ARRAY_PAGE || p->nr_vars != 0
			    || p->a_type != AIN_ARRAY_STRING) {
				printf("  FAIL: X_A_INIT 0 on %s.%s did not leave an empty string array\n",
				       st->name, st->members[m].name);
				fflush(stdout);
				_exit(1);
			}
			return slot;
		}
	}
	printf("EX-NAME-LIST no struct without a destructor has an array<string> member\n");
	exit(2);
}

static void xl_free(int arr)
{
	int n = oa_hll("Free", 0, 2, 1, &arr, NULL, 0);
	xl_collect();
	XL_CHECK(n == 0, "Array.Free left %d slot(s)", n);
}

/* fn(path, arr, id) through its declaration. The path is a fresh string
 * that must come back with the one reference it started with, and it must
 * not be given the slot the call is aimed at (a slot released just before
 * would be the next one allocated). */
static int xl_call(const char *fn, const char *path, int arr, int id)
{
	int p = sf_str(path, -1);
	int sp = stack_ptr;
	XL_CHECK(p != arr, "%s: the path string was allocated in the target's slot %d", fn, arr);
	int r = bu_call("MainEXFile", fn, 3, 0, 3, (union vm_value[]){ BU_I(p), BU_I(arr), BU_I(id) }).i;
	XL_CHECK(stack_ptr == sp, "%s: the stack moved by %d", fn, stack_ptr - sp);
	XL_CHECK(HEAP_REF(p) == 1, "%s: the path string has %d reference(s), want 1", fn, HEAP_REF(p));
	heap_unref(p);
	return r;
}

static const char *xl_text(int slot)
{
	return slot > 0 && heap_index_valid(slot) && heap[slot].type == VM_STRING && heap[slot].s
		? heap[slot].s->text : "(not a string)";
}

static struct page *xl_page(int slot)
{
	return da_live_page(slot) && heap[slot].page->type == ARRAY_PAGE ? heap[slot].page : NULL;
}

static void xl_show(const char *what, int slot)
{
	struct page *p = slot > 0 && heap_index_valid(slot) && heap[slot].type == VM_PAGE ? heap[slot].page : NULL;
	printf("    %s: slot %d", what, slot);
	if (!p) {
		printf(" (no page)\n");
		return;
	}
	printf(" page type=%d a_type=%d rank=%d n=%d [", p->type, p->a_type, p->array.rank, p->nr_vars);
	for (int i = 0; i < p->nr_vars && i < 12; i++) {
		int v = p->values[i].i;
		bool str = p->type == ARRAY_PAGE && v > 0 && heap_index_valid(v) && heap[v].type == VM_STRING;
		if (str)
			printf("%s\"%s\"", i ? " " : "", xl_text(v));
		else
			printf("%s%d", i ? " " : "", v);
	}
	printf("]\n");
}

/* The array is a one-dimensional string array and elements from..from+n-1
 * are distinct strings with one reference each that spell want. */
static bool xl_names(const char *what, int arr, int total, int from, int n, const char *const *want)
{
	struct page *p = xl_page(arr);
	bool ok = p && p->nr_vars == total && p->a_type == AIN_ARRAY_STRING && p->array.rank == 1;
	for (int i = 0; ok && i < n; i++) {
		int v = p->values[from + i].i;
		ok = v > 0 && heap_index_valid(v) && heap[v].type == VM_STRING && HEAP_REF(v) == 1
			&& !strcmp(xl_text(v), want[i]);
		for (int j = 0; ok && j < from + i; j++)
			ok = p->values[j].i != v;
	}
	XL_CHECK(ok, "%s: want %d element(s), names at %d..%d", what, total, from, from + n - 1);
	if (!ok)
		xl_show("array", arr);
	return ok;
}

/* What is live now, to name the slots a call leaves behind. */
static unsigned char *xl_mark_map;
static size_t xl_mark_size;
static void xl_mark(void)
{
	free(xl_mark_map);
	xl_mark_size = heap_size;
	xl_mark_map = xcalloc(heap_size, 1);
	for (size_t i = 2; i < heap_size; i++)
		xl_mark_map[i] = HEAP_REF(i) > 0;
}

static void xl_show_new(void)
{
	int shown = 0;
	for (size_t i = 2; i < heap_size && shown < 8; i++) {
		if (HEAP_REF(i) <= 0 || (i < xl_mark_size && xl_mark_map[i]))
			continue;
		shown++;
		if (heap[i].type == VM_STRING)
			printf("    left: slot %zu ref %d string \"%.40s\"\n", i, HEAP_REF(i), xl_text((int)i));
		else if (!heap[i].page)
			printf("    left: slot %zu ref %d page (none)\n", i, HEAP_REF(i));
		else
			printf("    left: slot %zu ref %d page type=%d index=%d n=%d\n", i, HEAP_REF(i),
			       heap[i].page->type, heap[i].page->index, heap[i].page->nr_vars);
	}
}

/* --- EL1: real bytecode on the work copy's EX file --- */

static int xl_leaf_total, xl_leaf_differ;
static void xl_leaf_names(struct ex_tree *t)
{
	if (t->is_leaf) {
		xl_leaf_total++;
		if (!t->name || !t->leaf.name || strcmp(t->name->text, t->leaf.name->text))
			xl_leaf_differ++;
		return;
	}
	for (unsigned i = 0; i < t->nr_children; i++)
		xl_leaf_names(&t->children[i]);
}

/* fn(path) -> array<string>: the result is the caller's (one reference).
 * Returns the live slots left once the result and the argument are released;
 * idle is that number for a path that is not there (-1: do not compare). */
static long xl_real_list(const char *fn, const char *what, const char *path, long idle, int count,
		int nfirst, const char *const *first, const char *last)
{
	size_t base = live_slots();
	xl_mark();
	int arg = sf_str(path, -1);
	union vm_value r[2] = {{ .i = -1 }, { .i = -1 }};
	int n = da_callfunc(da_fn(fn), 1, &arg, r);
	xl_collect();
	int arr = n == 1 ? r[0].i : -1;
	struct page *p = xl_page(arr);
	int got = p ? p->nr_vars : -1;
	printf("EL1 %s(%s): %d name(s), want %d\n", fn, what, got, count);
	XL_CHECK(n == 1 && p && HEAP_REF(arr) == 1 && p->a_type == AIN_ARRAY_STRING && got == count,
		"%s(%s): %d result slot(s), %d name(s), want one array of %d", fn, what, n, got, count);
	for (int i = 0; p && i < nfirst && i < got; i++)
		XL_CHECK(!strcmp(xl_text(p->values[i].i), first[i]), "%s(%s): name %d differs", fn, what, i);
	if (p && last && got == count && count > 0)
		XL_CHECK(!strcmp(xl_text(p->values[got - 1].i), last), "%s(%s): the last name differs", fn, what);
	if (p && (got != count || xl_failures))
		xl_show("result", arr);
	if (arr > 0 && heap_index_valid(arr))
		heap_unref(arr);
	/* a string argument that is not a temporary stays the caller's */
	XL_CHECK(HEAP_REF(arg) == 1, "%s(%s): the argument has %d reference(s), want 1", fn, what, HEAP_REF(arg));
	heap_unref(arg);
	long left = (long)live_slots() - (long)base;
	if (idle >= 0) {
		XL_CHECK(left == idle, "%s(%s): %ld live slot(s) left over, want the %ld of a missing path", fn, what,
			left, idle);
		if (left != idle)
			xl_show_new();
	}
	return left;
}

static void xl_real(void)
{
	const char *game = getenv("XS4_GAME");
	char path[PATH_MAX];
	snprintf(path, sizeof path, "%s/dohnadohnaEx.ex", game ? game : ".");
	struct ex *e = game ? ex_read_file(path) : NULL;
	if (!e) {
		printf("EX-NAME-LIST no EX file in XS4_GAME\n");
		exit(2);
	}
	struct ex_tree *motion = ex_get_tree(e, XL_MOTION);
	if (!motion || motion->is_leaf) {
		printf("EX-NAME-LIST the EX file has no motion tree\n");
		exit(2);
	}
	/* The counts and names below are those of one EX file. */
	int motions = 0;
	for (unsigned i = 0; i < motion->nr_children; i++)
		if (!motion->children[i].is_leaf)
			motions++;
	if (e->nr_blocks != XL_EX_BLOCKS || motions != XL_EX_MOTIONS) {
		printf("EX-NAME-LIST the EX file is not the one EL1's expectations are from: %u block(s), %d motion node(s), want %d and %d\n",
		       e->nr_blocks, motions, XL_EX_BLOCKS, XL_EX_MOTIONS);
		exit(2);
	}
	probe_mainex_set(e);
	/* GetEXNameList reads a leaf's own name where the original reads the
	 * node's; in this file they are the same. */
	for (unsigned i = 0; i < e->nr_blocks; i++)
		if (e->blocks[i].val.type == EX_TREE)
			xl_leaf_names(e->blocks[i].val.tree);
	printf("EL1 leaves whose EX name differs from their node name: %d of %d\n", xl_leaf_differ, xl_leaf_total);
	XL_CHECK(xl_leaf_differ == 0 && xl_leaf_total > 0, "a leaf's EX name is not its node name");

#define XL_P_CHARA XL_MOTION "." XL_CHARA
#define XL_P_IDLE XL_P_CHARA "." XL_IDLE
	/* What the bytecode leaves when the library stores nothing. */
	long idle_n = xl_real_list("EXHelper::GetNodes", "missing motion", XL_P_CHARA ".NoSuchMotion", -1, 0, 0, NULL, NULL);
	long idle_p = xl_real_list("EXHelper::GetParams", "missing motion", XL_P_CHARA ".NoSuchMotion", -1, 0, 0, NULL, NULL);
	printf("EL1 a call that stores nothing leaves %ld (GetNodes) and %ld (GetParams) slot(s)\n", idle_n, idle_p);
	xl_real_list("EXHelper::GetNodes", "motion tree", XL_MOTION, idle_n, 70, 2,
		(const char *const[]){ XL_DEBUG, XL_ITEM }, NULL);
	xl_real_list("EXHelper::GetNodes", "character", XL_P_CHARA, idle_n, 20, 1,
		(const char *const[]){ XL_ITEM }, XL_FIXED);
	xl_real_list("EXHelper::GetNodes", "motion", XL_P_IDLE, idle_n, 14, 4,
		(const char *const[]){ "FocusColor", XL_F000, XL_F001, XL_F002 }, XL_F012);
	xl_real_list("EXHelper::GetParams", "motion", XL_P_IDLE, idle_p, 4, 4,
		(const char *const[]){ "Range", "RangeType", "TargetDelay", "TargetRandom" }, NULL);
	xl_real_list("EXHelper::GetNodes", "frame", XL_P_IDLE "." XL_F000, idle_n, 1, 1,
		(const char *const[]){ "CgLayers" }, NULL);
	xl_real_list("EXHelper::GetParams", "frame", XL_P_IDLE "." XL_F000, idle_p, 5, 2,
		(const char *const[]){ "Frame", "OffsetX" }, "ShadowOffsetY");
	xl_real_list("EXHelper::GetNodes", "node without nodes", XL_P_IDLE ".FocusColor", idle_n, 0, 0, NULL, NULL);
	xl_real_list("EXHelper::GetParams", "node without nodes", XL_P_IDLE ".FocusColor", idle_p, 2, 2,
		(const char *const[]){ "AddColor", "MulColor" }, NULL);
}

/* --- EL2: an empty array --- */

static int xl_empty(int shape)
{
	static const char *const two[] = { "gone0", "gone1" };
	int arr;
	switch (shape) {
	case 0:
		return xl_real_empty();
	case 1:
		arr = xl_array(2, two);
		xl_free(arr);
		return arr;
	case 2:
		arr = heap_alloc_slot(VM_PAGE);
		heap_set_page(arr, NULL);
		return arr;
	default: {
		struct page *p = alloc_page(ARRAY_PAGE, AIN_ARRAY, 0);
		p->array.rank = 1;
		p->array.struct_type = 1;
		return heap_alloc_page(p);
	}
	}
}

static void xl_into_empty(void)
{
	static const char *const shapes[] = { "X_A_INIT 0", "Array.Free", "no page", "untyped empty page" };
	xl_install();
	for (int f = 0; f < 3; f++) {
		for (int s = 0; s < 4; s++) {
			char what[80];
			snprintf(what, sizeof what, "%s(%s)", xl_fns[f].fn, shapes[s]);
			size_t before = live_slots();
			int arr = xl_empty(s);
			size_t base = live_slots();
			unsigned seq = heap[arr].seq;
			int r = xl_call(xl_fns[f].fn, xl_fns[f].path, arr, 0);
			struct page *p = xl_page(arr);
			printf("EL2 %s: result %d, %d element(s), %+ld live slot(s)\n", what, r, p ? p->nr_vars : -1,
			       (long)live_slots() - (long)base);
			XL_CHECK(r == 1, "%s returned %d, want 1", what, r);
			XL_CHECK(heap_index_valid(arr) && HEAP_REF(arr) == 1 && heap[arr].seq == seq,
				"%s: the array's slot or its references changed", what);
			xl_names(what, arr, 3, 0, 3, xl_fns[f].names);
			XL_CHECK(live_slots() == base + 3, "%s: %+ld live slot(s), want +3 (the names)", what,
				(long)live_slots() - (long)base);
			heap_unref(arr);
			XL_CHECK(live_slots() == before, "%s: %ld slot(s) left after the array was released", what,
				(long)live_slots() - (long)before);
		}
	}
}

/* --- EL3: an array that already has elements --- */

static void xl_into_filled(void)
{
	static const char *const two[] = { "keep0", "keep1" };
	xl_install();
	for (int f = 0; f < 2; f++) {
		const char *fn = xl_fns[f].fn, *path = xl_fns[f].path;
		size_t before = live_slots();
		int arr = xl_array(2, two);
		int k0 = heap[arr].page->values[0].i, k1 = heap[arr].page->values[1].i;
		heap_ref(k1);
		size_t base = live_slots();
		int r = xl_call(fn, path, arr, 0);
		struct page *p = xl_page(arr);
		printf("EL3 %s on two strings: result %d, %d element(s)\n", fn, r, p ? p->nr_vars : -1);
		XL_CHECK(r == 1 && HEAP_REF(arr) == 1, "%s: result %d, array references %d", fn, r, HEAP_REF(arr));
		bool kept = p && p->nr_vars == 5 && p->values[0].i == k0 && p->values[1].i == k1
			&& HEAP_REF(k0) == 1 && HEAP_REF(k1) == 2
			&& !strcmp(xl_text(k0), "keep0") && !strcmp(xl_text(k1), "keep1");
		XL_CHECK(kept, "%s: the two old elements did not keep their slots and references", fn);
		xl_names(fn, arr, 5, 2, 3, xl_fns[f].names);
		XL_CHECK(live_slots() == base + 3, "%s: %+ld live slot(s), want +3", fn, (long)live_slots() - (long)base);
		if (!kept)
			xl_show("array", arr);

		r = xl_call(fn, path, arr, 0);
		p = xl_page(arr);
		printf("EL3 %s again: result %d, %d element(s)\n", fn, r, p ? p->nr_vars : -1);
		XL_CHECK(r == 1 && p && p->nr_vars == 8 && p->values[0].i == k0 && p->values[1].i == k1,
			"%s again: want 8 elements behind the same two", fn);
		xl_names(fn, arr, 8, 2, 3, xl_fns[f].names);
		xl_names(fn, arr, 8, 5, 3, xl_fns[f].names);
		XL_CHECK(live_slots() == base + 6, "%s again: %+ld live slot(s), want +6", fn,
			(long)live_slots() - (long)base);

		/* The script's shape: Array.Free, then the call. */
		xl_free(arr);
		XL_CHECK(HEAP_REF(k0) == 0 && HEAP_REF(k1) == 1 && !strcmp(xl_text(k1), "keep1"),
			"Array.Free: element references %d and %d, want 0 and 1", HEAP_REF(k0), HEAP_REF(k1));
		r = xl_call(fn, path, arr, 0);
		p = xl_page(arr);
		printf("EL3 %s after Array.Free: result %d, %d element(s)\n", fn, r, p ? p->nr_vars : -1);
		XL_CHECK(r == 1, "%s after Array.Free returned %d", fn, r);
		xl_names(fn, arr, 3, 0, 3, xl_fns[f].names);
		heap_unref(arr);
		heap_unref(k1);
		XL_CHECK(live_slots() == before, "%s: %ld slot(s) left over", fn, (long)live_slots() - (long)before);
	}

	/* GetFormatNameList replaces. */
	const char *fn = xl_fns[2].fn;
	size_t before = live_slots();
	int arr = xl_array(2, two);
	int k0 = heap[arr].page->values[0].i, k1 = heap[arr].page->values[1].i;
	heap_ref(k1);
	size_t base = live_slots();
	int r = xl_call(fn, xl_fns[2].path, arr, 0);
	struct page *p = xl_page(arr);
	printf("EL3 %s on two strings: result %d, %d element(s), old references %d and %d\n", fn, r,
	       p ? p->nr_vars : -1, HEAP_REF(k0), HEAP_REF(k1));
	XL_CHECK(r == 1 && HEAP_REF(arr) == 1, "%s: result %d, array references %d", fn, r, HEAP_REF(arr));
	xl_names(fn, arr, 3, 0, 3, xl_fns[2].names);
	XL_CHECK(HEAP_REF(k0) == 0, "%s: the old element nobody else holds has %d reference(s), want 0", fn, HEAP_REF(k0));
	XL_CHECK(HEAP_REF(k1) == 1 && !strcmp(xl_text(k1), "keep1"),
		"%s: the old element held elsewhere has %d reference(s), want 1", fn, HEAP_REF(k1));
	XL_CHECK(live_slots() == base + 2, "%s: %+ld live slot(s), want +2 (three names, one release)", fn,
		(long)live_slots() - (long)base);
	r = xl_call(fn, "XlList", arr, 0);
	xl_names("GetFormatNameList(list) over three names", arr, 2, 0, 2, (const char *const[]){ "ColX", "ColY" });
	XL_CHECK(r == 1 && live_slots() == base + 1, "%s again: result %d, %+ld live slot(s), want +1", fn, r,
		(long)live_slots() - (long)base);
	heap_unref(arr);
	heap_unref(k1);
	XL_CHECK(live_slots() == before, "%s: %ld slot(s) left over", fn, (long)live_slots() - (long)before);
}

/* --- EL4: not a string array --- */

struct xl_snap {
	bool valid;
	int type, ref, nr, ptype, pindex, rank, stride;
	unsigned seq;
	struct page *page;
	struct string *s;
	union vm_value v[8];
};

static struct xl_snap xl_snap(int slot)
{
	struct xl_snap n;
	memset(&n, 0, sizeof n);
	n.valid = slot >= 0 && (size_t)slot < heap_size;
	if (!n.valid)
		return n;
	n.type = heap[slot].type;
	n.ref = heap[slot].ref;
	n.seq = heap[slot].seq;
	if (n.ref <= 0) {
		n.page = heap[slot].page;	/* whatever the free slot holds; not followed */
		return n;
	}
	if (n.type == VM_STRING) {
		n.s = heap[slot].s;
		return n;
	}
	n.page = heap[slot].page;
	if (!n.page)
		return n;
	n.nr = n.page->nr_vars;
	n.ptype = n.page->type;
	n.pindex = n.page->index;
	n.rank = n.page->array.rank;
	n.stride = n.page->array.struct_type;
	for (int i = 0; i < n.nr && i < 8; i++)
		n.v[i] = n.page->values[i];
	return n;
}

static bool xl_same(struct xl_snap a, struct xl_snap b)
{
	return !memcmp(&a, &b, sizeof a);
}

static void xl_not_array(void)
{
	static const char *const one[] = { "inner" };
	static const char *const what[] = { "slot -1", "the guard slot", "a slot past the heap", "a released slot",
		"a string", "a struct", "a wrap box around an array", "an int array", "a two-dimensional array",
		"a generic page with elements", "a delegate page" };
	xl_install();
	for (int f = 0; f < 3; f++) {
		for (int k = 0; k < 11; k++) {
			int target = -1, inner = -1, pad = -1;
			struct page *p;
			switch (k) {
			case 0: target = -1; break;
			case 1: target = 0; break;
			case 2: target = 0x7ffffff0; break;
			case 3:
				/* The next allocation takes the slot released last: pad,
				 * not the target. */
				target = sf_str("released", -1);
				pad = sf_str("pad", -1);
				heap_unref(target);
				heap_unref(pad);
				break;
			case 4: target = sf_str("text", -1); break;
			case 5: target = oa_obj(); break;
			case 6:
				inner = xl_array(1, one);
				p = alloc_page(STRUCT_PAGE, -1, 1);
				p->values[0].i = inner;
				target = heap_alloc_page(p);
				break;
			case 7:
				p = alloc_page(ARRAY_PAGE, AIN_ARRAY_INT, 2);
				p->array.rank = 1;
				p->array.struct_type = -1;
				p->values[0].i = 7;
				p->values[1].i = 8;
				target = heap_alloc_page(p);
				break;
			case 8:
				inner = xl_array(1, one);
				p = alloc_page(ARRAY_PAGE, AIN_ARRAY_STRING, 1);
				p->array.rank = 2;
				p->array.struct_type = -1;
				p->values[0].i = inner;
				target = heap_alloc_page(p);
				break;
			case 9:
				p = alloc_page(ARRAY_PAGE, AIN_ARRAY, 4);
				p->array.rank = 1;
				p->array.struct_type = 2;
				for (int i = 0; i < 4; i++)
					p->values[i].i = i % 2 ? 1 : -1;
				target = heap_alloc_page(p);
				break;
			default:
				target = heap_alloc_page(alloc_page(DELEGATE_PAGE, 0, 0));
				break;
			}
			xl_collect();
			struct xl_snap t0 = xl_snap(target), i0 = xl_snap(inner);
			size_t base = live_slots();
			int r = xl_call(xl_fns[f].fn, xl_fns[f].path, target, 0);
			bool same = xl_same(t0, xl_snap(target)) && xl_same(i0, xl_snap(inner));
			printf("EL4 %s(%s): result %d, target %s, %+ld live slot(s)\n", xl_fns[f].fn, what[k], r,
			       same ? "unchanged" : "CHANGED", (long)live_slots() - (long)base);
			XL_CHECK(r == 0, "%s(%s) returned %d, want 0", xl_fns[f].fn, what[k], r);
			XL_CHECK(same, "%s(%s) changed its target", xl_fns[f].fn, what[k]);
			if (k == 3)
				XL_CHECK(HEAP_REF(target) == 0 && HEAP_REF(pad) == 0,
					"%s(%s): the target was not a released slot during the call (references %d, pad %d)",
					xl_fns[f].fn, what[k], HEAP_REF(target), HEAP_REF(pad));
			XL_CHECK(live_slots() == base, "%s(%s): %+ld live slot(s), want 0", xl_fns[f].fn, what[k],
				(long)live_slots() - (long)base);
			if (!same)
				xl_show("target", target);
		}
	}
}

/* --- EL5: no slot is left behind --- */

static void xl_leak(void)
{
	static const char *const two[] = { "a", "b" };
	xl_install();
	size_t before = live_slots();
	int arr = xl_array(2, two);
	int str = sf_str("text", -1);
	size_t level[3] = { 0, 0, 0 };
	for (int round = 0; round < 100; round++) {
		for (int f = 0; f < 3; f++) {
			if (xl_fns[f].append)
				xl_free(arr);
			int r = xl_call(xl_fns[f].fn, xl_fns[f].path, arr, 0);
			if (!round)
				level[f] = live_slots();
			if (r != 1 || live_slots() != level[f] || !xl_page(arr) || xl_page(arr)->nr_vars != 3) {
				XL_CHECK(false, "round %d %s: result %d, %ld live slot(s) over the first round", round,
					xl_fns[f].fn, r, (long)live_slots() - (long)level[f]);
				goto out;
			}
		}
	}
	for (int round = 0; round < 100; round++) {
		for (int f = 0; f < 3; f++) {
			size_t base = live_slots();
			int r1 = xl_call(xl_fns[f].fn, "XlTree.Missing", arr, 0);
			int r2 = xl_call(xl_fns[f].fn, xl_fns[f].path, str, 0);
			if (r1 || r2 || live_slots() != base) {
				XL_CHECK(false, "round %d %s failures: results %d %d, %+ld live slot(s)", round, xl_fns[f].fn,
					r1, r2, (long)live_slots() - (long)base);
				goto out;
			}
		}
	}
out:
	printf("EL5 300 calls and 600 failures: %+ld live slot(s) over the first round\n",
	       (long)live_slots() - (long)level[2]);
	heap_unref(arr);
	heap_unref(str);
	XL_CHECK(live_slots() == before, "%ld slot(s) left after the array was released", (long)live_slots() - (long)before);
}

/* --- EL6: results --- */

static void xl_results(void)
{
	static const struct {
		int fn;
		const char *path;
		int id, want, count;
		const char *first;
	} t[] = {
		{ 0, "XlTree", 0, 1, 3, "NodeA" },
		{ 0, "XlTree.NodeC", 0, 1, 1, "Deep" },
		{ 0, "XlTree.NodeC.Deep", 0, 1, 0, NULL },	/* a node without nodes is still found */
		{ 0, "XlTree.NodeB", 0, 1, 0, NULL },
		{ 0, "XlLeaves", 0, 1, 0, NULL },
		{ 0, "XlTree.Missing", 0, 0, 0, NULL },
		{ 0, "Missing", 0, 0, 0, NULL },
		{ 0, "XlInt", 0, 0, 0, NULL },
		{ 0, "XlTable", 0, 0, 0, NULL },
		{ 0, "XlTree.leafA", 0, 0, 0, NULL },		/* xsystem4's */
		{ 0, "", 0, 0, 0, NULL },			/* xsystem4's */
		{ 0, "XlTree", 5, 1, 3, "NodeA" },		/* xsystem4's: the id is ignored */
		{ 1, "XlTree", 0, 1, 3, "leafA" },
		{ 1, "XlTree.NodeC", 0, 1, 1, "deepLeaf" },
		{ 1, "XlTree.NodeB", 0, 1, 0, NULL },
		{ 1, "XlTree.Missing", 0, 0, 0, NULL },
		{ 2, "XlTable", 0, 1, 3, "FieldA" },
		{ 2, "XlList", 0, 1, 2, "ColX" },		/* xsystem4's */
		{ 2, "XlTree", 0, 0, 0, NULL },
	};
	xl_install();
	for (unsigned i = 0; i < sizeof t / sizeof t[0]; i++) {
		int arr = xl_array(0, NULL);
		size_t base = live_slots();
		int r = xl_call(xl_fns[t[i].fn].fn, t[i].path, arr, t[i].id);
		struct page *p = xl_page(arr);
		int n = p ? p->nr_vars : -1;
		printf("EL6 %s(\"%s\", id %d): result %d, %d name(s)%s%s\n", xl_fns[t[i].fn].fn, t[i].path, t[i].id, r, n,
		       n > 0 ? ", first " : "", n > 0 ? xl_text(p->values[0].i) : "");
		XL_CHECK(r == t[i].want && n == t[i].count
			&& (!t[i].first || (n > 0 && !strcmp(xl_text(p->values[0].i), t[i].first))),
			"%s(\"%s\", id %d): want result %d and %d name(s)", xl_fns[t[i].fn].fn, t[i].path, t[i].id,
			t[i].want, t[i].count);
		XL_CHECK(live_slots() == base + t[i].count, "%s(\"%s\"): %+ld live slot(s), want %+d",
			xl_fns[t[i].fn].fn, t[i].path, (long)live_slots() - (long)base, t[i].count);
		heap_unref(arr);
	}
}

/* --- EL7: the real EX_GetFormatNameList --- */

static void xl_format_bytecode(void)
{
	xl_install();
	size_t base = live_slots();
	xl_mark();
	int args[] = { sf_str("XlTable", -1), 0 };
	union vm_value r[2] = {{ .i = -1 }, { .i = -1 }};
	int n = da_callfunc(da_fn("EX_GetFormatNameList"), 2, args, r);
	xl_collect();
	int arr = n == 1 ? r[0].i : -1;
	struct page *p = xl_page(arr);
	printf("EL7 EX_GetFormatNameList(table): %d result slot(s), %d name(s), references %d\n", n,
	       p ? p->nr_vars : -1, p ? HEAP_REF(arr) : -1);
	XL_CHECK(n == 1 && p && HEAP_REF(arr) == 1, "want one array with one reference");
	xl_names("EX_GetFormatNameList", arr, 3, 0, 3, xl_fields);
	if (arr > 0 && heap_index_valid(arr))
		heap_unref(arr);
	XL_CHECK(HEAP_REF(args[0]) == 1, "the argument has %d reference(s), want 1", HEAP_REF(args[0]));
	heap_unref(args[0]);
	XL_CHECK(live_slots() == base, "%ld slot(s) left over", (long)live_slots() - (long)base);
	if (live_slots() != base)
		xl_show_new();
}

/* --- G1: the library's other functions --- */

static const char *const xl_g1_want[] = {
	"G1 Row(XlTable)=1",
	"G1 Row(XlList)=1",
	"G1 Col(XlTable)=3",
	"G1 Col(XlList)=1",
	"G1 Type(XlTree)=6",
	"G1 Type(XlTree.NodeA)=6",
	"G1 Type(XlTree.leafA)=1",
	"G1 Exists(XlTree.NodeC.Deep)=1",
	"G1 Exists(XlTree.Missing)=0",
	"G1 Int(XlInt)=7",
	"G1 Int(Missing)=-5",
	"G1 Int(XlTree.NodeC.deepLeaf)=3",
	"G1 String(XlTree.leafB)=s",
	"G1 GetNodeNameCount(XlTree)=3",
	"G1 GetEXNameCount(XlTree)=3",
	"G1 GetColAtFormatName(XlTable,FieldB)=1",
};

static int xl_g1_i;
static void xl_g1_line(const char *line)
{
	unsigned n = sizeof xl_g1_want / sizeof xl_g1_want[0];
	const char *want = (unsigned)xl_g1_i < n ? xl_g1_want[xl_g1_i] : "(no such line)";
	printf("%s\n", line);
	XL_CHECK(!strcmp(line, want), "line %d differs, want \"%s\"", xl_g1_i, want);
	xl_g1_i++;
}

static void xl_g1_int(const char *fn, const char *name, int nargs, int dflt)
{
	int p = sf_str(name, -1);
	union vm_value three[] = { BU_I(p), BU_I(dflt), BU_I(0) };
	union vm_value two[] = { BU_I(p), BU_I(0) };
	int r = bu_call("MainEXFile", fn, nargs, 0, nargs, nargs == 3 ? three : two).i;
	heap_unref(p);
	char line[160];
	snprintf(line, sizeof line, "G1 %s(%s)=%d", fn, name, r);
	xl_g1_line(line);
}

static void xl_guard_others(void)
{
	xl_install();
	size_t base = live_slots();
	xl_g1_int("Row", "XlTable", 2, 0);
	xl_g1_int("Row", "XlList", 2, 0);
	xl_g1_int("Col", "XlTable", 2, 0);
	xl_g1_int("Col", "XlList", 2, 0);
	xl_g1_int("Type", "XlTree", 2, 0);
	xl_g1_int("Type", "XlTree.NodeA", 2, 0);
	xl_g1_int("Type", "XlTree.leafA", 2, 0);
	xl_g1_int("Exists", "XlTree.NodeC.Deep", 2, 0);
	xl_g1_int("Exists", "XlTree.Missing", 2, 0);
	xl_g1_int("Int", "XlInt", 3, -5);
	xl_g1_int("Int", "Missing", 3, -5);
	xl_g1_int("Int", "XlTree.NodeC.deepLeaf", 3, -5);
	char line[160];
	int p = sf_str("XlTree.leafB", -1), d = sf_str("default", -1);
	int s = bu_call("MainEXFile", "String", 3, 0, 3, (union vm_value[]){ BU_I(p), BU_I(d), BU_I(0) }).i;
	snprintf(line, sizeof line, "G1 String(XlTree.leafB)=%s", xl_text(s));
	xl_g1_line(line);
	heap_unref(p);
	heap_unref(d);
	if (s > 0 && heap_index_valid(s))
		heap_unref(s);
	xl_g1_int("GetNodeNameCount", "XlTree", 2, 0);
	xl_g1_int("GetEXNameCount", "XlTree", 2, 0);
	p = sf_str("XlTable", -1);
	d = sf_str("FieldB", -1);
	int c = bu_call("MainEXFile", "GetColAtFormatName", 3, 0, 3, (union vm_value[]){ BU_I(p), BU_I(d), BU_I(0) }).i;
	snprintf(line, sizeof line, "G1 GetColAtFormatName(XlTable,FieldB)=%d", c);
	xl_g1_line(line);
	heap_unref(p);
	heap_unref(d);
	XL_CHECK(xl_g1_i == (int)(sizeof xl_g1_want / sizeof xl_g1_want[0]), "%d line(s) printed", xl_g1_i);
	XL_CHECK(live_slots() == base, "%ld slot(s) left over", (long)live_slots() - (long)base);
}

/* --- G2: no version switch --- */

static void xl_guard_version(void)
{
	xl_install();
	int version = ain->version;
	ain->version = 13;
	for (int f = 0; f < 3; f++) {
		int arr = xl_array(0, NULL);
		size_t base = live_slots();
		int r = xl_call(xl_fns[f].fn, xl_fns[f].path, arr, 0);
		printf("G2 version 13 %s: result %d, %d element(s)\n", xl_fns[f].fn, r,
		       xl_page(arr) ? xl_page(arr)->nr_vars : -1);
		XL_CHECK(r == 1 && live_slots() == base + 3, "version 13 %s: result %d, %+ld live slot(s)", xl_fns[f].fn,
			r, (long)live_slots() - (long)base);
		xl_names(xl_fns[f].fn, arr, 3, 0, 3, xl_fns[f].names);
		int str = sf_str("text", -1);
		r = xl_call(xl_fns[f].fn, xl_fns[f].path, str, 0);
		XL_CHECK(r == 0 && !strcmp(xl_text(str), "text"), "version 13 %s(a string) returned %d", xl_fns[f].fn, r);
		heap_unref(str);
		heap_unref(arr);
	}
	ain->version = version;
}

static int test_ex_name_list(void)
{
	static const struct { const char *name; void (*run)(void); } cases[] = {
		{ "EL1", xl_real }, { "EL2", xl_into_empty }, { "EL3", xl_into_filled }, { "EL4", xl_not_array },
		{ "EL5", xl_leak }, { "EL6", xl_results }, { "EL7", xl_format_bytecode },
		{ "G1", xl_guard_others }, { "G2", xl_guard_version },
	};
	int failed = 0, environment = 0;
	for (unsigned i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
		fflush(stdout);
		fflush(stderr);
		pid_t pid = fork();
		if (pid == 0) {
			alarm(60);
			xl_failures = da_failures = wc_failures = oa_failures = bu_failures = 0;
			cases[i].run();
			xl_collect();
			fflush(stdout);
			_exit(xl_failures ? 1 : 0);
		}
		int st = 0;
		if (pid < 0 || waitpid(pid, &st, 0) != pid) {
			printf("%s ENVIRONMENT (fork/wait failed)\n", cases[i].name);
			environment++;
		} else if (WIFSIGNALED(st)) {
			printf("%s FAIL (terminated by signal %d)\n", cases[i].name, WTERMSIG(st));
			failed++;
		} else if (WEXITSTATUS(st) == 0) {
			printf("%s ok\n", cases[i].name);
		} else if (WEXITSTATUS(st) == 2) {
			printf("%s ENVIRONMENT (no EX file, or the AIN lacks something this case looks up)\n", cases[i].name);
			environment++;
		} else {
			printf("%s FAIL (exit %d)\n", cases[i].name, WEXITSTATUS(st));
			failed++;
		}
	}
	printf("EX-NAME-LIST %s: %d of %zu cases failed, %d environment error(s)\n",
		failed || environment ? "FAIL" : "PASS", failed, sizeof(cases) / sizeof(cases[0]), environment);
	return environment ? 2 : failed ? 1 : 0;
}
