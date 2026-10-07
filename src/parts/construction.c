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

#include <assert.h>
#include <math.h>
#include "system4.h"
#include "system4/cg.h"
#include "system4/string.h"

#include "xsystem4.h"
#include "asset_manager.h"
#include "parts.h"
#include "parts_internal.h"

static struct parts_construction_process *get_cproc(int parts_no, int state)
{
	return parts_get_construction_process(parts_get(parts_no), state);
}

void parts_cp_op_free(struct parts_cp_op *op)
{
	switch (op->type) {
	case PARTS_CP_CREATE:
	case PARTS_CP_CREATE_PIXEL_ONLY:
	case PARTS_CP_CG:
	case PARTS_CP_FILL:
	case PARTS_CP_FILL_ALPHA_COLOR:
	case PARTS_CP_FILL_AMAP:
	case PARTS_CP_FILL_WITH_ALPHA:
	case PARTS_CP_DRAW_RECT:
	case PARTS_CP_DRAW_CUT_CG:
	case PARTS_CP_COPY_CUT_CG:
	case PARTS_CP_GRAY_FILTER:
	case PARTS_CP_FILL_PIE_AMAP:
		break;
	case PARTS_CP_DRAW_TEXT:
	case PARTS_CP_COPY_TEXT:
		free_string(op->text.text);
		break;
	}
	free(op);
}

void parts_add_cp_op(struct parts_construction_process *cproc, struct parts_cp_op *op)
{
	TAILQ_INSERT_TAIL(&cproc->ops, op, entry);
}

bool PE_ClearPartsConstructionProcess(int parts_no, int state);

bool PE_AddCreateToPartsConstructionProcess(int parts_no, int w, int h, int state)
{
	if (!parts_state_valid(--state))
		return false;

	struct parts_construction_process *cproc = get_cproc(parts_no, state);
	struct parts_cp_op *op = xcalloc(1, sizeof(struct parts_cp_op));
	op->type = PARTS_CP_CREATE;
	op->create.w = w;
	op->create.h = h;
	parts_add_cp_op(cproc, op);
	return true;
}

bool PE_AddCreatePixelOnlyToPartsConstructionProcess(int parts_no, int w, int h, int state)
{
	if (!parts_state_valid(--state))
		return false;

	struct parts_construction_process *cproc = get_cproc(parts_no, state);
	struct parts_cp_op *op = xcalloc(1, sizeof(struct parts_cp_op));
	op->type = PARTS_CP_CREATE_PIXEL_ONLY;
	op->create.w = w;
	op->create.h = h;
	parts_add_cp_op(cproc, op);
	return true;
}

bool PE_AddCreateCGToProcess(int parts_no, struct string *cg_name, int state)
{
	if (!parts_state_valid(--state))
		return false;

	int no;
	if (!asset_exists_by_name(ASSET_CG, cg_name->text, &no)) {
		WARNING("Invalid CG name: %s", display_sjis0(cg_name->text));
		return false;
	}

	struct parts_construction_process *cproc = get_cproc(parts_no, state);
	struct parts_cp_op *op = xcalloc(1, sizeof(struct parts_cp_op));
	op->type = PARTS_CP_CG;
	op->cg.no = no;
	parts_add_cp_op(cproc, op);
	return true;
}

bool PE_AddFillToPartsConstructionProcess(int parts_no, int x, int y, int w, int h, int r, int g, int b, int state)
{
	if (!parts_state_valid(--state))
		return false;

	struct parts_construction_process *cproc = get_cproc(parts_no, state);
	struct parts_cp_op *op = xcalloc(1, sizeof(struct parts_cp_op));
	op->type = PARTS_CP_FILL;
	op->fill = (struct parts_cp_fill) {
		.x = x, .y = y, .w = w, .h = h,
		.r = r, .g = g, .b = b, .a = 255
	};

	parts_add_cp_op(cproc, op);
	return true;
}

