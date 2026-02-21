/* Copyright (C) 2019 Nunuhara Cabbage <nunuhara@haniwa.technology>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see <http://gnu.org/licenses/>.
 */

#define VM_PRIVATE

#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <time.h>
#include <setjmp.h>
#include <assert.h>
#include <signal.h>
#include <execinfo.h>
#include <SDL.h> // for system.MsgBox

#include "system4.h"
#include "system4/ain.h"
#include "system4/instructions.h"
#include "system4/file.h"
#include "system4/little_endian.h"
#include "system4/string.h"
#include "system4/utfsjis.h"

#include "debugger.h"
#include "input.h"
#include "savedata.h"
#include "vm.h"
#include "vm/heap.h"
#include "vm/page.h"
#include "xsystem4.h"

static inline int32_t lint_clamp(int64_t n)
{
	if (n < 0)
		return 0;
	if (n > INT32_MAX)
		return INT32_MAX;
	return (int32_t)n;
}

#define INITIAL_STACK_SIZE 4096

// When the IP is set to VM_RETURN, the VM halts
#define VM_RETURN 0xFFFFFFFF

static unsigned long long insn_count = 0;

// Circular trace buffer for last N instructions
#define TRACE_BUF_SIZE 32
static struct { size_t ip; uint16_t op; int32_t sp; } trace_buf[TRACE_BUF_SIZE];
static int trace_buf_idx = 0;

static void dump_trace_buf(void)
{
	WARNING("=== Last %d instructions ===", TRACE_BUF_SIZE);
	for (int k = 0; k < TRACE_BUF_SIZE; k++) {
		int idx = (trace_buf_idx + k) % TRACE_BUF_SIZE;
		if (trace_buf[idx].ip == 0 && trace_buf[idx].op == 0)
			continue;
		WARNING("  [%02d] ip=0x%lX op=0x%04X (%s) sp=%d",
			k, (unsigned long)trace_buf[idx].ip, trace_buf[idx].op,
			trace_buf[idx].op < NR_OPCODES ? instructions[trace_buf[idx].op].name : "?",
			trace_buf[idx].sp);
	}
}

/*
 * NOTE: The current implementation is a simple bytecode interpreter.
 *       System40.exe uses a JIT compiler, and we should too.
 */

// The stack
union vm_value *stack = NULL; // the stack
int32_t stack_ptr = 0;        // pointer to the top of the stack
static size_t stack_size;     // current size of the stack

// Stack of function call frames
struct function_call call_stack[4096];
int32_t call_stack_ptr = 0;

struct ain *ain;
size_t instr_ptr = 0;

bool vm_reset_once = false;

// Read the opcode at ADDR.
static int16_t get_opcode(size_t addr)
{
	return LittleEndian_getW(ain->code, addr);
}

static const char *current_instruction_name(void)
{
	int16_t opcode = get_opcode(instr_ptr);
	if (opcode >= 0 && opcode < NR_OPCODES)
		return instructions[opcode].name;
	return "UNKNOWN OPCODE";
}

static int local_page_slot(void)
{
	return call_stack[call_stack_ptr-1].page_slot;
}

struct page *local_page(void)
{
	return heap[local_page_slot()].page;
}

struct page *get_local_page(int frame_no)
{
	if (frame_no < 0 || frame_no >= call_stack_ptr)
		return NULL;
	int slot = call_stack[call_stack_ptr - (frame_no + 1)].page_slot;
	return slot < 1 ? NULL : heap[slot].page;
}

union vm_value local_get(int varno)
{
	return local_page()->values[varno];
}

static void local_set(int varno, int32_t value)
{
	local_page()->values[varno].i = value;
}

static union vm_value *local_ptr(int varno)
{
	return local_page()->values + varno;
}

struct page *global_page(void)
{
	return heap[0].page;
}

union vm_value global_get(int varno)
{
	return heap[0].page->values[varno];
}

void global_set(int varno, union vm_value val, bool call_dtors)
{
	switch (ain->globals[varno].type.data) {
	case AIN_STRING:
	case AIN_STRUCT:
	case AIN_ARRAY_TYPE:
		if (heap[0].page->values[varno].i > 0) {
			if (call_dtors)
				heap_unref(heap[0].page->values[varno].i);
			else
				exit_unref(heap[0].page->values[varno].i);
		}
	default:
		break;
	}
	heap[0].page->values[varno] = val;
}

static int32_t struct_page_slot(void)
{
	return call_stack[call_stack_ptr-1].struct_page;
}

static struct page *struct_page(void)
{
	return heap[struct_page_slot()].page;
}

struct page *get_struct_page(int frame_no)
{
	if (frame_no < 0 || frame_no >= call_stack_ptr)
		return NULL;
	int slot = call_stack[call_stack_ptr - (frame_no + 1)].struct_page;
	return slot < 1 ? NULL : heap[slot].page;
}

union vm_value member_get(int varno)
{
	return struct_page()->values[varno];
}

static void member_set(int varno, int32_t value)
{
	struct_page()->values[varno].i = value;
}

union vm_value stack_peek(int n)
{
	return stack[stack_ptr - (1 + n)];
}

union vm_value stack_pop(void)
{
	stack_ptr--;
	if (unlikely(stack_ptr < 0)) {
		static int underflow_count = 0;
		if (underflow_count < 3) {
			WARNING("STACK UNDERFLOW: sp=%d ip=0x%lX fno=%d insn#%llu",
				stack_ptr, (unsigned long)instr_ptr,
				call_stack_ptr > 0 ? call_stack[call_stack_ptr-1].fno : -1,
				insn_count);
			dump_trace_buf();
			underflow_count++;
		}
		stack_ptr = 0;
		return (union vm_value){.i = 0};
	}
	return stack[stack_ptr];
}

static union vm_value *stack_peek_ptr(int n)
{
	return &stack[stack_ptr - (1 + n)];
}

// Pop a reference off the stack, returning the address of the referenced object.
// dummy_var is an array to absorb multi-slot writes (e.g. X_ASSIGN with n > 1)
// that may occur when stack_pop_var returns an error sentinel.
static union vm_value dummy_var[64];
static union vm_value *stack_pop_var(void)
{
	int32_t page_index = stack_pop().i;
	int32_t heap_index = stack_pop().i;
	if (unlikely(!heap_index_valid(heap_index))) {
		static int inv_warn = 0;
		if (inv_warn++ < 5)
			WARNING("stack_pop_var: invalid heap index %d/%d", heap_index, page_index);
		memset(dummy_var, 0, sizeof(dummy_var));
		return dummy_var;
	}
	if (unlikely(!heap[heap_index].page || page_index < 0 || page_index >= heap[heap_index].page->nr_vars)) {
		static int oob_trace = 0;
		WARNING("stack_pop_var: out of bounds %d/%d (nr_vars=%d) ip=0x%lX fno=%d sp=%d page_type=%d",
			heap_index, page_index,
			heap[heap_index].page ? heap[heap_index].page->nr_vars : -1,
			(unsigned long)instr_ptr,
			call_stack_ptr > 0 ? call_stack[call_stack_ptr-1].fno : -1,
			stack_ptr,
			heap[heap_index].page ? heap[heap_index].page->type : -1);
		if (oob_trace++ < 1)
			dump_trace_buf();
		memset(dummy_var, 0, sizeof(dummy_var));
		return dummy_var;
	}
	return &heap[heap_index].page->values[page_index];
}

union vm_value *stack_peek_var(void)
{
	int32_t page_index = stack_peek(0).i;
	int32_t heap_index = stack_peek(1).i;
	if (unlikely(!heap_index_valid(heap_index))) {
		WARNING("stack_peek_var: invalid heap index %d/%d", heap_index, page_index);
		memset(dummy_var, 0, sizeof(dummy_var));
		return dummy_var;
	}
	if (unlikely(!heap[heap_index].page || page_index < 0 || page_index >= heap[heap_index].page->nr_vars)) {
		WARNING("stack_peek_var: out of bounds %d/%d", heap_index, page_index);
		memset(dummy_var, 0, sizeof(dummy_var));
		return dummy_var;
	}
	return &heap[heap_index].page->values[page_index];
}

static void stack_push_string(struct string *s)
{
	int32_t heap_slot = heap_alloc_slot(VM_STRING);
	heap[heap_slot].s = s;
	stack[stack_ptr++].i = heap_slot;
}

static struct string *stack_peek_string(int n)
{
	int slot = stack_peek(n).i;
	if (slot < 0 || (size_t)slot >= heap_size || !heap[slot].s)
		return &EMPTY_STRING;
	return heap[slot].s;
}

int vm_string_ref(struct string *s)
{
	if (!s) {
		int slot = heap_alloc_slot(VM_STRING);
		heap[slot].s = string_ref(&EMPTY_STRING);
		return slot;
	}
	int slot = heap_alloc_slot(VM_STRING);
	heap[slot].s = string_ref(s);
	return slot;
}

int vm_copy_page(struct page *page)
{
	int slot = heap_alloc_slot(VM_PAGE);
	heap_set_page(slot, copy_page(page));
	return slot;
}

union vm_value vm_copy(union vm_value v, enum ain_data_type type)
{
	switch (type) {
	case AIN_STRING:
		if (v.i < 0 || (size_t)v.i >= heap_size || !string_index_valid(v.i))
			return v;
		return (union vm_value) { .i = vm_string_ref(heap_get_string(v.i)) };
	case AIN_STRUCT:
	case AIN_DELEGATE:
	case AIN_ARRAY_TYPE:
		if (v.i < 0 || (size_t)v.i >= heap_size || !page_index_valid(v.i))
			return v;
		return (union vm_value) { .i = vm_copy_page(heap_get_page(v.i)) };
	case AIN_REF_TYPE:
		if (v.i > 0 && (size_t)v.i < heap_size)
			heap_ref(v.i);
		return v;
	default:
		return v;
	}
}

static int get_function_by_name(const char *name)
{
	for (int i = 0; i < ain->nr_functions; i++) {
		if (!strcmp(name, ain->functions[i].name))
			return i;
	}
	return -1;
}

static int scenario_label_addr(const char *lname)
{
	for (int i = 0; i < ain->nr_scenario_labels; i++) {
		if (!strcmp(ain->scenario_labels[i].name, lname)) {
			return ain->scenario_labels[i].address;
		}
	}
	VM_ERROR("Invalid scenario label: %s", display_sjis0(lname));
}

static int alloc_scenario_page(const char *fname)
{
	int fno, slot;
	struct ain_function *f;

	if ((fno = get_function_by_name(fname)) < 0)
		VM_ERROR("Invalid scenario function: %s", display_sjis0(fname));
	f = &ain->functions[fno];

	slot = heap_alloc_slot(VM_PAGE);
	heap_set_page(slot, alloc_page(LOCAL_PAGE, fno, f->nr_vars));
	for (int i = 0; i < f->nr_vars; i++) {
		heap[slot].page->values[i] = variable_initval(f->vars[i].type.data);
	}
	return slot;
}

static void scenario_jump(int address)
{
	// flush call stack
	for (int i = call_stack_ptr - 1; i >= 0; i--) {
		heap_unref(call_stack[i].page_slot);
	}
	call_stack_ptr = 0;
	instr_ptr = address;
}

static void scenario_call(int slot)
{
	int fno = heap[slot].page->index;
	// flush call stack
	for (int i = call_stack_ptr - 1; i >= 0; i--) {
		heap_unref(call_stack[i].page_slot);
	}
	call_stack[0] = (struct function_call) {
		.fno = fno,
		.call_address = instr_ptr,
		.return_address = VM_RETURN,
		.page_slot = slot,
		.struct_page = -1,
	};
	call_stack_ptr = 1;
	instr_ptr = ain->functions[fno].address;
}

/*
 * System 4 calling convention:
 *   - caller pushes arguments, in order
 *   - CALLFUNC creates stack frame, pops arguments into local page
 *   - callee pushes return value on the stack
 *   - RETURN jumps to return address (saved in stack frame)
 */
static int call_depth = 0;

static int _function_call(int fno, int return_address)
{
	call_depth++;
	struct ain_function *f = &ain->functions[fno];
	int slot = heap_alloc_slot(VM_PAGE);
	heap_set_page(slot, alloc_page(LOCAL_PAGE, fno, f->nr_vars));
	heap[slot].page->local.struct_ptr = -1;
	call_stack[call_stack_ptr++] = (struct function_call) {
		.fno = fno,
		.call_address = instr_ptr,
		.return_address = return_address,
		.page_slot = slot,
		.struct_page = -1,
		.saved_sp = -1, // set after args are popped
		.dg_page = -1,
	};
	// initialize local variables
	for (int i = f->nr_args; i < f->nr_vars; i++) {
		heap[slot].page->values[i] = variable_initval(f->vars[i].type.data);
		if (ain->version <= 1 && f->vars[i].type.data == AIN_STRUCT) {
			create_struct(f->vars[i].type.struc, &heap[slot].page->values[i]);
		}
	}
	// jump to function start
	instr_ptr = ain->functions[fno].address;

	return slot;
}

static void function_call(int fno, int return_address)
{
	int slot = _function_call(fno, return_address);

	// pop arguments, store in local page
	struct ain_function *f = &ain->functions[fno];
	for (int i = f->nr_args - 1; i >= 0; i--) {
		heap[slot].page->values[i] = stack_pop();
		switch (f->vars[i].type.data) {
		case AIN_REF_TYPE:
		case AIN_STRING:
		case AIN_STRUCT:
		case AIN_DELEGATE:
		case AIN_ARRAY_TYPE:
		case AIN_ARRAY:
		case AIN_WRAP:
			if (heap[slot].page->values[i].i != -1)
				heap_ref(heap[slot].page->values[i].i);
			break;
		default:
			break;
		}
	}
	// Save stack pointer for leak detection
	call_stack[call_stack_ptr-1].saved_sp = stack_ptr;
}

static void function_return(void);

static void method_call(int fno, int return_address)
{
	function_call(fno, return_address);
	int struct_page = stack_pop().i;
	// Update saved_sp after struct_page pop (function_call saved it before)
	call_stack[call_stack_ptr-1].saved_sp = stack_ptr;
	if (struct_page > 0 && !page_index_valid(struct_page)) {
		static int bad_sp_warn = 0;
		if (bad_sp_warn++ < 10) {
			int caller_fno = call_stack_ptr >= 2 ? call_stack[call_stack_ptr-2].fno : -1;
			WARNING("method_call: invalid struct_page %d for func#%d (caller=func#%d ip=0x%lX) heap_type=%d ref=%d — bailing out",
				struct_page, fno, caller_fno, (unsigned long)call_stack[call_stack_ptr-1].call_address,
				(struct_page >= 0 && (size_t)struct_page < heap_size) ? heap[struct_page].type : -99,
				(struct_page >= 0 && (size_t)struct_page < heap_size) ? heap[struct_page].ref : -99);
		}
		// Bail out: don't execute with corrupted struct_page
		// Push return value(s) if non-void, then immediately return
		{
			int rtype = ain->functions[fno].return_type.data;
			int bail_ret = 0;
			if (rtype != AIN_VOID) {
				bail_ret = 1;
				if (ain->version >= 14 && rtype == AIN_OPTION) {
					bail_ret = 2;
				} else if (ain->version >= 14 && rtype == AIN_WRAP) {
					struct ain_type *ws = ain->functions[fno].return_type.array_type;
					bool prim = ws && (ws->data == AIN_INT || ws->data == AIN_BOOL ||
						ws->data == AIN_FLOAT || ws->data == AIN_LONG_INT ||
						ws->data == AIN_ENUM || ws->data == AIN_ENUM2 ||
						ws->data == AIN_IFACE || ws->data == AIN_IFACE_WRAP);
					bail_ret = prim ? 2 : 1;
				}
			}
			for (int i = 0; i < bail_ret; i++)
				stack_push(-1);
		}
		function_return();
		return;
	}
	call_stack[call_stack_ptr-1].struct_page = struct_page;
	heap[call_stack[call_stack_ptr-1].page_slot].page->local.struct_ptr = struct_page;
}

