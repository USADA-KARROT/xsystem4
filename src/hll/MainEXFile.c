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

#include "system4/ex.h"
#include "system4/string.h"
#include "system4/ain.h"

#include "vm.h"
#include "vm/heap.h"
#include "vm/page.h"
#include "xsystem4.h"
#include "hll.h"

static struct ex *ex;

/*
 * Helpers
 */
static struct ex_value *resolve(struct string *name)
{
	if (!name) return NULL;
	return ex_get(ex, name->text);
}

static struct ex_table *resolve_table(struct string *name)
{
	struct ex_value *v = resolve(name);
	return (v && v->type == EX_TABLE) ? v->t : NULL;
}

static struct ex_list *resolve_list(struct string *name)
{
	struct ex_value *v = resolve(name);
	return (v && v->type == EX_LIST) ? v->list : NULL;
}

static struct ex_tree *resolve_tree(struct string *name)
{
	struct ex_value *v = resolve(name);
	if (!v || v->type != EX_TREE) return NULL;
	return v->tree->is_leaf ? NULL : v->tree;
}

/*
 * Module lifecycle
 */
static void MainEXFile_ModuleInit(void)
{
	if (!config.ex_path || !(ex = ex_read_file(config.ex_path)))
		ERROR("Failed to load .ex file: %s", display_utf0(config.ex_path));
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
	return false;
}

/*
 * [ 4] bool AddEXText(string FilePath)
 */
static bool MainEXFile_AddEXText(struct string *path)
{
	return false;
}

/*
 * [ 5] bool Save(wrap image) — stub
 * [ 6] bool Load(wrap image) — stub
 */
static bool MainEXFile_Save(union vm_value *image)
{
	return false;
}

static bool MainEXFile_Load(union vm_value *image)
{
	return false;
}

/*
 * [ 7] int Row(string Name, int ID)
 */
static int MainEXFile_Row(struct string *name, int id)
{
	struct ex_table *t = resolve_table(name);
	return t ? t->nr_rows : 0;
}

/*
 * [ 8] int Col(string Name, int ID)
 */
static int MainEXFile_Col(struct string *name, int id)
{
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
	if (!t) return 0;
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
	return t ? !!ex_table_get(t, row, col) : false;
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
	if (v && v->type == EX_STRING)
		return string_ref(v->s);
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
	if (!t) return dflt;
	struct ex_value *v = ex_table_get(t, row, col);
	return (v && v->type == EX_INT) ? v->i : dflt;
}

/*
 * [22] float A2Float(string Name, int Row, int Col, float Default, int ID)
 */
static float MainEXFile_A2Float(struct string *name, int row, int col, float dflt, int id)
{
	struct ex_table *t = resolve_table(name);
	if (!t) return dflt;
	struct ex_value *v = ex_table_get(t, row, col);
	return (v && v->type == EX_FLOAT) ? v->f : dflt;
}

/*
 * [23] string A2String(string Name, int Row, int Col, string Default, int ID)
 */
static struct string *MainEXFile_A2String(struct string *name, int row, int col, struct string *dflt, int id)
{
	struct ex_table *t = resolve_table(name);
	if (!t) goto def;
	struct ex_value *v = ex_table_get(t, row, col);
	if (v && v->type == EX_STRING)
		return string_ref(v->s);
def:
	return dflt ? string_ref(dflt) : string_ref(&EMPTY_STRING);
}

/*
 * [24] int GetRowAtIntKey(string Name, int Key, int ID)
 */
static int MainEXFile_GetRowAtIntKey(struct string *name, int key, int id)
{
	struct ex_table *t = resolve_table(name);
	return t ? ex_row_at_int_key(t, key) : -1;
}

/*
 * [25] int GetRowAtStringKey(string Name, string Key, int ID)
 */
static int MainEXFile_GetRowAtStringKey(struct string *name, struct string *key, int id)
{
	struct ex_table *t = resolve_table(name);
	return t ? ex_row_at_string_key(t, key->text) : -1;
}

/*
 * [26] int GetColAtFormatName(string Name, string FormatName, int ID)
 */
