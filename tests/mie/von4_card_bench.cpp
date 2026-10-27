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

/* The card of a new pilot of Virtual-On (von4_card.cpp) from the program of
 * the game: the keys of its terminal, the names it refuses, and cards equal
 * to those of the game (MARKDOWN/107 § 17): the one its terminal wrote for
 * the pilot "1" (FNV-1a of its 216 bytes), and those its encoder (0C18516C,
 * run alone on the SH-4 of Odin) makes of the data of a new pilot for three
 * other names.
 *
 * Not run with the unit tests: it needs the EPROMs.
 * Build: g++ -std=c++20 -O2 -Wall -Wextra -Wpedantic -Wconversion -Wshadow \
 *            -I src tests/mie/von4_card_bench.cpp src/odin/machine/chips/von4_card.cpp src/odin/machine/chips/card_reader.cpp \
 *            src/odin/machine/chips/mie.cpp src/odin/machine/cpu/z80/z80.cpp src/odin/machine/cpu/z80/z80_ops.cpp src/odin/machine/cabinet/jvs.cpp src/odin/host/app/user_files.cpp \
 *            -o von4_card_bench
 * Run: von4_card_bench <epr23766e.ic29> <epr23767e.ic30>. Exit status is 0
 * when every check passes. */

#include "odin/machine/chips/von4_card.hpp"

#include <cstdio>
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

std::vector<uint8_t> read_file(const char *path)
{
	std::vector<uint8_t> bytes;

	if (std::FILE *const f = std::fopen(path, "rb")) {
		int c;

		while ((c = std::fgetc(f)) != EOF)
			bytes.push_back(static_cast<uint8_t>(c));
		std::fclose(f);
	}
	return bytes;
}

uint64_t fnv(std::span<const uint8_t> bytes)
{
	uint64_t h = 0xCBF29CE484222325u;

	for (const uint8_t b : bytes)
		h = (h ^ b) * 0x100000001B3u;
	return h;
}

bool made(const odin::Von4Card &maker, const char *name, uint64_t expected)
{
	const auto card = maker.make(name);

	return card && odin::CardReader::whole_card(*card) && fnv(*card) == expected;
}

} /* namespace */

int main(int argc, char **argv)
{
	if (argc < 3) {
		std::printf("von4_card_bench <epr23766e.ic29> <epr23767e.ic30>\n");
		return 2;
	}

	/* The two EPROMs as the bus sees them: two bytes of each in turn */
	const std::vector<uint8_t> even = read_file(argv[1]), odd = read_file(argv[2]);
	std::vector<uint8_t> program;

	for (size_t i = 0; i + 1 < even.size() && i + 1 < odd.size(); i += 2)
		program.insert(program.end(), { even[i], even[i + 1], odd[i], odd[i + 1] });

	const odin::Von4Card maker(program);

	check(maker.valid(), "the program has the tables");
	check(maker.keys() == "1234567890@,.ABCDEFGHIJ/~_:;KLMNOPQRST^!?<>UVWXYZ+-*=%'\"()",
	      "the keys of the terminal, its arrows aside");
	check(made(maker, "1", 0x2EC4D25BA8B4C7B8u), "the card its terminal wrote for 1");
	check(made(maker, "ODIN", 0x8F382E1EAC9EAAF8u), "the card of ODIN as its encoder makes it");
	check(made(maker, "R2 D2", 0x0BC2BDD6E608D270u), "the card of R2 D2 as its encoder makes it");
	check(made(maker, "A(1)=%?", 0x17CAA90316AEC772u), "the card of A(1)=%? as its encoder makes it");
	for (const char *name : { "", "        ", "odin", "A{", "123456789", "\xC3\x89T\xC3\x89" })
		check(!maker.make(name), "a name the terminal does not take: refused");
	check(!odin::Von4Card(std::vector<uint8_t>(program.size(), 0)).valid(), "a program without the tables");
	check(!odin::Von4Card(std::span<const uint8_t>(program.data(), 0x100000)).valid(), "a program too short");
	std::printf("%d checks, %d failed\n", g_checks, g_failures);
	return g_failures ? 1 : 0;
}