static void vm_execute(void);

static void delegate_call(int dg_no, int return_address)
{
	if (dg_no < 0 || dg_no >= ain->nr_delegates)
		VM_ERROR("Invalid delegate index");

	if (ain->version >= 14) {
		// v14: dg_page/dg_index stored off-stack in caller's call frame
		struct ain_function_type *dg = &ain->delegates[dg_no];
		// Compute return slot count (AIN_OPTION=2, AIN_WRAP<prim>=2, else 1/0)
		int has_return;
		if (dg->return_type.data == AIN_VOID) {
			has_return = 0;
		} else if (dg->return_type.data == AIN_OPTION) {
			has_return = 2;
		} else if (dg->return_type.data == AIN_WRAP) {
			struct ain_type *ws = dg->return_type.array_type;
			bool prim = ws && (ws->data == AIN_INT || ws->data == AIN_BOOL ||
				ws->data == AIN_FLOAT || ws->data == AIN_LONG_INT ||
				ws->data == AIN_ENUM || ws->data == AIN_ENUM2 ||
				ws->data == AIN_IFACE || ws->data == AIN_IFACE_WRAP);
			has_return = prim ? 2 : 1;
		} else {
			has_return = 1;
		}
		// The caller's frame (which ran DG_CALLBEGIN) is at call_stack_ptr-1
		struct function_call *caller = &call_stack[call_stack_ptr - 1];

		// Pop return value(s) if non-void
		union vm_value ret_vals[2] = {{0}, {0}};
		for (int r = has_return - 1; r >= 0; r--) {
			ret_vals[r] = stack_pop();
		}

		int obj, fun;
		if (delegate_get(heap_get_delegate_page(caller->dg_page),
				caller->dg_index, &obj, &fun)) {
			if (fun < 0 || fun >= ain->nr_functions) {
				WARNING("delegate_call: invalid function number %d, skipping", fun);
				caller->dg_index++;
				for (int r = 0; r < has_return; r++)
					stack_push(ret_vals[r]);
				return;
			}
			caller->dg_index++;

			// Push saved args back to stack (function will consume them)
			for (int i = 0; i < caller->dg_nr_args; i++) {
				stack_push(caller->dg_args[i]);
			}

			int slot = _function_call(fun, instr_ptr + instruction_width(DG_CALL));

			// Copy args from stack into local page
			for (int i = 0; i < dg->nr_arguments && i < caller->dg_nr_args; i++) {
				union vm_value arg = stack[stack_ptr - caller->dg_nr_args + i];
				heap[slot].page->values[i] = vm_copy(arg, dg->variables[i].type.data);
			}

			// Pop delegate args from stack (they've been copied to local page)
			stack_ptr -= caller->dg_nr_args;
			call_stack[call_stack_ptr-1].saved_sp = stack_ptr;

			call_stack[call_stack_ptr-1].struct_page = obj;
		} else {
			// No more entries: push return value(s) and jump to exit
			for (int r = 0; r < has_return; r++) {
				stack_push(ret_vals[r]);
			}
			instr_ptr = return_address;
		}
		return;
	}

	// Pre-v14: dg_page and dg_index on the value stack
	// stack: [arg0, ..., dg_page, dg_index, [return_value]]
	int return_values = (ain->delegates[dg_no].return_type.data != AIN_VOID) ? 1 : 0;
	int dg_page = stack_peek(1 + return_values).i;
	int dg_index = stack_peek(0 + return_values).i;
	int obj, fun;
	if (delegate_get(heap_get_delegate_page(dg_page), dg_index, &obj, &fun)) {
		if (fun < 0 || fun >= ain->nr_functions) {
			WARNING("delegate_call: invalid function number %d, skipping", fun);
			stack[stack_ptr - 1].i++;
			return;
		}
		if (ain->delegates[dg_no].return_type.data != AIN_VOID) {
			stack_pop();
		}
		stack[stack_ptr - 1].i++;

		int slot = _function_call(fun, instr_ptr + instruction_width(DG_CALL));

		struct ain_function_type *dg = &ain->delegates[dg_no];
		for (int i = 0; i < dg->nr_arguments; i++) {
			union vm_value arg = stack_peek((dg->nr_arguments + 1) - i);
			heap[slot].page->values[i] = vm_copy(arg, dg->variables[i].type.data);
		}

		call_stack[call_stack_ptr-1].struct_page = obj;
	} else {
		union vm_value r;
		if (return_values) {
			r = stack_pop();
		}
		stack_pop(); // dg_index
		stack_pop(); // dg_page
		for (int i = ain->delegates[dg_no].nr_variables - 1; i >= 0; i--) {
			variable_fini(stack_pop(), ain->delegates[dg_no].variables[i].type.data, true);
		}
		if (return_values) {
			stack_push(r);
		}
		instr_ptr = get_argument(1);
	}
}

void vm_call(int fno, int struct_page)
{
	size_t saved_ip = instr_ptr;
	if (struct_page < 0) {
		function_call(fno, VM_RETURN);
	} else {
		stack_push(struct_page);
		method_call(fno, VM_RETURN);
	}
	vm_execute();
	instr_ptr = saved_ip;
}

// Call a function with args already on the stack, WITHOUT popping them.
// The args are copied into the local page but remain on the stack for
// the bytecode to consume directly (used by v14 HLL callback calling convention).
void vm_call_nopop(int fno, int nargs)
{
	size_t saved_ip = instr_ptr;
	struct ain_function *f = &ain->functions[fno];
	int slot = _function_call(fno, VM_RETURN);

	// Copy args from stack to local page WITHOUT popping
	// Stack layout: [arg0, arg1, ..., argN-1] with arg0 at sp-nargs
	for (int i = 0; i < nargs && i < f->nr_args; i++) {
		heap[slot].page->values[i] = stack[stack_ptr - nargs + i];
		// heap_ref for ref types
		switch (f->vars[i].type.data) {
		case AIN_REF_TYPE:
		case AIN_STRING:
		case AIN_STRUCT:
		case AIN_DELEGATE:
		case AIN_ARRAY_TYPE:
		case AIN_ARRAY:
		case AIN_WRAP:
			if (heap[slot].page->values[i].i != -1)
				heap_ref(heap[slot].page->values[i].i);
			break;
		default:
			break;
		}
	}

	vm_execute();
	instr_ptr = saved_ip;
}

static void function_return(void)
{
	call_depth--;
	int fno = call_stack[call_stack_ptr-1].fno;
	int page_slot = call_stack[call_stack_ptr-1].page_slot;
	size_t ret_addr = call_stack[call_stack_ptr-1].return_address;
	int saved_sp = call_stack[call_stack_ptr-1].saved_sp;
	// Stack leak detection
	if (saved_sp >= 0) {
		struct ain_type *ret = &ain->functions[fno].return_type;
		int ret_type = ret->data;
		int has_retval;
		if (ret_type == AIN_VOID) {
			has_retval = 0;
		} else if (ain->version >= 14 && ret_type == AIN_OPTION) {
			// v14: option returns are always 2-slot [value, discriminant]
			has_retval = 2;
		} else if (ain->version >= 14 && ret_type == AIN_WRAP) {
			// v14: wrap<primitive> returns are 2-slot, wrap<non-primitive> are 1-slot
			struct ain_type *ws = ret->array_type;
			bool prim = ws && (ws->data == AIN_INT || ws->data == AIN_BOOL ||
				ws->data == AIN_FLOAT || ws->data == AIN_LONG_INT ||
				ws->data == AIN_ENUM || ws->data == AIN_ENUM2 ||
				ws->data == AIN_IFACE || ws->data == AIN_IFACE_WRAP);
			has_retval = prim ? 2 : 1;
		} else {
			has_retval = 1;
		}
		int expected = saved_sp + has_retval;
		int delta = stack_ptr - expected;
		if (delta > 0) {
			// Positive delta = stack growing
			// In v14, non-void functions often return ref+value (e.g., 4 slots
			// for a 2-slot value type) as part of the assignment-return pattern.
			// Only auto-fix void functions; for non-void, trust the bytecode.
			static int pos_warn = 0;
			if (pos_warn++ < 50) {
				uint32_t call_addr = call_stack[call_stack_ptr-1].call_address;
				int call_opcode = (call_addr < (size_t)ain->code_size) ?
					LittleEndian_getW(ain->code, call_addr) : -1;
				int caller_fno = (call_stack_ptr >= 2) ? call_stack[call_stack_ptr-2].fno : -1;
				WARNING("STACK GROW: func#%d(%s) delta=+%d saved_sp=%d expected=%d actual=%d ret_type=%d wrap_sub=%d call_op=0x%x caller=func#%d(%s) nr_args=%d",
					fno, ain->functions[fno].name, delta, saved_sp, expected, stack_ptr,
					ain->functions[fno].return_type.data,
					ret->array_type ? ret->array_type->data : -1,
					call_opcode, caller_fno,
					(caller_fno >= 0 && caller_fno < ain->nr_functions) ? ain->functions[caller_fno].name : "?",
					ain->functions[fno].nr_args);
			}
			if (ret_type == AIN_VOID) {
				// Void function: no return value to preserve, safe to chop
				stack_ptr = expected;
			}
			// Non-void: do NOT trim — v14 bytecode uses ref+value return patterns
		} else if (delta < 0) {
			// Negative delta = stack shrinking = less critical
			static int neg_warn = 0;
			if (neg_warn++ < 10)
				WARNING("STACK SHRINK: func#%d(%s) delta=%d ret_type=%d wrap_sub=%d",
					fno, ain->functions[fno].name, delta,
					ain->functions[fno].return_type.data,
					ret->array_type ? ret->array_type->data : -1);
			// Auto-fix: pad stack to expected level
			while (stack_ptr < expected)
				stack_push(0);
		}
	}
	heap_unref(page_slot);
	instr_ptr = ret_addr;
	call_stack_ptr--;
}

static const SDL_MessageBoxButtonData ok_cancel_buttons[] = {
	{ SDL_MESSAGEBOX_BUTTON_RETURNKEY_DEFAULT, 1, "OK" },
	{ SDL_MESSAGEBOX_BUTTON_ESCAPEKEY_DEFAULT, 0, "Cancel" },
};

static const SDL_MessageBoxButtonData stop_continue_buttons[] = {
	{ SDL_MESSAGEBOX_BUTTON_RETURNKEY_DEFAULT, 1, "Stop" },
	{ 0, 0, "Continue" },
};

static struct string *get_func_stack_name(int index)
{
	int i = call_stack_ptr - (1 + index);
	if (i < 0 || i >= call_stack_ptr) {
		return string_ref(&EMPTY_STRING);
	}
	struct function_call *call = &call_stack[i];
	struct ain_function *fun = &ain->functions[call->fno];
	return cstr_to_string(fun->name);
}

