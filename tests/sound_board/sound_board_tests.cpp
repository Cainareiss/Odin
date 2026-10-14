/*
 * Odin
 * Copyright (C) 2026 Réda Chérif-Touil
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 * 02110-1301, USA.
 */

/* A sound board with the turns of its ARM7 counted (SoundBoard default)
 * must end as one that runs every turn: each case runs the same small
 * program on two boards, one of each kind, and compares the memory, the
 * ARM7 and the output.
 *
 * Build: g++ -std=c++20 -Wall -Wextra -Wpedantic -Wconversion -Wshadow \
 *            -O2 -I src tests/sound_board/sound_board_tests.cpp src/odin/sound_board.cpp \
 *            src/odin/aica.cpp src/odin/aica_voice.cpp src/odin/aica_dsp.cpp src/odin/arm7.cpp \
 *            src/odin/arm7_ops.cpp -o sound_board_tests
 */

#include "odin/sound_board.hpp"

#include <cstdio>
#include <cstdlib>
#include <initializer_list>
#include <memory>
#include <vector>

namespace {

uint32_t failures = 0;

void
check(bool ok, const char *what)
{
	if (!ok) {
		failures++;
		printf("FAIL: %s\n", what);
	}
}

constexpr uint32_t REGS = 0x700000;	/* the board as the main processor sees it */
constexpr uint32_t RAM = 0x800000;
constexpr uint32_t FLAG = 0x20000;	/* where the programs below say they are done */

/* A board run one sample period at a time */
struct Board {
	std::vector<uint8_t> ram = std::vector<uint8_t>(odin::Aica::RAM_SIZE);
	std::unique_ptr<odin::SoundBoard> board;

	explicit Board(bool counted)
	{
		board = std::make_unique<odin::SoundBoard>(ram, odin::Aica::SAMPLE_RATE, [](bool) {});
		board->reset();
		board->set_turn_replay(counted);
		reg(0x2800, 0x020F);
	}
	void reg(uint32_t offset, uint32_t value) { board->host_write(REGS + offset, 2, value); }
	void program(std::initializer_list<uint32_t> words)
	{
		uint32_t at = 0;
		for (uint32_t w : words) {
			board->host_write(RAM + at, 4, w);
			at += 4;
		}
	}
	void start() { reg(0x2C00, 0); }
	void run(uint32_t samples)
	{
		for (uint32_t i = 0; i < samples; i++)
			board->run(1);
	}
	uint32_t word(uint32_t addr)
	{
		uint64_t v = 0;
		board->host_read(RAM + addr, 4, v);
		return static_cast<uint32_t>(v);
	}
};

/* Both boards end the same: the memory, the ARM7, the chip, the output */
void
compare(Board &counted, Board &run, const char *what)
{
	odin::SoundBoard::State a, b;
	counted.board->save(a);
	run.board->save(b);
	char line[160];
	std::snprintf(line, sizeof(line), "%s: the same memory", what);
	check(counted.ram == run.ram, line);
	std::snprintf(line, sizeof(line), "%s: the same ARM7", what);
	check(a.arm == b.arm, line);
	std::snprintf(line, sizeof(line), "%s: the same output", what);
	check(counted.board->samples() == run.board->samples(), line);
}

/* The ARM7 waits for LP in 0x2810, which a read clears: whether a counted
 * turn may go on must not be asked by reading it. */
void
test_monitor_reads()
{
	Board boards[2] = { Board(true), Board(false) };

	for (Board &b : boards) {
		/*	mov r3, #0x800000
		 *	add r3, r3, #0x2800
		 *	add r3, r3, #0x10	0x2810
		 *	mov r2, #0x20000	FLAG
		 * 1:	ldr r1, [r3]
		 *	tst r1, #0x8000		LP
		 *	beq 1b
		 *	str r1, [r2]
		 *	b .				*/
		b.program({ 0xE3A03502, 0xE2833C28, 0xE2833010, 0xE3A02802, 0xE5931000, 0xE3110902, 0x0AFFFFFC,
		            0xE5821000, 0xEAFFFFFE });
		b.reg(0x280C, 9 << 8);
		b.start();
		b.run(20);
		/* Slot 9 loops over 64 samples at 0x1000 */
		const uint32_t slot = 9 * odin::Aica::SLOT_SIZE;
		b.reg(slot + 0x04, 0x1000);
		b.reg(slot + 0x0C, 64);
		b.reg(slot + 0x10, 0x001F);
		b.reg(slot + 0x14, 0x3C00);
		b.reg(slot + 0x24, 0x0F00);
		b.reg(slot + 0x28, 0x0060);
		b.reg(slot, 0xC200);
		b.run(300);
	}
	check((boards[1].word(FLAG) & 0x8000) != 0, "monitor reads: every turn run, the ARM7 sees LP");
	check((boards[0].word(FLAG) & 0x8000) != 0, "monitor reads: turns counted, the ARM7 sees LP");
	compare(boards[0], boards[1], "monitor reads");
}

} /* namespace */

int
main()
{
	test_monitor_reads();
	if (failures) {
		printf("%u failure(s)\n", failures);
		return EXIT_FAILURE;
	}
	printf("sound board: all checks passed\n");
	return EXIT_SUCCESS;
}
