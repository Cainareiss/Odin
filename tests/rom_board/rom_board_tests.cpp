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
	const auto airtrix = RomBoard::config_of("airtrix");
	const auto braveff = RomBoard::config_of("braveff");
	const auto sgnascar = RomBoard::config_of("sgnascar");
	const auto bootrom = RomBoard::config_of("bootrom");

	check(!RomBoard::config_of("pacman"), "a game that is not known has no layout");
	check(airtrix && airtrix->has_rom && airtrix->eprom_bank[0] == 0x10 && airtrix->eprom_bank[1] == 0x13 &&
	      airtrix->maskrom_bank[0] == 0x20 && airtrix->maskrom_bank[1] == 0x2F && airtrix->eeprom_bank == 0x14,
	      "EPROMs in the pages 10 to 13, mask ROMs in 20 to 2F, the EEPROM in 14");
	check(airtrix && airtrix->has_5881 && airtrix->crypt_key == 0x291B02C7 && airtrix->eprom_bank_size == 4,
	      "AIRTRIX has the 315-5881, with its key");
	check(braveff && !braveff->has_5881 && braveff->eprom_bank_size == 2 && !braveff->maskrom_is_stretched,
	      "BRAVEFF has not, and EPROMs of 2 MB");
	check(sgnascar && sgnascar->maskrom_is_stretched && !sgnascar->has_5881, "SGNASCAR has its mask ROMs stretched");
	check(bootrom && !bootrom->has_rom && bootrom->eeprom_bank == 0x14, "the BOOTROM alone has no ROM board");
	check(sizeof(RomBoard::Config) == 48, "the layout is 48 bytes in the save states");
}

void test_roms()
{
	const std::vector<uint8_t> eprom = numbered(12 * MB), maskrom = numbered(40 * MB);
	std::string told;
	int times = 0;
	RomBoard board(*RomBoard::config_of("pharrier"), eprom, maskrom, false,
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

	RomBoard braveff(*RomBoard::config_of("braveff"), eprom, maskrom, false, nullptr);
	check(braveff.read(0x11000100, 4) == 4 * MB + 0x100, "a page of 4 MB for a pair of 2 MB EPROMs");

	RomBoard bare(*RomBoard::config_of("bootrom"), {}, {}, false, nullptr);
	check(bare.read(0x10000000, 4) == 0xDEADBEEF, "without a ROM board nothing reads zero");
}

void test_eeprom_and_guards()
{
	const std::vector<uint8_t> eprom = numbered(8 * MB), maskrom = numbered(32 * MB);
	RomBoard plain(*RomBoard::config_of("von4"), eprom, maskrom, false, nullptr);
	RomBoard nascar(*RomBoard::config_of("sgnascar"), eprom, maskrom, true, nullptr);
	RomBoard airtrix(*RomBoard::config_of("airtrix"), eprom, maskrom, false, nullptr);

	check(plain.write(0x14000000, 4, 0x0D) && !plain.write(0x10000000, 4, 0),
	      "only the page of the EEPROM is written");
	check(plain.read(0x14000000, 4) <= 1, "its page reads the data line");

	check(plain.read_guard(0x8, 4) == 0x19620217 && plain.read_guard(0xC, 4) == 0 &&
	      plain.read_guard(0x8, 2) == 0x0217, "a board without a guard chip answers the probe at +8");
	check(!plain.crypt() && !plain.fpga() && plain.crypt_ram().empty(), "and has no chip there");

	check(nascar.fpga() && nascar.read_guard(0x8, 4) == 0x4252, "SGNASCAR has the FPGA, with its ID at +8");
	nascar.write_guard(0xC, 4, 0x12345678);
	check(nascar.read_guard(0xC, 4) == 0x12345678 && nascar.read_guard(0xE, 2) == 0x1234,
	      "its +0C keeps what is written");
	nascar.reset();
	check(nascar.read_guard(0xC, 4) == 0, "until a reset");

	check(airtrix.crypt() && airtrix.crypt_ram().size() == 0x10000, "AIRTRIX has the 315-5881 and its 64 KB of RAM");
	airtrix.write_guard(0x100, 4, 0xCAFEF00D);
	check(airtrix.read_guard(0x100, 4) == 0xCAFEF00D && airtrix.read_guard(0x102, 2) == 0xCAFE &&
	      airtrix.crypt_ram()[0x100] == 0x0D, "the RAM at 0A000000");
	check(airtrix.read_guard(0x10000, 2) == 0, "the registers of the chip at 0A010000: ready");
	airtrix.reset();
	check(airtrix.read_guard(0x100, 4) == 0, "a reset empties the RAM");
}

} /* namespace */

int main()
{
	test_layouts();
	test_roms();
	test_eeprom_and_guards();

	std::printf("%d checks, %d failures\n", g_checks, g_failures);
	return g_failures ? 1 : 0;
}
