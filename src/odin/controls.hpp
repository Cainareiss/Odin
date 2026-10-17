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

#ifndef ODIN_CONTROLS_HPP
#define ODIN_CONTROLS_HPP

#include <array>
#include <span>
#include <vector>

#include <SDL.h>

#include "odin/bindings.hpp"
#include "odin/control_panel.hpp"
#include "odin/games.hpp"
#include "odin/hikaru.hpp"
#include "odin/keyboard.hpp"

namespace odin {

/* The control panel of the cabinet as the host works it: the keyboard, a
 * gamepad for each player, opened and closed as they are plugged in and
 * out, the first one for player 1, and the mouse, once it has moved on the
 * window more than MOUSE_MOVED pixels from where the system first put it
 * (the system tells where it is when the window opens under it).
 * ODIN_CONTROLLERS=none leaves the gamepads and the mouse out. In a
 * game where player 1 has a sight, the pointer of the system is hidden on
 * the window. */
class Controls : public Hikaru::Panel {
public:
	Controls(Keyboard &keys, const std::vector<GameControl> &controls, const std::vector<GameOutput> &outputs,
	         const Settings &settings, const std::string &game);
	~Controls();
	Controls(const Controls &) = delete;
	Controls &operator=(const Controls &) = delete;

	/* An event of a gamepad or of the mouse; false for any other. */
	bool event(const SDL_Event &event);
	/* Once a frame, before it runs. */
	void update();

	void read_inputs(MieBoard::Inputs &inputs) override { m_panel.fill(inputs); }
	/* The outputs of the board after a frame: the pad of a player shakes
	 * while a rumble output of that player is on; the pad of player 1 also
	 * as hard as the vibration device the sound of the optional board
	 * feeds, when it feels it. */
	void show(std::span<const uint8_t> outputs, std::span<const int16_t> option_sound);
	/* No pad shakes (the emulation stops) */
	void quiet();
	/* EXPERIMENTAL: the pad of player 1 feels the sound of the optional
	 * board, as the vibration device of the cabinet it feeds */
	void feel_option_board(bool on) { m_felt = on; }

	static constexpr int MOUSE_MOVED = 2;
	/* The strength of both motors of a pad that shakes (the nozzle of
	 * Brave has one motor, on or off: a choice of the emulator), and how
	 * often a shake is asked again while it lasts (an effect has an end). */
	static constexpr Uint16 RUMBLE = 0xC000;
	static constexpr Uint32 RUMBLE_MS = 10000;
	static constexpr Uint32 RUMBLE_AGAIN_MS = 5000;
	/* The strength a felt sound changes the shake by, at the least, and
	 * how long a shake lasts before it changes again */
	static constexpr Uint16 FELT_STEP = 0x1000;
	static constexpr Uint32 FELT_MS = 50;

private:
	/* A pad and its buttons: a button pressed and released before a frame
	 * saw it stays pressed until that frame has run. */
	struct Pad {
		SDL_GameController *handle = nullptr;
		SDL_JoystickID id = -1;
		PadState state;
		std::array<bool, SDL_CONTROLLER_BUTTON_MAX> fresh{};
		std::array<bool, SDL_CONTROLLER_BUTTON_MAX> release{};
		Uint16 rumbling = 0;	/* how hard it shakes */
		Uint32 rumbled = 0;	/* when it was last asked to */
	};

	void open(int device);
	void close(SDL_JoystickID id);
	Pad *find(SDL_JoystickID id);

	Keyboard &m_keys;
	ControlPanel m_panel;
	std::vector<GameOutput> m_outputs;
	bool m_on;
	bool m_felt = false;
	Uint16 m_felt_level = 0;	/* how hard the felt sound shakes, smoothed */
	std::array<Pad, 2> m_pads;	/* of each player */
	MouseState m_mouse;
	bool m_mouse_seen;		/* it has moved on the window */
	bool m_mouse_placed;		/* the system has told where it is */
	int m_mouse_from_x, m_mouse_from_y;	/* where */
	std::array<bool, 3> m_mouse_fresh;
	std::array<bool, 3> m_mouse_release;
};

} /* namespace odin */

#endif /* ODIN_CONTROLS_HPP */
