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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <ffi.h>
#include "system4/ain.h"
#include "system4/string.h"
#include "system4/utfsjis.h"
#include "vm.h"
#include "vm/heap.h"
#include "vm/page.h"
#include "xsystem4.h"

// v14: source tracking for null-array FFI write-back.
// Set by X_REF when it pushes -1 from a struct member.
int xref_null_src_page = -1;
int xref_null_src_var = -1;

#define HLL_MAX_ARGS 64

struct hll_function {
	void *fun;
	ffi_cif cif;
	unsigned int nr_args;
	ffi_type **args;
	ffi_type *return_type;
};

static struct hll_function **libraries = NULL;

// Object slot for AIN_HLL_FUNC callbacks (closure env / method receiver).
// Set by FFI when processing AIN_HLL_FUNC arguments; read by vm_call_nopop
// to set struct_page on the new call frame for lambda/method callbacks.
int hll_func_obj = -1;

bool library_exists(int libno)
{
	return libraries[libno];
}

bool library_function_exists(int libno, int fno)
{
	return libraries[libno] && libraries[libno][fno].fun;
}

/*
 * Primitive HLL function tracing facility.
 */
//#define TRACE_HLL
#ifdef TRACE_HLL
#include "system4/string.h"

struct traced_library {
	const char *name;
	bool (*should_trace)(struct ain_hll_function *f, union vm_value **args);
};

static bool chipmunk_should_trace(struct ain_hll_function *f, union vm_value **args)
{
	const char *no_trace[] = {
		"SYSTEM_SetReadMessageSkipping",
		"Update",
		"KeepPreviousView",
		"Sleep",
	};
	for (unsigned i = 0; i < sizeof(no_trace) / sizeof(*no_trace); i++) {
		if (!strcmp(f->name, no_trace[i]))
			return false;
	}
	return true;
}

#include "parts.h"
#include "parts/parts_internal.h"
static bool gui_engine_should_trace(struct ain_hll_function *f, union vm_value **args)
{
	const char *no_trace[] = {
		"Parts_UpdateMotionTime",
		"UpdateInputState",
		"UpdateComponent",
		"UpdateParts",
		"GetMessageType",
		"ReleaseMessage",
		"Parts_SetPartsPixelDecide",
		"Parts_GetPartsShow",
		"Parts_IsCursorIn",
		"Parts_GetPartsAlpha",
	};
	for (unsigned i = 0; i < sizeof(no_trace) / sizeof(*no_trace); i++) {
		if (!strcmp(f->name, no_trace[i]))
			return false;
	}
	if (!strcmp(f->name, "Parts_SetPartsCG")) {
		static char *u = NULL;
		if (!u)
			u = utf2sjis("システム／ボタン／メニュー／通常", 0);
		struct string ***strs = (void*)args;
		struct string *s = *strs[1];
		return strcmp(s->text, u);
	}
	if (!strcmp(f->name, "Parts_SetPos")) {
		if (args[1]->i < 0 || args[2]->i < 0)
			return false;
		return PE_GetPartsX(args[0]->i) != args[1]->i || PE_GetPartsY(args[0]->i) != args[2]->i;
	}
	if (!strcmp(f->name, "Parts_SetZ"))
		return PE_GetPartsZ(args[0]->i) != args[1]->i;
	if (!strcmp(f->name, "Parts_SetShow"))
		return PE_GetPartsShow(args[0]->i) != args[1]->i;
	if (!strcmp(f->name, "Parts_SetAlpha"))
		return PE_GetPartsAlpha(args[0]->i) != args[1]->i;
	if (!strcmp(f->name, "Parts_SetPartsCGSurfaceArea")) {
		return false;
		struct parts *p = parts_try_get(args[0]->i);
		if (!p)
			return true;
		Rectangle *r = &p->states[args[5]->i].common.surface_area;
		return args[1]->i != r->x || args[2]->i != r->y
			|| args[3]->i != r->w || args[4]->i != r->h;
	}
	if (!strcmp(f->name, "Parts_SetAddColor")) {
		struct parts *p = parts_try_get(args[0]->i);
		if (!p)
			return true;
		SDL_Color *c = &p->local.add_color;
		return args[1]->i != c->r || args[2]->i != c->g || args[3]->i != c->b;
	}
	if (!strcmp(f->name, "Parts_GetPartsX"))
		return false;
	return true;
}

static bool parts_engine_should_trace(struct ain_hll_function *f, union vm_value **args)
{
	if (!strcmp(f->name, "Update"))
		return false;
	if (!strcmp(f->name, "SetAddColor"))
		return false;
	if (!strcmp(f->name, "SetAlpha"))
		return PE_GetPartsAlpha(args[0]->i) != args[1]->i;
	if (!strcmp(f->name, "SetPartsCGSurfaceArea"))
		return args[1]->i >= 0 && args[2]->i >= 0;
	if (!strcmp(f->name, "SetPartsConstructionSurfaceArea"))
		return args[1]->i >= 0 && args[2]->i >= 0;
	if (!strcmp(f->name, "SetPartsMagX"))
		return args[1]->f > 1.001 || args[1]->f < -1.001;
	if (!strcmp(f->name, "SetPartsMagY"))
		return args[1]->f > 1.001 || args[1]->f < -1.001;
	if (!strcmp(f->name, "SetPartsOriginPosMode"))
		return PE_GetPartsOriginPosMode(args[0]->i) != args[1]->i;
	if (!strcmp(f->name, "SetPartsRotateZ"))
		return args[1]->f > 0.001 || args[1]->f < -0.001;
	if (!strcmp(f->name, "SetPos"))
		return PE_GetPartsX(args[0]->i) != args[1]->i || PE_GetPartsX(args[0]->i) != args[2]->i;
	if (!strcmp(f->name, "SetShow")) {
		if (args[0]->i == 0)
			return false; // ???
		return PE_GetPartsShow(args[0]->i) != args[1]->i;
	}
	if (!strcmp(f->name, "SetZ"))
		return PE_GetPartsZ(args[0]->i) != args[1]->i;
	return true;
}

