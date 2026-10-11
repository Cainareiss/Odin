/* 
 * Valkyrie
 * Copyright (C) 2011-2013, Stefano Teso
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

#include "vk/input.h"

#include "mach/hikaru/hikaru-renderer.h"
#include "mach/hikaru/hikaru-renderer-private.h"

#define VP0	VP.current
#define MAT0	MAT.scratch
#define TEX0	TEX.scratch
#define LS0	LIT.scratchset

#define MAX_VIEWPORTS	4096
#define MAX_MODELVIEWS	8192
#define MAX_MATERIALS	4096
#define MAX_TEXHEADS	4096
#define MAX_LIGHTSETS	4096
#define MAX_MESHES	16384

#define INV255	(1.0f / 255.0f)

#define isnonnegative(x_) \
	(isfinite(x_) && (x_) >= 0.0)

/****************************************************************************
 Debug
****************************************************************************/

static const struct {
	int32_t min, max;
	uint32_t key;
} debug_controls[] = {
	[HR_DEBUG_LOG]			= {  0, 1,     ~0 },
	[HR_DEBUG_NO_LAYER1]		= {  0, 1, SDLK_1 },
	[HR_DEBUG_NO_LAYER2]		= {  0, 1, SDLK_2 },
	[HR_DEBUG_NO_3D]		= {  0, 1, SDLK_3 },
	[HR_DEBUG_SELECT_VIEWPORT]	= { -1, 7, SDLK_v },
	[HR_DEBUG_NO_TEXTURES]		= {  0, 1, SDLK_t },
	[HR_DEBUG_NO_MIPMAPS]		= {  0, 1, SDLK_u },
	[HR_DEBUG_SELECT_POLYTYPE]	= { -1, 7, SDLK_p },
	[HR_DEBUG_NO_INSTANCING]	= {  0, 1, SDLK_i },
	[HR_DEBUG_SELECT_INSTANCE]	= {  0, 3, SDLK_j }, 
	[HR_DEBUG_NO_LIGHTING]		= {  0, 1, SDLK_l },
	[HR_DEBUG_NO_AMBIENT]		= {  0, 1, SDLK_a },
	[HR_DEBUG_NO_DIFFUSE]		= {  0, 1, SDLK_d },
	[HR_DEBUG_NO_SPECULAR]		= {  0, 1, SDLK_s },
	[HR_DEBUG_NO_FOG]		= {  0, 1, SDLK_g },
};

static void
init_debug_flags (hikaru_renderer_t *hr)
{
	unsigned i;

	for (i = 0; i < NUMELEM (debug_controls); i++)
		hr->debug.flags[i] = debug_controls[i].min;

	hr->debug.flags[HR_DEBUG_LOG] =
		vk_util_get_bool_option ("HR_LOG", false) ? 1 : 0;
}

static void
update_debug_flags (hikaru_renderer_t *hr)
{
	char *msg = hr->base.message;
	unsigned i;

	for (i = 0; i < NUMELEM (debug_controls); i++) {
		uint32_t key = debug_controls[i].key;
		if (key != ~0 && vk_input_get_key (key)) {
			hr->debug.flags[i] += 1;
			if (i == HR_DEBUG_NO_MIPMAPS)
				hikaru_renderer_invalidate_texcache (&hr->base, NULL);
			if (hr->debug.flags[i] > debug_controls[i].max)
				hr->debug.flags[i] = debug_controls[i].min;
		}
		msg += sprintf (msg, "%d|", hr->debug.flags[i]);
	}
}

/****************************************************************************
 Utils
****************************************************************************/

static GLuint
get_uniform_loc (GLuint prog, const char *id)
{
	GLuint loc;

	loc = glGetUniformLocation (prog, id);
	VK_ASSERT_NO_GL_ERROR ();
	if (loc == (GLuint) -1)
		VK_ABORT ("uniform '%s' has no location!", id);

	return loc;
}

/****************************************************************************
 Textures
****************************************************************************/

static bool
is_texhead_eq (hikaru_renderer_t *hr,
               hikaru_texhead_t *a, hikaru_texhead_t *b)
{
	return (a->format == b->format) &&
	       (a->logw == b->logw) &&
	       (a->logh == b->logh) &&
	       (a->bank == b->bank) &&
	       (a->slotx == b->slotx) &&
	       (a->sloty == b->sloty);
}

/* A texture the graphics card may still be drawing with is not deleted at
 * once: the driver would wait for the card. It goes at the next frame. */
static void
bury_texture (hikaru_renderer_t *hr, GLuint id)
{
	if (!id)
		return;
	if (hr->textures.num_dead == hr->textures.max_dead) {
		hr->textures.max_dead = MAX2 (hr->textures.max_dead * 2, 256);
		hr->textures.dead = (GLuint *)
			realloc (hr->textures.dead,
			         hr->textures.max_dead * sizeof (GLuint));
		VK_ASSERT (hr->textures.dead);
	}
	hr->textures.dead[hr->textures.num_dead++] = id;
}

static void
delete_dead_textures (hikaru_renderer_t *hr)
{
	if (!hr->textures.num_dead)
		return;
	glDeleteTextures (hr->textures.num_dead, hr->textures.dead);
	VK_ASSERT_NO_GL_ERROR ();
	hr->textures.num_dead = 0;
}

static void
destroy_texture (hikaru_renderer_t *hr, hikaru_texture_t *tex)
{
	bury_texture (hr, tex->id);
	memset ((void *) tex, 0, sizeof (hikaru_texture_t));
}

/* A texel of the format 2 is one 4-bit value: the ramp PHARRIER draws its
 * red halo with goes 0, 2, 4, 6, 9, B, D, F across. The four channels get
 * it (HEURISTIC: the halo takes its alpha, the BOOTROM font, 0 or F, its
 * colour weight and its alpha). */
static uint16_t
i4_to_rgba4444 (uint32_t texel)
{
	return (texel & 15) * 0x1111;
}

/* A word holds a block of two by two texels: a texhead of w x h words
 * is a picture of 2w x 2h texels. The font of the BOOTROM menu is one of
 * 64 x 64 words, its glyphs 8 texels apart across and 16 down. */
static void *
decode_texture_i4 (vk_buffer_t *texram,
                         uint32_t w, uint32_t h,
                         uint32_t basex, uint32_t basey)
{
	uint32_t x, y;
	uint16_t *data;

	data = (uint16_t *) malloc ((w * 2) * (h * 2) * 2);
	if (!data)
		return NULL;

	for (y = 0; y < h; y++) {
		uint16_t *top = &data[(y * 2 + 0) * (w * 2)];
		uint16_t *bottom = &data[(y * 2 + 1) * (w * 2)];
		for (x = 0; x < w; x++) {
			uint32_t offs = (basey + y) * 4096 + (basex + x) * 2;
			uint32_t texels = vk_buffer_get (texram, 2, offs);
			top[x * 2 + 0] = i4_to_rgba4444 (texels >> 12);
			top[x * 2 + 1] = i4_to_rgba4444 (texels >> 8);
			bottom[x * 2 + 0] = i4_to_rgba4444 (texels >> 4);
			bottom[x * 2 + 1] = i4_to_rgba4444 (texels >> 0);
		}
	}
	return (void *) data;
}

/* Wrap and filter of the bound texture. They belong to the texhead, not to
 * the texels: two texheads may share a slot and differ only there, so this
 * runs on every bind, as the X360 port does (825C6C98, 825C6650). */
static void
set_texhead_sampling (hikaru_texhead_t *th)
{
	/* Mirror wins over repeat, else clamp. */
	glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_WRAP_S,
	                 th->mirroru ? GL_MIRRORED_REPEAT :
	                 th->repeatu ? GL_REPEAT : GL_CLAMP_TO_EDGE);
	glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_WRAP_T,
	                 th->mirrorv ? GL_MIRRORED_REPEAT :
	                 th->repeatv ? GL_REPEAT : GL_CLAMP_TO_EDGE);
	VK_ASSERT_NO_GL_ERROR ();

	/* Filter of the 0C1 mode; mode 3 is left at the sampler defaults in
	 * the port, trilinear here (HEURISTIC). */
	switch (th->filter) {
	case 0:
		glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
		glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
		break;
	case 2:
		glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
		glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
		break;
	default:
		glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
		glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
		break;
	}
	VK_ASSERT_NO_GL_ERROR ();
}

static GLuint
upload_texture (hikaru_renderer_t *hr, hikaru_texhead_t *th)
{
	static const GLint la8_swizzle[4] = { GL_RED, GL_RED, GL_RED, GL_GREEN };

	uint32_t w, h, num_levels, level, basex, basey, bank;
	GLuint id;

	w = 16 << th->logw;
	h = 16 << th->logh;
	/* The levels alternate between the two banks (see copy_texture),
	 * down to the level where the smaller side reaches one texel: the
	 * games upload no further (the sky dome of von4, 256x128, has a zero
	 * 1x1 level that the port would read, 825C6A00; HEURISTIC). */
	num_levels = hr->debug.flags[HR_DEBUG_NO_MIPMAPS] ? 1 :
	             MIN2 (th->logw, th->logh) + 5;

	get_texhead_coords (&basex, &basey, th);
	bank = th->bank;

	glGenTextures (1, &id);
	VK_ASSERT_NO_GL_ERROR ();

	glActiveTexture (GL_TEXTURE0 + 0);
	VK_ASSERT_NO_GL_ERROR ();

	glBindTexture (GL_TEXTURE_2D, id);
	VK_ASSERT_NO_GL_ERROR ();

	set_texhead_sampling (th);

	/* LOD bias and anisotropy of the X360 port (825C6650). */
	glTexParameterf (GL_TEXTURE_2D, GL_TEXTURE_LOD_BIAS, -0.5f);
	if (GLEW_EXT_texture_filter_anisotropic)
		glTexParameterf (GL_TEXTURE_2D, GL_TEXTURE_MAX_ANISOTROPY_EXT, 8.0f);
	VK_ASSERT_NO_GL_ERROR ();

	glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, num_levels - 1);
	VK_ASSERT_NO_GL_ERROR ();

	glPixelStorei (GL_UNPACK_ALIGNMENT, 1);
	VK_ASSERT_NO_GL_ERROR ();

	for (level = 0; level < num_levels; level++) {
		void *data = (void *) hr->gpu->texram[bank]->ptr;

		switch (th->format) {
		case HIKARU_FORMAT_ABGR1555:
			glPixelStorei (GL_UNPACK_ROW_LENGTH, 2048);
			glPixelStorei (GL_UNPACK_SKIP_ROWS, basey);
			glPixelStorei (GL_UNPACK_SKIP_PIXELS, basex);
			VK_ASSERT_NO_GL_ERROR ();
	
			glTexImage2D (GL_TEXTURE_2D, level,
			              GL_RGB5_A1,
			              w, h, 0,
			              GL_RGBA, GL_UNSIGNED_SHORT_1_5_5_5_REV,
			              data);
			VK_ASSERT_NO_GL_ERROR ();
			break;
		case HIKARU_FORMAT_ABGR4444:
			glPixelStorei (GL_UNPACK_ROW_LENGTH, 2048);
			glPixelStorei (GL_UNPACK_SKIP_ROWS, basey);
			glPixelStorei (GL_UNPACK_SKIP_PIXELS, basex);
			VK_ASSERT_NO_GL_ERROR ();
	
			glTexImage2D (GL_TEXTURE_2D, level,
			              GL_RGBA4,
			              w, h, 0,
			              GL_RGBA, GL_UNSIGNED_SHORT_4_4_4_4_REV,
			              data);
			VK_ASSERT_NO_GL_ERROR ();
			break;
		case HIKARU_FORMAT_LA8:
			glPixelStorei (GL_UNPACK_ROW_LENGTH, 2048);
			glPixelStorei (GL_UNPACK_SKIP_ROWS, basey);
			glPixelStorei (GL_UNPACK_SKIP_PIXELS, basex);
			VK_ASSERT_NO_GL_ERROR ();
	
			glTexImage2D (GL_TEXTURE_2D, level,
			              GL_RGBA8,
			              w, h, 0,
			              GL_RG, GL_UNSIGNED_BYTE,
			              data);
			VK_ASSERT_NO_GL_ERROR ();

			glTexParameteriv (GL_TEXTURE_2D, GL_TEXTURE_SWIZZLE_RGBA,
			                  la8_swizzle);
			VK_ASSERT_NO_GL_ERROR ();
			break;
		case HIKARU_FORMAT_I4:
			data = decode_texture_i4 (hr->gpu->texram[bank],
			                                w, h, basex, basey);
			if (!data)
				goto fail;

			glTexImage2D (GL_TEXTURE_2D, level,
			              GL_RGBA4,
			              w * 2, h * 2, 0,
			              GL_RGBA, GL_UNSIGNED_SHORT_4_4_4_4_REV,
			              data);
			VK_ASSERT_NO_GL_ERROR ();

			free (data);
			break;
		default:
			goto fail;
		}

		w = MAX2 (w >> 1, 1);
		h = MAX2 (h >> 1, 1);

		basex += (2048 - basex) / 2;
		basey += (1024 - basey) / 2;
		bank ^= 1;
	}

	glPixelStorei (GL_UNPACK_ROW_LENGTH, 0);
	glPixelStorei (GL_UNPACK_SKIP_ROWS, 0);
	glPixelStorei (GL_UNPACK_SKIP_PIXELS, 0);
	VK_ASSERT_NO_GL_ERROR ();

	return id;

fail:
	glPixelStorei (GL_UNPACK_ROW_LENGTH, 0);
	glPixelStorei (GL_UNPACK_SKIP_ROWS, 0);
	glPixelStorei (GL_UNPACK_SKIP_PIXELS, 0);
	VK_ASSERT_NO_GL_ERROR ();

	glDeleteTextures (1, &id);
	VK_ASSERT_NO_GL_ERROR ();

	return 0;
}

hikaru_texture_t *
get_texture (hikaru_renderer_t *hr, hikaru_texhead_t *th)
{
	hikaru_texture_t *cached;
	uint32_t bank, slotx, sloty;
	GLuint id;

	bank  = th->bank;
	slotx = th->slotx;
	sloty = th->sloty;

	if (slotx < 0x80 || sloty < 0xC0)
		return NULL;

	slotx -= 0x80;
	sloty -= 0xC0;

	cached = &hr->textures.cache[bank][sloty][slotx];
	if (is_texhead_eq (hr, th, &cached->th))
		return cached;
	LOG ("TEXUPLOAD %s", get_texhead_str (th));

	destroy_texture (hr, cached);

	id = upload_texture (hr, th);
	if (!id) {
		destroy_texture (hr, cached);
		return NULL;
	}

	cached->th = *th;
	cached->id = id;

	hr->textures.is_clear[bank] = false;
	return cached;
}

static void
clear_texcache_bank (hikaru_renderer_t *hr, unsigned bank)
{
	unsigned x, y;

	if (hr->textures.is_clear[bank])
		return;
	hr->textures.is_clear[bank] = true;

	/* Free all allocated surfaces. */
	for (y = 0; y < 0x40; y++)
		for (x = 0; x < 0x80; x++)
			destroy_texture (hr, &hr->textures.cache[bank][y][x]);

	/* Zero out the cache itself, to avoid spurious hits. Note that
	 * texture RAM origin is (80,C0), so (slotx, sloty) will never match
	 * a zeroed out cache entries. */
	memset ((void *) &hr->textures.cache[bank], 0, sizeof (hr->textures.cache[bank]));
}

void
hikaru_renderer_invalidate_texcache (vk_renderer_t *rend, hikaru_texhead_t *th)
{
	hikaru_renderer_t *hr = (hikaru_renderer_t *) rend;

	VK_ASSERT (hr);

	if (th == NULL) {
		clear_texcache_bank (hr, 0);
		clear_texcache_bank (hr, 1);
	} else
		clear_texcache_bank (hr, th->bank);
}

/****************************************************************************
 State
****************************************************************************/

/* The translucent fragments of a viewport, see draw_translucent_meshes:
 * u_heads has the last one of each pixel and next the one before it, as
 * indices plus one; 0 ends a list. */
#define LISTS_GLSL \
"struct node_t {								\n" \
"	vec4 color;								\n" \
"	float depth;								\n" \
"	uint next;								\n" \
"	uint order;								\n" \
"	uint mode;								\n" \
"};										\n" \
"layout(binding = 0, r32ui) uniform uimage2D u_heads;				\n" \
"layout(std430, binding = 0) buffer nodes_t {					\n" \
"	node_t u_nodes[];							\n" \
"};										\n"

#define NODE_SIZE 32	/* bytes of a node_t */

