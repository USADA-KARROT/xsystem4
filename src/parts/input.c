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

#include "system4.h"

#include "asset_manager.h"
#include "audio.h"
#include "input.h"
#include "xsystem4.h"
#include "parts_internal.h"
#include "system4/ain.h"
#include "vm.h"
#include "xsystem4.h"

// true between calls to BeginClick and EndClick
bool parts_began_click = false;
// v14 BeginInput/EndInput scopes can nest inside synchronous scene callbacks.
static unsigned v14_input_depth;

// the mouse position at last update
static Point parts_prev_pos = {0};
// true when the left mouse button was down last update
static bool prev_clicking = false;
// the last (fully) clicked parts number
static int clicked_parts = 0;
// the current (partially) clicked parts number
static int click_down_parts = 0;
static bool v14_background_click_pending = false;
void parts_enqueue_message_vars(int type, int parts_no, int delegate_index,
		int unique_id, int nr_vars, const int *vars);

// drag state
static struct parts *drag_parts = NULL;
static bool is_dragging = false;
static Point drag_initial_pos;
static Point drag_start_cursor;
static struct parts *drop_target = NULL;

/*
 * Invert parts_box_transform: translation, the ancestors' reverse flags,
 * rotation, scale, the parts' own reverse flags and origin. Returns the point
 * in the parts' box (pixels from its top-left corner), or false when the
 * scale is 0.
 */
bool parts_screen_to_box(struct parts *parts, struct parts_common *c, float sx, float sy,
		float *bx, float *by)
{
	float x = sx - parts->global.pos.x;
	float y = sy - parts->global.pos.y;
	if (parts->global.reverse_lr != parts->local.reverse_lr)
		x = -x;
	if (parts->global.reverse_tb != parts->local.reverse_tb)
		y = -y;
	if (parts->local.rotation.z != 0.0f) {
		float angle = parts->local.rotation.z * (3.14159265358979323846f / 180.0f);
		float cs = cosf(angle), sn = sinf(angle);
		float rx = cs * x + sn * y;
		y = -sn * x + cs * y;
		x = rx;
	}
	if (!parts->global.scale.x || !parts->global.scale.y)
		return false;
	x = x / parts->global.scale.x;
	y = y / parts->global.scale.y;
	if (parts->local.reverse_lr)
		x = -x;
	if (parts->local.reverse_tb)
		y = -y;
	*bx = x - c->origin_offset.x;
	*by = y - c->origin_offset.y;
	return true;
}

static bool parts_hittest(struct parts *parts, int state, Point pos)
{
	struct parts_common *c = &parts->states[state].common;
	Rectangle hitbox = parts_screen_hitbox(parts, c);
	if (!parts->pixel_hittest || !c->texture.handle || c->texture.w <= 0
			|| c->texture.h <= 0 || c->w <= 0 || c->h <= 0)
		return SDL_PointInRect(&pos, &hitbox);

	// Use the full image origin: surface_area clips pixels rather than
	// scaling the complete image into the smaller hit rectangle. A mirrored
	// box maps a pixel's left edge to the right edge of a texel, so sample
	// the pixel centre there, as the rasterizer does; unmirrored parts keep
	// the previous corner sampling (the same texel at scale 1).
	bool mirrored = parts->global.reverse_lr || parts->global.reverse_tb
		|| parts->local.reverse_lr || parts->local.reverse_tb;
	float half = mirrored ? 0.5f : 0.0f, x, y;
	if (!parts_screen_to_box(parts, c, (float)pos.x + half, (float)pos.y + half, &x, &y))
		return false;
	if (!isfinite(x) || !isfinite(y) || x < 0 || y < 0 || x >= c->w || y >= c->h)
		return false;
	int tx = (int)((double)x * c->texture.w / c->w);
	int ty = (int)((double)y * c->texture.h / c->h);
	if (parts->sprite_deform == 1)
		tx = c->texture.w - 1 - tx;
	else if (parts->sprite_deform == 2)
		ty = c->texture.h - 1 - ty;
	if (c->surface_area.w || c->surface_area.h) {
		Point texel = { tx, ty };
		if (!SDL_PointInRect(&texel, &c->surface_area))
			return false;
	}

	// A CG changes only through its setter, which invalidates this mask.
	// Other parts (gauges, animation, movies, construction) can mutate their
	// texture in place, so sample them directly instead of caching stale alpha.
	if (parts->states[state].type != PARTS_CG)
		return gfx_get_pixel(&c->texture, tx, ty).a != 0;
	if (!c->hit_mask) {
		uint8_t *pixels = gfx_get_pixels(&c->texture);
		if (!pixels)
			return true;
		size_t count = (size_t)c->texture.w * c->texture.h;
		c->hit_mask = xmalloc(count);
		for (size_t i = 0; i < count; i++)
			c->hit_mask[i] = pixels[i * 4 + 3];
		free(pixels);
	}
	return c->hit_mask[(size_t)ty * c->texture.w + tx] != 0;
}
static void drag_state_reset(void)
{
	drag_parts = NULL;
	is_dragging = false;
	drop_target = NULL;
}

