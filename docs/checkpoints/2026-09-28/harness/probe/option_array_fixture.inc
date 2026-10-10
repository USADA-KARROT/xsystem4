/* option-array: Array HLL calls on arrays whose elements are options.
 *
 * Original (static disassembly). CALLHLL (0x66d470) passes its third operand
 * to the argument reader (0x66a5e0 -> 0x66a700 -> 0x659970). For an
 * hll_param argument it is a packed type: low word 1 plain value, 2 object
 * (string, struct, array, delegate), 3 interface; bit 16 wrap; bit 17
 * option. An option takes its flag from the top of the stack, then the
 * vtable offset when the payload is an interface, then the value: 0x20002
 * and 0x30002 are two slots, 0x30003 three. A "ref hll_param" result (At,
 * First, Last) of an option element is a position, (array page, index *
 * stride) (0x6499e0 at 0x649b0d), and (-1, 0) when there is none (0x649c25).
 *
 * The array itself knows its element type and stride (array +0x48, from
 * 0x653420). An option element keeps its value slots first and the flag
 * last; flag 0 is "has a value", none is every value slot -1 and flag 1
 * (0x656a44). Alloc (0x647470 -> 0x67f4a0) clears the array and then
 * default-initializes every element, Realloc (0x67f4d0) default-initializes
 * the new tail and releases the removed one, PushBack/Add (0x6476b0) is
 * Insert at the end (0x647990), and storing none writes only the flag
 * (0x646d70 at 0x646e7b, 0x67f380). Predicates get the whole element
 * (0x6455d0 at 0x645829). Bytecode addresses an element as index * stride,
 * reads it with X_REF 2 and tests "flag >= 1" for none; X_A_SIZE (0x66e860)
 * is the element count.
 *
 *   OA1  PushBack 0x30002 of a value onto the empty array X_A_INIT 0 makes.
 *   OA2  PushBack 0x30002 of none: the element is (-1, 1) whatever the value
 *        slot held, and nothing else is touched.
 *   OA3  The real ArrayExtensions::Select<Worker&?, WorkerCollection&> with
 *        the real selector of SceneWorkMatching@GetMatchedShopIndex over
 *        three real WorkerCollections, one of them without a showcase.
 *   OA4  Where 0x30002 with the real predicate of ItemStock@ToItem, then the
 *        real ArrayExtensions::Select<Item&, Item&?> with its real selector.
 *   OA5  Add 0x20002 (option<string>) read back by the real
 *        OptionalExtensions::HasValue<string> at the positions
 *        LocalGame@GetStealableRivalShopIndex uses.
 *   OA6  Alloc, Count, the real X_OP_SET 65538 and Erase 0x30002, the calls
 *        of the item list views (ItemViewCollection@Reset and the others),
 *        including Erase down to empty followed by PushBack and by Alloc.
 *   OA7  The accesses of MapView@LoadNodes on the array MapView@0 allocates:
 *        the real X_A_INIT, Alloc 0x30002, the real
 *        OptionalExtensions::HasValue<MapNodeView&> and both X_OP_SET 65538
 *        of that function, for an odd and an even number of nodes.
 *   OA8  First(predicate), At, Last and First() 0x30002 return a position,
 *        read back with the real X_REF 2, and do not retain the array. On a
 *        page without the option layout First still answers with the shape
 *        of a position.
 *   OA9  Realloc 0x30002.
 *   OA10 Insert, PopBack, Clear, Free and Concat 0x30002.
 *   OA11 The real MapView@GetNode (the one First 0x30002 in the game) with
 *        its real predicate over a MapView's node array: the nodes are
 *        found, the array's references are unchanged, and releasing the
 *        MapView releases the array and its views.
 *   OA12 Alloc and Realloc install the new page before they release the old
 *        elements: the (empty, real) destructor of a removed element is
 *        entered with the new array already in place.
 *   G1   Operands 1, 2, 0x10002 and 0x10003: 41 calls whose printed lines
 *        (result slots, page contents, references) must equal a table
 *        recorded before this change.
 *   G2   An AIN older than version 14, and operands -1 and 0, never read
 *        bit 17.
 *   G3   0x30003 (option<wrap<iwrap<T>>>, ShopButtonCollection@0): three
 *        slots, none is (-1, -1, 1), for Alloc, X_OP_SET 65539, PushBack,
 *        Insert, At, Erase and Realloc. Only the page layout is covered
 *        here; functions that return such an option are option-return's.
 *   G4   A declaration without an implementation discards the same slots,
 *        and no declaration has an argument after an hll_param.
 *   G5   A_REF of an option array (what MatchingInfo's constructor does)
 *        shares the values, and releasing either copy leaves the other.
 *
 * Where the expectations are xsystem4's, not the original's:
 *   - A position is not counted. Natively it holds a reference to its page
 *     (0x658390 -> 0x679f10) that the holder's function page gives back
 *     when it releases the type 0x57 variable (0x6570f0 -> 0x656cf0 ->
 *     0x656c10 at 0x656c58). This VM has neither half for type 87 (no retain
 *     for such an argument, no release with the page), so the HLL call adds
 *     nothing (OA8, OA11, G3).
 *   - OA5's reference count is the one a string element has with operand 2
 *     here: the slot is shared and retained. Natively the element gets its
 *     own copy of the text.
 *   - OA10's Concat has no option call in the game; "append whole elements"
 *     is inferred from the other operands.
 *   - EmplaceBack and Realloc(n, value) on option elements only warn; Fill,
 *     Copy, Sort and the other value operations are not covered.
 *   - MapView@GetNode for an id that is not there reads through (-1, 0);
 *     the original reports a "Ref" error, this VM reads (-1, -1). Not run.
 *
 * Real bytecode that no case runs: ArrayExtensions::Select<Item&?, string>
 * (the first step of ItemStock@ToItem; the same shape as OA3's function),
 * ItemStock@ToItem as a whole (OA4 is its second half on a hand-made
 * source), MatchingCalculator::Match, LocalGame@GetStealableRivalShopIndex
 * (OA5 calls Add directly) and the body of MapView@LoadNodes (OA7 performs
 * its array accesses). OA4 and OA8 use ItemStock's predicate with no
 * object; MapView@GetNode's own predicate runs in OA11.
 *
 * Every case runs in a child process. Exit code 1 is a failed check, also
 * when a precondition on the engine fails (the empty array of X_A_INIT 0,
 * the result of WorkerCollection@GetShowCaseWorker). Exit code 2 is the
 * environment: the AIN lacks a function, struct, member or instruction the
 * case looks up, a struct used as a plain value has a destructor, or no
 * struct with an empty destructor is found. A signal or a sanitizer abort
 * is that case failing.
 */
#include <sys/wait.h>
#include <unistd.h>

enum { OA_REF = 0x30002, OA_STR = 0x20002, OA_IFACE = 0x30003 };

static int oa_failures;
#define OA_CHECK(c, ...) do { if (!(c)) { printf("  FAIL: "); printf(__VA_ARGS__); printf("\n"); fflush(stdout); oa_failures++; } } while (0)

/* Watches: the stack before PushBack inside a bytecode function, and the
 * argument slots a predicate is entered with. */
static bool oa_watch_push;
static int oa_push_owner, oa_push_fn, oa_push_depth;
static unsigned oa_push_calls, oa_push_bad;
static int oa_pred_fn = -1, oa_pred_seen[16][2], oa_pred_n;
/* What a destructor finds in an array when it is entered. */
static int oa_dtor_fn = -1, oa_dtor_arr, oa_dtor_seen, oa_dtor_slots, oa_dtor_none;

static void option_array_probe_step(void)
{
	if (!call_stack_ptr)
		return;
	struct function_call *f = &call_stack[call_stack_ptr - 1];
	if (oa_pred_fn >= 0 && f->fno == oa_pred_fn && instr_ptr == ain->functions[oa_pred_fn].address
	    && oa_pred_n < 16 && da_live_page(f->page_slot) && heap[f->page_slot].page->nr_vars >= 2) {
		oa_pred_seen[oa_pred_n][0] = heap[f->page_slot].page->values[0].i;
		oa_pred_seen[oa_pred_n][1] = heap[f->page_slot].page->values[1].i;
		oa_pred_n++;
	}
	if (oa_dtor_fn >= 0 && f->fno == oa_dtor_fn && instr_ptr == ain->functions[oa_dtor_fn].address) {
		struct page *p = da_live_page(oa_dtor_arr) && heap[oa_dtor_arr].page->type == ARRAY_PAGE
			? heap[oa_dtor_arr].page : NULL;
		oa_dtor_seen++;
		oa_dtor_slots = p ? p->nr_vars : -1;
		oa_dtor_none = p != NULL;
		for (int i = 0; p && i < p->nr_vars; i++)
			if (p->values[i].i != (i % 2 ? 1 : -1))
				oa_dtor_none = 0;
	}
	if (!oa_watch_push || get_opcode(instr_ptr) != CALLHLL || f->fno != oa_push_owner
	    || get_argument(0) != bu_lib("Array") || get_argument(1) != oa_push_fn)
		return;
	oa_push_calls++;
	if (stack_ptr != f->base_sp + oa_push_depth)
		oa_push_bad++;
}

/* The nth Array declaration of that name: the bytecode's Name#n. */
static int oa_fn(const char *name, int ordinal)
{
	int lib = bu_lib("Array"), seen = 0;
	for (int j = 0; j < ain->libraries[lib].nr_functions; j++)
		if (!strcmp(ain->libraries[lib].functions[j].name, name) && seen++ == ordinal)
			return j;
	printf("OPTION-ARRAY Array.%s#%d not declared\n", name, ordinal);
	exit(2);
}

/* Pushes the slots and calls Array.name#ordinal with the type operand.
 * Returns how many slots the call left above the stack it started from (0
 * for a void function that took all its arguments) and copies them. */
static int oa_hll(const char *name, int ordinal, int arg3, int nslots, const int *slots, int *out, int maxout)
{
	int lib = bu_lib("Array"), f = oa_fn(name, ordinal);
	int sp = stack_ptr;
	for (int i = 0; i < nslots; i++)
		stack_push(slots[i]);
	hll_call(lib, f, arg3);
	int n = stack_ptr - sp;
	for (int i = 0; i < n && i < maxout; i++)
		out[i] = stack[sp + i].i;
	stack_ptr = sp;
	return n;
}

static int oa_hll1(const char *name, int ordinal, int arg3, int self)
{
	int out[2] = { -999, -999 };
	int n = oa_hll(name, ordinal, arg3, 1, &self, out, 2);
	OA_CHECK(n == 1, "Array.%s#%d(%#x) left %d slot(s), want 1", name, ordinal, arg3, n);
	return out[0];
}

static uint32_t oa_find(const char *fn, enum opcode want, int argument, int skip)
{
	int fno = da_fn(fn);
	for (uint32_t ip = ain->functions[fno].address; ip + 6 <= ain->code_size;) {
		enum opcode op = get_opcode(ip);
		if (op == want && LittleEndian_getDW(ain->code, ip + 2) == argument && skip-- == 0)
			return ip;
		if (op == FUNC || op == ENDFUNC)
			break;
		ip += instruction_width(op);
	}
	printf("OPTION-ARRAY instruction %#x %d not found in %s\n", want, argument, fn);
	exit(2);
}

