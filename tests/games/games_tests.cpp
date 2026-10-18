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
 * its files, the controls of a cabinet and their roles, the chip of a new
 * cabinet, and what is refused. The chips of the list of the repo are checked
 * too: run it from the repo.
 *
 * Build: g++ -std=c++20 -O2 -Wall -Wextra -Wpedantic -Wconversion -Wshadow \
 *            -I src tests/games/games_tests.cpp src/odin/games.cpp src/odin/roles.cpp -ljansson \
 *            -o games_tests
 * It writes its files in the folder given as its argument, or the current
 * one. Exit status is 0 when every check passes. */

#include "odin/games.hpp"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include <jansson.h>

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
    { "name": "fit", "mach": "hikaru", "rom_board": { "eprom_mb": 2 }, "sections": [
        { "name": "eprom", "type": "interleave", "amnt": 2, "data": [
            { "name": "e.bin", "size": 2097152 }, { "name": "e.bin", "size": 2097152 } ] } ] },
    { "name": "misfit", "mach": "hikaru", "rom_board": { "eprom_mb": 2 }, "sections": [
        { "name": "eprom", "type": "interleave", "amnt": 2, "data": [
            { "name": "a.bin", "size": 4 }, { "name": "b.bin", "size": 4 } ] } ] },
    { "name": "wheel", "title": "Wheel ~ Test: 2!", "mach": "hikaru", "io": "837-13741", "option_speakers": true, "option_level": -22.5,
      "option_vibration": true, "drive_board": { "first_output": 15 },
      "rom_board": { "eprom_mb": 4, "315-5881": "291b02C7", "actel": { "key": "56DEDF33", "id": "4252" } },
      "mie_eeprom": "4242520001aBCDEF0000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000ff",
      "controls": [
        { "name": "WHEEL", "id": "wheel", "analog": 0, "low": "left", "role": "steer" },
        { "name": "GAS", "id": "gas_2", "analog": 7, "low": "released", "role": "accel" },
        { "name": "LEVER", "id": "lever", "analog": 3, "low": "up", "role": "stick2_y", "player": 2 },
        { "name": "VIEW", "id": "view", "switch": "1P DOWN", "role": "button4", "player": 1, "keys": [ "Z", "Space" ] },
        { "name": "FIRE", "id": "fire", "switch": "2P PUSH3", "role": "gun_offscreen" },
        { "name": "SHIFT", "id": "shift", "switch": "1P PUSH10", "role": "stick2_left" },
        { "name": "GEAR", "id": "gear", "role": "gear", "positions": [
            { "name": "N", "switches": [] },
            { "name": "1", "switches": [ "2P LEFT", "2P UP" ] } ] } ],
      "outputs": [
        { "name": "1P LAMP", "id": "1p_lamp", "output": 1, "role": "lamp" },
        { "name": "2P SHAKE", "id": "2p_shake", "output": 13, "role": "rumble", "player": 2 },
        { "name": "LOCK", "id": "lock", "output": 32, "role": "lock" },
        { "name": "MUTE", "id": "mute", "output": 14, "role": "mute" } ],
      "sections": [
        { "name": "bootrom", "type": "alternative", "data": [ { "name": "boot.bin", "size": 4 } ] } ] }
  ]
})";

