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
 * the accent for what is touched and the ink on it, the action colour for
 * what is chosen and the ink on it. The drawings: the body of a gamepad and
 * a part on it, their strokes (the details take the outline of a control),
 * an arrow on a part; the lavender of the cabinet, its strokes and its
 * details. */
struct MenuPalette {
	ImU32 screen, frame, group, ink, dim, faint, line, outline, hover, accent, on_accent, action, on_action;
	ImU32 body, part, stroke, arrow;
	ImU32 cabinet, cabinet_stroke, cabinet_detail;
};

/* Rúnasteinn, the palette by default: granite and the red ochre of painted
 * runestones. The frame lets the game show through, at a fifth. */
inline constexpr MenuPalette RUNASTEINN = {
	IM_COL32(0x2A, 0x2A, 0x28, 0xFF), IM_COL32(0x2F, 0x2F, 0x2C, 0xCC), IM_COL32(0x38, 0x38, 0x34, 0x99),
	IM_COL32(0xEE, 0xEA, 0xE2, 0xFF), IM_COL32(0xB0, 0xAB, 0xA0, 0xFF), IM_COL32(0x83, 0x7F, 0x75, 0xFF),
	IM_COL32(0xEE, 0xEA, 0xE2, 0x24), IM_COL32(0xEE, 0xEA, 0xE2, 0x38), IM_COL32(0xEE, 0xEA, 0xE2, 0x10),
	IM_COL32(0xE0, 0x66, 0x4A, 0xFF), IM_COL32(0x16, 0x06, 0x04, 0xFF), IM_COL32(0x6A, 0xA9, 0xB0, 0xFF),
	IM_COL32(0x2A, 0x2A, 0x28, 0xFF),
	IM_COL32(0x2F, 0x2F, 0x2C, 0xFF), IM_COL32(0x38, 0x38, 0x34, 0xFF), IM_COL32(0xEE, 0xEA, 0xE2, 0x9E),
	IM_COL32(0xEE, 0xEA, 0xE2, 0x73),
	IM_COL32(0xBD, 0xB5, 0xD0, 0xFF), IM_COL32(0xBD, 0xB5, 0xD0, 0xCC), IM_COL32(0xBD, 0xB5, 0xD0, 0x66),
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

	/* The icons of the menu (mock-up v50), drawn in a square of that side:
	 * a gamepad, a gamepad with its cross, a screen, a keyboard, a folder,
	 * an i */
	enum class Icon { PAD, INPUT, DISPLAY, KEYBOARD, FOLDER, ABOUT };
	static void icon(ImDrawList *list, Icon icon, ImVec2 at, float side, ImU32 colour);

	/* A frame with its corners cut; a colour of 0 is none */
	static void frame(ImDrawList *list, ImVec2 a, ImVec2 b, float cut, ImU32 fill, ImU32 border);
	/* A line of dashes along points, `on` long and `off` apart; a frame of
	 * them with its corners cut */
	static void dashes(ImDrawList *list, std::span<const ImVec2> points, bool closed, ImU32 colour, float width,
	                   float on, float off);
	void dashed_frame(ImDrawList *list, ImVec2 a, ImVec2 b, ImU32 colour) const;

	/* The tabs in the middle of the width, each its icon and its name (the
	 * one chosen only, when `narrow`): a click picks one. True when the
	 * choice changed. */
	bool tabs(std::span<const char *const> names, std::span<const Icon> icons, bool narrow, int &chosen) const;

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
	/* A row with a value at its right and after it a button (true on a
	 * click), or a word in small capitals when there is no button; one the
	 * command line sets says so (`locked`), with no button */
	bool value(const char *name, const char *about, const char *value, const char *action, const char *word = nullptr,
	           const char *locked = nullptr);
	/* A row with a list that drops down from its right: true when the
	 * choice changed */
	bool dropdown(const char *name, const char *about, std::span<const std::string> labels, int &chosen);
	/* The same list from a box alone at that place, that wide, an icon
	 * before what is chosen (none: nullptr); its height */
	bool picker(const char *id, ImVec2 at, float width, std::span<const std::string> labels, int &chosen,
	            const Icon *icon = nullptr) const;
	float picker_height() const;

	/* A row of a list of controls, at that place and that width: its name
	 * and its inputs; lit while the control is worked, "press..." while
	 * its input is being caught. True on a click. Its height in `h`. */
	bool binding(const char *id, ImVec2 at, float width, bool first, const char *name, const char *inputs,
	             bool lit, bool catching, float &h) const;
	/* A button of a page, after the one before on its line (`same_line`).
	 * True on a click. */
	bool button(const char *label, bool same_line = false) const;
	/* A button at that place, outlined with the action colour while on; its
	 * size first. True on a click. */
	ImVec2 button_size(const char *label) const;
	bool button(const char *label, ImVec2 at, bool on) const;

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
