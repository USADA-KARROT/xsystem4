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
#include "system4/hashtable.h"
#include "system4/string.h"

#include "asset_manager.h"
#include "audio.h"
#include "vm/page.h"
#include "xsystem4.h"
#include "sact.h"
#include "sprite.h"
#include "parts.h"
#include "parts_internal.h"
#include "reign.h"

struct parts_list parts_list = TAILQ_HEAD_INITIALIZER(parts_list);
static struct parts_list dirty_list = TAILQ_HEAD_INITIALIZER(dirty_list);
static struct hash_table *parts_table = NULL;
static Point root_pos = { 0, 0 };

struct parts_controller_stack ctrl_stack;
bool parts_multi_controller;

static void ctrl_stack_init(void);

#define PARTS_PARAMS_INITIALIZER (struct parts_params) { \
	.z = 1, \
	.pos = { 0, 0 }, \
	.show = true, \
	.alpha = 255, \
	.scale = { 1.0f, 1.0f }, \
	.rotation = { 0.0f, 0.0f, 0.0f }, \
	.add_color = { 0, 0, 0, 0 }, \
	.multiply_color = { 255, 255, 255, 255 } \
}

static void parts_init(struct parts *parts)
{
	parts->sp.z = 1;
	parts->sp.has_pixel = true;
	parts->sp.has_alpha = true;
	parts->sp.render = parts_sprite_render;
	parts->sp.to_json = parts_sprite_to_json;
	parts->local = PARTS_PARAMS_INITIALIZER;
	parts->global = PARTS_PARAMS_INITIALIZER;
	parts->delegate_index = -1;
	parts->want_save = true;
	parts->on_cursor_sound = -1;
	parts->on_click_sound = -1;
	parts->origin_mode = 1;
	parts->pending_parent = -1;
	parts->linked_to = -1;
	parts->linked_from = -1;
	TAILQ_INIT(&parts->children);
	TAILQ_INIT(&parts->motion);
}

static struct parts *parts_alloc(void)
{
	struct parts *parts = xcalloc(1, sizeof(struct parts));
	parts_init(parts);
	return parts;
}

void parts_component_dirty(struct parts *parts)
{
	if (parts->dirty)
		return;
	parts->dirty = true;
	TAILQ_INSERT_TAIL(&dirty_list, parts, dirty_list_entry);
}

static void dirty_list_remove(struct parts *parts)
{
	if (parts->dirty)
		TAILQ_REMOVE(&dirty_list, parts, dirty_list_entry);
}

/*
 * v14: a user component hands its origin mode to its first child when it is
 * updated (parts_do_layout; the original on every frame, 0x4e57d0). Called
 * when the component's mode, its first child or that child's mode changed.
 */
static void parts_user_component_dirty(struct parts *parts)
{
	if (ain->version >= 14 && parts->component_type == 17)
		parts_component_dirty(parts);
}

static int parts_get_sprite_z(struct parts *parts)
{
	if (!parts_multi_controller)
		return parts->global.z;
	// The system overlay controller sorts above any in-stack controller.
	return parts->controller_no;
}

static int parts_get_sprite_z2(struct parts *parts)
{
	if (!parts_multi_controller)
		return 0;
	return parts->global.z;
}

static void parts_list_insert(struct parts *parts)
{
	int z = parts_get_sprite_z(parts);
	int z2 = parts_get_sprite_z2(parts);
	parts->sp.z = z;
	parts->sp.z2 = z2;
	struct parts *p;
	PARTS_LIST_FOREACH(p) {
		int pz = parts_get_sprite_z(p);
		int pz2 = parts_get_sprite_z2(p);
		if (pz > z || (pz == z && pz2 > z2)) {
			TAILQ_INSERT_BEFORE(p, parts, parts_list_entry);
			goto done;
		}
	}
	TAILQ_INSERT_TAIL(&parts_list, parts, parts_list_entry);
done:
	parts_engine_dirty();
	scene_register_sprite(&parts->sp);
}

static void parts_list_remove(struct parts *parts)
{
	TAILQ_REMOVE(&parts_list, parts, parts_list_entry);
	scene_unregister_sprite(&parts->sp);
}

void parts_list_resort(struct parts *parts)
{
	// TODO: this could be optimized
	parts_list_remove(parts);
	parts_list_insert(parts);
}

struct parts *parts_try_get(int parts_no)
{
	struct ht_slot *slot = ht_put_int(parts_table, parts_no, NULL);
	if (slot->value)
		return slot->value;
	return NULL;
}

struct parts *parts_get(int parts_no)
{
	struct ht_slot *slot = ht_put_int(parts_table, parts_no, NULL);
	if (slot->value)
		return slot->value;

	struct parts *parts = parts_alloc();
	parts->no = parts_no;
	parts->controller_no = ctrl_stack.active;
	slot->value = parts;
	parts_list_insert(parts);
	return parts;
}

bool parts_exists(int parts_no)
{
	return !!ht_get_int(parts_table, parts_no, NULL);
}

static void parts_state_free(struct parts_state *state)
{
	parts_clear_hit_mask(&state->common);
	switch (state->type) {
	case PARTS_UNINITIALIZED:
	case PARTS_RECT_DETECTION:
	case PARTS_LAYOUT_BOX:
		break;
	case PARTS_CG:
		gfx_delete_texture(&state->common.texture);
		if (state->cg.name)
			free_string(state->cg.name);
		break;
	case PARTS_TEXT:
		parts_text_free(&state->text);
		break;
	case PARTS_ANIMATION:
		for (unsigned i = 0; i < state->anim.nr_frames; i++) {
			gfx_delete_texture(&state->anim.frames[i]);
		}
		free(state->anim.frames);
		break;
	case PARTS_NUMERAL:
		gfx_delete_texture(&state->common.texture);
		break;
	case PARTS_HGAUGE:
	case PARTS_VGAUGE:
		gfx_delete_texture(&state->common.texture);
		gfx_delete_texture(&state->gauge.cg);
		if (state->gauge.cg_name) free_string(state->gauge.cg_name);
		break;
	case PARTS_CONSTRUCTION_PROCESS:
		gfx_delete_texture(&state->common.texture);
		parts_clear_construction_process(&state->cproc);
		break;
	case PARTS_FLASH:
		parts_flash_free(&state->flash);
		break;
	case PARTS_FLAT:
		parts_flat_free(&state->flat);
		break;
	case PARTS_MOVIE:
		// The texture is owned by the SACT sprite.
		state->common.texture.handle = 0;
		if (state->movie.sprite_no >= 0)
			sact_SP_Delete(state->movie.sprite_no);
		break;
	case PARTS_3DLAYER:
		state->common.texture.handle = 0;
		if (state->layer3d.plugin >= 0)
			ReignEngine_ReleasePlugin(state->layer3d.plugin);
		if (state->layer3d.sprite_no >= 0)
			sact_SP_Delete(state->layer3d.sprite_no);
		break;
	}
	memset(state, 0, sizeof(struct parts_state));
}

static struct text_style default_text_style = {
	.face = FONT_GOTHIC,
	.size = 16.0f,
	.bold_width = 0.0f,
	.weight = FW_NORMAL,
	.edge_left = 0.0f,
	.edge_up = 0.0f,
	.edge_right = 0.0f,
	.edge_down = 0.0f,
	.color = { .r = 255, .g = 255, .b = 255, .a = 255 },
	.edge_color = { .r = 0, .g = 0, .b = 0, .a = 255 },
	.scale_x = 1.0f,
	.space_scale_x = 1.0f,
	.font_spacing = 0.0f,
	.font_size = NULL
};

void parts_state_reset(struct parts_state *state, enum parts_type type)
{
	parts_state_free(state);
	state->type = type;
	switch (type) {
	case PARTS_TEXT:
		state->text.ts = default_text_style;
		break;
	case PARTS_NUMERAL:
		state->num.length = 1;
		state->num.font_no = -1;
		state->num.zero_pad = true;
		state->num.font = default_text_style;
		break;
	case PARTS_CONSTRUCTION_PROCESS:
		TAILQ_INIT(&state->cproc.ops);
		break;
	case PARTS_HGAUGE:
	case PARTS_VGAUGE:
		state->gauge.cg_no = -1;
		state->gauge.rate = 0.0f;
		if (ain->version >= 14) {
			state->gauge.numerator = state->gauge.denominator = 100.0f;
			state->gauge.rate = 1.0f;
		}
		break;
	case PARTS_3DLAYER:
		state->layer3d.plugin = -1;
		state->layer3d.sprite_no = -1;
		break;
	case PARTS_UNINITIALIZED:
	case PARTS_CG:
	case PARTS_ANIMATION:
	case PARTS_FLASH:
	case PARTS_FLAT:
	case PARTS_RECT_DETECTION:
		break;
	case PARTS_MOVIE:
		state->movie.sprite_no = -1;
		break;
	case PARTS_LAYOUT_BOX:
		state->layout_box.layout_type = PARTS_LAYOUT_VERTICAL;
		state->layout_box.align = 1;
		break;
	}
}

struct parts_cg *parts_get_cg(struct parts *parts, int state)
{
	if (parts->states[state].type != PARTS_CG) {
		parts_state_reset(&parts->states[state], PARTS_CG);
	}
	return &parts->states[state].cg;
}

struct parts_text *parts_get_text(struct parts *parts, int state)
{
	if (parts->states[state].type != PARTS_TEXT) {
		parts_state_reset(&parts->states[state], PARTS_TEXT);
	}
	return &parts->states[state].text;
}

struct parts_animation *parts_get_animation(struct parts *parts, int state)
{
	if (parts->states[state].type != PARTS_ANIMATION) {
		parts_state_reset(&parts->states[state], PARTS_ANIMATION);
	}
	return &parts->states[state].anim;
}

struct parts_numeral *parts_get_numeral(struct parts *parts, int state)
{
	if (parts->states[state].type != PARTS_NUMERAL) {
		parts_state_reset(&parts->states[state], PARTS_NUMERAL);
	}
	return &parts->states[state].num;
}

struct parts_gauge *parts_get_hgauge(struct parts *parts, int state)
{
	if (parts->states[state].type != PARTS_HGAUGE) {
		parts_state_reset(&parts->states[state], PARTS_HGAUGE);
	}
	if (ain->version >= 14 && parts->component_type == 18)
		parts->component_state_type[state] = 22;
	return &parts->states[state].gauge;
}

struct parts_gauge *parts_get_vgauge(struct parts *parts, int state)
{
	if (parts->states[state].type != PARTS_VGAUGE) {
		parts_state_reset(&parts->states[state], PARTS_VGAUGE);
	}
	if (ain->version >= 14 && parts->component_type == 18)
		parts->component_state_type[state] = 23;
	return &parts->states[state].gauge;
}

struct parts_construction_process *parts_get_construction_process(struct parts *parts, int state)
{
	if (parts->states[state].type != PARTS_CONSTRUCTION_PROCESS) {
		parts_state_reset(&parts->states[state], PARTS_CONSTRUCTION_PROCESS);
	}
	return &parts->states[state].cproc;
}

struct parts_flash *parts_get_flash(struct parts *parts, int state)
{
	if (parts->states[state].type != PARTS_FLASH) {
		parts_state_reset(&parts->states[state], PARTS_FLASH);
	}
	return &parts->states[state].flash;
}

struct parts_flat *parts_get_flat(struct parts *parts, int state)
{
	if (parts->states[state].type != PARTS_FLAT) {
		parts_state_reset(&parts->states[state], PARTS_FLAT);
	}
	return &parts->states[state].flat;
}

struct parts_movie *parts_get_movie(struct parts *parts, int state)
{
	if (parts->states[state].type != PARTS_MOVIE) {
		parts_state_reset(&parts->states[state], PARTS_MOVIE);
	}
	return &parts->states[state].movie;
}

struct parts_layout_box *parts_get_layout_box(struct parts *parts)
{
	if (parts->states[0].type != PARTS_LAYOUT_BOX) {
		parts_state_reset(&parts->states[0], PARTS_LAYOUT_BOX);
	}
	return &parts->states[0].layout_box;
}

struct parts_3dlayer *parts_get_3dlayer(struct parts *parts, int state)
{
	if (parts->states[state].type != PARTS_3DLAYER) {
		parts_state_reset(&parts->states[state], PARTS_3DLAYER);
	}
	return &parts->states[state].layer3d;
}

static Point calculate_offset(int mode, int w, int h)
{
	switch (mode) {
	case 1:  return (Point) {    0, 0    }; // top-left
	case 2:  return (Point) { -w/2, 0    }; // top-center
	case 3:  return (Point) {   -w, 0    }; // top-right
	case 4:  return (Point) {    0, -h/2 }; // middle-left
	case 5:  return (Point) { -w/2, -h/2 }; // middle-center
	case 6:  return (Point) {   -w, -h/2 }; // middle-right
	case 7:  return (Point) {    0, -h   }; // bottom-left
	case 8:  return (Point) { -w/2, -h   }; // bottom-center
	case 9:  return (Point) {   -w, -h   }; // bottom-right
	default: return (Point) { mode, (3*h)/4 }; // why...
	}
}

