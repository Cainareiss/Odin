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
#include "odin/games.hpp"
#include "odin/audio_out.hpp"
#include "odin/hikaru.hpp"
#include "odin/controls.hpp"
#include "odin/keyboard.hpp"
#include "odin/menu.hpp"
#include "odin/outputs_network.hpp"
#include "odin/settings.hpp"

namespace {

struct Options {
	std::string rom_path;
	std::string game;
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
	odin::Display::Options display = { 1, 1, 0, false, true, {} };
	/* What the command line says of the display: odin.ini does not change it */
	struct {
		bool scale = false, anisotropy = false, window = false, fullscreen = false, unthrottled = false;
	} given;
};

const char HELP[] = "Usage: %s [options]\n"
"	-R <path>	Path to the ROM directory\n"
"	-r <string>	Name of the game to run\n"
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

/* The help is shown for -h, without options and for an option that is not
 * right. */
Parsed
parse_options(int argc, char **argv, Options &options)
{
	int opt = (argc < 2) ? 'h' : 0;
	bool wrong = false;

	while (opt != 'h' && (opt = getopt(argc, argv, "R:r:n:l:s:ux:a:w:fo:vh?")) != -1) {
		switch (opt) {
		case 'R':
			options.rom_path = optarg;
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
			options.verbose = true;
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
		} else {
			std::fprintf(stderr, "ERROR: settings: [display]: not right: %s = %s\n", key.c_str(), value.c_str());
		}
	}
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

/* A game being run: its board, with the window, the keyboard and the
 * gamepads around it.
 *
 * ODIN_MENU_TEST=<n> is a test of the menu: Escape is pressed after every
 * n frames, which opens it, and again once it has drawn its pictures,
 * which closes it. The pictures of the game must be the same. */
class Session {
public:
	Session(const Options &options, odin::Hikaru &board, odin::Display &display, odin::Keyboard &keys,
	        odin::Controls &controls, odin::AudioOut *audio, odin::OutputsNetwork *outputs, odin::Settings &settings,
	        const std::string &settings_file, const odin::GameList &games)
		: m_options(options), m_board(board), m_display(display), m_keys(keys), m_controls(controls), m_audio(audio),
		  m_outputs(outputs), m_settings(settings), m_settings_file(settings_file), m_games(games), m_paused(false),
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
		state.scale_given = options.given.scale;
		state.anisotropy_given = options.given.anisotropy;
		state.window_given = options.given.window;
		state.fullscreen_given = options.given.fullscreen;
		state.unthrottled_given = options.given.unthrottled;
		state.gpu = display.gpu();
		state.kept = !settings_file.empty();
		state.games = games.titles("hikaru");
		for (const auto &[key, value] : settings.section("menu"))
			if (key == "classic")
				state.classic = value == "on";
		state.shown = state.running = state.games.size();
		for (size_t g = 0; g < state.games.size(); g++)
			if (state.games[g].first == board.game_name())
				state.shown = state.running = g;
		/* The BIOS alone: the page Input shows the first game */
		if (state.running == state.games.size())
			show_game(0);
	}

	/* Runs the frames until the user quits or the last one asked is
	 * done. */
	void run();

private:
	/* After an event the menu draws this many pictures, one in so many
	 * milliseconds at most */
	static constexpr unsigned MENU_PICTURES = 3;
	static constexpr Uint64 MENU_PICTURE_MS = 16;
	/* How long Undo is offered after Clear all */
	static constexpr Uint64 UNDO_MS = 5000;

	bool process_events();
	bool menu_event(const SDL_Event &event);
	void open_menu();
	void close_menu();
	void wait_in_menu();
	void draw_menu();
	void apply(const odin::MenuState &was);
	void keep(const char *key, const std::optional<std::string> &value);
	/* The game whose controls the page Input shows: its name and panel */
	std::string game_shown() const;
	const odin::ControlPanel &panel_shown() const { return m_other ? *m_other : m_controls.panel(); }
	void show_game(size_t game);
	bool catch_input(const SDL_Event &event);
	void caught(const odin::HostInput &input);
	void change_inputs(bool to_defaults);
	void undo();
	void keep_binding(const odin::Bindings::Line &line, const std::string &game = "");
	void press_escape();
	void load_or_save_state(bool load);
	void save_state_at_frame();
	void limit_frame_rate();

	const Options &m_options;
	odin::Hikaru &m_board;
	odin::Display &m_display;
	odin::Keyboard &m_keys;
	odin::Controls &m_controls;
	odin::AudioOut *m_audio;	/* nullptr: no sound */
	odin::OutputsNetwork *m_outputs;	/* nullptr: none for other programs */
	odin::Settings &m_settings;
	const std::string &m_settings_file;	/* empty: nothing is kept */
	const odin::GameList &m_games;
	/* The panel of a game shown that does not run, and its controls */
	std::vector<odin::GameControl> m_other_controls;
	std::optional<odin::ControlPanel> m_other;
	odin::MenuState m_menu_state;
	/* The inputs as Clear all found them, and until when Undo puts them back */
	odin::Settings::Lines m_cleared;
	std::string m_cleared_game;
	Uint64 m_undo_until = 0;
	/* Guide is held while an input is caught: the next button goes with it */
	bool m_catch_guide = false;
	bool m_menu_opened = false;
	bool m_paused;
	std::chrono::steady_clock::time_point m_next;	/* when the next frame is due */
	unsigned m_menu_pictures;	/* still to draw */
	Uint64 m_menu_drawn;		/* when the last one was */
	unsigned m_menu_test;
};

bool
is_quit(const SDL_KeyboardEvent &key)
{
	return key.down && key.key == SDLK_Q && (key.mod & SDL_KMOD_CTRL);
}

bool
is_menu(const SDL_KeyboardEvent &key)
{
	return key.down && !key.repeat && (key.key == SDLK_ESCAPE || key.key == SDLK_F10);
}

bool
is_fullscreen(const SDL_KeyboardEvent &key)
{
	return key.down && (key.key == SDLK_F11 || (key.key == SDLK_RETURN && (key.mod & SDL_KMOD_ALT)));
}

/* F1 and F4: <game>.odinstate */
void
Session::load_or_save_state(bool load)
{
	const std::string path = m_board.game_name() + ".odinstate";

	if (load ? m_board.load_state(path) : m_board.save_state(path))
		std::printf("%s state '%s'\n", load ? "loaded" : "saved", path.c_str());
	else
		std::fprintf(stderr, "ERROR: failed to %s state '%s'\n", load ? "load" : "save", path.c_str());
}

/* The keys of the emulator: Escape or F10 opens the menu, which pauses the
 * game, Ctrl+Q quits, P pauses (as in MAME), F1 loads the state of the
 * game and F4 saves it, F11 or Alt+Enter takes the whole display. The
 * others are the control panel, see keyboard.hpp. True to quit. */
bool
Session::process_events()
{
	SDL_Event event;
	bool quit = false;

	while (SDL_PollEvent(&event)) {
		if (m_display.menu_shown()) {
			quit = menu_event(event) || quit;
			continue;
		}
		if (m_controls.event(event)) {
			if (m_controls.guide_tapped())
				open_menu();
			continue;
		}
		switch (event.type) {
		case SDL_EVENT_KEY_DOWN:
			m_keys.set(event.key.key, true);
			if (is_quit(event.key))
				quit = true;
			else if (is_menu(event.key))
				open_menu();
			else if (is_fullscreen(event.key))
				m_display.toggle_fullscreen();
			switch (event.key.key) {
			case SDLK_P:
				m_paused = !m_paused;
				if (m_paused)
					m_controls.quiet();
				break;
			case SDLK_F1:
				load_or_save_state(true);
				break;
			case SDLK_F4:
				load_or_save_state(false);
				break;
			default:
				break;
			}
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

/* With the menu open, Escape or F10 closes it and Ctrl+Q quits; F11 and
 * Alt+Enter still take the whole display. The keyboard and the gamepads
 * are followed as they go; all the rest is the menu's. True to quit. */
bool
Session::menu_event(const SDL_Event &event)
{
	if (m_menu_state.catching && catch_input(event)) {
		m_menu_pictures = MENU_PICTURES;
		return false;
	}
	switch (event.type) {
	case SDL_EVENT_QUIT:
		return true;
	case SDL_EVENT_KEY_DOWN:
	case SDL_EVENT_KEY_UP:
		m_keys.set(event.key.key, event.key.down);
		if (is_quit(event.key))
			return true;
		if (is_menu(event.key)) {
			close_menu();
			return false;
		}
		if (is_fullscreen(event.key))
			m_display.toggle_fullscreen();
		break;
	case SDL_EVENT_GAMEPAD_ADDED:
	case SDL_EVENT_GAMEPAD_REMOVED:
	case SDL_EVENT_GAMEPAD_AXIS_MOTION:
	case SDL_EVENT_GAMEPAD_BUTTON_DOWN:
	case SDL_EVENT_GAMEPAD_BUTTON_UP:
		m_controls.event(event);
		if (m_controls.guide_tapped())
			close_menu();
		m_menu_pictures = MENU_PICTURES;
		return false;
	case SDL_EVENT_WINDOW_MOUSE_LEAVE:
		m_controls.event(event);
		break;
	default:
		break;
	}
	m_display.menu_event(event);
	m_menu_pictures = MENU_PICTURES;
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
	m_keys.swallow();
	if (m_audio)
		m_audio->pause(false);
	m_next = std::chrono::steady_clock::now();
}

/* The keys Odin works itself, which a control of the game does not take */
bool
is_odin_key(SDL_Keycode key)
{
	return key == SDLK_ESCAPE || key == SDLK_F10 || key == SDLK_F11 || key == SDLK_F1 || key == SDLK_F4 || key == SDLK_P;
}

/* The page Input waits for an input: Escape lets it go; a key, or on a
 * gamepad a button (Guide held, then another: both), a stick or a trigger
 * from half its way, is the one. True when the event was taken. */
bool
Session::catch_input(const SDL_Event &event)
{
	odin::MenuState &state = m_menu_state;

	switch (event.type) {
	case SDL_EVENT_KEY_DOWN:
	case SDL_EVENT_KEY_UP:
		m_keys.set(event.key.key, event.key.down);
		if (!event.key.down || event.key.repeat)
			return true;
		if (event.key.key == SDLK_ESCAPE) {
			state.catching.reset();
			return true;
		}
		if (!state.on_pad() && !is_odin_key(event.key.key)) {
			odin::HostInput input{};

			input.kind = odin::HostInput::Kind::KEY;
			input.key = event.key.key;
			caught(input);
		}
		return true;
	case SDL_EVENT_GAMEPAD_BUTTON_UP:
		m_controls.event(event);
		m_controls.guide_tapped();
		if (event.gbutton.button == SDL_GAMEPAD_BUTTON_GUIDE)
			m_catch_guide = false;
		return true;
	case SDL_EVENT_GAMEPAD_BUTTON_DOWN:
	case SDL_EVENT_GAMEPAD_AXIS_MOTION: {
		m_controls.event(event);
		if (!state.on_pad())
			return true;

		odin::HostInput input{};
		const bool axis = event.type == SDL_EVENT_GAMEPAD_AXIS_MOTION;
		const int value = axis ? event.gaxis.value : 0;

		if (axis && std::abs(value) < odin::ControlPanel::PRESSED)
			return true;
		if (!axis && event.gbutton.button == SDL_GAMEPAD_BUTTON_GUIDE) {
			m_catch_guide = true;
			return true;
		}
		input.kind = odin::HostInput::Kind::PAD;
		input.pad.index = axis ? event.gaxis.axis : event.gbutton.button;
		input.pad.type = !axis ? odin::PadInput::Type::BUTTON : odin::PadInput::Type::AXIS;
		if (!axis && m_catch_guide)
			input.pad.with = SDL_GAMEPAD_BUTTON_GUIDE;
		/* A stick on a part that is pressed: its half */
		if (axis && event.gaxis.axis < SDL_GAMEPAD_AXIS_LEFT_TRIGGER && state.catching->part != 0)
			input.pad.type = value < 0 ? odin::PadInput::Type::AXIS_NEGATIVE : odin::PadInput::Type::AXIS_POSITIVE;
		caught(input);
		return true;
	}
	default:
		return false;
	}
}

/* The input caught takes the place of those of its kind on that part */
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
}

/* Default and Clear all: the inputs of the device shown, of every part of
 * the controls of the player and their coin (and TEST for player 1), back
 * to their defaults or none */
void
Session::change_inputs(bool to_defaults)
{
	const odin::MenuState &state = m_menu_state;
	const auto kind = state.on_pad() ? odin::HostInput::Kind::PAD : odin::HostInput::Kind::KEY;
	const auto changed = [&](std::vector<odin::HostInput> inputs, const std::vector<odin::HostInput> &standard) {
		std::erase_if(inputs, [kind](const odin::HostInput &in) { return in.kind == kind; });
		if (to_defaults)
			for (const odin::HostInput &in : standard)
				if (in.kind == kind)
					inputs.push_back(in);
		return inputs;
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

/* The inputs back as Clear all found them */
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
	m_other.reset();
	if (!other)
		return;
	m_other_controls = other->controls;
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
	/* Undo goes when its time is over */
	if (m_menu_state.undo && SDL_GetTicks() >= m_undo_until)
		m_menu_pictures = std::max(m_menu_pictures, 1u);
	if (!m_menu_pictures) {
		if (m_menu_state.undo)
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

	state.fullscreen = m_display.fullscreen();
	m_display.display_size(state.display_w, state.display_h);
	state.panel = &panel_shown();
	state.touched.clear();
	if (!m_other) {
		for (size_t c = 0; c < m_controls.panel().controls().size(); c++)
			state.touched.push_back(m_controls.touched(c));
		for (const odin::Role role : { odin::Role::COIN1, odin::Role::COIN2, odin::Role::TEST })
			state.touched.push_back(m_controls.touched(role));
	}
	for (unsigned player = 0; player < 2; player++) {
		state.pads[player] = m_controls.pad_name(player);
		state.pad_types[player] = m_controls.pad_type(player);
		state.pad_states[player] = m_controls.pad_state(player);
	}
	state.undo = m_undo_until && SDL_GetTicks() < m_undo_until;
	state.plugged.clear();
	for (const odin::Controls::Plugged &pad : m_controls.plugged())
		state.plugged.emplace_back(pad.name, pad.guid);
	for (unsigned player = 0; player < 2; player++) {
		state.wanted[player] = m_controls.wanted(player);
		state.wanted_names[player] = m_controls.wanted_name(player);
	}
	/* Those of a game that does not run are in its settings */
	if (m_other) {
		state.wanted = {};
		state.wanted_names = {};
		for (const auto &[key, value] : m_settings.section("players." + game_shown())) {
			const size_t space = value.find(' ');

			if (key == "p1" || key == "p2") {
				state.wanted[key[1] - '1'] = value.substr(0, space);
				state.wanted_names[key[1] - '1'] = space == std::string::npos ? "" : value.substr(space + 1);
			}
		}
	}

	const odin::MenuState was = state;

	m_display.draw_menu(state);
	apply(was);

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
			m_controls.want(state.player, guid, name);
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
	if (now.unthrottled != was.unthrottled)
		keep("unthrottled", on_off(now.unthrottled, false));
	if (now.show_speed != was.show_speed) {
		m_display.set_show_speed(now.show_speed);
		keep("show_speed", on_off(now.show_speed, true));
	}
	if (now.classic != was.classic && !m_settings_file.empty())
		m_settings.change(m_settings_file, "menu", "classic", on_off(now.classic, false));
}

void
Session::keep(const char *key, const std::optional<std::string> &value)
{
	if (!m_settings_file.empty())
		m_settings.change(m_settings_file, "display", key, value);
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
	const std::chrono::nanoseconds period(1000000000 / 60);
	const auto now = std::chrono::steady_clock::now();

	m_next += period;
	if (now - m_next > 2 * period)
		m_next = now;
	else if (now < m_next)
		std::this_thread::sleep_until(m_next);
}

void
Session::run()
{
	while (!process_events()) {
		if (m_options.num_frames > 0 && m_board.frame() >= m_options.num_frames)
			return;
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
		m_display.end_frame(m_board.frame());
		save_state_at_frame();
		if (m_menu_test && m_board.frame() % m_menu_test == 0)
			press_escape();
		if (!m_options.unthrottled)
			limit_frame_rate();
	}
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

	std::printf("Odin, Copyright(C) 2026 Réda Chérif-Touil\n");
	std::printf("A hard fork of Valkyrie, Copyright(C) 2011-2013 Stefano Teso\n");
	std::printf("Released under the GPL3 License.\n");

	if (const Parsed parsed = parse_options(argc, argv, options); parsed != Parsed::RUN)
		return parsed == Parsed::HELP_ASKED ? 0 : 1;

	if (!load_game_list(games)) {
		std::fprintf(stderr, "ERROR: failed to load the game list\n");
		return 1;
	}

	if (!games.has(options.game)) {
		std::fprintf(stderr, "ERROR: failed to load '%s': game name not in game list\n", options.game.c_str());
		return 1;
	}

	const std::optional<odin::Game> game = games.load_game(options.rom_path, options.game);
	if (!game) {
		std::fprintf(stderr, "ERROR: failed to load '%s': can't load game files\n", options.game.c_str());
		return 1;
	}
	if (game->machine != "hikaru") {
		std::fprintf(stderr, "ERROR: failed to load '%s': a game of the %s, not of the Hikaru\n",
		             options.game.c_str(), game->machine.c_str());
		return 1;
	}

	const std::unique_ptr<odin::Hikaru> board = odin::Hikaru::make(*game, options.verbose);
	if (!board)
		return 1;

	odin::Settings settings;
	const std::string settings_file = odin::Settings::user_file();
	if (!settings_file.empty() && settings.load(settings_file))
		std::printf("settings: '%s'\n", settings_file.c_str());
	read_display(settings, options);

	odin::Keyboard keys;
	options.display.title = game->title;
	const std::unique_ptr<odin::Display> display = odin::Display::make(*board, keys, options.display);
	if (!display)
		return 1;
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
	const std::unique_ptr<odin::OutputsNetwork> outputs =
		network ? odin::OutputsNetwork::open(game->name, game->outputs) : nullptr;

	if (!options.sound_asked && game->option_speakers)
		options.sound = odin::AudioOut::Mode::MIX;

	/* The optional board in the mix and on the pad: the list, or the last
	 * lines of [sound.<game>] */
	double option_level = game->option_level;
	bool vibration = game->option_vibration || game->drive_first_output;
	for (const auto &[key, value] : settings.section("sound." + game->name)) {
		char *end = nullptr;
		const double db = std::strtod(value.c_str(), &end);

		if (key == "option_level" && !value.empty() && !*end && db >= odin::Game::LOWEST_LEVEL &&
		    db <= odin::Game::HIGHEST_LEVEL)
			option_level = db;
		else if (key == "vibration" && (value == "on" || value == "off"))
			vibration = value == "on";
		else
			std::fprintf(stderr, "ERROR: settings: [sound.%s]: not right: %s = %s\n", game->name.c_str(),
			             key.c_str(), value.c_str());
	}

	/* Unthrottled frames do not keep time: no sound with -u */
	std::unique_ptr<odin::AudioOut> audio;
	if (options.sound && options.unthrottled && options.sound_asked)
		std::fprintf(stderr, "sound: not with -u\n");
	else if (options.sound && !options.unthrottled)
		audio = odin::AudioOut::make(*options.sound, option_level);
	if (audio && options.sound == odin::AudioOut::Mode::MIX && option_level != 0)
		std::printf("sound: the optional board at %+g dB in the mix\n", option_level);
	controls.feel_option_board(vibration && game->option_vibration);
	controls.feel_drive_board(vibration ? game->drive_first_output : 0);
	if (vibration)
		std::printf("controls: the pad of player 1 shakes as the%s%s of the cabinet (experimental)\n",
		            game->option_vibration ? " vibration device" : "",
		            game->drive_first_output ? (game->option_vibration ? " and the wheel" : " wheel") : "");

	bool started = true;

	board->keep_in(odin::SaveFolder::of_game(game->name));
	board->reset();
	if (!options.state_path.empty()) {
		started = board->load_state(options.state_path);
		if (started)
			std::printf("loaded state '%s', frame %u\n", options.state_path.c_str(), board->frame());
		else
			std::fprintf(stderr, "ERROR: failed to load state '%s'\n", options.state_path.c_str());
	}
	if (started) {
		std::printf("Running\n");
		Session(options, *board, *display, keys, controls, audio.get(), outputs.get(), settings, settings_file, games)
			.run();
	}

	std::printf("Finalizing\n");
	board->power_off();
	return started ? 0 : 1;
}
