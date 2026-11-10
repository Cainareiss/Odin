/*
 * The graphics card of odin_core.dll (M3): Odin's Vulkan card made headless
 * (vk_common.cpp Context::make_headless, P03) and the frame sink that publishes
 * the picture of each frame. There is no window and no swap chain: the window
 * image of the card IS the picture (its size is the picture's size, 640 x 480
 * at scale 1). After a frame is drawn into it, the window image is copied to a
 * host-visible staging buffer, the frame's fence is waited for, and the rows
 * are copied (no conversion: the image is B8G8R8A8, the frontend's pixel format
 * 1) into one of two CPU buffers of a mailbox. odin_frame reads the latest one.
 * GPL-3.0-or-later.
 */

#ifndef ODIN_CORE_GRAPHICS_VK_HPP
#define ODIN_CORE_GRAPHICS_VK_HPP

#include "odin/host/gfx/host_graphics.hpp"
#include "odin/host/gfx/vk_common.hpp"

#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace odinc {

/* A published picture: B8G8R8A8 pixels, the top line first, pitch bytes a
 * line. sequence counts the pictures published so far (0: none). The pixels
 * stay valid until the next frame is drawn (see OdinGraphics::latest). */
struct PictureView {
	const uint8_t *pixels = nullptr;
	uint32_t w = 0, h = 0, pitch = 0;
	uint64_t sequence = 0;
};

class OdinGraphics final : public odin::HostGraphics {
public:
	/* The card for the DLL: the one the ICD entry given to set_headless_loader
	 * makes. None: the reason in `why`. */
	static std::unique_ptr<OdinGraphics> make(const std::string &gpu, std::string &why);
	~OdinGraphics() override;
	OdinGraphics(const OdinGraphics &) = delete;
	OdinGraphics &operator=(const OdinGraphics &) = delete;

	/* The frame sink. The window image (as drawn, its size) is copied,
	 * the frame sent and waited for, and the rows published. Called on the
	 * drawing thread once the frame is in the window. */
	void show_window() override;

	/* The latest published picture. Its pixels are those of the last
	 * publish; they are overwritten by the publish after the next one, so
	 * the caller uses them before its next frame is drawn. */
	PictureView latest() const;

	/* Card-level access for the screen (the renderer makes its drawing on
	 * it, the harness reads the card's counters) */
	odin::vk::Context &card() { return *m_card; }

	const std::string &name() const override { return m_card->name(); }
	bool vulkan() const override { return true; }
	const char *api() const override { return "Vulkan"; }
	/* One thread at a time uses the card: the drawing thread (screen.cpp) */
	void take_thread() override {}
	void leave_thread() override {}

	void clear_window(unsigned window_w, unsigned window_h) override;
	bool set_waits(bool) override { return true; }
	void read(odin::HostPicture picture, unsigned w, unsigned h, uint8_t *pixels) override;

	/* No menus, images or bezels in the DLL: the frontend draws those */
	odin::HostImage make_image(const SDL_Surface &, bool) override { return {}; }
	void forget_image(odin::HostImage) override {}
	void draw_bezel(odin::HostImage, unsigned, unsigned) override {}
	void draw_blurred_sides(odin::HostPicture, odin::PictureRect, unsigned, unsigned) override {}
	void draw_white_border(odin::PictureRect, unsigned, unsigned, unsigned) override {}
	void blur_background(odin::HostPicture) override {}
	void blur_window(unsigned, unsigned) override {}
	void draw_background(unsigned, unsigned, unsigned, unsigned, float) override {}
	void start_ui() override {}
	void draw_ui(ImDrawData *, unsigned, unsigned, const char *) override {}
	void stop_ui() override {}
	uint64_t ui_image(odin::HostImage) const override { return 0; }
	void ui_blocks(ImDrawList *, bool) const override {}
	void ui_levels(ImDrawList *, bool) const override {}

	std::unique_ptr<odin::Drawing> make_drawing() override;

private:
	explicit OdinGraphics(std::unique_ptr<odin::vk::Context> card);

	std::unique_ptr<odin::vk::Context> m_card;
	const odin::vk::Functions &m_fn;

	/* The mailbox: two CPU buffers, the front one the latest picture */
	mutable std::mutex m_mutex;
	std::vector<uint8_t> m_buffers[2];
	uint32_t m_w[2] = { 0, 0 }, m_h[2] = { 0, 0 }, m_pitch[2] = { 0, 0 };
	int m_front = -1;
	uint64_t m_sequence = 0;
};

} /* namespace odinc */

#endif /* ODIN_CORE_GRAPHICS_VK_HPP */
