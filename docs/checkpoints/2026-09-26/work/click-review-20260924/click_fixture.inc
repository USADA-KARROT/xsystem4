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

static void test_click_chain(int manager)
{
    const size_t baseline = live_slots();
    const unsigned long ctor = calls[420], dtor = calls[422];
    clock_ms = 1000; probe_key_down = 0;
    unsigned site = 0;
    for (int f = 0; f < ain->nr_functions && !site; f++) {
        size_t ip = ain->functions[f].address;
        for (int n = 0; n < 5000 && ip < ain->code_size && ain->code_size - ip >= 2; n++) {
            unsigned op = LittleEndian_getW(ain->code, ip);
            if (op >= NR_OPCODES) break;
            unsigned width = instruction_width(op);
            if (!width || width > ain->code_size - ip || op == ENDFUNC || op == FUNC || op == EOF) break;
            if (op == NEW && LittleEndian_getDW(ain->code, ip+2) == 233) {site=ip;break;}
            ip += width;
        }
    }
    assert(site);
    fprintf(stderr,"CLICK REAL NEW site=0x%x structflags=%d\n",site,struct_flags[233]);
    int ctor_fno=LittleEndian_getDW(ain->code,site+6);
    int ctor_args=ctor_fno<0?0:ain->functions[ctor_fno].nr_args;
    fprintf(stderr,"CLICK REAL NEW ctor=%d args=%d\n",ctor_fno,ctor_args);
    assert(ctor_args==0 || ctor_args==2);
    if(ctor_args==2){stack_push(65);stack_push(0);}
    size_t saved_ip=instr_ptr;
    instr_ptr=site;
    execute_instruction(NEW);
    int click=stack_pop().i;
    instr_ptr=saved_ip;
    fprintf(stderr,"CLICK NEW DONE parent=%d child=%d ctor=%lu dtor=%lu\n",click,heap[click].page->values[7].i,calls[420],calls[422]);
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
