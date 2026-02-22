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
#include <string.h>
#include <stdio.h>

#include "system4/ain.h"
#include "system4/archive.h"
#include "system4/ex.h"
#include "system4/string.h"
#include "system4/utfsjis.h"

#include "asset_manager.h"
#include "parts.h"
#include "../parts/parts_internal.h"
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

/* v14 Activity system.
 * Activities group parts together and are loaded from .pactex files
 * (which use the same .ex format). The game's AIN code walks the
 * component tree using GetActivityPartsNumber, GetComponentType,
 * NumofChild, and GetChild. We parse the .pactex tree to create
 * PE parts entries with correct component types and parent-child
 * relationships so the tree walk terminates naturally. */

struct activity_part {
	char name[256];
	int number;
};

#define MAX_ACTIVITY_PARTS 512
#define MAX_ACTIVITIES 128

struct activity {
	char name[256];
	struct activity_part parts[MAX_ACTIVITY_PARTS];
	int nr_parts;
};

static struct activity activities[MAX_ACTIVITIES];
static int nr_activities = 0;

/* Parts numbers for activity components use a high range to avoid
 * collision with game-allocated parts (which start from low numbers). */
#define ACTIVITY_PARTS_BASE 900000
static int next_activity_parts_no = ACTIVITY_PARTS_BASE;

static int alloc_activity_parts_no(void)
{
	return next_activity_parts_no++;
}

/* Pactex files in this CN version use GBK encoding for field names.
 * We match against raw GBK byte patterns. */

/* GBK byte sequences for key pactex field names. */
static const char GBK_BUJIAN[] = "\xb2\xbf\xbc\xfe";    /* 部件 (CN: component) */
static const char GBK_BUPIN[]  = "\xb2\xbf\xc6\xb7";    /* 部品 (JP: parts) */

/* --- Pactex tree parser --- */

/* Format SJIS bytes as hex for debugging. */
static const char *hex_name(const char *name)
{
	static char buf[128];
	int pos = 0;
	for (int i = 0; name[i] && pos < 120; i++)
		pos += snprintf(buf + pos, sizeof(buf) - pos, "%02x ",
				(unsigned char)name[i]);
	if (pos > 0) buf[pos - 1] = '\0';
	return buf;
}

static void dump_ex_tree(struct ex_tree *tree, int depth)
{
	if (depth > 4) return;
	char indent[32] = "";
	int n = depth * 2;
	if (n > 30) n = 30;
	for (int i = 0; i < n; i++) indent[i] = ' ';
	indent[n] = '\0';

	if (tree->is_leaf) {
		struct ex_value *v = &tree->leaf.value;
		switch (v->type) {
		case EX_INT:
			WARNING("%sleaf [%s] = %d", indent,
				hex_name(tree->name->text), v->i);
			break;
		case EX_STRING:
			WARNING("%sleaf [%s] = str", indent,
				hex_name(tree->name->text));
			break;
		case EX_LIST:
			WARNING("%sleaf [%s] = list[%u]", indent,
				hex_name(tree->name->text),
				v->list ? v->list->nr_items : 0);
			break;
		default:
			WARNING("%sleaf [%s] type=%d", indent,
				hex_name(tree->name->text), v->type);
			break;
		}
	} else {
		WARNING("%sbranch [%s] (%u children)", indent,
			hex_name(tree->name->text), tree->nr_children);
		for (unsigned i = 0; i < tree->nr_children && i < 8; i++)
			dump_ex_tree(&tree->children[i], depth + 1);
		if (tree->nr_children > 8)
			WARNING("%s  ... +%u more children", indent,
				tree->nr_children - 8);
	}
}

/* Check if tree node name contains a byte substring (raw SJIS). */
static bool pactex_name_contains(struct ex_tree *node, const char *pattern)
{
	if (!node->name) return false;
	return strstr(node->name->text, pattern) != NULL;
}

/* Find a direct child branch/leaf by byte pattern in name. */
static struct ex_tree *pactex_find_child(struct ex_tree *parent, const char *pattern)
{
	if (parent->is_leaf) return NULL;
	for (unsigned i = 0; i < parent->nr_children; i++) {
		if (pactex_name_contains(&parent->children[i], pattern))
			return &parent->children[i];
	}
	return NULL;
}

/* Find the "部件"/"部品" (parts/components) branch among children.
 * Uses GBK name match first, then structural heuristic as fallback. */
static struct ex_tree *pactex_find_buhin(struct ex_tree *parent)
{
	if (parent->is_leaf) return NULL;

	/* Try GBK name match: 部件 (CN) or 部品 (JP) */
	struct ex_tree *found = pactex_find_child(parent, GBK_BUJIAN);
	if (found && !found->is_leaf) return found;
	found = pactex_find_child(parent, GBK_BUPIN);
	if (found && !found->is_leaf) return found;

	/* Structural heuristic: find a non-leaf child whose children
	 * are predominantly non-leaf branches (component definitions). */
	for (unsigned i = 0; i < parent->nr_children; i++) {
		struct ex_tree *child = &parent->children[i];
		if (child->is_leaf || child->nr_children == 0) continue;
		int branches = 0, leaves = 0;
		for (unsigned j = 0; j < child->nr_children; j++) {
			if (child->children[j].is_leaf) leaves++;
			else branches++;
		}
		if (branches > 0 && branches >= leaves)
			return child;
	}
	return NULL;
}

/* Recursively create PE parts entries from a pactex component branch.
 * Component type is determined STRUCTURALLY:
 *   - Has a "部件" sub-branch → type 17 (UserComponent / container)
 *   - No children → type 1 (leaf component, e.g. sprite)
 * This avoids unreliable parsing of GBK field names for type detection. */
