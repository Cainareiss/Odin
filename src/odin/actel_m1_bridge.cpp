/*
 * Odin
 * Copyright (C) 2026 Réda Chérif-Touil
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 * 02110-1301, USA.
 */

#include "odin/actel_m1_bridge.hpp"
#include "odin/actel_m1.hpp"

#include <cstring>
#include <memory>

struct odin_actel_m1 {
	odin_actel_m1(uint32_t key, uint16_t id, const uint8_t *rom_data, size_t rom_bytes)
		: chip(key, id), rom(rom_data), rom_size(rom_bytes)
	{
	}

	odin::ActelM1 chip;
	const uint8_t *rom;
	size_t rom_size;
};

extern "C" {

odin_actel_m1_t *
odin_actel_m1_new(uint32_t key, uint16_t id, const uint8_t *rom, size_t rom_size)
{
	auto dev = std::make_unique<odin_actel_m1>(key, id, rom, rom_size);
	odin_actel_m1 *raw = dev.get();
	raw->chip.set_read([raw](uint32_t addr) -> uint8_t {
		return (raw->rom && addr < raw->rom_size) ? raw->rom[addr] : 0;
	});
	return dev.release();
}

void
odin_actel_m1_delete(odin_actel_m1_t *dev)
{
	std::unique_ptr<odin_actel_m1> owned(dev);
}

void
odin_actel_m1_reset(odin_actel_m1_t *dev)
{
	dev->chip.reset();
}

uint32_t
odin_actel_m1_read(odin_actel_m1_t *dev, uint32_t offset, unsigned size)
{
	const uint32_t reg = offset & ~3u;
	const uint32_t shift = (offset & 3) * 8;
	uint32_t value = 0;

	if (reg == odin::ActelM1::REG_DATA) {
		for (unsigned i = 0; i < size && i < 4; i++) {
			value |= static_cast<uint32_t>(dev->chip.next_byte()) << (i * 8);
		}
		return value;
	}
	value = dev->chip.read(reg) >> shift;
	return (size >= 4) ? value : (value & ((1u << (size * 8)) - 1));
}

void
odin_actel_m1_write(odin_actel_m1_t *dev, uint32_t offset, unsigned size, uint32_t value)
{
	const uint32_t reg = offset & ~3u;
	const uint32_t shift = (offset & 3) * 8;
	const uint32_t mask = (size >= 4) ? 0xFFFFFFFFu : (((1u << (size * 8)) - 1) << shift);

	dev->chip.write(reg, (dev->chip.read(reg) & ~mask) | ((value << shift) & mask));
}

size_t
odin_actel_m1_state_size(void)
{
	return sizeof(odin::ActelM1State);
}

void
odin_actel_m1_save_state(const odin_actel_m1_t *dev, void *out)
{
	odin::ActelM1State state;
	std::memset(&state, 0, sizeof(state));
	dev->chip.save(state);
	std::memcpy(out, &state, sizeof(state));
}

void
odin_actel_m1_load_state(odin_actel_m1_t *dev, const void *in)
{
	odin::ActelM1State state;
	std::memcpy(&state, in, sizeof(state));
	dev->chip.load(state);
}

} /* extern "C" */
