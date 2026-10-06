/* Copyright (C) 2026 kichikuou <KichikuouChrome@gmail.com>
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

#include <limits.h>
#include <math.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#include "system4/ain.h"
#include "system4/flat.h"

#include "parts.h"
#include "parts_internal.h"
#include "vm.h"

void PE_SetComponentMargin(int parts_no, int top, int bottom, int left, int right)
{
	struct parts *parts = parts_get(parts_no);
	bool changed = parts->margin_top != top || parts->margin_bottom != bottom
		|| parts->margin_left != left || parts->margin_right != right;
	parts->margin_top = top;
	parts->margin_bottom = bottom;
	parts->margin_left = left;
	parts->margin_right = right;
	if (changed)
		parts_layout_size_changed(parts);
}

int PE_GetComponentMarginTop(int parts_no)
{
	return parts_get(parts_no)->margin_top;
}

int PE_GetComponentMarginBottom(int parts_no)
{
	return parts_get(parts_no)->margin_bottom;
}

int PE_GetComponentMarginLeft(int parts_no)
{
	return parts_get(parts_no)->margin_left;
}

int PE_GetComponentMarginRight(int parts_no)
{
	return parts_get(parts_no)->margin_right;
}

void PE_SetLayoutBoxLayoutType(int parts_no, int type)
{
	struct parts *parts = parts_get(parts_no);
	struct parts_layout_box *lb = parts_get_layout_box(parts);
	if (lb->layout_type != type) {
		lb->layout_type = type;
		parts_component_dirty(parts);
	}
}

int PE_GetLayoutBoxLayoutType(int parts_no)
{
	struct parts *parts = parts_try_get(parts_no);
	if (!parts || parts->states[0].type != PARTS_LAYOUT_BOX)
		return PARTS_LAYOUT_FREE;
	return parts->states[0].layout_box.layout_type;
}

void PE_SetLayoutBoxReturn(int parts_no, bool return_flag, int return_size)
{
	struct parts *parts = parts_get(parts_no);
	struct parts_layout_box *lb = parts_get_layout_box(parts);
	lb->wrap = return_flag;
	lb->wrap_size = return_size;
	parts_component_dirty(parts);
}

bool PE_IsLayoutBoxReturn(int parts_no)
{
	struct parts *parts = parts_try_get(parts_no);
	if (!parts || parts->states[0].type != PARTS_LAYOUT_BOX)
		return false;
	return parts->states[0].layout_box.wrap;
}

int PE_GetLayoutBoxReturnSize(int parts_no)
{
	struct parts *parts = parts_try_get(parts_no);
	if (!parts || parts->states[0].type != PARTS_LAYOUT_BOX)
		return 0;
	return parts->states[0].layout_box.wrap_size;
}

/*
 * v14 shape: void SetLayoutBoxReturn(int Number, bool Return, float ReturnSize)
 *            float GetLayoutBoxReturnSize(int Number)
 * Native (PartsEngine case 481 @0x5828db -> 0x594d80, case 483 @0x58294f ->
 * 0x594df0) keeps ReturnSize as a float (+0x48), marks the box dirty only when
 * a value changes, is a no-op on an unknown parts and returns 0.0f for it.
 * The int prototypes above read the float from an integer register (arm64:
 * s2 vs w2) and return an int in w0 where the caller reads s0.
 *
 * The layout code only tests "extent > wrap_size" with integer extents, and
 * for integer x, x > f  <=>  x > floor(f), so floorf() keeps the native
 * comparison. The getter loses the fraction; CN only feeds ITOF'd ints
 * (parts::detail::CLayoutBoxParts@SetReturn, fno 12029), and
 * SetLayoutBoxReturnSizeForRate is still a stub.
 */
