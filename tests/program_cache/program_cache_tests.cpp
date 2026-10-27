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

/* Unit tests for the files of the programs the driver made: a program is
 * given back only to the driver that made it, for the sources it was made
 * from, from a whole file.
 *
 * Build: g++ -std=c++20 -O2 -Wall -Wextra -Wpedantic -Wconversion -Wshadow \
 *            -I src tests/program_cache/program_cache_tests.cpp src/odin/host/gfx/program_cache.cpp \
 *            src/odin/host/app/user_files.cpp -o program_cache_tests
 * It writes its files in the folder given as its argument, or the current
 * one. Exit status is 0 when every check passes. */

#include "odin/host/gfx/program_cache.hpp"
#include "odin/host/app/user_files.hpp"

#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <string>
#include <vector>

namespace {

using odin::KeptProgram;
using odin::program_file;
using odin::program_from_file;

int g_checks, g_failures;

void check(bool ok, const char *what)
{
	g_checks++;
	if (!ok) {
		g_failures++;
		std::printf("FAILED: %s\n", what);
	}
}

const std::string DRIVER = "Intel\nIntel(R) HD Graphics 520\n4.6.0 - Build 31.0.101.2111\n4.60 - Build 31.0.101.2111\n";
const std::string VERTEX = "#version 140\n#define HAS_TEXTURE 1\nvoid main() {}\n";
const std::string FRAGMENT = "#version 140\n#define HAS_TEXTURE 1\nout vec4 c; void main() { c = vec4(1.0); }\n";

void test_file()
{
	const KeptProgram made{0x8E21, {1, 2, 3, 4, 5, 250, 0, 7}};
	const std::vector<uint8_t> file = program_file(DRIVER, VERTEX, FRAGMENT, made);
	const std::optional<KeptProgram> back = program_from_file(file, DRIVER, VERTEX, FRAGMENT);

	check(back && back->format == made.format && back->binary == made.binary,
	      "a program comes back whole, with its format");
	check(!program_from_file(file, DRIVER + " ", VERTEX, FRAGMENT), "not to another driver");
	check(!program_from_file(file, "", VERTEX, FRAGMENT), "not to a driver that says nothing");
	check(!program_from_file(file, DRIVER, VERTEX + "\n", FRAGMENT), "not for another vertex source");
	check(!program_from_file(file, DRIVER, VERTEX, "x" + FRAGMENT.substr(1)), "not for another fragment source");
	check(!program_from_file(file, DRIVER, FRAGMENT, VERTEX), "not for the two sources the other way");

	bool shorter = false;

	for (size_t size = 0; size < file.size(); size++) {
		const std::vector<uint8_t> cut(file.begin(), file.begin() + static_cast<std::ptrdiff_t>(size));

		shorter = shorter || program_from_file(cut, DRIVER, VERTEX, FRAGMENT).has_value();
	}
	check(!shorter, "nothing from a file cut short, wherever");

	std::vector<uint8_t> longer = file;

	longer.push_back(0);
	check(!program_from_file(longer, DRIVER, VERTEX, FRAGMENT), "nothing from a file with a byte more");

	std::vector<uint8_t> other = file;

	other[0] ^= 1;
	check(!program_from_file(other, DRIVER, VERTEX, FRAGMENT), "nothing from a file of another kind");

	const std::vector<uint8_t> empty = program_file(DRIVER, VERTEX, FRAGMENT, KeptProgram{0x8E21, {}});

	check(!program_from_file(empty, DRIVER, VERTEX, FRAGMENT), "nothing from a file without a program");
}

void test_folder(const std::string &folder)
{
	const std::string path = folder + "/programs-test/0000000130c30d06.bin";
	const KeptProgram made{7, std::vector<uint8_t>(100000, 0x5A)};
	std::error_code error;

	std::filesystem::remove_all(folder + "/programs-test", error);
	check(!odin::read_file(path), "nothing from a file that is not there");
	check(odin::write_kept(path, program_file(DRIVER, VERTEX, FRAGMENT, made)), "a program kept, its folder made");

	const std::optional<std::vector<uint8_t>> file = odin::read_file(path);
	const std::optional<KeptProgram> back = file ? program_from_file(*file, DRIVER, VERTEX, FRAGMENT) : std::nullopt;

	check(back && back->binary == made.binary, "and read back from the file, longer than a read");
	std::filesystem::remove_all(folder + "/programs-test", error);
}

} /* namespace */

int main(int argc, char **argv)
{
	test_file();
	test_folder(argc > 1 ? argv[1] : ".");
	std::printf("%d checks, %d failures\n", g_checks, g_failures);
	return g_failures == 0 ? 0 : 1;
}
