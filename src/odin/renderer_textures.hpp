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

#ifndef ODIN_RENDERER_TEXTURES_HPP
#define ODIN_RENDERER_TEXTURES_HPP

#include <cstdint>
#include <cstdio>
#include <span>
#include <vector>

#include <GL/glew.h>

#include "odin/antarctic_list.h"

namespace odin {

/* The textures of the texture RAM as OpenGL textures.
 *
 * A texture is decoded the first time a texhead is drawn and kept until its
 * bank of the texture RAM is written. The slots of a bank are 16 x 16 texels
 * and start at (80, C0); a slot has a texture for each size and format it
 * is read with: PHARRIER draws the same frames with a 32x64 and a 128x128
 * texture at the slot C8,D0 of the bank 1. */
class TextureCache {
public:
	/* texram: the two banks of the texture RAM, 4 MB each */
	explicit TextureCache(const std::span<const uint8_t> texram[2]);
	~TextureCache();

	/* The texture of a texhead, decoded if it is not held; 0 if it
	 * cannot be. It is left bound on the unit 0 when decoded. */
	GLuint get(const odin_texhead_t &th);
	/* The texture RAM of a bank has changed; every bank if negative. */
	void invalidate(int bank);

	/* Wrap and filter of the texture bound on the active unit. */
	static void set_sampling(const odin_texhead_t &th);

	/* A texture the graphics card may still be drawing with is not
	 * deleted at once: the driver would wait for the card, and the list
	 * drawn last may have to be drawn again with it. It goes when
	 * delete_dead is called, at the next frame. */
	void bury(GLuint id);
	void delete_dead();

	/* The first level only, for looking at a scene */
	void set_no_mipmaps(bool on) { m_no_mipmaps = on; }
	/* Anisotropic filtering, 1 for none as on the board. The textures
	 * held are decoded again. */
	void set_anisotropy(unsigned level);
	/* Each texture decoded is told there; nullptr for none. */
	void set_log(FILE *out) { m_log = out; }

private:
	struct Texture {
		GLuint id;
		odin_texhead_t th;
	};
	/* The textures read at one slot */
	using Slot = std::vector<Texture>;

	enum : uint32_t {
		FIRST_SLOT_X = 0x80,
		FIRST_SLOT_Y = 0xC0,
		SLOTS_X = 0x80,
		SLOTS_Y = 0x40
	};

	GLuint decode(const odin_texhead_t &th) const;
	std::vector<uint16_t> decode_i4(uint32_t bank, uint32_t w, uint32_t h,
	                                uint32_t x0, uint32_t y0) const;
	void clear_bank(unsigned bank);

	std::span<const uint8_t> m_texram[2];
	std::vector<Slot> m_slots[2];
	bool m_is_clear[2];
	std::vector<GLuint> m_dead;
	bool m_no_mipmaps;
	unsigned m_anisotropy;
	FILE *m_log;
};

} /* namespace odin */

#endif /* ODIN_RENDERER_TEXTURES_HPP */