bool PE_AddFillAlphaColorToPartsConstructionProcess(int parts_no, int x, int y, int w, int h, int r, int g, int b, int a, int state)
{
	if (!parts_state_valid(--state))
		return false;

	struct parts_construction_process *cproc = get_cproc(parts_no, state);
	struct parts_cp_op *op = xcalloc(1, sizeof(struct parts_cp_op));
	op->type = PARTS_CP_FILL_ALPHA_COLOR;
	op->fill = (struct parts_cp_fill) {
		.x = x, .y = y, .w = w, .h = h,
		.r = r, .g = g, .b = b, .a = a
	};

	parts_add_cp_op(cproc, op);
	return true;
}

bool PE_AddFillAMapToPartsConstructionProcess(int parts_no, int x, int y, int w, int h, int a, int state)
{
	if (!parts_state_valid(--state))
		return false;

	struct parts_construction_process *cproc = get_cproc(parts_no, state);
	struct parts_cp_op *op = xcalloc(1, sizeof(struct parts_cp_op));
	op->type = PARTS_CP_FILL_AMAP;
	op->fill = (struct parts_cp_fill) {
		.x = x, .y = y, .w = w, .h = h,
		.r = 0, .g = 0, .b = 0, .a = a
	};

	parts_add_cp_op(cproc, op);
	return true;
}

bool PE_AddFillWithAlphaToPartsConstructionProcess(int parts_no, int x, int y, int w, int h,
		int r, int g, int b, int a, int state)
{
	if (!parts_state_valid(--state))
		return false;

	struct parts_construction_process *cproc = get_cproc(parts_no, state);
	struct parts_cp_op *op = xcalloc(1, sizeof(struct parts_cp_op));
	op->type = PARTS_CP_FILL_WITH_ALPHA;
	op->fill = (struct parts_cp_fill) {
		.x = x, .y = y, .w = w, .h = h,
		.r = r, .g = g, .b = b, .a = a
	};

	parts_add_cp_op(cproc, op);
	return true;
}

bool PE_AddFillGradationHorizonToPartsConstructionProcess(int parts_no, int x, int y, int w, int h,
		int top_r, int top_g, int top_b, int bot_r, int bot_g, int bot_b, int state);

bool PE_AddDrawRectToPartsConstructionProcess(int parts_no, int x, int y, int w, int h,
		int r, int g, int b, int state)
{
	if (!parts_state_valid(--state))
		return false;

	struct parts_construction_process *cproc = get_cproc(parts_no, state);
	struct parts_cp_op *op = xcalloc(1, sizeof(struct parts_cp_op));
	op->type = PARTS_CP_DRAW_RECT;
	op->fill = (struct parts_cp_fill) {
		.x = x, .y = y, .w = w, .h = h,
		.r = r, .g = g, .b = b, .a = 255
	};

	parts_add_cp_op(cproc, op);
	return true;
}

bool PE_AddDrawCutCGToPartsConstructionProcess(int parts_no, struct string *cg_name,
		int dx, int dy, int dw, int dh, int sx, int sy, int sw, int sh, int interp_type, int state)
{
	if (!parts_state_valid(--state))
		return false;

	int cg_no;
	if (!asset_exists_by_name(ASSET_CG, cg_name->text, &cg_no)) {
		WARNING("Invalid CG name: %s", display_sjis0(cg_name->text));
		return false;
	}

	struct parts_construction_process *cproc = get_cproc(parts_no, state);
	struct parts_cp_op *op = xcalloc(1, sizeof(struct parts_cp_op));
	op->type = PARTS_CP_DRAW_CUT_CG;
	op->cut_cg = (struct parts_cp_cut_cg) {
		.cg_no = cg_no,
		.dx = dx, .dy = dy, .dw = dw, .dh = dh,
		.sx = sx, .sy = sy, .sw = sw, .sh = sh,
		.interp_type = interp_type
	};

	parts_add_cp_op(cproc, op);
	return true;
}

