/* Sort / AscSort / DescSort / QuickSort / Remain through the real ffi path.
 * Adapted from the batch-2 review probe (2026-09-28). */
#include <sys/wait.h>
#include <unistd.h>
static int so_LIB = -1;
static int so_fails = 0;

static int so_find_fn(const char *name, int nargs, bool func)
{
	struct ain_library *l = &ain->libraries[so_LIB];
	for (int i = 0; i < l->nr_functions; i++) {
		struct ain_hll_function *f = &l->functions[i];
		if (strcmp(f->name, name) || f->nr_arguments != nargs)
			continue;
		bool has = false;
		for (int k = 0; k < f->nr_arguments; k++)
			if (f->arguments[k].type.data == AIN_HLL_FUNC || f->arguments[k].type.data == AIN_HLL_FUNC_71)
				has = true;
		if (has == func)
			return i;
	}
	return -1;
}

static int so_int_array(const int *v, int n)
{
	struct page *a = alloc_page(ARRAY_PAGE, AIN_ARRAY_INT, n);
	a->array.rank = 1; a->array.struct_type = -1;
	for (int i = 0; i < n; i++) a->values[i].i = v[i];
	int s = heap_alloc_slot(VM_PAGE); heap_set_page(s, a); return s;
}

static int so_str_slot(const char *t)
{
	int s = heap_alloc_slot(VM_STRING);
	heap[s].s = make_string(t, strlen(t));
	return s;
}

/* generic v14 page: a_type ARRAY_INT even for strings (what PushBack makes) */
static int so_str_array(const char **v, int n, int *slots)
{
	struct page *a = alloc_page(ARRAY_PAGE, AIN_ARRAY_INT, n);
	a->array.rank = 1; a->array.struct_type = -1;
	for (int i = 0; i < n; i++) a->values[i].i = slots[i] = so_str_slot(v[i]);
	int s = heap_alloc_slot(VM_PAGE); heap_set_page(s, a); return s;
}

static int so_pos_array(int st, const int *x, const int *y, int n, int *slots)
{
	struct page *a = alloc_page(ARRAY_PAGE, AIN_ARRAY_INT, n);
	a->array.rank = 1; a->array.struct_type = -1;
	for (int i = 0; i < n; i++) {
		int s = alloc_struct(st);
		heap[s].page->values[0].i = x[i];
		heap[s].page->values[1].i = y[i];
		a->values[i].i = slots[i] = s;
	}
	int s = heap_alloc_slot(VM_PAGE); heap_set_page(s, a); return s;
}

/* push args per declaration, so_call, return pushed slot count and value */
static int so_call(int fno, int arg3, int self, int func, int *ret)
{
	struct ain_hll_function *f = &ain->libraries[so_LIB].functions[fno];
	int sp = stack_ptr;
	stack_push(self);
	if (f->nr_arguments == 2) { stack_push(-1); stack_push(func); }
	hll_call(so_LIB, fno, arg3);
	int got = stack_ptr - sp;
	*ret = got ? stack[sp].i : 0;
	stack_ptr = sp;
	return got;
}

static void so_expect_ints(const char *label, int self, const int *want, int n)
{
	struct page *a = heap[self].page;
	bool ok = a && a->nr_vars == n;
	for (int i = 0; ok && i < n; i++) ok = a->values[i].i == want[i];
	printf("  %-44s %s  got=[", label, ok ? "PASS" : "FAIL");
	for (int i = 0; a && i < a->nr_vars; i++) printf("%s%d", i ? "," : "", a->values[i].i);
	printf("]\n");
	if (!ok) so_fails++;
}

static void so_expect_ret(const char *label, int got, int ret, int self, int ref_before)
{
	bool ok = got == 1 && ret == self && HEAP_REF(self) == ref_before + 1;
	printf("  %-44s %s  slots=%d ret=%d self=%d ref %d->%d\n", label, ok ? "PASS" : "FAIL",
	       got, ret, self, ref_before, HEAP_REF(self));
	if (!ok) so_fails++;
	if (got == 1 && ret == self) heap_unref(ret); /* bytecode DELETE of the dummy */
}

