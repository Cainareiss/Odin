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
 *            -I src $(sdl2-config --cflags) tests/control_panel/control_panel_tests.cpp \
 *            src/odin/control_panel.cpp src/odin/bindings.cpp src/odin/settings.cpp src/odin/roles.cpp \
 *            src/odin/save_folder.cpp $(sdl2-config --libs) -o control_panel_tests
 * Exit status is 0 when every check passes. */

#include "odin/control_panel.hpp"

#include <cctype>
#include <cstdio>
#include <initializer_list>

namespace {

using odin::ControlPanel;
using odin::GameControl;
using odin::HostState;
using odin::JvsSwitch;
using odin::MieBoard;
using odin::PadInput;
using odin::PadState;
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
MieBoard::Inputs after(ControlPanel &panel, const std::vector<SDL_Keycode> &keys, const PadState *pad1 = nullptr,
                       const PadState *pad2 = nullptr)
{
	HostState host;
	MieBoard::Inputs inputs = {};

	host.keys = &keys;
	host.pads = { pad1, pad2 };
	panel.update(host);
	panel.fill(inputs);
	return inputs;
}

/* The standard panel with these keys held */
MieBoard::Inputs held(std::initializer_list<SDL_Keycode> keys)
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
	const MieBoard::Inputs none = held({});
	bool centred = true;

	for (const uint16_t channel : none.jvs_analog)
		centred = centred && channel == 0x8000;
	check(!none.mainboard_test && !none.mainboard_service && none.jvs_system == 0 &&
	      none.jvs_player[0] == 0 && none.jvs_player[1] == 0 && !none.jvs_coin[0] && !none.jvs_coin[1],
	      "nothing is pressed without a key");
	check(centred, "the eight analog channels are centred");

	check(held({SDLK_F5}).mainboard_test && held({SDLK_F6}).mainboard_service,
	      "F5 and F6 are Test and Service of the main board");
	check(held({SDLK_F2}).jvs_system == 0x80, "F2 is Test of the I/O board");
	check(held({SDLK_1}).jvs_player[0] == 0x8000 && held({SDLK_9}).jvs_player[0] == 0x4000,
	      "player 1: Start and Service");
	check(held({SDLK_UP}).jvs_player[0] == 0x2000 && held({SDLK_DOWN}).jvs_player[0] == 0x1000 &&
	      held({SDLK_LEFT}).jvs_player[0] == 0x0800 && held({SDLK_RIGHT}).jvs_player[0] == 0x0400,
	      "player 1: the arrows");
	check(held({SDLK_LCTRL}).jvs_player[0] == 0x0200 && held({SDLK_LALT}).jvs_player[0] == 0x0100 &&
	      held({SDLK_LSHIFT}).jvs_player[0] == 0x0080 && held({SDLK_z}).jvs_player[0] == 0x0040 &&
	      held({SDLK_x}).jvs_player[0] == 0x0020 && held({SDLK_c}).jvs_player[0] == 0x0010,
	      "player 1: the six buttons, PUSH3 to PUSH6 in the low byte");
	check(held({SDLK_2}).jvs_player[1] == 0x8000 && held({SDLK_0}).jvs_player[1] == 0x4000 &&
	      held({SDLK_r}).jvs_player[1] == 0x2000 && held({SDLK_f}).jvs_player[1] == 0x1000 &&
	      held({SDLK_d}).jvs_player[1] == 0x0800 && held({SDLK_g}).jvs_player[1] == 0x0400,
	      "player 2: Start, Service and R F D G");
	check(held({SDLK_a}).jvs_player[1] == 0x0200 && held({SDLK_s}).jvs_player[1] == 0x0100 &&
	      held({SDLK_q}).jvs_player[1] == 0x0080 && held({SDLK_w}).jvs_player[1] == 0x0040 &&
	      held({SDLK_e}).jvs_player[1] == 0x0020 && held({SDLK_t}).jvs_player[1] == 0x0010,
	      "player 2: the six buttons");
	check(held({SDLK_1, SDLK_UP, SDLK_z}).jvs_player[0] == 0xA040 && held({SDLK_1, SDLK_UP, SDLK_z}).jvs_player[1] == 0,
	      "the keys held together add up, each player its own word");

	ControlPanel panel({}, Settings(), "bootrom");
	MieBoard::Inputs before = {};
	panel.fill(before);
	check(before.jvs_analog[5] == 0x8000 && before.jvs_player[0] == 0, "before any update: nothing pressed");
}

void test_coins()
{
	ControlPanel panel({}, Settings(), "bootrom");
	MieBoard::Inputs inputs = {};

	inputs = after(panel, {SDLK_5});
	check(inputs.jvs_coin[0] == 1 && inputs.jvs_coin[1] == 0, "5 is a coin in the first slot");
	panel.fill(inputs);
	check(inputs.jvs_coin[0] == 0, "read once");
	check(after(panel, {SDLK_5}).jvs_coin[0] == 0, "not again while the key is held");
	after(panel, {});
	inputs = after(panel, {SDLK_5, SDLK_6});
	check(inputs.jvs_coin[0] == 1 && inputs.jvs_coin[1] == 1, "again at the next press, and 6 is the second slot");
}

