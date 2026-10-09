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

#include <math.h>
#include <string.h>

#include "system4.h"
#include "system4/ain.h"
#include "system4/string.h"
#include "system4/utfsjis.h"

#include "vm.h"
#include "xsystem4.h"
#include "parts_internal.h"

static int extract_multibyte_char(const char *src, char *dst)
{
	unsigned char c = (unsigned char)*src;
	if (ain_is_gb18030 && c >= 0x81 && c <= 0xFE) {
		/* 2-byte, or 4-byte GB18030; a character cut short by the NUL keeps
		 * only the bytes before it */
		unsigned char c2 = (unsigned char)src[1];
		int n = 2;
		if (!c2)
			n = 1;
		else if (c2 >= 0x30 && c2 <= 0x39)
			n = !src[2] ? 2 : !src[3] ? 3 : 4;
		memcpy(dst, src, n);
		dst[n] = '\0';
		return n;
	}
	if (ain_is_gb18030 && SJIS_2BYTE(*src) && !src[1]) {
		/* 0x80 or 0xFF right before the NUL */
		dst[0] = src[0];
		dst[1] = '\0';
		return 1;
	}
	if (SJIS_2BYTE(*src)) {
		dst[0] = src[0];
		dst[1] = src[1];
		dst[2] = '\0';
		return 2;
	}
	dst[0] = src[0];
	dst[1] = '\0';
	return 1;
}

// The bytes of the character at src, as a text is cut into characters.
int parts_text_char_bytes(const char *src)
{
	char ch[8];
	return extract_multibyte_char(src, ch);
}

struct string *parts_text_line_get(struct parts_text_line *line)
{
	struct string *s = make_string("", 0);
	for (int i = 0; i < line->nr_chars; i++) {
		string_append_cstr(&s, line->chars[i].ch, strlen(line->chars[i].ch));
	}
	return s;
}

struct string *parts_text_get(struct parts_text *t)
{
	struct string *s = make_string("", 0);
	for (int i = 0; i < t->nr_lines; i++) {
		if (i > 0)
			string_push_back(&s, '\n');
		struct parts_text_line *line = &t->lines[i];
		for (int i = 0; i < line->nr_chars; i++) {
			string_append_cstr(&s, line->chars[i].ch, strlen(line->chars[i].ch));
		}
	}
	return s;
}

/*
 * A v14 text state on the CN glyph grid (gfx_text_cn_gdi) is laid out as the
 * original lays it out. Each character is a run of its own
 * (0x5bfe00 -> 0x5c0820) with a glyph texture of its cell and e on every
 * side, (cell + 2e) x (size + 2e), the glyph at (e, e) (0x69c290, 0x69c3d0);
 * e is gfx_text_cn_style_edge. A line is as high as the font size made even
 * plus 2e, or a taller glyph texture, which there is none of here (0x5bce00),
 * and the glyphs stand on its bottom (0x5bcf60).
 *
 * The texture made here is e rows higher than that cell, below it. That is
 * not the original's: its font keeps the descenders inside the cell, the
 * default font here draws them below it, and their edge needs the rows. The
 * rows below the baseline are then round(0.15 size) + 2e. With no bold_width
 * and an edge no wider than the font size (this game's data and scripts give
 * nothing else) that is no fewer than a texture of the font size and both
 * edges has, round(0.15 size) + trunc(2 * edge). The line, the extent, the
 * hit box and the glyph's place in the line are the cell's, never the
 * texture's; what is drawn in the extra rows shows below the line. With
 * e = 0 (neither 太さ nor 縁取り) there are no extra rows, and a descender
 * that reaches below the cell is cut as it is elsewhere. With 太さ and no
 * 縁取り e is 1: the one extra row lets up to one more row of a descender's
 * body show below the line.
 *
 * The message window's text is laid out here too. Its widget is another one
 * in the original, not traced, and keeps the layout below, as do v13 and the
 * games whose glyphs are not drawn on that grid.
 */
static bool parts_text_v14_grid(struct parts *parts, struct parts_text *t)
{
	if (ain->version < 14 || !gfx_text_cn_gdi())
		return false;
	for (int i = 0; i < PARTS_NR_STATES; i++) {
		if (t == &parts->states[i].text)
			return true;
	}
	return false;
}

static int parts_text_v14_cell_height(struct text_style *ts)
{
	return lroundf(ts->size) + 2 * gfx_text_cn_style_edge(ts);
}

static int parts_text_v14_line_height(struct text_style *ts)
{
	int size = lroundf(ts->size);
	return size + (size & 1) + 2 * gfx_text_cn_style_edge(ts);
}

