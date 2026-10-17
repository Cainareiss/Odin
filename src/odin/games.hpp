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

#include "odin/roles.hpp"

struct json_t;

namespace odin {

/* A switch of a player on the JVS I/O board: the player, 0 or 1, and its
 * bit in the word of that player (see MieBoard::Inputs). */
struct JvsSwitch {
	uint8_t player;
	uint16_t mask;
};

/* A control of the cabinet of a game, with the name its INPUT TEST gives
 * it, and what it drives on the JVS I/O board:
 *
 *  - BUTTON: one switch ("switch" : "1P PUSH1" in the list);
 *  - ANALOG: an analog channel, 0 to 7, and the way of the control that
 *    gives its low values ("analog" : 0, "low" : "left"); up is away from
 *    the player, down towards;
 *  - POSITIONS: a lever whose each position closes some switches, none
 *    for some ("positions" : [ { "name" : "1", "switches" : [...] }, ...]).
 *
 * "id" is the name the settings give it (odin.ini: [controls.<game>],
 * "<id> = ..."; a lever has "<id>_down" and "<id>_up", and an analog
 * control that is not a pedal the halves of its way, "<id>_left" and
 * "<id>_right" or "<id>_up" and "<id>_down"): small letters,
 * figures and _, its own in the game. "role" is what the control does
 * (roles.hpp), which gives its keys and pad inputs until the player
 * changes them: a role that is pressed or not for a button, one that has a
 * position for an analog channel, "gear" for a lever. "player" : 2 for a
 * control of player 2, player 1 else. */
struct GameControl {
	enum class Type { BUTTON, ANALOG, POSITIONS };
	enum class Low { LEFT, RIGHT, UP, DOWN, RELEASED, PRESSED };
	struct Position {
		std::string name;
		std::vector<JvsSwitch> switches;
	};

	/* The ids of what every cabinet has, which no control of a game takes:
	 * the SERVICE switch of each player, the coins, TEST of the I/O board,
	 * Test and Service of the main board. */
	static constexpr const char *CABINET_IDS[] = { "1p_service", "2p_service", "coin1", "coin2", "test",
	                                               "board_test", "board_service" };

	/* The suffix of the half of an analog control that goes left or up,
	 * or right or down (positive: the way the axis of a gamepad grows);
	 * none for a pedal. */
	static const char *half(Low low, bool positive)
	{
		if (low == Low::LEFT || low == Low::RIGHT)
			return positive ? "_right" : "_left";
		if (low == Low::UP || low == Low::DOWN)
			return positive ? "_down" : "_up";
		return nullptr;
	}

	std::string name;
	std::string id;
	Type type;
	JvsSwitch button;
	uint8_t analog;
	Low low;
	std::vector<Position> positions;
	Role role;
	uint8_t player;		/* 0 or 1 */
};

/* An output of the I/O board of a cabinet, with the name its OUTPUT TEST
 * gives it: "output", 1 to 32 as JVS counts them (output 1 is bit 7 of the
 * first byte), and "role", what the host does with it: "rumble", the
 * gamepad of the player shakes while it is on; "lamp" and "lock" (a
 * solenoid that holds a control), nothing yet.
 * "player" : 2 for one of player 2. "id" as for a control. */
struct GameOutput {
	enum class Role { LAMP, LOCK, RUMBLE };

	std::string name;
	std::string id;
	unsigned output;	/* 1 to 32 */
	Role role;
	uint8_t player;		/* 0 or 1 */

	/* Whether it is on, among the outputs of the board */
	bool on(std::span<const uint8_t> outputs) const
	{
		const unsigned bit = output - 1;

		return bit / 8 < outputs.size() && (outputs[bit / 8] & (0x80 >> (bit % 8)));
	}
};

/* A game and what its ROMs hold, section by section ("bootrom", "eprom",
 * "maskrom", "mie", "eeprom", "rombd_eeprom"). */
struct Game {
	std::string name;
	std::string machine;
	/* The JVS I/O board of its cabinet: "io" in the list, 837-13551 when
	 * the list does not say. */
	std::string io_board;
	/* The controls of its cabinet: "controls" in the list, none when the
	 * list does not say. */
	std::vector<GameControl> controls;
	/* The outputs of its cabinet the host uses: "outputs" in the list,
	 * none when the list does not say. */
	std::vector<GameOutput> outputs;
	/* The 93C46 of the MIE of a new cabinet, the settings of the SYSTEM
	 * MENU as SEGA left them: "mie_eeprom" in the list, 128 bytes in
	 * hexadecimal; empty when the list does not say (a blank chip). */
	std::vector<uint8_t> mie_eeprom;
	/* The cabinet has speakers for the optional sound board as well as for
	 * the main one: "option_speakers" in the list, false when it does not
	 * say. */
	bool option_speakers = false;
	std::vector<std::pair<std::string, std::vector<uint8_t>>> sections;

	/* The bytes of a section; empty when the game has no such section. */
	std::span<const uint8_t> section(const std::string &name) const;
};

/* The list of the games, a JSON file (odin-games.json). A game has sections
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
