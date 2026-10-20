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
 * names of keys and pad inputs, the defaults of the roles, the keys and
 * pad inputs the list of games gives a button, the lines of a game by the
 * ids of its controls, and what is refused.
 *
 * Build: g++ -std=c++20 -O2 -Wall -Wextra -Wpedantic -Wconversion -Wshadow \
 *            -I src $(pkg-config --cflags sdl3) tests/bindings/bindings_tests.cpp src/odin/bindings.cpp \
 *            src/odin/settings.cpp src/odin/roles.cpp src/odin/save_folder.cpp $(pkg-config --libs sdl3) -o bindings_tests
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
using odin::MouseInput;
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
	check(HostInput::parse("key:z") && is_key(*HostInput::parse("key:z"), SDLK_Z) &&
	      HostInput::parse("key:Z") && is_key(*HostInput::parse("key:Z"), SDLK_Z), "a letter, either case");
	check(HostInput::parse("key:F2") && is_key(*HostInput::parse("key:F2"), SDLK_F2), "key:F2");
	check(!HostInput::parse("key:nope") && !HostInput::parse("Left Ctrl") && !HostInput::parse("key:"),
	      "what is not a key");

	check(HostInput::parse("pad:a") && is_pad(*HostInput::parse("pad:a"), PadInput::Type::BUTTON, 0), "pad:a");
	check(HostInput::parse("pad:touchpad") &&
	      is_pad(*HostInput::parse("pad:touchpad"), PadInput::Type::BUTTON, SDL_GAMEPAD_BUTTON_TOUCHPAD),
	      "pad:touchpad");
	check(HostInput::parse("pad:leftx") && is_pad(*HostInput::parse("pad:leftx"), PadInput::Type::AXIS, 0),
	      "pad:leftx, an axis");
	check(HostInput::parse("pad:righttrigger") && HostInput::parse("pad:righttrigger")->pad.is_trigger(),
	      "pad:righttrigger, a trigger");
	check(HostInput::parse("pad:-lefty") &&
	      is_pad(*HostInput::parse("pad:-lefty"), PadInput::Type::AXIS_NEGATIVE, SDL_GAMEPAD_AXIS_LEFTY),
	      "pad:-lefty, the left stick up");
	check(HostInput::parse("pad:+rightx") &&
	      is_pad(*HostInput::parse("pad:+rightx"), PadInput::Type::AXIS_POSITIVE, SDL_GAMEPAD_AXIS_RIGHTX),
	      "pad:+rightx, the right stick right");
	check(!HostInput::parse("pad:+lefttrigger") && !HostInput::parse("pad:-a") && !HostInput::parse("pad:nope") &&
	      !HostInput::parse("pad:") && !HostInput::parse("joy:a"), "what is not a pad input");
	check(HostInput::parse("pad:lefty:invert") && HostInput::parse("pad:lefty:invert")->pad.inverted &&
	      is_pad(*HostInput::parse("pad:lefty:invert"), PadInput::Type::AXIS, SDL_GAMEPAD_AXIS_LEFTY) &&
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
	check(b1p2.size() == 2 && is_key(b1p2[0], SDLK_A) && is_pad(b1p2[1], PadInput::Type::BUTTON, 0),
	      "button1 of player 2: the key A and A of its pad");
	check(Bindings::defaults(Role::START, 1).size() == 2 && is_key(Bindings::defaults(Role::START, 1)[0], SDLK_2),
	      "start of player 2: 2 and Start");
	check(Bindings::defaults(Role::SERVICE, 0).size() == 2 && is_key(Bindings::defaults(Role::SERVICE, 0)[0], SDLK_9) &&
	      Bindings::defaults(Role::SERVICE, 0)[1].text() == "pad:guide+leftstick" &&
	      Bindings::defaults(Role::SERVICE, 1).size() == 1 && Bindings::defaults(Role::SERVICE, 1)[0].text() == "pad:guide+leftstick",
	      "Service: 9 for player 1, and Guide with the left stick pressed on the pad of each player");
	check(is_key(Bindings::defaults(Role::COIN1, 0)[0], SDLK_5) && is_key(Bindings::defaults(Role::COIN2, 1)[0], SDLK_6) &&
	      is_pad(Bindings::defaults(Role::COIN1, 0)[1], PadInput::Type::BUTTON, SDL_GAMEPAD_BUTTON_BACK) &&
	      is_pad(Bindings::defaults(Role::COIN2, 0)[1], PadInput::Type::BUTTON, SDL_GAMEPAD_BUTTON_BACK) &&
	      Bindings::defaults(Role::TEST, 0).size() == 2 && is_key(Bindings::defaults(Role::TEST, 0)[0], SDLK_0) &&
	      Bindings::defaults(Role::TEST, 0)[1].text() == "pad:guide+rightstick" &&
	      Bindings::defaults(Role::BOARD_TEST, 0).empty() && Bindings::defaults(Role::BOARD_SERVICE, 0).empty(),
	      "the cabinet: coins 5, 6 and SELECT, Test 0 and Guide with the right stick; the switches of the main board "
	      "without key");
	check(is_key(Bindings::defaults(Role::UP, 1)[0], SDLK_R) && is_key(Bindings::defaults(Role::BUTTON6, 1)[0], SDLK_T) &&
	      is_key(Bindings::defaults(Role::BUTTON4, 0)[0], SDLK_Z), "the other keys as INSTALL tells them");
	check(Bindings::defaults(Role::STEER, 0).size() == 1 &&
	      is_pad(Bindings::defaults(Role::STEER, 0)[0], PadInput::Type::AXIS, SDL_GAMEPAD_AXIS_LEFTX) &&
	      is_pad(Bindings::defaults(Role::ACCEL, 0)[0], PadInput::Type::AXIS, SDL_GAMEPAD_AXIS_RIGHT_TRIGGER) &&
	      is_pad(Bindings::defaults(Role::STICK2_Y, 1)[0], PadInput::Type::AXIS, SDL_GAMEPAD_AXIS_RIGHTY),
	      "the axes: steering on the left stick, gas on the right trigger");
	check(is_pad(Bindings::defaults(Role::GEAR_UP, 0)[1], PadInput::Type::BUTTON, SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER) &&
	      is_pad(Bindings::defaults(Role::BUTTON7, 0)[1], PadInput::Type::AXIS, SDL_GAMEPAD_AXIS_LEFT_TRIGGER) &&
	      is_pad(Bindings::defaults(Role::STICK_UP, 0)[1], PadInput::Type::AXIS_NEGATIVE, SDL_GAMEPAD_AXIS_LEFTY),
	      "gears on RB, button 7 on LT, the left stick up");
	check(is_key(Bindings::defaults(Role::GEAR_UP, 0)[0], SDLK_C) &&
	      is_key(Bindings::defaults(Role::GEAR_DOWN, 0)[0], SDLK_X) &&
	      is_key(Bindings::defaults(Role::BUTTON7, 0)[0], SDLK_V) &&
	      is_key(Bindings::defaults(Role::BUTTON8, 0)[0], SDLK_B), "gears on C and X (RB, LB), buttons 7 and 8 on V and B");
	check(is_key(Bindings::defaults(Role::STICK_UP, 0)[0], SDLK_UP) &&
	      is_key(Bindings::defaults(Role::STICK_RIGHT, 1)[0], SDLK_G) &&
	      is_key(Bindings::defaults(Role::STICK2_LEFT, 0)[0], SDLK_D) &&
	      is_key(Bindings::defaults(Role::STICK2_DOWN, 0)[0], SDLK_F) &&
	      Bindings::defaults(Role::STICK2_UP, 1).size() == 1,
	      "a stick on the directions of the player, the second of player 1 on those of player 2");
	check(Bindings::defaults(Role::GUN_X, 0).size() == 1 &&
	      is_pad(Bindings::defaults(Role::GUN_X, 0)[0], PadInput::Type::AXIS, SDL_GAMEPAD_AXIS_LEFTX) &&
	      is_pad(Bindings::defaults(Role::GUN_Y, 1)[0], PadInput::Type::AXIS, SDL_GAMEPAD_AXIS_LEFTY),
	      "the sight of a gun where the left stick points");
	check(Bindings::defaults(Role::GEAR, 0).empty(), "the lever: none");
}

