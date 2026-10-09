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

#include "system4.h"
#include "system4/string.h"

#include "vm/page.h"
#include "parts.h"
#include "system4/ain.h"
#include "vm.h"
#include "parts_internal.h"
#include "../hll/iarray.h"

/*
 * A v14 save ("XPE"; engines before v14 keep their version 3 layout):
 *
 *   version, numeral fonts, [active controller, number of controllers],
 *   the parts records,
 *   the hidden parts' numbers          (version 7 and later),
 *   the 減算色モード parts' numbers     (version 8 and later),
 *   the controller ID section          (version 7 and later, see below).
 *
 * Each list is a count and then the numbers; a parts record has the same
 * layout in versions 6, 7 and 8. The controller ID section is always the
 * last nr + 3 words, whatever precedes it, so a version 7 save with or
 * without it is read as well (and so is a version 8 save without it).
 *
 * Version 8 also has the construction operations 14..22 (the pixel
 * operations, struct parts_cp_pixel) in a parts record's operation list. A
 * reader reads no fields for an operation type it does not know, so a save
 * with a new operation needs a new version; these came with version 8.
 */
#define CURRENT_SAVE_VERSION 8

/*
 * Controllers in a save. The controller fields (the active controller, each
 * parts' controller) hold stack positions, which is what they held while a
 * controller's ID was its position. A v14 save ends with a section that
 * holds the controllers' IDs and the active one (version 7 and later). It
 * is read first, from the end of the buffer, so that the controllers have
 * their IDs and their roots before any parts is loaded (a plain parts can
 * have the number that the default ID of a position would be).
 *
 * A v14 save without the section was written while a controller's ID was
 * its position, or by a build that left the section out for a stack that
 * had only been pushed and popped. Its controllers get the default ID of
 * their positions (the root number for the position) and its parts come
 * back on their controllers. A layer ID that the script kept in the same
 * image is still the old one, though, and names no controller here; a
 * warning says so. Engines before v14 neither write nor read the section.
 *
 * The v14 root parts are not saved: they come with the controllers, and a
 * top-level parts records no parent, as before.
 */
#define SAVE_CONTROLLER_IDS 0x44494350 /* "PCID" */

static int save_controller_no(int id)
{
	int index = parts_controller_index(id);
	return index < 0 ? id : index;
}

static void save_controller_ids(struct iarray_writer *w)
{
	// v14: always, the mark of a save whose IDs are root numbers.
	if (ain->version < 14)
		return;
	iarray_write(w, SAVE_CONTROLLER_IDS);
	iarray_write(w, ctrl_stack.nr_controllers);
	for (int i = 0; i < ctrl_stack.nr_controllers; i++)
		iarray_write(w, ctrl_stack.ids[i]);
	iarray_write(w, ctrl_stack.active);
}

// The section of a save of `nr` controllers: its last nr + 3 words. Returns
// the IDs (to be freed) and the active controller, NULL without a section.
static int *load_controller_ids(struct iarray_reader *r, int version, int nr, int *active_out)
{
	if (ain->version < 14 || version < 7 || nr < 0 || nr > PARTS_CONTROLLER_STACK_MAX)
		return NULL;
	unsigned len = nr + 3;
	if (r->size < len || r->size - len < r->pos)
		return NULL;
	union vm_value *section = r->data + (r->size - len);
	if (section[0].i != SAVE_CONTROLLER_IDS || section[1].i != nr)
		return NULL;
	int *ids = xmalloc(max(nr, 1) * sizeof(int));
	for (int i = 0; i < nr; i++) {
		ids[i] = section[2 + i].i;
		bool valid = ids[i] >= PARTS_CONTROLLER_ID_BASE;
		for (int j = 0; j < i; j++)
			valid &= ids[j] != ids[i];
		if (!valid) {
			free(ids);
			return NULL;
		}
	}
	*active_out = section[2 + nr].i;
	return ids;
}

// The loaded parts' controller fields are positions; make them IDs, put the
// top-level parts under their roots and make the saved controller active
// (`active` is an ID when the save had the section, else a position).
static void load_controller_finish(int active, bool is_id)
{
	int nr = ctrl_stack.nr_controllers;
	struct parts *parts;
	PARTS_LIST_FOREACH(parts) {
		if (parts_is_controller_root(parts))
			continue;
		if (parts->controller_no >= 0 && parts->controller_no < nr)
			parts->controller_no = ctrl_stack.ids[parts->controller_no];
	}
	if (!is_id && active != PARTS_CONTROLLER_SYSTEM_OVERLAY)
		active = active >= 0 && active < nr ? ctrl_stack.ids[active] : -1;
	PE_set_active_controller(active);
	parts_controller_relink_roots();
}

static void save_parts_params(struct iarray_writer *w, struct parts_params *params)
{
	iarray_write(w, params->z);
	iarray_write_point(w, &params->pos);
	iarray_write(w, params->show);
	iarray_write(w, params->alpha);
	iarray_write_float(w, params->scale.x);
	iarray_write_float(w, params->scale.y);
	iarray_write_float(w, params->rotation.x);
	iarray_write_float(w, params->rotation.y);
	iarray_write_float(w, params->rotation.z);
	iarray_write_color(w, &params->add_color);
	iarray_write_color(w, &params->multiply_color);
}

