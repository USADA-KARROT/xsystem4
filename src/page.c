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

#include <stdio.h>
#include <string.h>
#ifdef __APPLE__
#include <malloc/malloc.h>
#else
#include <malloc.h>
#endif
#include "system4.h"
#include "system4/ain.h"
#include "system4/string.h"
#include "vm.h"
#include "vm/heap.h"
#include "vm/page.h"

#define NR_CACHES 32
#define CACHE_SIZE 64

static const char *pagetype_strtab[] = {
	[GLOBAL_PAGE] = "GLOBAL_PAGE",
	[LOCAL_PAGE] = "LOCAL_PAGE",
	[STRUCT_PAGE] = "STRUCT_PAGE",
	[ARRAY_PAGE] = "ARRAY_PAGE",
	[DELEGATE_PAGE] = "DELEGATE_PAGE",
};

const char *pagetype_string(enum page_type type)
{
	if (type < NR_PAGE_TYPES)
		return pagetype_strtab[type];
	return "INVALID PAGE TYPE";
}

struct page_cache {
	unsigned int cached;
	struct page *pages[CACHE_SIZE];
};

struct page_cache page_cache[NR_CACHES];

struct page *_alloc_page(int nr_vars)
{
	int cache_nr = nr_vars - 1;
	if (cache_nr >= 0 && cache_nr < NR_CACHES && page_cache[cache_nr].cached) {
		struct page *page = page_cache[cache_nr].pages[--page_cache[cache_nr].cached];
		memset(page->values, 0, sizeof(union vm_value) * nr_vars);
		return page;
	}
	return xcalloc(1, sizeof(struct page) + sizeof(union vm_value) * nr_vars);
}

void free_page(struct page *page)
{
	int cache_no = page->nr_vars - 1;
	if (cache_no < 0 || cache_no >= NR_CACHES || page_cache[cache_no].cached >= CACHE_SIZE) {
		free(page);
		return;
	}
	page_cache[cache_no].pages[page_cache[cache_no].cached++] = page;
}

struct page *alloc_page(enum page_type type, int type_index, int nr_vars)
{
	struct page *page = _alloc_page(nr_vars);
	page->type = type;
	page->index = type_index;
	page->nr_vars = nr_vars;
	return page;
}

union vm_value variable_initval(enum ain_data_type type)
{
	int slot;
	switch (type) {
	case AIN_STRING:
		slot = heap_alloc_slot(VM_STRING);
		heap[slot].s = string_ref(&EMPTY_STRING);
		return (union vm_value) { .i = slot };
	case AIN_STRUCT:
	case AIN_REF_TYPE:
	case AIN_IFACE:
		return (union vm_value) { .i = -1 };
	case AIN_ARRAY_TYPE:
	case AIN_ARRAY: // v14 generic array
	case AIN_DELEGATE:
		slot = heap_alloc_slot(VM_PAGE);
		heap_set_page(slot, NULL);
		return (union vm_value) { .i = slot };
	case AIN_WRAP: {
		// Allocate a 1-element wrap page with uninitialized inner value
		slot = heap_alloc_slot(VM_PAGE);
		struct page *wrap = alloc_page(STRUCT_PAGE, -1, 1);
		wrap->values[0].i = -1;
		heap_set_page(slot, wrap);
		return (union vm_value) { .i = slot };
	}
	case AIN_FUNC_TYPE:
	case AIN_IFACE_WRAP:
	case AIN_OPTION:
		// v14 types that need heap slots (like delegate/array)
		slot = heap_alloc_slot(VM_PAGE);
		heap_set_page(slot, NULL);
		return (union vm_value) { .i = slot };
	default: {
		static int initval_warn = 0;
		// Log unexpected types that might need heap slot allocation
		if (type != AIN_INT && type != AIN_FLOAT && type != AIN_BOOL
		    && type != AIN_LONG_INT && type != AIN_VOID
		    && type != AIN_ENUM && type != AIN_ENUM2
		    && type != AIN_HLL_PARAM && type != AIN_HLL_FUNC
		    && type != AIN_IFACE
		    && initval_warn++ < 1)
			WARNING("variable_initval: unhandled type %d → 0", type);
		return (union vm_value) { .i = 0 };
	}
	}
}

void variable_fini(union vm_value v, enum ain_data_type type, bool call_dtor)
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
		if (v.i == -1)
			break;
		if (call_dtor)
			heap_unref(v.i);
		else
			exit_unref(v.i);
		break;
	default:
		break;
	}
}

enum ain_data_type array_type(enum ain_data_type type)
{
	switch (type) {
	case AIN_ARRAY_INT:
	case AIN_REF_ARRAY_INT:
		return AIN_INT;
	case AIN_ARRAY_FLOAT:
	case AIN_REF_ARRAY_FLOAT:
		return AIN_FLOAT;
	case AIN_ARRAY_STRING:
	case AIN_REF_ARRAY_STRING:
		return AIN_STRING;
	case AIN_ARRAY_STRUCT:
	case AIN_REF_ARRAY_STRUCT:
		return AIN_STRUCT;
	case AIN_ARRAY_FUNC_TYPE:
	case AIN_REF_ARRAY_FUNC_TYPE:
		return AIN_FUNC_TYPE;
	case AIN_ARRAY_BOOL:
	case AIN_REF_ARRAY_BOOL:
		return AIN_BOOL;
	case AIN_ARRAY_LONG_INT:
	case AIN_REF_ARRAY_LONG_INT:
		return AIN_LONG_INT;
	case AIN_ARRAY_DELEGATE:
	case AIN_REF_ARRAY_DELEGATE:
		return AIN_DELEGATE;
	case AIN_ARRAY:
	case AIN_REF_ARRAY:
		// v14 generic array — element type is erased at type level.
		// Return AIN_INT as neutral fallback (no refcounting).
		return AIN_INT;
	default:
		return type;
	}
}

