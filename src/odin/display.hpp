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

#ifndef ODIN_DISPLAY_HPP
#define ODIN_DISPLAY_HPP

#include <atomic>
#include <cstdint>
#include <cstdio>
#include <functional>
#include <memory>
#include <semaphore>
#include <string>
#include <vector>

#include <GL/glew.h>
#include <SDL3/SDL.h>

#include "odin/hikaru.hpp"
#include "odin/picture.hpp"

namespace odin {

class Keyboard;
class Menu;
struct BarState;
struct LibraryState;
struct MenuState;
class Renderer;
class Worker;

/* The screen of the cabinet: a window, and in it the pictures the renderer
 * (renderer.hpp) draws from what the graphics chips leave.
 *
 * The frames are drawn on a thread of their own, which holds the OpenGL
 * context: the board runs a frame while the one before is drawn. What a
 * drawing reads of the board is told in renderer.hpp; the board waits for
 * the drawing before it changes any of it (Hikaru::Screen). Everything else
 * here is called from the thread that runs the board.
 *
 * The environment asks for what is measured on the pictures:
 *
 *  ODIN_SCREENSHOT=<prefix>	<prefix>-<frame>.ppm every 60 frames, or
 *				every ODIN_SCREENSHOT_EVERY frames
 *  ODIN_PICTURE_FP=1		a line for the picture of each frame on
 *				stderr, FNV-1a 64 of its pixels, to compare
 *				two builds
 *  ODIN_SPEED=1		a line every 60 frames on stderr: the frames
 *				a second over them and the longest of them
 *				in milliseconds
 *  ODIN_STATS=1		at the end, on stderr: the host time of the
 *				drawings and what the board waited for them
 *				(and the time of each part of the board, see
 *				hikaru.cpp)
 *  ODIN_DRAW_TIMES=1		at the end, on stderr: what each part of the
 *				drawings cost, on their thread and for the
 *				graphics card (renderer_times.hpp)
 *  ODIN_LATE_DRAW=1		a test of the thread: the drawing of a frame
 *				starts when the board has run the next one,
 *				or has to wait for it. The pictures must be
 *				the same.
 *
 * and for the looks at what is drawn, see Renderer::Options: HR_LOG,
 * HR_LOG_FRAME, ODIN_LIST_FP, ODIN_LIST_DUMP, ODIN_LISTS_TEST,
 * ODIN_DEBUG_KEYS. */
class Display : public Hikaru::Screen {
public:
	/* How the frames wait for the display: not at all (one may be torn),
	 * each of them, or those on time (a late one is shown at once, so
	 * that the game keeps its 60 a second) */
	enum class Vsync : uint8_t { OFF, ON, ADAPTIVE };
	/* Around the picture where the window is wider than it: black, the
	 * picture itself blurred and dimmer, or the bezel of the game (black
	 * without one) */
	enum class Sides : uint8_t { OFF, BLUR, BEZEL };

	struct Options {
		unsigned scale;		/* the 3D that many times finer than the screen, 1 to 6 */
		unsigned anisotropy;	/* of the texture filter, 1 for none as on the board */
		unsigned window;	/* the window that many times the screen */
		bool fullscreen;
		bool show_speed;	/* the frames a second in the title */
		Vsync vsync;
		/* The window of that size in points instead, the one the user gave
		 * it by hand (0: as `window` says) */
		unsigned window_w, window_h;
	};

	/* A display that shows fewer frames a second would hold the board
	 * back where each frame waits for it: the game slower than the 60 of
	 * the cabinet, and its sound short of more than the sample a frame
	 * AudioOut makes up (1 in 735). The frames on time alone wait on any
	 * display: the late ones catch up. */
	static constexpr float LEAST_REFRESH = 59.92f;

	/* The window, black until a board is attached; nothing, with the
	 * reason on stderr, if it cannot be opened. The keyboard outlives it. */
	static std::unique_ptr<Display> make(const Keyboard &keys, const Options &options);
	~Display();
	Display(const Display &) = delete;
	Display &operator=(const Display &) = delete;