static void load_parts_params(struct iarray_reader *r, struct parts_params *params)
{
	params->z = iarray_read(r);
	iarray_read_point(r, &params->pos);
	params->show = iarray_read(r);
	params->alpha = iarray_read(r);
	params->scale.x = iarray_read_float(r);
	params->scale.y = iarray_read_float(r);
	params->rotation.x = iarray_read_float(r);
	params->rotation.y = iarray_read_float(r);
	params->rotation.z = iarray_read_float(r);
	iarray_read_color(r, &params->add_color);
	iarray_read_color(r, &params->multiply_color);
}

static void save_parts_cg(struct iarray_writer *w, struct parts_cg *cg)
{
	iarray_write(w, cg->no);
	iarray_write_string_or_null(w, cg->name);
}

static void load_parts_cg(struct iarray_reader *r, struct parts *parts, struct parts_cg *cg)
{
	int no = iarray_read(r);
	struct string *name = iarray_read_string_or_null(r);
	if (name) {
		parts_cg_set(parts, cg, name);
	} else if (no) {
		parts_cg_set_by_index(parts, cg, no);
	}
}

static void save_parts_text(struct iarray_writer *w, struct parts_text *text, int version)
{
	iarray_write(w, text->line_space);
	iarray_write_text_style(w, &text->ts);
	// 太さ as PE_SetFont keeps it: it widens the glyph cells of the GBK
	// grid (gfx_text_cn_style_edge), so a layout box needs it back.
	if (version >= 6)
		iarray_write_float(w, text->ts.bold_weight);
	iarray_write(w, text->nr_lines);
	for (unsigned i = 0; i < text->nr_lines; i++) {
		struct string *s = parts_text_line_get(&text->lines[i]);
		iarray_write_string(w, s);
		free_string(s);
	}
}

static void load_parts_text(struct iarray_reader *r, struct parts *parts,
		struct parts_text *text, int version)
{
	// FIXME: this won't accurately restore the text state if the text style
	//        varies per-character
	text->line_space = iarray_read(r);
	iarray_read_text_style(r, &text->ts);
	if (version >= 6)
		text->ts.bold_weight = iarray_read_float(r);

	text->nr_lines = 0;
	text->lines = NULL;
	text->cursor.x = 0;
	text->cursor.y = 0;

	int nr_lines = iarray_read(r);
	for (unsigned i = 0; i < nr_lines; i++) {
		struct string *line = iarray_read_string(r);
		parts_text_append(parts, text, line);
	}
}

static void save_parts_animation(struct iarray_writer *w, struct parts_animation *anim)
{
	iarray_write_string_or_null(w, anim->cg_name);
	iarray_write(w, anim->start_no);
	iarray_write(w, anim->nr_frames);
	iarray_write(w, anim->frame_time);
	iarray_write(w, anim->elapsed);
	iarray_write(w, anim->current_frame);
}

static void load_parts_animation(struct iarray_reader *r, struct parts *parts,
		struct parts_animation *anim)
{
	struct string *cg_name = iarray_read_string_or_null(r);
	int start_no = iarray_read(r);
	int nr_frames = iarray_read(r);
	int frame_time = iarray_read(r);

	if (cg_name) {
		parts_animation_set_cg(parts, anim, cg_name, start_no, nr_frames, frame_time);
		free_string(cg_name);
	} else {
		parts_animation_set_cg_by_index(parts, anim, start_no, nr_frames, frame_time);
	}

	anim->elapsed = iarray_read(r);
	anim->current_frame = iarray_read(r);
}

static void save_parts_numeral(struct iarray_writer *w, struct parts_numeral *num)
{
	iarray_write(w, num->have_num);
	iarray_write(w, num->num);
	iarray_write(w, num->space);
	iarray_write(w, num->show_comma);
	iarray_write(w, num->length);
	iarray_write(w, num->font_no);
}

static void load_parts_numeral(struct iarray_reader *r, struct parts *parts,
		struct parts_numeral *num)
{
	num->have_num = iarray_read(r);
	num->num = iarray_read(r);
	num->space = iarray_read(r);
	num->show_comma = iarray_read(r);
	num->length = iarray_read(r);
	num->font_no = iarray_read(r);

	if (num->have_num)
		parts_numeral_set_number(parts, num, num->num);
}

static void save_parts_gauge(struct iarray_writer *w, struct parts_gauge *gauge, int version)
{
	iarray_write(w, gauge->cg_no);
	iarray_write_float(w, gauge->rate);
	if (version >= 5) {
		iarray_write_float(w, gauge->numerator);
		iarray_write_float(w, gauge->denominator);
		iarray_write(w, gauge->reverse);
		iarray_write_string_or_null(w, gauge->cg_name);
	}
}

static void load_parts_gauge(struct iarray_reader *r, struct parts *parts,
		struct parts_gauge *gauge, bool vert, int version)
{
	gauge->cg_no = iarray_read(r);
	gauge->rate = iarray_read_float(r);
	if (version >= 5) {
		gauge->numerator = iarray_read_float(r);
		gauge->denominator = iarray_read_float(r);
		gauge->reverse = !!iarray_read(r);
		gauge->cg_name = iarray_read_string_or_null(r);
	} else {
		// Older XPE records retained only a quotient.
		gauge->numerator = gauge->rate;
		gauge->denominator = 1;
	}
	if (gauge->cg_no >= 0)
		parts_gauge_set_cg_by_index(parts, gauge, gauge->cg_no);
	if (ain->version < 14) {
		if (vert) parts_vgauge_set_rate(parts, gauge, gauge->rate);
		else parts_hgauge_set_rate(parts, gauge, gauge->rate);
	}
}

