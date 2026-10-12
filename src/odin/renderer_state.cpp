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

#include "odin/renderer.hpp"
#include "odin/renderer_gl.hpp"

namespace odin {

void
Renderer::save(std::span<uint8_t> pictures, Counters &counters)
{
	if (pictures.size() != PICTURES * PICTURE_SIZE)
		fatal("a save state of another size");

	/* The pictures are whole once the last list is counted. */
	check_last_list();

	glPixelStorei(GL_PACK_ALIGNMENT, 1);
	for (unsigned i = 0; i < PICTURES; i++) {
		glBindTexture(GL_TEXTURE_2D, picture(i));
		glGetTexImage(GL_TEXTURE_2D, 0, GL_RGBA, GL_UNSIGNED_BYTE,
		              &pictures[i * PICTURE_SIZE]);
	}
	glBindTexture(GL_TEXTURE_2D, 0);
	gl::check("saving the pictures");

	counters.num_lists = m_num_lists;
	counters.shown = m_shown;
	counters.next = m_next;
	counters.launched = m_launched;
}

void
Renderer::load(std::span<const uint8_t> pictures, const Counters &counters)
{
	if (pictures.size() != PICTURES * PICTURE_SIZE)
		fatal("a save state of another size");

	glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
	for (unsigned i = 0; i < PICTURES; i++) {
		glBindTexture(GL_TEXTURE_2D, picture(i));
		glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, WIDTH, HEIGHT,
		                GL_RGBA, GL_UNSIGNED_BYTE, &pictures[i * PICTURE_SIZE]);
	}
	glBindTexture(GL_TEXTURE_2D, 0);
	gl::check("loading the pictures");

	m_num_lists = counters.num_lists;
	m_shown = counters.shown;
	m_next = counters.next;
	m_launched = counters.launched;

	/* The pictures of the state are whole. */
	m_last.list = nullptr;

	m_textures.invalidate(-1);
}

} /* namespace odin */
