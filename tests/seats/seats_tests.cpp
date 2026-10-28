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

/* Unit tests for the seats of the gamepads: which pad each player has.
 *
 * Build: g++ -std=c++20 -O2 -Wall -Wextra -Wpedantic -Wconversion -Wshadow \
 *            `pkg-config --cflags sdl3` -I src tests/seats/seats_tests.cpp -o seats_tests
 * Exit status is 0 when every check passes. */

#include "odin/host/input/seats.hpp"

#include <cstdio>

namespace {

using odin::OpenPad;
using odin::seat_pads;
using Seats = std::array<SDL_JoystickID, 2>;

int g_checks, g_failures;

void check(bool ok, const char *what)
{
	g_checks++;
	if (!ok) {
		g_failures++;
		std::printf("FAILED: %s\n", what);
	}
}

const std::array<std::string, 2> ANY = { "", "" };
const Seats NONE{};

void test_plug_order()
{
	check(seat_pads({}, ANY, NONE, {}) == Seats{ 0, 0 }, "no pad: no seat");
	check(seat_pads({ { 7, "ps4" } }, ANY, NONE, {}) == Seats{ 7, 0 }, "one pad: player 1");
	check(seat_pads({ { 7, "ps4" }, { 9, "xbox" } }, ANY, NONE, {}) == Seats{ 7, 9 }, "two: in the order they came");
	check(seat_pads({ { 7, "ps4" }, { 9, "xbox" }, { 11, "ds3" } }, ANY, NONE, {}) == Seats{ 7, 9 },
	      "a third one: no seat");
	check(seat_pads({ { 9, "xbox" } }, ANY, NONE, { 7, 9 }) == Seats{ 0, 9 },
	      "the pad of player 1 gone: player 2 keeps theirs");
	check(seat_pads({ { 9, "xbox" }, { 12, "ds3" } }, ANY, NONE, { 0, 9 }) == Seats{ 12, 9 },
	      "a new one takes the free seat");
	check(seat_pads({ { 9, "xbox" }, { 11, "ds3" } }, ANY, NONE, { 7, 9 }) == Seats{ 11, 9 },
	      "one that waited takes the seat freed");
}

void test_wanted()
{
	check(seat_pads({ { 7, "ps4" }, { 9, "xbox" } }, { "xbox", "" }, NONE, { 7, 9 }) == Seats{ 9, 7 },
	      "player 1 wants the Xbox pad: the two swap");
	check(seat_pads({ { 7, "ps4" } }, { "xbox", "" }, NONE, {}) == Seats{ 7, 0 },
	      "the pad wanted is not there: the first free one meanwhile");
	check(seat_pads({ { 7, "ps4" }, { 9, "ds3" }, { 11, "xbox" } }, { "xbox", "" }, NONE, { 7, 9 }) == Seats{ 11, 9 },
	      "it comes: player 1 takes it, player 2 keeps theirs, the other waits");
	check(seat_pads({ { 7, "ps4" }, { 9, "ps4" } }, { "ps4", "ps4" }, NONE, {}) == Seats{ 7, 9 },
	      "two of the same make: one each");
	check(seat_pads({ { 7, "ps4" }, { 9, "ps4" } }, { "ps4", "" }, NONE, { 9, 7 }) == Seats{ 9, 7 },
	      "player 1 keeps the one of that kind they have");
	check(seat_pads({ { 7, "ps4" }, { 9, "xbox" } }, { "", "ps4" }, NONE, {}) == Seats{ 9, 7 },
	      "player 2 wants the first one: player 1 gets the other");
}

/* Two of a make share their GUID: the very one picked in the menu */
void test_picked()
{
	check(seat_pads({ { 7, "ps4" }, { 9, "ps4" } }, { "ps4", "" }, { 9, 0 }, { 7, 0 }) == Seats{ 9, 7 },
	      "player 1 picks the second of two alike: they get it, player 2 the first");
	check(seat_pads({ { 7, "ps4" }, { 9, "ps4" } }, { "ps4", "ps4" }, { 0, 7 }, { 7, 9 }) == Seats{ 9, 7 },
	      "player 2 picks the one of player 1: the two swap");
	check(seat_pads({ { 7, "ps4" }, { 11, "ps4" } }, { "ps4", "" }, { 9, 0 }, { 9, 7 }) == Seats{ 7, 11 },
	      "the one picked is gone: the first of that make meanwhile");
	check(seat_pads({ { 7, "ps4" }, { 9, "ps4" }, { 11, "xbox" } }, { "ps4", "xbox" }, { 9, 0 }, { 7, 11 }) ==
	              Seats{ 9, 11 },
	      "player 2 keeps theirs when player 1 picks the other of their make");
}

} /* namespace */

int main()
{
	test_plug_order();
	test_wanted();
	test_picked();

	std::printf("%d checks, %d failures\n", g_checks, g_failures);
	return g_failures ? 1 : 0;
}
