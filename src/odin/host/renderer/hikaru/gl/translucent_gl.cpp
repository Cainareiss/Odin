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

/* The translucent fragments of a view with OpenGL (drawing.hpp; the
 * rules they follow: renderer_translucent.cpp). A fragment goes to the list
 * of its pixel, in a storage buffer, then every pixel blends its list
 * (glsl/sort.frag). Without storage buffers (OpenGL 4.1) they are peeled
 * one layer at a time instead. */

#include "odin/host/renderer/hikaru/gl/drawing_gl.hpp"
#include "odin/host/gfx/gl_common.hpp"
#include "odin/host/gfx/glsl_texts.hpp"

#include <algorithm>
#include <cstdint>

namespace odin {

namespace {

/* Bytes of a node_t of the shaders */
constexpr GLsizeiptr NODE_SIZE = 32;
/* The texture unit of the colour below the fragments */
constexpr GLenum UNIT_BELOW = 5;
constexpr GLenum UNIT_PEEL_COLOR = 10, UNIT_PEEL_LAYER = 11, UNIT_PEEL_INFO = 12;
constexpr unsigned PEEL_BATCH = 8;

GLuint
pair_framebuffer(GLuint first, GLuint second)
{
	static const GLenum targets[2] = { GL_COLOR_ATTACHMENT0, GL_COLOR_ATTACHMENT1 };
	GLuint id;

	glGenFramebuffers(1, &id);
	glBindFramebuffer(GL_FRAMEBUFFER, id);
	glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, first, 0);
	glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT1, GL_TEXTURE_2D, second, 0);
	glDrawBuffers(2, targets);
	if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
		gl::fatal("a framebuffer is not complete");
	return id;
}

void
bind_unit(GLenum unit, GLuint texture)
{
	glActiveTexture(GL_TEXTURE0 + unit);
	glBindTexture(GL_TEXTURE_2D, texture);
	glActiveTexture(GL_TEXTURE0);
}

} /* namespace */

/* The pictures of the lists, at the size of the buffers: the last fragment
 * of each pixel, the colour under them, and the depth the other types left */
void
GlBoardDrawing::build_list_pictures()
{
	const unsigned w = width(), h = height();

	m_solid_depth = gl::new_texture(GL_DEPTH_COMPONENT24, w, h);
	m_lists.below = gl::new_texture(GL_RGBA8, w, h);
	if (m_peel.on) {
		m_peel.depth = gl::new_texture(GL_DEPTH_COMPONENT32F, w, h);
		m_peel.layer = gl::new_texture(GL_RGBA32F, w, h);
		m_peel.info = gl::new_texture(GL_RGBA32F, w, h);
		m_peel.layer_fb = pair_framebuffer(m_peel.layer, m_peel.info);
		glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, m_peel.depth, 0);
		if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
			gl::fatal("a framebuffer is not complete");
		for (unsigned i = 0; i < 2; i++) {
			m_peel.color[i] = gl::new_texture(GL_RGBA32F, w, h);
			m_peel.last[i] = gl::new_texture(GL_RGBA32F, w, h);
			m_peel.color_fb[i] = pair_framebuffer(m_peel.color[i], m_peel.last[i]);
		}
	} else {
		m_lists.heads = gl::new_texture(GL_R32UI, w, h);
		m_lists.heads_fb = gl::new_framebuffer(m_lists.heads);
	}

	glBindFramebuffer(GL_FRAMEBUFFER, 0);
	glBindTexture(GL_TEXTURE_2D, 0);
	gl::check("the pictures of the lists");
}

void
GlBoardDrawing::destroy_list_pictures()
{
	glBindFramebuffer(GL_FRAMEBUFFER, 0);
	glDeleteTextures(1, &m_solid_depth);
	glDeleteTextures(1, &m_lists.below);
	if (m_peel.on) {
		const GLuint fbs[] = { m_peel.layer_fb, m_peel.color_fb[0], m_peel.color_fb[1] };
		const GLuint textures[] = { m_peel.depth, m_peel.layer, m_peel.info, m_peel.color[0], m_peel.color[1],
		                            m_peel.last[0], m_peel.last[1] };

		glDeleteFramebuffers(3, fbs);
		glDeleteTextures(7, textures);
	} else {
		glDeleteFramebuffers(1, &m_lists.heads_fb);
		glDeleteTextures(1, &m_lists.heads);
	}
}

