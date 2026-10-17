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

/* The command of the drive board read on the outputs of the I/O board
 * (the bytes NASCAR wrote in Odin, 17/10/2026), and how hard its forces
 * shake a pad.
 *
 * Build: g++ -std=c++20 -Wall -Wextra -Wpedantic -Wconversion -Wshadow \
 *            -O2 -I src tests/drive_board/drive_board_tests.cpp -o drive_board_tests
 */

#include "odin/drive_board.hpp"

#include <cstdio>
#include <cstdlib>
#include <vector>

namespace {

uint32_t failures = 0;
uint32_t checks = 0;

void
check(bool ok, const char *what)
{
	checks++;
	if (!ok) {
		failures++;
		printf("FAILED: %s\n", what);
	}
}

uint8_t
command(std::vector<uint8_t> outputs)
{
	return odin::DriveBoard::command(outputs, 15);
}

} /* namespace */

int
main()
{
	using odin::DriveBoard;

	check(command({ 0x00, 0x07, 0xFC }) == 0x00, "power-on: all eight outputs set, command 00");
	check(command({ 0x00, 0x05, 0xF0 }) == 0xC1 && command({ 0x00, 0x04, 0x70 }) == 0xC7, "the start: C1, then C7");
	check(command({ 0x00, 0x07, 0xF4 }) == 0x40, "at rest: 40");
	check(command({ 0x10, 0x61, 0xF4 }) == 0x41 && command({ 0x10, 0x63, 0x7C }) == 0x04,
	      "a race: 41 and 04, whatever the other outputs");
	check(command({ 0x00, 0x01, 0xFC }) == 0x01 && command({ 0x00, 0x03, 0xF8 }) == 0x80,
	      "TX0 on output 15, TX7 on output 22, low when set");
	check(DriveBoard::command(std::vector<uint8_t>{ 0xFF }, 15) == 0xFF, "outputs the board does not have: none set");
	check(DriveBoard::command(std::vector<uint8_t>{ 0x7F, 0x80 }, 2) == 0x00, "TX0 elsewhere");

	check(DriveBoard::shake_of(0x00) == 0 && DriveBoard::shake_of(0x40) == 0, "no force, no shake, either way");
	check(DriveBoard::shake_of(0x3F) == 0xFFFF && DriveBoard::shake_of(0x7F) == 0xFFFF,
	      "the strongest shakes all, either way");
	check(DriveBoard::shake_of(0x38) == 58253 && DriveBoard::shake_of(0x41) == 1040, "in between, as large as the force");
	check(DriveBoard::shake_of(0x80) == 0 && DriveBoard::shake_of(0x81) == 0 && DriveBoard::shake_of(0xBF) == 0 &&
	      DriveBoard::shake_of(0xC7) == 0 && DriveBoard::shake_of(0xFF) == 0, "tests, starts and pings shake nothing");

	printf("%u checks, %u failures\n", checks, failures);
	return failures ? EXIT_FAILURE : EXIT_SUCCESS;
}
