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
#include <math.h>
#include <cglm/cglm.h>

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
// v14: the parts under the cursor when the left button went down within the
// scope, the one its release clicks: the original's left slot (+0x1ec),
// which holds it from the press until the update of the release; see
// v14_update_input
static int v14_press_parts;
// v14: the parts under the cursor at the last left press within the scope,
// standing in for the original's focus (+0x1b4 of the element, set by
// 0x546890 at a press only). It is not one of the manager's slots: it stays
// after the release and after the end of a scope, until a press elsewhere
// or a BeginInput (LostFocus); GetActiveParts reports it within a scope
// (PE_GetActivePartsNumber)
static int v14_active_parts;
void parts_enqueue_message_vars(int type, int parts_no, int delegate_index,
		int unique_id, int nr_vars, const int *vars);

// drag state
static struct parts *drag_parts = NULL;
static bool is_dragging = false;
static Point drag_initial_pos;
static Point drag_start_cursor;
static struct parts *drop_target = NULL;

/*
 * Invert parts_box_transform (the parts' accumulated matrix, which now
 * carries the ancestors' rotation and scale as well, and the origin offset).
 * Returns the point in the parts' box (pixels from its top-left corner), or
 * false when the transform is singular (a scale of 0).
 *
 * Before v14 it is taken apart step by step as it was: translation, the
 * ancestors' reverse flags, rotation, scale, the parts' own reverse flags
 * and origin (parts_anchor_transform's earlier form).
 */