// v14: check if member is wrap<struct> (inheritance or contained struct).
static bool is_wrap_struct(struct ain_variable *member)
{
	return member->type.data == AIN_WRAP &&
	       member->type.array_type &&
	       member->type.array_type->data == AIN_STRUCT &&
	       member->type.struc >= 0;
}

// v14: resolve the struct that owns a given slot index, walking the
// inheritance chain.  Returns the ain_variable for the member, or NULL.
static struct ain_variable *resolve_struct_member(int struct_no, int varno)
{
	while (struct_no >= 0 && struct_no < ain->nr_structures) {
		struct ain_struct *s = &ain->structures[struct_no];
		if (varno >= 0 && varno < s->nr_members)
			return &s->members[varno];
		// Walk to base class via wrap<struct> at member[0]
		if (ain->version >= 14 && s->nr_members > 0 &&
		    s->members[0].type.data == AIN_WRAP &&
		    s->members[0].type.array_type &&
		    s->members[0].type.array_type->data == AIN_STRUCT) {
			struct_no = s->members[0].type.struc;
		} else {
			return NULL;
		}
	}
	return NULL;
}

enum ain_data_type variable_type(struct page *page, int varno, int *struct_type, int *array_rank)
{
	if (struct_type) *struct_type = -1;
	if (array_rank) *array_rank = 0;
	switch (page->type) {
	case GLOBAL_PAGE:
		if (varno < 0 || varno >= ain->nr_globals) return AIN_VOID;
		if (struct_type)
			*struct_type = ain->globals[varno].type.struc;
		if (array_rank)
			*array_rank = ain->globals[varno].type.rank;
		return ain->globals[varno].type.data;
	case LOCAL_PAGE:
		if (page->index < 0 || page->index >= ain->nr_functions) return AIN_VOID;
		if (varno < 0 || varno >= ain->functions[page->index].nr_vars) return AIN_VOID;
		if (struct_type)
			*struct_type = ain->functions[page->index].vars[varno].type.struc;
		if (array_rank)
			*array_rank = ain->functions[page->index].vars[varno].type.rank;
		return ain->functions[page->index].vars[varno].type.data;
	case STRUCT_PAGE: {
		if (page->index < 0 || page->index >= ain->nr_structures) return AIN_VOID;
		if (varno < 0) return AIN_VOID;
		struct ain_variable *m = resolve_struct_member(page->index, varno);
		if (!m) return AIN_VOID;
		if (struct_type)
			*struct_type = m->type.struc;
		if (array_rank)
			*array_rank = m->type.rank;
		return m->type.data;
	}
	case ARRAY_PAGE:
		if (struct_type)
			*struct_type = page->array.struct_type;
		if (array_rank)
			*array_rank = page->array.rank - 1;
		return page->array.rank > 1 ? page->a_type : array_type(page->a_type);
	case DELEGATE_PAGE:
		// XXX: we return void here because objects in a delegate page aren't
		//      reference counted
		return AIN_VOID;
	}
	return AIN_VOID;
}

void variable_set(struct page *page, int varno, enum ain_data_type type, union vm_value val)
{
	variable_fini(page->values[varno], type, true);
	page->values[varno] = val;
}

void delete_page_vars(struct page *page)
{
	for (int i = 0; i < page->nr_vars; i++) {
		variable_fini(page->values[i], variable_type(page, i, NULL, NULL), true);
	}
}

void delete_page(int slot)
{
	if (unlikely(slot == 0)) {
		WARNING("delete_page: BUG! attempt to delete slot 0 (global page)");
		return;
	}
	struct page *page = heap[slot].page;
	if (!page)
		return;
	// Validate page before freeing
	if (page->type >= NR_PAGE_TYPES || page->nr_vars < 0 || page->nr_vars > 1000000) {
		heap[slot].page = NULL;
		return;  // corrupted page — leak memory rather than crash
	}
	if (page->type == STRUCT_PAGE) {
		// Validate struct index before calling destructor
		if (page->index >= 0 && page->index < ain->nr_structures) {
			// Destructor needs heap[slot].page and ref > 0 for PUSHSTRUCTPAGE/X_REF.
			// heap_unref already set ref=0 before calling us. Temporarily boost ref
			// to a high value so X_REF works and accidental re-unref won't re-enter.
			heap[slot].ref = 1 << 16;
			delete_struct(page->index, slot);
			heap[slot].ref = 0;
		}
	}
	heap[slot].page = NULL;
	delete_page_vars(page);
	free_page(page);
}

/*
 * Recursively copy a page.
 */
static int copy_depth = 0;
static int copy_calls = 0;
#define COPY_MAX_DEPTH 15
#define COPY_MAX_CALLS 10000

