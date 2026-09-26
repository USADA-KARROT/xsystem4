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
static inline bool array_elem_is_ref(void) {
	return hll_current_arg3 >= 0x10000 || hll_current_arg3 == 2;
}

static inline bool array_elem_is_2slot(void) {
	int etype = hll_current_arg3 & 0xFFFF;
	return etype == 3 || etype == 5 || etype == AIN_IFACE || etype == AIN_OPTION || etype == AIN_IFACE_WRAP;
}

static int array_erase_stride(const struct page *a)
{
	if ((a->a_type == AIN_ARRAY || a->a_type == AIN_REF_ARRAY)
	    && a->array.struct_type > 1 && array_elem_is_2slot())
		return a->array.struct_type;
	return 1;
}

static bool array_erase_value_equal(union vm_value a, union vm_value b)
{
	if (a.i == b.i)
		return true;
	if (!array_elem_is_ref() || a.i <= 0 || b.i <= 0
	    || (size_t)a.i >= heap_size || (size_t)b.i >= heap_size
	    || HEAP_REF(a.i) <= 0 || HEAP_REF(b.i) <= 0
	    || heap[a.i].type != VM_STRING || heap[b.i].type != VM_STRING
	    || !heap[a.i].s || !heap[b.i].s)
		return false;
	struct string *sa = heap[a.i].s, *sb = heap[b.i].s;
	return sa->size == sb->size && !memcmp(sa->text, sb->text, sa->size);
}

static int Array_Numof(struct page **self)
{
	struct page *array = (self && *self) ? *self : NULL;
	if (!array || array->type != ARRAY_PAGE)
		return 0;
	int n = array ? array->nr_vars : 0;
	// v14: return logical element count for multi-slot arrays.
	// struct_type doubles as the AIN struct id for struct-element lists
	// (EmplaceBack) — only treat it as a stride when the call's element
	// type is actually a 2-slot value (iface/option), or Numof on a
	// CMessageText list divides 4/693 to 0 and every reader sees an
	// empty list (the dialogue-text blackout, gui-run-log fb39-fb47).
	if (array && (array->a_type == AIN_ARRAY || array->a_type == AIN_REF_ARRAY)
	    && array->array.struct_type > 1 && array_elem_is_2slot())
		n /= array->array.struct_type;
	return n;
}

static int Array_Count(struct page **self)
{
	return Array_Numof(self);
}

static bool array_query_callback_shape(struct page **array, int func, int stride)
{
	if (func < 0 || func >= ain->nr_functions)
		VM_ERROR("Array query: invalid predicate %d", func);
	struct ain_function *cb = &ain->functions[func];
	if (cb->address >= ain->code_size || cb->return_type.data != AIN_BOOL
	    || !cb->vars || cb->nr_vars < cb->nr_args || cb->nr_args < 1 || cb->nr_args > 2)
		VM_ERROR("Array query: unsupported predicate signature %d", func);
	enum ain_data_type type = cb->vars[0].type.data;
	if (cb->nr_args == 1 && stride == 1) {
		switch (type) {
		case AIN_INT: case AIN_FLOAT: case AIN_BOOL: case AIN_LONG_INT:
		case AIN_ENUM: case AIN_ENUM2: case AIN_STRING: case AIN_REF_STRING:
		case AIN_STRUCT: case AIN_REF_STRUCT: case AIN_WRAP:
			return false;
		default:
			break;
		}
	}
	if (cb->nr_args == 2 && cb->vars[1].type.data == AIN_VOID) {
		bool wrapped_iface = type == AIN_WRAP && cb->vars[0].type.array_type
			&& (cb->vars[0].type.array_type->data == AIN_IFACE
			    || cb->vars[0].type.array_type->data == AIN_IFACE_WRAP);
		if (stride == 2 && (type == AIN_IFACE || type == AIN_IFACE_WRAP || wrapped_iface))
			return false;
		if (stride == 1 && (type == AIN_REF_INT || type == AIN_REF_FLOAT
		    || type == AIN_REF_BOOL || type == AIN_REF_LONG_INT)) {
			if (hll_self_slot < 0 || (size_t)hll_self_slot >= heap_size
			    || HEAP_REF(hll_self_slot) <= 0 || heap[hll_self_slot].type != VM_PAGE
			    || heap[hll_self_slot].page != *array)
				VM_ERROR("Array query: predicate reference has no array owner");
			return true;
		}
	}
	VM_ERROR("Array query: unsupported predicate argument type %d / stride %d", type, stride);
}

