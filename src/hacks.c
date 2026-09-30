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

#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include "system4.h"
#include "system4/ain.h"
#include "system4/buffer.h"
#include "system4/instructions.h"
#include "system4/string.h"
#include "system4/utfsjis.h"

#include "gfx/gfx.h"
#include "input.h"
#include "xsystem4.h"

#include "hll/hll.h"

bool game_daibanchou_en = false;
bool game_rance02_mg = false;
bool game_rance6_mg = false;
bool game_rance7_mg = false;
bool game_rance8 = false;
bool game_rance8_mg = false;
bool game_dungeons_and_dolls = false;
bool ain_is_gb18030 = false;

static bool gb_sjis_boundary_hit(const uint8_t *s)
{
	while (*s) {
		if (SJIS_2BYTE(*s) && s[1]) {
			s += 2;
			continue;
		}
		if (*s >= 0xA1 && *s <= 0xDF && s[1] >= 0x40)
			return true;
		s++;
	}
	return false;
}

// Something SJIS cannot contain: 80, A0, FD..FF, or a lead byte without a
// valid trail (40..7E, 80..FC).
static bool gb_sjis_invalid(const uint8_t *s, int n)
{
	for (int i = 0; i < n; ) {
		uint8_t b = s[i];
		if (b == 0x80 || b == 0xA0 || b >= 0xFD)
			return true;
		if (SJIS_2BYTE(b)) {
			if (i + 1 >= n)
				return true;
			uint8_t t = s[i+1];
			if (t < 0x40 || t == 0x7F || t > 0xFC)
				return true;
			i += 2;
			continue;
		}
		i++;
	}
	return false;
}

// Lead 81..FE with a trail in 40..FE except 7F; no other byte >= 0x80.
static bool gb_gbk_valid(const uint8_t *s, int n)
{
	for (int i = 0; i < n; ) {
		uint8_t b = s[i];
		if (b >= 0x81 && b <= 0xFE) {
			if (i + 1 >= n)
				return false;
			uint8_t t = s[i+1];
			if (t < 0x40 || t == 0x7F || t == 0xFF)
				return false;
			i += 2;
			continue;
		}
		if (b >= 0x80)
			return false;
		i++;
	}
	return true;
}

static bool gb_utf8_valid(const uint8_t *s, int n)
{
	for (int i = 0; i < n; ) {
		uint8_t b = s[i];
		int len;
		uint8_t lo = 0x80, hi = 0xBF;
		if (b < 0x80) {
			i++;
			continue;
		} else if (b >= 0xC2 && b <= 0xDF) {
			len = 2;
		} else if (b >= 0xE0 && b <= 0xEF) {
			len = 3;
			if (b == 0xE0) lo = 0xA0;
			if (b == 0xED) hi = 0x9F;
		} else if (b >= 0xF0 && b <= 0xF4) {
			len = 4;
			if (b == 0xF0) lo = 0x90;
			if (b == 0xF4) hi = 0x8F;
		} else {
			return false;
		}
		if (i + len > n || s[i+1] < lo || s[i+1] > hi)
			return false;
		for (int k = 2; k < len; k++)
			if ((s[i+k] & 0xC0) != 0x80)
				return false;
		i += len;
	}
	return true;
}

bool gb18030_detect_strings(struct string **strs, int n, struct gb18030_scores *out)
{
	struct gb18030_scores sc = { 0 };
	for (int i = 0; i < n && i < 100; i++) {
		if (!strs[i]) continue;
		// the historical check, unchanged
		const uint8_t *s = (const uint8_t *)strs[i]->text;
		for (; *s; s++) {
			if (*s >= 0xA1 && *s <= 0xDF && *(s+1) >= 0x40) {
				sc.legacy++;
				break;
			}
		}
		if (gb_sjis_boundary_hit((const uint8_t *)strs[i]->text))
			sc.boundary++;
	}
	for (int i = 0; i < n; i++) {
		if (!strs[i]) continue;
		const uint8_t *s = (const uint8_t *)strs[i]->text;
		int len = strs[i]->size;
		bool high = false;
		for (int k = 0; k < len && !high; k++)
			high = s[k] >= 0x80;
		if (!high)
			continue;
		sc.nonascii++;
		if (gb_utf8_valid(s, len))
			sc.utf8_valid++;
		if (gb_sjis_invalid(s, len) && gb_gbk_valid(s, len))
			sc.sjis_invalid++;
	}
	if (out)
		*out = sc;
	return sc.boundary > 5 && sc.sjis_invalid >= 16 && sc.utf8_valid * 2 < sc.nonascii;
}

void gbk_string_rules_enable(void)
{
	ain_is_gb18030 = true;
	sys4_set_string_charset(SYS4_CHARSET_GBK);
}

static void write_instruction0(struct buffer *out, enum opcode op)
{
	buffer_write_int16(out, op);
}

static void write_instruction1(struct buffer *out, enum opcode op, int32_t arg)
{
	buffer_write_int16(out, op);
	buffer_write_int32(out, arg);
}