static bool type_is_heap_ref(enum ain_data_type type)
{
	switch (type) {
	case AIN_STRUCT:
	case AIN_REF_STRUCT:
	case AIN_STRING:
	case AIN_REF_STRING:
	case AIN_DELEGATE:
	case AIN_REF_DELEGATE:
		return true;
	default:
		// Array types and other compound types
		return (type >= 50 && type < 80);
	}
}

static struct page *copy_page_shallow(struct page *src)
{
	struct page *stub = alloc_page(src->type, src->index, src->nr_vars);
	stub->array = src->array;
	for (int i = 0; i < src->nr_vars; i++) {
		enum ain_data_type type = variable_type(src, i, NULL, NULL);
		if (type_is_heap_ref(type)) {
			if (src->values[i].i > 0 && heap_index_valid(src->values[i].i)) {
				heap_ref(src->values[i].i);
				stub->values[i] = src->values[i];
			} else {
				stub->values[i].i = 0;
			}
		} else {
			stub->values[i] = src->values[i];
		}
	}
	return stub;
}

struct page *copy_page(struct page *src)
{
	if (!src)
		return NULL;
	// Validate page before copying
	if (src->type >= NR_PAGE_TYPES || src->nr_vars < 0 || src->nr_vars > 1000000) {
		static int cp_corrupt_warn = 0;
		if (cp_corrupt_warn++ < 1)
			WARNING("copy_page: corrupted page type=%d nr_vars=%d, returning empty page",
				src->type, src->nr_vars);
		return alloc_page(0, 0, 0);
	}
	bool is_top = (copy_depth == 0);
	if (is_top) copy_calls = 0;
	copy_depth++;
	copy_calls++;
	if (copy_depth > COPY_MAX_DEPTH || copy_calls > COPY_MAX_CALLS) {
		static int cp_limit_warn = 0;
		if (cp_limit_warn++ < 1)
			WARNING("copy_page: limit hit (depth=%d calls=%d), returning shallow copy", copy_depth, copy_calls);
		struct page *result = copy_page_shallow(src);
		copy_depth--;
		return result;
	}
	struct page *dst = alloc_page(src->type, src->index, src->nr_vars);
	dst->array = src->array;

	for (int i = 0; i < src->nr_vars; i++) {
		dst->values[i] = vm_copy(src->values[i], variable_type(src, i, NULL, NULL));
	}
	copy_depth--;
	return dst;
}

// Initialize a single page slot based on its type (v14).
static void init_struct_slot(struct page *page, int idx, struct ain_variable *member)
{
	if (member->type.data == AIN_STRUCT) {
		page->values[idx].i = alloc_struct(member->type.struc);
	} else if (ain->version >= 14 && is_wrap_struct(member)) {
		// v14: wrap<struct> — allocate inner struct.
		// This handles both inheritance (member[0]) and contained structs.
		page->values[idx].i = alloc_struct(member->type.struc);
	} else if (ain->version >= 14) {
		switch (member->type.data) {
		case AIN_ARRAY_TYPE:
		case AIN_ARRAY: {
			// v14: allocate empty array page (nr_vars=0) for array members.
			// Bytecode writes to elements via X_ASSIGN; stack_pop_var auto-grows.
			// For AIN_ARRAY_TYPE cases, member->type.data is already the
			// concrete container type (AIN_ARRAY_INT, AIN_ARRAY_FLOAT, etc.).
			// For generic AIN_ARRAY, try subtype or default to AIN_ARRAY.
			enum ain_data_type arr_dtype = member->type.data;
			if (arr_dtype == AIN_ARRAY && member->type.array_type) {
				switch (member->type.array_type->data) {
				case AIN_INT: case AIN_BOOL: arr_dtype = AIN_ARRAY_INT; break;
				case AIN_FLOAT: arr_dtype = AIN_ARRAY_FLOAT; break;
				case AIN_STRING: arr_dtype = AIN_ARRAY_STRING; break;
				case AIN_STRUCT: arr_dtype = AIN_ARRAY_STRUCT; break;
				default: break;
				}
			}
			union vm_value zero_dim = { .i = 0 };
			struct page *arr = alloc_array(1, &zero_dim, arr_dtype,
				member->type.struc, false);
			int arr_slot = heap_alloc_slot(VM_PAGE);
			heap_set_page(arr_slot, arr);
			page->values[idx].i = arr_slot;
			break;
		}
		case AIN_STRING:
		case AIN_DELEGATE:
		case AIN_FUNC_TYPE:
		case AIN_WRAP:
		case AIN_IFACE_WRAP:
		case AIN_OPTION:
		case AIN_REF_TYPE:
		case AIN_IFACE:
			page->values[idx].i = -1;
			break;
		default:
			page->values[idx].i = 0;
			break;
		}
	} else {
		page->values[idx] = variable_initval(member->type.data);
	}
}

int alloc_struct(int no)
{
	struct ain_struct *s = &ain->structures[no];
	bool has_inheritance = (ain->version >= 14 && s->nr_members > 0 &&
	    is_wrap_struct(&s->members[0]));
	int slot = heap_alloc_slot(VM_PAGE);
	heap_set_page(slot, alloc_page(STRUCT_PAGE, no, s->nr_members));
	// Initialize all members.
	for (int i = 0; i < s->nr_members; i++) {
		init_struct_slot(heap[slot].page, i, &s->members[i]);
	}
	return slot;
}