static uint32_t oa_find_any(enum opcode want, int argument)
{
	for (uint32_t ip = 0; ip + 6 <= ain->code_size;) {
		enum opcode op = get_opcode(ip);
		if (op == want && LittleEndian_getDW(ain->code, ip + 2) == argument)
			return ip;
		int w = instruction_width(op);
		if (w <= 0)
			break;
		ip += w;
	}
	printf("OPTION-ARRAY instruction %#x %d not found\n", want, argument);
	exit(2);
}

/* X_A_INIT 0 of size 0 on an array<option<wrap<Worker>>> member: the empty
 * page a constructor gives such a member. (MatchingInfo's own @2 is "PUSH 3;
 * X_A_INIT 1", three none elements; array-literal AL7 runs that.) The owner
 * is dropped; the caller holds the only reference to the empty array. */
static int oa_new_array(void)
{
	int st = sp_sno("MatchingInfo"), owner = alloc_struct(st);
	if (ain->structures[st].destructor > 0) {
		printf("OPTION-ARRAY MatchingInfo has a destructor\n");
		exit(2);
	}
	int slot = wc_init_member(owner, sp_mem(st, "FrontWorker"), 0, oa_find("MapView@2", X_A_INIT, 0, 0));
	oa_failures += wc_failures;
	wc_failures = 0;
	heap_ref(slot);
	heap_unref(owner);
	if (!da_live_page(slot) || HEAP_REF(slot) != 1 || heap[slot].page->type != ARRAY_PAGE
	    || heap[slot].page->nr_vars != 0) {
		printf("  FAIL: X_A_INIT 0 did not leave an empty array\n");
		fflush(stdout);
		_exit(1);
	}
	return slot;
}

static int oa_obj(void)
{
	int st = sp_sno("CASColor");
	if (ain->structures[st].destructor > 0) {
		printf("OPTION-ARRAY CASColor has a destructor\n");
		exit(2);
	}
	return alloc_struct(st);
}

static struct page *oa_page(int slot)
{
	return da_live_page(slot) && heap[slot].page->type == ARRAY_PAGE ? heap[slot].page : NULL;
}

/* The physical slots of an array are exactly the given values. */
static bool oa_is(int slot, int n, const int *want)
{
	struct page *p = oa_page(slot);
	if (!p || p->nr_vars != n)
		return false;
	for (int i = 0; i < n; i++)
		if (p->values[i].i != want[i])
			return false;
	return true;
}

static void oa_show(const char *what, int slot)
{
	struct page *p = oa_page(slot);
	printf("    %s:", what);
	if (!p)
		printf(" no array page");
	else {
		printf(" a_type=%d stride=%d slots=%d [", p->a_type, p->array.struct_type, p->nr_vars);
		for (int i = 0; i < p->nr_vars && i < 16; i++)
			printf("%s%d", i ? " " : "", p->values[i].i);
		printf("]");
	}
	printf("\n");
}

#define OA_IS(slot, what, ...) do { \
	int oa_want_[] = { __VA_ARGS__ }; \
	if (!oa_is(slot, (int)(sizeof(oa_want_) / sizeof(oa_want_[0])), oa_want_)) { \
		OA_CHECK(false, "%s", what); oa_show("array", slot); } \
} while (0)

/* The generic layout every option array has: stride in array.struct_type. */
static void oa_layout(int slot, int stride, int elements, const char *what)
{
	struct page *p = oa_page(slot);
	OA_CHECK(p && (p->a_type == AIN_ARRAY || p->a_type == AIN_REF_ARRAY)
		&& p->array.struct_type == stride && p->nr_vars == stride * elements
		&& wc_size(slot) == elements,
		"%s: want %d element(s) of %d slot(s) on a generic page (X_A_SIZE %d)", what, elements, stride,
		p ? wc_size(slot) : -1);
	if (!(p && p->array.struct_type == stride && p->nr_vars == stride * elements))
		oa_show("array", slot);
}

/* [page, index, values...] X_OP_SET: leaves the values, as the bytecode's
 * two POPs expect. */
static void oa_op_set(uint32_t at, int page, int index, int n, const int *vals)
{
	size_t ip = instr_ptr;
	int sp = stack_ptr;
	stack_push(page);
	stack_push(index);
	for (int i = 0; i < n; i++)
		stack_push(vals[i]);
	instr_ptr = at;
	execute_instruction(X_OP_SET);
	instr_ptr = ip;
	OA_CHECK(stack_ptr == sp + n, "X_OP_SET left %d slot(s), want %d", stack_ptr - sp, n);
	stack_ptr = sp;
}

/* OptionalExtensions::HasValue<T>(wrap<option<T>> self): a position. */
static int oa_has_value(int fno, int page, int index)
{
	int args[] = { page, index };
	union vm_value r[2] = {{ .i = -1 }, { .i = -1 }};
	int ref = HEAP_REF(page);
	int n = da_callfunc(fno, 2, args, r);
	oa_failures += da_failures;
	da_failures = 0;
	OA_CHECK(n == 1 && HEAP_REF(page) == ref, "HasValue returned %d slot(s), array ref %d -> %d", n, ref, HEAP_REF(page));
	return r[0].i;
}

static int oa_push2(int arr, int value, int flag)
{
	int s[] = { arr, value, flag };
	return oa_hll("PushBack", 0, OA_REF, 3, s, NULL, 0);
}

static void oa_push_value(void)
{
	size_t baseline = live_slots();
	int arr = oa_new_array(), a = oa_obj(), b = oa_obj();
	int n = oa_push2(arr, a, 0);
	OA_CHECK(n == 0, "PushBack(value, 0) left %d slot(s): it must take [array, value, flag]", n);
	oa_layout(arr, 2, 1, "after one PushBack");
	OA_IS(arr, "the element is not (value, 0)", a, 0);
	OA_CHECK(oa_hll1("Count", 0, OA_REF, arr) == 1 && oa_hll1("Numof", 0, OA_REF, arr) == 1
		&& oa_hll1("Empty", 0, OA_REF, arr) == 0, "Count/Numof/Empty do not see one element");
	OA_CHECK(HEAP_REF(a) == 2, "the pushed object has %d reference(s), want 2 (its own and the array's)", HEAP_REF(a));
	n = oa_push2(arr, b, 0);
	OA_CHECK(n == 0, "second PushBack left %d slot(s)", n);
	oa_layout(arr, 2, 2, "after two PushBack");
	OA_IS(arr, "elements are not (a, 0) (b, 0)", a, 0, b, 0);
	heap_unref(arr);
	OA_CHECK(!da_live_page(arr) && HEAP_REF(a) == 1 && HEAP_REF(b) == 1,
		"releasing the array left refs %d/%d, want 1/1", HEAP_REF(a), HEAP_REF(b));
	heap_unref(a);
	heap_unref(b);
	OA_CHECK(live_slots() == baseline, "live slots %zu, want %zu", live_slots(), baseline);
	printf("  OA1 PushBack 0x30002 keeps (value, flag) and one reference per element\n");
}

static void oa_push_none(void)
{
	size_t baseline = live_slots();
	int arr = oa_new_array(), junk = oa_obj(), bystander = oa_obj();
	struct page *bp = heap[bystander].page;
	int nb = bp->nr_vars, bsum = 0;
	for (int i = 0; i < nb; i++)
		bsum += bp->values[i].i * (i + 1);
	size_t before = live_slots();
	int n = oa_push2(arr, junk, 1);
	OA_CHECK(n == 0, "PushBack(none) left %d slot(s)", n);
	oa_layout(arr, 2, 1, "after PushBack(none)");
	OA_IS(arr, "none is not stored as (-1, 1)", -1, 1);
	OA_CHECK(HEAP_REF(junk) == 1, "the value slot of none gained a reference (%d)", HEAP_REF(junk));
	OA_CHECK(live_slots() == before, "PushBack(none) created %zd heap slot(s)", (ssize_t)(live_slots() - before));
	int sum = 0;
	bp = da_live_page(bystander) ? heap[bystander].page : NULL;
	for (int i = 0; bp && i < nb && i < bp->nr_vars; i++)
		sum += bp->values[i].i * (i + 1);
	OA_CHECK(bp && bp->nr_vars == nb && sum == bsum, "an unrelated struct changed");
	n = oa_push2(arr, -1, 1);
	OA_CHECK(n == 0, "second PushBack(none) left %d slot(s)", n);
	OA_IS(arr, "two none elements are not (-1, 1) (-1, 1)", -1, 1, -1, 1);
	OA_CHECK(wc_size(arr) == 2 && oa_hll1("Count", 0, OA_REF, arr) == 2, "two none elements are not counted");
	heap_unref(arr);
	heap_unref(junk);
	heap_unref(bystander);
	OA_CHECK(live_slots() == baseline, "live slots %zu, want %zu", live_slots(), baseline);
	printf("  OA2 PushBack 0x30002 of none stores (-1, 1) and touches nothing else\n");
}

/* A WorkerCollection made by its real constructor. With an id it holds one
 * Worker of that Id and shows it; without, ShowcaseId is none. */
static int oa_shop(const char *id, int *worker)
{
	int st = sp_sno("WorkerCollection"), wst = sp_sno("Worker");
	int shop = sp_new("WorkerCollection");
	int m = sp_mem(st, "ShowcaseId");
	union vm_value *v = heap[shop].page->values;
	if (v[m].i > 1 && heap_index_valid(v[m].i))
		heap_unref(v[m].i);
	v[m].i = -1;
	v[m + 1].i = 1;
	*worker = -1;
	if (!id)
		return shop;
	int w = alloc_struct(wst);
	heap[w].page->values[sp_mem(wst, "Id")].i = sp_str(id);
	sp_arr_set(sp_get(sp_get(shop, "m_workers"), "m_list"), &w, 1, true);
	heap[shop].page->values[m].i = sp_str(id);
	heap[shop].page->values[m + 1].i = 0;
	*worker = w;
	return shop;
}

