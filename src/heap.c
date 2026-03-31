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
#include <sys/mman.h>
#ifdef __APPLE__
#include <malloc/malloc.h>
#endif
#include <SDL.h>
#include "system4/string.h"
#include "vm.h"
#include "vm/heap.h"
#include "vm/page.h"
#include "xsystem4.h"

#define INITIAL_HEAP_SIZE  4096
// Exponential growth: double up to 4M, then linear 4M steps
#define HEAP_GROW_LINEAR_STEP  (4 * 1024 * 1024)

// Virtual memory reservation for heap array (8GB = ~340M slots).
// Only physical pages that are touched get allocated by the OS.
#define HEAP_MMAP_RESERVE  (48ULL * 1024 * 1024 * 1024)
#define HEAP_MAX_SLOTS     (HEAP_MMAP_RESERVE / sizeof(struct vm_pointer))

struct vm_pointer *heap = NULL;
size_t heap_size = 0;
uint32_t heap_next_seq;
static bool heap_is_mmap = false;  // true if heap was allocated via mmap

// Intrusive free list through heap[slot].seq (saves separate array allocation).
// When ref == 0, seq stores the next-free-slot index (-1 = end of list).
int32_t heap_free_head = -1;
size_t heap_free_count = 0;

// GC scan limit: only scan slots [2, heap_scan_limit) during mark/sweep.
// Updated by heap_alloc_slot (high-water mark) and heap_gc (after sweep).
static size_t heap_scan_limit = 2;

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
	if (heap_is_mmap) {
		// mmap reservation: just extend into pre-reserved virtual space
		if (new_size > HEAP_MAX_SLOTS) {
			VM_ERROR("heap_grow: exceeded mmap reservation (%zu > %zu)",
				new_size, (size_t)HEAP_MAX_SLOTS);
		}
	} else {
		heap = xrealloc(heap, sizeof(struct vm_pointer) * new_size);
	}
	// Zero-init new slots and chain into free list
	memset(&heap[heap_size], 0, (new_size - heap_size) * sizeof(struct vm_pointer));
	for (size_t i = heap_size; i < new_size; i++) {
		heap[i].seq = (i + 1 < new_size) ? (uint32_t)(i + 1) : (uint32_t)heap_free_head;
	}
	heap_free_head = (int32_t)heap_size;
	heap_free_count += new_size - heap_size;
	heap_size = new_size;
}

void heap_init(void)
{
	if (!heap) {
		// Try mmap reservation first (avoids realloc copies during growth)
		void *p = mmap(NULL, HEAP_MMAP_RESERVE,
			PROT_READ | PROT_WRITE,
			MAP_PRIVATE | MAP_ANON, -1, 0);
		if (p != MAP_FAILED) {
			heap = p;
			heap_is_mmap = true;
		} else {
			heap = xcalloc(1, INITIAL_HEAP_SIZE * sizeof(struct vm_pointer));
			heap_is_mmap = false;
		}
		heap_size = INITIAL_HEAP_SIZE;
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

static void heap_free_slot(int32_t slot);

// ---- Mark-and-sweep GC for reference cycle collection ----
// Uses generation counter to avoid memset of multi-GB mark array.
static uint8_t *gc_mark = NULL;
static size_t gc_mark_size = 0;
static uint8_t gc_gen = 0;  // generation counter (wraps at 255→1)
#define GC_IS_MARKED(slot) (gc_mark[slot] == gc_gen)
#define GC_SET_MARK(slot)  (gc_mark[slot] = gc_gen)
static unsigned long long gc_alloc_counter = 0;
static unsigned long long gc_last_alloc __attribute__((unused)) = 0;
// GC is triggered based on heap occupancy, not allocation count.
// Only run when free slots drop below 10% of heap size.
// This avoids expensive GC sweeps during initialization/transition phases
// where millions of cycle-garbage objects are created and discarded quickly.
#define GC_FREE_RATIO_THRESHOLD 10  // trigger GC when free < 10% of heap

// GC inhibit counter: when > 0, GC is deferred (e.g. during alloc_struct)
static int gc_inhibit = 0;
void heap_gc_inhibit(void) { gc_inhibit++; }
void heap_gc_allow(void) { gc_inhibit--; }
#define GC_WORK_STACK_SIZE (1024 * 1024)  // 1M entries for v14 large arrays

// ---- Precise type-aware scanning ----
// Returns true if an AIN data type stores a heap slot reference.
static bool gc_type_is_ref(enum ain_data_type type)
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
	case AIN_IFACE:
		return true;
	case AIN_REF_TYPE:
		// REF types store [page_slot, var_index]; the page_slot is a ref
		return true;
	default:
		return false;
	}
}