static void pactex_create_component(struct activity *act, struct ex_tree *node,
		int parent_no, int depth)
{
	if (node->is_leaf || depth > 15) return;

	int parts_no = alloc_activity_parts_no();
	struct parts *p = parts_get(parts_no);

	/* Register in activity by name (raw GBK bytes) */
	if (act->nr_parts < MAX_ACTIVITY_PARTS) {
		struct activity_part *ap = &act->parts[act->nr_parts++];
		snprintf(ap->name, sizeof(ap->name), "%s", node->name->text);
		ap->number = parts_no;
	}

	/* Set parent-child relationship */
	if (parent_no >= 0)
		PE_SetParentPartsNumber(parts_no, parent_no);

	/* Find child components branch — determines if this is a container */
	struct ex_tree *buhin = pactex_find_buhin(node);

	/* Component type: container (17) if has children, leaf (1) otherwise.
	 * Type 17 = UserComponent triggers child recursion in the game's tree walk.
	 * Type 1 = leaf component, no children → recursion stops. */
	if (buhin && buhin->nr_children > 0)
		p->component_type = 17;  /* UserComponent (container) */
	else
		p->component_type = 1;   /* Sprite (leaf) */

	/* Recurse into child component definitions */
	if (buhin) {
		for (unsigned i = 0; i < buhin->nr_children; i++) {
			struct ex_tree *sub = &buhin->children[i];
			if (!sub->is_leaf) {
				pactex_create_component(act, sub, parts_no, depth + 1);
			}
		}
	}
}

/* Parse pactex EX data and populate activity with parts entries. */
static bool pactex_load(struct activity *act, struct ex *ex)
{
	/* Find the tree block — should be block 0 ("アクティビティ") */
	struct ex_tree *tree = NULL;
	for (unsigned i = 0; i < ex->nr_blocks; i++) {
		if (ex->blocks[i].val.type == EX_TREE) {
			tree = ex->blocks[i].val.tree;
			WARNING("pactex: tree block '%s' (%u children)",
				ex->blocks[i].name->text, tree ? tree->nr_children : 0);
			break;
		}
	}
	if (!tree || tree->is_leaf || tree->nr_children == 0) {
		WARNING("pactex: no valid tree block found (nr_blocks=%u)", ex->nr_blocks);
		return false;
	}

	/* Dump tree structure for debugging (first load only) */
	static int dump_count = 0;
	if (dump_count++ < 3)
		dump_ex_tree(tree, 0);

	/* The tree root has one branch per activity variant (usually just one).
	 * Create a root PE parts entry for the first branch. */
	struct ex_tree *root_branch = &tree->children[0];
	if (root_branch->is_leaf) {
		WARNING("pactex: root branch is a leaf, aborting");
		return false;
	}

	int root_no = alloc_activity_parts_no();
	struct parts *root = parts_get(root_no);

	/* Root is always a container (type 17) since it has children */
	struct ex_tree *root_buhin = pactex_find_buhin(root_branch);
	root->component_type = (root_buhin && root_buhin->nr_children > 0) ? 17 : 0;

	/* Register root with empty name (sentinel for fallback) and actual name */
	if (act->nr_parts < MAX_ACTIVITY_PARTS) {
		struct activity_part *ap = &act->parts[act->nr_parts++];
		ap->name[0] = '\0';
		ap->number = root_no;
	}
	if (act->nr_parts < MAX_ACTIVITY_PARTS) {
		struct activity_part *ap = &act->parts[act->nr_parts++];
		snprintf(ap->name, sizeof(ap->name), "%s", root_branch->name->text);
		ap->number = root_no;
	}

	/* Process children from the root's component branch */
	struct ex_tree *buhin = root_buhin;
	if (buhin && !buhin->is_leaf) {
		WARNING("pactex: root has %u child components", buhin->nr_children);
		for (unsigned i = 0; i < buhin->nr_children; i++) {
			struct ex_tree *sub = &buhin->children[i];
			if (!sub->is_leaf) {
				pactex_create_component(act, sub, root_no, 1);
			}
		}
	} else {
		WARNING("pactex: no child components found in root '%s'",
			root_branch->name->text);
	}

	WARNING("pactex: loaded %d parts for activity '%s'", act->nr_parts, act->name);
	return true;
}

/* --- Activity management --- */

static int find_activity_idx(const char *name)
{
	for (int i = 0; i < nr_activities; i++) {
		if (!strcmp(activities[i].name, name))
			return i;
	}
	return -1;
}

static int find_activity(struct string *name)
{
	return find_activity_idx(name->text);
}

static bool PartsEngine_IsExistActivity(struct string *name)
{
	return find_activity(name) >= 0;
}

static bool PartsEngine_CreateActivity(struct string *name)
{
	if (find_activity(name) >= 0)
		return true;
	if (nr_activities >= MAX_ACTIVITIES)
		return false;
	struct activity *act = &activities[nr_activities];
	snprintf(act->name, sizeof(act->name), "%s", name->text);
	act->nr_parts = 0;
	nr_activities++;
	return true;
}

static bool PartsEngine_ReleaseActivity(struct string *name, int erase_list)
{
	int idx = find_activity(name);
	if (idx < 0) return false;
	if (idx < nr_activities - 1)
		activities[idx] = activities[nr_activities - 1];
	nr_activities--;
	return true;
}

static bool PartsEngine_ReadActivityFile(struct string *name, struct string *filename, bool edit)
{
	PartsEngine_CreateActivity(name);
	int aidx = find_activity(name);
	if (aidx < 0)
		return false;

	struct activity *act = &activities[aidx];

	/* Try to load .pactex from the Pact archive.
	 * The game passes filenames like "SceneLogo" or paths like
	 * "Scene/20_Title/Title/SceneLogo". Archive entries are "SceneLogo.pactex". */
	const char *fname = filename->text;
	const char *base = strrchr(fname, '/');
	base = base ? base + 1 : fname;

	struct archive_data *dfile = NULL;
	char pactex_name[512];

	/* Try: basename.pactex */
	snprintf(pactex_name, sizeof(pactex_name), "%s.pactex", base);
	dfile = asset_get_by_name(ASSET_PACT, pactex_name, NULL);

	/* Try: full path.pactex */
	if (!dfile && base != fname) {
		snprintf(pactex_name, sizeof(pactex_name), "%s.pactex", fname);
		dfile = asset_get_by_name(ASSET_PACT, pactex_name, NULL);
	}

	/* Try: name.pactex (the activity name, not filename) */
	if (!dfile) {
		snprintf(pactex_name, sizeof(pactex_name), "%s.pactex", name->text);
		dfile = asset_get_by_name(ASSET_PACT, pactex_name, NULL);
	}

	if (dfile) {
		struct ex *ex = ex_read(dfile->data, dfile->size);
		archive_free_data(dfile);
		if (ex) {
			bool ok = pactex_load(act, ex);
			ex_free(ex);
			if (ok) {
				WARNING("ReadActivityFile('%s', '%s') → loaded %d parts from pactex",
					name->text, fname, act->nr_parts);
				return true;
			}
		} else {
			WARNING("ReadActivityFile('%s'): ex_read failed for '%s'",
				name->text, pactex_name);
		}
	} else {
		WARNING("ReadActivityFile('%s'): pactex not found (tried '%s.pactex')",
			name->text, base);
	}

	/* Fallback: create a minimal root so the game doesn't crash */
	int root_no = alloc_activity_parts_no();
	struct parts *root = parts_get(root_no);
	root->component_type = 0;
	if (act->nr_parts < MAX_ACTIVITY_PARTS) {
		struct activity_part *ap = &act->parts[act->nr_parts++];
		ap->name[0] = '\0';
		ap->number = root_no;
	}
	WARNING("ReadActivityFile('%s') → fallback root parts=%d", name->text, root_no);
	return true;
}

