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

#include "odin/menu_kit.hpp"

#include <algorithm>
#include <array>
#include <cfloat>
#include <cmath>
#include <numbers>
#include <utility>

namespace odin {

namespace {

/* The cut of the corners of a group and of a control, the room around a
 * row and between its texts and its control, and the room in a cell of a
 * control, in points */
constexpr float GROUP_CUT = 6.0f;
constexpr float CONTROL_CUT = 3.0f;
constexpr float ROW_PAD = 8.0f;
constexpr float GAP = 14.0f;
constexpr float CELL_WIDE = 7.0f;
constexpr float CELL_HIGH = 3.0f;
/* The texts of a row have that room at the least beside its control, else
 * the control goes under them */
constexpr float TEXT_LEAST = 120.0f;
/* What is left of a colour on a row that cannot change */
constexpr float OFF_ALPHA = 0.4f;

/* The most labels a choice has */
constexpr size_t MOST_LABELS = 8;

ImU32
faded(ImU32 colour, bool off)
{
	if (!off)
		return colour;

	const ImU32 alpha = (colour >> IM_COL32_A_SHIFT) & 0xFF;

	return (colour & ~IM_COL32_A_MASK) |
	       (static_cast<ImU32>(static_cast<float>(alpha) * OFF_ALPHA) << IM_COL32_A_SHIFT);
}

/* The fill of a cell of a control, its corners cut on the outer sides of
 * the control only */
void
fill_cell(ImDrawList *list, ImVec2 a, ImVec2 b, float cut, bool first, bool last, ImU32 colour)
{
	std::array<ImVec2, 8> points;
	int n = 0;

	if (first) {
		points[n++] = { a.x, a.y + cut };
		points[n++] = { a.x + cut, a.y };
	} else {
		points[n++] = a;
	}
	if (last) {
		points[n++] = { b.x - cut, a.y };
		points[n++] = { b.x, a.y + cut };
		points[n++] = { b.x, b.y - cut };
		points[n++] = { b.x - cut, b.y };
	} else {
		points[n++] = { b.x, a.y };
		points[n++] = b;
	}
	if (first) {
		points[n++] = { a.x + cut, b.y };
		points[n++] = { a.x, b.y - cut };
	} else {
		points[n++] = { a.x, b.y };
	}
	list->AddConvexPolyFilled(points.data(), n, colour);
}

} /* namespace */

void
MenuKit::frame(ImDrawList *list, ImVec2 a, ImVec2 b, float cut, ImU32 fill, ImU32 border)
{
	const ImVec2 points[8] = { { a.x + cut, a.y }, { b.x - cut, a.y }, { b.x, a.y + cut }, { b.x, b.y - cut },
	                           { b.x - cut, b.y }, { a.x + cut, b.y }, { a.x, b.y - cut }, { a.x, a.y + cut } };

	if (fill)
		list->AddConvexPolyFilled(points, 8, fill);
	if (border)
		list->AddPolyline(points, 8, border, ImDrawFlags_Closed, 1.0f);
}

void
MenuKit::dashes(ImDrawList *list, std::span<const ImVec2> points, bool closed, ImU32 colour, float width, float on,
                float off)
{
	const size_t n = points.size();
	float left = on;	/* of the dash or of the gap now */
	bool drawing = true;

	for (size_t i = 0; i + 1 < n + (closed ? 1 : 0); i++) {
		ImVec2 p = points[i];
		const ImVec2 q = points[(i + 1) % n];
		float length = std::hypot(q.x - p.x, q.y - p.y);

		while (length > 0.0f) {
			const float step = std::min(left, length);
			const ImVec2 r = { p.x + (q.x - p.x) * step / length, p.y + (q.y - p.y) * step / length };

			if (drawing)
				list->AddLine(p, r, colour, width);
			p = r;
			length -= step;
			left -= step;
			if (left <= 0.0f) {
				drawing = !drawing;
				left = drawing ? on : off;
			}
		}
	}
}

void
MenuKit::dashed_frame(ImDrawList *list, ImVec2 a, ImVec2 b, ImU32 colour) const
{
	const float cut = px(CONTROL_CUT);
	const ImVec2 points[8] = { { a.x + cut, a.y }, { b.x - cut, a.y }, { b.x, a.y + cut }, { b.x, b.y - cut },
	                           { b.x - cut, b.y }, { a.x + cut, b.y }, { a.x, b.y - cut }, { a.x, a.y + cut } };

	dashes(list, points, true, colour, 1.0f, px(3.0f), px(2.0f));
}

/* The paths of the mock-up, in a square of 24 */
void
MenuKit::icon(ImDrawList *list, Icon icon, ImVec2 at, float side, ImU32 colour)
{
	const float k = side / 24.0f, w = std::max(1.0f, 1.8f * k);
	const auto p = [&](float x, float y) { return ImVec2{ at.x + x * k, at.y + y * k }; };
	const auto arc = [&](float x, float y, float r, float from, float to) {
		list->PathArcTo(p(x, y), r * k, from * std::numbers::pi_v<float> / 180.0f, to * std::numbers::pi_v<float> / 180.0f);
	};

	switch (icon) {
	case Icon::PAD:
	case Icon::INPUT:
		list->PathLineTo(p(6, 9));
		list->PathLineTo(p(18, 9));
		arc(18, 13, 4, 270, 360);
		list->PathLineTo(p(22, 14));
		arc(19, 14, 3, 0, 143);
		list->PathLineTo(p(15, 14));
		list->PathLineTo(p(9, 14));
		list->PathLineTo(p(7.4f, 15.8f));
		arc(5, 14, 3, 37, 180);
		list->PathLineTo(p(2, 13));
		arc(6, 13, 4, 180, 270);
		list->PathStroke(colour, ImDrawFlags_Closed, w);
		if (icon == Icon::INPUT) {
			list->AddLine(p(7, 12), p(7, 15), colour, w);
			list->AddLine(p(5.5f, 13.5f), p(8.5f, 13.5f), colour, w);
		}
		break;
	case Icon::DISPLAY:
		list->AddRect(p(3, 5), p(21, 17), colour, 2.0f * k, ImDrawFlags_None, w);
		list->AddLine(p(8, 21), p(16, 21), colour, w);
		break;
	case Icon::KEYBOARD:
		list->AddRect(p(2, 6), p(22, 18), colour, 2.0f * k, ImDrawFlags_None, w);
		for (const float x : { 6.0f, 10.0f, 14.0f, 18.0f })
			list->AddLine(p(x, 10), p(x + 1, 10), colour, w);
		list->AddLine(p(7, 14), p(17, 14), colour, w);
		break;
	case Icon::AUDIO:
	case Icon::MUTE:
		for (const ImVec2 q : { ImVec2{ 4, 9 }, ImVec2{ 4, 15 }, ImVec2{ 8, 15 }, ImVec2{ 13, 19 }, ImVec2{ 13, 5 },
		                        ImVec2{ 8, 9 } })
			list->PathLineTo(p(q.x, q.y));
		list->PathStroke(colour, ImDrawFlags_Closed, w);
		if (icon == Icon::AUDIO) {
			arc(13.35f, 12, 4, -48.5f, 48.5f);
			list->PathStroke(colour, ImDrawFlags_None, w);
		} else {
			list->AddLine(p(16, 9), p(21, 15), colour, w);
			list->AddLine(p(21, 9), p(16, 15), colour, w);
		}
		break;
	case Icon::HOTKEYS:
		list->AddRect(p(3, 7), p(21, 17), colour, 2.0f * k, ImDrawFlags_None, w);
		for (const float x : { 7.0f, 11.0f, 15.0f })
			list->AddLine(p(x, 11), p(x + 2, 11), colour, w);
		list->AddLine(p(8, 14), p(16, 14), colour, w);
		break;
	case Icon::OUTPUTS:
		list->AddCircle(p(12, 12), 3.0f * k, colour, 0, w);
		list->AddLine(p(12, 3), p(12, 6), colour, w);
		list->AddLine(p(12, 18), p(12, 21), colour, w);
		list->AddLine(p(3, 12), p(6, 12), colour, w);
		list->AddLine(p(18, 12), p(21, 12), colour, w);
		break;
	case Icon::LAMP:
		/* The bulb over its top, from its left foot to its right one */
		arc(12, 9, 6, 131.6f, 408.4f);
		list->PathLineTo(p(14.5f, 17));
		list->PathLineTo(p(9.5f, 17));
		list->PathStroke(colour, ImDrawFlags_Closed, w);
		list->AddLine(p(9, 18), p(15, 18), colour, w);
		list->AddLine(p(10, 21), p(14, 21), colour, w);
		break;
	case Icon::RUMBLE:
		list->AddRect(p(8, 4), p(16, 20), colour, 2.0f * k, ImDrawFlags_None, w);
		list->AddLine(p(4, 8), p(4, 16), colour, w);
		list->AddLine(p(20, 8), p(20, 16), colour, w);
		list->AddLine(p(2, 10), p(2, 14), colour, w);
		list->AddLine(p(22, 10), p(22, 14), colour, w);
		break;
	case Icon::LOCK:
		list->AddRect(p(5, 11), p(19, 20), colour, 2.0f * k, ImDrawFlags_None, w);
		list->PathLineTo(p(8, 11));
		arc(12, 8, 4, 180, 360);
		list->PathLineTo(p(16, 11));
		list->PathStroke(colour, ImDrawFlags_None, w);
		break;
	case Icon::FOLDER:
		list->PathLineTo(p(3, 7));
		arc(5, 7, 2, 180, 270);
		list->PathLineTo(p(9, 5));
		list->PathLineTo(p(11, 7));
		arc(19, 9, 2, 270, 360);
		arc(19, 17, 2, 0, 90);
		arc(5, 17, 2, 90, 180);
		list->PathStroke(colour, ImDrawFlags_Closed, w);
		break;
	case Icon::ABOUT:
		list->AddCircle(p(12, 12), 9.0f * k, colour, 0, w);
		list->AddLine(p(12, 11), p(12, 17), colour, w);
		list->AddLine(p(12, 7.5f), p(12, 8.0f), colour, w);
		break;
	}
}

bool
MenuKit::tabs(std::span<const char *const> names, std::span<const Icon> icons, bool narrow, int &chosen) const
{
	ImDrawList *const list = ImGui::GetWindowDrawList();
	const ImVec2 at = ImGui::GetCursorScreenPos();
	const float width = ImGui::GetContentRegionAvail().x;
	const float wide = px(10.0f), high = px(CELL_HIGH + 1.0f), gap = px(6.0f), line = ImGui::GetTextLineHeight();
	const float h = line + 2.0f * high, side = line, space = px(6.0f);
	const int was = chosen;
	const auto shows = [&](size_t i) { return !narrow || static_cast<int>(i) == chosen; };
	const auto tab_w = [&](size_t i) {
		return side + (shows(i) ? space + ImGui::CalcTextSize(names[i]).x : 0.0f) + 2.0f * wide;
	};
	float total = -gap;

	for (size_t i = 0; i < names.size(); i++)
		total += tab_w(i) + gap;

	float x = at.x + std::max(0.0f, (width - total) / 2.0f);

	for (size_t i = 0; i < names.size(); i++) {
		const float w = tab_w(i);
		const ImVec2 a = { x, at.y }, b = { x + w, at.y + h };

		ImGui::SetCursorScreenPos(a);
		if (ImGui::InvisibleButton(names[i], { w, h }))
			chosen = static_cast<int>(i);

		const bool on = chosen == static_cast<int>(i), hovered = ImGui::IsItemHovered();
		const ImU32 colour = on ? m_palette.action : hovered ? m_palette.ink : m_palette.dim;

		if (on)
			frame(list, a, b, px(CONTROL_CUT), 0, m_palette.action);
		else if (hovered)
			frame(list, a, b, px(CONTROL_CUT), m_palette.hover, 0);
		if (i < icons.size())
			icon(list, icons[i], { a.x + wide, a.y + high }, side, colour);
		if (shows(i))
			list->AddText({ a.x + wide + side + space, a.y + high }, colour, names[i]);
		x += w + gap;
	}
	ImGui::SetCursorScreenPos(at);
	ImGui::Dummy({ width, h });
	return chosen != was;
}

void
MenuKit::begin_group(const char *title)
{
	ImDrawList *const list = ImGui::GetWindowDrawList();

	ImGui::Dummy({ 0.0f, px(10.0f) });
	list->AddText(ImGui::GetCursorScreenPos(), m_palette.dim, title);
	ImGui::Dummy({ 0.0f, ImGui::GetTextLineHeight() + px(4.0f) });
	m_group_top = ImGui::GetCursorScreenPos();
	m_first_row = true;
	/* The rows on top, the frame drawn under them once their height is known */
	list->ChannelsSplit(2);
	list->ChannelsSetCurrent(1);
}

void
MenuKit::end_group()
{
	ImDrawList *const list = ImGui::GetWindowDrawList();
	const ImVec2 end = ImGui::GetCursorScreenPos();

	list->ChannelsSetCurrent(0);
	frame(list, m_group_top, { m_group_top.x + ImGui::GetContentRegionAvail().x, end.y }, px(GROUP_CUT),
	      m_palette.group, m_palette.line);
	list->ChannelsMerge();
}

ImVec2
MenuKit::begin_row(float &width)
{
	const ImVec2 at = ImGui::GetCursorScreenPos();

	width = ImGui::GetContentRegionAvail().x;
	if (!m_first_row)
		ImGui::GetWindowDrawList()->AddLine({ at.x + px(ROW_PAD), at.y }, { at.x + width - px(ROW_PAD), at.y },
		                                    m_palette.line, 1.0f);
	m_first_row = false;
	return at;
}

/* The name, what the row does and why it does not change, one under the
 * other in that width: their height */
float
MenuKit::texts(ImVec2 at, float width, const char *name, const char *about, const char *locked) const
{
	ImDrawList *const list = ImGui::GetWindowDrawList();
	ImFont *const font = ImGui::GetFont();
	const float size = ImGui::GetFontSize(), space = px(2.0f);
	float h = ImGui::CalcTextSize(name, nullptr, false, width).y;

	list->AddText(font, size, at, m_palette.ink, name, nullptr, width);
	for (const auto &[text, colour] : { std::pair{ about, m_palette.dim }, std::pair{ locked, m_palette.faint } }) {
		if (!text || !*text)
			continue;
		list->AddText(font, size, { at.x, at.y + h + space }, colour, text, nullptr, width);
		h += space + ImGui::CalcTextSize(text, nullptr, false, width).y;
	}
	return h;
}

MenuKit::Placed
MenuKit::place(ImVec2 at, float width, float control, float control_h, const char *name, const char *about,
               const char *locked) const
{
	const float pad = px(ROW_PAD), beside = width - control - px(GAP) - 2.0f * pad;
	const bool under = beside < px(TEXT_LEAST);
	const float text_h = texts({ at.x + pad, at.y + pad }, under ? width - 2.0f * pad : beside, name, about, locked);

	if (under)
		return { text_h + px(6.0f) + control_h + 2.0f * pad, at.y + pad + text_h + px(6.0f) };

	const float h = std::max(text_h, control_h) + 2.0f * pad;

	return { h, at.y + (h - control_h) / 2.0f };
}

bool
MenuKit::choice(const char *name, const char *about, std::span<const char *const> labels, int &chosen,
                const char *locked, bool off)
{
	IM_ASSERT(!labels.empty() && labels.size() <= MOST_LABELS);
	ImGui::PushID(name);

	ImDrawList *const list = ImGui::GetWindowDrawList();
	const bool still = locked || off;
	const float pad = px(ROW_PAD), wide = px(CELL_WIDE), high = px(CELL_HIGH);
	const float control_h = ImGui::GetTextLineHeight() + 2.0f * high;
	std::array<float, MOST_LABELS> cells;
	float control = 0.0f, width;

	for (size_t i = 0; i < labels.size(); i++)
		control += cells[i] = ImGui::CalcTextSize(labels[i]).x + 2.0f * wide;

	const ImVec2 at = begin_row(width);
	const Placed placed = place(at, width, control, control_h, name, about, locked);
	const float h = placed.h;
	const ImVec2 a = { at.x + width - pad - control, placed.top };
	const int was = chosen;
	float x = a.x;

	ImGui::BeginDisabled(still);
	for (size_t i = 0; i < labels.size(); i++) {
		const ImVec2 ca = { x, a.y }, cb = { x + cells[i], a.y + control_h };

		ImGui::SetCursorScreenPos(ca);
		if (ImGui::InvisibleButton(labels[i], { cells[i], control_h }))
			chosen = static_cast<int>(i);

		const bool on = chosen == static_cast<int>(i), hovered = !still && ImGui::IsItemHovered();
		const ImVec2 size = ImGui::CalcTextSize(labels[i]);

		if (on || hovered)
			fill_cell(list, ca, cb, px(CONTROL_CUT), i == 0, i + 1 == labels.size(),
			          faded(on ? m_palette.action : m_palette.hover, still));
		if (i > 0)
			list->AddLine(ca, { x, cb.y }, faded(m_palette.outline, still), 1.0f);
		list->AddText({ x + (cells[i] - size.x) / 2.0f, a.y + high },
		              faded(on ? m_palette.on_action : hovered ? m_palette.ink : m_palette.dim, still), labels[i]);
		x += cells[i];
	}
	ImGui::EndDisabled();
	frame(list, a, { a.x + control, a.y + control_h }, px(CONTROL_CUT), 0, faded(m_palette.outline, still));

	ImGui::SetCursorScreenPos(at);
	ImGui::Dummy({ width, h });
	ImGui::PopID();
	return chosen != was;
}

bool
MenuKit::dropdown(const char *name, const char *about, std::span<const std::string> labels, int &chosen)
{
	ImGui::PushID(name);

	const float pad = px(ROW_PAD), wide = px(CELL_WIDE), arrow = ImGui::CalcTextSize("v").x;
	const float control_h = picker_height();
	float longest = 0.0f, width;

	for (const std::string &label : labels)
		longest = std::max(longest, ImGui::CalcTextSize(label.c_str()).x);

	const ImVec2 at = begin_row(width);
	const float control = std::min(longest + 3.0f * wide + arrow, width / 2.0f);
	const Placed placed = place(at, width, control, control_h, name, about, nullptr);
	const bool changed = picker("box", { at.x + width - pad - control, placed.top }, control, labels, chosen);

	ImGui::SetCursorScreenPos(at);
	ImGui::Dummy({ width, placed.h });
	ImGui::PopID();
	return changed;
}

float
MenuKit::picker_height() const
{
	return ImGui::GetTextLineHeight() + 2.0f * px(CELL_HIGH);
}

bool
MenuKit::picker(const char *id, ImVec2 at, float width, std::span<const std::string> labels, int &chosen,
                const Icon *icon_shown) const
{
	ImGui::PushID(id);

	ImDrawList *const list = ImGui::GetWindowDrawList();
	const MenuPalette &p = m_palette;
	const float wide = px(CELL_WIDE), high = px(CELL_HIGH), arrow = ImGui::CalcTextSize("v").x;
	const ImVec2 b = { at.x + width, at.y + picker_height() };
	const int was = chosen;

	ImGui::SetCursorScreenPos(at);
	if (ImGui::InvisibleButton("open", { width, b.y - at.y }))
		ImGui::OpenPopup("list");
	frame(list, at, b, px(CONTROL_CUT), ImGui::IsItemHovered() ? p.hover : 0, p.outline);
	if (chosen >= 0 && static_cast<size_t>(chosen) < labels.size()) {
		const float side = ImGui::GetTextLineHeight(), before = icon_shown ? side + px(6.0f) : 0.0f;

		if (icon_shown)
			icon(list, *icon_shown, { at.x + wide, at.y + high }, side, p.dim);
		list->PushClipRect(at, { b.x - wide - arrow, b.y }, true);
		list->AddText({ at.x + wide + before, at.y + high }, p.ink, labels[static_cast<size_t>(chosen)].c_str());
		list->PopClipRect();
	}
	list->AddText({ b.x - wide - arrow, at.y + high }, p.dim, "v");

	list_popup({ at.x, b.y + px(2.0f) }, width, labels, chosen);
	ImGui::PopID();
	return chosen != was;
}

bool
MenuKit::list_popup(ImVec2 at, float width, std::span<const std::string> labels, int &chosen) const
{
	const MenuPalette &p = m_palette;
	const int was = chosen;
	const float room = ImGui::GetIO().DisplaySize.x, margin = px(8.0f);
	float longest = width;

	/* As wide as its longest label, moved left where it would leave the
	 * window */
	for (const std::string &label : labels)
		longest = std::max(longest, ImGui::CalcTextSize(label.c_str()).x + 2.0f * px(CELL_WIDE));
	longest = std::min(longest, room - 2.0f * margin);
	ImGui::SetNextWindowPos({ std::max(margin, std::min(at.x, room - margin - longest)), at.y });
	ImGui::SetNextWindowSizeConstraints({ longest, 0.0f }, { longest, ImGui::GetIO().DisplaySize.y });
	ImGui::PushStyleColor(ImGuiCol_PopupBg, ImGui::ColorConvertU32ToFloat4(p.screen));
	ImGui::PushStyleColor(ImGuiCol_Border, ImGui::ColorConvertU32ToFloat4(p.faint));
	ImGui::PushStyleColor(ImGuiCol_Header, ImGui::ColorConvertU32ToFloat4(p.action));
	ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ImGui::ColorConvertU32ToFloat4(p.line));
	ImGui::PushStyleColor(ImGuiCol_HeaderActive, ImGui::ColorConvertU32ToFloat4(p.action));
	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, { px(CELL_WIDE), px(CELL_HIGH) });
	ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, { 0.0f, px(2.0f) });
	ImGui::PushStyleVar(ImGuiStyleVar_PopupBorderSize, 1.0f);
	ImGui::PushStyleVar(ImGuiStyleVar_PopupRounding, 0.0f);
	if (ImGui::BeginPopup("list")) {
		for (size_t i = 0; i < labels.size(); i++) {
			const bool on = static_cast<int>(i) == chosen;

			ImGui::PushStyleColor(ImGuiCol_Text, ImGui::ColorConvertU32ToFloat4(on ? p.on_action : p.ink));
			if (ImGui::Selectable(labels[i].c_str(), on))
				chosen = static_cast<int>(i);
			ImGui::PopStyleColor();
		}
		ImGui::EndPopup();
	}
	ImGui::PopStyleVar(4);
	ImGui::PopStyleColor(5);
	return chosen != was;
}

