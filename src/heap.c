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
#include <assert.h>
#include "system4/string.h"
#include "vm.h"
#include "vm/heap.h"
#include "vm/page.h"
#include "xsystem4.h"

#define INITIAL_HEAP_SIZE  4096
#define HEAP_ALLOC_STEP    4096

struct vm_pointer *heap = NULL;
size_t heap_size = 0;
uint32_t heap_next_seq;

// Heap free list
// This is a list of unused indices into the 'heap' array.
int32_t *heap_free_stack = NULL;
size_t heap_free_ptr = 0;

static const char *vm_ptrtype_strtab[] = {
	[VM_PAGE] = "VM_PAGE",
	[VM_STRING] = "VM_STRING",
};

static const char *vm_ptrtype_string(enum vm_pointer_type type) {
	if (type < NR_VM_POINTER_TYPES)
		return vm_ptrtype_strtab[type];
	return "INVALID POINTER TYPE";
}

void heap_grow(size_t new_size)
{
	assert(new_size > heap_size);
	heap = xrealloc(heap, sizeof(struct vm_pointer) * new_size);
	heap_free_stack = xrealloc(heap_free_stack, sizeof(int32_t) * new_size);
	for (size_t i = heap_size; i < new_size; i++) {
		heap[i].ref = 0;
		heap_free_stack[i] = i;
	}
	heap_size = new_size;
}

void heap_init(void)
{
	if (!heap) {
		heap_size = INITIAL_HEAP_SIZE;
		heap = xcalloc(1, INITIAL_HEAP_SIZE * sizeof(struct vm_pointer));
		heap_free_stack = xmalloc(INITIAL_HEAP_SIZE * sizeof(int32_t));
	} else {
		memset(heap, 0, heap_size * sizeof(struct vm_pointer));
	}

	for (size_t i = 0; i < heap_size; i++) {
		heap_free_stack[i] = i;
	}
	heap_free_ptr = 1; // global page at index 0
	heap_next_seq = 1;
}

int32_t heap_alloc_slot(enum vm_pointer_type type)
{
	if (heap_free_ptr >= heap_size) {
		heap_grow(heap_size+HEAP_ALLOC_STEP);
	}

	int32_t slot = heap_free_stack[heap_free_ptr++];
	if (unlikely(slot == 0)) {
		WARNING("heap_alloc_slot: BUG! allocated slot 0 (global page) type=%d free_ptr=%zu", type, heap_free_ptr);
		slot = heap_free_stack[heap_free_ptr++];
	}
	heap[slot].ref = 1;
	heap[slot].seq = heap_next_seq++;
	heap[slot].type = type;
	heap[slot].page = NULL;  // Clear stale pointer from previous allocation
#ifdef DEBUG_HEAP
	heap[slot].alloc_addr = instr_ptr;
	memset(heap[slot].ref_addr, 0, sizeof(heap[slot].ref_addr));
	heap[slot].ref_nr = 0;
	memset(heap[slot].deref_addr, 0, sizeof(heap[slot].deref_addr));
	heap[slot].deref_nr = 0;
	heap[slot].free_addr = 0;
#endif
	return slot;
}

static void heap_free_slot(int32_t slot)
{
	if (unlikely(slot == 0)) {
		WARNING("heap_free_slot: BUG! freeing slot 0 (global page)");
		return;
	}
	heap[slot].seq = 0;
	heap_free_stack[--heap_free_ptr] = slot;
}

static void heap_double_free(int32_t slot)
{
#ifdef DEBUG_HEAP
		WARNING("double free of slot %d (%s)\nOriginally allocated at %X\nOriginally freed at %X",
			 slot, vm_ptrtype_string(heap[slot].type),
			 heap[slot].alloc_addr, heap[slot].free_addr);
#else
		WARNING("double free of slot %d (%s)", slot, vm_ptrtype_string(heap[slot].type));
#endif
}

