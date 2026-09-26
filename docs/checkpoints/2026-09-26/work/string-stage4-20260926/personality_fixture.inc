/* Optional integration fragment for the existing real-VM headless harness.
 * Not run by this review. Executes two original AIN instruction sites and the
 * complete real Personality constructor; no GUI, EX module or fake callback.
 * Requires init_probe(), live_slots(), and production vm.c in the same TU.
 */
static void test_personality_literal(void)
{
    const unsigned push_ip=0x6b9002, new_ip=0x6b9008;
    assert(get_opcode(push_ip)==S_PUSH && get_opcode(new_ip)==NEW);
    assert(LittleEndian_getDW(ain->code,new_ip+2)==706);
    assert(LittleEndian_getDW(ain->code,new_ip+6)==28149);
    int string_no=LittleEndian_getDW(ain->code,push_ip+2);
    struct string *literal=ain->strings[string_no];
    assert(literal && literal->size>0);
    int literal_ref=literal->ref;
    size_t baseline=live_slots(), saved_ip=instr_ptr;
    int sp=stack_ptr,csp=call_stack_ptr;
    for(int i=0;i<20;i++) {
        instr_ptr=push_ip;execute_instruction(S_PUSH);
        instr_ptr=new_ip;execute_instruction(NEW);
        assert(stack_ptr==sp+1 && call_stack_ptr==csp);
        int object=stack_pop().i;
        assert(heap_index_valid(object) && heap[object].type==VM_PAGE);
        assert(heap[object].page->type==STRUCT_PAGE && heap[object].page->index==706);
        int id=heap[object].page->values[0].i;
        assert(string_index_valid(id));
        struct string *stored=heap_get_string(id);
        assert(stored && stored->size==literal->size);
        assert(!memcmp(stored->text,literal->text,literal->size));
        heap_unref(object);free_deferred_strings();
        assert(stack_ptr==sp && call_stack_ptr==csp);
        assert(live_slots()==baseline && literal->ref==literal_ref);
    }
    instr_ptr=saved_ip;
    puts("PERSONALITY literal: 20 original NEW/ctor cycles, bytes preserved, heap/string refs balanced");
}
