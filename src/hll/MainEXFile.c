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

/*
 * v14 MainEXFile — name-based API (replaces handle-based API for AIN v14+)
 *
 * v14 differences from older versions:
 *  - All functions take (string Name, ..., int ID) instead of (int handle, ...)
 *  - Data functions (Int/Float/String) return values directly with defaults
 *  - Handle/AHandle/A2Handle/IA2Handle/SA2Handle/RA2Handle removed
 *  - New: AddEXReader/EraseEXReader/AddEX/AddEXText/Save/Load
 *  - New: GetNodeNameList/GetEXNameList/GetFormatNameList
 */

#define VM_PRIVATE

#include <string.h>
#include <iconv.h>

#include "system4/ex.h"
#include "system4/file.h"
#include "system4/string.h"
#include "system4/ain.h"

#include "vm.h"
#include "vm/heap.h"
#include "vm/page.h"
#include "xsystem4.h"
#include "hll.h"

/*
 * SJIS→GBK string converter for .ex file loading.
 * The .ex file stores block/field names in SJIS, but the CN version's AIN
 * bytecode uses GBK-encoded string constants.  Converting at load time makes
 * name lookups match.
 */
struct string *sjis_to_gbk_string(const char *src, size_t len)
{
	static iconv_t cd = (iconv_t)-1;
	if (cd == (iconv_t)-1) {
		cd = iconv_open("GBK", "SHIFT_JIS");
		if (cd == (iconv_t)-1)
			return make_string(src, len);
	}
	size_t outlen = len * 2 + 1;
	char *out = xmalloc(outlen);
	char *inp = (char *)src;
	char *outp = out;
	size_t inleft = len, outleft = outlen - 1;
	iconv(cd, NULL, NULL, NULL, NULL); /* reset */
	size_t ret = iconv(cd, &inp, &inleft, &outp, &outleft);
	if (ret == (size_t)-1 || inleft > 0) {
		/* Conversion failed — keep original bytes */
		free(out);
		return make_string(src, len);
	}
	size_t result_len = (outlen - 1) - outleft;
	struct string *s = make_string(out, result_len);
	free(out);
	return s;
}

static struct ex *ex;

/*
 * Helpers — cached resolve to avoid O(n) linear scan per EX call
 */
#define RESOLVE_CACHE_SIZE 32
static struct {
	char name[64];
	struct ex_value *value;
} resolve_cache[RESOLVE_CACHE_SIZE];

static uint32_t fnv1a(const char *s)
{
	uint32_t h = 2166136261u;
	for (; *s; s++)
		h = (h ^ (uint8_t)*s) * 16777619u;
	return h;
}

/*
 * Convert a byte string between encodings using iconv.
 * Returns a malloc'd buffer (caller frees) or NULL on failure.
 */
static char *iconv_convert(const char *from_enc, const char *to_enc,
                           const char *src, size_t src_len, size_t *out_len)
{
	iconv_t cd = iconv_open(to_enc, from_enc);
	if (cd == (iconv_t)-1) return NULL;
	size_t buflen = src_len * 2 + 1;
	char *buf = xmalloc(buflen);
	char *inp = (char *)src;
	char *outp = buf;
	size_t inleft = src_len, outleft = buflen - 1;
	size_t ret = iconv(cd, &inp, &inleft, &outp, &outleft);
	iconv_close(cd);
	if (ret == (size_t)-1 || inleft > 0) {
		free(buf);
		return NULL;
	}
	*out_len = (buflen - 1) - outleft;
	buf[*out_len] = '\0';
	return buf;
}

static struct ex_value *resolve(struct string *name)
{
	if (!name) return NULL;
	uint32_t h = fnv1a(name->text);
	unsigned idx = h % RESOLVE_CACHE_SIZE;
	if (resolve_cache[idx].name[0] && !strcmp(resolve_cache[idx].name, name->text))
		return resolve_cache[idx].value;
	struct ex_value *v = ex_get(ex, name->text);
	/*
	 * .ex block names are EUC-JP but AIN queries arrive as SJIS.
	 * If direct lookup fails, try converting SJIS→EUC-JP.
	 */
	if (!v && ain_is_gb18030) {
		size_t conv_len;
		char *conv = iconv_convert("SHIFT_JIS", "EUC-JP",
		                           name->text, name->size, &conv_len);
		if (conv) {
			v = ex_get(ex, conv);
			free(conv);
		}
	}
	if (v) {
		snprintf(resolve_cache[idx].name, sizeof(resolve_cache[idx].name), "%s", name->text);
		resolve_cache[idx].value = v;
	}
	return v;
}