static const char *parts_text_append_char(struct parts_text *t, const char *str, bool grid)
{
	int line_height = grid ? parts_text_v14_line_height(&t->ts) : text_style_height(&t->ts);
	if (*str == '\n') {
		// 0x5beefd: a line with nothing before its break is as high as the
		// break's run; so is the line after the last break (0x5bef63).
		if (grid && !t->lines[t->nr_lines - 1].nr_chars)
			t->lines[t->nr_lines - 1].height = line_height;
		t->lines = xrealloc_array(t->lines, t->nr_lines, t->nr_lines + 1,
				sizeof(struct parts_text_line));
		t->lines[t->nr_lines].height = line_height;
		t->nr_lines++;
		return str + 1;
	}

	struct parts_text_line *line = &t->lines[t->nr_lines - 1];
	line->chars = xrealloc_array(line->chars, line->nr_chars, line->nr_chars + 1,
			sizeof(struct parts_text_char));
	struct parts_text_char *ch = &line->chars[line->nr_chars++];

	int len = extract_multibyte_char(str, ch->ch);
	int width = ceilf(text_style_width(&t->ts, ch->ch));
	int height = grid ? parts_text_v14_cell_height(&t->ts) : text_style_height(&t->ts);
	int edge = grid ? gfx_text_cn_style_edge(&t->ts) : 0;
	// Where the texture is drawn in its line: the cell on the line's bottom
	// (v14 grid).
	ch->off = (Point) { 0, grid ? line_height - height : 0 };
	gfx_init_texture_rgba(&ch->t, width, height + edge, (SDL_Color){0,0,0,0});
	ch->advance = gfx_render_textf(&ch->t, 0, edge, ch->ch, &t->ts, false);

	line->width += ch->advance;
	line->height = max(line->height, grid ? line_height : height);
	return str + len;
}

/*
 * A text's extent as the original computes it for a v14 text state
 * (0x5bee50): the widest line and the lines' heights with the line spacing
 * between them. A line is its glyph textures' widths and the character
 * spacing between them, one less than the glyphs (0x5bcd20; 0x5bef22 between
 * the one-character runs). A glyph texture is as wide as
 * parts_text_append_char makes a character's. The line's own width is not
 * used: it adds up the advances, a spacing after every character. A text
 * without a run has no extent.
 */
void parts_text_extent(struct parts_text *t, int *w, int *h)
{
	float width = 0;
	int height = 0;
	for (unsigned i = 0; i < t->nr_lines; i++) {
		struct parts_text_line *line = &t->lines[i];
		float line_w = 0;
		for (int c = 0; c < line->nr_chars; c++)
			line_w += ceilf(text_style_width(&t->ts, line->chars[c].ch));
		if (line->nr_chars > 1)
			line_w += (line->nr_chars - 1) * t->ts.font_spacing;
		if (line_w > width)
			width = line_w;
		if (i > 0)
			height += t->line_space;
		height += line->height;
	}
	*w = ceilf(width);
	*h = height;
}

void parts_text_append(struct parts *parts, struct parts_text *t, struct string *text)
{
	bool grid = parts_text_v14_grid(parts, t);
	if (!t->nr_lines) {
		t->lines = xcalloc(1, sizeof(struct parts_text_line));
		t->nr_lines = 1;
	}

	const char *msgp = text->text;
	while (*msgp) {
		msgp = parts_text_append_char(t, msgp, grid);
	}

	if (grid) {
		// The state reports that extent (0x5bf8c0, 0x5bf910), and its origin
		// offset and hit box are taken from it (0x5bf2c0, 0x5bf7d0): also
		// when it is empty.
		int w, h;
		parts_text_extent(t, &w, &h);
		parts_set_dims(parts, &t->common, w, h);
		return;
	}

	// calculate dimensions of the text
	float f_width = 0;
	int height = 0;
	for (int i = 0; i < t->nr_lines; i++) {
		f_width = max(f_width, t->lines[i].width);
		if (i > 0)
			height += t->line_space;
		height += t->lines[i].height;
	}
	// A layout box sizes a text by its lines: an empty text keeps its
	// texture size below, and a text set back to that size is no change for
	// parts_set_dims.
	if (t == &parts->states[0].text)
		parts_layout_size_changed(parts);

	int width = ceilf(f_width);
	if (!width || !height)
		return;

	parts_set_dims(parts, &t->common, width, height);
}

void parts_text_free(struct parts_text *t)
{
	gfx_delete_texture(&t->common.texture);
	for (int i = 0; i < t->nr_lines; i++) {
		struct parts_text_line *line = &t->lines[i];
		for (int i = 0; i < line->nr_chars; i++) {
			gfx_delete_texture(&line->chars[i].t);
		}
		free(line->chars);
	}
	free(t->lines);
}

static void parts_text_rerender(struct parts *parts, struct parts_text *t)
{
	if (!t->nr_lines)
		return;
	struct string *text = parts_text_get(t);
	parts_text_free(t);
	t->lines = NULL;
	t->nr_lines = 0;
	parts_text_append(parts, t, text);
	free_string(text);
	parts_dirty(parts);
}

static void parts_text_clear(struct parts *parts, int state)
{
	struct parts_text *text = parts_get_text(parts, state);
	parts_text_free(text);
	text->lines = NULL;
	text->nr_lines = 0;
	text->cursor.x = 0;
	text->cursor.y = 0;
}

bool PE_SetText(int parts_no, struct string *text, int state)
{
	if (!parts_state_valid(--state))
		return false;

	struct parts *parts = parts_get(parts_no);
	parts_text_clear(parts, state);
	parts_text_append(parts, parts_get_text(parts, state), text);
	return true;
}