static bool sact_should_trace(struct ain_hll_function *f, union vm_value **args)
{
	const char *no_trace[] = {
		"Key_IsDown",
		"Mouse_ClearWheel",
		"Mouse_GetPos",
		"Mouse_GetWheel",
		"SP_GetWidth",
		"SP_GetHeight",
		"SP_ExistAlpha",
		"Update",
	};
	for (unsigned i = 0; i < sizeof(no_trace) / sizeof(*no_trace); i++) {
		if (!strcmp(f->name, no_trace[i]))
			return false;
	}
	return true;
}

struct traced_library traced_libraries[] = {
	{ "ChipmunkSpriteEngine", chipmunk_should_trace },
	{ "GUIEngine", gui_engine_should_trace },
	{ "PartsEngine", parts_engine_should_trace },
	{ "SACTDX", sact_should_trace },
};

static void print_hll_trace(struct ain_library *lib, struct ain_hll_function *f,
		      struct hll_function *fun, union vm_value *r, void *_args)
{
	sys_message("(%s) ", display_sjis0(ain->functions[call_stack[call_stack_ptr-1].fno].name));
	sys_message("%s.%s(", lib->name, f->name);
	union vm_value **args = _args;
	for (int i = 0; i < f->nr_arguments; i++) {
		if (i > 0) {
			sys_message(", ");
		}
		sys_message("%s=", display_sjis0(f->arguments[i].name));
		switch (f->arguments[i].type.data) {
		case AIN_INT:
		case AIN_LONG_INT:
			sys_message("%d", args[i]->i);
			break;
		case AIN_FLOAT:
			sys_message("%f", args[i]->f);
			break;
		case AIN_STRING: {
			struct string ***strs = _args;
			struct string *s = *strs[i];
			sys_message("\"%s\"", display_sjis0(s->text));
			break;
		}
		case AIN_BOOL:
			sys_message("%s", args[i]->i ? "true" : "false");
			break;
		case AIN_STRUCT:
			sys_message("<struct>");
			break;
		case AIN_ARRAY_TYPE:
			sys_message("<array>");
			break;
		case AIN_REF_TYPE:
			sys_message("<ref>");
			break;
		default:
			sys_message("<%d>", f->arguments[i].type.data);
			break;
		}
	}
	sys_message(")");

	ffi_call(&fun->cif, (void*)fun->fun, r, _args);

	switch (f->return_type.data) {
	case AIN_VOID:
		break;
	case AIN_INT:
		sys_message(" -> %d", r->i);
		break;
	case AIN_FLOAT:
		sys_message(" -> %f", r->f);
		break;
	case AIN_STRING:
		sys_message(" -> \"%s\"", display_sjis0(((struct string*)r->ref)->text));
		break;
	case AIN_BOOL:
		sys_message(" -> %s", r->i ? "true" : "false");
		break;
	default:
		sys_message(" -> <%d>", f->return_type.data);
		break;
	}
	for (int i = 0; i < f->nr_arguments; i++) {
		union vm_value ***args = _args;
		switch (f->arguments[i].type.data) {
		case AIN_REF_INT:
			sys_message(" (%s=%d)", display_sjis0(f->arguments[i].name), (*args[i])->i);
			break;
		case AIN_REF_FLOAT:
			sys_message(" (%s=%f)", display_sjis0(f->arguments[i].name), (*args[i])->f);
			break;
		default:
			break;
		}
	}
	sys_message("\n");
}

static void trace_hll_call(struct ain_library *lib, struct ain_hll_function *f,
		      struct hll_function *fun, union vm_value *r, void *_args)
{
	for (unsigned i = 0; i < sizeof(traced_libraries)/sizeof(*traced_libraries); i++) {
		struct traced_library *l = &traced_libraries[i];
		if (!strcmp(lib->name, l->name)) {
			if (!l->should_trace || l->should_trace(f, _args)) {
				print_hll_trace(lib, f, fun, r, _args);
				return;
			}
			break;
		}
	}

	ffi_call(&fun->cif, (void*)fun->fun, r, _args);
}
#endif /* TRACE_HLL */

/* Ring buffer of last HLL calls for crash diagnostics */
#define HLL_RING_SIZE 8
static struct { int libno; int fno; } hll_ring[HLL_RING_SIZE];
static int hll_ring_idx = 0;

void hll_dump_ring(void)
{
	WARNING("=== Last %d HLL calls ===", HLL_RING_SIZE);
	for (int i = 0; i < HLL_RING_SIZE; i++) {
		int idx = (hll_ring_idx - HLL_RING_SIZE + i + HLL_RING_SIZE * 2) % HLL_RING_SIZE;
		int lib = hll_ring[idx].libno, fn = hll_ring[idx].fno;
		if (lib >= 0 && lib < ain->nr_libraries && fn >= 0 && fn < ain->libraries[lib].nr_functions)
			WARNING("  [%d] %s.%s", i, ain->libraries[lib].name, ain->libraries[lib].functions[fn].name);
	}
}

