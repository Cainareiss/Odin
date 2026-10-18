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

/* Unit tests for the ROM board: where the ROMs of each game sit on the bus,
 * empty sockets, the EEPROM page and the page of the guard chips.
 *
 * Build: g++ -std=c++20 -O2 -Wall -Wextra -Wpedantic -Wconversion -Wshadow \
 *            -I src tests/rom_board/rom_board_tests.cpp src/odin/rom_board.cpp \
 *            src/odin/x76f100.cpp src/odin/crypt_315_5881.cpp src/odin/actel_m1.cpp \
 *            -o rom_board_tests
 * Exit status is 0 when every check passes. */

#include "odin/rom_board.hpp"

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace {

using odin::RomBoard;

int g_checks, g_failures;

void check(bool ok, const char *what)
{
	g_checks++;
	if (!ok) {
		g_failures++;
		std::printf("FAILED: %s\n", what);
	}
}

constexpr uint32_t MB = 1024 * 1024;

/* ROMs whose 32-bit words hold their own offset */
std::vector<uint8_t> numbered(uint32_t size)
{
	std::vector<uint8_t> rom(size);

	for (uint32_t offs = 0; offs < size; offs += 4)
		std::memcpy(&rom[offs], &offs, 4);
	return rom;
}

void test_layouts()
{
	const RomBoard::Config airtrix = RomBoard::config_of(4, 0x291B02C7);
	const RomBoard::Config braveff = RomBoard::config_of(2, std::nullopt);
	const RomBoard::Config bootrom = RomBoard::config_of(0, std::nullopt);

	check(airtrix.has_rom && airtrix.eprom_bank[0] == 0x10 && airtrix.eprom_bank[1] == 0x13 &&
	      airtrix.maskrom_bank[0] == 0x20 && airtrix.maskrom_bank[1] == 0x2F && airtrix.eeprom_bank == 0x14,
	      "EPROMs in the pages 10 to 13, mask ROMs in 20 to 2F, the EEPROM in 14");
	check(airtrix.has_5881 && airtrix.crypt_key == 0x291B02C7 && airtrix.eprom_bank_size == 4,
	      "a board with the 315-5881 has its key");
	check(!braveff.has_5881 && braveff.crypt_key == 0 && braveff.eprom_bank_size == 2,
	      "one without it, and EPROMs of 2 MB");
	check(!bootrom.has_rom && !bootrom.has_5881 && bootrom.eeprom_bank == 0x14, "the BOOTROM alone has no ROM board");
	check(sizeof(RomBoard::Config) == 48, "the layout is 48 bytes in the save states");
}

void test_roms()
{
	const std::vector<uint8_t> eprom = numbered(12 * MB), maskrom = numbered(40 * MB);
	std::string told;
	int times = 0;
	RomBoard board(RomBoard::config_of(4, 0x2912C68A), eprom, maskrom, std::nullopt,
	               [&](const char *what) { told = what; times++; });

	check(board.read(0x10000100, 4) == 0x100 && board.read(0x11000100, 4) == 8 * MB + 0x100,
	      "a page of 8 MB for each pair of 4 MB EPROMs");
	check(board.read(0x10800100, 4) == 0x100, "which answers twice in its page");
	check(board.read(0x10000102, 2) == 0 && board.read(0x10000101, 1) == 1, "by 16 or 8 bits too");
	check(board.read(0x12000000, 4) == 0 && times == 1 &&
	      told == "ROMBD unpopulated socket: bank 12 reads as zero (first R32 12000000)",
	      "an empty socket reads zero, and it is told");
	board.read(0x12000004, 4);
	check(times == 1, "once a page");
	check(board.read(0x21000004, 4) == 16 * MB + 4 && board.read(0x22FFFFFC, 4) == 0 && times == 2,
	      "the mask ROMs, 16 MB a page, and an empty socket of theirs");
	check(board.read(0x15000000, 4) == 0xDEADBEEF && board.read(0x15000000, 2) == 0xBEEF &&
	      board.read(0x3F000000, 1) == 0xEF, "a page without a socket reads neither zero nor ones");

	RomBoard::Config saved;
	board.save(saved);
	check(saved.unpopulated_logged == ((1ull << 2) | (1ull << 0x12)), "the sockets told are in the layout saved");

	RomBoard braveff(RomBoard::config_of(2, std::nullopt), eprom, maskrom, std::nullopt, nullptr);
	check(braveff.read(0x11000100, 4) == 4 * MB + 0x100, "a page of 4 MB for a pair of 2 MB EPROMs");

	RomBoard bare(RomBoard::config_of(0, std::nullopt), {}, {}, std::nullopt, nullptr);
	check(bare.read(0x10000000, 4) == 0xDEADBEEF, "without a ROM board nothing reads zero");
}

/* What plain() gives is what read() gives, byte for byte, up to where read()
 * would answer from elsewhere: the next mirror of an EPROM page, the end of
 * a mask ROM page or of the ROM, an empty socket, the EEPROM. */
void test_plain()
{
	const std::vector<uint8_t> eprom = numbered(12 * MB), maskrom = numbered(40 * MB);
	RomBoard board(RomBoard::config_of(4, 0x2912C68A), eprom, maskrom, std::nullopt, nullptr);
	const auto same = [&board](uint32_t at, size_t size) {
		const std::span<const uint8_t> bytes = board.plain(at);
		const uint32_t last = at + static_cast<uint32_t>(bytes.size()) - 1;

		return bytes.size() == size && bytes.front() == board.read(at, 1) && bytes.back() == board.read(last, 1);
	};

	check(same(0x10000100, 8 * MB - 0x100) && same(0x10800100, 8 * MB - 0x100),
	      "an EPROM page up to its end, in each of its two places");
	check(same(0x11000100, 4 * MB - 0x100) && board.plain(0x11400000).empty() && board.plain(0x12000000).empty(),
	      "up to the end of the EPROMs; nothing past it");
	check(same(0x21000004, 16 * MB - 4) && same(0x227FFFFE, 2) && board.plain(0x22800000).empty(),
	      "a mask ROM page up to its end or to the end of the ROMs");
	check(board.plain(0x14000000).empty() && board.plain(0x15000000).empty() && board.plain(0x0C000000).empty(),
	      "nothing for the EEPROM, a page without a socket, a page not of the board");

	RomBoard::Config none = RomBoard::config_of(4, 0x2912C68A);
	none.has_rom = false;
	RomBoard bare(none, eprom, maskrom, std::nullopt, nullptr);
	check(bare.read(0x10000000, 4) == 0xDEADBEEF && bare.plain(0x10000000).empty() && bare.plain(0x20000000).empty(),
	      "nothing without a ROM board, whatever ROMs are given");
}

void test_eeprom_and_guards()
{
	const std::vector<uint8_t> eprom = numbered(8 * MB), maskrom = numbered(32 * MB);
	RomBoard plain(RomBoard::config_of(4, std::nullopt), eprom, maskrom, std::nullopt, nullptr);
	RomBoard nascar(RomBoard::config_of(4, std::nullopt), eprom, maskrom, RomBoard::Actel{ 0x56DEDF33, 0x4252 },
	                nullptr);
	RomBoard airtrix(RomBoard::config_of(4, 0x291B02C7), eprom, maskrom, std::nullopt, nullptr);

	check(plain.write(0x14000000, 4, 0x0D) && !plain.write(0x10000000, 4, 0),
	      "only the page of the EEPROM is written");
	check(plain.read(0x14000000, 4) <= 1, "its page reads the data line");

	/* The probe of the BOOTROM (ROM 0x1F3A): 0x19620217 at +C on a wide
	 * local bus, then at +8 on a narrow one; a 16-bit RAM keeps the second
	 * only, as the EPROMs from IC29 want. */
	plain.write_guard(0xC, 4, 0x19620217, true);
	plain.write_guard(0x8, 4, 0x19620217, false);
	check(plain.read_guard(0xC, 4, true) == 0x0217 && plain.read_guard(0x8, 4, false) == 0x19620217,
	      "a board without a guard chip has its RAM: the probe keeps +8, not +C");
	check(plain.read_guard(0xC, 4, false) == 0x0217 && plain.read_guard(0xA, 2, true) == 0x1962,
	      "a wide 32-bit access reaches the low half alone; 16 bits are the same either way");
	check(!plain.crypt() && !plain.fpga() && plain.local_ram().size() == 0x10000, "and no chip there");

	check(nascar.fpga() && nascar.read_guard(0x8, 4, true) == 0x4252, "SGNASCAR has the FPGA, with its ID at +8");
	nascar.write_guard(0xC, 4, 0x12345678, true);
	check(nascar.read_guard(0xC, 4, true) == 0x12345678 && nascar.read_guard(0xE, 2, false) == 0x1234,
	      "its +0C keeps what is written, all 32 bits");
	check(nascar.local_ram().empty(), "and no RAM");
	nascar.reset();
	check(nascar.read_guard(0xC, 4, true) == 0, "until a reset");

	check(airtrix.crypt() && airtrix.local_ram().size() == 0x10000, "AIRTRIX has the 315-5881 and its 64 KB of RAM");
	airtrix.write_guard(0x100, 4, 0xCAFEF00D, false);
	check(airtrix.read_guard(0x100, 4, false) == 0xCAFEF00D && airtrix.read_guard(0x102, 2, false) == 0xCAFE &&
	      airtrix.local_ram()[0x100] == 0x0D, "the RAM at 0A000000");
	check(airtrix.read_guard(0x10000, 2, false) == 0, "the registers of the chip at 0A010000: ready");
	airtrix.reset();
	check(airtrix.read_guard(0x100, 4, false) == 0, "a reset empties the RAM");

	check(airtrix.guarded(0x100) && airtrix.guarded(0x1000F) && !airtrix.guarded(0x10010),
	      "the chip answers over its RAM and its registers, not above");
	check(nascar.guarded(0x13) && !nascar.guarded(0x14) && plain.guarded(0xFFFF) && !plain.guarded(0x10000),
	      "the FPGA over its 20 bytes; the RAM alone on a board without a chip");
}

} /* namespace */

int main()
{
	test_layouts();
	test_roms();
	test_plain();
	test_eeprom_and_guards();

	std::printf("%d checks, %d failures\n", g_checks, g_failures);
	return g_failures ? 1 : 0;
}