static void system_call(enum syscall_code code)
{
	switch (code) {
	case SYS_EXIT: {// system.Exit(int nResult)
		vm_exit(stack_pop().i);
		break;
	}
	case SYS_GLOBAL_SAVE: { // system.GlobalSave(string szKeyName, string szFileName)
		int filename = stack_pop().i;
		int keyname = stack_pop().i;
		stack_push(save_globals(heap_get_string(keyname)->text, heap_get_string(filename)->text, NULL, NULL));
		heap_unref(filename);
		heap_unref(keyname);
		break;
	}
	case SYS_GLOBAL_LOAD: { // system.GlobalLoad(string szKeyName, string szFileName)
		int filename = stack_pop().i;
		int keyname = stack_pop().i;
		stack_push(load_globals(heap_get_string(keyname)->text, heap_get_string(filename)->text, NULL, NULL));
		heap_unref(filename);
		heap_unref(keyname);
		break;
	}
	case SYS_LOCK_PEEK: // system.LockPeek(void)
	case SYS_UNLOCK_PEEK: {// system.UnlockPeek(void)
		stack_push(1);
		break;
	}
	case SYS_RESET: {
		vm_reset();
		break;
	}
	case SYS_OUTPUT: {// system.Output(string szText)
		struct string *str = stack_peek_string(0);
		log_message("stdout", "%s", display_sjis0(str->text));
		// XXX: caller S_POPs
		break;
	}
	case SYS_MSGBOX: {
		struct string *str = stack_peek_string(0);
		char *utf = sjis2utf(str->text, str->size);
		SDL_ShowSimpleMessageBox(0, "xsystem4", utf, NULL);
		free(utf);
		// XXX: caller S_POPs
		break;
	}
	case SYS_MSGBOX_OK_CANCEL: {
		int result = 0;
		struct string *str = stack_peek_string(0);
		char *utf = sjis2utf(str->text, str->size);

		const SDL_MessageBoxData mbox = {
			SDL_MESSAGEBOX_INFORMATION,
			NULL,
			"xsystem4",
			utf,
			SDL_arraysize(ok_cancel_buttons),
			ok_cancel_buttons,
			NULL
		};
		if (SDL_ShowMessageBox(&mbox, &result)) {
			WARNING("Error displaying message box");
		}
		free(utf);
		heap_unref(stack_pop().i);
		stack_push(result);
		break;
	}
	case SYS_RESUME_SAVE: {
		union vm_value *success = stack_pop_var();
		struct string *filename = stack_peek_string(0);
		struct string *keyname = stack_peek_string(1);
		success->i = vm_save_image(keyname->text, filename->text);
		heap_unref(stack_pop().i);
		heap_unref(stack_pop().i);
		stack_push(1);
		break;
	}
	case SYS_RESUME_LOAD: {
		int filename_slot = stack_pop().i;
		int key_slot = stack_pop().i;
		vm_load_image(heap_get_string(key_slot)->text, heap_get_string(filename_slot)->text);
		stack_push(0);
		break;
	}
	case SYS_EXISTS_FILE: { // system.ExistsFile(string szFileName)
		int str = stack_pop().i;
		char *path = gamedir_path(heap_get_string(str)->text);
		stack_push(file_exists(path));
		heap_unref(str);
		free(path);
		break;
	}
	case SYS_OPEN_WEB: {
#if !SDL_VERSION_ATLEAST(2, 0, 14)
		WARNING("SDL_OpenURL not available");
#else
		struct string *url = stack_peek_string(0);
		if (SDL_OpenURL(url->text) < 0) {
			WARNING("SDL_OpenURL failed: %s", SDL_GetError());
		}
#endif
		heap_unref(stack_pop().i);
		break;
	};
	case SYS_GET_SAVE_FOLDER_NAME: {// system.GetSaveFolderName(void)
		if (config.save_dir) {
			char *sjis = utf2sjis(config.save_dir, strlen(config.save_dir));
			stack_push_string(make_string(sjis, strlen(sjis)));
			free(sjis);
		} else {
			stack_push_string(string_ref(&EMPTY_STRING));
		}
		break;
	}
	case SYS_GET_TIME: {// system.GetTime(void)
		stack_push(vm_time());
		break;
	}
	case SYS_GET_GAME_NAME: {// system.GetGameName(void)
		stack_push_string(make_string(config.game_name, strlen(config.game_name)));
		break;
	}
	case SYS_ERROR: {// system.Error(string szText)
		int result = 0;
		struct string *str = stack_peek_string(0);
		char *utf = sjis2utf(str->text, str->size);
		sys_warning("*GAME ERROR*: %s\n", utf);
		const SDL_MessageBoxData mbox = {
			SDL_MESSAGEBOX_ERROR,
			NULL,
			"Game Error - xsystem4",
			utf,
			SDL_arraysize(stop_continue_buttons),
			stop_continue_buttons,
			NULL
		};
		if (SDL_ShowMessageBox(&mbox, &result)) {
			WARNING("Error displaying message box");
		}
		free(utf);
		if (result == 1) {
			// stop execution
			vm_exit(1);
		}
		// XXX: caller S_POPs
		break;
	}
	case SYS_EXISTS_SAVE_FILE: {
		int slot = stack_pop().i;
		char *path = savedir_path(heap_get_string(slot)->text);
		stack_push(file_exists(path));
		heap_unref(slot);
		free(path);
		break;
	}
	case SYS_IS_DEBUG_MODE: {// system.IsDebugMode(void)
		stack_push(0);
		break;
	}
	case SYS_GET_FUNC_STACK_NAME: { // system.GetFuncStackName(int nIndex)
		stack_push_string(get_func_stack_name(stack_pop().i));
		break;
	}
	case SYS_PEEK: {// system.Peek(void)
		handle_events();
		break;
	}
	case SYS_SLEEP: {// system.Sleep(int nSleep)
		int ms = stack_pop().i;
		vm_sleep(ms);
		break;
	}
	case SYS_RESUME_READ_COMMENT: {// system.ResumeReadComment(string szKeyName, string szFileName, ref array@string aszComment)
		int success;
		int comment = stack_pop().i;
		int filename = stack_pop().i;
		int keyname = stack_pop().i;
		// FIXME: free ref'd array if allocated
		heap_set_page(comment, vm_load_image_comments(heap_get_string(keyname)->text,
							      heap_get_string(filename)->text,
							      &success));
		heap_unref(filename);
		heap_unref(keyname);
		stack_push(success);
		break;
	}
	case SYS_RESUME_WRITE_COMMENT: { // system.ResumeWriteComment(string szKeyName, string szFileName, ref array@string aszComment)
		int comment = stack_pop().i;
		int filename = stack_pop().i;
		int keyname = stack_pop().i;
		stack_push(vm_write_image_comments(heap_get_string(keyname)->text,
						   heap_get_string(filename)->text,
						   heap_get_page(comment)));
		heap_unref(filename);
		heap_unref(keyname);
		break;
	}
	case SYS_GROUP_SAVE: { // system.GroupSave(string szKeyName, string szFileName, string szGroupName, ref int nNumofLoad)
		union vm_value *n = stack_pop_var();
		int groupname = stack_pop().i;
		int filename = stack_pop().i;
		int keyname = stack_pop().i;
		stack_push(save_globals(heap_get_string(keyname)->text,
				      heap_get_string(filename)->text,
				      heap_get_string(groupname)->text,
				      &n->i));
		heap_unref(groupname);
		heap_unref(filename);
		heap_unref(keyname);
		break;
	}
	case SYS_GROUP_LOAD: { // system.GroupLoad(string szKeyName, string szFileName, string szGroupName, ref int nNumofLoad)
		union vm_value *n = stack_pop_var();
		int groupname = stack_pop().i;
		int filename = stack_pop().i;
		int keyname = stack_pop().i;
		stack_push(load_globals(heap_get_string(keyname)->text,
					heap_get_string(filename)->text,
					heap_get_string(groupname)->text,
					&n->i));
		heap_unref(groupname);
		heap_unref(filename);
		heap_unref(keyname);
		break;
	}
	case SYS_DELETE_SAVE_FILE: { // system.DeleteSaveFile(string szFileName)
		int filename = stack_pop().i;
		stack_push(delete_save_file(heap_get_string(filename)->text));
		heap_unref(filename);
		break;
	}
	case SYS_EXIST_FUNC: { // system.ExistFunc(string szFuncName)
		int funcname = stack_pop().i;
		stack_push(ain_get_function(ain, heap_get_string(funcname)->text) > 0);
		heap_unref(funcname);
		break;
	}
	case SYS_COPY_SAVE_FILE: { // system.CopySaveFile(string szDestFileName, string szSourceFileName)
		int src = stack_pop().i;
		int dst = stack_pop().i;
		char *u_src = savedir_path(heap_get_string(src)->text);
		char *u_dst = savedir_path(heap_get_string(dst)->text);
		stack_push(file_copy(u_src, u_dst));
		free(u_src);
		free(u_dst);
		heap_unref(src);
		heap_unref(dst);
		break;
	}
	default:
		// xsystem4-specific system calls (used for hacks)
		switch ((enum vm_extra_syscall)code) {
		case VM_XSYS_KEY_IS_DOWN:
			stack_push(key_is_down(stack_pop().i));
			break;
		default:
			VM_ERROR("Unimplemented syscall: 0x%X", code);
		}
	}
}

uint32_t get_switch_address(int no, int val)
{
	struct ain_switch *s = &ain->switches[no];
	for (int i = 0; i < s->nr_cases; i++) {
		if (s->cases[i].value == val) {
			return s->cases[i].address;
		}
	}
	if (s->default_address > 0)
		return s->default_address;
	else
		return instr_ptr + instruction_width(SWITCH);
}

uint32_t get_strswitch_address(int no, struct string *str)
{
	struct ain_switch *s = &ain->switches[no];
	for (int i = 0; i < s->nr_cases; i++) {
		if (!strcmp(str->text, ain->strings[s->cases[i].value]->text)) {
			return s->cases[i].address;
		}
	}
	if (s->default_address > 0)
		return s->default_address;
	else
		return instr_ptr + instruction_width(STRSWITCH);
}

static void echo_message(int i)
{
	NOTICE("MSG %d: %s", i, display_sjis0(ain->messages[i]->text));
}