/*
 * Should be called when:
 *   - position (parts->pos) changes
 *   - width or height changes
 *   - origin mode changes
 */
static void parts_common_recalculate_hitbox(struct parts *parts, struct parts_common *common)
{
	if (ain->version >= 14) {
		for (int i = 0; i < PARTS_NR_STATES; i++) {
			struct parts_state *s = &parts->states[i];
			if (&s->common != common || (s->type != PARTS_HGAUGE && s->type != PARTS_VGAUGE))
				continue;
			Rectangle area;
			parts_v14_gauge_surface(&s->gauge, &area);
			common->w = area.w; common->h = area.h;
			common->origin_offset = calculate_offset(parts->origin_mode, area.w, area.h);
			common->hitbox = (Rectangle) { parts->local.pos.x + common->origin_offset.x,
				parts->local.pos.y + common->origin_offset.y, area.w, area.h };
			return;
		}
	}

	if (common->surface_area.w || common->surface_area.h) {
		common->origin_offset = calculate_offset(parts->origin_mode,
				common->surface_area.w, common->surface_area.h);
		// SDL_IntersectRect leaves x and y alone when either rectangle is
		// empty (e.g. an emptied v14 text, 0x0). They are then the surface
		// area's, as they are when neither is, and as 0x59b760 writes them
		// whatever the content's size: the corner does not move when the
		// content is emptied, nor add up from one call to the next.
		Rectangle r = { 0, 0, common->w, common->h };
		Rectangle hit = { max(0, common->surface_area.x), max(0, common->surface_area.y), 0, 0 };
		SDL_IntersectRect(&r, &common->surface_area, &hit);
		common->origin_offset.x -= common->surface_area.x;
		common->origin_offset.y -= common->surface_area.y;
		hit.x += parts->local.pos.x + common->origin_offset.x;
		hit.y += parts->local.pos.y + common->origin_offset.y;
		common->hitbox = hit;
	} else {
		common->origin_offset = calculate_offset(parts->origin_mode, common->w, common->h);
		common->hitbox = (Rectangle) {
			.x = parts->local.pos.x + common->origin_offset.x,
			.y = parts->local.pos.y + common->origin_offset.y,
			.w = common->w,
			.h = common->h,
		};
	}
}

void parts_recalculate_hitbox(struct parts *parts)
{
	for (int i = 0; i < PARTS_NR_STATES; i++) {
		parts_common_recalculate_hitbox(parts, &parts->states[i].common);
	}
}

/*
 * A child's position relative to its parent's anchor (the parent's global
 * position). The parent's reverse flags mirror it around that anchor and the
 * parent's accumulated scale scales it, as in the original, where each parts'
 * flip and scale are part of its level of the transform (0x537b00 records,
 * applied by 0x4e6d80 before the child's own level). E.g. Tutorial's
 * MoveParent shrinks the scene parents to 0.85 with their whole scene.
 * Rotation still does not move the children (existing simplification).
 */
static Point parts_child_pos(const struct parts_params *parent, Point local)
{
	float x = parent->scale.x * local.x, y = parent->scale.y * local.y;
	return (Point) {
		parent->pos.x + (int)lroundf(parent->reverse_lr ? -x : x),
		parent->pos.y + (int)lroundf(parent->reverse_tb ? -y : y)
	};
}

/*
 * Where a parts is placed in its parent: its position and, for a child of a
 * v14 free layout box, the box's offset (layoutbox.c). The original adds the
 * two in both places that read the position, the per-frame update (0x53567a,
 * 0x535659) and the transform records (0x537b2f, 0x537b4f), so the offset
 * moves the parts' subtree, its hit box and its upper-left position with it.
 */
static Point parts_placed_pos(const struct parts *parts)
{
	return (Point) {
		parts->local.pos.x + parts->layout_offset.x,
		parts->local.pos.y + parts->layout_offset.y
	};
}

// parent == NULL: a top-level parts, relative to root_pos and never mirrored.
static void parts_update_global_pos(struct parts *parts, const struct parts_params *parent)
{
	Point pos = parts_placed_pos(parts);
	if (parent) {
		parts->global.pos = parts_child_pos(parent, pos);
	} else {
		parts->global.pos = (Point) { root_pos.x + pos.x, root_pos.y + pos.y };
	}

	struct parts *child;
	PARTS_FOREACH_CHILD(child, parts) {
		parts_update_global_pos(child, &parts->global);
	}
}

void parts_set_pos(struct parts *parts, Point pos)
{
	bool moved = parts->local.pos.x != pos.x || parts->local.pos.y != pos.y;
	parts->local.pos.x = pos.x;
	parts->local.pos.y = pos.y;
	parts_recalculate_hitbox(parts);
	parts_update_global_pos(parts, parts->parent ? &parts->parent->global : NULL);
	parts_dirty(parts);
	// v14: a vertical/horizontal box puts its child back, as the original
	// does on every frame the box is shown.
	if (moved)
		parts_layout_size_changed(parts);
}

void parts_set_global_pos(Point pos)
{
	root_pos = pos;
	struct parts *parts;
	PARTS_LIST_FOREACH(parts) {
		parts_update_global_pos(parts, NULL);
	}
	parts_engine_dirty();
}

/*
 * The hit box on screen. common->hitbox is the box at the parts' own
 * position (local pos + origin offset). The box is placed at the parts'
 * anchor and mirrored around it by the reverse flags of the parts and of all
 * its ancestors (like the original, whose hit test 0x57a6b0 transforms the
 * box corners with the accumulated matrix). Scale and rotation are ignored
 * here, as before.
 */
Rectangle parts_screen_hitbox(struct parts *parts, struct parts_common *common)
{
	Rectangle box = common->hitbox;
	Point anchor = parts_placed_pos(parts);
	if (parts->parent)
		anchor = parts_child_pos(&parts->parent->global, anchor);
	int rx = box.x - parts->local.pos.x, ry = box.y - parts->local.pos.y;
	if (parts->global.reverse_lr)
		rx = -rx - box.w;
	if (parts->global.reverse_tb)
		ry = -ry - box.h;
	box.x = anchor.x + rx;
	box.y = anchor.y + ry;
	return box;
}

/*
 * The screen position of the box's top-left corner (the texture's first
 * pixel), as Parts_GetPartsUpperLeftPos returns it: the original transforms
 * that corner with the parts' matrix (0x534bb0 -> 0x57b6a0), so under a flip
 * it is mirrored around the anchor like every other point of the box.
 */
Point parts_screen_upper_left(struct parts *parts, struct parts_common *common)
{
	Point anchor = parts_placed_pos(parts);
	if (parts->parent)
		anchor = parts_child_pos(&parts->parent->global, anchor);
	int rx = common->hitbox.x - parts->local.pos.x, ry = common->hitbox.y - parts->local.pos.y;
	return (Point) {
		anchor.x + (parts->global.reverse_lr ? -rx : rx),
		anchor.y + (parts->global.reverse_tb ? -ry : ry)
	};
}

static void parts_update_global_z(struct parts *parts, int parent_z)
{
	parts->global.z = parent_z + parts->local.z;
	parts_list_resort(parts);

	struct parts *child;
	PARTS_FOREACH_CHILD(child, parts) {
		parts_update_global_z(child, parts->global.z);
	}
}

void parts_set_z(struct parts *parts, int z)
{
	if (parts->local.z == z)
		return;

	parts->local.z = z;
	parts_update_global_z(parts, parts->parent ? parts->parent->global.z : 0);
	parts_dirty(parts);
}

static void parts_update_global_show(struct parts *parts, bool parent_show)
{
	parts->global.show = parent_show && parts->local.show && !parts->edit_hidden;

	struct parts *child;
	PARTS_FOREACH_CHILD(child, parts) {
		parts_update_global_show(child, parts->global.show);
	}
}

/* v14 pactex 編輯上表示 = 0: the parts and its subtree are never shown. */
void parts_set_edit_hidden(struct parts *parts, bool hidden)
{
	if (parts->edit_hidden == hidden)
		return;
	parts->edit_hidden = hidden;
	parts_update_global_show(parts, parts->parent ? parts->parent->global.show : true);
	parts_dirty(parts);
	// A layout box skips children hidden this way.
	parts_layout_size_changed(parts);
}

void parts_set_show(struct parts *parts, bool show)
{
	if (parts->local.show == show)
		return;

	parts->local.show = show;
	parts_update_global_show(parts, parts->parent ? parts->parent->global.show : true);
	parts_dirty(parts);
	parts_layout_size_changed(parts);
}

static void parts_update_global_alpha(struct parts *parts, int parent_alpha)
{
	parts->global.alpha = parent_alpha * (parts->local.alpha / 255.0f);

	struct parts *child;
	PARTS_FOREACH_CHILD(child, parts) {
		parts_update_global_alpha(child, parts->global.alpha);
	}
}

void parts_set_alpha(struct parts *parts, int alpha)
{
	parts->local.alpha = max(0, min(255, alpha));
	parts_update_global_alpha(parts, parts->parent ? parts->parent->global.alpha : 255);
	parts_dirty(parts);
}

static void parts_update_global_reverse(struct parts *parts, bool parent_lr, bool parent_tb)
{
	parts->global.reverse_lr = parent_lr != parts->local.reverse_lr;
	parts->global.reverse_tb = parent_tb != parts->local.reverse_tb;

	struct parts *child;
	PARTS_FOREACH_CHILD(child, parts) {
		parts_update_global_reverse(child, parts->global.reverse_lr, parts->global.reverse_tb);
	}
}

/*
 * SetComponentReverseLR/TB. In the original each level of the transform
 * starts with S(reverse ? -1 : 1) (0x4e6d80, which also XORs the flags into
 * the accumulated state), so the flags of a parts and its ancestors combine
 * by XOR, the parts' box is mirrored around its anchor, and so are the
 * positions of its children.
 */
void parts_set_reverse(struct parts *parts, bool lr, bool tb)
{
	if (parts->local.reverse_lr == lr && parts->local.reverse_tb == tb)
		return;
	parts->local.reverse_lr = lr;
	parts->local.reverse_tb = tb;
	const struct parts_params *parent = parts->parent ? &parts->parent->global : NULL;
	parts_update_global_reverse(parts, parent && parent->reverse_lr, parent && parent->reverse_tb);
	struct parts *child;
	PARTS_FOREACH_CHILD(child, parts) {
		parts_update_global_pos(child, &parts->global);
	}
	parts_dirty(parts);
}

static void parts_update_global_add_color(struct parts *parts, SDL_Color parent_color)
{
	parts->global.add_color = (SDL_Color) {
		parent_color.r * (parts->local.add_color.r / 255.0f),
		parent_color.g * (parts->local.add_color.g / 255.0f),
		parent_color.b * (parts->local.add_color.b / 255.0f),
		0
	};

	struct parts *child;
	PARTS_FOREACH_CHILD(child, parts) {
		parts_update_global_add_color(child, parts->global.add_color);
	}
}

void parts_set_add_color(struct parts *parts, SDL_Color color)
{
	parts->local.add_color = color;
	parts_update_global_add_color(parts, parts->parent ? parts->parent->global.add_color
			: (SDL_Color){0,0,0,0});
	parts_dirty(parts);
}

static void parts_update_global_multiply_color(struct parts *parts, SDL_Color parent_color)
{
	parts->global.multiply_color = (SDL_Color) {
		parent_color.r * (parts->local.multiply_color.r / 255.0f),
		parent_color.g * (parts->local.multiply_color.g / 255.0f),
		parent_color.b * (parts->local.multiply_color.b / 255.0f),
		255
	};

	struct parts *child;
	PARTS_FOREACH_CHILD(child, parts) {
		parts_update_global_multiply_color(child, parts->global.multiply_color);
	}
}

void parts_set_multiply_color(struct parts *parts, SDL_Color color)
{
	parts->local.multiply_color = color;
	parts_update_global_multiply_color(parts, parts->parent ? parts->parent->global.multiply_color
			: (SDL_Color){255,255,255,255});
	parts_dirty(parts);
}

void parts_set_origin_mode(struct parts *parts, int origin_mode)
{
	bool changed = parts->origin_mode != origin_mode;
	parts->origin_mode = origin_mode;
	parts_recalculate_hitbox(parts);
	parts_dirty(parts);
	// v14: a vertical/horizontal box starts from its own origin mode and
	// sets its children's back to its alignment; a free box moves its
	// children by its own; a user component writes its own to its first
	// child.
	if (changed && ain->version >= 14) {
		if (parts->states[0].type == PARTS_LAYOUT_BOX)
			parts_component_dirty(parts);
		parts_layout_size_changed(parts);
		parts_user_component_dirty(parts);
		if (parts->parent && TAILQ_FIRST(&parts->parent->children) == parts)
			parts_user_component_dirty(parts->parent);
	}
}

