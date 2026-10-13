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

#include "odin/renderer_textures.hpp"
#include "odin/renderer_gl.hpp"

#include <algorithm>
#include <cstdlib>
#include <cstring>

namespace odin {

namespace {

/* A bank of the texture RAM is 2048 x 1024 texels of 16 bits. */
constexpr uint32_t BANK_WIDTH = 2048;
constexpr uint32_t BANK_HEIGHT = 1024;

bool
same_texture(const odin_texhead_t &a, const odin_texhead_t &b)
{
	return odin_tex_format(&a) == odin_tex_format(&b) &&
	       odin_tex_logw(&a) == odin_tex_logw(&b) &&
	       odin_tex_logh(&a) == odin_tex_logh(&b) &&
	       odin_tex_bank(&a) == odin_tex_bank(&b) &&
	       odin_tex_slotx(&a) == odin_tex_slotx(&b) &&
	       odin_tex_sloty(&a) == odin_tex_sloty(&b);
}

/* A texel of the format 2 is one 4-bit value: the ramp PHARRIER draws its
 * red halo with goes 0, 2, 4, 6, 9, B, D, F across. The four channels get
 * it (HEURISTIC: the halo takes its alpha, the BOOTROM font, 0 or F, its
 * colour weight and its alpha). */
uint16_t
i4_to_rgba4444(uint32_t texel)
{
	return static_cast<uint16_t>((texel & 15) * 0x1111);
}

} /* namespace */

TextureCache::TextureCache(const std::span<const uint8_t> texram[2])
	: m_texram{texram[0], texram[1]}, m_is_clear{true, true}, m_no_mipmaps(false), m_anisotropy(1),
	  m_log(nullptr)
{
	for (auto &bank : m_slots)
		bank.resize(SLOTS_X * SLOTS_Y);
}

TextureCache::~TextureCache()
{
	invalidate(-1);
	delete_dead();
}

void
TextureCache::bury(GLuint id)
{
	if (id)
		m_dead.push_back(id);
}

void
TextureCache::delete_dead()
{
	if (m_dead.empty())
		return;
	glDeleteTextures(static_cast<GLsizei>(m_dead.size()), m_dead.data());
	gl::check("deleting textures");
	m_dead.clear();
}

/* They belong to the texhead, not to the texels: two texheads may share a
 * slot and differ only there, so this runs on every bind, as the X360 port
 * does (825C6C98, 825C6650). */
void
TextureCache::set_sampling(const odin_texhead_t &th)
{
	/* Mirror wins over repeat, else clamp. */
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S,
	                odin_tex_mirroru(&th) ? GL_MIRRORED_REPEAT :
	                odin_tex_repeatu(&th) ? GL_REPEAT : GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T,
	                odin_tex_mirrorv(&th) ? GL_MIRRORED_REPEAT :
	                odin_tex_repeatv(&th) ? GL_REPEAT : GL_CLAMP_TO_EDGE);

	/* Filter of the 0C1 mode; mode 3 is left at the sampler defaults in
	 * the port, trilinear here (HEURISTIC). */
	switch (odin_tex_filter(&th)) {
	case 0:
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
		break;
	case 2:
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
		break;
	default:
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
		break;
	}

	/* The byte of the 0C1 moves the level. HEURISTIC: eighths of a level,
	 * signed, on top of the -0.5 of the X360 port (825C6650), which
	 * ignores the byte and filters with an anisotropy of 8 instead. The
	 * floor of the VON4 arena sends D0 and its cabinet shows the finest
	 * level where a pixel is 25 texels long: sixteenths do not reach it.
	 * How the board picks the level is not known; the one of OpenGL
	 * stands here. */
	glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_LOD_BIAS,
	                -0.5f + static_cast<float>(odin_tex_bias(&th)) * 0.125f);
}

/* A word holds a block of two by two texels: a texhead of w x h words is a
 * picture of 2w x 2h texels. The font of the BOOTROM menu is one of 64 x 64
 * words, its glyphs 8 texels apart across and 16 down. */