void test_game()
{
	ControlPanel panel(cabinet(), Settings(), "sgnascar");
	PadState pad, pad2;

	check(after(panel, {SDLK_z}).jvs_player[0] == 0x1000, "button4 (Z) works VIEW, wired on 1P DOWN");
	check(after(panel, {SDLK_DOWN}).jvs_player[0] == 0, "the arrow does nothing: no control of the game is down");
	check(after(panel, {SDLK_9}).jvs_player[0] == 0x4000 && after(panel, {SDLK_F2}).jvs_system == 0x80 &&
	      after(panel, {SDLK_F5}).mainboard_test, "Service, Test and the main board stay for every game");
	check(after(panel, {SDLK_s}).jvs_player[1] == 0x0080 && after(panel, {SDLK_LALT}).jvs_player[1] == 0,
	      "FOG belongs to player 2: his button2 (S), not that of player 1");

	MieBoard::Inputs inputs = after(panel, {});
	check(inputs.jvs_analog[0] == 0x8000 && inputs.jvs_analog[1] == 0x8000 && inputs.jvs_analog[3] == 0x8000 &&
	      inputs.jvs_analog[4] == 0x8000, "no pad: the channels at 8000");
	inputs = after(panel, {}, &pad);
	check(inputs.jvs_analog[0] == 0x8000 && inputs.jvs_analog[1] == 0x0000 && inputs.jvs_analog[3] == 0x8000,
	      "a pad at rest: wheel and lever centred, gas let go");
	pad.axes[SDL_CONTROLLER_AXIS_LEFTX] = -32768;
	pad.axes[SDL_CONTROLLER_AXIS_TRIGGERRIGHT] = 32767;
	pad.axes[SDL_CONTROLLER_AXIS_LEFTY] = 32767;
	inputs = after(panel, {}, &pad);
	check(inputs.jvs_analog[0] == 0x0000, "the stick left: wheel 0000, low on the left");
	check(inputs.jvs_analog[1] == 0xFFFF, "the trigger pressed: gas FFFF");
	check(inputs.jvs_analog[3] == 0x0001, "the stick down on a lever low when down: 0001");
	pad.axes[SDL_CONTROLLER_AXIS_LEFTY] = -32768;
	check(after(panel, {}, &pad).jvs_analog[3] == 0xFFFF, "up on that lever: FFFF");
	pad = PadState{};

	pad.buttons[SDL_CONTROLLER_BUTTON_Y] = true;
	check(after(panel, {}, &pad).jvs_player[0] == 0x1000, "Y of the pad works VIEW too");
	pad.buttons[SDL_CONTROLLER_BUTTON_Y] = false;
	pad2.buttons[SDL_CONTROLLER_BUTTON_B] = true;
	check(after(panel, {}, &pad, &pad2).jvs_player[1] == 0x0080 && after(panel, {}, &pad2).jvs_player[1] == 0,
	      "B on the pad of player 2 works FOG; on the pad of player 1, nothing");
	pad2 = PadState{};
	pad.axes[SDL_CONTROLLER_AXIS_RIGHTY] = -32768;
	check(after(panel, {}, &pad).jvs_player[1] == 0x2000 && after(panel, {}, &pad).jvs_player[0] == 0,
	      "a control of player 1 wired on a switch of player 2: the right stick up of pad 1");
	pad = PadState{};
	pad.axes[SDL_CONTROLLER_AXIS_LEFTX] = -32768;
	check(after(panel, {}, &pad).jvs_analog[4] == 0x8000 && after(panel, {}, &pad, &pad2).jvs_analog[4] == 0x8000,
	      "the channel of player 2 does not follow pad 1");
	pad2.axes[SDL_CONTROLLER_AXIS_LEFTX] = 32767;
	check(after(panel, {}, &pad, &pad2).jvs_analog[4] == 0xFFFF, "it follows pad 2");
}

