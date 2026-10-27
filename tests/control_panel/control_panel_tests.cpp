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

/* Unit tests for the control panel worked through the roles: the keys of
 * the standard panel (those of INSTALL), the coins, the controls of a game,
 * the gamepads, the lever, the analog channels and their way.
 *
 * Build: g++ -std=c++20 -O2 -Wall -Wextra -Wpedantic -Wconversion -Wshadow \
 *            -I src $(pkg-config --cflags sdl3) tests/control_panel/control_panel_tests.cpp \
 *            src/odin/host/input/control_panel.cpp src/odin/host/input/bindings.cpp src/odin/host/app/settings.cpp src/odin/host/input/roles.cpp \
 *            src/odin/host/app/user_files.cpp $(pkg-config --libs sdl3) -o control_panel_tests
 * Exit status is 0 when every check passes. */

#include "odin/host/input/control_panel.hpp"
#include "odin/host/video/picture.hpp"

#include <cctype>
#include <cmath>
#include <cstdio>
#include <initializer_list>
#include <utility>

namespace {

using odin::ControlPanel;
using odin::GameControl;
using odin::HostState;
using odin::JoyState;
using odin::JvsSwitch;
using odin::CabinetInputs;
using odin::MouseState;
using odin::PadInput;
using odin::PadState;
using odin::PadTuning;
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

/* The inputs of a panel after one update */
CabinetInputs after(ControlPanel &panel, const std::vector<SDL_Keycode> &keys, const PadState *pad1 = nullptr,
                       const PadState *pad2 = nullptr, const MouseState *mouse = nullptr,
                       const MouseState *mouse2 = nullptr)
{
	HostState host;
	CabinetInputs inputs = {};

	host.keys = &keys;
	host.pads = { pad1, pad2 };
	host.mice = { mouse, mouse2 };
	panel.update(host);
	panel.fill(inputs);
	return inputs;
}

/* The standard panel with these keys held */
CabinetInputs held(std::initializer_list<SDL_Keycode> keys)
{
	ControlPanel panel({}, Settings(), "bootrom");

	return after(panel, std::vector<SDL_Keycode>(keys));
}

/* Its id: its name in small letters, _ for a space */
GameControl control(const char *name, Role role, uint8_t player)
{
	GameControl c{};

	c.name = name;
	for (const char *n = name; *n; n++)
		c.id += (*n == ' ' || *n == '-') ? '_' : static_cast<char>(std::tolower(static_cast<unsigned char>(*n)));
	c.role = role;
	c.player = player;
	return c;
}

GameControl button(const char *name, JvsSwitch sw, Role role, uint8_t player = 0)
{
	GameControl c = control(name, role, player);

	c.type = GameControl::Type::BUTTON;
	c.button = sw;
	return c;
}

GameControl analog(const char *name, uint8_t channel, GameControl::Low low, Role role, uint8_t player = 0)
{
	GameControl c = control(name, role, player);

	c.type = GameControl::Type::ANALOG;
	c.analog = channel;
	c.low = low;
	return c;
}

/* A cabinet like NASCAR's, with a control of player 2 and a lever read the other way */
std::vector<GameControl> cabinet()
{
	GameControl gear = control("GEAR", Role::GEAR, 0);

	gear.type = GameControl::Type::POSITIONS;
	gear.positions = { { "N", {} }, { "1", { { 1, 0x0800 }, { 1, 0x2000 } } }, { "2", { { 1, 0x1000 } } } };
	return { analog("WHEEL", 0, GameControl::Low::LEFT, Role::STEER),
	         analog("GAS", 1, GameControl::Low::RELEASED, Role::ACCEL),
	         analog("LEVER", 3, GameControl::Low::DOWN, Role::STICK_Y),
	         button("VIEW", { 0, 0x1000 }, Role::BUTTON4),
	         button("FOG", { 1, 0x0080 }, Role::BUTTON2, 1),
	         button("RIGHT-LEVER UP", { 1, 0x2000 }, Role::STICK2_UP),
	         analog("2P AIM", 4, GameControl::Low::LEFT, Role::STICK_X, 1),
	         gear };
}

void test_standard_panel()
{
	const CabinetInputs none = held({});
	bool centred = true;

	for (const uint16_t channel : none.jvs_analog)
		centred = centred && channel == 0x8000;
	check(!none.mainboard_test && !none.mainboard_service && none.jvs_system == 0 &&
	      none.jvs_player[0] == 0 && none.jvs_player[1] == 0 && !none.jvs_coin[0] && !none.jvs_coin[1],
	      "nothing is pressed without a key");
	check(centred, "the eight analog channels are centred");

	check(!held({SDLK_F5}).mainboard_test && !held({SDLK_F6}).mainboard_service && held({SDLK_F2}).jvs_system == 0,
	      "the switches of the main board have no key; F2 is nothing");
	check(held({SDLK_0}).jvs_system == 0x80 && held({SDLK_0}).jvs_player[1] == 0, "0 is Test of the I/O board");
	check(held({SDLK_1}).jvs_player[0] == 0x8000 && held({SDLK_9}).jvs_player[0] == 0x4000,
	      "player 1: Start and Service");
	check(held({SDLK_UP}).jvs_player[0] == 0x2000 && held({SDLK_DOWN}).jvs_player[0] == 0x1000 &&
	      held({SDLK_LEFT}).jvs_player[0] == 0x0800 && held({SDLK_RIGHT}).jvs_player[0] == 0x0400,
	      "player 1: the arrows");
	check(held({SDLK_LCTRL}).jvs_player[0] == 0x0200 && held({SDLK_LALT}).jvs_player[0] == 0x0100 &&
	      held({SDLK_LSHIFT}).jvs_player[0] == 0x0080 && held({SDLK_Z}).jvs_player[0] == 0x0040 &&
	      held({SDLK_X}).jvs_player[0] == 0x0020 && held({SDLK_C}).jvs_player[0] == 0x0010,
	      "player 1: the six buttons, PUSH3 to PUSH6 in the low byte");
	check(held({SDLK_2}).jvs_player[1] == 0x8000 &&
	      held({SDLK_R}).jvs_player[1] == 0x2000 && held({SDLK_F}).jvs_player[1] == 0x1000 &&
	      held({SDLK_D}).jvs_player[1] == 0x0800 && held({SDLK_G}).jvs_player[1] == 0x0400,
	      "player 2: Start and R F D G (the SERVICE of player 2 has no key, only Guide + L3 on its pad)");
	check(held({SDLK_A}).jvs_player[1] == 0x0200 && held({SDLK_S}).jvs_player[1] == 0x0100 &&
	      held({SDLK_Q}).jvs_player[1] == 0x0080 && held({SDLK_W}).jvs_player[1] == 0x0040 &&
	      held({SDLK_E}).jvs_player[1] == 0x0020 && held({SDLK_T}).jvs_player[1] == 0x0010,
	      "player 2: the six buttons");
	check(held({SDLK_1, SDLK_UP, SDLK_Z}).jvs_player[0] == 0xA040 && held({SDLK_1, SDLK_UP, SDLK_Z}).jvs_player[1] == 0,
	      "the keys held together add up, each player its own word");

	ControlPanel panel({}, Settings(), "bootrom");
	CabinetInputs before = {};
	panel.fill(before);
	check(before.jvs_analog[5] == 0x8000 && before.jvs_player[0] == 0, "before any update: nothing pressed");
}

void test_coins()
{
	ControlPanel panel({}, Settings(), "bootrom");
	CabinetInputs inputs = {};

	inputs = after(panel, {SDLK_5});
	check(inputs.jvs_coin[0] == 1 && inputs.jvs_coin[1] == 0, "5 is a coin in the first slot");
	panel.fill(inputs);
	check(inputs.jvs_coin[0] == 0, "read once");
	check(after(panel, {SDLK_5}).jvs_coin[0] == 0, "not again while the key is held");
	after(panel, {});
	inputs = after(panel, {SDLK_5, SDLK_6});
	check(inputs.jvs_coin[0] == 1 && inputs.jvs_coin[1] == 1, "again at the next press, and 6 is the second slot");

	ControlPanel pads({}, Settings(), "bootrom");
	PadState pad1, pad2;

	pad1.buttons[SDL_GAMEPAD_BUTTON_BACK] = true;
	inputs = after(pads, {}, &pad1, &pad2);
	check(inputs.jvs_coin[0] == 1 && inputs.jvs_coin[1] == 0, "SELECT of pad 1: a coin in the first slot");
	pad1 = PadState{};
	pad2.buttons[SDL_GAMEPAD_BUTTON_BACK] = true;
	inputs = after(pads, {}, &pad1, &pad2);
	check(inputs.jvs_coin[0] == 0 && inputs.jvs_coin[1] == 1, "SELECT of pad 2: the second slot");

	/* Guide held with Back pauses Odin: no coin */
	ControlPanel shortcuts({}, Settings(), "bootrom");

	pad1 = pad2 = PadState{};
	pad1.buttons[SDL_GAMEPAD_BUTTON_GUIDE] = pad1.buttons[SDL_GAMEPAD_BUTTON_BACK] = true;
	pad1.shortcut[SDL_GAMEPAD_BUTTON_BACK] = true;
	inputs = after(shortcuts, {}, &pad1, &pad2);
	check(inputs.jvs_coin[0] == 0, "SELECT pressed with Guide held: a shortcut, no coin");
	pad1.buttons[SDL_GAMEPAD_BUTTON_GUIDE] = false;
	inputs = after(shortcuts, {}, &pad1, &pad2);
	check(inputs.jvs_coin[0] == 0, "nor once Guide is let go, while SELECT is held");
	pad1 = PadState{};
	after(shortcuts, {}, &pad1, &pad2);
	pad1.buttons[SDL_GAMEPAD_BUTTON_BACK] = true;
	inputs = after(shortcuts, {}, &pad1, &pad2);
	check(inputs.jvs_coin[0] == 1, "SELECT pressed again alone: a coin");
	pad1 = PadState{};
	pad1.buttons[SDL_GAMEPAD_BUTTON_GUIDE] = pad1.buttons[SDL_GAMEPAD_BUTTON_LEFT_STICK] = true;
	pad1.shortcut[SDL_GAMEPAD_BUTTON_LEFT_STICK] = true;
	size_t service = 0;

	while (service < shortcuts.controls().size() && shortcuts.controls()[service].role != odin::Role::SERVICE)
		service++;
	check(service < shortcuts.controls().size() && shortcuts.touched({ nullptr, { &pad1, nullptr }, nullptr }, service),
	      "Guide with the left stick pressed is still SERVICE");
}

void test_game()
{
	ControlPanel panel(cabinet(), Settings(), "sgnascar");
	PadState pad, pad2;

	check(after(panel, {SDLK_Z}).jvs_player[0] == 0x1000, "button4 (Z) works VIEW, wired on 1P DOWN");
	check(after(panel, {SDLK_DOWN}).jvs_player[0] == 0, "the arrow closes no switch: no control of the game is down");
	check(after(panel, {SDLK_9}).jvs_player[0] == 0x4000 && after(panel, {SDLK_0}).jvs_system == 0x80,
	      "Service and Test stay for every game");
	check(after(panel, {SDLK_S}).jvs_player[1] == 0x0080 && after(panel, {SDLK_LALT}).jvs_player[1] == 0,
	      "FOG belongs to player 2: his button2 (S), not that of player 1");

	CabinetInputs inputs = after(panel, {});
	check(inputs.jvs_analog[0] == 0x8000 && inputs.jvs_analog[3] == 0x8000 && inputs.jvs_analog[4] == 0x8000 &&
	      inputs.jvs_analog[2] == 0x8000, "no pad: wheel, lever and aim centred, a channel of no control at 8000");
	check(inputs.jvs_analog[1] == 0x0000, "no pad: gas let go");
	inputs = after(panel, {}, &pad);
	check(inputs.jvs_analog[0] == 0x8000 && inputs.jvs_analog[1] == 0x0000 && inputs.jvs_analog[3] == 0x8000,
	      "a pad at rest: wheel and lever centred, gas let go");
	pad.axes[SDL_GAMEPAD_AXIS_LEFTX] = -32768;
	pad.axes[SDL_GAMEPAD_AXIS_RIGHT_TRIGGER] = 32767;
	pad.axes[SDL_GAMEPAD_AXIS_LEFTY] = 32767;
	inputs = after(panel, {}, &pad);
	check(inputs.jvs_analog[0] == 0x0000, "the stick left: wheel 0000, low on the left");
	check(inputs.jvs_analog[1] == 0xFFFF, "the trigger pressed: gas FFFF");
	check(inputs.jvs_analog[3] == 0x0001, "the stick down on a lever low when down: 0001");
	pad.axes[SDL_GAMEPAD_AXIS_LEFTY] = -32768;
	check(after(panel, {}, &pad).jvs_analog[3] == 0xFFFF, "up on that lever: FFFF");
	pad = PadState{};

	pad.buttons[SDL_GAMEPAD_BUTTON_NORTH] = true;
	check(after(panel, {}, &pad).jvs_player[0] == 0x1000, "Y of the pad works VIEW too");
	pad.buttons[SDL_GAMEPAD_BUTTON_NORTH] = false;
	pad2.buttons[SDL_GAMEPAD_BUTTON_EAST] = true;
	check(after(panel, {}, &pad, &pad2).jvs_player[1] == 0x0080 && after(panel, {}, &pad2).jvs_player[1] == 0,
	      "B on the pad of player 2 works FOG; on the pad of player 1, nothing");
	pad2 = PadState{};
	pad.axes[SDL_GAMEPAD_AXIS_RIGHTY] = -32768;
	check(after(panel, {}, &pad).jvs_player[1] == 0x2000 && after(panel, {}, &pad).jvs_player[0] == 0,
	      "a control of player 1 wired on a switch of player 2: the right stick up of pad 1");
	pad = PadState{};
	pad.axes[SDL_GAMEPAD_AXIS_LEFTX] = -32768;
	check(after(panel, {}, &pad).jvs_analog[4] == 0x8000 && after(panel, {}, &pad, &pad2).jvs_analog[4] == 0x8000,
	      "the channel of player 2 does not follow pad 1");
	pad2.axes[SDL_GAMEPAD_AXIS_LEFTX] = 32767;
	check(after(panel, {}, &pad, &pad2).jvs_analog[4] == 0xFFFF, "it follows pad 2");
}

/* The keys on the analog controls of a cabinet like NASCAR's */
void test_keys()
{
	ControlPanel panel(cabinet(), Settings(), "sgnascar");

	check(after(panel, {SDLK_LEFT}).jvs_analog[0] == 0x7000, "Left: the wheel a step to the left");
	for (int i = 0; i < 6; i++)
		after(panel, {SDLK_LEFT});
	check(after(panel, {SDLK_LEFT}).jvs_analog[0] == 0x0000, "eight frames: all the way");
	check(after(panel, {SDLK_LEFT}).jvs_analog[0] == 0x0000, "and it stays there");
	check(after(panel, {}).jvs_analog[0] == 0x1000, "let go: a step back");
	for (int i = 0; i < 6; i++)
		after(panel, {});
	check(after(panel, {}).jvs_analog[0] == 0x8000, "eight frames: centred");
	for (int i = 0; i < 7; i++)
		after(panel, {SDLK_RIGHT});
	check(after(panel, {SDLK_RIGHT}).jvs_analog[0] == 0xFFFF, "Right: all the way in eight frames");
	check(after(panel, {SDLK_LEFT, SDLK_RIGHT}).jvs_analog[0] == 0xEFFF, "both: back toward the centre");

	ControlPanel pedal(cabinet(), Settings(), "sgnascar");

	check(after(pedal, {SDLK_UP}).jvs_analog[1] == 0x1000, "Up: the gas a step down");
	for (int i = 0; i < 14; i++)
		after(pedal, {SDLK_UP});
	check(after(pedal, {SDLK_UP}).jvs_analog[1] == 0xFFFF, "sixteen frames: all the way down");
	check(after(pedal, {}).jvs_analog[1] == 0xEFFF, "let go: a step up");

	ControlPanel lever(cabinet(), Settings(), "sgnascar");

	check(after(lever, {SDLK_UP}).jvs_analog[3] == 0x9000, "Up on a lever low when down: toward FFFF");
	for (int i = 0; i < 8; i++)
		after(lever, {});
	for (int i = 0; i < 7; i++)
		after(lever, {SDLK_DOWN});
	check(after(lever, {SDLK_DOWN}).jvs_analog[3] == 0x0001, "Down, eight frames: 0001, as the stick gives it");

	ControlPanel two(cabinet(), Settings(), "sgnascar");

	check(after(two, {SDLK_G}).jvs_analog[4] == 0x9000 && after(two, {SDLK_G}).jvs_analog[0] == 0x8000,
	      "the aim of player 2 on G, and not the wheel of player 1");
	check(after(two, {SDLK_RIGHT}).jvs_analog[4] == 0x9000, "and not on the arrows: back a step");

	ControlPanel both(cabinet(), Settings(), "sgnascar");
	PadState pad;

	after(both, {SDLK_LEFT});
	after(both, {SDLK_LEFT});
	pad.axes[SDL_GAMEPAD_AXIS_LEFTX] = 30000;
	check(after(both, {SDLK_LEFT}, &pad).jvs_analog[0] == 0x8000 + 30000,
	      "the stick moved more than the keys this frame: the stick");
	check(after(both, {SDLK_LEFT}, &pad).jvs_analog[0] == 0x4000, "the stick still, the keys still moving: the keys");
	pad.axes[SDL_GAMEPAD_AXIS_LEFTX] = 30000 + 0x300;
	check(after(both, {}, &pad).jvs_analog[0] == 0x5000,
	      "let go, the keys on their way back, the stick moved less than 400: the keys keep the wheel");
	for (int i = 0; i < 3; i++)
		after(both, {}, &pad);
	pad.axes[SDL_GAMEPAD_AXIS_LEFTX] = 30000 + 0x500;
	check(after(both, {}, &pad).jvs_analog[0] == 0x8000 + 30000 + 0x500,
	      "the keys at rest, the stick 500 from where it was kept, little by little: the stick takes it");
	check(after(both, {}).jvs_analog[0] == 0x8000, "the pad gone: the keys again, at rest");
	check(after(both, {}, &pad).jvs_analog[0] == 0x8000, "the pad back, its stick where it was: the keys keep it");

	ControlPanel back(cabinet(), Settings(), "sgnascar");

	pad = PadState{};
	pad.axes[SDL_GAMEPAD_AXIS_LEFTX] = 30000;
	after(back, {}, &pad);
	after(back, {SDLK_LEFT});
	after(back, {SDLK_LEFT});
	pad.axes[SDL_GAMEPAD_AXIS_LEFTX] = 0;
	check(after(back, {SDLK_LEFT}, &pad).jvs_analog[0] == 0x5000,
	      "a pad back, at rest after it was held right: the keys keep the wheel");

	ControlPanel plugged(cabinet(), Settings(), "sgnascar");

	for (int i = 0; i < 5; i++)
		after(plugged, {SDLK_UP});
	pad = PadState{};
	check(after(plugged, {SDLK_UP}, &pad).jvs_analog[1] == 0x6000,
	      "a pad plugged while Up presses the gas, its trigger let go: the gas goes on down");

	Settings settings;

	settings.parse("[controls.sgnascar]\nwheel_left = key:A\ngas = pad:righttrigger, key:Space\n", "test");
	ControlPanel mine(cabinet(), settings, "sgnascar");

	check(after(mine, {SDLK_A}).jvs_analog[0] == 0x7000, "a key of the settings moves the wheel");
	check(after(mine, {SDLK_LEFT}).jvs_analog[0] == 0x8000, "Left no longer");
	check(after(mine, {SDLK_UP}).jvs_analog[1] == 0x0000 && after(mine, {SDLK_SPACE}).jvs_analog[1] == 0x1000,
	      "the gas on Space, no longer on Up");
	pad = PadState{};
	pad.axes[SDL_GAMEPAD_AXIS_RIGHT_TRIGGER] = 32767;
	check(after(mine, {}, &pad).jvs_analog[1] == 0xFFFF, "and on the trigger of its line");
}

void test_lever()
{
	ControlPanel panel(cabinet(), Settings(), "sgnascar");
	PadState pad;

	check(after(panel, {}, &pad).jvs_player[1] == 0, "the lever starts on N");
	pad.buttons[SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER] = true;
	check(after(panel, {}, &pad).jvs_player[1] == 0x2800, "RB: 1, its two switches");
	check(after(panel, {}, &pad).jvs_player[1] == 0x2800, "held: no other step");
	pad.buttons[SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER] = false;
	after(panel, {}, &pad);
	pad.buttons[SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER] = true;
	check(after(panel, {}, &pad).jvs_player[1] == 0x1000, "RB again: 2");
	pad.buttons[SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER] = false;
	after(panel, {}, &pad);
	pad.buttons[SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER] = true;
	check(after(panel, {}, &pad).jvs_player[1] == 0x1000, "the last stays the last");
	pad = PadState{};
	pad.buttons[SDL_GAMEPAD_BUTTON_LEFT_SHOULDER] = true;
	check(after(panel, {}, &pad).jvs_player[1] == 0x2800, "LB: back to 1");
	pad = PadState{};
	after(panel, {}, &pad);
	pad.buttons[SDL_GAMEPAD_BUTTON_LEFT_SHOULDER] = true;
	after(panel, {}, &pad);
	pad = PadState{};
	after(panel, {}, &pad);
	pad.buttons[SDL_GAMEPAD_BUTTON_LEFT_SHOULDER] = true;
	check(after(panel, {}, &pad).jvs_player[1] == 0, "N, and the first stays the first");
	pad = PadState{};
	pad.buttons[SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER] = true;
	after(panel, {}, &pad);
	check(after(panel, {}).jvs_player[1] == 0x2800, "the lever stays where it is when the pad is gone");

	ControlPanel keys(cabinet(), Settings(), "sgnascar");

	check(after(keys, {SDLK_C}).jvs_player[1] == 0x2800, "C: 1, as RB");
	after(keys, {});
	check(after(keys, {SDLK_X}).jvs_player[1] == 0, "X: back to N, as LB");
	check(after(keys, {SDLK_R}).jvs_player[1] == 0x2000 && after(keys, {SDLK_UP}).jvs_player[1] == 0,
	      "the second stick of player 1 up on R, not on Up");
}

void test_bindings_from_settings()
{
	Settings settings;
	PadState pad;

	settings.parse("[controls.sgnascar]\nwheel = pad:leftx, pad:rightx\nview = key:Space\n", "test");
	ControlPanel panel(cabinet(), settings, "sgnascar");

	pad.axes[SDL_GAMEPAD_AXIS_LEFTX] = 1000;
	pad.axes[SDL_GAMEPAD_AXIS_RIGHTX] = -20000;
	check(after(panel, {}, &pad).jvs_analog[0] == 0x8000 - 20000, "of two axes that moved, the one that moved more");
	pad.axes[SDL_GAMEPAD_AXIS_LEFTX] = 30000;
	check(after(panel, {}, &pad).jvs_analog[0] == 0x8000 + 30000, "then the other when it moves");
	check(after(panel, {}, &pad).jvs_analog[0] == 0x8000 + 30000, "and it keeps the wheel while neither moves");
	check(after(panel, {SDLK_SPACE}).jvs_player[0] == 0x1000 && after(panel, {SDLK_Z}).jvs_player[0] == 0,
	      "a key of the settings in the place of the default");
}

void test_channel()
{
	using Low = GameControl::Low;
	const PadInput stick{ PadInput::Type::AXIS, SDL_GAMEPAD_AXIS_LEFTX };
	const PadInput trigger{ PadInput::Type::AXIS, SDL_GAMEPAD_AXIS_RIGHT_TRIGGER };

	check(ControlPanel::channel(stick, Low::LEFT, -32768) == 0x0000 &&
	      ControlPanel::channel(stick, Low::LEFT, 0) == 0x8000 &&
	      ControlPanel::channel(stick, Low::LEFT, 32767) == 0xFFFF, "a stick: 0000, 8000, FFFF");
	check(ControlPanel::channel(stick, Low::UP, -32768) == 0x0000, "low up: up is low");
	check(ControlPanel::channel(stick, Low::RIGHT, -32768) == 0xFFFF &&
	      ControlPanel::channel(stick, Low::RIGHT, 0) == 0x8000 &&
	      ControlPanel::channel(stick, Low::RIGHT, 32767) == 0x0001, "low right: turned over, centre kept");
	check(ControlPanel::channel(trigger, Low::RELEASED, 0) == 0x0000 &&
	      ControlPanel::channel(trigger, Low::RELEASED, 32767) == 0xFFFF &&
	      ControlPanel::channel(trigger, Low::RELEASED, 16384) == 0x8000 &&
	      ControlPanel::channel(trigger, Low::RELEASED, -5) == 0x0000, "a trigger: 0000 to FFFF");
	check(ControlPanel::channel(trigger, Low::PRESSED, 0) == 0xFFFF, "low pressed: turned over");
	check(ControlPanel::rest(Low::RELEASED) == 0x0000 && ControlPanel::rest(Low::PRESSED) == 0xFFFF &&
	      ControlPanel::rest(Low::LEFT) == 0x8000 && ControlPanel::rest(Low::DOWN) == 0x8000,
	      "at rest: a pedal let go, a stick centred");

	const PadInput inverted{ PadInput::Type::AXIS, SDL_GAMEPAD_AXIS_LEFTY, true };

	check(ControlPanel::channel(inverted, Low::DOWN, -32768) == 0x0000 &&
	      ControlPanel::channel(inverted, Low::DOWN, 0) == 0x8000 &&
	      ControlPanel::channel(inverted, Low::DOWN, 32767) == 0xFFFF &&
	      ControlPanel::channel(inverted, Low::LEFT, 32767) == 0x0001,
	      "a stick the settings turn over: the other way, the centre kept");
}

/* The sight of player 1 of Brave: X low on the left, Y low at the top;
 * WATER on PUSH1, OUT OF SCREEN on PUSH2, FOG on PUSH3 */
void test_mouse()
{
	ControlPanel panel({ analog("1P SIGHT X", 0, GameControl::Low::LEFT, Role::GUN_X),
	                     analog("1P SIGHT Y", 1, GameControl::Low::UP, Role::GUN_Y),
	                     button("1P OUT OF SCREEN", { 0, 0x0100 }, Role::GUN_OFFSCREEN),
	                     button("1P WATER", { 0, 0x0200 }, Role::BUTTON1),
	                     button("1P FOG", { 0, 0x0080 }, Role::BUTTON2),
	                     button("1P START", { 0, 0x8000 }, Role::START) },
	                   Settings(), "braveff");
	MouseState mouse;
	PadState pad;

	check(after(panel, {}, nullptr, nullptr, &mouse).jvs_analog[0] == 0x8000, "the mouse in the middle: 8000");
	mouse.x = 0.0;
	mouse.y = 1.0;
	CabinetInputs inputs = after(panel, {}, nullptr, nullptr, &mouse);
	check(inputs.jvs_analog[0] == 0x0000 && inputs.jvs_analog[1] == 0xFFFF,
	      "the left and the bottom of the picture: 0000 and FFFF");
	mouse.x = 1.0;
	mouse.y = 0.25;
	inputs = after(panel, {}, nullptr, nullptr, &mouse);
	check(inputs.jvs_analog[0] == 0xFFFF && inputs.jvs_analog[1] == 0x4000, "the right, a quarter down");
	mouse.x = 1.5;
	check(after(panel, {}, nullptr, nullptr, &mouse).jvs_analog[0] == 0xFFFF, "beyond the picture: its edge");
	mouse.x = 0.5;
	mouse.buttons = { true, false, false };
	check(after(panel, {}, nullptr, nullptr, &mouse).jvs_player[0] == 0x0200, "the left button: water");
	mouse.buttons = { false, true, true };
	check(after(panel, {}, nullptr, nullptr, &mouse).jvs_player[0] == 0x8080, "the right one fog, the middle one START");
	mouse.buttons = {};
	mouse.off = true;
	check(after(panel, {}, nullptr, nullptr, &mouse).jvs_player[0] == 0x0100, "off the picture: OUT OF SCREEN");
	after(panel, {SDLK_RIGHT}, nullptr, nullptr, &mouse);
	check(after(panel, {SDLK_RIGHT}, nullptr, nullptr, &mouse).jvs_player[0] == 0,
	      "the keys take X, the mouse off still holds Y: it aims no more, no OUT OF SCREEN");
	pad.axes[SDL_GAMEPAD_AXIS_LEFTX] = 20000;
	pad.axes[SDL_GAMEPAD_AXIS_LEFTY] = 20000;
	inputs = after(panel, {}, &pad, nullptr, &mouse);
	check(inputs.jvs_analog[0] == 0x8000 + 20000 && inputs.jvs_analog[1] == 0x8000 + 20000,
	      "the stick moves: it takes the sight");
	check(after(panel, {}, &pad, nullptr, &mouse).jvs_player[0] == 0, "the stick aims: the mouse off counts no more");
	mouse.off = false;
	mouse.x = 0.25;
	check(after(panel, {}, &pad, nullptr, &mouse).jvs_analog[0] == 0x4000, "the mouse moves: it takes X back");
	check(after(panel, {}, &pad).jvs_analog[0] == 0x8000 && after(panel, {}, &pad).jvs_player[0] == 0,
	      "no mouse, as a pad unplugged: the keys at rest, its buttons up");
}

/* The light gun of player 2 aims for player 2 alone, with the buttons of
 * player 1's mouse: WATER on PUSH1, OUT OF SCREEN on PUSH2, FOG on PUSH3 */
void test_gun_of_player_2()
{
	ControlPanel panel({ analog("1P SIGHT X", 0, GameControl::Low::LEFT, Role::GUN_X),
	                     analog("1P SIGHT Y", 1, GameControl::Low::UP, Role::GUN_Y),
	                     button("1P WATER", { 0, 0x0200 }, Role::BUTTON1),
	                     analog("2P SIGHT X", 2, GameControl::Low::LEFT, Role::GUN_X, 1),
	                     analog("2P SIGHT Y", 3, GameControl::Low::UP, Role::GUN_Y, 1),
	                     button("2P OUT OF SCREEN", { 1, 0x0100 }, Role::GUN_OFFSCREEN, 1),
	                     button("2P WATER", { 1, 0x0200 }, Role::BUTTON1, 1),
	                     button("2P FOG", { 1, 0x0080 }, Role::BUTTON2, 1) },
	                   Settings(), "braveff");
	MouseState gun;

	gun.x = 0.25;
	gun.y = 0.75;
	gun.buttons = { true, false, true };
	CabinetInputs inputs = after(panel, {}, nullptr, nullptr, nullptr, &gun);
	check(inputs.jvs_analog[2] == 0x4000 && inputs.jvs_analog[3] == 0xBFFF && inputs.jvs_player[1] == 0x0280,
	      "the gun of player 2: its sight, water and fog");
	check(inputs.jvs_analog[0] == 0x8000 && inputs.jvs_player[0] == 0, "player 1 at rest");
	gun.buttons = {};
	gun.off = true;
	check(after(panel, {}, nullptr, nullptr, nullptr, &gun).jvs_player[1] == 0x0100,
	      "off the picture: OUT OF SCREEN of player 2");
	MouseState mouse;

	mouse.buttons = { true, false, false };
	inputs = after(panel, {}, nullptr, nullptr, &mouse, nullptr);
	check(inputs.jvs_player[0] == 0x0200 && inputs.jvs_player[1] == 0, "the mouse of player 1 is not player 2's");
}

/* The mouse is one pointer: moving it across takes the Y of the sight too */
void test_mouse_one_pointer()
{
	ControlPanel panel({ analog("1P SIGHT X", 0, GameControl::Low::LEFT, Role::GUN_X),
	                     analog("1P SIGHT Y", 1, GameControl::Low::UP, Role::GUN_Y),
	                     button("1P OUT OF SCREEN", { 0, 0x0100 }, Role::GUN_OFFSCREEN) },
	                   Settings(), "braveff");
	MouseState mouse;
	PadState pad;

	after(panel, {}, &pad, nullptr, &mouse);
	pad.axes[SDL_GAMEPAD_AXIS_LEFTY] = 20000;
	check(after(panel, {}, &pad, nullptr, &mouse).jvs_analog[1] == 0x8000 + 20000, "the stick takes Y");
	mouse.x = 0.25;
	CabinetInputs inputs = after(panel, {}, &pad, nullptr, &mouse);
	check(inputs.jvs_analog[0] == 0x4000 && inputs.jvs_analog[1] == 0x8000,
	      "the mouse moved across only: it takes X and Y back");
	mouse.off = true;
	after(panel, {}, &pad, nullptr, &mouse);
	check(after(panel, {}, &pad, nullptr, &mouse).jvs_player[0] == 0x0100, "then off the picture: OUT OF SCREEN");
	pad.axes[SDL_GAMEPAD_AXIS_LEFTY] = -20000;
	check(after(panel, {}, &pad, nullptr, &mouse).jvs_analog[1] == 0x8000 - 20000, "the stick takes Y again");
	mouse.x = 0.25 + 0x200 / 65536.0;
	check(after(panel, {}, &pad, nullptr, &mouse).jvs_analog[1] == 0x8000 - 20000,
	      "a move of the mouse under 400: Y stays with the stick");
}

void test_picture()
{
	const odin::PictureRect whole = odin::picture_rect(640, 480);
	const odin::PictureRect wide = odin::picture_rect(1280, 720);
	const odin::PictureRect tall = odin::picture_rect(800, 1000);

	check(whole.x == 0 && whole.y == 0 && whole.w == 640 && whole.h == 480, "640 by 480: the whole window");
	check(wide.x == 160 && wide.y == 0 && wide.w == 960 && wide.h == 720, "a wide window: black on the sides");
	check(tall.x == 0 && tall.y == 200 && tall.w == 800 && tall.h == 600, "a tall window: black above and below");

	const odin::PicturePoint corner = odin::picture_point(160, 719, 1280, 720);
	const odin::PicturePoint last = odin::picture_point(1119, 360, 1280, 720);

	check(corner.x > 0.0 && corner.x < 0.001 && corner.y > 0.999 && corner.y < 1.0,
	      "the bottom left pixel of the picture");
	check(last.x > 0.999 && last.x < 1.0 && last.y > 0.5 && last.y < 0.501, "the last pixel of a line");
	check(odin::picture_point(159, 0, 1280, 720).x < 0.0 && odin::picture_point(1120, 0, 1280, 720).x >= 1.0,
	      "beside the picture: off it");
}

void test_pressed()
{
	Settings settings;
	PadState pad;

	settings.parse("[controls.x]\n1p_push1 = pad:-lefty\n1p_push2 = pad:lefttrigger\n"
	               "1p_push3 = pad:+rightx\n", "test");
	ControlPanel panel({}, settings, "x");

	pad.axes[SDL_GAMEPAD_AXIS_LEFTY] = -16383;
	check(after(panel, {}, &pad).jvs_player[0] == 0, "the stick up less than half way: not pressed");
	pad.axes[SDL_GAMEPAD_AXIS_LEFTY] = -16384;
	check(after(panel, {}, &pad).jvs_player[0] == 0x0200, "half way: pressed");
	pad = PadState{};
	pad.axes[SDL_GAMEPAD_AXIS_LEFT_TRIGGER] = 982;
	check(after(panel, {}, &pad).jvs_player[0] == 0, "a trigger short of its threshold (3 %): not pressed");
	pad.axes[SDL_GAMEPAD_AXIS_LEFT_TRIGGER] = 983;
	check(after(panel, {}, &pad).jvs_player[0] == 0x0100, "at its threshold: pressed");
	pad.trigger_pressed = PadTuning{ 3, 50 }.trigger_pressed();
	pad.axes[SDL_GAMEPAD_AXIS_LEFT_TRIGGER] = 16382;
	check(after(panel, {}, &pad).jvs_player[0] == 0, "a threshold of 50 %: not pressed short of half way");
	pad.axes[SDL_GAMEPAD_AXIS_LEFT_TRIGGER] = 16383;
	check(after(panel, {}, &pad).jvs_player[0] == 0x0100, "pressed from there");
	pad = PadState{};
	pad.axes[SDL_GAMEPAD_AXIS_RIGHTX] = 16384;
	check(after(panel, {}, &pad).jvs_player[0] == 0x0080, "+rightx half way: pressed");
}

/* The dead zone and the trigger threshold of a gamepad: [gamepads], then
 * [gamepad.<GUID>]; a stick shaped round its centre */
void test_tuning()
{
	Settings settings;

	settings.parse("[gamepads]\ndead_zone = 10\n[gamepad.abc]\ntrigger_threshold = 20\n"
	               "[gamepad.bad]\ndead_zone = 31\ntrigger_threshold = 5x\nwhat = 1\n", "test");

	const PadTuning none = PadTuning::of(Settings(), "abc", false), all = PadTuning::of(settings, "", false);
	const PadTuning abc = PadTuning::of(settings, "abc", false), bad = PadTuning::of(settings, "bad", false);

	check(none.dead_zone == 3 && none.trigger_threshold == 3, "by default: 3 % and 3 %");
	check(all.dead_zone == 10 && all.trigger_threshold == 3, "[gamepads] for every gamepad");
	check(abc.dead_zone == 10 && abc.trigger_threshold == 20, "[gamepad.<GUID>] over it for its make and model");
	check(bad.dead_zone == 10 && bad.trigger_threshold == 3, "past its end, not a number: refused");

	const PadTuning ten{ 10, 3 }, three{ 3, 3 }, zero{ 0, 3 };
	const auto shaped = [](const PadTuning &tuning, int16_t x, int16_t y) {
		tuning.shape(x, y);
		return std::pair<int, int>(x, y);
	};

	check(shaped(ten, 3000, 0) == std::pair(0, 0) && shaped(ten, 2000, 2000) == std::pair(0, 0),
	      "inside the dead zone, a circle: at rest");
	check(shaped(ten, 16384, 0) == std::pair(14564, 0), "past it: from 0 at its edge");
	check(shaped(ten, 32767, 0) == std::pair(32767, 0) && shaped(ten, -32768, 0) == std::pair(-32768, 0) &&
	      shaped(ten, 32767, 32767) == std::pair(32767, 32767), "all the way: all the way, the corners too");
	check(shaped(three, 983, 0) == std::pair(0, 0) && shaped(three, 1000, 0) == std::pair(18, 0),
	      "3 %: a stick at rest stays there");
	check(shaped(zero, 3000, -5) == std::pair(3000, -5), "none: as the stick gives it");
	check(PadTuning{ 3, 0 }.trigger_pressed() == 1 && three.trigger_pressed() == 983 &&
	      PadTuning{ 3, 50 }.trigger_pressed() == 16383, "a trigger presses from its threshold, 0 %: once it moves");
}

} /* namespace */

