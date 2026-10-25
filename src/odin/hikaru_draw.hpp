/*
 * Odin
 * Copyright (C) 2026 Réda Chérif-Touil
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef ODIN_HIKARU_DRAW_HPP
#define ODIN_HIKARU_DRAW_HPP

#include <cstdint>

#include "odin/antarctic_list.hpp"

/* What the board asks to be drawn, worked out from its lists in plain data,
 * without a word of a graphics API (MARKDOWN/105): the rules of the board
 * are written here once, and the code of each API (OpenGL now) only turns
 * them into its own calls. */

namespace odin {

/* How a texture is read, as its texhead says: along u and v, its filter,
 * the level moved */
struct TextureSampling {
	enum class Wrap : uint8_t { CLAMP, REPEAT, MIRROR };
	enum class Filter : uint8_t { NEAREST, LINEAR, TRILINEAR };

	Wrap u, v;
	Filter filter;
	float lod_bias;
};

inline TextureSampling
sampling_of(const odin_texhead_t &th)
{
	TextureSampling s;

	/* Mirror wins over repeat, else clamp. */
	s.u = odin_tex_mirroru(&th) ? TextureSampling::Wrap::MIRROR :
	      odin_tex_repeatu(&th) ? TextureSampling::Wrap::REPEAT : TextureSampling::Wrap::CLAMP;
	s.v = odin_tex_mirrorv(&th) ? TextureSampling::Wrap::MIRROR :
	      odin_tex_repeatv(&th) ? TextureSampling::Wrap::REPEAT : TextureSampling::Wrap::CLAMP;
	/* Filter of the 0C1 mode; mode 3 is left at the sampler defaults in
	 * the port, trilinear here (HEURISTIC). */
	switch (odin_tex_filter(&th)) {
	case 0:
		s.filter = TextureSampling::Filter::NEAREST;
		break;
	case 2:
		s.filter = TextureSampling::Filter::LINEAR;
		break;
	default:
		s.filter = TextureSampling::Filter::TRILINEAR;
		break;
	}
	/* The byte of the 0C1 moves the level. HEURISTIC: eighths of a level,
	 * signed, on top of the -0.5 of the X360 port (825C6650), which
	 * ignores the byte and filters with an anisotropy of 8 instead. The
	 * floor of the VON4 arena sends D0 and its cabinet shows the finest
	 * level where a pixel is 25 texels long: sixteenths do not reach it.
	 * How the board picks the level is not known; the one of OpenGL
	 * stands here. */
	s.lod_bias = -0.5f + static_cast<float>(odin_tex_bias(&th)) * 0.125f;
	return s;
}

} /* namespace odin */

#endif /* ODIN_HIKARU_DRAW_HPP */
