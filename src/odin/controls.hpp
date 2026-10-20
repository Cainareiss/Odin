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
#include <utility>
#include <vector>

#include <SDL3/SDL.h>

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
	/* While the menu is open: what was let go of is let go of, as a frame
	 * would */
	void settle();

	/* The panel of the game and its inputs (control_panel.hpp), made
	 * again when they change in the settings */
	const ControlPanel &panel() const { return m_panel; }
	void rebind(const Settings &settings);
	/* Whether a control of the panel or a role of the cabinet is worked
	 * now */
	bool touched(size_t control) const { return m_panel.touched(host(), control); }
	bool touched(Role role) const { return m_panel.touched(host(), role); }
	/* Guide of a gamepad pressed and let go alone since the last call:
	 * the menu (with another button, Guide is a shortcut) */
	bool guide_tapped() { return std::exchange(m_guide_tapped, false); }
	/* The gamepad of a player: its name, its kind (SDL_GamepadType);
	 * empty and unknown when there is none */
	std::string pad_name(unsigned player) const;
	SDL_GamepadType pad_type(unsigned player) const;

	void read_inputs(MieBoard::Inputs &inputs) override { m_panel.fill(inputs); }
	/* The outputs of the board after a frame: the pad of a player shakes
	 * while a rumble output of that player is on; the pad of player 1 also
	 * as hard as the vibration device the sound of the optional board
	 * feeds, when it feels it and no mute output is on, or as the shocks
	 * the drive board is told, when it feels them. */
	void show(std::span<const uint8_t> outputs, std::span<const int16_t> option_sound);
	/* No pad shakes (the emulation stops) */
	void quiet();
	/* EXPERIMENTAL: the pad of player 1 feels the sound of the optional
	 * board, as the vibration device of the cabinet it feeds */
	void feel_option_board(bool on) { m_felt = on; }
	/* EXPERIMENTAL: the pad of player 1 feels the shocks among the forces
	 * the drive board is told, TX0 on that output (0: none), not what the
	 * moves of the wheel give; the stronger of the two */
	void feel_drive_board(unsigned first_output) { m_drive_output = first_output; }

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
		SDL_Gamepad *handle = nullptr;
		SDL_JoystickID id = -1;
		PadState state;
		std::array<bool, SDL_GAMEPAD_BUTTON_COUNT> fresh{};
		std::array<bool, SDL_GAMEPAD_BUTTON_COUNT> release{};
		Uint16 rumbling = 0;	/* how hard it shakes */
		Uint64 rumbled = 0;	/* when it was last asked to */
		bool guide_alone = false;	/* Guide held, no other button since */
	};

	void open(SDL_JoystickID id);
	void close(SDL_JoystickID id);
	Pad *find(SDL_JoystickID id);
	HostState host() const;

	Keyboard &m_keys;
	std::vector<GameControl> m_game_controls;
	std::string m_game;
	ControlPanel m_panel;
	std::vector<GameOutput> m_outputs;
	bool m_on;
	bool m_guide_tapped = false;
	bool m_felt = false;
	Uint16 m_felt_level = 0;	/* how hard the felt sound shakes, smoothed */
	unsigned m_drive_output = 0;
	Uint16 m_drive_level = 0;	/* how hard the felt shocks shake */
	int m_force_was = 0;		/* the force of the last frame */
	int m_wheel_channel = -1;	/* the steering wheel of player 1, if any */
	uint16_t m_wheel_was = 0;
	std::array<double, 6> m_wheel_moves{};	/* its moves over the last frames */
	size_t m_wheel_move = 0;
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
