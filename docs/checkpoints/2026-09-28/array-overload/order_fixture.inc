/* Min / Max / Last(pred) with real AIN data and lambdas (2026-09-28).
 * Requires first_overload_fixture.inc. Checks stack growth, which element is
 * returned (identity, not value) and that the heap returns to its baseline. */
static int or_fail;
static void or_expect(const char *what, int got, int want)
{
	printf("ORDER %-46s got=%3d want=%3d %s\n", what, got, want, got == want ? "ok" : "FAIL");
	if (got != want) or_fail++;
}

static int or_struct(const char *name)
{
	for (int i = 0; i < ain->nr_structures; i++)
		if (ain->structures[i].name && !strcmp(ain->structures[i].name, name))
			return i;
	return -1;
}

static int or_struct_array(int st, const int (*m)[2], int n)
{
	struct page *a = alloc_page(ARRAY_PAGE, AIN_ARRAY_STRUCT, n);
	a->array.rank = 1;
	a->array.struct_type = st;
	for (int k = 0; k < n; k++) {
		int o = alloc_struct(st);
		heap[o].page->values[0].i = m[k][0];
		if (heap[o].page->nr_vars > 1)
			heap[o].page->values[1].i = m[k][1];
		a->values[k].i = o;
	}
	return heap_alloc_page(a);
}

static int or_float_array(const float *v, int n)
{
	struct page *a = alloc_page(ARRAY_PAGE, AIN_ARRAY_FLOAT, n);
	a->array.rank = 1;
	for (int k = 0; k < n; k++)
		a->values[k].f = v[k];
	return heap_alloc_page(a);
}

/* Returns the logical index of the returned element, -1 for null. */
static int or_call(int lib, int fno, int arr, int func, int arg3)
{
	int sp = stack_ptr;
	stack_push(arr);
	if (func >= 0) { stack_push(-1); stack_push(func); }
	hll_call(lib, fno, arg3);
	bool value = !(arg3 >= 0x10000 || arg3 == 2);
	int want = value ? 2 : 1;
	if (stack_ptr - sp != want) {
		printf("ORDER stack grew by %d, want %d\n", stack_ptr - sp, want);
		or_fail++;
		stack_ptr = sp;
		return -99;
	}
	if (value) {
		int idx = stack_pop().i, owner = stack_pop().i;
		if (owner == -1) return -1;
		if (owner != arr) { printf("ORDER owner %d != array %d\n", owner, arr); or_fail++; }
		heap_unref(owner);  /* Array_At retains the owner for the caller */
		return idx;
	}
	int slot = stack_pop().i;
	if (slot <= 0) return -1;
	struct page *a = heap[arr].page;
	int idx = -98;
	for (int k = 0; k < a->nr_vars; k++)
		if (a->values[k].i == slot) idx = k;
	heap_unref(slot);  /* ref elements come back retained */
	return idx;
}