/* Games whose one control is not right, each refused */
const char *const BAD_CONTROLS[] = {
	R"("controls": { "name": "A", "id": "a", "switch": "1P START", "role": "start" })",
	R"("controls": [ { "id": "a", "switch": "1P START", "role": "start" } ])",
	R"("controls": [ { "name": "", "switch": "1P START", "role": "start" } ])",
	R"("controls": [ { "name": "A", "id": "a", "role": "start" } ])",
	R"("controls": [ { "name": "A", "id": "a", "switch": "1P START", "analog": 0, "low": "left", "role": "start" } ])",
	R"("controls": [ { "name": "A", "id": "a", "switch": "3P START", "role": "start" } ])",
	R"("controls": [ { "name": "A", "id": "a", "switch": "1P PUSH11", "role": "start" } ])",
	R"("controls": [ { "name": "A", "id": "a", "switch": "1P", "role": "start" } ])",
	R"("controls": [ { "name": "A", "id": "a", "analog": 8, "low": "left", "role": "steer" } ])",
	R"("controls": [ { "name": "A", "id": "a", "analog": -1, "low": "left", "role": "steer" } ])",
	R"("controls": [ { "name": "A", "id": "a", "analog": "0", "low": "left", "role": "steer" } ])",
	R"("controls": [ { "name": "A", "id": "a", "analog": 0, "role": "steer" } ])",
	R"("controls": [ { "name": "A", "id": "a", "analog": 0, "low": "sideways", "role": "steer" } ])",
	R"("controls": [ { "name": "A", "id": "a", "positions": [], "role": "gear" } ])",
	R"("controls": [ { "name": "A", "id": "a", "positions": [ { "switches": [] } ], "role": "gear" } ])",
	R"("controls": [ { "name": "A", "id": "a", "positions": [ { "name": "1" } ], "role": "gear" } ])",
	R"("controls": [ { "name": "A", "id": "a", "positions": [ { "name": "1", "switches": [ "1P NOWHERE" ] } ], "role": "gear" } ])",
	R"("controls": [ { "name": "A", "id": "a", "switch": "1P START" } ])",
	R"("controls": [ { "name": "A", "id": "a", "switch": "1P START", "role": "nowhere" } ])",
	R"("controls": [ { "name": "A", "id": "a", "switch": "1P START", "role": 3 } ])",
	R"("controls": [ { "name": "A", "id": "a", "switch": "1P START", "role": "steer" } ])",
	R"("controls": [ { "name": "A", "id": "a", "switch": "1P START", "role": "gear" } ])",
	R"("controls": [ { "name": "A", "id": "a", "switch": "1P START", "role": "coin1" } ])",
	R"("controls": [ { "name": "A", "id": "a", "analog": 0, "low": "left", "role": "button1" } ])",
	R"("controls": [ { "name": "A", "id": "a", "analog": 0, "low": "left", "role": "gear" } ])",
	R"("controls": [ { "name": "A", "id": "a", "analog": 0, "low": "left", "role": "test" } ])",
	R"("controls": [ { "name": "A", "id": "a", "positions": [ { "name": "1", "switches": [] } ], "role": "gear_up" } ])",
	R"("controls": [ { "name": "A", "id": "a", "switch": "1P START", "role": "start", "player": 3 } ])",
	R"("controls": [ { "name": "A", "id": "a", "switch": "1P START", "role": "start", "player": 0 } ])",
	R"("controls": [ { "name": "A", "id": "a", "switch": "1P START", "role": "start", "player": "2" } ])",
	R"("controls": [ { "name": "A", "id": "a", "switch": "1P START", "role": "start", "keys": [] } ])",
	R"("controls": [ { "name": "A", "id": "a", "switch": "1P START", "role": "start", "keys": "Z" } ])",
	R"("controls": [ { "name": "A", "id": "a", "switch": "1P START", "role": "start", "keys": [ "Z", "" ] } ])",
	R"("controls": [ { "name": "A", "id": "a", "switch": "1P START", "role": "start", "keys": [ 3 ] } ])",
	R"("controls": [ { "name": "A", "id": "a", "analog": 0, "low": "left", "role": "steer", "keys": [ "Z" ] } ])",
	R"("controls": [ { "name": "A", "switch": "1P START", "role": "start" } ])",
	R"("controls": [ { "name": "A", "id": "", "switch": "1P START", "role": "start" } ])",
	R"("controls": [ { "name": "A", "id": "View", "switch": "1P START", "role": "start" } ])",
	R"("controls": [ { "name": "A", "id": "a b", "switch": "1P START", "role": "start" } ])",
	R"("controls": [ { "name": "A", "id": 3, "switch": "1P START", "role": "start" } ])",
	R"("controls": [ { "name": "A", "id": "a", "switch": "1P START", "role": "start" },
	                 { "name": "B", "id": "a", "switch": "1P UP", "role": "up" } ])",
	R"("controls": [ { "name": "A", "id": "coin1", "switch": "1P START", "role": "start" } ])",
	R"("controls": [ { "name": "A", "id": "1p_service", "switch": "1P START", "role": "start" } ])",
	R"("controls": [ { "name": "A", "id": "gear_up", "switch": "1P START", "role": "start" },
	                 { "name": "B", "id": "gear", "positions": [ { "name": "N", "switches": [] } ], "role": "gear" } ])",
	R"("controls": [ { "name": "A", "id": "wheel_left", "switch": "1P START", "role": "start" },
	                 { "name": "B", "id": "wheel", "analog": 0, "low": "right", "role": "steer" } ])",
	R"("controls": [ { "name": "B", "id": "lever", "analog": 0, "low": "down", "role": "stick_y" },
	                 { "name": "A", "id": "lever_up", "switch": "1P START", "role": "start" } ])",
	R"("outputs": { "name": "A", "id": "a", "output": 1, "role": "lamp" })",
	R"("outputs": [ { "id": "a", "output": 1, "role": "lamp" } ])",
	R"("outputs": [ { "name": "A", "output": 1, "role": "lamp" } ])",
	R"("outputs": [ { "name": "A", "id": "a", "role": "lamp" } ])",
	R"("outputs": [ { "name": "A", "id": "a", "output": 0, "role": "lamp" } ])",
	R"("outputs": [ { "name": "A", "id": "a", "output": 33, "role": "lamp" } ])",
	R"("outputs": [ { "name": "A", "id": "a", "output": "1", "role": "lamp" } ])",
	R"("outputs": [ { "name": "A", "id": "a", "output": 1 } ])",
	R"("outputs": [ { "name": "A", "id": "a", "output": 1, "role": "smoke" } ])",
	R"("outputs": [ { "name": "A", "id": "a", "output": 1, "role": "lamp", "player": 3 } ])",
	R"("outputs": [ { "name": "A", "id": "a", "output": 1, "role": "lamp" },
	                { "name": "B", "id": "a", "output": 2, "role": "lamp" } ])",
	R"("controls": [ { "name": "A", "id": "a", "switch": "1P START", "role": "start" } ],
	   "outputs": [ { "name": "B", "id": "a", "output": 2, "role": "lamp" } ])",
	R"("rom_board": 4)",
	R"("rom_board": {})",
	R"("rom_board": { "eprom_mb": 3 })",
	R"("rom_board": { "eprom_mb": "4" })",
	R"("rom_board": { "eprom_mb": 4, "315-5881": "291B02C" })",
	R"("rom_board": { "eprom_mb": 4, "315-5881": "291B02C7FF" })",
	R"("rom_board": { "eprom_mb": 4, "315-5881": 689636039 })",
	R"("rom_board": { "eprom_mb": 4, "actel": { "key": "56DEDF33" } })",
	R"("rom_board": { "eprom_mb": 4, "actel": { "key": "56DEDF33", "id": "425" } })",
	R"("rom_board": { "eprom_mb": 4, "actel": { "key": "56DEDF33", "id": "4252", "chip": "A54SX32" } })",
	R"("rom_board": { "eprom_mb": 4, "stretched": true })",
};

