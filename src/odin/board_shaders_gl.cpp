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

#include "odin/board_shaders_gl.hpp"

namespace odin::shaders {

/* The translucent fragments of a viewport, see renderer_translucent.cpp:
 * u_heads has the last one of each pixel and next the one before it, as
 * indices plus one; 0 ends a list. */
#define FRAGMENT_LISTS R"glsl(struct node_t {
	vec4 color;
	float depth;
	uint next;
	uint order;
	uint mode;
};
layout(binding = 0, r32ui) uniform uimage2D u_heads;
layout(std430, binding = 0) buffer nodes_t {
	node_t u_nodes[];
};
)glsl"

const char *const MESH_VERTEX = R"glsl(#extension GL_ARB_explicit_attrib_location : require

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
)glsl";

const char *const MESH_FRAGMENT = R"glsl(out vec4 fragColor;

struct light_t {
	vec3 position;
	vec3 direction;
	vec3 color;
	float ambient;
	vec2 extents;
	vec3 spot;
	vec2 cone;
	float table[16];
};

uniform light_t		u_lights[4];
uniform vec3		u_ambient;
uniform sampler2D	u_texture;
uniform vec2		u_fog;
uniform vec3		u_fog_color;
uniform float		u_alpha_ref;
uniform float		u_weigh_by_alpha;
#if HAS_PATTERN
uniform uint		u_pattern;
#endif

#if COLLECT
)glsl"
FRAGMENT_LISTS
R"glsl(layout(std430, binding = 1) buffer count_t {
	uint u_count;
};
uniform sampler2D	u_solid_depth;
uniform uint		u_order;
uniform uint		u_blend_mode;
uniform float		u_depth_scale;
uniform float		u_sort_scale;
#endif

in vec4 p_position;
in vec3 p_normal;
in vec3 p_diffuse;
in vec4 p_specular;
in vec3 p_color1;
in vec3 p_ambient;
in vec2 p_texcoords;
in float p_alpha;

void
apply_light (inout vec3 diffuse,
             inout vec3 specular,
             in light_t light,
             in int type,
             in int att_type)
{
	vec3 normal = normalize (p_normal);
	vec3 light_direction;
	float distance, attenuation, n_dot_l, r_dot_v;

	if (type == 3) {
		diffuse += light.color;
		return;
	}

	if (type == 0) {
		distance = length (light.direction);
		light_direction = light.direction / distance;
	} else {
		vec3 delta = light.position - p_position.xyz;
		distance = length (delta);
		light_direction = delta / distance;
	}

	/* X360 825C5BA0: sat (P d + P Q), sat (P d^2 + P Q). The port does
	 * not attenuate the types 2 and 3; the hardware does, on 1 / d and
	 * 1 / d^2 (the far red light of the PHARRIER logo scene, which
	 * leaves the asteroids grey on the cabinet). */
	if (att_type == 0)
		attenuation = clamp (light.extents.x * (distance + light.extents.y), 0.0, 1.0);
	else if (att_type == 1)
		attenuation = clamp (light.extents.x * (distance * distance + light.extents.y), 0.0, 1.0);
	else if (att_type == 2)
		attenuation = clamp (light.extents.x * (1.0 / distance + light.extents.y), 0.0, 1.0);
	else if (att_type == 3)
		attenuation = clamp (light.extents.x * (1.0 / (distance * distance) + light.extents.y), 0.0, 1.0);
	else
		attenuation = 1.0;

	/* The cone. sat ((L . axis - cos outer) * scale), the axis pointing
	 * from the target to the light, is not the light itself as in the
	 * X360 port (825C5498): it addresses the table of the light, whose
	 * sixteen points go from the cutoff to the cone angle with straight
	 * lines between them (US patent 6,333,742 of SEGA; the 194 loads
	 * the table). HEURISTIC: the first point is taken at the cutoff,
	 * the last at the cone angle, and nothing is lit beyond the cutoff,
	 * which the patent says is deleted. */
	if (type == 2) {
		float where = 15.0 * clamp ((dot (light_direction, normalize (-light.spot)) - light.cone.x) * light.cone.y, 0.0, 1.0);
		int point = min (int (where), 14);
		attenuation *= (where > 0.0) ? mix (light.table[point], light.table[point + 1], where - float (point)) : 0.0;
	}

	n_dot_l = dot (normal, light_direction);
	/* The ambient level of the 051 is a constant share of the colour,
	 * cut by the distance and by the cone like the Lambert term
	 * (HEURISTIC: the scale and the cuts are read from the cabinets). */
	diffuse += attenuation * (max (n_dot_l, 0.0) + light.ambient) * light.color;

	/* The reflected ray against the view. The vo3d pixel shader takes
	 * the Blinn half vector, whose lobe is about four times as wide for
	 * the same exponent: it washes out the PHARRIER logo seen from the
	 * front, which the cabinet shows plain blue. */
	r_dot_v = dot (reflect (-light_direction, normal), -normalize (p_position.xyz));
	if (n_dot_l > 0.0 && r_dot_v > 0.0)
		specular += attenuation * pow (r_dot_v, 255.0 * p_specular.a) * light.color;
}

