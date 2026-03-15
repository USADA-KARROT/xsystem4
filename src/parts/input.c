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
// the current (partially) clicked parts number
static int click_down_parts = 0;

static void parts_update_mouse(struct parts *parts, Point cur_pos, bool cur_clicking)
{
	// Build screen-space hitbox: local hitbox + parent global offset
	Rectangle hitbox_local = parts->states[parts->state].common.hitbox;
	Rectangle hitbox_screen = hitbox_local;
	if (parts->parent) {
		hitbox_screen.x += parts->parent->global.pos.x;
		hitbox_screen.y += parts->parent->global.pos.y;
	}
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

	// click down: just remember the parts number for later
	if (cur_clicking && !prev_clicking) {
		click_down_parts = parts->no;
	}

	if (cur_clicking && click_down_parts == parts->no) {
		parts_set_state(parts, PARTS_STATE_CLICKED);
	} else {
		if (!prev_in && parts->on_cursor_sound >= 0) {
			audio_play_sound(parts->on_cursor_sound);
		}
		parts_set_state(parts, PARTS_STATE_HOVERED);
	}

	// click event: only if the click down event had same parts number
	if (prev_clicking && !cur_clicking && click_down_parts == parts->no) {
		if (parts->on_click_sound >= 0)
			audio_play_sound(parts->on_click_sound);
		clicked_parts = parts->no;
		parts_enqueue_message(4, parts->no,
			parts->delegate_index, parts->unique_id);
	}
}

void PE_UpdateInputState(possibly_unused int passed_time)
{
	Point cur_pos;
	bool cur_clicking = key_is_down(VK_LBUTTON);
	mouse_get_pos(&cur_pos.x, &cur_pos.y);


	struct parts *parts;
	PARTS_LIST_FOREACH(parts) {
		parts_update_mouse(parts, cur_pos, cur_clicking);
	}

	if (prev_clicking && !cur_clicking) {
		// Whole mouse left click event (type=4): fires on ANY left click
		// release during active input. CallFunctionMouseClick reads var[2]
		// as the button VK code: 1=VK_LBUTTON, 2=VK_RBUTTON, 4=VK_MBUTTON.
		if (parts_began_click) {
			int vars[3] = { cur_pos.x, cur_pos.y, 1 }; // VK_LBUTTON=1
			parts_enqueue_message_vars(4, 0, 0, 0, 3, vars);
		}

		if (!click_down_parts && parts_began_click) {
			// Background click: no clickable part was hit.
			// Signal WaitForClick to exit via global[2].
			global_set(2, (union vm_value){.i = 1}, false);
		}
		click_down_parts = 0;
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

void PE_SetPassCursor(possibly_unused int parts_no, possibly_unused bool pass)
{
	// PassCursor = cursor events pass through this part (not intercepted).
	// Currently a no-op; clickable flag handles click interception.
}

bool PE_GetPartsPassCursor(possibly_unused int parts_no)
{
	return false;
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
