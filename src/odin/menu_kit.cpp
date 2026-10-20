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
 * control and in a button, in points */
constexpr float GROUP_CUT = 5.0f;
constexpr float CONTROL_CUT = 3.0f;
constexpr float ROW_PAD = 8.0f;
constexpr float GAP = 14.0f;
constexpr float CELL_WIDE = 7.0f;
constexpr float CELL_HIGH = 3.0f;
constexpr float BUTTON_WIDE = 8.0f;
constexpr float BUTTON_HIGH = 4.0f;
/* The texts of a row have that room at the least beside its control, else
 * the control goes under them */
constexpr float TEXT_LEAST = 120.0f;
/* What is left of a colour on a row that cannot change; of the action
 * colour on the outline of what is chosen, and in its fill */
constexpr float OFF_ALPHA = 0.4f;
constexpr float CHOSEN_LINE = 0.6f;
constexpr float CHOSEN_FILL = 0.15f;
/* The letters of a title in small capitals that far apart, in ems */
constexpr float TITLE_SPACING = 0.12f;

/* The most labels a choice has */
constexpr size_t MOST_LABELS = 8;

ImU32
faded(ImU32 colour, bool off)
{
	return off ? MenuKit::alpha(colour, OFF_ALPHA) : colour;
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

/* The height of a text at that size, on as many lines as that width takes */
float
wrapped_h(const char *text, float size, float width)
{
	return ImGui::GetFont()->CalcTextSizeA(size, FLT_MAX, width, text).y;
}

/* The character at p (UTF-8) and its length in bytes */
unsigned
decode(const char *p, int &length)
{
	const auto c = static_cast<unsigned char>(p[0]);

	length = 1;
	if (c < 0x80)
		return c;

	const int n = c >= 0xF0 ? 4 : c >= 0xE0 ? 3 : c >= 0xC0 ? 2 : 1;
	unsigned code = c & (0x7Fu >> n);

	for (; length < n && (static_cast<unsigned char>(p[length]) & 0xC0) == 0x80; length++)
		code = code << 6 | (static_cast<unsigned char>(p[length]) & 0x3F);
	return code;
}

/* The signs the font has not, and their widths in ems */
struct Sign {
	unsigned code;
	float em;
};

constexpr Sign SIGNS[] = {
	{ 0x25B2, 0.75f }, { 0x25BC, 0.75f }, { 0x25C0, 0.7f }, { 0x25B6, 0.7f }, { 0x25BE, 0.6f },
	{ 0x2190, 0.9f }, { 0x2192, 0.9f }, { 0x2191, 0.6f }, { 0x2193, 0.6f }, { 0x2194, 1.1f }, { 0x2195, 0.6f },
	{ 0x23CE, 0.9f }, { 0x2715, 0.85f }, { 0x25CB, 0.85f }, { 0x25A1, 0.85f }, { 0x25B3, 0.85f },
	{ 0x2039, 0.5f }, { 0x203A, 0.5f }, { 0x2212, 0.6f },
};

const Sign *
sign_of(unsigned code)
{
	for (const Sign &sign : SIGNS)
		if (sign.code == code)
			return &sign;
	return nullptr;
}

/* A sign in its box from x, a line of that size high from y */
void
draw_sign(ImDrawList *list, const Sign &sign, float x, float y, float size, ImU32 colour)
{
	const float e = size, w = std::max(1.0f, e * 0.09f), adv = sign.em * e, cx = x + adv / 2.0f, cy = y + e * 0.52f;
	const float h = e * 0.26f, l = x + e * 0.1f, r = x + adv - e * 0.1f, head = e * 0.2f;
	const auto line = [&](ImVec2 p, ImVec2 q) { list->AddLine(p, q, colour, w); };
	/* An arrow head at the tip, pointing along (dx, dy) */
	const auto tip = [&](ImVec2 t, float dx, float dy) {
		line(t, { t.x - dx * head + dy * head, t.y - dy * head - dx * head });
		line(t, { t.x - dx * head - dy * head, t.y - dy * head + dx * head });
	};

	switch (sign.code) {
	case 0x25B2:
		list->AddTriangleFilled({ cx, cy - h }, { cx + h * 1.15f, cy + h * 0.85f }, { cx - h * 1.15f, cy + h * 0.85f },
		                        colour);
		break;
	case 0x25BC:
		list->AddTriangleFilled({ cx - h * 1.15f, cy - h * 0.85f }, { cx + h * 1.15f, cy - h * 0.85f }, { cx, cy + h },
		                        colour);
		break;
	case 0x25B6:
		list->AddTriangleFilled({ cx - h * 0.85f, cy - h * 1.15f }, { cx + h, cy }, { cx - h * 0.85f, cy + h * 1.15f },
		                        colour);
		break;
	case 0x25C0:
		list->AddTriangleFilled({ cx + h * 0.85f, cy + h * 1.15f }, { cx - h, cy }, { cx + h * 0.85f, cy - h * 1.15f },
		                        colour);
		break;
	case 0x25BE: {
		const float s = e * 0.18f;

		list->AddTriangleFilled({ cx - s * 1.2f, cy - s * 0.6f }, { cx + s * 1.2f, cy - s * 0.6f }, { cx, cy + s * 0.9f },
		                        colour);
		break;
	}
	case 0x2190:
	case 0x2192:
	case 0x2194: {
		line({ l, cy }, { r, cy });
		if (sign.code != 0x2190)
			tip({ r, cy }, 1.0f, 0.0f);
		if (sign.code != 0x2192)
			tip({ l, cy }, -1.0f, 0.0f);
		break;
	}
	case 0x2191:
	case 0x2193:
	case 0x2195: {
		const float top = cy - e * 0.32f, bottom = cy + e * 0.32f;

		line({ cx, top }, { cx, bottom });
		if (sign.code != 0x2193)
			tip({ cx, top }, 0.0f, -1.0f);
		if (sign.code != 0x2191)
			tip({ cx, bottom }, 0.0f, 1.0f);
		break;
	}
	case 0x23CE: {
		const ImVec2 corner = { r - e * 0.05f, cy + e * 0.12f }, end = { l + e * 0.05f, cy + e * 0.12f };

		line({ corner.x, cy - e * 0.32f }, corner);
		line(corner, end);
		tip(end, -1.0f, 0.0f);
		break;
	}
	case 0x2715: {
		const float m = e * 0.27f;

		line({ cx - m, cy - m }, { cx + m, cy + m });
		line({ cx + m, cy - m }, { cx - m, cy + m });
		break;
	}
	case 0x25CB:
		list->AddCircle({ cx, cy }, e * 0.3f, colour, 0, w);
		break;
	case 0x25A1:
		list->AddRect({ cx - e * 0.27f, cy - e * 0.27f }, { cx + e * 0.27f, cy + e * 0.27f }, colour, 0.0f,
		              ImDrawFlags_None, w);
		break;
	case 0x25B3:
		list->AddTriangle({ cx, cy - e * 0.3f }, { cx + e * 0.32f, cy + e * 0.25f }, { cx - e * 0.32f, cy + e * 0.25f },
		                  colour, w);
		break;
	case 0x2039:
	case 0x203A: {
		const float d = sign.code == 0x2039 ? 1.0f : -1.0f;
		const ImVec2 points[3] = { { cx + d * e * 0.1f, cy - e * 0.22f }, { cx - d * e * 0.1f, cy },
			                   { cx + d * e * 0.1f, cy + e * 0.22f } };

		list->AddPolyline(points, 3, colour, ImDrawFlags_None, w);
		break;
	}
	default:
		line({ l, cy }, { r, cy });
		break;
	}
}

/* A text drawn, its signs too, when `list` is given: its width */
float
run(ImDrawList *list, ImVec2 at, ImU32 colour, const char *text, float size, float spacing)
{
	ImFont *const font = ImGui::GetFont();
	float x = at.x;
	const char *p = text, *plain = text;

	if (size <= 0.0f)
		size = ImGui::GetFontSize();

	const auto flush = [&](const char *end) {
		if (end <= plain)
			return;
		if (list)
			list->AddText(font, size, { x, at.y }, colour, plain, end);
		x += font->CalcTextSizeA(size, FLT_MAX, 0.0f, plain, end).x;
	};

	while (*p) {
		int n;
		const Sign *const sign = sign_of(decode(p, n));

		if (!sign && spacing == 0.0f) {
			p += n;
			continue;
		}
		flush(p);
		if (sign) {
			if (list)
				draw_sign(list, *sign, x, at.y, size, colour);
			x += sign->em * size;
		} else {
			if (list)
				list->AddText(font, size, { x, at.y }, colour, p, p + n);
			x += font->CalcTextSizeA(size, FLT_MAX, 0.0f, p, p + n).x;
		}
		p += n;
		plain = p;
		if (*p)
			x += spacing;
	}
	flush(p);
	return x - at.x;
}

/* The points of the outline of a pill */
std::vector<ImVec2>
pill_points(ImVec2 a, ImVec2 b)
{
	const float r = (b.y - a.y) / 2.0f, pi = std::numbers::pi_v<float>;
	std::vector<ImVec2> points;

	for (int i = 0; i <= 8; i++) {
		const float t = -pi / 2.0f + pi * static_cast<float>(i) / 8.0f;

		points.push_back({ b.x - r + r * std::cos(t), a.y + r + r * std::sin(t) });
	}
	for (int i = 0; i <= 8; i++) {
		const float t = pi / 2.0f + pi * static_cast<float>(i) / 8.0f;

		points.push_back({ a.x + r + r * std::cos(t), a.y + r + r * std::sin(t) });
	}
	return points;
}

} /* namespace */

