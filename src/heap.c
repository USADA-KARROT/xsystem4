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
// Exponential growth: double up to 4M, then linear 4M steps
#define HEAP_GROW_LINEAR_STEP  (4 * 1024 * 1024)


struct vm_pointer *heap = NULL;
size_t heap_size = 0;
uint32_t heap_next_seq;

// Intrusive free list through heap[slot].seq (saves separate array allocation).
// When ref == 0, seq stores the next-free-slot index (-1 = end of list).
int32_t heap_free_head = -1;
size_t heap_free_count = 0;

// Global page heap slot. Initialized to 1 (not 0) to prevent null references
// (method_call skip returns, failed lookups, etc.) from aliasing to the global page.
int global_page_slot = 1;

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
	// Chain new slots into free list (low→high, prepend range to head)
	for (size_t i = heap_size; i < new_size; i++) {
		heap[i].ref = 0;
		heap[i].seq = (i + 1 < new_size) ? (uint32_t)(i + 1) : (uint32_t)heap_free_head;
	}
	heap_free_head = (int32_t)heap_size;
	heap_free_count += new_size - heap_size;
	heap_size = new_size;
}

void heap_init(void)
{
	if (!heap) {
		heap_size = INITIAL_HEAP_SIZE;
		heap = xcalloc(1, INITIAL_HEAP_SIZE * sizeof(struct vm_pointer));
	} else {
		memset(heap, 0, heap_size * sizeof(struct vm_pointer));
	}

	// Build free list starting at slot 2 (0=guard, 1=global page reserved)
	heap_free_head = -1;
	heap_free_count = 0;
	for (size_t i = heap_size; i > 2; ) {
		i--;
		heap[i].seq = (uint32_t)heap_free_head;
		heap_free_head = (int32_t)i;
		heap_free_count++;
	}
	heap_next_seq = 1;
}