static struct ex_table *resolve_table(struct string *name)
{
	struct ex_value *v = resolve(name);
	return (v && v->type == EX_TABLE) ? v->t : NULL;
}

static struct ex_list *resolve_list(struct string *name)
{
	struct ex_value *v = resolve(name);
	if (v && v->type == EX_LIST) return v->list;
	return NULL;
}

static struct ex_tree *resolve_tree(struct string *name)
{
	struct ex_value *v = resolve(name);
	if (!v || v->type != EX_TREE) return NULL;
	return v->tree->is_leaf ? NULL : v->tree;
}

/*
 * Helper: get the sub-table from a list item at the given row index.
 * EX_LIST data in Dohna Dohna stores each "row" as a list item whose
 * value is a single-row EX_TABLE.
 */
static struct ex_table *list_item_table(struct ex_list *list, int row)
{
	if (!list || row < 0 || (unsigned)row >= list->nr_items)
		return NULL;
	struct ex_value *item = &list->items[row].value;
	return (item->type == EX_TABLE) ? item->t : NULL;
}

/*
 * Module lifecycle
 */
static struct ex *load_ex_file(const char *path)
{
	// CN .ex files are already GBK-encoded — do NOT apply sjis_to_gbk_string.
	// The SJIS→GBK conv was only needed when the .ex used SJIS and the
	// AIN bytecode queried with GBK. CN .ex uses GBK natively.
	return ex_read_file(path);
}

static void MainEXFile_ModuleInit(void)
{
	if (!config.ex_path || !(ex = load_ex_file(config.ex_path)))
		ERROR("Failed to load .ex file: %s", display_utf0(config.ex_path));
	NOTICE("MainEXFile: loaded %u blocks from '%s'", ex->nr_blocks, config.ex_path);
}

static void MainEXFile_ModuleFini(void)
{
	// Skip ex_free at shutdown — ex_free_tree crashes on some .ex structures.
	// Process exit reclaims memory anyway.
	ex = NULL;
}

/*
 * [ 0] bool ReloadDebugEXFile()
 */
static bool MainEXFile_ReloadDebugEXFile(void)
{
	return false;
}

/*
 * [ 1] int AddEXReader(string FilePath)
 */
static int MainEXFile_AddEXReader(struct string *path)
{
	WARNING("MainEXFile.AddEXReader('%s') — attempting to load", display_game0(path->text));
	struct ex *extra = load_ex_file(path->text);
	if (!extra) {
		char *full = path_join(config.game_dir, path->text);
		extra = load_ex_file(full);
		free(full);
	}
	if (extra) {
		ex_append(ex, extra);
		WARNING("MainEXFile.AddEXReader: loaded %u blocks", extra->nr_blocks);
		return 1;  // Return non-zero ID
	}
	WARNING("MainEXFile.AddEXReader('%s'): file not found", display_game0(path->text));
	return 0;
}

/*
 * [ 2] void EraseEXReader(int ID)
 */
static void MainEXFile_EraseEXReader(int id)
{
}

/*
 * [ 3] bool AddEX(string FilePath)
 */
static bool MainEXFile_AddEX(struct string *path)
{
	WARNING("MainEXFile.AddEX('%s') — attempting to load", display_game0(path->text));
	struct ex *extra = load_ex_file(path->text);
	if (extra) {
		ex_append(ex, extra);
		WARNING("MainEXFile.AddEX: loaded %u blocks", extra->nr_blocks);
		return true;
	}
	// Try relative to game dir
	char *full = path_join(config.game_dir, path->text);
	extra = load_ex_file(full);
	if (extra) {
		ex_append(ex, extra);
		WARNING("MainEXFile.AddEX('%s'): loaded %u blocks",
			game_charset_is_gbk() ? display_game0(path->text) : full, extra->nr_blocks);
		free(full);
		return true;
	}
	WARNING("MainEXFile.AddEX('%s'): file not found", display_game0(path->text));
	free(full);
	return false;
}

/*
 * [ 4] bool AddEXText(string FilePath)
 */