ImU32
MenuKit::alpha(ImU32 colour, float share)
{
	const ImU32 a = (colour >> IM_COL32_A_SHIFT) & 0xFF;

	return (colour & ~IM_COL32_A_MASK) | (static_cast<ImU32>(static_cast<float>(a) * share) << IM_COL32_A_SHIFT);
}

float
MenuKit::measure(const char *text, float size, float spacing)
{
	return run(nullptr, {}, 0, text, size, spacing);
}

void
MenuKit::write(ImDrawList *list, ImVec2 at, ImU32 colour, const char *text, float size, float spacing)
{
	run(list, at, colour, text, size, spacing);
}

std::string
MenuKit::fitted(const std::string &text, float width, float size)
{
	if (measure(text.c_str(), size) <= width)
		return text;

	const float dots = measure("...", size);
	size_t n = text.size();

	while (n > 0 && measure(text.substr(0, n).c_str(), size) + dots > width) {
		n--;
		/* not in the middle of a character of UTF-8 */
		while (n > 0 && (static_cast<unsigned char>(text[n]) & 0xC0) == 0x80)
			n--;
	}
	return text.substr(0, n) + "...";
}

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

void
MenuKit::dashed_pill(ImDrawList *list, ImVec2 a, ImVec2 b, ImU32 colour) const
{
	dashes(list, pill_points(a, b), true, colour, 1.0f, px(3.0f), px(2.0f));
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
	const auto lines = [&](std::initializer_list<ImVec2> points, bool closed) {
		for (const ImVec2 q : points)
			list->PathLineTo(p(q.x, q.y));
		list->PathStroke(colour, closed ? ImDrawFlags_Closed : ImDrawFlags_None, w);
	};

	switch (icon) {
	case Icon::NONE:
		break;
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
		lines({ { 4, 9 }, { 4, 15 }, { 8, 15 }, { 13, 19 }, { 13, 5 }, { 8, 9 } }, true);
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
	case Icon::LIST:
		for (const float y : { 7.0f, 12.0f, 17.0f }) {
			list->AddLine(p(9, y), p(20, y), colour, w);
			list->AddCircleFilled(p(4.5f, y), w * 0.8f, colour);
		}
		break;
	case Icon::SWAP:
		lines({ { 4, 8 }, { 20, 8 } }, false);
		lines({ { 16, 4 }, { 20, 8 }, { 16, 12 } }, false);
		lines({ { 20, 16 }, { 4, 16 } }, false);
		lines({ { 8, 12 }, { 4, 16 }, { 8, 20 } }, false);
		break;
	case Icon::DEFAULTS:
		/* Round from the top, the way of the clock, back to the top */
		arc(12, 13, 7, -60, 200);
		list->PathStroke(colour, ImDrawFlags_None, w);
		lines({ { 11, 3 }, { 15.5f, 6.9f }, { 10.5f, 9.5f } }, false);
		break;
	case Icon::CLEAR:
		list->AddLine(p(6, 6), p(18, 18), colour, w);
		list->AddLine(p(18, 6), p(6, 18), colour, w);
		break;
	case Icon::MAP:
		lines({ { 6, 6 }, { 14, 12 }, { 6, 18 } }, true);
		list->AddLine(p(18, 6), p(18, 18), colour, w);
		break;
	case Icon::UNDO:
		lines({ { 9, 14 }, { 4, 9 }, { 9, 4 } }, false);
		list->PathLineTo(p(4, 9));
		list->PathLineTo(p(14, 9));
		arc(14, 15, 6, -90, 90);
		list->PathLineTo(p(11, 21));
		list->PathStroke(colour, ImDrawFlags_None, w);
		break;
	case Icon::RUNE:
		/* ᚨ, ansuz: a stem and two branches down to the right */
		list->AddLine(p(8, 2), p(8, 22), colour, w);
		list->AddLine(p(8, 3), p(16, 9), colour, w);
		list->AddLine(p(8, 9), p(16, 15), colour, w);
		break;
	}
}