static void save_parts_cp_op(struct iarray_writer *w, struct parts_cp_op *op)
{
	iarray_write(w, op->type);
	switch (op->type) {
	case PARTS_CP_CREATE:
	case PARTS_CP_CREATE_PIXEL_ONLY:
		iarray_write(w, op->create.w);
		iarray_write(w, op->create.h);
		break;
	case PARTS_CP_CG:
		iarray_write(w, op->cg.no);
		break;
	case PARTS_CP_FILL:
	case PARTS_CP_FILL_ALPHA_COLOR:
	case PARTS_CP_FILL_AMAP:
	case PARTS_CP_FILL_WITH_ALPHA:
	case PARTS_CP_DRAW_RECT:
		iarray_write(w, op->fill.x);
		iarray_write(w, op->fill.y);
		iarray_write(w, op->fill.w);
		iarray_write(w, op->fill.h);
		iarray_write(w, op->fill.r);
		iarray_write(w, op->fill.g);
		iarray_write(w, op->fill.b);
		iarray_write(w, op->fill.a);
		break;
	case PARTS_CP_DRAW_CUT_CG:
	case PARTS_CP_COPY_CUT_CG:
		iarray_write(w, op->cut_cg.cg_no);
		iarray_write(w, op->cut_cg.dx);
		iarray_write(w, op->cut_cg.dy);
		iarray_write(w, op->cut_cg.dw);
		iarray_write(w, op->cut_cg.dh);
		iarray_write(w, op->cut_cg.sx);
		iarray_write(w, op->cut_cg.sy);
		iarray_write(w, op->cut_cg.sw);
		iarray_write(w, op->cut_cg.sh);
		iarray_write(w, op->cut_cg.interp_type);
		break;
	case PARTS_CP_DRAW_TEXT:
	case PARTS_CP_COPY_TEXT:
		iarray_write_string(w, op->text.text);
		iarray_write(w, op->text.x);
		iarray_write(w, op->text.y);
		iarray_write(w, op->text.line_space);
		iarray_write_text_style(w, &op->text.style);
		break;
	case PARTS_CP_GRAY_FILTER:
		iarray_write(w, op->filter.x);
		iarray_write(w, op->filter.y);
		iarray_write(w, op->filter.w);
		iarray_write(w, op->filter.h);
		iarray_write(w, op->filter.full_size);
		break;
	case PARTS_CP_FILL_PIE_AMAP:
		// Since version 7: older readers read no fields for a type they
		// do not know, and would misread the rest of the save.
		iarray_write(w, op->pie.x);
		iarray_write(w, op->pie.y);
		iarray_write(w, op->pie.rx);
		iarray_write(w, op->pie.ry);
		iarray_write(w, op->pie.start);
		iarray_write(w, op->pie.sweep);
		iarray_write(w, op->pie.a);
		iarray_write(w, op->pie.angle);
		break;
	case PARTS_CP_MUL_AMAP_GRADATION_ROWS:
	case PARTS_CP_MUL_AMAP_GRADATION_COLUMNS:
	case PARTS_CP_BLUR_H:
	case PARTS_CP_BLUR_V:
	case PARTS_CP_FILL_CIRCLE_AMAP:
	case PARTS_CP_FILL_CIRCLE_BLEND:
	case PARTS_CP_FILL_POLYGON_BLEND:
	case PARTS_CP_TILE_CG:
	case PARTS_CP_DRAW_CIRCLE_AMAP:
		// Since version 8, for the reason given above. One layout for all
		// of them, the polygon's points last.
		iarray_write(w, op->pixel.x);
		iarray_write(w, op->pixel.y);
		iarray_write(w, op->pixel.w);
		iarray_write(w, op->pixel.h);
		iarray_write(w, op->pixel.full);
		iarray_write(w, op->pixel.r);
		iarray_write(w, op->pixel.g);
		iarray_write(w, op->pixel.b);
		iarray_write(w, op->pixel.a);
		iarray_write(w, op->pixel.a2);
		iarray_write(w, op->pixel.radius);
		iarray_write(w, op->pixel.cg_no);
		iarray_write(w, op->pixel.line_width);
		iarray_write(w, op->pixel.nr_points);
		for (int i = 0; i < op->pixel.nr_points * 2; i++)
			iarray_write(w, op->pixel.points[i]);
		break;
	}
}

