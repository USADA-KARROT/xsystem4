/* Copyright (C) 2026 xsystem4 contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include <string.h>
#include <math.h>
#include <limits.h>
#include "system4/ain.h"

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

/* One character of the window text: when its fade begins, in the time of
 * the text (0x5bc859 gives each run its start and interval, 0x5bd247 each
 * character its place in the run), and whether it fades at all (0x5bd215:
 * a run with an interval of 0 or less is drawn at full alpha). */
struct message_glyph_time {
	int64_t start;
	bool fade;
};

/* A message window is a background and independent text, not two mutually
 * exclusive DEFAULT states. The sidecar dies with its owning parts object. */
struct parts_message_window {
	// Tells this window from one made later under the same number: the
	// script's speed rate runs while the windows are updated.
	uint64_t identity;
	int msg_num;                   // +0x204
	int text_speed;                // +0x1b8
	// The time of the text (text state +0xc8) and when its last fading
	// character is at full alpha.
	int64_t elapsed;
	int64_t fade_end;
	// Every character is at full alpha: Fix, or an update that reaches
	// fade_end (0x5bd371 clears the intervals of the runs, 0x566160 tests
	// them).
	bool fixed;
	// The text is laid out with a placeholder rate; the script's rate is
	// taken by the next update or Fix, and the characters stay hidden.
	bool rate_pending;
	// A property changed since the last update or Fix (+0x208).
	bool dirty;
	struct message_glyph_time *glyph_times;
	int nr_glyph_times;
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
		static uint64_t next_identity;
		mw->identity = ++next_identity;
		mw->text_speed = 23;  // 0x4ee657
		mw->fixed = true;
		// A new window counts as changed until its first update or Fix
		// (0x4ee71e sets +0x208).
		mw->dirty = true;
		parts->message = mw;
	}
	return parts->message;
}

/* The object of the text speed and Fix calls (0x53e010), as the other
 * message window calls of the original take it: a number above 0 gets its
 * parts and its window made (0x53d920 -> 0x53a0b0, then 0x536c20); 0 and
 * below have none (0x539c80). Returns the parts with its window, or NULL.
 * (0x536c20 replaces the content of a parts of another kind; here such a
 * parts keeps its states and gets the window beside them.) */
static struct parts *message_window_parts(int parts_no)
{
	if (parts_no <= 0)
		return NULL;
	struct parts *parts = parts_get(parts_no);
	message_window_get(parts);
	return parts;
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
	free(mw->glyph_times);
	free(mw);
}

static bool markup_tag_is(const char *tag, size_t size, const char *name)
{
	size_t length = strlen(name);
	return size >= length && !memcmp(tag, name, length)
		&& (size == length || tag[length] == ' ' || tag[length] == '\t');
}

/* The AIN composer emits time/font spans. These tags are metadata, never
 * glyphs. Keep the original bytes separately for the next
 * GetMessageWindowText + append + SetMessageWindowText cycle. Copy runs
 * rather than using string_push_back, whose input is a packed SJIS character.
 *
 * The text is also given its times here. The original (0x5bfe00) starts at 0
 * with the window's interval and gives every character a slot of it; a
 * ${time N} span has N as its interval, unscaled, and ${/time} takes the
 * previous one back (0x5c0a39, 0x5c0a6b; a ${/time} without a span is
 * dropped). A line break is no character and takes no slot (0x5bc785); a
 * full-width space does. Font tags are removed as before; their styling and
 * ruby are not done here. */
