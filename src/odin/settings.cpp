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

#include "odin/settings.hpp"

#include "odin/save_folder.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <system_error>

namespace odin {

namespace {

std::string_view
trimmed(std::string_view text)
{
	while (!text.empty() && (text.front() == ' ' || text.front() == '\t' || text.front() == '\r'))
		text.remove_prefix(1);
	while (!text.empty() && (text.back() == ' ' || text.back() == '\t' || text.back() == '\r'))
		text.remove_suffix(1);
	return text;
}

} /* namespace */

std::string
Settings::user_file()
{
	const char *const chosen = std::getenv("ODIN_CONFIG");
	const std::string data = user_data_dir();

	if (chosen && chosen[0])
		return std::strcmp(chosen, "none") ? chosen : std::string();
	return data.empty() ? data : data + "/odin/odin.ini";
}

bool
Settings::load(const std::string &path)
{
	std::error_code error;

	if (!std::filesystem::exists(path, error))
		return true;

	std::ifstream file(path, std::ios::binary);
	std::stringstream text;

	text << file.rdbuf();
	if (!file) {
		std::fprintf(stderr, "ERROR: settings: cannot read '%s'\n", path.c_str());
		return false;
	}
	parse(text.str(), path);
	return true;
}

void
Settings::parse(std::string_view text, const std::string &origin)
{
	Lines *section = &m_sections[""];
	unsigned number = 0;

	while (!text.empty()) {
		const size_t end = text.find('\n');
		const std::string_view line = trimmed(text.substr(0, end));

		text.remove_prefix(end == std::string_view::npos ? text.size() : end + 1);
		number++;
		if (line.empty() || line.front() == '#' || line.front() == ';')
			continue;
		if (line.front() == '[' && line.back() == ']' && line.size() > 2) {
			section = &m_sections[std::string(trimmed(line.substr(1, line.size() - 2)))];
			continue;
		}
		const size_t equal = line.find('=');
		const std::string_view key = trimmed(line.substr(0, equal));

		if (equal == std::string_view::npos || key.empty()) {
			std::fprintf(stderr, "ERROR: settings: %s:%u: not a setting: %.*s\n", origin.c_str(), number,
			             static_cast<int>(line.size()), line.data());
			continue;
		}
		section->emplace_back(std::string(key), std::string(trimmed(line.substr(equal + 1))));
	}
}

const Settings::Lines &
Settings::section(const std::string &name) const
{
	static const Lines none;
	const auto found = m_sections.find(name);

	return found == m_sections.end() ? none : found->second;
}

std::string
Settings::changed(std::string_view text, const std::string &section, const std::string &key,
                  const std::optional<std::string> &value)
{
	std::vector<std::string> lines;
	const bool ended = !text.empty() && text.back() == '\n';

	while (!text.empty()) {
		const size_t end = text.find('\n');

		lines.emplace_back(text.substr(0, end));
		text.remove_prefix(end == std::string_view::npos ? text.size() : end + 1);
	}

	/* The lines of the key, and where a new one goes in the section: after
	 * its last line that is not blank, or its header (the lines before the
	 * first header are the section without a name) */
	const std::string eol = !lines.empty() && !lines.front().empty() && lines.front().back() == '\r' ? "\r" : "";
	std::vector<size_t> found;
	std::optional<size_t> place;
	bool inside = section.empty();

	if (inside)
		place = 0;
	for (size_t i = 0; i < lines.size(); i++) {
		const std::string_view line = trimmed(lines[i]);
		const size_t equal = line.find('=');

		if (line.size() > 2 && line.front() == '[' && line.back() == ']') {
			inside = trimmed(line.substr(1, line.size() - 2)) == section;
			if (inside)
				place = i + 1;
			continue;
		}
		if (!inside || line.empty())
			continue;
		place = i + 1;
		if (line.front() != '#' && line.front() != ';' && equal != std::string_view::npos &&
		    trimmed(line.substr(0, equal)) == key)
			found.push_back(i);
	}

	const std::string setting = value ? key + " = " + *value + eol : std::string();

	if (value && !found.empty()) {
		lines[found.back()] = setting;
		found.pop_back();
	} else if (value && place) {
		lines.insert(lines.begin() + static_cast<std::ptrdiff_t>(*place), setting);
	} else if (value) {
		if (!lines.empty() && !trimmed(lines.back()).empty())
			lines.push_back(eol);
		lines.push_back("[" + section + "]" + eol);
		lines.push_back(setting);
	}
	for (auto i = found.rbegin(); i != found.rend(); i++)
		lines.erase(lines.begin() + static_cast<std::ptrdiff_t>(*i));

	std::string out;

	for (size_t i = 0; i < lines.size(); i++) {
		out += lines[i];
		if (i + 1 < lines.size() || ended || value)
			out += '\n';
	}
	return out;
}

bool
Settings::change(const std::string &path, const std::string &section, const std::string &key,
                 const std::optional<std::string> &value)
{
	std::error_code error;
	std::string text;

	if (std::filesystem::exists(path, error)) {
		std::ifstream file(path, std::ios::binary);
		std::stringstream read;

		read << file.rdbuf();
		if (!file) {
			std::fprintf(stderr, "ERROR: settings: cannot read '%s'\n", path.c_str());
			return false;
		}
		text = read.str();
	}

	const std::string out = changed(text, section, key, value);
	const std::string temporary = path + ".new";

	if (const std::filesystem::path folder = std::filesystem::path(path).parent_path(); !folder.empty())
		std::filesystem::create_directories(folder, error);
	{
		std::ofstream file(temporary, std::ios::binary | std::ios::trunc);

		file << out;
		file.close();
		if (!file) {
			std::fprintf(stderr, "ERROR: settings: cannot write '%s'\n", temporary.c_str());
			std::filesystem::remove(temporary, error);
			return false;
		}
	}
	std::filesystem::rename(temporary, path, error);
	if (error) {
		std::fprintf(stderr, "ERROR: settings: cannot replace '%s': %s\n", path.c_str(), error.message().c_str());
		std::filesystem::remove(temporary, error);
		return false;
	}
	m_sections.clear();
	parse(out, path);
	return true;
}

} /* namespace odin */
