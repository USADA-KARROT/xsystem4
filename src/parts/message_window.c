/* Copyright (C) 2026 xsystem4 contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include <string.h>

#include "system4.h"
#include "system4/cg.h"
#include "system4/string.h"
#include "asset_manager.h"
#include "vm.h"
#include "parts.h"
#include "parts_internal.h"

/* The key wait mark (キー待ちマーク), the NEXT sign of a message window.
 * The original keeps it as the third private child of the window object
 * (obj+0xa8, after the background +0xa0 and the text +0xa4) and builds it
 * from these properties whenever one of them changed (0x4f2370):
 *   a Flat of FlatName exists           -> that Flat
 *   StartNo, NumofCG, TimePerCG all 0   -> the CG CGName, or the text "▼"
 *                                          (size 24) when there is no such CG
 *   otherwise                           -> a loop CG (0x5b30f0): CGName is a
 *                                          printf format for the numbers
 *                                          StartNo .. StartNo+NumofCG-1
 * Its place is the top left corner of the background plus Pos, and it shows
 * while Show is set (0x4f2790, 0x4f2370). Not drawn here: the Flat and the
 * "▼"; such a window has no mark, as before. */
struct parts_key_wait {
	struct string *cg_name;        // +0x100
	struct string *flat_name;      // +0x118
	int start_no, nr_cg, time_per_cg;  // +0x130, +0x134, +0x138
	int x, y, z;                   // +0x13c, +0x140, +0x144
	bool show;                     // +0x148
	// A property changed since the mark was built.
	bool changed;
	// What is built: one CG, or the frames of the loop CG.
	bool built;
	bool loop;
	struct string *built_name;
	int built_start, built_nr, built_time;
	Texture *frames;
	int nr_frames;
	int frame;
	int remainder;
	// The next update does not advance the loop (0x5b3090).
	bool first;
};

/* A message window is a background and independent text, not two mutually
 * exclusive DEFAULT states. The sidecar dies with its owning parts object. */