void parts_input_reset_drag(struct parts *parts)
{
	if (parts == drag_parts)
		drag_state_reset();
	else if (parts == drop_target)
		drop_target = NULL;
}

/* A disabled v14 button has no cursor or click sound: native 0x5285b0 empties
 * the inner parts' sound names every frame while +0xb0 is false. */
static int parts_cursor_sound(struct parts *parts)
{
	return parts->button_disabled ? -1 : parts->on_cursor_sound;
}

static int parts_click_sound(struct parts *parts)
{
	return parts->button_disabled ? -1 : parts->on_click_sound;
}

static bool parts_takes_input(struct parts *parts)
{
	return parts->global.show && parts->global.alpha != 0;
}

/*
 * v14: the one parts under the cursor that takes the mouse, for the hover
 * and the click alike. The original's input update (0x546890) finds it with
 * 0x545e10 and the predicate 0x546e20 and keeps it both as the hovered
 * element (on a change the old one and the new one get the events 0x80f5a4
 * and 0x80f580, presumably MouseLeave and MouseEnter) and as the pressed
 * one: walking the list from its end (presumably front to back), the first
 * parts that is clickable (+0x1a4) or does not pass the cursor (+0x1a5,
 * オン指針透過 0), is shown and not fully transparent (+0x2f0 and +0x2ec,
 * presumably) and whose area contains the point (vtable +0x4c). A parts
 * that passes the cursor and is not clickable is skipped, so the pactex
 * decorations marked オン指針透過 1 (the title characters, the worker
 * card's texts and numbers, the portraits on the choice cards) block
 * neither the hover nor the click of the button behind them. (0x546e20 also
 * takes an element whose +0x1dc is not negative, presumably one in the
 * middle of a swipe; swipes are not implemented.) The area is always the
 * default state's: at a press the parts has already switched to its down
 * state, and a ＣＧ判定部件 detector (FooterButton) defines its area only in
 * the normal state (on-cursor and down are CG parts without a CG).
 */
static struct parts *v14_input_target(Point pos)
{
	struct parts *parts;
	PARTS_LIST_FOREACH_REVERSE(parts) {
		if (!parts->clickable && parts->pass_cursor)
			continue;
		if (!parts_takes_input(parts))
			continue;
		if (parts_hittest(parts, PARTS_STATE_DEFAULT, pos))
			return parts;
	}
	return NULL;
}

