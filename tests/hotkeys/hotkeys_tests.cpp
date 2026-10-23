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

/* Unit tests for the keys of Odin: those by default, the key and the
 * button of each action, the modifiers held with a key, a key or a button
 * given to another action, the lines of [hotkeys] and what is refused.
 *
 * Build: g++ -std=c++20 -O2 -Wall -Wextra -Wpedantic -Wconversion -Wshadow \
 *            -I src $(pkg-config --cflags sdl3) tests/hotkeys/hotkeys_tests.cpp src/odin/hotkeys.cpp \
 *            src/odin/settings.cpp src/odin/save_folder.cpp $(pkg-config --libs sdl3) -o hotkeys_tests
 * Exit status is 0 when every check passes. */

#include "odin/hotkeys.hpp"

#include <cstdio>

#include "odin/settings.hpp"

namespace {

using odin::HotKey;
using odin::Hotkeys;
using Action = odin::Hotkeys::Action;

int g_checks, g_failures;

void check(bool ok, const char *what)
{
	g_checks++;
	if (!ok) {
		g_failures++;
		std::printf("FAILED: %s\n", what);
	}
}

bool is(std::optional<Action> found, Action action)
{
	return found && *found == action;
}

void test_standard()
{
	const Hotkeys keys;

	check(is(keys.key_action(SDLK_ESCAPE, SDL_KMOD_NONE), Action::MENU), "Escape: the menu");
	check(is(keys.key_action(SDLK_F10, SDL_KMOD_NONE), Action::MENU), "F10: the menu");
	check(is(keys.key_action(SDLK_P, SDL_KMOD_NONE), Action::PAUSE), "P: pause");
	check(is(keys.key_action(SDLK_P, SDL_KMOD_LCTRL), Action::PAUSE), "P with Left Ctrl held (a player's button): pause");
	check(is(keys.key_action(SDLK_F4, SDL_KMOD_NONE), Action::SAVE_STATE), "F4: save the state");
	check(is(keys.key_action(SDLK_F1, SDL_KMOD_NONE), Action::LOAD_STATE), "F1: load it");
	check(is(keys.key_action(SDLK_F11, SDL_KMOD_NONE), Action::FULLSCREEN), "F11: the whole display");
	check(is(keys.key_action(SDLK_RETURN, SDL_KMOD_RALT), Action::FULLSCREEN), "Alt+Enter, either Alt");
	check(!keys.key_action(SDLK_RETURN, SDL_KMOD_NONE), "Enter alone: nothing");
	check(is(keys.key_action(SDLK_Q, SDL_KMOD_RCTRL), Action::QUIT), "Ctrl+Q: quit");
	check(is(keys.key_action(SDLK_R, SDL_KMOD_LCTRL), Action::RESET), "Ctrl+R: reset");
	check(!keys.key_action(SDLK_R, SDL_KMOD_NONE), "R alone: the game's");
	check(!keys.key_action(SDLK_Q, SDL_KMOD_NONE), "Q alone (a key of player 2): nothing");
	check(is(keys.pad_action(Hotkeys::GUIDE_ALONE), Action::MENU), "Guide tapped: the menu");
	check(is(keys.pad_action(SDL_GAMEPAD_BUTTON_BACK), Action::PAUSE), "Guide+Back: pause");
	check(is(keys.pad_action(SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER), Action::SAVE_STATE), "Guide+RB: save");
	check(is(keys.pad_action(SDL_GAMEPAD_BUTTON_LEFT_SHOULDER), Action::LOAD_STATE), "Guide+LB: load");
	check(is(keys.pad_action(SDL_GAMEPAD_BUTTON_START), Action::QUIT), "Guide+Start: quit");
	check(!keys.pad_action(SDL_GAMEPAD_BUTTON_LEFT_STICK), "Guide+L3 (SERVICE, a control): no action");
	check(!keys.pad_action(Hotkeys::NO_PAD), "no button: no action");
	check(keys.takes(SDLK_P) && keys.takes(SDLK_F10) && keys.takes(SDLK_ESCAPE), "P, F10, Escape are Odin's");
	check(!keys.takes(SDLK_Q) && !keys.takes(SDLK_RETURN), "Q and Enter alone are not");
}

void test_text()
{
	check(Hotkeys::text({ SDLK_Q, SDL_KMOD_CTRL }) == "Ctrl+Q", "Ctrl+Q said");
	check(Hotkeys::text({ SDLK_F10, SDL_KMOD_NONE }) == "F10", "F10 said");
	check(Hotkeys::key_of("Alt+Return") == HotKey{ SDLK_RETURN, SDL_KMOD_ALT }, "Alt+Return read");
	check(Hotkeys::key_of(" Ctrl+Shift+F5 ") == HotKey{ SDLK_F5, static_cast<SDL_Keymod>(SDL_KMOD_CTRL | SDL_KMOD_SHIFT) },
	      "two modifiers read");
	check(Hotkeys::key_of("Keypad +") == HotKey{ SDLK_KP_PLUS, SDL_KMOD_NONE }, "a key whose name ends with +");
	check(!Hotkeys::key_of("Hyper+Q"), "an unknown modifier refused");
	check(!Hotkeys::key_of("Left Ctrl"), "a modifier alone refused");
	check(!Hotkeys::key_of("NoSuchKey"), "an unknown key refused");
	check(Hotkeys::line(Hotkeys::standard(Action::MENU)) == "Escape, F10, pad:guide", "the line of the menu");
	check(Hotkeys::line(Hotkeys::standard(Action::QUIT)) == "Ctrl+Q, pad:guide+start", "the line of quit");
	check(Hotkeys::line(Hotkeys::standard(Action::RESET)) == "Ctrl+R", "the line of reset");
	check(Hotkeys::line(Hotkeys::standard(Action::FULLSCREEN)) == "F11, Alt+Return", "no button: none said");
	for (size_t a = 0; a < Hotkeys::ACTIONS; a++) {
		const Hotkeys::Binding &standard = Hotkeys::standard(static_cast<Action>(a));

		check(Hotkeys::parse(Hotkeys::line(standard)) == standard, "each line read back as it was");
	}
	check(Hotkeys::parse("none") == Hotkeys::Binding{}, "none: no key, no button");
	check(!Hotkeys::parse("F1, F2, F3"), "three keys refused");
	check(!Hotkeys::parse("pad:start"), "a button without Guide refused");
	check(!Hotkeys::parse("pad:guide+back, pad:guide+start"), "two buttons refused");
	check(!Hotkeys::parse("pad:guide+guide"), "Guide with Guide refused");
}

void test_changes()
{
	Hotkeys keys;

	keys.set_key(Action::PAUSE, 0, { SDLK_F10, SDL_KMOD_NONE });
	check(is(keys.key_action(SDLK_F10, SDL_KMOD_NONE), Action::PAUSE), "F10 given to pause");
	check(keys.of(Action::MENU).keys[1].key == SDLK_UNKNOWN && keys.of(Action::MENU).keys[0].key == SDLK_ESCAPE,
	      "the menu lost it, keeps Escape");
	check(!keys.key_action(SDLK_P, SDL_KMOD_NONE), "P is nothing now");
	keys.set_key(Action::SAVE_STATE, 1, { SDLK_S, SDL_KMOD_LCTRL });
	check(keys.of(Action::SAVE_STATE).keys[1] == HotKey{ SDLK_S, SDL_KMOD_CTRL }, "a modifier kept of either side");
	check(is(keys.key_action(SDLK_S, SDL_KMOD_RCTRL), Action::SAVE_STATE), "Ctrl+S saves");
	keys.set_key(Action::LOAD_STATE, 1, { SDLK_S, SDL_KMOD_NONE });
	check(is(keys.key_action(SDLK_S, SDL_KMOD_CTRL), Action::SAVE_STATE), "the key with most of its own held first");
	check(is(keys.key_action(SDLK_S, SDL_KMOD_NONE), Action::LOAD_STATE), "S alone loads");
	keys.set_pad(Action::FULLSCREEN, SDL_GAMEPAD_BUTTON_BACK);
	check(is(keys.pad_action(SDL_GAMEPAD_BUTTON_BACK), Action::FULLSCREEN), "Guide+Back given to the whole display");
	check(keys.of(Action::PAUSE).pad == Hotkeys::NO_PAD, "pause lost it");
	keys.reset();
	check(keys.of(Action::PAUSE) == Hotkeys::standard(Action::PAUSE), "all back as by default");
}

void test_settings()
{
	odin::Settings settings;

	settings.parse("[hotkeys]\npause = Pause, pad:guide+y\nquit = Ctrl+Q, F12\nmenu = what\nsing = F2\n", "test");

	const Hotkeys keys(settings);

	check(is(keys.key_action(SDLK_PAUSE, SDL_KMOD_NONE), Action::PAUSE), "pause on the key Pause");
	check(!keys.key_action(SDLK_P, SDL_KMOD_NONE), "P no more");
	check(is(keys.pad_action(SDL_GAMEPAD_BUTTON_NORTH), Action::PAUSE), "pause on Guide+Y");
	check(!keys.pad_action(SDL_GAMEPAD_BUTTON_BACK), "Guide+Back no more");
	check(is(keys.key_action(SDLK_F12, SDL_KMOD_NONE), Action::QUIT), "quit on F12 too");
	check(keys.of(Action::QUIT).pad == Hotkeys::NO_PAD, "quit has no button: its line gives none");
	check(keys.of(Action::MENU) == Hotkeys::standard(Action::MENU), "a line not right: as by default");
}

} /* namespace */

int main()
{
	test_standard();
	test_text();
	test_changes();
	test_settings();

	std::printf("%d checks, %d failures\n", g_checks, g_failures);
	return g_failures ? 1 : 0;
}
