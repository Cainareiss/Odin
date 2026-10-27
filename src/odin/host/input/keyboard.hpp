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

#include <SDL3/SDL.h>

namespace odin {

/* The keys held. A key pressed and released before a frame saw it stays
 * held until that frame has run (settle), so that no press is lost. What
 * each key does on the cabinet is in bindings.cpp. */
class Keyboard {
public:
	/* A key went down or up. */
	void set(SDL_Keycode key, bool down);
	bool down(SDL_Keycode key) const;
	const std::vector<SDL_Keycode> &held() const { return m_held; }

	/* A frame has seen the keys. */
	void settle();
	/* The keys down now are let go of, and pressed again only once
	 * released: the key that closes the menu is not the game's. */
	void swallow();

private:
	std::vector<SDL_Keycode> m_held;
	std::vector<SDL_Keycode> m_fresh;	/* down since the last frame */
	std::vector<SDL_Keycode> m_release;	/* up before a frame saw them */
	std::vector<SDL_Keycode> m_swallowed;	/* down when swallowed, until released */
};

} /* namespace odin */

#endif /* ODIN_KEYBOARD_HPP */