static bool MainEXFile_AddEXText(struct string *path)
{
	WARNING("MainEXFile.AddEXText('%s') stub", display_game0(path->text));
	return false;
}

/*
 * [ 5] bool Save(wrap image) — stub
 * [ 6] bool Load(wrap image) — stub
 */
static bool MainEXFile_Save(int image_slot)
{
	// No-op: ResumeSave is a stub so VM state (including EX data) stays in memory.
	return true;
}

static bool MainEXFile_Load(int image_slot)
{
	// No-op: data is still in memory since ResumeSave doesn't actually save/restore.
	return true;
}

/*
 * [ 7] int Row(string Name, int ID)
 */
static int MainEXFile_Row(struct string *name, int id)
{
	struct ex_table *t = resolve_table(name);
	if (t) return t->nr_rows;
	struct ex_list *list = resolve_list(name);
	if (list) return list->nr_items;
	return 0;
}

/*
 * [ 8] int Col(string Name, int ID)
 *
 * Native 0x4ae006 -> 0x4b00e0 looks the name up as a list first (EX object
 * vt+0x2c, node type 5) and returns its element count (list vt+8 =
 * 0x478e50); only a table (vt+0x28, type 4) reports its column count.
 * EXHelper::GetStringArray sizes flat lists with it (Tutorial::GetCgs).
 */
static int MainEXFile_Col(struct string *name, int id)
{
	struct ex_list *list = resolve_list(name);
	if (list) return list->nr_items;
	struct ex_table *t = resolve_table(name);
	return t ? t->nr_columns : 0;
}

/*
 * [ 9] int Type(string Name, int ID)
 */
static int MainEXFile_Type(struct string *name, int id)
{
	struct ex_value *v = resolve(name);
	return v ? v->type : 0;
}

/*
 * [10] int AType(string Name, int Index, int ID)
 */
static int MainEXFile_AType(struct string *name, int index, int id)
{
	struct ex_list *list = resolve_list(name);
	if (!list) return 0;
	struct ex_value *v = ex_list_get(list, index);
	return v ? v->type : 0;
}

/*
 * [11] int A2Type(string Name, int Row, int Col, int ID)
 */
static int MainEXFile_A2Type(struct string *name, int row, int col, int id)
{
	struct ex_table *t = resolve_table(name);
	if (!t) {
		t = list_item_table(resolve_list(name), row);
		if (!t) return 0;
		struct ex_value *v = ex_table_get(t, 0, col);
		return v ? v->type : 0;
	}
	struct ex_value *v = ex_table_get(t, row, col);
	return v ? v->type : 0;
}

/*
 * [12] bool Exists(string Name, int ID)
 */
static bool MainEXFile_Exists(struct string *name, int id)
{
	return !!resolve(name);
}

/*
 * [13] bool AExists(string Name, int Index, int ID)
 */
static bool MainEXFile_AExists(struct string *name, int index, int id)
{
	struct ex_list *list = resolve_list(name);
	return list ? !!ex_list_get(list, index) : false;
}

/*
 * [14] bool A2Exists(string Name, int Row, int Col, int ID)
 */
static bool MainEXFile_A2Exists(struct string *name, int row, int col, int id)
{
	struct ex_table *t = resolve_table(name);
	if (t) return !!ex_table_get(t, row, col);
	t = list_item_table(resolve_list(name), row);
	return t ? !!ex_table_get(t, 0, col) : false;
}

/*
 * [15] int Int(string Name, int Default, int ID)
 */
static int MainEXFile_Int(struct string *name, int dflt, int id)
{
	struct ex_value *v = resolve(name);
	return (v && v->type == EX_INT) ? v->i : dflt;
}

/*
 * [16] float Float(string Name, float Default, int ID)
 */
static float MainEXFile_Float(struct string *name, float dflt, int id)
{
	struct ex_value *v = resolve(name);
	return (v && v->type == EX_FLOAT) ? v->f : dflt;
}

/*
 * [17] string String(string Name, string Default, int ID)
 */
static struct string *MainEXFile_String(struct string *name, struct string *dflt, int id)
{
	struct ex_value *v = resolve(name);
	if (v && v->type == EX_STRING) {
		return string_ref(v->s);
	}
	return dflt ? string_ref(dflt) : string_ref(&EMPTY_STRING);
}