static struct string *message_window_plain_text(struct parts_message_window *mw, int interval)
{
	struct string *plain = string_ref(&EMPTY_STRING);
	free(mw->glyph_times);
	mw->glyph_times = NULL;
	mw->nr_glyph_times = 0;
	mw->fade_end = 0;
	if (!mw->raw_text)
		return plain;
	const char *p = mw->raw_text->text;
	const char *end = p + mw->raw_text->size;
	// Every character and every time tag is at least one byte.
	mw->glyph_times = xcalloc(mw->raw_text->size + 1, sizeof(*mw->glyph_times));
	int *saved = xcalloc(mw->raw_text->size + 1, sizeof(int));
	int depth = 0;
	int64_t start = 0;
	while (p < end) {
		const char *close = NULL;
		if (end - p >= 3 && p[0] == '$' && p[1] == '{')
			close = memchr(p + 2, '}', end - (p + 2));
		if (close) {
			const char *tag = p + 2;
			size_t size = close - tag;
			if (markup_tag_is(tag, size, "time")) {
				saved[depth++] = interval;
				char *tail;
				long n = strtol(tag + 4, &tail, 10);
				if (tail == tag + 4)
					interval = 0;
				else
					interval = n > INT_MAX ? INT_MAX : n < INT_MIN ? INT_MIN : (int)n;
				p = close + 1;
				continue;
			}
			if (markup_tag_is(tag, size, "/time")) {
				if (depth)
					interval = saved[--depth];
				p = close + 1;
				continue;
			}
			if (markup_tag_is(tag, size, "font") || markup_tag_is(tag, size, "/font")) {
				p = close + 1;
				continue;
			}
		}
		int bytes = parts_text_char_bytes(p);
		if (bytes > end - p)
			bytes = end - p;
		string_append_cstr(&plain, p, bytes);
		if (*p != '\n') {
			struct message_glyph_time *g = &mw->glyph_times[mw->nr_glyph_times++];
			g->start = start;
			g->fade = ain->version >= 14 && interval > 0;
			if (g->fade)
				mw->fade_end = max(mw->fade_end, start + 200);
			start += interval;
		}
		p += bytes;
	}
	free(saved);
	mw->fixed = mw->fade_end <= 0;
	return plain;
}

/* The rate the script gives the text speed (0x4f2110 calls back through
 * 0x6837b0): message::detail::GetMessageSpeedRate, 1 at the default setting,
 * 4 at the slowest and 0 at the fastest. Called from an update or Fix only,
 * never while the render objects are walked; the script may replace parts,
 * so the callers find their window again by its identity. */
static float message_speed_rate(void)
{
	static bool busy;
	if (busy || ain->version < 14)
		return 1;
	int fno = ain_get_function(ain, "message::detail::GetMessageSpeedRate");
	if (fno < 0 || ain->functions[fno].nr_args
			|| ain->functions[fno].return_type.data != AIN_FLOAT
			|| ain->functions[fno].address >= ain->code_size)
		return 1;
	busy = true;
	vm_call(fno, -1);
	float rate = stack_pop().f;
	busy = false;
	return isfinite(rate) ? rate : 1;
}

/* Lays the text out (one texture per glyph). Only the drawing reads the
 * result, so the setters just mark it changed: the dialogue text model
 * sets the text three to four times per line (GetMessageWindowText + append +
 * SetMessageWindowText), and laying the whole text out on every call took
 * about 1 ms each, up to 5 ms (research/gui-visual/pacing.md).
 * The interval between two characters is the speed times the rate,
 * truncated (0x4f21a3); 0 or less shows the text at once. */
static void message_window_layout(struct parts *parts, float rate)
{
	struct parts_message_window *mw = parts->message;
	struct text_style style = mw->text.ts;
	int line_space = mw->text.line_space;
	parts_text_free(&mw->text);
	memset(&mw->text, 0, sizeof(mw->text));
	mw->text.ts = style;
	mw->text.line_space = line_space;
	double speed = (double)mw->text_speed * rate;
	int interval = 0;
	if (ain->version >= 14)
		interval = speed >= INT_MAX ? INT_MAX : speed <= INT_MIN ? INT_MIN : (int)speed;
	struct string *plain = message_window_plain_text(mw, interval);
	mw->plain_bytes = plain->size;
	parts_text_append(parts, &mw->text, plain);
	free_string(plain);
	mw->text_changed = false;
	mw->rate_pending = false;
}

/* A property of the window differs: the original sets +0x208 and builds the
 * window again in its next update (0x4f25e0) or Fix (0x4f1f80). Until then
 * the text does not count as fixed (0x595709). */
static void message_window_dirty(struct parts *parts)
{
	parts->message->dirty = true;
	parts_dirty(parts);
}

static void message_window_rebuild(struct parts *parts)
{
	parts->message->text_changed = true;
	parts->message->rate_pending = ain->version >= 14 && parts->message->text_speed != 0;
	message_window_dirty(parts);
}

/* 0x595610 -> 0x4f2a60: the same text under the same message number changes
 * nothing. Otherwise the time of the text starts again from 0 (0x5bfa10) and
 * the old text is gone at once; the new one is built by the next update. */
