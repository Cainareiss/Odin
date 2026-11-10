/*
 * The graphics card and frame sink of odin_core.dll (odin_graphics_vk.hpp).
 * GPL-3.0-or-later.
 */

#include "odin_graphics_vk.hpp"

#include "odin/host/renderer/hikaru/drawing.hpp"

#include <cstring>

namespace odinc {

using odin::vk::Context;

std::unique_ptr<OdinGraphics>
OdinGraphics::make(const std::string &gpu, std::string &why)
{
	std::unique_ptr<Context> card = Context::make_headless(gpu, why);

	if (!card)
		return nullptr;
	return std::unique_ptr<OdinGraphics>(new OdinGraphics(std::move(card)));
}

OdinGraphics::OdinGraphics(std::unique_ptr<Context> card) : m_card(std::move(card)), m_fn(m_card->fn())
{
}

OdinGraphics::~OdinGraphics() = default;

std::unique_ptr<odin::Drawing>
OdinGraphics::make_drawing()
{
	return odin::Drawing::make_vk(*m_card);
}

void
OdinGraphics::clear_window(unsigned window_w, unsigned window_h)
{
	m_card->clear(m_card->commands(), m_card->window_image(window_w, window_h), { { 0.0f, 0.0f, 0.0f, 1.0f } });
}

/* The frame sink (M3, C). The window image is B8G8R8A8. It is copied to a
 * host-visible staging buffer whose rows are 256-byte aligned (the pitch is
 * the width times 4 rounded up to 256; for the picture sizes of Odin it is
 * the width times 4, already a multiple of 256). One frame in flight: the
 * copy is sent and its fence waited for, then the rows are published, so the
 * next frame may reuse the staging buffer. */
void
OdinGraphics::show_window()
{
	odin::vk::Image &window = m_card->window_image();

	if (!window.image || !window.w || !window.h)
		return;
	const unsigned w = window.w, h = window.h;
	const uint32_t pitch = (w * 4 + 255) & ~255u;
	const size_t bytes = size_t{ pitch } * h;
	const VkCommandBuffer cb = m_card->commands();
	odin::vk::Buffer &out = m_card->staging(bytes);
	const VkBufferImageCopy region{ 0, pitch / 4, 0, { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 }, { 0, 0, 0 },
		                        { w, h, 1 } };

	m_card->barrier(cb, window, odin::vk::COPIED_FROM);
	m_fn.vkCmdCopyImageToBuffer(cb, window.image, window.layout, out.buffer, 1, &region);
	const uint64_t number = m_card->sending();
	m_card->end_frame({});
	m_card->wait_sent(number);

	std::lock_guard<std::mutex> lock(m_mutex);
	const int back = m_front == 0 ? 1 : 0;

	if (m_buffers[back].size() != bytes)
		m_buffers[back].resize(bytes);
	std::memcpy(m_buffers[back].data(), out.bytes, bytes);
	m_w[back] = w;
	m_h[back] = h;
	m_pitch[back] = pitch;
	m_front = back;
	m_sequence++;
}

PictureView
OdinGraphics::latest() const
{
	std::lock_guard<std::mutex> lock(m_mutex);
	PictureView view;

	if (m_front < 0)
		return view;
	view.pixels = m_buffers[m_front].data();
	view.w = m_w[m_front];
	view.h = m_h[m_front];
	view.pitch = m_pitch[m_front];
	view.sequence = m_sequence;
	return view;
}

/* The picture of the card, its lines the bottom one first, as OpenGL has them
 * (the same as the Vulkan card's read, kept for the harness). The window has
 * the top line first; the pixels are given B, G, R. */
void
OdinGraphics::read(odin::HostPicture picture, unsigned w, unsigned h, uint8_t *pixels)
{
	odin::vk::Image *const image = picture.id ? m_card->picture(picture.id) : &m_card->window_image();

	if (!image || !image->image || w > image->w || h > image->h) {
		std::memset(pixels, 0, size_t{ w } * h * 3);
		return;
	}
	const VkCommandBuffer cb = m_card->commands();
	odin::vk::Buffer &out = m_card->staging(VkDeviceSize{ w } * h * 4);
	const VkBufferImageCopy region{ 0, 0, 0, { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 }, { 0, 0, 0 }, { w, h, 1 } };

	m_card->barrier(cb, *image, odin::vk::COPIED_FROM);
	m_fn.vkCmdCopyImageToBuffer(cb, image->image, image->layout, out.buffer, 1, &region);
	m_card->send_and_wait();

	for (unsigned y = 0; y < h; y++) {
		const uint8_t *from = out.bytes + size_t{ picture.id ? y : h - 1 - y } * w * 4;

		for (unsigned x = 0; x < w; x++, from += 4, pixels += 3)
			std::memcpy(pixels, from, 3);
	}
}

} /* namespace odinc */
