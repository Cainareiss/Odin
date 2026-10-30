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

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstring>
#include <string>
#include <unordered_map>

#include "odin/host/gfx/glsl_texts.hpp"
#include "odin/host/renderer/hikaru/gl/drawing_gl.hpp"

/* Draws Neon 250 scenes to a target, then to the frame's picture */

namespace odin {

struct GlDrawing::Pmx {
	/* The second is for Native 2D (pmx.geom) */
	struct Program {
		GLuint id = 0;
		GLint most_w = 0, lod_bias = 0, flags = 0, scale = -1;

		void take(GLuint program, bool native)
		{
			id = program;
			most_w = gl::uniform(id, "u_most_w");
			lod_bias = gl::uniform(id, "u_lod_bias");
			flags = gl::uniform(id, "u_flags");
			if (native)
				scale = gl::uniform(id, "u_scale");
		}
	} programs[2];
	GLuint vao = 0, vbo = 0;
	/* Where scenes are drawn, if the depth can be a float from 0 to 1 as
	 * the chip's is: games lay polygons a hair apart at very small 1/W */
	GLuint fine_fb = 0, fine_depth = 0;
	unsigned fine_w = 0, fine_h = 0;
	std::unordered_map<uint32_t, GLuint> images;
	std::unordered_map<uint32_t, GLuint> samplers;

	~Pmx()
	{
		for (const auto &[id, texture] : images)
			glDeleteTextures(1, &texture);
		for (const auto &[key, sampler] : samplers)
			glDeleteSamplers(1, &sampler);
		glDeleteFramebuffers(1, &fine_fb);
		glDeleteTextures(1, &fine_depth);
		glDeleteBuffers(1, &vbo);
		glDeleteVertexArrays(1, &vao);
		for (const Program &program : programs)
			glDeleteProgram(program.id);
	}

