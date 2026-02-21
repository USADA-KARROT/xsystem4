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
#include "gfx/gfx.h"
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
static union vm_value dummy_var;
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
		dummy_var.i = 0;
		return &dummy_var;
	}
	if (unlikely(!heap[heap_index].page || page_index < 0 || page_index >= heap[heap_index].page->nr_vars)) {
		static int oob_trace = 0;
		if (oob_trace < 20) {
			{
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
		}
			if (oob_trace == 0)
				dump_trace_buf();
			oob_trace++;
		}
		dummy_var.i = 0;
		return &dummy_var;
	}
	return &heap[heap_index].page->values[page_index];
}

union vm_value *stack_peek_var(void)
{
	int32_t page_index = stack_peek(0).i;
	int32_t heap_index = stack_peek(1).i;
	if (unlikely(!heap_index_valid(heap_index))) {
		WARNING("stack_peek_var: invalid heap index %d/%d", heap_index, page_index);
		dummy_var.i = 0;
		return &dummy_var;
	}
	if (unlikely(!heap[heap_index].page || page_index < 0 || page_index >= heap[heap_index].page->nr_vars)) {
		WARNING("stack_peek_var: out of bounds %d/%d", heap_index, page_index);
		dummy_var.i = 0;
		return &dummy_var;
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
		return (union vm_value) { .i = vm_string_ref(heap_get_string(v.i)) };
	case AIN_STRUCT:
	case AIN_DELEGATE:
	case AIN_ARRAY_TYPE:
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
}

static void method_call(int fno, int return_address)
{
	function_call(fno, return_address);
	int struct_page = stack_pop().i;
	// Validate struct_page: should point to a STRUCT_PAGE, not LOCAL_PAGE
	if (heap_index_valid(struct_page) && heap[struct_page].page) {
		struct page *sp = heap[struct_page].page;
		if (sp->type != STRUCT_PAGE) {
			static int mc_trace = 0;
			if (mc_trace < 20) {
				WARNING("method_call: struct_page=%d is type %d (idx=%d nr_vars=%d), "
					"expected STRUCT_PAGE. fno=%d '%s' nr_args=%d page_slot=%d",
					struct_page, sp->type, sp->index, sp->nr_vars,
					fno, ain->functions[fno].name, ain->functions[fno].nr_args,
					call_stack[call_stack_ptr-1].page_slot);
				mc_trace++;
			}
		}
	}
	call_stack[call_stack_ptr-1].struct_page = struct_page;
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
	if (delegate_get(heap_get_delegate_page(dg_page), dg_index, &obj, &fun)) {
		// Guard: skip invalid function numbers
		if (fun < 0 || fun >= ain->nr_functions) {
			WARNING("delegate_call: invalid function number %d, skipping", fun);
			// increment dg_index to advance past this entry
			stack[stack_ptr - 1].i++;
			return;
		}
		// pop previous return value
		if (ain->delegates[dg_no].return_type.data != AIN_VOID) {
			stack_pop();
		}
		// increment dg_index
		stack[stack_ptr - 1].i++;

		int slot = _function_call(fun, instr_ptr + instruction_width(DG_CALL));

		// copy arguments into local page
		struct ain_function_type *dg = &ain->delegates[dg_no];
		for (int i = 0; i < dg->nr_arguments; i++) {
			union vm_value arg = stack_peek((dg->nr_arguments + 1) - i);
			heap[slot].page->values[i] = vm_copy(arg, dg->variables[i].type.data);
		}

		call_stack[call_stack_ptr-1].struct_page = obj;
	} else {
		// call finished: clean up stack and jump to return address
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
	int page_slot = call_stack[call_stack_ptr-1].page_slot;
	size_t ret_addr = call_stack[call_stack_ptr-1].return_address;
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
				// Call constructor as method
				size_t saved_ip = instr_ptr;
				method_call(ctor_func, VM_RETURN);
				vm_execute();
				instr_ptr = saved_ip;
				// Constructor is void — push 'this' back on stack
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
		int _fno = get_argument(0);
		if (_fno >= 0 && _fno < ain->nr_functions
				&& ain->functions[_fno].address == 0xFFFFFFFF) {
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
				&& ain->functions[_fno2].address == 0xFFFFFFFF) {
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
			// Diagnostic: check nargs vs function's nr_args
			{
				static int cm_trace = 0;
				if (cm_trace < 30 && funcno >= 0 && funcno < ain->nr_functions) {
					int fnr = ain->functions[funcno].nr_args;
					int caller_fno = call_stack_ptr > 0 ? call_stack[call_stack_ptr-1].fno : -1;
					if (nargs != fnr) {
						WARNING("CALLMETHOD: ip=0x%lX nargs=%d != nr_args=%d funcno=%d '%s' "
							"caller=%d '%s' sp=%d peek=%d",
							(unsigned long)(instr_ptr - instruction_width(CALLMETHOD)),
							nargs, fnr, funcno, ain->functions[funcno].name,
							caller_fno, caller_fno >= 0 ? ain->functions[caller_fno].name : "?",
							stack_ptr,
							stack_ptr > 0 ? stack[stack_ptr-1].i : -999);
						cm_trace++;
					}
				}
			}
			// Handle nargs vs resolved function's nr_args mismatch.
			// When vtable dispatch resolves to the wrong function (common
			// for unimplemented interface methods), nargs won't match.
			// Skip the call entirely to prevent stack corruption.
			bool skip_call = false;
			if (funcno >= 0 && funcno < ain->nr_functions) {
				int fnr_args = ain->functions[funcno].nr_args;
				if (nargs != fnr_args) {
					skip_call = true;
				}
			}
			if (skip_call) {
				// Vtable mismatch: skip the call entirely.
				// Stack is currently: [struct_page] (args and funcno already popped)
				if (saved_args != small_args) free(saved_args);
				stack_pop(); // struct_page
				// CALLMETHOD has ip_inc=0, so we must advance instr_ptr manually.
				instr_ptr += instruction_width(CALLMETHOD);
				// Check if caller expects a return value via bytecode lookahead.
				int16_t next_op = get_opcode(instr_ptr);
				if (next_op == POP || next_op == DELETE) {
					stack_push((union vm_value){.i = 0});
				}
			} else {
				// Push args back for method_call/function_call to consume.
				for (int i = nargs - 1; i >= 0; i--) {
					stack_push(saved_args[i]);
				}
				if (saved_args != small_args) free(saved_args);
				// Stack: [struct_page, arg0, ..., argN-1]
				if (funcno >= 0 && funcno < ain->nr_functions) {
					if (ain->functions[funcno].address == 0xFFFFFFFF) {
						// Function has no body (null constructor/method).
						method_call(funcno, instr_ptr + instruction_width(CALLMETHOD));
						function_return();
					} else {
						method_call(funcno, instr_ptr + instruction_width(CALLMETHOD));
					}
				} else {
					WARNING("CALLMETHOD: invalid funcno=%d nargs=%d", funcno, nargs);
					for (int i = 0; i < nargs; i++)
						stack_pop();
					stack_pop(); // struct_page
				}
			}
		} else {
			// Pre-v14: bytecode arg = function number
			method_call(get_argument(0), instr_ptr + instruction_width(CALLMETHOD));
		}
		break;
	}
	case CALLHLL: {
		int hll_arg2 = (ain->version >= 11) ? get_argument(2) : -1;
		hll_call(get_argument(0), get_argument(1), hll_arg2);
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
		int rval = stack_peek(0).i;
		int lval = stack_peek(1).i;
		heap_string_assign(lval, heap_get_string(rval));
		// remove A from the stack, but leave B
		stack_set(1, rval);
		stack_pop();
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
		bool noteq = !!strcmp(stack_peek_string(1)->text, stack_peek_string(0)->text);
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
			// v14: A_REF on string — create a string reference copy
			stack_push_string(string_ref(heap[array].s ? heap[array].s : &EMPTY_STRING));
		} else if (heap[array].type == VM_PAGE) {
			struct page *p = heap[array].page;
			if (p && (p->type >= NR_PAGE_TYPES || p->nr_vars < 0 || p->nr_vars > 1000000)) {
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
		int dst_i = stack_pop().i;
		struct page *set = heap_get_delegate_page(set_i);
		struct page *new_dg = copy_page(set);
		delete_page(dst_i);
		heap_set_page(dst_i, new_dg);
		stack_push(set_i);
		break;
	}
	case DG_PLUSA: {
		int add_i = stack_pop().i;
		int dst_i = stack_pop().i;
		struct page *add = heap_get_delegate_page(add_i);
		struct page *dst = heap_get_delegate_page(dst_i);
		heap_set_page(dst_i, delegate_plusa(dst, add));
		stack_push(add_i);
		break;
	}
	case DG_MINUSA: {
		int minus_i = stack_pop().i;
		int dst_i = stack_pop().i;
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
		// In v14 this has 1 arg (delegate type), stack has: [obj_page, string_slot]
		int str_slot = stack_pop().i;
		struct string *name = heap_get_string(str_slot);
		int fno = -1;
		if (name) {
			fno = ain_get_function(ain, name->text);
			WARNING("DG_STR_TO_METHOD: '%s' -> fno=%d (dg_type=%d)", display_sjis0(name->text), fno, get_argument(0));
		} else {
			WARNING("DG_STR_TO_METHOD: NULL string (slot=%d)", str_slot);
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
		// In delegate_call, obj (the delegate's object slot) is stored as
		// struct_page in the call frame. For lambdas, this is the enclosing
		// function's local page slot — the captured environment.
		stack_push(struct_page_slot());
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
		// X_ICAST struct_type: interface cast
		int struct_type = get_argument(0);
		// The value on stack is already the object; just leave it
		(void)struct_type;
		break;
	}
	case X_OP_SET: {
		// X_OP_SET n: like X_ASSIGN but for option types
		// stack = [..., heap_idx, page_idx, val0, ..., val(n-1)]
		// Pop n values, pop 2-slot var ref, write n consecutive slots, push n values
		int n = get_argument(0);
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
		// Periodically process SDL events and throttle CPU
		if (unlikely(insn_count % 10000 == 0)) {
			handle_events();
			// Yield CPU briefly to prevent 100% usage in tight loops
			if (insn_count % 100000 == 0)
				SDL_Delay(1);
			// Heap[0] corruption watchpoint
			if (unlikely(heap[0].type != VM_PAGE)) {
				static int h0_warn = 0;
				if (h0_warn++ < 3) {
					int fno = call_stack_ptr > 0 ? call_stack[call_stack_ptr-1].fno : -1;
					WARNING("HEAP0_CORRUPT: heap[0].type=%d ref=%d insn=%llu ip=0x%lX fno=%d '%s'",
						heap[0].type, heap[0].ref, insn_count, (unsigned long)instr_ptr,
						fno, (fno >= 0 && fno < ain->nr_functions) ? ain->functions[fno].name : "?");
				}
			}
		}
		trace_buf[trace_buf_idx].ip = instr_ptr;
		trace_buf[trace_buf_idx].op = opcode;
		trace_buf[trace_buf_idx].sp = stack_ptr;
		trace_buf_idx = (trace_buf_idx + 1) % TRACE_BUF_SIZE;
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
	(void)sig;
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
	signal(SIGABRT, SIG_DFL);
	raise(SIGABRT);
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

	WARNING("VM: alloc=%d, main=%d, nr_globals=%d, nr_functions=%d, version=%d",
		ain->alloc, ain->main, ain->nr_globals, ain->nr_functions, ain->version);
	if (ain->alloc >= 0) {
		WARNING("VM: alloc func '%s' address=0x%X",
			ain->functions[ain->alloc].name, ain->functions[ain->alloc].address);
	}
	if (ain->alloc >= 0 && ain->functions[ain->alloc].address != 0xFFFFFFFF) {
		WARNING("VM: calling alloc function...");
		vm_call(ain->alloc, -1); // function "0": allocate global arrays
		WARNING("VM: alloc done");
	} else {
		WARNING("VM: skipping alloc (address=0xFFFFFFFF or alloc<0)");
	}

	// XXX: global constructors must be called AFTER initializing non-struct variables
	//      otherwise a global set in a constructor will be clobbered by its initval
	WARNING("VM: initializing %d global structs...", ain->nr_globals);
	for (int i = 0; i < ain->nr_globals; i++) {
		if (ain->globals[i].type.data == AIN_STRUCT) {
			int slot = heap[0].page->values[i].i;
			if (slot > 0 && heap_index_valid(slot))
				init_struct(ain->globals[i].type.struc, slot);
		}
	}
	WARNING("VM: global structs done, calling main...");

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