bool
MenuKit::binding(const char *id, ImVec2 at, float width, bool first, const char *name, const char *inputs, bool lit,
                 bool catching, float &h) const
{
	ImDrawList *const list = ImGui::GetWindowDrawList();
	const float pad = px(ROW_PAD), line = ImGui::GetTextLineHeight(), gap = px(GAP);
	const char *const shown = catching ? "press..." : (inputs && *inputs) ? inputs : "-";
	const float shown_w = std::min(ImGui::CalcTextSize(shown).x, width / 2.0f);
	const float name_w = std::max(px(40.0f), width - shown_w - gap - 2.0f * pad);
	const float name_h = ImGui::CalcTextSize(name, nullptr, false, name_w).y;
	const float shown_h = ImGui::CalcTextSize(shown, nullptr, false, shown_w).y;

	h = std::max({ name_h, shown_h, line }) + 2.0f * pad;
	ImGui::SetCursorScreenPos(at);

	const bool clicked = ImGui::InvisibleButton(id, { width, h });
	const bool hovered = ImGui::IsItemHovered();
	const ImVec2 b = { at.x + width, at.y + h };

	if (catching)
		frame(list, { at.x + 1.0f, at.y + 1.0f }, { b.x - 1.0f, b.y - 1.0f }, px(CONTROL_CUT), m_palette.hover,
		      m_palette.accent);
	else if (hovered)
		frame(list, at, b, px(CONTROL_CUT), m_palette.hover, 0);
	if (!first && !catching)
		list->AddLine({ at.x + pad, at.y }, { b.x - pad, at.y }, m_palette.line, 1.0f);
	if (lit)
		list->AddRectFilled({ at.x + px(2.0f), at.y + pad }, { at.x + px(4.0f), b.y - pad }, m_palette.accent);
	list->AddText(ImGui::GetFont(), ImGui::GetFontSize(), { at.x + pad, at.y + (h - name_h) / 2.0f },
	              lit ? m_palette.accent : m_palette.ink, name, nullptr, name_w);
	list->AddText(ImGui::GetFont(), ImGui::GetFontSize(), { b.x - pad - shown_w, at.y + (h - shown_h) / 2.0f },
	              catching ? m_palette.accent : (inputs && *inputs) ? m_palette.dim : m_palette.faint, shown,
	              nullptr, shown_w);
	return clicked;
}