static void parts_update_mouse(struct parts *parts, Point cur_pos, bool cur_clicking,
		int passed_time, struct parts *v14_target, bool *hover_consumed,
		bool *click_consumed)
{
	// Always use DEFAULT state hitbox regardless of the current display state.
	// v14: only the input target is hovered (see v14_input_target).
	bool is_hovered = ain->version >= 14 ? parts == v14_target
		: parts_hittest(parts, PARTS_STATE_DEFAULT, cur_pos) && !*hover_consumed;

	bool was_hovered = parts->is_hovered;
	parts->is_hovered = is_hovered;

	// !pass_cursor parts consume the cursor for parts behind them
	if (is_hovered && !parts->pass_cursor)
		*hover_consumed = true;

	if (parts->linked_from >= 0 && is_hovered != was_hovered) {
		parts_dirty(parts_get(parts->linked_from));
	}

	if (!parts_began_click)
		return;

	if (is_hovered && !was_hovered) {
		parts_msg_push(parts, PARTS_MSG_MOUSE_ENTER, "ii", cur_pos.x, cur_pos.y);
		parts->hover_time = 0;
	}
	if (!is_hovered && was_hovered) {
		parts_msg_push(parts, PARTS_MSG_MOUSE_LEAVE, "ii", cur_pos.x, cur_pos.y);
	}

	if (is_hovered) {
		// MOUSE_ON message fires every frame while hovering
		parts_msg_push(parts, PARTS_MSG_MOUSE_ON,
				"iii", cur_pos.x, cur_pos.y, parts->hover_time);
		parts->hover_time += passed_time;

		// MOUSE_MOVE message fires when cursor moves while hovering
		if (cur_pos.x != parts_prev_pos.x || cur_pos.y != parts_prev_pos.y) {
			parts_msg_push(parts, PARTS_MSG_MOUSE_MOVE, "ii", cur_pos.x, cur_pos.y);
		}
	}

	if (!is_hovered) {
		parts_set_state(parts, PARTS_STATE_DEFAULT);
		return;
	}

	bool click_eligible = parts->clickable || !parts->pass_cursor;
	if (!click_eligible || *click_consumed) {
		if (!was_hovered)
			audio_play_sound(parts_cursor_sound(parts));
		parts_set_state(parts, PARTS_STATE_HOVERED);
		return;
	}

	// click down: first eligible part captures the click
	if (cur_clicking && !prev_clicking) {
		click_down_parts = parts->no;
		*click_consumed = true;

		drag_parts = parts;
		drag_initial_pos = parts->local.pos;
		drag_start_cursor = cur_pos;

		// KEY_TRIGGER message fires on press transition
		parts_msg_push(parts, PARTS_MSG_KEY_TRIGGER, "i", VK_LBUTTON);
	}

	// KEY_DOWN message fires every frame while held (not first frame)
	if (prev_clicking && cur_clicking && click_down_parts == parts->no) {
		parts_msg_push(parts, PARTS_MSG_KEY_DOWN, "i", VK_LBUTTON);
	}

	if (cur_clicking && click_down_parts == parts->no) {
		parts_set_state(parts, PARTS_STATE_CLICKED);
	} else {
		if (!was_hovered) {
			audio_play_sound(parts_cursor_sound(parts));
		}
		parts_set_state(parts, PARTS_STATE_HOVERED);
	}

	// click event: only if the click down event had same parts number.
	// v14 dispatches clicks on the DOWN transition (see PE_UpdateInputState);
	// the release-time path would set clicked_parts a second time after the
	// title's WaitForClick already consumed it, and the stale value made
	// every later WaitForClick exit immediately (dialogue auto-skipped).
	if (ain->version < 14 && parts->clickable && prev_clicking && !cur_clicking
			&& click_down_parts == parts->no) {
		audio_play_sound(parts_click_sound(parts));
		clicked_parts = parts->no;

		parts_msg_push(parts, PARTS_MSG_MOUSE_CLICK,
				"iii", cur_pos.x, cur_pos.y, VK_LBUTTON);
		parts_msg_push(parts, PARTS_MSG_KEY_UP, "i", VK_LBUTTON);
	}
}

