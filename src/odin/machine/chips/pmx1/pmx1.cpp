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

#include "odin/machine/chips/pmx1/pmx1.hpp"

#include <cstring>

namespace odin {

Pmx1::Pmx1(std::span<uint8_t> vram)
	: m_vram(vram), m_strips(size_t{1} << 21)
{
}

uint32_t Pmx1::word(uint32_t offset) const
{
	uint32_t value = 0;

	if (offset + 4 <= m_vram.size())
		std::memcpy(&value, m_vram.data() + offset, 4);
	return value;
}

/* TEX_STRIDE, bits 5-0 of Texture Control (PG, the chip's programming guide, p. 92): 32 texels a step
 * (p. 117: "Index = V*StrideRegister*32 + U") */
void Pmx1::draw(uint32_t region_base, uint32_t object_base, uint32_t texture_control)
{
	m_stride = (texture_control & 0x3F) * 32;
	render_tiles(region_base, object_base);
}

} /* namespace odin */
