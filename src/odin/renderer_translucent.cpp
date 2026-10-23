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

/* The translucent polygons of a viewport are blended far to near at every
 * pixel, whatever the triangle or the mesh they come from. On the PHARRIER
 * cabinet the faces of a SEGA letter, alpha 0.8 and drawn on both sides,
 * show the faces behind them and none over them, from any angle; the glass
 * of an AIRTRIX street lamp glows over its frame; a wall of fire of BRAVEFF
 * keeps its texture. The patents of the hardware peel them the deepest
 * first whatever the order they were sent in, with a depth register of
 * their own (US 6 320 580, US 6 680 741); how they break a tie is not
 * written (HEURISTIC: in the order sent). Whole triangles sorted on their
 * centroid break those letters, and writing the depth drops layers that
 * the cabinet shows. The X360 port reverses its list of objects instead
 * (825B3EB0). Neither writes the depth here.
 *
 * The meshes are drawn once. A fragment that is not behind the other types
 * goes to the list of its pixel with its colour, its depth, the blend mode
 * of its material and its rank in the display list; then every pixel
 * blends its list (shaders::SORT_FRAGMENT). The 101 of a mesh moves it one
 * way against the other types and another in that list (depth_bias.hpp). */

#include "odin/renderer.hpp"
#include "odin/renderer_gl.hpp"
#include "odin/renderer_shaders.hpp"

#include <algorithm>
#include <cstdint>
#include <cstdio>

namespace odin {

namespace {

/* Bytes of a node_t of the shaders */
constexpr GLsizeiptr NODE_SIZE = 32;
/* The texture units of the colour below the fragments and of the depth of
 * the other types */
constexpr GLenum UNIT_BELOW = 5;

} /* namespace */

void
Renderer::build_lists()
{
	GLint64 limit = 0;

	m_lists.program = gl::compile_program(shaders::WHOLE_PICTURE_VERTEX, shaders::SORT_FRAGMENT);
	glUseProgram(m_lists.program);
	glUniform1i(gl::uniform(m_lists.program, "u_below"), UNIT_BELOW);
	glUseProgram(0);

	glGenBuffers(8, m_lists.count);
	for (const GLuint count : m_lists.count) {
		glBindBuffer(GL_SHADER_STORAGE_BUFFER, count);
		glBufferData(GL_SHADER_STORAGE_BUFFER, sizeof(GLuint), nullptr, GL_DYNAMIC_COPY);
	}
	glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);

	glGetInteger64v(GL_MAX_SHADER_STORAGE_BLOCK_SIZE, &limit);
	m_lists.limit = static_cast<uint32_t>(std::min<GLint64>(limit / NODE_SIZE, UINT32_MAX));
	m_lists.full = false;

	glGenBuffers(1, &m_lists.nodes);
	size_lists(room_at_start());
}

void
Renderer::destroy_lists()
{
	glDeleteBuffers(1, &m_lists.nodes);
	glDeleteBuffers(8, m_lists.count);
	glDeleteProgram(m_lists.program);
}

/* A fragment a pixel of the buffers, and four a pixel of the screen of the
 * hardware at least: the six games fit in that at the scale 1, but for one
 * list of VON4. */
uint32_t
Renderer::room_at_start() const
{
	return std::max(4 * WIDTH * HEIGHT, width() * height());
}

/* Room for `max` fragments. What the buffer held is lost. */
void
Renderer::size_lists(uint32_t max)
{
	m_lists.max = max;
	glBindBuffer(GL_SHADER_STORAGE_BUFFER, m_lists.nodes);
	glBufferData(GL_SHADER_STORAGE_BUFFER, static_cast<GLsizeiptr>(max) * NODE_SIZE,
	             nullptr, GL_DYNAMIC_COPY);
	glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);
	gl::check("sizing the fragment lists");
}

/* More room, for `used` fragments at least. False if the graphics card has
 * no more to give: the fragments that do not fit are lost. */
bool
Renderer::grow_lists(uint32_t used)
{
	const uint32_t twice = (m_lists.max > m_lists.limit / 2) ? m_lists.limit : m_lists.max * 2;
	const uint32_t wanted = std::min(std::max(used, twice), m_lists.limit);

	if (wanted <= m_lists.max) {
		if (!m_lists.full)
			error("no room for %u translucent fragments, the card takes %u",
			      used, m_lists.limit);
		m_lists.full = true;
		return false;
	}
	std::printf("renderer: more room for %u translucent fragments\n", used);
	size_lists(wanted);
	return true;
}

void
Renderer::draw_translucent(unsigned vpi, const odin_mesh_t *meshes, unsigned num)
{
	static const GLuint zero[4] = { 0, 0, 0, 0 };
	GLuint used = 1;	/* not known yet */

	/* The depth the other types left. */
	glActiveTexture(GL_TEXTURE0 + MeshPrograms::UNIT_SOLID_DEPTH);
	glBindTexture(GL_TEXTURE_2D, m_solid_depth);
	glCopyTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 0, 0,
	                    static_cast<GLsizei>(width()), static_cast<GLsizei>(height()));
	glActiveTexture(GL_TEXTURE0);

	glBindImageTexture(0, m_lists.heads, 0, GL_FALSE, 0, GL_READ_WRITE, GL_R32UI);
	glDisable(GL_DEPTH_TEST);

	/* Collect the fragments. Whether their buffer was large enough is
	 * known once the graphics card has drawn them: nobody waits for it
	 * here, the count is left for check_last_list. A list drawn again
	 * does wait, and collects once more if the buffer is too small.
	 * Each viewport has its count: writing a part of a buffer the card
	 * is using waits for the card as well.
	 * The Intel driver 21.20.16.5174 wants a buffer that has grown bound
	 * again, and does not always reset an atomic counter buffer: the
	 * count is in a storage buffer. */
	m_pass.collect = true;
	for (;;) {
		glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, m_lists.nodes);
		glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 1, m_lists.count[vpi]);
		glBindFramebuffer(GL_FRAMEBUFFER, m_lists.heads_fb);
		glDisable(GL_SCISSOR_TEST);
		glClearBufferuiv(GL_COLOR, 0, zero);
		glBufferSubData(GL_SHADER_STORAGE_BUFFER, 0, sizeof(GLuint), zero);
		glBindFramebuffer(GL_FRAMEBUFFER, m_scene_fb);
		glEnable(GL_SCISSOR_TEST);
		glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);

		m_pass.order = 0;
		for (unsigned j = 0; j < num; j++) {
			set_material_blend(meshes[j]);
			draw_mesh(meshes[j]);
		}

		glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
		glMemoryBarrier(GL_ALL_BARRIER_BITS);
		if (!m_again) {
			m_last.views |= 1u << vpi;
			break;
		}
		glGetBufferSubData(GL_SHADER_STORAGE_BUFFER, 0, sizeof(GLuint), &used);
		if (used <= m_lists.max || !grow_lists(used))
			break;
	}
	m_pass.collect = false;

	/* Blend them over the colour below. */
	m_times.part(DrawTimes::PART_SORT);
	if (used) {
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

} /* namespace odin */