void heap_ref(int32_t slot)
{
	if (slot <= 0 || (size_t)slot >= heap_size)
		return;
	// DIAG: trace ref changes for CASTask (struct#504)
	if (heap[slot].type == VM_PAGE && heap[slot].page
	    && heap[slot].page->type == STRUCT_PAGE && heap[slot].page->index == 504) {
		static int ct_ref_log = 0;
		if (ct_ref_log++ < 20) {
			extern struct ain *ain;
			extern struct function_call call_stack[];
			extern int32_t call_stack_ptr;
			extern size_t instr_ptr;
			int caller_fno = call_stack_ptr > 0 ? call_stack[call_stack_ptr-1].fno : -1;
			WARNING("CT_REF: slot=%d ref=%d→%d caller=%d '%s' ip=0x%lX",
				slot, heap[slot].ref, heap[slot].ref + 1,
				caller_fno,
				(ain && caller_fno >= 0 && caller_fno < ain->nr_functions) ? ain->functions[caller_fno].name : "?",
				(unsigned long)instr_ptr);
		}
	}
	heap[slot].ref++;
#ifdef DEBUG_HEAP
	heap[slot].ref_addr[heap[slot].ref_nr++ % 16] = instr_ptr;
#endif
}

// Does this type hold a heap slot index? Must match variable_fini's list exactly.
static bool type_holds_heap_slot(enum ain_data_type type)
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
		return true;
	default:
		return false;
	}
}

// v14: Boolean array marking heap slots reachable from global page + call frames.
// Rebuilt every 10K instructions. O(1) lookup via grp_flags[slot].
// Uses tracked marking to avoid clearing the entire heap-sized array.
static bool *grp_flags = NULL;
static size_t grp_flags_size = 0;
static unsigned long long grp_rebuild_insn = 0;
static int *grp_marked = NULL;     // list of marked slot indices
static int grp_marked_count = 0;
static int grp_marked_cap = 0;

static inline void grp_mark_slot(int slot)
{
	if (slot <= 0 || (size_t)slot >= grp_flags_size) return;
	if (grp_flags[slot]) return;
	grp_flags[slot] = true;
	if (grp_marked_count < grp_marked_cap)
		grp_marked[grp_marked_count++] = slot;
}

// Helper: mark all struct page members (2 levels deep) in grp_flags
static void grp_mark_struct_members(int struct_slot)
{
	if (struct_slot <= 0 || (size_t)struct_slot >= heap_size) return;
	if (heap[struct_slot].type != VM_PAGE || !heap[struct_slot].page) return;
	if (heap[struct_slot].page->type != STRUCT_PAGE) return;

	struct page *p1 = heap[struct_slot].page;
	for (int j = 0; j < p1->nr_vars; j++) {
		int slot_j = p1->values[j].i;
		grp_mark_slot(slot_j);

		// Level 2: members of members
		if (slot_j <= 0 || (size_t)slot_j >= heap_size) continue;
		if (heap[slot_j].type != VM_PAGE || !heap[slot_j].page) continue;
		if (heap[slot_j].page->type != STRUCT_PAGE) continue;

		struct page *p2 = heap[slot_j].page;
		for (int k = 0; k < p2->nr_vars; k++) {
			grp_mark_slot(p2->values[k].i);
		}
	}
}

static void grp_rebuild(void)
{
	extern struct ain *ain;
	if (!ain || !heap[0].page) return;

	// Resize arrays if heap grew
	if (grp_flags_size < heap_size) {
		grp_flags = xrealloc(grp_flags, heap_size * sizeof(bool));
		memset(grp_flags + grp_flags_size, 0, (heap_size - grp_flags_size) * sizeof(bool));
		grp_flags_size = heap_size;
	}
	if (!grp_marked) {
		grp_marked_cap = 8192;
		grp_marked = xmalloc(grp_marked_cap * sizeof(int));
	}

	// Clear only previously marked slots (O(marked) instead of O(heap_size))
	for (int i = 0; i < grp_marked_count; i++) {
		int ms = grp_marked[i];
		if (ms >= 0 && (size_t)ms < grp_flags_size)
			grp_flags[ms] = false;
	}
	grp_marked_count = 0;

	// Part 1: global page (3 levels deep)
	struct page *global = heap[0].page;
	for (int i = 0; i < global->nr_vars; i++) {
		enum ain_data_type gtype = variable_type(global, i, NULL, NULL);
		if (!type_holds_heap_slot(gtype)) continue;

		int slot_i = global->values[i].i;
		grp_mark_slot(slot_i);
		grp_mark_struct_members(slot_i);
	}

	// Call-frame struct pages are handled by the fast inline check in heap_unref,
	// not here in the cached set.
}

