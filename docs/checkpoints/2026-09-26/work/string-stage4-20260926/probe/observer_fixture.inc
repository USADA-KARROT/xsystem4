/* Include after live_slots()/call_method0(), before main().
 * Optional strict lifetime observation: forward-declare observer_probe_step()
 * and call it from the harness's existing probe_step(). No VM callbacks mocked.
 * Fixture owns one root each for observer, collection, motion and environment.
 * The production delegate constructor owns its receiver/environment refs;
 * teardown never repairs missing releases by hand. */
static int observer_fixture_active, observer_fixture_phase;
static int observer_fixture_env = -1;
static unsigned long observer_fixture_checked_steps, observer_fixture_setter_steps;

static void observer_probe_step(void)
{
    if (!observer_fixture_active) return;
    for (int i = 0; i < call_stack_ptr; i++) {
        if (call_stack[i].fno != 20752) continue;
        int slot = call_stack[i].page_slot;
        assert(slot > 0 && (size_t)slot < heap_size && HEAP_REF(slot) > 0);
        assert(heap[slot].type == VM_PAGE && heap[slot].page);
        assert(heap[slot].page->type == LOCAL_PAGE && heap[slot].page->index == 20752);
        assert(heap[slot].page->nr_vars == 1);
        observer_fixture_checked_steps++;
        if (call_stack_ptr && call_stack[call_stack_ptr-1].fno == 20748) {
            int value = observer_fixture_phase == 2 ? 1 : 0;
            assert(heap[slot].page->values[0].i == value);
            assert(heap[observer_fixture_env].page->values[2].i == value);
            observer_fixture_setter_steps++;
        }
    }
}

static int observer_fixture_ref(int slot)
{
    return slot > 0 && (size_t)slot < heap_size ? HEAP_REF(slot) : 0;
}

static int observer_fixture_array_size(int slot)
{
    assert(slot > 0 && heap_index_valid(slot) && heap[slot].type == VM_PAGE);
    const struct page *page = heap[slot].page;
    assert(!page || page->type == ARRAY_PAGE);
    return page ? page->nr_vars : 0;
}

static void observer_fixture_array_one(int array_slot, int type, int struc, int value)
{
    assert(heap_index_valid(array_slot) && heap[array_slot].type == VM_PAGE);
    struct page *old = heap[array_slot].page;
    assert(!old || (old->type == ARRAY_PAGE && old->nr_vars == 0));
    if (old) free_page(old);
    struct page *page = alloc_page(ARRAY_PAGE, type, 1);
    page->array.rank = 1;
    page->array.struct_type = struc;
    page->values[0].i = value;
    heap_ref(value); /* array's ownership, separate from existing owner */
    heap[array_slot].page = page;
}

