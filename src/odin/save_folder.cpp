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

#include <cctype>
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

std::string
user_path(const char *variable, const std::string &chosen, const char *name)
{
	const char *const given = std::getenv(variable);
	const std::string data = user_data_dir();

	if (given && given[0])
		return std::strcmp(given, "none") ? given : "";
	if (!chosen.empty())
		return chosen;
	return data.empty() ? data : data + "/odin/" + name;
}

std::string
artwork_folder(const std::string &chosen)
{
	return user_path("ODIN_ARTWORK", chosen, "artwork");
}

std::string
artwork_file(const std::string &folder, const std::string &game, const std::string &kind)
{
	const auto lower = [](std::string text) {
		for (char &c : text)
			c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
		return text;
	};
	const std::string wanted = lower(game + "_" + kind + ".png");
	std::error_code error;

	if (folder.empty())
		return {};
	if (const std::string exact = folder + "/" + game + "_" + kind + ".png"; std::filesystem::is_regular_file(exact, error))
		return exact;
	for (auto it = std::filesystem::directory_iterator(folder, error); !error && it != std::filesystem::directory_iterator();
	     it.increment(error))
		if (lower(it->path().filename().string()) == wanted)
			return it->path().string();
	return {};
}

std::string
SaveFolder::root(const std::string &chosen)
{
	return user_path("ODIN_SAVES", chosen, "saves");
}

SaveFolder
SaveFolder::of_game(const std::string &game, const std::string &chosen)
{
	const std::string folder = root(chosen);

	return folder.empty() ? SaveFolder() : SaveFolder(folder + "/" + game);
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