void init_struct(int no, int slot)
{
	if (!heap_index_valid(slot) || !heap[slot].page)
		return;
	struct ain_struct *s = &ain->structures[no];
	for (int i = 0; i < s->nr_members; i++) {
		bool is_struct = (s->members[i].type.data == AIN_STRUCT);
		bool is_wrap_s = (ain->version >= 14 && is_wrap_struct(&s->members[i]));
		if (is_struct || is_wrap_s) {
			int child = heap[slot].page->values[i].i;
			int child_type = s->members[i].type.struc;
			if (child > 0 && child_type >= 0 && child_type < ain->nr_structures) {
				init_struct(child_type, child);
			}
		}
	}
	if (s->constructor > 0 && ain->version < 14) {
		vm_call(s->constructor, slot);
	}
}

// v14: call constructors for global structs, recursing into nested members.
// alloc_struct only allocates and zero-initializes; constructors (which set up
// arrays, delegates, etc.) must be called separately.  This recurses depth-first
// so child constructors run before parent constructors — matching the order in
// which the original compiler would construct aggregates.
void init_global_struct_v14(int no, int slot)
{
	if (!heap_index_valid(slot) || !heap[slot].page)
		return;
	struct ain_struct *s = &ain->structures[no];
	// Recursively initialize nested struct members first
	for (int i = 0; i < s->nr_members; i++) {
		bool is_struct = (s->members[i].type.data == AIN_STRUCT);
		bool is_wrap_s = (is_wrap_struct(&s->members[i]));
		if (is_struct || is_wrap_s) {
			int child = heap[slot].page->values[i].i;
			int child_type = s->members[i].type.struc;
			if (child > 0 && child_type >= 0 && child_type < ain->nr_structures)
				init_global_struct_v14(child_type, child);
		}
	}
	// Then call this struct's constructor (skip known-broken debug structs)
	if (s->constructor > 0
	    && !(s->name && strstr(s->name, "CDebug"))) {
		vm_call(s->constructor, slot);
	}
}

static int destructor_depth = 0;
#define MAX_DESTRUCTOR_DEPTH 4

// Blacklist for struct types whose destructors loop/timeout
static bool *dtor_blacklist = NULL;
static bool dtor_blacklist_inited = false;

static void dtor_blacklist_init(void)
{
	if (dtor_blacklist_inited) return;
	if (!ain || ain->nr_structures <= 0) return;
	dtor_blacklist_inited = true;
	dtor_blacklist = xcalloc(ain->nr_structures, sizeof(bool));
	// Pre-blacklist known-bad destructors for v14 (sound/3D systems with uninitialized deps)
	if (ain->version >= 14) {
		for (int si = 0; si < ain->nr_structures; si++) {
			const char *name = ain->structures[si].name;
			if (name && (strcmp(name, "CASTimer") == 0 ||
				strcmp(name, "parts::detail::CParts3DLayerManager") == 0)) {
				dtor_blacklist[si] = true;
			}
		}
	}
}

void delete_struct(int no, int slot)
{
	if (no < 0 || no >= ain->nr_structures)
		return;
	struct ain_struct *s = &ain->structures[no];
	if (s->destructor > 0 && s->destructor < ain->nr_functions) {
		if (destructor_depth >= MAX_DESTRUCTOR_DEPTH) {
			return;
		}
		// Check blacklist
		if (!dtor_blacklist) dtor_blacklist_init();
		if (dtor_blacklist[no]) {
			return;
		}
		destructor_depth++;
		extern unsigned long long vm_call_get_insn_count(void);
		unsigned long long before = vm_call_get_insn_count();
		vm_call(s->destructor, slot);
		unsigned long long after = vm_call_get_insn_count();
		destructor_depth--;
		// If destructor consumed >400K instructions, it likely timed out — blacklist it
		if (after - before > 400000) {
			dtor_blacklist[no] = true;
			WARNING("delete_struct: blacklisting destructor '%s' (struct #%d) — took %llu insns",
				ain->functions[s->destructor].name, no, after - before);
		}
	}
}

void create_struct(int no, union vm_value *var)
{
	var->i = alloc_struct(no);
	init_struct(no, var->i);
}

static enum ain_data_type unref_array_type(enum ain_data_type type)
{
	switch (type) {
	case AIN_REF_ARRAY_INT:       return AIN_ARRAY_INT;
	case AIN_REF_ARRAY_FLOAT:     return AIN_ARRAY_FLOAT;
	case AIN_REF_ARRAY_STRING:    return AIN_ARRAY_STRING;
	case AIN_REF_ARRAY_STRUCT:    return AIN_ARRAY_STRUCT;
	case AIN_REF_ARRAY_FUNC_TYPE: return AIN_ARRAY_FUNC_TYPE;
	case AIN_REF_ARRAY_BOOL:      return AIN_ARRAY_BOOL;
	case AIN_REF_ARRAY_LONG_INT:  return AIN_ARRAY_LONG_INT;
	case AIN_REF_ARRAY_DELEGATE:  return AIN_ARRAY_DELEGATE;
	case AIN_ARRAY_TYPE:          return type;
	case AIN_ARRAY:               return AIN_ARRAY;
	case AIN_REF_ARRAY:           return AIN_ARRAY;
	default: VM_ERROR("Attempt to array allocate non-array type");
	}
}

