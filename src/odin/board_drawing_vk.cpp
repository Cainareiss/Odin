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

/* The board drawing with Vulkan (board_drawing.hpp), its first step
 * (MARKDOWN/106 § 9): the loading screen, and a picture of the frame that
 * stays black. The textures, the layers, the scene and its translucent
 * fragments come with the next steps (§ 5). */

#include "odin/board_drawing.hpp"

#include <algorithm>
#include <cstring>

#include "odin/vk_common.hpp"

namespace odin {

namespace {

/* The number of the picture of the frame, as the window reads it */
constexpr uint32_t FRAME_PICTURE = 1;

class VkBoardDrawing : public BoardDrawing {
public:
	explicit VkBoardDrawing(vk::Context &card) : m_card(card) { build_frame(); }
	~VkBoardDrawing() override;
	VkBoardDrawing(const VkBoardDrawing &) = delete;
	VkBoardDrawing &operator=(const VkBoardDrawing &) = delete;

	void set_scale(unsigned scale) override;

	void begin_scene(const odin_display_list_t &, const SceneDebug &) override {}
	void clear_depth() override {}
	void begin_pass(const PassRules &) override {}
	void draw_mesh(const odin_mesh_t &, const MeshDraw &) override {}
	void end_pass() override {}
	void end_scene() override {}

	void begin_translucent() override {}
	void begin_collect(unsigned) override {}
	void end_collect() override {}
	void keep_count(unsigned, unsigned) override {}
	uint32_t count_now(unsigned) override { return 0; }
	void end_translucent(bool) override {}
	uint32_t counted_fragments(unsigned, uint32_t) override { return 0; }
	void forget_counts(unsigned) override {}
	void size_fragments(uint32_t) override {}
	uint32_t fragment_limit() const override { return UINT32_MAX / 32; }

	void keep_programs_in(const std::string &) override {}
	void list_programs_in(const std::string &) override {}
	bool holds_program(uint64_t) const override { return false; }
	void begin_preparing() override {}
	void prepare_program(uint64_t) override {}
	void end_preparing() override {}
	unsigned programs_read_back() const override { return 0; }
	unsigned programs_compiled() const override { return 0; }

	bool has_stamps() const override { return false; }
	uint32_t put_stamp() override { return 0; }
	bool reached(uint32_t) override { return true; }
	uint64_t stamp_time(uint32_t) override { return 0; }
	void forget_stamp(uint32_t, bool) override {}
	void send_commands() override {}

	void scene_to_pipe(unsigned) override {}
	void blend_scene_to_pipe(unsigned, unsigned, uint32_t) override {}
	void keep_picture(unsigned, unsigned) override {}

	void set_layer(unsigned, LayerTexels, const void *) override {}
	void compose(const Compose &, unsigned, const bool[2]) override {}
	void present(PictureRect rect, unsigned window_w, unsigned window_h) override;
	HostPicture frame_picture() const override { return { FRAME_PICTURE, m_frame.w, m_frame.h }; }
	void set_filter(const std::string &) override {}
	void draw_loading(std::span<const LoadingRect> rects, unsigned window_w, unsigned window_h) override;

	void read_kept(std::span<uint8_t> pictures) override { std::fill(pictures.begin(), pictures.end(), 0); }
	void write_kept(std::span<const uint8_t>) override {}

	HostTexture make_texture(Texels, std::span<const TexelRect>) override { return { m_next_texture++ }; }
	void forget_textures(std::span<const HostTexture>) override {}
	HostSampler make_sampler(const TextureSampling &, unsigned) override { return { m_next_sampler++ }; }
	void forget_sampler(HostSampler) override {}

private:
	void build_frame();
	void clear_window(unsigned window_w, unsigned window_h);