static void oa_select(void)
{
	const char *ids[3] = { "worker_a", "worker_b", NULL };
	int shops[3], workers[3];
	for (int i = 0; i < 3; i++)
		shops[i] = oa_shop(ids[i], &workers[i]);
	/* The shop's own lookup, on its own: (worker, 0) or (-1, 1). */
	for (int i = 0; i < 3; i++) {
		int sp = stack_ptr;
		vm_call(da_fn("WorkerCollection@GetShowCaseWorker"), shops[i]);
		int n = stack_ptr - sp, v0 = n == 2 ? stack[sp].i : -999, v1 = n == 2 ? stack[sp + 1].i : -999;
		stack_ptr = sp;
		if (!(n == 2 && v0 == workers[i] && v1 == (workers[i] > 0 ? 0 : 1))) {
			printf("  FAIL: GetShowCaseWorker(shop %d) returned %d slot(s) [%d, %d]\n", i, n, v0, v1);
			fflush(stdout);
			_exit(1);
		}
		if (v0 > 0)
			heap_unref(v0);
	}
	struct page *src = alloc_page(ARRAY_PAGE, AIN_ARRAY, 3);
	src->array.struct_type = 1;
	src->array.rank = 1;
	for (int i = 0; i < 3; i++) {
		src->values[i].i = shops[i];
		heap_ref(shops[i]);
	}
	int src_slot = heap_alloc_page(src);
	int wref[3];
	for (int i = 0; i < 3; i++)
		wref[i] = workers[i] > 0 ? HEAP_REF(workers[i]) : 0;
	/* Arrays alive before the call must keep their length: a shifted self
	 * used to be looked up among the Worker's leading int members. */
	enum { MAXA = 256 };
	int aslot[MAXA], alen[MAXA], na = 0;
	unsigned aseq[MAXA];
	for (size_t i = 2; i < heap_size && na < MAXA; i++)
		if (oa_page(i)) {
			aslot[na] = i;
			aseq[na] = heap[i].seq;
			alen[na++] = heap[i].page->nr_vars;
		}
	int selector = da_fn("SceneWorkMatching@<lambda : SceneWorkMatching@GetMatchedShopIndex()(250, 28)>");
	int select = da_fn("ArrayExtensions::Select<Worker&?, WorkerCollection&>");
	int dg = heap_alloc_page(delegate_new_from_method(-1, selector));
	oa_push_owner = select;
	oa_push_fn = oa_fn("PushBack", 0);
	oa_push_depth = 3;
	oa_push_calls = oa_push_bad = 0;
	unsigned long entered = calls[selector];
	int args[] = { src_slot, dg };
	union vm_value r[2] = {{ .i = -1 }, { .i = -1 }};
	oa_watch_push = true;
	int n = da_callfunc(select, 2, args, r);
	oa_watch_push = false;
	oa_failures += da_failures;
	da_failures = 0;
	int res = n >= 1 ? r[0].i : -1;
	struct page *rp = oa_page(res);
	printf("    Select returned %d slot(s); result has %d physical slot(s), X_A_SIZE %d; selector entered %lu time(s); "
		"%u PushBack call(s), %u with a stack other than [result, value, flag]\n",
		n, rp ? rp->nr_vars : -1, rp ? wc_size(res) : -1, calls[selector] - entered, oa_push_calls, oa_push_bad);
	if (rp)
		oa_show("result", res);
	OA_CHECK(n == 1 && rp, "Select did not return an array");
	OA_CHECK(calls[selector] - entered == 3 && oa_push_calls == 3,
		"selector/PushBack ran %lu/%u time(s), want 3/3", calls[selector] - entered, oa_push_calls);
	OA_CHECK(oa_push_bad == 0, "%u PushBack call(s) found leftovers of an earlier one on the stack", oa_push_bad);
	if (rp) {
		oa_layout(res, 2, 3, "Select result");
		OA_IS(res, "result is not (worker_a, 0) (worker_b, 0) (-1, 1)", workers[0], 0, workers[1], 0, -1, 1);
	}
	for (int i = 0; i < 2 && rp && rp->nr_vars == 6; i++)
		OA_CHECK(HEAP_REF(workers[i]) == wref[i] + 1, "worker %d has %d reference(s), want %d",
			i, HEAP_REF(workers[i]), wref[i] + 1);
	for (int i = 0; i < na; i++)
		if (oa_page(aslot[i]) && heap[aslot[i]].seq == aseq[i])
			OA_CHECK(heap[aslot[i]].page->nr_vars == alen[i], "array slot %d changed length %d -> %d",
				aslot[i], alen[i], heap[aslot[i]].page->nr_vars);
	if (da_live_page(res))
		heap_unref(res);
	for (int i = 0; i < 2; i++)
		OA_CHECK(da_live_page(workers[i]) && HEAP_REF(workers[i]) == wref[i],
			"dropping the result left worker %d with %d reference(s), want %d", i, HEAP_REF(workers[i]), wref[i]);
	printf("  OA3 real Select<Worker&?, WorkerCollection&>: three shops, the third without a showcase\n");
}

/* A hand-made array<option<wrap<T>>> page holding the given elements; the
 * page takes one reference to every present value. */
static int oa_make(int stride, int elements, const int *vals)
{
	struct page *p = alloc_page(ARRAY_PAGE, AIN_ARRAY, stride * elements);
	p->array.struct_type = stride;
	p->array.rank = 1;
	for (int i = 0; i < stride * elements; i++) {
		p->values[i].i = vals[i];
		if (i % stride == 0 && vals[i] > 1)
			heap_ref(vals[i]);
	}
	return heap_alloc_page(p);
}

static void oa_where(void)
{
	int a = oa_obj(), b = oa_obj();
	int vals[] = { a, 0, -1, 1, b, 0 };
	int src = oa_make(2, 3, vals);
	int pred = da_fn("ItemStock@<lambda : ItemStock@ToItem()(52, 20)>");
	oa_pred_fn = pred;
	oa_pred_n = 0;
	int s[] = { src, -1, pred }, out[2] = { -1, -1 };
	int n = oa_hll("Where", 0, OA_REF, 3, s, out, 2);
	oa_pred_fn = -1;
	OA_CHECK(n == 1, "Where left %d slot(s), want the result array", n);
	OA_CHECK(oa_pred_n == 3 && oa_pred_seen[0][0] == a && oa_pred_seen[0][1] == 0
		&& oa_pred_seen[1][0] == -1 && oa_pred_seen[1][1] == 1
		&& oa_pred_seen[2][0] == b && oa_pred_seen[2][1] == 0,
		"the predicate was entered %d time(s) with [%d,%d] [%d,%d] [%d,%d]; want each element once, whole",
		oa_pred_n, oa_pred_seen[0][0], oa_pred_seen[0][1], oa_pred_seen[1][0], oa_pred_seen[1][1],
		oa_pred_seen[2][0], oa_pred_seen[2][1]);
	int res = n == 1 ? out[0] : -1;
	oa_layout(res, 2, 2, "Where result");
	OA_IS(res, "Where did not keep (a, 0) (b, 0)", a, 0, b, 0);
	OA_CHECK(HEAP_REF(a) == 3 && HEAP_REF(b) == 3, "after Where the values have %d/%d reference(s), want 3/3",
		HEAP_REF(a), HEAP_REF(b));
	OA_IS(src, "Where changed its source", a, 0, -1, 1, b, 0);
	/* ItemStock@ToItem goes on: Select<Item&, Item&?>(that, obj => obj.Value). */
	int out2 = -1;
	if (oa_page(res)) {
		int selector = da_fn("ItemStock@<lambda : ItemStock@ToItem()(53, 28)>");
		int dg = heap_alloc_page(delegate_new_from_method(-1, selector));
		int args[] = { res, dg };
		union vm_value r[2] = {{ .i = -1 }, { .i = -1 }};
		unsigned long entered = calls[selector];
		int m = da_callfunc(da_fn("ArrayExtensions::Select<Item&, Item&?>"), 2, args, r);
		oa_failures += da_failures;
		da_failures = 0;
		out2 = m >= 1 ? r[0].i : -1;
		OA_CHECK(m == 1 && calls[selector] - entered == 2, "Select<Item&, Item&?> returned %d slot(s), selector entered %lu time(s)",
			m, calls[selector] - entered);
		OA_IS(out2, "Select<Item&, Item&?> did not unwrap to [a, b]", a, b);
	}
	if (da_live_page(out2))
		heap_unref(out2);
	if (da_live_page(res))
		heap_unref(res);
	heap_unref(src);
	int nothing = oa_make(2, 0, vals);
	int s0[] = { nothing, -1, pred };
	n = oa_hll("Where", 0, OA_REF, 3, s0, out, 2);
	OA_CHECK(n == 1 && oa_page(out[0]) && wc_size(out[0]) == 0, "Where on an empty array did not return an empty array");
	OA_CHECK(da_live_page(a) && da_live_page(b) && HEAP_REF(a) == 1 && HEAP_REF(b) == 1,
		"after releasing every array the values have %d/%d reference(s), want 1/1", HEAP_REF(a), HEAP_REF(b));
	printf("  OA4 Where 0x30002 passes whole elements; the real Select<Item&, Item&?> unwraps the result\n");
}

static void oa_add_string(void)
{
	int has = da_fn("OptionalExtensions::HasValue<string>");
	int arr = oa_new_array();
	int s1 = sp_str("alpha"), s2 = sp_str("beta");
	int a1[] = { arr, s1, 0 }, a2[] = { arr, -1, 1 }, a3[] = { arr, s2, 0 };
	int n1 = oa_hll("Add", 0, OA_STR, 3, a1, NULL, 0);
	int n2 = oa_hll("Add", 0, OA_STR, 3, a2, NULL, 0);
	int n3 = oa_hll("Add", 0, OA_STR, 3, a3, NULL, 0);
	OA_CHECK(n1 == 0 && n2 == 0 && n3 == 0, "Add 0x20002 left %d/%d/%d slot(s)", n1, n2, n3);
	oa_layout(arr, 2, 3, "after three Add");
	OA_IS(arr, "elements are not (alpha, 0) (-1, 1) (beta, 0)", s1, 0, -1, 1, s2, 0);
	if (oa_page(arr) && oa_page(arr)->nr_vars == 6)
		OA_CHECK(oa_has_value(has, arr, 0) == 1 && oa_has_value(has, arr, 2) == 0 && oa_has_value(has, arr, 4) == 1,
			"HasValue<string> at 0/2/4 is not true/false/true");
	/* The same string pushed onto a string array (operand 2) is the measure
	 * of what a by-value string element costs in references. This pins
	 * xsystem4's own convention, a shared string slot with one more
	 * reference; natively the element gets its own copy of the text
	 * (0x646d70 at 0x646e04, vtable +0x3c). */
	struct page *sp_ = alloc_page(ARRAY_PAGE, AIN_ARRAY_STRING, 0);
	sp_->array.struct_type = -1;
	sp_->array.rank = 1;
	int plain = heap_alloc_page(sp_), s3 = sp_str("gamma");
	int p1[] = { plain, s3 };
	int n4 = oa_hll("PushBack", 0, 2, 2, p1, NULL, 0);
	OA_CHECK(n4 == 0 && HEAP_REF(s1) == HEAP_REF(s3) && HEAP_REF(s2) == HEAP_REF(s3),
		"string references %d/%d differ from a plain string PushBack's %d", HEAP_REF(s1), HEAP_REF(s2), HEAP_REF(s3));
	printf("  OA5 Add 0x20002: three elements, HasValue<string> at 0 and 4, references as for a string array (%d)\n", HEAP_REF(s3));
}