static void so_t_value(void)
{
	int fSort1 = so_find_fn("Sort", 1, false), fAsc = so_find_fn("AscSort", 1, false);
	int fDesc = so_find_fn("DescSort", 1, false), fQ0 = so_find_fn("QuickSort", 1, false);
	printf("fno Sort(self)=%d AscSort=%d DescSort=%d QuickSort(self)=%d\n", fSort1, fAsc, fDesc, fQ0);
	int r, g, before;
	{ int v[] = {3, -1, 2, 1}; int s = so_int_array(v, 4); before = HEAP_REF(s);
	  g = so_call(fSort1, 1, s, -1, &r); int w[] = {-1, 1, 2, 3};
	  so_expect_ints("Sort[35] int arg3=1", s, w, 4); so_expect_ret("Sort[35] returns owned self", g, r, s, before); }
	{ int v[] = {5, -7, 3}; int s = so_int_array(v, 3); before = HEAP_REF(s);
	  g = so_call(fAsc, 1, s, -1, &r); int w[] = {-7, 3, 5};
	  so_expect_ints("AscSort[36] int", s, w, 3); so_expect_ret("AscSort[36] returns owned self", g, r, s, before); }
	{ int v[] = {5, -7, 3, 5}; int s = so_int_array(v, 4); before = HEAP_REF(s);
	  g = so_call(fDesc, 1, s, -1, &r); int w[] = {5, 5, 3, -7};
	  so_expect_ints("DescSort[37] int", s, w, 4); so_expect_ret("DescSort[37] returns owned self", g, r, s, before); }
	{ int v[] = {9, 4, 7, 1, 8}; int s = so_int_array(v, 5); before = HEAP_REF(s);
	  g = so_call(fQ0, 1, s, -1, &r); int w[] = {1, 4, 7, 8, 9};
	  so_expect_ints("QuickSort[38] int", s, w, 5); so_expect_ret("QuickSort[38] returns owned self", g, r, s, before); }
	/* strings: allocate so slot order != text order */
	{ const char *t[] = {"b", "a", "B", "ab"}; int sl[4]; int s = so_str_array(t, 4, sl); before = HEAP_REF(s);
	  g = so_call(fSort1, 2, s, -1, &r); int w[] = {sl[2], sl[1], sl[3], sl[0]}; /* B < a < ab < b (bytes) */
	  so_expect_ints("Sort[35] string arg3=2 -> B,a,ab,b", s, w, 4); so_expect_ret("Sort[35] string ret", g, r, s, before); }
	{ const char *t[] = {"Button3", "Button1", "Button2"}; int sl[3]; int s = so_str_array(t, 3, sl); before = HEAP_REF(s);
	  g = so_call(fQ0, 2, s, -1, &r); int w[] = {sl[1], sl[2], sl[0]};
	  so_expect_ints("QuickSort[38] string (GetUser names)", s, w, 3); so_expect_ret("QuickSort[38] string ret", g, r, s, before); }
	/* object elements: native value comparator is constant false */
	{ int st = ain_get_struct(ain, "CASPos"); int x[] = {3, 1, 2}, y[] = {0, 0, 0}, sl[3];
	  int s = so_pos_array(st, x, y, 3, sl);
	  g = so_call(fSort1, 2, s, -1, &r); int w[] = {sl[0], sl[1], sl[2]};
	  so_expect_ints("Sort[35] objects: no order -> unchanged", s, w, 3); heap_unref(r);
	  s = so_pos_array(st, x, y, 3, sl);
	  g = so_call(fQ0, 2, s, -1, &r); int w2[] = {sl[2], sl[0], sl[1]};
	  so_expect_ints("QuickSort[38] objects: native permutation z,x,y", s, w2, 3); heap_unref(r);
	  s = so_pos_array(st, x, y, 3, sl);
	  g = so_call(fDesc, 2, s, -1, &r); int w3[] = {sl[2], sl[1], sl[0]};
	  so_expect_ints("DescSort[37] objects: !false -> reversed", s, w3, 3); heap_unref(r); }
}