/* What the menu lights: a control whose input is held now, a stick of an
 * analog control from half its way, a role of the cabinet */
void test_touched()
{
	const ControlPanel panel(cabinet(), Settings(), "sgnascar");
	const auto index = [&panel](const char *id) {
		for (size_t c = 0; c < panel.controls().size(); c++)
			if (panel.controls()[c].id == id)
				return c;
		return panel.controls().size();
	};
	const std::vector<SDL_Keycode> none, z = { SDLK_Z }, x = { SDLK_X }, five = { SDLK_5 };
	PadState pad;
	HostState host;

	host.keys = &none;
	host.pads[0] = &pad;
	check(!panel.touched(host, index("view")) && !panel.touched(host, index("wheel")) &&
	      !panel.touched(host, Role::COIN1), "nothing held: nothing touched");
	host.keys = &z;
	check(panel.touched(host, index("view")) && !panel.touched(host, index("fog")), "Z: VIEW, not FOG");
	host.keys = &x;
	check(panel.touched(host, index("gear")), "X: the lever, down");
	host.keys = &five;
	check(panel.touched(host, Role::COIN1) && !panel.touched(host, Role::COIN2), "5: the coin of player 1");
	host.keys = &none;
	pad.axes[SDL_GAMEPAD_AXIS_LEFTX] = -20000;
	check(panel.touched(host, index("wheel")), "the stick from half its way: the wheel");
	pad.axes[SDL_GAMEPAD_AXIS_LEFTX] = -10000;
	check(!panel.touched(host, index("wheel")), "less: not yet");
	pad.axes[SDL_GAMEPAD_AXIS_LEFTX] = 0;
	pad.buttons[SDL_GAMEPAD_BUTTON_NORTH] = true;
	check(panel.touched(host, index("view")), "the pad of player 1: Y, VIEW");
	check(!panel.touched(host, index("fog")), "a control of player 2 is not worked by the pad of player 1");
	pad.buttons[SDL_GAMEPAD_BUTTON_NORTH] = false;
	pad.buttons[SDL_GAMEPAD_BUTTON_RIGHT_STICK] = true;
	check(!panel.touched(host, Role::TEST), "R3 alone: not TEST");
	pad.buttons[SDL_GAMEPAD_BUTTON_GUIDE] = true;
	check(panel.touched(host, Role::TEST), "Guide held with R3: TEST");
	pad.buttons[SDL_GAMEPAD_BUTTON_RIGHT_STICK] = false;
	check(!panel.touched(host, Role::TEST), "Guide alone: nothing");
}

