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
#include "system4/cg.h"
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

/* Activity components whose parts a controller released (RemoveController,
 * ReleaseAllWithoutSystem), one byte per number handed out; numbers are
 * never handed out twice.
 *
 * The activity table itself is the script's and stays as it is: entries go
 * when the script releases the activity (ReleaseActivity 0x540050 and the
 * like), not when a controller releases the parts. The original's
 * RemoveController (0x53d500 -> 0x5386c0, 0x53bea0) does not touch the table
 * IsExistActivity (0x58a7a0) looks names up in, and the script counts on
 * that: activity::detail::GetFreeName takes the first name IsExistActivity
 * does not know, so a name must stay taken for as long as its owner has not
 * released it, also after the layer it was loaded on is gone (the customer
 * cards of the sale result screen). So the table can name parts that no
 * longer exist; this only remembers which, for parts_get to say so when
 * such a number is written to again. */
static uint8_t *controller_released;
static int controller_released_size;

void pe_v14_activity_parts_released(int parts_no)
{
	if (parts_no < ACTIVITY_PARTS_BASE || parts_no >= next_activity_parts_no)
		return;
	int i = parts_no - ACTIVITY_PARTS_BASE;
	if (i >= controller_released_size) {
		int size = max(next_activity_parts_no - ACTIVITY_PARTS_BASE, controller_released_size * 2);
		controller_released = xrealloc_array(controller_released, controller_released_size, size, 1);
		controller_released_size = size;
	}
	controller_released[i] = 1;
}

