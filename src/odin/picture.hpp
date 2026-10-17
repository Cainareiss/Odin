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

#ifndef ODIN_PICTURE_HPP
#define ODIN_PICTURE_HPP

namespace odin {

/* Where the picture of the hardware goes in a window: the largest
 * rectangle of the shape of its screen, 4 by 3, that fits, in the middle.
 * The drawing places it so, the mouse is read so. */
struct PictureRect {
	unsigned x, y, w, h;
};

inline PictureRect
picture_rect(unsigned window_w, unsigned window_h)
{
	unsigned w = window_w, h = window_w * 3 / 4;

	if (h > window_h) {
		h = window_h;
		w = window_h * 4 / 3;
	}
	return { (window_w - w) / 2, (window_h - h) / 2, w, h };
}

/* Where a pixel of a window is on its picture: 0 to 1 each way from the
 * top left corner, outside for a pixel off the picture. */
struct PicturePoint {
	double x, y;
};

inline PicturePoint
picture_point(int x, int y, unsigned window_w, unsigned window_h)
{
	const PictureRect rect = picture_rect(window_w, window_h);

	if (rect.w == 0 || rect.h == 0)
		return { -1.0, -1.0 };
	return { (x + 0.5 - rect.x) / rect.w, (y + 0.5 - rect.y) / rect.h };
}

} /* namespace odin */

#endif /* ODIN_PICTURE_HPP */