static void parts_update_global_scale_x(struct parts *parts, float parent_scale_x)
{
	parts->global.scale.x = parent_scale_x * parts->local.scale.x;

	struct parts *child;
	PARTS_FOREACH_CHILD(child, parts) {
		parts_update_global_scale_x(child, parts->global.scale.x);
	}
}

void parts_set_scale_x(struct parts *parts, float mag)
{
	parts->local.scale.x = mag;
	parts_recalculate_hitbox(parts);
	parts_update_global_scale_x(parts, parts->parent ? parts->parent->global.scale.x : 1.0f);
	// The children's offsets scale with it (parts_child_pos).
	struct parts *child;
	PARTS_FOREACH_CHILD(child, parts) {
		parts_update_global_pos(child, &parts->global);
	}
	parts_dirty(parts);
}

static void parts_update_global_scale_y(struct parts *parts, float parent_scale_y)
{
	parts->global.scale.y = parent_scale_y * parts->local.scale.y;

	struct parts *child;
	PARTS_FOREACH_CHILD(child, parts) {
		parts_update_global_scale_y(child, parts->global.scale.y);
	}
}

void parts_set_scale_y(struct parts *parts, float mag)
{
	parts->local.scale.y = mag;
	parts_recalculate_hitbox(parts);
	parts_update_global_scale_y(parts, parts->parent ? parts->parent->global.scale.y : 1.0f);
	// The children's offsets scale with it (parts_child_pos).
	struct parts *child;
	PARTS_FOREACH_CHILD(child, parts) {
		parts_update_global_pos(child, &parts->global);
	}
	parts_dirty(parts);
}

static void parts_update_global_rotate_x(struct parts *parts, float parent_rot_x)
{
	parts->global.rotation.x = parent_rot_x + parts->local.rotation.x;

	struct parts *child;
	PARTS_FOREACH_CHILD(child, parts) {
		parts_update_global_rotate_x(child, parts->global.rotation.x);
	}
}

void parts_set_rotation_x(struct parts *parts, float rot)
{
	parts->local.rotation.x = rot;
	parts_update_global_rotate_x(parts, parts->parent ? parts->parent->global.rotation.x : 0.0f);
	parts_dirty(parts);
}

static void parts_update_global_rotate_y(struct parts *parts, float parent_rot_y)
{
	parts->global.rotation.y = parent_rot_y + parts->local.rotation.y;

	struct parts *child;
	PARTS_FOREACH_CHILD(child, parts) {
		parts_update_global_rotate_y(child, parts->global.rotation.y);
	}
}

void parts_set_rotation_y(struct parts *parts, float rot)
{
	parts->local.rotation.y = rot;
	parts_update_global_rotate_y(parts, parts->parent ? parts->parent->global.rotation.y : 0.0f);
	parts_dirty(parts);
}

static void parts_update_global_rotate_z(struct parts *parts, float parent_rot_z)
{
	parts->global.rotation.z = parent_rot_z + parts->local.rotation.z;

	struct parts *child;
	PARTS_FOREACH_CHILD(child, parts) {
		parts_update_global_rotate_z(child, parts->global.rotation.z);
	}
}

void parts_set_rotation_z(struct parts *parts, float rot)
{
	parts->local.rotation.z = rot;
	parts_update_global_rotate_z(parts, parts->parent ? parts->parent->global.rotation.z : 0.0f);
	parts_dirty(parts);
}

void parts_clear_hit_mask(struct parts_common *common)
{
	free(common->hit_mask);
	common->hit_mask = NULL;
}

void parts_set_dims(struct parts *parts, struct parts_common *common, int w, int h)
{
	bool changed = common->w != w || common->h != h;
	parts_clear_hit_mask(common);
	common->w = w;
	common->h = h;
	parts_common_recalculate_hitbox(parts, common);
	// A layout box sizes its children by their normal state.
	if (changed && common == &parts->states[0].common)
		parts_layout_size_changed(parts);
}

bool _parts_cg_set(struct parts *parts, struct parts_cg *parts_cg, struct cg *cg, int cg_no,
		struct string *name)
{
	if (!cg)
		return false;
	gfx_delete_texture(&parts_cg->common.texture);
	gfx_init_texture_with_cg(&parts_cg->common.texture, cg);
	parts_set_dims(parts, &parts_cg->common, cg->metrics.w, cg->metrics.h);
	parts_cg->no = cg_no;
	if (parts_cg->name)
		free_string(parts_cg->name);
	parts_cg->name = name;
	parts_dirty(parts);
	cg_free(cg);
	return true;
}

bool parts_cg_set_by_index(struct parts *parts, struct parts_cg *cg, int cg_no)
{
	assert(cg_no);
	return _parts_cg_set(parts, cg, asset_cg_load(cg_no), cg_no, NULL);
}

bool parts_cg_set(struct parts *parts, struct parts_cg *parts_cg, struct string *cg_name)
{
	assert(cg_name && *(cg_name->text) != '\0');
	int no;
	struct cg *cg;
	if (!memcmp(cg_name->text, "<save>", 6)) {
		char *path = savedir_path(cg_name->text + 6);
		cg = cg_load_file(path);
		no = 0;
		free(path);
	} else {
		cg = asset_cg_load_by_name(cg_name->text, &no);
	}
	return _parts_cg_set(parts, parts_cg, cg, no, string_dup(cg_name));
}

struct parts_numeral_font *parts_numeral_fonts = NULL;
int parts_nr_numeral_fonts = 0;

void parts_numeral_font_init(struct parts_numeral_font *font)
{
	if (font->type == PARTS_NUMERAL_FONT_SEPARATE) {
		for (int i = 0; i < 12; i++) {
			struct cg *cg = asset_cg_load(font->cg_no + i);
			if (!cg) {
				font->cg[i].handle = 0;
				continue;
			}
			gfx_init_texture_with_cg(&font->cg[i], cg);
			cg_free(cg);
		}
	} else if (font->type == PARTS_NUMERAL_FONT_SEPARATE2) {
		for (int i = 0; i < 12; i++) {
			if (font->width[i] < 0)
				continue;
			struct cg *cg = asset_cg_load(font->width[i]);
			if (!cg) {
				font->cg[i].handle = 0;
				continue;
			}
			gfx_init_texture_with_cg(&font->cg[i], cg);
			cg_free(cg);
		}
	} else if (font->type == PARTS_NUMERAL_FONT_COMBINED) {
		int x = 0;
		Texture t = {0};
		struct cg *cg = asset_cg_load(font->cg_no);
		gfx_init_texture_with_cg(&t, cg);
		for (int i = 0; i < 12; i++) {
			if (font->width[i] <= 0)
				continue;
			gfx_init_texture_blank(&font->cg[i], font->width[i], t.h);
			gfx_copy_with_alpha_map(&font->cg[i], 0, 0, &t, x, 0, font->width[i], t.h);
			x += font->width[i];
		}
		gfx_delete_texture(&t);
		free(cg);
	}
}

static int parts_load_numeral_font_separate(int cg_no)
{
	// find existing font
	for (int i = 0; i < parts_nr_numeral_fonts; i++) {
		if (parts_numeral_fonts[i].type == PARTS_NUMERAL_FONT_SEPARATE
				&& parts_numeral_fonts[i].cg_no == cg_no)
			return i;
	}

	// load new font
	int font_no = parts_nr_numeral_fonts++;
	parts_numeral_fonts = xrealloc_array(parts_numeral_fonts, font_no, font_no + 1,
			sizeof(struct parts_numeral_font));
	struct parts_numeral_font *font = &parts_numeral_fonts[font_no];
	font->cg_no = cg_no;
	font->type = PARTS_NUMERAL_FONT_SEPARATE;
	parts_numeral_font_init(font);
	return font_no;
}

static int parts_load_numeral_font_separate_string(struct string *cg_name)
{
	// convert name to CG indices
	int indices[12];
	for (int i = 0; i < 12; i++) {
		struct string *name = string_format(cg_name, (union vm_value){.i = i}, STRFMT_INT);
		if (!asset_exists_by_name(ASSET_CG, name->text, &indices[i])) {
			WARNING("numeral cg doesn't exist: %s", display_sjis0(name->text));
			indices[i] = -1;
		}
		free_string(name);
	}

	// find existing font
	for (int i = 0; i < parts_nr_numeral_fonts; i++) {
		struct parts_numeral_font *font = &parts_numeral_fonts[i];
		if (font->type != PARTS_NUMERAL_FONT_SEPARATE2)
			continue;
		bool not_match = false;
		for (int d = 0; d < 12; d++) {
			if (font->width[d] != indices[d]) {
				not_match = true;
				break;
			}
		}
		if (not_match)
			continue;
		return i;
	}

	// load new font
	int font_no = parts_nr_numeral_fonts++;
	parts_numeral_fonts = xrealloc_array(parts_numeral_fonts, font_no, font_no + 1,
			sizeof(struct parts_numeral_font));
	struct parts_numeral_font *font = &parts_numeral_fonts[font_no];
	font->cg_no = indices[0];
	font->type = PARTS_NUMERAL_FONT_SEPARATE2;
	memcpy(font->width, indices, sizeof(int) * 12);
	parts_numeral_font_init(font);
	return font_no;
}

static int parts_load_numeral_font_combined(struct cg *cg, int cg_no, int w[12])
{
	// find existing font
	for (int i = 0; i < parts_nr_numeral_fonts; i++) {
		struct parts_numeral_font *font = &parts_numeral_fonts[i];
		if (font->type != PARTS_NUMERAL_FONT_COMBINED || font->cg_no != cg_no)
			continue;
		for (int i = 0; i < 12; i++) {
			if (w[i] != font->width[i])
				continue;
		}
		return i;
	}

	// load new font
	int font_no = parts_nr_numeral_fonts++;
	parts_numeral_fonts = xrealloc_array(parts_numeral_fonts, font_no, font_no + 1,
			sizeof(struct parts_numeral_font));
	struct parts_numeral_font *font = &parts_numeral_fonts[font_no];
	font->cg_no = cg_no;
	font->type = PARTS_NUMERAL_FONT_COMBINED;
	memcpy(font->width, w, sizeof(int) * 12);
	parts_numeral_font_init(font);
	return font_no;
}

/*
 * v14 表示タイプ 2 (e.g. SceneHome's NumDay, MoneyView's Money): the number
 * is drawn as text with the numeral's font, digit spacing, comma grouping
 * and zero padding, with the same text renderer as text parts. The original
 * makes a glyph per character and sizes the numeral by them (0x5b4df0): the
 * width and, on the CN glyph grid, the height below follow it; elsewhere the
 * height is the font size and both edges.
 */
static bool parts_numeral_update_font(struct parts *parts, struct parts_numeral *num)
{
	int_least64_t n = num->num;
	bool negative = n < 0;
	if (negative)
		n = -n;
	char digits[32];
	int nd = 0;
	do {
		digits[nd++] = '0' + n % 10;
		n /= 10;
	} while (n && nd < 20);
	while (num->zero_pad && nd < num->length && nd < 20)
		digits[nd++] = '0';

	// digits[] is least significant first; build the string forwards.
	char buf[128];
	int len = 0;
	const bool full = num->full_pitch;
	#define PUT_CHAR(c) do { \
		if (full) { buf[len++] = '\xa3'; buf[len++] = (c) == '-' ? '\xad' : (c) == ',' ? '\xac' : '\xb0' + ((c) - '0'); } \
		else buf[len++] = (c); \
	} while (0)
	if (negative)
		PUT_CHAR('-');
	for (int i = nd - 1; i >= 0; i--) {
		PUT_CHAR(digits[i]);
		if (num->show_comma && i > 0 && i % 3 == 0)
			PUT_CHAR(',');
	}
	#undef PUT_CHAR
	buf[len] = '\0';

	struct text_style ts = num->font;
	ts.font_spacing = num->space;
	// The width is the digit cells and the spacing between them (0x5b4df0:
	// 0x5b50d3 adds a cell, 0x5b5164..0x5b5182 (cells - 1) * 字間隔), which
	// is where the last glyph drawn below ends; gfx_size_text is the cells.
	// Without ゼロパディング the original still counts 桁數 cells and only
	// hides the leading ones (0x5b5230); every numeral in the game pads.
	int nr_chars = full ? len / 2 : len;
	int w = (int)ceilf(gfx_size_text(&ts, buf)) + (nr_chars - 1) * num->space;
	int h = (int)ceilf(ts.size + ts.edge_up + ts.edge_down);
	int y = 0;
	if (ain->version >= 14 && gfx_text_cn_gdi()) {
		// The height is the tallest glyph texture's (0x5b5133), size + 2e
		// with the glyph e below its top (0x69c290, 0x69c3d0); the textures
		// are placed from the numeral's top (0x5b4380). No rows are added
		// below as for a text's glyphs (text.c): digits, the comma and the
		// minus sign do not reach below the cell.
		y = gfx_text_cn_style_edge(&ts);
		h = (int)lroundf(ts.size) + 2 * y;
	}
	if (w <= 0 || h <= 0)
		return true;
	gfx_delete_texture(&num->common.texture);
	gfx_init_texture_rgba(&num->common.texture, w, h, (SDL_Color){0, 0, 0, 0});
	// As a construction CopyText: transparent edge colour, then copy glyphs.
	gfx_fill_with_alpha(&num->common.texture, 0, 0, w, h,
			ts.edge_color.r, ts.edge_color.g, ts.edge_color.b, 0);
	gfx_render_text(&num->common.texture, 0, y, buf, &ts, false);
	parts_set_dims(parts, &num->common, w, h);
	parts_dirty(parts);
	return true;
}

