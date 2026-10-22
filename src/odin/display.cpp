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

#include "odin/display.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cinttypes>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <system_error>

#include "odin/bytes.hpp"
#include "odin/env.hpp"
#include "odin/keyboard.hpp"
#include "odin/logo.hpp"
#include "odin/menu.hpp"
#include "odin/renderer.hpp"
#include "odin/state_file.hpp"
#include "odin/worker.hpp"

namespace odin {

namespace {

/* The lists of the mesh programs of a game (MeshPrograms): the one that
 * comes with Odin, in shaders/ where the list of the games is, and the one
 * kept for the user, in the cache of their home. A path is left empty when
 * there is no such file or no place for it. */
void
find_program_lists(const std::string &game, std::string &seed, std::string &kept)
{
	const std::string home = home_dir();
	const char *const cache = std::getenv("XDG_CACHE_HOME");
	std::error_code error;
	std::string dir;

	kept.clear();
	seed = "shaders/" + game + ".txt";
	if (!std::filesystem::exists(seed, error) && !home.empty())
		seed = home + "/.local/share/odin/shaders/" + game + ".txt";
	if (!std::filesystem::exists(seed, error))
		seed.clear();

	if (cache && cache[0])
		dir = cache;
	else if (!home.empty())
		dir = home + "/.cache";
	else
		return;
	dir += "/odin/shaders";
	std::filesystem::create_directories(dir, error);
	kept = dir + "/" + game + ".txt";
}

/* The logo in the bar of the window and in the task bar: the rune in its
 * gold, its lines bolder than the menu draws them, nearly as high as the
 * picture; each pixel as opaque as the lines cover it */
void
set_icon(SDL_Window *window)
{
	constexpr int SIDE = 64;
	constexpr float STROKE = 3.0f, HEIGHT = 0.92f;
	/* The stem is 20 of the square of 24 high; the middle of the square
	 * in the middle of the picture */
	constexpr float K = SIDE * HEIGHT / 20.0f, FROM = SIDE / 2.0f - 12.0f * K, RADIUS = STROKE * K / 2.0f;
	SDL_Surface *const icon = SDL_CreateSurface(SIDE, SIDE, SDL_PIXELFORMAT_RGBA32);

	if (!icon)
		return;
	for (int y = 0; y < SIDE; y++) {
		auto *const row = static_cast<uint8_t *>(icon->pixels) + static_cast<size_t>(y) * static_cast<size_t>(icon->pitch);

		for (int x = 0; x < SIDE; x++) {
			const float px = static_cast<float>(x) + 0.5f, py = static_cast<float>(y) + 0.5f;
			float nearest = SIDE;

			for (const auto &[x1, y1, x2, y2] : RUNE_LINES) {
				const float ax = FROM + x1 * K, ay = FROM + y1 * K, dx = (x2 - x1) * K, dy = (y2 - y1) * K;
				const float t = std::clamp(((px - ax) * dx + (py - ay) * dy) / (dx * dx + dy * dy), 0.0f, 1.0f);

				nearest = std::min(nearest, std::hypot(px - ax - t * dx, py - ay - t * dy));
			}
			for (int c = 0; c < 3; c++)
				row[x * 4 + c] = RUNE_GOLD[c];
			row[x * 4 + 3] = static_cast<uint8_t>(std::lround(255.0f * std::clamp(RADIUS - nearest + 0.5f, 0.0f, 1.0f)));
		}
	}
	if (!SDL_SetWindowIcon(window, icon))
		std::fprintf(stderr, "ERROR: no icon for the window: '%s'\n", SDL_GetError());
	SDL_DestroySurface(icon);
}

/* The window and its OpenGL 4.3 context, current on the calling thread. */
bool
open_window(SDL_Window *&window, SDL_GLContext &context)
{
	int num_ms_buffers, num_samples;

	if (!SDL_Init(SDL_INIT_VIDEO)) {
		std::fprintf(stderr, "ERROR: could not initialize SDL: '%s'\n", SDL_GetError());
		return false;
	}

	SDL_GL_SetAttribute(SDL_GL_RED_SIZE, 8);
	SDL_GL_SetAttribute(SDL_GL_GREEN_SIZE, 8);
	SDL_GL_SetAttribute(SDL_GL_BLUE_SIZE, 8);
	SDL_GL_SetAttribute(SDL_GL_ALPHA_SIZE, 8);
	SDL_GL_SetAttribute(SDL_GL_BUFFER_SIZE, 0);
	SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
	SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
	SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 0);
	SDL_GL_SetAttribute(SDL_GL_ACCUM_RED_SIZE, 0);
	SDL_GL_SetAttribute(SDL_GL_ACCUM_GREEN_SIZE, 0);
	SDL_GL_SetAttribute(SDL_GL_ACCUM_BLUE_SIZE, 0);
	SDL_GL_SetAttribute(SDL_GL_ACCUM_ALPHA_SIZE, 0);
	SDL_GL_SetAttribute(SDL_GL_STEREO, 0);
	SDL_GL_SetAttribute(SDL_GL_MULTISAMPLEBUFFERS, 0);
	SDL_GL_SetAttribute(SDL_GL_MULTISAMPLESAMPLES, 0);
	SDL_GL_SetAttribute(SDL_GL_ACCELERATED_VISUAL, 1);
	SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 4);
	SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
	SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, SDL_GL_CONTEXT_FORWARD_COMPATIBLE_FLAG);
	SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);

	window = SDL_CreateWindow("Odin", Renderer::WIDTH, Renderer::HEIGHT, SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE);
	if (!window) {
		std::fprintf(stderr, "ERROR: could not create SDL window: '%s'\n", SDL_GetError());
		return false;
	}
	/* Never smaller than the screen of the board: the menu is made for it */
	SDL_SetWindowMinimumSize(window, Renderer::WIDTH, Renderer::HEIGHT);
	set_icon(window);
	SDL_SetWindowPosition(window, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED);

	context = SDL_GL_CreateContext(window);
	if (!context) {
		std::fprintf(stderr, "ERROR: could not create GL context: '%s'\n", SDL_GetError());
		return false;
	}

	/* The frames are paced by the board alone until set_vsync. */
	SDL_GL_SetSwapInterval(0);

	/* Under Wayland the context is EGL's: GLEW has the functions of OpenGL
	 * then, only GLX is missing. */
	glewExperimental = GL_TRUE;
	if (const GLenum glew = glewInit(); glew != GLEW_OK && glew != GLEW_ERROR_NO_GLX_DISPLAY) {
		std::fprintf(stderr, "ERROR: could not initialize glew (%u)\n", glew);
		return false;
	}

	glDisable(GL_MULTISAMPLE);

	SDL_GL_GetAttribute(SDL_GL_MULTISAMPLEBUFFERS, &num_ms_buffers);
	SDL_GL_GetAttribute(SDL_GL_MULTISAMPLESAMPLES, &num_samples);

	std::printf("renderer: GL vendor    = %s\n", glGetString(GL_VENDOR));
	std::printf("renderer: GL renderer  = %s\n", glGetString(GL_RENDERER));
	std::printf("renderer: GL version   = %s\n", glGetString(GL_VERSION));
	std::printf("renderer: GLSL version = %s\n", glGetString(GL_SHADING_LANGUAGE_VERSION));
	std::printf("renderer: %d samples on %d ms buffers\n", num_samples, num_ms_buffers);

	glViewport(0, 0, Renderer::WIDTH, Renderer::HEIGHT);

	/* What SDL and GLEW left: harmless, and in the way of the errors
	 * looked for later. */
	while (glGetError() != GL_NO_ERROR)
		;
	return true;
}

} /* namespace */

