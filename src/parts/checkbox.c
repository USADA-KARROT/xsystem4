/* Copyright (C) 2026 Nunuhara Cabbage <nunuhara@haniwa.technology>
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
#include <stdlib.h>
#include <string.h>

#include "system4.h"
#include "system4/ain.h"
#include "system4/cg.h"
#include "system4/string.h"

#include "asset_manager.h"
#include "gfx/font.h"
#include "vm.h"
#include "xsystem4.h"
#include "parts.h"
#include "parts_internal.h"

/*
 * The v14 檢查ボックス (component type 1, constructor 0x52d000) and ラジオ按鈕
 * ボックス (type 9, 0x56c550).
 *
 * The original check box is a widget with two inner parts, both low-level
 * parts that can be clicked: the box, a CG for each of the three states, and
 * the text, a construction of commands 0, 5 and 8 (0x52e8a0, 0x52e630). The
 * widget places them every update (0x52d340) and switches both to the
 * on-cursor and the down state together (0x52ce50, 0x52cd10). Here there are
 * no inner parts: the box and the text are drawn into the three states of
 * the parts itself, as this port's button has its CGs there. The pixels and
 * the area that takes the cursor are the same (the text's surface covers the
 * whole widget and the box lies inside it); the states are saved as the CGs
 * and constructions they are, the values below are not.
 *
 * Not the original's: a check box without a CG, or with one that is not
 * there, is drawn by the engine there (0x52d730, a panel in 按鈕の色 with the
 * text on it); here it keeps the states it has. The game's boxes all have
 * their CGs. フラット名 is kept and not shown.
 */

static bool checkbox_any_dirty;

/* The properties, with the constructor's values (0x529620) the first time. */
struct parts_checkbox *parts_get_checkbox(struct parts *parts)
{
	if (!parts->checkbox) {
		struct parts_checkbox *cb = xcalloc(1, sizeof(struct parts_checkbox));
		*cb = (struct parts_checkbox) {
			.w = 16, .h = 16,
			.enable = true,
			.r = 200, .g = 200, .b = 200,
			.font = { .size = 16, .r = 255, .g = 255, .b = 255 },
			.text_pos = 4,
		};
		parts->checkbox = cb;
	}
	return parts->checkbox;
}

struct parts_radio_box *parts_get_radio_box(struct parts *parts)
{
	if (!parts->radio_box)
		parts->radio_box = xcalloc(1, sizeof(struct parts_radio_box));
	return parts->radio_box;
}

static void checkbox_forget_clicks(int no);

static void checkbox_free(struct parts *parts)
{
	struct parts_checkbox *cb = parts->checkbox;
	if (!cb)
		return;
	// A click that was not applied yet must not reach whatever takes the
	// number next.
	checkbox_forget_clicks(parts->no);
	free(cb->cg_name);
	free(cb->over_cg_name);
	free(cb->over_checked_cg_name);
	free(cb->flat_name);
	free(cb->over_flat_name);
	free(cb->over_checked_flat_name);
	free(cb->text);
	free(cb);
	parts->checkbox = NULL;
}

static void radio_box_free(struct parts *parts)
{
	if (!parts->radio_box)
		return;
	free(parts->radio_box->children);
	free(parts->radio_box);
	parts->radio_box = NULL;
}

void parts_widget_free(struct parts *parts)
{
	checkbox_free(parts);
	radio_box_free(parts);
}

/*
 * SetComponentType gave the parts another widget type. The original drops
 * the widget it had and makes the new one with its constructor's values
 * (0x535e20), so a check box or a radio box that became something else has
 * lost its values when it becomes one again, and a parts that becomes a
 * 橫滾動條 scrolls 100 by 1 (parts_scrollbar_construct). What the parts
 * shows is kept here, where the original's new widget shows nothing of the
 * old one.
 */
void parts_widget_type_changed(struct parts *parts, int old_type)
{
	const int type = parts->component_type;
	if (type == old_type)
		return;
	if (old_type == PARTS_COMPONENT_CHECKBOX)
		checkbox_free(parts);
	if (old_type == PARTS_COMPONENT_RADIO_BOX)
		radio_box_free(parts);
	if (type == PARTS_COMPONENT_CHECKBOX)
		parts_get_checkbox(parts);
	if (type == PARTS_COMPONENT_RADIO_BOX)
		parts_get_radio_box(parts);
	if (type == PARTS_COMPONENT_HSCROLLBAR)
		parts_scrollbar_construct(parts);
}

