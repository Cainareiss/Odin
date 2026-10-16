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

#include "odin/jvs_837_13741.hpp"

#include <array>

namespace odin {

/* Block 44BC */
const char *Jvs837_13741::device_id() const
{
	return "SEGA ENTERPRISES,LTD.;837-13741  I/O CONTROL BD2;Ver0.15;99/06";
}

/* Block 45AC, the list of the DIP switches all off */
int32_t Jvs837_13741::function_list(std::span<uint8_t> out)
{
	static constexpr std::array<uint8_t, 16> list = {
		jvs::FUNC_SWITCH_INPUT, PLAYERS, 12, 0,
		jvs::FUNC_COIN_INPUT, COIN_SLOTS, 0, 0,
		jvs::FUNC_ANALOG_INPUT, ANALOG_CHANNELS, 0, 0,	/* bits not told */
		jvs::FUNC_GENERAL_OUTPUT, 22, 0, 0
	};

	for (size_t i = 0; i < list.size(); i++)
		out[i] = list[i];
	return static_cast<int32_t>(list.size());
}

/* 17CB: no player and no byte gives the system byte alone; 1 or 2 players
 * take 1 or 2 bytes each (1834, 18FD). The system byte has TEST and TILT
 * (bits 7 and 6, 0C2C), a player 8 switches then 4 in the high half of
 * the second byte (0C2C, 0CB3). */
int32_t Jvs837_13741::switches(std::span<uint8_t> out, uint8_t players, uint8_t bytes_per_player)
{
	if (players > PLAYERS || (players == 0) != (bytes_per_player == 0) || bytes_per_player > 2)
		return -1;

	uint32_t pos = 0;

	out[pos++] = m_inputs.system() & 0xC0;
	for (uint8_t i = 0; i < players; i++) {
		const uint16_t value = m_inputs.player(i);

		out[pos++] = static_cast<uint8_t>(value >> 8);
		if (bytes_per_player == 2)
			out[pos++] = static_cast<uint8_t>(value & 0xF0);
	}
	return static_cast<int32_t>(pos);
}

/* 1DC9: 1 to 3 bytes, kept in 8910-8912 */
bool Jvs837_13741::swoutputs(std::span<const uint8_t> values)
{
	if (values.empty() || values.size() > OUTPUT_BYTES)
		return false;
	for (size_t i = 0; i < values.size(); i++)
		m_outputs[i] = values[i];
	return true;
}

} /* namespace odin */
