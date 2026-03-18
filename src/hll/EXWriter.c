/* Copyright (C) 2024 <nunuhara@haniwa.technology>
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
#include <string.h>
#include <zlib.h>

#include "system4.h"
#include "system4/buffer.h"
#include "system4/ex.h"
#include "system4/file.h"
#include "system4/string.h"

#include "xsystem4.h"
#include "hll.h"

/*
 * EXWriter — in-memory EX document builder with serialization.
 *
 * Each handle owns a struct ex.  Set/Add calls create or modify blocks.
 * Save serializes to the binary EX format (HEAD+EXTF+DATA with zlib+encode).
 * Load reads an existing .ex file into the handle.
 *
 * Table building state: AddTableFormat* defines columns, then AddTable*
 * appends values left-to-right.  When a row is full (nr_values == nr_columns),
 * the next AddTable* starts a new row.
 */

#define MAX_HANDLES 64

struct exw_handle {
	int id;
	struct ex *ex;
	/* per-table build state: track which table is being built and current column */
	char *cur_table_name;
	int cur_col;
};

static struct exw_handle handles[MAX_HANDLES];
static int next_handle = 1;

static struct exw_handle *get_handle(int id)
{
	for (int i = 0; i < MAX_HANDLES; i++) {
		if (handles[i].id == id)
			return &handles[i];
	}
	return NULL;
}

static int EXWriter_CreateHandle(void)
{
	int id = next_handle++;
	for (int i = 0; i < MAX_HANDLES; i++) {
		if (handles[i].id == 0) {
			handles[i].id = id;
			handles[i].ex = xcalloc(1, sizeof(struct ex));
			handles[i].cur_table_name = NULL;
			handles[i].cur_col = 0;
			return id;
		}
	}
	WARNING("EXWriter: too many handles");
	return 0;
}

static void EXWriter_ReleaseHandle(int id)
{
	struct exw_handle *h = get_handle(id);
	if (!h) return;
	if (h->ex) ex_free(h->ex);
	free(h->cur_table_name);
	memset(h, 0, sizeof(*h));
}

/*
 * Block lookup/creation helpers
 */
static struct ex_block *find_block(struct ex *ex, const char *name)
{
	for (uint32_t i = 0; i < ex->nr_blocks; i++) {
		if (ex->blocks[i].name && !strcmp(ex->blocks[i].name->text, name))
			return &ex->blocks[i];
	}
	return NULL;
}

static struct ex_block *ensure_block(struct ex *ex, const char *name, enum ex_value_type type)
{
	struct ex_block *b = find_block(ex, name);
	if (b) return b;
	uint32_t n = ex->nr_blocks;
	ex->blocks = xrealloc_array(ex->blocks, n, n + 1, sizeof(struct ex_block));
	ex->nr_blocks = n + 1;
	b = &ex->blocks[n];
	memset(b, 0, sizeof(*b));
	b->name = cstr_to_string(name);
	b->val.type = type;
	return b;
}

/*
 * Serialization — write struct ex to binary EX format
 */
static void ex_write_pascal_string(struct buffer *b, struct string *s)
{
	buffer_write_pascal_string(b, s);
}

static void ex_write_value(struct buffer *b, struct ex_value *v);
static void ex_write_field(struct buffer *b, struct ex_field *f);
static void ex_write_table(struct buffer *b, struct ex_table *t);
static void ex_write_list(struct buffer *b, struct ex_list *l);
static void ex_write_tree(struct buffer *b, struct ex_tree *t);

static void ex_write_value(struct buffer *b, struct ex_value *v)
{
	switch (v->type) {
	case EX_INT:
		buffer_write_int32(b, v->i);
		break;
	case EX_FLOAT:
		buffer_write_float(b, v->f);
		break;
	case EX_STRING:
		ex_write_pascal_string(b, v->s ? v->s : &EMPTY_STRING);
		break;
	case EX_TABLE:
		if (v->t) ex_write_table(b, v->t);
		break;
	case EX_LIST:
		if (v->list) ex_write_list(b, v->list);
		break;
	case EX_TREE:
		if (v->tree) ex_write_tree(b, v->tree);
		break;
	}
}

