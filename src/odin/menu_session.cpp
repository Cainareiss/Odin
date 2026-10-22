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

#include "odin/menu_session.hpp"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <filesystem>

#include "odin/env.hpp"
#include "odin/save_folder.hpp"

namespace odin {

/* An archive Open... gave: its game, as MAME names its archive (the BIOS is
 * none), and the folder that holds it; else nothing, and what went wrong
 * for the bar to say (empty: nothing was chosen) */
std::optional<Opened>
open_archive(const FolderDialog::Answer &answer, const GameList &games, std::string &note)
{
	if (!answer.error.empty()) {
		std::fprintf(stderr, "ERROR: no file picker: %s\n", answer.error.c_str());
		note = "This system gives Odin no file picker";
		return std::nullopt;
	}
	if (answer.folder.empty())
		return std::nullopt;

	const std::filesystem::path file(answer.folder);
	std::string name = file.stem().string();

	std::transform(name.begin(), name.end(), name.begin(),
	               [](unsigned char c) { return static_cast<char>(c >= 'A' && c <= 'Z' ? c - 'A' + 'a' : c); });
	for (const auto &[game, title] : games.titles("hikaru"))
		if (game == name)
			return Opened{ game, file.parent_path().string() };
	std::fprintf(stderr, "ERROR: '%s' is no game of the list: an archive takes the name MAME gives its game\n",
	             answer.folder.c_str());
	note = file.filename().string() + ": not a game of Odin";
	return std::nullopt;
}

/* [outputs] network: the outputs sent to the programs of lamps. The last
 * line says. */
bool
network_on(const Settings &settings)
{
	bool network = false;

	for (const auto &[key, value] : settings.section("outputs")) {
		if (key == "network" && (value == "on" || value == "off"))
			network = value == "on";
		else
			std::fprintf(stderr, "ERROR: settings: [outputs]: not right: %s = %s\n", key.c_str(), value.c_str());
	}
	return network;
}

/* [network]: the network board of the games that link cabinets
 * (NetLinkSettings). The last line says; a line not right is left out. */
NetLinkSettings
link_settings(const Settings &settings)
{
	NetLinkSettings link;

	for (const auto &[key, value] : settings.section("network")) {
		if (key == "board" && NetLinkSettings::board_named(value, link.board))
			continue;
		if (key == "port" && NetLinkSettings::port_named(value, link.port))
			continue;
		if (key == "next") {
			NetLinkSettings tried = link;
			std::string host;
			uint16_t port;

			tried.next = value;
			if (tried.next_cabinet(host, port)) {
				link.next = value;
				continue;
			}
		}
		std::fprintf(stderr, "ERROR: settings: [network]: not right: %s = %s\n", key.c_str(), value.c_str());
	}
	return link;
}

/* ODIN_MENU_TEST: Escape pressed and released */
void
press_escape()
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

MenuSession::MenuSession(const Options &options, Display &display, Keyboard &keys, Controls &controls,
                         Settings &settings, const std::string &settings_file, const GameList &games, const Game *game,
                         Hikaru *board, AudioOut *audio, std::unique_ptr<OutputsNetwork> outputs, bool network)
	: m_options(options), m_display(display), m_keys(keys), m_controls(controls), m_settings(settings),
	  m_settings_file(settings_file), m_games(games), m_game(game), m_board(board), m_audio(audio),
	  m_outputs(std::move(outputs)), m_hotkeys(settings), m_nav(settings),
	  m_menu_test(game ? static_cast<unsigned>(std::max(env_int("ODIN_MENU_TEST", 0), 0)) : 0)
{
	MenuState &state = m_menu_state;

	state.scale = options.display.scale;
	state.anisotropy = options.display.anisotropy;
	state.window = options.display.window ? options.display.window : 1;
	state.custom_w = options.display.window_w;
	state.custom_h = options.display.window_h;
	state.unthrottled = options.unthrottled;
	state.show_speed = options.display.show_speed;
	state.vsync = static_cast<unsigned>(options.vsync);
	state.sides = static_cast<unsigned>(options.sides);
	state.scale_given = options.given.scale;
	state.anisotropy_given = options.given.anisotropy;
	state.window_given = options.given.window;
	state.fullscreen_given = options.given.fullscreen;
	state.unthrottled_given = options.given.unthrottled;
	state.gpu = display.gpu();
	state.kept = !settings_file.empty();
	state.roms = options.rom_path;
	state.roms_given = options.given.roms;
	state.bios = options.bios_path;
	state.bios_given = options.given.bios;
	state.saves = SaveFolder::root(options.saves);
	const char *const saves = std::getenv("ODIN_SAVES");
	state.saves_given = saves && saves[0];
	state.states = m_states = options.states.empty() ? default_states() : options.states;
	state.settings_file = settings_file;
	state.log_file = options.log_file;
	state.artwork = artwork_folder(options.artwork);
	const char *const artwork = std::getenv("ODIN_ARTWORK");
	state.artwork_given = artwork && artwork[0];
	if (game) {
		display.set_sides(options.sides, artwork_file(state.artwork, game->name, "bezel"));
		controls.set_picture_hole(display.picture_hole());
	}
	state.shader_lists = std::filesystem::path(display.program_list()).parent_path().string();
	state.verbose = options.verbose;
	state.verbose_given = options.given.verbose;
	state.network = network;
	state.network_failed = network && game && !m_outputs;

	const NetLinkSettings link = link_settings(settings);

	state.link_board = static_cast<int>(link.board);
	state.link_port = link.port;
	state.link_next = state.link_next_typed = link.next;
	state.link_given = std::getenv("ODIN_NETBD") != nullptr;
	state.link_game = game && game->link;
	state.device = options.sound_device;
	state.volume = options.volume;
	state.background = options.background;
	/* The library lists the devices of the system: its sound is
	 * started for that */
	m_listing = !game && options.sound && !options.unthrottled && SDL_InitSubSystem(SDL_INIT_AUDIO);
	state.silent = options.unthrottled ? "no sound at an unlimited pace (Display)" :
	               !options.sound ? "set by the command line: -o off" :
	               !audio && !m_listing ? "no sound device" : nullptr;
	state.library = !game;
	for (const auto &[name, title] : games.titles("hikaru")) {
		const bool runs = game && name == game->name;
		const std::optional<Game> other = runs ? std::nullopt : games.describe(name);

		const Game *const described = runs ? game : other ? &*other : nullptr;

		if (!described || !(described->option_speakers || described->option_vibration))
			continue;

		/* As the cabinet: both boards in the speakers where it has
		 * some for the optional one */
		const int cabinet = described->option_speakers ? 1 : 0;
		MenuState::SoundGame sound = { name, title, cabinet, cabinet, 0.0f, 0.0f, false };

		sound.level = sound.standard = static_cast<float>(described->option_level);
		for (const auto &[key, value] : settings.section("sound." + name)) {
			if (key == "mode" && mode_named(value))
				sound.played = played_of(*mode_named(value));
			else if (key == "option_level" && level_named(value))
				sound.level = static_cast<float>(*level_named(value));
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
		else if (key == "theme" && theme_named(value) >= 0)
			state.theme = static_cast<unsigned>(theme_named(value));
		else if (!MenuNav::knows(key))
			std::fprintf(stderr, "ERROR: settings: [menu]: not right: %s = %s\n", key.c_str(), value.c_str());
	}
	state.shown = state.running = state.games.size();
	for (size_t g = 0; g < state.games.size(); g++)
		if (board && state.games[g].first == board->game_name())
			state.shown = state.running = g;
	/* The BIOS alone, or no game: the page Input shows the first one */
	if (state.running == state.games.size())
		show_game(0);
}

MenuSession::Asked
MenuSession::event(const SDL_Event &event)
{
	using Action = Hotkeys::Action;

	if (m_menu_state.catching && catch_input(event)) {
		m_menu_pictures = MENU_PICTURES;
		return Asked::NONE;
	}
	if (m_menu_state.hotkey_catching && catch_hotkey(event)) {
		m_menu_pictures = MENU_PICTURES;
		return Asked::NONE;
	}

	std::optional<Action> action;
	bool pad = false;

	switch (event.type) {
	case SDL_EVENT_QUIT:
		return Asked::QUIT;
	case SDL_EVENT_KEY_DOWN:
	case SDL_EVENT_KEY_UP:
		m_keys.set(event.key.key, event.key.down);
		/* The keys write in a field of the menu, Escape leaving it */
		if (event.key.down && !event.key.repeat && !m_display.menu_typing())
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

		action = m_hotkeys.pad_action(combo >= 0 ? combo : Hotkeys::NO_PAD);
		if (m_controls.guide_tapped())
			action = m_hotkeys.pad_action(Hotkeys::GUIDE_ALONE);
		pad = true;
		break;
	}
	case SDL_EVENT_WINDOW_MOUSE_LEAVE:
	case SDL_EVENT_JOYSTICK_ADDED:
	case SDL_EVENT_JOYSTICK_REMOVED:
	case SDL_EVENT_JOYSTICK_AXIS_MOTION:
	case SDL_EVENT_JOYSTICK_BUTTON_DOWN:
	case SDL_EVENT_JOYSTICK_BUTTON_UP:
	case SDL_EVENT_JOYSTICK_HAT_MOTION:
		m_controls.event(event);
		break;
	default:
		break;
	}
	if (action == Action::QUIT)
		return Asked::QUIT;
	if (action == Action::MENU)
		return Asked::CLOSE;
	if (action == Action::FULLSCREEN)
		m_display.toggle_fullscreen();
	/* The menu's own buttons; the keys of a list dropped open are the list's */
	for (const MenuNav::Action nav : m_nav.event(event)) {
		if (action || (!pad && m_display.menu_popup_open()) || !navigate(nav))
			continue;
		m_menu_pictures = MENU_PICTURES;
		return Asked::CLOSE;
	}
	/* The gamepads are Odin's, not Dear ImGui's */
	if (!pad)
		m_display.menu_event(event);
	m_menu_pictures = MENU_PICTURES;
	return Asked::NONE;
}

std::string
MenuSession::press_name() const
{
	const unsigned player = m_controls.pad_name(0).empty() ? 1 : 0;

	if (m_controls.pad_name(player).empty())
		return {};
	for (const HostInput &in : m_nav.inputs(MenuNav::PRESS))
		if (in.kind == HostInput::Kind::PAD)
			return Menu::input_name(in.pad, m_controls.pad_type(player));
	return {};
}

/* For its next picture */
bool
MenuSession::navigate(MenuNav::Action action)
{
	using Nav = MenuNav;
	MenuState::Nav &nav = m_menu_state.nav;

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

void
MenuSession::open(int page)
{
	m_controls.quiet();
	if (m_audio)
		m_audio->pause(true);
	/* The page Input shows the device of each player, until the user picks
	 * one: their joystick in a game with a wheel, else their gamepad, else
	 * their joystick, else the keyboard */
	if (!m_menu_opened) {
		using Device = MenuState::Device;
		const bool wheel = m_game && std::any_of(m_game->controls.begin(), m_game->controls.end(),
		                                         [](const GameControl &control) { return control.role == Role::STEER; });

		for (unsigned player = 0; player < 2; player++) {
			const bool pad = !m_controls.pad_name(player).empty(), joystick = m_controls.joystick_id(player) != 0;

			m_menu_state.input_device[player] = joystick && (wheel || !pad) ? Device::JOYSTICK :
			                                    pad                         ? Device::PAD : Device::KEYBOARD;
		}
	}
	m_menu_opened = true;
	m_display.open_menu(page);
	m_menu_pictures = MENU_PICTURES;
}

void
MenuSession::close()
{
	m_display.close_menu();
	m_nav.reset();
	m_held.reset();
	m_menu_state.advanced = false;
	m_menu_state.nav = {};
	m_keys.swallow();
	if (m_audio)
		m_audio->pause(false);
}

void
MenuSession::show(const std::string &game)
{
	for (size_t g = 0; g < m_menu_state.games.size(); g++)
		if (m_menu_state.games[g].first == game)
			show_game(g);
}

/* Not heard in the background, when the user says so */
void
MenuSession::focus(bool focused)
{
	m_focused = focused;
	if (m_audio)
		m_audio->set_heard(m_focused || m_menu_state.background);
}

void
MenuSession::wait()
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

void
MenuSession::fill_bar(BarState &bar) const
{
	const MenuState &state = m_menu_state;

	bar.fullscreen = m_display.fullscreen();
	bar.show_speed = state.show_speed;
	bar.background = state.background;
	bar.verbose = state.verbose;
	bar.verbose_given = state.verbose_given;
	bar.sides = state.sides;
	bar.volume = state.volume;
	bar.roms_given = state.roms_given;
	bar.bios_given = state.bios_given;
	bar.silent = state.silent != nullptr;
	bar.theme = state.theme;
	for (size_t a = 0; a < Hotkeys::ACTIONS; a++) {
		const HotKey &key = m_hotkeys.of(static_cast<Hotkeys::Action>(a)).keys[0];

		bar.keys[a] = key.key == SDLK_UNKNOWN ? std::string() : Hotkeys::text(key);
	}
}

/* As the menu does them: kept in odin.ini the same way */
BarState::Ask
MenuSession::do_bar(BarState &bar)
{
	using Ask = BarState::Ask;
	MenuState &state = m_menu_state;

	if (bar.sides != state.sides || bar.volume != state.volume || bar.show_speed != state.show_speed ||
	    bar.background != state.background || bar.verbose != state.verbose) {
		const MenuState was = state;

		state.sides = bar.sides;
		state.volume = bar.volume;
		state.show_speed = bar.show_speed;
		state.background = bar.background;
		state.verbose = bar.verbose;
		apply(was);
	}

	const Ask ask = std::exchange(bar.ask, Ask::NONE);

	switch (ask) {
	case Ask::OPEN:
		m_dialog.ask_archive(m_display.window(), state.roms, ARCHIVE_ASKED);
		return Ask::NONE;
	case Ask::ROMS:
	case Ask::BIOS:
		ask_folder(ask == Ask::ROMS ? MenuState::Folder::ROMS : MenuState::Folder::BIOS);
		return Ask::NONE;
	case Ask::FULLSCREEN:
		m_display.toggle_fullscreen();
		return Ask::NONE;
	default:
		return ask;
	}
}

/* The folders of the games, of the BIOS and of the saves wait for the next
 * launch; the save states go to theirs now. A folder of their own by
 * default is no line in odin.ini. */
std::optional<FolderDialog::Answer>
MenuSession::take_folder()
{
	using Folder = MenuState::Folder;
	MenuState &state = m_menu_state;
	const std::optional<FolderDialog::Answer> answer = m_dialog.answer();

	m_menu_pictures = MENU_PICTURES;
	if (!answer || answer->asked == ARCHIVE_ASKED)
		return answer;
	state.no_picker = !answer->error.empty();
	if (state.no_picker)
		std::fprintf(stderr, "ERROR: no folder picker: %s\n", answer->error.c_str());
	if (answer->folder.empty())
		return std::nullopt;

	const std::string &folder = answer->folder;

	switch (static_cast<Folder>(answer->asked)) {
	case Folder::ROMS:
		state.roms = folder;
		keep("paths", "roms", folder);
		break;
	case Folder::BIOS:
		state.bios = folder;
		keep("paths", "bios", folder);
		break;
	case Folder::SAVES:
		state.saves = folder;
		keep("paths", "saves", folder == SaveFolder::root() ? std::nullopt : std::optional(folder));
		break;
	case Folder::STATES:
		state.states = m_states = folder;
		keep("paths", "states", folder == default_states() ? std::nullopt : std::optional(folder));
		break;
	case Folder::ARTWORK:
		state.artwork = folder;
		keep("paths", "artwork", folder == artwork_folder() ? std::nullopt : std::optional(folder));
		if (m_game) {
			m_display.set_sides(static_cast<Display::Sides>(state.sides),
			                    artwork_file(folder, m_game->name, "bezel"));
			m_controls.set_picture_hole(m_display.picture_hole());
		}
		break;
	default:
		break;
	}
	return std::nullopt;
}

/* The picker opened where the folder is now; that of the BIOS, else where
 * the games are */
void
MenuSession::ask_folder(MenuState::Folder folder)
{
	using Folder = MenuState::Folder;
	MenuState &state = m_menu_state;

	state.no_picker = false;
	m_dialog.ask(m_display.window(), folder == Folder::ROMS ? state.roms :
	             folder == Folder::BIOS ? (state.bios.empty() ? state.roms : state.bios) :
	             folder == Folder::SAVES ? state.saves :
	             folder == Folder::ARTWORK ? state.artwork : state.states, static_cast<int>(folder));
	m_menu_pictures = MENU_PICTURES;
}

void
MenuSession::keep(const char *key, const std::optional<std::string> &value)
{
	keep("display", key, value);
}

void
MenuSession::keep_window()
{
	const MenuState &state = m_menu_state;

	m_custom_waits = false;
	if (state.custom_w)
		keep("window", std::to_string(state.custom_w) + "x" + std::to_string(state.custom_h));
	else
		keep("window", state.window == 1 ? std::nullopt : std::optional(std::to_string(state.window)));
}

void
MenuSession::follow_hand_size()
{
	MenuState &state = m_menu_state;
	unsigned w = 0, h = 0;

	if (state.window_given || !m_display.hand_size(w, h) || (w == state.custom_w && h == state.custom_h))
		return;
	state.custom_w = w;
	state.custom_h = h;
	m_custom_waits = true;
}

void
MenuSession::keep_custom()
{
	follow_hand_size();
	if (m_custom_waits)
		keep_window();
}

/* In odin.ini; with none, for the time Odin runs */
void
MenuSession::keep(const std::string &section, const std::string &key, const std::optional<std::string> &value)
{
	if (m_settings_file.empty())
		m_settings.set(section, key, value);
	else
		m_settings.change(m_settings_file, section, key, value);
}

} /* namespace odin */
