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

/* The page Input of the menu (mock-up v50): the games, the device of each
 * player, and the gamepad drawn with the name of each control beside the
 * part that works it, or the controls as a list (Classic). */

#include "odin/menu.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <numbers>
#include <utility>

#include "imgui/imgui.h"
#include "odin/cabinet_view.hpp"
#include "odin/control_panel.hpp"
#include "odin/menu_kit.hpp"
#include "odin/pad_view.hpp"

namespace odin {

namespace {

/* The room of the page, in points: between its rows; the inside of the box
 * of the device; a label column at its widest and narrowest, a label from
 * the next, the room the leader lines keep from the box's edges */
constexpr float ROW_GAP = 6.0f;
/* Start mapping waits so long for each control */
constexpr uint64_t MAPPING_MS = 6000;
/* A window this many times wider than high has the cabinet beside the
 * device, which takes this share of the width (1.4 to 1) */
constexpr float WIDE = 1.5f;
constexpr float PAD_SHARE = 1.4f / 2.4f;
constexpr float BOX_CUT = 5.0f;
constexpr float BOX_INSIDE = 8.0f;
constexpr float COLUMN_MIN = 90.0f;
constexpr float LABEL_GAP = 19.0f;
constexpr float LABEL_EDGE = 10.0f;
/* A leader line at rest: its dashes; touched: its width; its dot */
constexpr float DASH_ON = 1.5f;
constexpr float DASH_OFF = 3.5f;
constexpr float REST_ALPHA = 0.3f;
constexpr float HOT_WIDTH = 1.8f;
constexpr float DOT = 2.0f;
/* A stick touched from there, a trigger from there */
constexpr float STICK_TOUCHED = 0.3f;
constexpr int16_t TRIGGER_TOUCHED = 6554;

/* The type of SDL of a kind of gamepad (pad_drawing.hpp): the Steam
 * Controller and an unknown one are standard */
SDL_GamepadType
sdl_type(int kind)
{
	return kind == PAD_STEAM_CONTROLLER || kind == SDL_GAMEPAD_TYPE_UNKNOWN ? SDL_GAMEPAD_TYPE_STANDARD :
	                                                                         static_cast<SDL_GamepadType>(kind);
}

bool
is_switch(int kind)
{
	return kind >= SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_PRO && kind <= SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_JOYCON_PAIR;
}

/* The names a maker prints on the buttons that differ: Back, Start, the
 * sticks pressed, Guide, the shoulders and the triggers; the D-pad and the
 * right stick (the pads of the Steam Controller) */
struct Family {
	const char *back, *start, *left_stick, *right_stick, *guide, *lb, *rb, *lt, *rt, *dpad, *right_axes;
};

const Family &
family(int kind)
{
	static constexpr Family ODIN = { "Select", "Start", "L3", "R3", "Guide", "LB", "RB", "LT", "RT", "D-pad", "R stick" };
	static constexpr Family XBOX_360 = { "Back", "Start", "LS", "RS", "Guide", "LB", "RB", "LT", "RT", "D-pad", "R stick" };
	static constexpr Family XBOX_ONE = { "View", "Menu", "LS", "RS", "Guide", "LB", "RB", "LT", "RT", "D-pad", "R stick" };
	static constexpr Family PS3 = { "Select", "Start", "L3", "R3", "PS", "L1", "R1", "L2", "R2", "D-pad", "R stick" };
	static constexpr Family PS4 = { "Share", "Options", "L3", "R3", "PS", "L1", "R1", "L2", "R2", "D-pad", "R stick" };
	static constexpr Family PS5 = { "Create", "Options", "L3", "R3", "PS", "L1", "R1", "L2", "R2", "D-pad", "R stick" };
	static constexpr Family SWITCH = { "−", "+", "LS", "RS", "Home", "L", "R", "ZL", "ZR", "D-pad", "R stick" };
	static constexpr Family STEAM = { "◀", "▶", "LS", "R pad", "Steam", "LB", "RB", "LT", "RT", "L pad", "R pad" };

	if (kind == PAD_STEAM_CONTROLLER)
		return STEAM;
	switch (static_cast<SDL_GamepadType>(kind)) {
	case SDL_GAMEPAD_TYPE_XBOX360: return XBOX_360;
	case SDL_GAMEPAD_TYPE_XBOXONE: return XBOX_ONE;
	case SDL_GAMEPAD_TYPE_PS3: return PS3;
	case SDL_GAMEPAD_TYPE_PS4: return PS4;
	case SDL_GAMEPAD_TYPE_PS5: return PS5;
	default: return is_switch(kind) ? SWITCH : ODIN;
	}
}

/* The name of an input of a gamepad of that kind, as its maker prints it:
 * the marks of a PlayStation, a way of a stick or of the D-pad as an
 * arrow (mock-up v50) */
std::string
pad_label(const PadInput &pad, int kind)
{
	const Family &names = family(kind);
	const std::string dpad = std::string(names.dpad) + " ";

	if (pad.type == PadInput::Type::BUTTON) {
		const auto button = static_cast<SDL_GamepadButton>(pad.index);

		switch (button) {
		case SDL_GAMEPAD_BUTTON_SOUTH:
		case SDL_GAMEPAD_BUTTON_EAST:
		case SDL_GAMEPAD_BUTTON_WEST:
		case SDL_GAMEPAD_BUTTON_NORTH: {
			static const char *const FACES[] = { "?", "A", "B", "X", "Y", "✕", "○", "□", "△" };
			const SDL_GamepadButtonLabel label = SDL_GetGamepadButtonLabelForType(sdl_type(kind), button);

			return FACES[static_cast<size_t>(label) < std::size(FACES) ? static_cast<size_t>(label) : 0];
		}
		case SDL_GAMEPAD_BUTTON_BACK: return names.back;
		case SDL_GAMEPAD_BUTTON_GUIDE: return names.guide;
		case SDL_GAMEPAD_BUTTON_START: return names.start;
		case SDL_GAMEPAD_BUTTON_LEFT_STICK: return names.left_stick;
		case SDL_GAMEPAD_BUTTON_RIGHT_STICK: return names.right_stick;
		case SDL_GAMEPAD_BUTTON_LEFT_SHOULDER: return names.lb;
		case SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER: return names.rb;
		case SDL_GAMEPAD_BUTTON_DPAD_UP: return dpad + "↑";
		case SDL_GAMEPAD_BUTTON_DPAD_DOWN: return dpad + "↓";
		case SDL_GAMEPAD_BUTTON_DPAD_LEFT: return dpad + "←";
		case SDL_GAMEPAD_BUTTON_DPAD_RIGHT: return dpad + "→";
		case SDL_GAMEPAD_BUTTON_TOUCHPAD: return "Touchpad";
		default: {
			/* The grips of the Steam Controller, under it */
			if (kind == PAD_STEAM_CONTROLLER && button == SDL_GAMEPAD_BUTTON_RIGHT_PADDLE1)
				return "R grip";
			if (kind == PAD_STEAM_CONTROLLER && button == SDL_GAMEPAD_BUTTON_LEFT_PADDLE1)
				return "L grip";

			const char *const name = SDL_GetGamepadStringForButton(button);

			return name ? name : "?";
		}
		}
	}

	const auto axis = static_cast<SDL_GamepadAxis>(pad.index);

	if (axis == SDL_GAMEPAD_AXIS_LEFT_TRIGGER)
		return names.lt;
	if (axis == SDL_GAMEPAD_AXIS_RIGHT_TRIGGER)
		return names.rt;

	const bool left = axis == SDL_GAMEPAD_AXIS_LEFTX || axis == SDL_GAMEPAD_AXIS_LEFTY;
	const bool across = axis == SDL_GAMEPAD_AXIS_LEFTX || axis == SDL_GAMEPAD_AXIS_RIGHTX;
	const bool positive = pad.type == PadInput::Type::AXIS_POSITIVE;
	std::string out = left ? "L stick " : std::string(names.right_axes) + " ";

	if (pad.type == PadInput::Type::AXIS)
		out += across ? "↔" : "↕";
	else if (across)
		out += positive ? "→" : "←";
	else
		out += positive ? "↓" : "↑";
	return pad.inverted ? out + " (inverted)" : out;
}

std::string
label(const HostInput &input, int kind)
{
	static const char *const MOUSE[] = { "Mouse left", "Mouse middle", "Mouse right" };

	switch (input.kind) {
	case HostInput::Kind::KEY:
		switch (input.key) {
		case SDLK_LEFT: return "←";
		case SDLK_RIGHT: return "→";
		case SDLK_UP: return "↑";
		case SDLK_DOWN: return "↓";
		case SDLK_RETURN: return "Enter";
		default: return SDL_GetKeyName(input.key);
		}
	case HostInput::Kind::PAD:
		if (input.pad.with >= 0) {
			const PadInput with = { PadInput::Type::BUTTON, static_cast<uint8_t>(input.pad.with) };

			return pad_label(with, kind) + " + " + pad_label(input.pad, kind);
		}
		return pad_label(input.pad, kind);
	case HostInput::Kind::MOUSE:
		break;
	}
	switch (input.mouse.type) {
	case MouseInput::Type::X: return "Mouse X";
	case MouseInput::Type::Y: return "Mouse Y";
	case MouseInput::Type::OFF: return "Mouse off the screen";
	case MouseInput::Type::BUTTON: break;
	}
	return MOUSE[input.mouse.button % 3];
}

std::string
labels(const std::vector<HostInput> &inputs, int kind)
{
	std::string out;

	for (const HostInput &input : inputs)
		out += (out.empty() ? "" : ", ") + label(input, kind);
	return out;
}

/* The ASCII letters in capitals, for a name of the page */
std::string
capitals(std::string text)
{
	for (char &c : text)
		if (c >= 'a' && c <= 'z')
			c = static_cast<char>(c - 'a' + 'A');
	return text;
}

bool
same(const std::optional<MenuSlot> &a, const MenuSlot &b)
{
	return a && a->control == b.control && a->part == b.part && a->role == b.role;
}

/* A control of the player shown, or a part of one: its name, what changes
 * it, its inputs on the device shown, lit while it is worked; a half of an
 * analog control on a gamepad, which the whole axis works already */
struct InputRow {
	std::string name;
	MenuSlot slot;
	std::vector<HostInput> inputs;
	bool lit, catchable, half = false;
};

/* The controls of the player in the order of the game, then the coin, the
 * SERVICE of the player and TEST (player 1): an axis on a gamepad only (or
 * the mouse that aims) */
std::vector<InputRow>
input_rows(const MenuState &state)
{
	const ControlPanel &panel = *state.panel;
	const Bindings &bindings = panel.bindings();
	const std::vector<GameControl> &controls = panel.controls();
	const bool pad = state.on_pad();
	std::vector<InputRow> rows;
	std::optional<InputRow> service;
	const auto add = [&](std::string name, MenuSlot slot, std::vector<HostInput> inputs, bool lit, bool catchable) {
		std::erase_if(inputs, [pad](const HostInput &in) { return (in.kind == HostInput::Kind::PAD) != pad; });
		rows.push_back({ std::move(name), slot, std::move(inputs), lit, catchable });
	};
	/* After the controls in `touched`: COIN1, COIN2, TEST */
	const auto worked = [&state, &controls](size_t i) {
		return state.touched.size() == controls.size() + 3 && state.touched[i];
	};

	for (size_t c = 0; c < controls.size(); c++) {
		const GameControl &control = controls[c];
		const int i = static_cast<int>(c);
		const bool lit = worked(c);

		if (control.player != state.player)
			continue;
		if (control.role == Role::SERVICE) {
			add("SERVICE", { i, 0, Role::SERVICE }, bindings.part(c, 0), lit, true);
			service = rows.back();
			rows.pop_back();
			continue;
		}
		switch (control.type) {
		case GameControl::Type::BUTTON:
			add(control.name, { i, 0, control.role }, bindings.part(c, 0), lit, true);
			break;
		case GameControl::Type::POSITIONS:
			add(control.name + " ▲", { i, 2, control.role }, bindings.part(c, 2), lit, true);
			add(control.name + " ▼", { i, 1, control.role }, bindings.part(c, 1), lit, true);
			break;
		case GameControl::Type::ANALOG: {
			const char *const left = GameControl::half(control.low, false);
			const char *const right = GameControl::half(control.low, true);
			const bool mouse = std::any_of(bindings.part(c, 0).begin(), bindings.part(c, 0).end(),
			                               [](const HostInput &in) { return in.kind == HostInput::Kind::MOUSE; });

			if (pad || mouse)
				add(control.name, { i, 0, control.role }, bindings.part(c, 0), lit, pad);
			if (!left || !right) {
				add(pad ? control.name + " (pressed)" : control.name, { i, 2, control.role }, bindings.part(c, 2), lit,
				    true);
				rows.back().half = pad;
				break;
			}
			add(control.name + " " + capitals(left + 1), { i, 1, control.role }, bindings.part(c, 1), lit, true);
			rows.back().half = pad;
			add(control.name + " " + capitals(right + 1), { i, 2, control.role }, bindings.part(c, 2), lit, true);
			rows.back().half = pad;
			break;
		}
		}
	}

	const Role coin = state.player ? Role::COIN2 : Role::COIN1;

	add("COIN", { -1, 0, coin }, bindings.cabinet(coin), worked(controls.size() + state.player), true);
	if (service)
		rows.push_back(*service);
	if (state.player == 0)
		add("TEST", { -1, 0, Role::TEST }, bindings.cabinet(Role::TEST), worked(controls.size() + 2), true);
	return rows;
}

/* Whether two parts are of the same control (or role of the cabinet) */
bool
same_control(const MenuSlot &a, const MenuSlot &b)
{
	return a.control == b.control && (a.control >= 0 || a.role == b.role);
}

/* The next input of these parts is caught, each in turn; none when the
 * first is caught already */
void
catch_parts(MenuState &state, const std::vector<MenuSlot> &parts)
{
	if (parts.empty() || same(state.catching, parts.front())) {
		state.catching.reset();
		state.catch_next.clear();
		return;
	}
	state.catching = parts.front();
	state.catch_next.assign(parts.begin() + 1, parts.end());
}

/* A control of the player as Classic and the line at the bottom show it:
 * its name, its parts (rows of input_rows) each after its sign (▲ and ▼
 * of a lever, · before a half on a gamepad), how far it is worked now (0
 * to 1) */
struct Group {
	struct Part {
		size_t row;
		const char *sign;
	};

