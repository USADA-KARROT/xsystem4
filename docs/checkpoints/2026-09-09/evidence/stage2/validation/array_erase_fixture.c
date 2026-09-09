
#include <assert.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ffi.h>
#include "system4/ain.h"
#include "system4/string.h"
#include "vm.h"
#include "vm/heap.h"
#include "vm/page.h"
struct ain *ain;
static union vm_value fixture_stack[64];
union vm_value *stack=fixture_stack;
int32_t stack_ptr;
static struct vm_pointer slots[128];
struct vm_pointer *heap=slots;
size_t heap_size=128;
int hll_current_arg3;
static int unrefs[128], callbacks;
static bool ended[128];
struct page *alloc_page(enum page_type type,int index,int count) {
 struct page *p=calloc(1,sizeof(*p)+count*sizeof(union vm_value));assert(p);
 p->type=type;p->index=index;p->nr_vars=count;p->array.rank=1;return p;
}
void free_page(struct page *p){free(p);}
void heap_unref(int slot){assert(slot>0 && slot<128 && heap[slot].ref>0);unrefs[slot]++;heap[slot].ref--;}
union vm_value stack_pop(void){assert(stack_ptr>0);return stack[--stack_ptr];}
void vm_call_nopop(int fno,int nargs){
 assert(fno==17 && (nargs==1 || nargs==2));int obj=stack[stack_ptr-nargs].i;assert(obj>0 && obj<128);
 if(nargs==2)assert(stack[stack_ptr-1].i==obj+1);
 callbacks++;stack_push(ended[obj]);
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
static bool Array_Erase(struct page **array, int index, int length)
{
	if (!array || !*array)
		return false;
	struct page *a = *array;
	int stride = array_erase_stride(a);
	int count = a->nr_vars / stride;
	if (length <= 0 || index < 0 || index >= count)
		return false;
	if (length > count - index)
		length = count - index;
	index *= stride;
	length *= stride;
	// Match PushBack: only the first slot owns a reference. The second slot
	// of an interface/option element is metadata, even if it looks like a slot.
	if (array_elem_is_ref()) {
		for (int i = index; i < index + length; i += stride) {
			int removed = a->values[i].i;
			if (removed > 0)
				heap_unref(removed);
		}
	}
	int new_size = a->nr_vars - length;
	if (new_size == 0) {
		free_page(a);
		*array = NULL;
		return true;
	}
	struct page *new_a = alloc_page(ARRAY_PAGE, a->a_type, new_size);
	for (int i = 0; i < index; i++)
		new_a->values[i] = a->values[i];
	for (int i = index; i < new_size; i++)
		new_a->values[i] = a->values[i + length];
	new_a->array = a->array;
	free_page(a);
	*array = new_a;
	return true;
}
static bool Array_EraseIf(struct page **array, int func)
{
	if (!array || !*array || func < 0 || func >= ain->nr_functions)
		return false;
	struct ain_function *cb = &ain->functions[func];
	if (cb->nr_args < 1 || cb->nr_args > 2)
		return false;
	int stride = array_erase_stride(*array);
	for (int i = 0; *array && i < (*array)->nr_vars / stride; i++) {
		int saved_sp = stack_ptr;
		stack_push((*array)->values[i * stride]);
		if (cb->nr_args == 2)
			stack_push(stride > 1 ? (*array)->values[i * stride + 1] : (union vm_value){.i = 0});
		vm_call_nopop(func, cb->nr_args);
		bool match = stack_pop().i != 0;
		stack_ptr = saved_sp;
		if (match)
			return Array_Erase(array, i, 1);
	}
	return false;
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
static bool Array_EraseValues(struct page **array, int source_slot)
{
	if (!array || !*array || source_slot <= 0 || (size_t)source_slot >= heap_size
	    || HEAP_REF(source_slot) <= 0 || heap[source_slot].type != VM_PAGE
	    || !heap[source_slot].page || heap[source_slot].page->type != ARRAY_PAGE)
		return false;
	const struct page *src = heap[source_slot].page;
	int stride = array_erase_stride(*array);
	if (array_erase_stride(src) != stride)
		return false;
	if (src == *array)
		return Array_Erase(array, 0, (*array)->nr_vars / stride);
	bool erased = false;
	for (int i = (*array)->nr_vars / stride - 1; i >= 0; i--) {
		bool found = false;
		for (int j = 0; j < src->nr_vars / stride && !found; j++) {
			found = true;
			for (int k = 0; k < stride; k++) {
				union vm_value a = (*array)->values[i * stride + k];
				union vm_value b = src->values[j * stride + k];
				// Extra slots are metadata, never string references.
				if (!(k == 0 ? array_erase_value_equal(a, b) : a.i == b.i)) {
					found = false;
					break;
				}
			}
		}
		if (found)
			erased = Array_Erase(array, i, 1) || erased;
	}
	return erased;
}
void *array_erase_function(const struct ain_hll_function *f)
{
	if (!f || !f->arguments || (f->return_type.data != AIN_BOOL && f->return_type.data != AIN_VOID)
	    || f->nr_arguments < 2)
		return NULL;
	switch (f->arguments[0].type.data) {
	case AIN_REF_ARRAY_TYPE:
	case AIN_REF_ARRAY:
		break;
	default:
		return NULL;
	}
	if (f->nr_arguments == 3 && f->arguments[1].type.data == AIN_INT
	    && f->arguments[2].type.data == AIN_INT)
		return Array_Erase;
	if (f->nr_arguments != 2 || f->return_type.data != AIN_BOOL)
		return NULL;
	if (f->arguments[1].type.data == AIN_HLL_FUNC || f->arguments[1].type.data == AIN_HLL_FUNC_71)
		return Array_EraseIf;
	if (f->arguments[1].type.data == AIN_WRAP && f->arguments[1].type.array_type
	    && f->arguments[1].type.array_type->data == AIN_ARRAY)
		return Array_EraseValues;
	return NULL;
}
static void Array_Erase_before(struct page **array, int index, int length)
{
	if (!array || !*array)
		return;
	struct page *a = *array;
	// v14: convert logical index/length to physical for multi-slot arrays
	// (2-slot value elements only — see Array_Numof).
	if ((a->a_type == AIN_ARRAY || a->a_type == AIN_REF_ARRAY)
	    && a->array.struct_type > 1 && array_elem_is_2slot()) {
		index *= a->array.struct_type;
		length *= a->array.struct_type;
	}
	if (length <= 0 || index < 0 || index >= a->nr_vars)
		return;
	// Clamp length to available elements
	if (index + length > a->nr_vars)
		length = a->nr_vars - index;
	// v14: unref removed elements if they are heap objects
	if (array_elem_is_ref()) {
		for (int i = index; i < index + length; i++) {
			int removed = a->values[i].i;
			if (removed > 0)
				heap_unref(removed);
		}
	}
	int new_size = a->nr_vars - length;
	if (new_size == 0) {
		free_page(a);
		*array = NULL;
		return;
	}
	struct page *new_a = alloc_page(ARRAY_PAGE, a->a_type, new_size);
	for (int i = 0; i < index; i++)
		new_a->values[i] = a->values[i];
	for (int i = index; i < new_size; i++)
		new_a->values[i] = a->values[i + length];
	new_a->array = a->array;
	free_page(a);
	*array = new_a;
}

static struct page *numbers(const int *v,int n,int type){
 struct page *p=alloc_page(ARRAY_PAGE,type,n);for(int i=0;i<n;i++)p->values[i].i=v[i];return p;
}
static void expect(struct page *p,const int *v,int n){
 assert((p?p->nr_vars:0)==n);for(int i=0;i<n;i++)assert(p->values[i].i==v[i]);
}
static bool call2(void *fn,struct page **p,int value){
 struct page **pp=p;ffi_type *types[]={&ffi_type_pointer,&ffi_type_sint32};
 void *args[]={&pp,&value};ffi_cif cif;ffi_arg ret=0;
 assert(ffi_prep_cif(&cif,FFI_DEFAULT_ABI,2,&ffi_type_sint32,types)==FFI_OK);
 ffi_call(&cif,FFI_FN(fn),&ret,args);return *(unsigned char*)&ret!=0;
}
static bool call3(void *fn,struct page **p,int index,int length){
 struct page **pp=p;ffi_type *types[]={&ffi_type_pointer,&ffi_type_sint32,&ffi_type_sint32};
 void *args[]={&pp,&index,&length};ffi_cif cif;ffi_arg ret=0;
 assert(ffi_prep_cif(&cif,FFI_DEFAULT_ABI,3,&ffi_type_sint32,types)==FFI_OK);
 ffi_call(&cif,FFI_FN(fn),&ret,args);return *(unsigned char*)&ret!=0;
}
int main(int argc,char **argv){
 assert(argc==2);int error=0;struct ain *actual=ain_open(argv[1],&error);assert(actual);
 int libno=ain_get_library(actual,"Array");assert(libno>=0);
 void *range=NULL,*predicate=NULL,*values=NULL;int declarations=0;
 for(int i=0;i<actual->libraries[libno].nr_functions;i++){
  struct ain_hll_function *f=&actual->libraries[libno].functions[i];if(strcmp(f->name,"Erase"))continue;
  printf("AIN Array:%d Erase return=%d argc=%d",i,f->return_type.data,f->nr_arguments);
  for(int j=0;j<f->nr_arguments;j++)printf(" arg%d=%d(inner=%d)",j,f->arguments[j].type.data,f->arguments[j].type.array_type?f->arguments[j].type.array_type->data:-1);
  void *fn=array_erase_function(f);assert(fn);
  if(f->nr_arguments==3){assert(fn==(void*)Array_Erase);range=fn;}
  else if(f->arguments[1].type.data==AIN_WRAP){assert(fn==(void*)Array_EraseValues);values=fn;}
  else {assert(fn==(void*)Array_EraseIf);predicate=fn;}
  declarations++;puts(" -> distinct ABI selected");
 }
 assert(declarations==3 && range && predicate && values);
 struct ain_function functions[32]={0};functions[17].nr_args=1;functions[17].return_type.data=AIN_BOOL;
 struct ain fixture={.nr_functions=32,.functions=functions};ain=&fixture;
 for(int i=1;i<128;i++){heap[i].ref=1;heap[i].type=VM_PAGE;}
 const int start[]={1,2,3,4};struct page *p=numbers(start,4,AIN_ARRAY);ended[2]=ended[3]=true;hll_current_arg3=65538;
 /* Old binding treats callback number as index and leaves these observers. */
 Array_Erase_before(&p,17,1);expect(p,start,4);assert(callbacks==0);
 assert(call2(predicate,&p,17));const int once[]={1,3,4};expect(p,once,3);assert(unrefs[2]==1 && unrefs[3]==0);
 assert(callbacks==2);assert(call2(predicate,&p,17));const int twice[]={1,4};expect(p,twice,2);
 assert(!call2(predicate,&p,17));expect(p,twice,2);assert(unrefs[1]==0 && unrefs[2]==1 && unrefs[3]==1 && unrefs[4]==0);assert(stack_ptr==0);
 free_page(p);puts("Observer regression: old binding keeps ended observers; new predicate removes first match, preserves survivors and refcounts: PASS");
 hll_current_arg3=1;const int nums[]={10,20,30,40};p=numbers(nums,4,AIN_INT);
 assert(!call3(range,&p,-1,1));assert(!call3(range,&p,0,0));expect(p,nums,4);
 assert(call3(range,&p,1,2));const int ends[]={10,40};expect(p,ends,2);
 assert(call3(range,&p,1,INT_MAX));const int first[]={10};expect(p,first,1);
 assert(call3(range,&p,0,1));assert(!p);assert(!call3(range,&p,0,1));puts("Index/length ABI, bool result, clamping and empty array: PASS");
 hll_current_arg3=0x10003;const int pairs[]={30,31,32,33};p=numbers(pairs,4,AIN_ARRAY);p->array.struct_type=2;
 Array_Erase_before(&p,0,1);assert(unrefs[30]==1 && unrefs[31]==1);free_page(p);
 heap[30].ref=heap[31].ref=1;unrefs[30]=unrefs[31]=0;
 p=numbers(pairs,4,AIN_ARRAY);p->array.struct_type=2;assert(call3(range,&p,0,1));const int pair_left[]={32,33};expect(p,pair_left,2);
 assert(unrefs[30]==1 && unrefs[31]==0 && unrefs[32]==0 && unrefs[33]==0);
 functions[17].nr_args=2;ended[32]=true;assert(call2(predicate,&p,17));assert(!p && stack_ptr==0);functions[17].nr_args=1;
 assert(unrefs[32]==1 && unrefs[33]==0);puts("Two-slot range/predicate: old erase releases metadata; new erase releases first slot only and passes both callback arguments: PASS");
 hll_current_arg3=1;
 const int dest[]={10,20,20,30,40},src[]={20,40};p=numbers(dest,5,AIN_INT);heap[100].page=numbers(src,2,AIN_INT);
 assert(call2(values,&p,100));const int diff[]={10,30};expect(p,diff,2);expect(heap[100].page,src,2);assert(!call2(values,&p,100));free_page(heap[100].page);heap[100].page=p;
 assert(call2(values,&p,100));assert(!p);heap[100].page=NULL;puts("Wrapped-source ABI: integer set subtraction, source unchanged, alias-to-self: PASS");
 hll_current_arg3=2;
 heap[20]=(struct vm_pointer){.ref=1,.type=VM_STRING,.s=make_string("alpha",5)};
 heap[21]=(struct vm_pointer){.ref=1,.type=VM_STRING,.s=make_string("beta",4)};
 heap[22]=(struct vm_pointer){.ref=1,.type=VM_STRING,.s=make_string("alpha",5)};
 heap[23]=(struct vm_pointer){.ref=1,.type=VM_STRING,.s=make_string("alpha",5)};
 const int strings[]={20,21,22},remove_strings[]={23};p=numbers(strings,3,AIN_STRING);heap[100].page=numbers(remove_strings,1,AIN_STRING);
 assert(call2(values,&p,100));const int remaining[]={21};expect(p,remaining,1);assert(unrefs[20]==1 && unrefs[22]==1 && unrefs[21]==0 && unrefs[23]==0);
 free_page(p);free_page(heap[100].page);heap[100].page=NULL;for(int i=20;i<=23;i++)free_string(heap[i].s);
 puts("Wrapped-source string values compare contents across distinct heap slots; removed references released once: PASS");
 hll_current_arg3=0x10003;heap[40].ref=2;
 heap[50]=(struct vm_pointer){.ref=1,.type=VM_STRING,.s=make_string("same",4)};
 heap[51]=(struct vm_pointer){.ref=1,.type=VM_STRING,.s=make_string("same",4)};
 const int iface[]={40,50},other_iface[]={40,51};p=numbers(iface,2,AIN_ARRAY);p->array.struct_type=2;
 heap[100].page=numbers(other_iface,2,AIN_ARRAY);heap[100].page->array.struct_type=2;
 assert(!call2(values,&p,100));expect(p,iface,2);assert(heap[40].ref==2 && unrefs[50]==0 && unrefs[51]==0);
 heap[100].page->values[1].i=50;assert(call2(values,&p,100));assert(!p && heap[40].ref==1 && unrefs[40]==1 && unrefs[50]==0 && unrefs[51]==0);
 free_page(heap[100].page);heap[100].page=NULL;free_string(heap[50].s);free_string(heap[51].s);
 puts("Two-slot source: metadata compares raw bits despite matching live string contents, only object reference is released: PASS");
 struct ain_hll_argument arg[3]={0};arg[0].type.data=AIN_REF_ARRAY;arg[1].type.data=arg[2].type.data=AIN_INT;
 struct ain_hll_function legacy={.nr_arguments=3,.arguments=arg};legacy.return_type.data=AIN_VOID;assert(array_erase_function(&legacy)==range);
 legacy.nr_arguments=2;assert(!array_erase_function(&legacy));legacy.return_type.data=AIN_BOOL;assert(!array_erase_function(&legacy));
 puts("Legacy void range preserved; unknown signatures rejected rather than called with wrong ABI: PASS");
 ain_free(actual);puts("Array.Erase production fixture: ALL PASS");return 0;
}
