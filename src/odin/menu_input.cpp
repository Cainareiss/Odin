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

bool
is_playstation(SDL_GamepadType type)
{
	return type == SDL_GAMEPAD_TYPE_PS3 || type == SDL_GAMEPAD_TYPE_PS4 || type == SDL_GAMEPAD_TYPE_PS5;
}

/* The name of an input of a gamepad of that kind, as its maker prints it */
std::string
pad_label(const PadInput &pad, SDL_GamepadType type)
{
	const bool ps = is_playstation(type);

	if (pad.type == PadInput::Type::BUTTON) {
		const auto button = static_cast<SDL_GamepadButton>(pad.index);

		switch (button) {
		case SDL_GAMEPAD_BUTTON_SOUTH:
		case SDL_GAMEPAD_BUTTON_EAST:
		case SDL_GAMEPAD_BUTTON_WEST:
		case SDL_GAMEPAD_BUTTON_NORTH: {
			static const char *const FACES[] = { "?", "A", "B", "X", "Y", "Cross", "Circle", "Square", "Triangle" };
			const SDL_GamepadButtonLabel label =
				SDL_GetGamepadButtonLabelForType(type == SDL_GAMEPAD_TYPE_UNKNOWN ? SDL_GAMEPAD_TYPE_STANDARD : type, button);

			return FACES[static_cast<size_t>(label) < std::size(FACES) ? static_cast<size_t>(label) : 0];
		}
		case SDL_GAMEPAD_BUTTON_BACK: return ps ? "Share" : "Back";
		case SDL_GAMEPAD_BUTTON_GUIDE: return "Guide";
		case SDL_GAMEPAD_BUTTON_START: return ps ? "Options" : "Start";
		case SDL_GAMEPAD_BUTTON_LEFT_STICK: return "L3";
		case SDL_GAMEPAD_BUTTON_RIGHT_STICK: return "R3";
		case SDL_GAMEPAD_BUTTON_LEFT_SHOULDER: return ps ? "L1" : "LB";
		case SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER: return ps ? "R1" : "RB";
		case SDL_GAMEPAD_BUTTON_DPAD_UP: return "D-pad up";
		case SDL_GAMEPAD_BUTTON_DPAD_DOWN: return "D-pad down";
		case SDL_GAMEPAD_BUTTON_DPAD_LEFT: return "D-pad left";
		case SDL_GAMEPAD_BUTTON_DPAD_RIGHT: return "D-pad right";
		case SDL_GAMEPAD_BUTTON_TOUCHPAD: return "Touchpad";
		default: {
			const char *const name = SDL_GetGamepadStringForButton(button);

			return name ? name : "?";
		}
		}
	}

	const auto axis = static_cast<SDL_GamepadAxis>(pad.index);

	if (axis == SDL_GAMEPAD_AXIS_LEFT_TRIGGER)
		return ps ? "L2" : "LT";
	if (axis == SDL_GAMEPAD_AXIS_RIGHT_TRIGGER)
		return ps ? "R2" : "RT";

	const bool left = axis == SDL_GAMEPAD_AXIS_LEFTX || axis == SDL_GAMEPAD_AXIS_LEFTY;
	const bool across = axis == SDL_GAMEPAD_AXIS_LEFTX || axis == SDL_GAMEPAD_AXIS_RIGHTX;
	std::string out = left ? "Left stick " : "Right stick ";

	if (pad.type == PadInput::Type::AXIS)
		out += across ? "X" : "Y";
	else if (across)
		out += pad.type == PadInput::Type::AXIS_POSITIVE ? "right" : "left";
	else
		out += pad.type == PadInput::Type::AXIS_POSITIVE ? "down" : "up";
	return pad.inverted ? out + " (inverted)" : out;
}

