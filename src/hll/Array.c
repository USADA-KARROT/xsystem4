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

#include <stdlib.h>

#include "hll.h"
#include "vm.h"
#include "vm/heap.h"
#include "vm/page.h"

static void check_array(struct page *array)
{
	if (!array || array->type != ARRAY_PAGE)
		return;  // silently accept invalid arrays in v14
}


// Alloc: allocate an integer array of given size
static void Array_Alloc(struct page **array, int numof)
{
	if (!array || numof < 0)
		return;
	if (*array && (*array)->type == ARRAY_PAGE) {
		delete_page_vars(*array);
		free_page(*array);
	}
	*array = alloc_page(ARRAY_PAGE, AIN_ARRAY_INT, numof);
	(*array)->array.rank = 1;
}

// PushBack (capital B) — alias for Pushback
static void Array_PushBack(struct page **array, int value)
{
	if (!array)
		return;
	struct page *a = *array;
	int old_size = a ? a->nr_vars : 0;
	struct page *new_a = alloc_page(ARRAY_PAGE, a ? a->a_type : AIN_ARRAY_INT, old_size + 1);
	if (a) {
		for (int i = 0; i < old_size; i++)
			new_a->values[i] = a->values[i];
		new_a->array = a->array;
		free_page(a);
	}
	new_a->values[old_size].i = value;
	*array = new_a;
}

// EraseAll: erase all elements matching a predicate
static bool Array_EraseAll(struct page **array, int func)
{
	struct page *src = (array && *array) ? *array : NULL;
	if (!src || src->nr_vars == 0 || func < 0)
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
	if (!src || src->nr_vars == 0 || func < 0) {
		// Return empty array
		struct page *result = alloc_page(ARRAY_PAGE, AIN_ARRAY_INT, 0);
		result->array.rank = 1;
		int slot = heap_alloc_slot(VM_PAGE);
		heap_set_page(slot, result);
		return slot;
	}

	struct ain_function *cb = &ain->functions[func];

	// Collect matching elements by calling the predicate function for each
	int *matches = malloc(src->nr_vars * sizeof(int));
	int match_count = 0;

	for (int i = 0; i < src->nr_vars; i++) {
		int saved_sp = stack_ptr;

		// Push args on stack (they stay for the bytecode to consume via nopop)
		if (cb->nr_args >= 2) {
			// Ref parameter: push element value (struct heap slot) and 0
			stack_push(src->values[i]);
			stack_push(0);
		} else {
			stack_push(src->values[i]);
		}

		vm_call_nopop(func, cb->nr_args);

		int result = stack_pop().i;
		stack_ptr = saved_sp;
		if (result) {
			matches[match_count++] = src->values[i].i;
		}
	}

	// Build result array
	struct page *result_page = alloc_page(ARRAY_PAGE, AIN_ARRAY_INT, match_count);
	result_page->array.rank = 1;
	for (int i = 0; i < match_count; i++) {
		result_page->values[i].i = matches[i];
	}
	free(matches);

	int slot = heap_alloc_slot(VM_PAGE);
	heap_set_page(slot, result_page);
	return slot;
}

// First: return first element matching predicate, or -1 if none
static int Array_First(struct page **array, int func)
{
	struct page *src = (array && *array) ? *array : NULL;
	if (!src || src->nr_vars == 0 || func < 0)
		return -1;

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
			return src->values[i].i;
		}
	}
	return -1;
}

// Any: return true if any element matches the predicate
static bool Array_Any(struct page **array, int func)
{
	struct page *src = (array && *array) ? *array : NULL;
	if (!src || src->nr_vars == 0 || func < 0)
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
	return array ? array->nr_vars : 0;
}

static int Array_Empty(struct page **self)
{
	struct page *array = (self && *self) ? *self : NULL;
	return !array || array->nr_vars == 0;
}

static int Array_At(struct page **self, int index)
{
	struct page *array = (self && *self) ? *self : NULL;
	if (!array || index < 0 || index >= array->nr_vars)
		return 0;
	return array->values[index].i;
}

static int Array_Last(struct page **self)
{
	struct page *array = (self && *self) ? *self : NULL;
	if (!array || array->nr_vars <= 0)
		return 0;
	return array->values[array->nr_vars - 1].i;
}

// PopBack (capital B) — v14 name
static void Array_PopBack(struct page **array)
{
	if (!array || !*array || (*array)->nr_vars <= 0)
		return;
	struct page *a = *array;
	int new_size = a->nr_vars - 1;
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
	struct page *a = *array;
	int old_size = a ? a->nr_vars : 0;
	struct page *new_a = alloc_page(ARRAY_PAGE, a ? a->a_type : AIN_ARRAY_INT, old_size + 1);
	if (a) {
		for (int i = 0; i < old_size; i++)
			new_a->values[i] = a->values[i];
		new_a->array = a->array;
		free_page(a);
	}
	new_a->values[old_size].i = value;
	*array = new_a;
}