bool PE_AddCopyCutCGToPartsConstructionProcess(int parts_no, struct string *cg_name,
		int dx, int dy, int dw, int dh, int sx, int sy, int sw, int sh, int interp_type, int state)
{
	if (!parts_state_valid(--state))
		return false;

	int cg_no;
	if (!asset_exists_by_name(ASSET_CG, cg_name->text, &cg_no)) {
		WARNING("Invalid CG name: %s", display_sjis0(cg_name->text));
		return false;
	}

	struct parts_construction_process *cproc = get_cproc(parts_no, state);
	struct parts_cp_op *op = xcalloc(1, sizeof(struct parts_cp_op));
	op->type = PARTS_CP_COPY_CUT_CG;
	op->cut_cg = (struct parts_cp_cut_cg) {
		.cg_no = cg_no,
		.dx = dx, .dy = dy, .dw = dw, .dh = dh,
		.sx = sx, .sy = sy, .sw = sw, .sh = sh,
		.interp_type = interp_type
	};

	parts_add_cp_op(cproc, op);
	return true;
}

bool PE_AddGrayFilterToPartsConstructionProcess(int parts_no, int x, int y, int w, int h,
		bool full_size, int state)
{
	if (!parts_state_valid(--state))
		return false;

	struct parts_construction_process *cproc = get_cproc(parts_no, state);
	struct parts_cp_op *op = xcalloc(1, sizeof(struct parts_cp_op));
	op->type = PARTS_CP_GRAY_FILTER;
	op->filter = (struct parts_cp_filter) {
		.x = x, .y = y, .w = w, .h = h,
		.full_size = full_size
	};

	parts_add_cp_op(cproc, op);
	return true;
}

bool PE_AddFillPieAMapToPartsConstructionProcess(int parts_no, int x, int y, int rx, int ry,
		int start_angle, int sweep_angle, int a, int angle, int state)
{
	if (!parts_state_valid(--state))
		return false;

	struct parts_construction_process *cproc = get_cproc(parts_no, state);
	struct parts_cp_op *op = xcalloc(1, sizeof(struct parts_cp_op));
	op->type = PARTS_CP_FILL_PIE_AMAP;
	op->pie = (struct parts_cp_pie) {
		.x = x, .y = y, .rx = rx, .ry = ry,
		.start = start_angle, .sweep = sweep_angle,
		.a = a, .angle = angle
	};

	parts_add_cp_op(cproc, op);
	return true;
}

bool PE_AddAddFilterToPartsConstructionProcess(int parts_no, int x, int y, int w, int h,
		int r, int g, int b, bool full_size, int state);
bool PE_AddMulFilterToPartsConstructionProcess(int parts_no, int x, int y, int w, int h,
		int r, int g, int b, bool full_size, int state);
bool PE_AddDrawLineToPartsConstructionProcess(int parts_no, int x1, int y1, int x2, int y2,
		int r, int g, int b, int a, int state);

static bool add_text_to_cproc(int parts_no, int x, int y, struct string *text,
		int type, int size, int r, int g, int b, float bold_weight,
		int edge_r, int edge_g, int edge_b, float edge_weight,
		int char_space, int line_space, int state, enum parts_cp_op_type op_type)
{
	if (!parts_state_valid(--state))
		return false;

	struct parts_cp_op *op = xcalloc(1, sizeof(struct parts_cp_op));
	op->type = op_type;
	op->text = (struct parts_cp_text) {
		.text = string_dup(text),
		.x = x,
		.y = y,
		.line_space = line_space,
		.style = {
			.face = type,
			.size = size,
			.bold_width = bold_weight,
			.weight = 0,
			.edge_left = edge_weight,
			.edge_up = edge_weight,
			.edge_right = edge_weight,
			.edge_down = edge_weight,
			.color = { r, g, b, 255 },
			.edge_color = { edge_r, edge_g, edge_b, 255 },
			.scale_x = 1.0f,
			.space_scale_x = 1.0f,
			.font_spacing = char_space
		}
	};

	parts_add_cp_op(get_cproc(parts_no, state), op);
	return true;

}