static bool PartsEngine_WriteActivityFile(struct string *name, struct string *filename)
{
	return true;
}

static bool PartsEngine_IsExistActivityFile(struct string *filename)
{
	const char *fname = filename->text;
	const char *base = strrchr(fname, '/');
	base = base ? base + 1 : fname;
	char pactex_name[512];
	snprintf(pactex_name, sizeof(pactex_name), "%s.pactex", base);
	return asset_exists_by_name(ASSET_PACT, pactex_name, NULL);
}

static bool PartsEngine_SaveActivityEXText(int text_slot, struct string *name)
{
	return true;
}

static bool PartsEngine_LoadActivityEXText(struct string *name, struct string *text, bool edit)
{
	return true;
}

static bool PartsEngine_AddActivityParts(struct string *name, struct string *parts_name, int number)
{
	int idx = find_activity(name);
	if (idx < 0) return false;
	struct activity *act = &activities[idx];
	if (act->nr_parts >= MAX_ACTIVITY_PARTS) return false;
	struct activity_part *ap = &act->parts[act->nr_parts++];
	snprintf(ap->name, sizeof(ap->name), "%s", parts_name->text);
	ap->number = number;
	return true;
}

static bool PartsEngine_RemoveActivityParts(struct string *name, struct string *parts_name)
{
	int idx = find_activity(name);
	if (idx < 0) return false;
	struct activity *act = &activities[idx];
	for (int i = 0; i < act->nr_parts; i++) {
		if (!strcmp(act->parts[i].name, parts_name->text)) {
			if (i < act->nr_parts - 1)
				act->parts[i] = act->parts[act->nr_parts - 1];
			act->nr_parts--;
			return true;
		}
	}
	return false;
}

static void PartsEngine_RemoveAllActivityParts(struct string *name)
{
	int idx = find_activity(name);
	if (idx >= 0)
		activities[idx].nr_parts = 0;
}

static int PartsEngine_NumofActivityParts(struct string *name)
{
	int idx = find_activity(name);
	return idx >= 0 ? activities[idx].nr_parts : 0;
}

static bool PartsEngine_GetActivityParts(int index, struct string *name, int parts_name_slot, int number_slot)
{
	int idx = find_activity(name);
	if (idx < 0) return false;
	struct activity *act = &activities[idx];
	if (index < 0 || index >= act->nr_parts) return false;
	/* TODO: set parts_name and number via wrap slots */
	return true;
}

static bool PartsEngine_IsExistActivityPartsByName(struct string *name, struct string *parts_name)
{
	int idx = find_activity(name);
	if (idx < 0) return false;
	struct activity *act = &activities[idx];
	for (int i = 0; i < act->nr_parts; i++) {
		if (!strcmp(act->parts[i].name, parts_name->text))
			return true;
	}
	return false;
}

static bool PartsEngine_IsExistActivityPartsByNumber(struct string *name, int number)
{
	int idx = find_activity(name);
	if (idx < 0) return false;
	struct activity *act = &activities[idx];
	for (int i = 0; i < act->nr_parts; i++) {
		if (act->parts[i].number == number)
			return true;
	}
	return false;
}

static int PartsEngine_GetActivityPartsNumber(struct string *name, struct string *parts_name)
{
	int idx = find_activity(name);
	if (idx < 0) return -1;
	struct activity *act = &activities[idx];

	/* If parts_name is empty, return the root (sentinel entry) */
	if (!parts_name->text[0]) {
		for (int i = 0; i < act->nr_parts; i++) {
			if (act->parts[i].name[0] == '\0' &&
			    act->parts[i].number >= ACTIVITY_PARTS_BASE)
				return act->parts[i].number;
		}
		return -1;
	}

	/* Try exact name match */
	for (int i = 0; i < act->nr_parts; i++) {
		if (act->parts[i].name[0] && !strcmp(act->parts[i].name, parts_name->text))
			return act->parts[i].number;
	}

	/* Not found — return -1, do NOT fallback to root.
	 * Returning root for unknown names causes the game's tree walk
	 * to create self-referential structures → infinite recursion. */
	static int gapn_miss = 0;
	if (gapn_miss++ < 20)
		WARNING("GetActivityPartsNumber('%s', '%s') → not found",
			name->text, parts_name->text);
	return -1;
}

static struct string *PartsEngine_GetActivityPartsName(struct string *name, int number)
{
	int idx = find_activity(name);
	if (idx >= 0) {
		struct activity *act = &activities[idx];
		for (int i = 0; i < act->nr_parts; i++) {
			if (act->parts[i].number == number)
				return cstr_to_string(act->parts[i].name);
		}
	}
	return string_ref(&EMPTY_STRING);
}

static int PartsEngine_GetActivityEXID(struct string *name) { return 0; }
static void PartsEngine_SetActivityEXText(struct string *name, struct string *text) {}
static struct string *PartsEngine_GetActivityEXText(struct string *name) { return string_ref(&EMPTY_STRING); }
static void PartsEngine_SetActivityBG(struct string *name, struct string *cg) {}
static struct string *PartsEngine_GetActivityBG(struct string *name) { return string_ref(&EMPTY_STRING); }

static void PartsEngine_AddActivityCloseParts(struct string *name, struct string *parts) {}
static bool PartsEngine_IsExistActivityCloseParts(struct string *name, struct string *parts) { return false; }
static void PartsEngine_AddActivityLockedParts(struct string *name, struct string *parts) {}
static bool PartsEngine_IsExistActivityLockedParts(struct string *name, struct string *parts) { return false; }

