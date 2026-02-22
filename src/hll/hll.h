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
 * wrap<T> helpers — Lua registry handle pattern.
 * AIN_WRAP parameters arrive as int (heap slot index).
 * The slot may be VM_PAGE (proper wrap page) or the inner type directly
 * (e.g., VM_STRING for wrap<string>).
 */

/* Write an int to a wrap<int> slot */
static inline void wrap_set_int(int slot, int value)
{
	if (slot < 0 || (size_t)slot >= heap_size) return;
	if (heap[slot].type == VM_PAGE && heap[slot].page
	    && heap[slot].page->nr_vars > 0) {
		heap[slot].page->values[0].i = value;
	}
}

/* Read an int from a wrap<int> slot */
static inline int wrap_get_int(int slot)
{
	if (slot < 0 || (size_t)slot >= heap_size) return 0;
	if (heap[slot].type == VM_PAGE && heap[slot].page
	    && heap[slot].page->nr_vars > 0)
		return heap[slot].page->values[0].i;
	return 0;
}

/* Write a string to a wrap<string> slot */
static inline void wrap_set_string(int slot, struct string *s)
{
	if (slot < 0 || (size_t)slot >= heap_size) return;
	if (heap[slot].type == VM_PAGE && heap[slot].page
	    && heap[slot].page->nr_vars > 0) {
		/* Wrap page: values[0] is string heap slot */
		int old = heap[slot].page->values[0].i;
		int ns = heap_alloc_slot(VM_STRING);
		heap[ns].s = s;
		heap[slot].page->values[0].i = ns;
		if (old > 0) heap_unref(old);
	} else if (heap[slot].type == VM_STRING) {
		/* Direct string slot: replace in place */
		if (heap[slot].s)
			free_string(heap[slot].s);
		heap[slot].s = s;
	}
}

/* Write a float to a wrap<float> slot */
static inline void wrap_set_float(int slot, float value)
{
	if (slot < 0 || (size_t)slot >= heap_size) return;
	if (heap[slot].type == VM_PAGE && heap[slot].page
	    && heap[slot].page->nr_vars > 0) {
		heap[slot].page->values[0].f = value;
	}
}

/* Write a bool to a wrap<bool> slot */
static inline void wrap_set_bool(int slot, bool value)
{
	if (slot < 0 || (size_t)slot >= heap_size) return;
	if (heap[slot].type == VM_PAGE && heap[slot].page
	    && heap[slot].page->nr_vars > 0) {
		heap[slot].page->values[0].i = value ? 1 : 0;
	}
}

/* Get the page from a wrap<struct/array/delegate> slot */
static inline struct page *wrap_get_page(int slot)
{
	if (slot < 0 || (size_t)slot >= heap_size) return NULL;
	if (heap[slot].type == VM_PAGE)
		return heap[slot].page;
	return NULL;
}

/* Set a heap slot value in a wrap page's values[0].
 * For wrap<array>, wrap<struct>, wrap<delegate>, this sets the inner heap slot.
 * Handles ref counting. */
static inline void wrap_set_slot(int wrap_slot, int new_inner_slot)
{
	if (wrap_slot < 0 || (size_t)wrap_slot >= heap_size) return;
	if (heap[wrap_slot].type == VM_PAGE && heap[wrap_slot].page
	    && heap[wrap_slot].page->nr_vars > 0) {
		int old = heap[wrap_slot].page->values[0].i;
		heap[wrap_slot].page->values[0].i = new_inner_slot;
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