static void ex_write_field(struct buffer *b, struct ex_field *f)
{
	buffer_write_int32(b, f->type);
	ex_write_pascal_string(b, f->name ? f->name : &EMPTY_STRING);
	buffer_write_int32(b, f->has_value);
	buffer_write_int32(b, f->is_index);
	if (f->has_value) {
		struct ex_value tmp = { .type = f->type };
		switch (f->type) {
		case EX_INT:    tmp.i = f->value.i; break;
		case EX_FLOAT:  tmp.f = f->value.f; break;
		case EX_STRING: tmp.s = f->value.s; break;
		default: break;
		}
		ex_write_value(b, &tmp);
	}
	if (f->type == EX_TABLE) {
		buffer_write_int32(b, f->nr_subfields);
		for (uint32_t i = 0; i < f->nr_subfields; i++) {
			ex_write_field(b, &f->subfields[i]);
		}
	}
}

static void ex_write_table(struct buffer *b, struct ex_table *t)
{
	/* fields */
	buffer_write_int32(b, t->nr_fields);
	for (uint32_t i = 0; i < t->nr_fields; i++) {
		ex_write_field(b, &t->fields[i]);
	}
	/* data: columns then rows */
	buffer_write_int32(b, t->nr_columns);
	buffer_write_int32(b, t->nr_rows);
	for (uint32_t r = 0; r < t->nr_rows; r++) {
		for (uint32_t c = 0; c < t->nr_columns; c++) {
			buffer_write_int32(b, t->rows[r][c].type);
			ex_write_value(b, &t->rows[r][c]);
		}
	}
}

static void ex_write_list(struct buffer *b, struct ex_list *l)
{
	buffer_write_int32(b, l->nr_items);
	for (uint32_t i = 0; i < l->nr_items; i++) {
		buffer_write_int32(b, l->items[i].value.type);
		/* write size placeholder, then value, then patch size */
		size_t size_pos = b->index;
		buffer_write_int32(b, 0);
		size_t data_start = b->index;
		ex_write_value(b, &l->items[i].value);
		size_t data_end = b->index;
		buffer_write_int32_at(b, size_pos, (uint32_t)(data_end - data_start));
	}
}

static void ex_write_tree(struct buffer *b, struct ex_tree *t)
{
	ex_write_pascal_string(b, t->name ? t->name : &EMPTY_STRING);
	buffer_write_int32(b, t->is_leaf ? 1 : 0);

	if (!t->is_leaf) {
		buffer_write_int32(b, t->nr_children);
		for (uint32_t i = 0; i < t->nr_children; i++) {
			ex_write_tree(b, &t->children[i]);
		}
	} else {
		buffer_write_int32(b, t->leaf.value.type);
		/* size placeholder */
		size_t size_pos = b->index;
		buffer_write_int32(b, 0);
		size_t data_start = b->index;
		ex_write_pascal_string(b, t->leaf.name ? t->leaf.name : &EMPTY_STRING);
		ex_write_value(b, &t->leaf.value);
		size_t data_end = b->index;
		buffer_write_int32_at(b, size_pos, (uint32_t)(data_end - data_start));
		buffer_write_int32(b, 0); /* zero marker */
	}
}

static void ex_write_block(struct buffer *b, struct ex_block *block)
{
	buffer_write_int32(b, block->val.type);
	/* size placeholder */
	size_t size_pos = b->index;
	buffer_write_int32(b, 0);
	size_t data_start = b->index;

	ex_write_pascal_string(b, block->name ? block->name : &EMPTY_STRING);

	switch (block->val.type) {
	case EX_INT:
		buffer_write_int32(b, block->val.i);
		break;
	case EX_FLOAT:
		buffer_write_float(b, block->val.f);
		break;
	case EX_STRING:
		ex_write_pascal_string(b, block->val.s ? block->val.s : &EMPTY_STRING);
		break;
	case EX_TABLE:
		if (block->val.t)
			ex_write_table(b, block->val.t);
		break;
	case EX_LIST:
		if (block->val.list)
			ex_write_list(b, block->val.list);
		break;
	case EX_TREE:
		if (block->val.tree)
			ex_write_tree(b, block->val.tree);
		break;
	}

	size_t data_end = b->index;
	buffer_write_int32_at(b, size_pos, (uint32_t)(data_end - data_start));
}

