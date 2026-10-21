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

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <memory>
#include <optional>
#include <utility>
#include <string>
#include <thread>
#include <vector>

#include <unistd.h>

#include <SDL3/SDL.h>

#include "odin/display.hpp"
#include "odin/env.hpp"
#include "odin/folder_dialog.hpp"
#include "odin/games.hpp"
#include "odin/audio_out.hpp"
#include "odin/hikaru.hpp"
#include "odin/hotkeys.hpp"
#include "odin/controls.hpp"
#include "odin/keyboard.hpp"
#include "odin/log_file.hpp"
#include "odin/menu.hpp"
#include "odin/menu_nav.hpp"
#include "odin/outputs_network.hpp"
#include "odin/rom_files.hpp"
#include "odin/save_folder.hpp"
#include "odin/seats.hpp"
#include "odin/settings.hpp"

namespace {

/* A frame of the cabinet, 60 a second */
constexpr std::chrono::nanoseconds FRAME(1000000000 / 60);

/* How the frames wait for the display by default, and the names of odin.ini */
constexpr odin::Display::Vsync VSYNC = odin::Display::Vsync::OFF;
constexpr const char *VSYNCS[] = { "off", "on", "adaptive" };

std::optional<odin::Display::Vsync>
vsync_named(const std::string &name)
{
	for (unsigned i = 0; i < std::size(VSYNCS); i++)
		if (name == VSYNCS[i])
			return static_cast<odin::Display::Vsync>(i);
	return std::nullopt;
}

struct Options {
	/* The folders of the games and of the sets of files they share (the
	 * BIOS...), rom_files.hpp */
	std::string rom_path, bios_path;
	/* The folder of the saves of the games and that of the save states
	 * odin.ini gives (empty: by default) */
	std::string saves, states;
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
	std::optional<odin::AudioOut::Mode> sound = odin::AudioOut::Mode::MAIN;
	bool sound_asked = false;
	/* [sound] of odin.ini: the device (empty: the system's), the volume
	 * from 0 to 100, the sound kept while the window is in the background */
	std::string sound_device;
	unsigned volume = 100;
	bool background = true;
	odin::Display::Options display = { 1, 1, 0, false, true, odin::Display::Vsync::OFF };
	/* [display] vsync: the display gets it when the frames are paced */
	odin::Display::Vsync vsync = VSYNC;
	/* What the command line says of the display: odin.ini does not change it */
	struct {
		bool scale = false, anisotropy = false, window = false, fullscreen = false, unthrottled = false;
		bool roms = false, bios = false, verbose = false;
	} given;
};

const char HELP[] = "Usage: %s [options]\n"
"	-R <path>	Path to the ROM directory\n"
"	-B <path>	Path to the BIOS directory (hikaru.zip...); none: the ROM directory\n"
"	-r <string>	Name of the game to run (none: Odin opens and waits)\n"
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

/* False when there is nothing to run: the help was shown. */
enum class Parsed { RUN, HELP_ASKED, WRONG };

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
			static const std::pair<const char *, odin::AudioOut::Mode> modes[] = {
				{ "main", odin::AudioOut::Mode::MAIN }, { "option", odin::AudioOut::Mode::OPTION },
				{ "mix", odin::AudioOut::Mode::MIX }, { "quad", odin::AudioOut::Mode::QUAD } };
			bool known = false;
			for (const auto &[name, mode] : modes) {
				if (std::string(optarg) == name) {
					options.sound = mode;
					known = true;
				}
			}
			if (std::string(optarg) == "off") {
				options.sound.reset();
				known = true;
			}
			options.sound_asked = true;
			if (!known) {
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

/* [display] of odin.ini, where the command line says nothing: the 3D 1 to
 * 6 times finer, the texture filter (1 to 16), the window 1 to 6 times the
 * screen, the whole display, no limit to the frames, the speed in the
 * title. The last line says. */
void
read_display(const odin::Settings &settings, Options &options)
{
	for (const auto &[key, value] : settings.section("display")) {
		char *end = nullptr;
		const long number = std::strtol(value.c_str(), &end, 10);
		const bool whole = !value.empty() && !*end;
		const bool on = value == "on", off = value == "off";

		if (key == "scale" && whole && number >= 1 && number <= 6) {
			if (!options.given.scale)
				options.display.scale = static_cast<unsigned>(number);
		} else if (key == "anisotropy" && whole && number >= 1 && number <= 16) {
			if (!options.given.anisotropy)
				options.display.anisotropy = static_cast<unsigned>(number);
		} else if (key == "window" && whole && number >= 1 && number <= 6) {
			if (!options.given.window)
				options.display.window = static_cast<unsigned>(number);
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
		} else {
			std::fprintf(stderr, "ERROR: settings: [display]: not right: %s = %s\n", key.c_str(), value.c_str());
		}
	}
}

/* [paths] of odin.ini: the folder of the games where the command line
 * gives none, those of the saves and of the save states; [log]: the board
 * says each access where the command line does not. The last line says. */
void
read_paths(const odin::Settings &settings, Options &options)
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
}

/* [sound] of odin.ini: the device, the volume, the sound in the
 * background. The last line says. */
void
read_sound(const odin::Settings &settings, Options &options)
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

/* How [sound.<game>] says to play the two boards: main, mix or quad */
std::optional<odin::AudioOut::Mode>
mode_named(const std::string &name)
{
	if (name == "main")
		return odin::AudioOut::Mode::MAIN;
	if (name == "mix")
		return odin::AudioOut::Mode::MIX;
	if (name == "quad")
		return odin::AudioOut::Mode::QUAD;
	return std::nullopt;
}

/* The way of playing of the page Audio: 0 the main board, 1 both, 2 four
 * channels, -1 the optional board alone */
int
played_of(odin::AudioOut::Mode mode)
{
	switch (mode) {
	case odin::AudioOut::Mode::MAIN:
		return 0;
	case odin::AudioOut::Mode::MIX:
		return 1;
	case odin::AudioOut::Mode::QUAD:
		return 2;
	default:
		return -1;
	}
}

odin::AudioOut::Mode
mode_of(int played)
{
	return played == 0 ? odin::AudioOut::Mode::MAIN :
	       played == 2 ? odin::AudioOut::Mode::QUAD : odin::AudioOut::Mode::MIX;
}

/* The folder of the save states by default: <data>/odin/states, else the
 * folder Odin is started from (empty) */
std::string
default_states()
{
	const std::string data = odin::user_data_dir();

	return data.empty() ? data : data + "/odin/states";
}

/* The list of the games: in the folder Odin is started from, else in the
 * home of the user, else where `make install` puts it. */
bool
load_game_list(odin::GameList &games)
{
	const std::string home = odin::home_dir();
	const std::string base = home.empty() ? "." : home;
	std::error_code error;
	const std::string paths[3] = {
		"./odin-games.json",
		base + "/odin-games.json",
		base + "/.local/share/odin/odin-games.json",
	};

	for (const std::string &path : paths) {
		if (!std::filesystem::exists(path, error))
			continue;
		if (!games.load(path))
			return false;
		std::printf("loading game list from '%s'\n", path.c_str());
		return true;
	}
	std::fprintf(stderr, "ERROR: no list of the games: '%s', '%s' nor '%s'\n",
	             paths[0].c_str(), paths[1].c_str(), paths[2].c_str());
	return false;
}

/* Every key of odin.ini, at its value by default, in the order the file
 * shows them: what holds for every game (INSTALL), then the sound, the
 * controls and the gamepads of each game of the list */
std::vector<odin::Settings::Known>
known_settings(const odin::GameList &games)
{
	using Hotkeys = odin::Hotkeys;
	std::vector<odin::Settings::Known> out = {
		{ "display", "scale", "1" }, { "display", "anisotropy", "1" }, { "display", "window", "1" },
		{ "display", "fullscreen", "off" }, { "display", "vsync", VSYNCS[static_cast<unsigned>(VSYNC)] },
		{ "display", "unthrottled", "off" }, { "display", "show_speed", "on" },
		{ "sound", "device", "" }, { "sound", "volume", "100" }, { "sound", "background", "on" },
	};

	for (size_t a = 0; a < Hotkeys::ACTIONS; a++) {
		const auto action = static_cast<Hotkeys::Action>(a);

		out.push_back({ "hotkeys", Hotkeys::id(action), Hotkeys::line(Hotkeys::standard(action)) });
	}
	out.insert(out.end(), { { "outputs", "network", "off" }, { "paths", "roms", "" }, { "paths", "bios", "" },
	                        { "paths", "saves", odin::SaveFolder::root() }, { "paths", "states", default_states() },
	                        { "log", "verbose", "off" }, { "menu", "classic", "off" },
	                        { "menu", "animations", "on" } });
	for (size_t a = 0; a < odin::MenuNav::ACTIONS; a++) {
		const auto action = static_cast<odin::MenuNav::Action>(a);

		out.push_back({ "menu", odin::MenuNav::id(action), odin::MenuNav::standard(action) });
	}
	out.insert(out.end(), { { "gamepads", "dead_zone", std::to_string(odin::PadTuning::DEAD_ZONE) },
	                        { "gamepads", "trigger_threshold", std::to_string(odin::PadTuning::TRIGGER_THRESHOLD) } });
	for (const auto &[name, title] : games.titles("hikaru")) {
		const std::optional<odin::Game> game = games.describe(name);

		if (!game)
			continue;
		if (game->option_speakers || game->option_vibration) {
			char db[16];

			std::snprintf(db, sizeof(db), "%g", game->option_level);
			out.push_back({ "sound." + name, "mode", game->option_speakers ? "mix" : "main" });
			out.push_back({ "sound." + name, "option_level", db });
		}
		for (const auto &[key, value] : odin::ControlPanel(game->controls, odin::Settings(), name).bindings().standard_lines())
			out.push_back({ "controls." + name, key, value });
		out.push_back({ "players." + name, "p1", "" });
		out.push_back({ "players." + name, "p2", "" });
		if (game->shakes())
			out.push_back({ "players." + name, "vibration", "on" });
	}
	return out;
}

/* A game being run: its board, with the window, the keyboard and the
 * gamepads around it.
 *
 * ODIN_MENU_TEST=<n> is a test of the menu: Escape is pressed after every
 * n frames, which opens it, and again once it has drawn its pictures,
 * which closes it. The pictures of the game must be the same. */
/* The make and model of the gamepad of the player shown in the menu; empty
 * when the player has none */
std::string
pad_shown(const odin::MenuState &state)
{
	for (const odin::MenuState::Plugged &pad : state.plugged)
		if (pad.player == static_cast<int>(state.player))
			return pad.guid;
	return {};
}

class Session {
public:
	Session(const Options &options, const odin::Game &game, odin::Hikaru &board, odin::Display &display,
	        odin::Keyboard &keys, odin::Controls &controls, odin::AudioOut *audio,
	        std::unique_ptr<odin::OutputsNetwork> outputs, bool network, odin::Settings &settings,
	        const std::string &settings_file, const odin::GameList &games)
		: m_options(options), m_game(game), m_board(board), m_display(display), m_keys(keys), m_controls(controls),
		  m_audio(audio), m_outputs(std::move(outputs)), m_settings(settings), m_settings_file(settings_file),
		  m_games(games), m_hotkeys(settings), m_nav(settings), m_paused(false),
		  m_next(),
		  m_menu_pictures(0), m_menu_drawn(0),
		  m_menu_test(static_cast<unsigned>(std::max(odin::env_int("ODIN_MENU_TEST", 0), 0)))
	{
		odin::MenuState &state = m_menu_state;

		state.scale = options.display.scale;
		state.anisotropy = options.display.anisotropy;
		state.window = options.display.window ? options.display.window : 1;
		state.unthrottled = options.unthrottled;
		state.show_speed = options.display.show_speed;
		state.vsync = static_cast<unsigned>(options.vsync);
		state.scale_given = options.given.scale;
		state.anisotropy_given = options.given.anisotropy;
		state.window_given = options.given.window;
		state.fullscreen_given = options.given.fullscreen;
		state.unthrottled_given = options.given.unthrottled;
		state.gpu = display.gpu();
		state.kept = !settings_file.empty();
		state.roms = options.rom_path;
		state.roms_given = options.given.roms;
		state.saves = odin::SaveFolder::root(options.saves);
		const char *const saves = std::getenv("ODIN_SAVES");
		state.saves_given = saves && saves[0];
		state.states = m_states = options.states.empty() ? default_states() : options.states;
		state.settings_file = settings_file;
		state.log_file = options.log_file;
		state.shader_lists = std::filesystem::path(display.program_list()).parent_path().string();
		state.verbose = options.verbose;
		state.verbose_given = options.given.verbose;
		state.network = network;
		state.network_failed = network && !m_outputs;
		state.device = options.sound_device;
		state.volume = options.volume;
		state.background = options.background;
		state.silent = options.unthrottled ? "no sound at an unlimited pace (Display)" :
		               !options.sound ? "set by the command line: -o off" :
		               !audio ? "no sound device" : nullptr;
		for (const auto &[name, title] : games.titles("hikaru")) {
			const bool runs = name == game.name;
			const std::optional<odin::Game> other = runs ? std::nullopt : games.describe(name);

			const odin::Game *const described = runs ? &game : other ? &*other : nullptr;

			if (!described || !(described->option_speakers || described->option_vibration))
				continue;

			/* As the cabinet: both boards in the speakers where it has
			 * some for the optional one */
			const int cabinet = described->option_speakers ? 1 : 0;
			odin::MenuState::SoundGame sound = { name, title, cabinet, cabinet, 0.0f, 0.0f, false };

			sound.level = sound.standard = static_cast<float>(described->option_level);
			for (const auto &[key, value] : settings.section("sound." + name)) {
				char *end = nullptr;
				const double db = std::strtod(value.c_str(), &end);

				if (key == "mode" && mode_named(value))
					sound.played = played_of(*mode_named(value));
				else if (key == "option_level" && !value.empty() && !*end && db >= odin::Game::LOWEST_LEVEL &&
				         db <= odin::Game::HIGHEST_LEVEL)
					sound.level = static_cast<float>(db);
			}
			if (runs && options.sound_asked) {
				sound.played = options.sound ? played_of(*options.sound) : -1;
				sound.locked = true;
			}
			state.sound_games.push_back(sound);
		}
		state.games = games.titles("hikaru");
		for (const auto &[key, value] : settings.section("menu")) {
			if (key == "classic" && (value == "on" || value == "off"))
				state.classic = value == "on";
			else if (key == "animations" && (value == "on" || value == "off"))
				state.animations = value == "on";
			else if (!odin::MenuNav::knows(key))
				std::fprintf(stderr, "ERROR: settings: [menu]: not right: %s = %s\n", key.c_str(), value.c_str());
		}
		state.shown = state.running = state.games.size();
		for (size_t g = 0; g < state.games.size(); g++)
			if (state.games[g].first == board.game_name())
				state.shown = state.running = g;
		/* The BIOS alone: the page Input shows the first game */
		if (state.running == state.games.size())
			show_game(0);
	}

