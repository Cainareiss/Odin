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

/* Unit tests for the keyboard: the keys held, a press shorter than a
 * frame kept until a frame has seen it.
 *
 * Build: g++ -std=c++20 -O2 -Wall -Wextra -Wpedantic -Wconversion -Wshadow \
 *            `pkg-config --cflags sdl3` -I src tests/keyboard/keyboard_tests.cpp \
 *            src/odin/keyboard.cpp -o keyboard_tests
 * Exit status is 0 when every check passes. */

#include "odin/keyboard.hpp"

#include <cstdio>

namespace {

using odin::Keyboard;

int g_checks, g_failures;

void check(bool ok, const char *what)
{
	g_checks++;
	if (!ok) {
		g_failures++;
		std::printf("FAILED: %s\n", what);
	}
}

void test_keys()
{
	Keyboard keyboard;

	check(!keyboard.down(SDLK_A) && keyboard.held().empty(), "no key is down at first");
	keyboard.set(SDLK_A, true);
	keyboard.set(SDLK_A, true);
	check(keyboard.down(SDLK_A) && !keyboard.down(SDLK_S) && keyboard.held().size() == 1,
	      "a key pressed is down, a repeat changes nothing");
	keyboard.settle();
	keyboard.set(SDLK_A, false);
	check(!keyboard.down(SDLK_A), "and up once released, after a frame saw it");
	keyboard.set(SDLK_S, false);
	check(!keyboard.down(SDLK_S), "a release without a press is nothing");
}

void test_short_press()
{
	Keyboard keyboard;

	keyboard.set(SDLK_5, true);
	keyboard.set(SDLK_5, false);
	check(keyboard.down(SDLK_5), "pressed and released before a frame: still held");
	keyboard.settle();
	check(!keyboard.down(SDLK_5), "released once a frame has seen it");
	keyboard.settle();
	check(!keyboard.down(SDLK_5), "and stays released");

	keyboard.set(SDLK_1, true);
	keyboard.set(SDLK_1, false);
	keyboard.set(SDLK_1, true);
	keyboard.settle();
	check(keyboard.down(SDLK_1), "pressed again before the frame: held after it");
	keyboard.set(SDLK_1, false);
	check(!keyboard.down(SDLK_1), "a key held over a frame is released at once");
}

void test_swallow()
{
	Keyboard keyboard;

	keyboard.set(SDLK_RETURN, true);
	keyboard.swallow();
	check(!keyboard.down(SDLK_RETURN), "a key down when swallowed is let go of");
	keyboard.set(SDLK_RETURN, true);
	check(!keyboard.down(SDLK_RETURN), "its repeats are nothing");
	keyboard.set(SDLK_RETURN, false);
	check(!keyboard.down(SDLK_RETURN), "nor is its release");
	keyboard.set(SDLK_RETURN, true);
	check(keyboard.down(SDLK_RETURN), "pressed again, it is down");

	keyboard.set(SDLK_5, true);
	keyboard.set(SDLK_5, false);
	keyboard.swallow();
	check(!keyboard.down(SDLK_5), "a short press is let go of too");
	keyboard.set(SDLK_5, true);
	check(keyboard.down(SDLK_5), "and the next press is not waited for a release");
	keyboard.settle();
	keyboard.set(SDLK_5, false);
	check(!keyboard.down(SDLK_5), "which releases it as before");
}

} /* namespace */

int main()
{
	test_keys();
	test_short_press();
	test_swallow();

	std::printf("%d checks, %d failures\n", g_checks, g_failures);
	return g_failures ? 1 : 0;
}
