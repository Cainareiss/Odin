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
 * must end as one that runs every turn, also when the chip uses the wave
 * memory: each case runs the same small program on two boards, one of each
 * kind, and compares the memory, the ARM7 and the output.
 *
 * Build: g++ -std=c++20 -Wall -Wextra -Wpedantic -Wconversion -Wshadow \
 *            -O2 -I src tests/sound_board/sound_board_tests.cpp src/odin/sound_board.cpp \
 *            src/odin/aica.cpp src/odin/aica_voice.cpp src/odin/aica_dsp.cpp src/odin/arm7.cpp \
 *            src/odin/arm7_ops.cpp -o sound_board_tests
 */

#include "odin/sound_board.hpp"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <initializer_list>
#include <memory>
#include <span>
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

/* The wave memory tells its watcher of a watched page before it reads or
 * writes there, and of no other */
void
test_wave_memory()
{
	struct Settler : odin::WaveMemory::Watcher {
		std::vector<uint8_t> &ram;
		uint32_t reads = 0, writes = 0;
		uint8_t found = 0;

		explicit Settler(std::vector<uint8_t> &memory) : ram(memory) {}
		/* As settle() would: what a counted turn left is put there */
		void reading(uint32_t addr, uint32_t) override
		{
			reads++;
			ram[addr] = 0x5A;
		}
		void writing(uint32_t addr, uint32_t) override
		{
			writes++;
			found = ram[addr];
		}
	};
	std::vector<uint8_t> ram(0x10000);
	Settler settler(ram);
	/* Pages of 4 KB: page 1 watched for the reads, page 2 for the writes */
	const uint8_t reads[2] = { 0x02, 0 }, writes[2] = { 0x04, 0 };
	const odin::WaveMemory memory(ram.data(), 0xFFFF, { &settler, reads, writes });

	ram[0x1010] = 0x11;
	check(memory.read8(0x1010) == 0x5A && settler.reads == 1, "wave memory: a watched read is told first");
	ram[0x1021] = 0x11;
	check(memory.read16(0x1020) == 0x115A && settler.reads == 2, "wave memory: a watched word is told first");
	ram[0x3010] = 0x11;
	ram[0x3011] = 0x22;
	check(memory.read8(0x3010) == 0x11 && memory.read16(0x3011) == 0x2211 && settler.reads == 2,
	      "wave memory: other reads are not told; a word at an odd address is the even one");
	ram[0x2010] = 0x22;
	memory.write16(0x2011, 0x3344);
	check(settler.writes == 1 && settler.found == 0x22 && ram[0x2010] == 0x44 && ram[0x2011] == 0x33,
	      "wave memory: a watched write is told first");
	memory.write16(0x1010, 0x3344);
	check(settler.writes == 1, "wave memory: another write is not told");
}

/* The ARM7 waits for a word the DSP writes: 0x1234 for one sample period
 * in 128, 0 at the others. */
void
test_dsp_writes()
{
	Board boards[2] = { Board(true), Board(false) };

	for (Board &b : boards) {
		/*	mov r0, #0x10000	the word
		 *	mov r2, #0x20000	FLAG
		 * 1:	ldr r1, [r0]
		 *	cmp r1, #0
		 *	beq 1b
		 *	str r1, [r2]
		 *	b .				*/
		b.program({ 0xE3A00801, 0xE3A02802, 0xE5901000, 0xE3510000, 0x0AFFFFFC, 0xE5821000, 0xEAFFFFFE });
		/* Ring buffer at 0x10000 (RBP 0x20). Step 0, empty, takes
		 * TEMP[MDEC] into ACC; step 1 writes it (MWT, a table, no
		 * float) at MADRS 0. TEMP 64 only is 0x1234 << 8: MDEC comes
		 * to 64 at the 64th period. */
		b.reg(0x2804, 0x0020);
		b.reg(0x4000 + 64 * 8 + 4, 0x1234);
		b.reg(0x3410 + 8, 0xC000);
		b.reg(0x3410 + 12, 0x8000);
		b.start();
		b.run(300);
	}
	check(boards[1].word(FLAG) == 0x1234, "dsp writes: every turn run, the ARM7 sees the word");
	check(boards[0].word(FLAG) == 0x1234, "dsp writes: turns counted, the ARM7 sees the word");
	compare(boards[0], boards[1], "dsp writes");
}

/* The ARM7 counts in a word that a voice plays as its sound: a counted
 * turn leaves its count there only when it is settled. */
