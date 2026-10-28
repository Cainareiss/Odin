/*
 *  ██████╗ ██████╗ ██╗███╗   ██╗
 * ██╔═══██╗██╔══██╗██║████╗  ██║
 * ██║   ██║██║  ██║██║██╔██╗ ██║
 * ██║   ██║██║  ██║██║██║╚██╗██║
 * ╚██████╔╝██████╔╝██║██║ ╚████║
 *  ╚═════╝ ╚═════╝ ╚═╝╚═╝  ╚═══╝
 *
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

#ifndef ODIN_SEATS_HPP
#define ODIN_SEATS_HPP

#include <array>
#include <string>
#include <vector>

#include <SDL3/SDL.h>

namespace odin {

/* A gamepad open: its id, and the GUID of its make and model */
struct OpenPad {
	SDL_JoystickID id;
	std::string guid;
};

/* The gamepad of each player (0: none), from those open in the order they
 * were plugged in, the GUID each player wants (empty: any), the very pad
 * each one picked (0: none; two of a make share their GUID) and the ones
 * they had: the one picked if it is open, else the one they want if they
 * have it, else the first free one of that kind, else the one they had (a
 * player keeps their pad when another goes), else the first free one. */
inline std::array<SDL_JoystickID, 2>
seat_pads(const std::vector<OpenPad> &pads, const std::array<std::string, 2> &wanted,
          const std::array<SDL_JoystickID, 2> &picked, const std::array<SDL_JoystickID, 2> &had)
{
	std::array<SDL_JoystickID, 2> out{};
	std::vector<bool> taken(pads.size());
	const auto take = [&](unsigned player, const auto &fits) {
		for (size_t i = 0; i < pads.size() && !out[player]; i++) {
			if (!taken[i] && fits(pads[i])) {
				out[player] = pads[i].id;
				taken[i] = true;
			}
		}
	};

	for (unsigned player = 0; player < 2; player++)
		if (picked[player])
			take(player, [&](const OpenPad &pad) { return pad.id == picked[player]; });
	for (unsigned player = 0; player < 2; player++)
		if (!wanted[player].empty())
			take(player, [&](const OpenPad &pad) { return pad.id == had[player] && pad.guid == wanted[player]; });
	for (unsigned player = 0; player < 2; player++)
		if (!wanted[player].empty())
			take(player, [&](const OpenPad &pad) { return pad.guid == wanted[player]; });
	for (unsigned player = 0; player < 2; player++)
		take(player, [&](const OpenPad &pad) { return pad.id == had[player]; });
	for (unsigned player = 0; player < 2; player++)
		take(player, [](const OpenPad &) { return true; });
	return out;
}

} /* namespace odin */

#endif /* ODIN_SEATS_HPP */