static bool parts_numeral_update(struct parts *parts, struct parts_numeral *num)
{
	if (num->have_num && num->show_type == 2)
		return parts_numeral_update_font(parts, num);
	// XXX: don't generate texture if number hasn't been set yet
	if (!num->have_num || num->font_no < 0)
		return true;
	int_least64_t n = num->num;
	bool negative = n < 0;
	if (negative)
		n *= -1;

	// extract digits
	int digits;
	uint8_t d[32];
	for (digits = 0; n; digits++) {
		d[digits] = n % 10;
		n /= 10;
	}

	// encode number as texture indices
	int nr_chars = 0;
	uint8_t chars[32];
	if (negative) {
		chars[nr_chars++] = 10;
	}
	for (int i = 0; i < digits; i++) {
		if (num->show_comma && i > 0 && i % 3 == 0) {
			chars[nr_chars++] = 11;
		}
		chars[nr_chars++] = d[i];
	}

	for (int i = digits; i < num->length; i++) {
		chars[nr_chars++] = 0;
	}

	struct parts_numeral_font *font = &parts_numeral_fonts[num->font_no];

	// determine output dimensions
	int w = 0, h = 0;
	for (int i = nr_chars-1; i >= 0; i--) {
		if (!font->cg[chars[i]].handle)
			continue;
		w += font->cg[chars[i]].w;
		h = max(h, font->cg[chars[i]].h);
	}
	w += (nr_chars-1) * num->space;

	// copy chars to texture
	gfx_delete_texture(&num->common.texture);
	gfx_init_texture_rgba(&num->common.texture, w, h, (SDL_Color){0, 0, 0, 255});

	int x = 0;
	for (int i = nr_chars-1; i>= 0; i--) {
		Texture *ch = &font->cg[chars[i]];
		if (!ch->handle)
			continue;
		gfx_copy_with_alpha_map(&num->common.texture, x, 0, ch, 0, 0, ch->w, ch->h);
		x += ch->w + num->space;
	}

	parts_set_dims(parts, &num->common, w, h);

	parts_dirty(parts);
	return true;

}

bool parts_numeral_set_number(struct parts *parts, struct parts_numeral *num, int n)
{
	num->have_num = true;
	num->num = n;
	return parts_numeral_update(parts, num);
}

/*
 * A hover/press state with nothing to show keeps the state below it. Besides
 * an uninitialized state, that is a CG state without a CG: the v14 pactex
 * gives every low-level ＣＧ部件 three states, and none of the game's 1,759
 * on-cursor states names a CG, while 699 normal states do and others get
 * theirs at run time (SceneTutorial's Image0). The original does not blank
 * those parts under the cursor, so the empty state shows the one below it
 * (inferred; the original's state switch itself was not traced).
 */
static bool parts_state_is_empty(struct parts_state *state)
{
	if (state->type == PARTS_UNINITIALIZED)
		return true;
	return ain->version >= 14 && state->type == PARTS_CG
		&& !state->common.texture.handle && !state->cg.name;
}

void parts_set_state(struct parts *parts, enum parts_state_type state)
{
	if (parts->lock_input_state)
		return;
	while (state > PARTS_STATE_DEFAULT && parts_state_is_empty(&parts->states[state]))
		state--;
	if (parts->state != state) {
		parts->state = state;
		parts_dirty(parts);
	}
}

void parts_set_surface_area(struct parts *parts, struct parts_common *common, int x, int y, int w, int h)
{
	if (x < 0 || y < 0)
		return;
	common->surface_area = (Rectangle) { x, y, w, h };
	parts_common_recalculate_hitbox(parts, common);
	// A layout box limits a text's size to its surface area.
	if (common == &parts->states[0].common)
		parts_layout_size_changed(parts);
}

static bool parts_animation_update(struct parts_animation *anim, int passed_time)
{
	if (passed_time <= 0 || !anim->nr_frames)
		return false;

	const unsigned elapsed = anim->elapsed + passed_time;
	const unsigned frame_diff = elapsed / anim->frame_time;
	const unsigned remainder = elapsed % anim->frame_time;

	if (frame_diff > 0) {
		anim->elapsed = remainder;
		anim->current_frame = (anim->current_frame + frame_diff) % anim->nr_frames;
		anim->common.texture = anim->frames[anim->current_frame];
		return true;
	} else {
		anim->elapsed = elapsed;
		return false;
	}
}

static void parts_update_loop(struct parts *parts, int passed_time)
{
	bool dirty = false;
	switch (parts->states[parts->state].type) {
	case PARTS_ANIMATION:
		dirty = parts_animation_update(&parts->states[parts->state].anim, passed_time);
		break;
	case PARTS_FLASH:
		dirty = parts_flash_update(&parts->states[parts->state].flash, passed_time);
		break;
	case PARTS_FLAT:
		dirty = parts_flat_update(&parts->states[parts->state].flat, passed_time);
		break;
	default:
		break;
	}
	if (dirty)
		parts_dirty(parts);
}

static void parts_update_animation(int passed_time)
{
	struct parts *parts;
	PARTS_LIST_FOREACH(parts) {
		parts_update_loop(parts, passed_time);
	}
}

void parts_release(int parts_no)
{
	struct ht_slot *slot = ht_put_int(parts_table, parts_no, NULL);
	if (!slot->value)
		return;

	struct parts *parts = slot->value;
	parts_input_reset_drag(parts);
	parts_clear_motion(parts);
	parts_message_window_free(parts->message);
	for (int i = 0; i < PARTS_NR_STATES; i++) {
		parts_state_free(&parts->states[i]);
	}

	// break parent/child relationships
	while (!TAILQ_EMPTY(&parts->children)) {
		struct parts *child = TAILQ_FIRST(&parts->children);
		TAILQ_REMOVE(&parts->children, child, child_list_entry);
		child->parent = NULL;
	}
	if (parts->parent) {
		// v14: a layout box above closes the gap.
		parts_layout_size_changed(parts);
		TAILQ_REMOVE(&parts->parent->children, parts, child_list_entry);
		parts_user_component_dirty(parts->parent);
		parts->parent = NULL;
	}

	parts_list_remove(parts);
	dirty_list_remove(parts);
	free(parts->user_component_name);
	free(parts->button_cg_name);
	for (int i = 0; i < parts->nr_uc_data; i++)
		free(parts->uc_data[i]);
	free(parts->uc_data);
	free(parts);
	slot->value = NULL;
	parts_engine_dirty();
}

void parts_release_all(void)
{
	while (!TAILQ_EMPTY(&parts_list)) {
		struct parts *parts = TAILQ_FIRST(&parts_list);
		parts_release(parts->no);
	}

	for (int i = 0; i < parts_nr_numeral_fonts; i++) {
		struct parts_numeral_font *font = &parts_numeral_fonts[i];
		for (int i = 0; i < 12; i++) {
			if (font->cg[i].handle)
				gfx_delete_texture(&font->cg[i]);
		}
	}

	free(parts_numeral_fonts);
	parts_numeral_fonts = NULL;
	parts_nr_numeral_fonts = 0;
}

static bool parts_engine_initialized = false;

void PE_enable_multi_controller(void)
{
	if (parts_multi_controller)
		return;
	assert(!parts_engine_initialized);
	parts_multi_controller = true;
}

bool PE_Init(void)
{
	if (parts_engine_initialized)
		return true;
	// XXX: Oyako Rankan doesn't call ChipmunkSpriteEngine.Init
	sact_init(16, CHIPMUNK_SPRITE_ENGINE);
	parts_table = ht_create(1024);
	parts_render_init();
	parts_debug_init();
	ctrl_stack_init();
	parts_reset_input();
	parts_engine_initialized = true;
	return true;
}

void PE_Reset(void)
{
	parts_reset_input();
	PE_ReleaseAllParts();
	PE_ReleaseMessage();
	ctrl_stack_init();
	sact_ModuleFini();
}

static bool parts_has_dirty_parent(struct parts *parts)
{
	for (struct parts *parent = parts->parent; parent; parent = parent->parent) {
		if (parent->dirty)
			return true;
	}
	return false;
}

// pos: where the child is placed in the parent (parts_placed_pos).
static void parts_combine_params(struct parts_params *parent, struct parts_params *child,
		Point pos, struct parts_params *out)
{
	out->z = parent->z + child->z;
	out->pos = parts_child_pos(parent, pos);
	out->show = parent->show && child->show;
	out->alpha = parent->alpha * (child->alpha / 255.0f);
	out->scale.x = parent->scale.x * child->scale.x;
	out->scale.y = parent->scale.y * child->scale.y;
	out->rotation.x = parent->rotation.x + child->rotation.x;
	out->rotation.y = parent->rotation.y + child->rotation.y;
	out->rotation.z = parent->rotation.z + child->rotation.z;
	out->add_color.r = parent->add_color.r * (child->add_color.r / 255.0f);
	out->add_color.g = parent->add_color.g * (child->add_color.g / 255.0f);
	out->add_color.b = parent->add_color.b * (child->add_color.b / 255.0f);
	out->multiply_color.r = parent->multiply_color.r * (child->multiply_color.r / 255.0f);
	out->multiply_color.g = parent->multiply_color.g * (child->multiply_color.g / 255.0f);
	out->multiply_color.b = parent->multiply_color.b * (child->multiply_color.b / 255.0f);
	out->reverse_lr = parent->reverse_lr != child->reverse_lr;
	out->reverse_tb = parent->reverse_tb != child->reverse_tb;
}

static void parts_update_component(struct parts *parts)
{
	if (parts->parent) {
		parts_combine_params(&parts->parent->global, &parts->local,
				parts_placed_pos(parts), &parts->global);
		if (parts->edit_hidden)
			parts->global.show = false;
	}
	if (parts_get_sprite_z(parts) != parts->sp.z
			|| parts_get_sprite_z2(parts) != parts->sp.z2) {
		parts_list_resort(parts);
	}

	if (parts->dirty) {
		TAILQ_REMOVE(&dirty_list, parts, dirty_list_entry);
		parts->dirty = false;
	}

	parts_do_layout(parts);

	struct parts *child;
	PARTS_FOREACH_CHILD(child, parts) {
		parts_update_component(child);
	}
}

void PE_UpdateComponent(possibly_unused int passed_time)
{
	// After loading a save, the game script may compute a negative time delta
	// because it stores an absolute system.GetTime() value in the save data,
	// which is meaningless across process restarts.
	if (passed_time < 0)
		passed_time = 0;
	while (!TAILQ_EMPTY(&dirty_list)) {
		// pop parts object from dirty list
		struct parts *parts = TAILQ_FIRST(&dirty_list);
		TAILQ_REMOVE(&dirty_list, parts, dirty_list_entry);
		parts->dirty = false;

		// update parent
		struct parts *parent;
		if (parts->pending_parent >= 0 && (parent = parts_try_get(parts->pending_parent))) {
			if (parts->parent) {
				TAILQ_REMOVE(&parts->parent->children, parts, child_list_entry);
			}
			parts->parent = parent;
			TAILQ_INSERT_TAIL(&parent->children, parts, child_list_entry);

			// if parent is layout box, mark it dirty so that it can re-layout its children
			if (parent->states[0].type == PARTS_LAYOUT_BOX)
				parts_component_dirty(parent);
			parts_user_component_dirty(parent);
			// v14: also a box above a free box or a user component
			parts_layout_size_changed(parts);
		}
		// TODO: should the child be orphaned if it already has a parent and an invalid
		//       parent no is given?
		parts->pending_parent = -1;

		// don't do anything here if parent is dirty
		if (parts_has_dirty_parent(parts))
			continue;

		// update child params
		parts_update_component(parts);
	}
}

bool parts_message_window_show = true;

void PE_Update(int passed_time, bool message_window_show)
{
	// v14 (Dohna Dohna): the game computes passed_time with its own
	// CASTimerManager, which yields 0 under xsystem4; without a real
	// delta, motions and loop animations never advance. Fall back to
	// wall-clock time when the game reports no progress.
	if (ain->version >= 14 && passed_time == 0) {
		static struct timespec pe_last_time;
		struct timespec now;
		clock_gettime(CLOCK_MONOTONIC, &now);
		if (pe_last_time.tv_sec > 0) {
			double delta_ms = (now.tv_sec - pe_last_time.tv_sec) * 1000.0
				+ (now.tv_nsec - pe_last_time.tv_nsec) / 1e6;
			passed_time = (int)delta_ms;
			if (passed_time < 0) passed_time = 0;
			if (passed_time > 100) passed_time = 100;
		}
		pe_last_time = now;
	}
	parts_message_window_show = message_window_show;
	PE_UpdateComponent(passed_time);
	audio_update();
	parts_update_animation(passed_time);
	PE_UpdateInputState(passed_time);
	parts_render_update();
}

