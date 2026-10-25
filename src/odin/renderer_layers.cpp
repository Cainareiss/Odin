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

/* A line of the framebuffer RAM is 4096 bytes, one of a layer taken from
 * it as many words as the screen is wide. */
constexpr size_t FB_PITCH = 4096;
constexpr size_t LAYER_PITCH = size_t{Renderer::WIDTH} * 4;

} /* namespace */

/* The picture of the frame is composed with that layer: it is shown, or
 * the blender takes it (its inputs 2 and 3, see compose). */
bool
Renderer::shows_layer(unsigned i) const
{
	const uint32_t input = 2 + i;

	return (m_frame.layers.layer[0][i].enabled ||
	        odin_fb_num_1(&m_frame.fb) == input || odin_fb_num_2(&m_frame.fb) == input) &&
	       !m_debug[i == 0 ? DEBUG_NO_LAYER1 : DEBUG_NO_LAYER2];
}

/* Keeps the framebuffer RAM of a layer as the frame leaves it. */
void
Renderer::take_layer(unsigned i)
{
	const odin_layer_t &layer = m_frame.layers.layer[0][i];
	const size_t offs = size_t{layer.y0} * FB_PITCH + size_t{layer.x0} * 4;

	if (offs + (HEIGHT - 1) * FB_PITCH + LAYER_PITCH > m_fb.size())
		fatal("a layer outside the framebuffer RAM");
	for (unsigned y = 0; y < HEIGHT; y++) {
		uint8_t *const kept = &m_layer_ram[i][y * LAYER_PITCH];
		const uint8_t *const line = &m_fb[offs + y * FB_PITCH];

		if (std::memcmp(kept, line, LAYER_PITCH) != 0) {
			std::memcpy(kept, line, LAYER_PITCH);
			m_layer_changed[i] = true;
		}
	}
}

/* A 2D layer as its texture, bound on the unit 0. The texture is kept and
 * sent again only when the RAM of the layer or its format changed. */
GLuint
Renderer::upload_layer(unsigned i)
{
	const odin_layer_t &layer = m_frame.layers.layer[0][i];
	const uint8_t *const data = m_layer_ram[i].data();
	const bool same_format = m_layer_format[i] == layer.format;

	if (logging())
		log("uploading LAYER ena=%u keyed=%u above=%u (%u,%u) (%u,%u) fmt=%u",
		    layer.enabled, layer.keyed, layer.above,
		    layer.x0, layer.y0, layer.x1, layer.y1, layer.format);

	if (layer.format != ODIN_FORMAT_ABGR1555 && layer.format != ODIN_FORMAT_A2BGR10)
		fatal("a layer of a format that is not known");

	glActiveTexture(GL_TEXTURE0 + 0);
	if (m_layer_tex[i] == 0) {
		glGenTextures(1, &m_layer_tex[i]);
		glBindTexture(GL_TEXTURE_2D, m_layer_tex[i]);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, 0);
	}
	glBindTexture(GL_TEXTURE_2D, m_layer_tex[i]);
	glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
	glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
	if (same_format && !m_layer_changed[i])
		return m_layer_tex[i];
	m_layer_changed[i] = false;
	m_layer_format[i] = layer.format;

	if (layer.format == ODIN_FORMAT_ABGR1555) {
		/* The two 16-bit pixels of each 32-bit word are stored
		 * swapped, as the texture transfer does for the texture RAM
		 * (MEASURED on the von4 title text, GPU-RE/16 § 9). */
		for (unsigned y = 0; y < HEIGHT; y++) {
			const uint8_t *row = data + y * LAYER_PITCH;
			uint16_t *dst = &m_layer_pixels[size_t{y} * WIDTH];

			for (unsigned x = 0; x < WIDTH / 2; x++) {
				uint32_t word;

				std::memcpy(&word, row + x * 4, 4);
				dst[x * 2] = static_cast<uint16_t>(word >> 16);
				dst[x * 2 + 1] = static_cast<uint16_t>(word);
			}
		}
		if (same_format)
			glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, WIDTH, HEIGHT,
			                GL_RGBA, GL_UNSIGNED_SHORT_1_5_5_5_REV, m_layer_pixels.data());
		else
			glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB5_A1, WIDTH, HEIGHT, 0,
			             GL_RGBA, GL_UNSIGNED_SHORT_1_5_5_5_REV, m_layer_pixels.data());
	} else if (same_format) {
		glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, WIDTH, HEIGHT,
		                GL_RGBA, GL_UNSIGNED_INT_2_10_10_10_REV, data);
	} else {
		glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB10_A2, WIDTH, HEIGHT, 0,
		             GL_RGBA, GL_UNSIGNED_INT_2_10_10_10_REV, data);
	}
	gl::check("a layer");

	return m_layer_tex[i];
}

} /* namespace odin */
