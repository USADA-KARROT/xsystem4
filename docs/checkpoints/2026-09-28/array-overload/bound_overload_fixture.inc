/* Headless check: Array LowerBound/UpperBound/BinarySearch overloads.
 * Value overloads via hll_call on a synthetic int array; comparator
 * overloads via the real IdSet<T>@Set / @Exists bytecode (lambdas
 * fno 37473/37474 and 37557/37558), which capture the key with X_GETENV. */
static int bo_member(int st, const char *name) {
    for (int j = 0; j < ain->structures[st].nr_members; j++)
        if (!strcmp(ain->structures[st].members[j].name, name)) return j;
    return -1;
}
static int bo_make_int_array(const int *v, int n) {
    struct page *arr = alloc_page(ARRAY_PAGE, AIN_ARRAY_INT, n);
    arr->array.rank = 1;
    for (int k = 0; k < n; k++) arr->values[k].i = v[k];
    return heap_alloc_page(arr);
}
static int bo_call2(int lib, int fn, int arr_slot, int value, int arg3) {
    int sp = stack_ptr;
    stack_push(arr_slot); stack_push(value); hll_call(lib, fn, arg3);
    int r = stack_pop().i; assert(stack_ptr == sp); return r;
}
static int bo_fail;
static void bo_expect(const char *what, int got, int want) {
    printf("BOUND %-40s got=%d want=%d %s\n", what, got, want, got == want ? "ok" : "FAIL");
    if (got != want) bo_fail++;
}
static int bo_string(const char *s) {
    int slot = heap_alloc_slot(VM_STRING);
    heap[slot].s = make_string(s, strlen(s));
    return slot;
}
static void bo_dump_list(int obj, int m) {
    int a = heap[obj].page->values[m].i;
    struct page *p = (a > 0 && heap[a].type == VM_PAGE) ? heap[a].page : NULL;
    printf("BOUND m_list n=%d:", p ? p->nr_vars : 0);
    for (int i = 0; p && i < p->nr_vars; i++) {
        int v = p->values[i].i;
        if (p->a_type == AIN_ARRAY_STRING || (v > 1 && (size_t)v < heap_size && heap[v].type == VM_STRING))
            printf(" \"%s\"", heap[v].s->text);
        else printf(" %d", v);
    }
    printf("\n");
}
static int test_bound_overload(void) {
    int lib = fo_find_lib("Array"); assert(lib >= 0);
    int lb0 = fo_find_fn(lib, "LowerBound", 2, 0, 0), ub0 = fo_find_fn(lib, "UpperBound", 2, 0, 0);
    int bs0 = fo_find_fn(lib, "BinarySearch", 2, 0, 0);
    printf("BOUND decl LowerBound#0=%d UpperBound#0=%d BinarySearch#0=%d\n", lb0, ub0, bs0);
    /* 1. value overloads, int elements (hll_arg3 = 1) */
    const int v[4] = {1, 3, 3, 5};
    int arr = bo_make_int_array(v, 4);
    const int keys[6] = {0, 1, 3, 4, 5, 6}, lbw[6] = {0, 0, 1, 3, 3, 4}, ubw[6] = {0, 1, 3, 3, 4, 4};
    const int bsw[6] = {-1, 0, 2, -1, 3, -1}; /* native half-open probe: key 3 hits mid=2 first */
    char buf[64];
    for (int i = 0; i < 6; i++) {
        snprintf(buf, sizeof buf, "LowerBound#0 {1,3,3,5} key=%d", keys[i]);
        bo_expect(buf, bo_call2(lib, lb0, arr, keys[i], 1), lbw[i]);
        snprintf(buf, sizeof buf, "UpperBound#0 {1,3,3,5} key=%d", keys[i]);
        bo_expect(buf, bo_call2(lib, ub0, arr, keys[i], 1), ubw[i]);
        snprintf(buf, sizeof buf, "BinarySearch#0 {1,3,3,5} key=%d", keys[i]);
        bo_expect(buf, bo_call2(lib, bs0, arr, keys[i], 1), bsw[i]);
    }
    heap_unref(arr);
    int empty = heap_alloc_page(NULL);
    bo_expect("LowerBound#0 empty key=7", bo_call2(lib, lb0, empty, 7, 1), 0);
    bo_expect("BinarySearch#0 empty key=7", bo_call2(lib, bs0, empty, 7, 1), -1);
    heap_unref(empty);

    /* 2. comparator overloads through real IdSet<string> bytecode */
    int st = ain_get_struct(ain, "IdSet<string>"); assert(st >= 0);
    int m = bo_member(st, "m_list"); assert(m >= 0);
    int set = alloc_struct(st);
    const char *ins[5] = {"d", "b", "a", "c", "b"};
    for (int i = 0; i < 5; i++) {
        int s = bo_string(ins[i]);
        call_method_args(35058, set, 1, &s);  /* Set: Exists, LowerBound#1, Insert */
        heap_unref(s);
    }
    bo_dump_list(set, m);
    {
        int a = heap[set].page->values[m].i;
        struct page *p = (a > 0 && heap[a].type == VM_PAGE) ? heap[a].page : NULL;
        bo_expect("IdSet<string> size after 5 Set", p ? p->nr_vars : 0, 4);
        const char *want[4] = {"a", "b", "c", "d"};
        int ordered = p && p->nr_vars == 4;
        for (int i = 0; ordered && i < 4; i++) ordered = !strcmp(heap[p->values[i].i].s->text, want[i]);
        bo_expect("IdSet<string> sorted a,b,c,d", ordered, 1);
    }
    const char *q[6] = {"a", "c", "d", "bb", "0", "z"}; const int qw[6] = {1, 1, 1, 0, 0, 0};
    for (int i = 0; i < 6; i++) {
        int s = bo_string(q[i]);
        call_method_args(35059, set, 1, &s);  /* Exists: BinarySearch#1 >= 0 */
        int r = stack_pop().i; heap_unref(s);
        snprintf(buf, sizeof buf, "IdSet<string>.Exists(\"%s\")", q[i]);
        bo_expect(buf, r != 0, qw[i]);
    }
    heap_unref(set);

    /* 3. comparator overloads, enum elements (hll_arg3 = 1) */
    int st2 = ain_get_struct(ain, "IdSet<GameDialogType>"); assert(st2 >= 0);
    int m2 = bo_member(st2, "m_list"); assert(m2 >= 0);
    int set2 = alloc_struct(st2);
    const int e_ins[4] = {5, 1, 3, 1};
    for (int i = 0; i < 4; i++) { int x = e_ins[i]; call_method_args(35342, set2, 1, &x); }
    bo_dump_list(set2, m2);
    {
        int a = heap[set2].page->values[m2].i;
        struct page *p = (a > 0 && heap[a].type == VM_PAGE) ? heap[a].page : NULL;
        int ok = p && p->nr_vars == 3 && p->values[0].i == 1 && p->values[1].i == 3 && p->values[2].i == 5;
        bo_expect("IdSet<GameDialogType> == {1,3,5}", ok, 1);
    }
    const int e_q[4] = {1, 3, 5, 2}, e_w[4] = {1, 1, 1, 0};
    for (int i = 0; i < 4; i++) {
        int x = e_q[i]; call_method_args(35343, set2, 1, &x);
        int r = stack_pop().i;
        snprintf(buf, sizeof buf, "IdSet<GameDialogType>.Exists(%d)", e_q[i]);
        bo_expect(buf, r != 0, e_w[i]);
    }
    heap_unref(set2);
    printf("BOUND summary: %d failure(s)\n", bo_fail);
    return bo_fail ? 89 : 0;
}
