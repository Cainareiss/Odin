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

/* Unit tests for the settings file and the bindings: the lines read, the
 * names of keys and pad inputs, the defaults of the roles, the lines of a
 * game by the ids of its controls, and what is refused.
 *
 * Build: g++ -std=c++20 -O2 -Wall -Wextra -Wpedantic -Wconversion -Wshadow \
 *            -I src $(sdl2-config --cflags) tests/bindings/bindings_tests.cpp src/odin/bindings.cpp \
 *            src/odin/settings.cpp src/odin/roles.cpp src/odin/save_folder.cpp $(sdl2-config --libs) -o bindings_tests
 * It writes its file in the folder given as its argument, or the temporary
 * one. Exit status is 0 when every check passes. */

#include "odin/bindings.hpp"

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>

namespace {

using odin::Bindings;
using odin::GameControl;
using odin::HostInput;
using odin::PadInput;
using odin::Role;
using odin::Settings;

int g_checks, g_failures;

void check(bool ok, const char *what)
{
	g_checks++;
	if (!ok) {
		g_failures++;
		std::printf("FAILED: %s\n", what);
	}
}

bool is_key(const HostInput &input, SDL_Keycode key)
{
	return input.kind == HostInput::Kind::KEY && input.key == key;
}

bool is_pad(const HostInput &input, PadInput::Type type, unsigned index)
{
	return input.kind == HostInput::Kind::PAD && input.pad.type == type && input.pad.index == index;
}

void test_settings(const std::string &dir)
{
	Settings settings;

	settings.parse("# a comment\n"
	               "top = 1\n"
	               "\n"
	               "[ controls ]\r\n"
	               "  p1.button1 =  key:A , pad:b  \n"
	               "; another comment\n"
	               "# p1.button3 = key:B\n"
	               "; p1.button3 = key:B\n"
	               "not a setting\n"
	               "= no key\n"
	               "p1.button2=\n"
	               "[controls.sgnascar]\n"
	               "p1.steer = pad:rightx\n", "test");
	const Settings::Lines &controls = settings.section("controls");

	check(settings.section("").size() == 1 && settings.section("")[0].first == "top", "keys before any section");
	check(controls.size() == 2, "comments, blank lines and what is not a setting are left out");
	check(controls.size() == 2 && controls[0].first == "p1.button1" && controls[0].second == "key:A , pad:b",
	      "key and value without the spaces around them, the section name too");
	check(controls.size() == 2 && controls[1].first == "p1.button2" && controls[1].second.empty(),
	      "an empty value, in the order of the file");
	check(settings.section("controls.sgnascar").size() == 1, "another section");
	check(settings.section("nowhere").empty(), "a section that is not there has no line");

	const std::string path = dir + "/bindings_test.ini";
	Settings missing;

	check(missing.load(dir + "/no_such_file.ini") && missing.section("controls").empty(),
	      "a file that is not there is an empty one");
	std::ofstream(path) << "[controls]\ncoin1 = key:7\n";
	Settings loaded;
	check(loaded.load(path) && loaded.section("controls").size() == 1 &&
	      loaded.section("controls")[0].second == "key:7", "a file read");
	std::filesystem::remove(path);

	setenv("ODIN_CONFIG", "none", 1);
	check(Settings::user_file().empty(), "ODIN_CONFIG=none: no file");
	setenv("ODIN_CONFIG", "/somewhere/mine.ini", 1);
	check(Settings::user_file() == "/somewhere/mine.ini", "ODIN_CONFIG=<file>: that file");
	unsetenv("ODIN_CONFIG");
	setenv("XDG_DATA_HOME", "/data", 1);
	check(Settings::user_file() == "/data/odin/odin.ini", "else odin.ini in the data folder of Odin");
}

void test_inputs()
{
	const auto ctrl = HostInput::parse("key:Left Ctrl");
	check(ctrl && is_key(*ctrl, SDLK_LCTRL), "key:Left Ctrl");
	check(HostInput::parse("key:z") && is_key(*HostInput::parse("key:z"), SDLK_z) &&
	      HostInput::parse("key:Z") && is_key(*HostInput::parse("key:Z"), SDLK_z), "a letter, either case");
	check(HostInput::parse("key:F2") && is_key(*HostInput::parse("key:F2"), SDLK_F2), "key:F2");
	check(!HostInput::parse("key:nope") && !HostInput::parse("Left Ctrl") && !HostInput::parse("key:"),
	      "what is not a key");

	check(HostInput::parse("pad:a") && is_pad(*HostInput::parse("pad:a"), PadInput::Type::BUTTON, 0), "pad:a");
	check(HostInput::parse("pad:touchpad") &&
	      is_pad(*HostInput::parse("pad:touchpad"), PadInput::Type::BUTTON, SDL_CONTROLLER_BUTTON_TOUCHPAD),
	      "pad:touchpad");
	check(HostInput::parse("pad:leftx") && is_pad(*HostInput::parse("pad:leftx"), PadInput::Type::AXIS, 0),
	      "pad:leftx, an axis");
	check(HostInput::parse("pad:righttrigger") && HostInput::parse("pad:righttrigger")->pad.is_trigger(),
	      "pad:righttrigger, a trigger");
	check(HostInput::parse("pad:-lefty") &&
	      is_pad(*HostInput::parse("pad:-lefty"), PadInput::Type::AXIS_NEGATIVE, SDL_CONTROLLER_AXIS_LEFTY),
	      "pad:-lefty, the left stick up");
	check(HostInput::parse("pad:+rightx") &&
	      is_pad(*HostInput::parse("pad:+rightx"), PadInput::Type::AXIS_POSITIVE, SDL_CONTROLLER_AXIS_RIGHTX),
	      "pad:+rightx, the right stick right");
	check(!HostInput::parse("pad:+lefttrigger") && !HostInput::parse("pad:-a") && !HostInput::parse("pad:nope") &&
	      !HostInput::parse("pad:") && !HostInput::parse("joy:a"), "what is not a pad input");
	check(HostInput::parse("pad:lefty:invert") && HostInput::parse("pad:lefty:invert")->pad.inverted &&
	      is_pad(*HostInput::parse("pad:lefty:invert"), PadInput::Type::AXIS, SDL_CONTROLLER_AXIS_LEFTY) &&
	      !HostInput::parse("pad:lefty")->pad.inverted && !HostInput::parse("pad:lefty:invert")->pad.is_digital(),
	      "pad:lefty:invert, the left stick turned over");
	check(!HostInput::parse("pad:righttrigger:invert") && !HostInput::parse("pad:-lefty:invert") &&
	      !HostInput::parse("pad:a:invert") && !HostInput::parse("pad::invert") && !HostInput::parse("pad:lefty:inv"),
	      "only the whole axis of a stick is turned over");
	check(HostInput::parse("pad:a")->pad.is_digital() && HostInput::parse("pad:-leftx")->pad.is_digital() &&
	      HostInput::parse("pad:lefttrigger")->pad.is_digital() && !HostInput::parse("pad:leftx")->pad.is_digital(),
	      "pressed or not: buttons, halves, triggers; not a whole stick");
}

GameControl control(const char *id, GameControl::Type type, Role role, uint8_t player)
{
	GameControl c{};

	c.name = id;
	c.id = id;
	c.type = type;
	c.role = role;
	c.player = player;
	return c;
}

void test_defaults()
{
	const auto b1 = Bindings::defaults(Role::BUTTON1, 0);
	const auto b1p2 = Bindings::defaults(Role::BUTTON1, 1);

	check(b1.size() == 2 && is_key(b1[0], SDLK_LCTRL) && is_pad(b1[1], PadInput::Type::BUTTON, 0),
	      "button1 of player 1: Left Ctrl and A");
	check(b1p2.size() == 2 && is_key(b1p2[0], SDLK_a) && is_pad(b1p2[1], PadInput::Type::BUTTON, 0),
	      "button1 of player 2: the key A and A of its pad");
	check(Bindings::defaults(Role::START, 1).size() == 2 && is_key(Bindings::defaults(Role::START, 1)[0], SDLK_2),
	      "start of player 2: 2 and Start");
	check(Bindings::defaults(Role::SERVICE, 0).size() == 1 && is_key(Bindings::defaults(Role::SERVICE, 0)[0], SDLK_9) &&
	      is_key(Bindings::defaults(Role::SERVICE, 1)[0], SDLK_0), "Service: 9 and 0, no pad");
	check(is_key(Bindings::defaults(Role::COIN1, 0)[0], SDLK_5) && is_key(Bindings::defaults(Role::COIN2, 1)[0], SDLK_6) &&
	      is_key(Bindings::defaults(Role::TEST, 0)[0], SDLK_F2) &&
	      is_key(Bindings::defaults(Role::BOARD_TEST, 0)[0], SDLK_F5) &&
	      is_key(Bindings::defaults(Role::BOARD_SERVICE, 0)[0], SDLK_F6), "the cabinet: 5, 6, F2, F5, F6, whatever player");
	check(is_key(Bindings::defaults(Role::UP, 1)[0], SDLK_r) && is_key(Bindings::defaults(Role::BUTTON6, 1)[0], SDLK_t) &&
	      is_key(Bindings::defaults(Role::BUTTON4, 0)[0], SDLK_z), "the other keys as INSTALL tells them");
	check(Bindings::defaults(Role::STEER, 0).size() == 1 &&
	      is_pad(Bindings::defaults(Role::STEER, 0)[0], PadInput::Type::AXIS, SDL_CONTROLLER_AXIS_LEFTX) &&
	      is_pad(Bindings::defaults(Role::ACCEL, 0)[0], PadInput::Type::AXIS, SDL_CONTROLLER_AXIS_TRIGGERRIGHT) &&
	      is_pad(Bindings::defaults(Role::STICK2_Y, 1)[0], PadInput::Type::AXIS, SDL_CONTROLLER_AXIS_RIGHTY),
	      "the axes: steering on the left stick, gas on the right trigger");
	check(is_pad(Bindings::defaults(Role::GEAR_UP, 0)[0], PadInput::Type::BUTTON, SDL_CONTROLLER_BUTTON_RIGHTSHOULDER) &&
	      is_pad(Bindings::defaults(Role::BUTTON7, 0)[0], PadInput::Type::AXIS, SDL_CONTROLLER_AXIS_TRIGGERLEFT) &&
	      is_pad(Bindings::defaults(Role::STICK_UP, 0)[0], PadInput::Type::AXIS_NEGATIVE, SDL_CONTROLLER_AXIS_LEFTY),
	      "gears on RB, button 7 on LT, the left stick up");
	check(Bindings::defaults(Role::GUN_X, 0).size() == 1 &&
	      is_pad(Bindings::defaults(Role::GUN_X, 0)[0], PadInput::Type::AXIS, SDL_CONTROLLER_AXIS_LEFTX) &&
	      is_pad(Bindings::defaults(Role::GUN_Y, 1)[0], PadInput::Type::AXIS, SDL_CONTROLLER_AXIS_LEFTY),
	      "the sight of a gun where the left stick points");
	check(Bindings::defaults(Role::GEAR, 0).empty(), "the lever: none");
}

void test_game_lines()
{
	const std::vector<GameControl> panel = {
		control("view", GameControl::Type::BUTTON, Role::BUTTON4, 0),
		control("gear", GameControl::Type::POSITIONS, Role::GEAR, 0),
		control("wheel", GameControl::Type::ANALOG, Role::STEER, 0),
		control("fog", GameControl::Type::BUTTON, Role::BUTTON2, 1),
		control("1p_service", GameControl::Type::BUTTON, Role::SERVICE, 0),
	};
	Settings settings;

	settings.parse("[controls.sgnascar]\n"
	               "view = key:Space, pad:b,\n"
	               "gear_up = pad:a\n"
	               "wheel = pad:rightx\n"
	               "fog =\n"
	               "coin1 = key:7, pad:back\n"
	               "1p_service = key:F12\n"
	               "nothing = key:A\n"
	               "p1.button1 = key:A\n"
	               "gear = pad:a\n"
	               "gear_down = pad:leftx\n"
	               "wheel = key:Left\n"
	               "wheel = pad:-leftx\n"
	               "coin2 = pad:leftx\n"
	               "1p_service = key:nope\n"
	               "[controls]\n"
	               "view = key:X\n", "test");
	const Bindings nascar(panel, settings, "sgnascar");
	const Bindings other(panel, settings, "airtrix");

	check(nascar.of(0).size() == 2 && is_key(nascar.of(0)[0], SDLK_SPACE) &&
	      is_pad(nascar.of(0)[1], PadInput::Type::BUTTON, SDL_CONTROLLER_BUTTON_B), "a control by its id");
	check(nascar.lever(1, true).size() == 1 && is_pad(nascar.lever(1, true)[0], PadInput::Type::BUTTON, 0),
	      "a lever: <id>_up");
	check(nascar.lever(1, false).size() == 1 &&
	      is_pad(nascar.lever(1, false)[0], PadInput::Type::BUTTON, SDL_CONTROLLER_BUTTON_LEFTSHOULDER),
	      "<id>_down as it was: the line for it was not right");
	check(nascar.of(2).size() == 1 && is_pad(nascar.of(2)[0], PadInput::Type::AXIS, SDL_CONTROLLER_AXIS_RIGHTX),
	      "a channel on another axis; the key and half a stick refused");
	check(nascar.of(3).empty(), "an empty value: none");
	check(nascar.cabinet(Role::COIN1).size() == 2 && is_key(nascar.cabinet(Role::COIN1)[0], SDLK_7),
	      "a control of the cabinet");
	check(nascar.cabinet(Role::COIN2).size() == 1 && is_key(nascar.cabinet(Role::COIN2)[0], SDLK_6),
	      "the other one as it was");
	check(nascar.of(4).size() == 1 && is_key(nascar.of(4)[0], SDLK_F12), "the SERVICE switch by its id");

	check(other.of(0).size() == 2 && is_key(other.of(0)[0], SDLK_z) &&
	      is_pad(other.of(0)[1], PadInput::Type::BUTTON, SDL_CONTROLLER_BUTTON_Y),
	      "another game keeps the defaults of the role: neither [controls.sgnascar] nor [controls] count");
	check(other.of(3).size() == 2 && is_key(other.of(3)[0], SDLK_s) &&
	      is_pad(other.of(3)[1], PadInput::Type::BUTTON, SDL_CONTROLLER_BUTTON_B),
	      "a control of player 2: the keys of player 2");
	check(other.lever(1, true).size() == 1 &&
	      is_pad(other.lever(1, true)[0], PadInput::Type::BUTTON, SDL_CONTROLLER_BUTTON_RIGHTSHOULDER),
	      "a lever: GEAR_UP and GEAR_DOWN");
}

} /* namespace */

int main(int argc, char **argv)
{
	const std::string dir = (argc > 1) ? argv[1] : std::filesystem::temp_directory_path().string();

	test_settings(dir);
	test_inputs();
	test_defaults();
	test_game_lines();

	std::printf("\n%d checks, %d failures\n", g_checks, g_failures);
	return g_failures ? 1 : 0;
}