// O(1) check with periodic rebuild (every 50K instructions).
static bool slot_reachable_from_global(int target_slot)
{
	extern struct ain *ain;
	if (!ain || ain->version < 14) return false;
	if (target_slot <= 0 || (size_t)target_slot >= heap_size) return false;

	extern unsigned long long vm_call_get_insn_count(void);
	unsigned long long insn = vm_call_get_insn_count();
	if (!grp_flags || insn - grp_rebuild_insn > 500000) {
		grp_rebuild();
		grp_rebuild_insn = insn;
	}

	return (size_t)target_slot < grp_flags_size && grp_flags[target_slot];
}

void heap_unref(int slot)
{
	// Never unref the global page (slot 0) or invalid slots
	if (slot <= 0 || (size_t)slot >= heap_size) {
		return;
	}
	// DIAG: trace unref for CASTask (struct#504)
	if (heap[slot].type == VM_PAGE && heap[slot].page
	    && heap[slot].page->type == STRUCT_PAGE && heap[slot].page->index == 504) {
		static int ct_unref_log = 0;
		if (ct_unref_log++ < 20) {
			extern struct ain *ain;
			extern struct function_call call_stack[];
			extern int32_t call_stack_ptr;
			extern size_t instr_ptr;
			int caller_fno = call_stack_ptr > 0 ? call_stack[call_stack_ptr-1].fno : -1;
			WARNING("CT_UNREF: slot=%d ref=%d→%d caller=%d '%s' ip=0x%lX",
				slot, heap[slot].ref, heap[slot].ref - 1,
				caller_fno,
				(ain && caller_fno >= 0 && caller_fno < ain->nr_functions) ? ain->functions[caller_fno].name : "?",
				(unsigned long)instr_ptr);
		}
	}
	if (unlikely(heap[slot].ref <= 0)) {
		return;
	}
	if (heap[slot].ref > 1) {
		heap[slot].ref--;
		return;
	}
	// About to free this slot — check if any active call frame still uses it.
	// function_return() decrements call_stack_ptr BEFORE calling heap_unref,
	// so the returning function's frame is already removed from the stack.
	{
		extern struct function_call call_stack[];
		extern int32_t call_stack_ptr;
		bool parent_uses = false;
		for (int i = 0; i < call_stack_ptr; i++) {
			if (call_stack[i].page_slot == slot) {
				if (!parent_uses) {
					static int lpf_warn = 0;
					if (lpf_warn++ < 3) {
						extern struct ain *ain;
						int ff = call_stack[i].fno;
						int top_fno = call_stack_ptr > 0 ? call_stack[call_stack_ptr-1].fno : -1;
						WARNING("LIVE_PAGE_FREE blocked: slot=%d frame[%d] fno=%d '%s' "
							"caller=%d '%s' ip=0x%lX csp=%d",
							slot, i, ff,
							(ain && ff >= 0 && ff < ain->nr_functions) ? ain->functions[ff].name : "?",
							top_fno,
							(ain && top_fno >= 0 && top_fno < ain->nr_functions) ? ain->functions[top_fno].name : "?",
							(unsigned long)instr_ptr, call_stack_ptr);
					}
				}
				parent_uses = true;
			}
		}
		if (parent_uses) {
			// Keep the slot alive — a parent frame still references it.
			// Bumping ref prevents cascading corruption from freeing live pages.
			heap[slot].ref = 1;
			return;
		}
	}
	// v14: protect objects reachable from active call frames or global page.
	{
		extern struct ain *ain;
		if (ain && ain->version >= 14 && heap[slot].type == VM_PAGE) {
			// Fast check: is this slot a direct member of any active call frame's
			// struct page? O(frames × members) ≈ O(100), no caching needed.
			for (int ci = 0; ci < call_stack_ptr; ci++) {
				int sp_slot = call_stack[ci].struct_page;
				if (sp_slot <= 0 || (size_t)sp_slot >= heap_size) continue;
				if (heap[sp_slot].type != VM_PAGE || !heap[sp_slot].page) continue;
				struct page *sp = heap[sp_slot].page;
				for (int mi = 0; mi < sp->nr_vars; mi++) {
					if (sp->values[mi].i == slot) {
						static int cfp_warn = 0;
						if (cfp_warn++ < 3)
							WARNING("V14_MEMBER_PROTECT: slot=%d member[%d] of struct_page=%d frame[%d]",
								slot, mi, sp_slot, ci);
						heap[slot].ref = 1;
						return;
					}
				}
			}
			// Cached check: global page reachability (O(1) lookup)
			if (slot_reachable_from_global(slot)) {
				static int grp_warn = 0;
				if (grp_warn++ < 3)
					WARNING("V14_GLOBAL_PROTECT: slot=%d reachable from global page, keeping alive", slot);
				heap[slot].ref = 1;
				return;
			}
		}
	}
	heap[slot].ref = 0;

	// Deferred iterative free: instead of recursively calling delete_page
	// (which calls variable_fini → heap_unref → delete_page → ...),
	// add the slot to a deferred free queue and process iteratively.
	// This prevents stack overflow on deep object graphs (Dohna Dohna's
	// SceneLogo cleanup creates 20+ levels of recursive heap_unref).
	{
		static int deferred_queue[65536];
		static int deferred_count = 0;
		static bool deferred_processing = false;

		if (deferred_count < 65536) {
			deferred_queue[deferred_count++] = slot;
		} else {
			static int overflow_warn = 0;
			if (overflow_warn++ < 3)
				WARNING("heap_unref: deferred queue overflow, leaking slot %d", slot);
		}

		if (!deferred_processing) {
			deferred_processing = true;
			while (deferred_count > 0) {
				int s = deferred_queue[--deferred_count];
				// Slot might have been re-referenced during processing
				if (heap[s].ref > 0)
					continue;
				switch (heap[s].type) {
				case VM_PAGE:
					if (heap[s].page) {
						delete_page(s);
					}
					break;
				case VM_STRING:
					if (heap[s].s)
						free_string(heap[s].s);
					break;
				default:
					break;
				}
				heap_free_slot(s);
			}
			deferred_processing = false;
		}
	}
}