std::unique_ptr<Display>
Display::make(const Keyboard &keys, const Options &options)
{
	SDL_Window *window = nullptr;
	SDL_GLContext context = nullptr;

	if (!open_window(window, context))
		return nullptr;
	return std::unique_ptr<Display>(new Display(keys, options, window, context));
}

Display::Display(const Keyboard &keys, const Options &options, SDL_Window *window, SDL_GLContext context)
	: m_board(nullptr), m_keys(keys), m_window(window), m_context(context), m_menu_shown(false),
	  m_show_speed(options.show_speed), m_vsync(options.vsync),
	  m_wait_on_time(false), m_wait_late(false), m_waits(false),
	  m_frame(0), m_scale(options.scale), m_anisotropy(options.anisotropy), m_logged(false),
	  m_picture_fb(0), m_picture_width(Renderer::WIDTH), m_picture_height(Renderer::HEIGHT),
	  m_late(env_flag("ODIN_LATE_DRAW")), m_held(false), m_late_draw(0),
	  m_shot_prefix(std::getenv("ODIN_SCREENSHOT")), m_shot_every(60),
	  m_fingerprint(env_flag("ODIN_PICTURE_FP")), m_speed(env_flag("ODIN_SPEED")),
	  m_speed_since(0), m_speed_last(0), m_speed_longest(0),
	  m_stats(env_flag("ODIN_STATS")), m_draw_ns(0), m_wait_frame_ns(0), m_wait_write_ns(0), m_drawn(0),
	  m_title_since(0), m_title_frames(0)
{
	m_gpu = reinterpret_cast<const char *>(glGetString(GL_RENDERER));
	m_given_w = static_cast<int>(Renderer::WIDTH);
	m_given_h = static_cast<int>(Renderer::HEIGHT);
	if (const int every = env_int("ODIN_SCREENSHOT_EVERY", 0); every > 0)
		m_shot_every = static_cast<unsigned>(every);

	/* The context goes to the drawing thread for good. */
	SDL_GL_MakeCurrent(m_window, nullptr);
	m_drawing = std::make_unique<Worker>();
	m_drawing->run([this] {
		SDL_GL_MakeCurrent(m_window, m_context);
		SDL_GL_SetSwapInterval(0);
	});
	if (options.window_w && options.window_h)
		set_window_size(static_cast<int>(options.window_w), static_cast<int>(options.window_h), options.fullscreen);
	else if (options.window >= 2 || options.fullscreen)
		set_window(options.window ? options.window : 1, options.fullscreen);
	show_black();
}