void
GlBoardDrawing::build_lists()
{
	GLint64 limit = 0;

	if (m_peel.on) {
		m_peel.blend = gl::compile_program(odin_glsl_whole_picture_vert, odin_glsl_peel_blend_frag);
		glUseProgram(m_peel.blend);
		glUniform1i(gl::uniform(m_peel.blend, "u_below"), UNIT_BELOW);
		glUniform1i(gl::uniform(m_peel.blend, "u_color"), UNIT_PEEL_COLOR);
		glUniform1i(gl::uniform(m_peel.blend, "u_last"), MeshPrograms::UNIT_PEEL_LAST);
		glUniform1i(gl::uniform(m_peel.blend, "u_layer"), UNIT_PEEL_LAYER);
		glUniform1i(gl::uniform(m_peel.blend, "u_info"), UNIT_PEEL_INFO);
		m_peel.show = gl::compile_program(odin_glsl_whole_picture_vert, odin_glsl_peel_show_frag);
		glUseProgram(m_peel.show);
		glUniform1i(gl::uniform(m_peel.show, "u_color"), UNIT_PEEL_COLOR);
		glUniform1i(gl::uniform(m_peel.show, "u_last"), MeshPrograms::UNIT_PEEL_LAST);
		glUseProgram(0);
		glGenQueries(PEEL_BATCH, m_peel.any);
		m_lists.limit = UINT32_MAX;
		return;
	}

	m_lists.program = gl::compile_program(odin_glsl_whole_picture_vert, odin_glsl_sort_frag);
	glUseProgram(m_lists.program);
	glUniform1i(gl::uniform(m_lists.program, "u_below"), UNIT_BELOW);
	glUseProgram(0);

	glGenBuffers(8, m_lists.count);
	for (const GLuint count : m_lists.count) {
		glBindBuffer(GL_SHADER_STORAGE_BUFFER, count);
		glBufferData(GL_SHADER_STORAGE_BUFFER, sizeof(GLuint), nullptr, GL_DYNAMIC_COPY);
	}
	glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);

	/* The counts of a list are read once the next one is sent (see show):
	 * the next one starts its own from zero. */
	const GLsizeiptr readback = 3 * sizeof(m_lists.count);

	glGenBuffers(1, &m_lists.readback);
	glBindBuffer(GL_COPY_WRITE_BUFFER, m_lists.readback);
	m_lists.counts = nullptr;
	if (GLEW_ARB_buffer_storage) {
		const GLbitfield flags = GL_MAP_READ_BIT | GL_MAP_PERSISTENT_BIT | GL_MAP_COHERENT_BIT;

		glBufferStorage(GL_COPY_WRITE_BUFFER, readback, nullptr, flags);
		m_lists.counts = static_cast<const GLuint *>(glMapBufferRange(GL_COPY_WRITE_BUFFER, 0, readback, flags));
	} else {
		glBufferData(GL_COPY_WRITE_BUFFER, readback, nullptr, GL_STREAM_READ);
	}
	glBindBuffer(GL_COPY_WRITE_BUFFER, 0);

	glGetInteger64v(GL_MAX_SHADER_STORAGE_BLOCK_SIZE, &limit);
	m_lists.limit = static_cast<uint32_t>(std::min<GLint64>(limit / NODE_SIZE, UINT32_MAX));

	/* Sized by size_fragments */
	glGenBuffers(1, &m_lists.nodes);
}

void
GlBoardDrawing::destroy_lists()
{
	if (m_peel.on) {
		glDeleteQueries(PEEL_BATCH, m_peel.any);
		glDeleteProgram(m_peel.blend);
		glDeleteProgram(m_peel.show);
		return;
	}
	for (unsigned slot = 0; slot < 3; slot++)
		forget_counts(slot);
	glDeleteBuffers(1, &m_lists.readback);
	glDeleteBuffers(1, &m_lists.nodes);
	glDeleteBuffers(8, m_lists.count);
	glDeleteProgram(m_lists.program);
}

