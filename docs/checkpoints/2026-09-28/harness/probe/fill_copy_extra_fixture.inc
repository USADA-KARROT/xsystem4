/* Extra Fill/Copy edge cases from the adversarial review (2026-09-28).
 * Requires first_overload_fixture.inc, array_overload_fixture.inc, order_fixture.inc. */
static int fx_fail;
static void fx_expect(const char *what, const char *got, const char *want)
{
	int ok = !strcmp(got, want);
	printf("FILLX %-44s got=%-16s want=%-16s %s\n", what, got, want, ok ? "ok" : "FAIL");
	if (!ok) fx_fail++;
}

static const char *fx_ints(int slot, int ret)
{
	static char buf[128];
	struct page *a = heap[slot].page;
	int w = snprintf(buf, sizeof buf, "r=%d [", ret);
	for (int i = 0; a && i < a->nr_vars; i++)
		w += snprintf(buf + w, sizeof buf - w, "%s%d", i ? "," : "", a->values[i].i);
	snprintf(buf + w, sizeof buf - w, "]");
	return buf;
}

static int fx_call(int lib, int fno, int arg3, const int *args, int n)
{
	int sp = stack_ptr;
	for (int i = 0; i < n; i++) stack_push(args[i]);
	hll_call(lib, fno, arg3);
	int r = stack_ptr > sp ? stack_pop().i : 0;
	assert(stack_ptr == sp);
	return r;
}

static int test_fill_copy_extra(void)
{
	int lib = fo_find_lib("Array");
	int copy2 = fo_find_fn(lib, "Copy", 4, 0, 0), fill0 = fo_find_fn(lib, "Fill", 2, 0, 0);
	size_t live0 = live_slots();
	/* dst n=3, src n=5, Copy(dst, src, 2, 10): native clamps with dst length -> 1 */
	int d1 = ob_int_array((int[]){0, 0, 0}, 3), s1 = ob_int_array((int[]){10, 11, 12, 13, 14}, 5);
	int r1 = fx_call(lib, copy2, 1, (int[]){d1, s1, 2, 10}, 4);
	fx_expect("Copy#2 dst3 src5 (2,10) native clamp", fx_ints(d1, r1), "r=1 [12,0,0]");
	/* dst n=5, src n=3, Copy(dst, src, 1, 10): count 4, reads past src -> null */
	int d2 = ob_int_array((int[]){9, 9, 9, 9, 9}, 5), s2 = ob_int_array((int[]){20, 21, 22}, 3);
	int r2 = fx_call(lib, copy2, 1, (int[]){d2, s2, 1, 10}, 4);
	fx_expect("Copy#2 dst5 src3 (1,10) past-src null", fx_ints(d2, r2), "r=4 [21,22,0,0,9]");
	/* Fill on a value-struct array: every element must be its own object */
	int st = or_struct("CASPos");
	int p = or_struct_array(st, (const int[][2]){{0, 0}, {0, 0}, {0, 0}}, 3);
	int v = alloc_struct(st);
	heap[v].page->values[0].i = 7; heap[v].page->values[1].i = 8;
	int r3 = fx_call(lib, fill0, 2, (int[]){p, v}, 2);
	struct page *pp = heap[p].page;
	int e0 = pp->values[0].i, e1 = pp->values[1].i, e2 = pp->values[2].i;
	char got[64];
	snprintf(got, sizeof got, "r=%d distinct=%d x=%d,%d,%d", r3,
		 e0 != e1 && e1 != e2 && e0 != e2 && e0 != v,
		 heap[e0].page->values[0].i, heap[e1].page->values[0].i, heap[e2].page->values[0].i);
	fx_expect("Fill#0 CASPos array values copied", got, "r=3 distinct=1 x=7,7,7");
	heap[e0].page->values[0].i = 99;
	snprintf(got, sizeof got, "%d,%d", heap[e1].page->values[0].i, heap[v].page->values[0].i);
	fx_expect("Fill#0 writing one element leaves others", got, "7,7");
	heap_unref(v); heap_unref(p);
	heap_unref(d1); heap_unref(s1); heap_unref(d2); heap_unref(s2);
	char l0[16], l1[16];
	snprintf(l0, sizeof l0, "%zu", live0); snprintf(l1, sizeof l1, "%zu", live_slots());
	fx_expect("heap back to baseline", l1, l0);
	printf("FILLX summary: %d failure(s)\n", fx_fail);
	return fx_fail ? 88 : 0;
}