bool
MenuKit::button(const char *label, bool same_line) const
{
	ImDrawList *const list = ImGui::GetWindowDrawList();
	const float wide = px(CELL_WIDE + 3.0f), high = px(CELL_HIGH + 1.0f);
	const ImVec2 size = ImGui::CalcTextSize(label);
	const ImVec2 box = { size.x + 2.0f * wide, size.y + 2.0f * high };

	if (same_line)
		ImGui::SameLine(0.0f, px(8.0f));

	const ImVec2 a = ImGui::GetCursorScreenPos(), b = { a.x + box.x, a.y + box.y };
	const bool clicked = ImGui::InvisibleButton(label, box);
	const bool hovered = ImGui::IsItemHovered();

	frame(list, a, b, px(CONTROL_CUT), hovered ? m_palette.hover : 0, m_palette.outline);
	list->AddText({ a.x + wide, a.y + high }, hovered ? m_palette.ink : m_palette.dim, label);
	return clicked;
}

ImVec2
MenuKit::button_size(const char *label) const
{
	const ImVec2 size = ImGui::CalcTextSize(label);

	return { size.x + 2.0f * px(CELL_WIDE + 3.0f), size.y + 2.0f * px(CELL_HIGH + 1.0f) };
}

bool
MenuKit::button(const char *label, ImVec2 at, bool on) const
{
	ImDrawList *const list = ImGui::GetWindowDrawList();
	const ImVec2 box = button_size(label), b = { at.x + box.x, at.y + box.y };

	ImGui::SetCursorScreenPos(at);

	const bool clicked = ImGui::InvisibleButton(label, box);
	const bool hovered = ImGui::IsItemHovered();

	frame(list, at, b, px(CONTROL_CUT), hovered ? m_palette.hover : 0, on ? m_palette.action : m_palette.outline);
	list->AddText({ at.x + px(CELL_WIDE + 3.0f), at.y + px(CELL_HIGH + 1.0f) },
	              on ? m_palette.action : hovered ? m_palette.ink : m_palette.dim, label);
	return clicked;
}