void test_games(const std::string &dir)
{
	const std::string roms = dir + "/games_test_roms";

	std::filesystem::create_directories(roms + "/toy");
	for (const char *game : { "short", "odd", "unequal", "stepless", "fit", "misfit", "wheel" })
		std::filesystem::create_directories(roms + "/" + game);
	write(dir + "/games_test.json", LIST);
	write(roms + "/fit/e.bin", std::string(2 << 20, 'E'));
	for (const char *game : { "toy", "short", "odd", "unequal", "stepless", "misfit", "wheel" }) {
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
	check(wheel && wheel->title == "Wheel ~ Test: 2!", "the title the list tells");
	check(toy && toy->title == "toy", "without \"title\", the name");
	check(wheel && wheel->io_board == "837-13741", "the I/O board the list tells");
	check(wheel && wheel->option_speakers, "the speakers of the optional sound board the list tells");
	check(wheel && wheel->option_level == -22.5, "the level of the optional board the list tells, in dB");
	check(wheel && wheel->option_vibration, "the vibration device the optional board feeds, the list tells");
	check(wheel && wheel->drive_first_output == 15, "the output of TX0 of the drive board, the list tells");
	check(wheel && wheel->rom_board && wheel->rom_board->eprom_mb == 4 && wheel->rom_board->crypt_key == 0x291B02C7 &&
	      wheel->rom_board->has_actel && wheel->rom_board->actel_key == 0x56DEDF33 &&
	      wheel->rom_board->actel_id == 0x4252, "the ROM board the list tells, its keys in small or capital figures");
	if (toy)
		check(toy->controls.empty() && toy->mie_eeprom.empty() && !toy->option_speakers && toy->option_level == 0 &&
		      !toy->option_vibration && !toy->drive_first_output && !toy->rom_board,
		      "without \"controls\", no control, without \"mie_eeprom\", no chip, without \"option_speakers\", none");
	check(wheel && wheel->mie_eeprom.size() == 128 && wheel->mie_eeprom[0] == 0x42 && wheel->mie_eeprom[4] == 0x01 &&
	      wheel->mie_eeprom[5] == 0xAB && wheel->mie_eeprom[7] == 0xEF && wheel->mie_eeprom[127] == 0xFF,
	      "the chip of a new cabinet, byte by byte, small or capital figures");
	if (wheel && wheel->controls.size() == 7) {
		using odin::GameControl;
		const auto &c = wheel->controls;

		check(c[0].name == "WHEEL" && c[0].type == GameControl::Type::ANALOG && c[0].analog == 0 &&
		      c[0].low == GameControl::Low::LEFT, "an analog channel and the way of its low values");
		check(c[0].role == odin::Role::STEER && c[1].role == odin::Role::ACCEL && c[3].role == odin::Role::BUTTON4 &&
		      c[4].role == odin::Role::GUN_OFFSCREEN && c[5].role == odin::Role::STICK2_LEFT &&
		      c[6].role == odin::Role::GEAR, "the role of each control");
		check(c[0].player == 0 && c[3].player == 0, "player 1 when the list does not say, or says 1");
		check(c[0].id == "wheel" && c[1].id == "gas_2" && c[6].id == "gear", "the id of each control");
		check(c[3].keys == std::vector<std::string>{ "Z", "Space" } && c[0].keys.empty() && c[4].keys.empty(),
		      "the keys of a button, in their order; none when the list gives none");
		check(c[2].player == 1 && c[2].role == odin::Role::STICK2_Y, "player 2 when it says 2");
		check(c[1].analog == 7 && c[1].low == GameControl::Low::RELEASED, "channel 7, low when released");
		check(c[2].analog == 3 && c[2].low == GameControl::Low::UP, "low when up");
		check(c[3].type == GameControl::Type::BUTTON && c[3].button.player == 0 && c[3].button.mask == 0x1000,
		      "1P DOWN: player 1, bit 12");
		check(c[4].button.player == 1 && c[4].button.mask == 0x0080, "2P PUSH3: player 2, bit 7");
		check(c[5].button.player == 0 && c[5].button.mask == 0x0001, "1P PUSH10: bit 0");
		check(c[6].type == GameControl::Type::POSITIONS && c[6].positions.size() == 2 &&
		      c[6].positions[0].name == "N" && c[6].positions[0].switches.empty() &&
		      c[6].positions[1].name == "1" && c[6].positions[1].switches.size() == 2 &&
		      c[6].positions[1].switches[0].player == 1 && c[6].positions[1].switches[0].mask == 0x0800 &&
		      c[6].positions[1].switches[1].mask == 0x2000, "a lever: each position with its switches");
	} else {
		check(false, "the seven controls of the list");
	}
	if (wheel && wheel->outputs.size() == 4) {
		using odin::GameOutput;
		const auto &o = wheel->outputs;
		const uint8_t bytes[] = { 0x80, 0x08 };
		const uint8_t last[] = { 0x00, 0x00, 0x00, 0x01 };

		check(o[0].name == "1P LAMP" && o[0].id == "1p_lamp" && o[0].output == 1 && o[0].role == GameOutput::Role::LAMP &&
		      o[0].player == 0, "an output, a lamp of player 1 when the list does not say");
		check(o[1].output == 13 && o[1].role == GameOutput::Role::RUMBLE && o[1].player == 1,
		      "an output that shakes the pad of player 2");
		check(o[0].on(bytes) && o[1].on(bytes) && !o[0].on(std::span(bytes + 1, 1)),
		      "output 1 is bit 7 of the first byte, output 13 bit 3 of the second");
		check(!o[1].on(std::span(bytes, 1)), "an output past the bytes of the board: off");
		check(o[2].role == GameOutput::Role::LOCK && o[2].output == 32 && o[2].on(last) && !o[2].on(bytes),
		      "a lock; output 32, bit 0 of the fourth byte");

		const uint8_t muted[] = { 0x00, 0x04 };

		check(o[3].role == GameOutput::Role::MUTE && o[3].output == 14 && o[3].on(muted),
		      "a mute output; output 14, bit 2 of the second byte");
		check(odin::any_on(o, GameOutput::Role::MUTE, muted) && !odin::any_on(o, GameOutput::Role::MUTE, bytes) &&
		      !odin::any_on(o, GameOutput::Role::LAMP, muted), "an output of a role on, or none");
		check(odin::any_on(o, GameOutput::Role::RUMBLE, bytes, 1) && !odin::any_on(o, GameOutput::Role::RUMBLE, bytes, 0) &&
		      odin::any_on(o, GameOutput::Role::RUMBLE, bytes), "of a player, or of either");
	} else {
		check(false, "the four outputs of the list");
	}
	if (toy)
		check(toy->outputs.empty(), "without \"outputs\", none");

	check(!list.load_game(roms, "other"), "a game that is not in the list does not load");
	check(!list.load_game(roms, "short"), "nor one whose file has another size than told");
	check(!list.load_game(roms, "odd"), "nor one with a section of a type that is not known");
	check(!list.load_game(roms, "unequal"), "nor one whose alternative files have different sizes");
	check(!list.load_game(roms, "stepless"), "nor one that interleaves without telling by how many bytes");
	check(!list.load_game(roms, "misfit"), "nor one whose EPROMs are not of the size its ROM board takes");
	const std::optional<Game> fit = list.load_game(roms, "fit");
	check(fit && fit->rom_board && fit->rom_board->eprom_mb == 2 && !fit->rom_board->crypt_key &&
	      !fit->rom_board->has_actel && fit->section("eprom").size() == 4 << 20,
	      "EPROMs of 2 MB on a board that takes them, without a guard chip");
	check(!list.load_game(dir + "/no_such_folder", "toy"), "nor one whose files are not there");
}

void test_roles()
{
	using odin::Role;
	bool round = true;
	unsigned switches = 0, analogs = 0, cabinet = 0;

	for (unsigned i = 0; i < static_cast<unsigned>(Role::COUNT); i++) {
		const Role role = static_cast<Role>(i);
		const auto parsed = odin::parse_role(odin::role_name(role));

		round = round && parsed && *parsed == role;
		switches += odin::role_is_switch(role);
		analogs += odin::role_is_analog(role);
		cabinet += odin::role_of_cabinet(role);
		check(odin::role_is_switch(role) + odin::role_is_analog(role) + odin::role_of_cabinet(role) +
		      (role == Role::GEAR) == 1, odin::role_name(role));
	}
	check(round, "each role read back from its name");
	check(switches == 25 && analogs == 9 && cabinet == 5, "25 switches, 9 analog roles, 5 of the cabinet");
	check(!odin::parse_role("Start") && !odin::parse_role("button9") && !odin::parse_role(""), "what is not a role");
	check(std::string(odin::role_name(Role::STICK2_UP)) == "stick2_up" &&
	      std::string(odin::role_name(Role::BOARD_SERVICE)) == "board_service", "the names of the list");
}

/* Each bad control refuses its game, a good one beside it loads */
void test_bad_controls(const std::string &dir)
{
	const std::string roms = dir + "/games_test_roms";
	const std::string path = dir + "/games_test_bad.json";
	const std::string files = R"("sections": [ { "name": "bootrom", "type": "alternative",
	    "data": [ { "name": "boot.bin", "size": 4 } ] } ])";
	int refused = 0;
	const auto refuses = [&](const std::string &bad) {
		GameList list;

		write(path, std::string(R"({ "version": 1, "roms": [ { "name": "good", "mach": "hikaru",)") +
		      R"( "controls": [ { "name": "A", "id": "a", "switch": "1P START", "role": "start" } ], )" + files +
		      R"( }, { "name": "bad", "mach": "hikaru", )" + bad + ", " + files + " } ] }");
		if (list.load(path) && list.load_game(roms, "good") && !list.load_game(roms, "bad"))
			return true;
		std::printf("not refused: %s\n", bad.c_str());
		return false;
	};

	std::filesystem::create_directories(roms + "/good");
	std::filesystem::create_directories(roms + "/bad");
	write(roms + "/good/boot.bin", "BOOT");
	write(roms + "/bad/boot.bin", "BOOT");
	for (const char *bad : BAD_CONTROLS)
		refused += refuses(bad);
	check(refused == static_cast<int>(std::size(BAD_CONTROLS)), "a game with a control that is not one is refused");

	GameList pedal;

	write(path, std::string(R"({ "version": 1, "roms": [ { "name": "good", "mach": "hikaru", "controls": [)") +
	      R"( { "name": "GAS", "id": "gas", "analog": 0, "low": "released", "role": "accel" },)" +
	      R"( { "name": "A", "id": "gas_left", "switch": "1P START", "role": "start" } ], )" + files + " } ] }");
	check(pedal.load(path) && pedal.load_game(roms, "good"), "a pedal has no halves: <id>_left is free");

	const std::string chip = std::string(256, '0');
	const std::string bad_chips[] = {
		R"("mie_eeprom": 3)", "\"mie_eeprom\": \"" + chip.substr(2) + "\"", "\"mie_eeprom\": \"" + chip + "00\"",
		"\"mie_eeprom\": \"" + chip.substr(1) + "\"", "\"mie_eeprom\": \"" + chip.substr(2) + "0g\"",
	};
	refused = 0;
	for (const std::string &bad : bad_chips)
		refused += refuses(bad);
	check(refused == static_cast<int>(std::size(bad_chips)),
	      "a game whose chip is not 128 bytes in hexadecimal is refused");
	check(refuses(R"("title": 3)") && refuses(R"("title": "")") &&
	      refuses("\"title\": \"" + std::string(odin::Game::MAX_TITLE + 1, 'A') + "\"") &&
	      refuses(R"("title": "Caf\u00e9")") && refuses(R"("title": "A\tB")") && refuses(R"("title": "A\u007fB")"),
	      "and one whose \"title\" is not 1 to 48 printable ASCII characters");
	check(!refuses("\"title\": \"" + std::string(odin::Game::MAX_TITLE, 'A') + "\""),
	      "a title of 48 characters is not");
	check(refuses(R"("option_speakers": "yes")") && refuses(R"("option_speakers": 1)"),
	      "and one whose \"option_speakers\" is not true or false");
	check(refuses(R"("drive_board": 15)") && refuses(R"("drive_board": {})") &&
	      refuses(R"("drive_board": { "first_output": 0 })") && refuses(R"("drive_board": { "first_output": 26 })") &&
	      refuses(R"("drive_board": { "first_output": "15" })"),
	      "and one whose \"drive_board\" has no first output of 1 to 25");
	check(refuses(R"("option_vibration": "yes")") && refuses(R"("option_vibration": 1)"),
	      "and one whose \"option_vibration\" is not true or false");
	check(refuses(R"("option_level": -3)") && refuses(R"("option_speakers": false, "option_level": -3)") &&
	      refuses(R"("option_speakers": true, "option_level": "-3")") &&
	      refuses(R"("option_speakers": true, "option_level": -60.5)") &&
	      refuses(R"("option_speakers": true, "option_level": 12.5)"),
	      "and one whose \"option_level\" is not a number of -60 to 12, or comes without its speakers");
	for (const char *level : { "-60", "12" }) {
		GameList ends;

		write(path, std::string(R"({ "version": 1, "roms": [ { "name": "good", "mach": "hikaru",)") +
		      R"( "option_speakers": true, "option_level": )" + level + ", " + files + " } ] }");
		const auto good = ends.load(path) ? ends.load_game(roms, "good") : std::nullopt;
		check(good && good->option_level == std::stod(level), "-60 and 12 dB are levels");
	}
}