static enum opcode execute_instruction(enum opcode opcode)
{
	switch (opcode) {
	//
	// --- Stack Management ---
	//
	case PUSH: {
		stack_push(get_argument(0));
		break;
	}
	case POP: {
		stack_pop();
		break;
	}
	case F_PUSH: {
		stack_push(get_argument_float(0));
		break;
	}
	case REF: {
		// Dereference a reference to a value.
		stack_push(stack_pop_var()->i);
		break;
	}
	case REFREF: {
		// Dereference a reference to a reference.
		union vm_value *ref = stack_pop_var();
		stack_push(ref[0].i);
		stack_push(ref[1].i);
		break;
	}
	case DUP: {
		// A -> AA
		stack_push(stack_peek(0).i);
		break;
	}
	case DUP2: {
		// AB -> ABAB
		int a = stack_peek(1).i;
		int b = stack_peek(0).i;
		stack_push(a);
		stack_push(b);
		break;
	}
	case DUP_X2: {
		// ABC -> CABC
		int a = stack_peek(2).i;
		int b = stack_peek(1).i;
		int c = stack_peek(0).i;
		stack_set(2, c);
		stack_set(1, a);
		stack_set(0, b);
		stack_push(c);
		break;
	}
	case DUP2_X1: {
		// ABC -> BCABC
		int a = stack_peek(2).i;
		int b = stack_peek(1).i;
		int c = stack_peek(0).i;
		stack_set(2, b);
		stack_set(1, c);
		stack_set(0, a);
		stack_push(b);
		stack_push(c);
		break;
	}
	case DUP_U2: {
		// AB -> ABA
		stack_push(stack_peek(1).i);
		break;
	}
	case SWAP: {
		int a = stack_peek(1).i;
		stack_set(1, stack_peek(0));
		stack_set(0, a);
		break;
	}
	//
	// --- Variables ---
	//
	case PUSHGLOBALPAGE: {
		stack_push(0);
		break;
	}
	case PUSHLOCALPAGE: {
		stack_push(local_page_slot());
		break;
	}
	case PUSHSTRUCTPAGE: {
		stack_push(struct_page_slot());
		break;
	}
	case ASSIGN:
	case F_ASSIGN: {
		union vm_value val = stack_pop();
		stack_pop_var()[0] = val;
		stack_push(val);
		break;
	}
	case SH_GLOBALREF: { // VARNO
		stack_push(global_get(get_argument(0)).i);
		break;
	}
	case SH_LOCALREF: { // VARNO
		stack_push(local_get(get_argument(0)).i);
		break;
	}
	case _EOF: // In Ain v0, opcode 0x62 is not EOF but SH_STRUCTREF
		if (ain->version != 0)
			VM_ERROR("Illegal opcode: 0x%04x", opcode);
		// fallthrough
	case SH_STRUCTREF: { // VARNO
		stack_push(member_get(get_argument(0)));
		break;
	}
	case SH_LOCALASSIGN: { // VARNO, VALUE
		local_set(get_argument(0), get_argument(1));
		break;
	}
	case SH_LOCALINC: { // VARNO
		int varno = get_argument(0);
		local_set(varno, local_get(varno).i+1);
		break;
	}
	case SH_LOCALDEC: { // VARNO
		int varno = get_argument(0);
		local_set(varno, local_get(varno).i-1);
		break;
	}
	case SH_LOCALDELETE: {
		int slot = local_get(get_argument(0)).i;
		if (slot != -1) {
			heap_unref(slot);
			local_set(get_argument(0), -1);
		}
		break;
	}
	case SH_LOCALCREATE: { // VARNO, STRUCTNO
		create_struct(get_argument(1), local_ptr(get_argument(0)));
		break;
	}
	case R_ASSIGN: {
		int src_var = stack_pop().i;
		int src_page = stack_pop().i;
		int dst_var = stack_pop().i;
		struct page *dst = heap_get_page(stack_pop().i);
		page_set_var(dst, dst_var, src_page);
		page_set_var(dst, dst_var+1, src_var);
		stack_push(src_page);
		stack_push(src_var);
		break;
	}
	case R_EQUALE: {
		int rhs_var = stack_pop().i;
		int rhs_page = stack_pop().i;
		int lhs_var = stack_pop().i;
		int lhs_page = stack_pop().i;
		stack_push(lhs_page == rhs_page && lhs_var == rhs_var ? 1 : 0);
		break;
	}
	case R_NOTE: {
		int rhs_var = stack_pop().i;
		int rhs_page = stack_pop().i;
		int lhs_var = stack_pop().i;
		int lhs_page = stack_pop().i;
		stack_push(lhs_page == rhs_page && lhs_var == rhs_var ? 0 : 1);
		break;
	}
	case NEW: {
		union vm_value v;
		if (ain->version >= 11) {
			int struct_type = get_argument(0);
			int ctor_func = get_argument(1);
			create_struct(struct_type, &v);
			if (ctor_func > 0 && ain->version >= 14) {
				// v14: constructor args are already on the stack
				int nr_ctor_args = ain->functions[ctor_func].nr_args;
				union vm_value saved[64];
				for (int i = nr_ctor_args - 1; i >= 0; i--) {
					saved[i] = stack_pop();
				}
				stack_push(v);
				for (int i = 0; i < nr_ctor_args; i++) {
					stack_push(saved[i]);
				}
				size_t saved_ip = instr_ptr;
				method_call(ctor_func, VM_RETURN);
				int expected_sp = stack_ptr;
				vm_execute();
				instr_ptr = saved_ip;
				if (stack_ptr != expected_sp) {
					static int ctor_imbalance_warn = 0;
					if (ctor_imbalance_warn++ < 20)
						WARNING("NEW: ctor stack imbalance func#%d(%s): expected sp=%d got sp=%d (delta=%d) ret=%d",
							ctor_func, ain->functions[ctor_func].name,
							expected_sp, stack_ptr, stack_ptr - expected_sp,
							ain->functions[ctor_func].return_type.data);
					stack_ptr = expected_sp;
				}
				stack_push(v);
				break;
			}
			} else {
			create_struct(stack_pop().i, &v);
		}
		stack_push(v);
		break;
	}
	case DELETE: {
		int slot = stack_pop().i;
		if (slot != -1)
			heap_unref(slot);
		break;
	}
	case SP_INC: {
		heap_ref(stack_pop().i);
		break;
	}
	case OBJSWAP: {
		if (ain->version < 11)
			stack_pop(); // pre-v11: type on stack
		// v11+: type in bytecode arg (get_argument(0)), ignored
		union vm_value *b = stack_pop_var();
		union vm_value *a = stack_pop_var();
		union vm_value tmp = *a;
		*a = *b;
		*b = tmp;
		break;
	}
	//
	// --- Control Flow ---
	//
	case CALLFUNC: {
		function_call(get_argument(0), instr_ptr + instruction_width(CALLFUNC));
		break;
	}
	case CALLFUNC2: {
		stack_pop(); // function-type index (only needed for compilation)
		function_call(stack_pop().i, instr_ptr + instruction_width(CALLFUNC2));
		break;
	}
	case CALLMETHOD: {
		if (ain->version >= 14) {
			// ain v14: bytecode arg = nargs (not function number)
			// Stack: [struct_page, funcno, arg0, ..., argN-1]
			int nargs = get_argument(0);
			// Save args, extract funcno from beneath them, restore args
			union vm_value small_args[64];
			union vm_value *saved_args = (nargs <= 64) ? small_args : malloc(nargs * sizeof(union vm_value));
			for (int i = 0; i < nargs; i++) {
				saved_args[i] = stack_pop();
			}
			int funcno = stack_pop().i;
			for (int i = nargs - 1; i >= 0; i--) {
				stack_push(saved_args[i]);
			}
			if (saved_args != small_args) free(saved_args);
			// Stack is now: [struct_page, arg0, ..., argN-1]
			if (funcno == 0) {
				// func#0 is "NULL" sentinel — unimplemented virtual method
				for (int i = 0; i < nargs; i++) stack_pop();
				stack_pop(); // pop struct_page
				stack_push(-1); // push -1 sentinel (NOT 0, which is the global page slot)
				instr_ptr += instruction_width(CALLMETHOD);
			} else if (funcno >= 0 && funcno < ain->nr_functions) {
				int fn_nargs = ain->functions[funcno].nr_args;
				if (fn_nargs != nargs) {
					// v14 overload resolution: vtable stores a base funcno,
					// VM resolves correct overload by matching argument count.
					// Overloaded functions are typically consecutive in the AIN.
					const char *name = ain->functions[funcno].name;
					int resolved = -1;
					for (int f = funcno - 50; f <= funcno + 50 && f < ain->nr_functions; f++) {
						if (f >= 0 && ain->functions[f].nr_args == nargs
						    && !strcmp(ain->functions[f].name, name)) {
							resolved = f;
							break;
						}
					}
					if (resolved >= 0) {
						method_call(resolved, instr_ptr + instruction_width(CALLMETHOD));
					} else {
						static int cm_mismatch_warn = 0;
						if (cm_mismatch_warn++ < 20) {
							WARNING("CALLMETHOD: nargs mismatch! bytecode=%d func#%d(%s).nr_args=%d sp=%d ip=0x%lX — no overload found",
								nargs, funcno, ain->functions[funcno].name, fn_nargs, stack_ptr,
								(unsigned long)instr_ptr);
						}
						for (int i = 0; i < nargs; i++) stack_pop();
						stack_pop(); // pop struct_page
						if (ain->functions[funcno].return_type.data != AIN_VOID) {
							stack_push(-1);
							if (ain->version >= 14) {
								int _rt = ain->functions[funcno].return_type.data;
								if (_rt == AIN_OPTION) {
									stack_push(0);
								} else if (_rt == AIN_WRAP) {
									struct ain_type *ws = ain->functions[funcno].return_type.array_type;
									bool prim = ws && (ws->data == AIN_INT || ws->data == AIN_BOOL ||
										ws->data == AIN_FLOAT || ws->data == AIN_LONG_INT ||
										ws->data == AIN_ENUM || ws->data == AIN_ENUM2 ||
				ws->data == AIN_IFACE || ws->data == AIN_IFACE_WRAP);
									if (prim) stack_push(0);
								}
							}
						}
						instr_ptr += instruction_width(CALLMETHOD);
					}
				} else {
					method_call(funcno, instr_ptr + instruction_width(CALLMETHOD));
				}
			} else {
				static int cm_invalid_warn = 0;
				if (cm_invalid_warn++ < 20) {
					WARNING("CALLMETHOD: invalid function number %d (nargs=%d sp=%d ip=0x%lX)", funcno, nargs, stack_ptr, (unsigned long)instr_ptr);
				}
				// Pop remaining items
				for (int i = 0; i < nargs; i++) stack_pop();
				stack_pop(); // pop struct_page
				// Always push sentinel — we can't tell void vs non-void from funcno=-1
				// The return value may be consumed by subsequent code (X_ASSIGN, DELETE, etc.)
				stack_push(-1);
				instr_ptr += instruction_width(CALLMETHOD);
			}
		} else {
			// Pre-v14: bytecode arg = function number
			method_call(get_argument(0), instr_ptr + instruction_width(CALLMETHOD));
		}
		break;
	}
	case CALLHLL: {
		int _hll_lib = get_argument(0);
		int _hll_fun = get_argument(1);
		int _sp_before = stack_ptr;
		hll_call(_hll_lib, _hll_fun);
		int _sp_after = stack_ptr;
		// HLL call trace — log ALL calls, skip RandF noise
		{
			static int hll_trace = 0;
			static int hll_total = 0;
			hll_total++;
			const char *_fn = ain->libraries[_hll_lib].functions[_hll_fun].name;
			bool _skip = (!strcmp(_fn, "RandF") || !strcmp(_fn, "Numof") ||
				!strcmp(_fn, "GetPart") || !strcmp(_fn, "Length") ||
				!strcmp(_fn, "GetFontWidth") || !strcmp(_fn, "Ceil"));
			if (!_skip && hll_trace++ < 500) {
				int retval = (_sp_after > _sp_before) ? stack[stack_ptr - 1].i : -9999;
				WARNING("HLL TRACE [%d/%d]: %s.%s (lib=%d func=%d) sp=%d→%d ret=%d ip=0x%lX caller=func#%d",
					hll_trace, hll_total,
					ain->libraries[_hll_lib].name, _fn,
					_hll_lib, _hll_fun, _sp_before, _sp_after, retval,
					(unsigned long)instr_ptr,
					call_stack[call_stack_ptr - 1].fno);
			}
		}
		// Calculate expected stack change: -(nr_args) + (has_retval)
		{
			struct ain_hll_function *_f = &ain->libraries[_hll_lib].functions[_hll_fun];
			int _expected_pop = 0;
			for (int _p = 0; _p < _f->nr_arguments; _p++) {
				switch (_f->arguments[_p].type.data) {
				case AIN_REF_INT: case AIN_REF_BOOL: case AIN_REF_FLOAT:
				case AIN_REF_FUNC_TYPE: case AIN_REF_HLL_PARAM:
					_expected_pop += 2; break;
				case AIN_HLL_FUNC:
					_expected_pop += 2; break;
				case AIN_WRAP: {
					// v14: WRAP<primitive> args = 2-slot ref, WRAP<struct> = 1-slot
					struct ain_type *_ws = _f->arguments[_p].type.array_type;
					bool _prim = _ws && (_ws->data == AIN_INT || _ws->data == AIN_BOOL ||
						_ws->data == AIN_FLOAT || _ws->data == AIN_LONG_INT ||
						_ws->data == AIN_ENUM || _ws->data == AIN_ENUM2 ||
						_ws->data == AIN_IFACE || _ws->data == AIN_IFACE_WRAP);
					_expected_pop += (ain->version >= 14 && _prim) ? 2 : 1;
					break;
				}
				case AIN_OPTION:
					_expected_pop += (ain->version >= 14) ? 2 : 1;
					break;
				default:
					_expected_pop += 1; break;
				}
			}
			int _has_ret;
			if (_f->return_type.data == AIN_VOID) {
				_has_ret = 0;
			} else if (ain->version >= 14 && _f->return_type.data == AIN_OPTION) {
				_has_ret = 2;
			} else if (ain->version >= 14 && _f->return_type.data == AIN_WRAP) {
				struct ain_type *_rws = _f->return_type.array_type;
				bool _rprim = _rws && (_rws->data == AIN_INT || _rws->data == AIN_BOOL ||
					_rws->data == AIN_FLOAT || _rws->data == AIN_LONG_INT ||
					_rws->data == AIN_ENUM || _rws->data == AIN_ENUM2 ||
					_rws->data == AIN_IFACE || _rws->data == AIN_IFACE_WRAP);
				_has_ret = _rprim ? 2 : 1;
			} else {
				_has_ret = 1;
			}
			int _expected_delta = -_expected_pop + _has_ret;
			int _actual_delta = _sp_after - _sp_before;
			if (_actual_delta != _expected_delta) {
				static int hll_delta_warn = 0;
				if (hll_delta_warn++ < 30)
					WARNING("CALLHLL stack mismatch: %s.%s expected_delta=%d actual_delta=%d (popped=%d ret=%d) sp=%d→%d ip=0x%lX nr_args=%d",
						ain->libraries[_hll_lib].name, _f->name,
						_expected_delta, _actual_delta,
						_expected_pop, _has_ret,
						_sp_before, _sp_after, (unsigned long)instr_ptr, _f->nr_arguments);
				// Auto-fix: push/pop to match expected delta
				int deficit = _expected_delta - _actual_delta;
				if (deficit > 0) {
					for (int _i = 0; _i < deficit; _i++)
						stack_push(0);
				} else if (deficit < 0) {
					stack_ptr += deficit; // pop excess
				}
			}
			// Dump parameter types for TextSurfaceManager.GetFontWidth (lib34/func0)
			if (_hll_lib == 34 && _hll_fun == 0) {
				static int tsm_trace = 0;
				if (tsm_trace++ < 2) {
					char buf[512];
					int pos = 0;
					for (int _p = 0; _p < _f->nr_arguments && pos < 500; _p++) {
						pos += snprintf(buf + pos, sizeof(buf) - pos, "%d ", _f->arguments[_p].type.data);
					}
					WARNING("TextSurfaceManager.GetFontWidth: %d args types=[%s] ret=%d sp_before=%d sp_after=%d",
						_f->nr_arguments, buf, _f->return_type.data, _sp_before, _sp_after);
				}
			}
		}
		// Check for global page corruption after HLL calls
		if (heap_size > 0 && heap[0].page && heap[0].page->type != GLOBAL_PAGE) {
			static int hll_corrupt = 0;
			if (hll_corrupt++ < 3)
				WARNING("CALLHLL corrupted global page! %s.%s (lib=%d func=%d) ip=0x%lX",
					ain->libraries[_hll_lib].name,
					ain->libraries[_hll_lib].functions[_hll_fun].name,
					_hll_lib, _hll_fun, (unsigned long)instr_ptr);
		}
		break;
	}
	case RETURN: {
		function_return();
		break;
	}
	case CALLSYS: {
		system_call(get_argument(0));
		break;
	}
	case CALLONJUMP: {
		int str = stack_pop().i;
		if (ain->scenario_labels) {
			stack_push(scenario_label_addr(heap_get_string(str)->text));
		} else {
			// XXX: I am GUESSING that the VM pre-allocates the scenario function's
			//      local page here. It certainly pushes what appears to be a page
			//      index to the stack.
			stack_push(alloc_scenario_page(heap_get_string(str)->text));
		}
		heap_unref(str);
		break;
	}
	case SJUMP: {
		if (ain->scenario_labels) {
			scenario_jump(stack_pop().i);
		} else {
			scenario_call(stack_pop().i);
		}
		break;
	}
	case _MSG: {
		if (config.echo)
			echo_message(get_argument(0));
		if (ain->msgf < 0)
			break;
		stack_push(get_argument(0));
		stack_push(ain->nr_messages);
		stack_push_string(string_ref(ain->messages[get_argument(0)]));
		function_call(ain->msgf, instr_ptr + instruction_width(_MSG));
		break;
	}
	case JUMP: { // ADDR
		instr_ptr = get_argument(0);
		break;
	}
	case IFZ: { // ADDR
		if (!stack_pop().i)
			instr_ptr = get_argument(0);
		else
			instr_ptr += instruction_width(IFZ);
		break;
	}
	case IFNZ: { // ADDR
		if (stack_pop().i)
			instr_ptr = get_argument(0);
		else
			instr_ptr += instruction_width(IFNZ);
		break;
	}
	case SWITCH: {
		instr_ptr = get_switch_address(get_argument(0), stack_pop().i);
		break;
	}
	case STRSWITCH: {
		int str = stack_pop().i;
		instr_ptr = get_strswitch_address(get_argument(0), heap_get_string(str));
		heap_unref(str);
		break;
	}
	case ASSERT: {
		int line = stack_pop().i; // line number
		int file = stack_pop().i; // filename
		int expr = stack_pop().i; // expression
		if (!stack_pop().i) {
			sys_message("Assertion failed at %s:%d: %s\n",
					display_sjis0(heap_get_string(file)->text),
					line,
					display_sjis1(heap_get_string(expr)->text));
			vm_exit(1);
		}
		heap_unref(file);
		heap_unref(expr);
		break;
	}
	//
	// --- Arithmetic ---
	//
	case INV: {
		stack[stack_ptr-1].i = -stack[stack_ptr-1].i;
		break;
	}
	case NOT: {
		stack[stack_ptr-1].i = !stack[stack_ptr-1].i;
		break;
	}
	case COMPL: {
		stack[stack_ptr-1].i = ~stack[stack_ptr-1].i;
		break;
	}
	case ADD: {
		stack[stack_ptr-2].i += stack[stack_ptr-1].i;
		stack_ptr--;
		break;
	}
	case SUB: {
		stack[stack_ptr-2].i -= stack[stack_ptr-1].i;
		stack_ptr--;
		break;
	}
	case MUL: {
		stack[stack_ptr-2].i *= stack[stack_ptr-1].i;
		stack_ptr--;
		break;
	}
	case DIV: {
		if (!stack[stack_ptr-1].i) {
			stack[stack_ptr-2].i = 0;
		} else {
			stack[stack_ptr-2].i /= stack[stack_ptr-1].i;
		}
		stack_ptr--;
		break;
	}
	case MOD: {
		if (!stack[stack_ptr-1].i) {
			stack[stack_ptr-2].i = 0;
		} else {
			stack[stack_ptr-2].i %= stack[stack_ptr-1].i;
		}
		stack_ptr--;
		break;
	}
	case AND: {
		stack[stack_ptr-2].i &= stack[stack_ptr-1].i;
		stack_ptr--;
		break;
	}
	case OR: {
		stack[stack_ptr-2].i |= stack[stack_ptr-1].i;
		stack_ptr--;
		break;
	}
	case XOR: {
		stack[stack_ptr-2].i ^= stack[stack_ptr-1].i;
		stack_ptr--;
		break;
	}
	case LSHIFT: {
		stack[stack_ptr-2].i <<= stack[stack_ptr-1].i;
		stack_ptr--;
		break;
	}
	case RSHIFT: {
		stack[stack_ptr-2].i >>= stack[stack_ptr-1].i;
		stack_ptr--;
		break;
	}
	// Numeric Comparisons
	case LT: {
		int32_t b = stack_pop().i;
		int32_t a = stack_pop().i;
		stack_push(a < b ? 1 : 0);
		break;
	}
	case GT: {
		int32_t b = stack_pop().i;
		int32_t a = stack_pop().i;
		stack_push(a > b ? 1 : 0);
		break;
	}
	case LTE: {
		int32_t b = stack_pop().i;
		int32_t a = stack_pop().i;
		stack_push(a <= b ? 1 : 0);
		break;
	}
	case GTE: {
		int32_t b = stack_pop().i;
		int32_t a = stack_pop().i;
		stack_push(a >= b ? 1 : 0);
		break;
	}
	case NOTE: {
		int32_t b = stack_pop().i;
		int32_t a = stack_pop().i;
		static int note_log = 0;
		if (instr_ptr >= 0x49A000 && instr_ptr <= 0x49A500 && note_log++ < 10) {
			WARNING("NOTE: %d != %d => %d (ip=0x%lX fno=%d)",
				a, b, (a != b ? 1 : 0),
				(unsigned long)instr_ptr,
				call_stack_ptr > 0 ? call_stack[call_stack_ptr-1].fno : -1);
		}
		stack_push(a != b ? 1 : 0);
		break;
	}
	case EQUALE: {
		int32_t b = stack_pop().i;
		int32_t a = stack_pop().i;
		stack_push(a == b ? 1 : 0);
		break;
	}
	// +=, -=, etc.
	case PLUSA: {
		int32_t n = stack_pop().i;
		stack_push(stack_pop_var()->i += n);
		break;
	}
	case MINUSA: {
		int32_t n = stack_pop().i;
		stack_push(stack_pop_var()->i -= n);
		break;
	}
	case MULA: {
		int32_t n = stack_pop().i;
		stack_push(stack_pop_var()->i *= n);
		break;
	}
	case DIVA: {
		int32_t n = stack_pop().i;
		stack_push(n ? stack_pop_var()->i /= n : 0);
		break;
	}
	case MODA: {
		int32_t n = stack_pop().i;
		stack_push(stack_pop_var()->i %= n);
		break;
	}
	case ANDA: {
		int32_t n = stack_pop().i;
		stack_push(stack_pop_var()->i &= n);
		break;
	}
	case ORA: {
		int32_t n = stack_pop().i;
		stack_push(stack_pop_var()->i |= n);
		break;
	}
	case XORA: {
		int32_t n = stack_pop().i;
		stack_push(stack_pop_var()->i ^= n);
		break;
	}
	case LSHIFTA: {
		int32_t n = stack_pop().i;
		stack_push(stack_pop_var()->i <<= n);
		break;
	}
	case RSHIFTA: {
		int32_t n = stack_pop().i;
		stack_push(stack_pop_var()->i >>= n);
		break;
	}
	case INC: {
		stack_pop_var()[0].i++;
		break;
	}
	case DEC: {
		stack_pop_var()[0].i--;
		break;
	}
	case ITOB: {
		stack_set(0, !!stack_peek(0).i);
		break;
	}
	//
	// --- 64-bit integers ---
	//
	case ITOLI: {
		stack_set(0, lint_clamp(stack_peek(0).i));
		break;
	}
	case LI_ADD: {
		int64_t a = stack[stack_ptr-2].i;
		int64_t b = stack[stack_ptr-1].i;
		stack[stack_ptr-2].i = lint_clamp(a + b);
		stack_ptr--;
		break;
	}
	case LI_SUB: {
		int64_t a = stack[stack_ptr-2].i;
		int64_t b = stack[stack_ptr-1].i;
		stack[stack_ptr-2].i = lint_clamp(a - b);
		stack_ptr--;
		break;
	}
	case LI_MUL: {
		int64_t a = stack[stack_ptr-2].i;
		int64_t b = stack[stack_ptr-1].i;
		stack[stack_ptr-2].i = lint_clamp(a * b);
		stack_ptr--;
		break;
	}
	case LI_DIV: {
		int64_t a = stack[stack_ptr-2].i;
		int64_t b = stack[stack_ptr-1].i;
		stack[stack_ptr-2].i = b ? lint_clamp(a / b) : 0;
		stack_ptr--;
		break;
	}
	case LI_MOD: {
		int64_t a = stack[stack_ptr-2].i;
		int64_t b = stack[stack_ptr-1].i;
		stack[stack_ptr-2].i = lint_clamp(a % b);
		stack_ptr--;
		break;
	}
	case LI_ASSIGN: {
		int64_t v = stack_pop().i;
		stack_push(stack_pop_var()->i = lint_clamp(v));
		break;
	}
	case LI_PLUSA: {
		int64_t n = stack_pop().i;
		union vm_value *v = stack_pop_var();
		stack_push(v->i = lint_clamp((int64_t)v->i + n));
		break;
	}
	case LI_MINUSA: {
		int64_t n = stack_pop().i;
		union vm_value *v = stack_pop_var();
		stack_push(v->i = lint_clamp((int64_t)v->i - n));
		break;
	}
	case LI_MULA: {
		int64_t n = stack_pop().i;
		union vm_value *v = stack_pop_var();
		stack_push(v->i = lint_clamp((int64_t)v->i * n));
		break;
	}
	case LI_DIVA: {
		int64_t n = stack_pop().i;
		union vm_value *v = stack_pop_var();
		stack_push(v->i = (n ? lint_clamp((int64_t)v->i / n) : 0));
		break;
	}
	case LI_MODA: {
		int64_t n = stack_pop().i;
		union vm_value *v = stack_pop_var();
		stack_push(v->i = lint_clamp((int64_t)v->i % n));
		break;
	}
	case LI_ANDA: {
		int64_t n = stack_pop().i;
		union vm_value *v = stack_pop_var();
		stack_push(v->i = lint_clamp((int64_t)v->i & n));
		break;
	}
	case LI_ORA: {
		int64_t n = stack_pop().i;
		union vm_value *v = stack_pop_var();
		stack_push(v->i = lint_clamp((int64_t)v->i | n));
		break;
	}
	case LI_XORA: {
		int64_t n = stack_pop().i;
		union vm_value *v = stack_pop_var();
		stack_push(v->i = lint_clamp((int64_t)v->i ^ n));
		break;
	}
	case LI_LSHIFTA: {
		int64_t n = stack_pop().i;
		union vm_value *v = stack_pop_var();
		stack_push(v->i = lint_clamp((int64_t)v->i << n));
		break;
	}
	case LI_RSHIFTA: {
		int64_t n = stack_pop().i;
		union vm_value *v = stack_pop_var();
		stack_push(v->i = lint_clamp((int64_t)v->i >> n));
		break;
	}
	case LI_INC: {
		union vm_value *v = stack_pop_var();
		v->i = lint_clamp((int64_t)v->i + (int64_t)1);
		break;
	}
	case LI_DEC: {
		union vm_value *v = stack_pop_var();
		v->i = lint_clamp((int64_t)v->i - (int64_t)1);
		break;
	}
	//
	// --- Floating Point Arithmetic ---
	//
	case FTOI: {
		stack_set(0, (int32_t)stack_peek(0).f);
		break;
	}
	case ITOF: {
		stack_set(0, (float)stack_peek(0).i);
		break;
	}
	case F_INV: {
		stack_set(0, -stack_peek(0).f);
		break;
	}
	case F_ADD: {
		float f = stack_pop().f;
		stack_set(0, stack_peek(0).f + f);
		break;
	}
	case F_SUB: {
		float f = stack_pop().f;
		stack_set(0, stack_peek(0).f - f);
		break;
	}
	case F_MUL: {
		float f = stack_pop().f;
		stack_set(0, stack_peek(0).f * f);
		break;
	}
	case F_DIV: {
		float f = stack_pop().f;
		stack_set(0, stack_peek(0).f / f);
		break;
	}
	// floating point comparison
	case F_LT: {
		float f = stack_pop().f;
		stack_set(0, stack_peek(0).f < f ? 1 : 0);
		break;
	}
	case F_GT: {
		float f = stack_pop().f;
		stack_set(0, stack_peek(0).f > f ? 1 : 0);
		break;
	}
	case F_LTE: {
		float f = stack_pop().f;
		stack_set(0, stack_peek(0).f <= f ? 1 : 0);
		break;
	}
	case F_GTE: {
		float f = stack_pop().f;
		stack_set(0, stack_peek(0).f >= f ? 1 : 0);
		break;
	}
	case F_NOTE: {
		float f = stack_pop().f;
		stack_set(0, stack_peek(0).f != f ? 1 : 0);
		break;
	}
	case F_EQUALE: {
		float f = stack_pop().f;
		stack_set(0, stack_peek(0).f == f ? 1 : 0);
		break;
	}
	case F_PLUSA: {
		float n = stack_pop().f;
		stack_push(stack_pop_var()->f += n);
		break;
	}
	case F_MINUSA: {
		float n = stack_pop().f;
		stack_push(stack_pop_var()->f -= n);
		break;
	}
	case F_MULA: {
		float n = stack_pop().f;
		stack_push(stack_pop_var()->f *= n);
		break;
	}
	case F_DIVA: {
		float n = stack_pop().f;
		stack_push(stack_pop_var()->f /= n);
		break;
	}
	//
	// --- Strings ---
	//
	case S_PUSH: {
		if (ain->version == 0)
			stack_push_string(string_ref(ain->messages[get_argument(0)]));
		else
			stack_push_string(string_ref(ain->strings[get_argument(0)]));
		break;
	}
	case S_POP: {
		heap_unref(stack_pop().i);
		break;
	}
	case S_REF: {
		// Dereference a reference to a string
		int str = stack_pop_var()->i;
		stack_push_string(string_ref(heap_get_string(str)));
		break;
	}
	//case S_REFREF: // ???: why/how is this different from regular REFREF?
	case S_ASSIGN: { // A = B
		if (ain->version >= 14) {
			// v14: pop string rval, then 2-slot variable reference for destination
			int rval = stack_pop().i;
			union vm_value *dest = stack_pop_var();
			int lval = dest->i;
			if (lval > 0 && string_index_valid(lval)) {
				heap_string_assign(lval, heap_get_string(rval));
			} else {
				// Destination not a valid string slot — assign directly
				heap_ref(rval);
				if (lval > 0) heap_unref(lval);
				dest->i = rval;
			}
			stack_push(rval);
		} else {
			int rval = stack_peek(0).i;
			int lval = stack_peek(1).i;
			heap_string_assign(lval, heap_get_string(rval));
			// remove A from the stack, but leave B
			stack_set(1, rval);
			stack_pop();
		}
		break;
	}
	case S_PLUSA:
	case S_PLUSA2: {
		int a = stack_peek(1).i;
		int b = stack_peek(0).i;
		string_append(&heap[a].s, heap[b].s);
		heap_unref(b);
		stack_pop();
		stack_pop();
		stack_push_string(string_ref(heap[a].s));
		break;
	}
	case S_ADD: {
		int b = stack_pop().i;
		int a = stack_pop().i;
		struct string *sa = heap_get_string(a);
		struct string *sb = heap_get_string(b);
		if (!sa) sa = &EMPTY_STRING;
		if (!sb) sb = &EMPTY_STRING;
		stack_push_string(string_concatenate(sa, sb));
		heap_unref(a);
		heap_unref(b);
		break;
	}
	case S_LT: {
		bool lt = strcmp(stack_peek_string(1)->text, stack_peek_string(0)->text) < 0;
		heap_unref(stack_pop().i);
		heap_unref(stack_pop().i);
		stack_push(lt);
		break;
	}
	case S_GT: {
		bool gt = strcmp(stack_peek_string(1)->text, stack_peek_string(0)->text) > 0;
		heap_unref(stack_pop().i);
		heap_unref(stack_pop().i);
		stack_push(gt);
		break;
	}
	case S_LTE: {
		bool lte = strcmp(stack_peek_string(1)->text, stack_peek_string(0)->text) <= 0;
		heap_unref(stack_pop().i);
		heap_unref(stack_pop().i);
		stack_push(lte);
		break;
	}
	case S_GTE: {
		bool gte = strcmp(stack_peek_string(1)->text, stack_peek_string(0)->text) >= 0;
		heap_unref(stack_pop().i);
		heap_unref(stack_pop().i);
		stack_push(gte);
		break;
	}
	case S_NOTE: {
		struct string *s1 = stack_peek_string(1);
		struct string *s0 = stack_peek_string(0);
		bool noteq = !!strcmp(s1->text, s0->text);
		static int snote_log = 0;
		if (snote_log++ < 10) {
			WARNING("S_NOTE: '%s' != '%s' => %d (ip=0x%lX fno=%d)",
				s1->text, s0->text, noteq,
				(unsigned long)instr_ptr,
				call_stack_ptr > 0 ? call_stack[call_stack_ptr-1].fno : -1);
		}
		heap_unref(stack_pop().i);
		heap_unref(stack_pop().i);
		stack_push(noteq);
		break;
	}
	case S_EQUALE: {
		bool eq = !strcmp(stack_peek_string(1)->text, stack_peek_string(0)->text);
		heap_unref(stack_pop().i);
		heap_unref(stack_pop().i);
		stack_push(eq);
		break;
	}
	case S_LENGTH: {
		int str = stack_pop_var()->i;
		stack_push(sjis_count_char(heap_get_string(str)->text));
		break;
	}
	case S_LENGTH2: {
		int str = stack_pop().i;
		stack_push(sjis_count_char(heap_get_string(str)->text));
		heap_unref(str);
		break;
	}
	case S_LENGTHBYTE: {
		int str = stack_pop_var()->i;
		stack_push(heap_get_string(str)->size);
		break;
	}
	case S_EMPTY: {
		bool empty = !stack_peek_string(0)->size;
		heap_unref(stack_pop().i);
		stack_push(empty);
		break;
	}
	case S_FIND: {
		int i = string_find(stack_peek_string(1), stack_peek_string(0));
		heap_unref(stack_pop().i);
		heap_unref(stack_pop().i);
		stack_push(i);
		break;
	}
	case S_GETPART: {
		int len = stack_pop().i; // length
		int i = stack_pop().i; // index
		struct string *s = string_copy(stack_peek_string(0), i, len);
		heap_unref(stack_pop().i);
		stack_push_string(s);
		break;
	}
	//case S_PUSHBACK: // ???
	case S_PUSHBACK2: {
		int c = stack_pop().i;
		int str = stack_pop().i;
		string_push_back(&heap[str].s, c);
		break;
	}
	//case S_POPBACK: // ???
	case S_POPBACK2: {
		int str = stack_pop().i;
		string_pop_back(&heap[str].s);
		break;
	}
	//case S_ERASE: // ???
	case S_ERASE2: {
		stack_pop(); // ???
		int i = stack_pop().i; // index
		int str = stack_pop().i;
		string_erase(&heap[str].s, i);
		break;
	}
	case S_MOD: {
		int type;
		if (ain->version >= 11)
			type = get_argument(0); // v14: type in bytecode arg
		else
			type = stack_pop().i;   // pre-v11: type on stack
		union vm_value val = stack_pop();
		int fmt = stack_pop().i;
		int dst = heap_alloc_slot(VM_STRING);
		heap[dst].s = string_format(heap[fmt].s, val, type);
		heap_unref(fmt);
		stack_push(dst);
		break;
	}
	case I_STRING: {
		stack_push_string(integer_to_string(stack_pop().i));
		break;
	}
	case FTOS: {
		int precision = stack_pop().i;
		stack_push_string(float_to_string(stack_pop().f, precision));
		break;
	}
	case STOI: {
		int str = stack_pop().i;
		stack_push(string_to_integer(heap[str].s));
		heap_unref(str);
		break;
	}
	case FT_ASSIGNS: {
		//int functype = stack_pop().i;
		stack_pop();
		int str = stack_pop().i;
		int fno = get_function_by_name(heap_get_string(str)->text);
		stack_pop_var()->i = fno > 0 ? fno : 0;
		stack_push(str);
		break;
	}
	// --- Characters ---
	case C_REF: {
		int i = stack_pop().i;
		int str = stack_pop().i;
		int32_t ch = string_get_char(heap_get_string(str), i);
		// 'é' for EN Rance Quest
		if (game_rance8_mg && ch == -91)
			stack_push(165);
		else
			stack_push(ch);
		break;
	}
	case C_ASSIGN: {
		int c = stack_pop().i;
		int i = stack_pop().i;
		int str = stack_pop().i;
		string_set_char(&heap[str].s, i, c);
		stack_push(c);
		break;
	}
	//
	// --- Structs/Classes ---
	//
	case SR_REF: {
		stack_push(vm_copy_page(heap[stack_pop_var()->i].page));
		break;
	}
	case SR_REF2: {
		stack_push(vm_copy_page(heap[stack_pop().i].page));
		break;
	}
	case SR_POP: {
		heap_unref(stack_pop().i);
		break;
	}
	case SR_ASSIGN: {
		if (ain->version >= 14) {
			// v14: pop source, then 2-slot variable reference for destination
			int rval = stack_pop().i;
			union vm_value *dest = stack_pop_var();
			int lval = dest->i;
			if (lval <= 0 || !heap_index_valid(lval) ||
			    (heap[lval].type == VM_PAGE && heap[lval].page &&
			     heap[lval].page->type != STRUCT_PAGE)) {
				// Destination uninitialized (0/-1) or not a struct page
				// Allocate new slot and copy source into it
				int new_slot = heap_alloc_slot(VM_PAGE);
				if (heap_index_valid(rval) && heap[rval].page)
					heap_set_page(new_slot, copy_page(heap[rval].page));
				dest->i = new_slot;
			} else {
				heap_struct_assign(lval, rval);
			}
			stack_push(rval);
		} else {
			if (ain->version > 1)
				stack_pop(); // struct type
			int rval = stack_pop().i;
			int lval = stack_pop().i;
			heap_struct_assign(lval, rval);
			stack_push(rval);
		}
		break;
	}
	//
	// -- Arrays --
	//
	case A_ALLOC: {
		int struct_type;
		int rank = stack_pop().i;
		int varno = stack_peek(rank).i;
		int pageno = stack_peek(rank+1).i;
		int array = heap[pageno].page->values[varno].i;
		enum ain_data_type data_type = variable_type(heap[pageno].page, varno, &struct_type, NULL);
		if (heap[array].page) {
			delete_page_vars(heap[array].page);
			free_page(heap[array].page);
		}
		heap_set_page(array, alloc_array(rank, stack_peek_ptr(rank-1), data_type, struct_type, true));
		stack_ptr -= rank + 2;
		break;
	}
	case A_REALLOC: {
		int struct_type;
		int rank = stack_pop().i; // rank
		int varno = stack_peek(rank).i;
		int pageno = stack_peek(rank+1).i;
		int array = heap[pageno].page->values[varno].i;
		enum ain_data_type data_type = variable_type(heap[pageno].page, varno, &struct_type, NULL);
		heap_set_page(array, realloc_array(heap[array].page, rank, stack_peek_ptr(rank-1), data_type, struct_type, true));
		stack_ptr -= rank + 2;
		break;
	}
	case A_FREE: {
		int array = stack_pop_var()->i;
		if (heap[array].page) {
			delete_page_vars(heap[array].page);
			free_page(heap[array].page);
			heap_set_page(array, NULL);
		}
		break;
	}
	case A_REF: {
		int array = stack_pop().i;
		if (array < 0 || (size_t)array >= heap_size || heap[array].ref <= 0) {
			stack_push(-1);
		} else if (heap[array].type == VM_STRING) {
			// v14: A_REF on string — create a string reference copy
			stack_push_string(string_ref(heap[array].s ? heap[array].s : &EMPTY_STRING));
		} else if (heap[array].type == VM_PAGE) {
			struct page *p = heap[array].page;
			if (p && (p->type >= NR_PAGE_TYPES || p->nr_vars < 0 || p->nr_vars > 1000000)) {
				static int aref_corrupt_warn = 0;
				if (aref_corrupt_warn++ < 5)
					WARNING("A_REF: corrupted page at slot %d (ptype=%d nr_vars=%d)", array, p->type, p->nr_vars);
				stack_push(-1);
			} else {
				int slot = heap_alloc_slot(VM_PAGE);
				heap_set_page(slot, copy_page(p));
				stack_push(slot);
			}
		} else {
			WARNING("A_REF: unknown type %d at slot %d", heap[array].type, array);
			stack_push(-1);
		}
		break;
	}
	case A_NUMOF: {
		int rank = stack_pop().i; // rank
		int array = stack_pop_var()->i;
		stack_push(array_numof(heap[array].page, rank));
		break;
	}
	case A_COPY: {
		int n = stack_pop().i;
		int src_i = stack_pop().i;
		int src = stack_pop().i;
		int dst_i = stack_pop().i;
		int dst = stack_pop_var()->i;
		array_copy(heap[dst].page, dst_i, heap[src].page, src_i, n);
		stack_push(n);
		break;
	}
	case A_FILL: {
		union vm_value val = stack_pop();
		int n = stack_pop().i;
		int i = stack_pop().i;
		int array = stack_pop_var()->i;
		stack_push(array_fill(heap[array].page, i, n, val));
		break;
	}
	case A_PUSHBACK: {
		int struct_type;
		union vm_value val = stack_pop();
		int varno = stack_pop().i;
		int pageno = stack_pop().i;
		int array = heap[pageno].page->values[varno].i;
		enum ain_data_type data_type = variable_type(heap[pageno].page, varno, &struct_type, NULL);
		heap_set_page(array, array_pushback(heap[array].page, val, data_type, struct_type));
		break;
	}
	case A_POPBACK: {
		int array = stack_pop_var()->i;
		heap_set_page(array, array_popback(heap[array].page));
		break;
	}
	case A_EMPTY: {
		struct page *array = heap_get_page(stack_pop_var()->i);
		stack_push(!array || !array->nr_vars);
		break;
	}
	case A_ERASE: {
		int i = stack_pop().i;
		int array = stack_pop_var()->i;
		bool success = false;
		heap_set_page(array, array_erase(heap[array].page, i, &success));
		stack_push(success);
		break;
	}
	case A_INSERT: {
		int struct_type;
		union vm_value val = stack_pop();
		int i = stack_pop().i;
		int varno = stack_pop().i;
		int pageno = stack_pop().i;
		int array = heap[pageno].page->values[varno].i;
		enum ain_data_type data_type = variable_type(heap[pageno].page, varno, &struct_type, NULL);
		heap_set_page(array, array_insert(heap[array].page, i, val, data_type, struct_type));
		break;
	}
	case A_SORT: {
		int fno = stack_pop().i;
		int array = stack_pop_var()->i;
		array_sort(heap[array].page, fno);
		break;
	}
	case A_FIND: {
		int fno = stack_pop().i;
		union vm_value v = stack_pop();
		int end = stack_pop().i;
		int start = stack_pop().i;
		struct page *array = heap_get_page(stack_pop_var()->i);
		stack_push(array_find(array, start, end, v, fno));
		// FIXME: string key isn't freed if array is empty
		if (array && array_type(array->a_type) == AIN_STRING) {
			heap_unref(v.i);
		}
		break;
	}
	case A_REVERSE: {
		int array = stack_pop_var()->i;
		array_reverse(heap[array].page);
		break;
	}
	//
	// -- Shorthand Instructions (added in Alice 2010) ---
	//
	case SH_SR_ASSIGN: {
		int rval = stack_pop_var()->i;
		int lval = stack_pop().i;
		heap_struct_assign(lval, rval);
		break;
	}
	case SH_MEM_ASSIGN_LOCAL: {
		member_set(get_argument(0), local_get(get_argument(1)).i);
		break;
	}
	case A_NUMOF_GLOB_1: {
		int array = global_get(get_argument(0)).i;
		stack_push(array_numof(heap_get_page(array), 1));
		break;
	}
	case A_NUMOF_STRUCT_1: {
		int array = member_get(get_argument(0)).i;
		stack_push(array_numof(heap_get_page(array), 1));
		break;
	}
	case SH_MEM_ASSIGN_IMM: {
		member_set(get_argument(0), get_argument(1));
		break;
	}
	case SH_LOCALREFREF: {
		stack_push(local_get(get_argument(0)));
		stack_push(local_get(get_argument(0)+1));
		break;
	}
	case SH_LOCALASSIGN_SUB_IMM: {
		int n = get_argument(0);
		local_set(n, local_get(n).i - get_argument(1));
		break;
	}
	case SH_IF_LOC_LT_IMM: {
		if (local_get(get_argument(0)).i < get_argument(1))
			instr_ptr = get_argument(2);
		else
			instr_ptr += instruction_width(SH_IF_LOC_LT_IMM);
		break;
	}
	case SH_IF_LOC_GE_IMM: {
		if (local_get(get_argument(0)).i >= get_argument(1))
			instr_ptr = get_argument(2);
		else
			instr_ptr += instruction_width(SH_IF_LOC_GE_IMM);
		break;
	}
	case SH_LOCREF_ASSIGN_MEM: {
		struct page *page = heap_get_page(local_get(get_argument(0)).i);
		int index = local_get(get_argument(0)+1).i;
		page_set_var(page, index, member_get(get_argument(1)));
		break;
	}
	case PAGE_REF: {
		struct page *page = heap_get_page(stack_pop().i);
		stack_push(page_get_var(page, get_argument(0)));
		break;
	}
	case SH_GLOBAL_ASSIGN_LOCAL: {
		global_set(get_argument(0), local_get(get_argument(1)), true);
		break;
	}
	case SH_STRUCTREF_GT_IMM: {
		stack_push(member_get(get_argument(0)).i > get_argument(1) ? 1 : 0);
		break;
	}
	case SH_STRUCT_ASSIGN_LOCALREF_ITOB: {
		member_set(get_argument(0), !!local_get(get_argument(1)).i);
		break;
	}
	case SH_LOCAL_ASSIGN_STRUCTREF: {
		local_set(get_argument(0), member_get(get_argument(1)).i);
		break;
	}
	case SH_IF_STRUCTREF_NE_LOCALREF: {
		if (member_get(get_argument(0)).i != local_get(get_argument(1)).i)
			instr_ptr = get_argument(2);
		else
			instr_ptr += instruction_width(SH_IF_STRUCTREF_NE_LOCALREF);
		break;
	}
	case SH_IF_STRUCTREF_GT_IMM: {
		if (member_get(get_argument(0)).i > get_argument(1))
			instr_ptr = get_argument(2);
		else
			instr_ptr += instruction_width(SH_IF_STRUCTREF_GT_IMM);
		break;
	}
	case SH_STRUCTREF_CALLMETHOD_NO_PARAM: {
		int memb_page = member_get(get_argument(0)).i;
		function_call(get_argument(1), instr_ptr + instruction_width(SH_STRUCTREF_CALLMETHOD_NO_PARAM));
		call_stack[call_stack_ptr-1].struct_page = memb_page;
		break;
	}
	case SH_STRUCTREF2: {
		int memb = member_get(get_argument(0)).i;
		stack_push(page_get_var(heap_get_page(memb), get_argument(1)));
		break;
	}
	case SH_REF_STRUCTREF2: {
		int page = stack_pop().i;
		int memb = page_get_var(heap_get_page(page), get_argument(0)).i;
		stack_push(page_get_var(heap_get_page(memb), get_argument(1)));
		break;
	}
	case SH_STRUCTREF3: {
		int memb0 = member_get(get_argument(0)).i;
		int memb1 = page_get_var(heap_get_page(memb0), get_argument(1)).i;
		stack_push(page_get_var(heap_get_page(memb1), get_argument(2)));
		break;
	}
	case SH_STRUCTREF2_CALLMETHOD_NO_PARAM: {
		int memb1 = member_get(get_argument(0)).i;
		int memb2 = page_get_var(heap_get_page(memb1), get_argument(1)).i;
		function_call(get_argument(2), instr_ptr + instruction_width(SH_STRUCTREF2_CALLMETHOD_NO_PARAM));
		call_stack[call_stack_ptr-1].struct_page = memb2;
		break;
	}
	case SH_IF_STRUCTREF_Z: {
		if (!member_get(get_argument(0)).i)
			instr_ptr = get_argument(1);
		else
			instr_ptr += instruction_width(SH_IF_STRUCTREF_Z);
		break;
	}
	case SH_IF_STRUCT_A_NOT_EMPTY: {
		struct page *array = heap_get_page(member_get(get_argument(0)).i);
		if (array && array->nr_vars)
			instr_ptr = get_argument(1);
		else
			instr_ptr += instruction_width(SH_IF_STRUCT_A_NOT_EMPTY);
		break;
	}
	case SH_IF_LOC_GT_IMM: {
		if (local_get(get_argument(0)).i > get_argument(1))
			instr_ptr = get_argument(2);
		else
			instr_ptr += instruction_width(SH_IF_LOC_GT_IMM);
		break;
	}
	case SH_IF_STRUCTREF_NE_IMM: {
		if (member_get(get_argument(0)).i != get_argument(1))
			instr_ptr = get_argument(2);
		else
			instr_ptr += instruction_width(SH_IF_STRUCTREF_NE_IMM);
		break;
	}
	case THISCALLMETHOD_NOPARAM: {
		int this_page = struct_page_slot();
		function_call(get_argument(0), instr_ptr + instruction_width(THISCALLMETHOD_NOPARAM));
		call_stack[call_stack_ptr-1].struct_page = this_page;
		break;
	}
	case SH_IF_LOC_NE_IMM: {
		if (local_get(get_argument(0)).i != get_argument(1))
			instr_ptr = get_argument(2);
		else
			instr_ptr += instruction_width(SH_IF_LOC_NE_IMM);
		break;
	}
	case SH_IF_STRUCTREF_EQ_IMM: {
		if (member_get(get_argument(0)).i == get_argument(1))
			instr_ptr = get_argument(2);
		else
			instr_ptr += instruction_width(SH_IF_STRUCTREF_EQ_IMM);
		break;
	}
	case SH_GLOBAL_ASSIGN_IMM: {
		global_set(get_argument(0), (union vm_value) { .i = get_argument(1) }, false);
		break;
	}
	case SH_LOCALSTRUCT_ASSIGN_IMM: {
		struct page *page = heap_get_page(local_get(get_argument(0)).i);
		page_set_var(page, get_argument(1), get_argument(2));
		break;
	}
	case SH_STRUCT_A_PUSHBACK_LOCAL_STRUCT: {
		int struct_type;
		int array = member_get(get_argument(0)).i;
		union vm_value val = vm_copy(local_get(get_argument(1)), AIN_STRUCT);
		enum ain_data_type data_type = variable_type(struct_page(), get_argument(0), &struct_type, NULL);
		heap_set_page(array, array_pushback(heap_get_page(array), val, data_type, struct_type));
		break;
	}
	case SH_GLOBAL_A_PUSHBACK_LOCAL_STRUCT: {
		int struct_type;
		int array = global_get(get_argument(0)).i;
		union vm_value val = vm_copy(local_get(get_argument(1)), AIN_STRUCT);
		enum ain_data_type data_type = variable_type(global_page(), get_argument(0), &struct_type, NULL);
		heap_set_page(array, array_pushback(heap_get_page(array), val, data_type, struct_type));
		break;
	}
	case SH_LOCAL_A_PUSHBACK_LOCAL_STRUCT: {
		int struct_type;
		int array = local_get(get_argument(0)).i;
		union vm_value val = vm_copy(local_get(get_argument(1)), AIN_STRUCT);
		enum ain_data_type data_type = variable_type(local_page(), get_argument(0), &struct_type, NULL);
		heap_set_page(array, array_pushback(heap_get_page(array), val, data_type, struct_type));
		break;
	}
	case SH_IF_SREF_NE_STR0: {
		struct string *a = heap_get_string(stack_pop_var()->i);
		struct string *b = ain->strings[get_argument(0)];
		if (strcmp(a->text, b->text))
			instr_ptr = get_argument(1);
		else
			instr_ptr += instruction_width(SH_IF_SREF_NE_STR0);
		break;
	}
	case SH_S_ASSIGN_REF: {
		int rval = stack_pop_var()->i;
		int lval = stack_pop().i;
		heap_string_assign(lval, heap_get_string(rval));
		break;
	}
	case SH_A_FIND_SREF: {
		union vm_value *v = stack_pop_var();
		int end = stack_pop().i;
		int start = stack_pop().i;
		int array = stack_pop_var()->i;
		stack_push(array_find(heap_get_page(array), start, end, *v, 0));
		break;
	}
	case SH_SREF_EMPTY: {
		stack_push(!heap_get_string(stack_pop_var()->i)->size);
		break;
	}
	case SH_STRUCTSREF_EQ_LOCALSREF: {
		struct string *a = heap_get_string(member_get(get_argument(0)).i);
		struct string *b = heap_get_string(local_get(get_argument(1)).i);
		stack_push(!strcmp(a->text, b->text));
		break;
	}
	case SH_LOCALSREF_EQ_STR0: {
		struct string *a = heap_get_string(local_get(get_argument(0)).i);
		struct string *b = ain->strings[get_argument(1)];
		stack_push(!strcmp(a->text, b->text));
		break;
	}
	case SH_STRUCTSREF_NE_LOCALSREF: {
		struct string *a = heap_get_string(member_get(get_argument(0)).i);
		struct string *b = heap_get_string(local_get(get_argument(1)).i);
		stack_push(!!strcmp(a->text, b->text));
		break;
	}
	case SH_LOCALSREF_NE_STR0: {
		struct string *a = heap_get_string(local_get(get_argument(0)).i);
		struct string *b = ain->strings[get_argument(1)];
		stack_push(!!strcmp(a->text, b->text));
		break;
	}
	case SH_STRUCT_SR_REF: {
		int sr = member_get(get_argument(0)).i;
		stack_push(vm_copy_page(heap_get_page(sr)));
		// NOTE: argument 1 (struct type) not used
		break;
	}
	case SH_STRUCT_S_REF: {
		int str = member_get(get_argument(0)).i;
		stack_push_string(string_ref(heap_get_string(str)));
		break;
	}
	case S_REF2: {
		struct page *page = heap_get_page(stack_pop().i);
		struct string *s = heap_get_string(page_get_var(page, get_argument(0)).i);
		stack_push_string(string_ref(s));
		break;
	}
	case SH_REF_LOCAL_ASSIGN_STRUCTREF2: {
		struct page *memb = heap_get_page(member_get(get_argument(0)).i);
		int page = local_get(get_argument(1)).i;
		int var = local_get(get_argument(1) + 1).i;
		page_set_var(heap_get_page(page), var, page_get_var(memb, get_argument(2)));
		break;
	}
	case SH_GLOBAL_S_REF: {
		int str = global_get(get_argument(0)).i;
		stack_push_string(string_ref(heap_get_string(str)));
		break;
	}
	case SH_LOCAL_S_REF: {
		int str = local_get(get_argument(0)).i;
		stack_push_string(string_ref(heap_get_string(str)));
		break;
	}
	case SH_LOCALREF_SASSIGN_LOCALSREF: {
		int lval = local_get(get_argument(0)).i;
		int rval = local_get(get_argument(1)).i;
		heap_string_assign(lval, heap_get_string(rval));
		break;
	}
	case SH_LOCAL_APUSHBACK_LOCALSREF: {
		int array = local_get(get_argument(0)).i;
		union vm_value val = vm_copy(local_get(get_argument(1)), AIN_STRING);
		heap_set_page(array, array_pushback(heap_get_page(array), val, AIN_ARRAY_STRING, -1));
		break;
	}
	case SH_S_ASSIGN_CALLSYS19: {
		struct string *name = get_func_stack_name(stack_pop().i);
		heap_string_assign(stack_pop().i, name);
		free_string(name);
		break;
	}
	case SH_S_ASSIGN_STR0: {
		int lval = stack_pop().i;
		heap_string_assign(lval, ain->strings[get_argument(0)]);
		break;
	}
	case SH_SASSIGN_LOCALSREF: {
		int lval = stack_pop().i;
		struct string *rval = heap_get_string(local_get(get_argument(0)).i);
		heap_string_assign(lval, rval);
		break;
	}
	case SH_STRUCTREF_SASSIGN_LOCALSREF: {
		int lval = member_get(get_argument(0)).i;
		int rval = local_get(get_argument(1)).i;
		heap_string_assign(lval, heap_get_string(rval));
		break;
	}
	case SH_LOCALSREF_EMPTY: {
		stack_push(!heap_get_string(local_get(get_argument(0)).i)->size);
		break;
	}
	case SH_GLOBAL_APUSHBACK_LOCALSREF: {
		int array = global_get(get_argument(0)).i;
		union vm_value val = vm_copy(local_get(get_argument(1)), AIN_STRING);
		heap_set_page(array, array_pushback(heap_get_page(array), val, AIN_ARRAY_STRING, -1));
		break;
	}
	case SH_STRUCT_APUSHBACK_LOCALSREF: {
		int array = member_get(get_argument(0)).i;
		union vm_value val = vm_copy(local_get(get_argument(1)), AIN_STRING);
		heap_set_page(array, array_pushback(heap_get_page(array), val, AIN_ARRAY_STRING, -1));
		break;
	}
	case SH_STRUCTSREF_EMPTY: {
		stack_push(!heap_get_string(member_get(get_argument(0)).i)->size);
		break;
	}
	case SH_GLOBALSREF_EMPTY: {
		stack_push(!heap_get_string(global_get(get_argument(0)).i)->size);
		break;
	}
	case SH_SASSIGN_STRUCTSREF: {
		int lval = stack_pop().i;
		int rval = member_get(get_argument(0)).i;
		heap_string_assign(lval, heap_get_string(rval));
		break;
	}
	case SH_SASSIGN_GLOBALSREF: {
		int lval = stack_pop().i;
		int rval = global_get(get_argument(0)).i;
		heap_string_assign(lval, heap_get_string(rval));
		break;
	}
	case SH_STRUCTSREF_NE_STR0: {
		struct string *a = heap_get_string(member_get(get_argument(0)).i);
		struct string *b = ain->strings[get_argument(1)];
		stack_push(!!strcmp(a->text, b->text));
		break;
	}
	case SH_GLOBALSREF_NE_STR0: {
		struct string *a = heap_get_string(global_get(get_argument(0)).i);
		struct string *b = ain->strings[get_argument(1)];
		stack_push(!!strcmp(a->text, b->text));
		break;
	}
	case SH_LOC_LT_IMM_OR_LOC_GE_IMM: {
		int i = local_get(get_argument(0)).i;
		stack_push(i < get_argument(1) || i >= get_argument(2));
		break;
	}
	case A_SORT_MEM: {
		int mno = stack_pop().i;
		int array = stack_pop_var()->i;
		array_sort_mem(heap[array].page, mno);
		break;
	}
	case DG_SET: {
		int fun = stack_pop().i;
		int obj = stack_pop().i;
		int dg_i = stack_pop().i;
		delete_page(dg_i);
		heap_set_page(dg_i, delegate_new_from_method(obj, fun));
		break;
	}
	case DG_ADD: {
		int fun = stack_pop().i;
		int obj = stack_pop().i;
		int dg_i = stack_pop().i;
		struct page *dg = heap_get_delegate_page(dg_i);
		heap_set_page(dg_i, delegate_append(dg, obj, fun));
		break;
	}
	case DG_CALL: { // DG_TYPE, ADDR
		delegate_call(get_argument(0), get_argument(1));
		break;
	}
	case DG_NUMOF: {
		int dg = stack_pop().i;
		stack_push(delegate_numof(heap_get_delegate_page(dg)));
		break;
	}
	case DG_EXIST: {
		int fun = stack_pop().i;
		int obj = stack_pop().i;
		int dg_i = stack_pop().i;
		stack_push(delegate_contains(heap_get_delegate_page(dg_i), obj, fun));
		break;
	}
	case DG_ERASE: {
		int fun = stack_pop().i;
		int obj = stack_pop().i;
		int dg_i = stack_pop().i;
		delegate_erase(heap_get_delegate_page(dg_i), obj, fun);
		break;
	}
	case DG_CLEAR: {
		int slot = stack_pop().i;
		if (!slot)
			break;
		heap_set_page(slot, delegate_clear(heap_get_delegate_page(slot)));
		break;
	}
	case DG_COPY: {
		stack_push(vm_copy_page(heap_get_delegate_page(stack_pop().i)));
		break;
	}
	case DG_ASSIGN: {
		int set_i = stack_pop().i;
		int dst_i;
		if (ain->version >= 14) {
			// v14: destination is a 2-slot variable reference
			union vm_value *var = stack_pop_var();
			dst_i = var ? var->i : -1;
		} else {
			dst_i = stack_pop().i;
		}
		struct page *set = heap_get_delegate_page(set_i);
		struct page *new_dg = copy_page(set);
		if (dst_i > 0) {
			delete_page(dst_i);
			heap_set_page(dst_i, new_dg);
		}
		stack_push(set_i);
		break;
	}
	case DG_PLUSA: {
		int add_i = stack_pop().i;
		int dst_i;
		if (ain->version >= 14) {
			// v14: destination is a 2-slot variable reference
			union vm_value *var = stack_pop_var();
			dst_i = var ? var->i : -1;
		} else {
			dst_i = stack_pop().i;
		}
		struct page *add = heap_get_delegate_page(add_i);
		struct page *dst = heap_get_delegate_page(dst_i);
		heap_set_page(dst_i, delegate_plusa(dst, add));
		stack_push(add_i);
		break;
	}
	case DG_MINUSA: {
		int minus_i = stack_pop().i;
		int dst_i;
		if (ain->version >= 14) {
			// v14: destination is a 2-slot variable reference
			union vm_value *var = stack_pop_var();
			dst_i = var ? var->i : -1;
		} else {
			dst_i = stack_pop().i;
		}
		struct page *minus = heap_get_delegate_page(minus_i);
		struct page *dst = heap_get_delegate_page(dst_i);
		heap_set_page(dst_i, delegate_minusa(dst, minus));
		stack_push(minus_i);
		break;
	}
	case DG_POP: {
		heap_unref(stack_pop().i);
		break;
	}
	case DG_NEW_FROM_METHOD: {
		int fun = stack_pop().i;
		int obj = stack_pop().i;
		stack_push(heap_alloc_page(delegate_new_from_method(obj, fun)));
		break;
	}
	case DG_CALLBEGIN: { // DG_TYPE
		int dg_no = get_argument(0);
		if (dg_no < 0 || dg_no >= ain->nr_delegates)
			VM_ERROR("Invalid delegate index");
		struct ain_function_type *dg = &ain->delegates[dg_no];

		// Stack before: [dg_page, arg0, arg1, ...]
		int dg_page = stack_peek(dg->nr_arguments).i;

		if (ain->version >= 14) {
			// v14: save dg_page and args off-stack in caller's frame
			struct function_call *frame = &call_stack[call_stack_ptr - 1];
			frame->dg_page = dg_page;
			frame->dg_index = 0;
			frame->dg_nr_args = dg->nr_arguments < 8 ? dg->nr_arguments : 8;

			// Save args (they are above dg_page on the stack)
			for (int i = 0; i < frame->dg_nr_args; i++) {
				frame->dg_args[i] = stack[stack_ptr - dg->nr_arguments + i];
			}

			// Pop args and dg_page from stack
			stack_ptr -= (dg->nr_arguments + 1);

			// Push return placeholder(s) if non-void
			if (dg->return_type.data != AIN_VOID) {
				int ret_slots = 1;
				if (dg->return_type.data == AIN_OPTION)
					ret_slots = 2;
				else if (dg->return_type.data == AIN_WRAP) {
					struct ain_type *ws = dg->return_type.array_type;
					bool prim = ws && (ws->data == AIN_INT || ws->data == AIN_BOOL ||
						ws->data == AIN_FLOAT || ws->data == AIN_LONG_INT ||
						ws->data == AIN_ENUM || ws->data == AIN_ENUM2 ||
				ws->data == AIN_IFACE || ws->data == AIN_IFACE_WRAP);
					if (prim) ret_slots = 2;
				}
				for (int r = 0; r < ret_slots; r++)
					stack_push(0);
			}
		} else {
			// Pre-v14: rearrange stack to [arg0, ..., dg_page, 0(dg_index)]
			for (int i = 0; i < dg->nr_arguments; i++) {
				int pos = (stack_ptr - dg->nr_arguments) + i;
				stack[pos-1] = stack[pos];
			}
			stack[stack_ptr-1].i = dg_page;
			stack_push(0);

			if (dg->return_type.data != AIN_VOID) {
				stack_push(0);
			}
		}
		break;
	}
	case DG_NEW: {
		// DG_NEW: create empty delegate, push slot
		// In v14 alloc context: stack=[page, idx] from X_DUP/DELETE pattern
		// Create empty delegate page (NULL = empty)
		int slot = heap_alloc_slot(VM_PAGE);
		heap_set_page(slot, NULL);
		stack_push(slot);
		break;
	}
	case DG_STR_TO_METHOD: {
		// Resolve method name string to function index
		// v14: 1 arg (delegate type index)
		// Stack: [..., obj_page, string_slot]
		// Pops string_slot only, pushes function number
		// obj_page stays on stack (becomes struct_page part of HLL_FUNC arg)
		int str_slot = stack_pop().i;
		int obj_page = stack_peek(0).i;  // peek, don't pop
		struct string *name = heap_get_string(str_slot);
		int fno = -1;
		if (name) {
			if (obj_page >= 0) {
				// Method call: resolve "StructName@MethodName"
				struct page *page = heap_get_page(obj_page);
				if (page && page->type == STRUCT_PAGE && page->index >= 0
				    && page->index < ain->nr_structures) {
					const char *sname = ain->structures[page->index].name;
					char buf[512];
					snprintf(buf, sizeof(buf), "%s@%s", sname, name->text);
					fno = ain_get_function(ain, buf);
				}
			} else {
				// Static function: resolve by plain name
				fno = ain_get_function(ain, name->text);
			}
			if (fno < 0) {
				WARNING("DG_STR_TO_METHOD: cannot resolve '%s' (obj=%d)",
					name->text, obj_page);
			}
		}
		heap_unref(str_slot);
		stack_push(fno);
		break;
	}
	case X_MOV: {
		// X_MOV n, offset: rotate top n stack slots.
		// Top `offset` values move to the bottom of the n-slot window.
		// Stack size does NOT change. Equivalent to SWAP when n=2,offset=1.
		int n = get_argument(0);
		int offset = get_argument(1);
		if (n > 1 && offset > 0 && offset < n) {
			union vm_value tmp_buf[16];
			union vm_value *buf = (n <= 16) ? tmp_buf : malloc(n * sizeof(union vm_value));
			for (int i = 0; i < n; i++) {
				buf[i] = stack[stack_ptr - n + i];
			}
			for (int i = 0; i < n; i++) {
				stack[stack_ptr - n + i] = buf[(i + n - offset) % n];
			}
			if (buf != tmp_buf) free(buf);
		}
		break;
	}
	case X_REF: {
		// X_REF n: dereference a 2-slot variable reference and push n consecutive values
		int n = get_argument(0);
		if (n <= 0) n = 1;
		int32_t var_idx = stack_pop().i;
		int32_t heap_idx = stack_pop().i;
		struct page *page = NULL;
		if (heap_idx >= 0 && (size_t)heap_idx < heap_size && heap[heap_idx].ref > 0
		    && heap[heap_idx].type == VM_PAGE)
			page = heap[heap_idx].page;
		if (page && var_idx >= 0 && var_idx + n <= page->nr_vars) {
			for (int i = 0; i < n; i++) {
				stack_push(page->values[var_idx + i]);
			}
		} else {
			for (int i = 0; i < n; i++) {
				stack_push(0);
			}
		}
		break;
	}
	case X_ASSIGN: {
		// X_ASSIGN n: stack = [..., heap_idx, page_idx, v0..v(n-1)]
		// Pop n values, pop 2-slot ref, write values, push values back
		int n = get_argument(0);
		if (n <= 0) n = 1;
		union vm_value small_buf[64];
		union vm_value *vals = (n <= 64) ? small_buf : malloc(n * sizeof(union vm_value));
		for (int i = n - 1; i >= 0; i--) {
			vals[i] = stack_pop();
		}
		union vm_value *var = stack_pop_var();
		if (var) {
			for (int i = 0; i < n; i++) {
				var[i] = vals[i];
			}
		}
		for (int i = 0; i < n; i++) {
			stack_push(vals[i]);
		}
		if (vals != small_buf) free(vals);
		break;
	}
	case X_DUP: {
		// X_DUP n: duplicate top n values on stack
		int n = get_argument(0);
		if (n <= 0) n = 1;
		int base = stack_ptr - n;
		for (int i = 0; i < n; i++) {
			stack_push(stack[base + i]);
		}
		break;
	}
	case X_GETENV: {
		// X_GETENV: get parent environment page (lambda captures)
		// Bytecode pattern: PUSHLOCALPAGE; X_GETENV
		// X_GETENV consumes the PUSHLOCALPAGE value and replaces it with
		// the captured environment (struct_page from delegate_call).
		stack_pop(); // consume preceding PUSHLOCALPAGE
		stack_push(struct_page_slot());
		break;
	}
	case X_SET: {
		// X_SET: assign value to variable reference (like X_ASSIGN 1 but no argument)
		// stack: [..., heap_idx, page_idx, value]
		union vm_value val = stack_pop();
		union vm_value *var = stack_pop_var();
		if (var) {
			*var = val;
		}
		stack_push(val);
		break;
	}
	case X_ICAST: {
		// X_ICAST struct_type: interface cast
		int struct_type = get_argument(0);
		// The value on stack is already the object; just leave it
		(void)struct_type;
		break;
	}
	case X_OP_SET: {
		// X_OP_SET arg: like X_ASSIGN but for option types
		// arg encoding: lower 16 bits = value count, upper 16 bits = type info
		// stack = [..., heap_idx, page_idx, val0, ..., val(n-1)]
		// Pop n values, pop 2-slot var ref, write n consecutive slots, push n values
		int n = get_argument(0) & 0xFFFF;
		if (n <= 0) n = 1;
		union vm_value vals[4];
		if (n > 4) n = 4;
		for (int i = n - 1; i >= 0; i--) {
			vals[i] = stack_pop();
		}
		union vm_value *var = stack_pop_var();
		if (var) {
			for (int i = 0; i < n; i++) {
				var[i] = vals[i];
			}
		}
		for (int i = 0; i < n; i++) {
			stack_push(vals[i]);
		}
		break;
	}
	case X_A_INIT: {
		// X_A_INIT arg: stack=[page, idx, size] → create array, assign to (page,idx), push slot
		// arg is NOT the data type — get actual type from variable definition
		int arg = get_argument(0);
		int size = stack_pop().i;
		int var_idx = stack_pop().i;
		int heap_idx = stack_pop().i;
		// Get data type from variable definition
		int struct_type = -1;
		enum ain_data_type data_type = AIN_ARRAY_INT; // fallback
		if (heap_idx == 0 && var_idx >= 0 && var_idx < ain->nr_globals) {
			data_type = ain->globals[var_idx].type.data;
			struct_type = ain->globals[var_idx].type.struc;
		} else if (heap_index_valid(heap_idx) && heap[heap_idx].page) {
			data_type = variable_type(heap[heap_idx].page, var_idx, &struct_type, NULL);
		}
		(void)arg;
		// Allocate array slot and page
		int slot = heap_alloc_slot(VM_PAGE);
		if (size > 0) {
			if (data_type == AIN_ARRAY || data_type == AIN_REF_ARRAY) {
				// Generic array (v14 type erasure) — create flat int array
				struct page *page = alloc_page(ARRAY_PAGE, data_type, size);
				for (int i = 0; i < size; i++)
					page->values[i].i = 0;
				heap_set_page(slot, page);
			} else {
				union vm_value dim = { .i = size };
				heap_set_page(slot, alloc_array(1, &dim, data_type, struct_type, false));
			}
		} else {
			heap_set_page(slot, NULL);
		}
		// Assign to variable
		if (heap_index_valid(heap_idx) && heap[heap_idx].page &&
		    var_idx >= 0 && var_idx < heap[heap_idx].page->nr_vars) {
			heap[heap_idx].page->values[var_idx].i = slot;
		}
		stack_push(slot);
		break;
	}
	case X_A_SIZE: {
		// X_A_SIZE: push array size
		int slot = stack_pop().i;
		struct page *page = (heap_index_valid(slot) && heap[slot].page) ? heap[slot].page : NULL;
		stack_push(page ? page->nr_vars : 0);
		break;
	}
	case X_TO_STR: {
		// X_TO_STR type: convert top of stack to string
		int type = get_argument(0);
		(void)type;
		// For now, create empty string
		int slot = heap_alloc_slot(VM_STRING);
		heap[slot].s = string_ref(&EMPTY_STRING);
		stack_pop(); // remove original value
		stack_push(slot);
		break;
	}
	// -- NOOPs ---
	case FUNC:
		if (get_argument(0) == 165) {
			int sp_slot = struct_page_slot();
			if (sp_slot > 0 && page_index_valid(sp_slot)) {
				struct page *p = heap[sp_slot].page;
				WARNING("FUNC 165 entry: struct_page=%d nr_vars=%d values[0]=%d type=%d ref=%d",
					sp_slot, p->nr_vars, p->values[0].i, p->type, heap[sp_slot].ref);
			} else {
				WARNING("FUNC 165 entry: struct_page=%d INVALID (heap_type=%d ref=%d)",
					sp_slot,
					(sp_slot >= 0 && (size_t)sp_slot < heap_size) ? heap[sp_slot].type : -99,
					(sp_slot >= 0 && (size_t)sp_slot < heap_size) ? heap[sp_slot].ref : -99);
			}
		}
		break;
	default:
#ifdef DEBUGGER_ENABLED
		if ((opcode & OPTYPE_MASK) == BREAKPOINT) {
			dbg_handle_breakpoint();
			return execute_instruction(opcode & ~OPTYPE_MASK);
		}
#endif
		VM_ERROR("Illegal opcode: 0x%04x", opcode);
	}
	return opcode;
}