int32_t heap_alloc_slot(enum vm_pointer_type type)
{
	if (heap_free_head < 0) {
		size_t new_size;
		if (heap_size < HEAP_GROW_LINEAR_STEP)
			new_size = heap_size * 2;  // exponential up to 4M
		else
			new_size = heap_size + HEAP_GROW_LINEAR_STEP;  // linear 4M steps
		heap_grow(new_size);
	}
	int32_t slot = heap_free_head;
	heap_free_head = (int32_t)heap[slot].seq;
	heap_free_count--;
	heap[slot].ref = 1;
	heap[slot].seq = heap_next_seq++;
	heap[slot].type = type;
	heap[slot].page = NULL;
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
	if (unlikely(slot <= 1)) {
		// slot 0 = null guard page, slot 1 = global page; never free these
		return;
	}
	heap[slot].seq = (uint32_t)heap_free_head;
	heap_free_head = slot;
	heap_free_count++;
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
	if (slot <= 1 || (size_t)slot >= heap_size)
		return;
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

// Recursively mark all page members reachable from a given slot.
// Uses iterative BFS with a work stack to avoid C stack overflow.
static int grp_work_stack[4096];
static int grp_work_count = 0;

static void grp_mark_reachable(int root_slot)
{
	if (root_slot <= 0 || (size_t)root_slot >= heap_size) return;
	if (heap[root_slot].type != VM_PAGE || !heap[root_slot].page) return;

	grp_work_count = 0;
	grp_work_stack[grp_work_count++] = root_slot;

	while (grp_work_count > 0) {
		int slot = grp_work_stack[--grp_work_count];
		if (slot <= 0 || (size_t)slot >= heap_size) continue;
		if (heap[slot].type != VM_PAGE || !heap[slot].page) continue;

		struct page *p = heap[slot].page;
		for (int j = 0; j < p->nr_vars; j++) {
			int child = p->values[j].i;
			if (child <= 0 || (size_t)child >= grp_flags_size) continue;
			if (grp_flags[child]) continue; // already marked
			grp_mark_slot(child);
			// If this child is a page, enqueue for further traversal
			if ((size_t)child < heap_size &&
			    heap[child].type == VM_PAGE && heap[child].page &&
			    grp_work_count < 4096) {
				grp_work_stack[grp_work_count++] = child;
			}
		}
	}
}

static void grp_rebuild(void)
{
	extern struct ain *ain;
	if (!ain || !heap[global_page_slot].page) return;

	// Resize arrays if heap grew
	if (grp_flags_size < heap_size) {
		grp_flags = xrealloc(grp_flags, heap_size * sizeof(bool));
		memset(grp_flags + grp_flags_size, 0, (heap_size - grp_flags_size) * sizeof(bool));
		grp_flags_size = heap_size;
	}
	if (!grp_marked) {
		grp_marked_cap = 65536;
		grp_marked = xmalloc(grp_marked_cap * sizeof(int));
	}

	// Clear only previously marked slots (O(marked) instead of O(heap_size))
	for (int i = 0; i < grp_marked_count; i++) {
		int ms = grp_marked[i];
		if (ms >= 0 && (size_t)ms < grp_flags_size)
			grp_flags[ms] = false;
	}
	grp_marked_count = 0;

	// Recursively mark all slots reachable from global page
	struct page *global = heap[global_page_slot].page;
	for (int i = 0; i < global->nr_vars; i++) {
		enum ain_data_type gtype = variable_type(global, i, NULL, NULL);
		if (!type_holds_heap_slot(gtype)) continue;

		int slot_i = global->values[i].i;
		grp_mark_slot(slot_i);
		grp_mark_reachable(slot_i);
	}
}

// O(1) check with periodic rebuild.
static bool slot_reachable_from_global(int target_slot)
{
	extern struct ain *ain;
	extern bool vm_in_alloc_phase;
	if (!ain || ain->version < 14) return false;
	if (target_slot <= 0 || (size_t)target_slot >= heap_size) return false;

	extern unsigned long long vm_call_get_insn_count(void);
	unsigned long long insn = vm_call_get_insn_count();
	// Periodic rebuild: 500K insns normally, 2M during alloc phase.
	// The recursive BFS marking ensures full transitive coverage.
	unsigned long long threshold = vm_in_alloc_phase ? 2000000ULL : 500000ULL;
	if (!grp_flags || insn - grp_rebuild_insn > threshold) {
		grp_rebuild();
		grp_rebuild_insn = insn;
	}

	return (size_t)target_slot < grp_flags_size && grp_flags[target_slot];
}


void heap_unref(int slot)
{
	// Never unref reserved slots (0=guard, 1=global page) or invalid slots
	if (slot <= 1 || (size_t)slot >= heap_size) {
		return;
	}
	// Strip temp flag for ref count checks
	int actual_ref = HEAP_REF(slot);
	if (unlikely(actual_ref <= 0)) {
		return;
	}
	if (actual_ref > 1) {
		heap[slot].ref--;  // decrement (preserves temp flag in high bits)
		return;
	}
	// actual_ref == 1, about to become 0: clear any temp flag too
	// (Fall through to free logic below)
	// Protection: prevent freeing objects still used by active call frames.
	// Use a bitmap for O(1) lookups instead of O(csp) per unref.
	static bool deferred_processing = false;
	static uint8_t *frame_protect_map = NULL;
	static size_t frame_protect_map_size = 0;
	// Track which entries were set for fast clearing
	static int fpm_dirty[512];
	static int fpm_dirty_count = 0;

	if (!deferred_processing) {
		extern struct function_call call_stack[];
		extern int32_t call_stack_ptr;
		extern struct ain *ain;

		// Grow bitmap if needed
		if (heap_size > frame_protect_map_size) {
			free(frame_protect_map);
			frame_protect_map_size = heap_size;
			frame_protect_map = calloc(heap_size, 1);
			fpm_dirty_count = 0;
		} else {
			// Clear only previously set entries (O(dirty) instead of O(heap_size))
			for (int i = 0; i < fpm_dirty_count; i++) {
				int idx = fpm_dirty[i];
				if ((size_t)idx < frame_protect_map_size)
					frame_protect_map[idx] = 0;
			}
			fpm_dirty_count = 0;
		}
		// Mark call stack page slots
		for (int i = 0; i < call_stack_ptr; i++) {
			int ps = call_stack[i].page_slot;
			if (ps > 0 && (size_t)ps < heap_size) {
				frame_protect_map[ps] = 1;
				if (fpm_dirty_count < 512) fpm_dirty[fpm_dirty_count++] = ps;
			}
		}
		// Also mark struct_page members of recent frames (top 16)
		if (ain && ain->version >= 14) {
			int start = call_stack_ptr > 16 ? call_stack_ptr - 16 : 0;
			for (int ci = start; ci < call_stack_ptr; ci++) {
				int sp_slot = call_stack[ci].struct_page;
				if (sp_slot <= 0 || (size_t)sp_slot >= heap_size) continue;
				if (heap[sp_slot].type != VM_PAGE || !heap[sp_slot].page) continue;
				struct page *sp = heap[sp_slot].page;
				for (int mi = 0; mi < sp->nr_vars; mi++) {
					int v = sp->values[mi].i;
					if (v > 0 && (size_t)v < heap_size) {
						frame_protect_map[v] = 1;
						if (fpm_dirty_count < 512) fpm_dirty[fpm_dirty_count++] = v;
					}
				}
			}
		}

		// Check this slot against the bitmap
		if ((size_t)slot < heap_size && frame_protect_map[slot]) {
			heap[slot].ref = 1;
			return;
		}
		// Global page reachability check
		if (ain && ain->version >= 14 && heap[slot].type == VM_PAGE) {
			if (slot_reachable_from_global(slot)) {
				heap[slot].ref = 1;
				return;
			}
		}
	} else {
		// During deferred processing: O(1) bitmap lookup only
		if ((size_t)slot < heap_size && frame_protect_map && frame_protect_map[slot]) {
			heap[slot].ref = 1;
			return;
		}
	}
	heap[slot].ref = 0;

	// Deferred iterative free
	{
		static int deferred_queue[65536];
		static int deferred_count = 0;

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
				// Skip if re-allocated (ref > 0) or already freed (ref < 0)
				if (heap[s].ref != 0)
					continue;
				// Mark as freed-in-progress to prevent double-free from
				// nested heap_unref calls during delete_page/destructor
				heap[s].ref = -1;
				switch (heap[s].type) {
				case VM_PAGE:
					if (heap[s].page) {
						delete_page(s);
					}
					break;
				case VM_STRING:
					if (heap[s].s) {
						free_string(heap[s].s);
						heap[s].s = NULL;
					}
					break;
				default:
					break;
				}
				heap[s].ref = 0;
				heap_free_slot(s);
			}
			deferred_processing = false;
		}
	}
}

