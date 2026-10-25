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

#include "odin/options.hpp"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <vector>

#include <unistd.h>

#include "odin/env.hpp"
#include "odin/hotkeys.hpp"
#include "odin/menu.hpp"
#include "odin/menu_nav.hpp"
#include "odin/netbd_ring.hpp"
#include "odin/user_files.hpp"

namespace odin {

namespace {

std::optional<DisplayVsync>
vsync_named(const std::string &name)
{
	for (unsigned i = 0; i < std::size(VSYNCS); i++)
		if (name == VSYNCS[i])
			return static_cast<DisplayVsync>(i);
	return std::nullopt;
}

const char HELP[] = "Usage: %s [options]\n"
"	-R <path>	Path to the ROM directory\n"
"	-B <path>	Path to the BIOS directory (hikaru.zip...); none: the ROM directory\n"
"	-r <string>	Name of the game to run (none: the library of the games)\n"
"	-n <num>	Stop after the frame num\n"
"	-l <path>	Load a state at startup\n"
"	-s <num>[,...]	Save <game>-<num>.odinstate after these frames\n"
"	-u		Run as fast as the host can, not at 60 frames per second\n"
"	-x <num>	Draw the 3D num times finer than 640 by 480 (1 to 6)\n"
"	-a <num>	Filter the textures with an anisotropy of num (2 to 16); the board has none\n"
"	-w <num>	Make the window num times 640 by 480, as far as the display has room\n"
"	-f		Take the whole display (F11 or Alt+Enter goes back and forth)\n"
"	-o <mode>	Play the sound: main (the speakers of the main board), option (those\n"
"			of the optional board), mix (both on two channels, the optional one\n"
"			at the level of the list or of odin.ini), quad (four) or off; by\n"
"			default those the cabinet has: main, or mix\n"
"	-v		Set verbosity (affects debug messages)\n"
"	-h              Show this help\n";

/* A size of the window in points as odin.ini writes it, <w>x<h>: from
 * 320 by 240 to 16384 by 16384 */
bool
window_size(const std::string &value, unsigned &w, unsigned &h)
{
	char rest = 0;

	return std::sscanf(value.c_str(), "%ux%u%c", &w, &h, &rest) == 2 && w >= 320 && h >= 240 && w <= 16384 &&
	       h <= 16384;
}

/* [display] of odin.ini, where the command line says nothing: the 3D 1 to
 * 6 times finer, the texture filter (1 to 16), the window 1 to 6 times the
 * screen or of a size the user gave it, the whole display, no limit to the
 * frames, the speed in the title. The last line says. */
void
read_display(const Settings &settings, Options &options)
{
	for (const auto &[key, value] : settings.section("display")) {
		char *end = nullptr;
		const long number = std::strtol(value.c_str(), &end, 10);
		const bool whole = !value.empty() && !*end;
		const bool on = value == "on", off = value == "off";
		unsigned w = 0, h = 0;

		if (key == "scale" && whole && number >= 1 && number <= 6) {
			if (!options.given.scale)
				options.display.scale = static_cast<unsigned>(number);
		} else if (key == "anisotropy" && whole && number >= 1 && number <= 16) {
			if (!options.given.anisotropy)
				options.display.anisotropy = static_cast<unsigned>(number);
		} else if (key == "window" && whole && number >= 1 && number <= 6) {
			if (!options.given.window) {
				options.display.window = static_cast<unsigned>(number);
				options.display.window_w = options.display.window_h = 0;
			}
		} else if (key == "window" && window_size(value, w, h)) {
			if (!options.given.window) {
				options.display.window_w = w;
				options.display.window_h = h;
			}
		} else if (key == "fullscreen" && (on || off)) {
			if (!options.given.fullscreen)
				options.display.fullscreen = on;
		} else if (key == "unthrottled" && (on || off)) {
			if (!options.given.unthrottled)
				options.unthrottled = on;
		} else if (key == "show_speed" && (on || off)) {
			options.display.show_speed = on;
		} else if (key == "vsync" && vsync_named(value)) {
			options.vsync = *vsync_named(value);
		} else if (key == "filter" && !value.empty()) {
			options.display.filter = value == "none" ? std::string() : value;
		} else if (key == "bezel" && std::find(std::begin(SIDES_NAMES), std::end(SIDES_NAMES), value) !=
		                                     std::end(SIDES_NAMES)) {
			options.sides = static_cast<DisplaySides>(
				std::find(std::begin(SIDES_NAMES), std::end(SIDES_NAMES), value) - std::begin(SIDES_NAMES));
		} else {
			std::fprintf(stderr, "ERROR: settings: [display]: not right: %s = %s\n", key.c_str(), value.c_str());
		}
	}
}

/* [paths] of odin.ini: the folder of the games where the command line
 * gives none, those of the saves and of the save states; [log]: the board
 * says each access where the command line does not. The last line says.
 * Where neither chooses, the games, the BIOS and the pictures are the roms,
 * bios and artwork folders of the folder of the program, when it has them:
 * a folder made ready with Odin (program_folder). */
void
read_paths(const Settings &settings, Options &options)
{
	for (const auto &[key, value] : settings.section("paths")) {
		if (key == "roms" && !value.empty()) {
			if (!options.given.roms)
				options.rom_path = value;
		} else if (key == "bios" && !value.empty()) {
			if (!options.given.bios)
				options.bios_path = value;
		} else if (key == "saves" && !value.empty()) {
			options.saves = value;
		} else if (key == "artwork" && !value.empty()) {
			options.artwork = value;
		} else if (key == "states" && !value.empty()) {
			options.states = value;
		} else {
			std::fprintf(stderr, "ERROR: settings: [paths]: not right: %s = %s\n", key.c_str(), value.c_str());
		}
	}
	for (const auto &[key, value] : settings.section("log")) {
		if (key == "verbose" && (value == "on" || value == "off")) {
			if (!options.given.verbose)
				options.verbose = value == "on";
		} else {
			std::fprintf(stderr, "ERROR: settings: [log]: not right: %s = %s\n", key.c_str(), value.c_str());
		}
	}

	const std::string program = program_folder();
	const auto beside = [&](std::string &folder, const char *name) {
		std::error_code error;
		const std::string path = program + "/" + name;

		if (folder.empty() && !program.empty() && std::filesystem::is_directory(path, error))
			folder = path;
	};

	beside(options.rom_path, "roms");
	beside(options.bios_path, "bios");
	beside(options.artwork, "artwork");
}

/* [sound] of odin.ini: the device, the volume, the sound in the
 * background. The last line says. */
void
read_sound(const Settings &settings, Options &options)
{
	for (const auto &[key, value] : settings.section("sound")) {
		char *end = nullptr;
		const long number = std::strtol(value.c_str(), &end, 10);

		if (key == "device" && !value.empty())
			options.sound_device = value;
		else if (key == "volume" && !value.empty() && !*end && number >= 0 && number <= 100)
			options.volume = static_cast<unsigned>(number);
		else if (key == "background" && (value == "on" || value == "off"))
			options.background = value == "on";
		else
			std::fprintf(stderr, "ERROR: settings: [sound]: not right: %s = %s\n", key.c_str(), value.c_str());
	}
}

} /* namespace */

/* The help is shown for -h and for an option that is not right. Without
 * options Odin opens its window and waits. */
Parsed
parse_options(int argc, char **argv, Options &options)
{
	int opt = 0;
	bool wrong = false;

	while (opt != 'h' && (opt = getopt(argc, argv, "R:B:r:n:l:s:ux:a:w:fo:vh?")) != -1) {
		switch (opt) {
		case 'R':
			options.rom_path = optarg;
			options.given.roms = true;
			break;
		case 'B':
			options.bios_path = optarg;
			options.given.bios = true;
			break;
		case 'r':
			options.game = optarg;
			break;
		case 'n':
			options.num_frames = static_cast<unsigned>(std::atoi(optarg));
			break;
		case 'l':
			options.state_path = optarg;
			break;
		case 's':
			for (const char *s = optarg; s && *s;) {
				options.save_frames.push_back(static_cast<unsigned>(std::atoi(s)));
				s = std::strchr(s, ',');
				if (s)
					s++;
			}
			break;
		case 'o': {
			const std::string mode = optarg;

			if (mode == "off")
				options.sound.reset();
			else if (mode == "option")
				options.sound = AudioOut::Mode::OPTION;
			else if (mode_named(mode))
				options.sound = mode_named(mode);
			options.sound_asked = true;
			if (mode != "off" && mode != "option" && !mode_named(mode)) {
				opt = 'h';
				wrong = true;
			}
			break;
		}
		case 'u':
			options.unthrottled = options.given.unthrottled = true;
			break;
		case 'x':
			if (const int scale = std::atoi(optarg); scale >= 2 && scale <= 6) {
				options.display.scale = static_cast<unsigned>(scale);
				options.given.scale = true;
			}
			break;
		case 'a':
			if (const int level = std::atoi(optarg); level >= 2 && level <= 16) {
				options.display.anisotropy = static_cast<unsigned>(level);
				options.given.anisotropy = true;
			}
			break;
		case 'w':
			options.display.window = static_cast<unsigned>(std::atoi(optarg));
			options.given.window = true;
			break;
		case 'f':
			options.display.fullscreen = options.given.fullscreen = true;
			break;
		case 'v':
			options.verbose = options.given.verbose = true;
			break;
		default:
			wrong = opt != 'h';
			opt = 'h';
			break;
		}
	}
	if (opt != 'h')
		return Parsed::RUN;
	std::printf(HELP, argv[0]);
	return wrong ? Parsed::WRONG : Parsed::HELP_ASKED;
}

/* What the command line says, and odin.ini where it says nothing: read
 * again after the library and each game, which change odin.ini (a line
 * gone, its value by default again) */
Options
with_settings(const Options &command, const Settings &settings)
{
	Options options = command;

	read_paths(settings, options);
	read_display(settings, options);
	read_sound(settings, options);
	return options;
}

std::optional<AudioOut::Mode>
mode_named(const std::string &name)
{
	for (int played = 0; played < static_cast<int>(std::size(MODE_NAMES)); played++)
		if (name == MODE_NAMES[played])
			return mode_of(played);
	return std::nullopt;
}

std::optional<double>
level_named(const std::string &value)
{
	char *end = nullptr;
	const double db = std::strtod(value.c_str(), &end);

	if (value.empty() || *end || db < Game::LOWEST_LEVEL || db > Game::HIGHEST_LEVEL)
		return std::nullopt;
	return db;
}

bool
vibration_on(const Settings &settings, const std::string &game)
{
	bool on = true;

	for (const auto &[key, value] : settings.section("players." + game))
		if (key == "vibration" && (value == "on" || value == "off"))
			on = value == "on";
	return on;
}

int
played_of(AudioOut::Mode mode)
{
	switch (mode) {
	case AudioOut::Mode::MAIN:
		return 0;
	case AudioOut::Mode::MIX:
		return 1;
	case AudioOut::Mode::QUAD:
		return 2;
	default:
		return -1;
	}
}

AudioOut::Mode
mode_of(int played)
{
	return played == 0 ? AudioOut::Mode::MAIN :
	       played == 2 ? AudioOut::Mode::QUAD : AudioOut::Mode::MIX;
}

/* The folder of the save states by default: <data>/odin/states, else the
 * folder Odin is started from (empty) */
std::string
default_states()
{
	const std::string data = user_data_dir();

	return data.empty() ? data : data + "/odin/states";
}

/* The list of the games: in the folder Odin is started from, else with the
 * program (program_data_folders), else in the home of the user, else where
 * `make install` puts it. */
bool
load_game_list(GameList &games)
{
	const std::string home = home_dir();
	const std::string base = home.empty() ? "." : home;
	std::error_code error;
	std::vector<std::string> paths = { "./odin-games.json" };

	for (const std::string &folder : program_data_folders())
		paths.push_back(folder + "/odin-games.json");
	paths.push_back(base + "/odin-games.json");
	paths.push_back(base + "/.local/share/odin/odin-games.json");
	for (const std::string &path : paths) {
		if (!std::filesystem::exists(path, error))
			continue;
		if (!games.load(path))
			return false;
		std::printf("loading game list from '%s'\n", path.c_str());
		return true;
	}
	std::fprintf(stderr, "ERROR: no list of the games: '%s', nor with the program, '%s' nor '%s'\n",
	             paths.front().c_str(), paths[paths.size() - 2].c_str(), paths.back().c_str());
	return false;
}

/* Every key of odin.ini, at its value by default, in the order the file
 * shows them: what holds for every game (INSTALL), then the sound, the
 * controls and the gamepads of each game of the list */
std::vector<Settings::Known>
known_settings(const GameList &games)
{
	std::vector<Settings::Known> out = {
		{ "display", "scale", "1" }, { "display", "anisotropy", "1" }, { "display", "window", "1" },
		{ "display", "fullscreen", "off" }, { "display", "vsync", VSYNCS[static_cast<unsigned>(VSYNC)] },
		{ "display", "bezel", SIDES_NAMES[static_cast<unsigned>(SIDES)] },
		{ "display", "unthrottled", "off" }, { "display", "show_speed", "on" }, { "display", "filter", "none" },
		{ "sound", "device", "" }, { "sound", "volume", "100" }, { "sound", "background", "on" },
	};

	for (size_t a = 0; a < Hotkeys::ACTIONS; a++) {
		const auto action = static_cast<Hotkeys::Action>(a);

		out.push_back({ "hotkeys", Hotkeys::id(action), Hotkeys::line(Hotkeys::standard(action)) });
	}
	out.insert(out.end(), { { "outputs", "network", "off" }, { "network", "board", "none" },
	                        { "network", "port", std::to_string(NetCable::PORT) }, { "network", "next", "" },
	                        { "network", "cabinet", "1" },
	                        { "paths", "roms", "" }, { "paths", "bios", "" },
	                        { "paths", "saves", SaveFolder::root() }, { "paths", "states", default_states() },
	                        { "paths", "artwork", artwork_folder() },
	                        { "log", "verbose", "off" }, { "menu", "classic", "off" },
	                        { "menu", "animations", "on" }, { "menu", "theme", THEMES[0].key } });
	for (size_t a = 0; a < MenuNav::ACTIONS; a++) {
		const auto action = static_cast<MenuNav::Action>(a);

		out.push_back({ "menu", MenuNav::id(action), MenuNav::standard(action) });
	}
	out.insert(out.end(), { { "gamepads", "dead_zone", std::to_string(PadTuning::DEAD_ZONE) },
	                        { "gamepads", "trigger_threshold", std::to_string(PadTuning::TRIGGER_THRESHOLD) } });
	for (const auto &[name, title] : games.titles("hikaru")) {
		const std::optional<Game> game = games.describe(name);

		if (!game)
			continue;
		if (game->option_speakers || game->option_vibration) {
			char db[16];

			std::snprintf(db, sizeof(db), "%g", game->option_level);
			out.push_back({ "sound." + name, "mode", game->option_speakers ? "mix" : "main" });
			out.push_back({ "sound." + name, "option_level", db });
		}
		for (const auto &[key, value] : ControlPanel(game->controls, Settings(), name).bindings().standard_lines())
			out.push_back({ "controls." + name, key, value });
		out.push_back({ "players." + name, "p1", "" });
		out.push_back({ "players." + name, "p2", "" });
		out.push_back({ "players." + name, "joy1", "" });
		out.push_back({ "players." + name, "joy2", "" });
		for (const GameControl &control : game->controls)
			if (control.role == Role::GUN_X)
				out.push_back({ "players." + name, control.player ? "gun2" : "gun1", "" });
		if (game->shakes())
			out.push_back({ "players." + name, "vibration", "on" });
	}
	return out;
}

} /* namespace odin */