void PE_SetLayoutBoxReturnF(int parts_no, bool return_flag, float return_size)
{
	struct parts *parts = parts_try_get(parts_no);
	if (!parts)
		return;
	struct parts_layout_box *lb = parts_get_layout_box(parts);
	int size;
	if (isnan(return_size))
		size = 0;
	else if (return_size >= 2147483647.0f)
		size = INT_MAX;
	else if (return_size <= -2147483648.0f)
		size = INT_MIN;
	else
		size = (int)floorf(return_size);
	if (lb->wrap != return_flag || lb->wrap_size != size) {
		lb->wrap = return_flag;
		lb->wrap_size = size;
		parts_component_dirty(parts);
	}
}

float PE_GetLayoutBoxReturnSizeF(int parts_no)
{
	return (float)PE_GetLayoutBoxReturnSize(parts_no);
}

void *pe_layoutbox_select_function(const struct ain_hll_function *f, void *dflt)
{
	if (!strcmp(f->name, "SetLayoutBoxReturn") && f->nr_arguments == 3
	    && f->arguments[2].type.data == AIN_FLOAT)
		return PE_SetLayoutBoxReturnF;
	if (!strcmp(f->name, "GetLayoutBoxReturnSize") && f->return_type.data == AIN_FLOAT)
		return PE_GetLayoutBoxReturnSizeF;
	return dflt;
}

void PE_SetLayoutBoxAlign(int parts_no, int align)
{
	struct parts *parts = parts_get(parts_no);
	struct parts_layout_box *lb = parts_get_layout_box(parts);
	if (lb->align != align) {
		lb->align = align;
		parts_component_dirty(parts);
	}
}

int PE_GetLayoutBoxAlign(int parts_no)
{
	struct parts *parts = parts_try_get(parts_no);
	if (!parts || parts->states[0].type != PARTS_LAYOUT_BOX)
		return 0;
	return parts->states[0].layout_box.align;
}

void PE_set_layoutbox_padding(int parts_no, int top, int bottom, int left, int right)
{
	struct parts *parts = parts_get(parts_no);
	struct parts_layout_box *lb = parts_get_layout_box(parts);
	lb->padding_top = top;
	lb->padding_bottom = bottom;
	lb->padding_left = left;
	lb->padding_right = right;
	parts_component_dirty(parts);
}

// v14 SetLayoutBoxPadding (0x594ec0): an unknown number is ignored
// (0x53ded0 finds no parts), and the box is marked for layout only when a
// value changes.
void PE_v14_SetLayoutBoxPadding(int parts_no, int top, int bottom, int left, int right)
{
	struct parts *parts = parts_try_get(parts_no);
	if (!parts)
		return;
	struct parts_layout_box *lb = parts_get_layout_box(parts);
	if (lb->padding_top == top && lb->padding_bottom == bottom
			&& lb->padding_left == left && lb->padding_right == right)
		return;
	lb->padding_top = top;
	lb->padding_bottom = bottom;
	lb->padding_left = left;
	lb->padding_right = right;
	parts_component_dirty(parts);
}

int PE_get_layoutbox_padding_top(int parts_no)
{
	struct parts *parts = parts_try_get(parts_no);
	if (!parts || parts->states[0].type != PARTS_LAYOUT_BOX)
		return 0;
	return parts->states[0].layout_box.padding_top;
}

int PE_get_layoutbox_padding_bottom(int parts_no)
{
	struct parts *parts = parts_try_get(parts_no);
	if (!parts || parts->states[0].type != PARTS_LAYOUT_BOX)
		return 0;
	return parts->states[0].layout_box.padding_bottom;
}

int PE_get_layoutbox_padding_left(int parts_no)
{
	struct parts *parts = parts_try_get(parts_no);
	if (!parts || parts->states[0].type != PARTS_LAYOUT_BOX)
		return 0;
	return parts->states[0].layout_box.padding_left;
}

int PE_get_layoutbox_padding_right(int parts_no)
{
	struct parts *parts = parts_try_get(parts_no);
	if (!parts || parts->states[0].type != PARTS_LAYOUT_BOX)
		return 0;
	return parts->states[0].layout_box.padding_right;
}

