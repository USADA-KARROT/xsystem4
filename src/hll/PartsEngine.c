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
#include <SDL.h>

#include "system4/ain.h"
#include "system4/archive.h"
#include "system4/ex.h"
#include "system4/string.h"
#include "system4/utfsjis.h"

#include "asset_manager.h"
#include "gfx/gfx.h"
#include "input.h"
#include "movie.h"
#include "parts.h"
#include "../parts/parts_internal.h"
#include "scene.h"
#include "hll.h"

extern void parts_update_animation(int passed_time);
extern struct string *sjis_to_gbk_string(const char *src, size_t len);

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
			pw = sz->items[0].value.i;
			ph = sz->items[1].value.i;
		}
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

static void PartsEngine_AddActivityCloseParts(struct string *name, struct string *parts) {}
static bool PartsEngine_IsExistActivityCloseParts(struct string *name, struct string *parts) { return false; }
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

/* v14 Controller system — manages UI layers (input focus stack).
 * Each controller has a unique ID and occupies a position in the stack.
 * The game uses this to manage focus between UI layers (dialog over game, etc.) */
#define MAX_CONTROLLERS 64
static struct {
	int ids[MAX_CONTROLLERS];
	int count;
	int active;
	int next_id;
} controllers = { .ids = {0}, .count = 0, .active = 0, .next_id = 0 };

static int PartsEngine_AddController(int index)
{
	if (controllers.count >= MAX_CONTROLLERS) return -1;
	int id = controllers.next_id++;
	/* Insert at position 'index' (clamped). -1 or beyond count → append at end */
	if (index < 0) index = controllers.count;
	if (index > controllers.count) index = controllers.count;
	for (int i = controllers.count; i > index; i--)
		controllers.ids[i] = controllers.ids[i-1];
	controllers.ids[index] = id;
	controllers.count++;
	/* New layer becomes the active layer (scene stack: newest = active) */
	controllers.active = id;
	return id;
}

static void PartsEngine_RemoveController(int erase_list, int index)
{
	/* Find controller by index and remove it */
	if (index < 0 || index >= controllers.count) return;
	int removed_id = controllers.ids[index];
	for (int i = index; i < controllers.count - 1; i++)
		controllers.ids[i] = controllers.ids[i+1];
	controllers.count--;
	/* If active was this layer, make the new top layer active (ID-based, not index) */
	if (controllers.active == removed_id) {
		controllers.active = (controllers.count > 0)
			? controllers.ids[controllers.count - 1]
			: -1;
	}
}

static void PartsEngine_MoveController(int id, int new_index) {
	/* Find and move controller to new position */
	int old_idx = -1;
	for (int i = 0; i < controllers.count; i++) {
		if (controllers.ids[i] == id) { old_idx = i; break; }
	}
	if (old_idx < 0) return;
	int saved_id = controllers.ids[old_idx];
	for (int i = old_idx; i < controllers.count - 1; i++)
		controllers.ids[i] = controllers.ids[i+1];
	if (new_index >= controllers.count) new_index = controllers.count - 1;
	if (new_index < 0) new_index = 0;
	for (int i = controllers.count - 1; i > new_index; i--)
		controllers.ids[i] = controllers.ids[i-1];
	controllers.ids[new_index] = saved_id;
}

static int PartsEngine_GetControllerIndex(int id) {
	for (int i = 0; i < controllers.count; i++) {
		if (controllers.ids[i] == id) return i;
	}
	return -1;
}

static int PartsEngine_GetControllerID(int index) {
	if (index < 0 || index >= controllers.count) return -1;
	return controllers.ids[index];
}

static int PartsEngine_GetControllerLength(void) {
	return controllers.count;
}
static int PartsEngine_GetSystemOverlayController(void) { return 0; }

static void PartsEngine_Update_Pascha3PC(struct string *xxx1, struct string *xxx2, int passed_time, bool is_skip, bool message_window_show)
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

static int PartsEngine_GetActiveController(void)
{
	return controllers.active;
}