static void PartsEngine_SetActivityEndKey(struct string *name, int key) {}
static void PartsEngine_EraseActivityEndKey(struct string *name, int key) {}
static bool PartsEngine_IsExistActivityEndKey(struct string *name, int key) { return false; }
static int PartsEngine_NumofActivityEndKey(struct string *name) { return 0; }
static int PartsEngine_GetActivityEndKey(struct string *name, int index) { return 0; }

/* v14 Controller system */
static int PartsEngine_AddController(int index)
{
	return index;
}

static void PartsEngine_RemoveController(int erase_list, int index) {}
static void PartsEngine_MoveController(int id, int index) {}
static int PartsEngine_GetControllerIndex(int id) { return 0; }
static int PartsEngine_GetControllerID(int index) { return 0; }
static int PartsEngine_GetControllerLength(void) { return 1; }
static int PartsEngine_GetSystemOverlayController(void) { return 0; }

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

static int PartsEngine_GetActiveController(void)
{
	return 0;
}

static void PartsEngine_SetActiveController(int controller)
{
	// stub: no controller system yet
}

/* v14 Component system — maps parts to widget types and provides
 * per-component property getters/setters. Most of these delegate
 * to existing parts functions using the component's parts number. */

static void PartsEngine_SetComponentType(int number, int type, int state)
{
	struct parts *p = parts_try_get(number);
	if (p) p->component_type = type;
}

static int PartsEngine_GetComponentType(int number, int state)
{
	struct parts *p = parts_try_get(number);
	if (p) return p->component_type;
	/* Non-existent parts: return -1 to signal "not a valid component".
	 * The game's factory uses this to decide what type of wrapper to create.
	 * Returning 0 could trigger the default case which creates phantom structs. */
	return -1;
}

static void PartsEngine_SetComponentPos(int number, float x, float y)
{
	PE_SetPos(number, (int)x, (int)y);
}

static float PartsEngine_GetComponentPosX(int number)
{
	return (float)PE_GetPartsX(number);
}

static float PartsEngine_GetComponentPosY(int number)
{
	return (float)PE_GetPartsY(number);
}

static int PartsEngine_GetComponentPosZ(int number)
{
	return PE_GetPartsZ(number);
}

static void PartsEngine_SetComponentPosZ(int number, int z)
{
	PE_SetZ(number, z);
}

static void PartsEngine_SetComponentShow(int number, bool show)
{
	PE_SetShow(number, show);
}

static bool PartsEngine_IsComponentShow(int number)
{
	return PE_GetPartsShow(number);
}

static void PartsEngine_SetComponentAlpha(int number, int alpha)
{
	PE_SetAlpha(number, alpha);
}

static int PartsEngine_GetComponentAlpha(int number)
{
	return PE_GetPartsAlpha(number);
}

static void PartsEngine_SetComponentAddColor(int number, int r, int g, int b)
{
	PE_SetAddColor(number, r, g, b);
}

static void PartsEngine_SetComponentMulColor(int number, int r, int g, int b)
{
	PE_SetMultiplyColor(number, r, g, b);
}

static void PartsEngine_SetComponentMagX(int number, float mag) { PE_SetPartsMagX(number, mag); }
static void PartsEngine_SetComponentMagY(int number, float mag) { PE_SetPartsMagY(number, mag); }
static float PartsEngine_GetComponentMagX(int number) { return PE_GetPartsMagX(number); }
static float PartsEngine_GetComponentMagY(int number) { return PE_GetPartsMagY(number); }

static void PartsEngine_SetComponentRotateX(int number, float r) { PE_SetPartsRotateX(number, r); }
static void PartsEngine_SetComponentRotateY(int number, float r) { PE_SetPartsRotateY(number, r); }
static void PartsEngine_SetComponentRotateZ(int number, float r) { PE_SetPartsRotateZ(number, r); }
static float PartsEngine_GetComponentRotateX(int number) { return 0; }
static float PartsEngine_GetComponentRotateY(int number) { return 0; }
static float PartsEngine_GetComponentRotateZ(int number) { return PE_GetPartsRotateZ(number); }

static void PartsEngine_SetComponentOriginPosMode(int number, int mode) { PE_SetPartsOriginPosMode(number, mode); }
static int PartsEngine_GetComponentOriginPosMode(int number) { return PE_GetPartsOriginPosMode(number); }

static void PartsEngine_SetComponentDrawFilter(int number, int filter) { PE_SetPartsDrawFilter(number, filter); }
static int PartsEngine_GetComponentDrawFilter(int number) { return PE_GetPartsDrawFilter(number); }

