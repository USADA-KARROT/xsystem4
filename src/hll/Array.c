/* Copyright (C) 2025 kichikuou <KichikuouChrome@gmail.com>
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

#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include <SDL.h>
#ifdef __APPLE__
#include <malloc/malloc.h>
#else
#include <malloc.h>
#endif

#include "system4/ain.h"
#include "hll.h"
#include "vm.h"
#include "vm/heap.h"
#include "vm/page.h"

// v14: hll_arg3 from CALLHLL encodes element type info for Array operations.
// Set by ffi.c before each HLL call. High bits indicate reference-counted elements.
int hll_current_arg3 = -1;
// Set by ffi.c for 2-slot HLL_PARAM: stores the second slot value.
int hll_param_slot2 = 0;

// v14: heap slot of the first AIN_REF_ARRAY argument (typically 'self').
// Set by ffi.c during argument processing so HLL functions can construct
// 2-slot references [page_slot, var_index] for AIN_REF_HLL_PARAM returns.
int hll_self_slot = -1;

// Check if current Array HLL call operates on reference-counted elements.
// hll_arg3 >= 0x10000 indicates ref-counted elements (v14 struct encoding).
// hll_arg3 == 2 indicates struct/wrap elements (pre-v14 convention).
static inline bool array_elem_is_ref(void) {
	return hll_current_arg3 >= 0x10000 || hll_current_arg3 == 2;
}

// Check if current Array HLL call operates on struct elements that need construction.
// v14 encodes struct types as 0x10000 + struct_index; pre-v14 uses 2.
static inline bool array_elem_is_struct(void) {
	return hll_current_arg3 == 2 || hll_current_arg3 >= 0x10000;
}

// Check if current Array HLL call operates on 2-slot elements (iface, option, etc.)
// v14: hll_arg3 low bits encode element category.
// etype==3 and etype==5 are v14 encodings for interface/option types.
static inline bool array_elem_is_2slot(void) {
	int etype = hll_current_arg3 & 0xFFFF;
	return etype == 3 || etype == 5 || etype == AIN_IFACE || etype == AIN_OPTION || etype == AIN_IFACE_WRAP;
}

// Alloc: allocate/resize an array.
// For struct/wrap elements, preserves existing elements and only creates
// new struct instances for newly added slots (like Realloc).
// This is critical because game code initializes struct members after
// the first Alloc, and a subsequent Alloc must not destroy those values.
static void Array_Alloc(struct page **array, int numof)
{
	if (!array || numof < 0)
		return;
	struct page *old = *array;
	int old_size = (old && old->type == ARRAY_PAGE) ? old->nr_vars : 0;
	int struct_type = (old && old->type == ARRAY_PAGE) ? old->array.struct_type : -1;
	struct page *new_a = alloc_page(ARRAY_PAGE, old ? old->a_type : AIN_ARRAY_INT, numof);
	new_a->array.rank = 1;
	if (old && old->type == ARRAY_PAGE)
		new_a->array = old->array;
	new_a->array.struct_type = struct_type;

	// Copy existing elements (up to the smaller of old/new size)
	int copy = old_size < numof ? old_size : numof;
	for (int i = 0; i < copy; i++)
		new_a->values[i] = old->values[i];

	// Free excess old elements that won't be copied
	if (old && old_size > numof) {
		for (int i = numof; i < old_size; i++) {
			if (old->values[i].i > 0)
				heap_unref(old->values[i].i);
			old->values[i].i = 0;
		}
	}

	// For struct/wrap arrays, create struct instances for new elements only
	if (array_elem_is_struct() && numof > old_size) {
		if (struct_type < 0 && hll_current_arg3 >= 0x10000)
			struct_type = hll_current_arg3 & 0xFFFF;
		if (struct_type >= 0 && struct_type < ain->nr_structures) {
			new_a->array.struct_type = struct_type;
			for (int i = old_size; i < numof; i++) {
				int slot = alloc_struct(struct_type);
				heap_ref(slot);
				new_a->values[i].i = slot;
			}
		}
	}

	// Free old array page (but NOT its vars — they were copied/freed above)
	if (old)
		free_page(old);
	*array = new_a;
}

// PushBack (capital B) — alias for Pushback
static void Array_PushBack(struct page **array, int value)
{
	if (!array)
		return;
	struct page *a = *array;
	int old_size = a ? a->nr_vars : 0;
	bool is_2slot = array_elem_is_2slot();
	int slots = is_2slot ? 2 : 1;
	int new_size = old_size + slots;

	if (a) {
		// Try to grow in-place if malloc has extra room
		size_t needed = sizeof(struct page) + sizeof(union vm_value) * new_size;
#ifdef __APPLE__
		size_t actual = malloc_size(a);
#else
		size_t actual = malloc_usable_size(a);
#endif
		if (needed <= actual) {
			a->nr_vars = new_size;
			a->values[old_size].i = value;
			if (is_2slot)
				a->values[old_size + 1].i = hll_param_slot2;
			if (array_elem_is_ref() && value > 0)
				heap_ref(value);
			return;
		}
		// Exponential growth to amortize realloc
		int grow_to = new_size * 2;
		if (grow_to < 16) grow_to = 16;
		a = xrealloc(a, sizeof(struct page) + sizeof(union vm_value) * grow_to);
		a->nr_vars = new_size;
		a->values[old_size].i = value;
		if (is_2slot)
			a->values[old_size + 1].i = hll_param_slot2;
		if (array_elem_is_ref() && value > 0)
			heap_ref(value);
		*array = a;
	} else {
		// For 2-slot interface/option elements, set AIN_ARRAY type and struct_type=2
		// so X_A_SIZE correctly divides nr_vars by stride to get logical element count.
		int a_type = is_2slot ? AIN_ARRAY : AIN_ARRAY_INT;
		struct page *new_a = alloc_page(ARRAY_PAGE, a_type, new_size);
		if (is_2slot)
			new_a->array.struct_type = 2;
		new_a->values[0].i = value;
		if (is_2slot)
			new_a->values[1].i = hll_param_slot2;
		if (array_elem_is_ref() && value > 0)
			heap_ref(value);
		*array = new_a;
	}
}

// EraseAll: erase all elements matching a predicate
static bool Array_EraseAll(struct page **array, int func)
{
	struct page *src = (array && *array) ? *array : NULL;
	if (!src || src->nr_vars == 0 || func < 0 || func >= ain->nr_functions)
		return false;

	struct ain_function *cb = &ain->functions[func];

	// Find elements to keep
	int *keep = malloc(src->nr_vars * sizeof(int));
	int keep_count = 0;

	for (int i = 0; i < src->nr_vars; i++) {
		int saved_sp = stack_ptr;

		// Push args on stack (stay for bytecode to consume via nopop)
		if (cb->nr_args >= 2) {
			stack_push(src->values[i]);
			stack_push(0);
		} else {
			stack_push(src->values[i]);
		}

		vm_call_nopop(func, cb->nr_args);

		int match = stack_pop().i;
		stack_ptr = saved_sp;
		if (!match) {
			keep[keep_count++] = src->values[i].i;
		}
	}

	bool erased = (keep_count != src->nr_vars);

	// v14: unref erased elements if they're heap objects
	if (array_elem_is_ref()) {
		for (int i = 0; i < src->nr_vars; i++) {
			bool kept = false;
			for (int j = 0; j < keep_count; j++) {
				if (keep[j] == src->values[i].i) { kept = true; break; }
			}
			if (!kept && src->values[i].i > 0)
				heap_unref(src->values[i].i);
		}
	}

	// Rebuild array with kept elements
	struct page *new_a = alloc_page(ARRAY_PAGE, src->a_type, keep_count);
	new_a->array = src->array;
	for (int i = 0; i < keep_count; i++) {
		new_a->values[i].i = keep[i];
	}
	free(keep);

	free_page(src);
	*array = new_a;
	return erased;
}

// Where: filter array with predicate function, return new array
static int Array_Where(struct page **array, int func)
{
	struct page *src = (array && *array) ? *array : NULL;
	if (!src || src->nr_vars == 0 || func < 0 || func >= ain->nr_functions) {
		// Return empty array
		struct page *result = alloc_page(ARRAY_PAGE, AIN_ARRAY_INT, 0);
		result->array.rank = 1;
		int slot = heap_alloc_slot(VM_PAGE);
		heap_set_page(slot, result);
		return slot;
	}

	struct ain_function *cb = &ain->functions[func];
	bool is_2slot = array_elem_is_2slot();
	int stride = is_2slot ? 2 : 1;

	// Collect matching elements by calling the predicate function for each.
	// For 2-slot elements (interface/option), matches stores interleaved [page_idx, vtoff] pairs.
	union vm_value *matches = malloc(src->nr_vars * sizeof(union vm_value));
	int match_count = 0;  // number of matched elements (not slots)

	for (int i = 0; i < src->nr_vars; i += stride) {
		int saved_sp = stack_ptr;

		if (is_2slot) {
			// 2-slot interface element: push [page_idx, vtoff]
			stack_push(src->values[i]);
			stack_push(src->values[i + 1]);
		} else if (cb->nr_args >= 2) {
			// Ref/struct parameter: push element value and 0
			stack_push(src->values[i]);
			stack_push(0);
		} else {
			stack_push(src->values[i]);
		}

		vm_call_nopop(func, cb->nr_args);

		int result = stack_pop().i;
		stack_ptr = saved_sp;
		if (result) {
			matches[match_count * stride] = src->values[i];
			if (is_2slot)
				matches[match_count * stride + 1] = src->values[i + 1];
			match_count++;
		}
	}

	// Build result array: preserve a_type and struct_type for X_A_SIZE correctness.
	// For 2-slot elements, force AIN_ARRAY type and struct_type=stride so X_A_SIZE
	// returns the logical element count (nr_vars / stride) not the raw slot count.
	int result_slots = match_count * stride;
	int result_a_type = is_2slot ? AIN_ARRAY : src->a_type;
	struct page *result_page = alloc_page(ARRAY_PAGE, result_a_type, result_slots);
	result_page->array.rank = 1;
	result_page->array.struct_type = is_2slot ? stride : src->array.struct_type;
	for (int i = 0; i < match_count; i++) {
		result_page->values[i * stride] = matches[i * stride];
		if (is_2slot)
			result_page->values[i * stride + 1] = matches[i * stride + 1];
		if (array_elem_is_ref() && matches[i * stride].i > 0)
			heap_ref(matches[i * stride].i);
	}
	free(matches);

	int slot = heap_alloc_slot(VM_PAGE);
	heap_set_page(slot, result_page);
	return slot;
}

// First: return first element matching predicate.
// Returns AIN_REF_HLL_PARAM: pushes directly to VM stack (same as At/Last).
// For struct types (arg3==2): push 1 slot (heap slot of found element).
// For simple types: push 2 slots [array_heap_slot, element_index].
// C return value is ignored by ffi.c for AIN_REF_HLL_PARAM.
static int Array_First(struct page **array, int func)
{
	struct page *src = (array && *array) ? *array : NULL;
	if (!src || src->nr_vars == 0 || func < 0 || func >= ain->nr_functions) {
		if (!array_elem_is_ref()) {
			stack_push(-1);
			stack_push(0);
		} else {
			stack_push(-1);
		}
		return 0;
	}

	struct ain_function *cb = &ain->functions[func];

	for (int i = 0; i < src->nr_vars; i++) {
		int saved_sp = stack_ptr;
		if (cb->nr_args >= 2) {
			stack_push(src->values[i]);
			stack_push(0);
		} else {
			stack_push(src->values[i]);
		}
		vm_call_nopop(func, cb->nr_args);
		int result = stack_pop().i;
		stack_ptr = saved_sp;
		if (result) {
			if (!array_elem_is_ref()) {
				stack_push(hll_self_slot);
				stack_push(i);
			} else {
				int val = src->values[i].i;
				if (val <= 0)
					val = -1;
				if (val > 0)
					heap_ref(val);
				stack_push(val);
			}
			return 0;
		}
	}
	if (!array_elem_is_ref()) {
		stack_push(-1);
		stack_push(0);
	} else {
		stack_push(-1);
	}
	return 0;
}

// Any: return true if any element matches the predicate
// No-predicate Any: returns true if array has any elements (C# LINQ Any())
static bool Array_Any(struct page **array)
{
	struct page *src = (array && *array) ? *array : NULL;
	return src && src->nr_vars > 0;
}

// Predicate Any: returns true if any element satisfies the predicate function
static bool Array_AnyPredicate(struct page **array, int func)
{
	struct page *src = (array && *array) ? *array : NULL;
	if (!src || src->nr_vars == 0 || func < 0 || func >= ain->nr_functions)
		return false;

	struct ain_function *cb = &ain->functions[func];

	for (int i = 0; i < src->nr_vars; i++) {
		int saved_sp = stack_ptr;
		if (cb->nr_args >= 2) {
			stack_push(src->values[i]);
			stack_push(0);
		} else {
			stack_push(src->values[i]);
		}
		vm_call_nopop(func, cb->nr_args);
		int result = stack_pop().i;
		stack_ptr = saved_sp;
		if (result)
			return true;
	}
	return false;
}

static void Array_Free(struct page **array)
{
	if (array && *array && (*array)->type == ARRAY_PAGE) {
		delete_page_vars(*array);
		free_page(*array);
		*array = NULL;
	}
}

static int Array_Numof(struct page **self)
{
	struct page *array = (self && *self) ? *self : NULL;
	if (!array || array->type != ARRAY_PAGE)
		return 0;
	int n = array ? array->nr_vars : 0;
	// v14: return logical element count for multi-slot arrays.
	// struct_type doubles as the AIN struct id for struct-element lists
	// (EmplaceBack) — only treat it as a stride when the call's element
	// type is actually a 2-slot value (iface/option), or Numof on a
	// CMessageText list divides 4/693 to 0 and every reader sees an
	// empty list (the dialogue-text blackout, gui-run-log fb39-fb47).
	if (array && (array->a_type == AIN_ARRAY || array->a_type == AIN_REF_ARRAY)
	    && array->array.struct_type > 1 && array_elem_is_2slot())
		n /= array->array.struct_type;
	return n;
}

static int Array_Empty(struct page **self)
{
	struct page *array = (self && *self) ? *self : NULL;
	return !array || array->nr_vars == 0;
}

// Array.At: returns AIN_REF_HLL_PARAM.
// For simple element types: push 2-slot reference [array_page_slot, index].
// For struct/ref-counted types: push 1-slot (the struct heap slot value).
// ffi.c does NOT push the C return value for AIN_REF_HLL_PARAM.
static int Array_At(struct page **self, int index)
{
	struct page *array = (self && *self) ? *self : NULL;
	// v14: convert logical index to physical for multi-slot arrays
	// (2-slot value elements only — see Array_Numof).
	if (array && (array->a_type == AIN_ARRAY || array->a_type == AIN_REF_ARRAY)
	    && array->array.struct_type > 1 && array_elem_is_2slot())
		index *= array->array.struct_type;
	if (!array || index < 0 || index >= array->nr_vars) {
		(void)0;
		if (!array_elem_is_ref()) {
			stack_push(-1);
			stack_push(0);
		} else {
			// v14: null reference is -1, not 0
			stack_push(-1);
		}
		return 0;
	}
	if (!array_elem_is_ref()) {
		if (ain->version >= 14) heap_ref(hll_self_slot);
		stack_push(hll_self_slot);
		stack_push(index);
	} else {
		int result = array->values[index].i;
		// v14: uninitialized/null elements are 0, but v14 null convention is -1
		if (result <= 0)
			result = -1;
		if (result > 0)
			heap_ref(result);
		stack_push(result);
	}
	return 0;
}

// Array.Last: returns AIN_REF_HLL_PARAM (same convention as Array.At).
static int Array_Last(struct page **self)
{
	struct page *array = (self && *self) ? *self : NULL;
	if (!array || array->nr_vars <= 0) {
		if (!array_elem_is_ref()) {
			stack_push(-1);
			stack_push(0);
		} else {
			stack_push(-1);
		}
		return 0;
	}
	int last_idx = array->nr_vars - 1;
	if (!array_elem_is_ref()) {
		if (ain->version >= 14) heap_ref(hll_self_slot);
		stack_push(hll_self_slot);
		stack_push(last_idx);
	} else {
		int result = array->values[last_idx].i;
		if (result <= 0)
			result = -1;
		if (result > 0)
			heap_ref(result);
		stack_push(result);
	}
	return 0;
}

// First without a predicate: same contract as At(0). The AIN declares
// First(ref array self) and First(ref array self, hll_func func) under one
// name; the single-argument form must not reach Array_First's func parameter.
static int Array_First_NoPred(struct page **array)
{
	return Array_At(array, 0);
}

// PopBack (capital B) — v14 name
static void Array_PopBack(struct page **array)
{
	if (!array || !*array || (*array)->nr_vars <= 0)
		return;
	struct page *a = *array;
	int new_size = a->nr_vars - 1;
	// v14: unref the removed element if it's a heap object
	if (array_elem_is_ref()) {
		int removed = a->values[new_size].i;
		if (removed > 0)
			heap_unref(removed);
	}
	if (new_size == 0 && ain->version < 14) {
		free_page(a);
		*array = NULL;
		return;
	}
	// v14 generic HLL calls need the element declaration even when empty.
	// Keep a typed zero-length page so EmplaceBack can construct it again.
	struct page *new_a = alloc_page(ARRAY_PAGE, a->a_type, new_size);
	for (int i = 0; i < new_size; i++)
		new_a->values[i] = a->values[i];
	new_a->array = a->array;
	free_page(a);
	*array = new_a;
}

// Clear: clear array (set to empty)
static void Array_Clear(struct page **array)
{
	if (!array)
		return;
	if (*array) {
		delete_page_vars(*array);
		free_page(*array);
	}
	*array = alloc_page(ARRAY_PAGE, AIN_ARRAY_INT, 0);
	(*array)->array.rank = 1;
}

static void Array_Pushback(struct page **array, int value)
{
	if (!array)
		return;
	// heap_ref for ref-counted elements (struct/string/delegate)
	if (array_elem_is_ref() && value > 0)
		heap_ref(value);
	struct page *a = *array;
	int old_size = a ? a->nr_vars : 0;
	int new_size = old_size + 1;

	if (a) {
		size_t needed = sizeof(struct page) + sizeof(union vm_value) * new_size;
#ifdef __APPLE__
		size_t actual = malloc_size(a);
#else
		size_t actual = malloc_usable_size(a);
#endif
		if (needed <= actual) {
			a->nr_vars = new_size;
			a->values[old_size].i = value;
			return;
		}
		int grow_to = new_size * 2;
		if (grow_to < 16) grow_to = 16;
		a = xrealloc(a, sizeof(struct page) + sizeof(union vm_value) * grow_to);
		a->nr_vars = new_size;
		a->values[old_size].i = value;
		*array = a;
	} else {
		struct page *new_a = alloc_page(ARRAY_PAGE, AIN_ARRAY_INT, new_size);
		new_a->values[0].i = value;
		*array = new_a;
	}
}

static void Array_Popback(struct page **array)
{
	if (!array || !*array || (*array)->nr_vars <= 0)
		return;
	struct page *a = *array;
	int new_size = a->nr_vars - 1;
	// heap_unref the removed element
	if (array_elem_is_ref() && a->values[new_size].i > 0)
		heap_unref(a->values[new_size].i);
	if (new_size == 0) {
		free_page(a);
		*array = NULL;
		return;
	}
	struct page *new_a = alloc_page(ARRAY_PAGE, a->a_type, new_size);
	for (int i = 0; i < new_size; i++)
		new_a->values[i] = a->values[i];
	new_a->array = a->array;
	free_page(a);
	*array = new_a;
}

// Erase: remove 'length' elements starting at 'index'.
// AIN declares: self (ref_array), index (int), length (int).
static int array_erase_stride(const struct page *a)
{
	if ((a->a_type == AIN_ARRAY || a->a_type == AIN_REF_ARRAY)
	    && a->array.struct_type > 1 && array_elem_is_2slot())
		return a->array.struct_type;
	return 1;
}

static bool Array_Erase(struct page **array, int index, int length)
{
	if (!array || !*array)
		return false;
	struct page *a = *array;
	int stride = array_erase_stride(a);
	int count = a->nr_vars / stride;
	if (length <= 0 || index < 0 || index >= count)
		return false;
	if (length > count - index)
		length = count - index;
	index *= stride;
	length *= stride;
	// Match PushBack: only the first slot owns a reference. The second slot
	// of an interface/option element is metadata, even if it looks like a slot.
	if (array_elem_is_ref()) {
		for (int i = index; i < index + length; i += stride) {
			int removed = a->values[i].i;
			if (removed > 0)
				heap_unref(removed);
		}
	}
	int new_size = a->nr_vars - length;
	if (new_size == 0) {
		free_page(a);
		*array = NULL;
		return true;
	}
	struct page *new_a = alloc_page(ARRAY_PAGE, a->a_type, new_size);
	for (int i = 0; i < index; i++)
		new_a->values[i] = a->values[i];
	for (int i = index; i < new_size; i++)
		new_a->values[i] = a->values[i + length];
	new_a->array = a->array;
	free_page(a);
	*array = new_a;
	return true;
}

/* Erase(predicate) removes the first match; EraseAll is a separate overload. */
static bool Array_EraseIf(struct page **array, int func)
{
	if (!array || !*array || func < 0 || func >= ain->nr_functions)
		return false;
	struct ain_function *cb = &ain->functions[func];
	if (cb->nr_args < 1 || cb->nr_args > 2)
		return false;
	int stride = array_erase_stride(*array);
	for (int i = 0; *array && i < (*array)->nr_vars / stride; i++) {
		int saved_sp = stack_ptr;
		stack_push((*array)->values[i * stride]);
		if (cb->nr_args == 2)
			stack_push(stride > 1 ? (*array)->values[i * stride + 1] : (union vm_value){.i = 0});
		vm_call_nopop(func, cb->nr_args);
		bool match = stack_pop().i != 0;
		stack_ptr = saved_sp;
		if (match)
			return Array_Erase(array, i, 1);
	}
	return false;
}