bool PE_AddDrawTextToPartsConstructionProcess(int parts_no, int x, int y, struct string *text,
		int type, int size, int r, int g, int b, float bold_weight,
		int edge_r, int edge_g, int edge_b, float edge_weight,
		int char_space, int line_space, int state)
{
	return add_text_to_cproc(parts_no, x, y, text, type, size, r, g, b, bold_weight,
			edge_r, edge_g, edge_b, edge_weight, char_space, line_space, state,
			PARTS_CP_DRAW_TEXT);
}

bool PE_AddCopyTextToPartsConstructionProcess(int parts_no, int x, int y, struct string *text,
		int type, int size, int r, int g, int b, float bold_weight,
		int edge_r, int edge_g, int edge_b, float edge_weight,
		int char_space, int line_space, int state)
{
	return add_text_to_cproc(parts_no, x, y, text, type, size, r, g, b, bold_weight,
			edge_r, edge_g, edge_b, edge_weight, char_space, line_space, state,
			PARTS_CP_COPY_TEXT);
}

static void build_create(struct parts *parts, struct parts_construction_process *cproc,
		struct parts_cp_create *op)
{
	gfx_delete_texture(&cproc->common.texture);
	gfx_init_texture_rgba(&cproc->common.texture, op->w, op->h, (SDL_Color){0,0,0,255});
	parts_set_dims(parts, &cproc->common, op->w, op->h);
}

static void build_create_pixel_only(struct parts *parts, struct parts_construction_process *cproc,
		struct parts_cp_create *op)
{
	gfx_delete_texture(&cproc->common.texture);
	gfx_init_texture_rgb(&cproc->common.texture, op->w, op->h, (SDL_Color){0,0,0,255});
	parts_set_dims(parts, &cproc->common, op->w, op->h);
}

static void build_cg(struct parts *parts, struct parts_construction_process *cproc, struct parts_cp_cg *op)
{
	struct cg *cg = asset_cg_load(op->no);
	assert(cg);
	gfx_delete_texture(&cproc->common.texture);
	gfx_init_texture_with_cg(&cproc->common.texture, cg);
	parts_set_dims(parts, &cproc->common, cg->metrics.w, cg->metrics.h);
	cg_free(cg);
}

static void build_fill(struct parts_construction_process *cproc, struct parts_cp_fill *op)
{
	gfx_fill(&cproc->common.texture, op->x, op->y, op->w, op->h, op->r, op->g, op->b);
}

static void build_fill_alpha_color(struct parts_construction_process *cproc, struct parts_cp_fill *op)
{
	gfx_fill_alpha_color(&cproc->common.texture, op->x, op->y, op->w, op->h, op->r, op->g, op->b, op->a);
}

static void build_fill_amap(struct parts_construction_process *cproc, struct parts_cp_fill *op)
{
	gfx_fill_amap(&cproc->common.texture, op->x, op->y, op->w, op->h, op->a);
}

static void build_fill_with_alpha(struct parts_construction_process *cproc, struct parts_cp_fill *op)
{
	gfx_fill_with_alpha(&cproc->common.texture, op->x, op->y, op->w, op->h, op->r, op->g, op->b, op->a);
}

static void build_draw_rect(struct parts_construction_process *cproc, struct parts_cp_fill *op)
{
	int x2 = op->x + op->w - 1;
	int y2 = op->y + op->h - 1;
	gfx_draw_line(&cproc->common.texture, op->x, op->y, x2, op->y, op->r, op->g, op->b);
	gfx_draw_line(&cproc->common.texture, op->x, op->y, op->x, y2, op->r, op->g, op->b);
	gfx_draw_line(&cproc->common.texture, x2, op->y, x2, y2, op->r, op->g, op->b);
	gfx_draw_line(&cproc->common.texture, op->x, y2, x2, y2, op->r, op->g, op->b);
}