static void PartsEngine_SetActiveController(int controller)
{
	controllers.active = controller;
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
	// v14: game bytecode may reference parts numbers allocated by
	// GetFreeNumber (>=1000000000) before the PE entry exists.
	// Auto-create the entry so Wrap/IsValid can work.
	if (number >= 1000000000) {
		p = parts_get(number);
		return p->component_type;  // 0 = default
	}
	WARNING("GetComponentType: parts %d not found (state=%d) - caller may have wrong parts number from vtable dispatch", number, state);
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
static void PartsEngine_GetComponentAbsolutePos(int n, float *x, float *y) { if (x) *x = 0; if (y) *y = 0; }
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
/* v14 stub: Parts_SetComment/GetComment — metadata label, no visual effect */
static void PartsEngine_Parts_SetComment(int parts_no, struct string *comment) {
	(void)parts_no;
	(void)comment;
}
static struct string *PartsEngine_Parts_GetComment(int parts_no) { (void)parts_no; return cstr_to_string(""); }

/*
 * v14: AddPartsConstructionProcess
 *
 * CASConstructionProcess has 4 arrays: ArrayInt, ArrayFloat, ArrayString, ArrayPos
 * Passed as wrap<array> heap slots. ArrayInt indices (from setter method names):
 *   [0] = Command (type enum — see v14_cp_type below)
 *   [1] = InterpolationType
 *   [2] = SrcX      [3] = SrcY      [4] = SrcWidth    [5] = SrcHeight
 *   [6] = DestX     [7] = DestY     [8] = DestX2       [9] = DestY2
 *  [10] = DestWidth [11] = DestHeight
 *  [12] = R        [13] = G        [14] = B          [15] = A
 *  [16] = R2       [17] = G2       [18] = B2         [19] = A2
 *  [20] = CharSpace [21] = LineSpace [22] = FontProperty
 *  [30] = FullSize  [34] = LineWidth [35] = RoundEdge  [36] = RoundCorner
 *
 * ArrayString: [0] = Text, [1] = CGName
 * ArrayFloat:  [0] = RadiusX?, [1] = RadiusY?, [2] = Angle?, [3] = StartAngle?, [4] = SweepAngle?
 */
enum v14_cp_type {
	V14_CP_CREATE = 0,
	V14_CP_CREATE_PIXEL_ONLY = 1,
	V14_CP_CREATE_CG = 2,
	V14_CP_FILL = 3,
	V14_CP_FILL_ALPHA_COLOR = 4,
	V14_CP_FILL_AMAP = 5,
	V14_CP_FILL_WITH_ALPHA = 6,
	V14_CP_DRAW_TEXT = 7,
	V14_CP_COPY_TEXT = 8,
	V14_CP_FILL_GRADATION_HORIZON = 9,
	V14_CP_DRAW_RECT = 10,
	V14_CP_CUT_CG_BLEND = 11,
	V14_CP_CUT_CG_COPY = 12,
	V14_CP_CUT_CG_SCALE_BLEND = 13,
	V14_CP_CUT_CG_SCALE_COPY = 14,
	V14_CP_GRAY_FILTER = 15,
	V14_CP_ADD_FILTER = 16,
	V14_CP_MUL_FILTER = 17,
	V14_CP_DRAW_LINE = 18,
	V14_CP_CUT_CG_ALPHA_BLEND = 19,
	V14_CP_CUT_CG_SCALE_ALPHA_BLEND = 20,
	V14_CP_CUT_CG_ONLY_ALPHA = 21,
	V14_CP_CUT_CG_SCALE_ONLY_ALPHA = 22,
	V14_CP_ALPHA_BLEND_TEXT = 23,
	V14_CP_ONLY_ALPHA_TEXT = 24,
	V14_CP_MUL_AMAP_GRADATION_HORIZON = 25,
	V14_CP_MUL_AMAP_GRADATION_VERTICAL = 26,
	V14_CP_HBLUR_FILTER = 27,
	V14_CP_VBLUR_FILTER = 28,
	V14_CP_CG_BLEND = 29,
	V14_CP_DRAW_LINE_WITH_ALPHA = 30,
	V14_CP_DRAW_CIRCLE_ALPHA_BLEND_IN_RECT = 57,
};

/*
 * wrap_get_backing_array — extract the backing ARRAY_PAGE from a wrap<array<T>> slot.
 *
 * In v14, wrap<array<T>> may be:
 *   (a) A simple wrap container: member[0] → inner slot → ARRAY_PAGE
 *   (b) An IArray<T> implementation class (STRUCT_PAGE with ~40 members):
 *       the backing array is stored in a member of AIN array type.
 *
 * This function handles both cases by:
 *   1. Checking if the slot directly holds an ARRAY_PAGE
 *   2. Trying the simple wrap path (member[0])
 *   3. Using the AIN struct definition to find the array-type member
 *   4. Falling back to scanning for the first ARRAY_PAGE member
 */
static struct page *wrap_get_backing_array(int slot)
{
	if (slot < 0 || (size_t)slot >= heap_size) return NULL;
	if (heap[slot].type != VM_PAGE || !heap[slot].page) return NULL;
	struct page *p = heap[slot].page;

	// Case 1: slot directly holds an ARRAY_PAGE
	if (p->type == ARRAY_PAGE)
		return p;

	// Case 2: simple wrap — member[0] is inner slot pointing to ARRAY_PAGE
	if (p->nr_vars > 0) {
		int inner = p->values[0].i;
		if (inner > 0 && (size_t)inner < heap_size
		    && heap[inner].type == VM_PAGE && heap[inner].page
		    && heap[inner].page->type == ARRAY_PAGE)
			return heap[inner].page;
	}

	// Case 3: IArray class — use AIN struct definition to find array member
	if (p->type == STRUCT_PAGE && p->index >= 0
	    && ain && p->index < ain->nr_structures) {
		struct ain_struct *st = &ain->structures[p->index];
		for (int k = 0; k < st->nr_members && k < p->nr_vars; k++) {
			switch (st->members[k].type.data) {
			case AIN_ARRAY_TYPE:
			case AIN_ARRAY: {
				int v = p->values[k].i;
				if (v > 0 && (size_t)v < heap_size
				    && heap[v].type == VM_PAGE && heap[v].page
				    && heap[v].page->type == ARRAY_PAGE)
					return heap[v].page;
				break;
			}
			default:
				break;
			}
		}
	}

	// Case 4: fallback — scan all members for first ARRAY_PAGE
	if (p->type == STRUCT_PAGE) {
		for (int k = 0; k < p->nr_vars; k++) {
			int v = p->values[k].i;
			if (v > 0 && (size_t)v < heap_size
			    && heap[v].type == VM_PAGE && heap[v].page
			    && heap[v].page->type == ARRAY_PAGE)
				return heap[v].page;
		}
	}

	return NULL;
}

static void PartsEngine_AddPartsConstructionProcess(int parts_no, int wi_slot, int wf_slot, int ws_slot, int wp_slot, int state)
{
	struct page *ints = wrap_get_backing_array(wi_slot);
	if (!ints || ints->nr_vars < 1) {
		return;
	}
	// Sanity check: ints[0] (command) should be 0-200 range
	if (ints->values[0].i > 200 || ints->values[0].i < 0) {
		return;
	}

	int cmd = ints->values[0].i;

	// Trace: dump ints array for debugging Construction
	{
		static int apt = 0;
		if (apt < 5 && 0) { // disabled trace
			WARNING("AddCP: parts=%d cmd=%d nr_vars=%d vals=[", parts_no, cmd, ints->nr_vars);
			for (int _j = 0; _j < ints->nr_vars && _j < 16; _j++)
				WARNING("  [%d]=%d", _j, ints->values[_j].i);
			apt++;
		}
	}

	int dx = ints->nr_vars > 6 ? ints->values[6].i : 0;
	int dy = ints->nr_vars > 7 ? ints->values[7].i : 0;
	int dx2 = ints->nr_vars > 8 ? ints->values[8].i : 0;
	int dy2 = ints->nr_vars > 9 ? ints->values[9].i : 0;
	int dw = ints->nr_vars > 10 ? ints->values[10].i : 0;
	int dh = ints->nr_vars > 11 ? ints->values[11].i : 0;
	int r  = ints->nr_vars > 12 ? ints->values[12].i : 0;
	int g  = ints->nr_vars > 13 ? ints->values[13].i : 0;
	int b  = ints->nr_vars > 14 ? ints->values[14].i : 0;
	int a  = ints->nr_vars > 15 ? ints->values[15].i : 255;
	int interp = ints->nr_vars > 1 ? ints->values[1].i : 0;
	int sx = ints->nr_vars > 2 ? ints->values[2].i : 0;
	int sy = ints->nr_vars > 3 ? ints->values[3].i : 0;
	int sw = ints->nr_vars > 4 ? ints->values[4].i : 0;
	int sh = ints->nr_vars > 5 ? ints->values[5].i : 0;

	/* Get CG name from ArrayString if available */
	struct page *strs = wrap_get_backing_array(ws_slot);
	struct string *cg_name = NULL;
	struct string *text = NULL;
	if (strs) {
		if (strs->nr_vars > 1) {
			int s = strs->values[1].i;
			if (s > 0 && heap_index_valid(s) && heap[s].s)
				cg_name = heap[s].s;
		}
		if (strs->nr_vars > 0) {
			int s = strs->values[0].i;
			if (s > 0 && heap_index_valid(s) && heap[s].s)
				text = heap[s].s;
		}
	}

	switch (cmd) {
	case V14_CP_CREATE:
		if (dw == 0 && dh == 0) {
			dw = dx2 > 0 ? dx2 : 0;
			dh = dy2 > 0 ? dy2 : 0;
		}
		PE_AddCreateToPartsConstructionProcess(parts_no, dw, dh, state);
		break;
	case V14_CP_CREATE_PIXEL_ONLY:
		if (dw == 0 && dh == 0) {
			dw = dx2 > 0 ? dx2 : 0;
			dh = dy2 > 0 ? dy2 : 0;
		}
		PE_AddCreatePixelOnlyToPartsConstructionProcess(parts_no, dw, dh, state);
		break;
	case V14_CP_CREATE_CG:
		if (cg_name)
			PE_AddCreateCGToProcess(parts_no, cg_name, state);
		break;
	case V14_CP_FILL: {
		int fw = dw > 0 ? dw : (dx2 > 0 ? dx2 : 16384);
		int fh = dh > 0 ? dh : (dy2 > 0 ? dy2 : 16384);
		PE_AddFillToPartsConstructionProcess(parts_no, dx, dy, fw, fh, r, g, b, state);
		break;
	}
	case V14_CP_FILL_ALPHA_COLOR: {
		int fw = dw > 0 ? dw : (dx2 > 0 ? dx2 : 16384);
		int fh = dh > 0 ? dh : (dy2 > 0 ? dy2 : 16384);
		PE_AddFillAlphaColorToPartsConstructionProcess(parts_no, dx, dy, fw, fh, r, g, b, a, state);
		break;
	}
	case V14_CP_FILL_AMAP: {
		int fw = dw > 0 ? dw : (dx2 > 0 ? dx2 : 16384);
		int fh = dh > 0 ? dh : (dy2 > 0 ? dy2 : 16384);
		PE_AddFillAMapToPartsConstructionProcess(parts_no, dx, dy, fw, fh, a, state);
		break;
	}
	case V14_CP_FILL_WITH_ALPHA: {
		int fw = dw > 0 ? dw : (dx2 > 0 ? dx2 : 16384);
		int fh = dh > 0 ? dh : (dy2 > 0 ? dy2 : 16384);
		PE_AddFillAlphaColorToPartsConstructionProcess(parts_no, dx, dy, fw, fh, r, g, b, a, state);
		break;
	}
	case V14_CP_DRAW_TEXT:
		if (text) {
			int font_type = ints->nr_vars > 22 ? ints->values[22].i : 0;
			int font_size = ints->nr_vars > 30 ? ints->values[30].i : 16;
			int char_space = ints->nr_vars > 20 ? ints->values[20].i : 0;
			int line_space = ints->nr_vars > 21 ? ints->values[21].i : 0;
			struct page *floats = wrap_get_backing_array(wf_slot);
			float bold_weight = (floats && floats->nr_vars > 0) ? floats->values[0].f : 0.0f;
			float edge_weight = (floats && floats->nr_vars > 1) ? floats->values[1].f : 0.0f;
			int r2 = ints->nr_vars > 16 ? ints->values[16].i : 0;
			int g2 = ints->nr_vars > 17 ? ints->values[17].i : 0;
			int b2 = ints->nr_vars > 18 ? ints->values[18].i : 0;
			PE_AddDrawTextToPartsConstructionProcess(parts_no, dx, dy, text,
				font_type, font_size, r, g, b, bold_weight,
				r2, g2, b2, edge_weight, char_space, line_space, state);
		}
		break;
	case V14_CP_COPY_TEXT:
		if (text) {
			int font_type = ints->nr_vars > 22 ? ints->values[22].i : 0;
			int font_size = ints->nr_vars > 30 ? ints->values[30].i : 16;
			int char_space = ints->nr_vars > 20 ? ints->values[20].i : 0;
			int line_space = ints->nr_vars > 21 ? ints->values[21].i : 0;
			struct page *floats = wrap_get_backing_array(wf_slot);
			float bold_weight = (floats && floats->nr_vars > 0) ? floats->values[0].f : 0.0f;
			float edge_weight = (floats && floats->nr_vars > 1) ? floats->values[1].f : 0.0f;
			int r2 = ints->nr_vars > 16 ? ints->values[16].i : 0;
			int g2 = ints->nr_vars > 17 ? ints->values[17].i : 0;
			int b2 = ints->nr_vars > 18 ? ints->values[18].i : 0;
			PE_AddCopyTextToPartsConstructionProcess(parts_no, dx, dy, text,
				font_type, font_size, r, g, b, bold_weight,
				r2, g2, b2, edge_weight, char_space, line_space, state);
		}
		break;
	case V14_CP_FILL_GRADATION_HORIZON: {
		/* Gradient fill: use RGBA + RGBA2 for start/end colors, approximate with fill */
		int fw = dw > 0 ? dw : (dx2 > 0 ? dx2 : 16384);
		int fh = dh > 0 ? dh : (dy2 > 0 ? dy2 : 16384);
		PE_AddFillAlphaColorToPartsConstructionProcess(parts_no, dx, dy, fw, fh, r, g, b, a, state);
		break;
	}
	case V14_CP_DRAW_RECT: {
		int fw = dw > 0 ? dw : dx2;
		int fh = dh > 0 ? dh : dy2;
		PE_AddDrawRectToPartsConstructionProcess(parts_no, dx, dy, fw, fh, r, g, b, state);
		break;
	}
	case V14_CP_CUT_CG_BLEND:
	case V14_CP_CUT_CG_SCALE_BLEND:
	case V14_CP_CUT_CG_ALPHA_BLEND:
	case V14_CP_CUT_CG_SCALE_ALPHA_BLEND:
	case V14_CP_CG_BLEND:
		if (cg_name)
			PE_AddDrawCutCGToPartsConstructionProcess(parts_no, cg_name,
				dx, dy, dw, dh, sx, sy, sw, sh, interp, state);
		break;
	case V14_CP_CUT_CG_COPY:
	case V14_CP_CUT_CG_SCALE_COPY:
	case V14_CP_CUT_CG_ONLY_ALPHA:
	case V14_CP_CUT_CG_SCALE_ONLY_ALPHA:
		if (cg_name)
			PE_AddCopyCutCGToPartsConstructionProcess(parts_no, cg_name,
				dx, dy, dw, dh, sx, sy, sw, sh, interp, state);
		break;
	case V14_CP_GRAY_FILTER:
	case V14_CP_ADD_FILTER:
	case V14_CP_MUL_FILTER:
	case V14_CP_HBLUR_FILTER:
	case V14_CP_VBLUR_FILTER: {
		/* Filter stubs — no-op, the parts texture is already rendered */
		static int filter_warn = 0;
		if (filter_warn++ < 3)
			WARNING("AddPartsConstructionProcess: filter type %d (stub) parts=%d", cmd, parts_no);
		break;
	}
	case V14_CP_DRAW_LINE:
	case V14_CP_DRAW_LINE_WITH_ALPHA: {
		int x1 = dx, y1 = dy, x2 = dx2, y2 = dy2;
		int lx = x1 < x2 ? x1 : x2;
		int ly = y1 < y2 ? y1 : y2;
		int lw = abs(x2 - x1);
		int lh = abs(y2 - y1);
		if (lw == 0) lw = 1;
		if (lh == 0) lh = 1;
		PE_AddFillAlphaColorToPartsConstructionProcess(parts_no, lx, ly, lw, lh, r, g, b, a, state);
		break;
	}
	case V14_CP_ALPHA_BLEND_TEXT:
	case V14_CP_ONLY_ALPHA_TEXT:
		/* Text with alpha blending — use DrawText as approximation */
		if (text) {
			int font_type = ints->nr_vars > 22 ? ints->values[22].i : 0;
			int font_size = ints->nr_vars > 30 ? ints->values[30].i : 16;
			int char_space = ints->nr_vars > 20 ? ints->values[20].i : 0;
			int line_space = ints->nr_vars > 21 ? ints->values[21].i : 0;
			struct page *floats = wrap_get_backing_array(wf_slot);
			float bold_weight = (floats && floats->nr_vars > 0) ? floats->values[0].f : 0.0f;
			float edge_weight = (floats && floats->nr_vars > 1) ? floats->values[1].f : 0.0f;
			int r2 = ints->nr_vars > 16 ? ints->values[16].i : 0;
			int g2 = ints->nr_vars > 17 ? ints->values[17].i : 0;
			int b2 = ints->nr_vars > 18 ? ints->values[18].i : 0;
			PE_AddDrawTextToPartsConstructionProcess(parts_no, dx, dy, text,
				font_type, font_size, r, g, b, bold_weight,
				r2, g2, b2, edge_weight, char_space, line_space, state);
		}
		break;
	case V14_CP_MUL_AMAP_GRADATION_HORIZON:
	case V14_CP_MUL_AMAP_GRADATION_VERTICAL: {
		/* Gradient alpha map — approximate with FillAMap */
		int fw = dw > 0 ? dw : (dx2 > 0 ? dx2 : 16384);
		int fh = dh > 0 ? dh : (dy2 > 0 ? dy2 : 16384);
		PE_AddFillAMapToPartsConstructionProcess(parts_no, dx, dy, fw, fh, a, state);
		break;
	}
	case V14_CP_DRAW_CIRCLE_ALPHA_BLEND_IN_RECT: {
		/* Circle draw — approximate with fill alpha color */
		int fw = dw > 0 ? dw : (dx2 > 0 ? dx2 : 16384);
		int fh = dh > 0 ? dh : (dy2 > 0 ? dy2 : 16384);
		PE_AddFillAlphaColorToPartsConstructionProcess(parts_no, dx, dy, fw, fh, r, g, b, a, state);
		break;
	}
	default: {
		static int cp_warn = 0;
		if (cp_warn++ < 10) {
			WARNING("AddPartsConstructionProcess: unknown type %d parts=%d "
				"ints_nr=%d ints[0..3]=%d,%d,%d,%d",
				cmd, parts_no, ints->nr_vars,
				ints->nr_vars > 0 ? ints->values[0].i : -1,
				ints->nr_vars > 1 ? ints->values[1].i : -1,
				ints->nr_vars > 2 ? ints->values[2].i : -1,
				ints->nr_vars > 3 ? ints->values[3].i : -1);
		}
		break;
	}
	}
}

static void PartsEngine_ClearChild(int number) {
	struct parts *p = parts_try_get(number);
	if (!p) return;
	while (!TAILQ_EMPTY(&p->children)) {
		struct parts *child = TAILQ_FIRST(&p->children);
		TAILQ_REMOVE(&p->children, child, child_list_entry);
		child->parent = NULL;
	}
}
static void PartsEngine_AddChild(int number, int child) {
	PE_SetParentPartsNumber(child, number);
}
static void PartsEngine_InsertChild(int n, int idx, int child) { PE_SetParentPartsNumber(child, n); }
static void PartsEngine_RemoveChild(int number, int child_no) {
	struct parts *p = parts_try_get(number);
	struct parts *c = parts_try_get(child_no);
	if (!p || !c || c->parent != p) return;
	TAILQ_REMOVE(&p->children, c, child_list_entry);
	c->parent = NULL;
}
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
static int PartsEngine_GetChildIndex(int number, int child_no) {
	struct parts *p = parts_try_get(number);
	if (!p) return -1;
	int i = 0;
	struct parts *child;
	PARTS_FOREACH_CHILD(child, p) {
		if (child->no == child_no) return i;
		i++;
	}
	return -1;
}
static int PartsEngine_GetChild(int number, int index) {
	struct parts *p = parts_try_get(number);
	if (!p)
		return -1;
	int i = 0;
	struct parts *child;
	PARTS_FOREACH_CHILD(child, p) {
		if (i == index)
			return child->no;
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

static int possibly_unused PartsEngine_GetUniqueID(int number)
{
	struct parts *p = parts_try_get(number);
	return p ? p->unique_id : 0;
}

static void PartsEngine_SetUserComponentName(int n, struct string *name) {
	struct parts *p = parts_try_get(n);
	if (!p) return;
	if (name && name->text && name->text[0]) {
		free(p->user_component_name);
		p->user_component_name = strdup(name->text);
	}
}
static struct string *PartsEngine_GetUserComponentName(int n) {
	struct parts *p = parts_try_get(n);
	if (p && p->user_component_name)
		return cstr_to_string(p->user_component_name);
	return string_ref(&EMPTY_STRING);
}
static void PartsEngine_SetUserComponentData(int n, struct string *key, struct string *val) {}
static struct string *PartsEngine_GetUserComponentData(int n, struct string *key) { return string_ref(&EMPTY_STRING); }

/* --- Panel support --- */
static void PartsEngine_SetPanelSize(int parts_no, int w, int h)
{
	PE_ClearPartsConstructionProcess(parts_no, 1);
	PE_AddCreateToPartsConstructionProcess(parts_no, w, h, 1);
	PE_BuildPartsConstructionProcess(parts_no, 1);
}

static void PartsEngine_SetPanelColor(int parts_no, int r, int g, int b, int a)
{
	struct parts *p = parts_try_get(parts_no);
	if (!p) return;
	struct parts_construction_process *cproc =
		parts_get_construction_process(p, 0); /* state 0 = internal index for state 1 */
	int w = cproc->common.w;
	int h = cproc->common.h;
	if (w <= 0 || h <= 0) return;
	PE_AddFillAlphaColorToPartsConstructionProcess(parts_no, 0, 0, w, h, r, g, b, a, 1);
	PE_BuildPartsConstructionProcess(parts_no, 1);
}

/* Part properties (v14 variants with state parameter) */
static void PartsEngine_SetWantSaveBackScene(int n, bool enable) {}
static bool PartsEngine_IsWantSaveBackScene(int n) { return false; }
static void PartsEngine_SaveThumbnail_v14(struct string *name, int width) {}
static int PartsEngine_GetActiveParts(void) { return PE_GetClickPartsNumber(); }
static void PartsEngine_SetClickMissSoundName(struct string *name) {}
static struct string *PartsEngine_GetClickMissSoundName(void) { return string_ref(&EMPTY_STRING); }
static int PartsEngine_GetClickNumber(void) {
	return PE_GetClickPartsNumber();
}
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
		handle_events();
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

/* ---- Message queue ----
 * When a parts button is clicked, we enqueue a message.
 * The game polls PopMessage() each frame and reads type/parts/variables.
 *
 * Message types (inferred from bytecode SWITCH in CPartsMessageManager@CallDelegate):
 *   -1 = none (queue empty)
 *    1 = button click (per-part)
 *    4 = mouse left click (whole screen, triggers WholeMouseLClickEvent)
 *    5 = mouse right click
 *    6 = mouse middle click
 */
#define MSG_QUEUE_SIZE 64
struct parts_message {
	int type;
	int parts_no;
	int delegate_index;
	int unique_id;
	int vars[4];
	int nr_vars;
};
static struct parts_message msg_queue[MSG_QUEUE_SIZE];
static int msg_head = 0, msg_tail = 0;
static struct parts_message msg_current = { .type = -1 };

void parts_enqueue_message(int type, int parts_no, int delegate_index, int unique_id)
{
	parts_enqueue_message_vars(type, parts_no, delegate_index, unique_id, 0, NULL);
}

void parts_enqueue_message_vars(int type, int parts_no, int delegate_index, int unique_id,
                                int nr_vars, const int *vars)
{
	int next = (msg_tail + 1) % MSG_QUEUE_SIZE;
	if (next == msg_head) {
		WARNING("parts message queue full, dropping message type=%d parts=%d", type, parts_no);
		return;
	}
	msg_queue[msg_tail].type = type;
	msg_queue[msg_tail].parts_no = parts_no;
	msg_queue[msg_tail].delegate_index = delegate_index;
	msg_queue[msg_tail].unique_id = unique_id;
	int nv = (nr_vars > 4) ? 4 : nr_vars;
	msg_queue[msg_tail].nr_vars = nv;
	for (int i = 0; i < nv; i++)
		msg_queue[msg_tail].vars[i] = vars[i];
	msg_tail = next;
}

static void PartsEngine_PopMessage(void)
{
	if (msg_head != msg_tail) {
		msg_current = msg_queue[msg_head];
		msg_head = (msg_head + 1) % MSG_QUEUE_SIZE;
	} else {
		msg_current.type = -1;
		msg_current.parts_no = 0;
	}
}

static void PartsEngine_ReleaseMessage(void)
{
	if (msg_head != msg_tail) {
		msg_head = (msg_head + 1) % MSG_QUEUE_SIZE;
	}
	msg_current.type = -1;
	msg_current.parts_no = 0;
	msg_current.nr_vars = 0;
}

// GetMessageType always peeks the queue head (not msg_current).
// msg_current holds the already-processed message after PopMessage; returning
// its type would cause CPartsMessageManager@Update to re-fire the same handler
// in nested UpdateMessage calls (e.g., inside Motion::Join's WaitForClick loop).
static int PartsEngine_GetMessageType(void)
{
	if (msg_head != msg_tail) {
		return msg_queue[msg_head].type;
	}
	return -1;
}
// After PopMessage, data is in msg_current (head already advanced).
// After GetMessageType peek (no PopMessage yet), data is at queue head.
// So: if msg_current.type >= 0, read from msg_current; else peek queue head.
static int PartsEngine_GetMessagePartsNumber(void)
{
	if (msg_head != msg_tail)
		return msg_queue[msg_head].parts_no;
	return 0;
}
static int PartsEngine_GetMessageDelegateIndex(void)
{
	if (msg_head != msg_tail)
		return msg_queue[msg_head].delegate_index;
	return 0;
}
static int PartsEngine_GetMessageUniqueID(void)
{
	if (msg_head != msg_tail)
		return msg_queue[msg_head].unique_id;
	return 0;
}


static bool PartsEngine_SeekMessage(int target_parts_no)
{
	// Advance through the queue until a message for the target parts is found.
	while (msg_head != msg_tail) {
		if (msg_queue[msg_head].parts_no == target_parts_no) {
			msg_current = msg_queue[msg_head];
			msg_head = (msg_head + 1) % MSG_QUEUE_SIZE;
			return true;
		}
		msg_head = (msg_head + 1) % MSG_QUEUE_SIZE;
	}
	msg_current.type = -1;
	return false;
}

static int PartsEngine_GetMessageVariableInt(int idx)
{
	int result;
	// After PopMessage, msg_current holds the popped message.
	// Before PopMessage, peek at queue head.
	if (msg_current.type >= 0) {
		if (idx < 0 || idx >= msg_current.nr_vars) result = 0;
		else result = msg_current.vars[idx];
	} else if (msg_head != msg_tail) {
		if (idx < 0 || idx >= msg_queue[msg_head].nr_vars) result = 0;
		else result = msg_queue[msg_head].vars[idx];
	} else {
		result = 0;
	}
	return result;
}
static float PartsEngine_GetMessageVariableFloat(int idx)
{
	if (msg_current.type >= 0) {
		if (idx < 0 || idx >= msg_current.nr_vars) return 0.0f;
		union { int i; float f; } u = { .i = msg_current.vars[idx] };
		return u.f;
	}
	if (msg_head != msg_tail) {
		if (idx < 0 || idx >= msg_queue[msg_head].nr_vars) return 0.0f;
		union { int i; float f; } u = { .i = msg_queue[msg_head].vars[idx] };
		return u.f;
	}
	return 0.0f;
}
static bool PartsEngine_GetMessageVariableBool(int idx)
{
	if (msg_current.type >= 0) {
		if (idx < 0 || idx >= msg_current.nr_vars) return false;
		return msg_current.vars[idx] != 0;
	}
	if (msg_head != msg_tail) {
		if (idx < 0 || idx >= msg_queue[msg_head].nr_vars) return false;
		return msg_queue[msg_head].vars[idx] != 0;
	}
	return false;
}
static void PartsEngine_GetMessageVariableString(possibly_unused int idx, possibly_unused struct string **out)
{
	// String variables not stored in message queue
}

// v14: Save/SaveWithoutHideParts/Load — AIN_WRAP arg[0] is heap slot index.
// Wrap the original PE_Save/PE_Load which take struct page **.
static bool PartsEngine_Save_wrap(int buf_slot)
{
	struct page *buf = wrap_get_page(buf_slot, 0);
	bool ok = PE_Save(&buf);
	if (ok)
		wrap_set_slot(buf_slot, 0, heap_alloc_page(buf));
	return ok;
}

static bool PartsEngine_SaveWithoutHideParts_wrap(int buf_slot)
{
	struct page *buf = wrap_get_page(buf_slot, 0);
	bool ok = PE_SaveWithoutHideParts(&buf);
	if (ok)
		wrap_set_slot(buf_slot, 0, heap_alloc_page(buf));
	return ok;
}

static bool PartsEngine_Load_wrap(int buf_slot)
{
	struct page *buf = wrap_get_page(buf_slot, 0);
	bool ok = PE_Load(&buf);
	return ok;
}

/* Parts3DLayer bridge — connects PartsEngine to SealEngine */
extern int seal_engine_create_plugin_for_parts(int parts_no);
extern int seal_engine_get_plugin_for_parts(int parts_no);
extern bool seal_engine_release_plugin_for_parts(int parts_no);

static int PartsEngine_Parts_CreateParts3DLayerPluginID(int parts_no, int state)
{
	return seal_engine_create_plugin_for_parts(parts_no);
}

static int PartsEngine_Parts_GetParts3DLayerPluginID(int parts_no, int state)
{
	return seal_engine_get_plugin_for_parts(parts_no);
}

static bool PartsEngine_Parts_ReleaseParts3DLayerPluginID(int parts_no, int state)
{
	return seal_engine_release_plugin_for_parts(parts_no);
}

/* PE_GetAddColor / PE_GetMultiplyColor implemented in parts.c */

/* --- Sound number getter/setter --- */
static void PE_SetSoundNumber(int parts_no, int sound_no)
{
	struct parts *p = parts_get(parts_no);
	p->on_cursor_sound = sound_no;
}

static int PE_GetSoundNumber(int parts_no)
{
	return parts_get(parts_no)->on_cursor_sound;
}

static int PE_GetClickMissSoundNumber(void)
{
	return -1;
}

/* --- Focus parts --- */
static int focus_parts_number = 0;

static int PE_GetFocusPartsNumber(void)
{
	return focus_parts_number;
}

static void PE_SetFocusPartsNumber(int parts_no)
{
	focus_parts_number = parts_no;
}

/* --- GUI Controller stack (stub) --- */
static void PE_PushGUIController(void) { }
static void PE_PopGUIController(void) { }
static void PE_UpdateGUIController(void) { }

/* --- Construction Process filters --- */
static void PE_AddFillGradationHorizonToPartsConstructionProcess(int parts_no, int x, int y, int w, int h, int r1, int g1, int b1, int r2, int g2, int b2)
{
	PE_AddFillToPartsConstructionProcess(parts_no, x, y, w, h, r1, g1, b1, 0);
}

/* PE_AddGrayFilterToPartsConstructionProcess already in parts.h */

static void PE_AddAddFilterToPartsConstructionProcess_stub(int parts_no, int x, int y, int w, int h, int r, int g, int b)
{
	/* No-op: add filter not supported yet */
}

static void PE_AddMulFilterToPartsConstructionProcess_stub(int parts_no, int x, int y, int w, int h, int r, int g, int b)
{
	/* No-op: multiply filter not supported yet */
}

/* --- Message Window --- */
static void PE_SetMessageWindowActive(int parts_no, bool active)
{
	struct parts *parts = parts_try_get(parts_no);
	if (!parts)
		return;
	parts->message_window = true;
	PE_SetShow(parts_no, active);
}

static void PE_SetMessageWindowText(int parts_no, struct string *text,
	int msg_num, struct string *func_name, int ver, int step)
{
	// Set text on DEFAULT state (state=1 in 1-based PE_SetText)
	PE_SetText(parts_no, text, 1);
}

static void PE_FixMessageWindowText(int parts_no)
{
	// Text is already rendered by SetMessageWindowText; nothing to finalize.
}

static bool PE_IsFixedMessageWindowText(int parts_no)
{
	// Text is rendered synchronously in SetMessageWindowText, so always "fixed".
	return true;
}

static struct string *PE_GetMessageWindowFlatName(int parts_no)
{
	return string_ref(&EMPTY_STRING);
}

static struct string *PE_GetMessageWindowText(int parts_no)
{
	return string_ref(&EMPTY_STRING);
}

static void PE_SetMessageWindowTextOriginPosMode(int parts_no, int mode)
{
	// Text origin position mode (e.g. left/center). Stub for now.
}

/*
 * Parts_SetPartsCGThread: "async" CG load. We have no real thread loader,
 * so load the CG synchronously and return true so that the ain-side Observer
 * (which polls Parts_IsThreadLoading == false) fires its callback immediately.
 */
static bool PartsEngine_Parts_SetPartsCGThread(int number, struct string *cgname, int state)
{
	PE_SetPartsCG(number, cgname, 0, state);
	return true;
}

static void PE_SetMessageWindowCGName(int parts_no, struct string *name)
{
	// Load CG as background for message window (state 1 = DEFAULT, 1-based)
	PE_SetPartsCG(parts_no, name, 0, 1);
}

static struct string *PE_GetMessageWindowCGName(int parts_no)
{
	struct parts *parts = parts_try_get(parts_no);
	if (!parts)
		return string_ref(&EMPTY_STRING);
	struct parts_cg *cg = parts_get_cg(parts, PARTS_STATE_DEFAULT);
	if (cg && cg->name)
		return string_ref(cg->name);
	return string_ref(&EMPTY_STRING);
}

static void PE_SetMessageWindowTextArea(int parts_no, int x, int y, int w, int h)
{
	// Set text surface area on DEFAULT state (1-based)
	PE_SetPartsTextSurfaceArea(parts_no, x, y, w, h, 1);
}

static void PE_SetMessageWindowTextFont(int parts_no, int type, int size,
	int r, int g, int b, float bold_weight,
	int edge_r, int edge_g, int edge_b, float edge_weight)
{
	// Set font on DEFAULT state (1-based)
	PE_SetFont(parts_no, type, size, r, g, b, bold_weight,
		edge_r, edge_g, edge_b, edge_weight, 1);
}

static void PE_SetMessageWindowTextSpace(int parts_no, int letter_space, int line_space)
{
	// Set line spacing on DEFAULT state (1-based)
	PE_SetTextLineSpace(parts_no, line_space, 1);
}

static void PE_SetKeyWaitShow(int parts_no, bool show)
{
	// Show/hide key-wait indicator. Stub for now.
	PE_SetShow(parts_no, show);
}

static void PE_SaveBackScene(int data)
{
	// Save current scene for back-log display. Stub for now.
}

static void PE_SetButtonEnable(int parts_no, bool enable)
{
	// Enable/disable button input. Stub for now.
}

/* --- Text-related stubs --- */
static void PE_DeletePartsTopTextLine(int parts_no) { }
static void PE_SetPartsTextHighlight(int parts_no, int start, int end) { }
static void PE_AddPartsTextHighlight(int parts_no, int start, int end) { }
static void PE_ClearPartsTextHighlight(int parts_no) { }
static void PE_SetPartsTextCountReturn(int parts_no, bool count) { }
static bool PE_GetPartsTextCountReturn(int parts_no) { return false; }

/* --- Numeral font stub --- */
static bool PE_SetNumeralFont(int parts_no, struct string *font_name, int size, int weight, int r, int g, int b, int er, int eg, int eb, float edge_weight, int state)
{
	return true;
}

/* --- Detection surface area stubs --- */
static void PE_SetPartsRectangleDetectionSurfaceArea(int parts_no, int x, int y, int w, int h)
{
	PE_SetPartsCGSurfaceArea(parts_no, x, y, w, h, 0);
}

static void PE_SetPartsCGDetectionSurfaceArea(int parts_no, int x, int y, int w, int h, int state)
{
	PE_SetPartsCGSurfaceArea(parts_no, x, y, w, h, state);
}

/* --- Timer/speedup stubs --- */
static void PE_SetResetTimerByChangeInputStatus(bool reset) { }
static bool PE_GetResetTimerByChangeInputStatus(void) { return false; }
static float PE_GetPartsSpeedupRateByMessageSkip(void) { return 1.0f; }

/* --- Motion CG stub --- */
static bool PE_AddMotionCG(int parts_no, struct string *cg_name, int begin_cg, int nr_cg, int begin_time, int end_time, int state)
{
	return true;
}

/* PE_ExistsFlashFile already declared in parts.h — use directly via HLL_EXPORT */

/* SetPartsFlashSurfaceArea stub */
static void PE_SetPartsFlashSurfaceArea_stub(int parts_no, int x, int y, int w, int h) { }

/* Old-style Parts_AddDrawCutCG/AddCopyCutCG — these use cg_no (int) instead of cg_name (string) */
static void PE_AddDrawCutCGToPartsConstructionProcess_old(int parts_no, int cg_no, int dx, int dy, int sx, int sy, int w, int h, int interp)
{
	/* Old API: convert cg_no to string for new API, or just use direct fill */
}

static void PE_AddCopyCutCGToPartsConstructionProcess_old(int parts_no, int cg_no, int dx, int dy, int sx, int sy, int w, int h, int interp)
{
	/* Old API: convert cg_no to string for new API, or just use direct fill */
}

static void PartsEngine_SetMotionData(int parts_no, struct string *motion_name, int time, int fps)
{
	struct parts *parts = parts_try_get(parts_no);
	if (!parts) return;
}
static int PartsEngine_GetMotionBeginFrame(struct string *motion_name) { return 0; }
static int PartsEngine_GetMotionEndFrame(struct string *motion_name) { return 0; }

// Parts movie implementation (APEG audio playback via movie.h)
#define PARTS_MOVIE_MAX 16
static struct { int number; struct movie_context *mc; } parts_movies[PARTS_MOVIE_MAX];

static struct movie_context **parts_movie_get(int number)
{
	for (int i = 0; i < PARTS_MOVIE_MAX; i++)
		if (parts_movies[i].number == number)
			return &parts_movies[i].mc;
	return NULL;
}

static bool PE_stub_CreatePartsMovie(int number, struct string *filename,
	possibly_unused int soundid, possibly_unused int soundgroup,
	int red, int green, int blue, int state)
{
	// Free any existing context for this number.
	struct movie_context **slot = parts_movie_get(number);
	if (slot && *slot) {
		movie_free(*slot);
		*slot = NULL;
	}
	// Find a free slot.
	if (!slot) {
		for (int i = 0; i < PARTS_MOVIE_MAX; i++) {
			if (!parts_movies[i].mc && parts_movies[i].number == 0) {
				parts_movies[i].number = number;
				slot = &parts_movies[i].mc;
				break;
			}
		}
	}
	if (!slot)
		return false;
	struct movie_context *mc = movie_load(filename->text);
	if (!mc)
		return false;
	*slot = mc;

	// Fill the parts texture with the background color (shows when video not decoded).
	struct parts *p = parts_try_get(number);
	if (p && parts_state_valid(state - 1)) {
		struct parts_common *common = &p->states[state - 1].common;
		int tw = common->w, th = common->h;
		// If parts has no size yet, use the movie's native dimensions.
		if (tw <= 0 || th <= 0)
			movie_get_video_size(mc, &tw, &th);
		if (tw > 0 && th > 0) {
			// Colors are in YCbCr; just use black (0,0,0) for simplicity.
			SDL_Color bg = { 0, 0, 0, 255 };
			(void)red; (void)green; (void)blue;
			gfx_init_texture_rgba(&common->texture, tw, th, bg);
			if (common->w <= 0) {
				common->w = tw;
				common->h = th;
			}
			parts_dirty(p);
		}
	}
	return true;
}

static bool PE_stub_ReleasePartsMovie(int number, possibly_unused int state)
{
	struct movie_context **slot = parts_movie_get(number);
	if (slot && *slot) {
		movie_free(*slot);
		*slot = NULL;
	}
	return true;
}

static bool PE_stub_PlayPartsMovie(int number, int msec, possibly_unused int state)
{
	struct movie_context **slot = parts_movie_get(number);
	if (!slot || !*slot)
		return false;
	(void)msec;
	return movie_play(*slot);
}

static void PE_stub_SetMovieTime(possibly_unused int number, possibly_unused int msec, possibly_unused int state) { }

static bool PE_stub_IsEndPartsMovie(int number, possibly_unused int state)
{
	struct movie_context **slot = parts_movie_get(number);
	if (!slot || !*slot)
		return true;
	return movie_is_end(*slot);
}

static int PE_stub_GetPartsMovieEndTime(possibly_unused int number, possibly_unused int state) { return 0; }

static int PE_stub_GetPartsMovieCurrentTime(int number, possibly_unused int state)
{
	struct movie_context **slot = parts_movie_get(number);
	if (!slot || !*slot)
		return 0;
	return movie_get_position(*slot);
}

#include "pe_v14_stubs.h"

static void PartsEngine_PreLink(void);
static void PartsEngine_PostLink(void);

HLL_LIBRARY(PartsEngine,
	    HLL_EXPORT(_PreLink, PartsEngine_PreLink),
	    HLL_EXPORT(_PostLink, PartsEngine_PostLink),
	    // for versions without PartsEngine.Init
	    HLL_EXPORT(_ModuleInit, PartsEngine_ModuleInit),
	    HLL_EXPORT(_ModuleFini, PartsEngine_ModuleFini),
	    // Oyako Rankan
	    HLL_EXPORT(Init, PE_Init),
	    HLL_EXPORT(Update, PartsEngine_Update),
	    HLL_EXPORT(UpdateGUIController, PE_UpdateGUIController),
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
	    HLL_EXPORT(DeletePartsTopTextLine, PE_DeletePartsTopTextLine),
	    HLL_EXPORT(SetPartsTextSurfaceArea, PE_SetPartsTextSurfaceArea),
	    HLL_EXPORT(SetPartsTextHighlight, PE_SetPartsTextHighlight),
	    HLL_EXPORT(AddPartsTextHighlight, PE_AddPartsTextHighlight),
	    HLL_EXPORT(ClearPartsTextHighlight, PE_ClearPartsTextHighlight),
	    HLL_EXPORT(SetPartsTextCountReturn, PE_SetPartsTextCountReturn),
	    HLL_EXPORT(GetPartsTextCountReturn, PE_GetPartsTextCountReturn),
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
	    HLL_EXPORT(SetNumeralFont, PE_SetNumeralFont),
	    HLL_EXPORT(SetNumeralNumber, PE_SetNumeralNumber),
	    HLL_EXPORT(SetNumeralShowComma, PE_SetNumeralShowComma),
	    HLL_EXPORT(SetNumeralSpace, PE_SetNumeralSpace),
	    HLL_EXPORT(SetNumeralLength, PE_SetNumeralLength),
	    HLL_EXPORT(SetNumeralSurfaceArea, PE_SetNumeralSurfaceArea),
	    HLL_EXPORT(SetPartsRectangleDetectionSize, PE_SetPartsRectangleDetectionSize),
	    HLL_EXPORT(SetPartsRectangleDetectionSurfaceArea, PE_SetPartsRectangleDetectionSurfaceArea),
	    HLL_EXPORT(SetPartsCGDetectionSize, PE_SetPartsCGDetectionSize),
	    HLL_EXPORT(SetPartsCGDetectionSurfaceArea, PE_SetPartsCGDetectionSurfaceArea),
	    HLL_EXPORT(SetPartsFlash, PE_SetPartsFlash),
	    HLL_EXPORT(IsPartsFlashEnd, PE_IsPartsFlashEnd),
	    HLL_EXPORT(GetPartsFlashCurrentFrameNumber, PE_GetPartsFlashCurrentFrameNumber),
	    HLL_EXPORT(BackPartsFlashBeginFrame, PE_BackPartsFlashBeginFrame),
	    HLL_EXPORT(StepPartsFlashFinalFrame, PE_StepPartsFlashFinalFrame),
	    HLL_EXPORT(SetPartsFlashSurfaceArea, PE_SetPartsFlashSurfaceArea_stub),
	    HLL_EXPORT(SetPartsFlashAndStop, PE_SetPartsFlashAndStop),
	    HLL_EXPORT(StopPartsFlash, PE_StopPartsFlash),
	    HLL_EXPORT(StartPartsFlash, PE_StartPartsFlash),
	    HLL_EXPORT(GoFramePartsFlash, PE_GoFramePartsFlash),
	    HLL_EXPORT(GetPartsFlashEndFrame, PE_GetPartsFlashEndFrame),
	    HLL_EXPORT(ExistsFlashFile, PE_ExistsFlashFile),
	    HLL_EXPORT(ClearPartsConstructionProcess, PE_ClearPartsConstructionProcess),
	    HLL_EXPORT(AddCreateToPartsConstructionProcess, PE_AddCreateToPartsConstructionProcess),
	    HLL_EXPORT(AddCreatePixelOnlyToPartsConstructionProcess, PE_AddCreatePixelOnlyToPartsConstructionProcess),
	    HLL_EXPORT(AddCreateCGToProcess, PE_AddCreateCGToProcess),
	    HLL_EXPORT(AddFillToPartsConstructionProcess, PE_AddFillToPartsConstructionProcess),
	    HLL_EXPORT(AddFillAlphaColorToPartsConstructionProcess, PE_AddFillAlphaColorToPartsConstructionProcess),
	    HLL_EXPORT(AddFillAMapToPartsConstructionProcess, PE_AddFillAMapToPartsConstructionProcess),
	    HLL_EXPORT(AddFillWithAlphaToPartsConstructionProcess, PE_AddFillWithAlphaToPartsConstructionProcess),
	    HLL_EXPORT(AddFillGradationHorizonToPartsConstructionProcess, PE_AddFillGradationHorizonToPartsConstructionProcess),
	    HLL_EXPORT(AddDrawRectToPartsConstructionProcess, PE_AddDrawRectToPartsConstructionProcess),
	    HLL_EXPORT(AddDrawCutCGToPartsConstructionProcess, PartsEngine_AddDrawCutCGToPartsConstructionProcess_old),
	    HLL_EXPORT(AddCopyCutCGToPartsConstructionProcess, PartsEngine_AddCopyCutCGToPartsConstructionProcess_old),
	    HLL_EXPORT(AddGrayFilterToPartsConstructionProcess, PE_AddGrayFilterToPartsConstructionProcess),
	    HLL_EXPORT(AddAddFilterToPartsConstructionProcess, PE_AddAddFilterToPartsConstructionProcess_stub),
	    HLL_EXPORT(AddMulFilterToPartsConstructionProcess, PE_AddMulFilterToPartsConstructionProcess_stub),
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
	    HLL_EXPORT(SetEnableInputProcess, PartsEngine_SetEnableInputProcess),
	    HLL_EXPORT(SetSpeedupRateByMessageSkip, PE_SetSpeedupRateByMessageSkip),
	    HLL_EXPORT(SetResetTimerByChangeInputStatus, PE_SetResetTimerByChangeInputStatus),
	    HLL_EXPORT(GetPartsX, PE_GetPartsX),
	    HLL_EXPORT(GetPartsY, PE_GetPartsY),
	    HLL_EXPORT(GetPartsZ, PE_GetPartsZ),
	    HLL_EXPORT(GetPartsShow, PE_GetPartsShow),
	    HLL_EXPORT(GetPartsAlpha, PE_GetPartsAlpha),
	    HLL_EXPORT(GetAddColor, PE_GetAddColor),
	    HLL_EXPORT(GetMultiplyColor, PE_GetMultiplyColor),
	    HLL_EXPORT(GetPartsClickable, PE_GetPartsClickable),
	    HLL_EXPORT(GetPartsSpeedupRateByMessageSkip, PE_GetPartsSpeedupRateByMessageSkip),
	    HLL_EXPORT(GetResetTimerByChangeInputStatus, PE_GetResetTimerByChangeInputStatus),
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
	    HLL_EXPORT(AddMotionCG, PE_AddMotionCG),
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
	    HLL_EXPORT(SetSoundNumber, PE_SetSoundNumber),
	    HLL_EXPORT(GetSoundNumber, PE_GetSoundNumber),
	    HLL_EXPORT(SetClickMissSoundNumber, PE_SetClickMissSoundNumber),
	    HLL_EXPORT(GetClickMissSoundNumber, PE_GetClickMissSoundNumber),
	    HLL_EXPORT(BeginMotion, PE_BeginMotion),
	    HLL_EXPORT(EndMotion, PE_EndMotion),
	    HLL_EXPORT(IsMotion, PE_IsMotion),
	    HLL_EXPORT(SeekEndMotion, PE_SeekEndMotion),
	    HLL_EXPORT(UpdateMotionTime, PE_UpdateMotionTime),
	    HLL_EXPORT(SetMotionData, PartsEngine_SetMotionData),
	    HLL_EXPORT(GetMotionBeginFrame, PartsEngine_GetMotionBeginFrame),
	    HLL_EXPORT(GetMotionEndFrame, PartsEngine_GetMotionEndFrame),
	    HLL_EXPORT(BeginInput, PE_BeginInput),
	    HLL_EXPORT(EndInput, PE_EndInput),
	    HLL_EXPORT(GetClickPartsNumber, PE_GetClickPartsNumber),
	    HLL_EXPORT(GetFocusPartsNumber, PE_GetFocusPartsNumber),
	    HLL_EXPORT(SetFocusPartsNumber, PE_SetFocusPartsNumber),
	    HLL_EXPORT(PushGUIController, PE_PushGUIController),
	    HLL_EXPORT(PopGUIController, PE_PopGUIController),
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
	    HLL_EXPORT(SeekMessage, PartsEngine_SeekMessage),
	    HLL_EXPORT(GetMessagePartsNumber, PartsEngine_GetMessagePartsNumber),
	    HLL_EXPORT(GetMessageDelegateIndex, PartsEngine_GetMessageDelegateIndex),
	    HLL_EXPORT(GetMessageUniqueID, PartsEngine_GetMessageUniqueID),
	    HLL_EXPORT(GetMessageType, PartsEngine_GetMessageType),
	    HLL_EXPORT(GetMessageVariableInt, PartsEngine_GetMessageVariableInt),
	    HLL_EXPORT(GetMessageVariableFloat, PartsEngine_GetMessageVariableFloat),
	    HLL_EXPORT(GetMessageVariableBool, PartsEngine_GetMessageVariableBool),
	    HLL_EXPORT(GetMessageVariableString, PartsEngine_GetMessageVariableString),
	    /* v14 Parts_ prefixed aliases (same implementations) */
	    HLL_EXPORT(Parts_SetPartsCG, PE_SetPartsCG),
	    HLL_EXPORT(Parts_GetPartsCGName, PE_GetPartsCGName),
	    HLL_EXPORT(Parts_GetPartsCGDeform, PE_GetPartsCGDeform),
	    HLL_EXPORT(Parts_SetPartsCGSurfaceArea, PE_SetPartsCGSurfaceArea),
	    HLL_EXPORT(Parts_SetLoopCG, PE_SetLoopCG),
	    HLL_EXPORT(Parts_SetLoopCGSurfaceArea, PE_SetLoopCGSurfaceArea),
	    HLL_EXPORT(Parts_SetText, PE_SetText),
	    HLL_EXPORT(Parts_AddPartsText, PE_AddPartsText),
	    HLL_EXPORT(Parts_SetPartsTextSurfaceArea, PE_SetPartsTextSurfaceArea),
	    HLL_EXPORT(Parts_SetFont, PE_SetFont),
	    HLL_EXPORT(Parts_SetPartsFontType, PE_SetPartsFontType),
	    HLL_EXPORT(Parts_SetPartsFontSize, PE_SetPartsFontSize),
	    HLL_EXPORT(Parts_SetPartsFontColor, PE_SetPartsFontColor),
	    HLL_EXPORT(Parts_SetPartsFontBoldWeight, PE_SetPartsFontBoldWeight),
	    HLL_EXPORT(Parts_SetPartsFontEdgeColor, PE_SetPartsFontEdgeColor),
	    HLL_EXPORT(Parts_SetPartsFontEdgeWeight, PE_SetPartsFontEdgeWeight),
	    HLL_EXPORT(Parts_SetTextCharSpace, PE_SetTextCharSpace),
	    HLL_EXPORT(Parts_SetTextLineSpace, PE_SetTextLineSpace),
	    HLL_EXPORT(Parts_SetParentPartsNumber, PE_SetParentPartsNumber),
	    HLL_EXPORT(Parts_SetPos, PE_SetPos),
	    HLL_EXPORT(Parts_SetZ, PE_SetZ),
	    HLL_EXPORT(Parts_SetShow, PE_SetShow),
	    HLL_EXPORT(Parts_SetAlpha, PE_SetAlpha),
	    HLL_EXPORT(Parts_SetClickable, PE_SetClickable),
	    HLL_EXPORT(Parts_SetPassCursor, PE_SetPassCursor),
	    HLL_EXPORT(Parts_SetWheelable, PartsEngine_Parts_SetWheelable),
	    HLL_EXPORT(Parts_SetPartsDrawFilter, PE_SetPartsDrawFilter),
	    HLL_EXPORT(Parts_SetAddColor, PE_SetAddColor),
	    HLL_EXPORT(Parts_SetMultiplyColor, PE_SetMultiplyColor),
	    HLL_EXPORT(Parts_GetPartsX, PE_GetPartsX),
	    HLL_EXPORT(Parts_GetPartsY, PE_GetPartsY),
	    HLL_EXPORT(Parts_GetPartsZ, PE_GetPartsZ),
	    HLL_EXPORT(Parts_GetPartsShow, PE_GetPartsShow),
	    HLL_EXPORT(Parts_GetPartsAlpha, PE_GetPartsAlpha),
	    HLL_EXPORT(Parts_GetPartsClickable, PE_GetPartsClickable),
	    HLL_EXPORT(Parts_GetPartsUpperLeftPosX, PE_GetPartsUpperLeftPosX),
	    HLL_EXPORT(Parts_GetPartsUpperLeftPosY, PE_GetPartsUpperLeftPosY),
	    HLL_EXPORT(Parts_GetPartsWidth, PE_GetPartsWidth),
	    HLL_EXPORT(Parts_GetPartsHeight, PE_GetPartsHeight),
	    HLL_EXPORT(Parts_GetPartsSize, PE_GetPartsSize),
	    HLL_EXPORT(Parts_GetParentPartsNumber, PE_GetParentPartsNumber),
	    HLL_EXPORT(Parts_SetInputState, PE_SetInputState),
	    HLL_EXPORT(Parts_GetInputState, PE_GetInputState),
	    HLL_EXPORT(Parts_SetPartsOriginPosMode, PE_SetPartsOriginPosMode),
	    HLL_EXPORT(Parts_GetPartsOriginPosMode, PE_GetPartsOriginPosMode),
	    HLL_EXPORT(Parts_SetPartsGroupNumber, PE_SetPartsGroupNumber),
	    HLL_EXPORT(Parts_SetPartsGroupDecideOnCursor, PE_SetPartsGroupDecideOnCursor),
	    HLL_EXPORT(Parts_SetPartsGroupDecideClick, PE_SetPartsGroupDecideClick),
	    HLL_EXPORT(Parts_SetOnCursorShowLinkPartsNumber, PE_SetOnCursorShowLinkPartsNumber),
	    HLL_EXPORT(Parts_SetPartsMessageWindowShowLink, PE_SetPartsMessageWindowShowLink),
	    HLL_EXPORT(Parts_GetPartsMessageWindowShowLink, PE_GetPartsMessageWindowShowLink),
	    HLL_EXPORT(Parts_SetHGaugeCG, PE_SetHGaugeCG),
	    HLL_EXPORT(Parts_SetHGaugeRate, PE_SetHGaugeRate_int),
	    HLL_EXPORT(Parts_SetVGaugeCG, PE_SetVGaugeCG),
	    HLL_EXPORT(Parts_SetVGaugeRate, PE_SetVGaugeRate_int),
	    HLL_EXPORT(Parts_SetHGaugeSurfaceArea, PE_SetHGaugeSurfaceArea),
	    HLL_EXPORT(Parts_SetVGaugeSurfaceArea, PE_SetVGaugeSurfaceArea),
	    HLL_EXPORT(Parts_SetNumeralCG, PE_SetNumeralCG),
	    HLL_EXPORT(Parts_SetNumeralLinkedCGNumberWidthWidthList, PE_SetNumeralLinkedCGNumberWidthWidthList),

	    HLL_EXPORT(Parts_SetNumeralNumber, PE_SetNumeralNumber),
	    HLL_EXPORT(Parts_SetNumeralShowComma, PE_SetNumeralShowComma),
	    HLL_EXPORT(Parts_SetNumeralSpace, PE_SetNumeralSpace),
	    HLL_EXPORT(Parts_SetNumeralLength, PE_SetNumeralLength),
	    HLL_EXPORT(Parts_SetNumeralSurfaceArea, PE_SetNumeralSurfaceArea),
	    HLL_EXPORT(Parts_SetPartsRectangleDetectionSize, PE_SetPartsRectangleDetectionSize),
	    HLL_EXPORT(Parts_SetPartsCGDetectionSize, PE_SetPartsCGDetectionSize),
	    HLL_EXPORT(Parts_SetPartsFlash, PE_SetPartsFlash),
	    HLL_EXPORT(Parts_IsPartsFlashEnd, PE_IsPartsFlashEnd),
	    HLL_EXPORT(Parts_GetPartsFlashCurrentFrameNumber, PE_GetPartsFlashCurrentFrameNumber),
	    HLL_EXPORT(Parts_BackPartsFlashBeginFrame, PE_BackPartsFlashBeginFrame),
	    HLL_EXPORT(Parts_StepPartsFlashFinalFrame, PE_StepPartsFlashFinalFrame),
	    HLL_EXPORT(Parts_SetPartsFlashAndStop, PE_SetPartsFlashAndStop),
	    HLL_EXPORT(Parts_StopPartsFlash, PE_StopPartsFlash),
	    HLL_EXPORT(Parts_StartPartsFlash, PE_StartPartsFlash),
	    HLL_EXPORT(Parts_GoFramePartsFlash, PE_GoFramePartsFlash),
	    HLL_EXPORT(Parts_GetPartsFlashEndFrame, PE_GetPartsFlashEndFrame),
	    HLL_EXPORT(Parts_ClearPartsConstructionProcess, PE_ClearPartsConstructionProcess),
	    HLL_EXPORT(Parts_BuildPartsConstructionProcess, PE_BuildPartsConstructionProcess),
	    HLL_EXPORT(Parts_AddCreateToPartsConstructionProcess, PE_AddCreateToPartsConstructionProcess),
	    HLL_EXPORT(Parts_AddCreatePixelOnlyToPartsConstructionProcess, PE_AddCreatePixelOnlyToPartsConstructionProcess),
	    HLL_EXPORT(Parts_AddCreateCGToProcess, PE_AddCreateCGToProcess),
	    HLL_EXPORT(Parts_AddFillToPartsConstructionProcess, PE_AddFillToPartsConstructionProcess),
	    HLL_EXPORT(Parts_AddFillAlphaColorToPartsConstructionProcess, PE_AddFillAlphaColorToPartsConstructionProcess),
	    HLL_EXPORT(Parts_AddFillAMapToPartsConstructionProcess, PE_AddFillAMapToPartsConstructionProcess),
	    HLL_EXPORT(Parts_AddDrawRectToPartsConstructionProcess, PE_AddDrawRectToPartsConstructionProcess),
	    HLL_EXPORT(Parts_AddDrawTextToPartsConstructionProcess, PE_AddDrawTextToPartsConstructionProcess),
	    HLL_EXPORT(Parts_AddCopyTextToPartsConstructionProcess, PE_AddCopyTextToPartsConstructionProcess),
	    HLL_EXPORT(Parts_SetPartsConstructionSurfaceArea, PE_SetPartsConstructionSurfaceArea),
	    HLL_EXPORT(Parts_ReleaseParts, PE_ReleaseParts),
	    HLL_EXPORT(Parts_ReleaseAllParts, PE_ReleaseAllParts),
	    HLL_EXPORT(Parts_ReleaseAllPartsWithoutSystem, PE_ReleaseAllPartsWithoutSystem),
	    HLL_EXPORT(Parts_SetPartsMagX, PE_SetPartsMagX),
	    HLL_EXPORT(Parts_SetPartsMagY, PE_SetPartsMagY),
	    HLL_EXPORT(Parts_SetPartsRotateX, PE_SetPartsRotateX),
	    HLL_EXPORT(Parts_SetPartsRotateY, PE_SetPartsRotateY),
	    HLL_EXPORT(Parts_SetPartsRotateZ, PE_SetPartsRotateZ),
	    HLL_EXPORT(Parts_SetPartsAlphaClipperPartsNumber, PE_SetPartsAlphaClipperPartsNumber),
	    HLL_EXPORT(Parts_SetPartsPixelDecide, PE_SetPartsPixelDecide),
	    HLL_EXPORT(Parts_IsCursorIn, PE_IsCursorIn),
	    HLL_EXPORT(Parts_SetThumbnailReductionSize, PE_SetThumbnailReductionSize),
	    HLL_EXPORT(Parts_SetThumbnailMode, PE_SetThumbnailMode),
	    HLL_EXPORT(Parts_AddMotionPos, PE_AddMotionPos_curve),
	    HLL_EXPORT(Parts_AddMotionAlpha, PE_AddMotionAlpha_curve),
	    HLL_EXPORT(Parts_AddMotionHGaugeRate, PE_AddMotionHGaugeRate_curve),
	    HLL_EXPORT(Parts_AddMotionVGaugeRate, PE_AddMotionVGaugeRate_curve),
	    HLL_EXPORT(Parts_AddMotionNumeralNumber, PE_AddMotionNumeralNumber_curve),
	    HLL_EXPORT(Parts_AddMotionMagX, PE_AddMotionMagX_curve),
	    HLL_EXPORT(Parts_AddMotionMagY, PE_AddMotionMagY_curve),
	    HLL_EXPORT(Parts_AddMotionRotateX, PE_AddMotionRotateX_curve),
	    HLL_EXPORT(Parts_AddMotionRotateY, PE_AddMotionRotateY_curve),
	    HLL_EXPORT(Parts_AddMotionRotateZ, PE_AddMotionRotateZ_curve),
	    HLL_EXPORT(Parts_AddMotionVibrationSize, PE_AddMotionVibrationSize),
	    HLL_EXPORT(Parts_AddWholeMotionVibrationSize, PE_AddWholeMotionVibrationSize),
	    HLL_EXPORT(Parts_AddMotionSound, PE_AddMotionSound),
	    HLL_EXPORT(Parts_SetClickMissSoundNumber, PE_SetClickMissSoundNumber),
	    HLL_EXPORT(Parts_BeginMotion, PE_BeginMotion),
	    HLL_EXPORT(Parts_EndMotion, PE_EndMotion),
	    HLL_EXPORT(Parts_IsMotion, PE_IsMotion),
	    HLL_EXPORT(Parts_SeekEndMotion, PE_SeekEndMotion),
	    HLL_EXPORT(Parts_UpdateMotionTime, PE_UpdateMotionTime),
	    HLL_EXPORT(Parts_SetSpeedupRateByMessageSkip, PE_SetSpeedupRateByMessageSkip),
	    HLL_EXPORT(SetEnableInputProcess, PartsEngine_SetEnableInputProcess),
	    /* v14 stubs for new functions */
	    HLL_EXPORT(Parts_SetComment, PartsEngine_Parts_SetComment),
	    HLL_EXPORT(Parts_GetComment, PartsEngine_Parts_GetComment),
	    HLL_EXPORT(Parts_DeletePartsTopTextLine, PE_DeletePartsTopTextLine),
	    HLL_EXPORT(Parts_SetPartsTextHighlight, PE_SetPartsTextHighlight),
	    HLL_EXPORT(Parts_AddPartsTextHighlight, PE_AddPartsTextHighlight),
	    HLL_EXPORT(Parts_ClearPartsTextHighlight, PE_ClearPartsTextHighlight),
	    HLL_EXPORT(Parts_SetPartsTextCountReturn, PE_SetPartsTextCountReturn),
	    HLL_EXPORT(Parts_GetPartsTextCountReturn, PE_GetPartsTextCountReturn),
	    HLL_EXPORT(Parts_SetNumeralFont, PE_SetNumeralFont),
	    HLL_EXPORT(Parts_SetPartsRectangleDetectionSurfaceArea, PE_SetPartsRectangleDetectionSurfaceArea),
	    HLL_EXPORT(Parts_SetPartsCGDetectionSurfaceArea, PE_SetPartsCGDetectionSurfaceArea),
	    HLL_EXPORT(Parts_SetPartsFlashSurfaceArea, PE_SetPartsFlashSurfaceArea_stub),
	    HLL_EXPORT(Parts_ExistsFlashFile, PE_ExistsFlashFile),
	    HLL_EXPORT(Parts_AddFillWithAlphaToPartsConstructionProcess, PE_AddFillWithAlphaToPartsConstructionProcess),
	    HLL_EXPORT(Parts_AddFillGradationHorizonToPartsConstructionProcess, PE_AddFillGradationHorizonToPartsConstructionProcess),
	    HLL_EXPORT(Parts_AddGrayFilterToPartsConstructionProcess, PE_AddGrayFilterToPartsConstructionProcess),
	    HLL_EXPORT(Parts_AddAddFilterToPartsConstructionProcess, PE_AddAddFilterToPartsConstructionProcess_stub),
	    HLL_EXPORT(Parts_AddMulFilterToPartsConstructionProcess, PE_AddMulFilterToPartsConstructionProcess_stub),
	    HLL_EXPORT(Parts_SetResetTimerByChangeInputStatus, PE_SetResetTimerByChangeInputStatus),
	    HLL_EXPORT(Parts_GetPartsSpeedupRateByMessageSkip, PE_GetPartsSpeedupRateByMessageSkip),
	    HLL_EXPORT(Parts_GetResetTimerByChangeInputStatus, PE_GetResetTimerByChangeInputStatus),
	    HLL_EXPORT(Parts_GetAddColor, PE_GetAddColor),
	    HLL_EXPORT(Parts_GetMultiplyColor, PE_GetMultiplyColor),
	    HLL_EXPORT(Parts_AddMotionCG, PE_AddMotionCG),
	    HLL_EXPORT(Parts_SetSoundNumber, PE_SetSoundNumber),
	    HLL_EXPORT(Parts_GetSoundNumber, PE_GetSoundNumber),
	    HLL_EXPORT(Parts_GetClickMissSoundNumber, PE_GetClickMissSoundNumber),
	    HLL_EXPORT(Parts_GetFocusPartsNumber, PE_GetFocusPartsNumber),
	    HLL_EXPORT(Parts_SetFocusPartsNumber, PE_SetFocusPartsNumber),
	    HLL_EXPORT(Parts_PushGUIController, PE_PushGUIController),
	    HLL_EXPORT(Parts_PopGUIController, PE_PopGUIController),
	    HLL_EXPORT(Parts_AddDrawCutCGToPartsConstructionProcess, PE_AddDrawCutCGToPartsConstructionProcess_old),
	    HLL_EXPORT(Parts_AddCopyCutCGToPartsConstructionProcess, PE_AddCopyCutCGToPartsConstructionProcess_old),
	    HLL_EXPORT(AddPartsConstructionProcess, PartsEngine_AddPartsConstructionProcess),
	    HLL_EXPORT(SetPanelSize, PartsEngine_SetPanelSize),
	    HLL_EXPORT(SetPanelColor, PartsEngine_SetPanelColor),
	    // v14 additional aliases
	    HLL_EXPORT(BeginClick, PE_BeginInput),
	    HLL_EXPORT(EndClick, PE_EndInput),
	    HLL_EXPORT(ReleaseMessage, PartsEngine_ReleaseMessage),
	    // Parts3DLayer bridge (PartsEngine ↔ SealEngine)
	    HLL_EXPORT(Parts_CreateParts3DLayerPluginID, PartsEngine_Parts_CreateParts3DLayerPluginID),
	    HLL_EXPORT(Parts_GetParts3DLayerPluginID, PartsEngine_Parts_GetParts3DLayerPluginID),
	    HLL_EXPORT(Parts_ReleaseParts3DLayerPluginID, PartsEngine_Parts_ReleaseParts3DLayerPluginID),
	    // Message window
	    HLL_EXPORT(SetMessageWindowActive, PE_SetMessageWindowActive),
	    HLL_EXPORT(SetMessageWindowText, PE_SetMessageWindowText),
	    HLL_EXPORT(FixMessageWindowText, PE_FixMessageWindowText),
	    HLL_EXPORT(IsFixedMessageWindowText, PE_IsFixedMessageWindowText),
	    HLL_EXPORT(GetMessageWindowFlatName, PE_GetMessageWindowFlatName),
	    HLL_EXPORT(GetMessageWindowText, PE_GetMessageWindowText),
	    HLL_EXPORT(SetMessageWindowTextOriginPosMode, PE_SetMessageWindowTextOriginPosMode),
	    HLL_EXPORT(SetKeyWaitShow, PE_SetKeyWaitShow),
	    HLL_EXPORT(SaveBackScene, PE_SaveBackScene),
	    HLL_EXPORT(SetButtonEnable, PE_SetButtonEnable)
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
	fun = get_fun(libno, "Update");
	if (fun && fun->nr_arguments == 5) {
		static_library_replace(&lib_PartsEngine, "Update",
				PartsEngine_Update_Pascha3PC);
	}
}

static void PartsEngine_PostLink(void)
{
#include "pe_v14_prelink.h"
}