/* The renderer is made on the drawing thread, where the context is; the
 * loading screen does not wait for the display. */
void
Display::attach(Hikaru &board, const std::string &title)
{
	Renderer::Memory memory;
	Renderer::Options looks;

	memory.fb = board.framebuffer_ram();
	memory.texram[0] = board.texture_ram(0);
	memory.texram[1] = board.texture_ram(1);

	looks.log = env_flag("HR_LOG");
	looks.log_frame = env_int("HR_LOG_FRAME", 0);
	looks.list_fp = env_flag("ODIN_LIST_FP");
	looks.list_dump = env_int("ODIN_LIST_DUMP", -1);
	looks.lists_test = env_int("ODIN_LISTS_TEST", 0) != 0;
	looks.times = env_flag("ODIN_DRAW_TIMES");
	looks.debug_keys = env_flag("ODIN_DEBUG_KEYS");
	looks.key_down = [](void *ctx, char key) {
		return static_cast<const Keyboard *>(ctx)->down(static_cast<SDL_Keycode>(key));
	};
	looks.ctx = const_cast<Keyboard *>(&m_keys);
	find_program_lists(board.game_name(), looks.programs_seed, looks.programs_kept);
	m_program_list = looks.programs_kept;

	run([&] {
		m_renderer = std::make_unique<Renderer>(memory, looks);
		m_renderer->set_gpu(&board.gpu());
		SDL_GL_SetSwapInterval(0);
		m_waits = false;
	});
	m_board = &board;
	m_frame = 0;
	m_picture_fb = 0;
	m_picture_width = Renderer::WIDTH;
	m_picture_height = Renderer::HEIGHT;
	m_speed_since = m_speed_last = m_speed_longest = 0;
	m_title_since = 0;
	m_fps = 0.0f;
	prepare_programs(title);
	set_vsync(m_vsync);
	board.set_screen(this);
}

void
Display::detach()
{
	if (!m_board)
		return;
	m_board->set_screen(nullptr);
	run([this] { m_renderer.reset(); });
	m_board = nullptr;
	m_picture_fb = 0;
	m_program_list.clear();
	SDL_SetWindowTitle(m_window, "Odin");
	show_black();
}

