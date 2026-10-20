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

#ifndef ODIN_MENU_KIT_HPP
#define ODIN_MENU_KIT_HPP

#include <span>
#include <string>

#include "imgui/imgui.h"

namespace odin {

/* The colours of a palette of the menu (mock-up v50): the screen behind
 * the pages, the frame and its groups, the ink and its two softer tones,
 * the lines between rows, the outline of a control and its hovered fill,
 * the accent for what is touched, the action colour for what is chosen
 * and the ink on it. */
struct MenuPalette {
	ImU32 screen, frame, group, ink, dim, faint, line, outline, hover, accent, action, on_action;
};

/* Rúnasteinn, the palette by default: granite and the red ochre of painted
 * runestones. The frame lets the game show through, at a fifth. */
inline constexpr MenuPalette RUNASTEINN = {
	IM_COL32(0x2A, 0x2A, 0x28, 0xFF), IM_COL32(0x2F, 0x2F, 0x2C, 0xCC), IM_COL32(0x38, 0x38, 0x34, 0x99),
	IM_COL32(0xEE, 0xEA, 0xE2, 0xFF), IM_COL32(0xB0, 0xAB, 0xA0, 0xFF), IM_COL32(0x83, 0x7F, 0x75, 0xFF),
	IM_COL32(0xEE, 0xEA, 0xE2, 0x24), IM_COL32(0xEE, 0xEA, 0xE2, 0x38), IM_COL32(0xEE, 0xEA, 0xE2, 0x10),
	IM_COL32(0xE0, 0x66, 0x4A, 0xFF), IM_COL32(0x6A, 0xA9, 0xB0, 0xFF), IM_COL32(0x2A, 0x2A, 0x28, 0xFF),
};

/* The dressed widgets of the menu, on Dear ImGui: corners cut at 45
 * degrees, one stroke of one pixel, the action colour only on what is
 * chosen. Sizes are in points, times the scale of the display. One is
 * made for each picture of the menu. */
class MenuKit {
public:
	MenuKit(const MenuPalette &palette, float scale) : m_palette(palette), m_scale(scale) {}

	const MenuPalette &palette() const { return m_palette; }
	float px(float points) const { return points * m_scale; }

	/* A frame with its corners cut; a colour of 0 is none */
	static void frame(ImDrawList *list, ImVec2 a, ImVec2 b, float cut, ImU32 fill, ImU32 border);

	/* The tabs in the middle of the width: a click picks one. True when
	 * the choice changed. */
	bool tabs(std::span<const char *const> names, int &chosen) const;

	/* A group of settings: its title, then its rows in a frame */
	void begin_group(const char *title);
	void end_group();

	/* A row: its name, what it does under it, and at its right a choice
	 * among labels (true when it changed; -1 is none of them) or a text.
	 * A row the command line sets says so (`locked`) and does not change;
	 * a row that cannot change now is `off`. */
	bool choice(const char *name, const char *about, std::span<const char *const> labels, int &chosen,
	            const char *locked = nullptr, bool off = false);
	void text(const char *name, const char *about, const char *value);
	/* A row with a list that drops down from its right: true when the
	 * choice changed */
	bool dropdown(const char *name, const char *about, std::span<const std::string> labels, int &chosen);

	/* A row of a list of controls, at that place and that width: its name
	 * and its inputs; lit while the control is worked, "press..." while
	 * its input is being caught. True on a click. Its height in `h`. */
	bool binding(const char *id, ImVec2 at, float width, bool first, const char *name, const char *inputs,
	             bool lit, bool catching, float &h) const;
	/* A button of a page, after the one before on its line (`same_line`).
	 * True on a click. */
	bool button(const char *label, bool same_line = false) const;

private:
	/* The top of the row, its line above it when it is not the first of
	 * its group, and the room left for its texts */
	ImVec2 begin_row(float &width);
	float texts(ImVec2 at, float width, const char *name, const char *about, const char *locked) const;

	const MenuPalette &m_palette;
	float m_scale;
	ImVec2 m_group_top = {};
	bool m_first_row = true;
};

} /* namespace odin */

#endif /* ODIN_MENU_KIT_HPP */
