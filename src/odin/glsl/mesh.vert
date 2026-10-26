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

/* A mesh of the display list, after the defines of its variant
 * (board_programs_gl.cpp) */

uniform mat4 u_projection;
uniform mat4 u_modelview;
uniform mat4 u_modelview2;
uniform float u_depth_scale;

layout(location = 0) in vec3 i_position;
layout(location = 1) in vec3 i_normal;
layout(location = 2) in vec3 i_diffuse;
layout(location = 3) in vec3 i_ambient;
layout(location = 4) in vec4 i_specular;
layout(location = 5) in vec3 i_color1;
layout(location = 6) in vec2 i_texcoords;
layout(location = 7) in float i_alpha;
layout(location = 8) in float i_second_mv;

out vec4 p_position;
out vec3 p_normal;
out vec3 p_diffuse;
out vec4 p_specular;
out vec3 p_color1;
out vec3 p_ambient;
out vec2 p_texcoords;
out float p_alpha;

void main (void) {
	/* 1: the second modelview, 64: without the 3x3, 128: without
	 * the last row. A normal goes through the 3x3 of its modelview,
	 * as the position does (US patent 6,714,690 of SEGA: the same
	 * matrix, the translation switched off). */
	int which = int (i_second_mv * 255.0 + 0.5);
	if (which == 1) {
		p_position = u_modelview2 * vec4 (i_position, 1.0);
		p_normal = mat3 (u_modelview2) * i_normal;
	} else if (which == 0) {
		p_position = u_modelview * vec4 (i_position, 1.0);
		p_normal = mat3 (u_modelview) * i_normal;
	} else if ((which & 1) != 0) {
		bool rotate = (which & 64) == 0;
		vec3 pos = rotate ? mat3 (u_modelview2) * i_position : i_position;
		if ((which & 128) == 0)
			pos += u_modelview2[3].xyz;
		p_position = vec4 (pos, 1.0);
		p_normal = rotate ? mat3 (u_modelview2) * i_normal : i_normal;
	} else {
		bool rotate = (which & 64) == 0;
		vec3 pos = rotate ? mat3 (u_modelview) * i_position : i_position;
		if ((which & 128) == 0)
			pos += u_modelview[3].xyz;
		p_position = vec4 (pos, 1.0);
		p_normal = rotate ? mat3 (u_modelview) * i_normal : i_normal;
	}
	/* The depth bias brings the whole position nearer: same pixel. */
	gl_Position = u_projection * vec4 (p_position.xyz * u_depth_scale, 1.0);

	p_diffuse = i_diffuse;
	p_ambient = i_ambient;
	p_specular = i_specular;
	p_color1 = i_color1;
	p_texcoords = i_texcoords;
	p_alpha = i_alpha;
}
