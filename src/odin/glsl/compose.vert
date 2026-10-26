/*
 * Odin
 * Copyright (C) 2026 Réda Chérif-Touil
 *
 * Valkyrie
 * Copyright (C) 2011-2014, Stefano Teso
 *
 * Valkyrie is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * Valkyrie is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with Valkyrie.  If not, see <http://www.gnu.org/licenses/>.
 */

/* The picture shown and the 2D layers, composed (compose.frag) */

uniform mat4 u_projection =
	mat4 (vec4 ( 2.0,  0.0,  0.0,  0.0),
	      vec4 ( 0.0, -2.0,  0.0,  0.0),
	      vec4 ( 0.0,  0.0, -1.0,  0.0),
	      vec4 (-1.0,  1.0,  0.0,  1.0));

layout(location = 0) in vec3 i_position;
layout(location = 1) in vec2 i_texcoords;

out vec2 p_texcoords;

void main (void) {
	gl_Position = u_projection * vec4 (i_position, 1.0);
	p_texcoords = i_texcoords;
}
