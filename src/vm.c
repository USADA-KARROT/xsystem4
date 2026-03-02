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
#include <time.h>
#include <unistd.h>
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
#include "gfx/gfx.h"
#include "input.h"
#include "parts.h"
#include "savedata.h"
#include "vm.h"
#include "vm/heap.h"
#include "vm/page.h"
#include "scene.h"
#include "sprite.h"
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
static int mc_skip_total = 0;
static int nargs_skip_total = 0;
static int new_count = 0;
static int vmcall_count = 0;

// Native CASTimer: intercept broken CASTimer method_call skips with real timers.
// CASTimerManager's struct page gets corrupted (LOCAL_PAGE instead of STRUCT_PAGE)
// during v14 init, causing all timer calls to be skipped and return 0.
#define CAS_TIMER_MAX 64
static struct {
	struct timespec epoch;
	bool active;
} cas_timers[CAS_TIMER_MAX];
static int cas_next_handle = 1;

static bool native_cas_timer_intercept(int fno, int struct_page, union vm_value *ret)
{
	const char *fname = ain->functions[fno].name;
	if (!strstr(fname, "CASTimer"))
		return false;

	// CASTimerManager methods
	if (strstr(fname, "CASTimerManager")) {
		if (strstr(fname, "CreateHandle")) {
			int h = cas_next_handle++;
			if (h > 0 && h < CAS_TIMER_MAX) {
				clock_gettime(CLOCK_MONOTONIC, &cas_timers[h].epoch);
				cas_timers[h].active = true;
			}
			ret->i = h;
			if (h == 1)
				WARNING("native_cas_timer: active (first CreateHandle)");
			return true;
		}
		if (strstr(fname, "GetObject")) {
			// Read handle from first argument in local page
			int pg_slot = call_stack[call_stack_ptr-1].page_slot;
			int handle = 1;
			if (pg_slot > 0 && heap_index_valid(pg_slot) && heap[pg_slot].page
				&& heap[pg_slot].page->nr_vars > 0) {
				handle = heap[pg_slot].page->values[0].i;
			}
			// Return negative handle as fake struct_page identifier
			ret->i = -(handle > 0 ? handle : 1);
			return true;
		}
		// Other Manager methods: default return
		ret->i = 0;
		return true;
	}

	// CASTimer / CASTimerImp instance methods
	int timer_id = (struct_page < 0) ? -struct_page : 1;
	if (timer_id <= 0 || timer_id >= CAS_TIMER_MAX) timer_id = 1;

	if (!cas_timers[timer_id].active) {
		clock_gettime(CLOCK_MONOTONIC, &cas_timers[timer_id].epoch);
		cas_timers[timer_id].active = true;
	}

	if (strstr(fname, "@Get") && !strstr(fname, "GetScaled") && !strstr(fname, "GetObject")) {
		struct timespec now;
		clock_gettime(CLOCK_MONOTONIC, &now);
		int elapsed = (int)((now.tv_sec - cas_timers[timer_id].epoch.tv_sec) * 1000
			+ (now.tv_nsec - cas_timers[timer_id].epoch.tv_nsec) / 1000000);
		if (elapsed < 0) elapsed = 0;
		ret->i = elapsed;
		return true;
	}
	if (strstr(fname, "GetScaled")) {
		struct timespec now;
		clock_gettime(CLOCK_MONOTONIC, &now);
		int elapsed = (int)((now.tv_sec - cas_timers[timer_id].epoch.tv_sec) * 1000
			+ (now.tv_nsec - cas_timers[timer_id].epoch.tv_nsec) / 1000000);
		if (elapsed < 0) elapsed = 0;
		ret->i = elapsed;
		return true;
	}
	if (strstr(fname, "@Reset")) {
		clock_gettime(CLOCK_MONOTONIC, &cas_timers[timer_id].epoch);
		ret->i = 0;
		return true;
	}

	// Constructor or other CASTimer methods: accept silently
	ret->i = 0;
	return true;
}

// vm_call timeout: when >0, vm_execute will bail out if insn_count exceeds this
static unsigned long long vm_call_insn_limit = 0;

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

// Stack of function call frames (v14 games use deeper call chains)
struct function_call call_stack[4096];
int32_t call_stack_ptr = 0;

struct ain *ain;
size_t instr_ptr = 0;