static struct parts_cp_op *load_parts_cp_op(struct iarray_reader *r)
{
	struct parts_cp_op *op = xcalloc(1, sizeof(struct parts_cp_op));
	op->type = iarray_read(r);
	switch (op->type) {
	case PARTS_CP_CREATE:
	case PARTS_CP_CREATE_PIXEL_ONLY:
		op->create.w = iarray_read(r);
		op->create.h = iarray_read(r);
		break;
	case PARTS_CP_CG:
		op->cg.no = iarray_read(r);
		break;
	case PARTS_CP_FILL:
	case PARTS_CP_FILL_ALPHA_COLOR:
	case PARTS_CP_FILL_AMAP:
	case PARTS_CP_FILL_WITH_ALPHA:
	case PARTS_CP_DRAW_RECT:
		op->fill.x = iarray_read(r);
		op->fill.y = iarray_read(r);
		op->fill.w = iarray_read(r);
		op->fill.h = iarray_read(r);
		op->fill.r = iarray_read(r);
		op->fill.g = iarray_read(r);
		op->fill.b = iarray_read(r);
		op->fill.a = iarray_read(r);
		break;
	case PARTS_CP_DRAW_CUT_CG:
	case PARTS_CP_COPY_CUT_CG:
		op->cut_cg.cg_no = iarray_read(r);
		op->cut_cg.dx = iarray_read(r);
		op->cut_cg.dy = iarray_read(r);
		op->cut_cg.dw = iarray_read(r);
		op->cut_cg.dh = iarray_read(r);
		op->cut_cg.sx = iarray_read(r);
		op->cut_cg.sy = iarray_read(r);
		op->cut_cg.sw = iarray_read(r);
		op->cut_cg.sh = iarray_read(r);
		op->cut_cg.interp_type = iarray_read(r);
		break;
	case PARTS_CP_DRAW_TEXT:
	case PARTS_CP_COPY_TEXT:
		op->text.text = iarray_read_string(r);
		op->text.x = iarray_read(r);
		op->text.y = iarray_read(r);
		op->text.line_space = iarray_read(r);
		iarray_read_text_style(r, &op->text.style);
		break;
	case PARTS_CP_GRAY_FILTER:
		op->filter.x = iarray_read(r);
		op->filter.y = iarray_read(r);
		op->filter.w = iarray_read(r);
		op->filter.h = iarray_read(r);
		op->filter.full_size = !!iarray_read(r);
		break;
	case PARTS_CP_FILL_PIE_AMAP:
		op->pie.x = iarray_read(r);
		op->pie.y = iarray_read(r);
		op->pie.rx = iarray_read(r);
		op->pie.ry = iarray_read(r);
		op->pie.start = iarray_read(r);
		op->pie.sweep = iarray_read(r);
		op->pie.a = iarray_read(r);
		op->pie.angle = iarray_read(r);
		break;
	case PARTS_CP_MUL_AMAP_GRADATION_ROWS:
	case PARTS_CP_MUL_AMAP_GRADATION_COLUMNS:
	case PARTS_CP_BLUR_H:
	case PARTS_CP_BLUR_V:
	case PARTS_CP_FILL_CIRCLE_AMAP:
	case PARTS_CP_FILL_CIRCLE_BLEND:
	case PARTS_CP_FILL_POLYGON_BLEND:
	case PARTS_CP_TILE_CG:
	case PARTS_CP_DRAW_CIRCLE_AMAP:
		op->pixel.x = iarray_read(r);
		op->pixel.y = iarray_read(r);
		op->pixel.w = iarray_read(r);
		op->pixel.h = iarray_read(r);
		op->pixel.full = !!iarray_read(r);
		op->pixel.r = iarray_read(r);
		op->pixel.g = iarray_read(r);
		op->pixel.b = iarray_read(r);
		op->pixel.a = iarray_read(r);
		op->pixel.a2 = iarray_read(r);
		op->pixel.radius = iarray_read(r);
		op->pixel.cg_no = iarray_read(r);
		op->pixel.line_width = iarray_read(r);
		op->pixel.nr_points = iarray_read(r);
		// A count this build would not have written reads no points and
		// builds nothing.
		if (op->pixel.nr_points < 0 || op->pixel.nr_points > PARTS_CP_POLYGON_MAX_POINTS)
			op->pixel.nr_points = 0;
		if (op->pixel.nr_points) {
			op->pixel.points = xmalloc(op->pixel.nr_points * 2 * sizeof(int));
			for (int i = 0; i < op->pixel.nr_points * 2; i++)
				op->pixel.points[i] = iarray_read(r);
		}
		// Likewise a circle larger than this build draws: it would be
		// refused at every build (build_fill_circle, build_draw_circle)
		// and is read as one with no radius, which draws nothing. A
		// rectangle's position and size, a centre and a vertex may be any
		// number: the builders clip them in 64 bits, and a limit here
		// would change what a rectangle reaching across the surface from
		// far outside it draws.
		if ((op->type == PARTS_CP_FILL_CIRCLE_AMAP || op->type == PARTS_CP_FILL_CIRCLE_BLEND
					|| op->type == PARTS_CP_DRAW_CIRCLE_AMAP)
				&& (op->pixel.radius > PARTS_CP_CIRCLE_MAX_RADIUS
					|| op->pixel.line_width > PARTS_CP_CIRCLE_MAX_RADIUS)) {
			WARNING("construction operation %d: radius %d, width %d not loaded",
					op->type, op->pixel.radius, op->pixel.line_width);
			op->pixel.radius = 0;
			op->pixel.line_width = 0;
		}
		break;
	}
	return op;
}

static void save_parts_construction_process(struct iarray_writer *w,
		struct parts_construction_process *cproc)
{
	unsigned ops_count_pos = iarray_writer_pos(w);
	iarray_write(w, 0); // size of ops list

	unsigned ops_count = 0;
	struct parts_cp_op *op;
	TAILQ_FOREACH(op, &cproc->ops, entry) {
		save_parts_cp_op(w, op);
		ops_count++;
	}

	iarray_write_at(w, ops_count_pos, ops_count);
}