void test_now()
{
	ControlPanel panel(cabinet(), Settings(), "sgnascar");
	const auto index = [&panel](const char *id) {
		for (size_t c = 0; c < panel.controls().size(); c++)
			if (panel.controls()[c].id == id)
				return c;
		return panel.controls().size();
	};
	const size_t wheel = index("wheel"), gas = index("gas"), gear = index("gear"), view = index("view");
	const std::vector<SDL_Keycode> none, left = { SDLK_LEFT }, up = { SDLK_UP };
	PadState pad;
	HostState host;

	host.keys = &none;
	host.pads[0] = &pad;
	check(!panel.now(host, wheel).on && panel.now(host, wheel).value == 0.0f && panel.now(host, gas).value == 0.0f &&
	      panel.now(host, gear).position == 0, "nothing held: the wheel centred, the gas let go, the lever on N");
	host.keys = &left;
	check(panel.now(host, wheel).on && panel.now(host, wheel).value == -1.0f, "Left: the wheel at the end of its way, left");
	host.keys = &up;
	check(panel.now(host, gas).on && panel.now(host, gas).value == 1.0f, "Up: the pedal all the way down");
	host.keys = &none;
	pad.axes[SDL_GAMEPAD_AXIS_LEFTX] = 16384;
	check(std::abs(panel.now(host, wheel).value - 0.5f) < 0.001f, "the stick half way right: the wheel half way right");
	pad.axes[SDL_GAMEPAD_AXIS_RIGHT_TRIGGER] = 32767;
	check(panel.now(host, gas).value == 1.0f, "the trigger pulled: the pedal down");
	host.keys = &left;
	pad.axes[SDL_GAMEPAD_AXIS_LEFTX] = 30000;
	check(panel.now(host, wheel).value == -1.0f, "the key further than the stick: the key");
	host.keys = &none;
	pad = PadState{};
	pad.buttons[SDL_GAMEPAD_BUTTON_NORTH] = true;
	check(panel.now(host, view).on && panel.now(host, view).value == 0.0f, "a button: worked, no value");
	pad = PadState{};
	pad.buttons[SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER] = true;
	check(panel.now(host, gear).position == 0, "the lever does not move without an update");
	after(panel, {}, &pad);
	check(panel.now(host, gear).position == 1 && panel.now(host, gear).on, "after the update: on 1, worked");
}

