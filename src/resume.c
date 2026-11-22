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

// enable private VM interface
#define VM_PRIVATE

#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include "cJSON.h"

#include "system4.h"
#include "system4/file.h"
#include "system4/instructions.h"
#include "system4/little_endian.h"
#include "system4/savefile.h"
#include "system4/string.h"

#include "savedata.h"
#include "vm.h"
#include "vm/heap.h"
#include "vm/page.h"
#include "xsystem4.h"

/*
 * Save/load VM images as JSON.
 */

static const char * const page_type_strtab[] = {
	[GLOBAL_PAGE]   = "globals",
	[LOCAL_PAGE]    = "locals",
	[STRUCT_PAGE]   = "struct",
	[ARRAY_PAGE]    = "array",
	[DELEGATE_PAGE] = "delegate"
};

static enum page_type string_to_page_type(const char *str)
{
	if (!strcmp(str, "globals"))  return GLOBAL_PAGE;
	if (!strcmp(str, "locals"))   return LOCAL_PAGE;
	if (!strcmp(str, "struct"))   return STRUCT_PAGE;
	if (!strcmp(str, "array"))    return ARRAY_PAGE;
	if (!strcmp(str, "delegate")) return DELEGATE_PAGE;
	return -1;
}

static cJSON *value_to_json(union vm_value v)
{
	return cJSON_CreateNumber(v.i);
}

static int get_number(int i, void *data)
{
	return ((union vm_value*)data)[i].i;
}

static cJSON *resume_page_to_json(struct page *page)
{
	if (!page)
		return cJSON_CreateNull();

	cJSON *json = cJSON_CreateObject();
	cJSON_AddStringToObject(json, "type", page_type_strtab[page->type]);
	cJSON_AddNumberToObject(json, "subtype", page->index);
	if (page->type == ARRAY_PAGE) {
		cJSON_AddNumberToObject(json, "struct-type", page->array.struct_type);
		cJSON_AddNumberToObject(json, "rank", page->array.rank);
		if (page->array.elem_slots > 1)
			cJSON_AddNumberToObject(json, "elem-slots", page->array.elem_slots);
	}

	cJSON *values = cJSON_CreateIntArray_cb(page->nr_vars, get_number, page->values);

	cJSON_AddItemToObject(json, "values", values);
	return json;
}

static cJSON *heap_item_to_json(int i, possibly_unused void *_)
{
	if (!heap[i].ref)
		return NULL;

	cJSON *item = cJSON_CreateArray();
	cJSON_AddItemToArray(item, cJSON_CreateNumber(i));
	cJSON_AddItemToArray(item, cJSON_CreateNumber(heap[i].ref));
	switch (heap[i].type) {
	case VM_PAGE:
		cJSON_AddItemToArray(item, resume_page_to_json(heap[i].page));
		break;
	case VM_STRING:
		// a string slot without a string is written as an empty one, as
		// the rsm writer does (heap_item_to_rsave)
		cJSON_AddItemToArray(item, cJSON_CreateString(heap[i].s ? heap[i].s->text : ""));
		break;
	}
	if (ain->nr_delegates > 0) {
		cJSON_AddItemToArray(item, cJSON_CreateNumber(heap[i].seq));
	}
	return item;
}

static cJSON *heap_to_json(void)
{
	return cJSON_CreateArray_cb(heap_size, heap_item_to_json, NULL);
}

/*
 * Port-owned frame metadata for v14 images (resume_frame_extra and
 * vm_resume_frames_loaded). JSON stores it in the call-stack entries;
 * RSM stores it in an engine-reserved comment (RESUME_FRAMES_TAG).
 * Both forms carry runtime frame state in addition to the base record's
 * function, return address, local page and struct page. The loader uses
 * this metadata when restoring the port's runtime frame state.
 * The reserved RSM comment is engine data: script-facing comment
 * readers omit it, and the comment writer preserves it separately from
 * comments supplied by the script.
 */
#define CALL_STACK_FRAMES_MAX ((int)(sizeof(call_stack) / sizeof(call_stack[0])))
#define RESUME_FRAMES_TAG "xsystem4-frames 1"

static cJSON *funcall_to_json(int i)
{
	struct function_call *call = &call_stack[i];
	cJSON *json = cJSON_CreateObject();
	cJSON_AddNumberToObject(json, "function", call->fno);
	cJSON_AddNumberToObject(json, "return-address", call->return_address);
	cJSON_AddNumberToObject(json, "local-page", call->page_slot);
	cJSON_AddNumberToObject(json, "struct-page", call->struct_page);
	if (ain->version >= 14) {
		struct resume_frame_extra x;
		vm_resume_frame_extra(i, &x);
		cJSON_AddNumberToObject(json, "env-page", x.env_page);
		cJSON_AddNumberToObject(json, "delegate-obj-ref", x.delegate_obj_ref);
		cJSON_AddNumberToObject(json, "delegate-env-ref", x.delegate_env_ref);
		cJSON_AddNumberToObject(json, "delegate-page", x.dg_page);
		cJSON_AddNumberToObject(json, "delegate-cursor", x.dg_cursor);
		cJSON_AddNumberToObject(json, "delegate-limit", x.dg_limit);
		cJSON_AddNumberToObject(json, "c-method", x.c_method);
	}
	return json;
}

static cJSON *call_stack_to_json(void)
{
	cJSON *json = cJSON_CreateArray();
	for (int i = 0; i < call_stack_ptr; i++) {
		cJSON_AddItemToArray(json, funcall_to_json(i));
	}
	return json;
}

// "xsystem4-frames 1 <frames>", then for each frame
// "|<function> <env> <object ref> <env ref> <delegate> <cursor> <limit>".
static char *resume_frames_comment(void)
{
	size_t cap = 64 + (size_t)call_stack_ptr * 96, len = 0;
	char *s = xmalloc(cap);
	len += snprintf(s + len, cap - len, "%s %d", RESUME_FRAMES_TAG, call_stack_ptr);
	for (int i = 0; i < call_stack_ptr; i++) {
		struct resume_frame_extra x;
		vm_resume_frame_extra(i, &x);
		len += snprintf(s + len, cap - len, "|%d %d %d %d %d %d %d", x.fno, x.env_page,
				x.delegate_obj_ref, x.delegate_env_ref, x.dg_page, x.dg_cursor, x.dg_limit);
	}
	return s;
}

static bool is_resume_frames_comment(const char *s)
{
	return ain && ain->version >= 14 && s
		&& !strncmp(s, RESUME_FRAMES_TAG " ", strlen(RESUME_FRAMES_TAG) + 1);
}

/* Only the last comment can be the port's frame record. The writer puts
 * it there, so even an identical earlier script comment remains visible.
 * Historical files without a source marker cannot distinguish a final
 * script comment that exactly matches a valid record. */
static int resume_saved_frame_count(const struct rsave *save)
{
	if (save->comments_only)
		return 0;
	int n = 0;
	for (int i = 0; i < save->nr_call_frames; i++)
		n += save->call_frames[i].type != RSAVE_CALL_STACK_BOTTOM;
	return n;
}

static struct resume_frame_extra *resume_frames_from_comments(struct rsave *save, int nr_frames)
{
	if (save->comments_only || nr_frames <= 0 ||
	    nr_frames > CALL_STACK_FRAMES_MAX || save->nr_comments <= 0)
		return NULL;
	const char *s = save->comments[save->nr_comments - 1];
	if (!is_resume_frames_comment(s))
		return NULL;
	s += strlen(RESUME_FRAMES_TAG);
	char *end;
	long n = strtol(s, &end, 10);
	if (end == s || n != nr_frames)
		return NULL;
	struct resume_frame_extra *x = xcalloc(nr_frames, sizeof *x);
	s = end;
	for (int i = 0; i < nr_frames; i++) {
		long v[7];
		if (*s++ != '|')
			goto bad;
		for (int k = 0; k < 7; k++) {
			if (k && *s++ != ' ')
				goto bad;
			v[k] = strtol(s, &end, 10);
			if (end == s || v[k] < INT32_MIN || v[k] > INT32_MAX)
				goto bad;
			s = end;
		}
		x[i] = (struct resume_frame_extra) {
			.present = true, .fno = v[0], .env_page = v[1],
			.delegate_obj_ref = v[2], .delegate_env_ref = v[3],
			.dg_page = v[4], .dg_cursor = v[5], .dg_limit = v[6],
		};
	}
	if (*s)
		goto bad;
	return x;
bad:
	free(x);
	return NULL;
}

static bool resume_has_frame_comment(struct rsave *save)
{
	struct resume_frame_extra *x = resume_frames_from_comments(save, resume_saved_frame_count(save));
	bool found = x != NULL;
	free(x);
	return found;
}

static cJSON *stack_to_json(void)
{
	cJSON *json = cJSON_CreateArray();
	for (int i = 0; i < stack_ptr; i++) {
		cJSON_AddItemToArray(json, value_to_json(stack[i]));
	}
	return json;
}