void hll_call(int libno, int fno, int hll_arg3)
{
	struct ain_hll_function *f = &ain->libraries[libno].functions[fno];

	/* Record in ring buffer */
	hll_ring[hll_ring_idx % HLL_RING_SIZE].libno = libno;
	hll_ring[hll_ring_idx % HLL_RING_SIZE].fno = fno;
	hll_ring_idx++;

	if (!libraries[libno] || !libraries[libno][fno].fun) {
		/* Rate-limited warning: first 3 per (lib,func), then every 1M */
		{
			static struct { int libno; int fno; int count; } unimp_log[64];
			static int unimp_log_cnt = 0;
			int idx = -1;
			for (int i = 0; i < unimp_log_cnt; i++) {
				if (unimp_log[i].libno == libno && unimp_log[i].fno == fno) {
					idx = i; break;
				}
			}
			if (idx < 0 && unimp_log_cnt < 64) {
				idx = unimp_log_cnt++;
				unimp_log[idx].libno = libno;
				unimp_log[idx].fno = fno;
				unimp_log[idx].count = 0;
			}
			int cnt = idx >= 0 ? ++unimp_log[idx].count : 1;
			if (cnt <= 5) {
				WARNING("UNIMPL HLL: %s.%s (args=%d, cnt=%d)",
					ain->libraries[libno].name, f->name, f->nr_arguments, cnt);
			} else if (cnt % 1000000 == 0) {
				WARNING("Unimplemented HLL (x%dM): %s.%s",
					cnt / 1000000, ain->libraries[libno].name, f->name);
			}
		}
		// Pop all arguments from stack
		for (int i = f->nr_arguments - 1; i >= 0; i--) {
			switch (f->arguments[i].type.data) {
			case AIN_REF_INT:
			case AIN_REF_LONG_INT:
			case AIN_REF_BOOL:
			case AIN_REF_FLOAT:
			case AIN_REF_HLL_PARAM:
				stack_ptr -= 2; break; // 2-slot reference (pageno, varno)
			case AIN_HLL_FUNC:
				stack_ptr -= 2; break; // 2-slot (object, function)
			case AIN_WRAP:
				stack_ptr -= 2; break; // v14: 2-slot ref [page, varno]
			case AIN_IFACE:
			case AIN_IFACE_WRAP:
				stack_ptr -= 2; break; // v14: 2-slot [page, vtable_offset]
			case AIN_OPTION:
				stack_ptr -= 2; break; // v14: 2-slot [value, discriminant]
			case AIN_HLL_PARAM: {
				// v14: generic element parameter. Slot count depends on
				// element type encoded in hll_arg3.
				int etype = hll_arg3 & 0xFFFF;
				bool is_2slot = (etype == 3 || etype == 5 ||
					etype == AIN_IFACE || etype == AIN_OPTION ||
					etype == AIN_IFACE_WRAP);
				stack_ptr -= is_2slot ? 2 : 1;
				break;
			}
			case AIN_REF_ARRAY:
				stack_ptr--; break; // v14: 1-slot (resolved heap slot)
			default:
				stack_ptr--; break;
			}
		}
		// Push default return value
		if (f->return_type.data != AIN_VOID) {
			stack_push(0);
		}
		return;
	}

	struct hll_function *fun = &libraries[libno][fno];

	void *args[HLL_MAX_ARGS];
	void *ptrs[HLL_MAX_ARGS];
	// Copy reference arguments to the stack to protect against heap
	// reallocation during HLL calls.
	void *heap_ptrs[HLL_MAX_ARGS];
	int heap_slots[HLL_MAX_ARGS];
	// (wrap_pagenos/wrap_varnos removed — AIN_WRAP is 1-slot)
	// v14: expose hll_arg3 to HLL functions (Array uses it for element type info)
	extern int hll_current_arg3;
	hll_current_arg3 = hll_arg3;
	// v14: save the first AIN_REF_ARRAY argument's resolved heap slot
	// so HLL functions can construct 2-slot references for REF_HLL_PARAM return.
	extern int hll_self_slot;
	hll_self_slot = -1;

	// Reset null-array source tracker (set by X_REF in vm.c)
	// before processing arguments — it will be set again if
	// the relevant X_REF just pushed -1 for this call.
	// (Don't reset here — the X_REF that set it is the one right
	// before CALLHLL, so it's still valid.)

	for (int i = f->nr_arguments - 1; i >= 0; i--) {
		switch (f->arguments[i].type.data) {
		case AIN_REF_INT:
		case AIN_REF_LONG_INT:
		case AIN_REF_BOOL:
		case AIN_REF_FLOAT:
		case AIN_REF_HLL_PARAM: {
			// need to create pointer for immediate ref types (2-slot)
			stack_ptr -= 2;
			int pageno = stack[stack_ptr].i;
			int varno  = stack[stack_ptr+1].i;
			if (pageno >= 0 && (size_t)pageno < heap_size
			    && heap[pageno].page
			    && varno >= 0 && varno < heap[pageno].page->nr_vars) {
				ptrs[i] = &heap[pageno].page->values[varno];
			} else {
				// Invalid ref — provide a safe scratch location
				heap_slots[i] = 0;
				ptrs[i] = (void *)&heap_slots[i];
			}
			args[i] = &ptrs[i];
			break;
		}
		case AIN_WRAP: {
			// v14: WRAP parameter in HLL calls.
			// Bytecode pushes a 2-slot reference [pageno, varno].
			// Behavior depends on the inner type:
			//   wrap<value_type> (int/float/bool): pass pointer for ref semantics
			//   wrap<ref_type> (array/struct/string): pass inner value as int (slot)
			stack_ptr -= 2;
			int pageno = stack[stack_ptr].i;
			int varno  = stack[stack_ptr+1].i;
			enum ain_data_type inner = AIN_VOID;
			if (f->arguments[i].type.array_type)
				inner = f->arguments[i].type.array_type->data;
			bool is_value_wrap = (inner == AIN_INT || inner == AIN_FLOAT
				|| inner == AIN_BOOL || inner == AIN_LONG_INT);
			if (pageno > 0 && heap_index_valid(pageno)
			    && heap[pageno].type == VM_PAGE && heap[pageno].page
			    && varno >= 0 && varno < heap[pageno].page->nr_vars) {
				if (is_value_wrap) {
					// Pass pointer to page value (ref semantics)
					ptrs[i] = &heap[pageno].page->values[varno];
					args[i] = &ptrs[i];
				} else {
					// Extract inner value (heap slot) and pass as int
					heap_slots[i] = heap[pageno].page->values[varno].i;
					args[i] = &heap_slots[i];
				}
			} else {
				// Fallback: try 1-slot
				stack_ptr += 2;
				stack_ptr--;
				heap_slots[i] = stack[stack_ptr].i;
				if (is_value_wrap) {
					ptrs[i] = (void *)&heap_slots[i];
					args[i] = &ptrs[i];
				} else {
					args[i] = &heap_slots[i];
				}
			}
			break;
		}
		case AIN_STRING: {
			stack_ptr--;
			int slot = stack[stack_ptr].i;
			if (slot >= 0 && (size_t)slot < heap_size)
				args[i] = &heap[slot].s;
			else {
				heap_ptrs[i] = NULL;
				args[i] = &heap_ptrs[i];
			}
			break;
		}
		case AIN_REF_STRING: {
			stack_ptr--;
			int slot = stack[stack_ptr].i;
			if (slot >= 0 && (size_t)slot < heap_size
			    && heap[slot].type == VM_STRING) {
				heap_slots[i] = slot;
				heap_ptrs[i] = heap[slot].s;
			} else {
				heap_slots[i] = -1;
				heap_ptrs[i] = NULL;
			}
			ptrs[i] = &heap_ptrs[i];
			args[i] = &ptrs[i];
			break;
		}
		case AIN_STRUCT:
		case AIN_ARRAY_TYPE:
		case AIN_ARRAY:
		case AIN_DELEGATE: {
			stack_ptr--;
			int slot = stack[stack_ptr].i;
			if (slot >= 0 && (size_t)slot < heap_size) {
				args[i] = &heap[slot].page;
				if (f->arguments[i].type.data == AIN_ARRAY && hll_self_slot < 0)
					hll_self_slot = slot;
			} else {
				heap_ptrs[i] = NULL;
				args[i] = &heap_ptrs[i];
			}
			break;
		}
		case AIN_REF_STRUCT:
		case AIN_REF_ARRAY_TYPE:
		case AIN_REF_DELEGATE: {
			stack_ptr--;
			int slot = stack[stack_ptr].i;
			if (slot >= 0 && (size_t)slot < heap_size
			    && heap[slot].type == VM_PAGE) {
				heap_slots[i] = slot;
				heap_ptrs[i] = heap[slot].page;
			} else {
				heap_slots[i] = -1;
				heap_ptrs[i] = NULL;
			}
			ptrs[i] = &heap_ptrs[i];
			args[i] = &ptrs[i];
			break;
		}
		case AIN_REF_ARRAY: {
			// v14: 1-slot — bytecode resolves via X_REF and pushes
			// the array's heap slot directly.
			// In v14, arrays may be wrapped in struct pages (IArray<T>).
			// Recursively unwrap struct wrappers to find the inner ARRAY_PAGE.
			stack_ptr--;
			int slot = stack[stack_ptr].i;
			int array_slot = -1;
			// Try to find the inner array by recursive unwrapping
			int cur = slot;
			for (int depth = 0; depth < 4; depth++) {
				if (cur <= 0 || (size_t)cur >= heap_size || heap[cur].type != VM_PAGE)
					break;
				if (!heap[cur].page || heap[cur].page->type == ARRAY_PAGE) {
					array_slot = cur;
					break;
				}
				if (heap[cur].page->type == STRUCT_PAGE) {
					struct page *sp = heap[cur].page;
					// Try each member to find an array slot
					bool found = false;
					for (int m = 0; m < sp->nr_vars && m < 4; m++) {
						int ms = sp->values[m].i;
						if (ms > 0 && (size_t)ms < heap_size
						    && heap[ms].type == VM_PAGE
						    && (!heap[ms].page || heap[ms].page->type == ARRAY_PAGE)) {
							array_slot = ms;
							found = true;
							break;
						}
					}
					if (found)
						break;
					// Try to recurse into struct members
					bool recursed = false;
					for (int m = 0; m < sp->nr_vars && m < 4; m++) {
						int ms = sp->values[m].i;
						if (ms > 0 && (size_t)ms < heap_size
						    && heap[ms].type == VM_PAGE && heap[ms].page
						    && heap[ms].page->type == STRUCT_PAGE) {
							cur = ms;
							recursed = true;
							break;
						}
					}
					if (!recursed)
						break;
				} else {
					break;
				}
			}
			if (array_slot > 0) {
				heap_slots[i] = array_slot;
				heap_ptrs[i] = heap[array_slot].page;
				if (hll_self_slot < 0) hll_self_slot = array_slot;
			} else if (slot <= 0 && xref_null_src_page >= 0) {
				// v14: null array (-1) from a struct member.
				// Allocate a new heap slot so the HLL function can
				// create an array and the write-back will succeed.
				// Then update the source struct member to point to
				// the new slot.
				int new_slot = heap_alloc_slot(VM_PAGE);
				heap[new_slot].page = NULL;
				heap_slots[i] = new_slot;
				heap_ptrs[i] = NULL;
				// Update the struct member that held -1
				if ((size_t)xref_null_src_page < heap_size
				    && heap[xref_null_src_page].type == VM_PAGE
				    && heap[xref_null_src_page].page
				    && xref_null_src_var >= 0
				    && xref_null_src_var < heap[xref_null_src_page].page->nr_vars) {
					heap[xref_null_src_page].page->values[xref_null_src_var].i = new_slot;
				}
				xref_null_src_page = -1;
				xref_null_src_var = -1;
			} else {
				// No inner array found — pass NULL safely
				heap_slots[i] = -1;
				heap_ptrs[i] = NULL;
			}
			ptrs[i] = &heap_ptrs[i];
			args[i] = &ptrs[i];
			break;
		}
		case AIN_HLL_FUNC: {
			// v14: 2-slot value — (object_heap_slot, function_number)
			// object_heap_slot: -1 for static/lambda, or heap slot of closure env
			// function_number: always the AIN function index directly
			stack_ptr -= 2;
			int obj_slot = stack[stack_ptr].i;
			int func_no  = stack[stack_ptr+1].i;
			// Store obj_slot so vm_call_nopop can set struct_page for
			// lambda/method callbacks (e.g. Array.First predicate).
			hll_func_obj = obj_slot;
			// NOTE: union vm_value is 8 bytes (contains void* member) but
			// FFI uses ffi_type_sint32 (4 bytes). Passing &stack[sp] may
			// cause FFI to read garbage from the upper bytes on 64-bit.
			// Store the clean int32 value in heap_slots[i] (int array).
			heap_slots[i] = func_no;
			args[i] = &heap_slots[i];
			break;
		}
		case AIN_IFACE:
		case AIN_IFACE_WRAP:
		case AIN_OPTION: {
			// v14: 2-slot value types
			// AIN_IFACE: [page_slot, vtable_offset]
			// AIN_OPTION: [value, discriminant]
			// AIN_IFACE_WRAP: [page_slot, vtable_offset] (wrapped)
			// Pop 2 slots, pass first slot as pointer-sized value.
			// FFI type is ffi_type_pointer, so use heap_ptrs for storage.
			stack_ptr -= 2;
			int slot = stack[stack_ptr].i;
			if (slot > 0 && (size_t)slot < heap_size && heap[slot].type == VM_PAGE)
				heap_ptrs[i] = heap[slot].page;
			else
				heap_ptrs[i] = (void*)(intptr_t)slot;
			args[i] = &heap_ptrs[i];
			break;
		}
		case AIN_HLL_PARAM: {
			// v14: generic element parameter (type 74). The actual element
			// type is encoded in hll_arg3. For 2-slot types (wrap, interface,
			// option), bytecode pushes 2 values but AIN declares 1 param.
			int etype = hll_current_arg3 & 0xFFFF;
			bool is_2slot = (etype == 3 || etype == 5 ||
				etype == AIN_IFACE || etype == AIN_OPTION ||
				etype == AIN_IFACE_WRAP);
			if (is_2slot) {
				stack_ptr -= 2;
				extern int hll_param_slot2;
				heap_slots[i] = stack[stack_ptr].i;
				hll_param_slot2 = stack[stack_ptr+1].i;
				args[i] = &heap_slots[i];
			} else {
				stack_ptr--;
				args[i] = &stack[stack_ptr];
			}
			break;
		}
		default:
			stack_ptr--;
			args[i] = &stack[stack_ptr];
			break;
		}
	}

	union vm_value r;

#ifdef TRACE_HLL
	trace_hll_call(&ain->libraries[libno], f, fun, &r, args);
#else
	ffi_call(&fun->cif, (void*)fun->fun, &r, args);
#endif

	for (int i = 0, j = 0; i < f->nr_arguments; i++, j++) {
		// XXX: We don't increase the ref count when passing ref arguments to HLL
		//      functions, so we need to avoid decreasing it via variable_fini
		switch (f->arguments[i].type.data) {
		case AIN_REF_INT:
		case AIN_REF_LONG_INT:
		case AIN_REF_BOOL:
		case AIN_REF_FLOAT:
		case AIN_REF_HLL_PARAM:
		case AIN_HLL_FUNC:
			j++;
			break;
		case AIN_WRAP: // v14: 2-slot ref — written through pointer, skip extra slot
			j++;
			break;
		case AIN_IFACE:
		case AIN_IFACE_WRAP:
		case AIN_OPTION: // v14: 2-slot value types, skip extra slot
			j++;
			break;
		case AIN_HLL_PARAM: {
			// v14: generic element — skip extra slot if 2-slot type
			int etype = hll_current_arg3 & 0xFFFF;
			if (etype == 3 || etype == 5 || etype == AIN_IFACE ||
			    etype == AIN_OPTION || etype == AIN_IFACE_WRAP)
				j++;
			break;
		}
		case AIN_REF_STRING:
			if (heap_slots[i] > 0 && (size_t)heap_slots[i] < heap_size
			    && heap[heap_slots[i]].type == VM_STRING)
				heap[heap_slots[i]].s = heap_ptrs[i];
			break;
		case AIN_REF_STRUCT:
		case AIN_REF_ARRAY_TYPE:
		case AIN_REF_DELEGATE:
			if (heap_slots[i] > 0 && (size_t)heap_slots[i] < heap_size
			    && heap[heap_slots[i]].type == VM_PAGE) {
				heap[heap_slots[i]].page = heap_ptrs[i];
			}
			break;
		case AIN_REF_ARRAY: {
			// v14: 1-slot — write back directly to heap slot
			// NOTE: Always write back unconditionally. The HLL function
			// (e.g. PushBack) may have freed the old page via free_page(),
			// making old->type unreliable (freed memory gets reused).
			if (heap_slots[i] > 0 && (size_t)heap_slots[i] < heap_size) {
				heap[heap_slots[i]].page = heap_ptrs[i];
			}
			break;
		}
		case AIN_REF_FUNC_TYPE:
			break;
		case AIN_ARRAY_TYPE:
			// Sys41VM doesn't make a copy when passing an array by value.
			if (ain->version <= 1)
				break;
			// fallthrough
		default:
			variable_fini(stack[stack_ptr+j], f->arguments[i].type.data, false);
			break;
		}
	}

	int slot;
	switch (f->return_type.data) {
	case AIN_VOID:
		break;
	case AIN_STRING:
	case AIN_HLL_PARAM:
		slot = heap_alloc_slot(VM_STRING);
		heap[slot].s = r.ref;
		stack_push(slot);
		break;
	case AIN_BOOL:
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wstrict-aliasing"
		stack_push(*(bool*)&r);
#pragma GCC diagnostic pop
		break;
	case AIN_REF_HLL_PARAM:
		// v14: The HLL function has already pushed the return value(s) directly
		// to the stack. Do NOT push the C return value here.
		break;
	default:
		stack_push(r);
		break;
	}

	hll_current_arg3 = -1;
	hll_self_slot = -1;
}

