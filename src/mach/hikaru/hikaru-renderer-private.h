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

/* Translucent layers found between two answers of the GPU */
#define MAX_LAYER_BATCH 32

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
	} u_lights[4];
	GLuint		u_ambient;
	GLuint		u_texture;
	GLuint		u_fog;
	GLuint		u_fog_color;
	GLuint		u_alpha_ref;
	GLuint		u_weigh_by_alpha;
	GLuint		u_find_layer;
	GLuint		u_last_layer;
	GLuint		u_solid_depth;
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
	int		find_layer;
} hikaru_mesh_held_t;

typedef struct {
	GLuint id;
	hikaru_texhead_t th;
} hikaru_texture_t;

typedef struct {
	uint32_t		first;	/* first vertex in the buffer of the frame */
	uint32_t		num_tris;
	uint32_t		addr[2];
	uint32_t		vp_index;
	uint32_t		mv_index;
	uint32_t		mv2_index;	/* second modelview */
	uint32_t		num_instances;
	uint32_t		mat_index;
	uint32_t		tex_index;
	uint32_t		ls_index;
	float			alpha_thresh[2];
	float			depth_bias;
	float			poly_alpha;	/* 103 alpha, logged */
	bool			has_two_sided_lighting;
	uint32_t		num;
	uint32_t		polytype;
} hikaru_mesh_t;

typedef struct {
	vk_renderer_t base;

	hikaru_gpu_t *gpu;

	hikaru_viewport_t	*vp_list;
	uint32_t		 num_vps;

	hikaru_modelview_t	*mv_list;
	uint32_t		 num_mvs;
	uint32_t		 num_instances;
	uint32_t		 mv2_index;	/* last second modelview listed this frame */

	hikaru_material_t	*mat_list;
	uint32_t		 num_mats;

	hikaru_texhead_t	*tex_list;
	uint32_t		 num_texs;

	hikaru_lightset_t	*ls_list;
	uint32_t		 num_lss;

	hikaru_mesh_t		*mesh_list[8][8];
	uint32_t		 num_meshes[8][8];
	uint32_t		 total_meshes;

	struct {
		unsigned		num_verts, num_tris;
		hikaru_vertex_t		tmp[4];
		hikaru_vertex_body_t	all[MAX_VERTICES_PER_MESH];
	} push;

	/* The vertices of all the meshes of the frame, uploaded in one go */
	struct {
		hikaru_vertex_body_t	*all;
		uint32_t		num, max;
	} verts;

	struct {
		hikaru_mesh_t		*current;

		hikaru_glsl_variant_t	variant;
		GLuint			program;
		GLuint			vao, vbo;
		float			alpha_ref;
		bool			depth_bias;
		bool			weigh_by_alpha;
		bool			find_layer;	/* pass that finds a translucent layer */

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
	} textures;

	struct {
		GLuint program;
		GLuint fb[2], cb[2], db[2];
		/* The pictures of the last three lists, see draw () */
		GLuint pipe[3];
		uint32_t num_lists;	/* lists drawn so far */
		uint32_t shown;		/* the picture on screen */
		int32_t next;		/* the one it takes at the next frame, or -1 */
		uint32_t launched;	/* a list was launched during this frame */
		/* Translucent layers, see draw_translucent_meshes */
		struct {
			GLuint fb, depth, query;
		} layers[MAX_LAYER_BATCH + 1];	/* [0] is the last one blended */
		GLuint blend_fb;
		GLuint solid_depth;	/* depth left by the other types */
		unsigned num_layers[8];	/* per viewport, at the last frame */
		unsigned scene;		/* the 3D buffer being drawn */
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