	/* The window becomes the screen of a board, the programs of its game
	 * prepared behind the loading screen with its title; detach before
	 * the board goes. Then the window is black again. */
	void attach(Hikaru &board, const std::string &title);
	void detach();
	/* Black again, the board detached, when the window has to be drawn */
	void show_black();

	/* A frame of the board: begin_frame before the board runs it,
	 * end_frame once it has, with its number, whether it ended after its
	 * time at 60 frames a second, and the bar over it while it is shown,
	 * built then from its state. */
	void begin_frame();
	void end_frame(unsigned frame, bool late, BarState *bar = nullptr);

	/* Between the window and the whole display */
	void toggle_fullscreen();

	/* What the menu sets, the board waiting meanwhile. The window takes
	 * `times` the screen unless it is on the whole display. */
	void set_scale(unsigned scale);
	void set_anisotropy(unsigned level);
	bool fullscreen() const;
	void set_fullscreen(bool on);
	void resize(unsigned times);
	/* The window of that size, as the display has room for it, unless it
	 * is on the whole display */
	void resize_to(unsigned w, unsigned h);
	/* The size the user gave the window by hand, in points: a window, not
	 * maximized, of another size than Odin gave it last (false: none). It
	 * is then the size Odin gives it back after the whole display. */
	bool hand_size(unsigned &w, unsigned &h);
	void set_show_speed(bool on);
	/* Each frame waits only where the display keeps up (LEAST_REFRESH);
	 * again when the window goes to another display */
	void set_vsync(Vsync vsync);
	void display_changed() { set_vsync(m_vsync); }
	/* The frames a second of the display the window is on (0: not
	 * known) */
	float refresh() const;
	/* The graphics card, and the size in pixels of the display the
	 * window is on */
	const std::string &gpu() const { return m_gpu; }
	void display_size(unsigned &w, unsigned &h) const;
	/* The window, for the dialogs of the system; the list of the 3D
	 * programs of the game kept for the user (empty: none) */
	SDL_Window *window() const { return m_window; }
	const std::string &program_list() const { return m_program_list; }

	/* The menu (menu.hpp), over the picture of the last frame while the
	 * board waits: open_menu freezes that picture, draw_menu builds the
	 * menu from the state and shows it once. */
	void open_menu(int page = -1);
	void close_menu();
	bool menu_shown() const { return m_menu_shown; }
	void menu_event(const SDL_Event &event);
	void draw_menu(MenuState &state);
	/* The library over the whole window, the bar over it, no board
	 * attached: opened, then built from their states and shown once a
	 * picture */
	void open_library();
	void draw_library(LibraryState &state, BarState &bar);
	/* A picture of a game for the library, RGBA (the menu takes it; none:
	 * the one it had forgotten), after it is opened */
	void add_library_picture(const std::string &game, SDL_Surface *rgba);
	/* The picture of the last frame in a PNG file, no larger than 640 by
	 * 480 (the folder made): false, said on stderr, when it cannot be */
	bool save_picture(const std::string &path);
	/* What is around the picture; with BEZEL, a PNG laid over the window
	 * where it is wider than the screen of the hardware, as large as the
	 * window holds it, the picture in its hole (empty: none) */
	void set_sides(Sides sides, const std::string &bezel);
	/* The hole of the bezel, where the picture goes (none: no bezel, or
	 * one with no see-through middle) */
	const PictureHole &picture_hole() const { return m_hole; }
	bool menu_popup_open() const;
	/* The bar at the top of the window (menu.hpp: BarState), over the
	 * frames of the game that goes on: shown or hidden, the events of the
	 * mouse for it, kept until it is built at the end of the next frame.
	 * The last picture shown again with the bar over it (none: without),
	 * while the board waits. */
	void show_bar(bool on);
	bool bar_shown() const { return m_bar_shown; }
	void bar_event(const SDL_Event &event);
	void show_again(BarState *bar);
	/* The frames shown in the last second (0: not known yet) */
	float fps() const { return m_fps; }