void PE_UpdateInputState(int passed_time)
{
	Point cur_pos;
	bool cur_clicking = key_is_down(VK_LBUTTON);
	mouse_get_pos(&cur_pos.x, &cur_pos.y);

	bool hover_consumed = false;
	bool click_consumed = false;
	struct parts *parts;
	struct parts *v14_target = ain->version >= 14 ? v14_input_target(cur_pos) : NULL;
	// Iterate front-to-back (highest z first) for proper cursor consumption
	PARTS_LIST_FOREACH_REVERSE(parts) {
		parts_update_mouse(parts, cur_pos, cur_clicking, passed_time,
				v14_target, &hover_consumed, &click_consumed);
	}

	// Drag movement processing
	if (drag_parts && cur_clicking) {
		bool cursor_moved = (cur_pos.x != parts_prev_pos.x ||
				cur_pos.y != parts_prev_pos.y);
		if (cursor_moved && drag_parts->draggable) {
			Point new_pos = {
				drag_initial_pos.x + (cur_pos.x - drag_start_cursor.x),
				drag_initial_pos.y + (cur_pos.y - drag_start_cursor.y)
			};
			parts_set_pos(drag_parts, new_pos);
			if (!is_dragging) {
				parts_msg_push(drag_parts, PARTS_MSG_DRAG_BEGIN, "");
			}
			parts_msg_push(drag_parts, PARTS_MSG_DRAGGING, "iiii",
					drag_start_cursor.x, drag_start_cursor.y, cur_pos.x, cur_pos.y);
			is_dragging = true;
		}

		// Drop target tracking
		if (cursor_moved && is_dragging) {
			struct parts *new_drop = NULL;
			PARTS_LIST_FOREACH_REVERSE(parts) {
				if (parts == drag_parts)
					continue;
				if (parts_hittest(parts, PARTS_STATE_DEFAULT, cur_pos)) {
					new_drop = parts;
					if (!parts->pass_cursor)
						break;
				}
			}
			if (new_drop != drop_target) {
				if (drop_target) {
					parts_msg_push(drop_target, PARTS_MSG_DROP_LEAVE,
							"i", drag_parts->no);
				}
				if (new_drop) {
					parts_msg_push(new_drop, PARTS_MSG_DROP_ENTER,
							"i", drag_parts->no);
				}
				drop_target = new_drop;
			} else if (drop_target) {
				parts_msg_push(drop_target, PARTS_MSG_DROP_ON,
						"iii", drag_parts->no, cur_pos.x, cur_pos.y);
			}
		}
	}

	// Release handling
	if (prev_clicking && !cur_clicking) {
		if (is_dragging && drag_parts && drag_parts->draggable) {
			parts_msg_push(drag_parts, PARTS_MSG_DRAG_END, "");
		}
		if (drop_target && drag_parts) {
			parts_msg_push(drop_target, PARTS_MSG_DROPPED,
					"iii", drag_parts->no, cur_pos.x, cur_pos.y);
			parts_msg_push(drop_target, PARTS_MSG_DROP_LEAVE, "i", drag_parts->no);
		}
		drag_state_reset();

		if (!click_down_parts) {
			// TODO: play misclick sound
		}
		click_down_parts = 0;
	}

	// v14 (Dohna Dohna): click detection happens on the mouse DOWN
	// transition — WaitForClick's bytecode key-check exits on DOWN and
	// calls PE_EndInput before UP arrives, so release-time detection
	// never fires. Front-to-back hit test; a miss becomes a whole-screen
	// click message (parts_no=0) which drives scene navigation
	// (WholeMouseLClickEvent), plus the g_EndPartsBusyLoop global.
	//
	// A disabled button (SetButtonEnable false) takes the click and sends
	// nothing, neither its MouseClick nor the whole-screen click: the
	// original does not react to its greyed buttons (成员/商店 on the base
	// screen), while the AIN click handlers and SceneHome@Exit do not check
	// Enable. How the original drops the click is not known: the widget
	// flag +0xb0 is only read by the rebuild (0x528870/0x529280), the
	// per-frame sound reset (0x5285b0), a CG size lookup (0x529490), save
	// (0x526ab0) and the HLL getter/setter (0x590b70/0x590ba0).
	//
	// The target is the hover's (v14_input_target). A clickable one gets the
	// MouseClick; one that only blocks the cursor (a CG image, a panel,
	// オン指針透過 0) keeps the click from the parts behind it and the click
	// becomes the whole-screen click (presumed: what the original sends for
	// a pressed element that is not clickable was not traced). System parts
	// (1000001000 and up) are not skipped: InputDisabler's and the dialogs'
	// full-screen blockers are clickable rects that must stop a click
	// reaching the buttons behind them. (They used to be skipped because a
	// leaked InputDisabler, kept alive by a delegate, covered the screen for
	// good; delegates no longer own their objects, see
	// delegate_page_register.)
	if (ain->version >= 14 && cur_clicking && !prev_clicking && parts_began_click) {
		struct parts *click_target = NULL;
		if (getenv("XSYS4_STAGE2_TRACE")) {
			unsigned candidates = 0;
			NOTICE("S2 parts press ms=%u pos=%d,%d began=%d", SDL_GetTicks(), cur_pos.x, cur_pos.y, parts_began_click);
			PARTS_LIST_FOREACH_REVERSE(parts) {
				if (!parts->clickable || !parts->global.show || !parts->global.alpha || candidates++ >= 32)
					continue;
				Rectangle box = parts->states[PARTS_STATE_DEFAULT].common.hitbox;
				NOTICE("S2 candidate no=%d state=%d pass=%d pixel=%d hit=%d default_hit=%d box=%d,%d,%d,%d global=%d,%d scale=%.3f,%.3f parent=%d",
					parts->no, parts->state, parts->pass_cursor, parts->pixel_hittest,
					parts_hittest(parts, parts->state, cur_pos), parts_hittest(parts, PARTS_STATE_DEFAULT, cur_pos),
					box.x, box.y, box.w, box.h, parts->global.pos.x, parts->global.pos.y,
					parts->global.scale.x, parts->global.scale.y, parts->parent ? parts->parent->no : 0);
			}
		}
		if (v14_target && v14_target->clickable)
			click_target = v14_target;
		int vars[3] = { cur_pos.x, cur_pos.y, 1 };
		if (getenv("XSYS4_STAGE2_TRACE"))
			NOTICE("S2 click target=%d%s blocked_by=%d", click_target ? click_target->no : 0,
				click_target && click_target->button_disabled ? " (disabled)" : "",
				v14_target && !click_target ? v14_target->no : 0);
		if (click_target && click_target->button_disabled) {
			// swallowed (see above)
		} else if (click_target) {
			if (parts_click_sound(click_target) >= 0)
				audio_play_sound(parts_click_sound(click_target));
			clicked_parts = click_target->no;
			// type 4 = MouseClick (CallEvent3: x,y,keyCode) in
			// CPartsMessageManager's SWITCH (bytecode-verified; type 5
			// is DoubleClick/CallEvent0 and dispatches an empty slot)
			parts_enqueue_message_vars(4, click_target->no,
					click_target->delegate_index,
					click_target->unique_id, 3, vars);
		} else {
			parts_enqueue_message_vars(4, 0, -1, -1, 3, vars);
			global_set(2, (union vm_value){.i = 1}, false);
			v14_background_click_pending = true;
		}
	}
	// Re-fire a pending background click once a new input frame begins
	// (the game may re-enter WaitForClick after consuming the first).
	if (ain->version >= 14 && v14_background_click_pending && parts_began_click
			&& !cur_clicking && !prev_clicking) {
		v14_background_click_pending = false;
	}

	prev_clicking = cur_clicking;
	parts_prev_pos = cur_pos;
}