static void build_draw_cut_cg(struct parts_construction_process *cproc, struct parts_cp_cut_cg *op)
{
	struct cg *cg = asset_cg_load(op->cg_no);
	assert(cg);

	Texture src;
	gfx_init_texture_with_cg(&src, cg);
	cg_free(cg);

	gfx_copy_stretch_blend_amap(&cproc->common.texture, op->dx, op->dy, op->dw, op->dh,
			&src, op->sx, op->sy, op->sw, op->sh);
	gfx_delete_texture(&src);
}

static void build_copy_cut_cg(struct parts_construction_process *cproc, struct parts_cp_cut_cg *op)
{
	struct cg *cg = asset_cg_load(op->cg_no);
	assert(cg);

	Texture src;
	gfx_init_texture_with_cg(&src, cg);
	cg_free(cg);

	gfx_copy_stretch_with_alpha_map(&cproc->common.texture, op->dx, op->dy, op->dw, op->dh,
			&src, op->sx, op->sy, op->sw, op->sh);
	gfx_delete_texture(&src);
}

static void build_draw_text(struct parts_construction_process *cproc, struct parts_cp_text *op)
{
	gfx_render_text(&cproc->common.texture, op->x, op->y, op->text->text, &op->style, true);
}

static void build_copy_text(struct parts_construction_process *cproc, struct parts_cp_text *op)
{
	int w = ceilf(gfx_size_text (&op->style, op->text->text));
	int h = ceilf(op->style.size + op->style.edge_up + op->style.edge_down);
	gfx_fill_with_alpha(&cproc->common.texture, op->x, op->y, w, h,
			op->style.edge_color.r, op->style.edge_color.g, op->style.edge_color.b, 0);
	gfx_render_text(&cproc->common.texture, op->x, op->y, op->text->text, &op->style, false);
}

static void build_gray_filter(struct parts_construction_process *cproc, struct parts_cp_filter *op)
{
	int x = op->x, y = op->y, w = op->w, h = op->h;
	if (op->full_size) {
		x = 0; y = 0;
		w = cproc->common.texture.w;
		h = cproc->common.texture.h;
	}

	gfx_copy_grayscale(&cproc->common.texture, x, y, &cproc->common.texture, x, y, w, h);
}

/*
 * FillPieAMap (v14 construction command 122, native 0x5093d0): replaces the
 * alpha in a sector of an ellipse, antialiased.
 *
 * The scan (0x4d0280) covers the 2r x 2r pixels around the centre, r being
 * the larger radius. A pixel is in the sector when the angle of its top-left
 * corner, in 1/65536 turns and truncated, lies in [start, start + sweep],
 * both ends included; the angles are degrees modulo 360 (0x4e5e50), and a
 * range that ends before it starts is the union of its two parts (0x4eb0c0).
 * Negative angles select nothing (0x4eae00). The coverage (0x4e6680) is the
 * number n of the pixel's 8x8 subsamples, at offsets k/8 from that corner,
 * that lie in the ellipse; a pixel with n > 0 gets a * (n * 255 / 64) / 255
 * (0x4d05c0, stored by 0x5b0a10) and the others keep their alpha. The
 * original multiplies by the reciprocal of each radius (0x4e6738) and rounds
 * the two squares before adding them; both decide the subsamples that lie
 * exactly on the ellipse (e.g. (6, 8) for r 10, (5, 12) for r 13), so the
 * products below stay separate statements.
 *
 * The rotation (0x4eafc0) is not implemented. Operations also come from
 * save data, so what the pactex loader would not pass is checked again here:
 * a rotation, radii above PARTS_CP_PIE_MAX_RADIUS and angles beyond one turn
 * draw nothing.
 */