void test_keys_toward()
{
	const auto one = [](Role role, unsigned player, bool positive) {
		const auto keys = Bindings::keys_toward(role, player, positive);

		return keys.size() == 1 && keys[0].kind == HostInput::Kind::KEY ? keys[0].key : SDLK_UNKNOWN;
	};

	check(one(Role::STEER, 0, false) == SDLK_LEFT && one(Role::STEER, 0, true) == SDLK_RIGHT &&
	      one(Role::STEER, 1, false) == SDLK_D && one(Role::STEER, 1, true) == SDLK_G,
	      "the wheel: the arrows, D and G for player 2");
	check(Bindings::keys_toward(Role::ACCEL, 0, false).empty() && one(Role::ACCEL, 0, true) == SDLK_UP &&
	      one(Role::BRAKE, 0, true) == SDLK_DOWN && one(Role::BRAKE, 1, true) == SDLK_F,
	      "the gas on Up, the brake on Down, as xemu's Chihiro has them");
	check(one(Role::STICK_X, 0, true) == SDLK_RIGHT && one(Role::STICK_Y, 0, false) == SDLK_UP &&
	      one(Role::STICK_Y, 1, true) == SDLK_F && one(Role::GUN_X, 1, true) == SDLK_G &&
	      one(Role::GUN_Y, 0, true) == SDLK_DOWN, "a stick and a sight: the directions of the player");
	check(one(Role::STICK2_X, 0, false) == SDLK_D && one(Role::STICK2_Y, 0, true) == SDLK_F &&
	      Bindings::keys_toward(Role::STICK2_X, 1, true).empty(),
	      "the second stick of player 1 on the keys of player 2; none for player 2");
	check(Bindings::keys_toward(Role::BUTTON1, 0, true).empty() && Bindings::keys_toward(Role::GEAR, 0, false).empty(),
	      "nothing for a button or a lever");
}

