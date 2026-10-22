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

#include "odin/library.hpp"

#include <SDL3/SDL.h>

#include "odin/controls.hpp"
#include "odin/rom_files.hpp"
#include "odin/save_folder.hpp"

namespace odin {

namespace {

/* The games of the list in the library, each with the files it lacks in
 * the folders now; that one chosen */
void
scan_library(LibraryState &state, const GameList &games, const Options &options, const std::string &chosen)
{
	RomFiles files(options.rom_path, options.bios_path);

	state.games.clear();
	state.selected = 0;
	for (const auto &[name, title] : games.titles("hikaru")) {
		LibraryState::Game game{ name, title, {} };

		for (const RomFiles::File &file : games.missing(files, name))
			game.missing.push_back(file.name + " (" + (file.set.empty() ? name : file.set) + ".zip)");
		if (name == chosen)
			state.selected = static_cast<int>(state.games.size());
		state.games.push_back(std::move(game));
	}
	state.roms = options.rom_path;
	state.bios = options.bios_path;
	state.roms_given = options.given.roms;
	state.bios_given = options.given.bios;
}

/* The pictures of the games in the library, from the folder of the
 * artwork: the user's <game>_logo.png, else the <game>_title.png Odin
 * kept; a game with none there forgets the one it had */
void
library_pictures(Display &display, const LibraryState &state, const std::string &artwork)
{
	const std::string folder = artwork_folder(artwork);

	for (const LibraryState::Game &game : state.games) {
		const std::string logo = folder.empty() ? "" : artwork_file(folder, game.name, "logo");
		const std::string path = logo.empty() && !folder.empty() ? artwork_file(folder, game.name, "title") : logo;
		SDL_Surface *const png = path.empty() ? nullptr : SDL_LoadPNG(path.c_str());
		SDL_Surface *const rgba = png ? SDL_ConvertSurface(png, SDL_PIXELFORMAT_RGBA32) : nullptr;

		SDL_DestroySurface(png);
		display.add_library_picture(game.name, rgba);
	}
}

} /* namespace */

/* The library, Odin started without a game, until one is chosen there or
 * by Open... (its name, and the folder of its archive: empty, that of the
 * games), or Odin is to quit (no name). The game played before is chosen
 * again; `failed`, one that did not start, says so. The bar is over it, at
 * all times; the menu opens over it as over a game, its page Input on the
 * game chosen. What they change is kept in odin.ini and taken at once. */
Opened
library(const Options &command, const GameList &games, Settings &settings, const std::string &settings_file,
        Display &display, Keyboard &keys, Pointers *pointers, const std::string &last, const std::string &failed)
{
	using Ask = BarState::Ask;
	/* Some 60 pictures a second, between the events */
	static constexpr Sint32 PICTURE_MS = 16;
	Options options = with_settings(command, settings);
	/* The gamepads of the menu: the controls of no game */
	Controls controls(keys, {}, {}, settings, {});

	controls.set_pointers(pointers);
	MenuSession menus(options, display, keys, controls, settings, settings_file, games, nullptr, nullptr, nullptr,
	                  nullptr, network_on(settings));
	LibraryState state;
	BarState bar;
	Uint64 note_until = 0;
	const auto chosen = [&state] {
		return state.games.empty() ? std::string() : state.games[static_cast<size_t>(state.selected)].name;
	};
	const auto open_menu = [&](int page) {
		menus.show(chosen());
		menus.open(page);
	};
	const auto say = [&](const std::string &note) {
		bar.note = note;
		note_until = SDL_GetTicks() + NOTE_MS;
	};

	scan_library(state, games, options, last);
	state.failed = failed;
	display.open_library();
	SDL_ShowCursor();
	library_pictures(display, state, options.artwork);
	for (;;) {
		SDL_Event event;

		while (SDL_PollEvent(&event)) {
			if (event.type == SDL_EVENT_QUIT)
				return {};
			/* A folder chosen: the games looked for there */
			if (event.type == menus.picker_event()) {
				const std::string artwork = menus.state().artwork;
				std::string note;

				if (const std::optional<FolderDialog::Answer> archive = menus.take_folder()) {
					if (const std::optional<Opened> opened = open_archive(*archive, games, note))
						return *opened;
					if (!note.empty())
						say(note);
					continue;
				}
				options = with_settings(command, settings);
				scan_library(state, games, options, chosen());
				if (menus.state().artwork != artwork)
					library_pictures(display, state, options.artwork);
				continue;
			}
			if (event.type == SDL_EVENT_WINDOW_DISPLAY_CHANGED)
				display.display_changed();
			if (event.type == SDL_EVENT_WINDOW_FOCUS_GAINED || event.type == SDL_EVENT_WINDOW_FOCUS_LOST)
				menus.focus(event.type == SDL_EVENT_WINDOW_FOCUS_GAINED);
			if (display.menu_shown()) {
				const MenuSession::Asked asked = menus.event(event);

				if (asked == MenuSession::Asked::QUIT)
					return {};
				if (asked == MenuSession::Asked::CLOSE) {
					menus.close();
					display.open_library();
				}
				continue;
			}

			/* The hotkeys that work without a game: the menu (not while a
			 * menu of the bar is open: Escape closes it), the whole display,
			 * quitting */
			std::optional<Hotkeys::Action> action;
			const Hotkeys &hotkeys = menus.hotkeys();

			/* The joysticks plugged in and out, for the page Input */
			if (event.type == SDL_EVENT_JOYSTICK_ADDED || event.type == SDL_EVENT_JOYSTICK_REMOVED) {
				controls.event(event);
				continue;
			}
			if (event.type == SDL_EVENT_GAMEPAD_ADDED || event.type == SDL_EVENT_GAMEPAD_REMOVED ||
			    event.type == SDL_EVENT_GAMEPAD_AXIS_MOTION || event.type == SDL_EVENT_GAMEPAD_BUTTON_DOWN ||
			    event.type == SDL_EVENT_GAMEPAD_BUTTON_UP) {
				controls.event(event);

				const int combo = controls.guide_combo();

				action = hotkeys.pad_action(combo >= 0 ? combo : Hotkeys::NO_PAD);
				if (controls.guide_tapped())
					action = hotkeys.pad_action(Hotkeys::GUIDE_ALONE);
				/* The buttons of the menu choose and play, as the arrows
				 * and Enter */
				for (const MenuNav::Action nav : menus.nav(event)) {
					using Nav = MenuNav;

					state.nav.across += nav == Nav::RIGHT ? 1 : nav == Nav::LEFT ? -1 : 0;
					state.nav.down += nav == Nav::DOWN ? 1 : nav == Nav::UP ? -1 : 0;
					state.nav.press = state.nav.press || nav == Nav::PRESS;
				}
			} else if (event.type == SDL_EVENT_KEY_DOWN && !event.key.repeat) {
				action = hotkeys.key_action(event.key.key, event.key.mod);
			}
			if (action == Hotkeys::Action::QUIT)
				return {};
			if (action == Hotkeys::Action::FULLSCREEN) {
				display.toggle_fullscreen();
				continue;
			}
			if (action == Hotkeys::Action::MENU && !bar.open) {
				open_menu(-1);
				continue;
			}
			display.menu_event(event);
		}
		if (display.menu_shown()) {
			menus.wait();
			continue;
		}

		state.press = menus.press_name();
		menus.fill_bar(bar);
		bar.game = false;
		if (SDL_GetTicks() >= note_until)
			bar.note.clear();
		display.draw_library(state, bar);
		/* Without a game, the bar has no game to close, save, load or
		 * pause */
		switch (menus.do_bar(bar)) {
		case Ask::SETTINGS:
			open_menu(bar.page);
			continue;
		case Ask::QUIT:
			return {};
		default:
			break;
		}
		if (state.play)
			return { *state.play, {} };
		if (state.choose != LibraryState::Folder::NONE) {
			menus.ask_folder(state.choose == LibraryState::Folder::ROMS ? MenuState::Folder::ROMS :
			                                                                    MenuState::Folder::BIOS);
			state.choose = LibraryState::Folder::NONE;
		}
		state.choosing = menus.choosing();
		SDL_WaitEventTimeout(nullptr, PICTURE_MS);
	}
}

} /* namespace odin */
