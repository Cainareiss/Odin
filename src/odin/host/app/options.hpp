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

#include "odin/host/audio/audio_out.hpp"
#include "odin/host/video/display_options.hpp"
#include "odin/host/app/games.hpp"
#include "odin/host/app/settings.hpp"
#include "odin/host/input/seats.hpp"

namespace odin {

/* How the frames wait for the display by default, and the names of odin.ini */
inline constexpr DisplayVsync VSYNC = DisplayVsync::OFF;
inline constexpr const char *VSYNCS[] = { "off", "on", "adaptive" };
/* Around the picture by default, and the names of odin.ini */
inline constexpr DisplaySides SIDES = DisplaySides::BEZEL;
inline constexpr const char *SIDES_NAMES[] = { "off", "blur", "image" };
/* The graphics API by default, and the names of odin.ini */
#if defined(__APPLE__)
inline constexpr DisplayApi API = DisplayApi::VULKAN;
#else
inline constexpr DisplayApi API = DisplayApi::OPENGL;
#endif
inline constexpr const char *APIS[] = { "opengl", "vulkan" };
/* The names of the upscaling modes in odin.ini (DisplayUpscaleMode), raw
 * by default */
inline constexpr const char *UPSCALE_MODES[] = { "raw", "snap", "native" };

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
	 * default), user_files.hpp */
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
	DisplayOptions display = { 1, 1, 0, false, true, DisplayVsync::OFF, 0, 0, "", API, DisplayUpscaleMode::RAW };
	/* [display] vsync: the display gets it when the frames are paced */
	DisplayVsync vsync = VSYNC;
	/* [display] bezel: around the picture */
	DisplaySides sides = SIDES;
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

/* How [sound.<game>] says to play the two boards, as played_of counts
 * them, and the mode of such a name */
inline constexpr const char *MODE_NAMES[] = { "main", "mix", "quad" };
std::optional<AudioOut::Mode> mode_named(const std::string &name);
/* [sound.<game>] option_level: the optional board in the mix, in dB, as far
 * as the list allows (Game::LOWEST_LEVEL to HIGHEST_LEVEL) */
std::optional<double> level_named(const std::string &value);
/* [players.<game>] vibration: the pads shake unless it says off (the last
 * line says) */
bool vibration_on(const Settings &settings, const std::string &game);
/* [players.<game>] wheel_force: how hard the wheel turns as the drive board
 * tells it, in % of its force (0 to MOST_WHEEL_FORCE; at WHEEL_FORCE, when
 * it does not say, the force the board asks at its most is the most of the
 * wheel; above, the wheel reaches its most sooner); the last line says */
inline constexpr int WHEEL_FORCE = 100, MOST_WHEEL_FORCE = 200;
std::optional<int> wheel_force_named(const std::string &value);
int wheel_force_of(const Settings &settings, const std::string &game);
/* [players.<game>] vibration_force: how hard the pads shake, in % of the
 * strength Odin gives each shake (0 to MOST_VIBRATION_FORCE,
 * VIBRATION_FORCE when it does not say); the last line says */
inline constexpr int VIBRATION_FORCE = 100, MOST_VIBRATION_FORCE = 200;
std::optional<int> vibration_force_named(const std::string &value);
int vibration_force_of(const Settings &settings, const std::string &game);
/* [players.<game>] wheel_rotation: the degrees a wheel of the host turns to
 * take the steering of the game from one end to the other, one of
 * WHEEL_ROTATIONS (0: the whole way of the wheel); WHEEL_ROTATION when it
 * does not say, a choice of the emulator as xemu's Chihiro wheels; the last
 * line says */
inline constexpr int WHEEL_ROTATIONS[] = { 0, 270, 360, 540, 900, 1080 };
inline constexpr int WHEEL_ROTATION = 270;
std::optional<int> wheel_rotation_named(const std::string &value);
int wheel_rotation_of(const Settings &settings, const std::string &game);
/* [players.<game>] device1, device2: the kind of device a player chose
 * (DEVICE_NAMES, seats.hpp; nothing: Odin picks); the last line says */
std::optional<InputDevice> device_named(const std::string &value);
std::optional<InputDevice> device_chosen(const Settings &settings, const std::string &game, unsigned player);
/* [players.<game>] cabinet: the id of the cabinet of the game played
 * (Game::cabinets; "standard": that of the list, when it does not say) */
std::string cabinet_of(const Settings &settings, const std::string &game);

/* The way of playing of the page Audio: 0 the main board, 1 both, 2 four
 * channels, -1 the optional board alone */
int played_of(AudioOut::Mode mode);

/* And back */
AudioOut::Mode mode_of(int played);

/* The folder of the save states by default: <data>/odin/states, else the
 * folder Odin is started from (empty) */
std::string default_states();

/* The list of the games: in the folder Odin is started from, else in the
 * home of the user, else where an install to ~/.local puts it. */
bool load_game_list(GameList &games);

/* Every key of odin.ini, at its value by default, in the order the file
 * shows them: what holds for every game (INSTALL), then the sound, the
 * controls and the gamepads of each game of the list */
std::vector<Settings::Known> known_settings(const GameList &games);

} /* namespace odin */

#endif /* ODIN_OPTIONS_HPP */