void test_analog_lines()
{
	GameControl gas = control("gas", GameControl::Type::ANALOG, Role::ACCEL, 0);
	GameControl lever = control("lever", GameControl::Type::ANALOG, Role::STICK_Y, 0);

	gas.low = GameControl::Low::RELEASED;
	lever.low = GameControl::Low::DOWN;

	const std::vector<GameControl> panel = { control("wheel", GameControl::Type::ANALOG, Role::STEER, 0), gas, lever };
	Settings settings;

	settings.parse("[controls.sgnascar]\n"
	               "wheel_left = key:A, pad:dpleft\n"
	               "wheel_right = pad:leftx\n"
	               "gas = pad:lefttrigger, key:Space, pad:a\n"
	               "lever_up = key:W\n"
	               "lever_left = key:Q\n"
	               "gas_left = key:Q\n", "test");
	const Bindings mine(panel, settings, "sgnascar");
	const Bindings plain(panel, Settings(), "sgnascar");

	check(mine.toward(0, false).size() == 2 && is_key(mine.toward(0, false)[0], SDLK_A) &&
	      is_pad(mine.toward(0, false)[1], PadInput::Type::BUTTON, SDL_GAMEPAD_BUTTON_DPAD_LEFT),
	      "<id>_left: a key and a button");
	check(mine.toward(0, true).size() == 1 && is_key(mine.toward(0, true)[0], SDLK_RIGHT),
	      "<id>_right as it was: the whole axis of a stick is refused there");
	check(mine.of(0).size() == 1 && is_pad(mine.of(0)[0], PadInput::Type::AXIS, SDL_GAMEPAD_AXIS_LEFTX),
	      "the axis of the wheel kept");
	check(mine.of(1).size() == 1 && is_pad(mine.of(1)[0], PadInput::Type::AXIS, SDL_GAMEPAD_AXIS_LEFT_TRIGGER) &&
	      mine.toward(1, true).size() == 2 && is_key(mine.toward(1, true)[0], SDLK_SPACE) &&
	      is_pad(mine.toward(1, true)[1], PadInput::Type::BUTTON, SDL_GAMEPAD_BUTTON_SOUTH),
	      "a pedal: its axes, and what presses it, on its own line");
	check(mine.toward(2, false).size() == 1 && is_key(mine.toward(2, false)[0], SDLK_W) &&
	      mine.toward(2, true).size() == 1 && is_key(mine.toward(2, true)[0], SDLK_DOWN),
	      "a lever up and down: <id>_up; <id>_left is none of its halves");
	check(plain.toward(0, false).size() == 1 && is_key(plain.toward(0, false)[0], SDLK_LEFT) &&
	      plain.toward(1, false).empty() && plain.toward(1, true).size() == 1 &&
	      is_key(plain.toward(1, true)[0], SDLK_UP) && is_key(plain.toward(2, false)[0], SDLK_UP),
	      "without settings, the keys of the roles");
}

