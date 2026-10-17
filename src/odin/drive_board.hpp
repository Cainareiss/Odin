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

#ifndef ODIN_DRIVE_BOARD_HPP
#define ODIN_DRIVE_BOARD_HPP

#include <cmath>
#include <cstdint>
#include <span>

namespace odin {

/* The drive board of a cabinet, the motor of its wheel, as the outputs of
 * the I/O board command it; not yet the board itself. NASCAR: 838-13366
 * running EPR-20512, as Sega Rally 2. The command is a byte on eight
 * outputs, TX0 first, low when set (deluxe wiring: CN9 pins 25-32 of the
 * 837-13844; the decoding of Flycast, measured here: 00 at power-on, C1
 * and C7 to start, 40 at rest). Its commands (njz3, DRIVEBOARD.md): 00-3F
 * a force to the left (00 none), 40-7F to the right (40 none), 8X the
 * tests, C6, C7, CB, FF the others. */
struct DriveBoard {
	/* The strongest force each way */
	static constexpr uint8_t MOST = 0x3F;

	/* The command on the outputs of the board, TX0 on first_output (1 to
	 * 32 as JVS counts them, output 1 bit 7 of the first byte) */
	static uint8_t command(std::span<const uint8_t> outputs, unsigned first_output)
	{
		uint8_t set = 0;

		for (unsigned bit = 0; bit < 8; bit++) {
			const unsigned output = first_output + bit - 1;

			if (output / 8 < outputs.size() && (outputs[output / 8] & (0x80 >> (output % 8))))
				set = static_cast<uint8_t>(set | (1u << bit));
		}
		return static_cast<uint8_t>(~set);
	}

	/* How hard the force of a command shakes a pad, 0 to 0xFFFF: its size
	 * against the strongest, either way; none for the other commands.
	 * HEURISTIC: a pad has no direction. */
	static uint16_t shake_of(uint8_t command)
	{
		if (command >= 0x80)
			return 0;

		const unsigned force = command & MOST;

		return static_cast<uint16_t>(std::lround(force * 65535.0 / MOST));
	}
};

} /* namespace odin */

#endif /* ODIN_DRIVE_BOARD_HPP */
