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

/* Unit tests for where the picture goes in a window: in the middle, the
 * largest 4:3 that fits; in the hole of a bezel where the window is wider
 * than 4:3 (a television whose screen is off the middle, as Batocera
 * draws one), and the mouse read there.
 * Build: g++ -std=c++20 -O2 -Wall -Wextra -Wpedantic -Wconversion -Wshadow -I src \
 *            tests/picture/picture_tests.cpp -o picture_tests
 * Exit status is 0 when every check passes. */

#include "odin/host/video/picture.hpp"

#include <cstdio>

namespace {

int g_checks, g_failures;

void check(bool ok, const char *what)
{
	g_checks++;
	if (!ok) {
		g_failures++;
		std::printf("FAILED: %s\n", what);
	}
}

bool same(const odin::PictureRect &a, unsigned x, unsigned y, unsigned w, unsigned h)
{
	return a.x == x && a.y == y && a.w == w && a.h == h;
}

} /* namespace */

int main()
{
	using odin::PictureHole;
	using odin::picture_point;
	using odin::picture_rect;

	/* The television of Batocera: 1920 by 1080, its screen 201 from the
	 * left, 61 from the top, 1296 by 974 */
	const PictureHole tv = { 201.0 / 1920, 61.0 / 1080, 1296.0 / 1920, 974.0 / 1080, 1920.0, 1080.0 };

	check(same(picture_rect(1280, 720), 160, 0, 960, 720), "16:9: the largest 4:3, in the middle");
	check(same(picture_rect(640, 480), 0, 0, 640, 480), "4:3: the whole window");
	check(same(picture_rect(640, 600), 0, 60, 640, 480), "taller: in the middle");
	check(same(picture_rect(640, 480, tv), 0, 0, 640, 480), "4:3: no bezel shown, its hole not taken");
	check(odin::bezel_shown(1280, 720) && !odin::bezel_shown(640, 480) && !odin::bezel_shown(640, 600),
	      "the bezel shows where the window is wider than 4:3");

	/* In 1280 by 720 the bezel is two thirds of its size: the screen from
	 * 134 to 998, 40.67 to 690, the picture 4:3 in it */
	const odin::PictureRect in = picture_rect(1280, 720, tv);

	check(same(in, 134, 41, 864, 648), "16:9: the picture in the screen of the television");

	const odin::PicturePoint corner = picture_point(134, 41, 1280, 720, tv);
	const odin::PicturePoint middle = picture_point(134 + 432, 41 + 324, 1280, 720, tv);
	const odin::PicturePoint off = picture_point(1100, 300, 1280, 720, tv);

	check(corner.x >= 0.0 && corner.x < 0.01 && corner.y >= 0.0 && corner.y < 0.01, "the mouse at its top left corner");
	check(middle.x > 0.49 && middle.x < 0.51 && middle.y > 0.49 && middle.y < 0.51, "the mouse in its middle");
	check(off.x > 1.0, "the mouse on the knobs of the television: off the picture");

	std::printf("%d checks, %d failures\n", g_checks, g_failures);
	return g_failures ? 1 : 0;
}