bool is_mouse(const HostInput &input, MouseInput::Type type, unsigned button = 0)
{
	return input.kind == HostInput::Kind::MOUSE && input.mouse.type == type &&
	       (type != MouseInput::Type::BUTTON || input.mouse.button == button);
}

void test_mouse()
{
	using Type = MouseInput::Type;
	const auto parsed = [](const char *text, Type type, unsigned button = 0) {
		const auto input = HostInput::parse(text);

		return input && is_mouse(*input, type, button);
	};

	check(parsed("mouse:left", Type::BUTTON, 0) && parsed("mouse:middle", Type::BUTTON, 1) &&
	      parsed("mouse:right", Type::BUTTON, 2), "the buttons of the mouse");
	check(parsed("mouse:x", Type::X) && parsed("mouse:y", Type::Y) && parsed("mouse:off", Type::OFF) &&
	      HostInput::parse("mouse:x")->is_axis() && !HostInput::parse("mouse:off")->is_axis() &&
	      !HostInput::parse("mouse:left")->is_axis(), "where it points: x and y are axes, off is pressed or not");
	check(!HostInput::parse("mouse:wheel") && !HostInput::parse("mouse:") && !HostInput::parse("mouse:Left"),
	      "what is not of the mouse");

	const std::vector<GameControl> brave = {
		control("1p_sight_x", GameControl::Type::ANALOG, Role::GUN_X, 0),
		control("1p_sight_y", GameControl::Type::ANALOG, Role::GUN_Y, 0),
		control("1p_out_of_screen", GameControl::Type::BUTTON, Role::GUN_OFFSCREEN, 0),
		control("1p_water", GameControl::Type::BUTTON, Role::BUTTON1, 0),
		control("1p_fog", GameControl::Type::BUTTON, Role::BUTTON2, 0),
		control("1p_start", GameControl::Type::BUTTON, Role::START, 0),
		control("2p_water", GameControl::Type::BUTTON, Role::BUTTON1, 1),
		control("2p_sight_x", GameControl::Type::ANALOG, Role::GUN_X, 1),
	};
	const Bindings aims(brave, Settings(), "braveff");

	check(aims.of(0).size() == 2 && is_pad(aims.of(0)[0], PadInput::Type::AXIS, SDL_GAMEPAD_AXIS_LEFTX) &&
	      is_mouse(aims.of(0)[1], Type::X) && is_mouse(aims.of(1).back(), Type::Y),
	      "the sight of player 1: the left stick and the mouse");
	check(aims.of(2).size() == 1 && is_mouse(aims.of(2)[0], Type::OFF), "OUT OF SCREEN: the mouse off the picture");
	check(is_mouse(aims.of(3).back(), Type::BUTTON, 0) && is_mouse(aims.of(4).back(), Type::BUTTON, 2) &&
	      is_mouse(aims.of(5).back(), Type::BUTTON, 1) && is_key(aims.of(3)[0], SDLK_LCTRL),
	      "water on the left button, fog on the right, START on the middle, the keys kept");
	check(!is_mouse(aims.of(6).back(), Type::BUTTON, 0) && aims.of(7).size() == 1,
	      "player 2: no mouse");

	const Bindings plain({ control("bullet", GameControl::Type::BUTTON, Role::BUTTON1, 0) }, Settings(), "pharrier");

	check(plain.of(0).size() == 2 && plain.of(0)[1].kind == HostInput::Kind::PAD, "no sight, no mouse");

	Settings settings;

	settings.parse("[controls.braveff]\n1p_sight_x = mouse:left\n1p_water = mouse:x\n"
	               "1p_fog = mouse:left, key:F\n1p_sight_y = mouse:x\n", "test");
	const Bindings mine(brave, settings, "braveff");

	check(mine.of(0).size() == 2, "a button of the mouse on a channel: refused");
	check(mine.of(3).size() == 3, "where the mouse points on a button: refused");
	check(mine.of(4).size() == 2 && is_mouse(mine.of(4)[0], Type::BUTTON, 0) && is_key(mine.of(4)[1], SDLK_F),
	      "a button of the mouse on a button");
	check(mine.of(1).size() == 1 && is_mouse(mine.of(1)[0], Type::X), "the sight Y on where the mouse points across");
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
	      is_pad(nascar.of(0)[1], PadInput::Type::BUTTON, SDL_GAMEPAD_BUTTON_EAST), "a control by its id");
	check(nascar.lever(1, true).size() == 1 && is_pad(nascar.lever(1, true)[0], PadInput::Type::BUTTON, 0),
	      "a lever: <id>_up");
	check(nascar.lever(1, false).size() == 2 && is_key(nascar.lever(1, false)[0], SDLK_X) &&
	      is_pad(nascar.lever(1, false)[1], PadInput::Type::BUTTON, SDL_GAMEPAD_BUTTON_LEFT_SHOULDER),
	      "<id>_down as it was: the line for it was not right");
	check(nascar.of(2).size() == 1 && is_pad(nascar.of(2)[0], PadInput::Type::AXIS, SDL_GAMEPAD_AXIS_RIGHTX),
	      "a channel on another axis; the key and half a stick refused");
	check(nascar.of(3).empty(), "an empty value: none");
	check(nascar.cabinet(Role::COIN1).size() == 2 && is_key(nascar.cabinet(Role::COIN1)[0], SDLK_7),
	      "a control of the cabinet");
	check(nascar.cabinet(Role::COIN2).size() == 2 && is_key(nascar.cabinet(Role::COIN2)[0], SDLK_6),
	      "the other one as it was");
	check(nascar.of(4).size() == 1 && is_key(nascar.of(4)[0], SDLK_F12), "the SERVICE switch by its id");

	check(other.of(0).size() == 2 && is_key(other.of(0)[0], SDLK_Z) &&
	      is_pad(other.of(0)[1], PadInput::Type::BUTTON, SDL_GAMEPAD_BUTTON_NORTH),
	      "another game keeps the defaults of the role: neither [controls.sgnascar] nor [controls] count");
	check(other.of(3).size() == 2 && is_key(other.of(3)[0], SDLK_S) &&
	      is_pad(other.of(3)[1], PadInput::Type::BUTTON, SDL_GAMEPAD_BUTTON_EAST),
	      "a control of player 2: the keys of player 2");
	check(other.lever(1, true).size() == 2 && is_key(other.lever(1, true)[0], SDLK_C) &&
	      is_pad(other.lever(1, true)[1], PadInput::Type::BUTTON, SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER),
	      "a lever: GEAR_UP and GEAR_DOWN");
}