static void parts_get_layout_size(struct parts *parts, int *w, int *h)
{
	struct parts_state *state = &parts->states[parts->state];
	if (state->type == PARTS_FLAT && state->flat.flat) {
		// The layout size of Flat is specified in the header, and this may
		// differ from the hitbox size.
		*w = state->flat.flat->hdr.width;
		*h = state->flat.flat->hdr.height;
	} else {
		*w = state->common.w;
		*h = state->common.h;
	}
}

static int align_offset_x(int align, int total_w)
{
	switch (align) {
	case 2: case 5: case 8: return -(total_w / 2);
	case 3: case 6: case 9: return -total_w;
	default: return 0;
	}
}

static int align_offset_y(int align, int total_h)
{
	switch (align) {
	case 4: case 5: case 6: return -(total_h / 2);
	case 7: case 8: case 9: return -total_h;
	default: return 0;
	}
}

// Calculates the total size of a horizontal layout box by summing child widths per row.
static void parts_layout_calc_size_horizontal(struct parts_layout_box *lb, struct parts *parts,
		int *out_w, int *out_h)
{
	int row_w = 0, max_row_w = 0, max_row_h = 0, total_h = 0;
	struct parts *child;
	PARTS_FOREACH_CHILD(child, parts) {
		int child_w, child_h;
		parts_get_layout_size(child, &child_w, &child_h);
		int cw = child->margin_left + child_w + child->margin_right;
		int ch = child->margin_top + child_h + child->margin_bottom;
		if (lb->wrap && row_w + cw > lb->wrap_size && row_w > 0) {
			if (row_w > max_row_w)
				max_row_w = row_w;
			total_h += max_row_h;
			row_w = 0;
			max_row_h = 0;
		}
		row_w += cw;
		if (ch > max_row_h)
			max_row_h = ch;
	}
	if (row_w > max_row_w)
		max_row_w = row_w;
	total_h += max_row_h;
	*out_w = max_row_w + lb->padding_left + lb->padding_right;
	*out_h = total_h + lb->padding_top + lb->padding_bottom;
}

// Calculates the total size of a vertical layout box by summing child heights per column.
static void parts_layout_calc_size_vertical(struct parts_layout_box *lb, struct parts *parts,
		int *out_w, int *out_h)
{
	int col_h = 0, max_col_h = 0, max_col_w = 0, total_w = 0;
	struct parts *child;
	PARTS_FOREACH_CHILD(child, parts) {
		int child_w, child_h;
		parts_get_layout_size(child, &child_w, &child_h);
		int cw = child->margin_left + child_w + child->margin_right;
		int ch = child->margin_top + child_h + child->margin_bottom;
		if (lb->wrap && col_h + ch > lb->wrap_size && col_h > 0) {
			if (col_h > max_col_h)
				max_col_h = col_h;
			total_w += max_col_w;
			col_h = 0;
			max_col_w = 0;
		}
		col_h += ch;
		if (cw > max_col_w)
			max_col_w = cw;
	}
	if (col_h > max_col_h)
		max_col_h = col_h;
	total_w += max_col_w;
	*out_w = total_w + lb->padding_left + lb->padding_right;
	*out_h = max_col_h + lb->padding_top + lb->padding_bottom;
}

