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

#include "odin/x76f100_bridge.hpp"
#include "odin/x76f100.hpp"

#include <memory>
#include <span>

struct odin_x76f100 {
	odin::X76f100 chip;
};

extern "C" {

odin_x76f100_t *
odin_x76f100_new(void)
{
	auto dev = std::make_unique<odin_x76f100>();
	return dev.release();
}

void
odin_x76f100_delete(odin_x76f100_t *dev)
{
	std::unique_ptr<odin_x76f100> owned(dev);
}

void
odin_x76f100_load(odin_x76f100_t *dev, const uint8_t *data, size_t size)
{
	if (data) {
		dev->chip.load(std::span<const uint8_t>(data, size));
	}
}

void
odin_x76f100_reset(odin_x76f100_t *dev)
{
	dev->chip.reset();
}

void
odin_x76f100_set_pins(odin_x76f100_t *dev, int cs, int rst, int scl, int sda)
{
	dev->chip.set_pins(cs != 0, rst != 0, scl != 0, sda != 0);
}

int
odin_x76f100_sda(odin_x76f100_t *dev)
{
	return dev->chip.sda_read() ? 1 : 0;
}

} /* extern "C" */
