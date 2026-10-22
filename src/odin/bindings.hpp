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

#ifndef ODIN_BINDINGS_HPP
#define ODIN_BINDINGS_HPP

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <SDL3/SDL.h>

#include "odin/games.hpp"
#include "odin/roles.hpp"
#include "odin/settings.hpp"

namespace odin {

/* An input of a gamepad, by the name SDL's gamepad API gives it ("a",
 * "start", "leftshoulder", "leftx", "righttrigger"...), half the axis of a
 * stick: "-lefty" is the left stick up, "+leftx" to the right, the axis of
 * a stick turned over: "lefty:invert", or a button held with another one
 * first: "guide+leftstick". The index is an SDL_GamepadButton or an
 * SDL_GamepadAxis. */
struct PadInput {
	enum class Type { BUTTON, AXIS, AXIS_NEGATIVE, AXIS_POSITIVE };

	Type type;
	uint8_t index;
	bool inverted = false;	/* a whole axis of a stick, turned over */
	int8_t with = -1;	/* the button held with a button, none: -1 */

	/* A trigger, from 0 when let go */
	bool is_trigger() const;
	/* A button, half a stick or a trigger: pressed or not */
	bool is_digital() const { return type != Type::AXIS || is_trigger(); }
};

/* The mouse: a button, "left", "middle" or "right"; where it points on the
 * picture, "x" or "y"; "off", pointing away from the picture while it
 * aims. */
struct MouseInput {
	enum class Type { BUTTON, X, Y, OFF };

	Type type;
	uint8_t button;	/* 0 left, 1 middle, 2 right */

	/* Where it points: an axis */
	bool is_axis() const { return type == Type::X || type == Type::Y; }
};

/* An input of a joystick that is no gamepad (a wheel, its pedals, its
 * shifter, a flight stick), by its numbers in SDL's joystick API, those
 * EmulationStation gives it on Batocera: "button12"; an axis whole,
 * "axis0", or turned over, "axis2:invert" (a pedal at rest at the top of
 * its axis); half an axis, "-axis1", "+axis1"; a way of a hat, "hat0_up",
 * "_down", "_left", "_right". */
struct JoyInput {
	enum class Type { BUTTON, AXIS, AXIS_NEGATIVE, AXIS_POSITIVE, HAT };

	Type type;
	uint8_t index;
	bool inverted = false;	/* a whole axis, turned over */
	uint8_t way = 0;	/* of a hat: SDL_HAT_UP, _RIGHT, _DOWN or _LEFT */

	/* A button, half an axis or a way of a hat: pressed or not */
	bool is_digital() const { return type != Type::AXIS; }
};

/* What the host gives: a key, by the name SDL gives it ("Left Ctrl",
 * "Z", "F2"), an input of the gamepad of the player, of their joystick, or
 * of the mouse. */
struct HostInput {
	enum class Kind { KEY, PAD, MOUSE, JOY };

	Kind kind;
	SDL_Keycode key;
	PadInput pad;
	MouseInput mouse;
	JoyInput joy;

	/* "key:Left Ctrl", "pad:a", "pad:-lefty", "pad:lefty:invert",
	 * "pad:guide+leftstick", "mouse:left", "mouse:x", "joy:axis0",
	 * "joy:button4"; nothing for what is not one */
	static std::optional<HostInput> parse(std::string_view text);
	/* And back: what parse reads; a list with ", " between */
	std::string text() const;
	static std::string text(const std::vector<HostInput> &inputs);
	bool operator==(const HostInput &other) const { return text() == other.text(); }
	/* The axis of a pad or of a joystick, or where the mouse points: a
	 * channel */
	bool is_axis() const
	{
		switch (kind) {
		case Kind::PAD: return pad.type == PadInput::Type::AXIS;
		case Kind::JOY: return joy.type == JoyInput::Type::AXIS;
		case Kind::MOUSE: return mouse.is_axis();
		case Kind::KEY: break;
		}
		return false;
	}
};

/* The inputs that work each control of a panel (any one of them) and each
 * control of the cabinet: the defaults of their roles (the keys as INSTALL
 * tells them, the gamepad of the player and the first axis of their
 * joystick for the wheel, or the keys and the inputs of the gamepad the
 * list of games gives a button), then the lines of
 * [controls.<game>] in the settings, "<id> = key:Left Ctrl, pad:a" (a lever
 * has "<id>_down" and "<id>_up"; an analog control the axes of a pad, and
 * what moves it one way or the other on the halves of its way, "<id>_left"
 * and "<id>_right" or "<id>_up" and "<id>_down", a pedal on its own line
 * with its axes; the cabinet "coin1", "coin2", "test", "board_test",
 * "board_service"). A line takes the place of the inputs of its control,
 * an empty value leaves it without any; a line that is not right is said
 * on stderr and skipped. */
class Bindings {
public:
	Bindings(const std::vector<GameControl> &panel, const Settings &settings, const std::string &game);

	/* The inputs of a control of the panel, by its place in the panel: an
	 * analog control the axes of a pad */
	const std::vector<HostInput> &of(size_t control) const { return m_controls[control][0]; }
	/* Those that move a lever down or up */
	const std::vector<HostInput> &lever(size_t control, bool up) const { return m_controls[control][1 + up]; }
	/* Those that move an analog control left or up, right or down
	 * (positive), a pedal down (positive) */
	const std::vector<HostInput> &toward(size_t control, bool positive) const
	{
		return m_controls[control][1 + positive];
	}
	/* Those of a role of the cabinet */
	const std::vector<HostInput> &cabinet(Role role) const;
	/* A part of a control (0 the control, 1 and 2 its halves or a lever
	 * down and up), and the defaults of the part or of a role of the
	 * cabinet, before the settings */
	const std::vector<HostInput> &part(size_t control, unsigned part) const { return m_controls[control][part]; }
	const std::vector<HostInput> &standard(size_t control, unsigned part) const { return m_defaults[control][part]; }
	const std::vector<HostInput> &cabinet_standard(Role role) const;

	/* A line of [controls.<game>]: its key, and its value; none when the
	 * inputs are the defaults (the line can go) */
	struct Line {
		std::string key;
		std::optional<std::string> value;
	};
	/* The line that gives those inputs to a part of a control: 0 the
	 * control (the axes of an analog one), 1 and 2 a lever down and up, an
	 * analog control left or up and right or down, 2 what presses a pedal.
	 * A pedal has one line for its axes and what presses it: the part not
	 * given stays as it is. */
	Line line(size_t control, unsigned part, const std::vector<HostInput> &inputs) const;
	Line cabinet_line(Role role, const std::vector<HostInput> &inputs) const;
	/* Every line of [controls.<game>] at the defaults, those of the
	 * controls in the order of the panel, then those of the cabinet: its
	 * key and its value */
	std::vector<std::pair<std::string, std::string>> standard_lines() const;

	/* The defaults of a role of a player; for a role of the cabinet the
	 * player does not count. */
	static std::vector<HostInput> defaults(Role role, unsigned player);
	/* The keys that move an analog control of that role and player left
	 * or up, right or down (positive), a pedal down (positive) */
	static std::vector<HostInput> keys_toward(Role role, unsigned player, bool positive);

private:
	std::vector<GameControl> m_panel;
	/* As the settings leave them, and before them: the defaults */
	std::vector<std::array<std::vector<HostInput>, 3>> m_controls, m_defaults;
	std::array<std::vector<HostInput>, 5> m_cabinet, m_cabinet_defaults;
};

} /* namespace odin */

#endif /* ODIN_BINDINGS_HPP */
