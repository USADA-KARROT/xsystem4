#include <assert.h>
#include <stdint.h>
static void probe_entry(int fno);
static void probe_return(int fno, int page);
static void probe_step(unsigned op);
static void observer_probe_step(void);
static void timer_review_heap(unsigned op);
static void timer_review_dump(const char *stage);
#include "instrumented-vm.inc"
#include FFI_SOURCE

static unsigned long calls[40000], returns[40000], steps[40000], total_steps;
static int captured_slot=-1, observer_slot=-1;
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
    timer_review_heap(op);

}
static size_t live_slots(void) {
    size_t n=0;for(size_t i=2;i<heap_size;i++)if(HEAP_REF(i)>0)n++;return n;
}

static int rv_refs[256], rv_type[256], rv_index[256], rv_nvars[256];
static uint32_t rv_seq[256];
static size_t rv_prev_ip;
static int rv_prev_f=-1;
static unsigned rv_prev_op;
static void timer_review_heap(unsigned op) {
    for(int i=2;i<256 && (size_t)i<heap_size;i++) {
        int ref=HEAP_REF(i),type=-1,index=-1,nvars=-1;
        if(ref>0 && heap[i].type==VM_PAGE && heap[i].page) {
            type=heap[i].page->type;index=heap[i].page->index;nvars=heap[i].page->nr_vars;
        }
        uint32_t seq=ref>0?heap[i].seq:0;
        if(ref!=rv_refs[i] || seq!=rv_seq[i] || (ref>0 && (type!=rv_type[i] || index!=rv_index[i] || nvars!=rv_nvars[i]))) {
            fprintf(stderr,"RV after f=%d ip=0x%zx op=%u slot=%d seq=%u ref=%d->%d page_type=%d index=%d nvars=%d\n",rv_prev_f,rv_prev_ip,rv_prev_op,i,seq,rv_refs[i],ref,type,index,nvars);
        }
        rv_refs[i]=ref;rv_seq[i]=seq;rv_type[i]=type;rv_index[i]=index;rv_nvars[i]=nvars;
    }
    rv_prev_ip=instr_ptr;rv_prev_op=op;rv_prev_f=call_stack_ptr?call_stack[call_stack_ptr-1].fno:-1;
}
static void timer_review_dump(const char *stage) {
    fprintf(stderr,"RV SNAPSHOT %s stack=%d calls=%d\n",stage,stack_ptr,call_stack_ptr);
    for(size_t i=2;i<heap_size;i++)if(HEAP_REF(i)>0) {
        fprintf(stderr,"  slot=%zu seq=%u ref=%d heaptype=%d",i,heap[i].seq,HEAP_REF(i),heap[i].type);
        if(heap[i].type==VM_PAGE && heap[i].page) {
            struct page *p=heap[i].page;
            fprintf(stderr," page_type=%d index=%d nvars=%d struct=%d rank=%d values=",p->type,p->index,p->nr_vars,p->array.struct_type,p->array.rank);
            for(int j=0;j<p->nr_vars && j<8;j++)fprintf(stderr,"%s%d",j?",":"",p->values[j].i);
        }else fprintf(stderr," page=null");
        fputc('\n',stderr);
    }
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
int main(int argc,char **argv) {
    assert(argc==3);init_probe(argv[1]);
    printf("PROBE initialized real VM/AIN, %d functions, live slots %zu\n",ain->nr_functions,live_slots());
    fflush(stdout);
    if(!strcmp(argv[2],"observer")) {for(int i=0;i<20;i++)test_observer_chain();return live_slots()==0?0:78;}
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
    timer_review_dump("manager baseline");
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
        if(live_slots()!=manager_baseline) {timer_review_dump("timer residual");puts("TIMER FAIL lifecycle behavior passed first round; remaining heap slots exceed baseline");return 77;}
    }
    heap[global_page_slot].page->values[15].i=-1;heap_unref(manager);
    assert(live_slots()==0);puts("TIMER PASS 60 constructors/destructors; clock, reset, hole reuse, GC, teardown live=0");
    return 0;
}
