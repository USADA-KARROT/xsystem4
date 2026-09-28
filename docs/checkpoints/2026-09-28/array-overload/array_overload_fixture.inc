/* Shape probe for non-predicate Array overloads (2026-09-28).
 * Calls each declaration through the real ffi path (hll_call) and checks that
 * the VM stack grows by exactly the number of slots the declaration returns.
 * Each case runs in a forked child so a mis-bound function that corrupts memory
 * does not take the other cases down. Requires first_overload_fixture.inc. */
#include <sys/wait.h>
#include <unistd.h>

static int ob_int_array(const int *v, int n)
{
	struct page *a = alloc_page(ARRAY_PAGE, AIN_ARRAY_INT, n);
	a->array.rank = 1;
	for (int i = 0; i < n; i++)
		a->values[i].i = v[i];
	return heap_alloc_page(a);
}

static int ob_ret_slots(const struct ain_hll_function *f, int arg3)
{
	switch (f->return_type.data) {
	case AIN_VOID: return 0;
	case AIN_REF_HLL_PARAM: return (arg3 >= 0x10000 || arg3 == 2) ? 1 : 2;
	default: return 1;
	}
}

static void ob_print_int_array(int slot)
{
	if (!(slot > 1 && heap_index_valid(slot) && heap[slot].page)) { printf("<gone>"); return; }
	struct page *a = heap[slot].page;
	printf("[");
	for (int i = 0; i < a->nr_vars; i++) printf("%s%d", i ? "," : "", a->values[i].i);
	printf("]");
}

/* ints: values for AIN_INT parameters in order; value: the hll_param argument */
static int ob_case(int lib, int fno, int arg3, const int *self_v, int self_n,
		   const int *src_v, int src_n, const int *ints, int value)
{
	const struct ain_hll_function *f = &ain->libraries[lib].functions[fno];
	int self = ob_int_array(self_v, self_n);
	int src = src_v ? ob_int_array(src_v, src_n) : -1;
	int sp = stack_ptr, k = 0;
	for (int i = 0; i < f->nr_arguments; i++) {
		switch (f->arguments[i].type.data) {
		case AIN_REF_ARRAY: case AIN_REF_ARRAY_TYPE: stack_push(self); break;
		case AIN_INT: stack_push(ints[k++]); break;
		case AIN_HLL_PARAM: stack_push(value); break;
		case AIN_WRAP: stack_push(src); break;
		default:
			printf("OB %s[%d] unsupported arg type %d\n", f->name, fno, f->arguments[i].type.data);
			return 2;
		}
	}
	hll_call(lib, fno, arg3);
	int want = ob_ret_slots(f, arg3), got = stack_ptr - sp;
	printf("OB %-12s[%2d] nargs=%d ret=%d want_slots=%d got_slots=%d", f->name, fno,
	       f->nr_arguments, f->return_type.data, want, got);
	if (got == 2) printf(" ret=(%d,%d)", stack[sp].i, stack[sp+1].i);
	else if (got == 1) printf(" ret=%d", stack[sp].i);
	printf(" self_after=");
	ob_print_int_array(self);
	printf("%s\n", got == want ? "" : "  <== STACK MISMATCH");
	fflush(stdout);
	return got == want ? 0 : 1;
}

static int ob_forked(int lib, const char *name, int nargs, int nth, int arg3,
		     const int *self_v, int self_n, const int *src_v, int src_n,
		     const int *ints, int value)
{
	int fno = fo_find_fn(lib, name, nargs, 0, nth);
	if (fno < 0) { printf("OB %s/%d#%d not declared\n", name, nargs, nth); return 0; }
	fflush(stdout);
	pid_t pid = fork();
	if (pid == 0)
		_exit(ob_case(lib, fno, arg3, self_v, self_n, src_v, src_n, ints, value));
	int status = 0;
	waitpid(pid, &status, 0);
	if (WIFSIGNALED(status)) {
		printf("OB %-12s[%2d] CRASHED signal=%d\n", name, fno, WTERMSIG(status));
		return 1;
	}
	return WEXITSTATUS(status) ? 1 : 0;
}