static void vm_execute(void)
{
	for (;;) {
		uint16_t opcode;
		if (instr_ptr == VM_RETURN) {
			return;
		}
		if (unlikely(instr_ptr >= ain->code_size)) {
			VM_ERROR("Illegal instruction pointer: 0x%08lX", instr_ptr);
		}
		opcode = get_opcode(instr_ptr);
		insn_count++;
		trace_buf[trace_buf_idx].ip = instr_ptr;
		trace_buf[trace_buf_idx].op = opcode;
		trace_buf[trace_buf_idx].sp = stack_ptr;
		trace_buf_idx = (trace_buf_idx + 1) % TRACE_BUF_SIZE;
		// Periodic function trace (every 5M instructions)
		if (unlikely(insn_count % 5000000 == 0)) {
			int fno = call_stack_ptr > 0 ? call_stack[call_stack_ptr-1].fno : -1;
			WARNING("[%lluM] ip=0x%lX fno=%d(%s) sp=%d csp=%d heap_size=%zu",
				insn_count / 1000000, (unsigned long)instr_ptr,
				fno, (fno >= 0 && fno < ain->nr_functions) ? ain->functions[fno].name : "?",
				stack_ptr, call_stack_ptr, heap_size);
		}
		// Global page integrity watchpoint
		static int global_page_ok = 1;
		if (global_page_ok && heap_size > 0 && heap[0].page &&
		    heap[0].page->type != GLOBAL_PAGE) {
			global_page_ok = 0;
			int cur_fno = call_stack_ptr > 0 ? call_stack[call_stack_ptr-1].fno : -1;
			WARNING("!!! GLOBAL PAGE CORRUPTED at insn #%llu !!!", insn_count);
			WARNING("  ip=0x%lX op=%s fno=%d(%s) sp=%d csp=%d",
				(unsigned long)instr_ptr, instructions[opcode].name,
				cur_fno, cur_fno >= 0 ? ain->functions[cur_fno].name : "?",
				stack_ptr, call_stack_ptr);
			WARNING("  page->type=%d (expected 0=GLOBAL_PAGE) nr_vars=%d",
				heap[0].page->type, heap[0].page->nr_vars);
			dump_trace_buf();
			// Dump call stack
			for (int k = call_stack_ptr - 1; k >= 0 && k >= call_stack_ptr - 10; k--) {
				WARNING("  call_stack[%d]: fno=%d(%s) ip=0x%lX struct_page=%d",
					k, call_stack[k].fno,
					call_stack[k].fno >= 0 && call_stack[k].fno < ain->nr_functions
						? ain->functions[call_stack[k].fno].name : "?",
					(unsigned long)call_stack[k].call_address,
					call_stack[k].struct_page);
			}
		}
		opcode = execute_instruction(opcode);
		instr_ptr += instructions[opcode].ip_inc;
	}
}