	std::string name;
	std::vector<Part> parts;
	bool lit = false, chosen = false;
	float amount = 0.0f;

	/* Its parts caught in turn on a click: not the halves of an axis on a
	 * gamepad, which a button pressed for the whole takes */
	std::vector<MenuSlot> to_catch(std::span<const InputRow> rows) const
	{
		std::vector<MenuSlot> out;

		for (const Part &part : parts)
			if (rows[part.row].catchable && !rows[part.row].half)
				out.push_back(rows[part.row].slot);
		return out;
	}
};

std::vector<Group>
groups(const MenuState &state, std::span<const InputRow> rows)
{
	const std::vector<GameControl> &controls = state.panel->controls();
	std::vector<Group> out;

	for (size_t r = 0; r < rows.size(); r++) {
		const InputRow &row = rows[r];
		const int c = row.slot.control;
		const bool chosen = same(state.catching, row.slot);

		/* A half on a gamepad, empty, says nothing */
		if (row.half && row.inputs.empty() && !chosen)
			continue;
		if (out.empty() || c < 0 || rows[out.back().parts.front().row].slot.control != c) {
			Group group;

			group.name = c >= 0 && controls[static_cast<size_t>(c)].type != GameControl::Type::BUTTON ?
			             controls[static_cast<size_t>(c)].name : row.name;
			if (c >= 0 && static_cast<size_t>(c) < state.now.size()) {
				const ControlNow &now = state.now[static_cast<size_t>(c)];

				group.amount = controls[static_cast<size_t>(c)].type == GameControl::Type::ANALOG ?
				               std::min(1.0f, std::abs(now.value)) : now.on ? 1.0f : 0.0f;
			}
			out.push_back(std::move(group));
		}

		Group &group = out.back();
		const char *sign = "";

		if (c >= 0) {
			const GameControl &control = controls[static_cast<size_t>(c)];

			if (control.type == GameControl::Type::POSITIONS)
				sign = row.slot.part == 2 ? "▲" : "▼";
			else if (row.half)
				sign = "·";
		}
		group.parts.push_back({ r, sign });
		group.lit = group.lit || row.lit;
		group.chosen = group.chosen || chosen;
		if (c < 0)
			group.amount = row.lit ? 1.0f : 0.0f;
	}
	return out;
}

/* A part as a chip says it: its sign, then its inputs, "press..." while
 * caught, "-" for none */
std::string
part_text(const MenuState &state, std::span<const InputRow> rows, const Group::Part &part, int kind)
{
	const InputRow &row = rows[part.row];
	const std::string inputs = same(state.catching, row.slot) ? "press..." :
	                           row.inputs.empty() ? "-" : labels(row.inputs, kind);

	return *part.sign ? std::string(part.sign) + " " + inputs : inputs;
}

/* What part of the drawing an input of a gamepad is, and the key of its
 * label (the inputs of one label: a button, a stick, a trigger); -1: none */
std::pair<int, int>
part_of(const PadDrawing &drawing, const PadInput &pad)
{
	PadPartKind kind = PadPartKind::BUTTON;
	unsigned input = pad.index;
	int key = pad.index;

	if (pad.type == PadInput::Type::BUTTON) {
		if (pad.index == SDL_GAMEPAD_BUTTON_LEFT_STICK || pad.index == SDL_GAMEPAD_BUTTON_RIGHT_STICK) {
			kind = PadPartKind::STICK;
			input = pad.index - SDL_GAMEPAD_BUTTON_LEFT_STICK;
		}
	} else if (pad.is_trigger()) {
		kind = PadPartKind::TRIGGER;
		key = 200 + pad.index;
	} else {
		input = pad.index / 2;
		key = 100 + static_cast<int>(input);
		/* A stick's direction points at its well, if it has one */
		for (size_t p = 0; p < drawing.parts.size(); p++)
			if (drawing.parts[p].kind == PadPartKind::WELL && drawing.parts[p].input == input)
				return { static_cast<int>(p), key };
		kind = PadPartKind::STICK;
	}
	for (size_t p = 0; p < drawing.parts.size(); p++)
		if (drawing.parts[p].kind == kind && drawing.parts[p].input == input)
			return { static_cast<int>(p), key };
	return { -1, key };
}

/* Whether the inputs of a label are worked now */
bool
touched(int key, const PadState &now)
{
	if (key >= 200)
		return now.axes[static_cast<size_t>(key - 200)] > TRIGGER_TOUCHED;
	if (key >= 100) {
		const size_t s = static_cast<size_t>(key - 100);
		const float x = static_cast<float>(now.axes[2 * s]) / 32767.0f;
		const float y = static_cast<float>(now.axes[2 * s + 1]) / 32767.0f;

		return std::hypot(x, y) > STICK_TOUCHED || now.buttons[SDL_GAMEPAD_BUTTON_LEFT_STICK + s];
	}
	return now.buttons[static_cast<size_t>(key)];
}

/* A name beside the drawing: the controls on one input of it, their text,
 * the first of them caught by a click; the box of its part, the side it is
 * on, its height and its leader line */
struct Label {
	std::vector<std::string> names;
	size_t row;
	int key;
	size_t part;
	std::string text = {};
	ImVec2 lo = {}, hi = {};
	bool left = false, chosen = false, hot = false;
	float y = 0.0f;
	std::vector<ImVec2> line = {};
};

/* The text of a label: its names, the words they begin with said once
 * ("1P START · WATER"); names that differ only by their last word are one
 * control, its directions say nothing more, when there are three of them
 * or more ("LEFT-LEVER") or when that word is an axis or an arrow ("1P
 * SIGHT LOCATION", for X and Y) */
std::string
label_text(std::span<const std::string> names)
{
	const auto stem = [](const std::string &name) {
		const size_t space = name.rfind(' ');

		return space == std::string::npos ? std::string() : name.substr(0, space);
	};
	/* One character, of one byte or of a sign */
	const auto mark = [](const std::string &name) {
		const size_t space = name.rfind(' ');
		const size_t length = space == std::string::npos ? 0 : name.size() - space - 1;

		return length == 1 || (length == 3 && static_cast<unsigned char>(name[space + 1]) >= 0xE0);
	};
	const std::string first = stem(names[0]);

	if (names.size() >= 2 && !first.empty() &&
	    std::all_of(names.begin(), names.end(), [&](const std::string &n) { return stem(n) == first; }) &&
	    (names.size() >= 3 || std::all_of(names.begin(), names.end(), mark)))
		return first;

	std::string text = names[0];

	for (size_t i = 1; i < names.size(); i++) {
		const std::string before = stem(names[i - 1]), now = stem(names[i]);

		text += " · " + (!now.empty() && now == before ? names[i].substr(now.size() + 1) : names[i]);
	}
	return text;
}

/* Whether the segment p-q crosses a box */
bool
crosses(ImVec2 p, ImVec2 q, ImVec2 lo, ImVec2 hi)
{
	float t0 = 0.0f, t1 = 1.0f;
	const float sides[4][2] = { { p.x - q.x, p.x - lo.x }, { q.x - p.x, hi.x - p.x },
	                            { p.y - q.y, p.y - lo.y }, { q.y - p.y, hi.y - p.y } };

	for (const auto &[a, b] : sides) {
		if (a == 0.0f) {
			if (b < 0.0f)
				return false;
			continue;
		}

		const float t = b / a;

		if (a < 0.0f) {
			if (t > t1)
				return false;
			t0 = std::max(t0, t);
		} else {
			if (t < t0)
				return false;
			t1 = std::min(t1, t);
		}
	}
	return true;
}

/* Where the line from s towards c leaves a box */
ImVec2
edge_of(ImVec2 s, ImVec2 c, ImVec2 lo, ImVec2 hi)
{
	float t = 1.0f;
	const float sides[2][4] = { { s.x, c.x - s.x, lo.x, hi.x }, { s.y, c.y - s.y, lo.y, hi.y } };

	for (const auto &[from, by, a, b] : sides) {
		if (by == 0.0f)
			continue;
		for (const float side : { a, b }) {
			const float u = (side - from) / by;
			const ImVec2 o = { s.x + (c.x - s.x) * u, s.y + (c.y - s.y) * u };

			if (u >= 0.0f && u < t && o.x >= lo.x - 0.5f && o.x <= hi.x + 0.5f && o.y >= lo.y - 0.5f &&
			    o.y <= hi.y + 0.5f)
				t = u;
		}
	}
	return { s.x + (c.x - s.x) * t, s.y + (c.y - s.y) * t };
}

/* The leader line of a label: from its column to its part, the way that
 * crosses the fewest other parts, then the shortest, with the fewest
 * bends */
std::vector<ImVec2>
route(const Label &label, float from, float knee, std::span<const std::pair<ImVec2, ImVec2>> parts, const MenuKit &kit)
{
	const ImVec2 lo = { label.lo.x - kit.px(2.0f), label.lo.y - kit.px(2.0f) };
	const ImVec2 hi = { label.hi.x + kit.px(2.0f), label.hi.y + kit.px(2.0f) };
	const ImVec2 c = { (label.lo.x + label.hi.x) / 2.0f, (label.lo.y + label.hi.y) / 2.0f };
	const float y = label.y, side = label.left ? lo.x : hi.x;
	std::vector<std::vector<ImVec2>> ways;

	ways.push_back({ { from, y }, { knee, y }, edge_of({ knee, y }, c, lo, hi) });
	if (y > lo.y && y < hi.y)
		ways.push_back({ { from, y }, { knee, y }, { side, y } });
	else
		ways.push_back({ { from, y }, { knee, y }, { c.x, y }, { c.x, y < lo.y ? lo.y : hi.y } });
	ways.push_back({ { from, y }, { knee, y }, { knee, c.y }, { side, c.y } });
	for (const float dy : { 10.0f, 24.0f, 40.0f, 58.0f })
		for (const float yy : { lo.y - kit.px(dy), hi.y + kit.px(dy) })
			ways.push_back({ { from, y }, { knee, y }, { knee, yy }, { c.x, yy }, { c.x, yy < lo.y ? lo.y : hi.y } });

	const auto mine = [&](const std::pair<ImVec2, ImVec2> &box) {
		return box.first.x >= lo.x && box.second.x <= hi.x && box.first.y >= lo.y && box.second.y <= hi.y;
	};
	const auto cost = [&](const std::vector<ImVec2> &way) {
		float crossings = 0.0f, length = 0.0f;

		for (size_t i = 1; i < way.size(); i++) {
			length += std::hypot(way[i].x - way[i - 1].x, way[i].y - way[i - 1].y);
			for (const auto &box : parts)
				if (!mine(box) && crosses(way[i - 1], way[i], box.first, box.second))
					crossings += 1.0f;
		}
		return crossings * 1e4f + length + static_cast<float>(way.size() - 3) * kit.px(14.0f);
	};

	return *std::min_element(ways.begin(), ways.end(),
	                         [&](const auto &a, const auto &b) { return cost(a) < cost(b); });
}

} /* namespace */

std::string
Menu::button_name(int button, int kind)
{
	PadInput pad{};

	pad.type = PadInput::Type::BUTTON;
	pad.index = static_cast<uint8_t>(button);
	return pad_label(pad, kind);
}

/* The games, the one shown marked and the one that runs dotted; when they
 * do not fit, the one shown between arrows. Its bottom. */
float
Menu::games_row(const MenuKit &kit, MenuState &state, ImVec2 at, float width)
{
	ImDrawList *const list = ImGui::GetWindowDrawList();
	const MenuPalette &palette = kit.palette();
	const float line = ImGui::GetTextLineHeight(), spacing = kit.px(14.0f), dot = kit.px(2.5f);
	const float letters = ImGui::GetFontSize() * 0.1f;
	std::vector<std::string> names;
	float total = 0.0f;

	for (const auto &[name, title] : state.games) {
		names.push_back(capitals(title));
		total += MenuKit::measure(names.back().c_str(), 0.0f, letters) + spacing + 4.0f * dot;
	}
	if (names.empty())
		return at.y;

	const auto title = [&](size_t g, ImVec2 p, bool clickable) {
		const float w = MenuKit::measure(names[g].c_str(), 0.0f, letters);
		const bool shown = g == state.shown;

		if (clickable) {
			ImGui::SetCursorScreenPos(p);
			ImGui::PushID(static_cast<int>(g));
			if (ImGui::InvisibleButton("game", { w + 4.0f * dot, line + kit.px(4.0f) }) && !shown)
				state.show = g;
			ImGui::PopID();
		}

		const ImU32 colour = shown ? palette.action : palette.dim;

		MenuKit::write(list, p, colour, names[g].c_str(), 0.0f, letters);
		if (shown)
			list->AddLine({ p.x, p.y + line + kit.px(2.0f) }, { p.x + w, p.y + line + kit.px(2.0f) }, colour, kit.stroke());
		if (g == state.running)
			list->AddCircleFilled({ p.x + w + 2.4f * dot + dot, p.y + line / 2.0f }, dot, MenuKit::alpha(colour, 0.8f));
		return w + 4.0f * dot;
	};

	if (total - spacing <= width) {
		float x = at.x + (width - total + spacing) / 2.0f;

		for (size_t g = 0; g < names.size(); g++)
			x += title(g, { x, at.y }, true) + spacing;
		return at.y + line + kit.px(4.0f);
	}

	/* ‹ the game › and a dot for each */
	const size_t n = names.size(), shown = std::min(state.shown, n - 1);
	const ImVec2 arrow = kit.arrow_size();
	const float w = MenuKit::measure(names[shown].c_str(), 0.0f, letters) + 4.0f * dot;
	const float dots = static_cast<float>(n) * kit.px(9.0f);
	float x = at.x + (width - (2.0f * arrow.x + w + 2.0f * spacing + spacing + dots)) / 2.0f;

	ImGui::PushID("games");
	if (kit.arrow("before", { x, at.y }, false))
		state.show = (shown + n - 1) % n;
	x += arrow.x + spacing;
	title(shown, { x, at.y + (arrow.y - line) / 2.0f }, false);
	x += w + spacing;
	if (kit.arrow("after", { x, at.y }, true))
		state.show = (shown + 1) % n;
	ImGui::PopID();
	x += arrow.x + spacing;
	for (size_t g = 0; g < n; g++)
		list->AddCircleFilled({ x + kit.px(4.5f) + static_cast<float>(g) * kit.px(9.0f), at.y + arrow.y / 2.0f }, dot,
		                      g == shown ? palette.ink : palette.outline);
	return at.y + arrow.y;
}

namespace {

/* The device of each player: the keyboard, any gamepad or one of them; in
 * a game of two, a click on P1 or P2 shows that player. Its bottom. */
float
devices_row(const MenuKit &kit, MenuState &state, ImVec2 at, float width, bool two)
{
	ImDrawList *const list = ImGui::GetWindowDrawList();
	const MenuPalette &palette = kit.palette();
	const float h = kit.picker_height(), spacing = kit.px(10.0f);
	const unsigned players = two ? 2 : 1;
	const float each = (width - spacing * static_cast<float>(players - 1)) / static_cast<float>(players);

	for (unsigned player = 0; player < players; player++) {
		/* The keyboard, any gamepad, each one plugged in (the same name
		 * twice counted), the one wanted when it is not there */
		std::vector<std::string> devices = { "Keyboard and mouse", "Any gamepad (the first free one)" };
		int device = state.gamepad[player] ? 1 : 0;

		for (size_t i = 0; i < state.plugged.size(); i++) {
			const auto &[name, guid] = state.plugged[i];
			const auto before = std::count_if(state.plugged.begin(), state.plugged.begin() + static_cast<std::ptrdiff_t>(i),
			                                  [&name](const auto &other) { return other.first == name; });

			if (state.gamepad[player] && device == 1 && guid == state.wanted[player])
				device = static_cast<int>(devices.size());
			devices.push_back(before ? name + " (" + std::to_string(before + 1) + ")" : name);
		}
		if (state.gamepad[player] && !state.wanted[player].empty() && device == 1) {
			device = static_cast<int>(devices.size());
			devices.push_back((state.wanted_names[player].empty() ? "The gamepad chosen" : state.wanted_names[player]) +
			                  " (not plugged in)");
		}
		/* Any gamepad says which one it is now */
		if (device == 1 && !state.pads[player].empty())
			devices[1] = "Any gamepad: " + state.pads[player];

		float x = at.x + static_cast<float>(player) * (each + spacing);
		float w = each;

		ImGui::PushID(static_cast<int>(player));
		if (two) {
			const char *const name = player ? "P2" : "P1";
			const ImVec2 size = ImGui::CalcTextSize(name);
			const bool shown = state.player == player;

			ImGui::SetCursorScreenPos({ x, at.y });
			if (ImGui::InvisibleButton("player", { size.x + kit.px(8.0f), h }))
				state.player = player, state.catching.reset(), state.catch_next.clear();
			list->AddText({ x, at.y + (h - size.y) / 2.0f }, shown ? palette.ink : palette.dim, name);
			x += size.x + kit.px(8.0f);
			w -= size.x + kit.px(8.0f);
		}
		const MenuKit::Icon kind = device ? MenuKit::Icon::PAD : MenuKit::Icon::KEYBOARD;

		if (kit.picker("device", { x, at.y }, w, devices, device, kind)) {
			const size_t chosen = static_cast<size_t>(device);

			state.player = player;
			state.gamepad[player] = device != 0;
			state.catching.reset();
			if (device == 1 && !state.wanted[player].empty())
				state.want = std::pair<std::string, std::string>();
			else if (chosen >= 2 && chosen - 2 < state.plugged.size())
				state.want = std::pair(state.plugged[chosen - 2].second, state.plugged[chosen - 2].first);
		}
		ImGui::PopID();
	}
	return at.y + h;
}

/* The width of a label column: its longest name, within bounds */
float
column_width(const MenuKit &kit, std::span<const Label> labels, float box)
{
	float longest = 0.0f;

	for (const Label &l : labels)
		longest = std::max(longest, MenuKit::measure(l.text.c_str()));
	return std::clamp(longest + kit.px(16.0f), kit.px(COLUMN_MIN), box * 0.3f);
}

/* The row of the controls with no input, centred at the bottom of the box:
 * its height; drawn and clicked when `draw` */
float
not_mapped(const MenuKit &kit, MenuState &state, std::span<const InputRow> rows, std::span<const size_t> empty, ImVec2 a,
           ImVec2 b, bool draw)
{
	if (empty.empty())
		return 0.0f;

	ImDrawList *const list = ImGui::GetWindowDrawList();
	const MenuPalette &palette = kit.palette();
	const char *const title = "NOT MAPPED";
	const float line = ImGui::GetTextLineHeight(), wide = kit.px(9.0f), high = kit.px(2.0f);
	const float h = line + 2.0f * high, gap = kit.px(6.0f), room = b.x - a.x - 2.0f * kit.px(BOX_INSIDE);
	const float tiny = MenuKit::font(MenuKit::TINY), letters = tiny * 0.12f;
	/* The lines of chips: the title first, then each name */
	std::vector<std::vector<std::pair<int, float>>> lines(1);
	float used = MenuKit::measure(title, tiny, letters) + gap;

	lines[0].push_back({ -1, used - gap });
	for (const size_t r : empty) {
		const float w = MenuKit::measure(rows[r].name.c_str()) + 2.0f * wide;

		if (used + w > room && !lines.back().empty()) {
			lines.emplace_back();
			used = 0.0f;
		}
		lines.back().push_back({ static_cast<int>(r), w });
		used += w + gap;
	}

	const float height = static_cast<float>(lines.size()) * (h + gap);

	if (!draw)
		return height;

	float y = b.y - kit.px(BOX_INSIDE) - height + gap;

	for (const auto &chips : lines) {
		float total = -gap;

		for (const auto &[r, w] : chips)
			total += w + gap;

		float x = a.x + (b.x - a.x - total) / 2.0f;

		for (const auto &[r, w] : chips) {
			if (r < 0) {
				MenuKit::write(list, { x, y + (h - tiny) / 2.0f }, palette.faint, title, tiny, letters);
				x += w + gap;
				continue;
			}

			const InputRow &row = rows[static_cast<size_t>(r)];
			const bool chosen = same(state.catching, row.slot);

			ImGui::SetCursorScreenPos({ x, y });
			ImGui::PushID(r);
			if (ImGui::InvisibleButton("empty", { w, h }) && row.catchable)
				catch_parts(state, { row.slot });

			const bool hovered = ImGui::IsItemHovered();

			ImGui::PopID();
			if (chosen)
				kit.frame(list, { x, y }, { x + w, y + h }, kit.px(3.0f), 0, palette.accent);
			else if (hovered)
				kit.frame(list, { x, y }, { x + w, y + h }, kit.px(3.0f), 0, palette.action);
			else
				kit.dashed_frame(list, { x, y }, { x + w, y + h }, MenuKit::alpha(palette.action, MenuKit::ACTION_LINE));
			MenuKit::write(list, { x + wide, y + high }, chosen ? palette.accent : palette.action, row.name.c_str());
			x += w + gap;
		}
		y += h + gap;
	}
	return height;
}

/* Start mapping: once a control is caught or its time is over, the next
 * one of the player (not a half the whole axis works); the end after the
 * last, or when the user catches another one */
void
next_mapping(MenuState &state, std::span<const InputRow> rows)
{
	if (!state.mapping)
		return;

	/* The control caught now; past the rows: none yet */
	size_t &i = *state.mapping;
	const uint64_t now = SDL_GetTicks();

	if (state.catching) {
		if (i >= rows.size() || !same_control(*state.catching, rows[i].slot)) {
			state.mapping.reset();
			return;
		}
		if (now - state.mapping_since < MAPPING_MS)
			return;
	}
	for (i = i >= rows.size() ? 0 : i + 1; i < rows.size(); i++)
		if (rows[i].catchable && !rows[i].half)
			break;
	state.catch_next.clear();
	if (i >= rows.size()) {
		state.mapping.reset();
		state.catching.reset();
		return;
	}
	state.catching = rows[i].slot;
	state.mapping_since = now;
}

/* The cabinet of the game shown: in its own box, its name and the game's
 * at the top (`framed`), or in the box of the device */
void
cabinet_box(const MenuKit &kit, const MenuState &state, ImVec2 a, ImVec2 b, bool framed)
{
	if (!state.panel || state.shown >= state.games.size())
		return;

	ImDrawList *const list = ImGui::GetWindowDrawList();
	const MenuPalette &palette = kit.palette();
	const std::vector<GameControl> &controls = state.panel->controls();
	const size_t n = controls.size();
	const auto service = std::find_if(controls.begin(), controls.end(),
	                                  [](const GameControl &c) { return c.role == Role::SERVICE && c.player == 0; });
	CabinetNow now{ controls, state.now };

	if (state.touched.size() == n + 3) {
		now.coin = state.touched[n];
		now.test = state.touched[n + 2];
	}
	if (service != controls.end() && static_cast<size_t>(service - controls.begin()) < state.now.size())
		now.service = state.now[static_cast<size_t>(service - controls.begin())].on;
	if (const std::optional<MenuSlot> &slot = state.catching) {
		if (slot->control < 0)
			now.chosen = slot->role == Role::COIN1 ? "coin" : slot->role == Role::TEST ? "test" : "";
		else if (static_cast<size_t>(slot->control) < n)
			now.chosen = controls.begin() + slot->control == service ?
			             "service" : std::string_view(controls[static_cast<size_t>(slot->control)].id);
	}

	float top = a.y + kit.px(BOX_INSIDE);

	if (framed) {
		const std::string &title = state.games[state.shown].second;
		const float pad = kit.px(8.0f);

		const float tiny = MenuKit::font(MenuKit::TINY), small = MenuKit::font(MenuKit::SMALL);

		kit.frame(list, a, b, kit.px(BOX_CUT), 0, palette.line);
		MenuKit::write(list, { a.x + pad, a.y + kit.px(6.0f) + small - tiny }, palette.cabinet, "THE CABINET", tiny,
		               tiny * 0.14f);
		MenuKit::write(list, { b.x - pad - MenuKit::measure(title.c_str(), small), a.y + kit.px(6.0f) }, palette.dim,
		               title.c_str(), small);
		top = a.y + kit.px(6.0f) + small;
	}
	draw_cabinet(kit, state.games[state.shown].first, now, { a.x + kit.px(4.0f), top },
	             { b.x - kit.px(4.0f), b.y - kit.px(6.0f) });
}

/* The controls of the player sorted for the drawing: those on a part of it,
 * one label for each part's input; those on an input it has not; those on
 * nothing */
struct Sorted {
	std::vector<Label> labels;
	std::vector<std::pair<size_t, std::string>> elsewhere;
	std::vector<size_t> empty;
};

Sorted
sort_rows(const MenuState &state, std::span<const InputRow> rows, const PadDrawing &drawing, int kind)
{
	const PadState &now = state.pad_states[state.player & 1];
	Sorted out;

	for (size_t r = 0; r < rows.size(); r++) {
		if (rows[r].inputs.empty()) {
			if (!rows[r].half)
				out.empty.push_back(r);
			continue;
		}
		for (const HostInput &input : rows[r].inputs) {
			const auto [part, key] = part_of(drawing, input.pad);
			std::string name = rows[r].name;

			if (input.pad.with >= 0)
				name += " + " + capitals(pad_label({ PadInput::Type::BUTTON, static_cast<uint8_t>(input.pad.with) }, kind));
			if (part < 0) {
				out.elsewhere.push_back({ r, std::move(name) + " · " + label(input, kind) });
				continue;
			}

			const auto it = std::find_if(out.labels.begin(), out.labels.end(), [key](const Label &l) { return l.key == key; });

			if (it != out.labels.end()) {
				it->names.push_back(name);
				it->chosen |= same(state.catching, rows[r].slot);
				continue;
			}

			Label l{ { name }, r, key, static_cast<size_t>(part) };

			l.chosen = same(state.catching, rows[r].slot);
			l.hot = touched(key, now);
			out.labels.push_back(std::move(l));
		}
	}
	for (Label &l : out.labels)
		l.text = label_text(l.names);
	return out;
}

/* Each column from the top down, each label at the height of its part or
 * under the one above, then from the bottom up; the right one starts under
 * the corner of the box */
void
place_labels(const MenuKit &kit, std::span<Label> labels, float top, float corner, float bottom)
{
	const float gap = kit.px(LABEL_GAP);

	for (const bool left : { true, false }) {
		std::vector<Label *> side;

		for (Label &l : labels)
			if (l.left == left)
				side.push_back(&l);
		std::sort(side.begin(), side.end(), [](const Label *p, const Label *q) { return p->lo.y + p->hi.y < q->lo.y + q->hi.y; });

		float y = -1e9f;

		for (Label *l : side)
			y = l->y = std::max({ (l->lo.y + l->hi.y) / 2.0f, y + gap, top + (left ? 0.0f : corner) });
		y = 1e9f;
		for (auto it = side.rbegin(); it != side.rend(); ++it)
			y = (*it)->y = std::min({ (*it)->y, y - gap, bottom });
	}
}

/* A label: its leader line from the edge of its column, then its name, as
 * wide as the room; a click on it catches */
void
draw_label(const MenuKit &kit, MenuState &state, std::span<const InputRow> rows, Label &l, float edge, float room,
           std::span<const std::pair<ImVec2, ImVec2>> parts)
{
	ImDrawList *const list = ImGui::GetWindowDrawList();
	const MenuPalette &palette = kit.palette();
	const float from = l.left ? edge - kit.px(2.0f) : edge + kit.px(2.0f);
	const float knee = l.left ? from + kit.px(10.0f) : from - kit.px(10.0f);

	l.line = route(l, from, knee, parts, kit);
	if (l.hot) {
		list->AddPolyline(l.line.data(), static_cast<int>(l.line.size()), palette.accent, ImDrawFlags_None,
		                  kit.px(HOT_WIDTH));
		list->AddCircleFilled(l.line.back(), kit.px(DOT), palette.accent);
	} else {
		const float alpha = static_cast<float>(palette.stroke >> IM_COL32_A_SHIFT) * REST_ALPHA;
		const ImU32 rest = (palette.stroke & ~IM_COL32_A_MASK) | (static_cast<ImU32>(alpha) << IM_COL32_A_SHIFT);

		MenuKit::dashes(list, l.line, false, rest, kit.stroke(), kit.px(DASH_ON), kit.px(DASH_OFF));
		list->AddCircleFilled(l.line.back(), kit.px(DOT), rest);
	}

	const std::string text = MenuKit::fitted(l.text, room);
	const float line = ImGui::GetTextLineHeight(), w = MenuKit::measure(text.c_str());
	const ImVec2 at = { l.left ? edge - kit.px(6.0f) - w : edge + kit.px(6.0f), l.y - line / 2.0f };

	ImGui::SetCursorScreenPos(at);
	ImGui::PushID(l.key);
	if (ImGui::InvisibleButton("label", { w, line }) && rows[l.row].catchable)
		catch_parts(state, l.chosen ? std::vector<MenuSlot>() : std::vector<MenuSlot>{ rows[l.row].slot });
	ImGui::PopID();

	const ImU32 colour = l.hot || l.chosen ? palette.accent : palette.ink;

	MenuKit::write(list, at, colour, text.c_str());
	if (l.hot)
		list->AddLine({ at.x, at.y + line + kit.px(1.0f) }, { at.x + w, at.y + line + kit.px(1.0f) }, colour, kit.stroke());
}

/* The controls on an input the drawing has not, at the foot of the column
 * with fewer names */
void
draw_elsewhere(const MenuKit &kit, const MenuState &state, std::span<const InputRow> rows, const Sorted &sorted,
               ImVec2 a, ImVec2 b, float bottom)
{
	if (sorted.elsewhere.empty())
		return;

	ImDrawList *const list = ImGui::GetWindowDrawList();
	const float gap = kit.px(LABEL_GAP), line = ImGui::GetTextLineHeight(), w = (b.x - a.x) / 2.0f - kit.px(10.0f);
	const auto last = [&](bool left) {
		float y = a.y + kit.px(LABEL_EDGE) - gap;

		for (const Label &l : sorted.labels)
			if (l.left == left)
				y = std::max(y, l.y);
		return y;
	};
	const bool left = last(true) <= last(false);
	float y = std::max(last(left) + gap * 1.6f, bottom - static_cast<float>(sorted.elsewhere.size() - 1) * gap);

	for (const auto &[r, whole] : sorted.elsewhere) {
		const std::string text = MenuKit::fitted(whole, w);
		const float tw = MenuKit::measure(text.c_str());
		const ImVec2 at = { left ? a.x + kit.px(6.0f) : b.x - kit.px(6.0f) - tw, y - line / 2.0f };

		MenuKit::write(list, at, same(state.catching, rows[r].slot) ? kit.palette().accent : kit.palette().dim,
		               text.c_str());
		y += gap;
	}
}

/* The gamepad drawn in the box, the name of each control beside the part
 * that works it, joined by a leader line: pale dashes at rest, the accent
 * while it is worked. A control on an input the drawing has not is said at
 * the foot of a column; a control on nothing waits in the row at the bottom
 * (Not mapped). A click on a name, or in that row, catches its next
 * input. */
void
pad_box(const MenuKit &kit, MenuState &state, std::span<const InputRow> rows, ImVec2 a, ImVec2 b, float corner)
{
	const unsigned me = state.player & 1;
	const int kind = state.pad_types[me];
	const PadDrawing &drawing = pad_drawing(kind);
	Sorted sorted = sort_rows(state, rows, drawing, kind);
	const float tray = not_mapped(kit, state, rows, sorted.empty, a, b, false);
	const float column = column_width(kit, sorted.labels, b.x - a.x), inside = kit.px(BOX_INSIDE);
	const float bottom = b.y - kit.px(LABEL_EDGE) - tray;
	const PadView view(drawing, { a.x + column, a.y + inside }, { b.x - column, b.y - inside - tray });
	std::vector<std::pair<ImVec2, ImVec2>> parts;
	uint32_t chosen = 0;

	/* What a leader line should not cross: each part, a little smaller */
	for (size_t p = 0; p < drawing.parts.size(); p++) {
		const auto [lo, hi] = view.box(p);
		const float in = kit.px(1.5f);

		parts.push_back({ { lo.x + in, lo.y + in }, { hi.x - in, hi.y - in } });
	}
	for (Label &l : sorted.labels) {
		const auto [lo, hi] = view.box(l.part);

		l.lo = lo, l.hi = hi;
		l.left = (lo.x + hi.x) / 2.0f < (a.x + b.x) / 2.0f;
		if (l.chosen)
			chosen |= 1u << l.part;
	}
	view.draw(ImGui::GetWindowDrawList(), kit.palette(), state.pad_states[me], chosen, kit.px(1.0f));
	place_labels(kit, sorted.labels, a.y + kit.px(LABEL_EDGE), corner, bottom);
	for (Label &l : sorted.labels)
		draw_label(kit, state, rows, l, l.left ? a.x + column : b.x - column, column - kit.px(10.0f), parts);
	draw_elsewhere(kit, state, rows, sorted, a, b, bottom);
	not_mapped(kit, state, rows, sorted.empty, a, b, true);
}

/* The keys of the drawing of the keyboard, row by row: the key, its name,
 * its width in keys */
struct DrawnKey {
	SDL_Keycode key;
	const char *name;
	float width;
};

constexpr DrawnKey KEY_ROWS[5][14] = {
	{ { SDLK_ESCAPE, "Esc", 1 }, { SDLK_1, "1", 1 }, { SDLK_2, "2", 1 }, { SDLK_3, "3", 1 }, { SDLK_4, "4", 1 },
	  { SDLK_5, "5", 1 }, { SDLK_6, "6", 1 }, { SDLK_7, "7", 1 }, { SDLK_8, "8", 1 }, { SDLK_9, "9", 1 },
	  { SDLK_0, "0", 1 }, { SDLK_MINUS, "-", 1 }, { SDLK_EQUALS, "=", 1 }, { SDLK_BACKSPACE, "⌫", 2 } },
	{ { SDLK_TAB, "Tab", 1.5f }, { SDLK_Q, "Q", 1 }, { SDLK_W, "W", 1 }, { SDLK_E, "E", 1 }, { SDLK_R, "R", 1 },
	  { SDLK_T, "T", 1 }, { SDLK_Y, "Y", 1 }, { SDLK_U, "U", 1 }, { SDLK_I, "I", 1 }, { SDLK_O, "O", 1 },
	  { SDLK_P, "P", 1 }, { SDLK_LEFTBRACKET, "[", 1 }, { SDLK_RIGHTBRACKET, "]", 1 }, { SDLK_BACKSLASH, "\\", 1.5f } },
	{ { SDLK_CAPSLOCK, "Caps", 1.75f }, { SDLK_A, "A", 1 }, { SDLK_S, "S", 1 }, { SDLK_D, "D", 1 }, { SDLK_F, "F", 1 },
	  { SDLK_G, "G", 1 }, { SDLK_H, "H", 1 }, { SDLK_J, "J", 1 }, { SDLK_K, "K", 1 }, { SDLK_L, "L", 1 },
	  { SDLK_SEMICOLON, ";", 1 }, { SDLK_APOSTROPHE, "'", 1 }, { SDLK_RETURN, "⏎", 2.25f }, {} },
	{ { SDLK_LSHIFT, "Shift", 2.25f }, { SDLK_Z, "Z", 1 }, { SDLK_X, "X", 1 }, { SDLK_C, "C", 1 }, { SDLK_V, "V", 1 },
	  { SDLK_B, "B", 1 }, { SDLK_N, "N", 1 }, { SDLK_M, "M", 1 }, { SDLK_COMMA, ",", 1 }, { SDLK_PERIOD, ".", 1 },
	  { SDLK_SLASH, "/", 1 }, { SDLK_RSHIFT, "Shift", 2.75f }, {}, {} },
	{ { SDLK_LCTRL, "Ctrl", 1.5f }, { SDLK_LGUI, "", 1.25f }, { SDLK_LALT, "Alt", 1.25f }, { SDLK_SPACE, "", 6.25f },
	  { SDLK_RALT, "Alt", 1.25f }, { SDLK_RCTRL, "Ctrl", 1.5f }, {}, {}, {}, {}, {}, {}, {}, {} },
};

/* A quadratic curve from p, through the control c, to q */
void
curve_to(std::vector<ImVec2> &points, ImVec2 c, ImVec2 q)
{
	const ImVec2 p = points.back();

	for (int k = 1; k <= 8; k++) {
		const float t = static_cast<float>(k) / 8.0f, u = 1.0f - t;

		points.push_back({ u * u * p.x + 2 * u * t * c.x + t * t * q.x, u * u * p.y + 2 * u * t * c.y + t * t * q.y });
	}
}

/* The keyboard drawn (and the mouse): the keys of the controls of the
 * player clear, the others faint, a key held lit; a mouse button pressed
 * lit. Its controls change in Classic. */
void
keyboard_box(const MenuKit &kit, const MenuState &state, std::span<const InputRow> rows, ImVec2 a, ImVec2 b,
             float corner)
{
	ImDrawList *const list = ImGui::GetWindowDrawList();
	const MenuPalette &palette = kit.palette();
	/* The drawing of the mock-up, in its units: 640 by 220 from (0, 96) */
	constexpr float W = 640.0f, H = 220.0f, TOP = 96.0f, U = 28.0f, X0 = 14.0f, Y0 = 130.0f, GAP = 3.0f;
	const ImVec2 lo = { a.x + kit.px(BOX_INSIDE), a.y + corner + kit.px(BOX_INSIDE) };
	const ImVec2 hi = { b.x - kit.px(BOX_INSIDE), b.y - kit.px(BOX_INSIDE) };
	const float k = std::max(0.0f, std::min((hi.x - lo.x) / W, (hi.y - lo.y) / H));
	const ImVec2 o = { (lo.x + hi.x - W * k) / 2.0f, (lo.y + hi.y - H * k) / 2.0f - TOP * k };
	const auto at = [&](float x, float y) { return ImVec2{ o.x + x * k, o.y + y * k }; };
	const ImU32 faint = (palette.ink & ~IM_COL32_A_MASK) | (0x2Eu << IM_COL32_A_SHIFT);
	std::vector<SDL_Keycode> bound;

	for (const InputRow &row : rows)
		for (const HostInput &input : row.inputs)
			if (input.kind == HostInput::Kind::KEY)
				bound.push_back(input.key);

	kit.frame(list, at(X0 - 8, Y0 - 10), at(X0 - 8 + 18 * U + 30, Y0 - 10 + 5 * U + 18), kit.px(4.0f), palette.body,
	               palette.stroke);

	const float size = 10.0f * k;
	const auto key = [&](const DrawnKey &dk, float x, float y, float w) {
		const bool on = std::find(bound.begin(), bound.end(), dk.key) != bound.end();
		const bool down = std::find(state.keys_held.begin(), state.keys_held.end(), dk.key) != state.keys_held.end();
		const float dy = down ? 1.5f : 0.0f;
		const ImVec2 p = at(x, y + dy), q = at(x + w, y + dy + U - GAP - 1);
		const float text = MenuKit::measure(dk.name, size);

		list->AddRectFilled(p, q, down ? palette.accent : palette.part, 4.0f * k);
		list->AddRect(p, q, on ? palette.stroke : faint, 4.0f * k, ImDrawFlags_None, kit.px(1.0f));
		MenuKit::write(list, { (p.x + q.x - text) / 2.0f, (p.y + q.y - size) / 2.0f },
		               down ? palette.on_accent : on ? palette.ink : palette.faint, dk.name, size);
	};

	for (size_t r = 0; r < 5; r++) {
		float x = X0;

		for (const DrawnKey &dk : KEY_ROWS[r]) {
			if (!dk.name)
				break;
			key(dk, x, Y0 + static_cast<float>(r) * U, dk.width * U - GAP);
			x += dk.width * U;
		}
	}

	const float ax = X0 + 15 * U + 8;

	key({ SDLK_UP, "↑", 1 }, ax + U, Y0 + 3 * U, U - GAP);
	key({ SDLK_LEFT, "←", 1 }, ax, Y0 + 4 * U, U - GAP);
	key({ SDLK_DOWN, "↓", 1 }, ax + U, Y0 + 4 * U, U - GAP);
	key({ SDLK_RIGHT, "→", 1 }, ax + 2 * U, Y0 + 4 * U, U - GAP);

	/* The mouse: its outline, its two buttons, its wheel */
	std::vector<ImVec2> body = { { 584, 116 } }, left = { { 582, 118 } }, right = { { 586, 118 } };

	curve_to(body, { 620, 116 }, { 622, 160 });
	body.push_back({ 622, 230 });
	curve_to(body, { 622, 282 }, { 584, 282 });
	curve_to(body, { 546, 282 }, { 546, 230 });
	body.push_back({ 546, 160 });
	curve_to(body, { 548, 116 }, { 584, 116 });
	curve_to(left, { 550, 120 }, { 548, 160 });
	left.insert(left.end(), { { 548, 176 }, { 582, 176 } });
	curve_to(right, { 618, 120 }, { 620, 160 });
	right.insert(right.end(), { { 620, 176 }, { 586, 176 } });
	for (std::vector<ImVec2> *shape : { &body, &left, &right })
		for (ImVec2 &p : *shape)
			p = at(p.x, p.y);
	list->AddConvexPolyFilled(body.data(), static_cast<int>(body.size()), palette.body);
	for (const auto &[half, button] : { std::pair{ &left, 0 }, std::pair{ &right, 1 } })
		if (ImGui::IsMouseDown(button))
			list->AddConvexPolyFilled(half->data(), static_cast<int>(half->size()), palette.accent);
	list->AddPolyline(body.data(), static_cast<int>(body.size()), palette.stroke, ImDrawFlags_Closed, kit.px(1.1f));
	list->AddLine(at(584, 116), at(584, 176), palette.outline, kit.px(0.8f));
	list->AddLine(at(547, 176), at(621, 176), palette.outline, kit.px(0.8f));
	list->AddRectFilled(at(580, 132), at(588, 154), ImGui::IsMouseDown(2) ? palette.accent : palette.part, 4.0f * k);
	list->AddRect(at(580, 132), at(588, 154), palette.stroke, 4.0f * k, ImDrawFlags_None, kit.px(1.0f));
}

/* What the page asks while the next input of a part is caught (mock-up
 * v50) */
std::string
prompt(const MenuState &state, const MenuSlot &slot)
{
	const bool pad = state.on_pad();
	const std::string thing = pad ? "button" : "key";
	const GameControl *const control = slot.control >= 0 ? &state.panel->controls()[static_cast<size_t>(slot.control)] :
	                                   nullptr;

	if (!control || control->type == GameControl::Type::BUTTON)
		return "Press the " + thing + " you want";
	if (control->type == GameControl::Type::POSITIONS)
		return "The " + thing + (slot.part == 2 ? " that shifts up" : " that shifts down");

	const bool two_ways = GameControl::half(control->low, false);
	const bool vertical = control->low == GameControl::Low::UP || control->low == GameControl::Low::DOWN;

	if (slot.part == 0)
		return two_ways ? "Move a stick along it, or press a button for each way" : "Press the trigger or pedal you want";
	if (!two_ways)
		return "Press the " + thing + " you want";
	return "The " + thing + " for " + (slot.part == 1 ? (vertical ? "forward" : "left") : (vertical ? "back" : "right"));
}

/* The controls as a list in two columns (Classic): each name, and its
 * inputs on the device shown in a pill, filled as far as the control is
 * worked; a click on the name catches its parts in turn, on a part that
 * part */
void
list_box(const MenuKit &kit, MenuState &state, std::span<const InputRow> rows, ImVec2 a, ImVec2 b, float corner)
{
	ImDrawList *list = ImGui::GetWindowDrawList();
	const MenuPalette &palette = kit.palette();
	const int kind = state.pad_types[state.player & 1];
	const std::vector<Group> all = groups(state, rows);
	const float line = ImGui::GetTextLineHeight(), gap = kit.px(28.0f), wide = kit.px(8.0f), high = kit.px(2.0f);
	const float apart = kit.px(8.0f);
	ImVec2 lo = { a.x + kit.px(14.0f), a.y + kit.px(BOX_INSIDE) + corner };
	const ImVec2 hi = { b.x - kit.px(14.0f), b.y - kit.px(BOX_INSIDE) };
	const size_t half = (all.size() + 1) / 2;
	/* As high as the room leaves them, at most as in the mock-up; when
	 * they do not fit, the list scrolls, its bar beside the columns */
	const float h = std::clamp((hi.y - lo.y) / static_cast<float>(std::max<size_t>(half, 1)), line + 2.0f * high + kit.px(4.0f),
	                           line + kit.px(16.0f));
	const bool scrolls = static_cast<float>(half) * h > hi.y - lo.y;
	const float column = (hi.x - lo.x - gap - (scrolls ? ImGui::GetStyle().ScrollbarSize : 0.0f)) / 2.0f;

	if (scrolls) {
		ImGui::SetCursorScreenPos(lo);
		ImGui::BeginChild("list", { hi.x - lo.x, hi.y - lo.y }, ImGuiChildFlags_None, ImGuiWindowFlags_NoBackground);
		lo = ImGui::GetCursorScreenPos();
		list = ImGui::GetWindowDrawList();
	}

	const float top = scrolls ? lo.y : std::max(lo.y, (lo.y + hi.y - static_cast<float>(half) * h) / 2.0f);

	for (size_t g = 0; g < all.size(); g++) {
		const Group &group = all[g];
		const bool second = g >= half;
		const ImVec2 at = { lo.x + (second ? column + gap : 0.0f), top + static_cast<float>(second ? g - half : g) * h };
		/* The pill: its parts, the longest cut first where they are too
		 * long together */
		const size_t n = group.parts.size();
		const float room = column * 0.6f - 2.0f * wide;
		std::vector<std::string> texts;
		std::vector<float> widths;
		bool empty = true;

		for (const Group::Part &part : group.parts) {
			texts.push_back(part_text(state, rows, part, kind));
			widths.push_back(MenuKit::measure(texts.back().c_str()));
			empty = empty && rows[part.row].inputs.empty() && !same(state.catching, rows[part.row].slot);
		}

		const auto sum = [&] {
			float together = static_cast<float>(n - 1) * apart;

			for (const float w : widths)
				together += w;
			return together;
		};

		for (size_t k = 0; k < n && sum() > room; k++) {
			const size_t longest = static_cast<size_t>(std::max_element(widths.begin(), widths.end()) - widths.begin());

			texts[longest] = MenuKit::fitted(texts[longest], std::max(0.0f, widths[longest] - (sum() - room)));
			widths[longest] = MenuKit::measure(texts[longest].c_str());
		}

		const float total = sum();

		const float chip_w = std::max(kit.px(34.0f), total + 2.0f * wide);
		const ImVec2 ca = { at.x + column - chip_w, at.y + (h - line) / 2.0f - high };
		const ImVec2 cb = { at.x + column, ca.y + line + 2.0f * high };
		ImGui::PushID(static_cast<int>(g));
		ImGui::SetCursorScreenPos(at);
		if (ImGui::InvisibleButton("name", { ca.x - at.x, h }))
			catch_parts(state, group.chosen ? std::vector<MenuSlot>() : group.to_catch(rows));

		bool hovered = ImGui::IsItemHovered();
		float x = ca.x + wide + (chip_w - 2.0f * wide - total) / 2.0f;

		for (size_t p = 0; p < n; p++) {
			const InputRow &row = rows[group.parts[p].row];
			const float w = MenuKit::measure(texts[p].c_str());
			const float from = p ? x - apart / 2.0f : ca.x, to = p + 1 < n ? x + w + apart / 2.0f : cb.x;

			ImGui::SetCursorScreenPos({ from, at.y });
			ImGui::PushID(static_cast<int>(p));
			if (ImGui::InvisibleButton("part", { to - from, h }) && row.catchable)
				catch_parts(state, same(state.catching, row.slot) ? std::vector<MenuSlot>() :
				                                                     std::vector<MenuSlot>{ row.slot });
			hovered = hovered || ImGui::IsItemHovered();
			ImGui::PopID();
			x += w + apart;
		}
		ImGui::PopID();

		if (hovered)
			list->AddRectFilled(at, { at.x + column, at.y + h }, palette.hover);
		list->AddLine({ at.x, at.y + h }, { at.x + column, at.y + h }, palette.line, kit.stroke());
		MenuKit::write(list, { at.x + kit.px(4.0f), at.y + (h - line) / 2.0f },
		               group.lit || group.chosen ? palette.accent : palette.ink,
		               MenuKit::fitted(group.name, ca.x - at.x - kit.px(14.0f)).c_str());
		if (group.amount > 0.01f) {
			list->PushClipRect(ca, { ca.x + chip_w * group.amount, cb.y }, true);
			list->AddRectFilled(ca, cb, MenuKit::alpha(palette.accent, 0.55f), (cb.y - ca.y) / 2.0f);
			list->PopClipRect();
		}
		if (empty)
			kit.dashed_pill(list, ca, cb, palette.outline);
		else
			list->AddRect(ca, cb, group.chosen || group.lit ? palette.accent : palette.outline, (cb.y - ca.y) / 2.0f,
			              ImDrawFlags_None, kit.stroke());
		x = ca.x + wide + (chip_w - 2.0f * wide - total) / 2.0f;
		for (size_t p = 0; p < n; p++) {
			const InputRow &row = rows[group.parts[p].row];
			const ImU32 colour = same(state.catching, row.slot) ? palette.accent :
			                     row.inputs.empty() ? palette.faint : palette.ink;

			MenuKit::write(list, { x, ca.y + high }, colour, texts[p].c_str());
			x += MenuKit::measure(texts[p].c_str()) + apart;
		}
	}
	if (scrolls) {
		ImGui::SetCursorScreenPos(lo);
		ImGui::Dummy({ hi.x - lo.x, static_cast<float>(half) * h });
		ImGui::EndChild();
	}
}

/* The buttons under the drawing, as many on a line as fit: Defaults, Clear
 * mapping (Undo for a while after), Start mapping; the vibration of the
 * pad where the game shakes it. Their height, with the room around them;
 * drawn and clicked when `draw`. */
float
buttons_bar(const MenuKit &kit, MenuState &state, std::span<const InputRow> rows, ImVec2 at, float width, bool draw)
{
	using Icon = MenuKit::Icon;
	using Tone = MenuKit::Tone;
	static constexpr const char *ON_OFF[] = { "on", "off" };
	const float across = kit.px(12.0f), down = kit.px(5.0f), h = kit.button_size("Defaults", Icon::DEFAULTS).y;
	const char *const clear = state.undo ? "Undo" : "Clear mapping";
	const ImVec2 cells = kit.segments_size(ON_OFF);
	const char *const shake_name = ImGui::CalcTextSize("Vibration (experimental)").x + kit.px(8.0f) + cells.x <= width ?
	                               "Vibration (experimental)" : "Vibration";
	std::vector<float> widths = { kit.button_size("Defaults", Icon::DEFAULTS).x,
		                      kit.button_size(clear, state.undo ? Icon::UNDO : Icon::CLEAR).x };

	if (!state.mapping)
		widths.push_back(kit.button_size("Start mapping", Icon::MAP).x);
	if (state.can_shake)
		widths.push_back(ImGui::CalcTextSize(shake_name).x + kit.px(8.0f) + cells.x);

	/* Each at its place, left to right, a line under the other */
	std::vector<ImVec2> places;
	float x = 0.0f, y = 0.0f;

	for (const float w : widths) {
		if (x > 0.0f && x + w > width) {
			x = 0.0f;
			y += h + down;
		}
		places.push_back({ at.x + x, at.y + kit.px(5.0f) + y });
		x += w + across;
	}

	const float height = kit.px(5.0f) + y + h + kit.px(6.0f);

	if (!draw)
		return height;

	ImDrawList *const list = ImGui::GetWindowDrawList();
	size_t i = 0;

	if (kit.button("Defaults", places[i++], false, Icon::DEFAULTS))
		state.ask = MenuState::Ask::DEFAULTS;
	if (kit.button(clear, places[i++], false, state.undo ? Icon::UNDO : Icon::CLEAR,
	               state.undo ? Tone::STRONG : Tone::DANGER))
		state.ask = state.undo ? MenuState::Ask::UNDO : MenuState::Ask::CLEAR;
	if (!state.mapping && kit.button("Start mapping", places[i++], false, Icon::MAP)) {
		/* The first control: the one before it is none */
		state.mapping = rows.size();
		state.catching.reset();
		state.catch_next.clear();
	}
	if (state.can_shake) {
		const ImVec2 p = places[i];
		const float name_w = ImGui::CalcTextSize(shake_name).x;
		int shake = state.shake ? 0 : 1;

		list->AddText({ p.x, p.y + (h - ImGui::GetTextLineHeight()) / 2.0f }, kit.palette().dim, shake_name);
		if (kit.segments({ p.x + name_w + kit.px(8.0f), p.y + (h - cells.y) / 2.0f }, ON_OFF, shake))
			state.shake = shake == 0;
	}
	return height;
}

} /* namespace */

void
Menu::page_input(MenuKit &kit, MenuState &state) const
{
	if (!state.panel)
		return;

	using Icon = MenuKit::Icon;
	const MenuPalette &palette = kit.palette();
	const std::vector<GameControl> &controls = state.panel->controls();
	/* Every cabinet has the SERVICE of player 2: a game of two players has
	 * more of theirs */
	const bool two = std::any_of(controls.begin(), controls.end(),
	                             [](const GameControl &c) { return c.player == 1 && c.role != Role::SERVICE; });

	if (!two)
		state.player = 0;
	if (!state.catching)
		state.catch_next.clear();

	const ImVec2 top = ImGui::GetCursorScreenPos(), room = ImGui::GetContentRegionAvail();
	const float width = room.x, gap = kit.px(ROW_GAP);
	float y = top.y;

	y = games_row(kit, state, { top.x, y }, width) + gap;
	y = devices_row(kit, state, { top.x, y }, width, two) + gap;

	const std::vector<InputRow> rows = input_rows(state);

	next_mapping(state, rows);
	/* A window wide enough (16:9) has the cabinet beside the device; a
	 * narrower one shows either */
	const ImVec2 window = ImGui::GetIO().DisplaySize;
	const bool wide = window.x >= WIDE * window.y;
	const float bottom = top.y + room.y - kit.px(4.0f);
	const float split = wide ? top.x + (width - gap) * PAD_SHARE : top.x + width;
	const ImVec2 a = { top.x, y }, b = { split, bottom };
	const bool cabinet = !wide && state.cabinet_shown;
	/* The buttons at the bottom of the box, under a line (none while the
	 * cabinet is in it); the drawing above them */
	const ImVec2 bar_at = { a.x + kit.px(10.0f), 0.0f };
	const float bar_w = b.x - a.x - kit.px(20.0f);
	const float bar_top = cabinet ? b.y : b.y - buttons_bar(kit, state, rows, bar_at, bar_w, false);
	const ImVec2 inside = { b.x, bar_top };
	/* The buttons in the corner of the box: Classic for a gamepad, the
	 * swap of the device and the cabinet in a narrow window */
	const char *const swap = cabinet ? "Controller" : "Cabinet";
	const bool classic = !cabinet;
	const ImVec2 classic_size = kit.button_size("Classic", Icon::LIST), swap_size = kit.button_size(swap, Icon::SWAP);
	const float corner = classic || !wide ? std::max(classic_size.y, swap_size.y) + kit.px(5.0f) : 0.0f;

	kit.frame(ImGui::GetWindowDrawList(), a, b, kit.px(BOX_CUT), 0, palette.line);
	if (cabinet)
		cabinet_box(kit, state, a, b, false);
	else if (state.classic)
		list_box(kit, state, rows, a, inside, corner);
	else if (state.on_pad())
		pad_box(kit, state, rows, a, inside, corner);
	else
		keyboard_box(kit, state, rows, a, inside, corner);
	if (wide)
		cabinet_box(kit, state, { split + gap, y }, { top.x + width, bottom }, true);

	float x = b.x - kit.px(6.0f);

	if (!wide) {
		x -= swap_size.x;
		if (kit.button(swap, { x, a.y + kit.px(5.0f) }, false, Icon::SWAP))
			state.cabinet_shown = !state.cabinet_shown;
		x -= kit.px(6.0f);
	}
	if (classic && kit.button("Classic", { x - classic_size.x, a.y + kit.px(5.0f) }, state.classic, Icon::LIST))
		state.classic = !state.classic;
	if (!cabinet) {
		ImGui::GetWindowDrawList()->AddLine({ a.x + 1.0f, bar_top }, { b.x - 1.0f, bar_top }, palette.line, kit.stroke());
		buttons_bar(kit, state, rows, { bar_at.x, bar_top }, bar_w, true);
	}
	ImGui::SetCursorScreenPos(top);
	ImGui::Dummy({ width, room.y - kit.px(4.0f) });
}

/* What the page says at the bottom (mock-up v50): the part caught, what to
 * do, Skip and Stop while each control is caught in turn; else the control
 * worked now and its inputs, filled as far as it is; else how it works */
float
Menu::input_foot(const MenuKit &kit, MenuState &state, ImVec2 at, float width, float height, bool draw)
{
	static constexpr char REST[] = "Touch a button or move a stick: its control lights. Click a name to change it.";
	const float line = ImGui::GetTextLineHeight();

	if (!draw)
		return std::max({ line, kit.button_size("Stop").y, ImGui::CalcTextSize(REST, nullptr, false, width).y });
	if (!state.panel)
		return height;

	ImDrawList *const list = ImGui::GetWindowDrawList();
	const MenuPalette &palette = kit.palette();
	const std::vector<InputRow> rows = input_rows(state);
	const int kind = state.pad_types[state.player & 1];
	const float y = at.y + (height - line) / 2.0f, space = kit.px(10.0f), right = at.x + width;
	float x = at.x;
	const auto say = [&](const std::string &text, ImU32 colour) {
		const std::string shown = MenuKit::fitted(text, std::max(0.0f, right - x));

		MenuKit::write(list, { x, y }, colour, shown.c_str());
		x += MenuKit::measure(shown.c_str()) + space;
	};
	const auto caught = std::find_if(rows.begin(), rows.end(),
	                                 [&](const InputRow &row) { return same(state.catching, row.slot); });

	if (caught != rows.end()) {
		const double beat = static_cast<double>(SDL_GetTicks() % 1600) / 1600.0;
		const float beating = 0.725f + 0.275f * static_cast<float>(std::cos(beat * 2.0 * std::numbers::pi));
		const ImU32 pulse = MenuKit::alpha(palette.accent, beating);
		const bool mapping = state.mapping && *state.mapping < rows.size();
		/* What comes after the prompt: the seconds left and two buttons,
		 * or how to let it be */
		float after = ImGui::CalcTextSize("Esc cancels").x;

		if (mapping) {
			size_t done = 0, all = 0;

			for (size_t r = 0; r < rows.size(); r++)
				if (rows[r].catchable && !rows[r].half)
					all++, done += r <= *state.mapping;
			say(std::to_string(done) + " / " + std::to_string(all), palette.accent);
			after = ImGui::CalcTextSize("6 s").x + space + kit.button_size("Skip ›").x + space + kit.button_size("Stop").x;
		}
		say(caught->name, palette.accent);

		const std::string asked = prompt(state, caught->slot);
		const float room = std::max(0.0f, right - x - space - after);
		const std::string shown = MenuKit::fitted(asked, room);

		MenuKit::write(list, { x, y }, pulse, shown.c_str());
		x += MenuKit::measure(shown.c_str()) + space;
		if (!mapping) {
			say("Esc cancels", palette.faint);
			return height;
		}

		const uint64_t gone = SDL_GetTicks() - state.mapping_since;
		const uint64_t left = gone >= MAPPING_MS ? 0 : (MAPPING_MS - gone + 999) / 1000;
		const float button_y = at.y + (height - kit.button_size("Stop").y) / 2.0f;

		say(std::to_string(left) + " s", palette.faint);
		if (kit.button("Skip ›", { x, button_y }, false))
			state.mapping_since = 0;
		x += kit.button_size("Skip ›").x + space;
		if (kit.button("Stop", { x, button_y }, false))
			state.mapping.reset(), state.catching.reset(), state.catch_next.clear();
		return height;
	}

	for (const Group &group : groups(state, rows)) {
		if (!group.lit)
			continue;

		std::string text;

		for (const Group::Part &part : group.parts)
			text += (text.empty() ? "" : "  ") + part_text(state, rows, part, kind);
		say(group.name, palette.accent);

		const float chip_w = std::min(MenuKit::measure(text.c_str()) + 2.0f * kit.px(7.0f), std::max(0.0f, right - x));
		const ImVec2 ca = { x, y - kit.px(2.0f) }, cb = { x + chip_w, y + line + kit.px(2.0f) };

		if (group.amount > 0.01f) {
			list->PushClipRect(ca, { ca.x + chip_w * group.amount, cb.y }, true);
			list->AddRectFilled(ca, cb, MenuKit::alpha(palette.accent, 0.55f), kit.px(5.0f));
			list->PopClipRect();
		}
		list->AddRect(ca, cb, palette.stroke, kit.px(5.0f), ImDrawFlags_None, kit.stroke());
		MenuKit::write(list, { x + kit.px(7.0f), y }, palette.ink,
		               MenuKit::fitted(text, chip_w - 2.0f * kit.px(7.0f)).c_str());
		return height;
	}
	const float rest_h = ImGui::CalcTextSize(REST, nullptr, false, width).y;

	list->AddText(ImGui::GetFont(), ImGui::GetFontSize(), { at.x, at.y + (height - rest_h) / 2.0f }, palette.dim, REST,
	              nullptr, width);
	return height;
}

} /* namespace odin */