/*
 * [18] int AInt(string Name, int Index, int Default, int ID)
 */
static int MainEXFile_AInt(struct string *name, int index, int dflt, int id)
{
	struct ex_list *list = resolve_list(name);
	if (!list) return dflt;
	struct ex_value *v = ex_list_get(list, index);
	return (v && v->type == EX_INT) ? v->i : dflt;
}

/*
 * [19] float AFloat(string Name, int Index, float Default, int ID)
 */
static float MainEXFile_AFloat(struct string *name, int index, float dflt, int id)
{
	struct ex_list *list = resolve_list(name);
	if (!list) return dflt;
	struct ex_value *v = ex_list_get(list, index);
	return (v && v->type == EX_FLOAT) ? v->f : dflt;
}

/*
 * [20] string AString(string Name, int Index, string Default, int ID)
 */
static struct string *MainEXFile_AString(struct string *name, int index, struct string *dflt, int id)
{
	struct ex_list *list = resolve_list(name);
	if (!list) goto def;
	struct ex_value *v = ex_list_get(list, index);
	if (v && v->type == EX_STRING)
		return string_ref(v->s);
def:
	return dflt ? string_ref(dflt) : string_ref(&EMPTY_STRING);
}

/*
 * [21] int A2Int(string Name, int Row, int Col, int Default, int ID)
 */
static int MainEXFile_A2Int(struct string *name, int row, int col, int dflt, int id)
{
	struct ex_table *t = resolve_table(name);
	if (!t) {
		t = list_item_table(resolve_list(name), row);
		if (!t) return dflt;
		struct ex_value *v = ex_table_get(t, 0, col);
		return (v && v->type == EX_INT) ? v->i : dflt;
	}
	struct ex_value *v = ex_table_get(t, row, col);
	return (v && v->type == EX_INT) ? v->i : dflt;
}

/*
 * [22] float A2Float(string Name, int Row, int Col, float Default, int ID)
 */
static float MainEXFile_A2Float(struct string *name, int row, int col, float dflt, int id)
{
	struct ex_table *t = resolve_table(name);
	if (!t) {
		t = list_item_table(resolve_list(name), row);
		if (!t) return dflt;
		struct ex_value *v = ex_table_get(t, 0, col);
		return (v && v->type == EX_FLOAT) ? v->f : dflt;
	}
	struct ex_value *v = ex_table_get(t, row, col);
	return (v && v->type == EX_FLOAT) ? v->f : dflt;
}

/*
 * [23] string A2String(string Name, int Row, int Col, string Default, int ID)
 */
static struct string *MainEXFile_A2String(struct string *name, int row, int col, struct string *dflt, int id)
{
	struct ex_table *t = resolve_table(name);
	if (!t) {
		t = list_item_table(resolve_list(name), row);
		if (t) {
			struct ex_value *v = ex_table_get(t, 0, col);
			if (v && v->type == EX_STRING)
				return string_ref(v->s);
		}
		return dflt ? string_ref(dflt) : string_ref(&EMPTY_STRING);
	}
	struct ex_value *v = ex_table_get(t, row, col);
	if (v && v->type == EX_STRING) {
		return string_ref(v->s);
	}
	return dflt ? string_ref(dflt) : string_ref(&EMPTY_STRING);
}

/*
 * [24] int GetRowAtIntKey(string Name, int Key, int ID)
 */
static int MainEXFile_GetRowAtIntKey(struct string *name, int key, int id)
{
	struct ex_table *t = resolve_table(name);
	if (t) return ex_row_at_int_key(t, key);
	struct ex_list *list = resolve_list(name);
	if (!list) return -1;
	for (unsigned i = 0; i < list->nr_items; i++) {
		struct ex_table *lt = list_item_table(list, i);
		if (lt && ex_row_at_int_key(lt, key) >= 0)
			return i;
	}
	return -1;
}

/*
 * [25] int GetRowAtStringKey(string Name, string Key, int ID)
 */
static int MainEXFile_GetRowAtStringKey(struct string *name, struct string *key, int id)
{
	if (!key) return -1;
	struct ex_table *t = resolve_table(name);
	if (t) {
		return ex_row_at_string_key(t, key->text);
	}
	struct ex_list *list = resolve_list(name);
	if (!list) return -1;
	for (unsigned i = 0; i < list->nr_items; i++) {
		struct ex_table *lt = list_item_table(list, i);
		if (lt && ex_row_at_string_key(lt, key->text) >= 0)
			return i;
	}
	return -1;
}

