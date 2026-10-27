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
 * Then the 837-13741, when its EPROM is given: epr-21868 starts its reply
 * 108.5 us after the request (tests/jvs/epr21868_bench.cpp), 84.6 turns,
 * as its imitation; a millisecond after its reset it still tests its
 * memory and says nothing. A state saved in the middle goes on the same.
 *
 * Not run with the unit tests: it needs the PROM (and the EPROM).
 * Build: g++ -std=c++20 -O2 -Wall -Wextra -Wpedantic -Wconversion -Wshadow \
 *            -I src tests/jvs/line_bench.cpp src/odin/machine/boards/hikaru/mie_board.cpp src/odin/machine/chips/mie.cpp \
 *            src/odin/machine/cpu/z80/z80.cpp src/odin/machine/cpu/z80/z80_ops.cpp src/odin/machine/chips/eeprom_93c46.cpp src/odin/machine/chips/card_reader.cpp \
 *            src/odin/machine/cabinet/jvs.cpp src/odin/machine/cabinet/io_837_13551_imitation.cpp src/odin/machine/cabinet/io_837_13741_imitation.cpp \
 *            src/odin/machine/cabinet/io_837_13551.cpp src/odin/machine/chips/tmp90ph44.cpp src/odin/machine/cpu/tlcs90/tlcs90.cpp src/odin/machine/cabinet/io_837_13741.cpp \
 *            src/odin/machine/chips/d71054.cpp src/odin/host/app/user_files.cpp -o line_bench
 * Run: line_bench <315-6215.bin>. Exit status 0 when every check passes. */

#include "odin/machine/boards/hikaru/mie_board.hpp"

#include <algorithm>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <memory>
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
 * for the request to fall elsewhere in the runs of the chip. `boot`: the
 * clocks the board runs before. `save_at`: the clocks after the host lets
 * the program go at which the board is saved, then loaded into another
 * that goes on in its place (0: none). */
Reply ask(std::span<const uint8_t> prom, uint8_t lead, uint32_t piece, uint32_t call = 100000,
          const char *board = "837-13551", uint32_t boot = odin::Mie::CLOCK_HZ, uint32_t save_at = 0)
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
	odin::MieBoard first(rom, ram, odin::Mie::CLOCK_HZ, nullptr, board, prom);
	first.reset();
	first.set_line_piece(piece);

	/* The board starts: its memory test, then its loop */
	first.run(boot);
	first.host_write(0x20000 + 2 * 0xC001, 1, lead);
	first.host_write(0x20000 + 2 * 0xC000, 1, 1);
	std::vector<uint8_t> other_ram;
	std::unique_ptr<odin::MieBoard> other;
	odin::MieBoard *mie = &first;
	if (save_at) {
		auto state = std::make_unique<odin::MieBoard::State>();
		first.run(save_at);
		first.save(*state);
		other_ram = ram;
		other = std::make_unique<odin::MieBoard>(rom, other_ram, odin::Mie::CLOCK_HZ, nullptr, board, prom);
		other->set_line_piece(piece);
		check(other->load(*state), "the state loads");
		mie = other.get();
	}
	/* In one call longer than the chip runs ahead, or in calls of `call` clocks */
	for (uint32_t done = save_at; done < 100000; done += call)
		mie->run(call);
	Reply reply{};
	uint32_t low = 0, high = 0, v = 0;
	mie->host_read(0x20000 + 2 * 0xC100, 1, low);
	mie->host_read(0x20000 + 2 * 0xC101, 1, high);
	reply.turns = (high << 8) | low;
	for (uint32_t i = 0; i < 6; i++) {
		mie->host_read(0x20000 + 2 * (0xC102 + i), 1, v);
		reply.bytes.push_back(static_cast<uint8_t>(v));
	}
	mie->host_read(0x20000 + 2 * 0xC108, 1, v);
	reply.sense = static_cast<uint8_t>(v);
	return reply;
}

} /* namespace */

int main(int argc, char **argv)
{
	if (argc < 2) {
		std::fprintf(stderr, "usage: line_bench <315-6215.bin> [<epr-21868.ic7>]\n");
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
	if (argc > 2) {
		std::ifstream other(argv[2], std::ios::binary);
		const std::vector<uint8_t> eprom((std::istreambuf_iterator<char>(other)), std::istreambuf_iterator<char>());

		for (uint8_t lead : { uint8_t(0), uint8_t(19), uint8_t(41), uint8_t(63) }) {
			const char *board = "837-13741";
			const Reply chip = ask(eprom, lead, 1024, 100000, board), fine = ask(eprom, lead, 16, 100000, board);
			const Reply calls = ask(eprom, lead, 1024, 333, board), imitation = ask({}, lead, 1024, 100000, board);
			const Reply loaded = ask(eprom, lead, 1024, 100000, board, odin::Mie::CLOCK_HZ, 9000u + 300u * lead);
			const Reply early = ask(eprom, lead, 1024, 100000, board, 16000), ready = ask({}, lead, 1024, 100000, board, 16000);

			std::printf("the 837-13741: %u turns (%u with the chip 1 us ahead), the imitation: %u\n", chip.turns,
			            fine.turns, imitation.turns);
			check(chip.bytes == f1 && imitation.bytes == f1, "837-13741: both answer E0 00 03 01 01 05");
			check(chip.turns == fine.turns && chip.bytes == fine.bytes, "837-13741: the chip 64 us ahead, the same as 1 us");
			check(chip.turns == calls.turns && chip.bytes == calls.bytes, "837-13741: and in calls of 333 clocks");
			check(chip.turns >= 83 && chip.turns <= 87 && imitation.turns >= 83 && imitation.turns <= 86,
			      "837-13741: 196 us to the first byte, its program's time and the imitation's");
			check(chip.sense == 0x8E && imitation.sense == 0x8E, "837-13741: the sense input: addressed");
			check(loaded.turns == chip.turns && loaded.bytes == chip.bytes && loaded.sense == chip.sense,
			      "837-13741: saved in the middle, it goes on the same");
			check(!early.turns && ready.turns >= 83 && ready.turns <= 86,
			      "837-13741: a millisecond after its reset, its program still tests its memory; the imitation answers");
		}
		std::vector<uint8_t> rom(2048, 0), ram(32768, 0);
		odin::MieBoard racer(rom, ram, odin::Mie::CLOCK_HZ, nullptr, "837-13551", prom);
		odin::MieBoard nascar(rom, ram, odin::Mie::CLOCK_HZ, nullptr, "837-13741", eprom);
		auto state = std::make_unique<odin::MieBoard::State>();
		racer.save(*state);
		check(!nascar.load(*state), "the state of the other board refused");
	}
	std::printf("%d checks, %d failures\n", g_checks, g_failures);
	return g_failures ? 1 : 0;
}