std::vector<uint16_t>
TextureCache::decode_i4(uint32_t bank, uint32_t w, uint32_t h, uint32_t x0, uint32_t y0) const
{
	std::vector<uint16_t> data(size_t{w} * 2 * h * 2);

	for (uint32_t y = 0; y < h; y++) {
		uint16_t *top = &data[size_t{y} * 2 * (w * 2)];
		uint16_t *bottom = top + w * 2;

		for (uint32_t x = 0; x < w; x++) {
			const size_t offs = size_t{y0 + y} * (BANK_WIDTH * 2) + (x0 + x) * 2;
			uint16_t texels;

			std::memcpy(&texels, &m_texram[bank][offs], 2);
			top[x * 2 + 0] = i4_to_rgba4444(texels >> 12);
			top[x * 2 + 1] = i4_to_rgba4444(texels >> 8);
			bottom[x * 2 + 0] = i4_to_rgba4444(texels >> 4);
			bottom[x * 2 + 1] = i4_to_rgba4444(texels >> 0);
		}
	}
	return data;
}

GLuint
TextureCache::decode(const odin_texhead_t &th) const
{
	static const GLint la8_swizzle[4] = { GL_RED, GL_RED, GL_RED, GL_GREEN };
	const uint32_t format = odin_tex_format(&th);
	uint32_t w = 16u << odin_tex_logw(&th);
	uint32_t h = 16u << odin_tex_logh(&th);
	uint32_t x0, y0, bank = odin_tex_bank(&th), scale = 1;
	GLenum internal, layout = GL_RGBA, type;
	GLuint id;

	/* The levels alternate between the two banks (see
	 * Antarctic::copy_texture), down to the level where the smaller side
	 * reaches one texel: the games upload no further (the sky dome of
	 * von4, 256x128, has a zero 1x1 level that the port would read,
	 * 825C6A00; HEURISTIC). */
	const uint32_t num_levels = m_no_mipmaps ? 1 :
		std::min(odin_tex_logw(&th), odin_tex_logh(&th)) + 5;

	switch (format) {
	case ODIN_FORMAT_ABGR1555:
		internal = GL_RGB5_A1;
		type = GL_UNSIGNED_SHORT_1_5_5_5_REV;
		break;
	case ODIN_FORMAT_ABGR4444:
		internal = GL_RGBA4;
		type = GL_UNSIGNED_SHORT_4_4_4_4_REV;
		break;
	case ODIN_FORMAT_LA8:
		internal = GL_RGBA8;
		layout = GL_RG;
		type = GL_UNSIGNED_BYTE;
		break;
	case ODIN_FORMAT_I4:
		/* Four texels in a word */
		internal = GL_RGBA4;
		type = GL_UNSIGNED_SHORT_4_4_4_4_REV;
		scale = 2;
		break;
	default:
		return 0;
	}

	odin_tex_coords(&th, &x0, &y0);
	/* Each level then lies in its bank too. */
	if (x0 + w > BANK_WIDTH || y0 + h > BANK_HEIGHT) {
		std::fprintf(stderr, "renderer: a texture outside its bank: %s\n",
		             odin_texhead_str(&th));
		std::abort();
	}

	glGenTextures(1, &id);
	glActiveTexture(GL_TEXTURE0);
	glBindTexture(GL_TEXTURE_2D, id);
	set_sampling(th);

	/* The board has trilinear filtering alone (its patents); the
	 * anisotropy is an option of ours. */
	if (m_anisotropy > 1 && GLEW_EXT_texture_filter_anisotropic)
		glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAX_ANISOTROPY_EXT,
		                static_cast<float>(m_anisotropy));
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, static_cast<GLint>(num_levels - 1));
	if (format == ODIN_FORMAT_LA8)
		glTexParameteriv(GL_TEXTURE_2D, GL_TEXTURE_SWIZZLE_RGBA, la8_swizzle);

	/* The levels are allocated at once: given one by one, a 64x64 texture
	 * takes the NVIDIA driver 595 some 1.7 ms instead of 0.03. */
	glTexStorage2D(GL_TEXTURE_2D, static_cast<GLsizei>(num_levels), internal,
	               static_cast<GLsizei>(w * scale), static_cast<GLsizei>(h * scale));

	glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
	for (uint32_t level = 0; level < num_levels; level++) {
		if (format == ODIN_FORMAT_I4) {
			const std::vector<uint16_t> data = decode_i4(bank, w, h, x0, y0);

			glTexSubImage2D(GL_TEXTURE_2D, static_cast<GLint>(level), 0, 0,
			                static_cast<GLsizei>(w * 2), static_cast<GLsizei>(h * 2),
			                layout, type, data.data());
		} else {
			/* Read where it lies in the bank */
			glPixelStorei(GL_UNPACK_ROW_LENGTH, BANK_WIDTH);
			glPixelStorei(GL_UNPACK_SKIP_ROWS, static_cast<GLint>(y0));
			glPixelStorei(GL_UNPACK_SKIP_PIXELS, static_cast<GLint>(x0));
			glTexSubImage2D(GL_TEXTURE_2D, static_cast<GLint>(level), 0, 0,
			                static_cast<GLsizei>(w), static_cast<GLsizei>(h),
			                layout, type, m_texram[bank].data());
		}

		w = std::max(w >> 1, 1u);
		h = std::max(h >> 1, 1u);
		x0 += (BANK_WIDTH - x0) / 2;
		y0 += (BANK_HEIGHT - y0) / 2;
		bank ^= 1;
	}

	glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
	glPixelStorei(GL_UNPACK_SKIP_ROWS, 0);
	glPixelStorei(GL_UNPACK_SKIP_PIXELS, 0);
	gl::check("decoding a texture");

	return id;
}