static bool array_erase_value_equal(union vm_value a, union vm_value b)
{
	if (a.i == b.i)
		return true;
	if (!array_elem_is_ref() || a.i <= 0 || b.i <= 0
	    || (size_t)a.i >= heap_size || (size_t)b.i >= heap_size
	    || HEAP_REF(a.i) <= 0 || HEAP_REF(b.i) <= 0
	    || heap[a.i].type != VM_STRING || heap[b.i].type != VM_STRING
	    || !heap[a.i].s || !heap[b.i].s)
		return false;
	struct string *sa = heap[a.i].s, *sb = heap[b.i].s;
	return sa->size == sb->size && !memcmp(sa->text, sb->text, sa->size);
}

/* Erase(source) is set subtraction, used for selected IDs and skill lists. */
static bool Array_EraseValues(struct page **array, int source_slot)
{
	if (!array || !*array || source_slot <= 0 || (size_t)source_slot >= heap_size
	    || HEAP_REF(source_slot) <= 0 || heap[source_slot].type != VM_PAGE
	    || !heap[source_slot].page || heap[source_slot].page->type != ARRAY_PAGE)
		return false;
	const struct page *src = heap[source_slot].page;
	int stride = array_erase_stride(*array);
	if (array_erase_stride(src) != stride)
		return false;
	if (src == *array)
		return Array_Erase(array, 0, (*array)->nr_vars / stride);
	bool erased = false;
	for (int i = (*array)->nr_vars / stride - 1; i >= 0; i--) {
		bool found = false;
		for (int j = 0; j < src->nr_vars / stride && !found; j++) {
			found = true;
			for (int k = 0; k < stride; k++) {
				union vm_value a = (*array)->values[i * stride + k];
				union vm_value b = src->values[j * stride + k];
				// Extra slots are metadata, never string references.
				if (!(k == 0 ? array_erase_value_equal(a, b) : a.i == b.i)) {
					found = false;
					break;
				}
			}
		}
		if (found)
			erased = Array_Erase(array, i, 1) || erased;
	}
	return erased;
}

/* The AIN declarations share a name, but require distinct C prototypes. */
void *array_erase_function(const struct ain_hll_function *f)
{
	if (!f || !f->arguments || (f->return_type.data != AIN_BOOL && f->return_type.data != AIN_VOID)
	    || f->nr_arguments < 2)
		return NULL;
	switch (f->arguments[0].type.data) {
	case AIN_REF_ARRAY_TYPE:
	case AIN_REF_ARRAY:
		break;
	default:
		return NULL;
	}
	if (f->nr_arguments == 3 && f->arguments[1].type.data == AIN_INT
	    && f->arguments[2].type.data == AIN_INT)
		return Array_Erase;
	if (f->nr_arguments != 2 || f->return_type.data != AIN_BOOL)
		return NULL;
	if (f->arguments[1].type.data == AIN_HLL_FUNC || f->arguments[1].type.data == AIN_HLL_FUNC_71)
		return Array_EraseIf;
	if (f->arguments[1].type.data == AIN_WRAP && f->arguments[1].type.array_type
	    && f->arguments[1].type.array_type->data == AIN_ARRAY)
		return Array_EraseValues;
	return NULL;
}

static void Array_Insert(struct page **array, int index, int value)
{
	if (!array)
		return;
	// heap_ref for ref-counted elements (struct/string/delegate)
	if (array_elem_is_ref() && value > 0)
		heap_ref(value);
	struct page *a = *array;
	int old_size = a ? a->nr_vars : 0;
	if (index < 0) index = 0;
	if (index > old_size) index = old_size;
	struct page *new_a = alloc_page(ARRAY_PAGE, a ? a->a_type : AIN_ARRAY_INT, old_size + 1);
	if (a) {
		for (int i = 0; i < index; i++)
			new_a->values[i] = a->values[i];
		for (int i = index; i < old_size; i++)
			new_a->values[i + 1] = a->values[i];
		new_a->array = a->array;
		free_page(a);
	}
	new_a->values[index].i = value;
	*array = new_a;
}

static void Array_Sort(struct page **array)
{
	// Simple insertion sort for int arrays
	if (!array || !*array || (*array)->nr_vars <= 1)
		return;
	struct page *a = *array;
	for (int i = 1; i < a->nr_vars; i++) {
		int val = a->values[i].i;
		int j = i - 1;
		while (j >= 0 && a->values[j].i > val) {
			a->values[j + 1].i = a->values[j].i;
			j--;
		}
		a->values[j + 1].i = val;
	}
}

// Unique: remove duplicate values from sorted array
static void Array_Unique(struct page **array)
{
	if (!array || !*array || (*array)->nr_vars <= 1)
		return;
	struct page *a = *array;
	int write = 1;
	for (int read = 1; read < a->nr_vars; read++) {
		if (a->values[read].i != a->values[read - 1].i) {
			a->values[write] = a->values[read];
			write++;
		}
	}
	if (write < a->nr_vars) {
		struct page *new_a = alloc_page(ARRAY_PAGE, a->a_type, write);
		for (int i = 0; i < write; i++)
			new_a->values[i] = a->values[i];
		new_a->array = a->array;
		free_page(a);
		*array = new_a;
	}
}

static int qsort_int_cmp(const void *a, const void *b)
{
	int ia = ((const union vm_value *)a)->i;
	int ib = ((const union vm_value *)b)->i;
	return (ia > ib) - (ia < ib);
}

// QuickSort: sort array using comparator function.
// For now, we ignore the comparator and do simple integer ascending sort.
static void Array_QuickSort(struct page **array, int comparator)
{
	(void)comparator;
	if (!array || !*array || (*array)->nr_vars <= 1)
		return;
	struct page *a = *array;
	qsort(a->values, a->nr_vars, sizeof(union vm_value), qsort_int_cmp);
}

/* ================================================================
 * Vector operations: {dst}{src}_{op}
 * N=numeric array, V=scalar, S=struct array
 * ================================================================ */

static inline int smget(struct page *arr, int i, int m) {
	int slot = arr->values[i].i;
	if (slot <= 0 || (size_t)slot >= heap_size) return 0;
	struct page *p = heap[slot].page;
	if (!p || m < 0 || m >= p->nr_vars) return 0;
	return p->values[m].i;
}

static inline void smset(struct page *arr, int i, int m, int v) {
	int slot = arr->values[i].i;
	if (slot <= 0 || (size_t)slot >= heap_size) return;
	struct page *p = heap[slot].page;
	if (!p || m < 0 || m >= p->nr_vars) return;
	p->values[m].i = v;
}

#define OP_copy(a,b) (b)
#define OP_add(a,b) ((a)+(b))
#define OP_sub(a,b) ((a)-(b))
#define OP_mul(a,b) ((a)*(b))
#define OP_div(a,b) ((b)?(a)/(b):0)
#define OP_and(a,b) ((a)&(b))
#define OP_or(a,b)  ((a)|(b))
#define OP_xor(a,b) ((a)^(b))
#define OP_min(a,b) ((a)<(b)?(a):(b))
#define OP_max(a,b) ((a)>(b)?(a):(b))

#define DEF_NV(op) \
static void Array_NV_##op(struct page **self, int num) { \
	struct page *a = (self && *self) ? *self : NULL; \
	if (!a) return; \
	for (int i = 0; i < a->nr_vars; i++) \
		a->values[i].i = OP_##op(a->values[i].i, num); \
}
DEF_NV(copy) DEF_NV(add) DEF_NV(sub) DEF_NV(mul) DEF_NV(div)
DEF_NV(and) DEF_NV(or) DEF_NV(xor) DEF_NV(min) DEF_NV(max)

#define DEF_NN(op) \
static void Array_NN_##op(struct page **self, struct page **src) { \
	struct page *a = (self && *self) ? *self : NULL; \
	struct page *s = (src && *src) ? *src : NULL; \
	if (!a || !s) return; \
	int n = a->nr_vars < s->nr_vars ? a->nr_vars : s->nr_vars; \
	for (int i = 0; i < n; i++) \
		a->values[i].i = OP_##op(a->values[i].i, s->values[i].i); \
}
DEF_NN(copy) DEF_NN(add) DEF_NN(sub) DEF_NN(mul) DEF_NN(div)
DEF_NN(and) DEF_NN(or) DEF_NN(xor) DEF_NN(min) DEF_NN(max)

#define DEF_NS(op) \
static void Array_NS_##op(struct page **self, struct page **sarr, int m) { \
	struct page *a = (self && *self) ? *self : NULL; \
	struct page *s = (sarr && *sarr) ? *sarr : NULL; \
	if (!a || !s) return; \
	int n = a->nr_vars < s->nr_vars ? a->nr_vars : s->nr_vars; \
	for (int i = 0; i < n; i++) \
		a->values[i].i = OP_##op(a->values[i].i, smget(s, i, m)); \
}
DEF_NS(copy) DEF_NS(add) DEF_NS(sub) DEF_NS(mul) DEF_NS(div)
DEF_NS(and) DEF_NS(or) DEF_NS(xor) DEF_NS(min) DEF_NS(max)