/* A string property of the check box: NULL for "". */
void parts_checkbox_set_string(struct parts *parts, char **field, const char *value)
{
	free(*field);
	*field = value && value[0] ? xstrdup(value) : NULL;
	parts_get_checkbox(parts)->dirty = true;
	checkbox_any_dirty = true;
}

/*
 * The CG of the box in a state (0 普通, 1 オン, 2 ダウン, 3 無効), "" when the
 * box has none. 0x529c40 .. 0x529d60 for a box that is not checked: nothing
 * without a ＣＧ名, whatever 覆蓋ＣＧ名 says; else 覆蓋ＣＧ名, or ＣＧ名 when
 * that is empty, and the state's suffix. 0x529dc0 .. 0x52a150 for a checked
 * one: nothing without a ＣＧ名; else 覆蓋檢查ＣＧ名, or ＣＧ名 with ／檢查
 * when that is empty, and the suffix. The suffixes are the GBK strings of
 * the CN executable (0x7d863c ..); an SJIS release's ／檢查 is not known, and
 * neither are its pactex keys, so a box is only ever named by a GBK pactex.
 */
static void checkbox_cg_name(const struct parts_checkbox *cb, bool checked, int state,
		char *out, size_t size)
{
	static const char *const suffix[] = {
		"\xa3\xaf\xc6\xd5\xcd\xa8",         // ／普通
		"\xa3\xaf\xa5\xaa\xa5\xf3",         // ／オン
		"\xa3\xaf\xa5\xc0\xa5\xa6\xa5\xf3", // ／ダウン
		"\xa3\xaf\x9f\x6f\x84\xbf",         // ／無効
	};
	out[0] = '\0';
	if (!cb->cg_name)
		return;
	if (!checked)
		snprintf(out, size, "%s%s", cb->over_cg_name ? cb->over_cg_name : cb->cg_name,
				suffix[state]);
	else if (cb->over_checked_cg_name)
		snprintf(out, size, "%s%s", cb->over_checked_cg_name, suffix[state]);
	else
		snprintf(out, size, "%s\xa3\xaf\x99\x7a\xb2\xe9%s", cb->cg_name, suffix[state]); // ／檢查
}

static struct text_style checkbox_text_style(const struct parts_checkbox *cb)
{
	return (struct text_style) {
		.face = cb->font.type,
		.size = cb->font.size,
		.bold_width = cb->font.bold,
		.edge_left = cb->font.edge,
		.edge_up = cb->font.edge,
		.edge_right = cb->font.edge,
		.edge_down = cb->font.edge,
		.color = { cb->font.r, cb->font.g, cb->font.b, 255 },
		.edge_color = { cb->font.edge_r, cb->font.edge_g, cb->font.edge_b, 255 },
		.scale_x = 1.0f,
		.space_scale_x = 1.0f,
		.font_spacing = cb->char_space,
	};
}

/*
 * The size of the text (0x528aa0), 0 x 0 for no text. A line is the font's
 * size made even plus 2e high, e as for every text on the CN glyph grid
 * (gfx_text_cn_edge); a "\n" adds the line spacing and a line; a character
 * adds the character spacing and its cell (0x69da70), a byte of 0x81 .. 0xFE
 * taking the next one with it. At the end one character spacing is taken off
 * the width and one line spacing off the height: a single line of size 19
 * with 行間隔 7 is 13 high, which is how the game centres its captions on a
 * box of 23.
 * Where the glyphs are not on that grid (an SJIS game, or one with a font
 * library) the original still runs this with those glyphs' widths; here it
 * is the one line construction command 8 draws there, and was not verified.
 */
static void checkbox_measure_text(const struct parts_checkbox *cb, int *w, int *h)
{
	*w = *h = 0;
	if (!cb->text)
		return;
	if (!gfx_text_cn_gdi()) {
		struct text_style ts = checkbox_text_style(cb);
		*w = ceilf(gfx_size_text(&ts, cb->text));
		*h = ceilf(ts.size + ts.edge_up + ts.edge_down);
		return;
	}
	const int size = cb->font.size;
	const int e = gfx_text_cn_edge(size, cb->font.bold, cb->font.edge);
	const int line = size + (size & 1) + 2 * e;
	int width = 0, row = 0, height = line;
	for (const uint8_t *p = (const uint8_t *)cb->text; *p; p++) {
		if (*p == '\n') {
			height += cb->line_space + line;
			width = max(width, row);
			row = 0;
			continue;
		}
		row += cb->char_space + gfx_text_cn_width(size, *p) + 2 * e;
		if (*p >= 0x81 && *p <= 0xfe && p[1])
			p++;
	}
	*w = max(width, row) - cb->char_space;
	*h = height - cb->line_space;
}