void
MenuKit::text(const char *name, const char *about, const char *value)
{
	this->value(name, about, value, nullptr);
}

bool
MenuKit::value(const char *name, const char *about, const char *value, const char *action, const char *word,
               const char *locked)
{
	ImGui::PushID(name);

	ImDrawList *const list = ImGui::GetWindowDrawList();
	const float pad = px(ROW_PAD), space = px(8.0f), small = ImGui::GetFontSize() * 0.8f;
	const char *const shown_button = locked ? nullptr : action;
	const char *const shown_word = shown_button ? nullptr : word;
	ImFont *const font = ImGui::GetFont();
	const ImVec2 button_box = shown_button ? button_size(shown_button) : ImVec2{};
	const float word_w = shown_word ? font->CalcTextSizeA(small, FLT_MAX, 0.0f, shown_word).x : 0.0f;
	const float after = shown_button ? button_box.x + space : shown_word ? word_w + space : 0.0f;
	float width;
	const ImVec2 at = begin_row(width);
	/* A long value takes half the row at most, on more lines; the whole
	 * row under the texts, where they have not room enough */
	const float wide_w = ImGui::CalcTextSize(value).x;
	const bool under = width - std::min(wide_w, width / 2.0f - after) - after - px(GAP) - 2.0f * pad < px(TEXT_LEAST);
	const float value_w = std::max(px(20.0f), std::min(wide_w, (under ? width - 2.0f * pad : width / 2.0f) - after));
	const ImVec2 value_size = ImGui::CalcTextSize(value, nullptr, false, value_w);
	const float control_h = std::max(value_size.y, button_box.y);
	const Placed placed = place(at, width, value_w + after, control_h, name, about, locked);
	const float h = placed.h, mid = placed.top + control_h / 2.0f;
	const float right = at.x + width - pad;
	bool clicked = false;

	list->AddText(font, ImGui::GetFontSize(), { right - after - value_w, mid - value_size.y / 2.0f }, m_palette.dim,
	              value, nullptr, value_w);
	if (shown_button) {
		m_button_a = { right - button_box.x, mid - button_box.y / 2.0f };
		m_button_b = { m_button_a.x + button_box.x, m_button_a.y + button_box.y };
		clicked = button(shown_button, m_button_a, false);
	} else if (shown_word)
		list->AddText(font, small, { right - word_w, mid - small / 2.0f }, m_palette.faint, shown_word);
	ImGui::SetCursorScreenPos(at);
	ImGui::Dummy({ width, h });
	ImGui::PopID();
	return clicked;
}