static void oa_alloc(void)
{
	uint32_t set = oa_find("MapView@LoadNodes", X_OP_SET, 0x10002, 0);
	int arr = oa_new_array(), a = oa_obj(), b = oa_obj();
	size_t before = live_slots();
	int s[] = { arr, 3 };
	int n = oa_hll("Alloc", 0, OA_REF, 2, s, NULL, 0);
	OA_CHECK(n == 0, "Alloc left %d slot(s)", n);
	oa_layout(arr, 2, 3, "after Alloc(3)");
	OA_IS(arr, "Alloc did not make three none elements", -1, 1, -1, 1, -1, 1);
	OA_CHECK(live_slots() == before, "Alloc created %zd heap object(s)", (ssize_t)(live_slots() - before));
	OA_CHECK(oa_hll1("Count", 0, OA_REF, arr) == 3, "Count is not 3");
	if (oa_page(arr) && oa_page(arr)->nr_vars == 6) {
		int v[] = { a, 0 };
		oa_op_set(set, arr, 2, 2, v);
		OA_IS(arr, "X_OP_SET did not write element 1", -1, 1, a, 0, -1, 1);
		OA_CHECK(HEAP_REF(a) == 2, "the stored object has %d reference(s), want 2", HEAP_REF(a));
		int e[] = { arr, 1, 1 }, out[1] = { -1 };
		n = oa_hll("Erase", 0, OA_REF, 3, e, out, 1);
		OA_CHECK(n == 1 && out[0] == 1, "Erase(1, 1) returned %d slot(s), value %d", n, out[0]);
		oa_layout(arr, 2, 2, "after Erase(1, 1)");
		OA_IS(arr, "Erase(1, 1) did not leave two none elements", -1, 1, -1, 1);
		OA_CHECK(HEAP_REF(a) == 1, "the erased object has %d reference(s), want 1", HEAP_REF(a));
		int v2[] = { b, 0 };
		oa_op_set(set, arr, 0, 2, v);
		oa_op_set(set, arr, 2, 2, v2);
		int e2[] = { arr, 0, 1 };
		n = oa_hll("Erase", 0, OA_REF, 3, e2, out, 1);
		OA_CHECK(n == 1 && out[0] == 1 && HEAP_REF(a) == 1 && HEAP_REF(b) == 2, "Erase(0, 1): result %d, refs %d/%d",
			out[0], HEAP_REF(a), HEAP_REF(b));
		OA_IS(arr, "Erase(0, 1) did not leave (b, 0)", b, 0);
		int e3[] = { arr, 5, 1 };
		n = oa_hll("Erase", 0, OA_REF, 3, e3, out, 1);
		OA_CHECK(n == 1 && out[0] == 0, "Erase past the end returned %d", out[0]);
		/* Alloc starts over: the old elements are released, not kept. */
		int s2[] = { arr, 2 };
		n = oa_hll("Alloc", 0, OA_REF, 2, s2, NULL, 0);
		OA_CHECK(n == 0 && HEAP_REF(b) == 1, "a second Alloc kept the old element (ref %d)", HEAP_REF(b));
		OA_IS(arr, "a second Alloc(2) is not two none elements", -1, 1, -1, 1);
		int s3[] = { arr, 0 };
		n = oa_hll("Alloc", 0, OA_REF, 2, s3, NULL, 0);
		OA_CHECK(n == 0 && wc_size(arr) == 0 && oa_hll1("Count", 0, OA_REF, arr) == 0, "Alloc(0) is not empty");
		/* Erasing the only element leaves an empty array that still takes
		 * option elements. */
		oa_push2(arr, a, 0);
		int e4[] = { arr, 0, 1 };
		n = oa_hll("Erase", 0, OA_REF, 3, e4, out, 1);
		OA_CHECK(n == 1 && out[0] == 1 && HEAP_REF(a) == 1 && wc_size(arr) == 0
			&& oa_hll1("Count", 0, OA_REF, arr) == 0, "erasing the only element: result %d, ref %d, size %d",
			out[0], HEAP_REF(a), wc_size(arr));
		oa_push2(arr, b, 0);
		oa_layout(arr, 2, 1, "PushBack after erasing the only element");
		OA_IS(arr, "PushBack after erasing the only element is not (b, 0)", b, 0);
		OA_CHECK(HEAP_REF(b) == 2, "PushBack onto the erased array left ref %d, want 2", HEAP_REF(b));
		/* The item lists' own order: Erase to empty, then Alloc
		 * (PartyItemButtonCollection@Reset). */
		n = oa_hll("Erase", 0, OA_REF, 3, e4, out, 1);
		OA_CHECK(n == 1 && out[0] == 1 && HEAP_REF(b) == 1 && wc_size(arr) == 0, "erasing to empty again: result %d, ref %d",
			out[0], HEAP_REF(b));
		n = oa_hll("Alloc", 0, OA_REF, 2, s2, NULL, 0);
		OA_CHECK(n == 0, "Alloc after erasing to empty left %d slot(s)", n);
		oa_layout(arr, 2, 2, "Alloc after erasing to empty");
		OA_IS(arr, "Alloc(2) after erasing to empty is not two none elements", -1, 1, -1, 1);
		oa_push2(arr, a, 0);
		OA_IS(arr, "PushBack after that Alloc is not none none (a, 0)", -1, 1, -1, 1, a, 0);
		OA_CHECK(HEAP_REF(a) == 2, "PushBack after that Alloc left ref %d, want 2", HEAP_REF(a));
	}
	heap_unref(arr);
	OA_CHECK(!da_live_page(arr) && HEAP_REF(a) == 1 && HEAP_REF(b) == 1, "releasing the array left refs %d/%d, want 1/1",
		HEAP_REF(a), HEAP_REF(b));
	printf("  OA6 Alloc 0x30002 makes none elements and starts over; Count, X_OP_SET and Erase work on elements\n");
}

/* What MapView@LoadNodes does to m_uiNode, with "inside" decided here. */
static void oa_nodes(int n)
{
	int st = sp_sno("MapView"), owner = alloc_struct(st);
	int arr = wc_init_member(owner, sp_mem(st, "m_uiNode"), 0, oa_find("MapView@2", X_A_INIT, 0, 0));
	oa_failures += wc_failures;
	wc_failures = 0;
	uint32_t set_value = oa_find("MapView@LoadNodes", X_OP_SET, 0x10002, 0);
	uint32_t set_none = oa_find("MapView@LoadNodes", X_OP_SET, 0x10002, 1);
	int has = da_fn("OptionalExtensions::HasValue<MapNodeView&>");
	int s[] = { arr, n };
	int left = oa_hll("Alloc", 0, OA_REF, 2, s, NULL, 0);
	struct page *p = oa_page(arr);
	printf("    %d nodes: Alloc left %d slot(s); page has %d slot(s), stride %d, X_A_SIZE %d\n",
		n, left, p ? p->nr_vars : -1, p ? p->array.struct_type : -1, wc_size(arr));
	fflush(stdout);
	OA_CHECK(left == 0, "Alloc left %d slot(s)", left);
	oa_layout(arr, 2, n, "m_uiNode after Alloc");
	int views[16] = { 0 }, size = wc_size(arr), wrong = 0;
	/* First pass: nothing has a value; nodes with an even index are in view. */
	for (int i = 0; i < size && i < 16; i++) {
		int h = oa_has_value(has, arr, i * 2);
		wrong += h != 0;
		if (i % 2 == 0 && !h) {
			views[i] = oa_obj();
			int v[] = { views[i], 0 };
			oa_op_set(set_value, arr, i * 2, 2, v);
		} else if (i % 2 && h) {
			int v[] = { -1, 1 };
			oa_op_set(set_none, arr, i * 2, 2, v);
		}
	}
	OA_CHECK(wrong == 0, "%d of %d fresh element(s) claimed to have a value", wrong, size);
	OA_CHECK(size == n, "the loop ran over %d element(s), want %d", size, n);
	p = oa_page(arr);
	OA_CHECK(p && p->nr_vars == 2 * n, "the page has %d slot(s) after the first pass, want %d", p ? p->nr_vars : -1, 2 * n);
	int bad = 0;
	for (int i = 0; i < size && i < 16; i++) {
		int h = oa_has_value(has, arr, i * 2);
		bad += h != (i % 2 == 0) || (views[i] && HEAP_REF(views[i]) != 2);
	}
	OA_CHECK(bad == 0, "%d element(s) are wrong after the first pass", bad);
	/* Second pass: everything is out of view. */
	for (int i = 0; i < size && i < 16; i++)
		if (oa_has_value(has, arr, i * 2)) {
			int v[] = { -1, 1 };
			oa_op_set(set_none, arr, i * 2, 2, v);
		}
	bad = 0;
	p = oa_page(arr);
	for (int i = 0; i < size && i < 16; i++)
		bad += oa_has_value(has, arr, i * 2) != 0 || (views[i] && HEAP_REF(views[i]) != 1)
			|| !p || p->values[i * 2].i != -1 || p->values[i * 2 + 1].i != 1;
	OA_CHECK(bad == 0 && p && p->nr_vars == 2 * n, "%d element(s) are not none after the second pass (%d slots)",
		bad, p ? p->nr_vars : -1);
}

static void oa_load_nodes(void)
{
	oa_nodes(5);
	oa_nodes(6);
	printf("  OA7 the accesses of MapView@LoadNodes stay inside the array MapView@0 allocates\n");
}

/* X_REF 2 through a position, the way bytecode reads an option element. */
static void oa_read(int page, int index, int out[2])
{
	uint32_t at = oa_find("MapView@LoadNodes", X_REF, 2, 0);
	size_t ip = instr_ptr;
	int sp = stack_ptr;
	stack_push(page);
	stack_push(index);
	instr_ptr = at;
	execute_instruction(X_REF);
	instr_ptr = ip;
	out[0] = stack_ptr - sp == 2 ? stack[sp].i : -999;
	out[1] = stack_ptr - sp == 2 ? stack[sp + 1].i : -999;
	stack_ptr = sp;
}