static void load_parts_construction_process(struct iarray_reader *r, struct parts *parts,
		struct parts_construction_process *cproc)
{
	int ops_count = iarray_read(r);
	for (int i = 0; i < ops_count; i++) {
		struct parts_cp_op *op = load_parts_cp_op(r);
		parts_add_cp_op(cproc, op);
	}
	parts_build_construction_process(parts, cproc);
}

static void save_parts_flash(struct iarray_writer *w, struct parts_flash *flash)
{
	iarray_write_string_or_null(w, flash->name);
	iarray_write(w, flash->stopped);
	iarray_write(w, flash->current_frame);
}

static void load_parts_flash(struct iarray_reader *r, struct parts *parts,
		struct parts_flash *flash)
{
	struct string *name = iarray_read_string_or_null(r);
	parts_flash_load(parts, flash, name);
	free_string(name);
	flash->stopped = !!iarray_read(r);
	parts_flash_seek(flash, iarray_read(r));
}

static void save_parts_flat(struct iarray_writer *w, struct parts_flat *flat)
{
	iarray_write_string_or_null(w, flat->name);
}

static void load_parts_flat(struct iarray_reader *r, struct parts *parts,
		struct parts_flat *flat)
{
	struct string *name = iarray_read_string_or_null(r);
	parts_flat_load(parts, flat, name);
	free_string(name);
	flat->needs_advance = true;
}

static void save_parts_layout_box(struct iarray_writer *w, struct parts_layout_box *lb)
{
	iarray_write(w, lb->layout_type);
	iarray_write(w, lb->wrap);
	iarray_write(w, lb->wrap_size);
	iarray_write(w, lb->align);
	iarray_write(w, lb->padding_top);
	iarray_write(w, lb->padding_bottom);
	iarray_write(w, lb->padding_left);
	iarray_write(w, lb->padding_right);
}

static void load_parts_layout_box(struct iarray_reader *r, struct parts_layout_box *lb)
{
	lb->layout_type = iarray_read(r);
	lb->wrap = iarray_read(r);
	lb->wrap_size = iarray_read(r);
	lb->align = iarray_read(r);
	lb->padding_top = iarray_read(r);
	lb->padding_bottom = iarray_read(r);
	lb->padding_left = iarray_read(r);
	lb->padding_right = iarray_read(r);
}

static void save_parts_state(struct iarray_writer *w, struct parts_state *state, int version)
{
	iarray_write(w, state->type);
	iarray_write(w, state->common.w);
	iarray_write(w, state->common.h);
	iarray_write_point(w, &state->common.origin_offset);
	iarray_write_rectangle(w, &state->common.hitbox);
	iarray_write_rectangle(w, &state->common.surface_area);
	switch (state->type) {
	case PARTS_UNINITIALIZED:
	case PARTS_MOVIE:
	case PARTS_RECT_DETECTION:
	case PARTS_3DLAYER:
		break;
	case PARTS_CG:
		save_parts_cg(w, &state->cg);
		break;
	case PARTS_TEXT:
		save_parts_text(w, &state->text, version);
		break;
	case PARTS_ANIMATION:
		save_parts_animation(w, &state->anim);
		break;
	case PARTS_NUMERAL:
		save_parts_numeral(w, &state->num);
		break;
	case PARTS_HGAUGE:
	case PARTS_VGAUGE:
		save_parts_gauge(w, &state->gauge, version);
		break;
	case PARTS_CONSTRUCTION_PROCESS:
		save_parts_construction_process(w, &state->cproc);
		break;
	case PARTS_FLASH:
		save_parts_flash(w, &state->flash);
		break;
	case PARTS_FLAT:
		save_parts_flat(w, &state->flat);
		break;
	case PARTS_LAYOUT_BOX:
		save_parts_layout_box(w, &state->layout_box);
		break;
	}
}

static void load_parts_state(struct iarray_reader *r, struct parts *parts,
		struct parts_state *state, int version)
{
	parts_state_reset(state, iarray_read(r));
	state->common.w = iarray_read(r);
	state->common.h = iarray_read(r);
	iarray_read_point(r, &state->common.origin_offset);
	iarray_read_rectangle(r, &state->common.hitbox);
	iarray_read_rectangle(r, &state->common.surface_area);
	switch (state->type) {
	case PARTS_UNINITIALIZED:
	case PARTS_MOVIE:
	case PARTS_RECT_DETECTION:
	case PARTS_3DLAYER:
		break;
	case PARTS_CG:
		load_parts_cg(r, parts, &state->cg);
		break;
	case PARTS_TEXT:
		load_parts_text(r, parts, &state->text, version);
		break;
	case PARTS_ANIMATION:
		load_parts_animation(r, parts, &state->anim);
		break;
	case PARTS_NUMERAL:
		load_parts_numeral(r, parts, &state->num);
		break;
	case PARTS_HGAUGE:
		load_parts_gauge(r, parts, &state->gauge, false, version);
		break;
	case PARTS_VGAUGE:
		load_parts_gauge(r, parts, &state->gauge, true, version);
		break;
	case PARTS_CONSTRUCTION_PROCESS:
		load_parts_construction_process(r, parts, &state->cproc);
		break;
	case PARTS_FLASH:
		load_parts_flash(r, parts, &state->flash);
		break;
	case PARTS_FLAT:
		load_parts_flat(r, parts, &state->flat);
		break;
	case PARTS_LAYOUT_BOX:
		load_parts_layout_box(r, &state->layout_box);
		break;
	}
}

