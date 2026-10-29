/*
 *  ██████╗ ██████╗ ██╗███╗   ██╗
 * ██╔═══██╗██╔══██╗██║████╗  ██║
 * ██║   ██║██║  ██║██║██╔██╗ ██║
 * ██║   ██║██║  ██║██║██║╚██╗██║
 * ╚██████╔╝██████╔╝██║██║ ╚████║
 *  ╚═════╝ ╚═════╝ ╚═╝╚═╝  ╚═══╝
 *
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

#include "odin/host/app/games.hpp"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <initializer_list>
#include <utility>

#include <jansson.h>

#include "odin/common/bytes.hpp"
#include "odin/common/repeats.hpp"
#include "odin/host/app/rom_files.hpp"

namespace odin {

namespace {

/* A file of a section: {"name": ..., "size": ..., "crc": "<8 hexadecimal
 * digits>", "set": ...}, the CRC and the set MAME keeps it in when the
 * list tells them */
std::optional<RomFiles::File>
file_of(const json_t *datum)
{
	const char *const name = json_string_value(json_object_get(datum, "name"));
	const json_int_t size = json_integer_value(json_object_get(datum, "size"));
	const char *const crc = json_string_value(json_object_get(datum, "crc"));
	const char *const set = json_string_value(json_object_get(datum, "set"));

	if (!name || size <= 0)
		return std::nullopt;

	RomFiles::File file = { name, static_cast<uint64_t>(size), std::nullopt, set ? set : "" };

	if (crc) {
		char *end = nullptr;
		const unsigned long value = std::strtoul(crc, &end, 16);

		if (std::strlen(crc) != 8 || *end)
			return std::nullopt;
		file.crc = static_cast<uint32_t>(value);
	}
	return file;
}

std::optional<std::vector<uint8_t>>
load_datum(const json_t *datum, RomFiles &files, const std::string &game)
{
	const std::optional<RomFiles::File> file = file_of(datum);
	std::string where;

	if (!file)
		return std::nullopt;
	std::optional<std::vector<uint8_t>> bytes = files.read(game, *file, where);
	/* A line for each file in the detailed log only: tens of them */
	if (bytes && repeats::detailed())
		std::printf("Loading %u bytes from '%s'\n", static_cast<unsigned>(file->size), where.c_str());
	return bytes;
}

