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

#include "odin/roles.hpp"
#include "odin/settings.hpp"

namespace odin {

/* An input of a gamepad, by the name SDL's game controller gives it ("a",
 * "start", "leftshoulder", "leftx", "righttrigger"...), or half the axis
 * of a stick: "-lefty" is the left stick up, "+leftx" to the right. The
 * index is an SDL_GameControllerButton or an SDL_GameControllerAxis. */
struct PadInput {
	enum class Type { BUTTON, AXIS, AXIS_NEGATIVE, AXIS_POSITIVE };

	Type type;
	uint8_t index;

	/* A trigger, from 0 when let go */
	bool is_trigger() const;
	/* A button, half a stick or a trigger: pressed or not */
	bool is_digital() const { return type != Type::AXIS || is_trigger(); }
};

/* What the host gives: a key, by the name SDL gives it ("Left Ctrl",
 * "Z", "F2"), or an input of the gamepad of the player. */
struct HostInput {
	enum class Kind { KEY, PAD };

	Kind kind;
	SDL_Keycode key;
	PadInput pad;

	/* "key:Left Ctrl", "pad:a", "pad:-lefty"; nothing for what is not one */
	static std::optional<HostInput> parse(std::string_view text);
};

/* For each role of each player, and each role of the cabinet, the inputs
 * that work it, any one of them: the defaults, then the lines of
 * [controls] in the settings, then those of [controls.<game>]. A line is
 * "p1.button1 = key:Left Ctrl, pad:a" (a role of player 1 or 2) or
 * "coin1 = key:5" (a role of the cabinet), and takes the place of the
 * inputs of its role; an empty value leaves the role without any. */
class Bindings {
public:
	Bindings();
	Bindings(const Settings &settings, const std::string &game);

	/* The player does not count for a role of the cabinet. */
	const std::vector<HostInput> &of(Role role, unsigned player) const;

	/* A line that is not right is said on stderr and skipped. */
	void apply(const Settings::Lines &lines, const std::string &where);

private:
	std::array<std::array<std::vector<HostInput>, static_cast<size_t>(Role::COUNT)>, 2> m_inputs;
};

} /* namespace odin */

#endif /* ODIN_BINDINGS_HPP */
