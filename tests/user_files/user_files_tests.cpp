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

/* Unit tests for the save folder: where it is, what is read back, what is
 * refused, and that a file written takes the place of the old one whole.
 *
 * Build: g++ -std=c++20 -O2 -Wall -Wextra -Wpedantic -Wconversion -Wshadow \
 *            -I src tests/user_files/user_files_tests.cpp src/odin/user_files.cpp \
 *            -o user_files_tests
 * It writes its files in the folder given as its argument, or the current
 * one. Exit status is 0 when every check passes. */

#include "odin/user_files.hpp"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <vector>

namespace {

using odin::SaveFolder;

int g_checks, g_failures;

void check(bool ok, const char *what)
{
	g_checks++;
	if (!ok) {
		g_failures++;
		std::printf("FAILED: %s\n", what);
	}
}

void write_file(const std::string &path, const std::vector<uint8_t> &bytes)
{
	std::FILE *const file = std::fopen(path.c_str(), "wb");

	if (file) {
		std::fwrite(bytes.data(), 1, bytes.size(), file);
		std::fclose(file);
	}
}

std::vector<uint8_t> pattern(size_t size, uint8_t seed)
{
	std::vector<uint8_t> bytes(size);

	for (size_t i = 0; i < size; i++)
		bytes[i] = static_cast<uint8_t>(seed + i * 7);
	return bytes;
}

void test_where()
{
	unsetenv("XDG_DATA_HOME");
	setenv("HOME", "/home/someone", 1);
	setenv("ODIN_SAVES", "/elsewhere", 1);
	check(SaveFolder::of_game("braveff").dir() == "/elsewhere/braveff", "ODIN_SAVES=<folder>: <folder>/<game>");
	setenv("ODIN_SAVES", "none", 1);
	check(SaveFolder::of_game("braveff").none(), "ODIN_SAVES=none: no folder");
	check(SaveFolder::of_game("braveff").path("nvram").empty(), "no folder: no path");
	check(SaveFolder::of_game("braveff", "/chosen").none(), "ODIN_SAVES=none: no folder, whatever the settings say");
	setenv("ODIN_SAVES", "/elsewhere", 1);
	check(SaveFolder::of_game("braveff", "/chosen").dir() == "/elsewhere/braveff", "ODIN_SAVES before the settings");
	check(SaveFolder::root("/chosen") == "/elsewhere", "the folder of the games: ODIN_SAVES");
	unsetenv("ODIN_SAVES");
	check(SaveFolder::of_game("braveff", "/chosen").dir() == "/chosen/braveff", "the settings: <folder>/<game>");
	check(SaveFolder::root("/chosen") == "/chosen", "the folder of the games: the settings");
	check(SaveFolder::root() == "/home/someone/.local/share/odin/saves", "the folder of the games by default");
	check(SaveFolder::of_game("sgnascar").dir() == "/home/someone/.local/share/odin/saves/sgnascar",
	      "default: ~/.local/share/odin/saves/<game>");
	check(SaveFolder::of_game("sgnascar").path("nvram") == "/home/someone/.local/share/odin/saves/sgnascar/nvram",
	      "a file of the folder");
	setenv("XDG_DATA_HOME", "/data", 1);
	check(SaveFolder::of_game("airtrix").dir() == "/data/odin/saves/airtrix", "XDG_DATA_HOME: <data>/odin/saves/<game>");
	setenv("XDG_DATA_HOME", "", 1);
	check(SaveFolder::of_game("airtrix").dir() == "/home/someone/.local/share/odin/saves/airtrix",
	      "an empty XDG_DATA_HOME is not set");
	unsetenv("XDG_DATA_HOME");
	unsetenv("HOME");
	unsetenv("USERPROFILE");
	check(SaveFolder::of_game("airtrix").none(), "no home: no folder");
}

void test_read_write(const std::string &dir)
{
	const SaveFolder saves(dir + "/saves-test/pharrier");
	const std::string path = saves.path("nvram");
	const std::vector<uint8_t> first = pattern(65536, 1), second = pattern(65536, 2);
	std::error_code error;

	std::filesystem::remove_all(dir + "/saves-test", error);
	check(!odin::read_kept(path, 65536), "a missing file is not read");

	check(odin::write_kept(path, first), "written, its folders made");
	const auto read = odin::read_kept(path, 65536);
	check(read && *read == first, "read back as written");
	check(!std::filesystem::exists(path + ".new", error), "no temporary file left");

	check(odin::write_kept(path, second), "written again");
	const auto again = odin::read_kept(path, 65536);
	check(again && *again == second, "the new file takes the place of the old one");

	check(!odin::read_kept(path, 65535), "a longer file is refused");
	check(!odin::read_kept(path, 65537), "a shorter file is refused");
	write_file(path, {});
	check(!odin::read_kept(path, 128), "an empty file is refused");
	write_file(path, pattern(128, 3));
	const auto small = odin::read_kept(path, 128);
	check(small && *small == pattern(128, 3), "a file of 128 bytes");

	/* A folder where the file should be: nothing is written, and what
	 * was there stays. */
	const std::string blocked = saves.path("blocked");
	std::filesystem::create_directories(blocked, error);
	check(!odin::write_kept(blocked, first), "cannot write in place of a folder");
	check(std::filesystem::is_directory(blocked, error), "the folder stays");
	check(!std::filesystem::exists(blocked + ".new", error), "no temporary file left after a failure");

	std::filesystem::remove_all(dir + "/saves-test", error);
}

} /* namespace */

/* The folder of the pictures of the games, and a picture found there by
 * its name in any case */
void
test_artwork(const std::string &dir)
{
	const std::string folder = dir + "/artwork-test";
	std::error_code error;

	setenv("HOME", "/home/someone", 1);
	unsetenv("XDG_DATA_HOME");
	setenv("ODIN_ARTWORK", "none", 1);
	check(odin::artwork_folder("/chosen").empty(), "ODIN_ARTWORK=none: no folder of the artwork");
	setenv("ODIN_ARTWORK", "/elsewhere", 1);
	check(odin::artwork_folder("/chosen") == "/elsewhere", "ODIN_ARTWORK before the settings");
	unsetenv("ODIN_ARTWORK");
	check(odin::artwork_folder("/chosen") == "/chosen", "the folder of the artwork the settings give");
	check(odin::artwork_folder() == "/home/someone/.local/share/odin/artwork", "the folder of the artwork by default");
	unsetenv("HOME");

	std::filesystem::remove_all(folder, error);
	std::filesystem::create_directories(folder, error);
	for (const char *name : { "Swracer_Bezel.png", "airtrix_title.png", "airtrix_logo.txt" })
		if (std::FILE *const file = std::fopen((folder + "/" + name).c_str(), "wb"))
			std::fclose(file);
	check(odin::artwork_file(folder, "airtrix", "title") == folder + "/airtrix_title.png", "a picture by its name");
	check(odin::artwork_file(folder, "swracer", "bezel") == folder + "/Swracer_Bezel.png",
	      "a picture by its name in another case");
	check(odin::artwork_file(folder, "airtrix", "logo").empty(), "no picture but a PNG");
	check(odin::artwork_file(folder, "von4", "title").empty(), "no picture of a game without one");
	check(odin::artwork_file("", "airtrix", "title").empty(), "no picture without a folder");
	std::filesystem::remove_all(folder, error);
}

int main(int argc, char **argv)
{
	const std::string dir = argc > 1 ? argv[1] : ".";

	test_where();
	test_read_write(dir);
	test_artwork(dir);

	std::printf("%d checks, %d failures\n", g_checks, g_failures);
	return g_failures ? 1 : 0;
}