static const char *mesh_vs_source =
"%s										\n \
										\n \
#extension GL_ARB_explicit_attrib_location : require				\n \
										\n \
uniform mat4 u_projection;							\n \
uniform mat4 u_modelview;							\n \
uniform mat3 u_normal;								\n \
uniform mat4 u_modelview2;							\n \
uniform mat3 u_normal2;								\n \
uniform float u_depth_scale;							\n \
										\n \
layout(location = 0) in vec3 i_position;					\n \
layout(location = 1) in vec3 i_normal;						\n \
layout(location = 2) in vec3 i_diffuse;						\n \
layout(location = 3) in vec3 i_ambient;						\n \
layout(location = 4) in vec4 i_specular;					\n \
layout(location = 5) in vec3 i_color1;						\n \
layout(location = 6) in vec2 i_texcoords;					\n \
layout(location = 7) in float i_alpha;						\n \
layout(location = 8) in float i_second_mv;					\n \
										\n \
out vec4 p_position;								\n \
out vec3 p_normal;								\n \
out vec3 p_diffuse;								\n \
out vec4 p_specular;								\n \
out vec3 p_color1;								\n \
out vec3 p_ambient;								\n \
out vec2 p_texcoords;								\n \
out float p_alpha;								\n \
										\n \
void main (void) {								\n \
	if (i_second_mv > 0.875) {						\n \
		p_position = u_modelview2 * vec4 (i_position, 1.0);		\n \
		p_normal = u_normal2 * i_normal;				\n \
	} else if (i_second_mv < 0.125) {					\n \
		p_position = u_modelview * vec4 (i_position, 1.0);		\n \
		p_normal = u_normal * i_normal;					\n \
	} else {								\n \
		/* 64: without the 3x3, 128: without the last row. */		\n \
		bool rotate = i_second_mv > 0.375 && i_second_mv < 0.625;	\n \
		vec3 pos = rotate ? mat3 (u_modelview) * i_position : i_position;	\n \
		if (i_second_mv < 0.375)					\n \
			pos += u_modelview[3].xyz;				\n \
		p_position = vec4 (pos, 1.0);					\n \
		p_normal = rotate ? u_normal * i_normal : i_normal;		\n \
	}									\n \
	/* The depth bias brings the whole position nearer: same pixel. */	\n \
	gl_Position = u_projection * vec4 (p_position.xyz * u_depth_scale, 1.0);	\n \
										\n \
	p_diffuse = i_diffuse;							\n \
	p_ambient = i_ambient;							\n \
	p_specular = i_specular;						\n \
	p_color1 = i_color1;							\n \
	p_texcoords = i_texcoords;						\n \
	p_alpha = i_alpha;							\n \
}";

static const char *mesh_fs_source =
"%s										\n \
										\n \
out vec4 fragColor;								\n \
										\n \
struct light_t {								\n \
	vec3 position;								\n \
	vec3 direction;								\n \
	vec3 color;								\n \
	float ambient;								\n \
	vec2 extents;								\n \
	vec3 spot;								\n \
	vec2 cone;								\n \
};										\n \
										\n \
uniform light_t		u_lights[4];						\n \
uniform vec3		u_ambient;						\n \
uniform sampler2D	u_texture;						\n \
uniform vec2		u_fog;							\n \
uniform vec3		u_fog_color;						\n \
uniform float		u_alpha_ref;						\n \
uniform float		u_weigh_by_alpha;					\n \
										\n \
#if COLLECT									\n"
LISTS_GLSL
"layout(std430, binding = 1) buffer count_t {					\n \
	uint u_count;								\n \
};										\n \
uniform sampler2D	u_solid_depth;						\n \
uniform uint		u_order;						\n \
uniform uint		u_blend_mode;						\n \
#endif										\n \
										\n \
in vec4 p_position;								\n \
in vec3 p_normal;								\n \
in vec3 p_diffuse;								\n \
in vec4 p_specular;								\n \
in vec3 p_color1;								\n \
in vec3 p_ambient;								\n \
in vec2 p_texcoords;								\n \
in float p_alpha;								\n \
										\n \
void	\n \
apply_light (inout vec3 diffuse,	\n \
             inout vec3 specular,	\n \
             in light_t light,	\n \
             in int type,	\n \
             in int att_type)	\n \
{	\n \
	vec3 normal = normalize (p_normal);	\n \
	vec3 light_direction;	\n \
	float distance, attenuation, n_dot_l, r_dot_v;	\n \
	\n \
	if (type == 3) {	\n \
		diffuse += light.color;	\n \
		return;	\n \
	}	\n \
	\n \
	if (type == 0) {	\n \
		distance = length (light.direction);	\n \
		light_direction = light.direction / distance;	\n \
	} else {	\n \
		vec3 delta = light.position - p_position.xyz;	\n \
		distance = length (delta);	\n \
		light_direction = delta / distance;	\n \
	}	\n \
	\n \
	/* X360 825C5BA0: sat (P d + P Q), sat (P d^2 + P Q). The port does	\n \
	 * not attenuate the types 2 and 3; the hardware does, on 1 / d and	\n \
	 * 1 / d^2 (the far red light of the PHARRIER logo scene, which	\n \
	 * leaves the asteroids grey on the cabinet). */	\n \
	if (att_type == 0)	\n \
		attenuation = clamp (light.extents.x * (distance + light.extents.y), 0.0, 1.0);	\n \
	else if (att_type == 1)	\n \
		attenuation = clamp (light.extents.x * (distance * distance + light.extents.y), 0.0, 1.0);	\n \
	else if (att_type == 2)	\n \
		attenuation = clamp (light.extents.x * (1.0 / distance + light.extents.y), 0.0, 1.0);	\n \
	else if (att_type == 3)	\n \
		attenuation = clamp (light.extents.x * (1.0 / (distance * distance) + light.extents.y), 0.0, 1.0);	\n \
	else	\n \
		attenuation = 1.0;	\n \
	\n \
	/* X360 825C5498: sat ((L . axis - cos outer) / (cos inner - cos	\n \
	 * outer)), the axis pointing from the target to the light. */	\n \
	if (type == 2)	\n \
		attenuation *= clamp ((dot (light_direction, normalize (-light.spot)) - light.cone.x) * light.cone.y, 0.0, 1.0);	\n \
	\n \
#if HAS_TWO_SIDED_LIGHTING	\n \
	n_dot_l = abs (dot (normal, light_direction));	\n \
#else	\n \
	n_dot_l = dot (normal, light_direction);	\n \
#endif	\n \
	/* The ambient level of the 051 is a constant share of the colour,	\n \
	 * cut by the distance and by the cone like the Lambert term	\n \
	 * (HEURISTIC: the scale and the cuts are read from the cabinets). */	\n \
	diffuse += attenuation * (max (n_dot_l, 0.0) + light.ambient) * light.color;	\n \
	\n \
	/* The reflected ray against the view. The vo3d pixel shader takes	\n \
	 * the Blinn half vector, whose lobe is about four times as wide for	\n \
	 * the same exponent: it washes out the PHARRIER logo seen from the	\n \
	 * front, which the cabinet shows plain blue. */	\n \
	r_dot_v = dot (reflect (-light_direction, normal), -normalize (p_position.xyz));	\n \
	if (n_dot_l > 0.0 && r_dot_v > 0.0)	\n \
		specular += attenuation * pow (r_dot_v, 255.0 * p_specular.a) * light.color;	\n \
}	\n \
	\n \
void	\n \
main (void)	\n \
{	\n \
	vec4 color;	\n \
	vec3 base;	\n \
	float alpha;	\n \
	\n \
	/* The depth buffer holds the logarithm of the eye depth: with the	\n \
	 * near planes the games send, the projected depth cannot tell apart	\n \
	 * surfaces that the cabinets do (the shadow of the AIRTRIX combo	\n \
	 * labels lies 0.06 behind them at a depth of 560). */			\n \
	float frag_depth = log2 (1.0 + 1.0 / gl_FragCoord.w) * (1.0 / 24.0);	\n \
	gl_FragDepth = frag_depth;						\n \
	\n \
#if HAS_TEXTURE	\n \
	vec4 texel = texture (u_texture, p_texcoords);	\n \
#if ALPHA_MODE	\n \
	/* 881 bit a: the vertex alpha blends the texel over the 091 colour	\n \
	 * (vo3d.0x01100). */	\n \
	base = mix (p_diffuse, texel.rgb, p_alpha);	\n \
	alpha = texel.a;	\n \
#elif TEX_MODE == 1	\n \
	/* The texel alpha blends the texel over the 091 colour, the polygon	\n \
	 * alpha alone remains (vo3d.0x01010). */	\n \
	base = mix (p_diffuse, texel.rgb, texel.a);	\n \
	alpha = p_alpha;	\n \
#elif TEX_MODE == 2	\n \
	/* The texel modulates the 291 colour: the vo3d.0x01020 vertex	\n \
	 * shader sends ve_polygon_color1, not color0. */	\n \
	base = texel.rgb * p_color1;	\n \
	alpha = texel.a * p_alpha;	\n \
#elif TEX_MODE == 3	\n \
	/* The 091 colour with the texel alpha (vo3d.0x01030). */	\n \
	base = p_diffuse;	\n \
	alpha = texel.a * p_alpha;	\n \
#elif TEX_MODE == 4	\n \
	/* The texel blends the 291 colour over the 091 colour (vo3d.0x01040). */	\n \
	base = mix (p_diffuse, p_color1, texel.rgb);	\n \
	alpha = texel.a * p_alpha;	\n \
#else	\n \
	base = texel.rgb;	\n \
	alpha = texel.a * p_alpha;	\n \
#endif	\n \
#else	\n \
#if ALPHA_MODE	\n \
	/* 881 bit a: the vertex alpha blends the 291 colour over the 091	\n \
	 * colour (vo3d.0x00100). */	\n \
	base = mix (p_diffuse, p_color1, p_alpha);	\n \
	alpha = 1.0;	\n \
#else	\n \
	base = p_diffuse;	\n \
	alpha = p_alpha;	\n \
#endif	\n \
#endif	\n \
	\n \
#if HAS_LIGHTING	\n \
	/* vo3d: (ambient + diffuse + emission) * base + specular * material;	\n \
	 * mode 1 has the ambient, mode 2 the emission, mode 3 neither and no	\n \
	 * specular (X360 825B4DE8). */	\n \
	vec3 diffuse  = vec3 (0.0);	\n \
	vec3 specular = vec3 (0.0);	\n \
	vec3 ambient  = (SHADING_MODE == 1) ? u_ambient * p_ambient : vec3 (0.0);	\n \
	vec3 emission = (SHADING_MODE == 2) ? p_ambient : vec3 (0.0);	\n \
	\n \
#if HAS_LIGHT0	\n \
	apply_light (diffuse, specular, u_lights[0], LIGHT0_TYPE, LIGHT0_ATT_TYPE);	\n \
#endif	\n \
#if HAS_LIGHT1	\n \
	apply_light (diffuse, specular, u_lights[1], LIGHT1_TYPE, LIGHT1_ATT_TYPE);	\n \
#endif	\n \
#if HAS_LIGHT2	\n \
	apply_light (diffuse, specular, u_lights[2], LIGHT2_TYPE, LIGHT2_ATT_TYPE);	\n \
#endif	\n \
#if HAS_LIGHT3	\n \
	apply_light (diffuse, specular, u_lights[3], LIGHT3_TYPE, LIGHT3_ATT_TYPE);	\n \
#endif	\n \
	\n \
	color = vec4 ((ambient + diffuse + emission) * base, alpha);	\n \
	if (SHADING_MODE != 3) {	\n \
		color.rgb += specular * p_specular.rgb * (255.0 / 64.0);	\n \
	}	\n \
#else	\n \
	color = vec4 (base, alpha);	\n \
#endif	\n \
	\n \
										\n \
	/* Alpha test of the render pass (X360 825B2408). */			\n \
	if (color.a <= u_alpha_ref)						\n \
		discard;							\n \
										\n \
#if COLLECT									\n \
	/* A translucent fragment is kept unless it is behind the other	\n \
	 * types, whose depth buffer has steps of 6e-8. */			\n \
	ivec2 pixel = ivec2 (gl_FragCoord.xy);					\n \
	if (frag_depth >= 1.0 ||						\n \
	    frag_depth > texelFetch (u_solid_depth, pixel, 0).r + 3.0e-8)	\n \
		discard;							\n \
#endif										\n \
										\n \
#if HAS_FOG									\n \
	/* X360 vo3d: linear fog on the eye depth from start to end. */	\n \
	float fog = clamp ((-p_position.z - u_fog.x) * u_fog.y, 0.0, 1.0);	\n \
	fragColor = vec4 (mix (color.rgb, u_fog_color, fog), color.a);		\n \
#else										\n \
	fragColor = color;							\n \
#endif										\n \
										\n \
#if HAS_CUE									\n \
	/* Depth queue without a colour: the pixel fades into what is behind	\n \
	 * it, linearly on the eye depth from start to end. */			\n \
	float cue = clamp ((-p_position.z - u_fog.x) * u_fog.y, 0.0, 1.0);	\n \
	if (cue >= 1.0)								\n \
		discard;							\n \
	fragColor.a = 1.0 - cue;						\n \
#endif										\n \
										\n \
	/* A blended colour is weighted by its alpha before it is clamped: a	\n \
	 * lit colour above 1 keeps its excess (the galaxy of the PHARRIER	\n \
	 * logo scene, whose veils burn to white on the cabinet). The blend	\n \
	 * function takes the source as it is then. */				\n \
	if (u_weigh_by_alpha > 0.5)						\n \
		fragColor.rgb *= fragColor.a;					\n \
										\n \
#if COLLECT									\n \
	uint n = atomicAdd (u_count, 1u);					\n \
	if (n < uint (u_nodes.length ())) {					\n \
		u_nodes[n].color = clamp (fragColor, 0.0, 1.0);			\n \
		u_nodes[n].depth = frag_depth;					\n \
		u_nodes[n].next = imageAtomicExchange (u_heads, pixel, n + 1u);	\n \
		u_nodes[n].order = u_order + uint (gl_PrimitiveID);		\n \
		u_nodes[n].mode = u_blend_mode;					\n \
	}									\n \
#endif										\n \
}";

static hikaru_light_att_t
get_light_attenuation_type (hikaru_light_t *lit)
{
	if (lit->att_type == 0 &&
	    lit->attenuation[0] == 1.0f &&
	    lit->attenuation[1] == 1.0f)
		return HIKARU_LIGHT_ATT_INF;
	return lit->att_type;
}

static hikaru_light_type_t
get_light_type (hikaru_light_t *lit)
{
	/* Violated by BRAVEFF, in the intro sequence; it recalls a lightset
	 * at the wrong base, and poof. */
	if (!lit->has_direction && !lit->has_position)
		VK_ERROR ("recalling light with no direction nor position!");
	if (get_light_attenuation_type (lit) == HIKARU_LIGHT_ATT_INF) {
		/* A parallel light sent with a null direction lights every
		 * side (HEURISTIC, read from the BRAVEFF cabinet: the light
		 * its stages add to every lightset). */
		if (lit->has_direction && lit->direction[0] == 0.0f &&
		    lit->direction[1] == 0.0f && lit->direction[2] == 0.0f)
			return HIKARU_LIGHT_TYPE_UNDIRECTED;
		return HIKARU_LIGHT_TYPE_DIRECTIONAL;
	} else if (lit->has_position && lit->has_spot && lit->has_cone)
		return HIKARU_LIGHT_TYPE_SPOT;
	else if (lit->has_position)
		return HIKARU_LIGHT_TYPE_POSITIONAL;
	return HIKARU_LIGHT_TYPE_DIRECTIONAL;
}

/* A depth queue without a colour (621 bit 17) fades the polygons that do not
 * blend into what is behind them (HEURISTIC, read from the PHARRIER cabinet:
 * its demo play loses the far scenery and keeps the mountains, sent as
 * translucent polygons). */
static bool
is_faded_by_depth (hikaru_renderer_t *hr, hikaru_mesh_t *mesh)
{
	hikaru_viewport_t *vp;

	if (mesh->vp_index >= MAX_VIEWPORTS)
		return false;
	vp = &hr->list->vp_list[mesh->vp_index];

	return vp->depth.q_behind && vp->depth.q_type == 0 &&
	       vp->depth.inv_range > 0.0f && vp->depth.near_by_edge > 0.0f &&
	       mesh->polytype != HIKARU_POLYTYPE_BACKGROUND &&
	       mesh->polytype != HIKARU_POLYTYPE_TRANSLUCENT &&
	       !hr->debug.flags[HR_DEBUG_NO_FOG];
}

