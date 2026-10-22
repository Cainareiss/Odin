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

#include <algorithm>
#include <cstdio>
#include <initializer_list>
#include <iterator>
#include <utility>

namespace odin {

namespace {

/* The keys of the cabinet as INSTALL tells them, and the gamepad of each
 * player: a stick on the keys of the directions, the second stick of
 * player 1 on those of player 2, the gears on the keys of LB and RB.
 * TEST and SERVICE are the buttons of the cabinet, on its I/O board,
 * which the BOOTROM and the games all read: Guide with the left stick
 * pressed for SERVICE, with the right one for TEST (Guide is the key of
 * the shortcuts, as on Batocera); the switches of the main board have no
 * key. The coin of a player is also the SELECT of their pad. */
struct Default {
	Role role;
	const char *key1;	/* the key of player 1, or of the cabinet */
	const char *key2;	/* the key of player 2 */
	const char *pad;
};

const Default DEFAULTS[] = {
	{ Role::START, "1", "2", "start" },
	{ Role::SERVICE, "9", nullptr, "guide+leftstick" },
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
	{ Role::BUTTON7, "V", nullptr, "lefttrigger" },
	{ Role::BUTTON8, "B", nullptr, "righttrigger" },
	{ Role::STICK_UP, "Up", "R", "-lefty" },
	{ Role::STICK_DOWN, "Down", "F", "+lefty" },
	{ Role::STICK_LEFT, "Left", "D", "-leftx" },
	{ Role::STICK_RIGHT, "Right", "G", "+leftx" },
	{ Role::STICK2_UP, "R", nullptr, "-righty" },
	{ Role::STICK2_DOWN, "F", nullptr, "+righty" },
	{ Role::STICK2_LEFT, "D", nullptr, "-rightx" },
	{ Role::STICK2_RIGHT, "G", nullptr, "+rightx" },
	{ Role::GEAR_UP, "C", nullptr, "rightshoulder" },
	{ Role::GEAR_DOWN, "X", nullptr, "leftshoulder" },
	{ Role::STEER, nullptr, nullptr, "leftx" },
	{ Role::ACCEL, nullptr, nullptr, "righttrigger" },
	{ Role::BRAKE, nullptr, nullptr, "lefttrigger" },
	{ Role::STICK_X, nullptr, nullptr, "leftx" },
	{ Role::STICK_Y, nullptr, nullptr, "lefty" },
	{ Role::STICK2_X, nullptr, nullptr, "rightx" },
	{ Role::STICK2_Y, nullptr, nullptr, "righty" },
	{ Role::GUN_X, nullptr, nullptr, "leftx" },
	{ Role::GUN_Y, nullptr, nullptr, "lefty" },
	{ Role::COIN1, "5", nullptr, "back" },
	{ Role::COIN2, "6", nullptr, "back" },
	{ Role::TEST, "0", nullptr, "guide+rightstick" },
	{ Role::BOARD_TEST, nullptr, nullptr, nullptr },
	{ Role::BOARD_SERVICE, nullptr, nullptr, nullptr },
};

/* The keys that move an analog control: left or up, right or down, for
 * player 1 then player 2; those of the directions of the player, the
 * second stick of player 1 on those of player 2. The gas on Up and the
 * brake on Down, as xemu's Chihiro has them. */
struct KeysToward {
	Role role;
	const char *keys[2][2];	/* [player][positive] */
};

const KeysToward KEYS_TOWARD[] = {
	{ Role::STEER, { { "Left", "Right" }, { "D", "G" } } },
	{ Role::ACCEL, { { nullptr, "Up" }, { nullptr, "R" } } },
	{ Role::BRAKE, { { nullptr, "Down" }, { nullptr, "F" } } },
	{ Role::STICK_X, { { "Left", "Right" }, { "D", "G" } } },
	{ Role::STICK_Y, { { "Up", "Down" }, { "R", "F" } } },
	{ Role::STICK2_X, { { "D", "G" }, { nullptr, nullptr } } },
	{ Role::STICK2_Y, { { "R", "F" }, { nullptr, nullptr } } },
	{ Role::GUN_X, { { "Left", "Right" }, { "D", "G" } } },
	{ Role::GUN_Y, { { "Up", "Down" }, { "R", "F" } } },
};

/* The mouse, in a game where player 1 has a sight: it aims for player 1,
 * the left button fires, the right one is the second button, the middle
 * one Start. */
const std::pair<Role, const char *> MOUSE_DEFAULTS[] = {
	{ Role::GUN_X, "x" }, { Role::GUN_Y, "y" }, { Role::GUN_OFFSCREEN, "off" },
	{ Role::BUTTON1, "left" }, { Role::BUTTON2, "right" }, { Role::START, "middle" },
};

/* What a line takes: what is pressed or not (a key, a button, half a
 * stick, a trigger), the whole axis of a pad, or both (a pedal) */
enum class Takes { PRESSED, AXIS, BOTH };

/* A list of inputs, "key:A, pad:b"; nothing when one is not an input, or
 * not of the kind the line takes. */
std::optional<std::vector<HostInput>>
parse_list(std::string_view value, Takes takes)
{
	std::vector<HostInput> inputs;

	while (!trimmed(value).empty()) {
		const size_t comma = value.find(',');
		const auto input = HostInput::parse(trimmed(value.substr(0, comma)));
		const bool pressed = input && (input->kind == HostInput::Kind::PAD ? input->pad.is_digital()
		                                                                    : !input->is_axis());

		if (!input || (takes == Takes::PRESSED && !pressed) || (takes == Takes::AXIS && !input->is_axis()))
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

size_t
cabinet_index(Role role)
{
	return static_cast<size_t>(role) - static_cast<size_t>(Role::COIN1);
}

/* The inputs of a button: the keys, then the inputs of the gamepad, of its
 * role or those the list of games gives it in their place; a name that is
 * not one is said on stderr */
std::vector<HostInput>
own_inputs(const std::vector<HostInput> &of_role, const GameControl &control, const std::string &game)
{
	using Kind = HostInput::Kind;
	std::vector<HostInput> out;

	for (const auto &[kind, names] : { std::pair{ Kind::KEY, &control.keys }, std::pair{ Kind::PAD, &control.pad } }) {
		if (names->empty()) {
			std::copy_if(of_role.begin(), of_role.end(), std::back_inserter(out),
			             [kind](const HostInput &input) { return input.kind == kind; });
			continue;
		}
		for (const std::string &name : *names) {
			const auto input = HostInput::parse((kind == Kind::KEY ? "key:" : "pad:") + name);

			if (input && (kind == Kind::KEY || input->pad.is_digital()))
				out.push_back(*input);
			else
				std::fprintf(stderr, "ERROR: controls: %s: %s: not a %s: %s\n", game.c_str(), control.id.c_str(),
				             kind == Kind::KEY ? "key" : "button of a gamepad", name.c_str());
		}
	}
	return out;
}

/* An analog control that only goes one way */
bool
is_pedal(const GameControl &control)
{
	return control.type == GameControl::Type::ANALOG && !GameControl::half(control.low, false);
}

} /* namespace */

bool
PadInput::is_trigger() const
{
	return type == Type::AXIS && index >= SDL_GAMEPAD_AXIS_LEFT_TRIGGER;
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
	if (text.substr(0, 6) == "mouse:") {
		static const std::pair<std::string_view, MouseInput> NAMES[] = {
			{ "left", { MouseInput::Type::BUTTON, 0 } }, { "middle", { MouseInput::Type::BUTTON, 1 } },
			{ "right", { MouseInput::Type::BUTTON, 2 } }, { "x", { MouseInput::Type::X, 0 } },
			{ "y", { MouseInput::Type::Y, 0 } }, { "off", { MouseInput::Type::OFF, 0 } },
		};

		out.kind = Kind::MOUSE;
		for (const auto &[name, mouse] : NAMES)
			if (text.substr(6) == name) {
				out.mouse = mouse;
				return out;
			}
		return std::nullopt;
	}
	if (text.substr(0, 4) != "pad:")
		return std::nullopt;
	text.remove_prefix(4);

	/* A button held first, then another */
	if (const size_t plus = text.find('+', 1); plus != std::string_view::npos) {
		const std::string first(text.substr(0, plus)), second(text.substr(plus + 1));
		const SDL_GamepadButton with = SDL_GetGamepadButtonFromString(first.c_str());
		const SDL_GamepadButton button = SDL_GetGamepadButtonFromString(second.c_str());

		if (with == SDL_GAMEPAD_BUTTON_INVALID || button == SDL_GAMEPAD_BUTTON_INVALID || with == button)
			return std::nullopt;
		out.kind = Kind::PAD;
		out.pad = PadInput{ PadInput::Type::BUTTON, static_cast<uint8_t>(button) };
		out.pad.with = static_cast<int8_t>(with);
		return out;
	}

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
	const SDL_GamepadAxis axis = SDL_GetGamepadAxisFromString(name.c_str());
	const SDL_GamepadButton button = SDL_GetGamepadButtonFromString(name.c_str());

	out.kind = Kind::PAD;
	if (axis != SDL_GAMEPAD_AXIS_INVALID) {
		/* Half a trigger is no input, a stick is turned over whole */
		if (half != PadInput::Type::AXIS && axis >= SDL_GAMEPAD_AXIS_LEFT_TRIGGER)
			return std::nullopt;
		if (inverted && (half != PadInput::Type::AXIS || axis >= SDL_GAMEPAD_AXIS_LEFT_TRIGGER))
			return std::nullopt;
		out.pad = PadInput{ half, static_cast<uint8_t>(axis), inverted };
		return out;
	}
	if (inverted || half != PadInput::Type::AXIS || button == SDL_GAMEPAD_BUTTON_INVALID)
		return std::nullopt;
	out.pad = PadInput{ PadInput::Type::BUTTON, static_cast<uint8_t>(button) };
	return out;
}

std::string
HostInput::text() const
{
	static const char *const BUTTONS[] = { "left", "middle", "right" };

	switch (kind) {
	case Kind::KEY:
		return std::string("key:") + SDL_GetKeyName(key);
	case Kind::MOUSE:
		return std::string("mouse:") + (mouse.type == MouseInput::Type::X   ? "x" :
		                                mouse.type == MouseInput::Type::Y   ? "y" :
		                                mouse.type == MouseInput::Type::OFF ? "off" :
		                                                                      BUTTONS[mouse.button % 3]);
	case Kind::PAD:
		break;
	}

	const char *const name = pad.type == PadInput::Type::BUTTON ?
	                         SDL_GetGamepadStringForButton(static_cast<SDL_GamepadButton>(pad.index)) :
	                         SDL_GetGamepadStringForAxis(static_cast<SDL_GamepadAxis>(pad.index));
	std::string out = "pad:";

	if (pad.with >= 0)
		out = out + SDL_GetGamepadStringForButton(static_cast<SDL_GamepadButton>(pad.with)) + "+";
	if (pad.type == PadInput::Type::AXIS_NEGATIVE)
		out += '-';
	else if (pad.type == PadInput::Type::AXIS_POSITIVE)
		out += '+';
	out += name ? name : "";
	if (pad.inverted)
		out += ":invert";
	return out;
}

std::string
HostInput::text(const std::vector<HostInput> &inputs)
{
	std::string out;

	for (const HostInput &input : inputs)
		out += (out.empty() ? "" : ", ") + input.text();
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

std::vector<HostInput>
Bindings::keys_toward(Role role, unsigned player, bool positive)
{
	std::vector<HostInput> out;

	for (const KeysToward &entry : KEYS_TOWARD) {
		const char *const key = entry.keys[player & 1][positive];

		if (entry.role != role || !key)
			continue;
		if (const auto input = HostInput::parse(std::string("key:") + key))
			out.push_back(*input);
	}
	return out;
}

Bindings::Bindings(const std::vector<GameControl> &panel, const Settings &settings, const std::string &game)
	: m_panel(panel), m_controls(panel.size())
{
	/* The players with a sight: the mouse aims for them (player 2 with
	 * their light gun) */
	std::array<bool, 2> aims{};

	for (const GameControl &control : panel)
		if (control.role == Role::GUN_X)
			aims[control.player & 1] = true;

	for (size_t c = 0; c < panel.size(); c++) {
		const GameControl &control = panel[c];

		if (control.type == GameControl::Type::POSITIONS) {
			m_controls[c][1] = defaults(Role::GEAR_DOWN, control.player);
			m_controls[c][2] = defaults(Role::GEAR_UP, control.player);
			continue;
		}
		m_controls[c][0] = own_inputs(defaults(control.role, control.player), control, game);
		for (const auto &[role, name] : MOUSE_DEFAULTS)
			if (aims[control.player & 1] && role == control.role)
				m_controls[c][0].push_back(*HostInput::parse(std::string("mouse:") + name));
		if (control.type == GameControl::Type::ANALOG) {
			m_controls[c][1] = keys_toward(control.role, control.player, false);
			m_controls[c][2] = keys_toward(control.role, control.player, true);
		}
	}
	for (size_t r = 0; r < m_cabinet.size(); r++)
		m_cabinet[r] = defaults(cabinet_role(r), 0);
	m_defaults = m_controls;
	m_cabinet_defaults = m_cabinet;

	for (const auto &[key, value] : settings.section("controls." + game)) {
		std::vector<HostInput> *target = nullptr;
		std::vector<HostInput> *pedal_down = nullptr;	/* a pedal: where what is pressed goes */
		Takes takes = Takes::PRESSED;

		for (size_t c = 0; c < panel.size() && !target; c++) {
			const GameControl &control = panel[c];
			const bool lever = control.type == GameControl::Type::POSITIONS;
			const bool analog = control.type == GameControl::Type::ANALOG;
			const bool pedal = analog && !GameControl::half(control.low, false);

			if (lever || (analog && !pedal))
				for (const bool positive : { false, true }) {
					const char *const half = lever ? (positive ? "_up" : "_down")
					                               : GameControl::half(control.low, positive);

					if (key == control.id + half)
						target = &m_controls[c][1 + positive];
				}
			if (target || lever || key != control.id)
				continue;
			target = &m_controls[c][0];
			takes = pedal ? Takes::BOTH : analog ? Takes::AXIS : Takes::PRESSED;
			if (pedal)
				pedal_down = &m_controls[c][2];
		}
		for (size_t r = 0; r < m_cabinet.size() && !target; r++)
			if (key == role_name(cabinet_role(r)))
				target = &m_cabinet[r];

		const auto inputs = target ? parse_list(value, takes) : std::nullopt;

		if (!inputs) {
			std::fprintf(stderr, "ERROR: controls: [controls.%s]: not right: %s = %s\n", game.c_str(),
			             key.c_str(), value.c_str());
			continue;
		}
		if (!pedal_down) {
			*target = *inputs;
			continue;
		}
		target->clear();
		pedal_down->clear();
		for (const HostInput &input : *inputs)
			(input.is_axis() ? target : pedal_down)->push_back(input);
	}
}

const std::vector<HostInput> &
Bindings::cabinet(Role role) const
{
	return m_cabinet[cabinet_index(role)];
}

const std::vector<HostInput> &
Bindings::cabinet_standard(Role role) const
{
	return m_cabinet_defaults[cabinet_index(role)];
}

Bindings::Line
Bindings::line(size_t control, unsigned part, const std::vector<HostInput> &inputs) const
{
	const GameControl &c = m_panel[control];
	const auto value = [](const std::vector<HostInput> &now, const std::vector<HostInput> &standard) {
		return now == standard ? std::nullopt : std::optional<std::string>(HostInput::text(now));
	};

	if (is_pedal(c)) {
		std::vector<HostInput> both = part == 0 ? inputs : m_controls[control][0];
		std::vector<HostInput> standard = m_defaults[control][0];

		for (const HostInput &input : part == 2 ? inputs : m_controls[control][2])
			both.push_back(input);
		for (const HostInput &input : m_defaults[control][2])
			standard.push_back(input);
		return { c.id, value(both, standard) };
	}

	std::string key = c.id;

	if (part && c.type == GameControl::Type::POSITIONS)
		key += part == 2 ? "_up" : "_down";
	else if (part)
		key += GameControl::half(c.low, part == 2);
	return { key, value(inputs, m_defaults[control][part]) };
}

std::vector<std::pair<std::string, std::string>>
Bindings::standard_lines() const
{
	std::vector<std::pair<std::string, std::string>> out;

	for (size_t c = 0; c < m_panel.size(); c++) {
		const GameControl &control = m_panel[c];
		const bool whole = control.type == GameControl::Type::BUTTON || is_pedal(control);
		const unsigned first = control.type == GameControl::Type::POSITIONS ? 1 : 0;

		for (unsigned part = first; part <= (whole ? 0u : 2u); part++) {
			std::vector<HostInput> inputs = m_defaults[c][part];

			/* A pedal: what presses it on the line of its axes */
			if (is_pedal(control))
				inputs.insert(inputs.end(), m_defaults[c][2].begin(), m_defaults[c][2].end());
			out.emplace_back(line(c, part, m_defaults[c][part]).key, HostInput::text(inputs));
		}
	}
	for (size_t r = 0; r < m_cabinet_defaults.size(); r++)
		out.emplace_back(role_name(cabinet_role(r)), HostInput::text(m_cabinet_defaults[r]));
	return out;
}

Bindings::Line
Bindings::cabinet_line(Role role, const std::vector<HostInput> &inputs) const
{
	const std::vector<HostInput> &standard = m_cabinet_defaults[cabinet_index(role)];

	return { role_name(role), inputs == standard ? std::nullopt : std::optional<std::string>(HostInput::text(inputs)) };
}

} /* namespace odin */