// XXX: special version of heap_unref which avoids calling destructors
void exit_unref(int slot)
{
	if (slot <= 0 || (size_t)slot >= heap_size) {
		if (slot != 0) { // slot=0 is common in v14 (uninitialized members), skip silently
			static int exit_oob_warn = 0;
			if (exit_oob_warn++ < 5)
				WARNING("exit_unref: out of bounds heap index: %d", slot);
		}
		return;
	}
	if (heap[slot].ref <= 0) {
		heap_double_free(slot);
		return;
	}
	if (heap[slot].ref > 1) {
#ifdef DEBUG_HEAP
		heap[slot].deref_addr[heap[slot].deref_nr++ % 16] = 0xDEADC0DE;
#endif
		heap[slot].ref--;
		return;
	}
	switch (heap[slot].type) {
	case VM_PAGE:
		if (heap[slot].page) {
			struct page *page = heap[slot].page;
			for (int i = 0; i < page->nr_vars; i++) {
				switch (variable_type(page, i, NULL, NULL)) {
				case AIN_STRING:
				case AIN_STRUCT:
				case AIN_DELEGATE:
				case AIN_ARRAY_TYPE:
				case AIN_REF_TYPE:
					if (page->values[i].i == -1)
						break;
					exit_unref(page->values[i].i);
					break;
				default:
					break;
				}
			}
			free_page(page);
		}
		break;
	case VM_STRING:
		free_string(heap[slot].s);
		break;
	}
	heap[slot].ref = 0;
	heap_free_slot(slot);
}

