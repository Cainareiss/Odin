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

/* The program of the drive board of NASCAR (epr-20512, in the srally2 set
 * of MAME) run by the board of Odin (drive_838_13366.cpp), as its code
 * reads (MARKDOWN/94): its replies over its start (FF, CF for 8 s, CE, CD,
 * CC for 1 s each, then its state), the game mode C7 and its state EF, the
 * torque of each command 80 + n to the left and 80 - n to the right, a
 * command taken only once stable, the position of the wheel as a reply
 * (D5), its wheel centred by the motor at its start, and its waits
 * counted without being run.
 *
 * Not run with the unit tests: it needs the EPROM.
 * Build: g++ -std=c++20 -O2 -Wall -Wextra -Wpedantic -Wconversion -Wshadow \
 *            -I src tests/drive_board/epr20512_bench.cpp src/odin/machine/cabinet/drive_838_13366.cpp src/odin/machine/cpu/z80/z80.cpp \
 *            src/odin/machine/cpu/z80/z80_ops.cpp -o epr20512_bench
 * Run: epr20512_bench <epr-20512.bin>. Exit status is 0 when every check
 * passes. */

#include "odin/machine/cabinet/drive_838_13366.hpp"

#include <algorithm>
#include <cstdio>
#include <vector>

namespace {

using odin::Drive838_13366;

int g_checks, g_failures;

void check(bool ok, const char *what)
{
	g_checks++;
	if (!ok) {
		g_failures++;
		std::printf("FAILED: %s\n", what);
	}
}

constexpr uint64_t MS = Drive838_13366::CLOCK_HZ / 1000;

/* The board run that many ms more with that command and wheel */
void run(Drive838_13366 &board, uint64_t ms, uint8_t command, uint8_t wheel = 0x80)
{
	board.run_until(board.clocks() + ms * MS, command, wheel);
}

/* When the reply first takes that value from now on, in ms (0 if never
 * within `most` ms) */
uint64_t until_reply(Drive838_13366 &board, uint8_t reply, uint64_t most, uint8_t command, uint8_t wheel = 0x80)
{
	for (uint64_t ms = 1; ms <= most; ms++) {
		run(board, 1, command, wheel);
		if (board.reply() == reply)
			return ms;
	}
	return 0;
}

} /* namespace */

int main(int argc, char **argv)
{
	if (argc < 2) {
		std::printf("usage: epr20512_bench <epr-20512.bin>\n");
		return 2;
	}

	std::vector<uint8_t> rom(Drive838_13366::ROM_SIZE);
	std::FILE *const file = std::fopen(argv[1], "rb");

	if (!file || std::fread(rom.data(), 1, rom.size(), file) != rom.size()) {
		std::printf("cannot read %s\n", argv[1]);
		return 2;
	}
	std::fclose(file);

	Drive838_13366 board(rom);

	check(board.reply() == 0xFF, "FF before it starts");
	check(until_reply(board, 0xCF, 1000, 0xFF) != 0, "CF within a second");
	{
		const uint64_t cf = until_reply(board, 0xCE, 9000, 0xFF);

		check(cf >= 7900 && cf <= 8100, "CF for 8 s: the motor board found");
	}
	check(until_reply(board, 0xCD, 1100, 0xFF) >= 900, "CE for 1 s");
	check(until_reply(board, 0xCC, 1100, 0xFF) >= 900, "CD for 1 s, the wheel already centred");
	check(until_reply(board, 0xFF, 1100, 0xFF) >= 900, "CC for 1 s, then its state");
	check(board.torque() == 0, "no torque at rest");

	run(board, 20, 0xC7);
	check(board.reply() == 0xEF, "C7: the game mode, its state EF");
	run(board, 2, 0x20);
	check(board.torque() != -0x20, "a command not yet stable for 4 NMI is not taken");
	run(board, 20, 0x20);
	check(board.torque() == -0x20, "20: 0x20 to the left");
	run(board, 20, 0x60);
	check(board.torque() == 0x20, "60: 0x20 to the right");
	run(board, 20, 0x3F);
	check(board.torque() == -0x3F, "3F: all to the left");
	run(board, 20, 0x7F);
	check(board.torque() == 0x3F, "7F: all to the right");
	run(board, 20, 0x40);
	check(board.torque() == 0, "40: none");
	run(board, 20, 0xD5, 0x93);
	check(board.reply() == 0x93, "D5: the position of the wheel as its reply");
	run(board, 20, 0xD0, 0x93);
	check(board.reply() == 0xEF, "D0: its state again");
	run(board, 20, 0xC1);
	run(board, 20, 0x30);
	check(board.torque() == 0, "C1: at rest, a torque command does nothing");

	/* Started again with the wheel off its centre: 1 s after CD (the
	 * game may give the centre), the motor pushes it to the right while it
	 * is left of the centre */
	Drive838_13366 again(rom);

	until_reply(again, 0xCD, 11000, 0xFF, 0x60);
	run(again, 900, 0xFF, 0x60);
	check(again.torque() == 0, "CD: nothing for a second");
	run(again, 200, 0xFF, 0x60);
	check(again.torque() == 0x80 - 0x6C && again.reply() == 0xCD, "then the wheel left of its centre pushed right (6C)");
	check(until_reply(again, 0xCC, 100, 0xFF, 0x80) != 0, "the centre reached: CC");
	run(again, 5, 0xFF, 0x80);
	check(again.torque() == 0, "and the push stops");

	/* A wheel held off its centre: three tries of 5 s, then error 22 (E9),
	 * the board stopped, its motor at rest */
	Drive838_13366 held(rom);

	until_reply(held, 0xCD, 11000, 0xFF, 0x60);
	check(until_reply(held, 0xE9, 20000, 0xFF, 0x60) > 15000 && held.torque() == 0,
	      "a wheel held off its centre: E9 after three tries of 5 s, no torque");

	/* Its main loop counted without being run while it waits (MARKDOWN/94
	 * § D): a board that runs every turn, given the same calls of every
	 * length and the commands of a race, has the same state after each */
	{
		Drive838_13366 counted(rom), every(rom);
		uint32_t seed = 2026;
		uint8_t command = 0xFF, wheel = 0x80;
		bool same = true;
		const auto next = [&seed](uint32_t most) {
			seed = seed * 1103515245u + 12345u;
			return (seed >> 8) % most;
		};

		every.set_waits_counted(false);
		while (counted.clocks() < 40 * Drive838_13366::CLOCK_HZ && same) {
			const uint64_t length = 1 + next(next(16) == 0 ? 80000 : 600);
			const uint64_t ms = counted.clocks() / MS;

			if (ms < 12000)
				command = 0xFF;
			else if (ms < 13000)
				command = 0xC1;
			else if (ms < 14000)
				command = 0xC7;
			else if (next(400) == 0)
				command = static_cast<uint8_t>(next(4) == 0 ? 0xD5 : next(0x80));
			if (next(30) == 0)
				wheel = static_cast<uint8_t>(std::clamp<int>(wheel + static_cast<int>(next(41)) - 20, 0, 255));
			counted.run_until(counted.clocks() + length, command, wheel);
			every.run_until(every.clocks() + length, command, wheel);

			Drive838_13366::State a, b;

			counted.save(a);
			every.save(b);
			same = a == b;
		}
		check(same, "its waits counted: the same states as a board that runs every turn, 40 s");
		std::printf("waits counted: %.1f %% of its clocks\n",
		            100.0 * static_cast<double>(counted.waits_counted()) / static_cast<double>(counted.clocks()));
	}

	std::printf("%d checks, %d failures\n", g_checks, g_failures);
	return g_failures ? 1 : 0;
}