#define DEF_SV(op) \
static void Array_SV_##op(struct page **self, int m, int num) { \
	struct page *a = (self && *self) ? *self : NULL; \
	if (!a) return; \
	for (int i = 0; i < a->nr_vars; i++) \
		smset(a, i, m, OP_##op(smget(a, i, m), num)); \
}
DEF_SV(copy) DEF_SV(add) DEF_SV(sub) DEF_SV(mul) DEF_SV(div)
DEF_SV(and) DEF_SV(or) DEF_SV(xor) DEF_SV(min) DEF_SV(max)

#define DEF_SN(op) \
static void Array_SN_##op(struct page **self, int m, struct page **src) { \
	struct page *a = (self && *self) ? *self : NULL; \
	struct page *s = (src && *src) ? *src : NULL; \
	if (!a || !s) return; \
	int n = a->nr_vars < s->nr_vars ? a->nr_vars : s->nr_vars; \
	for (int i = 0; i < n; i++) \
		smset(a, i, m, OP_##op(smget(a, i, m), s->values[i].i)); \
}
DEF_SN(copy) DEF_SN(add) DEF_SN(sub) DEF_SN(mul) DEF_SN(div)
DEF_SN(and) DEF_SN(or) DEF_SN(xor) DEF_SN(min) DEF_SN(max)

#define DEF_SS(op) \
static void Array_SS_##op(struct page **self, int m, struct page **sarr, int ms) { \
	struct page *a = (self && *self) ? *self : NULL; \
	struct page *s = (sarr && *sarr) ? *sarr : NULL; \
	if (!a || !s) return; \
	int n = a->nr_vars < s->nr_vars ? a->nr_vars : s->nr_vars; \
	for (int i = 0; i < n; i++) \
		smset(a, i, m, OP_##op(smget(a, i, m), smget(s, i, ms))); \
}
DEF_SS(copy) DEF_SS(add) DEF_SS(sub) DEF_SS(mul) DEF_SS(div)
DEF_SS(and) DEF_SS(or) DEF_SS(xor) DEF_SS(min) DEF_SS(max)

/* ---- Enumerate: count elements matching condition ---- */
#define DEF_NV_EN(sfx, cmpop) \
static int Array_NV_en##sfx(struct page **self, int num) { \
	struct page *a = (self && *self) ? *self : NULL; \
	if (!a) return 0; \
	int c = 0; \
	for (int i = 0; i < a->nr_vars; i++) \
		if (a->values[i].i cmpop num) c++; \
	return c; \
}
DEF_NV_EN(eq, ==) DEF_NV_EN(ne, !=) DEF_NV_EN(lo, <) DEF_NV_EN(hi, >)

static int Array_NV_enra(struct page **self, int lo, int hi) {
	struct page *a = (self && *self) ? *self : NULL;
	if (!a) return 0;
	int c = 0;
	for (int i = 0; i < a->nr_vars; i++) {
		int v = a->values[i].i;
		if (v >= lo && v <= hi) c++;
	}
	return c;
}

#define DEF_NN_EN(sfx, cmpop) \
static int Array_NN_en##sfx(struct page **self, struct page **src) { \
	struct page *a = (self && *self) ? *self : NULL; \
	struct page *s = (src && *src) ? *src : NULL; \
	if (!a || !s) return 0; \
	int n = a->nr_vars < s->nr_vars ? a->nr_vars : s->nr_vars; \
	int c = 0; \
	for (int i = 0; i < n; i++) \
		if (a->values[i].i cmpop s->values[i].i) c++; \
	return c; \
}
DEF_NN_EN(eq, ==) DEF_NN_EN(ne, !=) DEF_NN_EN(lo, <) DEF_NN_EN(hi, >)

#define DEF_NS_EN(sfx, cmpop) \
static int Array_NS_en##sfx(struct page **self, struct page **sarr, int m) { \
	struct page *a = (self && *self) ? *self : NULL; \
	struct page *s = (sarr && *sarr) ? *sarr : NULL; \
	if (!a || !s) return 0; \
	int n = a->nr_vars < s->nr_vars ? a->nr_vars : s->nr_vars; \
	int c = 0; \
	for (int i = 0; i < n; i++) \
		if (a->values[i].i cmpop smget(s, i, m)) c++; \
	return c; \
}
DEF_NS_EN(eq, ==) DEF_NS_EN(ne, !=) DEF_NS_EN(lo, <) DEF_NS_EN(hi, >)

#define DEF_SV_EN(sfx, cmpop) \
static int Array_SV_en##sfx(struct page **self, int m, int num) { \
	struct page *a = (self && *self) ? *self : NULL; \
	if (!a) return 0; \
	int c = 0; \
	for (int i = 0; i < a->nr_vars; i++) \
		if (smget(a, i, m) cmpop num) c++; \
	return c; \
}
DEF_SV_EN(eq, ==) DEF_SV_EN(ne, !=) DEF_SV_EN(lo, <) DEF_SV_EN(hi, >)

static int Array_SV_enra(struct page **self, int m, int lo, int hi) {
	struct page *a = (self && *self) ? *self : NULL;
	if (!a) return 0;
	int c = 0;
	for (int i = 0; i < a->nr_vars; i++) {
		int v = smget(a, i, m);
		if (v >= lo && v <= hi) c++;
	}
	return c;
}

#define DEF_SN_EN(sfx, cmpop) \
static int Array_SN_en##sfx(struct page **self, int m, struct page **src) { \
	struct page *a = (self && *self) ? *self : NULL; \
	struct page *s = (src && *src) ? *src : NULL; \
	if (!a || !s) return 0; \
	int n = a->nr_vars < s->nr_vars ? a->nr_vars : s->nr_vars; \
	int c = 0; \
	for (int i = 0; i < n; i++) \
		if (smget(a, i, m) cmpop s->values[i].i) c++; \
	return c; \
}
DEF_SN_EN(eq, ==) DEF_SN_EN(ne, !=) DEF_SN_EN(lo, <) DEF_SN_EN(hi, >)

#define DEF_SS_EN(sfx, cmpop) \
static int Array_SS_en##sfx(struct page **self, int m, struct page **sarr, int ms) { \
	struct page *a = (self && *self) ? *self : NULL; \
	struct page *s = (sarr && *sarr) ? *sarr : NULL; \
	if (!a || !s) return 0; \
	int n = a->nr_vars < s->nr_vars ? a->nr_vars : s->nr_vars; \
	int c = 0; \
	for (int i = 0; i < n; i++) \
		if (smget(a, i, m) cmpop smget(s, i, ms)) c++; \
	return c; \
}
DEF_SS_EN(eq, ==) DEF_SS_EN(ne, !=) DEF_SS_EN(lo, <) DEF_SS_EN(hi, >)

/* ---- Change: replace elements matching condition with nChg ---- */
#define DEF_NV_CH(sfx, cmpop) \
static void Array_NV_ch##sfx(struct page **self, int num, int chg) { \
	struct page *a = (self && *self) ? *self : NULL; \
	if (!a) return; \
	for (int i = 0; i < a->nr_vars; i++) \
		if (a->values[i].i cmpop num) a->values[i].i = chg; \
}
DEF_NV_CH(eq, ==) DEF_NV_CH(ne, !=) DEF_NV_CH(lo, <) DEF_NV_CH(hi, >)

static void Array_NV_chra(struct page **self, int lo, int hi, int chg) {
	struct page *a = (self && *self) ? *self : NULL;
	if (!a) return;
	for (int i = 0; i < a->nr_vars; i++) {
		int v = a->values[i].i;
		if (v >= lo && v <= hi) a->values[i].i = chg;
	}
}

#define DEF_NN_CH(sfx, cmpop) \
static void Array_NN_ch##sfx(struct page **self, struct page **src, int chg) { \
	struct page *a = (self && *self) ? *self : NULL; \
	struct page *s = (src && *src) ? *src : NULL; \
	if (!a || !s) return; \
	int n = a->nr_vars < s->nr_vars ? a->nr_vars : s->nr_vars; \
	for (int i = 0; i < n; i++) \
		if (a->values[i].i cmpop s->values[i].i) a->values[i].i = chg; \
}
DEF_NN_CH(eq, ==) DEF_NN_CH(ne, !=) DEF_NN_CH(lo, <) DEF_NN_CH(hi, >)

#define DEF_NS_CH(sfx, cmpop) \
static void Array_NS_ch##sfx(struct page **self, struct page **sarr, int m, int chg) { \
	struct page *a = (self && *self) ? *self : NULL; \
	struct page *s = (sarr && *sarr) ? *sarr : NULL; \
	if (!a || !s) return; \
	int n = a->nr_vars < s->nr_vars ? a->nr_vars : s->nr_vars; \
	for (int i = 0; i < n; i++) \
		if (a->values[i].i cmpop smget(s, i, m)) a->values[i].i = chg; \
}
DEF_NS_CH(eq, ==) DEF_NS_CH(ne, !=) DEF_NS_CH(lo, <) DEF_NS_CH(hi, >)

#define DEF_SV_CH(sfx, cmpop) \
static void Array_SV_ch##sfx(struct page **self, int m, int num, int chg) { \
	struct page *a = (self && *self) ? *self : NULL; \
	if (!a) return; \
	for (int i = 0; i < a->nr_vars; i++) \
		if (smget(a, i, m) cmpop num) smset(a, i, m, chg); \
}
DEF_SV_CH(eq, ==) DEF_SV_CH(ne, !=) DEF_SV_CH(lo, <) DEF_SV_CH(hi, >)

static void Array_SV_chra(struct page **self, int m, int lo, int hi, int chg) {
	struct page *a = (self && *self) ? *self : NULL;
	if (!a) return;
	for (int i = 0; i < a->nr_vars; i++) {
		int v = smget(a, i, m);
		if (v >= lo && v <= hi) smset(a, i, m, chg);
	}
}

#define DEF_SN_CH(sfx, cmpop) \
static void Array_SN_ch##sfx(struct page **self, int m, struct page **src, int chg) { \
	struct page *a = (self && *self) ? *self : NULL; \
	struct page *s = (src && *src) ? *src : NULL; \
	if (!a || !s) return; \
	int n = a->nr_vars < s->nr_vars ? a->nr_vars : s->nr_vars; \
	for (int i = 0; i < n; i++) \
		if (smget(a, i, m) cmpop s->values[i].i) smset(a, i, m, chg); \
}
DEF_SN_CH(eq, ==) DEF_SN_CH(ne, !=) DEF_SN_CH(lo, <) DEF_SN_CH(hi, >)

#define DEF_SS_CH(sfx, cmpop) \
static void Array_SS_ch##sfx(struct page **self, int m, struct page **sarr, int ms, int chg) { \
	struct page *a = (self && *self) ? *self : NULL; \
	struct page *s = (sarr && *sarr) ? *sarr : NULL; \
	if (!a || !s) return; \
	int n = a->nr_vars < s->nr_vars ? a->nr_vars : s->nr_vars; \
	for (int i = 0; i < n; i++) \
		if (smget(a, i, m) cmpop smget(s, i, ms)) smset(a, i, m, chg); \
}
DEF_SS_CH(eq, ==) DEF_SS_CH(ne, !=) DEF_SS_CH(lo, <) DEF_SS_CH(hi, >)

/* ---- Filter helpers ---- */
static void filter_write(bool *flags, int n, struct page **dst) {
	if (!dst) return;
	int c = 0;
	for (int i = 0; i < n; i++) if (flags[i]) c++;
	if (*dst) free_page(*dst);
	*dst = alloc_page(ARRAY_PAGE, AIN_ARRAY_INT, c);
	c = 0;
	for (int i = 0; i < n; i++)
		if (flags[i]) (*dst)->values[c++].i = i;
}

static void filter_and(bool *flags, int n, struct page **dst) {
	if (!dst || !*dst) return;
	struct page *d = *dst;
	int c = 0;
	for (int i = 0; i < d->nr_vars; i++) {
		int idx = d->values[i].i;
		if (idx >= 0 && idx < n && flags[idx])
			d->values[c++] = d->values[i];
	}
	if (c < d->nr_vars) {
		struct page *nd = alloc_page(ARRAY_PAGE, AIN_ARRAY_INT, c);
		for (int i = 0; i < c; i++) nd->values[i] = d->values[i];
		free_page(d);
		*dst = nd;
	}
}

static void filter_or(bool *flags, int n, struct page **dst) {
	if (!dst) return;
	struct page *d = *dst;
	int d_size = d ? d->nr_vars : 0;
	bool *exists = calloc(n, sizeof(bool));
	for (int i = 0; i < d_size; i++) {
		int idx = d->values[i].i;
		if (idx >= 0 && idx < n) exists[idx] = true;
	}
	int add = 0;
	for (int i = 0; i < n; i++)
		if (flags[i] && !exists[i]) add++;
	struct page *nd = alloc_page(ARRAY_PAGE, AIN_ARRAY_INT, d_size + add);
	for (int i = 0; i < d_size; i++) nd->values[i] = d->values[i];
	int c = d_size;
	for (int i = 0; i < n; i++)
		if (flags[i] && !exists[i]) nd->values[c++].i = i;
	if (d) free_page(d);
	*dst = nd;
	free(exists);
}

/* ---- Filter: NV ---- */
#define DEF_NV_FILTER(sfx, cmpop) \
static void Array_NV_fw##sfx(struct page **arr, int num, struct page **dst) { \
	struct page *a = (arr && *arr) ? *arr : NULL; \
	if (!a) { filter_write(NULL, 0, dst); return; } \
	bool *f = malloc(a->nr_vars * sizeof(bool)); \
	for (int i = 0; i < a->nr_vars; i++) f[i] = a->values[i].i cmpop num; \
	filter_write(f, a->nr_vars, dst); free(f); \
} \
static void Array_NV_fa##sfx(struct page **arr, int num, struct page **dst) { \
	struct page *a = (arr && *arr) ? *arr : NULL; \
	if (!a) return; \
	bool *f = malloc(a->nr_vars * sizeof(bool)); \
	for (int i = 0; i < a->nr_vars; i++) f[i] = a->values[i].i cmpop num; \
	filter_and(f, a->nr_vars, dst); free(f); \
} \
static void Array_NV_fo##sfx(struct page **arr, int num, struct page **dst) { \
	struct page *a = (arr && *arr) ? *arr : NULL; \
	if (!a) return; \
	bool *f = malloc(a->nr_vars * sizeof(bool)); \
	for (int i = 0; i < a->nr_vars; i++) f[i] = a->values[i].i cmpop num; \
	filter_or(f, a->nr_vars, dst); free(f); \
}
DEF_NV_FILTER(eq, ==) DEF_NV_FILTER(ne, !=) DEF_NV_FILTER(lo, <) DEF_NV_FILTER(hi, >)

