/*
 * Odin
 * Copyright (C) 2026 Réda Chérif-Touil
 *
 * Valkyrie
 * Copyright (C) 2011-2013, Stefano Teso
 *
 * Valkyrie is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * Valkyrie is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with Valkyrie.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef ODIN_OPTIONS_HPP
#define ODIN_OPTIONS_HPP

#include <optional>
#include <string>
#include <vector>

#include "odin/audio_out.hpp"
#include "odin/display.hpp"
#include "odin/games.hpp"
#include "odin/settings.hpp"

namespace odin {

/* How the frames wait for the display by default, and the names of odin.ini */
inline constexpr Display::Vsync VSYNC = Display::Vsync::OFF;
inline constexpr const char *VSYNCS[] = { "off", "on", "adaptive" };
/* Around the picture by default, and the names of odin.ini */
inline constexpr Display::Sides SIDES = Display::Sides::BEZEL;
inline constexpr const char *SIDES_NAMES[] = { "off", "blur", "image" };

/* What Odin runs with: the command line, and odin.ini where it says
 * nothing (with_settings) */
struct Options {
	/* The folders of the games and of the sets of files they share (the
	 * BIOS...), rom_files.hpp */
	std::string rom_path, bios_path;
	/* The folder of the saves of the games and that of the save states
	 * odin.ini gives (empty: by default) */
	std::string saves, states;
	/* The folder of the pictures of the games odin.ini gives (empty: by
	 * default), save_folder.hpp */
	std::string artwork;
	std::string game;
	/* The file of what Odin says; empty: none */
	std::string log_file;
	unsigned num_frames = 0;
	std::string state_path;
	std::vector<unsigned> save_frames;
	bool unthrottled = false;
	bool verbose = false;
	/* The speakers of the cabinet unless -o says otherwise: those of the
	 * main board, which get the optional board too where the game opens its
	 * external inputs, and those of the optional board where the cabinet has
	 * them (the two boards on two channels) */
	std::optional<AudioOut::Mode> sound = AudioOut::Mode::MAIN;
	bool sound_asked = false;
	/* [sound] of odin.ini: the device (empty: the system's), the volume
	 * from 0 to 100, the sound kept while the window is in the background */
	std::string sound_device;
	unsigned volume = 100;
	bool background = true;
	Display::Options display = { 1, 1, 0, false, true, Display::Vsync::OFF, 0, 0 };
	/* [display] vsync: the display gets it when the frames are paced */
	Display::Vsync vsync = VSYNC;
	/* [display] bezel: around the picture */
	Display::Sides sides = SIDES;
	/* What the command line says of the display: odin.ini does not change it */
	struct {
		bool scale = false, anisotropy = false, window = false, fullscreen = false, unthrottled = false;
		bool roms = false, bios = false, verbose = false;
	} given;
};

/* False when there is nothing to run: the help was shown. */
enum class Parsed { RUN, HELP_ASKED, WRONG };

/* The help is shown for -h and for an option that is not right. Without
 * options Odin opens its window and waits. */
Parsed parse_options(int argc, char **argv, Options &options);

/* What the command line says, and odin.ini where it says nothing: read
 * again after the library and each game, which change odin.ini (a line
 * gone, its value by default again) */
Options with_settings(const Options &command, const Settings &settings);

/* How [sound.<game>] says to play the two boards: main, mix or quad */
std::optional<AudioOut::Mode> mode_named(const std::string &name);

/* The way of playing of the page Audio: 0 the main board, 1 both, 2 four
 * channels, -1 the optional board alone */
int played_of(AudioOut::Mode mode);

AudioOut::Mode mode_of(int played);

/* The folder of the save states by default: <data>/odin/states, else the
 * folder Odin is started from (empty) */
std::string default_states();

/* The list of the games: in the folder Odin is started from, else in the
 * home of the user, else where `make install` puts it. */
bool load_game_list(GameList &games);

/* Every key of odin.ini, at its value by default, in the order the file
 * shows them: what holds for every game (INSTALL), then the sound, the
 * controls and the gamepads of each game of the list */
std::vector<Settings::Known> known_settings(const GameList &games);

} /* namespace odin */

#endif /* ODIN_OPTIONS_HPP */