static void Array_Popback(struct page **array)
{
	if (!array || !*array || (*array)->nr_vars <= 0)
		return;
	struct page *a = *array;
	int new_size = a->nr_vars - 1;
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

static void Array_Erase(struct page **array, int index)
{
	if (!array || !*array)
		return;
	struct page *a = *array;
	if (index < 0 || index >= a->nr_vars)
		return;
	int new_size = a->nr_vars - 1;
	if (new_size == 0) {
		free_page(a);
		*array = NULL;
		return;
	}
	struct page *new_a = alloc_page(ARRAY_PAGE, a->a_type, new_size);
	for (int i = 0; i < index; i++)
		new_a->values[i] = a->values[i];
	for (int i = index; i < new_size; i++)
		new_a->values[i] = a->values[i + 1];
	new_a->array = a->array;
	free_page(a);
	*array = new_a;
}

static void Array_Insert(struct page **array, int index, int value)
{
	if (!array)
		return;
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

//void Array_NV_copy(struct page **nArray, int nNum);
//void Array_NV_add(struct page **nArray, int nNum);
//void Array_NV_sub(struct page **nArray, int nNum);
//void Array_NV_mul(struct page **nArray, int nNum);
//void Array_NV_div(struct page **nArray, int nNum);
//void Array_NV_and(struct page **nArray, int nNum);
//void Array_NV_or(struct page **nArray, int nNum);
//void Array_NV_xor(struct page **nArray, int nNum);
//void Array_NV_min(struct page **nArray, int nNum);
//void Array_NV_max(struct page **nArray, int nNum);
//void Array_NN_copy(struct page **nArray, struct page *nArrayS);
//void Array_NN_add(struct page **nArray, struct page *nArrayS);
//void Array_NN_sub(struct page **nArray, struct page *nArrayS);
//void Array_NN_mul(struct page **nArray, struct page *nArrayS);
//void Array_NN_div(struct page **nArray, struct page *nArrayS);
//void Array_NN_and(struct page **nArray, struct page *nArrayS);
//void Array_NN_or(struct page **nArray, struct page *nArrayS);
//void Array_NN_xor(struct page **nArray, struct page *nArrayS);
//void Array_NN_min(struct page **nArray, struct page *nArrayS);
//void Array_NN_max(struct page **nArray, struct page *nArrayS);
//void Array_NS_copy(struct page **nArray, struct page *sArray, int nMember);
//void Array_NS_add(struct page **nArray, struct page *sArray, int nMember);
//void Array_NS_sub(struct page **nArray, struct page *sArray, int nMember);
//void Array_NS_mul(struct page **nArray, struct page *sArray, int nMember);
//void Array_NS_div(struct page **nArray, struct page *sArray, int nMember);
//void Array_NS_and(struct page **nArray, struct page *sArray, int nMember);
//void Array_NS_or(struct page **nArray, struct page *sArray, int nMember);
//void Array_NS_xor(struct page **nArray, struct page *sArray, int nMember);
//void Array_NS_min(struct page **nArray, struct page *sArray, int nMember);
//void Array_NS_max(struct page **nArray, struct page *sArray, int nMember);
//void Array_SV_copy(struct page **sArray, int nMember, int nNum);
//void Array_SV_add(struct page **sArray, int nMember, int nNum);
//void Array_SV_sub(struct page **sArray, int nMember, int nNum);
//void Array_SV_mul(struct page **sArray, int nMember, int nNum);
//void Array_SV_div(struct page **sArray, int nMember, int nNum);
//void Array_SV_and(struct page **sArray, int nMember, int nNum);
//void Array_SV_or(struct page **sArray, int nMember, int nNum);
//void Array_SV_xor(struct page **sArray, int nMember, int nNum);
//void Array_SV_min(struct page **sArray, int nMember, int nNum);
//void Array_SV_max(struct page **sArray, int nMember, int nNum);
//void Array_SN_copy(struct page **sArray, int nMember, struct page *nArray);
//void Array_SN_add(struct page **sArray, int nMember, struct page *nArray);
//void Array_SN_sub(struct page **sArray, int nMember, struct page *nArray);
//void Array_SN_mul(struct page **sArray, int nMember, struct page *nArray);
//void Array_SN_div(struct page **sArray, int nMember, struct page *nArray);
//void Array_SN_and(struct page **sArray, int nMember, struct page *nArray);
//void Array_SN_or(struct page **sArray, int nMember, struct page *nArray);
//void Array_SN_xor(struct page **sArray, int nMember, struct page *nArray);
//void Array_SN_min(struct page **sArray, int nMember, struct page *nArray);
//void Array_SN_max(struct page **sArray, int nMember, struct page *nArray);
//void Array_SS_copy(struct page **sArray, int nMember, struct page *sArrayS, int nMemberS);
//void Array_SS_add(struct page **sArray, int nMember, struct page *sArrayS, int nMemberS);
//void Array_SS_sub(struct page **sArray, int nMember, struct page *sArrayS, int nMemberS);
//void Array_SS_mul(struct page **sArray, int nMember, struct page *sArrayS, int nMemberS);
//void Array_SS_div(struct page **sArray, int nMember, struct page *sArrayS, int nMemberS);
//void Array_SS_and(struct page **sArray, int nMember, struct page *sArrayS, int nMemberS);
//void Array_SS_or(struct page **sArray, int nMember, struct page *sArrayS, int nMemberS);
//void Array_SS_xor(struct page **sArray, int nMember, struct page *sArrayS, int nMemberS);
//void Array_SS_min(struct page **sArray, int nMember, struct page *sArrayS, int nMemberS);
//void Array_SS_max(struct page **sArray, int nMember, struct page *sArrayS, int nMemberS);

static int Array_NV_eneq(struct page **self, int num)
{
	struct page *array = (self && *self) ? *self : NULL;
	if (!array)
		return 0;
	int count = 0;
	for (int i = 0; i < array->nr_vars; i++) {
		if (array->values[i].i == num)
			count++;
	}
	return count;
}

//int Array_NV_enne(struct page *nArray, int nNum);
//int Array_NV_enlo(struct page *nArray, int nNum);
//int Array_NV_enhi(struct page *nArray, int nNum);
//int Array_NV_enra(struct page *nArray, int nMin, int nMax);
//int Array_NN_eneq(struct page *nArray, struct page *nArrayS);
//int Array_NN_enne(struct page *nArray, struct page *nArrayS);
//int Array_NN_enlo(struct page *nArray, struct page *nArrayS);
//int Array_NN_enhi(struct page *nArray, struct page *nArrayS);
//int Array_NS_eneq(struct page *nArray, struct page *sArray, int nMember);
//int Array_NS_enne(struct page *nArray, struct page *sArray, int nMember);
//int Array_NS_enlo(struct page *nArray, struct page *sArray, int nMember);
//int Array_NS_enhi(struct page *nArray, struct page *sArray, int nMember);
//int Array_SV_eneq(struct page *sArray, int nMember, int nNum);
//int Array_SV_enne(struct page *sArray, int nMember, int nNum);
//int Array_SV_enlo(struct page *sArray, int nMember, int nNum);
//int Array_SV_enhi(struct page *sArray, int nMember, int nNum);
//int Array_SV_enra(struct page *sArray, int nMember, int nMin, int nMax);
//int Array_SN_eneq(struct page *sArray, int nMember, struct page *nArray);
//int Array_SN_enne(struct page *sArray, int nMember, struct page *nArray);
//int Array_SN_enlo(struct page *sArray, int nMember, struct page *nArray);
//int Array_SN_enhi(struct page *sArray, int nMember, struct page *nArray);
//int Array_SS_eneq(struct page *sArray, int nMember, struct page *sArrayS, int nMemberS);
//int Array_SS_enne(struct page *sArray, int nMember, struct page *sArrayS, int nMemberS);
//int Array_SS_enlo(struct page *sArray, int nMember, struct page *sArrayS, int nMemberS);
//int Array_SS_enhi(struct page *sArray, int nMember, struct page *sArrayS, int nMemberS);
//void Array_NV_cheq(struct page **nArray, int nNum, int nChg);
//void Array_NV_chne(struct page **nArray, int nNum, int nChg);
//void Array_NV_chlo(struct page **nArray, int nNum, int nChg);
//void Array_NV_chhi(struct page **nArray, int nNum, int nChg);
//void Array_NV_chra(struct page **nArray, int nMin, int nMax, int nChg);
//void Array_NN_cheq(struct page **nArray, struct page *nArrayS, int nChg);
//void Array_NN_chne(struct page **nArray, struct page *nArrayS, int nChg);
//void Array_NN_chlo(struct page **nArray, struct page *nArrayS, int nChg);
//void Array_NN_chhi(struct page **nArray, struct page *nArrayS, int nChg);
//void Array_NS_cheq(struct page **nArray, struct page *sArray, int nMember, int nChg);
//void Array_NS_chne(struct page **nArray, struct page *sArray, int nMember, int nChg);
//void Array_NS_chlo(struct page **nArray, struct page *sArray, int nMember, int nChg);
//void Array_NS_chhi(struct page **nArray, struct page *sArray, int nMember, int nChg);
//void Array_SV_cheq(struct page **sArray, int nMember, int nNum, int nChg);
//void Array_SV_chne(struct page **sArray, int nMember, int nNum, int nChg);
//void Array_SV_chlo(struct page **sArray, int nMember, int nNum, int nChg);
//void Array_SV_chhi(struct page **sArray, int nMember, int nNum, int nChg);
//void Array_SV_chra(struct page **sArray, int nMember, int nMin, int nMax, int nChg);
//void Array_SN_cheq(struct page **sArray, int nMember, struct page *nArray, int nChg);
//void Array_SN_chne(struct page **sArray, int nMember, struct page *nArray, int nChg);
//void Array_SN_chlo(struct page **sArray, int nMember, struct page *nArray, int nChg);
//void Array_SN_chhi(struct page **sArray, int nMember, struct page *nArray, int nChg);
//void Array_SS_cheq(struct page **sArray, int nMember, struct page *sArrayS, int nMemberS, int nChg);
//void Array_SS_chne(struct page **sArray, int nMember, struct page *sArrayS, int nMemberS, int nChg);
//void Array_SS_chlo(struct page **sArray, int nMember, struct page *sArrayS, int nMemberS, int nChg);
//void Array_SS_chhi(struct page **sArray, int nMember, struct page *sArrayS, int nMemberS, int nChg);
//void Array_NV_fweq(struct page *nArray, int nNum, struct page **nArrayD);
//void Array_NV_fwne(struct page *nArray, int nNum, struct page **nArrayD);
//void Array_NV_fwlo(struct page *nArray, int nNum, struct page **nArrayD);
//void Array_NV_fwhi(struct page *nArray, int nNum, struct page **nArrayD);
//void Array_NV_fwra(struct page *nArray, int nMin, int nMax, struct page **nArrayD);
//void Array_NV_faeq(struct page *nArray, int nNum, struct page **nArrayD);
//void Array_NV_fane(struct page *nArray, int nNum, struct page **nArrayD);
//void Array_NV_falo(struct page *nArray, int nNum, struct page **nArrayD);
//void Array_NV_fahi(struct page *nArray, int nNum, struct page **nArrayD);
//void Array_NV_fara(struct page *nArray, int nMin, int nMax, struct page **nArrayD);
//void Array_NV_foeq(struct page *nArray, int nNum, struct page **nArrayD);
//void Array_NV_fone(struct page *nArray, int nNum, struct page **nArrayD);
//void Array_NV_folo(struct page *nArray, int nNum, struct page **nArrayD);
//void Array_NV_fohi(struct page *nArray, int nNum, struct page **nArrayD);
//void Array_NV_fora(struct page *nArray, int nMin, int nMax, struct page **nArrayD);
//void Array_NN_fweq(struct page *nArray, struct page *nArrayS, struct page **nArrayD);
//void Array_NN_fwne(struct page *nArray, struct page *nArrayS, struct page **nArrayD);
//void Array_NN_fwlo(struct page *nArray, struct page *nArrayS, struct page **nArrayD);
//void Array_NN_fwhi(struct page *nArray, struct page *nArrayS, struct page **nArrayD);
//void Array_NN_faeq(struct page *nArray, struct page *nArrayS, struct page **nArrayD);
//void Array_NN_fane(struct page *nArray, struct page *nArrayS, struct page **nArrayD);
//void Array_NN_falo(struct page *nArray, struct page *nArrayS, struct page **nArrayD);
//void Array_NN_fahi(struct page *nArray, struct page *nArrayS, struct page **nArrayD);
//void Array_NN_foeq(struct page *nArray, struct page *nArrayS, struct page **nArrayD);
//void Array_NN_fone(struct page *nArray, struct page *nArrayS, struct page **nArrayD);
//void Array_NN_folo(struct page *nArray, struct page *nArrayS, struct page **nArrayD);
//void Array_NN_fohi(struct page *nArray, struct page *nArrayS, struct page **nArrayD);
//void Array_NS_fweq(struct page *nArray, struct page *sArray, int nMember, struct page **nArrayD);
//void Array_NS_fwne(struct page *nArray, struct page *sArray, int nMember, struct page **nArrayD);
//void Array_NS_fwlo(struct page *nArray, struct page *sArray, int nMember, struct page **nArrayD);
//void Array_NS_fwhi(struct page *nArray, struct page *sArray, int nMember, struct page **nArrayD);
//void Array_NS_faeq(struct page *nArray, struct page *sArray, int nMember, struct page **nArrayD);
//void Array_NS_fane(struct page *nArray, struct page *sArray, int nMember, struct page **nArrayD);
//void Array_NS_falo(struct page *nArray, struct page *sArray, int nMember, struct page **nArrayD);
//void Array_NS_fahi(struct page *nArray, struct page *sArray, int nMember, struct page **nArrayD);
//void Array_NS_foeq(struct page *nArray, struct page *sArray, int nMember, struct page **nArrayD);
//void Array_NS_fone(struct page *nArray, struct page *sArray, int nMember, struct page **nArrayD);
//void Array_NS_folo(struct page *nArray, struct page *sArray, int nMember, struct page **nArrayD);
//void Array_NS_fohi(struct page *nArray, struct page *sArray, int nMember, struct page **nArrayD);
//void Array_SV_fweq(struct page *sArray, int nMember, int nNum, struct page **nArrayD);
//void Array_SV_fwne(struct page *sArray, int nMember, int nNum, struct page **nArrayD);
//void Array_SV_fwlo(struct page *sArray, int nMember, int nNum, struct page **nArrayD);
//void Array_SV_fwhi(struct page *sArray, int nMember, int nNum, struct page **nArrayD);
//void Array_SV_fwra(struct page *sArray, int nMember, int nMin, int nMax, struct page **nArrayD);
//void Array_SV_faeq(struct page *sArray, int nMember, int nNum, struct page **nArrayD);
//void Array_SV_fane(struct page *sArray, int nMember, int nNum, struct page **nArrayD);
//void Array_SV_falo(struct page *sArray, int nMember, int nNum, struct page **nArrayD);
//void Array_SV_fahi(struct page *sArray, int nMember, int nNum, struct page **nArrayD);
//void Array_SV_fara(struct page *sArray, int nMember, int nMin, int nMax, struct page **nArrayD);
//void Array_SV_foeq(struct page *sArray, int nMember, int nNum, struct page **nArrayD);
//void Array_SV_fone(struct page *sArray, int nMember, int nNum, struct page **nArrayD);
//void Array_SV_folo(struct page *sArray, int nMember, int nNum, struct page **nArrayD);
//void Array_SV_fohi(struct page *sArray, int nMember, int nNum, struct page **nArrayD);
//void Array_SV_fora(struct page *sArray, int nMember, int nMin, int nMax, struct page **nArrayD);
//void Array_SN_fweq(struct page *sArray, int nMember, struct page *nArrayS, struct page **nArrayD);
//void Array_SN_fwne(struct page *sArray, int nMember, struct page *nArrayS, struct page **nArrayD);
//void Array_SN_fwlo(struct page *sArray, int nMember, struct page *nArrayS, struct page **nArrayD);
//void Array_SN_fwhi(struct page *sArray, int nMember, struct page *nArrayS, struct page **nArrayD);
//void Array_SN_faeq(struct page *sArray, int nMember, struct page *nArrayS, struct page **nArrayD);
//void Array_SN_fane(struct page *sArray, int nMember, struct page *nArrayS, struct page **nArrayD);
//void Array_SN_falo(struct page *sArray, int nMember, struct page *nArrayS, struct page **nArrayD);
//void Array_SN_fahi(struct page *sArray, int nMember, struct page *nArrayS, struct page **nArrayD);
//void Array_SN_foeq(struct page *sArray, int nMember, struct page *nArrayS, struct page **nArrayD);
//void Array_SN_fone(struct page *sArray, int nMember, struct page *nArrayS, struct page **nArrayD);
//void Array_SN_folo(struct page *sArray, int nMember, struct page *nArrayS, struct page **nArrayD);
//void Array_SN_fohi(struct page *sArray, int nMember, struct page *nArrayS, struct page **nArrayD);
//void Array_SS_fweq(struct page *sArray, int nMember, struct page *sArrayS, int nMemberS, struct page **nArrayD);
//void Array_SS_fwne(struct page *sArray, int nMember, struct page *sArrayS, int nMemberS, struct page **nArrayD);
//void Array_SS_fwlo(struct page *sArray, int nMember, struct page *sArrayS, int nMemberS, struct page **nArrayD);
//void Array_SS_fwhi(struct page *sArray, int nMember, struct page *sArrayS, int nMemberS, struct page **nArrayD);
//void Array_SS_faeq(struct page *sArray, int nMember, struct page *sArrayS, int nMemberS, struct page **nArrayD);
//void Array_SS_fane(struct page *sArray, int nMember, struct page *sArrayS, int nMemberS, struct page **nArrayD);
//void Array_SS_falo(struct page *sArray, int nMember, struct page *sArrayS, int nMemberS, struct page **nArrayD);
//void Array_SS_fahi(struct page *sArray, int nMember, struct page *sArrayS, int nMemberS, struct page **nArrayD);
//void Array_SS_foeq(struct page *sArray, int nMember, struct page *sArrayS, int nMemberS, struct page **nArrayD);
//void Array_SS_fone(struct page *sArray, int nMember, struct page *sArrayS, int nMemberS, struct page **nArrayD);
//void Array_SS_folo(struct page *sArray, int nMember, struct page *sArrayS, int nMemberS, struct page **nArrayD);
//void Array_SS_fohi(struct page *sArray, int nMember, struct page *sArrayS, int nMemberS, struct page **nArrayD);

static int Array_NV_sceq(struct page **self, int index, int num, int *out_index)
{
	struct page *array = (self && *self) ? *self : NULL;
	if (!array)
		return 0;

	if (index >= 0) {
		for (int i = index; i < array->nr_vars; i++) {
			if (array->values[i].i == num) {
				*out_index = i;
				return 1;
			}
		}
	} else {
		// Negative index means backwards search
		index &= 0x7fffffff;
		if (index >= array->nr_vars)
			return 0;
		for (int i = index; i >= 0; --i) {
			if (array->values[i].i == num) {
				*out_index = i;
				return 1;
			}
		}
	}
	return 0;
}

//int Array_NV_scne(struct page *nArray, int nIndex, int nNum, int *pnIndex);
//int Array_NV_sclo(struct page *nArray, int nIndex, int nNum, int *pnIndex);
//int Array_NV_schi(struct page *nArray, int nIndex, int nNum, int *pnIndex);
//int Array_NV_scra(struct page *nArray, int nIndex, int nMin, int nMax, int *pnIndex);
//int Array_SV_sceq(struct page *sArray, int nMember, int nIndex, int nNum, int *pnIndex);
//int Array_SV_scne(struct page *sArray, int nMember, int nIndex, int nNum, int *pnIndex);
//int Array_SV_sclo(struct page *sArray, int nMember, int nIndex, int nNum, int *pnIndex);
//int Array_SV_schi(struct page *sArray, int nMember, int nIndex, int nNum, int *pnIndex);
//int Array_SV_scra(struct page *sArray, int nMember, int nIndex, int nMin, int nMax, int *pnIndex);
//int Array_NN_sclowest(struct page *nArray, struct page **nArrayD, int *pnIndex);
//int Array_NN_schighest(struct page *nArray, struct page **nArrayD, int *pnIndex);
//int Array_NS_sclowest(struct page *nArray, struct page **sArray, int nMember, int *pnIndex);
//int Array_NS_schighest(struct page *nArray, struct page **sArray, int nMember, int *pnIndex);
//int Array_SN_sclowest(struct page *sArray, int nMember, struct page **nArray, int *pnIndex);
//int Array_SN_schighest(struct page *sArray, int nMember, struct page **nArray, int *pnIndex);
//int Array_SS_sclowest(struct page *sArray, int nMember, struct page **sArrayD, int nMemberS, int *pnIndex);
//int Array_SS_schighest(struct page *sArray, int nMember, struct page **sArrayD, int nMemberS, int *pnIndex);
//int Array_VN_add(struct page *nArray);
//int Array_VN_and(struct page *nArray);
// Realloc: resize array, preserving existing elements
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
	free_page(old);
	*array = new_a;
}

// Add: alias for Pushback (v14 generic array)
static void Array_Add(struct page **array, int value)
{
	Array_Pushback(array, value);
}

// Find: alias for First (v14 generic array)
static int Array_Find(struct page **array, int func)
{
	return Array_First(array, func);
}

// Copy: copy elements between arrays
static void Array_Copy(struct page **dst, int dst_i, struct page **src, int src_i, int count)
{
	if (!dst || !*dst || !src || !*src || count <= 0)
		return;
	array_copy(*dst, dst_i, *src, src_i, count);
}

//int Array_VN_or(struct page *nArray);
//int Array_VS_add(struct page *sArray, int nMember);
//int Array_VS_and(struct page *sArray, int nMember);
//int Array_VS_or(struct page *sArray, int nMember);

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
	    HLL_EXPORT(Find, Array_Find),
	    HLL_EXPORT(Realloc, Array_Realloc),
	    HLL_EXPORT(Copy, Array_Copy),
	    HLL_TODO_EXPORT(NV_copy, Array_NV_copy),
	    HLL_TODO_EXPORT(NV_add, Array_NV_add),
	    HLL_TODO_EXPORT(NV_sub, Array_NV_sub),
	    HLL_TODO_EXPORT(NV_mul, Array_NV_mul),
	    HLL_TODO_EXPORT(NV_div, Array_NV_div),
	    HLL_TODO_EXPORT(NV_and, Array_NV_and),
	    HLL_TODO_EXPORT(NV_or, Array_NV_or),
	    HLL_TODO_EXPORT(NV_xor, Array_NV_xor),
	    HLL_TODO_EXPORT(NV_min, Array_NV_min),
	    HLL_TODO_EXPORT(NV_max, Array_NV_max),
	    HLL_TODO_EXPORT(NN_copy, Array_NN_copy),
	    HLL_TODO_EXPORT(NN_add, Array_NN_add),
	    HLL_TODO_EXPORT(NN_sub, Array_NN_sub),
	    HLL_TODO_EXPORT(NN_mul, Array_NN_mul),
	    HLL_TODO_EXPORT(NN_div, Array_NN_div),
	    HLL_TODO_EXPORT(NN_and, Array_NN_and),
	    HLL_TODO_EXPORT(NN_or, Array_NN_or),
	    HLL_TODO_EXPORT(NN_xor, Array_NN_xor),
	    HLL_TODO_EXPORT(NN_min, Array_NN_min),
	    HLL_TODO_EXPORT(NN_max, Array_NN_max),
	    HLL_TODO_EXPORT(NS_copy, Array_NS_copy),
	    HLL_TODO_EXPORT(NS_add, Array_NS_add),
	    HLL_TODO_EXPORT(NS_sub, Array_NS_sub),
	    HLL_TODO_EXPORT(NS_mul, Array_NS_mul),
	    HLL_TODO_EXPORT(NS_div, Array_NS_div),
	    HLL_TODO_EXPORT(NS_and, Array_NS_and),
	    HLL_TODO_EXPORT(NS_or, Array_NS_or),
	    HLL_TODO_EXPORT(NS_xor, Array_NS_xor),
	    HLL_TODO_EXPORT(NS_min, Array_NS_min),
	    HLL_TODO_EXPORT(NS_max, Array_NS_max),
	    HLL_TODO_EXPORT(SV_copy, Array_SV_copy),
	    HLL_TODO_EXPORT(SV_add, Array_SV_add),
	    HLL_TODO_EXPORT(SV_sub, Array_SV_sub),
	    HLL_TODO_EXPORT(SV_mul, Array_SV_mul),
	    HLL_TODO_EXPORT(SV_div, Array_SV_div),
	    HLL_TODO_EXPORT(SV_and, Array_SV_and),
	    HLL_TODO_EXPORT(SV_or, Array_SV_or),
	    HLL_TODO_EXPORT(SV_xor, Array_SV_xor),
	    HLL_TODO_EXPORT(SV_min, Array_SV_min),
	    HLL_TODO_EXPORT(SV_max, Array_SV_max),
	    HLL_TODO_EXPORT(SN_copy, Array_SN_copy),
	    HLL_TODO_EXPORT(SN_add, Array_SN_add),
	    HLL_TODO_EXPORT(SN_sub, Array_SN_sub),
	    HLL_TODO_EXPORT(SN_mul, Array_SN_mul),
	    HLL_TODO_EXPORT(SN_div, Array_SN_div),
	    HLL_TODO_EXPORT(SN_and, Array_SN_and),
	    HLL_TODO_EXPORT(SN_or, Array_SN_or),
	    HLL_TODO_EXPORT(SN_xor, Array_SN_xor),
	    HLL_TODO_EXPORT(SN_min, Array_SN_min),
	    HLL_TODO_EXPORT(SN_max, Array_SN_max),
	    HLL_TODO_EXPORT(SS_copy, Array_SS_copy),
	    HLL_TODO_EXPORT(SS_add, Array_SS_add),
	    HLL_TODO_EXPORT(SS_sub, Array_SS_sub),
	    HLL_TODO_EXPORT(SS_mul, Array_SS_mul),
	    HLL_TODO_EXPORT(SS_div, Array_SS_div),
	    HLL_TODO_EXPORT(SS_and, Array_SS_and),
	    HLL_TODO_EXPORT(SS_or, Array_SS_or),
	    HLL_TODO_EXPORT(SS_xor, Array_SS_xor),
	    HLL_TODO_EXPORT(SS_min, Array_SS_min),
	    HLL_TODO_EXPORT(SS_max, Array_SS_max),
	    HLL_EXPORT(NV_eneq, Array_NV_eneq),
	    HLL_TODO_EXPORT(NV_enne, Array_NV_enne),
	    HLL_TODO_EXPORT(NV_enlo, Array_NV_enlo),
	    HLL_TODO_EXPORT(NV_enhi, Array_NV_enhi),
	    HLL_TODO_EXPORT(NV_enra, Array_NV_enra),
	    HLL_TODO_EXPORT(NN_eneq, Array_NN_eneq),
	    HLL_TODO_EXPORT(NN_enne, Array_NN_enne),
	    HLL_TODO_EXPORT(NN_enlo, Array_NN_enlo),
	    HLL_TODO_EXPORT(NN_enhi, Array_NN_enhi),
	    HLL_TODO_EXPORT(NS_eneq, Array_NS_eneq),
	    HLL_TODO_EXPORT(NS_enne, Array_NS_enne),
	    HLL_TODO_EXPORT(NS_enlo, Array_NS_enlo),
	    HLL_TODO_EXPORT(NS_enhi, Array_NS_enhi),
	    HLL_TODO_EXPORT(SV_eneq, Array_SV_eneq),
	    HLL_TODO_EXPORT(SV_enne, Array_SV_enne),
	    HLL_TODO_EXPORT(SV_enlo, Array_SV_enlo),
	    HLL_TODO_EXPORT(SV_enhi, Array_SV_enhi),
	    HLL_TODO_EXPORT(SV_enra, Array_SV_enra),
	    HLL_TODO_EXPORT(SN_eneq, Array_SN_eneq),
	    HLL_TODO_EXPORT(SN_enne, Array_SN_enne),
	    HLL_TODO_EXPORT(SN_enlo, Array_SN_enlo),
	    HLL_TODO_EXPORT(SN_enhi, Array_SN_enhi),
	    HLL_TODO_EXPORT(SS_eneq, Array_SS_eneq),
	    HLL_TODO_EXPORT(SS_enne, Array_SS_enne),
	    HLL_TODO_EXPORT(SS_enlo, Array_SS_enlo),
	    HLL_TODO_EXPORT(SS_enhi, Array_SS_enhi),
	    HLL_TODO_EXPORT(NV_cheq, Array_NV_cheq),
	    HLL_TODO_EXPORT(NV_chne, Array_NV_chne),
	    HLL_TODO_EXPORT(NV_chlo, Array_NV_chlo),
	    HLL_TODO_EXPORT(NV_chhi, Array_NV_chhi),
	    HLL_TODO_EXPORT(NV_chra, Array_NV_chra),
	    HLL_TODO_EXPORT(NN_cheq, Array_NN_cheq),
	    HLL_TODO_EXPORT(NN_chne, Array_NN_chne),
	    HLL_TODO_EXPORT(NN_chlo, Array_NN_chlo),
	    HLL_TODO_EXPORT(NN_chhi, Array_NN_chhi),
	    HLL_TODO_EXPORT(NS_cheq, Array_NS_cheq),
	    HLL_TODO_EXPORT(NS_chne, Array_NS_chne),
	    HLL_TODO_EXPORT(NS_chlo, Array_NS_chlo),
	    HLL_TODO_EXPORT(NS_chhi, Array_NS_chhi),
	    HLL_TODO_EXPORT(SV_cheq, Array_SV_cheq),
	    HLL_TODO_EXPORT(SV_chne, Array_SV_chne),
	    HLL_TODO_EXPORT(SV_chlo, Array_SV_chlo),
	    HLL_TODO_EXPORT(SV_chhi, Array_SV_chhi),
	    HLL_TODO_EXPORT(SV_chra, Array_SV_chra),
	    HLL_TODO_EXPORT(SN_cheq, Array_SN_cheq),
	    HLL_TODO_EXPORT(SN_chne, Array_SN_chne),
	    HLL_TODO_EXPORT(SN_chlo, Array_SN_chlo),
	    HLL_TODO_EXPORT(SN_chhi, Array_SN_chhi),
	    HLL_TODO_EXPORT(SS_cheq, Array_SS_cheq),
	    HLL_TODO_EXPORT(SS_chne, Array_SS_chne),
	    HLL_TODO_EXPORT(SS_chlo, Array_SS_chlo),
	    HLL_TODO_EXPORT(SS_chhi, Array_SS_chhi),
	    HLL_TODO_EXPORT(NV_fweq, Array_NV_fweq),
	    HLL_TODO_EXPORT(NV_fwne, Array_NV_fwne),
	    HLL_TODO_EXPORT(NV_fwlo, Array_NV_fwlo),
	    HLL_TODO_EXPORT(NV_fwhi, Array_NV_fwhi),
	    HLL_TODO_EXPORT(NV_fwra, Array_NV_fwra),
	    HLL_TODO_EXPORT(NV_faeq, Array_NV_faeq),
	    HLL_TODO_EXPORT(NV_fane, Array_NV_fane),
	    HLL_TODO_EXPORT(NV_falo, Array_NV_falo),
	    HLL_TODO_EXPORT(NV_fahi, Array_NV_fahi),
	    HLL_TODO_EXPORT(NV_fara, Array_NV_fara),
	    HLL_TODO_EXPORT(NV_foeq, Array_NV_foeq),
	    HLL_TODO_EXPORT(NV_fone, Array_NV_fone),
	    HLL_TODO_EXPORT(NV_folo, Array_NV_folo),
	    HLL_TODO_EXPORT(NV_fohi, Array_NV_fohi),
	    HLL_TODO_EXPORT(NV_fora, Array_NV_fora),
	    HLL_TODO_EXPORT(NN_fweq, Array_NN_fweq),
	    HLL_TODO_EXPORT(NN_fwne, Array_NN_fwne),
	    HLL_TODO_EXPORT(NN_fwlo, Array_NN_fwlo),
	    HLL_TODO_EXPORT(NN_fwhi, Array_NN_fwhi),
	    HLL_TODO_EXPORT(NN_faeq, Array_NN_faeq),
	    HLL_TODO_EXPORT(NN_fane, Array_NN_fane),
	    HLL_TODO_EXPORT(NN_falo, Array_NN_falo),
	    HLL_TODO_EXPORT(NN_fahi, Array_NN_fahi),
	    HLL_TODO_EXPORT(NN_foeq, Array_NN_foeq),
	    HLL_TODO_EXPORT(NN_fone, Array_NN_fone),
	    HLL_TODO_EXPORT(NN_folo, Array_NN_folo),
	    HLL_TODO_EXPORT(NN_fohi, Array_NN_fohi),
	    HLL_TODO_EXPORT(NS_fweq, Array_NS_fweq),
	    HLL_TODO_EXPORT(NS_fwne, Array_NS_fwne),
	    HLL_TODO_EXPORT(NS_fwlo, Array_NS_fwlo),
	    HLL_TODO_EXPORT(NS_fwhi, Array_NS_fwhi),
	    HLL_TODO_EXPORT(NS_faeq, Array_NS_faeq),
	    HLL_TODO_EXPORT(NS_fane, Array_NS_fane),
	    HLL_TODO_EXPORT(NS_falo, Array_NS_falo),
	    HLL_TODO_EXPORT(NS_fahi, Array_NS_fahi),
	    HLL_TODO_EXPORT(NS_foeq, Array_NS_foeq),
	    HLL_TODO_EXPORT(NS_fone, Array_NS_fone),
	    HLL_TODO_EXPORT(NS_folo, Array_NS_folo),
	    HLL_TODO_EXPORT(NS_fohi, Array_NS_fohi),
	    HLL_TODO_EXPORT(SV_fweq, Array_SV_fweq),
	    HLL_TODO_EXPORT(SV_fwne, Array_SV_fwne),
	    HLL_TODO_EXPORT(SV_fwlo, Array_SV_fwlo),
	    HLL_TODO_EXPORT(SV_fwhi, Array_SV_fwhi),
	    HLL_TODO_EXPORT(SV_fwra, Array_SV_fwra),
	    HLL_TODO_EXPORT(SV_faeq, Array_SV_faeq),
	    HLL_TODO_EXPORT(SV_fane, Array_SV_fane),
	    HLL_TODO_EXPORT(SV_falo, Array_SV_falo),
	    HLL_TODO_EXPORT(SV_fahi, Array_SV_fahi),
	    HLL_TODO_EXPORT(SV_fara, Array_SV_fara),
	    HLL_TODO_EXPORT(SV_foeq, Array_SV_foeq),
	    HLL_TODO_EXPORT(SV_fone, Array_SV_fone),
	    HLL_TODO_EXPORT(SV_folo, Array_SV_folo),
	    HLL_TODO_EXPORT(SV_fohi, Array_SV_fohi),
	    HLL_TODO_EXPORT(SV_fora, Array_SV_fora),
	    HLL_TODO_EXPORT(SN_fweq, Array_SN_fweq),
	    HLL_TODO_EXPORT(SN_fwne, Array_SN_fwne),
	    HLL_TODO_EXPORT(SN_fwlo, Array_SN_fwlo),
	    HLL_TODO_EXPORT(SN_fwhi, Array_SN_fwhi),
	    HLL_TODO_EXPORT(SN_faeq, Array_SN_faeq),
	    HLL_TODO_EXPORT(SN_fane, Array_SN_fane),
	    HLL_TODO_EXPORT(SN_falo, Array_SN_falo),
	    HLL_TODO_EXPORT(SN_fahi, Array_SN_fahi),
	    HLL_TODO_EXPORT(SN_foeq, Array_SN_foeq),
	    HLL_TODO_EXPORT(SN_fone, Array_SN_fone),
	    HLL_TODO_EXPORT(SN_folo, Array_SN_folo),
	    HLL_TODO_EXPORT(SN_fohi, Array_SN_fohi),
	    HLL_TODO_EXPORT(SS_fweq, Array_SS_fweq),
	    HLL_TODO_EXPORT(SS_fwne, Array_SS_fwne),
	    HLL_TODO_EXPORT(SS_fwlo, Array_SS_fwlo),
	    HLL_TODO_EXPORT(SS_fwhi, Array_SS_fwhi),
	    HLL_TODO_EXPORT(SS_faeq, Array_SS_faeq),
	    HLL_TODO_EXPORT(SS_fane, Array_SS_fane),
	    HLL_TODO_EXPORT(SS_falo, Array_SS_falo),
	    HLL_TODO_EXPORT(SS_fahi, Array_SS_fahi),
	    HLL_TODO_EXPORT(SS_foeq, Array_SS_foeq),
	    HLL_TODO_EXPORT(SS_fone, Array_SS_fone),
	    HLL_TODO_EXPORT(SS_folo, Array_SS_folo),
	    HLL_TODO_EXPORT(SS_fohi, Array_SS_fohi),
	    HLL_EXPORT(NV_sceq, Array_NV_sceq),
	    HLL_TODO_EXPORT(NV_scne, Array_NV_scne),
	    HLL_TODO_EXPORT(NV_sclo, Array_NV_sclo),
	    HLL_TODO_EXPORT(NV_schi, Array_NV_schi),
	    HLL_TODO_EXPORT(NV_scra, Array_NV_scra),
	    HLL_TODO_EXPORT(SV_sceq, Array_SV_sceq),
	    HLL_TODO_EXPORT(SV_scne, Array_SV_scne),
	    HLL_TODO_EXPORT(SV_sclo, Array_SV_sclo),
	    HLL_TODO_EXPORT(SV_schi, Array_SV_schi),
	    HLL_TODO_EXPORT(SV_scra, Array_SV_scra),
	    HLL_TODO_EXPORT(NN_sclowest, Array_NN_sclowest),
	    HLL_TODO_EXPORT(NN_schighest, Array_NN_schighest),
	    HLL_TODO_EXPORT(NS_sclowest, Array_NS_sclowest),
	    HLL_TODO_EXPORT(NS_schighest, Array_NS_schighest),
	    HLL_TODO_EXPORT(SN_sclowest, Array_SN_sclowest),
	    HLL_TODO_EXPORT(SN_schighest, Array_SN_schighest),
	    HLL_TODO_EXPORT(SS_sclowest, Array_SS_sclowest),
	    HLL_TODO_EXPORT(SS_schighest, Array_SS_schighest),
	    HLL_TODO_EXPORT(VN_add, Array_VN_add),
	    HLL_TODO_EXPORT(VN_and, Array_VN_and),
	    HLL_TODO_EXPORT(VN_or, Array_VN_or),
	    HLL_TODO_EXPORT(VS_add, Array_VS_add),
	    HLL_TODO_EXPORT(VS_and, Array_VS_and),
	    HLL_TODO_EXPORT(VS_or, Array_VS_or)
	    );