static void call_global_destructors(void)
{
	if (heap_size <= 0 || heap[0].ref <= 0)
		return;
	struct page *global_page = heap_get_page(0);
	if (!global_page) return;
	for (int i = global_page->nr_vars - 1; i >= 0; i--) {
		if (variable_type(global_page, i, NULL, NULL) != AIN_STRUCT)
			continue;
		int slot = global_page->values[i].i;
		struct page *p = heap_get_page(slot);
		if (p)
			delete_struct(p->index, slot);
	}
}

static void vm_free(void)
{
	if (game_dungeons_and_dolls) {
		// Dungeons & Dolls saves the game state in destructors of global variables
		call_global_destructors();
	}

	// call library exit routines
	exit_libraries();
	// flush call stack
	for (int i = call_stack_ptr - 1; i >= 0; i--) {
		exit_unref(call_stack[i].page_slot);
	}
	// free globals
	if (heap_size > 0 && heap[0].ref > 0)
		exit_unref(0);

	vm_reset_once = true;
}

static jmp_buf reset_buf;

_Noreturn void vm_reset(void)
{
	vm_free();
	longjmp(reset_buf, 1);
}

static void sigabrt_handler(int sig)
{
	const char *signame = (sig == SIGSEGV) ? "SIGSEGV" : "SIGABRT";
	WARNING("=== Signal %s caught ===", signame);
	WARNING("ip=0x%lX sp=%d csp=%d fno=%d",
		(unsigned long)instr_ptr, stack_ptr, call_stack_ptr,
		call_stack_ptr > 0 ? call_stack[call_stack_ptr-1].fno : -1);
	dump_trace_buf();
	// Print C backtrace (use fd-based version for signal safety)
	void *bt[64];
	int n = backtrace(bt, 64);
	backtrace_symbols_fd(bt, n, 2);
	signal(sig, SIG_DFL);
	raise(sig);
}