/* Stubs for component properties we don't fully support yet */
static void PartsEngine_SetComponentVibrationPos(int n, float x, float y) {}
static void PartsEngine_SetComponentShowEditor(int n, bool show) {}
static bool PartsEngine_IsComponentShowEditor(int n) { return true; }
static void PartsEngine_SetComponentMessageWindowShowLink(int n, bool link) { PE_SetPartsMessageWindowShowLink(n, link); }
static bool PartsEngine_IsComponentMessageWindowShowLink(int n) { return PE_GetPartsMessageWindowShowLink(n); }
static void PartsEngine_SetComponentMessageWindowEffectLink(int n, bool link) {}
static bool PartsEngine_IsComponentMessageWindowEffectLink(int n) { return false; }
static int PartsEngine_GetComponentAddColorR(int n) { return 0; }
static int PartsEngine_GetComponentAddColorG(int n) { return 0; }
static int PartsEngine_GetComponentAddColorB(int n) { return 0; }
static void PartsEngine_SetComponentSubColorMode(int n, bool enable) {}
static bool PartsEngine_IsComponentSubColorMode(int n) { return false; }
static int PartsEngine_GetComponentMulColorR(int n) { return 255; }
static int PartsEngine_GetComponentMulColorG(int n) { return 255; }
static int PartsEngine_GetComponentMulColorB(int n) { return 255; }
static void PartsEngine_SetLockInputState(int n, bool lock) {}
static bool PartsEngine_IsLockInputState(int n) { return false; }
static void PartsEngine_SetComponentEnableClipArea(int n, bool enable) {}
static bool PartsEngine_IsComponentEnableClipArea(int n) { return false; }
static void PartsEngine_SetComponentClipArea(int n, int x, int y, int w, int h) {}
static int PartsEngine_GetComponentClipAreaPosX(int n) { return 0; }
static int PartsEngine_GetComponentClipAreaPosY(int n) { return 0; }
static int PartsEngine_GetComponentClipAreaPosWidth(int n) { return 0; }
static int PartsEngine_GetComponentClipAreaPosHeight(int n) { return 0; }
static void PartsEngine_SetComponentReverseTB(int n, bool r) {}
static void PartsEngine_SetComponentReverseLR(int n, bool r) {}
static bool PartsEngine_GetComponentReverseTB(int n) { return false; }
static bool PartsEngine_GetComponentReverseLR(int n) { return false; }
static void PartsEngine_SetComponentMargin(int n, int t, int b, int l, int r) {}
static int PartsEngine_GetComponentMarginTop(int n) { return 0; }
static int PartsEngine_GetComponentMarginBottom(int n) { return 0; }
static int PartsEngine_GetComponentMarginLeft(int n) { return 0; }
static int PartsEngine_GetComponentMarginRight(int n) { return 0; }
static void PartsEngine_SetComponentAlphaClipper(int n, int clipper) { PE_SetPartsAlphaClipperPartsNumber(n, clipper); }
static int PartsEngine_GetComponentAlphaClipper(int n) { return -1; }
static void PartsEngine_SetComponentTextureFilterType(int n, int type) {}
static int PartsEngine_GetComponentTextureFilterType(int n) { return 0; }
static void PartsEngine_SetComponentTextureAddressType(int n, int type) {}
static int PartsEngine_GetComponentTextureAddressType(int n) { return 0; }
static void PartsEngine_SetComponentMipmap(int n, bool mipmap) {}
static bool PartsEngine_IsComponentMipmap(int n) { return false; }
static void PartsEngine_SetComponentScrollPosXLinkNumber(int n, int link) {}
static void PartsEngine_SetComponentScrollPosYLinkNumber(int n, int link) {}
static int PartsEngine_GetComponentScrollPosXLinkNumber(int n) { return -1; }
static int PartsEngine_GetComponentScrollPosYLinkNumber(int n) { return -1; }
static void PartsEngine_SetComponentScrollAlphaLinkNumber(int n, int link) {}
static int PartsEngine_GetComponentScrollAlphaLinkNumber(int n) { return -1; }
static void PartsEngine_SetComponentCheckBoxShowLinkNumber(int n, int link) {}
static int PartsEngine_GetComponentCheckBoxShowLinkNumber(int n) { return -1; }
static void PartsEngine_GetComponentAbsolutePos(int n, int x_slot, int y_slot) { wrap_set_float(x_slot, 0); wrap_set_float(y_slot, 0); }
static float PartsEngine_GetComponentAbsolutePosX(int n) { return 0; }
static float PartsEngine_GetComponentAbsolutePosY(int n) { return 0; }
static int PartsEngine_GetComponentAbsolutePosZ(int n) { return 0; }
static int PartsEngine_GetComponentAbsoluteMaxPosZ(int n) { return 0; }
static void PartsEngine_GetComponentAbsoluteSquarePos(int n, int x1, int y1, int x2, int y2, int x3, int y3, int x4, int y4, int state) {}

/* Child management — delegate to existing functions */
static bool PartsEngine_IsExistChild(int number, int child) {
	struct parts *p = parts_try_get(number);
	if (!p) return false;
	struct parts *c;
	PARTS_FOREACH_CHILD(c, p) {
		if (c->no == child) return true;
	}
	return false;
}
static void PartsEngine_ClearChild(int number) {}
static void PartsEngine_AddChild(int number, int child) {
	PE_SetParentPartsNumber(child, number);
}
static void PartsEngine_InsertChild(int n, int idx, int child) { PE_SetParentPartsNumber(child, n); }
static void PartsEngine_RemoveChild(int number, int child) {}
static int PartsEngine_NumofChild(int number) {
	/* Count children from PE parent-child relationships */
	struct parts *p = parts_try_get(number);
	if (!p) return 0;
	int count = 0;
	struct parts *child;
	PARTS_FOREACH_CHILD(child, p) {
		count++;
	}
	return count;
}
static int PartsEngine_GetChildIndex(int number, int child) { return -1; }
static int PartsEngine_GetChild(int number, int index) {
	struct parts *p = parts_try_get(number);
	if (!p) return -1;
	int i = 0;
	struct parts *child;
	PARTS_FOREACH_CHILD(child, p) {
		if (i == index) return child->no;
		i++;
	}
	return -1;
}

/* Unique ID for event dispatch */
static void PartsEngine_SetDelegateIndex(int number, int delegate_index) { PE_SetDelegateIndex(number, delegate_index); }
static int PartsEngine_GetDelegateIndex(int number) { return PE_GetDelegateIndex(number); }

static void PartsEngine_SetEventID(int number, int delegate_index, int unique_id)
{
	PE_SetDelegateIndex(number, delegate_index);
	struct parts *p = parts_try_get(number);
	if (p) p->unique_id = unique_id;
}

static int PartsEngine_GetUniqueID(int number)
{
	struct parts *p = parts_try_get(number);
	return p ? p->unique_id : 0;
}

static void PartsEngine_SetUserComponentName(int n, struct string *name) {}
static struct string *PartsEngine_GetUserComponentName(int n) { return string_ref(&EMPTY_STRING); }
static void PartsEngine_SetUserComponentData(int n, struct string *key, struct string *val) {}
static struct string *PartsEngine_GetUserComponentData(int n, struct string *key) { return string_ref(&EMPTY_STRING); }

/* Part properties (v14 variants with state parameter) */
static void PartsEngine_SetWantSaveBackScene(int n, bool enable) {}
static bool PartsEngine_IsWantSaveBackScene(int n) { return false; }
static void PartsEngine_SaveThumbnail_v14(struct string *name, int width) {}
static int PartsEngine_GetActiveParts(void) { return PE_GetClickPartsNumber(); }
static void PartsEngine_SetClickMissSoundName(struct string *name) {}
static struct string *PartsEngine_GetClickMissSoundName(void) { return string_ref(&EMPTY_STRING); }
static int PartsEngine_GetClickNumber(void) { return PE_GetClickPartsNumber(); }
static void PartsEngine_StopSoundWithoutSystemSound(void) {}
static void PartsEngine_SetEnableInput(bool enable) {}
static bool PartsEngine_IsEnableInput(void) { return true; }