bool
MenuKit::tabs(std::span<const char *const> names, std::span<const Icon> icons, bool narrow, int &chosen) const
{
	ImDrawList *const list = ImGui::GetWindowDrawList();
	const ImVec2 at = ImGui::GetCursorScreenPos();
	const float width = ImGui::GetContentRegionAvail().x;
	const float wide = px(8.0f), high = px(CELL_HIGH + 1.0f), gap = px(4.0f), line = ImGui::GetTextLineHeight();
	const float h = line + 2.0f * high, side = line, space = px(5.0f);
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
			frame(list, a, b, px(CONTROL_CUT), 0, alpha(m_palette.action, CHOSEN_LINE));
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
	const float size = font(SMALL);

	ImGui::Dummy({ 0.0f, px(10.0f) });
	write(list, ImGui::GetCursorScreenPos(), m_palette.dim, title, size, size * TITLE_SPACING);
	ImGui::Dummy({ 0.0f, size + px(5.0f) });
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
	frame(list, m_group_top, { m_group_top.x + ImGui::GetContentRegionAvail().x, end.y }, px(GROUP_CUT), 0,
	      m_palette.line);
	list->ChannelsMerge();
}

ImVec2
MenuKit::begin_row(float &width)
{
	const ImVec2 at = ImGui::GetCursorScreenPos();

	width = ImGui::GetContentRegionAvail().x;
	if (!m_first_row)
		ImGui::GetWindowDrawList()->AddLine({ at.x + 1.0f, at.y }, { at.x + width - 1.0f, at.y }, m_palette.line, 1.0f);
	m_first_row = false;
	return at;
}

