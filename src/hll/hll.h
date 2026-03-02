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

/* Write a string to a wrap<string> reference (pageno, varno) */
static inline void wrap_set_string(int pageno, int varno, struct string *s)
{
	if (pageno < 0 || (size_t)pageno >= heap_size) return;
	if (heap[pageno].type == VM_PAGE && heap[pageno].page
	    && varno >= 0 && varno < heap[pageno].page->nr_vars) {
		/* values[varno] holds the string heap slot */
		int old = heap[pageno].page->values[varno].i;
		int ns = heap_alloc_slot(VM_STRING);
		heap[ns].s = s;
		heap[pageno].page->values[varno].i = ns;
		if (old > 0) heap_unref(old);
	} else if (heap[pageno].type == VM_STRING) {
		/* Direct string slot (legacy): replace in place */
		if (heap[pageno].s)
			free_string(heap[pageno].s);
		heap[pageno].s = s;
	}
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