void PE_UpdateParts(int passed_time, possibly_unused bool is_skip, bool message_window_show)
{
	parts_message_window_show = message_window_show;
	audio_update();
	parts_update_animation(passed_time);
	parts_render_update();
}

void PE_SetDelegateIndex(int parts_no, int delegate_index)
{
	parts_get(parts_no)->delegate_index = delegate_index;
}

int PE_GetDelegateIndex(int parts_no)
{
	struct parts *parts = parts_try_get(parts_no);
	return parts ? parts->delegate_index : -1;
}

bool PE_SetPartsCG(int parts_no, struct string *cg_name, int sprite_deform, int state)
{
	if (!parts_state_valid(--state))
		return false;

	struct parts *parts = parts_get(parts_no);
	parts->sprite_deform = sprite_deform;
	if (!cg_name || *(cg_name->text) == '\0') {
		parts_state_reset(&parts->states[state], PARTS_CG);
		parts_dirty(parts);
		// A layout box sizes the parts by its normal state, now 0x0.
		if (!state)
			parts_layout_size_changed(parts);
		return true;
	}

	struct parts_cg *cg = parts_get_cg(parts, state);
	return parts_cg_set(parts, cg, cg_name);
}

bool PE_SetPartsCG_by_index(int parts_no, int cg_no, int sprite_deform, int state)
{
	if (!parts_state_valid(--state))
		return false;

	struct parts *parts = parts_get(parts_no);
	parts->sprite_deform = sprite_deform;
	if (!cg_no) {
		parts_state_reset(&parts->states[state], PARTS_CG);
		parts_dirty(parts);
		if (!state)
			parts_layout_size_changed(parts);
		return true;
	}

	struct parts_cg *cg = parts_get_cg(parts, state);
	return parts_cg_set_by_index(parts, cg, cg_no);
}

// XXX: Rance Quest
bool PE_SetPartsCG_by_string_index(int parts_no, struct string *cg_name,
		int sprite_deform, int state)
{
	if (!parts_state_valid(--state))
		return false;

	struct parts *parts = parts_get(parts_no);
	parts->sprite_deform = sprite_deform;
	if (!cg_name) {
		parts_state_reset(&parts->states[state], PARTS_CG);
		parts_dirty(parts);
		if (!state)
			parts_layout_size_changed(parts);
		return true;
	}

	struct parts_cg *cg = parts_get_cg(parts, state);
	int cg_no = atoi(cg_name->text);
	if (cg_no) {
		if (!parts_cg_set_by_index(parts, cg, atoi(cg_name->text)))
			return false;
		cg->name = string_ref(cg_name);
		return true;
	} else if (!memcmp(cg_name->text, "<save>SaveData\\", 15)) {
		char *path = savedir_path(cg_name->text + 15);
		bool result = _parts_cg_set(parts, cg, cg_load_file(path), 0, string_ref(cg_name));
		free(path);
		return result;
	} else {
		VM_ERROR("Invalid CG name: %s", display_sjis0(cg_name->text));
	}
}

void PE_GetPartsCGName(int parts_no, struct string **cg_name, int state)
{
	if (!parts_state_valid(--state))
		return;
	struct parts_cg *cg = parts_get_cg(parts_get(parts_no), state);
	if (cg->name) {
		if (*cg_name)
			free_string(*cg_name);
		*cg_name = string_dup(cg->name);
	}
}

bool PE_SetPartsCGSurfaceArea(int parts_no, int x, int y, int w, int h, int state)
{
	if (!parts_state_valid(--state))
		return false;

	struct parts *parts = parts_get(parts_no);
	struct parts_cg *cg = parts_get_cg(parts, state);
	parts_set_surface_area(parts, &cg->common, x, y, w, h);
	return true;
}

void PE_GetPartsCGSurfaceArea(int parts_no, int *x, int *y, int *w, int *h, int state)
{
	if (!parts_state_valid(--state))
		return;

	struct parts_cg *cg = parts_get_cg(parts_get(parts_no), state);
	*x = cg->common.surface_area.x;
	*y = cg->common.surface_area.y;
	*w = cg->common.surface_area.w;
	*h = cg->common.surface_area.h;
}

int PE_GetPartsCGNumber(int parts_no, int state)
{
	if (!parts_state_valid(--state)) {
		return false;
	}

	return parts_get_cg(parts_get(parts_no), state)->no;
}

static bool _parts_animation_set_cg(struct parts *parts, struct parts_animation *anim,
		int start_no, int nr_frames, int frame_time,
		struct cg *(*load_cg)(int no, void *data), void *data)
{
	int w = 0, h = 0;
	Texture *frames = xcalloc(nr_frames, sizeof(Texture));
	for (int i = 0; i < nr_frames; i++) {
		struct cg *cg = load_cg(start_no + i, data);
		if (!cg) {
			for (int j = 0; j < i; j++) {
				gfx_delete_texture(&frames[j]);
			}
			free(frames);
			return false;
		}
		gfx_init_texture_with_cg(&frames[i], cg);
		w = max(w, cg->metrics.w);
		h = max(h, cg->metrics.h);
		cg_free(cg);
	}

	parts_set_dims(parts, &anim->common, w, h);
	free(anim->frames);
	anim->start_no = start_no;
	anim->frames = frames;
	anim->nr_frames = nr_frames;
	anim->frame_time = frame_time;
	anim->elapsed = 0;
	anim->current_frame = 0;
	anim->common.texture = frames[0];
	return true;
}

static struct cg *load_loop_cg_by_index(int no, void *_)
{
	return asset_cg_load(no);
}

bool parts_animation_set_cg_by_index(struct parts *parts, struct parts_animation *anim,
		int cg_no, int nr_frames, int frame_time)
{
	return _parts_animation_set_cg(parts, anim, cg_no, nr_frames, frame_time,
			load_loop_cg_by_index, NULL);
}

bool PE_SetLoopCG_by_index(int parts_no, int cg_no, int nr_frames, int frame_time, int state)
{
	if (!parts_state_valid(--state))
		return false;
	if (nr_frames <= 0 || nr_frames > 10000) {
		WARNING("Invalid frame count: %d", nr_frames);
		return false;
	}

	struct parts *parts = parts_get(parts_no);
	struct parts_animation *anim = parts_get_animation(parts, state);
	return parts_animation_set_cg_by_index(parts, anim, cg_no, nr_frames, frame_time);
}

static struct cg *load_loop_cg_by_name(int no, void *data)
{
	int unused_no;
	struct string *cg_name = string_format((struct string*)data, (union vm_value){.i=no}, STRFMT_INT);
	struct cg *cg = asset_cg_load_by_name(cg_name->text, &unused_no);
	free_string(cg_name);
	return cg;
}

bool parts_animation_set_cg(struct parts *parts, struct parts_animation *anim,
		struct string *cg_name, int start_no, int nr_frames, int frame_time)
{
	bool r = _parts_animation_set_cg(parts, anim, start_no, nr_frames, frame_time,
			load_loop_cg_by_name, cg_name);
	if (r)
		anim->cg_name = string_dup(cg_name);
	return r;
}

bool PE_SetLoopCG(int parts_no, struct string *cg_name, int start_no, int nr_frames,
	int frame_time, int state)
{
	if (!parts_state_valid(--state))
		return false;
	if (nr_frames <= 0 || nr_frames > 10000) {
		WARNING("Invalid frame count: %d", nr_frames);
		return false;
	}

	struct parts *parts = parts_get(parts_no);
	struct parts_animation *anim = parts_get_animation(parts, state);
	return parts_animation_set_cg(parts, anim, cg_name, start_no, nr_frames, frame_time);
}

bool PE_SetLoopCGSurfaceArea(int parts_no, int x, int y, int w, int h, int state)
{
	if (!parts_state_valid(--state)) {
		WARNING("Invalid parts state: %d", state);
		return false;
	}

	struct parts *parts = parts_get(parts_no);
	struct parts_animation *anim = parts_get_animation(parts, state);
	parts_set_surface_area(parts, &anim->common, x, y, w, h);
	return true;
}

/* Native 0x59b760 normalizes only for drawing; getters retain the raw
 * surface rectangle. A nonpositive width AND height means the whole CG. */
void parts_v14_gauge_surface(struct parts_gauge *g, Rectangle *out)
{
	Rectangle r = g->common.surface_area;
	if (r.w <= 0 && r.h <= 0) {
		r = (Rectangle) {0, 0, g->cg.w, g->cg.h};
	} else {
		r.x = max(0, r.x); r.y = max(0, r.y);
		r.w = max(0, min(r.w, g->cg.w - r.x));
		r.h = max(0, min(r.h, g->cg.h - r.y));
	}
	*out = r;
}

/* Native H 0x5a7d90 / V 0x5c4020. Return a rectangle relative to the
 * effective surface, used for BOTH geometry and source cropping. */
void parts_v14_gauge_fill_rect(struct parts_gauge *g, bool vertical, Rectangle *out)
{
	Rectangle area;
	parts_v14_gauge_surface(g, &area);
	Rectangle r = {0, 0, area.w, area.h};
	int extent = vertical ? area.h : area.w;
	int pixels = extent;
	if (g->denominator > 0 && g->numerator != g->denominator) {
		float n = fminf(fmaxf(g->numerator, 0), g->denominator);
		float value = extent * n / g->denominator;
		// Keep malformed/non-finite data away from float-to-int UB.
		pixels = !isfinite(value) || value >= extent ? extent : value <= 0 ? 0 : (int)value;
		pixels = max(0, min(pixels, extent));
	}
	if (vertical) {
		r.h = pixels;
		r.y = g->reverse ? 0 : extent - pixels;
	} else {
		r.w = pixels;
		r.x = g->reverse ? extent - pixels : 0;
	}
	*out = r;
}

static void _parts_set_gauge_cg(struct parts *parts, struct parts_gauge *g, struct cg *cg)
{
	if (ain->version >= 14) {
		gfx_delete_texture(&g->cg);
		gfx_delete_texture(&g->common.texture);
		gfx_init_texture_with_cg(&g->common.texture, cg);
		// v14 draws a cropped quad from the original image; no per-frame
		// render-to-texture copy. cg keeps source dimensions, not an alias.
		g->cg.w = cg->metrics.w; g->cg.h = cg->metrics.h;
		parts_set_dims(parts, &g->common, g->cg.w, g->cg.h);
		parts_dirty(parts);
		return;
	}

	gfx_init_texture_with_cg(&g->cg, cg);
	gfx_init_texture_with_cg(&g->common.texture, cg);

	gfx_init_texture_rgba(&g->common.texture, g->cg.w, g->cg.h, (SDL_Color){0,0,0,255});
	gfx_copy_with_alpha_map(&g->common.texture, 0, 0, &g->cg, 0, 0, g->cg.w, g->cg.h);

	parts_set_dims(parts, &g->common, g->cg.w, g->cg.h);

	parts_dirty(parts);
}

bool parts_gauge_set_cg(struct parts *parts, struct parts_gauge *g, struct string *cg_name)
{
	if (ain->version >= 14 && !strcmp(g->cg_name ? g->cg_name->text : "", cg_name->text))
		return true;

	int cg_no;
	struct cg *cg = asset_cg_load_by_name(cg_name->text, &cg_no);
	if (!cg)
		return false;
	_parts_set_gauge_cg(parts, g, cg);
	if (ain->version >= 14) cg_free(cg);
	g->cg_no = cg_no;
	if (ain->version >= 14) {
		if (g->cg_name) free_string(g->cg_name);
		g->cg_name = string_ref(cg_name);
	}
	return true;
}

bool parts_gauge_set_cg_by_index(struct parts *parts, struct parts_gauge *g, int cg_no)
{
	struct cg *cg = asset_cg_load(cg_no);
	if (!cg)
		return false;
	_parts_set_gauge_cg(parts, g, cg);
	if (ain->version >= 14) cg_free(cg);
	g->cg_no = cg_no;
	return true;
}

bool PE_SetHGaugeCG(int parts_no, struct string *cg_name, int state)
{
	if (!parts_state_valid(--state))
		return false;

	struct parts *parts = parts_get(parts_no);
	struct parts_gauge *g = parts_get_hgauge(parts, state);
	return parts_gauge_set_cg(parts, g, cg_name);
}

bool PE_SetHGaugeCG_by_index(int parts_no, int cg_no, int state)
{
	if (!parts_state_valid(--state))
		return false;

	struct parts *parts = parts_get(parts_no);
	struct parts_gauge *g = parts_get_hgauge(parts, state);
	return parts_gauge_set_cg_by_index(parts, g, cg_no);
}

