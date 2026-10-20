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

#ifndef ODIN_MENU_HPP
#define ODIN_MENU_HPP

#include <array>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <GL/glew.h>
#include <SDL3/SDL.h>

#include "imgui/imgui.h"
#include "odin/control_panel.hpp"
#include "odin/hotkeys.hpp"
#include "odin/menu_kit.hpp"
#include "odin/roles.hpp"

namespace odin {

class MenuKit;

/* What the page Input changes: a part of a control of the panel (0 the
 * control, 1 and 2 its halves or a lever down and up, bindings.hpp), or a
 * role of the cabinet (control -1) */
struct MenuSlot {
	int control = -1;
	unsigned part = 0;
	Role role = Role::COIN1;
};

/* What the pages of the menu show and change. The thread of the board
 * keeps it and does what changed (main.cpp). */
struct MenuState {
	/* Display: the 3D that many times finer, the texture filter (1 for
	 * none), the window at start that many times the screen, the whole
	 * display, frames without a limit (at the next launch), the frames a
	 * second in the title */
	unsigned scale = 1, anisotropy = 1, window = 1;
	bool fullscreen = false, unthrottled = false, show_speed = true;
	/* Said on the command line: shown, not changed */
	bool scale_given = false, anisotropy_given = false, window_given = false, fullscreen_given = false,
	     unthrottled_given = false;
	/* The graphics card, and the size of the display in pixels */
	std::string gpu;
	unsigned display_w = 0, display_h = 0;
	/* odin.ini keeps what changes: not with ODIN_CONFIG=none */
	bool kept = true;

	/* Audio: the devices the system plays on and the one chosen (empty:
	 * the system's), the volume of Odin from 0 to 100, its sound kept while
	 * its window is in the background; why there is no sound now (none:
	 * there is) */
	std::vector<std::string> devices;
	std::string device;
	unsigned volume = 100;
	bool background = true;
	const char *silent = nullptr;
	/* The games whose optional sound board the user may hear: those with
	 * speakers for it on the cabinet, and Racer, whose cabinet has it shake
	 * the panel. How they are played (0 the main board, 1 both on two
	 * channels, 2 the four channels apart; -1 another way the command line
	 * says, for the game that runs, which `locked` says) and how the cabinet
	 * plays them; the level of the optional board in the two channels and
	 * the one of the list, in dB */
	struct SoundGame {
		std::string name, title;
		int played, cabinet_played;
		float level, standard;
		bool locked;
	};
	std::vector<SoundGame> sound_games;

	/* Hotkeys: the keys and the gamepad buttons of each action of Odin; the
	 * action whose next key (0 or 1) or buttons (2) are caught; all of them
	 * back as by default */
	std::array<Hotkeys::Binding, Hotkeys::ACTIONS> hotkeys{};
	std::optional<std::pair<size_t, size_t>> hotkey_catching;
	bool hotkeys_default = false;

	/* Outputs: those of the cabinet of the game shown (their names, what
	 * they are), lit while on in the game that runs; sent to the programs
	 * of lamps, the port taken by another program, how many programs take
	 * them */
	struct Output {
		std::string name;
		GameOutput::Role role;
		bool on;
	};
	std::vector<Output> outputs;
	bool network = false, network_failed = false;
	size_t programs = 0;

	/* Paths: the folders of the games and of their saves, at the next
	 * launch (those the command line or ODIN_SAVES gives are shown, not
	 * changed), of the save states; the settings file and the lists of the
	 * 3D programs (empty: none). The board saying each access, at the next
	 * launch. */
	std::string roms, saves, states, settings_file, shader_lists, log_file;
	bool roms_given = false, saves_given = false;
	bool verbose = false, verbose_given = false;
	/* A folder to ask the system for; its picker is open; the system has
	 * none */
	enum class Folder { NONE, ROMS, SAVES, STATES } choose = Folder::NONE;
	bool choosing = false, no_picker = false;

	/* Input: the games of the list (name, title), the one whose controls
	 * are shown and the one that runs, another one asked */
	std::vector<std::pair<std::string, std::string>> games;
	size_t shown = 0, running = 0;
	std::optional<size_t> show;
	/* The panel of the cabinet of the game shown (its controls and their
	 * inputs), which are worked now (the controls, then COIN1, COIN2 and
	 * TEST) and where its controls are, the gamepad of each player (its
	 * name, its kind: pad_drawing.hpp) */
	const ControlPanel *panel = nullptr;
	std::vector<bool> touched;
	std::vector<ControlNow> now;
	std::array<std::string, 2> pads;
	std::array<int, 2> pad_types{};
	/* The gamepads plugged in (name, GUID, kind, the player it is the pad
	 * of in the game shown, -1: none, by a wire or not), the one asked for
	 * the player shown */
	struct Plugged {
		std::string name, guid;
		int kind, player;
		SDL_JoystickConnectionState connection;
	};
	std::vector<Plugged> plugged;
	std::optional<std::pair<std::string, std::string>> want;
	/* The player shown, and the inputs of each player: of the gamepad, else
	 * of the keyboard (and the mouse, shown); what the gamepad of each player
	 * does now; the controls as a list rather than on the drawing */
	unsigned player = 0;
	std::array<bool, 2> gamepad{};
	std::array<PadState, 2> pad_states{};
	/* The keys held now */
	std::vector<SDL_Keycode> keys_held;
	bool classic = false;
	/* The game shown shakes the pads as its cabinet does (the nozzle of
	 * Brave; experimental: the shaker of Racer, the shocks of NASCAR), and
	 * whether it does */
	bool can_shake = false, shake = true;
	/* What shakes on the cabinet ("output NOZZLE VIBRATION"); the dead
	 * zone and the trigger threshold of the gamepad shown (its make and
	 * model's); its advanced options open over the drawing */
	std::string shake_from;
	PadTuning tuning;
	bool advanced = false;
	/* In a window not wide enough for both, the cabinet in place of the
	 * device */
	bool cabinet_shown = false;
	bool on_pad() const { return gamepad[player & 1]; }
	/* The part whose next input is caught, and those caught after it, in
	 * turn; the part the frame of the gamepad is on (drawn as if caught);
	 * what the page asks of the inputs of the player and the device shown,
	 * of all its parts or of that part, Undo while it is offered */
	std::optional<MenuSlot> catching, picked;
	std::vector<MenuSlot> catch_next;
	/* Start mapping: each control of the player in turn, the place of the
	 * one caught now in the list of the page, since when (SDL ticks) */
	std::optional<size_t> mapping;
	uint64_t mapping_since = 0;
	/* How far the button that presses is held toward Skip, 0 to 1 */
	float skip_hold = 0.0f;
	enum class Ask { NONE, DEFAULTS, CLEAR, UNDO, PART_DEFAULTS, PART_CLEAR } ask = Ask::NONE;
	bool undo = false;

