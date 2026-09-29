/* Copyright (C) 2026 xsystem4 contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include <string.h>

#include "system4.h"
#include "system4/string.h"
#include "parts.h"
#include "parts_internal.h"

/* A message window is a background and independent text, not two mutually
 * exclusive DEFAULT states. The sidecar dies with its owning parts object. */
struct parts_message_window {
	struct string *raw_text;
	struct string *cg_name;
	struct parts_text text;
	int plain_bytes;
	Rectangle text_area;
	int text_origin_mode;
	bool key_wait_show;
	// The text, font or spacing changed since the text was laid out.
	bool text_changed;
};

static struct parts_message_window *message_window_get(struct parts *parts)
{
	if (!parts->message) {
		struct parts_message_window *mw = xcalloc(1, sizeof(*mw));
		struct parts_state initial = {0};
		parts_state_reset(&initial, PARTS_TEXT);
		mw->text = initial.text;
		mw->text_origin_mode = 1;
		parts->message = mw;
	}
	return parts->message;
}

void parts_message_window_free(struct parts_message_window *mw)
{
	if (!mw)
		return;
	if (mw->raw_text)
		free_string(mw->raw_text);
	if (mw->cg_name)
		free_string(mw->cg_name);
	parts_text_free(&mw->text);
	free(mw);
}

static bool markup_tag_is(const char *tag, size_t size, const char *name)
{
	size_t length = strlen(name);
	return size >= length && !memcmp(tag, name, length)
		&& (size == length || tag[length] == ' ' || tag[length] == '\t');
}

/* The AIN composer emits time/font spans. For immediate text rendering these
 * tags are metadata, never glyphs. Keep the original bytes separately for the
 * next GetMessageWindowText + append + SetMessageWindowText cycle. Copy runs
 * rather than using string_push_back, whose input is a packed SJIS character. */
static struct string *message_window_plain_text(struct string *raw)
{
	struct string *plain = string_ref(&EMPTY_STRING);
	if (!raw)
		return plain;
	const char *end = raw->text + raw->size;
	const char *run = raw->text;
	for (const char *p = run; p < end; p++) {
		if (end - p < 3 || p[0] != '$' || p[1] != '{')
			continue;
		const char *close = memchr(p + 2, '}', end - (p + 2));
		if (!close)
			break;
		const char *tag = p + 2;
		size_t size = close - tag;
		if (!markup_tag_is(tag, size, "time") && !markup_tag_is(tag, size, "/time")
				&& !markup_tag_is(tag, size, "font") && !markup_tag_is(tag, size, "/font"))
			continue;
		string_append_cstr(&plain, run, p - run);
		p = close;
		run = close + 1;
	}
	string_append_cstr(&plain, run, end - run);
	return plain;
}

/* Lays the text out (one texture per glyph). Only the drawing reads the
 * layout, so it runs there, once for all the changes before it: the game
 * sets the text three to four times per line (GetMessageWindowText + append +
 * SetMessageWindowText), and laying the whole text out on every call took
 * about 1 ms each, up to 5 ms (research/gui-visual/pacing.md). */
static void message_window_layout(struct parts *parts)
{
	struct parts_message_window *mw = parts->message;
	struct text_style style = mw->text.ts;
	int line_space = mw->text.line_space;
	parts_text_free(&mw->text);
	memset(&mw->text, 0, sizeof(mw->text));
	mw->text.ts = style;
	mw->text.line_space = line_space;
	struct string *plain = message_window_plain_text(mw->raw_text);
	mw->plain_bytes = plain->size;
	parts_text_append(parts, &mw->text, plain);
	free_string(plain);
	mw->text_changed = false;
}

static void message_window_rebuild(struct parts *parts)
{
	parts->message->text_changed = true;
	parts_dirty(parts);
}

void PE_SetMessageWindowText(int parts_no, struct string *text,
		possibly_unused int msg_num, possibly_unused struct string *func_name,
		possibly_unused int ver, possibly_unused int step)
{
	struct parts *parts = parts_get(parts_no);
	struct parts_message_window *mw = message_window_get(parts);
	struct string *copy = text ? string_dup(text) : string_ref(&EMPTY_STRING);
	if (mw->raw_text)
		free_string(mw->raw_text);
	mw->raw_text = copy;
	message_window_rebuild(parts);
}

struct string *PE_GetMessageWindowText(int parts_no)
{
	struct parts *parts = parts_try_get(parts_no);
	if (!parts || !parts->message || !parts->message->raw_text)
		return string_ref(&EMPTY_STRING);
	return string_ref(parts->message->raw_text);
}

void PE_SetMessageWindowCGName(int parts_no, struct string *name)
{
	struct parts *parts = parts_get(parts_no);
	struct parts_message_window *mw = message_window_get(parts);
	struct string *copy = name ? string_dup(name) : string_ref(&EMPTY_STRING);
	if (mw->cg_name)
		free_string(mw->cg_name);
	mw->cg_name = copy;
	PE_SetPartsCG(parts_no, copy, 0, 1);
	parts_dirty(parts);
}