// Per-struct-type ref bitmap cache: gc_struct_refmap[struct_no][member] = 1 if ref
static uint8_t **gc_struct_refmap = NULL;
static int gc_struct_refmap_size = 0;

// Build or return cached ref bitmap for a given struct page.
// Returns NULL for unknown/out-of-range struct (caller falls back to conservative).
static const uint8_t *gc_get_struct_refmap(struct page *p)
{
	int idx = p->index;
	if (idx < 0 || idx >= gc_struct_refmap_size)
		return NULL;
	if (gc_struct_refmap[idx])
		return gc_struct_refmap[idx];

	// Build bitmap using variable_type
	int nv = p->nr_vars;
	uint8_t *map = xcalloc(nv + 1, sizeof(uint8_t));  // +1 safety
	for (int j = 0; j < nv; j++) {
		enum ain_data_type vtype = variable_type(p, j, NULL, NULL);
		if (gc_type_is_ref(vtype))
			map[j] = 1;
	}
	gc_struct_refmap[idx] = map;
	return map;
}

// Initialize struct refmap cache from AIN metadata
static void gc_init_struct_cache(void)
{
	if (gc_struct_refmap) return;
	gc_struct_refmap_size = ain->nr_structures;
	gc_struct_refmap = xcalloc(gc_struct_refmap_size, sizeof(uint8_t *));
}

static void gc_mark_slot(int slot);

// Precisely scan page members, only processing variables that hold heap refs.
// Pushes page-type children onto work_stack for BFS traversal.
static void gc_scan_page(struct page *p, int *work_stack, int *work_count)
{
	if (!p) return;

	// DELEGATE_PAGE: stores 3-tuples (obj_slot, fun_no, seq).
	// obj_slot (every 3rd entry) is a heap reference that must be marked.
	if (p->type == DELEGATE_PAGE) {
		for (int j = 0; j < p->nr_vars; j += 3) {
			int child = p->values[j].i;
			if (child <= 1 || (size_t)child >= gc_mark_size) continue;
			if (GC_IS_MARKED(child) || heap[child].ref <= 0) continue;
			GC_SET_MARK(child);
			if (heap[child].type == VM_PAGE && heap[child].page) {
				if (*work_count < GC_WORK_STACK_SIZE) {
					work_stack[(*work_count)++] = child;
				} else {
					static int dg_overflow = 0;
					if (dg_overflow++ < 5)
						WARNING("GC WORK STACK OVERFLOW (delegate): child=%d (count=%d)",
							child, *work_count);
				}
			}
		}
		return;
	}

	// ARRAY_PAGE: for v14, scan all arrays conservatively because v14 uses
	// type-erased generic arrays that may store struct page references in
	// arrays typed as AIN_ARRAY_INT.  For pre-v14, use element type to skip
	// arrays that definitely contain no refs (int/float/bool).
	if (p->type == ARRAY_PAGE && (!ain || ain->version < 14)) {
		if (p->array.rank <= 1) {
			if (p->a_type != AIN_ARRAY && p->a_type != AIN_REF_ARRAY) {
				enum ain_data_type etype = array_type(p->a_type);
				if (!gc_type_is_ref(etype))
					return;  // int/float/bool array — no refs
			}
		}
	}

	// STRUCT_PAGE: use cached per-type ref bitmap
	const uint8_t *refmap = NULL;
	if (p->type == STRUCT_PAGE)
		refmap = gc_get_struct_refmap(p);

	for (int j = 0; j < p->nr_vars; j++) {
		if (refmap && !refmap[j])
			continue;
		int child = p->values[j].i;
		if (child <= 1 || (size_t)child >= gc_mark_size) continue;
		if (GC_IS_MARKED(child) || heap[child].ref <= 0) continue;
		GC_SET_MARK(child);
		if (heap[child].type == VM_PAGE && heap[child].page) {
			if (*work_count < GC_WORK_STACK_SIZE) {
				work_stack[(*work_count)++] = child;
			} else {
				static int overflow_warn = 0;
				if (overflow_warn++ < 5)
					WARNING("GC WORK STACK OVERFLOW: child=%d not scanned (count=%d)",
						child, *work_count);
			}
		}
	}
}

