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

#ifndef ODIN_SESSION_HPP
#define ODIN_SESSION_HPP

#include <chrono>
#include <memory>
#include <optional>
#include <string>

#include <SDL3/SDL.h>

#include "odin/host/ui/menu_session.hpp"

namespace odin {

/* Why a game ends: the user quits Odin, its last frame is done, it cannot
 * run, the user closes it or opens another one (Open... of the bar), or
 * its cabinet is to be switched off and on: Reset, or the seat of a link
 * taken (Hikaru::restart_asked) */
enum class Ended { QUIT, LAST_FRAME, FAILED, CLOSED, OPENED, RESTART };

/* What Session needs from a running board */
class GameBoard {
public:
	virtual ~GameBoard() = default;
	virtual const std::string &game_name() const = 0;
	virtual unsigned frame() const = 0;
	virtual void run_frame() = 0;
	virtual bool restart_asked() const = 0;
	virtual bool save_state(const std::string &path) = 0;
	virtual bool load_state(const std::string &path) = 0;
	virtual std::span<const uint8_t> io_outputs() const = 0;
	virtual const std::vector<int16_t> &sound(bool optional) const = 0;
	virtual std::optional<int> drive_torque() const = 0;
};

/* A game being run: its board, with the window, the keyboard and the
 * gamepads around it, the menu and the bar over it */
class Session {
public:
	Session(const Options &options, const Game &game, GameBoard &board, Hikaru *hikaru, Display &display,
	        Keyboard &keys, Controls &controls, AudioOut *audio, std::unique_ptr<OutputsNetwork> outputs, bool network,
	        Settings &settings, const std::string &settings_file, const GameList &games)
		: m_options(options), m_game(game), m_board(board), m_display(display), m_keys(keys), m_controls(controls),
		  m_audio(audio), m_games(games),
		  m_menus(options, display, keys, controls, settings, settings_file, games, &game, hikaru, audio,
		          std::move(outputs), network),
		  m_paused(false), m_next()
	{
	}

	/* Runs the frames until the user quits, closes the game or opens
	 * another one (opened() then), or the last one asked is done. */
	Ended run();
	const Opened &opened() const { return m_opened; }

private:
	/* The bar: shown that long after the mouse moved, never in a game the
	 * mouse aims in; drawn that often while the game is paused */
	static constexpr Uint64 BAR_MS = 2500;
	static constexpr Sint32 BAR_PAUSED_MS = 16;

	Ended frames();
	bool process_events();
	/* What Odin does on a hotkey in the game: true to quit */
	bool act(Hotkeys::Action action);
	/* The menu over the game, which waits; closed, its frames start again
	 * now */
	void open_menu(int page = -1);
	void close_menu();
	/* The bar: the mouse shows it and works it (true: the event was the
	 * bar's alone); shown or hidden as the mouse says; its state from what
	 * is so now; what it changed and asked, done; a note of Odin, in the
	 * bottom-left corner a while (Menu::note) */
	bool bar_event(const SDL_Event &event);
	void follow_bar();
	void fill_bar();
	void do_bar();
	void say(const std::string &note);
	/* The folder the picker of the system gave; the game of an archive
	 * Open... gave, in place of this one */
	void take_folder();
	void load_or_save_state(bool load);
	std::string state_path() const;
	bool state_saved() const;
	void save_state_at_frame();
	void limit_frame_rate();

	const Options &m_options;
	const Game &m_game;
	GameBoard &m_board;
	Display &m_display;
	Keyboard &m_keys;
	Controls &m_controls;
	AudioOut *m_audio;	/* nullptr: no sound */
	const GameList &m_games;
	MenuSession m_menus;
	/* The bar, until when it is shown since the mouse moved, the buttons
	 * of the mouse pressed on it, until when its word is said */
	BarState m_bar;
	Uint64 m_bar_until = 0, m_note_until = 0;
	unsigned m_bar_buttons = 0;
	/* How the bar ended the game, and the game it opened */
	std::optional<Ended> m_end;
	Opened m_opened;
	bool m_paused;
	std::chrono::steady_clock::time_point m_next;	/* when the next frame is due */
};

} /* namespace odin */

#endif /* ODIN_SESSION_HPP */
