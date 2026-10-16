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
 * names of keys and pad inputs, the defaults, the lines of [controls] and
 * of a game, and what is refused.
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
	check(HostInput::parse("pad:a")->pad.is_digital() && HostInput::parse("pad:-leftx")->pad.is_digital() &&
	      HostInput::parse("pad:lefttrigger")->pad.is_digital() && !HostInput::parse("pad:leftx")->pad.is_digital(),
	      "pressed or not: buttons, halves, triggers; not a whole stick");
}

void test_defaults()
{
	const Bindings bindings;
	const auto &b1 = bindings.of(Role::BUTTON1, 0);
	const auto &b1p2 = bindings.of(Role::BUTTON1, 1);

	check(b1.size() == 2 && is_key(b1[0], SDLK_LCTRL) && is_pad(b1[1], PadInput::Type::BUTTON, 0),
	      "p1.button1: Left Ctrl and A");
	check(b1p2.size() == 2 && is_key(b1p2[0], SDLK_a) && is_pad(b1p2[1], PadInput::Type::BUTTON, 0),
	      "p2.button1: the key A and A of its pad");
	check(bindings.of(Role::START, 1).size() == 2 && is_key(bindings.of(Role::START, 1)[0], SDLK_2),
	      "p2.start: 2 and Start");
	check(bindings.of(Role::SERVICE, 0).size() == 1 && is_key(bindings.of(Role::SERVICE, 0)[0], SDLK_9) &&
	      is_key(bindings.of(Role::SERVICE, 1)[0], SDLK_0), "Service: 9 and 0, no pad");
	check(is_key(bindings.of(Role::COIN1, 0)[0], SDLK_5) && is_key(bindings.of(Role::COIN2, 1)[0], SDLK_6) &&
	      is_key(bindings.of(Role::TEST, 0)[0], SDLK_F2) && is_key(bindings.of(Role::BOARD_TEST, 0)[0], SDLK_F5) &&
	      is_key(bindings.of(Role::BOARD_SERVICE, 0)[0], SDLK_F6), "the cabinet: 5, 6, F2, F5, F6, whatever player");
	check(is_key(bindings.of(Role::UP, 1)[0], SDLK_r) && is_key(bindings.of(Role::BUTTON6, 1)[0], SDLK_t) &&
	      is_key(bindings.of(Role::BUTTON4, 0)[0], SDLK_z), "the other keys as INSTALL tells them");
	check(bindings.of(Role::STEER, 0).size() == 1 &&
	      is_pad(bindings.of(Role::STEER, 0)[0], PadInput::Type::AXIS, SDL_CONTROLLER_AXIS_LEFTX) &&
	      is_pad(bindings.of(Role::ACCEL, 0)[0], PadInput::Type::AXIS, SDL_CONTROLLER_AXIS_TRIGGERRIGHT) &&
	      is_pad(bindings.of(Role::STICK2_Y, 1)[0], PadInput::Type::AXIS, SDL_CONTROLLER_AXIS_RIGHTY),
	      "the axes: steering on the left stick, gas on the right trigger");
	check(is_pad(bindings.of(Role::GEAR_UP, 0)[0], PadInput::Type::BUTTON, SDL_CONTROLLER_BUTTON_RIGHTSHOULDER) &&
	      is_pad(bindings.of(Role::BUTTON7, 0)[0], PadInput::Type::AXIS, SDL_CONTROLLER_AXIS_TRIGGERLEFT) &&
	      is_pad(bindings.of(Role::STICK_UP, 0)[0], PadInput::Type::AXIS_NEGATIVE, SDL_CONTROLLER_AXIS_LEFTY),
	      "gears on RB, button 7 on LT, the left stick up");
	check(bindings.of(Role::GUN_X, 0).empty() && bindings.of(Role::GEAR, 0).empty(), "the gun and the lever: none");
}

void test_settings_lines()
{
	Settings settings;

	settings.parse("[controls]\n"
	               "p1.button1 = key:Space, pad:b,\n"
	               "p1.button2 =\n"
	               "coin1 = key:7, pad:back\n"
	               "p3.button1 = key:A\n"
	               "button3 = key:A\n"
	               "p1.coin2 = key:A\n"
	               "p1.gear = pad:a\n"
	               "p1.steer = key:Left\n"
	               "p1.accel = pad:-leftx\n"
	               "p1.button4 = pad:leftx\n"
	               "p1.button5 = key:nope\n"
	               "p1.button6 = key:A, nope\n"
	               "p1.nothing = key:A\n"
	               "[controls.sgnascar]\n"
	               "p2.button4 = pad:b\n", "test");
	const Bindings nascar(settings, "sgnascar");
	const Bindings airtrix(settings, "airtrix");
	const auto &b1 = airtrix.of(Role::BUTTON1, 0);

	check(b1.size() == 2 && is_key(b1[0], SDLK_SPACE) && is_pad(b1[1], PadInput::Type::BUTTON, 1),
	      "a line of [controls] takes the place of the default");
	check(airtrix.of(Role::BUTTON2, 0).empty(), "an empty value: none");
	check(airtrix.of(Role::COIN1, 0).size() == 2 && is_key(airtrix.of(Role::COIN1, 0)[0], SDLK_7),
	      "a role of the cabinet");
	check(nascar.of(Role::BUTTON4, 1).size() == 1 && is_pad(nascar.of(Role::BUTTON4, 1)[0], PadInput::Type::BUTTON, 1),
	      "a line of [controls.<game>] for that game");
	check(airtrix.of(Role::BUTTON4, 1).size() == 2 && is_key(airtrix.of(Role::BUTTON4, 1)[0], SDLK_w),
	      "not for another one");
	check(is_key(nascar.of(Role::BUTTON1, 0)[0], SDLK_SPACE), "[controls] for every game");
	check(is_key(airtrix.of(Role::BUTTON3, 0)[0], SDLK_LSHIFT) && is_key(airtrix.of(Role::COIN2, 0)[0], SDLK_6) &&
	      airtrix.of(Role::STEER, 0).size() == 1 &&
	      is_pad(airtrix.of(Role::STEER, 0)[0], PadInput::Type::AXIS, SDL_CONTROLLER_AXIS_LEFTX) &&
	      airtrix.of(Role::ACCEL, 0).size() == 1 &&
	      is_pad(airtrix.of(Role::ACCEL, 0)[0], PadInput::Type::AXIS, SDL_CONTROLLER_AXIS_TRIGGERRIGHT) &&
	      is_pad(airtrix.of(Role::BUTTON4, 0)[1], PadInput::Type::BUTTON, 3) &&
	      is_key(airtrix.of(Role::BUTTON5, 0)[0], SDLK_x) && is_key(airtrix.of(Role::BUTTON6, 0)[0], SDLK_c) &&
	      airtrix.of(Role::GEAR, 0).empty(),
	      "a line not right leaves the role as it was: player 3, no player, a player for the cabinet, the lever, "
	      "a key or half a stick for a channel, a whole stick for a button, a key that is not one");
}

} /* namespace */

int main(int argc, char **argv)
{
	const std::string dir = (argc > 1) ? argv[1] : std::filesystem::temp_directory_path().string();

	test_settings(dir);
	test_inputs();
	test_defaults();
	test_settings_lines();

	std::printf("\n%d checks, %d failures\n", g_checks, g_failures);
	return g_failures ? 1 : 0;
}