/* v14 UpdateComponent: full rendering pipeline.
 * Args: PassedTime, ScaledPassedTime, MessageWindowShow, MulColorRate, AlphaRate
 * Delegates to PE_Update which handles events, animation, rendering, and swap.
 * Reentrancy guard: PE_Update can trigger VM callbacks (via sprite_call_plugins)
 * which might call UpdateComponent again → use guard to prevent infinite recursion. */
static bool in_update = false;
static void PartsEngine_UpdateComponent(int passed_time, int scaled_time,
		bool message_window_show, float mul_color_rate, float alpha_rate)
{
	if (in_update) {
		PE_UpdateComponent(passed_time);
		return;
	}
	in_update = true;
	PE_Update(passed_time, message_window_show);
	in_update = false;
}

/* v14 UpdateMatrix: recalculate transformation matrices.
 * The actual matrix update is done in PE_Update via parts_update_component. */
static void PartsEngine_UpdateMatrix(bool message_window_show,
		float mul_color_rate, float alpha_rate)
{
	/* no-op: UpdateComponent already calls the full PE_Update pipeline */
}

/* Message pump quiet stubs — polled every frame in the game main loop.
 * Returning 0/"empty" causes the loop to skip message processing. */
static void PartsEngine_PopMessage(void) {}
static int PartsEngine_GetMessageType(void) { return 0; }
static int PartsEngine_GetMessagePartsNumber(void) { return 0; }
static int PartsEngine_GetMessageVariableInt(int idx) { return 0; }

// v14: Save/SaveWithoutHideParts/Load — AIN_WRAP arg[0] is heap slot index.
// Wrap the original PE_Save/PE_Load which take struct page **.
static bool PartsEngine_Save_wrap(int buf_slot)
{
	struct page *buf = wrap_get_page(buf_slot);
	bool ok = PE_Save(&buf);
	if (ok)
		wrap_set_slot(buf_slot, heap_alloc_page(buf));
	return ok;
}

static bool PartsEngine_SaveWithoutHideParts_wrap(int buf_slot)
{
	struct page *buf = wrap_get_page(buf_slot);
	bool ok = PE_SaveWithoutHideParts(&buf);
	if (ok)
		wrap_set_slot(buf_slot, heap_alloc_page(buf));
	return ok;
}