	/* Runs the frames until the user quits (true) or the last one asked
	 * is done (false). */
	bool run();

private:
	/* After an event the menu draws this many pictures, one in so many
	 * milliseconds at most */
	static constexpr unsigned MENU_PICTURES = 3;
	static constexpr Uint64 MENU_PICTURE_MS = 16;
	/* How long Undo is offered after Clear mapping; how often the menu is
	 * drawn while an input is caught, its prompt beating (Start mapping
	 * counts its seconds too) */
	static constexpr Uint64 UNDO_MS = 5000;
	static constexpr Uint64 CATCH_TICK_MS = 33;
	/* How long the button that presses is held to skip a control while
	 * Start mapping: a choice of the emulator */
	static constexpr Uint64 SKIP_HOLD_MS = 1000;

	bool frames();
	bool process_events();
	/* What Odin does on a hotkey in the game: true to quit */
	bool act(odin::Hotkeys::Action action);
	bool menu_event(const SDL_Event &event);
	bool navigate(odin::MenuNav::Action action);
	bool presses(int button) const;
	void catch_pad(bool axis, int index, int value);
	void open_menu();
	void close_menu();
	void wait_in_menu();
	void draw_menu();
	void apply(const odin::MenuState &was);
	std::vector<odin::HostInput> kind_changed(std::vector<odin::HostInput> inputs,
	                                          const std::vector<odin::HostInput> &standard, bool to_defaults) const;
	void change_part(const odin::MenuSlot &slot, bool to_defaults);
	void keep(const char *key, const std::optional<std::string> &value);
	void keep(const char *section, const char *key, const std::optional<std::string> &value);
	/* The folder the picker of the system gave */
	void take_folder();
	/* The game whose controls the page Input shows: its name and panel */
	std::string game_shown() const;
	const odin::ControlPanel &panel_shown() const { return m_other ? *m_other : m_controls.panel(); }
	void show_game(size_t game);
	bool catch_input(const SDL_Event &event);
	bool catch_hotkey(const SDL_Event &event);
	void keep_hotkeys();
	void caught(const odin::HostInput &input);
	void change_inputs(bool to_defaults);
	void undo();
	void keep_binding(const odin::Bindings::Line &line, const std::string &game = "");
	void press_escape();
	void load_or_save_state(bool load);
	void save_state_at_frame();
	void limit_frame_rate();

