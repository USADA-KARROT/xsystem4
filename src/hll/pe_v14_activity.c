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

/* v14 PartsEngine Activity system + .pactex loader (Dohna Dohna).
 *
 * Activities group parts together and are loaded from .pactex files in
 * the Pact archive (same container format as .ex). The AIN code walks
 * the component tree via GetActivityPartsNumber / GetComponentType /
 * child links; the loader creates parts entries with the correct raw
 * component types and parent-child relationships.
 *
 * Registered from PartsEngine's _PreLink when the AIN declares the
 * Activity API (v14 games); see pe_v14_activity_prelink().
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
#include "gfx/gfx.h"
#include "parts.h"
#include "../parts/parts_internal.h"
#include "hll.h"
#include "xsystem4.h"

extern struct string *sjis_to_gbk_string(const char *src, size_t len);
extern struct static_library lib_PartsEngine;


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
	char close_parts[MAX_ACTIVITY_PARTS][256];
	int nr_close_parts;
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

/* Pactex files use SJIS encoding for field names (confirmed from tree dump).
 * We match against raw SJIS byte patterns. */

/* SJIS byte sequences for child component branch names. */
static const char SJIS_KO_PARTS[]   = "\x8e\x71\x83\x70\x81\x5b\x83\x63";  /* 子パーツ (child parts) */
static const char SJIS_BUHIN[]      = "\x95\x94\x95\x69";                    /* 部品 (parts) */

/* GBK byte sequences (legacy — kept as fallback). */
static const char GBK_BUJIAN[] = "\xb2\xbf\xbc\xfe";    /* 部件 (CN: component) */
static const char GBK_BUPIN[]  = "\xb2\xbf\xc6\xb7";    /* 部品 (JP: parts, GBK) */

/* SJIS byte sequences for pactex property names (from tree dump). */
static const char SJIS_POSITION[]    = "\x8d\xc0\x95\x57";                     /* 座標 */
static const char SJIS_SHOW[]        = "\x95\x5c\x8e\xa6";                     /* 表示 */
static const char SJIS_ALPHA[]       = "\x83\x41\x83\x8b\x83\x74\x83\x40";     /* アルファ */
static const char SJIS_ORIGIN_MODE[] = "\x8c\xb4\x93\x5f\x8d\xc0\x95\x57\x83\x82\x81\x5b\x83\x68"; /* 原点座標モード */
static const char SJIS_TYPE_INFO[]   = "\x8e\xed\x97\xde\x95\xca\x8f\xee\x95\xf1"; /* 種類別情報 */
static const char SJIS_PARTS_TYPE[]  = "\x83\x70\x81\x5b\x83\x63\x83\x5e\x83\x43\x83\x76"; /* パーツタイプ */
static const char SJIS_CG_MEI[]      = "\x82\x62\x82\x66\x96\xbc";             /* ＣＧ名 (CG name) */
static const char SJIS_SIZE[]        = "\x83\x54\x83\x43\x83\x59";             /* サイズ (size) */
static const char SJIS_COLOR[]       = "\x90\x46";                             /* 色 (color) */
static const char SJIS_PANEL[]       = "\x83\x70\x83\x6c\x83\x8b";             /* パネル (panel) */
/* static const char SJIS_MULTI_LEVEL[] = "\x83\x7d\x83\x8b\x83\x60\x83\x8c\x83\x78\x83\x8b\x83\x70\x81\x5b\x83\x63"; */ /* マルチレベルパーツ — unused */
static const char SJIS_SCALE[]       = "\x8a\x67\x91\xe5\x8f\x6b\x8f\xac"; /* 拡大縮小 (scale) */
static const char SJIS_ROTATION[]    = "\x89\xf1\x93\x5d";                 /* 回転 (rotation) */
static const char SJIS_BUTTON[]      = "\x83\x7b\x83\x5e\x83\x93";         /* ボタン (button) */
static const char SJIS_BUTTON_COLOR[]= "\x83\x7b\x83\x5e\x83\x93\x82\xcc\x90\x46"; /* ボタンの色 (button color) */
static const char SJIS_FONT_TYPE[]   = "\x83\x74\x83\x48\x83\x93\x83\x67\x8e\xed\x97\xde"; /* フォント種類 (font type) */
static const char SJIS_FONT_SIZE[]   = "\x83\x74\x83\x48\x83\x93\x83\x67\x83\x54\x83\x43\x83\x59"; /* フォントサイズ (font size) */
static const char SJIS_FONT_COLOR[]  = "\x83\x74\x83\x48\x83\x93\x83\x67\x90\x46"; /* フォント色 (font color) */
static const char SJIS_FONT_EDGE_COLOR[] = "\x83\x74\x83\x48\x83\x93\x83\x67\x83\x47\x83\x62\x83\x57\x90\x46"; /* フォントエッジ色 (font edge color) */
static const char SJIS_SURFACE_AREA[] = "\x83\x54\x81\x5b\x83\x74\x83\x46\x83\x43\x83\x58\x83\x47\x83\x8a\x83\x41"; /* サーフェイスエリア (surface area) */
static const char SJIS_DRAW_FILTER[]  = "\x95\x60\x89\xe6\x83\x74\x83\x42\x83\x8b\x83\x5e"; /* 描画フィルタ (draw filter) */
static const char SJIS_ADD_COLOR[]    = "\x89\xc1\x8e\x5a\x90\x46"; /* 加算色 (add color) */
static const char SJIS_MUL_COLOR[]    = "\x8f\xe6\x8e\x5a\x90\x46"; /* 乗算色 (multiply color) */
/* static const char SJIS_CG_PARTS[]    = "\x82\x62\x82\x66\x83\x70\x81\x5b\x83\x63"; */ /* ＣＧパーツ — unused */
/* static const char SJIS_ALPHA_CLIPPER[] = "\x83\x41\x83\x8b\x83\x74\x83\x40\x83\x4e\x83\x8a\x83\x62\x83\x70\x81\x5b"; */ /* アルファクリッパー — unused */
/* static const char SJIS_NORMAL_STATE[]= "\x92\xca\x8f\xed\x8f\xf3\x91\xd4"; */ /* 通常状態 — unused, kept for reference */

