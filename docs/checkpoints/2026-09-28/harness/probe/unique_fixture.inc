/* Any / Unique / UniqueSorted / Equals with real AIN data and lambdas
 * (2026-09-28). Requires first_overload_fixture.inc and order_fixture.inc. */
static int uq_fail;
static void uq_expect(const char *what, const char *got, const char *want)
{
	int ok = !strcmp(got, want);
	printf("UNIQUE %-44s got=%-12s want=%-12s %s\n", what, got, want, ok ? "ok" : "FAIL");
	if (!ok) uq_fail++;
}

static const char *uq_dump(int slot, bool strings, bool frame)
{
	static char buf[256];
	struct page *a = heap_index_valid(slot) ? heap[slot].page : NULL;
	int w = snprintf(buf, sizeof buf, "[");
	for (int i = 0; a && i < a->nr_vars && w < 200; i++) {
		int v = a->values[i].i;
		if (strings) w += snprintf(buf + w, sizeof buf - w, "%s%s", i ? "," : "", fo_slot_text(v));
		else if (frame) w += snprintf(buf + w, sizeof buf - w, "%s%d", i ? "," : "", heap[v].page->values[0].i);
		else w += snprintf(buf + w, sizeof buf - w, "%s%d", i ? "," : "", v);
	}
	snprintf(buf + w, sizeof buf - w, "]");
	return buf;
}

static int uq_call(int lib, int fno, int arg3, int self, int src, int func)
{
	const struct ain_hll_function *f = &ain->libraries[lib].functions[fno];
	int sp = stack_ptr;
	stack_push(self);
	if (src >= 0) stack_push(src);
	if (func >= 0) { stack_push(-1); stack_push(func); }
	hll_call(lib, fno, arg3);
	int want = f->return_type.data == AIN_VOID ? 0 : 1;
	assert(stack_ptr - sp == want);
	return want ? stack_pop().i : 0;
}

