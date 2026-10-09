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
#include <limits.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>
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
	case PARTS_CP_MUL_AMAP_GRADATION_ROWS:
	case PARTS_CP_MUL_AMAP_GRADATION_COLUMNS:
	case PARTS_CP_BLUR_H:
	case PARTS_CP_BLUR_V:
	case PARTS_CP_FILL_CIRCLE_AMAP:
	case PARTS_CP_FILL_CIRCLE_BLEND:
	case PARTS_CP_FILL_POLYGON_BLEND:
	case PARTS_CP_TILE_CG:
	case PARTS_CP_DRAW_CIRCLE_AMAP:
		free(op->pixel.points);
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

static bool add_pixel_op(int parts_no, int state, enum parts_cp_op_type type, struct parts_cp_pixel pixel)
{
	if (!parts_state_valid(--state))
		return false;

	struct parts_cp_op *op = xcalloc(1, sizeof(struct parts_cp_op));
	op->type = type;
	op->pixel = pixel;
	parts_add_cp_op(get_cproc(parts_no, state), op);
	return true;
}

bool PE_AddMulAMapGradationToPartsConstructionProcess(int parts_no, bool columns,
		int x, int y, int w, int h, bool full_size, int a1, int a2, int state)
{
	return add_pixel_op(parts_no, state, columns ? PARTS_CP_MUL_AMAP_GRADATION_COLUMNS
			: PARTS_CP_MUL_AMAP_GRADATION_ROWS, (struct parts_cp_pixel) {
		.x = x, .y = y, .w = w, .h = h, .full = full_size, .a = a1, .a2 = a2
	});
}

bool PE_AddBlurToPartsConstructionProcess(int parts_no, bool vertical,
		int x, int y, int w, int h, bool full_size, int radius, int state)
{
	return add_pixel_op(parts_no, state, vertical ? PARTS_CP_BLUR_V : PARTS_CP_BLUR_H,
			(struct parts_cp_pixel) {
		.x = x, .y = y, .w = w, .h = h, .full = full_size, .radius = radius
	});
}

bool PE_AddFillCircleToPartsConstructionProcess(int parts_no, bool blend, int x, int y, int radius,
		int r, int g, int b, int a, int state)
{
	return add_pixel_op(parts_no, state, blend ? PARTS_CP_FILL_CIRCLE_BLEND : PARTS_CP_FILL_CIRCLE_AMAP,
			(struct parts_cp_pixel) {
		.x = x, .y = y, .radius = radius, .r = r, .g = g, .b = b, .a = a
	});
}

bool PE_AddDrawCircleToPartsConstructionProcess(int parts_no, int x, int y, int radius, int line_width,
		int a, int state)
{
	return add_pixel_op(parts_no, state, PARTS_CP_DRAW_CIRCLE_AMAP, (struct parts_cp_pixel) {
		.x = x, .y = y, .radius = radius, .line_width = line_width, .a = a
	});
}

// `points` is x0, y0, x1, y1, ...; it is copied.
bool PE_AddFillPolygonToPartsConstructionProcess(int parts_no, int nr_points, const int *points,
		int r, int g, int b, int a, int state)
{
	if (nr_points < 0 || nr_points > PARTS_CP_POLYGON_MAX_POINTS || (nr_points && !points))
		return false;
	int *copy = NULL;
	if (nr_points) {
		copy = xmalloc(nr_points * 2 * sizeof(int));
		memcpy(copy, points, nr_points * 2 * sizeof(int));
	}
	if (!add_pixel_op(parts_no, state, PARTS_CP_FILL_POLYGON_BLEND, (struct parts_cp_pixel) {
			.r = r, .g = g, .b = b, .a = a, .nr_points = nr_points, .points = copy })) {
		free(copy);
		return false;
	}
	return true;
}