static cJSON *vm_image_to_json(const char *key)
{
	cJSON *image = cJSON_CreateObject();
	cJSON_AddStringToObject(image, "key", key);
	cJSON_AddItemToObject(image, "heap", heap_to_json());
	cJSON_AddItemToObject(image, "call-stack", call_stack_to_json());
	cJSON_AddItemToObject(image, "stack", stack_to_json());
	cJSON_AddNumberToObject(image, "ip", instr_ptr);
	if (ain->nr_delegates > 0) {
		cJSON_AddNumberToObject(image, "next_seq", heap_next_seq);
	}
	return image;
}

/*
 * The symbol of a function in an rsm image. Functions can share a name (v14
 * overloads), and a bare name reads back as the first of them
 * (ain_get_function). A later one is written "name#n", n its place among the
 * functions of that name in function-number order, which ain_get_function
 * reads back as that function. The first keeps its bare name, so images of
 * functions that do not share a name are unchanged.
 */
static char *function_symbol(int fno)
{
	struct ain_function *f = &ain->functions[fno];
	if (!f->name)
		return strdup("");
	int n = ain_get_function_index(ain, f);
	if (n <= 0)
		return strdup(f->name);
	size_t len = strlen(f->name) + 16;
	char *sym = xmalloc(len);
	snprintf(sym, len, "%s#%d", f->name, n);
	return sym;
}

static struct rsave_heap_frame *frame_page_to_rsave(struct page *page, int slot)
{
	struct rsave_heap_frame *o = xcalloc(1, sizeof(struct rsave_heap_frame) + page->nr_vars * sizeof(int32_t));
	o->ref = heap[slot].ref;
	o->seq = heap[slot].seq;
	struct ain_variable *vars;
	if (page->type == GLOBAL_PAGE) {
		o->tag = RSAVE_GLOBALS;
		o->func.id = -1;
		vars = ain->globals;
	} else {
		if (page->index < 0 || page->index >= ain->nr_functions) {
			WARNING("frame_page_to_rsave: invalid func index %d (slot %d)", page->index, slot);
			o->tag = RSAVE_LOCALS;
			o->func.name = strdup("");
			o->nr_slots = page->nr_vars;
			for (int i = 0; i < o->nr_slots; i++)
				o->slots[i] = page->values[i].i;
			return o;
		}
		struct ain_function *f = &ain->functions[page->index];
		o->tag = RSAVE_LOCALS;
		o->func.name = function_symbol(page->index);
		vars = f->vars;
		o->struct_ptr = page->local.struct_ptr;
	}
	int nr_types = page->nr_vars;
	if (page->type == GLOBAL_PAGE && nr_types > ain->nr_globals)
		nr_types = ain->nr_globals;
	else if (page->type == LOCAL_PAGE && page->index >= 0 && page->index < ain->nr_functions
		 && nr_types > ain->functions[page->index].nr_vars)
		nr_types = ain->functions[page->index].nr_vars;
	o->nr_types = nr_types;
	o->types = xcalloc(o->nr_types, sizeof(int32_t));
	for (int i = 0; i < o->nr_types; i++)
		o->types[i] = vars[i].type.data;
	o->nr_slots = page->nr_vars;
	for (int i = 0; i < o->nr_slots; i++)
		o->slots[i] = page->values[i].i;
	return o;
}

static struct rsave_heap_struct *struct_page_to_rsave(struct page *page, int slot)
{
	struct rsave_heap_struct *o = xcalloc(1, sizeof(struct rsave_heap_struct) + page->nr_vars * sizeof(int32_t));
	o->tag = RSAVE_STRUCT;
	o->ref = heap[slot].ref;
	o->seq = heap[slot].seq;
	if (page->index < 0 || page->index >= ain->nr_structures) {
		// v14: pages with invalid struct index (e.g. wrap pages, guard pages)
		o->ctor.name = strdup("");
		o->dtor.name = strdup("");
		o->struct_type.name = strdup("");
		o->nr_types = 0;
		o->types = NULL;
		o->nr_slots = page->nr_vars;
		for (int i = 0; i < o->nr_slots; i++)
			o->slots[i] = page->values[i].i;
		return o;
	}
	struct ain_struct *s = &ain->structures[page->index];
	o->ctor.name = strdup(s->constructor >= 0 && s->constructor < ain->nr_functions
		? (ain->functions[s->constructor].name ? ain->functions[s->constructor].name : "") : "");
	o->dtor.name = strdup(s->destructor >= 0 && s->destructor < ain->nr_functions
		? (ain->functions[s->destructor].name ? ain->functions[s->destructor].name : "") : "");
	o->uk = 0;
	o->struct_type.name = strdup(s->name ? s->name : "");
	o->nr_types = s->nr_members;
	o->types = xcalloc(o->nr_types, sizeof(int32_t));
	for (int i = 0; i < o->nr_types; i++)
		o->types[i] = s->members[i].type.data;
	o->nr_slots = page->nr_vars;
	for (int i = 0; i < o->nr_slots; i++)
		o->slots[i] = page->values[i].i;
	return o;
}

#define RSAVE_ELEM_SLOTS_SHIFT 16

static struct rsave_heap_array *array_page_to_rsave(struct page *page, int slot)
{
	struct rsave_heap_array *o = xcalloc(1, sizeof(struct rsave_heap_array) + page->nr_vars * sizeof(int32_t));
	o->tag = RSAVE_ARRAY;
	o->ref = heap[slot].ref;
	o->seq = heap[slot].seq;
	o->rank_minus_1 = page->array.rank - 1;
	o->data_type = page->index;
	if (page->array.struct_type >= 0 && page->array.struct_type < ain->nr_structures)
		o->struct_type.name = strdup(ain->structures[page->array.struct_type].name ? ain->structures[page->array.struct_type].name : "");
	else
		o->struct_type.name = strdup("");
	o->root_rank = page->array.rank;  // FIXME: this is incorrect for subarrays
	// The format has no field for a v14 page's multi-slot layout, and
	// struct_type (written as a struct name) cannot carry it: 2 and 3 are
	// also struct indices. The reference counts in the image were taken
	// under that layout, so it must come back exactly. root_rank is not
	// read back by this engine (rank_minus_1 is); its upper half carries
	// elem_slots. It is 0 in every image written before, and those pages
	// come back under the older rule their counts were taken with.
	if (page->array.elem_slots > 1)
		o->root_rank |= page->array.elem_slots << RSAVE_ELEM_SLOTS_SHIFT;
	o->is_not_empty = page->nr_vars ? 1 : 0;
	o->nr_slots = page->nr_vars;
	for (int i = 0; i < o->nr_slots; i++)
		o->slots[i] = page->values[i].i;
	return o;
}

static struct rsave_heap_delegate *delegate_page_to_rsave(struct page *page, int slot)
{
	struct rsave_heap_delegate *o = xcalloc(1, sizeof(struct rsave_heap_delegate) + page->nr_vars * sizeof(int32_t));
	o->tag = RSAVE_DELEGATE;
	o->ref = heap[slot].ref;
	o->seq = heap[slot].seq;
	o->nr_slots = page->nr_vars;
	for (int i = 0; i < o->nr_slots; i++)
		o->slots[i] = page->values[i].i;
	return o;
}

static void *heap_item_to_rsave(int i)
{
	if (!heap[i].ref)
		return rsave_null;
	if (heap[i].type == VM_STRING) {
		if (!heap[i].s) {
			// Null string entry — save as empty string
			struct rsave_heap_string *s = xmalloc(sizeof(struct rsave_heap_string) + 1);
			s->tag = RSAVE_STRING;
			s->ref = heap[i].ref;
			s->seq = heap[i].seq;
			s->uk = 0;
			s->len = 1;
			s->text[0] = '\0';
			return s;
		}
		int len = heap[i].s->size + 1;
		struct rsave_heap_string *s = xmalloc(sizeof(struct rsave_heap_string) + len);
		s->tag = RSAVE_STRING;
		s->ref = heap[i].ref;
		s->seq = heap[i].seq;
		s->uk = 0;
		s->len = len;
		memcpy(s->text, heap[i].s->text, len);
		return s;
	}
	struct page *page = heap[i].page;
	if (!page) {
		// Empty array.
		struct rsave_heap_array *o = xcalloc(1, sizeof(struct rsave_heap_array));
		o->tag = RSAVE_ARRAY;
		o->ref = heap[i].ref;
		o->seq = heap[i].seq;
		o->rank_minus_1 = -1;

		// FIXME: System40.exe populates them but we don't have the type
		// information of the array here.
		o->data_type = 0;
		o->struct_type.name = strdup("");

		o->root_rank = -1;
		o->is_not_empty = 0;
		o->nr_slots = 0;
		return o;
	}
	switch (page->type) {
	case GLOBAL_PAGE:
	case LOCAL_PAGE:
		return frame_page_to_rsave(page, i);
	case STRUCT_PAGE:
		return struct_page_to_rsave(page, i);
	case ARRAY_PAGE:
		return array_page_to_rsave(page, i);
	case DELEGATE_PAGE:
		return delegate_page_to_rsave(page, i);
	default:
		ERROR("unsupported type %d", page->type);
	}
}

