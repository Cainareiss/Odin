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

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

#include "odin/host/video/display.hpp"
#include "odin/host/app/games.hpp"
#include "odin/machine/boards/hikaru/hikaru.hpp"
#include "odin/host/input/keyboard.hpp"
#include "odin/host/app/library.hpp"
#include "odin/host/app/log_file.hpp"
#include "odin/host/app/options.hpp"
#include "odin/common/repeats.hpp"
#include "odin/host/app/session.hpp"
#include "odin/host/app/settings.hpp"
#include "odin/host/app/user_files.hpp"

namespace odin {
namespace {

/* A game in the window, from its power-on, or from the state of -l, to its
 * end; the game of an archive Open... gave in `opened`. The options of the
 * command line and odin.ini hold for it, what it changes of them for itself
 * does not stay. */
Ended
play(const Options &given, const std::string &name, const GameList &games, Settings &settings,
     const std::string &settings_file, Display &display, Keyboard &keys, Pointers *pointers, Opened &opened)
{
	Options options = given;

	repeats::begin(options.verbose);
	if (!games.has(name)) {
		std::fprintf(stderr, "ERROR: failed to load '%s': game name not in game list\n", name.c_str());
		return Ended::FAILED;
	}

	/* The archives are let go of once the files are read; the cabinet of
	 * [players.<game>] */
	const std::optional<Game> game = [&] {
		RomFiles files(options.rom_path, options.bios_path);
		std::optional<Game> loaded = games.load_game(files, name);

		if (loaded) {
			const std::string cabinet = cabinet_of(settings, name);

			if (!loaded->use_cabinet(cabinet))
				std::fprintf(stderr, "ERROR: settings: [players.%s]: not right: cabinet = %s\n", name.c_str(),
				             cabinet.c_str());
			else if (cabinet != "standard")
				std::printf("cabinet: %s\n", cabinet.c_str());
			return loaded;
		}
		for (const RomFiles::File &file : games.missing(files, name)) {
			char crc[16] = "";

			if (file.crc)
				std::snprintf(crc, sizeof(crc), ", CRC %08X", *file.crc);
			std::fprintf(stderr, "ERROR: '%s': %s (%llu bytes%s) is missing; MAME keeps it in %s.zip\n", name.c_str(),
			             file.name.c_str(), static_cast<unsigned long long>(file.size), crc,
			             (file.set.empty() ? name : file.set).c_str());
		}
		return loaded;
	}();
	if (!game) {
		std::fprintf(stderr, "ERROR: failed to load '%s': can't load game files\n", name.c_str());
		return Ended::FAILED;
	}
	if (game->machine != "hikaru") {
		std::fprintf(stderr, "ERROR: failed to load '%s': a game of the %s, not of the Hikaru\n", name.c_str(),
		             game->machine.c_str());
		return Ended::FAILED;
	}

	const std::unique_ptr<Hikaru> board = Hikaru::make(*game, options.verbose, link_settings(settings));
	if (!board)
		return Ended::FAILED;

	display.attach(*board, game->title);
	Controls controls(keys, game->controls, game->outputs, settings, game->name);

	controls.set_pointers(pointers);
	board->set_panel(&controls);

	const bool network = network_on(settings);
	std::unique_ptr<OutputsNetwork> outputs =
		network ? OutputsNetwork::open(game->name, game->outputs) : nullptr;

	if (!options.sound_asked && game->option_speakers)
		options.sound = AudioOut::Mode::MIX;
	for (const auto &[key, value] : settings.section("sound." + game->name))
		if (key == "mode" && !options.sound_asked && mode_named(value))
			options.sound = mode_named(value);

	/* The optional board in the mix: the list, or the last lines of
	 * [sound.<game>]; the pads shake unless [players.<game>] says off */
	double option_level = game->option_level;
	const bool vibration = vibration_on(settings, game->name);
	for (const auto &[key, value] : settings.section("sound." + game->name)) {
		if (key == "option_level" && level_named(value))
			option_level = *level_named(value);
		else if (key == "mode" && mode_named(value))
			continue;
		else
			std::fprintf(stderr, "ERROR: settings: [sound.%s]: not right: %s = %s\n", game->name.c_str(),
			             key.c_str(), value.c_str());
	}

	/* Unthrottled frames do not keep time: no sound with -u */
	std::unique_ptr<AudioOut> audio;
	if (options.sound && options.unthrottled && options.sound_asked)
		std::fprintf(stderr, "sound: not with -u\n");
	else if (options.sound && !options.unthrottled)
		audio = AudioOut::make(*options.sound, option_level, options.sound_device);
	if (audio && options.volume != 100)
		audio->set_volume(static_cast<float>(options.volume) / 100.0f);
	if (audio && options.sound == AudioOut::Mode::MIX && option_level != 0)
		std::printf("sound: the optional board at %+g dB in the mix\n", option_level);
	controls.feel_game(*game, vibration);
	controls.set_wheel_force(wheel_force_of(settings, game->name));
	controls.ground(game->grounded);
	if (vibration && (game->option_vibration || game->drive_first_output))
		std::printf("controls: the pad of player 1 shakes as the%s%s of the cabinet (experimental)\n",
		            game->option_vibration ? " vibration device" : "",
		            game->drive_first_output ? (game->option_vibration ? " and the wheel" : " wheel") : "");

	bool started = true;

	board->keep_in(SaveFolder::of_game(game->name, options.saves));
	board->reset();
	if (!options.state_path.empty()) {
		started = board->load_state(options.state_path);
		if (started)
			std::printf("loaded state '%s', frame %u\n", options.state_path.c_str(), board->frame());
		else
			std::fprintf(stderr, "ERROR: failed to load state '%s'\n", options.state_path.c_str());
	}
	Ended ended = Ended::FAILED;

	if (started) {
		Session session(options, *game, *board, display, keys, controls, audio.get(), std::move(outputs), network,
		                settings, settings_file, games);

		std::printf("Running\n");
		ended = session.run();
		opened = session.opened();
	}

	std::printf("Finalizing\n");
	/* Started again with the settings lost, it would ask again */
	if (!board->power_off() && board->restart_asked()) {
		std::fprintf(stderr, "ERROR: '%s': its settings could not be kept: not started again\n", name.c_str());
		ended = Ended::FAILED;
	}
	display.detach();
	repeats::end();
	return ended;
}

} /* namespace */
} /* namespace odin */