void
main (void)
{
	vec4 color;
	vec3 base;
	float alpha;

	/* The depth buffer holds the logarithm of the eye depth: with the
	 * near planes the games send, the projected depth cannot tell apart
	 * surfaces that the cabinets do (the shadow of the AIRTRIX combo
	 * labels lies 0.06 behind them at a depth of 560). */
#if HAS_PATTERN
	/* 501: bit 0 is the top left pixel of the 4 x 4 pattern. The pattern
	 * is laid in the pixels drawn: at a finer scale its dots are finer
	 * than those of the hardware, and hardly show. */
	ivec2 dot = ivec2 (gl_FragCoord.xy) & 3;
	if (((u_pattern >> uint ((3 - dot.y) * 4 + dot.x)) & 1u) == 0u)
		discard;
#endif
	float frag_depth = log2 (1.0 + 1.0 / gl_FragCoord.w) * (1.0 / 24.0);
	gl_FragDepth = frag_depth;

#if HAS_TEXTURE
	vec4 texel = texture (u_texture, p_texcoords);
#if ALPHA_MODE
	/* 881 bit a: the vertex alpha blends the texel over the 091 colour
	 * (vo3d.0x01100). */
	base = mix (p_diffuse, texel.rgb, p_alpha);
	alpha = texel.a;
#elif TEX_MODE == 1
	/* The texel alpha blends the texel over the 091 colour, the polygon
	 * alpha alone remains (vo3d.0x01010). */
	base = mix (p_diffuse, texel.rgb, texel.a);
	alpha = p_alpha;
#elif TEX_MODE == 2
	/* The texel modulates the 291 colour: the vo3d.0x01020 vertex
	 * shader sends ve_polygon_color1, not color0. */
	base = texel.rgb * p_color1;
	alpha = texel.a * p_alpha;
#elif TEX_MODE == 3
	/* The 091 colour with the texel alpha (vo3d.0x01030). */
	base = p_diffuse;
	alpha = texel.a * p_alpha;
#elif TEX_MODE == 4
	/* The texel blends the 291 colour over the 091 colour (vo3d.0x01040). */
	base = mix (p_diffuse, p_color1, texel.rgb);
	alpha = texel.a * p_alpha;
#else
	base = texel.rgb;
	alpha = texel.a * p_alpha;
#endif
#else
#if ALPHA_MODE
	/* 881 bit a: the vertex alpha blends the 291 colour over the 091
	 * colour (vo3d.0x00100). */
	base = mix (p_diffuse, p_color1, p_alpha);
	alpha = 1.0;
#else
	base = p_diffuse;
	alpha = p_alpha;
#endif
#endif

#if HAS_LIGHTING
	/* vo3d: (ambient + diffuse + emission) * base + specular * material;
	 * mode 1 has the ambient, mode 2 the emission, mode 3 neither and no
	 * specular (X360 825B4DE8). */
	vec3 diffuse  = vec3 (0.0);
	vec3 specular = vec3 (0.0);
	vec3 ambient  = (SHADING_MODE == 1) ? u_ambient * p_ambient : vec3 (0.0);
	vec3 emission = (SHADING_MODE == 2) ? p_ambient : vec3 (0.0);

#if HAS_LIGHT0
	apply_light (diffuse, specular, u_lights[0], LIGHT0_TYPE, LIGHT0_ATT_TYPE);
#endif
#if HAS_LIGHT1
	apply_light (diffuse, specular, u_lights[1], LIGHT1_TYPE, LIGHT1_ATT_TYPE);
#endif
#if HAS_LIGHT2
	apply_light (diffuse, specular, u_lights[2], LIGHT2_TYPE, LIGHT2_ATT_TYPE);
#endif
#if HAS_LIGHT3
	apply_light (diffuse, specular, u_lights[3], LIGHT3_TYPE, LIGHT3_ATT_TYPE);
#endif

	color = vec4 ((ambient + diffuse + emission) * base, alpha);
	if (SHADING_MODE != 3) {
		color.rgb += specular * p_specular.rgb * (255.0 / 64.0);
	}
#else
	color = vec4 (base, alpha);
#endif


	/* Alpha test of the render pass (X360 825B2408). */
	if (color.a <= u_alpha_ref)
		discard;

#if COLLECT
	/* A translucent fragment is kept unless it is behind the other
	 * types, whose depth buffer has steps of 6e-8. */
	ivec2 pixel = ivec2 (gl_FragCoord.xy);
	if (frag_depth >= 1.0 ||
	    frag_depth > texelFetch (u_solid_depth, pixel, 0).r + 3.0e-8)
		discard;
#endif

#if HAS_FOG
	/* X360 vo3d: linear fog on the eye depth from start to end. */
	float fog = clamp ((-p_position.z - u_fog.x) * u_fog.y, 0.0, 1.0);
	fragColor = vec4 (mix (color.rgb, u_fog_color, fog), color.a);
#else
	fragColor = color;
#endif

#if HAS_CUE
	/* Depth queue without a colour: the pixel fades into what is behind
	 * it, linearly on the eye depth from start to end. */
	float cue = clamp ((-p_position.z - u_fog.x) * u_fog.y, 0.0, 1.0);
	if (cue >= 1.0)
		discard;
	fragColor.a = 1.0 - cue;
#endif

	/* A blended colour is weighted by its alpha before it is clamped: a
	 * lit colour above 1 keeps its excess (the galaxy of the PHARRIER
	 * logo scene, whose veils burn to white on the cabinet). The blend
	 * function takes the source as it is then. */
	if (u_weigh_by_alpha > 0.5)
		fragColor.rgb *= fragColor.a;

#if COLLECT
	uint n = atomicAdd (u_count, 1u);
	if (n < uint (u_nodes.length ())) {
		u_nodes[n].color = clamp (fragColor, 0.0, 1.0);
		/* Sorted on the depth the 101 gives among translucent
		 * fragments, not the one it gives against the other types
		 * (depth_bias.hpp). */
		u_nodes[n].depth = log2 (1.0 + u_sort_scale / (gl_FragCoord.w * u_depth_scale)) *
		                   (1.0 / 24.0);
		u_nodes[n].next = imageAtomicExchange (u_heads, pixel, n + 1u);
		u_nodes[n].order = u_order + uint (gl_PrimitiveID);
		u_nodes[n].mode = u_blend_mode;
	}
#endif
}
)glsl";

