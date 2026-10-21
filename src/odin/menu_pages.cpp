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

/* The pages Audio, Hotkeys, Outputs and Paths of the menu (mock-up v50) */

#include "odin/menu.hpp"

#include <algorithm>
#include <array>
#include <cfloat>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <utility>
#include <vector>

#include "imgui/imgui.h"
#include "odin/env.hpp"
#include "odin/menu_kit.hpp"

namespace odin {

namespace {

constexpr const char *ON_OFF[] = { "on", "off" };

/* A page in two: what it shows in a box at the left, its settings at the
 * right, as 1 to 1.15 (mock-up v50); the room between them, below the row
 * of the games, the cut of the corners of the box and the room inside */
constexpr float LEFT_SHARE = 1.0f / 2.15f;
constexpr float SPLIT_GAP = 10.0f;
constexpr float ROW_GAP = 6.0f;
constexpr float BOX_CUT = 5.0f;
constexpr float BOX_INSIDE = 8.0f;

/* An output: its width at the least, the side of its icon, the room in it
 * and between two (mock-up v50) */
constexpr float TILE_LEAST = 110.0f;
constexpr float TILE_ICON = 26.0f;
constexpr float TILE_PAD = 8.0f;
constexpr float TILE_GAP = 8.0f;

/* The box at the left, from the top of the page to its bottom, framed:
 * its corners. The settings then go into begin_settings, at its right. */
struct Box {
	ImVec2 a, b;
};

Box
left_box(const MenuKit &kit, ImVec2 top, ImVec2 room)
{
	const Box box = { top, { top.x + (room.x - kit.px(SPLIT_GAP)) * LEFT_SHARE, top.y + room.y } };

	kit.frame(ImGui::GetWindowDrawList(), box.a, box.b, kit.px(BOX_CUT), 0, kit.palette().line);
	return box;
}

void
begin_settings(const MenuKit &kit, const Box &box, ImVec2 top, ImVec2 room)
{
	const float x = box.b.x + kit.px(SPLIT_GAP);

	ImGui::SetCursorScreenPos({ x, top.y });
	ImGui::BeginChild("settings", { top.x + room.x - x, room.y }, ImGuiChildFlags_None,
	                  ImGuiWindowFlags_NoBackground);
}

/* The page under them is as high as the room: it does not scroll */
void
end_settings(const MenuKit &kit, ImVec2 top, ImVec2 room)
{
	ImGui::Dummy({ 0.0f, kit.px(4.0f) });
	ImGui::EndChild();
	ImGui::SetCursorScreenPos(top);
	ImGui::Dummy({ room.x, room.y - kit.px(4.0f) });
}

/* A text on as many lines as that width takes, each in its middle, from
 * `top`: its height. Drawn when `colour` is not 0. */
float
centred(ImVec2 top, float width, float size, const char *text, ImU32 colour)
{
	ImDrawList *const list = ImGui::GetWindowDrawList();
	ImFont *const font = ImGui::GetFont();
	const char *const end = text + std::strlen(text);
	float y = top.y;

	for (const char *line = text; line < end;) {
		const char *cut = font->CalcWordWrapPosition(size, line, end, width);

		if (cut == line)
			cut++;

		const float w = font->CalcTextSizeA(size, FLT_MAX, 0.0f, line, cut).x;

		if (colour)
			list->AddText(font, size, { top.x + (width - w) / 2.0f, y }, colour, line, cut);
		y += size;
		line = cut;
		while (line < end && *line == ' ')
			line++;
	}
	return y - top.y;
}

/* What a box shows, `high` high: in its middle, or from its top in a
 * child that scrolls where it is higher than the box (mock-up v50).
 * `draw` takes the top, and draws in the list of the window it is in. */
template <typename Draw>
void
in_box(const MenuKit &kit, const Box &box, float high, Draw draw)
{
	const float inside = kit.px(BOX_INSIDE);
	const ImVec2 a = { box.a.x + 1.0f, box.a.y + 1.0f }, size = { box.b.x - box.a.x - 2.0f, box.b.y - box.a.y - 2.0f };

	if (high <= size.y - 2.0f * inside) {
		ImGui::GetWindowDrawList()->PushClipRect(box.a, box.b, true);
		draw((box.a.y + box.b.y - high) / 2.0f);
		ImGui::GetWindowDrawList()->PopClipRect();
		return;
	}
	ImGui::SetCursorScreenPos(a);
	ImGui::BeginChild("drawing", size, ImGuiChildFlags_None, ImGuiWindowFlags_NoBackground);

	const float top = ImGui::GetCursorScreenPos().y + inside;

	draw(top);
	ImGui::SetCursorScreenPos({ a.x, top });
	ImGui::Dummy({ 1.0f, high + inside });
	ImGui::EndChild();
}

MenuKit::Icon
icon_of(GameOutput::Role role)
{
	switch (role) {
	case GameOutput::Role::LOCK:
		return MenuKit::Icon::LOCK;
	case GameOutput::Role::RUMBLE:
		return MenuKit::Icon::RUMBLE;
	case GameOutput::Role::MUTE:
		return MenuKit::Icon::MUTE;
	default:
		return MenuKit::Icon::LAMP;
	}
}

/* The outputs of the cabinet as tiles, as many in a row as fit, an output
 * on lit with the accent; what they are under them; all in the middle of
 * the box */
void
output_tiles(const MenuKit &kit, const MenuState &state, const Box &box)
{
	const MenuPalette &palette = kit.palette();
	const float inside = kit.px(BOX_INSIDE), gap = kit.px(TILE_GAP), pad = kit.px(TILE_PAD), side = kit.px(TILE_ICON);
	const float width = box.b.x - box.a.x - 2.0f * inside, size = MenuKit::font(MenuKit::TINY);
	const float small = MenuKit::font(MenuKit::SMALL);
	const size_t columns = std::max<size_t>(1, static_cast<size_t>((width + gap) / (kit.px(TILE_LEAST) + gap)));
	const size_t rows = (state.outputs.size() + columns - 1) / columns;
	const float tile_w = (width - static_cast<float>(columns - 1) * gap) / static_cast<float>(columns);
	const char caption[] = "The cabinet's outputs: they light when the game turns them on.";
	float names = 0.0f;

	for (const MenuState::Output &output : state.outputs)
		names = std::max(names, centred({}, tile_w - 2.0f * pad, size, output.name.c_str(), 0));

	const float tile_h = 2.0f * pad + side + kit.px(4.0f) + names;
	const float tiles_h = static_cast<float>(rows) * tile_h + static_cast<float>(rows ? rows - 1 : 0) * gap;
	const float caption_h = centred({}, width, small, caption, 0);
	const float all_h = tiles_h + (rows ? gap : 0.0f) + caption_h;

	in_box(kit, box, all_h, [&](float top) {
		ImDrawList *const list = ImGui::GetWindowDrawList();

		for (size_t i = 0; i < state.outputs.size(); i++) {
			const MenuState::Output &output = state.outputs[i];
			const ImVec2 a = { box.a.x + inside + static_cast<float>(i % columns) * (tile_w + gap),
			                   top + static_cast<float>(i / columns) * (tile_h + gap) };
			kit.frame(list, a, { a.x + tile_w, a.y + tile_h }, kit.px(BOX_CUT), output.on ? palette.hover : 0,
			               output.on ? palette.accent : palette.line);
			MenuKit::icon(list, icon_of(output.role), { a.x + (tile_w - side) / 2.0f, a.y + pad }, side,
			              output.on ? palette.accent : palette.stroke);
			centred({ a.x + pad, a.y + pad + side + kit.px(4.0f) }, tile_w - 2.0f * pad, size, output.name.c_str(),
			        output.on ? palette.accent : palette.dim);
		}
		centred({ box.a.x + inside, top + tiles_h + (rows ? gap : 0.0f) }, width, small, caption, palette.faint);
	});
}


} /* namespace */

/* A path as the user knows it: the home folder as ~; on Windows with its
 * separators, as its own dialogs give them */
std::string
shown_path(const std::string &path, const char *none)
{
	if (path.empty())
		return none;
#ifdef _WIN32
	std::string out = path;

	std::replace(out.begin(), out.end(), '/', '\\');
	return out;
#else
	const std::string home = home_dir();

	if (!home.empty() && path.starts_with(home) && (path.size() == home.size() || path[home.size()] == '/'))
		return "~" + path.substr(home.size());
	return path;
#endif
}

/* The two sound boards of a cabinet, as its wiring takes them (the manuals
 * and wiring diagrams of SEGA, note 69): its game, where each board plays,
 * what sets the level of the optional one on the cabinet. The optional
 * board of Racer feeds the shaker of its panel, no speaker. */
struct Boards {
	const char *game, *main, *option, *knob;
};

constexpr Boards BOARDS[] = {
	{ "sgnascar", "front speakers (FRONT)", "seat-back speakers (REAR)", "; as the REAR knob of the cabinet" },
	{ "pharrier", "monitor speakers and woofer", "seat-back speakers (REAR)", "" },
	{ "swracer", "seat speakers and woofer", "shaker of the panel (VIBRATION)",
	  "; the cabinet feeds it to the shaker only" },
};
constexpr Boards OTHER_BOARDS = { "", "main speakers", "optional speakers", "" };

const Boards &
boards_of(const std::string &game)
{
	for (const Boards &boards : BOARDS)
		if (game == boards.game)
			return boards;
	return OTHER_BOARDS;
}

constexpr const char *PLAYED[] = { "Main", "Both", "Four" };

/* The drawing of a card, in a space of 330 by 110 (mock-up v50) */
constexpr float ROUTE_W = 330.0f;
constexpr float ROUTE_H = 110.0f;
constexpr float CARD_PAD = 8.0f;

/* A level as the cabinet says it: "-12 dB", "0 dB", "+3 dB" */
std::string
decibels(float db)
{
	char text[16];

	std::snprintf(text, sizeof(text), "%s%.0f dB", db > 0.5f ? "+" : "", static_cast<double>(db));
	return text;
}

/* The largest size up to `size` a text fits `width` at: measured there,
 * as the font has its own widths at each size */
float
fit(const char *text, float size, float width)
{
	ImFont *const font = ImGui::GetFont();
	const float wide = font->CalcTextSizeA(size, FLT_MAX, 0.0f, text).x;
	float fitted = wide > width ? size * width / std::max(wide, 1.0f) : size;

	while (fitted > 4.0f && font->CalcTextSizeA(fitted, FLT_MAX, 0.0f, text).x > width)
		fitted -= 0.5f;
	return fitted;
}

/* A box of the drawing: its name and where it plays (`sub`, when the card
 * has room for it), in its middle; framed when it plays, dashed when not.
 * A small box takes the short name. */
void
route_box(const MenuKit &kit, ImVec2 a, ImVec2 b, const char *name, const char *short_name, const char *sub, bool on)
{
	ImDrawList *const list = ImGui::GetWindowDrawList();
	ImFont *const font = ImGui::GetFont();
	const MenuPalette &palette = kit.palette();
	const float f = ImGui::GetFontSize(), inner = b.x - a.x - kit.px(6.0f);
	const auto width = [&](float size, const char *text) { return font->CalcTextSizeA(size, FLT_MAX, 0.0f, text).x; };
	const char *const shown = width(f * 0.8f, name) <= inner ? name : short_name;
	const float name_size = fit(shown, f * 0.8f, inner);
	const float second = sub ? fit(sub, f * 0.7f, inner) : 0.0f;
	const bool with_sub = sub && name_size + second + kit.px(4.0f) <= b.y - a.y;
	const float high = name_size + (with_sub ? second + kit.px(2.0f) : 0.0f);
	const float top = (a.y + b.y - high) / 2.0f, mid = (a.x + b.x) / 2.0f;

	if (on)
		kit.frame(list, a, b, kit.px(3.0f), palette.part, palette.stroke);
	else {
		kit.frame(list, a, b, kit.px(3.0f), palette.part, 0);
		kit.dashed_frame(list, a, b, palette.outline);
	}
	list->PushClipRect(a, b, true);
	list->AddText(font, name_size, { mid - width(name_size, shown) / 2.0f, top }, on ? palette.ink : palette.dim,
	              shown);
	if (with_sub)
		list->AddText(font, second, { mid - width(second, sub) / 2.0f, top + name_size + kit.px(2.0f) },
		              on ? palette.dim : palette.faint, sub);
	list->PopClipRect();
}

/* A line of the drawing from the box of a board to the speakers, a curve
 * of two bends; dashed when that board is not played; the level of the
 * optional board in its middle */
void
route_line(const MenuKit &kit, ImVec2 p, ImVec2 q, bool on, const std::string &level)
{
	ImDrawList *const list = ImGui::GetWindowDrawList();
	const MenuPalette &palette = kit.palette();
	const float mid = (p.x + q.x) / 2.0f;
	std::vector<ImVec2> points;

	for (int i = 0; i <= 24; i++) {
		const float t = static_cast<float>(i) / 24.0f, u = 1.0f - t;
		const float x = u * u * u * p.x + 3.0f * u * u * t * mid + 3.0f * u * t * t * mid + t * t * t * q.x;
		const float y = u * u * u * p.y + 3.0f * u * u * t * p.y + 3.0f * u * t * t * q.y + t * t * t * q.y;

		points.push_back({ x, y });
	}
	if (on)
		list->AddPolyline(points.data(), static_cast<int>(points.size()), palette.stroke, ImDrawFlags_None, kit.px(1.4f));
	else
		MenuKit::dashes(list, points, false, palette.outline, kit.px(1.4f), kit.px(2.0f), kit.px(4.0f));
	if (level.empty())
		return;

	const float size = ImGui::GetFontSize() * 0.75f;
	const ImVec2 text = ImGui::GetFont()->CalcTextSizeA(size, FLT_MAX, 0.0f, level.c_str());
	const ImVec2 at = { mid - text.x / 2.0f, (p.y + q.y) / 2.0f - text.y - kit.px(3.0f) };

	list->AddRectFilled({ at.x - kit.px(2.0f), at.y }, { at.x + text.x + kit.px(2.0f), at.y + text.y }, palette.screen);
	list->AddText(ImGui::GetFont(), size, at, palette.dim, level.c_str());
}

/* How a game plays its two boards: each board in its box, joined to the
 * speakers of the user (stereo), or to the front and the rear in four
 * channels; a board not played dashed, the level of the optional board on
 * its line. In a card with the game's name; its height, drawn when
 * `draw`. */
float
route_card(const MenuKit &kit, const MenuState::SoundGame &game, ImVec2 at, float width, bool draw)
{
	ImDrawList *const list = ImGui::GetWindowDrawList();
	const MenuPalette &palette = kit.palette();
	const float pad = kit.px(CARD_PAD), line = ImGui::GetTextLineHeight();
	const float k = std::min(kit.px(1.0f), (width - 2.0f * pad) / ROUTE_W);
	const float h = 2.0f * pad + line + kit.px(4.0f) + ROUTE_H * k;

	if (!draw)
		return h;

	const Boards &boards = boards_of(game.name);
	const ImVec2 o = { at.x + (width - ROUTE_W * k) / 2.0f, at.y + pad + line + kit.px(4.0f) };
	const auto p = [&](float x, float y) { return ImVec2{ o.x + x * k, o.y + y * k }; };
	const bool option = game.played != 0, four = game.played == 2;
	std::string title = game.title;

	for (char &c : title)
		if (c >= 'a' && c <= 'z')
			c = static_cast<char>(c - 'a' + 'A');
	const float small = MenuKit::font(MenuKit::SMALL);

	kit.frame(list, at, { at.x + width, at.y + h }, kit.px(BOX_CUT), 0, palette.line);
	MenuKit::write(list, { at.x + pad, at.y + pad + line - small }, palette.dim, title.c_str(), small, small * 0.12f);
	route_box(kit, p(4, 6), p(144, 48), "Main board", "Main", boards.main, true);
	route_box(kit, p(4, 62), p(144, 104), "Optional board", "Optional", boards.option, option);
	if (four) {
		route_box(kit, p(222, 6), p(326, 48), "Front", "Front", "channels 1-2", true);
		route_box(kit, p(222, 62), p(326, 104), "Rear", "Rear", "channels 3-4", true);
		route_line(kit, p(144, 27), p(222, 27), true, "");
		route_line(kit, p(144, 83), p(222, 83), true, "");
	} else {
		route_box(kit, p(222, 34), p(326, 76), "Your speakers", "Speakers", "stereo", true);
		route_line(kit, p(144, 27), p(222, 50), true, "");
		route_line(kit, p(144, 83), p(222, 60), option, game.played == 1 ? decibels(game.level) : "");
	}
	return h;
}

/* The cards of the games in the middle of the box, what the others play
 * under them */
void
route_cards(const MenuKit &kit, const MenuState &state, const Box &box)
{
	const float inside = kit.px(BOX_INSIDE), gap = kit.px(TILE_GAP), size = MenuKit::font(MenuKit::SMALL);
	const float width = std::min(box.b.x - box.a.x - 2.0f * inside, kit.px(ROUTE_W + 2.0f * CARD_PAD));
	const float x = (box.a.x + box.b.x - width) / 2.0f;
	std::string others;
	std::vector<std::string> titles;

	for (const auto &[name, title] : state.games) {
		bool two = false;

		for (const MenuState::SoundGame &game : state.sound_games)
			two = two || game.name == name;
		if (!two)
			titles.push_back(title);
	}
	for (size_t i = 0; i < titles.size(); i++)
		others += (i == 0 ? "" : i + 1 == titles.size() ? " and " : ", ") + titles[i];
	others += titles.size() == 1 ? " plays its main board alone, in stereo." :
	                               " play their main board alone, in stereo.";

	float all_h = titles.empty() ? -gap : centred({}, width, size, others.c_str(), 0);

	for (const MenuState::SoundGame &game : state.sound_games)
		all_h += route_card(kit, game, {}, width, false) + gap;

	in_box(kit, box, all_h, [&](float y) {
		for (const MenuState::SoundGame &game : state.sound_games)
			y += route_card(kit, game, { x, y }, width, true) + gap;
		if (!titles.empty())
			centred({ x, y }, width, size, others.c_str(), kit.palette().faint);
	});
}

/* Audio: where the sound goes, how loud; how the cabinets with two sound
 * boards play them */
void
Menu::page_audio(MenuKit &kit, MenuState &state) const
{
	const ImVec2 top = ImGui::GetCursorScreenPos(), room = ImGui::GetContentRegionAvail();
	const Box box = left_box(kit, top, room);

	route_cards(kit, state, box);
	begin_settings(kit, box, top, room);

	kit.begin_group("AUDIO OUTPUT");
	std::vector<std::string> devices = { "System default" };
	int device = 0;

	for (const std::string &name : state.devices) {
		if (name == state.device)
			device = static_cast<int>(devices.size());
		devices.push_back(name);
	}
	if (device == 0 && !state.device.empty()) {
		device = static_cast<int>(devices.size());
		devices.push_back(state.device);
	}
	if (kit.value_list("Device", "the sound of Odin plays there", devices[static_cast<size_t>(device)].c_str(),
	                   "Change...", devices, device, state.silent))
		state.device = device == 0 ? "" : devices[static_cast<size_t>(device)];

	float volume = static_cast<float>(state.volume);
	char shown[16];

	std::snprintf(shown, sizeof(shown), "%u %%", state.volume);
	if (kit.slider("Volume", "of the sound of Odin alone; 0 is none", volume, 0.0f, 100.0f, 1.0f, shown, state.silent))
		state.volume = static_cast<unsigned>(std::lround(volume));

	static constexpr const char *KEEP_MUTE[] = { "Keep", "Mute" };
	int background = state.background ? 0 : 1;

	if (kit.choice("Sound in the background", "when Odin is not the active window", KEEP_MUTE, background,
	               state.silent))
		state.background = background == 0;
	kit.end_group();

	kit.paragraph("SOUND BOARDS", "Both: the main and the optional board mixed on two speakers. Four: the two "
	              "boards apart, on a four-channel output. Main: without the optional board. By default as the "
	              "cabinet: the optional board of Racer shakes its panel and is heard nowhere; the shaking is set "
	              "in Input.");
	for (MenuState::SoundGame &game : state.sound_games) {
		std::string title = game.title;
		char about[160];

		for (char &c : title)
			if (c >= 'a' && c <= 'z')
				c = static_cast<char>(c - 'a' + 'A');
		ImGui::PushID(game.name.c_str());
		kit.begin_group(title.c_str());
		kit.choice("Played", "", PLAYED, game.played, game.locked ? "set by the command line: -o" : nullptr);

		const Boards &boards = boards_of(game.name);
		const std::string level = decibels(game.level);

		std::snprintf(about, sizeof(about), "%s; with Both, -60 to +12 dB%s", boards.option, boards.knob);
		kit.slider("Optional board level", about, game.level, -60.0f, 12.0f, 1.0f, level.c_str(), nullptr,
		           game.played != 1);
		kit.end_group();
		ImGui::PopID();
	}

	end_settings(kit, top, room);
}

/* The actions of Odin as the page Hotkeys says them, in the order of
 * Hotkeys::Action */
constexpr const char *HOTKEY_NAMES[Hotkeys::ACTIONS] = {
	"Open / close the menu (the game pauses)", "Pause", "Save the state", "Load the state", "Full screen", "Quit",
};

/* The modifiers as their caps say them */
constexpr std::pair<SDL_Keymod, const char *> MODIFIER_CAPS[] = {
	{ SDL_KMOD_CTRL, "Ctrl" }, { SDL_KMOD_ALT, "Alt" }, { SDL_KMOD_SHIFT, "Shift" }, { SDL_KMOD_GUI, "Gui" },
};

/* The row of the function keys of a keyboard, a gap (0) between groups */
constexpr SDL_Keycode FUNCTION_ROW[] = {
	SDLK_ESCAPE, 0, SDLK_F1, SDLK_F2, SDLK_F3, SDLK_F4, 0, SDLK_F5, SDLK_F6, SDLK_F7, SDLK_F8, 0,
	SDLK_F9, SDLK_F10, SDLK_F11, SDLK_F12,
};

/* A key as its cap says it */
std::string
cap_name(SDL_Keycode key)
{
	switch (key) {
	case SDLK_ESCAPE:
		return "Esc";
	case SDLK_RETURN:
		return "Enter";
	default:
		return SDL_GetKeyName(key);
	}
}

/* The caps of a combination: its modifiers, then its key (Enter after
 * them as its sign, mock-up v50) */
std::vector<std::string>
caps(const HotKey &key)
{
	std::vector<std::string> out;

	for (const auto &[mod, name] : MODIFIER_CAPS)
		if (key.mods & mod)
			out.push_back(name);
	out.push_back(!out.empty() && key.key == SDLK_RETURN ? "⏎" : cap_name(key.key));
	return out;
}

/* Whether a key, or a modifier of either side, is held */
bool
held(const MenuState &state, SDL_Keycode key)
{
	const auto is = [&](SDL_Keycode k) {
		return std::find(state.keys_held.begin(), state.keys_held.end(), k) != state.keys_held.end();
	};

	switch (key) {
	case SDLK_LCTRL:
		return is(SDLK_LCTRL) || is(SDLK_RCTRL);
	case SDLK_LALT:
		return is(SDLK_LALT) || is(SDLK_RALT);
	case SDLK_LSHIFT:
		return is(SDLK_LSHIFT) || is(SDLK_RSHIFT);
	case SDLK_LGUI:
		return is(SDLK_LGUI) || is(SDLK_RGUI);
	default:
		return is(key);
	}
}

/* The keyboard drawn: the function row, Odin's keys clear and lit while
 * held (in a narrow box its keys alone), then its other keys and the
 * modifiers they take; how it reads; Guide on a gamepad, as its maker
 * names it. In the middle of the box. */
void
hotkeys_keyboard(const MenuKit &kit, const MenuState &state, const Box &box, const std::string &guide_name)
{
	const MenuPalette &palette = kit.palette();
	const float inside = kit.px(BOX_INSIDE), gap = kit.px(3.0f), group = kit.px(6.0f), size = MenuKit::font(MenuKit::SMALL);
	const float width = box.b.x - box.a.x - 2.0f * inside, cap_h = kit.key_size("x").y;
	std::vector<std::pair<SDL_Keycode, std::string>> others;
	const auto used = [&](SDL_Keycode key) {
		for (const Hotkeys::Binding &binding : state.hotkeys)
			for (const HotKey &k : binding.keys)
				if (k.key == key && key != SDLK_UNKNOWN)
					return true;
		return false;
	};
	const auto add = [&](SDL_Keycode key, const std::string &name) {
		for (const auto &[k, n] : others)
			if (k == key)
				return;
		others.emplace_back(key, name);
	};

	for (const Hotkeys::Binding &binding : state.hotkeys) {
		for (const HotKey &k : binding.keys) {
			if (k.key == SDLK_UNKNOWN)
				continue;
			static constexpr SDL_Keycode LEFT[] = { SDLK_LCTRL, SDLK_LALT, SDLK_LSHIFT, SDLK_LGUI };

			for (size_t m = 0; m < std::size(MODIFIER_CAPS); m++)
				if (k.mods & MODIFIER_CAPS[m].first)
					add(LEFT[m], MODIFIER_CAPS[m].second);
			if (std::find(std::begin(FUNCTION_ROW), std::end(FUNCTION_ROW), k.key) == std::end(FUNCTION_ROW))
				add(k.key, cap_name(k.key));
		}
	}

	/* The function row, or Odin's keys of it alone where it does not fit */
	const auto row_width = [&](bool all) {
		float w = 0.0f;
		bool first = true;

		for (const SDL_Keycode key : FUNCTION_ROW) {
			if (!key) {
				w += all ? group : 0.0f;
				continue;
			}
			if (!all && !used(key))
				continue;
			w += (first ? 0.0f : gap) + kit.key_size(cap_name(key).c_str()).x;
			first = false;
		}
		return w;
	};
	const bool all = row_width(true) <= width;
	float others_w = -gap;

	for (const auto &[key, name] : others)
		others_w += kit.key_size(name.c_str()).x + gap;

	const char caption[] = "Light outline: Odin's keys. Touch them: they light.";
	const char guide[] = "held = the controller's hotkey, as on Batocera";
	const float text_w = std::min(width, kit.px(260.0f));
	const float caption_h = centred({}, width, size, caption, 0);
	const float guide_w = kit.pill_size(guide_name.c_str()).x, guide_text_w = text_w - guide_w - kit.px(8.0f);
	const float guide_h = std::max(cap_h, ImGui::GetFont()->CalcTextSizeA(size, FLT_MAX, guide_text_w, guide).y);
	const float all_h = cap_h + gap + (others.empty() ? 0.0f : cap_h + gap) + kit.px(6.0f) + caption_h +
	                    kit.px(10.0f) + guide_h;
	in_box(kit, box, all_h, [&](float y) {
		ImDrawList *const list = ImGui::GetWindowDrawList();
		float x = (box.a.x + box.b.x - row_width(all)) / 2.0f;
		bool first = true;

		for (const SDL_Keycode key : FUNCTION_ROW) {
			if (!key) {
				x += all ? group : 0.0f;
				continue;
			}
			if (!all && !used(key))
				continue;

			const std::string name = cap_name(key);

			x += first ? 0.0f : gap;
			kit.key(list, { x, y }, name.c_str(), used(key), held(state, key));
			x += kit.key_size(name.c_str()).x;
			first = false;
		}
		y += cap_h + gap;
		if (!others.empty()) {
			x = (box.a.x + box.b.x - others_w) / 2.0f;
			for (const auto &[key, name] : others) {
				kit.key(list, { x, y }, name.c_str(), true, held(state, key));
				x += kit.key_size(name.c_str()).x + gap;
			}
			y += cap_h + gap;
		}
		y += kit.px(6.0f);
		y += centred({ box.a.x + inside, y }, width, size, caption, palette.faint) + kit.px(10.0f);
		x = (box.a.x + box.b.x - text_w) / 2.0f;
		kit.pill(list, { x, y }, guide_name.c_str());
		list->AddText(ImGui::GetFont(), size, { x + guide_w + kit.px(8.0f), y + (cap_h - size) / 2.0f }, palette.dim, guide,
		              nullptr, guide_text_w);
	});
}

/* Hotkeys: what Odin does itself on a key or with Guide held, each changed
 * by a click and the next key or button */
void
Menu::page_hotkeys(MenuKit &kit, MenuState &state) const
{
	const ImVec2 top = ImGui::GetCursorScreenPos(), room = ImGui::GetContentRegionAvail();
	const Box box = left_box(kit, top, room);
	const int type = state.pad_types[0];

	hotkeys_keyboard(kit, state, box, button_name(SDL_GAMEPAD_BUTTON_GUIDE, type));
	begin_settings(kit, box, top, room);

	kit.begin_group("KEYS");
	for (size_t a = 0; a < Hotkeys::ACTIONS; a++) {
		const Hotkeys::Binding &binding = state.hotkeys[a];
		std::vector<std::vector<std::string>> keys;
		std::vector<std::string> pad;
		std::vector<size_t> slots;
		const auto caught = [&](size_t slot) {
			return state.hotkey_catching && state.hotkey_catching->first == a && state.hotkey_catching->second == slot;
		};
		int catching = -1;

		for (size_t slot = 0; slot < Hotkeys::KEYS; slot++) {
			if (binding.keys[slot].key == SDLK_UNKNOWN)
				continue;
			if (caught(slot))
				catching = static_cast<int>(keys.size());
			keys.push_back(caps(binding.keys[slot]));
			slots.push_back(slot);
		}
		/* An action without a key takes one in its first place */
		if (keys.empty()) {
			if (caught(0))
				catching = 0;
			keys.push_back({ "-" });
			slots.push_back(0);
		}
		if (caught(Hotkeys::KEYS))
			catching = 2;
		if (binding.pad != Hotkeys::NO_PAD)
			pad.push_back(button_name(SDL_GAMEPAD_BUTTON_GUIDE, type));
		if (binding.pad != Hotkeys::NO_PAD && binding.pad != Hotkeys::GUIDE_ALONE)
			pad.push_back(button_name(binding.pad, type));

		const int clicked = kit.shortcut(HOTKEY_NAMES[a], keys, pad, catching);

		if (clicked == 2)
			state.hotkey_catching = { a, Hotkeys::KEYS };
		else if (clicked >= 0)
			state.hotkey_catching = { a, slots[static_cast<size_t>(clicked)] };
	}
	kit.end_group();
	ImGui::Dummy({ 0.0f, kit.px(6.0f) });
	if (kit.button("Defaults", false, MenuKit::Icon::DEFAULTS)) {
		state.hotkeys_default = true;
		state.hotkey_catching.reset();
	}

	end_settings(kit, top, room);
}

/* What the page says at the bottom: what the key or button caught does and
 * how to give it, else how it works */
std::string
Menu::hotkeys_help(const MenuState &state)
{
	if (!state.hotkey_catching)
		return "Everything can be reassigned: click a key or a combination to change it.";

	const std::string name = HOTKEY_NAMES[state.hotkey_catching->first];

	if (state.hotkey_catching->second == Hotkeys::KEYS)
		return name + ": hold Guide and press a button, or tap Guide alone. Esc lets it be.";
	return name + ": press the key you want, with Ctrl, Alt or Shift held if you like. Esc lets it be.";
}

/* Outputs: those of the cabinet of the game shown, lit as the game lights
 * them; sent to the programs of lamps, for every game */
void
Menu::page_outputs(MenuKit &kit, MenuState &state) const
{
	ImVec2 top = ImGui::GetCursorScreenPos(), room = ImGui::GetContentRegionAvail();
	const float row = games_row(kit, state, top, room.x) + kit.px(ROW_GAP);

	room.y -= row - top.y;
	top.y = row;

	const Box box = left_box(kit, top, room);

	output_tiles(kit, state, box);
	begin_settings(kit, box, top, room);

	kit.begin_group("LAMP PROGRAMS");
	int send = state.network ? 0 : 1;

	if (kit.choice("Send to lamp programs", "LEDBlinky, MAMEHooker, DOFLinx · this machine, port 8000", ON_OFF, send))
		state.network = send == 0;
	if (state.network)
		kit.text("Programs connected", state.network_failed ? "another program has port 8000: nothing is sent" : "",
		         state.network_failed ? "-" : std::to_string(state.programs).c_str());
	kit.end_group();

	end_settings(kit, top, room);
}

/* Paths: where the games are, where Odin writes, its own files */
void
Menu::page_paths(MenuKit &kit, MenuState &state) const
{
	const char *const change = state.choosing ? nullptr : "Change...";

	kit.begin_group("GAMES");
	if (kit.value("Games (ROMs)", "a folder for each game · at the next launch",
	              shown_path(state.roms, "not set").c_str(), change, nullptr,
	              state.roms_given ? "set by the command line: -R" : nullptr))
		state.choose = MenuState::Folder::ROMS;
	if (kit.value("BIOS", "hikaru, mie, jvs13551 as MAME keeps them · at the next launch",
	              shown_path(state.bios, "the folder of the games").c_str(), change, nullptr,
	              state.bios_given ? "set by the command line: -B" : nullptr))
		state.choose = MenuState::Folder::BIOS;
	kit.end_group();

	kit.begin_group("SAVES");
	if (kit.value("Game saves", "EEPROM, cards, operator settings; one folder per game · at the next launch",
	              shown_path(state.saves, "none").c_str(), change, nullptr,
	              state.saves_given ? "set by ODIN_SAVES" : nullptr))
		state.choose = MenuState::Folder::SAVES;
	if (kit.value("Save states", "F4 saves the game where it is, F1 takes it back",
	              shown_path(state.states, "the folder Odin was started from").c_str(), change))
		state.choose = MenuState::Folder::STATES;
	kit.end_group();

	kit.begin_group("ODIN FILES");
	kit.value("Configuration", "odin.ini, written by this menu",
	          shown_path(state.settings_file, "none: ODIN_CONFIG=none").c_str(), nullptr, "READ ONLY");
	kit.value("Shader lists", "the 3D programs each game draws with, made ready when it starts",
	          shown_path(state.shader_lists, "none").c_str(), nullptr, "READ ONLY");
	kit.end_group();

	kit.begin_group("LOG");
	kit.value("Log file", "what Odin says, kept from its last launch; the one before in odin-previous.log",
	          shown_path(state.log_file, "none: ODIN_LOG=none").c_str(), nullptr, "READ ONLY");

	int verbose = state.verbose ? 0 : 1;

	if (kit.choice("Detailed log", "each access of the board, in the terminal and the log file; Odin runs far "
	               "slower · at the next launch", ON_OFF, verbose, state.verbose_given ? "set by the command line: -v" : nullptr))
		state.verbose = verbose == 0;
	kit.end_group();
}

} /* namespace odin */