static void build_fill_pie_amap(struct parts_construction_process *cproc, struct parts_cp_pie *op)
{
	Texture *t = &cproc->common.texture;
	if (op->rx <= 0 || op->ry <= 0 || op->start < 0 || op->sweep < 0)
		return;
	if (op->angle || op->rx > PARTS_CP_PIE_MAX_RADIUS || op->ry > PARTS_CP_PIE_MAX_RADIUS
			|| op->start > 360 || op->sweep > 360) {
		WARNING("FillPieAMap: unsupported sector");
		return;
	}
	if (!t->handle || t->w <= 0 || t->h <= 0)
		return;

	const int start = (op->start % 360) * (65536.0 / 360.0);
	const int end = ((op->start + op->sweep) % 360) * (65536.0 / 360.0);
	const double r = max(op->rx, op->ry);
	const int size = r + r;
	const double inv_rx = 1.0 / op->rx;
	const double inv_ry = 1.0 / op->ry;

	uint8_t *pixels = gfx_get_pixels(t);
	for (int row = 0; row < size; row++) {
		const double dy = row - r;
		for (int col = 0; col < size; col++) {
			const double dx = col - r;
			// 65536 / 2pi (0x8132b8)
			double turn = atan2(dy, dx) * 10430.378350470453;
			if (turn < 0.0)
				turn += 65536.0;
			const int angle = turn;
			if (start > end ? angle < start && angle > end : angle < start || angle > end)
				continue;

			int n = 0;
			for (int sy = 0; sy < 8; sy++) {
				const double v = (dy + sy * 0.125) * inv_ry;
				const double vv = v * v;
				for (int sx = 0; sx < 8; sx++) {
					const double u = (dx + sx * 0.125) * inv_rx;
					const double uu = u * u;
					if (!(1.0 < vv + uu))
						n++;
				}
			}
			if (!n)
				continue;

			// The centre is (x + 0.5, y + 0.5); the conversion truncates
			// towards zero (0x4d03ba), so the column and the row just
			// outside the left and top edges land on the first one.
			const double px = op->x + 0.5 + dx;
			const double py = op->y + 0.5 + dy;
			if (px <= -1.0 || px >= t->w || py <= -1.0 || py >= t->h)
				continue;
			// Scaled first (0x4d05c0), then clamped (0x5b0a10).
			const int64_t a = (int64_t)op->a * (n * 255 / 64) / 255;
			pixels[((int)py * t->w + (int)px) * 4 + 3] = min(max(a, 0), 255);
		}
	}
	gfx_update_texture_with_pixels(t, pixels);
	free(pixels);
}

bool parts_build_construction_process(struct parts *parts,
		struct parts_construction_process *cproc)
{
	// CreatePixelOnly makes a surface without alpha, on which the original
	// fails FillPieAMap (0x50940e); the texture is left as it is. A Texture
	// does not record its format, so this follows the operations that
	// create the surface.
	bool has_alpha = true;
	struct parts_cp_op *op;
	TAILQ_FOREACH(op, &cproc->ops, entry) {
		switch (op->type) {
		case PARTS_CP_CREATE:
			build_create(parts, cproc, &op->create);
			has_alpha = true;
			break;
		case PARTS_CP_CREATE_PIXEL_ONLY:
			build_create_pixel_only(parts, cproc, &op->create);
			has_alpha = false;
			break;
		case PARTS_CP_CG:
			build_cg(parts, cproc, &op->cg);
			has_alpha = true;
			break;
		case PARTS_CP_FILL:
			build_fill(cproc, &op->fill);
			break;
		case PARTS_CP_FILL_ALPHA_COLOR:
			build_fill_alpha_color(cproc, &op->fill);
			break;
		case PARTS_CP_FILL_AMAP:
			build_fill_amap(cproc, &op->fill);
			break;
		case PARTS_CP_FILL_WITH_ALPHA:
			build_fill_with_alpha(cproc, &op->fill);
			break;
		case PARTS_CP_DRAW_RECT:
			build_draw_rect(cproc, &op->fill);
			break;
		case PARTS_CP_DRAW_CUT_CG:
			build_draw_cut_cg(cproc, &op->cut_cg);
			break;
		case PARTS_CP_COPY_CUT_CG:
			build_copy_cut_cg(cproc, &op->cut_cg);
			break;
		case PARTS_CP_DRAW_TEXT:
			build_draw_text(cproc, &op->text);
			break;
		case PARTS_CP_COPY_TEXT:
			build_copy_text(cproc, &op->text);
			break;
		case PARTS_CP_GRAY_FILTER:
			build_gray_filter(cproc, &op->filter);
			break;
		case PARTS_CP_FILL_PIE_AMAP:
			if (has_alpha)
				build_fill_pie_amap(cproc, &op->pie);
			break;
		}
	}
	parts_dirty(parts);
	return true;
}