static void oa_positions(void)
{
	int pred = da_fn("ItemStock@<lambda : ItemStock@ToItem()(52, 20)>");
	int a = oa_obj(), b = oa_obj();
	int vals[] = { -1, 1, a, 0, b, 0 };
	int arr = oa_make(2, 3, vals);
	int ref = HEAP_REF(arr), e[2];
	oa_pred_fn = pred;
	oa_pred_n = 0;
	int s[] = { arr, -1, pred }, out[3] = { -999, -999, -999 };
	int n = oa_hll("First", 3, OA_REF, 3, s, out, 3);
	oa_pred_fn = -1;
	OA_CHECK(n == 2 && out[0] == arr && out[1] == 2, "First(pred) returned %d slot(s) [%d, %d], want the position [array, 2]",
		n, out[0], out[1]);
	OA_CHECK(oa_pred_n == 2 && oa_pred_seen[0][0] == -1 && oa_pred_seen[0][1] == 1
		&& oa_pred_seen[1][0] == a && oa_pred_seen[1][1] == 0,
		"the predicate was entered %d time(s) with [%d,%d] [%d,%d]", oa_pred_n,
		oa_pred_seen[0][0], oa_pred_seen[0][1], oa_pred_seen[1][0], oa_pred_seen[1][1]);
	/* A position is not counted here: nothing in this VM releases one
	 * (its only holder, the type 87 local of MapView@GetNode, is not
	 * released with the page), so the call must not retain the array. */
	OA_CHECK(HEAP_REF(arr) == ref, "First(pred) left the array with %d reference(s), want %d", HEAP_REF(arr), ref);
	if (n == 2 && out[0] == arr) {
		oa_read(out[0], out[1], e);
		OA_CHECK(e[0] == a && e[1] == 0, "X_REF 2 through the position read [%d, %d], want (a, 0)", e[0], e[1]);
	}
	OA_CHECK(HEAP_REF(a) == 2 && HEAP_REF(b) == 2, "First(pred) changed the values' references (%d/%d)", HEAP_REF(a), HEAP_REF(b));
	int i2[] = { arr, 2 }, i5[] = { arr, 5 };
	n = oa_hll("At", 1, OA_REF, 2, i2, out, 3);
	OA_CHECK(n == 2 && out[0] == arr && out[1] == 4, "At(2) returned %d slot(s) [%d, %d], want [array, 4]", n, out[0], out[1]);
	if (n == 2 && out[0] == arr) {
		oa_read(out[0], out[1], e);
		OA_CHECK(e[0] == b && e[1] == 0, "X_REF 2 through At(2) read [%d, %d], want (b, 0)", e[0], e[1]);
	}
	n = oa_hll("At", 1, OA_REF, 2, i5, out, 3);
	OA_CHECK(n == 2 && out[0] == -1 && out[1] == 0, "At(5) returned %d slot(s) [%d, %d], want [-1, 0]", n, out[0], out[1]);
	n = oa_hll("Last", 1, OA_REF, 1, &arr, out, 3);
	OA_CHECK(n == 2 && out[0] == arr && out[1] == 4, "Last() returned %d slot(s) [%d, %d], want [array, 4]", n, out[0], out[1]);
	n = oa_hll("First", 1, OA_REF, 1, &arr, out, 3);
	OA_CHECK(n == 2 && out[0] == arr && out[1] == 0, "First() returned %d slot(s) [%d, %d], want [array, 0]", n, out[0], out[1]);
	if (n == 2 && out[0] == arr) {
		oa_read(out[0], out[1], e);
		OA_CHECK(e[0] == -1 && e[1] == 1, "X_REF 2 through First() read [%d, %d], want none", e[0], e[1]);
	}
	OA_CHECK(HEAP_REF(arr) == ref, "At/Last/First left the array with %d reference(s), want %d", HEAP_REF(arr), ref);
	int none[] = { -1, 1, -1, 1 };
	int empty = oa_make(2, 2, none);
	int s2[] = { empty, -1, pred };
	n = oa_hll("First", 3, OA_REF, 3, s2, out, 3);
	OA_CHECK(n == 2 && out[0] == -1 && out[1] == 0, "First(pred) without a match returned %d slot(s) [%d, %d], want [-1, 0]",
		n, out[0], out[1]);
	/* A page that does not have the option layout (one slot per element,
	 * as an array literal is still allocated): the result keeps the shape
	 * of a position and the predicate is not fed single slots. */
	struct page *fp = alloc_page(ARRAY_PAGE, AIN_ARRAY, 2);
	fp->array.struct_type = 1;
	fp->array.rank = 1;
	fp->values[0].i = a;
	fp->values[1].i = 0;
	heap_ref(a);
	int flat = heap_alloc_page(fp);
	int s3[] = { flat, -1, pred };
	oa_pred_fn = pred;
	oa_pred_n = 0;
	n = oa_hll("First", 3, OA_REF, 3, s3, out, 3);
	oa_pred_fn = -1;
	OA_CHECK(n == 2 && out[0] == -1 && out[1] == 0 && oa_pred_n == 0,
		"First(pred) on a one-slot page returned %d slot(s) [%d, %d] after %d predicate call(s), want [-1, 0] and none",
		n, out[0], out[1], oa_pred_n);
	heap_unref(flat);
	heap_unref(empty);
	heap_unref(arr);
	OA_CHECK(!da_live_page(arr) && HEAP_REF(a) == 1 && HEAP_REF(b) == 1, "values have %d/%d reference(s) at the end",
		HEAP_REF(a), HEAP_REF(b));
	printf("  OA8 First(pred), At, Last and First return an uncounted position into an option array\n");
}

/* The real MapView@GetNode over a real MapView's m_uiNode: First with the
 * script's own predicate (it reads the id through its closure environment
 * and calls MapNodeView@Id::get), the position kept in the function's type
 * 87 local and read back with X_REF 2. */
static void oa_get_node(void)
{
	int st = sp_sno("MapView"), vst = sp_sno("MapNodeView"), nst = sp_sno("MapNode");
	if (ain->structures[st].destructor > 0 || ain->structures[vst].destructor > 0
	    || ain->structures[nst].destructor > 0) {
		printf("OPTION-ARRAY MapView, MapNodeView or MapNode has a destructor\n");
		exit(2);
	}
	int map = alloc_struct(st);
	int arr = wc_init_member(map, sp_mem(st, "m_uiNode"), 0, oa_find("MapView@2", X_A_INIT, 0, 0));
	oa_failures += wc_failures;
	wc_failures = 0;
	uint32_t set = oa_find("MapView@LoadNodes", X_OP_SET, 0x10002, 0);
	int s[] = { arr, 4 };
	oa_hll("Alloc", 0, OA_REF, 2, s, NULL, 0);
	const int ids[4] = { 11, 0, 13, 14 };	/* element 1 stays none */
	int views[4] = { 0 };
	if (!oa_page(arr) || oa_page(arr)->nr_vars != 8) {
		OA_CHECK(false, "Alloc 0x30002 (4) did not make four two-slot elements");
		oa_show("m_uiNode", arr);
		return;
	}
	for (int i = 0; i < 4; i++) {
		if (!ids[i])
			continue;
		views[i] = alloc_struct(vst);
		union vm_value *node = &heap[views[i]].page->values[sp_mem(vst, "m_node")];
		if (node->i <= 0)
			node->i = alloc_struct(nst);
		heap[node->i].page->values[sp_mem(nst, "Id")].i = ids[i];
		int v[] = { views[i], 0 };
		oa_op_set(set, arr, i * 2, 2, v);
	}
	int get = da_fn("MapView@GetNode"), pred = da_fn("MapView@<lambda : MapView@GetNode(int)(56, 31)>");
	int ref = HEAP_REF(arr), found = 0;
	const int ask[4] = { 13, 11, 14, 13 }, at[4] = { 2, 0, 3, 2 };
	unsigned long entered = calls[pred];
	for (int k = 0; k < 4; k++) {
		int sp = stack_ptr, csp = call_stack_ptr;
		size_t ip = instr_ptr;
		stack_push(map);
		stack_push(ask[k]);
		method_call(get, VM_RETURN);
		vm_execute();
		instr_ptr = ip;
		int n = stack_ptr - sp, v0 = n == 2 ? stack[sp].i : -999, v1 = n == 2 ? stack[sp + 1].i : -999;
		stack_ptr = sp;
		OA_CHECK(call_stack_ptr == csp && n == 2 && v0 == views[at[k]] && v1 == 0,
			"GetNode(%d) returned %d slot(s) [%d, %d], want (view %d, 0)", ask[k], n, v0, v1, at[k]);
		if (n == 2 && v0 == views[at[k]] && v1 == 0) {
			found++;
			/* the returned option owns its value (X_DUP 1; SP_INC) */
			OA_CHECK(HEAP_REF(v0) == 3, "the returned view has %d reference(s), want 3", HEAP_REF(v0));
			heap_unref(v0);
		}
		OA_CHECK(HEAP_REF(arr) == ref, "after GetNode(%d) the node array has %d reference(s), want %d (the position in the "
			"function's local is not released with it)", ask[k], HEAP_REF(arr), ref);
	}
	/* 13: elements 0, 1 (none), 2; 11: element 0; 14: all four; 13 again */
	OA_CHECK(calls[pred] - entered == 3 + 1 + 4 + 3, "the predicate ran %lu time(s), want 11", calls[pred] - entered);
	/* The map is the array's only owner, and the array the views' last. */
	for (int i = 0; i < 4; i++)
		if (views[i] && found == 4) {
			OA_CHECK(HEAP_REF(views[i]) == 2, "view %d has %d reference(s) before the probe lets go, want 2", i, HEAP_REF(views[i]));
			heap_unref(views[i]);
		}
	heap_unref(map);
	int alive = 0;
	for (int i = 0; i < 4; i++)
		alive += views[i] && da_live_page(views[i]);
	OA_CHECK(!da_live_page(map) && !da_live_page(arr) && (found != 4 || alive == 0),
		"releasing the MapView left the node array %s and %d view(s) alive", da_live_page(arr) ? "alive" : "released", alive);
	printf("  OA11 the real MapView@GetNode finds its nodes and leaves the node array's references alone\n");
}

/* A struct whose destructor is a bare RETURN and is run by this VM for an
 * object that was only allocated (FrameLayerImages and the like). */
static int oa_dtor_struct(void)
{
	for (int i = 0; i < ain->nr_structures; i++) {
		struct ain_struct *s = &ain->structures[i];
		int d = s->destructor;
		if (d <= 0 || d >= ain->nr_functions || ain->functions[d].address + 2 > ain->code_size
		    || get_opcode(ain->functions[d].address) != RETURN)
			continue;
		unsigned long ran = calls[d];
		heap_unref(alloc_struct(i));
		if (calls[d] == ran + 1)
			return i;
	}
	printf("OPTION-ARRAY no struct with a bare destructor\n");
	exit(2);
}

/* Alloc and Realloc put the new page in place before they release the old
 * elements: a destructor that runs meanwhile finds the new array. */
static void oa_resize_order(void)
{
	int st = oa_dtor_struct(), dtor = ain->structures[st].destructor;
	uint32_t set = oa_find("MapView@LoadNodes", X_OP_SET, 0x10002, 0);
	int arr = oa_new_array();
	int s2[] = { arr, 2 }, s3[] = { arr, 3 }, s1[] = { arr, 1 };
	oa_hll("Alloc", 0, OA_REF, 2, s2, NULL, 0);
	int obj = alloc_struct(st);
	int v[] = { obj, 0 };
	oa_op_set(set, arr, 0, 2, v);
	heap_unref(obj);	/* the array is its only owner now */
	OA_CHECK(da_live_page(obj) && HEAP_REF(obj) == 1, "the element is not held by the array alone");
	oa_dtor_fn = dtor;
	oa_dtor_arr = arr;
	oa_dtor_seen = 0;
	int n = oa_hll("Alloc", 0, OA_REF, 2, s3, NULL, 0);
	OA_CHECK(n == 0 && oa_dtor_seen == 1, "Alloc ran the old element's destructor %d time(s), want 1", oa_dtor_seen);
	OA_CHECK(oa_dtor_seen != 1 || (oa_dtor_slots == 6 && oa_dtor_none),
		"during Alloc(3) the destructor found a page of %d slot(s)%s; want the new one: 6 slots, all none",
		oa_dtor_slots, oa_dtor_none ? "" : " that still held an element");
	OA_IS(arr, "Alloc(3) is not three none elements", -1, 1, -1, 1, -1, 1);
	obj = alloc_struct(st);
	v[0] = obj;
	if (oa_page(arr) && oa_page(arr)->nr_vars == 6)
		oa_op_set(set, arr, 4, 2, v);
	heap_unref(obj);
	oa_dtor_seen = 0;
	n = oa_hll("Realloc", 0, OA_REF, 2, s1, NULL, 0);
	oa_dtor_fn = -1;
	OA_CHECK(n == 0 && oa_dtor_seen == 1, "Realloc ran the removed element's destructor %d time(s), want 1", oa_dtor_seen);
	OA_CHECK(oa_dtor_seen != 1 || (oa_dtor_slots == 2 && oa_dtor_none),
		"during Realloc(1) the destructor found a page of %d slot(s)%s; want the new one: 2 slots, none",
		oa_dtor_slots, oa_dtor_none ? "" : " that still held an element");
	heap_unref(arr);
	printf("  OA12 Alloc and Realloc install the new page before a removed element's destructor (%s) runs\n",
		ain->functions[dtor].name);
}