/*
 * v14 (Dohna Dohna) layout boxes, after the original box widget (vtable
 * 0x80f8ec): the layout 0x5498e0 -> 0x549960 dispatches on the type to
 * 0x5499e0 (free), 0x549bd0 (vertical) and 0x54a200 (horizontal); GetSize
 * 0x54a9f0 to 0x54aaf0 / 0x54adc0 / 0x54b0d0. The shape is the one below,
 * plus:
 *   - the effective alignment (0x54b4c0): a vertical box takes 4/5/6 as 1
 *     (when wrapping also 2/8), a horizontal box 2/5/8 (also 4/6). It sets
 *     the start, the margins and the children's origin mode;
 *   - padding like the margins: left/top +, right/bottom -, a centred axis 0;
 *   - only a child that is shown (+0xab) and not hidden in the editor
 *     (+0xac) takes space; the others are placed but do not advance;
 *   - a child takes its normal state's size (GetSize(1)), see
 *     lb_v14_child_size;
 *   - a box whose width or height is not positive has size 0x0 (0x54aab1);
 *     a vertical/horizontal box writes its size back to its state, for
 *     GetPartsWidth/Height and for the box around it;
 *   - a row/column wraps without waiting for a non-empty one (the same
 *     result unless it starts with zero-size children).
 * The original calls the box every frame (0x535adc -> 0x5498e0) and lays
 * it out whenever the box is shown (its accumulated show flag, 0x549970;
 * set at 0x579c81), or, while hidden, when a parts of its subtree is another
 * parts' アルファクリッパー (0x534d50; the set is filled by 0x53a2d0). Here
 * a change of something the layout reads marks the box dirty instead
 * (parts_layout_size_changed): a child's normal state size, text, shown
 * flags, margins, parent, position or origin mode, and the box's own
 * properties and origin mode. A hidden box is laid out as well.
 * Not implemented:
 *   - a free box's offset of its children by its own origin (+0x238/+0x23c,
 *     0x5499e0). Every free box in the game's pactex has origin mode 1, but
 *     a user component writes its own origin mode to its first child, the
 *     content's root, on every frame (0x4e57d0), and 30 user components have
 *     another one (e.g. the garage's information panel, origin mode 3, which
 *     therefore extends to the right of its position instead of the left);
 *   - a child's message-window link (+0xad; 0 for every component in the
 *     game) and link condition (0x537f10; no child of a vertical/horizontal
 *     box in the game has one, only children of free boxes on the settings
 *     pages);
 *   - 折り返しサイズをレートとして認識する, and the change of the wrap size
 *     with an anchored box's size (0x5498e4; no component has an anchor).
 * The size a script reads (GetPartsWidth/Height) is the one of the last
 * layout, and a free box or a user component still reports 0 there (the
 * original computes all of them on the call, 0x58c620 -> 0x4db840).
 */
static bool lb_v14_is_box(struct parts *parts)
{
	return parts->states[0].type == PARTS_LAYOUT_BOX;
}

static bool lb_v14_lays_out(struct parts *parts)
{
	if (!lb_v14_is_box(parts))
		return false;
	enum parts_layout_type type = parts->states[0].layout_box.layout_type;
	return type == PARTS_LAYOUT_VERTICAL || type == PARTS_LAYOUT_HORIZONTAL;
}

static int lb_v14_align(struct parts_layout_box *lb)
{
	int a = lb->align;
	if (lb->layout_type == PARTS_LAYOUT_VERTICAL) {
		if (a == 4 || a == 5 || a == 6 || (lb->wrap && (a == 2 || a == 8)))
			return 1;
	} else if (lb->layout_type == PARTS_LAYOUT_HORIZONTAL) {
		if (a == 2 || a == 5 || a == 8 || (lb->wrap && (a == 4 || a == 6)))
			return 1;
	}
	return a;
}

static bool lb_v14_counted(struct parts *child)
{
	return child->local.show && !child->edit_hidden;
}

static void lb_v14_box_size(struct parts *box, int *w, int *h);

// A text's laid-out extent (0x5bee50), limited by its surface area (0x5bf8c0).
// A line is its glyph textures' widths and the character spacing between
// them, one less than the glyphs (0x5bcd20: 0x5bcde8; 0x5bef1b for a run that
// does not start the line). A glyph texture is as wide as text.c makes a
// character's. The line's own width is not used: it adds up the advances, a
// spacing after every character. The line height is text.c's, not yet the
// original's (0x5bce00). An emptied text has no extent (its texture size is
// not reset).
static void lb_v14_text_size(struct parts_text *t, int *w, int *h)
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
	Rectangle *sa = &t->common.surface_area;
	if (sa->w > 0 && sa->h > 0) {
		if (sa->w < *w)
			*w = sa->w;
		if (sa->h < *h)
			*h = sa->h;
	}
}

/*
 * The size a box gives a child, without margins: GetSize(1), the normal
 * state's size. A box reports its own size; a user component its first
 * child's, or 200x200 without one (0x4e5880, its content's root is attached
 * as that child); a 構築部件 the loader does not build its first step's
 * canvas.
 */