bool
MenuKit::value_list(const char *name, const char *about, const char *value, const char *action,
                    std::span<const std::string> labels, int &chosen, const char *locked)
{
	const int was = chosen;

	ImGui::PushID(name);
	if (this->value(name, about, value, action, nullptr, locked))
		ImGui::OpenPopup("list");
	list_popup({ m_button_a.x, m_button_b.y + px(2.0f) }, m_button_b.x - m_button_a.x, labels, chosen);
	ImGui::PopID();
	return chosen != was;
}

bool
MenuKit::slider(const char *name, const char *about, float &value, float low, float high, float step, const char *shown,
                const char *locked, bool off)
{
	ImGui::PushID(name);

	ImDrawList *const list = ImGui::GetWindowDrawList();
	const bool still = locked || off;
	const float pad = px(ROW_PAD), track_w = px(64.0f), track_h = px(6.0f), space = px(8.0f);
	const float shown_w = std::max(ImGui::CalcTextSize(shown).x, ImGui::CalcTextSize("-60 dB").x);
	const float control = track_w + space + shown_w, line = ImGui::GetTextLineHeight();
	float width;
	const ImVec2 at = begin_row(width);
	const Placed placed = place(at, width, control, line, name, about, locked);
	const float h = placed.h, mid = placed.top + line / 2.0f;
	const ImVec2 a = { at.x + width - pad - control, mid - track_h / 2.0f }, b = { a.x + track_w, a.y + track_h };
	const float was = value;

	/* The track takes the mouse a little above and below it */
	ImGui::SetCursorScreenPos({ a.x, placed.top });
	ImGui::BeginDisabled(still);
	ImGui::InvisibleButton("track", { track_w, line });

	const bool hovered = !still && ImGui::IsItemHovered();

	if (!still && ImGui::IsItemActive()) {
		const float t = std::clamp((ImGui::GetIO().MousePos.x - a.x) / track_w, 0.0f, 1.0f);

		value = low + std::round(t * (high - low) / step) * step;
	} else if (hovered && ImGui::GetIO().MouseWheel != 0.0f) {
		value = std::clamp(value + (ImGui::GetIO().MouseWheel > 0.0f ? step : -step), low, high);
	}
	ImGui::EndDisabled();

	const float fill = (std::clamp(value, low, high) - low) / (high - low);
	const float r = track_h / 2.0f;

	list->AddRectFilled(a, { a.x + track_w * fill, b.y }, faded(m_palette.action, still), r);
	list->AddRect(a, b, faded(hovered ? m_palette.dim : m_palette.outline, still), r);
	list->AddText({ b.x + space, placed.top }, faded(m_palette.ink, still), shown);

	ImGui::SetCursorScreenPos(at);
	ImGui::Dummy({ width, h });
	ImGui::PopID();
	return value != was;
}

