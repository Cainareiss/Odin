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

/* The turns of the main loop of the drive board counted without being
 * run (drive_838_13366.cpp, MARKDOWN/94 § D), on a program written here
 * the way epr-20512 works: its CTC set for an NMI every 16000 clocks, a
 * main loop that calls two routines (the returns written on the stack)
 * and reads its RAM, an NMI that reads the command and leaves it for the
 * loop, which answers it and gives it as the torque. A board that counts
 * its waits and one that runs every turn are given the same calls of
 * every length: their states must stay the same after each, and the
 * waits must be counted. Loops that use a port, read R, count in their
 * RAM or write more than the board keeps must not be counted.
 *
 * Build: g++ -std=c++20 -O2 -Wall -Wextra -Wpedantic -Wconversion -Wshadow \
 *            -I src tests/drive_board/drive_wait_test.cpp src/odin/drive_838_13366.cpp src/odin/z80.cpp \
 *            src/odin/z80_ops.cpp -o drive_wait_test
 * Run: drive_wait_test. Exit status is 0 when every check passes. */

#include "odin/drive_838_13366.hpp"

#include <algorithm>
#include <cstdio>
#include <initializer_list>
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

void put(std::vector<uint8_t> &rom, uint16_t at, std::initializer_list<uint8_t> bytes)
{
	for (uint8_t b : bytes)
		rom[at++] = b;
}

enum class Loop { CLEAN, PORT, OUT, REFRESH, RARE, COUNTER, DEEP };

std::vector<uint8_t> program(Loop loop)
{
	std::vector<uint8_t> rom(Drive838_13366::ROM_SIZE, 0xFF);

	/* DI, LD SP,0000, LD A,R once, the CTC as epr-20512 sets it (01F7),
	 * JP 0100 */
	put(rom, 0x0000, {0xF3, 0x31, 0x00, 0x00, 0xED, 0x5F, 0x3E, 0x07, 0xD3, 0x10, 0x3E, 0xFA, 0xD3, 0x10,
	                  0x3E, 0x57, 0xD3, 0x11, 0x3E, 0x04, 0xD3, 0x11, 0xC3, 0x00, 0x01});
	/* NMI: E001 + 1 into E001 and E010, the command into E020 */
	put(rom, 0x0066, {0xF5, 0x3A, 0x01, 0xE0, 0x3C, 0x32, 0x01, 0xE0, 0x32, 0x10, 0xE0,
	                  0xDB, 0x28, 0x32, 0x20, 0xE0, 0xF1, 0xED, 0x45});
	/* CALL 0200, CALL 0210, LD A,(E010), OR A, JP Z,0100; then the
	 * command as the reply and the torque, E010 cleared, JP 0100 */
	put(rom, 0x0100, {0xCD, 0x00, 0x02, 0xCD, 0x10, 0x02, 0x3A, 0x10, 0xE0, 0xB7, 0xCA, 0x00, 0x01,
	                  0x3A, 0x20, 0xE0, 0xD3, 0x29, 0x3E, 0xFE, 0xD3, 0x2E, 0x3A, 0x20, 0xE0, 0xD3, 0x2A,
	                  0xAF, 0x32, 0x10, 0xE0, 0xC3, 0x00, 0x01});
	switch (loop) {
	case Loop::CLEAN:	/* LD HL,E000, LD A,(HL), RET */
	case Loop::DEEP:
		put(rom, 0x0200, {0x21, 0x00, 0xE0, 0x7E, 0xC9});
		break;
	case Loop::PORT:	/* IN A,(21), RET */
		put(rom, 0x0200, {0xDB, 0x21, 0xC9});
		break;
	case Loop::OUT:		/* LD A,0F, OUT (2D),A (the lamps), RET */
		put(rom, 0x0200, {0x3E, 0x0F, 0xD3, 0x2D, 0xC9});
		break;
	case Loop::REFRESH:	/* LD A,R, RET */
		put(rom, 0x0200, {0xED, 0x5F, 0xC9});
		break;
	case Loop::RARE:	/* LD A,R, AND 7E, CP 7E, JR NZ,+3, LD (E030),A, LD A,0,
				 * RET: the same turn but once in a while */
		put(rom, 0x0200, {0xED, 0x5F, 0xE6, 0x7E, 0xFE, 0x7E, 0x20, 0x03, 0x32, 0x30, 0xE0, 0x3E, 0x00, 0xC9});
		break;
	case Loop::COUNTER:	/* LD HL,E000, INC (HL), RET */
		put(rom, 0x0200, {0x21, 0x00, 0xE0, 0x34, 0xC9});
		break;
	}
	if (loop == Loop::DEEP)	/* PUSH BC four times, POP BC four times, RET: 10 bytes */
		put(rom, 0x0210, {0xC5, 0xC5, 0xC5, 0xC5, 0xC1, 0xC1, 0xC1, 0xC1, 0xC9});
	else			/* PUSH BC, POP BC, RET */
		put(rom, 0x0210, {0xC5, 0xC1, 0xC9});
	return rom;
}

