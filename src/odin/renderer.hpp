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

#ifndef ODIN_RENDERER_HPP
#define ODIN_RENDERER_HPP

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

#include <GL/glew.h>

#include "odin/antarctic_list.h"
#include "odin/eurasia_layers.h"
#include "odin/renderer_programs.hpp"
#include "odin/renderer_textures.hpp"

namespace odin {

class HikaruGpu;

/* Draws what the graphics chips of the Hikaru hand over, with OpenGL 4.3:
 * it stands for the chips that plot the polygons (see hikaru_gpu.hpp).
 *
 * The command processor leaves the display list of a frame, the display chip
 * the two 2D layers. The picture comes out in three steps:
 *
 *  - the list launched during a frame is drawn at the end of that frame,
 *    viewport by viewport and polygon type by polygon type
 *    (renderer_scene.cpp, renderer_translucent.cpp);
 *  - its picture is blended into a 3D buffer of the hardware and waits: it
 *    reaches the screen at the next launch (renderer.cpp);
 *  - the picture on the screen is composed with the layers at every frame
 *    (renderer.cpp, renderer_layers.cpp), then brought to the size of the
 *    window.
 *
 * Two sizes are told apart. The screen of the hardware, WIDTH by HEIGHT, is
 * what the games address: the centre of a view, its clip window, the 2D
 * layers, the 501 pattern, the pictures of a save state. The buffers the 3D
 * is drawn in are `scale` times as wide and as high, and so is the picture
 * on its way to the screen: a pixel of the hardware is scale x scale pixels
 * there. The window has a size of its own.
 *
 * An OpenGL 4.3 context must be current for every call, the constructor
 * included. */
class Renderer {
public:
	/* The screen of the hardware, in pixels */
	static constexpr unsigned WIDTH = 640;
	static constexpr unsigned HEIGHT = 480;

	/* The memories of the board the renderer reads */
	struct Memory {
		std::span<const uint8_t> fb;		/* framebuffer RAM, 8 MB */
		std::span<const uint8_t> texram[2];	/* texture RAM, two banks of 4 MB */
	};
	/* For looking at what is drawn. Everything is off at zero. */
	struct Options {
		bool log;		/* the objects of every mesh, on stdout */
		int log_frame;		/* that log for this frame only */
		bool list_fp;		/* a fingerprint of each list, on stderr */
		int list_dump;		/* with a line for each mesh of this frame, or -1 */
		bool lists_test;	/* no room for the translucent fragments at first */
		bool debug_keys;	/* the keys change the debug switches */
		bool (*key_down)(void *ctx, char key);
		void *ctx;
	};

	Renderer(const Memory &memory, const Options &options);
	~Renderer();
	Renderer(const Renderer &) = delete;
	Renderer &operator=(const Renderer &) = delete;

	void set_gpu(HikaruGpu *gpu) { m_gpu = gpu; }

	/* Draws the 3D `scale` times finer than the screen of the hardware,
	 * 1 at first. The pictures kept are carried over, pixel for pixel. */
	void set_scale(unsigned scale);

	/* A display list was launched. */
	void launch() { m_launched = 1; }
	/* The texture RAM of a bank has changed; every bank if negative. */
	void invalidate_textures(int bank) { m_textures.invalidate(bank); }

	/* A frame of the machine: begin_frame before it runs, end_frame once
	 * it has, with the number of that frame. The picture of the frame is
	 * shown in the window, of window_w by window_h pixels: as large as
	 * fits, in the shape of the screen of the hardware, black around. */
	void begin_frame(unsigned frame);
	void end_frame(unsigned frame, unsigned window_w, unsigned window_h);

	/* The picture of the last frame, whatever the size of the window:
	 * the framebuffer it can be read from, and its size. */
	GLuint frame_picture(unsigned &w, unsigned &h) const
	{
		w = width();
		h = height();
		return m_frame_fb;
	}

	/* The debug switches as text, for the title of the window; empty
	 * without the debug keys. */
	const char *message() const { return m_message; }