void
GlBoardDrawing::size_fragments(uint32_t max)
{
	if (m_peel.on)
		return;
	glBindBuffer(GL_SHADER_STORAGE_BUFFER, m_lists.nodes);
	glBufferData(GL_SHADER_STORAGE_BUFFER, static_cast<GLsizeiptr>(max) * NODE_SIZE,
	             nullptr, GL_DYNAMIC_COPY);
	glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);
	gl::check("sizing the fragment lists");
}

void
GlBoardDrawing::begin_translucent()
{
	/* The depth the other types left */
	glBindFramebuffer(GL_READ_FRAMEBUFFER, m_scene_fb);
	glActiveTexture(GL_TEXTURE0 + MeshPrograms::UNIT_SOLID_DEPTH);
	glBindTexture(GL_TEXTURE_2D, m_solid_depth);
	glCopyTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 0, 0,
	                    static_cast<GLsizei>(width()), static_cast<GLsizei>(height()));
	glActiveTexture(GL_TEXTURE0);

	if (!m_peel.on)
		glBindImageTexture(0, m_lists.heads, 0, GL_FALSE, 0, GL_READ_WRITE, GL_R32UI);
	glDisable(GL_DEPTH_TEST);
}

/* Each viewport has its count: writing a part of a buffer the card is
 * using waits for the card as well. The Intel driver 21.20.16.5174 wants a
 * buffer that has grown bound again, and does not always reset an atomic
 * counter buffer: the count is in a storage buffer. */
void
GlBoardDrawing::begin_collect(unsigned view)
{
	static const GLuint zero[4] = { 0, 0, 0, 0 };

	if (m_peel.on) {
		m_peel.draws.clear();
		m_pass.order = 0;
		glBindVertexArray(m_mesh_vao);
		return;
	}
	glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, m_lists.nodes);
	glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 1, m_lists.count[view]);
	glBindFramebuffer(GL_FRAMEBUFFER, m_lists.heads_fb);
	glDisable(GL_SCISSOR_TEST);
	glClearBufferuiv(GL_COLOR, 0, zero);
	glBufferSubData(GL_SHADER_STORAGE_BUFFER, 0, sizeof(GLuint), zero);
	glBindFramebuffer(GL_FRAMEBUFFER, m_scene_fb);
	glEnable(GL_SCISSOR_TEST);
	glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);

	m_pass.order = 0;
	glBindVertexArray(m_mesh_vao);
}

void
GlBoardDrawing::end_collect()
{
	if (m_peel.on)
		return;
	glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
	glMemoryBarrier(GL_ALL_BARRIER_BITS);
}

/* Nobody waits for the card here: the count goes where it waits for
 * counted_fragments. */
void
GlBoardDrawing::keep_count(unsigned view, unsigned slot)
{
	if (m_peel.on)
		return;
	glBindBuffer(GL_COPY_READ_BUFFER, m_lists.count[view]);
	glBindBuffer(GL_COPY_WRITE_BUFFER, m_lists.readback);
	glCopyBufferSubData(GL_COPY_READ_BUFFER, GL_COPY_WRITE_BUFFER, 0,
	                    static_cast<GLintptr>((slot * 8 + view) * sizeof(GLuint)),
	                    sizeof(GLuint));
	glBindBuffer(GL_COPY_READ_BUFFER, 0);
	glBindBuffer(GL_COPY_WRITE_BUFFER, 0);
	if (m_lists.counts) {
		forget_counts(slot);
		m_counted[slot] = glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE, 0);
	}
}

uint32_t
GlBoardDrawing::count_now(unsigned view)
{
	GLuint used;

	if (m_peel.on)
		return m_peel.draws.empty() ? 0 : 1;
	glBindBuffer(GL_SHADER_STORAGE_BUFFER, m_lists.count[view]);
	glGetBufferSubData(GL_SHADER_STORAGE_BUFFER, 0, sizeof(GLuint), &used);
	return used;
}