struct page *alloc_array(int rank, union vm_value *dimensions, enum ain_data_type data_type, int struct_type, bool init_structs)
{
	if (rank < 1)
		return NULL;

	data_type = unref_array_type(data_type);
	enum ain_data_type type = array_type(data_type);
	struct page *page = alloc_page(ARRAY_PAGE, data_type, max(0, dimensions->i));
	page->array.struct_type = struct_type;
	page->array.rank = rank;

	for (int i = 0; i < dimensions->i; i++) {
		if (rank == 1) {
			if (type == AIN_STRUCT && init_structs)
				create_struct(struct_type, &page->values[i]);
			else
				page->values[i] = variable_initval(type);
		} else {
			struct page *child = alloc_array(rank - 1, dimensions + 1, data_type, struct_type, init_structs);
			int slot = heap_alloc_slot(VM_PAGE);
			heap_set_page(slot, child);
			page->values[i].i = slot;
		}
	}
	return page;
}

struct page *realloc_array(struct page *src, int rank, union vm_value *dimensions, enum ain_data_type data_type, int struct_type, bool init_structs)
{
	if (rank < 1)
		VM_ERROR("Tried to allocate 0-rank array");
	if (!src && !dimensions->i)
		return NULL;
	if (!src)
		return alloc_array(rank, dimensions, data_type, struct_type, init_structs);
	if (src->type != ARRAY_PAGE)
		VM_ERROR("Not an array");
	if (src->array.rank != rank) {
		// v14 NEW constructors: alloc_struct pre-creates rank=1 arrays for
		// array members, but constructors may resize with different rank.
		// Allow this by re-allocating from scratch.
		{ static int ra_warn = 0; if (ra_warn++ < 1)
			WARNING("realloc_array: rank mismatch (src=%d, new=%d), re-creating",
				src->array.rank, rank);
		}
		delete_page_vars(src);
		free_page(src);
		return alloc_array(rank, dimensions, data_type, struct_type, init_structs);
	}
	if (!dimensions->i) {
		delete_page_vars(src);
		free_page(src);
		return NULL;
	}

	// if shrinking array, unref orphaned children
	if (dimensions->i < src->nr_vars) {
		for (int i = dimensions->i; i < src->nr_vars; i++) {
			variable_fini(src->values[i], variable_type(src, i, NULL, NULL), true);
		}
	}

	src = xrealloc(src, sizeof(struct page) + sizeof(union vm_value) * dimensions->i);

	// if growing array, init new children
	enum ain_data_type type = array_type(data_type);
	if (dimensions->i > src->nr_vars) {
		for (int i = src->nr_vars; i < dimensions->i; i++) {
			if (rank == 1) {
				if (type == AIN_STRUCT && init_structs)
					create_struct(struct_type, &src->values[i]);
				else
					src->values[i] = variable_initval(type);
			} else {
				struct page *child = alloc_array(rank - 1, dimensions + 1, data_type, struct_type, init_structs);
				int slot = heap_alloc_slot(VM_PAGE);
				heap_set_page(slot, child);
				src->values[i].i = slot;
			}
		}
	}

	src->nr_vars = dimensions->i;
	return src;
}

int array_numof(struct page *page, int rank)
{
	if (!page)
		return 0;
	if (rank < 1 || rank > page->array.rank)
		return 0;
	if (rank == 1) {
		return page->nr_vars;
	}
	return array_numof(heap[page->values[0].i].page, rank - 1);
}

static bool array_index_ok(struct page *array, int i)
{
	return i >= 0 && i < array->nr_vars;
}

void array_copy(struct page *dst, int dst_i, struct page *src, int src_i, int n)
{
	if (n <= 0)
		return;
	if (!dst || !src)
		VM_ERROR("Array is NULL");
	if (dst->type != ARRAY_PAGE || src->type != ARRAY_PAGE)
		VM_ERROR("Not an array");
	if (!array_index_ok(dst, dst_i) || !array_index_ok(src, src_i))
		VM_ERROR("Out of bounds array access");
	if (!array_index_ok(dst, dst_i + n - 1) || !array_index_ok(src, src_i + n - 1))
		VM_ERROR("Out of bounds array access");
	if (dst->array.rank != 1 || src->array.rank != 1) {
		// v14 generic arrays may have rank=0; treat as rank=1
		if (ain->version >= 14) {
			if (!dst->array.rank) dst->array.rank = 1;
			if (!src->array.rank) src->array.rank = 1;
		} else {
			VM_ERROR("Tried to copy to/from a multi-dimensional array");
		}
	}
	if (dst->a_type != src->a_type && ain->version < 14)
		VM_ERROR("Array types do not match");

	for (int i = 0; i < n; i++) {
		enum ain_data_type type = array_type(dst->a_type);
		variable_set(dst, dst_i+i, type, vm_copy(src->values[src_i+i], type));
	}
}

int array_fill(struct page *dst, int dst_i, int n, union vm_value v)
{
	if (!dst)
		return 0;
	if (dst->type != ARRAY_PAGE)
		VM_ERROR("Not an array");

	// clamp (dst_i, dst_i+n) to range of array
	if (dst_i < 0) {
		n += dst_i;
		dst_i = 0;
	}
	if (dst_i >= dst->nr_vars)
		return 0;
	if (dst_i + n >= dst->nr_vars)
		n = dst->nr_vars - dst_i;

	enum ain_data_type type = array_type(dst->a_type);
	for (int i = 0; i < n; i++) {
		variable_set(dst, dst_i+i, type, vm_copy(v, type));
	}
	variable_fini(v, type, true);
	return n;
}

