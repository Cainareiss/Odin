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

#include <cstring>

namespace odin {

namespace {

/* A line of the framebuffer RAM is 4096 bytes. */
constexpr size_t FB_PITCH = 4096;

} /* namespace */

/* A 2D layer as a new texture, bound on the unit 0. `mult` is what its
 * colours are multiplied by on the screen. */
GLuint
Renderer::upload_layer(const odin_layer_t &layer, float &mult)
{
	const size_t offs = size_t{layer.y0} * FB_PITCH + size_t{layer.x0} * 4;
	GLuint id;

	if (logging())
		log("uploading LAYER ena=%u keyed=%u above=%u (%u,%u) (%u,%u) fmt=%u",
		    layer.enabled, layer.keyed, layer.above,
		    layer.x0, layer.y0, layer.x1, layer.y1, layer.format);

	if (offs + (HEIGHT - 1) * FB_PITCH + size_t{WIDTH} * 4 > m_fb.size())
		fatal("a layer outside the framebuffer RAM");
	const uint8_t *const data = &m_fb[offs];

	glGenTextures(1, &id);
	glActiveTexture(GL_TEXTURE0 + 0);
	glBindTexture(GL_TEXTURE_2D, id);

	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, 0);

	glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
	switch (layer.format) {
	case ODIN_FORMAT_ABGR1555:
		/* The two 16-bit pixels of each 32-bit word are stored
		 * swapped, as the texture transfer does for the texture RAM
		 * (MEASURED on the von4 title text, GPU-RE/16 § 9). */
		for (unsigned y = 0; y < HEIGHT; y++) {
			const uint8_t *row = data + y * FB_PITCH;
			uint16_t *dst = &m_layer_pixels[size_t{y} * WIDTH];

			for (unsigned x = 0; x < WIDTH / 2; x++) {
				uint32_t word;

				std::memcpy(&word, row + x * 4, 4);
				dst[x * 2] = static_cast<uint16_t>(word >> 16);
				dst[x * 2 + 1] = static_cast<uint16_t>(word);
			}
		}

		glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
		glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB5_A1, WIDTH, HEIGHT, 0,
		             GL_RGBA, GL_UNSIGNED_SHORT_1_5_5_5_REV, m_layer_pixels.data());
		mult = 1.0f;
		break;
	case ODIN_FORMAT_A2BGR10:
		/* Read where it lies. Its colours are shown four times as
		 * bright (as Valkyrie had it; HEURISTIC). */
		glPixelStorei(GL_UNPACK_ROW_LENGTH, FB_PITCH / 4);
		glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB10_A2, WIDTH, HEIGHT, 0,
		             GL_RGBA, GL_UNSIGNED_INT_2_10_10_10_REV, data);
		mult = 4.0f;
		break;
	default:
		fatal("a layer of a format that is not known");
	}
	glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
	gl::check("a layer");

	return id;
}

} /* namespace odin */
