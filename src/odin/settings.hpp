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

#ifndef ODIN_SETTINGS_HPP
#define ODIN_SETTINGS_HPP

#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace odin {

/* The settings of the user, odin.ini: "[section]" lines, then
 * "key = value" lines; a line that starts with # or ; is a comment. A key
 * keeps the order of the file. */
class Settings {
public:
	using Lines = std::vector<std::pair<std::string, std::string>>;

	/* ODIN_CONFIG=<file>; none with ODIN_CONFIG=none; else odin.ini in
	 * the data folder of Odin, beside saves/. Empty when there is none. */
	static std::string user_file();

	/* A missing file is an empty one. A line that is neither a section, a
	 * key nor a comment is said on stderr and skipped. False, said on
	 * stderr, when the file is there but cannot be read. */
	bool load(const std::string &path);
	void parse(std::string_view text, const std::string &origin);

	/* The lines of a section; none when the file has no such section. */
	const Lines &section(const std::string &name) const;

	/* A key of a section given a value, or taken out (nullopt: the value
	 * by default then): the file is read again, changed, written whole to
	 * a file beside it and renamed over it, then read here again. False,
	 * said on stderr, if it cannot be written. */
	bool change(const std::string &path, const std::string &section, const std::string &key,
	            const std::optional<std::string> &value);
	/* The text of such a file with that change, the rest of it as it was:
	 * comments, order, unknown lines and line ends. The last line of the
	 * key takes the value and the ones before it go (the last line says);
	 * a new key goes after the last line of its section, a new section at
	 * the end. */
	static std::string changed(std::string_view text, const std::string &section, const std::string &key,
	                           const std::optional<std::string> &value);

private:
	std::map<std::string, Lines> m_sections;
};

} /* namespace odin */

#endif /* ODIN_SETTINGS_HPP */
