/* Production static linker + marshaller + Array query code, real libffi/real
 * AIN declarations. VM callback bodies/heap services are bounded substitutes. */
#include <assert.h>
#include <stdarg.h>
#include FFI_SOURCE
#include "system4/string.h"
int hll_current_arg3 = -1, hll_self_slot = -1, hll_param_slot2;
#include "query-production.inc"

static union vm_value fixture_stack[256];
union vm_value *stack = fixture_stack;
int32_t stack_ptr;
static struct vm_pointer fixture_heap[128];
struct vm_pointer *heap = fixture_heap;
size_t heap_size = 128;
struct ain *ain;
static int array_lib, free_index, callbacks, nested_calls, expected_self;
static bool mutate;

_Noreturn void _vm_error(const char *fmt, ...) {
    va_list ap; va_start(ap, fmt); vfprintf(stderr, fmt, ap); va_end(ap);
    fputc('\n', stderr); exit(42);
}
union vm_value stack_pop(void) { assert(stack_ptr > 0); return stack[--stack_ptr]; }
bool heap_index_valid(int i) { return i > 0 && i < 128 && heap[i].ref > 0; }
int32_t heap_alloc_slot(enum vm_pointer_type type) { (void)type; abort(); }
void variable_fini(union vm_value v, enum ain_data_type type, bool dtor) {
    (void)v; (void)dtor; assert(type == AIN_INT || type == AIN_HLL_FUNC_71);
}
void *array_erase_function(const struct ain_hll_function *f) { (void)f; return NULL; }
void *array_isexist_function(const struct ain_hll_function *f) { (void)f; return NULL; }