	GLuint sampler(const PmxDraw &draw, unsigned anisotropy)
	{
		const uint32_t key = static_cast<uint32_t>(draw.wrap_u) | static_cast<uint32_t>(draw.wrap_v) << 2 |
		                     uint32_t{ draw.smooth } << 4 | uint32_t{ draw.mipmap } << 5 | anisotropy << 6;
		const auto found = samplers.find(key);

		if (found != samplers.end())
			return found->second;

		const auto wrap = [](PmxWrap how) {
			return how == PmxWrap::CLAMP ? GL_CLAMP_TO_EDGE : how == PmxWrap::MIRROR ? GL_MIRRORED_REPEAT : GL_REPEAT;
		};
		GLuint id;

		glGenSamplers(1, &id);
		glSamplerParameteri(id, GL_TEXTURE_WRAP_S, wrap(draw.wrap_u));
		glSamplerParameteri(id, GL_TEXTURE_WRAP_T, wrap(draw.wrap_v));
		glSamplerParameteri(id, GL_TEXTURE_MAG_FILTER, draw.smooth ? GL_LINEAR : GL_NEAREST);
		glSamplerParameteri(id, GL_TEXTURE_MIN_FILTER,
		                    draw.mipmap ? (draw.smooth ? GL_LINEAR_MIPMAP_NEAREST : GL_NEAREST_MIPMAP_NEAREST)
		                                : (draw.smooth ? GL_LINEAR : GL_NEAREST));
		if (anisotropy > 1 && draw.mipmap && gl::anisotropic_filter())
			glSamplerParameterf(id, GL_TEXTURE_MAX_ANISOTROPY_EXT, static_cast<float>(anisotropy));
		samplers.emplace(key, id);
		return id;
	}
};

void
GlDrawing::draw_pmx(const PmxScene &scene, unsigned anisotropy)
{
	static constexpr std::array<GLenum, 8> COMPARE = { GL_NEVER, GL_LESS, GL_EQUAL, GL_LEQUAL,
		                                           GL_GEQUAL, GL_NOTEQUAL, GL_GEQUAL, GL_ALWAYS };
	static constexpr std::array<GLenum, 8> SRC = { GL_ZERO, GL_ONE, GL_DST_COLOR, GL_ONE_MINUS_DST_COLOR,
		                                       GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_DST_ALPHA, GL_ONE_MINUS_DST_ALPHA };
	static constexpr std::array<GLenum, 8> DST = { GL_ZERO, GL_ONE, GL_SRC_COLOR, GL_ONE_MINUS_SRC_COLOR,
		                                       GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_DST_ALPHA, GL_ONE_MINUS_DST_ALPHA };

#ifdef ODIN_GLES
	const bool fine_depth = false;
#else
	const bool fine_depth = GLEW_ARB_clip_control;
#endif

	if (!m_pmx) {
		const std::string vertex = std::string(fine_depth ? "#define DEPTH_FROM_0 1\n" : "") + odin_glsl_pmx_vert;

		m_pmx = std::make_shared<Pmx>();
		m_pmx->programs[0].take(gl::compile_program(vertex.c_str(), odin_glsl_pmx_frag), false);
		if (gl::geometry_shaders()) {
			const std::string fragment = std::string("#define NATIVE 1\n") + odin_glsl_pmx_frag;

			m_pmx->programs[1].take(gl::compile_program(vertex.c_str(), fragment.c_str(), false, odin_glsl_pmx_geom), true);
		}
		glGenVertexArrays(1, &m_pmx->vao);
		glGenBuffers(1, &m_pmx->vbo);
		glBindVertexArray(m_pmx->vao);
		glBindBuffer(GL_ARRAY_BUFFER, m_pmx->vbo);
		for (GLuint i = 0; i < 4; i++)
			glEnableVertexAttribArray(i);
		const auto at = [](size_t offset) { return reinterpret_cast<const void *>(offset); };
		const GLsizei size = sizeof(PmxVertex);

		glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, size, at(offsetof(PmxVertex, x)));
		glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, size, at(offsetof(PmxVertex, u)));
		glVertexAttribPointer(2, 4, GL_UNSIGNED_BYTE, GL_TRUE, size, at(offsetof(PmxVertex, color)));
		glVertexAttribPointer(3, 4, GL_UNSIGNED_BYTE, GL_TRUE, size, at(offsetof(PmxVertex, offset)));
		glBindVertexArray(0);
		glBindBuffer(GL_ARRAY_BUFFER, 0);
	}

	Pmx &pmx = *m_pmx;

	for (const uint32_t id : scene.gone_images)
		if (const auto found = pmx.images.find(id); found != pmx.images.end()) {
			glDeleteTextures(1, &found->second);
			pmx.images.erase(found);
		}
	glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
	for (const std::shared_ptr<const PmxImage> &image : scene.new_images) {
		GLuint texture;

		glGenTextures(1, &texture);
		glBindTexture(GL_TEXTURE_2D, texture);
		glTexStorage2D(GL_TEXTURE_2D, static_cast<GLsizei>(image->levels.size()), GL_RGBA8,
		               static_cast<GLsizei>(image->width), static_cast<GLsizei>(image->height));
		for (size_t level = 0; level < image->levels.size(); level++)
			glTexSubImage2D(GL_TEXTURE_2D, static_cast<GLint>(level), 0, 0,
			                std::max(static_cast<GLsizei>(image->width >> level), 1),
			                std::max(static_cast<GLsizei>(image->height >> level), 1), GL_RGBA, GL_UNSIGNED_BYTE,
			                image->levels[level].data());
		pmx.images[image->id] = texture;
	}
	glBindTexture(GL_TEXTURE_2D, 0);

	const GLsizei w = static_cast<GLsizei>(width()), h = static_cast<GLsizei>(height());

	glBindFramebuffer(GL_FRAMEBUFFER, m_scene_fb);
