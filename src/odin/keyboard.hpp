/*
 * Odin
 * Copyright (C) 2026 Réda Chérif-Touil
 *
 * Valkyrie
 * Copyright (C) 2011-2013, Stefano Teso
 *
 * Valkyrie is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * Valkyrie is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with Valkyrie.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef ODIN_KEYBOARD_HPP
#define ODIN_KEYBOARD_HPP

#include <vector>

#include <SDL.h>

#include "odin/hikaru.hpp"

namespace odin {

/* The keyboard as the control panel of the cabinet.
 *
 * Switches of the main board: F5 Test, F6 Service. JVS I/O board: F2 Test,
 * 9 and 0 Service, 5 and 6 the coins. Player 1: 1 Start, the arrows,
 * LCtrl LAlt LShift Z X C. Player 2: 2 Start, R F D G, A S Q W E T. The
 * analog channels stay centred: no stick yet. */
class Keyboard : public Hikaru::Panel {
public:
	Keyboard() : m_coin_was{false, false} {}

	/* A key went down or up. */
	void set(SDL_Keycode key, bool down);
	bool down(SDL_Keycode key) const;

	void read_inputs(MieBoard::Inputs &inputs) override;

private:
	uint16_t player_word(SDL_Keycode start, SDL_Keycode service, SDL_Keycode up, SDL_Keycode down_,
	                     SDL_Keycode left, SDL_Keycode right, const SDL_Keycode push[6]) const;

	std::vector<SDL_Keycode> m_held;
	bool m_coin_was[2];
};

} /* namespace odin */

#endif /* ODIN_KEYBOARD_HPP */
