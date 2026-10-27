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

/* The translucent fragments of a view with OpenGL (board_drawing.hpp; the
 * rules they follow: renderer_translucent.cpp). A fragment goes to the list
 * of its pixel, in a storage buffer, then every pixel blends its list
 * (glsl/sort.frag). */

#include "odin/host/renderer/hikaru/gl/board_drawing_gl.hpp"
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

} /* namespace */

/* The pictures of the lists, at the size of the buffers: the last fragment
 * of each pixel, the colour under them, and the depth the other types left */
void
GlBoardDrawing::build_list_pictures()
{
	const unsigned w = width(), h = height();

	m_solid_depth = gl::new_texture(GL_DEPTH_COMPONENT24, w, h);
	m_lists.heads = gl::new_texture(GL_R32UI, w, h);
	m_lists.heads_fb = gl::new_framebuffer(m_lists.heads);
	m_lists.below = gl::new_texture(GL_RGBA8, w, h);

	glBindFramebuffer(GL_FRAMEBUFFER, 0);
	glBindTexture(GL_TEXTURE_2D, 0);
	gl::check("the pictures of the lists");
}

void
GlBoardDrawing::destroy_list_pictures()
{
	glBindFramebuffer(GL_FRAMEBUFFER, 0);
	glDeleteTextures(1, &m_solid_depth);
	glDeleteFramebuffers(1, &m_lists.heads_fb);
	glDeleteTextures(1, &m_lists.heads);
	glDeleteTextures(1, &m_lists.below);
}

void
GlBoardDrawing::build_lists()
{
	GLint64 limit = 0;

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
	glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
	glMemoryBarrier(GL_ALL_BARRIER_BITS);
}

/* Nobody waits for the card here: the count goes where it waits for
 * counted_fragments. */
void
GlBoardDrawing::keep_count(unsigned view, unsigned slot)
{
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

	glBindBuffer(GL_SHADER_STORAGE_BUFFER, m_lists.count[view]);
	glGetBufferSubData(GL_SHADER_STORAGE_BUFFER, 0, sizeof(GLuint), &used);
	return used;
}

/* Blends them over the colour below */
void
GlBoardDrawing::end_translucent(bool blend)
{
	if (blend) {
		glBindFramebuffer(GL_READ_FRAMEBUFFER, m_scene_fb);
		glActiveTexture(GL_TEXTURE0 + UNIT_BELOW);
		glBindTexture(GL_TEXTURE_2D, m_lists.below);
		glCopyTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 0, 0,
		                    static_cast<GLsizei>(width()), static_cast<GLsizei>(height()));

		glUseProgram(m_lists.program);
		m_program = nullptr;
		glDisable(GL_BLEND);
		glDisable(GL_SCISSOR_TEST);
		glBindVertexArray(m_quad_vao);
		glDrawArrays(GL_TRIANGLES, 0, 3);
		glBindVertexArray(0);
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

} /* namespace odin */
