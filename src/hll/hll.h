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

#ifndef SYSTEM4_HLL_H
#define SYSTEM4_HLL_H

/*
 * DSL for implementing libraries.
 */

#include "vm.h"
#include "vm/heap.h"
#include "vm/page.h"
#include "system4.h"
#include "system4/string.h"

/*
 * wrap<T> helpers — v14 AIN_WRAP parameter convention.
 *
 * AIN_WRAP parameters are passed as 2-slot (pageno, varno) references:
 *   - For heap-allocated wrap<T> variables (X_REF 2 pattern):
 *       pageno = heap slot of the wrap page, varno = 0 (discriminant)
 *       => writes to heap[pageno].page->values[0]
 *   - For plain local variable out-params (PUSHLOCALPAGE+PUSH n pattern):
 *       pageno = local page heap slot, varno = var_index
 *       => writes to heap[pageno].page->values[varno]
 *
 * HLL C functions receive (int pageno, int varno) for each AIN_WRAP arg.
 * Use the wrap_set and wrap_get helpers below.
 */

/* Write an int to a wrap<int> reference (pageno, varno) */
static inline void wrap_set_int(int pageno, int varno, int value)
{
	if (pageno < 0 || (size_t)pageno >= heap_size) return;
	if (heap[pageno].type == VM_PAGE && heap[pageno].page
	    && varno >= 0 && varno < heap[pageno].page->nr_vars) {
		heap[pageno].page->values[varno].i = value;
	}
}

/* Read an int from a wrap<int> reference (pageno, varno) */
static inline int wrap_get_int(int pageno, int varno)
{
	if (pageno < 0 || (size_t)pageno >= heap_size) return 0;
	if (heap[pageno].type == VM_PAGE && heap[pageno].page
	    && varno >= 0 && varno < heap[pageno].page->nr_vars)
		return heap[pageno].page->values[varno].i;
	return 0;
}

/* Write a string to a WRAP output ref.
 * ptr points to page->values[varno] (the variable holding the string heap slot).
 * Allocates a new string heap slot, writes it to *ptr, unrefs old. */
static inline void wrap_set_string(int *ptr, struct string *s)
{
	if (!ptr) return;
	int old = *ptr;
	int ns = heap_alloc_slot(VM_STRING);
	heap[ns].s = s;
	*ptr = ns;
	if (old > 0) heap_unref(old);
}

/*
 * Write a string to a 1-slot wrap<string> argument.
 *
 * ffi passes wrap<string> as ffi_type_sint32 (link_static_library_function):
 * the C side gets the heap slot, never a pointer. In CN v14 bytecode the slot
 * is the VM_STRING slot of the target variable itself (".LOCALREF text", or
 * "X_REF 1" on a string member, see AFL_TextFile_ReadAll / gamesave read),
 * so the string object is replaced in place and every holder of that slot
 * sees the new text. A v14 wrap box (STRUCT_PAGE, index == -1, values[0] =
 * inner string slot) is also accepted.
 *
 * Takes ownership of s. Returns false (and frees s) when the slot is not a
 * live string or wrap box.
 */
static inline bool wrap_slot_set_string(int slot, struct string *s)
{
	if (slot > 0 && (size_t)slot < heap_size && HEAP_REF(slot) > 0) {
		if (heap[slot].type == VM_STRING) {
			if (heap[slot].s)
				free_string(heap[slot].s);
			heap[slot].s = s;
			return true;
		}
		struct page *box = heap[slot].type == VM_PAGE ? heap[slot].page : NULL;
		if (box && box->type == STRUCT_PAGE && box->index == -1 && box->nr_vars >= 1) {
			int inner = box->values[0].i;
			if (inner > 0 && (size_t)inner < heap_size && HEAP_REF(inner) > 0
			    && heap[inner].type == VM_STRING) {
				if (heap[inner].s)
					free_string(heap[inner].s);
				heap[inner].s = s;
			} else {
				box->values[0].i = heap_alloc_string(s);
				if (inner > 0)
					heap_unref(inner);
			}
			return true;
		}
	}
	free_string(s);
	return false;
}

