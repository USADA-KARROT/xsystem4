/* Real AIN DeletedEvent::add tail -> Match -> some/none. Seeded entry skips
 * only CParts FuncSet GUI lookup (entry..0x38b34c). No bytecode replacement,
 * no callback mock, no GUI ctor/HLL. CParts(354) and FunctionSet(327) stay
 * distinct. Inert handlers are real f25047; registration never invokes them.
 * Requires original lifetime probe helpers and production vm.c in same TU.
 */
static void deleted_event_fail(const char *why, int round, int slot)
{
    fprintf(stderr,"DELETED_EVENT FAIL round=%d reason=%s slot=%d valid=%d ref=%d seq=%u type=%d\n",
        round,why,slot,heap_index_valid(slot),
        slot>0&&(size_t)slot<heap_size?HEAP_REF(slot):-1,
        slot>0&&(size_t)slot<heap_size?heap[slot].seq:0,
        slot>0&&(size_t)slot<heap_size?heap[slot].type:-1);
    fflush(stderr);exit(86);
}
static void deleted_event_add_tail(int host,int set,int handler)
{
    int sp=stack_ptr,csp=call_stack_ptr;
    size_t saved_ip=instr_ptr;
    int local=_function_call(14312,VM_RETURN);
    assert(local>0 && heap[local].page->nr_vars==3);
    heap[local].page->values[0].i=handler;heap_ref(handler);
    variable_fini(heap[local].page->values[1],AIN_WRAP,true);
    heap[local].page->values[1].i=set;heap_ref(set);
    assert(heap[local].page->values[2].i==-1);
    set_struct_page(host);
    heap[local].page->local.struct_ptr=host;
    instr_ptr=0x38b34c;
    vm_execute();instr_ptr=saved_ip;
    assert(stack_ptr==sp && call_stack_ptr==csp);
}
static void test_deleted_event(void)
{
    assert(ain->functions[14312].address==0x38b276);
    assert(ain->functions[24980].address==0x51755e);
    assert(ain->structures[354].nr_members==3);
    assert(ain->structures[327].members[64].type.data==AIN_OPTION);
    assert(ain->structures[327].members[64].type.array_type->data==AIN_DELEGATE);
    const size_t baseline=live_slots();
    unsigned long some_before=calls[25144],none_before=calls[25145],match_before=calls[24980];
    int set=alloc_struct(327),host=alloc_struct(354),handlers[3],objects[3];
    heap[host].page->values[1].i=0;heap[host].page->values[2].i=0;
    variable_fini(heap[set].page->values[64],AIN_OPTION,true);
    heap[set].page->values[64].i=-1;heap[set].page->values[65].i=1;
    for(int i=0;i<3;i++) {
        objects[i]=alloc_struct(354);
        heap[objects[i]].page->values[1].i=i+1;
        heap[objects[i]].page->values[2].i=0;
        handlers[i]=heap_alloc_page(delegate_new_from_method_env(objects[i],25047,0));
    }
    int event=-1;unsigned event_seq=0;
    for(int round=0;round<3;round++) {
        deleted_event_add_tail(host,set,handlers[round]);
        int slot=heap[set].page->values[64].i;
        if(slot<=0||!heap_index_valid(slot)||heap[slot].type!=VM_PAGE||!heap[slot].page||heap[slot].page->type!=DELEGATE_PAGE)
            deleted_event_fail("event owner lost or wrong heap/page type",round,slot);
        if(round && (slot!=event||heap[slot].seq!=event_seq))
            deleted_event_fail("event slot identity changed",round,slot);
        event=slot;event_seq=heap[slot].seq;
        int count=delegate_numof(heap[slot].page);
        printf("DELETED_EVENT round=%d event=%d seq=%u ref=%d count=%d tag=%d some=%lu none=%lu\n",
          round,slot,event_seq,HEAP_REF(slot),count,heap[set].page->values[65].i,calls[25144]-some_before,calls[25145]-none_before);
        fflush(stdout);
        if(count!=round+1||HEAP_REF(slot)!=1||heap[set].page->values[65].i!=0)
            deleted_event_fail("count, sole field owner, or option tag",round,slot);
        /* Reuse pressure holds many new string slots simultaneously. None may
         * alias a live event or alter its delegate bytes/seq/type. */
        int pressure[128];
        for(int i=0;i<128;i++) {stack_push_string(make_string("pressure",8));pressure[i]=stack_pop().i;}
        if(!heap_index_valid(event)||heap[event].seq!=event_seq||heap[event].type!=VM_PAGE||delegate_numof(heap[event].page)!=round+1)
            deleted_event_fail("string allocation reused event owner",round,event);
        for(int i=0;i<128;i++)heap_unref(pressure[i]);
        free_deferred_strings();
    }
    assert(calls[24980]-match_before==3 && calls[25145]-none_before==1 && calls[25144]-some_before==2);
    heap_unref(set);
    for(int i=0;i<3;i++)heap_unref(handlers[i]);
    free_deferred_strings();
    fprintf(stderr,"DELETED_EVENT roots host=%d ref=%d live=%zu\n",host,HEAP_REF(host),live_slots());
    for(size_t i=2;i<heap_size;i++)if(HEAP_REF(i)>0) {
        fprintf(stderr,"DELETED_EVENT live slot=%zu ref=%d seq=%u type=%d",i,HEAP_REF(i),heap[i].seq,heap[i].type);
        if(heap[i].type==VM_PAGE && heap[i].page)fprintf(stderr," page_type=%d index=%d vars=%d",heap[i].page->type,heap[i].page->index,heap[i].page->nr_vars);
        fputc('\n',stderr);
    }
    /* UI constructor/destructor are outside this fixture. All event and
     * closure refs have already been released; these are harness roots only. */
    /* Relinquish each harness root exactly once even when production retained
     * extra owners. Do not repair cycles or free hidden owners in the test. */
    exit_unref(host);
    for(int i=0;i<3;i++)exit_unref(objects[i]);
    free_deferred_strings();
    printf("DELETED_EVENT teardown live=%zu baseline=%zu\n",live_slots(),baseline);fflush(stdout);
    if(live_slots()!=baseline) {
        fprintf(stderr,"DELETED_EVENT functional checks PASS; production ownership teardown remains incomplete\n");
        exit(87);
    }
    puts("DELETED_EVENT PASS original add tail/Match/some/none; 3 adds; 384 string allocations; balanced teardown");
}
