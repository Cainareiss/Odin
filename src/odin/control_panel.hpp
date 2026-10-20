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

#include <SDL3/SDL.h>

#include "odin/bindings.hpp"
#include "odin/games.hpp"
#include "odin/mie_board.hpp"

namespace odin {

/* A gamepad as the host sees it now */
struct PadState {
	std::array<int16_t, SDL_GAMEPAD_AXIS_COUNT> axes{};
	std::array<bool, SDL_GAMEPAD_BUTTON_COUNT> buttons{};
};

/* The mouse as the host sees it now: where it points on the picture, 0 to
 * 1 each way from the top left corner, off when away from it (or from the
 * window), and its buttons, left, middle, right. */
struct MouseState {
	double x = 0.5, y = 0.5;
	bool off = false;
	std::array<bool, 3> buttons{};
};

/* What the host holds now: the keys down, the pad of each player and the
 * mouse (nullptr: none). */
struct HostState {
	const std::vector<SDL_Keycode> *keys = nullptr;
	std::array<const PadState *, 2> pads{};
	const MouseState *mouse = nullptr;
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
	/* A key moves an analog control this much a frame, and lets it back
	 * as fast (the step of xemu's Chihiro wheel): a choice of the
	 * emulator. */
	static constexpr int KEY_STEP = 0x1000;
	/* An input takes an analog control from another when it moves this
	 * much: above the noise of a stick, a choice of the emulator. */
	static constexpr int MOVED = 0x400;

	ControlPanel(const std::vector<GameControl> &controls, const Settings &settings, const std::string &game);

	/* What the host holds now, once a frame before it runs: a lever moves
	 * and a coin goes in on a press. */
	void update(const HostState &host);
	/* The inputs of the last update; a coin goes in once. */
	void fill(MieBoard::Inputs &inputs);
	/* An analog channel as the last update left it */
	uint16_t analog_channel(size_t channel) const { return m_inputs.jvs_analog[channel]; }

	/* The controls of the panel, those of the game and those every
	 * cabinet has, and their inputs */
	const std::vector<GameControl> &controls() const { return m_controls; }
	const Bindings &bindings() const { return m_bindings; }
	/* Whether an input of a control of the panel, or of a role of the
	 * cabinet, is held now: a stick of an analog control from half its
	 * way */
	bool touched(const HostState &host, size_t control) const;
	bool touched(const HostState &host, Role role) const { return pressed(host, m_bindings.cabinet(role), 0); }

	/* The channel for that value of an axis, the low values on the way the
	 * game wants them: a stick from -32768 (left or up), a trigger from 0
	 * (let go). */
	static uint16_t channel(const PadInput &input, GameControl::Low low, int16_t value);
	/* The channel of a control nothing moves: a stick or a lever centred,
	 * a pedal let go. */
	static uint16_t rest(GameControl::Low low);
	/* The channel of a control at the end of its way: right or down
	 * (positive), left or up; a pedal down (positive). */
	static uint16_t end(GameControl::Low low, bool positive);
	/* The channel where the mouse points, from 0 to 1 across the picture:
	 * the whole way of the channel, the left or the top at 0. */
	static uint16_t pointed(GameControl::Low low, double at);

private:
	bool held(const HostState &host, const HostInput &input, unsigned player) const;
	bool pressed(const HostState &host, const std::vector<HostInput> &inputs, unsigned player) const;
	uint16_t analog(const HostState &host, size_t control);

	enum : uint8_t { NEVER, THERE, GONE };	/* an input never seen, there, gone after it was there */

	std::vector<GameControl> m_controls;
	Bindings m_bindings;
	MieBoard::Inputs m_inputs;
	std::vector<size_t> m_positions;		/* of each lever */
	std::vector<uint16_t> m_keyed;			/* the channel where the keys have taken it */
	std::vector<size_t> m_owner;			/* the input that works it: 0 the keys, then its axes */
	std::vector<std::vector<uint16_t>> m_kept;	/* where each of them was last kept */
	std::vector<std::vector<uint8_t>> m_presence;	/* NEVER, THERE or GONE at the last update */
	std::vector<std::array<bool, 2>> m_levers_were;	/* GEAR_DOWN, GEAR_UP at the last update */
	std::array<bool, 2> m_coins_were;
	std::array<bool, 2> m_coins;			/* gone in, not read yet */
	bool m_mouse_aims;				/* the mouse worked all its channels at the last update */
	bool m_mouse_moved;				/* it has moved at this update */
	bool m_mouse_seen;
	double m_mouse_x, m_mouse_y;			/* where it was when it last moved */
};

} /* namespace odin */

#endif /* ODIN_CONTROL_PANEL_HPP */