/* GBK property names (legacy fallback). */
static const char GBK_POSITION[]    = "\xd7\xf9\x98\xcb";         /* 座標 (GBK) */
static const char GBK_SHOW[]        = "\xb1\xed\xca\xbe";         /* 表示 (GBK) */
static const char GBK_ALPHA[]       = "\xa5\xa2\xa5\xeb\xa5\xd5\xa5\xa1"; /* アルファ (GBK) */
static const char GBK_ORIGIN_MODE[] = "\xd4\xad\xfc\x63\xd7\xf9\x98\xcb\xc4\xa3\xca\xbd"; /* 原點座標模式 (GBK) */
static const char GBK_CG_MEI[]      = "\xa3\xc3\xa3\xc7\xc3\xfb"; /* ＣＧ名 (GBK) */
/* static const char GBK_ADD_COLOR[]   = "\xbc\xd3\xcb\xe3\xc9\xab"; */ /* 加算色 (GBK) — unused */
/* static const char GBK_MUL_COLOR[]   = "\x81\x5c\xcb\xe3\xc9\xab"; */ /* 乗算色 (GBK) — unused */
static const char GBK_PARTS_TYPE[]  = "\xb2\xbf\xbc\xfe\xa5\xbf\xa5\xa4\xa5\xd7"; /* 部件タイプ (GBK) */
static const char GBK_PANEL[]       = "\xa5\xd1\xa5\xcd\xa5\xeb"; /* パネル (GBK) */
static const char GBK_SIZE[]        = "\xa5\xb5\xa5\xa4\xa5\xba"; /* サイズ (GBK) */
static const char GBK_COLOR[]       = "\xc9\xab";                 /* 色 (GBK) */
static const char GBK_BUTTON[]      = "\xa5\xdc\xa5\xbf\xa5\xf3"; /* ボタン (GBK katakana) */
static const char GBK_CN_BUTTON[]   = "\xb0\xb4\xe2\x6f";         /* 按鈕 (GBK Chinese) */
static const char GBK_SURFACE_AREA[] = "\xa5\xb5\xa1\xbc\xa5\xd5\xa5\xa7\xa5\xa4\xa5\xb9\xa5\xa8\xa5\xea\xa5\xa2"; /* サーフェイスエリア (GBK) */
static const char GBK_CN_PANEL[]    = "\xb5\xcd\xb5\xc8\xbc\x89"; /* 低等級 (GBK CN panel type) */
static const char GBK_SCALE[]       = "\x92\x88\xb4\xf3\xbf\x73\xd0\xa1"; /* 拡大縮小 (GBK) */
static const char GBK_ROTATION[]    = "\xd0\xfd\xde\x44"; /* 回転 (GBK) */
static const char GBK_MUL_COLOR[]   = "\x81\x5c\xcb\xe3\xc9\xab"; /* 乗算色 (GBK) */
static const char GBK_CG_DETECT[]   = "\xa3\xc3\xa3\xc7\xc5\xd0\xb6\xa8\xb2\xbf\xbc\xfe"; /* ＣＧ判定部件 (GBK CG detection parts) */
static const char SJIS_CG_DETECT[]  = "\x82\x62\x82\x66\x94\xbb\x92\xe8\x83\x70\x81\x5b\x83\x63"; /* ＣＧ判定パーツ (SJIS) */

/* --- Pactex tree parser --- */

/* Check if tree node name contains a byte substring (raw SJIS/GBK). */
static bool pactex_name_contains(struct ex_tree *node, const char *pattern)
{
	if (!node->name) return false;
	return strstr(node->name->text, pattern) != NULL;
}

/* Find the "部件"/"部品" (parts/components) branch among children.
 * IMPORTANT: Only search branch children (skip leaves) to avoid
 * matching leaf "子部件リスト" which contains 部件 as substring.
 * The old code matched the leaf first, then fell back to a structural
 * heuristic that incorrectly returned 種類別情報 instead of 子部件. */
