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

#include "odin/session.hpp"

#include <cstdio>
#include <filesystem>
#include <thread>

namespace odin {

namespace {

/* A frame of the cabinet, 60 a second */
constexpr std::chrono::nanoseconds FRAME(1000000000 / 60);

} /* namespace */

/* F1 and F4: <game>.odinstate in the folder of the save states */
std::string
Session::state_path() const
{
	const std::string &states = m_menus.states();

	return (states.empty() ? "" : states + "/") + m_board.game_name() + ".odinstate";
}

void
Session::load_or_save_state(bool load)
{
	const std::string path = state_path();
	std::error_code error;

	/* None saved yet: the game goes on */
	if (load && !std::filesystem::exists(path, error)) {
		std::fprintf(stderr, "no state '%s' to load yet\n", path.c_str());
		return;
	}
	if (!load && !m_menus.states().empty())
		std::filesystem::create_directories(m_menus.states(), error);
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
		if (event.type == SDL_EVENT_KEY_DOWN || event.type == SDL_EVENT_MOUSE_BUTTON_DOWN ||
		    event.type == SDL_EVENT_GAMEPAD_BUTTON_DOWN || event.type == SDL_EVENT_JOYSTICK_BUTTON_DOWN)
			m_touched = true;
		if (event.type == m_menus.picker_event()) {
			take_folder();
			continue;
		}
		if (event.type == SDL_EVENT_WINDOW_DISPLAY_CHANGED)
			m_display.display_changed();
		if (event.type == SDL_EVENT_WINDOW_FOCUS_GAINED || event.type == SDL_EVENT_WINDOW_FOCUS_LOST)
			m_menus.focus(event.type == SDL_EVENT_WINDOW_FOCUS_GAINED);
		if (m_display.menu_shown()) {
			const MenuSession::Asked asked = m_menus.event(event);

			if (asked == MenuSession::Asked::CLOSE)
				close_menu();
			quit = asked == MenuSession::Asked::QUIT || quit;
			continue;
		}
		/* A menu of the bar open: the keys are its own (Escape closes it)
		 * but those of quitting and of the whole display; a key let go is
		 * let go for the game too */
		if ((event.type == SDL_EVENT_KEY_DOWN || event.type == SDL_EVENT_KEY_UP) && m_display.bar_shown() &&
		    m_bar.open) {
			const std::optional<Hotkeys::Action> action =
				event.key.down && !event.key.repeat ? m_menus.hotkeys().key_action(event.key.key, event.key.mod) :
				                                      std::nullopt;

			if (!event.key.down)
				m_keys.set(event.key.key, false);
			if (action == Hotkeys::Action::QUIT || action == Hotkeys::Action::FULLSCREEN)
				quit = act(*action) || quit;
			else
				m_display.bar_event(event);
			continue;
		}
		if (bar_event(event))
			continue;
		if (m_controls.event(event)) {
			const Hotkeys &hotkeys = m_menus.hotkeys();
			const int combo = m_controls.guide_combo();

			if (const auto action = hotkeys.pad_action(combo >= 0 ? combo : Hotkeys::NO_PAD))
				quit = act(*action) || quit;
			if (m_controls.guide_tapped())
				if (const auto action = hotkeys.pad_action(Hotkeys::GUIDE_ALONE))
					quit = act(*action) || quit;
			continue;
		}
		switch (event.type) {
		case SDL_EVENT_KEY_DOWN:
			m_keys.set(event.key.key, true);
			if (!event.key.repeat)
				if (const auto action = m_menus.hotkeys().key_action(event.key.key, event.key.mod))
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
Session::act(Hotkeys::Action action)
{
	using Action = Hotkeys::Action;

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

void
Session::open_menu(int page)
{
	m_bar_until = 0;
	m_bar_buttons = 0;
	m_menus.open(page);
}

void
Session::close_menu()
{
	m_menus.close();
	m_next = std::chrono::steady_clock::now();
}

/* The mouse moved shows the bar a while (not where the system first puts
 * it: no move); where it aims in the game, only from the top edge of the
 * window. Its buttons pressed on the bar, or with one of its menus open,
 * are the bar's, and let go there; all the rest of the mouse is the game's
 * too. */
bool
Session::bar_event(const SDL_Event &event)
{
	switch (event.type) {
	case SDL_EVENT_MOUSE_MOTION:
		if ((event.motion.xrel != 0.0f || event.motion.yrel != 0.0f) &&
		    (!m_controls.aims() || event.motion.y < BAR_EDGE))
			m_bar_until = std::max(m_bar_until, SDL_GetTicks() + (m_controls.aims() ? BAR_AIM_MS : BAR_MS));
		follow_bar();
		m_display.bar_event(event);
		return false;
	case SDL_EVENT_MOUSE_BUTTON_DOWN:
		if (!m_display.bar_shown() || !(m_bar.wants_mouse || m_bar.open || event.button.y < m_bar.bottom))
			return false;
		m_bar_buttons |= 1u << event.button.button;
		m_display.bar_event(event);
		return true;
	case SDL_EVENT_MOUSE_BUTTON_UP: {
		const bool bar = (m_bar_buttons >> event.button.button) & 1u;

		m_bar_buttons &= ~(1u << event.button.button);
		m_display.bar_event(event);
		return bar;
	}
	case SDL_EVENT_MOUSE_WHEEL:
		m_display.bar_event(event);
		return m_display.bar_shown() && m_bar.wants_mouse;
	case SDL_EVENT_WINDOW_MOUSE_LEAVE:
		m_bar_until = 0;
		m_display.bar_event(event);
		return false;
	case SDL_EVENT_WINDOW_MOUSE_ENTER:
	case SDL_EVENT_WINDOW_FOCUS_GAINED:
	case SDL_EVENT_WINDOW_FOCUS_LOST:
		m_display.bar_event(event);
		return false;
	default:
		return false;
	}
}

/* Shown while the mouse moved lately, is on it, holds a button pressed on
 * it or a menu of it is open; never with the menu. Hidden while the game is
 * paused, the picture is shown again without it. */
void
Session::follow_bar()
{
	const bool held = m_display.bar_shown() && (m_bar.wants_mouse || m_bar.open || m_bar_buttons);
	const bool shown = !m_display.menu_shown() && (SDL_GetTicks() < m_bar_until || held);
	std::error_code error;

	if (shown == m_display.bar_shown())
		return;
	m_display.show_bar(shown);
	m_bar.wants_mouse = m_bar.open = false;
	m_bar_buttons = 0;
	if (shown)
		m_bar.saved = std::filesystem::exists(state_path(), error);
	else if (m_paused)
		m_display.show_again(nullptr);
}

void
Session::fill_bar()
{
	m_menus.fill_bar(m_bar);
	m_bar.title = m_game.title;
	m_bar.fps = m_display.fps();
	m_bar.paused = m_paused;
	m_bar.game = true;
	if (SDL_GetTicks() >= m_note_until)
		m_bar.note.clear();
}

void
Session::do_bar()
{
	using Ask = BarState::Ask;
	std::error_code error;

	switch (m_menus.do_bar(m_bar)) {
	case Ask::CLOSE:
		m_end = Ended::CLOSED;
		break;
	case Ask::SAVE_STATE:
		load_or_save_state(false);
		m_bar.saved = std::filesystem::exists(state_path(), error);
		break;
	case Ask::LOAD_STATE:
		load_or_save_state(true);
		m_touched = true;
		break;
	case Ask::SETTINGS:
		open_menu(m_bar.page);
		break;
	case Ask::PAUSE:
		act(Hotkeys::Action::PAUSE);
		break;
	case Ask::QUIT:
		m_end = Ended::QUIT;
		break;
	default:
		break;
	}
}

/* At the right of the bar, shown a while */
void
Session::say(const std::string &note)
{
	m_bar.note = note;
	m_note_until = SDL_GetTicks() + NOTE_MS;
	m_bar_until = std::max(m_bar_until, m_note_until);
	follow_bar();
}

void
Session::take_folder()
{
	std::string note;

	if (const std::optional<FolderDialog::Answer> archive = m_menus.take_folder()) {
		if (const std::optional<Opened> opened = open_archive(*archive, m_games, note)) {
			m_opened = *opened;
			m_end = Ended::OPENED;
		} else if (!note.empty()) {
			say(note);
		}
	}
}

/* The picture of its title for the library (<game>_title.png in the
 * folder of the artwork), once: at its title_frame, the game left to
 * itself since power-on, when none is kept yet */
void
Session::capture_title()
{
	const std::string &folder = m_menus.state().artwork;

	if (!m_game.title_frame || m_board.frame() != m_game.title_frame || m_touched || folder.empty() ||
	    !artwork_file(folder, m_game.name, "title").empty())
		return;

	const std::string path = folder + "/" + m_game.name + "_title.png";

	if (m_display.save_picture(path))
		std::printf("library: the title of %s kept in '%s'\n", m_game.name.c_str(), path.c_str());
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

Ended
Session::run()
{
	const Ended ended = frames();

	/* The next game, or the window without one, finds the menu and the
	 * bar closed */
	if (m_display.menu_shown())
		close_menu();
	m_display.show_bar(false);
	return ended;
}

Ended
Session::frames()
{
	while (!process_events()) {
		if (m_end)
			return *m_end;
		if (m_options.num_frames > 0 && m_board.frame() >= m_options.num_frames)
			return Ended::LAST_FRAME;
		if (m_display.menu_shown()) {
			m_menus.wait();
			if (m_menus.closing())
				m_end = Ended::CLOSED;
			continue;
		}
		follow_bar();
		if (m_paused) {
			/* Asleep until something comes, the next key; the bar drawn
			 * over the picture while it is shown */
			if (m_display.bar_shown()) {
				fill_bar();
				m_display.show_again(&m_bar);
				do_bar();
				SDL_WaitEventTimeout(nullptr, BAR_PAUSED_MS);
			} else {
				SDL_WaitEvent(nullptr);
			}
			continue;
		}
		m_display.begin_frame();
		m_controls.update();
		m_board.run_frame();
		m_controls.show(m_board.io_outputs(), m_board.sound(true), m_board.drive_torque());
		if (OutputsNetwork *const outputs = m_menus.outputs())
			outputs->show(m_board.io_outputs());
		if (m_audio)
			m_audio->play(m_board.sound(false), m_board.sound(true));

		const bool bar = m_display.bar_shown();

		if (bar)
			fill_bar();
		m_display.end_frame(m_board.frame(), std::chrono::steady_clock::now() > m_next + FRAME, bar ? &m_bar : nullptr);
		if (bar)
			do_bar();
		save_state_at_frame();
		capture_title();
		if (m_menus.tested_at(m_board.frame()))
			press_escape();
		if (!m_options.unthrottled)
			limit_frame_rate();
	}
	return Ended::QUIT;
}

} /* namespace odin */