static void save_parts_motion(struct iarray_writer *w, struct parts_motion *motion)
{
	iarray_write(w, motion->type);
	switch (motion->type) {
	case PARTS_MOTION_POS:
		iarray_write(w, motion->begin.x);
		iarray_write(w, motion->begin.y);
		iarray_write(w, motion->end.x);
		iarray_write(w, motion->end.y);
		break;
	case PARTS_MOTION_ALPHA:
	case PARTS_MOTION_CG:
	case PARTS_MOTION_NUMERAL_NUMBER:
		iarray_write(w, motion->begin.i);
		iarray_write(w, motion->end.i);
		break;
	case PARTS_MOTION_HGAUGE_RATE:
	case PARTS_MOTION_VGAUGE_RATE:
	case PARTS_MOTION_MAG_X:
	case PARTS_MOTION_MAG_Y:
	case PARTS_MOTION_ROTATE_X:
	case PARTS_MOTION_ROTATE_Y:
	case PARTS_MOTION_ROTATE_Z:
		iarray_write_float(w, motion->begin.f);
		iarray_write_float(w, motion->end.f);
		break;
	case PARTS_MOTION_VIBRATION_SIZE:
		iarray_write(w, motion->begin.x);
		iarray_write(w, motion->begin.y);
		break;
	}
	iarray_write(w, motion->begin_time);
	iarray_write(w, motion->end_time);
}

static struct parts_motion *load_parts_motion(struct iarray_reader *r)
{
	struct parts_motion *motion = xcalloc(1, sizeof(struct parts_motion));
	motion->type = iarray_read(r);
	switch (motion->type) {
	case PARTS_MOTION_POS:
		motion->begin.x = iarray_read(r);
		motion->begin.y = iarray_read(r);
		motion->end.x = iarray_read(r);
		motion->end.y = iarray_read(r);
		break;
	case PARTS_MOTION_ALPHA:
	case PARTS_MOTION_CG:
	case PARTS_MOTION_NUMERAL_NUMBER:
		motion->begin.i = iarray_read(r);
		motion->end.i = iarray_read(r);
		break;
	case PARTS_MOTION_HGAUGE_RATE:
	case PARTS_MOTION_VGAUGE_RATE:
	case PARTS_MOTION_MAG_X:
	case PARTS_MOTION_MAG_Y:
	case PARTS_MOTION_ROTATE_X:
	case PARTS_MOTION_ROTATE_Y:
	case PARTS_MOTION_ROTATE_Z:
		motion->begin.f = iarray_read_float(r);
		motion->end.f = iarray_read_float(r);
		break;
	case PARTS_MOTION_VIBRATION_SIZE:
		motion->begin.x = iarray_read(r);
		motion->begin.y = iarray_read(r);
		break;
	}
	motion->begin_time = iarray_read(r);
	motion->end_time = iarray_read(r);
	return motion;
}

static void save_parts(struct iarray_writer *w, struct parts *parts, int version)
{
	iarray_write(w, parts->no);
	iarray_write(w, parts->state);
	for (int i = 0; i < PARTS_NR_STATES; i++) {
		save_parts_state(w, &parts->states[i], version);
	}

	save_parts_params(w, &parts->local);
	save_parts_params(w, &parts->global);
	// A top-level parts hangs under its controller's root (v14), which is
	// not saved: it records no parent, as it did without the roots.
	iarray_write(w, parts->parent && !parts_is_controller_root(parts->parent)
			? parts->parent->no : -1);
	iarray_write(w, parts->delegate_index);
	iarray_write(w, parts->sprite_deform);
	iarray_write(w, parts->clickable);
	iarray_write(w, parts->on_cursor_sound);
	iarray_write(w, parts->on_click_sound);
	iarray_write(w, parts->origin_mode);
	// XXX: no need to save pending parent since we called UpdateComponent first
	iarray_write(w, parts->linked_to);
	iarray_write(w, parts->linked_from);
	iarray_write(w, parts->draw_filter);
	iarray_write(w, parts->message_window);
	iarray_write(w, parts->alpha_clipper_parts_no);
	if (version >= 4) {
		iarray_write(w, parts->clip_enabled);
		iarray_write_rectangle(w, &parts->clip_area);
	}
	if (version >= 5) {
		iarray_write(w, parts->component_type);
		for (int i = 0; i < PARTS_NR_STATES; i++)
			iarray_write(w, parts->component_state_type[i]);
	}
	if (version >= 6) {
		// Loading does not run the pactex loader again.
		iarray_write(w, parts->pactex_canvas_w);
		iarray_write(w, parts->pactex_canvas_h);
	}
	// TODO: once the Rance 9 save format stabilizes, bump save version
	// and save unconditionally
	if (parts_multi_controller) {
		iarray_write(w, save_controller_no(parts->controller_no));
		iarray_write(w, parts->pass_cursor);
		iarray_write(w, parts->lock_input_state);
		iarray_write(w, parts->margin_top);
		iarray_write(w, parts->margin_bottom);
		iarray_write(w, parts->margin_left);
		iarray_write(w, parts->margin_right);
		iarray_write(w, parts->draggable);
	}

	unsigned motion_count_pos = iarray_writer_pos(w);
	iarray_write(w, 0); // size of motion list

	unsigned motion_count = 0;
	struct parts_motion *motion;
	TAILQ_FOREACH(motion, &parts->motion, entry) {
		save_parts_motion(w, motion);
		motion_count++;
	}

	iarray_write_at(w, motion_count_pos, motion_count);
}