static struct ex_tree *pactex_find_buhin(struct ex_tree *parent)
{
	if (parent->is_leaf) return NULL;

	/* Search branch children for child-parts container.
	 * Try SJIS names first (confirmed from tree dump), then GBK fallback. */
	for (unsigned i = 0; i < parent->nr_children; i++) {
		struct ex_tree *child = &parent->children[i];
		if (child->is_leaf) continue;
		if (pactex_name_contains(child, SJIS_KO_PARTS) ||
		    pactex_name_contains(child, SJIS_BUHIN) ||
		    pactex_name_contains(child, GBK_BUJIAN) ||
		    pactex_name_contains(child, GBK_BUPIN))
			return child;
	}
	return NULL;
}

/* Find the type-specific info branch (種類別情報) among children.
 * This is a non-leaf child that is NOT the children branch. */
static struct ex_tree *pactex_find_type_info(struct ex_tree *component)
{
	if (component->is_leaf) return NULL;
	/* First try exact SJIS name match for 種類別情報 */
	for (unsigned i = 0; i < component->nr_children; i++) {
		struct ex_tree *child = &component->children[i];
		if (child->is_leaf) continue;
		if (pactex_name_contains(child, SJIS_TYPE_INFO))
			return child;
	}
	/* Fallback: first non-leaf child that is NOT the children branch */
	for (unsigned i = 0; i < component->nr_children; i++) {
		struct ex_tree *child = &component->children[i];
		if (child->is_leaf) continue;
		if (pactex_name_contains(child, SJIS_KO_PARTS) ||
		    pactex_name_contains(child, SJIS_BUHIN) ||
		    pactex_name_contains(child, GBK_BUJIAN) ||
		    pactex_name_contains(child, GBK_BUPIN))
			continue;
		return child;
	}
	return NULL;
}

/* Extract an integer leaf property by exact name match. Returns default if not found. */
static int pactex_get_int(struct ex_tree *node, const char *name, int def)
{
	if (node->is_leaf) return def;
	for (unsigned i = 0; i < node->nr_children; i++) {
		struct ex_tree *c = &node->children[i];
		if (!c->is_leaf) continue;
		if (!c->name || strcmp(c->name->text, name) != 0) continue;
		if (c->leaf.value.type == EX_INT) return c->leaf.value.i;
		break;
	}
	return def;
}

/* Extract a list leaf property by exact name match. Returns NULL if not found. */
static struct ex_list *pactex_get_list(struct ex_tree *node, const char *name)
{
	if (node->is_leaf) return NULL;
	for (unsigned i = 0; i < node->nr_children; i++) {
		struct ex_tree *c = &node->children[i];
		if (!c->is_leaf) continue;
		if (!c->name || strcmp(c->name->text, name) != 0) continue;
		if (c->leaf.value.type == EX_LIST) return c->leaf.value.list;
		break;
	}
	return NULL;
}

/* Extract a string leaf property by name substring (strstr). Returns NULL if not found/empty. */
static const char *pactex_get_string(struct ex_tree *node, const char *pattern)
{
	if (node->is_leaf) return NULL;
	for (unsigned i = 0; i < node->nr_children; i++) {
		struct ex_tree *c = &node->children[i];
		if (!c->is_leaf) continue;
		if (!pactex_name_contains(c, pattern)) continue;
		if (c->leaf.value.type == EX_STRING && c->leaf.value.s &&
		    c->leaf.value.s->text[0])
			return c->leaf.value.s->text;
		break;
	}
	return NULL;
}

/* Search for ＣＧ名 leaf in a branch, recursively descending into sub-branches.
 * In the CN/GBK version, CG names are nested inside 素材リスト/素材N/ＣＧ名
 * (depth 2 below the state branch), not as direct children. */
static const char *pactex_find_cg_name(struct ex_tree *branch, int depth)
{
	if (branch->is_leaf || depth > 3) return NULL;
	/* Direct child search */
	const char *cg = pactex_get_string(branch, SJIS_CG_MEI);
	if (!cg) cg = pactex_get_string(branch, GBK_CG_MEI);
	if (cg) return cg;
	/* Recurse into sub-branches (素材リスト → 素材N) */
	for (unsigned i = 0; i < branch->nr_children; i++) {
		struct ex_tree *child = &branch->children[i];
		if (child->is_leaf) continue;
		cg = pactex_find_cg_name(child, depth + 1);
		if (cg) return cg;
	}
	return NULL;
}

/* Extract サーフェイスエリア (surface area / clip rect) from a state branch.
 * The value is a list of 4 integers: [x, y, w, h].
 * Searches direct children and recurses into sub-branches (same as CG name). */
