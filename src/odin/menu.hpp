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

#include <GL/glew.h>
#include <SDL3/SDL.h>

struct ImGuiContext;

namespace odin {

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
	void build();

	/* The drawing thread: the picture of the last frame, from its
	 * framebuffer, becomes the background (0: there is none yet, black);
	 * then the menu is drawn on it in the window. */
	void freeze(GLuint picture_fb, unsigned w, unsigned h);
	void draw(unsigned window_w, unsigned window_h);
	void release();

private:
	void start_gl();
	void size_blur(unsigned w, unsigned h);

	SDL_Window *m_window;
	std::string m_title;
	ImGuiContext *m_imgui;
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