static void oa_realloc(void)
{
	uint32_t set = oa_find("MapView@LoadNodes", X_OP_SET, 0x10002, 0);
	int arr = oa_new_array(), a = oa_obj(), b = oa_obj();
	oa_push2(arr, a, 0);
	int s[] = { arr, 3 };
	int n = oa_hll("Realloc", 0, OA_REF, 2, s, NULL, 0);
	OA_CHECK(n == 0, "Realloc left %d slot(s)", n);
	oa_layout(arr, 2, 3, "after Realloc(3)");
	OA_IS(arr, "Realloc(3) is not (a, 0) (-1, 1) (-1, 1)", a, 0, -1, 1, -1, 1);
	if (oa_page(arr) && oa_page(arr)->nr_vars == 6) {
		int v[] = { b, 0 };
		oa_op_set(set, arr, 4, 2, v);
		int s1[] = { arr, 1 };
		n = oa_hll("Realloc", 0, OA_REF, 2, s1, NULL, 0);
		OA_CHECK(n == 0 && HEAP_REF(b) == 1, "Realloc(1) kept the removed element (ref %d)", HEAP_REF(b));
		OA_IS(arr, "Realloc(1) is not (a, 0)", a, 0);
		OA_CHECK(HEAP_REF(a) == 2, "Realloc(1) changed the kept element's references (%d)", HEAP_REF(a));
		int s0[] = { arr, 0 };
		n = oa_hll("Realloc", 0, OA_REF, 2, s0, NULL, 0);
		OA_CHECK(n == 0 && HEAP_REF(a) == 1 && wc_size(arr) == 0 && oa_hll1("Count", 0, OA_REF, arr) == 0,
			"Realloc(0) is not empty (ref %d, size %d)", HEAP_REF(a), wc_size(arr));
	}
	printf("  OA9 Realloc 0x30002 keeps elements, adds none elements and releases the removed tail\n");
}

static void oa_insert(void)
{
	int arr = oa_new_array(), a = oa_obj(), b = oa_obj(), c = oa_obj();
	oa_push2(arr, a, 0);
	oa_push2(arr, b, 0);
	int i1[] = { arr, 1, c, 0 }, i0[] = { arr, 0, c, 1 };
	int n = oa_hll("Insert", 0, OA_REF, 4, i1, NULL, 0);
	OA_CHECK(n == 0 && HEAP_REF(c) == 2, "Insert left %d slot(s), ref %d", n, HEAP_REF(c));
	OA_IS(arr, "Insert(1, c) is not a c b", a, 0, c, 0, b, 0);
	n = oa_hll("Insert", 0, OA_REF, 4, i0, NULL, 0);
	OA_CHECK(n == 0 && HEAP_REF(c) == 2, "Insert of none left %d slot(s), ref %d", n, HEAP_REF(c));
	OA_IS(arr, "Insert(0, none) is not none a c b", -1, 1, a, 0, c, 0, b, 0);
	oa_layout(arr, 2, 4, "after two Insert");
	n = oa_hll("PopBack", 0, OA_REF, 1, &arr, NULL, 0);
	OA_CHECK(n == 0 && HEAP_REF(b) == 1, "PopBack left %d slot(s), ref %d", n, HEAP_REF(b));
	OA_IS(arr, "PopBack is not none a c", -1, 1, a, 0, c, 0);
	n = oa_hll("Clear", 0, OA_REF, 1, &arr, NULL, 0);
	OA_CHECK(n == 0 && HEAP_REF(a) == 1 && HEAP_REF(c) == 1 && wc_size(arr) == 0, "Clear: refs %d/%d, size %d",
		HEAP_REF(a), HEAP_REF(c), wc_size(arr));
	oa_push2(arr, a, 0);
	oa_layout(arr, 2, 1, "PushBack after Clear");
	n = oa_hll("Free", 0, OA_REF, 1, &arr, NULL, 0);
	OA_CHECK(n == 0 && HEAP_REF(a) == 1 && oa_hll1("Count", 0, OA_REF, arr) == 0, "Free: ref %d", HEAP_REF(a));
	oa_push2(arr, b, 1);
	oa_layout(arr, 2, 1, "PushBack after Free");
	OA_IS(arr, "PushBack(none) after Free is not (-1, 1)", -1, 1);
	/* Concat onto the empty array X_A_INIT 0 makes: whole elements, one
	 * more reference per value. */
	int dst = oa_new_array();
	int vals[] = { a, 0, -1, 1 };
	int src = oa_make(2, 2, vals);
	int cc[] = { dst, src };
	n = oa_hll("Concat", 0, OA_REF, 2, cc, NULL, 0);
	OA_CHECK(n == 0 && HEAP_REF(a) == 3, "Concat left %d slot(s), ref %d (want 3)", n, HEAP_REF(a));
	oa_layout(dst, 2, 2, "after Concat");
	OA_IS(dst, "Concat did not append (a, 0) (-1, 1)", a, 0, -1, 1);
	heap_unref(dst);
	heap_unref(src);
	OA_CHECK(HEAP_REF(a) == 1, "releasing both arrays left ref %d", HEAP_REF(a));
	printf("  OA10 Insert, PopBack, Clear, Free and Concat 0x30002 work on whole elements\n");
}

/* G1: operands this change must not affect. Every step prints one line: the
 * slots the call left, the page (a_type, array.struct_type, slot count and
 * values) and the references of the named objects. The lines are compared
 * with this table, recorded on 57e98af before the change. It pins what
 * xsystem4 does, not the original: Alloc on a wrap array, for one, keeps
 * the old element and constructs an object ("[A obj]"). The primitive
 * Alloc line was corrected to zero on 2026-10-10 (native 0x67f4a0,
 * 0x6569a9); the other entries still pin the earlier behavior. */
static const char *const oa_g1_want[] = {
	"int PushBack#0 left=0 type=14 meta=-1 n=1 [5]",
	"int Add#0 left=0 type=14 meta=-1 n=2 [5 7]",
	"int Insert#0 left=0 type=14 meta=-1 n=3 [9 5 7]",
	"int Count#0 left=1 3 type=14 meta=-1 n=3 [9 5 7]",
	"int At#1 left=2 self 1 type=14 meta=-1 n=3 [9 5 7]",
	"int First#1 left=2 self 0 type=14 meta=-1 n=3 [9 5 7]",
	"int Last#1 left=2 self 2 type=14 meta=-1 n=3 [9 5 7]",
	"int Erase#0 left=1 1 type=14 meta=-1 n=2 [9 7]",
	"int PopBack#0 left=0 type=14 meta=-1 n=1 [9]",
	"int Realloc#0 left=0 type=14 meta=-1 n=3 [9 0 0]",
	"int Alloc#0 left=0 type=14 meta=-1 n=2 [0 0]",
	"int Free#0 left=0 type=14 meta=-1 n=0 []",
	"string PushBack#0 left=0 type=16 meta=-1 n=1 [A] A:2 B:1",
	"string Add#0 left=0 type=16 meta=-1 n=2 [A B] A:2 B:2",
	"string Count#0 left=1 2 type=16 meta=-1 n=2 [A B] A:2 B:2",
	"string At#1 left=1 A type=16 meta=-1 n=2 [A B] A:3 B:2",
	"string Erase#0 left=1 1 type=16 meta=-1 n=1 [B] A:1 B:2",
	"string PopBack#0 left=0 type=16 meta=-1 n=0 [] A:1 B:1",
	"wrap PushBack#0 left=0 type=79 meta=1 n=1 [A] A:2 B:1 C:1",
	"wrap Add#0 left=0 type=79 meta=1 n=2 [A B] A:2 B:2 C:1",
	"wrap Count#0 left=1 2 type=79 meta=1 n=2 [A B] A:2 B:2 C:1",
	"wrap At#1 left=1 B type=79 meta=1 n=2 [A B] A:2 B:3 C:1",
	"wrap At#1 left=1 -1 type=79 meta=1 n=2 [A B] A:2 B:2 C:1",
	"wrap First#1 left=1 A type=79 meta=1 n=2 [A B] A:3 B:2 C:1",
	"wrap Last#1 left=1 B type=79 meta=1 n=2 [A B] A:2 B:3 C:1",
	"wrap Insert#0 left=0 type=79 meta=1 n=3 [C A B] A:2 B:2 C:2",
	"wrap Erase#0 left=1 1 type=79 meta=1 n=2 [A B] A:2 B:2 C:1",
	"wrap PopBack#0 left=0 type=79 meta=1 n=1 [A] A:2 B:1 C:1",
	"wrap Alloc#0 left=0 type=79 meta=1 n=2 [A obj] A:2 B:1 C:1",
	"wrap Free#0 left=0 type=79 meta=1 n=0 [] A:1 B:1 C:1",
	"iface PushBack#0 left=0 type=79 meta=2 n=2 [A 1] A:2 B:1 C:1",
	"iface Add#0 left=0 type=79 meta=2 n=4 [A 1 B 0] A:2 B:2 C:1",
	"iface Count#0 left=1 2 type=79 meta=2 n=4 [A 1 B 0] A:2 B:2 C:1",
	"iface At#1 left=2 B 0 type=79 meta=2 n=4 [A 1 B 0] A:2 B:3 C:1",
	"iface At#1 left=2 -1 0 type=79 meta=2 n=4 [A 1 B 0] A:2 B:2 C:1",
	"iface First#1 left=2 A 1 type=79 meta=2 n=4 [A 1 B 0] A:3 B:2 C:1",
	"iface Last#1 left=2 B 0 type=79 meta=2 n=4 [A 1 B 0] A:2 B:3 C:1",
	"iface Insert#0 left=0 type=79 meta=2 n=6 [A 1 C 0 B 0] A:2 B:2 C:2",
	"iface Erase#0 left=1 1 type=79 meta=2 n=4 [C 0 B 0] A:1 B:2 C:2",
	"iface PopBack#0 left=0 type=79 meta=2 n=2 [C 0] A:1 B:1 C:2",
	"iface Free#0 left=0 type=79 meta=2 n=0 [] A:1 B:1 C:1",
};
static int oa_named[8], oa_nnamed, oa_g1_i;

#define OA_APP(...) do { if (len < size) len += snprintf(buf + len, size - len, __VA_ARGS__); } while (0)