/* The name, what the row does and why it does not change, one under the
 * other in that width, the last two smaller and fainter: their height */
float
MenuKit::texts(ImVec2 at, float width, const char *name, const char *about, const char *locked) const
{
	ImDrawList *const list = ImGui::GetWindowDrawList();
	ImFont *const face = ImGui::GetFont();
	const float space = px(2.0f), note = font(NOTE);
	float h = wrapped_h(name, ImGui::GetFontSize(), width);

	list->AddText(face, ImGui::GetFontSize(), at, m_palette.ink, name, nullptr, width);
	for (const char *text : { about, locked }) {
		if (!text || !*text)
			continue;
		list->AddText(face, note, { at.x, at.y + h + space }, m_palette.faint, text, nullptr, width);
		h += space + wrapped_h(text, note, width);
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
	ImGui::PushID(name);

	const float pad = px(ROW_PAD);
	const ImVec2 control = segments_size(labels);
	float width;
	const ImVec2 at = begin_row(width);
	const Placed placed = place(at, width, control.x, control.y, name, about, locked);
	const bool changed = segments({ at.x + width - pad - control.x, placed.top }, labels, chosen, locked || off);

	ImGui::SetCursorScreenPos(at);
	ImGui::Dummy({ width, placed.h });
	ImGui::PopID();
	return changed;
}

ImVec2
MenuKit::segments_size(std::span<const char *const> labels) const
{
	float w = 0.0f;

	for (const char *label : labels)
		w += measure(label, font(SMALL)) + 2.0f * px(CELL_WIDE);
	return { w, font(SMALL) + 2.0f * px(CELL_HIGH) };
}

bool
MenuKit::segments(ImVec2 a, std::span<const char *const> labels, int &chosen, bool still) const
{
	IM_ASSERT(!labels.empty() && labels.size() <= MOST_LABELS);

	ImDrawList *const list = ImGui::GetWindowDrawList();
	const float wide = px(CELL_WIDE), high = px(CELL_HIGH), size = font(SMALL);
	const float control_h = size + 2.0f * high;
	std::array<float, MOST_LABELS> cells;
	float control = 0.0f;

	for (size_t i = 0; i < labels.size(); i++)
		control += cells[i] = measure(labels[i], size) + 2.0f * wide;

	const int was = chosen;
	float x = a.x;

	ImGui::BeginDisabled(still);
	for (size_t i = 0; i < labels.size(); i++) {
		const ImVec2 ca = { x, a.y }, cb = { x + cells[i], a.y + control_h };

		ImGui::SetCursorScreenPos(ca);
		if (ImGui::InvisibleButton(labels[i], { cells[i], control_h }))
			chosen = static_cast<int>(i);

		const bool on = chosen == static_cast<int>(i), hovered = !still && ImGui::IsItemHovered();

		if (on || hovered)
			fill_cell(list, ca, cb, px(CONTROL_CUT), i == 0, i + 1 == labels.size(),
			          faded(on ? m_palette.action : m_palette.hover, still));
		if (i > 0)
			list->AddLine(ca, { x, cb.y }, faded(m_palette.outline, still), 1.0f);
		write(list, { x + wide, a.y + high },
		      faded(on ? m_palette.on_action : hovered ? m_palette.ink : m_palette.dim, still), labels[i], size);
		x += cells[i];
	}
	ImGui::EndDisabled();
	frame(list, a, { a.x + control, a.y + control_h }, px(CONTROL_CUT), 0, faded(m_palette.outline, still));
	return chosen != was;
}

bool
MenuKit::dropdown(const char *name, const char *about, std::span<const std::string> labels, int &chosen)
{
	ImGui::PushID(name);

	const float pad = px(ROW_PAD), wide = px(CELL_WIDE), caret = measure("▾");
	const float control_h = picker_height();
	float longest = 0.0f, width;

	for (const std::string &label : labels)
		longest = std::max(longest, measure(label.c_str()));

	const ImVec2 at = begin_row(width);
	const float control = std::min(longest + 3.0f * wide + caret, width / 2.0f);
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
                Icon icon_shown) const
{
	ImGui::PushID(id);

	ImDrawList *const list = ImGui::GetWindowDrawList();
	const MenuPalette &p = m_palette;
	const float wide = px(CELL_WIDE + 1.0f), high = px(CELL_HIGH), caret = measure("▾");
	const ImVec2 b = { at.x + width, at.y + picker_height() };
	const int was = chosen;

	ImGui::SetCursorScreenPos(at);
	if (ImGui::InvisibleButton("open", { width, b.y - at.y }))
		ImGui::OpenPopup("list");
	frame(list, at, b, px(GROUP_CUT), 0, ImGui::IsItemHovered() || ImGui::IsPopupOpen("list") ? p.stroke : p.line);
	if (chosen >= 0 && static_cast<size_t>(chosen) < labels.size()) {
		const float side = ImGui::GetTextLineHeight();
		const float before = icon_shown != Icon::NONE ? side + px(6.0f) : 0.0f;

		icon(list, icon_shown, { at.x + wide, at.y + high }, side, p.dim);
		list->PushClipRect(at, { b.x - wide - caret, b.y }, true);
		write(list, { at.x + wide + before, at.y + high }, p.ink,
		      fitted(labels[static_cast<size_t>(chosen)], b.x - 2.0f * wide - caret - at.x - wide - before).c_str());
		list->PopClipRect();
	}
	write(list, { b.x - wide - caret, at.y + high }, p.dim, "▾");

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
		longest = std::max(longest, measure(label.c_str()) + 2.0f * px(CELL_WIDE) + 2.0f * px(4.0f));
	longest = std::min(longest, room - 2.0f * margin);
	ImGui::SetNextWindowPos({ std::max(margin, std::min(at.x, room - margin - longest)), at.y });
	ImGui::SetNextWindowSizeConstraints({ longest, 0.0f }, { longest, ImGui::GetIO().DisplaySize.y });
	ImGui::PushStyleColor(ImGuiCol_PopupBg, ImGui::ColorConvertU32ToFloat4(p.screen));
	ImGui::PushStyleColor(ImGuiCol_Border, ImGui::ColorConvertU32ToFloat4(p.stroke));
	ImGui::PushStyleColor(ImGuiCol_Header, ImGui::ColorConvertU32ToFloat4(0));
	ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ImGui::ColorConvertU32ToFloat4(p.hover));
	ImGui::PushStyleColor(ImGuiCol_HeaderActive, ImGui::ColorConvertU32ToFloat4(p.hover));
	ImGui::PushStyleColor(ImGuiCol_Text, ImGui::ColorConvertU32ToFloat4(p.ink));
	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, { px(4.0f), px(4.0f) });
	ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, { 0.0f, px(2.0f) });
	ImGui::PushStyleVar(ImGuiStyleVar_SelectableTextAlign, { 0.0f, 0.5f });
	ImGui::PushStyleVar(ImGuiStyleVar_PopupBorderSize, 1.0f);
	ImGui::PushStyleVar(ImGuiStyleVar_PopupRounding, 0.0f);
	if (ImGui::BeginPopup("list")) {
		ImDrawList *const list = ImGui::GetWindowDrawList();
		const float h = ImGui::GetTextLineHeight() + 2.0f * px(CELL_HIGH + 1.0f);

		for (size_t i = 0; i < labels.size(); i++) {
			const bool on = static_cast<int>(i) == chosen;
			const ImVec2 a = ImGui::GetCursorScreenPos();

			ImGui::PushID(static_cast<int>(i));
			if (ImGui::Selectable("##item", on, ImGuiSelectableFlags_None, { 0.0f, h }))
				chosen = static_cast<int>(i);
			ImGui::PopID();
			if (on)
				frame(list, ImGui::GetItemRectMin(), ImGui::GetItemRectMax(), px(CONTROL_CUT), 0, p.stroke);
			write(list, { a.x + px(CELL_WIDE), a.y + (h - ImGui::GetTextLineHeight()) / 2.0f }, p.ink, labels[i].c_str());
		}
		ImGui::EndPopup();
	}
	ImGui::PopStyleVar(5);
	ImGui::PopStyleColor(6);
	return chosen != was;
}

