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

#include "odin/bindings.hpp"

#include <cstdio>

namespace odin {

namespace {

/* The keys of the cabinet as INSTALL tells them, and the gamepad of each
 * player. */
struct Default {
	Role role;
	const char *key1;	/* the key of player 1, or of the cabinet */
	const char *key2;	/* the key of player 2 */
	const char *pad;
};

const Default DEFAULTS[] = {
	{ Role::START, "1", "2", "start" },
	{ Role::SERVICE, "9", "0", nullptr },
	{ Role::UP, "Up", "R", "dpup" },
	{ Role::DOWN, "Down", "F", "dpdown" },
	{ Role::LEFT, "Left", "D", "dpleft" },
	{ Role::RIGHT, "Right", "G", "dpright" },
	{ Role::BUTTON1, "Left Ctrl", "A", "a" },
	{ Role::BUTTON2, "Left Alt", "S", "b" },
	{ Role::BUTTON3, "Left Shift", "Q", "x" },
	{ Role::BUTTON4, "Z", "W", "y" },
	{ Role::BUTTON5, "X", "E", "leftshoulder" },
	{ Role::BUTTON6, "C", "T", "rightshoulder" },
	{ Role::BUTTON7, nullptr, nullptr, "lefttrigger" },
	{ Role::BUTTON8, nullptr, nullptr, "righttrigger" },
	{ Role::STICK_UP, nullptr, nullptr, "-lefty" },
	{ Role::STICK_DOWN, nullptr, nullptr, "+lefty" },
	{ Role::STICK_LEFT, nullptr, nullptr, "-leftx" },
	{ Role::STICK_RIGHT, nullptr, nullptr, "+leftx" },
	{ Role::STICK2_UP, nullptr, nullptr, "-righty" },
	{ Role::STICK2_DOWN, nullptr, nullptr, "+righty" },
	{ Role::STICK2_LEFT, nullptr, nullptr, "-rightx" },
	{ Role::STICK2_RIGHT, nullptr, nullptr, "+rightx" },
	{ Role::GEAR_UP, nullptr, nullptr, "rightshoulder" },
	{ Role::GEAR_DOWN, nullptr, nullptr, "leftshoulder" },
	{ Role::STEER, nullptr, nullptr, "leftx" },
	{ Role::ACCEL, nullptr, nullptr, "righttrigger" },
	{ Role::BRAKE, nullptr, nullptr, "lefttrigger" },
	{ Role::STICK_X, nullptr, nullptr, "leftx" },
	{ Role::STICK_Y, nullptr, nullptr, "lefty" },
	{ Role::STICK2_X, nullptr, nullptr, "rightx" },
	{ Role::STICK2_Y, nullptr, nullptr, "righty" },
	{ Role::COIN1, "5", nullptr, nullptr },
	{ Role::COIN2, "6", nullptr, nullptr },
	{ Role::TEST, "F2", nullptr, nullptr },
	{ Role::BOARD_TEST, "F5", nullptr, nullptr },
	{ Role::BOARD_SERVICE, "F6", nullptr, nullptr },
};

std::string_view
trimmed(std::string_view text)
{
	while (!text.empty() && (text.front() == ' ' || text.front() == '\t'))
		text.remove_prefix(1);
	while (!text.empty() && (text.back() == ' ' || text.back() == '\t'))
		text.remove_suffix(1);
	return text;
}

/* The line of a default: "key:<key>, pad:<pad>" */
std::string
default_value(const char *key, const char *pad)
{
	std::string value;

	if (key)
		value = std::string("key:") + key;
	if (pad)
		value += (value.empty() ? "pad:" : ", pad:") + std::string(pad);
	return value;
}

} /* namespace */

bool
PadInput::is_trigger() const
{
	return type == Type::AXIS && index >= SDL_CONTROLLER_AXIS_TRIGGERLEFT;
}

std::optional<HostInput>
HostInput::parse(std::string_view text)
{
	HostInput out{};

	if (text.substr(0, 4) == "key:") {
		const std::string name(text.substr(4));

		out.kind = Kind::KEY;
		out.key = SDL_GetKeyFromName(name.c_str());
		if (out.key == SDLK_UNKNOWN)
			return std::nullopt;
		return out;
	}
	if (text.substr(0, 4) != "pad:")
		return std::nullopt;
	text.remove_prefix(4);

	PadInput::Type half = PadInput::Type::AXIS;

	if (!text.empty() && (text.front() == '-' || text.front() == '+')) {
		half = (text.front() == '-') ? PadInput::Type::AXIS_NEGATIVE : PadInput::Type::AXIS_POSITIVE;
		text.remove_prefix(1);
	}

	const std::string name(text);
	const SDL_GameControllerAxis axis = SDL_GameControllerGetAxisFromString(name.c_str());
	const SDL_GameControllerButton button = SDL_GameControllerGetButtonFromString(name.c_str());

	out.kind = Kind::PAD;
	if (axis != SDL_CONTROLLER_AXIS_INVALID) {
		/* Half a trigger is no input */
		if (half != PadInput::Type::AXIS && axis >= SDL_CONTROLLER_AXIS_TRIGGERLEFT)
			return std::nullopt;
		out.pad = PadInput{ half, static_cast<uint8_t>(axis) };
		return out;
	}
	if (half != PadInput::Type::AXIS || button == SDL_CONTROLLER_BUTTON_INVALID)
		return std::nullopt;
	out.pad = PadInput{ PadInput::Type::BUTTON, static_cast<uint8_t>(button) };
	return out;
}

Bindings::Bindings()
{
	Settings::Lines lines;

	for (const Default &entry : DEFAULTS) {
		if (role_of_cabinet(entry.role)) {
			lines.emplace_back(role_name(entry.role), default_value(entry.key1, entry.pad));
			continue;
		}
		lines.emplace_back(std::string("p1.") + role_name(entry.role), default_value(entry.key1, entry.pad));
		lines.emplace_back(std::string("p2.") + role_name(entry.role), default_value(entry.key2, entry.pad));
	}
	apply(lines, "the defaults");
}

Bindings::Bindings(const Settings &settings, const std::string &game)
	: Bindings()
{
	apply(settings.section("controls"), "[controls]");
	apply(settings.section("controls." + game), "[controls." + game + "]");
}

const std::vector<HostInput> &
Bindings::of(Role role, unsigned player) const
{
	return m_inputs[role_of_cabinet(role) ? 0 : player & 1][static_cast<size_t>(role)];
}

void
Bindings::apply(const Settings::Lines &lines, const std::string &where)
{
	for (const auto &[key, value] : lines) {
		const std::string_view name(key);
		const bool player = name.size() > 3 && (name.substr(0, 3) == "p1." || name.substr(0, 3) == "p2.");
		const auto role = parse_role(player ? name.substr(3) : name);
		std::vector<HostInput> inputs;
		bool right = role && *role != Role::GEAR && player != role_of_cabinet(*role);
		std::string_view rest(value);

		while (right && !trimmed(rest).empty()) {
			const size_t comma = rest.find(',');
			const auto input = HostInput::parse(trimmed(rest.substr(0, comma)));

			/* A channel takes the axis of a pad; anything else, what is pressed or not */
			right = input && (role_is_analog(*role) ? input->kind == HostInput::Kind::PAD &&
			                                          input->pad.type == PadInput::Type::AXIS
			                                        : input->kind == HostInput::Kind::KEY || input->pad.is_digital());
			if (right)
				inputs.push_back(*input);
			rest.remove_prefix(comma == std::string_view::npos ? rest.size() : comma + 1);
		}
		if (!right) {
			std::fprintf(stderr, "ERROR: controls: %s: not right: %s = %s\n", where.c_str(), key.c_str(),
			             value.c_str());
			continue;
		}
		m_inputs[player && name[1] == '2' ? 1 : 0][static_cast<size_t>(*role)] = std::move(inputs);
	}
}

} /* namespace odin */