extern struct static_library lib_ACXLoader;
extern struct static_library lib_ACXLoaderP2;
extern struct static_library lib_ADVEngine;
extern struct static_library lib_ADVSYS;
extern struct static_library lib_AFAFactory;
extern struct static_library lib_AliceLogo;
extern struct static_library lib_AliceLogo2;
extern struct static_library lib_AliceLogo3;
extern struct static_library lib_AliceLogo4;
extern struct static_library lib_AliceLogo5;
extern struct static_library lib_AnteaterADVEngine;
extern struct static_library lib_Array;
extern struct static_library lib_BanMisc;
extern struct static_library lib_Bitarray;
extern struct static_library lib_CalcTable;
extern struct static_library lib_CGManager;
extern struct static_library lib_Clipboard;
extern struct static_library lib_ChipmunkSpriteEngine;
extern struct static_library lib_ChrLoader;
extern struct static_library lib_CommonSystemData;
extern struct static_library lib_Confirm;
extern struct static_library lib_Confirm2;
extern struct static_library lib_Confirm3;
extern struct static_library lib_CrayfishLogViewer;
extern struct static_library lib_Cursor;
extern struct static_library lib_DALKDemo;
extern struct static_library lib_DALKEDemo;
extern struct static_library lib_Data;
extern struct static_library lib_DataFile;
extern struct static_library lib_DrawDungeon;
extern struct static_library lib_DrawDungeon2;
extern struct static_library lib_DrawDungeon14;
extern struct static_library lib_DrawEffect;
extern struct static_library lib_DrawField;
extern struct static_library lib_DrawGraph;
extern struct static_library lib_DrawMovie;
extern struct static_library lib_DrawMovie2;
extern struct static_library lib_DrawMovie3;
extern struct static_library lib_DrawNumeral;
extern struct static_library lib_DrawPluginManager;
extern struct static_library lib_DrawRain;
extern struct static_library lib_DrawRipple;
extern struct static_library lib_DrawSimpleText;
extern struct static_library lib_DrawSnow;
extern struct static_library lib_EXWriter;
extern struct static_library lib_File;
extern struct static_library lib_File2;
extern struct static_library lib_FileDialog;
extern struct static_library lib_FileOperation;
extern struct static_library lib_FillAngle;
extern struct static_library lib_GoatGUIEngine;
extern struct static_library lib_Gpx2Plus;
extern struct static_library lib_GUIEngine;
extern struct static_library lib_HTTPDownloader;
extern struct static_library lib_IbisInputEngine;
extern struct static_library lib_InputDevice;
extern struct static_library lib_InputString;
extern struct static_library lib_InstallInfo;
extern struct static_library lib_KiwiSoundEngine;
extern struct static_library lib_LoadCG;
extern struct static_library lib_MADLoader;
extern struct static_library lib_MainEXFile;
extern struct static_library lib_MainSurface;
extern struct static_library lib_MamanyoDemo;
extern struct static_library lib_MamanyoSDemo;
extern struct static_library lib_MarmotModelEngine;
extern struct static_library lib_Math;
extern struct static_library lib_MapLoader;
extern struct static_library lib_MenuMsg;
extern struct static_library lib_MonsterInfo;
extern struct static_library lib_MsgLogManager;
extern struct static_library lib_MsgLogViewer;
extern struct static_library lib_MsgSkip;
extern struct static_library lib_MusicSystem;
extern struct static_library lib_NewFont;
extern struct static_library lib_OutputLog;
extern struct static_library lib_P3MapSprite;
extern struct static_library lib_P3SquareSprite;
extern struct static_library lib_PassRegister;
extern struct static_library lib_PastelChime2;
extern struct static_library lib_PartsEngine;
extern struct static_library lib_PixelRestore;
extern struct static_library lib_PlayDemo;
extern struct static_library lib_PlayMovie;
extern struct static_library lib_ReignEngine;
extern struct static_library lib_SealEngine;
extern struct static_library lib_SACT2;
extern struct static_library lib_SACTDX;
extern struct static_library lib_SengokuRanceFont;
extern struct static_library lib_Sound2ex;
extern struct static_library lib_SoundFilePlayer;
extern struct static_library lib_StoatSpriteEngine;
extern struct static_library lib_StretchHelper;
extern struct static_library lib_SystemService;
extern struct static_library lib_SystemServiceEx;
extern struct static_library lib_TapirEngine;
extern struct static_library lib_TextSurfaceManager;
extern struct static_library lib_Timer;
extern struct static_library lib_Toushin3Loader;
extern struct static_library lib_vmAnime;
extern struct static_library lib_vmArray;
extern struct static_library lib_vmCG;
extern struct static_library lib_vmChrLoader;
extern struct static_library lib_vmCursor;
extern struct static_library lib_vmDalkGaiden;
extern struct static_library lib_vmData;
extern struct static_library lib_vmDialog;
extern struct static_library lib_vmDrawGauge;
extern struct static_library lib_vmDrawMsg;
extern struct static_library lib_vmDrawNumber;
extern struct static_library lib_vmFile;
extern struct static_library lib_vmGraph;
extern struct static_library lib_vmGraphQuake;
extern struct static_library lib_vmKey;
extern struct static_library lib_vmMapLoader;
extern struct static_library lib_vmMsgLog;
extern struct static_library lib_vmMsgSkip;
extern struct static_library lib_vmMusic;
extern struct static_library lib_vmSound;
extern struct static_library lib_vmSprite;
extern struct static_library lib_vmString;
extern struct static_library lib_vmSurface;
extern struct static_library lib_vmSystem;
extern struct static_library lib_system;
extern struct static_library lib_String;
extern struct static_library lib_Delegate;
extern struct static_library lib_Float;
extern struct static_library lib_HashMap;
extern struct static_library lib_Int;
extern struct static_library lib_Sys43VM;
extern struct static_library lib_TextFile;
extern struct static_library lib_vmTimer;
extern struct static_library lib_ValueEncryption;
extern struct static_library lib_VSFile;