bool PE_SetVGaugeCG(int parts_no, struct string *cg_name, int state)
{
	if (!parts_state_valid(--state))
		return false;

	struct parts *parts = parts_get(parts_no);
	struct parts_gauge *g = parts_get_vgauge(parts, state);
	return parts_gauge_set_cg(parts, g, cg_name);
}

bool PE_SetVGaugeCG_by_index(int parts_no, int cg_no, int state)
{
	if (!parts_state_valid(--state))
		return false;

	struct parts *parts = parts_get(parts_no);
	struct parts_gauge *g = parts_get_vgauge(parts, state);
	return parts_gauge_set_cg_by_index(parts, g, cg_no);
}

void parts_hgauge_set_rate(struct parts *parts, struct parts_gauge *g, float rate)
{
	if (ain->version >= 14) {
		g->numerator = rate; g->denominator = 1; g->rate = rate;
		parts_dirty(parts);
		return;
	}

	if (!g->common.texture.handle) {
		WARNING("HGauge texture uninitialized");
		return;
	}
	int pixels = rate * g->cg.w;
	gfx_copy_with_alpha_map(&g->common.texture, 0, 0, &g->cg, 0, 0, pixels, g->cg.h);
	gfx_fill_amap(&g->common.texture, pixels, 0, g->cg.w - pixels, g->cg.h, 0);
	parts_dirty(parts);
	g->rate = rate;
}

void parts_vgauge_set_rate(struct parts *parts, struct parts_gauge *g, float rate)
{
	if (ain->version >= 14) {
		g->numerator = rate; g->denominator = 1; g->rate = rate;
		parts_dirty(parts);
		return;
	}

	if (!g->common.texture.handle) {
		WARNING("VGauge texture uninitialized");
		return;
	}
	int pixels = rate * g->cg.h;
	gfx_copy_with_alpha_map(&g->common.texture, 0, pixels, &g->cg, 0, pixels, g->cg.w, g->cg.h - pixels);
	gfx_fill_amap(&g->common.texture, 0, 0, g->cg.w, pixels, 0);
	parts_dirty(parts);
	g->rate = rate;
}

bool PE_SetHGaugeRate(int parts_no, float numerator, float denominator, int state)
{
	if (!parts_state_valid(--state))
		return false;

	struct parts *parts = parts_get(parts_no);
	struct parts_gauge *g = parts_get_hgauge(parts, state);
	if (ain->version >= 14) {
		g->numerator = numerator; g->denominator = denominator;
		g->rate = denominator > 0 ? numerator / denominator : 1;
		parts_dirty(parts);
	} else {
	parts_hgauge_set_rate(parts, g, numerator/denominator);
	}
	return true;
}

bool PE_SetHGaugeRate_int(int parts_no, int numerator, int denominator, int state)
{
	return PE_SetHGaugeRate(parts_no, numerator, denominator, state);
}

bool PE_SetVGaugeRate(int parts_no, float numerator, float denominator, int state)
{
	if (!parts_state_valid(--state))
		return false;

	struct parts *parts = parts_get(parts_no);
	struct parts_gauge *g = parts_get_vgauge(parts, state);
	if (ain->version >= 14) {
		g->numerator = numerator; g->denominator = denominator;
		g->rate = denominator > 0 ? numerator / denominator : 1;
		parts_dirty(parts);
	} else {
	parts_vgauge_set_rate(parts, g, (float)numerator/(float)denominator);
	}
	return true;
}

bool PE_SetVGaugeRate_int(int parts_no, int numerator, int denominator, int state)
{
	return PE_SetVGaugeRate(parts_no, numerator, denominator, state);
}

bool PE_SetHGaugeSurfaceArea(int parts_no, int x, int y, int w, int h, int state)
{
	if (!parts_state_valid(--state))
		return false;

	struct parts *parts = parts_get(parts_no);
	struct parts_gauge *g = parts_get_hgauge(parts, state);
	if (ain->version >= 14) {
		g->common.surface_area = (Rectangle) {x, y, w, h};
		parts_common_recalculate_hitbox(parts, &g->common);
		parts_dirty(parts);
		// The gauge's size is now the area's.
		if (!state)
			parts_layout_size_changed(parts);
	} else {
		parts_set_surface_area(parts, &g->common, x, y, w, h);
	}
	return true;
}

bool PE_SetVGaugeSurfaceArea(int parts_no, int x, int y, int w, int h, int state)
{
	if (!parts_state_valid(--state))
		return false;

	struct parts *parts = parts_get(parts_no);
	struct parts_gauge *g = parts_get_vgauge(parts, state);
	if (ain->version >= 14) {
		g->common.surface_area = (Rectangle) {x, y, w, h};
		parts_common_recalculate_hitbox(parts, &g->common);
		parts_dirty(parts);
		// The gauge's size is now the area's.
		if (!state)
			parts_layout_size_changed(parts);
	} else {
		parts_set_surface_area(parts, &g->common, x, y, w, h);
	}
	return true;
}

bool PE_SetNumeralCG(int parts_no, struct string *cg_name, int state)
{
	if (!parts_state_valid(--state))
		return false;
	struct parts_numeral *n = parts_get_numeral(parts_get(parts_no), state);
	n->font_no = parts_load_numeral_font_separate_string(cg_name);
	return true;
}

bool PE_SetNumeralCG_by_index(int parts_no, int cg_no, int state)
{
	if (!parts_state_valid(--state))
		return false;

	struct parts_numeral *n = parts_get_numeral(parts_get(parts_no), state);
	n->font_no = parts_load_numeral_font_separate(cg_no);
	return true;
}

bool PE_SetNumeralLinkedCGNumberWidthWidthList_by_index(int parts_no, int cg_no,
		int w0, int w1, int w2, int w3, int w4, int w5, int w6, int w7, int w8,
		int w9, int w_minus, int w_comma, int state)
{
	if (!parts_state_valid(--state))
		return false;

	struct cg *cg = asset_cg_load(cg_no);
	if (!cg)
		return false;

	int w[12] = { w0, w1, w2, w3, w4, w5, w6, w7, w8, w9, w_minus, w_comma };
	struct parts_numeral *n = parts_get_numeral(parts_get(parts_no), state);
	n->font_no = parts_load_numeral_font_combined(cg, cg_no, w);
	return true;
}

bool PE_SetNumeralLinkedCGNumberWidthWidthList(int parts_no, struct string *cg_name,
		int w0, int w1, int w2, int w3, int w4, int w5, int w6, int w7, int w8,
		int w9, int w_minus, int w_comma, int state)
{
	if (!parts_state_valid(--state))
		return false;

	int no;
	struct cg *cg = asset_cg_load_by_name(cg_name->text, &no);
	if (!cg)
		return false;

	int w[12] = { w0, w1, w2, w3, w4, w5, w6, w7, w8, w9, w_minus, w_comma };
	struct parts_numeral *n = parts_get_numeral(parts_get(parts_no), state);
	n->font_no = parts_load_numeral_font_combined(cg, no, w);
	return true;
}

bool PE_SetNumeralNumber(int parts_no, int n, int state)
{
	if (!parts_state_valid(--state))
		return false;

	struct parts *parts = parts_get(parts_no);
	struct parts_numeral *numeral = parts_get_numeral(parts, state);
	return parts_numeral_set_number(parts, numeral, n);
}

bool PE_SetNumeralShowComma(int parts_no, bool show_comma, int state)
{
	if (!parts_state_valid(--state))
		return false;

	struct parts *parts = parts_get(parts_no);
	struct parts_numeral *num = parts_get_numeral(parts, state);
	if (num->show_comma == show_comma)
		return true;

	num->show_comma = show_comma;
	parts_numeral_update(parts, num);
	return true;
}

bool PE_SetNumeralSpace(int parts_no, int space, int state)
{
	if (!parts_state_valid(--state))
		return false;

	struct parts *parts = parts_get(parts_no);
	struct parts_numeral *num = parts_get_numeral(parts, state);
	if (num->space == space)
		return true;

	num->space = space;
	parts_numeral_update(parts, num);
	return true;
}

bool PE_SetNumeralLength(int parts_no, int length, int state)
{
	if (!parts_state_valid(--state))
		return false;

	struct parts *parts = parts_get(parts_no);
	struct parts_numeral *num = parts_get_numeral(parts, state);
	if (num->length == length)
		return true;

	num->length = max(1, length);
	num->have_num = true;
	parts_numeral_update(parts, num);
	return true;
}

bool PE_SetNumeralSurfaceArea(int parts_no, int x, int y, int w, int h, int state)
{
	if (!parts_state_valid(--state))
		return false;

	struct parts *parts = parts_get(parts_no);
	struct parts_numeral *n = parts_get_numeral(parts, state);
	parts_set_surface_area(parts, &n->common, x, y, w, h);
	return true;
}

void PE_ReleaseParts(int parts_no)
{
	parts_release(parts_no);
}

void PE_ReleaseAllParts(void)
{
	parts_release_all();
}

void PE_ReleaseAllPartsWithoutSystem(void)
{
	// FIXME: what's the difference?
	parts_release_all();
}

void PE_ReleaseAllWithoutSystem(struct page **erase_number_list)
{
	// Release all parts not belonging to the system overlay controller
	struct parts *parts = TAILQ_FIRST(&parts_list);
	while (parts) {
		struct parts *next = TAILQ_NEXT(parts, parts_list_entry);
		if (parts->controller_no != PARTS_CONTROLLER_SYSTEM_OVERLAY) {
			*erase_number_list = array_pushback(*erase_number_list,
					(union vm_value){.i = parts->no}, AIN_ARRAY_INT, -1);
			parts_release(parts->no);
		}
		parts = next;
	}

	// Drop all normal controllers and add a fresh default one.
	ctrl_stack.nr_controllers = 0;
	PE_AddController(-1);
}

void PE_SetPos(int parts_no, int x, int y)
{
	parts_set_pos(parts_get(parts_no), (Point){ x, y });
}

void PE_SetZ(int parts_no, int z)
{
	parts_set_z(parts_get(parts_no), z);
}

void PE_SetShow(int parts_no, bool show)
{
	parts_set_show(parts_get(parts_no), show);
}

void PE_SetAlpha(int parts_no, int alpha)
{
	parts_set_alpha(parts_get(parts_no), alpha);
}

void PE_SetPartsDrawFilter(int parts_no, int draw_filter)
{
	parts_get(parts_no)->draw_filter = draw_filter;
}

void PE_SetAddColor(int parts_no, int r, int g, int b)
{
	SDL_Color add_color = {
		min(255, max(0, r)),
		min(255, max(0, g)),
		min(255, max(0, b)),
		255
	};
	parts_set_add_color(parts_get(parts_no), add_color);
}

void PE_SetMultiplyColor(int parts_no, int r, int g, int b)
{
	SDL_Color multiply_color = {
		min(255, max(0, r)),
		min(255, max(0, g)),
		min(255, max(0, b)),
		255
	};
	parts_set_multiply_color(parts_get(parts_no), multiply_color);
}

int PE_GetPartsX(int parts_no)
{
	return parts_get(parts_no)->local.pos.x;
}

int PE_GetPartsY(int parts_no)
{
	return parts_get(parts_no)->local.pos.y;
}

int PE_GetPartsWidth(int parts_no, int state)
{
	if (!parts_state_valid(--state))
		return 0;
	return parts_get(parts_no)->states[state].common.w;
}

int PE_GetPartsHeight(int parts_no, int state)
{
	if (!parts_state_valid(--state))
		return 0;
	return parts_get(parts_no)->states[state].common.h;
}

void PE_GetPartsSize(int parts_no, int *width, int *height, int state)
{
	if (!width || !height)
		return;
	if (!parts_state_valid(--state)) {
		*width = 0;
		*height = 0;
		return;
	}
	struct parts *parts = parts_get(parts_no);
	*width = parts->states[state].common.w;
	*height = parts->states[state].common.h;
}

int PE_GetPartsCGDeform(int parts_no, int state)
{
	// SpriteDeform: 0 = normal (no deformation)
	// PE_SetPartsCG receives this but ignores it.
	(void)parts_no; (void)state;
	return 0;
}

int PE_GetPartsUpperLeftPosX(int parts_no, int state)
{
	if (!parts_state_valid(--state))
		return 0;
	struct parts *parts = parts_get(parts_no);
	return parts_screen_upper_left(parts, &parts->states[state].common).x;
}

int PE_GetPartsUpperLeftPosY(int parts_no, int state)
{
	if (!parts_state_valid(--state))
		return 0;
	struct parts *parts = parts_get(parts_no);
	return parts_screen_upper_left(parts, &parts->states[state].common).y;
}

int PE_GetPartsZ(int parts_no)
{
	return parts_get(parts_no)->local.z;
}

bool PE_GetPartsShow(int parts_no)
{
	return parts_get(parts_no)->local.show;
}