static bool PartsEngine_Load_wrap(int buf_slot)
{
	struct page *buf = wrap_get_page(buf_slot);
	bool ok = PE_Load(&buf);
	return ok;
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
	    HLL_EXPORT(Release, PE_ReleaseParts),
	    HLL_EXPORT(GetFreeNumber, PE_GetFreeNumber),
	    HLL_EXPORT(GetFreeSystemPartsNumber, PE_GetFreeNumber),
	    HLL_EXPORT(GetFreeSystemPartsNumberNotSaved, PE_GetFreeNumber),
	    HLL_EXPORT(IsExist, PE_IsExist),
	    HLL_EXPORT(IsExistParts, PE_IsExist),
	    // v14 Controller system
	    HLL_EXPORT(AddController, PartsEngine_AddController),
	    HLL_EXPORT(RemoveController, PartsEngine_RemoveController),
	    HLL_EXPORT(MoveController, PartsEngine_MoveController),
	    HLL_EXPORT(GetControllerIndex, PartsEngine_GetControllerIndex),
	    HLL_EXPORT(GetControllerID, PartsEngine_GetControllerID),
	    HLL_EXPORT(GetControllerLength, PartsEngine_GetControllerLength),
	    HLL_EXPORT(GetSystemOverlayController, PartsEngine_GetSystemOverlayController),
	    // v14 rendering pipeline
	    HLL_EXPORT(UpdateComponent, PartsEngine_UpdateComponent),
	    HLL_EXPORT(UpdateMatrix, PartsEngine_UpdateMatrix),
	    // v14 Activity system
	    HLL_EXPORT(IsExistActivity, PartsEngine_IsExistActivity),
	    HLL_EXPORT(CreateActivity, PartsEngine_CreateActivity),
	    HLL_EXPORT(ReleaseActivity, PartsEngine_ReleaseActivity),
	    HLL_EXPORT(SaveActivityEXText, PartsEngine_SaveActivityEXText),
	    HLL_EXPORT(LoadActivityEXText, PartsEngine_LoadActivityEXText),
	    HLL_EXPORT(ReadActivityFile, PartsEngine_ReadActivityFile),
	    HLL_EXPORT(WriteActivityFile, PartsEngine_WriteActivityFile),
	    HLL_EXPORT(IsExistActivityFile, PartsEngine_IsExistActivityFile),
	    HLL_EXPORT(AddActivityParts, PartsEngine_AddActivityParts),
	    HLL_EXPORT(RemoveActivityParts, PartsEngine_RemoveActivityParts),
	    HLL_EXPORT(RemoveAllActivityParts, PartsEngine_RemoveAllActivityParts),
	    HLL_EXPORT(NumofActivityParts, PartsEngine_NumofActivityParts),
	    HLL_EXPORT(GetActivityParts, PartsEngine_GetActivityParts),
	    HLL_EXPORT(IsExistActivityPartsByName, PartsEngine_IsExistActivityPartsByName),
	    HLL_EXPORT(IsExistActivityPartsByNumber, PartsEngine_IsExistActivityPartsByNumber),
	    HLL_EXPORT(GetActivityPartsNumber, PartsEngine_GetActivityPartsNumber),
	    HLL_EXPORT(GetActivityPartsName, PartsEngine_GetActivityPartsName),
	    HLL_EXPORT(GetActivityEXID, PartsEngine_GetActivityEXID),
	    HLL_EXPORT(SetActivityEXText, PartsEngine_SetActivityEXText),
	    HLL_EXPORT(GetActivityEXText, PartsEngine_GetActivityEXText),
	    HLL_EXPORT(SetActivityBG, PartsEngine_SetActivityBG),
	    HLL_EXPORT(GetActivityBG, PartsEngine_GetActivityBG),
	    HLL_EXPORT(AddActivityCloseParts, PartsEngine_AddActivityCloseParts),
	    HLL_EXPORT(IsExistActivityCloseParts, PartsEngine_IsExistActivityCloseParts),
	    HLL_EXPORT(AddActivityLockedParts, PartsEngine_AddActivityLockedParts),
	    HLL_EXPORT(IsExistActivityLockedParts, PartsEngine_IsExistActivityLockedParts),
	    HLL_EXPORT(SetActivityEndKey, PartsEngine_SetActivityEndKey),
	    HLL_EXPORT(EraseActivityEndKey, PartsEngine_EraseActivityEndKey),
	    HLL_EXPORT(IsExistActivityEndKey, PartsEngine_IsExistActivityEndKey),
	    HLL_EXPORT(NumofActivityEndKey, PartsEngine_NumofActivityEndKey),
	    HLL_EXPORT(GetActivityEndKey, PartsEngine_GetActivityEndKey),
	    // v14 Component system
	    HLL_EXPORT(SetComponentType, PartsEngine_SetComponentType),
	    HLL_EXPORT(GetComponentType, PartsEngine_GetComponentType),
	    HLL_EXPORT(SetComponentVibrationPos, PartsEngine_SetComponentVibrationPos),
	    HLL_EXPORT(SetComponentPos, PartsEngine_SetComponentPos),
	    HLL_EXPORT(SetComponentPosZ, PartsEngine_SetComponentPosZ),
	    HLL_EXPORT(GetComponentPosX, PartsEngine_GetComponentPosX),
	    HLL_EXPORT(GetComponentPosY, PartsEngine_GetComponentPosY),
	    HLL_EXPORT(GetComponentPosZ, PartsEngine_GetComponentPosZ),
	    HLL_EXPORT(GetComponentAbsolutePos, PartsEngine_GetComponentAbsolutePos),
	    HLL_EXPORT(GetComponentAbsolutePosX, PartsEngine_GetComponentAbsolutePosX),
	    HLL_EXPORT(GetComponentAbsolutePosY, PartsEngine_GetComponentAbsolutePosY),
	    HLL_EXPORT(GetComponentAbsolutePosZ, PartsEngine_GetComponentAbsolutePosZ),
	    HLL_EXPORT(GetComponentAbsoluteMaxPosZ, PartsEngine_GetComponentAbsoluteMaxPosZ),
	    HLL_EXPORT(GetComponentAbsoluteSquarePos, PartsEngine_GetComponentAbsoluteSquarePos),
	    HLL_EXPORT(SetLockInputState, PartsEngine_SetLockInputState),
	    HLL_EXPORT(IsLockInputState, PartsEngine_IsLockInputState),
	    HLL_EXPORT(SetComponentOriginPosMode, PartsEngine_SetComponentOriginPosMode),
	    HLL_EXPORT(GetComponentOriginPosMode, PartsEngine_GetComponentOriginPosMode),
	    HLL_EXPORT(SetComponentShow, PartsEngine_SetComponentShow),
	    HLL_EXPORT(IsComponentShow, PartsEngine_IsComponentShow),
	    HLL_EXPORT(SetComponentShowEditor, PartsEngine_SetComponentShowEditor),
	    HLL_EXPORT(IsComponentShowEditor, PartsEngine_IsComponentShowEditor),
	    HLL_EXPORT(SetComponentMessageWindowShowLink, PartsEngine_SetComponentMessageWindowShowLink),
	    HLL_EXPORT(IsComponentMessageWindowShowLink, PartsEngine_IsComponentMessageWindowShowLink),
	    HLL_EXPORT(SetComponentMessageWindowEffectLink, PartsEngine_SetComponentMessageWindowEffectLink),
	    HLL_EXPORT(IsComponentMessageWindowEffectLink, PartsEngine_IsComponentMessageWindowEffectLink),
	    HLL_EXPORT(SetComponentAlpha, PartsEngine_SetComponentAlpha),
	    HLL_EXPORT(GetComponentAlpha, PartsEngine_GetComponentAlpha),
	    HLL_EXPORT(SetComponentAddColor, PartsEngine_SetComponentAddColor),
	    HLL_EXPORT(GetComponentAddColorR, PartsEngine_GetComponentAddColorR),
	    HLL_EXPORT(GetComponentAddColorG, PartsEngine_GetComponentAddColorG),
	    HLL_EXPORT(GetComponentAddColorB, PartsEngine_GetComponentAddColorB),
	    HLL_EXPORT(SetComponentSubColorMode, PartsEngine_SetComponentSubColorMode),
	    HLL_EXPORT(IsComponentSubColorMode, PartsEngine_IsComponentSubColorMode),
	    HLL_EXPORT(SetComponentMulColor, PartsEngine_SetComponentMulColor),
	    HLL_EXPORT(GetComponentMulColorR, PartsEngine_GetComponentMulColorR),
	    HLL_EXPORT(GetComponentMulColorG, PartsEngine_GetComponentMulColorG),
	    HLL_EXPORT(GetComponentMulColorB, PartsEngine_GetComponentMulColorB),
	    HLL_EXPORT(SetComponentDrawFilter, PartsEngine_SetComponentDrawFilter),
	    HLL_EXPORT(GetComponentDrawFilter, PartsEngine_GetComponentDrawFilter),
	    HLL_EXPORT(SetComponentMagX, PartsEngine_SetComponentMagX),
	    HLL_EXPORT(SetComponentMagY, PartsEngine_SetComponentMagY),
	    HLL_EXPORT(GetComponentMagX, PartsEngine_GetComponentMagX),
	    HLL_EXPORT(GetComponentMagY, PartsEngine_GetComponentMagY),
	    HLL_EXPORT(SetComponentRotateX, PartsEngine_SetComponentRotateX),
	    HLL_EXPORT(SetComponentRotateY, PartsEngine_SetComponentRotateY),
	    HLL_EXPORT(SetComponentRotateZ, PartsEngine_SetComponentRotateZ),
	    HLL_EXPORT(GetComponentRotateX, PartsEngine_GetComponentRotateX),
	    HLL_EXPORT(GetComponentRotateY, PartsEngine_GetComponentRotateY),
	    HLL_EXPORT(GetComponentRotateZ, PartsEngine_GetComponentRotateZ),
	    HLL_EXPORT(SetComponentEnableClipArea, PartsEngine_SetComponentEnableClipArea),
	    HLL_EXPORT(IsComponentEnableClipArea, PartsEngine_IsComponentEnableClipArea),
	    HLL_EXPORT(SetComponentClipArea, PartsEngine_SetComponentClipArea),
	    HLL_EXPORT(GetComponentClipAreaPosX, PartsEngine_GetComponentClipAreaPosX),
	    HLL_EXPORT(GetComponentClipAreaPosY, PartsEngine_GetComponentClipAreaPosY),
	    HLL_EXPORT(GetComponentClipAreaPosWidth, PartsEngine_GetComponentClipAreaPosWidth),
	    HLL_EXPORT(GetComponentClipAreaPosHeight, PartsEngine_GetComponentClipAreaPosHeight),
	    HLL_EXPORT(SetComponentReverseTB, PartsEngine_SetComponentReverseTB),
	    HLL_EXPORT(SetComponentReverseLR, PartsEngine_SetComponentReverseLR),
	    HLL_EXPORT(GetComponentReverseTB, PartsEngine_GetComponentReverseTB),
	    HLL_EXPORT(GetComponentReverseLR, PartsEngine_GetComponentReverseLR),
	    HLL_EXPORT(SetComponentMargin, PartsEngine_SetComponentMargin),
	    HLL_EXPORT(GetComponentMarginTop, PartsEngine_GetComponentMarginTop),
	    HLL_EXPORT(GetComponentMarginBottom, PartsEngine_GetComponentMarginBottom),
	    HLL_EXPORT(GetComponentMarginLeft, PartsEngine_GetComponentMarginLeft),
	    HLL_EXPORT(GetComponentMarginRight, PartsEngine_GetComponentMarginRight),
	    HLL_EXPORT(SetComponentAlphaClipper, PartsEngine_SetComponentAlphaClipper),
	    HLL_EXPORT(GetComponentAlphaClipper, PartsEngine_GetComponentAlphaClipper),
	    HLL_EXPORT(SetComponentScrollPosXLinkNumber, PartsEngine_SetComponentScrollPosXLinkNumber),
	    HLL_EXPORT(SetComponentScrollPosYLinkNumber, PartsEngine_SetComponentScrollPosYLinkNumber),
	    HLL_EXPORT(GetComponentScrollPosXLinkNumber, PartsEngine_GetComponentScrollPosXLinkNumber),
	    HLL_EXPORT(GetComponentScrollPosYLinkNumber, PartsEngine_GetComponentScrollPosYLinkNumber),
	    HLL_EXPORT(SetComponentScrollAlphaLinkNumber, PartsEngine_SetComponentScrollAlphaLinkNumber),
	    HLL_EXPORT(GetComponentScrollAlphaLinkNumber, PartsEngine_GetComponentScrollAlphaLinkNumber),
	    HLL_EXPORT(SetComponentCheckBoxShowLinkNumber, PartsEngine_SetComponentCheckBoxShowLinkNumber),
	    HLL_EXPORT(GetComponentCheckBoxShowLinkNumber, PartsEngine_GetComponentCheckBoxShowLinkNumber),
	    HLL_EXPORT(SetComponentTextureFilterType, PartsEngine_SetComponentTextureFilterType),
	    HLL_EXPORT(GetComponentTextureFilterType, PartsEngine_GetComponentTextureFilterType),
	    HLL_EXPORT(SetComponentTextureAddressType, PartsEngine_SetComponentTextureAddressType),
	    HLL_EXPORT(GetComponentTextureAddressType, PartsEngine_GetComponentTextureAddressType),
	    HLL_EXPORT(SetComponentMipmap, PartsEngine_SetComponentMipmap),
	    HLL_EXPORT(IsComponentMipmap, PartsEngine_IsComponentMipmap),
	    // v14 Child management
	    HLL_EXPORT(IsExistChild, PartsEngine_IsExistChild),
	    HLL_EXPORT(ClearChild, PartsEngine_ClearChild),
	    HLL_EXPORT(AddChild, PartsEngine_AddChild),
	    HLL_EXPORT(InsertChild, PartsEngine_InsertChild),
	    HLL_EXPORT(RemoveChild, PartsEngine_RemoveChild),
	    HLL_EXPORT(NumofChild, PartsEngine_NumofChild),
	    HLL_EXPORT(GetChildIndex, PartsEngine_GetChildIndex),
	    HLL_EXPORT(GetChild, PartsEngine_GetChild),
	    // v14 misc
	    HLL_EXPORT(SetDelegateIndex, PartsEngine_SetDelegateIndex),
	    HLL_EXPORT(GetDelegateIndex, PartsEngine_GetDelegateIndex),
	    HLL_EXPORT(SetEventID, PartsEngine_SetEventID),
	    HLL_EXPORT(SetUserComponentName, PartsEngine_SetUserComponentName),
	    HLL_EXPORT(GetUserComponentName, PartsEngine_GetUserComponentName),
	    HLL_EXPORT(SetUserComponentData, PartsEngine_SetUserComponentData),
	    HLL_EXPORT(GetUserComponentData, PartsEngine_GetUserComponentData),
	    HLL_EXPORT(SetWantSaveBackScene, PartsEngine_SetWantSaveBackScene),
	    HLL_EXPORT(IsWantSaveBackScene, PartsEngine_IsWantSaveBackScene),
	    HLL_EXPORT(SaveThumbnail, PartsEngine_SaveThumbnail_v14),
	    HLL_EXPORT(GetActiveParts, PartsEngine_GetActiveParts),
	    HLL_EXPORT(SetClickMissSoundName, PartsEngine_SetClickMissSoundName),
	    HLL_EXPORT(GetClickMissSoundName, PartsEngine_GetClickMissSoundName),
	    HLL_EXPORT(GetClickNumber, PartsEngine_GetClickNumber),
	    HLL_EXPORT(StopSoundWithoutSystemSound, PartsEngine_StopSoundWithoutSystemSound),
	    HLL_EXPORT(SetEnableInput, PartsEngine_SetEnableInput),
	    HLL_EXPORT(IsEnableInput, PartsEngine_IsEnableInput),
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
	    HLL_EXPORT(Save, PartsEngine_Save_wrap),
	    HLL_EXPORT(SaveWithoutHideParts, PartsEngine_SaveWithoutHideParts_wrap),
	    HLL_EXPORT(Load, PartsEngine_Load_wrap),
	    HLL_EXPORT(GetActiveController, PartsEngine_GetActiveController),
	    HLL_EXPORT(SetActiveController, PartsEngine_SetActiveController),
	    HLL_EXPORT(PopMessage, PartsEngine_PopMessage),
	    HLL_EXPORT(GetMessageType, PartsEngine_GetMessageType),
	    HLL_EXPORT(GetMessagePartsNumber, PartsEngine_GetMessagePartsNumber),
	    HLL_EXPORT(GetMessageVariableInt, PartsEngine_GetMessageVariableInt)
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