/* A wheel read as a joystick, its pedals at the top of their axes at
 * rest (a G29 on Linux: wheel 0, clutch 1, gas 2, brake 3, 25 buttons, a
 * hat): steers on its first axis by default; its gas and its buttons as
 * the settings give them */
void test_joystick()
{
	Settings settings;

	settings.parse("[controls.sgnascar]\n"
	               "gas = joy:axis2:invert\n"
	               "view = joy:button3, joy:hat0_up\n"
	               "gear_up = joy:button4\n"
	               "fog = joy:+axis1\n", "test");

	ControlPanel panel(cabinet(), settings, "sgnascar");
	const auto index = [&panel](const char *id) {
		for (size_t c = 0; c < panel.controls().size(); c++)
			if (panel.controls()[c].id == id)
				return c;
		return panel.controls().size();
	};
	const std::vector<SDL_Keycode> none;
	JoyState wheel;
	HostState host;
	CabinetInputs inputs = {};
	const auto update = [&] {
		panel.update(host);
		panel.fill(inputs);
	};

	wheel.axes = { -335, 32767, 32767, 32767 };
	wheel.known = { true, true, true, true };
	wheel.buttons.assign(25, false);
	wheel.hats = { 0 };
	host.keys = &none;
	host.joys[0] = &wheel;
	update();
	check(inputs.jvs_analog[0] == 0x8000 && inputs.jvs_analog[1] == 0x0000,
	      "at rest, off its centre: the wheel and the gas take nothing before they move");
	wheel.axes[0] = -16384;
	wheel.axes[2] = -1;
	update();
	check(inputs.jvs_analog[0] == 0x4000, "the wheel half way left: its channel half way down");
	check(inputs.jvs_analog[1] == 0x8000, "the gas half way down, turned over: half its channel");
	check(panel.touched(host, index("gas")) && panel.touched(host, index("wheel")), "both worked, from half their way");
	check(std::abs(panel.now(host, index("gas")).value - 0.5f) < 0.001f &&
	      std::abs(panel.now(host, index("wheel")).value + 0.5f) < 0.001f, "and drawn there");
	wheel.axes[2] = -32768;
	update();
	check(inputs.jvs_analog[1] == 0xFFFF && panel.now(host, index("gas")).value == 1.0f, "the gas all the way down");
	wheel.axes[2] = 32767;
	update();
	check(inputs.jvs_analog[1] == 0x0000 && !panel.touched(host, index("gas")), "let go: back to its very rest");

	wheel.buttons[3] = true;
	update();
	check(inputs.jvs_player[0] & 0x1000, "button 3: VIEW");
	wheel.buttons[3] = false;
	wheel.hats[0] = SDL_HAT_UP | SDL_HAT_LEFT;
	update();
	check(inputs.jvs_player[0] & 0x1000, "the hat up, and left with it: VIEW");
	wheel.hats[0] = 0;
	wheel.buttons[4] = true;
	update();
	check(panel.now(host, index("gear")).position == 1, "button 4: a gear up");
	check(!(inputs.jvs_player[1] & 0x0080), "a control of player 2: not by the joystick of player 1");

	JoyState other = wheel;

	other.axes[1] = 20000;
	host.joys = { nullptr, &other };
	update();
	check(inputs.jvs_player[1] & 0x0080, "half an axis of the joystick of player 2: FOG");
	other.axes[1] = 10000;
	update();
	check(!(inputs.jvs_player[1] & 0x0080), "less than half: not pressed");

	ControlPanel unknown(cabinet(), Settings(), "sgnascar");
	JoyState still;

	still.axes = { -20000 };
	still.known = { false };
	host.joys = { &still, nullptr };
	unknown.update(host);
	unknown.fill(inputs);
	check(inputs.jvs_analog[0] == 0x8000, "an axis SDL has no value of yet: nothing");
	still.known = { true };
	still.axes = { -20000 };
	unknown.update(host);
	unknown.fill(inputs);
	check(inputs.jvs_analog[0] == 0x8000, "its first value, far from the centre: still nothing");
	still.axes = { -8000 };
	unknown.update(host);
	unknown.fill(inputs);
	check(inputs.jvs_analog[0] == 0x8000 - 8000, "its first value, then a move: the wheel");
}

int main()
{
	test_standard_panel();
	test_coins();
	test_game();
	test_keys();
	test_lever();
	test_bindings_from_settings();
	test_channel();
	test_mouse();
	test_gun_of_player_2();
	test_mouse_one_pointer();
	test_picture();
	test_pressed();
	test_tuning();
	test_touched();
	test_now();
	test_joystick();

	std::printf("%d checks, %d failures\n", g_checks, g_failures);
	return g_failures ? 1 : 0;
}