static int test_unique(void)
{
	int lib = fo_find_lib("Array");
	int any0 = fo_find_fn(lib, "Any", 1, 0, 0), any1 = fo_find_fn(lib, "Any", 2, 1, 0);
	int ex1 = fo_find_fn(lib, "IsExist", 2, 1, 0);
	int un0 = fo_find_fn(lib, "Unique", 1, 0, 0), un1 = fo_find_fn(lib, "Unique", 2, 1, 0);
	int us0 = fo_find_fn(lib, "UniqueSorted", 1, 0, 0), us1 = fo_find_fn(lib, "UniqueSorted", 2, 1, 0);
	int eq0 = fo_find_fn(lib, "Equals", 2, 0, 0), eq1 = fo_find_fn(lib, "Equals", 3, 1, 0);
	size_t live0 = live_slots();
	char g[32];
	#define B(x) (snprintf(g, sizeof g, "%d", (x)), g)

	int sa = fo_make_string_array((const char *[]){"a", ""}, 2);
	int sb = fo_make_string_array((const char *[]){"a", "b"}, 2);
	int se = fo_make_string_array((const char *[]){0}, 0);
	uq_expect("Any#1 [a,\"\"] obj==\"\"", B(uq_call(lib, any1, 2, sa, -1, 36080)), "1");
	uq_expect("Any#1 [a,b] obj==\"\"", B(uq_call(lib, any1, 2, sb, -1, 36080)), "0");
	uq_expect("Any#1 [] obj==\"\"", B(uq_call(lib, any1, 2, se, -1, 36080)), "0");
	uq_expect("IsExist#1 [a,b] same as Any#1", B(uq_call(lib, ex1, 2, sb, -1, 36080)), "0");
	uq_expect("Any#0 [a,b]", B(uq_call(lib, any0, 2, sb, -1, -1)), "1");
	uq_expect("Any#0 []", B(uq_call(lib, any0, 2, se, -1, -1)), "0");

	int i1 = ob_int_array((int[]){3, 1, 3, 2, 1}, 5);
	uq_call(lib, un0, 1, i1, -1, -1);
	uq_expect("Unique#0 int [3,1,3,2,1]", uq_dump(i1, false, false), "[3,1,2]");
	int i2 = ob_int_array((int[]){1, 1, 2, 1}, 4);
	uq_call(lib, us0, 1, i2, -1, -1);
	uq_expect("UniqueSorted#0 int [1,1,2,1]", uq_dump(i2, false, false), "[1,2,1]");
	int s1 = fo_make_string_array((const char *[]){"b", "a", "b", "a"}, 4);
	uq_call(lib, un0, 2, s1, -1, -1);
	uq_expect("Unique#0 string [b,a,b,a]", uq_dump(s1, true, false), "[b,a]");
	int s2 = fo_make_string_array((const char *[]){"a", "a", "b"}, 3);
	uq_call(lib, us0, 2, s2, -1, -1);
	uq_expect("UniqueSorted#0 string [a,a,b]", uq_dump(s2, true, false), "[a,b]");

	int st = or_struct("modelviewer::detail::SMotionAlphaParam");
	printf("UNIQUE SMotionAlphaParam struct=%d lambda 24536 nr_args=%d ret=%d\n", st,
	       ain->functions[24536].nr_args, ain->functions[24536].return_type.data);
	int p1 = -1, p2 = -1, p3 = -1, e1 = -1, e2 = -1, e3 = -1;
	if (st >= 0) {
		p1 = or_struct_array(st, (const int[][2]){{1, 0}, {3, 0}}, 2);
		uq_call(lib, un1, 2, p1, -1, 24536);
		uq_expect("Unique#1 [1,3] a<b drops later", uq_dump(p1, false, true), "[1]");
		p2 = or_struct_array(st, (const int[][2]){{2, 0}, {5, 0}, {1, 0}, {3, 0}}, 4);
		uq_call(lib, un1, 2, p2, -1, 24536);
		uq_expect("Unique#1 [2,5,1,3]", uq_dump(p2, false, true), "[2,1]");
		p3 = or_struct_array(st, (const int[][2]){{2, 0}, {5, 0}, {1, 0}, {3, 0}}, 4);
		uq_call(lib, us1, 2, p3, -1, 24536);
		uq_expect("UniqueSorted#1 [2,5,1,3]", uq_dump(p3, false, true), "[2,1]");
		e1 = or_struct_array(st, (const int[][2]){{1, 0}, {2, 0}}, 2);
		e2 = or_struct_array(st, (const int[][2]){{2, 0}, {3, 0}}, 2);
		e3 = or_struct_array(st, (const int[][2]){{2, 0}, {2, 0}}, 2);
		uq_expect("Equals#1 [1,2] vs [2,3] a<b", B(uq_call(lib, eq1, 2, e1, e2, 24536)), "1");
		uq_expect("Equals#1 [1,2] vs [2,2]", B(uq_call(lib, eq1, 2, e1, e3, 24536)), "0");
	}
	int a = ob_int_array((int[]){1, 2, 3}, 3), b = ob_int_array((int[]){1, 2, 3}, 3);
	int c = ob_int_array((int[]){1, 2, 4}, 3), d = ob_int_array((int[]){1, 2}, 2);
	int z1 = ob_int_array((int[]){0}, 0), z2 = ob_int_array((int[]){0}, 0);
	uq_expect("Equals#0 [1,2,3] vs [1,2,3]", B(uq_call(lib, eq0, 1, a, b, -1)), "1");
	uq_expect("Equals#0 [1,2,3] vs [1,2,4]", B(uq_call(lib, eq0, 1, a, c, -1)), "0");
	uq_expect("Equals#0 [1,2,3] vs [1,2]", B(uq_call(lib, eq0, 1, a, d, -1)), "0");
	uq_expect("Equals#0 [] vs []", B(uq_call(lib, eq0, 1, z1, z2, -1)), "1");
	int t1 = fo_make_string_array((const char *[]){"x", "y"}, 2), t2 = fo_make_string_array((const char *[]){"x", "y"}, 2);
	uq_expect("Equals#0 string by content", B(uq_call(lib, eq0, 2, t1, t2, -1)), "1");
	#undef B
	int all[] = {sa, sb, se, i1, i2, s1, s2, p1, p2, p3, e1, e2, e3, a, b, c, d, z1, z2, t1, t2};
	for (unsigned k = 0; k < sizeof all / sizeof all[0]; k++)
		if (all[k] > 0) heap_unref(all[k]);
	char l0[16], l1[16];
	snprintf(l0, sizeof l0, "%zu", live0); snprintf(l1, sizeof l1, "%zu", live_slots());
	uq_expect("heap back to baseline", l1, l0);
	printf("UNIQUE summary: %d failure(s)\n", uq_fail);
	return uq_fail ? 88 : 0;
}