static void so_t_pred(void)
{
	int fSort0 = so_find_fn("Sort", 2, true), fQ1 = so_find_fn("QuickSort", 2, true);
	int st = ain_get_struct(ain, "CASPos");
	printf("fno Sort(self,f)=%d QuickSort(self,f)=%d CASPos=%d\n", fSort0, fQ1, st);
	int r, g, before;
	/* 37050: lhs.X < rhs.X ; ties keep input order (stable insertion) */
	{ int x[] = {2, 1, 2, 0}, y[] = {10, 11, 12, 13}, sl[4]; int s = so_pos_array(st, x, y, 4, sl); before = HEAP_REF(s);
	  g = so_call(fSort0, 2, s, 37050, &r); int w[] = {sl[3], sl[1], sl[0], sl[2]};
	  so_expect_ints("Sort[34] pred X< stable", s, w, 4); so_expect_ret("Sort[34] returns owned self", g, r, s, before); }
	/* 36467 is `lhs.Score > rhs.Score` on StandSearchResult; member 0 of CASPos is X */
	{ int x[] = {1, 3, 2}, y[] = {0, 0, 0}, sl[3]; int s = so_pos_array(st, x, y, 3, sl);
	  g = so_call(fSort0, 2, s, 36467, &r); int w[] = {sl[1], sl[2], sl[0]};
	  so_expect_ints("Sort[34] pred member0 > (desc)", s, w, 3); heap_unref(r); }
	{ int x[] = {3, 1, 2}, y[] = {0, 0, 0}, sl[3]; int s = so_pos_array(st, x, y, 3, sl); before = HEAP_REF(s);
	  g = so_call(fQ1, 2, s, 37050, &r); int w[] = {sl[1], sl[2], sl[0]};
	  so_expect_ints("QuickSort[39] pred X<", s, w, 3); so_expect_ret("QuickSort[39] returns owned self", g, r, s, before); }
	/* native quicksort is not stable: two equal keys come out swapped */
	{ int x[] = {1, 1}, y[] = {20, 21}, sl[2]; int s = so_pos_array(st, x, y, 2, sl);
	  g = so_call(fQ1, 2, s, 37050, &r); int w[] = {sl[1], sl[0]};
	  so_expect_ints("QuickSort[39] ties swapped like 0x646a00", s, w, 2); heap_unref(r); }
}

static void so_t_remain(void)
{
	int fRem = so_find_fn("Remain", 2, true);
	printf("fno Remain=%d\n", fRem);
	int r, g, before;
	/* empty int array: checks that Remain returns an owned self handle */
	{ int s = so_int_array(NULL, 0); before = HEAP_REF(s);
	  g = so_call(fRem, 1, s, 37050, &r); so_expect_ret("Remain[19] returns owned self", g, r, s, before); }
}

static void so_run(const char *name, void (*fn)(void))
{
	fflush(stdout);
	pid_t pid = fork();
	if (pid == 0) { fn(); fflush(stdout); _exit(so_fails ? 1 : 0); }
	int st = 0; waitpid(pid, &st, 0);
	if (WIFSIGNALED(st)) { printf("%s: child killed by signal %d\n", name, WTERMSIG(st)); so_fails++; }
	else if (WEXITSTATUS(st)) { printf("%s: FAILURES\n", name); so_fails++; }
	else printf("%s: all pass\n", name);
}


static int test_sort(void)
{
	so_LIB = ain_get_library(ain, "Array");
	so_run("value", so_t_value);
	so_run("pred", so_t_pred);
	so_run("remain", so_t_remain);
	printf("SORT summary: %s\n", so_fails ? "FAIL" : "PASS");
	return so_fails ? 88 : 0;
}