static int test_order(void)
{
	int lib = fo_find_lib("Array");
	int min1 = fo_find_fn(lib, "Min", 1, 0, 1), max1 = fo_find_fn(lib, "Max", 1, 0, 1);
	int min3 = fo_find_fn(lib, "Min", 2, 1, 1), max3 = fo_find_fn(lib, "Max", 2, 1, 1);
	int last3 = fo_find_fn(lib, "Last", 2, 1, 1), last1 = fo_find_fn(lib, "Last", 1, 0, 1);
	printf("ORDER decls Min#1=%d Max#1=%d Min#3=%d Max#3=%d Last#1=%d Last#3=%d\n",
	       min1, max1, min3, max3, last1, last3);
	size_t live0 = live_slots();

	int a = ob_int_array((int[]){3, 9, 1, 9, 1}, 5);
	or_expect("Max#1 int [3,9,1,9,1] last max", or_call(lib, max1, a, -1, 1), 3);
	or_expect("Min#1 int [3,9,1,9,1] first min", or_call(lib, min1, a, -1, 1), 2);
	int e = ob_int_array((int[]){0}, 0);
	or_expect("Max#1 empty", or_call(lib, max1, e, -1, 1), -1);
	or_expect("Min#1 empty", or_call(lib, min1, e, -1, 1), -1);
	int one = ob_int_array((int[]){42}, 1);
	or_expect("Max#1 single", or_call(lib, max1, one, -1, 1), 0);
	int f = or_float_array((float[]){-2.0f, -1.0f, 3.5f, -7.25f}, 4);
	or_expect("Min#1 float [-2,-1,3.5,-7.25]", or_call(lib, min1, f, -1, 1), 3);
	or_expect("Max#1 float [-2,-1,3.5,-7.25]", or_call(lib, max1, f, -1, 1), 2);
	int s = fo_make_string_array((const char *[]){"b", "gamma", "a", "gamma"}, 4);
	or_expect("Max#1 string last max by content", or_call(lib, max1, s, -1, 2), 3);
	or_expect("Min#1 string", or_call(lib, min1, s, -1, 2), 2);

	int pos = or_struct("CASPos");
	printf("ORDER CASPos struct=%d members=%d\n", pos, pos >= 0 ? ain->structures[pos].nr_members : -1);
	for (int fn = 37050; fn <= 37053; fn++) {
		struct ain_function *cb = &ain->functions[fn];
		printf("ORDER lambda %d nr_args=%d ret=%d arg0=%d\n", fn, cb->nr_args,
		       cb->return_type.data, cb->nr_vars ? cb->vars[0].type.data : -1);
	}
	if (pos >= 0) {
		int p = or_struct_array(pos, (const int[][2]){{7, 1}, {2, 9}, {2, 3}, {7, 3}}, 4);
		or_expect("Min#3 CASPos by x (37050)", or_call(lib, min3, p, 37050, 2), 1);
		or_expect("Max#3 CASPos by x (37052) last max", or_call(lib, max3, p, 37052, 2), 3);
		or_expect("Min#3 CASPos by y (37051)", or_call(lib, min3, p, 37051, 2), 0);
		or_expect("Max#3 CASPos by y (37053)", or_call(lib, max3, p, 37053, 2), 1);
		heap_unref(p);
	}
	int la = fo_make_string_array((const char *[]){"a", "", "b", "", "c"}, 5);
	or_expect("Last#3 string obj==\"\" (36080)", or_call(lib, last3, la, 36080, 2), 3);
	int lb = fo_make_string_array((const char *[]){"a", "b"}, 2);
	or_expect("Last#3 no match", or_call(lib, last3, lb, 36080, 2), -1);
	or_expect("Last#1 string unchanged", or_call(lib, last1, la, -1, 2), 4);
	int fd = or_struct("FrameDamage");
	if (fd >= 0 && 36357 < ain->nr_functions) {
		printf("ORDER FrameDamage struct=%d member0 type=%d\n", fd, ain->structures[fd].members[0].type.data);
		int d = or_struct_array(fd, (const int[][2]){{1, 0}, {0, 0}, {1, 0}, {0, 0}}, 4);
		or_expect("Last#3 FrameDamage member0 (36357)", or_call(lib, last3, d, 36357, 2), 2);
		int z = or_struct_array(fd, (const int[][2]){{0, 0}, {0, 0}}, 2);
		or_expect("Last#3 FrameDamage all false", or_call(lib, last3, z, 36357, 2), -1);
		heap_unref(d); heap_unref(z);
	}
	heap_unref(a); heap_unref(e); heap_unref(one); heap_unref(f); heap_unref(s);
	heap_unref(la); heap_unref(lb);
	or_expect("heap back to baseline", (int)live_slots(), (int)live0);
	printf("ORDER summary: %d failure(s)\n", or_fail);
	return or_fail ? 88 : 0;
}