static void lb_v14_child_size(struct parts *child, int *w, int *h)
{
	if (lb_v14_is_box(child)) {
		lb_v14_box_size(child, w, h);
		return;
	}
	if (child->component_type == 17) {
		struct parts *first = TAILQ_FIRST(&child->children);
		if (first) {
			lb_v14_child_size(first, w, h);
		} else {
			*w = 200;
			*h = 200;
		}
		return;
	}
	struct parts_state *state = &child->states[0];
	if (state->type == PARTS_FLAT && state->flat.flat) {
		*w = state->flat.flat->hdr.width;
		*h = state->flat.flat->hdr.height;
		return;
	}
	if (state->type == PARTS_TEXT) {
		lb_v14_text_size(&state->text, w, h);
		return;
	}
	*w = state->common.w;
	*h = state->common.h;
	if (!*w && !*h && child->component_state_type[0] == 26) {
		*w = child->pactex_canvas_w;
		*h = child->pactex_canvas_h;
	}
}

// 0x54aaf0: the largest right and bottom edge of the children, from INT_MIN.
static void lb_v14_free_size(struct parts *box, int *out_w, int *out_h)
{
	int max_x = INT_MIN, max_y = INT_MIN;
	struct parts *child;
	PARTS_FOREACH_CHILD(child, box) {
		if (!lb_v14_counted(child))
			continue;
		int w, h;
		lb_v14_child_size(child, &w, &h);
		int right = child->local.pos.x + align_offset_x(child->origin_mode, w) + w;
		int bottom = child->local.pos.y + align_offset_y(child->origin_mode, h) + h;
		if (right > max_x)
			max_x = right;
		if (bottom > max_y)
			max_y = bottom;
	}
	*out_w = max_x;
	*out_h = max_y;
}

// 0x54adc0
static void lb_v14_vertical_size(struct parts_layout_box *lb, struct parts *box,
		int *out_w, int *out_h)
{
	int col_w = 0, col_h = 0, max_col_h = 0, total_w = 0;
	struct parts *child;
	PARTS_FOREACH_CHILD(child, box) {
		if (!lb_v14_counted(child))
			continue;
		int cw, ch;
		lb_v14_child_size(child, &cw, &ch);
		cw += child->margin_left + child->margin_right;
		ch += child->margin_top + child->margin_bottom;
		if (lb->wrap && ch + col_h > lb->wrap_size) {
			if (col_h > max_col_h)
				max_col_h = col_h;
			total_w += col_w;
			col_w = 0;
			col_h = 0;
		}
		if (cw > col_w)
			col_w = cw;
		col_h += ch;
	}
	*out_w = col_w + total_w + lb->padding_left + lb->padding_right;
	*out_h = max(col_h, max_col_h) + lb->padding_top + lb->padding_bottom;
}

// 0x54b0d0
static void lb_v14_horizontal_size(struct parts_layout_box *lb, struct parts *box,
		int *out_w, int *out_h)
{
	int row_w = 0, row_h = 0, max_row_w = 0, total_h = 0;
	struct parts *child;
	PARTS_FOREACH_CHILD(child, box) {
		if (!lb_v14_counted(child))
			continue;
		int cw, ch;
		lb_v14_child_size(child, &cw, &ch);
		cw += child->margin_left + child->margin_right;
		ch += child->margin_top + child->margin_bottom;
		if (lb->wrap && cw + row_w > lb->wrap_size) {
			if (row_w > max_row_w)
				max_row_w = row_w;
			total_h += row_h;
			row_w = 0;
			row_h = 0;
		}
		if (ch > row_h)
			row_h = ch;
		row_w += cw;
	}
	*out_w = max(row_w, max_row_w) + lb->padding_left + lb->padding_right;
	*out_h = row_h + total_h + lb->padding_top + lb->padding_bottom;
}

