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

/* Unit tests for the keyboard as the control panel: which key is which
 * switch, and a coin counted once per press.
 *
 * Build: g++ -std=c++20 -O2 -Wall -Wextra -Wpedantic -Wconversion -Wshadow \
 *            `sdl2-config --cflags` -I src tests/keyboard/keyboard_tests.cpp \
 *            src/odin/keyboard.cpp -o keyboard_tests
 * Exit status is 0 when every check passes. */

#include "odin/keyboard.hpp"

#include <cstdio>
#include <initializer_list>

namespace {

using odin::Keyboard;
using odin::MieBoard;

int g_checks, g_failures;

void check(bool ok, const char *what)
{
	g_checks++;
	if (!ok) {
		g_failures++;
		std::printf("FAILED: %s\n", what);
	}
}

/* The inputs with these keys held, and no other */
MieBoard::Inputs held(std::initializer_list<SDL_Keycode> keys)
{
	Keyboard keyboard;
	MieBoard::Inputs inputs = {};

	for (const SDL_Keycode key : keys)
		keyboard.set(key, true);
	keyboard.read_inputs(inputs);
	return inputs;
}

void test_keys()
{
	Keyboard keyboard;

	check(!keyboard.down(SDLK_a), "no key is down at first");
	keyboard.set(SDLK_a, true);
	keyboard.set(SDLK_a, true);
	check(keyboard.down(SDLK_a) && !keyboard.down(SDLK_s), "a key pressed is down, a repeat changes nothing");
	keyboard.set(SDLK_a, false);
	check(!keyboard.down(SDLK_a), "and up once released");
	keyboard.set(SDLK_s, false);
	check(!keyboard.down(SDLK_s), "a release without a press is nothing");
}

void test_switches()
{
	const MieBoard::Inputs none = held({});
	bool centred = true;

	for (const uint16_t channel : none.jvs_analog)
		centred = centred && channel == 0x8000;
	check(!none.mainboard_test && !none.mainboard_service && none.jvs_system == 0 &&
	      none.jvs_player[0] == 0 && none.jvs_player[1] == 0 && !none.jvs_coin[0] && !none.jvs_coin[1],
	      "nothing is pressed without a key");
	check(centred, "the eight analog channels are centred");

	check(held({SDLK_F5}).mainboard_test && held({SDLK_F6}).mainboard_service,
	      "F5 and F6 are Test and Service of the main board");
	check(held({SDLK_F2}).jvs_system == 0x80, "F2 is Test of the I/O board");
	check(held({SDLK_1}).jvs_player[0] == 0x8000 && held({SDLK_9}).jvs_player[0] == 0x4000,
	      "player 1: Start and Service");
	check(held({SDLK_UP}).jvs_player[0] == 0x2000 && held({SDLK_DOWN}).jvs_player[0] == 0x1000 &&
	      held({SDLK_LEFT}).jvs_player[0] == 0x0800 && held({SDLK_RIGHT}).jvs_player[0] == 0x0400,
	      "player 1: the arrows");
	check(held({SDLK_LCTRL}).jvs_player[0] == 0x0200 && held({SDLK_LALT}).jvs_player[0] == 0x0100 &&
	      held({SDLK_LSHIFT}).jvs_player[0] == 0x0080 && held({SDLK_z}).jvs_player[0] == 0x0040 &&
	      held({SDLK_x}).jvs_player[0] == 0x0020 && held({SDLK_c}).jvs_player[0] == 0x0010,
	      "player 1: the six buttons, PUSH3 to PUSH6 in the low byte");
	check(held({SDLK_2}).jvs_player[1] == 0x8000 && held({SDLK_0}).jvs_player[1] == 0x4000 &&
	      held({SDLK_r}).jvs_player[1] == 0x2000 && held({SDLK_f}).jvs_player[1] == 0x1000 &&
	      held({SDLK_d}).jvs_player[1] == 0x0800 && held({SDLK_g}).jvs_player[1] == 0x0400,
	      "player 2: Start, Service and R F D G");
	check(held({SDLK_a}).jvs_player[1] == 0x0200 && held({SDLK_s}).jvs_player[1] == 0x0100 &&
	      held({SDLK_q}).jvs_player[1] == 0x0080 && held({SDLK_w}).jvs_player[1] == 0x0040 &&
	      held({SDLK_e}).jvs_player[1] == 0x0020 && held({SDLK_t}).jvs_player[1] == 0x0010,
	      "player 2: the six buttons");
	check(held({SDLK_1, SDLK_UP, SDLK_z}).jvs_player[0] == 0xA040 && held({SDLK_1, SDLK_UP, SDLK_z}).jvs_player[1] == 0,
	      "the keys held together add up, each player its own word");
}

void test_coins()
{
	Keyboard keyboard;
	MieBoard::Inputs inputs = {};

	keyboard.set(SDLK_5, true);
	keyboard.read_inputs(inputs);
	check(inputs.jvs_coin[0] == 1 && inputs.jvs_coin[1] == 0, "5 is a coin in the first slot");
	keyboard.read_inputs(inputs);
	check(inputs.jvs_coin[0] == 0, "counted once while the key is held");
	keyboard.set(SDLK_5, false);
	keyboard.read_inputs(inputs);
	keyboard.set(SDLK_5, true);
	keyboard.set(SDLK_6, true);
	keyboard.read_inputs(inputs);
	check(inputs.jvs_coin[0] == 1 && inputs.jvs_coin[1] == 1, "again at the next press, and 6 is the second slot");
}

} /* namespace */

int main()
{
	test_keys();
	test_switches();
	test_coins();

	std::printf("%d checks, %d failures\n", g_checks, g_failures);
	return g_failures ? 1 : 0;
}
