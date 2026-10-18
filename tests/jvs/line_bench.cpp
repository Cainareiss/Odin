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

/* The MIE and the 837-13551 on the line between them (MARKDOWN/77): a
 * program of the Z80 waits for the host and a few turns more, sends F1 01
 * to every node, waits
 * for the end of its last character, switches the driver off and counts
 * turns of a loop of 37 clocks until a byte is there, reads the six bytes
 * of the reply and the sense input. The chip runs 315-6215, which starts
 * its reply 288.6 us after the request (tests/jvs/s6215_bench.cpp), the
 * byte landing a character (86.8 us) later: 375.4 us, 162.3 turns; up to
 * 13 us later when its tick holds the interrupt of the last byte. The
 * imitation, two characters after the request: 75.1 turns. The chip runs
 * up to 64 us ahead of the MIE, the same as up to 1 us.
 *
 * Not run with the unit tests: it needs the PROM.
 * Build: g++ -std=c++20 -O2 -Wall -Wextra -Wpedantic -Wconversion -Wshadow \
 *            -I src tests/jvs/line_bench.cpp src/odin/mie_board.cpp src/odin/mie.cpp \
 *            src/odin/z80.cpp src/odin/z80_ops.cpp src/odin/eeprom_93c46.cpp src/odin/card_reader.cpp \
 *            src/odin/jvs.cpp src/odin/jvs_837_13551.cpp src/odin/jvs_837_13741.cpp \
 *            src/odin/io_837_13551.cpp src/odin/tmp90ph44.cpp src/odin/tlcs90.cpp -o line_bench
 * Run: line_bench <315-6215.bin>. Exit status 0 when every check passes. */

#include "odin/mie_board.hpp"

#include <algorithm>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <vector>

namespace {

int g_checks, g_failures;

void check(bool ok, const char *what)
{
	g_checks++;
	if (!ok) {
		g_failures++;
		std::printf("FAILED: %s\n", what);
	}
}

struct Reply {
	unsigned turns;
	std::vector<uint8_t> bytes;
	uint8_t sense;
};

/* `lead`: turns of 13 clocks the program waits after the host lets it go,
 * for the request to fall elsewhere in the runs of the chip */
Reply ask(std::span<const uint8_t> prom, uint8_t lead, uint32_t piece, uint32_t call = 100000)
{
	std::vector<uint8_t> rom(2048, 0), ram(32768, 0);
	const std::vector<uint8_t> program = {
		0x3E, 0x80, 0xD3, 0x13, 0x3E, 0x08, 0xD3, 0x10, 0xAF, 0xD3, 0x13,	/* the divisor at 8 */
		0x3A, 0x00, 0xC0, 0xB7, 0x28, 0xFA,					/* ld a, (C000) / or a / jr z */
		0x3A, 0x01, 0xC0, 0xB7, 0x28, 0x03, 0x47, 0x10, 0xFE,			/* ld a, (C001) / or a / jr z / ld b, a / djnz $ */
		0x3E, 0x01, 0xD3, 0x90,							/* the driver on */
		0x3E, 0xE0, 0xD3, 0x10, 0x3E, 0xFF, 0xD3, 0x10, 0x3E, 0x03, 0xD3, 0x10,
		0x3E, 0xF1, 0xD3, 0x10, 0x3E, 0x01, 0xD3, 0x10, 0x3E, 0xF4, 0xD3, 0x10,
		0xDB, 0x15, 0xCB, 0x77, 0x28, 0xFA,					/* in a, (15) / bit 6, a / jr z */
		0xAF, 0xD3, 0x90, 0x21, 0x00, 0x00,					/* the driver off / ld hl, 0 */
		0x23, 0xDB, 0x15, 0xCB, 0x47, 0x28, 0xF9,				/* inc hl / in a, (15) / bit 0, a / jr z */
		0x22, 0x00, 0xC1,							/* ld (C100), hl */
		0x06, 0x06, 0x21, 0x02, 0xC1,						/* ld b, 6 / ld hl, C102 */
		0xDB, 0x15, 0xCB, 0x47, 0x28, 0xFA,					/* in a, (15) / bit 0, a / jr z */
		0xDB, 0x10, 0x77, 0x23, 0x10, 0xF4,					/* in a, (10) / ld (hl), a / inc hl / djnz */
		0xDB, 0x91, 0x32, 0x08, 0xC1, 0x76,					/* in a, (91) / ld (C108), a / halt */
	};
	std::copy(program.begin(), program.end(), rom.begin());
	odin::MieBoard mie(rom, ram, odin::Mie::CLOCK_HZ, nullptr, "837-13551", prom);
	mie.reset();
	mie.set_line_piece(piece);

	/* The board starts: its memory test, then its loop */
	mie.run(odin::Mie::CLOCK_HZ);
	mie.host_write(0x20000 + 2 * 0xC001, 1, lead);
	mie.host_write(0x20000 + 2 * 0xC000, 1, 1);
	/* In one call longer than the chip runs ahead, or in calls of `call` clocks */
	for (uint32_t done = 0; done < 100000; done += call)
		mie.run(call);
	Reply reply{};
	uint32_t low = 0, high = 0, v = 0;
	mie.host_read(0x20000 + 2 * 0xC100, 1, low);
	mie.host_read(0x20000 + 2 * 0xC101, 1, high);
	reply.turns = (high << 8) | low;
	for (uint32_t i = 0; i < 6; i++) {
		mie.host_read(0x20000 + 2 * (0xC102 + i), 1, v);
		reply.bytes.push_back(static_cast<uint8_t>(v));
	}
	mie.host_read(0x20000 + 2 * 0xC108, 1, v);
	reply.sense = static_cast<uint8_t>(v);
	return reply;
}

} /* namespace */

int main(int argc, char **argv)
{
	if (argc < 2) {
		std::fprintf(stderr, "usage: line_bench <315-6215.bin>\n");
		return 2;
	}
	std::ifstream file(argv[1], std::ios::binary);
	const std::vector<uint8_t> prom((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());

	const std::vector<uint8_t> f1 = { 0xE0, 0x00, 0x03, 0x01, 0x01, 0x05 };

	for (uint8_t lead : { uint8_t(0), uint8_t(19), uint8_t(41), uint8_t(63) }) {
		const Reply chip = ask(prom, lead, 1024), fine = ask(prom, lead, 16), imitation = ask({}, lead, 1024);
		const Reply calls = ask(prom, lead, 1024, 333);

		std::printf("the chip: %u turns (%u with the chip 1 us ahead), the imitation: %u\n", chip.turns, fine.turns,
		            imitation.turns);
		check(chip.bytes == f1 && imitation.bytes == f1, "both answer E0 00 03 01 01 05");
		check(chip.turns == fine.turns && chip.bytes == fine.bytes, "the chip 64 us ahead: the same as 1 us");
		check(chip.turns == calls.turns && chip.bytes == calls.bytes, "and the same in calls of 333 clocks");
		check(chip.turns >= 161 && chip.turns <= 168, "the chip: 375 us to the first byte, its program's time");
		check(imitation.turns >= 74 && imitation.turns <= 77, "the imitation: two characters");
		check(chip.sense == 0x8E && imitation.sense == 0x8E, "the sense input: addressed");
	}
	std::printf("%d checks, %d failures\n", g_checks, g_failures);
	return g_failures ? 1 : 0;
}
