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

#include <algorithm>
#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include <SDL3/SDL.h>

#include "odin/bindings.hpp"
#include "odin/games.hpp"
#include "odin/mie_board.hpp"

namespace odin {

/* The dead zone of the sticks of a gamepad and how far its triggers go
 * before they press a button, in % of their way: [gamepads] of odin.ini,
 * then [gamepad.<GUID>] of its make and model. */
struct PadTuning {
	static constexpr int DEAD_ZONE = 3, TRIGGER_THRESHOLD = 3;
	static constexpr int MOST_DEAD_ZONE = 30, MOST_TRIGGER_THRESHOLD = 50;

	int dead_zone = DEAD_ZONE, trigger_threshold = TRIGGER_THRESHOLD;

	/* Those of a make and model; a line that is not right is said on
	 * stderr when `tell` */
	static PadTuning of(const Settings &settings, const std::string &guid, bool tell);
	/* A stick, both its axes, past its dead zone: from 0 at its edge to
	 * all the way at its end, the same way */
	void shape(int16_t &x, int16_t &y) const;
	/* Where a trigger starts to press */
	constexpr int16_t trigger_pressed() const
	{
		return static_cast<int16_t>(std::max(1, trigger_threshold * 32767 / 100));
	}
};

/* A gamepad as the host sees it now: its sticks past their dead zone */
struct PadState {
	std::array<int16_t, SDL_GAMEPAD_AXIS_COUNT> axes{};
	std::array<bool, SDL_GAMEPAD_BUTTON_COUNT> buttons{};
	/* Pressed with Guide held: a shortcut of Odin (hotkeys.hpp), not the
	 * game's, until let go; it still works with Guide (guide+leftstick) */
	std::array<bool, SDL_GAMEPAD_BUTTON_COUNT> shortcut{};
	/* Where a trigger starts to press a button */
	int16_t trigger_pressed = PadTuning{}.trigger_pressed();
};

/* The mouse as the host sees it now: where it points on the picture, 0 to
 * 1 each way from the top left corner, off when away from it (or from the
 * window), and its buttons, left, middle, right. */
struct MouseState {
	double x = 0.5, y = 0.5;
	bool off = false;
	std::array<bool, 3> buttons{};
};

/* A joystick that is no gamepad (a wheel) as the host sees it now, all
 * of its way and no dead zone: its axes as SDL gives them, its buttons,
 * its hats (SDL_HAT_UP...); which axes have a value (SDL tells the first
 * one when it opens it, else its first move), and that first value, where
 * the axis rested */
struct JoyState {
	std::vector<int16_t> axes;
	std::vector<bool> known;
	std::vector<bool> buttons;
	std::vector<uint8_t> hats;
	std::vector<int16_t> rest;
};

/* What the host holds now: the keys down, the pad, the joystick and the
 * mouse of each player (nullptr: none): player 1's light gun, else the
 * mouse of the system; player 2's light gun. */
struct HostState {
	const std::vector<SDL_Keycode> *keys = nullptr;
	std::array<const PadState *, 2> pads{};
	std::array<const JoyState *, 2> joys{};
	std::array<const MouseState *, 2> mice{};
};

/* Where a control is now, for the drawing of the cabinet: worked or not;
 * an analog one from -1 (left or up) to 1 (right or down), a pedal from 0
 * to 1, as its inputs hold it now (a key at the end of the way); a lever at
 * its position. */
struct ControlNow {
	bool on = false;
	float value = 0.0f;
	size_t position = 0;
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
	/* Half a stick is pressed from half its way on (a trigger from its
	 * threshold, PadTuning): a choice of the emulator, not of the
	 * cabinet. */
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
	/* A control as the host holds it now, the lever where the last update
	 * left it; nothing changes */
	ControlNow now(const HostState &host, size_t control) const;

	/* The channel for that value of an axis, the low values on the way the
	 * game wants them: a stick from -32768 (left or up), a trigger from 0
	 * (let go); an axis of a joystick from -32768, a pedal too (let go
	 * there, or at 32767 turned over). */
	static uint16_t channel(const PadInput &input, GameControl::Low low, int16_t value);
	static uint16_t channel(const JoyInput &input, GameControl::Low low, int16_t value);
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
	/* The channel an axis of the joystick of the player gives that control
	 * (nothing: no such axis, or no value yet) */
	static std::optional<uint16_t> joy_channel(const JoyState *joy, const JoyInput &input, GameControl::Low low);
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
	/* The mouse of each player: it worked all its channels at the last
	 * update, it has moved at this one, where it was when it last moved */
	std::array<bool, 2> m_mouse_aims, m_mouse_moved, m_mouse_seen;
	std::array<double, 2> m_mouse_x, m_mouse_y;
};

} /* namespace odin */

#endif /* ODIN_CONTROL_PANEL_HPP */