void PE_SetPassCursor(int parts_no, bool pass)
{
	parts_get(parts_no)->pass_cursor = !!pass;
}

bool PE_GetPartsPassCursor(int parts_no)
{
	return parts_get(parts_no)->pass_cursor;
}

void PE_SetClickable(int parts_no, bool clickable)
{
	parts_get(parts_no)->clickable = !!clickable;
}

bool PE_GetPartsClickable(int parts_no)
{
	return parts_get(parts_no)->clickable;
}

void PE_SetPartsGroupDecideOnCursor(possibly_unused int group_no, possibly_unused bool decide_on_cursor)
{
	UNIMPLEMENTED("(%d, %s)", group_no, decide_on_cursor ? "true" : "false");
}

void PE_SetPartsGroupDecideClick(possibly_unused int group_no, possibly_unused bool decide_click)
{
	UNIMPLEMENTED("(%d, %s)", group_no, decide_click ? "true" : "false");
}

void PE_SetOnCursorShowLinkPartsNumber(int parts_no, int link_parts_no)
{
	struct parts *parts = parts_get(parts_no);
	struct parts *link_parts = parts_get(link_parts_no);
	parts->linked_to = link_parts_no;
	link_parts->linked_from = parts_no;
}

int PE_GetOnCursorShowLinkPartsNumber(int parts_no)
{
	return parts_get(parts_no)->linked_to;
}