static void save_heap_to_rsave(struct rsave *rs)
{
	rs->nr_heap_objs = heap_size;
	rs->heap = xcalloc(heap_size, sizeof(void*));
	for (size_t i = 0; i < heap_size; i++) {
		rs->heap[i] = heap_item_to_rsave(i);
	}
}

/*
 * A frame entered from C (return address VM_RETURN, 0xFFFFFFFF) above the
 * bottom one.
 * Its return record is written with return_addr RSAVE_C_ENTRY_RETURN and
 * local_addr RSAVE_C_ENTRY_OFFSET, keeping the caller's symbol and CRC: a
 * record whose return_addr is -1 is written without them, and the reader
 * would have no function for the frame below. No return into a function
 * has a negative offset. (The port's own convention for its rsm images.)
 */
#define RSAVE_C_ENTRY_RETURN (-2)
#define RSAVE_C_ENTRY_OFFSET (-1)

// An image needs a frame to resume in and a function for every frame (the
// return records name them); otherwise the image is not written.
static bool save_call_stack_to_rsave(struct rsave *rs)
{
	if (call_stack_ptr <= 0) {
		WARNING("save_call_stack: no call frame");
		return false;
	}
	for (int i = 0; i < call_stack_ptr; i++) {
		if (call_stack[i].fno < 0 || call_stack[i].fno >= ain->nr_functions) {
			WARNING("save_call_stack: invalid fno=%d at frame %d", call_stack[i].fno, i);
			return false;
		}
	}
	rs->nr_call_frames = call_stack_ptr + 1;
	rs->call_frames = xcalloc(rs->nr_call_frames, sizeof(struct rsave_call_frame));
	rs->call_frames[0].type = RSAVE_CALL_STACK_BOTTOM;
	rs->call_frames[0].local_ptr = -1;
	rs->nr_return_records = call_stack_ptr;
	rs->return_records = xcalloc(rs->nr_return_records + 1, sizeof(struct rsave_return_record));
	rs->return_records[0].return_addr = -1;

	struct ain_function *prev_func = NULL;
	for (int i = 0; i < call_stack_ptr; i++) {
		struct function_call *call = &call_stack[i];
		struct ain_function *func = &ain->functions[call->fno];
		if (call->struct_page >= 0) {
			rs->call_frames[i + 1].type = RSAVE_METHOD_CALL;
		} else if (func->name && !strcmp(func->name, "main")) {
			rs->call_frames[i + 1].type = RSAVE_ENTRY_POINT;
		} else {
			rs->call_frames[i + 1].type = RSAVE_FUNCTION_CALL;
		}
		rs->call_frames[i + 1].local_ptr = call->page_slot;
		rs->call_frames[i + 1].struct_ptr = call->struct_page;
		if (i > 0 && prev_func && call->return_address == UINT32_MAX) {
			rs->return_records[i].return_addr = RSAVE_C_ENTRY_RETURN;
			rs->return_records[i].local_addr = RSAVE_C_ENTRY_OFFSET;
		} else if (i > 0 && prev_func) {
			rs->return_records[i].return_addr = call->return_address;
			rs->return_records[i].local_addr = call->return_address - prev_func->address;
		}
		rs->return_records[i + 1].caller_func = function_symbol(call->fno);
		rs->return_records[i + 1].crc = func->crc;
		prev_func = func;
	}
	rs->ip = rs->return_records[call_stack_ptr];
	rs->ip.return_addr = instr_ptr + 6;
	rs->ip.local_addr = instr_ptr - prev_func->address + 6;
	return true;
}

static void save_stack_to_rsave(struct rsave *rs)
{
	// For compatibility with the AliceSoft's implementation, exclude two values
	// (the SYS_RESUME_SAVE arguments) at the top of the stack.
	rs->stack_size = stack_ptr - 2;

	rs->stack = xcalloc(rs->stack_size, sizeof(int32_t));
	for (int i = 0; i < rs->stack_size; i++) {
		rs->stack[i] = stack[i].i;
	}
}

static void save_func_names_to_rsave(struct rsave *rs)
{
	rs->nr_func_names = ain->nr_functions;
	rs->func_names = xcalloc(rs->nr_func_names, sizeof(char *));
	for (int i = 0; i < ain->nr_functions; i++) {
		rs->func_names[i] = strdup(ain->functions[i].name ? ain->functions[i].name : "");
	}
}

static struct rsave *make_rsave(const char *key)
{
	struct rsave *save = xcalloc(1, sizeof(struct rsave));
	if (ain->nr_delegates > 0) {
		save->version = 9;
	} else {
		// FIXME: This is not always correct. Pastel Chime Continue is AIN v4
		//        but uses RSM v7.
		save->version = ain->version <= 4 ? 6 : 7;
	}
	save->key = strdup(key);
	return save;
}

static struct rsave *vm_image_to_rsave(const char *key)
{
	struct rsave *save = make_rsave(key);
	save->next_seq = heap_next_seq;
	save_heap_to_rsave(save);
	if (!save_call_stack_to_rsave(save)) {
		rsave_free(save);
		return NULL;
	}
	save_stack_to_rsave(save);
	save_func_names_to_rsave(save);
	if (ain->version >= 14 && save->version >= 7) {
		save->nr_comments = 1;
		save->comments = xcalloc(1, sizeof(char*));
		save->comments[0] = resume_frames_comment();
	}
	return save;
}

/*
 * An image is written to "<file>.xs4tmp" and renamed over the file once it
 * is complete: a failed write leaves the previous image whole, and a later
 * read gets it, older than the write was for (the port's own handling). Where
 * rename() does not replace a file (Windows), it is written in place as before.
 */
static char *image_write_path(const char *full_path)
{
#ifdef _WIN32
	return xstrdup(full_path);
#else
	size_t n = strlen(full_path) + 8;
	char *tmp = xmalloc(n);
	snprintf(tmp, n, "%s.xs4tmp", full_path);
	return tmp;
#endif
}

// Closes fp; with ok, puts the written file in place. True if it is there.
static bool image_write_finish(FILE *fp, bool ok, const char *tmp, const char *full_path)
{
	if (fclose(fp))
		ok = false;
#ifndef _WIN32
	if (ok && rename(tmp, full_path)) {
		WARNING("Failed to replace save file %s: %s", display_utf0(full_path), strerror(errno));
		ok = false;
	}
	if (!ok)
		remove(tmp);
#endif
	return ok;
}

static int save_rsave_image(const char *key, const char *path)
{
	struct rsave *save = vm_image_to_rsave(key);
	if (!save)
		return 0;
	char *full_path = savedir_path(path);
	char *tmp = image_write_path(full_path);
	FILE *fp = file_open_utf8(tmp, "wb");
	if (!fp) {
		WARNING("Failed to open save file %s: %s", display_utf0(tmp), strerror(errno));
		free(tmp);
		free(full_path);
		rsave_free(save);
		return 0;
	}

	bool encrypt = true;
	int compression_level = 1;
	enum savefile_error error = rsave_write(save, fp, encrypt, compression_level);
	if (error != SAVEFILE_SUCCESS)
		WARNING("Failed to write save file: %s", savefile_strerror(error));
	bool ok = image_write_finish(fp, error == SAVEFILE_SUCCESS, tmp, full_path);
	free(tmp);
	free(full_path);
	rsave_free(save);
	return ok;
}

// The text save_json writes, through image_write_path.
static int save_json_image(const char *key, const char *path)
{
	cJSON *image = vm_image_to_json(key);
	char *full_path = savedir_path(path);
	char *tmp = image_write_path(full_path);
	char *text = cJSON_Print(image);
	cJSON_Delete(image);
	bool ok = false;
	FILE *fp = file_open_utf8(tmp, "w");
	if (!fp) {
		WARNING("Failed to open save file: %s: %s", display_utf0(tmp), strerror(errno));
	} else {
		bool written = text && fwrite(text, strlen(text), 1, fp) == 1;
		if (!written)
			WARNING("Failed to write save file: %s", strerror(errno));
		ok = image_write_finish(fp, written, tmp, full_path);
	}
	free(text);
	free(tmp);
	free(full_path);
	return ok;
}

int vm_save_image(const char *key, const char *path)
{
	vm_resume_trace("save", path);
	switch (config.save_format) {
	case SAVE_FORMAT_RSM:
		return save_rsave_image(key, path);
	case SAVE_FORMAT_JSON:
		return save_json_image(key, path);
	}
	return 0;
}

/*
 * While vm_load_image reads an image, bad data ends the program through
 * image_unreadable (a message to the player first); elsewhere (the comment
 * readers and writers) it is a VM error as before.
 */
