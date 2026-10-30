/*
 *  ██████╗ ██████╗ ██╗███╗   ██╗
 * ██╔═══██╗██╔══██╗██║████╗  ██║
 * ██║   ██║██║  ██║██║██╔██╗ ██║
 * ██║   ██║██║  ██║██║██║╚██╗██║
 * ╚██████╔╝██████╔╝██║██║ ╚████║
 *  ╚═════╝ ╚═════╝ ╚═╝╚═╝  ╚═══╝
 *
 * Copyright (C) 2026 Réda Chérif-Touil
 * Copyright (C) 2026 Daniel Martin
 *
 * Odin is free software: you can redistribute it and/or modify it under
 * the terms of the GNU General Public License as published by the Free
 * Software Foundation, either version 3 of the License, or (at your
 * option) any later version.
 *
 * Odin is distributed in the hope that it will be useful, but WITHOUT ANY
 * WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 * for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with Odin.  If not, see <http://www.gnu.org/licenses/>.
 */

/* Neon 250 pixel: texture over colour, then offset colour */

#ifdef VULKAN
layout(set = 0, binding = 0) uniform sampler2D u_texture;
layout(push_constant) uniform Draw {
	float most_w;
	float lod_bias;
	int flags;
	float scale;
} draw;
#define LOD_BIAS draw.lod_bias
#define FLAGS draw.flags
#define SCALE draw.scale
#else
uniform sampler2D u_texture;
uniform float u_lod_bias;
uniform int u_flags;
uniform float u_scale;
#define LOD_BIAS u_lod_bias
#define FLAGS u_flags
#define SCALE u_scale
#endif

#ifdef NATIVE
layout(location = 0) noperspective in vec4 q_color;
layout(location = 1) noperspective in vec4 q_offset;
layout(location = 2) in vec2 q_uv;
layout(location = 3) flat in vec2 q_corner0;
layout(location = 4) flat in vec2 q_corner1;
layout(location = 5) flat in vec2 q_corner2;
vec4 v_color, v_offset;
vec2 v_uv;
#elif defined(GL_ES)
layout(location = 0) in vec4 v_color;
layout(location = 1) in vec4 v_offset;
layout(location = 2) in vec2 v_uv;
#else
layout(location = 0) noperspective in vec4 v_color;
layout(location = 1) noperspective in vec4 v_offset;
layout(location = 2) in vec2 v_uv;
#endif

layout(location = 0) out vec4 o_color;

#ifdef NATIVE
/* Native 2D, as in mesh.frag: whether c is inside edge a-b, or on an edge
 * the card would include at this scale */
bool
holds(vec2 a, vec2 b, vec2 c, float area)
{
	vec2 d = area > 0.0 ? b - a : a - b;
	float e = (b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x);

	if (area < 0.0)
		e = -e;
	if (e != 0.0)
		return e > 0.0;
	return d.y < 0.0 || (d.y == 0.0 && d.x > 0.0);
}

bool
in_triangle(vec2 c)
{
	float area = (q_corner1.x - q_corner0.x) * (q_corner2.y - q_corner0.y) -
	             (q_corner1.y - q_corner0.y) * (q_corner2.x - q_corner0.x);

	return area != 0.0 && holds(q_corner0, q_corner1, c, area) && holds(q_corner1, q_corner2, c, area) &&
	       holds(q_corner2, q_corner0, c, area);
}
#endif

void
main()
{
#ifdef NATIVE
	/* Keep fine pixels whose chip pixel centre is in the triangle. Those
	 * outside it use that centre's values. */
	vec4 dx0 = dFdx(q_color), dy0 = dFdy(q_color), dx1 = dFdx(q_offset), dy1 = dFdy(q_offset);
	vec2 dx2 = dFdx(q_uv), dy2 = dFdy(q_uv);
	vec2 at = gl_FragCoord.xy / SCALE, centre = floor(at) + 0.5;

	if (!in_triangle(centre))
		discard;
	v_color = q_color;
	v_offset = q_offset;
	v_uv = q_uv;
	if (!in_triangle(at)) {
		vec2 d = (centre - at) * SCALE;

		v_color += dx0 * d.x + dy0 * d.y;
		v_offset += dx1 * d.x + dy1 * d.y;
		v_uv += dx2 * d.x + dy2 * d.y;
	}
#endif
	/* Bits 0-3: textured, use alpha, ignore texture alpha, offset. Bits
	 * 4-5: shading. */
	vec4 color = v_color;

	if ((FLAGS & 2) == 0)
		color.a = 1.0;
	if ((FLAGS & 1) != 0) {
		vec4 texel = texture(u_texture, v_uv, LOD_BIAS);
		int shading = (FLAGS >> 4) & 3;

		if ((FLAGS & 4) != 0)
			texel.a = 1.0;
		if (shading == 0) {
			color = texel;
		} else if (shading == 1) {
			color = vec4(texel.rgb * color.rgb, texel.a);
		} else if (shading == 2) {
			color.rgb = mix(color.rgb, texel.rgb, texel.a);
		} else {
			color *= texel;
		}
	}
	if ((FLAGS & 8) != 0)
		color.rgb = min(color.rgb + v_offset.rgb, vec3(1.0));
	o_color = color;
}
