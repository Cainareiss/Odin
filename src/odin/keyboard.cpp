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

#include "odin/keyboard.hpp"

#include <algorithm>

namespace odin {

void
Keyboard::set(SDL_Keycode key, bool down)
{
	const auto held = std::find(m_held.begin(), m_held.end(), key);

	if (down && held == m_held.end())
		m_held.push_back(key);
	else if (!down && held != m_held.end())
		m_held.erase(held);
}

bool
Keyboard::down(SDL_Keycode key) const
{
	return std::find(m_held.begin(), m_held.end(), key) != m_held.end();
}

/* The word of a player on the JVS board */
uint16_t
Keyboard::player_word(SDL_Keycode start, SDL_Keycode service, SDL_Keycode up, SDL_Keycode down_,
                      SDL_Keycode left, SDL_Keycode right, const SDL_Keycode push[6]) const
{
	uint16_t word = 0;

	if (down(start))
		word |= 0x8000;
	if (down(service))
		word |= 0x4000;
	if (down(up))
		word |= 0x2000;
	if (down(down_))
		word |= 0x1000;
	if (down(left))
		word |= 0x0800;
	if (down(right))
		word |= 0x0400;
	/* PUSH1 and PUSH2 in the high byte, PUSH3 to PUSH6 from bit 7 down */
	for (unsigned i = 0; i < 6; i++)
		if (down(push[i]))
			word |= static_cast<uint16_t>((i < 2) ? (0x0200 >> i) : (0x0080 >> (i - 2)));
	return word;
}

void
Keyboard::read_inputs(MieBoard::Inputs &inputs)
{
	static const SDL_Keycode push1[6] = { SDLK_LCTRL, SDLK_LALT, SDLK_LSHIFT, SDLK_z, SDLK_x, SDLK_c };
	static const SDL_Keycode push2[6] = { SDLK_a, SDLK_s, SDLK_q, SDLK_w, SDLK_e, SDLK_t };
	static const SDL_Keycode coin_key[2] = { SDLK_5, SDLK_6 };

	inputs.mainboard_test = down(SDLK_F5);
	inputs.mainboard_service = down(SDLK_F6);
	inputs.jvs_system = down(SDLK_F2) ? 0x80 : 0;
	inputs.jvs_player[0] = player_word(SDLK_1, SDLK_9, SDLK_UP, SDLK_DOWN, SDLK_LEFT, SDLK_RIGHT, push1);
	inputs.jvs_player[1] = player_word(SDLK_2, SDLK_0, SDLK_r, SDLK_f, SDLK_d, SDLK_g, push2);
	for (uint16_t &channel : inputs.jvs_analog)
		channel = 0x8000;
	/* A coin is counted on the key press, not while it is held */
	for (unsigned i = 0; i < 2; i++) {
		const bool now = down(coin_key[i]);

		inputs.jvs_coin[i] = now && !m_coin_was[i];
		m_coin_was[i] = now;
	}
}

} /* namespace odin */
