/* Actual CASClick bytecode with only host key/clock services controlled. */
static int probe_key_down;
static int probe_key_is_down(int key) { assert(key == 65); return probe_key_down; }

static void call_method_args(int fno, int object, int nargs, const int *args)
{
    const int sp = stack_ptr, csp = call_stack_ptr;
    const size_t saved_ip = instr_ptr;
    assert(ain->functions[fno].nr_args == nargs);
    stack_push(object);
    for (int i = 0; i < nargs; i++) stack_push(args[i]);
    method_call(fno, VM_RETURN);
    vm_execute();
    instr_ptr = saved_ip;
    assert(call_stack_ptr == csp);
    assert(stack_ptr == sp + ain_return_slots_type(&ain->functions[fno].return_type));
}

static int click_check(int click, int time, int down)
{
    clock_ms = time; probe_key_down = down;
    int object = 42;
    call_method_args(6374, click, 1, &object);
    return stack_pop().i;
}

static int new_object_from_ain(int structure)
{
    // Execute a real NEW instruction with the original operands, rather than
    // bypassing NEW's construction path with a direct host-side ctor call.
    size_t saved_ip=instr_ptr;int sp=stack_ptr;
    for(unsigned ip=0;ip+10<=ain->code_size;) {
        enum opcode op=get_opcode(ip)&~OPTYPE_MASK;
        assert(op>=0 && op<NR_OPCODES);
        int width=instruction_width(op);assert(width>0);
        if(op==NEW && LittleEndian_getDW(ain->code,ip+2)==structure
                   && LittleEndian_getDW(ain->code,ip+6)==-1) {
            instr_ptr=ip;execute_instruction(NEW);instr_ptr=saved_ip;
            assert(stack_ptr==sp+1);return stack_pop().i;
        }
        ip+=width;
    }
    abort();
}

static void test_click_chain(int manager)
{
    const size_t baseline = live_slots();
    const unsigned long ctor = calls[420], dtor = calls[422];
    clock_ms = 1000; probe_key_down = 0;
    int click = new_object_from_ain(233);
    assert(calls[420] == ctor + 1);
    int args[] = {65, 0};
    call_method_args(6368, click, 2, args);
    assert(click_check(click, 1000, 0) == 0);
    assert(click_check(click, 1000, 1) == 1);
    assert(click_check(click, 1349, 1) == 0);
    assert(click_check(click, 1350, 1) == 1);
    assert(click_check(click, 1399, 1) == 0);
    assert(click_check(click, 1400, 1) == 1);
    assert(click_check(click, 1401, 0) == 0);
    heap_unref(click);
    assert(calls[422] == dtor + 1);
    assert(observer_fixture_array_size(heap[manager].page->values[0].i) == 0);
    assert(observer_fixture_array_size(heap[manager].page->values[1].i) == 0);
    assert(live_slots() == baseline);
    puts("CLICK PASS initial keydown, 350/50ms repeat boundaries, release, nested timer ctor/dtor exactly once");
}