static void load_parts(struct iarray_reader *r, int version)
{
	int no = iarray_read(r);
	struct parts *parts = parts_get(no);
	parts->state = iarray_read(r);
	for (int i = 0; i < PARTS_NR_STATES; i++) {
		load_parts_state(r, parts, &parts->states[i], version);
	}

	load_parts_params(r, &parts->local);
	load_parts_params(r, &parts->global);
	parts->pending_parent = iarray_read(r);
	parts->delegate_index = iarray_read(r);
	parts->sprite_deform = iarray_read(r);
	parts->clickable = iarray_read(r);
	parts->on_cursor_sound = iarray_read(r);
	parts->on_click_sound = iarray_read(r);
	parts->origin_mode = iarray_read(r);
	parts->linked_to = iarray_read(r);
	parts->linked_from = iarray_read(r);
	parts->draw_filter = iarray_read(r);
	if (version > 0)
		parts->message_window = iarray_read(r);
	if (version > 2)
		parts->alpha_clipper_parts_no = iarray_read(r);
	if (version >= 4) {
		parts->clip_enabled = !!iarray_read(r);
		iarray_read_rectangle(r, &parts->clip_area);
	}
	if (version >= 5) {
		parts->component_type = iarray_read(r);
		for (int i = 0; i < PARTS_NR_STATES; i++)
			parts->component_state_type[i] = iarray_read(r);
	} else if (ain->version >= 14) {
		// Old records have no widget metadata. Recover gauge types before
		// AIN CompParts queries them (a getter must not be needed first).
		for (int i = 0; i < PARTS_NR_STATES; i++) {
			if (parts->states[i].type == PARTS_HGAUGE || parts->states[i].type == PARTS_VGAUGE) {
				parts->component_type = 18;
				parts->component_state_type[i] = parts->states[i].type == PARTS_HGAUGE ? 22 : 23;
			}
		}
	}
	if (version >= 6) {
		parts->pactex_canvas_w = iarray_read(r);
		parts->pactex_canvas_h = iarray_read(r);
	}
	// The transform is not saved (v14); a child's is combined once its
	// parent is attached, by the update PE_Load ends with.
	parts_load_transform(parts);
	// A v14 panel written before its colour kept the alpha.
	if (ain->version >= 14 && parts->component_type == 14)
		parts_panel_load_blended(parts);
	// TODO: once the Rance 9 save format stabilizes, bump save version
	// and load based on version check
	if (parts_multi_controller) {
		parts->controller_no = iarray_read(r);
		parts->pass_cursor = iarray_read(r);
		parts->lock_input_state = iarray_read(r);
		parts->margin_top = iarray_read(r);
		parts->margin_bottom = iarray_read(r);
		parts->margin_left = iarray_read(r);
		parts->margin_right = iarray_read(r);
		parts->draggable = iarray_read(r);
	}

	int motion_count = iarray_read(r);
	for (int i = 0; i < motion_count; i++) {
		struct parts_motion *motion = load_parts_motion(r);
		parts_add_motion(parts, motion);
	}

	parts_list_resort(parts);
	parts_component_dirty(parts);
	parts_recalculate_hitbox(parts);
}

static void save_numeral_fonts(struct iarray_writer *w)
{
	iarray_write(w, parts_nr_numeral_fonts);
	for (int i = 0; i < parts_nr_numeral_fonts; i++) {
		struct parts_numeral_font *font = &parts_numeral_fonts[i];
		iarray_write(w, font->type);
		iarray_write(w, font->cg_no);
		for (int i = 0; i < 12; i++) {
			iarray_write(w, font->width[i]);
		}
	}
}

static void load_numeral_fonts(struct iarray_reader *r)
{
	parts_nr_numeral_fonts = iarray_read(r);
	parts_numeral_fonts = xcalloc(parts_nr_numeral_fonts, sizeof(struct parts_numeral_font));
	for (int i = 0; i < parts_nr_numeral_fonts; i++) {
		struct parts_numeral_font *font = &parts_numeral_fonts[i];
		font->type = iarray_read(r);
		font->cg_no = iarray_read(r);
		for (int i = 0; i < 12; i++) {
			font->width[i] = iarray_read(r);
		}
		parts_numeral_font_init(font);
	}
}