static const char *loading_image;
static bool loading_image_touched;	// the load has begun to replace the heap
static _Noreturn void image_unreadable(const char *fmt, ...);
#define _invalid_save_data(file, func, line, fmt, ...) do {				\
		if (loading_image)							\
			image_unreadable("(%s:%s:%d) " fmt, file, func, line, ##__VA_ARGS__); \
		_vm_error("*ERROR*(%s:%s:%d): " fmt "\n", file, func, line, ##__VA_ARGS__); \
	} while (0)
#define invalid_save_data(fmt, ...) \
	_invalid_save_data(__FILE__, __func__, __LINE__, fmt, ##__VA_ARGS__)

static const char *json_strtype(int type)
{
	switch (type & 0xFF) {
	case cJSON_False:
	case cJSON_True:
		return "boolean";
	case cJSON_NULL:   return "null";
	case cJSON_Number: return "number";
	case cJSON_String: return "string";
	case cJSON_Array:  return "array";
	case cJSON_Object: return "object";
	}
	return "unknown-type";
}

static cJSON *_type_check(const char *file, const char *func, int line, int type, cJSON *json)
{
	if (!json)
		_invalid_save_data(file, func, line, "Expected %s but got NULL", json_strtype(type));
	if (!(json->type & type))
		_invalid_save_data(file, func, line, "Expected %s but value is of type %s", json_strtype(type), json_strtype(json->type));
	return json;
}

#define type_check(type, json) _type_check(__FILE__, __func__, __LINE__, type, json)

static void load_json_page(int slot, cJSON *json)
{
	int struct_type = 0, rank = 0;

	// unpack
	cJSON *type    = type_check(cJSON_String, cJSON_GetObjectItem(json, "type"));
	cJSON *subtype = type_check(cJSON_Number, cJSON_GetObjectItem(json, "subtype"));
	cJSON *values  = type_check(cJSON_Array,  cJSON_GetObjectItem(json, "values"));

	enum page_type page_type = string_to_page_type(cJSON_GetStringValue(type));
	if ((int)page_type < 0)
		invalid_save_data("Invalid page type: %.64s", cJSON_GetStringValue(type));
	if (page_type == ARRAY_PAGE) {
		cJSON *_struct_type = type_check(cJSON_Number, cJSON_GetObjectItem(json, "struct-type"));
		cJSON *_rank        = type_check(cJSON_Number, cJSON_GetObjectItem(json, "rank"));
		struct_type = _struct_type->valueint;
		rank = _rank->valueint;
	}

	// allocate page
	struct page *page = alloc_page(page_type, subtype->valueint, cJSON_GetArraySize(values));
	page->array.struct_type = struct_type;
	page->array.rank = rank;
	if (page_type == ARRAY_PAGE) {
		cJSON *slots = cJSON_GetObjectItem(json, "elem-slots");
		if (cJSON_IsNumber(slots) && (slots->valueint == 2 || slots->valueint == 3)
		    && page->nr_vars % slots->valueint == 0)
			page->array.elem_slots = slots->valueint;
	}

	// init page variables
	int i = 0;
	cJSON *item;
	cJSON_ArrayForEach(item, values) {
		page->values[i].i = item->valueint;
		i++;
	}

	heap[slot].page = page;
	heap[slot].type = VM_PAGE;
}

static void load_json_string(int slot, cJSON *json)
{
	const char *str = cJSON_GetStringValue(json);
	heap[slot].s = make_string(str, strlen(str));
	heap[slot].type = VM_STRING;
}

static void delete_heap(void)
{
	// free heap
	for (size_t i = 0; i < heap_size; i++) {
		if (heap[i].ref) {
			switch (heap[i].type) {
			case VM_PAGE:
				if (heap[i].page)
					free_page(heap[i].page);
				break;
			case VM_STRING:
				free_string(heap[i].s);
				break;
			}
		}
		// Clear the type and the pointer too: the loader fills only
		// the slots the image has, and the collector frees the string
		// of any unreferenced slot of nonzero type (a page is 0 and is
		// skipped), so a string freed above would be freed again.
		heap[i].ref = 0;
		heap[i].seq = 0;
		heap[i].type = 0;
		heap[i].page = NULL;
	}

	// Clear free list — will be rebuilt after load completes
	heap_free_head = -1;
	heap_free_count = 0;
}

// Rebuild the intrusive free list by scanning all heap slots.
// Called after loading a save file to restore the free list.
static void heap_rebuild_free_list(void)
{
	heap_free_head = -1;
	heap_free_count = 0;
	for (size_t i = heap_size; i > 2; ) {
		i--;
		if (heap[i].ref == 0) {
			heap[i].seq = (uint32_t)heap_free_head;
			heap_free_head = (int32_t)i;
			heap_free_count++;
		}
	}
}

// v14 delegates do not own their objects; the object -> delegate lists
// (see delegate_page_register) describe the heap before the load, so they are
// built again once every object of the image is back. Otherwise releasing an
// object of the image would leave the entries of its delegates pointing at
// the freed slot.
static void rebuild_delegate_targets(void)
{
	delegate_targets_reset();
	for (size_t slot = 0; slot < heap_size; slot++) {
		if (heap[slot].ref && heap[slot].type == VM_PAGE && heap[slot].page
		    && heap[slot].page->type == DELEGATE_PAGE)
			delegate_page_register(slot);
	}
}

// Slot 0 is the guard page the VM starts with (vm_alloc_guard_page). An rsm
// image cannot carry it: rank 0 is written as rank_minus_1 = -1 (values too),
// and the reader takes rank_minus_1 < 0 for an empty array with no page. What
// it holds is only what stray writes left there, so after a load of either
// format it is the page of the start again, also for images of older builds.
static void restore_guard_page(void)
{
	if (heap[0].ref) {
		if (heap[0].type == VM_PAGE && heap[0].page)
			free_page(heap[0].page);
		else if (heap[0].type == VM_STRING && heap[0].s)
			free_string(heap[0].s);
	}
	heap[0].ref = 1;
	heap[0].type = VM_PAGE;
	heap[0].page = vm_alloc_guard_page();
}

// What both readers do once the whole image is in the heap. delete_heap
// emptied the free list; without it allocations after the load grow the heap,
// or collect if the heap is big, free slots are few and the cooldown is over.
static void finish_heap_load(void)
{
	heap_rebuild_free_list();
	restore_guard_page();
	rebuild_delegate_targets();
}

/*
 * The most heap slots an image may give (the port's own bound): the reader
 * grows the heap to the slot numbers it is given and clears the slot table up
 * to as much as twice the largest. An image this port writes has as many slots
 * as the heap had (heap_size, which the VM grows by doubling up to 4M slots
 * and by 4M at a time past that). The largest heap among the images the game
 * wrote with the port in its tests had 8M slots (1 << 23, builds that leaked
 * slots); images of the builds since have had at most 512K (1 << 19). The
 * VM's own bound is far higher (heap.c: HEAP_MAX_SLOTS, what the mmap
 * reservation holds, in the mmap case only), too high to keep a bad slot
 * number from taking the memory; an image of a heap past this bound is not
 * read.
 */
#define RESUME_HEAP_SLOTS_MAX (1 << 24)

// Allocate a specific heap slot for save/load.
static void alloc_heap_slot(int slot)
{
	if ((size_t)slot >= heap_size) {
		size_t next_size = heap_size * 2;
		while ((size_t)slot >= next_size)
			next_size *= 2;
		heap_grow(next_size);
	}
	heap[slot].ref = 1;
}

static void load_json_heap(cJSON *json)
{
	loading_image_touched = true;
	delete_heap();

	cJSON *item;
	cJSON_ArrayForEach(item, json) {
		type_check(cJSON_Array, item);
		if (cJSON_GetArraySize(item) < 3)
			invalid_save_data("Invalid heap data");

		int slot = type_check(cJSON_Number, cJSON_GetArrayItem(item, 0))->valueint;
		int ref  = type_check(cJSON_Number, cJSON_GetArrayItem(item, 1))->valueint;
		cJSON *value = cJSON_GetArrayItem(item, 2);
		int seq = ain->nr_delegates > 0
			? type_check(cJSON_Number, cJSON_GetArrayItem(item, 3))->valueint
			: slot;
		// alloc_heap_slot would not end on a negative slot; a slot given
		// twice would leak the first page or string
		if (slot < 0 || slot >= RESUME_HEAP_SLOTS_MAX)
			invalid_save_data("heap slot %d", slot);
		if ((size_t)slot < heap_size && heap[slot].ref)
			invalid_save_data("heap slot %d given twice", slot);

		alloc_heap_slot(slot);
		heap[slot].ref = ref;
		heap[slot].seq = seq;

		if (cJSON_IsString(value)) {
			load_json_string(slot, value);
		} else if (cJSON_IsObject(value)) {
			load_json_page(slot, value);
		} else if (cJSON_IsNull(value)) {
			heap[slot].type = VM_PAGE;
			heap[slot].page = NULL;
		} else {
			invalid_save_data("Invalid heap data");
		}
	}

	finish_heap_load();
}

// A function's symbol as function_symbol writes it ("name" or "name#n"; an
// image without "#n" reads every name as the first function of that name).
// ain_get_function cuts "#n" off the string it is given, so it gets a copy:
// the symbol in the image stays whole for messages and any further look.
static int resolve_function_name(const char *name)
{
	char *copy = strdup(name);
	int fno = ain_get_function(ain, copy);
	free(copy);
	return fno;
}

// A symbol for an error message. display_sjis0 frees its last buffer on each
// call; _vm_error prints the message, a stack trace (more names), then formats
// it again and would read a freed buffer, so the message gets a copy.
static const char *message_name(const char *name, char *buf, size_t n)
{
	if (!name)
		return "(by number)";
	snprintf(buf, n, "%s", display_sjis0(name));
	return buf;
}

/*
 * The end of a load that cannot go on: the reason goes to the log, the
 * player is told which file could not be read, and the program ends
 * (vm_image_unreadable). What the load had already put in the heap is
 * dropped first, so the end of the program does not release a half-read
 * heap. For what the original does, see vm_image_unreadable.
 */
static _Noreturn void image_unreadable(const char *fmt, ...)
{
	char reason[1024], name[512];
	va_list ap;
	va_start(ap, fmt);
	vsnprintf(reason, sizeof reason, fmt, ap);
	va_end(ap);
	WARNING("Failed to read VM image '%s': %s", message_name(loading_image, name, sizeof name), reason);
	if (loading_image_touched) {
		delete_heap();
		call_stack_ptr = 0;
		stack_ptr = 0;
	}
	vm_image_unreadable(loading_image);
}

// Whether addr lies in the code from function fno's start on. Where a
// function ends is not checked: the code of a lambda can lie inside another's.
static bool in_function_code(int fno, uint32_t addr)
{
	if (fno < 0 || fno >= ain->nr_functions)
		return false;
	uint32_t a = ain->functions[fno].address;
	return a < ain->code_size && addr < ain->code_size && addr >= a;
}

// Whether addr is, in addition, the start of an instruction (the walk of
// resume_call_opcode in vm.c).
static bool at_instruction(int fno, uint32_t addr)
{
	if (!in_function_code(fno, addr))
		return false;
	uint32_t a = ain->functions[fno].address;
	while (a < addr) {
		uint16_t op = LittleEndian_getW(ain->code, a);
		if (op >= NR_OPCODES)
			return false;
		a += instruction_width(op);
	}
	return a == addr;
}

static int resolve_func_symbol(struct rsave_symbol *sym)
{
	if (!sym->name)
		return sym->id;
	return resolve_function_name(sym->name);
}

static int resolve_struct_symbol(struct rsave_symbol *sym)
{
	if (!sym->name)
		return sym->id;
	return ain_get_struct(ain, sym->name);
}

static void load_rsave_frame(int slot, struct rsave_heap_frame *f)
{
	struct page *page;
	int nr_vars;
	struct ain_variable *vars;
	if (f->nr_slots < 0)
		invalid_save_data("page with %d slots", f->nr_slots);
	if (f->tag == RSAVE_GLOBALS) {
		page = alloc_page(GLOBAL_PAGE, 0, f->nr_slots);
		nr_vars = ain->nr_globals;
		vars = ain->globals;
	} else {
		int func = resolve_func_symbol(&f->func);
		// the checks below read the function's variables
		if (func < 0 || func >= ain->nr_functions) {
			char name[512];
			invalid_save_data("local page of function '%s': no such function",
					  message_name(f->func.name, name, sizeof name));
		}
		page = alloc_page(LOCAL_PAGE, func, f->nr_slots);
		nr_vars = ain->functions[func].nr_vars;
		vars = ain->functions[func].vars;
		page->local.struct_ptr = f->struct_ptr;
	}

	// type check
	if (nr_vars < f->nr_types) {
		invalid_save_data("call frame has too many variables");
	}
	// the VM reads every variable of the page
	if (f->nr_slots < nr_vars)
		invalid_save_data("call frame has %d slots for %d variables", f->nr_slots, nr_vars);
	for (int i = 0; i < f->nr_types; i++) {
		if (f->types[i] != vars[i].type.data) {
			invalid_save_data(
				"variable type mismatch. Expected %d, got %d",
				vars[i].type.data, f->types[i]);
		}
	}

	for (int i = 0; i < f->nr_slots; i++) {
		// TODO: update function pointers using rsave->func_names
		page->values[i].i = f->slots[i];
	}
	alloc_heap_slot(slot);
	heap[slot].ref = f->ref;
	heap[slot].seq = f->seq;
	heap[slot].type = VM_PAGE;
	heap[slot].page = page;
}

static void load_rsave_string(int slot, struct rsave_heap_string *s)
{
	// the length counts the terminating zero
	if (s->len <= 0)
		invalid_save_data("string of length %d", s->len);
	alloc_heap_slot(slot);
	heap[slot].ref = s->ref;
	heap[slot].seq = s->seq;
	heap[slot].type = VM_STRING;
	heap[slot].s = make_string(s->text, s->len - 1);
}

static void load_rsave_array(int slot, struct rsave_heap_array *a)
{
	if (a->rank_minus_1 < 0) {
		alloc_heap_slot(slot);
		heap[slot].ref = a->ref;
		heap[slot].seq = a->seq;
		heap[slot].type = VM_PAGE;
		heap[slot].page = NULL;
		return;
	}
	if (a->nr_slots < 0)
		invalid_save_data("array with %d slots", a->nr_slots);
	// the writer names only structs of the AIN (array_page_to_rsave)
	if (a->struct_type.name && a->struct_type.name[0] && resolve_struct_symbol(&a->struct_type) < 0) {
		char name[512];
		invalid_save_data("array of struct '%s': no such struct",
				  message_name(a->struct_type.name, name, sizeof name));
	}
	struct page *page = alloc_page(ARRAY_PAGE, a->data_type, a->nr_slots);
	page->array.struct_type = resolve_struct_symbol(&a->struct_type);
	page->array.rank = a->rank_minus_1 + 1;
	// see array_page_to_rsave
	int elem_slots = a->root_rank > 0 ? (a->root_rank >> RSAVE_ELEM_SLOTS_SHIFT) & 0xff : 0;
	if ((elem_slots == 2 || elem_slots == 3) && a->nr_slots % elem_slots == 0
	    && (a->data_type == AIN_ARRAY || a->data_type == AIN_REF_ARRAY))
		page->array.elem_slots = elem_slots;
	for (int i = 0; i < a->nr_slots; i++)
		page->values[i].i = a->slots[i];

	alloc_heap_slot(slot);
	heap[slot].ref = a->ref;
	heap[slot].seq = a->seq;
	heap[slot].type = VM_PAGE;
	heap[slot].page = page;
}

static void load_rsave_struct(int slot, struct rsave_heap_struct *s)
{
	// A struct page with no struct number (v14: a wrap box, see
	// variable_initval) is written with an empty struct name and no member
	// types (struct_page_to_rsave). It comes back as such a page with its
	// values; there is no struct to check it against.
	if (s->nr_slots < 0)
		invalid_save_data("struct page with %d slots", s->nr_slots);
	if (s->struct_type.name && !s->struct_type.name[0]) {
		struct page *page = alloc_page(STRUCT_PAGE, -1, s->nr_slots);
		for (int i = 0; i < s->nr_slots; i++)
			page->values[i].i = s->slots[i];
		alloc_heap_slot(slot);
		heap[slot].ref = s->ref;
		heap[slot].seq = s->seq;
		heap[slot].type = VM_PAGE;
		heap[slot].page = page;
		return;
	}
	int struct_index = resolve_struct_symbol(&s->struct_type);
	// the checks below read the struct's members
	if (struct_index < 0 || struct_index >= ain->nr_structures) {
		char name[512];
		invalid_save_data("struct '%s': no such struct", message_name(s->struct_type.name, name, sizeof name));
	}
	struct page *page = alloc_page(STRUCT_PAGE, struct_index, s->nr_slots);

	// type check
	struct ain_struct *as = &ain->structures[struct_index];
	if (as->nr_members < s->nr_types) {
		invalid_save_data("Struct %s has too many members", as->name);
	}
	// every member is read; a page without slots is what alloc_struct
	// makes when structs nest too deep, and comes back as it was
	if (s->nr_slots && s->nr_slots < as->nr_members)
		invalid_save_data("struct page has %d slots for %d members", s->nr_slots, as->nr_members);
	for (int i = 0; i < s->nr_types; i++) {
		if (s->types[i] != as->members[i].type.data) {
			invalid_save_data(
				"%s.%s: member type mismatch. Expected %d, got %d",
				as->name, as->members[i].name,
				as->members[i].type.data, s->types[i]);
		}
	}

	for (int i = 0; i < s->nr_slots; i++) {
		// TODO: update function pointers using rsave->func_names
		page->values[i].i = s->slots[i];
	}

	alloc_heap_slot(slot);
	heap[slot].ref = s->ref;
	heap[slot].seq = s->seq;
	heap[slot].type = VM_PAGE;
	heap[slot].page = page;
}

static void load_rsave_delegate(int slot, struct rsave_heap_delegate *d)
{
	if (d->nr_slots < 0)
		invalid_save_data("delegate with %d slots", d->nr_slots);
	struct page *page = alloc_page(DELEGATE_PAGE, 0, d->nr_slots);
	for (int i = 0; i < d->nr_slots; i++)
		page->values[i].i = d->slots[i];

	alloc_heap_slot(slot);
	heap[slot].ref = d->ref;
	heap[slot].seq = d->seq;
	heap[slot].type = VM_PAGE;
	heap[slot].page = page;
}

static void load_rsave_heap(struct rsave *save)
{
	loading_image_touched = true;
	delete_heap();

	for (int slot = 0; slot < save->nr_heap_objs; slot++) {
		enum rsave_heap_tag *tag = save->heap[slot];
		switch (*tag) {
		case RSAVE_GLOBALS:
		case RSAVE_LOCALS:   load_rsave_frame(slot, save->heap[slot]);    break;
		case RSAVE_STRING:   load_rsave_string(slot, save->heap[slot]);   break;
		case RSAVE_ARRAY:    load_rsave_array(slot, save->heap[slot]);    break;
		case RSAVE_STRUCT:   load_rsave_struct(slot, save->heap[slot]);   break;
		case RSAVE_DELEGATE: load_rsave_delegate(slot, save->heap[slot]); break;
		case RSAVE_NULL: break;
		}
	}
	heap_next_seq = save->next_seq;
	finish_heap_load();
}

/*
 * Once the heap and the frames are back: every frame's local page is a live
 * page with at least as many slots as the frame's function has variables (the
 * VM reads the frame's variables from it). Its page type is not checked: these
 * checks keep the VM from reading out of bounds, and an image of an earlier
 * build can hold a page of another type in that slot, which reads back as it
 * did before. A frame's struct page, and the struct pointer kept in a local
 * page, are only checked to be none (negative) or to lie in the heap: from v14
 * on a frame holds no reference to its struct page (keep_alive_enabled), so
 * the slot of an object deleted during its own method can hold something else
 * by the time of the save, and the VM skips negative slots when it releases a
 * frame.
 */
static void check_frame_pages(void)
{
	for (int i = 0; i < call_stack_ptr; i++) {
		int s = call_stack[i].page_slot;
		if (s < 0 || (size_t)s >= heap_size || !heap[s].ref || heap[s].type != VM_PAGE
		    || !heap[s].page)
			invalid_save_data("call frame %d: slot %d is not a page", i, s);
		struct page *page = heap[s].page;
		int nr_vars = ain->functions[call_stack[i].fno].nr_vars;
		if (page->nr_vars < nr_vars)
			invalid_save_data("call frame %d: page of %d slots for %d variables", i, page->nr_vars, nr_vars);
		if (page->type == LOCAL_PAGE && page->local.struct_ptr >= 0
		    && (size_t)page->local.struct_ptr >= heap_size)
			invalid_save_data("call frame %d: struct pointer %d is past the heap", i, page->local.struct_ptr);
		if (call_stack[i].struct_page >= 0 && (size_t)call_stack[i].struct_page >= heap_size)
			invalid_save_data("call frame %d: struct page %d is past the heap", i, call_stack[i].struct_page);
	}
}

// call_stack is a fixed array; an image with more frames cannot be loaded.

static void load_json_call_stack(cJSON *json)
{
	int nr_frames = cJSON_GetArraySize(json);
	if (nr_frames > CALL_STACK_FRAMES_MAX)
		invalid_save_data("too many call frames (%d, at most %d)", nr_frames, CALL_STACK_FRAMES_MAX);
	if (nr_frames < 1)
		invalid_save_data("no call frame");
	call_stack_ptr = 0;
	cJSON *item;
	cJSON_ArrayForEach(item, json) {
		type_check(cJSON_Object, item);
		if (ain->version < 14) {
			call_stack[call_stack_ptr++] = (struct function_call) {
				.fno            = type_check(cJSON_Number, cJSON_GetObjectItem(item, "function"))->valueint,
				.return_address = type_check(cJSON_Number, cJSON_GetObjectItem(item, "return-address"))->valueint,
				.page_slot      = type_check(cJSON_Number, cJSON_GetObjectItem(item, "local-page"))->valueint,
				.struct_page    = type_check(cJSON_Number, cJSON_GetObjectItem(item, "struct-page"))->valueint,
			};
			continue;
		}
		cJSON *ret = type_check(cJSON_Number, cJSON_GetObjectItem(item, "return-address"));
		/* cJSON's valueint saturates at INT_MAX. Existing JSON writers put
		 * the unsigned VM_RETURN value in this field; older files may use -1. */
		uint32_t return_address = ret->valuedouble == (double)UINT32_MAX || ret->valuedouble == -1.0
			? UINT32_MAX : (uint32_t)ret->valueint;
		call_stack[call_stack_ptr++] = (struct function_call) {
			.fno            = type_check(cJSON_Number, cJSON_GetObjectItem(item, "function"))->valueint,
			.return_address = return_address,
			.page_slot      = type_check(cJSON_Number, cJSON_GetObjectItem(item, "local-page"))->valueint,
			.struct_page    = type_check(cJSON_Number, cJSON_GetObjectItem(item, "struct-page"))->valueint,
		};
		// above the bottom a frame returns to C or into the code of the
		// function below it
		struct function_call *f = &call_stack[call_stack_ptr - 1];
		if (call_stack_ptr > 1 && f->return_address != UINT32_MAX
		    && !in_function_code(call_stack[call_stack_ptr - 2].fno, f->return_address))
			invalid_save_data("call frame %d: return address %u is not in its code",
					  call_stack_ptr - 1, f->return_address);
	}
	for (int i = 0; i < call_stack_ptr; i++) {
		if (call_stack[i].fno < 0 || call_stack[i].fno >= ain->nr_functions)
			invalid_save_data("call frame %d: no function %d", i, call_stack[i].fno);
	}
}

// The frame fields of a json image (funcall_to_json); an entry without them
// (an image of an earlier build) is not present. NULL if no entry has them.
static struct resume_frame_extra *load_json_frame_extra(cJSON *json)
{
	static const char *const keys[] = { "env-page", "delegate-obj-ref", "delegate-env-ref",
		"delegate-page", "delegate-cursor", "delegate-limit" };
	int n = cJSON_GetArraySize(json), any = 0;
	if (n <= 0)
		return NULL;
	struct resume_frame_extra *x = xcalloc(n, sizeof *x);
	for (int i = 0; i < n; i++) {
		cJSON *item = cJSON_GetArrayItem(json, i);
		int32_t v[6];
		bool ok = true;
		for (int k = 0; k < 6 && ok; k++) {
			cJSON *m = cJSON_GetObjectItem(item, keys[k]);
			ok = m && cJSON_IsNumber(m);
			v[k] = ok ? m->valueint : 0;
		}
		if (!ok)
			continue;
		cJSON *fno = cJSON_GetObjectItem(item, "function");
		x[i] = (struct resume_frame_extra) {
			.present = true, .fno = fno ? fno->valueint : -1, .env_page = v[0],
			.delegate_obj_ref = v[1], .delegate_env_ref = v[2],
			.dg_page = v[3], .dg_cursor = v[4], .dg_limit = v[5],
		};
		cJSON *method = cJSON_GetObjectItem(item, "c-method");
		if (cJSON_IsNumber(method) && (method->valuedouble == 0 || method->valuedouble == 1)) {
			x[i].c_method_present = true;
			x[i].c_method = method->valuedouble == 1;
		}
		any++;
	}
	if (!any) {
		free(x);
		return NULL;
	}
	return x;
}

static void load_rsave_call_stack(struct rsave *save)
{
	int nr_frames = resume_saved_frame_count(save);
	if (nr_frames > CALL_STACK_FRAMES_MAX)
		invalid_save_data("too many call frames (%d, at most %d)", nr_frames, CALL_STACK_FRAMES_MAX);
	call_stack_ptr = 0;

	if (save->nr_return_records == 0 || save->return_records[0].return_addr != -1)
		invalid_save_data("invalid resume save: no bottom return record");
	struct rsave_return_record *rr = save->return_records;
	if (save->nr_return_records == save->nr_call_frames)
		rr++;
	else if (save->nr_return_records != save->nr_call_frames - 1)
		invalid_save_data("invalid resume save: %d return records for %d call frames",
				  save->nr_return_records, save->nr_call_frames);

	int32_t return_address = -1;
	for (int i = 0; i < save->nr_call_frames; i++) {
		if (save->call_frames[i].type != RSAVE_CALL_STACK_BOTTOM) {
			char name[512];
			// with as many records as frames and a single frame, the
			// record after the bottom one is past the end
			if (rr != &save->ip && rr >= save->return_records + save->nr_return_records)
				invalid_save_data("call frame %d has no return record", i);
			// a record with return address -1 has no function: an image
			// of an earlier build with a frame entered from C above the
			// bottom (see RSAVE_C_ENTRY_RETURN)
			if (!rr->caller_func)
				invalid_save_data("call frame %d has no function", i);
			int fno = resolve_function_name(rr->caller_func);
			if (fno < 0)
				invalid_save_data("function '%s' not found", message_name(rr->caller_func, name, sizeof name));
			if (ain->functions[fno].crc != rr->crc)
				invalid_save_data("CRC mismatch for function '%s'", message_name(rr->caller_func, name, sizeof name));
			// the return address of the next frame lies in this function's
			// code, and the instruction pointer (rr is the ip record) is an
			// instruction of it
			bool ip = rr == &save->ip;
			bool c_entry = !ip && rr->return_addr == RSAVE_C_ENTRY_RETURN
				&& rr->local_addr == RSAVE_C_ENTRY_OFFSET;
			uint32_t at = ain->functions[fno].address + rr->local_addr - (ip ? 6 : 0);
			if (!c_entry && !(ip ? at_instruction(fno, at) : in_function_code(fno, at)))
				invalid_save_data("%s %d of function '%s' is not %s", ip ? "instruction pointer" : "return offset",
						  rr->local_addr, message_name(rr->caller_func, name, sizeof name),
						  ip ? "an instruction" : "in its code");
			call_stack[call_stack_ptr++] = (struct function_call) {
				.fno            = fno,
				.return_address = return_address,
				.page_slot      = save->call_frames[i].local_ptr,
				.struct_page    = save->call_frames[i].struct_ptr,
			};
			// Calculate return address from the function address and offset
			// to make it robust to ain changes.
			return_address = c_entry ? -1 : (int32_t)(ain->functions[fno].address + rr->local_addr);
		}
		if (++rr == save->return_records + save->nr_return_records)
			rr = &save->ip;
	}
	// an image of the bottom frame only has nothing to resume in
	if (call_stack_ptr < 1)
		invalid_save_data("no call frame");
	// RSM records instruction pointer after the CALLSYS instruction, but
	// xsystem4 expects the address of the CALLSYS instruction, so -6.
	instr_ptr = return_address - 6;
}

static void load_json_stack(cJSON *json)
{
	// the two arguments of the save are taken off below
	if (cJSON_GetArraySize(json) < 2)
		invalid_save_data("%d stack values", cJSON_GetArraySize(json));
	stack_ptr = 0;

	cJSON *item;
	cJSON_ArrayForEach(item, json) {
		type_check(cJSON_Number, item);
		stack_push_value(vm_int(item->valueint));
	}
	// Pop the arguments of SYS_RESUME_SAVE.
	//heap_unref(stack_pop().i);
	//heap_unref(stack_pop().i);
	stack_pop();
	stack_pop();
}

static void load_rsave_stack(struct rsave *save)
{
	stack_ptr = 0;

	for (int i = 0; i < save->stack_size; i++) {
		stack_push_value(vm_int(save->stack[i]));
	}
}

static cJSON *read_json_image(const char *keyname, const char *path)
{
	char *full_path = savedir_path(path);
	char *save_file = file_read(full_path, NULL);
	if (!save_file) {
		free(save_file);
		free(full_path);
		return NULL;
	}

	cJSON *save = cJSON_Parse(save_file);
	cJSON *key = type_check(cJSON_String, cJSON_GetObjectItem(save, "key"));
	if (strcmp(keyname, cJSON_GetStringValue(key)))
		invalid_save_data("Key doesn't match");

	free(save_file);
	free(full_path);
	return type_check(cJSON_Object, save);
}

static void load_json_image(const char *key, const char *path)
{
	cJSON *save = read_json_image(key, path);
	if (!save)
		invalid_save_data("the file could not be read");
	cJSON *ip = type_check(cJSON_Number, cJSON_GetObjectItem(save, "ip"));
	load_json_heap(type_check(cJSON_Array, cJSON_GetObjectItem(save, "heap")));
	cJSON *frames = type_check(cJSON_Array, cJSON_GetObjectItem(save, "call-stack"));
	load_json_call_stack(frames);
	check_frame_pages();
	if (ain->version >= 14 && !at_instruction(call_stack[call_stack_ptr - 1].fno, (uint32_t)ip->valueint))
		invalid_save_data("instruction pointer %d is not an instruction", ip->valueint);
	load_json_stack(type_check(cJSON_Array, cJSON_GetObjectItem(save, "stack")));
	if (ain->version >= 14) {
		struct resume_frame_extra *extra = load_json_frame_extra(frames);
		vm_resume_frames_loaded(extra);
		free(extra);
	}
	instr_ptr = ip->valueint;
	if (ain->nr_delegates > 0) {
		cJSON *next_seq = type_check(cJSON_Number, cJSON_GetObjectItem(save, "next_seq"));
		heap_next_seq = next_seq->valueint;
	} else {
		heap_next_seq = heap_size;
	}
	cJSON_Delete(save);
}

/*
 * rsave_read (libsys4) in its two steps, savefile_read and rsave_parse, the
 * same on good data. On bad data: an error the library would end the program
 * with (sys_error: a read past the data, a count it cannot allocate; while
 * the handler is set, from any thread, as it is the whole program's) ends it
 * through image_unreadable instead; and an image the parser gave up on part
 * way is freed only up to the first heap entry it did not read, which is
 * NULL and which rsave_free would read. (The port's own; libsys4 unchanged.)
 */
static void (*library_error_handler)(const char *msg);

static void image_library_error(const char *msg)
{
	// an error from here on ends the program as it did before
	sys_error_handler = library_error_handler;
	image_unreadable("%.*s", (int)strcspn(msg, "\n"), msg);
}

static struct rsave *read_rsave(const char *full_path, enum savefile_error *error)
{
	library_error_handler = sys_error_handler;
	sys_error_handler = image_library_error;
	struct rsave *rs = NULL;
	struct savefile *file = savefile_read(full_path, error);
	if (file) {
		rs = xcalloc(1, sizeof *rs);
		*error = rsave_parse(file->buf, file->len, RSAVE_READ_ALL, rs);
		savefile_free(file);
		if (*error != SAVEFILE_SUCCESS) {
			for (int i = 0; i < rs->nr_heap_objs; i++) {
				if (!rs->heap[i]) {
					rs->nr_heap_objs = i;
					break;
				}
			}
			rsave_free(rs);
			rs = NULL;
		}
	}
	sys_error_handler = library_error_handler;
	return rs;
}

static enum savefile_error load_rsave_image(const char *key, const char *path)
{
	char *full_path = savedir_path(path);
	enum savefile_error error;
	struct rsave *save = read_rsave(full_path, &error);
	free(full_path);
	if (error != SAVEFILE_SUCCESS)
		return error;
	if (strcmp(key, save->key))
		invalid_save_data("Key doesn't match");
	// a file the comment writer made holds comments only
	if (save->comments_only)
		invalid_save_data("the file holds no image");
	if (save->nr_heap_objs > RESUME_HEAP_SLOTS_MAX)
		invalid_save_data("%d heap slots", save->nr_heap_objs);
	load_rsave_heap(save);
	load_rsave_call_stack(save);
	check_frame_pages();
	load_rsave_stack(save);
	if (ain->version >= 14) {
		struct resume_frame_extra *extra = resume_frames_from_comments(save, call_stack_ptr);
		vm_resume_frames_loaded(extra);
		free(extra);
	}
	rsave_free(save);
	return SAVEFILE_SUCCESS;
}

/*
 * An image written by an earlier engine can hold a one-dimensional generic
 * page for a struct member declared as an array of values: init_struct_slot
 * gave an array<enum> member of an object made without its constructor such
 * a page. Its teardown and copies would take the values for heap slots. The
 * original array always carries its declared element type (0x679350) and
 * never touches the heap for int, float, bool and enum elements (0x680270 ->
 * 0x656c10), so such a page gets the page type of its declaration. Nothing
 * else changes: struct_type, layout and values stay; pages of members
 * declared with reference elements, multi-slot or multi-dimensional pages,
 * and pages no struct member names are left as they are.
 */
static void fix_value_array_members(void)
{
	if (ain->version < 14)
		return;
	for (size_t slot = 0; slot < heap_size; slot++) {
		if (!heap[slot].ref || heap[slot].type != VM_PAGE || !heap[slot].page)
			continue;
		struct page *page = heap[slot].page;
		if (page->type != STRUCT_PAGE || page->index < 0 || page->index >= ain->nr_structures)
			continue;
		for (int i = 0; i < page->nr_vars; i++) {
			const struct ain_type *t = variable_decltype(page, i);
			if (!t || t->data != AIN_ARRAY)
				continue;
			enum ain_data_type value = array_value_page_type(t->array_type);
			int a = page->values[i].i;
			if (value == AIN_VOID || a <= 0 || (size_t)a >= heap_size || !heap[a].ref
			    || heap[a].type != VM_PAGE || !heap[a].page)
				continue;
			struct page *arr = heap[a].page;
			if (arr->type == ARRAY_PAGE && arr->a_type == AIN_ARRAY && arr->array.rank == 1
			    && arr->array.elem_slots <= 1)
				arr->a_type = value;
		}
	}
}

/*
 * True once the image is back. False, with the VM as it was, for an rsm file
 * of a version libsys4 does not read: an image of another program (the
 * images of the original's release this port runs are version 14), which the
 * port can only meet in a file left from another run; as read in the script
 * of the game this port runs, going on from where the game was left and a
 * quick load go on when nothing is read back, and the quick load looks for
 * the file first. (The port's own handling: the original never meets a
 * format it did not write.) Any other image that cannot be read back, also a
 * file that is not there, ends the program through image_unreadable (for the
 * original, see vm_image_unreadable: it differs on a checksum mismatch).
 */
static bool load_image(const char *key, const char *path)
{
	loading_image = path;
	loading_image_touched = false;
	// First, try to read as a rsave.
	enum savefile_error error = load_rsave_image(key, path);
	switch (error) {
	case SAVEFILE_SUCCESS:
		loading_image = NULL;
		fix_value_array_members();
		vm_resume_trace("load", path);
		return true;
	case SAVEFILE_INVALID_SIGNATURE:
		// If not a System4 resume save file, read as a json.
		load_json_image(key, path);
		loading_image = NULL;
		fix_value_array_members();
		vm_resume_trace("load", path);
		return true;
	case SAVEFILE_UNSUPPORTED_FORMAT: {
		char name[512];
		WARNING("VM image '%s' not read: %s", message_name(path, name, sizeof name), savefile_strerror(error));
		loading_image = NULL;
		return false;
	}
	default:
		image_unreadable("%s", savefile_strerror(error));
	}
}

static struct page *load_json_image_comments(const char *key, const char *path, int *success)
{
	cJSON *save = read_json_image(key, path);
	if (!save) {
		*success = 0;
		return NULL;
	}
	cJSON *comments = cJSON_GetObjectItem(save, "comments");
	if (comments)
		type_check(cJSON_Array, comments);
	if (!comments || cJSON_GetArraySize(comments) == 0) {
		*success = 1;
		return NULL;
		//union vm_value dims = { .i = 0 };
		//return alloc_array(1, &dims, AIN_ARRAY_STRING, 0, false);
	}

	// read comments from JSON array into VM array
	union vm_value dims = { .i = cJSON_GetArraySize(comments) };
	struct page *array = alloc_array(1, &dims, AIN_ARRAY_STRING, 0, false);
	for (int i = 0; i < dims.i; i++) {
		int slot = heap_alloc_slot(VM_STRING);
		cJSON *jstr = type_check(cJSON_String, cJSON_GetArrayItem(comments, i));
		char *str = cJSON_GetStringValue(jstr);
		if (!str[0]) {
			heap[slot].s = string_ref(&EMPTY_STRING);
		} else {
			heap[slot].s = make_string(str, strlen(str));
		}
		array->values[i].i = slot;
	}

	*success = 1;
	cJSON_Delete(save);
	return array;
}

static struct page *load_rsave_image_comments(const char *key, const char *path, enum savefile_error *error)
{
	char *full_path = savedir_path(path);
	struct savefile *file = savefile_read(full_path, error);
	free(full_path);
	if (!file)
		return NULL;
	struct rsave *save = xcalloc(1, sizeof *save);
	*error = rsave_parse(file->buf, file->len, RSAVE_READ_COMMENTS, save);
	if (*error != SAVEFILE_SUCCESS) {
		savefile_free(file);
		rsave_free(save);
		return NULL;
	}
	bool has_frames = false;
	/* Read the frames from the same bytes as the comments. Ordinary
	 * comments do not require parsing the rest of the image. */
	if (save->nr_comments > 0 && is_resume_frames_comment(save->comments[save->nr_comments - 1])) {
		struct rsave *full = xcalloc(1, sizeof *full);
		if (rsave_parse(file->buf, file->len, RSAVE_READ_ALL, full) == SAVEFILE_SUCCESS)
			has_frames = resume_has_frame_comment(full);
		rsave_free(full);
	}
	savefile_free(file);
	if (strcmp(key, save->key))
		invalid_save_data("Key doesn't match");
	int nr = save->nr_comments - has_frames;
	if (nr == 0) {
		rsave_free(save);
		return NULL;
	}
	union vm_value dims = { .i = nr };
	struct page *array = alloc_array(1, &dims, AIN_ARRAY_STRING, 0, false);
	for (int i = 0; i < nr; i++) {
		if (save->comments[i][0]) {
			struct string *str = make_string(save->comments[i], strlen(save->comments[i]));
			heap_string_assign(array->values[i].i, str);
			free_string(str);
		}
	}
	rsave_free(save);
	return array;
}

struct page *vm_load_image_comments(const char *key, const char *path, int *success)
{
	// First, try to read as a rsave.
	enum savefile_error error;
	struct page *page = load_rsave_image_comments(key, path, &error);
	if (error == SAVEFILE_SUCCESS) {
		*success = 1;
		return page;
	}

	if (error == SAVEFILE_INVALID_SIGNATURE) {
		// If not a System4 resume save file, read as a json.
		return load_json_image_comments(key, path, success);
	}
	*success = 0;
	return NULL;
}

static int write_rsave_image_comments(const char *key, const char *path, struct page *comments)
{
	char *full_path = savedir_path(path);
	enum savefile_error error;
	struct rsave *save = rsave_read(full_path, RSAVE_READ_ALL, &error);
	switch (error) {
	case SAVEFILE_SUCCESS:
		if (strcmp(key, save->key))
			invalid_save_data("Key doesn't match");
		break;
	case SAVEFILE_FILE_ERROR:
		save = make_rsave(key);
		save->comments_only = true;
		break;
	default:
		free(full_path);
		return 0;
	}

	// Comments were added in RSM v7, so if the save is older, upgrade it.
	if (save->version <= 6) {
		save->version = 7;
	}

	// Preserve only a valid final frame record, after all script comments.
	bool has_frames = resume_has_frame_comment(save);
	char *frames = has_frames ? save->comments[save->nr_comments - 1] : NULL;
	for (int i = 0; i < save->nr_comments - has_frames; i++)
		free(save->comments[i]);
	free(save->comments);
	save->nr_comments = comments->nr_vars + has_frames;
	save->comments = xcalloc(ain->version >= 14 && !save->nr_comments ? 1 : save->nr_comments, sizeof(char*));
	for (int i = 0; i < comments->nr_vars; i++)
		save->comments[i] = xstrdup(heap_get_string(comments->values[i].i)->text);
	if (frames)
		save->comments[comments->nr_vars] = frames;

	FILE *fp = file_open_utf8(full_path, "wb");
	if (!fp) {
		WARNING("Failed to open save file %s: %s", display_utf0(full_path), strerror(errno));
		free(full_path);
		rsave_free(save);
		return 0;
	}
	free(full_path);

	bool encrypt = true;
	int compression_level = 1;
	error = rsave_write(save, fp, encrypt, compression_level);
	if (error != SAVEFILE_SUCCESS)
		WARNING("Failed to write save file: %s", savefile_strerror(error));
	fclose(fp);
	rsave_free(save);
	return 1;
}

static int write_json_image_comments(const char *key, const char *path, struct page *comments)
{
	cJSON *save = read_json_image(key, path);
	if (!save) {
		// create blank save
		save = cJSON_CreateObject();
		cJSON_AddStringToObject(save, "key", key);
	}
	cJSON_DeleteItemFromObject(save, "comments");

	// TODO: check that comments is an array of strings
	cJSON *array = cJSON_CreateArray();
	for (int i = 0; i < comments->nr_vars; i++) {
		cJSON *s = cJSON_CreateString(heap_get_string(comments->values[i].i)->text);
		cJSON_AddItemToArray(array, s);
	}
	cJSON_AddItemToObject(save, "comments", array);

	save_json(path, save);
	cJSON_Delete(save);
	return 1;
}

int vm_write_image_comments(const char *key, const char *path, struct page *comments)
{
	switch (config.save_format) {
	case SAVE_FORMAT_RSM:
		return write_rsave_image_comments(key, path, comments);
	case SAVE_FORMAT_JSON:
		return write_json_image_comments(key, path, comments);
	}
	return 0;
}

/*
 * load_image with copies of the key and the file name: they can be the text
 * of strings in the VM's heap (the system call SYS_RESUME_LOAD passes its
 * arguments'), which the load frees when it replaces the heap, and the log
 * and the box of a load that fails name the file.
 */
bool vm_load_image(const char *key, const char *path)
{
	char *own_key = xstrdup(key), *own_path = xstrdup(path);
	bool loaded = load_image(own_key, own_path);
	free(own_key);
	free(own_path);
	return loaded;
}