/*
 * [26] int GetColAtFormatName(string Name, string FormatName, int ID)
 */
static int MainEXFile_GetColAtFormatName(struct string *name, struct string *format_name, int id)
{
	struct ex_table *t = resolve_table(name);
	if (!t) t = list_item_table(resolve_list(name), 0);
	if (!t)
		return -1;
	if (!format_name || format_name->size == 0)
		return (t->nr_fields > 0) ? 0 : -1;
	return ex_col_from_name(t, format_name->text);
}

/*
 * [27] int GetNodeNameCount(string TreePath, int ID)
 */
static int MainEXFile_GetNodeNameCount(struct string *tree_path, int id)
{
	struct ex_tree *tree = resolve_tree(tree_path);
	if (!tree) return 0;
	int count = 0;
	for (unsigned i = 0; i < tree->nr_children; i++) {
		if (!tree->children[i].is_leaf)
			count++;
	}
	return count;
}

/*
 * [28] int GetEXNameCount(string TreePath, int ID)
 */
static int MainEXFile_GetEXNameCount(struct string *tree_path, int id)
{
	struct ex_tree *tree = resolve_tree(tree_path);
	if (!tree) return 0;
	int count = 0;
	for (unsigned i = 0; i < tree->nr_children; i++) {
		if (tree->children[i].is_leaf)
			count++;
	}
	return count;
}

/*
 * [29] bool GetNodeName(string TreePath, int Index, wrap NodeName, int ID)
 *
 * wrap<string> — the vm_value pointed to by node_name is a string heap slot
 */
static bool MainEXFile_GetNodeName(struct string *tree_path, int index, int *name_out, int id)
{
	struct ex_tree *tree = resolve_tree(tree_path);
	if (!tree) return false;

	int count = 0;
	for (unsigned i = 0; i < tree->nr_children; i++) {
		if (tree->children[i].is_leaf)
			continue;
		if (count == index) {
			wrap_set_string(name_out, string_ref(tree->children[i].name));
			return true;
		}
		count++;
	}
	return false;
}

/*
 * [30] bool GetEXName(string TreePath, int Index, wrap EXName, int ID)
 */
static bool MainEXFile_GetEXName(struct string *tree_path, int index, int *name_out, int id)
{
	struct ex_tree *tree = resolve_tree(tree_path);
	if (!tree) return false;

	int count = 0;
	for (unsigned i = 0; i < tree->nr_children; i++) {
		if (!tree->children[i].is_leaf)
			continue;
		if (count == index) {
			wrap_set_string(name_out, string_ref(tree->children[i].leaf.name));
			return true;
		}
		count++;
	}
	return false;
}

/*
 * The "wrap<array<string>>" argument of the three list functions is the
 * caller's array itself: native 0x65a620 -> 0x67aba0 takes the argument as
 * a heap slot, requires a page of type 3 (an array) and hands the callee
 * that page's IVMArray, or NULL. EX_GetNodeNameList and EX_GetEXNameList
 * pass their "ref array<string>" after Array.Free, EX_GetFormatNameList a
 * local that X_A_INIT 0 just made; none of them boxes it.
 *
 * Returns false when the slot is not a one-dimensional string array. A slot
 * without a page is an array variable that was never initialized. An empty
 * page without an element type is taken as a string array: the declaration
 * says it is one, and the original array always knows its element type.
 */
static bool ex_list_array(int slot, struct page **array)
{
	if (slot <= 0 || !heap_index_valid(slot) || heap[slot].type != VM_PAGE)
		return false;
	struct page *page = heap[slot].page;
	*array = page;
	if (!page)
		return true;
	if (page->type != ARRAY_PAGE || page->array.rank > 1)
		return false;
	if (page->a_type == AIN_ARRAY_STRING || page->a_type == AIN_REF_ARRAY_STRING)
		return true;
	return (page->a_type == AIN_ARRAY || page->a_type == AIN_REF_ARRAY) && page->nr_vars == 0;
}