std::optional<std::vector<uint8_t>>
load_section(const json_t *section, RomFiles &files, const std::string &game)
{
	const char *const name = json_string_value(json_object_get(section, "name"));
	const char *const type = json_string_value(json_object_get(section, "type"));
	const json_t *const data = json_object_get(section, "data");
	const size_t num = json_array_size(data);
	std::vector<uint8_t> out;

	if (!name || !type || !num)
		return std::nullopt;
	if (repeats::detailed())
		std::printf("Loading section %s, %s\n", name, type);

	if (!std::strcmp(type, "alternative")) {
		/* They stand for one another: the same size. */
		const json_int_t size = json_integer_value(json_object_get(json_array_get(data, 0), "size"));

		for (size_t i = 0; i < num; i++)
			if (json_integer_value(json_object_get(json_array_get(data, i), "size")) != size)
				return std::nullopt;
		for (size_t i = 0; i < num; i++)
			if (auto file = load_datum(json_array_get(data, i), files, game))
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
			const auto file = load_datum(json_array_get(data, i), files, game);

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
			const auto file = load_datum(json_array_get(data, i), files, game);

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
 * "keys", a list of key names, and "pad", a list of inputs of the
 * gamepad}. */
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
	const json_t *const pad = json_object_get(control, "pad");
	const auto parsed_role = role ? parse_role(role) : std::nullopt;

	if (!name || !name[0] || !is_id(id) || (button != nullptr) + (analog != nullptr) + (positions != nullptr) != 1 ||
	    !parsed_role || (player && json_integer_value(player) != 1 && json_integer_value(player) != 2) ||
	    (keys && (!button || !json_array_size(keys))) || (pad && (!button || !json_array_size(pad))))
		return std::nullopt;
	out.name = name;
	out.id = id;
	out.role = *parsed_role;
	out.player = player ? static_cast<uint8_t>(json_integer_value(player) - 1) : 0;
	for (const auto &[list, names] : { std::pair{ keys, &out.keys }, std::pair{ pad, &out.pad } })
		for (size_t k = 0; k < json_array_size(list); k++) {
			const char *const input = json_string_value(json_array_get(list, k));

			if (!input || !input[0])
				return std::nullopt;
			names->emplace_back(input);
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

/* "link" of a game (Game::Link) */
bool
parse_link(const json_t *link, Game::Link &out)
{
	const json_t *const cabinets = json_object_get(link, "cabinets");

	if (!json_is_object(link) || !json_is_integer(cabinets) || json_integer_value(cabinets) < 2 ||
	    json_integer_value(cabinets) > 8)
		return false;
	out.cabinets = static_cast<unsigned>(json_integer_value(cabinets));

	const json_t *const seat = json_object_get(link, "seat");

	if (!seat)
		return true;

	const char *const in = json_string_value(json_object_get(seat, "in"));
	const auto byte = parse_hex_number(json_object_get(seat, "byte"), 2);
	const auto small = [seat](const char *key, unsigned fallback, unsigned most) -> std::optional<unsigned> {
		const json_t *const value = json_object_get(seat, key);

		if (!value)
			return fallback;
		if (!json_is_integer(value) || json_integer_value(value) < 0 || json_integer_value(value) > most)
			return std::nullopt;
		return static_cast<unsigned>(json_integer_value(value));
	};
	const auto shift = small("shift", 0, 7), bits = small("bits", 8, 8), first = small("first", 0, 255);
	const json_t *const letter = json_object_get(seat, "letter");

	if (!in || (std::strcmp(in, "mie") && std::strcmp(in, "backup")) || !byte || !shift || !bits || !first ||
	    *bits == 0 || *shift + *bits > 8 || (letter && !json_is_boolean(letter)))
		return false;
	out.seat_in = std::strcmp(in, "mie") ? Game::Link::In::BACKUP : Game::Link::In::MIE;
	out.seat_byte = *byte;
	out.seat_shift = *shift;
	out.seat_bits = *bits;
	out.seat_first = *first;
	out.seat_letter = letter && json_is_true(letter);
	return true;
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


/* "title": 1 to MAX_TITLE printable ASCII characters; the name when there
 * is none */
bool
read_title(const json_t *rom, Game &game)
{
	const json_t *const title = json_object_get(rom, "title");

	game.title = game.name;
	if (!title)
		return true;

	const char *const text = json_string_value(title);
	const size_t length = text ? std::strlen(text) : 0;

	if (!length || length > Game::MAX_TITLE ||
	    !std::all_of(text, text + length, [](char c) { return c >= ' ' && c <= '~'; })) {
		std::fprintf(stderr, "ERROR: game '%s': \"title\" is not 1 to %zu printable ASCII characters\n",
		             game.name.c_str(), Game::MAX_TITLE);
		return false;
	}
	game.title = text;
	return true;
}

bool
read_controls(const json_t *rom, Game &game)
{
	const json_t *const controls = json_object_get(rom, "controls");

	if (controls && !json_is_array(controls)) {
		std::fprintf(stderr, "ERROR: game '%s': \"controls\" is not a list\n", game.name.c_str());
		return false;
	}
	for (size_t i = 0; i < json_array_size(controls); i++) {
		auto control = parse_control(json_array_get(controls, i));

		if (!control) {
			std::fprintf(stderr, "ERROR: game '%s': control %zu is not one\n", game.name.c_str(), i + 1);
			return false;
		}
		game.controls.push_back(std::move(*control));
	}
	return true;
}

bool
read_outputs(const json_t *rom, Game &game)
{
	const json_t *const outputs = json_object_get(rom, "outputs");

	if (outputs && !json_is_array(outputs)) {
		std::fprintf(stderr, "ERROR: game '%s': \"outputs\" is not a list\n", game.name.c_str());
		return false;
	}
	for (size_t i = 0; i < json_array_size(outputs); i++) {
		auto output = parse_output(json_array_get(outputs, i));

		if (!output) {
			std::fprintf(stderr, "ERROR: game '%s': output %zu is not one\n", game.name.c_str(), i + 1);
			return false;
		}
		game.outputs.push_back(std::move(*output));
	}
	return true;
}

/* Each id names one thing in the settings of the game */
bool
ids_once(const Game &game)
{
	std::vector<std::string> ids(std::begin(GameControl::CABINET_IDS), std::end(GameControl::CABINET_IDS));
	const auto take = [&ids, &game](const std::string &id) {
		if (std::find(ids.begin(), ids.end(), id) != ids.end()) {
			std::fprintf(stderr, "ERROR: game '%s': the id '%s' is taken twice\n", game.name.c_str(), id.c_str());
			return false;
		}
		ids.push_back(id);
		return true;
	};

	for (const GameControl &control : game.controls) {
		std::vector<std::string> names = control.type == GameControl::Type::POSITIONS
			? std::vector<std::string>{ control.id + "_down", control.id + "_up" }
			: std::vector<std::string>{ control.id };

		if (control.type == GameControl::Type::ANALOG && GameControl::half(control.low, false)) {
			names.push_back(control.id + GameControl::half(control.low, false));
			names.push_back(control.id + GameControl::half(control.low, true));
		}
		for (const std::string &id : names)
			if (!take(id))
				return false;
	}
	for (const GameOutput &output : game.outputs)
		if (!take(output.id))
			return false;
	return true;
}

/* The switches its wiring grounds: none is right too */
bool
grounded_of(const json_t *list, std::vector<JvsSwitch> &out)
{
	if (list && !json_is_array(list))
		return false;
	for (size_t i = 0; i < json_array_size(list); i++) {
		const auto sw = parse_switch(json_array_get(list, i));

		if (!sw)
			return false;
		out.push_back(*sw);
	}
	return true;
}

/* A cabinet of the game but the standard one: the controls of the list,
 * some of their fields in place of theirs */
bool
read_cabinet(const json_t *rom, const json_t *cabinet, size_t c, Game &game)
{
	const char *const name = game.name.c_str();
	const json_t *const controls = json_object_get(rom, "controls");
	const char *const id = json_string_value(json_object_get(cabinet, "id"));
	const char *const cabinet_name = json_string_value(json_object_get(cabinet, "name"));
	json_t *const changes = json_object_get(cabinet, "controls");
	Game::Cabinet kept;

	if (!is_id(id) || !std::strcmp(id, "standard") || !cabinet_name || !cabinet_name[0] ||
	    (changes && !json_is_object(changes)) || !grounded_of(json_object_get(cabinet, "grounded"), kept.grounded) ||
	    std::any_of(game.cabinets.begin(), game.cabinets.end(), [id](const Game::Cabinet &other) {
		    return other.id == id;
	    })) {
		std::fprintf(stderr, "ERROR: game '%s': cabinet %zu is not one\n", name, c + 1);
		return false;
	}
	kept.id = id;
	kept.name = cabinet_name;
	size_t changed = 0;
	for (size_t i = 0; i < json_array_size(controls); i++) {
		json_t *const control = json_deep_copy(json_array_get(controls, i));
		json_t *const change = json_object_get(changes, json_string_value(json_object_get(control, "id")));

		if (change)
			changed++;

		const bool bad = change && (!json_is_object(change) || json_object_update(control, change) != 0);
		std::optional<GameControl> parsed = bad ? std::nullopt : parse_control(control);

		json_decref(control);
		if (!parsed || parsed->id != game.controls[i].id) {
			std::fprintf(stderr, "ERROR: game '%s': cabinet '%s': control %zu is not one\n", name, id, i + 1);
			return false;
		}
		kept.controls.push_back(std::move(*parsed));
	}
	if (changed != json_object_size(changes)) {
		std::fprintf(stderr, "ERROR: game '%s': cabinet '%s': a control it changes is not in the list\n", name, id);
		return false;
	}
	game.cabinets.push_back(std::move(kept));
	return true;
}

/* The switches its wiring grounds, and its other cabinets */
bool
read_cabinets(const json_t *rom, Game &game)
{
	if (!grounded_of(json_object_get(rom, "grounded"), game.grounded)) {
		std::fprintf(stderr, "ERROR: game '%s': \"grounded\" is not a list of switches\n", game.name.c_str());
		return false;
	}

	const json_t *const cabinets = json_object_get(rom, "cabinets");

	if (cabinets && !json_is_array(cabinets)) {
		std::fprintf(stderr, "ERROR: game '%s': \"cabinets\" is not a list\n", game.name.c_str());
		return false;
	}
	for (size_t c = 0; c < json_array_size(cabinets); c++)
		if (!read_cabinet(rom, json_array_get(cabinets, c), c, game))
			return false;
	return true;
}

/* What the optional sound board drives */
bool
read_options(const json_t *rom, Game &game)
{
	const char *const name = game.name.c_str();

	if (const json_t *const speakers = json_object_get(rom, "option_speakers")) {
		if (!json_is_boolean(speakers)) {
			std::fprintf(stderr, "ERROR: game '%s': \"option_speakers\" is not true or false\n", name);
			return false;
		}
		game.option_speakers = json_is_true(speakers);
	}
	if (const json_t *const shaken = json_object_get(rom, "option_vibration")) {
		if (!json_is_boolean(shaken)) {
			std::fprintf(stderr, "ERROR: game '%s': \"option_vibration\" is not true or false\n", name);
			return false;
		}
		game.option_vibration = json_is_true(shaken);
	}
	if (const json_t *const level = json_object_get(rom, "option_level")) {
		const double db = json_number_value(level);

		if (!json_is_number(level) || !(game.option_speakers || game.option_vibration) ||
		    !(db >= Game::LOWEST_LEVEL && db <= Game::HIGHEST_LEVEL)) {
			std::fprintf(stderr, "ERROR: game '%s': \"option_level\" is not %g to %g dB for \"option_speakers\" "
			             "or \"option_vibration\"\n", name, Game::LOWEST_LEVEL, Game::HIGHEST_LEVEL);
			return false;
		}
		game.option_level = db;
	}
	return true;
}

/* The drive board, the card reader and the network board of its cabinet */
bool
read_boards(const json_t *rom, Game &game)
{
	const char *const name = game.name.c_str();

	if (const json_t *const drive = json_object_get(rom, "drive_board")) {
		const json_t *const first = json_object_get(drive, "first_output");

		if (!json_is_integer(first) || json_integer_value(first) < 1 || json_integer_value(first) > 25) {
			std::fprintf(stderr, "ERROR: game '%s': \"drive_board\" has no \"first_output\" of 1 to 25\n", name);
			return false;
		}
		game.drive_first_output = static_cast<unsigned>(json_integer_value(first));
	}
	if (const json_t *const reader = json_object_get(rom, "card_reader")) {
		const char *const model = json_string_value(reader);

		if (!model || std::strcmp(model, "CRP-1231BR-10")) {
			std::fprintf(stderr, "ERROR: game '%s': \"card_reader\" is not \"CRP-1231BR-10\"\n", name);
			return false;
		}
		game.card_reader = model;
	}
	if (const json_t *const link = json_object_get(rom, "link")) {
		if (!parse_link(link, game.link)) {
			std::fprintf(stderr, "ERROR: game '%s': \"link\" is not {\"cabinets\": 2 to 8, \"seat\": {\"in\": \"mie\" "
			             "or \"backup\", \"byte\": \"hhhh\", \"shift\", \"bits\", \"first\", \"letter\"}}\n", name);
			return false;
		}
	}
	return true;
}

bool
read_title_frame(const json_t *rom, Game &game)
{
	if (const json_t *const frame = json_object_get(rom, "title_frame")) {
		if (!json_is_integer(frame) || json_integer_value(frame) < 1) {
			std::fprintf(stderr, "ERROR: game '%s': \"title_frame\" is not a frame\n", game.name.c_str());
			return false;
		}
		game.title_frame = static_cast<unsigned>(json_integer_value(frame));
	}
	return true;
}

/* Its ROM board, and the 93C46 of the MIE of a new cabinet */
bool
read_chips(const json_t *rom, Game &game)
{
	if (json_t *const board = json_object_get(rom, "rom_board")) {
		game.rom_board = parse_rom_board(board);
		if (!game.rom_board) {
			std::fprintf(stderr, "ERROR: game '%s': \"rom_board\" is not one\n", game.name.c_str());
			return false;
		}
	}
	if (const json_t *const chip = json_object_get(rom, "mie_eeprom")) {
		auto bytes = parse_hex(json_string_value(chip));

		if (!bytes || bytes->size() != 128) {
			std::fprintf(stderr, "ERROR: game '%s': \"mie_eeprom\" is not 128 bytes in hexadecimal\n",
			             game.name.c_str());
			return false;
		}
		game.mie_eeprom = std::move(*bytes);
	}
	return true;
}

} /* namespace */

std::string
Game::shaker(unsigned player) const
{
	std::string out;

	for (const GameOutput &output : outputs)
		if (output.role == GameOutput::Role::RUMBLE && output.player == player)
			out += (out.empty() ? "from output " : ", ") + output.name;
	if (out.empty() && player == 0 && option_vibration)
		out = "from the optional board's panel shaker";
	if (out.empty() && player == 0 && drive_first_output)
		out = "from the jolts of the cabinet's wheel";
	return out;
}

bool
Game::shaker_experimental(unsigned player) const
{
	for (const GameOutput &output : outputs)
		if (output.role == GameOutput::Role::RUMBLE && output.player == player)
			return false;
	return player == 0 && (option_vibration || drive_first_output);
}

bool
Game::use_cabinet(const std::string &id)
{
	if (id == "standard")
		return true;
	for (const Cabinet &other : cabinets)
		if (other.id == id) {
			controls = other.controls;
			grounded = other.grounded;
			cabinet = id;
			return true;
		}
	return false;
}

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

		if (machine == json_string_value(json_object_get(rom, "mach")) &&
		    json_array_size(json_object_get(rom, "controls")) > 0)
			out.emplace_back(name, title ? title : name);
	}
	return out;
}

