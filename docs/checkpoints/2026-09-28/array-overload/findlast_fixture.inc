/* FindLast / Find overloads with a real AIN predicate (2026-09-28).
 * fno 36080 is a lambda `obj == ""` over string. Requires first_overload_fixture.inc. */
static int fl_fail;
static void fl_expect(const char *what, int got, int want)
{
	printf("FINDLAST %-34s got=%2d want=%2d %s\n", what, got, want, got == want ? "ok" : "FAIL");
	if (got != want) fl_fail++;
}

static int fl_run(int lib, int fno, int arr, bool range, int begin, int end, bool pred, int key)
{
	int sp = stack_ptr;
	stack_push(arr);
	if (range) { stack_push(begin); stack_push(end); }
	if (pred) { stack_push(-1); stack_push(key); }
	else stack_push(key);
	hll_call(lib, fno, 2);
	int r = stack_pop().i;
	assert(stack_ptr == sp);
	return r;
}

static int fl_str(const char *t)
{
	int s = heap_alloc_slot(VM_STRING);
	heap[s].s = make_string(t, strlen(t));
	return s;
}

static int test_findlast(void)
{
	int lib = fo_find_lib("Array");
	const int lam = 36080;
	assert(lam < ain->nr_functions && ain->functions[lam].nr_args == 1
	       && ain->functions[lam].return_type.data == AIN_BOOL
	       && ain->functions[lam].vars[0].type.data == AIN_STRING);
	int find_f = fo_find_fn(lib, "Find", 2, 1, 0), find_rf = fo_find_fn(lib, "Find", 4, 1, 0);
	int fl_f = fo_find_fn(lib, "FindLast", 2, 1, 0), fl_rf = fo_find_fn(lib, "FindLast", 4, 1, 0);
	int fl_v = fo_find_fn(lib, "FindLast", 2, 0, 0), fl_rv = fo_find_fn(lib, "FindLast", 4, 0, 0);
	printf("FINDLAST decls Find#2=%d Find#3=%d FindLast#0=%d #1=%d #2=%d #3=%d\n",
	       find_f, find_rf, fl_v, fl_rv, fl_f, fl_rf);
	int A = fo_make_string_array((const char *[]){"a", "", "b", "", "c"}, 5);
	fl_expect("FindLast(A, empty)", fl_run(lib, fl_f, A, false, 0, 0, true, lam), 3);
	fl_expect("Find(A, empty)", fl_run(lib, find_f, A, false, 0, 0, true, lam), 1);
	fl_expect("FindLast(A, 0, 3, empty) end incl.", fl_run(lib, fl_rf, A, true, 0, 3, true, lam), 3);
	fl_expect("FindLast(A, 0, 10, empty)", fl_run(lib, fl_rf, A, true, 0, 10, true, lam), 3);
	fl_expect("FindLast(A, 0, 2, empty)", fl_run(lib, fl_rf, A, true, 0, 2, true, lam), 1);
	fl_expect("FindLast(A, -5, 0, empty)", fl_run(lib, fl_rf, A, true, -5, 0, true, lam), -1);
	fl_expect("FindLast(A, 4, 10, empty)", fl_run(lib, fl_rf, A, true, 4, 10, true, lam), -1);
	fl_expect("FindLast(A, 0, -1, empty)", fl_run(lib, fl_rf, A, true, 0, -1, true, lam), -1);
	fl_expect("FindLast(A, 2, 1, empty)", fl_run(lib, fl_rf, A, true, 2, 1, true, lam), -1);
	fl_expect("Find(A, 0, 3, empty) end excl.", fl_run(lib, find_rf, A, true, 0, 3, true, lam), 1);
	fl_expect("Find(A, 1, 1, empty)", fl_run(lib, find_rf, A, true, 1, 1, true, lam), -1);
	int B = fo_make_string_array((const char *[]){"x", "y", "x"}, 3);
	int x = fl_str("x"), z = fl_str("z"), y = fl_str("y");
	fl_expect("FindLast(B, \"x\") by content", fl_run(lib, fl_v, B, false, 0, 0, false, x), 2);
	fl_expect("FindLast(B, \"z\")", fl_run(lib, fl_v, B, false, 0, 0, false, z), -1);
	fl_expect("FindLast(B, 0, 1, \"x\")", fl_run(lib, fl_rv, B, true, 0, 1, false, x), 0);
	fl_expect("FindLast(B, 0, 2, \"x\")", fl_run(lib, fl_rv, B, true, 0, 2, false, x), 2);
	fl_expect("FindLast(B, 1, 1, \"y\")", fl_run(lib, fl_rv, B, true, 1, 1, false, y), 1);
	fl_expect("FindLast(B, 3, 5, \"x\")", fl_run(lib, fl_rv, B, true, 3, 5, false, x), -1);
	struct page *ep = alloc_page(ARRAY_PAGE, AIN_ARRAY_STRING, 0);
	ep->array.rank = 1;
	int E = heap_alloc_page(ep);
	fl_expect("FindLast(E, empty) on empty", fl_run(lib, fl_f, E, false, 0, 0, true, lam), -1);
	fl_expect("FindLast(E, 0, 9, empty) on empty", fl_run(lib, fl_rf, E, true, 0, 9, true, lam), -1);
	fl_expect("FindLast(E, \"x\") on empty", fl_run(lib, fl_v, E, false, 0, 0, false, x), -1);
	heap_unref(x); heap_unref(y); heap_unref(z);
	heap_unref(A); heap_unref(B); heap_unref(E);
	printf("FINDLAST summary: %d failure(s)\n", fl_fail);
	return fl_fail ? 88 : 0;
}