void test_own_keys()
{
	std::vector<GameControl> panel = {
		control("left", GameControl::Type::BUTTON, Role::LEFT, 0),
		control("up", GameControl::Type::BUTTON, Role::BUTTON5, 1),
		control("boost", GameControl::Type::BUTTON, Role::BUTTON1, 0),
	};
	Settings settings;

	panel[0].keys = { "X", "Space" };
	panel[1].keys = { "Up" };
	panel[2].keys = { "nope" };
	settings.parse("[controls.airtrix]\nup = key:F1\n", "test");
	const Bindings own(panel, Settings(), "airtrix");
	const Bindings lined(panel, settings, "airtrix");

	check(own.of(0).size() == 3 && is_key(own.of(0)[0], SDLK_X) && is_key(own.of(0)[1], SDLK_SPACE) &&
	      is_pad(own.of(0)[2], PadInput::Type::BUTTON, SDL_GAMEPAD_BUTTON_DPAD_LEFT),
	      "the keys the list gives a button take the place of those of its role; its pad input stays");
	check(own.of(1).size() == 2 && is_key(own.of(1)[0], SDLK_UP) &&
	      is_pad(own.of(1)[1], PadInput::Type::BUTTON, SDL_GAMEPAD_BUTTON_LEFT_SHOULDER),
	      "for player 2, in place of the keys of player 2");
	check(own.of(2).size() == 1 && is_pad(own.of(2)[0], PadInput::Type::BUTTON, SDL_GAMEPAD_BUTTON_SOUTH),
	      "a name that is no key: no key, the pad input stays");
	check(lined.of(1).size() == 1 && is_key(lined.of(1)[0], SDLK_F1), "a line of the settings takes their place");
}

