/* Prepared, not yet compiled/run. Real CN AIN constructor; no GUI or EX.
 * Literal flow is the original S_PUSH/NEW. Caller-owned flow is a separate
 * controlled non-TEMP argument fixture, not a reconstructed game callsite. */
static bool personality_fixture_active;
static const char *personality_expected;
static int personality_expected_size;
static unsigned personality_entry_checks, personality_field_checks;

static void personality_expect_string(int slot)
{
    assert(slot > 1 && (size_t)slot < heap_size && HEAP_REF(slot) > 0);
    assert(heap[slot].type == VM_STRING && heap[slot].s);
    assert(heap[slot].s->ref > 0);
    assert(heap[slot].s->size == personality_expected_size);
    assert(!memcmp(heap[slot].s->text, personality_expected, personality_expected_size));
}

static void personality_expect_field(int object)
{
    assert(object > 1 && heap_index_valid(object) && HEAP_REF(object) > 0);
    assert(heap[object].type == VM_PAGE && heap[object].page);
    assert(heap[object].page->type == STRUCT_PAGE && heap[object].page->index == 706);
    assert(heap[object].page->nr_vars == 1);
    personality_expect_string(heap[object].page->values[0].i);
}

static void personality_probe_step(void)
{
    if (!personality_fixture_active || !call_stack_ptr) return;
    struct function_call *frame = &call_stack[call_stack_ptr-1];
    if (frame->fno != 28149) return;
    if (instr_ptr == ain->functions[28149].address) {
        int input = heap[frame->page_slot].page->values[0].i;
        personality_expect_string(input);
        personality_entry_checks++;
        printf("PERSONALITY entry input=%d seq=%u heap_ref=%d temp=%d bytes=%d\n",
               input, heap[input].seq, HEAP_REF(input),
               !!(heap[input].ref & HEAP_TEMP_FLAG), heap[input].s->size);
    }
    /* After real Id assignment and immediately before the real RETURN. */
    if (instr_ptr == 0x5c342c || instr_ptr == 0x5c345a) {
        personality_expect_string(heap[frame->page_slot].page->values[0].i);
        personality_expect_field(struct_page_slot());
        personality_field_checks++;
    }
}

static void personality_check_metadata(void)
{
    assert(ain->version == 14);
    assert(!strcmp(ain->functions[28149].name, "Personality@0"));
    assert(ain->functions[28149].address == 0x5c33f2);
    assert(ain->functions[28149].nr_args == 1 && ain->functions[28149].nr_vars == 1);
    assert(ain->functions[28149].vars[0].type.data == AIN_STRING);
    assert(ain->functions[28149].return_type.data == AIN_VOID);
    assert(ain->structures[706].nr_members == 1);
    assert(ain->structures[706].members[0].type.data == AIN_STRING);
    assert(!strcmp(ain->structures[706].members[0].name, "Id"));
    assert(get_opcode(0x5c342c) == POP && get_opcode(0x5c345a) == RETURN);
}

static void test_personality_literal(void)
{
    personality_check_metadata();
    const size_t push_ip = 0x6b9002, new_ip = 0x6b9008;
    assert(get_opcode(push_ip) == S_PUSH && get_opcode(new_ip) == NEW);
    assert(LittleEndian_getDW(ain->code, new_ip+2) == 706);
    assert(LittleEndian_getDW(ain->code, new_ip+6) == 28149);
    int string_no = LittleEndian_getDW(ain->code, push_ip+2);
    assert(string_no >= 0 && string_no < ain->nr_strings);
    struct string *literal = ain->strings[string_no];
    assert(literal && literal->size > 0);
    /* Keep golden bytes outside the VM, so a corrupt shared string cannot
     * make the expected value silently change along with the stored value. */
    char *golden = xmalloc(literal->size);
    memcpy(golden, literal->text, literal->size);
    personality_expected = golden;
    personality_expected_size = literal->size;
    int literal_ref = literal->ref;
    const size_t baseline = live_slots(), saved_ip = instr_ptr;
    const int sp = stack_ptr, csp = call_stack_ptr;
    const unsigned entries = personality_entry_checks, fields = personality_field_checks;
    personality_fixture_active = true;
    for (int i = 0; i < 20; i++) {
        instr_ptr = push_ip; execute_instruction(S_PUSH);
        instr_ptr = new_ip; execute_instruction(NEW);
        assert(stack_ptr == sp+1 && call_stack_ptr == csp);
        int object = stack_pop().i;
        personality_expect_field(object);
        /* Original S_PUSH/NEW transfers the argument. The original caller
         * does not DELETE the same string after NEW; do not invent one. */
        heap_unref(object);
        free_deferred_strings();
        assert(stack_ptr == sp && call_stack_ptr == csp);
        assert(live_slots() == baseline && literal->ref == literal_ref);
    }
    personality_fixture_active = false;
    assert(personality_entry_checks == entries+20 && personality_field_checks == fields+40);
    instr_ptr = saved_ip;
    free(golden);
    personality_expected = NULL;
    puts("PERSONALITY literal PASS: 20 original S_PUSH/NEW/constructor cycles; incoming Id, field bytes, heap and literal refs balanced");
}

static void test_personality_caller_owned(void)
{
    personality_check_metadata();
    static const char value[] = "stage4-personality-id";
    personality_expected = value;
    personality_expected_size = sizeof(value)-1;
    const size_t baseline = live_slots(), saved_ip = instr_ptr;
    const int sp = stack_ptr, csp = call_stack_ptr;
    const unsigned entries = personality_entry_checks, fields = personality_field_checks;
    assert(get_opcode(0x5c3414) == DELETE);
    personality_fixture_active = true;
    for (int i = 0; i < 20; i++) {
        int input = heap_alloc_slot(VM_STRING);
        heap[input].s = make_string(value, sizeof(value)-1);
        assert(HEAP_REF(input) == 1 && !(heap[input].ref & HEAP_TEMP_FLAG));
        const unsigned input_seq = heap[input].seq;
        int object = alloc_struct(706);
        call_method_args(28149, object, 1, &input);
        assert(stack_ptr == sp && call_stack_ptr == csp);
        personality_expect_string(input);
        assert(heap[input].seq == input_seq && HEAP_REF(input) == 1);
        personality_expect_field(object);
        /* Release the caller's one established non-TEMP owner using the
         * production DELETE opcode. No extra retain masks callee errors. */
        stack_push(input);
        instr_ptr = 0x5c3414; execute_instruction(DELETE);
        free_deferred_strings();
        personality_expect_field(object);
        heap_unref(object);
        free_deferred_strings();
        assert(stack_ptr == sp && call_stack_ptr == csp && live_slots() == baseline);
    }
    personality_fixture_active = false;
    assert(personality_entry_checks == entries+20 && personality_field_checks == fields+40);
    instr_ptr = saved_ip;
    personality_expected = NULL;
    puts("PERSONALITY caller-owned PASS: 20 real constructor calls; caller DELETE leaves Id bytes valid; teardown balanced");
}