ImVec2
MenuKit::button_size(const char *label, Icon icon_shown) const
{
	const float size = font(SMALL), side = icon_shown != Icon::NONE ? size + px(4.0f) : 0.0f;

	return { side + measure(label, size) + 2.0f * px(BUTTON_WIDE), size + 2.0f * px(BUTTON_HIGH) };
}

bool
MenuKit::button(const char *label, bool same_line, Icon icon_shown, Tone tone) const
{
	if (same_line)
		ImGui::SameLine(0.0f, px(10.0f));

	const ImVec2 at = ImGui::GetCursorScreenPos();
	const bool clicked = button(label, at, false, icon_shown, tone);

	ImGui::SetCursorScreenPos(at);
	ImGui::Dummy(button_size(label, icon_shown));
	return clicked;
}

bool
MenuKit::button(const char *label, ImVec2 at, bool on, Icon icon_shown, Tone tone) const
{
	ImDrawList *const list = ImGui::GetWindowDrawList();
	const float size = font(SMALL);
	const ImVec2 box = button_size(label, icon_shown), b = { at.x + box.x, at.y + box.y };

	ImGui::SetCursorScreenPos(at);

	const bool clicked = ImGui::InvisibleButton(label, box);
	const bool hovered = ImGui::IsItemHovered();
	const ImU32 hot = tone == Tone::DANGER ? m_palette.danger : m_palette.action;
	const ImU32 border = hovered ? hot : on ? alpha(m_palette.action, CHOSEN_LINE) :
	                     tone == Tone::STRONG ? m_palette.stroke : m_palette.outline;
	const ImU32 ink = hovered ? hot : m_palette.ink;
	float x = at.x + px(BUTTON_WIDE);

	frame(list, at, b, px(CONTROL_CUT), on ? alpha(m_palette.action, CHOSEN_FILL) : 0, border);
	if (icon_shown != Icon::NONE) {
		icon(list, icon_shown, { x, at.y + px(BUTTON_HIGH) }, size, ink);
		x += size + px(4.0f);
	}
	write(list, { x, at.y + px(BUTTON_HIGH) }, ink, label, size);
	return clicked;
}