void PE_SetMessageWindowText(int parts_no, struct string *text,
		int msg_num, possibly_unused struct string *func_name,
		possibly_unused int ver, possibly_unused int step)
{
	struct parts *parts = parts_get(parts_no);
	struct parts_message_window *mw = message_window_get(parts);
	if (mw->raw_text && mw->msg_num == msg_num
			&& mw->raw_text->size == (text ? text->size : 0)
			&& !memcmp(mw->raw_text->text, text ? text->text : "", mw->raw_text->size))
		return;
	mw->msg_num = msg_num;
	mw->elapsed = 0;
	mw->fixed = false;
	struct string *copy = text ? string_dup(text) : string_ref(&EMPTY_STRING);
	if (mw->raw_text)
		free_string(mw->raw_text);
	mw->raw_text = copy;
	message_window_rebuild(parts);
}

// 0x595960: a speed that differs is stored and the window marked changed.
void PE_SetMessageWindowTextSpeed(int parts_no, int speed)
{
	struct parts *parts = message_window_parts(parts_no);
	if (!parts || parts->message->text_speed == speed)
		return;
	parts->message->text_speed = speed;
	message_window_rebuild(parts);
}

// 0x595990: 0 without an object, 23 for a window just made.
int PE_GetMessageWindowTextSpeed(int parts_no)
{
	struct parts *parts = message_window_parts(parts_no);
	return parts ? parts->message->text_speed : 0;
}

static void key_wait_build(struct parts_key_wait *kw);

/* Takes the script's rate and lays the text out with it, when the text is
 * waiting for that. The script can free or replace the window meanwhile:
 * returns the window of that number and identity, or NULL. */
static struct parts *message_window_prepare(int parts_no, uint64_t identity)
{
	struct parts *parts = parts_try_get(parts_no);
	if (!parts || !parts->message || parts->message->identity != identity)
		return NULL;
	struct parts_message_window *mw = parts->message;
	if (!mw->text_changed && !mw->rate_pending)
		return parts;
	float rate = mw->raw_text && mw->text_speed ? message_speed_rate() : 1;
	parts = parts_try_get(parts_no);
	if (!parts || !parts->message || parts->message->identity != identity)
		return NULL;
	message_window_layout(parts, rate);
	return parts;
}

/* 0x5956f0: no object, or a window whose properties changed since its last
 * update (one just made among them), is not fixed; otherwise it is once no
 * run of the text is fading (0x566160). */
bool PE_IsFixedMessageWindowText(int parts_no)
{
	struct parts *parts = message_window_parts(parts_no);
	if (!parts)
		return false;
	struct parts_message_window *mw = parts->message;
	return !mw->dirty && !mw->text_changed && !mw->rate_pending && mw->fixed;
}

/* 0x5956b0: builds the window as an update would (0x4f1f80) and ends the
 * fade of every run (0x5660d0). The original builds unconditionally, asking
 * the script for its rate every time; here only what changed is built. */
void PE_FixMessageWindowText(int parts_no)
{
	struct parts *parts = message_window_parts(parts_no);
	if (!parts)
		return;
	parts = message_window_prepare(parts_no, parts->message->identity);
	if (!parts)
		return;
	struct parts_message_window *mw = parts->message;
	if (mw->key_wait.changed)
		key_wait_build(&mw->key_wait);
	mw->dirty = false;
	mw->fixed = true;
	parts_dirty(parts);
}

/* The alpha of one character of the window text (0x5bd205 .. 0x5bd297):
 * (time - start) * 0.005 between 0 and 1, so a character takes 200 ms. */