std::vector<RomFiles::File>
GameList::missing(RomFiles &files, const std::string &name) const
{
	const json_t *const sections = json_object_get(find(name), "sections");
	std::vector<RomFiles::File> out;

	for (size_t i = 0; i < json_array_size(sections); i++) {
		const json_t *const section = json_array_get(sections, i);
		const json_t *const data = json_object_get(section, "data");
		const char *const type = json_string_value(json_object_get(section, "type"));
		const bool any = type && !std::strcmp(type, "alternative");
		std::vector<RomFiles::File> gone;

		if (json_is_true(json_object_get(section, "optional")))
			continue;
		for (size_t j = 0; j < json_array_size(data); j++) {
			const std::optional<RomFiles::File> file = file_of(json_array_get(data, j));

			if (!file)
				gone.push_back({ "?", 0, std::nullopt, "" });
			else if (files.find(name, *file).empty())
				gone.push_back(*file);
		}
		/* One of the files of an alternative is enough */
		if (any && gone.size() < json_array_size(data))
			continue;
		out.insert(out.end(), gone.begin(), any && !gone.empty() ? gone.begin() + 1 : gone.end());
	}
	return out;
}

std::optional<Game>
GameList::load_game(RomFiles &files, const std::string &name) const
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
		auto bytes = load_section(section, files, name);

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
	Game game;

	if (!rom || !json_array_size(json_object_get(rom, "sections")))
		return std::nullopt;
	game.name = name;
	game.machine = json_string_value(json_object_get(rom, "mach"));
	const char *const io = json_string_value(json_object_get(rom, "io"));
	game.io_board = io ? io : "837-13551";
	if (!read_title(rom, game) || !read_controls(rom, game) || !read_outputs(rom, game) || !ids_once(game) ||
	    !read_cabinets(rom, game) || !read_options(rom, game) || !read_boards(rom, game) ||
	    !read_title_frame(rom, game) || !read_chips(rom, game))
		return std::nullopt;
	return game;
}

} /* namespace odin */
