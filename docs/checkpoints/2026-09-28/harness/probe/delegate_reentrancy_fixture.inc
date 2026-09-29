/* Bounded v14 callback Clear regression; include after the VM and helpers.
 * Forward-declare static void delegate_reentrancy_probe_step(void), then call
 * it from probe_step(). test_delegate_reentrancy() uses unmodified original
 * AIN f20752 -> delegate248 -> f36081. The hook only removes the live delegate
 * at the first callback opcode and observes ownership; it never writes a
 * callback result, changes bytecode, or adds a keep-alive reference.
 * Empty collection is an intentional input to the original true branch.
 * This covers normal RETURN, not scenario/timeout unwinding or cycle GC.
 * The delegate owns the environment but not the receiver (v14 method targets
 * are weak, original 0x652210), so the harness keeps the collection's root;
 * the callback frame pins both while it runs.
 */
static struct {
    bool active, cleared, return_seen, released_seen;
    int collection, env, notify;
    unsigned long active_steps;
} delegate_reentrancy;

static void delegate_reentrancy_probe_step(void)
{
    if (!delegate_reentrancy.active) return;
    struct function_call *callback = NULL;
    for (int i = call_stack_ptr - 1; i >= 0; i--) {
        if (call_stack[i].fno == 36081) { callback = &call_stack[i]; break; }
    }
    const int obj = delegate_reentrancy.collection;
    const int env = delegate_reentrancy.env;
    if (callback) {
        assert(heap_index_valid(obj) && heap_index_valid(env));
        assert(heap[obj].type == VM_PAGE && heap[obj].page->index == 612);
        assert(heap[env].type == VM_PAGE && heap[env].page->index == 27031);
        assert(callback->delegate_obj_ref == obj && callback->delegate_env_ref == env);
        if (!delegate_reentrancy.cleared) {
            assert(callback == &call_stack[call_stack_ptr - 1]);
            assert(instr_ptr == ain->functions[36081].address);
            /* obj: harness root + frame pin; env: delegate + frame pin
             * (the harness root of env was relinquished). */
            assert(HEAP_REF(obj) == 2 && HEAP_REF(env) == 2);
            int dg = delegate_reentrancy.notify;
            assert(heap[dg].page->nr_vars == 3);
            heap_set_page(dg, delegate_clear(heap[dg].page));
            assert(heap[dg].page->nr_vars == 0);
            assert(HEAP_REF(obj) == 2 && HEAP_REF(env) == 1);
            delegate_reentrancy.cleared = true;
        }
        assert(HEAP_REF(obj) >= 1 && HEAP_REF(env) >= 1);
        delegate_reentrancy.active_steps++;
        if (callback == &call_stack[call_stack_ptr - 1]
                && (get_opcode(instr_ptr) & ~OPTYPE_MASK) == RETURN) {
            /* Observe the actual callback's two X_ASSIGN results. */
            struct page *args = heap[callback->page_slot].page;
            assert(args->nr_vars >= 2);
            int caller = args->values[0].i, member = args->values[1].i;
            assert(heap_index_valid(caller) && member == 0);
            assert(heap[caller].page->index == 20752);
            assert(heap[caller].page->values[member].i == 1);
            assert(heap[env].page->values[2].i == 1);
            delegate_reentrancy.return_seen = true;
        }
    } else if (delegate_reentrancy.cleared && !delegate_reentrancy.released_seen) {
        /* First original caller instruction after callback RETURN: no new
         * bytecode allocation has had an opportunity to reuse either slot. */
        assert(delegate_reentrancy.return_seen);
        assert(HEAP_REF(obj) == 1 && HEAP_REF(env) == 0);
        delegate_reentrancy.released_seen = true;
    }
}

static void test_delegate_reentrancy(void)
{
    assert(ain->version == 14);
    assert(ain->structures[508].nr_members == 3);
    assert(ain->structures[612].nr_members == 2);
    assert(ain->functions[27031].nr_vars == 4);
    assert(ain->functions[36081].nr_args == 2);
    assert(ain->functions[36081].vars[0].type.data == AIN_REF_BOOL);
    assert(!delegate_reentrancy.active);
    const size_t baseline = live_slots();
    const int initial_sp = stack_ptr, initial_csp = call_stack_ptr;
    const unsigned long initial_calls = calls[36081], initial_returns = returns[36081];
    const unsigned long initial_endwait = calls[9197];
    const union vm_value saved_global2 = heap[global_page_slot].page->values[2];
    const union vm_value saved_global3 = heap[global_page_slot].page->values[3];

    int collection = alloc_struct(612);
    int env = heap_alloc_page(alloc_page(LOCAL_PAGE, 27031, 4));
    for (int i = 0; i < 4; i++)
        heap[env].page->values[i] = variable_initval(ain->functions[27031].vars[i].type.data);
    heap[env].page->values[1].i = heap[env].page->values[2].i = 0;
    assert(heap[env].page->values[3].i == -1);
    int observer = alloc_struct(508);
    assert(heap[observer].page->values[0].i == -1);
    assert(heap[observer].page->values[1].i == -1);
    /* Transfer each page's initial owner into its owning observer field.
     * Seeding bypasses the unrelated DG_NEW/X_ASSIGN initializer; execution
     * and callback behavior below still use original production bytecode. */
    heap[observer].page->values[0].i = heap_alloc_page(alloc_page(DELEGATE_PAGE, 0, 0));
    int notify = heap_alloc_page(delegate_new_from_method_env(collection, 36081, env));
    heap[observer].page->values[1].i = notify;
    heap[observer].page->values[2].i = 0;
    assert(HEAP_REF(collection) == 1 && HEAP_REF(env) == 2);
    heap_unref(env); /* surrender the harness root of env only */
    assert(HEAP_REF(collection) == 1 && HEAP_REF(env) == 1);
    heap[global_page_slot].page->values[2].i = 0;
    heap[global_page_slot].page->values[3].i = 12345;
    delegate_reentrancy = (__typeof__(delegate_reentrancy)) {
        .active = true, .collection = collection, .env = env, .notify = notify,
    };

    call_method0(20752, observer);
    assert(delegate_reentrancy.cleared && delegate_reentrancy.return_seen);
    assert(delegate_reentrancy.released_seen && delegate_reentrancy.active_steps > 1);
    assert(calls[36081] == initial_calls + 1 && returns[36081] == initial_returns + 1);
    assert(calls[9197] == initial_endwait + 1);
    assert(heap[observer].page->values[2].i == 1);
    assert(heap[global_page_slot].page->values[2].i == 1);
    assert(heap[global_page_slot].page->values[3].i == 0);
    assert(heap[notify].page->nr_vars == 0);
    assert(stack_ptr == initial_sp && call_stack_ptr == initial_csp);
    delegate_reentrancy.active = false;
    heap_unref(observer); /* does not compensate for any missing release */
    heap_unref(collection); /* the harness root kept for the weak target */
    assert(live_slots() == baseline);
    heap[global_page_slot].page->values[2] = saved_global2;
    heap[global_page_slot].page->values[3] = saved_global3;
    printf("DELEGATE self-Clear PASS: original 20752->36081; after Clear obj=root+pin env=pin; "
           "callback_steps=%lu; return releases env, frame unpins obj; live=%zu baseline=%zu\n",
           delegate_reentrancy.active_steps, live_slots(), baseline);
    fflush(stdout);
}