// 0x54a9f0
static void lb_v14_box_size(struct parts *box, int *w, int *h)
{
	struct parts_layout_box *lb = &box->states[0].layout_box;
	*w = 0;
	*h = 0;
	switch (lb->layout_type) {
	case PARTS_LAYOUT_FREE:
		lb_v14_free_size(box, w, h);
		break;
	case PARTS_LAYOUT_VERTICAL:
		lb_v14_vertical_size(lb, box, w, h);
		break;
	case PARTS_LAYOUT_HORIZONTAL:
		lb_v14_horizontal_size(lb, box, w, h);
		break;
	default:
		break;
	}
	if (*w <= 0 || *h <= 0) {
		*w = 0;
		*h = 0;
	}
}

/*
 * v14: something that can change the size a box gives `parts` (its normal
 * state's size, its shown flags, its margins, its parent) changed: the
 * nearest vertical/horizontal box above lays out again. A free box or a user
 * component in between passes it on, as their size follows their children.
 * A box whose own size changes this way reports it from parts_do_layout.
 */
void parts_layout_size_changed(struct parts *parts)
{
	if (ain->version < 14)
		return;
	for (struct parts *p = parts, *a = parts->parent; a; p = a, a = a->parent) {
		if (lb_v14_lays_out(a)) {
			parts_component_dirty(a);
			return;
		}
		if (lb_v14_is_box(a))
			continue;
		if (a->component_type == 17 && TAILQ_FIRST(&a->children) == p)
			continue;
		return;
	}
}

// 0x549bd0 (vertical) / 0x54a200 (horizontal)
static void parts_do_layout_v14(struct parts *box)
{
	if (!lb_v14_lays_out(box))
		return;
	struct parts_layout_box *lb = &box->states[0].layout_box;
	int total_w, total_h;
	lb_v14_box_size(box, &total_w, &total_h);
	if (lb->common.w != total_w || lb->common.h != total_h)
		parts_set_dims(box, &lb->common, total_w, total_h);

	int align = lb_v14_align(lb);
	bool is_left   = (align == 1 || align == 4 || align == 7);
	bool is_right  = (align == 3 || align == 6 || align == 9);
	bool is_top    = (align == 1 || align == 2 || align == 3);
	bool is_bottom = (align == 7 || align == 8 || align == 9);
	int start_x = (is_left ? lb->padding_left : is_right ? -lb->padding_right : 0)
		+ align_offset_x(box->origin_mode, total_w) - align_offset_x(align, total_w);
	int start_y = (is_top ? lb->padding_top : is_bottom ? -lb->padding_bottom : 0)
		+ align_offset_y(box->origin_mode, total_h) - align_offset_y(align, total_h);
	int x_dir = is_right ? -1 : 1;
	int y_dir = is_bottom ? -1 : 1;
	bool horizontal = lb->layout_type == PARTS_LAYOUT_HORIZONTAL;

	int cur_x = start_x;
	int cur_y = start_y;
	int max_cross = 0;
	struct parts *child;
	PARTS_FOREACH_CHILD(child, box) {
		bool counted = lb_v14_counted(child);
		if (child->origin_mode != align) {
			child->origin_mode = align;
			parts_recalculate_hitbox(child);
		}
		int cw, ch;
		lb_v14_child_size(child, &cw, &ch);
		cw += child->margin_left + child->margin_right;
		ch += child->margin_top + child->margin_bottom;

		if (counted && lb->wrap) {
			if (horizontal && abs(cur_x + cw * x_dir - start_x) > lb->wrap_size) {
				cur_y += max_cross * y_dir;
				cur_x = start_x;
				max_cross = 0;
			} else if (!horizontal && abs(cur_y + ch * y_dir - start_y) > lb->wrap_size) {
				cur_x += max_cross * x_dir;
				cur_y = start_y;
				max_cross = 0;
			}
		}

		int pos_x = cur_x + (is_left ? child->margin_left : is_right ? -child->margin_right : 0);
		int pos_y = cur_y + (is_top ? child->margin_top : is_bottom ? -child->margin_bottom : 0);
		if (child->local.pos.x != pos_x || child->local.pos.y != pos_y) {
			child->local.pos.x = pos_x;
			child->local.pos.y = pos_y;
			parts_recalculate_hitbox(child);
			parts_component_dirty(child);
		}

		if (!counted)
			continue;
		if (horizontal) {
			cur_x += cw * x_dir;
			if (ch > max_cross)
				max_cross = ch;
		} else {
			cur_y += ch * y_dir;
			if (cw > max_cross)
				max_cross = cw;
		}
	}
}