void test_own_pad()
{
	using Type = PadInput::Type;
	std::vector<GameControl> panel = {
		control("1p_water", GameControl::Type::BUTTON, Role::BUTTON1, 0),
		control("bomb", GameControl::Type::BUTTON, Role::BUTTON3, 0),
		control("2p_fog", GameControl::Type::BUTTON, Role::BUTTON2, 1),
		control("boost", GameControl::Type::BUTTON, Role::BUTTON6, 0),
		control("view", GameControl::Type::BUTTON, Role::BUTTON4, 0),
		control("1p_sight_x", GameControl::Type::ANALOG, Role::GUN_X, 0),
	};
	Settings settings;

	panel[0].pad = { "righttrigger" };
	panel[1].pad = { "a", "guide+x" };
	panel[2].pad = { "lefttrigger" };
	panel[3].pad = { "leftx" };
	panel[4].keys = { "Z" };
	panel[4].pad = { "y", "nope" };
	settings.parse("[controls.braveff]\n1p_water = pad:a\n", "test");
	const Bindings own(panel, Settings(), "braveff");
	const Bindings lined(panel, settings, "braveff");

	check(own.of(0).size() == 3 && is_key(own.of(0)[0], SDLK_LCTRL) &&
	      is_pad(own.of(0)[1], Type::AXIS, SDL_GAMEPAD_AXIS_RIGHT_TRIGGER) && own.of(0)[2].kind == HostInput::Kind::MOUSE,
	      "the pad input the list gives a button takes the place of that of its role; its key and the mouse stay");
	check(own.of(1).size() == 3 && is_key(own.of(1)[0], SDLK_LSHIFT) &&
	      is_pad(own.of(1)[1], Type::BUTTON, SDL_GAMEPAD_BUTTON_SOUTH) &&
	      is_pad(own.of(1)[2], Type::BUTTON, SDL_GAMEPAD_BUTTON_WEST) && own.of(1)[2].pad.with == SDL_GAMEPAD_BUTTON_GUIDE,
	      "several, in their order, a button held with Guide among them");
	check(own.of(2).size() == 2 && is_key(own.of(2)[0], SDLK_S) &&
	      is_pad(own.of(2)[1], Type::AXIS, SDL_GAMEPAD_AXIS_LEFT_TRIGGER), "for player 2, the key of player 2 stays");
	check(own.of(3).size() == 1 && is_key(own.of(3)[0], SDLK_C), "a whole stick is no button: no pad input");
	check(own.of(4).size() == 2 && is_key(own.of(4)[0], SDLK_Z) && is_pad(own.of(4)[1], Type::BUTTON, SDL_GAMEPAD_BUTTON_NORTH),
	      "its own keys and pad inputs together; a name that is no input is left out");
	check(own.standard(0, 0) == own.of(0) && !own.line(0, 0, own.of(0)).value, "they are the defaults: no line");
	check(lined.of(0).size() == 1 && is_pad(lined.of(0)[0], Type::BUTTON, SDL_GAMEPAD_BUTTON_SOUTH) &&
	      lined.line(0, 0, lined.of(0)).value == "pad:a", "a line of the settings takes their place, and is written");
}

} /* namespace */