int PE_GetPartsAlpha(int parts_no)
{
	return parts_get(parts_no)->local.alpha;
}

int PE_GetPartsDrawFilter(int parts_no)
{
	return parts_get(parts_no)->draw_filter;
}

void PE_GetAddColor(int parts_no, int *r, int *g, int *b)
{
	struct parts *parts = parts_get(parts_no);
	*r = parts->local.add_color.r;
	*g = parts->local.add_color.g;
	*b = parts->local.add_color.b;
}

void PE_GetMultiplyColor(int parts_no, int *r, int *g, int *b)
{
	struct parts *parts = parts_get(parts_no);
	*r = parts->local.multiply_color.r;
	*g = parts->local.multiply_color.g;
	*b = parts->local.multiply_color.b;
}

void PE_SetPartsOriginPosMode(int parts_no, int origin_pos_mode)
{
	struct parts *parts = parts_get(parts_no);
	parts_set_origin_mode(parts, origin_pos_mode);
	parts_dirty(parts);
}

int PE_GetPartsOriginPosMode(int parts_no)
{
	return parts_get(parts_no)->origin_mode;
}

static bool parts_is_ancestor(struct parts *ancestor, struct parts *parts)
{
	for (struct parts *p = parts; p; p = p->parent) {
		if (p == ancestor)
			return true;
	}
	return false;
}

/* v14: the native setter (0x58f060 -> 0x58f310 -> 0x550d90) links the child
 * immediately and 0 detaches it (0x53ab60); an unknown parent is ignored.
 * AIN walks the tree with NumofChild/GetChild in the same call, e.g.
 * activity::detail::CallUserComponentEventWithChild right after ReadFile. */
static void parts_set_parent_now(struct parts *parts, int parent_parts_no)
{
	struct parts *parent = NULL;
	if (parent_parts_no != 0) {
		parent = parts_try_get(parent_parts_no);
		if (!parent || parts_is_ancestor(parts, parent))
			return;
	}
	parts->pending_parent = -1;
	if (parts->parent == parent)
		return;
	if (parts->parent) {
		struct parts *old = parts->parent;
		// Before leaving: the boxes above the old parent lose its size.
		parts_layout_size_changed(parts);
		TAILQ_REMOVE(&old->children, parts, child_list_entry);
		if (old->states[0].type == PARTS_LAYOUT_BOX)
			parts_component_dirty(old);
		parts_user_component_dirty(old);
	}
	parts->parent = parent;
	if (parent) {
		TAILQ_INSERT_TAIL(&parent->children, parts, child_list_entry);
		if (parent->states[0].type == PARTS_LAYOUT_BOX)
			parts_component_dirty(parent);
		parts_user_component_dirty(parent);
		// E.g. a user component's content root (the component's size).
		parts_layout_size_changed(parts);
	}
	parts_component_dirty(parts);
}

void PE_SetParentPartsNumber(int parts_no, int parent_parts_no)
{
	struct parts *parts = parts_get(parts_no);
	if (ain->version >= 14) {
		parts_set_parent_now(parts, parent_parts_no);
		return;
	}
	parts->pending_parent = parent_parts_no;
	parts_component_dirty(parts);
}

int PE_GetParentPartsNumber(int parts_no)
{
	struct parts *parts = parts_get(parts_no);
	if (parts->parent)
		return parts->parent->no;
	return -1;
}

bool PE_SetPartsGroupNumber(possibly_unused int PartsNumber, possibly_unused int GroupNumber)
{
	UNIMPLEMENTED("(%d, %d)", PartsNumber, GroupNumber);
	return true;
}

void PE_SetPartsMessageWindowShowLink(possibly_unused int parts_no, bool message_window_show_link)
{
	struct parts *parts = parts_get(parts_no);
	parts->message_window = message_window_show_link;
}

bool PE_GetPartsMessageWindowShowLink(int parts_no)
{
	return parts_get(parts_no)->message_window;
}

void PE_SetPartsMagX(int parts_no, float scale_x)
{
	struct parts *parts = parts_get(parts_no);
	parts_set_scale_x(parts, scale_x);
}

float PE_GetPartsMagX(int parts_no)
{
	return parts_get(parts_no)->local.scale.x;
}

void PE_SetPartsMagY(int parts_no, float scale_y)
{
	struct parts *parts = parts_get(parts_no);
	parts_set_scale_y(parts, scale_y);
}

float PE_GetPartsMagY(int parts_no)
{
	return parts_get(parts_no)->local.scale.y;
}

void PE_SetPartsRotateX(int parts_no, float rot_x)
{
	UNIMPLEMENTED("(%d, %f)", parts_no, rot_x);
	parts_set_rotation_x(parts_get(parts_no), rot_x);
}

void PE_SetPartsRotateY(int parts_no, float rot_y)
{
	UNIMPLEMENTED("(%d, %f)", parts_no, rot_y);
	parts_set_rotation_y(parts_get(parts_no), rot_y);
}

void PE_SetPartsRotateZ(int parts_no, float rot_z)
{
	parts_set_rotation_z(parts_get(parts_no), rot_z);
}

float PE_GetPartsRotateZ(int parts_no)
{
	return parts_get(parts_no)->local.rotation.z;
}

void PE_SetPartsAlphaClipperPartsNumber(int parts_no, int alpha_clipper_parts_no)
{
	struct parts *parts = parts_get(parts_no);
	parts->alpha_clipper_parts_no = alpha_clipper_parts_no;
	parts_dirty(parts);
}

void PE_SetPartsPixelDecide(int parts_no, bool pixel_decide)
{
	parts_get(parts_no)->pixel_hittest = pixel_decide;
}

bool PE_SetThumbnailReductionSize(int reduction_size)
{
	UNIMPLEMENTED("(%d)", reduction_size);
	return true;
}

bool PE_SetThumbnailMode(bool mode)
{
	UNIMPLEMENTED("(%s)", mode ? "true" : "false");
	return true;
}

bool PE_save_thumbnail(struct string *filename, int reduction_factor)
{
	if (reduction_factor < 1)
		reduction_factor = 1;

	Texture *src = gfx_main_surface();
	int w = src->w / reduction_factor;
	int h = src->h / reduction_factor;

	// Downscale by repeatedly halving until we are within a factor of two of
	// the target size, then do the final stretch. This avoids the aliasing
	// that a single bilinear minification would otherwise produce.
	Texture tmp, *cur = src;
	bool have_tmp = false;
	while (cur->w / 2 > w && cur->h / 2 > h) {
		Texture next;
		gfx_init_texture_blank(&next, cur->w / 2, cur->h / 2);
		gfx_copy_stretch_with_alpha_map(&next, 0, 0, next.w, next.h, cur, 0, 0, cur->w, cur->h);
		if (have_tmp)
			gfx_delete_texture(&tmp);
		tmp = next;
		cur = &tmp;
		have_tmp = true;
	}

	Texture dst;
	gfx_init_texture_blank(&dst, w, h);
	gfx_copy_stretch_with_alpha_map(&dst, 0, 0, w, h, cur, 0, 0, cur->w, cur->h);
	if (have_tmp)
		gfx_delete_texture(&tmp);

	char *path = savedir_path(filename->text);
	int r = gfx_save_texture(&dst, path, ALCG_QNT);
	free(path);
	gfx_delete_texture(&dst);
	return !!r;
}

void PE_SetInputState(int parts_no, int state)
{
	if (!parts_state_valid(--state)) {
		WARNING("invalid input state: %d", state);
		return;
	}
	parts_set_state(parts_get(parts_no), state);
}

int PE_GetInputState(int parts_no)
{
	return parts_get(parts_no)->state + 1;
}

void PE_SetComponentType(int parts_no, int type, int state)
{
	// v14 (Dohna Dohna): component types are a game-level widget taxonomy
	// used by bytecode vtable dispatch via
	// GetComponentType, not a 1:1 mapping onto engine parts states. Store
	// the raw value; the actual parts state is built by the pactex loader
	// and the Create* calls.
	if (ain->version >= 14) {
		struct parts *parts = parts_get(parts_no);
		// Native 0x535e20: the widget already has the type -> nothing
		// (0x536540). A low-level widget (outer type 18) takes a state type
		// (19 and up) into that state (0x5653d0) and stays low-level, so
		// e.g. GetCGDetection's wrapper keeps a detection CG undrawn.
		// Any other type replaces the widget (raw value, state types
		// cleared).
		if (PE_GetComponentType(parts_no, state) == type)
			return;
		if (type > 18 && parts->component_type == 18 && state >= 1 && state <= PARTS_NR_STATES) {
			switch (type) {
			case 19: case 27: parts_get_cg(parts, state - 1); break;
			case 21: parts_get_text(parts, state - 1); break;
			case 22: parts_get_hgauge(parts, state - 1); break;
			case 23: parts_get_vgauge(parts, state - 1); break;
			case 24: parts_get_numeral(parts, state - 1); break;
			case 25:
				if (parts->states[state - 1].type != PARTS_RECT_DETECTION)
					parts_state_reset(&parts->states[state - 1], PARTS_RECT_DETECTION);
				break;
			case 26: parts_get_construction_process(parts, state - 1); break;
			default: break;
			}
			parts->component_state_type[state - 1] = type;
			parts_dirty(parts);
			return;
		}
		parts->component_type = type;
		// Preserve explicit raw setters over the loader's inferred state types.
		memset(parts->component_state_type, 0, sizeof(parts->component_state_type));
		parts_user_component_dirty(parts);
		return;
	}
	if (!parts_state_valid(--state))
		return;
	struct parts *parts = parts_get(parts_no);
	enum parts_type pt = PARTS_UNINITIALIZED;
	switch (type) {
	case 8:  pt = PARTS_LAYOUT_BOX; break;
	case 11: pt = PARTS_CG; break;
	case 12: pt = PARTS_ANIMATION; break;
	case 13: pt = PARTS_TEXT; break;
	case 14: pt = PARTS_HGAUGE; break;
	case 15: pt = PARTS_VGAUGE; break;
	case 16: pt = PARTS_NUMERAL; break;
	case 17: pt = PARTS_RECT_DETECTION; break;
	case 18: pt = PARTS_CONSTRUCTION_PROCESS; break;
	case 20: pt = PARTS_FLAT; break;
	case 21: pt = PARTS_3DLAYER; break;
	case 22: pt = PARTS_MOVIE; break;
	default:
		VM_ERROR("unknown component type %d", type);
	}
	if (parts->states[state].type != pt)
		parts_state_reset(&parts->states[state], pt);
}

int PE_GetComponentType(int parts_no, int state)
{
	// v14: recognized low-level pactex states have their own native type.
	// Other widgets retain the raw value stored by PE_SetComponentType.
	// -1 is a valid "no parts" sentinel used by bytecode vtable dispatch.
	if (ain->version >= 14) {
		if (parts_no < 0)
			return -1;
		struct parts *parts = parts_try_get(parts_no);
		if (parts) {
			int native = state >= 1 && state <= PARTS_NR_STATES
				? parts->component_state_type[state - 1] : 0;
			if (native) {
				// A recognized state follows a later CG/text transition.
				switch (parts->states[state - 1].type) {
				case PARTS_TEXT: return 21;
				case PARTS_CG: return native == 21 || native == 22 || native == 23 ? 19 : native;
				case PARTS_NUMERAL: return 24;
				case PARTS_HGAUGE: return 22;
				case PARTS_VGAUGE: return 23;
				default: return native;
				}
			}
			return parts->component_type;
		}
		// Bytecode may probe numbers allocated by GetFreeNumber before
		// the parts entry exists; auto-create so Wrap/IsValid work.
		if (parts_no >= 1000000000)
			return parts_get(parts_no)->component_type;
		return -1;
	}
	if (!parts_state_valid(--state))
		return -1;
	struct parts *parts = parts_try_get(parts_no);
	if (!parts)
		return -1;

	switch (parts->states[state].type) {
	case PARTS_LAYOUT_BOX: return 8;
	case PARTS_UNINITIALIZED:  // defaluts to CG
	case PARTS_CG:
		return 11;
	case PARTS_ANIMATION: return 12;
	case PARTS_TEXT: return 13;
	case PARTS_HGAUGE: return 14;
	case PARTS_VGAUGE: return 15;
	case PARTS_NUMERAL: return 16;
	case PARTS_RECT_DETECTION: return 17;
	case PARTS_CONSTRUCTION_PROCESS: return 18;
	case PARTS_FLAT: return 20;
	case PARTS_3DLAYER: return 21;
	case PARTS_MOVIE: return 22;
	case PARTS_FLASH:
		break;
	}
	VM_ERROR("unsupported component type %d", parts->states[state].type);
}

bool PE_SetPartsRectangleDetectionSize(int parts_no, int w, int h, int state)
{
	if (!parts_state_valid(--state))
		return false;
	struct parts *parts = parts_try_get(parts_no);
	if (!parts)
		return false;
	if (parts->states[state].type != PARTS_RECT_DETECTION)
		parts_state_reset(&parts->states[state], PARTS_RECT_DETECTION);
	parts_set_dims(parts, &parts->states[state].common, w, h);
	return true;
}

