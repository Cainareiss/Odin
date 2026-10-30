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

#include "odin/machine/boards/atvtrack/pmx1.hpp"

#include <cstdio>
#include <algorithm>
#include <cmath>
#include <cstring>

namespace odin {

namespace {

/* Shared by the driver and the PMX: the setup block, its acknowledgement at
 * +4, the queue at +20, and the offset the driver has queued up to */
constexpr uint32_t CONTROL = 0x814800, ACK = CONTROL + 0x04, QUEUE_START = CONTROL + 0x20;
constexpr uint32_t QUEUE_WRITE = 0x811020;
constexpr uint32_t VRAM_BASE = 0x18000000;

constexpr uint32_t ROUTINE_STORE = 0x18000DA4;	/* address, count, value */
constexpr uint32_t ROUTINE_RENDER = 0x18008590;	/* flags; RENDER_SCENE */

/* The render routine's argument, written by the driver before queuing: its
 * third word is where the scene's tiles go */
constexpr uint32_t RENDER_SCENE = 0x99B8 + 8;


} /* namespace */

Pmx1::Pmx1(std::span<uint8_t> vram, std::function<void(uint32_t offset, uint32_t size)> written)
	: m_vram(vram), m_written(std::move(written))
{
}

void Pmx1::reset()
{
	m_read = 0;
}

uint32_t Pmx1::word(uint32_t offset) const
{
	uint32_t value = 0;

	if (offset + 4 <= m_vram.size())
		std::memcpy(&value, m_vram.data() + offset, 4);
	return value;
}

void Pmx1::put(uint32_t offset, uint32_t value)
{
	if (offset + 4 > m_vram.size())
		return;
	std::memcpy(m_vram.data() + offset, &value, 4);
	m_written(offset, 4);
}

void Pmx1::doorbell(uint32_t value)
{
	if (value == 0x8001) {
		m_read = 0;
		put(ACK, 1);
		return;
	}
	if (value != 1)
		return;
	const uint32_t start = word(QUEUE_START) - VRAM_BASE;
	const uint32_t queued = word(QUEUE_WRITE) & 0xFFFF;

	/* At most one lap of the queue */
	for (unsigned blocks = 0; m_read != queued && blocks < 0x10000 / 0x40; blocks++) {
		const uint32_t size = word(start + m_read);

		run(start + m_read);
		m_read = (m_read + (size ? size : 0x40)) & 0xFFFF;
	}
}

void Pmx1::run(uint32_t offset)
{
	const uint32_t routine = word(offset + 4);

	switch (routine) {
	case ROUTINE_STORE: {
		const uint32_t to = word(offset + 8) - VRAM_BASE, count = word(offset + 12), value = word(offset + 16);

		for (uint32_t i = 0; i < count && i < 0x1000; i++)
			put(to + i * 4, value);
		break;
	}
	case ROUTINE_RENDER:
		/* HEURISTIC: the scene is the 64 KB holding its tiles */
		render((word(RENDER_SCENE) - VRAM_BASE) & ~0xFFFFu);
		break;
	default:
		break;
	}
}

} /* namespace odin */