bool vm_reset_once = false;
bool vm_in_alloc_phase = false;

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
		if (underflow_count++ < 1) {
			WARNING("STACK UNDERFLOW: sp=%d ip=0x%lX fno=%d insn#%llu",
				stack_ptr, (unsigned long)instr_ptr,
				call_stack_ptr > 0 ? call_stack[call_stack_ptr-1].fno : -1,
				insn_count);
			dump_trace_buf();
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
// dummy_var is a buffer (not a single element) because X_ASSIGN can write
// var[0..n-1] for multi-slot assignments. A single element would corrupt memory.
static union vm_value dummy_var[64];
static union vm_value *stack_pop_var(void)
{
	int32_t page_index = stack_pop().i;
	int32_t heap_index = stack_pop().i;
	if (unlikely(!heap_index_valid(heap_index))) {
		static int inv_trace = 0;
		if (inv_trace < 5) {
			WARNING("stack_pop_var: invalid heap index %d/%d ip=0x%lX fno=%d csp=%d "
				"heap_ref=%d heap_type=%d",
				heap_index, page_index,
				(unsigned long)instr_ptr,
				call_stack_ptr > 0 ? call_stack[call_stack_ptr-1].fno : -1,
				call_stack_ptr,
				heap_index >= 0 && (size_t)heap_index < heap_size ? heap[heap_index].ref : -99,
				heap_index >= 0 && (size_t)heap_index < heap_size ? heap[heap_index].type : -99);
			inv_trace++;
		}
		dummy_var[0].i = 0;
		return dummy_var;
	}
	// Check heap type — must be VM_PAGE for page access
	if (unlikely(heap[heap_index].type != VM_PAGE)) {
		static int type_trace = 0;
		if (type_trace++ < 5) {
			int fno = call_stack_ptr > 0 ? call_stack[call_stack_ptr-1].fno : -1;
			WARNING("stack_pop_var: heap type mismatch %d/%d heap_type=%d (expected VM_PAGE) "
				"ip=0x%lX fno=%d '%s'",
				heap_index, page_index, heap[heap_index].type,
				(unsigned long)instr_ptr,
				fno, fno >= 0 ? ain->functions[fno].name : "?");
		}
		dummy_var[0].i = 0;
		return dummy_var;
	}
	if (unlikely(!heap[heap_index].page || page_index < 0 || page_index >= heap[heap_index].page->nr_vars)) {
		static int oob_trace = 0;
		if (oob_trace < 5) {
			int fno = call_stack_ptr > 0 ? call_stack[call_stack_ptr-1].fno : -1;
			struct page *p = heap[heap_index].page;
			WARNING("stack_pop_var: OOB %d/%d (page_nr_vars=%d page_idx=%d) ip=0x%lX fno=%d '%s' "
				"(func_nr_vars=%d) sp=%d page_type=%d heap_type=%d heap_ref=%d",
				heap_index, page_index,
				p ? p->nr_vars : -1,
				p ? p->index : -1,
				(unsigned long)instr_ptr,
				fno, fno >= 0 ? ain->functions[fno].name : "?",
				fno >= 0 ? ain->functions[fno].nr_vars : -1,
				stack_ptr,
				p ? p->type : -1,
				heap[heap_index].type, heap[heap_index].ref);
			if (oob_trace == 0)
				dump_trace_buf();
			oob_trace++;
		}
		dummy_var[0].i = 0;
		return dummy_var;
	}
	return &heap[heap_index].page->values[page_index];
}

union vm_value *stack_peek_var(void)
{
	int32_t page_index = stack_peek(0).i;
	int32_t heap_index = stack_peek(1).i;
	if (unlikely(!heap_index_valid(heap_index) || heap[heap_index].type != VM_PAGE)) {
		dummy_var[0].i = 0;
		return dummy_var;
	}
	if (unlikely(!heap[heap_index].page || page_index < 0 || page_index >= heap[heap_index].page->nr_vars)) {
		dummy_var[0].i = 0;
		return dummy_var;
	}
	return &heap[heap_index].page->values[page_index];
}

static void stack_push_string(struct string *s)
{
	int32_t heap_slot = heap_alloc_slot(VM_STRING);
	heap[heap_slot].s = s;
	heap[heap_slot].ref |= HEAP_TEMP_FLAG;  // mark as stack-owned temporary
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
	int slot = heap_alloc_slot(VM_STRING);
	heap[slot].s = string_ref(s);
	return slot;
}

int vm_copy_page(struct page *page)
{
	int slot = heap_alloc_slot(VM_PAGE);
	struct page *dst = copy_page(page);
	if (!dst && page) {
		// copy_page returned NULL (circular ref or corrupted) — create empty placeholder
		dst = alloc_page(page->type, page->index, page->nr_vars);
	}
	heap_set_page(slot, dst);
	return slot;
}

union vm_value vm_copy(union vm_value v, enum ain_data_type type)
{
	switch (type) {
	case AIN_STRING:
		return (union vm_value) { .i = vm_string_ref(heap_get_string(v.i)) };
	case AIN_STRUCT:
	case AIN_DELEGATE:
	case AIN_ARRAY_TYPE:
	case AIN_ARRAY:
		return (union vm_value) { .i = vm_copy_page(heap_get_page(v.i)) };
	case AIN_WRAP:
	case AIN_OPTION:
	case AIN_IFACE_WRAP:
	case AIN_IFACE:
		// v14 types: deep copy the heap page (wrap/option/iface_wrap are 1-element pages)
		if (v.i <= 0)
			return v;
		return (union vm_value) { .i = vm_copy_page(heap_get_page(v.i)) };
	case AIN_REF_TYPE:
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
static int _function_call(int fno, int return_address)
{
	if (unlikely(fno < 0 || fno >= ain->nr_functions)) {
		WARNING("_function_call: invalid fno=%d (nr_functions=%d)", fno, ain->nr_functions);
		return -1;
	}
	if (unlikely(call_stack_ptr >= 4090)) {
		static int cso_warn = 0;
		if (cso_warn++ < 5)
			WARNING("_function_call: call stack near overflow (csp=%d) fno=%d '%s' [%d]",
				call_stack_ptr, fno, ain->functions[fno].name, cso_warn);
		return -1;
	}
	struct ain_function *f = &ain->functions[fno];

	// Reject functions with invalid addresses (sentinel/unimplemented functions)
	if (unlikely(f->address >= ain->code_size)) {
		static int bad_addr = 0;
		if (bad_addr++ < 10)
			WARNING("_function_call: invalid address 0x%lX for fno=%d '%s' (code_size=0x%lX)",
				(unsigned long)f->address, fno, f->name, (unsigned long)ain->code_size);
		return -1;
	}

	// Validate nr_vars
	if (unlikely(f->nr_vars < 0 || f->nr_vars > 100000)) {
		WARNING("_function_call: suspicious nr_vars=%d for fno=%d '%s'",
			f->nr_vars, fno, f->name);
		return -1;
	}

	int slot = heap_alloc_slot(VM_PAGE);
	heap_set_page(slot, alloc_page(LOCAL_PAGE, fno, f->nr_vars));
	heap[slot].page->local.struct_ptr = -1;

	call_stack[call_stack_ptr++] = (struct function_call) {
		.fno = fno,
		.call_address = instr_ptr,
		.return_address = return_address,
		.page_slot = slot,
		.struct_page = -1,
		.base_sp = stack_ptr,  // default; callers may override after args
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
		// v14: wrap<T> slot count depends on inner type.
		// Reference wraps (wrap<struct>) = 1 slot (-1 for none).
		// Value wraps (wrap<int>, etc.) = 2 slots [value, has_value].
		// Since we can't distinguish here, return 2 as default for
		// skip/error paths (safer to over-allocate). function_return
		// should NOT enforce this for normal returns.
		return 2;
	default:
		return 1;
	}
}


static void function_call(int fno, int return_address)
{
	// DIAG: track dispatch chain (global[7] event system)
	if (fno == 9178 || fno == 9164 || fno == 9166 || fno == 9167 ||
	    fno == 27282 || fno == 27277 || fno == 9140 || fno == 7855 ||
	    fno == 21031 || fno == 20992 || fno == 9196) {
		static int dc_log = 0;
		dc_log++;
		if (dc_log <= 50 || dc_log % 10000 == 0) {
			int caller_fno = call_stack_ptr > 0 ? call_stack[call_stack_ptr-1].fno : -1;
			WARNING("DISPATCH[%d]: func#%d '%s' (caller=%d '%s')", dc_log, fno,
				ain->functions[fno].name, caller_fno,
				(caller_fno >= 0 && caller_fno < ain->nr_functions) ? ain->functions[caller_fno].name : "?");
		}
	}
	// DIAG: track SceneLogo coroutine functions
	if ((fno >= 31790 && fno <= 31810) || fno == 36738 || fno == 36733 || fno == 36115 || fno == 36116 || fno == 36117 || fno == 27194 || fno == 27192 || fno == 27268 || fno == 27201 || fno == 20862 || fno == 8398 || fno == 27275 || fno == 26604 || fno == 26607
	    || fno == 27269 || fno == 27270 || fno == 27273 || fno == 27274 || fno == 27276 || fno == 36117 || fno == 36118 || fno == 36119 || fno == 37656) {
		int caller_fno = call_stack_ptr > 0 ? call_stack[call_stack_ptr-1].fno : -1;
		WARNING("DIAG: func#%d '%s' ENTERED ip=0x%lX addr=0x%X (caller=%d '%s')", fno,
			ain->functions[fno].name, (unsigned long)instr_ptr,
			ain->functions[fno].address, caller_fno,
			(caller_fno >= 0 && caller_fno < ain->nr_functions) ? ain->functions[caller_fno].name : "?");
		// For Join/RegisterEvent, also print instruction pointer context
		if (fno == 27192 || fno == 27194) {
			WARNING("  ip=0x%lX return_addr=0x%lX call_depth=%d",
				(unsigned long)instr_ptr,
				(unsigned long)(call_stack_ptr > 0 ? call_stack[call_stack_ptr-1].return_address : 0),
				call_stack_ptr);
			// Print call stack
			for (int ci = call_stack_ptr - 1; ci >= 0 && ci >= call_stack_ptr - 5; ci--) {
				WARNING("  callstack[%d]: fno=%d '%s' ret=0x%lX is_method=%d",
					ci, call_stack[ci].fno,
					(call_stack[ci].fno >= 0 && call_stack[ci].fno < ain->nr_functions) ?
						ain->functions[call_stack[ci].fno].name : "?",
					(unsigned long)call_stack[ci].return_address,
					call_stack[ci].is_method);
			}
			// The struct page is on the stack (below the args)
			// In method_call, struct page is at stack_ptr-1 after args are popped
			// But at this point args haven't been popped yet. Let's check the top of stack.
			for (int si = 0; si < 5 && si < stack_ptr; si++) {
				int sv = stack[stack_ptr - 1 - si].i;
				if (sv > 0 && heap_index_valid(sv) && heap[sv].type == VM_PAGE
				    && heap[sv].page && heap[sv].page->type == STRUCT_PAGE) {
					int sidx = heap[sv].page->index;
					const char *sn = (sidx >= 0 && sidx < ain->nr_structures) ?
						ain->structures[sidx].name : "?";
					WARNING("  stack[-%d]=%d -> STRUCT_PAGE idx=%d '%s' nr_vm=%d",
						si, sv, sidx, sn,
						(sidx >= 0 && sidx < ain->nr_structures) ? ain->structures[sidx].nr_vmethods : -1);
				}
			}
		}
	}
	int slot = _function_call(fno, return_address);
	if (unlikely(slot < 0)) return;
	// DIAG: verify function address after _function_call
	if (fno == 31792 || fno == 27192 || fno == 27194 || fno == 36115 || fno == 27268 || fno == 36733 || fno == 27201 || fno == 27178 || fno == 27268) {
		// Also dump JUMP target for trampoline functions
		if (fno == 27194 || fno == 27178) {
			uint16_t fop = (uint16_t)ain->code[instr_ptr] | ((uint16_t)ain->code[instr_ptr+1] << 8);
			if (fop == 0x002C) { // JUMP
				int32_t jt = *(int32_t*)(ain->code + instr_ptr + 2);
				WARNING("  func#%d is JUMP -> 0x%lX", fno, (unsigned long)(uint32_t)jt);
				// Check first few ops at target
				size_t tip = (uint32_t)jt;
				for (int di = 0; di < 10 && tip < ain->code_size - 6; di++) {
					uint16_t dop = (uint16_t)ain->code[tip] | ((uint16_t)ain->code[tip+1] << 8);
					int32_t darg = *(int32_t*)(ain->code + tip + 2);
					WARNING("    [%d] 0x%lX: op=0x%04X arg=%d (0x%X)", di, (unsigned long)tip, dop, darg, (uint32_t)darg);
					int nargs = 0;
					if (dop == 0x0000 || dop == 0x002C || dop == 0x002D || dop == 0x002E ||
					    dop == 0x0030 || dop == 0x005C || dop == 0x0061 || dop == 0x004F ||
					    dop == 0x010C || dop == 0x010D || dop == 0x010A || dop == 0x010E || dop == 0x0040 || dop == 0x0041) nargs = 1;
					else if (dop == 0x0076 || dop == 0x010B || dop == 0x00F4) nargs = 2;
					else if (dop == 0x005A) nargs = 3;
					tip += 2 + nargs * 4;
					if (dop == 0x002F) break; // RETURN
				}
			}
		}
		uint16_t first_op = (uint16_t)ain->code[instr_ptr] | ((uint16_t)ain->code[instr_ptr+1] << 8);
		WARNING("DIAG POST-CALL: func#%d ip=0x%lX first_opcode=0x%04X code_size=%zu",
			fno, (unsigned long)instr_ptr, first_op, ain->code_size);
		if (first_op == 0x002C) { // JUMP
			int32_t target = *(int32_t*)(ain->code + instr_ptr + 2);
			WARNING("  JUMP target=0x%lX", (unsigned long)(uint32_t)target);
			// Dump 20 opcodes at target
			size_t tip = (uint32_t)target;
			for (int di = 0; di < 20 && tip < ain->code_size - 6; di++) {
				uint16_t dop = (uint16_t)ain->code[tip] | ((uint16_t)ain->code[tip+1] << 8);
				int32_t darg = *(int32_t*)(ain->code + tip + 2);
				WARNING("  [%d] 0x%lX: op=0x%04X arg=%d", di, (unsigned long)tip, dop, darg);
				// advance by instruction width (simplified)
				int nargs = 0;
				if (dop == 0x0000 || dop == 0x002C || dop == 0x002D || dop == 0x002E ||
				    dop == 0x0030 || dop == 0x005C || dop == 0x0061 || dop == 0x004F) nargs = 1;
				else if (dop == 0x0076) nargs = 2;
				else if (dop == 0x005A) nargs = 3;
				else if (dop == 0x010C || dop == 0x010D || dop == 0x010A || dop == 0x010E ||
				         dop == 0x0106) nargs = 1;
				else if (dop == 0x010B) nargs = 2;
				tip += 2 + nargs * 4;
				if (dop == 0x002F) break; // RETURN
			}
		}
	}

	// pop arguments, store in local page
	struct ain_function *f = &ain->functions[fno];

	for (int i = f->nr_args - 1; i >= 0; i--) {
		heap[slot].page->values[i] = stack_pop();
		switch (f->vars[i].type.data) {
		case AIN_REF_TYPE:
		case AIN_STRUCT:
		case AIN_DELEGATE:
		case AIN_ARRAY_TYPE:
		case AIN_ARRAY:
		case AIN_WRAP:
			if (heap[slot].page->values[i].i != -1)
				heap_ref(heap[slot].page->values[i].i);
			break;
		case AIN_STRING: {
			// v14 leak fix: use "temp" flag to distinguish owned vs shared slots.
			// Temp slots (from stack_push_string) don't need heap_ref — ownership
			// transfers from stack to local page. Shared slots (from X_REF) need
			// heap_ref because the source page also references them.
			int vi = heap[slot].page->values[i].i;
			if (vi > 0 && (size_t)vi < heap_size) {
				if (heap[vi].ref & HEAP_TEMP_FLAG) {
					heap[vi].ref &= ~HEAP_TEMP_FLAG;
				} else {
					heap_ref(vi);
				}
			}
			break;
		}
		default:
			break;
		}
	}
	// Record base_sp for stack balance check
	call_stack[call_stack_ptr-1].base_sp = stack_ptr;
}

static void function_return(void);

static void method_call(int fno, int return_address)
{
	function_call(fno, return_address);

	if (ain->version >= 14) {
		// v14: struct_page stays on stack below base_sp.
		int struct_page = stack[stack_ptr - 1].i;

		// Force-intercept CASTimerManager methods with native implementation.
		// The Manager's struct members are corrupted even when page type is
		// valid, so we must intercept BEFORE the script code runs.
		// (CASTimer instance methods with valid struct_pages should NOT be
		// intercepted — those are normal timer objects embedded in other structs.)
		if (strstr(ain->functions[fno].name, "CASTimerManager")) {
			union vm_value native_ret = {.i = 0};
			native_cas_timer_intercept(fno, struct_page, &native_ret);
			stack_pop(); // remove struct_page
			call_stack[call_stack_ptr-1].base_sp = stack_ptr;
			call_stack[call_stack_ptr-1].is_method = false;
			int ret_slots = ain_return_slots_type(&ain->functions[fno].return_type);
			for (int i = 0; i < ret_slots; i++) {
				if (i == 0) stack_push(native_ret);
				else stack_push((union vm_value){.i = 0});
			}
			function_return();
			return;
		}

		bool valid = struct_page > 0 && heap_index_valid(struct_page)
			&& heap[struct_page].type == VM_PAGE  // Must be a page, not a string
			&& heap[struct_page].page && heap[struct_page].page->type == STRUCT_PAGE;

		// v14 auto-allocate: when struct_page=0 (null object), allocate a
		// new struct page so the method can execute.  This handles cases
		// where array elements or pool objects weren't properly initialized
		// (e.g. CPartsFunctionSet from CPartsMessageManager pool).
		if (!valid && struct_page <= 0) {
			// Parse struct name from "StructName@MethodName" in function name
			int stype = -1;
			const char *fname = ain->functions[fno].name;
			const char *at = fname ? strchr(fname, '@') : NULL;
			if (at && at > fname) {
				size_t len = at - fname;
				// Check for namespace prefix (e.g. "parts::detail::CPartsFunctionSet")
				// Use the full prefix before @
				char buf[256];
				if (len < sizeof(buf)) {
					memcpy(buf, fname, len);
					buf[len] = '\0';
					stype = ain_get_struct(ain, buf);
				}
			}
			if (stype >= 0 && stype < ain->nr_structures) {
				int new_slot = alloc_struct(stype);
				heap_ref(new_slot);
				stack[stack_ptr - 1].i = new_slot;
				struct_page = new_slot;
				valid = true;
				{ static int aa_log = 0; if (aa_log++ < 30)
					WARNING("method_call: auto-alloc struct[%d] '%s' slot=%d for fno=%d '%s'",
						stype, ain->structures[stype].name, new_slot,
						fno, ain->functions[fno].name);
				}
			}
		}

		if (unlikely(!valid)) {
			// Invalid struct_page: pop it, push default return, bail out.
			stack_pop(); // remove struct_page
			call_stack[call_stack_ptr-1].base_sp = stack_ptr;
			call_stack[call_stack_ptr-1].is_method = false; // popped already

			// Try native CASTimer interception for broken instances
			union vm_value native_ret = {.i = 0};
			bool intercepted = native_cas_timer_intercept(fno, struct_page, &native_ret);

			mc_skip_total++;
			if (!intercepted && (mc_skip_total <= 50 || (mc_skip_total % 100000 == 0))) {
				int caller_fno = (call_stack_ptr >= 2) ? call_stack[call_stack_ptr-2].fno : -1;
				const char *caller_name = (caller_fno >= 0 && caller_fno < ain->nr_functions) ? ain->functions[caller_fno].name : "?";
				const char *reason = "unknown";
				if (struct_page <= 0) reason = "slot<=0";
				else if ((size_t)struct_page >= heap_size) reason = "slot>=heap_size";
				else if (heap[struct_page].ref <= 0) reason = "ref<=0";
				else if (heap[struct_page].type != VM_PAGE) reason = "not VM_PAGE";
				else if (!heap[struct_page].page) reason = "page=NULL";
				else if (heap[struct_page].page->type != STRUCT_PAGE) {
					static char pgbuf[128];
					snprintf(pgbuf, sizeof(pgbuf), "page_type=%d(expect %d) idx=%d nv=%d",
						heap[struct_page].page->type, STRUCT_PAGE,
						heap[struct_page].page->index, heap[struct_page].page->nr_vars);
					reason = pgbuf;
				}
				WARNING("method_call: skip[%d] fno=%d '%s' struct_page=%d caller=%d '%s' reason=%s",
					mc_skip_total, fno, ain->functions[fno].name, struct_page,
					caller_fno, caller_name, reason);
			}

			int ret_slots = ain_return_slots_type(&ain->functions[fno].return_type);
			for (int i = 0; i < ret_slots; i++) {
				if (i == 0 && intercepted)
					stack_push(native_ret);
				else if (i == 0 && ain->functions[fno].return_type.data == AIN_STRING)
					stack_push(vm_string_ref(string_ref(&EMPTY_STRING)));
				else
					stack_push((union vm_value){.i = 0});
			}
			function_return();
			return;
		}
		call_stack[call_stack_ptr-1].struct_page = struct_page;
		call_stack[call_stack_ptr-1].is_method = true;
		// base_sp stays as function_call set it (above struct_page)

		// v14: constructors always use separate pages.
		// PUSHLOCALPAGE → local page (args + temps)
		// PUSHSTRUCTPAGE → struct page (members)
		// Do NOT replace page_slot with struct_page. The compiler uses
		// PUSHLOCALPAGE for temporary variables (local[0..N]) which are
		// independent from struct members. Replacing page_slot would cause
		// local variable cleanup (e.g. local[1] = -1) to overwrite struct
		// members (e.g. m_context), resulting in -1 values.
		heap[call_stack[call_stack_ptr-1].page_slot].page->local.struct_ptr = struct_page;
		return;
	}

	// Pre-v14: pop struct_page and update base_sp
	int struct_page = stack_pop().i;
	bool valid = struct_page > 0 && heap_index_valid(struct_page)
		&& heap[struct_page].page && heap[struct_page].page->type == STRUCT_PAGE;
	if (unlikely(!valid)) {
		static int mc_skip = 0;
		if (mc_skip++ < 20) {
			WARNING("method_call: skip fno=%d '%s' struct_page=%d sp=%d",
				fno, ain->functions[fno].name, struct_page, stack_ptr);
		}
		call_stack[call_stack_ptr-1].base_sp = stack_ptr;
		int ret_slots = ain_return_slots_type(&ain->functions[fno].return_type);
		for (int i = 0; i < ret_slots; i++)
			stack_push((union vm_value){.i = 0});
		function_return();
		return;
	}
	call_stack[call_stack_ptr-1].struct_page = struct_page;
	call_stack[call_stack_ptr-1].base_sp = stack_ptr;
	heap[call_stack[call_stack_ptr-1].page_slot].page->local.struct_ptr = struct_page;
}

static void vm_execute(void);

static void delegate_call(int dg_no, int return_address)
{
	if (dg_no < 0 || dg_no >= ain->nr_delegates)
		VM_ERROR("Invalid delegate index");

	// stack: [arg0, ..., dg_page, dg_index, [return_value]]
	int return_values = (ain->delegates[dg_no].return_type.data != AIN_VOID) ? 1 : 0;
	int dg_page = stack_peek(1 + return_values).i;
	int dg_index = stack_peek(0 + return_values).i;
	int obj, fun;
	struct page *dg_pg = heap_get_delegate_page(dg_page);

	if (delegate_get(dg_pg, dg_index, &obj, &fun)) {
		// DIAG: log delegate dispatch
		{
			static int dg_disp_log = 0;
			dg_disp_log++;
			if (dg_disp_log <= 100 || dg_disp_log % 10000 == 0) {
				int caller = (call_stack_ptr > 0) ? call_stack[call_stack_ptr-1].fno : -1;
				WARNING("DG_DISPATCH[%d]: dg#%d idx=%d -> obj=%d fun=%d '%s' (caller=%d '%s')",
					dg_disp_log, dg_no, dg_index, obj, fun,
					(fun >= 0 && fun < ain->nr_functions) ? ain->functions[fun].name : "?",
					caller, (caller >= 0 && caller < ain->nr_functions) ? ain->functions[caller].name : "?");
			}
			// DIAG: for func#36117, dump struct fields to understand the condition
			if (fun == 36117 && obj >= 0 && heap_index_valid(obj) && heap[obj].type == VM_PAGE && heap[obj].page) {
				static int f36117_log = 0;
				f36117_log++;
				if (f36117_log <= 20 || f36117_log % 5000 == 0) {
					struct page *sp = heap[obj].page;
					int f7 = (sp->nr_vars > 7) ? sp->values[7].i : -999;
					int f9 = (sp->nr_vars > 9) ? sp->values[9].i : -999;
					int f6_slot = (sp->nr_vars > 6) ? sp->values[6].i : -999;
					WARNING("  36117-DIAG[%d]: obj=%d struct_idx=%d nr_vars=%d field[7]=%d field[9]=%d field[6]=%d",
						f36117_log, obj, sp->index, sp->nr_vars,
						f7, f9, f6_slot);
				}
			}
		}
		// Guard: skip invalid function numbers
		if (fun < 0 || fun >= ain->nr_functions) {
			static int dc_warn = 0;
			dc_warn++;
			if (dc_warn <= 5 || dc_warn % 500000 == 0) {
				int caller = (call_stack_ptr > 0) ? call_stack[call_stack_ptr-1].fno : -1;
				const char *dg_name = (dg_no >= 0 && dg_no < ain->nr_delegates) ? ain->delegates[dg_no].name : "?";
				WARNING("delegate_call[%d]: invalid fun=%d obj=%d dg=%d '%s' dg_page=%d dg_idx=%d caller=%d '%s'",
					dc_warn, fun, obj, dg_no, dg_name, dg_page, dg_index,
					caller, (caller >= 0 && caller < ain->nr_functions) ? ain->functions[caller].name : "?");
			}
			// Pop return value first (like the success path does) so
			// we increment dg_index, not the return value slot.
			if (return_values)
				stack_pop();
			// increment dg_index to advance past this entry
			stack[stack_ptr - 1].i++;
			// Push back dummy return value to keep stack balanced
			if (return_values)
				stack_push(0);
			return;
		}
		// pop previous return value
		if (ain->delegates[dg_no].return_type.data != AIN_VOID) {
			stack_pop();
		}
		// increment dg_index
		stack[stack_ptr - 1].i++;

		int slot = _function_call(fun, instr_ptr + instruction_width(DG_CALL));
		if (unlikely(slot < 0)) return;
		// Set base_sp so function_return won't destroy delegate stack state
		call_stack[call_stack_ptr-1].base_sp = stack_ptr;
		call_stack[call_stack_ptr-1].is_delegate_call = true;

		// copy arguments into local page
		struct ain_function_type *dg = &ain->delegates[dg_no];
		if (!heap[slot].page || heap[slot].page->nr_vars < dg->nr_arguments) {
			static int dc_page_warn = 0;
			if (dc_page_warn++ < 5)
				WARNING("delegate_call: slot=%d page=%p nr_vars=%d dg_args=%d fun=%d obj=%d",
					slot, (void*)(heap[slot].page),
					heap[slot].page ? heap[slot].page->nr_vars : -1,
					dg->nr_arguments, fun, obj);
		}
		if (heap[slot].page) {
			for (int i = 0; i < dg->nr_arguments && i < heap[slot].page->nr_vars; i++) {
				union vm_value arg = stack_peek((dg->nr_arguments + 1) - i);
				heap[slot].page->values[i] = vm_copy(arg, dg->variables[i].type.data);
			}
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
	vmcall_count++;
	size_t saved_ip = instr_ptr;
	unsigned long long saved_limit = vm_call_insn_limit;
	// Disable vm_call timeout — Dohna Dohna's constructors legitimately
	// need 100M+ instructions for activity tree loading.
	vm_call_insn_limit = 0;  // disabled
	if (struct_page < 0) {
		function_call(fno, VM_RETURN);
	} else {
		stack_push(struct_page);
		method_call(fno, VM_RETURN);
	}
	vm_execute();
	vm_call_insn_limit = saved_limit;
	instr_ptr = saved_ip;
}

unsigned long long vm_call_get_insn_count(void)
{
	return insn_count;
}

// Call a function with args already on the stack, WITHOUT popping them.
// The args are copied into the local page but remain on the stack for
// the bytecode to consume directly (used by v14 HLL callback calling convention).
void vm_call_nopop(int fno, int nargs)
{
	if (unlikely(fno < 0 || fno >= ain->nr_functions)) return;
	size_t saved_ip = instr_ptr;
	struct ain_function *f = &ain->functions[fno];
	int slot = _function_call(fno, VM_RETURN);
	if (unlikely(slot < 0)) { instr_ptr = saved_ip; return; }
	call_stack[call_stack_ptr-1].base_sp = stack_ptr;

	// Set struct_page for lambda/method callbacks invoked from HLL functions.
	// hll_func_obj is set by FFI when processing AIN_HLL_FUNC arguments.
	extern int hll_func_obj;
	if (hll_func_obj >= 0) {
		call_stack[call_stack_ptr-1].struct_page = hll_func_obj;
	}

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
	int page_slot = call_stack[call_stack_ptr-1].page_slot;
	size_t ret_addr = call_stack[call_stack_ptr-1].return_address;
	int fno = call_stack[call_stack_ptr-1].fno;
	int base_sp = call_stack[call_stack_ptr-1].base_sp;

	// Determine expected return slots based on function's declared return type.
	// v14 2-valued types (AIN_IFACE, AIN_OPTION) push 2 slots on return.
	int expected_return = 0;
	if (fno >= 0 && fno < ain->nr_functions)
		expected_return = ain_return_slots_type(&ain->functions[fno].return_type);


	// Enforce stack balance: ensure exactly expected_return values above base_sp.
	bool is_dg = call_stack[call_stack_ptr-1].is_delegate_call;
	int delta = stack_ptr - base_sp;

	// For delegate-called functions: enforce stack balance too.
	// The delegate framework expects exactly expected_return values above base_sp.
	if (is_dg && delta != expected_return && delta >= 0) {
		static int dg_fix = 0;
		if (dg_fix++ < 30) {
			WARNING("delegate function_return: fixing delta=%d expected=%d fno=%d '%s' base_sp=%d sp=%d",
				delta, expected_return, fno,
				(fno >= 0 && fno < ain->nr_functions) ? ain->functions[fno].name : "?",
				base_sp, stack_ptr);
		}
		if (delta > expected_return) {
			// Too many values: keep only the return value(s) at the top
			union vm_value retvals[2] = {{0}, {0}};
			for (int i = 0; i < expected_return && i < 2; i++)
				retvals[i] = stack[stack_ptr - expected_return + i];
			stack_ptr = base_sp + expected_return;
			for (int i = 0; i < expected_return && i < 2; i++)
				stack[base_sp + i] = retvals[i];
		} else {
			// Too few values: push defaults
			enum ain_data_type rtype = (fno >= 0 && fno < ain->nr_functions)
				? ain->functions[fno].return_type.data : AIN_INT;
			while (stack_ptr < base_sp + expected_return) {
				if (rtype == AIN_STRING)
					stack_push(vm_string_ref(string_ref(&EMPTY_STRING)));
				else
					stack_push((union vm_value){.i = 0});
			}
		}
		delta = expected_return; // updated for downstream checks
	}

	// Determine return type for enforcement decisions.
	enum ain_data_type ret_data = (fno >= 0 && fno < ain->nr_functions)
		? ain->functions[fno].return_type.data : AIN_INT;

	// AIN_WRAP: slot count varies by inner type (reference=1, value=2).
	// We cannot reliably determine which from the return type alone.
	// Trust the bytecode — skip truncation/padding for normal WRAP returns.
	bool skip_enforce = (ret_data == AIN_WRAP);

	if (!is_dg && !skip_enforce && delta > expected_return) {
		// Too many values: save return value(s) and pop extras.
		static int trunc_ret = 0;
		if (trunc_ret++ < 20) {
			WARNING("function_return: truncating delta=%d to expected=%d fno=%d '%s' ret_type=%d",
				delta, expected_return, fno,
				(fno >= 0 && fno < ain->nr_functions) ? ain->functions[fno].name : "?",
				ret_data);
		}
		union vm_value retvals[2] = {{0}, {0}};
		for (int i = 0; i < expected_return && i < 2; i++)
			retvals[i] = stack[stack_ptr - expected_return + i];
		stack_ptr = base_sp + expected_return;
		for (int i = 0; i < expected_return && i < 2; i++)
			stack[base_sp + i] = retvals[i];
	} else if (!is_dg && !skip_enforce && delta < expected_return) {
		// Too few values: push default return values to fill the gap.
		// CRITICAL: if stack_ptr < base_sp (underflow), jump to base_sp first.
		// Values below base_sp belong to the caller and must NOT be overwritten.
		// Without this, padding zeros corrupt the caller's stack frame.
		static int short_ret = 0;
		if (short_ret++ < 20) {
			WARNING("function_return: stack short by %d (delta=%d expected=%d) fno=%d '%s' base_sp=%d sp=%d",
				expected_return - delta, delta, expected_return, fno,
				(fno >= 0 && fno < ain->nr_functions) ? ain->functions[fno].name : "?",
				base_sp, stack_ptr);
		}
		if (stack_ptr < base_sp) {
			stack_ptr = base_sp;  // skip caller's values
		}
		while (stack_ptr < base_sp + expected_return) {
			if (ret_data == AIN_STRING)
				stack_push(vm_string_ref(string_ref(&EMPTY_STRING)));
			else
				stack_push((union vm_value){.i = 0});
		}
	}

	// v14 method return: handle the struct_page phantom slot below base_sp.
	// The callee's struct_page was left on the stack (not popped by method_call).
	//
	// ALWAYS consume struct_page: shift return values (if any) down over the
	// struct_page slot and decrement stack_ptr. The calling bytecode does NOT
	// expect struct_page to remain — void methods like setters are followed
	// by DELETE which cleans up a different value (e.g., a duplicated string).
	// Leaving struct_page on stack causes DELETE to unref it incorrectly.
	//
	// Constructors called via NEW are handled separately by the NEW handler
	// which explicitly sets the stack after the constructor returns.
	if (call_stack[call_stack_ptr-1].is_method) {
		int actual_count = stack_ptr - base_sp;
		if (actual_count < 0) actual_count = 0;
		if (actual_count > 0) {
			// Shift return values down to consume struct_page
			for (int i = 0; i < actual_count; i++) {
				stack[base_sp - 1 + i] = stack[base_sp + i];
			}
		}
		stack_ptr--;  // always consume struct_page
	}

	// Decrement call_stack_ptr BEFORE heap_unref so that LIVE_PAGE_FREE
	// protection can check all active frames (including what was the top).
	// The current frame's page_slot is no longer "active" after return.
	call_stack_ptr--;
	heap_unref(page_slot);
	instr_ptr = ret_addr;
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
		int exit_code = stack_pop().i;
		WARNING("SYS_EXIT(%d): caller fno=%d '%s'",
			exit_code,
			(call_stack_ptr > 0) ? call_stack[call_stack_ptr-1].fno : -1,
			(call_stack_ptr > 0 && call_stack[call_stack_ptr-1].fno >= 0
			 && call_stack[call_stack_ptr-1].fno < ain->nr_functions
			 && ain->functions[call_stack[call_stack_ptr-1].fno].name)
			? ain->functions[call_stack[call_stack_ptr-1].fno].name : "?");
		vm_exit(exit_code);
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
		gfx_swap();
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
		int sp_slot = struct_page_slot();
		stack_push((union vm_value){.i = sp_slot});
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
		int new_sp_before = stack_ptr;
		if (ain->version >= 11) {
			int struct_type = get_argument(0);
			int ctor_func = get_argument(1);
			new_count++;
			create_struct(struct_type, &v);
			{
				int strt_ctor = (struct_type >= 0 && struct_type < ain->nr_structures)
					? ain->structures[struct_type].constructor : -99;
				// If bytecode has no explicit ctor, use STRT-defined one.
				// v14 encodes -1 for "no ctor", older versions use 0.
				// But NOT during alloc phase.
				if (ctor_func <= 0 && strt_ctor > 0 && !vm_in_alloc_phase) {
					ctor_func = strt_ctor;
				}
			}
			if (ctor_func > 0 && ain->version >= 14) {
				// v14: constructor args are already on the stack
				// method_call expects: [struct_page, arg1, ..., argN]
				// Need to insert struct_page below the args
				int nr_ctor_args = ain->functions[ctor_func].nr_args;
				// Save constructor args from stack
				union vm_value saved[64];
				for (int i = nr_ctor_args - 1; i >= 0; i--) {
					saved[i] = stack_pop();
				}
				// Push struct_page (this) below args
				stack_push(v);
				// Re-push args on top
				for (int i = 0; i < nr_ctor_args; i++) {
					stack_push(saved[i]);
				}
				// Ref-protect struct page during constructor.
				// Some constructors (e.g. SceneContext@0) have DELETE
				// instructions that unref the struct page via local
				// variable cleanup. The extra ref prevents the page
				// from being freed during construction.
				heap_ref(v.i);
				// Call constructor as method
				size_t saved_ip = instr_ptr;
				method_call(ctor_func, VM_RETURN);
				vm_execute();
				instr_ptr = saved_ip;
				// Remove protection ref if constructor didn't consume it.
				// If constructor unref'd once: ref went 2→1, keep it.
				// If constructor didn't unref: ref stayed 2, remove extra.
				if (v.i > 0 && (size_t)v.i < heap_size && heap[v.i].ref > 1) {
					heap_unref(v.i);
				}
				// v14 NEW stack fix: function_return's is_method handling
				// may or may not consume struct_page depending on the
				// constructor's raw_delta. Instead of relying on that
				// implicit behavior, explicitly set the stack to the
				// correct state: NEW consumed nr_ctor_args user args
				// and produced 1 result (struct_page).
				{
					int expected_sp = new_sp_before - nr_ctor_args + 1;
					stack_ptr = expected_sp;
					stack[stack_ptr - 1] = v;
				}
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
		int _fno = get_argument(0);
		// --skip-title: bypass SceneLogo and SceneTitle
		if (config.skip_title && _fno >= 0 && _fno < ain->nr_functions
				&& ain->functions[_fno].name) {
			if (strstr(ain->functions[_fno].name, "RunResult<SceneTitle")) {
				WARNING("--skip-title: skipping %s, returning 0 (NewGame)", ain->functions[_fno].name);
				stack_push((union vm_value){.i = 0});
				instr_ptr += instruction_width(CALLFUNC);
				break;
			}
			if (strstr(ain->functions[_fno].name, "Run<SceneLogo>")) {
				WARNING("--skip-title: skipping %s", ain->functions[_fno].name);
				instr_ptr += instruction_width(CALLFUNC);
				break;
			}
		}
		if (_fno >= 0 && _fno < ain->nr_functions
				&& (ain->functions[_fno].address == 0xFFFFFFFF
				    || ain->functions[_fno].address >= ain->code_size)) {
			function_call(_fno, instr_ptr + instruction_width(CALLFUNC));
			function_return();
		} else {
			function_call(_fno, instr_ptr + instruction_width(CALLFUNC));
		}
		break;
	}
	case CALLFUNC2: {
		stack_pop(); // function-type index (only needed for compilation)
		int _fno2 = stack_pop().i;
		if (_fno2 >= 0 && _fno2 < ain->nr_functions
				&& (ain->functions[_fno2].address == 0xFFFFFFFF
				    || ain->functions[_fno2].address >= ain->code_size)) {
			function_call(_fno2, instr_ptr + instruction_width(CALLFUNC2));
			function_return();
		} else {
			function_call(_fno2, instr_ptr + instruction_width(CALLFUNC2));
		}
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
			// DIAG: virtual dispatch investigation
			{
				int cm_caller = call_stack_ptr > 0 ? call_stack[call_stack_ptr-1].fno : -1;
				if (cm_caller == 27194 || cm_caller == 27192) {
					static int cm_in_re = 0;
					cm_in_re++;
					if (cm_in_re <= 30) {
						WARNING("CALLMETHOD inside func#%d: funcno=%d '%s' nargs=%d ip=0x%lX",
							cm_caller, funcno,
							(funcno >= 0 && funcno < ain->nr_functions) ? ain->functions[funcno].name : "?",
							nargs, (unsigned long)instr_ptr);
					}
				}
			}
			if (funcno == 27194 || funcno == 27178 || funcno == 27201) {
				int sp_idx = stack_peek(0).i;
				int stype = -1;
				const char *sname = "?";
				int nr_vm = 0;
				if (sp_idx > 0 && heap_index_valid(sp_idx) && heap[sp_idx].type == VM_PAGE
				    && heap[sp_idx].page && heap[sp_idx].page->type == STRUCT_PAGE) {
					stype = heap[sp_idx].page->index;
					if (stype >= 0 && stype < ain->nr_structures)
						sname = ain->structures[stype].name;
					nr_vm = (stype >= 0 && stype < ain->nr_structures) ? ain->structures[stype].nr_vmethods : 0;
				}
				int func_stype = ain->functions[funcno].struct_type;
				const char *func_sname = (func_stype >= 0 && func_stype < ain->nr_structures) ?
					ain->structures[func_stype].name : "(none)";
				WARNING("CALLMETHOD DIAG: funcno=%d '%s' nargs=%d | struct_page=%d actual_type=%d '%s' nr_vm=%d | func_struct=%d '%s'",
					funcno, ain->functions[funcno].name, nargs,
					sp_idx, stype, sname, nr_vm, func_stype, func_sname);
				// Check if vmethods exist and if funcno is in the base struct vtable
				if (func_stype >= 0 && func_stype < ain->nr_structures) {
					struct ain_struct *base_s = &ain->structures[func_stype];
					for (int vi = 0; vi < base_s->nr_vmethods && vi < 30; vi++) {
						if (base_s->vmethods[vi] == funcno) {
							WARNING("  funcno=%d found at base vtable[%d]", funcno, vi);
							// Check if actual struct has different entry
							if (stype >= 0 && stype < ain->nr_structures && stype != func_stype) {
								struct ain_struct *act_s = &ain->structures[stype];
								if (vi < act_s->nr_vmethods) {
									int resolved = act_s->vmethods[vi];
									const char *rname = (resolved >= 0 && resolved < ain->nr_functions) ?
										ain->functions[resolved].name : "?";
									WARNING("  -> OVERRIDE: actual vtable[%d]=%d '%s'", vi, resolved, rname);
								}
							}
							break;
						}
					}
				}
			}
			// Handle nargs vs function's nr_args mismatch
			if (funcno < 0 || funcno >= ain->nr_functions) {
				static int bad_fno = 0;
				if (bad_fno++ < 10) {
					int sp_val = stack_peek(0).i;
					WARNING("CALLMETHOD: invalid funcno=%d nargs=%d ip=0x%lX struct_page=%d",
						funcno, nargs, (unsigned long)instr_ptr, sp_val);
				}
			}
			if (funcno >= 0 && funcno < ain->nr_functions) {
				// --skip-title: intercept SceneLogo and SceneTitle via CALLMETHOD too
				if (config.skip_title && ain->functions[funcno].name) {
					const char *fn = ain->functions[funcno].name;
					if (strstr(fn, "RunResult<SceneTitle")) {
						WARNING("--skip-title: CALLMETHOD skipping %s, returning 0 (NewGame)", fn);
						if (saved_args != small_args) free(saved_args);
						stack_pop(); // struct_page
						instr_ptr += instruction_width(CALLMETHOD);
						stack_push((union vm_value){.i = 0});
						break;
					}
					if (strstr(fn, "Run<SceneLogo>")) {
						WARNING("--skip-title: CALLMETHOD skipping %s", fn);
						if (saved_args != small_args) free(saved_args);
						stack_pop(); // struct_page
						instr_ptr += instruction_width(CALLMETHOD);
						break;
					}
				}
				int fnr_args = ain->functions[funcno].nr_args;
				if (nargs != fnr_args) {
					// nargs mismatch: vtable resolved to a function with a
					// different signature (common with interface dispatch).
					// Push the correct number of dummy return values to keep
					// the caller's stack balanced (e.g., AIN_IFACE/OPTION = 2 slots).
					nargs_skip_total++;
					if (nargs_skip_total <= 10 || (nargs_skip_total % 100000 == 0)) {
						WARNING("CALLMETHOD nargs mismatch[%d]: fno=%d '%s' nargs=%d fnr_args=%d ip=0x%lX",
							nargs_skip_total, funcno, ain->functions[funcno].name,
							nargs, fnr_args, (unsigned long)instr_ptr);
					}
					if (saved_args != small_args) free(saved_args);
					stack_pop(); // struct_page
					instr_ptr += instruction_width(CALLMETHOD);
					int ret_slots = ain_return_slots_type(&ain->functions[funcno].return_type);
					for (int i = 0; i < ret_slots; i++)
						stack_push((union vm_value){.i = (i == 0) ? -1 : 0});
				} else {
					// nargs matches: push args back and call normally.
					for (int i = nargs - 1; i >= 0; i--)
						stack_push(saved_args[i]);
					if (saved_args != small_args) free(saved_args);
					if (ain->functions[funcno].address == 0xFFFFFFFF
				    || ain->functions[funcno].address >= ain->code_size) {
						method_call(funcno, instr_ptr + instruction_width(CALLMETHOD));
						function_return();
					} else {
						method_call(funcno, instr_ptr + instruction_width(CALLMETHOD));
					}
				}
			} else {
				// Invalid funcno: skip call and push dummy return value(s).
				// We don't know the return type, so check nargs as heuristic:
				// the caller's bytecode nargs sometimes correlates with expected
				// return slots for wrap/option operations.
				if (saved_args != small_args) free(saved_args);
				stack_pop(); // struct_page
				instr_ptr += instruction_width(CALLMETHOD);
				stack_push((union vm_value){.i = -1});
			}
		} else {
			// Pre-v14: bytecode arg = function number
			method_call(get_argument(0), instr_ptr + instruction_width(CALLMETHOD));
		}
		break;
	}
	case CALLHLL: {
		int hll_arg2 = (ain->version >= 11) ? get_argument(2) : -1;
		int hll_lib = get_argument(0), hll_fn = get_argument(1);
		hll_call(hll_lib, hll_fn, hll_arg2);
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
			static int assert_count = 0;
			if (assert_count++ < 10) {
				sys_message("Assertion failed at %s:%d: %s\n",
						display_sjis0(heap_get_string(file)->text),
						line,
						display_sjis1(heap_get_string(expr)->text));
				// Trace call stack for debugging
				for (int _cs = call_stack_ptr - 1; _cs >= 0 && _cs >= call_stack_ptr - 8; _cs--) {
					int _fno = call_stack[_cs].fno;
					WARNING("  ASSERT callstack[%d]: fno=%d '%s'", _cs, _fno,
						(_fno >= 0 && _fno < ain->nr_functions && ain->functions[_fno].name)
						? ain->functions[_fno].name : "?");
				}
			}
			// Continue execution instead of vm_exit for v14 debugging
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
		union vm_value *v = stack_pop_var();
		v[0].i++;
		break;
	}
	case DEC: {
		union vm_value *v = stack_pop_var();
		{
			static int dec_dummy_count = 0;
			if (v == dummy_var && dec_dummy_count < 10) {
				int fno = call_stack_ptr > 0 ? call_stack[call_stack_ptr-1].fno : -1;
				WARNING("DEC: writing to dummy_var! ip=0x%lX fno=%d '%s' sp=%d",
					(unsigned long)instr_ptr, fno,
					(fno >= 0 && fno < ain->nr_functions) ? ain->functions[fno].name : "?",
					stack_ptr);
				dec_dummy_count++;
			}
		}
		v[0].i--;
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
			// v14: stack = [heap_idx, var_idx, string_slot]
			// 2-slot var ref + string value, like X_ASSIGN but for strings
			int rval = stack_pop().i;
			union vm_value *var = stack_pop_var();
			if (var) {
				int lval = var->i;
				if (lval > 0 && string_index_valid(lval)) {
					heap_string_assign(lval, heap_get_string(rval));
				} else {
					// Uninitialized member: ref and store the string slot directly
					heap_ref(rval);
					if (lval > 0) heap_unref(lval);
					var->i = rval;
				}
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
		struct string *sb = heap_get_string(b);
		if (string_index_valid(a) && heap[a].s) {
			string_append(&heap[a].s, sb);
		}
		heap_unref(b);
		stack_pop();
		stack_pop();
		if (string_index_valid(a) && heap[a].s)
			stack_push_string(string_ref(heap[a].s));
		else
			stack_push_string(string_ref(&EMPTY_STRING));
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
		bool noteq = !!strcmp(stack_peek_string(1)->text, stack_peek_string(0)->text);
		heap_unref(stack_pop().i);
		heap_unref(stack_pop().i);
		stack_push(noteq);
		break;
	}
	case S_EQUALE: {
		struct string *s1 = stack_peek_string(1);
		struct string *s0 = stack_peek_string(0);
		bool eq = !strcmp(s1->text, s0->text);
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
			// v14: stack = [heap_idx, page_idx, rval]
			// 2-slot ref points to the struct field to assign
			int rval = stack_pop().i;
			union vm_value *var = stack_pop_var();
			int old_slot = var->i;
			if (old_slot > 0 && (size_t)old_slot < heap_size) {
				heap_struct_assign(old_slot, rval);
			} else {
				// Field uninitialized (-1): alloc new slot, copy data, update field
				int new_slot = heap_alloc_slot(VM_PAGE);
				if (rval > 0 && (size_t)rval < heap_size && heap[rval].page)
					heap_set_page(new_slot, copy_page(heap[rval].page));
				var->i = new_slot;
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
			stack_push_string(string_ref(heap[array].s ? heap[array].s : &EMPTY_STRING));
		} else if (heap[array].type == VM_PAGE) {
			struct page *p = heap[array].page;
			if (p && (p->type >= NR_PAGE_TYPES || p->nr_vars < 0 || p->nr_vars > 1000000)) {
				{ static int aref_warn = 0; if (aref_warn++ < 5)
				WARNING("A_REF: corrupted page at slot %d (ptype=%d nr_vars=%d)", array, p->type, p->nr_vars); }
				stack_push(-1);
			} else if (ain->version >= 14) {
				// v14: reference semantics — share the heap slot
				// Structs are reference types in v14; no deep copy needed.
				heap_ref(array);
				stack_push(array);
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
		if (ain->version >= 14) {
			union vm_value *var = stack_pop_var();
			int dg_i = var->i;
			struct page *new_dg = delegate_new_from_method(obj, fun);
			if (dg_i > 0 && (size_t)dg_i < heap_size && heap[dg_i].ref > 0) {
				delete_page(dg_i);
				heap_set_page(dg_i, new_dg);
			} else {
				int new_slot = heap_alloc_page(new_dg);
				var->i = new_slot;
			}
		} else {
			int dg_i = stack_pop().i;
			if (dg_i > 0)
				delete_page(dg_i);
			else
				dg_i = heap_alloc_slot(VM_PAGE);
			heap_set_page(dg_i, delegate_new_from_method(obj, fun));
		}
		break;
	}
	case DG_ADD: {
		int fun = stack_pop().i;
		int obj = stack_pop().i;
		if (ain->version >= 14) {
			union vm_value *var = stack_pop_var();
			int dg_i = var->i;
			if (dg_i > 0 && (size_t)dg_i < heap_size && heap[dg_i].ref > 0) {
				struct page *dg = heap_get_delegate_page(dg_i);
				heap_set_page(dg_i, delegate_append(dg, obj, fun));
			} else {
				struct page *new_dg = delegate_append(NULL, obj, fun);
				int new_slot = heap_alloc_page(new_dg);
				var->i = new_slot;
			}
		} else {
			int dg_i = stack_pop().i;
			if (dg_i <= 0)
				dg_i = heap_alloc_slot(VM_PAGE);
			struct page *dg = heap_get_delegate_page(dg_i);
			heap_set_page(dg_i, delegate_append(dg, obj, fun));
		}
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
		if (ain->version >= 14) {
			union vm_value *var = stack_pop_var();
			int dg_i = var->i;
			if (dg_i > 0 && (size_t)dg_i < heap_size && heap[dg_i].ref > 0)
				delegate_erase(heap_get_delegate_page(dg_i), obj, fun);
		} else {
			int dg_i = stack_pop().i;
			delegate_erase(heap_get_delegate_page(dg_i), obj, fun);
		}
		break;
	}
	case DG_CLEAR: {
		if (ain->version >= 14) {
			union vm_value *var = stack_pop_var();
			int slot = var->i;
			if (slot > 0 && (size_t)slot < heap_size && heap[slot].ref > 0)
				heap_set_page(slot, delegate_clear(heap_get_delegate_page(slot)));
		} else {
			int slot = stack_pop().i;
			if (!slot)
				break;
			heap_set_page(slot, delegate_clear(heap_get_delegate_page(slot)));
		}
		break;
	}
	case DG_COPY: {
		stack_push(vm_copy_page(heap_get_delegate_page(stack_pop().i)));
		break;
	}
	case DG_ASSIGN: {
		// v14: destination is a 2-slot variable reference (heap_idx, page_idx)
		// Stack: [dst_ref(2-slot), src_delegate_slot]
		int set_i = stack_pop().i;
		if (ain->version >= 14) {
			union vm_value *var = stack_pop_var();
			int dst_i = var->i;
			struct page *set = heap_get_delegate_page(set_i);
			struct page *new_dg = copy_page(set);
			if (dst_i > 0 && (size_t)dst_i < heap_size && heap[dst_i].ref > 0) {
				delete_page(dst_i);
				heap_set_page(dst_i, new_dg);
			} else {
				// Uninitialized delegate slot — allocate and write back
				int new_slot = heap_alloc_page(new_dg);
				var->i = new_slot;
			}
		} else {
			int dst_i = stack_pop().i;
			struct page *set = heap_get_delegate_page(set_i);
			struct page *new_dg = copy_page(set);
			delete_page(dst_i);
			heap_set_page(dst_i, new_dg);
		}
		stack_push(set_i);
		break;
	}
	case DG_PLUSA: {
		// v14: destination is a 2-slot variable reference (heap_idx, page_idx)
		// Stack: [dst_ref(2-slot), src_delegate_slot]
		int add_i = stack_pop().i;
		if (ain->version >= 14) {
			union vm_value *var = stack_pop_var();
			int dst_i = var->i;
			struct page *add = heap_get_delegate_page(add_i);
			struct page *dst = heap_get_delegate_page(dst_i);
			struct page *result = delegate_plusa(dst, add);
			if (dst_i > 0 && (size_t)dst_i < heap_size && heap[dst_i].ref > 0) {
				heap_set_page(dst_i, result);
			} else {
				// Uninitialized delegate slot — allocate and write back
				int new_slot = heap_alloc_page(result);
				var->i = new_slot;
			}
		} else {
			int dst_i = stack_pop().i;
			struct page *add = heap_get_delegate_page(add_i);
			struct page *dst = heap_get_delegate_page(dst_i);
			heap_set_page(dst_i, delegate_plusa(dst, add));
		}
		stack_push(add_i);
		break;
	}
	case DG_MINUSA: {
		// v14: destination is a 2-slot variable reference (heap_idx, page_idx)
		// Stack: [dst_ref(2-slot), src_delegate_slot]
		int minus_i = stack_pop().i;
		if (ain->version >= 14) {
			union vm_value *var = stack_pop_var();
			int dst_i = var->i;
			struct page *minus = heap_get_delegate_page(minus_i);
			struct page *dst = heap_get_delegate_page(dst_i);
			struct page *result = delegate_minusa(dst, minus);
			if (dst_i > 0 && (size_t)dst_i < heap_size && heap[dst_i].ref > 0) {
				heap_set_page(dst_i, result);
			} else {
				int new_slot = heap_alloc_page(result);
				var->i = new_slot;
			}
		} else {
			int dst_i = stack_pop().i;
			struct page *minus = heap_get_delegate_page(minus_i);
			struct page *dst = heap_get_delegate_page(dst_i);
			heap_set_page(dst_i, delegate_minusa(dst, minus));
		}
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
		if (obj == -1 && ain->version >= 14) {
			// v14 lambda closure: obj=-1 means "capture current function's
			// local page as the closure environment".  X_GETENV in the
			// lambda body retrieves struct_page, which delegate_call sets
			// to this captured page_slot.
			obj = local_page_slot();
			heap_ref(obj);
			{ static int dnfm_log = 0; if (dnfm_log++ < 20) {
				int caller = call_stack[call_stack_ptr-1].fno;
				WARNING("DG_NEW_FROM_METHOD: closure capture fun=%d obj=-1→%d caller=%d '%s'",
					fun, obj, caller, ain->functions[caller].name);
			}}
		}
		// DIAG: track delegate creation for delayed callback functions
		if (fun == 36117 || fun == 36123 || fun == 27282) {
			int caller = call_stack_ptr > 0 ? call_stack[call_stack_ptr-1].fno : -1;
			int sp_page = call_stack_ptr > 0 ? call_stack[call_stack_ptr-1].struct_page : -1;
			WARNING("DG_NEW_FROM_METHOD: fun=%d obj=%d ip=0x%lX caller=%d '%s' struct_page=%d",
				fun, obj, (unsigned long)instr_ptr, caller,
				(caller >= 0 && caller < ain->nr_functions) ? ain->functions[caller].name : "?",
				sp_page);
			if (obj >= 0 && heap_index_valid(obj) && heap[obj].type == VM_PAGE && heap[obj].page) {
				struct page *p = heap[obj].page;
				WARNING("  captured page: slot=%d type=%d index=%d nr_vars=%d",
					obj, p->type, p->index, p->nr_vars);
			}
			// Dump preceding instructions for context
			size_t ip = instr_ptr;
			WARNING("  bytecode at ip-20:");
			for (int bi = -20; bi <= 0; bi += 2) {
				if (ip + bi >= 0 && ip + bi + 6 <= ain->code_size) {
					uint16_t op = *(uint16_t*)(ain->code + ip + bi);
					int32_t arg = *(int32_t*)(ain->code + ip + bi + 2);
					WARNING("    0x%lX: op=0x%04X arg=%d", (unsigned long)(ip + bi), op, arg);
				}
			}
			// Dump call stack
			for (int ci = call_stack_ptr - 1; ci >= 0 && ci >= call_stack_ptr - 4; ci--) {
				WARNING("  callstack[%d]: fno=%d '%s' struct_page=%d",
					ci, call_stack[ci].fno,
					(call_stack[ci].fno >= 0 && call_stack[ci].fno < ain->nr_functions) ?
						ain->functions[call_stack[ci].fno].name : "?",
					call_stack[ci].struct_page);
			}
		}
		int slot = heap_alloc_page(delegate_new_from_method(obj, fun));
		stack_push(slot);
		break;
	}
	case DG_CALLBEGIN: { // DG_TYPE
		int dg_no = get_argument(0);
		if (dg_no < 0 || dg_no >= ain->nr_delegates)
			VM_ERROR("Invalid delegate index");
		struct ain_function_type *dg = &ain->delegates[dg_no];
		// DIAG: trace delegate dispatch during RegisterEvent
		{
			static int dg_diag = 0;
			int caller_fno = call_stack_ptr > 0 ? call_stack[call_stack_ptr-1].fno : -1;
			if (caller_fno == 27194 || caller_fno == 36115 || caller_fno == 27192) {
				dg_diag++;
				if (dg_diag <= 20) {
					int dg_page = stack_peek(dg->nr_arguments).i;
					WARNING("DG_CALLBEGIN DIAG[%d]: dg_no=%d nargs=%d dg_page=%d caller=func#%d ip=0x%lX",
						dg_diag, dg_no, dg->nr_arguments, dg_page, caller_fno, (unsigned long)instr_ptr);
				}
			}
		}

		// Stack before: [dg_page, arg0, ...]
		// Stack after:  [arg0, ..., dg_page, 0(dg_index)]
		int dg_page = stack_peek(dg->nr_arguments).i;
		for (int i = 0; i < dg->nr_arguments; i++) {
			int pos = (stack_ptr - dg->nr_arguments) + i;
			stack[pos-1] = stack[pos];
		}
		stack[stack_ptr-1].i = dg_page;
		stack_push(0);

		// XXX: If the delegate has a return value, we push a dummy value
		//      so that DG_CALL can replace it
		if (dg->return_type.data != AIN_VOID) {
			stack_push(0);
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
		// v14: stack has [obj_page, string_slot], arg = delegate type
		// Must combine obj's class name with method name: "ClassName@MethodName"
		int str_slot = stack_pop().i;
		struct string *name = heap_get_string(str_slot);
		int fno = -1;
		if (name && name->size > 0) {
			// First try exact name (global functions)
			fno = ain_get_function(ain, name->text);
			// If not found, try with class prefix from object on stack
			if (fno < 0 && ain->version >= 14) {
				int obj_page = stack_peek(0).i;
				if (obj_page > 0 && heap_index_valid(obj_page) && heap[obj_page].page
				    && heap[obj_page].page->type == STRUCT_PAGE) {
					int sidx = heap[obj_page].page->index;
					if (sidx >= 0 && sidx < ain->nr_structures) {
						char buf[512];
						snprintf(buf, sizeof(buf), "%s@%s",
							ain->structures[sidx].name, name->text);
						fno = ain_get_function(ain, buf);
						// resolved
					}
				}
			}
			if (fno < 0) {
				static int dg_miss = 0;
				if (dg_miss++ < 20) {
					int obj_page = stack_peek(0).i;
					const char *sname = "?";
					if (obj_page > 0 && heap_index_valid(obj_page) && heap[obj_page].page
					    && heap[obj_page].page->type == STRUCT_PAGE) {
						int sidx = heap[obj_page].page->index;
						if (sidx >= 0 && sidx < ain->nr_structures)
							sname = ain->structures[sidx].name;
					}
					int caller_fno = call_stack_ptr > 0 ? call_stack[call_stack_ptr-1].fno : -1;
					WARNING("DG_STR_TO_METHOD: '%s' on struct '%s' (obj_page=%d) -> fno=%d (dg_type=%d) ip=0x%lX caller=%d '%s'",
						display_sjis0(name->text), sname, obj_page, fno, get_argument(0),
						(unsigned long)instr_ptr, caller_fno,
						(caller_fno >= 0 && caller_fno < ain->nr_functions) ? ain->functions[caller_fno].name : "?");
				}
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
			static int xref_fail = 0;
			if (xref_fail++ < 20) {
				int caller_fno = call_stack_ptr > 0 ? call_stack[call_stack_ptr-1].fno : -1;
				WARNING("X_REF FAIL: n=%d heap=%d var=%d page=%s nr_vars=%d ip=0x%lX caller=%d '%s'",
					n, heap_idx, var_idx, page ? "valid" : "NULL",
					page ? page->nr_vars : -1, (unsigned long)instr_ptr,
					caller_fno,
					(caller_fno >= 0 && caller_fno < ain->nr_functions) ?
						ain->functions[caller_fno].name : "?");
			}
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
		if (var && var != dummy_var) {
			for (int i = 0; i < n; i++)
				var[i] = vals[i];
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
		// X_GETENV: replace PUSHLOCALPAGE with parent environment page.
		// Always preceded by PUSHLOCALPAGE; replaces that value with the
		// enclosing function's captured environment (struct_page).
		// Equivalent to: pop local_page_slot, push struct_page_slot.
		stack[stack_ptr - 1].i = struct_page_slot();
		break;
	}
	case X_SET: {
		// X_SET: assign value to variable reference (like X_ASSIGN 1 but no argument)
		// Unlike X_ASSIGN, X_SET is followed by DELETE (not SP_INC), so it must
		// handle ref counting internally for ref-counted types.
		// stack: [..., heap_idx, page_idx, value]
		union vm_value val = stack_pop();
		int32_t page_index = stack_pop().i;
		int32_t heap_index = stack_pop().i;
		if (heap_index_valid(heap_index) && heap[heap_index].page
		    && page_index >= 0 && page_index < heap[heap_index].page->nr_vars) {
			struct page *page = heap[heap_index].page;
			// Check if variable is ref-counted type
			enum ain_data_type dtype = variable_type(page, page_index, NULL, NULL);
			switch (dtype) {
			case AIN_STRING:
			case AIN_STRUCT:
			case AIN_DELEGATE:
			case AIN_ARRAY_TYPE:
			case AIN_ARRAY:
			case AIN_WRAP:
			case AIN_REF_TYPE:
				// Unref old value
				if (page->values[page_index].i != -1)
					heap_unref(page->values[page_index].i);
				// Ref new value
				if (val.i != -1)
					heap_ref(val.i);
				break;
			default:
				break;
			}
			page->values[page_index] = val;
		}
		stack_push(val);
		break;
	}
	case X_ICAST: {
		// X_ICAST target_type: interface cast.
		// Peek at struct page on stack, look up the interface vtable offset
		// for target_type, push [page_index, vtable_offset].
		// Stack: [..., page_idx] → [..., page_idx, page_idx, vtable_offset]
		// Net: +2 (peek, push 2)
		int target_type = get_argument(0);
		int32_t page_idx = stack_peek(0).i;
		int vtoff = 0;
		if (heap_index_valid(page_idx) && heap[page_idx].type == VM_PAGE
		    && heap[page_idx].page) {
			struct page *p = heap[page_idx].page;
			int sidx = p->index;
			if (sidx >= 0 && sidx < ain->nr_structures) {
				struct ain_struct *s = &ain->structures[sidx];
				for (int i = 0; i < s->nr_interfaces; i++) {
					if (s->interfaces[i].struct_type == target_type) {
						vtoff = s->interfaces[i].vtable_offset;
						break;
					}
				}
			}
		}
		stack_push((union vm_value){.i = page_idx});
		stack_push((union vm_value){.i = vtoff});
		break;
	}
	case X_OP_SET: {
		// X_OP_SET arg: assign to multi-slot variable (option/wrap/iface_wrap types)
		// arg encoding: low 16 bits = type hint, actual slot count = max(2, low16)
		// stack = [..., heap_idx, page_idx, val0, ..., val(n-1)]
		// Pop n values, pop 2-slot var ref, write n consecutive slots, push n values
		int arg = get_argument(0);
		int n = arg & 0xFFFF;
		if (n < 2) n = 2;  // v14 multi-slot types are always >= 2 slots
		if (n > 8) n = 8;
		union vm_value vals[8];
		for (int i = n - 1; i >= 0; i--) {
			vals[i] = stack_pop();
		}
		// Peek at 2-slot ref to determine variable types for ref counting
		int32_t peek_page_idx = stack_peek(0).i;
		int32_t peek_heap_idx = stack_peek(1).i;
		struct page *target_page = NULL;
		if (peek_heap_idx >= 0 && (size_t)peek_heap_idx < heap_size
		    && heap[peek_heap_idx].ref > 0 && heap[peek_heap_idx].type == VM_PAGE)
			target_page = heap[peek_heap_idx].page;
		union vm_value *var = stack_pop_var();
		if (var && target_page) {
			for (int i = 0; i < n; i++) {
				// Check variable type for ref counting
				enum ain_data_type dtype = variable_type(target_page, peek_page_idx + i, NULL, NULL);
				switch (dtype) {
				case AIN_STRING:
				case AIN_STRUCT:
				case AIN_DELEGATE:
				case AIN_ARRAY_TYPE:
				case AIN_ARRAY:
				case AIN_WRAP:
				case AIN_OPTION:
				case AIN_IFACE:
				case AIN_IFACE_WRAP:
				case AIN_REF_TYPE:
					// Ref new value, unref old value
					if (vals[i].i > 0) heap_ref(vals[i].i);
					if (var[i].i > 0 && var[i].i != -1) heap_unref(var[i].i);
					break;
				default:
					break;
				}
				var[i] = vals[i];
			}
		} else if (var) {
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
			// size=0: create an empty array page to preserve type metadata
			// (struct_type, data_type) for later EmplaceBack calls.
			struct page *page = alloc_page(ARRAY_PAGE,
				(data_type == AIN_ARRAY || data_type == AIN_REF_ARRAY) ? data_type : AIN_ARRAY_INT, 0);
			page->array.struct_type = struct_type;
			heap_set_page(slot, page);
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
		int size = page ? page->nr_vars : 0;
		// Detect suspicious array sizes that may cause infinite loops
		{
			static int xa_diag = 0;
			if (xa_diag < 5 && size > 100000) {
				int fno = call_stack_ptr > 0 ? call_stack[call_stack_ptr-1].fno : -1;
				WARNING("X_A_SIZE: huge array size=%d slot=%d page_type=%d ip=0x%lX fno=%d '%s'",
					size, slot, page ? page->type : -1,
					(unsigned long)instr_ptr, fno,
					(fno >= 0 && fno < ain->nr_functions) ? ain->functions[fno].name : "?");
				xa_diag++;
			}
		}
		stack_push(size);
		break;
	}
	case X_TO_STR: {
		// X_TO_STR type: convert top of stack value to string
		int type = get_argument(0);
		union vm_value val = stack_pop();
		struct string *s;
		switch (type) {
		case AIN_INT:
		case AIN_BOOL:
		case AIN_LONG_INT:
			s = integer_to_string(val.i);
			break;
		case AIN_FLOAT:
			s = float_to_string(val.f, 6);
			break;
		case AIN_STRING:
			// Already a string — ref it
			s = string_ref(heap_get_string(val.i));
			heap_unref(val.i);
			break;
		default:
			s = integer_to_string(val.i);
			break;
		}
		stack_push_string(s);
		break;
	}
	// -- NOOPs ---
	case FUNC:
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
		// DIAG: trace all CALLFUNC/CALLMETHOD inside View_Update (one frame)
		{
			static int vu_trace_count = 0;
			static bool vu_trace_active = false;
			int cur_fno = call_stack_ptr > 0 ? call_stack[call_stack_ptr-1].fno : -1;
			// Detect when View_Update (21031) is on call stack
			bool vu_on_stack = false;
			for (int ci = call_stack_ptr - 1; ci >= 0 && !vu_on_stack; ci--) {
				if (call_stack[ci].fno == 21031) vu_on_stack = true;
			}
			if (vu_on_stack && !vu_trace_active && vu_trace_count == 0) {
				vu_trace_active = true;
			}
			if (vu_trace_active && vu_trace_count < 500 && vu_on_stack &&
			    (opcode == CALLFUNC || opcode == CALLMETHOD || opcode == CALLFUNC2 || opcode == CALLHLL)) {
				int nargs = (opcode < NR_OPCODES) ? instructions[opcode].nr_args : 0;
				int32_t a0 = nargs >= 1 ? LittleEndian_getDW(ain->code, instr_ptr + 2) : 0;
				// For CALLMETHOD v14, funcno is on stack
				if (opcode == CALLMETHOD && ain->version >= 14) {
					int cm_nargs = a0;
					if (stack_ptr > cm_nargs)
						a0 = stack[stack_ptr - 1 - cm_nargs].i; // funcno
				}
				const char *tgt = (opcode != CALLHLL && a0 >= 0 && a0 < ain->nr_functions) ? ain->functions[a0].name : "";
				const char *oname = (opcode < NR_OPCODES) ? instructions[opcode].name : "?";
				WARNING("VU_CALL[%03d] %s %d '%s' from func#%d '%s'",
					vu_trace_count++, oname, a0, tgt, cur_fno,
					(cur_fno >= 0 && cur_fno < ain->nr_functions) ? ain->functions[cur_fno].name : "?");
			}
			if (vu_trace_active && !vu_on_stack && vu_trace_count > 0) {
				vu_trace_active = false; // done tracing this frame
			}
		}
		if (0) {
			static int join_trace_count = 0;
			int cur_fno = call_stack_ptr > 0 ? call_stack[call_stack_ptr-1].fno : -1;
			if ((cur_fno == 27268 || cur_fno == 20992) && join_trace_count < 300) {
				const char *oname = (opcode < NR_OPCODES) ? instructions[opcode].name : "???";
				int nargs = (opcode < NR_OPCODES) ? instructions[opcode].nr_args : 0;
				if (nargs >= 2) {
					int32_t a0 = LittleEndian_getDW(ain->code, instr_ptr + 2);
					int32_t a1 = LittleEndian_getDW(ain->code, instr_ptr + 6);
					WARNING("JOIN[%03d] 0x%lX: %s %d %d", join_trace_count, (unsigned long)instr_ptr, oname, a0, a1);
				} else if (nargs == 1) {
					int32_t a0 = LittleEndian_getDW(ain->code, instr_ptr + 2);
					WARNING("JOIN[%03d] 0x%lX: %s %d", join_trace_count, (unsigned long)instr_ptr, oname, a0);
				} else {
					WARNING("JOIN[%03d] 0x%lX: %s", join_trace_count, (unsigned long)instr_ptr, oname);
				}
				join_trace_count++;
			}
		}
		// Periodically render and process events (~every 4M instructions)
		if (unlikely((insn_count & 0x3FFFFF) == 0)) {
			// Skip rendering AND event processing during constructor/vm_call phases
			// (EX loading can take 100M+ insns; event handling can trigger
			// SDL_QUIT from macOS "not responding" detection)
			bool in_vm_call = false;
			for (int ci = call_stack_ptr - 1; ci > 0; ci--) {
				if (call_stack[ci].return_address == VM_RETURN) {
					in_vm_call = true;
					break;
				}
			}
			if (!in_vm_call) {
				handle_window_events();
				scene_render();
				gfx_swap();
			}
			// Heartbeat trace every ~16M instructions
			if ((insn_count & 0xFFFFFF) == 0) {
				static struct timespec hb_start = {0};
				static struct timespec hb_epoch = {0};
				struct timespec hb_now;
				clock_gettime(CLOCK_MONOTONIC, &hb_now);
				if (hb_epoch.tv_sec == 0) hb_epoch = hb_now;
				double wall = (hb_now.tv_sec - hb_epoch.tv_sec) + (hb_now.tv_nsec - hb_epoch.tv_nsec) / 1e9;
				double interval = (hb_start.tv_sec > 0)
					? (hb_now.tv_sec - hb_start.tv_sec) + (hb_now.tv_nsec - hb_start.tv_nsec) / 1e9
					: 0.0;
				double mips = (interval > 0) ? 16.8 / interval : 0.0;
				WARNING("HEARTBEAT: insn=%llu wall=%.1fs %.1fMIPS d=%d used=%zu mc=%d na=%d new=%d vc=%d",
					insn_count, wall, mips, call_stack_ptr, heap_free_ptr,
					mc_skip_total, nargs_skip_total, new_count, vmcall_count);
				fflush(stderr);
				hb_start = hb_now;
			}
			// vm_call timeout: bail out if destructor/constructor takes too long
			if (vm_call_insn_limit > 0 && insn_count > vm_call_insn_limit) {
				// Find nearest VM_RETURN frame (skip frame 0 = initial entry)
				int vm_ret_frame = -1;
				for (int ci = call_stack_ptr - 1; ci > 0; ci--) {
					if (call_stack[ci].return_address == VM_RETURN) {
						vm_ret_frame = ci;
						break;
					}
				}
				if (vm_ret_frame > 0) {
					static int timeout_warn = 0;
					if (timeout_warn++ < 10) {
						int fno_t = call_stack[vm_ret_frame].fno;
						WARNING("VM_CALL_TIMEOUT: %llu insns in vm_call fno=%d '%s', unwinding %d frames",
							insn_count - (vm_call_insn_limit - 500000), fno_t,
							(fno_t >= 0 && fno_t < ain->nr_functions) ? ain->functions[fno_t].name : "?",
							call_stack_ptr - vm_ret_frame);
					}
					int target_sp = call_stack[vm_ret_frame].base_sp;
					while (call_stack_ptr > vm_ret_frame) {
						int ps = call_stack[call_stack_ptr-1].page_slot;
						call_stack_ptr--;
						exit_unref(ps);
					}
					stack_ptr = target_sp;
					instr_ptr = VM_RETURN;
					vm_call_insn_limit = 0;
					continue;
				}
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

static volatile sig_atomic_t in_signal_handler = 0;

static void sigabrt_handler(int sig)
{
	// Prevent re-entrant crashes (e.g., SIGSEGV inside this handler)
	if (in_signal_handler) {
		signal(sig, SIG_DFL);
		raise(sig);
		_exit(128 + sig);
	}
	in_signal_handler = 1;

	WARNING("=== SIGNAL %d received ===", sig);
	extern void hll_dump_ring(void);
	hll_dump_ring();
	if (call_stack_ptr > 0) {
		int fno = call_stack[call_stack_ptr-1].fno;
		WARNING("Current function: fno=%d '%s' ip=0x%lX sp=%d",
			fno, (fno >= 0 && fno < ain->nr_functions) ? ain->functions[fno].name : "?",
			(unsigned long)instr_ptr, stack_ptr);
	}
	dump_trace_buf();
	// Print C backtrace
	void *bt[64];
	int n = backtrace(bt, 64);
	WARNING("=== C backtrace (%d frames) ===", n);
	char **syms = backtrace_symbols(bt, n);
	if (syms) {
		for (int i = 0; i < n; i++)
			WARNING("  [%d] %s", i, syms[i]);
		free(syms);
	}
	signal(sig, SIG_DFL);
	raise(sig);
	_exit(128 + sig);
}

int vm_execute_ain(struct ain *program)
{
	ain = program;

	// Install signal handlers to dump trace buffer and C backtrace
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

	// Protect ain strings/messages from being realloc'd by cow_check.
	// The ain table holds a reference to each string, but the string's ref
	// count doesn't account for it. When all runtime refs are freed, ref
	// drops to 1, and cow_check(ref=1,cow=1) allows in-place realloc,
	// corrupting the ain table pointer. Fix: increment ref for ain's ownership.
	for (int i = 0; i < ain->nr_strings; i++) {
		if (ain->strings[i]) ain->strings[i]->ref++;
	}
	for (int i = 0; i < ain->nr_messages; i++) {
		if (ain->messages[i]) ain->messages[i]->ref++;
	}

	heap_init();
	init_libraries();

	// Initialize globals
	heap[0].ref = 1;
	heap[0].seq = heap_next_seq++;
	heap[0].type = VM_PAGE;
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

	WARNING("VM: alloc=%d, main=%d, nr_globals=%d, nr_functions=%d, version=%d, nr_delegates=%d",
		ain->alloc, ain->main, ain->nr_globals, ain->nr_functions, ain->version, ain->nr_delegates);
	// DIAG: find LibraryObject and View_Update functions
	for (int fi = 0; fi < ain->nr_functions; fi++) {
		const char *fn = ain->functions[fi].name;
		if (!fn) continue;
		if (strstr(fn, "LibraryObject") || strstr(fn, "View_Update")) {
			static int lo_log = 0;
			if (lo_log++ < 80)
				WARNING("  func#%d '%s'", fi, fn);
		}
	}
	// (v14-init dump removed)
	if (ain->alloc >= 0 && ain->functions[ain->alloc].address != 0xFFFFFFFF) {
		vm_in_alloc_phase = true;
		vm_call(ain->alloc, -1);
		vm_in_alloc_phase = false;
		free_deferred_strings();
	}
	// (v14-post-alloc dump removed)
	// v14: after alloc, repair any global struct pages that were corrupted
	// during the alloc function (pages freed and reused as local pages).
	if (ain->version >= 14) {
		int repaired = 0;
		for (int i = 0; i < ain->nr_globals; i++) {
			if (ain->globals[i].type.data != AIN_STRUCT)
				continue;
			int slot = heap[0].page->values[i].i;
			if (slot <= 0)
				continue;
			bool need_repair = !heap_index_valid(slot)
				|| heap[slot].type != VM_PAGE
				|| !heap[slot].page
				|| heap[slot].page->type != STRUCT_PAGE;
			if (need_repair) {
				WARNING("v14: repairing global[%d] '%s' struct=%d (slot %d had page_type=%d)",
					i, ain->globals[i].name, ain->globals[i].type.struc,
					slot, (heap_index_valid(slot) && heap[slot].page) ? heap[slot].page->type : -1);
				int new_slot = alloc_struct(ain->globals[i].type.struc);
				heap[0].page->values[i].i = new_slot;
				repaired++;
			}
		}
		if (repaired > 0)
			WARNING("v14: repaired %d global struct pages after alloc", repaired);

		for (int i = 0; i < ain->nr_globals; i++) {
			if (ain->globals[i].type.data != AIN_STRUCT)
				continue;
			int slot = heap[0].page->values[i].i;
			if (slot <= 0) continue;
			int ptype = -1;
			if (heap_index_valid(slot) && heap[slot].page)
				ptype = heap[slot].page->type;
			if (ptype != STRUCT_PAGE)
				WARNING("v14: global[%d] '%s' slot=%d page_type=%d (expected 2)",
					i, ain->globals[i].name, slot, ptype);
		}
	}

	// Global constructors must be called AFTER initializing non-struct variables
	if (ain->version >= 14) {
		// v14: call top-level constructors in reverse order to satisfy dependencies
		for (int i = ain->nr_globals - 1; i >= 0; i--) {
			if (ain->globals[i].type.data == AIN_STRUCT) {
				int slot = heap[0].page->values[i].i;
				if (slot > 0 && heap_index_valid(slot))
					init_global_struct_v14(ain->globals[i].type.struc, slot);
			}
		}
	} else {
		for (int i = 0; i < ain->nr_globals; i++) {
			if (ain->globals[i].type.data == AIN_STRUCT) {
				int slot = heap[0].page->values[i].i;
				if (slot > 0 && heap_index_valid(slot))
					init_struct(ain->globals[i].type.struc, slot);
			}
		}
	}
	// (v14-post-ctor dump removed)
	/* v14: after constructors, repair ALL heap slots that should be structs
	 * but have been overwritten (e.g. by delegate/local page reuse).
	 * We scan all heap struct pages and recursively check their members. */
	if (ain->version >= 14) {
		int deep_repaired = 0;
		// Recursive repair helper: checks struct members up to max_depth
		// Uses a simple iterative worklist to avoid C stack overflow.
		#define REPAIR_MAX_DEPTH 8
		struct { int slot; int depth; } worklist[4096];
		int wl_head = 0, wl_tail = 0;
		#define WL_PUSH(s, d) do { if (wl_tail < 4096) { worklist[wl_tail].slot = (s); worklist[wl_tail].depth = (d); wl_tail++; } } while(0)

		// Seed worklist with all struct pages on the heap
		for (int slot = 1; (size_t)slot < heap_size; slot++) {
			if (heap[slot].type != VM_PAGE || !heap[slot].page)
				continue;
			if (heap[slot].page->type != STRUCT_PAGE)
				continue;
			WL_PUSH(slot, 0);
		}

		while (wl_head < wl_tail) {
			int slot = worklist[wl_head].slot;
			int depth = worklist[wl_head].depth;
			wl_head++;

			if (!heap_index_valid(slot) || heap[slot].type != VM_PAGE || !heap[slot].page)
				continue;
			struct page *pg = heap[slot].page;
			if (pg->type != STRUCT_PAGE)
				continue;
			int struc = pg->index;
			if (struc < 0 || struc >= ain->nr_structures)
				continue;

			for (int m = 0; m < ain->structures[struc].nr_members; m++) {
				if (ain->structures[struc].members[m].type.data != AIN_STRUCT)
					continue;
				if (m >= pg->nr_vars) continue;
				int mslot = pg->values[m].i;
				if (mslot <= 0 || !heap_index_valid(mslot))
					continue;
				bool bad = heap[mslot].type != VM_PAGE
					|| !heap[mslot].page
					|| heap[mslot].page->type != STRUCT_PAGE;
				if (bad) {
					int mstruc = ain->structures[struc].members[m].type.struc;
					if (deep_repaired < 50)
						WARNING("v14: deep repair struct[%d].member[%d] '%s.%s' slot=%d (was page_type=%d) depth=%d",
							struc, m, ain->structures[struc].name,
							ain->structures[struc].members[m].name,
							mslot,
							(heap_index_valid(mslot) && heap[mslot].type == VM_PAGE && heap[mslot].page)
								? heap[mslot].page->type : -1,
							depth);
					int new_slot = alloc_struct(mstruc);
					pg->values[m].i = new_slot;
					deep_repaired++;
					// Queue newly allocated struct for recursive repair
					if (depth + 1 < REPAIR_MAX_DEPTH)
						WL_PUSH(new_slot, depth + 1);
				} else if (depth + 1 < REPAIR_MAX_DEPTH) {
					// Valid struct member — still recurse to check its members
					WL_PUSH(mslot, depth + 1);
				}
			}
		}
		#undef WL_PUSH
		#undef REPAIR_MAX_DEPTH
		if (deep_repaired > 0)
			WARNING("v14: deep repaired %d struct member pages after constructors (recursive)", deep_repaired);
	}

	vm_call(ain->main, -1);
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
	WARNING("vm_exit(%d) — callstack depth=%d heap=%zu", code, call_stack_ptr, heap_size);
					fflush(stderr);
	for (int _cs = call_stack_ptr - 1; _cs >= 0 && _cs >= call_stack_ptr - 20; _cs--) {
		int _fno = call_stack[_cs].fno;
		WARNING("  exit callstack[%d]: fno=%d '%s'", _cs, _fno,
			(_fno >= 0 && _fno < ain->nr_functions && ain->functions[_fno].name)
			? ain->functions[_fno].name : "?");
	}
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
