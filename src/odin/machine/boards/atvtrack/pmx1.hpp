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

#ifndef ODIN_PMX1_HPP
#define ODIN_PMX1_HPP

#include <cstdint>
#include <functional>
#include <span>
#include <unordered_map>
#include <vector>

#include "odin/machine/boards/atvtrack/pmx1_scene.hpp"

namespace odin {

/* The PowerVR Neon 250's processor, as its driver sees it: it runs Command
 * Control Blocks queued in video RAM, each naming a microcode routine.
 * HEURISTIC: routines are emulated from their effects, as the microcode is
 * unknown. */
class Pmx1 {
public:
	/* `vram` is the 32 MB the slave SH-4 sees at 18000000. `written`
	 * reports what the PMX wrote there. */
	Pmx1(std::span<uint8_t> vram, std::function<void(uint32_t offset, uint32_t size)> written);

	void reset();
	/* Register B0: 8001 starts the CCB queue, 1 says CCBs were queued */
	void doorbell(uint32_t value);

	static constexpr unsigned WIDTH = 640, HEIGHT = 480;
	/* Scenes draw at `fine` times screen resolution. Flat polygons: as
	 * they are (0), corners snapped to pixels (1), or over the pixels
	 * they cover (2). */
	void set_look(unsigned fine, unsigned flat_mode)
	{
		m_fine = fine;
		m_flat_mode = flat_mode;
	}
	/* The last scene, with everything it needs loaded since the previous
	 * call */
	PmxScene take_scene();

	uint32_t queue_read() const { return m_read; }
	void set_queue_read(uint32_t read) { m_read = read; }

private:
	uint32_t word(uint32_t offset) const;
	void put(uint32_t offset, uint32_t value);
	void run(uint32_t offset);
	struct State {
		uint32_t vertices, isp, tsp, tcw;
	};
	struct Vertex {
		float x, y, w, u, v;
		uint32_t argb, offset;
	};
	void render(uint32_t scene);
	void draw_list(uint32_t table, uint32_t size, bool translucent);
	void draw_strip(const State &state, uint32_t entry, bool translucent);
	struct Triangle {
		State state;
		Vertex a, b, c;
		bool translucent, native, sorted;
	};
	void push_triangle(const State &state, Vertex a, Vertex b, Vertex c, bool translucent);
	struct Point {
		double x, y;
	};
	static Point overlap_of(const Triangle &l, const Triangle &r);
	void sort_translucent(size_t from);
	void build_scene();
	uint32_t image_of(uint32_t tsp, uint32_t tcw);
	Vertex vertex(uint32_t offset) const;
	struct Texture {
		uint32_t codebook, base, width, height, side, stride;
		unsigned format, levels;
		float lod_scale;
		bool vq, linear, mipmap, flip_u, flip_v, clamp_u, clamp_v, bilinear;
	};
	Texture texture_of(uint32_t tsp, uint32_t tcw) const;
	uint64_t hash_of(const Texture &t) const;
	static uint32_t mip_of(const Texture &t, uint32_t size_x);
	uint32_t fetch(const Texture &t, uint32_t size_x, uint32_t size_y, uint32_t mip, uint32_t x, uint32_t y) const;

	std::span<uint8_t> m_vram;
	std::function<void(uint32_t, uint32_t)> m_written;
	uint32_t m_read = 0;	/* next CCB, from the start of the queue */
	std::vector<Triangle> m_triangles;
	unsigned m_fine = 1, m_flat_mode = 0;
	/* The scene, and what is known of the textures on the GPU */
	PmxScene m_scene;
	struct CardImage {
		uint32_t id;
		uint64_t hash;
		unsigned used;	/* the last render that used it */
	};
	std::unordered_map<uint64_t, CardImage> m_card_images;
	uint32_t m_next_image = 1;
	uint32_t m_last_isp = 0, m_last_tsp = 0, m_last_tcw = 0;
	unsigned m_rendered = 0;
};

} /* namespace odin */

#endif