void
Display::show_black()
{
	int w, h;

	if (m_board)
		return;
	SDL_GetWindowSizeInPixels(m_window, &w, &h);
	run([this, w, h] {
		glBindFramebuffer(GL_FRAMEBUFFER, 0);
		glViewport(0, 0, w, h);
		glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
		glClear(GL_COLOR_BUFFER_BIT);
		SDL_GL_SwapWindow(m_window);
	});
}

/* The programs of the lists of the game are compiled before its first
 * frame, on the drawing thread, with the loading screen drawn at most 60
 * times a second. The window answers meanwhile; its events wait in their
 * queue for the game. */
void
Display::prepare_programs(const std::string &title)
{
	constexpr uint64_t EVERY_NS = 1000000000 / 60;
	const auto window_size = [this] {
		int w, h;

		SDL_GetWindowSizeInPixels(m_window, &w, &h);
		return static_cast<uint64_t>(static_cast<uint32_t>(w)) << 32 | static_cast<uint32_t>(h);
	};
	std::atomic<uint64_t> size(window_size());
	uint64_t shown = 0;

	finish();
	m_drawing->post([&] {
		m_renderer->prepare_programs([&](unsigned done, unsigned total) {
			const uint64_t now = host_ns();

			if (done && done < total && now - shown < EVERY_NS)
				return;
			shown = now;
			const uint64_t wh = size;
			m_renderer->draw_loading(title, done, total, static_cast<unsigned>(wh >> 32),
			                         static_cast<unsigned>(wh));
			SDL_GL_SwapWindow(m_window);
		});
	});
	while (!m_drawing->wait_for(std::chrono::milliseconds(16))) {
		SDL_PumpEvents();
		size = window_size();
	}
}

Display::~Display()
{
	if (m_board)
		m_board->set_screen(nullptr);

	/* The context comes back: the renderer is destroyed with it. */
	run([this] { SDL_GL_MakeCurrent(m_window, nullptr); });
	m_drawing.reset();
	SDL_GL_MakeCurrent(m_window, m_context);
	if (m_menu) {
		m_menu->release();
		m_menu.reset();
	}
	m_renderer.reset();

	if (m_stats)
		std::fprintf(stderr, "screen: %u frames drawn in %.3f s on their thread; the board waited for them "
		             "%.3f s at the end of its frames and %.3f s before it wrote what they read\n", m_drawn,
		             static_cast<double>(m_draw_ns) / 1e9, static_cast<double>(m_wait_frame_ns) / 1e9,
		             static_cast<double>(m_wait_write_ns) / 1e9);

	SDL_GL_DestroyContext(m_context);
	SDL_DestroyWindow(m_window);
	SDL_Quit();
}

void
Display::finish()
{
	const uint64_t begin = m_stats ? host_ns() : 0;

	if (m_held) {
		m_held = false;
		m_late_draw.release();
	}
	m_drawing->wait();
	if (m_stats)
		m_wait_write_ns += host_ns() - begin;
}

void
Display::run(std::function<void()> job)
{
	finish();
	m_drawing->run(std::move(job));
}

void
Display::open_menu(int page)
{
	if (!m_menu)
		m_menu = std::make_unique<Menu>(m_window);
	/* The menus of the bar closed, that of the library too */
	show_bar(false);
	m_menu->hide_bar();
	m_menu->open(page);
	/* Behind it, the last picture of the game, or the library as it was
	 * drawn last */
	if (m_board) {
		run([this] { m_menu->freeze(m_picture_fb, m_picture_width, m_picture_height); });
	} else {
		int w, h;

		SDL_GetWindowSizeInPixels(m_window, &w, &h);
		run([this, w, h] { m_menu->freeze_window(static_cast<unsigned>(w), static_cast<unsigned>(h)); });
	}
	m_menu_shown = true;
}

void
Display::close_menu()
{
	m_menu->close();
	m_menu_shown = false;
}

void
Display::menu_event(const SDL_Event &event)
{
	if (m_menu)
		m_menu->event(event);
}

bool
Display::menu_popup_open() const
{
	return m_menu && m_menu->popup_open();
}

bool
Display::menu_typing() const
{
	return m_menu && m_menu->typing();
}

/* The menu made as the library makes it, its renderer first; the bar takes
 * the pointer and the hints of the mouse as the menu does */
