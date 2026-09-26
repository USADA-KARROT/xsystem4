/* Isolated production extraction fixture; callback body is a controlled stub.
 * Does not execute AIN bytecode or replace the real VM / FFI integration tests. */
#define VM_PRIVATE
#include <assert.h>
#include <limits.h>
#include <math.h>
#include <setjmp.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ffi.h>
#include "system4/ain.h"
#include "system4/string.h"
#include "system4/instructions.h"
#include "vm.h"
#include "vm/heap.h"
#include "vm/page.h"
struct ain *ain;
static union vm_value fixture_stack[64];
union vm_value *stack = fixture_stack;
int32_t stack_ptr;
static struct vm_pointer fixture_heap[128];
struct vm_pointer *heap = fixture_heap;
size_t heap_size = 128;
int hll_current_arg3 = 1, hll_param_slot2, hll_func_obj = -1, hll_self_slot = -1;
static int checks, callbacks, cb_mode, cb_target, cb_fno, cb_nargs;
static jmp_buf error_jump;
static int error_active, errors_caught;
static char last_error[1024];
static int cb_first[64], cb_second[64];
static struct page *cb_array;
static int cb_start, cb_stride;
static int cb_mutation;
#define CHECK(x) do { checks++; if (!(x)) { fprintf(stderr,"FAIL line %d: %s\n",__LINE__,#x); abort(); } } while(0)
_Noreturn void _vm_error(const char *format, ...) {
    va_list args; va_start(args,format); vsnprintf(last_error,sizeof(last_error),format,args); va_end(args);
    if(error_active) { errors_caught++; longjmp(error_jump,1); }
    fprintf(stderr,"Unexpected VM error: %s",last_error); abort();
}
union vm_value stack_pop(void) { CHECK(stack_ptr > 0); return stack[--stack_ptr]; }
void vm_call_nopop(int fno, int nargs) {
    CHECK(fno == cb_fno && nargs == cb_nargs);
    CHECK(stack_ptr == 2 + nargs);
    int second = nargs == 2 ? stack_pop().i : 0;
    int first = stack_pop().i;
    int at = (cb_start + callbacks) * cb_stride;
    int type=ain->functions[fno].vars[0].type.data;
    bool reference=type==AIN_REF_INT || type==AIN_REF_FLOAT || type==AIN_REF_BOOL || type==AIN_REF_LONG_INT;
    if(reference) { CHECK(first==100 && second==at); first=cb_array->values[at].i; }
    else { CHECK(first == cb_array->values[at].i);
        if (nargs == 2) CHECK(second == cb_array->values[at + 1].i); }
    cb_first[callbacks] = first; cb_second[callbacks] = second; callbacks++;
    /* Noncanonical true value verifies match-counting rather than result-summing. */
    stack_push(cb_mode == 2 || (cb_mode == 1 && first == cb_target) ? 7 : 0);
    if(cb_mutation==1) {
        size_t size=sizeof(*cb_array)+cb_array->nr_vars*sizeof(union vm_value);
        struct page *replacement=malloc(size); CHECK(replacement);
        memcpy(replacement,cb_array,size); heap[100].page=replacement; free(cb_array);
    } else if(cb_mutation==2) cb_array->nr_vars--;
}
/* @PRODUCTION@ */
/* @BASELINE@ */
static struct page *numbers(const int *values, int n, int type, int stride) {
    struct page *p = calloc(1, sizeof(*p) + n * sizeof(union vm_value)); CHECK(p);
    p->type = ARRAY_PAGE; p->a_type = type; p->nr_vars = n;
    p->array.rank = 1; p->array.struct_type = stride;
    for(int i=0;i<n;i++) p->values[i].i = values[i];
    return p;
}
static void configure(struct page *p, int fno, int mode, int target, int start) {
    callbacks = 0; cb_array=p; cb_fno=fno; cb_mode=mode; cb_target=target; cb_start=start;
    cb_nargs = fno >= 0 ? ain->functions[fno].nr_args : 0;
    cb_stride = p ? array_erase_stride(p) : 1;
    hll_self_slot=100; heap[100]=(struct vm_pointer){.type=VM_PAGE,.page=p,.ref=p?1:0}; cb_mutation=0;
}
/* Types and arity come from the actual loaded AIN declaration. */
static int invoke(void *fn, const struct ain_hll_function *f, struct page **p, int a, int b, int c) {
    ffi_cif cif; ffi_type *types[4]; void *args[4];
    struct page **pp=p; int inputs[3]={a,b,c}; ffi_arg result=0;
    CHECK(fn && f->nr_arguments >= 1 && f->nr_arguments <= 4);
    types[0]=&ffi_type_pointer; args[0]=&pp;
    for(int i=1;i<f->nr_arguments;i++) { types[i]=&ffi_type_sint32; args[i]=&inputs[i-1]; }
    CHECK(ffi_prep_cif(&cif,FFI_DEFAULT_ABI,f->nr_arguments,&ffi_type_sint32,types)==FFI_OK);
    struct page *old=p?*p:NULL; int n=old?old->nr_vars:0;
    union vm_value *copy=calloc(n?n:1,sizeof(*copy)); CHECK(copy);
    if(n) memcpy(copy,old->values,n*sizeof(*copy));
    stack_ptr=2; stack[0].i=0x12345678; stack[1].i=0x23456789;
    error_active=1;
    if(setjmp(error_jump)==0) ffi_call(&cif,FFI_FN(fn),&result,args);
    else result=(ffi_arg)INT_MIN;
    error_active=0;
    CHECK(stack_ptr==2 && stack[0].i==0x12345678 && stack[1].i==0x23456789);
    if(!cb_mutation) {
        CHECK(!p || *p==old); CHECK(!old || old->nr_vars==n);
        CHECK(!n || !memcmp(copy,old->values,n*sizeof(*copy)));
    } free(copy);
    return (int32_t)result;
}
static int call(struct ain_library *lib,int i,struct page **p,int a,int b,int c) {
    return invoke(array_query_function(&lib->functions[i]),&lib->functions[i],p,a,b,c);
}
struct callback_site { int line, fno, metadata, overload; };
static const struct callback_site sites[]={ /* @CALLBACK_SITES@ */ };
int main(int argc,char **argv) {
    CHECK(argc==2); int error=0; ain=ain_open(argv[1],&error); CHECK(ain && ain->version==14);
    initialize_instructions(ain->version);
    int library_index=ain_get_library(ain,"Array"); CHECK(library_index>=0);
    struct ain_library *lib=&ain->libraries[library_index];
    int ids[]={20,21,22,23,42,44,46,48};
    void *expected[]={Array_Numof,Array_CountIf,Array_Numof,Array_CountIf,
        Array_FindValue,Array_FindValueRange,Array_FindIf,Array_FindIfRange};
    for(int i=0;i<8;i++) CHECK(array_query_function(&lib->functions[ids[i]])==expected[i]);
    int single=-1,iface=-1;
    for(size_t i=0;i<sizeof(sites)/sizeof(*sites);i++) {
        struct ain_function *f=&ain->functions[sites[i].fno];
        if(f->nr_args==1 && f->vars[0].type.data==AIN_INT) single=sites[i].fno;
        if(f->nr_args==2 && f->vars[0].type.data==AIN_IFACE) iface=sites[i].fno;
    }
    CHECK(single>=0 && iface>=0);
    const int data[]={8,3,9,3}; struct page *p=numbers(data,4,AIN_ARRAY_INT,1);
    for(int id=20;id<=23;id++) {
        configure(p,single,0,3,0);
        CHECK(call(lib,id,&p,single,0,0)==(id%2?0:4)); CHECK(callbacks==(id%2?4:0));
        if(id%2) {
            configure(p,single,1,3,0); CHECK(call(lib,id,&p,single,0,0)==2 && callbacks==4);
            configure(p,single,2,0,0); CHECK(call(lib,id,&p,single,0,0)==4 && callbacks==4);
        }
    }
    configure(p,-1,0,0,0); CHECK(call(lib,42,&p,3,0,0)==1 && callbacks==0);
    CHECK(call(lib,44,&p,2,4,3)==3 && callbacks==0);
    CHECK(call(lib,44,&p,-20,INT_MAX,8)==0); CHECK(call(lib,44,&p,0,2,9)==-1);
    CHECK(call(lib,44,&p,3,3,3)==-1); CHECK(call(lib,44,&p,3,2,3)==-1);
    CHECK(call(lib,44,&p,0,-1,3)==-1); CHECK(call(lib,44,&p,99,INT_MAX,3)==-1);
    CHECK(call(lib,42,&p,100,0,0)==-1 && callbacks==0);
    configure(p,single,1,3,0); CHECK(call(lib,46,&p,single,0,0)==1 && callbacks==2);
    configure(p,single,0,0,0); CHECK(call(lib,46,&p,single,0,0)==-1 && callbacks==4);
    configure(p,single,2,0,0); CHECK(call(lib,46,&p,single,0,0)==0 && callbacks==1);
    configure(p,single,1,3,2); CHECK(call(lib,48,&p,2,4,single)==3 && callbacks==2);
    configure(p,single,1,8,0); CHECK(call(lib,48,&p,-10,99,single)==0 && callbacks==1);
    configure(p,single,1,9,0); CHECK(call(lib,48,&p,0,2,single)==-1 && callbacks==2);
    configure(p,single,2,0,0); CHECK(call(lib,48,&p,3,2,single)==-1 && callbacks==0);
    CHECK(call(lib,48,&p,0,-1,single)==-1 && callbacks==0);
    CHECK(call(lib,48,&p,99,INT_MAX,single)==-1 && callbacks==0);
    /* Fixed baseline deliberately reproduces the old declaration/dispatch defects. */
    configure(p,single,0,0,0);
    int old_numof=invoke(Array_Numof_before,&lib->functions[21],&p,single,0,0);
    int old_count=invoke(Array_Count_before,&lib->functions[23],&p,single,0,0);
    int old_range=invoke(Array_Find_before,&lib->functions[44],&p,1,4,9);
    CHECK(old_numof==4 && old_count==4 && old_range==-1 && callbacks==0);
    configure(p,single,1,3,0);
    CHECK(invoke(Array_Find_before,&lib->functions[46],&p,single,0,0)==-1 && callbacks==0);
    /* Rejection of invalid callback shapes, not a promise of ref-primitive support. */
    configure(p,single,2,0,0);
    CHECK(call(lib,21,&p,-1,0,0)==INT_MIN && callbacks==0);
    CHECK(call(lib,46,&p,ain->nr_functions,0,0)==INT_MIN && callbacks==0);
    int old_nargs=ain->functions[single].nr_args;
    ain->functions[single].nr_args=0; CHECK(call(lib,21,&p,single,0,0)==INT_MIN);
    ain->functions[single].nr_args=3; CHECK(call(lib,46,&p,single,0,0)==INT_MIN);
    ain->functions[single].nr_args=old_nargs;
    struct ain_function saved=ain->functions[single];
    ain->functions[single].return_type.data=AIN_INT; CHECK(call(lib,21,&p,single,0,0)==INT_MIN);
    ain->functions[single]=saved; ain->functions[single].address=ain->code_size;
    CHECK(call(lib,21,&p,single,0,0)==INT_MIN); ain->functions[single]=saved;
    /* Synthetic reference-primitive contract: receives [owner slot, index]. */
    struct ain_variable reference_vars[2]={0};
    reference_vars[0].type.data=AIN_REF_INT; reference_vars[1].type.data=AIN_VOID;
    ain->functions[single].vars=reference_vars; ain->functions[single].nr_args=2; ain->functions[single].nr_vars=2;
    configure(p,single,1,3,0); CHECK(call(lib,21,&p,single,0,0)==2 && callbacks==4);
    configure(p,single,1,3,2); CHECK(call(lib,48,&p,2,4,single)==3 && callbacks==2);
    hll_self_slot=-1; CHECK(call(lib,21,&p,single,0,0)==INT_MIN);
    ain->functions[single]=saved;
    /* Reject a callback that reallocates storage; no access to freed old page. */
    configure(p,single,2,0,0); cb_mutation=1;
    CHECK(call(lib,21,&p,single,0,0)==INT_MIN && callbacks==1);
    CHECK(p==heap[100].page); cb_mutation=0;
    configure(p,single,2,0,0); cb_mutation=2;
    CHECK(call(lib,46,&p,single,0,0)==INT_MIN && callbacks==1); cb_mutation=0;
    free(p);
    const int pair[]={30,50,32,51,30,52}; p=numbers(pair,6,AIN_ARRAY,2);
    hll_current_arg3=65539; hll_param_slot2=51;
    configure(p,iface,1,32,0); CHECK(call(lib,21,&p,iface,0,0)==1 && callbacks==3);
    CHECK(cb_second[0]==50 && cb_second[1]==51 && cb_second[2]==52);
    configure(p,iface,1,32,0); CHECK(call(lib,46,&p,iface,0,0)==1 && callbacks==2);
    configure(p,23361,1,32,0); CHECK(call(lib,46,&p,23361,0,0)==1 && callbacks==2);
    configure(p,25454,1,32,0); CHECK(call(lib,21,&p,25454,0,0)==1 && callbacks==3);
    configure(p,-1,0,0,0); CHECK(call(lib,42,&p,32,0,0)==INT_MIN && callbacks==0);
    hll_param_slot2=52; CHECK(call(lib,44,&p,1,3,30)==INT_MIN);
    hll_param_slot2=99; CHECK(call(lib,42,&p,32,0,0)==INT_MIN);
    CHECK(call(lib,20,&p,0,0,0)==3 && call(lib,22,&p,0,0,0)==3);
    CHECK(Array_Count(&p)==3); free(p);
    /* String equality compares contents across distinct live heap objects. */
    heap[20]=(struct vm_pointer){.ref=1,.type=VM_STRING,.s=make_string("alpha",5)};
    heap[21]=(struct vm_pointer){.ref=1,.type=VM_STRING,.s=make_string("beta",4)};
    heap[22]=(struct vm_pointer){.ref=1,.type=VM_STRING,.s=make_string("alpha",5)};
    const int strings[]={20,21}; p=numbers(strings,2,AIN_ARRAY_STRING,1); hll_current_arg3=2;
    configure(p,-1,0,0,0); CHECK(call(lib,42,&p,22,0,0)==0 && callbacks==0);
    CHECK(call(lib,44,&p,1,2,22)==-1);
    for(int i=20;i<=22;i++) { CHECK(heap[i].ref==1); free_string(heap[i].s); heap[i].s=NULL; }
    free(p);
    /* IEEE float equality: +0 == -0, NaN != itself. */
    const int floats[]={0,(int)0x80000000,(int)0x7fc00001,0x3fc00000};
    p=numbers(floats,4,AIN_ARRAY_FLOAT,1); hll_current_arg3=1; configure(p,-1,0,0,0);
    CHECK(call(lib,42,&p,(int)0x80000000,0,0)==0); CHECK(call(lib,44,&p,1,4,0)==1);
    CHECK(call(lib,42,&p,(int)0x7fc00001,0,0)==-1); CHECK(call(lib,42,&p,0x3fc00000,0,0)==3);
    CHECK(callbacks==0); free(p); p=NULL;
    for(int k=0;k<8;k++) {
        int id=ids[k],wanted=id<=23?0:-1;
        CHECK(call(lib,id,&p,0,0,0)==wanted); CHECK(call(lib,id,NULL,0,0,0)==wanted);
    }
    p=numbers(NULL,0,AIN_ARRAY_INT,1); CHECK(call(lib,20,&p,0,0,0)==0);
    CHECK(call(lib,21,&p,single,0,0)==0); CHECK(call(lib,42,&p,0,0,0)==-1); free(p);
    /* Signature matching: malformed name, count, return and parameter types. */
    struct ain_hll_function f=lib->functions[48]; struct ain_hll_argument args[4];
    memcpy(args,f.arguments,sizeof(args)); f.arguments=args;
    CHECK(!array_query_function(NULL));
    f.name=NULL; CHECK(!array_query_function(&f)); f.name="Bogus"; CHECK(!array_query_function(&f)); f.name="Find";
    f.return_type.data=AIN_BOOL; CHECK(!array_query_function(&f)); f.return_type.data=AIN_INT;
    f.nr_arguments=3; CHECK(!array_query_function(&f)); f.nr_arguments=4;
    args[0].type.data=AIN_INT; CHECK(!array_query_function(&f)); args[0].type.data=AIN_REF_ARRAY;
    args[1].type.data=AIN_FLOAT; CHECK(!array_query_function(&f)); args[1].type.data=AIN_INT;
    args[3].type.data=AIN_INT; CHECK(!array_query_function(&f)); args[3].type.data=AIN_HLL_FUNC_71;
    CHECK(array_query_function(&f)==Array_FindIfRange);
    args[0].type.data=AIN_REF_ARRAY_INT; CHECK(array_query_function(&f)==Array_FindIfRange);
    f.arguments=NULL; CHECK(!array_query_function(&f));
    f=lib->functions[21]; f.nr_arguments=3; CHECK(!array_query_function(&f));
    printf("{\"status\":\"pass\",\"checks\":%d,\"expected_errors_caught\":%d,\"overload_indices\":[20,21,22,23,42,44,46,48],",checks,errors_caught);
    printf("\"baseline\":{\"false_Numof\":%d,\"false_Count\":%d,\"range_Find\":%d,\"expected\":[0,0,2]},",old_numof,old_count,old_range);
    printf("\"callback_scope\":\"controlled stub; real VM retention and nested FFI not executed; ref primitive owner-index shape is synthetic\",\"callback_sites\":[");
    for(size_t i=0;i<sizeof(sites)/sizeof(*sites);i++) {
        struct ain_function *af=&ain->functions[sites[i].fno];
        if(i) putchar(',');
        printf("{\"dump_line\":%d,\"fno\":%d,\"metadata\":%d,\"overload\":%d,\"argument_types\":[",sites[i].line,sites[i].fno,sites[i].metadata,sites[i].overload);
        for(int j=0;j<af->nr_args;j++) { if(j)putchar(','); printf("%d",af->vars[j].type.data); } printf("]}");
    }
    printf("]}\n"); ain_free(ain); return 0;
}