/* An input back to its text, which reads the same; a list of them */
void test_text()
{
	for (const char *text : { "key:Left Ctrl", "key:Z", "key:F2", "key:Return", "pad:a", "pad:start", "pad:-lefty",
	                          "pad:+rightx", "pad:lefty:invert", "pad:righttrigger", "pad:leftshoulder",
	                          "mouse:left", "mouse:middle", "mouse:right", "mouse:x", "mouse:y", "mouse:off" }) {
		const auto input = HostInput::parse(text);

		check(input && input->text() == text && HostInput::parse(input->text()) == input, text);
	}
	const auto chord = HostInput::parse("pad:guide+leftstick");

	check(chord && chord->pad.type == PadInput::Type::BUTTON && chord->pad.index == SDL_GAMEPAD_BUTTON_LEFT_STICK &&
	      chord->pad.with == SDL_GAMEPAD_BUTTON_GUIDE && chord->text() == "pad:guide+leftstick",
	      "pad:guide+leftstick: a button held with Guide, and back");
	check(!HostInput::parse("pad:guide+leftx") && !HostInput::parse("pad:nope+a") && !HostInput::parse("pad:guide+") &&
	      !HostInput::parse("pad:a+a") && HostInput::parse("pad:+leftx") && !(*HostInput::parse("pad:a") == *chord),
	      "a second button only, not the same, and +leftx stays half a stick");
	check(HostInput::text({ *HostInput::parse("key:A"), *HostInput::parse("pad:b") }) == "key:A, pad:b",
	      "a list, with a comma");
	check(HostInput::text({}).empty(), "no input: nothing");
	check(*HostInput::parse("pad:a") == *HostInput::parse("pad:a") && !(*HostInput::parse("pad:a") == *HostInput::parse("pad:b")),
	      "the same input is equal, another is not");
}

