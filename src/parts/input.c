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
#include "parts.h"
#include "xsystem4.h"
#include "parts_internal.h"
#include "system4/ain.h"
#include "system4/string.h"
#include "vm.h"
#include "xsystem4.h"

// true between calls to BeginClick and EndClick
bool parts_began_click = false;
// v14 BeginInput/EndInput scopes can nest inside synchronous scene callbacks.
static unsigned v14_input_depth;
// One Whole table for all the controllers, a port's strategy, as are the
// sampled buttons, the stored position and the cancellation suppression
// below; native managers own one each (ctrl+0x18, +0x48). See v14_in_scope.
static struct parts_v14_keys v14_whole_keys;

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
/* Native sources are independent of the focus and its held-key table.
 * Source priority is left, middle, right (0x546988..0x546a1f). */
struct v14_mouse_slot {
	int parts_no;
	Point begin;
	Point initial;
	bool began;
	bool crossed_threshold;
};
static struct v14_mouse_slot v14_mouse_slots[3];
static const int v14_mouse_keys[3] = { VK_LBUTTON, VK_MBUTTON, VK_RBUTTON };
static bool v14_prev_middle, v14_prev_right;
static bool v14_cancelled;
// v14: the focus of the update's controller, the parts under the cursor at
// the last mouse press in it within the scope, standing in for the
// original's focus (+0x1b4 of an element of that controller's list, set by
// 0x546890 at a press only). It is not one of the manager's slots: it stays
// after the release and after the end of a scope, until another press in
// that controller or a BeginInput (LostFocus). The other controllers'
// focuses are kept in v14_kept; GetActiveParts reports the active
// controller's within a scope (PE_GetActivePartsNumber)
static int v14_active_parts;
/* v14: what the manager of a controller other than the update's still
 * holds. The original has one manager per controller (0x53e690:
 * ctrl+0x18), which keeps its slots while another controller acts
 * (0x53c5b0 skips 0x547c90 without the input flag), and its focus is the
 * +0x1b4 of an element of its own list (0x545c70). The update's
 * controller uses the variables above (v14_mouse_slots, click_down_parts
 * for the left press mark +4, v14_active_parts); the others are kept here
 * from the first update of another controller until their controller acts
 * again (v14_switch_manager) or a BeginInput clears them
 * (v14_reset_input_session). `sampled` is what the shared sampled buttons
 * were when the controller was left, its manager's +0x40..+0x42 at its
 * last update (0x547fbd). */
struct v14_kept_manager {
	int controller;
	int focus;
	struct v14_mouse_slot slots[3];
	int click_down_parts;
	bool sampled[3];
};
static struct v14_kept_manager *v14_kept;
static int v14_nr_kept, v14_kept_cap;

static void v14_kept_push(struct v14_kept_manager m)
{
	if (v14_nr_kept == v14_kept_cap) {
		int cap = v14_kept_cap ? v14_kept_cap * 2 : 4;
		v14_kept = xrealloc_array(v14_kept, v14_kept_cap, cap, sizeof *v14_kept);
		v14_kept_cap = cap;
	}
	v14_kept[v14_nr_kept++] = m;
}

static struct v14_kept_manager *v14_kept_find(int controller)
{
	for (int i = 0; i < v14_nr_kept; i++) {
		if (v14_kept[i].controller == controller)
			return &v14_kept[i];
	}
	return NULL;
}

/* A released parts leaves every kept manager (0x53c140, as for the
 * update's own one in parts_input_release). */
