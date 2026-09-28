/* Headless check for H1 (2026-09-26): Array.First single-argument overload is
 * bound to the two-argument C implementation Array_First(array, func).
 * Replays the Lottery call shape: Shuffle(temp, -1) then First#1(temp) with
 * hll_arg3 = 2 (string elements). Real ffi path via hll_call; no bytecode. */
static int fo_find_lib(const char *name) {
    for (int i = 0; i < ain->nr_libraries; i++)
        if (!strcmp(ain->libraries[i].name, name)) return i;
    return -1;
}
static int fo_find_fn(int libno, const char *name, int nr_args, int has_func, int nth) {
    int seen = 0;
    for (int j = 0; j < ain->libraries[libno].nr_functions; j++) {
        struct ain_hll_function *f = &ain->libraries[libno].functions[j];
        if (strcmp(f->name, name) || f->nr_arguments != nr_args) continue;
        int hf = 0;
        for (int k = 0; k < f->nr_arguments; k++)
            if (f->arguments[k].type.data == AIN_HLL_FUNC || f->arguments[k].type.data == AIN_HLL_FUNC_71) hf = 1;
        if (hf != has_func) continue;
        if (seen++ == nth) return j;
    }
    return -1;
}
static int fo_make_string_array(const char **vals, int n) {
    struct page *arr = alloc_page(ARRAY_PAGE, AIN_ARRAY_STRING, n);
    arr->array.rank = 1;
    for (int k = 0; k < n; k++) {
        int s = heap_alloc_slot(VM_STRING);
        heap[s].s = make_string(vals[k], strlen(vals[k]));
        arr->values[k].i = s;
    }
    return heap_alloc_page(arr);
}
static const char *fo_slot_text(int r) {
    if (r > 1 && (size_t)r < heap_size && HEAP_REF(r) > 0 && heap[r].type == VM_STRING && heap[r].s)
        return heap[r].s->text;
    return "(not a live string)";
}
static int test_first_overload(void) {
    int lib = fo_find_lib("Array"); assert(lib >= 0);
    int first1 = fo_find_fn(lib, "First", 1, 0, 1);   /* First#1 = second 1-arg decl */
    int first0 = fo_find_fn(lib, "First", 1, 0, 0);
    int first3 = fo_find_fn(lib, "First", 2, 1, 1);   /* First#3 = second predicate decl */
    int shuffle = fo_find_fn(lib, "Shuffle", 2, 0, 0);
    int at = fo_find_fn(lib, "At", 2, 0, 0);
    int last1 = fo_find_fn(lib, "Last", 1, 0, 1);
    int max1 = fo_find_fn(lib, "Max", 1, 0, 1);
    printf("FIRST decl indices: First#0=%d First#1=%d First#3=%d Shuffle=%d At=%d Last#1=%d Max#1=%d lib=%d\n",
           first0, first1, first3, shuffle, at, last1, max1, lib);
    assert(first1 >= 0 && shuffle >= 0 && at >= 0);
    const char *vals[3] = {"alpha", "beta", "gamma"};
    int bad = 0, good = 0;
    for (int round = 0; round < 20; round++) {
        int slot = fo_make_string_array(vals, 3);
        int sp = stack_ptr;
        /* control: At(slot, 0) */
        stack_push(slot); stack_push(0); hll_call(lib, at, 2);
        int r_at = stack_pop().i; assert(stack_ptr == sp);
        /* Lottery shape: Shuffle(slot, -1); First#1(slot) */
        stack_push(slot); stack_push(-1); hll_call(lib, shuffle, 2);
        assert(stack_ptr == sp);
        stack_push(slot); hll_call(lib, first1, 2);
        int r_first = stack_pop().i; assert(stack_ptr == sp);
        /* also Last#1 and Max#1 for comparison */
        stack_push(slot); hll_call(lib, last1, 2);
        int r_last = stack_pop().i; assert(stack_ptr == sp);
        int r_max = -999;
        if (max1 >= 0) { stack_push(slot); hll_call(lib, max1, 2); r_max = stack_pop().i; assert(stack_ptr == sp); }
        printf("FIRST round=%d At(0)=%d[%s] First#1=%d[%s] Last#1=%d[%s] Max#1=%d[%s]\n",
               round, r_at, fo_slot_text(r_at), r_first, fo_slot_text(r_first),
               r_last, fo_slot_text(r_last), r_max, fo_slot_text(r_max));
        if (r_first <= 1) bad++; else good++;
        if (r_at > 1) heap_unref(r_at);
        if (r_first > 1) heap_unref(r_first);
        if (r_last > 1) heap_unref(r_last);
        if (r_max > 1) heap_unref(r_max);
        heap_unref(slot);
    }
    printf("FIRST summary: First#1 returned -1/invalid in %d of 20 rounds, valid in %d\n", bad, good);
    if (bad) { puts("FIRST FAIL: single-arg First overload returns invalid element (H1 confirmed)"); return 88; }
    puts("FIRST PASS: single-arg First returns a live element"); return 0;
}