ImVec2
MenuKit::arrow_size() const
{
	return { px(22.0f), font(SMALL) + 2.0f * px(BUTTON_HIGH) };
}

bool
MenuKit::arrow(const char *id, ImVec2 at, bool right) const
{
	ImDrawList *const list = ImGui::GetWindowDrawList();
	const ImVec2 size = arrow_size(), b = { at.x + size.x, at.y + size.y };
	const char *const sign = right ? "›" : "‹";
	const float big = ImGui::GetFontSize() * 1.15f;

	ImGui::SetCursorScreenPos(at);

	const bool clicked = ImGui::InvisibleButton(id, size);
	const bool hovered = ImGui::IsItemHovered();

	list->AddRect(at, b, hovered ? m_palette.action : m_palette.outline, size.y / 2.0f);
	write(list, { (at.x + b.x - measure(sign, big)) / 2.0f, (at.y + b.y - big) / 2.0f },
	      hovered ? m_palette.action : m_palette.ink, sign, big);
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
	const float pad = px(ROW_PAD), space = px(8.0f), size = font(SMALL), small = font(TINY);
	const char *const shown_button = locked ? nullptr : action;
	const char *const shown_word = shown_button ? nullptr : word;
	ImFont *const face = ImGui::GetFont();
	const ImVec2 button_box = shown_button ? button_size(shown_button) : ImVec2{};
	const float word_w = shown_word ? measure(shown_word, small, small * TITLE_SPACING) : 0.0f;
	const float after = shown_button ? button_box.x + space : shown_word ? word_w + space : 0.0f;
	float width;
	const ImVec2 at = begin_row(width);
	/* A long value takes half the row at most, on more lines; the whole
	 * row under the texts, where they have not room enough (a pixel more:
	 * a text as wide as its room may go to the line) */
	const float wide_w = std::ceil(face->CalcTextSizeA(size, FLT_MAX, 0.0f, value).x) + 1.0f;
	const bool under = width - std::min(wide_w, width / 2.0f - after) - after - px(GAP) - 2.0f * pad < px(TEXT_LEAST);
	const float value_w = std::max(px(20.0f), std::min(wide_w, (under ? width - 2.0f * pad : width / 2.0f) - after));
	const ImVec2 value_size = face->CalcTextSizeA(size, FLT_MAX, value_w, value);
	const float control_h = std::max(value_size.y, button_box.y);
	const Placed placed = place(at, width, value_w + after, control_h, name, about, locked);
	const float h = placed.h, mid = placed.top + control_h / 2.0f;
	const float right = at.x + width - pad;
	bool clicked = false;

	list->AddText(face, size, { right - after - value_w, mid - value_size.y / 2.0f }, m_palette.dim, value, nullptr,
	              value_w);
	if (shown_button) {
		m_button_a = { right - button_box.x, mid - button_box.y / 2.0f };
		m_button_b = { m_button_a.x + button_box.x, m_button_a.y + button_box.y };
		clicked = button(shown_button, m_button_a, false);
	} else if (shown_word) {
		write(list, { right - word_w, mid - small / 2.0f }, m_palette.faint, shown_word, small, small * TITLE_SPACING);
	}
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
	const float pad = px(ROW_PAD), track_w = px(64.0f), track_h = px(6.0f), space = px(8.0f), size = font(SMALL);
	const float shown_w = std::max(measure(shown, size), measure("-60 dB", size));
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
	list->AddRect(a, b, faded(hovered ? m_palette.stroke : m_palette.outline, still), r);
	write(list, { b.x + space, mid - size / 2.0f }, faded(m_palette.ink, still), shown, size);

	ImGui::SetCursorScreenPos(at);
	ImGui::Dummy({ width, h });
	ImGui::PopID();
	return value != was;
}

