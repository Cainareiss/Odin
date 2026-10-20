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

#include "odin/games.hpp"

#include <algorithm>
#include <cstdio>
#include <cstring>

#include <jansson.h>

#include "odin/bytes.hpp"

namespace odin {

namespace {

/* The whole file, if it is there and has that size. */
std::optional<std::vector<uint8_t>>
read_file(const std::string &path, size_t size)
{
	std::FILE *const file = std::fopen(path.c_str(), "rb");
	std::optional<std::vector<uint8_t>> out;

	if (!file)
		return out;
	std::vector<uint8_t> bytes(size);
	/* One byte more than the size told would be a longer file. */
	if (std::fread(bytes.data(), 1, size, file) == size && std::fgetc(file) == EOF)
		out = std::move(bytes);
	std::fclose(file);
	return out;
}

/* A file of a section: {"name": ..., "size": ...} */
std::optional<std::vector<uint8_t>>
load_datum(const json_t *datum, const std::string &dir)
{
	const char *const name = json_string_value(json_object_get(datum, "name"));
	const size_t size = static_cast<size_t>(json_integer_value(json_object_get(datum, "size")));

	if (!name || !size)
		return std::nullopt;
	const std::string path = dir + "/" + name;
	std::printf("Loading %u bytes from '%s'\n", static_cast<unsigned>(size), path.c_str());
	return read_file(path, size);
}

std::optional<std::vector<uint8_t>>
load_section(const json_t *section, const std::string &dir)
{
	const char *const name = json_string_value(json_object_get(section, "name"));
	const char *const type = json_string_value(json_object_get(section, "type"));
	const json_t *const data = json_object_get(section, "data");
	const size_t num = json_array_size(data);
	std::vector<uint8_t> out;

	if (!name || !type || !num)
		return std::nullopt;
	std::printf("Loading section %s, %s\n", name, type);

	if (!std::strcmp(type, "alternative")) {
		/* They stand for one another: the same size. */
		const json_int_t size = json_integer_value(json_object_get(json_array_get(data, 0), "size"));

		for (size_t i = 0; i < num; i++)
			if (json_integer_value(json_object_get(json_array_get(data, i), "size")) != size)
				return std::nullopt;
		for (size_t i = 0; i < num; i++)
			if (auto file = load_datum(json_array_get(data, i), dir))
				return file;
		return std::nullopt;
	}
	if (!std::strcmp(type, "interleave")) {
		const json_t *const amnt = json_object_get(section, "amnt");
		const size_t step = static_cast<size_t>(json_integer_value(amnt));
		size_t base = 0;

		if (!amnt || (step != 1 && step != 2 && step != 4 && step != 8))
			return std::nullopt;
		for (size_t i = 0; i < num; i++) {
			const auto file = load_datum(json_array_get(data, i), dir);

			if (!file)
				return std::nullopt;
			if (out.size() < base + file->size() * 2)
				out.resize(base + file->size() * 2);
			for (size_t j = 0; j + step <= file->size(); j += step)
				store_bytes(&out[base + (i & 1) * step + j * 2], static_cast<unsigned>(step),
				            load_bytes(&(*file)[j], static_cast<unsigned>(step)));
			if (i & 1)
				base += file->size() * 2;
		}
		return out;
	}
	if (!std::strcmp(type, "concatenate")) {
		for (size_t i = 0; i < num; i++) {
			const auto file = load_datum(json_array_get(data, i), dir);

			if (!file)
				return std::nullopt;
			out.insert(out.end(), file->begin(), file->end());
		}
		return out;
	}
	return std::nullopt;
}

/* "1P START", "2P PUSH3"...: the bits of the word of a player, START
 * SERVICE UP DOWN LEFT RIGHT PUSH1 PUSH2 from bit 15 down, then PUSH3 to
 * PUSH10 from bit 7 down. */
std::optional<JvsSwitch>
parse_switch(const json_t *value)
{
	static const char *const NAMES[] = { "START", "SERVICE", "UP", "DOWN", "LEFT", "RIGHT", "PUSH1", "PUSH2",
	                                      "PUSH3", "PUSH4", "PUSH5", "PUSH6", "PUSH7", "PUSH8", "PUSH9", "PUSH10" };
	const char *const text = json_string_value(value);

	if (!text || (std::strncmp(text, "1P ", 3) && std::strncmp(text, "2P ", 3)))
		return std::nullopt;
	for (unsigned i = 0; i < 16; i++)
		if (!std::strcmp(text + 3, NAMES[i]))
			return JvsSwitch{ static_cast<uint8_t>(text[0] - '1'), static_cast<uint16_t>(0x8000 >> i) };
	return std::nullopt;
}

std::optional<GameControl::Low>
parse_low(const json_t *value)
{
	static const std::pair<const char *, GameControl::Low> WAYS[] = {
		{ "left", GameControl::Low::LEFT }, { "right", GameControl::Low::RIGHT },
		{ "up", GameControl::Low::UP }, { "down", GameControl::Low::DOWN },
		{ "released", GameControl::Low::RELEASED }, { "pressed", GameControl::Low::PRESSED }
	};
	const char *const text = json_string_value(value);

	for (const auto &[name, low] : WAYS)
		if (text && !std::strcmp(text, name))
			return low;
	return std::nullopt;
}

/* Bytes written two hexadecimal figures each */
std::optional<std::vector<uint8_t>>
parse_hex(const char *text)
{
	const auto figure = [](char c) {
		if (c >= '0' && c <= '9')
			return c - '0';
		if (c >= 'a' && c <= 'f')
			return c - 'a' + 10;
		if (c >= 'A' && c <= 'F')
			return c - 'A' + 10;
		return -1;
	};
	std::vector<uint8_t> bytes;

	if (!text)
		return std::nullopt;
	for (const char *c = text; *c; c += 2) {
		if (!c[1] || figure(c[0]) < 0 || figure(c[1]) < 0)
			return std::nullopt;
		bytes.push_back(static_cast<uint8_t>(figure(c[0]) << 4 | figure(c[1])));
	}
	return bytes;
}

/* small letters, figures and _ */
bool
is_id(const char *text)
{
	if (!text || !text[0])
		return false;
	for (const char *c = text; *c; c++)
		if (!((*c >= 'a' && *c <= 'z') || (*c >= '0' && *c <= '9') || *c == '_'))
			return false;
	return true;
}

/* A control: {"name": ..., "id": ..., then "switch", "analog" with "low",
 * or "positions"; "role", and "player" when it is 2; a switch may have
 * "keys", a list of key names}. */
std::optional<GameControl>
parse_control(const json_t *control)
{
	const char *const name = json_string_value(json_object_get(control, "name"));
	const json_t *const button = json_object_get(control, "switch");
	const json_t *const analog = json_object_get(control, "analog");
	const json_t *const positions = json_object_get(control, "positions");
	GameControl out{};

	const char *const id = json_string_value(json_object_get(control, "id"));
	const char *const role = json_string_value(json_object_get(control, "role"));
	const json_t *const player = json_object_get(control, "player");
	const json_t *const keys = json_object_get(control, "keys");
	const auto parsed_role = role ? parse_role(role) : std::nullopt;

	if (!name || !name[0] || !is_id(id) || (button != nullptr) + (analog != nullptr) + (positions != nullptr) != 1 ||
	    !parsed_role || (player && json_integer_value(player) != 1 && json_integer_value(player) != 2) ||
	    (keys && (!button || !json_array_size(keys))))
		return std::nullopt;
	out.name = name;
	out.id = id;
	out.role = *parsed_role;
	out.player = player ? static_cast<uint8_t>(json_integer_value(player) - 1) : 0;
	for (size_t k = 0; k < json_array_size(keys); k++) {
		const char *const key = json_string_value(json_array_get(keys, k));

		if (!key || !key[0])
			return std::nullopt;
		out.keys.emplace_back(key);
	}
	if (button) {
		const auto sw = parse_switch(button);

		if (!sw || !role_is_switch(out.role))
			return std::nullopt;
		out.type = GameControl::Type::BUTTON;
		out.button = *sw;
		return out;
	}
	if (analog) {
		const auto low = parse_low(json_object_get(control, "low"));

		if (!json_is_integer(analog) || json_integer_value(analog) < 0 || json_integer_value(analog) > 7 || !low)
			return std::nullopt;
		if (!role_is_analog(out.role))
			return std::nullopt;
		out.type = GameControl::Type::ANALOG;
		out.analog = static_cast<uint8_t>(json_integer_value(analog));
		out.low = *low;
		return out;
	}
	if (!json_array_size(positions) || out.role != Role::GEAR)
		return std::nullopt;
	out.type = GameControl::Type::POSITIONS;
	for (size_t i = 0; i < json_array_size(positions); i++) {
		const json_t *const position = json_array_get(positions, i);
		const char *const position_name = json_string_value(json_object_get(position, "name"));
		const json_t *const switches = json_object_get(position, "switches");
		GameControl::Position kept{ position_name ? position_name : "", {} };

		if (kept.name.empty() || !json_is_array(switches))
			return std::nullopt;
		for (size_t j = 0; j < json_array_size(switches); j++) {
			const auto sw = parse_switch(json_array_get(switches, j));

			if (!sw)
				return std::nullopt;
			kept.switches.push_back(*sw);
		}
		out.positions.push_back(std::move(kept));
	}
	return out;
}

/* An output: {"name": ..., "id": ..., "output": 1 to 32, "role":
 * "rumble", "mute", "lamp" or "lock", and "player" when it is 2}. */
std::optional<GameOutput>
parse_output(const json_t *output)
{
	static const std::pair<const char *, GameOutput::Role> ROLES[] = {
		{ "lamp", GameOutput::Role::LAMP }, { "lock", GameOutput::Role::LOCK },
		{ "rumble", GameOutput::Role::RUMBLE }, { "mute", GameOutput::Role::MUTE },
	};
	const char *const name = json_string_value(json_object_get(output, "name"));
	const char *const id = json_string_value(json_object_get(output, "id"));
	const json_t *const number = json_object_get(output, "output");
	const char *const role = json_string_value(json_object_get(output, "role"));
	const json_t *const player = json_object_get(output, "player");
	GameOutput out{};

	if (!name || !name[0] || !is_id(id) || !json_is_integer(number) || json_integer_value(number) < 1 ||
	    json_integer_value(number) > 32 || !role ||
	    (player && json_integer_value(player) != 1 && json_integer_value(player) != 2))
		return std::nullopt;
	for (const auto &[role_name, value] : ROLES) {
		if (std::strcmp(role, role_name))
			continue;
		out.name = name;
		out.id = id;
		out.output = static_cast<unsigned>(json_integer_value(number));
		out.role = value;
		out.player = player ? static_cast<uint8_t>(json_integer_value(player) - 1) : 0;
		return out;
	}
	return std::nullopt;
}

/* A number of `bytes` bytes written in hexadecimal, most significant first */
std::optional<uint32_t>
parse_hex_number(const json_t *value, size_t bytes)
{
	const auto figures = parse_hex(json_string_value(value));
	uint32_t number = 0;

	if (!figures || figures->size() != bytes)
		return std::nullopt;
	for (uint8_t b : *figures)
		number = number << 8 | b;
	return number;
}

/* "rom_board" of a game (GameRomBoard) */
std::optional<GameRomBoard>
parse_rom_board(json_t *board)
{
	GameRomBoard out;
	const char *key;
	json_t *value;

	if (!json_is_object(board))
		return std::nullopt;
	json_object_foreach(board, key, value) {
		const std::string name = key;

		if (name == "eprom_mb") {
			const json_int_t mb = json_integer_value(value);

			if (!json_is_integer(value) || (mb != 2 && mb != 4))
				return std::nullopt;
			out.eprom_mb = static_cast<unsigned>(mb);
		} else if (name == "315-5881") {
			out.crypt_key = parse_hex_number(value, 4);
			if (!out.crypt_key)
				return std::nullopt;
		} else if (name == "actel") {
			const auto actel_key = parse_hex_number(json_object_get(value, "key"), 4);
			const auto actel_id = parse_hex_number(json_object_get(value, "id"), 2);

			if (!actel_key || !actel_id || json_object_size(value) != 2)
				return std::nullopt;
			out.has_actel = true;
			out.actel_key = *actel_key;
			out.actel_id = static_cast<uint16_t>(*actel_id);
		} else {
			return std::nullopt;
		}
	}
	if (!out.eprom_mb)
		return std::nullopt;
	return out;
}

} /* namespace */

std::span<const uint8_t>
Game::section(const std::string &wanted) const
{
	for (const auto &[section_name, bytes] : sections)
		if (section_name == wanted)
			return bytes;
	return {};
}

GameList::GameList()
	: m_root(nullptr)
{
}

GameList::~GameList()
{
	if (m_root)
		json_decref(m_root);
}

bool
GameList::load(const std::string &path)
{
	json_error_t error;

	if (m_root)
		json_decref(m_root);
	m_root = nullptr;

	std::FILE *const file = std::fopen(path.c_str(), "rb");
	if (!file) {
		std::fprintf(stderr, "ERROR: could not open game list '%s'\n", path.c_str());
		return false;
	}
	m_root = json_loadf(file, 0, &error);
	std::fclose(file);
	if (!m_root) {
		std::fprintf(stderr, "ERROR: can't load game list '%s'\n", error.text);
		return false;
	}

	const json_t *const version = json_object_get(m_root, "version");
	const json_t *const roms = json_object_get(m_root, "roms");
	bool right = true;

	if (!json_is_integer(version)) {
		std::fprintf(stderr, "ERROR: not game list version\n");
		right = false;
	} else if (json_integer_value(version) != static_cast<json_int_t>(VERSION)) {
		std::fprintf(stderr, "ERROR: invalid game list version: found %u, expected %u\n",
		             static_cast<unsigned>(json_integer_value(version)), VERSION);
		right = false;
	} else if (!json_is_array(roms)) {
		std::fprintf(stderr, "ERROR: invalid game list format\n");
		right = false;
	} else {
		for (size_t i = 0; i < json_array_size(roms); i++) {
			const json_t *const rom = json_array_get(roms, i);

			if (!json_is_string(json_object_get(rom, "name")) ||
			    !json_is_string(json_object_get(rom, "mach"))) {
				std::fprintf(stderr, "ERROR: failed to parse rom list\n");
				right = false;
				break;
			}
		}
	}
	if (!right) {
		json_decref(m_root);
		m_root = nullptr;
	}
	return right;
}

const json_t *
GameList::find(const std::string &name) const
{
	const json_t *const roms = json_object_get(m_root, "roms");

	for (size_t i = 0; i < json_array_size(roms); i++) {
		const json_t *const rom = json_array_get(roms, i);

		if (name == json_string_value(json_object_get(rom, "name")))
			return rom;
	}
	return nullptr;
}

bool
GameList::has(const std::string &name) const
{
	return m_root && find(name) != nullptr;
}

std::vector<std::pair<std::string, std::string>>
GameList::titles(const std::string &machine) const
{
	const json_t *const roms = json_object_get(m_root, "roms");
	std::vector<std::pair<std::string, std::string>> out;

	for (size_t i = 0; i < json_array_size(roms); i++) {
		const json_t *const rom = json_array_get(roms, i);
		const char *const name = json_string_value(json_object_get(rom, "name"));
		const char *const title = json_string_value(json_object_get(rom, "title"));

		if (machine == json_string_value(json_object_get(rom, "mach")))
			out.emplace_back(name, title ? title : name);
	}
	return out;
}

std::optional<Game>
GameList::load_game(const std::string &rom_dir, const std::string &name) const
{
	std::optional<Game> game = describe(name);
	const json_t *const sections = json_object_get(find(name), "sections");

	if (!game)
		return std::nullopt;
	for (size_t i = 0; i < json_array_size(sections); i++) {
		const json_t *const section = json_array_get(sections, i);
		const char *const section_name = json_string_value(json_object_get(section, "name"));
		const json_t *const data = json_object_get(section, "data");

		/* EPROMs of the size the board takes */
		for (size_t j = 0; game->rom_board && section_name && !std::strcmp(section_name, "eprom") &&
		                   j < json_array_size(data); j++) {
			if (json_integer_value(json_object_get(json_array_get(data, j), "size")) !=
			    static_cast<json_int_t>(game->rom_board->eprom_mb) << 20) {
				std::fprintf(stderr, "ERROR: game '%s': an EPROM is not of %u MB\n", name.c_str(),
				             game->rom_board->eprom_mb);
				return std::nullopt;
			}
		}
		auto bytes = load_section(section, rom_dir + "/" + name);

		/* "optional": true, a section the game runs without */
		if (!bytes && json_is_true(json_object_get(section, "optional"))) {
			std::printf("Section %s left out: not found\n", section_name ? section_name : "?");
			continue;
		}
		if (!bytes)
			return std::nullopt;
		game->sections.emplace_back(json_string_value(json_object_get(section, "name")), std::move(*bytes));
	}
	return game;
}

std::optional<Game>
GameList::describe(const std::string &name) const
{
	const json_t *const rom = m_root ? find(name) : nullptr;
	const json_t *const sections = json_object_get(rom, "sections");
	const size_t num = json_array_size(sections);
	Game game;

	if (!rom || !num)
		return std::nullopt;
	game.name = name;
	game.title = name;
	if (const json_t *const title = json_object_get(rom, "title")) {
		const char *const text = json_string_value(title);
		const size_t length = text ? std::strlen(text) : 0;

		if (!length || length > Game::MAX_TITLE ||
		    !std::all_of(text, text + length, [](char c) { return c >= ' ' && c <= '~'; })) {
			std::fprintf(stderr, "ERROR: game '%s': \"title\" is not 1 to %zu printable ASCII characters\n",
			             name.c_str(), Game::MAX_TITLE);
			return std::nullopt;
		}
		game.title = text;
	}
	game.machine = json_string_value(json_object_get(rom, "mach"));
	const char *const io = json_string_value(json_object_get(rom, "io"));
	game.io_board = io ? io : "837-13551";
	const json_t *const controls = json_object_get(rom, "controls");
	if (controls && !json_is_array(controls)) {
		std::fprintf(stderr, "ERROR: game '%s': \"controls\" is not a list\n", name.c_str());
		return std::nullopt;
	}
	for (size_t i = 0; i < json_array_size(controls); i++) {
		auto control = parse_control(json_array_get(controls, i));

		if (!control) {
			std::fprintf(stderr, "ERROR: game '%s': control %zu is not one\n", name.c_str(), i + 1);
			return std::nullopt;
		}
		game.controls.push_back(std::move(*control));
	}
	const json_t *const outputs = json_object_get(rom, "outputs");
	if (outputs && !json_is_array(outputs)) {
		std::fprintf(stderr, "ERROR: game '%s': \"outputs\" is not a list\n", name.c_str());
		return std::nullopt;
	}
	for (size_t i = 0; i < json_array_size(outputs); i++) {
		auto output = parse_output(json_array_get(outputs, i));

		if (!output) {
			std::fprintf(stderr, "ERROR: game '%s': output %zu is not one\n", name.c_str(), i + 1);
			return std::nullopt;
		}
		game.outputs.push_back(std::move(*output));
	}
	/* Each id names one thing in the settings of the game */
	std::vector<std::string> ids(std::begin(GameControl::CABINET_IDS), std::end(GameControl::CABINET_IDS));
	for (const GameControl &control : game.controls) {
		std::vector<std::string> names = control.type == GameControl::Type::POSITIONS
			? std::vector<std::string>{ control.id + "_down", control.id + "_up" }
			: std::vector<std::string>{ control.id };

		if (control.type == GameControl::Type::ANALOG && GameControl::half(control.low, false)) {
			names.push_back(control.id + GameControl::half(control.low, false));
			names.push_back(control.id + GameControl::half(control.low, true));
		}

		for (const std::string &id : names) {
			if (std::find(ids.begin(), ids.end(), id) != ids.end()) {
				std::fprintf(stderr, "ERROR: game '%s': the id '%s' is taken twice\n", name.c_str(), id.c_str());
				return std::nullopt;
			}
			ids.push_back(id);
		}
	}
	for (const GameOutput &output : game.outputs) {
		if (std::find(ids.begin(), ids.end(), output.id) != ids.end()) {
			std::fprintf(stderr, "ERROR: game '%s': the id '%s' is taken twice\n", name.c_str(),
			             output.id.c_str());
			return std::nullopt;
		}
		ids.push_back(output.id);
	}
	if (const json_t *const speakers = json_object_get(rom, "option_speakers")) {
		if (!json_is_boolean(speakers)) {
			std::fprintf(stderr, "ERROR: game '%s': \"option_speakers\" is not true or false\n", name.c_str());
			return std::nullopt;
		}
		game.option_speakers = json_is_true(speakers);
	}
	if (const json_t *const level = json_object_get(rom, "option_level")) {
		const double db = json_number_value(level);

		if (!json_is_number(level) || !game.option_speakers ||
		    !(db >= Game::LOWEST_LEVEL && db <= Game::HIGHEST_LEVEL)) {
			std::fprintf(stderr,
			             "ERROR: game '%s': \"option_level\" is not %g to %g dB for \"option_speakers\"\n",
			             name.c_str(), Game::LOWEST_LEVEL, Game::HIGHEST_LEVEL);
			return std::nullopt;
		}
		game.option_level = db;
	}
	if (const json_t *const shaken = json_object_get(rom, "option_vibration")) {
		if (!json_is_boolean(shaken)) {
			std::fprintf(stderr, "ERROR: game '%s': \"option_vibration\" is not true or false\n", name.c_str());
			return std::nullopt;
		}
		game.option_vibration = json_is_true(shaken);
	}
	if (const json_t *const drive = json_object_get(rom, "drive_board")) {
		const json_t *const first = json_object_get(drive, "first_output");

		if (!json_is_integer(first) || json_integer_value(first) < 1 || json_integer_value(first) > 25) {
			std::fprintf(stderr, "ERROR: game '%s': \"drive_board\" has no \"first_output\" of 1 to 25\n",
			             name.c_str());
			return std::nullopt;
		}
		game.drive_first_output = static_cast<unsigned>(json_integer_value(first));
	}
	if (json_t *const board = json_object_get(rom, "rom_board")) {
		game.rom_board = parse_rom_board(board);
		if (!game.rom_board) {
			std::fprintf(stderr, "ERROR: game '%s': \"rom_board\" is not one\n", name.c_str());
			return std::nullopt;
		}
	}
	if (const json_t *const chip = json_object_get(rom, "mie_eeprom")) {
		auto bytes = parse_hex(json_string_value(chip));

		if (!bytes || bytes->size() != 128) {
			std::fprintf(stderr, "ERROR: game '%s': \"mie_eeprom\" is not 128 bytes in hexadecimal\n", name.c_str());
			return std::nullopt;
		}
		game.mie_eeprom = std::move(*bytes);
	}
	return game;
}

} /* namespace odin */