struct page *array_pushback(struct page *dst, union vm_value v, enum ain_data_type data_type, int struct_type)
{
	if (dst) {
		if (dst->type != ARRAY_PAGE)
			VM_ERROR("Not an array");
		if (dst->array.rank != 1)
			VM_ERROR("Tried pushing to a multi-dimensional array");

		int index = dst->nr_vars;
		size_t needed = sizeof(struct page) + sizeof(union vm_value) * (index + 1);
#ifdef __APPLE__
		size_t actual = malloc_size(dst);
#else
		size_t actual = malloc_usable_size(dst);
#endif
		if (needed <= actual) {
			// Allocation already has room — skip realloc
			dst->nr_vars = index + 1;
		} else {
			// Exponential growth to amortize realloc cost
			int grow_to = (index + 1) * 2;
			if (grow_to < 16) grow_to = 16;
			dst = xrealloc(dst, sizeof(struct page) + sizeof(union vm_value) * grow_to);
			dst->nr_vars = index + 1;
		}
		variable_set(dst, index, array_type(data_type), v);
	} else {
		union vm_value dims[1] = { (union vm_value) { .i = 1 } };
		dst = alloc_array(1, dims, data_type, struct_type, false);
		variable_set(dst, 0, array_type(data_type), v);
	}
	return dst;
}

struct page *array_popback(struct page *dst)
{
	if (!dst)
		return NULL;
	if (dst->type != ARRAY_PAGE)
		VM_ERROR("Not an array");
	if (dst->array.rank != 1)
		VM_ERROR("Tried popping from a multi-dimensional array");

	union vm_value dims[1] = { (union vm_value) { .i = dst->nr_vars - 1 } };
	dst = realloc_array(dst, 1, dims, dst->a_type, dst->array.struct_type, false);
	return dst;
}

struct page *array_erase(struct page *page, int i, bool *success)
{
	*success = false;
	if (!page)
		return NULL;
	if (page->type != ARRAY_PAGE)
		VM_ERROR("Not an array");
	if (page->array.rank != 1)
		VM_ERROR("Tried erasing from a multi-dimensional array");
	if (!array_index_ok(page, i))
		return page;

	// if array will be empty...
	if (page->nr_vars == 1) {
		delete_page_vars(page);
		free_page(page);
		*success = true;
		return NULL;
	}

	// delete variable, shift subsequent variables, then realloc page
	variable_fini(page->values[i], array_type(page->a_type), true);
	for (int j = i + 1; j < page->nr_vars; j++) {
		page->values[j-1] = page->values[j];
	}
	page->nr_vars--;
	page = xrealloc(page, sizeof(struct page) + sizeof(union vm_value) * page->nr_vars);

	*success = true;
	return page;
}

struct page *array_insert(struct page *page, int i, union vm_value v, enum ain_data_type data_type, int struct_type)
{
	if (!page) {
		return array_pushback(NULL, v, data_type, struct_type);
	}
	if (page->type != ARRAY_PAGE)
		VM_ERROR("Not an array");
	if (page->array.rank != 1)
		VM_ERROR("Tried inserting into a multi-dimensional array");

	// NOTE: you cannot insert at the end of an array due to how i is clamped
	if (i >= page->nr_vars)
		i = page->nr_vars - 1;
	if (i < 0)
		i = 0;

	page->nr_vars++;
	page = xrealloc(page, sizeof(struct page) + sizeof(union vm_value) * page->nr_vars);
	for (int j = page->nr_vars - 1; j > i; j--) {
		page->values[j] = page->values[j-1];
	}
	page->values[i] = v;
	return page;
}

static int array_compare_int(const void *_a, const void *_b)
{
	union vm_value a = *((union vm_value*)_a);
	union vm_value b = *((union vm_value*)_b);
	return (a.i > b.i) - (a.i < b.i);
}

static int array_compare_float(const void *_a, const void *_b)
{
	union vm_value a = *((union vm_value*)_a);
	union vm_value b = *((union vm_value*)_b);
	return (a.f > b.f) - (a.f < b.f);
}

static int array_compare_string(const void *_a, const void *_b)
{
	union vm_value a = *((union vm_value*)_a);
	union vm_value b = *((union vm_value*)_b);
	return strcmp(heap_get_string(a.i)->text, heap_get_string(b.i)->text);
}

// Used for stable sorting arrays with qsort()
struct sortable {
	union vm_value v;
	int index;
};

static int current_sort_function;

static int array_compare_custom(const void *_a, const void *_b)
{
	const struct sortable *a = _a;
	const struct sortable *b = _b;
	stack_push(a->v);
	stack_push(b->v);
	vm_call(current_sort_function, -1);
	int d = stack_pop().i;
	return d ? d : a->index - b->index;
}

static int array_compare_custom_string(const void *_a, const void *_b)
{
	const struct sortable *a = _a;
	const struct sortable *b = _b;
	stack_push(vm_string_ref(heap_get_string(a->v.i)));
	stack_push(vm_string_ref(heap_get_string(b->v.i)));
	vm_call(current_sort_function, -1);
	int d = stack_pop().i;
	return d ? d : a->index - b->index;
}