static bool pactex_get_surface_area(struct ex_tree *branch, int *x, int *y, int *w, int *h, int depth)
{
	if (branch->is_leaf || depth > 3) return false;
	/* Direct child search */
	for (unsigned i = 0; i < branch->nr_children; i++) {
		struct ex_tree *c = &branch->children[i];
		if (!c->is_leaf || !c->name) continue;
		if (strstr(c->name->text, SJIS_SURFACE_AREA) &&
		    c->leaf.value.type == EX_LIST && c->leaf.value.list &&
		    c->leaf.value.list->nr_items >= 4) {
			struct ex_list *sa = c->leaf.value.list;
			*x = sa->items[0].value.i;
			*y = sa->items[1].value.i;
			*w = sa->items[2].value.i;
			*h = sa->items[3].value.i;
			return true;
		}
	}
	/* Recurse into sub-branches */
	for (unsigned i = 0; i < branch->nr_children; i++) {
		struct ex_tree *child = &branch->children[i];
		if (child->is_leaf) continue;
		if (pactex_get_surface_area(child, x, y, w, h, depth + 1))
			return true;
	}
	return false;
}

/* Apply pactex properties (position, show, alpha, CG) to a parts entry.
 * Extracts standard properties from leaf children, and CG names from
 * the type-specific info branch (種類別情報). */
