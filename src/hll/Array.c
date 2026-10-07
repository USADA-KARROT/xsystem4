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
#include <stdint.h>
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
#include "serialize_struct.h"

// v14: hll_arg3 from CALLHLL encodes element type info for Array operations.
// Set by ffi.c before each HLL call. High bits indicate reference-counted elements.
int hll_current_arg3 = -1;
// Set by ffi.c for 2-slot HLL_PARAM: stores the second slot value.
int hll_param_slot2 = 0;
// Third slot of a 3-slot HLL_PARAM (the flag of an option of an interface).
int hll_param_slot3 = 0;

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

// Slots per element of the current Array HLL call (hll_generic_slots, ffi.c):
// 2 for an interface (object, vtable offset) or an option of a one-slot
// payload (value, flag), 3 for an option of an interface.
static inline int array_elem_slots(void) {
	return hll_generic_slots(hll_current_arg3);
}

// Check if current Array HLL call operates on multi-slot elements (iface,
// option, etc.). The name predates the 3-slot option.
static inline bool array_elem_is_2slot(void) {
	return array_elem_slots() > 1;
}

// Option elements (bit 17 of the type operand) keep their value slots first
// and the flag last. Flag 0 is "has a value"; none is -1 in every value slot
// and flag 1 (native 0x656a44).
static inline bool array_elem_is_option(void) {
	return hll_generic_is_option(hll_current_arg3);
}

// The slots after the first of the element a call stores (PushBack, Insert).
// An option that is none stores no value: native 0x646d70 (at 0x646e7b)
// writes only the flag and leaves the value slots at their default, -1.
static void array_elem_extra(int *value, int extra[2])
{
	extra[0] = hll_param_slot2;
	extra[1] = hll_param_slot3;
	if (!array_elem_is_option())
		return;
	int slots = array_elem_slots();
	if (extra[slots - 2] != 0) {
		*value = -1;
		if (slots > 2)
			extra[0] = -1;
	}
}

/* Option elements: native Alloc (0x647470 -> 0x67f4a0) clears the array and
 * default-initializes every element (0x67fe20 -> 0x656970); Realloc
 * (0x67f4d0) keeps the leading elements, default-initializes the new tail
 * (0x680170) and releases the removed one (0x680270). The default is none.
 * The new page is in place before the old elements go, because releasing
 * an element can run a destructor.
 *
 * Not as the original: a removed element releases its first slot whatever
 * its flag is, front to back (natively a none element releases nothing,
 * 0x656c10 at 0x656c36, and the array is emptied from the back, 0x67ec50
 * and 0x67f554; the two differ only for a none whose value slot is not -1,
 * which nothing here stores). And a negative count returns at once in
 * Array_Alloc and Array_Realloc, where the original clears the array
 * (0x67f4a0 clears before it looks at the count, 0x67f4d0 for n <= 0).
 *
 * Only the first slot of an element is counted: on the HLL side (PushBack,
 * Insert, Erase, PopBack, AddRange and here), and by the VM for a page
 * that records its layout (page->array.elem_slots, set by X_A_INIT from
 * the declaration and by the calls here that give a generic page its
 * stride). X_OP_SET, the A_REF copy and the page's teardown go through
 * variable_type, which calls the other slots of an element plain values,
 * so a vtable offset above 1 is never taken for a heap slot. A generic
 * page without that record (a declaration X_A_INIT could not resolve, a
 * page from an older resume image) keeps the older rule, every slot above
 * 1 a reference; such a page turns to the new rule when one of the calls
 * here gives it its stride, which can leave one reference too many on
 * heap[offset] for an offset X_OP_SET stored before, never one too few.
 *
 * These do not follow that layout and still treat a multi-slot page slot by
 * slot: Array_Alloc for a non-option element (shrinking releases every slot
 * above 0, and it reads the page's struct_type, a stride there, as a struct
 * index), Array_Realloc for a non-option element (shrinking releases
 * nothing), Array_ShallowCopy (no reference for a generic page, though the
 * copy's teardown releases the first slots), Array_Duplicate and
 * Array_Remain (a reference for every slot above 0, where the copy's
 * teardown gives back only the first ones). This AIN calls none of them
 * with a multi-slot operand, except Realloc 0x10003 once, in the editor
 * (elkeditor::detail::CKeyDataList). */
static void array_option_resize(struct page **array, int numof, bool keep)
{
	if (*array && (*array)->type != ARRAY_PAGE)
		return;
	int stride = array_elem_slots();
	struct page *old = *array;
	bool generic = old && (old->a_type == AIN_ARRAY || old->a_type == AIN_REF_ARRAY);
	bool packed = generic && old->array.struct_type == stride;
	int old_n = packed ? old->nr_vars / stride : 0;
	int kept = 0;
	if (keep)
		kept = old_n < numof ? old_n : numof;
	struct page *new_a = alloc_page(ARRAY_PAGE, generic ? old->a_type : AIN_ARRAY, numof * stride);
	new_a->array.rank = 1;
	new_a->array.struct_type = stride;
	new_a->array.elem_slots = stride;
	for (int i = 0; i < kept * stride; i++)
		new_a->values[i] = old->values[i];
	for (int i = kept * stride; i < numof * stride; i++)
		new_a->values[i].i = (i % stride == stride - 1) ? 1 : -1;
	int nr_gone = packed ? old_n - kept : 0;
	int *gone = nr_gone > 0 ? xmalloc(nr_gone * sizeof(int)) : NULL;
	for (int i = 0; i < nr_gone; i++)
		gone[i] = old->values[(kept + i) * stride].i;
	if (hll_self_slot > 0 && (size_t)hll_self_slot < heap_size
	    && heap[hll_self_slot].type == VM_PAGE && heap[hll_self_slot].page == *array)
		heap[hll_self_slot].page = new_a;
	*array = new_a;
	if (old) {
		// a page of another layout is torn down as what it is
		if (!packed)
			delete_page_vars(old);
		free_page(old);
	}
	for (int i = 0; i < nr_gone; i++) {
		if (gone[i] > 0)
			heap_unref(gone[i]);
	}
	free(gone);
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
	if (array_elem_is_option()) {
		array_option_resize(array, numof, false);
		return;
	}
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
	int slots = array_elem_slots();
	bool is_2slot = slots > 1;
	int new_size = old_size + slots;
	int extra[2];
	array_elem_extra(&value, extra);

	// An empty generic array (X_A_INIT 0 of e.g. array<wrap<iwrap<T>>>) gets
	// its element stride from the first multi-slot element, so Numof/At and
	// X_A_SIZE count elements, not slots.
	if (a && is_2slot && old_size == 0
	    && (a->a_type == AIN_ARRAY || a->a_type == AIN_REF_ARRAY)) {
		a->array.struct_type = slots;
		a->array.elem_slots = slots;
	}

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
			for (int k = 1; k < slots; k++)
				a->values[old_size + k].i = extra[k - 1];
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
		for (int k = 1; k < slots; k++)
			a->values[old_size + k].i = extra[k - 1];
		if (array_elem_is_ref() && value > 0)
			heap_ref(value);
		*array = a;
	} else {
		// For multi-slot interface/option elements, set AIN_ARRAY type and
		// struct_type=stride so X_A_SIZE correctly divides nr_vars by stride
		// to get logical element count.
		int a_type = is_2slot ? AIN_ARRAY : AIN_ARRAY_INT;
		struct page *new_a = alloc_page(ARRAY_PAGE, a_type, new_size);
		// alloc_page keeps a cached page's rank: a rank of 0 is written to
		// a resume image as no array at all (rank_minus_1 -1).
		new_a->array.rank = 1;
		if (is_2slot) {
			new_a->array.struct_type = slots;
			new_a->array.elem_slots = slots;
		}
		new_a->values[0].i = value;
		for (int k = 1; k < slots; k++)
			new_a->values[k].i = extra[k - 1];
		if (array_elem_is_ref() && value > 0)
			heap_ref(value);
		*array = new_a;
	}
}

static int array_erase_stride(const struct page *a);
static int Array_At(struct page **self, int index);
static int array_callback_kind(struct page **array, int func, int stride,
			       enum ain_data_type ret, const char **why);
static bool array_query_predicate(struct page **array, int index, int stride, int func);
static void array_drop_flagged(struct page **array, const bool *drop, int stride);