bool PE_SetPartsOnCursorSoundNumber(int parts_no, int sound_no)
{
	if (!asset_exists(ASSET_SOUND, sound_no)) {
		WARNING("Invalid sound number: %d", sound_no);
		return false;
	}

	struct parts *parts = parts_get(parts_no);
	parts->on_cursor_sound = sound_no;
	return true;
}

bool PE_SetPartsClickSoundNumber(int parts_no, int sound_no)
{
	if (!asset_exists(ASSET_SOUND, sound_no)) {
		WARNING("Invalid sound number: %d", sound_no);
		return false;
	}

	struct parts *parts = parts_get(parts_no);
	parts->on_click_sound = sound_no;
	return true;
}

bool PE_SetClickMissSoundNumber(possibly_unused int sound_no)
{
	UNIMPLEMENTED("(%d)", sound_no);
	return true;
}

static void v14_reset_input_session(bool begin)
{
	// Native 0x53e830/0x53e8b0 reset the input targets, not the message
	// queue. Beginning (0x546030) samples held buttons so returning from a
	// child scene cannot turn its closing press into another DOWN.
	struct parts *parts;
	PARTS_LIST_FOREACH(parts) {
		if (parts->is_hovered || parts->no == click_down_parts) {
			parts->is_hovered = false;
			parts_dirty(parts);
			parts_set_state(parts, PARTS_STATE_DEFAULT);
		}
		parts->hover_time = 0;
	}
	clicked_parts = 0;
	click_down_parts = 0;
	v14_background_click_pending = false;
	drag_state_reset();
	parts_prev_pos = (Point){0};
	prev_clicking = begin && key_is_down(VK_LBUTTON);
	parts_began_click = begin;
}

void parts_reset_input(void)
{
	if (ain->version >= 14) {
		v14_input_depth = 0;
		v14_reset_input_session(false);
	}
}

void PE_BeginInput(void)
{
	if (ain->version >= 14) {
		// Native 0x58a720: reset/restart the session, then increment depth.
		v14_reset_input_session(true);
		v14_input_depth++;
		return;
	}
	parts_began_click = true;
}

void PE_EndInput(void)
{
	if (ain->version >= 14) {
		// Native 0x58a750: end the child, resume a remaining outer scope,
		// and clamp an unmatched EndInput at zero.
		if (v14_input_depth)
			v14_input_depth--;
		v14_reset_input_session(v14_input_depth > 0);
		return;
	}
	parts_began_click = false;
	v14_background_click_pending = false;
	clicked_parts = 0;
	drag_state_reset();
}

void PE_SetDrag(int parts_no, bool enable)
{
	parts_get(parts_no)->draggable = !!enable;
}

int PE_GetClickPartsNumber(void)
{
	return clicked_parts;
}

bool PE_IsCursorIn(int parts_no, int mouse_x, int mouse_y, int state)
{
	if (!parts_state_valid(--state))
		return false;

	struct parts *parts = parts_try_get(parts_no);
	if (!parts)
		return false;

	Point mouse_pos = { mouse_x, mouse_y };
	return parts_hittest(parts, state, mouse_pos);
}