static void v14_kept_forget(int parts_no)
{
	for (int i = 0; i < v14_nr_kept; i++) {
		struct v14_kept_manager *m = &v14_kept[i];
		if (m->focus == parts_no)
			m->focus = 0;
		if (m->click_down_parts == parts_no)
			m->click_down_parts = 0;
		for (int j = 0; j < 3; j++) {
			if (m->slots[j].parts_no == parts_no)
				m->slots[j] = (struct v14_mouse_slot){0};
		}
	}
}
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
	if (ain->version == 14) {
		for (int i = 0; i < 3; i++) {
			if (v14_mouse_slots[i].parts_no == parts->no)
				v14_mouse_slots[i] = (struct v14_mouse_slot){0};
		}
		v14_kept_forget(parts->no);
	}
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
 * v14: the input update reads one controller, the script's layer. The parts
 * engine's update (0x53e690) runs the update of every controller of the
 * stack (+0x88..+0x8c) and of the system overlay (+0x84), 0x53c5b0, and
 * gives the input flag to one of them: the designated active controller
 * (+0x94) or, while none is designated, the top of the stack (0x53e640);
 * the overlay only while it is the designated one (and only while +0xb4 is
 * set, SetEnableInput, which the game never calls). Only that controller
 * runs the input update (0x547c90 from 0x53c64d), with its own manager
 * (ctrl+0x18) on the elements of its own tree: 0x5385b0 collects every
 * parts below the controller's root (pre-order), 0x546520 turns them into
 * their widgets' elements. The time since entered (0x546620), the slots
 * (0x547c00), the press target and the focus (0x546890, 0x545c70), the
 * cursor walk (0x5471d0), the local wheel (0x547000) and GetClickNumber
 * (0x547610) go over that list alone. The parts of every other controller
 * are not touched: not entered, not left, no message, their time stands
 * still. So a button under a child scene (a confirmation box opens a layer
 * of its own: SceneContext@0 -> AFL_Parts_AddLayer(-1)) keeps its hover look
 * and, when the box goes with the cursor still on it, is not entered again.
 * GetActiveParts reads the active controller's own focus (0x57bc90 ->
 * 0x53c6c0: +0x280, refreshed for every controller by 0x53c240).
 *
 * Here parts->controller_no follows the parent chain (parts_set_controller),
 * which is the original's "below the controller's root", and the controller
 * is PE_get_active_controller(), the choice of 0x53e690. In the
 * single-controller mode (engines without AddController) every parts is in
 * the scope, as before.
 *
 * As the original's managers, each controller keeps its own slots, left
 * press mark and focus (v14_kept, v14_switch_manager): while another
 * controller acts they stay as they were; a press there does not take the
 * focus (0x546a4e compares with the focus 0x545c70 found in that
 * controller's own list) and sends it no LostFocus; GetActiveParts reads
 * the active controller's own focus; a BeginInput within a scope and every
 * EndInput reset the elements in every manager's slots, and a BeginInput
 * (or an EndInput that resumes a scope) sends every manager's focus
 * LostFocus (v14_reset_input_session). So when a press lands in a
 * confirmation box while it opens, before the box's BeginInput (a
 * controller created inside an input scope takes presses at once), the
 * button that opened it stays the old controller's focus and gets its
 * LostFocus at the box's BeginInput, as inferred for the original.
 *
 * The port's strategy, not the original's:
 *  - one sampled state for all the controllers (the original's managers
 *    have one each): the sampled buttons (+0x40..+0x43), the stored
 *    position (+0x18/+0x1c), the Whole key table (+0x48) and the
 *    cancellation suppression (+0x1fc). BeginInput and EndInput reset every
 *    one of the original's managers (0x53e8b0, 0x53e830) as they reset
 *    these, so the two differ only while the active controller changes
 *    between them, presumably: a button held over SetActiveLayer (or over
 *    the removal of the designated controller) is a new press for the
 *    manager that acts next (neither 0x57ba47 nor 0x53d500 begins it,
 *    0x547f2d compares with what it sampled last), which it is not here:
 *    no whole-screen Trigger, no slot, no new choice of the focus. Only a
 *    focus that controller kept sees it, as its own key table had the
 *    button up (v14_keys_update): its Trigger and Down and, when entered,
 *    its Press and the left button's pressed look; at the release its
 *    KeyUp and, entered with a position, its Click. That is the native
 *    press's messages when the cursor is on that focus. With no element
 *    of that controller under the cursor the native press presumably
 *    finds nothing and sends the focus LostFocus (GetActiveParts 0), where
 *    here the focus stays, gets a KeyUp and no Click, until the next
 *    BeginInput; with another of its elements there that one presumably
 *    takes the focus and the click, where here it gets its MouseEnter
 *    alone;
 *    the old manager, acting again after a release elsewhere, sends its
 *    whole-screen KeyUp and Click a second time, which is not done here; a
 *    BeginInput or an EndInput releases every manager's own Whole table
 *    (0x5461a0 -> 0x547ad0), the one here; a controller added inside a
 *    scope begins its own manager at once (0x53d3a0 -> 0x546030), whose
 *    first updates send the whole-screen MouseMove, which is not sent here
 *    when the cursor has not moved; and the suppression of a controller
 *    left while cancelled is not kept for its return. Only the cancellation
 *    of a slot taken up at a switch back uses what the old manager sampled
 *    (v14_update_input). Inferred, with no native screen;
 *  - one focus per controller (v14_active_parts and the kept ones) where
 *    the original has a flag on every element (+0x1b4) and looks it up in
 *    the list (0x545c70): a focus moved under another controller's parts
 *    (SetParentPartsNumber) stays its old controller's here, and a press in
 *    that controller sends it LostFocus (v14_press), where the original
 *    finds it in no list and sends that at the next 0x53c750 (BeginInput);
 *    pressed in its new controller it gets Focus again here, and the next
 *    BeginInput sends it one LostFocus for each manager that has it, where
 *    the original presumably sends no Focus (the new controller's 0x545c70
 *    finds it with its flag set) and one LostFocus;
 *  - SetEnableInputProcess (+0x90 false leaves an element out of the list)
 *    and SetEnableInput (+0xb4) stay stubs; 0x53c830 re-sorts the list
 *    (keys not traced); the port uses z order (the drawing order);
 *  - a dragged scroll knob of another controller still follows the cursor
 *    (the original's knob follows its focus' key events, which the other
 *    controller's manager no longer sends; not followed further).
 */
static int v14_scope;	/* the controller of this input update */

static bool v14_in_scope_of(const struct parts *parts, int controller)
{
	return !parts_multi_controller || parts->controller_no == controller;
}

static bool v14_in_scope(const struct parts *parts)
{
	return v14_in_scope_of(parts, v14_scope);
}

/* v14: the update's controller changed from `from` to `to` (SetActiveLayer,
 * AddLayer or EraseLayer since the last update; SetActiveLayer begins no
 * manager, 0x57ba47, so this waits for the first update). The old
 * controller's manager keeps its slots, its left press mark and its focus;
 * one without a focus holds nothing (in v14 a slot and the press mark only
 * ever hold the focus' element: v14_press sets all three, a left Trigger
 * marks the focus, and the focus changes only at a press, which empties
 * the slots and comes with the left button up or going down: another
 * button's press while it is held cancels instead). The new one takes up
 * what it kept, or nothing (all false in `sampled` then), with what the
 * shared buttons were when that controller was left in `sampled`. */
static void v14_switch_manager(int from, int to, bool sampled[3])
{
	struct v14_kept_manager live = { .controller = from, .focus = v14_active_parts,
		.click_down_parts = click_down_parts,
		.sampled = { prev_clicking, v14_prev_middle, v14_prev_right } };
	for (int i = 0; i < 3; i++)
		live.slots[i] = v14_mouse_slots[i];
	// The update's controller has no kept entry (taken up below).
	if (live.focus)
		v14_kept_push(live);
	struct v14_kept_manager next = { .controller = to };
	struct v14_kept_manager *m = v14_kept_find(to);
	if (m) {
		next = *m;
		*m = v14_kept[--v14_nr_kept];
	}
	v14_active_parts = next.focus;
	click_down_parts = next.click_down_parts;
	for (int i = 0; i < 3; i++) {
		v14_mouse_slots[i] = next.slots[i];
		sampled[i] = next.sampled[i];
	}
	// The left slot's parts is the pressed one (v14_press,
	// v14_release_slots).
	v14_press_parts = v14_mouse_slots[0].parts_no;
}

/* The focus of a controller's manager: the update's own, or a kept one. */
static int v14_focus_of(int controller)
{
	if (controller == v14_scope)
		return v14_active_parts;
	struct v14_kept_manager *m = v14_kept_find(controller);
	return m ? m->focus : 0;
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
 * the normal state (on-cursor and down are CG parts without a CG). Only the
 * parts of the update's controller are candidates (v14_in_scope).
 */
static struct parts *v14_input_target(Point pos)
{
	struct parts *parts;
	PARTS_LIST_FOREACH_REVERSE(parts) {
		if (!v14_in_scope(parts))
			continue;
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
 * update 0x53c5b0 alone, for the active controller alone: v14_in_scope; the
 * update does nothing outside a BeginInput/EndInput scope, 0x547cba), with
 * that controller's own manager: when the active controller changed, the
 * old one's slots, left press mark and focus are kept and the new one's
 * taken up first (v14_switch_manager), also outside a scope. In its order:
 *
 *  0. The slots whose element is not in the controller's list are emptied
 *     silently (0x547c00 -> 0x548530; v14_drop_slots_out_of_scope).
 *  1. Every listed element's time since entered grows by the passed time
 *     (0x546620 at 0x547df2, whether the element is entered or not), and
 *     GetClickNumber is cleared (+0x14 at 0x547e23).
 *  2. The cursor's position is read through the same call as the HLL
 *     Mouse_GetPos (0x547e50; 0x498b95). When it gives none, the update goes
 *     on with the last position it stored (+0x18/+0x1c, 0x547e5f). It sends
 *     no whole-screen move/click (0x548024), takes no press and runs no
 *     cursor walk (0x5482a7 skips 0x546890). Wheel events still go out;
 *     only the left slot's parts is
 *     left (0x548339 -> 0x5476b0 -> 0x577cf0) when it is the focus, that is
 *     while the left button is still held on the parts it went down on. The
 *     slot is empty after the release (6. below), and a release without a
 *     position empties it before (0x5481cb -> 0x5462b0), so a parts clicked
 *     or released is not left here: it stays entered, as the other parts do,
 *     until the pointer is back. Here the position counts as given while
 *     the pointer is in the window (mouse_in_window: the scheduled pointer,
 *     or SDL's mouse focus). What the original's call checks was not
 *     followed (it is behind an interface); the port's Mouse_GetPos also asks
 *     for the keyboard focus, which is not asked here, so that a window in
 *     the background shows the hover as before.
 *  3. Wheel counts read from the input interface (0x547fd1): when either
 *     is nonzero, the whole-screen MouseWheel (forward, back), through
 *     the manager's own emitter at 0x54800f. It precedes the check for a
 *     position and is sent even when the two nonzero counts are equal.
 *  4. With a position, when it is not the one stored: the whole-screen
 *     MouseMove (x, y), through the manager's own emitter (0x548054; its
 *     source is null, so the message's parts number is 0, and its mask
 *     0x54562a leaves type 1 through: of the types 0..29 it lets 1, 3, 4, 5,
 *     9 and 15..18 through). The game hooks it only in its debugging tools
 *     (AFL_Parts_AddWholeMouseMoveEvent).
 *  5. With a position, a release: the whole-screen MouseClick (x, y, key 1),
 *     whatever is under the cursor (0x548064, the same emitter). Also when the
 *     button was already down at BeginInput: 0x546030 samples the buttons
 *     into +0x40..+0x42, and EndInput samples them again for the outer scope
 *     (0x58a750 -> 0x53e830).
 *  6. With a position, 0x546890 (at 0x5482f9): at a press (down now, up at
 *     the last update; 0x547f2d..0x547fb3) the parts under the cursor,
 *     v14_input_target, becomes the pressed parts (the left slot) and the
 *     focus; when the focus changes, the old one gets LostFocus and the new
 *     one Focus (0x546a4e..0x546aa2: 0x80f5a4, 0x80f580, each only when the
 *     element's +0x1b4 says so). Without a press and with the left button
 *     up, the update of the release included, the left slot is emptied
 *     (0x5468d6..0x5468fb -> 0x547460: +0x1ec and the drop target +0x210),
 *     before the click of 8. goes out. Then the cursor walk
 *     (v14_cursor_walk), every update.
 *  7. Local MouseWheel (0x547000 at 0x548362): the first eligible element
 *     under the cursor in reverse order, or the updated focus/root
 *     fallback, receives the same two counts. This also uses the stored
 *     position when the position query failed; see v14_dispatch_wheel.
 *  8. The focused element's keys (0x578580 at 0x54836a): at the press a
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
 *  9. The position is stored (+0x18/+0x1c) when this update and the last one
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
 * v14 now keeps all three source slots independently of the focused
 * element's key table. Any mouse press can choose the focus. A new other
 * button cancels the existing source before target selection, and blocks
 * that selection and the cursor walk until all buttons are released.
 * Whole mouse releases and focused key updates continue while blocked.
 * Cancellation sends the original Begin/Draging/End wire messages and
 * restores only a movable left source. Normal drop target selection and
 * completion remain unimplemented, as do swipes and double clicks.
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
 * v14 key messages use the fixed table below. Not done: the double click
 * (0x4e6bd0), normal drop target selection/completion and the swipe (an element in a swipe, +0x1dc not negative, stops the walk and
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
		// 0x564f20, the cursor sound (Parts_PlaySound(n, 2)), at every
		// new enter (0x577c17), whether or not a button is down: the
		// sound of the name in slot 2 (parts_set_sound_name). Nothing is
		// played without a number.
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
	if (ain->version == 14)
		parts->v14_scrollbar_track_entered = false;
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
 * drop targets are not established by the current v14 walk.
 *
 * The messages go to the element's own emitter (+0x1e4), so they carry the
 * parts' number, delegate index and event ID. The coordinates go through
 * 0x696e00, the window's scale, which mouse_get_pos has already applied.
 *
 * A scroll bar stands for its knob and its track (the original's inner
 * elements), as in v14_input_target: entered when one of them is under the
 * cursor, and it stops the walk.
 *
 * The list is the update's controller's (v14_in_scope): the parts of the
 * other controllers are neither entered nor left, so they keep their
 * entered flag, their state and their time.
 */
static void v14_cursor_walk(Point pos, bool cur_clicking)
{
	bool stopped = false;
	struct parts *parts;
	PARTS_LIST_FOREACH_REVERSE(parts) {
		if (!v14_in_scope(parts))
			continue;
		bool hit = false, stops = false;
		enum parts_scrollbar_hit scrollbar_hit = PARTS_SCROLLBAR_MISS;
		if (!stopped && parts_takes_input(parts)) {
			if (parts_scrollbar_has_look(parts)) {
				scrollbar_hit = parts_scrollbar_hittest(parts, pos);
				hit = scrollbar_hit != PARTS_SCROLLBAR_MISS;
				stops = true;
			} else {
				hit = parts_hittest(parts, PARTS_STATE_DEFAULT, pos);
				stops = !parts->pass_cursor;
			}
		}
		// Native 0x5471d0 walks the knob before the clickable track.
		// A non-passing knob leaves the track; a passing knob allows it
		// to enter. Cache only on the ordinary walk, not at release:
		// cancellation can suppress this walk and retain entered state.
		if (ain->version == 14)
			parts->v14_scrollbar_track_entered = scrollbar_hit == PARTS_SCROLLBAR_TRACK;
		if (hit) {
			v14_enter(parts, pos, cur_clicking);
			stopped = stops;
		} else {
			v14_leave(parts, pos);
		}
	}
}

/* 0x5781b0: a source has separate drag-start flags for each button.
 * The wire representation of Draging has five integers, not four. */
static void v14_drag_update(struct v14_mouse_slot *slot, int key, Point pos)
{
	struct parts *source = slot->parts_no ? parts_try_get(slot->parts_no) : NULL;
	if (!source)
		return;
	if (!slot->began)
		v14_send(source, PARTS_V14_MSG_DRAG_BEGIN, 1, &key);
	int vars[5] = { slot->begin.x, slot->begin.y, pos.x, pos.y, key };
	v14_send(source, PARTS_V14_MSG_DRAGGING, 5, vars);
	slot->began = true;
}

static void v14_drag_end(struct v14_mouse_slot *slot, int key, int mode)
{
	struct parts *source = slot->parts_no ? parts_try_get(slot->parts_no) : NULL;
	if (source && slot->began) {
		int vars[2] = { key, mode };
		v14_send(source, PARTS_V14_MSG_DRAG_END, 2, vars);
	}
	slot->began = false;
}

/* Native 0x5462b0/0x5463c0/0x546470: cancellation is not a focus change.
 * Other-button press serializes mode 2; release without a position mode 3
 * (0x577660). A source need not have moved or be movable to be cancelled. */
static void v14_cancel_slot(int index, bool outside, Point pos)
{
	struct v14_mouse_slot *slot = &v14_mouse_slots[index];
	struct parts *source = slot->parts_no ? parts_try_get(slot->parts_no) : NULL;
	if (!source)
		return;
	int key = v14_mouse_keys[index];
	if (index == 0 && source->draggable)
		parts_set_pos(source, slot->initial);
	v14_drag_update(slot, key, slot->begin);
	v14_drag_end(slot, key, outside ? 3 : 2);
	/* A native left cancellation also leaves its drop target. The port's
	 * existing v14 input does not establish a drop target; target selection
	 * and normal drop completion remain outside this input change. */
	if (index == 0) {
		if (drop_target) {
			int no = source->no;
			v14_send(drop_target, PARTS_V14_MSG_DROP_LEAVE, 1, &no);
			v14_leave(drop_target, pos);
		}
		drag_state_reset();
		v14_press_parts = 0;
	}
	*slot = (struct v14_mouse_slot){0};
	v14_cancelled = true;
}

/* Normal source movement is needed to retain the cancellation's Begin
 * state. Native 0x546c02 uses the configured Manhattan threshold, default
 * 10 (0x545a3f..0x545a58). This port currently uses that default; importing
 * the original configuration source is not implemented. Middle/right
 * movement has no left-button threshold (0x546d36..0x546d90). */
static void v14_move_slots(Point pos, bool moved)
{
	for (int i = 0; i < 3; i++) {
		struct v14_mouse_slot *slot = &v14_mouse_slots[i];
		struct parts *source = slot->parts_no ? parts_try_get(slot->parts_no) : NULL;
		if (!source)
			continue;
		if (i == 0) {
			long long dx = (long long)pos.x - slot->begin.x;
			long long dy = (long long)pos.y - slot->begin.y;
			if (llabs(dx) + llabs(dy) >= 10)
				slot->crossed_threshold = true;
			if (!slot->crossed_threshold || source->no != v14_active_parts)
				continue;
			if (moved && source->draggable)
				parts_set_pos(source, (Point){ slot->initial.x + (pos.x - slot->begin.x),
						slot->initial.y + (pos.y - slot->begin.y) });
		}
		if (moved)
			v14_drag_update(slot, v14_mouse_keys[i], pos);
	}
}

/* 0x547460/0x547530/0x5475a0: ordinary release ends a started drag and
 * clears the source. No drop target is represented by the current v14
 * walk, so the left end uses mode 1; middle/right use mode 0. */
static void v14_release_slots(const bool down[3])
{
	for (int i = 0; i < 3; i++) {
		if (down[i])
			continue;
		struct v14_mouse_slot *slot = &v14_mouse_slots[i];
		v14_drag_end(slot, v14_mouse_keys[i], i == 0 ? 1 : 0);
		*slot = (struct v14_mouse_slot){0};
	}
	v14_press_parts = v14_mouse_slots[0].parts_no;
	if (!down[0])
		click_down_parts = 0;
}

/* 0x546890 at a press: the pressed parts and the focus of the update's
 * controller, and the focus' messages when it changes (0x546a4e..0x546aa2).
 * Another controller's focus is kept apart (v14_kept) and gets nothing
 * here. The old focus gets its LostFocus here also when it was moved under
 * another controller's parts; the original looks the old focus up in the
 * list (0x545c70) and leaves one outside it to the next 0x53c750 (see
 * v14_in_scope: one focus per controller is the port's strategy). */
static void v14_press(Point cur_pos, struct parts *target, int key)
{
	int no = target ? target->no : 0;
	if (no != v14_active_parts) {
		struct parts *old = v14_active_parts ? parts_try_get(v14_active_parts) : NULL;
		if (old)
			v14_send(old, PARTS_V14_MSG_LOST_FOCUS, 0, NULL);
		if (target)
			v14_send(target, PARTS_V14_MSG_FOCUS, 0, NULL);
	}
	v14_press_parts = key == VK_LBUTTON ? no : 0;
	v14_active_parts = no;
	if (ain->version == 14) {
		// The focused inner element keeps its role until another press.
		// Do not classify it again from the release position, and do not
		// overwrite a different parts' role needed by old-focus flush.
		if (target)
			target->v14_scrollbar_track_focus = parts_scrollbar_has_look(target)
				&& parts_scrollbar_hittest(target, cur_pos) == PARTS_SCROLLBAR_TRACK;
		for (int i = 0; i < 3; i++) {
			v14_mouse_slots[i] = (struct v14_mouse_slot){0};
			if (target && key == v14_mouse_keys[i])
				v14_mouse_slots[i] = (struct v14_mouse_slot){
					.parts_no = no, .begin = cur_pos, .initial = target->local.pos };
		}
	}
	// The click and a drag start on the target when it takes the click
	// (it is clickable or blocks the cursor; a scroll bar may do neither).
	if (key == VK_LBUTTON && target && (target->clickable || !target->pass_cursor)) {
		click_down_parts = target->no;
		drag_parts = target;
		drag_initial_pos = target->local.pos;
		drag_start_cursor = cur_pos;
	}
	if (getenv("XSYS4_STAGE2_TRACE")) {
		unsigned candidates = 0;
		struct parts *parts;
		NOTICE("S2 parts press ms=%u key=%d pos=%d,%d target=%d", SDL_GetTicks(),
				key, cur_pos.x, cur_pos.y, no);
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

/* Native 0x574648 makes the track clickable; the ordinary knob keeps the
 * default false (0x550a2f). The outer pass_cursor, but not clickable, is
 * copied by 0x573b20. The local release (0x5788d0) uses that inner element's
 * entered state, including when its keys are flushed after losing focus. */
static bool v14_click_ready(struct parts *parts)
{
	if (ain->version == 14 && parts_scrollbar_has_look(parts))
		return parts->v14_scrollbar_track_focus && parts->v14_scrollbar_track_entered;
	return parts->clickable && parts->is_hovered;
}

/* Trace (XSYS4_STAGE2_TRACE) only, no behavior: whether the input update
 * reads a left release, and whether that update has printed its "S2 click"
 * line, so that every left release inside a scope has at least one line
 * (target=0 when no parts takes it, as before the v14 key events). A line
 * from an update without a left release ends in " (late)": an old focus'
 * table flushed at a focus change, a controller's focus whose left button
 * went up while another controller acted (its release comes when its
 * controller acts again, see v14_drop_slots_out_of_scope), or an element
 * whose own table still holds the left button when it is the focus again
 * (a source cancelled by another button keeps it, as in the original). */
static bool v14_trace_left_release;
static bool v14_left_release_traced;

/* 0x578580 -> 0x5788d0 at a release: the pressed parts' own click, when it
 * is entered and the position is given (0x578944, 0x578955). */
static void v14_release(int pressed_no, Point cur_pos, bool have_pos)
{
	int vars[3] = { cur_pos.x, cur_pos.y, VK_LBUTTON };
	struct parts *target = pressed_no ? parts_try_get(pressed_no) : NULL;
	bool on_target = target && have_pos && v14_click_ready(target);
	v14_left_release_traced = true;
	if (getenv("XSYS4_STAGE2_TRACE"))
		NOTICE("S2 click ms=%u key=%d pos=%d,%d target=%d%s%s%s", SDL_GetTicks(), VK_LBUTTON,
				cur_pos.x, cur_pos.y, target ? target->no : 0,
				target && !on_target ? " (left)" : "",
				on_target && target->button_disabled ? " (disabled)" : "",
				v14_trace_left_release ? "" : " (late)");
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

/* Native 0x575060: fixed table order, including generic Shift/Control and
 * F1..F9, excluding Alt, separator and F10+. Platform VK sampling remains
 * key_is_down's existing policy (including its current keypad mapping). */
static const enum sact_keycode v14_key_codes[PARTS_V14_NR_KEYS] = {
	0x01, 0x02, 0x04, 0x08, 0x09, 0x0d, 0x10, 0x11, 0x1b, 0x20,
	0x21, 0x22, 0x23, 0x24, 0x25, 0x26, 0x27, 0x28, 0x2d, 0x2e,
	0x30, 0x31, 0x32, 0x33, 0x34, 0x35, 0x36, 0x37, 0x38, 0x39,
	0x41, 0x42, 0x43, 0x44, 0x45, 0x46, 0x47, 0x48, 0x49, 0x4a,
	0x4b, 0x4c, 0x4d, 0x4e, 0x4f, 0x50, 0x51, 0x52, 0x53, 0x54,
	0x55, 0x56, 0x57, 0x58, 0x59, 0x5a, 0x60, 0x61, 0x62, 0x63,
	0x64, 0x65, 0x66, 0x67, 0x68, 0x69, 0x6a, 0x6b, 0x6d, 0x6e,
	0x6f, 0x70, 0x71, 0x72, 0x73, 0x74, 0x75, 0x76, 0x77, 0x78,
};

static void v14_key_message(struct parts *parts, int type, int key)
{
	if (parts)
		v14_send(parts, type, 1, &key);
	else
		parts_enqueue_message_vars(type, 0, -1, -1, 1, &key);
}

/* Native 0x5788d0: local Click precedes KeyUp, also when flushing an old
 * focus. Only left release has sound, ClickNumber and checkbox effects.
 * Keep this port's disabled-button policy for all three local clicks. */
static void v14_key_up(struct parts *parts, int key, Point pos, bool have_pos)
{
	if (parts) {
		if (key == VK_LBUTTON) {
			if (parts->clickable) {
				if (click_down_parts == parts->no)
					click_down_parts = 0;
				// An entered element with no position keeps its state.
				if (!parts->is_hovered)
					parts_set_state(parts, PARTS_STATE_DEFAULT);
				else if (have_pos)
					parts_set_state(parts, PARTS_STATE_HOVERED);
			}
			v14_release(parts->no, pos, have_pos);
		} else if ((key == VK_RBUTTON || key == VK_MBUTTON)
				&& have_pos && v14_click_ready(parts)
				&& !parts->button_disabled) {
			int vars[3] = { pos.x, pos.y, key };
			v14_send(parts, PARTS_V14_MSG_MOUSE_CLICK, 3, vars);
		}
	}
	v14_key_message(parts, PARTS_V14_MSG_KEY_UP, key);
}

/* Native 0x574fd0 clears held bits but leaves repeat time/threshold intact.
 * Whole EndInput uses only Up; a focus change uses the full local release. */
static void v14_keys_flush(struct parts_v14_keys *keys, struct parts *parts,
		Point pos, bool have_pos)
{
	bool released[PARTS_V14_NR_KEYS];
	for (int i = 0; i < PARTS_V14_NR_KEYS; i++) {
		released[i] = keys->down[i];
		keys->down[i] = false;
	}
	for (int i = 0; i < PARTS_V14_NR_KEYS; i++) {
		if (released[i])
			v14_key_up(parts, v14_key_codes[i], pos, have_pos);
	}
}

static void v14_keys_update(struct parts_v14_keys *keys, struct parts *parts,
		Point pos, bool have_pos)
{
	// Native gate is a WM_ACTIVATE active&&!minimized cache. Using the
	// SDL activation/minimization cache is an explicit port approximation,
	// not OS-message equivalence; platform sampling is unchanged here.
	bool active = input_is_active();
	bool repeat = (int32_t)keys->elapsed > keys->threshold;
	if (repeat) {
		keys->elapsed = 0;
		keys->threshold = 80;
	}
	unsigned char events[PARTS_V14_NR_KEYS];
	bool held = false, triggered = false;
	for (int i = 0; i < PARTS_V14_NR_KEYS; i++) {
		bool down = active && key_is_down(v14_key_codes[i]);
		bool trigger = down && !keys->down[i];
		events[i] = (trigger ? 1 : 0) | (down ? 2 : 0)
			| (down && (trigger || repeat) ? 4 : 0)
			| (!down && keys->down[i] ? 8 : 0);
		keys->down[i] = down;
		held |= down;
		triggered |= trigger;
	}
	if (!held || triggered) {
		keys->elapsed = 0;
		keys->threshold = 500;
	}
	// Four complete batches, not four interleaved messages per key.
	for (int batch = 0; batch < 4; batch++) {
		for (int i = 0; i < PARTS_V14_NR_KEYS; i++) {
			if (!(events[i] & (1 << batch)))
				continue;
			int key = v14_key_codes[i];
			// 0x578650..0x578675: left Trigger sets the pressed state
			// on the focused element, even while source selection is
			// suppressed by another button's cancellation.
			if (batch == 0 && parts && key == VK_LBUTTON
					&& parts->is_hovered && parts->clickable) {
				click_down_parts = parts->no;
				parts_set_state(parts, PARTS_STATE_CLICKED);
			}
			if (batch == 2 && parts && i < 3 && !parts->is_hovered)
				continue;
			if (batch == 3)
				v14_key_up(parts, key, pos, have_pos);
			else
				v14_key_message(parts, PARTS_V14_MSG_KEY_TRIGGER + batch, key);
		}
	}
}

/*
 * Native 0x5350c0: types 5/7/15 accept wheel input; 2/3/6/11/12/13 do
 * when focused. The next source is an internal component owner (+0x7c),
 * then the parts' own flag. That owner is not the visual parent set at
 * 0x53b098 (+0x13c): ordinary named parts are constructed with a null owner
 * (0x578cb0 -> 0x4f3c20 -> 0x533da0). Do not walk parts->parent here.
 * The port has one input element per parts, so its active ID is the focus
 * here; native composite widgets' internal owner and focus are not rebuilt.
 */
static bool v14_wheelable(struct parts *parts)
{
	switch (parts->component_type) {
	case 5: case 7: case 15:
		return true;
	case 2: case 3: case 6: case 11: case 12: case 13:
		if (parts->no == v14_active_parts)
			return true;
		break;
	}
	return parts->wheelable;
}

/*
 * Native 0x547000: the first wheelable, shown, nontransparent element hit
 * in reverse order receives one event. Cursor blocking, click permission
 * and disabled-button state do not take part in this walk. A scrollbar
 * stands for its inner track and knob, as in the existing input walk.
 * Without a hit, the updated focus receives it, otherwise the manager's
 * root. Here the active controller's root represents that fallback; the
 * system overlay has no root and cannot supply such an element. The
 * candidates and the focus are the update's controller's (0x545c70 finds
 * the focus in the list, else the root): a focus held in another
 * controller does not take it.
 */
static void v14_dispatch_wheel(Point pos, const int *counts)
{
	struct parts *target = NULL, *parts;
	PARTS_LIST_FOREACH_REVERSE(parts) {
		if (!v14_in_scope(parts) || !v14_wheelable(parts) || !parts_takes_input(parts))
			continue;
		bool hit = parts_scrollbar_has_look(parts)
			? parts_scrollbar_hittest(parts, pos) != PARTS_SCROLLBAR_MISS
			: parts_hittest(parts, PARTS_STATE_DEFAULT, pos);
		if (hit) {
			target = parts;
			break;
		}
	}
	bool fallback = !target;
	if (!target && v14_active_parts) {
		struct parts *focus = parts_try_get(v14_active_parts);
		if (focus && v14_in_scope(focus))
			target = focus;
	}
	if (!target)
		target = parts_controller_root(PE_get_active_controller());
	if (target)
		v14_send(target, PARTS_V14_MSG_MOUSE_WHEEL, 2, counts);
	static unsigned traced;
	if (input_stage2_trace() && traced++ < 2000)
		NOTICE("S2 wheel-dispatch ms=%u pos=%d,%d forward=%d back=%d target=%d fallback=%d",
				SDL_GetTicks(), pos.x, pos.y, counts[0], counts[1],
				target ? target->no : 0, fallback);
}

/* 0x547c90 -> 0x547c00 -> 0x548530 for each of the five slots: a slot whose
 * element is not in this update's list is emptied, with no message and no
 * change to the element (0x577af0 is not called). The slots, the left press
 * mark and the focus go with their controller's manager
 * (v14_switch_manager), so this empties only a slot or a press mark whose
 * element was moved under another controller's parts
 * (SetParentPartsNumber); the left slot's drag goes with it. The focus is
 * not touched here. A release while another controller acts has no slot
 * there: that controller's manager never had the press (also one begun
 * inside the scope, which has a manager of its own), so the release sends
 * the whole-screen click and, when that controller kept a focus whose own
 * table had the button up, that focus' KeyUp and, entered with a
 * position, its Click: the table read the button down at the controller's
 * first update (see v14_in_scope).
 * The focus' own key table is neither sampled nor flushed while another
 * controller acts (v14_update_input). When the left button went down on a
 * controller's focus and the active controller changed, the focus' KeyUp
 * and MouseClick (when it is entered) come when its controller acts again:
 * at the first update after the switch back when the button was released
 * in the other controller (late, with GetClickNumber; the whole-screen
 * Click went out there at the release and is not sent a second time, the
 * sampled buttons being shared), or at the release when it comes after
 * the switch back. A press in the other controller does not move that
 * focus. A BeginInput or an EndInput meanwhile resets the element silently
 * while it is still in its manager's slot (0x5461a0 -> 0x577af0;
 * v14_reset_input_session), so none of its keys comes late then.
 * Inferred, with no native screen or dynamic measurement: the original's
 * 0x578580 and 0x574d50 use the element's own key table ([edi+8]), and
 * while the old controller's manager does not run its slots, its +0x40 and
 * the element's +0x1b4 stay (0x545c70 looks in the update's list only).
 * So that manager presumably also sends its whole-screen KeyUp and Click
 * again when it acts again after a release elsewhere, and a button held
 * since SetActiveLayer (0x57ba47 does not begin the manager it designates,
 * which compares with the buttons it sampled last, 0x547f2d) presumably is
 * a new press there; the port sends no second whole-screen KeyUp or Click
 * and makes no new press of it beyond a kept focus' own table (its sampled
 * buttons are shared, see v14_in_scope). How this compares with the
 * original running is not verified. */
static void v14_drop_slots_out_of_scope(void)
{
	struct parts *held = v14_press_parts ? parts_try_get(v14_press_parts) : NULL;
	if (held && !v14_in_scope(held))
		v14_press_parts = 0;
	struct parts *down = click_down_parts ? parts_try_get(click_down_parts) : NULL;
	if (down && !v14_in_scope(down))
		click_down_parts = 0;
	if (drag_parts && !v14_in_scope(drag_parts))
		drag_state_reset();
	if (ain->version == 14) {
		for (int i = 0; i < 3; i++) {
			int no = v14_mouse_slots[i].parts_no;
			struct parts *p = no ? parts_try_get(no) : NULL;
			if (!p || !v14_in_scope(p))
				v14_mouse_slots[i] = (struct v14_mouse_slot){0};
		}
	}
}

static void v14_update_input(int passed_time)
{
	bool key_events = ain->version == 14;
	bool active = !key_events || input_is_active();
	bool cur_clicking = active && key_is_down(VK_LBUTTON);
	bool down[3] = { cur_clicking, active && key_is_down(VK_MBUTTON),
		active && key_is_down(VK_RBUTTON) };
	Point cur_pos;
	mouse_get_pos(&cur_pos.x, &cur_pos.y);
	bool have_pos = mouse_in_window();
	// The controller whose parts this update reads (0x53e690); in the
	// single-controller mode every parts is read.
	int scope = parts_multi_controller ? PE_get_active_controller() : 0;
	/* Native controllers have separate managers (0x53e690): on a switch
	 * the old one keeps its slots, its left press mark and its focus, and
	 * the new one takes up what it kept (v14_switch_manager). The
	 * cancellation suppression is shared (the port's), so a switch
	 * discards it: it must not block the new layer's input, and returning
	 * to the old scope does not restore its cancelled state. */
	bool switched = false, taken_sampled[3] = { false, false, false };
	if (key_events && scope != v14_scope) {
		v14_cancelled = false;
		v14_switch_manager(v14_scope, scope, taken_sampled);
		switched = true;
	}
	v14_scope = scope;
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
	// Keep the initial hover trace as the run's opening anchor.
	static int traced_scope = -1;
	static unsigned traced_scopes;
	if (parts_multi_controller && v14_scope != traced_scope && input_stage2_trace()
			&& traced_scopes < 2000) {
		traced_scopes++;
		traced_scope = v14_scope;
		NOTICE("S2 input-scope ms=%u controller=%d began=%d", SDL_GetTicks(), v14_scope,
				parts_began_click);
	}
	if (!parts_began_click)
		return;
	struct parts *old_focus = v14_active_parts ? parts_try_get(v14_active_parts) : NULL;
	if (old_focus && !v14_in_scope(old_focus))
		old_focus = NULL;
	v14_drop_slots_out_of_scope();
	struct parts *parts;
	PARTS_LIST_FOREACH(parts) {
		if (!v14_in_scope(parts))
			continue;
		long long t = (long long)parts->hover_time + passed_time;
		parts->hover_time = t > INT_MAX ? INT_MAX : t < INT_MIN ? INT_MIN : (int)t;
		// Every element in this active scope advances, even without focus.
		// Unsigned addition preserves native 32-bit wrapping without UB.
		if (key_events)
			parts->v14_keys.elapsed += (uint32_t)passed_time;
	}
	clicked_parts = 0;
	v14_left_release_traced = false;
	if (key_events) {
		v14_whole_keys.elapsed += (uint32_t)passed_time;
		// 0x547e16: complete Whole keys before wheel, move or click.
		v14_keys_update(&v14_whole_keys, NULL, cur_pos, have_pos);
	}
	if (!have_pos)
		cur_pos = parts_prev_pos;
	bool moved = cur_pos.x != parts_prev_pos.x || cur_pos.y != parts_prev_pos.y;
	bool press = cur_clicking && !prev_clicking;
	bool release = !cur_clicking && prev_clicking;
	v14_trace_left_release = release;
	bool pressed[3] = { press, down[1] && !v14_prev_middle,
		down[2] && !v14_prev_right };
	bool released[3] = { release, !down[1] && v14_prev_middle,
		!down[2] && v14_prev_right };
	// The parts whose own click this release sends (the focus' key table,
	// 0x578580): the left slot's, taken before the slot is emptied.
	int released_no = release ? v14_press_parts : 0;

	// Native 0x547fd1..0x54800f: Whole wheel precedes Whole move/click
	// and does not require a current position. Keep both counts: equal
	// nonzero counts still produce an event, consumed differently by scripts.
	int wheel[2];
	mouse_get_wheel(&wheel[0], &wheel[1]);
	bool have_wheel = wheel[0] || wheel[1];
	if (have_wheel)
		parts_enqueue_message_vars(PARTS_V14_MSG_MOUSE_WHEEL, 0, -1, -1, 2, wheel);

	if (have_pos) {
		int vars[3] = { cur_pos.x, cur_pos.y, VK_LBUTTON };
		if (moved)
			parts_enqueue_message_vars(PARTS_V14_MSG_MOUSE_MOVE, 0, -1, -1, 2, vars);
		// Whole mouse release order is L/R/M, unlike slot priority L/M/R.
		static const int order[3] = { 0, 2, 1 };
		for (int n = 0; n < (key_events ? 3 : 1); n++) {
			int i = order[n];
			if (released[i]) {
				vars[2] = v14_mouse_keys[i];
				parts_enqueue_message_vars(PARTS_V14_MSG_MOUSE_CLICK, 0, -1, -1, 3, vars);
			}
		}
	}
	if (key_events) {
		// Cancels precede ordinary press/hover processing. The focus and
		// local held bits stay intact and continue updating while blocked.
		// At a switch back, a slot taken up is also cancelled by
		// another button held now that its manager sampled up when it
		// last ran: that is a new press for the manager (0x547f2d),
		// which cancels the slot (0x548153..0x5481b0). One it sampled
		// down, held since before the slot was filled, is no press
		// there. (Ordinary presses still come from the shared sampled
		// buttons.)
		for (int i = 0; i < 3; i++) {
			int j = (i + 1) % 3, k = (i + 2) % 3;
			bool other = pressed[j] || pressed[k];
			if (switched)
				other = other || (down[j] && !taken_sampled[j])
					|| (down[k] && !taken_sampled[k]);
			if (other)
				v14_cancel_slot(i, false, have_pos ? cur_pos : (Point){0, 0});
		}
		if (!have_pos) {
			for (int i = 0; i < 3; i++) {
				if (released[i])
					v14_cancel_slot(i, true, (Point){0, 0});
			}
		}
		if (!down[0] && !down[1] && !down[2])
			v14_cancelled = false;
	}
	if (!key_events || !v14_cancelled) {
		if (have_pos) {
			if (key_events) {
				int key = pressed[0] ? VK_LBUTTON : pressed[1] ? VK_MBUTTON
					: pressed[2] ? VK_RBUTTON : 0;
				if (key)
					v14_press(cur_pos, target, key);
				else
					v14_release_slots(down);
				v14_move_slots(cur_pos, moved);
			} else if (press) {
				v14_press(cur_pos, target, VK_LBUTTON);
			} else if (!cur_clicking) {
				v14_press_parts = 0;
				click_down_parts = 0;
			}
			v14_cursor_walk(cur_pos, cur_clicking);
		} else if (!release && v14_press_parts && v14_press_parts == v14_active_parts) {
			// 0x548311..0x548339 -> 0x5476b0: without a position only the
			// left slot's parts is left (0x548324 -> 0x547740 also leaves a
			// hovered drop target, which the port does not make), when it
			// is the focus (0x5476dc..0x5476ec calls 0x577cf0), that is
			// while the left button is held on the parts it went down on
			// (at a release here 0x5462b0
			// has emptied the slot first, 0x5481cb). 0x577cf0 reads the
			// position again and, without one, has (0, 0) (its locals are
			// cleared first, 0x577d1d; what the call writes when it fails
			// was not followed).
			struct parts *focus = parts_try_get(v14_active_parts);
			if (focus)
				v14_leave(focus, (Point){ 0, 0 });
		}
	}

	// Retain the previous movement policy outside the v14 key path.
	if (!key_events && drag_parts && cur_clicking && moved && drag_parts->draggable) {
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
	// 0x548362: after hover/focus processing, before the focus' release.
	// A failed position query still uses the manager's previous position.
	if (have_wheel)
		v14_dispatch_wheel(cur_pos, wheel);
	if (key_events) {
		struct parts *focus = v14_active_parts ? parts_try_get(v14_active_parts) : NULL;
		if (focus && !v14_in_scope(focus))
			focus = NULL;
		if (focus)
			v14_keys_update(&focus->v14_keys, focus, cur_pos, have_pos);
		// 0x548367..0x54837c: new focus first, then old focus's full Up
		// path. Inactive controllers' element tables remain untouched.
		if (old_focus && old_focus != focus)
			v14_keys_flush(&old_focus->v14_keys, old_focus, cur_pos, have_pos);
	}
	if (release) {
		// v14's element table already dispatched Click before KeyUp.
		if (!key_events)
			v14_release(released_no, cur_pos, have_pos);
		// Trace: a left release no parts of the scope took (no focus in
		// the scope, e.g. after BeginInput sampled the button held, or a
		// press on a layer that is no longer active) is target=0, as it
		// was before the v14 key events.
		else if (!v14_left_release_traced && getenv("XSYS4_STAGE2_TRACE"))
			NOTICE("S2 click ms=%u key=%d pos=%d,%d target=0", SDL_GetTicks(), VK_LBUTTON,
					cur_pos.x, cur_pos.y);
		drag_state_reset();
		// Without a position the release empties the slot here
		// (0x5462b0).
		v14_press_parts = 0;
		click_down_parts = 0;
	}

	v14_trace_left_release = false;
	prev_clicking = cur_clicking;
	if (key_events) {
		v14_prev_middle = down[1];
		v14_prev_right = down[2];
	}
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

/*
 * v14 オン指針表示連動: a parts shown while another (its link) is entered.
 * The original keeps the link's number at the component's +0x124, 0 for
 * none (the constructor, 0x550927): Parts_SetOnCursorShowLinkPartsNumber
 * (0x58e6d0) gets or makes the component and writes the number (and +0xb4,
 * its fade, 0), Parts_GetOnCursorShowLinkPartsNumber (0x58e770) reads it,
 * 0 without a component; the pactex loader resolves the name of the key in
 * the activity (pactex_resolve_show_links). The link is a number: nothing
 * is made for it, and a number without a parts shows the parts as if it
 * had no link (0x535c90; see parts_link_shown). Here no link is -1, as for
 * the engines before v14 (linked_to), and a number of 0 or less is no link;
 * linked_from is the last parts linked to a target (the input marks the
 * screen dirty through it). The original fades the parts in and out over
 * +0x44 (100) ms; this port shows and hides it at once.
 */
void parts_set_show_link(struct parts *parts, int link_parts_no)
{
	struct parts *old = parts->linked_to > 0 ? parts_try_get(parts->linked_to) : NULL;
	if (old && old->linked_from == parts->no)
		old->linked_from = -1;
	parts->linked_to = link_parts_no > 0 ? link_parts_no : -1;
	struct parts *target = link_parts_no > 0 ? parts_try_get(link_parts_no) : NULL;
	if (target)
		target->linked_from = parts->no;
	parts_dirty(parts);
}

/* Shared with the engines before v14 (GUIEngine, GoatGUIEngine, the older
 * PartsEngine), which keep their behaviour: both parts are got or made, and
 * the number is kept as it is. */
void PE_SetOnCursorShowLinkPartsNumber(int parts_no, int link_parts_no)
{
	if (ain->version >= 14) {
		if (parts_no > 0)
			parts_set_show_link(parts_get(parts_no), link_parts_no);
		return;
	}
	struct parts *parts = parts_get(parts_no);
	struct parts *link_parts = parts_get(link_parts_no);
	parts->linked_to = link_parts_no;
	link_parts->linked_from = parts_no;
}

/* The engines before v14: -1 is no link. */
int PE_GetOnCursorShowLinkPartsNumber(int parts_no)
{
	return parts_get(parts_no)->linked_to;
}

/* v14 (0x58e770): 0 is no link, and a number without a parts reads 0. */
int PE_v14_GetOnCursorShowLinkPartsNumber(int parts_no)
{
	struct parts *parts = parts_no > 0 ? parts_try_get(parts_no) : NULL;
	return parts && parts->linked_to > 0 ? parts->linked_to : 0;
}

/*
 * v14 sound names. A component has four names (+0x1b0 + i * 0x18).
 * Parts_SetSoundName (0x590230) and Parts_GetSoundName (0x5902e0) write and
 * read slot State of the component the get-or-create lookup gives (0x540250:
 * none for a number of 0 or less), without looking at the name or the
 * State. The original plays a name by name: its low-level parts copy the
 * slots 1..3 at their update (0x565200 -> 0x5b7f20, a name that differs
 * replaces the old one and frees its sound), slots 2 and 3 get a CSound when
 * not empty (0x5b7590 -> 0x5bac00), which plays through PlaySe(3, name, "")
 * (0x5ba830); the enter plays slot 2 (0x564f20), the release that clicks
 * plays slot 3 (0x5b7fc0).
 *
 * Here (a port's strategy): a name of slot 2 or 3 is made the sound
 * archive's number of that name when it is set, on_cursor_sound and
 * on_click_sound, which the input plays as before (audio_play_sound); an
 * empty name, or one the archive does not have, is -1 and plays nothing
 * (the original's PlaySe of such a name fails, silently). A State outside
 * 0..3 writes nothing and reads "" (the original indexes past its slots).
 * The number API of the engines before v14 writes the numbers alone.
 */
static int parts_sound_name_number(const struct string *name)
{
	int no = -1;
	if (!name || !name->size || !asset_exists_by_name(ASSET_SOUND, name->text, &no) || no < 0)
		return -1;
	return no;
}

void parts_set_sound_name(struct parts *parts, int slot, const char *text, size_t len)
{
	if (slot < 0 || slot >= PARTS_NR_SOUND_NAMES)
		return;
	if (parts->sound_names[slot])
		free_string(parts->sound_names[slot]);
	parts->sound_names[slot] = text && len ? make_string(text, len) : NULL;
	if (slot == 2)
		parts->on_cursor_sound = parts_sound_name_number(parts->sound_names[slot]);
	else if (slot == 3)
		parts->on_click_sound = parts_sound_name_number(parts->sound_names[slot]);
}

void PE_SetSoundName(int parts_no, struct string *name, int state)
{
	if (parts_no <= 0)
		return;
	parts_set_sound_name(parts_get(parts_no), state, name ? name->text : NULL, name ? name->size : 0);
}

struct string *PE_GetSoundName(int parts_no, int state)
{
	if (parts_no <= 0)
		return string_ref(&EMPTY_STRING);
	struct parts *parts = parts_get(parts_no);
	if (state < 0 || state >= PARTS_NR_SOUND_NAMES || !parts->sound_names[state])
		return string_ref(&EMPTY_STRING);
	return string_dup(parts->sound_names[state]);
}

/* Parts_PlaySound (0x59b220): the component the get-or-create lookup gives
 * (0x53e510); 2 plays the cursor sound (0x564f20), 3 the click sound
 * (0x5b7fc0), any other State nothing. A disabled button plays neither, as
 * at its enter and its click (parts_cursor_sound; read from 0x5285b0, which
 * empties its inner parts' names, not followed for this call). */
void PE_PlaySound(int parts_no, int state)
{
	if (parts_no <= 0)
		return;
	struct parts *parts = parts_get(parts_no);
	int no = state == 2 ? parts_cursor_sound(parts) : state == 3 ? parts_click_sound(parts) : -1;
	if (no >= 0)
		audio_play_sound(no);
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
	if (ain->version == 14) {
		// 0x577af0: silent local reset, with repeat fields preserved.
		for (int i = 0; i < PARTS_V14_NR_KEYS; i++)
			parts->v14_keys.down[i] = false;
		parts->v14_scrollbar_track_focus = false;
		parts->v14_scrollbar_track_entered = false;
	}
	if (parts->is_hovered) {
		parts->is_hovered = false;
		v14_hover_changed(parts);
	}
	parts->hover_time = 0;
	parts->hover_pos = (Point){0};
	// The +0x1b4 is the element's: no manager has it as its focus now.
	if (parts_no == v14_active_parts)
		v14_active_parts = 0;
	for (int i = 0; i < v14_nr_kept; i++) {
		if (v14_kept[i].focus == parts_no)
			v14_kept[i].focus = 0;
	}
}

/* A controller's place in 0x53e830's order: the system overlay first (+0x84,
 * 0x53e835), then the stack from its bottom (+0x88 on, 0x53e880; 0x53e640
 * reads the top as the last one). One no longer on the stack goes last. */
static int v14_begin_order(int controller)
{
	if (controller == PARTS_CONTROLLER_SYSTEM_OVERLAY)
		return -1;
	int index = parts_controller_index(controller);
	return index < 0 ? INT_MAX : index;
}

static void v14_reset_input_session(bool begin)
{
	if (ain->version == 14)
		v14_keys_flush(&v14_whole_keys, NULL, (Point){0}, false);
	// Native 0x53e830/0x53e8b0 reset the input targets, not the message
	// queue. The end of a scope (0x53e8b0 -> 0x5461a0: EndInput, and
	// BeginInput within a scope) resets the elements in the manager's five
	// slots (0x577af0 on +0x1ec, +0x200, +0x208, +0x210, +0x218) and empties
	// the slots and the stored position (+0x18/+0x1c). The three source
	// slots are represented here; normal drop and swipe targets are not.
	// 0x53e8b0 does this for every controller's manager (the stack, then
	// the overlay), so the slots another controller's manager kept are
	// reset as well (v14_kept), with the elements in them; the order is
	// not seen, as nothing is sent.
	// Each source is held until its release or cancellation, so a release
	// before EndInput has already emptied it
	// (0x547460, v14_update_input). The focus is not a slot (it is the
	// element's +0x1b4, found anew by 0x545c70 every update): a parts just
	// clicked keeps its entered flag, its time and its state like every
	// other parts, so a parts the cursor stays on is not entered again in
	// the next scope, and one the cursor is no longer on is left by the
	// next walk. When the held parts is the focus, 0x577af0 clears its
	// +0x1b4 too, silently.
	// A source cancelled by another button is no longer in its slot:
	// 0x5462b0/0x5463c0/0x546470 empty the slot with 0x548530 (left:
	// 0x54636e..0x546378), which drops the reference and leaves the
	// element as it is. So in v14 only the three source slots are reset
	// here; click_down_parts (the left press or a left Trigger on the
	// focus while selection is suppressed) and v14_press_parts are reset
	// only as members of a slot. A cancelled source keeps its focus, its
	// entered flag and its key table, and the next Begin sends it
	// LostFocus (0x538060). Versions after v14 keep the earlier reset of
	// the pressed parts.
	if (ain->version == 14) {
		for (int i = 0; i < 3; i++) {
			v14_reset_slot(v14_mouse_slots[i].parts_no);
			v14_mouse_slots[i] = (struct v14_mouse_slot){0};
		}
		v14_cancelled = false;
		for (int k = 0; k < v14_nr_kept; k++) {
			for (int i = 0; i < 3; i++) {
				v14_reset_slot(v14_kept[k].slots[i].parts_no);
				v14_kept[k].slots[i] = (struct v14_mouse_slot){0};
			}
			v14_kept[k].click_down_parts = 0;
		}
	} else {
		v14_reset_slot(click_down_parts);
		v14_reset_slot(v14_press_parts);
	}
	if (begin) {
		// The beginning (0x53e830, BeginInput and an EndInput that
		// resumes an outer scope: 0x53c750 -> 0x538060) takes the focus
		// from every widget (vtable +0x1c with false, SpriteParts
		// 0x564cc0, a button's inner parts through 0x4db7e0 -> 0x4f3110):
		// LostFocus to an element whose +0x1b4 is still set, which is
		// cleared; its entered flag (+0x1ac) and state are not touched.
		// The outermost EndInput (0x53e8b0 alone) leaves every
		// manager's focus. 0x53e830 takes the overlay first, then the
		// stack from its bottom, one controller's trees at a time
		// (v14_begin_order), so with a focus in several controllers
		// (pressed before a BeginInput of the controller on top, see
		// v14_in_scope) each gets one LostFocus in that order. The kept
		// table is empty afterwards.
		v14_kept_push((struct v14_kept_manager){ .controller = v14_scope,
			.focus = v14_active_parts });
		v14_active_parts = 0;
		for (int i = 1; i < v14_nr_kept; i++) {
			struct v14_kept_manager m = v14_kept[i];
			int j = i;
			for (; j > 0 && v14_begin_order(v14_kept[j - 1].controller)
					> v14_begin_order(m.controller); j--)
				v14_kept[j] = v14_kept[j - 1];
			v14_kept[j] = m;
		}
		for (int i = 0; i < v14_nr_kept; i++) {
			struct parts *focus = v14_kept[i].focus ? parts_try_get(v14_kept[i].focus) : NULL;
			if (focus)
				v14_send(focus, PARTS_V14_MSG_LOST_FOCUS, 0, NULL);
		}
		v14_nr_kept = 0;
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
	// (0x546030 samples all three into +0x40..+0x42).
	bool active = ain->version != 14 || input_is_active();
	prev_clicking = begin && active && key_is_down(VK_LBUTTON);
	if (ain->version == 14) {
		v14_prev_middle = begin && active && key_is_down(VK_MBUTTON);
		v14_prev_right = begin && active && key_is_down(VK_RBUTTON);
	}
	parts_began_click = begin;
}

void parts_reset_input(void)
{
	if (ain->version >= 14) {
		v14_input_depth = 0;
		v14_reset_input_session(false);
		v14_active_parts = 0;
		v14_nr_kept = 0;
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

/* v14 GetActiveParts: the active controller's focus, the parts under the
 * cursor at the last mouse press in that controller within the scope (see
 * v14_update_input and pe_v14_message.c), 0 after a press there beside
 * everything, after a reset of the input and when that parts was released.
 * The press of any button counts in v14. The original keeps one value in
 * every controller and reads the active one's (0x57bc90: +0x94 or, without
 * one, the top of the stack, 0x53e640; 0x53c6c0 reads its +0x280, 0 when
 * empty), which 0x53c240 refreshes in every controller's update, input
 * flag or not, with the first parts of the controller's own tree whose
 * widget has the focus (0x538180 from its root). Here it is the focus of
 * the active controller's manager (v14_focus_of): the update's own or a
 * kept one, so also right after SetActiveLayer, before that controller's
 * first update; and 0 when that focus was moved under another
 * controller's parts (see v14_in_scope). The timing differs in two ways,
 * which are this port's choice and not taken from the original:
 *  - it is 0 outside a scope here, from the outermost EndInput on. In the
 *    original BeginInput and a nested EndInput walk the widgets to clear
 *    the focus (0x53e830 -> 0x53c750 -> 0x538060), while the outermost
 *    EndInput (0x58a750 at depth 1) runs 0x53e8b0 alone, which resets only
 *    the elements still in the managers' five slots (0x5461a0 -> 0x577af0,
 *    their +0x1b4 with them). The focus itself is kept here as there
 *    (v14_reset_input_session), so the next BeginInput sends it LostFocus.
 *  - it changes at once here. The original reports the scope object's
 *    +0x280 (0x53c6c0), which the parts update refreshes after its input
 *    update (0x53c240 at 0x53c654, after 0x547c90 at 0x53c64d), so a focus
 *    cleared by BeginInput or EndInput shows at the next parts update.
 * Versions after v14 keep the earlier one focus for every controller,
 * counted when it is the active controller's. */
int PE_GetActivePartsNumber(void)
{
	if (!parts_began_click)
		return 0;
	int active = parts_multi_controller ? PE_get_active_controller() : 0;
	int no = ain->version == 14 ? v14_focus_of(active) : v14_active_parts;
	struct parts *focus = no ? parts_try_get(no) : NULL;
	if (focus && !v14_in_scope_of(focus, active))
		return 0;
	return no;
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