int vm_execute_ain(struct ain *program)
{
	ain = program;

	// Install SIGABRT handler to dump trace buffer and C backtrace
	signal(SIGABRT, sigabrt_handler);
	signal(SIGSEGV, sigabrt_handler);

	setjmp(reset_buf);

	// initialize VM state
	if (!stack) {
		stack_size = INITIAL_STACK_SIZE;
		stack = xmalloc(INITIAL_STACK_SIZE * sizeof(union vm_value));
	}
	stack_ptr = 0;
	call_stack_ptr = 0;

	initialize_instructions(ain->version);
	heap_init();
	init_libraries();

	// Initialize globals
	heap[0].ref = 1;
	heap[0].seq = heap_next_seq++;
	heap_set_page(0, alloc_page(GLOBAL_PAGE, 0, ain->nr_globals));
	for (int i = 0; i < ain->nr_globals; i++) {
		if (ain->version >= 14) {
			// v14: alloc function handles all global initialization
			// Use -1 so DELETE on uninitialized globals is safely skipped
			heap[0].page->values[i].i = -1;
		} else if (ain->globals[i].type.data == AIN_STRUCT) {
			// XXX: need to allocate storage for global structs BEFORE calling
			//      constructors.
			heap[0].page->values[i].i = alloc_struct(ain->globals[i].type.struc);
		} else {
			heap[0].page->values[i] = variable_initval(ain->globals[i].type.data);
		}
	}
	for (int i = 0; i < ain->nr_initvals; i++) {
		int32_t index;
		struct ain_initval *v = &ain->global_initvals[i];
		switch (v->data_type) {
		case AIN_STRING:
			index = heap_alloc_slot(VM_STRING);
			heap[0].page->values[v->global_index].i = index;
			heap[index].s = make_string(v->string_value, strlen(v->string_value));
			break;
		default:
			heap[0].page->values[v->global_index].i = v->int_value;
			break;
		}
	}

	WARNING("VM: alloc=%d, main=%d, nr_globals=%d, nr_functions=%d, version=%d",
		ain->alloc, ain->main, ain->nr_globals, ain->nr_functions, ain->version);
	if (ain->alloc >= 0) {
		WARNING("VM: alloc func '%s' address=0x%X",
			ain->functions[ain->alloc].name, ain->functions[ain->alloc].address);
	}
	if (ain->alloc >= 0 && ain->functions[ain->alloc].address != 0xFFFFFFFF) {
		WARNING("VM: calling alloc function (sp=%d csp=%d)...", stack_ptr, call_stack_ptr);
		vm_call(ain->alloc, -1); // function "0": allocate global arrays
		WARNING("VM: alloc done (sp=%d csp=%d)", stack_ptr, call_stack_ptr);
	} else {
		WARNING("VM: skipping alloc (address=0xFFFFFFFF or alloc<0)");
	}

	if (ain->version >= 14) {
		WARNING("VM: v14 — skipping global struct init (alloc handles init_struct)");
	} else {
		// global constructors must be called AFTER initializing non-struct variables
		// otherwise a global set in a constructor will be clobbered by its initval
		WARNING("VM: initializing %d global structs...", ain->nr_globals);
		for (int i = 0; i < ain->nr_globals; i++) {
			if (ain->globals[i].type.data == AIN_STRUCT) {
				int slot = heap[0].page->values[i].i;
				if (slot > 0 && heap_index_valid(slot))
					init_struct(ain->globals[i].type.struc, slot);
			}
		}
	}
	// Debug: check global[67] and heap[0] state before calling main
	if (heap[0].page && 67 < heap[0].page->nr_vars) {
		int g67 = heap[0].page->values[67].i;
		WARNING("VM: global[67]=%d heap[0].page=%p nr_vars=%d", g67, (void*)heap[0].page, heap[0].page->nr_vars);
		if (g67 > 0 && (size_t)g67 < heap_size) {
			WARNING("VM:   heap[%d]: type=%d ref=%d page=%p", g67, heap[g67].type, heap[g67].ref,
				(void*)heap[g67].page);
		}
	} else {
		WARNING("VM: heap[0].page=%p (INVALID!)", (void*)heap[0].page);
	}
	// Debug: scan all structs for vmethods (vtable data)
	{
		int with_vmethods = 0, with_interfaces = 0;
		for (int si = 0; si < ain->nr_structures; si++) {
			struct ain_struct *s = &ain->structures[si];
			if (s->nr_vmethods > 0) with_vmethods++;
			if (s->nr_interfaces > 0) with_interfaces++;
		}
		WARNING("VM: struct stats: %d/%d have vmethods, %d/%d have interfaces",
			with_vmethods, ain->nr_structures, with_interfaces, ain->nr_structures);
		// Dump first 10 structs with vmethods as examples
		int shown = 0;
		for (int si = 0; si < ain->nr_structures && shown < 10; si++) {
			struct ain_struct *s = &ain->structures[si];
			if (s->nr_vmethods <= 0) continue;
			shown++;
			WARNING("VM:   struct#%d '%s' nr_vmethods=%d nr_interfaces=%d member[0]='%s' type=%d",
				si, s->name, s->nr_vmethods, s->nr_interfaces,
				s->nr_members > 0 ? s->members[0].name : "(none)",
				s->nr_members > 0 ? s->members[0].type.data : -1);
			for (int v = 0; v < s->nr_vmethods && v < 5; v++) {
				const char *fn = (s->vmethods[v] >= 0 && s->vmethods[v] < ain->nr_functions)
					? ain->functions[s->vmethods[v]].name : "?";
				WARNING("VM:     vmethod[%d] = func#%d (%s)", v, s->vmethods[v], fn);
			}
			if (s->nr_vmethods > 5) WARNING("VM:     ... +%d more", s->nr_vmethods - 5);
		}
	}
	WARNING("VM: calling main (sp=%d csp=%d)...", stack_ptr, call_stack_ptr);

	vm_call(ain->main, -1);
	WARNING("VM: main returned");
	return stack_pop().i;
}