static hikaru_glsl_variant_t
get_glsl_variant (hikaru_renderer_t *hr, hikaru_mesh_t *mesh)
{
	hikaru_material_t *mat	= &hr->list->mat_list[mesh->mat_index];
	hikaru_lightset_t *ls	= &hr->list->ls_list[mesh->ls_index];
	hikaru_viewport_t *vp	= &hr->list->vp_list[mesh->vp_index];
	hikaru_glsl_variant_t variant;

	VK_ASSERT (mesh->vp_index != ~0);

	variant.full = 0;
	variant.collect = hr->meshes.collect;

	if (mesh->mat_index == ~0) {
		VK_ERROR ("no material.");
		return variant;
	}
	if (mesh->tex_index == ~0) {
		VK_ERROR ("no texhead.");
		return variant;
	}
	if (mesh->ls_index == ~0) {
		VK_ERROR ("no lightset.");
		return variant;
	}

	variant.has_texture	= mat->has_texture &&
	                          !hr->debug.flags[HR_DEBUG_NO_TEXTURES];

	variant.has_lighting	= ls->mask != 0xF &&
	                          mat->shading_mode != 0 &&
	                          !hr->debug.flags[HR_DEBUG_NO_LIGHTING];

	/* The X360 port picks the fogged vo3d variant on the 881 z bit alone
	 * (shader option 11, digit 1). */
	variant.has_fog		= mat->depth_blend &&
	                          !vp->depth.q_behind &&
	                          !hr->debug.flags[HR_DEBUG_NO_FOG];
	variant.has_cue		= is_faded_by_depth (hr, mesh);

	/* The alpha mode and the texel mode apply to unlit materials too
	 * (vo3d.x1xxxxx variants). */
	variant.alpha_mode		= mat->has_alpha;

	/* The 2C1 bits 31:29 pick how the texel combines with the material
	 * colours, numbered as the port's shader variants (8252E368 byte 6,
	 * 825B4340): values 1, 2, 4 and 5 select modes 1 to 4; 3, 6 and 7
	 * keep the previous mode there, plain texturing here (HEURISTIC). */
	if (variant.has_texture) {
		hikaru_texhead_t *th = &hr->list->tex_list[mesh->tex_index];
		static const uint8_t modes[8] = { 0, 1, 2, 0, 3, 4, 0, 0 };
		variant.tex_mode = modes[th->texmode];
	}

	if (!variant.has_lighting)
		return variant;

	variant.shading_mode		= mat->shading_mode;

	variant.has_light0		= !(ls->mask & (1 << 0));
	variant.light0_type		= get_light_type (&ls->lights[0]);
	variant.light0_att_type		= get_light_attenuation_type (&ls->lights[0]);

	variant.has_light1		= !(ls->mask & (1 << 1));
	variant.light1_type		= get_light_type (&ls->lights[1]);
	variant.light1_att_type		= get_light_attenuation_type (&ls->lights[1]);

	variant.has_light2		= !(ls->mask & (1 << 2));
	variant.light2_type		= get_light_type (&ls->lights[2]);
	variant.light2_att_type		= get_light_attenuation_type (&ls->lights[2]);

	variant.has_light3		= !(ls->mask & (1 << 3));
	variant.light3_type		= get_light_type (&ls->lights[3]);
	variant.light3_att_type		= get_light_attenuation_type (&ls->lights[3]);

	if (mesh->has_two_sided_lighting)
		variant.has_two_sided_lighting = 1;

	return variant;
}

#define MAX_PROGRAMS	256

static struct {
	hikaru_glsl_variant_t variant;
	GLuint program;
	hikaru_mesh_locs_t locs;
	hikaru_mesh_held_t held;
} program_cache[MAX_PROGRAMS];
unsigned num_programs;

static void
upload_glsl_program (hikaru_renderer_t *hr, hikaru_mesh_t *mesh)
{
	static const char *definitions_template =
	"#version %d\n"
	"#define HAS_TEXTURE %d\n"
	"#define HAS_LIGHTING %d\n"
	"#define HAS_TWO_SIDED_LIGHTING %d\n"
	"#define SHADING_MODE %d\n"
	"#define ALPHA_MODE %d\n"
	"#define TEX_MODE %d\n"
	"#define HAS_LIGHT0 %d\n"
	"#define LIGHT0_TYPE %d\n"
	"#define LIGHT0_ATT_TYPE %d\n"
	"#define HAS_LIGHT1 %d\n"
	"#define LIGHT1_TYPE %d\n"
	"#define LIGHT1_ATT_TYPE %d\n"
	"#define HAS_LIGHT2 %d\n"
	"#define LIGHT2_TYPE %d\n"
	"#define LIGHT2_ATT_TYPE %d\n"
	"#define HAS_LIGHT3 %d\n"
	"#define LIGHT3_TYPE %d\n"
	"#define LIGHT3_ATT_TYPE %d\n"
	"#define HAS_FOG %d\n"
	"#define HAS_CUE %d\n"
	"#define COLLECT %d\n";

	hikaru_glsl_variant_t variant;
	char *definitions, *vs_source, *fs_source;
	int ret, i;

	variant = get_glsl_variant (hr, mesh);
	if (hr->meshes.variant.full == variant.full)
		return;

	hr->meshes.variant.full = variant.full;

	for (i = 0; i < num_programs; i++) {
		if (program_cache[i].variant.full == variant.full) {
			hr->meshes.program = program_cache[i].program;
			hr->meshes.locs = program_cache[i].locs;
			hr->meshes.held = &program_cache[i].held;
			glUseProgram (hr->meshes.program);
			VK_ASSERT_NO_GL_ERROR ();
			return;
		}
	}

	VK_LOG ("compiling shader for variant %lX", variant.full);

	ret = asprintf (&definitions, definitions_template,
	                variant.collect ? 430 : 140,
	                variant.has_texture,
	                variant.has_lighting,
	                variant.has_two_sided_lighting,
	                variant.shading_mode,
	                variant.alpha_mode,
	                variant.tex_mode,
	                variant.has_light0,
	                variant.light0_type,
	                variant.light0_att_type,
	                variant.has_light1,
	                variant.light1_type,
	                variant.light1_att_type,
	                variant.has_light2,
	                variant.light2_type,
	                variant.light2_att_type,
	                variant.has_light3,
	                variant.light3_type,
	                variant.light3_att_type,
	                variant.has_fog,
	                variant.has_cue,
	                variant.collect);
	VK_ASSERT (ret >= 0);

	ret = asprintf (&vs_source, mesh_vs_source, definitions);
	VK_ASSERT (ret >= 0);

	ret = asprintf (&fs_source, mesh_fs_source, definitions);
	VK_ASSERT (ret >= 0);

	hr->meshes.variant = variant;
	hr->meshes.program = vk_renderer_compile_program (vs_source, fs_source);

	free (definitions);
	free (vs_source);
	free (fs_source);

	if (0) {
		vk_renderer_print_uniforms (hr->meshes.program);
		VK_ASSERT_NO_GL_ERROR ();
	}

	glUseProgram (hr->meshes.program);
	VK_ASSERT_NO_GL_ERROR ();

	hr->meshes.locs.u_projection =
		glGetUniformLocation (hr->meshes.program, "u_projection");
	hr->meshes.locs.u_modelview =
		glGetUniformLocation (hr->meshes.program, "u_modelview");
	hr->meshes.locs.u_normal =
		glGetUniformLocation (hr->meshes.program, "u_normal");
	hr->meshes.locs.u_depth_scale =
		glGetUniformLocation (hr->meshes.program, "u_depth_scale");
	hr->meshes.locs.u_modelview2 =
		glGetUniformLocation (hr->meshes.program, "u_modelview2");
	hr->meshes.locs.u_normal2 =
		glGetUniformLocation (hr->meshes.program, "u_normal2");
	for (i = 0; i < 4; i++) {
		char temp[64];

		sprintf (temp, "u_lights[%d].position", i);
		hr->meshes.locs.u_lights[i].position =
			glGetUniformLocation (hr->meshes.program, temp);
		sprintf (temp, "u_lights[%d].direction", i);
		hr->meshes.locs.u_lights[i].direction =
			glGetUniformLocation (hr->meshes.program, temp);
		sprintf (temp, "u_lights[%d].color", i);
		hr->meshes.locs.u_lights[i].color =
			glGetUniformLocation (hr->meshes.program, temp);
		sprintf (temp, "u_lights[%d].ambient", i);
		hr->meshes.locs.u_lights[i].ambient =
			glGetUniformLocation (hr->meshes.program, temp);
		sprintf (temp, "u_lights[%d].spot", i);
		hr->meshes.locs.u_lights[i].spot =
			glGetUniformLocation (hr->meshes.program, temp);
		sprintf (temp, "u_lights[%d].cone", i);
		hr->meshes.locs.u_lights[i].cone =
			glGetUniformLocation (hr->meshes.program, temp);
		sprintf (temp, "u_lights[%d].extents", i);
		hr->meshes.locs.u_lights[i].extents =
			glGetUniformLocation (hr->meshes.program, temp);
	}
	hr->meshes.locs.u_ambient =
		glGetUniformLocation (hr->meshes.program, "u_ambient");
	hr->meshes.locs.u_texture =
		glGetUniformLocation (hr->meshes.program, "u_texture");
	hr->meshes.locs.u_fog =
		glGetUniformLocation (hr->meshes.program, "u_fog");
	hr->meshes.locs.u_fog_color =
		glGetUniformLocation (hr->meshes.program, "u_fog_color");
	hr->meshes.locs.u_alpha_ref =
		glGetUniformLocation (hr->meshes.program, "u_alpha_ref");
	hr->meshes.locs.u_weigh_by_alpha =
		glGetUniformLocation (hr->meshes.program, "u_weigh_by_alpha");
	hr->meshes.locs.u_solid_depth =
		glGetUniformLocation (hr->meshes.program, "u_solid_depth");
	hr->meshes.locs.u_order =
		glGetUniformLocation (hr->meshes.program, "u_order");
	hr->meshes.locs.u_blend_mode =
		glGetUniformLocation (hr->meshes.program, "u_blend_mode");
	VK_ASSERT_NO_GL_ERROR ();

	/* The texture units never change. */
	glUniform1i (hr->meshes.locs.u_texture, 0);
	glUniform1i (hr->meshes.locs.u_solid_depth, 6);
	VK_ASSERT_NO_GL_ERROR ();

	program_cache[num_programs].variant.full = variant.full;
	program_cache[num_programs].program = hr->meshes.program;
	program_cache[num_programs].locs = hr->meshes.locs;
	program_cache[num_programs].held.frame = ~0;
	hr->meshes.held = &program_cache[num_programs].held;
	num_programs++;
	VK_ASSERT (num_programs < MAX_PROGRAMS);
}

static void
upload_viewport (hikaru_renderer_t *hr, hikaru_mesh_t *mesh)
{
	hikaru_viewport_t *vp = &hr->list->vp_list[mesh->vp_index];
	/* The projection plane is clip.f pixels away on the 640x480 screen
	 * for x and clip.f2 pixels away for y. */
	const float kx = vp->clip.n / vp->clip.f;
	const float ky = vp->clip.n / vp->clip.f2;
	mtx4x4f_t projection;

	VK_ASSERT (mesh->vp_index != ~0);

	/* The clip window is cut by the scissor, whatever the program. */
	if (hr->meshes.scissor_vp_index != mesh->vp_index) {
		hr->meshes.scissor_vp_index = mesh->vp_index;
		glScissor ((GLint) vp->clip.l, 480 - (GLint) vp->clip.t,
		           (GLsizei) (vp->clip.r - vp->clip.l),
		           (GLsizei) (vp->clip.t - vp->clip.b));
	}

	if (hr->meshes.held->vp_index == mesh->vp_index)
		return;
	hr->meshes.held->vp_index = mesh->vp_index;

	if (!isnonnegative (vp->clip.l) || !isnonnegative (vp->clip.r) ||
	    !isnonnegative (vp->clip.b) || !isnonnegative (vp->clip.t) ||
	    !isnonnegative (vp->clip.f) || !isnonnegative (vp->clip.n)) {
		VK_ERROR ("non-positive viewport clipping planes: %s",
		          get_viewport_str (vp));
		/* continue anyway */
	}

	if ((vp->clip.l >= vp->clip.r) ||
	    (vp->clip.b >= vp->clip.t) ||
	    (vp->clip.n >= vp->clip.f)) {
		VK_ERROR ("inverted viewport clipping planes: %s",
		          get_viewport_str (vp));
		/* continue anyway */
	}

	LOG ("vp  = %s", get_viewport_str (vp));

	/* The centre and the clip window are screen coordinates, y down: the
	 * view is centred on the offset, reaches the four edges of the
	 * screen and is cut by the window. The driver panels of SGNASCAR
	 * sit 40 lines below the middle and stop under their banner; the
	 * letterboxed robots of von4 fill the lines 39 to 360, as on the
	 * cabinets. The BOOTROM menu has its centre on the top left corner
	 * of the screen. */
	vk_renderer_frustum (projection,
	                     (0.0f - vp->offset.x) * kx,
	                     (640.0f - vp->offset.x) * kx,
	                     (vp->offset.y - 480.0f) * ky,
	                     vp->offset.y * ky,
	                     vp->clip.n, 1e5);

	glUniformMatrix4fv (hr->meshes.locs.u_projection, 1, GL_FALSE,
	                    (const GLfloat *) projection);

	if (hr->meshes.variant.has_cue) {
		/* 621 words without a colour: P = 1 / (end - start),
		 * Q = near / start (PHARRIER library 0C015A3E). */
		vec2f_t cue;

		cue[0] = vp->clip.n / vp->depth.near_by_edge;
		cue[1] = vp->depth.inv_range;
		glUniform2fv (hr->meshes.locs.u_fog, 1, cue);
	}

	if (hr->meshes.variant.has_fog) {
		/* 621 words: P = 1 / (end - start), Q = near / end (von4
		 * library 0C716320); the port fogs linearly from start to end. */
		float p = vp->depth.inv_range, q = vp->depth.near_by_edge;
		vec2f_t fog = { 0.0f, 0.0f };
		vec3f_t fog_color;

		if (p > 0.0f && q > 0.0f) {
			fog[0] = vp->clip.n / q - 1.0f / p;
			fog[1] = p;
		}
		glUniform2fv (hr->meshes.locs.u_fog, 1, fog);

		fog_color[0] = vp->depth.mask[0] * INV255;
		fog_color[1] = vp->depth.mask[1] * INV255;
		fog_color[2] = vp->depth.mask[2] * INV255;
		glUniform3fv (hr->meshes.locs.u_fog_color, 1, fog_color);
	}
}

static void
upload_modelview (hikaru_renderer_t *hr, hikaru_mesh_t *mesh, unsigned i)
{
	hikaru_modelview_t *mv = &hr->list->mv_list[mesh->mv_index + i];
	mtx3x3f_t normal;

	if (mesh->mv_index != ~0 &&
	    hr->meshes.held->mv_index == mesh->mv_index + i &&
	    hr->meshes.held->mv2_index == mesh->mv2_index)
		return;
	hr->meshes.held->mv_index = (mesh->mv_index != ~0) ? mesh->mv_index + i : ~0;
	hr->meshes.held->mv2_index = mesh->mv2_index;

	if (mesh->mv_index == ~0) {
		static const hikaru_modelview_t identity_mv = {
			.mtx = {
				{ 1.0f, 0.0f, 0.0f, 0.0f },
				{ 0.0f, 1.0f, 0.0f, 0.0f },
				{ 0.0f, 0.0f, 1.0f, 0.0f },
				{ 0.0f, 0.0f, 0.0f, 1.0f }
			}
		};

		VK_ERROR ("attempting to draw with no modelview!");

		/* Attempt to render something anyway. */
		mv = (hikaru_modelview_t *) &identity_mv;
	}

	vk_renderer_compute_normal_matrix (normal, mv->mtx);

	LOG ("mv  = [%u+%u] %s", mesh->mv_index, i, get_modelview_str (mv));
	glUniformMatrix4fv (hr->meshes.locs.u_modelview, 1, GL_FALSE,
	                    (const GLfloat *) mv->mtx);

	glUniformMatrix3fv (hr->meshes.locs.u_normal, 1, GL_FALSE,
	                    (const GLfloat *) normal);

	/* The second modelview, for the vertices that follow it. */
	if (mesh->mv2_index < MAX_MODELVIEWS)
		mv = &hr->list->mv_list[mesh->mv2_index];
	vk_renderer_compute_normal_matrix (normal, mv->mtx);
	glUniformMatrix4fv (hr->meshes.locs.u_modelview2, 1, GL_FALSE,
	                    (const GLfloat *) mv->mtx);
	glUniformMatrix3fv (hr->meshes.locs.u_normal2, 1, GL_FALSE,
	                    (const GLfloat *) normal);
}

static void
upload_material_texhead (hikaru_renderer_t *hr, hikaru_mesh_t *mesh)
{
	hikaru_texture_t *tex;

	if (!hr->meshes.variant.has_texture)
		return;

	/* The texture of a texhead and its sampling stay bound. */
	if (hr->meshes.bound_tex_index == mesh->tex_index)
		return;
	hr->meshes.bound_tex_index = mesh->tex_index;

	tex = get_texture (hr, &hr->list->tex_list[mesh->tex_index]);

	glActiveTexture (GL_TEXTURE0 + 0);
	VK_ASSERT_NO_GL_ERROR ();

	glBindTexture (GL_TEXTURE_2D, tex ? tex->id : 0);
	VK_ASSERT_NO_GL_ERROR ();

	if (tex)
		set_texhead_sampling (&hr->list->tex_list[mesh->tex_index]);
}