static int *gc_work_stack;
static int gc_work_stack_size;

static void gc_ensure_work_stack(void)
{
	if (!gc_work_stack) {
		gc_work_stack_size = GC_WORK_STACK_SIZE;
		gc_work_stack = xmalloc(gc_work_stack_size * sizeof(int));
	}
}

static void gc_mark_slot(int slot)
{
	// Iterative BFS to avoid stack overflow
	gc_ensure_work_stack();
	int *work_stack = gc_work_stack;
	int work_count = 0;

	GC_SET_MARK(slot);
	if (heap[slot].type == VM_PAGE && heap[slot].page)
		work_stack[work_count++] = slot;

	while (work_count > 0) {
		int s = work_stack[--work_count];
		struct page *p = heap[s].page;
		gc_scan_page(p, work_stack, &work_count);
	}
}

static void heap_gc(void);

// Public entry: run GC if inhibit allows and heap has grown enough.
// Called periodically from VM loop to collect cycle-garbage promptly.
void heap_gc_periodic(void)
{
	// TEMPORARILY DISABLED FOR DEBUGGING
	return;
	if (gc_inhibit > 0 || heap_size < 10000)
		return;
	heap_gc();
}

static void heap_gc(void)
{
	extern struct function_call call_stack[];
	extern int32_t call_stack_ptr;
	extern int32_t stack_ptr;
	// Initialize struct ref cache on first GC
	gc_init_struct_cache();

	// Advance generation counter (avoids memset of multi-GB array)
	gc_gen++;
	if (gc_gen == 0) gc_gen = 1;  // skip 0 (initial/cleared value)

	// Resize mark array if needed (only on first use or growth)
	if (gc_mark_size < heap_size) {
		free(gc_mark);
		gc_mark = xcalloc(heap_size, sizeof(uint8_t));
		gc_mark_size = heap_size;
	}

	// Mark roots: global page
	GC_SET_MARK(global_page_slot);
	if (heap[global_page_slot].page) {
		// Global page: scan all vars (only one page, conservative is fine)
		gc_ensure_work_stack();
		int *root_stack = gc_work_stack;
		int root_count = 0;
		gc_scan_page(heap[global_page_slot].page, root_stack, &root_count);
		while (root_count > 0) {
			int s = root_stack[--root_count];
			struct page *p = heap[s].page;
			gc_scan_page(p, root_stack, &root_count);
		}
	}

	// Mark roots: call stack pages and their members
	for (int i = 0; i < call_stack_ptr; i++) {
		int ps = call_stack[i].page_slot;
		if (ps > 1 && (size_t)ps < heap_size && heap[ps].ref > 0 && !GC_IS_MARKED(ps))
			gc_mark_slot(ps);
		int sp = call_stack[i].struct_page;
		if (sp > 1 && (size_t)sp < heap_size && heap[sp].ref > 0 && !GC_IS_MARKED(sp))
			gc_mark_slot(sp);
	}

	// Mark roots: VM value stack (conservative — stack values lack type info)
	for (int i = 0; i < stack_ptr && i < 65536; i++) {
		int v = stack[i].i;
		if (v > 1 && (size_t)v < heap_size && heap[v].ref > 0 && !GC_IS_MARKED(v))
			gc_mark_slot(v);
	}

	// Use heap_scan_limit instead of heap_size for sweep — skip unallocated tail.
	size_t scan_end = heap_scan_limit;

	// v14 GC mark coverage diagnostic removed
	// Combined sweep: find unreachable alive slots, free resources, rebuild free list.
	size_t swept = 0;
	size_t orphans = 0;
	size_t new_scan_limit = 2;  // track highest alive slot
	heap_free_head = -1;
	heap_free_count = 0;

	// Scan from high to low so free list ends with lowest slot at head
	for (size_t i = scan_end; i-- > 2; ) {
		if (heap[i].ref > 0) {
			if (!GC_IS_MARKED(i) && 0) {
				// DISABLED: v14 GC mark incomplete — skip cycle collection for now
					swept++;
				switch (heap[i].type) {
				case VM_PAGE:
					if (heap[i].page) {
						free_page(heap[i].page);
						heap[i].page = NULL;
					}
					break;
				case VM_STRING:
					if (heap[i].s) {
						free_string(heap[i].s);
						heap[i].s = NULL;
					}
					break;
				default:
					break;
				}
				heap[i].ref = 0;
				heap[i].type = 0;
				heap[i].seq = (uint32_t)heap_free_head;
				heap_free_head = (int32_t)i;
				heap_free_count++;
			} else {
				// Alive — track highest
				if (i + 1 > new_scan_limit)
					new_scan_limit = i + 1;
			}
		} else {
			// Dead slot (ref <= 0)
			if (heap[i].type != 0) {
				// Orphaned resources
				switch (heap[i].type) {
				case VM_PAGE:
					if (heap[i].page) {
						free_page(heap[i].page);
						heap[i].page = NULL;
					}
					break;
				case VM_STRING:
					if (heap[i].s) {
						free_string(heap[i].s);
						heap[i].s = NULL;
					}
					break;
				default:
					break;
				}
				heap[i].type = 0;
				orphans++;
			}
			heap[i].ref = 0;
			heap[i].seq = (uint32_t)heap_free_head;
			heap_free_head = (int32_t)i;
			heap_free_count++;
		}
	}
	// Also add unscanned tail slots [scan_end, heap_size) to free list
	for (size_t i = heap_size; i-- > scan_end; ) {
		if (heap[i].ref <= 0) {
			heap[i].ref = 0;
			heap[i].seq = (uint32_t)heap_free_head;
			heap_free_head = (int32_t)i;
			heap_free_count++;
		} else if (i + 1 > new_scan_limit) {
			new_scan_limit = i + 1;
		}
	}
	heap_scan_limit = new_scan_limit;

	if (swept > 0 || orphans > 0) {
		static unsigned gc_log_count = 0;
		if (++gc_log_count <= 3 || (gc_log_count & 4095) == 0)
			WARNING("heap_gc: swept %zu cycle-garbage, %zu orphans, free=%zu scan=%zu/%zu",
				swept, orphans, heap_free_count, scan_end, heap_size);
	}

#ifdef __APPLE__
	if (swept > 100000 || heap_free_count > 1000000)
		malloc_zone_pressure_relief(NULL, 0);
#endif

	// Shrink heap: release tail memory back to OS after sweep.
	if (heap_is_mmap && heap_size > 2000000) {
		// heap_scan_limit already tracks highest alive slot + 1
		size_t keep = heap_scan_limit + 262144;
		if (keep < heap_size / 2) {
			// Release tail physical pages via madvise
			size_t old_size = heap_size;
			heap_size = keep;
			// Rebuild free list for kept region (already done above for full heap,
			// but we need to trim entries beyond keep)
			heap_free_head = -1;
			heap_free_count = 0;
			for (size_t i = keep; i-- > 2; ) {
				if (heap[i].ref == 0) {
					heap[i].seq = (uint32_t)heap_free_head;
					heap_free_head = (int32_t)i;
					heap_free_count++;
				}
			}
			uintptr_t start = (uintptr_t)&heap[keep];
			uintptr_t end = (uintptr_t)&heap[old_size];
			// Align to 16K page boundary (Apple Silicon)
			uintptr_t aligned = (start + 16383) & ~(uintptr_t)16383;
			if (aligned < end) {
				memset((void*)aligned, 0, end - aligned);
				madvise((void*)aligned, end - aligned, MADV_FREE);
			}
			WARNING("heap_gc: shrunk heap %zu → %zu (released %zuMB)",
				old_size, keep,
				(end - aligned) / (1024*1024));
		}
	}
}