uint32_t heap_get_seq(int slot)
{
	return heap_index_valid(slot) ? heap[slot].seq : 0;
}

bool heap_index_valid(int index)
{
	return index >= 0 && (size_t)index < heap_size && heap[index].ref > 0;
}

bool page_index_valid(int index)
{
	return heap_index_valid(index) && heap[index].type == VM_PAGE;
}

bool string_index_valid(int index)
{
	return heap_index_valid(index) && heap[index].type == VM_STRING;
}

struct page *heap_get_page(int index)
{
	if (unlikely(!page_index_valid(index))) {
		static int page_warn_count = 0;
		if (page_warn_count++ < 5) {
			int fno = call_stack_ptr > 0 ? call_stack[call_stack_ptr-1].fno : -1;
			if (index >= 0 && (size_t)index < heap_size)
				WARNING("heap_get_page: invalid page index %d (ref=%d type=%d) ip=0x%lX fno=%d '%s'",
					index, heap[index].ref, heap[index].type,
					(unsigned long)instr_ptr, fno,
					(fno >= 0 && fno < ain->nr_functions) ? ain->functions[fno].name : "?");
			else
				WARNING("heap_get_page: invalid page index %d (out of range, heap_size=%zu) ip=0x%lX fno=%d",
					index, heap_size, (unsigned long)instr_ptr, fno);
		}
		return NULL;
	}
	return heap[index].page;
}

struct string *heap_get_string(int index)
{
	if (unlikely(!string_index_valid(index))) {
		static int str_warn_count = 0;
		if (str_warn_count++ < 5) {
			int fno = call_stack_ptr > 0 ? call_stack[call_stack_ptr-1].fno : -1;
			if (index >= 0 && (size_t)index < heap_size) {
				if (heap[index].type == VM_PAGE && heap[index].page)
					WARNING("heap_get_string: invalid string index %d (ref=%d type=VM_PAGE page_type=%d a_type=%d nr_vars=%d) ip=0x%lX fno=%d",
						index, heap[index].ref, heap[index].page->type, heap[index].page->a_type, heap[index].page->nr_vars,
						(unsigned long)instr_ptr, fno);
				else
					WARNING("heap_get_string: invalid string index %d (ref=%d type=%d) ip=0x%lX fno=%d",
						index, heap[index].ref, heap[index].type, (unsigned long)instr_ptr, fno);
			} else
				WARNING("heap_get_string: invalid string index %d (out of range, heap_size=%zu) ip=0x%lX fno=%d",
					index, heap_size, (unsigned long)instr_ptr, fno);
		}
		return &EMPTY_STRING;
	}
	return heap[index].s;
}

static int delegate_page_warn_count = 0;
struct page *heap_get_delegate_page(int index)
{
	struct page *page = heap_get_page(index);
	if (unlikely(page && page->type != DELEGATE_PAGE)) {
		if (delegate_page_warn_count++ < 5) {
			int fno = call_stack_ptr > 0 ? call_stack[call_stack_ptr-1].fno : -1;
			WARNING("heap_get_delegate_page: slot %d type=%d ip=0x%lX fno=%d '%s'",
				index, page->type, (unsigned long)instr_ptr, fno,
				(fno >= 0 && fno < ain->nr_functions) ? ain->functions[fno].name : "?");
		}
		return NULL;
	}
	return page;
}

void heap_set_page(int slot, struct page *page)
{
#ifdef DEBUG_HEAP
	if (unlikely(!page_index_valid(slot)))
		VM_ERROR("Invalid page index: %d", index);
#endif
	if (unlikely(slot == 0 && page && page->type != GLOBAL_PAGE)) {
		static int hp0_warn = 0;
		if (hp0_warn++ < 3)
			WARNING("heap_set_page: BUG! overwriting slot 0 (global page) with type=%d idx=%d",
				page->type, page->index);
		return;
	}
	heap[slot].page = page;
}

