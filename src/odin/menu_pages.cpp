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

/* The page Paths of the menu (mock-up v50) */

#include "odin/menu.hpp"

#include <string>

#include "imgui/imgui.h"
#include "odin/env.hpp"
#include "odin/menu_kit.hpp"

namespace odin {

namespace {

constexpr const char *ON_OFF[] = { "On", "Off" };

/* A path as the user knows it: the home folder as ~ */
std::string
shown_path(const std::string &path, const char *none)
{
	const std::string home = home_dir();

	if (path.empty())
		return none;
	if (!home.empty() && path.starts_with(home) && (path.size() == home.size() || path[home.size()] == '/'))
		return "~" + path.substr(home.size());
	return path;
}

} /* namespace */

/* Paths: where the games are, where Odin writes, its own files */
void
Menu::page_paths(MenuKit &kit, MenuState &state) const
{
	const char *const change = state.choosing ? nullptr : "Change...";

	kit.begin_group("GAMES");
	if (kit.value("Games (ROMs)", "a folder for each game · at the next launch",
	              shown_path(state.roms, "not set").c_str(), change, nullptr,
	              state.roms_given ? "set by the command line: -R" : nullptr))
		state.choose = MenuState::Folder::ROMS;
	kit.end_group();

	kit.begin_group("SAVES");
	if (kit.value("Game saves", "EEPROM, cards, operator settings; one folder per game · at the next launch",
	              shown_path(state.saves, "none").c_str(), change, nullptr,
	              state.saves_given ? "set by ODIN_SAVES" : nullptr))
		state.choose = MenuState::Folder::SAVES;
	if (kit.value("Save states", "F4 saves the game where it is, F1 takes it back",
	              shown_path(state.states, "the folder Odin was started from").c_str(), change))
		state.choose = MenuState::Folder::STATES;
	kit.end_group();

	kit.begin_group("ODIN FILES");
	kit.value("Configuration", "odin.ini, written by this menu",
	          shown_path(state.settings_file, "none: ODIN_CONFIG=none").c_str(), nullptr, "READ ONLY");
	kit.value("Shader lists", "the 3D programs each game draws with, made ready when it starts",
	          shown_path(state.shader_lists, "none").c_str(), nullptr, "READ ONLY");
	kit.end_group();

	kit.begin_group("LOG");
	int verbose = state.verbose ? 0 : 1;

	if (kit.choice("Detailed log", "each access of the board, in the terminal; Odin runs far slower · at the next "
	               "launch", ON_OFF, verbose, state.verbose_given ? "set by the command line: -v" : nullptr))
		state.verbose = verbose == 0;
	kit.end_group();
}

} /* namespace odin */
