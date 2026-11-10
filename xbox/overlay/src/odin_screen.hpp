/*
 * The picture of the board in odin_core.dll (M3): the screen the board draws
 * through, trimmed from upstream's Display (host/video/display.cpp) to what
 * the frontend needs. No window, no menus, no bezel, no speed or screenshot
 * keys. It keeps upstream's threading: the renderer is made on the drawing
 * thread, a frame is taken on the caller's thread (the board's) and drawn on
 * the drawing thread while the board runs the next one; a draw waits for the
 * one before it. The frame sink (OdinGraphics::show_window) publishes each
 * drawn frame. GPL-3.0-or-later.
 */

#ifndef ODIN_CORE_SCREEN_HPP
#define ODIN_CORE_SCREEN_HPP

#include "odin/machine/boards/hikaru/hikaru_host.hpp"
#include "odin/machine/core/worker.hpp"

#include "odin_graphics_vk.hpp"

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <string_view>

namespace odin {
class Hikaru;
class Renderer;
} /* namespace odin */

namespace odinc {

class OdinScreen final : public odin::HikaruScreen {
public:
	/* The program lists of a game: the seed (shipped, read only), the kept
	 * list (learns what is met) and the folder of the programs made. Empty
	 * for none. */
	struct Lists {
		std::string seed, kept, made;
	};

	/* The card outlives the screen */
	explicit OdinScreen(OdinGraphics &card);
	~OdinScreen() override;
	OdinScreen(const OdinScreen &) = delete;
	OdinScreen &operator=(const OdinScreen &) = delete;

	/* Gives the board its screen: the renderer is made on the drawing thread
	 * and the programs of `lists` are made ready first (`progress`: done of
	 * total, on the caller's thread's behalf). False, with why, if the
	 * renderer cannot be made. */
	bool attach(odin::Hikaru &board, const Lists &lists, const std::function<void(unsigned, unsigned)> &progress,
	            std::string &why);
	/* The board's screen is taken away and the renderer destroyed */
	void detach();

	/* A frame of the board: begin_frame before the board runs it,
	 * end_frame after (with the number of the frame). The frame is drawn on
	 * the drawing thread; the picture is published when it is drawn. */
	void begin_frame();
	void end_frame(unsigned frame);
	/* Waits for the drawing given last (returns early once the emulated
	 * machine is stopped) */
	void finish();
	/* The loading picture of a game: `done` of `total` programs, drawn and
	 * published (the frontend shows it while the programs are made ready) */
	void loading_frame(const std::string &title, unsigned done, unsigned total);
	/* Frames drawn so far by the drawing thread */
	unsigned frames_drawn() const { return m_drawn; }
	/* errata 4 (M3): the pipelines made so far are written to the programs
	 * folder now (the screen does it after each preparation, and on request:
	 * the suspend and the menu of the frontend, through odin_flush_saves) */
	void flush_programs();
	/* The scale of the picture: 1 (640 x 480) to 3 */
	void set_scale(unsigned scale);
	unsigned scale() const { return m_scale; }

	/* The programs of the lists made ready, read and compiled; the counts
	 * are those of the last attach */
	unsigned programs_read_back() const { return m_read_back; }
	unsigned programs_compiled() const { return m_compiled; }
	unsigned programs_listed() const { return m_listed; }

	/* odin::HikaruScreen */
	void list_launched() override;
	void before_texture_write(unsigned bank, uint32_t x, uint32_t y, uint32_t w, uint32_t h) override;
	void before_memory_changes() override { finish(); }
	void memory_changed() override;
	bool save_pictures(odin::StateFile &state) override;
	bool load_pictures(odin::StateFile &state) override;

private:
	/* Runs on the drawing thread, after what was posted before it */
	void run(const std::function<void()> &job);
	void post(std::function<void()> job);

	OdinGraphics &m_card;
	odin::Worker m_drawing;
	std::unique_ptr<odin::Renderer> m_renderer;
	odin::Hikaru *m_board = nullptr;
	unsigned m_scale = 1;
	unsigned m_drawn = 0;
	unsigned m_read_back = 0, m_compiled = 0, m_listed = 0;
};

} /* namespace odinc */

#endif /* ODIN_CORE_SCREEN_HPP */
