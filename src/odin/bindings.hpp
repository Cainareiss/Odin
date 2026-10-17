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

#include <SDL.h>

#include "odin/games.hpp"
#include "odin/roles.hpp"
#include "odin/settings.hpp"

namespace odin {

/* An input of a gamepad, by the name SDL's game controller gives it ("a",
 * "start", "leftshoulder", "leftx", "righttrigger"...), half the axis of a
 * stick: "-lefty" is the left stick up, "+leftx" to the right, or the axis
 * of a stick turned over: "lefty:invert". The index is an
 * SDL_GameControllerButton or an SDL_GameControllerAxis. */
struct PadInput {
	enum class Type { BUTTON, AXIS, AXIS_NEGATIVE, AXIS_POSITIVE };

	Type type;
	uint8_t index;
	bool inverted = false;	/* a whole axis of a stick, turned over */

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

/* What the host gives: a key, by the name SDL gives it ("Left Ctrl",
 * "Z", "F2"), an input of the gamepad of the player, or of the mouse. */
struct HostInput {
	enum class Kind { KEY, PAD, MOUSE };

	Kind kind;
	SDL_Keycode key;
	PadInput pad;
	MouseInput mouse;

	/* "key:Left Ctrl", "pad:a", "pad:-lefty", "pad:lefty:invert",
	 * "mouse:left", "mouse:x"; nothing for what is not one */
	static std::optional<HostInput> parse(std::string_view text);
	/* The axis of a pad or where the mouse points: a channel */
	bool is_axis() const
	{
		return kind == Kind::PAD ? pad.type == PadInput::Type::AXIS : kind == Kind::MOUSE && mouse.is_axis();
	}
};

/* The inputs that work each control of a panel (any one of them) and each
 * control of the cabinet: the defaults of their roles (the keys as INSTALL
 * tells them, the gamepad of the player), then the lines of
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

	/* The defaults of a role of a player; for a role of the cabinet the
	 * player does not count. */
	static std::vector<HostInput> defaults(Role role, unsigned player);
	/* The keys that move an analog control of that role and player left
	 * or up, right or down (positive), a pedal down (positive) */
	static std::vector<HostInput> keys_toward(Role role, unsigned player, bool positive);

private:
	std::vector<std::array<std::vector<HostInput>, 3>> m_controls;
	std::array<std::vector<HostInput>, 5> m_cabinet;
};

} /* namespace odin */

#endif /* ODIN_BINDINGS_HPP */
