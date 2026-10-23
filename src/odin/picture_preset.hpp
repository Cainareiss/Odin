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

#ifndef ODIN_PICTURE_PRESET_HPP
#define ODIN_PICTURE_PRESET_HPP

#include <map>
#include <string>
#include <utility>
#include <vector>

/* The texts of a filter of the picture (picture_filter.hpp), read as 86Box
 * reads them: the .glslp preset, and the GLSL file of each pass. */
namespace odin::preset {

/* 86Box takes a parameter whose name and description are 1 to 63
 * characters long */
constexpr size_t LONGEST_NAME = 63;

/* The keys of a preset as the reader of 86Box takes them (utils/ini.c): a
 * line that starts with # or ; is a comment and a [section] ends them; a
 * name ends at = or a space; a value is cut at its first #, loses its
 * quotes and its spaces; the first of two equal names counts. */
std::map<std::string, std::string> read_keys(const std::string &text);

/* A key that is true, false or a number, as 86Box reads it; false when
 * there is none */
bool key_on(const std::map<std::string, std::string> &keys, const std::string &name);

/* A part of a pass (VERTEX or FRAGMENT) as 86Box gives it to the driver:
 * its #version line first, then the extension, the part,
 * PARAMETER_UNIFORM and the rest of the file, its #pragma parameter
 * lines blanked */
std::string part(const std::string &source, const char *which);

/* The parameters a pass declares (#pragma parameter NAME "description"
 * default minimum maximum step), added to `parameters` unless there
 * already */
void declared(const std::string &source, std::vector<std::pair<std::string, float>> &parameters);

} /* namespace odin::preset */

#endif /* ODIN_PICTURE_PRESET_HPP */