/* The line a part of a control is written to: none for its defaults */
void test_lines_to_write()
{
	GameControl gas = control("gas", GameControl::Type::ANALOG, Role::ACCEL, 0);
	GameControl stick = control("stick", GameControl::Type::ANALOG, Role::STICK_Y, 0);

	gas.low = GameControl::Low::RELEASED;
	stick.low = GameControl::Low::DOWN;

	const std::vector<GameControl> panel = { control("view", GameControl::Type::BUTTON, Role::BUTTON4, 0),
	                                         control("gear", GameControl::Type::POSITIONS, Role::GEAR, 0), gas, stick };
	const Bindings plain(panel, Settings(), "sgnascar");
	const auto key = [](const char *name) { return *HostInput::parse(name); };

	Bindings::Line line = plain.line(0, 0, plain.of(0));
	const auto lines = plain.standard_lines();
	const auto has = [&lines](size_t i, const char *k, const char *v) {
		return i < lines.size() && lines[i].first == k && lines[i].second == v;
	};

	check(lines.size() == 12 && has(0, "view", "key:Z, pad:y") && has(1, "gear_down", "key:X, pad:leftshoulder") &&
	      has(2, "gear_up", "key:C, pad:rightshoulder") && has(3, "gas", "pad:righttrigger, key:Up") &&
	      has(4, "stick", "pad:lefty") && has(5, "stick_up", "key:Up") && has(6, "stick_down", "key:Down") &&
	      has(7, "coin1", "key:5, pad:back") && has(11, "board_service", ""),
	      "every line at the defaults: a button, a lever, a pedal on one line, an analog control, the cabinet");
	check(line.key == "view" && !line.value, "a button with its defaults: no value, the line can go");
	line = plain.line(0, 0, { key("key:Q"), key("pad:y") });
	check(line.key == "view" && line.value == "key:Q, pad:y", "a button changed");
	line = plain.line(0, 0, {});
	check(line.key == "view" && line.value && line.value->empty(), "a button without inputs: an empty value");
	line = plain.line(1, 2, { key("key:W") });
	check(line.key == "gear_up" && line.value == "key:W", "a lever up");
	line = plain.line(1, 1, plain.lever(1, false));
	check(line.key == "gear_down" && !line.value, "a lever down with its defaults");
	line = plain.line(3, 1, { key("key:I") });
	check(line.key == "stick_up" && line.value == "key:I", "an analog control: its half that goes up");
	line = plain.line(3, 0, { key("pad:righty") });
	check(line.key == "stick" && line.value == "pad:righty", "and its axes");

	line = plain.line(2, 2, { key("key:Space") });
	check(line.key == "gas" && line.value == "pad:righttrigger, key:Space",
	      "a pedal: one line, its axes kept and what presses it");
	line = plain.line(2, 0, {});
	check(line.key == "gas" && line.value == "key:Up", "a pedal without axes keeps what presses it");
	line = plain.line(2, 2, plain.toward(2, true));
	check(line.key == "gas" && !line.value, "a pedal with its defaults");

	Settings settings;

	settings.parse("[controls.sgnascar]\n" + line.key + " = " + *plain.line(2, 2, { key("key:Space") }).value + "\n"
	               "view = key:Q, pad:y\n", "test");
	const Bindings mine(panel, settings, "sgnascar");

	check(mine.of(0).size() == 2 && mine.of(0)[0] == key("key:Q") && mine.of(0)[1] == key("pad:y"),
	      "a line written reads back");
	check(mine.toward(2, true).size() == 1 && mine.toward(2, true)[0] == key("key:Space") && mine.of(2).size() == 1,
	      "a pedal line written reads back on its two parts");
	check(!mine.line(0, 0, plain.of(0)).value, "the defaults are those of the role, whatever the settings say");

	check(mine.standard(0, 0) == plain.of(0) && mine.part(0, 0) == mine.of(0) && mine.part(2, 2) == mine.toward(2, true) &&
	      mine.cabinet_standard(Role::TEST) == plain.cabinet(Role::TEST), "the parts, and their defaults kept");
	line = plain.cabinet_line(Role::COIN1, plain.cabinet(Role::COIN1));
	check(line.key == "coin1" && !line.value, "a coin with its defaults");
	line = plain.cabinet_line(Role::TEST, { key("key:F2") });
	check(line.key == "test" && line.value == "key:F2", "TEST changed");
}

int main(int argc, char **argv)
{
	const std::string dir = (argc > 1) ? argv[1] : std::filesystem::temp_directory_path().string();

	test_settings(dir);
	test_inputs();
	test_defaults();
	test_keys_toward();
	test_analog_lines();
	test_mouse();
	test_game_lines();
	test_own_keys();
	test_own_pad();
	test_text();
	test_lines_to_write();

	std::printf("\n%d checks, %d failures\n", g_checks, g_failures);
	return g_failures ? 1 : 0;
}
