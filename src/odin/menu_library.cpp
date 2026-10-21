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

/* The library: the games to choose from when Odin starts without one, as
 * the game picker of sm2-emu lays them out (mock-up v2), in the palette of
 * the menu. Tiles at the left, the game chosen framed; its sheet at the
 * right. Arrows choose, Enter or a click on the game chosen plays it. */

#include "odin/menu.hpp"

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <string>

#include "imgui/imgui.h"
#include "imgui/imgui_impl_sdl3.h"
#include "odin/menu_kit.hpp"

namespace odin {

namespace {

/* In points: the margin of the window, the gap between the tiles and
 * before the sheet, what is inside a tile and the sheet; the share of the
 * width the sheet takes; a tile as the screen of the game is (4:3), three
 * on a row at most; the cuts of the corners */
constexpr float MARGIN = 16.0f;
constexpr float GAP = 12.0f;
constexpr float TILE_INSIDE = 7.0f;
constexpr float SHEET_INSIDE = 14.0f;
constexpr float SHEET_SHARE = 0.31f;
constexpr float TILE_W = 4.0f;
constexpr float TILE_H = 3.0f;
constexpr int COLUMNS = 3;
constexpr float TILE_CUT = 5.0f;
constexpr float SHEET_CUT = 8.0f;
/* The back of a tile, darker than the window (mock-up v2); a game that
 * cannot be played: its picture that dim */
constexpr ImU32 TILE_BACK = IM_COL32(0x1D, 0x1D, 0x1B, 0xFF);
constexpr ImU32 DIMMED = IM_COL32(0xFF, 0xFF, 0xFF, 0x50);
/* The title of the head, its letters a fifth of the font apart; the line
 * of the accent under the head, its opacity, the share of it that fades
 * at each end; the title of the sheet, larger than the font */
constexpr float TITLE_SPACING = 0.2f;
constexpr float LINE_ALPHA = 0.7f;
constexpr float LINE_FADE = 0.2f;
constexpr float SHEET_TITLE = 1.35f;

/* The game chosen to be played, when its files are all there */
void
play_chosen(LibraryState &state)
{
	const LibraryState::Game &game = state.games[static_cast<size_t>(state.selected)];

	if (game.missing.empty())
		state.play = game.name;
}

/* A text from `at`, wrapped to that width: the height it took */
float
wrapped(ImDrawList *list, ImVec2 at, float width, float size, ImU32 colour, const std::string &text)
{
	ImFont *const font = ImGui::GetFont();

	list->AddText(font, size, at, colour, text.c_str(), nullptr, width);
	return font->CalcTextSizeA(size, FLT_MAX, width, text.c_str()).y;
}

} /* namespace */

void
Menu::add_library_picture(const std::string &game, SDL_Surface *rgba)
{
	m_library_handed.emplace_back(game, rgba);
}

void
Menu::upload_library_pictures()
{
	for (const auto &[game, rgba] : m_library_handed) {
		const auto old = m_library_pictures.find(game);
		GLuint texture = 0;

		if (old != m_library_pictures.end())
			glDeleteTextures(1, &old->second.texture);
		glGenTextures(1, &texture);
		glBindTexture(GL_TEXTURE_2D, texture);
		glPixelStorei(GL_UNPACK_ROW_LENGTH, rgba->pitch / 4);
		glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, rgba->w, rgba->h, 0, GL_RGBA, GL_UNSIGNED_BYTE, rgba->pixels);
		glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
		glGenerateMipmap(GL_TEXTURE_2D);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
		glBindTexture(GL_TEXTURE_2D, 0);
		m_library_pictures[game] = { texture, static_cast<float>(rgba->w), static_cast<float>(rgba->h) };
		SDL_DestroySurface(rgba);
	}
	m_library_handed.clear();
}

void
Menu::open_library()
{
	ImGui::SetCurrentContext(m_imgui);
	ImGui::GetIO().ClearInputKeys();
	ImGui::GetIO().ClearInputMouse();
}