	/* Save state. The renderer keeps from one frame to the next the two
	 * 3D buffers, which the blender feeds back, and the pictures on their
	 * way to the screen: PICTURES pictures of PICTURE_SIZE bytes, then
	 * the counters. A picture of a state has the size of the screen of
	 * the hardware whatever the scale: one pixel of each scale x scale is
	 * saved, and comes back as all of them. The textures are decoded
	 * again from the texture RAM. */
	static constexpr unsigned PICTURES = 5;
	static constexpr size_t PICTURE_SIZE = size_t{WIDTH} * HEIGHT * 4;
	struct Counters {
		uint32_t num_lists;
		uint32_t shown;
		int32_t next;
		uint32_t launched;
	};
	void save(std::span<uint8_t> pictures, Counters &counters);
	void load(std::span<const uint8_t> pictures, const Counters &counters);

private:
	/* The switches of the debug keys, see renderer_debug.cpp */
	enum Debug : unsigned {
		DEBUG_LOG,
		DEBUG_NO_LAYER1,
		DEBUG_NO_LAYER2,
		DEBUG_NO_3D,
		DEBUG_NO_TEXTURES,
		DEBUG_NO_MIPMAPS,
		DEBUG_SELECT_POLYTYPE,
		DEBUG_NO_INSTANCING,
		DEBUG_SELECT_INSTANCE,
		DEBUG_NO_LIGHTING,
		DEBUG_NO_AMBIENT,
		DEBUG_NO_DIFFUSE,
		DEBUG_NO_FOG,

		NUM_DEBUG
	};
	/* How the meshes of the polygon type being drawn are drawn */
	struct Pass {
		float alpha_ref;	/* fragments at or below it are dropped */
		bool depth_bias;	/* the 101 bias moves the mesh */
		bool weigh_by_alpha;	/* the colour is weighted by its alpha */
		bool collect;		/* the fragments go to the lists */
		unsigned blend_mode;	/* A81, of the mesh */
		uint32_t order;		/* triangles sent so far */
	};
	/* The list drawn last, until its translucent fragments are counted:
	 * see check_last_list */
	struct Last {
		odin_display_list_t *list;	/* nullptr once counted */
		unsigned buffer;	/* its 3D buffer */
		int back;		/* the one blended into it, or -1 */
		uint32_t rate;		/* 181 */
		uint32_t slot;		/* its picture in the pipe */
		uint32_t views;		/* that have such fragments */
	};
	/* The texture each texhead of a list was drawn with, see tex_ids */
	struct ListTextures {
		const odin_display_list_t *list;
		std::vector<GLuint> ids;
	};

	/* renderer.cpp */
	void build_blender();
	void build_quad();
	void build_compose();
	void build_presenter();
	void forget_held();
	void set_2d_state();
	void show(unsigned frame);
	void draw_list();
	void check_last_list();
	void compose(const odin_fb_config_t &fb);
	void present(unsigned window_w, unsigned window_h);

	/* renderer_buffers.cpp */
	unsigned width() const { return WIDTH * m_scale; }
	unsigned height() const { return HEIGHT * m_scale; }
	void build_buffers();
	void destroy_buffers(bool pictures_too);
	static GLuint new_buffer_texture(GLenum format, unsigned w, unsigned h);
	static GLuint new_framebuffer(GLuint color);
	GLuint &picture(unsigned i) { return (i < 2) ? m_buffers[i] : m_pipe[i - 2]; }
	void copy_picture(GLuint from, unsigned from_w, unsigned from_h,
	                  GLuint to, unsigned to_w, unsigned to_h);

	/* renderer_scene.cpp */
	void draw_scene();
	void draw_polytype(unsigned vpi, unsigned polytype);
	void draw_mesh(const odin_mesh_t &mesh);
	void draw_instance(const odin_mesh_t &mesh, unsigned i);
	void upload_vertices();
	MeshVariant variant_of(const odin_mesh_t &mesh) const;
	bool is_faded_by_depth(const odin_mesh_t &mesh) const;
	void use_program(const odin_mesh_t &mesh);
	void upload_viewport(const odin_mesh_t &mesh);
	void upload_modelview(const odin_mesh_t &mesh, unsigned i);
	void upload_texture(const odin_mesh_t &mesh);
	void upload_lightset(const odin_mesh_t &mesh);
	void set_material_blend(const odin_mesh_t &mesh);
	void set_opaque_blend(const odin_mesh_t &mesh);
	std::vector<GLuint> &tex_ids();

	/* renderer_translucent.cpp */
	void build_lists();
	void destroy_lists();
	void size_lists(uint32_t max);
	bool grow_lists(uint32_t used);
	uint32_t room_at_start() const;
	void draw_translucent(unsigned vpi, const odin_mesh_t *meshes, unsigned num);