static void Array_NV_fwra(struct page **arr, int lo, int hi, struct page **dst) {
	struct page *a = (arr && *arr) ? *arr : NULL;
	if (!a) return;
	bool *f = malloc(a->nr_vars * sizeof(bool));
	for (int i = 0; i < a->nr_vars; i++) { int v = a->values[i].i; f[i] = v >= lo && v <= hi; }
	filter_write(f, a->nr_vars, dst); free(f);
}
static void Array_NV_fara(struct page **arr, int lo, int hi, struct page **dst) {
	struct page *a = (arr && *arr) ? *arr : NULL;
	if (!a) return;
	bool *f = malloc(a->nr_vars * sizeof(bool));
	for (int i = 0; i < a->nr_vars; i++) { int v = a->values[i].i; f[i] = v >= lo && v <= hi; }
	filter_and(f, a->nr_vars, dst); free(f);
}
static void Array_NV_fora(struct page **arr, int lo, int hi, struct page **dst) {
	struct page *a = (arr && *arr) ? *arr : NULL;
	if (!a) return;
	bool *f = malloc(a->nr_vars * sizeof(bool));
	for (int i = 0; i < a->nr_vars; i++) { int v = a->values[i].i; f[i] = v >= lo && v <= hi; }
	filter_or(f, a->nr_vars, dst); free(f);
}

/* ---- Filter: NN ---- */
#define DEF_NN_FILTER(sfx, cmpop) \
static void Array_NN_fw##sfx(struct page **arr, struct page **src, struct page **dst) { \
	struct page *a = (arr && *arr) ? *arr : NULL; \
	struct page *s = (src && *src) ? *src : NULL; \
	if (!a || !s) return; \
	int n = a->nr_vars < s->nr_vars ? a->nr_vars : s->nr_vars; \
	bool *f = malloc(n * sizeof(bool)); \
	for (int i = 0; i < n; i++) f[i] = a->values[i].i cmpop s->values[i].i; \
	filter_write(f, n, dst); free(f); \
} \
static void Array_NN_fa##sfx(struct page **arr, struct page **src, struct page **dst) { \
	struct page *a = (arr && *arr) ? *arr : NULL; \
	struct page *s = (src && *src) ? *src : NULL; \
	if (!a || !s) return; \
	int n = a->nr_vars < s->nr_vars ? a->nr_vars : s->nr_vars; \
	bool *f = malloc(n * sizeof(bool)); \
	for (int i = 0; i < n; i++) f[i] = a->values[i].i cmpop s->values[i].i; \
	filter_and(f, n, dst); free(f); \
} \
static void Array_NN_fo##sfx(struct page **arr, struct page **src, struct page **dst) { \
	struct page *a = (arr && *arr) ? *arr : NULL; \
	struct page *s = (src && *src) ? *src : NULL; \
	if (!a || !s) return; \
	int n = a->nr_vars < s->nr_vars ? a->nr_vars : s->nr_vars; \
	bool *f = malloc(n * sizeof(bool)); \
	for (int i = 0; i < n; i++) f[i] = a->values[i].i cmpop s->values[i].i; \
	filter_or(f, n, dst); free(f); \
}
DEF_NN_FILTER(eq, ==) DEF_NN_FILTER(ne, !=) DEF_NN_FILTER(lo, <) DEF_NN_FILTER(hi, >)

/* ---- Filter: NS ---- */
#define DEF_NS_FILTER(sfx, cmpop) \
static void Array_NS_fw##sfx(struct page **arr, struct page **sarr, int m, struct page **dst) { \
	struct page *a = (arr && *arr) ? *arr : NULL; \
	struct page *s = (sarr && *sarr) ? *sarr : NULL; \
	if (!a || !s) return; \
	int n = a->nr_vars < s->nr_vars ? a->nr_vars : s->nr_vars; \
	bool *f = malloc(n * sizeof(bool)); \
	for (int i = 0; i < n; i++) f[i] = a->values[i].i cmpop smget(s, i, m); \
	filter_write(f, n, dst); free(f); \
} \
static void Array_NS_fa##sfx(struct page **arr, struct page **sarr, int m, struct page **dst) { \
	struct page *a = (arr && *arr) ? *arr : NULL; \
	struct page *s = (sarr && *sarr) ? *sarr : NULL; \
	if (!a || !s) return; \
	int n = a->nr_vars < s->nr_vars ? a->nr_vars : s->nr_vars; \
	bool *f = malloc(n * sizeof(bool)); \
	for (int i = 0; i < n; i++) f[i] = a->values[i].i cmpop smget(s, i, m); \
	filter_and(f, n, dst); free(f); \
} \
static void Array_NS_fo##sfx(struct page **arr, struct page **sarr, int m, struct page **dst) { \
	struct page *a = (arr && *arr) ? *arr : NULL; \
	struct page *s = (sarr && *sarr) ? *sarr : NULL; \
	if (!a || !s) return; \
	int n = a->nr_vars < s->nr_vars ? a->nr_vars : s->nr_vars; \
	bool *f = malloc(n * sizeof(bool)); \
	for (int i = 0; i < n; i++) f[i] = a->values[i].i cmpop smget(s, i, m); \
	filter_or(f, n, dst); free(f); \
}
DEF_NS_FILTER(eq, ==) DEF_NS_FILTER(ne, !=) DEF_NS_FILTER(lo, <) DEF_NS_FILTER(hi, >)

/* ---- Filter: SV ---- */
#define DEF_SV_FILTER(sfx, cmpop) \
static void Array_SV_fw##sfx(struct page **arr, int m, int num, struct page **dst) { \
	struct page *a = (arr && *arr) ? *arr : NULL; \
	if (!a) return; \
	bool *f = malloc(a->nr_vars * sizeof(bool)); \
	for (int i = 0; i < a->nr_vars; i++) f[i] = smget(a, i, m) cmpop num; \
	filter_write(f, a->nr_vars, dst); free(f); \
} \
static void Array_SV_fa##sfx(struct page **arr, int m, int num, struct page **dst) { \
	struct page *a = (arr && *arr) ? *arr : NULL; \
	if (!a) return; \
	bool *f = malloc(a->nr_vars * sizeof(bool)); \
	for (int i = 0; i < a->nr_vars; i++) f[i] = smget(a, i, m) cmpop num; \
	filter_and(f, a->nr_vars, dst); free(f); \
} \
static void Array_SV_fo##sfx(struct page **arr, int m, int num, struct page **dst) { \
	struct page *a = (arr && *arr) ? *arr : NULL; \
	if (!a) return; \
	bool *f = malloc(a->nr_vars * sizeof(bool)); \
	for (int i = 0; i < a->nr_vars; i++) f[i] = smget(a, i, m) cmpop num; \
	filter_or(f, a->nr_vars, dst); free(f); \
}
DEF_SV_FILTER(eq, ==) DEF_SV_FILTER(ne, !=) DEF_SV_FILTER(lo, <) DEF_SV_FILTER(hi, >)

static void Array_SV_fwra(struct page **arr, int m, int lo, int hi, struct page **dst) {
	struct page *a = (arr && *arr) ? *arr : NULL;
	if (!a) return;
	bool *f = malloc(a->nr_vars * sizeof(bool));
	for (int i = 0; i < a->nr_vars; i++) { int v = smget(a, i, m); f[i] = v >= lo && v <= hi; }
	filter_write(f, a->nr_vars, dst); free(f);
}
static void Array_SV_fara(struct page **arr, int m, int lo, int hi, struct page **dst) {
	struct page *a = (arr && *arr) ? *arr : NULL;
	if (!a) return;
	bool *f = malloc(a->nr_vars * sizeof(bool));
	for (int i = 0; i < a->nr_vars; i++) { int v = smget(a, i, m); f[i] = v >= lo && v <= hi; }
	filter_and(f, a->nr_vars, dst); free(f);
}
static void Array_SV_fora(struct page **arr, int m, int lo, int hi, struct page **dst) {
	struct page *a = (arr && *arr) ? *arr : NULL;
	if (!a) return;
	bool *f = malloc(a->nr_vars * sizeof(bool));
	for (int i = 0; i < a->nr_vars; i++) { int v = smget(a, i, m); f[i] = v >= lo && v <= hi; }
	filter_or(f, a->nr_vars, dst); free(f);
}

/* ---- Filter: SN ---- */
#define DEF_SN_FILTER(sfx, cmpop) \
static void Array_SN_fw##sfx(struct page **arr, int m, struct page **src, struct page **dst) { \
	struct page *a = (arr && *arr) ? *arr : NULL; \
	struct page *s = (src && *src) ? *src : NULL; \
	if (!a || !s) return; \
	int n = a->nr_vars < s->nr_vars ? a->nr_vars : s->nr_vars; \
	bool *f = malloc(n * sizeof(bool)); \
	for (int i = 0; i < n; i++) f[i] = smget(a, i, m) cmpop s->values[i].i; \
	filter_write(f, n, dst); free(f); \
} \
static void Array_SN_fa##sfx(struct page **arr, int m, struct page **src, struct page **dst) { \
	struct page *a = (arr && *arr) ? *arr : NULL; \
	struct page *s = (src && *src) ? *src : NULL; \
	if (!a || !s) return; \
	int n = a->nr_vars < s->nr_vars ? a->nr_vars : s->nr_vars; \
	bool *f = malloc(n * sizeof(bool)); \
	for (int i = 0; i < n; i++) f[i] = smget(a, i, m) cmpop s->values[i].i; \
	filter_and(f, n, dst); free(f); \
} \
static void Array_SN_fo##sfx(struct page **arr, int m, struct page **src, struct page **dst) { \
	struct page *a = (arr && *arr) ? *arr : NULL; \
	struct page *s = (src && *src) ? *src : NULL; \
	if (!a || !s) return; \
	int n = a->nr_vars < s->nr_vars ? a->nr_vars : s->nr_vars; \
	bool *f = malloc(n * sizeof(bool)); \
	for (int i = 0; i < n; i++) f[i] = smget(a, i, m) cmpop s->values[i].i; \
	filter_or(f, n, dst); free(f); \
}
DEF_SN_FILTER(eq, ==) DEF_SN_FILTER(ne, !=) DEF_SN_FILTER(lo, <) DEF_SN_FILTER(hi, >)

/* ---- Filter: SS ---- */
#define DEF_SS_FILTER(sfx, cmpop) \
static void Array_SS_fw##sfx(struct page **arr, int m, struct page **sarr, int ms, struct page **dst) { \
	struct page *a = (arr && *arr) ? *arr : NULL; \
	struct page *s = (sarr && *sarr) ? *sarr : NULL; \
	if (!a || !s) return; \
	int n = a->nr_vars < s->nr_vars ? a->nr_vars : s->nr_vars; \
	bool *f = malloc(n * sizeof(bool)); \
	for (int i = 0; i < n; i++) f[i] = smget(a, i, m) cmpop smget(s, i, ms); \
	filter_write(f, n, dst); free(f); \
} \
static void Array_SS_fa##sfx(struct page **arr, int m, struct page **sarr, int ms, struct page **dst) { \
	struct page *a = (arr && *arr) ? *arr : NULL; \
	struct page *s = (sarr && *sarr) ? *sarr : NULL; \
	if (!a || !s) return; \
	int n = a->nr_vars < s->nr_vars ? a->nr_vars : s->nr_vars; \
	bool *f = malloc(n * sizeof(bool)); \
	for (int i = 0; i < n; i++) f[i] = smget(a, i, m) cmpop smget(s, i, ms); \
	filter_and(f, n, dst); free(f); \
} \
static void Array_SS_fo##sfx(struct page **arr, int m, struct page **sarr, int ms, struct page **dst) { \
	struct page *a = (arr && *arr) ? *arr : NULL; \
	struct page *s = (sarr && *sarr) ? *sarr : NULL; \
	if (!a || !s) return; \
	int n = a->nr_vars < s->nr_vars ? a->nr_vars : s->nr_vars; \
	bool *f = malloc(n * sizeof(bool)); \
	for (int i = 0; i < n; i++) f[i] = smget(a, i, m) cmpop smget(s, i, ms); \
	filter_or(f, n, dst); free(f); \
}
DEF_SS_FILTER(eq, ==) DEF_SS_FILTER(ne, !=) DEF_SS_FILTER(lo, <) DEF_SS_FILTER(hi, >)

/* ---- Search: linear scan with forward/backward ---- */
#define DEF_NV_SC(sfx, cmpop) \
static int Array_NV_sc##sfx(struct page **self, int index, int num, int *out) { \
	struct page *a = (self && *self) ? *self : NULL; \
	if (!a) return 0; \
	if (index >= 0) { \
		for (int i = index; i < a->nr_vars; i++) \
			if (a->values[i].i cmpop num) { *out = i; return 1; } \
	} else { \
		int start = index & 0x7fffffff; \
		if (start >= a->nr_vars) return 0; \
		for (int i = start; i >= 0; --i) \
			if (a->values[i].i cmpop num) { *out = i; return 1; } \
	} \
	return 0; \
}
DEF_NV_SC(eq, ==) DEF_NV_SC(ne, !=) DEF_NV_SC(lo, <) DEF_NV_SC(hi, >)

static int Array_NV_scra(struct page **self, int index, int lo, int hi, int *out) {
	struct page *a = (self && *self) ? *self : NULL;
	if (!a) return 0;
	if (index >= 0) {
		for (int i = index; i < a->nr_vars; i++) {
			int v = a->values[i].i;
			if (v >= lo && v <= hi) { *out = i; return 1; }
		}
	} else {
		int start = index & 0x7fffffff;
		if (start >= a->nr_vars) return 0;
		for (int i = start; i >= 0; --i) {
			int v = a->values[i].i;
			if (v >= lo && v <= hi) { *out = i; return 1; }
		}
	}
	return 0;
}

#define DEF_SV_SC(sfx, cmpop) \
static int Array_SV_sc##sfx(struct page **self, int m, int index, int num, int *out) { \
	struct page *a = (self && *self) ? *self : NULL; \
	if (!a) return 0; \
	if (index >= 0) { \
		for (int i = index; i < a->nr_vars; i++) \
			if (smget(a, i, m) cmpop num) { *out = i; return 1; } \
	} else { \
		int start = index & 0x7fffffff; \
		if (start >= a->nr_vars) return 0; \
		for (int i = start; i >= 0; --i) \
			if (smget(a, i, m) cmpop num) { *out = i; return 1; } \
	} \
	return 0; \
}
DEF_SV_SC(eq, ==) DEF_SV_SC(ne, !=) DEF_SV_SC(lo, <) DEF_SV_SC(hi, >)

static int Array_SV_scra(struct page **self, int m, int index, int lo, int hi, int *out) {
	struct page *a = (self && *self) ? *self : NULL;
	if (!a) return 0;
	if (index >= 0) {
		for (int i = index; i < a->nr_vars; i++) {
			int v = smget(a, i, m);
			if (v >= lo && v <= hi) { *out = i; return 1; }
		}
	} else {
		int start = index & 0x7fffffff;
		if (start >= a->nr_vars) return 0;
		for (int i = start; i >= 0; --i) {
			int v = smget(a, i, m);
			if (v >= lo && v <= hi) { *out = i; return 1; }
		}
	}
	return 0;
}

/* ---- Search: sclowest/schighest — find index of min/max ---- */
static int Array_NN_sclowest(struct page **self, struct page **dst, int *out) {
	struct page *a = (self && *self) ? *self : NULL;
	struct page *d = (dst && *dst) ? *dst : NULL;
	if (!a || !d || d->nr_vars == 0) return 0;
	int best = 0, best_val = a->nr_vars > 0 ? a->values[d->values[0].i >= 0 && d->values[0].i < a->nr_vars ? d->values[0].i : 0].i : 0;
	for (int i = 0; i < d->nr_vars; i++) {
		int idx = d->values[i].i;
		if (idx < 0 || idx >= a->nr_vars) continue;
		if (i == 0 || a->values[idx].i < best_val) { best_val = a->values[idx].i; best = idx; }
	}
	*out = best; return 1;
}

