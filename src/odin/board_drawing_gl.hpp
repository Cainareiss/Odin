/*
 * Odin
 * Copyright (C) 2026 Réda Chérif-Touil
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef ODIN_BOARD_DRAWING_GL_HPP
#define ODIN_BOARD_DRAWING_GL_HPP

/* The board drawing with OpenGL (board_drawing.hpp), for its own files
 * only: board_drawing_gl.cpp (the textures), board_pictures_gl.cpp (the
 * pictures and what goes from one to the next). */

#include <memory>
#include <optional>
#include <string>

#include <GL/glew.h>

#include "odin/board_drawing.hpp"
#include "odin/hikaru_draw.hpp"
#include "odin/picture_filter.hpp"

namespace odin {

class GlBoardDrawing : public BoardDrawing {
public:
	GlBoardDrawing();
	~GlBoardDrawing() override;
	GlBoardDrawing(const GlBoardDrawing &) = delete;
	GlBoardDrawing &operator=(const GlBoardDrawing &) = delete;

	void set_scale(unsigned scale) override;
	uint32_t scene_framebuffer() const override { return m_scene_fb; }

	void scene_to_pipe(unsigned slot) override;
	void blend_scene_to_pipe(unsigned slot, unsigned buffer, uint32_t rate) override;
	void keep_picture(unsigned slot, unsigned buffer) override;

	void set_layer(unsigned i, LayerTexels texels, const void *pixels) override;
	void compose(const Compose &compose, unsigned shown, const bool layers[2]) override;
	void present(PictureRect rect, unsigned window_w, unsigned window_h) override;
	HostPicture frame_picture() const override { return { m_frame_fb, width(), height() }; }
	void set_filter(const std::string &preset) override;
	void draw_loading(std::span<const LoadingRect> rects, unsigned window_w, unsigned window_h) override;

	void read_kept(std::span<uint8_t> pictures) override;
	void write_kept(std::span<const uint8_t> pictures) override;

	HostTexture make_texture(Texels texels, std::span<const TexelRect> levels) override;
	void forget_textures(std::span<const HostTexture> textures) override;
	HostSampler make_sampler(const TextureSampling &sampling, unsigned anisotropy) override;
	void forget_sampler(HostSampler sampler) override { glDeleteSamplers(1, &sampler.id); }

private:
	unsigned width() const { return SCREEN_WIDTH * m_scale; }
	unsigned height() const { return SCREEN_HEIGHT * m_scale; }
	void build_pictures();
	void destroy_pictures(bool kept_too);
	GLuint &kept(unsigned i) { return (i < 2) ? m_buffers[i] : m_pipe[i - 2]; }
	void copy_picture(GLuint from, unsigned from_w, unsigned from_h, GLuint to, unsigned to_w, unsigned to_h);
	void build_blender();
	void build_quad();
	void build_compose();
	void build_presenter();
	void set_2d_state();

	unsigned m_scale = 1;
	/* Where a list is drawn: a colour buffer and a depth buffer */
	GLuint m_scene_fb, m_scene_color, m_scene_depth;
	/* The two 3D buffers of the hardware: the picture last blended into
	 * each */
	GLuint m_buffers[2];
	/* The pictures of the last three lists */
	GLuint m_pipe[3], m_pipe_fb[3];
	/* For copy_picture, and a picture of the size of the screen of the
	 * hardware for the save states */
	GLuint m_copy_fb[2];
	GLuint m_state_picture;

	/* Blends the scene and a 3D buffer, see blend_scene_to_pipe */
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
	/* The texture of each layer, kept from frame to frame, and what it
	 * holds (none before the first) */
	GLuint m_layer_tex[2] = { 0, 0 };
	std::optional<LayerTexels> m_layer_texels[2];

	/* The filter of the picture, its preset, the frames it has drawn */
	std::unique_ptr<PictureFilter> m_filter;
	std::string m_filter_preset;
	unsigned m_filter_frames = 0;
};

} /* namespace odin */

#endif /* ODIN_BOARD_DRAWING_GL_HPP */