static bool ex_serialize_to_file(struct ex *ex, const char *path)
{
	/* Phase 1: serialize blocks to uncompressed buffer */
	struct buffer data = {0};
	for (uint32_t i = 0; i < ex->nr_blocks; i++) {
		ex_write_block(&data, &ex->blocks[i]);
	}

	/* Phase 2: zlib compress */
	unsigned long comp_bound = compressBound(data.index);
	uint8_t *compressed = xmalloc(comp_bound);
	unsigned long comp_size = comp_bound;
	int rv = compress2(compressed, &comp_size, data.buf, data.index, Z_DEFAULT_COMPRESSION);
	if (rv != Z_OK) {
		WARNING("EXWriter: compress failed: %d", rv);
		free(compressed);
		free(data.buf);
		return false;
	}

	/* Phase 3: encode compressed data */
	ex_encode(compressed, comp_size);

	/* Phase 4: build file (HEAD + EXTF + nr_blocks + DATA + compressed) */
	struct buffer file = {0};
	/* HEAD section */
	buffer_write_bytes(&file, (const uint8_t*)"HEAD", 4);
	buffer_write_int32(&file, 0);
	/* EXTF section */
	buffer_write_bytes(&file, (const uint8_t*)"EXTF", 4);
	buffer_write_int32(&file, 0);
	/* nr_blocks */
	buffer_write_int32(&file, ex->nr_blocks);
	/* DATA section */
	buffer_write_bytes(&file, (const uint8_t*)"DATA", 4);
	buffer_write_int32(&file, (uint32_t)comp_size);
	buffer_write_int32(&file, (uint32_t)data.index);
	buffer_write_bytes(&file, compressed, comp_size);

	free(compressed);
	free(data.buf);

	/* Phase 5: write to file */
	char *upath = unix_path(path);
	bool ok = file_write(upath, file.buf, file.index);
	if (!ok)
		WARNING("EXWriter: file_write('%s') failed", display_utf0(upath));
	free(upath);
	free(file.buf);
	return ok;
}

/*
 * HLL API implementations
 */
static int EXWriter_Load(int id, struct string *filename)
{
	struct exw_handle *h = get_handle(id);
	if (!h) return 0;
	char *path = unix_path(filename->text);
	struct ex *loaded = ex_read_file(path);
	if (!loaded) {
		/* try relative to game dir */
		char *full = path_join(config.game_dir, path);
		loaded = ex_read_file(full);
		free(full);
	}
	free(path);
	if (!loaded) return 0;
	if (h->ex) ex_free(h->ex);
	h->ex = loaded;
	return 1;
}

static int EXWriter_Save(int id, struct string *filename)
{
	struct exw_handle *h = get_handle(id);
	if (!h || !h->ex) return 0;
	return ex_serialize_to_file(h->ex, filename->text) ? 1 : 0;
}

static int EXWriter_LoadFromString(int id, struct string *text)
{
	/* text-format EX not supported */
	WARNING("EXWriter.LoadFromString: text format not supported");
	return 0;
}

static int EXWriter_SaveToString(int id, int page, int var)
{
	WARNING("EXWriter.SaveToString: text format not supported");
	return 0;
}

static int EXWriter_EraseDefine(int id, struct string *name)
{
	struct exw_handle *h = get_handle(id);
	if (!h || !h->ex) return 0;
	struct ex *ex = h->ex;
	for (uint32_t i = 0; i < ex->nr_blocks; i++) {
		if (ex->blocks[i].name && !strcmp(ex->blocks[i].name->text, name->text)) {
			/* shift remaining blocks */
			free_string(ex->blocks[i].name);
			/* NOTE: we don't deep-free the value here — it would require
			   type-specific cleanup.  For small save files this is fine. */
			memmove(&ex->blocks[i], &ex->blocks[i+1],
				(ex->nr_blocks - i - 1) * sizeof(struct ex_block));
			ex->nr_blocks--;
			return 1;
		}
	}
	return 0;
}

static int EXWriter_Rename(int id, struct string *old_name, struct string *new_name)
{
	struct exw_handle *h = get_handle(id);
	if (!h || !h->ex) return 0;
	struct ex_block *b = find_block(h->ex, old_name->text);
	if (!b) return 0;
	free_string(b->name);
	b->name = string_dup(new_name);
	return 1;
}

static int EXWriter_SetPartial(int id, struct string *name, int partial)
{
	/* partial flag is a hint for incremental saves — not needed for correctness */
	return 1;
}

static int EXWriter_SetInt(int id, struct string *name, int data)
{
	struct exw_handle *h = get_handle(id);
	if (!h || !h->ex) return 0;
	struct ex_block *b = ensure_block(h->ex, name->text, EX_INT);
	b->val.type = EX_INT;
	b->val.i = data;
	return 1;
}