void parts_do_layout(struct parts *parts)
{
	if (ain->version >= 14) {
		parts_do_layout_v14(parts);
		return;
	}
	if (parts->states[0].type != PARTS_LAYOUT_BOX)
		return;
	struct parts_layout_box *lb = &parts->states[0].layout_box;
	if (lb->layout_type == PARTS_LAYOUT_FREE)
		return;

	int total_w, total_h;
	if (lb->layout_type == PARTS_LAYOUT_HORIZONTAL)
		parts_layout_calc_size_horizontal(lb, parts, &total_w, &total_h);
	else
		parts_layout_calc_size_vertical(lb, parts, &total_w, &total_h);

	int align = lb->align;
	bool is_left   = (align == 1 || align == 4 || align == 7);
	bool is_right  = (align == 3 || align == 6 || align == 9);
	bool is_top    = (align == 1 || align == 2 || align == 3);
	bool is_bottom = (align == 7 || align == 8 || align == 9);

	int origin_x = align_offset_x(parts->origin_mode, total_w);
	int origin_y = align_offset_y(parts->origin_mode, total_h);
	int align_x = align_offset_x(align, total_w);
	int align_y = align_offset_y(align, total_h);

	int start_x = (is_left ? lb->padding_left : -lb->padding_right)
		+ (origin_x - align_x);
	int start_y = (is_top ? lb->padding_top : -lb->padding_bottom)
		+ (origin_y - align_y);

	int x_dir = is_right ? -1 : 1;
	int y_dir = is_bottom ? -1 : 1;

	int cur_x = start_x;
	int cur_y = start_y;
	int max_cross = 0;

	struct parts *child;
	PARTS_FOREACH_CHILD(child, parts) {
		int child_w, child_h;
		parts_get_layout_size(child, &child_w, &child_h);
		int cw = child->margin_left + child_w + child->margin_right;
		int ch = child->margin_top + child_h + child->margin_bottom;

		// The layout box forces the child's origin_mode to match its own align.
		if (child->origin_mode != align) {
			child->origin_mode = align;
			parts_recalculate_hitbox(child);
		}

		if (lb->layout_type == PARTS_LAYOUT_HORIZONTAL) {
			if (lb->wrap && abs((cur_x + cw * x_dir) - start_x) > lb->wrap_size
					&& max_cross > 0) {
				cur_y += max_cross * y_dir;
				max_cross = 0;
				cur_x = start_x;
			}
		} else {
			if (lb->wrap && abs((cur_y + ch * y_dir) - start_y) > lb->wrap_size
					&& max_cross > 0) {
				cur_x += max_cross * x_dir;
				max_cross = 0;
				cur_y = start_y;
			}
		}

		// margin offset depends on alignment direction
		int mx = is_left ? child->margin_left : is_right ? -child->margin_right : 0;
		int my = is_top ? child->margin_top : is_bottom ? -child->margin_bottom : 0;

		int pos_x = cur_x + mx;
		int pos_y = cur_y + my;
		if (child->local.pos.x != pos_x || child->local.pos.y != pos_y) {
			child->local.pos.x = pos_x;
			child->local.pos.y = pos_y;
			parts_recalculate_hitbox(child);
			parts_component_dirty(child);
		}

		if (lb->layout_type == PARTS_LAYOUT_HORIZONTAL) {
			cur_x += cw * x_dir;
			if (ch > max_cross)
				max_cross = ch;
		} else {
			cur_y += ch * y_dir;
			if (cw > max_cross)
				max_cross = cw;
		}
	}
}