// EraseAll: erase all elements matching a predicate
static bool Array_EraseAll(struct page **array, int func)
{
	struct page *src = (array && *array) ? *array : NULL;
	if (!src || src->nr_vars == 0 || func < 0 || func >= ain->nr_functions)
		return false;

	// Two-slot elements (0x10003, an interface): the predicate takes a whole
	// element (object, vtable offset) and a match removes both slots. The
	// slot loop below would call it on half elements.
	int stride = array_elem_is_2slot() && array_elem_is_ref() ? array_erase_stride(src) : 1;
	const char *why = NULL;
	if (stride > 1 && array_callback_kind(array, func, stride, AIN_BOOL, &why) == 0) {
		int n = src->nr_vars / stride;
		bool *drop = xcalloc(n, sizeof(bool));
		bool any = false;
		for (int i = 0; i < n; i++) {
			if (!*array || (*array)->type != ARRAY_PAGE || i >= (*array)->nr_vars / stride)
				break;
			drop[i] = array_query_predicate(array, i, stride, func);
			any = any || drop[i];
		}
		if (any)
			array_drop_flagged(array, drop, stride);
		free(drop);
		return any;
	}
	if (stride > 1) {
		static int warned;
		if (warned++ < 8)
			WARNING("Array.EraseAll: %s (fno %d); nothing erased", why, func);
		return false;
	}

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
	int stride = array_elem_slots();
	bool is_2slot = stride > 1;

	// Collect matching elements by calling the predicate function for each.
	// For multi-slot elements (interface/option), matches stores the slots
	// of each element in order ([page_idx, vtoff], [value, flag]).
	union vm_value *matches = malloc(src->nr_vars * sizeof(union vm_value));
	int match_count = 0;  // number of matched elements (not slots)

	for (int i = 0; i + stride <= src->nr_vars; i += stride) {
		int saved_sp = stack_ptr;

		if (is_2slot) {
			// multi-slot element: push it whole ([page_idx, vtoff], or an
			// option's [value, flag], native 0x6455d0)
			for (int k = 0; k < stride; k++)
				stack_push(src->values[i + k]);
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
			for (int k = 0; k < stride; k++)
				matches[match_count * stride + k] = src->values[i + k];
			match_count++;
		}
	}

	// Build result array: preserve a_type and struct_type for X_A_SIZE correctness.
	// For multi-slot elements, force AIN_ARRAY type and struct_type=stride so X_A_SIZE
	// returns the logical element count (nr_vars / stride) not the raw slot count.
	int result_slots = match_count * stride;
	int result_a_type = is_2slot ? AIN_ARRAY : src->a_type;
	struct page *result_page = alloc_page(ARRAY_PAGE, result_a_type, result_slots);
	result_page->array.rank = 1;
	result_page->array.struct_type = is_2slot ? stride : src->array.struct_type;
	result_page->array.elem_slots = is_2slot ? stride : 0;
	for (int i = 0; i < match_count; i++) {
		for (int k = 0; k < stride; k++)
			result_page->values[i * stride + k] = matches[i * stride + k];
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
	if (!src || src->nr_vars == 0 || func < 0 || func >= ain->nr_functions)
		return Array_At(array, -1);

	// Two-slot elements (0x10003, an interface): walk elements, pass the
	// whole element to the predicate and return it as At does (two slots).
	// e.g. Motion::PartsParamCollection@0 finds its TimeParam this way.
	int stride = array_elem_is_2slot() && array_elem_is_ref() ? array_erase_stride(src) : 1;
	const char *why = NULL;
	if (stride > 1 && array_callback_kind(array, func, stride, AIN_BOOL, &why) == 0) {
		int n = src->nr_vars / stride;
		for (int i = 0; i < n; i++) {
			if (!*array || (*array)->type != ARRAY_PAGE || i >= (*array)->nr_vars / stride)
				break;
			if (array_query_predicate(array, i, stride, func))
				return Array_At(array, i);
		}
		return Array_At(array, -1);
	}
	if (stride > 1) {
		static int warned;
		if (warned++ < 8)
			WARNING("Array.First: %s (fno %d); returning none", why, func);
		return Array_At(array, -1);
	}
	// An option element never takes the slot loop below: it would feed the
	// predicate single slots and return one slot where the bytecode takes a
	// position (native First is At of the index found, 0x649e50). Stride 1
	// means the page does not have the option layout, e.g. an array literal,
	// which is still allocated one slot per element.
	if (array_elem_is_option()) {
		static int warned;
		if (warned++ < 8)
			WARNING("Array.First: option array page has no element stride (arg3 %#x, %d slots); returning none",
				hll_current_arg3, src->nr_vars);
		return Array_At(array, -1);
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

/* v14 Free/Clear empty the values without discarding the element declaration.
 * Native CArrayPage::clear (0x67ec50) keeps its descriptor and stride; the next
 * EmplaceBack still constructs that type. Our page stores that declaration,
 * so a NULL page would make a struct list restart as an integer list. */
static void array_clear_typed(struct page **array)
{
	struct page *old = *array;
	struct page *empty = alloc_page(ARRAY_PAGE, old->a_type, 0);
	empty->array = old->array;
	delete_page_vars(old);
	free_page(old);
	*array = empty;
}

static void Array_Free(struct page **array)
{
	if (array && *array && (*array)->type == ARRAY_PAGE) {
		if (ain->version >= 14) {
			array_clear_typed(array);
			return;
		}
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
// For two-slot elements (0x10003, an interface): push both slots, the
// object (retained) and its vtable offset; a missing element is (-1, 0).
// The AIN takes such a result as two slots (X_MOV 4 2; X_ASSIGN 2 into an
// interface local, e.g. SceneParentStack@Get, AnimateText@AdjustPos).
// ffi.c does NOT push the C return value for AIN_REF_HLL_PARAM.
static int Array_At(struct page **self, int index)
{
	struct page *array = (self && *self) ? *self : NULL;
	// An option element is returned as its position, (array page, physical
	// slot index): the bytecode reads the value and the flag through it with
	// X_REF (native 0x6499e0 at 0x649b0d). Without an element the position
	// is (-1, 0) (0x649c25).
	// The position is not counted here. Natively it holds a reference to
	// its page (0x658390 -> 0x679f10), which the function page that keeps
	// it gives back when it releases the type 0x57 variable (0x6570f0 ->
	// 0x656cf0 -> 0x656c10 at 0x656c58). This VM has neither half for type
	// 87: variable_fini does not release it, and function_call,
	// vm_call_nopop and delegate calls do not retain such an argument.
	// Retaining here alone leaks the array on every call (MapView@GetNode,
	// the one caller, keeps the position in a type 87 local and has no
	// .LOCALDELETE); releasing type 87 without the argument retains would
	// release too much. So the position is borrowed and must not outlive
	// the array. GetNode reads the element through it at once, while its
	// object owns the array.
	if (array_elem_is_option()) {
		int stride = array ? array_erase_stride(array) : 1;
		if (!array || index < 0 || index >= array->nr_vars / stride || hll_self_slot <= 0) {
			stack_push(-1);
			stack_push(0);
			return 0;
		}
		stack_push(hll_self_slot);
		stack_push(index * stride);
		return 0;
	}
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
			if (array_elem_is_2slot())
				stack_push(0);
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
		if (array_elem_is_2slot())
			stack_push(index + 1 < array->nr_vars ? array->values[index + 1].i : 0);
	}
	return 0;
}

// Array.Last: returns AIN_REF_HLL_PARAM (same convention as Array.At): the
// last element, which is the last two slots for two-slot elements.
static int Array_Last(struct page **self)
{
	struct page *array = (self && *self) ? *self : NULL;
	return Array_At(self, array ? array->nr_vars / array_erase_stride(array) - 1 : -1);
}

// First without a predicate: same contract as At(0). The AIN declares
// First(ref array self) and First(ref array self, hll_func func) under one
// name; the single-argument form must not reach Array_First's func parameter.
static int Array_First_NoPred(struct page **array)
{
	return Array_At(array, 0);
}

// PopBack (capital B) — v14 name. A two-slot element (interface, option)
// is removed whole; its first slot holds the reference.
static void Array_PopBack(struct page **array)
{
	if (!array || !*array || (*array)->nr_vars <= 0)
		return;
	struct page *a = *array;
	int slots = array_erase_stride(a);
	if (slots > a->nr_vars)
		slots = 1;
	int new_size = a->nr_vars - slots;
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
	if (ain->version >= 14 && *array && (*array)->type == ARRAY_PAGE) {
		array_clear_typed(array);
		return;
	}
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
		new_a->array.rank = 1;
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
	// (The VM counts differently for a three-slot element: array_option_resize.)
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

// Insert: a two-slot element (0x10003, an interface) is inserted whole, at
// a logical index, like PushBack/Add store it (see Array_PushBack).
static void Array_Insert(struct page **array, int index, int value)
{
	if (!array)
		return;
	int extra[2];
	array_elem_extra(&value, extra);
	// heap_ref for ref-counted elements (struct/string/delegate)
	if (array_elem_is_ref() && value > 0)
		heap_ref(value);
	struct page *a = *array;
	int stride = array_elem_slots();
	int old_size = a ? a->nr_vars : 0;
	if (index < 0) index = 0;
	if (index > old_size / stride) index = old_size / stride;
	int at = index * stride;
	int a_type = a ? a->a_type : (stride > 1 ? AIN_ARRAY : AIN_ARRAY_INT);
	struct page *new_a = alloc_page(ARRAY_PAGE, a_type, old_size + stride);
	if (a) {
		for (int i = 0; i < at; i++)
			new_a->values[i] = a->values[i];
		for (int i = at; i < old_size; i++)
			new_a->values[i + stride] = a->values[i];
		new_a->array = a->array;
		free_page(a);
	} else {
		new_a->array.rank = 1;
	}
	new_a->values[at].i = value;
	if (stride > 1) {
		for (int k = 1; k < stride; k++)
			new_a->values[at + k].i = extra[k - 1];
		if (old_size == 0 && (a_type == AIN_ARRAY || a_type == AIN_REF_ARRAY)) {
			new_a->array.struct_type = stride;
			new_a->array.elem_slots = stride;
		}
	}
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
	(*dst)->array.rank = 1;
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
		nd->array.rank = 1;
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
	nd->array.rank = 1;
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
	if (array_elem_is_option()) {
		array_option_resize(array, new_size, true);
		return;
	}
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
	/* Native 0x658d40 wraps each string/struct element and 0x67f2e1
	 * retains its heap owner. These concrete single-slot pages already
	 * represent the shared object directly; the result needs its own ref.
	 * Leave unverified primitive, nested and multi-slot shapes unchanged. */
	bool retain = ain->version >= 14 && src->array.rank == 1
		&& (src->a_type == AIN_ARRAY_STRUCT || src->a_type == AIN_REF_ARRAY_STRUCT
		    || src->a_type == AIN_ARRAY_STRING || src->a_type == AIN_REF_ARRAY_STRING);
	for (int i = 0; i < src->nr_vars; i++) {
		copy->values[i] = src->values[i];
		if (retain && copy->values[i].i > 0)
			heap_ref(copy->values[i].i);
	}
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
	// Nothing in the game emplaces an option element, and the struct path
	// below would construct an object from the element stride.
	if (array_elem_is_option()) {
		static int warned;
		if (warned++ < 8)
			WARNING("Array.EmplaceBack: option elements are not supported (arg3 %#x); nothing appended", hll_current_arg3);
		return -1;
	}
	struct page *a = *array;
	int old_size = a ? a->nr_vars : 0;
	struct page *new_a = alloc_page(ARRAY_PAGE, a ? a->a_type : AIN_ARRAY_INT, old_size + 1);
	if (a) {
		for (int i = 0; i < old_size; i++)
			new_a->values[i] = a->values[i];
		new_a->array = a->array;
		free_page(a);
	} else {
		new_a->array.rank = 1;
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
	// Multi-slot elements (0x10003, options): only the first slot of an
	// element is an object; the others are its vtable offset or flag and gain
	// no reference (for what the VM counts in a three-slot element see
	// array_option_resize). An empty generic destination takes the stride
	// (see Array_PushBack).
	int stride = array_elem_slots();
	for (int i = 0; i < src_count; i++) {
		new_a->values[old_size + i] = src_vals[i];
		if (array_elem_is_ref() && i % stride == 0 && src_vals[i].i > 0)
			heap_ref(src_vals[i].i);
	}
	if (stride > 1 && old_size == 0
	    && (new_a->a_type == AIN_ARRAY || new_a->a_type == AIN_REF_ARRAY)) {
		new_a->array.struct_type = stride;
		new_a->array.elem_slots = stride;
	}
	free(src_vals);
	*dst = new_a;
}

// SYSTEMONLY_GetStructPageList: fallback for declarations other than the v14
// shape below (no-op, as before).
static void Array_SYSTEMONLY_GetStructPageList(struct page **array)
{
}

/*
 * v14 SYSTEMONLY_GetStructPageList(ref array<hll_param>) -> array<int>
 * (native: Array case 83 @0x644ecd -> 0x65a620 -> 0x64a2d0). Returns a new
 * array (ref 1) holding the struct handle of every element; the handles are
 * not referenced again (0x418940 copies plain ints). One invalid element
 * empties the whole result (0x64a3f1). The element count follows the page
 * layout; xsystem4 needs arg3 to tell the X_A_INIT slot count apart from a
 * struct index, the same rule Numof/At/Erase use (array_erase_stride).
 */
intptr_t Array_SYSTEMONLY_GetStructPageList_v14(struct page **self)
{
	struct page *src = self && *self && (*self)->type == ARRAY_PAGE ? *self : NULL;
	int stride = src ? array_erase_stride(src) : 1;
	int n = src ? src->nr_vars / stride : 0;
	int count = n;
	int *h = n > 0 ? xcalloc(n, sizeof(int)) : NULL;
	for (int i = 0; i < n; i++) {
		h[i] = ss_struct_slot(src->values[i * stride].i);
		if (h[i] < 0) {
			static int warned;
			if (warned++ < 8)
				WARNING("Array.SYSTEMONLY_GetStructPageList: element %d (slot %d) is not a struct; "
					"returning an empty list", i, src->values[i * stride].i);
			count = 0;
			break;
		}
	}
	struct page *out = alloc_page(ARRAY_PAGE, AIN_ARRAY_INT, count);
	out->array.struct_type = -1; // cached pages keep old metadata (_alloc_page)
	out->array.rank = 1;         // rank 1 + AIN_ARRAY_INT: elements are not unreferenced
	for (int i = 0; i < count; i++)
		out->values[i].i = h[i];
	free(h);
	ss_trace("gspl n=%d stride=%d out=%d", n, stride, count);
	return heap_alloc_page(out);
}

// Add: the native jump table sends Add and PushBack to the same handler
// (0x644455), so a two-slot element (interface, option) keeps both slots.
static void Array_Add(struct page **array, int value)
{
	Array_PushBack(array, value);
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
		// A two-slot option element is passed whole, by value: (value,
		// flag) into the option argument and its void companion (native
		// 0x6455d0 at 0x645829), e.g. MapView@GetNode's First.
		if (stride == 2 && type == AIN_OPTION && array_elem_is_option())
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
	if (!array || !*array || (*array)->type != ARRAY_PAGE || (*array)->nr_vars <= 0)
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
	if (!src || src->type != ARRAY_PAGE || src->nr_vars <= 0)
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
	if (!array || !*array || (*array)->type != ARRAY_PAGE || (*array)->nr_vars <= 0)
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

/* FindLast mirrors Find but scans backwards, and its end is inclusive.
 * Native (Array dispatcher 43/45/47/49 -> reverse scan 0x646c10):
 * begin = max(begin, 0); end = end >= length ? length - 1 : end;
 * for i = end down to begin, return the first match; otherwise -1.
 * The full-range forms pass begin 0 and end length. An empty array
 * returns -1 before the callback is looked at. */
static int Array_FindLastValueRange(struct page **array, int begin, int end, int value)
{
	const struct page *src = array ? *array : NULL;
	if (!src || src->type != ARRAY_PAGE || src->nr_vars <= 0)
		return -1;
	enum ain_data_type type = array_query_value_type(src);
	int count = src->nr_vars;
	if (begin < 0) begin = 0;
	if (end >= count) end = count - 1;
	union vm_value needle = {.i = value};
	for (int i = end; i >= begin; i--) {
		if (array_query_value_equal(src->values[i], needle, type))
			return i;
	}
	return -1;
}

static int Array_FindLastValue(struct page **array, int value)
{
	return Array_FindLastValueRange(array, 0, INT_MAX, value);
}

static int Array_FindLastIfRange(struct page **array, int begin, int end, int func)
{
	if (!array || !*array || (*array)->type != ARRAY_PAGE)
		return -1;
	int stride = array_erase_stride(*array);
	int count = (*array)->nr_vars / stride;
	if (count <= 0)
		return -1;
	array_query_callback_shape(array, func, stride);
	if (begin < 0) begin = 0;
	if (end >= count) end = count - 1;
	for (int i = end; i >= begin && *array && (*array)->type == ARRAY_PAGE
	     && i < (*array)->nr_vars / stride; i--) {
		if (array_query_predicate(array, i, stride, func))
			return i;
	}
	return -1;
}

static int Array_FindLastIf(struct page **array, int func)
{
	return Array_FindLastIfRange(array, 0, INT_MAX, func);
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

/* Min / Max / Last(pred).
 *
 * Native (Array dispatcher 71-82; each #0/#1 and #2/#3 pair shares a helper):
 *   Last(pred)  73/74 -> 0x648c80 -> 0x646c10: scan from n-1 down, first hit.
 *   Min         75-78 -> loop 0x646af0: best = 0; for j = 1..n-1,
 *               if less(e[j], e[best]) best = j   (first minimum on ties)
 *   Max         79-82 -> loop 0x646b50: best = 0; for j = 1..n-1,
 *               if !less(e[j], e[best]) best = j  (last maximum on ties)
 * The lambda is called as less(lhs = e[j], rhs = e[best]). Every CN Min/Max
 * site passes a two-argument bool lambda "key(lhs) < key(rhs)"; Max gets
 * the same less, not a greater. The default less (0x645dd0) compares
 * int/bool/enum signed, float with <, strings bytewise; any other type uses
 * an always-false less, so Min() yields e[0] and Max() e[n-1]. With one
 * element the lambda is never called. Results go through Array_At, which
 * owns the ref hll_param return contract; a miss is a null reference.
 */
enum array_order_kind {
	ARRAY_ORDER_NONE,
	ARRAY_ORDER_INT,
	ARRAY_ORDER_FLOAT,
	ARRAY_ORDER_STRING,
};

// hll_arg3 is 1 for both int and float elements, so the page type decides.
static enum array_order_kind array_order_value_kind(const struct page *a)
{
	if (a->array.rank > 1 || array_elem_is_2slot())
		return ARRAY_ORDER_NONE;
	switch (a->a_type) {
	case AIN_FLOAT: case AIN_ARRAY_FLOAT:
		return ARRAY_ORDER_FLOAT;
	case AIN_STRING: case AIN_ARRAY_STRING:
		return ARRAY_ORDER_STRING;
	case AIN_INT: case AIN_ARRAY_INT: case AIN_BOOL: case AIN_ARRAY_BOOL: case AIN_ENUM:
		if (!array_elem_is_ref())
			return ARRAY_ORDER_INT;
		// PushBack on a NULL page records AIN_ARRAY_INT even for strings.
		if (a->nr_vars > 0 && a->values[0].i > 0 && (size_t)a->values[0].i < heap_size
		    && heap[a->values[0].i].type == VM_STRING)
			return ARRAY_ORDER_STRING;
		return ARRAY_ORDER_NONE;
	default:
		return ARRAY_ORDER_NONE;
	}
}

static bool array_order_value_less(const struct page *a, int lhs, int rhs,
				   enum array_order_kind kind)
{
	union vm_value x = a->values[lhs], y = a->values[rhs];
	switch (kind) {
	case ARRAY_ORDER_INT:
		return x.i < y.i;
	case ARRAY_ORDER_FLOAT:
		return x.f < y.f;
	case ARRAY_ORDER_STRING: {
		const struct string *sx = array_bound_string(x.i), *sy = array_bound_string(y.i);
		int n = sx->size < sy->size ? sx->size : sy->size;
		int r = n ? memcmp(sx->text, sy->text, n) : 0;
		return r ? r < 0 : sx->size < sy->size;
	}
	default:
		return false;
	}
}

/* Comparator shape: 0 = two elements by value, 1 = two by-reference
 * primitives ([owner, index] each), -1 = unsupported. */
static int array_order_callback_kind(struct page **array, int func, int stride)
{
	if (func < 0 || func >= ain->nr_functions || stride != 1)
		return -1;
	struct ain_function *cb = &ain->functions[func];
	if (cb->address >= ain->code_size || cb->return_type.data != AIN_BOOL
	    || !cb->vars || cb->nr_vars < cb->nr_args)
		return -1;
	if (cb->nr_args == 2) {
		for (int k = 0; k < 2; k++) {
			switch (cb->vars[k].type.data) {
			case AIN_INT: case AIN_FLOAT: case AIN_BOOL: case AIN_LONG_INT:
			case AIN_ENUM: case AIN_ENUM2: case AIN_STRING: case AIN_REF_STRING:
			case AIN_STRUCT: case AIN_REF_STRUCT: case AIN_WRAP:
				break;
			default:
				return -1;
			}
		}
		return 0;
	}
	enum ain_data_type t = cb->nr_args == 4 ? cb->vars[0].type.data : AIN_VOID;
	if ((t == AIN_REF_INT || t == AIN_REF_FLOAT || t == AIN_REF_BOOL || t == AIN_REF_LONG_INT)
	    && cb->vars[1].type.data == AIN_VOID && cb->vars[2].type.data == t
	    && cb->vars[3].type.data == AIN_VOID) {
		if (hll_self_slot < 0 || (size_t)hll_self_slot >= heap_size
		    || HEAP_REF(hll_self_slot) <= 0 || heap[hll_self_slot].type != VM_PAGE
		    || heap[hll_self_slot].page != *array)
			return -1;
		return 1;
	}
	return -1;
}

// less(e[lhs], e[rhs]) through the game's lambda; same stack and owner
// refresh discipline as array_callback_call.
static bool array_order_less(struct page **array, int lhs, int rhs, int func, bool reference)
{
	struct ain_function *cb = &ain->functions[func];
	struct page *before = *array;
	int size = before->nr_vars;
	int owner = hll_self_slot;
	bool tracked = owner >= 0 && (size_t)owner < heap_size && HEAP_REF(owner) > 0
		&& heap[owner].type == VM_PAGE && heap[owner].page == before;
	int saved_sp = stack_ptr;
	if (reference) {
		stack_push(owner);
		stack_push(lhs);
		stack_push(owner);
		stack_push(rhs);
	} else {
		stack_push(before->values[lhs]);
		stack_push(before->values[rhs]);
	}
	vm_call_nopop(func, cb->nr_args);
	bool less = stack_pop().i != 0;
	stack_ptr = saved_sp;
	if (tracked) {
		if ((size_t)owner >= heap_size || HEAP_REF(owner) <= 0 || heap[owner].type != VM_PAGE)
			VM_ERROR("Array.Min/Max: comparator released its array owner");
		*array = heap[owner].page;
	}
	if (*array != before || !*array || (*array)->type != ARRAY_PAGE || (*array)->nr_vars != size)
		VM_ERROR("Array.Min/Max: comparator changed array storage");
	return less;
}

// Index of the extreme element, or -1 for a missing or empty array.
static int array_order_extreme(struct page **array, bool has_func, int func, bool want_max)
{
	if (!array || !*array || (*array)->type != ARRAY_PAGE)
		return -1;
	int stride = array_erase_stride(*array);
	int n = (*array)->nr_vars / stride;
	if (n <= 0)
		return -1;
	if (n == 1)
		return 0;
	int reference = 0;
	enum array_order_kind kind = ARRAY_ORDER_NONE;
	if (has_func) {
		reference = array_order_callback_kind(array, func, stride);
		if (reference < 0) {
			static int warned;
			int fallback = want_max ? n - 1 : 0;
			if (warned++ < 8)
				WARNING("Array.%s: unsupported comparator fno %d (stride %d); returning element %d",
					want_max ? "Max" : "Min", func, stride, fallback);
			return fallback;
		}
	} else {
		kind = array_order_value_kind(*array);
		if (kind == ARRAY_ORDER_NONE) {
			static int warned;
			if (warned++ < 8)
				WARNING("Array.%s: element type %d has no default order",
					want_max ? "Max" : "Min", (*array)->a_type);
		}
	}
	int best = 0;
	for (int j = 1; j < n; j++) {
		bool less = has_func
			? array_order_less(array, j, best, func, reference == 1)
			: array_order_value_less(*array, j * stride, best * stride, kind);
		if (want_max ? !less : less)
			best = j;
	}
	return best;
}

static int Array_MinNoPred(struct page **array)
{
	return Array_At(array, array_order_extreme(array, false, 0, false));
}

static int Array_MaxNoPred(struct page **array)
{
	return Array_At(array, array_order_extreme(array, false, 0, true));
}

static int Array_MinPredicate(struct page **array, int func)
{
	return Array_At(array, array_order_extreme(array, true, func, false));
}

static int Array_MaxPredicate(struct page **array, int func)
{
	return Array_At(array, array_order_extreme(array, true, func, true));
}

// Last element whose predicate is true. The old binding ignored the
// predicate and returned the last element; keep that for shapes the
// predicate ABI does not cover instead of stopping the VM.
static int Array_LastPredicate(struct page **array, int func)
{
	if (!array || !*array || (*array)->type != ARRAY_PAGE)
		return Array_At(array, -1);
	int stride = array_erase_stride(*array);
	int n = (*array)->nr_vars / stride;
	if (n <= 0)
		return Array_At(array, -1);
	const char *why = NULL;
	if (array_callback_kind(array, func, stride, AIN_BOOL, &why) < 0) {
		static int warned;
		if (warned++ < 8)
			WARNING("Array.Last: %s (fno %d); returning the last element", why, func);
		return Array_Last(array);
	}
	for (int i = n - 1; i >= 0; i--) {
		if (!*array || (*array)->type != ARRAY_PAGE || i >= (*array)->nr_vars / stride)
			break;
		if (array_query_predicate(array, i, stride, func))
			return Array_At(array, i);
	}
	return Array_At(array, -1);
}

/* Any / Unique / UniqueSorted / Equals.
 *
 * Native (Array dispatcher, jump table 0x644f18):
 *   26 Any()              0x6446fc: length != 0.
 *   27 Any(pred)          0x64473a, the same entry as 57 IsExist(pred):
 *                         find-first predicate match >= 0.
 *   40 Equals(src)        0x6488c0: same length, then a typed compare of
 *                         self[i] and src[i].
 *   41 Equals(src, pred)  0x6489a0: same length, pred(self[i], src[i]) for all i.
 *   61 Unique()           0x6494f0: remove every later duplicate, keep the
 *                         first occurrence and the order.
 *   62 Unique(pred)       0x649670: the same loop; pred(self[i], self[j]) for
 *                         surviving i < j, true erases j.
 *   63 UniqueSorted()     adjacent removal against the last kept element.
 *   64 UniqueSorted(pred) the same with pred(kept, next).
 * The native loops erase in place. Here the callbacks run against the
 * unchanged page and the drops are applied once at the end; the call order
 * and arguments are the same (erasing j only shifts later indices), and the
 * FFI snapshot of self is never freed while a callback runs. Dropped
 * reference elements are released.
 */

/* Element equality for the non-predicate forms: floats by value, strings
 * by content (an invalid slot counts as the empty string, as the native
 * std::string would), anything else by identity. */
static bool array_elem_value_equal(const struct page *a, union vm_value x, union vm_value y)
{
	if (a->a_type == AIN_FLOAT || a->a_type == AIN_ARRAY_FLOAT)
		return x.f == y.f;
	if (x.i == y.i)
		return true;
	if (!array_elem_is_ref())
		return false;
	bool xs = x.i > 0 && (size_t)x.i < heap_size && heap[x.i].type == VM_STRING;
	bool ys = y.i > 0 && (size_t)y.i < heap_size && heap[y.i].type == VM_STRING;
	bool xo = x.i > 0 && (size_t)x.i < heap_size && HEAP_REF(x.i) > 0 && !xs;
	bool yo = y.i > 0 && (size_t)y.i < heap_size && HEAP_REF(y.i) > 0 && !ys;
	if ((!xs && !ys) || xo || yo)
		return false;
	const struct string *sx = array_bound_string(x.i), *sy = array_bound_string(y.i);
	return sx->size == sy->size && (!sx->size || !memcmp(sx->text, sy->text, sx->size));
}

static bool array_elem_equal_at(const struct page *a, int i, const struct page *b, int j, int stride)
{
	if (!array_elem_value_equal(a, a->values[i * stride], b->values[j * stride]))
		return false;
	// Extra slots of interface/option elements are compared raw.
	for (int k = 1; k < stride; k++) {
		if (a->values[i * stride + k].i != b->values[j * stride + k].i)
			return false;
	}
	return true;
}

/* (lhs, rhs) -> bool callbacks: one slot per argument (a scalar, a string
 * slot, or the struct slot). vm_call_nopop owns the argument references. */
static bool array_pair_callback_ok(int func, int stride)
{
	if (func < 0 || func >= ain->nr_functions || stride != 1)
		return false;
	struct ain_function *cb = &ain->functions[func];
	if (cb->address >= ain->code_size || cb->return_type.data != AIN_BOOL
	    || !cb->vars || cb->nr_vars < cb->nr_args || cb->nr_args != 2)
		return false;
	for (int k = 0; k < 2; k++) {
		switch (cb->vars[k].type.data) {
		case AIN_INT: case AIN_FLOAT: case AIN_BOOL: case AIN_LONG_INT:
		case AIN_ENUM: case AIN_ENUM2: case AIN_STRING: case AIN_REF_STRING:
		case AIN_STRUCT: case AIN_REF_STRUCT: case AIN_WRAP:
			break;
		default:
			return false;
		}
	}
	return true;
}

static void array_pair_warn(const char *name, int func)
{
	static int count;
	if (count++ < 8)
		WARNING("Array.%s: unsupported pair predicate fno %d; comparing values instead", name, func);
}

static bool array_pair_predicate(int func, union vm_value lhs, union vm_value rhs)
{
	int saved_sp = stack_ptr;
	stack_push(lhs);
	stack_push(rhs);
	vm_call_nopop(func, 2);
	bool match = stack_pop().i != 0;
	stack_ptr = saved_sp;
	return match;
}

/* The FFI passes a local snapshot of self. A callback that replaces the
 * owner's page would be overwritten by the FFI write-back; refuse it, as
 * array_callback_call does. */
struct array_owner {
	int slot;
	bool tracked;
	struct page *before;
};

static struct array_owner array_owner_track(struct page *page)
{
	int owner = hll_self_slot;
	struct array_owner o = { owner, false, page };
	o.tracked = owner >= 0 && (size_t)owner < heap_size && HEAP_REF(owner) > 0
		&& heap[owner].type == VM_PAGE && heap[owner].page == page;
	return o;
}

static void array_owner_check(const struct array_owner *o, struct page **array)
{
	if (o->tracked) {
		if ((size_t)o->slot >= heap_size || HEAP_REF(o->slot) <= 0
		    || heap[o->slot].type != VM_PAGE)
			VM_ERROR("Array: predicate released its array owner");
		*array = heap[o->slot].page;
	}
	if (*array != o->before)
		VM_ERROR("Array: predicate changed array storage");
}

/* Drop the flagged logical elements in one pass. Only the first slot of an
 * element owns a heap reference (same contract as Array_Erase). */
static void array_drop_flagged(struct page **array, const bool *drop, int stride)
{
	struct page *a = *array;
	int count = a->nr_vars / stride;
	int keep = 0;
	for (int i = 0; i < count; i++) {
		if (!drop[i])
			keep++;
	}
	if (keep == count)
		return;
	if (array_elem_is_ref()) {
		for (int i = 0; i < count; i++) {
			if (drop[i] && a->values[i * stride].i > 0)
				heap_unref(a->values[i * stride].i);
		}
	}
	struct page *n = alloc_page(ARRAY_PAGE, a->a_type, keep * stride);
	int w = 0;
	for (int i = 0; i < count; i++) {
		if (drop[i])
			continue;
		for (int k = 0; k < stride; k++)
			n->values[w * stride + k] = a->values[i * stride + k];
		w++;
	}
	n->array = a->array;
	free_page(a);
	*array = n;
}

static void array_unique(struct page **array, bool use_pred, int func, bool adjacent)
{
	if (!array || !*array || (*array)->type != ARRAY_PAGE)
		return;
	struct page *a = *array;
	int stride = array_erase_stride(a);
	int count = a->nr_vars / stride;
	if (count <= 1)
		return;
	if (use_pred && !array_pair_callback_ok(func, stride)) {
		array_pair_warn(adjacent ? "UniqueSorted" : "Unique", func);
		use_pred = false;
	}
	struct array_owner owner = array_owner_track(a);
	bool *drop = xcalloc(count, sizeof(bool));
	if (adjacent) {
		int kept = 0;
		for (int j = 1; j < count; j++) {
			bool dup;
			if (use_pred) {
				dup = array_pair_predicate(func, a->values[kept], a->values[j]);
				array_owner_check(&owner, array);
			} else {
				dup = array_elem_equal_at(a, kept, a, j, stride);
			}
			if (dup)
				drop[j] = true;
			else
				kept = j;
		}
	} else {
		for (int i = 0; i < count; i++) {
			if (drop[i])
				continue;
			for (int j = i + 1; j < count; j++) {
				if (drop[j])
					continue;
				bool dup;
				if (use_pred) {
					dup = array_pair_predicate(func, a->values[i], a->values[j]);
					array_owner_check(&owner, array);
				} else {
					dup = array_elem_equal_at(a, i, a, j, stride);
				}
				if (dup)
					drop[j] = true;
			}
		}
	}
	array_drop_flagged(array, drop, stride);
	free(drop);
}

static void Array_Unique(struct page **array)
{
	array_unique(array, false, -1, false);
}

static void Array_UniqueIf(struct page **array, int func)
{
	array_unique(array, true, func, false);
}

static void Array_UniqueSorted(struct page **array)
{
	array_unique(array, false, -1, true);
}

static void Array_UniqueSortedIf(struct page **array, int func)
{
	array_unique(array, true, func, true);
}

/* wrap<array<T>> arrives as the array's heap slot (sint32 in the CIF), not
 * as a struct page **. A live slot with a NULL page is an empty array. */
static bool array_wrap_slot_valid(int slot)
{
	return slot > 0 && (size_t)slot < heap_size && HEAP_REF(slot) > 0
		&& heap[slot].type == VM_PAGE
		&& (!heap[slot].page || heap[slot].page->type == ARRAY_PAGE);
}

static bool array_equals(struct page **self, int src_slot, bool use_pred, int func)
{
	if (!self || !array_wrap_slot_valid(src_slot))
		return false;
	struct page *a = *self;
	struct page *b = heap[src_slot].page;
	if (a && a->type != ARRAY_PAGE)
		return false;
	int stride = a ? array_erase_stride(a) : (b ? array_erase_stride(b) : 1);
	if (a && b && array_erase_stride(b) != stride)
		return false;
	int na = a ? a->nr_vars / stride : 0;
	int nb = b ? b->nr_vars / stride : 0;
	if (na != nb)
		return false;
	if (na == 0)
		return true;
	if (use_pred && !array_pair_callback_ok(func, stride)) {
		array_pair_warn("Equals", func);
		use_pred = false;
	}
	struct array_owner owner = array_owner_track(a);
	for (int i = 0; i < na; i++) {
		bool eq;
		if (use_pred) {
			eq = array_pair_predicate(func, a->values[i], b->values[i]);
			array_owner_check(&owner, self);
			if (!array_wrap_slot_valid(src_slot) || heap[src_slot].page != b)
				VM_ERROR("Array.Equals: predicate changed source storage");
		} else {
			eq = array_elem_equal_at(a, i, b, i, stride);
		}
		if (!eq)
			return false;
	}
	return true;
}

static bool Array_Equals(struct page **self, int src_slot)
{
	return array_equals(self, src_slot, false, -1);
}

static bool Array_EqualsIf(struct page **self, int src_slot, int func)
{
	return array_equals(self, src_slot, true, func);
}

/* Any(pred) shares IsExist(pred)'s native entry. Use the checked query
 * path; shapes it does not cover keep the old looser IsExist loop. */
static bool Array_AnyIf(struct page **array, int func)
{
	if (!array || !*array || (*array)->type != ARRAY_PAGE)
		return false;
	int stride = array_erase_stride(*array);
	if ((*array)->nr_vars / stride <= 0)
		return false;
	const char *why = NULL;
	if (array_callback_kind(array, func, stride, AIN_BOOL, &why) < 0)
		return Array_IsExist(array, func);
	return Array_FindIf(array, func) >= 0;
}

/* Fill / Copy / Realloc(n, value).
 *
 * Native (Array dispatcher, jump table 0x644f18):
 *   1  Realloc(self, n)             CArrayPage vtbl+0x50
 *   2  Realloc(self, n, value)      0x647510: resize, then fill [old, n)
 *   28 Fill(self, value)            fill(self, 0, Numof(self), value)
 *   29 Fill(self, i, len, value)    fill(self, i, len, value)
 *   30 Copy(self, src)              copy(self, 0, src, 0, Numof(src))
 *   31 Copy(self, d, src)           copy(self, d, src, 0, Numof(src))
 *   32 Copy(self, src, s, len)      copy(self, 0, src, s, len)
 *   33 Copy(self, d, src, s, len)   copy(self, d, src, s, len)
 * fill (0x648120) and copy (0x648210) clamp the range and return the
 * clamped count, which may be zero or negative. The element setter
 * (0x646d70) stores values as is, shares reference objects, and gives
 * strings and value structs their own copy.
 */

// VM slots per logical element for the current call (2 for iface/option).
static int array_call_stride(void)
{
	return array_elem_slots();
}

static int array_call_numof(const struct page *a)
{
	return (a && a->type == ARRAY_PAGE) ? a->nr_vars / array_call_stride() : 0;
}

// wrap<array> arguments arrive as the inner heap slot.
static struct page *array_wrap_page(int slot)
{
	if (slot <= 0 || (size_t)slot >= heap_size || heap[slot].type != VM_PAGE)
		return NULL;
	struct page *p = heap[slot].page;
	return (p && p->type == ARRAY_PAGE) ? p : NULL;
}

// The value one element should hold after being set from v.
static int array_elem_value_for_store(int v)
{
	if (!array_elem_is_ref() || v <= 0 || (size_t)v >= heap_size)
		return v;
	if (hll_current_arg3 >= 0x10000) {
		// Reference elements share the object.
		if (HEAP_REF(v) > 0)
			heap_ref(v);
		return v;
	}
	if (heap[v].type == VM_STRING)
		return vm_copy((union vm_value){ .i = v }, AIN_STRING).i;
	if (heap[v].type == VM_PAGE && heap[v].page && heap[v].page->type == STRUCT_PAGE)
		return vm_copy((union vm_value){ .i = v }, AIN_STRUCT).i;
	if (HEAP_REF(v) > 0)
		heap_ref(v);
	return v;
}

static void array_elem_store(struct page *a, int phys, int value, int slot2)
{
	int v = array_elem_value_for_store(value);
	int old = a->values[phys].i;
	a->values[phys].i = v;
	if (array_elem_is_2slot())
		a->values[phys + 1].i = slot2;
	if (array_elem_is_ref() && old > 0 && (size_t)old < heap_size && HEAP_REF(old) > 0)
		heap_unref(old);
}

static int array_fill_range(struct page **array, int start, int count, int value)
{
	if (!array)
		return 0;
	struct page *a = *array;
	if (!a || a->type != ARRAY_PAGE)
		return 0;
	int numof = array_call_numof(a);
	if (start < 0) {
		count += start;
		start = 0;
	}
	if ((long long)start + count > numof)
		count = numof - start;
	int stride = array_call_stride();
	for (int i = 0; i < count; i++)
		array_elem_store(a, (start + i) * stride, value, hll_param_slot2);
	return count;
}

static int Array_Fill_All(struct page **array, int value)
{
	return array_fill_range(array, 0, array ? array_call_numof(*array) : 0, value);
}

static int Array_Fill_Range(struct page **array, int index, int length, int value)
{
	return array_fill_range(array, index, length, value);
}

static bool array_is_generic(const struct page *a)
{
	return a->a_type == AIN_ARRAY || a->a_type == AIN_REF_ARRAY;
}

static int array_copy_range(struct page **dst, int dst_i, struct page *src, int src_i, int count)
{
	if (!dst || !*dst || (*dst)->type != ARRAY_PAGE || !src)
		return 0;
	struct page *d = *dst;
	// Same normalisation order as the native helper.
	while (src_i < 0 || dst_i < 0) {
		if (src_i < 0) {
			dst_i -= src_i;
			count += src_i;
			src_i = 0;
		} else {
			src_i -= dst_i;
			count += dst_i;
			dst_i = 0;
		}
	}
	int s_num = array_call_numof(src);
	int d_num = array_call_numof(d);
	// Native quirk (0x648262): an overlong source range is clamped with the
	// destination's length, not the source's.
	if ((long long)src_i + count > s_num)
		count = d_num - src_i;
	if ((long long)dst_i + count > d_num)
		count = d_num - dst_i;
	if (count <= 0)
		return count;
	bool backward = d == src && dst_i > src_i && dst_i < src_i + count;
	int stride = array_call_stride();
	for (int k = 0; k < count; k++) {
		int e = backward ? count - 1 - k : k;
		int di = dst_i + e, si = src_i + e;
		if (si >= s_num) {
			// The native element read fails and stores a zero value.
			static int warned;
			if (warned++ < 8)
				WARNING("Array.Copy: source index %d past length %d; storing null", si, s_num);
			array_elem_store(d, di * stride, array_elem_is_ref() ? -1 : 0, 0);
			continue;
		}
		if (stride == 1 && !array_is_generic(d)) {
			// Typed arrays keep the existing per-type element copy.
			array_copy(d, di, src, si, 1);
			continue;
		}
		array_elem_store(d, di * stride, src->values[si * stride].i,
				 stride > 1 ? src->values[si * stride + 1].i : 0);
	}
	return count;
}

static int Array_Copy_All(struct page **dst, int src_wrap)
{
	struct page *src = array_wrap_page(src_wrap);
	return array_copy_range(dst, 0, src, 0, array_call_numof(src));
}

static int Array_Copy_To(struct page **dst, int dst_i, int src_wrap)
{
	struct page *src = array_wrap_page(src_wrap);
	return array_copy_range(dst, dst_i, src, 0, array_call_numof(src));
}

static int Array_Copy_From(struct page **dst, int src_wrap, int src_i, int count)
{
	return array_copy_range(dst, 0, array_wrap_page(src_wrap), src_i, count);
}

// destIndex is a plain int in every declaration. The old version looked it
// up as a wrap<int> handle whenever it matched a live page slot, so
// destIndex 1 read the first global.
static int Array_Copy(struct page **dst, int dst_i, int src_wrap, int src_i, int count)
{
	return array_copy_range(dst, dst_i, array_wrap_page(src_wrap), src_i, count);
}

// Resize as Realloc(n), then fill only the added tail [old, n).
static void Array_Realloc_Fill(struct page **array, int numof, int value)
{
	if (!array)
		return;
	// Nothing in the game fills an option array. Resize it as Realloc does
	// (new elements are none) rather than through the slot arithmetic below.
	if (array_elem_is_option()) {
		static int warned;
		if (warned++ < 8)
			WARNING("Array.Realloc: filling option elements is not supported (arg3 %#x); new elements are none", hll_current_arg3);
		if (numof >= 0)
			array_option_resize(array, numof, true);
		return;
	}
	int old = array_call_numof(*array);
	Array_Realloc(array, numof * array_call_stride());
	if (numof > old)
		array_fill_range(array, old, numof - old, value);
}

static bool array_arg_is_wrap_array(const struct ain_hll_function *f, int i)
{
	return f->arguments[i].type.data == AIN_WRAP && f->arguments[i].type.array_type
	    && f->arguments[i].type.array_type->data == AIN_ARRAY;
}

static void *array_fill_copy_function(const struct ain_hll_function *f)
{
	int n = f->nr_arguments;
	enum ain_data_type ret = f->return_type.data;
#define ARG(i) (f->arguments[i].type.data)
	if (!strcmp(f->name, "Realloc")) {
		if (ret != AIN_VOID || n < 2 || ARG(1) != AIN_INT)
			return NULL;
		if (n == 2)
			return (void *)Array_Realloc;
		if (n == 3 && ARG(2) == AIN_HLL_PARAM)
			return (void *)Array_Realloc_Fill;
		return NULL;
	}
	if (!strcmp(f->name, "Fill")) {
		if (ret != AIN_INT)
			return NULL;
		if (n == 2 && ARG(1) == AIN_HLL_PARAM)
			return (void *)Array_Fill_All;
		if (n == 4 && ARG(1) == AIN_INT && ARG(2) == AIN_INT && ARG(3) == AIN_HLL_PARAM)
			return (void *)Array_Fill_Range;
		return NULL;
	}
	if (ret != AIN_INT)
		return NULL;
	if (n == 2 && array_arg_is_wrap_array(f, 1))
		return (void *)Array_Copy_All;
	if (n == 3 && ARG(1) == AIN_INT && array_arg_is_wrap_array(f, 2))
		return (void *)Array_Copy_To;
	if (n == 4 && array_arg_is_wrap_array(f, 1) && ARG(2) == AIN_INT && ARG(3) == AIN_INT)
		return (void *)Array_Copy_From;
	if (n == 5 && ARG(1) == AIN_INT && array_arg_is_wrap_array(f, 2)
	    && ARG(3) == AIN_INT && ARG(4) == AIN_INT)
		return (void *)Array_Copy;
#undef ARG
	return NULL;
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
	bool last = !strcmp(f->name, "FindLast");
	if (!last && strcmp(f->name, "Find"))
		return NULL;
	bool range = f->nr_arguments == 4;
	if (range) {
		if (f->arguments[1].type.data != AIN_INT || f->arguments[2].type.data != AIN_INT)
			return NULL;
	} else if (f->nr_arguments != 2) {
		return NULL;
	}
	enum ain_data_type arg = f->arguments[range ? 3 : 1].type.data;
	if (arg == AIN_HLL_PARAM) {
		if (last)
			return range ? (void *)Array_FindLastValueRange : (void *)Array_FindLastValue;
		return range ? (void *)Array_FindValueRange : (void *)Array_FindValue;
	}
	if (arg == AIN_HLL_FUNC || arg == AIN_HLL_FUNC_71) {
		if (last)
			return range ? (void *)Array_FindLastIfRange : (void *)Array_FindLastIf;
		return range ? (void *)Array_FindIfRange : (void *)Array_FindIf;
	}
	return NULL;
}

/* Sort family: Sort / AscSort / DescSort / QuickSort, and Remain's return value.
 * Native: dispatcher 0x644300, jump table 0x644f18.
 *   [34] Sort(self,f)      0x644907 -> 0x648340: insertion 0x646980, pred cmp 0x645f70
 *   [35] Sort(self)        0x644927 -> 0x648420 -> 0x648500 (== AscSort)
 *   [36] AscSort(self)     0x644940 -> 0x648500: insertion 0x646980, value cmp 0x645dd0
 *   [37] DescSort(self)    0x644959 -> 0x6485e0: insertion 0x646980, 0x64b350 = !less(a,b)
 *   [38] QuickSort(self)   0x644972 -> 0x648700: quicksort 0x646a00, value cmp
 *   [39] QuickSort(self,f) 0x64498b -> 0x6487e0: quicksort 0x646a00, pred cmp
 * All return the self handle (vt+0x30) via 0x658150 type 0x4f with ref+1
 * (0x679f10). v14 bytecode stores it in a wrap<array<T>> dummy (X_ASSIGN
 * transfers ownership, later DELETE) or A_REFs it for return/chaining.
 * Value cmp 0x645dd0: INT/BOOL/ENUM signed <, FLOAT <, STRING unsigned strcmp<0,
 * anything else logs and is constantly false. */
enum array_sort_cmp { ARRAY_SORT_CMP_NONE, ARRAY_SORT_CMP_INT, ARRAY_SORT_CMP_FLOAT,
	ARRAY_SORT_CMP_STRING, ARRAY_SORT_CMP_FUNC };
struct array_sort_ctx {
	struct page **array; int owner; int stride; int n; enum array_sort_cmp cmp;
	bool invert; int func; int func_slots; bool func_ref;
};
static const char *array_sort_text(union vm_value v)
{
	if (v.i > 0 && (size_t)v.i < heap_size && HEAP_REF(v.i) > 0
	    && heap[v.i].type == VM_STRING && heap[v.i].s)
		return heap[v.i].s->text;
	return "";
}
// Generic v14 pages usually say AIN_ARRAY_INT whatever they hold; arg3 == 2
// (string or object handle) is resolved from the heap.
static enum array_sort_cmp array_sort_value_cmp(const struct page *a, int stride)
{
	if (stride != 1 || hll_current_arg3 >= 0x10000) return ARRAY_SORT_CMP_NONE;
	switch (a->a_type) {
	case AIN_FLOAT: case AIN_ARRAY_FLOAT: return ARRAY_SORT_CMP_FLOAT;
	case AIN_STRING: case AIN_ARRAY_STRING: return ARRAY_SORT_CMP_STRING;
	case AIN_STRUCT: case AIN_ARRAY_STRUCT:
	case AIN_LONG_INT: case AIN_ARRAY_LONG_INT: return ARRAY_SORT_CMP_NONE; // no LONG_INT case in 0x645dd0
	default: break;
	}
	if ((hll_current_arg3 & 0xFFFF) != 2) return ARRAY_SORT_CMP_INT;
	for (int i = 0; i < a->nr_vars; i++) {
		int s = a->values[i].i;
		if (s <= 0 || (size_t)s >= heap_size || HEAP_REF(s) <= 0) continue;
		if (heap[s].type == VM_STRING) return ARRAY_SORT_CMP_STRING;
		if (heap[s].type == VM_PAGE) return ARRAY_SORT_CMP_NONE;
	}
	return ARRAY_SORT_CMP_NONE;
}
static bool array_sort_func_shape(struct array_sort_ctx *c)
{
	if (c->func < 0 || c->func >= ain->nr_functions) return false;
	struct ain_function *cb = &ain->functions[c->func];
	if (cb->address >= ain->code_size || !cb->vars || cb->nr_vars < cb->nr_args
	    || cb->nr_args < 2 || (cb->nr_args & 1)) return false;
	int per = cb->nr_args / 2;
	c->func_ref = false;
	if (per == c->stride) { c->func_slots = per; return true; } // (T,T) or (iface,<void>,iface,<void>)
	if (per == 2 && c->stride == 1 && cb->vars[1].type.data == AIN_VOID && cb->vars[3].type.data == AIN_VOID) {
		switch (cb->vars[0].type.data) {
		case AIN_REF_INT: case AIN_REF_FLOAT: case AIN_REF_BOOL: case AIN_REF_LONG_INT:
			if (c->owner < 0) return false;
			c->func_ref = true; break;
		default: break; // (value, 0), same as Where/First
		}
		c->func_slots = 2; return true;
	}
	return false;
}
static void array_sort_push_operand(const struct array_sort_ctx *c, const struct page *a, int x)
{
	if (c->func_ref) { stack_push(c->owner); stack_push(x); return; }
	for (int k = 0; k < c->func_slots; k++) {
		if (k < c->stride) stack_push(a->values[x * c->stride + k]);
		else stack_push(0);
	}
}
static bool array_sort_call(struct array_sort_ctx *c, int x, int y)
{
	struct page *before = *c->array;
	int size = before->nr_vars, saved_sp = stack_ptr;
	array_sort_push_operand(c, before, x);
	array_sort_push_operand(c, before, y);
	vm_call_nopop(c->func, ain->functions[c->func].nr_args);
	bool r = stack_pop().i != 0;
	stack_ptr = saved_sp;
	// Same guard as array_query_predicate.
	if (c->owner >= 0) {
		if ((size_t)c->owner >= heap_size || HEAP_REF(c->owner) <= 0 || heap[c->owner].type != VM_PAGE)
			VM_ERROR("Array.Sort: comparator released its array owner");
		*c->array = heap[c->owner].page;
	}
	if (*c->array != before || !*c->array || (*c->array)->type != ARRAY_PAGE || (*c->array)->nr_vars != size)
		VM_ERROR("Array.Sort: comparator changed array storage");
	return r;
}
static bool array_sort_less(struct array_sort_ctx *c, int x, int y)
{
	const struct page *a = *c->array;
	union vm_value vx = a->values[x * c->stride], vy = a->values[y * c->stride];
	bool r;
	switch (c->cmp) {
	case ARRAY_SORT_CMP_INT: r = vx.i < vy.i; break;
	case ARRAY_SORT_CMP_FLOAT: r = vx.f < vy.f; break;
	case ARRAY_SORT_CMP_STRING: r = strcmp(array_sort_text(vx), array_sort_text(vy)) < 0; break;
	case ARRAY_SORT_CMP_FUNC: r = array_sort_call(c, x, y); break;
	default: r = false; break;
	}
	return c->invert ? !r : r;
}
static void array_sort_swap(struct array_sort_ctx *c, int x, int y)
{
	struct page *a = *c->array;
	for (int k = 0; k < c->stride; k++) {
		union vm_value t = a->values[x * c->stride + k];
		a->values[x * c->stride + k] = a->values[y * c->stride + k];
		a->values[y * c->stride + k] = t;
	}
}
// 0x646980. Length read once; stable for a strict less.
static void array_sort_insertion(struct array_sort_ctx *c)
{
	for (int i = 1; i < c->n; i++)
		for (int j = i; j > 0 && array_sort_less(c, j, j - 1); j--)
			array_sort_swap(c, j - 1, j);
}
// 0x646a00 on [lo, hi): recurse left, loop right, pivot tracked by index.
// Bounds/clamps only matter for an inconsistent cmp (native runs off the array).
static void array_sort_quick(struct array_sort_ctx *c, int lo, int hi)
{
	while (hi - lo > 1) {
		int i = lo, j = hi - 1, p = (lo + hi) / 2;
		while (i <= j) {
			while (i < hi && array_sort_less(c, i, p)) i++;
			while (j >= lo && array_sort_less(c, p, j)) j--;
			if (i >= j) break;
			array_sort_swap(c, i, j);
			if (i == p) p = j; else if (j == p) p = i;
			i++; j--;
		}
		if (i <= lo) i = lo + 1;
		if (i >= hi) i = hi - 1;
		array_sort_quick(c, lo, i);
		lo = i;
	}
}
// Owned reference to the self array for a wrap<?> return; -1 = none.
static int array_self_result(int self)
{
	if (self <= 0 || (size_t)self >= heap_size || HEAP_REF(self) <= 0 || heap[self].type != VM_PAGE)
		return -1;
	heap_ref(self);
	return self;
}
static int array_sort_run(struct page **array, int func, bool has_func, bool invert, bool quick)
{
	int self = hll_self_slot;
	struct page *a = (array && *array && (*array)->type == ARRAY_PAGE) ? *array : NULL;
	if (!a) return array_self_result(self);
	struct array_sort_ctx c = { .array = array, .owner = -1, .func = func, .invert = invert,
		.stride = array_elem_is_2slot() ? 2 : 1 };
	if (a->nr_vars % c.stride) {
		WARNING("Array.Sort: %d slots not a multiple of stride %d", a->nr_vars, c.stride);
		return array_self_result(self);
	}
	c.n = a->nr_vars / c.stride;
	if (self > 0 && (size_t)self < heap_size && HEAP_REF(self) > 0
	    && heap[self].type == VM_PAGE && heap[self].page == a)
		c.owner = self;
	if (c.n <= 1) return array_self_result(self);
	if (has_func) {
		c.cmp = ARRAY_SORT_CMP_FUNC;
		if (!array_sort_func_shape(&c)) {
			static int warned;
			if (warned++ < 5) WARNING("Array.Sort: unsupported comparator %d (stride %d), not sorted", func, c.stride);
			return array_self_result(self);
		}
	} else {
		c.cmp = array_sort_value_cmp(a, c.stride);
		if (c.cmp == ARRAY_SORT_CMP_NONE) { // native logs and continues with a constant-false cmp
			static int warned;
			if (warned++ < 5) WARNING("Array.Sort: element type has no default order (a_type %d, arg3 %#x)", a->a_type, hll_current_arg3);
		}
	}
	if (quick) array_sort_quick(&c, 0, c.n); else array_sort_insertion(&c);
	return array_self_result(self);
}
static int Array_SortPred(struct page **array, int func) { return array_sort_run(array, func, true, false, false); }       // [34]
static int Array_SortValue(struct page **array) { return array_sort_run(array, -1, false, false, false); }                // [35] == AscSort
static int Array_QuickSortValue(struct page **array) { return array_sort_run(array, -1, false, false, true); }            // [38]
static int Array_QuickSortPred(struct page **array, int func) { return array_sort_run(array, func, true, false, true); }  // [39]
static int Array_AscSortWrap(struct page **array) { return array_sort_run(array, -1, false, false, false); }              // [36]
static int Array_DescSortWrap(struct page **array) { return array_sort_run(array, -1, false, true, false); }             // [37] !less
// [19] Remain: filtering stays in the existing Array_Remain; native 0x647f60 returns self like Sort.
static void Array_Remain(struct page **array, int func);
static int Array_RemainWrap(struct page **array, int func)
{
	int self = hll_self_slot;
	Array_Remain(array, func);
	return array_self_result(self);
}
// NULL = shape not recognised, caller keeps the name-matched default.
static void *array_sort_function(const struct ain_hll_function *f)
{
	if (!f->arguments || f->nr_arguments < 1) return NULL;
	switch (f->arguments[0].type.data) {
	case AIN_REF_ARRAY_TYPE:
	case AIN_REF_ARRAY: break;
	default: return NULL;
	}
	bool func = f->nr_arguments == 2 && (f->arguments[1].type.data == AIN_HLL_FUNC
					     || f->arguments[1].type.data == AIN_HLL_FUNC_71);
	enum ain_data_type ret = f->return_type.data;
	if (ret != AIN_WRAP) return NULL;
	if (!strcmp(f->name, "Sort"))
		return func ? (void *)Array_SortPred : f->nr_arguments == 1 ? (void *)Array_SortValue : NULL;
	if (!strcmp(f->name, "QuickSort"))
		return func ? (void *)Array_QuickSortPred : f->nr_arguments == 1 ? (void *)Array_QuickSortValue : NULL;
	if (!strcmp(f->name, "AscSort")) return f->nr_arguments == 1 ? (void *)Array_AscSortWrap : NULL;
	if (!strcmp(f->name, "DescSort")) return f->nr_arguments == 1 ? (void *)Array_DescSortWrap : NULL;
	if (!strcmp(f->name, "Remain")) return func ? (void *)Array_RemainWrap : NULL;
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
	if (!strcmp(f->name, "FindLast")) {
		void *fn = array_query_function(f);
		return fn ? fn : fallback;
	}
	if (!strcmp(f->name, "First"))
		return array_decl_has_func(f) ? (void *)Array_First : (void *)Array_First_NoPred;
	if (!strcmp(f->name, "Sort") || !strcmp(f->name, "QuickSort")
	    || !strcmp(f->name, "AscSort") || !strcmp(f->name, "DescSort")
	    || !strcmp(f->name, "Remain")) {
		void *fn = array_sort_function(f);
		return fn ? fn : fallback;
	}
	if (!strcmp(f->name, "Fill") || !strcmp(f->name, "Copy") || !strcmp(f->name, "Realloc")) {
		void *fn = f->arguments ? array_fill_copy_function(f) : NULL;
		return fn ? fn : fallback;
	}
	if (!strcmp(f->name, "Any")) {
		if (f->return_type.data != AIN_BOOL || f->nr_arguments < 1)
			return fallback;
		if (f->nr_arguments == 1)
			return (void *)Array_Any;
		return f->nr_arguments == 2 && array_decl_has_func(f) ? (void *)Array_AnyIf : fallback;
	}
	if (!strcmp(f->name, "Unique") || !strcmp(f->name, "UniqueSorted")) {
		bool sorted = !strcmp(f->name, "UniqueSorted");
		if (f->return_type.data != AIN_VOID)
			return fallback;
		if (f->nr_arguments == 1)
			return sorted ? (void *)Array_UniqueSorted : (void *)Array_Unique;
		if (f->nr_arguments == 2 && array_decl_has_func(f))
			return sorted ? (void *)Array_UniqueSortedIf : (void *)Array_UniqueIf;
		return fallback;
	}
	if (!strcmp(f->name, "Equals")) {
		if (f->return_type.data != AIN_BOOL || f->nr_arguments < 2
		    || f->arguments[1].type.data != AIN_WRAP)
			return fallback;
		if (f->nr_arguments == 2)
			return (void *)Array_Equals;
		return f->nr_arguments == 3 && array_decl_has_func(f) ? (void *)Array_EqualsIf : fallback;
	}
	if (!strcmp(f->name, "Min") || !strcmp(f->name, "Max") || !strcmp(f->name, "Last")) {
		bool func = array_decl_has_func(f);
		if (f->nr_arguments != (func ? 2 : 1) || f->return_type.data != AIN_REF_HLL_PARAM)
			return fallback;
		if (!strcmp(f->name, "Last"))
			return func ? (void *)Array_LastPredicate : fallback;
		if (!strcmp(f->name, "Min"))
			return func ? (void *)Array_MinPredicate : (void *)Array_MinNoPred;
		return func ? (void *)Array_MaxPredicate : (void *)Array_MaxNoPred;
	}
	if (!strcmp(f->name, "LowerBound") || !strcmp(f->name, "UpperBound")
	    || !strcmp(f->name, "BinarySearch")) {
		void *fn = array_bound_function(f);
		return fn ? fn : fallback;
	}
	if (!strcmp(f->name, "SYSTEMONLY_GetStructPageList")) {
		extern bool system_struct_list_consumers_supported(void);
		if (f->return_type.data == AIN_ARRAY && f->return_type.array_type
		    && f->return_type.array_type->data == AIN_INT
		    && f->nr_arguments == 1 && f->arguments && f->arguments[0].type.data == AIN_REF_ARRAY
		    && system_struct_list_consumers_supported())
			return (void *)Array_SYSTEMONLY_GetStructPageList_v14;
		return fallback;
	}
	return fallback;
}

// Concat: append all elements from src to self (both wrap<array>)
// AIN: Concat(wrap<array<T>> self, wrap<array<T>> src) -> void
static void Array_Concat(struct page **self, int src_wrap)
{
	// self is ref array, src is wrap<array> (heap slot)
	Array_AddRange(self, src_wrap);
}


static void Array_Reverse(struct page **array)
{
	if (!array || !*array)
		return;
	// Two-slot elements keep their (object, vtable offset) order.
	struct page *a = *array;
	int stride = array_erase_stride(a);
	if (stride <= 1) {
		array_reverse(a);
		return;
	}
	for (int i = 0, j = a->nr_vars / stride - 1; i < j; i++, j--) {
		for (int k = 0; k < stride; k++) {
			union vm_value t = a->values[i * stride + k];
			a->values[i * stride + k] = a->values[j * stride + k];
			a->values[j * stride + k] = t;
		}
	}
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
	    HLL_EXPORT(Fill, Array_Fill_Range),
	    HLL_EXPORT(SYSTEMONLY_GetStructPageList, Array_SYSTEMONLY_GetStructPageList),
	    HLL_EXPORT(Concat, Array_Concat),
	    HLL_EXPORT(Max, Array_MaxNoPred),
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
	    HLL_EXPORT(FindLast, Array_FindLastValue),
	    HLL_EXPORT(Min, Array_MinNoPred),
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