void
Display::show_bar(bool on)
{
	if (on == m_bar_shown)
		return;
	if (on && !m_menu) {
		m_menu = std::make_unique<Menu>(m_window);
		run([this] { m_menu->freeze(0, 0, 0); });
	}
	m_bar_shown = on;
	m_bar_events.clear();
	if (on) {
		m_menu->forget_input();
		m_menu->open();
	} else {
		m_menu->hide_bar();
		m_menu->close();
	}
}

void
Display::bar_event(const SDL_Event &event)
{
	if (m_bar_shown)
		m_bar_events.push_back(event);
}

/* The bar given the events of the mouse kept for it and built, when it is
 * shown: whether it is */
bool
Display::build_bar(BarState *bar)
{
	if (!bar || !m_bar_shown)
		return false;
	for (const SDL_Event &event : m_bar_events)
		m_menu->event(event);
	m_bar_events.clear();
	m_menu->build_bar(*bar);
	return true;
}

/* Over the picture of the game: the bezel, the game blurred at its sides,
 * the bar */
void
Display::draw_over(unsigned w, unsigned h, bool bar)
{
	draw_bezel(w, h);
	draw_blur(w, h);
	if (bar)
		m_menu->draw_bar(w, h);
}

void
Display::show_again(BarState *bar)
{
	int w, h;

	if (!m_renderer)
		return;
	finish();

	const bool over = build_bar(bar);

	SDL_GetWindowSizeInPixels(m_window, &w, &h);
	m_drawing->run([this, w, h, over] {
		const auto ww = static_cast<unsigned>(w), wh = static_cast<unsigned>(h);

		m_renderer->show_again(ww, wh, m_hole);
		draw_over(ww, wh, over);
		SDL_GL_SwapWindow(m_window);
	});
}

/* Built on this thread, drawn on the drawing thread meanwhile */
void
Display::open_library()
{
	/* Its renderer first: Dear ImGui builds on it */
	if (!m_menu) {
		m_menu = std::make_unique<Menu>(m_window);
		run([this] { m_menu->freeze(0, 0, 0); });
	}
	m_menu->forget_input();
}

void
Display::add_library_picture(const std::string &game, SDL_Surface *rgba)
{
	m_menu->add_library_picture(game, rgba);
}

bool
Display::save_picture(const std::string &path)
{
	const int w = static_cast<int>(m_picture_width), h = static_cast<int>(m_picture_height);
	SDL_Surface *const shot = w && h ? SDL_CreateSurface(w, h, SDL_PIXELFORMAT_RGB24) : nullptr;
	std::vector<uint8_t> pixels(size_t{m_picture_width} * m_picture_height * 3);
	std::error_code error;
	bool saved = false;

	if (!shot)
		return false;
	run([this, &pixels] { read_picture(pixels.data()); });
	/* read_picture gives the bottom line first */
	for (int y = 0; y < h; y++)
		std::memcpy(static_cast<uint8_t *>(shot->pixels) + static_cast<size_t>(y) * static_cast<size_t>(shot->pitch),
		            &pixels[static_cast<size_t>(h - 1 - y) * static_cast<size_t>(w) * 3], static_cast<size_t>(w) * 3);

	SDL_Surface *const small = w > 640 ? SDL_ScaleSurface(shot, 640, 480, SDL_SCALEMODE_LINEAR) : shot;

	std::filesystem::create_directories(std::filesystem::path(path).parent_path(), error);
	saved = small && SDL_SavePNG(small, path.c_str());
	if (!saved)
		std::fprintf(stderr, "ERROR: picture: '%s' cannot be written: %s\n", path.c_str(), SDL_GetError());
	if (small != shot)
		SDL_DestroySurface(small);
	SDL_DestroySurface(shot);
	return saved;
}

void
Display::draw_library(LibraryState &state, BarState &bar)
{
	int w, h;

	m_menu->build_library(state, bar);
	SDL_GetWindowSizeInPixels(m_window, &w, &h);
	run([this, w, h] {
		/* No picture of a game behind it */
		m_menu->freeze(0, 0, 0);
		m_menu->draw(static_cast<unsigned>(w), static_cast<unsigned>(h));
		SDL_GL_SwapWindow(m_window);
	});
}