static int oa_format_values(char *buf, int size, int slot)
{
	struct page *p = oa_page(slot);
	int len = 0;
	if (!p) {
		OA_APP(" (no page)");
		return len;
	}
	OA_APP(" type=%d meta=%d n=%d [", p->a_type, p->array.struct_type, p->nr_vars);
	for (int i = 0; i < p->nr_vars && i < 12; i++) {
		int v = p->values[i].i, k = -1;
		for (int j = 0; j < oa_nnamed; j++)
			if (oa_named[j] == v)
				k = j;
		if (k >= 0)
			OA_APP("%s%c", i ? " " : "", 'A' + k);
		else if (v > 1 && da_live_page(v))
			OA_APP("%sobj", i ? " " : "");
		else if (v > 1 && heap_index_valid(v) && heap[v].type == VM_STRING)
			OA_APP("%sstr", i ? " " : "");
		else
			OA_APP("%s%d", i ? " " : "", v);
	}
	OA_APP("]");
	return len;
}

static void oa_print_values(int slot)
{
	char buf[320];
	oa_format_values(buf, sizeof(buf), slot);
	fputs(buf, stdout);
}

static void oa_g1_step(const char *tag, const char *name, int ordinal, int arg3, int self, int nslots, const int *slots)
{
	int out[3] = { 0, 0, 0 };
	int n = oa_hll(name, ordinal, arg3, nslots, slots, out, 3);
	/* At/First/Last return an element or a position: name its first slot */
	bool element = !strcmp(name, "At") || !strcmp(name, "First") || !strcmp(name, "Last");
	char buf[320];
	int size = sizeof(buf), len = 0;
	OA_APP("%s %s#%d left=%d", tag, name, ordinal, n);
	for (int i = 0; i < n && i < 3; i++) {
		int k = -1;
		for (int j = 0; element && i == 0 && j < oa_nnamed; j++)
			if (oa_named[j] == out[i])
				k = j;
		if (element && i == 0 && out[i] == self)
			OA_APP(" self");
		else if (k >= 0)
			OA_APP(" %c", 'A' + k);
		else
			OA_APP(" %d", out[i]);
	}
	if (len < size)
		len += oa_format_values(buf + len, size - len, self);
	for (int j = 0; j < oa_nnamed; j++)
		OA_APP(" %c:%d", 'A' + j, HEAP_REF(oa_named[j]));
	printf("G1 %s\n", buf);
	int nwant = sizeof(oa_g1_want) / sizeof(oa_g1_want[0]);
	OA_CHECK(oa_g1_i < nwant && !strcmp(buf, oa_g1_want[oa_g1_i]), "G1 step %d differs; want \"%s\"",
		oa_g1_i + 1, oa_g1_i < nwant ? oa_g1_want[oa_g1_i] : "(no more steps)");
	oa_g1_i++;
	/* with these operands the caller owns what At/First/Last return: the
	 * element, or for a plain value the array of its (array, index) */
	if (element && n >= 1 && out[0] > 1 && heap_index_valid(out[0]) && HEAP_REF(out[0]) > 1)
		heap_unref(out[0]);
}

static int oa_plain(enum ain_data_type type, int meta)
{
	struct page *p = alloc_page(ARRAY_PAGE, type, 0);
	p->array.struct_type = meta;
	p->array.rank = 1;
	return heap_alloc_page(p);
}

static void oa_guard_operands(void)
{
	/* 1: ints */
	int arr = oa_plain(AIN_ARRAY_INT, -1);
	oa_nnamed = 0;
	int p5[] = { arr, 5 }, p7[] = { arr, 7 }, ins[] = { arr, 0, 9 }, at1[] = { arr, 1 }, er[] = { arr, 1, 1 }, re[] = { arr, 3 }, al[] = { arr, 2 };
	oa_g1_step("int", "PushBack", 0, 1, arr, 2, p5);
	oa_g1_step("int", "Add", 0, 1, arr, 2, p7);
	oa_g1_step("int", "Insert", 0, 1, arr, 3, ins);
	oa_g1_step("int", "Count", 0, 1, arr, 1, &arr);
	oa_g1_step("int", "At", 1, 1, arr, 2, at1);
	oa_g1_step("int", "First", 1, 1, arr, 1, &arr);
	oa_g1_step("int", "Last", 1, 1, arr, 1, &arr);
	oa_g1_step("int", "Erase", 0, 1, arr, 3, er);
	oa_g1_step("int", "PopBack", 0, 1, arr, 1, &arr);
	oa_g1_step("int", "Realloc", 0, 1, arr, 2, re);
	oa_g1_step("int", "Alloc", 0, 1, arr, 2, al);
	oa_g1_step("int", "Free", 0, 1, arr, 1, &arr);
	OA_CHECK(oa_page(arr) && oa_page(arr)->nr_vars == 0, "int array is not empty at the end");
	/* 2: strings */
	int sarr = oa_plain(AIN_ARRAY_STRING, -1);
	oa_named[0] = sp_str("one");
	oa_named[1] = sp_str("two");
	oa_nnamed = 2;
	int sa[] = { sarr, oa_named[0] }, sb[] = { sarr, oa_named[1] }, sat[] = { sarr, 0 }, ser[] = { sarr, 0, 1 };
	oa_g1_step("string", "PushBack", 0, 2, sarr, 2, sa);
	oa_g1_step("string", "Add", 0, 2, sarr, 2, sb);
	oa_g1_step("string", "Count", 0, 2, sarr, 1, &sarr);
	oa_g1_step("string", "At", 1, 2, sarr, 2, sat);
	oa_g1_step("string", "Erase", 0, 2, sarr, 3, ser);
	oa_g1_step("string", "PopBack", 0, 2, sarr, 1, &sarr);
	/* 0x10002: wrap<struct> on the generic page X_A_INIT 0 makes */
	int warr = oa_new_array();
	oa_named[0] = oa_obj();
	oa_named[1] = oa_obj();
	oa_named[2] = oa_obj();
	oa_nnamed = 3;
	int wa[] = { warr, oa_named[0] }, wb[] = { warr, oa_named[1] }, wat[] = { warr, 1 }, wat9[] = { warr, 9 };
	int win[] = { warr, 0, oa_named[2] }, wer[] = { warr, 0, 1 }, wal[] = { warr, 2 };
	oa_g1_step("wrap", "PushBack", 0, 0x10002, warr, 2, wa);
	oa_g1_step("wrap", "Add", 0, 0x10002, warr, 2, wb);
	oa_g1_step("wrap", "Count", 0, 0x10002, warr, 1, &warr);
	oa_g1_step("wrap", "At", 1, 0x10002, warr, 2, wat);
	oa_g1_step("wrap", "At", 1, 0x10002, warr, 2, wat9);
	oa_g1_step("wrap", "First", 1, 0x10002, warr, 1, &warr);
	oa_g1_step("wrap", "Last", 1, 0x10002, warr, 1, &warr);
	oa_g1_step("wrap", "Insert", 0, 0x10002, warr, 3, win);
	oa_g1_step("wrap", "Erase", 0, 0x10002, warr, 3, wer);
	oa_g1_step("wrap", "PopBack", 0, 0x10002, warr, 1, &warr);
	OA_IS(warr, "wrap array is not [A] before Alloc", oa_named[0]);
	OA_CHECK(HEAP_REF(oa_named[0]) == 2 && HEAP_REF(oa_named[1]) == 1 && HEAP_REF(oa_named[2]) == 1, "wrap references are not 2/1/1");
	oa_g1_step("wrap", "Alloc", 0, 0x10002, warr, 2, wal);
	oa_g1_step("wrap", "Free", 0, 0x10002, warr, 1, &warr);
	/* 0x10003: interfaces, (object, vtable offset) */
	int iarr = oa_new_array();
	int ia[] = { iarr, oa_named[0], 1 }, ib[] = { iarr, oa_named[1], 0 }, iat[] = { iarr, 1 }, iat9[] = { iarr, 9 };
	int iin[] = { iarr, 1, oa_named[2], 0 }, ier[] = { iarr, 0, 1 };
	oa_g1_step("iface", "PushBack", 0, 0x10003, iarr, 3, ia);
	oa_g1_step("iface", "Add", 0, 0x10003, iarr, 3, ib);
	oa_g1_step("iface", "Count", 0, 0x10003, iarr, 1, &iarr);
	oa_g1_step("iface", "At", 1, 0x10003, iarr, 2, iat);
	oa_g1_step("iface", "At", 1, 0x10003, iarr, 2, iat9);
	oa_g1_step("iface", "First", 1, 0x10003, iarr, 1, &iarr);
	oa_g1_step("iface", "Last", 1, 0x10003, iarr, 1, &iarr);
	oa_g1_step("iface", "Insert", 0, 0x10003, iarr, 4, iin);
	OA_IS(iarr, "interface array is not (A,1) (C,0) (B,0)", oa_named[0], 1, oa_named[2], 0, oa_named[1], 0);
	oa_g1_step("iface", "Erase", 0, 0x10003, iarr, 3, ier);
	oa_g1_step("iface", "PopBack", 0, 0x10003, iarr, 1, &iarr);
	OA_IS(iarr, "interface array is not (C,0) at the end", oa_named[2], 0);
	oa_g1_step("iface", "Free", 0, 0x10003, iarr, 1, &iarr);
	OA_CHECK(HEAP_REF(oa_named[0]) == 1 && HEAP_REF(oa_named[1]) == 1 && HEAP_REF(oa_named[2]) == 1, "interface references are not back to 1");
	OA_CHECK(oa_g1_i == (int)(sizeof(oa_g1_want) / sizeof(oa_g1_want[0])), "G1 ran %d step(s), the table has %zu",
		oa_g1_i, sizeof(oa_g1_want) / sizeof(oa_g1_want[0]));
	printf("  G1 operands 1, 2, 0x10002 and 0x10003: %d steps match the compatibility table\n", oa_g1_i);
}