/* The CRC of the settings of the BOOTROM (@0C007504): a 32-bit register
 * from 01A18104, each byte ORed in and shifted out, eight more shifts. */
uint16_t bootrom_crc(const uint8_t *bytes, size_t size)
{
	uint32_t r = 0x01A18104;
	const auto shift = [&r] {
		const bool top = (r & 0x80000000u) != 0;

		r <<= 1;
		if (top)
			r ^= 0x10210000u;
	};

	for (size_t i = 0; i < size; i++) {
		r |= bytes[i];
		for (int k = 0; k < 8; k++)
			shift();
	}
	for (int k = 0; k < 8; k++)
		shift();
	return static_cast<uint16_t>(r >> 16);
}

/* The chips of new cabinets in the list of the repo (the tests run from the
 * repo): written by the BOOTROM, each copy right for it, the country U.S.A. */
void test_shipped_chips()
{
	json_error_t error;
	json_t *const root = json_load_file("odin-games.json", 0, &error);
	const json_t *const roms = json_object_get(root, "roms");
	int chips = 0, right = 0;

	for (size_t i = 0; i < json_array_size(roms); i++) {
		const char *const text = json_string_value(json_object_get(json_array_get(roms, i), "mie_eeprom"));
		std::vector<uint8_t> b;

		if (!text)
			continue;
		chips++;
		for (const char *c = text; c[0] && c[1]; c += 2)
			b.push_back(static_cast<uint8_t>(std::stoul(std::string(c, 2), nullptr, 16)));
		const char *const game = json_string_value(json_object_get(json_array_get(roms, i), "name"));
		/* AIRTRIX keeps the calibration of its board there (0x26 - 0x2B): the player's to make, not Odin's */
		const bool blank = std::string(game) != "airtrix" ||
		                   (b.size() == 128 && std::all_of(b.begin() + 0x26, b.begin() + 0x2C, [](uint8_t v) { return !v; }));

		if (b.size() == 128 && std::equal(b.begin(), b.begin() + 64, b.begin() + 64) &&
		    bootrom_crc(b.data(), 62) == (b[62] | b[63] << 8) && b[5] == 1 && blank)
			right++;
		else
			std::printf("not right: %s\n", json_string_value(json_object_get(json_array_get(roms, i), "name")));
	}
	json_decref(root);
	check(chips == 6 && right == 6,
	      "the six games have the chip of a new U.S.A. cabinet, its CRC right, the board of AIRTRIX not calibrated");
}

