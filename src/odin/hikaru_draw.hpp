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

/* How a mesh goes over what is drawn. The passes that do not blend replace
 * the pixel and cover what is below the 3D (they leave 0 in the alpha of
 * the colour buffer, which holds what the 3D lets through); a mesh faded
 * by the depth queue lets through what it does not hide. The blend modes
 * of a material (A81 bits 17:16), as the X360 port applies them (825B4290):
 * source alpha over the destination, the product, source alpha added, the
 * source as it is over the destination. Where the source is weighted by
 * its alpha the shader does it, before the clamp (weighs_by_alpha). The
 * translucent type is blended by the shader that sorts its fragments,
 * which does the same. */
enum class MeshBlend : uint8_t { REPLACE, FADED, ALPHA_OVER, PRODUCT, ALPHA_ADDED, OVER };

/* The blend of a mode of a material, 0 to 3 */
inline MeshBlend
material_blend(unsigned mode)
{
	constexpr MeshBlend MODES[4] = { MeshBlend::ALPHA_OVER, MeshBlend::PRODUCT, MeshBlend::ALPHA_ADDED, MeshBlend::OVER };

	return MODES[mode & 3];
}

inline bool
weighs_by_alpha(unsigned mode)
{
	return mode == 0 || mode == 2;
}

/* How the meshes of a polygon type are drawn: the render passes of the
 * X360 port (825B2408, 825B3EB0). The opaque and shadow types write the
 * depth without blending nor alpha test; the transparent type is
 * punch-through (alpha > 0.5, depth written, no blending); the background
 * and the translucent type blend with the mode of their material, keep the
 * depth and drop alpha <= 1/255. The background alone is not moved by the
 * depth bias of 101. */
struct PassRules {
	bool depth_write;
	bool depth_bias;
	float alpha_ref;	/* fragments at or below it are dropped; -1: none */
	bool material_blend;	/* else REPLACE, or FADED by the depth queue */
};

inline PassRules
pass_rules(unsigned polytype)
{
	switch (polytype) {
	case ODIN_POLYTYPE_TRANSPARENT:
		return { true, true, 0.5f, false };
	case ODIN_POLYTYPE_TRANSLUCENT:
		return { false, true, 1.0f / 255.0f, true };
	case ODIN_POLYTYPE_BACKGROUND:
		return { false, false, 1.0f / 255.0f, true };
	default:
		return { true, true, -1.0f, false };
	}
}

} /* namespace odin */

#endif /* ODIN_HIKARU_DRAW_HPP */