void array_sort(struct page *page, int compare_fno)
{
	if (!page)
		return;

	if (compare_fno) {
		struct sortable *values = xcalloc(page->nr_vars, sizeof(struct sortable));
		for (int i = 0; i < page->nr_vars; i++) {
			values[i].v = page->values[i];
			values[i].index = i;
		}
		current_sort_function = compare_fno;
		qsort(values, page->nr_vars, sizeof(struct sortable),
			page->a_type == AIN_ARRAY_STRING ? array_compare_custom_string : array_compare_custom);
		for (int i = 0; i < page->nr_vars; i++) {
			page->values[i] = values[i].v;
		}
		free(values);
	} else {
		switch (page->a_type) {
		case AIN_ARRAY_INT:
		case AIN_ARRAY_LONG_INT:
			qsort(page->values, page->nr_vars, sizeof(union vm_value), array_compare_int);
			break;
		case AIN_ARRAY_FLOAT:
			qsort(page->values, page->nr_vars, sizeof(union vm_value), array_compare_float);
			break;
		case AIN_ARRAY_STRING:
			qsort(page->values, page->nr_vars, sizeof(union vm_value), array_compare_string);
			break;
		default:
			VM_ERROR("A_SORT(&NULL) called on ain_data_type %d", page->a_type);
		}
	}
}

static int current_sort_member;

static int array_compare_member(const void *_a, const void *_b)
{
	const struct sortable *a = _a;
	const struct sortable *b = _b;
	int32_t a_i = heap_get_page(a->v.i)->values[current_sort_member].i;
	int32_t b_i = heap_get_page(b->v.i)->values[current_sort_member].i;
	int d = (a_i > b_i) - (a_i < b_i);
	return d ? d : a->index - b->index;
}

static int array_compare_member_string(const void *_a, const void *_b)
{
	const struct sortable *a = _a;
	const struct sortable *b = _b;
	int32_t a_i = heap_get_page(a->v.i)->values[current_sort_member].i;
	int32_t b_i = heap_get_page(b->v.i)->values[current_sort_member].i;
	int d = strcmp(heap_get_string(a_i)->text, heap_get_string(b_i)->text);
	return d ? d : a->index - b->index;
}

void array_sort_mem(struct page *page, int member_no)
{
	if (!page)
		return;
	if (page->type != ARRAY_PAGE || array_type(page->a_type) != AIN_STRUCT)
		VM_ERROR("A_SORT_MEM called on something other than an array of structs");

	struct ain_struct *s = &ain->structures[page->array.struct_type];
	if (member_no < 0 || member_no >= s->nr_members)
		VM_ERROR("A_SORT_MEM called with invalid member index");

	struct sortable *values = xcalloc(page->nr_vars, sizeof(struct sortable));
	for (int i = 0; i < page->nr_vars; i++) {
		values[i].v = page->values[i];
		values[i].index = i;
	}
	current_sort_member = member_no;
	if (s->members[member_no].type.data == AIN_STRING)
		qsort(values, page->nr_vars, sizeof(struct sortable), array_compare_member_string);
	else
		qsort(values, page->nr_vars, sizeof(struct sortable), array_compare_member);
	for (int i = 0; i < page->nr_vars; i++) {
		page->values[i] = values[i].v;
	}
	free(values);
}

int array_find(struct page *page, int start, int end, union vm_value v, int compare_fno)
{
	if (!page)
		return -1;

	start = max(start, 0);
	end = min(end, page->nr_vars);

	// if no compare function given, compare integer/string values
	if (!compare_fno) {
		if (array_type(page->a_type) == AIN_STRING) {
			struct string *v_str = heap_get_string(v.i);
			for (int i = start; i < end; i++) {
				if (!strcmp(v_str->text, heap_get_string(page->values[i].i)->text))
					return i;
			}
		} else {
			for (int i = start; i < end; i++) {
				if (page->values[i].i == v.i)
					return i;
			}
		}
		return -1;
	}

	for (int i = start; i < end; i++) {
		stack_push(v);
		stack_push(page->values[i]);
		vm_call(compare_fno, -1);
		if (stack_pop().i)
			return i;
	}

	return -1;
}

void array_reverse(struct page *page)
{
	if (!page)
		return;

	for (int start = 0, end = page->nr_vars-1; start < end; start++, end--) {
		union vm_value tmp = page->values[start];
		page->values[start] = page->values[end];
		page->values[end] = tmp;
	}
}

struct page *delegate_new_from_method(int obj, int fun)
{
	return delegate_new_from_method_env(obj, fun, 0);
}

struct page *delegate_new_from_method_env(int obj, int fun, int env)
{
	struct page *page = alloc_page(DELEGATE_PAGE, 0, 3);
	page->values[0].i = obj;
	page->values[1].i = fun;
	page->values[2].i = (ain->version >= 14) ? env : heap_get_seq(obj);
	return page;
}

bool delegate_contains(struct page *dst, int obj, int fun)
{
	if (!dst)
		return false;
	for (int i = 0; i < dst->nr_vars; i += 3) {
		if (dst->values[i].i == obj &&
		    dst->values[i+1].i == fun &&
		    (ain->version >= 14 || dst->values[i+2].i == heap_get_seq(obj)))
			return true;
	}
	return false;
}

