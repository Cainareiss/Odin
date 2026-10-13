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

#include <cstdio>
#include <cstring>

#include <jansson.h>

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
	std::printf("Loading %u bytes from '%s'", static_cast<unsigned>(size), path.c_str());
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
	std::printf("Loading section %s, %s", name, type);

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
				std::memcpy(&out[base + (i & 1) * step + j * 2], &(*file)[j], step);
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

std::optional<Game>
GameList::load_game(const std::string &rom_dir, const std::string &name) const
{
	const json_t *const rom = m_root ? find(name) : nullptr;
	const json_t *const sections = json_object_get(rom, "sections");
	const size_t num = json_array_size(sections);
	Game game;

	if (!rom || !num)
		return std::nullopt;
	game.name = name;
	game.machine = json_string_value(json_object_get(rom, "mach"));
	for (size_t i = 0; i < num; i++) {
		const json_t *const section = json_array_get(sections, i);
		auto bytes = load_section(section, rom_dir + "/" + name);

		if (!bytes)
			return std::nullopt;
		game.sections.emplace_back(json_string_value(json_object_get(section, "name")), std::move(*bytes));
	}
	return game;
}

} /* namespace odin */