bool PE_BuildPartsConstructionProcess(int parts_no, int state)
{
	if (!parts_state_valid(--state))
		return false;

	struct parts *parts = parts_get(parts_no);
	struct parts_construction_process *cproc = parts_get_construction_process(parts, state);
	return parts_build_construction_process(parts, cproc);
}

bool parts_clear_construction_process(struct parts_construction_process *cproc)
{
	while (!TAILQ_EMPTY(&cproc->ops)) {
		struct parts_cp_op *op = TAILQ_FIRST(&cproc->ops);
		TAILQ_REMOVE(&cproc->ops, op, entry);
		parts_cp_op_free(op);
	}
	return true;
}

bool PE_ClearPartsConstructionProcess(int parts_no, int state)
{
	if (!parts_state_valid(--state))
		return false;
	struct parts *parts = parts_get(parts_no);
	struct parts_construction_process *cproc = parts_get_construction_process(parts, state);
	return parts_clear_construction_process(cproc);
}

/*
 * v14 パネル (component type 14). The native widget rebuilds its surface
 * when its size or colour changed (0x4dcc10, from the update 0x4dc940). A
 * width or height that is not positive ends the rebuild at once (0x4dcc69:
 * the steps and the surface stay as they were). Otherwise the steps of its
 * inner construction state are cleared (0x568f40), command 0 creates the
 * surface (0x568fa0 -> 0x5a6980) and command 6 fills all of it with the
 * colour (0x569200 -> 0x5a6df0), alpha included: a dialogue's dimmer is
 * (0,0,0,128) and shows the scene behind it.
 *
 * What the original does and this does not:
 *   - the alpha gradation of the four edges, added after the fill (commands
 *     25 and 26, 0x569600 / 0x569670);
 *   - the rebuild waits for the next update; here it is done at the call;
 *   - the widget reports its width and height fields as the size of the
 *     parts (vtable 0x80eea4 +0x20 = 0x4dce10); here the size is the
 *     surface's. A parts that was only made a panel (SetComponentType 14)
 *     has no size, not 200x200, so CPanelParts@PanelWidth::set, which reads
 *     the height back from the parts, asks for a height of 0 and builds
 *     nothing; and a size that is not positive leaves the old surface's;
 *   - the update adds a growth to the size (0x4dc94d), the anchors' by its
 *     caller (0x53538c); anchors do not resize a parts here. Every panel of
 *     the game's pactex has none;
 *   - turning a parts into a panel replaces the whole widget (0x536ff0);
 *     here the other states, and the normal state while the size is not
 *     positive, keep what they had;
 *   - `valid` outlives a change of type: a panel that became something else
 *     and then a panel again keeps its values, where the original's are the
 *     constructor's again.
 *
 * The values of a parts that has none yet are the constructor's, or, for a
 * parts that is a panel already, those of its operations. That is a loaded
 * panel, but also a construction parts that SetComponentType made a panel
 * before the first panel call (the original's has the constructor's).
 */
struct parts_panel *parts_get_panel(struct parts *parts)
{
	struct parts_panel *panel = &parts->panel;
	if (panel->valid)
		return panel;
	*panel = (struct parts_panel) { .valid = true, .w = 200, .h = 200, .r = 220, .g = 220, .b = 220, .a = 255 };
	if (parts->component_type != 14 || parts->states[0].type != PARTS_CONSTRUCTION_PROCESS)
		return panel;
	struct parts_cp_op *op = TAILQ_FIRST(&parts->states[0].cproc.ops);
	if (!op || op->type != PARTS_CP_CREATE)
		return panel;
	panel->w = op->create.w;
	panel->h = op->create.h;
	op = TAILQ_NEXT(op, entry);
	if (op && op->type == PARTS_CP_FILL_WITH_ALPHA) {
		panel->r = op->fill.r;
		panel->g = op->fill.g;
		panel->b = op->fill.b;
		panel->a = op->fill.a;
	}
	return panel;
}