struct page *delegate_append(struct page *dst, int obj, int fun)
{
	if (!dst)
		return delegate_new_from_method(obj, fun);
	if (dst->type != DELEGATE_PAGE) {
		WARNING("delegate_append: not a delegate (type=%d), creating new", dst->type);
		return delegate_new_from_method(obj, fun);
	}
	if (delegate_contains(dst, obj, fun))
		return dst;

	dst = xrealloc(dst, sizeof(struct page) + sizeof(union vm_value) * (dst->nr_vars + 3));
	dst->values[dst->nr_vars+0].i = obj;
	dst->values[dst->nr_vars+1].i = fun;
	dst->values[dst->nr_vars+2].i = heap_get_seq(obj);
	dst->nr_vars += 3;
	return dst;
}

int delegate_numof(struct page *page)
{
	if (!page)
		return 0;
	if (page->type != DELEGATE_PAGE) {
		// v14: uninitialized delegate fields (slot=0) point to the
		// global page.  Treat non-delegate pages as empty delegates.
		return 0;
	}

	// v14 uses different object lifecycle management; the seq-based GC
	// incorrectly removes live handlers whose objects have ref=0 temporarily.
	if (ain->version >= 14)
		return page->nr_vars / 3;

	// garbage collection (pre-v14)
	for (int i = 0; i < page->nr_vars; i += 3) {
		if (heap_get_seq(page->values[i].i) != page->values[i+2].i) {
			for (int j = i+3; j < page->nr_vars; j += 3) {
				page->values[j-3].i = page->values[j+0].i;
				page->values[j-2].i = page->values[j+1].i;
				page->values[j-1].i = page->values[j+2].i;
			}
			page->nr_vars -= 3;
			i -= 3;
		}
	}
	return page->nr_vars / 3;
}

void delegate_erase(struct page *page, int obj, int fun)
{
	if (!page)
		return;
	if (page->type != DELEGATE_PAGE) {
		WARNING("delegate_erase: not a delegate (type=%d)", page->type);
		return;
	}
	for (int i = 0; i < page->nr_vars; i += 3) {
		if (page->values[i].i == obj && page->values[i+1].i == fun) {
			for (int j = i+3; j < page->nr_vars; j += 3) {
				page->values[j-3].i = page->values[j+0].i;
				page->values[j-2].i = page->values[j+1].i;
				page->values[j-1].i = page->values[j+2].i;
			}
			page->nr_vars -= 3;
			break;
		}
	}
}

struct page *delegate_plusa(struct page *dst, struct page *add)
{
	if (!add)
		return dst;
	if ((dst && dst->type != DELEGATE_PAGE) || add->type != DELEGATE_PAGE) {
		WARNING("delegate_plusa: not a delegate");
		return dst;
	}

	for (int i = 0; i < add->nr_vars; i += 3) {
		if (ain->version >= 14 || heap_get_seq(add->values[i].i) == add->values[i+2].i)
			dst = delegate_append(dst, add->values[i].i, add->values[i+1].i);
	}
	return dst;
}

struct page *delegate_minusa(struct page *dst, struct page *minus)
{
	if (!dst)
		return NULL;
	if (!minus)
		return dst;
	if (dst->type != DELEGATE_PAGE || minus->type != DELEGATE_PAGE) {
		WARNING("delegate_minusa: not a delegate");
		return dst;
	}

	for (int i = 0; i < minus->nr_vars; i += 3) {
		if (ain->version >= 14 || heap_get_seq(minus->values[i].i) == minus->values[i+2].i)
			delegate_erase(dst, minus->values[i].i, minus->values[i+1].i);
	}

	return dst;
}

struct page *delegate_clear(struct page *page)
{
	if (!page)
		return NULL;
	if (page->type != DELEGATE_PAGE) {
		WARNING("delegate_clear: not a delegate (type=%d nr_vars=%d), returning NULL",
			page->type, page->nr_vars);
		return NULL;
	}
	for (int i = 0; i < page->nr_vars; i += 3) {
		page->values[i].i = -1;
		page->values[i+1].i = -1;
		page->values[i+2].i = 0;
	}
	page->index = 0;
	page->nr_vars = 0;
	return page;
}

bool delegate_get(struct page *page, int i, int *obj_out, int *fun_out)
{
	if (!page)
		return false;
	if (page->type != DELEGATE_PAGE) {
		WARNING("delegate_get: not a delegate (type=%d)", page->type);
		return false;
	}
	if (ain->version >= 14) {
		// v14: skip seq-based GC, just return the i-th handler directly
		if (i*3 < page->nr_vars) {
			*obj_out = page->values[i*3].i;
			*fun_out = page->values[i*3+1].i;
			return true;
		}
		return false;
	}
	while (i*3 < page->nr_vars) {
		if (heap_get_seq(page->values[i*3].i) == page->values[i*3+2].i) {
			*obj_out = page->values[i*3].i;
			*fun_out = page->values[i*3+1].i;
			return true;
		}
		for (int j = (i + 1) * 3; j < page->nr_vars; j += 3) {
			page->values[j-3].i = page->values[j+0].i;
			page->values[j-2].i = page->values[j+1].i;
			page->values[j-1].i = page->values[j+2].i;
		}
		page->nr_vars -= 3;
	}
	return false;
}