static int Array_NN_schighest(struct page **self, struct page **dst, int *out) {
	struct page *a = (self && *self) ? *self : NULL;
	struct page *d = (dst && *dst) ? *dst : NULL;
	if (!a || !d || d->nr_vars == 0) return 0;
	int best = 0, best_val = 0;
	for (int i = 0; i < d->nr_vars; i++) {
		int idx = d->values[i].i;
		if (idx < 0 || idx >= a->nr_vars) continue;
		if (i == 0 || a->values[idx].i > best_val) { best_val = a->values[idx].i; best = idx; }
	}
	*out = best; return 1;
}

static int Array_NS_sclowest(struct page **self, struct page **sarr, int m, int *out) {
	struct page *a = (self && *self) ? *self : NULL;
	struct page *s = (sarr && *sarr) ? *sarr : NULL;
	if (!a || !s) return 0;
	int n = a->nr_vars < s->nr_vars ? a->nr_vars : s->nr_vars;
	if (n == 0) return 0;
	int best = 0, best_val = a->values[0].i;
	for (int i = 0; i < n; i++) {
		int sv = smget(s, i, m);
		if (sv < best_val) { best_val = sv; best = i; }
	}
	*out = best; return 1;
}

static int Array_NS_schighest(struct page **self, struct page **sarr, int m, int *out) {
	struct page *a = (self && *self) ? *self : NULL;
	struct page *s = (sarr && *sarr) ? *sarr : NULL;
	if (!a || !s) return 0;
	int n = a->nr_vars < s->nr_vars ? a->nr_vars : s->nr_vars;
	if (n == 0) return 0;
	int best = 0, best_val = a->values[0].i;
	for (int i = 0; i < n; i++) {
		int sv = smget(s, i, m);
		if (sv > best_val) { best_val = sv; best = i; }
	}
	*out = best; return 1;
}

static int Array_SN_sclowest(struct page **self, int m, struct page **src, int *out) {
	struct page *a = (self && *self) ? *self : NULL;
	struct page *s = (src && *src) ? *src : NULL;
	if (!a || !s) return 0;
	int n = a->nr_vars < s->nr_vars ? a->nr_vars : s->nr_vars;
	if (n == 0) return 0;
	int best = 0, best_val = smget(a, 0, m);
	for (int i = 1; i < n; i++) {
		int v = smget(a, i, m);
		if (v < best_val) { best_val = v; best = i; }
	}
	*out = best; return 1;
}

static int Array_SN_schighest(struct page **self, int m, struct page **src, int *out) {
	struct page *a = (self && *self) ? *self : NULL;
	struct page *s = (src && *src) ? *src : NULL;
	if (!a || !s) return 0;
	int n = a->nr_vars < s->nr_vars ? a->nr_vars : s->nr_vars;
	if (n == 0) return 0;
	int best = 0, best_val = smget(a, 0, m);
	for (int i = 1; i < n; i++) {
		int v = smget(a, i, m);
		if (v > best_val) { best_val = v; best = i; }
	}
	*out = best; return 1;
}

static int Array_SS_sclowest(struct page **self, int m, struct page **sarr, int ms, int *out) {
	struct page *a = (self && *self) ? *self : NULL;
	struct page *s = (sarr && *sarr) ? *sarr : NULL;
	if (!a || !s) return 0;
	int n = a->nr_vars < s->nr_vars ? a->nr_vars : s->nr_vars;
	if (n == 0) return 0;
	int best = 0, best_val = smget(a, 0, m);
	for (int i = 1; i < n; i++) {
		int v = smget(a, i, m);
		if (v < best_val) { best_val = v; best = i; }
	}
	*out = best; return 1;
}

static int Array_SS_schighest(struct page **self, int m, struct page **sarr, int ms, int *out) {
	struct page *a = (self && *self) ? *self : NULL;
	struct page *s = (sarr && *sarr) ? *sarr : NULL;
	if (!a || !s) return 0;
	int n = a->nr_vars < s->nr_vars ? a->nr_vars : s->nr_vars;
	if (n == 0) return 0;
	int best = 0, best_val = smget(a, 0, m);
	for (int i = 1; i < n; i++) {
		int v = smget(a, i, m);
		if (v > best_val) { best_val = v; best = i; }
	}
	*out = best; return 1;
}

/* ---- Aggregate: reduce array to scalar ---- */
static int Array_VN_add(struct page **self) {
	struct page *a = (self && *self) ? *self : NULL;
	if (!a) return 0;
	int r = 0;
	for (int i = 0; i < a->nr_vars; i++) r += a->values[i].i;
	return r;
}

static int Array_VN_and(struct page **self) {
	struct page *a = (self && *self) ? *self : NULL;
	if (!a || a->nr_vars == 0) return 0;
	int r = a->values[0].i;
	for (int i = 1; i < a->nr_vars; i++) r &= a->values[i].i;
	return r;
}

static int Array_VN_or(struct page **self) {
	struct page *a = (self && *self) ? *self : NULL;
	if (!a) return 0;
	int r = 0;
	for (int i = 0; i < a->nr_vars; i++) r |= a->values[i].i;
	return r;
}

static int Array_VS_add(struct page **self, int m) {
	struct page *a = (self && *self) ? *self : NULL;
	if (!a) return 0;
	int r = 0;
	for (int i = 0; i < a->nr_vars; i++) r += smget(a, i, m);
	return r;
}

static int Array_VS_and(struct page **self, int m) {
	struct page *a = (self && *self) ? *self : NULL;
	if (!a || a->nr_vars == 0) return 0;
	int r = smget(a, 0, m);
	for (int i = 1; i < a->nr_vars; i++) r &= smget(a, i, m);
	return r;
}

static int Array_VS_or(struct page **self, int m) {
	struct page *a = (self && *self) ? *self : NULL;
	if (!a) return 0;
	int r = 0;
	for (int i = 0; i < a->nr_vars; i++) r |= smget(a, i, m);
	return r;
}
// Realloc: resize array, preserving existing elements.
// For struct/wrap arrays (hll_arg3 == 2), allocates struct instances
// for any newly added elements (beyond the old size).
static void Array_Realloc(struct page **array, int new_size)
{
	if (!array || new_size < 0)
		return;
	struct page *old = *array;
	if (!old) {
		*array = alloc_page(ARRAY_PAGE, AIN_ARRAY_INT, new_size);
		(*array)->array.rank = 1;
		return;
	}
	int old_size = old->nr_vars;
	struct page *new_a = alloc_page(ARRAY_PAGE, old->a_type, new_size);
	new_a->array = old->array;
	int copy = old_size < new_size ? old_size : new_size;
	for (int i = 0; i < copy; i++)
		new_a->values[i] = old->values[i];
	// v14 struct/wrap arrays: allocate struct instances for new elements.
	if (array_elem_is_struct() && new_size > old_size) {
		int struct_type = new_a->array.struct_type;
		if (struct_type < 0 && hll_current_arg3 >= 0x10000)
			struct_type = hll_current_arg3 & 0xFFFF;
		if (struct_type >= 0 && struct_type < ain->nr_structures) {
			new_a->array.struct_type = struct_type;
			for (int i = old_size; i < new_size; i++) {
				int slot = alloc_struct(struct_type);
				heap_ref(slot);
				new_a->values[i].i = slot;
			}
		}
	}
	free_page(old);
	*array = new_a;
}

// ShallowCopy: return a shallow copy of the array
static int Array_ShallowCopy(struct page **self)
{
	struct page *src = (self && *self) ? *self : NULL;
	if (!src || src->type != ARRAY_PAGE || src->nr_vars == 0) {
		struct page *empty = alloc_page(ARRAY_PAGE, AIN_ARRAY_INT, 0);
		empty->array.rank = 1;
		int slot = heap_alloc_slot(VM_PAGE);
		heap_set_page(slot, empty);
		return slot;
	}
	struct page *copy = alloc_page(ARRAY_PAGE, src->a_type, src->nr_vars);
	copy->array = src->array;
	for (int i = 0; i < src->nr_vars; i++)
		copy->values[i] = src->values[i];
	int slot = heap_alloc_slot(VM_PAGE);
	heap_set_page(slot, copy);
	return slot;
}

// IsExist(value) must not interpret a small enum/integer as a function number.
static bool Array_IsExistValue(struct page **self, int value)
{
	const struct page *array = self ? *self : NULL;
	if (!array || array->type != ARRAY_PAGE)
		return false;
	int stride = array_erase_stride(array);
	if (stride > 2)
		return false;
	for (int i = 0; i < array->nr_vars / stride; i++) {
		if (array_erase_value_equal(array->values[i * stride], (union vm_value){.i = value})
		    && (stride == 1 || array->values[i * stride + 1].i == hll_param_slot2))
			return true;
	}
	return false;
}

// IsExist(predicate): the VM call owns the temporary callback argument refs.
static bool Array_IsExist(struct page **self, int func)
{
	struct page *array = (self && *self) ? *self : NULL;
	if (!array || array->type != ARRAY_PAGE || array->nr_vars == 0
	    || func < 0 || func >= ain->nr_functions)
		return false;

	struct ain_function *cb = &ain->functions[func];
	if (cb->nr_args < 1 || cb->nr_args > 2)
		return false;
	int stride = array_erase_stride(array);
	if (stride > 2)
		return false;

	for (int i = 0; *self && i < (*self)->nr_vars / stride; i++) {
		int saved_sp = stack_ptr;
		stack_push((*self)->values[i * stride]);
		if (cb->nr_args == 2)
			stack_push(stride > 1 ? (*self)->values[i * stride + 1] : (union vm_value){.i = 0});
		vm_call_nopop(func, cb->nr_args);
		int result = stack_pop().i;
		stack_ptr = saved_sp;
		if (result)
			return true;
	}
	return false;
}

void *array_isexist_function(const struct ain_hll_function *f)
{
	if (!f || !f->arguments || f->nr_arguments != 2 || f->return_type.data != AIN_BOOL)
		return NULL;
	switch (f->arguments[0].type.data) {
	case AIN_REF_ARRAY_TYPE:
	case AIN_REF_ARRAY:
		break;
	default:
		return NULL;
	}
	if (f->arguments[1].type.data == AIN_HLL_PARAM)
		return Array_IsExistValue;
	if (f->arguments[1].type.data == AIN_HLL_FUNC || f->arguments[1].type.data == AIN_HLL_FUNC_71)
		return Array_IsExist;
	return NULL;
}

// EmplaceBack: push a default value (like PushBack(0) for int arrays)
// EmplaceBack: append a default-constructed element and return it as wrap<T>.
// For struct elements (hll_arg3=2): create a new struct via alloc_struct().
// For int elements (hll_arg3=1): append 0.
// Returns the new element's value (heap slot for structs, 0 for ints).
static int Array_EmplaceBack(struct page **array)
{
	if (!array) return 0;
	struct page *a = *array;
	int old_size = a ? a->nr_vars : 0;
	struct page *new_a = alloc_page(ARRAY_PAGE, a ? a->a_type : AIN_ARRAY_INT, old_size + 1);
	if (a) {
		for (int i = 0; i < old_size; i++)
			new_a->values[i] = a->values[i];
		new_a->array = a->array;
		free_page(a);
	}
	int new_val = 0;
	// For struct/wrap elements, construct a new struct object.
	if (array_elem_is_struct()) {
		int struct_type = new_a->array.struct_type;
		if (struct_type < 0 && hll_current_arg3 >= 0x10000)
			struct_type = hll_current_arg3 & 0xFFFF;
		if (struct_type >= 0 && struct_type < ain->nr_structures) {
			new_a->array.struct_type = struct_type;
			new_val = alloc_struct(struct_type);
			heap_ref(new_val);
		}
	}
	new_a->values[old_size].i = new_val;
	*array = new_a;
	return new_val;
}

// Shuffle: Fisher-Yates in-place shuffle
static void Array_Shuffle(struct page **array, int seed)
{
	struct page *a = (array && *array) ? *array : NULL;
	if (!a || a->nr_vars <= 1)
		return;
	srand((unsigned)seed);
	for (int i = a->nr_vars - 1; i > 0; i--) {
		int j = rand() % (i + 1);
		union vm_value tmp = a->values[i];
		a->values[i] = a->values[j];
		a->values[j] = tmp;
	}
}

// The original Count() and Numof() entries share the same implementation.
static int Array_Count(struct page **self)
{
	return Array_Numof(self);
}

// AddRange: append all elements from src to dst
// AIN declares: AddRange(ref array<?> dst, wrap<?> src)
// arg[0] = AIN_REF_ARRAY → struct page **, arg[1] = AIN_WRAP → int (heap slot)
static void Array_AddRange(struct page **dst, int src_wrap)
{
	if (!dst)
		return;
	// Resolve wrap handle to source page
	struct page *s = NULL;
	if (src_wrap > 0 && (size_t)src_wrap < heap_size
	    && heap[src_wrap].type == VM_PAGE)
		s = heap[src_wrap].page;
	if (!s || s->type != ARRAY_PAGE || s->nr_vars <= 0)
		return;
	struct page *d = *dst;
	if (d && d->type != ARRAY_PAGE)
		return;
	int old_size = d ? d->nr_vars : 0;
	int new_size = old_size + s->nr_vars;
	// Save src values before any allocation (alloc_page may trigger GC)
	int src_count = s->nr_vars;
	union vm_value *src_vals = malloc(src_count * sizeof(union vm_value));
	if (!src_vals) return;
	for (int i = 0; i < src_count; i++)
		src_vals[i] = s->values[i];
	struct page *new_a = alloc_page(ARRAY_PAGE, d ? d->a_type : s->a_type, new_size);
	if (d) {
		for (int i = 0; i < old_size; i++)
			new_a->values[i] = d->values[i];
		new_a->array = d->array;
		free_page(d);
	} else {
		new_a->array.rank = 1;
	}
	for (int i = 0; i < src_count; i++) {
		new_a->values[old_size + i] = src_vals[i];
		if (array_elem_is_ref() && src_vals[i].i > 0)
			heap_ref(src_vals[i].i);
	}
	free(src_vals);
	*dst = new_a;
}

// SYSTEMONLY_GetStructPageList: internal debug function, no-op
static void Array_SYSTEMONLY_GetStructPageList(struct page **array)
{
}

// Add: alias for Pushback (v14 generic array)
static void Array_Add(struct page **array, int value)
{
	Array_Pushback(array, value);
}

/* Array query overloads. Ranges are [begin, end), in logical elements.
 * Numof(predicate) and Count(predicate) count matches, not callback values.
 * Keep the callback ABI consistent with IsExist/Erase; vm_call_nopop owns
 * argument retains and hll_call restores the enclosing generic context.
 */
/* Callback argument shape. Returns 1 when the callback takes an (array,
 * index) reference, 0 when it takes the element by value, and -1 with *why
 * set when the shape is not supported. ret is the declared callback return
 * type: bool for predicates (Find/Count/IsExist), int for the three-way
 * comparators of LowerBound/UpperBound/BinarySearch. */