void heap_string_assign(int slot, struct string *string)
{
#ifdef DEBUG_HEAP
	if (unlikely(!string_index_valid(slot)))
		VM_ERROR("Tried to assign string to non-string slot");
#endif
	if (heap[slot].s) {
		free_string(heap[slot].s);
	}
	heap[slot].s = string_ref(string);
}

void heap_struct_assign(int lval, int rval)
{
	if (unlikely(lval == -1))
		VM_ERROR("Assignment to null-pointer");
	if (unlikely(lval <= 0)) {
		WARNING("heap_struct_assign: attempt to assign to slot %d (global page), rval=%d", lval, rval);
		return;
	}
	if (lval == rval)
		return;
	// Validate rval before accessing heap[rval].page
	if (unlikely(!page_index_valid(rval))) {
		static int rval_warn = 0;
		if (rval_warn++ < 10) {
			int fno = call_stack_ptr > 0 ? call_stack[call_stack_ptr-1].fno : -1;
			WARNING("heap_struct_assign: invalid rval=%d lval=%d ip=0x%lX fno=%d '%s'",
				rval, lval, (unsigned long)instr_ptr,
				fno, (fno >= 0 && fno < ain->nr_functions) ? ain->functions[fno].name : "?");
		}
		return;
	}
	if (heap[lval].page) {
		delete_page(lval);
	}
	heap_set_page(lval, copy_page(heap[rval].page));
}

int32_t heap_alloc_string(struct string *s)
{
	int slot = heap_alloc_slot(VM_STRING);
	heap[slot].s = s;
	return slot;
}

int32_t heap_alloc_page(struct page *page)
{
	int slot = heap_alloc_slot(VM_PAGE);
	heap[slot].page = page;
	return slot;
}

static void describe_page(struct page *page)
{
	if (!page) {
		sys_message("NULL_PAGE\n");
		return;
	}

	switch (page->type) {
	case GLOBAL_PAGE:
		sys_message("GLOBAL_PAGE\n");
		break;
	case LOCAL_PAGE:
		sys_message("LOCAL_PAGE: %s\n", display_sjis0(ain->functions[page->index].name));
		break;
	case STRUCT_PAGE:
		sys_message("STRUCT_PAGE: %s\n", display_sjis0(ain->structures[page->index].name));
		break;
	case ARRAY_PAGE:
		sys_message("ARRAY_PAGE: %s\n", display_sjis0(ain_strtype(ain, page->a_type, page->array.struct_type)));
		break;
	case DELEGATE_PAGE:
		// TODO: list function names
		sys_message("DELEGATE_PAGE\n");
		break;
	}
}

void heap_describe_slot(int slot)
{
	if (heap[slot].type == VM_STRING && heap[slot].s == &EMPTY_STRING)
		return;
#ifdef DEBUG_HEAP
	sys_message("[%d](%d)(%08X)[", slot, heap[slot].ref, heap[slot].alloc_addr);
	for (int i = 0; i < heap[slot].ref_nr && i < 16; i++) {
		if (i > 0)
			sys_message(",");
		sys_message("%08X", heap[slot].ref_addr[i]);
	}
	sys_message("][");
	for (int i = 0; i < heap[slot].deref_nr && i < 16; i++) {
		if (i > 0)
			sys_message(",");
		sys_message("%08X", heap[slot].deref_addr[i]);
	}
	sys_message("] = ");
#else
	sys_message("[%d](%d) = ", slot, heap[slot].ref);
#endif
	switch (heap[slot].type) {
	case VM_PAGE:
		describe_page(heap[slot].page);
		break;
	case VM_STRING:
		if (heap[slot].s) {
			sys_message("STRING: %s\n", display_sjis0(heap[slot].s->text));
		} else {
			sys_message("STRING: NULL\n");
		}
		break;
	default:
		sys_message("???\n");
		break;
	}
}