const char *const WHOLE_PICTURE_VERTEX = R"glsl(#version 430

void main (void) {
	gl_Position = vec4 (vec2 (gl_VertexID & 1, gl_VertexID >> 1) * 4.0 - 1.0,
	                    0.0, 1.0);
}
)glsl";

/* Blends the translucent fragments of every pixel over the colour below:
 * the farthest first and, at the same depth, in the order they were sent.
 * The arithmetic is that of an 8-bit colour buffer, each result rounded
 * to the nearest before the next (HEURISTIC: the precision of the blender
 * of the hardware is not known); the colour is held as 0 to 255 so that
 * every host computes the same. KEPT of them are put in order at a time,
 * the same order whatever KEPT is: 8 costs the Intel HD 520 and the
 * Radeon RX 6600 the least (half and two thirds of 32). */
const char *const SORT_FRAGMENT = R"glsl(#version 430

#define KEPT 8

)glsl"
FRAGMENT_LISTS
R"glsl(uniform sampler2D u_below;

out vec4 fragColor;

bool
comes_before (float depth_a, uint order_a, float depth_b, uint order_b)
{
	return depth_a > depth_b ||
	       (depth_a == depth_b && order_a < order_b);
}

void
main (void)
{
	ivec2 pixel = ivec2 (gl_FragCoord.xy);
	uint head = imageLoad (u_heads, pixel).r;
	float depth[KEPT], last_depth = 2.0;
	uint order[KEPT], index[KEPT], last_order = 0u, i;
	precise vec4 color;
	precise float through;
	bool more;
	int num, j;

	if (head == 0u)
		discard;
	color = floor (texelFetch (u_below, pixel, 0) * 255.0 + 0.5);

	for (;;) {
		num = 0;
		more = false;
		for (i = head; i != 0u; i = u_nodes[i - 1u].next) {
			float d = u_nodes[i - 1u].depth;
			uint o = u_nodes[i - 1u].order;

			if (!comes_before (last_depth, last_order, d, o))
				continue;
			if (num < KEPT) {
				j = num++;
			} else {
				more = true;
				if (!comes_before (d, o, depth[KEPT - 1], order[KEPT - 1]))
					continue;
				j = KEPT - 1;
			}
			for (; j > 0 && comes_before (d, o, depth[j - 1], order[j - 1]); j--) {
				depth[j] = depth[j - 1];
				order[j] = order[j - 1];
				index[j] = index[j - 1];
			}
			depth[j] = d;
			order[j] = o;
			index[j] = i - 1u;
		}

		for (j = 0; j < num; j++) {
			vec4 src = u_nodes[index[j]].color;
			uint mode = u_nodes[index[j]].mode;

			through = color.a * (1.0 - src.a);
			if (mode == 1u)
				color = vec4 (src.rgb * color.rgb, 0.0);
			else if (mode == 2u)
				color = vec4 (src.rgb * 255.0 + color.rgb, through);
			else
				color = vec4 (src.rgb * 255.0 + color.rgb * (1.0 - src.a), through);
			color = floor (clamp (color, 0.0, 255.0) + 0.5);
		}

		if (!more)
			break;
		last_depth = depth[KEPT - 1];
		last_order = order[KEPT - 1];
	}

	fragColor = color / 255.0;
}
)glsl";

