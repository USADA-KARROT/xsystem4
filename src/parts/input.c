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

#include <limits.h>
#include "system4.h"

#include "asset_manager.h"
#include "audio.h"
#include "input.h"
#include "xsystem4.h"
#include "parts_internal.h"
#include "vm.h"

// the mouse position at last update
Point parts_prev_pos = {0};
// true between calls to BeginClick and EndClick
bool parts_began_click = false;

// true when the left mouse button was down last update
static bool prev_clicking = false;
// the last (fully) clicked parts number
static int clicked_parts = 0;
// true if any clickable part contained the cursor on mousedown
// Pending background-click signal for re-delivery across nested
// PE_UpdateInputState calls.  The outer call detects the release and
// sets global[2]=1, but WaitForClick resets global[2]=0 before the
// inner call runs.  This flag lets the inner call re-signal.
static bool background_click_pending = false;

static Rectangle parts_get_screen_hitbox(struct parts *parts)
{
	Rectangle hitbox = parts->states[PARTS_STATE_DEFAULT].common.hitbox;
	if (parts->parent) {
		hitbox.x += parts->parent->global.pos.x;
		hitbox.y += parts->parent->global.pos.y;
	}
	return hitbox;
}

static void parts_update_mouse(struct parts *parts, Point cur_pos, bool cur_clicking)
{
	Rectangle hitbox_screen = parts_get_screen_hitbox(parts);
	Rectangle *hitbox = &hitbox_screen;
	bool prev_in = SDL_PointInRect(&parts_prev_pos, hitbox);
	bool cur_in = SDL_PointInRect(&cur_pos, hitbox);

	if (parts->linked_from >= 0 && cur_in != prev_in) {
		parts_dirty(parts_get(parts->linked_from));
	}

	if (!parts_began_click || !parts->clickable || !parts->global.show || parts->global.alpha == 0)
		return;

	if (!cur_in) {
		parts_set_state(parts, PARTS_STATE_DEFAULT);
		return;
	}

	// PassCursor parts allow events to pass through — skip click tracking
	if (parts->pass_cursor)
		return;


	if (cur_clicking) {
		parts_set_state(parts, PARTS_STATE_HOVERED);
	} else {
		if (!prev_in && parts->on_cursor_sound >= 0) {
			audio_play_sound(parts->on_cursor_sound);
		}
		parts_set_state(parts, PARTS_STATE_HOVERED);
	}
}

void PE_UpdateInputState(possibly_unused int passed_time)
{
	// Re-deliver pending background click signal.
	if (background_click_pending && parts_began_click) {
		global_set(2, (union vm_value){.i = 1}, false);
		background_click_pending = false;
	}

	Point cur_pos;
	bool cur_clicking = key_is_down(VK_LBUTTON);
	mouse_get_pos(&cur_pos.x, &cur_pos.y);

	parts_list_ensure_sorted();
	struct parts *parts;
	PARTS_LIST_FOREACH(parts) {
		parts_update_mouse(parts, cur_pos, cur_clicking);
	}

	// Process click on mouse DOWN transition (not UP).
	// WaitForClick's bytecode key-check exits on mouse DOWN, calling
	// PE_EndInput before UP arrives. So per-button click detection
	// must happen on DOWN to set clicked_parts and enqueue messages
	// before WaitForClick can exit.
	if (cur_clicking && !prev_clicking) {
		WARNING("PE_UpdateInputState: DOWN detected at (%d,%d) began_click=%d", cur_pos.x, cur_pos.y, parts_began_click);
	}
	if (cur_clicking && !prev_clicking && parts_began_click) {
		struct parts *click_target = NULL;
		PARTS_LIST_FOREACH(parts) {
			if (parts->no >= 1000001000)
				continue;
			if (!parts->clickable || !parts->global.show || parts->global.alpha == 0)
				continue;
			if (parts->pass_cursor)
				continue;
			Rectangle hitbox = parts_get_screen_hitbox(parts);
			if (!SDL_PointInRect(&cur_pos, &hitbox))
				continue;
			click_target = parts;
		}
		if (click_target) {
			WARNING("PE_UpdateInputState: click_target=%d delegate_idx=%d", click_target->no, click_target->delegate_index);
			if (click_target->on_click_sound >= 0)
				audio_play_sound(click_target->on_click_sound);
			clicked_parts = click_target->no;

			int vars[3] = { cur_pos.x, cur_pos.y, 1 };
			// type 4 = MouseClick (CPartsFunctionSet event 4)
			// CPartsFunctionSet events: 0=Enter,1=Move,2=Leave,3=Wheel,4=Click,...
			parts_enqueue_message_vars(4, click_target->no,
				click_target->delegate_index,
				click_target->unique_id, 3, vars);
		} else {
			// Background click: no clickable part was hit.
			global_set(2, (union vm_value){.i = 1}, false);
			background_click_pending = true;
		}
	}

	prev_clicking = cur_clicking;
	parts_prev_pos = cur_pos;
}

void PE_SetClickable(int parts_no, bool clickable)
{
	parts_get(parts_no)->clickable = !!clickable;
}

bool PE_GetPartsClickable(int parts_no)
{
	return parts_get(parts_no)->clickable;
}

void PE_SetPassCursor(int parts_no, bool pass)
{
	parts_get(parts_no)->pass_cursor = !!pass;
}

bool PE_GetPartsPassCursor(int parts_no)
{
	return parts_get(parts_no)->pass_cursor;
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

void PE_BeginInput(void)
{
	parts_began_click = true;
}

void PE_EndInput(void)
{
	parts_began_click = false;
	clicked_parts = 0;
	background_click_pending = false;
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

	Rectangle hitbox = parts->states[state].common.hitbox;
	if (parts->parent) {
		hitbox.x += parts->parent->global.pos.x;
		hitbox.y += parts->parent->global.pos.y;
	}
	Point mouse_pos = { mouse_x, mouse_y };
	return SDL_PointInRect(&mouse_pos, &hitbox);
}