	/* renderer_layers.cpp */
	GLuint upload_layer(const odin_layer_t &layer, float &mult);

	/* renderer_debug.cpp */
	void init_debug();
	void update_debug(unsigned frame);
	bool logging() const { return m_debug[DEBUG_LOG] != 0; }
	void log(const char *fmt, ...) const __attribute__((format(printf, 2, 3)));
	void log_mesh(const odin_mesh_t &mesh) const;
	void print_list_fingerprint(const odin_fb_config_t &fb, unsigned frame) const;
	static void error(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
	[[noreturn]] static void fatal(const char *what);

	HikaruGpu *m_gpu;
	std::span<const uint8_t> m_fb;
	Options m_options;

	/* The list of the frame, built by the command processor */
	odin_display_list_t *m_list;
	/* The command processor builds its lists in two buffers. */
	ListTextures m_list_textures[2];

	TextureCache m_textures;
	MeshPrograms m_programs;

	/* The meshes: their vertices, the program in use and what is set
	 * for the list being drawn */
	GLuint m_mesh_vao, m_mesh_vbo;
	MeshProgram *m_program;		/* nullptr: none of the mesh programs */
	MeshVariant m_variant;		/* of m_program */
	uint64_t m_variant_key;
	uint32_t m_drawing;		/* counts the lists drawn, see forget_held */
	/* The matrix of the normals sent last, see upload_modelview */
	float m_normal[3][3];
	uint32_t m_scissor_vp_index;	/* of the list being drawn */
	uint32_t m_bound_tex_index;	/* of the list being drawn, on the unit 0 */
	Pass m_pass;
	/* The list is drawn again, see check_last_list */
	bool m_again;

	/* How many times finer than the screen of the hardware the buffers
	 * below are */
	unsigned m_scale;
	/* Where a list is drawn: a colour buffer and a depth buffer */
	GLuint m_scene_fb, m_scene_color, m_scene_depth;
	/* The depth the polygon types other than the translucent left */
	GLuint m_solid_depth;
	/* The two 3D buffers of the hardware: the picture last blended into
	 * each */
	GLuint m_buffers[2];
	/* The pictures of the last three lists, see show */
	GLuint m_pipe[3], m_pipe_fb[3];
	uint32_t m_num_lists;		/* lists drawn so far */
	uint32_t m_shown;		/* the picture on the screen */
	int32_t m_next;			/* the one it takes at the next frame, or -1 */
	uint32_t m_launched;		/* a list was launched during this frame */
	Last m_last;
	/* For copy_picture, and a picture of the size of the screen of the
	 * hardware for the save states */
	GLuint m_copy_fb[2];
	GLuint m_state_picture;

	/* Blends two 3D buffers, see draw_list */
	GLuint m_blender;
	GLint m_blender_rate;
	/* The picture of the frame: the 3D and the layers, composed */
	GLuint m_frame_fb, m_frame_color;
	/* Brings it to the size of the window */
	GLuint m_presenter;
	GLint m_presenter_rect;
	/* Composes the picture and the layers */
	GLuint m_quad_vao, m_quad_vbo;
	GLuint m_compose;
	struct {
		GLint front, back, layer1, layer2;
		GLint factor;
		GLint mult_f, mult_b, mult_1, mult_2;
		GLint keyed_1, keyed_2, keyed_3d;
		GLint above_1, above_2;
		GLint invert_f, invert_b;
		GLint clear;
	} m_compose_locs;
	std::vector<uint16_t> m_layer_pixels;

	/* The translucent fragments, see renderer_translucent.cpp */
	struct {
		GLuint program;		/* blends them */
		GLuint heads, heads_fb;	/* the last one of each pixel */
		GLuint nodes;
		GLuint count[8];	/* one per viewport */
		uint32_t max;		/* nodes */
		uint32_t limit;		/* nodes the graphics card takes in a buffer */
		bool full;		/* a list did not fit, and it was told */
		GLuint below;		/* the colour under them */
	} m_lists;

	int m_debug[NUM_DEBUG];
	bool m_debug_held[NUM_DEBUG];
	char m_message[256];
};

} /* namespace odin */

#endif /* ODIN_RENDERER_HPP */