bool PE_AddTileCGToPartsConstructionProcess(int parts_no, struct string *cg_name,
		int x, int y, int w, int h, bool full_size, int state)
{
	int cg_no;
	if (!asset_exists_by_name(ASSET_CG, cg_name->text, &cg_no)) {
		WARNING("Invalid CG name: %s", display_sjis0(cg_name->text));
		return false;
	}
	return add_pixel_op(parts_no, state, PARTS_CP_TILE_CG, (struct parts_cp_pixel) {
		.x = x, .y = y, .w = w, .h = h, .full = full_size, .cg_no = cg_no
	});
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

/*
 * The commands below work on the surface's pixels, rows from the top, RGBA;
 * parts_build_construction_process reads the texture once for a run of them.
 */

/* [pos, pos + size) as 0x5abed0 normalizes it (a negative size extends the
 * other way) and clips it to [0, limit]; false when nothing is left
 * (0x5abde0). The original adds in 32 bits and wraps around; a sum outside
 * that range is not reproduced and counts as nothing left. */
static bool cp_clip_span(int *pos, int *size, int limit)
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
	return b > a;
}

static uint8_t cp_byte(int v)
{
	return min(max(v, 0), 255);
}

/* The composition of a staged pixel (r, g, b, a) onto the surface, as the
 * alpha-blend commands end (0x5acc00 -> 0x494420): each colour moves towards
 * the staged one by a / 256, rounded down (0x4944c6), and the alpha becomes
 * the larger of the two (0x4944f1). */
static void cp_blend_pixel(uint8_t *d, int r, int g, int b, int a)
{
	const int src[3] = { r, g, b };
	for (int c = 0; c < 3; c++) {
		const int v = (src[c] - d[c]) * a;
		// sar 8: rounds towards minus infinity
		d[c] += v >= 0 ? v >> 8 : -((255 - v) >> 8);
	}
	if (a > d[3])
		d[3] = a;
}

/*
 * MulAMapGradation (v14 construction commands 25 and 26, native 0x4ff740 and
 * 0x4ff8d0): line i of the n rows (25) or columns (26) of the rectangle has
 * its alpha multiplied by a1 + i * (a2 - a1) / n, the quotient truncated
 * (0x4ff7e8, 0x4ff983), as A * a / 255 with the low byte of a (0x5ac8d0,
 * 0x5aca3c). 全體 selects the whole surface. Each line is clipped on its own
 * and the original fails the command, and with it the steps that follow, at
 * the first line with nothing left (0x4ff805, 0x4ff99f); here the rest of
 * the command is skipped and the later operations still run.
 */
static void build_mul_amap_gradation(uint8_t *pixels, int tw, int th,
		const struct parts_cp_pixel *op, bool columns)
{
	int x = op->x, y = op->y, w = op->w, h = op->h;
	if (op->full) {
		x = 0; y = 0;
		w = tw; h = th;
	}
	const int n = columns ? w : h;
	const int64_t diff = (int64_t)op->a2 - op->a;
	for (int i = 0; i < n; i++) {
		const unsigned a = (unsigned)(i * diff / n + op->a) & 0xff;
		int lx = columns ? x + i : x, lw = columns ? 1 : w;
		int ly = columns ? y : y + i, lh = columns ? h : 1;
		if (!cp_clip_span(&lx, &lw, tw) || !cp_clip_span(&ly, &lh, th))
			return;
		for (int row = ly; row < ly + lh; row++) {
			uint8_t *p = pixels + ((size_t)row * tw + lx) * 4 + 3;
			for (int col = 0; col < lw; col++, p += 4)
				*p = *p * a / 255;
		}
	}
}

/*
 * One line of n pixels, `step` bytes apart, blurred from src into dst as
 * 0x493790 (rows) and 0x493b10 (columns) do: each of the three colours is
 * the sum of the 2r + 1 pixels around it divided by 2r + 1, truncated (the
 * table built at 0x493844). The first pixel stands in for those before the
 * line and the last for those after it. For the last r pixels the original
 * drops p[n - 2r + k] (0x493a60, 0x493e01) where the window's left end is
 * p[n - 2r - 1 + k]: those sums keep one pixel too far and miss a near one,
 * and so do these. The alpha is not written.
 */
static void cp_blur_line(uint8_t *dst, const uint8_t *src, int n, int step, int r)
{
	const int div = 2 * r + 1;
	for (int c = 0; c < 3; c++) {
		const uint8_t *p = src + c;
		uint8_t *o = dst + c;
		int sum = p[0] * r;
		for (int j = 0; j <= r; j++)
			sum += p[j * step];
		o[0] = sum / div;
		for (int i = 1; i <= r; i++) {
			sum += p[(r + i) * step] - p[0];
			o[i * step] = sum / div;
		}
		for (int k = 0; k < n - 2 * r - 1; k++) {
			sum += p[(2 * r + 1 + k) * step] - p[k * step];
			o[(r + 1 + k) * step] = sum / div;
		}
		for (int k = 0; k < r; k++) {
			sum += p[(n - 1) * step] - p[(n - 2 * r + k) * step];
			o[(n - r + k) * step] = sum / div;
		}
	}
}