/*
 * Stores count names in the array at slot (old is its page, from
 * ex_list_array).
 *
 * keep: the names are appended. Native GetNodeNameList / GetEXNameList
 * (CEXReader 0x488550 / 0x4886e0) take the array's size (IVMArray vt+0xc,
 * 0x67ee60), Realloc to size + count (vt+0x50, 0x67f4d0) and store name i
 * at size + i; with no names the array is left as it is. Otherwise the
 * names replace the elements: GetFormatNameList (0x4b0fd0) calls Alloc
 * (vt+0x4c, 0x67f4a0), which clears the array first.
 *
 * Each name gets a string slot of its own (vt+0x3c, 0x67f1e0, assigns a
 * copy of the text to the element's string; here the string object is
 * shared with the EX data, as MainEXFile.String does). As in Array.c the
 * new page is in place before the old elements are released.
 */
static void ex_list_store(int slot, struct page *old, struct string **names, int count, bool keep)
{
	if (keep && count == 0)
		return;
	int kept = keep && old ? old->nr_vars : 0;
	bool typed = old && (old->a_type == AIN_ARRAY_STRING || old->a_type == AIN_REF_ARRAY_STRING);
	// The new strings are unreachable until the page is installed.
	heap_gc_inhibit();
	struct page *page = alloc_page(ARRAY_PAGE, typed ? old->a_type : AIN_ARRAY_STRING, kept + count);
	page->array.struct_type = typed ? old->array.struct_type : -1;
	page->array.rank = 1;
	for (int i = 0; i < kept; i++)
		page->values[i] = old->values[i];
	for (int i = 0; i < count; i++) {
		struct string *name = names[i] ? names[i] : &EMPTY_STRING;
		page->values[kept + i].i = heap_alloc_string(string_ref(name));
	}
	heap_set_page(slot, page);
	heap_gc_allow();
	if (old) {
		if (!keep)
			delete_page_vars(old);
		free_page(old);
	}
}

/*
 * Appends the names of tree's children to the array: the nodes (native
 * 0x48dd40, the children without a value) or the EX values (0x48dde0, the
 * children with one), in their stored order.
 */
static void ex_list_children(int slot, struct page *old, struct ex_tree *tree, bool leaves)
{
	if (!tree->nr_children || !tree->children)
		return;
	struct string **names = xcalloc(tree->nr_children, sizeof(struct string *));
	int count = 0;
	for (unsigned i = 0; i < tree->nr_children; i++) {
		struct ex_tree *child = &tree->children[i];
		if (child->is_leaf == leaves)
			names[count++] = leaves ? child->leaf.name : child->name;
	}
	ex_list_store(slot, old, names, count, true);
	free(names);
}

/*
 * [31] bool GetNodeNameList(string TreePath, wrap NodeNameList, int ID)
 *
 * Native 0x4ae75b -> 0x4b0ef0 -> CEXReader vt+0xc (0x488550): false when
 * the array is NULL or the path is not found (0x487990: its first part must
 * name a tree); otherwise the node names are appended and the result is
 * true, also when there are none.
 *
 * Not as the original: the empty path lists the top-level trees there
 * (0x487d70) and is false here; a path that ends at a leaf is false here
 * (not read natively); ID selects a reader there (0x442fc0, false when it
 * is not registered) and is ignored here.
 *
 * Downstream: the battle's motion frames are found through this list
 * (FrameInfoCollection@InnerLoad), but a frame still loads only its first 9
 * sub-parameters. The array literal of FrameInfo@GetParams (19 two-slot
 * elements) is cut to 19 slots by X_A_INIT's stride (vm.c), so CG layers and
 * effects are not loaded until that is fixed, and each frame leaves the
 * references of the elements that were cut.
 */
static bool MainEXFile_GetNodeNameList(struct string *tree_path, int list_slot, int id)
{
	struct page *old;
	if (!ex_list_array(list_slot, &old))
		return false;
	struct ex_tree *tree = resolve_tree(tree_path);
	if (!tree)
		return false;
	ex_list_children(list_slot, old, tree, false);
	return true;
}

/*
 * [32] bool GetEXNameList(string TreePath, wrap EXNameList, int ID)
 *
 * Native 0x4ae7b4 -> 0x4b0f60 -> CEXReader vt+0x10 (0x4886e0): as
 * GetNodeNameList, for the children that hold a value.
 */