void vm_stack_trace(void)
{
	for (int i = call_stack_ptr - 1; i >= 0; i--) {
		struct ain_function *f = &ain->functions[call_stack[i].fno];
		uint32_t addr = (i == call_stack_ptr - 1) ? instr_ptr : call_stack[i+1].call_address;
		sys_warning("\t0x%08x in %s\n", addr, display_sjis0(f->name));
	}
}

_Noreturn void _vm_error(const char *fmt, ...)
{
	va_list ap;
	WARNING("_vm_error entered");
	fflush(stderr);
	va_start(ap, fmt);
	sys_vwarning(fmt, ap);
	va_end(ap);
	fflush(stderr);
	WARNING("_vm_error: printed message, calling stack trace");
	fflush(stderr);
	sys_warning("at %s (0x%X) in:\n", current_instruction_name(), instr_ptr);
	vm_stack_trace();
	fflush(stderr);
	WARNING("_vm_error: stack trace done, calling dbg_repl");
	fflush(stderr);

	char msg[1024];
	va_start(ap, fmt);
	vsnprintf(msg, 1024, fmt, ap);
	va_end(ap);

	dbg_repl(DBG_STOP_ERROR, msg);
	WARNING("_vm_error: calling sys_exit(1)");
	fflush(stderr);
	sys_exit(1);
}

int vm_time(void)
{
	return SDL_GetTicks();
}

void vm_sleep(int ms)
{
	SDL_Delay(ms);
}

_Noreturn void vm_exit(int code)
{
	vm_free();
#ifdef DEBUG_HEAP
	for (size_t i = 0; i < heap_size; i++) {
		if (heap[i].ref > 0)
			heap_describe_slot(i);
	}
	sys_message("Number of leaked objects: %d\n", heap_free_ptr);
#endif
	sys_exit(code);
}