/* The blender of the two 3D buffers, as the patent of the hardware draws it
 * (US 6,549,209, fig. 11): the rate of the buffer kept counts one more from
 * 0x80 up, 0xFF keeps it whole, and each product loses its fraction before
 * the sum. */
const char *const BLEND_FRAGMENT = R"glsl(#version 430

out vec4 fragColor;
uniform sampler2D u_drawn;
uniform sampler2D u_kept;
uniform int u_rate;

void main (void) {
	ivec2 at = ivec2 (gl_FragCoord.xy);
	ivec4 drawn = ivec4 (texelFetch (u_drawn, at, 0) * 255.0 + 0.5);
	ivec4 kept = ivec4 (texelFetch (u_kept, at, 0) * 255.0 + 0.5);
	int k = u_rate + (u_rate >> 7);

	fragColor = vec4 (((kept * k) >> 8) + ((drawn * (256 - k)) >> 8)) / 255.0;
}
)glsl";

const char *const COMPOSE_VERTEX = R"glsl(#version 140

#extension GL_ARB_explicit_attrib_location : require

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
)glsl";

const char *const COMPOSE_FRAGMENT = R"glsl(#version 140

out vec4 fragColor;
uniform sampler2D u_front;
uniform sampler2D u_back;
uniform sampler2D u_layer1;
uniform sampler2D u_layer2;
uniform float u_factor = 0.0;
uniform float u_mult_f = 1.0;
uniform float u_mult_b = 1.0;
uniform float u_mult_1 = 1.0;
uniform float u_mult_2 = 1.0;
uniform float u_keyed_1 = 1.0;
uniform float u_keyed_2 = 1.0;
uniform float u_keyed_3d = 0.0;
uniform float u_above_1 = 1.0;
uniform float u_above_2 = 1.0;
uniform int u_invert_f = 0;
uniform int u_invert_b = 0;
uniform vec4 u_clear = vec4 (0.0);

in vec2 p_texcoords;

