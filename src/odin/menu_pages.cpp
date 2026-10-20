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

/* The pages Outputs and Paths of the menu (mock-up v50) */

#include "odin/menu.hpp"

#include <algorithm>
#include <cfloat>
#include <cstring>
#include <string>

#include "imgui/imgui.h"
#include "odin/env.hpp"
#include "odin/menu_kit.hpp"

namespace odin {

namespace {

constexpr const char *ON_OFF[] = { "On", "Off" };

/* A page in two: what it shows in a box at the left, its settings at the
 * right, as 1 to 1.15 (mock-up v50); the room between them, below the row
 * of the games, the cut of the corners of the box and the room inside */
constexpr float LEFT_SHARE = 1.0f / 2.15f;
constexpr float SPLIT_GAP = 10.0f;
constexpr float ROW_GAP = 6.0f;
constexpr float BOX_CUT = 5.0f;
constexpr float BOX_INSIDE = 8.0f;

/* An output: its width at the least, the side of its icon, the room in it
 * and between two */
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

	MenuKit::frame(ImGui::GetWindowDrawList(), box.a, box.b, kit.px(BOX_CUT), 0, kit.palette().line);
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
	ImDrawList *const list = ImGui::GetWindowDrawList();
	const MenuPalette &palette = kit.palette();
	const float inside = kit.px(BOX_INSIDE), gap = kit.px(TILE_GAP), pad = kit.px(TILE_PAD), side = kit.px(TILE_ICON);
	const float width = box.b.x - box.a.x - 2.0f * inside, size = ImGui::GetFontSize();
	const size_t columns = std::max<size_t>(1, static_cast<size_t>((width + gap) / (kit.px(TILE_LEAST) + gap)));
	const size_t rows = (state.outputs.size() + columns - 1) / columns;
	const float tile_w = (width - static_cast<float>(columns - 1) * gap) / static_cast<float>(columns);
	const char caption[] = "The cabinet's outputs: they light when the game turns them on.";
	float names = 0.0f;

	for (const MenuState::Output &output : state.outputs)
		names = std::max(names, centred({}, tile_w - 2.0f * pad, size, output.name.c_str(), 0));

	const float tile_h = 2.0f * pad + side + kit.px(4.0f) + names;
	const float tiles_h = static_cast<float>(rows) * tile_h + static_cast<float>(rows ? rows - 1 : 0) * gap;
	const float caption_h = centred({}, width, size, caption, 0);
	const float all_h = tiles_h + (rows ? gap : 0.0f) + caption_h;
	const float top = std::max(box.a.y + inside, (box.a.y + box.b.y - all_h) / 2.0f);

	list->PushClipRect(box.a, box.b, true);
	for (size_t i = 0; i < state.outputs.size(); i++) {
		const MenuState::Output &output = state.outputs[i];
		const ImVec2 a = { box.a.x + inside + static_cast<float>(i % columns) * (tile_w + gap),
		                   top + static_cast<float>(i / columns) * (tile_h + gap) };
		const ImU32 colour = output.on ? palette.accent : palette.dim;

		MenuKit::frame(list, a, { a.x + tile_w, a.y + tile_h }, kit.px(BOX_CUT), output.on ? palette.hover : 0,
		               output.on ? palette.accent : palette.line);
		MenuKit::icon(list, icon_of(output.role), { a.x + (tile_w - side) / 2.0f, a.y + pad }, side, colour);
		centred({ a.x + pad, a.y + pad + side + kit.px(4.0f) }, tile_w - 2.0f * pad, size, output.name.c_str(),
		        output.on ? palette.accent : palette.dim);
	}
	centred({ box.a.x + inside, top + tiles_h + (rows ? gap : 0.0f) }, width, size, caption, palette.faint);
	list->PopClipRect();
}

/* A path as the user knows it: the home folder as ~ */
std::string
shown_path(const std::string &path, const char *none)
{
	const std::string home = home_dir();

	if (path.empty())
		return none;
	if (!home.empty() && path.starts_with(home) && (path.size() == home.size() || path[home.size()] == '/'))
		return "~" + path.substr(home.size());
	return path;
}

} /* namespace */

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
	int verbose = state.verbose ? 0 : 1;

	if (kit.choice("Detailed log", "each access of the board, in the terminal; Odin runs far slower · at the next "
	               "launch", ON_OFF, verbose, state.verbose_given ? "set by the command line: -v" : nullptr))
		state.verbose = verbose == 0;
	kit.end_group();
}

} /* namespace odin */