	/* Hikaru::Screen */
	void list_launched() override;
	void before_texture_write(unsigned bank, uint32_t x, uint32_t y, uint32_t w, uint32_t h) override;
	void before_memory_changes() override { finish(); }
	void memory_changed() override;
	bool save_pictures(StateFile &state) override;
	bool load_pictures(StateFile &state) override;

private:
	Display(const Keyboard &keys, const Options &options, SDL_Window *window, SDL_GLContext context);

	void set_window(unsigned times, bool fullscreen);
	void set_window_size(int w, int h, bool fullscreen);
	void prepare_programs(const std::string &title);
	/* Waits for the frame being drawn. */
	void finish();
	/* Has the drawing thread do a job, and waits for it. */
	void run(std::function<void()> job);
	/* On the drawing thread, the bar over the frame when `bar` */
	void draw(bool late, unsigned window_w, unsigned window_h, bool bar);
	/* On the drawing thread: the bezel made a texture (none: forgotten),
	 * then laid over the window */
	void set_bezel(const std::string &path);
	void make_bezel(SDL_Surface *rgba);
	void draw_bezel(unsigned window_w, unsigned window_h);
	void draw_blur(unsigned window_w, unsigned window_h);
	void read_picture(uint8_t *pixels);
	void screenshot();
	void print_fingerprint();
	/* After a frame */
	void show_speed();
	void print_speed();

	Hikaru *m_board;	/* none: the window is black */
	const Keyboard &m_keys;
	SDL_Window *m_window;
	/* The size Odin gave the window last, or took from the user, in
	 * points; whether it is the user's */
	int m_given_w = 0, m_given_h = 0;
	bool m_by_hand = false;
	std::string m_program_list;
	SDL_GLContext m_context;
	std::unique_ptr<Renderer> m_renderer;
	std::unique_ptr<Worker> m_drawing;
	/* Made when it opens first */
	std::unique_ptr<Menu> m_menu;
	bool m_menu_shown;
	bool m_bar_shown = false;
	std::vector<SDL_Event> m_bar_events;
	std::string m_gpu;
	bool m_show_speed;
	Vsync m_vsync;
	/* On the drawing thread: whether a frame on time and a late one wait
	 * for the display, and whether the frames wait now */
	bool m_wait_on_time, m_wait_late, m_waits;

	/* The frame given to the drawing thread: its number, the options it
	 * is drawn with, whether its drawing writes a log, and the picture
	 * the drawing leaves: the framebuffer it can be read from and its
	 * size. */
	unsigned m_frame;
	unsigned m_scale, m_anisotropy;
	bool m_logged;
	GLuint m_picture_fb;
	GLuint m_bezel = 0, m_bezel_program = 0, m_bezel_vao = 0, m_bezel_light = 0;
	/* The sides blurred: the picture made smaller twice */
	std::atomic<Sides> m_sides{ Sides::OFF };
	GLuint m_blur_fb[2] = {}, m_blur_tex[2] = {};
	float m_bezel_w = 0.0f, m_bezel_h = 0.0f;
	PictureHole m_hole;
	unsigned m_picture_width, m_picture_height;
	/* The debug switches as text, for the title */
	std::string m_message;

	/* ODIN_LATE_DRAW: the drawing given last waits for the semaphore */
	bool m_late;
	bool m_held;
	std::binary_semaphore m_late_draw;

	/* What the environment asks for */
	const char *m_shot_prefix;
	unsigned m_shot_every;
	bool m_fingerprint;
	std::vector<uint8_t> m_pixels;
	bool m_speed;
	uint64_t m_speed_since, m_speed_last, m_speed_longest;
	/* ODIN_STATS: the host time of the drawings on their thread, and what
	 * the board waited for them: at the end of a frame, and before it
	 * changed what a drawing reads */
	bool m_stats;
	uint64_t m_draw_ns, m_wait_frame_ns, m_wait_write_ns;
	unsigned m_drawn;
	/* The frames shown in the title, and in the last second */
	uint64_t m_title_since;
	unsigned m_title_frames;
	float m_fps = 0.0f;
};

} /* namespace odin */

#endif /* ODIN_DISPLAY_HPP */
