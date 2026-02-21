/* Copyright (C) 2023 Nunuhara Cabbage <nunuhara@haniwa.technology>
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

#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "system4/ain.h"
#include "system4/archive.h"
#include "system4/ex.h"
#include "system4/string.h"

#include "asset_manager.h"
#include "parts.h"
#include "parts/parts_internal.h"
#include "hll.h"

static void PartsEngine_ModuleInit(void)
{
	PE_Init();
}

static void PartsEngine_ModuleFini(void)
{
	PE_Reset();
}

static void PartsEngine_Update(int passed_time, bool is_skip, bool message_window_show)
{
	PE_Update(passed_time, message_window_show);
}

// Oyako Rankan
static bool PartsEngine_AddDrawCutCGToPartsConstructionProcess_old(int parts_no,
		struct string *cg_name, int dx, int dy, int sx, int sy, int w, int h,
		int state)
{
	return PE_AddDrawCutCGToPartsConstructionProcess(parts_no, cg_name, dx, dy, w, h,
			sx, sy, w, h, 0, state);
}

// Oyako Rankan
static bool PartsEngine_AddCopyCutCGToPartsConstructionProcess_old(int parts_no,
		struct string *cg_name, int dx, int dy, int sx, int sy, int w, int h,
		int state)
{
	return PE_AddCopyCutCGToPartsConstructionProcess(parts_no, cg_name, dx, dy, w, h,
			sx, sy, w, h, 0, state);
}

// Message pump stubs — return "no message" to break the game's message loop.
// Without these, the game loops forever calling PopMessage/GetMessageType.
HLL_QUIET_UNIMPLEMENTED(, void, PartsEngine, PopMessage);
HLL_QUIET_UNIMPLEMENTED(0, int, PartsEngine, GetMessageType);
HLL_QUIET_UNIMPLEMENTED(0, int, PartsEngine, GetMessagePartsNumber);
HLL_QUIET_UNIMPLEMENTED(0, int, PartsEngine, GetMessageVariableInt, int index);
HLL_QUIET_UNIMPLEMENTED(, void, PartsEngine, SetActiveController, int controller);
HLL_QUIET_UNIMPLEMENTED(0, int, PartsEngine, GetActiveController);

static void PartsEngine_UpdateComponent(int passed_time, int is_skip, int unknown,
		float mag_x, float mag_y)
{
	PE_UpdateComponent(passed_time);
}

// --- Component type and child management ---

// [75] int GetComponentType(int Number, int State)
static int PartsEngine_GetComponentType(int parts_no, int state)
{
	struct parts *p = parts_try_get(parts_no);
	if (!p) return 0;
	if (state < 0 || state >= PARTS_NR_STATES) return 0;
	return p->states[state].type;
}

// [74] void SetComponentType(int Number, int Type, int State)
static void PartsEngine_SetComponentType(int parts_no, int type, int state)
{
	struct parts *p = parts_try_get(parts_no);
	if (!p) return;
	if (state < 0 || state >= PARTS_NR_STATES) return;
	if (type >= 0 && type < PARTS_NR_TYPES)
		parts_state_reset(&p->states[state], type);
}

// [174] int NumofChild(int Number)
static int PartsEngine_NumofChild(int parts_no)
{
	struct parts *p = parts_try_get(parts_no);
	if (!p) return 0;
	int count = 0;
	struct parts *child;
	PARTS_FOREACH_CHILD(child, p) {
		count++;
	}
	return count;
}

// --- Activity system ---
// Activities are GUI scenes loaded from .pactex files (EX format) in the PACT archive.
// Each activity defines a tree of PartsEngine components.
//
// AIN HLL signatures (v14 Dohna Dohna):
//   [24] bool IsExistActivity(string Name)
//   [25] bool CreateActivity(string Name)
//   [26] bool ReleaseActivity(string Name, wrap[array[int]] EraseNumberList)
//   [29] bool ReadActivityFile(string Name, string FileName, bool Edit)
//   [31] bool IsExistActivityFile(string FileName)
//   [32] bool AddActivityParts(string Name, string PartsName, int Number)
//   [35] int  NumofActivityParts(string Name)
//   [37] bool IsExistActivityPartsByName(string Name, string PartsName)
//   [38] bool IsExistActivityPartsByNumber(string Name, int Number)
//   [39] int  GetActivityPartsNumber(string Name, string PartsName)
//   [40] string GetActivityPartsName(string Name, int Number)

#define MAX_ACTIVITIES 32
#define MAX_ACTIVITY_PARTS 512

struct activity_part {
	int parts_no;
	char name[128];
};

struct activity {
	bool in_use;
	char name[256];                          // Activity name (string key)
	struct ex *ex_data;                      // Parsed .pactex EX data
	int root_parts_no;                       // Root PE part number
	struct activity_part parts[MAX_ACTIVITY_PARTS];
	int nr_parts;
};

static struct activity activities[MAX_ACTIVITIES];

static struct activity *find_activity(const char *name)
{
	for (int i = 0; i < MAX_ACTIVITIES; i++) {
		if (activities[i].in_use && !strcmp(activities[i].name, name))
			return &activities[i];
	}
	return NULL;
}

static void register_activity_part(struct activity *act, int parts_no, const char *name)
{
	if (act->nr_parts >= MAX_ACTIVITY_PARTS) {
		WARNING("Activity '%s': too many parts (max %d)", act->name, MAX_ACTIVITY_PARTS);
		return;
	}
	act->parts[act->nr_parts].parts_no = parts_no;
	snprintf(act->parts[act->nr_parts].name, sizeof(act->parts[0].name), "%s", name);
	act->nr_parts++;
}

// Helper: find a property leaf in an EX tree node by index
static struct ex_tree *find_child_by_index(struct ex_tree *tree, int index)
{
	if (tree->is_leaf || index < 0 || (uint32_t)index >= tree->nr_children)
		return NULL;
	return &tree->children[index];
}

static int leaf_int(struct ex_tree *node, int dflt)
{
	if (!node || !node->is_leaf) return dflt;
	if (node->leaf.value.type == EX_INT) return node->leaf.value.i;
	if (node->leaf.value.type == EX_FLOAT) return (int)node->leaf.value.f;
	return dflt;
}

static float leaf_float(struct ex_tree *node, float dflt)
{
	if (!node || !node->is_leaf) return dflt;
	if (node->leaf.value.type == EX_FLOAT) return node->leaf.value.f;
	if (node->leaf.value.type == EX_INT) return (float)node->leaf.value.i;
	return dflt;
}

static int list_int(struct ex_list *list, int index, int dflt)
{
	if (!list || (uint32_t)index >= list->nr_items) return dflt;
	struct ex_value *v = &list->items[index].value;
	if (v->type == EX_INT) return v->i;
	if (v->type == EX_FLOAT) return (int)v->f;
	return dflt;
}

static float list_float(struct ex_list *list, int index, float dflt)
{
	if (!list || (uint32_t)index >= list->nr_items) return dflt;
	struct ex_value *v = &list->items[index].value;
	if (v->type == EX_FLOAT) return v->f;
	if (v->type == EX_INT) return (float)v->i;
	return dflt;
}

// Apply properties from a .pactex component tree node to a PE parts number.
// Field layout (indices into component's children array, names are SJIS):
//  [0]  座標 (position) = LIST[3]: [x, y, z]
//  [2]  原点座標モード (origin_mode) = INT
//  [5]  表示 (show) = INT
//  [7]  メッセージ窓表示連動 (msg_window_link) = INT
//  [9]  アルファ (alpha) = INT
//  [10] 加算色 (add_color) = LIST[3]: [r, g, b]
//  [12] 乗算色 (multiply_color) = LIST[3]: [r, g, b]
//  [13] 描画フィルタ (draw_filter) = INT
//  [14] 拡大縮小 (scale) = LIST[2]: [sx, sy]
//  [15] 回転 (rotation) = LIST[3]: [rx, ry, rz]
//  [41] ビジュアル (children) = subtree with child components
static void apply_component_props(struct ex_tree *comp, int parts_no,
		const char *comp_name, struct activity *act)
{
	if (!comp || comp->is_leaf || comp->nr_children < 10)
		return;

	// Register this part with its name
	register_activity_part(act, parts_no, comp_name);

	// [0] Position: LIST[3] = [x, y, z]
	struct ex_tree *pos_node = find_child_by_index(comp, 0);
	if (pos_node && pos_node->is_leaf && pos_node->leaf.value.type == EX_LIST) {
		struct ex_list *pos = pos_node->leaf.value.list;
		int x = (int)list_float(pos, 0, 0);
		int y = (int)list_float(pos, 1, 0);
		int z = list_int(pos, 2, 0);
		PE_SetPos(parts_no, x, y);
		PE_SetZ(parts_no, z);
	}

	// [2] Origin mode
	struct ex_tree *om = find_child_by_index(comp, 2);
	if (om) PE_SetPartsOriginPosMode(parts_no, leaf_int(om, 1));

	// [5] Show
	struct ex_tree *show = find_child_by_index(comp, 5);
	if (show) PE_SetShow(parts_no, leaf_int(show, 1));

	// [7] Message window show link
	struct ex_tree *mw = find_child_by_index(comp, 7);
	if (mw) PE_SetPartsMessageWindowShowLink(parts_no, leaf_int(mw, 0));

	// [9] Alpha
	struct ex_tree *alpha = find_child_by_index(comp, 9);
	if (alpha) PE_SetAlpha(parts_no, leaf_int(alpha, 255));

	// [10] Add color: LIST[3] = [r, g, b]
	struct ex_tree *ac = find_child_by_index(comp, 10);
	if (ac && ac->is_leaf && ac->leaf.value.type == EX_LIST) {
		struct ex_list *c = ac->leaf.value.list;
		PE_SetAddColor(parts_no, list_int(c, 0, 0), list_int(c, 1, 0), list_int(c, 2, 0));
	}

	// [12] Multiply color: LIST[3] = [r, g, b]
	struct ex_tree *mc = find_child_by_index(comp, 12);
	if (mc && mc->is_leaf && mc->leaf.value.type == EX_LIST) {
		struct ex_list *c = mc->leaf.value.list;
		PE_SetMultiplyColor(parts_no, list_int(c, 0, 255), list_int(c, 1, 255), list_int(c, 2, 255));
	}

	// [13] Draw filter
	struct ex_tree *df = find_child_by_index(comp, 13);
	if (df) PE_SetPartsDrawFilter(parts_no, leaf_int(df, 0));

	// [14] Scale: LIST[2] = [sx, sy]
	struct ex_tree *sc = find_child_by_index(comp, 14);
	if (sc && sc->is_leaf && sc->leaf.value.type == EX_LIST) {
		struct ex_list *s = sc->leaf.value.list;
		PE_SetPartsMagX(parts_no, list_float(s, 0, 1.0f));
		PE_SetPartsMagY(parts_no, list_float(s, 1, 1.0f));
	}

	// [15] Rotation: LIST[3] = [rx, ry, rz]
	struct ex_tree *rot = find_child_by_index(comp, 15);
	if (rot && rot->is_leaf && rot->leaf.value.type == EX_LIST) {
		struct ex_list *r = rot->leaf.value.list;
		PE_SetPartsRotateX(parts_no, list_float(r, 0, 0));
		PE_SetPartsRotateY(parts_no, list_float(r, 1, 0));
		PE_SetPartsRotateZ(parts_no, list_float(r, 2, 0));
	}

	// Find children subtree — the LAST non-leaf child in the component
	for (int i = comp->nr_children - 1; i >= 0; i--) {
		struct ex_tree *child = &comp->children[i];
		if (!child->is_leaf && child->nr_children > 0) {
			for (uint32_t c = 0; c < child->nr_children; c++) {
				struct ex_tree *child_comp = &child->children[c];
				if (!child_comp->is_leaf && child_comp->nr_children >= 10) {
					int child_no = PE_GetFreeNumber();
					PE_SetParentPartsNumber(child_no, parts_no);
					const char *child_name = child_comp->name ?
						child_comp->name->text : "";
					apply_component_props(child_comp, child_no, child_name, act);
				}
			}
			break;
		}
	}
}

// Load a .pactex from the PACT archive and return the parsed EX data
static struct ex *load_pactex(const char *filename)
{
	char path[512];
	snprintf(path, sizeof(path), "%s.pactex", filename);
	for (char *p = path; *p; p++) {
		if (*p == '/') *p = '\\';
	}

	struct archive_data *dfile = asset_get_by_name(ASSET_PACT, path, NULL);
	if (!dfile) {
		snprintf(path, sizeof(path), "%s", filename);
		for (char *p = path; *p; p++) {
			if (*p == '/') *p = '\\';
		}
		dfile = asset_get_by_name(ASSET_PACT, path, NULL);
	}
	if (!dfile) return NULL;
	if (!archive_load_file(dfile)) {
		archive_free_data(dfile);
		return NULL;
	}

	struct ex *ex = ex_read(dfile->data, dfile->size);
	archive_free_data(dfile);
	return ex;
}

// Build the parts tree from loaded EX data for an activity
static void build_activity_parts(struct activity *act)
{
	if (!act->ex_data || act->nr_parts > 0) return;  // already built or no data

	if (act->ex_data->nr_blocks > 0 && act->ex_data->blocks[0].val.type == EX_TREE) {
		struct ex_tree *root = act->ex_data->blocks[0].val.tree;
		if (!root->is_leaf && root->nr_children > 0) {
			struct ex_tree *component = &root->children[0];
			if (!component->is_leaf && component->nr_children >= 10) {
				act->root_parts_no = PE_GetFreeNumber();
				const char *root_name = component->name ?
					component->name->text : "";
				apply_component_props(component, act->root_parts_no, root_name, act);
				WARNING("Activity '%s': built %d parts (root=%d '%s')",
					act->name, act->nr_parts, act->root_parts_no, root_name);
				for (int i = 0; i < act->nr_parts && i < 20; i++) {
					WARNING("  part[%d]: '%s' = PE#%d",
						i, act->parts[i].name, act->parts[i].parts_no);
				}
			}
		}
	}
}

// [29] bool ReadActivityFile(string Name, string FileName, bool Edit)
// The game calls this and immediately queries parts — so we must build parts here.
static bool PartsEngine_ReadActivityFile(struct string *name, struct string *filename, bool edit)
{
	// Find existing or allocate new activity slot
	struct activity *act = find_activity(name->text);
	if (!act) {
		for (int i = 0; i < MAX_ACTIVITIES; i++) {
			if (!activities[i].in_use) {
				act = &activities[i];
				break;
			}
		}
	}
	if (!act) {
		WARNING("ReadActivityFile: no free activity slots for '%s'", name->text);
		return false;
	}

	struct ex *ex = load_pactex(filename->text);
	if (!ex) {
		WARNING("ReadActivityFile: failed to load '%s'", filename->text);
		return false;
	}

	// Clean up any previous data in this slot
	if (act->in_use) {
		for (int i = 0; i < act->nr_parts; i++)
			PE_ReleaseParts(act->parts[i].parts_no);
		if (act->ex_data)
			ex_free(act->ex_data);
	}

	memset(act, 0, sizeof(*act));
	act->in_use = true;
	snprintf(act->name, sizeof(act->name), "%s", name->text);
	act->ex_data = ex;

	WARNING("ReadActivityFile: loaded '%s' (%d blocks)", name->text, ex->nr_blocks);

	// Build parts tree immediately — game queries parts right after loading
	build_activity_parts(act);
	return true;
}

// [25] bool CreateActivity(string Name)
// Usually parts are already built by ReadActivityFile. This ensures they exist.
static bool PartsEngine_CreateActivity(struct string *name)
{
	struct activity *act = find_activity(name->text);
	if (!act || !act->ex_data) {
		WARNING("CreateActivity: no data for '%s'", name->text);
		return false;
	}

	if (act->nr_parts == 0)
		build_activity_parts(act);

	return act->nr_parts > 0;
}

// [24] bool IsExistActivity(string Name)
static bool PartsEngine_IsExistActivity(struct string *name)
{
	return find_activity(name->text) != NULL;
}

// [31] bool IsExistActivityFile(string FileName)
static bool PartsEngine_IsExistActivityFile(struct string *filename)
{
	char path[512];
	snprintf(path, sizeof(path), "%s.pactex", filename->text);
	for (char *p = path; *p; p++) {
		if (*p == '/') *p = '\\';
	}
	struct archive_data *dfile = asset_get_by_name(ASSET_PACT, path, NULL);
	if (dfile) {
		archive_free_data(dfile);
		return true;
	}
	return false;
}

// [26] bool ReleaseActivity(string Name, wrap[array[int]] EraseNumberList)
static bool PartsEngine_ReleaseActivity(struct string *name, struct page *erase_list)
{
	struct activity *act = find_activity(name->text);
	if (!act) return false;

	// Release all PE parts
	for (int i = 0; i < act->nr_parts; i++) {
		PE_ReleaseParts(act->parts[i].parts_no);
	}

	if (act->ex_data)
		ex_free(act->ex_data);

	WARNING("ReleaseActivity: '%s' (%d parts released)", name->text, act->nr_parts);
	memset(act, 0, sizeof(*act));
	return true;
}

// [39] int GetActivityPartsNumber(string Name, string PartsName)
static int PartsEngine_GetActivityPartsNumber(struct string *name, struct string *parts_name)
{
	struct activity *act = find_activity(name->text);
	if (!act) {
		WARNING("GetActivityPartsNumber: activity '%s' not found", name->text);
		return -1;
	}
	for (int i = 0; i < act->nr_parts; i++) {
		if (!strcmp(act->parts[i].name, parts_name->text))
			return act->parts[i].parts_no;
	}
	WARNING("GetActivityPartsNumber: part '%s' not found in '%s' (%d parts registered)",
		parts_name->text, name->text, act->nr_parts);
	return -1;
}

// [37] bool IsExistActivityPartsByName(string Name, string PartsName)
static bool PartsEngine_IsExistActivityPartsByName(struct string *name, struct string *parts_name)
{
	struct activity *act = find_activity(name->text);
	if (!act) return false;
	for (int i = 0; i < act->nr_parts; i++) {
		if (!strcmp(act->parts[i].name, parts_name->text))
			return true;
	}
	return false;
}

// [38] bool IsExistActivityPartsByNumber(string Name, int Number)
static bool PartsEngine_IsExistActivityPartsByNumber(struct string *name, int number)
{
	struct activity *act = find_activity(name->text);
	if (!act) return false;
	for (int i = 0; i < act->nr_parts; i++) {
		if (act->parts[i].parts_no == number)
			return true;
	}
	return false;
}

// [35] int NumofActivityParts(string Name)
static int PartsEngine_NumofActivityParts(struct string *name)
{
	struct activity *act = find_activity(name->text);
	return act ? act->nr_parts : 0;
}

// [40] string GetActivityPartsName(string Name, int Number)
static struct string *PartsEngine_GetActivityPartsName(struct string *name, int number)
{
	struct activity *act = find_activity(name->text);
	if (act) {
		for (int i = 0; i < act->nr_parts; i++) {
			if (act->parts[i].parts_no == number)
				return cstr_to_string(act->parts[i].name);
		}
	}
	return string_ref(&EMPTY_STRING);
}

// [32] bool AddActivityParts(string Name, string PartsName, int Number)
static bool PartsEngine_AddActivityParts(struct string *name, struct string *parts_name, int number)
{
	struct activity *act = find_activity(name->text);
	if (!act) return false;
	register_activity_part(act, number, parts_name->text);
	return true;
}

// [33] bool RemoveActivityParts(string Name, string PartsName)
static bool PartsEngine_RemoveActivityParts(struct string *name, struct string *parts_name)
{
	struct activity *act = find_activity(name->text);
	if (!act) return false;
	for (int i = 0; i < act->nr_parts; i++) {
		if (!strcmp(act->parts[i].name, parts_name->text)) {
			// Shift remaining entries
			for (int j = i; j < act->nr_parts - 1; j++)
				act->parts[j] = act->parts[j + 1];
			act->nr_parts--;
			return true;
		}
	}
	return false;
}

// [34] bool RemoveAllActivityParts(string Name)
static bool PartsEngine_RemoveAllActivityParts(struct string *name)
{
	struct activity *act = find_activity(name->text);
	if (!act) return false;
	act->nr_parts = 0;
	return true;
}

// [41] int GetActivityEXID(string Name)
static int PartsEngine_GetActivityEXID(struct string *name)
{
	// Return the activity slot index as the EX ID
	for (int i = 0; i < MAX_ACTIVITIES; i++) {
		if (activities[i].in_use && !strcmp(activities[i].name, name->text))
			return i;
	}
	return -1;
}

static void PartsEngine_PreLink(void);

HLL_LIBRARY(PartsEngine,
	    HLL_EXPORT(_PreLink, PartsEngine_PreLink),
	    // for versions without PartsEngine.Init
	    HLL_EXPORT(_ModuleInit, PartsEngine_ModuleInit),
	    HLL_EXPORT(_ModuleFini, PartsEngine_ModuleFini),
	    // Oyako Rankan
	    HLL_EXPORT(Init, PE_Init),
	    HLL_EXPORT(Update, PartsEngine_Update),
	    HLL_TODO_EXPORT(UpdateGUIController, PartsEngine_UpdateGUIController),
	    HLL_EXPORT(GetFreeSystemPartsNumber, PE_GetFreeNumber),
	    // FIXME: what is the difference?
	    HLL_EXPORT(GetFreeSystemPartsNumberNotSaved, PE_GetFreeNumber),
	    HLL_EXPORT(IsExistParts, PE_IsExist),
	    HLL_EXPORT(SetPartsCG, PE_SetPartsCG),
	    HLL_EXPORT(GetPartsCGName, PE_GetPartsCGName),
	    HLL_EXPORT(SetPartsCGSurfaceArea, PE_SetPartsCGSurfaceArea),
	    HLL_EXPORT(SetLoopCG, PE_SetLoopCG),
	    HLL_EXPORT(SetLoopCGSurfaceArea, PE_SetLoopCGSurfaceArea),
	    HLL_EXPORT(SetText, PE_SetText),
	    HLL_EXPORT(AddPartsText, PE_AddPartsText),
	    HLL_TODO_EXPORT(DeletePartsTopTextLine, PartsEngine_DeletePartsTopTextLine),
	    HLL_EXPORT(SetPartsTextSurfaceArea, PE_SetPartsTextSurfaceArea),
	    HLL_TODO_EXPORT(SetPartsTextHighlight, PartsEngine_SetPartsTextHighlight),
	    HLL_TODO_EXPORT(AddPartsTextHighlight, PartsEngine_AddPartsTextHighlight),
	    HLL_TODO_EXPORT(ClearPartsTextHighlight, PartsEngine_ClearPartsTextHighlight),
	    HLL_TODO_EXPORT(SetPartsTextCountReturn, PartsEngine_SetPartsTextCountReturn),
	    HLL_TODO_EXPORT(GetPartsTextCountReturn, PartsEngine_GetPartsTextCountReturn),
	    HLL_EXPORT(SetFont, PE_SetFont),
	    HLL_EXPORT(SetPartsFontType, PE_SetPartsFontType),
	    HLL_EXPORT(SetPartsFontSize, PE_SetPartsFontSize),
	    HLL_EXPORT(SetPartsFontColor, PE_SetPartsFontColor),
	    HLL_EXPORT(SetPartsFontBoldWeight, PE_SetPartsFontBoldWeight),
	    HLL_EXPORT(SetPartsFontEdgeColor, PE_SetPartsFontEdgeColor),
	    HLL_EXPORT(SetPartsFontEdgeWeight, PE_SetPartsFontEdgeWeight),
	    HLL_EXPORT(SetTextCharSpace, PE_SetTextCharSpace),
	    HLL_EXPORT(SetTextLineSpace, PE_SetTextLineSpace),
	    HLL_EXPORT(SetHGaugeCG, PE_SetHGaugeCG),
	    HLL_EXPORT(SetHGaugeRate, PE_SetHGaugeRate_int),
	    HLL_EXPORT(SetVGaugeCG, PE_SetVGaugeCG),
	    HLL_EXPORT(SetVGaugeRate, PE_SetVGaugeRate_int),
	    HLL_EXPORT(SetHGaugeSurfaceArea, PE_SetHGaugeSurfaceArea),
	    HLL_EXPORT(SetVGaugeSurfaceArea, PE_SetVGaugeSurfaceArea),
	    HLL_EXPORT(SetNumeralCG, PE_SetNumeralCG),
	    HLL_EXPORT(SetNumeralLinkedCGNumberWidthWidthList, PE_SetNumeralLinkedCGNumberWidthWidthList),
	    HLL_TODO_EXPORT(SetNumeralFont, PartsEngine_SetNumeralFont),
	    HLL_EXPORT(SetNumeralNumber, PE_SetNumeralNumber),
	    HLL_EXPORT(SetNumeralShowComma, PE_SetNumeralShowComma),
	    HLL_EXPORT(SetNumeralSpace, PE_SetNumeralSpace),
	    HLL_EXPORT(SetNumeralLength, PE_SetNumeralLength),
	    HLL_EXPORT(SetNumeralSurfaceArea, PE_SetNumeralSurfaceArea),
	    HLL_EXPORT(SetPartsRectangleDetectionSize, PE_SetPartsRectangleDetectionSize),
	    HLL_TODO_EXPORT(SetPartsRectangleDetectionSurfaceArea, PartsEngine_SetPartsRectangleDetectionSurfaceArea),
	    HLL_EXPORT(SetPartsCGDetectionSize, PE_SetPartsCGDetectionSize),
	    HLL_TODO_EXPORT(SetPartsCGDetectionSurfaceArea, PartsEngine_SetPartsCGDetectionSurfaceArea),
	    HLL_EXPORT(SetPartsFlash, PE_SetPartsFlash),
	    HLL_EXPORT(IsPartsFlashEnd, PE_IsPartsFlashEnd),
	    HLL_EXPORT(GetPartsFlashCurrentFrameNumber, PE_GetPartsFlashCurrentFrameNumber),
	    HLL_EXPORT(BackPartsFlashBeginFrame, PE_BackPartsFlashBeginFrame),
	    HLL_EXPORT(StepPartsFlashFinalFrame, PE_StepPartsFlashFinalFrame),
	    HLL_TODO_EXPORT(SetPartsFlashSurfaceArea, PE_SetPartsFlashSurfaceArea),
	    HLL_EXPORT(SetPartsFlashAndStop, PE_SetPartsFlashAndStop),
	    HLL_EXPORT(StopPartsFlash, PE_StopPartsFlash),
	    HLL_EXPORT(StartPartsFlash, PE_StartPartsFlash),
	    HLL_EXPORT(GoFramePartsFlash, PE_GoFramePartsFlash),
	    HLL_EXPORT(GetPartsFlashEndFrame, PE_GetPartsFlashEndFrame),
	    HLL_TODO_EXPORT(ExistsFlashFile, PE_ExistsFlashFile),
	    HLL_EXPORT(ClearPartsConstructionProcess, PE_ClearPartsConstructionProcess),
	    HLL_EXPORT(AddCreateToPartsConstructionProcess, PE_AddCreateToPartsConstructionProcess),
	    HLL_EXPORT(AddCreatePixelOnlyToPartsConstructionProcess, PE_AddCreatePixelOnlyToPartsConstructionProcess),
	    HLL_EXPORT(AddCreateCGToProcess, PE_AddCreateCGToProcess),
	    HLL_EXPORT(AddFillToPartsConstructionProcess, PE_AddFillToPartsConstructionProcess),
	    HLL_EXPORT(AddFillAlphaColorToPartsConstructionProcess, PE_AddFillAlphaColorToPartsConstructionProcess),
	    HLL_EXPORT(AddFillAMapToPartsConstructionProcess, PE_AddFillAMapToPartsConstructionProcess),
	    HLL_TODO_EXPORT(AddFillWithAlphaToPartsConstructionProcess, PartsEngine_AddFillWithAlphaToPartsConstructionProcess),
	    HLL_TODO_EXPORT(AddFillGradationHorizonToPartsConstructionProcess, PartsEngine_AddFillGradationHorizonToPartsConstructionProcess),
	    HLL_EXPORT(AddDrawRectToPartsConstructionProcess, PE_AddDrawRectToPartsConstructionProcess),
	    HLL_EXPORT(AddDrawCutCGToPartsConstructionProcess, PartsEngine_AddDrawCutCGToPartsConstructionProcess_old),
	    HLL_EXPORT(AddCopyCutCGToPartsConstructionProcess, PartsEngine_AddCopyCutCGToPartsConstructionProcess_old),
	    HLL_TODO_EXPORT(AddGrayFilterToPartsConstructionProcess, PartsEngine_AddGrayFilterToPartsConstructionProcess),
	    HLL_TODO_EXPORT(AddAddFilterToPartsConstructionProcess, PartsEngine_AddAddFilterToPartsConstructionProcess),
	    HLL_TODO_EXPORT(AddMulFilterToPartsConstructionProcess, PartsEngine_AddMulFilterToPartsConstructionProcess),
	    HLL_EXPORT(BuildPartsConstructionProcess, PE_BuildPartsConstructionProcess),
	    HLL_EXPORT(AddDrawTextToPartsConstructionProcess, PE_AddDrawTextToPartsConstructionProcess),
	    HLL_EXPORT(AddCopyTextToPartsConstructionProcess, PE_AddCopyTextToPartsConstructionProcess),
	    HLL_EXPORT(SetPartsConstructionSurfaceArea, PE_SetPartsConstructionSurfaceArea),
	    HLL_EXPORT(ReleaseParts, PE_ReleaseParts),
	    HLL_EXPORT(ReleaseAllParts, PE_ReleaseAllParts),
	    HLL_EXPORT(ReleaseAllPartsWithoutSystem, PE_ReleaseAllPartsWithoutSystem),
	    HLL_EXPORT(SetPos, PE_SetPos),
	    HLL_EXPORT(SetZ, PE_SetZ),
	    HLL_EXPORT(SetShow, PE_SetShow),
	    HLL_EXPORT(SetAlpha, PE_SetAlpha),
	    HLL_EXPORT(SetPartsDrawFilter, PE_SetPartsDrawFilter),
	    HLL_EXPORT(SetAddColor, PE_SetAddColor),
	    HLL_EXPORT(SetMultiplyColor, PE_SetMultiplyColor),
	    HLL_EXPORT(SetClickable, PE_SetClickable),
	    HLL_EXPORT(SetSpeedupRateByMessageSkip, PE_SetSpeedupRateByMessageSkip),
	    HLL_TODO_EXPORT(SetResetTimerByChangeInputStatus, PartsEngine_SetResetTimerByChangeInputStatus),
	    HLL_EXPORT(GetPartsX, PE_GetPartsX),
	    HLL_EXPORT(GetPartsY, PE_GetPartsY),
	    HLL_EXPORT(GetPartsZ, PE_GetPartsZ),
	    HLL_EXPORT(GetPartsShow, PE_GetPartsShow),
	    HLL_EXPORT(GetPartsAlpha, PE_GetPartsAlpha),
	    HLL_TODO_EXPORT(GetAddColor, PartsEngine_GetAddColor),
	    HLL_TODO_EXPORT(GetMultiplyColor, PartsEngine_GetMultiplyColor),
	    HLL_EXPORT(GetPartsClickable, PE_GetPartsClickable),
	    HLL_TODO_EXPORT(GetPartsSpeedupRateByMessageSkip, PartsEngine_GetPartsSpeedupRateByMessageSkip),
	    HLL_TODO_EXPORT(GetResetTimerByChangeInputStatus, PartsEngine_GetResetTimerByChangeInputStatus),
	    HLL_EXPORT(GetPartsUpperLeftPosX, PE_GetPartsUpperLeftPosX),
	    HLL_EXPORT(GetPartsUpperLeftPosY, PE_GetPartsUpperLeftPosY),
	    HLL_EXPORT(GetPartsWidth, PE_GetPartsWidth),
	    HLL_EXPORT(GetPartsHeight, PE_GetPartsHeight),
	    HLL_EXPORT(SetInputState, PE_SetInputState),
	    HLL_EXPORT(GetInputState, PE_GetInputState),
	    HLL_EXPORT(SetPartsOriginPosMode, PE_SetPartsOriginPosMode),
	    HLL_EXPORT(GetPartsOriginPosMode, PE_GetPartsOriginPosMode),
	    HLL_EXPORT(SetParentPartsNumber, PE_SetParentPartsNumber),
	    HLL_EXPORT(SetPartsGroupNumber, PE_SetPartsGroupNumber),
	    HLL_EXPORT(SetPartsGroupDecideOnCursor, PE_SetPartsGroupDecideOnCursor),
	    HLL_EXPORT(SetPartsGroupDecideClick, PE_SetPartsGroupDecideClick),
	    HLL_EXPORT(SetOnCursorShowLinkPartsNumber, PE_SetOnCursorShowLinkPartsNumber),
	    HLL_EXPORT(SetPartsMessageWindowShowLink, PE_SetPartsMessageWindowShowLink),
	    HLL_EXPORT(GetPartsMessageWindowShowLink, PE_GetPartsMessageWindowShowLink),
	    HLL_EXPORT(AddMotionPos, PE_AddMotionPos_curve),
	    HLL_EXPORT(AddMotionAlpha, PE_AddMotionAlpha_curve),
	    HLL_TODO_EXPORT(AddMotionCG, PartsEngine_AddMotionCG),
	    HLL_EXPORT(AddMotionHGaugeRate, PE_AddMotionHGaugeRate_curve),
	    HLL_EXPORT(AddMotionVGaugeRate, PE_AddMotionVGaugeRate_curve),
	    HLL_EXPORT(AddMotionNumeralNumber, PE_AddMotionNumeralNumber_curve),
	    HLL_EXPORT(AddMotionMagX, PE_AddMotionMagX_curve),
	    HLL_EXPORT(AddMotionMagY, PE_AddMotionMagY_curve),
	    HLL_EXPORT(AddMotionRotateX, PE_AddMotionRotateX_curve),
	    HLL_EXPORT(AddMotionRotateY, PE_AddMotionRotateY_curve),
	    HLL_EXPORT(AddMotionRotateZ, PE_AddMotionRotateZ_curve),
	    HLL_EXPORT(AddMotionVibrationSize, PE_AddMotionVibrationSize),
	    HLL_EXPORT(AddWholeMotionVibrationSize, PE_AddWholeMotionVibrationSize),
	    HLL_EXPORT(AddMotionSound, PE_AddMotionSound),
	    HLL_TODO_EXPORT(SetSoundNumber, PartsEngine_SetSoundNumber),
	    HLL_TODO_EXPORT(GetSoundNumber, PartsEngine_GetSoundNumber),
	    HLL_EXPORT(SetClickMissSoundNumber, PE_SetClickMissSoundNumber),
	    HLL_TODO_EXPORT(GetClickMissSoundNumber, PartsEngine_GetClickMissSoundNumber),
	    HLL_EXPORT(BeginMotion, PE_BeginMotion),
	    HLL_EXPORT(EndMotion, PE_EndMotion),
	    HLL_EXPORT(IsMotion, PE_IsMotion),
	    HLL_EXPORT(SeekEndMotion, PE_SeekEndMotion),
	    HLL_EXPORT(UpdateMotionTime, PE_UpdateMotionTime),
	    HLL_EXPORT(BeginInput, PE_BeginInput),
	    HLL_EXPORT(EndInput, PE_EndInput),
	    HLL_EXPORT(GetClickPartsNumber, PE_GetClickPartsNumber),
	    HLL_TODO_EXPORT(GetFocusPartsNumber, PartsEngine_GetFocusPartsNumber),
	    HLL_TODO_EXPORT(SetFocusPartsNumber, PartsEngine_SetFocusPartsNumber),
	    HLL_TODO_EXPORT(PushGUIController, PartsEngine_PushGUIController),
	    HLL_TODO_EXPORT(PopGUIController, PartsEngine_PopGUIController),
	    HLL_EXPORT(SetPartsMagX, PE_SetPartsMagX),
	    HLL_EXPORT(SetPartsMagY, PE_SetPartsMagY),
	    HLL_EXPORT(SetPartsRotateX, PE_SetPartsRotateX),
	    HLL_EXPORT(SetPartsRotateY, PE_SetPartsRotateY),
	    HLL_EXPORT(SetPartsRotateZ, PE_SetPartsRotateZ),
	    HLL_EXPORT(SetPartsAlphaClipperPartsNumber, PE_SetPartsAlphaClipperPartsNumber),
	    HLL_EXPORT(SetPartsPixelDecide, PE_SetPartsPixelDecide),
	    HLL_EXPORT(IsCursorIn, PE_IsCursorIn),
	    HLL_EXPORT(SetThumbnailReductionSize, PE_SetThumbnailReductionSize),
	    HLL_EXPORT(SetThumbnailMode, PE_SetThumbnailMode),
	    HLL_EXPORT(Save, PE_Save),
	    HLL_EXPORT(SaveWithoutHideParts, PE_SaveWithoutHideParts),
	    HLL_EXPORT(Load, PE_Load),
	    HLL_EXPORT(PopMessage, PartsEngine_PopMessage),
	    HLL_EXPORT(GetMessageType, PartsEngine_GetMessageType),
	    HLL_EXPORT(GetMessagePartsNumber, PartsEngine_GetMessagePartsNumber),
	    HLL_EXPORT(GetMessageVariableInt, PartsEngine_GetMessageVariableInt),
	    HLL_EXPORT(SetActiveController, PartsEngine_SetActiveController),
	    HLL_EXPORT(GetActiveController, PartsEngine_GetActiveController),
	    HLL_EXPORT(UpdateComponent, PartsEngine_UpdateComponent),
	    // Component type and child management
	    HLL_EXPORT(SetComponentType, PartsEngine_SetComponentType),
	    HLL_EXPORT(GetComponentType, PartsEngine_GetComponentType),
	    HLL_EXPORT(NumofChild, PartsEngine_NumofChild),
	    // Activity system (v14 Dohna Dohna) — all use string Name as key
	    HLL_EXPORT(IsExistActivity, PartsEngine_IsExistActivity),
	    HLL_EXPORT(CreateActivity, PartsEngine_CreateActivity),
	    HLL_EXPORT(ReleaseActivity, PartsEngine_ReleaseActivity),
	    HLL_EXPORT(ReadActivityFile, PartsEngine_ReadActivityFile),
	    HLL_EXPORT(IsExistActivityFile, PartsEngine_IsExistActivityFile),
	    HLL_EXPORT(AddActivityParts, PartsEngine_AddActivityParts),
	    HLL_EXPORT(RemoveActivityParts, PartsEngine_RemoveActivityParts),
	    HLL_EXPORT(RemoveAllActivityParts, PartsEngine_RemoveAllActivityParts),
	    HLL_EXPORT(NumofActivityParts, PartsEngine_NumofActivityParts),
	    HLL_EXPORT(IsExistActivityPartsByName, PartsEngine_IsExistActivityPartsByName),
	    HLL_EXPORT(IsExistActivityPartsByNumber, PartsEngine_IsExistActivityPartsByNumber),
	    HLL_EXPORT(GetActivityPartsNumber, PartsEngine_GetActivityPartsNumber),
	    HLL_EXPORT(GetActivityPartsName, PartsEngine_GetActivityPartsName),
	    HLL_EXPORT(GetActivityEXID, PartsEngine_GetActivityEXID)
	    );

static struct ain_hll_function *get_fun(int libno, const char *name)
{
	int fno = ain_get_library_function(ain, libno, name);
	return fno >= 0 ? &ain->libraries[libno].functions[fno] : NULL;
}

static void PartsEngine_PreLink(void)
{
	struct ain_hll_function *fun;
	int libno = ain_get_library(ain, "PartsEngine");
	assert(libno >= 0);

	fun = get_fun(libno, "AddDrawCutCGToPartsConstructionProcess");
	if (fun && fun->nr_arguments == 12) {
		static_library_replace(&lib_PartsEngine, "AddDrawCutCGToPartsConstructionProcess",
				PE_AddDrawCutCGToPartsConstructionProcess);
	}
	fun = get_fun(libno, "AddCopyCutCGToPartsConstructionProcess");
	if (fun && fun->nr_arguments == 12) {
		static_library_replace(&lib_PartsEngine, "AddCopyCutCGToPartsConstructionProcess",
				PE_AddCopyCutCGToPartsConstructionProcess);
	}
}