static int EXWriter_SetFloat(int id, struct string *name, float data)
{
	struct exw_handle *h = get_handle(id);
	if (!h || !h->ex) return 0;
	struct ex_block *b = ensure_block(h->ex, name->text, EX_FLOAT);
	b->val.type = EX_FLOAT;
	b->val.f = data;
	return 1;
}

static int EXWriter_SetString(int id, struct string *name, struct string *data)
{
	struct exw_handle *h = get_handle(id);
	if (!h || !h->ex) return 0;
	struct ex_block *b = ensure_block(h->ex, name->text, EX_STRING);
	b->val.type = EX_STRING;
	if (b->val.s) free_string(b->val.s);
	b->val.s = string_dup(data);
	return 1;
}

/*
 * Array (List) operations — find-or-create a LIST block and append items
 */
static struct ex_list *ensure_list(struct ex *ex, const char *name)
{
	struct ex_block *b = ensure_block(ex, name, EX_LIST);
	if (b->val.type != EX_LIST) {
		b->val.type = EX_LIST;
		b->val.list = xcalloc(1, sizeof(struct ex_list));
	}
	if (!b->val.list)
		b->val.list = xcalloc(1, sizeof(struct ex_list));
	return b->val.list;
}

static void list_append(struct ex_list *list, struct ex_value *val, size_t val_size)
{
	uint32_t n = list->nr_items;
	list->items = xrealloc_array(list->items, n, n + 1, sizeof(struct ex_list_item));
	list->items[n].value = *val;
	list->items[n].size = val_size;
	list->nr_items = n + 1;
}

static int EXWriter_AddArrayInt(int id, struct string *name, int data)
{
	struct exw_handle *h = get_handle(id);
	if (!h || !h->ex) return 0;
	struct ex_list *list = ensure_list(h->ex, name->text);
	struct ex_value v = { .type = EX_INT, .i = data };
	list_append(list, &v, 4);
	return 1;
}

static int EXWriter_AddArrayFloat(int id, struct string *name, float data)
{
	struct exw_handle *h = get_handle(id);
	if (!h || !h->ex) return 0;
	struct ex_list *list = ensure_list(h->ex, name->text);
	struct ex_value v = { .type = EX_FLOAT, .f = data };
	list_append(list, &v, 4);
	return 1;
}

static int EXWriter_AddArrayString(int id, struct string *name, struct string *data)
{
	struct exw_handle *h = get_handle(id);
	if (!h || !h->ex) return 0;
	struct ex_list *list = ensure_list(h->ex, name->text);
	struct ex_value v = { .type = EX_STRING, .s = string_dup(data) };
	list_append(list, &v, 4 + data->size); /* pascal string: len + data */
	return 1;
}

/*
 * Table operations
 *
 * AddTableFormat* defines columns for the named table.
 * AddTable* appends values to the current row, advancing columns.
 * When nr_columns values have been added, the row is complete and
 * the next AddTable* starts a new row.
 */
static struct ex_table *ensure_table(struct ex *ex, const char *name)
{
	struct ex_block *b = ensure_block(ex, name, EX_TABLE);
	if (b->val.type != EX_TABLE) {
		b->val.type = EX_TABLE;
		b->val.t = xcalloc(1, sizeof(struct ex_table));
	}
	if (!b->val.t)
		b->val.t = xcalloc(1, sizeof(struct ex_table));
	return b->val.t;
}

static void table_add_field(struct ex_table *t, const char *fmt_name,
			    enum ex_value_type type, int is_key)
{
	uint32_t n = t->nr_fields;
	t->fields = xrealloc_array(t->fields, n, n + 1, sizeof(struct ex_field));
	memset(&t->fields[n], 0, sizeof(struct ex_field));
	t->fields[n].type = type;
	t->fields[n].name = cstr_to_string(fmt_name);
	t->fields[n].is_index = is_key;
	t->nr_fields = n + 1;
	t->nr_columns = n + 1;
}