/* Write a float to a wrap<float> reference (pageno, varno) */
static inline void wrap_set_float(int pageno, int varno, float value)
{
	if (pageno < 0 || (size_t)pageno >= heap_size) return;
	if (heap[pageno].type == VM_PAGE && heap[pageno].page
	    && varno >= 0 && varno < heap[pageno].page->nr_vars) {
		heap[pageno].page->values[varno].f = value;
	}
}

/* Write a bool to a wrap<bool> reference (pageno, varno) */
static inline void wrap_set_bool(int pageno, int varno, bool value)
{
	if (pageno < 0 || (size_t)pageno >= heap_size) return;
	if (heap[pageno].type == VM_PAGE && heap[pageno].page
	    && varno >= 0 && varno < heap[pageno].page->nr_vars) {
		heap[pageno].page->values[varno].i = value ? 1 : 0;
	}
}

/* Get the page from a wrap<struct/array/delegate> reference (pageno, varno).
 * Returns the page pointed to by the inner heap slot at values[varno]. */
static inline struct page *wrap_get_page(int pageno, int varno)
{
	if (pageno < 0 || (size_t)pageno >= heap_size) return NULL;
	if (heap[pageno].type == VM_PAGE && heap[pageno].page
	    && varno >= 0 && varno < heap[pageno].page->nr_vars) {
		int inner = heap[pageno].page->values[varno].i;
		if (inner > 0 && (size_t)inner < heap_size
		    && heap[inner].type == VM_PAGE)
			return heap[inner].page;
	}
	return NULL;
}

/* Set the inner heap slot of a wrap<struct/array/delegate> reference.
 * For wrap<array>, wrap<struct>, wrap<delegate>: values[varno] holds the inner heap slot.
 * Handles ref counting. */
static inline void wrap_set_slot(int pageno, int varno, int new_inner_slot)
{
	if (pageno < 0 || (size_t)pageno >= heap_size) return;
	if (heap[pageno].type == VM_PAGE && heap[pageno].page
	    && varno >= 0 && varno < heap[pageno].page->nr_vars) {
		int old = heap[pageno].page->values[varno].i;
		heap[pageno].page->values[varno].i = new_inner_slot;
		if (new_inner_slot > 0) heap_ref(new_inner_slot);
		if (old > 0) heap_unref(old);
	}
}

void static_library_replace(struct static_library *lib, const char *name, void *fun);
void static_library_register(struct static_library *lib, const char *name, void *fun);

/* The original's 521-word random number generator (Math.c): Math.SetSeed/Rand/
 * RandF use a global instance, Array.Shuffle a fresh one per call. */
struct sys4_rand521 {
	int idx;
	uint32_t w[521];
};
void sys4_rand521_seed(struct sys4_rand521 *r, uint32_t seed);
uint32_t sys4_rand521_next(struct sys4_rand521 *r);
uint32_t sys4_rand_time_seed(void);

#define HLL_WARN_UNIMPLEMENTED(rval, rtype, libname, fname, ...)	\
	static rtype libname ## _ ## fname(__VA_ARGS__) {		\
		WARNING("Unimplemented HLL function: " #libname "." #fname); \
		return rval;						\
	}

#define HLL_QUIET_UNIMPLEMENTED(rval, rtype, libname, fname, ...)	\
	static rtype libname ## _ ## fname(__VA_ARGS__) {		\
		return rval;						\
	}

#define HLL_EXPORT(fname, funptr) { .name = #fname, .fun = funptr }
#define HLL_TODO_EXPORT(fname, funptr) { .name = #fname, .fun = NULL }

#define HLL_LIBRARY(lname, ...)				\
	struct static_library lib_ ## lname = {		\
		.name = #lname,				\
		.functions = {				\
			__VA_ARGS__,			\
			{ .name = NULL, .fun = NULL }	\
		}					\
	}

#endif /* SYSTEM4_HLL_H */