int
main(int argc, char **argv)
{
	odin::Options options;
	odin::GameList games;

	/* With the OpenGL driver of Intel loaded, a process of Windows ends
	 * without what its buffers still hold: it goes out here. */
	std::atexit([] { std::fflush(nullptr); });

	/* What Odin says goes to odin.log too, from its first line; on
	 * Windows, with no console of its own after a double click */
	odin::LogFile::leave_own_console();
	if (const std::string log = odin::LogFile::path(); odin::LogFile::start(log))
		options.log_file = log;

	std::printf(" ██████╗ ██████╗ ██╗███╗   ██╗\n"
		    "██╔═══██╗██╔══██╗██║████╗  ██║\n"
		    "██║   ██║██║  ██║██║██╔██╗ ██║\n"
		    "██║   ██║██║  ██║██║██║╚██╗██║\n"
		    "╚██████╔╝██████╔╝██║██║ ╚████║\n"
		    " ╚═════╝ ╚═════╝ ╚═╝╚═╝  ╚═══╝\n\n");
	std::printf("Odin (ᚨ), Copyright(C) 2026 Réda Chérif-Touil\n");
	std::printf("A hard fork of Valkyrie, Copyright(C) 2011-2013 Stefano Teso\n");
	std::printf("Released under the GPL3 License.\n");

	if (const odin::Parsed parsed = odin::parse_options(argc, argv, options); parsed != odin::Parsed::RUN)
		return parsed == odin::Parsed::HELP_ASKED ? 0 : 1;

	if (!odin::load_game_list(games)) {
		std::fprintf(stderr, "ERROR: failed to load the game list\n");
		return 1;
	}

	odin::Settings settings;
	if (const std::string portable = odin::portable_folder(); !portable.empty())
		std::printf("portable: the files of this Odin in '%s'\n", portable.c_str());
	const std::string settings_file = odin::Settings::user_file();
	if (!settings_file.empty() && settings.load(settings_file)) {
		std::printf("settings: '%s'\n", settings_file.c_str());
		settings.know(odin::known_settings(games));
		settings.complete(settings_file);
	}
	odin::Options command = options;
	options = odin::with_settings(command, settings);
	odin::repeats::begin(options.verbose);

	odin::Keyboard keys;
	options.display.vsync = options.unthrottled ? odin::Display::Vsync::OFF : options.vsync;
	const std::unique_ptr<odin::Display> display = odin::Display::make(keys, options.display);
	if (!display)
		return 1;
	/* The light guns and the mice, each read on its own (held only for the
	 * game that names them); none with ODIN_CONTROLLERS=none, as the
	 * gamepads and the mouse */
	const char *const controllers = std::getenv("ODIN_CONTROLLERS");
	const std::unique_ptr<odin::Pointers> pointers =
		controllers && !std::strcmp(controllers, "none") ? nullptr : std::make_unique<odin::Pointers>(display->window());

	/* For the tests: ODIN_GAMES_TEST=<game>,..., these games after the
	 * first, each from its power-on to its frame -n, in the same window */
	std::vector<std::string> next;
	const char *const games_test = std::getenv("ODIN_GAMES_TEST");
	for (std::string list = games_test ? games_test : ""; !list.empty();) {
		const size_t comma = list.find(',');

		next.push_back(list.substr(0, comma));
		list = comma == std::string::npos ? "" : list.substr(comma + 1);
	}

	/* Without a game, the odin::library: a game chosen there goes back to it
	 * when it ends, but by the hotkey that quits. Close game goes back to
	 * it from any game; Open... plays the game of an archive, from the
	 * folder that holds it, as if chosen there. What the odin::library and each
	 * game change in the settings holds for what comes after them. */
	std::string name = options.game, last, failed;
	odin::Opened opened;
	bool chosen = name.empty();
	for (size_t played = 0;; played++) {
		if (name.empty()) {
			opened = odin::library(command, games, settings, settings_file, *display, keys, pointers.get(), last, failed);
			if (opened.game.empty())
				return 0;
			name = opened.game;
			chosen = true;
			options = odin::with_settings(command, settings);
		}

		odin::Options launch = options;
		if (!opened.folder.empty())
			launch.rom_path = opened.folder;
		opened = {};

		odin::Ended ended =
			odin::play(launch, name, games, settings, settings_file, *display, keys, pointers.get(), opened);

		/* The cabinet switched off and on: the game from its power-on */
		while (ended == odin::Ended::RESTART) {
			launch.state_path.clear();
			launch.save_frames.clear();
			ended = odin::play(launch, name, games, settings, settings_file, *display, keys, pointers.get(),
			                   opened);
		}

		/* -l and -s were for the first game */
		command.state_path.clear();
		command.save_frames.clear();
		options = odin::with_settings(command, settings);
		if (ended == odin::Ended::OPENED) {
			name = last = opened.game;
			failed.clear();
			chosen = true;
			continue;
		}
		if (ended == odin::Ended::CLOSED || (chosen && ended != odin::Ended::QUIT)) {
			failed = ended == odin::Ended::FAILED ? name : "";
			last = name;
			name.clear();
			continue;
		}
		if (ended == odin::Ended::FAILED)
			return 1;
		if (ended == odin::Ended::QUIT || played >= next.size())
			return 0;
		name = next[played];
	}
}
