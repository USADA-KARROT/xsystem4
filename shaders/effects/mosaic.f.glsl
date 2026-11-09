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

uniform sampler2D tex;   // the new scene
uniform sampler2D old;   // the old scene
uniform vec2 resolution; // the screen resolution
uniform float progress;  // effect progress (0..1)

in vec2 tex_coord;
out vec4 frag_color;

// the largest chunk (half-way through the effect) is 2 * HALF_CHUNK + 1 pixels
#define HALF_CHUNK 48.0

vec3 get_pixel(vec2 xy) {
        return mix(texture(old, xy).rgb, texture(tex, xy).rgb, progress);
}

// progress=(0..0.5..1) -> (0..1..0)
float triangle(float f) {
        return 1.0 - abs(f * 2.0 - 1.0);
}

void main() {
        // size of each chunk in pixels, as in the original engine: odd, from 1
        // at progress 0 and 1 to 97 half-way; at least 1 for a progress
        // outside 0..1 (never a chunk of 0 pixels or less)
        float chunk_size = max(floor(triangle(progress) * HALF_CHUNK) * 2.0 + 1.0, 1.0);
        // the grid is anchored at the center of the screen, in whole pixels, as
        // in the original engine: a chunk edge passes through it on both axes
        vec2 center = floor(resolution * 0.5);
        // offset of the pixel in its chunk
        vec2 chunk_off = mod(gl_FragCoord.xy - center, chunk_size);
        // the chunk coordinate
        vec2 chunk = tex_coord - chunk_off / resolution;
        // each chunk shows the crossfade at its first pixel (the top left one),
        // as in the original engine. The partial chunks at the left and top
        // edges begin off screen; the textures clamp to the edge, so theirs is
        // the first pixel on screen.
        vec2 first = chunk + vec2(0.5) / resolution;
        frag_color = vec4(get_pixel(first), 1.0);
}