ImVec2
MenuKit::key_size(const char *name) const
{
	const float h = ImGui::GetTextLineHeight() + 2.0f * px(3.0f);

	return { std::max(h, ImGui::CalcTextSize(name).x + 2.0f * px(5.0f)), h };
}

void
MenuKit::key(ImDrawList *list, ImVec2 at, const char *name, bool used, bool lit) const
{
	const ImVec2 size = key_size(name), b = { at.x + size.x, at.y + size.y };
	const float w = ImGui::CalcTextSize(name).x;

	frame(list, at, b, px(CONTROL_CUT), lit ? m_palette.accent : m_palette.group,
	      lit ? m_palette.accent : used ? m_palette.dim : m_palette.outline);
	list->AddText({ at.x + (size.x - w) / 2.0f, at.y + px(3.0f) },
	              lit ? m_palette.on_accent : used ? m_palette.ink : m_palette.faint, name);
}

ImVec2
MenuKit::pill_size(const char *name) const
{
	const float h = ImGui::GetTextLineHeight() + 2.0f * px(3.0f);

	return { std::max(h, ImGui::CalcTextSize(name).x + 2.0f * px(7.0f)), h };
}

void
MenuKit::pill(ImDrawList *list, ImVec2 at, const char *name) const
{
	const ImVec2 size = pill_size(name);
	const float w = ImGui::CalcTextSize(name).x;

	list->AddRect(at, { at.x + size.x, at.y + size.y }, m_palette.outline, size.y / 2.0f);
	list->AddText({ at.x + (size.x - w) / 2.0f, at.y + px(3.0f) }, m_palette.ink, name);
}

