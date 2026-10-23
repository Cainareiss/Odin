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

/* The bar over the game and the library (mock-up v3, Atelier), in the
 * palette of the menu: the six menus, the game and its speed at the right;
 * the rune and the name of Odin are those of the window.
 * The menu open has the accent under its name. */

#include "odin/menu.hpp"

#include <cmath>
#include <cstdio>
#include <string>
#include <utility>

#include "imgui/imgui.h"
#include "imgui/imgui_impl_sdl3.h"
/* Its menus closed from outside them, Dear ImGui's own code */
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wconversion"
#include "imgui/imgui_internal.h"
#pragma GCC diagnostic pop
#include "odin/menu_kit.hpp"
#include "odin/version.hpp"

namespace odin {

namespace {

/* In points: the room around a name of the bar, between two, around the
 * items of a menu; the cut of the corners of a menu; the line of the
 * accent under the menu open */
constexpr float NAME_WIDE = 7.0f;
constexpr float NAME_HIGH = 5.0f;
constexpr float NAME_GAP = 10.0f;
constexpr float ITEMS_INSIDE = 5.0f;
constexpr float ITEM_HIGH = 3.0f;
constexpr float MENU_CUT = 6.0f;
constexpr float ACCENT = 2.0f;
/* The bar lets the game show through, a little */
constexpr float BAR_ALPHA = 0.96f;

constexpr const char *NAMES[] = { "FILE", "INPUT", "VIDEO", "AUDIO", "DEBUG", "ABOUT" };

/* An item that asks something: true when clicked */
bool
item(const char *label, const std::string &key = {}, bool checked = false, bool enabled = true)
{
	return ImGui::MenuItem(label, key.empty() ? nullptr : key.c_str(), checked, enabled);
}

} /* namespace */

void
Menu::forget_input()
{
	ImGui::SetCurrentContext(m_imgui);
	ImGui::GetIO().ClearInputKeys();
	ImGui::GetIO().ClearInputMouse();
}

void
Menu::hide_bar()
{
	m_bar_hidden = true;
}

void
Menu::close_bar_menus()
{
	if (std::exchange(m_bar_hidden, false))
		ImGui::ClosePopupsExceptModals();
}

void
Menu::build_bar(BarState &state)
{
	begin_frame(state.theme);
	bar(state);
	ImGui::Render();
}

void
Menu::bar(BarState &state)
{
	using Ask = BarState::Ask;
	using Action = Hotkeys::Action;
	const MenuKit kit(*m_palette, m_scale);
	const MenuPalette &p = kit.palette();
	ImGuiStyle &style = ImGui::GetStyle();
	const ImVec2 safe = style.DisplaySafeAreaPadding;
	const float line = ImGui::GetTextLineHeight();
	/* What is asked, and on which page */
	const auto ask = [&state](Ask what, int page = -1) {
		state.ask = what;
		state.page = page;
	};
	/* The first key of an action, as odin.ini says it */
	const auto key = [&state](Action action) -> const std::string & { return state.keys[static_cast<size_t>(action)]; };
	/* A menu: a frame with its corners cut, behind its items */
	const auto framed = [&kit, &p] {
		const ImVec2 a = ImGui::GetWindowPos(), b = { a.x + ImGui::GetWindowWidth(), a.y + ImGui::GetWindowHeight() };

		kit.frame(ImGui::GetWindowDrawList(), a, b, kit.px(MENU_CUT), p.screen, p.outline);
	};

	style.DisplaySafeAreaPadding = { 0.0f, 0.0f };
	const std::pair<ImGuiCol, ImU32> colours[] = {
		{ ImGuiCol_WindowBg, IM_COL32_BLACK_TRANS }, { ImGuiCol_MenuBarBg, MenuKit::alpha(p.screen, BAR_ALPHA) },
		{ ImGuiCol_PopupBg, IM_COL32_BLACK_TRANS }, { ImGuiCol_Border, IM_COL32_BLACK_TRANS },
		{ ImGuiCol_Header, p.hover }, { ImGuiCol_HeaderHovered, p.hover }, { ImGuiCol_HeaderActive, p.hover },
		{ ImGuiCol_Text, p.ink }, { ImGuiCol_TextDisabled, p.faint }, { ImGuiCol_Separator, p.line },
		{ ImGuiCol_CheckMark, p.accent }, { ImGuiCol_FrameBg, p.hover }, { ImGuiCol_FrameBgHovered, p.hover },
		{ ImGuiCol_FrameBgActive, p.hover }, { ImGuiCol_SliderGrab, p.accent },
		{ ImGuiCol_SliderGrabActive, p.accent }, { ImGuiCol_NavCursor, IM_COL32_BLACK_TRANS },
	};

	for (const auto &[which, colour] : colours)
		ImGui::PushStyleColor(which, colour);
	ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, { kit.px(NAME_WIDE), kit.px(NAME_HIGH) });
	ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, { kit.px(NAME_GAP), kit.px(ITEM_HIGH) });
	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, { std::round(kit.px(ITEMS_INSIDE)), std::round(kit.px(ITEMS_INSIDE)) });
	ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
	ImGui::PushStyleVar(ImGuiStyleVar_PopupBorderSize, 0.0f);
	ImGui::PushStyleVar(ImGuiStyleVar_PopupRounding, 0.0f);
	ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 0.0f);
	ImGui::PushStyleVar(ImGuiStyleVar_GrabRounding, 0.0f);

	if (ImGui::BeginMainMenuBar()) {
		ImDrawList *const list = ImGui::GetWindowDrawList();
		const ImVec2 top = ImGui::GetWindowPos(), size = ImGui::GetWindowSize();
		const float bottom = top.y + size.y;
		int open = -1;

		list->AddLine({ top.x, bottom - kit.stroke() / 2.0f }, { top.x + size.x, bottom - kit.stroke() / 2.0f }, p.line,
		              kit.stroke());

		for (int m = 0; m < static_cast<int>(std::size(NAMES)); m++) {
			const float at = ImGui::GetCursorScreenPos().x + std::trunc(style.ItemSpacing.x * 0.5f);
			const float w = ImGui::CalcTextSize(NAMES[m]).x;

			ImGui::PushStyleColor(ImGuiCol_Text, p.dim);
			const bool shown = ImGui::BeginMenu(NAMES[m]);
			ImGui::PopStyleColor();
			if (!shown)
				continue;
			open = m;
			list->AddLine({ at - style.ItemSpacing.x, bottom - kit.px(ACCENT) / 2.0f },
			              { at + w + style.ItemSpacing.x, bottom - kit.px(ACCENT) / 2.0f }, p.accent, kit.px(ACCENT));

			framed();
			switch (m) {
			case 0:
				if (item("Open..."))
					ask(Ask::OPEN);
				if (item("Close game", {}, false, state.game))
					ask(Ask::CLOSE);
				if (item("Reset", key(Action::RESET), false, state.game))
					ask(Ask::RESET);
				ImGui::Separator();
				if (item("Save state", key(Action::SAVE_STATE), false,
				         state.game))
					ask(Ask::SAVE_STATE);
				if (item("Load state", key(Action::LOAD_STATE), false,
				         state.game && state.saved))
					ask(Ask::LOAD_STATE);
				ImGui::Separator();
				if (item("ROM folder...", {}, false, !state.roms_given))
					ask(Ask::ROMS);
				if (item("BIOS folder...", {}, false, !state.bios_given))
					ask(Ask::BIOS);
				if (item("Paths..."))
					ask(Ask::SETTINGS, PATHS);
				ImGui::Separator();
				if (item("Settings...", key(Action::MENU)))
					ask(Ask::SETTINGS);
				ImGui::Separator();
				if (item("Quit", key(Action::QUIT)))
					ask(Ask::QUIT);
				break;
			case 1:
				if (item("Controls..."))
					ask(Ask::SETTINGS, INPUT);
				if (item("Hotkeys..."))
					ask(Ask::SETTINGS, HOTKEYS);
				if (item("Outputs..."))
					ask(Ask::SETTINGS, OUTPUTS);
				if (item("Cabinet link..."))
					ask(Ask::SETTINGS, LINK);
				break;
			case 2:
				if (item("Full screen", key(Action::FULLSCREEN), state.fullscreen))
					ask(Ask::FULLSCREEN);
				if (ImGui::BeginMenu("Bezel")) {
					framed();
					for (unsigned s = 0; s < std::size(SIDES_SHOWN); s++)
						if (item(SIDES_SHOWN[s], {}, state.sides == s))
							state.sides = s;
					ImGui::EndMenu();
				}
				if (item("Show the speed", {}, state.show_speed))
					state.show_speed = !state.show_speed;
				ImGui::Separator();
				if (item("Display settings..."))
					ask(Ask::SETTINGS, DISPLAY);
				break;
			case 3: {
				int volume = static_cast<int>(state.volume);

				ImGui::BeginDisabled(state.silent);
				ImGui::TextUnformatted("Volume");
				ImGui::SetNextItemWidth(kit.px(150.0f));
				if (ImGui::SliderInt("##volume", &volume, 0, 100, "%d %%", ImGuiSliderFlags_AlwaysClamp))
					state.volume = static_cast<unsigned>(volume);
				if (item("Sound in the background", {}, state.background))
					state.background = !state.background;
				ImGui::EndDisabled();
				ImGui::Separator();
				if (item("Audio settings..."))
					ask(Ask::SETTINGS, AUDIO);
				break;
			}
			case 4:
				if (item("Pause", key(Action::PAUSE), state.paused, state.game))
					ask(Ask::PAUSE);
				if (item("Log each access (next game)", {}, state.verbose, !state.verbose_given))
					state.verbose = !state.verbose;
				break;
			default:
				if (item("About Odin..."))
					ask(Ask::SETTINGS, ABOUT);
				if (*odin_version()) {
					ImGui::Separator();
					ImGui::TextDisabled("%s", odin_version());
				}
				break;
			}
			ImGui::EndMenu();
		}

		/* The game and its speed, or what went wrong */
		char right[256];

		if (!state.note.empty())
			std::snprintf(right, sizeof(right), "%s", state.note.c_str());
		else if (state.paused)
			std::snprintf(right, sizeof(right), "%s  ·  paused", state.title.c_str());
		else if (state.show_speed && state.fps > 0.0f)
			std::snprintf(right, sizeof(right), "%s  ·  %.0f fps", state.title.c_str(), static_cast<double>(state.fps));
		else
			std::snprintf(right, sizeof(right), "%s", state.title.c_str());

		const float room = top.x + size.x - kit.px(NAME_WIDE) - ImGui::GetCursorScreenPos().x;
		const std::string shown = MenuKit::fitted(right, room);

		list->AddText({ top.x + size.x - kit.px(NAME_WIDE) - MenuKit::measure(shown.c_str()),
		                top.y + (size.y - line) / 2.0f }, state.note.empty() ? p.faint : p.danger, shown.c_str());
		state.open = open >= 0;
		state.bottom = bottom;
		ImGui::EndMainMenuBar();
	}
	ImGui::PopStyleVar(8);
	ImGui::PopStyleColor(static_cast<int>(std::size(colours)));
	style.DisplaySafeAreaPadding = safe;
	state.wants_mouse = ImGui::GetIO().WantCaptureMouse;
}

} /* namespace odin */
