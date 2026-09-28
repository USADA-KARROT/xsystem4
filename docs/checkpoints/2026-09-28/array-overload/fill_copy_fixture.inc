/* Headless check (2026-09-28): Array Fill/Copy/Realloc overload binding.
 * Real AIN declarations + real ffi.c hll_call path; no bytecode. Each mode
 * replays one CN call shape (ain_code.txt line in comment). Exit 0 = native
 * semantics (dohnadohna_dump_SCY.exe 0x648120 / 0x648210 / 0x67f4d0). */
static int fc_int_array(const int *v, int n) {
    struct page *a = alloc_page(ARRAY_PAGE, AIN_ARRAY_INT, n);
    a->array.rank = 1;
    for (int i = 0; i < n; i++) a->values[i].i = v ? v[i] : 0;
    return heap_alloc_page(a);
}
static int fc_str(const char *s) {
    int slot = heap_alloc_slot(VM_STRING);
    heap[slot].s = make_string(s, strlen(s));
    return slot;
}
static const char *fc_text(int slot) {
    if (slot > 1 && (size_t)slot < heap_size && heap[slot].type == VM_STRING && heap[slot].s)
        return heap[slot].s->text;
    return "(none)";
}
static void fc_dump(const char *tag, int slot) {
    struct page *a = heap[slot].page;
    printf("FC %s n=%d [", tag, a ? a->nr_vars : -1);
    for (int i = 0; a && i < a->nr_vars; i++) printf("%s%d", i ? "," : "", a->values[i].i);
    printf("]\n");
}
static int fc_expect(int slot, const int *want, int n) {
    struct page *a = heap[slot].page;
    if (!a || a->nr_vars != n) return 0;
    for (int i = 0; i < n; i++) if (a->values[i].i != want[i]) return 0;
    return 1;
}
static int fc_call_ret(int lib, int fn, int arg3, int sp) {
    hll_call(lib, fn, arg3);
    int r = stack_pop().i;
    assert(stack_ptr == sp);
    return r;
}
static int test_fill_copy(const char *mode) {
    int lib = fo_find_lib("Array"); assert(lib >= 0);
    int fill0 = fo_find_fn(lib, "Fill", 2, 0, 0), fill1 = fo_find_fn(lib, "Fill", 4, 0, 0);
    int copy0 = fo_find_fn(lib, "Copy", 2, 0, 0), copy1 = fo_find_fn(lib, "Copy", 3, 0, 0);
    int copy2 = fo_find_fn(lib, "Copy", 4, 0, 0), copy3 = fo_find_fn(lib, "Copy", 5, 0, 0);
    int re0 = fo_find_fn(lib, "Realloc", 2, 0, 0), re1 = fo_find_fn(lib, "Realloc", 3, 0, 0);
    int first0 = fo_find_fn(lib, "First", 1, 0, 0), first2 = fo_find_fn(lib, "First", 2, 1, 0);
    printf("FC decl Fill=%d,%d Copy=%d,%d,%d,%d Realloc=%d,%d First=%d,%d\n",
           fill0, fill1, copy0, copy1, copy2, copy3, re0, re1, first0, first2);
    int sp = stack_ptr, ok = 1;
    if (!strcmp(mode, "fc-selector")) {
        int idx[] = {re0, re1, fill0, fill1, copy0, copy1, copy2, copy3, first0, first2};
        const char *nm[] = {"Realloc#0", "Realloc#1", "Fill#0", "Fill#1", "Copy#0", "Copy#1", "Copy#2", "Copy#3", "First#0", "First#2"};
        for (int k = 0; k < 10; k++) {
            void *fp = libraries[lib] ? (void *)libraries[lib][idx[k]].fun : NULL;
            printf("FC bind %s decl=%d fun=%p\n", nm[k], idx[k], fp);
            if (!fp) ok = 0;
        }
        if (libraries[lib][fill0].fun == libraries[lib][fill1].fun) ok = 0;
        if (libraries[lib][copy0].fun == libraries[lib][copy3].fun) ok = 0;
        if (libraries[lib][re0].fun == libraries[lib][re1].fun) ok = 0;
    } else if (!strcmp(mode, "fc-fill0")) {
        /* 475905 CDrawMovie@Release: Fill(m_keyDownList, 0), arg3=1 */
        int v[] = {1, 1, 1, 1, 1}, want[] = {0, 0, 0, 0, 0};
        int a = fc_int_array(v, 5);
        stack_push(a); stack_push(0);
        int r = fc_call_ret(lib, fill0, 1, sp);
        fc_dump("fill0", a); printf("FC fill0 ret=%d (want 5)\n", r);
        ok = fc_expect(a, want, 5) && r == 5;
    } else if (!strcmp(mode, "fc-fill1-int")) {
        /* 831929 CASConfigData@0: Fill#1(table, 0, Numof(table)=50, -1), arg3=1 */
        int a = fc_int_array(NULL, 50);
        stack_push(a); stack_push(0); stack_push(50); stack_push(-1);
        int r = fc_call_ret(lib, fill1, 1, sp);
        struct page *p = heap[a].page;
        int allm1 = 1; for (int i = 0; i < 50; i++) if (p->values[i].i != -1) allm1 = 0;
        printf("FC fill1-int ret=%d first=%d last=%d all_minus1=%d (want ret 50, all -1)\n",
               r, p->values[0].i, p->values[49].i, allm1);
        ok = allm1 && r == 50;
    } else if (!strcmp(mode, "fc-fill1-str")) {
        /* 376271 SSystemInfo@0: Fill#1(m_list, 0, Numof(m_list), "0"), arg3=2 */
        const char *vals[3] = {"a", "b", "c"};
        int a = fo_make_string_array(vals, 3);
        int s = fc_str("0");
        stack_push(a); stack_push(0); stack_push(3); stack_push(s);
        int r = fc_call_ret(lib, fill1, 2, sp);
        struct page *p = heap[a].page;
        printf("FC fill1-str ret=%d elems=%d[%s] %d[%s] %d[%s]\n", r,
               p->values[0].i, fc_text(p->values[0].i), p->values[1].i, fc_text(p->values[1].i),
               p->values[2].i, fc_text(p->values[2].i));
        ok = r == 3;
        for (int i = 0; i < 3; i++) if (strcmp(fc_text(p->values[i].i), "0")) ok = 0;
        if (ok) {
            /* SetInfo#1 shape: S_ASSIGN through At() rewrites element 0 in place */
            heap_string_assign(p->values[0].i, make_string("changed", 7));
            printf("FC fill1-str after assign e0=[%s] e1=[%s] e2=[%s]\n",
                   fc_text(p->values[0].i), fc_text(p->values[1].i), fc_text(p->values[2].i));
            if (strcmp(fc_text(p->values[1].i), "0") || strcmp(fc_text(p->values[2].i), "0")) ok = 0;
        }
    } else if (!strcmp(mode, "fc-fill-clamp")) {
        int a = fc_int_array(NULL, 5);
        stack_push(a); stack_push(3); stack_push(10); stack_push(7);
        int r1 = fc_call_ret(lib, fill1, 1, sp);
        stack_push(a); stack_push(-2); stack_push(4); stack_push(8);
        int r2 = fc_call_ret(lib, fill1, 1, sp);
        stack_push(a); stack_push(7); stack_push(1); stack_push(9);
        int r3 = fc_call_ret(lib, fill1, 1, sp);
        int want[] = {8, 8, 0, 7, 7};
        fc_dump("fill-clamp", a);
        printf("FC fill-clamp ret=%d,%d,%d (want 2,2,-2)\n", r1, r2, r3);
        ok = fc_expect(a, want, 5) && r1 == 2 && r2 == 2 && r3 == -2;
    } else if (!strcmp(mode, "fc-copy0")) {
        /* 153716 SBatchRenameData@CopyCurrentNumbers: Copy(m_numberList, data.m_numberList) */
        int sv[] = {9, 8, 7, 6}, want[] = {9, 8, 7};
        int d = fc_int_array(NULL, 3), s = fc_int_array(sv, 4);
        stack_push(d); stack_push(s);
        int r = fc_call_ret(lib, copy0, 1, sp);
        fc_dump("copy0", d); printf("FC copy0 ret=%d (want 3)\n", r);
        ok = fc_expect(d, want, 3) && r == 3;
    } else if (!strcmp(mode, "fc-copy3")) {
        /* 524466 CallPartsUpdateEvent shape: Realloc(list, n); Copy#3(list, 0, dataList, 0, n) */
        int sv[] = {4, 5, 6}, want[] = {4, 5, 6};
        int d = fc_int_array(NULL, 0), s = fc_int_array(sv, 3);
        stack_push(d); stack_push(3); hll_call(lib, re0, 2); assert(stack_ptr == sp);
        stack_push(d); stack_push(0); stack_push(s); stack_push(0); stack_push(3);
        int r = fc_call_ret(lib, copy3, 2, sp);
        fc_dump("copy3", d); printf("FC copy3 ret=%d (want 3)\n", r);
        ok = fc_expect(d, want, 3) && r == 3;
    } else if (!strcmp(mode, "fc-copy3-str")) {
        /* 738876 ShuffleSoundList: Alloc(tempList, n); Copy#3(tempList, 0, soundList, 0, n), arg3=2 */
        const char *vals[2] = {"x", "y"};
        int s = fo_make_string_array(vals, 2);
        const char *empty[2] = {"", ""};
        int d = fo_make_string_array(empty, 2);
        stack_push(d); stack_push(0); stack_push(s); stack_push(0); stack_push(2);
        int r = fc_call_ret(lib, copy3, 2, sp);
        struct page *dp = heap[d].page, *spg = heap[s].page;
        printf("FC copy3-str ret=%d d=%d[%s],%d[%s] s=%d,%d\n", r, dp->values[0].i, fc_text(dp->values[0].i),
               dp->values[1].i, fc_text(dp->values[1].i), spg->values[0].i, spg->values[1].i);
        ok = r == 2 && !strcmp(fc_text(dp->values[0].i), "x") && !strcmp(fc_text(dp->values[1].i), "y")
             && dp->values[0].i != spg->values[0].i;
    } else if (!strcmp(mode, "fc-copy3-dest1")) {
        /* destIndex > 0: old dst_i_wrap lookup treats 1 as heap slot 1 (global page) */
        int sv[] = {5, 6, 7}, want[] = {0, 5, 6, 7};
        int d = fc_int_array(NULL, 4), s = fc_int_array(sv, 3);
        stack_push(d); stack_push(1); stack_push(s); stack_push(0); stack_push(3);
        int r = fc_call_ret(lib, copy3, 1, sp);
        fc_dump("copy3-dest1", d); printf("FC copy3-dest1 ret=%d (want 3)\n", r);
        ok = fc_expect(d, want, 4) && r == 3;
    } else if (!strcmp(mode, "fc-copy-overlap")) {
        int v[] = {1, 2, 3, 4, 5}, want[] = {1, 1, 2, 3, 4};
        int a = fc_int_array(v, 5);
        stack_push(a); stack_push(1); stack_push(a); stack_push(0); stack_push(4);
        int r = fc_call_ret(lib, copy3, 1, sp);
        fc_dump("copy-overlap", a); printf("FC copy-overlap ret=%d (want 4)\n", r);
        ok = fc_expect(a, want, 5) && r == 4;
    } else if (!strcmp(mode, "fc-realloc")) {
        int v[] = {1, 2}, w0[] = {1, 2, 0, 0}, w1[] = {1, 2, 9, 9}, w2[] = {1};
        int a = fc_int_array(v, 2), b = fc_int_array(v, 2);
        stack_push(a); stack_push(4); hll_call(lib, re0, 1); assert(stack_ptr == sp);
        stack_push(b); stack_push(4); stack_push(9); hll_call(lib, re1, 1); assert(stack_ptr == sp);
        fc_dump("realloc0", a); fc_dump("realloc1", b);
        ok = fc_expect(a, w0, 4) && fc_expect(b, w1, 4);
        stack_push(b); stack_push(1); stack_push(5); hll_call(lib, re1, 1); assert(stack_ptr == sp);
        fc_dump("realloc1-shrink", b);
        ok = ok && fc_expect(b, w2, 1);
    } else {
        printf("FC unknown mode %s\n", mode); return 2;
    }
    printf("FC %s %s\n", mode, ok ? "PASS" : "FAIL");
    return ok ? 0 : 88;
}