void set_msgskip_delay(struct ain *ain, unsigned ms)
{
	int orig_fno = ain_get_function(ain, "A");
	if (orig_fno <= 0) {
		WARNING("No 'A' function when applying msgskip delay");
		return;
	}

	int new_fno = ain_dup_function(ain, orig_fno);
	struct ain_function *override_f = &ain->functions[orig_fno];

	// override void A(void) {
	//     super();
	//     if (key_is_down(VK_CONTROL))
	//         System.sleep(ms);
	// }
	struct buffer out;
	buffer_init(&out, NULL, 0);
	write_instruction1(&out, FUNC, orig_fno);
	write_instruction1(&out, CALLFUNC, new_fno);
	write_instruction1(&out, PUSH, VK_CONTROL);
	write_instruction1(&out, CALLSYS, VM_XSYS_KEY_IS_DOWN);
	uint32_t ifz_addr = out.index;
	write_instruction1(&out, IFZ, 0);
	write_instruction1(&out, PUSH, ms);
	write_instruction1(&out, CALLSYS, SYS_SLEEP);
	buffer_write_int32_at(&out, ifz_addr + 2, ain->code_size + out.index);
	write_instruction0(&out, RETURN);
	write_instruction1(&out, ENDFUNC, orig_fno);

	override_f->address = ain->code_size + 6;
	ain->code = xrealloc(ain->code, ain->code_size + out.index);
	memcpy(ain->code + ain->code_size, out.buf, out.index);
	ain->code_size += out.index;
}

// Rance 02 (MangaGamer version)
// -----------------------------
// Uses a custom proportional font implementation which makes assumptions
// about the font. We replace the _CalculateWidth function with a font-generic
// implementation.
//
// '^' character is mapped to 'é' (Rosé)
// '}' character is mapped to double-width dash
static void apply_rance02_hacks(struct ain *ain)
{
	int fno = ain_get_function(ain, "_CalculateWidth");
	if (fno <= 0) {
		WARNING("No '_CalculateWidth' function when applying Rance 02 compatibility hack");
		return;
	}
	struct ain_function *f = &ain->functions[fno];

	// Reduce the text scale if it wasn't set by the user
	if (!config.manual_text_x_scale) {
		config.text_x_scale = 0.9375;
	}

	// rewrite function
	struct buffer out;
	buffer_init(&out, ain->code, ain->code_size);
	buffer_seek(&out, f->address);

	// _CalculateWidth(ref string str, int nFontSize, int nWeightSize) {
	//     if (str[0] > 255 || str[0] == '}')
	//         return nFontSize + nWeightSize;
	//     return nFontSize / 2 + nWeightSize;
	// }

	// if (str[0] > 255 || str[0] == '}')
	write_instruction1(&out, SH_LOCALREF, 0);
	write_instruction1(&out, PUSH, 0);
	write_instruction0(&out, C_REF);
	write_instruction0(&out, DUP);
	write_instruction1(&out, PUSH, 255);
	write_instruction0(&out, GT);
	write_instruction0(&out, SWAP);
	write_instruction1(&out, PUSH, '}');
	write_instruction0(&out, EQUALE);
	write_instruction0(&out, OR);
	int ifz_addr = out.index;
	write_instruction1(&out, IFZ, 0);

	// return nFontSize + nWeightSize
	write_instruction1(&out, SH_LOCALREF, 1);
	write_instruction1(&out, SH_LOCALREF, 2);
	write_instruction0(&out, ADD);
	int jump_addr = out.index;
	write_instruction1(&out, JUMP, 0);

	// return nFontSize / 2 + nWeightSize;
	buffer_write_int32_at(&out, ifz_addr + 2, out.index);
	write_instruction1(&out, SH_LOCALREF, 1);
	write_instruction1(&out, PUSH, 2);
	write_instruction0(&out, DIV);
	write_instruction1(&out, SH_LOCALREF, 2);
	write_instruction0(&out, ADD);

	// multiply by config.text_x_scale (if not 1.0)
	buffer_write_int32_at(&out, jump_addr + 2, out.index);
	if (fabsf(1.0f - config.text_x_scale) > 0.01) {
		union { int32_t i; float f; } cast = { .f = config.text_x_scale };
		write_instruction0(&out, ITOF);
		write_instruction1(&out, F_PUSH, cast.i);
		write_instruction0(&out, F_MUL);
		write_instruction0(&out, FTOI);
	}

	write_instruction0(&out, RETURN);
	write_instruction1(&out, ENDFUNC, fno);

	game_rance02_mg = true;
}

// Rance VI (MangaGamer version)
// -----------------------------
// 'ﾉ' character is mapped to 'é' (Rosé)
static void apply_rance6_hacks(struct ain *ain)
{
	if (ain_get_function(ain, "GetTextWidth") < 0)
		return;
	game_rance6_mg = true;
}

// Daibanchou (English fan translation)
// ------------------------------------
// Gpx2Plus.CopyStretchReduceAMap behaves differently in order to work around
// text rendering issues caused by how the game calculates text width in
// StructRegionalSelect::DrawMenuItem.
static void apply_daibanchou_hacks(struct ain *ain)
{
	if (ain->nr_strings > 2 && !strcmp(ain->strings[2]->text, "Seijou Academy")) {
		game_daibanchou_en = true;
	}
}

void apply_game_specific_hacks(struct ain *ain)
{
	char *game_name = game_to_utf8(config.game_name, 0);
	if (!strcmp(game_name, "大番長")) {
		apply_daibanchou_hacks(ain);
	} else if (!strcmp(game_name, "Rance 02")) {
		apply_rance02_hacks(ain);
	} else if (!strcmp(game_name, "Rance6")) {
		apply_rance6_hacks(ain);
	} else if (!strcmp(game_name, "ランス・クエスト")) {
		game_rance8 = true;
	} else if (!strcmp(game_name, "Rance Quest")) {
		game_rance8 = true;
		game_rance8_mg = true;
	} else if (!strcmp(game_name, "ＤＵＮＧＥＯＮＳ＆ＤＯＬＬＳ")) {
		game_dungeons_and_dolls = true;
	}
	free(game_name);

	// XXX: we don't rely on game_name because mods change it
	if (ain_get_library(ain, "SengokuRanceFont") >= 0)
		game_rance7_mg = true;
}