static int array_callback_kind(struct page **array, int func, int stride,
			       enum ain_data_type ret, const char **why)
{
	if (func < 0 || func >= ain->nr_functions) {
		*why = "invalid callback";
		return -1;
	}
	struct ain_function *cb = &ain->functions[func];
	if (cb->address >= ain->code_size || cb->return_type.data != ret
	    || !cb->vars || cb->nr_vars < cb->nr_args || cb->nr_args < 1 || cb->nr_args > 2) {
		*why = "unsupported callback signature";
		return -1;
	}
	enum ain_data_type type = cb->vars[0].type.data;
	if (cb->nr_args == 1 && stride == 1) {
		switch (type) {
		case AIN_INT: case AIN_FLOAT: case AIN_BOOL: case AIN_LONG_INT:
		case AIN_ENUM: case AIN_ENUM2: case AIN_STRING: case AIN_REF_STRING:
		case AIN_STRUCT: case AIN_REF_STRUCT: case AIN_WRAP:
			return 0;
		default:
			break;
		}
	}
	if (cb->nr_args == 2 && cb->vars[1].type.data == AIN_VOID) {
		bool wrapped_iface = type == AIN_WRAP && cb->vars[0].type.array_type
			&& (cb->vars[0].type.array_type->data == AIN_IFACE
			    || cb->vars[0].type.array_type->data == AIN_IFACE_WRAP);
		if (stride == 2 && (type == AIN_IFACE || type == AIN_IFACE_WRAP || wrapped_iface))
			return 0;
		if (stride == 1 && (type == AIN_REF_INT || type == AIN_REF_FLOAT
		    || type == AIN_REF_BOOL || type == AIN_REF_LONG_INT)) {
			if (hll_self_slot < 0 || (size_t)hll_self_slot >= heap_size
			    || HEAP_REF(hll_self_slot) <= 0 || heap[hll_self_slot].type != VM_PAGE
			    || heap[hll_self_slot].page != *array) {
				*why = "callback reference has no array owner";
				return -1;
			}
			return 1;
		}
	}
	*why = "unsupported callback argument type";
	return -1;
}

/* Same check, but a mismatch stops the VM. Keep the callback ABI consistent
 * with IsExist/Erase; vm_call_nopop owns argument retains and hll_call
 * restores the enclosing generic context. */
static bool array_callback_shape(struct page **array, int func, int stride, enum ain_data_type ret)
{
	const char *why = NULL;
	int kind = array_callback_kind(array, func, stride, ret, &why);
	if (kind < 0) {
		int type = AIN_VOID;
		if (func >= 0 && func < ain->nr_functions && ain->functions[func].vars
		    && ain->functions[func].nr_vars > 0)
			type = ain->functions[func].vars[0].type.data;
		VM_ERROR("Array query: %s (fno %d, argument type %d, stride %d)", why, func, type, stride);
	}
	return kind == 1;
}

static bool array_query_callback_shape(struct page **array, int func, int stride)
{
	return array_callback_shape(array, func, stride, AIN_BOOL);
}

/* Call the callback on one logical element and return its raw int result. */
static int array_callback_call(struct page **array, int index, int stride, int func,
			       enum ain_data_type ret)
{
	struct ain_function *cb = &ain->functions[func];
	bool reference = array_callback_shape(array, func, stride, ret);
	struct page *before = *array;
	int size = before->nr_vars;
	int owner = hll_self_slot;
	bool tracked = owner >= 0 && (size_t)owner < heap_size && HEAP_REF(owner) > 0
		&& heap[owner].type == VM_PAGE && heap[owner].page == before;
	int saved_sp = stack_ptr;
	if (reference) {
		stack_push(owner);
		stack_push(index * stride);
	} else {
		stack_push(before->values[index * stride]);
		if (cb->nr_args == 2)
			stack_push(before->values[index * stride + 1]);
	}
	vm_call_nopop(func, cb->nr_args);
	int result = stack_pop().i;
	stack_ptr = saved_sp;
	// FFI passed a local page snapshot. A nested HLL can replace/free the
	// owner's page; refresh before touching it or outer FFI writes it back.
	if (tracked) {
		if ((size_t)owner >= heap_size || HEAP_REF(owner) <= 0 || heap[owner].type != VM_PAGE)
			VM_ERROR("Array query: callback released its array owner");
		*array = heap[owner].page;
	}
	if (*array != before || !*array || (*array)->type != ARRAY_PAGE || (*array)->nr_vars != size)
		VM_ERROR("Array query: callback changed array storage");
	return result;
}

static bool array_query_predicate(struct page **array, int index, int stride, int func)
{
	return array_callback_call(array, index, stride, func, AIN_BOOL) != 0;
}

static int Array_CountIf(struct page **array, int func)
{
	if (!array || !*array || (*array)->type != ARRAY_PAGE)
		return 0;
	int stride = array_erase_stride(*array);
	array_query_callback_shape(array, func, stride);
	int end = (*array)->nr_vars / stride;
	int matches = 0;
	for (int i = 0; i < end && *array && (*array)->type == ARRAY_PAGE
	     && i < (*array)->nr_vars / stride; i++) {
		if (array_query_predicate(array, i, stride, func))
			matches++;
	}
	return matches;
}

static enum ain_data_type array_query_value_type(const struct page *array)
{
	if (array->array.rank > 1 || array_elem_is_2slot())
		VM_ERROR("Array.Find: unsupported value element shape");
	switch (array->a_type) {
	case AIN_INT: case AIN_ARRAY_INT: case AIN_BOOL: case AIN_ARRAY_BOOL:
	case AIN_ENUM: case AIN_ENUM2:
		// Generic allocation can default to ARRAY_INT even for heap elements.
		if (array_elem_is_ref())
			VM_ERROR("Array.Find: reference element lacks a supported concrete type");
		return AIN_INT;
	case AIN_FLOAT: case AIN_ARRAY_FLOAT:
		return AIN_FLOAT;
	case AIN_STRING: case AIN_ARRAY_STRING:
		return AIN_STRING;
	default:
		VM_ERROR("Array.Find: unsupported or erased value element type %d", array->a_type);
	}
}

static bool array_query_value_equal(union vm_value a, union vm_value b, enum ain_data_type type)
{
	if (type == AIN_FLOAT)
		return a.f == b.f;
	if (type != AIN_STRING)
		return a.i == b.i;
	if (a.i <= 0 || b.i <= 0 || (size_t)a.i >= heap_size || (size_t)b.i >= heap_size
	    || HEAP_REF(a.i) <= 0 || HEAP_REF(b.i) <= 0
	    || heap[a.i].type != VM_STRING || heap[b.i].type != VM_STRING
	    || !heap[a.i].s || !heap[b.i].s)
		VM_ERROR("Array.Find: invalid string value");
	struct string *sa = heap[a.i].s, *sb = heap[b.i].s;
	return sa->size == sb->size && !memcmp(sa->text, sb->text, sa->size);
}

static int Array_FindValueRange(struct page **array, int begin, int end, int value)
{
	const struct page *src = array ? *array : NULL;
	if (!src || src->type != ARRAY_PAGE)
		return -1;
	enum ain_data_type type = array_query_value_type(src);
	int count = src->nr_vars;
	if (begin < 0) begin = 0;
	if (end > count) end = count;
	union vm_value needle = {.i = value};
	for (int i = begin; i < end; i++) {
		if (array_query_value_equal(src->values[i], needle, type))
			return i;
	}
	return -1;
}

static int Array_FindValue(struct page **array, int value)
{
	return Array_FindValueRange(array, 0, INT_MAX, value);
}

static int Array_FindIfRange(struct page **array, int begin, int end, int func)
{
	if (!array || !*array || (*array)->type != ARRAY_PAGE)
		return -1;
	int stride = array_erase_stride(*array);
	array_query_callback_shape(array, func, stride);
	int count = (*array)->nr_vars / stride;
	if (begin < 0) begin = 0;
	if (end > count) end = count;
	for (int i = begin; i < end && *array && (*array)->type == ARRAY_PAGE
	     && i < (*array)->nr_vars / stride; i++) {
		if (array_query_predicate(array, i, stride, func))
			return i;
	}
	return -1;
}

static int Array_FindIf(struct page **array, int func)
{
	return Array_FindIfRange(array, 0, INT_MAX, func);
}

/* Sorted-range search: LowerBound / UpperBound / BinarySearch.
 *
 * Native engine (dohnadohna_dump_SCY.exe, Array dispatcher 0x644300):
 *   50 LowerBound(value)    0x644b33 -> 0x648e40 -> core 0x646c70
 *   51 LowerBound(func)     0x644b4e -> 0x648ef0 -> core 0x646c70
 *   52 UpperBound(value)    0x644b69 -> 0x648fa0 -> core 0x646cc0
 *   53 UpperBound(func)     0x644b84 -> 0x649050 -> core 0x646cc0
 *   54 BinarySearch(value)  0x644b9f -> 0x649100 -> core 0x646d10
 *   55 BinarySearch(func)   0x644bba -> 0x6491b0 -> core 0x646d10
 * All three run lo = 0, hi = length, mid = (lo + hi) / 2 over a three-way
 * comparison cmp(i) = sign(elem[i] - target):
 *   LowerBound:   cmp < 0 -> lo = mid + 1, else hi = mid; return lo.
 *   UpperBound:   cmp <= 0 -> lo = mid + 1, else hi = mid; return lo.
 *   BinarySearch: cmp == 0 -> return mid; cmp < 0 -> lo = mid + 1,
 *                 else hi = mid; a miss returns -1.
 * An empty array returns 0 (bounds) or -1 (BinarySearch) without touching
 * the callback.
 *
 * The func overloads take a one-element comparator returning int; the key
 * is captured by the lambda, never passed, and the result is used as is.
 * The value overloads compare int/bool/enum by 32-bit wrapping subtraction,
 * float with NaN comparing equal, and strings bytewise then by length.
 */
enum array_bound_kind {
	ARRAY_LOWER_BOUND,
	ARRAY_UPPER_BOUND,
	ARRAY_BINARY_SEARCH,
};

struct array_bound_key {
	bool by_func;             // func overload: call the comparator
	int func;                 // comparator fno
	union vm_value value;     // value search target
	enum ain_data_type type;  // AIN_INT, AIN_FLOAT or AIN_STRING
	int stride;               // slots per logical element
};

static void array_bound_warn(enum array_bound_kind kind, const char *why, int func, int result)
{
	static const char *names[] = { "LowerBound", "UpperBound", "BinarySearch" };
	static int count;
	if (count++ < 8)
		WARNING("Array.%s: %s (fno %d); returning %d", names[kind], why, func, result);
}

// Element type for the value overloads; false keeps the old raw int compare.
static bool array_bound_value_type(const struct page *array, enum ain_data_type *type)
{
	if (array->array.rank > 1 || array_elem_is_2slot())
		return false;
	switch (array->a_type) {
	case AIN_FLOAT: case AIN_ARRAY_FLOAT:
		*type = AIN_FLOAT;
		return true;
	case AIN_STRING: case AIN_ARRAY_STRING:
		*type = AIN_STRING;
		return true;
	case AIN_INT: case AIN_ARRAY_INT: case AIN_BOOL: case AIN_ARRAY_BOOL:
	case AIN_ENUM: case AIN_ENUM2:
		if (!array_elem_is_ref()) {
			*type = AIN_INT;
			return true;
		}
		// PushBack on a NULL page records AIN_ARRAY_INT even for strings.
		if (array->nr_vars > 0) {
			int s = array->values[0].i;
			if (s > 0 && (size_t)s < heap_size && heap[s].type == VM_STRING) {
				*type = AIN_STRING;
				return true;
			}
		}
		return false;
	default:
		return false;
	}
}

static const struct string *array_bound_string(int slot)
{
	static struct string empty = { .size = 0 };
	if (slot <= 0 || (size_t)slot >= heap_size || HEAP_REF(slot) <= 0
	    || heap[slot].type != VM_STRING || !heap[slot].s)
		return &empty;
	return heap[slot].s;
}

static int array_bound_compare(struct page **array, int index, const struct array_bound_key *k)
{
	if (k->by_func)
		return array_callback_call(array, index, k->stride, k->func, AIN_INT);
	union vm_value e = (*array)->values[index];
	switch (k->type) {
	case AIN_FLOAT:
		if (k->value.f > e.f)
			return -1;
		return e.f > k->value.f;
	case AIN_STRING: {
		const struct string *a = array_bound_string(e.i);
		const struct string *b = array_bound_string(k->value.i);
		int n = a->size < b->size ? a->size : b->size;
		int r = n ? memcmp(a->text, b->text, n) : 0;
		if (r)
			return r < 0 ? -1 : 1;
		return (a->size > b->size) - (a->size < b->size);
	}
	default:
		return (int32_t)((uint32_t)e.i - (uint32_t)k->value.i);
	}
}

static int array_bound_search(struct page **array, enum array_bound_kind kind,
			      struct array_bound_key *k)
{
	const int miss = kind == ARRAY_BINARY_SEARCH ? -1 : 0;
	if (!array || !*array || (*array)->type != ARRAY_PAGE)
		return miss;
	k->stride = k->by_func ? array_erase_stride(*array) : 1;
	int count = (*array)->nr_vars / k->stride;
	if (count <= 0)
		return miss;
	if (k->by_func) {
		const char *why = NULL;
		if (array_callback_kind(array, k->func, k->stride, AIN_INT, &why) < 0) {
			// Interface arrays built by Insert do not keep two slots per
			// element yet, so their comparators do not fit. The old binding
			// returned an arbitrary position here; do not stop the game.
			int fallback = kind == ARRAY_BINARY_SEARCH ? -1 : count;
			array_bound_warn(kind, why, k->func, fallback);
			return fallback;
		}
	} else if (!array_bound_value_type(*array, &k->type)) {
		k->type = AIN_INT;
	}
	int lo = 0, hi = count;
	while (lo < hi) {
		int mid = (lo + hi) / 2;
		int c = array_bound_compare(array, mid, k);
		if (kind == ARRAY_BINARY_SEARCH && c == 0)
			return mid;
		if (kind == ARRAY_UPPER_BOUND ? c <= 0 : c < 0)
			lo = mid + 1;
		else
			hi = mid;
	}
	return kind == ARRAY_BINARY_SEARCH ? -1 : lo;
}

static int Array_LowerBoundValue(struct page **array, int value)
{
	struct array_bound_key k = { .by_func = false, .value = { .i = value } };
	return array_bound_search(array, ARRAY_LOWER_BOUND, &k);
}

static int Array_LowerBoundIf(struct page **array, int func)
{
	struct array_bound_key k = { .by_func = true, .func = func };
	return array_bound_search(array, ARRAY_LOWER_BOUND, &k);
}

static int Array_UpperBoundValue(struct page **array, int value)
{
	struct array_bound_key k = { .by_func = false, .value = { .i = value } };
	return array_bound_search(array, ARRAY_UPPER_BOUND, &k);
}

static int Array_UpperBoundIf(struct page **array, int func)
{
	struct array_bound_key k = { .by_func = true, .func = func };
	return array_bound_search(array, ARRAY_UPPER_BOUND, &k);
}

static int Array_BinarySearchValue(struct page **array, int value)
{
	struct array_bound_key k = { .by_func = false, .value = { .i = value } };
	return array_bound_search(array, ARRAY_BINARY_SEARCH, &k);
}

static int Array_BinarySearchIf(struct page **array, int func)
{
	struct array_bound_key k = { .by_func = true, .func = func };
	return array_bound_search(array, ARRAY_BINARY_SEARCH, &k);
}

static void *array_bound_function(const struct ain_hll_function *f)
{
	if (!f->arguments || f->nr_arguments != 2 || f->return_type.data != AIN_INT)
		return NULL;
	switch (f->arguments[0].type.data) {
	case AIN_REF_ARRAY_TYPE:
	case AIN_REF_ARRAY:
		break;
	default:
		return NULL;
	}
	enum ain_data_type arg = f->arguments[1].type.data;
	bool by_func = arg == AIN_HLL_FUNC || arg == AIN_HLL_FUNC_71;
	if (!by_func && arg != AIN_HLL_PARAM)
		return NULL;
	if (!strcmp(f->name, "LowerBound"))
		return by_func ? (void *)Array_LowerBoundIf : (void *)Array_LowerBoundValue;
	if (!strcmp(f->name, "UpperBound"))
		return by_func ? (void *)Array_UpperBoundIf : (void *)Array_UpperBoundValue;
	return by_func ? (void *)Array_BinarySearchIf : (void *)Array_BinarySearchValue;
}