/*
 * Blur (v14 construction commands 27 and 28, native 0x4ffa60 and 0x4ffba0):
 * a box blur of the rectangle (全體: the whole surface) along its rows (27,
 * 0x5b0d70) or its columns (28, 0x5b0fa0), read from a copy of the surface.
 * The radius is ブラー, 1 when it is smaller, and n / 2 - 1 for a line of n
 * pixels when it is n / 2 or more (0x49379f, 0x493b1f). That leaves 0 for a
 * line of 2 or 3 pixels, which changes nothing; for a shorter line, or one of
 * 2 with a radius below 1, the original reads beyond the line, and nothing
 * is done here.
 */
static void build_blur(uint8_t *pixels, int tw, int th, const struct parts_cp_pixel *op, bool vertical)
{
	int x = op->x, y = op->y, w = op->w, h = op->h;
	if (op->full) {
		x = 0; y = 0;
		w = tw; h = th;
	}
	if (!cp_clip_span(&x, &w, tw) || !cp_clip_span(&y, &h, th))
		return;
	const int n = vertical ? h : w;
	int r = op->radius;
	if (r < 1)
		r = 1;
	else if (r >= n / 2)
		r = n / 2 - 1;
	if (r < 1 || 2 * r + 1 > n)
		return;

	const size_t size = (size_t)tw * th * 4;
	uint8_t *src = xmalloc(size);
	memcpy(src, pixels, size);
	if (vertical) {
		for (int col = x; col < x + w; col++) {
			const size_t off = ((size_t)y * tw + col) * 4;
			cp_blur_line(pixels + off, src + off, h, tw * 4, r);
		}
	} else {
		for (int row = y; row < y + h; row++) {
			const size_t off = ((size_t)row * tw + x) * 4;
			cp_blur_line(pixels + off, src + off, w, 4, r);
		}
	}
	free(src);
}

/* The coverage of the cell whose corner is (dx, dy) from a circle's centre
 * (0x4e6530): the number of its 8x8 subsamples, at offsets k/8 from that
 * corner, within the radius, scaled to n * 255 / 64. In eighths, exactly. */
static int cp_circle_coverage(int dx, int dy, int r)
{
	const int64_t rr = (int64_t)64 * r * r;
	int n = 0;
	for (int j = 0; j < 8; j++) {
		const int64_t v = (int64_t)8 * dy + j;
		for (int i = 0; i < 8; i++) {
			const int64_t u = (int64_t)8 * dx + i;
			if (u * u + v * v <= rr)
				n++;
		}
	}
	return n * 255 / 64;
}

/*
 * FillCircle (v14 construction commands 102 and 106, native 0x507640 and
 * 0x507d60): an antialiased disc of 半徑 around (x + 0.5, y + 0.5). The scan
 * (0x4cfb40) takes the 2r + 1 positions centre - r, centre - r + 1, ... on
 * both axes; the pixel is the position truncated towards zero (0x4cfc39),
 * so the column and the row just left of and above the surface land on the
 * first one, and the coverage is that of the cell from the position's
 * offset to the centre. Cells without coverage write nothing. A radius that
 * is not positive draws nothing and succeeds.
 *   102 replaces the alpha by a * coverage / 255 (0x4cbbd0 -> 0x5b0a10).
 *   106 stages (r, g, b, a * coverage / 255) on a transparent surface of the
 *       same size (0x5a94d0, 0x4cbb20 -> 0x5b0b80), a later write to a pixel
 *       replacing an earlier one, and composes it (cp_blend_pixel). Only the
 *       scanned pixels are staged here; the others would not change.
 *       With nothing written (the disc lies outside the surface) the
 *       original fails the command and the steps after it (0x507ee2 ->
 *       0x507f2c); here the later operations still run.
 * The colour and the alpha are clamped to 0..255 here (0x5b0b80 was not
 * read for what it does with other values).
 */