/* Blends them over the colour below */
void
GlBoardDrawing::end_translucent(bool blend)
{
	if (m_peel.on) {
		if (blend && !m_peel.draws.empty())
			peel();
		m_peel.draws.clear();
	} else if (blend) {
		glBindFramebuffer(GL_READ_FRAMEBUFFER, m_scene_fb);
		glActiveTexture(GL_TEXTURE0 + UNIT_BELOW);
		glBindTexture(GL_TEXTURE_2D, m_lists.below);
		glCopyTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 0, 0,
		                    static_cast<GLsizei>(width()), static_cast<GLsizei>(height()));

		glUseProgram(m_lists.program);
		m_program = nullptr;
		glDisable(GL_BLEND);
		glDisable(GL_SCISSOR_TEST);
		glDisable(GL_CULL_FACE);
		glBindVertexArray(m_quad_vao);
		glDrawArrays(GL_TRIANGLES, 0, 3);
		glBindVertexArray(0);
		glEnable(GL_CULL_FACE);
		glEnable(GL_SCISSOR_TEST);
		glEnable(GL_BLEND);

		glBindTexture(GL_TEXTURE_2D, 0);
		glActiveTexture(GL_TEXTURE0);
	}

	glActiveTexture(GL_TEXTURE0 + MeshPrograms::UNIT_SOLID_DEPTH);
	glBindTexture(GL_TEXTURE_2D, 0);
	glActiveTexture(GL_TEXTURE0);
	glEnable(GL_DEPTH_TEST);
	gl::check("the translucent polygons");
}

uint32_t
GlBoardDrawing::counted_fragments(unsigned slot, uint32_t views)
{
	GLuint used = 0;

	if (m_peel.on)
		return 0;
	if (m_counted[slot]) {
		GLenum waited;

		do
			waited = glClientWaitSync(m_counted[slot], GL_SYNC_FLUSH_COMMANDS_BIT, 1000000000);
		while (waited == GL_TIMEOUT_EXPIRED);
		if (waited == GL_WAIT_FAILED)
			gl::fatal("waiting for the count of the translucent fragments");
		forget_counts(slot);
	}
	for (unsigned vpi = 0; vpi < 8; vpi++) {
		const uint32_t at = slot * 8 + vpi;
		GLuint count;

		if (!(views & (1u << vpi)))
			continue;
		if (m_lists.counts) {
			count = m_lists.counts[at];
		} else {
			glBindBuffer(GL_COPY_READ_BUFFER, m_lists.readback);
			glGetBufferSubData(GL_COPY_READ_BUFFER, static_cast<GLintptr>(at * sizeof(GLuint)),
			                   sizeof(GLuint), &count);
			glBindBuffer(GL_COPY_READ_BUFFER, 0);
		}
		used = std::max(used, count);
	}
	return used;
}

void
GlBoardDrawing::forget_counts(unsigned slot)
{
	if (m_counted[slot])
		glDeleteSync(m_counted[slot]);
	m_counted[slot] = nullptr;
}

void
GlBoardDrawing::draw_peel_layer()
{
	m_peel.drawing = true;
	m_pass.order = 0;
	m_program = nullptr;
	glBindVertexArray(m_mesh_vao);
	glEnable(GL_SCISSOR_TEST);
	for (const PeelDraw &d : m_peel.draws)
		draw_mesh(*d.mesh, d.draw);
	m_peel.drawing = false;
}

