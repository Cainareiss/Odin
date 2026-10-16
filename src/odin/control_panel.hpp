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

#ifndef ODIN_CONTROL_PANEL_HPP
#define ODIN_CONTROL_PANEL_HPP

#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include <SDL.h>

#include "odin/bindings.hpp"
#include "odin/games.hpp"
#include "odin/mie_board.hpp"

namespace odin {

/* A gamepad as the host sees it now */
struct PadState {
	std::array<int16_t, SDL_CONTROLLER_AXIS_MAX> axes{};
	std::array<bool, SDL_CONTROLLER_BUTTON_MAX> buttons{};
};

/* What the host holds now: the keys down, and the pad of each player
 * (nullptr: none). */
struct HostState {
	const std::vector<SDL_Keycode> *keys = nullptr;
	std::array<const PadState *, 2> pads{};
};

/* The control panel of a cabinet: from what the host holds, the switches
 * and the channels the board reads, each control worked by its own inputs
 * (bindings.hpp). A game without controls in the list has the standard
 * panel of the JVS: START, the directions and PUSH1 to PUSH6 of both
 * players ("1p_start", "1p_up"... "2p_push6"). Every game also has the
 * SERVICE switch of each player ("1p_service", "2p_service") and the
 * controls of the cabinet: TEST of the I/O board, the two coins, Test and
 * Service of the main board. */
class ControlPanel {
public:
	/* Half a stick or a trigger is pressed from half its way on: a choice
	 * of the emulator, not of the cabinet. */
	static constexpr int PRESSED = 16384;

	ControlPanel(const std::vector<GameControl> &controls, const Settings &settings, const std::string &game);

	/* What the host holds now, once a frame before it runs: a lever moves
	 * and a coin goes in on a press. */
	void update(const HostState &host);
	/* The inputs of the last update; a coin goes in once. */
	void fill(MieBoard::Inputs &inputs);

	/* The channel for that value of an axis, the low values on the way the
	 * game wants them: a stick from -32768 (left or up), a trigger from 0
	 * (let go). */
	static uint16_t channel(const PadInput &input, GameControl::Low low, int16_t value);

private:
	bool held(const HostState &host, const HostInput &input, unsigned player) const;
	bool pressed(const HostState &host, const std::vector<HostInput> &inputs, unsigned player) const;

	std::vector<GameControl> m_controls;
	Bindings m_bindings;
	MieBoard::Inputs m_inputs;
	std::vector<size_t> m_positions;		/* of each lever */
	std::vector<std::array<bool, 2>> m_levers_were;	/* GEAR_DOWN, GEAR_UP at the last update */
	std::array<bool, 2> m_coins_were;
	std::array<bool, 2> m_coins;			/* gone in, not read yet */
};

} /* namespace odin */

#endif /* ODIN_CONTROL_PANEL_HPP */