static void fixture_free(struct page **p) { free(*p); *p = NULL; }
static struct page *make_array(int owner, const int *values, int n, int type, int stride) {
    struct page *p = calloc(1, sizeof(*p) + n*sizeof(union vm_value)); assert(p);
    p->type = ARRAY_PAGE; p->a_type = type; p->nr_vars = n;
    p->array.rank = 1; p->array.struct_type = stride;
    for (int i=0;i<n;i++) p->values[i].i=values[i];
    heap[owner] = (struct vm_pointer){.ref=1,.type=VM_PAGE,.page=p}; return p;
}
void vm_call_nopop(int fno, int nargs) {
    int before = stack_ptr;
    assert(hll_func_obj == 77 && hll_self_slot == expected_self);
    assert(nargs == ain->functions[fno].nr_args);
    int v = stack[stack_ptr-nargs].i;
    if (nargs == 2) assert(stack[stack_ptr-1].i == 900);
    int tag=hll_current_arg3, owner=hll_self_slot, obj=hll_func_obj, second=hll_param_slot2;
    stack_push(2); hll_call(array_lib,20,1);
    assert(stack_pop().i == 3 && stack_ptr == before); nested_calls++;
    assert(hll_current_arg3==tag && hll_self_slot==owner && hll_func_obj==obj && hll_param_slot2==second);
    callbacks++;
    if (mutate) { stack_push(owner); hll_call(array_lib,free_index,tag); }
    stack_push(v % 2 == 0 ? 7 : 0); /* nonzero is true, not a weight */
}
static int query(int fno, int owner, int begin, int end, int value, int tag) {
    assert(stack_ptr == 0);
    hll_current_arg3=-7; hll_self_slot=-8; hll_func_obj=-9; hll_param_slot2=123;
    expected_self=owner;
    stack_push(987654); stack_push(owner);
    struct ain_hll_function *f=&ain->libraries[array_lib].functions[fno];
    if(f->nr_arguments == 4) { stack_push(begin); stack_push(end); }
    if(f->nr_arguments > 1) {
        if(f->arguments[f->nr_arguments-1].type.data == AIN_HLL_FUNC) stack_push(77);
        stack_push(value);
    }
    hll_call(array_lib,fno,tag);
    assert(stack_ptr==2 && stack[0].i==987654);
    int answer=stack_pop().i; stack_pop();
    assert(hll_current_arg3==-7 && hll_self_slot==-8 && hll_func_obj==-9 && hll_param_slot2==123);
    return answer;
}
int main(int argc, char **argv) {
    assert(argc>=2); int error=0; ain=ain_open(argv[1],&error); assert(ain);
    array_lib=ain_get_library(ain,"Array"); assert(array_lib>=0);
    struct ain_library *a=&ain->libraries[array_lib];
    static struct static_library exports={.name="Array",.functions={
        {.name="Numof",.fun=Array_Numof},{.name="Count",.fun=Array_Count},
        {.name="Find",.fun=Array_FindValue},{.name="Free",.fun=fixture_free},{0}
    }};
    struct ain_function saved[2]={ain->functions[0],ain->functions[1]};
    struct ain_variable vars[2]={ {.type={.data=AIN_INT}}, {.type={.data=AIN_VOID}} };
    struct ain_variable pair_vars[2]={ {.type={.data=AIN_IFACE}}, {.type={.data=AIN_VOID}} };
    ain->functions[0]=(struct ain_function){.address=2,.return_type={.data=AIN_BOOL},.nr_args=1,.nr_vars=1,.vars=vars};
    ain->functions[1]=(struct ain_function){.address=2,.return_type={.data=AIN_BOOL},.nr_args=2,.nr_vars=2,.vars=pair_vars};
    libraries=calloc(ain->nr_libraries,sizeof(*libraries));
    libraries[array_lib]=link_static_library(a,&exports);
    for(int i=0;i<a->nr_functions;i++) if(!strcmp(a->functions[i].name,"Free"))free_index=i;
    int ids[]={20,21,22,23,42,44,46,48};
    for(unsigned i=0;i<sizeof(ids)/sizeof(*ids);i++) {
        int f=ids[i]; assert(libraries[array_lib][f].fun==array_query_function(&a->functions[f]));
        assert(libraries[array_lib][f].nr_args==(unsigned)a->functions[f].nr_arguments);
    }
    const int vals[]={1,2,4,7,8}, inner[]={6,7,8}, pairs[]={20,900,21,900,22,900};
    make_array(1,vals,5,AIN_ARRAY_INT,1); make_array(2,inner,3,AIN_ARRAY_INT,1);
    make_array(3,pairs,6,AIN_ARRAY,2);
    mutate=argc>2;
    assert(query(20,1,0,0,0,1)==5 && query(22,1,0,0,0,1)==5);
    assert(query(21,1,0,0,0,1)==3 && query(23,1,0,0,0,1)==3);
    int prior=callbacks;
    assert(query(42,1,0,0,4,1)==2 && query(44,1,2,4,2,1)==-1);
    assert(query(44,1,2,4,4,1)==2 && query(44,1,2,4,8,1)==-1);
    assert(callbacks==prior);
    assert(query(46,1,0,0,0,1)==1 && query(48,1,2,4,0,1)==2);
    assert(query(20,3,0,0,0,65539)==3 && query(22,3,0,0,0,65539)==3);
    assert(query(21,3,0,0,1,65539)==2 && query(23,3,0,0,1,65539)==2);
    assert(query(46,3,0,0,1,65539)==0 && query(48,3,1,3,1,65539)==2);
    assert(callbacks==nested_calls && callbacks>0);
    printf("PASS: 8 actual declarations linked via production selector/CIF; %d callback+nested HLL calls; closure/generic context and VM stack restored; no value-search callbacks.\n",callbacks);
    for(int i=0;i<a->nr_functions;i++)free(libraries[array_lib][i].args);
    free(libraries[array_lib]);free(libraries);
    for(int i=1;i<=3;i++)free(heap[i].page);
    ain->functions[0]=saved[0];ain->functions[1]=saved[1];ain_free(ain); return 0;
}
