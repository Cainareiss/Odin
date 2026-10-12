/* 
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

#ifndef __HIKARU_RENDERER_PRIVATE_H__
#define __HIKARU_RENDERER_PRIVATE_H__

#include "mach/hikaru/hikaru-gpu-private.h"

enum {
	HR_DEBUG_LOG,
	HR_DEBUG_NO_LAYER1,
	HR_DEBUG_NO_LAYER2,
	HR_DEBUG_NO_3D,
	HR_DEBUG_SELECT_VIEWPORT,
	HR_DEBUG_NO_TEXTURES,
	HR_DEBUG_NO_MIPMAPS,
	HR_DEBUG_SELECT_POLYTYPE,
	HR_DEBUG_NO_INSTANCING,
	HR_DEBUG_SELECT_INSTANCE,
	HR_DEBUG_NO_LIGHTING,
	HR_DEBUG_NO_AMBIENT,
	HR_DEBUG_NO_DIFFUSE,
	HR_DEBUG_NO_SPECULAR,
	HR_DEBUG_NO_FOG,

	HR_NUM_DEBUG_VARS
};

typedef union {
	struct {
		uint64_t has_texture		: 1;
		uint64_t has_lighting		: 1;
		uint64_t has_two_sided_lighting	: 1;
		uint64_t shading_mode		: 2;
		uint64_t alpha_mode		: 1;
		uint64_t tex_mode		: 3;

		uint64_t has_light0		: 1;
		uint64_t light0_type		: 2;
		uint64_t light0_att_type	: 3;

		uint64_t has_light1		: 1;
		uint64_t light1_type		: 2;
		uint64_t light1_att_type	: 3;

		uint64_t has_light2		: 1;
		uint64_t light2_type		: 2;
		uint64_t light2_att_type	: 3;

		uint64_t has_light3		: 1;
		uint64_t light3_type		: 2;
		uint64_t light3_att_type	: 3;

		uint64_t has_fog		: 1;
		uint64_t has_cue		: 1;

		uint64_t collect		: 1;	/* see draw_translucent_meshes */
		uint64_t has_pattern		: 1;	/* 501 */
	};
	uint64_t full;
} hikaru_glsl_variant_t;

/* Uniform locations of a mesh program */
typedef struct {
	GLuint		u_projection;
	GLuint		u_modelview;
	GLuint		u_normal;
	GLuint		u_modelview2;
	GLuint		u_depth_scale;
	GLuint		u_normal2;
	struct {
		GLuint	position;
		GLuint	direction;
		GLuint	color;
		GLuint	ambient;
		GLuint	extents;
		GLuint	spot;
		GLuint	cone;
		GLuint	table;
	} u_lights[4];
	GLuint		u_ambient;
	GLuint		u_texture;
	GLuint		u_fog;
	GLuint		u_fog_color;
	GLuint		u_alpha_ref;
	GLuint		u_weigh_by_alpha;
	GLuint		u_solid_depth;
	GLuint		u_order;
	GLuint		u_blend_mode;
	GLuint		u_pattern;
} hikaru_mesh_locs_t;

/* What a mesh program was given last, so as not to give it again: indices
 * in the lists of the frame `frame`. */
typedef struct {
	uint32_t	frame;
	uint32_t	vp_index;
	uint32_t	ls_index, ls_vp_index;
	uint32_t	mv_index, mv2_index;
	float		alpha_ref;
	float		depth_scale;
	int		weigh_by_alpha;
	uint32_t	pattern;
} hikaru_mesh_held_t;

typedef struct hikaru_texture_t hikaru_texture_t;

struct hikaru_texture_t {
	GLuint id;
	odin_texhead_t th;
	/* Another size or format read at the same slot */
	hikaru_texture_t *next;
};

/* How a light of the list is drawn */
typedef enum {
	HIKARU_LIGHT_TYPE_DIRECTIONAL,
	HIKARU_LIGHT_TYPE_POSITIONAL,
	HIKARU_LIGHT_TYPE_SPOT,
	HIKARU_LIGHT_TYPE_UNDIRECTED,

	HIKARU_NUM_LIGHT_TYPES
} hikaru_light_type_t;

typedef enum {
	HIKARU_LIGHT_ATT_LINEAR = 0,
	HIKARU_LIGHT_ATT_SQUARE = 1,
	HIKARU_LIGHT_ATT_INVLINEAR = 2,
	HIKARU_LIGHT_ATT_INVSQUARE = 3,
	HIKARU_LIGHT_ATT_INF = 4,

	HIKARU_NUM_LIGHT_ATTS
} hikaru_light_att_t;