	const Options &m_options;
	const odin::Game &m_game;
	odin::Hikaru &m_board;
	odin::Display &m_display;
	odin::Keyboard &m_keys;
	odin::Controls &m_controls;
	odin::AudioOut *m_audio;	/* nullptr: no sound */
	std::unique_ptr<odin::OutputsNetwork> m_outputs;	/* none: nothing for other programs */
	odin::Settings &m_settings;
	const std::string &m_settings_file;	/* empty: nothing is kept */
	const odin::GameList &m_games;
	odin::Hotkeys m_hotkeys;
	odin::MenuNav m_nav;
	/* The panel of a game shown that does not run, its controls and its
	 * outputs */
	std::vector<odin::GameControl> m_other_controls;
	std::vector<odin::GameOutput> m_other_outputs;
	bool m_other_shakes = false;
	std::array<std::string, 2> m_other_shakers;
	std::optional<odin::ControlPanel> m_other;
	odin::MenuState m_menu_state;
	/* The inputs as Clear mapping found them, and until when Undo puts them back */
	odin::Settings::Lines m_cleared;
	std::string m_cleared_game;
	Uint64 m_undo_until = 0;
	/* Guide is held while an input is caught: the next button goes with it */
	bool m_catch_guide = false;
	/* Start mapping: the button that presses, held since then; it skipped */
	struct Held {
		int button;
		uint64_t since;
		bool skipped;
	};
	std::optional<Held> m_held;
	/* The window is the active one */
	bool m_focused = true;
	/* The folder of the save states (empty: the one Odin is started from),
	 * and the picker of the system that changes the folders */
	std::string m_states;
	odin::FolderDialog m_dialog;
	bool m_menu_opened = false;
	bool m_paused;
	std::chrono::steady_clock::time_point m_next;	/* when the next frame is due */
	unsigned m_menu_pictures;	/* still to draw */
	Uint64 m_menu_drawn;		/* when the last one was */
	unsigned m_menu_test;
};

/* F1 and F4: <game>.odinstate in the folder of the save states */
void
Session::load_or_save_state(bool load)
{
	const std::string path = (m_states.empty() ? "" : m_states + "/") + m_board.game_name() + ".odinstate";
	std::error_code error;

	/* None saved yet: the game goes on */
	if (load && !std::filesystem::exists(path, error)) {
		std::fprintf(stderr, "no state '%s' to load yet\n", path.c_str());
		return;
	}
	if (!load && !m_states.empty())
		std::filesystem::create_directories(m_states, error);
	if (load ? m_board.load_state(path) : m_board.save_state(path))
		std::printf("%s state '%s'\n", load ? "loaded" : "saved", path.c_str());
	else
		std::fprintf(stderr, "ERROR: failed to %s state '%s'\n", load ? "load" : "save", path.c_str());
}

/* The hotkeys of the emulator (hotkeys.hpp): by default Escape or F10
 * opens the menu, which pauses the game, Ctrl+Q quits, P pauses (as in
 * MAME), F1 loads the state of the game and F4 saves it, F11 or Alt+Enter
 * takes the whole display; on a gamepad Guide alone the menu, and with
 * another button the others. The keys are the control panel's too, see
 * keyboard.hpp. True to quit. */
bool
Session::process_events()
{
	SDL_Event event;
	bool quit = false;

	while (SDL_PollEvent(&event)) {
		if (event.type == m_dialog.event()) {
			take_folder();
			continue;
		}
		if (event.type == SDL_EVENT_WINDOW_DISPLAY_CHANGED)
			m_display.display_changed();
		if (event.type == SDL_EVENT_WINDOW_FOCUS_GAINED || event.type == SDL_EVENT_WINDOW_FOCUS_LOST) {
			m_focused = event.type == SDL_EVENT_WINDOW_FOCUS_GAINED;
			if (m_audio)
				m_audio->set_heard(m_focused || m_menu_state.background);
		}
		if (m_display.menu_shown()) {
			quit = menu_event(event) || quit;
			continue;
		}
		if (m_controls.event(event)) {
			const int combo = m_controls.guide_combo();

			if (const auto action = m_hotkeys.pad_action(combo >= 0 ? combo : odin::Hotkeys::NO_PAD))
				quit = act(*action) || quit;
			if (m_controls.guide_tapped())
				if (const auto action = m_hotkeys.pad_action(odin::Hotkeys::GUIDE_ALONE))
					quit = act(*action) || quit;
			continue;
		}
		switch (event.type) {
		case SDL_EVENT_KEY_DOWN:
			m_keys.set(event.key.key, true);
			if (!event.key.repeat)
				if (const auto action = m_hotkeys.key_action(event.key.key, event.key.mod))
					quit = act(*action) || quit;
			break;
		case SDL_EVENT_KEY_UP:
			m_keys.set(event.key.key, false);
			break;
		case SDL_EVENT_QUIT:
			quit = true;
			break;
		default:
			break;
		}
	}
	return quit;
}

bool
Session::act(odin::Hotkeys::Action action)
{
	using Action = odin::Hotkeys::Action;

	switch (action) {
	case Action::MENU:
		open_menu();
		break;
	case Action::PAUSE:
		m_paused = !m_paused;
		if (m_paused)
			m_controls.quiet();
		break;
	case Action::SAVE_STATE:
		load_or_save_state(false);
		break;
	case Action::LOAD_STATE:
		load_or_save_state(true);
		break;
	case Action::FULLSCREEN:
		m_display.toggle_fullscreen();
		break;
	case Action::QUIT:
		return true;
	}
	return false;
}

/* With the menu open, its hotkey closes it and that of quitting quits;
 * that of the whole display still works. The keyboard and the gamepads
 * are followed as they go; all the rest is the menu's. True to quit. */
bool
Session::menu_event(const SDL_Event &event)
{
	using Action = odin::Hotkeys::Action;

	if (m_menu_state.catching && catch_input(event)) {
		m_menu_pictures = MENU_PICTURES;
		return false;
	}
	if (m_menu_state.hotkey_catching && catch_hotkey(event)) {
		m_menu_pictures = MENU_PICTURES;
		return false;
	}

	std::optional<Action> action;
	bool pad = false;

	switch (event.type) {
	case SDL_EVENT_QUIT:
		return true;
	case SDL_EVENT_KEY_DOWN:
	case SDL_EVENT_KEY_UP:
		m_keys.set(event.key.key, event.key.down);
		if (event.key.down && !event.key.repeat)
			action = m_hotkeys.key_action(event.key.key, event.key.mod);
		/* Escape closes a list dropped open first, then the advanced
		 * options */
		if (action == Action::MENU && event.key.key == SDLK_ESCAPE && m_display.menu_popup_open()) {
			action.reset();
		} else if (action == Action::MENU && event.key.key == SDLK_ESCAPE && m_menu_state.advanced) {
			action.reset();
			m_menu_state.advanced = false;
		}
		break;
	case SDL_EVENT_GAMEPAD_ADDED:
	case SDL_EVENT_GAMEPAD_REMOVED:
	case SDL_EVENT_GAMEPAD_AXIS_MOTION:
	case SDL_EVENT_GAMEPAD_BUTTON_DOWN:
	case SDL_EVENT_GAMEPAD_BUTTON_UP: {
		m_controls.event(event);

		const int combo = m_controls.guide_combo();

		action = m_hotkeys.pad_action(combo >= 0 ? combo : odin::Hotkeys::NO_PAD);
		if (m_controls.guide_tapped())
			action = m_hotkeys.pad_action(odin::Hotkeys::GUIDE_ALONE);
		pad = true;
		break;
	}
	case SDL_EVENT_WINDOW_MOUSE_LEAVE:
		m_controls.event(event);
		break;
	default:
		break;
	}
	if (action == Action::QUIT)
		return true;
	if (action == Action::MENU) {
		close_menu();
		return false;
	}
	if (action == Action::FULLSCREEN)
		m_display.toggle_fullscreen();
	/* The menu's own buttons; the keys of a list dropped open are the list's */
	for (const odin::MenuNav::Action nav : m_nav.event(event)) {
		if (action || (!pad && m_display.menu_popup_open()) || !navigate(nav))
			continue;
		close_menu();
		m_menu_pictures = MENU_PICTURES;
		return false;
	}
	/* The gamepads are Odin's, not Dear ImGui's */
	if (!pad)
		m_display.menu_event(event);
	m_menu_pictures = MENU_PICTURES;
	return false;
}

/* What a button of the menu asks, for its next picture: true for B with no
 * list dropped open, back to the game */
bool
Session::navigate(odin::MenuNav::Action action)
{
	using Nav = odin::MenuNav;
	odin::MenuState::Nav &nav = m_menu_state.nav;

	switch (action) {
	case Nav::UP: nav.move--; break;
	case Nav::DOWN: nav.move++; break;
	case Nav::LEFT: nav.side--; break;
	case Nav::RIGHT: nav.side++; break;
	case Nav::PRESS: nav.press = true; break;
	case Nav::CLEAR: nav.clear = true; break;
	case Nav::RESET: nav.reset = true; break;
	case Nav::PAGE_BEFORE: nav.page--; break;
	case Nav::PAGE_AFTER: nav.page++; break;
	case Nav::GAME_BEFORE: nav.game--; break;
	case Nav::GAME_AFTER: nav.game++; break;
	case Nav::BACK:
		/* The advanced options close first, as a list dropped open */
		if (m_menu_state.advanced && !m_display.menu_popup_open()) {
			m_menu_state.advanced = false;
			break;
		}
		if (!m_display.menu_popup_open())
			return true;
		nav.back = true;
		break;
	default:
		break;
	}
	return false;
}

/* The game pauses: no pad shakes, the sound stops where it is */
void
Session::open_menu()
{
	m_controls.quiet();
	if (m_audio)
		m_audio->pause(true);
	/* The page Input shows the gamepad of the player when there is one,
	 * until the user picks a device */
	if (!m_menu_opened)
		for (unsigned player = 0; player < 2; player++)
			m_menu_state.gamepad[player] = !m_controls.pad_name(player).empty();
	m_menu_opened = true;
	m_display.open_menu();
	m_menu_pictures = MENU_PICTURES;
}

/* The keys held now are not the game's; its frames start again now */
void
Session::close_menu()
{
	m_display.close_menu();
	m_nav.reset();
	m_held.reset();
	m_menu_state.advanced = false;
	m_menu_state.nav = {};
	m_keys.swallow();
	if (m_audio)
		m_audio->pause(false);
	m_next = std::chrono::steady_clock::now();
}

/* The page Input waits for an input: Escape lets it go; a key, or on a
 * gamepad a button (Guide held, then another: both), a stick or a trigger
 * from half its way, is the one. True when the event was taken. */
bool
Session::catch_input(const SDL_Event &event)
{
	odin::MenuState &state = m_menu_state;

	/* The buttons of the menu follow what is held, as ever */
	const std::vector<odin::MenuNav::Action> nav = m_nav.event(event);
	const bool back = std::find(nav.begin(), nav.end(), odin::MenuNav::BACK) != nav.end();

	switch (event.type) {
	case SDL_EVENT_KEY_DOWN:
	case SDL_EVENT_KEY_UP:
		m_keys.set(event.key.key, event.key.down);
		if (!event.key.down || event.key.repeat)
			return true;
		if (event.key.key == SDLK_ESCAPE) {
			state.catching.reset();
			state.catch_next.clear();
			state.mapping.reset();
			return true;
		}
		/* A gamepad is caught: the keys of quitting and of the whole display
		 * work as in the rest of the menu */
		if (state.on_pad()) {
			const auto action = m_hotkeys.key_action(event.key.key, event.key.mod);

			return action != odin::Hotkeys::Action::QUIT && action != odin::Hotkeys::Action::FULLSCREEN;
		}
		/* The keys Odin works itself go to no control of the game */
		if (event.key.key != SDLK_ESCAPE && !m_hotkeys.takes(event.key.key)) {
			odin::HostInput input{};

			input.kind = odin::HostInput::Kind::KEY;
			input.key = event.key.key;
			caught(input);
		}
		return true;
	case SDL_EVENT_GAMEPAD_BUTTON_UP:
		m_controls.event(event);
		m_controls.guide_tapped();
		m_controls.guide_combo();
		if (event.gbutton.button == SDL_GAMEPAD_BUTTON_GUIDE)
			m_catch_guide = false;
		/* The button that presses, let go before it skipped: caught */
		if (m_held && m_held->button == event.gbutton.button) {
			const Held held = *m_held;

			m_held.reset();
			if (!held.skipped)
				catch_pad(false, held.button, 0);
		}
		return true;
	case SDL_EVENT_GAMEPAD_BUTTON_DOWN:
	case SDL_EVENT_GAMEPAD_AXIS_MOTION: {
		m_controls.event(event);
		m_controls.guide_combo();
		/* A key is caught: B of a gamepad lets it go */
		if (!state.on_pad()) {
			if (back) {
				state.catching.reset();
				state.catch_next.clear();
				state.mapping.reset();
			}
			return true;
		}

		const bool axis = event.type == SDL_EVENT_GAMEPAD_AXIS_MOTION;
		const int value = axis ? event.gaxis.value : 0;

		if (axis && std::abs(value) < odin::ControlPanel::PRESSED)
			return true;
		if (!axis && event.gbutton.button == SDL_GAMEPAD_BUTTON_GUIDE) {
			m_catch_guide = true;
			return true;
		}
		/* Start mapping: the button that presses, held, skips the control
		 * (draw_menu); let go sooner, it is caught then */
		if (!axis && state.mapping && !m_catch_guide && presses(event.gbutton.button)) {
			m_held = Held{ event.gbutton.button, SDL_GetTicks(), false };
			return true;
		}
		catch_pad(axis, axis ? event.gaxis.axis : event.gbutton.button, value);
		return true;
	}
	default:
		return false;
	}
}

/* Whether a button of a gamepad presses in the menu (menu_nav.hpp) */
bool
Session::presses(int button) const
{
	const std::vector<odin::HostInput> &inputs = m_nav.inputs(odin::MenuNav::PRESS);

	return std::any_of(inputs.begin(), inputs.end(), [button](const odin::HostInput &in) {
		return in.kind == odin::HostInput::Kind::PAD && in.pad.type == odin::PadInput::Type::BUTTON &&
		       in.pad.index == button && in.pad.with < 0;
	});
}

/* An input of a gamepad caught: a button (held with Guide when it is), or
 * an axis past half its way */
void
Session::catch_pad(bool axis, int index, int value)
{
	odin::MenuState &state = m_menu_state;
	odin::HostInput input{};

	if (!state.catching)
		return;
	input.kind = odin::HostInput::Kind::PAD;
	input.pad.index = static_cast<uint8_t>(index);
	input.pad.type = !axis ? odin::PadInput::Type::BUTTON : odin::PadInput::Type::AXIS;
	if (!axis && m_catch_guide)
		input.pad.with = SDL_GAMEPAD_BUTTON_GUIDE;
	/* A stick on a part that is pressed: its half */
	if (axis && index < SDL_GAMEPAD_AXIS_LEFT_TRIGGER && state.catching->part != 0)
		input.pad.type = value < 0 ? odin::PadInput::Type::AXIS_NEGATIVE : odin::PadInput::Type::AXIS_POSITIVE;
	/* A button on an analog control goes on its half: of a pedal, or of
	 * each way in turn */
	if (!axis && state.catching->control >= 0 && state.catching->part == 0) {
		const auto c = static_cast<size_t>(state.catching->control);
		const odin::GameControl &control = panel_shown().controls()[c];

		if (control.type == odin::GameControl::Type::ANALOG) {
			const bool two_ways = odin::GameControl::half(control.low, false);
			const odin::MenuSlot other_way = { state.catching->control, 2, state.catching->role };

			state.catching->part = two_ways ? 1 : 2;
			if (two_ways)
				state.catch_next.insert(state.catch_next.begin(), other_way);
		}
	}
	caught(input);
}

/* The page Hotkeys waits for a key (Ctrl, Alt, Shift or Gui held with it
 * are its own) or for a button pressed with Guide held, or Guide tapped
 * alone; Escape lets it be. True when the event was taken. */
bool
Session::catch_hotkey(const SDL_Event &event)
{
	odin::MenuState &state = m_menu_state;
	const auto action = static_cast<odin::Hotkeys::Action>(state.hotkey_catching->first);
	const size_t slot = state.hotkey_catching->second;
	const std::vector<odin::MenuNav::Action> nav = m_nav.event(event);

	switch (event.type) {
	case SDL_EVENT_KEY_DOWN:
	case SDL_EVENT_KEY_UP:
		m_keys.set(event.key.key, event.key.down);
		if (!event.key.down || event.key.repeat)
			return true;
		if (event.key.key == SDLK_ESCAPE) {
			state.hotkey_catching.reset();
			return true;
		}
		if (slot < odin::Hotkeys::KEYS && !odin::Hotkeys::is_modifier(event.key.key)) {
			m_hotkeys.set_key(action, slot, { event.key.key, event.key.mod });
			state.hotkey_catching.reset();
			keep_hotkeys();
		}
		return true;
	case SDL_EVENT_GAMEPAD_BUTTON_DOWN:
	case SDL_EVENT_GAMEPAD_BUTTON_UP:
	case SDL_EVENT_GAMEPAD_AXIS_MOTION: {
		m_controls.event(event);

		const int combo = m_controls.guide_combo();
		const bool tapped = m_controls.guide_tapped();

		if (slot == odin::Hotkeys::KEYS && (combo >= 0 || tapped)) {
			m_hotkeys.set_pad(action, combo >= 0 ? combo : odin::Hotkeys::GUIDE_ALONE);
			state.hotkey_catching.reset();
			keep_hotkeys();
		} else if (std::find(nav.begin(), nav.end(), odin::MenuNav::BACK) != nav.end()) {
			/* B alone is no shortcut: it lets the catch go */
			state.hotkey_catching.reset();
		}
		return true;
	}
	default:
		return false;
	}
}

/* [hotkeys]: a line for each action not as by default */
void
Session::keep_hotkeys()
{
	for (size_t a = 0; a < odin::Hotkeys::ACTIONS; a++) {
		const auto action = static_cast<odin::Hotkeys::Action>(a);
		const odin::Hotkeys::Binding &binding = m_hotkeys.of(action);
		const std::optional<std::string> line = binding == odin::Hotkeys::standard(action) ? std::nullopt :
		                                        std::optional(odin::Hotkeys::line(binding));

		if (m_settings_file.empty())
			m_settings.set("hotkeys", odin::Hotkeys::id(action), line);
		else
			m_settings.change(m_settings_file, "hotkeys", odin::Hotkeys::id(action), line);
	}
}

/* The input caught takes the place of those of its kind on that part; the
 * next part waiting is caught then */
void
Session::caught(const odin::HostInput &input)
{
	const odin::MenuSlot slot = *m_menu_state.catching;
	const odin::Bindings &bindings = panel_shown().bindings();
	const size_t c = static_cast<size_t>(slot.control);
	std::vector<odin::HostInput> inputs = slot.control < 0 ? bindings.cabinet(slot.role) : bindings.part(c, slot.part);

	m_menu_state.catching.reset();
	std::erase_if(inputs, [&input](const odin::HostInput &in) { return in.kind == input.kind; });
	inputs.push_back(input);
	keep_binding(slot.control < 0 ? bindings.cabinet_line(slot.role, inputs) : bindings.line(c, slot.part, inputs));
	if (!m_menu_state.catch_next.empty()) {
		m_menu_state.catching = m_menu_state.catch_next.front();
		m_menu_state.catch_next.erase(m_menu_state.catch_next.begin());
	}
}

/* Defaults and Clear mapping: the inputs of the device shown, of every
 * part of the controls of the player and their coin (and TEST for player
 * 1), back to their defaults or none */
void
Session::change_inputs(bool to_defaults)
{
	const odin::MenuState &state = m_menu_state;
	const auto changed = [&](const std::vector<odin::HostInput> &inputs, const std::vector<odin::HostInput> &standard) {
		return kind_changed(inputs, standard, to_defaults);
	};
	std::vector<odin::Bindings::Line> lines;
	{
		const odin::Bindings &bindings = panel_shown().bindings();
		const std::vector<odin::GameControl> &controls = panel_shown().controls();

		for (size_t c = 0; c < controls.size(); c++) {
			if (controls[c].player != state.player)
				continue;
			const bool parts = controls[c].type != odin::GameControl::Type::BUTTON;

			for (unsigned part = parts ? 1 : 0; part <= (parts ? 2u : 0u); part++)
				lines.push_back(bindings.line(c, part, changed(bindings.part(c, part), bindings.standard(c, part))));
			if (controls[c].type == odin::GameControl::Type::ANALOG && odin::GameControl::half(controls[c].low, false))
				lines.push_back(bindings.line(c, 0, changed(bindings.part(c, 0), bindings.standard(c, 0))));
		}
		for (const odin::Role role : { state.player ? odin::Role::COIN2 : odin::Role::COIN1, odin::Role::TEST }) {
			if (role == odin::Role::TEST && state.player)
				continue;
			lines.push_back(bindings.cabinet_line(role, changed(bindings.cabinet(role), bindings.cabinet_standard(role))));
		}
	}
	for (const odin::Bindings::Line &line : lines)
		keep_binding(line);
}

/* The inputs of a part without those of the device shown, or with those
 * of its defaults */
std::vector<odin::HostInput>
Session::kind_changed(std::vector<odin::HostInput> inputs, const std::vector<odin::HostInput> &standard,
                      bool to_defaults) const
{
	const auto kind = m_menu_state.on_pad() ? odin::HostInput::Kind::PAD : odin::HostInput::Kind::KEY;

	std::erase_if(inputs, [kind](const odin::HostInput &in) { return in.kind == kind; });
	if (to_defaults)
		for (const odin::HostInput &in : standard)
			if (in.kind == kind)
				inputs.push_back(in);
	return inputs;
}

/* X and Y of a gamepad on the part picked: its inputs of the device shown
 * cleared, or back to their defaults */
void
Session::change_part(const odin::MenuSlot &slot, bool to_defaults)
{
	const odin::Bindings &bindings = panel_shown().bindings();

	if (slot.control < 0) {
		keep_binding(bindings.cabinet_line(slot.role, kind_changed(bindings.cabinet(slot.role),
		                                                          bindings.cabinet_standard(slot.role), to_defaults)));
		return;
	}

	const size_t c = static_cast<size_t>(slot.control);

	keep_binding(bindings.line(c, slot.part, kind_changed(bindings.part(c, slot.part), bindings.standard(c, slot.part),
	                                                      to_defaults)));
}

/* The inputs back as Clear mapping found them */
void
Session::undo()
{
	const std::string section = "controls." + m_cleared_game;
	std::vector<std::string> keys;

	for (const auto &[key, value] : m_settings.section(section))
		keys.push_back(key);
	for (const auto &[key, value] : m_cleared)
		keys.push_back(key);
	for (const std::string &key : keys) {
		std::optional<std::string> was;

		for (const auto &[k, value] : m_cleared)
			if (k == key)
				was = value;
		keep_binding({ key, was }, m_cleared_game);
	}
	m_undo_until = 0;
}

void
Session::keep_binding(const odin::Bindings::Line &line, const std::string &game)
{
	const std::string section = "controls." + (game.empty() ? game_shown() : game);

	if (m_settings_file.empty())
		m_settings.set(section, line.key, line.value);
	else
		m_settings.change(m_settings_file, section, line.key, line.value);
	m_controls.rebind(m_settings);
	if (m_other)
		m_other.emplace(m_other_controls, m_settings, game_shown());
}

std::string
Session::game_shown() const
{
	const odin::MenuState &state = m_menu_state;

	return state.shown < state.games.size() ? state.games[state.shown].first : m_board.game_name();
}

/* Another game in the page Input: a panel of its own, made from the list
 * and the settings, for its inputs only */
void
Session::show_game(size_t game)
{
	odin::MenuState &state = m_menu_state;
	const std::optional<odin::Game> other = game < state.games.size() && game != state.running ?
	                                        m_games.describe(state.games[game].first) : std::nullopt;

	state.shown = other ? game : state.running;
	state.catching.reset();
	state.catch_next.clear();
	m_other.reset();
	if (!other)
		return;
	m_other_controls = other->controls;
	m_other_outputs = other->outputs;
	m_other_shakes = other->shakes();
	m_other_shakers = { other->shaker(0), other->shaker(1) };
	m_other.emplace(m_other_controls, m_settings, other->name);
}

/* The menu draws its pictures, then sleeps until the next event */
void
Session::wait_in_menu()
{
	const Uint64 now = SDL_GetTicks();

	if (m_menu_pictures && now - m_menu_drawn >= MENU_PICTURE_MS) {
		draw_menu();
		m_menu_drawn = now;
		m_menu_pictures--;
		if (m_menu_test && !m_menu_pictures)
			press_escape();
	}
	/* Undo goes when its time is over; the prompt of an input caught
	 * beats, Start mapping counts the seconds of the control it waits for */
	const bool caught = m_menu_state.catching || m_menu_state.mapping;

	if ((m_menu_state.undo && SDL_GetTicks() >= m_undo_until) || m_menu_state.moving)
		m_menu_pictures = std::max(m_menu_pictures, 1u);
	if (caught && SDL_GetTicks() - m_menu_drawn >= CATCH_TICK_MS)
		m_menu_pictures = std::max(m_menu_pictures, 1u);
	if (!m_menu_pictures) {
		if (caught)
			SDL_WaitEventTimeout(nullptr, static_cast<Sint32>(CATCH_TICK_MS));
		else if (m_menu_state.undo)
			SDL_WaitEventTimeout(nullptr, static_cast<Sint32>(m_undo_until - std::min(m_undo_until, SDL_GetTicks())));
		else
			SDL_WaitEvent(nullptr);
		return;
	}

	const Uint64 since = SDL_GetTicks() - m_menu_drawn;

	SDL_WaitEventTimeout(nullptr, since >= MENU_PICTURE_MS ? 0 : static_cast<Sint32>(MENU_PICTURE_MS - since));
}

/* The menu drawn once from what is so now (F11 may have changed the
 * display), then what it changed done */
void
Session::draw_menu()
{
	odin::MenuState &state = m_menu_state;

	state.choosing = m_dialog.open();
	state.fullscreen = m_display.fullscreen();
	m_display.display_size(state.display_w, state.display_h);
	state.refresh = m_display.refresh();
	state.vsync_off = m_options.unthrottled ? "off at an unlimited pace" : nullptr;
	state.refresh_short = state.refresh > 0.0f && state.refresh < odin::Display::LEAST_REFRESH;
	state.panel = &panel_shown();
	state.touched.clear();
	state.now.clear();
	{
		const odin::HostState host = m_controls.host_state();

		for (size_t c = 0; c < state.panel->controls().size(); c++) {
			state.now.push_back(state.panel->now(host, c));
			state.touched.push_back(state.now.back().on);
		}
		for (const odin::Role role : { odin::Role::COIN1, odin::Role::COIN2, odin::Role::TEST })
			state.touched.push_back(state.panel->touched(host, role));
		state.keys_held = m_keys.held();
	}
	for (unsigned player = 0; player < 2; player++) {
		state.pads[player] = m_controls.pad_name(player);
		state.pad_types[player] = m_controls.pad_type(player);
		state.pad_states[player] = m_controls.pad_state(player);
	}
	/* The pills of the pages and the games: the first button of each, as
	 * the pad of player 1 (else 2) names it */
	{
		using Nav = odin::MenuNav;
		const int kind = !state.pads[0].empty() ? state.pad_types[0] :
		                 !state.pads[1].empty() ? state.pad_types[1] : SDL_GAMEPAD_TYPE_UNKNOWN;
		const Nav::Action pills[] = { Nav::PAGE_BEFORE, Nav::PAGE_AFTER, Nav::GAME_BEFORE, Nav::GAME_AFTER,
		                              Nav::PRESS,       Nav::CLEAR,      Nav::RESET };

		for (size_t i = 0; i < state.pills.size(); i++) {
			const std::vector<odin::HostInput> &inputs = m_nav.inputs(pills[i]);
			const auto pad = std::find_if(inputs.begin(), inputs.end(),
			                              [](const odin::HostInput &in) { return in.kind == odin::HostInput::Kind::PAD; });

			state.pills[i] = pad == inputs.end() ? std::string() : odin::Menu::input_name(pad->pad, kind);
		}
	}
	state.undo = m_undo_until && SDL_GetTicks() < m_undo_until;
	/* The button that presses held long enough while Start mapping: Skip */
	state.skip_hold = 0.0f;
	if (m_held && !m_held->skipped) {
		const uint64_t held = SDL_GetTicks() - m_held->since;

		if (!state.mapping) {
			m_held.reset();
		} else if (held >= SKIP_HOLD_MS) {
			state.mapping_since = 0;
			m_held->skipped = true;
		} else {
			state.skip_hold = static_cast<float>(held) / SKIP_HOLD_MS;
		}
	}
	for (size_t a = 0; a < odin::Hotkeys::ACTIONS; a++)
		state.hotkeys[a] = m_hotkeys.of(static_cast<odin::Hotkeys::Action>(a));
	if (m_audio)
		state.devices = m_audio->devices();
	/* The pad shakes as the cabinet of the game shown, unless
	 * [players.<game>] says off (the last line says) */
	state.can_shake = m_other ? m_other_shakes : m_game.shakes();
	state.shake_from = m_other ? m_other_shakers[state.player & 1] : m_game.shaker(state.player & 1);
	state.shake = true;
	for (const auto &[key, value] : m_settings.section("players." + game_shown()))
		if (key == "vibration" && (value == "on" || value == "off"))
			state.shake = value == "on";
	/* The outputs of the game shown, lit as the board left them */
	state.outputs.clear();
	for (const odin::GameOutput &output : m_other ? m_other_outputs : m_game.outputs)
		state.outputs.push_back({ output.name, output.role, !m_other && output.on(m_board.io_outputs()) });
	if (m_outputs) {
		m_outputs->welcome();
		state.programs = m_outputs->clients();
	}
	/* The gamepads plugged in and their players; those of a game that does
	 * not run as its settings would seat them */
	const std::vector<odin::Controls::Plugged> plugged = m_controls.plugged();
	std::array<SDL_JoystickID, 2> seats{};

	if (m_other) {
		std::array<std::string, 2> wanted;
		std::vector<odin::OpenPad> open;

		for (const auto &[key, value] : m_settings.section("players." + game_shown()))
			if (key == "p1" || key == "p2")
				wanted[key[1] - '1'] = value.substr(0, value.find(' '));
		for (const odin::Controls::Plugged &pad : plugged)
			open.push_back({ pad.id, pad.guid });
		seats = odin::seat_pads(open, wanted, {});
	}
	state.plugged.clear();
	for (const odin::Controls::Plugged &pad : plugged)
		state.plugged.push_back({ pad.name, pad.guid, pad.kind,
		                          !m_other ? pad.player : pad.id == seats[0] ? 0 : pad.id == seats[1] ? 1 : -1,
		                          pad.connection });
	/* A player with no gamepad is shown on the keyboard */
	for (unsigned player = 0; player < 2; player++) {
		if (!state.gamepad[player] ||
		    std::any_of(state.plugged.begin(), state.plugged.end(),
		                [player](const odin::MenuState::Plugged &pad) { return pad.player == static_cast<int>(player); }))
			continue;
		state.gamepad[player] = false;
		if (state.player == player) {
			state.catching.reset();
			state.catch_next.clear();
			state.mapping.reset();
		}
	}
	state.tuning = odin::PadTuning::of(m_settings, pad_shown(state), false);
	/* What moves says so as it is drawn */
	state.moving = false;

	const odin::MenuState was = state;

	m_display.draw_menu(state);
	state.nav = {};
	apply(was);

	if (state.hotkeys_default) {
		m_hotkeys.reset();
		keep_hotkeys();
		state.hotkeys_default = false;
		m_menu_pictures = MENU_PICTURES;
	}
	if (state.choose != odin::MenuState::Folder::NONE) {
		const odin::MenuState::Folder folder = state.choose;

		state.no_picker = false;
		m_dialog.ask(m_display.window(), folder == odin::MenuState::Folder::ROMS ? state.roms :
		             folder == odin::MenuState::Folder::SAVES ? state.saves : state.states, static_cast<int>(folder));
		state.choose = odin::MenuState::Folder::NONE;
		m_menu_pictures = MENU_PICTURES;
	}

	if (state.show) {
		show_game(*state.show);
		state.show.reset();
		m_menu_pictures = MENU_PICTURES;
	}
	/* The gamepad a player wants for the game: [players.<game>] p1 or p2 */
	if (state.want) {
		const auto &[guid, name] = *state.want;
		const std::string section = "players." + game_shown(), key = state.player ? "p2" : "p1";
		const std::optional<std::string> value = guid.empty() ? std::nullopt : std::optional(guid + " " + name);

		if (!m_other)
			m_controls.want(state.player, guid);
		if (m_settings_file.empty())
			m_settings.set(section, key, value);
		else
			m_settings.change(m_settings_file, section, key, value);
		state.want.reset();
		m_menu_pictures = MENU_PICTURES;
	}
	if (state.ask == odin::MenuState::Ask::DEFAULTS) {
		change_inputs(true);
	} else if (state.ask == odin::MenuState::Ask::CLEAR) {
		m_cleared_game = game_shown();
		m_cleared = m_settings.section("controls." + m_cleared_game);
		change_inputs(false);
		m_undo_until = SDL_GetTicks() + UNDO_MS;
	} else if (state.ask == odin::MenuState::Ask::UNDO) {
		undo();
	} else if ((state.ask == odin::MenuState::Ask::PART_DEFAULTS || state.ask == odin::MenuState::Ask::PART_CLEAR) &&
	           state.picked) {
		change_part(*state.picked, state.ask == odin::MenuState::Ask::PART_DEFAULTS);
	}
	if (state.ask != odin::MenuState::Ask::NONE)
		m_menu_pictures = MENU_PICTURES;
	state.ask = odin::MenuState::Ask::NONE;
	m_controls.settle();
}

/* A setting changed in the menu is done now, and kept in odin.ini when it
 * is not the one by default; the pace waits for the next launch */
void
Session::apply(const odin::MenuState &was)
{
	const odin::MenuState &now = m_menu_state;
	const auto number = [](unsigned value, unsigned standard) {
		return value == standard ? std::nullopt : std::optional<std::string>(std::to_string(value));
	};
	const auto on_off = [](bool value, bool standard) {
		return value == standard ? std::nullopt : std::optional<std::string>(value ? "on" : "off");
	};

	if (now.scale != was.scale) {
		m_display.set_scale(now.scale);
		keep("scale", number(now.scale, 1));
	}
	if (now.anisotropy != was.anisotropy) {
		m_display.set_anisotropy(now.anisotropy);
		keep("anisotropy", number(now.anisotropy, 1));
	}
	if (now.fullscreen != was.fullscreen) {
		m_display.set_fullscreen(now.fullscreen);
		keep("fullscreen", on_off(now.fullscreen, false));
	}
	if (now.window != was.window) {
		m_display.resize(now.window);
		keep("window", number(now.window, 1));
	}
	if (now.vsync != was.vsync) {
		const auto vsync = static_cast<odin::Display::Vsync>(now.vsync);

		m_display.set_vsync(m_options.unthrottled ? odin::Display::Vsync::OFF : vsync);
		keep("vsync", vsync == VSYNC ? std::nullopt : std::optional<std::string>(VSYNCS[now.vsync]));
	}
	if (now.unthrottled != was.unthrottled)
		keep("unthrottled", on_off(now.unthrottled, false));
	if (now.show_speed != was.show_speed) {
		m_display.set_show_speed(now.show_speed);
		keep("show_speed", on_off(now.show_speed, true));
	}
	if (now.classic != was.classic && !m_settings_file.empty())
		m_settings.change(m_settings_file, "menu", "classic", on_off(now.classic, false));
	if (now.verbose != was.verbose)
		keep("log", "verbose", on_off(now.verbose, false));
	/* The sound: at once for the game that runs */
	if (now.device != was.device) {
		if (m_audio)
			m_audio->change(m_audio->mode(), now.device);
		keep("sound", "device", now.device.empty() ? std::nullopt : std::optional(now.device));
	}
	if (now.volume != was.volume) {
		if (m_audio)
			m_audio->set_volume(static_cast<float>(now.volume) / 100.0f);
		keep("sound", "volume", number(now.volume, 100));
	}
	if (now.background != was.background) {
		if (m_audio)
			m_audio->set_heard(m_focused || now.background);
		keep("sound", "background", on_off(now.background, true));
	}
	for (size_t g = 0; g < now.sound_games.size() && g < was.sound_games.size(); g++) {
		const odin::MenuState::SoundGame &game = now.sound_games[g], &before = was.sound_games[g];
		const std::string section = "sound." + game.name;
		const bool runs = game.name == m_game.name && m_audio;

		if (game.played != before.played) {
			static constexpr const char *MODES[] = { "main", "mix", "quad" };

			if (runs)
				m_audio->change(mode_of(game.played), now.device);
			keep(section.c_str(), "mode", game.played == game.cabinet_played || game.played < 0 ? std::nullopt :
			                              std::optional<std::string>(MODES[game.played]));
		}
		if (game.level != before.level) {
			char db[16];

			std::snprintf(db, sizeof(db), "%g", static_cast<double>(game.level));
			if (runs)
				m_audio->set_option_level(game.level);
			keep(section.c_str(), "option_level", game.level == game.standard ? std::nullopt :
			                                      std::optional<std::string>(db));
		}
	}
	/* The vibration of the game shown: at once for the game that runs */
	if (now.shake != was.shake && now.can_shake) {
		const std::string section = "players." + game_shown();

		if (!m_other) {
			m_controls.feel_option_board(now.shake && m_game.option_vibration);
			m_controls.feel_drive_board(now.shake ? m_game.drive_first_output : 0);
			m_controls.feel_outputs(now.shake);
		}
		keep(section.c_str(), "vibration", on_off(now.shake, true));
	}
	/* The dead zone and the trigger threshold of the gamepad shown: of its
	 * make and model, a line of their own where they differ from
	 * [gamepads] */
	if (const std::string guid = pad_shown(now);
	    !guid.empty() && (now.tuning.dead_zone != was.tuning.dead_zone ||
	                      now.tuning.trigger_threshold != was.tuning.trigger_threshold)) {
		const odin::PadTuning all = odin::PadTuning::of(m_settings, {}, false);
		const std::string section = "gamepad." + guid;
		const auto line = [](int percent, int standard) {
			return percent == standard ? std::nullopt : std::optional(std::to_string(percent));
		};

		if (now.tuning.dead_zone != was.tuning.dead_zone)
			keep(section.c_str(), "dead_zone", line(now.tuning.dead_zone, all.dead_zone));
		if (now.tuning.trigger_threshold != was.tuning.trigger_threshold)
			keep(section.c_str(), "trigger_threshold", line(now.tuning.trigger_threshold, all.trigger_threshold));
		m_controls.retune();
	}
	/* The programs of lamps: the port opened or closed now */
	if (now.network != was.network) {
		if (now.network)
			m_outputs = odin::OutputsNetwork::open(m_game.name, m_game.outputs);
		else
			m_outputs.reset();
		m_menu_state.network_failed = now.network && !m_outputs;
		m_menu_state.programs = 0;
		keep("outputs", "network", on_off(now.network, false));
	}
}

void
Session::keep(const char *key, const std::optional<std::string> &value)
{
	keep("display", key, value);
}

/* In odin.ini; with none, for the time Odin runs */
void
Session::keep(const char *section, const char *key, const std::optional<std::string> &value)
{
	if (m_settings_file.empty())
		m_settings.set(section, key, value);
	else
		m_settings.change(m_settings_file, section, key, value);
}

/* The folder of the games and that of the saves wait for the next launch;
 * the save states go to theirs now. A folder of their own by default is
 * no line in odin.ini. */
void
Session::take_folder()
{
	using Folder = odin::MenuState::Folder;
	odin::MenuState &state = m_menu_state;
	const std::optional<odin::FolderDialog::Answer> answer = m_dialog.answer();

	m_menu_pictures = MENU_PICTURES;
	if (!answer)
		return;
	state.no_picker = !answer->error.empty();
	if (state.no_picker)
		std::fprintf(stderr, "ERROR: no folder picker: %s\n", answer->error.c_str());
	if (answer->folder.empty())
		return;

	const std::string &folder = answer->folder;

	switch (static_cast<Folder>(answer->asked)) {
	case Folder::ROMS:
		state.roms = folder;
		keep("paths", "roms", folder);
		break;
	case Folder::SAVES:
		state.saves = folder;
		keep("paths", "saves", folder == odin::SaveFolder::root() ? std::nullopt : std::optional(folder));
		break;
	case Folder::STATES:
		state.states = m_states = folder;
		keep("paths", "states", folder == default_states() ? std::nullopt : std::optional(folder));
		break;
	default:
		break;
	}
}

/* ODIN_MENU_TEST: Escape pressed and released */
void
Session::press_escape()
{
	SDL_Event event{};

	event.type = SDL_EVENT_KEY_DOWN;
	event.key.key = SDLK_ESCAPE;
	event.key.scancode = SDL_SCANCODE_ESCAPE;
	event.key.down = true;
	SDL_PushEvent(&event);
	event.type = SDL_EVENT_KEY_UP;
	event.key.down = false;
	SDL_PushEvent(&event);
}

/* -s: <game>-<frame>.odinstate, written once the frame is drawn */
void
Session::save_state_at_frame()
{
	char path[64];

	for (const unsigned frame : m_options.save_frames) {
		if (frame != m_board.frame())
			continue;
		std::snprintf(path, sizeof(path), "%s-%06u.odinstate", m_board.game_name().c_str(), frame);
		if (m_board.save_state(path))
			std::printf("saved state '%s'\n", path);
		else
			std::fprintf(stderr, "ERROR: failed to save state '%s'\n", path);
	}
}

/* Holds the frames at the rate of the board, 60 per second. A frame late
 * by less than two periods is caught up, a longer delay is dropped. */
void
Session::limit_frame_rate()
{
	const auto now = std::chrono::steady_clock::now();

	m_next += FRAME;
	if (now - m_next > 2 * FRAME)
		m_next = now;
	else if (now < m_next)
		std::this_thread::sleep_until(m_next);
}

bool
Session::run()
{
	const bool quit = frames();

	/* The next game, or the window without one, finds the menu closed */
	if (m_display.menu_shown())
		close_menu();
	return quit;
}

bool
Session::frames()
{
	while (!process_events()) {
		if (m_options.num_frames > 0 && m_board.frame() >= m_options.num_frames)
			return false;
		if (m_display.menu_shown()) {
			wait_in_menu();
			continue;
		}
		if (m_paused) {
			/* Asleep until something comes, the next key */
			SDL_WaitEvent(nullptr);
			continue;
		}
		m_display.begin_frame();
		m_controls.update();
		m_board.run_frame();
		m_controls.show(m_board.io_outputs(), m_board.sound(true));
		if (m_outputs)
			m_outputs->show(m_board.io_outputs());
		if (m_audio)
			m_audio->play(m_board.sound(false), m_board.sound(true));
		m_display.end_frame(m_board.frame(), std::chrono::steady_clock::now() > m_next + FRAME);
		save_state_at_frame();
		if (m_menu_test && m_board.frame() % m_menu_test == 0)
			press_escape();
		if (!m_options.unthrottled)
			limit_frame_rate();
	}
	return true;
}

/* No game: the window black until it closes or the hotkey quits; the one
 * of the whole display works */
void
wait_without_game(odin::Display &display, const odin::Settings &settings)
{
	const odin::Hotkeys hotkeys(settings);
	SDL_Event event;

	while (SDL_WaitEvent(&event)) {
		if (event.type == SDL_EVENT_QUIT)
			return;
		if (event.type == SDL_EVENT_KEY_DOWN && !event.key.repeat) {
			const auto action = hotkeys.key_action(event.key.key, event.key.mod);

			if (action == odin::Hotkeys::Action::QUIT)
				return;
			if (action == odin::Hotkeys::Action::FULLSCREEN)
				display.toggle_fullscreen();
		}
		if (event.type == SDL_EVENT_WINDOW_EXPOSED || event.type == SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED)
			display.show_black();
	}
}

/* Why a game ends: the user quits, its last frame is done, or it cannot
 * run */
enum class Ended { QUIT, LAST_FRAME, FAILED };

/* A game in the window, from its power-on, or from the state of -l, to its
 * end. The options of the command line and odin.ini hold for it, what it
 * changes of them for itself does not stay. */
Ended
play(const Options &given, const std::string &name, const odin::GameList &games, odin::Settings &settings,
     const std::string &settings_file, odin::Display &display, odin::Keyboard &keys)
{
	Options options = given;

	if (!games.has(name)) {
		std::fprintf(stderr, "ERROR: failed to load '%s': game name not in game list\n", name.c_str());
		return Ended::FAILED;
	}

	/* The archives are let go of once the files are read */
	const std::optional<odin::Game> game = [&] {
		odin::RomFiles files(options.rom_path, options.bios_path);
		std::optional<odin::Game> loaded = games.load_game(files, name);

		if (loaded)
			return loaded;
		for (const odin::RomFiles::File &file : games.missing(files, name)) {
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

	const std::unique_ptr<odin::Hikaru> board = odin::Hikaru::make(*game, options.verbose);
	if (!board)
		return Ended::FAILED;

	display.attach(*board, game->title);
	odin::Controls controls(keys, game->controls, game->outputs, settings, game->name);
	board->set_panel(&controls);

	/* The last line says */
	bool network = false;
	for (const auto &[key, value] : settings.section("outputs")) {
		if (key == "network" && (value == "on" || value == "off"))
			network = value == "on";
		else
			std::fprintf(stderr, "ERROR: settings: [outputs]: not right: %s = %s\n", key.c_str(), value.c_str());
	}
	std::unique_ptr<odin::OutputsNetwork> outputs =
		network ? odin::OutputsNetwork::open(game->name, game->outputs) : nullptr;

	if (!options.sound_asked && game->option_speakers)
		options.sound = odin::AudioOut::Mode::MIX;
	for (const auto &[key, value] : settings.section("sound." + game->name))
		if (key == "mode" && !options.sound_asked && mode_named(value))
			options.sound = mode_named(value);

	/* The optional board in the mix: the list, or the last lines of
	 * [sound.<game>]; the pads shake unless [players.<game>] says off */
	double option_level = game->option_level;
	bool vibration = true;
	for (const auto &[key, value] : settings.section("players." + game->name))
		if (key == "vibration" && (value == "on" || value == "off"))
			vibration = value == "on";
	for (const auto &[key, value] : settings.section("sound." + game->name)) {
		char *end = nullptr;
		const double db = std::strtod(value.c_str(), &end);

		if (key == "option_level" && !value.empty() && !*end && db >= odin::Game::LOWEST_LEVEL &&
		    db <= odin::Game::HIGHEST_LEVEL)
			option_level = db;
		else if (key == "mode" && mode_named(value))
			continue;
		else
			std::fprintf(stderr, "ERROR: settings: [sound.%s]: not right: %s = %s\n", game->name.c_str(),
			             key.c_str(), value.c_str());
	}

	/* Unthrottled frames do not keep time: no sound with -u */
	std::unique_ptr<odin::AudioOut> audio;
	if (options.sound && options.unthrottled && options.sound_asked)
		std::fprintf(stderr, "sound: not with -u\n");
	else if (options.sound && !options.unthrottled)
		audio = odin::AudioOut::make(*options.sound, option_level, options.sound_device);
	if (audio && options.volume != 100)
		audio->set_volume(static_cast<float>(options.volume) / 100.0f);
	if (audio && options.sound == odin::AudioOut::Mode::MIX && option_level != 0)
		std::printf("sound: the optional board at %+g dB in the mix\n", option_level);
	controls.feel_option_board(vibration && game->option_vibration);
	controls.feel_drive_board(vibration ? game->drive_first_output : 0);
	controls.feel_outputs(vibration);
	if (vibration && (game->option_vibration || game->drive_first_output))
		std::printf("controls: the pad of player 1 shakes as the%s%s of the cabinet (experimental)\n",
		            game->option_vibration ? " vibration device" : "",
		            game->drive_first_output ? (game->option_vibration ? " and the wheel" : " wheel") : "");

	bool started = true;

	board->keep_in(odin::SaveFolder::of_game(game->name, options.saves));
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
		std::printf("Running\n");
		ended = Session(options, *game, *board, display, keys, controls, audio.get(), std::move(outputs), network,
		                settings, settings_file, games).run() ? Ended::QUIT : Ended::LAST_FRAME;
	}

	std::printf("Finalizing\n");
	board->power_off();
	display.detach();
	return ended;
}

} /* namespace */

int
main(int argc, char **argv)
{
	Options options;
	odin::GameList games;

	/* With the OpenGL driver of Intel loaded, a process of Windows ends
	 * without what its buffers still hold: it goes out here. */
	std::atexit([] { std::fflush(nullptr); });

	/* What Odin says goes to odin.log too, from its first line */
	if (const std::string log = odin::LogFile::path(); odin::LogFile::start(log))
		options.log_file = log;

	std::printf("Odin, Copyright(C) 2026 Réda Chérif-Touil\n");
	std::printf("A hard fork of Valkyrie, Copyright(C) 2011-2013 Stefano Teso\n");
	std::printf("Released under the GPL3 License.\n");

	if (const Parsed parsed = parse_options(argc, argv, options); parsed != Parsed::RUN)
		return parsed == Parsed::HELP_ASKED ? 0 : 1;

	if (!load_game_list(games)) {
		std::fprintf(stderr, "ERROR: failed to load the game list\n");
		return 1;
	}

	odin::Settings settings;
	const std::string settings_file = odin::Settings::user_file();
	if (!settings_file.empty() && settings.load(settings_file)) {
		std::printf("settings: '%s'\n", settings_file.c_str());
		settings.know(known_settings(games));
		settings.complete(settings_file);
	}
	read_paths(settings, options);
	read_display(settings, options);
	read_sound(settings, options);

	odin::Keyboard keys;
	options.display.vsync = options.unthrottled ? odin::Display::Vsync::OFF : options.vsync;
	const std::unique_ptr<odin::Display> display = odin::Display::make(keys, options.display);
	if (!display)
		return 1;

	/* ODIN_GAMES_TEST=<game>,...: these games after the first, each from
	 * its power-on to its frame -n, in the same window */
	std::vector<std::string> next;
	const char *const games_test = std::getenv("ODIN_GAMES_TEST");
	for (std::string list = games_test ? games_test : ""; !list.empty();) {
		const size_t comma = list.find(',');

		next.push_back(list.substr(0, comma));
		list = comma == std::string::npos ? "" : list.substr(comma + 1);
	}

	std::string name = options.game;
	for (size_t played = 0;; played++) {
		if (name.empty()) {
			wait_without_game(*display, settings);
			return 0;
		}

		const Ended ended = play(options, name, games, settings, settings_file, *display, keys);

		if (ended == Ended::FAILED)
			return 1;
		if (ended == Ended::QUIT || played >= next.size())
			return 0;
		name = next[played];
		options.state_path.clear();
		options.save_frames.clear();
	}
}