static void
get_light_ambient (hikaru_renderer_t *hr, hikaru_mesh_t *mesh, float *out)
{
	hikaru_viewport_t *vp = (mesh->vp_index == ~0) ? NULL :
	                        &hr->list->vp_list[mesh->vp_index];

	if (hr->debug.flags[HR_DEBUG_NO_AMBIENT] || !vp)
		out[0] = out[1] = out[2] = 0.0f;
	else {
		out[0] = vp->color.ambient[0] * INV255;
		out[1] = vp->color.ambient[1] * INV255;
		out[2] = vp->color.ambient[2] * INV255;
	}
}

static void
get_light_diffuse (hikaru_renderer_t *hr, hikaru_light_t *lit, float *out)
{
	if (hr->debug.flags[HR_DEBUG_NO_DIFFUSE])
		out[0] = out[1] = out[2] = 0.0f;
	else {
		out[0] = lit->diffuse[0] * INV255;
		out[1] = lit->diffuse[1] * INV255;
		out[2] = lit->diffuse[2] * INV255;
	}
}

static void
upload_lightset (hikaru_renderer_t *hr, hikaru_mesh_t *mesh)
{
	hikaru_lightset_t *ls = &hr->list->ls_list[mesh->ls_index];
	float tmp[4];
	unsigned i;

	if (!hr->meshes.variant.has_lighting)
		return;

	/* The ambient colour comes from the viewport. */
	if (hr->meshes.held->ls_index == mesh->ls_index &&
	    hr->meshes.held->ls_vp_index == mesh->vp_index)
		return;
	hr->meshes.held->ls_index = mesh->ls_index;
	hr->meshes.held->ls_vp_index = mesh->vp_index;

	LOG ("lightset [%u] mask=%X = %s", mesh->ls_index, ls->mask,
	     get_lightset_str (ls));

	get_light_ambient (hr, mesh, tmp);
	glUniform3fv (hr->meshes.locs.u_ambient, 1, (const GLfloat *) tmp);
	VK_ASSERT_NO_GL_ERROR ();

	for (i = 0; i < 4; i++) {
		hikaru_light_t *lt = &ls->lights[i];

		if (ls->mask & (1 << i))
			continue;

		glUniform3fv (hr->meshes.locs.u_lights[i].position, 1,
		              lt->position);
		VK_ASSERT_NO_GL_ERROR ();

		glUniform3fv (hr->meshes.locs.u_lights[i].direction, 1,
		              lt->direction);
		VK_ASSERT_NO_GL_ERROR ();

		get_light_diffuse (hr, lt, tmp);
		glUniform3fv (hr->meshes.locs.u_lights[i].color, 1, tmp);
		glUniform1f (hr->meshes.locs.u_lights[i].ambient,
		             lt->ambient * (1.0f / 16.0f));
		VK_ASSERT_NO_GL_ERROR ();

		glUniform2fv (hr->meshes.locs.u_lights[i].extents, 1,
		              lt->attenuation);

		glUniform3fv (hr->meshes.locs.u_lights[i].spot, 1, lt->spot);
		tmp[0] = lt->cos_outer;
		tmp[1] = lt->cone_scale;
		glUniform2fv (hr->meshes.locs.u_lights[i].cone, 1, tmp);
		VK_ASSERT_NO_GL_ERROR ();
	}
}

/****************************************************************************
 Meshes
****************************************************************************/

#define VK_COPY_VEC2F(dst_, src_) \
	do { \
		dst_[0] = src_[0]; \
		dst_[1] = src_[1]; \
	} while (0)

#define VK_COPY_VEC3F(dst_, src_) \
	do { \
		dst_[0] = src_[0]; \
		dst_[1] = src_[1]; \
		dst_[2] = src_[2]; \
	} while (0)

static float
clampf (float x, float min_, float max_)
{
	return (min_ > x) ? min_ :
	       (max_ < x) ? max_ : x;
}

static void
copy_colors (hikaru_renderer_t *hr, hikaru_vertex_t *dst, hikaru_vertex_t *src)
{
	hikaru_gpu_t *gpu = hr->gpu;
	hikaru_material_t *mat = &MAT.scratch;
	float alpha;
	unsigned i;

	for (i = 0; i < 3; i++)
		dst->body.diffuse[i] = mat->diffuse[i];

	/* The 691 colour is the ambient of the shading mode 1 and the
	 * emission of the mode 2, 255 for 1.0. The port saturates it there
	 * (825B4DE8), the hardware does not: PHARRIER sends 600 in mode 2
	 * for the syringe of its nurse and 736 in mode 1 for the selection
	 * frame of its shop, and the cabinet shows both that much brighter. */
	for (i = 0; i < 3; i++)
		dst->body.ambient[i] = mat->unknown[i] / 255.0f;

	/* The 491 colour is in 1/64: PHARRIER sets 0x40 and SGNASCAR goes up
	 * to 0x7F. The port multiplies it by 4 and wraps to 8 bits (825B4DE8),
	 * the same thing below 0x40; the shader scales it. */
	for (i = 0; i < 4; i++)
		dst->body.specular[i] = mat->specular[i];

	for (i = 0; i < 3; i++)
		dst->body.color1[i] = mat->ambient[i];

	/* Vertex alpha times polygon alpha for every type (X360 8252D858);
	 * the render pass decides what it means. */
	alpha = clampf (POLY.alpha * (src->info.alpha * INV255), 0.0f, 1.0f);
	dst->body.alpha = (uint8_t) (alpha * 255.0f);
}

static void
copy_texcoords (hikaru_renderer_t *hr,
                hikaru_vertex_t *dst, hikaru_vertex_t *src)
{
	hikaru_gpu_t *gpu = hr->gpu;
	hikaru_texhead_t *th = &TEX.scratch;
	float w = 16 << th->logw;
	float h = 16 << th->logh;
	/* Vertex UVs are 12.4 unless the texhead says whole texels; the 0D1
	 * offset slot picked by the vertex is added in 1/16 texel (X360
	 * 8252D710, 82538E70). */
	float k = th->uvtexels ? 16.0f : 1.0f;
	float u = src->body.texcoords[0] * k;
	float v = src->body.texcoords[1] * k;
	unsigned sel = src->info.uv_offset;

	if (th->format == HIKARU_FORMAT_I4) {
		w *= 2;
		h *= 2;
	}

	if (sel) {
		u += gpu->texoffset[sel][0] / 16.0f;
		v += gpu->texoffset[sel][1] / 16.0f;
	}

	dst->body.texcoords[0] = u / w;
	dst->body.texcoords[1] = v / h;
}

static void
add_triangle (hikaru_renderer_t *hr)
{
	if (hr->push.num_verts >= 3) {
		uint32_t index = hr->push.num_tris * 3;
		hikaru_vertex_body_t *dst = &hr->push.all[index];
		hikaru_vertex_body_t *a = &hr->push.tmp[0].body;
		hikaru_vertex_body_t *b = &hr->push.tmp[1].body;
		hikaru_vertex_body_t *c = &hr->push.tmp[2].body;
		float e1[3], e2[3], n[3];
		unsigned i;

		VK_ASSERT ((index + 2) < MAX_VERTICES_PER_MESH);

		/* Bits 19:18 select a UV offset, not two-sided lighting (X360
		 * port, 8252D590); the two-sided variant is never used now.
		if (hr->push.tmp[2].info.twosided &&
		    !hr->push.tmp[2].info.nocull)
			VK_ERROR ("got a vertex with culling and two-sided lighting!");

		if (hr->push.tmp[2].info.twosided)
			hr->meshes.current->has_two_sided_lighting = 1;
		*/

		/* Degenerate triangles are dropped, as in the X360 port
		 * (825B6100); not when the vertices follow different
		 * modelviews, the matrices give them their shape then (the
		 * AIRTRIX trails). */
		for (i = 0; i < 3; i++) {
			e1[i] = b->position[i] - a->position[i];
			e2[i] = c->position[i] - a->position[i];
		}
		n[0] = e1[1] * e2[2] - e1[2] * e2[1];
		n[1] = e1[2] * e2[0] - e1[0] * e2[2];
		n[2] = e1[0] * e2[1] - e1[1] * e2[0];
		if (a->second_mv == b->second_mv && b->second_mv == c->second_mv &&
		    (n[0] * n[0] + n[1] * n[1] + n[2] * n[2]) <= 0.0f)
			return;

		/* The winding is the vertex order, reversed by the winding bit
		 * of the closing vertex. The port winds each triangle after its
		 * vertex normals instead (825B6218), but the SGNASCAR cabinet
		 * shows quads whose normals point away from the viewer (the
		 * dark panels of its ENTRY DRIVERS screen). */
		if (hr->push.tmp[2].info.winding) {
			hikaru_vertex_body_t *t = b;
			b = c;
			c = t;
		}

		dst[0] = *a;
		dst[1] = *b;
		dst[2] = *c;
		hr->push.num_tris += 1;

		/* Both faces (bit 23): the triangle is emitted twice, the
		 * second time for its back, lit with the reversed normals. */
		if (hr->push.tmp[2].info.nocull) {
			unsigned j;

			dst[3] = *a;
			dst[4] = *c;
			dst[5] = *b;
			for (i = 3; i < 6; i++)
				for (j = 0; j < 3; j++)
					dst[i].normal[j] = -dst[i].normal[j];
			hr->push.num_tris += 1;
		}
	}
}

void
hikaru_renderer_push_vertices (vk_renderer_t *rend,
                               hikaru_vertex_t *v,
                               uint32_t flags,
                               unsigned num)
{
	hikaru_renderer_t *hr = (hikaru_renderer_t *) rend;
	hikaru_gpu_t *gpu = hr->gpu;
	unsigned i, vp_index = VP0.depth.func;

	VK_ASSERT (hr);
	VK_ASSERT (v);
	VK_ASSERT (num == 1 || num == 3);
	VK_ASSERT (v->info.tricap == 0 || v->info.tricap == 7);

	if (hr->debug.flags[HR_DEBUG_NO_3D])
		return;

	if (hr->debug.flags[HR_DEBUG_SELECT_VIEWPORT] >= 0 &&
	    hr->debug.flags[HR_DEBUG_SELECT_VIEWPORT] != vp_index)
		return;

	switch (num) {

	case 1:
		/* Note that VTX(2) always points to the last pushed vertex,
		 * which for instructions 12x, 1Ax and 1Bx means the vertex
		 * pushed by the instruction itself, and for instructions 1Ex
		 * and 15x the vertex pushed by the previous "push"
		 * instruction.
		 */

		/* If the incoming vertex includes the position, push it
		 * in the temporary buffer, updating it according to the
		 * p(osition)pivot bit. */
		if (flags & HR_PUSH_POS) {

			/* Do not change the pivot if it is not required */
			if (!v->info.ppivot)
				hr->push.tmp[0] = hr->push.tmp[1];
			hr->push.tmp[1] = hr->push.tmp[2];
			memset ((void *) &hr->push.tmp[2], 0, sizeof (hikaru_vertex_t));

			/* Set the position, colors and alpha. */
			VK_COPY_VEC3F (hr->push.tmp[2].body.position, v->body.position);
			copy_colors (hr, &hr->push.tmp[2], v);

			/* Account for the added vertex. */
			hr->push.num_verts += 1;
			VK_ASSERT (hr->push.num_verts < MAX_VERTICES_PER_MESH);
		}

		/* Set the normal. */
		if (flags & HR_PUSH_NRM)
			VK_COPY_VEC3F (hr->push.tmp[2].body.normal, v->body.normal);

		/* Bit 12 of the header: the vertex follows the second
		 * modelview (161 with bit 18). Bits 13 and 14: it takes the
		 * 3x3 and the last row of the modelview. The vertex shader
		 * picks the matrix. */
		if (flags & HR_PUSH_POS)
			hr->push.tmp[2].body.second_mv = v->info.second_mv ? 255 :
				(v->info.rotate ? 0 : 64) + (v->info.translate ? 0 : 128);

		/* Set the texcoords. */
		if (flags & HR_PUSH_TXC)
			copy_texcoords (hr, &hr->push.tmp[2], v);
		break;

	case 3:
		VK_ASSERT (flags == HR_PUSH_TXC);

		if (hr->push.num_verts < 3)
			return;

		for (i = 0; i < 3; i++)
			copy_texcoords (hr, &hr->push.tmp[2 - i], &v[i]);
		break;

	default:
		VK_ASSERT (!"num is neither 1 nor 3");
		break;
	}

	/* Finish the previous triangle. */
	if (v[0].info.tricap == 7) {
		hr->push.tmp[2].info.full = v[0].info.full;
		add_triangle (hr);
	}
}

static void
print_rendstate (hikaru_renderer_t *hr, hikaru_mesh_t *mesh, char *prefix)
{
	LOG ("RENDSTATE %s @%p #instances = %u uvoff=[%d %d | %d %d | %d %d | %d %d]", prefix, mesh, mesh->num_instances,
	     hr->gpu->texoffset[0][0], hr->gpu->texoffset[0][1], hr->gpu->texoffset[1][0], hr->gpu->texoffset[1][1],
	     hr->gpu->texoffset[2][0], hr->gpu->texoffset[2][1], hr->gpu->texoffset[3][0], hr->gpu->texoffset[3][1]);
	if (mesh->vp_index < MAX_VIEWPORTS)
		LOG ("RENDSTATE %s %u vp:  %s", prefix, mesh->num,
		     get_viewport_str (&hr->list->vp_list[mesh->vp_index]));
	if (mesh->mv_index < MAX_MODELVIEWS)
		LOG ("RENDSTATE %s %u mv:  %s", prefix, mesh->num,
		     get_modelview_str (&hr->list->mv_list[mesh->mv_index]));
	if (mesh->mat_index < MAX_MATERIALS)
		LOG ("RENDSTATE %s %u mat: %s", prefix, mesh->num,
		     get_material_str (&hr->list->mat_list[mesh->mat_index]));
	if (mesh->tex_index < MAX_TEXHEADS)
		LOG ("RENDSTATE %s %u tex: %s", prefix, mesh->num,
		     get_texhead_str (&hr->list->tex_list[mesh->tex_index]));
	if (mesh->ls_index < MAX_LIGHTSETS)
		LOG ("RENDSTATE %s %u ls:  %s", prefix, mesh->num,
		     get_lightset_str (&hr->list->ls_list[mesh->ls_index]));
}

/* TODO check boundary conditions when nothing is uploaded in a frame. */
/* TODO alpha threshold */
static void
update_and_set_rendstate (hikaru_renderer_t *hr, hikaru_mesh_t *mesh)
{
	hikaru_gpu_t *gpu = hr->gpu;
	unsigned i;

	memset ((void *) mesh, 0, sizeof (hikaru_mesh_t));

	LOG ("RENDSTATE updating vp %u/%u", hr->list->num_vps, MAX_VIEWPORTS);
	hr->list->vp_list[hr->list->num_vps++] = VP0;
	VK_ASSERT (hr->list->num_vps < MAX_VIEWPORTS);

	LOG ("RENDSTATE updating mat %u/%u", hr->list->num_mats, MAX_MATERIALS);
	hr->list->mat_list[hr->list->num_mats++] = MAT0;
	VK_ASSERT (hr->list->num_mats < MAX_MATERIALS);

	LOG ("RENDSTATE updating tex %u/%u", hr->list->num_texs, MAX_TEXHEADS);
	hr->list->tex_list[hr->list->num_texs++] = TEX0;
	VK_ASSERT (hr->list->num_texs < MAX_TEXHEADS);

	LOG ("RENDSTATE updating ls %u/%u", hr->list->num_lss, MAX_LIGHTSETS);
	hr->list->ls_list[hr->list->num_lss++] = LS0;
	VK_ASSERT (hr->list->num_lss < MAX_LIGHTSETS);

	/* Copy the per-instance modelviews from last to first. */
	/* TODO optimize by setting MV.total to 0 (and fix the fallback). */
	if (MV.total == ~0) {
		LOG ("RENDSTATE adding no mvs %u/%u [#instances=%u]",
		     hr->list->num_mvs, MAX_MODELVIEWS,
		     hr->list->num_instances);

		mesh->mv_index = hr->list->num_mvs - 1;
		mesh->num_instances = hr->list->num_instances;
	} else {
		MV.total = 1;

		mesh->mv_index = hr->list->num_mvs;
		mesh->num_instances = MV.total;
		hr->list->num_instances = MV.total;

		for (i = 0; i < MV.total; i++, hr->list->num_mvs++) {
			LOG ("RENDSTATE adding mv %u/%u [#instances=%u]",
			     hr->list->num_mvs, MAX_MODELVIEWS, MV.total);
	
			VK_ASSERT (hr->list->num_mvs < MAX_MODELVIEWS);
			hr->list->mv_list[hr->list->num_mvs] = MV.table[i];
		}

		MV.total = 0;
		MV.depth = 0;
	}

	/* The second modelview (161 with bit 18) is listed when it changes. */
	if (hr->list->mv2_index >= MAX_MODELVIEWS ||
	    memcmp (&hr->list->mv_list[hr->list->mv2_index], &MV.second,
	            sizeof (hikaru_modelview_t)) != 0) {
		VK_ASSERT (hr->list->num_mvs < MAX_MODELVIEWS);
		hr->list->mv2_index = hr->list->num_mvs;
		hr->list->mv_list[hr->list->num_mvs++] = MV.second;
	}
	mesh->mv2_index = hr->list->mv2_index;

	mesh->depth_bias = gpu->depth_bias;

	mesh->vp_index = hr->list->num_vps - 1;
	mesh->mat_index = hr->list->num_mats - 1;
	mesh->tex_index = hr->list->num_texs - 1;
	mesh->ls_index = hr->list->num_lss - 1;

	mesh->num = hr->list->total_meshes++;
	print_rendstate (hr, mesh, "U");
}

