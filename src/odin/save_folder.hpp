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

#ifndef ODIN_SAVE_FOLDER_HPP
#define ODIN_SAVE_FOLDER_HPP

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace odin {

/* Where the user keeps the data of programs: %APPDATA% on Windows,
 * ~/Library/Application Support on macOS, XDG_DATA_HOME or ~/.local/share
 * elsewhere. Empty when it cannot be told. */
std::string user_data_dir();

/* The folder where a game keeps what its board holds without power, one
 * file for each memory. Nothing is read or written when there is none. */
class SaveFolder {
public:
	/* <folder>/<game> with ODIN_SAVES=<folder>; else <chosen>/<game>, the
	 * folder the settings give; else <data>/odin/saves/<game>, <data>
	 * being %APPDATA% on Windows, ~/Library/Application Support on macOS,
	 * XDG_DATA_HOME or ~/.local/share elsewhere. None with
	 * ODIN_SAVES=none, or when there is no such place. */
	static SaveFolder of_game(const std::string &game, const std::string &chosen = "");
	/* The folder of the folders of the games; empty: none */
	static std::string root(const std::string &chosen = "");

	SaveFolder() = default;
	explicit SaveFolder(std::string dir) : m_dir(std::move(dir)) {}

	bool none() const { return m_dir.empty(); }
	const std::string &dir() const { return m_dir; }
	/* The path of a file of the folder; empty when there is none. */
	std::string path(const std::string &name) const;

private:
	std::string m_dir;
};

/* The bytes of a file that holds exactly `size` of them. Nothing when it is
 * missing, or holds another number of bytes (said on stderr). */
std::optional<std::vector<uint8_t>> read_kept(const std::string &path, size_t size);

/* Writes a file through a temporary one that then takes its place: a write
 * cut short leaves the old file whole. The folders are made when missing.
 * False, said on stderr, when the file could not be written. */
bool write_kept(const std::string &path, std::span<const uint8_t> bytes);

} /* namespace odin */

#endif /* ODIN_SAVE_FOLDER_HPP */