/* GL_GREATER keeps the farthest fragment, the first sent among equals */
void
GlBoardDrawing::peel()
{
	static const GLfloat before_first[4] = { 2.0f, 0.0f, 0.0f, 0.0f }, none[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
	static const GLfloat farthest = 0.0f;
	const GLsizei w = static_cast<GLsizei>(width()), h = static_cast<GLsizei>(height());
	GLboolean depth_mask;
	unsigned now = 0, layers = 0;

	glGetBooleanv(GL_DEPTH_WRITEMASK, &depth_mask);
	glBindFramebuffer(GL_READ_FRAMEBUFFER, m_scene_fb);
	bind_unit(UNIT_BELOW, m_lists.below);
	glActiveTexture(GL_TEXTURE0 + UNIT_BELOW);
	glCopyTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 0, 0, w, h);
	glActiveTexture(GL_TEXTURE0);

	glDisable(GL_BLEND);
	glDepthMask(GL_TRUE);
	glDisable(GL_SCISSOR_TEST);
	glBindFramebuffer(GL_FRAMEBUFFER, m_peel.color_fb[now]);
	glClearBufferfv(GL_COLOR, 1, before_first);
	bind_unit(UNIT_PEEL_LAYER, m_peel.layer);
	bind_unit(UNIT_PEEL_INFO, m_peel.info);

	for (unsigned found = PEEL_BATCH; found == PEEL_BATCH;) {
		const unsigned start = now;

		for (unsigned j = 0; j < PEEL_BATCH; j++) {
			if (j > 0)
				glBeginConditionalRender(m_peel.any[j - 1], GL_QUERY_WAIT);
			bind_unit(MeshPrograms::UNIT_PEEL_LAST, m_peel.last[now]);
			glBindFramebuffer(GL_FRAMEBUFFER, m_peel.layer_fb);
			glDisable(GL_SCISSOR_TEST);
			glClearBufferfv(GL_DEPTH, 0, &farthest);
			glClearBufferfv(GL_COLOR, 1, none);
			glEnable(GL_DEPTH_TEST);
			glDepthFunc(GL_GREATER);
			glBeginQuery(GL_ANY_SAMPLES_PASSED, m_peel.any[j]);
			draw_peel_layer();
			glEndQuery(GL_ANY_SAMPLES_PASSED);
			if (j > 0)
				glEndConditionalRender();

			glBeginConditionalRender(m_peel.any[j], GL_QUERY_WAIT);
			bind_unit(UNIT_PEEL_COLOR, m_peel.color[now]);
			glBindFramebuffer(GL_FRAMEBUFFER, m_peel.color_fb[now ^ 1]);
			glDisable(GL_DEPTH_TEST);
			glDisable(GL_SCISSOR_TEST);
			glDisable(GL_CULL_FACE);
			glUseProgram(m_peel.blend);
			glBindVertexArray(m_quad_vao);
			glDrawArrays(GL_TRIANGLES, 0, 3);
			glEnable(GL_CULL_FACE);
			bind_unit(UNIT_PEEL_COLOR, 0);
			glEndConditionalRender();
			now ^= 1;
		}

		for (found = 0; found < PEEL_BATCH; found++) {
			GLuint any = 0;

			glGetQueryObjectuiv(m_peel.any[found], GL_QUERY_RESULT, &any);
			if (!any)
				break;
		}
		layers += found;
		now = start ^ (found & 1);
	}

	glDisable(GL_DEPTH_TEST);
	glDepthFunc(GL_LEQUAL);
	glDepthMask(depth_mask);
	bind_unit(UNIT_PEEL_LAYER, 0);
	bind_unit(UNIT_PEEL_INFO, 0);
	glBindFramebuffer(GL_FRAMEBUFFER, m_scene_fb);
	glDisable(GL_SCISSOR_TEST);
	if (layers) {
		bind_unit(MeshPrograms::UNIT_PEEL_LAST, m_peel.last[now]);
		bind_unit(UNIT_PEEL_COLOR, m_peel.color[now]);
		glUseProgram(m_peel.show);
		glBindVertexArray(m_quad_vao);
		glDisable(GL_CULL_FACE);
		glDrawArrays(GL_TRIANGLES, 0, 3);
		glEnable(GL_CULL_FACE);
		bind_unit(UNIT_PEEL_COLOR, 0);
	}
	bind_unit(MeshPrograms::UNIT_PEEL_LAST, 0);
	bind_unit(UNIT_BELOW, 0);
	glUseProgram(0);
	m_program = nullptr;
	glBindVertexArray(m_mesh_vao);
	glEnable(GL_SCISSOR_TEST);
	glEnable(GL_BLEND);
}

} /* namespace odin */