void
Menu::build_library(LibraryState &state)
{
	ImGui::SetCurrentContext(m_imgui);
	follow_scale();
	ImGui_ImplSDL3_NewFrame();
	add_fonts();
	ImGui::NewFrame();
	close_bar_menus();

	const MenuKit kit(RUNASTEINN, m_scale);
	const MenuPalette &palette = kit.palette();
	const ImVec2 room = ImGui::GetIO().DisplaySize;
	const float margin = kit.px(MARGIN), gap = kit.px(GAP), line = ImGui::GetTextLineHeight();
	const float size = ImGui::GetFontSize();
	const int count = static_cast<int>(state.games.size());

	/* The library reads the keys itself: Dear ImGui moves nothing */
	ImGui::SetNextWindowPos({ 0.0f, 0.0f });
	ImGui::SetNextWindowSize(room);
	ImGui::Begin("Library", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
	             ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoNav);

	ImDrawList *const list = ImGui::GetWindowDrawList();

	list->AddRectFilled({ 0.0f, 0.0f }, room, palette.screen);

	/* The head: what to do, the keys at its right, the line of the accent
	 * under them */
	const char keys[] = "Enter: play    Ctrl+Q: quit";

	MenuKit::write(list, { margin, margin }, palette.cabinet, "SELECT A GAME", size, size * TITLE_SPACING);
	list->AddText({ room.x - margin - ImGui::CalcTextSize(keys).x, margin }, palette.faint, keys);

	const float under = margin + line + kit.px(8.0f);

	MenuKit::fading_line(list, margin, room.x - margin, under, kit.stroke() * 2.0f,
	                     MenuKit::alpha(palette.accent, LINE_ALPHA), LINE_FADE);

	/* The tiles at the left, as large as four on a row and their rows in
	 * the height allow; the sheet at the right */
	const float top = under + kit.px(14.0f), bottom = room.y - margin;
	const float sheet_w = std::round((room.x - 2.0f * margin) * SHEET_SHARE);
	const float grid_w = room.x - 2.0f * margin - sheet_w - gap;
	const float rows = static_cast<float>(std::max(1, (count + COLUMNS - 1) / COLUMNS));
	const float columns = static_cast<float>(COLUMNS);
	float tile_w = (grid_w - (columns - 1.0f) * gap) / columns;
	float tile_h = tile_w * TILE_H / TILE_W;

	if (rows * tile_h + (rows - 1.0f) * gap > bottom - top) {
		tile_h = (bottom - top - (rows - 1.0f) * gap) / rows;
		tile_w = tile_h * TILE_W / TILE_H;
	}
	tile_w = std::floor(tile_w);
	tile_h = std::floor(tile_h);

	/* The arrows move the choice, as the tiles lie; Enter plays */
	if (count) {
		const auto pressed = [](ImGuiKey key) { return ImGui::IsKeyPressed(key, true); };
		int chosen = state.selected;

		if (pressed(ImGuiKey_RightArrow))
			chosen++;
		if (pressed(ImGuiKey_LeftArrow))
			chosen--;
		if (pressed(ImGuiKey_DownArrow) && chosen + COLUMNS < count)
			chosen += COLUMNS;
		if (pressed(ImGuiKey_UpArrow) && chosen >= COLUMNS)
			chosen -= COLUMNS;
		state.selected = std::clamp(chosen, 0, count - 1);
		if (pressed(ImGuiKey_Enter) || pressed(ImGuiKey_KeypadEnter))
			play_chosen(state);
	}

	for (int i = 0; i < count; i++) {
		const LibraryState::Game &game = state.games[static_cast<size_t>(i)];
		const ImVec2 a = { margin + static_cast<float>(i % COLUMNS) * (tile_w + gap),
			           top + static_cast<float>(i / COLUMNS) * (tile_h + gap) };
		const ImVec2 b = { a.x + tile_w, a.y + tile_h };
		const float inside = kit.px(TILE_INSIDE);

		/* A click chooses a game; on the game chosen, plays it */
		ImGui::SetCursorScreenPos(a);
		ImGui::PushID(i);
		if (ImGui::InvisibleButton("tile", { tile_w, tile_h })) {
			if (state.selected == i)
				play_chosen(state);
			state.selected = i;
		}
		const bool hovered = ImGui::IsItemHovered();
		ImGui::PopID();

		const bool chosen = i == state.selected, playable = game.missing.empty();

		const auto picture = m_library_pictures.find(game.name);
		const float cut = kit.px(TILE_CUT);

		kit.frame(list, a, b, cut, TILE_BACK, 0);
		list->PushClipRect(a, b, true);
		if (picture != m_library_pictures.end()) {
			/* Its picture whole in the tile, the corners cut again over
			 * it */
			const float k = std::min(tile_w / picture->second.w, tile_h / picture->second.h);
			const ImVec2 pa = { std::round(a.x + (tile_w - picture->second.w * k) / 2.0f),
				            std::round(a.y + (tile_h - picture->second.h * k) / 2.0f) };

			list->AddImage(ImTextureRef(static_cast<ImTextureID>(picture->second.texture)), pa,
			               { pa.x + std::round(picture->second.w * k), pa.y + std::round(picture->second.h * k) },
			               { 0.0f, 0.0f }, { 1.0f, 1.0f }, playable ? IM_COL32_WHITE : DIMMED);
			list->AddTriangleFilled(a, { a.x + cut, a.y }, { a.x, a.y + cut }, palette.screen);
			list->AddTriangleFilled({ b.x, a.y }, { b.x, a.y + cut }, { b.x - cut, a.y }, palette.screen);
			list->AddTriangleFilled(b, { b.x - cut, b.y }, { b.x, b.y - cut }, palette.screen);
			list->AddTriangleFilled({ a.x, b.y }, { a.x, b.y - cut }, { a.x + cut, b.y }, palette.screen);
		} else {
			wrapped(list, { a.x + inside, a.y + inside }, tile_w - 2.0f * inside, size,
			        playable ? palette.dim : palette.faint, game.title);
		}
		if (!playable) {
			list->AddRectFilled({ a.x, b.y - inside - line - kit.px(4.0f) }, b, MenuKit::alpha(TILE_BACK, 0.85f));
			MenuKit::small_caps(list, { a.x + inside, b.y - inside - line }, palette.accent, "MISSING");
		}
		list->PopClipRect();
		kit.frame(list, a, b, kit.px(TILE_CUT), 0, chosen ? palette.action : hovered ? palette.dim : palette.outline,
		          chosen ? 3.0f : 1.0f);
	}

	/* The sheet of the game chosen: its title, its name in MAME, what is
	 * known of it; or what it lacks, where it was looked for, the folders
	 * to choose; how to play it at its foot */
	const ImVec2 sa = { room.x - margin - sheet_w, top }, sb = { room.x - margin, bottom };
	const float inside = kit.px(SHEET_INSIDE), x = sa.x + inside, width = sheet_w - 2.0f * inside;
	const char play[] = "Enter or a second click: play", enter[] = "Enter";
	const float foot = sb.y - inside - ImGui::GetFont()->CalcTextSizeA(size, FLT_MAX, width, play).y;
	float y = sa.y + inside;

	kit.frame(list, sa, sb, kit.px(SHEET_CUT), palette.body, 0);
	if (count) {
		const LibraryState::Game &game = state.games[static_cast<size_t>(state.selected)];

		y += wrapped(list, { x, y }, width, size * SHEET_TITLE, palette.ink, game.title) + kit.px(4.0f);
		list->AddText({ x, y }, palette.faint, game.name.c_str());
		y += line + kit.px(8.0f);
		list->AddLine({ x, y }, { x + width, y }, palette.line, kit.stroke());
		y += kit.px(10.0f);

		list->PushClipRect({ x, y }, { x + width, foot - kit.px(6.0f) }, true);
		if (state.failed == game.name)
			y += wrapped(list, { x, y }, width, size, palette.danger,
			             "It did not start: odin.log tells why.") + line;
		if (state.roms.empty()) {
			y += wrapped(list, { x, y }, width, size, palette.ink,
			             "Odin plays the games from the folder that holds them, as MAME keeps them: a folder, a zip or "
			             "a 7z of the name of each game.") + line;
		} else if (!game.missing.empty()) {
			y += wrapped(list, { x, y }, width, size, palette.ink, "Files missing:") + kit.px(4.0f);
			for (const std::string &file : game.missing)
				y += wrapped(list, { x, y }, width, size, palette.dim, file);
			y += line;
			y += wrapped(list, { x, y }, width, size, palette.faint,
			             "Looked for in " + shown_path(state.roms, "") +
			             (state.bios.empty() ? std::string() : " and " + shown_path(state.bios, ""))) + line;
		}
		if (!game.missing.empty()) {
			/* The folders to choose, unless the command line gives them */
			const char roms[] = "Folder of the games...", bios[] = "Folder of the BIOS...";

			if (!state.roms_given && !state.choosing) {
				if (kit.button(roms, { x, y }, false))
					state.choose = LibraryState::Folder::ROMS;
				y += kit.button_size(roms).y + kit.px(6.0f);
			}
			if (!state.bios_given && !state.choosing && !state.roms.empty() &&
			    kit.button(bios, { x, y }, false))
				state.choose = LibraryState::Folder::BIOS;
		}
		list->PopClipRect();

		/* How to play it, on as many lines as it takes: Enter in the
		 * action colour */
		if (game.missing.empty()) {
			wrapped(list, { x, foot }, width, size, palette.faint, play);
			list->AddText({ x, foot }, palette.action, enter);
		}
	}

	ImGui::End();
	ImGui::Render();
}

} /* namespace odin */