void test_lever()
{
	ControlPanel panel(cabinet(), Settings(), "sgnascar");
	PadState pad;

	check(after(panel, {}, &pad).jvs_player[1] == 0, "the lever starts on N");
	pad.buttons[SDL_CONTROLLER_BUTTON_RIGHTSHOULDER] = true;
	check(after(panel, {}, &pad).jvs_player[1] == 0x2800, "RB: 1, its two switches");
	check(after(panel, {}, &pad).jvs_player[1] == 0x2800, "held: no other step");
	pad.buttons[SDL_CONTROLLER_BUTTON_RIGHTSHOULDER] = false;
	after(panel, {}, &pad);
	pad.buttons[SDL_CONTROLLER_BUTTON_RIGHTSHOULDER] = true;
	check(after(panel, {}, &pad).jvs_player[1] == 0x1000, "RB again: 2");
	pad.buttons[SDL_CONTROLLER_BUTTON_RIGHTSHOULDER] = false;
	after(panel, {}, &pad);
	pad.buttons[SDL_CONTROLLER_BUTTON_RIGHTSHOULDER] = true;
	check(after(panel, {}, &pad).jvs_player[1] == 0x1000, "the last stays the last");
	pad = PadState{};
	pad.buttons[SDL_CONTROLLER_BUTTON_LEFTSHOULDER] = true;
	check(after(panel, {}, &pad).jvs_player[1] == 0x2800, "LB: back to 1");
	pad = PadState{};
	after(panel, {}, &pad);
	pad.buttons[SDL_CONTROLLER_BUTTON_LEFTSHOULDER] = true;
	after(panel, {}, &pad);
	pad = PadState{};
	after(panel, {}, &pad);
	pad.buttons[SDL_CONTROLLER_BUTTON_LEFTSHOULDER] = true;
	check(after(panel, {}, &pad).jvs_player[1] == 0, "N, and the first stays the first");
	pad = PadState{};
	pad.buttons[SDL_CONTROLLER_BUTTON_RIGHTSHOULDER] = true;
	after(panel, {}, &pad);
	check(after(panel, {}).jvs_player[1] == 0x2800, "the lever stays where it is when the pad is gone");
}

void test_bindings_from_settings()
{
	Settings settings;
	PadState pad;

	settings.parse("[controls.sgnascar]\nwheel = pad:leftx, pad:rightx\nview = key:Space\n", "test");
	ControlPanel panel(cabinet(), settings, "sgnascar");

	pad.axes[SDL_CONTROLLER_AXIS_LEFTX] = 1000;
	pad.axes[SDL_CONTROLLER_AXIS_RIGHTX] = -20000;
	check(after(panel, {}, &pad).jvs_analog[0] == 0x8000 - 20000, "of two axes, the one furthest from rest");
	pad.axes[SDL_CONTROLLER_AXIS_LEFTX] = 30000;
	check(after(panel, {}, &pad).jvs_analog[0] == 0x8000 + 30000, "whichever it is");
	check(after(panel, {SDLK_SPACE}).jvs_player[0] == 0x1000 && after(panel, {SDLK_z}).jvs_player[0] == 0,
	      "a key of the settings in the place of the default");
}

void test_channel()
{
	using Low = GameControl::Low;
	const PadInput stick{ PadInput::Type::AXIS, SDL_CONTROLLER_AXIS_LEFTX };
	const PadInput trigger{ PadInput::Type::AXIS, SDL_CONTROLLER_AXIS_TRIGGERRIGHT };

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

	const PadInput inverted{ PadInput::Type::AXIS, SDL_CONTROLLER_AXIS_LEFTY, true };

	check(ControlPanel::channel(inverted, Low::DOWN, -32768) == 0x0000 &&
	      ControlPanel::channel(inverted, Low::DOWN, 0) == 0x8000 &&
	      ControlPanel::channel(inverted, Low::DOWN, 32767) == 0xFFFF &&
	      ControlPanel::channel(inverted, Low::LEFT, 32767) == 0x0001,
	      "a stick the settings turn over: the other way, the centre kept");
}

void test_pressed()
{
	Settings settings;
	PadState pad;

	settings.parse("[controls.x]\n1p_push1 = pad:-lefty\n1p_push2 = pad:lefttrigger\n"
	               "1p_push3 = pad:+rightx\n", "test");
	ControlPanel panel({}, settings, "x");

	pad.axes[SDL_CONTROLLER_AXIS_LEFTY] = -16383;
	check(after(panel, {}, &pad).jvs_player[0] == 0, "the stick up less than half way: not pressed");
	pad.axes[SDL_CONTROLLER_AXIS_LEFTY] = -16384;
	check(after(panel, {}, &pad).jvs_player[0] == 0x0200, "half way: pressed");
	pad = PadState{};
	pad.axes[SDL_CONTROLLER_AXIS_TRIGGERLEFT] = 16383;
	check(after(panel, {}, &pad).jvs_player[0] == 0, "a trigger less than half way: not pressed");
	pad.axes[SDL_CONTROLLER_AXIS_TRIGGERLEFT] = 16384;
	check(after(panel, {}, &pad).jvs_player[0] == 0x0100, "half way: pressed");
	pad = PadState{};
	pad.axes[SDL_CONTROLLER_AXIS_RIGHTX] = 16384;
	check(after(panel, {}, &pad).jvs_player[0] == 0x0080, "+rightx half way: pressed");
}

} /* namespace */

int main()
{
	test_standard_panel();
	test_coins();
	test_game();
	test_lever();
	test_bindings_from_settings();
	test_channel();
	test_pressed();

	std::printf("%d checks, %d failures\n", g_checks, g_failures);
	return g_failures ? 1 : 0;
}
