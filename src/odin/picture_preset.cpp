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

#include "odin/picture_preset.hpp"
#include "odin/settings.hpp"

#include <algorithm>
#include <cstdlib>

namespace odin::preset {

std::map<std::string, std::string>
read_keys(const std::string &text)
{
	std::map<std::string, std::string> keys;
	size_t at = 0;

	while (at < text.size()) {
		size_t end = text.find('\n', at);

		end = end == std::string::npos ? text.size() : end;
		const std::string line(trimmed(std::string_view(text).substr(at, end - at)));

		at = end + 1;
		if (line.empty() || line[0] == '#' || line[0] == ';')
			continue;
		if (line[0] == '[')
			break;

		const size_t name_end = line.find_first_of("= ");
		const std::string name = line.substr(0, name_end);
		std::string value = name_end == std::string::npos ? std::string() : line.substr(name_end);

		value = value.substr(std::min(value.find_first_not_of("= "), value.size()));
		value = value.substr(0, value.find('#'));
		if (!value.empty() && value.front() == '"')
			value.erase(0, 1);
		value = value.substr(0, value.find('"'));
		value = std::string(trimmed(value));
		if (!value.empty())
			keys.emplace(name, value);
	}
	return keys;
}

bool
key_on(const std::map<std::string, std::string> &keys, const std::string &name)
{
	const auto key = keys.find(name);

	if (key == keys.end())
		return false;
	if (key->second == "true" || key->second == "false")
		return key->second == "true";
	return std::atoi(key->second.c_str()) != 0;
}

std::string
part(const std::string &source, const char *which)
{
	std::string text = source, version;

	for (const char *start : { "#pragma parameter", "#define texture" })
		for (size_t at = text.find(start); at != std::string::npos; at = text.find(start)) {
			const size_t end = std::min(text.find('\n', at), text.size());

			text.replace(at, end - at, end - at, ' ');
		}
	if (const size_t at = text.find("#version"); at != std::string::npos) {
		size_t end = text.find_first_not_of(" \t", at + 8);

		end = text.find_first_of(" \t\r\n", end);
		version = text.substr(at, end - at);
		text.replace(at, end - at, end - at, ' ');
	}
	return version + "\n\n#extension GL_ARB_shading_language_420pack : enable\n#define " + which +
	       "\n#define PARAMETER_UNIFORM\n\n#line 1\n" + text;
}

void
declared(const std::string &source, std::vector<std::pair<std::string, float>> &parameters)
{
	static constexpr char PRAGMA[] = "#pragma parameter";

	for (size_t at = source.find(PRAGMA); at != std::string::npos; at = source.find(PRAGMA, at + 1)) {
		const std::string line = source.substr(at, source.find('\n', at) - at);
		const size_t name_at = line.find_first_not_of(" \t", sizeof(PRAGMA) - 1);
		const size_t name_end = line.find_first_of(" \t", name_at);
		const size_t open = line.find('"', name_end), close = line.find('"', open + 1);

		if (name_at == std::string::npos || close == std::string::npos)
			continue;

		const std::string name = line.substr(name_at, name_end - name_at);
		const size_t described = close - open - 1;
		bool known = false;

		for (const auto &[have, value] : parameters)
			known = known || have == name;
		if (!known && name.size() <= LONGEST_NAME && described > 0 && described <= LONGEST_NAME)
			parameters.emplace_back(name, std::strtof(line.c_str() + close + 1, nullptr));
	}
}

} /* namespace odin::preset */