ImVec2
MenuKit::key_size(const char *name) const
{
	const float h = font(SMALL) + 2.0f * px(BUTTON_HIGH);

	return { std::max(h, measure(name, font(NOTE)) + 2.0f * px(4.0f)), h };
}

void
MenuKit::key(ImDrawList *list, ImVec2 at, const char *name, bool used, bool lit) const
{
	const ImVec2 size = key_size(name), b = { at.x + size.x, at.y + size.y };
	const float note = font(NOTE), w = measure(name, note);

	list->AddRectFilled(at, b, lit ? m_palette.accent : m_palette.part, px(5.0f));
	list->AddRect(at, b, lit ? m_palette.accent : used ? m_palette.stroke : m_palette.outline, px(5.0f));
	write(list, { at.x + (size.x - w) / 2.0f, at.y + (size.y - note) / 2.0f },
	      lit ? m_palette.on_accent : used ? m_palette.ink : m_palette.faint, name, note);
}

ImVec2
MenuKit::pill_size(const char *name) const
{
	const float h = font(SMALL) + 2.0f * px(BUTTON_HIGH);

	return { std::max(h, measure(name, font(SMALL)) + 2.0f * px(6.0f)), h };
}

void
MenuKit::pill(ImDrawList *list, ImVec2 at, const char *name) const
{
	const ImVec2 size = pill_size(name);
	const float small = font(SMALL), w = measure(name, small);

	list->AddRect(at, { at.x + size.x, at.y + size.y }, m_palette.outline, size.y / 2.0f);
	write(list, { at.x + (size.x - w) / 2.0f, at.y + (size.y - small) / 2.0f }, m_palette.ink, name, small);
}