bool parts_screen_to_box(struct parts *parts, struct parts_common *c, float sx, float sy,
		float *bx, float *by)
{
	if (ain->version < 14) {
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
	mat4 m;
	parts_box_transform(parts, c, m);
	if (!parts_matrix_is_flat(m)) {
		// With depth (an X or Y rotation along the chain) the box reaches
		// the screen through the camera (parts_perspective): the plane of
		// the box maps to the screen by the homography H made of the
		// columns x, y and w of persp * m, and a screen point goes back by
		// its inverse.
		mat4 persp, f;
		parts_perspective(persp);
		glm_mat4_mul(persp, m, f);
		mat3 h = {
			{ f[0][0], f[0][1], f[0][3] },
			{ f[1][0], f[1][1], f[1][3] },
			{ f[3][0], f[3][1], f[3][3] },
		};
		if (!isfinite(glm_mat3_det(h)) || glm_mat3_det(h) == 0.0f)
			return false;
		mat3 inv;
		glm_mat3_inv(h, inv);
		vec3 q;
		glm_mat3_mulv(inv, (vec3){ sx, sy, 1.0f }, q);
		// q is (u, v, 1) / w: a point behind the eye (w <= 0) is none of
		// the box's.
		if (!isfinite(q[2]) || !(q[2] > 0.0f))
			return false;
		*bx = q[0] / q[2];
		*by = q[1] / q[2];
		return true;
	}
	// The 2D affine part: screen = (a c; b d) * box + (tx, ty).
	float a = m[0][0], b = m[0][1], cc = m[1][0], d = m[1][1];
	float det = a * d - b * cc;
	if (!isfinite(det) || det == 0.0f)
		return false;
	float x = sx - m[3][0];
	float y = sy - m[3][1];
	*bx = (d * x - cc * y) / det;
	*by = (a * y - b * x) / det;
	return true;
}

/*
 * The original's point-in-triangle test (0x57aa60): the point is on the
 * centroid's side of each edge, or on the edge. Three equal points hold
 * nothing; three points on one line hold every point (each side value of
 * the centroid is 0).
 */
static bool parts_point_in_triangle(const float p1[2], const float p2[2], const float p3[2],
		float mx, float my)
{
	if (p1[0] == p2[0] && p1[1] == p2[1] && p1[0] == p3[0] && p1[1] == p3[1])
		return false;
	float gx = (p2[0] + p1[0] + p3[0]) * (1.0f / 3.0f);
	float gy = (p2[1] + p1[1] + p3[1]) * (1.0f / 3.0f);
	float e = p1[1] - p2[1];
	if (((p1[0] - gx) * e + (gy - p1[1]) * (p1[0] - p2[0]))
			* ((p1[0] - mx) * e + (my - p1[1]) * (p1[0] - p2[0])) < 0.0f)
		return false;
	e = p2[1] - p3[1];
	if (((gy - p2[1]) * (p2[0] - p3[0]) + (p2[0] - gx) * e)
			* ((my - p2[1]) * (p2[0] - p3[0]) + (p2[0] - mx) * e) < 0.0f)
		return false;
	e = p3[1] - p1[1];
	return ((gy - p3[1]) * (p3[0] - p1[0]) + (p3[0] - gx) * e)
		* ((my - p3[1]) * (p3[0] - p1[0]) + (p3[0] - mx) * e) >= 0.0f;
}

/*
 * The rectangle hit test of a box with depth (an X or Y rotation along the
 * chain): the four corners of the box through its matrix and the camera
 * used for drawing, and the point in the triangle of the first three
 * corners or in the one of the last three (top-left, top-right,
 * bottom-left; bottom-left, top-right, bottom-right), with the triangle
 * test of 0x57aa60. This is the port's choice for every kind of parts. The
 * original differs by kind: text (21) and hit-rectangle (25) parts go
 * through 0x57a6b0 -> 0x5c3910, which projects without the drawing's
 * centre shift, so their vanishing point is pixel (0,0); CG parts (19)
 * take the bounding rectangle of the stored drawn corners (0x59c460 ->
 * 0x5b11d0 -> 0x5b13d0). The game has no clickable parts that turn, so
 * the difference was not seen. Boxes without depth keep the upright box at
 * the anchor (parts_screen_hitbox).
 */
static bool parts_projected_box_hit(struct parts *parts, struct parts_common *c, mat4 m, Point pos)
{
	if (c->w <= 0 || c->h <= 0)
		return false;
	// The box of the hit rectangle in the box frame of parts_box_transform
	// (the rectangle may be set apart from the texture's size).
	float x0 = c->hitbox.x - parts->local.pos.x - c->origin_offset.x;
	float y0 = c->hitbox.y - parts->local.pos.y - c->origin_offset.y;
	float x1 = x0 + c->hitbox.w, y1 = y0 + c->hitbox.h;
	const float corner_box[4][2] = { { x0, y0 }, { x1, y0 }, { x0, y1 }, { x1, y1 } };
	float corner[4][2];
	for (int i = 0; i < 4; i++) {
		vec4 p;
		glm_mat4_mulv(m, (vec4){ corner_box[i][0], corner_box[i][1], 0.0f, 1.0f }, p);
		parts_project_point(p, &corner[i][0], &corner[i][1]);
	}
	return parts_point_in_triangle(corner[0], corner[1], corner[2], pos.x, pos.y)
		|| parts_point_in_triangle(corner[2], corner[1], corner[3], pos.x, pos.y);
}

static bool parts_hittest(struct parts *parts, int state, Point pos)
{
	// v14: a layout box is never under the cursor (its widget's hit test,
	// vtable +0x4c = 0x49c5b0, returns false); its state now has the box's
	// size (layoutbox.c).
	if (ain->version >= 14 && parts->states[0].type == PARTS_LAYOUT_BOX)
		return false;
	struct parts_common *c = &parts->states[state].common;
	bool use_pixels = parts->pixel_hittest && c->texture.handle && c->texture.w > 0
		&& c->texture.h > 0 && c->w > 0 && c->h > 0;
	if (ain->version >= 14) {
		mat4 m;
		parts_box_transform(parts, c, m);
		if (!parts_matrix_is_flat(m) && !parts_projected_box_hit(parts, c, m, pos))
			return false;
		if (!parts_matrix_is_flat(m) && !use_pixels)
			return true;
	}
	Rectangle hitbox = parts_screen_hitbox(parts, c);
	if (!use_pixels)
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
	const int deform = parts_cg_deform(parts, state);
	if (deform == 1)
		tx = c->texture.w - 1 - tx;
	else if (deform == 2)
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

/* The parts is being released: the input forgets it (v14: the original's
 * 0x53c140 drops a released parts from the scope's active parts). */
void parts_input_release(struct parts *parts)
{
	if (parts == drag_parts)
		drag_state_reset();
	else if (parts == drop_target)
		drop_target = NULL;
	// Programs before v14 keep a press on a released parts as it was.
	if (ain->version < 14)
		return;
	if (parts->no == click_down_parts)
		click_down_parts = 0;
	if (parts->no == v14_press_parts)
		v14_press_parts = 0;
	if (parts->no == v14_active_parts)
		v14_active_parts = 0;
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
 * v14: the one parts under the cursor that takes the mouse at a press. The
 * original's input update (0x546890) finds it at a press only, with
 * 0x545e10 and the predicate 0x546e20, and keeps it both as the focus (on a
 * change the old one gets LostFocus and the new one Focus, the events
 * 0x80f5a4 and 0x80f580 of message types 20 and 19) and as the pressed
 * one: walking the list from its end (presumably front to back), the first
 * parts that is clickable (+0x1a4) or does not pass the cursor (+0x1a5,
 * オン指針透過 0), is shown and not fully transparent (+0x2f0 and +0x2ec,
 * presumably) and whose area contains the point (vtable +0x4c). A parts
 * that passes the cursor and is not clickable is skipped, so the pactex
 * decorations marked オン指針透過 1 (the title characters, the worker
 * card's texts and numbers, the portraits on the choice cards) block
 * neither the hover nor the click of the button behind them. The hover is
 * the cursor walk's (v14_cursor_walk), which enters this parts and every
 * parts in front of it. (0x546e20 also
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
		// A scroll bar stands for its knob and its track, the original's
		// inner parts, which the bar's own flags do not decide alone: the
		// track can always be clicked (scrollbar.c).
		if (parts_scrollbar_has_look(parts)) {
			if (parts_takes_input(parts)
					&& parts_scrollbar_hittest(parts, pos) != PARTS_SCROLLBAR_MISS)
				return parts;
			continue;
		}
		if (!parts->clickable && parts->pass_cursor)
			continue;
		if (!parts_takes_input(parts))
			continue;
		if (parts_hittest(parts, PARTS_STATE_DEFAULT, pos))
			return parts;
	}
	return NULL;
}

/* Programs before v14: the hover and the click of one parts (v14 has
 * v14_update_input). */
static void parts_update_mouse(struct parts *parts, Point cur_pos, bool cur_clicking,
		int passed_time, bool *hover_consumed, bool *click_consumed)
{
	// Always use DEFAULT state hitbox regardless of the current display state.
	bool is_hovered = parts_hittest(parts, PARTS_STATE_DEFAULT, cur_pos) && !*hover_consumed;

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
	if (parts->clickable && prev_clicking && !cur_clicking
			&& click_down_parts == parts->no) {
		audio_play_sound(parts_click_sound(parts));
		clicked_parts = parts->no;

		parts_msg_push(parts, PARTS_MSG_MOUSE_CLICK,
				"iii", cur_pos.x, cur_pos.y, VK_LBUTTON);
		parts_msg_push(parts, PARTS_MSG_KEY_UP, "i", VK_LBUTTON);
	}
}

/*
 * v14 (Dohna Dohna): the original's input update (0x547c90, run by the parts
 * update 0x53c5b0 alone; the update does nothing outside a
 * BeginInput/EndInput scope, 0x547cba). In its order:
 *
 *  1. Every element's time since entered grows by the passed time
 *     (0x546620 at 0x547df2, whether the element is entered or not), and
 *     GetClickNumber is cleared (+0x14 at 0x547e23).
 *  2. The cursor's position is read through the same call as the HLL
 *     Mouse_GetPos (0x547e50; 0x498b95). When it gives none, the update goes
 *     on with the last position it stored (+0x18/+0x1c, 0x547e5f) and sends
 *     no mouse message: no whole-screen message (0x548024), no press, no
 *     cursor walk (0x5482a7 skips 0x546890); only the left slot's parts is
 *     left (0x548339 -> 0x5476b0 -> 0x577cf0) when it is the focus, that is
 *     while the left button is still held on the parts it went down on. The
 *     slot is empty after the release (5. below), and a release without a
 *     position empties it before (0x5481cb -> 0x5462b0), so a parts clicked
 *     or released is not left here: it stays entered, as the other parts do,
 *     until the pointer is back. Here the position counts as given while
 *     the pointer is in the window (mouse_in_window: the scheduled pointer,
 *     or SDL's mouse focus). What the original's call checks was not
 *     followed (it is behind an interface); the port's Mouse_GetPos also asks
 *     for the keyboard focus, which is not asked here, so that a window in
 *     the background shows the hover as before.
 *  3. With a position, when it is not the one stored: the whole-screen
 *     MouseMove (x, y), through the manager's own emitter (0x548054; its
 *     source is null, so the message's parts number is 0, and its mask
 *     0x54562a leaves type 1 through: of the types 0..29 it lets 1, 3, 4, 5,
 *     9 and 15..18 through). The game hooks it only in its debugging tools
 *     (AFL_Parts_AddWholeMouseMoveEvent).
 *  4. With a position, a release: the whole-screen MouseClick (x, y, key 1),
 *     whatever is under the cursor (0x548064, the same emitter). Also when the
 *     button was already down at BeginInput: 0x546030 samples the buttons
 *     into +0x40..+0x42, and EndInput samples them again for the outer scope
 *     (0x58a750 -> 0x53e830).
 *  5. With a position, 0x546890 (at 0x5482f9): at a press (down now, up at
 *     the last update; 0x547f2d..0x547fb3) the parts under the cursor,
 *     v14_input_target, becomes the pressed parts (the left slot) and the
 *     focus; when the focus changes, the old one gets LostFocus and the new
 *     one Focus (0x546a4e..0x546aa2: 0x80f5a4, 0x80f580, each only when the
 *     element's +0x1b4 says so). Without a press and with the left button
 *     up, the update of the release included, the left slot is emptied
 *     (0x5468d6..0x5468fb -> 0x547460: +0x1ec and the drop target +0x210),
 *     before the click of 6. goes out. Then the cursor walk
 *     (v14_cursor_walk), every update.
 *  6. The focused element's keys (0x578580 at 0x54836a): at the press a
 *     clickable entered element goes to its down state (0x5785f8); at the
 *     release the pressed parts' own MouseClick (0x5788d0, key 1), only when
 *     it is clickable (+0x1a4), entered (+0x1ac, this update's walk) and the
 *     position is given (0x578955; without one an entered parts keeps its
 *     state); the
 *     click sound (0x5789bc: the state 3 sound, as Parts_PlaySound(n, 3))
 *     and GetClickNumber come with it. GetClickNumber follows the left
 *     button's click of a parts and nothing else: its source is the element's
 *     +5, written at 0x5789cb in the key 1 branch alone, which 0x547610 looks
 *     up (0x545d40) and stores at +0x14. A parts the cursor left gets nothing
 *     (0x5789dd); dragged back, it is clicked. A button already down at
 *     BeginInput is not a press (and BeginInput clears the focus, 0x53e830 ->
 *     0x53c750), so no parts is clicked and only the whole-screen click goes
 *     out. A pactex-clickable parts without an event gets its MouseClick as
 *     well, which the AIN drops (CPartsMessageManager@CallDelegate); the
 *     whole-screen click is what closes the dialog over PartySkillView's
 *     Button. A check box flips here, after its MouseClick (0x52cdd0 ->
 *     0x52f030: the cursor still on the widget, key 1).
 *  7. The position is stored (+0x18/+0x1c) when this update and the last one
 *     both had one (0x548381..0x548395; +0x43).
 *
 * The original has no "pressed parts" for a click. It sends the parts'
 * click to the focused element (0x548367 calls 0x578580 on it), which finds
 * the released keys in the element's own key table (0x574d50 at 0x57861f)
 * and runs 0x5788d0 for each. Its per-button slots (+0x1ec left, +0x200
 * middle, +0x208 right; 0x546890 empties the three at a press and fills
 * one, in that priority: 0x546988, 0x5469db, 0x5469fe) are the sources of
 * a drag and drop. v14_press_parts stands for "the focus, with the left
 * button gone down on it", the left slot, the same parts as long as only
 * the left button is used, emptied in the update of the release;
 * v14_active_parts for the focus, which stays after the release. Only the
 * slots are reset at the end of a scope (v14_reset_input_session), so a
 * parts just clicked keeps its entered flag and its state there.
 *
 * The right and the middle button are not done: here their press and their
 * release do nothing to the parts' input, as before the left button was
 * reworked. In the original
 *  - a release sends the whole-screen click with key 2 or 4 (0x5480be,
 *    0x5480fb) and the focused element's MouseClick with that key (0x5788d0
 *    from 0x5789e8: clickable and the cursor on it), with no sound and
 *    without GetClickNumber (that branch does not write +5);
 *  - a press of any of the three moves the focus (0x546a1f..0x546aa2), with
 *    its Focus and LostFocus; here only a left press does;
 *  - a right or middle press with a parts in the left slot calls 0x5462b0
 *    (0x54816d), which cancels the drag and drop: it puts a draggable
 *    parts back where it was (0x5462d8), sends the drag's events (0x5781b0,
 *    and 0x80ffa8 for an element being dragged, +0x1e0), leaves the drop
 *    target (0x5784b0), empties the slot and sets +0x1fc (0x5463ad). While
 *    +0x1fc is set, until all three buttons are up (0x548206), the update
 *    skips 0x546890 (0x548278), so no new press is taken and no cursor walk
 *    runs. 0x5462b0 writes neither the focus nor the element's key table,
 *    so, read statically, the left button's click of the focused parts
 *    still goes out (not compared with the original running).
 * Doing them right takes the focus and the key table as the model and a
 * comparison with the original; that is left to a later round.
 *
 * A disabled button (SetButtonEnable false) gets no MouseClick, no
 * GetClickNumber and no sound here. This is the port's expedient and has no
 * counterpart in the original's input update: the button's per-frame update
 * makes its inner parts clickable whatever the enable flag (0x528690, ahead
 * of the read of +0xb0 at 0x5286df), and a disabled button only has its two
 * sound names emptied (the branch at 0x528787), so 0x5788d0 still sends
 * that parts its MouseClick and sets its +5. The original does not react to
 * its greyed buttons (成员/商店 on the base screen) while the AIN click
 * handlers do not check Enable, so the click is presumably stopped in
 * another layer; which one was not looked up. The whole-screen click is
 * sent as for any release. The hover of a disabled button is sent as for
 * any parts (0x577b90 does not ask).
 *
 * Not done: the whole-screen wheel (0x80f6e4) and the element's own
 * (0x547000), the KeyTrigger/KeyDown/KeyPress/KeyUp messages (types 15-18,
 * 0x578580), the double click (0x4e6bd0), the drag and drop messages and
 * the swipe (an element in a swipe, +0x1dc not negative, stops the walk and
 * keeps its focus out of it: 0x5472e9, 0x548294).
 */
static bool v14_prev_have_pos;

static void v14_send(struct parts *parts, int type, int nr_vars, const int *vars)
{
	parts_enqueue_message_vars(type, parts->no, parts->delegate_index, parts->unique_id,
			nr_vars, vars);
}

// Trace (XSYS4_STAGE2_TRACE): what the walk entered and left.
static void v14_trace_hover(const char *what, struct parts *parts, Point pos)
{
	static unsigned traced;
	if (input_stage2_trace() && traced < 5000) {
		traced++;
		NOTICE("S2 %s ms=%u pos=%d,%d parts=%d", what, SDL_GetTicks(), pos.x, pos.y, parts->no);
	}
}

static void v14_hover_changed(struct parts *parts)
{
	struct parts *link = parts->linked_from >= 0 ? parts_try_get(parts->linked_from) : NULL;
	if (link)
		parts_dirty(link);
}

/* 0x577b90: the walk entered the element, or it stays entered. */
static void v14_enter(struct parts *parts, Point pos, bool cur_clicking)
{
	int vars[3] = { pos.x, pos.y, 0 };
	if (!parts->is_hovered) {
		v14_send(parts, PARTS_V14_MSG_MOUSE_ENTER, 2, vars);
		// 0x564f20, the cursor sound (Parts_PlaySound(n, 2)). The
		// names of the sounds are not read yet (G4); nothing is played
		// without a number.
		if (parts_cursor_sound(parts) >= 0)
			audio_play_sound(parts_cursor_sound(parts));
		parts->hover_time = 0;
		v14_trace_hover("enter", parts, pos);
	}
	// +4: the left button went down on it, and it was clickable then
	// (0x578580); click_down_parts is that parts while the button is held.
	bool down = cur_clicking && click_down_parts == parts->no && parts->clickable;
	parts_set_state(parts, down ? PARTS_STATE_CLICKED : PARTS_STATE_HOVERED);
	vars[2] = parts->hover_time;
	v14_send(parts, PARTS_V14_MSG_ON_CURSOR, 3, vars);
	if (pos.x != parts->hover_pos.x || pos.y != parts->hover_pos.y)
		v14_send(parts, PARTS_V14_MSG_MOUSE_MOVE, 2, vars);
	parts->hover_pos = pos;
	if (!parts->is_hovered) {
		parts->is_hovered = true;
		v14_hover_changed(parts);
	}
}

/* 0x577cf0: the element is not entered (or no longer). */
static void v14_leave(struct parts *parts, Point pos)
{
	if (parts->is_hovered) {
		int vars[2] = { pos.x, pos.y };
		v14_send(parts, PARTS_V14_MSG_MOUSE_LEAVE, 2, vars);
		v14_trace_hover("leave", parts, pos);
		parts->is_hovered = false;
		v14_hover_changed(parts);
	}
	parts_set_state(parts, PARTS_STATE_DEFAULT);
}

/*
 * v14: the cursor walk at the end of 0x546890 (0x5471d0, called at 0x546dc7
 * with the enter functor 0x80f7a0 -> 0x548cf0 -> 0x577b90 and the leave
 * functor 0x80f6c8 -> 0x548bc0 -> 0x577cf0). It goes through the list from
 * its end, front to back: an element is entered when nothing before it
 * stopped the walk, its parts is shown (+0x2f0) and not fully transparent
 * (the alpha +0x2ec as a float above 0.0, [0x813018]) and its area contains
 * the point (vtable +0x4c, the default state's); after an entered element
 * that does not pass the cursor (+0x1a5 0) the walk stops. Every element
 * not entered is left: those behind the one that stopped the walk and those
 * before it the cursor is not on. Unlike the input target
 * (v14_input_target) the walk does not ask whether a parts is clickable: a
 * clickable parts that passes the cursor is entered and the walk goes on
 * behind it. So the input target is always entered (every parts in front
 * of it passes the cursor), and the decorations marked オン指針透過 1 in
 * front of a button are entered with it.
 *
 * Entering (0x577b90), every update while the element is entered:
 *  - the first time (+0x1ac 0): MouseEnter (x, y), the cursor sound
 *    (0x564f20) and the time since entered set to 0 (+0x1b0);
 *  - the down state when the left button went down on it (+4, set by
 *    0x578580 for a clickable entered element at the press; 3), the
 *    on-cursor state otherwise (2; vtable +0x34);
 *  - OnCursor (x, y, the time since entered);
 *  - MouseMove (x, y) when the cursor is not where the element last saw it
 *    (+0x1b8/+0x1bc), which is then the cursor's position; so also when it
 *    is entered again elsewhere.
 * Leaving (0x577cf0), every update while the element is not entered: a
 * MouseLeave (x, y) when it was entered, and the default state (1). The
 * original skips both while the element is a drop target (+0x1e3); drag and
 * drop is not sent in v14 here.
 *
 * The messages go to the element's own emitter (+0x1e4), so they carry the
 * parts' number, delegate index and event ID. The coordinates go through
 * 0x696e00, the window's scale, which mouse_get_pos has already applied.
 *
 * A scroll bar stands for its knob and its track (the original's inner
 * elements), as in v14_input_target: entered when one of them is under the
 * cursor, and it stops the walk.
 */
static void v14_cursor_walk(Point pos, bool cur_clicking)
{
	bool stopped = false;
	struct parts *parts;
	PARTS_LIST_FOREACH_REVERSE(parts) {
		bool hit = false, stops = false;
		if (!stopped && parts_takes_input(parts)) {
			if (parts_scrollbar_has_look(parts)) {
				hit = parts_scrollbar_hittest(parts, pos) != PARTS_SCROLLBAR_MISS;
				stops = true;
			} else {
				hit = parts_hittest(parts, PARTS_STATE_DEFAULT, pos);
				stops = !parts->pass_cursor;
			}
		}
		if (hit) {
			v14_enter(parts, pos, cur_clicking);
			stopped = stops;
		} else {
			v14_leave(parts, pos);
		}
	}
}

/* 0x546890 at a press: the pressed parts and the focus, and the focus'
 * messages when it changes (0x546a4e..0x546aa2). */
static void v14_press(Point cur_pos, struct parts *target)
{
	int no = target ? target->no : 0;
	if (no != v14_active_parts) {
		struct parts *old = v14_active_parts ? parts_try_get(v14_active_parts) : NULL;
		if (old)
			v14_send(old, PARTS_V14_MSG_LOST_FOCUS, 0, NULL);
		if (target)
			v14_send(target, PARTS_V14_MSG_FOCUS, 0, NULL);
	}
	v14_press_parts = no;
	v14_active_parts = no;
	// The click and a drag start on the target when it takes the click
	// (it is clickable or blocks the cursor; a scroll bar may do neither).
	if (target && (target->clickable || !target->pass_cursor)) {
		click_down_parts = target->no;
		drag_parts = target;
		drag_initial_pos = target->local.pos;
		drag_start_cursor = cur_pos;
	}
	if (getenv("XSYS4_STAGE2_TRACE")) {
		unsigned candidates = 0;
		struct parts *parts;
		NOTICE("S2 parts press ms=%u key=%d pos=%d,%d target=%d", SDL_GetTicks(),
				VK_LBUTTON, cur_pos.x, cur_pos.y, v14_press_parts);
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
}

/* 0x578580 -> 0x5788d0 at a release: the pressed parts' own click, when it
 * is entered and the position is given (0x578944, 0x578955). */
static void v14_release(int pressed_no, Point cur_pos, bool have_pos)
{
	int vars[3] = { cur_pos.x, cur_pos.y, VK_LBUTTON };
	struct parts *target = pressed_no ? parts_try_get(pressed_no) : NULL;
	bool on_target = target && target->is_hovered && target->clickable && have_pos;
	if (getenv("XSYS4_STAGE2_TRACE"))
		NOTICE("S2 click ms=%u key=%d pos=%d,%d target=%d%s%s", SDL_GetTicks(), VK_LBUTTON,
				cur_pos.x, cur_pos.y, target ? target->no : 0,
				target && !on_target ? " (left)" : "",
				on_target && target->button_disabled ? " (disabled)" : "");
	if (!on_target || target->button_disabled)
		return;
	if (parts_click_sound(target) >= 0)
		audio_play_sound(parts_click_sound(target));
	clicked_parts = target->no;
	// type 4 = MouseClick (CallEvent3: x,y,keyCode) in
	// CPartsMessageManager's SWITCH (bytecode-verified; type 5
	// is DoubleClick/CallEvent0 and dispatches an empty slot)
	v14_send(target, PARTS_V14_MSG_MOUSE_CLICK, 3, vars);
	// Only a parts that was made a check box has the values: the
	// pactex loader's legacy guess also gives a parts type 1.
	if (target->component_type == PARTS_COMPONENT_CHECKBOX && target->checkbox)
		parts_checkbox_set_checked(target, !target->checkbox->checked);
}

static void v14_update_input(int passed_time)
{
	bool cur_clicking = key_is_down(VK_LBUTTON);
	Point cur_pos;
	mouse_get_pos(&cur_pos.x, &cur_pos.y);
	bool have_pos = mouse_in_window();
	struct parts *target = have_pos ? v14_input_target(cur_pos) : NULL;
	// Trace (XSYS4_STAGE2_TRACE): the input target whenever it changes,
	// so that a run's log shows what the cursor is on also where nothing
	// is clicked (a scheduled move, see src/input.c); also outside an input
	// scope, where nothing else is done.
	static int traced_no = -1;
	static unsigned traced;
	int traced_now = target ? target->no : 0;
	if (have_pos && input_stage2_trace() && traced_now != traced_no && traced < 5000) {
		traced++;
		traced_no = traced_now;
		NOTICE("S2 hover ms=%u pos=%d,%d target=%d", SDL_GetTicks(), cur_pos.x, cur_pos.y, traced_now);
	}
	if (!parts_began_click)
		return;
	struct parts *parts;
	PARTS_LIST_FOREACH(parts) {
		long long t = (long long)parts->hover_time + passed_time;
		parts->hover_time = t > INT_MAX ? INT_MAX : t < INT_MIN ? INT_MIN : (int)t;
	}
	clicked_parts = 0;
	if (!have_pos)
		cur_pos = parts_prev_pos;
	bool moved = cur_pos.x != parts_prev_pos.x || cur_pos.y != parts_prev_pos.y;
	bool press = cur_clicking && !prev_clicking;
	bool release = !cur_clicking && prev_clicking;
	// The parts whose own click this release sends (the focus' key table,
	// 0x578580): the left slot's, taken before the slot is emptied.
	int released_no = release ? v14_press_parts : 0;

	if (have_pos) {
		int vars[3] = { cur_pos.x, cur_pos.y, VK_LBUTTON };
		if (moved)
			parts_enqueue_message_vars(PARTS_V14_MSG_MOUSE_MOVE, 0, -1, -1, 2, vars);
		if (release)
			parts_enqueue_message_vars(PARTS_V14_MSG_MOUSE_CLICK, 0, -1, -1, 3, vars);
		if (press) {
			v14_press(cur_pos, target);
		} else if (!cur_clicking) {
			// 0x5468d6..0x5468fb -> 0x547460: no press and the left
			// button up (the release included): the left slot is
			// emptied; the focus stays.
			v14_press_parts = 0;
			click_down_parts = 0;
		}
		v14_cursor_walk(cur_pos, cur_clicking);
	} else if (!release && v14_press_parts && v14_press_parts == v14_active_parts) {
		// 0x5476b0: the left slot's parts, when it is the focus, that is
		// while the left button is held on the parts it went down on (at
		// a release here 0x5462b0 has emptied the slot first, 0x5481cb).
		// 0x577cf0 reads the position again and, without one, has (0, 0)
		// (its locals are cleared first, 0x577d1d; what the call writes
		// when it fails was not followed).
		struct parts *focus = parts_try_get(v14_active_parts);
		if (focus)
			v14_leave(focus, (Point){ 0, 0 });
	}

	// Drag movement: a draggable pressed parts follows the cursor (the
	// drag messages are not sent in v14).
	if (drag_parts && cur_clicking && moved && drag_parts->draggable) {
		parts_set_pos(drag_parts, (Point){
			drag_initial_pos.x + (cur_pos.x - drag_start_cursor.x),
			drag_initial_pos.y + (cur_pos.y - drag_start_cursor.y) });
		is_dragging = true;
	}

	// The knob of a scroll bar moves in the input update, before the
	// widgets' update that reports it (parts_scrollbar_update). Without a
	// position the bars see the target of the last update.
	static int last_target_no;
	if (have_pos)
		last_target_no = target ? target->no : 0;
	parts_scrollbar_input(cur_pos, cur_clicking, prev_clicking,
			last_target_no ? parts_try_get(last_target_no) : NULL);
	if (release) {
		v14_release(released_no, cur_pos, have_pos);
		drag_state_reset();
		// Without a position the release empties the slot here
		// (0x5462b0).
		v14_press_parts = 0;
		click_down_parts = 0;
	}

	prev_clicking = cur_clicking;
	if (have_pos && v14_prev_have_pos)
		parts_prev_pos = cur_pos;
	v14_prev_have_pos = have_pos;
}

void PE_UpdateInputState(int passed_time)
{
	if (ain->version >= 14) {
		v14_update_input(passed_time);
		// The scheduled input (src/input.c) learns that the input was read.
		input_auto_read();
		return;
	}
	Point cur_pos;
	bool cur_clicking = key_is_down(VK_LBUTTON);
	mouse_get_pos(&cur_pos.x, &cur_pos.y);

	bool hover_consumed = false;
	bool click_consumed = false;
	struct parts *parts;
	// Iterate front-to-back (highest z first) for proper cursor consumption
	PARTS_LIST_FOREACH_REVERSE(parts) {
		parts_update_mouse(parts, cur_pos, cur_clicking, passed_time,
				&hover_consumed, &click_consumed);
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


	prev_clicking = cur_clicking;
	parts_prev_pos = cur_pos;
	// The scheduled input (src/input.c) learns that the input was read.
	input_auto_read();
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

/* 0x577af0 on an element in one of the manager's slots: not entered, no
 * time since entered, no last position, no button down on it and not the
 * focus (+0x1b4). Nothing is sent, and the state stays as it is until the
 * next cursor walk. */
static void v14_reset_slot(int parts_no)
{
	struct parts *parts = parts_no ? parts_try_get(parts_no) : NULL;
	if (!parts)
		return;
	if (parts->is_hovered) {
		parts->is_hovered = false;
		v14_hover_changed(parts);
	}
	parts->hover_time = 0;
	parts->hover_pos = (Point){0};
	if (parts_no == v14_active_parts)
		v14_active_parts = 0;
}

static void v14_reset_input_session(bool begin)
{
	// Native 0x53e830/0x53e8b0 reset the input targets, not the message
	// queue. The end of a scope (0x53e8b0 -> 0x5461a0: EndInput, and
	// BeginInput within a scope) resets the elements in the manager's five
	// slots (0x577af0 on +0x1ec, +0x200, +0x208, +0x210, +0x218) and empties
	// the slots and the stored position (+0x18/+0x1c). Of the slots only the
	// left one is followed here, and it holds a parts only while the left
	// button is held on it: the update of the release has emptied it
	// (0x547460, v14_update_input). The focus is not a slot (it is the
	// element's +0x1b4, found anew by 0x545c70 every update): a parts just
	// clicked keeps its entered flag, its time and its state like every
	// other parts, so a parts the cursor stays on is not entered again in
	// the next scope, and one the cursor is no longer on is left by the
	// next walk. When the held parts is the focus, 0x577af0 clears its
	// +0x1b4 too, silently.
	v14_reset_slot(click_down_parts);
	v14_reset_slot(v14_press_parts);
	if (begin) {
		// The beginning (0x53e830, BeginInput and an EndInput that
		// resumes an outer scope: 0x53c750 -> 0x538060) takes the focus
		// from every widget (vtable +0x1c with false, SpriteParts
		// 0x564cc0, a button's inner parts through 0x4db7e0 -> 0x4f3110):
		// LostFocus to an element whose +0x1b4 is still set, which is
		// cleared; its entered flag (+0x1ac) and state are not touched.
		// The outermost EndInput (0x53e8b0 alone) leaves the focus.
		struct parts *focus = v14_active_parts ? parts_try_get(v14_active_parts) : NULL;
		if (focus)
			v14_send(focus, PARTS_V14_MSG_LOST_FOCUS, 0, NULL);
		v14_active_parts = 0;
		// 0x546030 clears +0x40..+0x43 with one dword (0x5460c9), the
		// last update's "had a position" (+0x43) with the sampled buttons.
		v14_prev_have_pos = false;
	}
	clicked_parts = 0;
	click_down_parts = 0;
	v14_press_parts = 0;
	drag_state_reset();
	parts_scrollbar_reset_input();
	parts_prev_pos = (Point){0};
	// The beginning (0x546030) samples held buttons so returning from a
	// child scene cannot turn its closing press into another DOWN. The left
	// button held here is up at a later update: that release sends the
	// whole-screen click, as the original's sampled buttons make it
	// (0x546030 samples all three into +0x40..+0x42; only the left one is
	// followed here, see v14_update_input).
	prev_clicking = begin && key_is_down(VK_LBUTTON);
	parts_began_click = begin;
}

void parts_reset_input(void)
{
	if (ain->version >= 14) {
		v14_input_depth = 0;
		v14_reset_input_session(false);
		v14_active_parts = 0;
		v14_prev_have_pos = false;
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

/* v14 GetActiveParts: the parts under the cursor at the last left press
 * within the scope (see v14_update_input and pe_v14_message.c), 0 after a
 * press beside everything, after a reset of the input and when that parts
 * was released. That is the original's meaning too (there the press of any
 * button counts). The timing differs in three ways, which are this port's
 * choice and not taken from the original:
 *  - it is 0 outside a scope here, from the outermost EndInput on. In the
 *    original BeginInput and a nested EndInput walk the widgets to clear
 *    the focus (0x53e830 -> 0x53c750 -> 0x538060), while the outermost
 *    EndInput (0x58a750 at depth 1) runs 0x53e8b0 alone, which resets only
 *    the elements still in the manager's five slots (0x5461a0 -> 0x577af0,
 *    their +0x1b4 with them). The focus itself is kept here as there
 *    (v14_reset_input_session), so the next BeginInput sends it LostFocus.
 *  - it changes at once here. The original reports the scope object's
 *    +0x280 (0x53c6c0), which the parts update refreshes after its input
 *    update (0x53c240 at 0x53c654, after 0x547c90 at 0x53c64d), so a focus
 *    cleared by BeginInput or EndInput shows at the next parts update.
 *  - there is one value here. The original keeps one in every scope object
 *    and reads the object at +0x94 or, without one, the last of the array
 *    (0x57bc90 -> 0x53e640); which parts belong to which object was not
 *    looked up. */
int PE_GetActivePartsNumber(void)
{
	return parts_began_click ? v14_active_parts : 0;
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
