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
#include <string>

#include "odin/host_graphics.hpp"
#include "odin/loading_screen.hpp"
#include "odin/picture.hpp"

namespace odin {

struct Compose;
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

/* The pixels of a 2D layer: ABGR1555 a little-endian word each, A2BGR10 a
 * little-endian 32-bit word each */
enum class LayerTexels : uint8_t { ABGR1555, A2BGR10 };

/* What the graphics card of the host does for the renderer (renderer.hpp),
 * which draws the pictures of the board: the other side of the door of
 * host_graphics.hpp, said in the words of the board, never in those of the
 * API. HostGraphics makes one for each game, with the renderer, and it goes
 * with it. board_drawing_gl.cpp and board_pictures_gl.cpp do it with
 * OpenGL. The renderer still draws its meshes with OpenGL itself
 * (MARKDOWN/105, C3).
 *
 * The pictures it holds are `scale` times the screen of the hardware each
 * way (SCREEN_WIDTH by SCREEN_HEIGHT, hikaru_draw.hpp):
 *  - the scene, where a display list is drawn, with its depth;
 *  - the KEPT pictures, kept from one frame to the next: the two 3D
 *    buffers of the hardware, then the pictures of the last three lists
 *    (the pipe), on their way to the screen;
 *  - the picture of the frame: the 3D and the 2D layers composed.
 *
 * On the drawing thread (display.hpp), as the renderer. */
class BoardDrawing {
public:
	static constexpr unsigned KEPT = 5;

	/* With OpenGL, on the thread its context is current on */
	static std::unique_ptr<BoardDrawing> make_gl();
	virtual ~BoardDrawing() = default;

	/* The pictures `scale` times finer, 1 at first; the kept ones carried
	 * over, each pixel the nearest */
	virtual void set_scale(unsigned scale) = 0;
	/* Until C3 piece 3: the OpenGL framebuffer of the scene, which the
	 * renderer still draws its meshes in */
	virtual uint32_t scene_framebuffer() const = 0;

	/* The scene into a picture of the pipe: as it is, or blended with a
	 * 3D buffer, which fades by `rate` (181) at each frame: the motion
	 * blur of the cabinets */
	virtual void scene_to_pipe(unsigned slot) = 0;
	virtual void blend_scene_to_pipe(unsigned slot, unsigned buffer, uint32_t rate) = 0;
	/* A 3D buffer takes a picture of the pipe */
	virtual void keep_picture(unsigned slot, unsigned buffer) = 0;

	/* A 2D layer: SCREEN_WIDTH by SCREEN_HEIGHT pixels, the top line
	 * first, kept until it is given again */
	virtual void set_layer(unsigned i, LayerTexels texels, const void *pixels) = 0;
	/* The picture of the frame: the picture `shown` of the pipe and the
	 * layers shown, as `compose` says (hikaru_draw.hpp) */
	virtual void compose(const Compose &compose, unsigned shown, const bool layers[2]) = 0;
	/* The picture of the frame in the window, at `rect`, black around:
	 * pixel for pixel at its size, else each pixel the mean of what it
	 * covers; through the filter if there is one */
	virtual void present(PictureRect rect, unsigned window_w, unsigned window_h) = 0;
	virtual HostPicture frame_picture() const = 0;
	/* The filter of a preset (picture_filter.hpp), built when the path
	 * changes; none with an empty path, or when it does not build (said
	 * once on stderr) */
	virtual void set_filter(const std::string &preset) = 0;
	/* The loading screen: the window black, then each rectangle of it at
	 * its shade */
	virtual void draw_loading(std::span<const LoadingRect> rects, unsigned window_w, unsigned window_h) = 0;

	/* The kept pictures for a save state: KEPT of them at the size of the
	 * screen of the hardware, bytes of RGBA, the bottom line first, the
	 * first pixel of each scale x scale; and back, each pixel then all of
	 * its scale x scale */
	virtual void read_kept(std::span<uint8_t> pictures) = 0;
	virtual void write_kept(std::span<const uint8_t> pictures) = 0;

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