static void pactex_apply_properties(struct ex_tree *node, int parts_no)
{


	/* Extract position: 座標 = list[3] = (x, y, z) */
	struct ex_list *pos = pactex_get_list(node, SJIS_POSITION);
	if (!pos) pos = pactex_get_list(node, GBK_POSITION);
	if (pos && pos->nr_items >= 2) {
		int x = (pos->items[0].value.type == EX_FLOAT) ?
			(int)pos->items[0].value.f : pos->items[0].value.i;
		int y = (pos->items[1].value.type == EX_FLOAT) ?
			(int)pos->items[1].value.f : pos->items[1].value.i;
		PE_SetPos(parts_no, x, y);
		/* Z order from position list item 2 */
		if (pos->nr_items >= 3) {
			int z = (pos->items[2].value.type == EX_FLOAT) ?
				(int)pos->items[2].value.f : pos->items[2].value.i;
			PE_SetZ(parts_no, z);
		}
	}

	/* Extract show: 表示 = int */
	int show = pactex_get_int(node, SJIS_SHOW, -1);
	if (show < 0) show = pactex_get_int(node, GBK_SHOW, 1);
	PE_SetShow(parts_no, show);

	/* Extract alpha: アルファ = int 0-255 */
	int alpha = pactex_get_int(node, SJIS_ALPHA, -1);
	if (alpha < 0) alpha = pactex_get_int(node, GBK_ALPHA, 255);
	PE_SetAlpha(parts_no, alpha);

	/* Alpha clipper (not yet implemented) — ignored */

	/* Extract origin mode: 原点座標モード = int */
	int origin_mode = pactex_get_int(node, SJIS_ORIGIN_MODE, -1);
	if (origin_mode < 0) origin_mode = pactex_get_int(node, GBK_ORIGIN_MODE, 1);
	PE_SetPartsOriginPosMode(parts_no, origin_mode);

	/* Extract scale: 拡大縮小 = list[2] = (sx, sy) as float */
	struct ex_list *scale = pactex_get_list(node, SJIS_SCALE);
	if (!scale) scale = pactex_get_list(node, GBK_SCALE);
	if (scale && scale->nr_items >= 2) {
		float sx = (scale->items[0].value.type == EX_FLOAT) ?
			scale->items[0].value.f : (float)scale->items[0].value.i;
		float sy = (scale->items[1].value.type == EX_FLOAT) ?
			scale->items[1].value.f : (float)scale->items[1].value.i;
		if (sx != 1.0f || sy != 1.0f) {
			PE_SetPartsMagX(parts_no, sx);
			PE_SetPartsMagY(parts_no, sy);
		}
	}

	/* Extract rotation: 回転 = list[3] = (rx, ry, rz) as float */
	struct ex_list *rot = pactex_get_list(node, SJIS_ROTATION);
	if (!rot) rot = pactex_get_list(node, GBK_ROTATION);
	if (rot && rot->nr_items >= 3) {
		float rz = (rot->items[2].value.type == EX_FLOAT) ?
			rot->items[2].value.f : (float)rot->items[2].value.i;
		if (rz != 0.0f)
			PE_SetPartsRotateZ(parts_no, rz);
		/* rx, ry usually 0 for 2D; apply if non-zero */
		float rx = (rot->items[0].value.type == EX_FLOAT) ?
			rot->items[0].value.f : (float)rot->items[0].value.i;
		float ry = (rot->items[1].value.type == EX_FLOAT) ?
			rot->items[1].value.f : (float)rot->items[1].value.i;
		if (rx != 0.0f)
			PE_SetPartsRotateX(parts_no, rx);
		if (ry != 0.0f)
			PE_SetPartsRotateY(parts_no, ry);
	}

	/* Extract draw filter: 描画フィルタ = int (0=normal, 1=additive) */
	int draw_filter = pactex_get_int(node, SJIS_DRAW_FILTER, -1);
	if (draw_filter >= 0)
		PE_SetPartsDrawFilter(parts_no, draw_filter);

	/* Extract add color: 加算色 = list[3] = [r, g, b] */
	struct ex_list *add_col = pactex_get_list(node, SJIS_ADD_COLOR);
	if (add_col && add_col->nr_items >= 3)
		PE_SetAddColor(parts_no, add_col->items[0].value.i,
			add_col->items[1].value.i, add_col->items[2].value.i);

	/* Extract multiply color: 乗算色 = list[3] = [r, g, b] */
	struct ex_list *mul_col = pactex_get_list(node, SJIS_MUL_COLOR);
	if (!mul_col) mul_col = pactex_get_list(node, GBK_MUL_COLOR);
	if (mul_col && mul_col->nr_items >= 3)
		PE_SetMultiplyColor(parts_no, mul_col->items[0].value.i,
			mul_col->items[1].value.i, mul_col->items[2].value.i);

	/* Find type-specific info branch (種類別情報) for CG data */
	struct ex_tree *type_info = pactex_find_type_info(node);
	if (!type_info) {
		return;
	}

	/* Determine parts type from type_info (SJIS or GBK) */
	const char *ptype = pactex_get_string(type_info, SJIS_PARTS_TYPE);
	if (!ptype) ptype = pactex_get_string(type_info, GBK_PARTS_TYPE);

	/* --- Handle パネル (Panel) type: solid color rectangle --- */
	if (ptype && (strstr(ptype, SJIS_PANEL) || strstr(ptype, GBK_PANEL))) {
		/* サイズ = list[2] = [w, h] */
		struct ex_list *sz = pactex_get_list(type_info, SJIS_SIZE);
		if (!sz) sz = pactex_get_list(type_info, GBK_SIZE);
		int pw = 4, ph = 4;
		if (sz && sz->nr_items >= 2) {
			pw = (sz->items[0].value.type == EX_FLOAT) ?
				(int)sz->items[0].value.f : sz->items[0].value.i;
			ph = (sz->items[1].value.type == EX_FLOAT) ?
				(int)sz->items[1].value.f : sz->items[1].value.i;
		}
		/* Note: Panel Size=(4,4) from .pactex is the actual intended size.
		 * The white background issue is NOT solved by resizing here —
		 * it broke other small panels (buttons, indicators). */
		/* 色 = list[4] = [r, g, b, a] */
		struct ex_list *col = pactex_get_list(type_info, SJIS_COLOR);
		if (!col) col = pactex_get_list(type_info, GBK_COLOR);
		int cr = 255, cg = 255, cb = 255, ca = 255;
		if (col && col->nr_items >= 4) {
			cr = col->items[0].value.i;
			cg = col->items[1].value.i;
			cb = col->items[2].value.i;
			ca = col->items[3].value.i;
		}
		if (pw > 0 && ph > 0) {
			PE_AddCreateToPartsConstructionProcess(parts_no, pw, ph, 1);
			PE_AddFillAlphaColorToPartsConstructionProcess(
				parts_no, 0, 0, pw, ph, cr, cg, cb, ca, 1);
			PE_BuildPartsConstructionProcess(parts_no, 1);
		}
		return;
	}

	/* --- Handle ボタン / 按鈕 (Button) type: load CG images for each state --- */
	if (ptype && (strstr(ptype, SJIS_BUTTON) || strstr(ptype, GBK_BUTTON)
			|| strstr(ptype, GBK_CN_BUTTON))) {
		/* ＣＧ名 field has the base CG path (e.g. "システム／タイトル／ボタン／はじめから").
		 * Button CGs are stored as <base>／通常, <base>／オン, <base>／ダウン. */
		const char *cg_base = pactex_get_string(type_info, SJIS_CG_MEI);
		if (!cg_base)
			cg_base = pactex_get_string(type_info, GBK_CG_MEI);
		if (cg_base) {
			/* Detect encoding from CG base path. GBK fullwidth ／ = A3 AF.
			 * SJIS fullwidth ／ = 81 5E. */
			bool is_gbk_path = (strstr(cg_base, "\xa3\xaf") != NULL);

			/* SJIS: ／通常, ／オン, ／ダウン */
			static const char *sjis_suffixes[] = {
				"\x81\x5E\x92\xCA\x8F\xED",       /* ／通常 (DEFAULT) */
				"\x81\x5E\x83\x49\x83\x93",       /* ／オン (HOVERED) */
				"\x81\x5E\x83\x5F\x83\x45\x83\x93" /* ／ダウン (CLICKED) */
			};
			/* GBK: ／普通, ／オン, ／ダウン (CN uses 普通 instead of 通常) */
			static const char *gbk_suffixes[] = {
				"\xa3\xaf\xc6\xd5\xcd\xa8",             /* ／普通 (DEFAULT) */
				"\xa3\xaf\xa5\xaa\xa5\xf3",             /* ／オン (HOVERED) */
				"\xa3\xaf\xa5\xc0\xa5\xa6\xa5\xf3"      /* ／ダウン (CLICKED) */
			};
			const char **suffixes = is_gbk_path ? gbk_suffixes : sjis_suffixes;
			for (int st = 0; st < 3; st++) {
				char buf[512];
				snprintf(buf, sizeof(buf), "%s%s", cg_base, suffixes[st]);
				struct string *cg_name = cstr_to_string(buf);
				PE_SetPartsCG(parts_no, cg_name, 0, st + 1);
				free_string(cg_name);
			}
			/* Mark as clickable */
			PE_SetClickable(parts_no, true);
		}
		return;
	}

	/* --- Handle マルチレベルパーツ / ＣＧパーツ: extract CG name from state branches --- */
	/* MultiLevelParts structure:
	 *   種類別情報/
	 *     パーツタイプ = 'マルチレベルパーツ'
	 *     クリップ許可 = 0
	 *     通常状態/        ← normal (state 1)
	 *       パーツタイプ = 'ＣＧパーツ'
	 *       ＣＧ名 = 'resource_name'
	 *       変形 = 0
	 *       サーフェイスエリア = [x, y, w, h]
	 *     オンカーソル状態/  ← on-cursor (state 2)
	 *     キーダウン状態/    ← key-down (state 3)
	 */
	int state_idx = 0;
	for (unsigned i = 0; i < type_info->nr_children; i++) {
		struct ex_tree *state = &type_info->children[i];
		if (state->is_leaf) continue;

		int pe_state = state_idx + 1; /* PE_SetPartsCG uses 1-based state */

		/* Search for ＣＧ名 (CG name) leaf — may be nested in 素材リスト/素材N/ */
		const char *cg_name = pactex_find_cg_name(state, 0);
		if (cg_name) {
			struct string *s = cstr_to_string(cg_name);
			PE_SetPartsCG(parts_no, s, 0, pe_state);
			free_string(s);
		}

		/* Apply サーフェイスエリア (surface area / clip rect) if present */
		int sa_x, sa_y, sa_w, sa_h;
		if (pactex_get_surface_area(state, &sa_x, &sa_y, &sa_w, &sa_h, 0))
			PE_SetPartsCGSurfaceArea(parts_no, sa_x, sa_y, sa_w, sa_h, pe_state);

		/* ＣＧ判定部件 (CG Detection Parts) in normal state → mark as clickable button */
		if (pe_state == 1) {
			const char *stype = pactex_get_string(state, SJIS_PARTS_TYPE);
			if (!stype) stype = pactex_get_string(state, GBK_PARTS_TYPE);
			if (stype && (strstr(stype, GBK_CG_DETECT) || strstr(stype, SJIS_CG_DETECT))) {
				PE_SetClickable(parts_no, true);
			}
		}

		state_idx++;
	}
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

	/* Store user component name from pactex tree node */
	free(p->user_component_name);
	p->user_component_name = strdup(node->name->text);

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

	/* Component type: 0 for containers, 1 for leaf sprites, 17 for UserComponent.
	 * Type 17 = UserComponent — tells game code to instantiate a registered
	 * component template via GetUserComponentManager/AddUserComponent system.
	 * Rule: leaf nodes (no children) without a CG texture → UserComponent. */
	if (buhin && buhin->nr_children > 0) {
		p->component_type = 0;   /* generic container */
	} else {
		/* Check if this leaf has CG data (texture) — if so, it's a sprite.
		 * If no CG, it's a UserComponent placeholder. */
		struct ex_tree *type_info = pactex_find_type_info(node);
		const char *cg_name = NULL;
		if (type_info) {
			for (unsigned ti = 0; ti < type_info->nr_children; ti++) {
				struct ex_tree *state = &type_info->children[ti];
				if (!state->is_leaf) {
					cg_name = pactex_find_cg_name(state, 0);
					if (cg_name) break;
				}
			}
		}
		if (cg_name) {
			p->component_type = 1;   /* Sprite (leaf with CG) */
		} else {
			p->component_type = 0;   /* leaf without CG — let game code handle */
		}
	}

	/* Recurse into child component definitions */
	if (buhin) {
		for (unsigned i = 0; i < buhin->nr_children; i++) {
			struct ex_tree *sub = &buhin->children[i];
			if (!sub->is_leaf) {
				pactex_create_component(act, sub, parts_no, depth + 1);
			}
		}
	}

	/* Apply visual properties (position, show, alpha, CG) from pactex tree */
	pactex_apply_properties(node, parts_no);

	/* NOTE: auto-clickable hack removed — it marked ALL textured leaf parts as
	 * clickable, blocking clicks on actual buttons underneath. Buttons are now
	 * correctly marked clickable by the pactex ボタン (button) CG detection. */
}