float parts_message_window_glyph_alpha(struct parts *parts, struct parts_text *text, int index)
{
	struct parts_message_window *mw = parts->message;
	if (!mw || text != &mw->text)
		return 1;
	if (mw->rate_pending)
		return 0;
	if (mw->fixed || index < 0 || index >= mw->nr_glyph_times)
		return 1;
	struct message_glyph_time *g = &mw->glyph_times[index];
	if (!g->fade)
		return 1;
	return fminf(1, fmaxf(0, (mw->elapsed - g->start) * 0.005f));
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
	bool changed = !mw->cg_name || strcmp(mw->cg_name->text, copy->text);
	if (mw->cg_name)
		free_string(mw->cg_name);
	mw->cg_name = copy;
	PE_SetPartsCG(parts_no, copy, 0, 1);
	parts_dirty(parts);
	if (changed)
		mw->dirty = true;
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
	struct parts_message_window *mw = message_window_get(parts);
	// 0x595730: dirty only when a value differs.
	if (mw->text_area.x != x || mw->text_area.y != y || mw->text_area.w != w || mw->text_area.h != h)
		mw->dirty = true;
	mw->text_area = (Rectangle){x, y, w, h};
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
	struct parts_message_window *mw = message_window_get(parts);
	// 0x5957f0
	if (mw->text_origin_mode != mode)
		mw->dirty = true;
	mw->text_origin_mode = mode;
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
 * number of 0 or less (0x539c80 at 0x539cad), so the setters do nothing and
 * the getters return 0, false or "" and leave their outputs. Each setter
 * writes and asks for a rebuild only when a value differs.
 * Not the original's: for a number above 0 that has no parts, or whose parts
 * is not a message window, the original's getters make it one as the setters
 * do (0x578f24 -> 0x578cb0, then 0x536c20) and give its defaults ("", 0, 0,
 * 0; 0x595410 writes its outputs whenever there is an object). Here the
 * getters make nothing and leave their outputs. The scripts read these only
 * from windows that exist (CMessageWindowParts). */

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
	message_window_dirty(parts_get(parts_no));
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
			// One missing frame fails the whole loop CG (0x5b30f0). The
			// original then keeps the frames it had loaded so far, and its
			// name, numbers and current frame are not set (0x5b32b1 ..
			// 0x5b32cc releases the failed CG alone); what it goes on to
			// draw from that was not followed. Here there is no mark.
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

/* Once per PartsEngine update: builds what changed in each window (0x4f25e0)
 * and advances its text and the loop CG of its mark. The time is the one the
 * window's parts selects (0x5352b1): the scaled time with スピードアップ有効,
 * as every message window of the game has, else the passed time. The window
 * hands it to its text and its mark as both (0x4f3318); the text adds it to
 * its own time (0x564c80 -> 0x565376 -> 0x5bfa00). */
void parts_message_window_update_times(int passed_time, int scaled_passed_time)
{
	// The script's rate callback can free, replace or create a window:
	// walk a snapshot of numbers and identities, not the list.
	struct {
		int no;
		uint64_t identity;
	} *windows = NULL;
	int nr_windows = 0;
	struct parts *parts;
	PARTS_LIST_FOREACH(parts) {
		if (!parts->message)
			continue;
		windows = xrealloc_array(windows, nr_windows, nr_windows + 1, sizeof(*windows));
		windows[nr_windows].no = parts->no;
		windows[nr_windows].identity = parts->message->identity;
		nr_windows++;
	}
	for (int i = 0; i < nr_windows; i++) {
		parts = message_window_prepare(windows[i].no, windows[i].identity);
		if (!parts)
			continue;
		struct parts_message_window *mw = parts->message;
		int time = parts->speed_up_disabled ? passed_time : scaled_passed_time;
		bool fading = !mw->fixed;
		if (time > 0 && mw->elapsed < INT64_MAX - time)
			mw->elapsed += time;
		// With every character at full alpha the fade is over. The text
		// widget's update builds its draw items with the new time whether
		// the window is shown or not (0x565200 -> 0x5b8020 -> 0x5bf2c0),
		// and that ends the fade of each run (0x5bcf60 at 0x5bd36a).
		if (fading && mw->elapsed >= mw->fade_end)
			mw->fixed = true;
		struct parts_key_wait *kw = &mw->key_wait;
		bool redraw = kw->changed || fading;
		if (kw->changed)
			key_wait_build(kw);
		if (key_wait_advance(kw, time) && kw->show)
			redraw = true;
		mw->dirty = false;
		if (redraw)
			parts_dirty(parts);
	}
	free(windows);
}

void parts_message_window_update(int passed_time)
{
	parts_message_window_update_times(passed_time, passed_time);
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
	if (mw && mw->text_changed) {
		// A drawing or a size query can come before the update. The
		// script's rate is not asked for here: the text is laid out with
		// a placeholder and stays hidden until an update or Fix lays it
		// out again.
		bool pending = mw->rate_pending;
		message_window_layout(parts, 1);
		mw->rate_pending = pending;
	}
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