#define OFFSET(member_) \
	((const GLvoid *) offsetof (hikaru_vertex_body_t, member_))

#define VAP(loc_, num_, type_, member_, normalize_) \
	if (loc_ != (GLuint) -1) { \
		glVertexAttribPointer (loc_, num_, type_, normalize_, \
		                       sizeof (hikaru_vertex_body_t), \
		                       OFFSET (member_)); \
		VK_ASSERT_NO_GL_ERROR (); \
		\
		glEnableVertexAttribArray (loc_); \
		VK_ASSERT_NO_GL_ERROR (); \
	}

static void
draw_instance (hikaru_renderer_t *hr, hikaru_mesh_t *mesh, unsigned i)
{
	upload_modelview (hr, mesh, i);
	if (hr->meshes.collect) {
		glUniform1ui (hr->meshes.locs.u_order, hr->meshes.order);
		hr->meshes.order += mesh->num_tris;
	}
	glDrawArrays (GL_TRIANGLES, mesh->first, mesh->num_tris * 3);
}

static void
draw_mesh (hikaru_renderer_t *hr, hikaru_mesh_t *mesh)
{
	hikaru_mesh_held_t *held;
	float depth_scale;
	unsigned i;

	VK_ASSERT (mesh);

	LOG ("variant=%016llx", (unsigned long long) hr->meshes.variant.full);
	LOG ("==== DRAWING MESH @%p (#vertices=%u #instances=%u 103alpha=%.3f addr=%08X) ====",
	     mesh, mesh->num_tris * 3, mesh->num_instances, mesh->poly_alpha,
	     mesh->addr[0]);

	print_rendstate (hr, mesh, "D");

	glBindVertexArray (hr->meshes.vao);
	VK_ASSERT_NO_GL_ERROR ();

	upload_glsl_program (hr, mesh);
	held = hr->meshes.held;
	if (held->frame != hr->meshes.frame) {
		/* Nothing of this frame yet. */
		held->frame = hr->meshes.frame;
		held->vp_index = held->ls_index = held->ls_vp_index = ~0;
		held->mv_index = held->mv2_index = ~0;
		held->alpha_ref = held->depth_scale = NAN;
		held->weigh_by_alpha = -1;
	}
	if (held->alpha_ref != hr->meshes.alpha_ref) {
		held->alpha_ref = hr->meshes.alpha_ref;
		glUniform1f (hr->meshes.locs.u_alpha_ref, hr->meshes.alpha_ref);
	}
	if (held->weigh_by_alpha != (int) hr->meshes.weigh_by_alpha) {
		held->weigh_by_alpha = hr->meshes.weigh_by_alpha;
		glUniform1f (hr->meshes.locs.u_weigh_by_alpha,
		             hr->meshes.weigh_by_alpha ? 1.0f : 0.0f);
	}
	if (hr->meshes.collect)
		glUniform1ui (hr->meshes.locs.u_blend_mode, hr->meshes.blend_mode);
	VK_ASSERT_NO_GL_ERROR ();

	upload_viewport (hr, mesh);
	upload_material_texhead (hr, mesh);
	upload_lightset (hr, mesh);
	VK_ASSERT_NO_GL_ERROR ();

	/* The 101 bias moves the mesh by a fraction of its own depth, nearer
	 * when positive (HEURISTIC, 1/65536 per unit: four times as much and
	 * a light shaft of AIRTRIX covers the skater). It is signed and holds
	 * for the opaque types too: the lettering and the painted lines of
	 * the SGNASCAR track lie on layers of -1024 to -256, the shadow of the
	 * AIRTRIX skater is opaque. */
	depth_scale = hr->meshes.depth_bias ?
	              1.0f - mesh->depth_bias * (1.0f / 65536.0f) : 1.0f;
	if (held->depth_scale != depth_scale) {
		held->depth_scale = depth_scale;
		glUniform1f (hr->meshes.locs.u_depth_scale, depth_scale);
	}

	if (hr->debug.flags[HR_DEBUG_NO_INSTANCING]) {
		unsigned i = MIN2 (hr->debug.flags[HR_DEBUG_SELECT_INSTANCE],
		                   mesh->num_instances - 1);
		draw_instance (hr, mesh, i);
	} else {
		for (i = 0; i < mesh->num_instances; i++)
			draw_instance (hr, mesh, i);
	}

	glBindVertexArray (0);
}

/* Uploads the vertices of the frame, once, before its meshes are drawn. */
static void
upload_vertex_data (hikaru_renderer_t *hr)
{
	if (!hr->meshes.vao) {
		glGenVertexArrays (1, &hr->meshes.vao);
		glGenBuffers (1, &hr->meshes.vbo);
		VK_ASSERT_NO_GL_ERROR ();
	}

	glBindVertexArray (hr->meshes.vao);
	glBindBuffer (GL_ARRAY_BUFFER, hr->meshes.vbo);
	glBufferData (GL_ARRAY_BUFFER,
	              sizeof (hikaru_vertex_body_t) * hr->list->verts.num,
	              (const GLvoid *) hr->list->verts.all, GL_DYNAMIC_DRAW);
	VK_ASSERT_NO_GL_ERROR ();

	VAP (0, 3, GL_FLOAT,          position,  GL_FALSE);
	VAP (1, 3, GL_FLOAT,          normal,    GL_FALSE);
	VAP (2, 3, GL_UNSIGNED_BYTE,  diffuse,   GL_TRUE);
	VAP (3, 3, GL_FLOAT,          ambient,   GL_FALSE);
	VAP (4, 4, GL_UNSIGNED_BYTE,  specular,  GL_TRUE);
	VAP (5, 3, GL_UNSIGNED_BYTE,  color1,    GL_TRUE);
	VAP (6, 2, GL_FLOAT,          texcoords, GL_FALSE);
	VAP (7, 1, GL_UNSIGNED_BYTE,  alpha,     GL_TRUE);
	VAP (8, 1, GL_UNSIGNED_BYTE,  second_mv, GL_TRUE);

	glBindVertexArray (0);
}

#undef OFFSET

/* Appends the pushed vertices of a mesh to the buffer of the frame. */
static void
store_vertex_data (hikaru_renderer_t *hr, hikaru_mesh_t *mesh)
{
	hikaru_list_t *list = hr->list;
	uint32_t num;

	VK_ASSERT (mesh);

	mesh->num_tris = hr->push.num_tris;

	num = mesh->num_tris * 3;
	if (list->verts.num + num > list->verts.max) {
		list->verts.max = MAX2 (list->verts.max * 2, list->verts.num + num);
		list->verts.all = (hikaru_vertex_body_t *)
			realloc (list->verts.all,
			         list->verts.max * sizeof (hikaru_vertex_body_t));
		VK_ASSERT (list->verts.all);
	}
	if (num)
		memcpy (&list->verts.all[list->verts.num], hr->push.all,
		        num * sizeof (hikaru_vertex_body_t));
	mesh->first = list->verts.num;
	list->verts.num += num;
}

void
hikaru_renderer_launch (vk_renderer_t *rend)
{
	hikaru_renderer_t *hr = (hikaru_renderer_t *) rend;

	hr->framebuffer.launched = 1;
}

void
hikaru_renderer_begin_mesh (vk_renderer_t *rend, uint32_t addr,
                            bool is_static)
{
	hikaru_renderer_t *hr = (hikaru_renderer_t *) rend;
	hikaru_gpu_t *gpu = hr->gpu;
	unsigned vp_index = VP0.depth.func;
	unsigned polytype = POLY.type;
	unsigned mesh_index = hr->list->num_meshes[vp_index][polytype];
	hikaru_mesh_t *mesh;

	VK_ASSERT (hr);
	VK_ASSERT (!hr->meshes.current);

	if (hr->debug.flags[HR_DEBUG_NO_3D])
		return;

	/* Create a new mesh. */
	mesh = &hr->list->mesh_list[vp_index][polytype][mesh_index++];
	VK_ASSERT (mesh_index < MAX_MESHES);

	hr->list->num_meshes[vp_index][polytype] = mesh_index;

	/* Make the mesh current and set the rendering state. */
	hr->meshes.current = mesh;
	update_and_set_rendstate (hr, mesh);
	mesh->poly_alpha = POLY.alpha;
	mesh->polytype = polytype;
	mesh->addr[0] = addr;

	/* Clear the push buffer. */
	hr->push.num_verts = 0;
	hr->push.num_tris = 0;
}

void
hikaru_renderer_end_mesh (vk_renderer_t *rend, uint32_t addr)
{
	hikaru_renderer_t *hr = (hikaru_renderer_t *) rend;

	VK_ASSERT (hr);

	if (hr->debug.flags[HR_DEBUG_NO_3D])
		return;

	VK_ASSERT (hr->meshes.current);

	/* Store the pushed vertex data. */
	store_vertex_data (hr, hr->meshes.current);
	hr->meshes.current->addr[1] = addr;

	/* Make sure there is no current mesh bound. */
	hr->meshes.current = NULL;
}

/* Blend factors of the material (A81 bits 17:16), as the X360 port
 * applies them (825B4290): source alpha over the destination, the product,
 * source alpha added, the source as it is over the destination. Where the
 * source is weighted by its alpha the shader does it, before the clamp.
 * The translucent type is blended by lists_fs_source, which does the same. */
static void
set_material_blend (hikaru_renderer_t *hr, hikaru_mesh_t *mesh)
{
	static const GLenum factors[4][4] = {
		{ GL_ONE,       GL_ONE_MINUS_SRC_ALPHA, GL_ZERO, GL_ONE_MINUS_SRC_ALPHA },
		{ GL_DST_COLOR, GL_ZERO,                GL_ZERO, GL_ZERO },
		{ GL_ONE,       GL_ONE,                 GL_ZERO, GL_ONE_MINUS_SRC_ALPHA },
		{ GL_ONE,       GL_ONE_MINUS_SRC_ALPHA, GL_ZERO, GL_ONE_MINUS_SRC_ALPHA },
	};
	unsigned mode = 0;

	if (mesh->mat_index < MAX_MATERIALS)
		mode = hr->list->mat_list[mesh->mat_index].blending_mode;
	hr->meshes.blend_mode = mode;
	hr->meshes.weigh_by_alpha = (mode == 0 || mode == 2);
	glBlendFuncSeparate (factors[mode][0], factors[mode][1],
	                     factors[mode][2], factors[mode][3]);
}

/* The passes that do not blend replace the pixel and cover what is below the
 * 3D; a mesh faded by the depth queue lets through what it does not hide. */
static void
set_opaque_blend (hikaru_renderer_t *hr, hikaru_mesh_t *mesh)
{
	if (is_faded_by_depth (hr, mesh))
		glBlendFuncSeparate (GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA,
		                     GL_ZERO, GL_ONE_MINUS_SRC_ALPHA);
	else
		glBlendFuncSeparate (GL_ONE, GL_ZERO, GL_ZERO, GL_ZERO);
}

/* Blends the translucent fragments of every pixel over the colour below:
 * the farthest first and, at the same depth, in the order they were sent.
 * The arithmetic is that of an 8-bit colour buffer, each result rounded
 * to the nearest before the next (HEURISTIC: the precision of the blender
 * of the hardware is not known); the colour is held as 0 to 255 so that
 * every host computes the same. KEPT of them are put in order at a time. */
static const char *lists_vs_source =
"#version 430									\n"
"										\n"
"void main (void) {								\n"
"	gl_Position = vec4 (vec2 (gl_VertexID & 1, gl_VertexID >> 1) * 4.0 - 1.0,	\n"
"	                    0.0, 1.0);						\n"
"}										\n";

static const char *lists_fs_source =
"#version 430									\n"
"										\n"
"#define KEPT 32									\n"
"										\n"
LISTS_GLSL
"uniform sampler2D u_below;							\n"
"										\n"
"out vec4 fragColor;								\n"
"										\n"
"bool										\n"
"comes_before (float depth_a, uint order_a, float depth_b, uint order_b)		\n"
"{										\n"
"	return depth_a > depth_b ||						\n"
"	       (depth_a == depth_b && order_a < order_b);			\n"
"}										\n"
"										\n"
"void										\n"
"main (void)									\n"
"{										\n"
"	ivec2 pixel = ivec2 (gl_FragCoord.xy);					\n"
"	uint head = imageLoad (u_heads, pixel).r;				\n"
"	float depth[KEPT], last_depth = 2.0;					\n"
"	uint order[KEPT], index[KEPT], last_order = 0u, i;			\n"
"	precise vec4 color;							\n"
"	precise float through;							\n"
"	bool more;								\n"
"	int num, j;								\n"
"										\n"
"	if (head == 0u)								\n"
"		discard;							\n"
"	color = floor (texelFetch (u_below, pixel, 0) * 255.0 + 0.5);		\n"
"										\n"
"	for (;;) {								\n"
"		num = 0;							\n"
"		more = false;							\n"
"		for (i = head; i != 0u; i = u_nodes[i - 1u].next) {		\n"
"			float d = u_nodes[i - 1u].depth;			\n"
"			uint o = u_nodes[i - 1u].order;				\n"
"										\n"
"			if (!comes_before (last_depth, last_order, d, o))	\n"
"				continue;					\n"
"			if (num < KEPT) {					\n"
"				j = num++;					\n"
"			} else {						\n"
"				more = true;					\n"
"				if (!comes_before (d, o, depth[KEPT - 1], order[KEPT - 1]))	\n"
"					continue;				\n"
"				j = KEPT - 1;					\n"
"			}							\n"
"			for (; j > 0 && comes_before (d, o, depth[j - 1], order[j - 1]); j--) {	\n"
"				depth[j] = depth[j - 1];			\n"
"				order[j] = order[j - 1];			\n"
"				index[j] = index[j - 1];			\n"
"			}							\n"
"			depth[j] = d;						\n"
"			order[j] = o;						\n"
"			index[j] = i - 1u;					\n"
"		}								\n"
"										\n"
"		for (j = 0; j < num; j++) {					\n"
"			vec4 src = u_nodes[index[j]].color;			\n"
"			uint mode = u_nodes[index[j]].mode;			\n"
"										\n"
"			through = color.a * (1.0 - src.a);			\n"
"			if (mode == 1u)						\n"
"				color = vec4 (src.rgb * color.rgb, 0.0);	\n"
"			else if (mode == 2u)					\n"
"				color = vec4 (src.rgb * 255.0 + color.rgb, through);	\n"
"			else							\n"
"				color = vec4 (src.rgb * 255.0 + color.rgb * (1.0 - src.a), through);	\n"
"			color = floor (clamp (color, 0.0, 255.0) + 0.5);	\n"
"		}								\n"
"										\n"
"		if (!more)							\n"
"			break;							\n"
"		last_depth = depth[KEPT - 1];					\n"
"		last_order = order[KEPT - 1];					\n"
"	}									\n"
"										\n"
"	fragColor = color / 255.0;						\n"
"}										\n";

static void
size_lists (hikaru_renderer_t *hr, uint32_t max)
{
	hr->framebuffer.lists.max = max;
	glBindBuffer (GL_SHADER_STORAGE_BUFFER, hr->framebuffer.lists.nodes);
	glBufferData (GL_SHADER_STORAGE_BUFFER, (GLsizeiptr) max * NODE_SIZE,
	              NULL, GL_DYNAMIC_COPY);
	glBindBuffer (GL_SHADER_STORAGE_BUFFER, 0);
	VK_ASSERT_NO_GL_ERROR ();
}