/* Select by declared signature, never by a game's function index. */
void *array_query_function(const struct ain_hll_function *f)
{
	if (!f || !f->name || !f->arguments || f->nr_arguments < 1 || f->return_type.data != AIN_INT)
		return NULL;
	switch (f->arguments[0].type.data) {
	case AIN_REF_ARRAY_TYPE:
	case AIN_REF_ARRAY:
		break;
	default:
		return NULL;
	}
	bool count = !strcmp(f->name, "Numof") || !strcmp(f->name, "Count");
	if (count) {
		if (f->nr_arguments == 1)
			return Array_Numof;
		if (f->nr_arguments == 2 && (f->arguments[1].type.data == AIN_HLL_FUNC
		    || f->arguments[1].type.data == AIN_HLL_FUNC_71))
			return Array_CountIf;
		return NULL;
	}
	if (strcmp(f->name, "Find"))
		return NULL;
	bool range = f->nr_arguments == 4;
	if (range) {
		if (f->arguments[1].type.data != AIN_INT || f->arguments[2].type.data != AIN_INT)
			return NULL;
	} else if (f->nr_arguments != 2) {
		return NULL;
	}
	enum ain_data_type arg = f->arguments[range ? 3 : 1].type.data;
	if (arg == AIN_HLL_PARAM)
		return range ? (void *)Array_FindValueRange : (void *)Array_FindValue;
	if (arg == AIN_HLL_FUNC || arg == AIN_HLL_FUNC_71)
		return range ? (void *)Array_FindIfRange : (void *)Array_FindIf;
	return NULL;
}

static bool array_decl_has_func(const struct ain_hll_function *f)
{
	for (int i = 0; i < f->nr_arguments; i++) {
		enum ain_data_type t = f->arguments[i].type.data;
		if (t == AIN_HLL_FUNC || t == AIN_HLL_FUNC_71)
			return true;
	}
	return false;
}

/*
 * Single entry point for Array overloads. The v14 Array library declares
 * several functions under one name (First(self) and First(self, func), ...)
 * and libffi builds each call from the declaration, so every shape needs a C
 * implementation with a matching prototype. Returns the implementation for f.
 * Shapes not handled here keep the name-matched default, so games that do not
 * use these overloads link exactly as before.
 */
void *array_select_function(const struct ain_hll_function *f, void *fallback)
{
	if (!f || !f->name)
		return fallback;
	if (!strcmp(f->name, "Erase"))
		return array_erase_function(f);
	if (!strcmp(f->name, "IsExist"))
		return array_isexist_function(f);
	if (!strcmp(f->name, "Numof") || !strcmp(f->name, "Count") || !strcmp(f->name, "Find"))
		return array_query_function(f);
	if (!strcmp(f->name, "First"))
		return array_decl_has_func(f) ? (void *)Array_First : (void *)Array_First_NoPred;
	if (!strcmp(f->name, "LowerBound") || !strcmp(f->name, "UpperBound")
	    || !strcmp(f->name, "BinarySearch")) {
		void *fn = array_bound_function(f);
		return fn ? fn : fallback;
	}
	return fallback;
}

// Copy: copy elements between arrays.
// AIN declares: Copy(ref array<?> dst, wrap<?> dst_i, wrap<array<?>> src, int src_i, int count)
// arg[1] = AIN_WRAP (dst_i as wrap<int>), arg[2] = AIN_WRAP (src as wrap<array>)
// FFI passes both as int (heap slot index).
static void Array_Copy(struct page **dst, int dst_i_wrap, int src_wrap, int src_i, int count)
{
	if (!dst || !*dst || count <= 0)
		return;
	// Resolve wrap<int> handle for dst_i
	int dst_i = dst_i_wrap;
	if (dst_i_wrap > 0 && (size_t)dst_i_wrap < heap_size
	    && heap[dst_i_wrap].type == VM_PAGE
	    && heap[dst_i_wrap].page
	    && heap[dst_i_wrap].page->nr_vars > 0)
		dst_i = heap[dst_i_wrap].page->values[0].i;
	// Resolve wrap<array<?>> handle for src
	struct page *src_page = NULL;
	if (src_wrap > 0 && (size_t)src_wrap < heap_size
	    && heap[src_wrap].type == VM_PAGE)
		src_page = heap[src_wrap].page;
	if (!src_page || src_page->type != ARRAY_PAGE)
		return;
	array_copy(*dst, dst_i, src_page, src_i, count);
}

// Fill: fill array elements with a value.
// AIN signature: Fill(array, value, start_index, count)
static void Array_Fill(struct page **array, int value, int start, int count)
{
	struct page *a = (array && *array) ? *array : NULL;
	if (!a || count <= 0)
		return;
	int end = start + count;
	if (end > a->nr_vars)
		end = a->nr_vars;
	for (int i = start; i < end; i++) {
		if (array_elem_is_ref()) {
			// Unref old, ref new
			int old = a->values[i].i;
			if (old > 0)
				heap_unref(old);
			if (value > 0)
				heap_ref(value);
		}
		a->values[i].i = value;
	}
}

// Concat: append all elements from src to self (both wrap<array>)
// AIN: Concat(wrap<array<T>> self, wrap<array<T>> src) -> void
static void Array_Concat(struct page **self, int src_wrap)
{
	// self is ref array, src is wrap<array> (heap slot)
	Array_AddRange(self, src_wrap);
}

// Max: find element with maximum value via delegate comparison.
// Returns AIN_REF_HLL_PARAM: pushes directly to VM stack (same as At/Last/First).
// For struct types (arg3==2): push 1 slot (heap slot of best element).
// For simple types: push 2 slots [array_heap_slot, element_index].
static int Array_Max(struct page **array, int func)
{
	struct page *src = (array && *array) ? *array : NULL;
	if (!src || src->nr_vars == 0) {
		if (!array_elem_is_ref()) {
			stack_push(-1);
			stack_push(0);
		} else {
			stack_push(0);
		}
		return 0;
	}
	if (func < 0 || func >= ain->nr_functions) {
		// No comparator — find max value directly (for int arrays)
		int best_idx = 0;
		for (int i = 1; i < src->nr_vars; i++) {
			if (src->values[i].i > src->values[best_idx].i)
				best_idx = i;
		}
		if (!array_elem_is_ref()) {
			stack_push(hll_self_slot);
			stack_push(best_idx);
		} else {
			int val = src->values[best_idx].i;
			if (val > 0)
				heap_ref(val);
			stack_push(val);
		}
		return 0;
	}

	struct ain_function *cb = &ain->functions[func];
	int best_idx = 0;
	int best_score = INT32_MIN;

	for (int i = 0; i < src->nr_vars; i++) {
		int saved_sp = stack_ptr;
		if (cb->nr_args >= 2) {
			stack_push(src->values[i]);
			stack_push(0);
		} else {
			stack_push(src->values[i]);
		}
		vm_call_nopop(func, cb->nr_args);
		int score = stack_pop().i;
		stack_ptr = saved_sp;
		if (score > best_score) {
			best_score = score;
			best_idx = i;
		}
	}

	if (!array_elem_is_ref()) {
		stack_push(hll_self_slot);
		stack_push(best_idx);
	} else {
		int val = src->values[best_idx].i;
		if (val > 0)
			heap_ref(val);
		stack_push(val);
	}
	return 0;
}


static void Array_Reverse(struct page **array)
{
	if (!array || !*array)
		return;
	array_reverse(*array);
}

/* Duplicate: copy array elements.
 * AIN: Duplicate(ref array, ref array_src) — copy src into dst */
static void Array_Duplicate(struct page **dst, struct page **src)
{
	if (!dst || !src || !*src)
		return;
	struct page *s = *src;
	if (*dst) {
		delete_page_vars(*dst);
		free_page(*dst);
	}
	struct page *new_a = alloc_page(ARRAY_PAGE, s->a_type, s->nr_vars);
	new_a->array = s->array;
	for (int i = 0; i < s->nr_vars; i++) {
		new_a->values[i] = s->values[i];
		if (array_elem_is_ref() && s->values[i].i > 0)
			heap_ref(s->values[i].i);
	}
	*dst = new_a;
}

/* All: return true if all elements pass predicate */
static bool Array_All(struct page **array, int func)
{
	struct page *src = (array && *array) ? *array : NULL;
	if (!src || src->nr_vars == 0)
		return true;
	if (func < 0 || func >= ain->nr_functions)
		return false;
	struct ain_function *cb = &ain->functions[func];
	for (int i = 0; i < src->nr_vars; i++) {
		int saved_sp = stack_ptr;
		if (cb->nr_args >= 2) {
			stack_push(src->values[i]);
			stack_push(0);
		} else {
			stack_push(src->values[i]);
		}
		vm_call_nopop(func, cb->nr_args);
		int result = stack_pop().i;
		stack_ptr = saved_sp;
		if (!result)
			return false;
	}
	return true;
}

static int qsort_int_asc(const void *a, const void *b)
{
	int ia = ((const union vm_value *)a)->i;
	int ib = ((const union vm_value *)b)->i;
	return (ia > ib) - (ia < ib);
}

static int qsort_int_desc(const void *a, const void *b)
{
	int ia = ((const union vm_value *)a)->i;
	int ib = ((const union vm_value *)b)->i;
	return (ib > ia) - (ib < ia);
}

/* AscSort: sort array ascending */
static void Array_AscSort(struct page **array)
{
	if (!array || !*array || (*array)->nr_vars <= 1)
		return;
	struct page *a = *array;
	qsort(a->values, a->nr_vars, sizeof(union vm_value), qsort_int_asc);
}

/* DescSort: sort array descending */
static void Array_DescSort(struct page **array)
{
	if (!array || !*array || (*array)->nr_vars <= 1)
		return;
	struct page *a = *array;
	qsort(a->values, a->nr_vars, sizeof(union vm_value), qsort_int_desc);
}

/* FindLast: find last element matching value or predicate */
static int Array_FindLast(struct page **array, int value)
{
	struct page *src = (array && *array) ? *array : NULL;
	if (!src || src->nr_vars == 0)
		return -1;
	for (int i = src->nr_vars - 1; i >= 0; i--) {
		if (src->values[i].i == value)
			return i;
	}
	return -1;
}

/* Min: find minimum element */
static int Array_Min(struct page **array)
{
	struct page *src = (array && *array) ? *array : NULL;
	if (!src || src->nr_vars == 0)
		return 0;
	int min_val = src->values[0].i;
	for (int i = 1; i < src->nr_vars; i++) {
		if (src->values[i].i < min_val)
			min_val = src->values[i].i;
	}
	return min_val;
}

/* Remain: keep elements matching predicate (opposite of EraseAll) */
static void Array_Remain(struct page **array, int func)
{
	struct page *src = (array && *array) ? *array : NULL;
	if (!src || src->nr_vars == 0 || func < 0 || func >= ain->nr_functions)
		return;
	struct ain_function *cb = &ain->functions[func];
	int *keep = malloc(src->nr_vars * sizeof(int));
	int keep_count = 0;
	for (int i = 0; i < src->nr_vars; i++) {
		int saved_sp = stack_ptr;
		if (cb->nr_args >= 2) {
			stack_push(src->values[i]);
			stack_push(0);
		} else {
			stack_push(src->values[i]);
		}
		vm_call_nopop(func, cb->nr_args);
		int result = stack_pop().i;
		stack_ptr = saved_sp;
		if (result)
			keep[keep_count++] = i;
	}
	if (keep_count < src->nr_vars) {
		struct page *new_a = alloc_page(ARRAY_PAGE, src->a_type, keep_count);
		new_a->array = src->array;
		for (int i = 0; i < keep_count; i++) {
			new_a->values[i] = src->values[keep[i]];
			if (array_elem_is_ref() && new_a->values[i].i > 0)
				heap_ref(new_a->values[i].i);
		}
		delete_page_vars(src);
		free_page(src);
		*array = new_a;
	}
	free(keep);
}

/* UniqueSorted: remove consecutive duplicates */
static void Array_UniqueSorted(struct page **array)
{
	Array_Unique(array);
}

/* Equals: check if two arrays are equal */
static bool Array_Equals(struct page **a, struct page **b)
{
	struct page *pa = (a && *a) ? *a : NULL;
	struct page *pb = (b && *b) ? *b : NULL;
	if (!pa && !pb) return true;
	if (!pa || !pb) return false;
	if (pa->nr_vars != pb->nr_vars) return false;
	for (int i = 0; i < pa->nr_vars; i++) {
		if (pa->values[i].i != pb->values[i].i)
			return false;
	}
	return true;
}