	/* What the gamepad asked since the last picture (menu_nav.hpp): the
	 * page or the game before (-1) or after (1), the frame of focus moved
	 * up or down, a choice or a slider moved, pressed, cleared or set to
	 * its default; a list dropped open closed */
	struct Nav {
		int page = 0, game = 0, move = 0, side = 0;
		bool press = false, clear = false, reset = false, back = false;
	} nav;
	/* The names of the buttons that change the page and the game, before
	 * and after, then of those that change, clear and set to its default
	 * the part picked, on the gamepad of player 1 (empty: no pill) */
	std::array<std::string, 7> pills;
};

/* The menu of Odin, drawn with Dear ImGui over the picture of the last
 * frame, frozen and blurred, while the game is paused (main.cpp).
 *
 * It is made at its first opening: nothing of it runs before. Two threads
 * work it (display.hpp), never both at once: the one that runs the board
 * gives it the events and builds it, the drawing thread, which holds the
 * OpenGL context, freezes the picture and draws. Its OpenGL objects are
 * let go of where the context is (release) before it is destroyed. */
class Menu {
public:
	Menu(SDL_Window *window, const std::string &title);
	~Menu();
	Menu(const Menu &) = delete;
	Menu &operator=(const Menu &) = delete;

	/* An input of a gamepad of that kind (pad_drawing.hpp), as its maker
	 * prints it */
	static std::string input_name(const PadInput &pad, int kind);

	/* The thread of the board */
	void open();
	void close();
	void event(const SDL_Event &event);
	void build(MenuState &state);
	/* A list dropped open, which Escape closes before the menu */
	bool popup_open() const;

	/* The drawing thread: the picture of the last frame, from its
	 * framebuffer, becomes the background (0: there is none yet, black);
	 * then the menu is drawn on it in the window. */
	void freeze(GLuint picture_fb, unsigned w, unsigned h);
	void draw(unsigned window_w, unsigned window_h);
	void release();

private:
	/* The pages, in the order of their tabs */
	enum Page { INPUT, DISPLAY, AUDIO, HOTKEYS, OUTPUTS, PATHS, ABOUT, PAGES };

	void follow_scale();
	void navigate(MenuState &state);
	void page_input(MenuKit &kit, MenuState &state) const;
	void page_display(MenuKit &kit, MenuState &state) const;
	void page_audio(MenuKit &kit, MenuState &state) const;
	void page_hotkeys(MenuKit &kit, MenuState &state) const;
	void page_outputs(MenuKit &kit, MenuState &state) const;
	void page_paths(MenuKit &kit, MenuState &state) const;
	void page_about(MenuKit &kit) const;
	/* What the pages say at the bottom; Input, at that place, that wide
	 * and high: the control caught or touched, its inputs, what to do.
	 * The height it takes, drawn when `draw`. */
	static std::string help(int page, const MenuState &state);
	static float input_foot(const MenuKit &kit, MenuState &state, ImVec2 at, float width, float height, bool draw);
	static std::string hotkeys_help(const MenuState &state);
	/* A button of a gamepad of that kind (pad_drawing.hpp), as its maker
	 * prints it */
	static std::string button_name(int button, int kind);
	/* The row of the games of the pages Input and Outputs: its bottom */
	static float games_row(const MenuKit &kit, MenuState &state, ImVec2 at, float width, bool focusable = false);
	void start_gl();
	void size_blur(unsigned w, unsigned h);

	SDL_Window *m_window;
	std::string m_title;
	ImGuiContext *m_imgui;
	/* The style at a scale of 1, and the scale of the display now */
	ImGuiStyle m_style;
	float m_scale;
	int m_page;
	/* The frame the gamepad moves on the page */
	MenuFocus m_focus;
	/* What Odin had where Dear ImGui changes it while the menu is open:
	 * two hints of the mouse, the pointer shown or hidden */
	std::array<std::optional<std::string>, 2> m_hints;
	bool m_cursor_shown;

	/* The background: the picture at half its size, blurred back and
	 * forth between two textures, the result in the first */
	bool m_gl;
	GLuint m_vao, m_blur, m_show;
	GLint m_blur_step, m_show_light;
	std::array<GLuint, 2> m_blur_tex, m_blur_fb;
	unsigned m_blur_w, m_blur_h;
	bool m_frozen;
};

} /* namespace odin */

#endif /* ODIN_MENU_HPP */
