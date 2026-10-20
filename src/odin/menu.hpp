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
#include <vector>

#include <GL/glew.h>
#include <SDL3/SDL.h>

#include "imgui/imgui.h"
#include "odin/roles.hpp"

namespace odin {

class ControlPanel;
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

	/* Input: the game, the panel of its cabinet (its controls and their
	 * inputs), which are worked now (the controls, then COIN1, COIN2 and
	 * TEST), the gamepad of each player (its name, SDL_GamepadType) */
	std::string game;
	const ControlPanel *panel = nullptr;
	std::vector<bool> touched;
	std::array<std::string, 2> pads;
	std::array<int, 2> pad_types{};
	/* The player shown, and the inputs: of the gamepad, else of the
	 * keyboard (and the mouse, shown) */
	unsigned player = 0;
	bool gamepad = false;
	/* The part whose next input is caught; what the page asks of the
	 * inputs of the player and the device shown, Undo while it is offered */
	std::optional<MenuSlot> catching;
	enum class Ask { NONE, DEFAULTS, CLEAR, UNDO } ask = Ask::NONE;
	bool undo = false;
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

	/* The thread of the board */
	void open();
	void close();
	void event(const SDL_Event &event);
	void build(MenuState &state);

	/* The drawing thread: the picture of the last frame, from its
	 * framebuffer, becomes the background (0: there is none yet, black);
	 * then the menu is drawn on it in the window. */
	void freeze(GLuint picture_fb, unsigned w, unsigned h);
	void draw(unsigned window_w, unsigned window_h);
	void release();

private:
	/* The pages, in the order of their tabs */
	enum Page { INPUT, DISPLAY, ABOUT, PAGES };

	void follow_scale();
	void page_input(MenuKit &kit, MenuState &state) const;
	void page_display(MenuKit &kit, MenuState &state) const;
	void page_about(MenuKit &kit) const;
	/* What the page says at the bottom */
	static const char *display_help(const MenuState &state);
	void start_gl();
	void size_blur(unsigned w, unsigned h);

	SDL_Window *m_window;
	std::string m_title;
	ImGuiContext *m_imgui;
	/* The style at a scale of 1, and the scale of the display now */
	ImGuiStyle m_style;
	float m_scale;
	int m_page;
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
