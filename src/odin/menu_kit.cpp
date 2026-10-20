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

bool
MenuKit::tabs(std::span<const char *const> names, int &chosen) const
{
	ImDrawList *const list = ImGui::GetWindowDrawList();
	const ImVec2 at = ImGui::GetCursorScreenPos();
	const float width = ImGui::GetContentRegionAvail().x;
	const float wide = px(10.0f), high = px(CELL_HIGH + 1.0f), gap = px(6.0f);
	const float h = ImGui::GetTextLineHeight() + 2.0f * high;
	const int was = chosen;
	float total = 0.0f;

	for (const char *const name : names)
		total += ImGui::CalcTextSize(name).x + 2.0f * wide + gap;
	total -= gap;

	float x = at.x + std::max(0.0f, (width - total) / 2.0f);

	for (size_t i = 0; i < names.size(); i++) {
		const float w = ImGui::CalcTextSize(names[i]).x + 2.0f * wide;
		const ImVec2 a = { x, at.y }, b = { x + w, at.y + h };

		ImGui::SetCursorScreenPos(a);
		if (ImGui::InvisibleButton(names[i], { w, h }))
			chosen = static_cast<int>(i);

		const bool on = chosen == static_cast<int>(i), hovered = ImGui::IsItemHovered();

		if (on)
			frame(list, a, b, px(CONTROL_CUT), 0, m_palette.action);
		else if (hovered)
			frame(list, a, b, px(CONTROL_CUT), m_palette.hover, 0);
		list->AddText({ a.x + wide, a.y + high }, on ? m_palette.action : hovered ? m_palette.ink : m_palette.dim,
		              names[i]);
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
	float h = ImGui::GetTextLineHeight();

	list->AddText(at, m_palette.ink, name);
	for (const auto &[text, colour] : { std::pair{ about, m_palette.dim }, std::pair{ locked, m_palette.faint } }) {
		if (!text || !*text)
			continue;
		list->AddText(font, size, { at.x, at.y + h + space }, colour, text, nullptr, width);
		h += space + ImGui::CalcTextSize(text, nullptr, false, width).y;
	}
	return h;
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
	const float text_w = std::max(px(80.0f), width - control - px(GAP) - 2.0f * pad);
	const float h = std::max(texts({ at.x + pad, at.y + pad }, text_w, name, about, locked), control_h) + 2.0f * pad;
	const ImVec2 a = { at.x + width - pad - control, at.y + (h - control_h) / 2.0f };
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

void
MenuKit::text(const char *name, const char *about, const char *value)
{
	ImDrawList *const list = ImGui::GetWindowDrawList();
	const float pad = px(ROW_PAD);
	float width;
	const ImVec2 at = begin_row(width);
	/* A long value takes half the row at most, on more lines */
	const float value_w = std::min(ImGui::CalcTextSize(value).x, width / 2.0f);
	const ImVec2 value_size = ImGui::CalcTextSize(value, nullptr, false, value_w);
	const float text_w = std::max(px(80.0f), width - value_w - px(GAP) - 2.0f * pad);
	const float h = std::max(texts({ at.x + pad, at.y + pad }, text_w, name, about, nullptr), value_size.y) +
	                2.0f * pad;

	list->AddText(ImGui::GetFont(), ImGui::GetFontSize(),
	              { at.x + width - pad - value_w, at.y + (h - value_size.y) / 2.0f }, m_palette.dim, value, nullptr,
	              value_w);
	ImGui::SetCursorScreenPos(at);
	ImGui::Dummy({ width, h });
}

} /* namespace odin */