bool
TextureCache::slot_of(const odin_texhead_t &th, uint32_t &bank, size_t &index)
{
	const uint32_t slotx = odin_tex_slotx(&th);
	const uint32_t sloty = odin_tex_sloty(&th);

	if (slotx < FIRST_SLOT_X || sloty < FIRST_SLOT_Y)
		return false;
	bank = odin_tex_bank(&th);
	index = (sloty - FIRST_SLOT_Y) * SLOTS_X + (slotx - FIRST_SLOT_X);
	return true;
}

bool
TextureCache::holds(const odin_texhead_t &th) const
{
	uint32_t bank;
	size_t index;

	if (!slot_of(th, bank, index))
		return true;
	for (const Texture &held : m_slots[bank][index])
		if (same_texture(th, held.th))
			return true;
	return false;
}

GLuint
TextureCache::get(const odin_texhead_t &th)
{
	uint32_t bank;
	size_t index;

	if (!slot_of(th, bank, index))
		return 0;

	Slot &slot = m_slots[bank][index];
	for (const Texture &held : slot)
		if (same_texture(th, held.th))
			return held.id;

	if (m_log)
		std::fprintf(m_log, "\tHR: TEXUPLOAD %s\n", odin_texhead_str(&th));
	const GLuint id = decode(th);
	if (!id)
		return 0;

	slot.push_back({id, th});
	m_is_clear[bank] = false;
	return id;
}

void
TextureCache::clear_bank(unsigned bank)
{
	if (m_is_clear[bank])
		return;
	m_is_clear[bank] = true;

	for (Slot &slot : m_slots[bank]) {
		for (const Texture &held : slot)
			bury(held.id);
		slot.clear();
	}
}

void
TextureCache::set_anisotropy(unsigned level)
{
	if (level == m_anisotropy)
		return;
	m_anisotropy = level;
	invalidate(-1);
}

void
TextureCache::invalidate(int bank)
{
	if (bank < 0) {
		clear_bank(0);
		clear_bank(1);
	} else {
		clear_bank(static_cast<unsigned>(bank));
	}
}

} /* namespace odin */