// pactex_dump_components removed (Session 51)

/* Parse pactex EX data and populate activity with parts entries. */
static bool pactex_load(struct activity *act, struct ex *ex)
{
	/* Find the tree block — should be block 0 ("アクティビティ") */
	struct ex_tree *tree = NULL;
	for (unsigned i = 0; i < ex->nr_blocks; i++) {
		if (ex->blocks[i].val.type == EX_TREE) {
			tree = ex->blocks[i].val.tree;
			break;
		}
	}
	if (!tree || tree->is_leaf || tree->nr_children == 0) {
		WARNING("pactex: no valid tree block found (nr_blocks=%u)", ex->nr_blocks);
		return false;
	}

	// pactex dump removed (Session 51)

	/* The tree root has one branch per activity variant (usually just one).
	 * Create a root PE parts entry for the first branch. */
	struct ex_tree *root_branch = &tree->children[0];
	if (root_branch->is_leaf) {
		WARNING("pactex: root branch is a leaf, aborting");
		return false;
	}

	int root_no = alloc_activity_parts_no();
	struct parts *root = parts_get(root_no);

	/* Store user component name for root */
	free(root->user_component_name);
	root->user_component_name = strdup(root_branch->name->text);

	/* Root container — type 0 (not 17, which would trigger UserComponent lookup) */
	struct ex_tree *root_buhin = pactex_find_buhin(root_branch);
	root->component_type = 0;

	/* Register root with actual name, empty name sentinel, and "ルートパーツ" alias.
	 * The game looks up root parts by various names:
	 *   - actual pactex name (e.g. "アクティビティ")
	 *   - empty string ""
	 *   - "ルートパーツ" (root parts) — hardcoded in CActivityWrap@Load */
	if (act->nr_parts < MAX_ACTIVITY_PARTS) {
		struct activity_part *ap = &act->parts[act->nr_parts++];
		snprintf(ap->name, sizeof(ap->name), "%s", root_branch->name->text);
		ap->number = root_no;
	}
	if (act->nr_parts < MAX_ACTIVITY_PARTS) {
		struct activity_part *ap = &act->parts[act->nr_parts++];
		ap->name[0] = '\0';
		ap->number = root_no;
	}
	/* "ルートパーツ" (root parts) — SJIS or GBK depending on AIN encoding */
	if (act->nr_parts < MAX_ACTIVITY_PARTS) {
		struct activity_part *ap = &act->parts[act->nr_parts++];
		if (ain_is_gb18030) {
			/* GBK encoding of "ルートパーツ" */
			struct string *gbk = sjis_to_gbk_string(
				"\x83\x8b\x81\x5b\x83\x67\x83\x70\x81\x5b\x83\x63", 12);
			snprintf(ap->name, sizeof(ap->name), "%s", gbk->text);
			free_string(gbk);
		} else {
			snprintf(ap->name, sizeof(ap->name),
				"\x83\x8b\x81\x5b\x83\x67\x83\x70\x81\x5b\x83\x63");
		}
		ap->number = root_no;
	}

	/* Process children from the root's component branch */
	struct ex_tree *buhin = root_buhin;
	if (buhin && !buhin->is_leaf) {
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

	/* Apply properties to root component too */
	pactex_apply_properties(root_branch, root_no);

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
	act->nr_close_parts = 0;
	nr_activities++;
	return true;
}

static void release_parts_recursive(int parts_no)
{
	struct parts *p = parts_try_get(parts_no);
	if (!p) return;
	while (!TAILQ_EMPTY(&p->children)) {
		struct parts *child = TAILQ_FIRST(&p->children);
		release_parts_recursive(child->no);
	}
	parts_release(parts_no);
}

static bool PartsEngine_ReleaseActivity(struct string *name, int erase_list)
{
	int idx = find_activity(name);
	if (idx < 0) return false;
	struct activity *act = &activities[idx];
	for (int i = 0; i < act->nr_parts; i++)
		release_parts_recursive(act->parts[i].number);
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

	/* Try: full path.pactex (forward slash) */
	if (!dfile && base != fname) {
		snprintf(pactex_name, sizeof(pactex_name), "%s.pactex", fname);
		dfile = asset_get_by_name(ASSET_PACT, pactex_name, NULL);
	}

	/* Try: full path.pactex (backslash — AlicArch v2 uses backslash separators) */
	if (!dfile && base != fname) {
		snprintf(pactex_name, sizeof(pactex_name), "%s.pactex", fname);
		for (char *p = pactex_name; *p; p++) {
			if (*p == '/') *p = '\\';
		}
		dfile = asset_get_by_name(ASSET_PACT, pactex_name, NULL);
	}

	/* Try: name.pactex (the activity name, not filename) */
	if (!dfile) {
		snprintf(pactex_name, sizeof(pactex_name), "%s.pactex", name->text);
		dfile = asset_get_by_name(ASSET_PACT, pactex_name, NULL);
	}

	if (!dfile) {
		static int pact_miss = 0;
		if (pact_miss++ < 10)
			WARNING("pactex NOT FOUND for activity '%s' filename '%s'",
				name->text, fname);
	}

	if (dfile) {
		struct ex *ex = ex_read(dfile->data, dfile->size);
		archive_free_data(dfile);
		if (ex) {
			bool ok = pactex_load(act, ex);
			ex_free(ex);
			if (ok)
				return true;
		}
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
	if (asset_exists_by_name(ASSET_PACT, pactex_name, NULL))
		return true;
	if (base != fname) {
		snprintf(pactex_name, sizeof(pactex_name), "%s.pactex", fname);
		if (asset_exists_by_name(ASSET_PACT, pactex_name, NULL))
			return true;
	}
	return false;
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
	if (idx < 0) return;
	struct activity *act = &activities[idx];
	for (int i = 0; i < act->nr_parts; i++)
		release_parts_recursive(act->parts[i].number);
	act->nr_parts = 0;
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
	if (idx < 0) { WARNING("GetActivityPartsNumber: act='%s' NOT FOUND (looking for '%s')", name->text, parts_name->text); return -1; }
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
		if (act->parts[i].name[0] && !strcmp(act->parts[i].name, parts_name->text)) {
			return act->parts[i].number;
		}
	}

	WARNING("GetActivityPartsNumber: act='%s' parts='%s' NOT FOUND", name->text, parts_name->text);
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

static void PartsEngine_AddActivityCloseParts(struct string *name, struct string *parts_name)
{
	int idx = find_activity(name);
	if (idx < 0) return;
	struct activity *act = &activities[idx];
	if (act->nr_close_parts >= MAX_ACTIVITY_PARTS) return;
	snprintf(act->close_parts[act->nr_close_parts], 256, "%s", parts_name->text);
	act->nr_close_parts++;
}

static bool PartsEngine_IsExistActivityCloseParts(struct string *name, struct string *parts_name)
{
	int idx = find_activity(name);
	if (idx < 0) return false;
	struct activity *act = &activities[idx];
	for (int i = 0; i < act->nr_close_parts; i++) {
		if (!strcmp(act->close_parts[i], parts_name->text))
			return true;
	}
	return false;
}
static void PartsEngine_AddActivityLockedParts(struct string *name, struct string *parts) {}
static bool PartsEngine_IsExistActivityLockedParts(struct string *name, struct string *parts) { return false; }

static void PartsEngine_SetActivityEndKey(struct string *name, int key) {}
static void PartsEngine_EraseActivityEndKey(struct string *name, int key) {}
static bool PartsEngine_IsExistActivityEndKey(struct string *name, int key) { return false; }
static int PartsEngine_NumofActivityEndKey(struct string *name) { return 0; }
static int PartsEngine_GetActivityEndKey(struct string *name, int index) { return 0; }

// v14 stubs for input/wheel configuration
static void PartsEngine_SetEnableInputProcess(possibly_unused int parts_no,
	possibly_unused bool enable) {}
static void PartsEngine_Parts_SetWheelable(possibly_unused int parts_no,
	possibly_unused bool wheelable) {}

/* Register the Activity API into lib_PartsEngine. Called from
 * PartsEngine's _PreLink when the AIN declares CreateActivity (v14). */
void pe_v14_activity_prelink(void)
{
	struct static_library *lib = &lib_PartsEngine;
	static_library_register(lib, "IsExistActivity", PartsEngine_IsExistActivity);
	static_library_register(lib, "CreateActivity", PartsEngine_CreateActivity);
	static_library_register(lib, "ReleaseActivity", PartsEngine_ReleaseActivity);
	static_library_register(lib, "SaveActivityEXText", PartsEngine_SaveActivityEXText);
	static_library_register(lib, "LoadActivityEXText", PartsEngine_LoadActivityEXText);
	static_library_register(lib, "ReadActivityFile", PartsEngine_ReadActivityFile);
	static_library_register(lib, "WriteActivityFile", PartsEngine_WriteActivityFile);
	static_library_register(lib, "IsExistActivityFile", PartsEngine_IsExistActivityFile);
	static_library_register(lib, "AddActivityParts", PartsEngine_AddActivityParts);
	static_library_register(lib, "RemoveActivityParts", PartsEngine_RemoveActivityParts);
	static_library_register(lib, "RemoveAllActivityParts", PartsEngine_RemoveAllActivityParts);
	static_library_register(lib, "NumofActivityParts", PartsEngine_NumofActivityParts);
	static_library_register(lib, "GetActivityParts", PartsEngine_GetActivityParts);
	static_library_register(lib, "IsExistActivityPartsByName", PartsEngine_IsExistActivityPartsByName);
	static_library_register(lib, "IsExistActivityPartsByNumber", PartsEngine_IsExistActivityPartsByNumber);
	static_library_register(lib, "GetActivityPartsNumber", PartsEngine_GetActivityPartsNumber);
	static_library_register(lib, "GetActivityPartsName", PartsEngine_GetActivityPartsName);
	static_library_register(lib, "AddActivityCloseParts", PartsEngine_AddActivityCloseParts);
	static_library_register(lib, "IsExistActivityCloseParts", PartsEngine_IsExistActivityCloseParts);
	static_library_register(lib, "AddActivityLockedParts", PartsEngine_AddActivityLockedParts);
	static_library_register(lib, "IsExistActivityLockedParts", PartsEngine_IsExistActivityLockedParts);
	static_library_register(lib, "SetActivityEndKey", PartsEngine_SetActivityEndKey);
	static_library_register(lib, "EraseActivityEndKey", PartsEngine_EraseActivityEndKey);
	static_library_register(lib, "IsExistActivityEndKey", PartsEngine_IsExistActivityEndKey);
	static_library_register(lib, "NumofActivityEndKey", PartsEngine_NumofActivityEndKey);
	static_library_register(lib, "GetActivityEndKey", PartsEngine_GetActivityEndKey);
	static_library_register(lib, "SetEnableInputProcess", PartsEngine_SetEnableInputProcess);
	static_library_register(lib, "Parts_SetWheelable", PartsEngine_Parts_SetWheelable);
}