void
Display::draw_menu(MenuState &state)
{
	int w, h;

	m_menu->build(state);
	SDL_GetWindowSizeInPixels(m_window, &w, &h);
	run([this, w, h] {
		m_menu->draw(static_cast<unsigned>(w), static_cast<unsigned>(h));
		SDL_GL_SwapWindow(m_window);
	});
}

void
Display::begin_frame()
{
	m_renderer->begin_frame();
}

/* The frame before is drawn first: this one is taken with the drawing
 * thread at rest, then drawn while the board goes on. */
void
Display::end_frame(unsigned frame, bool late, BarState *bar)
{
	const uint64_t waited = m_wait_write_ns;
	int w, h;

	finish();
	/* That wait was for the end of the frame. */
	m_wait_frame_ns += m_wait_write_ns - waited;
	m_wait_write_ns = waited;
	m_frame = frame;
	SDL_GetWindowSizeInPixels(m_window, &w, &h);
	m_renderer->take_frame(frame, static_cast<unsigned>(w), static_cast<unsigned>(h), m_hole);
	m_logged = m_renderer->frame_is_logged();
	m_message = m_renderer->message();
	/* Built while the drawing thread rests, drawn over the frame */
	const bool over = build_bar(bar);

	m_drawing->post([this, late, w, h, over] { draw(late, static_cast<unsigned>(w), static_cast<unsigned>(h), over); });
	m_held = m_late;
	/* A log is written whole before the board writes on. */
	if (m_logged)
		finish();

	show_speed();
	print_speed();
}

void
Display::draw(bool late, unsigned window_w, unsigned window_h, bool bar)
{
	if (m_late)
		m_late_draw.acquire();

	const uint64_t begin = m_stats ? host_ns() : 0;

	m_renderer->set_scale(m_scale);
	m_renderer->set_anisotropy(m_anisotropy);
	m_renderer->draw_frame();
	m_picture_fb = m_renderer->frame_picture(m_picture_width, m_picture_height);
	draw_over(window_w, window_h, bar);

	screenshot();
	print_fingerprint();
	if (const bool wait = late ? m_wait_late : m_wait_on_time; wait != m_waits) {
		SDL_GL_SetSwapInterval(wait ? 1 : 0);
		m_waits = wait;
	}
	SDL_GL_SwapWindow(m_window);
	m_renderer->frame_shown();
	if (m_stats) {
		m_draw_ns += host_ns() - begin;
		m_drawn++;
	}
}

/* The picture of the frame as bytes of red, green and blue, the bottom line
 * first. */
void
Display::read_picture(uint8_t *pixels)
{
	glBindFramebuffer(GL_READ_FRAMEBUFFER, m_picture_fb);
	glReadBuffer(m_picture_fb ? GL_COLOR_ATTACHMENT0 : GL_BACK);
	glPixelStorei(GL_PACK_ALIGNMENT, 1);
	glReadPixels(0, 0, static_cast<GLsizei>(m_picture_width), static_cast<GLsizei>(m_picture_height),
	             GL_RGB, GL_UNSIGNED_BYTE, pixels);
	glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);
}

void
Display::screenshot()
{
	const size_t w = m_picture_width, h = m_picture_height;
	char path[512];

	if (!m_shot_prefix || (m_frame % m_shot_every) != 0)
		return;

	std::vector<uint8_t> pixels(w * h * 3);

	read_picture(pixels.data());
	std::snprintf(path, sizeof(path), "%s-%06u.ppm", m_shot_prefix, m_frame);
	std::FILE *const file = std::fopen(path, "wb");
	if (!file)
		return;
	std::fprintf(file, "P6\n%zu %zu\n255\n", w, h);
	for (size_t y = 0; y < h; y++)
		std::fwrite(&pixels[(h - 1 - y) * w * 3], 1, w * 3, file);
	std::fclose(file);
}

void
Display::print_fingerprint()
{
	if (!m_fingerprint)
		return;

	m_pixels.resize(size_t{m_picture_width} * m_picture_height * 3);
	read_picture(m_pixels.data());
	std::fprintf(stderr, "PICFP frame=%u fp=%016" PRIx64 "\n", m_frame, fnv1a(FNV_START, m_pixels.data(), m_pixels.size()));
}