static bool parts_engine_save(struct page **buffer, bool save_hidden)
{
	// get parts into clean state first
	PE_UpdateComponent(0);

	struct iarray_writer w;
	iarray_init_writer(&w, "XPE");
	// Older engines retain their byte-for-byte v3 layout.
	int version = ain->version >= 14 ? CURRENT_SAVE_VERSION : 3;
	iarray_write(&w, version);
	if (CURRENT_SAVE_VERSION > 1)
		save_numeral_fonts(&w);

	// TODO: once the Rance 9 save format stabilizes, bump save version
	// and save unconditionally
	if (parts_multi_controller) {
		// v14: the top controller acts while none is designated.
		int active = ain->version >= 14 && ctrl_stack.active < 0
			? ctrl_stack.nr_controllers - 1
			: save_controller_no(ctrl_stack.active);
		iarray_write(&w, active);
		iarray_write(&w, ctrl_stack.nr_controllers);
	}

	unsigned count_pos = iarray_writer_pos(&w);
	iarray_write(&w, 0); // size of parts list

	unsigned count = 0;
	struct parts *parts;
	PARTS_LIST_FOREACH(parts) {
		if (!save_hidden && !parts->global.show)
			continue;
		if (!parts->want_save || parts_is_controller_root(parts))
			continue;
		save_parts(&w, parts, version);
		count++;
	}

	iarray_write_at(&w, count_pos, count);

	if (version >= 7) {
		// 編輯上表示 = 0 is set by the pactex loader, which loading does
		// not run again; without it a parts hidden in the editor would
		// show (and take the cursor) after loading. The original keeps
		// the flag in each component's own parameter block (written at
		// 0x5510ad, read at 0x551955); here the numbers follow the parts
		// list, so every parts record keeps its layout. Saving a single
		// parts (SaveParts／LoadParts), once implemented, has to carry
		// the flag as well.
		unsigned hidden_pos = iarray_writer_pos(&w);
		iarray_write(&w, 0); // size of the list
		unsigned hidden = 0;
		PARTS_LIST_FOREACH(parts) {
			if (!save_hidden && !parts->global.show)
				continue;
			if (!parts->want_save || !parts->edit_hidden || parts_is_controller_root(parts))
				continue;
			iarray_write(&w, parts->no);
			hidden++;
		}
		iarray_write_at(&w, hidden_pos, hidden);
	}

	if (version >= 8) {
		// 減算色モード (SetComponentSubColorMode, pactex 減算色模式), kept
		// like the list above: the numbers of the parts that have it.
		unsigned sub_pos = iarray_writer_pos(&w);
		iarray_write(&w, 0); // size of the list
		unsigned sub = 0;
		PARTS_LIST_FOREACH(parts) {
			if (!save_hidden && !parts->global.show)
				continue;
			if (!parts->want_save || !parts->sub_color_mode)
				continue;
			iarray_write(&w, parts->no);
			sub++;
		}
		iarray_write_at(&w, sub_pos, sub);
	}

	// The controller ID section is the end of the save, after every list
	// (load_controller_ids takes it from there).
	if (version >= 7 && parts_multi_controller)
		save_controller_ids(&w);

	if (*buffer) {
		delete_page_vars(*buffer);
		free_page(*buffer);
	}
	*buffer = iarray_to_page(&w);;
	iarray_free_writer(&w);
	return true;

}

bool PE_SaveWithoutHideParts(struct page **buffer)
{
	return parts_engine_save(buffer, false);
}

bool PE_Save(struct page **buffer)
{
	return parts_engine_save(buffer, true);
}

bool PE_Load(struct page **buffer)
{
	if (!(*buffer)) {
		WARNING("savedata array is empty");
		return false;
	}

	struct iarray_reader r;
	if (!iarray_init_reader(&r, *buffer, "XPE")) {
		WARNING("unrecognized savedata magic");
		return false;
	}
	int version = iarray_read(&r);
	if (version > CURRENT_SAVE_VERSION) {
		WARNING("unrecognized savedata version");
		return false;
	}

	parts_release_all();

	if (version > 1)
		load_numeral_fonts(&r);

	// TODO: once the Rance 9 save format stabilizes, bump save version
	// and load based on version check
	int active = -1;
	int *ids = NULL;
	if (parts_multi_controller) {
		active = iarray_read(&r);
		int nr = iarray_read(&r);
		int active_id;
		ids = load_controller_ids(&r, version, nr, &active_id);
		parts_controller_set_stack(nr, ids);
		if (ids)
			active = active_id;
		if (ain->version >= 14)
			ctrl_stack.active = -1;	// made the loaded one below
		else
			ctrl_stack.active = active;
	}

	int nr_parts = iarray_read(&r);
	if (nr_parts < 0) {
		WARNING("invalid parts count");
		free(ids);
		return false;
	}

	for (int i = 0; i < nr_parts; i++) {
		load_parts(&r, version);
	}

	if (version >= 7) {
		// Applied once the parents are attached (parts_update_component).
		int nr_hidden = iarray_read(&r);
		for (int i = 0; i < nr_hidden; i++) {
			int no = iarray_read(&r);
			if (r.error)
				break;
			struct parts *parts = parts_try_get(no);
			if (parts)
				parts_set_edit_hidden(parts, true);
		}
	}
	if (version >= 8) {
		int nr_sub = iarray_read(&r);
		for (int i = 0; i < nr_sub; i++) {
			int no = iarray_read(&r);
			if (r.error)
				break;
			struct parts *parts = parts_try_get(no);
			if (parts)
				parts_set_sub_color_mode(parts, true);
		}
	}
	// (the controller ID section follows the lists; it was read first)
	if (parts_multi_controller && ain->version >= 14) {
		load_controller_finish(active, ids != NULL);
		static bool warned;
		if (!ids && !warned) {
			warned = true;
			WARNING("PartsEngine save without controller IDs (written before a layer's ID "
				"became its root parts number): layer IDs the script kept from it are not valid");
		}
	}
	free(ids);

	// v14: the matrices and the add colour sums are not saved, and a child
	// is linked to its parent by the update (pending_parent). Without it a
	// child would be drawn, hit and measured at its own position, as if it
	// had no parent, until the next frame's update. Before v14 the saved
	// global position is what is drawn, as it always was.
	if (ain->version >= 14)
		PE_UpdateComponent(0);

	parts_engine_clean();
	return true;
}