// XXX: special version of heap_unref which avoids calling destructors
void exit_unref(int slot)
{
	if (slot <= 1 || (size_t)slot >= heap_size) {
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
	// v14: negative indices and 0 mean null reference (optional empty)
	if (index <= 0)
		return NULL;
	if (unlikely(!page_index_valid(index))) {
		// v14: freed slots (ref=0) and VM_STRING slots accessed as pages
		// happen in closure environments and optional types — silently return NULL.
		if ((size_t)index < heap_size && (heap[index].ref <= 0 || heap[index].type == VM_STRING))
			return NULL;
		{
			int fno = call_stack_ptr > 0 ? call_stack[call_stack_ptr-1].fno : -1;
			const char *fname = (fno >= 0 && fno < ain->nr_functions) ? ain->functions[fno].name : "?";
			if ((size_t)index < heap_size)
				WARNING("heap_get_page: invalid page index %d (ref=%d type=%d) ip=0x%lX fno=%d '%s'",
					index, HEAP_REF(index), heap[index].type,
					(unsigned long)instr_ptr, fno, fname);
			else
				WARNING("heap_get_page: invalid page index %d (out of range, heap_size=%zu) ip=0x%lX fno=%d '%s'",
					index, heap_size, (unsigned long)instr_ptr, fno, fname);
		}
		return NULL;
	}
	return heap[index].page;
}

struct string *heap_get_string(int index)
{
	// v14: -1 is the null/empty value for optional<string>
	if (index < 0)
		return &EMPTY_STRING;
	if (unlikely(!string_index_valid(index))) {
		return &EMPTY_STRING;
	}
	if (unlikely(!heap[index].s))
		return &EMPTY_STRING;
	return heap[index].s;
}

struct page *heap_get_delegate_page(int index)
{
	// v14: slot <=1 (guard/global) means null delegate (optional empty)
	if (index <= 1)
		return NULL;
	struct page *page = heap_get_page(index);
	if (unlikely(page && page->type != DELEGATE_PAGE)) {
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
	// Protect slot 1 (global page) from being overwritten by non-global pages.
	// Slot 0 (guard page) may be written to by stray references — allow it.
	if (unlikely(slot == 1 && page && page->type != GLOBAL_PAGE)) {
		static int hp1_warn = 0;
		if (hp1_warn++ < 1)
			WARNING("heap_set_page: BUG! overwriting global page (slot 1) with type=%d idx=%d",
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