bool pe_v14_activity_number_released(int parts_no)
{
	int i = parts_no - ACTIVITY_PARTS_BASE;
	return i >= 0 && i < controller_released_size && controller_released[i];
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
static const char SJIS_ALPHA_CLIPPER[] = "\x83\x41\x83\x8b\x83\x74\x83\x40\x83\x4e\x83\x8a\x83\x62\x83\x70\x81\x5b"; /* アルファクリッパー */
static const char GBK_ALPHA_CLIPPER[]  = "\xa5\xa2\xa5\xeb\xa5\xd5\xa5\xa1\xa5\xaf\xa5\xea\xa5\xc3\xa5\xd1\xa9\x60"; /* アルファクリッパー (GBK) */
/* static const char SJIS_NORMAL_STATE[]= "\x92\xca\x8f\xed\x8f\xf3\x91\xd4"; */ /* 通常状態 — unused, kept for reference */

/* GBK property names (legacy fallback). */
static const char GBK_POSITION[]    = "\xd7\xf9\x98\xcb";         /* 座標 (GBK) */
static const char GBK_SHOW[]        = "\xb1\xed\xca\xbe";         /* 表示 (GBK) */
static const char GBK_ALPHA[]       = "\xa5\xa2\xa5\xeb\xa5\xd5\xa5\xa1"; /* アルファ (GBK) */
static const char GBK_ORIGIN_MODE[] = "\xd4\xad\xfc\x63\xd7\xf9\x98\xcb\xc4\xa3\xca\xbd"; /* 原點座標模式 (GBK) */
static const char GBK_CG_MEI[]      = "\xa3\xc3\xa3\xc7\xc3\xfb"; /* ＣＧ名 (GBK) */
/* static const char GBK_MUL_COLOR[]   = "\x81\x5c\xcb\xe3\xc9\xab"; */ /* 乗算色 (GBK) — unused */
static const char GBK_DRAW_FILTER[] = "\xc3\xe8\xae\x8b\xa5\xd5\xa5\xa3\xa5\xeb\xa5\xbf"; /* 描畫フィルタ (GBK) */
static const char GBK_ADD_COLOR[]   = "\xbc\xd3\xcb\xe3\xc9\xab"; /* 加算色 (GBK) */
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

/* オン指針透過 (on-cursor pass-through), a field of the component like the
 * pixel decision above: the native parser (0x553650, called with the block
 * at parts +0x84) reads it at 0x5547f4 as "== 1" into block +0x121, i.e.
 * parts +0x1a5, the flag SetPassCursor (0x58f700) writes and the input
 * target predicate (0x546e20) reads. A decoration marked 1 (407 components,
 * e.g. the title characters and every text and number of WorkerParamView)
 * blocks neither the hover nor the click of the parts behind it. Only the CN
 * spelling is known; a missing key is 0, as the parser's default. */
static const char GBK_CN_PASS_CURSOR[] = "\xa5\xaa\xa5\xf3\xd6\xb8\xe1\x98\xcd\xb8\xdf\x5e"; /* オン指針透過 */

static void pactex_apply_pass_cursor(struct ex_tree *node, int parts_no)
{
	PE_SetPassCursor(parts_no, pactex_get_int(node, GBK_CN_PASS_CURSOR, 0) == 1);
}

/* 點擊許可 (click permission), the field the same parser reads just before
 * オン指針透過: 0x5547be gets it with the default 0 and stores "== 1" at block
 * +0x120 (0x5547e9), i.e. parts +0x1a4, the flag Parts_SetClickable
 * (0x58f830) writes and both the input target predicate (0x546e20) and the
 * release handler (0x5788d0: only a clickable element gets the MouseClick)
 * read. Every component of the game has the key and 38 are 1. The 29 of
 * them that are neither buttons nor ＣＧ判定部件 (the ClickTarget, ClickGuard
 * and InputGuard rects, DungeonSelector's Area, the CG mode's side panels)
 * never got a click: the script registers its events on them, or nothing
 * for the guards, and relies on this flag.
 *
 * The native store is unconditional and the field starts as 0 (0x550a2f).
 * Here only a 1 is applied. A parts the loader has just allocated is not
 * clickable either, so the result is the same (numbers are not reused
 * within a process; after loading a save made by another process the loader
 * can be handed a restored parts, which is not handled here). And this flag
 * also stands in for the inner parts of a button widget, which the widget
 * makes clickable itself (0x528690) whatever its component says (190 of the
 * 192 buttons are 0). Only the CN spelling is known, not the key's SJIS
 * name.
 *
 * click_permission_only marks the parts as clickable through this key
 * alone; the button and ＣＧ判定部件 code further down clears it again
 * (PE_SetClickable). Until the script registers an event for a marked
 * parts, a press on it stays the whole-screen click (v14_click_unclaimed in
 * parts/input.c). */
static const char GBK_CN_CLICK_PERMISSION[] = "\xfc\x63\x93\xf4\xd4\x53\xbf\xc9"; /* 點擊許可 */

static void pactex_apply_click_permission(struct ex_tree *node, int parts_no)
{
	if (pactex_get_int(node, GBK_CN_CLICK_PERMISSION, 0) == 1) {
		PE_SetClickable(parts_no, true);
		parts_get(parts_no)->click_permission_only = true;
	}
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

static void pactex_apply_key_wait(struct ex_tree *type_info, int parts_no);

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
	pactex_apply_key_wait(type_info, parts_no);
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

/* The value of the leaf named sjis or gbk among node's children. */
static struct ex_value *pactex_leaf_value(struct ex_tree *node, const char *sjis, const char *gbk)
{
	if (!node || node->is_leaf) return NULL;
	for (unsigned i = 0; i < node->nr_children; i++) {
		struct ex_tree *c = &node->children[i];
		if (c->is_leaf && pactex_name_is(c, sjis, gbk))
			return &c->leaf.value;
	}
	return NULL;
}

static float pactex_value_number(const struct ex_value *v, float fallback)
{
	if (v && v->type == EX_FLOAT) return v->f;
	if (v && v->type == EX_INT) return v->i;
	return fallback;
}

/* キー待ちマーク of a message window (props::Load, 0x4f0821 to 0x4f0c5a): the
 * block's ＣＧ名 and フラット名 as they are, 循環ＣＧ開始番號, 循環ＣＧ枚數,
 * 循環ＣＧ切換時間 and the three items of 座標, each 0 when missing; without
 * the block the mark is the default one (no names, all 0, 0x4ee460). A loaded
 * mark is hidden. The names of the three numbers are the GBK release's; the
 * SJIS ones are those characters in SJIS, a guess (no SJIS pactex was read). */
static void pactex_apply_key_wait(struct ex_tree *type_info, int parts_no)
{
	struct ex_tree *block = NULL;
	if (type_info && !type_info->is_leaf) {
		for (unsigned i = 0; i < type_info->nr_children && !block; i++) {
			struct ex_tree *c = &type_info->children[i];
			if (!c->is_leaf && pactex_name_is(c,
					"\x83\x4c\x81\x5b\x91\xd2\x82\xbf\x83\x7d\x81\x5b\x83\x4e",
					"\xa5\xad\xa9\x60\xb4\xfd\xa4\xc1\xa5\xde\xa9\x60\xa5\xaf"))
				block = c;
		}
	}
	struct ex_value *cg = pactex_leaf_value(block, "\x82\x62\x82\x66\x96\xbc", "\xa3\xc3\xa3\xc7\xc3\xfb");
	struct ex_value *flat = pactex_leaf_value(block,
			"\x83\x74\x83\x89\x83\x62\x83\x67\x96\xbc", "\xa5\xd5\xa5\xe9\xa5\xc3\xa5\xc8\xc3\xfb");
	struct ex_value *pos = pactex_leaf_value(block, "\x8d\xc0\x95\x57", "\xd7\xf9\x98\xcb");
	int xyz[3] = {0, 0, 0};
	if (pos && pos->type == EX_LIST && pos->list) {
		for (unsigned i = 0; i < 3 && i < pos->list->nr_items; i++)
			xyz[i] = pactex_value_number(&pos->list->items[i].value, 0);
	}
	struct string *name = cstr_to_string(cg && cg->type == EX_STRING && cg->s ? cg->s->text : "");
	PE_SetKeyWaitCGName(parts_no, name,
		pactex_value_number(pactex_leaf_value(block,
			"\x8f\x7a\x8a\xc2\x82\x62\x82\x66\x8a\x4a\x8e\x6e\x94\xd4\xe5\x6a",
			"\xd1\xad\xad\x68\xa3\xc3\xa3\xc7\xe9\x5f\xca\xbc\xb7\xac\xcc\x96"), 0),
		pactex_value_number(pactex_leaf_value(block,
			"\x8f\x7a\x8a\xc2\x82\x62\x82\x66\x96\x87\x9d\xc9",
			"\xd1\xad\xad\x68\xa3\xc3\xa3\xc7\xc3\xb6\x94\xb5"), 0),
		pactex_value_number(pactex_leaf_value(block,
			"\x8f\x7a\x8a\xc2\x82\x62\x82\x66\x90\xd8\x8a\xb7\x8e\x9e\x8a\xd4",
			"\xd1\xad\xad\x68\xa3\xc3\xa3\xc7\xc7\xd0\x93\x51\x95\x72\xe9\x67"), 0));
	free_string(name);
	name = cstr_to_string(flat && flat->type == EX_STRING && flat->s ? flat->s->text : "");
	PE_SetKeyWaitFlatName(parts_no, name);
	free_string(name);
	PE_SetKeyWaitPos(parts_no, xyz[0], xyz[1], xyz[2]);
	PE_SetKeyWaitShow(parts_no, false);
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

/* 文本: the text a text state starts with. The native state loader
 * (0x5c2d20) reads it at 0x5c2fac, default "", into the state's text
 * (+0xd4). Fixed labels such as the 春銷 counts' 人材／顧客 and ／, or 剩餘時間,
 * are never set by the AIN; it replaces placeholders through Parts_SetText.
 * Exact key match: 文本位置 and 文本裝飾 start with 文本. */
static void pactex_apply_default_text(struct ex_tree *state, int parts_no, int pe_state)
{
	for (unsigned i = 0; i < state->nr_children; i++) {
		struct ex_tree *c = &state->children[i];
		if (!c->is_leaf || !pactex_name_is(c, "\x83\x65\x83\x4c\x83\x58\x83\x67", "\xce\xc4\xb1\xbe"))
			continue;
		if (c->leaf.value.type != EX_STRING || !c->leaf.value.s || !c->leaf.value.s->text[0])
			return;
		PE_SetText(parts_no, c->leaf.value.s, pe_state);
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
	PACTEX_EPT_HGAUGE = 22,
	PACTEX_EPT_VGAUGE = 23,
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
	case PACTEX_EPT_HGAUGE: return PACTEX_EPT_HGAUGE;
	case PACTEX_EPT_VGAUGE: return PACTEX_EPT_VGAUGE;
	case PACTEX_EPT_NUMERAL: return PACTEX_EPT_NUMERAL;
	case PACTEX_EPT_RECT: return PACTEX_EPT_RECT;
	case PACTEX_EPT_CONSTRUCTION: return PACTEX_EPT_CONSTRUCTION;
	case PACTEX_EPT_CG_DETECTION: return PACTEX_EPT_CG_DETECTION;
	default: return -1;
	}
}

/* 構築部件 with a non-empty 手順リスト. The native state loader (0x5a64e0)
 * reads the list (0x5a6569) and runs its steps at once (0x5a66ed -> 0x5a73c0
 * -> 0x4fa1d0), so the state has its surface before the AIN sees it. The
 * loader here builds the states pactex_construction_plan accepts (among them
 * the blurred backgrounds, e.g. SceneAzito's Bg: load 背景／那由多, then
 * blur); the others keep the legacy CG fallback (the first step's ＣＧ名)
 * and only report type 26. An empty list (StandView's PlayerC) is filled by
 * the game. */
static struct ex_tree *pactex_construction_steps(struct ex_tree *state)
{
	for (unsigned i = 0; i < state->nr_children; i++) {
		struct ex_tree *c = &state->children[i];
		if (!c->is_leaf && pactex_name_is(c, "\x8e\xe8\x8f\x87\x83\x8a\x83\x58\x83\x67",
				"\xca\xd6\xed\x98\xa5\xea\xa5\xb9\xa5\xc8")) /* 手順リスト */
			return c;
	}
	return NULL;
}

static bool pactex_construction_has_steps(struct ex_tree *state)
{
	struct ex_tree *list = pactex_construction_steps(state);
	return list && list->nr_children > 0;
}

/* One 手順, as far as the commands built here read it. The native step
 * parser (0x4f82a0) stores コマンド at +4, 先矩形 X,Y,X2,Y2,W,H at
 * +0x1c..+0x30, 色１ at +0x34..+0x40, 色２ at +0x44..+0x50, ＣＧ名 at +0xac,
 * 全體 at +0xc4, 半徑 at +0xc8/+0xcc, 旋轉角度 at +0xd8 and 円弧角度 (start,
 * sweep) at +0xdc/+0xe0. */
struct pactex_cp_step {
	int command;
	int x, y, w, h;
	int color[4];
	int alpha2;		// 色２'s alpha
	bool full;
	int rx, ry;
	int rotate;
	int start, sweep;
	int blur;		// ブラー (+0x54)
	const char *cg_name;	// in the pactex tree
};

enum {
	PACTEX_CP_CREATE = 0,
	PACTEX_CP_CREATE_CG = 2,
	PACTEX_CP_FILL = 3,
	PACTEX_CP_BLEND_FILL = 4,
	PACTEX_CP_FILL_AMAP = 5,
	PACTEX_CP_FILL_WITH_ALPHA = 6,
	PACTEX_CP_GRAY_SCALE = 15,
	PACTEX_CP_MUL_AMAP_GRADATION_ROWS = 25,
	PACTEX_CP_MUL_AMAP_GRADATION_COLUMNS = 26,
	PACTEX_CP_BLUR_H = 27,
	PACTEX_CP_BLUR_V = 28,
	PACTEX_CP_FILL_CIRCLE_AMAP = 102,
	PACTEX_CP_FILL_PIE_AMAP = 122,
	PACTEX_CP_TILE_CG = 129,
};
// Limit of what is built (xsystem4's; the original has none). The game's
// largest surface is 2048x1024. PARTS_CP_PIE_MAX_RADIUS limits the sectors.
#define PACTEX_CP_MAX_SIZE 8192

/* [pos, pos + size) as 0x5abed0 clips it: a negative size extends the other
 * way, then both ends are clamped to [0, limit]. The original adds in 32
 * bits (0x5abef3) and wraps around; a sum outside that range is not
 * reproduced here and fails the check, so the state is not built. */
static bool pactex_clip_span(int *pos, int *size, int limit)
{
	int64_t a = *pos, b = a + *size;
	if (b < INT32_MIN || b > INT32_MAX)
		return false;
	if (a > b) {
		int64_t t = a;
		a = b;
		b = t;
	}
	a = min(max(a, 0), limit);
	b = min(max(b, 0), limit);
	*pos = a;
	*size = b - a;
	return true;
}

static bool pactex_step_int(struct ex_tree *step, const char *sjis, const char *gbk, int *out)
{
	struct ex_value *v = pactex_leaf_value(step, sjis, gbk);
	if (!v || v->type != EX_INT)
		return false;
	*out = v->i;
	return true;
}

static bool pactex_step_item(struct ex_tree *step, const char *sjis, const char *gbk,
		unsigned index, int *out)
{
	struct ex_value *v = pactex_leaf_value(step, sjis, gbk);
	if (!v || v->type != EX_LIST || !v->list || index >= v->list->nr_items
			|| v->list->items[index].value.type != EX_INT)
		return false;
	*out = v->list->items[index].value.i;
	return true;
}

static bool pactex_byte(int v)
{
	return v >= 0 && v <= 255;
}

/* Reads one step and checks it against the surface (w x h) the steps before
 * it leave. False for anything the build below would not reproduce. */
static bool pactex_construction_step(struct ex_tree *node, struct pactex_cp_step *s,
		bool first, int surface_w, int surface_h)
{
	static const char dest_sjis[] = "\x90\xe6\x8b\xe9\x8c\x60";	/* 先矩形 */
	static const char dest_gbk[] = "\xcf\xc8\xbe\xd8\xd0\xce";
	static const char color_sjis[] = "\x90\x46\x82\x50";		/* 色１ */
	static const char color_gbk[] = "\xc9\xab\xa3\xb1";
	int full;

	if (node->is_leaf || !pactex_step_int(node, "\x83\x52\x83\x7d\x83\x93\x83\x68",
			"\xa5\xb3\xa5\xde\xa5\xf3\xa5\xc9", &s->command)) /* コマンド */
		return false;
	// The first step makes the surface: command 0 creates it, command 2
	// loads it from a CG. A step that makes none leaves none for the rest;
	// one that replaces the surface does not occur in the game.
	if (first != (s->command == PACTEX_CP_CREATE || s->command == PACTEX_CP_CREATE_CG))
		return false;
	// The states that start with a CG, and the commands only they have,
	// are built for v14 alone.
	if (ain->version < 14 && (s->command == PACTEX_CP_CREATE_CG || s->command == PACTEX_CP_BLEND_FILL
			|| s->command == PACTEX_CP_GRAY_SCALE || s->command == PACTEX_CP_BLUR_H
			|| s->command == PACTEX_CP_BLUR_V))
		return false;
	if (!pactex_step_item(node, dest_sjis, dest_gbk, 0, &s->x)
			|| !pactex_step_item(node, dest_sjis, dest_gbk, 1, &s->y)
			|| !pactex_step_item(node, dest_sjis, dest_gbk, 4, &s->w)
			|| !pactex_step_item(node, dest_sjis, dest_gbk, 5, &s->h))
		return false;

	switch (s->command) {
	case PACTEX_CP_CREATE:
		// 0x4fbd30 -> 0x5a94d0 fails unless both are positive.
		return s->w > 0 && s->h > 0 && s->w <= PACTEX_CP_MAX_SIZE && s->h <= PACTEX_CP_MAX_SIZE;
	case PACTEX_CP_CREATE_CG: {
		// 0x4fbf40 -> 0x50ac80: the surface is ＣＧ名's CG, of its size
		// (先矩形 is not read). A CG that cannot be loaded fails the step
		// and leaves no surface.
		int cg_no;
		struct cg_metrics metrics;
		s->cg_name = pactex_get_string(node, SJIS_CG_MEI);
		if (!s->cg_name)
			s->cg_name = pactex_get_string(node, GBK_CG_MEI);
		if (!s->cg_name || !asset_exists_by_name(ASSET_CG, s->cg_name, &cg_no)
				|| !asset_cg_get_metrics(cg_no, &metrics))
			return false;
		s->x = 0;
		s->y = 0;
		s->w = metrics.w;
		s->h = metrics.h;
		return s->w > 0 && s->h > 0 && s->w <= PACTEX_CP_MAX_SIZE && s->h <= PACTEX_CP_MAX_SIZE;
	}
	case PACTEX_CP_GRAY_SCALE:
	case PACTEX_CP_BLUR_H:
	case PACTEX_CP_BLUR_V: {
		// 0x4fe870 (gray scale), 0x4ffa60 / 0x4ffba0 (a box blur of ブラー
		// along the rows / the columns): over the rectangle, clipped as
		// the fills' is, or with 全體 the whole surface.
		if (!pactex_step_int(node, "\x91\x53\x91\xcc", "\xc8\xab\xf3\x77", &full)) /* 全体 / 全體 */
			return false;
		if (s->command != PACTEX_CP_GRAY_SCALE && !pactex_step_int(node, "\x83\x75\x83\x89\x81\x5b",
				"\xa5\xd6\xa5\xe9\xa9\x60", &s->blur)) /* ブラー */
			return false;
		s->full = full == 1;
		if (s->full) {
			s->x = 0;
			s->y = 0;
			s->w = surface_w;
			s->h = surface_h;
		}
		if (!pactex_clip_span(&s->x, &s->w, surface_w) || !pactex_clip_span(&s->y, &s->h, surface_h)
				|| s->w <= 0 || s->h <= 0)
			return false;
		if (s->command == PACTEX_CP_GRAY_SCALE)
			return true;
		// The radius is clamped to the line (0x49379f): a line of one
		// pixel, or of two with a radius below 1, is read beyond its end
		// by the original; not built.
		const int n = s->command == PACTEX_CP_BLUR_V ? s->h : s->w;
		return n >= 3 || (n == 2 && s->blur >= 1);
	}
	case PACTEX_CP_FILL:
	case PACTEX_CP_BLEND_FILL:
	case PACTEX_CP_FILL_AMAP:
	case PACTEX_CP_FILL_WITH_ALPHA: {
		for (int i = 0; i < 4; i++) {
			if (!pactex_step_item(node, color_sjis, color_gbk, i, &s->color[i]))
				return false;
		}
		if (!pactex_step_int(node, "\x91\x53\x91\xcc", "\xc8\xab\xf3\x77", &full)) /* 全体 / 全體 */
			return false;
		// The original clamps what commands 3 and 6 write to 0..255
		// (0x5ac2a4, 0x5ac764), and command 4's colour (0x5ac581), whose
		// alpha indexes a table as it is (0x4922be); it stores the low
		// byte for command 5 (0x492286). A state with a value outside
		// 0..255 (none in the game) is not built.
		if (s->command != PACTEX_CP_FILL_AMAP && !(pactex_byte(s->color[0])
				&& pactex_byte(s->color[1]) && pactex_byte(s->color[2])))
			return false;
		if (s->command != PACTEX_CP_FILL && !pactex_byte(s->color[3]))
			return false;
		// 0x4fc040, 0x4fc190, 0x4fc2f0, 0x4fc490: 全體 == 1 (0x4f8f76)
		// selects the whole surface.
		// 0x5abed0 normalizes the rectangle (a negative size extends the
		// other way) and clips it to the surface; 0x5abde0 fails the step,
		// and with it every later one (0x4fb675), when nothing is left.
		s->full = full == 1;
		if (s->full) {
			s->x = 0;
			s->y = 0;
			s->w = surface_w;
			s->h = surface_h;
		}
		return pactex_clip_span(&s->x, &s->w, surface_w)
			&& pactex_clip_span(&s->y, &s->h, surface_h)
			&& s->w > 0 && s->h > 0;
	}
	case PACTEX_CP_MUL_AMAP_GRADATION_ROWS:
	case PACTEX_CP_MUL_AMAP_GRADATION_COLUMNS:
		// 0x4ff740, 0x4ff8d0: the alpha goes from 色１'s to 色２'s over
		// the rectangle's rows or columns. The original clips line by line
		// and fails at the first one outside the surface (0x4ff99f), with
		// part of the gradation drawn; only a rectangle that lies within
		// the surface is built, and values the multiplication (0x5aca3c)
		// takes whole.
		if (!pactex_step_item(node, color_sjis, color_gbk, 3, &s->color[3])
				|| !pactex_step_item(node, "\x90\x46\x82\x51", "\xc9\xab\xa3\xb2", 3, &s->alpha2) /* 色２ */
				|| !pactex_step_int(node, "\x91\x53\x91\xcc", "\xc8\xab\xf3\x77", &full)) /* 全体 / 全體 */
			return false;
		s->full = full == 1;
		if (s->full) {
			s->x = 0;
			s->y = 0;
			s->w = surface_w;
			s->h = surface_h;
		}
		// No line to go over (no row for 25, no column for 26): the
		// original's loop does not run and the command succeeds (0x4ff7df
		// -> 0x4ff812, 0x4ff973 -> 0x4ff9ac), so the steps after it are
		// done; the operation draws nothing here either.
		if ((s->command == PACTEX_CP_MUL_AMAP_GRADATION_COLUMNS ? s->w : s->h) <= 0)
			return true;
		return pactex_byte(s->color[3]) && pactex_byte(s->alpha2)
			&& s->x >= 0 && s->y >= 0 && s->w > 0 && s->h > 0
			&& s->w <= surface_w - s->x && s->h <= surface_h - s->y;
	case PACTEX_CP_FILL_CIRCLE_AMAP:
		// 0x507640: the alpha of a disc of 半徑[0] around 先矩形 X,Y.
		if (!pactex_step_item(node, color_sjis, color_gbk, 3, &s->color[3])
				|| !pactex_step_item(node, "\x94\xbc\x8c\x61", "\xb0\xeb\x8f\xbd", 0, &s->rx)) /* 半径 / 半徑 */
			return false;
		return s->rx <= PARTS_CP_CIRCLE_MAX_RADIUS && pactex_byte(s->color[3]);
	case PACTEX_CP_TILE_CG: {
		// 0x50a220: ＣＧ名 tiled over the rectangle. A CG that cannot be
		// loaded fails the step; a rectangle that is not within the
		// surface is not built (what 0x5acd70 does with it was not read).
		int cg_no;
		s->cg_name = pactex_get_string(node, SJIS_CG_MEI);
		if (!s->cg_name)
			s->cg_name = pactex_get_string(node, GBK_CG_MEI);
		if (!s->cg_name || !asset_exists_by_name(ASSET_CG, s->cg_name, &cg_no)
				|| !pactex_step_int(node, "\x91\x53\x91\xcc", "\xc8\xab\xf3\x77", &full)) /* 全体 / 全體 */
			return false;
		s->full = full == 1;
		return s->full || (s->x >= 0 && s->y >= 0 && s->w > 0 && s->h > 0
			&& s->w <= surface_w - s->x && s->h <= surface_h - s->y);
	}
	case PACTEX_CP_FILL_PIE_AMAP:
		if (!pactex_step_item(node, color_sjis, color_gbk, 3, &s->color[3])
				|| !pactex_step_item(node, "\x94\xbc\x8c\x61", "\xb0\xeb\x8f\xbd", 0, &s->rx) /* 半径 / 半徑 */
				|| !pactex_step_item(node, "\x94\xbc\x8c\x61", "\xb0\xeb\x8f\xbd", 1, &s->ry)
				|| !pactex_step_item(node, "\x89\x7e\x8c\xca\x8a\x70\x93\x78",
					"\x83\xd2\xbb\xa1\xbd\xc7\xb6\xc8", 0, &s->start) /* 円弧角度 */
				|| !pactex_step_item(node, "\x89\x7e\x8c\xca\x8a\x70\x93\x78",
					"\x83\xd2\xbb\xa1\xbd\xc7\xb6\xc8", 1, &s->sweep)
				|| !pactex_step_int(node, "\x89\xf1\x93\x5d\x8a\x70\x93\x78",
					"\xd0\xfd\xde\x44\xbd\xc7\xb6\xc8", &s->rotate)) /* 回転角度 / 旋轉角度 */
			return false;
		// Only what parts_build_construction_process reproduces: no
		// rotation, and sector edges on the axes (within one turn), where
		// the truncated angle of a pixel does not depend on the last bit
		// of atan2.
		return !s->rotate && s->rx <= PARTS_CP_PIE_MAX_RADIUS && s->ry <= PARTS_CP_PIE_MAX_RADIUS
			&& s->start >= -360 && s->start <= 360 && s->sweep >= -360 && s->sweep <= 360
			&& s->start % 90 == 0 && s->sweep % 90 == 0 && pactex_byte(s->color[3]);
	default:
		return false;
	}
}

/* The steps of a construction state, when the loader can run them as the
 * original does: every command is one of 0 (create), 2 (the surface from a
 * CG), 3, 4, 5, 6 (fills), 15 (gray scale), 25, 26 (alpha gradations), 27, 28
 * (blurs), 102 (disc alpha), 122 (sector alpha) and 129 (CG tiles), the
 * first step makes the surface and none would fail.
 * A list whose first step is command 2 is narrower: every step must be one
 * of 2, 3, 4, 15, 27 and 28, and a list with any other step is not built
 * (the state keeps the CG it had). A CG's texture does not say whether its
 * source had an alpha channel, and natively command 5 skips a surface
 * without one (0x4fc378); 6, 25, 26, 102, 122 and 129 after a CG were not
 * checked against the original and are left out with it.
 * The original also builds the other states and keeps what the steps before
 * a failed one drew; those stay unbuilt here. サーフェイスエリア, which the
 * original reads after building, is (0, 0, 0, 0) in every state of the game;
 * a state with another one stays unbuilt as well.
 * The caller frees the result; NULL when the state is not built. */
static struct pactex_cp_step *pactex_construction_plan(struct ex_tree *state, int *nr_steps)
{
	struct ex_tree *list = pactex_construction_steps(state);
	if (!list || !list->nr_children)
		return NULL;
	struct ex_list *area = pactex_get_list(state, SJIS_SURFACE_AREA);
	if (!area) area = pactex_get_list(state, "\xa5\xb5\xa9\x60\xa5\xd5\xa5\xa7\xa5\xa4\xa5\xb9\xa5\xa8\xa5\xea\xa5\xa2");
	for (unsigned i = 0; area && i < area->nr_items; i++) {
		if (area->items[i].value.type != EX_INT || area->items[i].value.i)
			return NULL;
	}

	struct pactex_cp_step *steps = xcalloc(list->nr_children, sizeof(struct pactex_cp_step));
	for (unsigned i = 0; i < list->nr_children; i++) {
		if (!pactex_construction_step(&list->children[i], &steps[i], i == 0, steps[0].w, steps[0].h)) {
			free(steps);
			return NULL;
		}
		if (steps[0].command == PACTEX_CP_CREATE_CG) {
			/* CG textures do not preserve whether the source had an alpha
			 * channel. Native FillAMap skips RGB surfaces (0x4fc378), so
			 * keep the legacy CG for plans whose channel semantics we
			 * cannot reproduce. Reject the whole plan before building it. */
			switch (steps[i].command) {
			case PACTEX_CP_CREATE_CG:
			case PACTEX_CP_FILL:
			case PACTEX_CP_BLEND_FILL:
			case PACTEX_CP_GRAY_SCALE:
			case PACTEX_CP_BLUR_H:
			case PACTEX_CP_BLUR_V:
				break;
			default:
				free(steps);
				return NULL;
			}
		}
	}
	*nr_steps = list->nr_children;
	return steps;
}

static bool pactex_construction_build(struct ex_tree *state, int parts_no, int pe_state)
{
	int nr_steps;
	struct pactex_cp_step *steps = pactex_construction_plan(state, &nr_steps);
	if (!steps)
		return false;

	PE_ClearPartsConstructionProcess(parts_no, pe_state);
	for (int i = 0; i < nr_steps; i++) {
		struct pactex_cp_step *s = &steps[i];
		switch (s->command) {
		case PACTEX_CP_CREATE:
			PE_AddCreateToPartsConstructionProcess(parts_no, s->w, s->h, pe_state);
			break;
		case PACTEX_CP_CREATE_CG: {
			struct string *name = cstr_to_string(s->cg_name);
			PE_AddCreateCGToProcess(parts_no, name, pe_state);
			free_string(name);
			break;
		}
		case PACTEX_CP_FILL:
			PE_AddFillToPartsConstructionProcess(parts_no, s->x, s->y, s->w, s->h,
				s->color[0], s->color[1], s->color[2], pe_state);
			break;
		case PACTEX_CP_BLEND_FILL:
			PE_AddBlendFillToPartsConstructionProcess(parts_no, s->x, s->y, s->w, s->h, s->full,
				s->color[0], s->color[1], s->color[2], s->color[3], pe_state);
			break;
		case PACTEX_CP_GRAY_SCALE:
			PE_AddGrayScaleToPartsConstructionProcess(parts_no, s->x, s->y, s->w, s->h, s->full,
				pe_state);
			break;
		case PACTEX_CP_BLUR_H:
		case PACTEX_CP_BLUR_V:
			PE_AddBlurToPartsConstructionProcess(parts_no, s->command == PACTEX_CP_BLUR_V,
				s->x, s->y, s->w, s->h, s->full, s->blur, pe_state);
			break;
		case PACTEX_CP_FILL_AMAP:
			PE_AddFillAMapToPartsConstructionProcess(parts_no, s->x, s->y, s->w, s->h,
				s->color[3], pe_state);
			break;
		case PACTEX_CP_FILL_WITH_ALPHA:
			PE_AddFillWithAlphaToPartsConstructionProcess(parts_no, s->x, s->y, s->w, s->h,
				s->color[0], s->color[1], s->color[2], s->color[3], pe_state);
			break;
		case PACTEX_CP_FILL_PIE_AMAP:
			PE_AddFillPieAMapToPartsConstructionProcess(parts_no, s->x, s->y, s->rx, s->ry,
				s->start, s->sweep, s->color[3], s->rotate, pe_state);
			break;
		case PACTEX_CP_MUL_AMAP_GRADATION_ROWS:
		case PACTEX_CP_MUL_AMAP_GRADATION_COLUMNS:
			PE_AddMulAMapGradationToPartsConstructionProcess(parts_no,
				s->command == PACTEX_CP_MUL_AMAP_GRADATION_COLUMNS, s->x, s->y, s->w, s->h,
				false, s->color[3], s->alpha2, pe_state);
			break;
		case PACTEX_CP_FILL_CIRCLE_AMAP:
			PE_AddFillCircleToPartsConstructionProcess(parts_no, false, s->x, s->y, s->rx,
				0, 0, 0, s->color[3], pe_state);
			break;
		case PACTEX_CP_TILE_CG: {
			struct string *name = cstr_to_string(s->cg_name);
			PE_AddTileCGToPartsConstructionProcess(parts_no, name, s->x, s->y, s->w, s->h,
				s->full, pe_state);
			free_string(name);
			break;
		}
		}
	}
	free(steps);
	return PE_BuildPartsConstructionProcess(parts_no, pe_state);
}

static bool pactex_low_level_keeps_legacy_cg(struct ex_tree *state)
{
	if (pactex_low_level_type(state) != PACTEX_EPT_CONSTRUCTION
			|| !pactex_construction_has_steps(state))
		return false;
	int nr_steps;
	struct pactex_cp_step *steps = pactex_construction_plan(state, &nr_steps);
	bool built = steps != NULL;
	free(steps);
	return !built;
}

/* The canvas the steps of a state the loader does not build start with: the
 * first step creates it (コマンド 0, or 1 without pixels; a missing key is
 * 0) with 先矩形's W,H (items 4 and 5). The original has built the surface
 * by then, so a layout box sizes the parts by this canvas (layoutbox.c),
 * e.g. RivalShopView's three 20x20 personality marks. */
static void pactex_construction_canvas(struct ex_tree *state, struct parts *parts)
{
	for (unsigned i = 0; i < state->nr_children; i++) {
		struct ex_tree *list = &state->children[i];
		if (list->is_leaf || !pactex_name_is(list, "\x8e\xe8\x8f\x87\x83\x8a\x83\x58\x83\x67",
				"\xca\xd6\xed\x98\xa5\xea\xa5\xb9\xa5\xc8")) /* 手順リスト */
			continue;
		for (unsigned j = 0; j < list->nr_children; j++) {
			struct ex_tree *step = &list->children[j];
			if (step->is_leaf)
				continue;
			int command = pactex_value_number(pactex_leaf_value(step,
				"\x83\x52\x83\x7d\x83\x93\x83\x68", "\xa5\xb3\xa5\xde\xa5\xf3\xa5\xc9"), 0); /* コマンド */
			struct ex_value *dest = pactex_leaf_value(step,
				"\x90\xe6\x8b\xe9\x8c\x60", "\xcf\xc8\xbe\xd8\xd0\xce"); /* 先矩形 */
			if ((command == 0 || command == 1) && dest && dest->type == EX_LIST
					&& dest->list && dest->list->nr_items >= 6) {
				int w = pactex_value_number(&dest->list->items[4].value, 0);
				int h = pactex_value_number(&dest->list->items[5].value, 0);
				if (w > 0 && h > 0) {
					parts->pactex_canvas_w = w;
					parts->pactex_canvas_h = h;
				}
			}
			return;
		}
		return;
	}
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

static const char *pactex_get_exact_string(struct ex_tree *node, const char *name);

/* 數字部件: the native loader (0x5b66f0) reads 表示タイプ and then what that
 * type is made from: 0 ＣＧ名 (0x5b49f0); 1 ＣＧ名 and 幅リスト (0x5b4ac0);
 * 2 the font, フォントタイプ/サイズ/色/太さ/縁取り/縁取り色 (0x5b4b90), and
 * 全角. Then, for every type, サーフェイスエリア (by the caller here), 數值
 * (the number the numeral shows until the script sets one), コンマ表示,
 * 字間隔, 桁數 and ゼロパディング. A missing key is 0. */
static void pactex_apply_numeral_state(struct ex_tree *state, int parts_no, int pe_state)
{
	int show_type = pactex_get_int(state, "\x95\x5c\x8e\xa6\x83\x5e\x83\x43\x83\x76", /* 表示タイプ */
		pactex_get_int(state, "\xb1\xed\xca\xbe\xa5\xbf\xa5\xa4\xa5\xd7", 0));
	const char *name = pactex_get_exact_string(state, "\x82\x62\x82\x66\x96\xbc"); /* ＣＧ名 */
	if (!name) name = pactex_get_exact_string(state, "\xa3\xc3\xa3\xc7\xc3\xfb");
	struct string *cg = cstr_to_string(name ? name : "");
	if (show_type == 0) {
		PE_SetNumeralCG(parts_no, cg, pe_state);
	} else if (show_type == 1) {
		struct ex_list *l = pactex_get_list(state, "\x95\x9d\x83\x8a\x83\x58\x83\x67"); /* 幅リスト */
		if (!l) l = pactex_get_list(state, "\xb7\xf9\xa5\xea\xa5\xb9\xa5\xc8");
		int w[12] = {0};
		for (unsigned i = 0; l && i < l->nr_items && i < 12; i++)
			w[i] = pactex_value_number(&l->items[i].value, 0);
		PE_SetNumeralLinkedCGNumberWidthWidthList(parts_no, cg, w[0], w[1], w[2], w[3],
				w[4], w[5], w[6], w[7], w[8], w[9], w[10], w[11], pe_state);
	} else if (show_type == 2) {
		PE_SetNumeralFont(parts_no,
			pactex_message_number(state, PACTEX_MW_FACE, 0),
			pactex_message_number(state, PACTEX_MW_SIZE, 0),
			pactex_message_item(state, PACTEX_MW_COLOR, 0, 0),
			pactex_message_item(state, PACTEX_MW_COLOR, 1, 0),
			pactex_message_item(state, PACTEX_MW_COLOR, 2, 0),
			pactex_message_number(state, PACTEX_MW_WEIGHT, 0),
			pactex_message_item(state, PACTEX_MW_EDGE_COLOR, 0, 0),
			pactex_message_item(state, PACTEX_MW_EDGE_COLOR, 1, 0),
			pactex_message_item(state, PACTEX_MW_EDGE_COLOR, 2, 0),
			pactex_message_number(state, PACTEX_MW_EDGE, 0), pe_state);
		PE_SetNumeralFullPitch(parts_no, pactex_get_int(state, "\x91\x53\x8a\x70", /* 全角 */
			pactex_get_int(state, "\xc8\xab\xbd\xc7", 0)) == 1, pe_state);
	} else {
		// No type of the original's: a numeral that draws nothing.
		parts_get_numeral(parts_get(parts_no), pe_state - 1);
	}
	free_string(cg);
	PE_SetNumeralNumber(parts_no, pactex_get_int(state, "\x90\x94\x92\x6c", /* 数値 */
		pactex_get_int(state, "\x94\xb5\xd6\xb5", 0)), pe_state); /* 數值 */
	PE_SetNumeralShowComma(parts_no, pactex_get_int(state, "\x83\x52\x83\x93\x83\x7d\x95\x5c\x8e\xa6", /* コンマ表示 */
		pactex_get_int(state, "\xa5\xb3\xa5\xf3\xa5\xde\xb1\xed\xca\xbe", 0)) == 1, pe_state);
	PE_SetNumeralSpace(parts_no, pactex_get_int(state, "\x8e\x9a\x8a\xd4\x8a\x75", /* 字間隔 */
		pactex_get_int(state, "\xd7\xd6\xe9\x67\xb8\xf4", 0)), pe_state);
	PE_SetNumeralLength(parts_no, pactex_get_int(state, "\x8c\x85\x90\x94", /* 桁数 */
		pactex_get_int(state, "\xe8\xec\x94\xb5", 0)), pe_state); /* 桁數 */
	PE_SetNumeralShowPadding(parts_no, pactex_get_int(state, "\x83\x5b\x83\x8d\x83\x70\x83\x66\x83\x42\x83\x93\x83\x4f", /* ゼロパディング */
		pactex_get_int(state, "\xa5\xbc\xa5\xed\xa5\xd1\xa5\xc7\xa5\xa3\xa5\xf3\xa5\xb0", 0)) == 1, pe_state);
}

static float pactex_gauge_number(struct ex_tree *node, const char *sjis, const char *gbk)
{
	for (unsigned i = 0; i < node->nr_children; i++) {
		struct ex_tree *c = &node->children[i];
		if (c->is_leaf && pactex_name_is(c, sjis, gbk) && c->leaf.value.type == EX_FLOAT)
			return c->leaf.value.f;
	}
	return 0;
}

/* Native loaders 0x5a8660 / 0x5c4500: gauge-specific leaves belong to
 * this named state. CG fallback would both lose type and reset the gauge. */
static void pactex_apply_gauge_state(struct ex_tree *state, int parts_no, int pe_state, bool vertical)
{
	struct parts *p = parts_get(parts_no);
	struct parts_gauge *g = vertical ? parts_get_vgauge(p, pe_state - 1)
		: parts_get_hgauge(p, pe_state - 1);
	const char *name = pactex_get_exact_string(state, "\x82\x62\x82\x66\x96\xbc");
	if (!name) name = pactex_get_exact_string(state, "\xa3\xc3\xa3\xc7\xc3\xfb");
	if (name && *name) {
		struct string *cg = cstr_to_string(name);
		parts_gauge_set_cg(p, g, cg);
		free_string(cg);
	}
	float n = pactex_gauge_number(state, "\x95\xaa\x8e\x71", "\xb7\xd6\xd7\xd3");
	float d = pactex_gauge_number(state, "\x95\xaa\x95\xea", "\xb7\xd6\xc4\xb8");
	if (vertical) PE_SetVGaugeRate(parts_no, n, d, pe_state);
	else PE_SetHGaugeRate(parts_no, n, d, pe_state);
	g->reverse = pactex_get_int(state, "\x94\xbd\x93\x5d",
		pactex_get_int(state, "\xb7\xb4\xde\x44", 0)) != 0;
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
		} else if (!pactex_construction_build(state, parts_no, pe_state) && pe_state == 1) {
			pactex_construction_canvas(state, parts);
		}
		parts->component_state_type[pe_state - 1] = type;
		return true;
	}
	bool text = type == PACTEX_EPT_TEXT;
	bool numeral = type == PACTEX_EPT_NUMERAL;
	bool gauge = type == PACTEX_EPT_HGAUGE || type == PACTEX_EPT_VGAUGE;
	struct parts *parts = parts_get(parts_no);
	if (text) {
		parts_get_text(parts, pe_state - 1);
		pactex_apply_text_style(state, parts_no, pe_state);
		// After the style, so the text is laid out with it.
		pactex_apply_default_text(state, parts_no, pe_state);
	} else if (numeral) {
		pactex_apply_numeral_state(state, parts_no, pe_state);
	} else if (gauge) {
		pactex_apply_gauge_state(state, parts_no, pe_state, type == PACTEX_EPT_VGAUGE);
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
	} else if (text || numeral || gauge || !pactex_get_surface_area(state, &x, &y, &w, &h, 0)) {
		return true;
	}
	if (text) PE_SetPartsTextSurfaceArea(parts_no, x, y, w, h, pe_state);
	else if (numeral) PE_SetNumeralSurfaceArea(parts_no, x, y, w, h, pe_state);
	else if (type == PACTEX_EPT_HGAUGE) PE_SetHGaugeSurfaceArea(parts_no, x, y, w, h, pe_state);
	else if (type == PACTEX_EPT_VGAUGE) PE_SetVGaugeSurfaceArea(parts_no, x, y, w, h, pe_state);
	else PE_SetPartsCGSurfaceArea(parts_no, x, y, w, h, pe_state);
	return true;
}

/* Apply pactex properties (position, show, alpha, CG) to a parts entry.
 * Extracts standard properties from leaf children, and CG names from
 * the type-specific info branch (種類別情報). */
/* Alpha clippers seen while loading one activity, resolved by name at the end
 * of pactex_load. */
#define PACTEX_MAX_CLIPPERS 64
static struct { int parts_no; char name[256]; } pactex_clippers[PACTEX_MAX_CLIPPERS];
static int pactex_nr_clippers;

static void pactex_resolve_clippers(struct activity *act)
{
	for (int i = 0; i < pactex_nr_clippers; i++) {
		int no = -1;
		for (int j = 0; j < act->nr_parts && no < 0; j++) {
			if (act->parts[j].name[0] && !strcmp(act->parts[j].name, pactex_clippers[i].name))
				no = act->parts[j].number;
		}
		if (no > 0)
			PE_SetPartsAlphaClipperPartsNumber(pactex_clippers[i].parts_no, no);
		else
			WARNING("pactex: alpha clipper '%s' not found", display_game0(pactex_clippers[i].name));
	}
	pactex_nr_clippers = 0;
}

/* The size links of the scroll bars seen while loading one activity (see the
 * 豎滾動條 in pactex_apply_properties), resolved by name at the end of
 * pactex_load as the alpha clippers are. */
#define PACTEX_MAX_SCROLL_LINKS 16
static struct { int parts_no; bool view; char name[256]; } pactex_scroll_links[PACTEX_MAX_SCROLL_LINKS];
static int pactex_nr_scroll_links;

static void pactex_add_scroll_link(int parts_no, bool view, const struct ex_value *name)
{
	if (!name || name->type != EX_STRING || !name->s || !name->s->text[0])
		return;
	if (pactex_nr_scroll_links >= PACTEX_MAX_SCROLL_LINKS)
		return;
	pactex_scroll_links[pactex_nr_scroll_links].parts_no = parts_no;
	pactex_scroll_links[pactex_nr_scroll_links].view = view;
	snprintf(pactex_scroll_links[pactex_nr_scroll_links].name,
		sizeof(pactex_scroll_links[0].name), "%s", name->s->text);
	pactex_nr_scroll_links++;
}

static void pactex_resolve_scroll_links(struct activity *act)
{
	for (int i = 0; i < pactex_nr_scroll_links; i++) {
		int no = 0;
		for (int j = 0; j < act->nr_parts && no <= 0; j++) {
			if (act->parts[j].name[0] && !strcmp(act->parts[j].name, pactex_scroll_links[i].name))
				no = act->parts[j].number;
		}
		struct parts *bar = parts_try_get(pactex_scroll_links[i].parts_no);
		if (no <= 0 || !bar) {
			WARNING("pactex: scroll bar size link '%s' not found", display_game0(pactex_scroll_links[i].name));
			continue;
		}
		if (pactex_scroll_links[i].view)
			bar->scrollbar.view_link = no;
		else
			bar->scrollbar.total_link = no;
	}
	pactex_nr_scroll_links = 0;
}

static void pactex_apply_properties(struct ex_tree *node, int parts_no)
{
	pactex_apply_pixel_decide(node, parts_no);
	pactex_apply_click_permission(node, parts_no);
	pactex_apply_pass_cursor(node, parts_no);

	/* ClipArea is stored even when disabled. The pactex loader writes the
	 * fields directly; unlike SetComponentClipArea it does not auto-enable. */
	struct ex_list *clip = pactex_get_list(node, "\x83\x4e\x83\x8a\x83\x62\x83\x76\x97\xcc\x88\xe6");
	if (!clip) clip = pactex_get_list(node, "\xa5\xaf\xa5\xea\xa5\xc3\xa5\xd7\xee\x49\xd3\xf2");
	if (clip) {
		int v[5] = {0};
		for (unsigned i = 0; i < 5 && i < clip->nr_items; i++) {
			if (clip->items[i].value.type == EX_INT)
				v[i] = clip->items[i].value.i;
		}
		struct parts *parts = parts_get(parts_no);
		parts->clip_enabled = v[0] != 0;
		parts->clip_area = (Rectangle){ v[1], v[2], v[3], v[4] };
	}

	/* マージン = (top, bottom, left, right): the component parser (0x553650)
	 * reads the list's items 0-3 into +0x110..+0x11c (0x5542f9..0x55434e),
	 * the fields SetComponentMargin (0x58e240) writes; a missing list is 0,
	 * as for a new parts. A layout box adds them around the parts. */
	struct ex_list *margin = pactex_get_list(node, "\x83\x7d\x81\x5b\x83\x57\x83\x93");
	if (!margin) margin = pactex_get_list(node, "\xa5\xde\xa9\x60\xa5\xb8\xa5\xf3");
	if (margin) {
		int v[4] = {0};
		for (unsigned i = 0; i < 4 && i < margin->nr_items; i++)
			v[i] = pactex_value_number(&margin->items[i].value, 0);
		PE_SetComponentMargin(parts_no, v[0], v[1], v[2], v[3]);
	}


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

	/* アルファクリッパー names another parts of the same activity whose alpha
	 * masks this one (SceneLogo's gloss panels use the badge and the
	 * ALICESOFT text, so the gloss shows only inside the logo and not at all
	 * while the badge is scaled to 0). The name is resolved once the whole
	 * activity exists, at the end of pactex_load. */
	const char *clipper = pactex_get_string(node, SJIS_ALPHA_CLIPPER);
	if (!clipper) clipper = pactex_get_string(node, GBK_ALPHA_CLIPPER);
	if (clipper && clipper[0] && pactex_nr_clippers < PACTEX_MAX_CLIPPERS) {
		pactex_clippers[pactex_nr_clippers].parts_no = parts_no;
		snprintf(pactex_clippers[pactex_nr_clippers].name,
			sizeof(pactex_clippers[0].name), "%s", clipper);
		pactex_nr_clippers++;
	}

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

	/* Extract draw filter: 描画フィルタ = int (0 normal, 1 additive,
	 * 2 multiply, 3 screen). The CN files spell it 描畫フィルタ; without the
	 * GBK key SceneLogo's gloss (3) was drawn as a plain yellow band. */
	int draw_filter = pactex_get_int(node, SJIS_DRAW_FILTER, -1);
	if (draw_filter < 0) draw_filter = pactex_get_int(node, GBK_DRAW_FILTER, -1);
	if (draw_filter >= 0)
		PE_SetPartsDrawFilter(parts_no, draw_filter);

	/* Extract add color: 加算色 = list[3] = [r, g, b] */
	struct ex_list *add_col = pactex_get_list(node, SJIS_ADD_COLOR);
	if (!add_col) add_col = pactex_get_list(node, GBK_ADD_COLOR);
	if (add_col && add_col->nr_items >= 3)
		PE_SetAddColor(parts_no, add_col->items[0].value.i,
			add_col->items[1].value.i, add_col->items[2].value.i);

	/* Extract multiply color: 乗算色 = list[3] = [r, g, b] */
	struct ex_list *mul_col = pactex_get_list(node, SJIS_MUL_COLOR);
	if (!mul_col) mul_col = pactex_get_list(node, GBK_MUL_COLOR);
	if (mul_col && mul_col->nr_items >= 3)
		PE_SetMultiplyColor(parts_no, mul_col->items[0].value.i,
			mul_col->items[1].value.i, mul_col->items[2].value.i);

	/* 減算色モード / 減算色模式: the native parser keeps (value == 1)
	 * (0x553fdc -> 0x554020). MapNodeView's Base, Text and TextCaptionEn
	 * have it, for the yellow blink of a node that can be walked to. */
	int sub_color = pactex_get_int(node, "\x8c\xb8\x8e\x5a\x90\x46\x83\x82\x81\x5b\x83\x68", -1);
	if (sub_color < 0)
		sub_color = pactex_get_int(node, "\x9c\x70\xcb\xe3\xc9\xab\xc4\xa3\xca\xbd", 0);
	if (sub_color == 1)
		PE_SetSubColorMode(parts_no, true);

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

	/* --- 豎滾動條: what the bar scrolls. The native loader reads 全體スクロー
	 * ル量, 表示量, スクロール位置 and スクロールレート, a missing key being
	 * 0, into the widget as they are (0x544802, 0x544869, 0x5448d0,
	 * 0x544937 -> +0x110..+0x11c). BackLog's bar shows 16 lines (表示量), which
	 * is how many CBackLogView@SetLineIndex builds. The SJIS key of the first
	 * is a guess: no SJIS pactex was read. --- */
	if (parts_get(parts_no)->component_type == PARTS_COMPONENT_VSCROLLBAR) {
		struct parts_scrollbar *sb = &parts_get(parts_no)->scrollbar;
		sb->total = pactex_value_number(pactex_leaf_value(type_info,
				"\x91\x53\x91\xcc\x83\x58\x83\x4e\x83\x8d\x81\x5b\x83\x8b\x97\xca",
				"\xc8\xab\xf3\x77\xa5\xb9\xa5\xaf\xa5\xed\xa9\x60\xa5\xeb\xc1\xbf"), 0);
		sb->view = pactex_value_number(pactex_leaf_value(type_info,
				"\x95\x5c\x8e\xa6\x97\xca", "\xb1\xed\xca\xbe\xc1\xbf"), 0);
		sb->pos = pactex_value_number(pactex_leaf_value(type_info,
				"\x83\x58\x83\x4e\x83\x8d\x81\x5b\x83\x8b\x88\xca\x92\x75",
				"\xa5\xb9\xa5\xaf\xa5\xed\xa9\x60\xa5\xeb\xce\xbb\xd6\xc3"), 0);
		sb->rate = pactex_value_number(pactex_leaf_value(type_info,
				"\x83\x58\x83\x4e\x83\x8d\x81\x5b\x83\x8b\x83\x8c\x81\x5b\x83\x67",
				"\xa5\xb9\xa5\xaf\xa5\xed\xa9\x60\xa5\xeb\xa5\xec\xa9\x60\xa5\xc8"), 0);
		/* 全體スクロール量サイズ連動 and 表示量サイズ連動 name the parts
		 * whose size the two amounts follow (parts_scrollbar_total). The
		 * native loader reads the two strings, "" when missing, and has
		 * the name made a number at once (0x545278 and 0x5452da ->
		 * 0x4df410, a callback of the loader that was not read; +0x214,
		 * +0x218); here a name that is not a parts of this activity
		 * leaves the amount unlinked. SceneStandViewer's bar names its
		 * Image and ImageRect. The SJIS keys are guesses. */
		sb->total_link = sb->view_link = 0;
		pactex_add_scroll_link(parts_no, false, pactex_leaf_value(type_info,
				"\x91\x53\x91\xcc\x83\x58\x83\x4e\x83\x8d\x81\x5b\x83\x8b\x97\xca\x83\x54\x83\x43\x83\x59\x98\x41\x93\xae",
				"\xc8\xab\xf3\x77\xa5\xb9\xa5\xaf\xa5\xed\xa9\x60\xa5\xeb\xc1\xbf\xa5\xb5\xa5\xa4\xa5\xba\xdf\x42\x84\xd3"));
		pactex_add_scroll_link(parts_no, true, pactex_leaf_value(type_info,
				"\x95\x5c\x8e\xa6\x97\xca\x83\x54\x83\x43\x83\x59\x98\x41\x93\xae",
				"\xb1\xed\xca\xbe\xc1\xbf\xa5\xb5\xa5\xa4\xa5\xba\xdf\x42\x84\xd3"));
	}

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
		/* The widget's surface is this colour with its alpha (see
		 * parts_get_panel): SceneYesNoDialog's Dimmer, (0,0,0,128),
		 * darkens the scene and does not hide it. */
		parts_panel_init(parts_get(parts_no), pw, ph, cr, cg, cb, ca);
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

/* レイアウトボックス: the native loader (0x5493a0, called from 0x5393ef)
 * reads レイアウトタイプ, 折り返し許可 (== 1), 折り返しサイズ (a float),
 * 配置 and パディング (top, bottom, left, right) into the widget
 * (+0x44..+0x5c) and marks it for layout (+0x60). A missing key is the
 * getter's default 0, not the constructor's (vertical, 200, 配置 1), which
 * only a box created by the AIN keeps. 折り返しサイズをレートとして認識する
 * (read for pactex versions above 4) is not supported; every box in the
 * game has 0. The box lays out its children in layoutbox.c. */
static void pactex_apply_layout_box(struct ex_tree *type_info, struct parts *p)
{
	static const char *const keys[][2] = {
		{ "\x83\x8c\x83\x43\x83\x41\x83\x45\x83\x67\x83\x5e\x83\x43\x83\x76",
		  "\xa5\xec\xa5\xa4\xa5\xa2\xa5\xa6\xa5\xc8\xa5\xbf\xa5\xa4\xa5\xd7" }, /* レイアウトタイプ */
		{ "\x90\xdc\x82\xe8\x95\xd4\x82\xb5\x8b\x96\x89\xc2",
		  "\xd5\xdb\xa4\xea\xb7\xb5\xa4\xb7\xd4\x53\xbf\xc9" }, /* 折り返し許可 */
		{ "\x90\xdc\x82\xe8\x95\xd4\x82\xb5\x83\x54\x83\x43\x83\x59",
		  "\xd5\xdb\xa4\xea\xb7\xb5\xa4\xb7\xa5\xb5\xa5\xa4\xa5\xba" }, /* 折り返しサイズ */
		{ "\x90\xdc\x82\xe8\x95\xd4\x82\xb5\x83\x54\x83\x43\x83\x59\x82\xf0\x83\x8c\x81\x5b\x83\x67"
		  "\x82\xc6\x82\xb5\x82\xc4\x94\x46\x8e\xaf\x82\xb7\x82\xe9",
		  "\xd5\xdb\xa4\xea\xb7\xb5\xa4\xb7\xa5\xb5\xa5\xa4\xa5\xba\xa4\xf2\xa5\xec\xa9\x60\xa5\xc8"
		  "\xa4\xc8\xa4\xb7\xa4\xc6\xd5\x4a\xd7\x52\xa4\xb9\xa4\xeb" }, /* 折り返しサイズをレートとして認識する */
		{ "\x94\x7a\x92\x75", "\xc5\xe4\xd6\xc3" }, /* 配置 */
		{ "\x83\x70\x83\x66\x83\x42\x83\x93\x83\x4f", "\xa5\xd1\xa5\xc7\xa5\xa3\xa5\xf3\xa5\xb0" }, /* パディング */
	};
	struct ex_value *v[6];
	for (int i = 0; i < 6; i++)
		v[i] = pactex_leaf_value(type_info, keys[i][0], keys[i][1]);
	struct parts_layout_box *lb = parts_get_layout_box(p);
	lb->layout_type = (int)pactex_value_number(v[0], 0);
	PE_SetLayoutBoxReturnF(p->no, (int)pactex_value_number(v[1], 0) == 1, pactex_value_number(v[2], 0));
	if ((int)pactex_value_number(v[3], 0) == 1)
		WARNING("pactex: layout box wrap size as a rate is not supported");
	lb->align = (int)pactex_value_number(v[4], 0);
	int pad[4] = {0};
	struct ex_list *l = v[5] && v[5]->type == EX_LIST ? v[5]->list : NULL;
	for (unsigned i = 0; l && i < 4 && i < l->nr_items; i++)
		pad[i] = pactex_value_number(&l->items[i].value, 0);
	lb->padding_top = pad[0];
	lb->padding_bottom = pad[1];
	lb->padding_left = pad[2];
	lb->padding_right = pad[3];
	parts_component_dirty(p);
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
	if (type == PACTEX_EPT_LAYOUT_BOX)
		pactex_apply_layout_box(type_info, p);
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

	pactex_nr_clippers = 0;
	pactex_nr_scroll_links = 0;
	int root_no = alloc_activity_parts_no();
	struct parts *root = parts_get(root_no);
	{
		static unsigned traced;
		if (getenv("XSYS4_STAGE2_TRACE") && traced++ < 200)
			WARNING("S2 layer-root load: activity '%s' root %d on controller %d (active %d)",
				display_game0(act->name), root_no, root->controller_no, PE_get_active_controller());
	}

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
			display_game0(root_branch->name->text));
	}

	/* Apply properties to root component too */
	pactex_apply_properties(root_branch, root_no);

	pactex_resolve_clippers(act);
	pactex_resolve_scroll_links(act);
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
				display_game0(name->text), display_game1(fname));
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
	if (idx < 0) { WARNING("GetActivityPartsNumber: act='%s' NOT FOUND (looking for '%s')", display_game0(name->text), display_game1(parts_name->text)); return -1; }
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

	WARNING("GetActivityPartsNumber: act='%s' parts='%s' NOT FOUND", display_game0(name->text), display_game1(parts_name->text));
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
