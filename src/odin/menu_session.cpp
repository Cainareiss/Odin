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
#include "odin/seats.hpp"

namespace odin {

namespace {

/* The make and model of the gamepad of the player shown in the menu; empty
 * when the player has none */
std::string
pad_shown(const MenuState &state)
{
	for (const MenuState::Plugged &pad : state.plugged)
		if (pad.player == static_cast<int>(state.player))
			return pad.guid;
	return {};
}

} /* namespace */

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

		action = m_hotkeys.pad_action(combo >= 0 ? combo : Hotkeys::NO_PAD);
		if (m_controls.guide_tapped())
			action = m_hotkeys.pad_action(Hotkeys::GUIDE_ALONE);
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
	/* The page Input shows the gamepad of the player when there is one,
	 * until the user picks a device */
	if (!m_menu_opened)
		for (unsigned player = 0; player < 2; player++)
			m_menu_state.gamepad[player] = !m_controls.pad_name(player).empty();
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

/* The page Input waits for an input: Escape lets it go; a key, or on a
 * gamepad a button (Guide held, then another: both), a stick or a trigger
 * from half its way, is the one. True when the event was taken. */
bool
MenuSession::catch_input(const SDL_Event &event)
{
	MenuState &state = m_menu_state;

	/* The buttons of the menu follow what is held, as ever */
	const std::vector<MenuNav::Action> nav = m_nav.event(event);
	const bool back = std::find(nav.begin(), nav.end(), MenuNav::BACK) != nav.end();

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

			return action != Hotkeys::Action::QUIT && action != Hotkeys::Action::FULLSCREEN;
		}
		/* The keys Odin works itself go to no control of the game */
		if (event.key.key != SDLK_ESCAPE && !m_hotkeys.takes(event.key.key)) {
			HostInput input{};

			input.kind = HostInput::Kind::KEY;
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

		if (axis && std::abs(value) < ControlPanel::PRESSED)
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
MenuSession::presses(int button) const
{
	const std::vector<HostInput> &inputs = m_nav.inputs(MenuNav::PRESS);

	return std::any_of(inputs.begin(), inputs.end(), [button](const HostInput &in) {
		return in.kind == HostInput::Kind::PAD && in.pad.type == PadInput::Type::BUTTON &&
		       in.pad.index == button && in.pad.with < 0;
	});
}

/* An input of a gamepad caught: a button (held with Guide when it is), or
 * an axis past half its way */
void
MenuSession::catch_pad(bool axis, int index, int value)
{
	MenuState &state = m_menu_state;
	HostInput input{};

	if (!state.catching)
		return;
	input.kind = HostInput::Kind::PAD;
	input.pad.index = static_cast<uint8_t>(index);
	input.pad.type = !axis ? PadInput::Type::BUTTON : PadInput::Type::AXIS;
	if (!axis && m_catch_guide)
		input.pad.with = SDL_GAMEPAD_BUTTON_GUIDE;
	/* A stick on a part that is pressed: its half */
	if (axis && index < SDL_GAMEPAD_AXIS_LEFT_TRIGGER && state.catching->part != 0)
		input.pad.type = value < 0 ? PadInput::Type::AXIS_NEGATIVE : PadInput::Type::AXIS_POSITIVE;
	/* A button on an analog control goes on its half: of a pedal, or of
	 * each way in turn */
	if (!axis && state.catching->control >= 0 && state.catching->part == 0) {
		const auto c = static_cast<size_t>(state.catching->control);
		const GameControl &control = panel_shown().controls()[c];

		if (control.type == GameControl::Type::ANALOG) {
			const bool two_ways = GameControl::half(control.low, false);
			const MenuSlot other_way = { state.catching->control, 2, state.catching->role };

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
MenuSession::catch_hotkey(const SDL_Event &event)
{
	MenuState &state = m_menu_state;
	const auto action = static_cast<Hotkeys::Action>(state.hotkey_catching->first);
	const size_t slot = state.hotkey_catching->second;
	const std::vector<MenuNav::Action> nav = m_nav.event(event);

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
		if (slot < Hotkeys::KEYS && !Hotkeys::is_modifier(event.key.key)) {
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

		if (slot == Hotkeys::KEYS && (combo >= 0 || tapped)) {
			m_hotkeys.set_pad(action, combo >= 0 ? combo : Hotkeys::GUIDE_ALONE);
			state.hotkey_catching.reset();
			keep_hotkeys();
		} else if (std::find(nav.begin(), nav.end(), MenuNav::BACK) != nav.end()) {
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
MenuSession::keep_hotkeys()
{
	for (size_t a = 0; a < Hotkeys::ACTIONS; a++) {
		const auto action = static_cast<Hotkeys::Action>(a);
		const Hotkeys::Binding &binding = m_hotkeys.of(action);
		const std::optional<std::string> line = binding == Hotkeys::standard(action) ? std::nullopt :
		                                        std::optional(Hotkeys::line(binding));

		keep("hotkeys", Hotkeys::id(action), line);
	}
}

/* The input caught takes the place of those of its kind on that part; the
 * next part waiting is caught then */
void
MenuSession::caught(const HostInput &input)
{
	const MenuSlot slot = *m_menu_state.catching;
	const Bindings &bindings = panel_shown().bindings();
	const size_t c = static_cast<size_t>(slot.control);
	std::vector<HostInput> inputs = slot.control < 0 ? bindings.cabinet(slot.role) : bindings.part(c, slot.part);

	m_menu_state.catching.reset();
	std::erase_if(inputs, [&input](const HostInput &in) { return in.kind == input.kind; });
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
MenuSession::change_inputs(bool to_defaults)
{
	const MenuState &state = m_menu_state;
	const auto changed = [&](const std::vector<HostInput> &inputs, const std::vector<HostInput> &standard) {
		return kind_changed(inputs, standard, to_defaults);
	};
	std::vector<Bindings::Line> lines;
	{
		const Bindings &bindings = panel_shown().bindings();
		const std::vector<GameControl> &controls = panel_shown().controls();

		for (size_t c = 0; c < controls.size(); c++) {
			if (controls[c].player != state.player)
				continue;
			const bool parts = controls[c].type != GameControl::Type::BUTTON;

			for (unsigned part = parts ? 1 : 0; part <= (parts ? 2u : 0u); part++)
				lines.push_back(bindings.line(c, part, changed(bindings.part(c, part), bindings.standard(c, part))));
			if (controls[c].type == GameControl::Type::ANALOG && GameControl::half(controls[c].low, false))
				lines.push_back(bindings.line(c, 0, changed(bindings.part(c, 0), bindings.standard(c, 0))));
		}
		for (const Role role : { state.player ? Role::COIN2 : Role::COIN1, Role::TEST }) {
			if (role == Role::TEST && state.player)
				continue;
			lines.push_back(bindings.cabinet_line(role, changed(bindings.cabinet(role), bindings.cabinet_standard(role))));
		}
	}
	for (const Bindings::Line &line : lines)
		keep_binding(line);
}

/* The inputs of a part without those of the device shown, or with those
 * of its defaults */
std::vector<HostInput>
MenuSession::kind_changed(std::vector<HostInput> inputs, const std::vector<HostInput> &standard, bool to_defaults) const
{
	const auto kind = m_menu_state.on_pad() ? HostInput::Kind::PAD : HostInput::Kind::KEY;

	std::erase_if(inputs, [kind](const HostInput &in) { return in.kind == kind; });
	if (to_defaults)
		for (const HostInput &in : standard)
			if (in.kind == kind)
				inputs.push_back(in);
	return inputs;
}

/* X and Y of a gamepad on the part picked: its inputs of the device shown
 * cleared, or back to their defaults */
void
MenuSession::change_part(const MenuSlot &slot, bool to_defaults)
{
	const Bindings &bindings = panel_shown().bindings();

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
MenuSession::undo()
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
MenuSession::keep_binding(const Bindings::Line &line, const std::string &game)
{
	const std::string section = "controls." + (game.empty() ? game_shown() : game);

	keep(section, line.key, line.value);
	m_controls.rebind(m_settings);
	if (m_other)
		m_other.emplace(m_other_controls, m_settings, game_shown());
}

std::string
MenuSession::game_shown() const
{
	const MenuState &state = m_menu_state;

	return state.shown < state.games.size() ? state.games[state.shown].first :
	       m_board ? m_board->game_name() : std::string();
}

/* Another game in the page Input: a panel of its own, made from the list
 * and the settings, for its inputs only */
void
MenuSession::show_game(size_t game)
{
	MenuState &state = m_menu_state;
	const std::optional<Game> other = game < state.games.size() && game != state.running ?
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

/* The menu drawn once from what is so now (F11 may have changed the
 * display), then what it changed done */
void
MenuSession::draw_menu()
{
	MenuState &state = m_menu_state;

	state.choosing = m_dialog.open();
	state.fullscreen = m_display.fullscreen();
	{
		int w = 0, h = 0;

		SDL_GetWindowSize(m_display.window(), &w, &h);
		state.window_w = static_cast<unsigned>(std::max(w, 0));
		state.window_h = static_cast<unsigned>(std::max(h, 0));
	}
	follow_hand_size();
	m_display.display_size(state.display_w, state.display_h);
	state.refresh = m_display.refresh();
	state.vsync_off = m_options.unthrottled ? "off at an unlimited pace" : nullptr;
	state.refresh_short = state.refresh > 0.0f && state.refresh < Display::LEAST_REFRESH;
	state.panel = &panel_shown();
	state.touched.clear();
	state.now.clear();
	{
		const HostState host = m_controls.host_state();

		for (size_t c = 0; c < state.panel->controls().size(); c++) {
			state.now.push_back(state.panel->now(host, c));
			state.touched.push_back(state.now.back().on);
		}
		for (const Role role : { Role::COIN1, Role::COIN2, Role::TEST })
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
		using Nav = MenuNav;
		const int kind = !state.pads[0].empty() ? state.pad_types[0] :
		                 !state.pads[1].empty() ? state.pad_types[1] : SDL_GAMEPAD_TYPE_UNKNOWN;
		const Nav::Action pills[] = { Nav::PAGE_BEFORE, Nav::PAGE_AFTER, Nav::GAME_BEFORE, Nav::GAME_AFTER,
		                              Nav::PRESS,       Nav::CLEAR,      Nav::RESET };

		for (size_t i = 0; i < state.pills.size(); i++) {
			const std::vector<HostInput> &inputs = m_nav.inputs(pills[i]);
			const auto pad = std::find_if(inputs.begin(), inputs.end(),
			                              [](const HostInput &in) { return in.kind == HostInput::Kind::PAD; });

			state.pills[i] = pad == inputs.end() ? std::string() : Menu::input_name(pad->pad, kind);
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
	for (size_t a = 0; a < Hotkeys::ACTIONS; a++)
		state.hotkeys[a] = m_hotkeys.of(static_cast<Hotkeys::Action>(a));
	if (m_audio || m_listing)
		state.devices = AudioOut::devices();
	/* The pad shakes as the cabinet of the game shown, unless
	 * [players.<game>] says off (the last line says) */
	state.can_shake = m_other ? m_other_shakes : m_game && m_game->shakes();
	state.shake_from = m_other ? m_other_shakers[state.player & 1] :
	                   m_game ? m_game->shaker(state.player & 1) : std::string();
	state.shake = vibration_on(m_settings, game_shown());
	/* The outputs of the game shown, lit as the board left them */
	state.outputs.clear();
	for (const GameOutput &output : m_other || !m_game ? m_other_outputs : m_game->outputs)
		state.outputs.push_back({ output.name, output.role, !m_other && m_board && output.on(m_board->io_outputs()) });
	if (m_outputs) {
		m_outputs->welcome();
		state.programs = m_outputs->clients();
	}
	/* The gamepads plugged in and their players; those of a game that does
	 * not run as its settings would seat them */
	const std::vector<Controls::Plugged> plugged = m_controls.plugged();
	std::array<SDL_JoystickID, 2> seats{};

	if (m_other) {
		std::array<std::string, 2> wanted;
		std::vector<OpenPad> open;

		for (const auto &[key, value] : m_settings.section("players." + game_shown()))
			if (key == "p1" || key == "p2")
				wanted[key[1] - '1'] = value.substr(0, value.find(' '));
		for (const Controls::Plugged &pad : plugged)
			open.push_back({ pad.id, pad.guid });
		seats = seat_pads(open, wanted, {});
	}
	state.plugged.clear();
	for (const Controls::Plugged &pad : plugged)
		state.plugged.push_back({ pad.name, pad.guid, pad.kind,
		                          !m_other ? pad.player : pad.id == seats[0] ? 0 : pad.id == seats[1] ? 1 : -1,
		                          pad.connection });
	/* A player with no gamepad is shown on the keyboard */
	for (unsigned player = 0; player < 2; player++) {
		if (!state.gamepad[player] ||
		    std::any_of(state.plugged.begin(), state.plugged.end(),
		                [player](const MenuState::Plugged &pad) { return pad.player == static_cast<int>(player); }))
			continue;
		state.gamepad[player] = false;
		if (state.player == player) {
			state.catching.reset();
			state.catch_next.clear();
			state.mapping.reset();
		}
	}
	state.tuning = PadTuning::of(m_settings, pad_shown(state), false);
	/* What moves says so as it is drawn */
	state.moving = false;

	const MenuState was = state;

	m_display.draw_menu(state);
	state.nav = {};
	apply(was);

	if (state.hotkeys_default) {
		m_hotkeys.reset();
		keep_hotkeys();
		state.hotkeys_default = false;
		m_menu_pictures = MENU_PICTURES;
	}
	if (state.choose != MenuState::Folder::NONE) {
		ask_folder(state.choose);
		state.choose = MenuState::Folder::NONE;
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
		keep(section, key, value);
		state.want.reset();
		m_menu_pictures = MENU_PICTURES;
	}
	if (state.ask == MenuState::Ask::DEFAULTS) {
		change_inputs(true);
	} else if (state.ask == MenuState::Ask::CLEAR) {
		m_cleared_game = game_shown();
		m_cleared = m_settings.section("controls." + m_cleared_game);
		change_inputs(false);
		m_undo_until = SDL_GetTicks() + UNDO_MS;
	} else if (state.ask == MenuState::Ask::UNDO) {
		undo();
	} else if (state.ask == MenuState::Ask::CLOSE_GAME) {
		m_closing = true;
	} else if ((state.ask == MenuState::Ask::PART_DEFAULTS || state.ask == MenuState::Ask::PART_CLEAR) &&
	           state.picked) {
		change_part(*state.picked, state.ask == MenuState::Ask::PART_DEFAULTS);
	}
	if (state.ask != MenuState::Ask::NONE)
		m_menu_pictures = MENU_PICTURES;
	state.ask = MenuState::Ask::NONE;
	m_controls.settle();
}

/* A setting changed in the menu is done now, and kept in odin.ini when it
 * is not the one by default; the pace waits for the next launch */
void
MenuSession::apply(const MenuState &was)
{
	const MenuState &now = m_menu_state;
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
	if (now.window != was.window || now.custom_w != was.custom_w || now.custom_h != was.custom_h) {
		/* Custom keeps the window where it is, of the size it has */
		if (!now.custom_w)
			m_display.resize(now.window);
		else if (now.custom_w != now.window_w || now.custom_h != now.window_h)
			m_display.resize_to(now.custom_w, now.custom_h);
		keep_window();
	}
	if (now.sides != was.sides) {
		const auto sides = static_cast<Display::Sides>(now.sides);

		m_display.set_sides(sides, m_game ? artwork_file(now.artwork, m_game->name, "bezel") : std::string());
		m_controls.set_picture_hole(m_display.picture_hole());
		keep("bezel", sides == SIDES ? std::nullopt : std::optional<std::string>(SIDES_NAMES[now.sides]));
	}
	if (now.vsync != was.vsync) {
		const auto vsync = static_cast<Display::Vsync>(now.vsync);

		m_display.set_vsync(m_options.unthrottled ? Display::Vsync::OFF : vsync);
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
	if (now.theme != was.theme)
		keep("menu", "theme", now.theme ? std::optional<std::string>(THEMES[now.theme].key) : std::nullopt);
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
		const MenuState::SoundGame &game = now.sound_games[g], &before = was.sound_games[g];
		const std::string section = "sound." + game.name;
		const bool runs = m_game && game.name == m_game->name && m_audio;

		if (game.played != before.played) {
			if (runs)
				m_audio->change(mode_of(game.played), now.device);
			keep(section, "mode", game.played == game.cabinet_played || game.played < 0 ? std::nullopt :
			                      std::optional<std::string>(MODE_NAMES[game.played]));
		}
		if (game.level != before.level) {
			char db[16];

			std::snprintf(db, sizeof(db), "%g", static_cast<double>(game.level));
			if (runs)
				m_audio->set_option_level(game.level);
			keep(section, "option_level", game.level == game.standard ? std::nullopt : std::optional<std::string>(db));
		}
	}
	/* The vibration of the game shown: at once for the game that runs */
	if (now.shake != was.shake && now.can_shake) {
		const std::string section = "players." + game_shown();

		if (!m_other && m_game)
			m_controls.feel_game(*m_game, now.shake);
		keep(section, "vibration", on_off(now.shake, true));
	}
	/* The dead zone and the trigger threshold of the gamepad shown: of its
	 * make and model, a line of their own where they differ from
	 * [gamepads] */
	if (const std::string guid = pad_shown(now);
	    !guid.empty() && (now.tuning.dead_zone != was.tuning.dead_zone ||
	                      now.tuning.trigger_threshold != was.tuning.trigger_threshold)) {
		const PadTuning all = PadTuning::of(m_settings, {}, false);
		const std::string section = "gamepad." + guid;
		const auto line = [](int percent, int standard) {
			return percent == standard ? std::nullopt : std::optional(std::to_string(percent));
		};

		if (now.tuning.dead_zone != was.tuning.dead_zone)
			keep(section, "dead_zone", line(now.tuning.dead_zone, all.dead_zone));
		if (now.tuning.trigger_threshold != was.tuning.trigger_threshold)
			keep(section, "trigger_threshold", line(now.tuning.trigger_threshold, all.trigger_threshold));
		m_controls.retune();
	}
	/* The programs of lamps: the port opened or closed now, for the game
	 * that runs */
	if (now.network != was.network) {
		if (now.network && m_game)
			m_outputs = OutputsNetwork::open(m_game->name, m_game->outputs);
		else
			m_outputs.reset();
		m_menu_state.network_failed = now.network && m_game && !m_outputs;
		m_menu_state.programs = 0;
		keep("outputs", "network", on_off(now.network, false));
	}
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