bool PE_AddPartsText(int parts_no, struct string *text, int state)
{
	if (!parts_state_valid(--state))
		return false;

	struct parts *parts = parts_get(parts_no);
	parts_text_append(parts, parts_get_text(parts, state), text);
	return true;
}

bool PE_SetPartsTextSurfaceArea(int parts_no, int x, int y, int w, int h, int state)
{
	if (!parts_state_valid(--state))
		return false;

	struct parts *parts = parts_get(parts_no);
	struct parts_text *text = parts_get_text(parts, state);
	parts_set_surface_area(parts, &text->common, x, y, w, h);
	return true;
}

//bool PE_DeletePartsTopTextLine(int PartsNumber, int State);

bool PE_SetFont(int parts_no, int type, int size, int r, int g, int b, float bold_weight, int edge_r, int edge_g, int edge_b, float edge_weight, int state)
{
	if (!parts_state_valid(--state))
		return false;

	struct parts *parts = parts_get(parts_no);
	struct parts_text *text = parts_get_text(parts, state);
	text->ts.face = type;
	text->ts.size = size;
	text->ts.color = (SDL_Color) { r, g, b, 255 };
	text->ts.weight = bold_weight * 1000;
	text->ts.bold_weight = bold_weight;
	text->ts.edge_color = (SDL_Color) { edge_r, edge_g, edge_b, 255 };
	text_style_set_edge_width(&text->ts, edge_weight);
	parts_text_rerender(parts, text);
	return true;
}

bool PE_SetPartsFontType(int parts_no, int type, int state)
{
	if (!parts_state_valid(--state))
		return false;

	struct parts *parts = parts_get(parts_no);
	struct parts_text *text = parts_get_text(parts, state);
	if (text->ts.face == type)
		return true;
	text->ts.face = type;
	parts_text_rerender(parts, text);
	return true;
}

bool PE_SetPartsFontSize(int parts_no, int size, int state)
{
	if (!parts_state_valid(--state))
		return false;

	struct parts *parts = parts_get(parts_no);
	struct parts_text *text = parts_get_text(parts, state);
	if (text->ts.size == size)
		return true;
	text->ts.size = size;
	parts_text_rerender(parts, text);
	return true;
}

bool PE_SetPartsFontColor(int parts_no, int r, int g, int b, int state)
{
	if (!parts_state_valid(--state))
		return false;

	struct parts *parts = parts_get(parts_no);
	struct parts_text *text = parts_get_text(parts, state);
	SDL_Color color = { r, g, b, 255 };
	if (!memcmp(&text->ts.color, &color, sizeof(SDL_Color)))
		return true;
	text->ts.color = color;
	parts_text_rerender(parts, text);
	return true;
}

bool PE_SetPartsFontBoldWeight(int parts_no, float bold_weight, int state)
{
	if (!parts_state_valid(--state))
		return false;

	struct parts *parts = parts_get(parts_no);
	struct parts_text *text = parts_get_text(parts, state);
	if (text->ts.bold_width == bold_weight)
		return true;
	text->ts.bold_width = bold_weight;
	parts_text_rerender(parts, text);
	return true;
}

bool PE_SetPartsFontEdgeColor(int parts_no, int r, int g, int b, int state)
{
	if (!parts_state_valid(--state))
		return false;

	struct parts *parts = parts_get(parts_no);
	struct parts_text *text = parts_get_text(parts, state);
	SDL_Color color = { r, g, b, 255 };
	if (!memcmp(&text->ts.edge_color, &color, sizeof(SDL_Color)))
		return true;
	text->ts.edge_color = color;
	parts_text_rerender(parts, text);
	return true;
}

bool PE_SetPartsFontEdgeWeight(int parts_no, float edge_weight, int state)
{
	if (!parts_state_valid(--state))
		return false;

	struct parts *parts = parts_get(parts_no);
	struct parts_text *text = parts_get_text(parts, state);
	float old_left = text->ts.edge_left;
	float old_up = text->ts.edge_up;
	float old_right = text->ts.edge_right;
	float old_down = text->ts.edge_down;
	text_style_set_edge_width(&text->ts, edge_weight);
	if (text->ts.edge_left == old_left && text->ts.edge_up == old_up &&
	    text->ts.edge_right == old_right && text->ts.edge_down == old_down)
		return true;
	parts_text_rerender(parts, text);
	return true;
}

bool PE_SetTextCharSpace(int parts_no, int char_space, int state)
{
	if (!parts_state_valid(--state))
		return false;

	struct parts *parts = parts_get(parts_no);
	struct parts_text *text = parts_get_text(parts, state);
	if (text->ts.font_spacing == char_space)
		return true;
	text->ts.font_spacing = char_space;
	parts_text_rerender(parts, text);
	return true;
}

bool PE_SetTextLineSpace(int parts_no, int line_space, int state)
{
	if (!parts_state_valid(--state))
		return false;

	struct parts *parts = parts_get(parts_no);
	struct parts_text *text = parts_get_text(parts, state);
	if (text->line_space == line_space)
		return true;
	text->line_space = line_space;
	parts_text_rerender(parts, text);
	return true;
}


