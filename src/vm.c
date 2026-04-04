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
#include <setjmp.h>
#include <assert.h>
#include <signal.h>
#include <execinfo.h>
#include <pthread.h>
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
#include "gfx/font.h"
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

// ---- v14 message system (MSG + R/A fallback when delegates empty) ----
// In v14, MSG opcode has no MSGF callback. The game expects delegates in
// global[103]/[104] to handle R (render line) / A (advance/wait for click).
// If those delegates are never registered (the AFL init chain is broken),
// we implement the display + wait at the engine level.
static int vm_msg_idx = -1;              // last MSG index
static struct string *vm_msg_text = NULL; // last MSG text (borrowed ref, do not free)
// Accumulated lines for the current page (R adds lines, A displays + waits)
#define VM_MSG_MAX_LINES 8
static char *vm_msg_lines[VM_MSG_MAX_LINES];
static int vm_msg_line_count = 0;
// Function numbers resolved at init
static int vm_msg_fn_R = -1;   // "R"
static int vm_msg_fn_A = -1;   // "A"

static void vm_msg_init(void)
{
	vm_msg_fn_R = ain_get_function(ain, "R");
	vm_msg_fn_A = ain_get_function(ain, "A");
	if (vm_msg_fn_R >= 0 && vm_msg_fn_A >= 0)
		NOTICE("v14 MSG fallback enabled (R=%d A=%d)", vm_msg_fn_R, vm_msg_fn_A);
}

// Check if delegate in global[slot] is empty
static bool vm_msg_delegate_empty(int slot)
{
	int dg = heap[global_page_slot].page->values[slot].i;
	if (dg <= 0 || (size_t)dg >= heap_size)
		return true;
	if (!heap[dg].page || heap[dg].page->nr_vars == 0)
		return true;
	return false;
}

// Draw dialogue overlay: semi-transparent box with text lines
static void vm_msg_draw_overlay(void)
{
	if (vm_msg_line_count <= 0)
		return;
	Texture *surface = gfx_main_surface();
	if (!surface)
		return;

	// Draw semi-transparent black box at bottom (matching game's style)
	int box_h = 160;
	int box_y = 720 - box_h;
	gfx_fill_with_alpha(surface, 0, box_y, 1280, box_h, 0, 0, 0, 180);

	// Render text lines
	struct text_style ts = {
		.face = FONT_GOTHIC,
		.size = 24,
		.bold_width = 0,
		.weight = FW_NORMAL,
		.color = { .r = 255, .g = 255, .b = 255, .a = 255 },
		.edge_color = { .r = 0, .g = 0, .b = 0, .a = 255 },
		.scale_x = 1.0f,
		.space_scale_x = 1.0f,
		.font_spacing = 0,
		.font_size = NULL,
	};
	text_style_set_edge_width(&ts, 1.5f);

	int y = box_y + 16;
	for (int i = 0; i < vm_msg_line_count; i++) {
		if (vm_msg_lines[i]) {
			gfx_render_text(surface, 40, y, vm_msg_lines[i], &ts, true);
		}
		y += 32;
	}
}

// Wait for left click or enter key, drawing overlay each frame
static void vm_msg_wait_click(void)
{
	key_clear_flag(false);

	uint32_t start = SDL_GetTicks();
	while (!key_is_down(VK_LBUTTON) && !key_is_down(VK_RETURN)) {
		handle_events();
		vm_msg_draw_overlay();
		gfx_swap();
		SDL_Delay(16);
		// Auto-advance after 500ms when msgskip is active (ctrl held)
		// or after a longer timeout for automated testing
		if (key_is_down(VK_CONTROL) && SDL_GetTicks() - start > 50)
			break;
	}
	// Wait for release
	while (key_is_down(VK_LBUTTON) || key_is_down(VK_RETURN)) {
		handle_events();
		SDL_Delay(8);
	}
}

static bool vm_msg_a_was_called = false;

// R handler: accumulate a line from the last MSG
static void vm_msg_handle_R(void)
{
	if (!vm_msg_text || vm_msg_text->size <= 0)
		return;
	// If A was previously called, this R starts a new page — clear old lines
	if (vm_msg_a_was_called) {
		for (int i = 0; i < vm_msg_line_count; i++) {
			free(vm_msg_lines[i]);
			vm_msg_lines[i] = NULL;
		}
		vm_msg_line_count = 0;
		vm_msg_a_was_called = false;
	}
	if (vm_msg_line_count < VM_MSG_MAX_LINES) {
		vm_msg_lines[vm_msg_line_count] = strdup(vm_msg_text->text);
		vm_msg_line_count++;
	}
	vm_msg_text = NULL;
	vm_msg_idx = -1;
}

// A handler (full): display accumulated lines, wait for click, then clear
static void vm_msg_handle_A(void)
{
	// Add the last line (MSG before A)
	if (vm_msg_text && vm_msg_text->size > 0 && vm_msg_line_count < VM_MSG_MAX_LINES) {
		vm_msg_lines[vm_msg_line_count] = strdup(vm_msg_text->text);
		vm_msg_line_count++;
	}
	vm_msg_text = NULL;
	vm_msg_idx = -1;

	if (vm_msg_line_count > 0) {
		vm_msg_wait_click();
		// Clear lines
		for (int i = 0; i < vm_msg_line_count; i++) {
			free(vm_msg_lines[i]);
			vm_msg_lines[i] = NULL;
		}
		vm_msg_line_count = 0;
	}
}

// A handler (overlay only): accumulate last line, don't wait
// (game's own WaitForClick handles the wait)
static void vm_msg_handle_A_overlay(void)
{
	if (vm_msg_text && vm_msg_text->size > 0 && vm_msg_line_count < VM_MSG_MAX_LINES) {
		vm_msg_lines[vm_msg_line_count] = strdup(vm_msg_text->text);
		vm_msg_line_count++;
	}
	vm_msg_text = NULL;
	vm_msg_idx = -1;
	vm_msg_a_was_called = true;
	// Lines will be drawn by vm_msg_draw_overlay() during gfx_swap
	// and cleared when the next R starts a new page
}

// Called from gfx_swap to render the text overlay
void vm_msg_render_overlay(void)
{
	vm_msg_draw_overlay();
}
// ---- end v14 message system ----

// v14 vtable dispatch: maps funcno → vtable index for virtual method resolution.
// Built at AIN load time; used by CALLMETHOD to resolve overrides.
// v14 vtable dispatch removed — bytecode handles it (see notes/language-update.md)

// Per-function flags, precomputed at init to avoid strstr in hot paths.
#define FUNC_FLAG_LAMBDA     0x01  // name contains "<lambda"
#define FUNC_FLAG_CDEBUG     0x02  // name contains "CDebug"
#define FUNC_FLAG_CASTIMER_MGR  0x04  // CASTimerManager methods
#define FUNC_FLAG_CASTIMER_INST 0x08  // CASTimer@Get, CASTimer@Reset, CASTimer@GetScaled
#define FUNC_FLAG_SKIP_TITLE    0x10  // RunResult<SceneTitle or Run<SceneLogo>
static uint8_t *func_flags = NULL;

#define STRUCT_FLAG_CDEBUG      0x01
#define STRUCT_FLAG_MEMBER_CTOR 0x02  // has member structs with no-arg constructors
static uint8_t *struct_flags = NULL;

static void init_func_flags(void)
{
	func_flags = xcalloc(ain->nr_functions, sizeof(uint8_t));
	for (int i = 0; i < ain->nr_functions; i++) {
		const char *name = ain->functions[i].name;
		if (!name) continue;
		if (ain->functions[i].is_lambda || strstr(name, "<lambda"))
			func_flags[i] |= FUNC_FLAG_LAMBDA;
		if (strstr(name, "CDebug"))
			func_flags[i] |= FUNC_FLAG_CDEBUG;
		// Only intercept CASTimerManager/CASTimer, NOT RCASTimerManager/RCASTimer.
		// RCASTimer uses bytecode delegates for time accumulation and must run
		// through the VM. Intercepting it breaks Motion system timers.
		if (strstr(name, "CASTimerManager") && !strstr(name, "RCASTimerManager"))
			func_flags[i] |= FUNC_FLAG_CASTIMER_MGR;
		else if ((strstr(name, "CASTimer@Get") || strstr(name, "CASTimer@Reset")
				|| strstr(name, "CASTimer@GetScaled"))
				&& !strstr(name, "RCASTimer"))
			func_flags[i] |= FUNC_FLAG_CASTIMER_INST;
		if (strstr(name, "RunResult<SceneTitle") || strstr(name, "Run<SceneLogo>"))
			func_flags[i] |= FUNC_FLAG_SKIP_TITLE;
	}
	struct_flags = xcalloc(ain->nr_structures, sizeof(uint8_t));
	for (int i = 0; i < ain->nr_structures; i++) {
		if (ain->structures[i].name && strstr(ain->structures[i].name, "CDebug"))
			struct_flags[i] |= STRUCT_FLAG_CDEBUG;
		// Check if any member struct needs singleton registration via its ctor.
		// Only flag structs whose member has a timer-like ctor (RCASTimer,
		// VariableTimer) that registers with a singleton manager.
		if (ain->version >= 14) {
			for (int m = 0; m < ain->structures[i].nr_members; m++) {
				if (ain->structures[i].members[m].type.data == AIN_STRUCT) {
					int ms = ain->structures[i].members[m].type.struc;
					if (ms >= 0 && ms < ain->nr_structures
					    && ain->structures[ms].constructor > 0
					    && ain->structures[ms].constructor < ain->nr_functions
					    && ain->functions[ain->structures[ms].constructor].nr_args == 0
					    && ain->structures[ms].name
					    && (strstr(ain->structures[ms].name, "RCASTimer")
					        || strstr(ain->structures[ms].name, "VariableTimer"))) {
						struct_flags[i] |= STRUCT_FLAG_MEMBER_CTOR;
						break;
					}
				}
			}
		}
	}
}

// Native CASTimer: intercept broken CASTimer method_call skips with real timers.
// CASTimerManager's struct page gets corrupted (LOCAL_PAGE instead of STRUCT_PAGE)
// during v14 init, causing all timer calls to be skipped and return 0.
#define CAS_TIMER_MAX 2048
static struct {
	struct timespec epoch;
	bool active;
} cas_timers[CAS_TIMER_MAX];
static int cas_next_handle = 1;
static int cas_timer_rate = 1;  // Time scale factor (integer; ITOF'd by script)

