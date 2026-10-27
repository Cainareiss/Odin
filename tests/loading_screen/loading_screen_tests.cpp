/*
 *  ██████╗ ██████╗ ██╗███╗   ██╗
 * ██╔═══██╗██╔══██╗██║████╗  ██║
 * ██║   ██║██║  ██║██║██╔██╗ ██║
 * ██║   ██║██║  ██║██║██║╚██╗██║
 * ╚██████╔╝██████╔╝██║██║ ╚████║
 *  ╚═════╝ ╚═════╝ ╚═╝╚═╝  ╚═══╝
 *
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

/* Unit tests for the loading screen: the drawing of each character and
 * where the title and the bar go.
 *
 * Build: g++ -std=c++20 -O2 -Wall -Wextra -Wpedantic -Wconversion -Wshadow \
 *            -I src tests/loading_screen/loading_screen_tests.cpp src/odin/host/ui/loading_screen.cpp \
 *            -o loading_screen_tests
 * Exit status is 0 when every check passes. */

#include "odin/host/ui/loading_screen.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdio>
#include <string>
#include <vector>

namespace {

using odin::LoadingRect;
using odin::LoadingScreen;

int g_checks, g_failures;

void check(bool ok, const char *what)
{
	g_checks++;
	if (!ok) {
		g_failures++;
		std::printf("FAILED: %s\n", what);
	}
}

bool same(const LoadingRect &r, unsigned x, unsigned y, unsigned w, unsigned h, float shade)
{
	return r.x == x && r.y == y && r.w == w && r.h == h && r.shade == shade;
}

void test_glyphs()
{
	const std::array<uint8_t, 7> a = { 0x0E, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11 };
	bool all_lit = true, all_apart = true, small_as_capitals = true;

	check(LoadingScreen::glyph('A') == a, "A is drawn as it should");

	/* The drawings as they were seen on the screen: one that changes is
	 * looked at again, then this fingerprint with it. */
	uint64_t fingerprint = 0xCBF29CE484222325ull;

	for (char c = ' '; c <= '~'; c++)
		for (const uint8_t row : LoadingScreen::glyph(c))
			fingerprint = (fingerprint ^ row) * 0x100000001B3ull;
	check(fingerprint == 0xE3284B9D5BD4EDDDull, "no drawing has changed");
	for (char c = '!'; c <= '~'; c++) {
		const auto &g = LoadingScreen::glyph(c);

		all_lit = all_lit && std::any_of(g.begin(), g.end(), [](uint8_t row) { return row != 0; });
		if (c >= 'a' && c <= 'z') {
			small_as_capitals = small_as_capitals && g == LoadingScreen::glyph(static_cast<char>(c - 'a' + 'A'));
			continue;
		}
		for (char d = '!'; d < c; d++)
			if (!(d >= 'a' && d <= 'z') && LoadingScreen::glyph(d) == g)
				all_apart = false;
	}
	check(all_lit, "every printable character but the space has a drawing");
	check(all_apart, "and no two have the same");
	check(small_as_capitals, "small letters are drawn as capitals");

	const std::array<uint8_t, 7> blank = {};
	check(LoadingScreen::glyph(' ') == blank && LoadingScreen::glyph('\t') == blank &&
	      LoadingScreen::glyph('\x7F') == blank && LoadingScreen::glyph('\x80') == blank,
	      "a space, and any other character than printable ASCII as one");
}

/* The rectangles after those of the title: the label and the bar */
size_t after_title()
{
	return LoadingScreen::layout("", 0, 10).size();
}

void test_title()
{
	const size_t rest = after_title();

	/* One letter: the largest scale, in the middle */
	const auto one = LoadingScreen::layout("A", 0, 10);

	check(one.size() == 12 + rest, "A: twelve runs of lit pixels, then the label and the bar");
	check(!one.empty() && same(one[0], 310 + 4, 226, 12, 4, LoadingScreen::TITLE_SHADE),
	      "its top row at four times its size, in the middle of the picture");
	check(one.size() > 2 && same(one[1], 310, 230, 4, 4, LoadingScreen::TITLE_SHADE) &&
	      same(one[2], 310 + 16, 230, 4, 4, LoadingScreen::TITLE_SHADE), "a row of two runs gives two rectangles");

	/* A title: as much to its left as to its right, above as below */
	const auto air = LoadingScreen::layout("Air Trix", 0, 10);
	unsigned left = LoadingScreen::WIDTH, right = 0, top = LoadingScreen::HEIGHT, bottom = 0;

	for (size_t i = 0; i + rest < air.size(); i++) {
		left = std::min(left, air[i].x);
		right = std::max(right, air[i].x + air[i].w);
		top = std::min(top, air[i].y);
		bottom = std::max(bottom, air[i].y + air[i].h);
	}
	check(left == 226 && right == LoadingScreen::WIDTH - 226, "a title is in the middle across");
	check(top == 226 && bottom == LoadingScreen::HEIGHT - 226, "and up and down");

	/* The longest title the list takes: smaller, still in the picture */
	const auto wide = LoadingScreen::layout(std::string(48, 'W'), 0, 10);
	bool inside = true, halved = true;

	for (size_t i = 0; i + rest < wide.size(); i++) {
		inside = inside && wide[i].x + wide[i].w <= LoadingScreen::WIDTH &&
		         wide[i].y + wide[i].h <= LoadingScreen::HEIGHT;
		halved = halved && wide[i].h == 2;
	}
	check(inside, "a title of 48 characters is all in the picture");
	check(halved, "drawn twice its size: four times would not fit");
	check(wide.size() > 1 && wide[0].x == (LoadingScreen::WIDTH - (48 * 6 - 1) * 2) / 2,
	      "and in the middle across");

	check(LoadingScreen::layout("   ", 0, 10).size() == rest, "a title of spaces: no rectangle");
}

void test_label()
{
	const auto none = LoadingScreen::layout("", 0, 10);
	const size_t runs = LoadingScreen::layout(LoadingScreen::LABEL, 0, 10).size() - none.size();
	const size_t label = none.size() - 1;
	unsigned left = LoadingScreen::WIDTH, bottom = 0;
	bool small = true, grey = true;

	for (size_t i = 0; i < label; i++) {
		left = std::min(left, none[i].x);
		bottom = std::max(bottom, none[i].y + none[i].h);
		small = small && none[i].h == LoadingScreen::LABEL_SCALE;
		grey = grey && none[i].shade == LoadingScreen::LABEL_SHADE;
	}
	check(std::string_view(LoadingScreen::LABEL) == "NOW LOADING", "the label is NOW LOADING");
	check(label == runs, "as many runs as when it is a title");
	check(small && grey, "twice the size of the font, in grey");
	check(left == LoadingScreen::BAR_X && bottom == LoadingScreen::BAR_Y - 6,
	      "from the left end of the bar, six pixels above it");
	check(LoadingScreen::TRACK_SHADE < LoadingScreen::LABEL_SHADE && LoadingScreen::LABEL_SHADE < LoadingScreen::TITLE_SHADE,
	      "lighter than the track, darker than the title");
}

/* The rectangles of the bar of a layout: the last one or two */
std::vector<LoadingRect> bar(unsigned done, unsigned total)
{
	const auto all = LoadingScreen::layout("", done, total);
	const size_t label = after_title() - 1;

	return std::vector<LoadingRect>(all.begin() + static_cast<std::ptrdiff_t>(label), all.end());
}

void test_bar()
{
	const auto empty = bar(0, 4);
	const auto half = bar(2, 4);
	const auto full = bar(4, 4);

	check(empty.size() == 1 && same(empty[0], 192, 432, 256, 4, LoadingScreen::TRACK_SHADE),
	      "nothing done: the track of the bar alone, low down in the middle");
	check(half.size() == 2 && same(half[1], 192, 432, 128, 4, LoadingScreen::FILL_SHADE), "half done: half filled");
	check(full.size() == 2 && same(full[1], 192, 432, 256, 4, LoadingScreen::FILL_SHADE), "all done: filled");
	check(bar(1, 3).size() == 2 && bar(1, 3)[1].w == 85, "a third done: a third filled, rounded down");
	check(bar(9, 4).size() == 2 && bar(9, 4)[1].w == 256 && bar(0, 0).size() == 2 && bar(0, 0)[1].w == 256,
	      "more than all, or nothing to do: filled");
	check(LoadingScreen::TRACK_SHADE < LoadingScreen::FILL_SHADE, "the fill shows on its track");
}

} /* namespace */

int main()
{
	test_glyphs();
	test_title();
	test_label();
	test_bar();

	std::printf("%d checks, %d failures\n", g_checks, g_failures);
	return g_failures ? 1 : 0;
}
