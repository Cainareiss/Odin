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

/* Unit tests for the settings of a linked cabinet: the CRC of the BOOTROM,
 * COMMUNICATION MODE in the 93C46 of the MIE (the chip of a new Racer
 * cabinet, then the CRCs its BOOTROM gives as master and as slave), and
 * the block of a game in the backup RAM.
 *
 * Build: g++ -std=c++20 -O2 -Wall -Wextra -Wpedantic -Wconversion -Wshadow -I src \
 *            tests/cabinet_settings/cabinet_settings_tests.cpp src/odin/common/cabinet_settings.cpp \
 *            -o cabinet_settings_tests
 * Exit status is 0 when every check passes. */

#include "odin/common/cabinet_settings.hpp"

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace {

namespace cabinet = odin::cabinet;

int g_checks, g_failures;

void check(bool ok, const char *what)
{
	g_checks++;
	if (!ok) {
		g_failures++;
		std::printf("FAILED: %s\n", what);
	}
}

std::vector<uint8_t> bytes_of(const char *hex)
{
	std::vector<uint8_t> out;

	for (size_t i = 0; hex[i] && hex[i + 1]; i += 2)
		out.push_back(static_cast<uint8_t>(std::stoul(std::string(hex + i, 2), nullptr, 16)));
	return out;
}

/* "mie_eeprom" of swracer in odin-games.json */
const char *const RACER = "4242502d02010100180004010101010010000000000000000000000006001b17000006080500ffffff1fffffffffffffffff"
                          "ffffffffffffffffffffffff18e84242502d02010100180004010101010010000000000000000000000006001b1700000608"
                          "0500ffffff1fffffffffffffffffffffffffffffffffffffffff18e8";

void test_crc()
{
	const char *const nine = "123456789";

	check(cabinet::crc({}) == 0xB1A0, "the CRC of nothing");
	check(cabinet::crc(std::span(reinterpret_cast<const uint8_t *>(nine), 9)) == 0x5953, "the CRC of 123456789");

	const std::vector<uint8_t> chip = bytes_of(RACER);

	check(chip.size() == 128, "a chip of 128 bytes");
	check(cabinet::crc(std::span(chip).first(cabinet::MIE_SETTINGS)) == (chip[62] | chip[63] << 8),
	      "the CRC of a new cabinet's chip, as SEGA wrote it");
}

void test_mie()
{
	std::vector<uint8_t> chip = bytes_of(RACER);

	check(cabinet::set_mie(chip, cabinet::COMMUNICATION_MODE, 0, 8, cabinet::MASTER) && chip[9] == 1 &&
	      chip[73] == 1 && chip[62] == 0x6B && chip[63] == 0xAC && chip[126] == 0x6B && chip[127] == 0xAC,
	      "master: both copies and their CRC");
	check(cabinet::set_mie(chip, cabinet::COMMUNICATION_MODE, 0, 8, cabinet::SLAVE) && chip[9] == 2 &&
	      chip[62] == 0xFE && chip[63] == 0x60 && chip[126] == 0xFE && chip[127] == 0x60,
	      "slave: both copies and their CRC");

	const std::vector<uint8_t> before = chip;

	check(!cabinet::set_mie(chip, 62, 0, 8, 0) && chip == before, "the CRC itself is not a setting");
	check(!cabinet::set_mie(std::span(chip).first(64), 9, 0, 8, 1), "not a chip of 128 bytes");
	check(cabinet::set_mie(chip, 0x1E, 0, 8, 2) && chip[0x1E] == 2 && chip[64 + 0x1E] == 2 &&
	      cabinet::crc(std::span(chip).subspan(64, 62)) == (chip[126] | chip[127] << 8),
	      "another byte, CRC right in the second copy");
}

std::vector<uint8_t> backup(size_t size)
{
	std::vector<uint8_t> bram(0x10000, 0);

	std::memcpy(&bram[cabinet::BACKUP_HEAD], "BBP-", 4);
	for (size_t i = 0; i < size; i++)
		bram[cabinet::BACKUP_BLOCK + i] = bram[cabinet::BACKUP_BLOCK + size + i] = static_cast<uint8_t>(i * 7);

	const uint16_t sum = cabinet::crc(std::span(bram).subspan(cabinet::BACKUP_BLOCK, size));

	bram[cabinet::BACKUP_HEAD + 4] = static_cast<uint8_t>(sum);
	bram[cabinet::BACKUP_HEAD + 5] = static_cast<uint8_t>(sum >> 8);
	bram[cabinet::BACKUP_HEAD + 6] = static_cast<uint8_t>(size);
	bram[cabinet::BACKUP_HEAD + 7] = static_cast<uint8_t>(size >> 8);
	return bram;
}

void test_backup()
{
	std::vector<uint8_t> bram = backup(0xAA8);

	check(cabinet::backup_block(bram) == 0xAA8, "a block whose CRC is right");
	check(cabinet::backup_at_rest(bram), "its copy the same");
	bram[cabinet::BACKUP_BLOCK + 0xAA8 + 0x100] ^= 1;
	check(cabinet::backup_block(bram) == 0xAA8 && !cabinet::backup_at_rest(bram), "its copy not written yet");
	bram[cabinet::BACKUP_BLOCK + 0xAA8 + 0x100] ^= 1;
	/* Racer: SEAT NUMBER in bits 0-1 of byte 7 (0C14FACC), seat 2 */
	const uint8_t was = bram[cabinet::BACKUP_BLOCK + 7];

	check(cabinet::set_backup(bram, 7, 0, 2, 1), "a seat set");
	check(bram[cabinet::BACKUP_BLOCK + 7] == ((was & 0xFC) | 1) && bram[cabinet::BACKUP_BLOCK + 0xAA8 + 7] == bram[cabinet::BACKUP_BLOCK + 7],
	      "in both copies, the other bits kept");
	check(cabinet::backup_block(bram) == 0xAA8, "its CRC right again");
	/* NASCAR: SEAT NUMBER in bits 2-5 of byte 0x14E3 (0C01A984), seat 3 */
	std::vector<uint8_t> nascar = backup(0x14E4);

	nascar[cabinet::BACKUP_BLOCK + 0x14E3] = nascar[cabinet::BACKUP_BLOCK + 2 * 0x14E4 - 1] = 0x80;
	{
		const uint16_t sum = cabinet::crc(std::span(nascar).subspan(cabinet::BACKUP_BLOCK, 0x14E4));
		nascar[cabinet::BACKUP_HEAD + 4] = static_cast<uint8_t>(sum);
		nascar[cabinet::BACKUP_HEAD + 5] = static_cast<uint8_t>(sum >> 8);
	}
	check(cabinet::set_backup(nascar, 0x14E3, 2, 4, 3) && nascar[cabinet::BACKUP_BLOCK + 0x14E3] == 0x8C &&
	      cabinet::backup_block(nascar) == 0x14E4, "NASCAR's seat 3: 0x84, 0x88, 0x8C as its menu writes it");

	std::vector<uint8_t> empty(0x10000, 0);

	check(cabinet::backup_block(empty) == 0 && !cabinet::set_backup(empty, 7, 0, 2, 1), "no block before the game made one");
	bram[cabinet::BACKUP_BLOCK + 3] ^= 1;
	check(cabinet::backup_block(bram) == 0 && !cabinet::set_backup(bram, 7, 0, 2, 1), "nothing on a block whose CRC is wrong");
	bram = backup(0xAA8);
	check(!cabinet::set_backup(bram, 0xAA8, 0, 2, 1), "nothing out of the block");
}

} /* namespace */

int main()
{
	test_crc();
	test_mie();
	test_backup();
	std::printf("%d checks, %d failures\n", g_checks, g_failures);
	return g_failures ? 1 : 0;
}