static bool MainEXFile_GetEXNameList(struct string *tree_path, int list_slot, int id)
{
	struct page *old;
	if (!ex_list_array(list_slot, &old))
		return false;
	struct ex_tree *tree = resolve_tree(tree_path);
	if (!tree)
		return false;
	ex_list_children(list_slot, old, tree, true);
	return true;
}

/*
 * [33] bool GetFormatNameList(string Name, wrap FormatNameList, int ID)
 *
 * Returns column/field names for a table. Native 0x4ae7e8 -> 0x4b0fd0:
 * false when the name is not a table (EX vt+0x28); otherwise the array is
 * reallocated to the field count (Alloc) and name i is stored at i.
 *
 * Not as the original: 0x4b0fd0 does not test the array pointer (false
 * here when the argument is not an array), and a list whose first item is
 * a table answers with that table's fields here.
 */
static bool MainEXFile_GetFormatNameList(struct string *name, int list_slot, int id)
{
	struct page *old;
	if (!ex_list_array(list_slot, &old))
		return false;
	struct ex_table *t = resolve_table(name);
	if (!t) t = list_item_table(resolve_list(name), 0);
	if (!t)
		return false;

	struct string **names = xcalloc(t->nr_fields ? t->nr_fields : 1, sizeof(struct string *));
	for (unsigned i = 0; i < t->nr_fields; i++)
		names[i] = t->fields[i].name;
	ex_list_store(list_slot, old, names, t->nr_fields, false);
	free(names);
	return true;
}

HLL_LIBRARY(MainEXFile,
	    HLL_EXPORT(_ModuleInit, MainEXFile_ModuleInit),
	    HLL_EXPORT(_ModuleFini, MainEXFile_ModuleFini),
	    HLL_EXPORT(ReloadDebugEXFile, MainEXFile_ReloadDebugEXFile),
	    HLL_EXPORT(AddEXReader, MainEXFile_AddEXReader),
	    HLL_EXPORT(EraseEXReader, MainEXFile_EraseEXReader),
	    HLL_EXPORT(AddEX, MainEXFile_AddEX),
	    HLL_EXPORT(AddEXText, MainEXFile_AddEXText),
	    HLL_EXPORT(Save, MainEXFile_Save),
	    HLL_EXPORT(Load, MainEXFile_Load),
	    HLL_EXPORT(Row, MainEXFile_Row),
	    HLL_EXPORT(Col, MainEXFile_Col),
	    HLL_EXPORT(Type, MainEXFile_Type),
	    HLL_EXPORT(AType, MainEXFile_AType),
	    HLL_EXPORT(A2Type, MainEXFile_A2Type),
	    HLL_EXPORT(Exists, MainEXFile_Exists),
	    HLL_EXPORT(AExists, MainEXFile_AExists),
	    HLL_EXPORT(A2Exists, MainEXFile_A2Exists),
	    HLL_EXPORT(Int, MainEXFile_Int),
	    HLL_EXPORT(Float, MainEXFile_Float),
	    HLL_EXPORT(String, MainEXFile_String),
	    HLL_EXPORT(AInt, MainEXFile_AInt),
	    HLL_EXPORT(AFloat, MainEXFile_AFloat),
	    HLL_EXPORT(AString, MainEXFile_AString),
	    HLL_EXPORT(A2Int, MainEXFile_A2Int),
	    HLL_EXPORT(A2Float, MainEXFile_A2Float),
	    HLL_EXPORT(A2String, MainEXFile_A2String),
	    HLL_EXPORT(GetRowAtIntKey, MainEXFile_GetRowAtIntKey),
	    HLL_EXPORT(GetRowAtStringKey, MainEXFile_GetRowAtStringKey),
	    HLL_EXPORT(GetColAtFormatName, MainEXFile_GetColAtFormatName),
	    HLL_EXPORT(GetNodeNameCount, MainEXFile_GetNodeNameCount),
	    HLL_EXPORT(GetEXNameCount, MainEXFile_GetEXNameCount),
	    HLL_EXPORT(GetNodeName, MainEXFile_GetNodeName),
	    HLL_EXPORT(GetEXName, MainEXFile_GetEXName),
	    HLL_EXPORT(GetNodeNameList, MainEXFile_GetNodeNameList),
	    HLL_EXPORT(GetEXNameList, MainEXFile_GetEXNameList),
	    HLL_EXPORT(GetFormatNameList, MainEXFile_GetFormatNameList));
