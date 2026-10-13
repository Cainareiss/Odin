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

#include <cinttypes>
#include <cstdlib>
#include <filesystem>

#include "odin/env.hpp"
#include "odin/keyboard.hpp"
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

/* The window and its OpenGL 4.3 context, current on the calling thread. */
bool
open_window(SDL_Window *&window, SDL_GLContext &context)
{
	int num_ms_buffers, num_samples;

	if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER)) {
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

	window = SDL_CreateWindow("Odin", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
	                          Renderer::WIDTH, Renderer::HEIGHT,
	                          SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE);
	if (!window) {
		std::fprintf(stderr, "ERROR: could not create SDL window: '%s'\n", SDL_GetError());
		return false;
	}

	context = SDL_GL_CreateContext(window);
	if (!context) {
		std::fprintf(stderr, "ERROR: could not create GL context: '%s'\n", SDL_GetError());
		return false;
	}

	/* The frames are paced by the board, not by the display. */
	SDL_GL_SetSwapInterval(0);

	glewExperimental = GL_TRUE;
	if (glewInit() != GLEW_OK) {
		std::fprintf(stderr, "ERROR: could not initialize glew\n");
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
Display::make(Hikaru &board, const Keyboard &keys, const Options &options)
{
	SDL_Window *window = nullptr;
	SDL_GLContext context = nullptr;

	if (!open_window(window, context))
		return nullptr;
	return std::unique_ptr<Display>(new Display(board, keys, options, window, context));
}

Display::Display(Hikaru &board, const Keyboard &keys, const Options &options, SDL_Window *window,
                 SDL_GLContext context)
	: m_board(board), m_keys(keys), m_window(window), m_context(context),
	  m_frame(0), m_scale(options.scale), m_anisotropy(options.anisotropy), m_logged(false),
	  m_picture_fb(0), m_picture_width(Renderer::WIDTH), m_picture_height(Renderer::HEIGHT),
	  m_late(env_flag("ODIN_LATE_DRAW")), m_held(false), m_late_draw(0),
	  m_shot_prefix(std::getenv("VK_SCREENSHOT")), m_shot_every(60),
	  m_fingerprint(env_flag("ODIN_PICTURE_FP")), m_speed(env_flag("ODIN_SPEED")),
	  m_speed_since(0), m_speed_last(0), m_speed_longest(0), m_title_since(0), m_title_frames(0)
{
	Renderer::Memory memory;
	Renderer::Options looks;

	if (const int every = env_int("VK_SCREENSHOT_EVERY", 0); every > 0)
		m_shot_every = static_cast<unsigned>(every);

	memory.fb = board.framebuffer_ram();
	memory.texram[0] = board.texture_ram(0);
	memory.texram[1] = board.texture_ram(1);

	looks.log = env_flag("HR_LOG");
	looks.log_frame = env_int("HR_LOG_FRAME", 0);
	looks.list_fp = env_flag("ODIN_LIST_FP");
	looks.list_dump = env_int("ODIN_LIST_DUMP", -1);
	looks.lists_test = env_int("ODIN_LISTS_TEST", 0) != 0;
	looks.debug_keys = env_flag("ODIN_DEBUG_KEYS");
	looks.key_down = [](void *ctx, char key) {
		return static_cast<const Keyboard *>(ctx)->down(static_cast<SDL_Keycode>(key));
	};
	looks.ctx = const_cast<Keyboard *>(&m_keys);
	find_program_lists(board.game_name(), looks.programs_seed, looks.programs_kept);

	m_renderer = std::make_unique<Renderer>(memory, looks);
	m_renderer->set_gpu(&board.gpu());

	/* The context goes to the drawing thread for good. */
	SDL_GL_MakeCurrent(m_window, nullptr);
	m_drawing = std::make_unique<Worker>();
	m_drawing->run([this] {
		SDL_GL_MakeCurrent(m_window, m_context);
		SDL_GL_SetSwapInterval(0);
	});
	run([this] { m_renderer->prepare_programs(); });

	if (options.window >= 2 || options.fullscreen)
		set_window(options.window ? options.window : 1, options.fullscreen);

	m_board.set_screen(this);
}

Display::~Display()
{
	m_board.set_screen(nullptr);

	/* The context comes back: the renderer is destroyed with it. */
	run([this] { SDL_GL_MakeCurrent(m_window, nullptr); });
	m_drawing.reset();
	SDL_GL_MakeCurrent(m_window, m_context);
	m_renderer.reset();

	SDL_GL_DeleteContext(m_context);
	SDL_DestroyWindow(m_window);
	SDL_Quit();
}

void
Display::finish()
{
	if (m_held) {
		m_held = false;
		m_late_draw.release();
	}
	m_drawing->wait();
}

void
Display::run(std::function<void()> job)
{
	finish();
	m_drawing->run(std::move(job));
}

void
Display::begin_frame()
{
	m_renderer->begin_frame();
}

/* The frame before is drawn first: this one is taken with the drawing
 * thread at rest, then drawn while the board goes on. */
void
Display::end_frame(unsigned frame)
{
	int w, h;

	finish();
	m_frame = frame;
	SDL_GL_GetDrawableSize(m_window, &w, &h);
	m_renderer->take_frame(frame, static_cast<unsigned>(w), static_cast<unsigned>(h));
	m_logged = m_renderer->frame_is_logged();
	m_message = m_renderer->message();

	m_drawing->post([this] { draw(); });
	m_held = m_late;
	/* A log is written whole before the board writes on. */
	if (m_logged)
		finish();

	show_speed();
	print_speed();
}

void
Display::draw()
{
	if (m_late)
		m_late_draw.acquire();
	m_renderer->set_scale(m_scale);
	m_renderer->set_anisotropy(m_anisotropy);
	m_renderer->draw_frame();
	m_picture_fb = m_renderer->frame_picture(m_picture_width, m_picture_height);

	screenshot();
	print_fingerprint();
	SDL_GL_SwapWindow(m_window);
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
	uint64_t fp = 0xCBF29CE484222325ull;

	if (!m_fingerprint)
		return;

	m_pixels.resize(size_t{m_picture_width} * m_picture_height * 3);
	read_picture(m_pixels.data());
	for (const uint8_t byte : m_pixels) {
		fp ^= byte;
		fp *= 0x100000001B3ull;
	}
	std::fprintf(stderr, "PICFP frame=%u fp=%016" PRIx64 "\n", m_frame, fp);
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
	if (m_title_since && now - m_title_since < second)
		return;

	/* Nothing to tell at the start nor after a pause */
	if (m_title_since && now - m_title_since < 2 * second) {
		const double fps = static_cast<double>(m_title_frames) * static_cast<double>(second) /
		                   static_cast<double>(now - m_title_since);

		if (!m_message.empty())
			std::snprintf(title, sizeof(title), "Odin (%4.1f FPS) [%s]", fps, m_message.c_str());
		else
			std::snprintf(title, sizeof(title), "Odin (%4.1f FPS)", fps);
		SDL_SetWindowTitle(m_window, title);
	}
	m_title_since = now;
	m_title_frames = 0;
}

/* The window `times` the screen of the board: not larger than what the
 * display has room for, borders included, and of the shape of that
 * screen. */
void
Display::set_window(unsigned times, bool fullscreen)
{
	int w = static_cast<int>(Renderer::WIDTH * times), h = static_cast<int>(Renderer::HEIGHT * times);
	int top = 0, left = 0, bottom = 0, right = 0;
	SDL_Rect room;

	finish();

	SDL_GetWindowBordersSize(m_window, &top, &left, &bottom, &right);
	if (!SDL_GetDisplayUsableBounds(SDL_GetWindowDisplayIndex(m_window), &room)) {
		room.w -= left + right;
		room.h -= top + bottom;
		if (w > room.w) {
			h = h * room.w / w;
			w = room.w;
		}
		if (h > room.h) {
			w = w * room.h / h;
			h = room.h;
		}
	}
	SDL_SetWindowSize(m_window, w, h);
	SDL_SetWindowPosition(m_window, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED);
	if (fullscreen)
		SDL_SetWindowFullscreen(m_window, SDL_WINDOW_FULLSCREEN_DESKTOP);
}

void
Display::toggle_fullscreen()
{
	const bool on = (SDL_GetWindowFlags(m_window) & SDL_WINDOW_FULLSCREEN_DESKTOP) != 0;

	finish();
	SDL_SetWindowFullscreen(m_window, on ? 0 : SDL_WINDOW_FULLSCREEN_DESKTOP);
}

void
Display::list_launched()
{
	m_renderer->launch();
}

/* The frame being drawn reads the texture RAM as its own frame left it;
 * the decoded textures of that bank are stale then. */
void
Display::before_texture_write(unsigned bank)
{
	finish();
	m_renderer->invalidate_textures(static_cast<int>(bank));
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