static void build_fill_circle(uint8_t *pixels, int tw, int th, const struct parts_cp_pixel *op, bool blend)
{
	const int r = op->radius;
	if (r <= 0)
		return;
	if (r > PARTS_CP_CIRCLE_MAX_RADIUS) {
		WARNING("FillCircle: unsupported radius %d", r);
		return;
	}

	// The staged alpha of pixels x - r .. x + r + 1, y - r .. y + r + 1:
	// position p is pixel floor(p) = x - r + i, except -0.5, which is
	// pixel 0, one more than that (so 2r + 2 pixels a side, not 2r + 1).
	const int side = 2 * r + 2;
	const int64_t bx = (int64_t)op->x - r, by = (int64_t)op->y - r;
	uint8_t *staged = blend ? xcalloc(side, side) : NULL;
	for (int j = 0; j <= 2 * r; j++) {
		const double fy = op->y + 0.5 - r + j;
		if (fy <= -1.0 || fy >= th)
			continue;
		const int py = fy;
		for (int i = 0; i <= 2 * r; i++) {
			const double fx = op->x + 0.5 - r + i;
			if (fx <= -1.0 || fx >= tw)
				continue;
			const int px = fx;
			const int cov = cp_circle_coverage(i - r, j - r, r);
			if (cov <= 0)
				continue;
			// Scaled first, then clamped (0x5b0a10).
			const uint8_t a = cp_byte((int64_t)op->a * cov / 255);
			if (blend)
				staged[(py - by) * side + (px - bx)] = a;
			else
				pixels[((size_t)py * tw + px) * 4 + 3] = a;
		}
	}
	if (!blend)
		return;
	for (int j = 0; j < side; j++) {
		const int64_t py = by + j;
		if (py < 0 || py >= th)
			continue;
		for (int i = 0; i < side; i++) {
			const int64_t px = bx + i;
			if (px < 0 || px >= tw || !staged[j * side + i])
				continue;
			cp_blend_pixel(pixels + ((size_t)py * tw + px) * 4, cp_byte(op->r), cp_byte(op->g),
					cp_byte(op->b), staged[j * side + i]);
		}
	}
	free(staged);
}

/*
 * DrawCircle (v14 construction command 52, native 0x502570 -> 0x4cb910): the
 * alpha of a ring around (x + 0.5, y + 0.5), from 半徑 minus half 線の幅 (0
 * when that is negative, 0x4cb9b8) to 半徑 plus half of it, written as
 * command 102 writes its disc (0x4cbbd0 -> 0x5b0a10). The scan takes the
 * positions from the centre minus the outer radius in steps of 1 while they
 * do not exceed the centre plus it, the pixel being the position truncated
 * towards zero; the coverage (0x4e5f40) is the number of the cell's 8x8
 * subsamples whose distance from the centre lies between the two radii,
 * both included, scaled to n * 255 / 64. Counted in sixteenths, exactly
 * (the positions are multiples of 1/2, the subsamples of 1/8). A radius that
 * is not positive draws nothing and succeeds.
 */
static void build_draw_circle(uint8_t *pixels, int tw, int th, const struct parts_cp_pixel *op)
{
	const int r = op->radius, lw = op->line_width;
	if (r <= 0)
		return;
	if (r > PARTS_CP_CIRCLE_MAX_RADIUS || lw > PARTS_CP_CIRCLE_MAX_RADIUS) {
		WARNING("DrawCircle: unsupported radius %d, width %d", r, lw);
		return;
	}
	// With a width below -2r even the outer radius is negative: there is
	// no position to scan. (Checked after the limit above, so that -2 * r
	// cannot overflow.)
	if (lw < -2 * r)
		return;
	const int64_t inner = max(16 * r - 8 * lw, 0), outer = 16 * r + 8 * lw;
	const int64_t inner2 = inner * inner, outer2 = outer * outer;
	const int n = 2 * r + lw;	// the last position
	for (int j = 0; j <= n; j++) {
		const double fy = op->y + 0.5 - r - lw * 0.5 + j;
		if (fy <= -1.0 || fy >= th)
			continue;
		for (int i = 0; i <= n; i++) {
			const double fx = op->x + 0.5 - r - lw * 0.5 + i;
			if (fx <= -1.0 || fx >= tw)
				continue;
			int count = 0;
			for (int b = 0; b < 8; b++) {
				const int64_t v = -outer + 16 * j + 2 * b;
				for (int a = 0; a < 8; a++) {
					const int64_t u = -outer + 16 * i + 2 * a;
					const int64_t d2 = u * u + v * v;
					if (inner2 <= d2 && d2 <= outer2)
						count++;
				}
			}
			if (!count)
				continue;
			pixels[((size_t)(int)fy * tw + (int)fx) * 4 + 3] = cp_byte((int64_t)op->a * (count * 255 / 64) / 255);
		}
	}
}

