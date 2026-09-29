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
#include <math.h>
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
static const char SJIS_MULTI_LEVEL[] = "\x83\x7d\x83\x8b\x83\x60\x83\x8c\x83\x78\x83\x8b\x83\x70\x81\x5b\x83\x63"; /* マルチレベルパーツ */
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

/* Mouse pixel decisions use the source layout's encoding. The Chinese layout
 * translates "mouse cursor" but retains the Japanese pixel suffix. */
static const char SJIS_PIXEL_DECIDE[] = "\x83\x7d\x83\x45\x83\x58\x83\x4a\x81\x5b\x83\x5c\x83\x8b\x83\x73\x83\x4e\x83\x5a\x83\x8b\x94\xbb\x92\xe8"; /* マウスカーソルピクセル判定 */
static const char GBK_PIXEL_DECIDE[] = "\xa5\xde\xa5\xa6\xa5\xb9\xa5\xab\xa9\x60\xa5\xbd\xa5\xeb\xa5\xd4\xa5\xaf\xa5\xbb\xa5\xeb\xc5\xd0\xb6\xa8";
static const char GBK_CN_PIXEL_DECIDE[] = "\xca\xf3\x98\xcb\xd6\xb8\xe1\x98\xa5\xd4\xa5\xaf\xa5\xbb\xa5\xeb\xc5\xd0\xb6\xa8"; /* 鼠標指針ピクセル判定 */

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

