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

#ifndef ODIN_BOARD_DRAWING_HPP
#define ODIN_BOARD_DRAWING_HPP

#include <cstdint>
#include <memory>
#include <span>

namespace odin {

struct TextureSampling;

/* A texture the renderer draws the board with (0: none) */
struct HostTexture {
	uint32_t id = 0;
};

/* A way the renderer reads its textures (0: none) */
struct HostSampler {
	uint32_t id = 0;
};

/* The texels of a texture, 16 bits each as the texture RAM of the board
 * holds them (ODIN_FORMAT_*, antarctic_list.hpp): ABGR1555 and ABGR4444
 * a little-endian word each, LA8 a byte of light then one of alpha. */
enum class Texels : uint8_t { ABGR1555, ABGR4444, LA8 };

/* A level of a texture: the rectangle x, y, w by h of a picture `row`
 * texels wide that starts at `texels` */
struct TexelRect {
	const void *texels;
	unsigned row, x, y, w, h;
};

/* What the graphics card of the host does for the renderer (renderer.hpp),
 * which draws the pictures of the board: the other side of the door of
 * host_graphics.hpp, said in the words of the board, never in those of the
 * API. HostGraphics makes one for each game, with the renderer, and it goes
 * with it. board_drawing_gl.cpp does it with OpenGL. The renderer still
 * calls OpenGL itself for the rest (MARKDOWN/105, C3).
 *
 * On the drawing thread (display.hpp), as the renderer. */
class BoardDrawing {
public:
	/* With OpenGL, on the thread its context is current on */
	static std::unique_ptr<BoardDrawing> make_gl();
	virtual ~BoardDrawing() = default;

	/* The textures: one made of its levels, the largest first, as large
	 * as the first, their texels read before it returns; some forgotten */
	virtual HostTexture make_texture(Texels texels, std::span<const TexelRect> levels) = 0;
	virtual void forget_textures(std::span<const HostTexture> textures) = 0;
	/* A way to read them with that sampling (hikaru_draw.hpp) and that
	 * anisotropic filtering, 1 for none; forgotten */
	virtual HostSampler make_sampler(const TextureSampling &sampling, unsigned anisotropy) = 0;
	virtual void forget_sampler(HostSampler sampler) = 0;
};

} /* namespace odin */

#endif /* ODIN_BOARD_DRAWING_HPP */