	vk::Context &m_card;
	unsigned m_scale = 1;
	/* The picture of the frame: black */
	vk::Image m_frame;
	uint32_t m_next_texture = 1, m_next_sampler = 1;
};

VkBoardDrawing::~VkBoardDrawing()
{
	m_card.send_and_wait();
	m_card.forget_picture(FRAME_PICTURE);
	m_card.destroy_image(m_frame);
}

void
VkBoardDrawing::build_frame()
{
	const VkCommandBuffer cb = m_card.commands();
	const VkClearColorValue black{ { 0.0f, 0.0f, 0.0f, 1.0f } };
	const VkImageSubresourceRange all{ VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };

	m_frame = m_card.make_image(VK_FORMAT_R8G8B8A8_UNORM, SCREEN_WIDTH * m_scale, SCREEN_HEIGHT * m_scale, 1,
	                            VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT |
	                                    VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT);
	m_card.barrier(cb, m_frame, vk::COPIED_TO);
	m_card.fn().vkCmdClearColorImage(cb, m_frame.image, m_frame.layout, &black, 1, &all);
	m_card.name_picture(FRAME_PICTURE, &m_frame);
}

void
VkBoardDrawing::set_scale(unsigned scale)
{
	if (scale == m_scale)
		return;
	vk::Image old = m_frame;

	m_card.later([card = &m_card, old]() mutable { card->destroy_image(old); });
	m_scale = scale;
	build_frame();
}

void
VkBoardDrawing::clear_window(unsigned window_w, unsigned window_h)
{
	const VkCommandBuffer cb = m_card.commands();
	vk::Image &window = m_card.window_image(window_w, window_h);
	const VkClearColorValue black{ { 0.0f, 0.0f, 0.0f, 1.0f } };
	const VkImageSubresourceRange all{ VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };

	m_card.barrier(cb, window, vk::COPIED_TO);
	m_card.fn().vkCmdClearColorImage(cb, window.image, window.layout, &black, 1, &all);
}

/* The picture of the frame is black for now: so is the window. */
void
VkBoardDrawing::present(PictureRect, unsigned window_w, unsigned window_h)
{
	clear_window(window_w, window_h);
}

/* The window black, then each rectangle at its shade (the window image has
 * the top line first, as the rectangles count them) */
void
VkBoardDrawing::draw_loading(std::span<const LoadingRect> rects, unsigned window_w, unsigned window_h)
{
	clear_window(window_w, window_h);
	if (rects.empty())
		return;

	const VkCommandBuffer cb = m_card.commands();
	vk::Image &window = m_card.window_image();
	const VkRenderingAttachmentInfo colour{ VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO, nullptr, window.view,
	                                        VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_RESOLVE_MODE_NONE,
	                                        VK_NULL_HANDLE, VK_IMAGE_LAYOUT_UNDEFINED, VK_ATTACHMENT_LOAD_OP_LOAD,
	                                        VK_ATTACHMENT_STORE_OP_STORE, {} };
	const VkRenderingInfo rendering{ VK_STRUCTURE_TYPE_RENDERING_INFO, nullptr, 0, { { 0, 0 }, { window.w, window.h } },
	                                 1, 0, 1, &colour, nullptr, nullptr };

	m_card.barrier(cb, window, vk::DRAWN_IN);
	m_card.fn().vkCmdBeginRendering(cb, &rendering);
	for (const LoadingRect &r : rects) {
		const unsigned x0 = std::min(r.x, window.w), y0 = std::min(r.y, window.h);
		const unsigned x1 = std::min(r.x + r.w, window.w), y1 = std::min(r.y + r.h, window.h);
		if (x1 <= x0 || y1 <= y0)
			continue;
		const VkClearAttachment shade{ VK_IMAGE_ASPECT_COLOR_BIT, 0, { { { r.shade, r.shade, r.shade, 1.0f } } } };
		const VkClearRect where{ { { static_cast<int32_t>(x0), static_cast<int32_t>(y0) }, { x1 - x0, y1 - y0 } }, 0, 1 };

		m_card.fn().vkCmdClearAttachments(cb, 1, &shade, 1, &where);
	}
	m_card.fn().vkCmdEndRendering(cb);
}

} /* namespace */

std::unique_ptr<BoardDrawing>
BoardDrawing::make_vk(vk::Context &card)
{
	return std::make_unique<VkBoardDrawing>(card);
}

} /* namespace odin */
