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

/* Unit tests for the list of the games: the three ways a section is made of
 * its files, and what is refused.
 *
 * Build: g++ -std=c++20 -O2 -Wall -Wextra -Wpedantic -Wconversion -Wshadow \
 *            -I src tests/games/games_tests.cpp src/odin/games.cpp -ljansson \
 *            -o games_tests
 * It writes its files in the folder given as its argument, or the current
 * one. Exit status is 0 when every check passes. */

#include "odin/games.hpp"

#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace {

using odin::Game;
using odin::GameList;

int g_checks, g_failures;

void check(bool ok, const char *what)
{
	g_checks++;
	if (!ok) {
		g_failures++;
		std::printf("FAILED: %s\n", what);
	}
}

void write(const std::string &path, const std::string &bytes)
{
	std::ofstream(path, std::ios::binary) << bytes;
}

bool is(std::span<const uint8_t> bytes, const std::string &wanted)
{
	return std::string(bytes.begin(), bytes.end()) == wanted;
}

const char LIST[] = R"({
  "version": 1,
  "roms": [
    { "name": "toy", "mach": "hikaru", "sections": [
        { "name": "bootrom", "type": "alternative", "data": [
            { "name": "missing.bin", "size": 4 }, { "name": "boot.bin", "size": 4 } ] },
        { "name": "eprom", "type": "interleave", "amnt": 2, "data": [
            { "name": "a.bin", "size": 4 }, { "name": "b.bin", "size": 4 },
            { "name": "c.bin", "size": 2 }, { "name": "d.bin", "size": 2 } ] },
        { "name": "bytes", "type": "interleave", "amnt": 1, "data": [
            { "name": "c.bin", "size": 2 }, { "name": "d.bin", "size": 2 } ] },
        { "name": "maskrom", "type": "concatenate", "data": [
            { "name": "c.bin", "size": 2 }, { "name": "a.bin", "size": 4 } ] } ] },
    { "name": "short", "mach": "hikaru", "sections": [
        { "name": "bootrom", "type": "alternative", "data": [ { "name": "boot.bin", "size": 3 } ] } ] },
    { "name": "odd", "mach": "hikaru", "sections": [
        { "name": "bootrom", "type": "shuffle", "data": [ { "name": "boot.bin", "size": 4 } ] } ] },
    { "name": "unequal", "mach": "hikaru", "sections": [
        { "name": "bootrom", "type": "alternative", "data": [
            { "name": "boot.bin", "size": 4 }, { "name": "c.bin", "size": 2 } ] } ] },
    { "name": "stepless", "mach": "hikaru", "sections": [
        { "name": "eprom", "type": "interleave", "data": [
            { "name": "a.bin", "size": 4 }, { "name": "b.bin", "size": 4 } ] } ] },
    { "name": "wheel", "mach": "hikaru", "io": "837-13741", "sections": [
        { "name": "bootrom", "type": "alternative", "data": [ { "name": "boot.bin", "size": 4 } ] } ] }
  ]
})";

void test_games(const std::string &dir)
{
	const std::string roms = dir + "/games_test_roms";

	std::filesystem::create_directories(roms + "/toy");
	for (const char *game : { "short", "odd", "unequal", "stepless", "wheel" })
		std::filesystem::create_directories(roms + "/" + game);
	write(dir + "/games_test.json", LIST);
	for (const char *game : { "toy", "short", "odd", "unequal", "stepless", "wheel" }) {
		const std::string folder = roms + "/" + game;

		write(folder + "/boot.bin", "BOOT");
		write(folder + "/a.bin", "A0A1");
		write(folder + "/b.bin", "B0B1");
		write(folder + "/c.bin", "C0");
		write(folder + "/d.bin", "D0");
	}

	GameList list;

	check(list.load(dir + "/games_test.json"), "the list loads");
	check(list.has("toy") && !list.has("other"), "a game is in the list or not");

	const std::optional<Game> toy = list.load_game(roms, "toy");
	check(toy && toy->name == "toy" && toy->machine == "hikaru", "a game loads with its name and its machine");
	if (toy) {
		check(toy->sections.size() == 4 && toy->sections[0].first == "bootrom" &&
		      toy->sections[3].first == "maskrom", "its sections in the order of the list");
		check(is(toy->section("bootrom"), "BOOT"), "alternative: the first file that is there");
		check(is(toy->section("eprom"), "A0B0A1B1C0D0"),
		      "interleave: two bytes of each file of a pair in turn, the pairs one after the other");
		check(is(toy->section("bytes"), "CD00"), "interleave, a byte at a time");
		check(is(toy->section("maskrom"), "C0A0A1"), "concatenate: one file after the other");
		check(toy->section("eeprom").empty(), "a section the game does not have is empty");
		check(toy->io_board == "837-13551", "without \"io\", the I/O board is the 837-13551");
	}

	const std::optional<Game> wheel = list.load_game(roms, "wheel");
	check(wheel && wheel->io_board == "837-13741", "the I/O board the list tells");

	check(!list.load_game(roms, "other"), "a game that is not in the list does not load");
	check(!list.load_game(roms, "short"), "nor one whose file has another size than told");
	check(!list.load_game(roms, "odd"), "nor one with a section of a type that is not known");
	check(!list.load_game(roms, "unequal"), "nor one whose alternative files have different sizes");
	check(!list.load_game(roms, "stepless"), "nor one that interleaves without telling by how many bytes");
	check(!list.load_game(dir + "/no_such_folder", "toy"), "nor one whose files are not there");
}

void test_lists(const std::string &dir)
{
	GameList list;
	const std::string path = dir + "/games_test_bad.json";

	check(!list.load(dir + "/games_test_missing.json"), "a list that is not there does not load");
	write(path, "{ \"version\": 2, \"roms\": [] }");
	check(!list.load(path), "a list of another version is refused");
	write(path, "{ \"roms\": [] }");
	check(!list.load(path), "a list without a version too");
	write(path, "{ \"version\": 1 }");
	check(!list.load(path), "and one without games");
	write(path, "{ \"version\": 1, \"roms\": [ { \"name\": \"x\" } ] }");
	check(!list.load(path), "and one with a game that has no machine");
	write(path, "not json");
	check(!list.load(path), "and what is not JSON");
	check(!list.has("toy") && !list.load_game(dir, "toy"), "a list that did not load has no game");
}

} /* namespace */

int main(int argc, char **argv)
{
	const std::string dir = (argc > 1) ? argv[1] : ".";

	test_games(dir);
	test_lists(dir);
	std::filesystem::remove_all(dir + "/games_test_roms");
	std::filesystem::remove(dir + "/games_test.json");
	std::filesystem::remove(dir + "/games_test_bad.json");

	std::printf("\n%d checks, %d failures\n", g_checks, g_failures);
	return g_failures ? 1 : 0;
}