void
test_voice_reads()
{
	Board boards[2] = { Board(true), Board(false) };

	for (Board &b : boards) {
		/*	mov r0, #0x10000	the count
		 * 1:	ldr r1, [r0]
		 *	add r1, r1, #1
		 *	str r1, [r0]
		 *	mov r1, #0
		 *	b 1b				*/
		b.program({ 0xE3A00801, 0xE5901000, 0xE2811001, 0xE5801000, 0xE3A01000, 0xEAFFFFFA });
		b.start();
		b.run(20);
		/* Slot 9 plays 16-bit samples from 0x10000 in a loop of two,
		 * full level, centred */
		const uint32_t slot = 9 * odin::Aica::SLOT_SIZE;
		b.reg(slot + 0x04, 0x0000);
		b.reg(slot + 0x0C, 2);
		b.reg(slot + 0x10, 0x001F);
		b.reg(slot + 0x14, 0x3C00);
		b.reg(slot + 0x24, 0x0F00);
		b.reg(slot + 0x28, 0x0060);
		b.reg(slot, 0xC201);
		b.run(300);
	}
	check(boards[1].board->samples() != std::vector<int16_t>(boards[1].board->samples().size()),
	      "voice reads: every turn run, the voice sounds");
	compare(boards[0], boards[1], "voice reads");
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

/* The ARM7 writes a byte again and again with the value already there;
 * the main processor writes another one there: the next turn writes it
 * back. */
void
test_host_writes()
{
	Board boards[2] = { Board(true), Board(false) };
	uint32_t seen[2];

	for (uint32_t k = 0; k < 2; k++) {
		Board &b = boards[k];
		/*	mov r0, #0x10000
		 *	mov r1, #0x55
		 * 1:	strb r1, [r0]
		 *	b 1b				*/
		b.program({ 0xE3A00801, 0xE3A01055, 0xE5C01000, 0xEAFFFFFD });
		b.start();
		/* Long enough for the turn to be counted */
		b.run(100);
		b.board->host_write(RAM + 0x10000, 1, 0x77);
		b.run(2);
		/* As the main processor reads it, before a state is saved */
		seen[k] = b.word(0x10000) & 0xFF;
	}
	check(seen[1] == 0x55, "host writes: every turn run, the ARM7 writes its byte back");
	check(seen[0] == seen[1], "host writes: turns counted, the same byte read");
	compare(boards[0], boards[1], "host writes");
}

/* The main processor reaches the wave memory a span at a time: the ARM7
 * is told as by an access there, so a counted turn ends as it would. */
void
test_host_spans()
{
	/* The ARM7 waits for a word the main processor writes: by a span
	 * (turns counted), word by word (turns counted, every turn run). */
	Board writes[3] = { Board(true), Board(true), Board(false) };

	for (uint32_t k = 0; k < 3; k++) {
		Board &b = writes[k];
		/* As in test_dsp_writes */
		b.program({ 0xE3A00801, 0xE3A02802, 0xE5901000, 0xE3510000, 0x0AFFFFFC, 0xE5821000, 0xEAFFFFFE });
		b.start();
		b.run(100);
		if (k == 0) {
			const std::span<uint8_t> span = b.board->host_ram_write(RAM + 0x10000);
			const uint32_t word = 0x1234;

			check(span.size() == 0x1000 && span.data() == &b.ram[0x10000], "host spans: to the end of the page");
			std::memcpy(span.data(), &word, 4);
		} else {
			b.board->host_write(RAM + 0x10000, 4, 0x1234);
		}
		b.run(10);
	}
	check(writes[0].word(FLAG) == 0x1234, "host spans: a write told, the ARM7 sees the word");
	compare(writes[0], writes[1], "host spans, a write against word by word");
	compare(writes[0], writes[2], "host spans, a write against every turn run");

	/* The ARM7 counts in a word the main processor reads */
	Board reads[2] = { Board(true), Board(false) };
	uint32_t seen[2];

	for (uint32_t k = 0; k < 2; k++) {
		Board &b = reads[k];
		/* As in test_voice_reads; long enough for the turn to be
		 * counted */
		b.program({ 0xE3A00801, 0xE5901000, 0xE2811001, 0xE5801000, 0xE3A01000, 0xEAFFFFFA });
		b.start();
		b.run(100);
		if (k == 0) {
			const std::span<const uint8_t> span = b.board->host_ram_read(RAM + 0x10000);

			std::memcpy(&seen[k], span.data(), 4);
		} else {
			seen[k] = b.word(0x10000);
		}
	}
	check(seen[1] != 0 && seen[0] == seen[1], "host spans: a read told, the count as every turn run");
	compare(reads[0], reads[1], "host spans, a read");

	/* Where a span is: the end of a page, the memory of 2 MB seen four
	 * times, none below the wave memory */
	Board b(false);
	b.reg(0x2800, 0x000F);
	const std::span<const uint8_t> rest = b.board->host_ram_read(RAM + 0x10FF4);
	check(rest.size() == 12 && rest.data() == &b.ram[0x10FF4], "host spans: the rest of the page");
	const std::span<uint8_t> mirror = b.board->host_ram_write(RAM + 0x610020);
	check(mirror.size() == 0xFE0 && mirror.data() == &b.ram[0x10020], "host spans: the memory of 2 MB, a mirror");
	check(b.board->host_ram_read(RAM + 0x7FF000).data() == &b.ram[0x1FF000], "host spans: a mirror to read");
	check(b.board->host_ram_read(REGS).empty() && b.board->host_ram_write(RAM - 4).empty(),
	      "host spans: none below the wave memory");
}

/* The ARM7 counts in a word the DSP reads and sends out */
void
test_dsp_reads()
{
	Board boards[2] = { Board(true), Board(false) };

	for (Board &b : boards) {
		/* The count at 0x10000, as in test_voice_reads */
		b.program({ 0xE3A00801, 0xE5901000, 0xE2811001, 0xE5801000, 0xE3A01000, 0xEAFFFFFA });
		/* Ring buffer at 0x10000. Step 1 reads MADRS 0 (MRD, a table,
		 * no float), step 3 puts it in MEMS 0, step 4 takes it times
		 * COEF 4 (almost one), step 5 to EFREG 0, sent out by EFSDL 0. */
		b.reg(0x2804, 0x0020);
		b.reg(0x3010, 0x7FF8);
		b.reg(0x3410 + 8, 0xA000);
		b.reg(0x3410 + 12, 0x8000);
		b.reg(0x3430 + 4, 0x0040);
		b.reg(0x3440 + 4, 0xA000);
		b.reg(0x3440 + 8, 0x0002);
		b.reg(0x3450 + 8, 0x1000);
		b.reg(0x2000, 0x0F00);
		b.start();
		b.run(300);
	}
	check(boards[1].board->samples() != std::vector<int16_t>(boards[1].board->samples().size()),
	      "dsp reads: every turn run, the effect sounds");
	compare(boards[0], boards[1], "dsp reads");
}

/* The ARM7 runs 512 cycles a sample period: what its last instruction runs
 * past the end of a period comes off the next one */
void
test_arm_clock()
{
	/* 5 cycles a turn, counted in r0:
	 *	mov r0, #0
	 * 1:	add r0, r0, #1		1
	 *	mov r1, r1		1
	 *	b 1b			3	*/
	Board loop(false);
	loop.program({ 0xE3A00000, 0xE2800001, 0xE1A01001, 0xEAFFFFFC });
	loop.start();
	loop.run(1000);
	odin::SoundBoard::State state;
	loop.board->save(state);
	check(std::llabs(static_cast<int64_t>(state.arm.r[0]) - 1000 * 512 / 5) <= 1, "arm clock: 512 cycles a period");

	/* Saved halfway, with cycles run past a period, and loaded: the same */
	Board half(false), rest(false);
	half.program({ 0xE3A00000, 0xE2800001, 0xE1A01001, 0xEAFFFFFC });
	half.start();
	half.run(502);
	odin::SoundBoard::State middle, end;
	half.board->save(middle);
	std::copy(half.ram.begin(), half.ram.end(), rest.ram.begin());
	rest.board->load(middle);
	rest.run(498);
	rest.board->save(end);
	check(middle.arm_ahead != 0 && end.arm == state.arm, "arm clock: a state keeps the cycles run past a period");

	/* Held in reset with cycles run past a period, then let go: as a new start */
	Board again(false), fresh(false);
	for (Board *b : { &again, &fresh }) {
		b->program({ 0xE3A00000, 0xE2800001, 0xE1A01001, 0xEAFFFFFC });
		b->start();
	}
	again.run(502);
	again.reg(0x2C00, 1);
	again.run(3);
	again.reg(0x2C00, 0);
	again.run(100);
	fresh.run(100);
	odin::SoundBoard::State after_reset, new_start;
	again.board->save(after_reset);
	fresh.board->save(new_start);
	check(after_reset.arm == new_start.arm, "arm clock: an ARM7 let go of its reset starts a period anew");

	/* 10 cycles a turn, counted in the memory, the turns counted or run:
	 *	mov r6, #0x10000
	 * 1:	ldr r5, [r6]		3
	 *	add r5, r5, #1		1
	 *	str r5, [r6]		2
	 *	mov r5, #0		1
	 *	b 1b			3	*/
	Board boards[2] = { Board(true), Board(false) };
	for (Board &b : boards) {
		b.program({ 0xE3A06801, 0xE5965000, 0xE2855001, 0xE5865000, 0xE3A05000, 0xEAFFFFFA });
		b.start();
		b.run(1000);
	}
	check(std::llabs(static_cast<int64_t>(boards[0].word(0x10000)) - 1000 * 512 / 10) <= 1,
	      "arm clock: 512 cycles a period, the turns counted");
	compare(boards[0], boards[1], "arm clock");
}

} /* namespace */

int
main()
{
	test_wave_memory();
	test_dsp_writes();
	test_voice_reads();
	test_monitor_reads();
	test_dsp_reads();
	test_host_writes();
	test_host_spans();
	test_arm_clock();
	if (failures) {
		printf("%u failure(s)\n", failures);
		return EXIT_FAILURE;
	}
	printf("sound board: all checks passed\n");
	return EXIT_SUCCESS;
}