static struct static_library *static_libraries[] = {
	&lib_ACXLoader,
	&lib_ACXLoaderP2,
	&lib_ADVEngine,
	&lib_ADVSYS,
	&lib_AFAFactory,
	&lib_AliceLogo,
	&lib_AliceLogo2,
	&lib_AliceLogo3,
	&lib_AliceLogo4,
	&lib_AliceLogo5,
	&lib_AnteaterADVEngine,
	&lib_Array,
	&lib_BanMisc,
	&lib_Bitarray,
	&lib_CalcTable,
	&lib_CGManager,
	&lib_Clipboard,
	&lib_ChipmunkSpriteEngine,
	&lib_ChrLoader,
	&lib_CommonSystemData,
	&lib_Confirm,
	&lib_Confirm2,
	&lib_Confirm3,
	&lib_CrayfishLogViewer,
	&lib_Cursor,
	&lib_DALKDemo,
	&lib_DALKEDemo,
	&lib_Data,
	&lib_DataFile,
	&lib_DrawDungeon,
	&lib_DrawDungeon2,
	&lib_DrawDungeon14,
	&lib_DrawEffect,
	&lib_DrawField,
	&lib_DrawGraph,
	&lib_DrawMovie,
	&lib_DrawMovie2,
	&lib_DrawMovie3,
	&lib_DrawNumeral,
	&lib_DrawPluginManager,
	&lib_DrawRain,
	&lib_DrawRipple,
	&lib_DrawSimpleText,
	&lib_DrawSnow,
	&lib_EXWriter,
	&lib_File,
	&lib_File2,
	&lib_FileDialog,
	&lib_FileOperation,
	&lib_FillAngle,
	&lib_GoatGUIEngine,
	&lib_Gpx2Plus,
	&lib_GUIEngine,
	&lib_HTTPDownloader,
	&lib_IbisInputEngine,
	&lib_InputDevice,
	&lib_InputString,
	&lib_InstallInfo,
	&lib_KiwiSoundEngine,
	&lib_LoadCG,
	&lib_MADLoader,
	&lib_MainEXFile,
	&lib_MainSurface,
	&lib_MamanyoDemo,
	&lib_MamanyoSDemo,
	&lib_MarmotModelEngine,
	&lib_Math,
	&lib_MapLoader,
	&lib_MenuMsg,
	&lib_MonsterInfo,
	&lib_MsgLogManager,
	&lib_MsgLogViewer,
	&lib_MsgSkip,
	&lib_MusicSystem,
	&lib_NewFont,
	&lib_OutputLog,
	&lib_P3MapSprite,
	&lib_P3SquareSprite,
	&lib_PassRegister,
	&lib_PastelChime2,
	&lib_PartsEngine,
	&lib_PixelRestore,
	&lib_PlayDemo,
	&lib_PlayMovie,
	&lib_ReignEngine,
	&lib_SealEngine,
	&lib_SACT2,
	&lib_SACTDX,
	&lib_SengokuRanceFont,
	&lib_Sound2ex,
	&lib_SoundFilePlayer,
	&lib_StoatSpriteEngine,
	&lib_StretchHelper,
	&lib_SystemService,
	&lib_SystemServiceEx,
	&lib_TapirEngine,
	&lib_TextSurfaceManager,
	&lib_Timer,
	&lib_Toushin3Loader,
	&lib_vmAnime,
	&lib_vmArray,
	&lib_vmCG,
	&lib_vmChrLoader,
	&lib_vmCursor,
	&lib_vmDalkGaiden,
	&lib_vmData,
	&lib_vmDialog,
	&lib_vmDrawGauge,
	&lib_vmDrawMsg,
	&lib_vmDrawNumber,
	&lib_vmFile,
	&lib_vmGraph,
	&lib_vmGraphQuake,
	&lib_vmKey,
	&lib_vmMapLoader,
	&lib_vmMsgLog,
	&lib_vmMsgSkip,
	&lib_vmMusic,
	&lib_vmSound,
	&lib_vmSprite,
	&lib_vmString,
	&lib_vmSurface,
	&lib_vmSystem,
	&lib_vmTimer,
	&lib_ValueEncryption,
	&lib_VSFile,
	&lib_system,
	&lib_String,
	&lib_Delegate,
	&lib_Float,
	&lib_HashMap,
	&lib_Int,
	&lib_Sys43VM,
	&lib_TextFile,
	NULL
};

