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

#ifndef ODIN_GAMES_HPP
#define ODIN_GAMES_HPP

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

struct json_t;

namespace odin {

/* A game and what its ROMs hold, section by section ("bootrom", "eprom",
 * "maskrom", "mie", "eeprom", "rombd_eeprom"). */
struct Game {
	std::string name;
	std::string machine;
	std::vector<std::pair<std::string, std::vector<uint8_t>>> sections;

	/* The bytes of a section; empty when the game has no such section. */
	std::span<const uint8_t> section(const std::string &name) const;
};

/* The list of the games, a JSON file (vk-games.json). A game has sections
 * and a section the files that make it up, each with its size, put together
 * as its type tells:
 *
 *  - "alternative": the first of the files that is there;
 *  - "interleave": the files go in pairs, `amnt` bytes of the first, then
 *    `amnt` bytes of the second, and so on; the pairs follow each other;
 *  - "concatenate": one file after the other. */
class GameList {
public:
	GameList();
	~GameList();
	GameList(const GameList &) = delete;
	GameList &operator=(const GameList &) = delete;

	/* Reads the list. False, with the reason on stderr, when the file is
	 * not one. */
	bool load(const std::string &path);

	bool has(const std::string &name) const;
	/* The game with its files, read from <rom_dir>/<name>/. Nothing when
	 * the game is not in the list, a file is missing or has another size
	 * than the list tells. */
	std::optional<Game> load_game(const std::string &rom_dir, const std::string &name) const;

private:
	static constexpr unsigned VERSION = 1;

	const json_t *find(const std::string &name) const;

	json_t *m_root;
};

} /* namespace odin */

#endif /* ODIN_GAMES_HPP */