/* v14 button widget CGs: <base>／普通, ／オン, ／ダウン for the three states;
 * a disabled button shows <base>／無効 in all three (native rebuild 0x528870
 * -> 0x529280). The suffixes follow the base name's encoding (GBK ／ is
 * A3 AF, SJIS ／ is 81 5E). A missing disabled CG keeps the enabled CGs;
 * the native falls back to a generated panel there (0x528c80), which is
 * not implemented. */
static void parts_button_apply_cg(struct parts *parts)
{
	static const char *const gbk[] = {
		"\xa3\xaf\xc6\xd5\xcd\xa8", "\xa3\xaf\xa5\xaa\xa5\xf3",
		"\xa3\xaf\xa5\xc0\xa5\xa6\xa5\xf3", "\xa3\xaf\x9f\x6f\x84\xbf" };
	static const char *const sjis[] = {
		"\x81\x5e\x92\xca\x8f\xed", "\x81\x5e\x83\x49\x83\x93",
		"\x81\x5e\x83\x5f\x83\x45\x83\x93", "\x81\x5e\x96\xb3\x8c\xf8" };
	const char *base = parts->button_cg_name;
	if (!base)
		return;
	const char *const *suffix = strstr(base, "\xa3\xaf") ? gbk : sjis;
	char name[512];
	bool disabled = false;
	if (parts->button_disabled) {
		snprintf(name, sizeof(name), "%s%s", base, suffix[3]);
		disabled = asset_exists_by_name(ASSET_CG, name, NULL);
	}
	for (int st = 0; st < PARTS_NR_STATES; st++) {
		if (!disabled)
			snprintf(name, sizeof(name), "%s%s", base, suffix[st]);
		struct string *cg = cstr_to_string(name);
		PE_SetPartsCG(parts->no, cg, 0, st + 1);
		free_string(cg);
	}
}

void parts_button_set_cg_name(struct parts *parts, const char *base)
{
	free(parts->button_cg_name);
	parts->button_cg_name = base && base[0] ? xstrdup(base) : NULL;
	parts_button_apply_cg(parts);
}

/* Native 0x53d990: an existing parts becomes a button widget (SetComponentType
 * 0, state 1; no change for a button) and the call acts on that widget. */
static struct parts *parts_button_widget(int parts_no)
{
	struct parts *parts = parts_try_get(parts_no);
	if (parts && ain->version >= 14 && parts->component_type != 0)
		PE_SetComponentType(parts_no, 0, 1);
	return parts;
}

/* SetButtonEnable (case 212 -> 0x590b70): write the flag only when it
 * changes, then rebuild the button's CGs. A missing parts is ignored. */
void PE_SetButtonEnable(int parts_no, bool enable)
{
	struct parts *parts = parts_button_widget(parts_no);
	if (!parts || parts->button_disabled == !enable)
		return;
	parts->button_disabled = !enable;
	parts_button_apply_cg(parts);
}

/* IsButtonEnable (case 213 -> 0x590ba0): the flag; false without a parts. */
bool PE_IsButtonEnable(int parts_no)
{
	struct parts *parts = parts_button_widget(parts_no);
	return parts && !parts->button_disabled;
}

const char *parts_uc_data_get(struct parts *parts, const char *key)
{
	for (int i = 0; i + 1 < parts->nr_uc_data; i += 2) {
		if (!strcmp(parts->uc_data[i], key))
			return parts->uc_data[i + 1];
	}
	return NULL;
}

void parts_uc_data_set(struct parts *parts, const char *key, const char *value)
{
	for (int i = 0; i + 1 < parts->nr_uc_data; i += 2) {
		if (!strcmp(parts->uc_data[i], key)) {
			free(parts->uc_data[i + 1]);
			parts->uc_data[i + 1] = xstrdup(value);
			return;
		}
	}
	parts->uc_data = xrealloc_array(parts->uc_data, parts->nr_uc_data,
			parts->nr_uc_data + 2, sizeof(char*));
	parts->uc_data[parts->nr_uc_data++] = xstrdup(key);
	parts->uc_data[parts->nr_uc_data++] = xstrdup(value);
}

bool PE_SetPartsCGDetectionSize(int parts_no, struct string *cg_name, int state)
{
	UNIMPLEMENTED("(%d, %s, %d)", parts_no, display_sjis0(cg_name->text), state);
	return false;
}

int PE_GetFreeNumber(void)
{
	// NOTE: per GUIEngine.dll (Rance 01)
	static int first_free = 1000001000;
	while (PE_IsExist(first_free)) {
		first_free++;
	}
	// XXX: the ID is incremented even if the parts is not created
	return first_free++;
}

bool PE_IsExist(int parts_no)
{
	return !!ht_get_int(parts_table, parts_no, NULL);
}

void PE_SetSpeedupRateByMessageSkip(int parts_no, int rate)
{
	if (rate != 1)
		UNIMPLEMENTED("(%d, %d)");
}

static void ctrl_stack_init(void)
{
	memset(&ctrl_stack, 0, sizeof(ctrl_stack));
	// Add initial default controller
	PE_AddController(-1);
}

// Adds a new controller to the stack and makes it active. The `index`
// parameter specifies the position in the stack at which to insert the new
// controller; -1 means "insert directly after the currently active
// controller". In practice the game only ever passes -1, and the active
// controller is always the top of the stack at that point, so this
// degenerates to a simple push.
int PE_AddController(int index)
{
	if (index != -1)
		VM_ERROR("index != -1 not supported (got %d)", index);
	if (ctrl_stack.nr_controllers > 0 &&
			ctrl_stack.active != ctrl_stack.nr_controllers - 1)
		VM_ERROR("active controller is not at the top of the stack");
	if (ctrl_stack.nr_controllers >= PARTS_CONTROLLER_STACK_MAX)
		VM_ERROR("controller stack overflow");

	int no = ctrl_stack.nr_controllers++;
	ctrl_stack.active = no;
	return no;
}

// Removes the controller at position `index` from the stack, releases all
// parts belonging to it, and returns their parts numbers in
// `erase_number_list`. `index == -1` means "remove the currently active
// controller". In practice the game only ever passes -1, and the active
// controller is always the top of the stack at that point, so this
// degenerates to a simple pop.
void PE_RemoveController(struct page **erase_number_list, int index)
{
	// v14 games (Dohna Dohna's EraseLayer) remove controllers by explicit
	// stack position, not just the active top. Generalize: remove the
	// controller at `index` (-1 = active), shift the ones above it down.
	if (index == -1)
		index = ctrl_stack.active;
	if (index < 0 || index >= ctrl_stack.nr_controllers)
		VM_ERROR("invalid controller index %d (nr_controllers=%d)",
		         index, ctrl_stack.nr_controllers);

	// Collect and release parts belonging to this controller; renumber
	// parts owned by controllers above it.
	struct parts *p = TAILQ_FIRST(&parts_list);
	while (p) {
		struct parts *next = TAILQ_NEXT(p, parts_list_entry);
		if (p->controller_no == index) {
			// The list returns each erased parts' delegate index
			// (SetEventID), not its number: the original collects
			// parts +0x88 (0x53d500 -> 0x5386c0), the field SetEventID
			// writes (0x56d580), and EraseLayer hands the list to
			// CPartsMessageManager@ReleaseFunctionSetList, which fires
			// each parts' DeletedEvent and frees its function set.
			*erase_number_list = array_pushback(*erase_number_list,
					(union vm_value){.i = p->delegate_index}, AIN_ARRAY_INT, -1);
			parts_release(p->no);
		} else if (p->controller_no > index
				&& p->controller_no < PARTS_CONTROLLER_SYSTEM_OVERLAY) {
			p->controller_no--;
		}
		p = next;
	}

	ctrl_stack.nr_controllers--;
	if (ctrl_stack.nr_controllers == 0) {
		PE_AddController(-1);
	} else if (ctrl_stack.active == index) {
		ctrl_stack.active = ctrl_stack.nr_controllers - 1;
	} else if (ctrl_stack.active > index
			&& ctrl_stack.active != PARTS_CONTROLLER_SYSTEM_OVERLAY) {
		ctrl_stack.active--;
	}
}

void PE_set_active_controller(int controller_no)
{
	if (controller_no == PARTS_CONTROLLER_SYSTEM_OVERLAY ||
			(controller_no >= 0 && controller_no < ctrl_stack.nr_controllers))
		ctrl_stack.active = controller_no;
	else
		VM_ERROR("Invalid controller number: %d", controller_no);
}

int PE_get_active_controller(void)
{
	return ctrl_stack.active;
}

int PE_get_system_controller(void)
{
	return PARTS_CONTROLLER_SYSTEM_OVERLAY;
}

void PE_parts_set_want_save(int parts_no, bool want_save)
{
	parts_get(parts_no)->want_save = want_save;
}

float PE_parts_get_absolute_x(int parts_no)
{
	struct parts *parts = parts_try_get(parts_no);
	return parts ? (float)parts->global.pos.x : 0.0f;
}

float PE_parts_get_absolute_y(int parts_no)
{
	struct parts *parts = parts_try_get(parts_no);
	return parts ? (float)parts->global.pos.y : 0.0f;
}

int PE_parts_get_absolute_z(int parts_no)
{
	struct parts *parts = parts_try_get(parts_no);
	return parts ? parts->global.z : 0;
}

void PE_parts_set_lock_input_state(int parts_no, bool lock)
{
	parts_get(parts_no)->lock_input_state = lock;
}

bool PE_init_parts_movie(int parts_no, int width, int height, int bg_r, int bg_g, int bg_b, int state)
{
	if (!parts_state_valid(--state))
		return false;

	struct parts *parts = parts_get(parts_no);
	struct parts_movie *movie = parts_get_movie(parts, state);

	int sp_no = sact_SP_GetUnuseNum(0);
	struct sact_sprite *sp = sact_create_sprite(sp_no, width, height, bg_r, bg_g, bg_b, 255);
	if (!sp)
		return false;
	// The movie frames are composited by the parts engine via parts_render(),
	// so hide the bound sprite from the scene to avoid double-drawing.
	sprite_set_show(sp, false);

	struct texture *tex = sprite_get_texture(sp);
	movie->sprite_no = sp_no;
	movie->common.texture = *tex; // XXX: textures normally shouldn't be copied like this...
	parts_set_dims(parts, &movie->common, width, height);
	parts_dirty(parts);
	return true;
}

int PE_get_movie_sprite(int parts_no, int state)
{
	if (!parts_state_valid(--state))
		return -1;

	struct parts *parts = parts_try_get(parts_no);
	if (!parts || parts->states[state].type != PARTS_MOVIE)
		return -1;

	return parts->states[state].movie.sprite_no;
}

bool PE_CreateParts3DLayerPluginID(int parts_no, int state)
{
	if (!parts_state_valid(--state))
		return false;
	struct parts *parts = parts_get(parts_no);
	struct parts_3dlayer *l = parts_get_3dlayer(parts, state);

	if (l->plugin >= 0)
		return false;

	int handle = ReignEngine_create_plugin(RE_SEAL_PLUGIN);
	if (handle < 0)
		return false;

	int sp_no = sact_SP_GetUnuseNum(0);
	struct sact_sprite *sp = sact_create_sprite(sp_no, 1, 1, 0, 0, 0, 255);
	if (!sp) {
		ReignEngine_ReleasePlugin(handle);
		return false;
	}

	if (!ReignEngine_BindPlugin(handle, sp_no)) {
		sact_SP_Delete(sp_no);
		ReignEngine_ReleasePlugin(handle);
		return false;
	}
	// The 3D content is composited by the parts engine via parts_render(), so
	// hide the bound sprite from the scene to avoid double-drawing.
	sprite_set_show(sp, false);

	l->plugin = handle;
	l->sprite_no = sp_no;
	return true;
}

int PE_GetParts3DLayerPluginID(int parts_no, int state)
{
	if (!parts_state_valid(--state))
		return -1;
	struct parts *parts = parts_try_get(parts_no);
	if (!parts || parts->states[state].type != PARTS_3DLAYER)
		return -1;
	return parts->states[state].layer3d.plugin;
}

bool PE_ReleaseParts3DLayerPluginID(int parts_no, int state)
{
	if (!parts_state_valid(--state))
		return false;
	struct parts *parts = parts_try_get(parts_no);
	if (!parts || parts->states[state].type != PARTS_3DLAYER)
		return false;
	struct parts_3dlayer *l = &parts->states[state].layer3d;
	if (l->plugin < 0)
		return false;
	ReignEngine_ReleasePlugin(l->plugin);
	l->plugin = -1;
	if (l->sprite_no >= 0) {
		sact_SP_Delete(l->sprite_no);
		l->sprite_no = -1;
	}
	return true;
}