static bool array_query_predicate(struct page **array, int index, int stride, int func)
{
	struct ain_function *cb = &ain->functions[func];
	bool reference = array_query_callback_shape(array, func, stride);
	struct page *before = *array;
	int size = before->nr_vars;
	int owner = hll_self_slot;
	bool tracked = owner >= 0 && (size_t)owner < heap_size && HEAP_REF(owner) > 0
		&& heap[owner].type == VM_PAGE && heap[owner].page == before;
	int saved_sp = stack_ptr;
	if (reference) {
		stack_push(owner);
		stack_push(index * stride);
	} else {
		stack_push(before->values[index * stride]);
		if (cb->nr_args == 2)
			stack_push(before->values[index * stride + 1]);
	}
	vm_call_nopop(func, cb->nr_args);
	bool match = stack_pop().i != 0;
	stack_ptr = saved_sp;
	// FFI passed a local page snapshot. A nested HLL can replace/free the
	// owner's page; refresh before touching it or outer FFI writes it back.
	if (tracked) {
		if ((size_t)owner >= heap_size || HEAP_REF(owner) <= 0 || heap[owner].type != VM_PAGE)
			VM_ERROR("Array query: predicate released its array owner");
		*array = heap[owner].page;
	}
	if (*array != before || !*array || (*array)->type != ARRAY_PAGE || (*array)->nr_vars != size)
		VM_ERROR("Array query: predicate changed array storage");
	return match;
}

static int Array_CountIf(struct page **array, int func)
{
	if (!array || !*array || (*array)->type != ARRAY_PAGE)
		return 0;
	int stride = array_erase_stride(*array);
	array_query_callback_shape(array, func, stride);
	int end = (*array)->nr_vars / stride;
	int matches = 0;
	for (int i = 0; i < end && *array && (*array)->type == ARRAY_PAGE
	     && i < (*array)->nr_vars / stride; i++) {
		if (array_query_predicate(array, i, stride, func))
			matches++;
	}
	return matches;
}

static enum ain_data_type array_query_value_type(const struct page *array)
{
	if (array->array.rank > 1 || array_elem_is_2slot())
		VM_ERROR("Array.Find: unsupported value element shape");
	switch (array->a_type) {
	case AIN_INT: case AIN_ARRAY_INT: case AIN_BOOL: case AIN_ARRAY_BOOL:
	case AIN_ENUM: case AIN_ENUM2:
		// Generic allocation can default to ARRAY_INT even for heap elements.
		if (array_elem_is_ref())
			VM_ERROR("Array.Find: reference element lacks a supported concrete type");
		return AIN_INT;
	case AIN_FLOAT: case AIN_ARRAY_FLOAT:
		return AIN_FLOAT;
	case AIN_STRING: case AIN_ARRAY_STRING:
		return AIN_STRING;
	default:
		VM_ERROR("Array.Find: unsupported or erased value element type %d", array->a_type);
	}
}

static bool array_query_value_equal(union vm_value a, union vm_value b, enum ain_data_type type)
{
	if (type == AIN_FLOAT)
		return a.f == b.f;
	if (type != AIN_STRING)
		return a.i == b.i;
	if (a.i <= 0 || b.i <= 0 || (size_t)a.i >= heap_size || (size_t)b.i >= heap_size
	    || HEAP_REF(a.i) <= 0 || HEAP_REF(b.i) <= 0
	    || heap[a.i].type != VM_STRING || heap[b.i].type != VM_STRING
	    || !heap[a.i].s || !heap[b.i].s)
		VM_ERROR("Array.Find: invalid string value");
	struct string *sa = heap[a.i].s, *sb = heap[b.i].s;
	return sa->size == sb->size && !memcmp(sa->text, sb->text, sa->size);
}

static int Array_FindValueRange(struct page **array, int begin, int end, int value)
{
	const struct page *src = array ? *array : NULL;
	if (!src || src->type != ARRAY_PAGE)
		return -1;
	enum ain_data_type type = array_query_value_type(src);
	int count = src->nr_vars;
	if (begin < 0) begin = 0;
	if (end > count) end = count;
	union vm_value needle = {.i = value};
	for (int i = begin; i < end; i++) {
		if (array_query_value_equal(src->values[i], needle, type))
			return i;
	}
	return -1;
}

static int Array_FindValue(struct page **array, int value)
{
	return Array_FindValueRange(array, 0, INT_MAX, value);
}

static int Array_FindIfRange(struct page **array, int begin, int end, int func)
{
	if (!array || !*array || (*array)->type != ARRAY_PAGE)
		return -1;
	int stride = array_erase_stride(*array);
	array_query_callback_shape(array, func, stride);
	int count = (*array)->nr_vars / stride;
	if (begin < 0) begin = 0;
	if (end > count) end = count;
	for (int i = begin; i < end && *array && (*array)->type == ARRAY_PAGE
	     && i < (*array)->nr_vars / stride; i++) {
		if (array_query_predicate(array, i, stride, func))
			return i;
	}
	return -1;
}

static int Array_FindIf(struct page **array, int func)
{
	return Array_FindIfRange(array, 0, INT_MAX, func);
}