std::string
label(const HostInput &input, SDL_GamepadType type)
{
	static const char *const MOUSE[] = { "Mouse left", "Mouse middle", "Mouse right" };

	switch (input.kind) {
	case HostInput::Kind::KEY:
		return SDL_GetKeyName(input.key);
	case HostInput::Kind::PAD:
		if (input.pad.with >= 0) {
			const PadInput with = { PadInput::Type::BUTTON, static_cast<uint8_t>(input.pad.with) };

			return pad_label(with, type) + " + " + pad_label(input.pad, type);
		}
		return pad_label(input.pad, type);
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
labels(const std::vector<HostInput> &inputs, SDL_GamepadType type)
{
	std::string out;

	for (const HostInput &input : inputs)
		out += (out.empty() ? "" : ", ") + label(input, type);
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

/* A text cut to that width, three dots where it is cut */
std::string
fitted(const std::string &text, float width)
{
	if (ImGui::CalcTextSize(text.c_str()).x <= width)
		return text;

	const float dots = ImGui::CalcTextSize("...").x;
	size_t n = text.size();

	while (n > 0 && ImGui::CalcTextSize(text.c_str(), text.c_str() + n).x + dots > width)
		n--;
	/* not in the middle of a character of UTF-8 */
	while (n > 0 && (static_cast<unsigned char>(text[n]) & 0xC0) == 0x80)
		n--;
	return text.substr(0, n) + "...";
}

/* A text of inputs as it is drawn: on a PlayStation gamepad the names of
 * its four buttons as their marks (cross, circle, square, triangle). Its
 * width; drawn when `list` is given. */
float
inputs_text(ImDrawList *list, ImVec2 at, const std::string &text, bool marks, ImU32 colour)
{
	static constexpr const char *FACES[] = { "Cross", "Circle", "Square", "Triangle" };
	const float line = ImGui::GetTextLineHeight(), mark = line * 0.9f;
	float x = at.x;
	size_t from = 0;

	while (from < text.size()) {
		size_t next = std::string::npos;
		int face = -1;

		for (int f = 0; marks && f < 4; f++) {
			const size_t found = text.find(FACES[f], from);

			if (found < next)
				next = found, face = f;
		}

		const size_t end = std::min(next, text.size());

		if (end > from) {
			if (list)
				list->AddText({ x, at.y }, colour, text.c_str() + from, text.c_str() + end);
			x += ImGui::CalcTextSize(text.c_str() + from, text.c_str() + end).x;
		}
		if (face < 0)
			break;
		if (list) {
			const float m = mark * 0.22f, w = std::max(1.0f, line / 10.0f);
			const ImVec2 a = { x + m, at.y + (line - mark) / 2.0f + m }, b = { x + mark - m, at.y + (line + mark) / 2.0f - m };
			const ImVec2 c = { (a.x + b.x) / 2.0f, (a.y + b.y) / 2.0f };

			if (face == 0) {
				list->AddLine(a, b, colour, w);
				list->AddLine({ a.x, b.y }, { b.x, a.y }, colour, w);
			} else if (face == 1) {
				list->AddCircle(c, (b.x - a.x) / 2.0f, colour, 0, w);
			} else if (face == 2) {
				list->AddRect(a, b, colour, 0.0f, ImDrawFlags_None, w);
			} else {
				list->AddTriangle({ c.x, a.y }, { b.x, b.y }, { a.x, b.y }, colour, w);
			}
		}
		x += mark;
		from = next + std::strlen(FACES[face]);
	}
	return x - at.x;
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
			add(control.name + " UP", { i, 2, control.role }, bindings.part(c, 2), lit, true);
			add(control.name + " DOWN", { i, 1, control.role }, bindings.part(c, 1), lit, true);
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
 * ("STICK H · V"); three or more that differ only by their last word are
 * one control, its directions say nothing more ("LEFT-LEVER") */
std::string
label_text(std::span<const std::string> names)
{
	const auto stem = [](const std::string &name) {
		const size_t space = name.rfind(' ');

		return space == std::string::npos ? std::string() : name.substr(0, space);
	};
	const std::string first = stem(names[0]);

	if (names.size() >= 3 && !first.empty() &&
	    std::all_of(names.begin(), names.end(), [&](const std::string &n) { return stem(n) == first; }))
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

/* The games, the one shown marked and the one that runs dotted; when they
 * do not fit, the one shown between arrows. Its bottom. */
float
games_row(const MenuKit &kit, MenuState &state, ImVec2 at, float width)
{
	ImDrawList *const list = ImGui::GetWindowDrawList();
	const MenuPalette &palette = kit.palette();
	const float line = ImGui::GetTextLineHeight(), spacing = kit.px(14.0f), dot = kit.px(2.5f);
	std::vector<std::string> names;
	float total = 0.0f;

	for (const auto &[name, title] : state.games) {
		names.push_back(capitals(title));
		total += ImGui::CalcTextSize(names.back().c_str()).x + spacing + 3.0f * dot;
	}
	if (names.empty())
		return at.y;

	const auto title = [&](size_t g, ImVec2 p, bool clickable) {
		const ImVec2 size = ImGui::CalcTextSize(names[g].c_str());
		const bool shown = g == state.shown;
		bool hovered = false;

		if (clickable) {
			ImGui::SetCursorScreenPos(p);
			ImGui::PushID(static_cast<int>(g));
			if (ImGui::InvisibleButton("game", { size.x + 3.0f * dot, line + kit.px(4.0f) }) && !shown)
				state.show = g;
			hovered = ImGui::IsItemHovered();
			ImGui::PopID();
		}

		const ImU32 colour = shown ? palette.action : hovered ? palette.ink : palette.dim;

		list->AddText(p, colour, names[g].c_str());
		if (shown)
			list->AddLine({ p.x, p.y + line + kit.px(2.0f) }, { p.x + size.x, p.y + line + kit.px(2.0f) }, colour, 1.0f);
		if (g == state.running)
			list->AddCircleFilled({ p.x + size.x + 2.0f * dot, p.y + line / 2.0f }, dot, colour);
		return size.x + 3.0f * dot;
	};

	if (total - spacing <= width) {
		float x = at.x + (width - total + spacing) / 2.0f;

		for (size_t g = 0; g < names.size(); g++)
			x += title(g, { x, at.y }, true) + spacing;
		return at.y + line + kit.px(4.0f);
	}

	/* ‹ the game › and a dot for each */
	const size_t n = names.size(), shown = std::min(state.shown, n - 1);
	const float arrow = kit.button_size("<").x, w = ImGui::CalcTextSize(names[shown].c_str()).x + 3.0f * dot;
	const float dots = static_cast<float>(n) * kit.px(9.0f);
	float x = at.x + (width - (2.0f * arrow + w + 2.0f * spacing + spacing + dots)) / 2.0f;

	ImGui::PushID("games");
	if (kit.button("<", { x, at.y }, false))
		state.show = (shown + n - 1) % n;
	x += arrow + spacing;
	title(shown, { x, at.y + kit.px(4.0f) }, false);
	x += w + spacing;
	if (kit.button(">", { x, at.y }, false))
		state.show = (shown + 1) % n;
	ImGui::PopID();
	x += arrow + spacing;
	for (size_t g = 0; g < n; g++)
		list->AddCircleFilled({ x + kit.px(4.5f) + static_cast<float>(g) * kit.px(9.0f), at.y + kit.button_size("<").y / 2.0f },
		                      dot, g == shown ? palette.ink : palette.outline);
	return at.y + kit.button_size("<").y;
}

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
				state.player = player, state.catching.reset();
			list->AddText({ x, at.y + (h - size.y) / 2.0f },
			              shown ? palette.action : ImGui::IsItemHovered() ? palette.ink : palette.dim, name);
			x += size.x + kit.px(8.0f);
			w -= size.x + kit.px(8.0f);
		}
		const MenuKit::Icon kind = device ? MenuKit::Icon::PAD : MenuKit::Icon::KEYBOARD;

		if (kit.picker("device", { x, at.y }, w, devices, device, &kind)) {
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
		longest = std::max(longest, ImGui::CalcTextSize(l.text.c_str()).x);
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
	/* The lines of chips: the title first, then each name */
	std::vector<std::vector<std::pair<int, float>>> lines(1);
	float used = ImGui::CalcTextSize(title).x + gap;

	lines[0].push_back({ -1, used - gap });
	for (const size_t r : empty) {
		const float w = ImGui::CalcTextSize(rows[r].name.c_str()).x + 2.0f * wide;

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
				list->AddText({ x, y + high }, palette.faint, title);
				x += w + gap;
				continue;
			}

			const InputRow &row = rows[static_cast<size_t>(r)];
			const bool chosen = same(state.catching, row.slot);

			ImGui::SetCursorScreenPos({ x, y });
			ImGui::PushID(r);
			if (ImGui::InvisibleButton("empty", { w, h }) && row.catchable) {
				if (chosen)
					state.catching.reset();
				else
					state.catching = row.slot;
			}

			const bool hovered = ImGui::IsItemHovered();

			ImGui::PopID();
			if (chosen)
				MenuKit::frame(list, { x, y }, { x + w, y + h }, kit.px(3.0f), 0, palette.accent);
			else if (hovered)
				MenuKit::frame(list, { x, y }, { x + w, y + h }, kit.px(3.0f), 0, palette.stroke);
			else
				kit.dashed_frame(list, { x, y }, { x + w, y + h }, palette.outline);
			list->AddText({ x + wide, y + high }, chosen ? palette.accent : hovered ? palette.ink : palette.dim,
			              row.name.c_str());
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
		if (i >= rows.size() || !same(state.catching, rows[i].slot)) {
			state.mapping.reset();
			return;
		}
		if (now - state.mapping_since < MAPPING_MS)
			return;
	}
	for (i = i >= rows.size() ? 0 : i + 1; i < rows.size(); i++)
		if (rows[i].catchable && !rows[i].half)
			break;
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

	float top = a.y + kit.px(BOX_INSIDE);

	if (framed) {
		const std::string &title = state.games[state.shown].second;
		const float pad = kit.px(8.0f);

		MenuKit::frame(list, a, b, kit.px(BOX_CUT), 0, palette.line);
		list->AddText({ a.x + pad, a.y + kit.px(5.0f) }, palette.cabinet, "THE CABINET");
		list->AddText({ b.x - pad - ImGui::CalcTextSize(title.c_str()).x, a.y + kit.px(5.0f) }, palette.dim, title.c_str());
		top = a.y + kit.px(5.0f) + ImGui::GetTextLineHeight();
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
sort_rows(const MenuState &state, std::span<const InputRow> rows, const PadDrawing &drawing, SDL_GamepadType type)
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
				name += " + " + capitals(pad_label({ PadInput::Type::BUTTON, static_cast<uint8_t>(input.pad.with) }, type));
			if (part < 0) {
				out.elsewhere.push_back({ r, std::move(name) + " · " + label(input, type) });
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

		MenuKit::dashes(list, l.line, false, rest, 1.0f, kit.px(DASH_ON), kit.px(DASH_OFF));
		list->AddCircleFilled(l.line.back(), kit.px(DOT), rest);
	}

	const std::string text = fitted(l.text, room);
	const float line = ImGui::GetTextLineHeight(), w = ImGui::CalcTextSize(text.c_str()).x;
	const ImVec2 at = { l.left ? edge - kit.px(6.0f) - w : edge + kit.px(6.0f), l.y - line / 2.0f };

	ImGui::SetCursorScreenPos(at);
	ImGui::PushID(l.key);
	if (ImGui::InvisibleButton("label", { w, line }) && rows[l.row].catchable) {
		if (l.chosen)
			state.catching.reset();
		else
			state.catching = rows[l.row].slot;
	}
	ImGui::PopID();

	const ImU32 colour = l.hot || l.chosen ? palette.accent : ImGui::IsItemHovered() ? palette.dim : palette.ink;

	list->AddText(at, colour, text.c_str());
	if (l.hot)
		list->AddLine({ at.x, at.y + line + kit.px(1.0f) }, { at.x + w, at.y + line + kit.px(1.0f) }, colour, 1.0f);
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
		const std::string text = fitted(whole, w);
		const float tw = ImGui::CalcTextSize(text.c_str()).x;
		const ImVec2 at = { left ? a.x + kit.px(6.0f) : b.x - kit.px(6.0f) - tw, y - line / 2.0f };

		list->AddText(at, same(state.catching, rows[r].slot) ? kit.palette().accent : kit.palette().dim, text.c_str());
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
	const auto type = static_cast<SDL_GamepadType>(state.pad_types[me]);
	const PadDrawing &drawing = pad_drawing(type);
	Sorted sorted = sort_rows(state, rows, drawing, type);
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
	  { SDLK_0, "0", 1 }, { SDLK_MINUS, "-", 1 }, { SDLK_EQUALS, "=", 1 }, { SDLK_BACKSPACE, "Back", 2 } },
	{ { SDLK_TAB, "Tab", 1.5f }, { SDLK_Q, "Q", 1 }, { SDLK_W, "W", 1 }, { SDLK_E, "E", 1 }, { SDLK_R, "R", 1 },
	  { SDLK_T, "T", 1 }, { SDLK_Y, "Y", 1 }, { SDLK_U, "U", 1 }, { SDLK_I, "I", 1 }, { SDLK_O, "O", 1 },
	  { SDLK_P, "P", 1 }, { SDLK_LEFTBRACKET, "[", 1 }, { SDLK_RIGHTBRACKET, "]", 1 }, { SDLK_BACKSLASH, "\\", 1.5f } },
	{ { SDLK_CAPSLOCK, "Caps", 1.75f }, { SDLK_A, "A", 1 }, { SDLK_S, "S", 1 }, { SDLK_D, "D", 1 }, { SDLK_F, "F", 1 },
	  { SDLK_G, "G", 1 }, { SDLK_H, "H", 1 }, { SDLK_J, "J", 1 }, { SDLK_K, "K", 1 }, { SDLK_L, "L", 1 },
	  { SDLK_SEMICOLON, ";", 1 }, { SDLK_APOSTROPHE, "'", 1 }, { SDLK_RETURN, "Enter", 2.25f }, {} },
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

	MenuKit::frame(list, at(X0 - 8, Y0 - 10), at(X0 - 8 + 18 * U + 30, Y0 - 10 + 5 * U + 18), kit.px(4.0f), palette.body,
	               palette.stroke);

	const float size = 10.0f * k;
	const auto key = [&](const DrawnKey &dk, float x, float y, float w) {
		const bool on = std::find(bound.begin(), bound.end(), dk.key) != bound.end();
		const bool down = std::find(state.keys_held.begin(), state.keys_held.end(), dk.key) != state.keys_held.end();
		const float dy = down ? 1.5f : 0.0f;
		const ImVec2 p = at(x, y + dy), q = at(x + w, y + dy + U - GAP - 1);
		const ImVec2 text = ImGui::GetFont()->CalcTextSizeA(size, 1e9f, 0.0f, dk.name);

		list->AddRectFilled(p, q, down ? palette.accent : palette.part, 4.0f * k);
		list->AddRect(p, q, on ? palette.stroke : faint, 4.0f * k, ImDrawFlags_None, kit.px(1.0f));
		list->AddText(ImGui::GetFont(), size, { (p.x + q.x - text.x) / 2.0f, (p.y + q.y - text.y) / 2.0f },
		              down ? palette.on_accent : on ? palette.ink : palette.faint, dk.name);
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

	key({ SDLK_UP, "^", 1 }, ax + U, Y0 + 3 * U, U - GAP);
	key({ SDLK_LEFT, "<", 1 }, ax, Y0 + 4 * U, U - GAP);
	key({ SDLK_DOWN, "v", 1 }, ax + U, Y0 + 4 * U, U - GAP);
	key({ SDLK_RIGHT, ">", 1 }, ax + 2 * U, Y0 + 4 * U, U - GAP);

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

/* The controls as a list in two columns (Classic): each name and its
 * inputs on the device shown; a click catches the next one */
void
list_box(const MenuKit &kit, MenuState &state, std::span<const InputRow> rows, ImVec2 a, ImVec2 b, float corner)
{
	ImDrawList *list = ImGui::GetWindowDrawList();
	const MenuPalette &palette = kit.palette();
	const auto type = static_cast<SDL_GamepadType>(state.pad_types[state.player & 1]);
	const float line = ImGui::GetTextLineHeight(), gap = kit.px(28.0f), wide = kit.px(8.0f), high = kit.px(2.0f);
	ImVec2 lo = { a.x + kit.px(14.0f), a.y + kit.px(BOX_INSIDE) + corner };
	const ImVec2 hi = { b.x - kit.px(14.0f), b.y - kit.px(BOX_INSIDE) };
	const size_t half = (rows.size() + 1) / 2;
	/* As high as the room leaves them, at most as in the mock-up; when
	 * they do not fit, the list scrolls, its bar beside the columns */
	const float h = std::clamp((hi.y - lo.y) / static_cast<float>(std::max<size_t>(half, 1)), line + 2.0f * high + kit.px(2.0f),
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

	for (size_t r = 0; r < rows.size(); r++) {
		const InputRow &row = rows[r];
		const bool second = r >= half, chosen = same(state.catching, row.slot);
		const ImVec2 at = { lo.x + (second ? column + gap : 0.0f), top + static_cast<float>(second ? r - half : r) * h };
		const std::string whole = chosen ? "press..." : row.inputs.empty() ? "-" : labels(row.inputs, type);
		const bool marks = is_playstation(type) && state.on_pad() &&
		                   inputs_text(nullptr, {}, whole, true, 0) <= column / 2.0f - 2.0f * wide;
		const std::string inputs = marks ? whole : fitted(whole, column / 2.0f - 2.0f * wide);
		const float chip = inputs_text(nullptr, {}, inputs, marks, 0) + 2.0f * wide;
		const ImVec2 ca = { at.x + column - chip, at.y + (h - line) / 2.0f - high };
		const std::string name = fitted(row.name, column - chip - kit.px(14.0f));
		const ImVec2 cb = { at.x + column, ca.y + line + 2.0f * high };

		ImGui::SetCursorScreenPos(at);
		ImGui::PushID(static_cast<int>(r));
		if (ImGui::InvisibleButton("row", { column, h }) && row.catchable) {
			if (chosen)
				state.catching.reset();
			else
				state.catching = row.slot;
		}

		const bool hovered = ImGui::IsItemHovered();

		ImGui::PopID();
		if (hovered)
			list->AddRectFilled(at, { at.x + column, at.y + h }, palette.hover);
		list->AddLine({ at.x, at.y + h }, { at.x + column, at.y + h }, palette.line, 1.0f);
		list->AddText({ at.x + kit.px(4.0f), at.y + (h - line) / 2.0f }, row.lit || chosen ? palette.accent : palette.ink,
		              name.c_str());
		if (row.inputs.empty() && !chosen)
			kit.dashed_frame(list, ca, cb, palette.outline);
		else
			MenuKit::frame(list, ca, cb, kit.px(3.0f), 0, chosen || row.lit ? palette.accent : palette.outline);
		inputs_text(list, { ca.x + wide, ca.y + high }, inputs, marks,
		            chosen ? palette.accent : row.inputs.empty() ? palette.faint : palette.ink);
	}
	if (scrolls) {
		ImGui::SetCursorScreenPos(lo);
		ImGui::Dummy({ hi.x - lo.x, static_cast<float>(half) * h });
		ImGui::EndChild();
	}
}

} /* namespace */

void
Menu::page_input(MenuKit &kit, MenuState &state) const
{
	if (!state.panel)
		return;

	const MenuPalette &palette = kit.palette();
	const std::vector<GameControl> &controls = state.panel->controls();
	/* Every cabinet has the SERVICE of player 2: a game of two players has
	 * more of theirs */
	const bool two = std::any_of(controls.begin(), controls.end(),
	                             [](const GameControl &c) { return c.player == 1 && c.role != Role::SERVICE; });

	if (!two)
		state.player = 0;

	const ImVec2 top = ImGui::GetCursorScreenPos(), room = ImGui::GetContentRegionAvail();
	const float width = room.x, gap = kit.px(ROW_GAP);
	float y = top.y;

	y = games_row(kit, state, { top.x, y }, width) + gap;
	y = devices_row(kit, state, { top.x, y }, width, two) + gap;

	const std::vector<InputRow> rows = input_rows(state);
	const float buttons_h = kit.button_size("Default").y;

	next_mapping(state, rows);
	/* A window wide enough (16:9) has the cabinet beside the device; a
	 * narrower one shows either */
	const ImVec2 window = ImGui::GetIO().DisplaySize;
	const bool wide = window.x >= WIDE * window.y;
	const float bottom = top.y + room.y - buttons_h - gap - kit.px(4.0f);
	const float split = wide ? top.x + (width - gap) * PAD_SHARE : top.x + width;
	const ImVec2 a = { top.x, y }, b = { split, bottom };
	const bool cabinet = !wide && state.cabinet_shown;
	/* The buttons in the corner of the box: Classic for a gamepad, the
	 * swap of the device and the cabinet in a narrow window */
	const char *const swap = cabinet ? "Controller" : "Cabinet";
	const bool classic = !cabinet;
	const ImVec2 classic_size = kit.button_size("Classic"), swap_size = kit.button_size(swap);
	const float corner = classic || !wide ? std::max(classic_size.y, swap_size.y) + kit.px(5.0f) : 0.0f;

	MenuKit::frame(ImGui::GetWindowDrawList(), a, b, kit.px(BOX_CUT), 0, palette.line);
	if (cabinet)
		cabinet_box(kit, state, a, b, false);
	else if (state.classic)
		list_box(kit, state, rows, a, b, corner);
	else if (state.on_pad())
		pad_box(kit, state, rows, a, b, corner);
	else
		keyboard_box(kit, state, rows, a, b, corner);
	if (wide)
		cabinet_box(kit, state, { split + gap, y }, { top.x + width, bottom }, true);

	float x = b.x - kit.px(6.0f);

	if (!wide) {
		x -= swap_size.x;
		if (kit.button(swap, { x, a.y + kit.px(5.0f) }, false))
			state.cabinet_shown = !state.cabinet_shown;
		x -= kit.px(6.0f);
	}
	if (classic && kit.button("Classic", { x - classic_size.x, a.y + kit.px(5.0f) }, state.classic))
		state.classic = !state.classic;

	/* What changes all the inputs of the player on the device shown */
	ImGui::SetCursorScreenPos({ top.x, b.y + gap });
	if (kit.button("Default"))
		state.ask = MenuState::Ask::DEFAULTS;
	if (state.undo ? kit.button("Undo", true) : kit.button("Clear all", true))
		state.ask = state.undo ? MenuState::Ask::UNDO : MenuState::Ask::CLEAR;
	if (!state.mapping && kit.button("Start mapping", true)) {
		/* The first control: the one before it is none */
		state.mapping = rows.size();
		state.catching.reset();
	} else if (state.mapping) {
		if (kit.button("Skip", true))
			state.mapping_since = 0;
		if (kit.button("Stop", true))
			state.mapping.reset(), state.catching.reset();
	}
	ImGui::SetCursorScreenPos(top);
	ImGui::Dummy({ width, room.y - kit.px(4.0f) });
}

/* What the page says at the bottom: the control whose input is caught,
 * else the one worked, else how it works */
std::string
Menu::input_help(const MenuState &state)
{
	if (!state.panel)
		return "";

	const std::vector<InputRow> rows = input_rows(state);
	const auto type = static_cast<SDL_GamepadType>(state.pad_types[state.player & 1]);
	const std::string press = state.on_pad() ? "press the button, or move the stick, you want" : "press the key you want";

	if (state.mapping && *state.mapping < rows.size()) {
		size_t done = 0, all = 0;

		for (size_t r = 0; r < rows.size(); r++)
			if (rows[r].catchable && !rows[r].half)
				all++, done += r <= *state.mapping;

		const uint64_t gone = SDL_GetTicks() - state.mapping_since;
		const uint64_t left = gone >= MAPPING_MS ? 0 : (MAPPING_MS - gone + 999) / 1000;

		return std::to_string(done) + " / " + std::to_string(all) + "  " + rows[*state.mapping].name + ": " + press + ", " +
		       std::to_string(left) + " s. Esc stops.";
	}
	for (const InputRow &row : rows)
		if (same(state.catching, row.slot))
			return row.name + ": " + press + ". Esc lets it be.";
	for (const InputRow &row : rows)
		if (row.lit)
			return row.name + (row.inputs.empty() ? "" : ": " + labels(row.inputs, type));
	return "Touch a button or move a stick: its control lights. Click a name to change it.";
}

} /* namespace odin */