struct string *PE_GetMessageWindowCGName(int parts_no)
{
	struct parts *parts = parts_try_get(parts_no);
	if (!parts)
		return string_ref(&EMPTY_STRING);
	if (parts->message && parts->message->cg_name)
		return string_ref(parts->message->cg_name);
	// The activity loader can install a background before message APIs run.
	const struct parts_state *state = &parts->states[PARTS_STATE_DEFAULT];
	if (state->type == PARTS_CG && state->cg.name)
		return string_ref(state->cg.name);
	return string_ref(&EMPTY_STRING);
}

void PE_SetMessageWindowTextArea(int parts_no, int x, int y, int w, int h)
{
	struct parts *parts = parts_get(parts_no);
	message_window_get(parts)->text_area = (Rectangle){x, y, w, h};
	parts_dirty(parts);
}

void PE_GetMessageWindowTextArea(int parts_no, int *x, int *y, int *w, int *h)
{
	struct parts *parts = parts_try_get(parts_no);
	Rectangle area = parts && parts->message ? parts->message->text_area : (Rectangle){0};
	if (x) *x = area.x;
	if (y) *y = area.y;
	if (w) *w = area.w;
	if (h) *h = area.h;
}

void PE_SetMessageWindowTextOriginPosMode(int parts_no, int mode)
{
	struct parts *parts = parts_get(parts_no);
	message_window_get(parts)->text_origin_mode = mode;
	parts_dirty(parts);
}

void PE_SetMessageWindowTextFont(int parts_no, int type, int size,
		int r, int g, int b, float bold_weight,
		int edge_r, int edge_g, int edge_b, float edge_weight)
{
	struct parts *parts = parts_get(parts_no);
	struct text_style *ts = &message_window_get(parts)->text.ts;
	ts->face = type;
	ts->size = size;
	ts->color = (SDL_Color){r, g, b, 255};
	ts->weight = bold_weight * 1000;
	ts->bold_weight = bold_weight;
	ts->edge_color = (SDL_Color){edge_r, edge_g, edge_b, 255};
	text_style_set_edge_width(ts, edge_weight);
	message_window_rebuild(parts);
}

void PE_SetMessageWindowTextSpace(int parts_no, int letter_space, int line_space)
{
	struct parts *parts = parts_get(parts_no);
	struct parts_message_window *mw = message_window_get(parts);
	mw->text.ts.font_spacing = letter_space;
	mw->text.line_space = line_space;
	message_window_rebuild(parts);
}

void PE_SetKeyWaitShow(int parts_no, bool show)
{
	// The wait marker is independent of the window background and dialogue.
	// Its CG/animation is not implemented here; hiding it must not hide text.
	struct parts *parts = parts_get(parts_no);
	message_window_get(parts)->key_wait_show = show;
}

bool PE_IsKeyWaitShow(int parts_no)
{
	struct parts *parts = parts_try_get(parts_no);
	return parts && parts->message && parts->message->key_wait_show;
}

struct parts_text *parts_message_window_render_text(struct parts *parts, Point *position)
{
	struct parts_message_window *mw = parts->message;
	if (mw && mw->text_changed)
		message_window_layout(parts);
	if (!mw || !mw->text.nr_lines)
		return NULL;
	Point background = parts->states[PARTS_STATE_DEFAULT].common.origin_offset;
	*position = (Point){parts->global.pos.x + background.x + mw->text_area.x,
		parts->global.pos.y + background.y + mw->text_area.y};
	static unsigned trace_count;
	if (getenv("XSYS4_STAGE2_TRACE") && mw->raw_text && mw->raw_text->size
			&& trace_count < 12) {
		trace_count++;
		unsigned chars = 0;
		for (unsigned i = 0; i < mw->text.nr_lines; i++)
			chars += mw->text.lines[i].nr_chars;
		struct parts_common *bg = &parts->states[PARTS_STATE_DEFAULT].common;
		struct text_style *ts = &mw->text.ts;
		WARNING("S2 message Render parts=%d raw=%d plain=%d lines=%u chars=%u show=%d window_show=%d alpha=%d z=%d bg=(%u,%d,%d) origin=(%d,%d) area=(%d,%d,%d,%d) mode=%d pos=(%d,%d) font=(%u,%g) color=(%u,%u,%u,%u) spacing=(%g,%d)",
			parts->no, mw->raw_text->size, mw->plain_bytes, mw->text.nr_lines, chars,
			parts->global.show, parts_message_window_show, parts->global.alpha,
			parts->global.z, bg->texture.handle, bg->w, bg->h,
			background.x, background.y, mw->text_area.x, mw->text_area.y,
			mw->text_area.w, mw->text_area.h, mw->text_origin_mode,
			position->x, position->y, ts->face, (double)ts->size,
			ts->color.r, ts->color.g, ts->color.b, ts->color.a,
			(double)ts->font_spacing, mw->text.line_space);
	}
	// Dohna's observed modes 0/1/7 render at the area's top edge (Rufim's
	// message-window comparison). Keep the mode for future layout support;
	// applying generic parts anchoring here would shift mode 7 incorrectly.
	return &mw->text;
}