static void table_add_value(struct exw_handle *h, const char *name, struct ex_value *val)
{
	struct ex_table *t = ensure_table(h->ex, name);

	/* track which table we're building */
	if (!h->cur_table_name || strcmp(h->cur_table_name, name)) {
		free(h->cur_table_name);
		h->cur_table_name = xstrdup(name);
		h->cur_col = 0;
	}

	/* start new row if needed */
	if (h->cur_col == 0 || (t->nr_columns > 0 && (uint32_t)h->cur_col >= t->nr_columns)) {
		uint32_t nr = t->nr_rows;
		t->rows = xrealloc_array(t->rows, nr, nr + 1, sizeof(struct ex_value*));
		uint32_t cols = t->nr_columns > 0 ? t->nr_columns : 1;
		t->rows[nr] = xcalloc(cols, sizeof(struct ex_value));
		t->nr_rows = nr + 1;
		h->cur_col = 0;
	}

	if (t->nr_rows == 0) return;
	uint32_t row = t->nr_rows - 1;
	if ((uint32_t)h->cur_col < t->nr_columns) {
		t->rows[row][h->cur_col] = *val;
	}
	h->cur_col++;
}

static int EXWriter_AddTableFormatInt(int id, struct string *name, struct string *fmt, int is_key)
{
	struct exw_handle *h = get_handle(id);
	if (!h || !h->ex) return 0;
	struct ex_table *t = ensure_table(h->ex, name->text);
	table_add_field(t, fmt->text, EX_INT, is_key);
	return 1;
}

static int EXWriter_AddTableFormatFloat(int id, struct string *name, struct string *fmt)
{
	struct exw_handle *h = get_handle(id);
	if (!h || !h->ex) return 0;
	struct ex_table *t = ensure_table(h->ex, name->text);
	table_add_field(t, fmt->text, EX_FLOAT, 0);
	return 1;
}

static int EXWriter_AddTableFormatString(int id, struct string *name, struct string *fmt, int is_key)
{
	struct exw_handle *h = get_handle(id);
	if (!h || !h->ex) return 0;
	struct ex_table *t = ensure_table(h->ex, name->text);
	table_add_field(t, fmt->text, EX_STRING, is_key);
	return 1;
}

static int EXWriter_AddTableInt(int id, struct string *name, int data)
{
	struct exw_handle *h = get_handle(id);
	if (!h || !h->ex) return 0;
	struct ex_value v = { .type = EX_INT, .i = data };
	table_add_value(h, name->text, &v);
	return 1;
}

static int EXWriter_AddTableFloat(int id, struct string *name, float data)
{
	struct exw_handle *h = get_handle(id);
	if (!h || !h->ex) return 0;
	struct ex_value v = { .type = EX_FLOAT, .f = data };
	table_add_value(h, name->text, &v);
	return 1;
}

static int EXWriter_AddTableString(int id, struct string *name, struct string *data)
{
	struct exw_handle *h = get_handle(id);
	if (!h || !h->ex) return 0;
	struct ex_value v = { .type = EX_STRING, .s = string_dup(data) };
	table_add_value(h, name->text, &v);
	return 1;
}

HLL_LIBRARY(EXWriter,
	    HLL_EXPORT(CreateHandle, EXWriter_CreateHandle),
	    HLL_EXPORT(ReleaseHandle, EXWriter_ReleaseHandle),
	    HLL_EXPORT(Load, EXWriter_Load),
	    HLL_EXPORT(Save, EXWriter_Save),
	    HLL_EXPORT(LoadFromString, EXWriter_LoadFromString),
	    HLL_EXPORT(SaveToString, EXWriter_SaveToString),
	    HLL_EXPORT(EraseDefine, EXWriter_EraseDefine),
	    HLL_EXPORT(Rename, EXWriter_Rename),
	    HLL_EXPORT(SetPartial, EXWriter_SetPartial),
	    HLL_EXPORT(SetInt, EXWriter_SetInt),
	    HLL_EXPORT(SetFloat, EXWriter_SetFloat),
	    HLL_EXPORT(SetString, EXWriter_SetString),
	    HLL_EXPORT(AddArrayInt, EXWriter_AddArrayInt),
	    HLL_EXPORT(AddArrayFloat, EXWriter_AddArrayFloat),
	    HLL_EXPORT(AddArrayString, EXWriter_AddArrayString),
	    HLL_EXPORT(AddTableFormatInt, EXWriter_AddTableFormatInt),
	    HLL_EXPORT(AddTableFormatFloat, EXWriter_AddTableFormatFloat),
	    HLL_EXPORT(AddTableFormatString, EXWriter_AddTableFormatString),
	    HLL_EXPORT(AddTableInt, EXWriter_AddTableInt),
	    HLL_EXPORT(AddTableFloat, EXWriter_AddTableFloat),
	    HLL_EXPORT(AddTableString, EXWriter_AddTableString));
