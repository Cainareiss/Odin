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

#include "odin/crypt_315_5881_bridge.hpp"
#include "odin/crypt_315_5881.hpp"

#include <memory>

namespace {

/* Register offsets of iomap_le (MAME 315-5881_crypt.cpp:37). */
enum : uint32_t {
	REG_READY = 0x0,
	REG_ADDR_LO = 0x8,
	REG_ADDR_HI = 0xA,
	REG_SUBKEY = 0xC,
	REG_DECRYPT = 0xE
};

} /* namespace */

struct odin_crypt5881 {
	explicit odin_crypt5881(uint32_t key)
		: chip(key), read(nullptr), ctx(nullptr), log(nullptr), unknown_logged(false)
	{
	}

	void log_unknown(char rw, uint32_t offset, unsigned size, uint32_t value)
	{
		if (unknown_logged) {
			return;
		}
		unknown_logged = true;
		std::fprintf(log ? log : stderr, "315-5881: unknown register %c offset %X size %u value %04X\n",
		             rw, offset, size, value);
	}

	odin::Crypt315_5881 chip;
	uint16_t (*read)(void *ctx, uint32_t addr);
	void *ctx;
	FILE *log;
	bool unknown_logged;
};

extern "C" {

odin_crypt5881_t *
odin_crypt5881_new(uint32_t key, uint16_t (*read)(void *ctx, uint32_t addr), void *ctx)
{
	auto dev = std::make_unique<odin_crypt5881>(key);
	dev->read = read;
	dev->ctx = ctx;
	odin_crypt5881 *raw = dev.get();
	raw->chip.set_read([raw](uint32_t addr) -> uint16_t {
		return raw->read ? raw->read(raw->ctx, addr) : 0;
	});
	return dev.release();
}

void
odin_crypt5881_delete(odin_crypt5881_t *dev)
{
	std::unique_ptr<odin_crypt5881> owned(dev);
}

void
odin_crypt5881_reset(odin_crypt5881_t *dev)
{
	dev->chip.reset();
}

uint32_t
odin_crypt5881_read(odin_crypt5881_t *dev, uint32_t offset, unsigned size)
{
	if (size == 2) {
		switch (offset) {
		case REG_READY:
			return dev->chip.ready();
		case REG_DECRYPT:
			return dev->chip.read_decrypt();
		default:
			break;
		}
	}
	dev->log_unknown('R', offset, size, 0);
	return 0;
}

void
odin_crypt5881_write(odin_crypt5881_t *dev, uint32_t offset, unsigned size, uint32_t value)
{
	if (size == 2) {
		const uint16_t data = static_cast<uint16_t>(value);
		switch (offset) {
		case REG_ADDR_LO:
			dev->chip.write_addr_lo(data);
			return;
		case REG_ADDR_HI:
			dev->chip.write_addr_hi(data);
			return;
		case REG_SUBKEY:
			dev->chip.write_subkey(data);
			return;
		default:
			break;
		}
	}
	dev->log_unknown('W', offset, size, value);
}

void
odin_crypt5881_set_log(odin_crypt5881_t *dev, FILE *out)
{
	dev->log = out;
	dev->chip.set_log(out);
}

} /* extern "C" */