#ifndef ODIN_GLES
	if (fine_depth) {
		if (pmx.fine_w != width() || pmx.fine_h != height()) {
			glDeleteFramebuffers(1, &pmx.fine_fb);
			glDeleteTextures(1, &pmx.fine_depth);
			pmx.fine_depth = gl::new_texture(GL_DEPTH_COMPONENT32F, width(), height());
			pmx.fine_fb = gl::new_framebuffer(m_scene_color);
			glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, pmx.fine_depth, 0);
			if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
				gl::fatal("the framebuffer of the Neon 250 is not complete");
			pmx.fine_w = width();
			pmx.fine_h = height();
		}
		glBindFramebuffer(GL_FRAMEBUFFER, pmx.fine_fb);
		glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, m_scene_color, 0);
		glClipControl(GL_LOWER_LEFT, GL_ZERO_TO_ONE);
	}
#endif
	glViewport(0, 0, w, h);
	glDisable(GL_SCISSOR_TEST);
	glDisable(GL_CULL_FACE);
	glDisable(GL_BLEND);
	glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
	glDepthMask(GL_TRUE);
	glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
	glClearDepthf(0.0f);
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
	glClearDepthf(1.0f);
	/* Alpha reads back as all ones after the clear */
	glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_FALSE);
	glEnable(GL_DEPTH_TEST);

	const Pmx::Program *program = nullptr;

	glBindVertexArray(pmx.vao);
	glBindBuffer(GL_ARRAY_BUFFER, pmx.vbo);
	glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(scene.vertices.size() * sizeof(PmxVertex)),
	             scene.vertices.data(), GL_STREAM_DRAW);
	glActiveTexture(GL_TEXTURE0);

	for (const PmxDraw &draw : scene.draws) {
		const int flags = (draw.image ? 1 : 0) | (draw.use_alpha ? 2 : 0) | (draw.ignore_texture_alpha ? 4 : 0) |
		                  (draw.offset ? 8 : 0) | draw.shading << 4;

		glDepthFunc(COMPARE[draw.compare & 7]);
		glDepthMask(draw.write_z ? GL_TRUE : GL_FALSE);
		if (draw.blend) {
			glEnable(GL_BLEND);
			glBlendFunc(SRC[draw.src & 7], DST[draw.dst & 7]);
		} else {
			glDisable(GL_BLEND);
		}
		if (const Pmx::Program *const wanted = &pmx.programs[draw.native ? 1 : 0]; wanted != program) {
			program = wanted;
			glUseProgram(program->id);
			glUniform1f(program->most_w, scene.most_w);
			glUniform1f(program->scale, static_cast<float>(m_scale));
		}
		glUniform1i(program->flags, flags);
		glUniform1f(program->lod_bias, draw.lod_bias);
		if (draw.image) {
			const auto found = pmx.images.find(draw.image);

			glBindTexture(GL_TEXTURE_2D, found != pmx.images.end() ? found->second : 0);
			glBindSampler(0, pmx.sampler(draw, anisotropy));
		}
		glDrawArrays(GL_TRIANGLES, static_cast<GLint>(draw.first), static_cast<GLsizei>(draw.count));
	}
#ifndef ODIN_GLES
	if (fine_depth)
		glClipControl(GL_LOWER_LEFT, GL_NEGATIVE_ONE_TO_ONE);
#endif

	glBindSampler(0, 0);
	glBindTexture(GL_TEXTURE_2D, 0);
	glBindVertexArray(0);
	glBindBuffer(GL_ARRAY_BUFFER, 0);
	glUseProgram(0);
	glDisable(GL_BLEND);
	glDisable(GL_DEPTH_TEST);
	glDepthMask(GL_TRUE);
	glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);

	glBindFramebuffer(GL_READ_FRAMEBUFFER, m_scene_fb);
	glBindFramebuffer(GL_DRAW_FRAMEBUFFER, m_frame_fb);
	glBlitFramebuffer(0, 0, w, h, 0, 0, w, h, GL_COLOR_BUFFER_BIT, GL_NEAREST);
	glBindFramebuffer(GL_FRAMEBUFFER, 0);
	gl::check("a scene of the Neon 250");
}

} /* namespace odin */