int
MenuKit::shortcut(const char *name, std::span<const std::vector<std::string>> keys, std::span<const std::string> pad,
                  int catching)
{
	ImGui::PushID(name);

	ImDrawList *const list = ImGui::GetWindowDrawList();
	const char press[] = "press...", none[] = "-";
	const float small = font(SMALL), note = font(NOTE);
	const float pad_room = px(ROW_PAD), gap = px(3.0f), apart = px(8.0f), plus = measure("+", small);
	const float cap_h = key_size("x").y;
	const float press_key = measure(press, note) + 2.0f * px(5.0f), press_pad = measure(press, small) + 2.0f * px(7.0f);
	/* A combination: its caps with + between them */
	const auto combination_w = [&](size_t i) {
		if (static_cast<int>(i) == catching)
			return press_key;

		float w = 0.0f;

		for (size_t k = 0; k < keys[i].size(); k++)
			w += key_size(keys[i][k].c_str()).x + (k ? 2.0f * gap + plus : 0.0f);
		return w;
	};
	float keys_w = 0.0f, pad_w = 0.0f;

	for (size_t i = 0; i < keys.size(); i++)
		keys_w += combination_w(i) + (i ? apart : 0.0f);
	/* The buttons as wide as "Guide + Back" at the least */
	if (catching == 2)
		pad_w = press_pad;
	else if (pad.empty())
		pad_w = measure(none, small);
	else
		for (size_t i = 0; i < pad.size(); i++)
			pad_w += pill_size(pad[i].c_str()).x + (i ? 2.0f * gap + plus : 0.0f);
	pad_w = std::max(pad_w, pill_size("Guide").x + pill_size("Back").x + 2.0f * gap + plus);

	float width;
	const ImVec2 at = begin_row(width);
	const float control = keys_w + px(GAP) + pad_w;
	/* Where the name has not room enough beside them, they go under it */
	const bool under = width - control - px(GAP) - 2.0f * pad_room < px(TEXT_LEAST);
	const float text_w = under ? width - 2.0f * pad_room : width - control - px(GAP) - 2.0f * pad_room;
	const float text_h = texts({ at.x + pad_room, at.y + pad_room }, text_w, name, nullptr, nullptr);
	const float h = (under ? text_h + px(6.0f) + cap_h : std::max(text_h, cap_h)) + 2.0f * pad_room;
	const float y = under ? at.y + pad_room + text_h + px(6.0f) : at.y + (h - cap_h) / 2.0f;
	float x = at.x + width - pad_room - control;
	int clicked = -1;

	for (size_t i = 0; i < keys.size(); i++) {
		const float w = combination_w(i);

		ImGui::SetCursorScreenPos({ x, y });
		ImGui::PushID(static_cast<int>(i));
		if (ImGui::InvisibleButton("key", { w, cap_h }))
			clicked = static_cast<int>(i);
		ImGui::PopID();

		const bool hovered = ImGui::IsItemHovered();

		if (static_cast<int>(i) == catching) {
			list->AddRect({ x, y }, { x + w, y + cap_h }, m_palette.accent, px(5.0f));
			write(list, { x + px(5.0f), y + (cap_h - note) / 2.0f }, m_palette.accent, press, note);
			x += w + apart;
			continue;
		}
		for (size_t k = 0; k < keys[i].size(); k++) {
			const char *const cap = keys[i][k].c_str();
			const float cap_w = key_size(cap).x;

			if (k) {
				write(list, { x + gap, y + (cap_h - small) / 2.0f }, m_palette.faint, "+", small);
				x += 2.0f * gap + plus;
			}
			key(list, { x, y }, cap, true, false);
			if (hovered)
				list->AddRect({ x, y }, { x + cap_w, y + cap_h }, m_palette.action, px(5.0f));
			x += cap_w;
		}
		x += apart;
	}

	const float pad_x = at.x + width - pad_room - pad_w;

	ImGui::SetCursorScreenPos({ pad_x, y });
	if (ImGui::InvisibleButton("pad", { pad_w, cap_h }))
		clicked = 2;
	if (catching == 2) {
		list->AddRect({ pad_x + pad_w - press_pad, y }, { pad_x + pad_w, y + cap_h }, m_palette.accent, cap_h / 2.0f);
		write(list, { pad_x + pad_w - press_pad + px(7.0f), y + (cap_h - small) / 2.0f }, m_palette.accent, press, small);
	} else {
		const bool hovered = ImGui::IsItemHovered();
		float right = pad_x + pad_w;

		if (pad.empty())
			write(list, { right - measure(none, small), y + (cap_h - small) / 2.0f }, m_palette.faint, none, small);
		for (size_t i = pad.size(); i-- > 0;) {
			const ImVec2 size = pill_size(pad[i].c_str());

			right -= size.x;
			pill(list, { right, y }, pad[i].c_str());
			if (hovered)
				list->AddRect({ right, y }, { right + size.x, y + size.y }, m_palette.action, size.y / 2.0f);
			if (i) {
				right -= gap + plus;
				write(list, { right, y + (cap_h - small) / 2.0f }, m_palette.faint, "+", small);
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
	const float width = ImGui::GetContentRegionAvail().x, size = font(SMALL);

	ImGui::Dummy({ 0.0f, px(10.0f) });
	write(list, ImGui::GetCursorScreenPos(), m_palette.dim, title, size, size * TITLE_SPACING);
	ImGui::Dummy({ 0.0f, size + px(5.0f) });
	list->AddText(ImGui::GetFont(), size, ImGui::GetCursorScreenPos(), m_palette.faint, text, nullptr, width);
	ImGui::Dummy({ 0.0f, wrapped_h(text, size, width) });
}

} /* namespace odin */