struct parts_message_window {
	struct string *raw_text;
	struct string *cg_name;
	struct parts_text text;
	int plain_bytes;
	Rectangle text_area;
	int text_origin_mode;
	struct parts_key_wait key_wait;
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

static void key_wait_free_frames(struct parts_key_wait *kw)
{
	for (int i = 0; i < kw->nr_frames; i++)
		gfx_delete_texture(&kw->frames[i]);
	free(kw->frames);
	kw->frames = NULL;
	kw->nr_frames = 0;
	if (kw->built_name)
		free_string(kw->built_name);
	kw->built_name = NULL;
	kw->built = false;
}

void parts_message_window_free(struct parts_message_window *mw)
{
	if (!mw)
		return;
	key_wait_free_frames(&mw->key_wait);
	if (mw->key_wait.cg_name)
		free_string(mw->key_wait.cg_name);
	if (mw->key_wait.flat_name)
		free_string(mw->key_wait.flat_name);
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

/* Key wait mark properties. Like the other message window calls of the
 * original these act on the window object of 0x53e010: no object for a
 * number of 0 or less, so the setters do nothing and the getters return 0,
 * false or "" and leave their outputs. Each setter writes and asks for a
 * rebuild only when a value differs. */

static struct parts_key_wait *key_wait_get(int parts_no)
{
	if (parts_no <= 0)
		return NULL;
	return &message_window_get(parts_get(parts_no))->key_wait;
}

static struct parts_key_wait *key_wait_try_get(int parts_no)
{
	struct parts *parts = parts_no > 0 ? parts_try_get(parts_no) : NULL;
	return parts && parts->message ? &parts->message->key_wait : NULL;
}

static bool key_wait_name_is(struct string *have, struct string *name)
{
	return !strcmp(have ? have->text : "", name ? name->text : "");
}

static void key_wait_set_name(struct string **dst, struct string *name)
{
	if (*dst)
		free_string(*dst);
	*dst = name ? string_dup(name) : NULL;
}

static void key_wait_changed(int parts_no, struct parts_key_wait *kw)
{
	kw->changed = true;
	parts_dirty(parts_get(parts_no));
}

// 0x5953d0 -> 0x4ee930
void PE_SetKeyWaitCGName(int parts_no, struct string *name, int start_no,
		int nr_cg, int time_per_cg)
{
	struct parts_key_wait *kw = key_wait_get(parts_no);
	if (!kw)
		return;
	if (key_wait_name_is(kw->cg_name, name) && kw->start_no == start_no
			&& kw->nr_cg == nr_cg && kw->time_per_cg == time_per_cg)
		return;
	key_wait_set_name(&kw->cg_name, name);
	kw->start_no = start_no;
	kw->nr_cg = nr_cg;
	kw->time_per_cg = time_per_cg;
	key_wait_changed(parts_no, kw);
}

// 0x595410
bool PE_GetKeyWaitCGName(int parts_no, struct string **name, int *start_no,
		int *nr_cg, int *time_per_cg)
{
	struct parts_key_wait *kw = key_wait_try_get(parts_no);
	if (!kw)
		return false;
	if (name)
		*name = kw->cg_name ? string_dup(kw->cg_name) : string_ref(&EMPTY_STRING);
	if (start_no) *start_no = kw->start_no;
	if (nr_cg) *nr_cg = kw->nr_cg;
	if (time_per_cg) *time_per_cg = kw->time_per_cg;
	return true;
}

// 0x595470 -> 0x4ee9e0
void PE_SetKeyWaitFlatName(int parts_no, struct string *name)
{
	struct parts_key_wait *kw = key_wait_get(parts_no);
	if (!kw || key_wait_name_is(kw->flat_name, name))
		return;
	key_wait_set_name(&kw->flat_name, name);
	key_wait_changed(parts_no, kw);
}

// 0x5954a0
struct string *PE_GetKeyWaitFlatName(int parts_no)
{
	struct parts_key_wait *kw = key_wait_try_get(parts_no);
	return kw && kw->flat_name ? string_ref(kw->flat_name) : string_ref(&EMPTY_STRING);
}

// 0x595500
void PE_SetKeyWaitPos(int parts_no, int x, int y, int z)
{
	struct parts_key_wait *kw = key_wait_get(parts_no);
	if (!kw || (kw->x == x && kw->y == y && kw->z == z))
		return;
	kw->x = x;
	kw->y = y;
	kw->z = z;
	key_wait_changed(parts_no, kw);
}

// 0x595560, 0x595580, 0x5955a0
int PE_GetKeyWaitPosX(int parts_no)
{
	struct parts_key_wait *kw = key_wait_try_get(parts_no);
	return kw ? kw->x : 0;
}

int PE_GetKeyWaitPosY(int parts_no)
{
	struct parts_key_wait *kw = key_wait_try_get(parts_no);
	return kw ? kw->y : 0;
}

int PE_GetKeyWaitPosZ(int parts_no)
{
	struct parts_key_wait *kw = key_wait_try_get(parts_no);
	return kw ? kw->z : 0;
}

// 0x5955c0. The script sets it in every frame of the key wait and clears it
// on leaving (FUNC 7153); the rebuild this asks for puts a loop CG back to
// its first frame, so the mark starts from there each time it appears.
void PE_SetKeyWaitShow(int parts_no, bool show)
{
	struct parts_key_wait *kw = key_wait_get(parts_no);
	if (!kw || kw->show == show)
		return;
	kw->show = show;
	key_wait_changed(parts_no, kw);
}

// 0x5955f0
bool PE_IsKeyWaitShow(int parts_no)
{
	struct parts_key_wait *kw = key_wait_try_get(parts_no);
	return kw && kw->show;
}

/* Loads one CG of the mark by name; NULL takes it from the game's archives.
 * (The probe has no archives and puts its own CGs here.) */
struct cg *(*parts_key_wait_load_cg)(const char *name);

static Texture *key_wait_load(struct string *name, int start_no, int nr, bool format)
{
	Texture *frames = xcalloc(nr, sizeof(Texture));
	for (int i = 0; i < nr; i++) {
		struct string *cg_name = format
			? string_format(name, (union vm_value){.i = start_no + i}, STRFMT_INT)
			: string_ref(name);
		int no;
		struct cg *cg = parts_key_wait_load_cg ? parts_key_wait_load_cg(cg_name->text)
			: asset_cg_load_by_name(cg_name->text, &no);
		free_string(cg_name);
		if (!cg) {
			// One missing frame fails the whole loop CG (0x5b30f0).
			for (int j = 0; j < i; j++)
				gfx_delete_texture(&frames[j]);
			free(frames);
			return NULL;
		}
		gfx_init_texture_with_cg(&frames[i], cg);
		cg_free(cg);
	}
	return frames;
}

// 0x4f2370
static void key_wait_build(struct parts_key_wait *kw)
{
	kw->changed = false;
	bool loop = kw->start_no || kw->nr_cg || kw->time_per_cg;
	if (kw->built && kw->loop == loop && key_wait_name_is(kw->built_name, kw->cg_name)
			&& (!loop || (kw->built_start == kw->start_no && kw->built_nr == kw->nr_cg
				&& kw->built_time == kw->time_per_cg))) {
		// The same loop CG set again: back to its first frame, and the
		// next update leaves it there (0x5b30f0).
		kw->frame = 0;
		kw->remainder = 0;
		kw->first = true;
		return;
	}
	key_wait_free_frames(kw);
	kw->built = true;
	kw->loop = loop;
	kw->built_name = kw->cg_name ? string_dup(kw->cg_name) : NULL;
	kw->built_start = kw->start_no;
	kw->built_nr = kw->nr_cg;
	kw->built_time = kw->time_per_cg;
	kw->frame = 0;
	kw->remainder = 0;
	kw->first = true;
	int nr = loop ? kw->nr_cg : 1;
	// No name, no such CG (the original then draws "▼") or a loop CG that
	// does not load: no mark.
	if (!kw->cg_name || !kw->cg_name->size || nr <= 0 || nr > 10000)
		return;
	kw->frames = key_wait_load(kw->cg_name, kw->start_no, nr, loop);
	if (kw->frames)
		kw->nr_frames = nr;
}

// 0x5b3090: the frame of a loop CG after passed_time more milliseconds.
static bool key_wait_advance(struct parts_key_wait *kw, int passed_time)
{
	if (!kw->loop || kw->built_time <= 0 || !kw->nr_frames)
		return false;
	if (kw->first) {
		kw->first = false;
		return false;
	}
	if (passed_time < 0)
		passed_time = 0;
	int elapsed = passed_time + kw->remainder;
	int frame = (kw->frame + elapsed / kw->built_time) % kw->nr_frames;
	kw->remainder = elapsed % kw->built_time;
	if (frame == kw->frame)
		return false;
	kw->frame = frame;
	return true;
}

/* Once per PartsEngine update: rebuilds the marks whose properties changed
 * and advances the loop CGs. The original gives the mark the time its owner
 * selected, the scaled one for a window with スピードアップ有効 (0x4f32c0);
 * here it is the passed time of PE_Update. */
void parts_message_window_update(int passed_time)
{
	struct parts *parts;
	PARTS_LIST_FOREACH(parts) {
		if (!parts->message)
			continue;
		struct parts_key_wait *kw = &parts->message->key_wait;
		bool redraw = kw->changed;
		if (kw->changed)
			key_wait_build(kw);
		if (key_wait_advance(kw, passed_time) && kw->show)
			redraw = true;
		if (redraw)
			parts_dirty(parts);
	}
}

/* The frame to draw and where, or NULL: the top left corner of the mark is
 * the owner's position plus the background's origin offset plus Pos
 * (0x4f2790), as for the text area. */
Texture *parts_message_window_key_wait(struct parts *parts, Point *position)
{
	struct parts_message_window *mw = parts->message;
	if (!mw)
		return NULL;
	struct parts_key_wait *kw = &mw->key_wait;
	if (kw->changed)
		key_wait_build(kw);
	if (!kw->show || !kw->nr_frames)
		return NULL;
	Point background = parts->states[PARTS_STATE_DEFAULT].common.origin_offset;
	*position = (Point){parts->global.pos.x + background.x + kw->x,
		parts->global.pos.y + background.y + kw->y};
	return &kw->frames[kw->frame];
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