/* The crossings of the line y with the polygon's edges, unsorted, as
 * 0x4d0ba0 collects them; `out` has room for one per edge. An edge has its
 * crossing when y lies between its ends, both included; at the end it leads
 * to, it has none when the next edge goes on in the same vertical direction
 * (0x4d0cf7). A run of horizontal edges on the line (0x4d0f10) gives one
 * point: its largest x when the vertex after it lies right of that, else its
 * smallest. Crossings are clamped to the scanned columns (0x4d0da3). */
static int cp_polygon_crossings(const double *xs, const double *ys, int n, double y,
		double col0, double col1, double *out)
{
	int nr = 0;
	for (int i = 0; i < n; i++) {
		const double x0 = xs[i], y0 = ys[i];
		const double x1 = xs[(i + 1) % n], y1 = ys[(i + 1) % n];
		double x;
		if (y0 == y1) {
			if (y0 != y)
				continue;
			double lo = x0 < x1 ? x0 : x1;
			double hi = x0 > x1 ? x0 : x1;
			bool found = false;
			for (; i < n; i++) {
				const double xk = xs[(i + 2) % n];
				if (ys[(i + 2) % n] != y0) {
					x = xk > hi ? hi : lo;
					found = true;
					break;
				}
				lo = xk < lo ? xk : lo;
				hi = xk > hi ? xk : hi;
			}
			if (!found)
				break;
		} else {
			if (y == y1) {
				const double before = y1 - y0, after = ys[(i + 2) % n] - y1;
				if ((before < 0.0 && after < 0.0) || (before > 0.0 && after > 0.0))
					continue;
			}
			double xl = x0, yl = y0, xh = x1, yh = y1;
			if (y0 > y1) {
				xl = x1; yl = y1;
				xh = x0; yh = y0;
			}
			if (yl > y || y > yh)
				continue;
			// Three statements: the original multiplies, divides and adds.
			x = (xh - xl) * (y - yl);
			x = x / (yh - yl);
			x = x + xl;
		}
		if (!(x <= col1))
			x = col1;
		if (!(x > col0))
			x = col0;
		out[nr++] = x;
	}
	return nr;
}

static int cp_compare_double(const void *a, const void *b)
{
	const double x = *(const double *)a, y = *(const double *)b;
	return (x > y) - (x < y);
}

/*
 * FillPolygonAlphaBlend (v14 construction command 97, native 0x506610 ->
 * 0x5af7d0): the polygon of ArrayPos, each vertex at its pixel's centre
 * (0x4e9400 adds 0.5; a vertex equal to the one before it is left out),
 * staged as (r, g, b, a * coverage / 255) on a transparent surface and
 * composed like the circle of command 106.
 *
 * The scan (0x4d0820) covers the rows of the vertices' bounding box, the
 * coordinates truncated (0x4ea230) and the box clipped to the surface
 * (0x4caf50), and the columns from one before it to one after it (0x4d1030).
 * A row's coverage per column is summed over the 8 lines y + k/8 (0x4d09c0):
 * the crossings are sorted, an equal neighbour is removed (0x4d0e20, which
 * shortens what it still looks at by two unless the pair is the first or the
 * last), and each pair (x1, x2) adds (0x4d0a80), in 255ths of a column,
 *   the part of x1's column right of x1, the whole of the columns after it
 *   up to and including x2's, and the fraction of x2 to the column after
 *   that, or (x2 - x1) to the one column both lie in.
 * That is the span [x1, x2 + 1) unless it stays within one column: a
 * vertex names the last pixel of an edge, not the position after it. A
 * column with a sum of 8 or more gets coverage sum / 8 (0x4d0901).
 *
 * 丸め (rounded corners, 0x4e98a0) and a rotation (0x4e9940) are not
 * implemented; PartsEngine_AddPartsConstructionProcess adds no operation for
 * them. Fewer than three vertices draw nothing (0x4d0870). With nothing
 * written (that, or the polygon lies outside the surface) the original
 * fails the command and the steps after it (0x506756 -> 0x50679b); here the
 * later operations still run.
 */