/* The outputs of the list of the repo: the nozzles of BRAVEFF shake, read
 * in its OUTPUT TEST (output 2 player 1, output 5 player 2) */
void test_shipped_outputs()
{
	json_error_t error;
	json_t *const root = json_load_file("odin-games.json", 0, &error);
	const json_t *const roms = json_object_get(root, "roms");
	std::string rumbles;

	for (size_t i = 0; i < json_array_size(roms); i++) {
		const json_t *const outputs = json_object_get(json_array_get(roms, i), "outputs");

		for (size_t o = 0; o < json_array_size(outputs); o++) {
			const json_t *const output = json_array_get(outputs, o);
			const char *const role = json_string_value(json_object_get(output, "role"));

			if (role && std::string(role) == "rumble")
				rumbles += std::string(json_string_value(json_object_get(json_array_get(roms, i), "name"))) + ":" +
				           std::to_string(json_integer_value(json_object_get(output, "output"))) + ":" +
				           std::to_string(json_integer_value(json_object_get(output, "player"))) + " ";
		}
	}
	json_decref(root);
	check(rumbles == "braveff:2:0 braveff:5:2 ", "the nozzles of BRAVEFF shake the pads: outputs 2 and 5");
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
	test_roles();
	test_bad_controls(dir);
	test_shipped_chips();
	test_shipped_outputs();
	test_lists(dir);
	std::filesystem::remove_all(dir + "/games_test_roms");
	std::filesystem::remove(dir + "/games_test.json");
	std::filesystem::remove(dir + "/games_test_bad.json");

	std::printf("\n%d checks, %d failures\n", g_checks, g_failures);
	return g_failures ? 1 : 0;
}
