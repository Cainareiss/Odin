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

#include "odin/save_folder.hpp"

#include "odin/env.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <system_error>

namespace odin {

std::string
user_data_dir()
{
#if defined(_WIN32)
	const char *const appdata = std::getenv("APPDATA");

	return appdata && appdata[0] ? appdata : std::string();
#elif defined(__APPLE__)
	const std::string home = home_dir();

	return home.empty() ? home : home + "/Library/Application Support";
#else
	const char *const data = std::getenv("XDG_DATA_HOME");
	const std::string home = home_dir();

	if (data && data[0])
		return data;
	return home.empty() ? home : home + "/.local/share";
#endif
}

SaveFolder
SaveFolder::of_game(const std::string &game)
{
	const char *const chosen = std::getenv("ODIN_SAVES");
	const std::string data = user_data_dir();

	if (chosen && chosen[0])
		return std::strcmp(chosen, "none") ? SaveFolder(std::string(chosen) + "/" + game) : SaveFolder();
	if (!data.empty())
		return SaveFolder(data + "/odin/saves/" + game);
	return SaveFolder();
}

std::string
SaveFolder::path(const std::string &name) const
{
	return none() ? std::string() : m_dir + "/" + name;
}

std::optional<std::vector<uint8_t>>
read_kept(const std::string &path, size_t size)
{
	std::FILE *const file = std::fopen(path.c_str(), "rb");

	if (!file)
		return std::nullopt;

	/* One byte more than wanted tells a longer file. */
	std::vector<uint8_t> bytes(size + 1);
	const size_t got = std::fread(bytes.data(), 1, bytes.size(), file);

	std::fclose(file);
	if (got != size) {
		std::fprintf(stderr, "ERROR: '%s' does not hold %zu bytes: not loaded\n", path.c_str(), size);
		return std::nullopt;
	}
	bytes.resize(size);
	return bytes;
}

bool
write_kept(const std::string &path, std::span<const uint8_t> bytes)
{
	const std::filesystem::path target(path);
	const std::filesystem::path temporary(path + ".new");
	std::error_code error;

	if (target.has_parent_path())
		std::filesystem::create_directories(target.parent_path(), error);

	std::FILE *const file = std::fopen(temporary.string().c_str(), "wb");
	bool ok = file && std::fwrite(bytes.data(), 1, bytes.size(), file) == bytes.size();

	if (file && std::fclose(file))
		ok = false;
	if (ok) {
		std::filesystem::rename(temporary, target, error);
		ok = !error;
	}
	if (!ok) {
		std::fprintf(stderr, "ERROR: cannot write '%s'\n", path.c_str());
		std::filesystem::remove(temporary, error);
	}
	return ok;
}

} /* namespace odin */