static void test_delegate_ownership_smoke(void)
{
    size_t baseline = live_slots();
    int obj = heap_alloc_page(alloc_page(ARRAY_PAGE, AIN_INT, 0));
    int env = heap_alloc_page(alloc_page(ARRAY_PAGE, AIN_INT, 0));
    int src = heap_alloc_page(delegate_new_from_method_env(obj, 36081, env));
    assert(HEAP_REF(obj) == 2 && HEAP_REF(env) == 2);
    int copy = heap_alloc_page(copy_page(heap[src].page));
    assert(HEAP_REF(obj) == 3 && HEAP_REF(env) == 3);
    assert(heap[copy].page != heap[src].page);
    assert(heap[copy].page->values[0].i == obj && heap[copy].page->values[2].i == env);
    heap_set_page(src, delegate_clear(heap[src].page));
    heap_set_page(src, delegate_clear(heap[src].page));
    assert(HEAP_REF(obj) == 2 && HEAP_REF(env) == 2);
    int dst = heap_alloc_page(alloc_page(DELEGATE_PAGE, 0, 0));
    heap_set_page(dst, delegate_plusa(heap[dst].page, heap[copy].page));
    assert(heap[dst].page->values[2].i == env);
    assert(HEAP_REF(obj) == 3 && HEAP_REF(env) == 3);
    heap_set_page(dst, delegate_plusa(heap[dst].page, heap[copy].page));
    assert(HEAP_REF(obj) == 3 && HEAP_REF(env) == 3);
    delegate_erase(heap[dst].page, obj, 36081);
    assert(HEAP_REF(obj) == 2 && HEAP_REF(env) == 2);
    heap_set_page(dst, delegate_append(heap[dst].page, obj, 36081));
    assert(heap[dst].page->values[2].i == 0 && HEAP_REF(obj) == 3);
    heap_unref(dst); heap_unref(copy); heap_unref(src);
    assert(HEAP_REF(obj) == 1 && HEAP_REF(env) == 1);
    int alias = heap_alloc_page(delegate_new_from_method_env(obj, 36081, obj));
    assert(HEAP_REF(obj) == 3);
    heap_unref(alias);
    assert(HEAP_REF(obj) == 1);
    int version = ain->version;
    ain->version = 13;
    int old = heap_alloc_page(delegate_new_from_method_env(obj, 36081, env));
    assert(HEAP_REF(obj) == 1 && HEAP_REF(env) == 1);
    heap[old].page->values[2].i = env; /* sequence happens to match a live slot */
    int old_copy = heap_alloc_page(copy_page(heap[old].page));
    heap_set_page(old, delegate_clear(heap[old].page));
    heap_unref(old); heap_unref(old_copy);
    assert(HEAP_REF(obj) == 1 && HEAP_REF(env) == 1);
    ain->version = version;
    heap_unref(obj); heap_unref(env);
    assert(live_slots() == baseline);
    puts("DELEGATE ownership smoke PASS: create/copy/append/plusa/erase/clear/delete/alias/pre14");
}

