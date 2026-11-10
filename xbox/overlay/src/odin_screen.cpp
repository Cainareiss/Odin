/*
 * The picture of the board (odin_screen.hpp). GPL-3.0-or-later.
 */

#include "odin_screen.hpp"

#include "odin/host/renderer/hikaru/renderer.hpp"
#include "odin/host/video/display_options.hpp"
#include "odin/machine/boards/hikaru/hikaru.hpp"
#include "odin/machine/core/state_file.hpp"

#include <chrono>

namespace odinc {

/* Set by the core when the machine stops (odin_core.cpp): a wait for the
 * drawing gives up then, since a stopped drawing thread never finishes */
bool stop_requested();

namespace {

bool
never_down(void *, char)
{
	return false;
}

} /* namespace */

OdinScreen::OdinScreen(OdinGraphics &card) : m_card(card)
{
}

/* detach() comes first: the board may be gone by now (it is never touched
 * here). The renderer goes before the card does (the card outlives the
 * screen). */
OdinScreen::~OdinScreen()
{
	m_drawing.wait();
	m_renderer.reset();
}

/* The renderer is made on the drawing thread, where the card is used. The
 * loading picture is published first (the frontend shows it while the
 * programs are made ready). */
bool
OdinScreen::attach(odin::Hikaru &board, const Lists &lists, const std::function<void(unsigned, unsigned)> &progress,
                   std::string &why)
{
	odin::Renderer::Memory memory;
	odin::Renderer::Options looks{};

	memory.fb = board.framebuffer_ram();
	memory.fb_writes = &board.framebuffer_writes();
	memory.texram[0] = board.texture_ram(0);
	memory.texram[1] = board.texture_ram(1);
	looks.key_down = never_down;
	looks.ctx = nullptr;
	looks.programs_seed = lists.seed;
	looks.programs_kept = lists.kept;
	looks.programs_made = lists.made;

	const std::string title = board.game_name();
	bool made = true;

	run([&] {
		try {
			m_renderer = std::make_unique<odin::Renderer>(m_card, memory, looks);
		} catch (...) {
			made = false;
			return;
		}
		m_renderer->set_gpu(&board.gpu());
		m_renderer->set_upscale_mode(odin::DisplayUpscaleMode::RAW);
		m_renderer->set_scale(m_scale);
		const unsigned w = odin::Renderer::WIDTH * m_scale, h = odin::Renderer::HEIGHT * m_scale;

		m_listed = 0;
		m_renderer->draw_loading(title, 0, 1, w, h);
		m_card.show_window();
		m_renderer->prepare_programs([&](unsigned done, unsigned total) {
			m_listed = total;
			if (progress)
				progress(done, total);
		});
		m_renderer->flush_programs();
		m_read_back = m_renderer->programs_read_back();
		m_compiled = m_renderer->programs_compiled();
	});
	if (!made) {
		why = "the renderer could not be made (the graphics card refused it)";
		return false;
	}
	m_board = &board;
	board.set_screen(this);
	return true;
}

void
OdinScreen::detach()
{
	if (m_board) {
		m_board->set_screen(nullptr);
		m_board = nullptr;
	}
	run([this] { m_renderer.reset(); });
}

void
OdinScreen::begin_frame()
{
	if (m_renderer)
		m_renderer->begin_frame();
}

/* The frame before is drawn first (finish); this one is taken on the caller's
 * thread, then drawn on the drawing thread while the board runs the next. The
 * window is the picture: its size is the picture's size, so the picture fills
 * it (1:1 blit, no bars). */
void
OdinScreen::end_frame(unsigned frame)
{
	finish();
	if (!m_renderer)
		return;
	const unsigned w = odin::Renderer::WIDTH * m_scale, h = odin::Renderer::HEIGHT * m_scale;

	m_renderer->take_frame(frame, w, h);
	post([this] {
		m_renderer->draw_frame();
		m_card.show_window();
		m_renderer->frame_shown();
		m_drawn++;
	});
}

void
OdinScreen::flush_programs()
{
	if (m_renderer)
		run([this] { m_renderer->flush_programs(); });
}

void
OdinScreen::loading_frame(const std::string &title, unsigned done, unsigned total)
{
	const unsigned w = odin::Renderer::WIDTH * m_scale, h = odin::Renderer::HEIGHT * m_scale;

	if (!m_renderer)
		return;
	run([this, title, done, total, w, h] {
		m_renderer->draw_loading(title, done, total, w, h);
		m_card.show_window();
	});
}

void
OdinScreen::finish()
{
	while (!m_drawing.wait_for(std::chrono::milliseconds(5))) {
		if (stop_requested())
			return;
	}
}

void
OdinScreen::post(std::function<void()> job)
{
	finish();
	m_drawing.post(std::move(job));
}

void
OdinScreen::run(const std::function<void()> &job)
{
	post(job);
	finish();
}

void
OdinScreen::set_scale(unsigned scale)
{
	if (scale < 1)
		scale = 1;
	if (scale > 3)
		scale = 3;
	m_scale = scale;
	if (m_renderer)
		run([this] { m_renderer->set_scale(m_scale); });
}

void
OdinScreen::list_launched()
{
	if (m_renderer)
		m_renderer->launch();
}

/* The frame being drawn reads the texture RAM as its own frame left it: the
 * decoded textures that read those texels are stale then. */
void
OdinScreen::before_texture_write(unsigned bank, uint32_t x, uint32_t y, uint32_t w, uint32_t h)
{
	finish();
	if (m_renderer)
		m_renderer->invalidate_textures(bank, x, y, w, h);
}

void
OdinScreen::memory_changed()
{
	finish();
	if (m_renderer)
		m_renderer->invalidate_textures(-1);
}

/* The pictures the renderer keeps, one item each, then its four counters. */
bool
OdinScreen::save_pictures(odin::StateFile &state)
{
	std::vector<uint8_t> pictures(odin::Renderer::PICTURES * odin::Renderer::PICTURE_SIZE);
	odin::Renderer::Counters counters = {};

	if (!m_renderer)
		return false;
	run([&] { m_renderer->save(pictures, counters); });
	for (unsigned i = 0; i < odin::Renderer::PICTURES; i++)
		if (!state.put(&pictures[i * odin::Renderer::PICTURE_SIZE],
		               static_cast<uint32_t>(odin::Renderer::PICTURE_SIZE)))
			return false;
	return state.put(counters.num_lists) && state.put(counters.shown) && state.put(counters.next) &&
	       state.put(counters.launched);
}

bool
OdinScreen::load_pictures(odin::StateFile &state)
{
	std::vector<uint8_t> pictures(odin::Renderer::PICTURES * odin::Renderer::PICTURE_SIZE);
	odin::Renderer::Counters counters = {};

	if (!m_renderer)
		return false;
	for (unsigned i = 0; i < odin::Renderer::PICTURES; i++)
		if (!state.get(&pictures[i * odin::Renderer::PICTURE_SIZE], static_cast<uint32_t>(odin::Renderer::PICTURE_SIZE)))
			return false;
	if (!state.get(counters.num_lists) || !state.get(counters.shown) || !state.get(counters.next) ||
	    !state.get(counters.launched))
		return false;
	run([&] { m_renderer->load(pictures, counters); });
	return true;
}

} /* namespace odinc */
