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

#include "odin/host/ui/menu_session.hpp"

#include <algorithm>
#include <cstdio>

#include "odin/host/app/user_files.hpp"
#include "odin/host/input/seats.hpp"

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
	m_other_link = other->link;
	m_other_title = other->title;
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
	/* How the game shown links cabinets */
	const Game::Link shown_link = m_other ? m_other_link : m_game ? m_game->link : Game::Link{};

	state.link_shown_title = m_other ? m_other_title : m_game ? m_game->title : std::string();
	state.link_shown_cabinets = shown_link.cabinets;
	state.link_shown_seat = shown_link.seat_in == Game::Link::In::NONE ? MenuState::SEAT_NONE :
	                        shown_link.seat_letter ? MenuState::SEAT_LETTER : MenuState::SEAT_NUMBER;
	/* The outputs of the game shown, lit as the board left them */
	state.outputs.clear();
	for (const GameOutput &output : m_other || !m_game ? m_other_outputs : m_game->outputs)
		state.outputs.push_back({ output.name, output.role, !m_other && m_board && output.on(m_board->io_outputs()) });
	if (m_outputs) {
		m_outputs->welcome();
		state.programs = m_outputs->clients();
	}
	/* The network board of the game that runs, as it is linked now */
	state.link_running = m_board != nullptr;
	if (m_board) {
		const Hikaru::LinkStatus link = m_board->link_status();

		state.link_board_now = link.board;
		state.link_linked = link.linked;
		state.link_failed = link.failed;
		state.link_previous = link.previous;
		state.link_next_plugged = link.next;
	} else {
		state.link_board_now = state.link_linked = state.link_failed = false;
		state.link_previous = state.link_next_plugged = false;
	}
	/* The gamepads and the joysticks plugged in and their players; those of
	 * a game that does not run as its settings would seat them */
	const auto list = [this](std::vector<MenuState::Plugged> &out, const std::vector<Controls::Plugged> &plugged,
	                         const char *keys) {
		std::array<SDL_JoystickID, 2> seats{};

		if (m_other) {
			std::array<std::string, 2> wanted;
			std::vector<OpenPad> open;

			for (const auto &[key, value] : m_settings.section("players." + game_shown()))
				for (unsigned player = 0; player < 2; player++)
					if (key == keys + std::to_string(player + 1))
						wanted[player] = value.substr(0, value.find(' '));
			for (const Controls::Plugged &one : plugged)
				open.push_back({ one.id, one.guid });
			seats = seat_pads(open, wanted, {});
		}
		out.clear();
		for (const Controls::Plugged &one : plugged)
			out.push_back({ one.name, one.guid, one.kind,
			                !m_other ? one.player : one.id == seats[0] ? 0 : one.id == seats[1] ? 1 : -1,
			                one.connection });
	};

	list(state.plugged, m_controls.plugged(), "p");
	list(state.joysticks, m_controls.joysticks(), "joy");
	/* A player without the device shown is shown on the keyboard */
	for (unsigned player = 0; player < 2; player++) {
		const std::vector<MenuState::Plugged> &shown =
			state.input_device[player] == MenuState::Device::PAD ? state.plugged : state.joysticks;

		if (state.input_device[player] == MenuState::Device::KEYBOARD ||
		    std::any_of(shown.begin(), shown.end(),
		                [player](const MenuState::Plugged &one) { return one.player == static_cast<int>(player); }))
			continue;
		state.input_device[player] = MenuState::Device::KEYBOARD;
		if (state.player == player) {
			state.catching.reset();
			state.catch_next.clear();
			state.mapping.reset();
		}
	}
	state.tuning = PadTuning::of(m_settings, pad_shown(state), false);
	/* What moves says so as it is drawn */
	state.moving = false;

	/* The light guns seen (one chosen and never plugged in since Odin
	 * started, as the settings name it), that of each player in the game
	 * shown, the players with a sight there */
	state.guns.clear();
	if (Pointers *const pointers = m_controls.pointers()) {
		pointers->poll();
		for (const Pointers::Device &device : pointers->devices())
			state.guns.push_back({ device.identity, device.name, device.present, device.absolute });
	}
	state.gun = {};
	for (const auto &[key, value] : m_settings.section("players." + game_shown())) {
		if (key != "gun1" && key != "gun2")
			continue;

		const size_t space = value.find(' ');
		const std::string identity = value.substr(0, space);

		state.gun[key[3] - '1'] = identity;
		if (std::none_of(state.guns.begin(), state.guns.end(),
		                 [&identity](const MenuState::Gun &gun) { return gun.identity == identity; }))
			state.guns.push_back({ identity, space == std::string::npos ? identity : value.substr(space + 1), false, true });
	}
	state.sights = {};
	for (const GameControl &control : panel_shown().controls())
		if (control.role == Role::GUN_X)
			state.sights[control.player & 1] = true;
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
	/* The light gun a player wants for the game shown: kept, and taken at
	 * once by the game that runs */
	if (state.want_gun) {
		const auto &[player, gun] = *state.want_gun;

		want_gun(game_shown(), player, gun.identity, gun.name, !m_other);
		state.want_gun.reset();
		m_menu_pictures = MENU_PICTURES;
	}
	if (state.want) {
		const auto &[guid, name] = *state.want;

		want_pad(game_shown(), state.player, guid, name, false, !m_other);
		state.want.reset();
		m_menu_pictures = MENU_PICTURES;
	}
	/* The joystick a player wants for the game: [players.<game>] joy1 or
	 * joy2 */
	if (state.want_joystick) {
		const auto &[guid, name] = *state.want_joystick;

		want_pad(game_shown(), state.player, guid, name, true, !m_other);
		state.want_joystick.reset();
		m_menu_pictures = MENU_PICTURES;
	}
	if (state.ask == MenuState::Ask::DEFAULTS) {
		change_inputs(true);
	} else if (state.ask == MenuState::Ask::CLEAR) {
		clear_inputs();
	} else if (state.ask == MenuState::Ask::UNDO) {
		undo();
	} else if (state.ask == MenuState::Ask::CLOSE_GAME) {
		m_closing = true;
	} else if (state.ask == MenuState::Ask::BACK) {
		m_leaving = true;
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
	if (now.filter != was.filter) {
		m_display.set_filter(now.filter);
		keep("filter", now.filter.empty() ? std::nullopt : std::optional<std::string>(now.filter));
	}
	if (now.anisotropy != was.anisotropy) {
		m_display.set_anisotropy(now.anisotropy);
		keep("anisotropy", number(now.anisotropy, 1));
	}
	if (now.upscale_mode != was.upscale_mode && now.upscale_mode < std::size(UPSCALE_MODES)) {
		m_display.set_upscale_mode(static_cast<DisplayUpscaleMode>(now.upscale_mode));
		keep("upscale_mode", now.upscale_mode == 0 ? std::nullopt :
		                     std::optional<std::string>(UPSCALE_MODES[now.upscale_mode]));
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
			/* SDL keeps the device open on as many channels as it had:
			 * four or two again take the game started again */
			const bool four = mode_of(game.played) == AudioOut::Mode::QUAD;
			const bool later = runs && four != (m_audio->mode() == AudioOut::Mode::QUAD);

			m_menu_state.sound_games[g].later = later;
			if (runs && !later)
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
	if (now.shake != was.shake && now.can_shake)
		keep_vibration(game_shown(), now.shake, !m_other && m_game);
	/* The dead zone and the trigger threshold of the gamepad shown */
	if (const std::string guid = pad_shown(now); !guid.empty())
		keep_tuning(guid, now.tuning, was.tuning);
	/* The network board: for the games that start from now */
	if (now.link_board != was.link_board)
		keep("network", "board", now.link_board == 0 ? std::nullopt :
		     std::optional<std::string>(NetLinkSettings::board_name(static_cast<NetLinkSettings::Board>(now.link_board))));
	if (now.link_port != was.link_port)
		keep("network", "port", number(now.link_port, NetCable::PORT));
	if (now.link_next != was.link_next)
		keep("network", "next", now.link_next.empty() ? std::nullopt : std::optional<std::string>(now.link_next));
	if (now.link_cabinet != was.link_cabinet)
		keep("network", "cabinet", number(now.link_cabinet, 1));
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

/* Clear mapping: the inputs of the device shown of the player shown, none;
 * Undo offered a while */
void
MenuSession::clear_inputs()
{
	m_cleared_game = game_shown();
	m_cleared = m_settings.section("controls." + m_cleared_game);
	change_inputs(false);
	m_undo_until = SDL_GetTicks() + UNDO_MS;
}

/* [players.<game>] p1 or p2 (none: the first free gamepad), joy1 or joy2 */
void
MenuSession::want_pad(const std::string &game, unsigned player, const std::string &guid, const std::string &name,
                      bool joystick, bool runs)
{
	const std::string section = "players." + game;

	if (joystick) {
		if (runs)
			m_controls.want_joystick(player, guid);
		keep(section, player ? "joy2" : "joy1", guid + " " + name);
		return;
	}
	if (runs)
		m_controls.want(player, guid);
	keep(section, player ? "p2" : "p1", guid.empty() ? std::nullopt : std::optional(guid + " " + name));
}

/* [players.<game>] gun1 or gun2 (none: no light gun) */
void
MenuSession::want_gun(const std::string &game, unsigned player, const std::string &identity, const std::string &name,
                      bool runs)
{
	keep("players." + game, player ? "gun2" : "gun1",
	     identity.empty() ? std::nullopt : std::optional<std::string>(identity + " " + name));
	if (runs)
		m_controls.want_gun(player, identity);
}

/* [players.<game>] vibration: a line only when off */
void
MenuSession::keep_vibration(const std::string &game, bool on, bool runs)
{
	if (runs)
		m_controls.feel_game(*m_game, on);
	keep("players." + game, "vibration", on ? std::nullopt : std::optional<std::string>("off"));
}

/* The dead zone and the trigger threshold of a make and model of gamepad:
 * a line of their own where they differ from [gamepads] */
void
MenuSession::keep_tuning(const std::string &guid, const PadTuning &now, const PadTuning &was)
{
	if (now.dead_zone == was.dead_zone && now.trigger_threshold == was.trigger_threshold)
		return;

	const PadTuning all = PadTuning::of(m_settings, {}, false);
	const std::string section = "gamepad." + guid;
	const auto line = [](int percent, int standard) {
		return percent == standard ? std::nullopt : std::optional(std::to_string(percent));
	};

	if (now.dead_zone != was.dead_zone)
		keep(section, "dead_zone", line(now.dead_zone, all.dead_zone));
	if (now.trigger_threshold != was.trigger_threshold)
		keep(section, "trigger_threshold", line(now.trigger_threshold, all.trigger_threshold));
	m_controls.retune();
}

} /* namespace odin */
