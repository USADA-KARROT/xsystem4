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

uniform mat4 world_transform;
uniform mat4 view_transform;
// The camera on screen pixels (parts_perspective), or the identity.
uniform mat4 persp_transform;
uniform int use_persp;
uniform mat4 inv_clipper_transform;
uniform mat4 inv_clip_area_transform;

in vec4 vertex_pos;
in vec2 vertex_uv;
out vec2 tex_coord;
// The clipper and clip area are rectangles on the screen: with the camera,
// their coordinates come from the projected point and carry w, so that the
// fragment shader gets them interpolated linearly on the screen.
out vec3 clip_coord;
out vec3 clip_area_coord;

void main() {
	vec4 world_pos = world_transform * vertex_pos;
	tex_coord = vertex_uv;
	if (use_persp != 0) {
		vec4 screen_pos = persp_transform * world_pos;
		gl_Position = view_transform * screen_pos;
		vec4 flat_pos = vec4(screen_pos.xy / screen_pos.w, 0.0, 1.0);
		clip_coord = vec3((inv_clipper_transform * flat_pos).xy * screen_pos.w, screen_pos.w);
		clip_area_coord = vec3((inv_clip_area_transform * flat_pos).xy * screen_pos.w, screen_pos.w);
	} else {
		gl_Position = view_transform * world_pos;
		clip_coord = vec3((inv_clipper_transform * world_pos).xy, 1.0);
		clip_area_coord = vec3((inv_clip_area_transform * world_pos).xy, 1.0);
	}
}
