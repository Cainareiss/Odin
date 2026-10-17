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
	{ Role::GUN_X, nullptr, nullptr, "leftx" },
	{ Role::GUN_Y, nullptr, nullptr, "lefty" },
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

/* A list of inputs, "key:A, pad:b"; nothing when one is not an input, or
 * not of the kind the control takes: a channel the axis of a pad, any
 * other what is pressed or not. */
std::optional<std::vector<HostInput>>
parse_list(std::string_view value, bool analog)
{
	std::vector<HostInput> inputs;

	while (!trimmed(value).empty()) {
		const size_t comma = value.find(',');
		const auto input = HostInput::parse(trimmed(value.substr(0, comma)));

		if (!input || (analog ? input->kind != HostInput::Kind::PAD || input->pad.type != PadInput::Type::AXIS
		                      : input->kind == HostInput::Kind::PAD && !input->pad.is_digital()))
			return std::nullopt;
		inputs.push_back(*input);
		value.remove_prefix(comma == std::string_view::npos ? value.size() : comma + 1);
	}
	return inputs;
}

Role
cabinet_role(size_t index)
{
	return static_cast<Role>(static_cast<size_t>(Role::COIN1) + index);
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

	constexpr std::string_view INVERT = ":invert";
	const bool inverted = text.size() > INVERT.size() && text.substr(text.size() - INVERT.size()) == INVERT;

	if (inverted)
		text.remove_suffix(INVERT.size());

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
		/* Half a trigger is no input, a stick is turned over whole */
		if (half != PadInput::Type::AXIS && axis >= SDL_CONTROLLER_AXIS_TRIGGERLEFT)
			return std::nullopt;
		if (inverted && (half != PadInput::Type::AXIS || axis >= SDL_CONTROLLER_AXIS_TRIGGERLEFT))
			return std::nullopt;
		out.pad = PadInput{ half, static_cast<uint8_t>(axis), inverted };
		return out;
	}
	if (inverted || half != PadInput::Type::AXIS || button == SDL_CONTROLLER_BUTTON_INVALID)
		return std::nullopt;
	out.pad = PadInput{ PadInput::Type::BUTTON, static_cast<uint8_t>(button) };
	return out;
}

std::vector<HostInput>
Bindings::defaults(Role role, unsigned player)
{
	std::vector<HostInput> out;

	for (const Default &entry : DEFAULTS) {
		if (entry.role != role)
			continue;
		const char *const key = (player && !role_of_cabinet(role)) ? entry.key2 : entry.key1;

		if (const auto input = key ? HostInput::parse(std::string("key:") + key) : std::nullopt)
			out.push_back(*input);
		if (const auto input = entry.pad ? HostInput::parse(std::string("pad:") + entry.pad) : std::nullopt)
			out.push_back(*input);
	}
	return out;
}

Bindings::Bindings(const std::vector<GameControl> &panel, const Settings &settings, const std::string &game)
	: m_controls(panel.size())
{
	for (size_t c = 0; c < panel.size(); c++) {
		const GameControl &control = panel[c];

		if (control.type == GameControl::Type::POSITIONS) {
			m_controls[c][0] = defaults(Role::GEAR_DOWN, control.player);
			m_controls[c][1] = defaults(Role::GEAR_UP, control.player);
		} else {
			m_controls[c][0] = defaults(control.role, control.player);
		}
	}
	for (size_t r = 0; r < m_cabinet.size(); r++)
		m_cabinet[r] = defaults(cabinet_role(r), 0);

	for (const auto &[key, value] : settings.section("controls." + game)) {
		std::vector<HostInput> *target = nullptr;
		bool analog = false;

		for (size_t c = 0; c < panel.size() && !target; c++) {
			const GameControl &control = panel[c];

			if (control.type == GameControl::Type::POSITIONS) {
				if (key == control.id + "_down")
					target = &m_controls[c][0];
				else if (key == control.id + "_up")
					target = &m_controls[c][1];
			} else if (key == control.id) {
				target = &m_controls[c][0];
				analog = control.type == GameControl::Type::ANALOG;
			}
		}
		for (size_t r = 0; r < m_cabinet.size() && !target; r++)
			if (key == role_name(cabinet_role(r)))
				target = &m_cabinet[r];

		const auto inputs = target ? parse_list(value, analog) : std::nullopt;

		if (!inputs) {
			std::fprintf(stderr, "ERROR: controls: [controls.%s]: not right: %s = %s\n", game.c_str(),
			             key.c_str(), value.c_str());
			continue;
		}
		*target = *inputs;
	}
}

const std::vector<HostInput> &
Bindings::cabinet(Role role) const
{
	return m_cabinet[static_cast<size_t>(role) - static_cast<size_t>(Role::COIN1)];
}

} /* namespace odin */