HLL_LIBRARY(Array,
	    HLL_EXPORT(Alloc, Array_Alloc),
	    HLL_EXPORT(Free, Array_Free),
	    HLL_EXPORT(Numof, Array_Numof),
	    HLL_EXPORT(Empty, Array_Empty),
	    HLL_EXPORT(At, Array_At),
	    HLL_EXPORT(Last, Array_Last),
	    HLL_EXPORT(PushBack, Array_PushBack),
	    HLL_EXPORT(Pushback, Array_Pushback),
	    HLL_EXPORT(PopBack, Array_PopBack),
	    HLL_EXPORT(Clear, Array_Clear),
	    HLL_EXPORT(EraseAll, Array_EraseAll),
	    HLL_EXPORT(Where, Array_Where),
	    HLL_EXPORT(First, Array_First),
	    HLL_EXPORT(Popback, Array_Popback),
	    HLL_EXPORT(Erase, Array_Erase),
	    HLL_EXPORT(Insert, Array_Insert),
	    HLL_EXPORT(Sort, Array_Sort),
	    HLL_EXPORT(Unique, Array_Unique),
	    HLL_EXPORT(QuickSort, Array_QuickSort),
	    HLL_EXPORT(Any, Array_Any),
	    HLL_EXPORT(Add, Array_Add),
	HLL_EXPORT(Find, Array_FindValue),
	    HLL_EXPORT(Realloc, Array_Realloc),
	    HLL_EXPORT(BinarySearch, Array_BinarySearchValue),
	    HLL_EXPORT(LowerBound, Array_LowerBoundValue),
	    HLL_EXPORT(ShallowCopy, Array_ShallowCopy),
	    HLL_EXPORT(IsExist, Array_IsExist),
	    HLL_EXPORT(EmplaceBack, Array_EmplaceBack),
	    HLL_EXPORT(Copy, Array_Copy),
	    HLL_EXPORT(Shuffle, Array_Shuffle),
	    HLL_EXPORT(Count, Array_Count),
	    HLL_EXPORT(AddRange, Array_AddRange),
	    HLL_EXPORT(Fill, Array_Fill),
	    HLL_EXPORT(SYSTEMONLY_GetStructPageList, Array_SYSTEMONLY_GetStructPageList),
	    HLL_EXPORT(Concat, Array_Concat),
	    HLL_EXPORT(Max, Array_Max),
	    HLL_EXPORT(Reverse, Array_Reverse),
	    HLL_EXPORT(NV_copy, Array_NV_copy),
	    HLL_EXPORT(NV_add, Array_NV_add),
	    HLL_EXPORT(NV_sub, Array_NV_sub),
	    HLL_EXPORT(NV_mul, Array_NV_mul),
	    HLL_EXPORT(NV_div, Array_NV_div),
	    HLL_EXPORT(NV_and, Array_NV_and),
	    HLL_EXPORT(NV_or, Array_NV_or),
	    HLL_EXPORT(NV_xor, Array_NV_xor),
	    HLL_EXPORT(NV_min, Array_NV_min),
	    HLL_EXPORT(NV_max, Array_NV_max),
	    HLL_EXPORT(NN_copy, Array_NN_copy),
	    HLL_EXPORT(NN_add, Array_NN_add),
	    HLL_EXPORT(NN_sub, Array_NN_sub),
	    HLL_EXPORT(NN_mul, Array_NN_mul),
	    HLL_EXPORT(NN_div, Array_NN_div),
	    HLL_EXPORT(NN_and, Array_NN_and),
	    HLL_EXPORT(NN_or, Array_NN_or),
	    HLL_EXPORT(NN_xor, Array_NN_xor),
	    HLL_EXPORT(NN_min, Array_NN_min),
	    HLL_EXPORT(NN_max, Array_NN_max),
	    HLL_EXPORT(NS_copy, Array_NS_copy),
	    HLL_EXPORT(NS_add, Array_NS_add),
	    HLL_EXPORT(NS_sub, Array_NS_sub),
	    HLL_EXPORT(NS_mul, Array_NS_mul),
	    HLL_EXPORT(NS_div, Array_NS_div),
	    HLL_EXPORT(NS_and, Array_NS_and),
	    HLL_EXPORT(NS_or, Array_NS_or),
	    HLL_EXPORT(NS_xor, Array_NS_xor),
	    HLL_EXPORT(NS_min, Array_NS_min),
	    HLL_EXPORT(NS_max, Array_NS_max),
	    HLL_EXPORT(SV_copy, Array_SV_copy),
	    HLL_EXPORT(SV_add, Array_SV_add),
	    HLL_EXPORT(SV_sub, Array_SV_sub),
	    HLL_EXPORT(SV_mul, Array_SV_mul),
	    HLL_EXPORT(SV_div, Array_SV_div),
	    HLL_EXPORT(SV_and, Array_SV_and),
	    HLL_EXPORT(SV_or, Array_SV_or),
	    HLL_EXPORT(SV_xor, Array_SV_xor),
	    HLL_EXPORT(SV_min, Array_SV_min),
	    HLL_EXPORT(SV_max, Array_SV_max),
	    HLL_EXPORT(SN_copy, Array_SN_copy),
	    HLL_EXPORT(SN_add, Array_SN_add),
	    HLL_EXPORT(SN_sub, Array_SN_sub),
	    HLL_EXPORT(SN_mul, Array_SN_mul),
	    HLL_EXPORT(SN_div, Array_SN_div),
	    HLL_EXPORT(SN_and, Array_SN_and),
	    HLL_EXPORT(SN_or, Array_SN_or),
	    HLL_EXPORT(SN_xor, Array_SN_xor),
	    HLL_EXPORT(SN_min, Array_SN_min),
	    HLL_EXPORT(SN_max, Array_SN_max),
	    HLL_EXPORT(SS_copy, Array_SS_copy),
	    HLL_EXPORT(SS_add, Array_SS_add),
	    HLL_EXPORT(SS_sub, Array_SS_sub),
	    HLL_EXPORT(SS_mul, Array_SS_mul),
	    HLL_EXPORT(SS_div, Array_SS_div),
	    HLL_EXPORT(SS_and, Array_SS_and),
	    HLL_EXPORT(SS_or, Array_SS_or),
	    HLL_EXPORT(SS_xor, Array_SS_xor),
	    HLL_EXPORT(SS_min, Array_SS_min),
	    HLL_EXPORT(SS_max, Array_SS_max),
	    HLL_EXPORT(NV_eneq, Array_NV_eneq),
	    HLL_EXPORT(NV_enne, Array_NV_enne),
	    HLL_EXPORT(NV_enlo, Array_NV_enlo),
	    HLL_EXPORT(NV_enhi, Array_NV_enhi),
	    HLL_EXPORT(NV_enra, Array_NV_enra),
	    HLL_EXPORT(NN_eneq, Array_NN_eneq),
	    HLL_EXPORT(NN_enne, Array_NN_enne),
	    HLL_EXPORT(NN_enlo, Array_NN_enlo),
	    HLL_EXPORT(NN_enhi, Array_NN_enhi),
	    HLL_EXPORT(NS_eneq, Array_NS_eneq),
	    HLL_EXPORT(NS_enne, Array_NS_enne),
	    HLL_EXPORT(NS_enlo, Array_NS_enlo),
	    HLL_EXPORT(NS_enhi, Array_NS_enhi),
	    HLL_EXPORT(SV_eneq, Array_SV_eneq),
	    HLL_EXPORT(SV_enne, Array_SV_enne),
	    HLL_EXPORT(SV_enlo, Array_SV_enlo),
	    HLL_EXPORT(SV_enhi, Array_SV_enhi),
	    HLL_EXPORT(SV_enra, Array_SV_enra),
	    HLL_EXPORT(SN_eneq, Array_SN_eneq),
	    HLL_EXPORT(SN_enne, Array_SN_enne),
	    HLL_EXPORT(SN_enlo, Array_SN_enlo),
	    HLL_EXPORT(SN_enhi, Array_SN_enhi),
	    HLL_EXPORT(SS_eneq, Array_SS_eneq),
	    HLL_EXPORT(SS_enne, Array_SS_enne),
	    HLL_EXPORT(SS_enlo, Array_SS_enlo),
	    HLL_EXPORT(SS_enhi, Array_SS_enhi),
	    HLL_EXPORT(NV_cheq, Array_NV_cheq),
	    HLL_EXPORT(NV_chne, Array_NV_chne),
	    HLL_EXPORT(NV_chlo, Array_NV_chlo),
	    HLL_EXPORT(NV_chhi, Array_NV_chhi),
	    HLL_EXPORT(NV_chra, Array_NV_chra),
	    HLL_EXPORT(NN_cheq, Array_NN_cheq),
	    HLL_EXPORT(NN_chne, Array_NN_chne),
	    HLL_EXPORT(NN_chlo, Array_NN_chlo),
	    HLL_EXPORT(NN_chhi, Array_NN_chhi),
	    HLL_EXPORT(NS_cheq, Array_NS_cheq),
	    HLL_EXPORT(NS_chne, Array_NS_chne),
	    HLL_EXPORT(NS_chlo, Array_NS_chlo),
	    HLL_EXPORT(NS_chhi, Array_NS_chhi),
	    HLL_EXPORT(SV_cheq, Array_SV_cheq),
	    HLL_EXPORT(SV_chne, Array_SV_chne),
	    HLL_EXPORT(SV_chlo, Array_SV_chlo),
	    HLL_EXPORT(SV_chhi, Array_SV_chhi),
	    HLL_EXPORT(SV_chra, Array_SV_chra),
	    HLL_EXPORT(SN_cheq, Array_SN_cheq),
	    HLL_EXPORT(SN_chne, Array_SN_chne),
	    HLL_EXPORT(SN_chlo, Array_SN_chlo),
	    HLL_EXPORT(SN_chhi, Array_SN_chhi),
	    HLL_EXPORT(SS_cheq, Array_SS_cheq),
	    HLL_EXPORT(SS_chne, Array_SS_chne),
	    HLL_EXPORT(SS_chlo, Array_SS_chlo),
	    HLL_EXPORT(SS_chhi, Array_SS_chhi),
	    HLL_EXPORT(NV_fweq, Array_NV_fweq),
	    HLL_EXPORT(NV_fwne, Array_NV_fwne),
	    HLL_EXPORT(NV_fwlo, Array_NV_fwlo),
	    HLL_EXPORT(NV_fwhi, Array_NV_fwhi),
	    HLL_EXPORT(NV_fwra, Array_NV_fwra),
	    HLL_EXPORT(NV_faeq, Array_NV_faeq),
	    HLL_EXPORT(NV_fane, Array_NV_fane),
	    HLL_EXPORT(NV_falo, Array_NV_falo),
	    HLL_EXPORT(NV_fahi, Array_NV_fahi),
	    HLL_EXPORT(NV_fara, Array_NV_fara),
	    HLL_EXPORT(NV_foeq, Array_NV_foeq),
	    HLL_EXPORT(NV_fone, Array_NV_fone),
	    HLL_EXPORT(NV_folo, Array_NV_folo),
	    HLL_EXPORT(NV_fohi, Array_NV_fohi),
	    HLL_EXPORT(NV_fora, Array_NV_fora),
	    HLL_EXPORT(NN_fweq, Array_NN_fweq),
	    HLL_EXPORT(NN_fwne, Array_NN_fwne),
	    HLL_EXPORT(NN_fwlo, Array_NN_fwlo),
	    HLL_EXPORT(NN_fwhi, Array_NN_fwhi),
	    HLL_EXPORT(NN_faeq, Array_NN_faeq),
	    HLL_EXPORT(NN_fane, Array_NN_fane),
	    HLL_EXPORT(NN_falo, Array_NN_falo),
	    HLL_EXPORT(NN_fahi, Array_NN_fahi),
	    HLL_EXPORT(NN_foeq, Array_NN_foeq),
	    HLL_EXPORT(NN_fone, Array_NN_fone),
	    HLL_EXPORT(NN_folo, Array_NN_folo),
	    HLL_EXPORT(NN_fohi, Array_NN_fohi),
	    HLL_EXPORT(NS_fweq, Array_NS_fweq),
	    HLL_EXPORT(NS_fwne, Array_NS_fwne),
	    HLL_EXPORT(NS_fwlo, Array_NS_fwlo),
	    HLL_EXPORT(NS_fwhi, Array_NS_fwhi),
	    HLL_EXPORT(NS_faeq, Array_NS_faeq),
	    HLL_EXPORT(NS_fane, Array_NS_fane),
	    HLL_EXPORT(NS_falo, Array_NS_falo),
	    HLL_EXPORT(NS_fahi, Array_NS_fahi),
	    HLL_EXPORT(NS_foeq, Array_NS_foeq),
	    HLL_EXPORT(NS_fone, Array_NS_fone),
	    HLL_EXPORT(NS_folo, Array_NS_folo),
	    HLL_EXPORT(NS_fohi, Array_NS_fohi),
	    HLL_EXPORT(SV_fweq, Array_SV_fweq),
	    HLL_EXPORT(SV_fwne, Array_SV_fwne),
	    HLL_EXPORT(SV_fwlo, Array_SV_fwlo),
	    HLL_EXPORT(SV_fwhi, Array_SV_fwhi),
	    HLL_EXPORT(SV_fwra, Array_SV_fwra),
	    HLL_EXPORT(SV_faeq, Array_SV_faeq),
	    HLL_EXPORT(SV_fane, Array_SV_fane),
	    HLL_EXPORT(SV_falo, Array_SV_falo),
	    HLL_EXPORT(SV_fahi, Array_SV_fahi),
	    HLL_EXPORT(SV_fara, Array_SV_fara),
	    HLL_EXPORT(SV_foeq, Array_SV_foeq),
	    HLL_EXPORT(SV_fone, Array_SV_fone),
	    HLL_EXPORT(SV_folo, Array_SV_folo),
	    HLL_EXPORT(SV_fohi, Array_SV_fohi),
	    HLL_EXPORT(SV_fora, Array_SV_fora),
	    HLL_EXPORT(SN_fweq, Array_SN_fweq),
	    HLL_EXPORT(SN_fwne, Array_SN_fwne),
	    HLL_EXPORT(SN_fwlo, Array_SN_fwlo),
	    HLL_EXPORT(SN_fwhi, Array_SN_fwhi),
	    HLL_EXPORT(SN_faeq, Array_SN_faeq),
	    HLL_EXPORT(SN_fane, Array_SN_fane),
	    HLL_EXPORT(SN_falo, Array_SN_falo),
	    HLL_EXPORT(SN_fahi, Array_SN_fahi),
	    HLL_EXPORT(SN_foeq, Array_SN_foeq),
	    HLL_EXPORT(SN_fone, Array_SN_fone),
	    HLL_EXPORT(SN_folo, Array_SN_folo),
	    HLL_EXPORT(SN_fohi, Array_SN_fohi),
	    HLL_EXPORT(SS_fweq, Array_SS_fweq),
	    HLL_EXPORT(SS_fwne, Array_SS_fwne),
	    HLL_EXPORT(SS_fwlo, Array_SS_fwlo),
	    HLL_EXPORT(SS_fwhi, Array_SS_fwhi),
	    HLL_EXPORT(SS_faeq, Array_SS_faeq),
	    HLL_EXPORT(SS_fane, Array_SS_fane),
	    HLL_EXPORT(SS_falo, Array_SS_falo),
	    HLL_EXPORT(SS_fahi, Array_SS_fahi),
	    HLL_EXPORT(SS_foeq, Array_SS_foeq),
	    HLL_EXPORT(SS_fone, Array_SS_fone),
	    HLL_EXPORT(SS_folo, Array_SS_folo),
	    HLL_EXPORT(SS_fohi, Array_SS_fohi),
	    HLL_EXPORT(NV_sceq, Array_NV_sceq),
	    HLL_EXPORT(Duplicate, Array_Duplicate),
	    HLL_EXPORT(All, Array_All),
	    HLL_EXPORT(AscSort, Array_AscSort),
	    HLL_EXPORT(DescSort, Array_DescSort),
	    HLL_EXPORT(FindLast, Array_FindLast),
	    HLL_EXPORT(Min, Array_Min),
	    HLL_EXPORT(Remain, Array_Remain),
	    HLL_EXPORT(UniqueSorted, Array_UniqueSorted),
	    HLL_EXPORT(UpperBound, Array_UpperBoundValue),
	    HLL_EXPORT(Equals, Array_Equals),
	    HLL_EXPORT(NV_scne, Array_NV_scne),
	    HLL_EXPORT(NV_sclo, Array_NV_sclo),
	    HLL_EXPORT(NV_schi, Array_NV_schi),
	    HLL_EXPORT(NV_scra, Array_NV_scra),
	    HLL_EXPORT(SV_sceq, Array_SV_sceq),
	    HLL_EXPORT(SV_scne, Array_SV_scne),
	    HLL_EXPORT(SV_sclo, Array_SV_sclo),
	    HLL_EXPORT(SV_schi, Array_SV_schi),
	    HLL_EXPORT(SV_scra, Array_SV_scra),
	    HLL_EXPORT(NN_sclowest, Array_NN_sclowest),
	    HLL_EXPORT(NN_schighest, Array_NN_schighest),
	    HLL_EXPORT(NS_sclowest, Array_NS_sclowest),
	    HLL_EXPORT(NS_schighest, Array_NS_schighest),
	    HLL_EXPORT(SN_sclowest, Array_SN_sclowest),
	    HLL_EXPORT(SN_schighest, Array_SN_schighest),
	    HLL_EXPORT(SS_sclowest, Array_SS_sclowest),
	    HLL_EXPORT(SS_schighest, Array_SS_schighest),
	    HLL_EXPORT(VN_add, Array_VN_add),
	    HLL_EXPORT(VN_and, Array_VN_and),
	    HLL_EXPORT(VN_or, Array_VN_or),
	    HLL_EXPORT(VS_add, Array_VS_add),
	    HLL_EXPORT(VS_and, Array_VS_and),
	    HLL_EXPORT(VS_or, Array_VS_or)
	    );
