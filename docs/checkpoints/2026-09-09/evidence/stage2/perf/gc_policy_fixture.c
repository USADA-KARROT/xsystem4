
#define VM_PRIVATE
#include <assert.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#ifdef __APPLE__
#include <malloc/malloc.h>
#endif
#include "system4/ain.h"
#include "system4/string.h"
#include "vm.h"
#include "vm/page.h"
#include "vm/heap.h"
struct ain *ain;
static struct vm_pointer pointers[2048];
struct vm_pointer *heap=pointers;
size_t heap_size,heap_free_count;
int32_t heap_free_head;
uint32_t heap_next_seq;
int global_page_slot=1;
struct function_call call_stack[4096];
int32_t call_stack_ptr,stack_ptr;
static union vm_value stack_storage[64];
union vm_value *stack=stack_storage;
static size_t heap_scan_limit;
static bool heap_is_mmap;
static int gc_inhibit;
static uint8_t *gc_mark;
static size_t gc_mark_size;
static uint8_t gc_gen;
#define GC_IS_MARKED(slot) (gc_mark[slot]==gc_gen)
#define GC_SET_MARK(slot) (gc_mark[slot]=gc_gen)
#define GC_WORK_STACK_SIZE (1<<20)
#define HEAP_GROW_LINEAR_STEP (4*1024*1024)
#define FIXTURE_FREE_TYPE ((enum vm_pointer_type)NR_VM_POINTER_TYPES)
static int *gc_work_stack,gc_work_stack_size;
static uint8_t **gc_struct_refmap;
static int gc_struct_refmap_size;
static unsigned long long gc_alloc_counter,gc_last_alloc;
static uint32_t gc_last_completed_ms;
static bool gc_has_run;
static const bool gc_collect_cycles=false;
static int scan_calls,gc_calls,grow_calls;
static uint32_t now_ms,gc_duration;
uint32_t SDL_GetTicks(void){return now_ms;}
enum ain_data_type variable_type(struct page*p,int n,int*s,int*r){return AIN_INT;}
enum ain_data_type array_type(enum ain_data_type type){return AIN_INT;}
void free_page(struct page*p){free(p);}
static void fixture_timed_gc(void){gc_calls++;now_ms+=gc_duration;}
void heap_grow(size_t size){grow_calls++;heap_size=size;heap_free_head=1500;heap_free_count=1;heap[1500].ref=0;heap[1500].seq=(uint32_t)-1;}
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
static void gc_init_struct_cache(void)
{
	if (gc_struct_refmap) return;
	gc_struct_refmap_size = ain->nr_structures;
	gc_struct_refmap = xcalloc(gc_struct_refmap_size, sizeof(uint8_t *));
}
static void gc_scan_page_impl(struct page *p, int *work_stack, int *work_count)
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
static void gc_scan_page(struct page*p,int*w,int*n){scan_calls++;gc_scan_page_impl(p,w,n);}
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
static void heap_gc(void)
{
	extern struct function_call call_stack[];
	extern int32_t call_stack_ptr;
	extern int32_t stack_ptr;
	if (gc_collect_cycles) {
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
			if (gc_collect_cycles && !GC_IS_MARKED(i)) {
				// Cycle collection requires complete v14 mark coverage.
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
static void heap_gc_before(void)
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
#define heap_gc fixture_timed_gc
int32_t heap_alloc_slot(enum vm_pointer_type type)
{
	gc_alloc_counter++;
	// Amortize pressure collections across actual allocation progress. A
	// collection that recovers little space must not run again on every
	// allocation. Start the cooldown after GC, including when GC is slow.
	if (gc_inhibit <= 0 && heap_size >= 10000 && heap_free_count < 1024) {
		size_t allocation_budget = heap_size / 8;
		if (allocation_budget < 1024)
			allocation_budget = 1024;
		uint32_t now = SDL_GetTicks();
		if (!gc_has_run || ((uint32_t)(now - gc_last_completed_ms) >= 5000
		    && gc_alloc_counter - gc_last_alloc >= allocation_budget)) {
			heap_gc();
			gc_last_completed_ms = SDL_GetTicks();
			gc_last_alloc = gc_alloc_counter;
			gc_has_run = true;
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
int32_t heap_alloc_slot_before(enum vm_pointer_type type)
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
#undef heap_gc

static struct page*page(int slot,enum page_type type,int n){
 struct page*p=calloc(1,sizeof(*p)+n*sizeof(union vm_value));assert(p);p->type=type;p->nr_vars=n;heap[slot]=(struct vm_pointer){.ref=1,.type=VM_PAGE,.page=p};return p;
}
static void graph(void){
 memset(heap,0,sizeof(pointers));heap_size=64;heap_scan_limit=16;heap_is_mmap=false;call_stack_ptr=stack_ptr=0;scan_calls=0;
 page(1,GLOBAL_PAGE,2)->values[0].i=2;heap[1].page->values[1].i=7;
 page(2,LOCAL_PAGE,1)->values[0].i=3;page(7,LOCAL_PAGE,1)->values[0].i=3;
 page(6,LOCAL_PAGE,1)->values[0].i=6; /* Existing collector leaves unreachable cycles. */
 for(int i=3;i<=4;i++)heap[i]=(struct vm_pointer){.ref=i==3?1:0,.type=VM_STRING,.s=make_string("data",4)};
 heap[8]=(struct vm_pointer){.ref=-1,.type=VM_STRING,.s=make_string("orphan",6)};
 heap[9]=(struct vm_pointer){.ref=1,.type=VM_STRING,.s=make_string("unreachable",11)};
}
struct snapshot {int ref[64],type[64],seq[64],has_resource[64],free_head;size_t free_count,scan_limit;};
static struct snapshot snapshot(void){struct snapshot s={0};for(int i=0;i<64;i++){s.ref[i]=heap[i].ref;s.type[i]=heap[i].ref==0 && heap[i].type==FIXTURE_FREE_TYPE ? VM_PAGE : heap[i].type;s.seq[i]=heap[i].seq;s.has_resource[i]=heap[i].page!=NULL;}s.free_head=heap_free_head;s.free_count=heap_free_count;s.scan_limit=heap_scan_limit;return s;}
static void clear_graph(void){for(int i=1;i<64;i++)if(heap[i].page){if(heap[i].type==VM_STRING)free_string(heap[i].s);else free_page(heap[i].page);heap[i].page=NULL;}}
static void schedule(size_t size,size_t available){
 memset(heap,0,sizeof(pointers));heap_size=size;heap_scan_limit=2;heap_free_head=1000;heap_free_count=available;heap_next_seq=1;
 for(int i=1000;i<2000;i++)heap[i].seq=i==1999?(uint32_t)-1:(uint32_t)(i+1);
 gc_alloc_counter=gc_last_alloc=gc_last_completed_ms=0;gc_has_run=false;gc_inhibit=0;gc_calls=grow_calls=0;now_ms=5000;gc_duration=0;
}
int main(void){
 struct ain a={.version=14};ain=&a;
 graph();heap_gc_before();struct snapshot before=snapshot();int scans_before=scan_calls;
 assert(scans_before>=3 && !heap[4].s && !heap[8].s && heap[6].ref==1 && heap[9].ref==1);clear_graph();
 graph();heap_gc();struct snapshot after=snapshot();assert(!memcmp(&before,&after,sizeof(before)) && scan_calls==0);clear_graph();
 puts("Production heap_gc snapshot: current source has no free tag, so tag normalization is inactive; live/dead/ref/resource/free-list/scan-limit identical; ref<=0 string orphans reclaimed; existing live cycles kept; mark traversals fall from >=3 to zero: PASS");
 schedule(4000000,1023);gc_duration=6000;for(int i=0;i<100;i++)heap_alloc_slot_before(VM_PAGE);assert(gc_calls==100);
 schedule(4000000,1023);gc_duration=6000;for(int i=0;i<100;i++)heap_alloc_slot(VM_PAGE);assert(gc_calls==1 && gc_last_completed_ms==11000 && gc_last_alloc==1);
 puts("Production allocator at >=4M/low-free, 100 allocations with 6s GC: old calls GC100 times; new calls once and records completion time: PASS");
 schedule(2000000,1023);gc_duration=6000;for(int i=0;i<3;i++)heap_alloc_slot_before(VM_PAGE);assert(gc_calls==3);
 schedule(2000000,1023);gc_duration=6000;for(int i=0;i<3;i++)heap_alloc_slot(VM_PAGE);assert(gc_calls==1);
 puts("Moderate heap: 6s GC no longer immediately satisfies its own 5s cooldown on next allocation: PASS");
 now_ms=gc_last_completed_ms+5000;heap_alloc_slot(VM_PAGE);assert(gc_calls==1); /* time alone insufficient */
 gc_alloc_counter=gc_last_alloc+heap_size/8;now_ms=gc_last_completed_ms+4999;heap_alloc_slot(VM_PAGE);assert(gc_calls==1);
 now_ms++;heap_alloc_slot(VM_PAGE);assert(gc_calls==2); /* both conditions met */
 gc_alloc_counter=gc_last_alloc+heap_size/8;gc_last_completed_ms=UINT32_MAX-2000;now_ms=2999;gc_duration=0;heap_alloc_slot(VM_PAGE);assert(gc_calls==3);
 puts("Both allocation budget and completion cooldown required; collections resume when eligible; SDL uint32 wrap handled: PASS");
 schedule(9999,1023);heap_alloc_slot(VM_PAGE);assert(!gc_calls);
 schedule(10000,1024);heap_alloc_slot(VM_PAGE);assert(!gc_calls);heap_alloc_slot(VM_PAGE);assert(gc_calls==1);
 schedule(10000,1023);gc_inhibit=1;heap_alloc_slot(VM_PAGE);assert(!gc_calls);gc_inhibit=0;heap_alloc_slot(VM_PAGE);assert(gc_calls==1);
 schedule(10000,1023);now_ms=4999;heap_alloc_slot(VM_PAGE);assert(gc_calls==1); /* Current pre-freelist policy: first eligible GC is immediate. */
 schedule(10000,0);gc_has_run=true;gc_last_completed_ms=now_ms;heap_free_head=-1;assert(heap_alloc_slot(VM_PAGE)==1500 && grow_calls==1 && !gc_calls);
 puts("Heap9999/10000, free1023/1024, inhibit/release, first eligible collection and exhausted free-list fallback: PASS");
 free(gc_mark);free(gc_work_stack);free(gc_struct_refmap);puts("GC production policy fixture: ALL PASS");return 0;
}