static ffi_type *ain_to_ffi_type(enum ain_data_type type)
{
	switch (type) {
	case AIN_VOID:
		return &ffi_type_void;
	case AIN_INT:
	case AIN_BOOL:
		return &ffi_type_sint32;
	case AIN_LONG_INT:
		return &ffi_type_sint64;
	case AIN_FLOAT:
		return &ffi_type_float;
	case AIN_STRING:
	case AIN_STRUCT:
	case AIN_FUNC_TYPE:
	case AIN_DELEGATE:
	case AIN_ARRAY_TYPE:
	case AIN_REF_TYPE:
	case AIN_HLL_PARAM:
	case AIN_REF_HLL_PARAM:
	case AIN_ARRAY:
	case AIN_OPTION:
	case AIN_UNKNOWN_TYPE_87:
	case AIN_IFACE:
	case AIN_IFACE_WRAP:
	case AIN_IMAIN_SYSTEM: // ???
		return &ffi_type_pointer;
	case AIN_WRAP: // v14: 2-slot ref [page, varno] — C receives int* pointer
		return &ffi_type_pointer;
	case AIN_ENUM2:
	case AIN_ENUM:
	case AIN_REF_ENUM:
	case AIN_HLL_FUNC:
	case AIN_HLL_FUNC_71:
		return &ffi_type_sint32;
	default:
		WARNING("Unhandled type in HLL function: %d (%s)", type, ain_strtype(ain, type, -1));
		return &ffi_type_sint32;  // fallback to int
	}
}