/* Select by declared signature, never by a game's function index. */
void *array_query_function(const struct ain_hll_function *f)
{
	if (!f || !f->name || !f->arguments || f->nr_arguments < 1 || f->return_type.data != AIN_INT)
		return NULL;
	switch (f->arguments[0].type.data) {
	case AIN_REF_ARRAY_TYPE:
	case AIN_REF_ARRAY:
		break;
	default:
		return NULL;
	}
	bool count = !strcmp(f->name, "Numof") || !strcmp(f->name, "Count");
	if (count) {
		if (f->nr_arguments == 1)
			return Array_Numof;
		if (f->nr_arguments == 2 && (f->arguments[1].type.data == AIN_HLL_FUNC
		    || f->arguments[1].type.data == AIN_HLL_FUNC_71))
			return Array_CountIf;
		return NULL;
	}
	if (strcmp(f->name, "Find"))
		return NULL;
	bool range = f->nr_arguments == 4;
	if (range) {
		if (f->arguments[1].type.data != AIN_INT || f->arguments[2].type.data != AIN_INT)
			return NULL;
	} else if (f->nr_arguments != 2) {
		return NULL;
	}
	enum ain_data_type arg = f->arguments[range ? 3 : 1].type.data;
	if (arg == AIN_HLL_PARAM)
		return range ? (void *)Array_FindValueRange : (void *)Array_FindValue;
	if (arg == AIN_HLL_FUNC || arg == AIN_HLL_FUNC_71)
		return range ? (void *)Array_FindIfRange : (void *)Array_FindIf;
	return NULL;
}


static int Array_Numof_before(struct page **self)
{
	struct page *array = (self && *self) ? *self : NULL;
	int n = array ? array->nr_vars : 0;
	// v14: return logical element count for multi-slot arrays.
	// struct_type doubles as the AIN struct id for struct-element lists
	// (EmplaceBack) — only treat it as a stride when the call's element
	// type is actually a 2-slot value (iface/option), or Numof on a
	// CMessageText list divides 4/693 to 0 and every reader sees an
	// empty list (the dialogue-text blackout, gui-run-log fb39-fb47).
	if (array && (array->a_type == AIN_ARRAY || array->a_type == AIN_REF_ARRAY)
	    && array->array.struct_type > 1 && array_elem_is_2slot())
		n /= array->array.struct_type;
	return n;
}

static int Array_Count_before(struct page **self)
{
	struct page *array = (self && *self) ? *self : NULL;
	return array ? array->nr_vars : 0;
}

static int Array_Find_before(struct page **array, int value)
{
	struct page *src = (array && *array) ? *array : NULL;
	if (!src || src->nr_vars == 0)
		return -1;
	for (int i = 0; i < src->nr_vars; i++) {
		if (src->values[i].i == value)
			return i;
	}
	return -1;
}
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
static const struct callback_site sites[]={ {25650,22332,65538,46},{26025,22334,65538,46},{63764,22391,65538,21},{64181,22395,65538,46},{73884,22416,65538,46},{73920,22417,65538,46},{75895,22420,65538,46},{75931,22421,65538,46},{104017,22445,2,46},{112823,22456,1,46},{125608,22468,65538,46},{125644,22469,65538,46},{125751,22470,65538,46},{131763,22473,65538,46},{132599,22474,65538,46},{201625,23361,65539,46},{222566,23486,2,46},{222602,23487,2,46},{272988,23898,65538,46},{285039,23960,65538,46},{286404,23963,65538,21},{287201,23969,65538,46},{296559,23987,65539,46},{300613,23994,65538,46},{336370,24191,65539,46},{350110,24279,65538,21},{402371,24363,2,46},{407104,24369,65538,46},{412604,24370,65538,46},{412640,24371,65538,46},{412676,24372,65538,46},{412705,24373,65538,46},{416343,24376,65538,46},{416990,24380,2,46},{418835,24381,2,46},{437768,24530,2,46},{438103,24531,2,46},{438509,24534,65538,46},{448768,24575,2,46},{506981,24822,65538,46},{507033,24823,65538,46},{515587,24836,65538,46},{737357,25234,2,46},{738623,25235,2,46},{738656,25236,2,46},{740112,25237,2,46},{744247,25269,65538,46},{752126,25304,65538,46},{754235,25323,65538,46},{789026,25454,65539,46},{981607,36002,2,46},{983006,36011,2,46},{984960,36020,2,46},{985586,36021,2,46},{986320,36022,2,46},{986351,36023,2,46},{986382,36024,2,46},{986413,36025,2,46},{986444,36026,2,46},{986475,36027,2,46},{1019325,36161,2,46},{1027341,36212,2,21},{1032066,36232,2,21},{1045559,36299,65538,46},{1045588,36300,65538,46},{1045620,36301,65538,46},{1046441,36311,65538,23},{1046487,36312,65538,23},{1083411,36476,2,21},{1088882,36493,65538,23},{1100806,36564,2,23},{1154097,36702,65538,46},{1154982,36708,65538,21},{1155012,36709,65538,46},{1178588,36812,65538,23},{1189501,36836,65538,46},{1190002,36841,65538,46},{1198662,36874,65538,23},{1201332,36883,65538,46},{1204202,36896,2,23},{1204413,36898,65538,23},{1226149,36975,65538,46},{1231497,37005,65538,46},{1234811,37017,2,23},{1263176,37116,65538,23},{1325853,37330,65538,46},{1346366,37434,65538,46},{1346783,37436,65538,23},{1346813,37437,65538,23},{1701230,37517,65538,46},{1713546,37564,2,46} };
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
