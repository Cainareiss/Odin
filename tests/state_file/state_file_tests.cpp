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

/* Unit tests for the save state file: the first line, the items and their
 * sizes, what is refused.
 *
 * Build: g++ -std=c++20 -O2 -Wall -Wextra -Wpedantic -Wconversion -Wshadow \
 *            -I src tests/state_file/state_file_tests.cpp src/odin/state_file.cpp \
 *            -o state_file_tests
 * It writes its files in the folder given as its argument, or the current
 * one. Exit status is 0 when every check passes. */

#include "odin/state_file.hpp"

#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

namespace {

using odin::StateFile;

int g_checks, g_failures;

void check(bool ok, const char *what)
{
	g_checks++;
	if (!ok) {
		g_failures++;
		std::printf("FAILED: %s\n", what);
	}
}

std::vector<uint8_t> bytes_of(const std::string &path)
{
	std::vector<uint8_t> out;
	std::FILE *file = std::fopen(path.c_str(), "rb");
	int c;

	while (file && (c = std::fgetc(file)) != EOF)
		out.push_back(static_cast<uint8_t>(c));
	if (file)
		std::fclose(file);
	return out;
}

void test_save_and_load(const std::string &dir)
{
	const std::string path = dir + "/state_file_test.vkstate";
	const uint32_t word = 0x12345678;
	const uint8_t five[5] = { 1, 2, 3, 4, 5 };

	{
		StateFile state(path, StateFile::Mode::SAVE);

		check(state.ok() && !state.loading(), "a state to save opens");
		check(state.put(word), "a word is put");
		check(state.put(five, sizeof(five)), "five bytes are put");
		uint32_t back;
		check(!state.get(back), "nothing is read from a state being saved");
	}

	const std::vector<uint8_t> file = bytes_of(path);
	check(file.size() == 32 + 4 + 4 + 4 + 5, "the file: 32 bytes of name, then each item after its size");
	check(std::string(file.begin(), file.begin() + 20) == "odin state 00000007\n" && file[20] == 0 && file[31] == 0,
	      "the name of the format, padded with zeros");
	check(file[32] == 4 && file[33] == 0 && file[36] == 0x78 && file[39] == 0x12,
	      "a size and its item, least significant byte first");
	check(file[40] == 5 && file[44] == 1 && file[48] == 5, "the second item");

	{
		StateFile state(path, StateFile::Mode::LOAD);
		uint32_t back = 0;
		uint16_t half = 0;

		check(state.ok() && state.loading(), "the state opens to be loaded");
		check(state.get(back) && back == word, "the word comes back");
		check(!state.put(word), "nothing is written to a state being loaded");
		check(!state.get(half), "an item is not read into another size");
	}
	{
		StateFile state(path, StateFile::Mode::LOAD);
		uint32_t back = 0;
		uint8_t back5[5] = {};

		check(state.get(back) && state.get(back5, sizeof(back5)) && back5[4] == 5, "both items in their order");
		check(!state.get(back), "nothing after the last item");
	}
	std::remove(path.c_str());
}

void test_refused(const std::string &dir)
{
	const std::string path = dir + "/state_file_test_other.vkstate";

	{
		StateFile missing(dir + "/state_file_test_missing.vkstate", StateFile::Mode::LOAD);
		uint32_t word;

		check(!missing.ok(), "a file that is not there does not open");
		check(!missing.get(word), "and gives nothing");
	}
	{
		std::FILE *file = std::fopen(path.c_str(), "wb");
		char name[32] = "odin state 00000006\n";

		std::fwrite(name, 1, sizeof(name), file);
		std::fclose(file);
		StateFile old(path, StateFile::Mode::LOAD);
		check(!old.ok(), "a state of another version is refused");
	}
	{
		std::FILE *file = std::fopen(path.c_str(), "wb");

		std::fputs("odin", file);
		std::fclose(file);
		StateFile cut(path, StateFile::Mode::LOAD);
		check(!cut.ok(), "a file cut short is refused");
	}
	{
		StateFile nowhere(dir + "/no/such/folder/state.vkstate", StateFile::Mode::SAVE);
		check(!nowhere.ok(), "a state that cannot be created does not open");
	}
	std::remove(path.c_str());
}

} /* namespace */

int main(int argc, char **argv)
{
	const std::string dir = (argc > 1) ? argv[1] : ".";

	test_save_and_load(dir);
	test_refused(dir);

	std::printf("%d checks, %d failures\n", g_checks, g_failures);
	return g_failures ? 1 : 0;
}
