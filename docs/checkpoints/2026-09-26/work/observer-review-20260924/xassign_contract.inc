/* Seeded operand tests execute existing production handlers at original AIN
 * instruction sites. They are not complete AIN function execution. */
static unsigned review_site(enum opcode op)
{
    unsigned ip=ain->functions[20750].address;
    for(int n=0;n<100;n++) {
        enum opcode found=get_opcode(ip);
        if(found==op)return ip;
        assert(found!=RETURN); ip+=instruction_width(found);
    }
    abort();
}
static void review_opcode(enum opcode op)
{
    size_t saved=instr_ptr;
    /* SP_INC has no operands; this helper does not decode an AIN argument. */
    instr_ptr=op==SP_INC?ain->functions[26981].address:review_site(op);
    execute_instruction(op);instr_ptr=saved;
}
static int test_xassign_contract(void)
{
    size_t baseline=live_slots(); int sp=stack_ptr;
    review_phase="owned-DG_NEW-X_ASSIGN-POP";
    int holder=alloc_struct(508);
    stack_push(holder);stack_push(0);review_opcode(DG_NEW);
    int owned=stack_peek(0).i;
    review_opcode(X_ASSIGN);review_opcode(POP);
    int owned_after=HEAP_REF(owned);
    heap_unref(holder);int owned_cleanup=HEAP_REF(owned);
    printf("CONTRACT owned after_assign=%d after_holder_cleanup=%d expected=1,0\n",owned_after,owned_cleanup);

    review_phase="borrowed-X_ASSIGN-SP_INC";
    int borrowed=heap_alloc_page(alloc_page(DELEGATE_PAGE,0,0));
    holder=alloc_struct(508);
    stack_push(holder);stack_push(0);stack_push(borrowed);
    review_opcode(X_ASSIGN);review_opcode(SP_INC);
    int borrowed_after=HEAP_REF(borrowed);
    heap_unref(holder);int borrowed_holder_cleanup=HEAP_REF(borrowed);
    heap_unref(borrowed);
    printf("CONTRACT borrowed after_sp_inc=%d after_holder_cleanup=%d expected=2,1\n",borrowed_after,borrowed_holder_cleanup);

    review_phase="shared-old-DELETE-X_ASSIGN-null";
    int old=heap_alloc_page(alloc_page(DELEGATE_PAGE,0,0));
    holder=alloc_struct(508);heap[holder].page->values[0].i=old;heap_ref(old);
    stack_push(old);review_opcode(DELETE); /* compiler releases holder's old owner */
    stack_push(holder);stack_push(0);stack_push(-1);
    review_opcode(X_ASSIGN);review_opcode(POP);
    int old_after=HEAP_REF(old);
    heap_unref(holder);heap_unref(old);
    printf("CONTRACT shared_old after_delete_assign=%d expected=1\n",old_after);
    assert(stack_ptr==sp && call_stack_ptr==0);
    bool passed=owned_after==1 && owned_cleanup==0 && borrowed_after==2
        && borrowed_holder_cleanup==1 && old_after==1 && live_slots()==baseline;
    printf("CONTRACT result=%s live=%zu baseline=%zu\n",passed?"pass":"fail",live_slots(),baseline);
    return passed?0:81;
}