/* The translucent polygons of a viewport are blended far to near at every
 * pixel, whatever the triangle or the mesh they come from. On the PHARRIER
 * cabinet the faces of a SEGA letter, alpha 0.8 and drawn on both sides,
 * show the faces behind them and none over them, from any angle; the glass
 * of an AIRTRIX street lamp glows over its frame; a wall of fire of BRAVEFF
 * keeps its texture. How the hardware orders them is not known (HEURISTIC):
 * whole triangles sorted on their centroid break those letters, and writing
 * the depth drops layers that the cabinet shows. The X360 port reverses its
 * list of objects instead (825B3EB0). Neither writes the depth here.
 *
 * The meshes are drawn once. A fragment that is not behind the other types
 * goes to the list of its pixel with its colour, its depth, the blend mode
 * of its material and its rank in the display list; then every pixel
 * blends its list, see lists_fs_source. */
static void
draw_translucent_meshes (hikaru_renderer_t *hr, hikaru_mesh_t *meshes,
                         unsigned num)
{
	static const GLuint zero[4] = { 0, 0, 0, 0 };
	GLuint used;
	unsigned j;

	/* The depth the other types left. */
	glActiveTexture (GL_TEXTURE0 + 6);
	glBindTexture (GL_TEXTURE_2D, hr->framebuffer.solid_depth);
	glCopyTexSubImage2D (GL_TEXTURE_2D, 0, 0, 0, 0, 0, 640, 480);
	glActiveTexture (GL_TEXTURE0);

	glBindImageTexture (0, hr->framebuffer.lists.heads, 0, GL_FALSE, 0,
	                    GL_READ_WRITE, GL_R32UI);
	glDisable (GL_DEPTH_TEST);
	VK_ASSERT_NO_GL_ERROR ();

	/* Collect the fragments, once more if their buffer was too small.
	 * The Intel driver 21.20.16.5174 wants a buffer that has grown bound
	 * again, and does not always reset an atomic counter buffer: the
	 * count is in a storage buffer. */
	hr->meshes.collect = true;
	for (;;) {
		glBindBufferBase (GL_SHADER_STORAGE_BUFFER, 0, hr->framebuffer.lists.nodes);
		glBindBufferBase (GL_SHADER_STORAGE_BUFFER, 1, hr->framebuffer.lists.count);
		glBindFramebuffer (GL_FRAMEBUFFER, hr->framebuffer.lists.heads_fb);
		glDisable (GL_SCISSOR_TEST);
		glClearBufferuiv (GL_COLOR, 0, zero);
		glBufferSubData (GL_SHADER_STORAGE_BUFFER, 0, sizeof (GLuint), zero);
		glBindFramebuffer (GL_FRAMEBUFFER,
		                   hr->framebuffer.fb[hr->framebuffer.scene]);
		glEnable (GL_SCISSOR_TEST);
		glColorMask (GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);

		hr->meshes.order = 0;
		for (j = 0; j < num; j++) {
			set_material_blend (hr, &meshes[j]);
			draw_mesh (hr, &meshes[j]);
		}

		glColorMask (GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
		glMemoryBarrier (GL_ALL_BARRIER_BITS);
		glGetBufferSubData (GL_SHADER_STORAGE_BUFFER, 0, sizeof (GLuint), &used);
		VK_ASSERT_NO_GL_ERROR ();
		if (used <= hr->framebuffer.lists.max)
			break;
		VK_PRINT ("renderer: more room for %u translucent fragments", used);
		size_lists (hr, MAX2 (used, hr->framebuffer.lists.max * 2));
	}
	hr->meshes.collect = false;

	/* Blend them over the colour below. */
	if (used) {
		glActiveTexture (GL_TEXTURE0 + 5);
		glBindTexture (GL_TEXTURE_2D, hr->framebuffer.lists.below);
		glCopyTexSubImage2D (GL_TEXTURE_2D, 0, 0, 0, 0, 0, 640, 480);

		glUseProgram (hr->framebuffer.lists.program);
		hr->meshes.variant.full = ~0;
		glDisable (GL_BLEND);
		glDisable (GL_SCISSOR_TEST);
		glBindVertexArray (hr->framebuffer.vao);
		glDrawArrays (GL_TRIANGLES, 0, 3);
		glBindVertexArray (0);
		glEnable (GL_SCISSOR_TEST);
		glEnable (GL_BLEND);

		glBindTexture (GL_TEXTURE_2D, 0);
		glActiveTexture (GL_TEXTURE0);
	}

	glActiveTexture (GL_TEXTURE0 + 6);
	glBindTexture (GL_TEXTURE_2D, 0);
	glActiveTexture (GL_TEXTURE0);
	glEnable (GL_DEPTH_TEST);
	VK_ASSERT_NO_GL_ERROR ();
}

static void
draw_meshes_for_polytype (hikaru_renderer_t *hr, unsigned vpi, int polytype)
{
	hikaru_mesh_t *meshes = hr->list->mesh_list[vpi][polytype];
	unsigned num = hr->list->num_meshes[vpi][polytype];
	int j;

	if (num == 0)
		return;

	LOG ("POLYTYPE %d: %u meshes", polytype, num);

	if (hr->debug.flags[HR_DEBUG_SELECT_POLYTYPE] >= 0 &&
	    hr->debug.flags[HR_DEBUG_SELECT_POLYTYPE] != polytype)
		return;

	LOG (" ==== DRAWING VP %u, POLYTYPE %d ====", vpi, polytype);

	/* Render passes of the X360 port (825B2408, 825B3EB0): opaque and
	 * shadow types write the depth without blending nor alpha test; the
	 * transparent type is punch-through (alpha > 0.5, depth written, no
	 * blending); background and translucent blend with the material mode,
	 * keep the depth and drop alpha <= 1/255.
	 *
	 * The alpha of the color buffer holds what the 3D lets through: the
	 * passes that do not blend write 0 there. */
	glEnable (GL_BLEND);
	glBlendFuncSeparate (GL_ONE, GL_ZERO, GL_ZERO, GL_ZERO);
	hr->meshes.weigh_by_alpha = false;

	switch (polytype) {
	case HIKARU_POLYTYPE_TRANSPARENT:
		glDepthMask (GL_TRUE);
		hr->meshes.depth_bias = true;
		hr->meshes.alpha_ref = 0.5f;
		break;
	case HIKARU_POLYTYPE_TRANSLUCENT:
		glDepthMask (GL_FALSE);
		hr->meshes.depth_bias = true;
		hr->meshes.alpha_ref = 1.0f / 255.0f;
		break;
	case HIKARU_POLYTYPE_BACKGROUND:
		glDepthMask (GL_FALSE);
		hr->meshes.depth_bias = false;
		hr->meshes.alpha_ref = 1.0f / 255.0f;
		break;
	default:
		glDepthMask (GL_TRUE);
		hr->meshes.depth_bias = true;
		hr->meshes.alpha_ref = -1.0f;
		break;
	}

	if (polytype == HIKARU_POLYTYPE_TRANSLUCENT) {
		draw_translucent_meshes (hr, meshes, num);
		return;
	}

	for (j = 0; j < num; j++) {
		hikaru_mesh_t *mesh = &meshes[j];
		if (polytype == HIKARU_POLYTYPE_BACKGROUND)
			set_material_blend (hr, mesh);
		else
			set_opaque_blend (hr, mesh);
		draw_mesh (hr, mesh);
	}
}

static void
draw_scene (hikaru_renderer_t *hr)
{
	/* The background first, for the depth queue to fade the polygons
	 * into it, then the pass order of the X360 port (825B3EB0), which
	 * draws it after the opaque types: punch-through, opaque, then the
	 * translucent type. */
	static const int sorted_polytypes[] = {
		HIKARU_POLYTYPE_BACKGROUND,
		HIKARU_POLYTYPE_TRANSPARENT,
		HIKARU_POLYTYPE_OPAQUE,
		HIKARU_POLYTYPE_SHADOW_A,
		HIKARU_POLYTYPE_SHADOW_B,
		HIKARU_POLYTYPE_TRANSLUCENT,
	};
	unsigned vpi, i;

	if (hr->debug.flags[HR_DEBUG_NO_3D])
		return;

	upload_vertex_data (hr);

	glViewport (0, 0, 640, 480);

	/* Note that "the pixel ownership test, the scissor test, dithering,
	 * and the buffer writemasks affect the operation of glClear". */
	glDepthMask (GL_TRUE);
	glDisable (GL_SCISSOR_TEST);
	/* An empty 3D buffer lets everything through: the clear color comes
	 * in on the screen, see draw (). */
	glClearColor (0.0f, 0.0f, 0.0f, 1.0f);
	glClear (GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

	glEnable (GL_SCISSOR_TEST);

	glEnable (GL_DEPTH_TEST);
	glDepthFunc (GL_LEQUAL);

	glEnable (GL_CULL_FACE);
	glCullFace (GL_BACK);

	for (vpi = 0; vpi < 8; vpi++) {
		glDepthMask (GL_TRUE);
		glDisable (GL_SCISSOR_TEST);
		glClear (GL_DEPTH_BUFFER_BIT);
		glEnable (GL_SCISSOR_TEST);
		for (i = 0; i < NUMELEM (sorted_polytypes); i++)
			draw_meshes_for_polytype (hr, vpi, sorted_polytypes[i]);
	}

	glDepthMask (GL_TRUE);

	glDisable (GL_SCISSOR_TEST);
	glViewport (0, 0, 640, 480);
}

static void
destroy_list (hikaru_list_t *list)
{
	unsigned vpi, i;

	if (!list)
		return;

	free (list->vp_list);
	free (list->mv_list);
	free (list->mat_list);
	free (list->tex_list);
	free (list->ls_list);

	for (vpi = 0; vpi < 8; vpi++)
		for (i = 0; i < 8; i++)
			free (list->mesh_list[vpi][i]);

	free (list->verts.all);
	free (list);
}

static hikaru_list_t *
new_list (void)
{
	hikaru_list_t *list = ALLOC (hikaru_list_t);
	unsigned vpi, i;

	if (!list)
		return NULL;

	list->vp_list = (hikaru_viewport_t *)
			malloc (sizeof (hikaru_viewport_t) * MAX_VIEWPORTS);
	list->mv_list = (hikaru_modelview_t *)
			malloc (sizeof (hikaru_modelview_t) * MAX_MODELVIEWS);
	list->mat_list = (hikaru_material_t *)
			malloc (sizeof (hikaru_material_t) * MAX_MATERIALS);
	list->tex_list = (hikaru_texhead_t *)
			malloc (sizeof (hikaru_texhead_t) * MAX_TEXHEADS);
	list->ls_list = (hikaru_lightset_t *)
			malloc (sizeof (hikaru_lightset_t) * MAX_LIGHTSETS);

	if (!list->vp_list || !list->mv_list || !list->mat_list ||
	    !list->tex_list || !list->ls_list)
		goto fail;

	for (vpi = 0; vpi < 8; vpi++) {
		for (i = 0; i < 8; i++) {
			list->mesh_list[vpi][i] =
				(hikaru_mesh_t *) malloc (sizeof (hikaru_mesh_t) * MAX_MESHES);
			if (!list->mesh_list[vpi][i])
				goto fail;
		}
	}

	return list;

fail:
	destroy_list (list);
	return NULL;
}

static void
destroy_3d_state (hikaru_renderer_t *hr)
{
	destroy_list (hr->list);

	vk_renderer_destroy_program (hr->meshes.program);
	VK_ASSERT_NO_GL_ERROR ();

	if (hr->meshes.vao) {
		glBindVertexArray (0);
		glDeleteVertexArrays (1, &hr->meshes.vao);
		glDeleteBuffers (1, &hr->meshes.vbo);
	}
}

static int
build_3d_state (hikaru_renderer_t *hr)
{
	hr->list = new_list ();

	return hr->list ? 0 : -1;
}

/****************************************************************************
 Framebuffers
****************************************************************************/

static const struct {
       vec3f_t position;
       vec2f_t texcoords;
} fb_vbo_data[] = {
       { { 1.0f, 0.0f, 0.0f }, { 1.0f, 1.0f } },
       { { 0.0f, 0.0f, 0.0f }, { 0.0f, 1.0f } },
       { { 1.0f, 1.0f, 0.0f }, { 1.0f, 0.0f } },
       { { 0.0f, 1.0f, 0.0f }, { 0.0f, 0.0f } },
};

static const char *fb_vs_source =
"#version 140									\n"
"										\n"
"#extension GL_ARB_explicit_attrib_location : require				\n"
"										\n"
"uniform mat4 u_projection =							\n"
"	mat4 (vec4 ( 2.0,  0.0,  0.0,  0.0),					\n"
"	      vec4 ( 0.0, -2.0,  0.0,  0.0),					\n"
"	      vec4 ( 0.0,  0.0, -1.0,  0.0),					\n"
"	      vec4 (-1.0,  1.0,  0.0,  1.0));					\n"
"										\n"
"layout(location = 0) in vec3 i_position;					\n"
"layout(location = 1) in vec2 i_texcoords;					\n"
"										\n"
"out vec2 p_texcoords;								\n"
"										\n"
"void main (void) {								\n"
"	gl_Position = u_projection * vec4 (i_position, 1.0);			\n"
"	p_texcoords = i_texcoords;						\n"
"}										\n";

static const char *fb_fs_source =
"#version 140									\n"
"										\n"
"out vec4 fragColor;								\n"
"uniform sampler2D u_front;							\n"
"uniform sampler2D u_back;							\n"
"uniform sampler2D u_layer1;							\n"
"uniform sampler2D u_layer2;							\n"
"uniform float u_factor = 0.0;							\n"
"uniform float u_mult_f = 1.0;							\n"
"uniform float u_mult_b = 1.0;							\n"
"uniform float u_mult_1 = 1.0;							\n"
"uniform float u_mult_2 = 1.0;							\n"
"uniform float u_keyed_1 = 1.0;							\n"
"uniform float u_keyed_2 = 1.0;							\n"
"uniform float u_keyed_3d = 0.0;						\n"
"uniform float u_above_1 = 1.0;							\n"
"uniform float u_above_2 = 1.0;							\n"
"uniform int u_invert_f = 0;							\n"
"uniform int u_invert_b = 0;							\n"
"uniform vec4 u_clear = vec4 (0.0);						\n"
"										\n"
"in vec2 p_texcoords;								\n"
"										\n"
"void main (void) {								\n"
"	vec2 p_texcoords_inv = vec2 (p_texcoords.s, 1 - p_texcoords.t);		\n"
"	vec4 texel_f, texel_b;							\n"
"										\n"
"	if (u_invert_f == 0)							\n"
"		texel_f = texture (u_front, p_texcoords);			\n"
"	else									\n"
"		texel_f = texture (u_front, p_texcoords_inv);			\n"
"	texel_f = texel_f * u_mult_f;						\n"
"										\n"
"	if (u_invert_b == 0)							\n"
"		texel_b = texture (u_back,  p_texcoords);			\n"
"	else									\n"
"		texel_b = texture (u_back,  p_texcoords_inv);			\n"
"	texel_b = texel_b * u_mult_b;						\n"
"										\n"
"	vec4 texel_1 = texture (u_layer1, p_texcoords_inv);			\n"
"	vec4 texel_2 = texture (u_layer2, p_texcoords_inv);			\n"
"	float on_1 = (u_mult_1 > 0.0) ? 1.0 : 0.0;				\n"
"	float on_2 = (u_mult_2 > 0.0) ? 1.0 : 0.0;				\n"
"	/* The key is the alpha bit: any non-zero alpha covers (the 2-bit	\n"
"	 * alpha of the A2BGR10 layers of PHARRIER holds 1).			\n"
"	 */									\n"
"	float cover_1 = (u_keyed_1 > 0.0) ? step (0.1, texel_1.a) : 1.0;	\n"
"	float cover_2 = (u_keyed_2 > 0.0) ? step (0.1, texel_2.a) : 1.0;	\n"
"	/* The alpha of a 3D buffer is what it lets through; a layer taken	\n"
"	 * as a blender input lets nothing through.				\n"
"	 */									\n"
"	if (u_invert_f != 0)							\n"
"		texel_f.a = 0.0;						\n"
"	if (u_invert_b != 0)							\n"
"		texel_b.a = 0.0;						\n"
"	vec4 color = mix (texel_f, texel_b, u_factor);				\n"
"	/* The clear color shows where the 3D holds nothing, and covers	\n"
"	 * what is below it when its alpha bit is set.			\n"
"	 */									\n"
"	color.rgb += color.a * u_clear.rgb;					\n"
"	color.a *= 1.0 - u_clear.a;						\n"
"	float cover_3d = (u_keyed_3d > 0.0) ? 1.0 - step (0.999, color.a) : 1.0;	\n"
"										\n"
"	/* A layer pixel with its alpha bit covers the 3D, black included	\n"
"	 * (the black bands of the Racer attract); the others are		\n"
"	 * transparent whatever their color (its green 0x03E0 boxes).	\n"
"	 * Layer 1 sits above layer 2 (its HUD text over the band).	\n"
"	 * Cabinet footage, GPU-RE/16 § 10.					\n"
"	 *									\n"
"	 * A layer can sit below the 3D, which then shows it where nothing	\n"
"	 * was drawn (the AIRTRIX logo screens).				\n"
"	 */									\n"
"	vec3 below = color.rgb;							\n"
"	below = mix (below, texel_2.rgb * u_mult_2, cover_2 * on_2 * (1.0 - u_above_2));	\n"
"	below = mix (below, texel_1.rgb * u_mult_1, cover_1 * on_1 * (1.0 - u_above_1));	\n"
"	color.rgb = mix (below, color.rgb, cover_3d);				\n"
"	color.rgb = mix (color.rgb, texel_2.rgb * u_mult_2, cover_2 * on_2 * u_above_2);	\n"
"	color.rgb = mix (color.rgb, texel_1.rgb * u_mult_1, cover_1 * on_1 * u_above_1);	\n"
"	fragColor = color;							\n"
"}\n";

#define NUM_FRAMEBUFFERS 2

#define OFFSET(member_) \
	((const GLvoid *) offsetof (typeof (fb_vbo_data[0]), member_))

static int
build_fb_state (hikaru_renderer_t *hr)
{
	static const GLenum targets[1] = { GL_COLOR_ATTACHMENT0 };
	unsigned i;

	/* Compile the framebuffer GLSL program and retrieve the uniforms. */
	hr->framebuffer.program =
		vk_renderer_compile_program (fb_vs_source, fb_fs_source);
	VK_ASSERT_NO_GL_ERROR ();

	hr->framebuffer.locs.u_front =
		get_uniform_loc (hr->framebuffer.program, "u_front");
	hr->framebuffer.locs.u_back =
		get_uniform_loc (hr->framebuffer.program, "u_back");
	hr->framebuffer.locs.u_layer1 =
		get_uniform_loc (hr->framebuffer.program, "u_layer1");
	hr->framebuffer.locs.u_layer2 =
		get_uniform_loc (hr->framebuffer.program, "u_layer2");
	hr->framebuffer.locs.u_factor =
		get_uniform_loc (hr->framebuffer.program, "u_factor");
	hr->framebuffer.locs.u_mult_f =
		get_uniform_loc (hr->framebuffer.program, "u_mult_f");
	hr->framebuffer.locs.u_mult_b =
		get_uniform_loc (hr->framebuffer.program, "u_mult_b");
	hr->framebuffer.locs.u_mult_1 =
		get_uniform_loc (hr->framebuffer.program, "u_mult_1");
	hr->framebuffer.locs.u_mult_2 =
		get_uniform_loc (hr->framebuffer.program, "u_mult_2");
	hr->framebuffer.locs.u_keyed_1 =
		get_uniform_loc (hr->framebuffer.program, "u_keyed_1");
	hr->framebuffer.locs.u_keyed_2 =
		get_uniform_loc (hr->framebuffer.program, "u_keyed_2");
	hr->framebuffer.locs.u_keyed_3d =
		get_uniform_loc (hr->framebuffer.program, "u_keyed_3d");
	hr->framebuffer.locs.u_above_1 =
		get_uniform_loc (hr->framebuffer.program, "u_above_1");
	hr->framebuffer.locs.u_above_2 =
		get_uniform_loc (hr->framebuffer.program, "u_above_2");
	hr->framebuffer.locs.u_invert_f =
		get_uniform_loc (hr->framebuffer.program, "u_invert_f");
	hr->framebuffer.locs.u_invert_b =
		get_uniform_loc (hr->framebuffer.program, "u_invert_b");
	hr->framebuffer.locs.u_clear =
		get_uniform_loc (hr->framebuffer.program, "u_clear");

	/* Generate two framebuffers (the front and the back buffers), each
	 * associated with a colorbuffer (texture) and a depth buffer
	 * (renderbuffer). */
	glGenFramebuffers (NUM_FRAMEBUFFERS, hr->framebuffer.fb);
	glGenTextures (NUM_FRAMEBUFFERS, hr->framebuffer.cb);
	glGenRenderbuffers (NUM_FRAMEBUFFERS, hr->framebuffer.db);
	VK_ASSERT_NO_GL_ERROR ();

	for (i = 0; i < NUM_FRAMEBUFFERS; i++) {

		/* Bind the i'th framebuffer */
		glBindFramebuffer (GL_FRAMEBUFFER, hr->framebuffer.fb[i]);
		VK_ASSERT_NO_GL_ERROR ();

		/* Create and attach a texture as color buffer. */
		glBindTexture (GL_TEXTURE_2D, hr->framebuffer.cb[i]);
		VK_ASSERT_NO_GL_ERROR ();
		glTexStorage2D (GL_TEXTURE_2D, 1, GL_RGBA8, 640, 480);
		VK_ASSERT_NO_GL_ERROR ();
		glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
		VK_ASSERT_NO_GL_ERROR ();
		glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
		VK_ASSERT_NO_GL_ERROR ();

		glFramebufferTexture2D (GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
		                        GL_TEXTURE_2D, hr->framebuffer.cb[i], 0);
		VK_ASSERT_NO_GL_ERROR ();

		glDrawBuffers (1, targets);
		VK_ASSERT_NO_GL_ERROR ();

		/* Create and attach a renderbuffer as depth buffer. */
		glBindRenderbuffer (GL_RENDERBUFFER, hr->framebuffer.db[i]);
		glRenderbufferStorage (GL_RENDERBUFFER, GL_DEPTH_COMPONENT, 640, 480);
		VK_ASSERT_NO_GL_ERROR ();

		glFramebufferRenderbuffer (GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT,
		                           GL_RENDERBUFFER, hr->framebuffer.db[i]);
		VK_ASSERT_NO_GL_ERROR ();

		if (glCheckFramebufferStatus (GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
			return -1;
	}

	glBindFramebuffer (GL_FRAMEBUFFER, 0);
	VK_ASSERT_NO_GL_ERROR ();

	/* The pictures of the last three lists, empty at first. */
	{
		uint8_t *empty = (uint8_t *) calloc (640 * 480, 4);

		if (!empty)
			return -1;
		for (i = 0; i < 640 * 480; i++)
			empty[i * 4 + 3] = 0xFF;

		glGenTextures (3, hr->framebuffer.pipe);
		glPixelStorei (GL_UNPACK_ALIGNMENT, 1);
		for (i = 0; i < 3; i++) {
			glBindTexture (GL_TEXTURE_2D, hr->framebuffer.pipe[i]);
			glTexStorage2D (GL_TEXTURE_2D, 1, GL_RGBA8, 640, 480);
			glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
			glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
			glTexSubImage2D (GL_TEXTURE_2D, 0, 0, 0, 640, 480,
			                 GL_RGBA, GL_UNSIGNED_BYTE, empty);
		}
		glBindTexture (GL_TEXTURE_2D, 0);
		free (empty);

		hr->framebuffer.shown = 2;
		hr->framebuffer.next = -1;
	}
	VK_ASSERT_NO_GL_ERROR ();

	/* The translucent fragments, the colour below them and a copy of the
	 * depth of the other types. */
	hr->framebuffer.lists.program =
		vk_renderer_compile_program (lists_vs_source, lists_fs_source);
	glUseProgram (hr->framebuffer.lists.program);
	glUniform1i (get_uniform_loc (hr->framebuffer.lists.program, "u_below"), 5);
	glUseProgram (0);
	VK_ASSERT_NO_GL_ERROR ();

	glGenTextures (1, &hr->framebuffer.lists.heads);
	glBindTexture (GL_TEXTURE_2D, hr->framebuffer.lists.heads);
	glTexStorage2D (GL_TEXTURE_2D, 1, GL_R32UI, 640, 480);
	glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
	glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	glGenFramebuffers (1, &hr->framebuffer.lists.heads_fb);
	glBindFramebuffer (GL_FRAMEBUFFER, hr->framebuffer.lists.heads_fb);
	glFramebufferTexture2D (GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
	                        GL_TEXTURE_2D, hr->framebuffer.lists.heads, 0);
	if (glCheckFramebufferStatus (GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
		return -1;
	glBindFramebuffer (GL_FRAMEBUFFER, 0);

	glGenTextures (1, &hr->framebuffer.lists.below);
	glBindTexture (GL_TEXTURE_2D, hr->framebuffer.lists.below);
	glTexStorage2D (GL_TEXTURE_2D, 1, GL_RGBA8, 640, 480);
	glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
	glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);

	glGenTextures (1, &hr->framebuffer.solid_depth);
	glBindTexture (GL_TEXTURE_2D, hr->framebuffer.solid_depth);
	glTexStorage2D (GL_TEXTURE_2D, 1, GL_DEPTH_COMPONENT24, 640, 480);
	glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
	glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	glBindTexture (GL_TEXTURE_2D, 0);

	glGenBuffers (1, &hr->framebuffer.lists.count);
	glBindBuffer (GL_SHADER_STORAGE_BUFFER, hr->framebuffer.lists.count);
	glBufferData (GL_SHADER_STORAGE_BUFFER, sizeof (GLuint), NULL,
	              GL_DYNAMIC_COPY);
	glBindBuffer (GL_SHADER_STORAGE_BUFFER, 0);
	glGenBuffers (1, &hr->framebuffer.lists.nodes);
	size_lists (hr, 4 * 640 * 480);
	VK_ASSERT_NO_GL_ERROR ();

	/* Create the VAO/VBO. */
	glGenVertexArrays (1, &hr->framebuffer.vao);
	glBindVertexArray (hr->framebuffer.vao);
	VK_ASSERT_NO_GL_ERROR ();

	glGenBuffers (1, &hr->framebuffer.vbo);
	glBindBuffer (GL_ARRAY_BUFFER, hr->framebuffer.vbo);
	glBufferData (GL_ARRAY_BUFFER,
	              sizeof (fb_vbo_data), fb_vbo_data, GL_STATIC_DRAW);
	VK_ASSERT_NO_GL_ERROR ();

	glVertexAttribPointer (0, 3, GL_FLOAT, GL_FALSE,
	                       sizeof (fb_vbo_data[0]),
	                       OFFSET (position));
	glVertexAttribPointer (1, 2, GL_FLOAT, GL_FALSE,
	                       sizeof (fb_vbo_data[0]),
	                       OFFSET (texcoords));
	VK_ASSERT_NO_GL_ERROR ();

	glEnableVertexAttribArray (0);
	glEnableVertexAttribArray (1);
	VK_ASSERT_NO_GL_ERROR ();

	glBindVertexArray (0);
	VK_ASSERT_NO_GL_ERROR ();

	return 0;
}

#undef OFFSET

static void
destroy_fb_state (hikaru_renderer_t *hr)
{
	glBindFramebuffer (GL_FRAMEBUFFER, 0);
	glDeleteFramebuffers (NUM_FRAMEBUFFERS, hr->framebuffer.fb);
	glDeleteTextures (NUM_FRAMEBUFFERS, hr->framebuffer.cb);
	glDeleteTextures (3, hr->framebuffer.pipe);
	glDeleteRenderbuffers (NUM_FRAMEBUFFERS, hr->framebuffer.db);
	glDeleteFramebuffers (1, &hr->framebuffer.lists.heads_fb);
	glDeleteTextures (1, &hr->framebuffer.lists.heads);
	glDeleteTextures (1, &hr->framebuffer.lists.below);
	glDeleteBuffers (1, &hr->framebuffer.lists.nodes);
	glDeleteBuffers (1, &hr->framebuffer.lists.count);
	glDeleteTextures (1, &hr->framebuffer.solid_depth);
	VK_ASSERT_NO_GL_ERROR ();

	vk_renderer_destroy_program (hr->framebuffer.lists.program);
	vk_renderer_destroy_program (hr->framebuffer.program);
}

static void
upload_layer (hikaru_renderer_t *hr, hikaru_layer_t *layer, GLuint *id, float *mult)
{
	void *data;

	LOG ("uploading LAYER %s", get_layer_str (layer));

	data = vk_buffer_get_ptr (hr->gpu->fb,
	                          layer->y0 * 4096 + layer->x0 * 4);

	/* Upload the layer data to a new texture. */
	glGenTextures (1, id);
	glActiveTexture (GL_TEXTURE0 + 0);
	glBindTexture (GL_TEXTURE_2D, *id);
	VK_ASSERT_NO_GL_ERROR ();

	glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	VK_ASSERT_NO_GL_ERROR ();
	glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
	glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	VK_ASSERT_NO_GL_ERROR ();
	glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, 0);
	VK_ASSERT_NO_GL_ERROR ();

	glPixelStorei (GL_UNPACK_ALIGNMENT, 1);
	switch (layer->format) {
	case HIKARU_FORMAT_ABGR1555: {
		/* The two 16-bit pixels of each 32-bit FB word are stored
		 * swapped, as the IDMA does for TEXRAM (MEASURED on the von4
		 * title text, GPU-RE/16 § 9). */
		static uint16_t pixels[640 * 480];
		unsigned x, y;

		for (y = 0; y < 480; y++) {
			const uint32_t *row = (const uint32_t *) ((const uint8_t *) data + y * 4096);
			uint16_t *dst = &pixels[y * 640];

			for (x = 0; x < 320; x++) {
				uint32_t w = row[x];

				dst[x * 2] = w >> 16;
				dst[x * 2 + 1] = w & 0xFFFF;
			}
		}

		glPixelStorei (GL_UNPACK_ROW_LENGTH, 0);

		glTexImage2D (GL_TEXTURE_2D, 0,
		              GL_RGB5_A1,
		              640, 480, 0,
		              GL_RGBA, GL_UNSIGNED_SHORT_1_5_5_5_REV,
		              pixels);

		*mult = 1.0f;
		break;
	}
	case HIKARU_FORMAT_A2BGR10:
		glPixelStorei (GL_UNPACK_ROW_LENGTH, 1024);

		glTexImage2D (GL_TEXTURE_2D, 0,
		              GL_RGB10_A2,
		              640, 480, 0,
		              GL_RGBA, GL_UNSIGNED_INT_2_10_10_10_REV,
		              data);

		*mult = 4.0f;
		break;
	default:
		VK_ASSERT (0);
	}
	glPixelStorei (GL_UNPACK_ROW_LENGTH, 0);
	VK_ASSERT_NO_GL_ERROR ();
}

/* The polygons reach the screen in two steps. The game libraries launch a
 * list only once the GPU 15 has analysed the last one and the GPU 1A has
 * plotted the one before (RACER @0C0118C0): a launch hands the last list
 * over to the GPU 1A, which plots it; it shows at the next frame. Nothing
 * moves without a launch. The 781, the 181 and the clear color act at once.
 *
 * On the RACER cabinet the SEGA logo stays two frames on the black of the
 * next scene, and the stars leave one frame before the text of the layers;
 * SGNASCAR keeps its white picture while it loads, with no list launched.
 *
 * TODO make use of the framebuffer configuration in the GPU:1A registers,
 * right now it only uses the information in the 781/181 commands. */
static void
draw (hikaru_renderer_t *hr)
{
	hikaru_gpu_t *gpu = hr->gpu;
	GLuint n1, n2, f, b, scene, shown, layer1 = 0, layer2 = 0;
	float factor, mult1 = 0.0f, mult2 = 0.0f;
	bool launched = hr->framebuffer.launched;

	VK_PRINT ("FRAMEBUFFER 781=%08X 181=%08X", gpu->fb_config._781, gpu->fb_config._181);

	hr->framebuffer.launched = 0;

	delete_dead_textures (hr);

	/* The picture plotted during the last frame. */
	if (hr->framebuffer.next >= 0) {
		hr->framebuffer.shown = hr->framebuffer.next;
		hr->framebuffer.next = -1;
	}

	n1 = gpu->fb_config.num_1;
	n2 = gpu->fb_config.num_2;

	factor = 0.0f;
	if (gpu->fb_config.blend)
		factor = gpu->fb_config.alpha * (1.0f / 255);

	/* Draw the list to the 3D buffer its 781 names. */
	scene = (n1 < 2) ? n1 : (n2 < 2) ? n2 : 0;
	if (launched) {
		glBindFramebuffer (GL_FRAMEBUFFER, hr->framebuffer.fb[scene]);
		VK_ASSERT_NO_GL_ERROR ();
		hr->framebuffer.scene = scene;

		draw_scene (hr);
		VK_ASSERT_NO_GL_ERROR ();
	}

	/* Setup the GL state for 2D drawing. */
	glDisable (GL_DEPTH_TEST);
	glDisable (GL_BLEND);
	glDisable (GL_SCISSOR_TEST);
	glViewport (0, 0, 640, 480);
	VK_ASSERT_NO_GL_ERROR ();

	glUseProgram (hr->framebuffer.program);
	VK_ASSERT_NO_GL_ERROR ();

	/* The blender output goes back into the buffer just drawn: the other
	 * 3D buffer then holds every earlier frame, fading by the factor at
	 * each frame. This is the motion blur of the cabinets (the stars of
	 * the PHARRIER logo scene trail behind them at 181 = 01C6). */
	if (launched && factor > 0.0f && n1 < 2 && n2 < 2 && n1 != n2) {
		glActiveTexture (GL_TEXTURE0 + 0);
		glBindTexture (GL_TEXTURE_2D, hr->framebuffer.cb[n2]);
		glActiveTexture (GL_TEXTURE0 + 1);
		glBindTexture (GL_TEXTURE_2D, hr->framebuffer.cb[n2]);
		glActiveTexture (GL_TEXTURE0 + 2);
		glBindTexture (GL_TEXTURE_2D, 0);
		glActiveTexture (GL_TEXTURE0 + 3);
		glBindTexture (GL_TEXTURE_2D, 0);

		glUniform1i (hr->framebuffer.locs.u_front,  0);
		glUniform1i (hr->framebuffer.locs.u_back,   1);
		glUniform1i (hr->framebuffer.locs.u_layer1, 2);
		glUniform1i (hr->framebuffer.locs.u_layer2, 3);
		glUniform1f (hr->framebuffer.locs.u_factor, 0.0f);
		glUniform1f (hr->framebuffer.locs.u_mult_f, 1.0f);
		glUniform1f (hr->framebuffer.locs.u_mult_b, 1.0f);
		glUniform1f (hr->framebuffer.locs.u_mult_1, 0.0f);
		glUniform1f (hr->framebuffer.locs.u_mult_2, 0.0f);
		glUniform1f (hr->framebuffer.locs.u_keyed_3d, 0.0f);
		glUniform1i (hr->framebuffer.locs.u_invert_f, 0);
		glUniform1i (hr->framebuffer.locs.u_invert_b, 0);
		glUniform4f (hr->framebuffer.locs.u_clear, 0.0f, 0.0f, 0.0f, 0.0f);

		glEnable (GL_BLEND);
		glBlendColor (0.0f, 0.0f, 0.0f, factor);
		glBlendFunc (GL_CONSTANT_ALPHA, GL_ONE_MINUS_CONSTANT_ALPHA);

		glBindVertexArray (hr->framebuffer.vao);
		glDrawArrays (GL_TRIANGLE_STRIP, 0, 4);
		glBindVertexArray (0);

		glDisable (GL_BLEND);
		VK_ASSERT_NO_GL_ERROR ();
	}

	/* Keep the picture; the one of the list before shows next. */
	if (launched) {
		const uint32_t num = hr->framebuffer.num_lists;

		glActiveTexture (GL_TEXTURE0 + 0);
		glBindTexture (GL_TEXTURE_2D, hr->framebuffer.pipe[num % 3]);
		glCopyTexSubImage2D (GL_TEXTURE_2D, 0, 0, 0, 0, 0, 640, 480);
		VK_ASSERT_NO_GL_ERROR ();

		if (num > 0)
			hr->framebuffer.next = (int32_t) ((num - 1) % 3);
		hr->framebuffer.num_lists = num + 1;
	}

	glBindFramebuffer (GL_FRAMEBUFFER, 0);
	VK_ASSERT_NO_GL_ERROR ();


	/* Composite the picture shown and the layers. */

	/* Two 3D buffers were blended when their list was drawn. */
	if (n1 < 2 && n2 < 2)
		factor = 0.0f;

	/* We only care about unit 0 for now. (Unit 1 is probably only used
	 * for the dual-monitor case.) */
	if ((LAYERS.layer[0][0].enabled || n1 == 2 || n2 == 2) &&
	    !hr->debug.flags[HR_DEBUG_NO_LAYER1])
		upload_layer (hr, &LAYERS.layer[0][0], &layer1, &mult1);

	if ((LAYERS.layer[0][1].enabled || n1 == 3 || n2 == 3) &&
	    !hr->debug.flags[HR_DEBUG_NO_LAYER2])
		upload_layer (hr, &LAYERS.layer[0][1], &layer2, &mult2);

	/* The two blender inputs: 0 and 1 are the 3D, 2 and 3 the 2D layers
	 * (PHARRIER blends layer 2 in for its logo screens, 781 = 0704/0705). */
	shown = hr->framebuffer.pipe[hr->framebuffer.shown];
	f = (n1 < 2) ? shown : (n1 == 2) ? layer1 : layer2;
	b = (n2 < 2) ? shown : (n2 == 2) ? layer1 : layer2;

	glActiveTexture (GL_TEXTURE0 + 0);
	glBindTexture (GL_TEXTURE_2D, f);
	VK_ASSERT_NO_GL_ERROR ();

	glActiveTexture (GL_TEXTURE0 + 1);
	glBindTexture (GL_TEXTURE_2D, b);
	VK_ASSERT_NO_GL_ERROR ();

	glActiveTexture (GL_TEXTURE0 + 2);
	glBindTexture (GL_TEXTURE_2D, (f != layer1 && b != layer1) ? layer1 : 0);
	VK_ASSERT_NO_GL_ERROR ();

	glActiveTexture (GL_TEXTURE0 + 3);
	glBindTexture (GL_TEXTURE_2D, (f != layer2 && b != layer2) ? layer2 : 0);
	VK_ASSERT_NO_GL_ERROR ();

	glUniform1i (hr->framebuffer.locs.u_front,  0);
	glUniform1i (hr->framebuffer.locs.u_back,   1);
	glUniform1i (hr->framebuffer.locs.u_layer1, 2);
	glUniform1i (hr->framebuffer.locs.u_layer2, 3);
	glUniform1f (hr->framebuffer.locs.u_factor, factor);
	/* Uniforms persist in the program: set them all, every frame. A
	 * layer that is not uploaded this frame has mult 0 and no texture. */
	glUniform1f (hr->framebuffer.locs.u_mult_f,
	             (f == layer1 && layer1) ? mult1 : (f == layer2 && layer2) ? mult2 : 1.0f);
	glUniform1f (hr->framebuffer.locs.u_mult_b,
	             (b == layer1 && layer1) ? mult1 : (b == layer2 && layer2) ? mult2 : 1.0f);
	glUniform1f (hr->framebuffer.locs.u_mult_1, (layer1 && f != layer1 && b != layer1) ? mult1 : 0.0f);
	glUniform1f (hr->framebuffer.locs.u_mult_2, (layer2 && f != layer2 && b != layer2) ? mult2 : 0.0f);
	glUniform1f (hr->framebuffer.locs.u_keyed_1, LAYERS.layer[0][0].keyed ? 1.0f : 0.0f);
	glUniform1f (hr->framebuffer.locs.u_keyed_2, LAYERS.layer[0][1].keyed ? 1.0f : 0.0f);
	glUniform1f (hr->framebuffer.locs.u_keyed_3d, LAYERS.fb_keyed ? 1.0f : 0.0f);
	glUniform1f (hr->framebuffer.locs.u_above_1, LAYERS.layer[0][0].above ? 1.0f : 0.0f);
	glUniform1f (hr->framebuffer.locs.u_above_2, LAYERS.layer[0][1].above ? 1.0f : 0.0f);
	glUniform1i (hr->framebuffer.locs.u_invert_f, (n1 >= 2) ? 1 : 0);
	glUniform1i (hr->framebuffer.locs.u_invert_b, (n2 >= 2) ? 1 : 0);
	glUniform4f (hr->framebuffer.locs.u_clear,
	             gpu->fb_config.clear[0] * INV255,
	             gpu->fb_config.clear[1] * INV255,
	             gpu->fb_config.clear[2] * INV255,
	             gpu->fb_config.clear[3] * INV255);
	VK_ASSERT_NO_GL_ERROR ();

	glBindVertexArray (hr->framebuffer.vao);
	VK_ASSERT_NO_GL_ERROR ();

	glDrawArrays (GL_TRIANGLE_STRIP, 0, 4);
	VK_ASSERT_NO_GL_ERROR ();

	glBindVertexArray (0);
	glUseProgram (0);
	VK_ASSERT_NO_GL_ERROR ();


	bury_texture (hr, layer1);
	bury_texture (hr, layer2);
}

/****************************************************************************
 Interface
****************************************************************************/

static void
hikaru_renderer_begin_frame (vk_renderer_t *renderer)
{
	hikaru_renderer_t *hr = (hikaru_renderer_t *) renderer;
	unsigned vpi, i;

	hr->meshes.variant.full = ~0;
	hr->meshes.frame++;
	hr->meshes.scissor_vp_index = ~0;
	hr->meshes.bound_tex_index = ~0;

	hr->list->num_vps = 0;
	hr->list->num_mvs = 0;
	hr->list->num_instances = 0;
	hr->list->verts.num = 0;
	hr->list->mv2_index = ~0;
	hr->list->num_mats = 0;
	hr->list->num_texs = 0;
	hr->list->num_lss = 0;

	for (vpi = 0; vpi < 8; vpi++)
		for (i = 0; i < 8; i++)
			hr->list->num_meshes[vpi][i] = 0;
	hr->list->total_meshes = 0;

	update_debug_flags (hr);

	/* Log the render state of one frame only (HR_LOG_FRAME=N). */
	{
		static int probe_want = -1;
		/* The frame being built follows the one shown */
		const int probe_frame = (int) hr->base.frame + 1;
		if (probe_want < 0)
			probe_want = vk_util_get_int_option ("HR_LOG_FRAME", 0);
		if (probe_want)
			hr->debug.flags[HR_DEBUG_LOG] = (probe_frame == probe_want);
		LOG ("frame %d clear=%02X%02X%02X%02X 181=%08X 781=%08X",
		     probe_frame,
		     hr->gpu->fb_config.clear[0], hr->gpu->fb_config.clear[1],
		     hr->gpu->fb_config.clear[2], hr->gpu->fb_config.clear[3],
		     hr->gpu->fb_config._181, hr->gpu->fb_config._781);
	}

	VK_ASSERT_NO_GL_ERROR ();
}

static void
hikaru_renderer_end_frame (vk_renderer_t *renderer)
{
	hikaru_renderer_t *hr = (hikaru_renderer_t *) renderer;
	hikaru_gpu_t *gpu = hr->gpu;

	VK_ASSERT_NO_GL_ERROR ();

	/* The CP may still be in the middle of a mesh: draw what it pushed
	 * so far and let it start a new mesh on the next vertex. */
	if (hr->meshes.current) {
		hikaru_renderer_end_mesh (renderer, gpu->cp.pc);
		gpu->state.in_mesh = 0;
	}

	draw (hr);
	VK_ASSERT_NO_GL_ERROR ();

	LOG (" ==== RENDSTATE STATISTICS ==== ");
	LOG ("  vp  : %u", hr->list->num_vps);
	LOG ("  mv  : %u", hr->list->num_mvs);
	LOG ("  mat : %u", hr->list->num_mats);
	LOG ("  tex : %u", hr->list->num_texs);
	LOG ("  ls  : %u", hr->list->num_lss);
}

static void
hikaru_renderer_reset (vk_renderer_t *renderer)
{
	hikaru_renderer_invalidate_texcache (renderer, NULL);
}

/* What the renderer keeps from one frame to the next: the two 3D color
 * buffers, which the blender feeds back, and the pictures on their way to
 * the screen. The textures are decoded again from the texture RAM. */

int
hikaru_renderer_save_state (vk_renderer_t *renderer, vk_state_t *state)
{
	hikaru_renderer_t *hr = (hikaru_renderer_t *) renderer;
	const uint32_t size = 640 * 480 * 4;
	uint8_t *pixels = (uint8_t *) malloc (size);
	int ret = 0;
	unsigned i;

	if (!pixels)
		return -1;

	glPixelStorei (GL_PACK_ALIGNMENT, 1);
	for (i = 0; i < NUM_FRAMEBUFFERS; i++) {
		glBindFramebuffer (GL_READ_FRAMEBUFFER, hr->framebuffer.fb[i]);
		glReadBuffer (GL_COLOR_ATTACHMENT0);
		glReadPixels (0, 0, 640, 480, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
		ret |= vk_state_put (state, pixels, size);
	}
	glBindFramebuffer (GL_READ_FRAMEBUFFER, 0);
	for (i = 0; i < 3; i++) {
		glBindTexture (GL_TEXTURE_2D, hr->framebuffer.pipe[i]);
		glGetTexImage (GL_TEXTURE_2D, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
		ret |= vk_state_put (state, pixels, size);
	}
	glBindTexture (GL_TEXTURE_2D, 0);
	VK_ASSERT_NO_GL_ERROR ();

	ret |= vk_state_put (state, &hr->framebuffer.num_lists, sizeof (hr->framebuffer.num_lists));
	ret |= vk_state_put (state, &hr->framebuffer.shown, sizeof (hr->framebuffer.shown));
	ret |= vk_state_put (state, &hr->framebuffer.next, sizeof (hr->framebuffer.next));
	ret |= vk_state_put (state, &hr->framebuffer.launched, sizeof (hr->framebuffer.launched));

	free (pixels);
	return ret;
}

int
hikaru_renderer_load_state (vk_renderer_t *renderer, vk_state_t *state)
{
	hikaru_renderer_t *hr = (hikaru_renderer_t *) renderer;
	const uint32_t size = 640 * 480 * 4;
	uint8_t *pixels = (uint8_t *) malloc (size);
	int ret = 0;
	unsigned i;

	if (!pixels)
		return -1;

	glPixelStorei (GL_UNPACK_ALIGNMENT, 1);
	for (i = 0; i < NUM_FRAMEBUFFERS; i++) {
		ret |= vk_state_get (state, pixels, size);
		if (ret)
			break;
		glBindTexture (GL_TEXTURE_2D, hr->framebuffer.cb[i]);
		glTexSubImage2D (GL_TEXTURE_2D, 0, 0, 0, 640, 480,
		                 GL_RGBA, GL_UNSIGNED_BYTE, pixels);
	}
	for (i = 0; i < 3 && !ret; i++) {
		ret |= vk_state_get (state, pixels, size);
		if (ret)
			break;
		glBindTexture (GL_TEXTURE_2D, hr->framebuffer.pipe[i]);
		glTexSubImage2D (GL_TEXTURE_2D, 0, 0, 0, 640, 480,
		                 GL_RGBA, GL_UNSIGNED_BYTE, pixels);
	}
	glBindTexture (GL_TEXTURE_2D, 0);
	VK_ASSERT_NO_GL_ERROR ();

	ret |= vk_state_get (state, &hr->framebuffer.num_lists, sizeof (hr->framebuffer.num_lists));
	ret |= vk_state_get (state, &hr->framebuffer.shown, sizeof (hr->framebuffer.shown));
	ret |= vk_state_get (state, &hr->framebuffer.next, sizeof (hr->framebuffer.next));
	ret |= vk_state_get (state, &hr->framebuffer.launched, sizeof (hr->framebuffer.launched));

	hikaru_renderer_invalidate_texcache (renderer, NULL);

	free (pixels);
	return ret;
}

static void
hikaru_renderer_destroy (vk_renderer_t **renderer_)
{
	if (renderer_) {
		hikaru_renderer_t *hr = (hikaru_renderer_t *) *renderer_;

		destroy_3d_state (hr);
		destroy_fb_state (hr);

		hikaru_renderer_invalidate_texcache (*renderer_, NULL);
		delete_dead_textures (hr);
		free (hr->textures.dead);
	}
}

vk_renderer_t *
hikaru_renderer_new (vk_buffer_t *fb, vk_buffer_t *texram[2])
{
	hikaru_renderer_t *hr;
	int ret;

	hr = ALLOC (hikaru_renderer_t);
	if (!hr)
		goto fail;

	hr->base.width = 640;
	hr->base.height = 480;

	hr->base.destroy = hikaru_renderer_destroy;
	hr->base.reset = hikaru_renderer_reset;
	hr->base.begin_frame = hikaru_renderer_begin_frame;
	hr->base.end_frame = hikaru_renderer_end_frame;

	ret = vk_renderer_init ((vk_renderer_t *) hr);
	if (ret)
		goto fail;

	VK_ASSERT_NO_GL_ERROR ();

	memset ((void *) program_cache, 0, sizeof (program_cache));
	num_programs = 0;

	init_debug_flags (hr);

	if (build_3d_state (hr))
		goto fail;
	VK_ASSERT_NO_GL_ERROR ();

	if (build_fb_state (hr))
		goto fail;
	VK_ASSERT_NO_GL_ERROR ();

	return (vk_renderer_t *) hr;

fail:
	hikaru_renderer_destroy ((vk_renderer_t **) &hr);
	return NULL;
}

void
hikaru_renderer_set_gpu (vk_renderer_t *renderer, void *gpu_as_void)
{
	hikaru_renderer_t *hr = (hikaru_renderer_t *) renderer;
	hikaru_gpu_t *gpu = (hikaru_gpu_t *) gpu_as_void;

	hr->gpu = gpu;
}