static void link_static_library_function(struct hll_function *dst, struct ain_hll_function *src,
					  void *funcptr)
{
	dst->fun = funcptr;

	dst->nr_args = src->nr_arguments;
	dst->args = xcalloc(dst->nr_args, sizeof(ffi_type*));

	for (unsigned int i = 0; i < dst->nr_args; i++) {
		if (src->arguments[i].type.data == AIN_WRAP
		    && src->arguments[i].type.array_type) {
			// wrap<value_type> → pointer (ref semantics)
			// wrap<ref_type> → sint32 (pass inner heap slot)
			enum ain_data_type inner = src->arguments[i].type.array_type->data;
			if (inner == AIN_INT || inner == AIN_FLOAT
			    || inner == AIN_BOOL || inner == AIN_LONG_INT)
				dst->args[i] = &ffi_type_pointer;
			else
				dst->args[i] = &ffi_type_sint32;
		} else {
			dst->args[i] = ain_to_ffi_type(src->arguments[i].type.data);
		}
	}
	dst->return_type = ain_to_ffi_type(src->return_type.data);

	if (ffi_prep_cif(&dst->cif, FFI_DEFAULT_ABI, dst->nr_args, dst->return_type, dst->args) != FFI_OK)
		ERROR("Failed to link HLL function");
}

