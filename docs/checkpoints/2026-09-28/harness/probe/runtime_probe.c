#include <assert.h>
#include <stdint.h>
static void probe_entry(int fno);
static void probe_return(int fno, int page);
static void probe_step(unsigned op);
static void observer_probe_step(void);
static void delegate_reentrancy_probe_step(void);
static int probe_key_is_down(int key);
static void personality_probe_step(void);
static void iface_arg_probe_step(void);
static void title_review_probe_step(void);
static void third_review_probe_step(void);
static void working_cards_probe_step(void);
static void option_array_probe_step(void);
static void array_literal_probe_step(void);
static void global_init_probe_step(void);
static void global_init_probe_return(int fno);
static void delegate_cursor_probe_step(void);
#include "instrumented-vm.inc"
#include FFI_SOURCE

static unsigned long calls[40000], returns[40000], steps[40000], total_steps;
static unsigned long probe_step_budget=1000000; // a mode that runs the whole alloc function raises it
static int captured_slot=-1, observer_slot=-1;
static int clock_ms;
static int probe_clock(void) { return clock_ms; }
static struct string *probe_system_error(struct string *message) {
    (void)message;
    fprintf(stderr,"PROBE unexpected system.Error in fno=%d\n",call_stack_ptr?call_stack[call_stack_ptr-1].fno:-1);
    exit(93);
}
static void probe_entry(int fno) { if(fno>=0 && fno<40000)calls[fno]++; }
static void probe_return(int fno, int page) {
    (void)page;
    if(fno>=0 && fno<40000)returns[fno]++;
    global_init_probe_return(fno);
}
static void probe_step(unsigned op) {
    (void)op;
    if(++total_steps > probe_step_budget) { fprintf(stderr,"PROBE instruction budget exceeded\n");exit(90); }
    if(call_stack_ptr>0)steps[call_stack[call_stack_ptr-1].fno]++;
    observer_probe_step();
    delegate_reentrancy_probe_step();
    personality_probe_step();
    iface_arg_probe_step();
    title_review_probe_step();
    third_review_probe_step();
    working_cards_probe_step();
    option_array_probe_step();
    array_literal_probe_step();
    global_init_probe_step();
    delegate_cursor_probe_step();
}
static size_t live_slots(void) {
    size_t n=0;for(size_t i=2;i<heap_size;i++)if(HEAP_REF(i)>0)n++;return n;
}
static void call_method0(int fno,int object) {
    int sp=stack_ptr,csp=call_stack_ptr;
    vm_call(fno,object);
    assert(call_stack_ptr==csp);
    assert(stack_ptr==sp+ain_return_slots_type(&ain->functions[fno].return_type));
}
static void init_probe(const char *path) {
    int error=0;ain=ain_open(path,&error);assert(ain && ain->version==14);
    initialize_instructions(ain->version);
    stack_size=INITIAL_STACK_SIZE;stack=xcalloc(stack_size,sizeof(*stack));
    for(int i=0;i<ain->nr_strings;i++)if(ain->strings[i])ain->strings[i]->ref++;
    for(int i=0;i<ain->nr_messages;i++)if(ain->messages[i])ain->messages[i]->ref++;
    heap_init();init_func_flags();
    // Experiment: exercise original timer scripts, not the legacy native timer
    // substitution. No bytecode/function bodies are replaced.
    for(int i=0;i<ain->nr_functions;i++)func_flags[i]&=~(FUNC_FLAG_CASTIMER_MGR|FUNC_FLAG_CASTIMER_INST);
    heap[0].ref=heap[global_page_slot].ref=1;
    heap[0].type=heap[global_page_slot].type=VM_PAGE;
    heap[0].seq=heap_next_seq++;heap[global_page_slot].seq=heap_next_seq++;
    heap[0].page=alloc_page(ARRAY_PAGE,AIN_VOID,64);
    heap[global_page_slot].page=alloc_page(GLOBAL_PAGE,0,ain->nr_globals);
    for(int i=0;i<ain->nr_globals;i++)heap[global_page_slot].page->values[i].i=-1;
    instr_ptr=VM_RETURN;
    extern struct static_library lib_system;
    static_library_replace(&lib_system,"GetTime",probe_clock);
    static_library_replace(&lib_system,"Error",probe_system_error);
    extern struct static_library lib_IbisInputEngine;
    static_library_replace(&lib_IbisInputEngine,"Key_IsDown",probe_key_is_down);
    // Link the production services without unrelated save/UI module startup.
    link_libraries();
}
#include "observer_fixture.inc"
#include "delegate_reentrancy_fixture.inc"
#include "click_fixture.inc"
#include "xassign_contract.inc"
#include "heap_reuse_fixture.inc"
#include "personality_fixture.inc"
#include "first_overload_fixture.inc"
#include "array_overload_fixture.inc"
#include "bound_overload_fixture.inc"
#include "findlast_fixture.inc"
#include "order_fixture.inc"
#include "unique_fixture.inc"
#include "fill_copy_fixture.inc"
#include "fill_copy_extra_fixture.inc"
#include "math_fixture.inc"
#include "sort_fixture.inc"
#include "string_fixture.inc"
#include "gbk_chars_fixture.inc"
#include "cif_fixture.inc"
#include "activity_text_fixture.inc"
#include "dialogue_model_fixture.inc"
#include "dialogue_copy_fixture.inc"
#include "save_persist_fixture.inc"
#include "save_fixes_fixture.inc"
#include "parts_reverse_fixture.inc"
#include "reverse_inherit_fixture.inc"
#include "iface_arg_fixture.inc"
#include "icast_fixture.inc"
#include "shuffle_fixture.inc"
#include "delegate_args_fixture.inc"
#include "text_metrics_fixture.inc"
#include "base_ui_fixture.inc"
#include "base_ui_review_fixture.inc"
#include "frame_pacing_fixture.inc"
#include "title_review_fixture.inc"
#include "third_review_fixture.inc"
#include "mojibake_fixture.inc"
#include "logo_gloss_fixture.inc"
#include "clip_area_fixture.inc"
#include "input_nesting_fixture.inc"
#include "gauge_fixture.inc"
#include "working_cards_fixture.inc"
#include "alpha_inherit_fixture.inc"
#include "text_default_fixture.inc"
#include "layout_box_fixture.inc"
#include "construction_fixture.inc"
#include "free_box_fixture.inc"
#include "text_size_fixture.inc"
#include "click_permission_fixture.inc"
#include "panel_fixture.inc"
#include "option_array_fixture.inc"
#include "ex_name_list_fixture.inc"
#include "option_return_fixture.inc"
#include "array_literal_fixture.inc"
#include "arg_ownership_fixture.inc"
#include "global_init_fixture.inc"
#include "controller_id_fixture.inc"
#include "layer_root_fixture.inc"
#include "delegate_cursor_fixture.inc"
#include "aref_value_fixture.inc"
#include "parts_transform_fixture.inc"
#include "construction_cmds_fixture.inc"
#include "numeral_fixture.inc"
#include "option_member_fixture.inc"
#include "backlog_text_fixture.inc"
#include "key_wait_fixture.inc"
#include "construction_cg_fixture.inc"
#include "../deleted_event_fixture.inc"
int main(int argc,char **argv) {
    assert(argc==3);init_probe(argv[1]);
    gk_probe_enable_from_env();
    printf("PROBE initialized real VM/AIN, %d functions, live slots %zu\n",ain->nr_functions,live_slots());
    fflush(stdout);
    if(!strcmp(argv[2],"personality-literal")) {test_personality_literal();return 0;}
    if(!strcmp(argv[2],"personality-caller")) {test_personality_caller_owned();return 0;}
    if(!strcmp(argv[2],"personality")) {test_personality_literal();test_personality_caller_owned();return 0;}
    if(!strcmp(argv[2],"first-overload"))return test_first_overload();
    if(!strcmp(argv[2],"overload-shapes"))return test_overload_shapes();
    if(!strcmp(argv[2],"bound-overload"))return test_bound_overload();
    if(!strcmp(argv[2],"findlast"))return test_findlast();
    if(!strcmp(argv[2],"order"))return test_order();
    if(!strcmp(argv[2],"unique"))return test_unique();
    if(!strncmp(argv[2],"fc-",3))return test_fill_copy(argv[2]);
    if(!strcmp(argv[2],"fill-copy-extra"))return test_fill_copy_extra();
    if(!strcmp(argv[2],"math"))return test_math();
    if(!strcmp(argv[2],"sort"))return test_sort();
    if(!strcmp(argv[2],"string"))return test_string();
    if(!strcmp(argv[2],"gbk-string"))return test_gbk_string();
    if(!strcmp(argv[2],"gbk-vm"))return test_gbk_vm();
    if(!strcmp(argv[2],"gbk-detect"))return test_gbk_detect();
    if(!strcmp(argv[2],"sjis-chars"))return test_sjis_chars();
    if(!strcmp(argv[2],"cif"))return test_cif();
    if(!strcmp(argv[2],"activity-text"))return test_activity_text();
    if(!strcmp(argv[2],"dialogue-model"))return test_dialogue_model();
    if(!strcmp(argv[2],"dialogue-copy"))return test_dialogue_copy();
    if(!strcmp(argv[2],"save-list"))return test_save_list();
    if(!strcmp(argv[2],"save-roundtrip"))return test_save_roundtrip();
    if(!strcmp(argv[2],"save-comment"))return test_save_comment();
    if(!strcmp(argv[2],"save-fixes"))return test_save_fixes();
    if(!strcmp(argv[2],"parts-reverse"))return test_parts_reverse();
    if(!strcmp(argv[2],"reverse-inherit"))return test_reverse_inherit();
    if(!strcmp(argv[2],"iface-arg"))return test_iface_arg();
    if(!strcmp(argv[2],"icast"))return test_icast();
    if(!strcmp(argv[2],"shuffle"))return test_shuffle();
    if(!strcmp(argv[2],"delegate-args"))return test_delegate_args();
    if(!strcmp(argv[2],"text-metrics"))return test_text_metrics();
    if(!strcmp(argv[2],"base-ui"))return test_base_ui();
    if(!strcmp(argv[2],"base-ui-review"))return test_base_ui_review();
    if(!strcmp(argv[2],"frame-pacing"))return test_frame_pacing();
    if(!strcmp(argv[2],"title-review"))return test_title_review();
    if(!strcmp(argv[2],"third-review"))return test_third_review();
    if(!strcmp(argv[2],"mojibake"))return test_mojibake();
    if(!strcmp(argv[2],"logo-gloss"))return test_logo_gloss();
    if(!strcmp(argv[2],"clip-area"))return test_clip_area();
    if(!strcmp(argv[2],"input-nesting"))return test_input_nesting();
    if(!strcmp(argv[2],"gauge"))return test_gauge();
    if(!strcmp(argv[2],"working-cards"))return test_working_cards();
    if(!strcmp(argv[2],"alpha-inherit"))return test_alpha_inherit();
    if(!strcmp(argv[2],"text-default"))return test_text_default();
    if(!strcmp(argv[2],"layout-box"))return test_layout_box();
    if(!strcmp(argv[2],"construction"))return test_construction();
    if(!strcmp(argv[2],"free-box"))return test_free_box();
    if(!strcmp(argv[2],"text-size"))return test_text_size();
    if(!strcmp(argv[2],"click-permission"))return test_click_permission();
    if(!strcmp(argv[2],"panel"))return test_panel();
    if(!strcmp(argv[2],"option-array"))return test_option_array();
    if(!strcmp(argv[2],"ex-name-list"))return test_ex_name_list();
    if(!strcmp(argv[2],"option-return"))return test_option_return();
    if(!strcmp(argv[2],"array-literal"))return test_array_literal();
    if(!strcmp(argv[2],"arg-ownership"))return test_arg_ownership();
    if(!strcmp(argv[2],"global-init"))return test_global_init();
    if(!strcmp(argv[2],"controller-id"))return test_controller_id();
    if(!strcmp(argv[2],"layer-root"))return test_layer_root();
    if(!strcmp(argv[2],"delegate-cursor"))return test_delegate_cursor();
    if(!strcmp(argv[2],"aref-value"))return test_aref_value();
    if(!strcmp(argv[2],"parts-transform"))return test_parts_transform();
    if(!strcmp(argv[2],"construction-cmds"))return test_construction_cmds();
    if(!strcmp(argv[2],"numeral"))return test_numeral();
    if(!strcmp(argv[2],"option-member"))return test_option_member();
    if(!strcmp(argv[2],"backlog-text"))return test_backlog_text();
    if(!strcmp(argv[2],"key-wait"))return test_key_wait();
    if(!strcmp(argv[2],"construction-cg"))return test_construction_cg();
    if(!strcmp(argv[2],"save-seed"))return test_save_seed();
    if(!strcmp(argv[2],"save-localgame"))return test_save_localgame();
    if(!strcmp(argv[2],"overload-shapes-str"))return test_overload_shapes_str();
    if(!strcmp(argv[2],"deleted-event")) {test_deleted_event();return 0;}
    if(!strcmp(argv[2],"heap-reuse")) {test_heap_reuse();return 0;}
    if(!strcmp(argv[2],"assignment"))return test_xassign_contract();
    if(!strcmp(argv[2],"observer")) {for(int i=0;i<20;i++)test_observer_chain();return live_slots()==0?0:78;}
    if(!strcmp(argv[2],"reentrancy")) {for(int i=0;i<20;i++)test_delegate_reentrancy();return 0;}
    int manager=alloc_struct(9);heap[global_page_slot].page->values[15].i=manager;
    call_method0(22325,manager);
    const int expected_types[]={AIN_ARRAY_STRUCT,AIN_ARRAY_INT,AIN_ARRAY_STRUCT,AIN_ARRAY_STRUCT};
    const int expected_structs[]={8,-1,531,531};
    for(int i=0;i<4;i++) {
        int slot=heap[manager].page->values[i].i;
        assert(slot>0 && heap_index_valid(slot) && heap[slot].page);
        struct page *array=heap[slot].page;
        assert(array->type==ARRAY_PAGE && array->a_type==expected_types[i]);
        assert(array->array.struct_type==expected_structs[i] && array->nr_vars==0 && array->array.rank==1);
        printf("METADATA member=%d type=%d struct=%d size=%d\n",i,array->a_type,array->array.struct_type,array->nr_vars);
    }
    if(!strcmp(argv[2],"array-reinit")) {
        int held[4];unsigned seq[4];
        for(int i=0;i<4;i++) {held[i]=heap[manager].page->values[i].i;seq[i]=heap[held[i]].seq;heap_ref(held[i]);}
        call_method0(22325,manager);
        for(int i=0;i<4;i++) {
            printf("REINIT held[%d]=%d ref=%d seq=%u original_seq=%u\n",i,held[i],HEAP_REF(held[i]),heap[held[i]].seq,seq[i]);fflush(stdout);
            assert(HEAP_REF(held[i])==1 && heap[held[i]].seq==seq[i] && heap[held[i]].page);
            assert(heap[held[i]].page->a_type==expected_types[i]);
            assert(heap[manager].page->values[i].i!=held[i]);
        }
        heap[global_page_slot].page->values[15].i=-1;heap_unref(manager);
        for(int i=0;i<4;i++)heap_unref(held[i]);
        assert(live_slots()==0);puts("REINIT PASS shared old arrays retain their external owner after original constructor reinitialization");return 0;
    }
    if(!strcmp(argv[2],"metadata")) {
        puts("METADATA TYPE PASS four original constructor arrays");
        for(int i=0;i<4;i++)printf("METADATA array_ref[%d]=%d\n",i,HEAP_REF(heap[manager].page->values[i].i));
        heap[global_page_slot].page->values[15].i=-1;heap_unref(manager);
        printf("METADATA teardown live=%zu (expected 0)\n",live_slots());
        return live_slots()==0?0:79;
    }
    if(!strcmp(argv[2],"click")) {test_click_chain(manager);heap[global_page_slot].page->values[15].i=-1;heap_unref(manager);assert(live_slots()==0);return 0;}
    const size_t manager_baseline=live_slots();
    for(int round=0;round<20;round++) {
        int a=alloc_struct(7);clock_ms=1000+round*1000;call_method0(420,a);
        assert(heap[a].page->values[0].i==0);
        int flags=heap[manager].page->values[1].i;
        int imps=heap[manager].page->values[0].i;
        assert(heap[flags].page && heap[flags].page->values[0].i==1);
        clock_ms+=7;call_method0(424,a);assert(stack_pop().i==7);
        clock_ms+=3;call_method0(424,a);assert(stack_pop().i==10);
        int b=alloc_struct(7);call_method0(420,b);assert(heap[b].page->values[0].i==1);
        unsigned long destroyed=calls[422];heap_unref(a);assert(calls[422]==destroyed+1);
        if(!heap[flags].page || HEAP_REF(flags)<=0) {
            printf("TIMER FAIL after first dtor: flags owner lost; ctor=%lu dtor=%lu flags_ref=%d; clock 7/10 ms previously passed\n",calls[420],calls[422],HEAP_REF(flags));fflush(stdout);return 77;
        }
        assert(heap[flags].page->nr_vars==2 && heap[flags].page->values[0].i==0 && heap[flags].page->values[1].i==1);
        int c=alloc_struct(7);call_method0(420,c);assert(heap[c].page->values[0].i==0);
        assert(heap[flags].page->nr_vars==2);
        call_method0(423,c);clock_ms+=5;call_method0(424,c);assert(stack_pop().i==5);
        heap_unref(c);heap_unref(b);assert(calls[422]==destroyed+3);
        assert(observer_fixture_array_size(flags)==0 && observer_fixture_array_size(imps)==0);
        printf("TIMER round=%d ctor=%lu dtor=%lu live=%zu baseline=%zu\n",round,calls[420],calls[422],live_slots(),manager_baseline);fflush(stdout);
        if(live_slots()!=manager_baseline) {puts("TIMER FAIL lifecycle behavior passed first round; remaining heap slots exceed baseline");return 77;}
    }
    heap[global_page_slot].page->values[15].i=-1;heap_unref(manager);
    assert(live_slots()==0);puts("TIMER PASS 60 constructors/destructors; clock, reset, hole reuse, GC, teardown live=0");
    return 0;
}