int32_t heap_alloc_slot(enum vm_pointer_type type)
{
	gc_alloc_counter++;
	// Trigger GC when free list is nearly exhausted.
	// For large heaps (>=4M), trigger immediately.
	// For moderate heaps (>=10K), trigger at most every 5 seconds to clean
	// cycle-garbage (e.g. CParts with delegate cycles) without hurting perf.
	if (gc_inhibit <= 0 && heap_free_count < 1024) {
		bool do_gc = false;
		if (heap_size >= 4000000) {
			do_gc = true;
		} else if (heap_size >= 10000) {
			static uint32_t last_moderate_gc = 0;
			uint32_t now = SDL_GetTicks();
			if (now - last_moderate_gc >= 5000) {
				last_moderate_gc = now;
				do_gc = true;
			}
		}
		if (do_gc) {
			heap_gc();
		}
	}

	if (heap_free_head < 0) {
		size_t new_size;
		if (heap_size < HEAP_GROW_LINEAR_STEP)
			new_size = heap_size * 2;  // exponential up to 4M
		else
			new_size = heap_size + HEAP_GROW_LINEAR_STEP;  // linear 4M steps
		heap_grow(new_size);
	}
	int32_t slot = heap_free_head;
	// Validate free list: skip corrupt or in-use entries
	int skip = 0;
	while (slot >= 0 && (size_t)slot < heap_size && heap[slot].ref != 0 && skip++ < 64) {
		slot = (int32_t)heap[slot].seq;
	}
	if (skip > 0) {
		static int skip_warn = 0;
		if (skip_warn++ < 10)
			WARNING("heap_alloc_slot: skipped %d in-use entries (head=%d found=%d ref=%d)",
				skip, heap_free_head, slot,
				(slot >= 0 && (size_t)slot < heap_size) ? heap[slot].ref : -999);
	}
	if (slot < 0 || (size_t)slot >= heap_size || heap[slot].ref != 0) {
		// Free list is exhausted or corrupt — grow heap
		static int grow_warn = 0;
		if (grow_warn++ < 5)
			WARNING("heap_alloc_slot: free list exhausted/corrupt (head=%d slot=%d heap_size=%zu skip=%d) — growing",
				heap_free_head, slot, heap_size, skip);
		heap_grow(heap_size + HEAP_GROW_LINEAR_STEP);
		slot = heap_free_head;
	}
	heap_free_head = (int32_t)heap[slot].seq;
	heap_free_count--;
	heap[slot].ref = 1;
	heap[slot].seq = heap_next_seq++;
	heap[slot].type = type;
	if ((size_t)(slot + 1) > heap_scan_limit)
		heap_scan_limit = slot + 1;
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
	if (unlikely((size_t)slot >= heap_size)) {
		return;
	}
	// Guard: if ref is already 0, this is a double-free — skip silently.
	// This happens regularly after GC sweep adds slots to free list,
	// then VM code unrefs the same slots.
	if (unlikely(heap[slot].ref == 0 && heap[slot].type == 0)) {
		return;
	}
	heap[slot].ref = 0;
	heap[slot].type = 0;
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
	// actual_ref == 1, about to become 0
	static bool deferred_processing = false;
	heap[slot].ref = 0;

	// Deferred iterative free — fixed-size queue.
	// Items that don't fit are left with ref=0 for GC to reclaim.
	{
		#define DEFERRED_QUEUE_MAX (1 << 20)  // 1M entries
		static int deferred_queue[DEFERRED_QUEUE_MAX];
		static int deferred_count = 0;

		if (deferred_count < DEFERRED_QUEUE_MAX) {
			deferred_queue[deferred_count++] = slot;
		} else {
			// Queue full — free resources directly without recursing
			// (free_page, not delete_page, to avoid cascading heap_unref).
			// Children keep their ref counts; GC will sweep them as unreachable.
			switch (heap[slot].type) {
			case VM_PAGE:
				if (heap[slot].page) {
					free_page(heap[slot].page);
					heap[slot].page = NULL;
				}
				break;
			case VM_STRING:
				if (heap[slot].s) {
					free_string(heap[slot].s);
					heap[slot].s = NULL;
				}
				break;
			default:
				break;
			}
			heap[slot].type = 0;
			heap_free_slot(slot);
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
#ifdef __APPLE__
			// Periodically tell macOS malloc to return freed memory to the OS.
			// Without this, freed small allocations stay compressed and accumulate.
			{
				static unsigned long long relief_counter = 0;
				relief_counter++;
				if (relief_counter % 10000 == 0) {
					malloc_zone_pressure_relief(NULL, 0);
				}
			}
#endif
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
	// Validate pointer alignment (struct string requires 4-byte alignment)
	if (unlikely(((uintptr_t)heap[index].s & 0x3) != 0 || (uintptr_t)heap[index].s < 0x100000)) {
		heap[index].s = NULL;
		return &EMPTY_STRING;
	}
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
		// Save refcount: delete_page sets ref=0 (intended for heap_unref
		// deferred-free path), but here the slot is being REUSED, not freed.
		int saved_ref = heap[lval].ref;
		delete_page(lval);
		heap[lval].ref = saved_ref;
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