int
MenuKit::shortcut(const char *name, std::span<const std::string> keys, std::span<const std::string> pad, int catching)
{
	ImGui::PushID(name);

	ImDrawList *const list = ImGui::GetWindowDrawList();
	const char press[] = "press...", none[] = "-";
	const float pad_room = px(ROW_PAD), gap = px(4.0f), plus = ImGui::CalcTextSize("+").x;
	const float line = ImGui::GetTextLineHeight(), cap_h = key_size("x").y;
	/* The keys, then the buttons as wide as "Guide + Back" at the least */
	const auto width_of = [&](size_t i) {
		return static_cast<int>(i) == catching ? ImGui::CalcTextSize(press).x + 2.0f * px(5.0f) :
		                                         key_size(keys[i].c_str()).x;
	};
	float keys_w = 0.0f, pad_w = 0.0f;

	for (size_t i = 0; i < keys.size(); i++)
		keys_w += width_of(i) + (i ? gap : 0.0f);
	if (catching == 2)
		pad_w = ImGui::CalcTextSize(press).x + 2.0f * px(7.0f);
	else if (pad.empty())
		pad_w = ImGui::CalcTextSize(none).x;
	else
		for (size_t i = 0; i < pad.size(); i++)
			pad_w += pill_size(pad[i].c_str()).x + (i ? 2.0f * gap + plus : 0.0f);
	pad_w = std::max(pad_w, pill_size("Guide").x + pill_size("Back").x + 2.0f * gap + plus);

	float width;
	const ImVec2 at = begin_row(width);
	const float control = keys_w + px(GAP) + pad_w;
	/* Where the name has not room enough beside them, they go under it */
	const bool under = width - control - px(GAP) - 2.0f * pad_room < px(120.0f);
	const float text_w = under ? width - 2.0f * pad_room : width - control - px(GAP) - 2.0f * pad_room;
	const float text_h = texts({ at.x + pad_room, at.y + pad_room }, text_w, name, nullptr, nullptr);
	const float h = (under ? text_h + px(6.0f) + cap_h : std::max(text_h, cap_h)) + 2.0f * pad_room;
	const float y = under ? at.y + pad_room + text_h + px(6.0f) : at.y + (h - cap_h) / 2.0f;
	float x = at.x + width - pad_room - control;
	int clicked = -1;

	for (size_t i = 0; i < keys.size(); i++) {
		const float w = width_of(i);

		ImGui::SetCursorScreenPos({ x, y });
		ImGui::PushID(static_cast<int>(i));
		if (ImGui::InvisibleButton("key", { w, cap_h }))
			clicked = static_cast<int>(i);
		ImGui::PopID();
		if (static_cast<int>(i) == catching) {
			frame(list, { x, y }, { x + w, y + cap_h }, px(CONTROL_CUT), m_palette.hover, m_palette.accent);
			list->AddText({ x + px(5.0f), y + px(3.0f) }, m_palette.accent, press);
		} else {
			key(list, { x, y }, keys[i].c_str(), true, false);
			if (ImGui::IsItemHovered())
				frame(list, { x, y }, { x + w, y + cap_h }, px(CONTROL_CUT), m_palette.hover, 0);
		}
		x += w + gap;
	}

	const float pad_x = at.x + width - pad_room - pad_w;

	ImGui::SetCursorScreenPos({ pad_x, y });
	if (ImGui::InvisibleButton("pad", { pad_w, cap_h }))
		clicked = 2;
	if (catching == 2) {
		const float w = ImGui::CalcTextSize(press).x + 2.0f * px(7.0f);

		list->AddRect({ pad_x + pad_w - w, y }, { pad_x + pad_w, y + cap_h }, m_palette.accent, cap_h / 2.0f);
		list->AddText({ pad_x + pad_w - w + px(7.0f), y + px(3.0f) }, m_palette.accent, press);
	} else {
		if (ImGui::IsItemHovered())
			list->AddRectFilled({ pad_x, y }, { pad_x + pad_w, y + cap_h }, m_palette.hover, cap_h / 2.0f);
		float right = pad_x + pad_w;

		if (pad.empty())
			list->AddText({ right - ImGui::CalcTextSize(none).x, y + (cap_h - line) / 2.0f }, m_palette.faint, none);
		for (size_t i = pad.size(); i-- > 0;) {
			right -= pill_size(pad[i].c_str()).x;
			pill(list, { right, y }, pad[i].c_str());
			if (i) {
				right -= gap + plus;
				list->AddText({ right, y + (cap_h - line) / 2.0f }, m_palette.faint, "+");
				right -= gap;
			}
		}
	}
	ImGui::SetCursorScreenPos(at);
	ImGui::Dummy({ width, h });
	ImGui::PopID();
	return clicked;
}

void
MenuKit::paragraph(const char *title, const char *text)
{
	ImDrawList *const list = ImGui::GetWindowDrawList();
	const float width = ImGui::GetContentRegionAvail().x;

	ImGui::Dummy({ 0.0f, px(10.0f) });
	list->AddText(ImGui::GetCursorScreenPos(), m_palette.dim, title);
	ImGui::Dummy({ 0.0f, ImGui::GetTextLineHeight() + px(4.0f) });
	list->AddText(ImGui::GetFont(), ImGui::GetFontSize(), ImGui::GetCursorScreenPos(), m_palette.faint, text, nullptr,
	              width);
	ImGui::Dummy({ 0.0f, ImGui::CalcTextSize(text, nullptr, false, width).y });
}

} /* namespace odin */