static int test_overload_shapes(void)
{
	int lib = fo_find_lib("Array");
	assert(lib >= 0);
	static const int u[] = {5, 3, 9, 1};      /* unsorted */
	static const int s[] = {1, 3, 5, 9};      /* sorted */
	static const int z[] = {0, 0, 0, 0, 0};
	static const int src[] = {7, 8, 9};
	static const int none[] = {0};
	int bad = 0;
	/* value-type int elements: hll_arg3 = 1 */
	bad += ob_forked(lib, "First", 1, 1, 1, u, 4, NULL, 0, none, 0);
	bad += ob_forked(lib, "Last", 1, 1, 1, u, 4, NULL, 0, none, 0);
	bad += ob_forked(lib, "Min", 1, 1, 1, u, 4, NULL, 0, none, 0);
	bad += ob_forked(lib, "Max", 1, 1, 1, u, 4, NULL, 0, none, 0);
	bad += ob_forked(lib, "Any", 1, 0, 1, u, 4, NULL, 0, none, 0);
	bad += ob_forked(lib, "Unique", 1, 0, 1, (int[]){3, 3, 1, 3, 1}, 5, NULL, 0, none, 0);
	bad += ob_forked(lib, "UniqueSorted", 1, 0, 1, (int[]){1, 1, 3, 3, 9}, 5, NULL, 0, none, 0);
	{ int i[] = {6}; bad += ob_forked(lib, "Realloc", 2, 0, 1, u, 4, NULL, 0, i, 0); }
	{ int i[] = {6}; bad += ob_forked(lib, "Realloc", 3, 0, 1, u, 4, NULL, 0, i, 4); }
	bad += ob_forked(lib, "Fill", 2, 0, 1, u, 4, NULL, 0, none, 7);
	{ int i[] = {1, 2}; bad += ob_forked(lib, "Fill", 4, 0, 1, u, 4, NULL, 0, i, 7); }
	bad += ob_forked(lib, "Copy", 2, 0, 1, z, 5, src, 3, none, 0);
	{ int i[] = {1}; bad += ob_forked(lib, "Copy", 3, 0, 1, z, 5, src, 3, i, 0); }
	{ int i[] = {1, 2}; bad += ob_forked(lib, "Copy", 4, 0, 1, z, 5, src, 3, i, 0); }
	{ int i[] = {1, 1, 2}; bad += ob_forked(lib, "Copy", 5, 0, 1, z, 5, src, 3, i, 0); }
	bad += ob_forked(lib, "Equals", 2, 0, 1, s, 4, s, 4, none, 0);
	bad += ob_forked(lib, "FindLast", 2, 0, 1, (int[]){3, 5, 3, 1}, 4, NULL, 0, none, 3);
	{ int i[] = {0, 2}; bad += ob_forked(lib, "FindLast", 4, 0, 1, (int[]){3, 5, 3, 1}, 4, NULL, 0, i, 3); }
	bad += ob_forked(lib, "LowerBound", 2, 0, 1, s, 4, NULL, 0, none, 4);
	bad += ob_forked(lib, "UpperBound", 2, 0, 1, s, 4, NULL, 0, none, 5);
	bad += ob_forked(lib, "BinarySearch", 2, 0, 1, s, 4, NULL, 0, none, 5);
	bad += ob_forked(lib, "BinarySearch", 2, 0, 1, s, 4, NULL, 0, none, 4);
	printf("OB summary: %d case(s) with stack mismatch or crash\n", bad);
	return bad ? 89 : 0;
}

/* string elements: hll_arg3 = 2, ref-typed return is 1 slot */
static int ob_str_case(int lib, int fno, const char **v, int n, const char *needle)
{
	const struct ain_hll_function *f = &ain->libraries[lib].functions[fno];
	int self = fo_make_string_array(v, n);
	int sp = stack_ptr;
	for (int i = 0; i < f->nr_arguments; i++) {
		switch (f->arguments[i].type.data) {
		case AIN_REF_ARRAY: case AIN_REF_ARRAY_TYPE: stack_push(self); break;
		case AIN_HLL_PARAM: {
			int s = heap_alloc_slot(VM_STRING);
			heap[s].s = make_string(needle, strlen(needle));
			stack_push(s);
			break;
		}
		default: printf("OBS %s unsupported arg\n", f->name); return 2;
		}
	}
	hll_call(lib, fno, 2);
	int want = ob_ret_slots(f, 2), got = stack_ptr - sp;
	printf("OBS %-12s[%2d] want_slots=%d got_slots=%d", f->name, fno, want, got);
	if (got == 1) printf(" ret=%d[%s]", stack[sp].i,
		f->return_type.data == AIN_REF_HLL_PARAM ? fo_slot_text(stack[sp].i) : "-");
	struct page *a = heap[self].page;
	printf(" self_after=[");
	for (int i = 0; a && i < a->nr_vars; i++) printf("%s%s", i ? "," : "", fo_slot_text(a->values[i].i));
	printf("]%s\n", got == want ? "" : "  <== STACK MISMATCH");
	fflush(stdout);
	return got == want ? 0 : 1;
}

static int obs_forked(int lib, const char *name, int nargs, int nth, const char **v, int n, const char *needle)
{
	int fno = fo_find_fn(lib, name, nargs, 0, nth);
	if (fno < 0) return 0;
	fflush(stdout);
	pid_t pid = fork();
	if (pid == 0) _exit(ob_str_case(lib, fno, v, n, needle));
	int status = 0; waitpid(pid, &status, 0);
	if (WIFSIGNALED(status)) { printf("OBS %-12s[%2d] CRASHED signal=%d\n", name, fno, WTERMSIG(status)); return 1; }
	return WEXITSTATUS(status) ? 1 : 0;
}

static int test_overload_shapes_str(void)
{
	int lib = fo_find_lib("Array");
	static const char *w[] = {"pear", "apple", "fig", "apple"};
	static const char *sorted[] = {"apple", "fig", "pear"};
	int bad = 0;
	bad += obs_forked(lib, "First", 1, 1, w, 4, "");
	bad += obs_forked(lib, "Last", 1, 1, w, 4, "");
	bad += obs_forked(lib, "Min", 1, 1, w, 4, "");
	bad += obs_forked(lib, "Max", 1, 1, w, 4, "");
	bad += obs_forked(lib, "Unique", 1, 0, (const char *[]){"a", "a", "b", "a"}, 4, "");
	bad += obs_forked(lib, "UniqueSorted", 1, 0, (const char *[]){"a", "a", "b", "b"}, 4, "");
	bad += obs_forked(lib, "FindLast", 2, 0, w, 4, "apple");
	bad += obs_forked(lib, "LowerBound", 2, 0, sorted, 3, "banana");
	bad += obs_forked(lib, "BinarySearch", 2, 0, sorted, 3, "fig");
	printf("OBS summary: %d case(s) with stack mismatch or crash\n", bad);
	return bad ? 89 : 0;
}