typedef struct {
	vk_renderer_t base;

	hikaru_gpu_t *gpu;

	/* The list of the frame, built by the command processor */
	odin_display_list_t	*list;
	/* The texture each of its texheads was drawn with, see
	 * list_tex_ids */
	struct {
		const odin_display_list_t *list;
		GLuint *ids;
	} tex_ids[2];

	struct {
		hikaru_glsl_variant_t	variant;
		GLuint			program;
		GLuint			vao, vbo;
		float			alpha_ref;
		bool			depth_bias;
		bool			weigh_by_alpha;
		/* The pass that collects the translucent fragments */
		bool			collect;
		/* The list is drawn again, see check_last_list */
		bool			again;
		unsigned		blend_mode;
		uint32_t		order;	/* triangles sent so far */

		hikaru_mesh_locs_t	locs;

		/* See hikaru_mesh_held_t */
		hikaru_mesh_held_t	*held;
		uint32_t		frame;
		uint32_t		scissor_vp_index;	/* of this frame */
		uint32_t		bound_tex_index;	/* of this frame, on unit 0 */
	} meshes;

	struct {
		hikaru_texture_t cache[2][0x40][0x80];
		bool is_clear[2];
		/* No longer wanted, see bury_texture */
		GLuint *dead;
		uint32_t num_dead, max_dead;
	} textures;

	struct {
		GLuint program;
		/* Where a list is drawn, and the two 3D buffers of the
		 * hardware: the picture last blended into each */
		GLuint fb, cb, db;
		GLuint bank[2];
		/* The pictures of the last three lists, see draw () */
		GLuint pipe[3], pipe_fb[3];
		uint32_t num_lists;	/* lists drawn so far */
		uint32_t shown;		/* the picture on screen */
		int32_t next;		/* the one it takes at the next frame, or -1 */
		uint32_t launched;	/* a list was launched during this frame */
		/* Translucent fragments, see draw_translucent_meshes */
		struct {
			GLuint program;		/* blends them */
			GLuint heads, heads_fb;	/* the last one of each pixel */
			GLuint nodes;
			GLuint count[8];	/* one per viewport */
			uint32_t max;		/* nodes */
			bool test;		/* ODIN_LISTS_TEST */
			GLuint below;		/* the colour under them */
		} lists;
		/* The list drawn last, until its translucent fragments are
		 * counted: see check_last_list */
		struct {
			odin_display_list_t *list;	/* NULL once counted */
			unsigned bank;		/* its 3D buffer */
			int back;		/* the one blended into it, or -1 */
			uint32_t rate;		/* 181 */
			uint32_t slot;		/* its picture in the pipe */
			uint32_t views;		/* that have such fragments */
		} last;
		/* Blends two 3D buffers, see draw_list */
		struct {
			GLuint program;
			GLuint u_rate;
		} blend;
		GLuint solid_depth;	/* depth left by the other types */
		GLuint vao, vbo;
		struct {
			GLuint u_front;
			GLuint u_back;
			GLuint u_layer1;
			GLuint u_layer2;
			GLuint u_factor;
			GLuint u_mult_f;
			GLuint u_mult_b;
			GLuint u_mult_1;
			GLuint u_mult_2;
			GLuint	u_keyed_1;
			GLuint	u_keyed_2;
			GLuint	u_keyed_3d;
			GLuint	u_above_1;
			GLuint	u_above_2;
			GLuint u_invert_f;
			GLuint u_invert_b;
			GLuint u_clear;
		} locs;
	} framebuffer;

	struct {
		int32_t flags[HR_NUM_DEBUG_VARS];
		/* ODIN_DEBUG_KEYS: the keys change the flags */
		bool keys;
		/* ODIN_LIST_FP, ODIN_LIST_DUMP: see print_list_fingerprint */
		bool list_fp;
		int list_dump;
		bool held[HR_NUM_DEBUG_VARS];
	} debug;

} hikaru_renderer_t;

#define LOG(fmt_, args_...) \
	do { \
		if (hr->debug.flags[HR_DEBUG_LOG]) \
			fprintf (stdout, "\tHR: " fmt_"\n", ##args_); \
	} while (0)

uint16_t	abgr1555_to_rgba5551 (uint16_t pixel);
uint16_t	abgr4444_to_rgba4444 (uint16_t pixel);
uint32_t	a8_to_rgba8888 (uint32_t a);

#endif /* __HIKARU_RENDERER_PRIVATE_H__ */