/*
 * "Link" a library that has been compiled into the xsystem4 executable.
 */
static struct hll_function *link_static_library(struct ain_library *ainlib, struct static_library *lib)
{
	struct hll_function *dst = xcalloc(ainlib->nr_functions, sizeof(struct hll_function));

	for (int i = 0; i < ainlib->nr_functions; i++) {
		for (int j = 0; lib->functions[j].name; j++) {
			if (!strcmp(ainlib->functions[i].name, lib->functions[j].name)) {
				if (lib->functions[j].fun)
					link_static_library_function(&dst[i], &ainlib->functions[i], lib->functions[j].fun);
				break;
			}
		}
		if (ainlib->functions[i].nr_arguments >= HLL_MAX_ARGS)
			ERROR("Too many arguments to library function: %s", ainlib->functions[i].name);
	}

	return dst;
}

// v14 AIN uses different library names; map them to xsystem4 names
static const char *resolve_library_name(const char *name)
{
	if (!strcmp(name, "AnteaterADVLogList"))
		return "AnteaterADVEngine";
	return name;
}

static void library_run(struct static_library *lib, const char *name)
{
	for (int i = 0; lib->functions[i].name; i++) {
		if (!strcmp(lib->functions[i].name, name)) {
			((void(*)(void))lib->functions[i].fun)();
			break;
		}
	}
}

static void library_run_all(const char *name)
{
	for (int i = 0; i < ain->nr_libraries; i++) {
		const char *resolved = resolve_library_name(ain->libraries[i].name);
		for (int j = 0; static_libraries[j]; j++) {
			if (!strcmp(resolved, static_libraries[j]->name)) {
				library_run(static_libraries[j], name);
				break;
			}
		}
	}
}

static void link_libraries(void)
{
	if (libraries)
		return;

	libraries = xcalloc(ain->nr_libraries, sizeof(struct hll_function*));

	for (int i = 0; i < ain->nr_libraries; i++) {
		const char *resolved = resolve_library_name(ain->libraries[i].name);
		for (int j = 0; static_libraries[j]; j++) {
			if (!strcmp(resolved, static_libraries[j]->name)) {
				libraries[i] = link_static_library(&ain->libraries[i], static_libraries[j]);
				break;
			}
		}
		if (!libraries[i])
			WARNING("Unimplemented library: %s (lib[%d], %d funcs)", ain->libraries[i].name, i, ain->libraries[i].nr_functions);
	}
}

bool libraries_initialized = false;

void init_libraries(void)
{
	library_run_all("_PreLink");
	link_libraries();
	library_run_all("_ModuleInit");
	libraries_initialized = true;
}

void exit_libraries(void)
{
	if (libraries_initialized)
		library_run_all("_ModuleFini");
}

void static_library_replace(struct static_library *lib, const char *name, void *fun)
{
	for (int i = 0; lib->functions[i].name; i++) {
		if (!strcmp(lib->functions[i].name, name)) {
			lib->functions[i].fun = fun;
			return;
		}
	}
	ERROR("No library function '%s.%s'", lib->name, name);
}