static bool native_cas_timer_intercept(int fno, int struct_page, union vm_value *ret)
{
	const char *fname = ain->functions[fno].name;
	if (!(func_flags[fno] & (FUNC_FLAG_CASTIMER_MGR | FUNC_FLAG_CASTIMER_INST)))
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
		if (strstr(fname, "Rate::get")) {
			ret->i = cas_timer_rate;
			return true;
		}
		if (strstr(fname, "Rate::set")) {
			// Read new rate from first argument in local page
			int pg_slot = call_stack[call_stack_ptr-1].page_slot;
			if (pg_slot > 0 && heap_index_valid(pg_slot) && heap[pg_slot].page
				&& heap[pg_slot].page->nr_vars > 0) {
				cas_timer_rate = heap[pg_slot].page->values[0].i;
			}
			ret->i = 0;
			return true;
		}
		// Other Manager methods: default return
		ret->i = 0;
		return true;
	}

	// CASTimer / CASTimerImp instance methods.
	// Negative struct_page = handle from CASTimerManager::GetObject (direct index).
	// Positive struct_page = heap page of embedded CASTimer struct (unique per instance).
	// Use abs(struct_page) % CAS_TIMER_MAX to give each instance a distinct slot.
	// Slot 0 is reserved as a safe fallback; handles start at 1.
	int timer_id = abs(struct_page) % CAS_TIMER_MAX;
	if (timer_id <= 0) timer_id = 1;

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
// Number of VM_RETURN frames on the call stack (excluding frame 0).
// Used to quickly check if we're inside a vm_call without scanning.
static int vm_call_depth = 0;

struct ain *ain;

// Accessors for page.c debug checks (avoids incomplete type)
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
	return heap[global_page_slot].page;
}

union vm_value global_get(int varno)
{
	return heap[global_page_slot].page->values[varno];
}