static void oa_guard_legacy(void)
{
	int a = oa_obj();
	/* -1 (pre-v11 and non-generic calls) and 0 (every other library) have
	 * bit 17 set or clear by accident; neither is a packed type. */
	const int plain[] = { -1, 0 };
	for (int k = 0; k < 2; k++) {
		int arr = oa_plain(AIN_ARRAY_INT, -1);
		int s[] = { arr, 5 };
		int n = oa_hll("PushBack", 0, plain[k], 2, s, NULL, 0);
		printf("G2 operand %d PushBack left=%d", plain[k], n);
		oa_print_values(arr);
		printf("\n");
		OA_CHECK(n == 0, "operand %d: PushBack left %d slot(s), want 0 (one-slot element)", plain[k], n);
		OA_IS(arr, "operand -1/0: the int was not stored alone", 5);
		int al[] = { arr, 2 };
		n = oa_hll("Alloc", 0, plain[k], 2, al, NULL, 0);
		printf("G2 operand %d Alloc left=%d", plain[k], n);
		oa_print_values(arr);
		printf("\n");
		OA_CHECK(n == 0 && oa_page(arr) && oa_page(arr)->nr_vars == 2, "operand %d: Alloc(2) is not two slots", plain[k]);
	}
	/* An older AIN never has option elements: the low word decides alone. */
	int version = ain->version;
	ain->version = 13;
	int arr = oa_plain(AIN_ARRAY_INT, -1);
	int s[] = { arr, a, 0 };
	int n = oa_hll("PushBack", 0, OA_REF, 3, s, NULL, 0);
	printf("G2 v13 PushBack 0x30002 left=%d\n", n);
	OA_CHECK(n == 1, "v13: PushBack 0x30002 left %d slot(s), want 1 (the low word says one slot)", n);
	int arr2 = oa_plain(AIN_ARRAY_INT, -1);
	int al[] = { arr2, 2 };
	n = oa_hll("Alloc", 0, 0x30001, 2, al, NULL, 0);
	struct page *p = oa_page(arr2);
	printf("G2 v13 Alloc 0x30001 left=%d slots=%d\n", n, p ? p->nr_vars : -1);
	OA_CHECK(n == 0 && p && p->nr_vars == 2, "v13: Alloc 0x30001 (2) made %d slot(s), want 2", p ? p->nr_vars : -1);
	ain->version = version;
	printf("  G2 operands -1 and 0 and an AIN before version 14 do not read the option bit\n");
}

static void oa_guard_three(void)
{
	uint32_t set = oa_find_any(X_OP_SET, 0x10003);
	int arr = oa_new_array(), a = oa_obj(), b = oa_obj();
	size_t before = live_slots();
	int s[] = { arr, 2 };
	int n = oa_hll("Alloc", 0, OA_IFACE, 2, s, NULL, 0);
	OA_CHECK(n == 0, "Alloc 0x30003 left %d slot(s)", n);
	oa_layout(arr, 3, 2, "after Alloc 0x30003 (2)");
	OA_IS(arr, "none is not (-1, -1, 1)", -1, -1, 1, -1, -1, 1);
	OA_CHECK(live_slots() == before, "Alloc 0x30003 created %zd heap object(s)", (ssize_t)(live_slots() - before));
	OA_CHECK(oa_hll1("Count", 0, OA_IFACE, arr) == 2, "Count 0x30003 is not 2");
	int ref = HEAP_REF(arr);
	if (oa_page(arr) && oa_page(arr)->nr_vars == 6) {
		/* vtable offsets 0 and 1 only. Alloc 0x30003 gives the page its
		 * layout, so X_OP_SET and the teardown count the first slot of
		 * an element alone, as the HLL side does (before the
		 * array-literal change the VM counted every slot above 1 of a
		 * generic page); offsets above 1 are array-literal AL4 and AL8. */
		int v[] = { a, 1, 0 };
		oa_op_set(set, arr, 3, 3, v);
		OA_IS(arr, "X_OP_SET 65539 did not write element 1", -1, -1, 1, a, 1, 0);
		/* The vtable offset and the flag differ in both, so a swap of the
		 * two slots or a flag read from the wrong one shows: a value at
		 * offset 1, and none whose offset slot happens to be 0. */
		int pb[] = { arr, b, 1, 0 }, pn[] = { arr, b, 0, 1 };
		n = oa_hll("PushBack", 0, OA_IFACE, 4, pb, NULL, 0);
		OA_CHECK(n == 0 && HEAP_REF(b) == 2, "PushBack 0x30003 left %d slot(s), ref %d", n, HEAP_REF(b));
		n = oa_hll("PushBack", 0, OA_IFACE, 4, pn, NULL, 0);
		OA_CHECK(n == 0 && HEAP_REF(b) == 2, "PushBack 0x30003 of none left %d slot(s), ref %d", n, HEAP_REF(b));
		oa_layout(arr, 3, 4, "after two PushBack 0x30003");
		OA_IS(arr, "three-slot elements are not none (a,1,0) (b,1,0) none", -1, -1, 1, a, 1, 0, b, 1, 0, -1, -1, 1);
		int at[] = { arr, 2 }, out[3] = { -999, -999, -999 };
		n = oa_hll("At", 1, OA_IFACE, 2, at, out, 3);
		OA_CHECK(n == 2 && out[0] == arr && out[1] == 6 && HEAP_REF(arr) == ref,
			"At(2) 0x30003 returned %d slot(s) [%d, %d], array ref %d -> %d; want [array, 6], uncounted",
			n, out[0], out[1], ref, HEAP_REF(arr));
		int er[] = { arr, 1, 1 };
		n = oa_hll("Erase", 0, OA_IFACE, 3, er, out, 1);
		OA_CHECK(n == 1 && out[0] == 1 && HEAP_REF(a) == 1, "Erase 0x30003: %d, ref %d", out[0], HEAP_REF(a));
		OA_IS(arr, "Erase(1, 1) 0x30003 is not none (b,1,0) none", -1, -1, 1, b, 1, 0, -1, -1, 1);
		int in0[] = { arr, 0, a, 0, 0 }, in2[] = { arr, 2, a, 1, 1 };
		n = oa_hll("Insert", 0, OA_IFACE, 5, in0, NULL, 0);
		OA_CHECK(n == 0 && HEAP_REF(a) == 2, "Insert 0x30003 left %d slot(s), ref %d", n, HEAP_REF(a));
		n = oa_hll("Insert", 0, OA_IFACE, 5, in2, NULL, 0);
		OA_CHECK(n == 0 && HEAP_REF(a) == 2, "Insert 0x30003 of none left %d slot(s), ref %d", n, HEAP_REF(a));
		OA_IS(arr, "after two Insert 0x30003: want (a,0,0) none none (b,1,0) none",
			a, 0, 0, -1, -1, 1, -1, -1, 1, b, 1, 0, -1, -1, 1);
		int re[] = { arr, 2 };
		n = oa_hll("Realloc", 0, OA_IFACE, 2, re, NULL, 0);
		OA_CHECK(n == 0 && HEAP_REF(b) == 1, "Realloc 0x30003 left %d slot(s), removed ref %d", n, HEAP_REF(b));
		OA_IS(arr, "Realloc(2) 0x30003 is not (a,0,0) none", a, 0, 0, -1, -1, 1);
		int re3[] = { arr, 3 };
		oa_hll("Realloc", 0, OA_IFACE, 2, re3, NULL, 0);
		OA_IS(arr, "Realloc(3) 0x30003 did not add (-1, -1, 1)", a, 0, 0, -1, -1, 1, -1, -1, 1);
	}
	heap_unref(arr);
	OA_CHECK(!da_live_page(arr) && HEAP_REF(a) == 1 && HEAP_REF(b) == 1, "three-slot teardown left refs %d/%d", HEAP_REF(a), HEAP_REF(b));
	printf("  G3 0x30003 elements are three slots and none is (-1, -1, 1)\n");
}

static void oa_guard_unimplemented(void)
{
	int lib = bu_lib("Array"), f = oa_fn("PushBack", 0);
	void *saved = libraries[lib][f].fun;
	libraries[lib][f].fun = NULL;
	const struct { int arg3, slots; } c[] = { { 1, 2 }, { 2, 2 }, { 0x10002, 2 }, { 0x10003, 3 }, { OA_STR, 3 }, { OA_REF, 3 }, { OA_IFACE, 4 } };
	for (unsigned i = 0; i < sizeof(c) / sizeof(c[0]); i++) {
		int s[4] = { 11, 12, 13, 14 };
		int n = oa_hll("PushBack", 0, c[i].arg3, c[i].slots, s, NULL, 0);
		OA_CHECK(n == 0, "an unimplemented PushBack %#x left %d of its %d slot(s)", c[i].arg3, n, c[i].slots);
	}
	libraries[lib][f].fun = saved;
	/* hll_call's release loop also steps over the element's slots, to reach
	 * the arguments after it. No declaration has one: an hll_param is always
	 * the last argument, so that step cannot be observed through this AIN. */
	int params = 0, followed = 0;
	for (int l = 0; l < ain->nr_libraries; l++)
		for (int j = 0; j < ain->libraries[l].nr_functions; j++) {
			struct ain_hll_function *h = &ain->libraries[l].functions[j];
			for (int i = 0; i < h->nr_arguments; i++)
				if (h->arguments[i].type.data == AIN_HLL_PARAM) {
					params++;
					followed += i != h->nr_arguments - 1;
				}
		}
	OA_CHECK(params > 0 && followed == 0, "%d of %d hll_param arguments are followed by another argument", followed, params);
	printf("  G4 a declaration without an implementation discards the element's slots; %d hll_param arguments, all last\n", params);
}

static void oa_guard_copy(void)
{
	int a = oa_obj();
	int vals[] = { a, 0, -1, 1 };
	int src = oa_make(2, 2, vals);
	stack_push(src);
	execute_instruction(A_REF);
	int copy = stack_pop().i;
	OA_CHECK(copy != src && oa_page(copy), "A_REF did not make a new array");
	oa_layout(copy, 2, 2, "A_REF copy");
	OA_IS(copy, "the copy is not (a, 0) (-1, 1)", a, 0, -1, 1);
	OA_CHECK(HEAP_REF(a) == 3, "the shared value has %d reference(s), want 3", HEAP_REF(a));
	heap_unref(src);
	OA_CHECK(HEAP_REF(a) == 2 && oa_page(copy), "releasing the source left ref %d", HEAP_REF(a));
	heap_unref(copy);
	OA_CHECK(HEAP_REF(a) == 1, "releasing the copy left ref %d", HEAP_REF(a));
	printf("  G5 A_REF of an option array shares its values\n");
}

static int test_option_array(void)
{
	static const struct { const char *name; void (*run)(void); } cases[] = {
		{ "OA1", oa_push_value }, { "OA2", oa_push_none }, { "OA3", oa_select }, { "OA4", oa_where },
		{ "OA5", oa_add_string }, { "OA6", oa_alloc }, { "OA7", oa_load_nodes }, { "OA8", oa_positions },
		{ "OA9", oa_realloc }, { "OA10", oa_insert }, { "OA11", oa_get_node }, { "OA12", oa_resize_order },
		{ "G1", oa_guard_operands }, { "G2", oa_guard_legacy },
		{ "G3", oa_guard_three }, { "G4", oa_guard_unimplemented }, { "G5", oa_guard_copy },
	};
	int failed = 0, environment = 0;
	for (unsigned i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
		fflush(stdout);
		fflush(stderr);
		pid_t pid = fork();
		if (pid == 0) {
			alarm(60);
			oa_failures = da_failures = wc_failures = 0;
			cases[i].run();
			fflush(stdout);
			_exit(oa_failures ? 1 : 0);
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
			printf("%s ENVIRONMENT (the AIN lacks a declaration or bytecode shape this case needs)\n", cases[i].name);
			environment++;
		} else {
			printf("%s FAIL (exit %d)\n", cases[i].name, WEXITSTATUS(st));
			failed++;
		}
	}
	printf("OPTION-ARRAY %s: %d of %zu cases failed, %d environment error(s)\n",
		failed || environment ? "FAIL" : "PASS", failed, sizeof(cases) / sizeof(cases[0]), environment);
	return environment ? 2 : failed ? 1 : 0;
}