void
Display::print_speed()
{
	const double second = static_cast<double>(SDL_GetPerformanceFrequency());

	if (!m_speed)
		return;

	const uint64_t now = SDL_GetPerformanceCounter();

	if (m_speed_last && now - m_speed_last > m_speed_longest)
		m_speed_longest = now - m_speed_last;
	m_speed_last = now;
	if ((m_frame % 60) != 0)
		return;

	if (m_speed_since)
		std::fprintf(stderr, "SPEED frame=%u fps=%.1f longest=%.1f\n", m_frame,
		             60.0 * second / static_cast<double>(now - m_speed_since),
		             1000.0 * static_cast<double>(m_speed_longest) / second);
	m_speed_since = now;
	m_speed_longest = 0;
}

/* The title of the window tells the frames shown in the last second: the
 * time of one frame, in milliseconds, reads 59 or 62 for 60. */
void
Display::show_speed()
{
	const uint64_t second = SDL_GetPerformanceFrequency();
	const uint64_t now = SDL_GetPerformanceCounter();
	char title[512];

	m_title_frames++;
	if (!m_show_speed || (m_title_since && now - m_title_since < second))
		return;

	/* Nothing to tell at the start nor after a pause */
	if (m_title_since && now - m_title_since < 2 * second) {
		const double fps = static_cast<double>(m_title_frames) * static_cast<double>(second) /
		                   static_cast<double>(now - m_title_since);

		m_fps = static_cast<float>(fps);
		if (!m_message.empty())
			std::snprintf(title, sizeof(title), "Odin (%4.1f FPS) [%s]", fps, m_message.c_str());
		else
			std::snprintf(title, sizeof(title), "Odin (%4.1f FPS)", fps);
		SDL_SetWindowTitle(m_window, title);
	}
	m_title_since = now;
	m_title_frames = 0;
}

void
Display::set_scale(unsigned scale)
{
	finish();
	m_scale = scale;
}

void
Display::set_anisotropy(unsigned level)
{
	finish();
	m_anisotropy = level;
}

void
Display::set_show_speed(bool on)
{
	m_show_speed = on;
	m_title_since = 0;
	if (!on)
		SDL_SetWindowTitle(m_window, "Odin");
}

void
Display::list_launched()
{
	m_renderer->launch();
}

/* The frame being drawn reads the texture RAM as its own frame left it;
 * the decoded textures that read those texels are stale then. */
void
Display::before_texture_write(unsigned bank, uint32_t x, uint32_t y, uint32_t w, uint32_t h)
{
	finish();
	m_renderer->invalidate_textures(bank, x, y, w, h);
}

void
Display::memory_changed()
{
	finish();
	m_renderer->invalidate_textures(-1);
}

/* The pictures the renderer keeps, one item each, then its four counters.
 * They are read and written on the drawing thread. */
bool
Display::save_pictures(StateFile &state)
{
	std::vector<uint8_t> pictures(Renderer::PICTURES * Renderer::PICTURE_SIZE);
	Renderer::Counters counters = {};

	run([&] { m_renderer->save(pictures, counters); });
	for (unsigned i = 0; i < Renderer::PICTURES; i++)
		if (!state.put(&pictures[i * Renderer::PICTURE_SIZE], static_cast<uint32_t>(Renderer::PICTURE_SIZE)))
			return false;
	return state.put(counters.num_lists) && state.put(counters.shown) &&
	       state.put(counters.next) && state.put(counters.launched);
}

bool
Display::load_pictures(StateFile &state)
{
	std::vector<uint8_t> pictures(Renderer::PICTURES * Renderer::PICTURE_SIZE);
	Renderer::Counters counters = {};

	for (unsigned i = 0; i < Renderer::PICTURES; i++)
		if (!state.get(&pictures[i * Renderer::PICTURE_SIZE], static_cast<uint32_t>(Renderer::PICTURE_SIZE)))
			return false;
	if (!state.get(counters.num_lists) || !state.get(counters.shown) ||
	    !state.get(counters.next) || !state.get(counters.launched))
		return false;

	run([&] { m_renderer->load(pictures, counters); });
	return true;
}

} /* namespace odin */