static void build_fill_polygon(uint8_t *pixels, int tw, int th, const struct parts_cp_pixel *op)
{
	if (op->nr_points < 3 || op->nr_points > PARTS_CP_POLYGON_MAX_POINTS)
		return;

	double *xs = xmalloc(op->nr_points * 3 * sizeof(double));
	double *ys = xs + op->nr_points, *cross = ys + op->nr_points;
	int n = 0;
	int min_x = INT_MAX, min_y = INT_MAX, max_x = INT_MIN, max_y = INT_MIN;
	for (int i = 0; i < op->nr_points; i++) {
		const double x = op->points[i * 2] + 0.5, y = op->points[i * 2 + 1] + 0.5;
		if (n && xs[n - 1] == x && ys[n - 1] == y)
			continue;
		xs[n] = x;
		ys[n] = y;
		n++;
		min_x = min(min_x, (int)x); max_x = max(max_x, (int)x);
		min_y = min(min_y, (int)y); max_y = max(max_y, (int)y);
	}
	const int x0 = max(min_x, 0), y0 = max(min_y, 0);
	const int x1 = max_x >= tw ? tw - 1 : max_x, y1 = max_y >= th ? th - 1 : max_y;
	const int col0 = max(x0 - 1, 0), col1 = min(x1 + 2, tw);
	if (n < 3 || x1 < x0 || y1 < y0 || col1 <= col0) {
		free(xs);
		return;
	}

	const uint8_t r = cp_byte(op->r), g = cp_byte(op->g), b = cp_byte(op->b);
	int *sums = xmalloc((col1 - col0) * sizeof(int));
	for (int row = y0; row <= y1; row++) {
		memset(sums, 0, (col1 - col0) * sizeof(int));
		for (int k = 0; k < 8; k++) {
			int nr = cp_polygon_crossings(xs, ys, n, row + k * 0.125, col0, col1, cross);
			qsort(cross, nr, sizeof(double), cp_compare_double);
			for (int i = 0, last = nr - 1, penult = nr - 2; i < last;) {
				if (cross[i] != cross[i + 1]) {
					i++;
					continue;
				}
				memmove(&cross[i], &cross[i + 1], (nr - i - 1) * sizeof(double));
				nr--;
				const int less = (i == 0 || i == penult) ? 1 : 2;
				last -= less;
				penult -= less;
			}
			for (int i = 1; i < nr; i += 2) {
				const double a = cross[i - 1], z = cross[i];
				const int ca = a, cz = z;
				if (ca == cz) {
					if (ca >= col0 && ca < col1)
						sums[ca - col0] += (int)((z - a) * 255.0);
					continue;
				}
				if (ca >= col0 && ca < col1) {
					// Two statements: the original multiplies, then subtracts.
					const double part = (a - ca) * 255.0;
					sums[ca - col0] += (int)(255.0 - part);
				}
				for (int col = ca + 1; col <= cz; col++) {
					if (col >= col0 && col < col1)
						sums[col - col0] += 255;
				}
				if (cz + 1 >= col0 && cz + 1 < col1)
					sums[cz + 1 - col0] += (int)((z - cz) * 255.0);
			}
		}
		for (int col = col0; col < col1; col++) {
			const int cov = sums[col - col0] / 8;
			if (cov <= 0)
				continue;
			const int a = cp_byte((int64_t)op->a * cov / 255);
			if (a)
				cp_blend_pixel(pixels + ((size_t)row * tw + col) * 4, r, g, b, a);
		}
	}
	free(sums);
	free(xs);
}

/*
 * TileCG (v14 construction command 129, native 0x50a220): the CG copied,
 * alpha included (0x5acd70), over the rectangle (全體: the whole surface)
 * from its corner in steps of the CG's size, the last column and row of
 * tiles cut at the rectangle's edge. `src` is the CG's cw x ch pixels.
 * The rectangle is clipped to the surface first: a pixel (X, Y) of what is
 * left gets the CG's ((X - x) mod cw, (Y - y) mod ch), so the work does not
 * grow with a width or height beyond the surface (a script's or a save's
 * may be anything). A size that is not positive copies nothing.
 */