void global_set(int varno, union vm_value val, bool call_dtors)
{
	switch (ain->globals[varno].type.data) {
	case AIN_STRING:
	case AIN_STRUCT:
	case AIN_ARRAY_TYPE:
		if (heap[global_page_slot].page->values[varno].i > 0) {
			if (call_dtors)
				heap_unref(heap[global_page_slot].page->values[varno].i);
			else
				exit_unref(heap[global_page_slot].page->values[varno].i);
		}
	default:
		break;
	}
	heap[global_page_slot].page->values[varno] = val;
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
			WARNING("STACK UNDERFLOW: sp=%d fno=%d '%s'",
				stack_ptr,
				call_stack_ptr > 0 ? call_stack[call_stack_ptr-1].fno : -1,
				(call_stack_ptr > 0 && call_stack[call_stack_ptr-1].fno >= 0
				 && call_stack[call_stack_ptr-1].fno < ain->nr_functions)
				 ? ain->functions[call_stack[call_stack_ptr-1].fno].name : "?");
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
		dummy_var[0].i = 0;
		return dummy_var;
	}
	// Check heap type — must be VM_PAGE for page access
	if (unlikely(heap[heap_index].type != VM_PAGE)) {
		dummy_var[0].i = 0;
		return dummy_var;
	}
	if (unlikely(!heap[heap_index].page || page_index < 0 || page_index >= heap[heap_index].page->nr_vars)) {
		// OOB array/struct access — return dummy value (silently ignore)
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
	if (slot < 0 || (size_t)slot >= heap_size)
		return &EMPTY_STRING;
	if (heap[slot].type != VM_STRING || !heap[slot].s)
		return &EMPTY_STRING;
	struct string *s = heap[slot].s;
	// Guard against corrupted pointers (must be 4-byte aligned, valid range)
	if ((uintptr_t)s < 0x100000 || ((uintptr_t)s & 0x3) != 0) {
		heap[slot].s = NULL;
		return &EMPTY_STRING;
	}
	return s;
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
	vm_call_depth = 0;
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
	vm_call_depth = 0;
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
	// C stack overflow guard: check remaining stack space
	{
		volatile char stack_var;
		static void *stack_base = NULL;
		if (!stack_base) {
			pthread_t self = pthread_self();
			stack_base = pthread_get_stackaddr_np(self);
		}
		size_t stack_used = (size_t)((char*)stack_base - (char*)&stack_var);
		size_t stack_limit = 8 * 1024 * 1024; // 8MB default
		if (stack_used > stack_limit - 64*1024) { // 64KB safety margin
			static int cstack_warn = 0;
			if (cstack_warn++ < 5) {
				WARNING("_function_call: C stack nearly full! used=%zu/%zu fno=%d '%s' csp=%d ved=%d",
					stack_used, stack_limit, fno,
					(fno >= 0 && fno < ain->nr_functions) ? ain->functions[fno].name : "?",
					call_stack_ptr, 0);
			}
			return -1;
		}
	}
	if (unlikely(fno < 0 || fno >= ain->nr_functions)) {
		WARNING("_function_call: invalid fno=%d (nr_functions=%d)", fno, ain->nr_functions);
		return -1;
	}
	if (unlikely(call_stack_ptr >= 4090)) {
		static int cso_warn = 0;
		if (cso_warn++ < 3) {
			WARNING("_function_call: call stack overflow (csp=%d) fno=%d '%s'",
				call_stack_ptr, fno, ain->functions[fno].name);
			// Dump bottom 10 + top 30 frames with struct pages
			WARNING("=== Call stack (bottom 10 + top 30) ===");
			for (int ci = 0; ci < 10 && ci < call_stack_ptr; ci++) {
				int cfno = call_stack[ci].fno;
				WARNING("  [%d] fno=%d page=%d '%s'", ci, cfno,
					call_stack[ci].struct_page,
					(cfno >= 0 && cfno < ain->nr_functions) ? ain->functions[cfno].name : "?");
			}
			if (call_stack_ptr > 40)
				WARNING("  ... (%d frames omitted) ...", call_stack_ptr - 40);
			int start = call_stack_ptr > 30 ? call_stack_ptr - 30 : 0;
			if (start < 10) start = 10;
			for (int ci = start; ci < call_stack_ptr; ci++) {
				int cfno = call_stack[ci].fno;
				WARNING("  [%d] fno=%d page=%d '%s'", ci, cfno,
					call_stack[ci].struct_page,
					(cfno >= 0 && cfno < ain->nr_functions) ? ain->functions[cfno].name : "?");
			}
		}
		return -1;
	}
	struct ain_function *f = &ain->functions[fno];

	// Reject functions with invalid addresses (sentinel/unimplemented functions)
	if (unlikely(f->address >= ain->code_size)) {
		static int bad_addr = 0;
		if (bad_addr++ < 3) {
			int caller_fno = (call_stack_ptr > 0) ? call_stack[call_stack_ptr-1].fno : -1;
			WARNING("_function_call: invalid address 0x%lX for fno=%d '%s' (code_size=0x%lX)"
				" caller=%d '%s' ip=0x%lX",
				(unsigned long)f->address, fno, f->name, (unsigned long)ain->code_size,
				caller_fno,
				(caller_fno >= 0 && caller_fno < ain->nr_functions) ?
					ain->functions[caller_fno].name : "?",
				(unsigned long)instr_ptr);
		}
		return -1;
	}

	// Validate nr_vars
	if (unlikely(f->nr_vars < 0 || f->nr_vars > 100000)) {
		WARNING("_function_call: suspicious nr_vars=%d for fno=%d '%s'",
			f->nr_vars, fno, f->name);
		return -1;
	}

	int slot = heap_alloc_slot(VM_PAGE);
	struct page *new_page = alloc_page(LOCAL_PAGE, fno, f->nr_vars);
	if (!new_page) {
		WARNING("_function_call: alloc_page returned NULL for fno=%d '%s' nr_vars=%d",
			fno, f->name, f->nr_vars);
		return -1;
	}
	heap_set_page(slot, new_page);
	heap[slot].page->local.struct_ptr = -1;

	call_stack[call_stack_ptr++] = (struct function_call) {
		.fno = fno,
		.call_address = instr_ptr,
		.return_address = return_address,
		.page_slot = slot,
		.struct_page = -1,
		.base_sp = stack_ptr,  // default; callers may override after args
	};
	if (return_address == VM_RETURN && call_stack_ptr > 1)
		vm_call_depth++;
	// initialize local variables
	for (int i = f->nr_args; i < f->nr_vars; i++) {
		if (unlikely(f->vars[i].type.data < 0 || f->vars[i].type.data > 120)) {
			WARNING("_function_call: bad var type data=%d for fno=%d '%s' var[%d]",
				f->vars[i].type.data, fno, f->name, i);
			break;
		}
		union vm_value initv = variable_initval(f->vars[i].type.data);
		heap[slot].page->values[i] = initv;
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

// Check if a delegate argument type occupies 2 stack slots.
// v14 interface/option/iface_wrap types (and structs with is_interface flag) are
// 2-slot inline values [struct_page, vtable_offset] on the stack.
static bool delegate_arg_is_2slot(struct ain_type *type)
{
	switch (type->data) {
	case AIN_IFACE:
	case AIN_OPTION:
	case AIN_IFACE_WRAP:
		return true;
	case AIN_STRUCT:
		return type->struc >= 0 && type->struc < ain->nr_structures
		       && ain->structures[type->struc].is_interface;
	default:
		return false;
	}
}

// Count total stack slots for delegate parameters.
// Most args are 1 slot; v14 interface/option/iface_wrap types are 2 slots.
static int delegate_param_slots(struct ain_function_type *dg)
{
	int slots = 0;
	for (int i = 0; i < dg->nr_arguments; i++) {
		// v14: 2-slot args (interface/option) have a void companion slot
		// that's already part of the 2-slot count. Skip it.
		if (dg->variables[i].type.data == AIN_VOID)
			continue;
		slots += delegate_arg_is_2slot(&dg->variables[i].type) ? 2 : 1;
	}
	return slots;
}

// Return slot count for delegate return types.
// Unlike ain_return_slots_type, WRAP is always 2 here because delegate
// bytecode (DG_CALLBEGIN/DG_CALL) and the caller (e.g. X_ASSIGN 2)
// always handle WRAP as a 2-slot [value, has_value] pair.
static int delegate_return_slots(struct ain_type *type)
{
	if (type->data == AIN_WRAP)
		return 2;
	return ain_return_slots_type(type);
}

static void function_call(int fno, int return_address)
{
	int slot = _function_call(fno, return_address);
	if (unlikely(slot < 0))
		return;

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

	// traces removed

	// v14 lambda closure: when a lambda is called via CALLFUNC (not HLL),
	// set struct_page to the enclosing method's object page so PUSHSTRUCTPAGE works.
	// Without this, lambdas inside ArrayExtensions::Select etc. can't access
	// the enclosing class's members (e.g. ExTable.m_keyFormatName).
	if (ain->version >= 14 && call_stack_ptr >= 2
	    && (func_flags[fno] & FUNC_FLAG_LAMBDA)) {
		// Search up the call stack for the nearest frame with a valid struct_page
		for (int fr = call_stack_ptr - 2; fr >= 0; fr--) {
			if (call_stack[fr].struct_page > 0
			    && (size_t)call_stack[fr].struct_page < heap_size) {
				call_stack[call_stack_ptr-1].struct_page = call_stack[fr].struct_page;
				break;
			}
		}
	}
}

static void function_return(void);

static void method_call(int fno, int return_address)
{
	function_call(fno, return_address);

	if (ain->version >= 14) {
		// v14: struct_page stays on stack below base_sp.
		int struct_page = stack[stack_ptr - 1].i;

		// Skip CDebug methods — their struct state is corrupted (constructor skipped).
		if (func_flags[fno] & FUNC_FLAG_CDEBUG) {
			stack_pop(); // struct_page
			call_stack[call_stack_ptr-1].base_sp = stack_ptr;
			int ret_slots = ain_return_slots_type(&ain->functions[fno].return_type);
			for (int i = 0; i < ret_slots; i++)
				stack_push((union vm_value){.i = 0});
			function_return();
			return;
		}

		// Force-intercept CASTimerManager methods with native implementation.
		// The Manager's struct members are corrupted even when page type is
		// valid, so we must intercept BEFORE the script code runs.
		if (func_flags[fno] & FUNC_FLAG_CASTIMER_MGR) {
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

		// Force-intercept CASTimer instance methods (Get, GetScaled, Reset).
		// CASTimer@Get calls CASTimerManager@GetObject which returns a fake
		// negative struct_page, then calls func#432 on it → crash/garbage.
		// Intercept here: read timer handle from struct field0, compute natively.
		if (func_flags[fno] & FUNC_FLAG_CASTIMER_INST) {
			int handle = 0;
			if (struct_page > 0 && heap_index_valid(struct_page)
				&& heap[struct_page].page && heap[struct_page].page->nr_vars > 0) {
				handle = heap[struct_page].page->values[0].i;
			}
			// Auto-allocate timer handle for uninitialized RCASTimer/CASTimer
			// instances (handle=0). This happens when the CASTimerManager is
			// corrupt and CreateHandle was never properly called.
			if (handle <= 0 && struct_page > 0 && heap_index_valid(struct_page)
				&& heap[struct_page].page && heap[struct_page].page->nr_vars > 0) {
				int h = cas_next_handle++;
				if (h > 0 && h < CAS_TIMER_MAX) {
					clock_gettime(CLOCK_MONOTONIC, &cas_timers[h].epoch);
					cas_timers[h].active = true;
				}
				heap[struct_page].page->values[0].i = h;
				handle = h;
			}
			union vm_value native_ret = {.i = 0};
			native_cas_timer_intercept(fno, -(handle > 0 ? handle : 1), &native_ret);
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

		// v14 wrap unwrap: when struct_page is a wrap page (STRUCT_PAGE with
		// index==-1, nr_vars==1), follow the inner reference in values[0].
		// Wrap pages are created by variable_initval(AIN_WRAP) as a level
		// of indirection. Methods must be called on the inner struct, not
		// the wrap page itself.
		if (valid && heap[struct_page].page->index == -1
			&& heap[struct_page].page->nr_vars == 1) {
			int inner = heap[struct_page].page->values[0].i;
			if (inner > 0 && heap_index_valid(inner)
				&& heap[inner].type == VM_PAGE
				&& heap[inner].page
				&& heap[inner].page->type == STRUCT_PAGE) {
				struct_page = inner;
				stack[stack_ptr - 1].i = inner;
			} else {
				// Inner is null (-1) or invalid: auto-allocate inner struct
				// based on method's struct name.
				int stype = -1;
				const char *fname = ain->functions[fno].name;
				const char *at = fname ? strchr(fname, '@') : NULL;
				if (at && at > fname) {
					size_t len = at - fname;
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
					heap[struct_page].page->values[0].i = new_slot;
					struct_page = new_slot;
					stack[stack_ptr - 1].i = new_slot;
					}
				valid = struct_page > 0 && heap_index_valid(struct_page)
					&& heap[struct_page].type == VM_PAGE
					&& heap[struct_page].page
					&& heap[struct_page].page->type == STRUCT_PAGE;
			}
		}

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

			int ret_slots = ain_return_slots_type(&ain->functions[fno].return_type);
			for (int i = 0; i < ret_slots; i++) {
				if (i == 0 && intercepted)
					stack_push(native_ret);
				else if (i == 0 && ain->functions[fno].return_type.data == AIN_STRING)
					stack_push(vm_string_ref(string_ref(&EMPTY_STRING)));
				else
					// Slot 0 is a null guard page that safely absorbs writes
					stack_push((union vm_value){.i = 0});
			}
			function_return();
			return;
		}
		call_stack[call_stack_ptr-1].struct_page = struct_page;
		call_stack[call_stack_ptr-1].is_method = true;
		// base_sp stays as function_call set it (above struct_page)

		heap[call_stack[call_stack_ptr-1].page_slot].page->local.struct_ptr = struct_page;
		return;
	}

	// Pre-v14: pop struct_page and update base_sp
	int struct_page = stack_pop().i;
	bool valid = struct_page > 0 && heap_index_valid(struct_page)
		&& heap[struct_page].page && heap[struct_page].page->type == STRUCT_PAGE;
	if (unlikely(!valid)) {
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

		call_stack[call_stack_ptr-1].struct_page = obj;
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
						call_stack[call_stack_ptr-1].struct_page = call_stack[fr].struct_page;
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
		// Pop delegate arguments. Must pop exactly delegate_param_slots()
		// values to match what DG_CALLBEGIN pushed.
		{
			int arg_slots = delegate_param_slots(&ain->delegates[dg_no]);
			for (int s = 0; s < arg_slots; s++) {
				union vm_value v = stack_pop();
				// Simple fini: only free strings/pages that were pushed as args.
				// Ref types (AIN_REF_TYPE) don't own their slot — skip fini.
				// For 2-slot args, the second slot is metadata (vtoff/discriminant),
				// not a heap reference, so it needs no fini.
				(void)v;
			}
		}
		for (int i = 0; i < return_values; i++)
			stack_push(r[i]);
		instr_ptr = get_argument(1);
	}
}

void vm_call(int fno, int struct_page)
{
	if (unlikely(fno < 0 || fno >= ain->nr_functions)) return;
	if (unlikely(ain->functions[fno].address >= ain->code_size
			|| ain->functions[fno].address == 0xFFFFFFFF))
		return;
	size_t saved_ip = instr_ptr;
	unsigned long long saved_limit = vm_call_insn_limit;
	vm_call_insn_limit = 0;
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
	struct ain_function *f = &ain->functions[fno];
	// Skip sentinel functions (unimplemented/stub entries)
	if (unlikely(f->address >= ain->code_size || f->address == 0xFFFFFFFF))
		return;
	size_t saved_ip = instr_ptr;
	int slot = _function_call(fno, VM_RETURN);
	if (unlikely(slot < 0)) { instr_ptr = saved_ip; return; }
	call_stack[call_stack_ptr-1].base_sp = stack_ptr;

	// Set struct_page for lambda/method callbacks invoked from HLL functions.
	// For closures (lambdas), X_GETENV accesses struct_page to read captured
	// variables from the enclosing scope.
	extern int hll_func_obj;
	bool is_lambda = (func_flags[fno] & FUNC_FLAG_LAMBDA);
	if (is_lambda) {
		// Lambda closure: struct_page is the 'this' object (for PUSHSTRUCTPAGE),
		// env_page is the parent frame's local page (for X_GETENV closure access).
		if (hll_func_obj >= 0)
			call_stack[call_stack_ptr-1].struct_page = hll_func_obj;
		if (call_stack_ptr >= 2) {
			call_stack[call_stack_ptr-1].env_page = call_stack[call_stack_ptr-2].page_slot;
		}
	} else if (hll_func_obj >= 0) {
		// Non-lambda method callback: struct_page = the 'this' object
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

	// v14 2-valued types (AIN_IFACE, AIN_OPTION) push 2 slots on return.
	int expected_return = 0;
	if (fno >= 0 && fno < ain->nr_functions)
		expected_return = ain_return_slots_type(&ain->functions[fno].return_type);


	// Enforce stack balance: ensure exactly expected_return values above base_sp.
	bool is_dg = call_stack[call_stack_ptr-1].is_delegate_call;
	int delta = stack_ptr - base_sp;

	// Determine return type for enforcement decisions.
	enum ain_data_type ret_data = (fno >= 0 && fno < ain->nr_functions)
		? ain->functions[fno].return_type.data : AIN_INT;

	// AIN_WRAP: slot count varies and can't be reliably determined from type alone.
	// For non-delegate calls, skip enforcement and trust the bytecode.
	bool skip_enforce = (ret_data == AIN_WRAP && !is_dg);

	// For delegate-called functions: enforce stack balance to match the
	// delegate's expected return count (stored in dg_return_slots).
	// This handles WRAP mismatches: the function may push 2 values but
	// the delegate framework needs exactly dg_return_slots values.
	if (is_dg) {
		int dg_ret = call_stack[call_stack_ptr-1].dg_return_slots;
		if (dg_ret > 0)
			expected_return = dg_ret;
	}

	if (is_dg && delta != expected_return && delta >= 0) {
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

	if (!is_dg && !skip_enforce && delta > expected_return) {
		// Too many values: save return value(s) and pop extras.
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
	if (ret_addr == VM_RETURN && call_stack_ptr > 1)
		vm_call_depth--;
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
		static int sys_exit_count = 0;
		sys_exit_count++;
		if (sys_exit_count <= 5)
			WARNING("SYS_EXIT(%d) suppressed (call #%d): caller fno=%d '%s'",
				exit_code, sys_exit_count,
				(call_stack_ptr > 0) ? call_stack[call_stack_ptr-1].fno : -1,
				(call_stack_ptr > 0 && call_stack[call_stack_ptr-1].fno >= 0
				 && call_stack[call_stack_ptr-1].fno < ain->nr_functions
				 && ain->functions[call_stack[call_stack_ptr-1].fno].name)
				? ain->functions[call_stack[call_stack_ptr-1].fno].name : "?");
		// Don't actually exit — game may recover from assertion failures
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
		int len = strlen(path);
		// Return true for directories, but false if the path ends with a directory separator.
		if (len > 0 && (path[len-1] == '/' || path[len-1] == '\\')) {
			stack_push(0);
		} else {
			stack_push(file_exists(path));
		}
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
		scene_render();
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
		if (s->cases[i].value == val)
			return s->cases[i].address;
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

static inline __attribute__((always_inline)) enum opcode execute_instruction(enum opcode opcode)
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
		stack_push(global_page_slot);
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
		// Peek at var ref for type-aware ref counting
		int32_t assign_pidx = stack_peek(0).i;
		int32_t assign_hidx = stack_peek(1).i;
		union vm_value *assign_var = stack_pop_var();
		if (assign_var && assign_var != dummy_var &&
		    heap_index_valid(assign_hidx) && heap[assign_hidx].type == VM_PAGE &&
		    heap[assign_hidx].page) {
			struct page *assign_page = heap[assign_hidx].page;
			enum ain_data_type assign_vtype = variable_type(assign_page, assign_pidx, NULL, NULL);
			switch (assign_vtype) {
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
			case AIN_IFACE: {
				int old_slot = assign_var[0].i;
				if (val.i > 0 && heap_index_valid(val.i))
					heap_ref(val.i);
				if (old_slot > 0 && heap_index_valid(old_slot))
					heap_unref(old_slot);
				break;
			}
			default:
				break;
			}
		}
		assign_var[0] = val;
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
			int orig_ctor = ctor_func;  // track original bytecode value
			// Determine effective constructor BEFORE allocating.
			// v14 alloc phase: NEW x, -1 means allocate only (init_global_struct_v14
			// will call constructors later). Outside alloc phase: -1 means "no explicit
			// ctor in bytecode" — use the STRT-defined one.
			{
				int strt_ctor = (struct_type >= 0 && struct_type < ain->nr_structures)
					? ain->structures[struct_type].constructor : -99;
				bool explicit_no_ctor = (ain->version >= 14 && ctor_func == -1
							 && vm_in_alloc_phase);
				bool allow = (ain->version >= 14) || !vm_in_alloc_phase;
				if (ctor_func <= 0 && strt_ctor > 0 && allow && !explicit_no_ctor) {
					ctor_func = strt_ctor;
				}
				}
			// Allocate the struct.
			// v14 alloc phase: alloc_struct only (init_global_struct_v14 handles ctors).
			// Otherwise: create_struct which runs init_struct for child member pages.
			// Constructors may override members, but having them pre-initialized
			// prevents null-page issues during construction.
			if (ain->version >= 14 && vm_in_alloc_phase) {
				v.i = alloc_struct(struct_type);
			} else {
				create_struct(struct_type, &v);
			}
			// v14: call no-arg STRT constructors for member structs.
			// alloc_struct creates nested struct pages but doesn't call
			// constructors (e.g. RCASTimer@0 which registers with its
			// manager singleton). Only check struct types that have
			// members needing construction (precomputed in struct_flags).
			if (ain->version >= 14 && struct_type >= 0
			    && struct_type < ain->nr_structures
			    && (struct_flags[struct_type] & STRUCT_FLAG_MEMBER_CTOR)
			    && v.i > 0 && heap_index_valid(v.i) && heap[v.i].page) {
				struct ain_struct *s = &ain->structures[struct_type];
				for (int mi = 0; mi < s->nr_members; mi++) {
					if (s->members[mi].type.data == AIN_STRUCT) {
						int mst = s->members[mi].type.struc;
						if (mst >= 0 && mst < ain->nr_structures
						    && ain->structures[mst].constructor > 0
						    && ain->functions[ain->structures[mst].constructor].nr_args == 0) {
							int member_slot = heap[v.i].page->values[mi].i;
							if (member_slot > 0 && heap_index_valid(member_slot)) {
								heap_ref(member_slot);
								vm_call(ain->structures[mst].constructor, member_slot);
								if (heap_index_valid(member_slot) && heap[member_slot].ref > 1)
									heap_unref(member_slot);
							}
						}
					}
				}
			}
			if (ctor_func > 0 && ain->version >= 14) {
				// Skip known-broken constructors (debug features with corrupted struct state)
				if (struct_type >= 0 && struct_type < ain->nr_structures
				    && (struct_flags[struct_type] & STRUCT_FLAG_CDEBUG)) {
					stack_push(v);
					break;
				}
				int nr_ctor_args = ain->functions[ctor_func].nr_args;
				// Case 1: Resolved from bytecode -1 → STRT constructor (no args on stack).
				// Use vm_call for clean stack management — it pushes struct_page,
				// method_call sets up the frame, function_return consumes struct_page.
				// Net stack effect: zero. Then we push v as the NEW result.
				if (orig_ctor == -1) {
					heap_ref(v.i);
					vm_call(ctor_func, v.i);
					if (v.i > 0 && (size_t)v.i < heap_size && heap[v.i].ref > 1)
						heap_unref(v.i);
					stack_push(v);
					break;
				}
				// Case 2: Explicit constructor in bytecode — args are on the stack.
				// Save args, insert struct_page below, call via method_call.
				union vm_value saved[64];
				for (int i = nr_ctor_args - 1; i >= 0; i--) {
					saved[i] = stack_pop();
				}
				stack_push(v);
				for (int i = 0; i < nr_ctor_args; i++) {
					stack_push(saved[i]);
				}
				heap_ref(v.i);
				size_t saved_ip = instr_ptr;
				method_call(ctor_func, VM_RETURN);
				vm_execute();
				instr_ptr = saved_ip;
				if (v.i > 0 && (size_t)v.i < heap_size && heap[v.i].ref > 1) {
					heap_unref(v.i);
				}
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
		// v14 MSG: let the game's native R and A functions handle
		// message display.  The engine overlay is kept as a visual
		// fallback (drawn during gfx_swap) but does not block or
		// skip any game code.
		if (ain->version >= 14 && ain->msgf < 0 && vm_msg_fn_R >= 0) {
			if (_fno == vm_msg_fn_R) {
				vm_msg_handle_R();
				// Let game's R run too (it does window management)
			} else if (_fno == vm_msg_fn_A) {
				// Clear overlay lines so they don't persist over
				// the native message window.
				for (int _i = 0; _i < vm_msg_line_count; _i++)
					free_string(vm_msg_lines[_i]);
				vm_msg_line_count = 0;
				// Let game's A run normally
			}
		}
		// --skip-title: bypass SceneLogo and SceneTitle
		if (config.skip_title && _fno >= 0 && _fno < ain->nr_functions
				&& ain->functions[_fno].name) {
			bool _skip = false;
			int _retval = 0;
			bool _has_ret = false;
			if (func_flags[_fno] & FUNC_FLAG_SKIP_TITLE) {
				if (strstr(ain->functions[_fno].name, "RunResult<SceneTitle")) {
					_skip = true; _has_ret = true; _retval = 0;
				} else {
					_skip = true; _has_ret = false;
				}
			}
			if (_skip) {
				// Pop arguments that were pushed for this call
				struct ain_function *_f = &ain->functions[_fno];
				for (int _a = _f->nr_args - 1; _a >= 0; _a--) {
					stack_pop();
					// 2-slot types need an extra pop
					switch (_f->vars[_a].type.data) {
					case AIN_REF_INT: case AIN_REF_FLOAT: case AIN_REF_BOOL:
					case AIN_REF_LONG_INT: case AIN_REF_STRING:
					case AIN_REF_STRUCT: case AIN_REF_ARRAY_TYPE:
					case AIN_REF_DELEGATE: case AIN_REF_HLL_PARAM:
					case AIN_IFACE: case AIN_IFACE_WRAP: case AIN_OPTION:
						stack_pop(); break;
					default: break;
					}
				}
				if (_has_ret)
					stack_push((union vm_value){.i = _retval});
				instr_ptr += instruction_width(CALLFUNC);
				break;
			}
			// ADV scenes now handled by ADVEngine stub (NumofStep=0 → empty loop)
		}
		if (_fno >= 0 && _fno < ain->nr_functions
				&& (ain->functions[_fno].address == 0xFFFFFFFF
				    || ain->functions[_fno].address >= ain->code_size)) {
			// Sentinel function: skip call, push default return value.
			// Cannot use function_call + function_return because
			// _function_call rejects sentinels → function_return
			// would unwind the wrong frame.
			int ret = ain_return_slots_type(&ain->functions[_fno].return_type);
			for (int i = 0; i < ret; i++)
				stack_push((union vm_value){.i = (i == 0) ? -1 : 0});
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
			// Sentinel function: same as CALLFUNC above.
			int ret = ain_return_slots_type(&ain->functions[_fno2].return_type);
			for (int i = 0; i < ret; i++)
				stack_push((union vm_value){.i = (i == 0) ? -1 : 0});
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
			// v14: bytecode already resolves vtable dispatch via
			// X_REF/ADD instructions before CALLMETHOD. No VM-level
			// vtable lookup needed — doing it here would double-dispatch.
			// Handle nargs vs function's nr_args mismatch
			if (funcno >= 0 && funcno < ain->nr_functions) {
				// Skip sentinel functions (address == code_size or 0xFFFFFFFF).
				if (ain->functions[funcno].address >= ain->code_size
				    || ain->functions[funcno].address == 0xFFFFFFFF) {
					if (saved_args != small_args) free(saved_args);
					stack_pop(); // struct_page
					instr_ptr += instruction_width(CALLMETHOD);
					// Infer expected return slots from upcoming bytecode
					// (the caller's expectations, not the sentinel's return type).
					// struct_page already consumed; scan for patterns that
					// consume return values:
					//   SP_INC → 2 slots (ref + void)
					//   X_MOV followed by X_ASSIGN → X_ASSIGN's n value
					//   POP → skip (ambiguous: could be for return value
					//     or for a pre-call X_DUP'd value; safer to assume void)
					{
						int ret_slots = 0;
						int prev_op = -1;
						for (int scan = instr_ptr; scan + 2 <= (int)ain->code_size && scan < instr_ptr + 80; ) {
							int16_t op = LittleEndian_getW(ain->code, scan);
							if (op == POP && prev_op != X_ASSIGN) {
								// Don't infer return slots from POP for
								// CALLMETHOD sentinels.  POP after a void
								// method often pops a value that was X_DUP'd
								// before the call, not a return value.
								break;
							} else if (op == SP_INC) {
								ret_slots = 2;
								break;
							} else if (op == X_ASSIGN && scan + 6 <= (int)ain->code_size) {
								if (prev_op == X_MOV) {
									// X_MOV + X_ASSIGN = return value consumption
									ret_slots = LittleEndian_getDW(ain->code, scan + 2);
									if (ret_slots < 0 || ret_slots > 4) ret_slots = 0;
									break;
								}
								// X_ASSIGN not after X_MOV = cleanup, continue scanning
							} else if (op == JUMP || op == IFZ || op == IFNZ
								|| op == RETURN || op == CALLFUNC || op == CALLMETHOD) {
								// Control flow: stop scanning
								break;
							}
							prev_op = op;
							scan += instruction_width(op);
						}
						for (int i = 0; i < ret_slots; i++)
							stack_push((union vm_value){.i = 0});
					}
					break;
				}
				// v14: bytecode already resolves vtable dispatch via
			// REFREF/REF/ADD instructions. No VM-level vtable lookup needed.
			// (See notes/language-update.md: interface call convention)
		// --skip-title: intercept SceneLogo and SceneTitle via CALLMETHOD too
				if (config.skip_title && (func_flags[funcno] & FUNC_FLAG_SKIP_TITLE)) {
					const char *fn = ain->functions[funcno].name;
					if (strstr(fn, "RunResult<SceneTitle")) {
						NOTICE("--skip-title: CALLMETHOD skipping %s (NewGame)", fn);
						if (saved_args != small_args) free(saved_args);
						stack_pop(); // struct_page
						instr_ptr += instruction_width(CALLMETHOD);
						stack_push((union vm_value){.i = 0});
						break;
					}
					// Must be Run<SceneLogo>
					{
						// Void function — do NOT push a return value.
						NOTICE("--skip-title: CALLMETHOD skipping %s (void)", fn);
						if (saved_args != small_args) free(saved_args);
						stack_pop(); // struct_page
						instr_ptr += instruction_width(CALLMETHOD);
						break;
					}
				}
				// Early null struct_page check: if the object reference is
				// -1 (null), skip the call entirely.  Only catch truly null
				// references; sp>=0 may need method_call's auto-alloc.
				{
					int sp_chk = stack_peek(0).i;
					if (sp_chk < 0) {
						if (saved_args != small_args) free(saved_args);
						stack_pop(); // struct_page
						instr_ptr += instruction_width(CALLMETHOD);
						// Push correct return slots based on function's return type.
						// Use -1 for the first slot of reference types (null ref),
						// 0 for everything else.
						{
							int ret_slots = ain_return_slots_type(&ain->functions[funcno].return_type);
							enum ain_data_type rtype = ain->functions[funcno].return_type.data;
							for (int i = 0; i < ret_slots; i++) {
								int null_val = 0;
								if (i == 0) {
									switch (rtype) {
									case AIN_STRUCT: case AIN_IFACE: case AIN_WRAP:
									case AIN_OPTION: case AIN_IFACE_WRAP: case AIN_STRING:
									case AIN_ARRAY_TYPE: case AIN_ARRAY: case AIN_DELEGATE:
										null_val = -1;
										break;
									default:
										break;
									}
								}
								stack_push((union vm_value){.i = null_val});
							}
						}
						break;
					}
				}
				int fnr_args = ain->functions[funcno].nr_args;
				if (nargs != fnr_args) {
					// nargs mismatch: vtable resolved to a wrong function.
					if (saved_args != small_args) free(saved_args);
					stack_pop(); // struct_page
					instr_ptr += instruction_width(CALLMETHOD);
					// Infer expected return slots from next opcode rather than
					// using the wrong function's return type (which may differ
					// from the intended function).
					{
						int ret_slots = 0;
						int pv_op = -1;
						for (int scan = instr_ptr; scan + 2 <= (int)ain->code_size && scan < instr_ptr + 80; ) {
							int16_t op2 = LittleEndian_getW(ain->code, scan);
							if (op2 == POP && pv_op != X_ASSIGN) {
								// POP after sentinel CALLMETHOD is ambiguous;
								// assume void (see sentinel path comment above)
								break;
							} else if (op2 == SP_INC) {
								ret_slots = 2;
								break;
							} else if (op2 == X_ASSIGN && scan + 6 <= (int)ain->code_size) {
								if (pv_op == X_MOV) {
									ret_slots = LittleEndian_getDW(ain->code, scan + 2);
									if (ret_slots < 0 || ret_slots > 4) ret_slots = 0;
									break;
								}
							} else if (op2 == JUMP || op2 == IFZ || op2 == IFNZ
								|| op2 == RETURN || op2 == CALLFUNC || op2 == CALLMETHOD) {
								break;
							}
							pv_op = op2;
							scan += instruction_width(op2);
						}
						for (int i = 0; i < ret_slots; i++)
							stack_push((union vm_value){.i = 0});
					}
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
				// Invalid funcno: skip call.
				// Scan forward for return-value-consuming pattern.
				if (saved_args != small_args) free(saved_args);
				stack_pop(); // struct_page
				instr_ptr += instruction_width(CALLMETHOD);
				int ret_slots = 0;
				{
					int pv = -1;
					for (int scan = instr_ptr; scan + 2 <= (int)ain->code_size && scan < instr_ptr + 80; ) {
						int16_t op3 = LittleEndian_getW(ain->code, scan);
						if (op3 == POP && pv != X_ASSIGN) {
							// POP after sentinel CALLMETHOD is ambiguous;
							// assume void (see sentinel path comment above)
							break;
						} else if (op3 == SP_INC) {
							ret_slots = 2;
							break;
						} else if (op3 == X_ASSIGN && scan + 6 <= (int)ain->code_size) {
							if (pv == X_MOV) {
								ret_slots = LittleEndian_getDW(ain->code, scan + 2);
								if (ret_slots < 0 || ret_slots > 4) ret_slots = 0;
								break;
							}
						} else if (op3 == JUMP || op3 == IFZ || op3 == IFNZ
							|| op3 == RETURN || op3 == CALLFUNC || op3 == CALLMETHOD) {
							break;
						}
						pv = op3;
						scan += instruction_width(op3);
					}
				}
				for (int i = 0; i < ret_slots; i++)
					stack_push((union vm_value){.i = 0});
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
		// Check for pending ResumeLoad (deferred from system.ResumeLoad HLL).
		// vm_load_image replaces the entire VM state; must be called AFTER
		// hll_call completes its write-back and cleanup.
		extern char *resume_load_key_pending, *resume_load_path_pending;
		if (resume_load_key_pending) {
			char *key = resume_load_key_pending;
			char *path = resume_load_path_pending;
			resume_load_key_pending = NULL;
			resume_load_path_pending = NULL;
				vm_load_image(key, path);
			free(key);
			free(path);
			// vm_load_image restored instr_ptr to the save point's CALLHLL addr.
			// ip_inc (14 for CALLHLL v11+) will advance to the next instruction.
			// Push bool false (0) as ResumeSave's return value (= "loaded").
			stack_push(0);
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
		int msg_idx = get_argument(0);
		if (config.echo)
			echo_message(msg_idx);
		if (ain->msgf < 0) {
			// v14: store message for R/A handlers to use
			vm_msg_idx = msg_idx;
			vm_msg_text = (msg_idx >= 0 && msg_idx < ain->nr_messages)
				? ain->messages[msg_idx] : NULL;
			instr_ptr += instruction_width(_MSG);
			break;
		}
		stack_push(msg_idx);
		stack_push(ain->nr_messages);
		stack_push_string(string_ref(ain->messages[msg_idx]));
		function_call(ain->msgf, instr_ptr + instruction_width(_MSG));
		break;
	}
	case JUMP: { // ADDR
		instr_ptr = get_argument(0);
		break;
	}
	case IFZ: { // ADDR
		int ifz_val = stack_pop().i;
		if (!ifz_val)
			instr_ptr = get_argument(0);
		else
			instr_ptr += instruction_width(IFZ);
		break;
	}
	case IFNZ: { // ADDR
		int ifnz_val = stack_pop().i;
		if (ifnz_val)
			instr_ptr = get_argument(0);
		else
			instr_ptr += instruction_width(IFNZ);
		break;
	}
	case SWITCH: {
		int sw_no = get_argument(0);
		int sw_val = stack_pop().i;
		instr_ptr = get_switch_address(sw_no, sw_val);
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
			struct string *file_s = heap_get_string(file);
			struct string *expr_s = heap_get_string(expr);
			if (assert_count++ < 3) {
				WARNING("ASSERT FAILED: %s:%d: %s",
					file_s ? file_s->text : "?", line,
					expr_s ? expr_s->text : "?");
				sys_message("Assertion failed at %s:%d: %s\n",
						display_sjis0(file_s ? file_s->text : "?"),
						line,
						display_sjis1(expr_s ? expr_s->text : "?"));
			} else if (assert_count == 4) {
				WARNING("(suppressing further assert warnings)");
			}
			// v14 asserts: continue execution (non-fatal)
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
		struct string *sn1 = stack_peek_string(1);
		struct string *sn0 = stack_peek_string(0);
		bool noteq = (sn1 && sn0) ? !!strcmp(sn1->text, sn0->text) : (sn1 != sn0);
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
			} else if (ain->version >= 14 && p && p->type == STRUCT_PAGE
				   && p->index == -1 && p->nr_vars >= 1) {
				// v14 wrap<T> box: unwrap to get inner value
				int inner = p->values[0].i;
				if (inner > 0 && (size_t)inner < heap_size
				    && heap[inner].ref > 0 && heap[inner].type == VM_STRING) {
					stack_push_string(string_ref(
						heap[inner].s ? heap[inner].s : &EMPTY_STRING));
				} else if (inner > 0 && (size_t)inner < heap_size
					   && heap[inner].ref > 0) {
					heap_ref(inner);
					stack_push(inner);
				} else {
					stack_push(inner);
				}
			} else if (ain->version >= 14 && p && p->type == DELEGATE_PAGE) {
				// v14 delegates: copy semantics (like pre-v14).
				// Game code expects that copying a delegate to a local
				// produces an independent copy, so that clearing the
				// original doesn't destroy the copy.
				int slot = heap_alloc_slot(VM_PAGE);
				heap_set_page(slot, copy_page(p));
				stack_push(slot);
			} else if (ain->version >= 14) {
				// v14: reference semantics for structs/arrays
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
		int _fno = get_argument(1);
		if (_fno >= 0 && _fno < ain->nr_functions
				&& (ain->functions[_fno].address == 0xFFFFFFFF
					|| ain->functions[_fno].address >= ain->code_size)) {
			int ret = ain_return_slots_type(&ain->functions[_fno].return_type);
			for (int i = 0; i < ret; i++)
				stack_push((union vm_value){.i = (i == 0) ? -1 : 0});
			instr_ptr += instruction_width(SH_STRUCTREF_CALLMETHOD_NO_PARAM);
			break;
		}
		int memb_page = member_get(get_argument(0)).i;
		function_call(_fno, instr_ptr + instruction_width(SH_STRUCTREF_CALLMETHOD_NO_PARAM));
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
		int _fno2c = get_argument(2);
		if (_fno2c >= 0 && _fno2c < ain->nr_functions
				&& (ain->functions[_fno2c].address == 0xFFFFFFFF
					|| ain->functions[_fno2c].address >= ain->code_size)) {
			int ret = ain_return_slots_type(&ain->functions[_fno2c].return_type);
			for (int i = 0; i < ret; i++)
				stack_push((union vm_value){.i = (i == 0) ? -1 : 0});
			instr_ptr += instruction_width(SH_STRUCTREF2_CALLMETHOD_NO_PARAM);
			break;
		}
		int memb1 = member_get(get_argument(0)).i;
		int memb2 = page_get_var(heap_get_page(memb1), get_argument(1)).i;
		function_call(_fno2c, instr_ptr + instruction_width(SH_STRUCTREF2_CALLMETHOD_NO_PARAM));
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
		int _fno_tc = get_argument(0);
		if (_fno_tc >= 0 && _fno_tc < ain->nr_functions
				&& (ain->functions[_fno_tc].address == 0xFFFFFFFF
					|| ain->functions[_fno_tc].address >= ain->code_size)) {
			int ret = ain_return_slots_type(&ain->functions[_fno_tc].return_type);
			for (int i = 0; i < ret; i++)
				stack_push((union vm_value){.i = (i == 0) ? -1 : 0});
			instr_ptr += instruction_width(THISCALLMETHOD_NOPARAM);
			break;
		}
		int this_page = struct_page_slot();
		function_call(_fno_tc, instr_ptr + instruction_width(THISCALLMETHOD_NOPARAM));
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
		// v14: Do NOT resolve JUMP+FUNC thunks here.  The method's
		// body lives at the JUMP target and may read struct members,
		// create inner delegates, etc.  Resolving to the inner lambda
		// bypasses the outer method's logic and breaks delegate dispatch.
		if (fun < 0 && ain->version >= 14) {
			// v14: null method name → create empty delegate.
			// Game code checks Delegate.Empty() and skips null delegates.
			int slot = heap_alloc_slot(VM_PAGE);
			heap_set_page(slot, NULL);
			stack_push(slot);
			break;
		}
		int env = 0;
		if (ain->version >= 14) {
			env = local_page_slot();
			heap_ref(env);
			if (obj > 0)
				heap_ref(obj);
		}
		if (obj == -1 && ain->version >= 14) {
			obj = local_page_slot();
			heap_ref(obj);
		}
		int slot = heap_alloc_page(delegate_new_from_method_env(obj, fun, env));
		stack_push(slot);
		break;
	}
	case DG_CALLBEGIN: { // DG_TYPE
		int dg_no = get_argument(0);
		if (dg_no < 0 || dg_no >= ain->nr_delegates)
			VM_ERROR("Invalid delegate index");
		struct ain_function_type *dg = &ain->delegates[dg_no];
		// Stack before: [dg_page, arg_slot0, ..., arg_slotN]
		// Stack after:  [arg_slot0, ..., arg_slotN, dg_page, 0(dg_index)]
		int arg_slots = delegate_param_slots(dg);
		int dg_page = stack_peek(arg_slots).i;
		for (int i = 0; i < arg_slots; i++) {
			int pos = (stack_ptr - arg_slots) + i;
			stack[pos-1] = stack[pos];
		}
		stack[stack_ptr-1].i = dg_page;
		stack_push(0);

		// XXX: If the delegate has a return value, we push dummy value(s)
		//      so that DG_CALL can replace them.
		//      v14 2-slot types (AIN_IFACE, AIN_OPTION, etc.) need 2 slots.
		{
			int ret_slots = delegate_return_slots(&dg->return_type);
			for (int i = 0; i < ret_slots; i++)
				stack_push(0);
		}
		break;
	}
	case DG_NEW: {
		// DG_NEW: create empty delegate, push slot.
		// v14: use a proper DELEGATE_PAGE (nr_vars=0) instead of NULL page.
		// heap_get_delegate_page rejects non-DELEGATE_PAGE, so NULL would
		// cause delegate operations (Empty, numof, assign) to malfunction.
		int slot = heap_alloc_slot(VM_PAGE);
		heap_set_page(slot, alloc_page(DELEGATE_PAGE, 0, 0));
		stack_push(slot);
		break;
	}
	case DG_STR_TO_METHOD: {
		// Resolve method name string to function index
		// v14: stack has [obj_page, string_slot], arg = delegate type
		int str_slot = stack_pop().i;
		struct string *name = heap_get_string(str_slot);

		int fno = -1;
		if (name && name->size > 0) {
			fno = ain_get_function(ain, name->text);
			if (fno < 0 && ain->version >= 14) {
				int obj_page = stack_peek(0).i;
				if (obj_page <= 0) {
					for (int fr = call_stack_ptr - 1; fr >= 0 && obj_page <= 0; fr--) {
						if (call_stack[fr].struct_page > 0)
							obj_page = call_stack[fr].struct_page;
					}
				}
				if (obj_page > 0 && heap_index_valid(obj_page) && heap[obj_page].page
				    && heap[obj_page].page->type == STRUCT_PAGE) {
					int sidx = heap[obj_page].page->index;
					if (sidx >= 0 && sidx < ain->nr_structures) {
						char buf[512];
						snprintf(buf, sizeof(buf), "%s@%s",
							ain->structures[sidx].name, name->text);
						fno = ain_get_function(ain, buf);
						if (fno < 0 && ain->structures[sidx].nr_members > 0
						    && ain->structures[sidx].members[0].type.data == AIN_WRAP) {
							int parent = ain->structures[sidx].members[0].type.struc;
							if (parent >= 0 && parent < ain->nr_structures) {
								snprintf(buf, sizeof(buf), "%s@%s",
									ain->structures[parent].name, name->text);
								fno = ain_get_function(ain, buf);
							}
						}
					}
				}
			}
			// fno may be -1 if the method wasn't found; caller handles it.
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
		// X_REF n: dereference a 2-slot variable reference and push n consecutive values.
		// n=0 is valid: just consume the 2-slot reference (discard without reading).
		int n = get_argument(0);
		if (n < 0) n = 0;
		if (n == 0) {
			stack_pop(); // var_idx
			stack_pop(); // heap_idx
			break;
		}
		int32_t var_idx = stack_pop().i;
		int32_t heap_idx = stack_pop().i;
		struct page *page = NULL;
		// heap[0] is the guard page — never read from it (it absorbs
		// stray writes and contains garbage data).
		if (heap_idx > 0 && (size_t)heap_idx < heap_size && heap[heap_idx].ref > 0
		    && heap[heap_idx].type == VM_PAGE)
			page = heap[heap_idx].page;
		// v14: auto-unwrap wrap<T> boxes
		// If heap_idx points to a wrap box (STRUCT_PAGE, index==-1),
		// dereference to the inner value and read from that page instead.
		if (ain->version >= 14 && page && page->type == STRUCT_PAGE
		    && page->index == -1 && page->nr_vars >= 1) {
			int inner = page->values[0].i;
			page = NULL;
			if (inner >= 0 && (size_t)inner < heap_size
			    && heap[inner].ref > 0 && heap[inner].type == VM_PAGE)
				page = heap[inner].page;
		}
		if (page && var_idx >= 0 && var_idx + n <= page->nr_vars) {
			// read from resolved page
			for (int i = 0; i < n; i++) {
				stack_push(page->values[var_idx + i]);
			}
			// v14: track null-array source for FFI ref-array write-back.
			// When X_REF pushes a null value (<=0) from a struct member,
			// save the source so FFI can allocate a slot and write it back.
			// Both -1 (uninitialized) and 0 (guard page) are invalid array refs.
			if (ain->version >= 14 && n == 1 && page->values[var_idx].i <= 0
			    && page->type == STRUCT_PAGE) {
				extern int xref_null_src_page;
				extern int xref_null_src_var;
				xref_null_src_page = heap_idx;
				xref_null_src_var = var_idx;
			}
		} else if (ain->version >= 14 && page && page->type == STRUCT_PAGE
			   && page->index >= 0 && page->index < ain->nr_structures
			   && n > 0 && var_idx >= page->nr_vars) {
			// v14 virtual method table lookup.
			// var_idx >= nr_vars means it's an interface method call.
			// method_idx is relative to the struct's own fields:
			//   method_idx = var_idx - nr_vars
			// Note: page->values[0].i is the vtable ARRAY heap ref (a large
			// heap index), NOT a vtbase offset — do not subtract it.
			struct ain_struct *s = &ain->structures[page->index];
			int method_idx = var_idx - page->nr_vars;
			if (s->nr_vmethods > 0 && method_idx >= 0
			    && method_idx + n <= s->nr_vmethods) {
				for (int i = 0; i < n; i++)
					stack_push((union vm_value){.i = s->vmethods[method_idx + i]});
			} else if (s->nr_vmethods > 0 && var_idx >= 0
				   && var_idx + n <= s->nr_vmethods) {
				// Fallback: var_idx directly as vmethods index
				for (int i = 0; i < n; i++)
					stack_push((union vm_value){.i = s->vmethods[var_idx + i]});
			} else {
				for (int i = 0; i < n; i++)
					stack_push(0);
			}
		} else {
			// When heap_idx <= 0 (guard page or null ref), push -1 to
			// propagate null consistently. Bytecode null checks use
			// "== -1", so pushing 0 would bypass them and cause garbage
			// reads from the guard page. Only do this for null refs;
			// other fallback cases (valid heap but bad var_idx) push 0.
			int null_val = (heap_idx <= 0) ? -1 : 0;
			for (int i = 0; i < n; i++) {
				stack_push(null_val);
			}
		}
		break;
	}
	case X_ASSIGN: {
		// X_ASSIGN n: stack = [..., heap_idx, page_idx, v0..v(n-1)]
		// Pop n values, pop 2-slot ref, write values, push values back.
		// n=0 is valid: just pop the 2-slot ref (cleanup after X_A_INIT).
		int n = get_argument(0);
		if (n < 0) n = 0;
		if (n == 0) {
			// n=0: just consume the 2-slot reference, no values to write.
			stack_pop(); // page_index
			stack_pop(); // heap_index
			break;
		}
		union vm_value small_buf[64];
		union vm_value *vals = (n <= 64) ? small_buf : malloc(n * sizeof(union vm_value));
		for (int i = n - 1; i >= 0; i--) {
			vals[i] = stack_pop();
		}
		// Peek at stack to get page info for ref counting before pop_var consumes it
		int32_t xa_page_index = stack_peek(0).i;
		int32_t xa_heap_index = stack_peek(1).i;
		union vm_value *var = stack_pop_var();
		if (var && var != dummy_var) {
			// Get page for type-aware ref counting (v14 fix)
			struct page *xa_page = NULL;
			if (heap_index_valid(xa_heap_index) && heap[xa_heap_index].type == VM_PAGE)
				xa_page = heap[xa_heap_index].page;
			for (int i = 0; i < n; i++) {
				if (xa_page) {
					enum ain_data_type vtype = variable_type(xa_page, xa_page_index + i, NULL, NULL);
					switch (vtype) {
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
					case AIN_IFACE: {
						int new_slot = vals[i].i;
						int old_slot = var[i].i;
						// Ref new BEFORE unref old (safe for self-assignment)
						if (new_slot > 0 && heap_index_valid(new_slot))
							heap_ref(new_slot);
						if (old_slot > 0 && heap_index_valid(old_slot))
							heap_unref(old_slot);
						break;
					}
					default:
						break;
					}
				}
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
		// X_GETENV: replace PUSHLOCALPAGE with parent environment page.
		int env_result;
		if (ain->version >= 14 && call_stack[call_stack_ptr-1].env_page > 0) {
			env_result = call_stack[call_stack_ptr-1].env_page;
		} else {
			env_result = struct_page_slot();
		}
		stack[stack_ptr - 1].i = env_result;
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
		// X_ICAST target_type: interface cast (v14).
		// Peek at struct page on stack, look up the interface vtable offset
		// for target_type, push [page_index, vtable_offset].
		// On failure: set the ORIGINAL stack value to -1 (bytecode checks
		// it after popping the two pushed values), and push [-1, 0].
		// Stack: [..., page_idx] → [..., page_idx_or_-1, result, vtoff]
		// Net: +2
		int target_type = get_argument(0);
		int32_t page_idx = stack_peek(0).i;
		int vtoff = 0;
		bool found = false;
		if (heap_index_valid(page_idx) && heap[page_idx].type == VM_PAGE
		    && heap[page_idx].page) {
			struct page *p = heap[page_idx].page;
			int sidx = p->index;
			if (sidx >= 0 && sidx < ain->nr_structures) {
				// Direct type match: struct IS the target type
				if (sidx == target_type) {
					found = true;
					vtoff = 0;
				}
				// Interface check: struct implements target interface
				if (!found) {
					struct ain_struct *s = &ain->structures[sidx];
					for (int i = 0; i < s->nr_interfaces; i++) {
						if (s->interfaces[i].struct_type == target_type) {
							vtoff = s->interfaces[i].vtable_offset;
							found = true;
							break;
						}
					}
				}
			}
		}
		if (!found) {
			// Cast failed: set original stack value to -1 so the caller
			// can detect failure after popping the two pushed values
			stack[stack_ptr - 1].i = -1;
		}
		stack_push((union vm_value){.i = found ? page_idx : -1});
		stack_push((union vm_value){.i = vtoff});
		break;
	}
	case X_OP_SET: {
		// X_OP_SET arg: assign to multi-slot variable
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
		// arg encodes element slot count: 0 = 1-slot, 1 = 2-slot, etc.
		// For generic arrays (AIN_ARRAY), multiply physical size by (arg+1)
		// to accommodate multi-slot element types (e.g. wrap, interface).
		int elem_slots = arg + 1;
		// Allocate array slot and page
		int slot = heap_alloc_slot(VM_PAGE);
		if (size > 0) {
			if (data_type == AIN_ARRAY || data_type == AIN_REF_ARRAY) {
				// Generic array (v14 type erasure) — create flat value array.
				// Initialize to -1 (null ref) rather than 0: game code checks
				// for -1 to detect empty slots, and 0 = guard page would
				// cause garbage reads. Value types (int/float) are always
				// written before being read, so -1 is safe.
				int phys_size = size * elem_slots;
				struct page *page = alloc_page(ARRAY_PAGE, data_type, phys_size);
				for (int i = 0; i < phys_size; i++)
					page->values[i].i = -1;
				// Store elem_slots in struct_type for X_A_SIZE to use
				page->array.struct_type = elem_slots;
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
			// For generic arrays, store elem_slots (not the struct index)
			// so X_A_SIZE can correctly compute logical size.
			if (data_type == AIN_ARRAY || data_type == AIN_REF_ARRAY)
				page->array.struct_type = elem_slots;
			else
				page->array.struct_type = struct_type;
			heap_set_page(slot, page);
		}
		// Assign to variable with proper ref counting
		if (heap_index_valid(heap_idx) && heap[heap_idx].page &&
		    var_idx >= 0 && var_idx < heap[heap_idx].page->nr_vars) {
			int old_slot = heap[heap_idx].page->values[var_idx].i;
			heap[heap_idx].page->values[var_idx].i = slot;
			heap_ref(slot);
			if (old_slot > 0 && heap_index_valid(old_slot))
				heap_unref(old_slot);
		}
		stack_push(slot);
		break;
	}
	case X_A_SIZE: {
		// X_A_SIZE: push logical element count
		// For generic arrays (AIN_ARRAY) with multi-slot elements,
		// divide nr_vars by elem_slots (stored in array.struct_type).
		int slot = stack_pop().i;
		struct page *page = (heap_index_valid(slot) && heap[slot].page) ? heap[slot].page : NULL;
		int size = page ? page->nr_vars : 0;
		// For generic arrays with multi-slot elements, return logical size
		if (page && (page->a_type == AIN_ARRAY || page->a_type == AIN_REF_ARRAY)
		    && page->array.struct_type > 1) {
			size = size / page->array.struct_type;
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
	case FUNC: {
		break;
	}
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

static int vm_execute_depth = 0;

static void vm_execute(void)
{
	vm_execute_depth++;
	if (vm_execute_depth > 10) {
		static int ved_warn = 0;
		if (ved_warn++ < 3)
			WARNING("vm_execute: recursion depth %d (ip=0x%lX csp=%d)",
				vm_execute_depth, (unsigned long)instr_ptr, call_stack_ptr);
		vm_execute_depth--;
		return;
	}
	for (;;) {
		uint16_t opcode;
		if (instr_ptr == VM_RETURN) {
			vm_execute_depth--;
			return;
		}
		if (unlikely(instr_ptr >= ain->code_size)) {
			VM_ERROR("Illegal instruction pointer: 0x%08lX", instr_ptr);
		}
		opcode = get_opcode(instr_ptr);
		insn_count++;
		// Heartbeat: log current function every 50M instructions
		if (unlikely((insn_count & 0x2FFFFFF) == 0)) {
			int cfno = (call_stack_ptr > 0) ? call_stack[call_stack_ptr-1].fno : -1;
			WARNING("heartbeat: %lluM insns fno=%d depth=%d ip=0x%X op=0x%X",
				insn_count/1000000, cfno, call_stack_ptr, instr_ptr, opcode);
			if (call_stack_ptr > 1) {
				static int _deep_dump = 0;
				int dump_n = (call_stack_ptr > 100 && _deep_dump < 1) ? call_stack_ptr : 4;
				if (dump_n > 4) _deep_dump++;
				for (int _h = call_stack_ptr-1; _h >= 0 && _h >= call_stack_ptr-dump_n; _h--) {
					int _hfno = call_stack[_h].fno;
					WARNING("  stack[%d]: fno=%d '%s'", _h, _hfno,
						(_hfno >= 0 && _hfno < ain->nr_functions && ain->functions[_hfno].name) ? ain->functions[_hfno].name : "?");
				}
				// Show bottom frames (game scene context)
				if (call_stack_ptr > 6) {
					for (int _h = 0; _h < 6; _h++) {
						int _hfno = call_stack[_h].fno;
						WARNING("  base[%d]: fno=%d '%s'", _h, _hfno,
							(_hfno >= 0 && _hfno < ain->nr_functions && ain->functions[_hfno].name) ? ain->functions[_hfno].name : "?");
					}
				}
			}
		}
		// Periodically pump events (~every 256K instructions)
		// to prevent OS "not responding" and keep screen alive.
		if (unlikely((insn_count & 0x3FFFF) == 0)) {
			handle_events();
			// Periodic buffer swap to keep OS window alive.
			{
				static uint32_t last_vm_swap = 0;
				uint32_t now_r = SDL_GetTicks();
				if (now_r - last_vm_swap >= 500) {
					gfx_swap();
					last_vm_swap = now_r;
				}
			}
			// Periodic GC every 10 seconds to collect cycle-garbage
			// (e.g. CParts with delegate reference cycles)
			{
				static uint32_t last_periodic_gc = 0;
				uint32_t now_gc = SDL_GetTicks();
				if (now_gc - last_periodic_gc >= 10000) {
					last_periodic_gc = now_gc;
					extern void heap_gc_periodic(void);
					heap_gc_periodic();
				}
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
						if (call_stack[call_stack_ptr-1].return_address == VM_RETURN
								&& call_stack_ptr > 1)
							vm_call_depth--;
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
	if (heap_size <= 0 || heap[global_page_slot].ref <= 0)
		return;
	struct page *global_page = heap_get_page(global_page_slot);
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
	if (heap_size > 0 && heap[global_page_slot].ref > 0)
		exit_unref(global_page_slot);

	vm_reset_once = true;
}

static jmp_buf reset_buf;

_Noreturn void vm_reset(void)
{
	vm_free();
	longjmp(reset_buf, 1);
}

static volatile sig_atomic_t in_signal_handler = 0;
static pthread_t main_thread_id;

static void sigabrt_handler(int sig)
{
	// macOS: ignore SIGSEGV from background threads (BoardServices/XPC crashes)
	if (sig == SIGSEGV && !pthread_equal(pthread_self(), main_thread_id)) {
		return;  // let the background thread die quietly
	}
	// Prevent re-entrant crashes (e.g., SIGSEGV inside this handler)
	if (in_signal_handler) {
		signal(sig, SIG_DFL);
		raise(sig);
		_exit(128 + sig);
	}
	in_signal_handler = 1;

	WARNING("=== SIGNAL %d received ===", sig);
	if (call_stack_ptr > 0) {
		int fno = call_stack[call_stack_ptr-1].fno;
		WARNING("Current function: fno=%d '%s' ip=0x%lX sp=%d",
			fno, (fno >= 0 && fno < ain->nr_functions) ? ain->functions[fno].name : "?",
			(unsigned long)instr_ptr, stack_ptr);
		WARNING("=== VM call stack (%d frames) ===", call_stack_ptr);
		for (int i = call_stack_ptr - 1; i >= 0 && i >= call_stack_ptr - 20; i--) {
			int f = call_stack[i].fno;
			WARNING("  [%d] fno=%d '%s'",
				i, f, (f >= 0 && f < ain->nr_functions) ? ain->functions[f].name : "?");
		}
	}
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
	if (sig == SIGUSR1) {
		in_signal_handler = 0;
		signal(SIGUSR1, sigabrt_handler);
		return;
	}
	signal(sig, SIG_DFL);
	raise(sig);
	_exit(128 + sig);
}

int vm_execute_ain(struct ain *program)
{
	ain = program;
	main_thread_id = pthread_self();

	// Install signal handlers to dump trace buffer and C backtrace
	signal(SIGABRT, sigabrt_handler);
	signal(SIGUSR1, sigabrt_handler);
	signal(SIGSEGV, sigabrt_handler);

	setjmp(reset_buf);

	// initialize VM state
	if (!stack) {
		stack_size = INITIAL_STACK_SIZE;
		stack = xmalloc(INITIAL_STACK_SIZE * sizeof(union vm_value));
	}
	stack_ptr = 0;
	call_stack_ptr = 0;
	vm_call_depth = 0;

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
	init_func_flags();

	// Initialize v14 message system
	if (ain->version >= 14 && ain->msgf < 0) {
		vm_msg_init();
	}

	// Slot 0 = null guard page: absorbs accidental writes from null references
	// (method_call skips, failed lookups, etc.) without crashing or corrupting globals.
	// Use ARRAY_PAGE (not STRUCT_PAGE) so method_call validation rejects it.
	heap[0].ref = 1;
	heap[0].seq = heap_next_seq++;
	heap[0].type = VM_PAGE;
	heap[0].page = alloc_page(ARRAY_PAGE, AIN_VOID, 64);  // direct set, bypass heap_set_page guard

	// Initialize globals at slot 1 (not 0, so null refs don't alias global page)
	heap[global_page_slot].ref = 1;
	heap[global_page_slot].seq = heap_next_seq++;
	heap[global_page_slot].type = VM_PAGE;
	heap_set_page(global_page_slot, alloc_page(GLOBAL_PAGE, 0, ain->nr_globals));
	for (int i = 0; i < ain->nr_globals; i++) {
		if (ain->version >= 14) {
			// v14: alloc function handles all global initialization
			// Use -1 so DELETE on uninitialized globals is safely skipped
			heap[global_page_slot].page->values[i].i = -1;
		} else if (ain->globals[i].type.data == AIN_STRUCT) {
			// XXX: need to allocate storage for global structs BEFORE calling
			//      constructors.
			heap[global_page_slot].page->values[i].i = alloc_struct(ain->globals[i].type.struc);
		} else {
			heap[global_page_slot].page->values[i] = variable_initval(ain->globals[i].type.data);
		}
	}
	for (int i = 0; i < ain->nr_initvals; i++) {
		int32_t index;
		struct ain_initval *v = &ain->global_initvals[i];
		switch (v->data_type) {
		case AIN_STRING:
			index = heap_alloc_slot(VM_STRING);
			heap[global_page_slot].page->values[v->global_index].i = index;
			heap[index].s = make_string(v->string_value, strlen(v->string_value));
			break;
		default:
			heap[global_page_slot].page->values[v->global_index].i = v->int_value;
			break;
		}
	}

	NOTICE("VM: alloc=%d, main=%d, nr_globals=%d, nr_functions=%d, version=%d, nr_delegates=%d",
		ain->alloc, ain->main, ain->nr_globals, ain->nr_functions, ain->version, ain->nr_delegates);
	if (ain->alloc >= 0 && ain->functions[ain->alloc].address != 0xFFFFFFFF) {
		vm_in_alloc_phase = true;
		vm_call(ain->alloc, -1);
		vm_in_alloc_phase = false;
		free_deferred_strings();
	}
	// v14: after alloc, repair any global struct pages that were corrupted
	// during the alloc function (pages freed and reused as local pages).
	if (ain->version >= 14) {
		int repaired = 0;
		for (int i = 0; i < ain->nr_globals; i++) {
			if (ain->globals[i].type.data != AIN_STRUCT)
				continue;
			int slot = heap[global_page_slot].page->values[i].i;
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
				heap[global_page_slot].page->values[i].i = new_slot;
				repaired++;
			}
		}
		if (repaired > 0)
			WARNING("v14: repaired %d global struct pages after alloc", repaired);

		for (int i = 0; i < ain->nr_globals; i++) {
			if (ain->globals[i].type.data != AIN_STRUCT)
				continue;
			int slot = heap[global_page_slot].page->values[i].i;
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
		// v14: Two-pass initialization to resolve cross-global dependencies.
		// Pass 1 (forward): initialize infrastructure globals first
		// (e.g. CASTimerManager must be ready before CASTimer ctors use it).
		// Pass 2 (reverse): remaining globals that may depend on infrastructure.
		// Track which globals are initialized to avoid double-init.
		bool *ginited = xcalloc(ain->nr_globals, sizeof(bool));
		for (int i = 0; i < ain->nr_globals; i++) {
			if (ain->globals[i].type.data != AIN_STRUCT)
				continue;
			int slot = heap[global_page_slot].page->values[i].i;
			if (slot <= 0 || !heap_index_valid(slot))
				continue;
			int st = ain->globals[i].type.struc;
			if (st < 0 || st >= ain->nr_structures)
				continue;
			// Infrastructure types: timer managers, collections, singletons
			const char *sn = ain->structures[st].name;
			bool is_infra = (sn && (strstr(sn, "Manager") || strstr(sn, "Collection")
					 || strstr(sn, "Map<") || strstr(sn, "IdArray")));
			if (is_infra) {
				init_global_struct_v14(st, slot);
				ginited[i] = true;
			}
		}
		for (int i = ain->nr_globals - 1; i >= 0; i--) {
			if (ginited[i])
				continue;
			if (ain->globals[i].type.data == AIN_STRUCT) {
				int slot = heap[global_page_slot].page->values[i].i;
				if (slot > 0 && heap_index_valid(slot))
					init_global_struct_v14(ain->globals[i].type.struc, slot);
			}
		}
		free(ginited);
	} else {
		for (int i = 0; i < ain->nr_globals; i++) {
			if (ain->globals[i].type.data == AIN_STRUCT) {
				int slot = heap[global_page_slot].page->values[i].i;
				if (slot > 0 && heap_index_valid(slot))
					init_struct(ain->globals[i].type.struc, slot);
			}
		}
	}
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

/* Helper: return current top function name for debugging from HLL code */
const char *vm_get_func_name(void)
{
	if (call_stack_ptr <= 0) return "(none)";
	int fno = call_stack[call_stack_ptr - 1].fno;
	if (fno >= 0 && fno < ain->nr_functions)
		return ain->functions[fno].name;
	return "(unknown)";
}

_Noreturn void _vm_error(const char *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	sys_vwarning(fmt, ap);
	va_end(ap);
	sys_warning("at %s (0x%X) in:\n", current_instruction_name(), instr_ptr);
	vm_stack_trace();

	char msg[1024];
	va_start(ap, fmt);
	vsnprintf(msg, 1024, fmt, ap);
	va_end(ap);

	dbg_repl(DBG_STOP_ERROR, msg);
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
	sys_message("Number of free slots: %zu (leaked = %zu)\n", heap_free_count, heap_size - heap_free_count - 2);
#endif
	sys_exit(code);
}