static void test_observer_chain(void)
{
    static bool smoke_done;
    if (!smoke_done) { test_delegate_ownership_smoke(); smoke_done = true; }
    assert(ain->version == 14);
    assert(ain->structures[508].nr_members == 3);
    assert(ain->structures[611].nr_members == 10);
    assert(ain->structures[612].nr_members == 2);
    assert(ain->functions[27031].nr_vars == 4);
    assert(ain->functions[36081].nr_args == 2);
    assert(ain->functions[36081].vars[0].type.data == AIN_REF_BOOL);
    assert(ain->functions[36081].vars[1].type.data == AIN_VOID);
    assert(ain->globals[2].type.data == AIN_BOOL && ain->globals[3].type.data == AIN_INT);
    const size_t initial_live = live_slots();
    const int initial_sp = stack_ptr, initial_csp = call_stack_ptr;
    const unsigned long initial_callbacks = calls[36081];
    const unsigned long initial_endwait = calls[9197];

    int collection = alloc_struct(612);
    int motion = alloc_struct(611);
    int parts_shell = alloc_struct(389); /* IParts: no members, no virtual calls here */
    struct page *motion_page = heap[motion].page;
    assert(motion_page->values[1].i == -1);
    motion_page->values[1].i = parts_shell; /* transfer shell's only owner */
    motion_page->values[2].i = 0;
    motion_page->values[5].i = 0;
    // Explicit fixture inputs: v14 object fields start null until script NEW.
    assert(motion_page->values[0].i == -1);
    motion_page->values[0].i = alloc_struct(608); // transfer sole owner
    int motion_set = motion_page->values[0].i;
    assert(heap[motion_set].page->index == 608);
    assert(heap[motion_set].page->values[1].i == -1);
    heap[motion_set].page->values[1].i = alloc_struct(617); // transfer sole owner
    int section = heap[motion_set].page->values[1].i;
    assert(heap[section].page->index == 617);
    int name = heap_alloc_slot(VM_STRING);
    heap[name].s = make_string("probe", 5);
    variable_set(heap[section].page, 0, AIN_STRING, (union vm_value){.i=name});
    int motions = heap[collection].page->values[0].i;
    int names = heap[collection].page->values[1].i;
    observer_fixture_array_one(motions, AIN_ARRAY, 611, motion);
    observer_fixture_array_one(names, AIN_ARRAY_STRING, -1, name);

    int env = heap_alloc_page(alloc_page(LOCAL_PAGE, 27031, 4));
    for (int i = 0; i < 4; i++)
        heap[env].page->values[i] = variable_initval(ain->functions[27031].vars[i].type.data);
    heap[env].page->values[1].i = 0;
    heap[env].page->values[2].i = 0;
    assert(heap[env].page->values[3].i == -1); /* no InputDisabler ctor/UI */
    observer_fixture_array_one(heap[env].page->values[0].i, AIN_ARRAY_STRING, -1, name);
    int observer = alloc_struct(508);
    call_method0(20750, observer); /* real bytecode DG_NEW initializer */
    int notify = heap_alloc_page(delegate_new_from_method_env(collection, 36081, env));
    variable_set(heap[observer].page, 1, AIN_DELEGATE, (union vm_value){.i=notify});
    heap[observer].page->values[2].i = 0;
    heap[global_page_slot].page->values[2].i = 0;
    heap[global_page_slot].page->values[3].i = 12345;
    captured_slot = env;
    observer_slot = observer;
    observer_fixture_env = env;
    observer_fixture_checked_steps = observer_fixture_setter_steps = 0;
    observer_fixture_active = 1;
    observer_fixture_phase = 1;
    printf("OBSERVER seed live=%zu collection_ref=%d env_ref=%d motion_ref=%d\n",
           live_slots(), observer_fixture_ref(collection), observer_fixture_ref(env), observer_fixture_ref(motion));
    fflush(stdout);

    call_method0(20752, observer);
    assert(calls[36081] == initial_callbacks + 1);
    assert(heap[observer].page->values[2].i == 0);
    assert(heap[env].page->values[2].i == 0);
    assert(heap[global_page_slot].page->values[2].i == 0);
    assert(heap[global_page_slot].page->values[3].i == 12345);
    assert(observer_fixture_array_size(motions) == 1);
    assert(observer_fixture_array_size(names) == 1);
    assert(calls[9197] == initial_endwait);
    printf("OBSERVER false live=%zu env_ref=%d motion_ref=%d\n", live_slots(), observer_fixture_ref(env), observer_fixture_ref(motion));
    fflush(stdout);

    heap[motion].page->values[5].i = 1; /* controlled completion state, not callback replacement */
    observer_fixture_phase = 2;
    call_method0(20752, observer);
    assert(calls[36081] == initial_callbacks + 2);
    assert(heap[observer].page->values[2].i == 1);
    assert(heap[env].page->values[2].i == 1);
    assert(heap[global_page_slot].page->values[2].i == 1);
    assert(heap[global_page_slot].page->values[3].i == 0);
    assert(observer_fixture_array_size(motions) == 0);
    assert(observer_fixture_array_size(names) == 0);
    assert(calls[9197] == initial_endwait + 1);
    printf("OBSERVER true live=%zu env_ref=%d motion_ref=%d caller_checks=%lu setter_checks=%lu\n",
           live_slots(), observer_fixture_ref(env), observer_fixture_ref(motion),
           observer_fixture_checked_steps, observer_fixture_setter_steps);
    fflush(stdout);

    observer_fixture_phase = 3;
    call_method0(20752, observer);
    assert(calls[36081] == initial_callbacks + 2);
    assert(calls[9197] == initial_endwait + 1);
    assert(stack_ptr == initial_sp && call_stack_ptr == initial_csp);
    observer_fixture_active = 0;
    heap_unref(observer);
    heap_unref(motion);
    heap_unref(collection);
    heap_unref(env);
    printf("OBSERVER teardown initial_live=%zu final_live=%zu collection_ref=%d env_ref=%d motion_ref=%d callback_calls=%lu\n",
           initial_live, live_slots(), observer_fixture_ref(collection), observer_fixture_ref(env),
           observer_fixture_ref(motion), calls[36081] - initial_callbacks);
    fflush(stdout);
    if (initial_live == 0 && live_slots() != 0) {
        for (size_t i=2;i<heap_size;i++) if (HEAP_REF(i)>0) {
            struct page *p = heap[i].type==VM_PAGE ? heap[i].page : NULL;
            fprintf(stderr,"OBSERVER residual slot=%zu ref=%d type=%d page_type=%d index=%d vars=%d\n",i,HEAP_REF(i),heap[i].type,p?p->type:-1,p?p->index:-1,p?p->nr_vars:-1);
        }
    }
    /* Intentionally do not release any orphaned delegate-owned refs here. */
}