static void panel_rebuild(struct parts *parts)
{
	struct parts_panel *panel = &parts->panel;
	if (panel->w <= 0 || panel->h <= 0)
		return;
	struct parts_construction_process *cproc = parts_get_construction_process(parts, 0);
	parts_clear_construction_process(cproc);
	PE_AddCreateToPartsConstructionProcess(parts->no, panel->w, panel->h, 1);
	PE_AddFillWithAlphaToPartsConstructionProcess(parts->no, 0, 0, panel->w, panel->h,
			panel->r, panel->g, panel->b, panel->a, 1);
	parts_build_construction_process(parts, cproc);
}

static bool panel_built(struct parts *parts)
{
	return parts->states[0].type == PARTS_CONSTRUCTION_PROCESS && parts->states[0].common.texture.handle;
}

// The pactex loader's (0x4dc440).
void parts_panel_init(struct parts *parts, int w, int h, int r, int g, int b, int a)
{
	parts->panel = (struct parts_panel) { .valid = true, .w = w, .h = h, .r = r, .g = g, .b = b, .a = a };
	panel_rebuild(parts);
}

// Native 0x5971a0: nothing for the size it already has.
void parts_panel_set_size(struct parts *parts, int w, int h)
{
	struct parts_panel *panel = parts_get_panel(parts);
	if (panel->w == w && panel->h == h && panel_built(parts))
		return;
	panel->w = w;
	panel->h = h;
	panel_rebuild(parts);
}

// Native 0x5971f0: nothing for the colour it already has.
void parts_panel_set_color(struct parts *parts, int r, int g, int b, int a)
{
	struct parts_panel *panel = parts_get_panel(parts);
	if (panel->r == r && panel->g == g && panel->b == b && panel->a == a && panel_built(parts))
		return;
	panel->r = r;
	panel->g = g;
	panel->b = b;
	panel->a = a;
	panel_rebuild(parts);
}

/*
 * A panel saved before its colour kept the alpha: Create, then the blending
 * fill (command 4) over all of it, once for each colour SetPanelColor was
 * given, which drew opaque. Loaded as the panel it was meant to be: one
 * FillWithAlpha of the last colour. A construction that is not this shape
 * is left as it was saved.
 */
void parts_panel_load_blended(struct parts *parts)
{
	if (parts->states[0].type != PARTS_CONSTRUCTION_PROCESS)
		return;
	struct parts_construction_process *cproc = &parts->states[0].cproc;
	struct parts_cp_op *create = TAILQ_FIRST(&cproc->ops);
	if (!create || create->type != PARTS_CP_CREATE || !TAILQ_NEXT(create, entry))
		return;
	struct parts_cp_op *op;
	for (op = TAILQ_NEXT(create, entry); op; op = TAILQ_NEXT(op, entry)) {
		if (op->type != PARTS_CP_FILL_ALPHA_COLOR || op->fill.x || op->fill.y
				|| op->fill.w != create->create.w || op->fill.h != create->create.h)
			return;
	}
	while ((op = TAILQ_NEXT(create, entry)) && TAILQ_NEXT(op, entry)) {
		TAILQ_REMOVE(&cproc->ops, op, entry);
		parts_cp_op_free(op);
	}
	op->type = PARTS_CP_FILL_WITH_ALPHA;
	parts_build_construction_process(parts, cproc);
}

bool PE_SetPartsConstructionSurfaceArea(int parts_no, int x, int y, int w, int h, int state)
{
	if (!parts_state_valid(--state))
		return false;

	struct parts *parts = parts_get(parts_no);
	struct parts_construction_process *cproc = parts_get_construction_process(parts, state);
	parts_set_surface_area(parts, &cproc->common, x, y, w, h);
	return true;
}