void main (void) {
	vec2 p_texcoords_inv = vec2 (p_texcoords.s, 1 - p_texcoords.t);
	vec4 texel_f, texel_b;

	if (u_invert_f == 0)
		texel_f = texture (u_front, p_texcoords);
	else
		texel_f = texture (u_front, p_texcoords_inv);
	texel_f = texel_f * u_mult_f;

	if (u_invert_b == 0)
		texel_b = texture (u_back,  p_texcoords);
	else
		texel_b = texture (u_back,  p_texcoords_inv);
	texel_b = texel_b * u_mult_b;

	vec4 texel_1 = texture (u_layer1, p_texcoords_inv);
	vec4 texel_2 = texture (u_layer2, p_texcoords_inv);
	float on_1 = (u_mult_1 > 0.0) ? 1.0 : 0.0;
	float on_2 = (u_mult_2 > 0.0) ? 1.0 : 0.0;
	/* The key is the alpha bit: any non-zero alpha covers (the 2-bit
	 * alpha of the A2BGR10 layers of PHARRIER holds 1).
	 */
	float cover_1 = (u_keyed_1 > 0.0) ? step (0.1, texel_1.a) : 1.0;
	float cover_2 = (u_keyed_2 > 0.0) ? step (0.1, texel_2.a) : 1.0;
	/* The alpha of a 3D buffer is what it lets through; a layer taken
	 * as a blender input lets nothing through.
	 */
	if (u_invert_f != 0)
		texel_f.a = 0.0;
	if (u_invert_b != 0)
		texel_b.a = 0.0;
	vec4 color = mix (texel_f, texel_b, u_factor);
	/* The clear color shows where the 3D holds nothing, and covers
	 * what is below it when its alpha bit is set.
	 */
	color.rgb += color.a * u_clear.rgb;
	color.a *= 1.0 - u_clear.a;
	float cover_3d = (u_keyed_3d > 0.0) ? 1.0 - step (0.999, color.a) : 1.0;

	/* A layer pixel with its alpha bit covers the 3D, black included
	 * (the black bands of the Racer attract); the others are
	 * transparent whatever their color (its green 0x03E0 boxes).
	 * Layer 1 sits above layer 2 (its HUD text over the band).
	 * Cabinet footage, GPU-RE/16 § 10.
	 *
	 * A layer can sit below the 3D, which then shows it where nothing
	 * was drawn (the AIRTRIX logo screens).
	 */
	vec3 below = color.rgb;
	below = mix (below, texel_2.rgb * u_mult_2, cover_2 * on_2 * (1.0 - u_above_2));
	below = mix (below, texel_1.rgb * u_mult_1, cover_1 * on_1 * (1.0 - u_above_1));
	color.rgb = mix (below, color.rgb, cover_3d);
	color.rgb = mix (color.rgb, texel_2.rgb * u_mult_2, cover_2 * on_2 * u_above_2);
	color.rgb = mix (color.rgb, texel_1.rgb * u_mult_1, cover_1 * on_1 * u_above_1);
	fragColor = color;
}
)glsl";

/* Each pixel of the window takes the mean of the part of the picture it
 * covers, each pixel of the picture weighted by the share of it that is
 * covered. A window that is a whole number of times the picture shows it
 * pixel for pixel; a smaller one smooths it, which the hardware does not.
 * u_rect: where the rectangle starts in the window, and the pixels of the
 * picture one pixel of the window covers each way. */
const char *const PRESENT_FRAGMENT = R"glsl(#version 430

uniform sampler2D u_picture;
uniform vec4 u_rect;

out vec4 fragColor;

void main (void) {
	ivec2 last = textureSize (u_picture, 0) - 1;
	vec2 from = (gl_FragCoord.xy - 0.5 - u_rect.xy) * u_rect.zw;
	vec2 to = from + u_rect.zw;
	ivec2 a = ivec2 (floor (from)), b = ivec2 (ceil (to)) - 1;
	vec3 sum = vec3 (0.0);

	for (int y = a.y; y <= b.y; y++) {
		float wy = min (to.y, float (y + 1)) - max (from.y, float (y));
		for (int x = a.x; x <= b.x; x++) {
			float wx = min (to.x, float (x + 1)) - max (from.x, float (x));
			sum += texelFetch (u_picture, clamp (ivec2 (x, y), ivec2 (0), last), 0).rgb * (wx * wy);
		}
	}
	fragColor = vec4 (sum / (u_rect.z * u_rect.w), 1.0);
}
)glsl";

} /* namespace odin::shaders */