uint32_t g_seed = 12345;

uint32_t next(uint32_t most)
{
	g_seed = g_seed * 1103515245u + 12345u;
	return (g_seed >> 8) % most;
}

/* A board that counts its waits and one that runs every turn from the
 * same state, given the same calls for that many clocks; true when their
 * states were the same after every call */
bool same_run(const std::vector<uint8_t> &rom, uint64_t clocks, Drive838_13366 &counted)
{
	Drive838_13366 every(rom);
	Drive838_13366::State start;
	uint8_t command = 0x40, wheel = 0x80;
	bool same = true;

	counted.save(start);
	every.load(start);
	every.set_waits_counted(false);
	while (counted.clocks() < clocks && same) {
		const uint64_t length = 1 + next(next(8) == 0 ? 40000 : 600);

		if (next(50) == 0)
			command = static_cast<uint8_t>(next(256));
		if (next(20) == 0)
			wheel = static_cast<uint8_t>(next(256));
		counted.run_until(counted.clocks() + length, command, wheel);
		every.run_until(every.clocks() + length, command, wheel);

		Drive838_13366::State a, b;

		counted.save(a);
		every.save(b);
		same = a == b && counted.reply() == every.reply() && counted.torque() == every.torque();
	}
	return same;
}

} /* namespace */

int main()
{
	const uint64_t second = Drive838_13366::CLOCK_HZ;

	{
		const std::vector<uint8_t> rom = program(Loop::CLEAN);
		Drive838_13366 board(rom);

		check(same_run(rom, 2 * second, board), "a loop that waits: the same states as a board that runs every turn");
		check(board.waits_counted() > second, "and more than half of its clocks counted without being run");
		check(board.reply() != 0xFF, "its NMI answered");

		/* A state taken while it waits, loaded into another board: both
		 * go on the same */
		Drive838_13366::State mid;
		Drive838_13366 again(rom);

		board.save(mid);
		again.load(mid);
		check(same_run(rom, 3 * second, again), "a state loaded: the same states again");

		/* A state with its NMI just raised, loaded into a board that
		 * waits: it is taken, not counted away */
		Drive838_13366::State raised;

		again.save(mid);
		again.run_until(mid.next_nmi, 0x23, 0x80);
		again.save(raised);
		check(raised.cpu.nmi_pending != 0, "(the NMI raised in that state)");
		board.run_until(board.clocks() + second / 10, 0x40, 0x80);
		board.load(raised);
		check(same_run(rom, 4 * second, board), "loaded into a board that waits: the same states");
	}
	{
		/* Calls of any length end where one call to the same clock does,
		 * its CTC set during the first */
		const std::vector<uint8_t> rom = program(Loop::CLEAN);
		Drive838_13366 one(rom), many(rom);
		Drive838_13366::State a, b;

		one.run_until(2 * second, 0x40, 0x80);
		while (many.clocks() < 2 * second)
			many.run_until(std::min<uint64_t>(many.clocks() + 1 + next(3000), 2 * second), 0x40, 0x80);
		one.save(a);
		many.save(b);
		check(a == b, "calls of any length: the state of one call to the same clock");
		check(a.ram[1] != 0, "(its NMI taken in the first call)");
	}
	{
		/* Reset while it waits, or its waits run then counted again */
		const std::vector<uint8_t> rom = program(Loop::CLEAN);
		Drive838_13366 board(rom);

		board.run_until(second, 0x40, 0x80);
		board.reset();
		check(same_run(rom, second, board), "reset while it waits: the same states");
		board.set_waits_counted(false);
		board.run_until(board.clocks() + second / 3, 0x55, 0x80);
		board.set_waits_counted(true);
		check(same_run(rom, 3 * second, board), "its waits run a while, then counted again: the same states");
		board.set_waits_counted(false);
		board.run_until(board.clocks() + 300, 0x55, 0x80);
		board.set_waits_counted(true);
		check(same_run(rom, 4 * second, board), "and for less than a turn: the same states");
	}
	for (Loop loop : {Loop::PORT, Loop::OUT, Loop::REFRESH, Loop::RARE, Loop::COUNTER, Loop::DEEP}) {
		const std::vector<uint8_t> rom = program(loop);
		Drive838_13366 board(rom);
		const char *const what[] = {"", "a loop that reads a port", "a loop that writes a port", "a loop that reads R",
		                            "a loop that reads R and once in a while does more",
		                            "a loop that counts in its RAM", "a loop that writes 10 bytes"};

		check(same_run(rom, second, board), what[static_cast<int>(loop)]);
		check(board.waits_counted() == 0, "is never counted");
	}

	std::printf("%d checks, %d failures\n", g_checks, g_failures);
	return g_failures ? 1 : 0;
}