static void pactex_apply_pixel_decide(struct ex_tree *node, int parts_no)
{
	int enabled = pactex_get_int(node, SJIS_PIXEL_DECIDE, -1);
	if (enabled < 0) enabled = pactex_get_int(node, GBK_PIXEL_DECIDE, -1);
	if (enabled < 0) enabled = pactex_get_int(node, GBK_CN_PIXEL_DECIDE, 0);
	PE_SetPartsPixelDecide(parts_no, enabled != 0);
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

/* Message-window values are direct leaves of their own type-info branch.
 * Keep raw SJIS and the actual CN/GB18030 spellings separate; nested ruby and
 * key-wait settings must not be read as the window's text or CG states. */
enum pactex_message_key {
	PACTEX_MW_AREA,
	PACTEX_MW_ORIGIN,
	PACTEX_MW_FACE,
	PACTEX_MW_SIZE,
	PACTEX_MW_COLOR,
	PACTEX_MW_WEIGHT,
	PACTEX_MW_EDGE,
	PACTEX_MW_EDGE_COLOR,
	PACTEX_MW_CHAR_SPACE,
	PACTEX_MW_LINE_SPACE,
};
static const struct { const char *sjis, *gbk; } pactex_message_keys[] = {
	[PACTEX_MW_AREA] = {"\x83\x65\x83\x4c\x83\x58\x83\x67\x83\x47\x83\x8a\x83\x41", "\xce\xc4\xb1\xbe\xa5\xa8\xa5\xea\xa5\xa2"}, /* テキストエリア / 文本エリア */
	[PACTEX_MW_ORIGIN] = {"\x83\x65\x83\x4c\x83\x58\x83\x67\x88\xca\x92\x75", "\xce\xc4\xb1\xbe\xce\xbb\xd6\xc3"}, /* テキスト位置 / 文本位置 */
	[PACTEX_MW_FACE] = {"\x83\x74\x83\x48\x83\x93\x83\x67\x83\x5e\x83\x43\x83\x76", "\xa5\xd5\xa5\xa9\xa5\xf3\xa5\xc8\xa5\xbf\xa5\xa4\xa5\xd7"}, /* フォントタイプ / フォントタイプ */
	[PACTEX_MW_SIZE] = {"\x83\x74\x83\x48\x83\x93\x83\x67\x83\x54\x83\x43\x83\x59", "\xa5\xd5\xa5\xa9\xa5\xf3\xa5\xc8\xa5\xb5\xa5\xa4\xa5\xba"}, /* フォントサイズ / フォントサイズ */
	[PACTEX_MW_COLOR] = {"\x83\x74\x83\x48\x83\x93\x83\x67\x90\x46", "\xa5\xd5\xa5\xa9\xa5\xf3\xa5\xc8\xc9\xab"}, /* フォント色 / フォント色 */
	[PACTEX_MW_WEIGHT] = {"\x83\x74\x83\x48\x83\x93\x83\x67\x91\xbe\x82\xb3", "\xa5\xd5\xa5\xa9\xa5\xf3\xa5\xc8\xcc\xab\xa4\xb5"}, /* フォント太さ / フォント太さ */
	[PACTEX_MW_EDGE] = {"\x83\x74\x83\x48\x83\x93\x83\x67\x89\x8f\x8e\xe6\x82\xe8", "\xa5\xd5\xa5\xa9\xa5\xf3\xa5\xc8\xbf\x46\xc8\xa1\xa4\xea"}, /* フォント縁取り / フォント縁取り */
	[PACTEX_MW_EDGE_COLOR] = {"\x83\x74\x83\x48\x83\x93\x83\x67\x89\x8f\x8e\xe6\x82\xe8\x90\x46", "\xa5\xd5\xa5\xa9\xa5\xf3\xa5\xc8\xbf\x46\xc8\xa1\xa4\xea\xc9\xab"}, /* フォント縁取り色 / フォント縁取り色 */
	[PACTEX_MW_CHAR_SPACE] = {"\x95\xb6\x8e\x9a\x8a\xd4\x8a\x75", "\xce\xc4\xd7\xd6\xe9\x67\xb8\xf4"}, /* 文字間隔 / 文字間隔 */
	[PACTEX_MW_LINE_SPACE] = {"\x8d\x73\x8a\xd4\x8a\x75", "\xd0\xd0\xe9\x67\xb8\xf4"}, /* 行間隔 / 行間隔 */
};

static struct ex_value *pactex_message_value(struct ex_tree *node, enum pactex_message_key key)
{
	if (!node || node->is_leaf) return NULL;
	for (unsigned i = 0; i < node->nr_children; i++) {
		struct ex_tree *c = &node->children[i];
		if (c->is_leaf && c->name &&
				(!strcmp(c->name->text, pactex_message_keys[key].sjis) ||
				 !strcmp(c->name->text, pactex_message_keys[key].gbk)))
			return &c->leaf.value;
	}
	return NULL;
}

static float pactex_message_number(struct ex_tree *node, enum pactex_message_key key, float fallback)
{
	struct ex_value *v = pactex_message_value(node, key);
	if (!v) return fallback;
	if (v->type == EX_FLOAT) return v->f;
	if (v->type == EX_INT) return v->i;
	return fallback;
}

static int pactex_message_item(struct ex_tree *node, enum pactex_message_key key,
		unsigned index, int fallback)
{
	struct ex_value *v = pactex_message_value(node, key);
	if (!v || v->type != EX_LIST || !v->list || index >= v->list->nr_items)
		return fallback;
	struct ex_value *item = &v->list->items[index].value;
	if (item->type == EX_FLOAT) return item->f;
	if (item->type == EX_INT) return item->i;
	return fallback;
}

static bool pactex_apply_message_window(struct ex_tree *type_info, const char *ptype, int parts_no)
{
	if (!ptype || (strcmp(ptype, "\x83\x81\x83\x62\x83\x5a\x81\x5b\x83\x57\x83\x45\x83\x42\x83\x93\x83\x68\x83\x45") &&
			strcmp(ptype, "\xd0\xc5\xcf\xa2\xb4\xb0\xbf\xda")))
		return false;
	struct parts *parts = parts_get(parts_no);
	parts->message_window = true;
	const char *cg = pactex_get_string(type_info, SJIS_CG_MEI);
	if (!cg) cg = pactex_get_string(type_info, GBK_CG_MEI);
	if (cg) {
		struct string *name = cstr_to_string(cg);
		PE_SetMessageWindowCGName(parts_no, name);
		free_string(name);
	}
	PE_SetMessageWindowTextArea(parts_no,
		pactex_message_item(type_info, PACTEX_MW_AREA, 0, 0),
		pactex_message_item(type_info, PACTEX_MW_AREA, 1, 0),
		pactex_message_item(type_info, PACTEX_MW_AREA, 2, 0),
		pactex_message_item(type_info, PACTEX_MW_AREA, 3, 0));
	PE_SetMessageWindowTextOriginPosMode(parts_no,
		pactex_message_number(type_info, PACTEX_MW_ORIGIN, 1));
	PE_SetMessageWindowTextFont(parts_no,
		pactex_message_number(type_info, PACTEX_MW_FACE, 0),
		pactex_message_number(type_info, PACTEX_MW_SIZE, 16),
		pactex_message_item(type_info, PACTEX_MW_COLOR, 0, 255),
		pactex_message_item(type_info, PACTEX_MW_COLOR, 1, 255),
		pactex_message_item(type_info, PACTEX_MW_COLOR, 2, 255),
		pactex_message_number(type_info, PACTEX_MW_WEIGHT, 0),
		pactex_message_item(type_info, PACTEX_MW_EDGE_COLOR, 0, 0),
		pactex_message_item(type_info, PACTEX_MW_EDGE_COLOR, 1, 0),
		pactex_message_item(type_info, PACTEX_MW_EDGE_COLOR, 2, 0),
		pactex_message_number(type_info, PACTEX_MW_EDGE, 0));
	PE_SetMessageWindowTextSpace(parts_no,
		pactex_message_number(type_info, PACTEX_MW_CHAR_SPACE, 0),
		pactex_message_number(type_info, PACTEX_MW_LINE_SPACE, 0));
	return true;
}

/* Native low-level parts (outer type 18) select their three states by name,
 * then construct a CG (19) or text (21) even if its content is empty.
 * See native 0x5b8bc0, 0x533d80 and 0x4df5b0. Unknown types keep the legacy
 * loader/getter behavior; an arbitrary PARTS_TEXT state is not a text widget. */
static bool pactex_name_is(struct ex_tree *node, const char *sjis, const char *gbk)
{
	return node->name && (!strcmp(node->name->text, sjis) || !strcmp(node->name->text, gbk));
}

static int pactex_named_state(struct ex_tree *node)
{
	static const struct { const char *sjis, *gbk; } names[] = {
		{"\x92\xca\x8f\xed\x8f\xf3\x91\xd4", "\xc6\xd5\xcd\xa8\xa0\xee\x91\x42"}, /* 通常状態 / 普通狀態 */
		{"\x83\x49\x83\x93\x83\x4a\x81\x5b\x83\x5c\x83\x8b\x8f\xf3\x91\xd4", "\xa5\xaa\xa5\xf3\xd6\xb8\xe1\x98\xa0\xee\x91\x42"}, /* オンカーソル状態 / オン指針狀態 */
		{"\x83\x4c\x81\x5b\x83\x5f\x83\x45\x83\x93\x8f\xf3\x91\xd4", "\xa5\xad\xa9\x60\xa5\xc0\xa5\xa6\xa5\xf3\xa0\xee\x91\x42"}, /* キーダウン状態 / キーダウン狀態 */
	};
	for (int i = 0; i < PARTS_NR_STATES; i++) {
		if (pactex_name_is(node, names[i].sjis, names[i].gbk))
			return i + 1;
	}
	return 0;
}

static void pactex_apply_text_style(struct ex_tree *state, int parts_no, int pe_state)
{
	for (unsigned i = 0; i < state->nr_children; i++) {
		struct ex_tree *style = &state->children[i];
		// Main text only: ruby has a separate decoration branch (0x5c34a8).
		if (style->is_leaf || !pactex_name_is(style,
				"\x83\x65\x83\x4c\x83\x58\x83\x67\x91\x95\x8f\xfc", "\xce\xc4\xb1\xbe\xd1\x62\xef\x97"))
			continue;
		PE_SetFont(parts_no,
			pactex_message_number(style, PACTEX_MW_FACE, 0),
			pactex_message_number(style, PACTEX_MW_SIZE, 16),
			pactex_message_item(style, PACTEX_MW_COLOR, 0, 255),
			pactex_message_item(style, PACTEX_MW_COLOR, 1, 255),
			pactex_message_item(style, PACTEX_MW_COLOR, 2, 255),
			pactex_message_number(style, PACTEX_MW_WEIGHT, 0),
			pactex_message_item(style, PACTEX_MW_EDGE_COLOR, 0, 0),
			pactex_message_item(style, PACTEX_MW_EDGE_COLOR, 1, 0),
			pactex_message_item(style, PACTEX_MW_EDGE_COLOR, 2, 0),
			pactex_message_number(style, PACTEX_MW_EDGE, 0), pe_state);
		// Text decoration uses 字間隔; message windows use 文字間隔.
		int spacing = pactex_get_int(style, "\x8e\x9a\x8a\xd4\x8a\x75",
			pactex_get_int(style, "\xd7\xd6\xe9\x67\xb8\xf4",
				pactex_message_number(style, PACTEX_MW_CHAR_SPACE, 0)));
		PE_SetTextCharSpace(parts_no, spacing, pe_state);
		PE_SetTextLineSpace(parts_no, pactex_message_number(style, PACTEX_MW_LINE_SPACE, 0), pe_state);
		return;
	}
}

/* 部件タイプ names in EPartsType order: native 0x4eda70 builds this table
 * and 0x5b8f90 uses a name's index as the widget type (CN/GBK spellings from
 * the table's string literals). A name outside the table keeps the loader's
 * legacy structural guess. */
enum {
	PACTEX_EPT_BUTTON = 0,
	PACTEX_EPT_LAYOUT_BOX = 8,
	PACTEX_EPT_USER_COMPONENT = 17,
	PACTEX_EPT_LOW_LEVEL = 18,
	PACTEX_EPT_CG = 19,
	PACTEX_EPT_TEXT = 21,
	PACTEX_EPT_NUMERAL = 24,
	PACTEX_EPT_RECT = 25,
	PACTEX_EPT_CONSTRUCTION = 26,
	PACTEX_EPT_CG_DETECTION = 27,
	PACTEX_EPT_COUNT = 31,
};
static const char *const pactex_ept_names[PACTEX_EPT_COUNT] = {
	"\xb0\xb4\xe2\x6f",                                     /* 0 按鈕 */
	"\x99\x7a\xb2\xe9\xa5\xdc\xa5\xc3\xa5\xaf\xa5\xb9",             /* 1 檢查ボックス */
	"\xd8\x51\x9d\x4c\x84\xd3\x97\x6c",                         /* 2 豎滾動條 */
	"\x99\x4d\x9d\x4c\x84\xd3\x97\x6c",                         /* 3 橫滾動條 */
	"\xce\xc4\xb1\xbe\xa5\xdc\xa5\xc3\xa5\xaf\xa5\xb9",             /* 4 文本ボックス */
	"\xa5\xea\xa5\xb9\xa5\xc8\xa5\xdc\xa5\xc3\xa5\xaf\xa5\xb9",     /* 5 リストボックス */
	"\xa5\xb3\xa5\xf3\xa5\xdc\xa5\xdc\xa5\xc3\xa5\xaf\xa5\xb9",     /* 6 コンボボックス */
	"\xa5\xde\xa5\xeb\xa5\xc1\xa5\xe9\xa5\xa4\xa5\xf3\xce\xc4\xb1\xbe\xa5\xdc\xa5\xc3\xa5\xaf\xa5\xb9", /* 7 */
	"\xa5\xec\xa5\xa4\xa5\xa2\xa5\xa6\xa5\xc8\xa5\xdc\xa5\xc3\xa5\xaf\xa5\xb9", /* 8 レイアウトボックス */
	"\xa5\xe9\xa5\xb8\xa5\xaa\xb0\xb4\xe2\x6f\xa5\xdc\xa5\xc3\xa5\xaf\xa5\xb9", /* 9 ラジオ按鈕ボックス */
	"\xd0\xc5\xcf\xa2\xb4\xb0\xbf\xda",                         /* 10 信息窗口 */
	"\xa5\xb9\xa5\xd4\xa5\xf3\xa5\xdc\xa5\xc3\xa5\xaf\xa5\xb9",     /* 11 スピンボックス */
	"\xd8\x51\xbb\xac\x89\x4b\xa9\x60\xa5\xd0\xa9\x60",             /* 12 豎滑塊ーバー */
	"\x99\x4d\xbb\xac\x89\x4b\xa9\x60\xa5\xd0\xa9\x60",             /* 13 橫滑塊ーバー */
	"\xa5\xd1\xa5\xcd\xa5\xeb",                                 /* 14 パネル */
	"\xa5\xd5\xa5\xa9\xa9\x60\xa5\xe0",                         /* 15 フォーム */
	"\xa5\xd5\xa5\xa9\xa9\x60\xa5\xe0\xc8\xba\xbd\x4d",             /* 16 フォーム群組 */
	"\xa5\xe6\xa9\x60\xa5\xb6\xa5\xb3\xa5\xf3\xa5\xdd\xa9\x60\xa5\xcd\xa5\xf3\xa5\xc8", /* 17 ユーザコンポーネント */
	"\xb5\xcd\xb5\xc8\xbc\x89\xb2\xbf\xbc\xfe",                     /* 18 低等級部件 */
	"\xa3\xc3\xa3\xc7\xb2\xbf\xbc\xfe",                         /* 19 ＣＧ部件 */
	"\xd1\xad\xad\x68\xa3\xc3\xa3\xc7\xb2\xbf\xbc\xfe",             /* 20 循環ＣＧ部件 */
	"\xce\xc4\xb1\xbe\xb2\xbf\xbc\xfe",                         /* 21 文本部件 */
	"\x99\x4d\xa5\xb2\xa9\x60\xa5\xb8\xb2\xbf\xbc\xfe",             /* 22 橫ゲージ部件 */
	"\xd8\x51\xa5\xb2\xa9\x60\xa5\xb8\xb2\xbf\xbc\xfe",             /* 23 豎ゲージ部件 */
	"\x94\xb5\xd7\xd6\xb2\xbf\xbc\xfe",                         /* 24 數字部件 */
	"\xbe\xd8\xd0\xce\xb2\xbf\xbc\xfe",                         /* 25 矩形部件 */
	"\x98\x8b\xba\x42\xb2\xbf\xbc\xfe",                         /* 26 構築部件 */
	"\xa3\xc3\xa3\xc7\xc5\xd0\xb6\xa8\xb2\xbf\xbc\xfe",             /* 27 ＣＧ判定部件 */
	"\xa5\xd5\xa5\xe9\xa5\xc3\xa5\xc8\xb2\xbf\xbc\xfe",             /* 28 フラット部件 */
	"\xa3\xb3\xa3\xc4\xa5\xec\xa5\xa4\xa5\xe4\xb2\xbf\xbc\xfe",     /* 29 ３Ｄレイヤ部件 */
	"\xa5\xe0\xa9\x60\xa5\xd3\xa9\x60\xb2\xbf\xbc\xfe",             /* 30 ムービー部件 */
};

static int pactex_native_type(const char *name)
{
	if (!name)
		return -1;
	for (int i = 0; i < PACTEX_EPT_COUNT; i++) {
		if (!strcmp(name, pactex_ept_names[i]))
			return i;
	}
	return -1;
}

static const char *pactex_parts_type_name(struct ex_tree *node)
{
	const char *type = pactex_get_string(node, SJIS_PARTS_TYPE);
	return type ? type : pactex_get_string(node, GBK_PARTS_TYPE);
}

/* Low-level state types the loader builds (see pactex_apply_low_level_state);
 * others keep the legacy ordinal CG fallback and report the outer type. */
static int pactex_low_level_type(struct ex_tree *state)
{
	const char *type = pactex_parts_type_name(state);
	if (!type) return -1;
	if (!strcmp(type, "\x83\x65\x83\x4c\x83\x58\x83\x67\x83\x70\x81\x5b\x83\x63")) /* テキストパーツ */
		return PACTEX_EPT_TEXT;
	if (!strcmp(type, "\x82\x62\x82\x66\x83\x70\x81\x5b\x83\x63")) /* ＣＧパーツ */
		return PACTEX_EPT_CG;
	switch (pactex_native_type(type)) {
	case PACTEX_EPT_CG: return PACTEX_EPT_CG;
	case PACTEX_EPT_TEXT: return PACTEX_EPT_TEXT;
	case PACTEX_EPT_NUMERAL: return PACTEX_EPT_NUMERAL;
	case PACTEX_EPT_RECT: return PACTEX_EPT_RECT;
	case PACTEX_EPT_CONSTRUCTION: return PACTEX_EPT_CONSTRUCTION;
	case PACTEX_EPT_CG_DETECTION: return PACTEX_EPT_CG_DETECTION;
	default: return -1;
	}
}

/* 構築部件 with a non-empty 手順リスト: the loader does not run construction
 * steps (e.g. SceneAzito's Bg: load 背景／那由多, then blur), so such a state
 * keeps the legacy CG fallback (the first step's ＣＧ名) and only reports
 * type 26. An empty list (StandView's PlayerC) is filled by the game. */
static bool pactex_construction_has_steps(struct ex_tree *state)
{
	for (unsigned i = 0; i < state->nr_children; i++) {
		struct ex_tree *c = &state->children[i];
		if (!c->is_leaf && pactex_name_is(c, "\x8e\xe8\x8f\x87\x83\x8a\x83\x58\x83\x67",
				"\xca\xd6\xed\x98\xa5\xea\xa5\xb9\xa5\xc8")) /* 手順リスト */
			return c->nr_children > 0;
	}
	return false;
}

static bool pactex_low_level_keeps_legacy_cg(struct ex_tree *state)
{
	return pactex_low_level_type(state) == PACTEX_EPT_CONSTRUCTION
		&& pactex_construction_has_steps(state);
}

/* 矩形部件: the bounding box of the four corners 左上/右上/左下/右下 (矩形模式 1,
 * the only mode in the game's pactex; all have 左上 = (0, 0)). */
static void pactex_rect_size(struct ex_tree *state, int *w, int *h)
{
	static const char *const corners[4][2] = {
		{ "\x8d\xb6\x8f\xe3", "\xd7\xf3\xc9\xcf" },	/* 左上 */
		{ "\x89\x45\x8f\xe3", "\xd3\xd2\xc9\xcf" },	/* 右上 */
		{ "\x8d\xb6\x89\xba", "\xd7\xf3\xcf\xc2" },	/* 左下 */
		{ "\x89\x45\x89\xba", "\xd3\xd2\xcf\xc2" },	/* 右下 */
	};
	float min_x = 0, min_y = 0, max_x = 0, max_y = 0;
	bool any = false;
	for (int i = 0; i < 4; i++) {
		struct ex_list *l = pactex_get_list(state, corners[i][0]);
		if (!l) l = pactex_get_list(state, corners[i][1]);
		if (!l || l->nr_items < 2)
			continue;
		float v[2];
		for (int k = 0; k < 2; k++) {
			struct ex_value *e = &l->items[k].value;
			v[k] = e->type == EX_FLOAT ? e->f : e->type == EX_INT ? e->i : 0;
		}
		if (!any || v[0] < min_x) min_x = v[0];
		if (!any || v[0] > max_x) max_x = v[0];
		if (!any || v[1] < min_y) min_y = v[1];
		if (!any || v[1] > max_y) max_y = v[1];
		any = true;
	}
	*w = (int)lroundf(max_x - min_x);
	*h = (int)lroundf(max_y - min_y);
}

/* 數字部件: numeral widget state. 表示タイプ 2 draws the digits with the
 * state's font (フォントタイプ/サイズ/色/太さ/縁取り/縁取り色), padded with
 * zeros (ゼロパディング) and full-width if 全角; see parts_numeral_update_font. */
static void pactex_apply_numeral_state(struct ex_tree *state, int parts_no, int pe_state)
{
	struct parts_numeral *num = parts_get_numeral(parts_get(parts_no), pe_state - 1);
	num->show_type = pactex_get_int(state, "\x95\x5c\x8e\xa6\x83\x5e\x83\x43\x83\x76", /* 表示タイプ */
		pactex_get_int(state, "\xb1\xed\xca\xbe\xa5\xbf\xa5\xa4\xa5\xd7", 0));
	num->zero_pad = pactex_get_int(state, "\x83\x5b\x83\x8d\x83\x70\x83\x66\x83\x42\x83\x93\x83\x4f", /* ゼロパディング */
		pactex_get_int(state, "\xa5\xbc\xa5\xed\xa5\xd1\xa5\xc7\xa5\xa3\xa5\xf3\xa5\xb0", 1)) != 0;
	num->full_pitch = pactex_get_int(state, "\x91\x53\x8a\x70", /* 全角 */
		pactex_get_int(state, "\xc8\xab\xbd\xc7", 0)) != 0;
	struct text_style *ts = &num->font;
	ts->face = pactex_message_number(state, PACTEX_MW_FACE, 0);
	ts->size = pactex_message_number(state, PACTEX_MW_SIZE, 16);
	ts->color = (SDL_Color) { pactex_message_item(state, PACTEX_MW_COLOR, 0, 255),
		pactex_message_item(state, PACTEX_MW_COLOR, 1, 255),
		pactex_message_item(state, PACTEX_MW_COLOR, 2, 255), 255 };
	float bold = pactex_message_number(state, PACTEX_MW_WEIGHT, 0);
	ts->weight = bold * 1000;
	ts->bold_weight = bold;
	ts->edge_color = (SDL_Color) { pactex_message_item(state, PACTEX_MW_EDGE_COLOR, 0, 0),
		pactex_message_item(state, PACTEX_MW_EDGE_COLOR, 1, 0),
		pactex_message_item(state, PACTEX_MW_EDGE_COLOR, 2, 0), 255 };
	text_style_set_edge_width(ts, pactex_message_number(state, PACTEX_MW_EDGE, 0));
	int length = pactex_get_int(state, "\x8c\x85\x90\x94", /* 桁数 */
		pactex_get_int(state, "\xe8\xec\x94\xb5", 1)); /* 桁數 */
	PE_SetNumeralLength(parts_no, length, pe_state);
	int comma = pactex_get_int(state, "\x83\x52\x83\x93\x83\x7d\x95\x5c\x8e\xa6", /* コンマ表示 */
		pactex_get_int(state, "\xa5\xb3\xa5\xf3\xa5\xde\xb1\xed\xca\xbe", 0));
	PE_SetNumeralShowComma(parts_no, comma != 0, pe_state);
	int space = pactex_get_int(state, "\x8e\x9a\x8a\xd4\x8a\x75", /* 字間隔 */
		pactex_get_int(state, "\xd7\xd6\xe9\x67\xb8\xf4", 0));
	PE_SetNumeralSpace(parts_no, space, pe_state);
}

static bool pactex_apply_low_level_state(struct ex_tree *state, int parts_no, int pe_state)
{
	if (!pe_state) return false;
	int type = pactex_low_level_type(state);
	if (type < 0) return false;
	if (type == PACTEX_EPT_RECT || type == PACTEX_EPT_CONSTRUCTION) {
		// 矩形部件: an undrawn rectangle (GetRect -> CRectParts, sized by
		// RectSize/AFL_Parts_GetSize); 構築部件: a construction state
		// (GetConstruction -> CConstructionParts). CompParts(name, 25/26, 1)
		// needs the state type (ThumbnailPage, StandView). Their
		// サーフェイスエリア are all (0, 0, 0, 0) in the game's pactex.
		struct parts *parts = parts_get(parts_no);
		if (type == PACTEX_EPT_RECT) {
			int w, h;
			pactex_rect_size(state, &w, &h);
			PE_SetPartsRectangleDetectionSize(parts_no, w, h, pe_state);
		} else if (!pactex_construction_has_steps(state)) {
			parts_get_construction_process(parts, pe_state - 1);
		}
		parts->component_state_type[pe_state - 1] = type;
		return true;
	}
	bool text = type == PACTEX_EPT_TEXT;
	bool numeral = type == PACTEX_EPT_NUMERAL;
	struct parts *parts = parts_get(parts_no);
	if (text) {
		parts_get_text(parts, pe_state - 1);
		pactex_apply_text_style(state, parts_no, pe_state);
	} else if (numeral) {
		pactex_apply_numeral_state(state, parts_no, pe_state);
	} else {
		// ＣＧ部件, or ＣＧ判定部件: a CG kept for hit testing, not drawn.
		parts_get_cg(parts, pe_state - 1);
		const char *cg_name = pactex_find_cg_name(state, 0);
		if (cg_name) {
			struct string *s = cstr_to_string(cg_name);
			PE_SetPartsCG(parts_no, s, 0, pe_state);
			free_string(s);
		}
		if (type == PACTEX_EPT_CG_DETECTION)
			PE_SetClickable(parts_no, true);
	}
	parts->component_state_type[pe_state - 1] = type;
	// Use the matching setter: the CG setter would destroy a text state.
	struct ex_list *area = pactex_get_list(state, SJIS_SURFACE_AREA);
	if (!area) area = pactex_get_list(state, "\xa5\xb5\xa9\x60\xa5\xd5\xa5\xa7\xa5\xa4\xa5\xb9\xa5\xa8\xa5\xea\xa5\xa2");
	int x, y, w, h;
	if (area && area->nr_items >= 4 &&
			area->items[0].value.type == EX_INT && area->items[1].value.type == EX_INT &&
			area->items[2].value.type == EX_INT && area->items[3].value.type == EX_INT) {
		x = area->items[0].value.i; y = area->items[1].value.i;
		w = area->items[2].value.i; h = area->items[3].value.i;
	} else if (text || numeral || !pactex_get_surface_area(state, &x, &y, &w, &h, 0)) {
		return true;
	}
	if (text) PE_SetPartsTextSurfaceArea(parts_no, x, y, w, h, pe_state);
	else if (numeral) PE_SetNumeralSurfaceArea(parts_no, x, y, w, h, pe_state);
	else PE_SetPartsCGSurfaceArea(parts_no, x, y, w, h, pe_state);
	return true;
}

/* Apply pactex properties (position, show, alpha, CG) to a parts entry.
 * Extracts standard properties from leaf children, and CG names from
 * the type-specific info branch (種類別情報). */
static void pactex_apply_properties(struct ex_tree *node, int parts_no)
{
	pactex_apply_pixel_decide(node, parts_no);

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
	/* 編輯上表示 = 0 hides the parts and its subtree at run time as well
	 * (native parser 0x553dfa stores it at parts +0xac; see
	 * parts->edit_hidden). Only the CN spelling is known. */
	parts_set_edit_hidden(parts_get(parts_no),
		!pactex_get_int(node, "\xbe\x8e\xdd\x8b\xc9\xcf\xb1\xed\xca\xbe", 1));

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

	if (pactex_apply_message_window(type_info, ptype, parts_no))
		return;

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
		/* ＣＧ名 is the base path (e.g. "システム／タイトル／ボタン／はじめから");
		 * the widget shows <base>／普通 (SJIS ／通常), ／オン, ／ダウン, and
		 * <base>／無効 while disabled (see parts_button_set_cg_name). */
		const char *cg_base = pactex_get_string(type_info, SJIS_CG_MEI);
		if (!cg_base)
			cg_base = pactex_get_string(type_info, GBK_CG_MEI);
		if (cg_base) {
			parts_button_set_cg_name(parts_get(parts_no), cg_base);
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
	bool low_level = ptype && (!strcmp(ptype, SJIS_MULTI_LEVEL) ||
		!strcmp(ptype, "\xb5\xcd\xb5\xc8\xbc\x89\xb2\xbf\xbc\xfe")); /* 低等級部件 */
	for (unsigned i = 0; i < type_info->nr_children; i++) {
		struct ex_tree *state = &type_info->children[i];
		if (state->is_leaf) continue;

		int pe_state = ++state_idx; /* Preserve legacy order for unknown types. */
		if (low_level && pactex_named_state(state) && pactex_low_level_type(state) >= 0
				&& !pactex_low_level_keeps_legacy_cg(state))
			continue;

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
	}
	// Apply recognized states last so legacy ordinal fallback cannot overwrite
	// a named state when an unrelated/unknown branch occupies the same index.
	if (low_level) {
		for (unsigned i = 0; i < type_info->nr_children; i++) {
			struct ex_tree *state = &type_info->children[i];
			if (!state->is_leaf)
				pactex_apply_low_level_state(state, parts_no, pactex_named_state(state));
		}
	}
}

static const char *pactex_get_exact_string(struct ex_tree *node, const char *name)
{
	if (!node || node->is_leaf) return NULL;
	for (unsigned i = 0; i < node->nr_children; i++) {
		struct ex_tree *c = &node->children[i];
		if (c->is_leaf && c->name && !strcmp(c->name->text, name)
				&& c->leaf.value.type == EX_STRING && c->leaf.value.s)
			return c->leaf.value.s->text;
	}
	return NULL;
}

/* Widget type and user component name of a pactex component. The native
 * loader (0x5b8f90) indexes 部件タイプ in the EPartsType name table and a
 * user component reads ユーザコンポーネント名 (0x4e50c0); GetUserComponentName
 * returns that string, e.g. "Fotter" for SceneHome's Footer. Returns false
 * for a name outside the table so the caller keeps its legacy guess. */
static bool pactex_apply_native_type(struct ex_tree *node, struct parts *p)
{
	struct ex_tree *type_info = pactex_find_type_info(node);
	int type = pactex_native_type(type_info ? pactex_parts_type_name(type_info) : NULL);
	if (type < 0)
		return false;
	p->component_type = type;
	free(p->user_component_name);
	p->user_component_name = NULL;
	if (type == PACTEX_EPT_USER_COMPONENT) {
		const char *uc = pactex_get_exact_string(type_info,
			"\xa5\xe6\xa9\x60\xa5\xb6\xa5\xb3\xa5\xf3\xa5\xdd\xa9\x60\xa5\xcd\xa5\xf3\xa5\xc8\xc3\xfb");
		if (!uc) uc = pactex_get_exact_string(type_info,
			"\x83\x86\x81\x5b\x83\x55\x83\x52\x83\x93\x83\x7c\x81\x5b\x83\x6c\x83\x93\x83\x67\x96\xbc");
		if (uc && uc[0])
			p->user_component_name = strdup(uc);
		/* 數據 (SJIS データ): a key, value, key, value, ... string list. */
		struct ex_list *data = pactex_get_list(type_info, "\x94\xb5\x93\xfe");
		if (!data) data = pactex_get_list(type_info, "\x83\x66\x81\x5b\x83\x5e");
		for (unsigned i = 0; data && i + 1 < data->nr_items; i += 2) {
			struct ex_value *k = &data->items[i].value, *v = &data->items[i + 1].value;
			if (k->type == EX_STRING && v->type == EX_STRING && k->s && v->s)
				parts_uc_data_set(p, k->s->text, v->s->text);
		}
	}
	return true;
}

/* Recursively create PE parts entries from a pactex component branch.
 * A 部件タイプ from the native table decides the widget type (see
 * pactex_apply_native_type). Otherwise the type is guessed structurally:
 * containers and CG-less leaves 0, leaves with a CG 1. */
static void pactex_create_component(struct activity *act, struct ex_tree *node,
		int parent_no, int depth)
{
	if (node->is_leaf || depth > 15) return;

	int parts_no = alloc_activity_parts_no();
	struct parts *p = parts_get(parts_no);

	/* Legacy: the node name stands in for an unknown widget's UC name. */
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

	/* Legacy structural guess: 0 for containers and CG-less leaves, 1 for
	 * leaves with a CG. */
	if (pactex_apply_native_type(node, p)) {
		/* native widget type */
	} else if (buhin && buhin->nr_children > 0) {
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

	/* Root container: its native type (usually a layout box), else 0. */
	struct ex_tree *root_buhin = pactex_find_buhin(root_branch);
	root->component_type = 0;
	pactex_apply_native_type(root_branch, root);

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

static void release_parts_recursive(int parts_no, struct page **delegate_indices)
{
	struct parts *p = parts_try_get(parts_no);
	if (!p) return;
	*delegate_indices = array_pushback(*delegate_indices,
			(union vm_value){.i = p->delegate_index}, AIN_ARRAY_INT, -1);
	while (!TAILQ_EMPTY(&p->children)) {
		struct parts *child = TAILQ_FIRST(&p->children);
		release_parts_recursive(child->no, delegate_indices);
	}
	parts_release(parts_no);
}

void pe_v14_append_int_list(int out_slot, const struct page *list);

/* The erase list returns the released parts' delegate indices (0x540050 ->
 * 0x55e400 reads them with 0x56d620, as RemoveController does), which
 * activity::detail::Release hands to ReleaseFunctionSetList. */
static bool PartsEngine_ReleaseActivity(struct string *name, int erase_list)
{
	int idx = find_activity(name);
	if (idx < 0) return false;
	struct activity *act = &activities[idx];
	struct page *delegate_indices = NULL;
	for (int i = 0; i < act->nr_parts; i++)
		release_parts_recursive(act->parts[i].number, &delegate_indices);
	pe_v14_append_int_list(erase_list, delegate_indices);
	if (delegate_indices) {
		delete_page_vars(delegate_indices);
		free_page(delegate_indices);
	}
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
	struct page *delegate_indices = NULL;
	for (int i = 0; i < act->nr_parts; i++)
		release_parts_recursive(act->parts[i].number, &delegate_indices);
	if (delegate_indices) {
		delete_page_vars(delegate_indices);
		free_page(delegate_indices);
	}
	act->nr_parts = 0;
}

static int PartsEngine_NumofActivityParts(struct string *name)
{
	int idx = find_activity(name);
	return idx >= 0 ? activities[idx].nr_parts : 0;
}

/* bool GetActivityParts(int Index, string Name, wrap<string> PartsName,
 * wrap<int> Number): native case 36 (0x57c2f5 -> 0x58a9c0) writes both
 * outputs for the index-th entry. ActivityHelper::GetPartsNames builds the
 * list GetUser<T>("Button\d") binds (Footer buttons, among others). */
static bool PartsEngine_GetActivityParts(int index, struct string *name, int parts_name_slot,
		union vm_value *number)
{
	int idx = find_activity(name);
	if (idx < 0) return false;
	struct activity *act = &activities[idx];
	if (index < 0 || index >= act->nr_parts) return false;
	struct activity_part *ap = &act->parts[index];
	wrap_slot_set_string(parts_name_slot, make_string(ap->name, strlen(ap->name)));
	if (number)
		number->i = ap->number;
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

static bool PartsEngine_Parts_IsPartsPixelDecide(int parts_no)
{
	struct parts *parts = parts_try_get(parts_no);
	return parts && parts->pixel_hittest;
}

/* Register the Activity API into lib_PartsEngine. Called from
 * PartsEngine's _PreLink when the AIN declares CreateActivity (v14). */
void pe_v14_activity_prelink(void)
{
	struct static_library *lib = &lib_PartsEngine;
	static_library_register(lib, "Parts_IsPartsPixelDecide", PartsEngine_Parts_IsPartsPixelDecide);
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
