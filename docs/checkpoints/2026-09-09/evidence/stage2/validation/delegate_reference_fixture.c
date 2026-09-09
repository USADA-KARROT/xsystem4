
#define VM_PRIVATE
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "system4/ain.h"
#include "system4/instructions.h"
#include "system4/little_endian.h"
#include "vm.h"
#include "vm/page.h"
#include "vm/heap.h"
struct ain *ain;
static union vm_value stack_storage[64];
union vm_value *stack=stack_storage;
int32_t stack_ptr,call_stack_ptr;
struct function_call call_stack[4096];
size_t instr_ptr;
static unsigned char *func_flags;
#define FUNC_FLAG_LAMBDA 1
static struct vm_pointer heap_storage[128];
struct vm_pointer *heap=heap_storage;
size_t heap_size=128;
static int refs[128],unrefs[128],frees[128],next_slot;
_Noreturn void _vm_error(const char *fmt,...){abort();}
union vm_value stack_pop(void){assert(stack_ptr>0);return stack[--stack_ptr];}
union vm_value stack_peek(int n){assert(n>=0 && n<stack_ptr);return stack[stack_ptr-1-n];}
bool heap_index_valid(int n){return n>0 && n<128 && heap[n].ref>0;}
void heap_ref(int n){assert(heap_index_valid(n));heap[n].ref++;refs[n]++;}
void variable_fini(union vm_value v,enum ain_data_type type,bool call_dtor);
void heap_unref(int n){
 if(n<=1)return;
 assert(heap_index_valid(n));unrefs[n]++;if(--heap[n].ref)return;
 struct page*p=heap[n].page;heap[n].page=NULL;frees[n]++;
 if(p){
  if(p->type==LOCAL_PAGE)for(int i=p->nr_vars-1;i>=0;i--)variable_fini(p->values[i],ain->functions[p->index].vars[i].type.data,true);
  free(p);
 }
}
void exit_unref(int n){heap_unref(n);}
static struct page *make_page(int slot,enum page_type t,int fno,int count){
 assert(!heap[slot].ref);struct page*p=calloc(1,sizeof(*p)+count*sizeof(union vm_value));assert(p);
 p->type=t;p->index=fno;p->nr_vars=count;heap[slot]=(struct vm_pointer){.ref=1,.type=VM_PAGE,.page=p};return p;
}
struct page *heap_get_delegate_page(int n){assert(heap_index_valid(n));return heap[n].page;}
bool delegate_get(struct page*p,int i,int*obj,int*fn){
 if(i*3>=p->nr_vars)return false;*obj=p->values[i*3].i;*fn=p->values[i*3+1].i;return true;
}
static int _function_call(int fno,int ret){
 int slot=next_slot++;make_page(slot,LOCAL_PAGE,fno,ain->functions[fno].nr_vars);
 call_stack[call_stack_ptr++]=(struct function_call){.fno=fno,.page_slot=slot,.return_address=ret,.struct_page=-1};return slot;
}
static void set_struct_page(int n){call_stack[call_stack_ptr-1].struct_page=n;}
void variable_fini(union vm_value v, enum ain_data_type type, bool call_dtor)
{
	switch (type) {
	case AIN_STRING:
	case AIN_STRUCT:
	case AIN_DELEGATE:
	case AIN_FUNC_TYPE:
	case AIN_ARRAY_TYPE:
	case AIN_ARRAY:
	case AIN_WRAP:
	case AIN_IFACE_WRAP:
	case AIN_OPTION:
	case AIN_REF_TYPE:
	case AIN_IFACE:
		if (v.i == -1)
			break;
		if (call_dtor)
			heap_unref(v.i);
		else
			exit_unref(v.i);
		break;
	default:
		break;
	}
}
static int ain_return_slots_type(struct ain_type *type)
{
	if (ain->version < 14) {
		return (type->data != AIN_VOID) ? 1 : 0;
	}
	switch (type->data) {
	case AIN_VOID:
		return 0;
	// v14 2-slot types (return):
	case AIN_IFACE:      // [struct_page, vtable_offset]
	case AIN_IFACE_WRAP: // "2-value representation" per ain.h comment
	case AIN_OPTION:     // [value, discriminant] — v14 tagged union
		return 2;
	// AIN_REF_TYPE as RETURN type = 1-slot (heap reference/struct page).
	// As a PARAMETER, REF_TYPE is 2-slot (page+slot), but return values
	// only carry the object reference, not a variable binding.
	case AIN_REF_TYPE:
		return 1;
	case AIN_WRAP:
		// v14: wrap<T> slot count varies.
		// Reference wraps (wrap<struct>) = 1 slot (heap ref, -1 for none).
		// Value wraps (wrap<int>, etc.) = 2 slots [value, has_value].
		if (type->struc >= 0)
			return 1;
		if (type->array_type) {
			switch (type->array_type->data) {
			case AIN_STRUCT: case AIN_STRING: case AIN_ARRAY_TYPE:
			case AIN_ARRAY: case AIN_DELEGATE: case AIN_WRAP:
			case AIN_IFACE: case AIN_IFACE_WRAP: case AIN_REF_TYPE:
				return 1;
			default:
				return 2;
			}
		}
		return 2; // default: assume value type (2 slots)
	default:
		return 1;
	}
}
static bool delegate_arg_is_2slot(struct ain_type *type)
{
	switch (type->data) {
	case AIN_IFACE:
	case AIN_OPTION:
	case AIN_IFACE_WRAP:
		return true;
	// v14: ref value types are 2-slot [page, index] on the stack.
	// Only include primitive ref types here — AIN_REF_STRUCT etc. may
	// use 1-slot (heap ref) semantics depending on delegate encoding.
	case AIN_REF_BOOL:
	case AIN_REF_INT:
	case AIN_REF_FLOAT:
	case AIN_REF_LONG_INT:
		return true;
	case AIN_STRUCT:
		return type->struc >= 0 && type->struc < ain->nr_structures
		       && ain->structures[type->struc].is_interface;
	default:
		return false;
	}
}
static int delegate_param_slots(struct ain_function_type *dg)
{
	int slots = 0;
	for (int i = 0; i < dg->nr_arguments; i++) {
		// v14: a 2-slot arg (interface/option) is followed by a void
		// companion slot in the variable list. The bytecode pushes only
		// the 2-slot value (no separate void push), so skip the void
		// companion to match the actual push count.
		if (dg->variables[i].type.data == AIN_VOID
		    && i > 0 && delegate_arg_is_2slot(&dg->variables[i-1].type))
			continue;
		slots += delegate_arg_is_2slot(&dg->variables[i].type) ? 2 : 1;
	}
	return slots;
}
static int delegate_return_slots(struct ain_type *type)
{
	if (type->data == AIN_WRAP)
		return 2;
	return ain_return_slots_type(type);
}
static union vm_value delegate_copy_argument(union vm_value value, enum ain_data_type type)
{
	// Delegate arguments remain on the caller's stack. A v14 callee local
	// page owns its reference parameters, which variable_fini releases on
	// return. Retain the referenced page, preserving the [page, index] alias.
	if (ain->version >= 14) {
		switch (type) {
		case AIN_REF_TYPE:
			if (value.i > 0 && heap_index_valid(value.i))
				heap_ref(value.i);
			break;
		default:
			break;
		}
	}
	return value;
}
static void delegate_call(int dg_no, int return_address)
{
	if (dg_no < 0 || dg_no >= ain->nr_delegates)
		VM_ERROR("Invalid delegate index");
	// stack: [arg0, ..., dg_page, dg_index, [return_value(s)]]
	// v14 2-slot types (AIN_IFACE, AIN_OPTION, AIN_WRAP) use 2 return slots.
	int return_values = delegate_return_slots(&ain->delegates[dg_no].return_type);
	int dg_page = stack_peek(1 + return_values).i;
	int dg_index = stack_peek(0 + return_values).i;
	int obj, fun;
	struct page *dg_pg = heap_get_delegate_page(dg_page);
	if (delegate_get(dg_pg, dg_index, &obj, &fun)) {
		// Guard: skip invalid function numbers
		if (fun < 0 || fun >= ain->nr_functions) {
			// Pop return value(s) first (like the success path does) so
			// we increment dg_index, not the return value slot.
			for (int i = 0; i < return_values; i++)
				stack_pop();
			// increment dg_index to advance past this entry
			stack[stack_ptr - 1].i++;
			// Push back dummy return value(s) to keep stack balanced
			for (int i = 0; i < return_values; i++)
				stack_push(0);
			return;
		}
		// pop previous return value(s)
		for (int i = 0; i < return_values; i++)
			stack_pop();
		// increment dg_index
		stack[stack_ptr - 1].i++;

		int slot = _function_call(fun, instr_ptr + instruction_width(DG_CALL));
		if (unlikely(slot < 0)) return;
		// Set base_sp so function_return won't destroy delegate stack state
		call_stack[call_stack_ptr-1].base_sp = stack_ptr;
		call_stack[call_stack_ptr-1].is_delegate_call = true;
		call_stack[call_stack_ptr-1].dg_return_slots = return_values;
		// copy arguments into local page (slot-aware for multi-slot types)
		struct ain_function_type *dg = &ain->delegates[dg_no];
		if (heap[slot].page) {
			int arg_slots_total = delegate_param_slots(dg);
			int base = 2 + arg_slots_total;
			int vi = 0; // local page variable index (may advance 2 for 2-slot args)
			for (int i = 0; i < dg->nr_arguments && vi < heap[slot].page->nr_vars; i++) {
				bool is2 = delegate_arg_is_2slot(&dg->variables[i].type);
				heap[slot].page->values[vi] = delegate_copy_argument(stack_peek(base - 1), dg->variables[i].type.data);
				if (is2 && vi + 1 < heap[slot].page->nr_vars)
					heap[slot].page->values[vi + 1] = stack_peek(base - 2);
				base -= is2 ? 2 : 1;
				vi += is2 ? 2 : 1;
			}
		}

		set_struct_page(obj);
		// v14: read closure environment from delegate's 3rd slot.
		// dg_index was already incremented, so use (dg_index-1).
		call_stack[call_stack_ptr-1].env_page = 0;
		if (ain->version >= 14 && dg_pg) {
			int orig_idx = dg_index; // dg_index was read before increment
			if (orig_idx * 3 + 2 < dg_pg->nr_vars) {
				call_stack[call_stack_ptr-1].env_page = dg_pg->values[orig_idx * 3 + 2].i;
			}
		}
		// v14 lambda closure via delegate: if obj is invalid (-1/0),
		// search up the call stack for the enclosing method's struct_page.
		if (ain->version >= 14 && obj <= 0) {
			if (func_flags[fun] & FUNC_FLAG_LAMBDA) {
				for (int fr = call_stack_ptr - 2; fr >= 0; fr--) {
					if (call_stack[fr].struct_page > 0
					    && (size_t)call_stack[fr].struct_page < heap_size) {
						set_struct_page(call_stack[fr].struct_page);
						break;
					}
				}
			}
		}
	} else {
		// Save return value(s) — may be 2 slots for v14 2-slot types
		union vm_value r[2] = {{0}, {0}};
		for (int i = return_values - 1; i >= 0; i--)
			r[i] = stack_pop();
		stack_pop(); // dg_index
		stack_pop(); // dg_page
		// Pop delegate arguments. Pop exactly delegate_param_slots()
		// values to match DG_CALLBEGIN's push count. No variable_fini
		// needed — delegate args are borrowed refs without ownership.
		{
			int _arg_slots = delegate_param_slots(&ain->delegates[dg_no]);
			for (int _s = 0; _s < _arg_slots; _s++)
				stack_pop();
		}
		for (int i = 0; i < return_values; i++)
			stack_push(r[i]);
		instr_ptr = get_argument(1);
	}
}
static void delegate_call_before(int dg_no, int return_address)
{
	if (dg_no < 0 || dg_no >= ain->nr_delegates)
		VM_ERROR("Invalid delegate index");
	// stack: [arg0, ..., dg_page, dg_index, [return_value(s)]]
	// v14 2-slot types (AIN_IFACE, AIN_OPTION, AIN_WRAP) use 2 return slots.
	int return_values = delegate_return_slots(&ain->delegates[dg_no].return_type);
	int dg_page = stack_peek(1 + return_values).i;
	int dg_index = stack_peek(0 + return_values).i;
	int obj, fun;
	struct page *dg_pg = heap_get_delegate_page(dg_page);
	if (delegate_get(dg_pg, dg_index, &obj, &fun)) {
		// Guard: skip invalid function numbers
		if (fun < 0 || fun >= ain->nr_functions) {
			// Pop return value(s) first (like the success path does) so
			// we increment dg_index, not the return value slot.
			for (int i = 0; i < return_values; i++)
				stack_pop();
			// increment dg_index to advance past this entry
			stack[stack_ptr - 1].i++;
			// Push back dummy return value(s) to keep stack balanced
			for (int i = 0; i < return_values; i++)
				stack_push(0);
			return;
		}
		// pop previous return value(s)
		for (int i = 0; i < return_values; i++)
			stack_pop();
		// increment dg_index
		stack[stack_ptr - 1].i++;

		int slot = _function_call(fun, instr_ptr + instruction_width(DG_CALL));
		if (unlikely(slot < 0)) return;
		// Set base_sp so function_return won't destroy delegate stack state
		call_stack[call_stack_ptr-1].base_sp = stack_ptr;
		call_stack[call_stack_ptr-1].is_delegate_call = true;
		call_stack[call_stack_ptr-1].dg_return_slots = return_values;
		// copy arguments into local page (slot-aware for multi-slot types)
		struct ain_function_type *dg = &ain->delegates[dg_no];
		if (heap[slot].page) {
			int arg_slots_total = delegate_param_slots(dg);
			int base = 2 + arg_slots_total;
			int vi = 0; // local page variable index (may advance 2 for 2-slot args)
			for (int i = 0; i < dg->nr_arguments && vi < heap[slot].page->nr_vars; i++) {
				bool is2 = delegate_arg_is_2slot(&dg->variables[i].type);
				heap[slot].page->values[vi] = stack_peek(base - 1);
				if (is2 && vi + 1 < heap[slot].page->nr_vars)
					heap[slot].page->values[vi + 1] = stack_peek(base - 2);
				base -= is2 ? 2 : 1;
				vi += is2 ? 2 : 1;
			}
		}

		set_struct_page(obj);
		// v14: read closure environment from delegate's 3rd slot.
		// dg_index was already incremented, so use (dg_index-1).
		call_stack[call_stack_ptr-1].env_page = 0;
		if (ain->version >= 14 && dg_pg) {
			int orig_idx = dg_index; // dg_index was read before increment
			if (orig_idx * 3 + 2 < dg_pg->nr_vars) {
				call_stack[call_stack_ptr-1].env_page = dg_pg->values[orig_idx * 3 + 2].i;
			}
		}
		// v14 lambda closure via delegate: if obj is invalid (-1/0),
		// search up the call stack for the enclosing method's struct_page.
		if (ain->version >= 14 && obj <= 0) {
			if (func_flags[fun] & FUNC_FLAG_LAMBDA) {
				for (int fr = call_stack_ptr - 2; fr >= 0; fr--) {
					if (call_stack[fr].struct_page > 0
					    && (size_t)call_stack[fr].struct_page < heap_size) {
						set_struct_page(call_stack[fr].struct_page);
						break;
					}
				}
			}
		}
	} else {
		// Save return value(s) — may be 2 slots for v14 2-slot types
		union vm_value r[2] = {{0}, {0}};
		for (int i = return_values - 1; i >= 0; i--)
			r[i] = stack_pop();
		stack_pop(); // dg_index
		stack_pop(); // dg_page
		// Pop delegate arguments. Pop exactly delegate_param_slots()
		// values to match DG_CALLBEGIN's push count. No variable_fini
		// needed — delegate args are borrowed refs without ownership.
		{
			int _arg_slots = delegate_param_slots(&ain->delegates[dg_no]);
			for (int _s = 0; _s < _arg_slots; _s++)
				stack_pop();
		}
		for (int i = 0; i < return_values; i++)
			stack_push(r[i]);
		instr_ptr = get_argument(1);
	}
}