/* One state of a box with a text: the surface of the original's text parts
 * with the box's CG put where the box parts lies on it. */
static void checkbox_build_state(struct parts *parts, int state, const char *cg_name,
		int w, int h, int box_y, int text_x, int text_y)
{
	const struct parts_checkbox *cb = parts->checkbox;
	const int no = parts->no;
	struct cg_metrics cg;
	if (!asset_cg_get_metrics_by_name(cg_name, &cg))
		return;
	parts_clear_construction_process(parts_get_construction_process(parts, state));
	struct string *name = cstr_to_string(cg_name);
	struct string *text = cstr_to_string(cb->text);
	PE_AddCreateToPartsConstructionProcess(no, w, h, state + 1);
	PE_AddFillAMapToPartsConstructionProcess(no, 0, 0, w, h, 0, state + 1);
	PE_AddCopyCutCGToPartsConstructionProcess(no, name, 0, box_y, cg.w, cg.h,
			0, 0, cg.w, cg.h, 0, state + 1);
	PE_AddCopyTextToPartsConstructionProcess(no, text_x, text_y, text,
			cb->font.type, cb->font.size, cb->font.r, cb->font.g, cb->font.b, cb->font.bold,
			cb->font.edge_r, cb->font.edge_g, cb->font.edge_b, cb->font.edge,
			cb->char_space, cb->line_space, state + 1);
	PE_BuildPartsConstructionProcess(no, state + 1);
	free_string(name);
	free_string(text);
}

/*
 * The states of the check box from its values (0x52e8a0, run by the widget's
 * update while +0x1e8 is set).
 *
 * The box: a CG per state, by whether the box is checked; a box that is not
 * enabled shows ／無効 in all three (0x52dd80, 0x52e420). Here a state whose
 * CG is not there shows the normal one.
 *
 * The text, unless 按鈕模式 is set or the text has no size (0x52e630): with
 * the box's normal CG bw x bh and the text tw x th, a surface of bw + tw by
 * max(bh, th) (the gap at +0x74 is 0: only the constructor writes it),
 * cleared to alpha 0 (commands 0 and 5), and the text copied to (bw, y) by
 * command 8, y = (bh - th) / 2 when the text is lower than the box, else 0.
 * The widget is as large as that surface (0x52eb90) and puts the box at
 * H / 2 - bh / 2 on it (0x52d340). The surface is not made higher for the
 * glyphs: what a caption of the game has below it is cut, as in the original.
 */
void parts_checkbox_rebuild(struct parts *parts)
{
	struct parts_checkbox *cb = parts_get_checkbox(parts);
	cb->dirty = false;
	// The input takes a disabled box as it takes a disabled button: the
	// widget passes no MouseClick on (0x52ccbc), nothing changes its value
	// (parts_checkbox_set_checked) and its inner parts lose their sounds
	// (0x52d560).
	parts->button_disabled = !cb->enable;

	char names[PARTS_NR_STATES][512];
	struct cg_metrics box;
	for (int i = 0; i < PARTS_NR_STATES; i++)
		checkbox_cg_name(cb, cb->checked, cb->enable ? i : 3, names[i], sizeof(names[i]));
	if (!names[0][0] || !asset_cg_get_metrics_by_name(names[0], &box))
		return;
	for (int i = 1; i < PARTS_NR_STATES; i++) {
		if (!asset_exists_by_name(ASSET_CG, names[i], NULL))
			strcpy(names[i], names[0]);
	}

	int tw = 0, th = 0;
	if (!cb->button_mode)
		checkbox_measure_text(cb, &tw, &th);
	if (tw <= 0 || th <= 0) {
		for (int i = 0; i < PARTS_NR_STATES; i++) {
			struct string *name = cstr_to_string(names[i]);
			PE_SetPartsCG(parts->no, name, 0, i + 1);
			free_string(name);
		}
		return;
	}
	const int w = box.w + tw;
	const int h = max(box.h, th);
	const int box_y = h / 2 - box.h / 2;
	const int text_y = th < box.h ? (box.h - th) / 2 : 0;
	for (int i = 0; i < PARTS_NR_STATES; i++)
		checkbox_build_state(parts, i, names[i], w, h, box_y, box.w, text_y);
}

