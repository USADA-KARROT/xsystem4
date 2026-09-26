#include <assert.h>
#include <stdint.h>
static void probe_entry(int fno);
static void probe_return(int fno, int page);
static void probe_step(unsigned op);
static void observer_probe_step(void);
#include "instrumented-vm.inc"
#include FFI_SOURCE

static unsigned long calls[40000], returns[40000], steps[40000], total_steps;
static int captured_slot=-1, observer_slot=-1;
static size_t live_slots(void);

static const char *review_phase="initial";
void review_heap_event(const char *action, int slot) {
    if(!heap || slot<=1 || (size_t)slot>=heap_size || HEAP_REF(slot)<=0) return;
    struct page *page=heap[slot].type==VM_PAGE?heap[slot].page:NULL;
    if(heap[slot].type!=VM_STRING && (!page || (page->index!=617 && page->type!=DELEGATE_PAGE)))return;
    int fno=call_stack_ptr?call_stack[call_stack_ptr-1].fno:-1;
    fprintf(stderr,"OWN phase=%s action=%s slot=%d seq=%u ref=%d temp=%d kind=%d page=%d index=%d vars=%d fno=%d ip=%zx op=%s sp=%d\n",
       review_phase,action,slot,heap[slot].seq,HEAP_REF(slot),!!(heap[slot].ref&HEAP_TEMP_FLAG),heap[slot].type,
       page?page->type:-1,page?page->index:-1,page?page->nr_vars:-1,fno,instr_ptr,
       instr_ptr<ain->code_size?instructions[get_opcode(instr_ptr)].name:"none",stack_ptr);
}
static void review_checkpoint(const char *phase) {
    review_phase=phase;fprintf(stderr,"CHECKPOINT phase=%s live=%zu\n",phase,live_slots());
    for(int i=2;i<256 && i<(int)heap_size;i++)if(HEAP_REF(i)>0){
        struct page*p=heap[i].type==VM_PAGE?heap[i].page:NULL;
        if(heap[i].type==VM_STRING||(p&&(p->index==617||p->type==DELEGATE_PAGE)))
            fprintf(stderr,"STATE slot=%d seq=%u ref=%d kind=%d page=%d index=%d vars=%d\n",i,heap[i].seq,HEAP_REF(i),heap[i].type,p?p->type:-1,p?p->index:-1,p?p->nr_vars:-1);
    }
}
static int clock_ms;
static int probe_clock(void) { return clock_ms; }
static void probe_entry(int fno) { if(fno>=0 && fno<40000)calls[fno]++; }
static void probe_return(int fno, int page) {
    (void)page;
    if(fno>=0 && fno<40000)returns[fno]++;
}
static void probe_step(unsigned op) {
    (void)op;
    if(++total_steps > 1000000) { fprintf(stderr,"PROBE instruction budget exceeded\n");exit(90); }
    if(call_stack_ptr>0)steps[call_stack[call_stack_ptr-1].fno]++;
    observer_probe_step();

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
    // Link the production services without unrelated save/UI module startup.
    link_libraries();
}
#include "observer_fixture.inc"
#include "xassign_contract.inc"
int main(int argc,char **argv) {
    assert(argc==3);init_probe(argv[1]);
    printf("PROBE initialized real VM/AIN, %d functions, live slots %zu\n",ain->nr_functions,live_slots());
    fflush(stdout);
    if(!strcmp(argv[2],"assign")) return test_xassign_contract();
    if(!strcmp(argv[2],"observer")) {int rounds=getenv("REVIEW_ROUNDS")?atoi(getenv("REVIEW_ROUNDS")):1;for(int i=0;i<rounds;i++)test_observer_chain();return live_slots()==0?0:78;}
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
    if(!strcmp(argv[2],"metadata")) {
        puts("METADATA TYPE PASS four original constructor arrays");
        for(int i=0;i<4;i++)printf("METADATA array_ref[%d]=%d\n",i,HEAP_REF(heap[manager].page->values[i].i));
        heap[global_page_slot].page->values[15].i=-1;heap_unref(manager);
        printf("METADATA teardown live=%zu (expected 0)\n",live_slots());
        return live_slots()==0?0:79;
    }
    const size_t manager_baseline=live_slots();
    for(int round=0;round<20;round++) {
        int a=alloc_struct(7);clock_ms=1000+round*1000;call_method0(420,a);
        assert(heap[a].page->values[0].i==0);
        int flags=heap[manager].page->values[1].i;
        int imps=heap[manager].page->values[0].i;
        assert(heap[flags].page && heap[flags].page->values[0].i==1);
        clock_ms+=7;call_method0(424,a);int elapsed7=stack_pop().i;printf("TIMER clock round=%d got=%d expected=7\n",round,elapsed7);fflush(stdout);assert(elapsed7==7);
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
