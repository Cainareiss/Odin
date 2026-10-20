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

/* Unit tests for the buttons of the menu: their inputs by default and from
 * [menu] of odin.ini, what is refused, and the actions the events start.
 *
 * Build: g++ -std=c++20 -O2 -Wall -Wextra -Wpedantic -Wconversion -Wshadow \
 *            -I src $(pkg-config --cflags sdl3) tests/menu_nav/menu_nav_tests.cpp src/odin/menu_nav.cpp \
 *            src/odin/bindings.cpp src/odin/settings.cpp src/odin/roles.cpp src/odin/save_folder.cpp \
 *            $(pkg-config --libs sdl3) -o menu_nav_tests
 * Exit status is 0 when every check passes. */

#include "odin/menu_nav.hpp"

#include <cstdio>
#include <vector>

#include "odin/settings.hpp"

namespace {

using odin::MenuNav;
using odin::Settings;
using Actions = std::vector<MenuNav::Action>;

int g_checks, g_failures;

void check(bool ok, const char *what)
{
	g_checks++;
	if (!ok) {
		g_failures++;
		std::printf("FAILED: %s\n", what);
	}
}

SDL_Event key(SDL_Keycode code, bool down, bool repeat = false)
{
	SDL_Event event{};

	event.type = down ? SDL_EVENT_KEY_DOWN : SDL_EVENT_KEY_UP;
	event.key.key = code;
	event.key.down = down;
	event.key.repeat = repeat;
	return event;
}

SDL_Event button(SDL_GamepadButton which, bool down, SDL_JoystickID pad = 1)
{
	SDL_Event event{};

	event.type = down ? SDL_EVENT_GAMEPAD_BUTTON_DOWN : SDL_EVENT_GAMEPAD_BUTTON_UP;
	event.gbutton.which = pad;
	event.gbutton.button = static_cast<Uint8>(which);
	event.gbutton.down = down;
	return event;
}

SDL_Event axis(SDL_GamepadAxis which, Sint16 value, SDL_JoystickID pad = 1)
{
	SDL_Event event{};

	event.type = SDL_EVENT_GAMEPAD_AXIS_MOTION;
	event.gaxis.which = pad;
	event.gaxis.axis = static_cast<Uint8>(which);
	event.gaxis.value = value;
	return event;
}

void test_defaults()
{
	MenuNav nav;

	check(nav.event(button(SDL_GAMEPAD_BUTTON_SOUTH, true)) == Actions{ MenuNav::PRESS }, "A presses");
	check(nav.event(button(SDL_GAMEPAD_BUTTON_SOUTH, false)).empty(), "let go: nothing");
	check(nav.event(button(SDL_GAMEPAD_BUTTON_EAST, true)) == Actions{ MenuNav::BACK }, "B goes back");
	check(nav.event(button(SDL_GAMEPAD_BUTTON_WEST, true)) == Actions{ MenuNav::CLEAR } &&
	      nav.event(button(SDL_GAMEPAD_BUTTON_NORTH, true)) == Actions{ MenuNav::RESET }, "X clears, Y sets to default");
	check(nav.event(button(SDL_GAMEPAD_BUTTON_LEFT_SHOULDER, true)) == Actions{ MenuNav::PAGE_BEFORE } &&
	      nav.event(button(SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER, true)) == Actions{ MenuNav::PAGE_AFTER },
	      "LB and RB change the page");
	check(nav.event(axis(SDL_GAMEPAD_AXIS_RIGHT_TRIGGER, 30000)) == Actions{ MenuNav::GAME_AFTER } &&
	      nav.event(axis(SDL_GAMEPAD_AXIS_RIGHT_TRIGGER, 32767)).empty() &&
	      nav.event(axis(SDL_GAMEPAD_AXIS_RIGHT_TRIGGER, 0)).empty() &&
	      nav.event(axis(SDL_GAMEPAD_AXIS_RIGHT_TRIGGER, 20000)) == Actions{ MenuNav::GAME_AFTER },
	      "RT changes the game once each time it passes half its way");
	check(nav.event(axis(SDL_GAMEPAD_AXIS_LEFTY, -20000)) == Actions{ MenuNav::UP } &&
	      nav.event(axis(SDL_GAMEPAD_AXIS_LEFTY, 0)).empty() &&
	      nav.event(axis(SDL_GAMEPAD_AXIS_LEFTY, 20000)) == Actions{ MenuNav::DOWN },
	      "the left stick moves the frame up and down");
	check(nav.event(button(SDL_GAMEPAD_BUTTON_DPAD_LEFT, true)) == Actions{ MenuNav::LEFT } &&
	      nav.event(button(SDL_GAMEPAD_BUTTON_DPAD_RIGHT, true)) == Actions{ MenuNav::RIGHT }, "the D-pad too");
	check(nav.event(key(SDLK_UP, true)) == Actions{ MenuNav::UP } &&
	      nav.event(key(SDLK_UP, true, true)) == Actions{ MenuNav::UP } &&
	      nav.event(key(SDLK_RETURN, true)) == Actions{ MenuNav::PRESS } &&
	      nav.event(key(SDLK_RETURN, true, true)).empty(),
	      "the arrows and Return; an arrow held moves on, Return does not press again");
	check(nav.event(key(SDLK_ESCAPE, true)).empty(), "Escape is the menu's own key, not one of these");
}

void test_guide()
{
	MenuNav nav;

	check(nav.event(button(SDL_GAMEPAD_BUTTON_GUIDE, true, 7)).empty() &&
	      nav.event(button(SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER, true, 7)).empty(),
	      "Guide held: a shortcut, not the menu's");
	check(nav.event(button(SDL_GAMEPAD_BUTTON_LEFT_SHOULDER, true, 8)) == Actions{ MenuNav::PAGE_BEFORE },
	      "the other pad is not held");
	check(nav.event(button(SDL_GAMEPAD_BUTTON_GUIDE, false, 7)).empty() &&
	      nav.event(button(SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER, false, 7)).empty() &&
	      nav.event(button(SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER, true, 7)) == Actions{ MenuNav::PAGE_AFTER },
	      "Guide let go: the menu's again");
	nav.event(button(SDL_GAMEPAD_BUTTON_SOUTH, true));
	nav.reset();
	check(nav.event(button(SDL_GAMEPAD_BUTTON_SOUTH, true)) == Actions{ MenuNav::PRESS }, "all let go when it closes");
}

void test_settings()
{
	Settings settings;

	settings.parse("[menu]\nclassic = on\npage_after = key:F2, pad:x\nback = pad:guide+b\nup = mouse:left\n"
	               "down = pad:leftx\n", "test");

	const MenuNav nav(settings);
	MenuNav moved(settings);

	check(nav.inputs(MenuNav::PAGE_AFTER).size() == 2, "a line of [menu] takes the place of the inputs");
	check(moved.event(key(SDLK_F2, true)) == Actions{ MenuNav::PAGE_AFTER } &&
	      moved.event(button(SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER, true)).empty(), "RB no longer, F2 now");
	check(nav.inputs(MenuNav::BACK).size() == 1 && nav.inputs(MenuNav::UP).size() == 3 &&
	      nav.inputs(MenuNav::DOWN).size() == 3,
	      "refused, the defaults kept: a button with Guide, the mouse, a whole stick");
	check(MenuNav::knows("page_after") && MenuNav::knows("default") && !MenuNav::knows("classic"),
	      "the keys of [menu] it knows");
	check(MenuNav::standard(MenuNav::PRESS) == "key:Return, pad:a" && std::string(MenuNav::id(MenuNav::RESET)) == "default",
	      "a key and its inputs by default, as odin.ini writes them");
}

} /* namespace */

int main()
{
	test_defaults();
	test_guide();
	test_settings();

	std::printf("%d checks, %d failures\n", g_checks, g_failures);
	return g_failures ? 1 : 0;
}
