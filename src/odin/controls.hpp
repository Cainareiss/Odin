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
#include <vector>

#include <SDL.h>

#include "odin/bindings.hpp"
#include "odin/control_panel.hpp"
#include "odin/games.hpp"
#include "odin/hikaru.hpp"
#include "odin/keyboard.hpp"

namespace odin {

/* The control panel of the cabinet as the host works it: the keyboard and
 * a gamepad for each player, opened and closed as they are plugged in and
 * out, the first one for player 1. ODIN_CONTROLLERS=none leaves the
 * gamepads out. */
class Controls : public Hikaru::Panel {
public:
	Controls(Keyboard &keys, const std::vector<GameControl> &controls, const Bindings &bindings);
	~Controls();
	Controls(const Controls &) = delete;
	Controls &operator=(const Controls &) = delete;

	/* An event of a gamepad; false for any other. */
	bool event(const SDL_Event &event);
	/* Once a frame, before it runs. */
	void update();

	void read_inputs(MieBoard::Inputs &inputs) override { m_panel.fill(inputs); }

private:
	/* A pad and its buttons: a button pressed and released before a frame
	 * saw it stays pressed until that frame has run. */
	struct Pad {
		SDL_GameController *handle = nullptr;
		SDL_JoystickID id = -1;
		PadState state;
		std::array<bool, SDL_CONTROLLER_BUTTON_MAX> fresh{};
		std::array<bool, SDL_CONTROLLER_BUTTON_MAX> release{};
	};

	void open(int device);
	void close(SDL_JoystickID id);
	Pad *find(SDL_JoystickID id);

	Keyboard &m_keys;
	ControlPanel m_panel;
	bool m_on;
	std::array<Pad, 2> m_pads;	/* of each player */
};

} /* namespace odin */

#endif /* ODIN_CONTROLS_HPP */
