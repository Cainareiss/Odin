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

#ifndef ODIN_LOGO_HPP
#define ODIN_LOGO_HPP

namespace odin {

/* The logo of Odin: the rune ᚨ, ansuz, in gold (Valhöll, the colours of
 * the mock-ups). Its lines in a square of 24, from x, y to x, y: a stem and
 * two branches down to the right. Its gold: red, green, blue. */
inline constexpr float RUNE_LINES[3][4] = { { 8, 2, 8, 22 }, { 8, 3, 16, 9 }, { 8, 9, 16, 15 } };
inline constexpr unsigned char RUNE_GOLD[3] = { 0xD8, 0xAA, 0x4C };

} /* namespace odin */

#endif /* ODIN_LOGO_HPP */