static void setup(void){
 memset(heap,0,sizeof(heap_storage));memset(refs,0,sizeof(refs));memset(unrefs,0,sizeof(unrefs));memset(frees,0,sizeof(frees));
 stack_ptr=call_stack_ptr=0;next_slot=20;instr_ptr=0x4a59ea;
 make_page(10,LOCAL_PAGE,20752,1);struct page*d=make_page(11,DELEGATE_PAGE,0,6);
 for(int i=0;i<2;i++){d->values[i*3].i=12;d->values[i*3+1].i=36081;d->values[i*3+2].i=13;}
 make_page(12,STRUCT_PAGE,0,0);make_page(13,LOCAL_PAGE,27031,4);
 call_stack[call_stack_ptr++]=(struct function_call){.fno=20752,.page_slot=10,.struct_page=12};
 stack_push(123456);stack_push(10);stack_push(0);stack_push(11);stack_push(0);
}
static void callback_and_cleanup(void){
 int slot=call_stack[call_stack_ptr-1].page_slot;struct page*p=heap[slot].page;
 assert(p->index==36081 && p->nr_vars==2 && p->values[0].i==10 && p->values[1].i==0);
 /* The real trace verifies X_ASSIGN performs this alias write correctly. */
 heap[p->values[0].i].page->values[p->values[1].i].i=1;
 call_stack_ptr--;heap_unref(slot);
}
static void cleanup(void){for(int i=10;i<=13;i++)if(heap[i].ref)heap_unref(i);}
int main(int argc,char**argv){
 assert(argc==2);int err=0;ain=ain_open(argv[1],&err);assert(ain && ain->version>=14);
 initialize_instructions(ain->version);func_flags=calloc(ain->nr_functions,1);assert(func_flags);
 assert(ain->functions[20752].nr_vars==1 && ain->functions[20752].vars[0].type.data==AIN_BOOL);
 assert(ain->functions[36081].nr_args==2 && ain->functions[36081].vars[0].type.data==AIN_REF_BOOL && ain->functions[36081].vars[1].type.data==AIN_VOID);
 assert(ain->delegates[248].nr_arguments==2 && ain->delegates[248].variables[0].type.data==AIN_REF_BOOL && ain->delegates[248].variables[1].type.data==AIN_VOID);
 printf("Actual AIN f36081/ref-bool + void, delegate248/ref-bool + void, caller20752/bool verified\n");
 setup();delegate_call_before(248,0x4a59fa);assert(heap[10].ref==1);callback_and_cleanup();assert(!heap[10].ref && !heap[10].page && frees[10]==1);cleanup();
 puts("Before production delegate_call: End alias writes 1, callee variable_fini releases active caller page: reproduced");
 setup();
 for(int i=0;i<2;i++){
  delegate_call(248,0x4a59fa);assert(heap[10].ref==2 && refs[10]==i+1 && stack_ptr==5);
  assert(call_stack[call_stack_ptr-1].env_page==13);
  callback_and_cleanup();assert(heap[10].ref==1 && heap[10].page->values[0].i==1 && frees[10]==0);
 }
 instr_ptr=0x4a59ea;delegate_call(248,0x4a59fa);assert(stack_ptr==1 && stack[0].i==123456 && instr_ptr==0x4a59fa);
 assert(heap[10].ref==1 && refs[10]==2 && unrefs[10]==2 && refs[13]==0 && unrefs[13]==0);
 cleanup();assert(frees[10]==1);puts("After production delegate_call: both handlers retain same ref target; End survives cleanup; counts balance; delegate final stack cleanup preserves caller: PASS");
 setup();int types[]={AIN_REF_BOOL,AIN_REF_INT,AIN_REF_FLOAT,AIN_REF_LONG_INT,AIN_REF_STRING,AIN_REF_STRUCT,AIN_REF_ARRAY};
 for(unsigned i=0;i<sizeof(types)/sizeof(types[0]);i++){
  union vm_value v=delegate_copy_argument((union vm_value){.i=10},types[i]);assert(v.i==10 && heap[10].ref==2);
  variable_fini(v,types[i],true);assert(heap[10].ref==1);
 }
 int nrefs=refs[10];assert(delegate_copy_argument((union vm_value){.i=10},AIN_VOID).i==10);assert(delegate_copy_argument((union vm_value){.i=10},AIN_INT).i==10);assert(refs[10]==nrefs);
 assert(delegate_copy_argument((union vm_value){.i=-1},AIN_REF_BOOL).i==-1);assert(delegate_copy_argument((union vm_value){.i=0},AIN_REF_BOOL).i==0);
 int version=ain->version;ain->version=13;assert(delegate_copy_argument((union vm_value){.i=10},AIN_REF_BOOL).i==10 && refs[10]==nrefs);ain->version=version;
 cleanup();puts("Reference family aliases/refcounts, primitive and companion values, null references, unchanged pre-v14 path: PASS");
 free(func_flags);ain_free(ain);puts("Delegate reference production fixture: ALL PASS");return 0;
}