static int MainEXFile_GetColAtFormatName(struct string *name, struct string *format_name, int id)
{
	struct ex_table *t = resolve_table(name);
	return t ? ex_col_from_name(t, format_name->text) : -1;
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
static bool MainEXFile_GetNodeName(struct string *tree_path, int index, union vm_value *node_name, int id)
{
	struct ex_tree *tree = resolve_tree(tree_path);
	if (!tree) return false;

	int count = 0;
	for (unsigned i = 0; i < tree->nr_children; i++) {
		if (tree->children[i].is_leaf)
			continue;
		if (count == index) {
			if (node_name->i > 0)
				heap_unref(node_name->i);
			node_name->i = heap_alloc_string(string_ref(tree->children[i].name));
			return true;
		}
		count++;
	}
	return false;
}

/*
 * [30] bool GetEXName(string TreePath, int Index, wrap EXName, int ID)
 */
static bool MainEXFile_GetEXName(struct string *tree_path, int index, union vm_value *ex_name, int id)
{
	struct ex_tree *tree = resolve_tree(tree_path);
	if (!tree) return false;

	int count = 0;
	for (unsigned i = 0; i < tree->nr_children; i++) {
		if (!tree->children[i].is_leaf)
			continue;
		if (count == index) {
			if (ex_name->i > 0)
				heap_unref(ex_name->i);
			ex_name->i = heap_alloc_string(string_ref(tree->children[i].leaf.name));
			return true;
		}
		count++;
	}
	return false;
}

/*
 * [31] bool GetNodeNameList(string TreePath, wrap NodeNameList, int ID)
 *
 * wrap<array<string>> — write an array page of strings to the wrap value
 */
static bool MainEXFile_GetNodeNameList(struct string *tree_path, union vm_value *list, int id)
{
	struct ex_tree *tree = resolve_tree(tree_path);
	if (!tree) return false;

	int count = 0;
	for (unsigned i = 0; i < tree->nr_children; i++) {
		if (!tree->children[i].is_leaf)
			count++;
	}

	union vm_value dim = { .i = count };
	struct page *array = alloc_array(1, &dim, AIN_ARRAY_STRING, 0, false);
	int idx = 0;
	for (unsigned i = 0; i < tree->nr_children; i++) {
		if (!tree->children[i].is_leaf) {
			array->values[idx].i = heap_alloc_string(string_ref(tree->children[i].name));
			idx++;
		}
	}

	if (list->i > 0)
		heap_unref(list->i);
	list->i = heap_alloc_page(array);
	return true;
}

/*
 * [32] bool GetEXNameList(string TreePath, wrap EXNameList, int ID)
 */
static bool MainEXFile_GetEXNameList(struct string *tree_path, union vm_value *list, int id)
{
	struct ex_tree *tree = resolve_tree(tree_path);
	if (!tree) return false;

	int count = 0;
	for (unsigned i = 0; i < tree->nr_children; i++) {
		if (tree->children[i].is_leaf)
			count++;
	}

	union vm_value dim = { .i = count };
	struct page *array = alloc_array(1, &dim, AIN_ARRAY_STRING, 0, false);
	int idx = 0;
	for (unsigned i = 0; i < tree->nr_children; i++) {
		if (tree->children[i].is_leaf) {
			array->values[idx].i = heap_alloc_string(string_ref(tree->children[i].leaf.name));
			idx++;
		}
	}

	if (list->i > 0)
		heap_unref(list->i);
	list->i = heap_alloc_page(array);
	return true;
}

/*
 * [33] bool GetFormatNameList(string Name, wrap FormatNameList, int ID)
 *
 * Returns column/field names for a table
 */
static bool MainEXFile_GetFormatNameList(struct string *name, union vm_value *list, int id)
{
	struct ex_table *t = resolve_table(name);
	if (!t) return false;

	union vm_value dim = { .i = t->nr_fields };
	struct page *array = alloc_array(1, &dim, AIN_ARRAY_STRING, 0, false);
	for (unsigned i = 0; i < t->nr_fields; i++) {
		array->values[i].i = heap_alloc_string(string_ref(t->fields[i].name));
	}

	if (list->i > 0)
		heap_unref(list->i);
	list->i = heap_alloc_page(array);
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