void parts_cp_tile(uint8_t *pixels, int tw, int th, int x, int y, int w, int h,
		const uint8_t *src, int cw, int ch)
{
	if (!src || cw <= 0 || ch <= 0 || w <= 0 || h <= 0)
		return;
	const int64_t x0 = max((int64_t)x, 0), x1 = min((int64_t)x + w, tw);
	const int64_t y0 = max((int64_t)y, 0), y1 = min((int64_t)y + h, th);
	for (int64_t row = y0; row < y1; row++) {
		const uint8_t *s = src + (size_t)((row - y) % ch) * cw * 4;
		uint8_t *d = pixels + (size_t)row * tw * 4;
		for (int64_t col = x0; col < x1;) {
			const int64_t from = (col - x) % cw;
			const int64_t n = min(cw - from, x1 - col);
			memcpy(d + col * 4, s + from * 4, (size_t)n * 4);
			col += n;
		}
	}
}

static void build_tile_cg(uint8_t *pixels, int tw, int th, const struct parts_cp_pixel *op)
{
	struct cg *cg = asset_cg_load(op->cg_no);
	if (!cg)
		return;
	if (op->full)
		parts_cp_tile(pixels, tw, th, 0, 0, tw, th, cg->pixels, cg->metrics.w, cg->metrics.h);
	else
		parts_cp_tile(pixels, tw, th, op->x, op->y, op->w, op->h, cg->pixels, cg->metrics.w, cg->metrics.h);
	cg_free(cg);
}

bool parts_build_construction_process(struct parts *parts,
		struct parts_construction_process *cproc)
{
	// CreatePixelOnly makes a surface without alpha, on which the original
	// fails FillPieAMap (0x50940e); the texture is left as it is. A Texture
	// does not record its format, so this follows the operations that
	// create the surface.
	bool has_alpha = true;
	// The surface's pixels while the pixel commands run: read before the
	// first of a run of them, written back after the last.
	Texture *t = &cproc->common.texture;
	uint8_t *pixels = NULL;
	struct parts_cp_op *op;
	TAILQ_FOREACH(op, &cproc->ops, entry) {
		const bool on_pixels = op->type >= PARTS_CP_MUL_AMAP_GRADATION_ROWS
			&& op->type <= PARTS_CP_DRAW_CIRCLE_AMAP;
		if (pixels && !on_pixels) {
			gfx_update_texture_with_pixels(t, pixels);
			free(pixels);
			pixels = NULL;
		}
		if (on_pixels) {
			// Every one of them fails without a surface (e.g. 0x4ff904).
			if (!t->handle || t->w <= 0 || t->h <= 0)
				continue;
			if (!pixels)
				pixels = gfx_get_pixels(t);
		}
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
		// The commands that write only the alpha are left out on a
		// surface without one, as FillPieAMap is; what the original does
		// with them there was not read.
		case PARTS_CP_MUL_AMAP_GRADATION_ROWS:
		case PARTS_CP_MUL_AMAP_GRADATION_COLUMNS:
			if (has_alpha)
				build_mul_amap_gradation(pixels, t->w, t->h, &op->pixel,
						op->type == PARTS_CP_MUL_AMAP_GRADATION_COLUMNS);
			break;
		case PARTS_CP_BLUR_H:
		case PARTS_CP_BLUR_V:
			build_blur(pixels, t->w, t->h, &op->pixel, op->type == PARTS_CP_BLUR_V);
			break;
		case PARTS_CP_FILL_CIRCLE_AMAP:
			if (has_alpha)
				build_fill_circle(pixels, t->w, t->h, &op->pixel, false);
			break;
		case PARTS_CP_FILL_CIRCLE_BLEND:
			build_fill_circle(pixels, t->w, t->h, &op->pixel, true);
			break;
		case PARTS_CP_FILL_POLYGON_BLEND:
			build_fill_polygon(pixels, t->w, t->h, &op->pixel);
			break;
		case PARTS_CP_TILE_CG:
			build_tile_cg(pixels, t->w, t->h, &op->pixel);
			break;
		case PARTS_CP_DRAW_CIRCLE_AMAP:
			if (has_alpha)
				build_draw_circle(pixels, t->w, t->h, &op->pixel);
			break;
		}
	}
	if (pixels) {
		gfx_update_texture_with_pixels(t, pixels);
		free(pixels);
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