/*
 * The click of the left button on the box: the value becomes the other one.
 * The original does this when the button is released with the cursor still
 * on the widget (0x52cdd0, after the inner parts' MouseClick and KeyUp went
 * to the scripts); this port's v14 input sends the MouseClick on the press
 * and calls this right after it.
 *
 * The value does not change here but at the next update of the parts
 * (parts_checkbox_update, in PE_Update). The settings write the
 * configuration to every box before PartsEngine.UpdateComponent and read the
 * boxes back after it, so a click has to change the box between the two, as
 * the original's input update does; this port also looks at the mouse in
 * system.Peek, outside that bracket, and a value changed there would be
 * written over by the next frame's configuration before it was ever read.
 *
 * What this makes different from the original:
 *  - A press seen by system.Peek sends its MouseClick at once, and the
 *    script handles it in that frame, with the box still at its old value;
 *    the new value and ChangedFlg follow in the next frame's update. In the
 *    original all three belong to one input update, and a handler of the
 *    MouseClick already reads the new value. (A press seen by the update
 *    itself is like the original's.) None of the game's MouseClick handlers
 *    on a check box reads the box: they set a setting to a fixed value.
 *  - The value changes on the press, so while the button is held the box
 *    shows the pressed picture of the new value; the original shows the old
 *    value's until the release.
 *  - A press that is then dragged off the box and released there has
 *    already changed the value. In the original it changes nothing: the
 *    release looks at the cursor (0x52ce03 .. 0x52ce1d return when no inner
 *    parts is under it). This goes with sending a target's click on the
 *    press; when that moves to the release, this does too.
 *
 * A click is remembered with the number and the event ID the parts had, and
 * is dropped when the parts is released, stops being a check box or has
 * another event ID by the time of the update, and when the input is reset.
 * One press makes one click; the room for 8 is not expected to run out, and
 * a click beyond it is dropped with a warning.
 */
#define CHECKBOX_MAX_CLICKS 8
static struct checkbox_click {
	int no;
	int unique_id;
} checkbox_clicks[CHECKBOX_MAX_CLICKS];
static int checkbox_nr_clicks;

void parts_checkbox_clicked(struct parts *parts)
{
	if (checkbox_nr_clicks >= CHECKBOX_MAX_CLICKS) {
		WARNING("check box %d: too many clicks before an update", parts->no);
		return;
	}
	checkbox_clicks[checkbox_nr_clicks++] = (struct checkbox_click) {
		.no = parts->no,
		.unique_id = parts->unique_id,
	};
}

static void checkbox_forget_clicks(int no)
{
	int n = 0;
	for (int i = 0; i < checkbox_nr_clicks; i++) {
		if (checkbox_clicks[i].no != no)
			checkbox_clicks[n++] = checkbox_clicks[i];
	}
	checkbox_nr_clicks = n;
}

// With the input (parts_reset_input: PE_Init and PE_Reset).
void parts_checkbox_reset_clicks(void)
{
	checkbox_nr_clicks = 0;
}

/* Once per update, after the input: the boxes that were clicked take their
 * other value, and the boxes whose values changed are drawn again. */
void parts_checkbox_update(void)
{
	if (ain->version < 14)
		return;
	// Setting a value neither records nor forgets a click.
	const int nr_clicks = checkbox_nr_clicks;
	checkbox_nr_clicks = 0;
	for (int i = 0; i < nr_clicks; i++) {
		const struct checkbox_click *click = &checkbox_clicks[i];
		struct parts *clicked = parts_try_get(click->no);
		if (clicked && clicked->component_type == PARTS_COMPONENT_CHECKBOX && clicked->checkbox
				&& clicked->unique_id == click->unique_id)
			parts_checkbox_set_checked(clicked, !clicked->checkbox->checked);
	}
	if (!checkbox_any_dirty)
		return;
	checkbox_any_dirty = false;
	struct parts *parts;
	PARTS_LIST_FOREACH(parts) {
		if (parts->component_type == PARTS_COMPONENT_CHECKBOX && parts->checkbox
				&& parts->checkbox->dirty)
			parts_checkbox_rebuild(parts);
	}
}

static bool radio_box_has(const struct parts_radio_box *rb, int no)
{
	for (int i = 0; i < rb->nr_children; i++) {
		if (rb->children[i] == no)
			return true;
	}
	return false;
}

/* 檢查ボックスリスト of the pactex (0x56c170): the number goes to the end of
 * the list, taken out first where it already was. */
void parts_radio_box_add(struct parts *box, int child)
{
	struct parts_radio_box *rb = parts_get_radio_box(box);
	int n = 0;
	for (int i = 0; i < rb->nr_children; i++) {
		if (rb->children[i] != child)
			rb->children[n++] = rb->children[i];
	}
	rb->children = xrealloc(rb->children, (n + 1) * sizeof(int));
	rb->children[n] = child;
	rb->nr_children = n + 1;
}

/*
 * A check box of the radio box changed (0x56c3c0, from 0x56c380 for an event
 * of type 23). The box does not handle what it changes itself (+0x5c). A
 * member that was unchecked is checked again: the checked one of a group
 * cannot be clicked off. One that was checked tells the box's own observers
 * (type 22, Changed, no variables) and unchecks every other member that
 * exists; the original makes each of those a check box first (0x535e20),
 * whatever it was.
 */
static void radio_box_child_changed(struct parts *box, struct parts *source)
{
	struct parts_radio_box *rb = box->radio_box;
	if (rb->busy)
		return;
	rb->busy = true;
	if (!parts_get_checkbox(source)->checked) {
		parts_checkbox_set_checked(source, true);
	} else {
		if (box->delegate_index >= 0)
			parts_enqueue_message(22, box->no, box->delegate_index, box->unique_id);
		for (int i = 0; i < rb->nr_children; i++) {
			struct parts *child = parts_try_get(rb->children[i]);
			if (!child || child == source)
				continue;
			if (child->component_type != PARTS_COMPONENT_CHECKBOX)
				PE_SetComponentType(child->no, PARTS_COMPONENT_CHECKBOX, 1);
			parts_checkbox_set_checked(child, false);
		}
	}
	rb->busy = false;
}

/* The radio boxes the check box belongs to. The original keeps them as the
 * observers of the box's second emitter (+0x40; 0x56c7a0 puts a radio box
 * there for each of its members); here they are found by their lists. */
static void checkbox_notify_radio_boxes(struct parts *source)
{
	struct parts *box;
	PARTS_LIST_FOREACH(box) {
		if (box->component_type == PARTS_COMPONENT_RADIO_BOX && box->radio_box
				&& radio_box_has(box->radio_box, source->no))
			radio_box_child_changed(box, source);
	}
}

/*
 * Checks or unchecks the box (0x52f030), for the script's CheckBoxChecked,
 * a click and a radio box alike. A box that is not enabled keeps its value.
 * A value that changes is told to the radio boxes first, which may set it
 * back; only when it then still differs from what it was do the scripts get
 * ChangedFlg (message 23, the value that was asked for). Setting the value
 * it has does nothing, so the settings pages can write every box each frame.
 * A parts no script gave an event has nobody to tell, as for the scroll
 * bar's message (CPartsMessageManager@CallDelegate drops a message without
 * a delegate index).
 */
void parts_checkbox_set_checked(struct parts *parts, bool checked)
{
	struct parts_checkbox *cb = parts_get_checkbox(parts);
	if (!cb->enable)
		return;
	const bool old = cb->checked;
	if (old != checked) {
		cb->checked = checked;
		cb->dirty = true;
		checkbox_any_dirty = true;
		checkbox_notify_radio_boxes(parts);
	}
	if (old != cb->checked && parts->delegate_index >= 0) {
		const int vars[1] = { checked };
		parts_enqueue_message_vars(23, parts->no, parts->delegate_index,
				parts->unique_id, 1, vars);
	}
}

/* SetCheckBoxEnable (0x591120): a flag that changes has the box drawn again,
 * with ／無効 while it is clear. */
void parts_checkbox_set_enable(struct parts *parts, bool enable)
{
	struct parts_checkbox *cb = parts_get_checkbox(parts);
	if (cb->enable == enable)
		return;
	cb->enable = enable;
	cb->dirty = true;
	checkbox_any_dirty = true;
	parts->button_disabled = !enable;
}
