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

#ifndef ODIN_BOARD_DRAWING_VK_HPP
#define ODIN_BOARD_DRAWING_VK_HPP

/* The board drawing with Vulkan (board_drawing.hpp), for its own files
 * only: board_drawing_vk.cpp (made; what comes with the next steps),
 * board_pictures_vk.cpp (the pictures and what goes from one to the next).
 * The pictures keep the lines in the order OpenGL gives them, the bottom one
 * first: drawn with the same coordinates, they hold the same bytes
 * (MARKDOWN/106 § 2.2). */

#include <optional>

#include "odin/board_drawing.hpp"
#include "odin/vk_common.hpp"

namespace odin {

class VkBoardDrawing : public BoardDrawing {
public:
	explicit VkBoardDrawing(vk::Context &card);
	~VkBoardDrawing() override;
	VkBoardDrawing(const VkBoardDrawing &) = delete;
	VkBoardDrawing &operator=(const VkBoardDrawing &) = delete;

	void set_scale(unsigned scale) override;

	/* The scene is emptied; its meshes are not drawn yet (V3) */
	void begin_scene(const odin_display_list_t &list, const SceneDebug &debug) override;
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

	void scene_to_pipe(unsigned slot) override;
	void blend_scene_to_pipe(unsigned slot, unsigned buffer, uint32_t rate) override;
	void keep_picture(unsigned slot, unsigned buffer) override;

	void set_layer(unsigned i, LayerTexels texels, const void *pixels) override;
	void compose(const Compose &compose, unsigned shown, const bool layers[2]) override;
	void present(PictureRect rect, unsigned window_w, unsigned window_h) override;
	HostPicture frame_picture() const override { return { FRAME_PICTURE, width(), height() }; }
	void set_filter(const std::string &) override {}
	void draw_loading(std::span<const LoadingRect> rects, unsigned window_w, unsigned window_h) override;

	void read_kept(std::span<uint8_t> pictures) override;
	void write_kept(std::span<const uint8_t> pictures) override;

	HostTexture make_texture(Texels, std::span<const TexelRect>) override { return { m_next_texture++ }; }
	void forget_textures(std::span<const HostTexture>) override {}
	HostSampler make_sampler(const TextureSampling &, unsigned) override { return { m_next_sampler++ }; }
	void forget_sampler(HostSampler) override {}

private:
	/* The number of the picture of the frame, as the window reads it */
	static constexpr uint32_t FRAME_PICTURE = 1;

	unsigned width() const { return SCREEN_WIDTH * m_scale; }
	unsigned height() const { return SCREEN_HEIGHT * m_scale; }
	vk::Image &kept(unsigned i) { return m_kept[i]; }
	void build_pictures();
	void destroy_pictures(bool kept_too);
	vk::Image make_picture(unsigned w, unsigned h);
	void clear(VkCommandBuffer cb, vk::Image &image, float alpha);
	/* A copy, each pixel the nearest */
	void copy_picture(VkCommandBuffer cb, vk::Image &from, vk::Image &to);
	void clear_window(unsigned window_w, unsigned window_h);

	vk::Context &m_card;
	const vk::Functions &m_fn;
	unsigned m_scale = 1;
	/* Where a list is drawn; the KEPT pictures (the two 3D buffers, then
	 * the pipe of the last three lists); the picture of the frame; one
	 * of the size of the screen of the hardware for the save states */
	vk::Image m_scene, m_kept[KEPT], m_frame, m_state;
	/* The 2D layers, kept from frame to frame, and what they hold; what a
	 * layer not shown reads, as OpenGL's texture 0: (0, 0, 0, 1) */
	vk::Image m_layers[2];
	std::optional<LayerTexels> m_layer_texels[2];
	vk::Image m_none;
	VkSampler m_nearest = VK_NULL_HANDLE;
	/* The passes: the blender of the 3D buffers, the composition, the
	 * picture brought to the size of the window */
	vk::Pass m_blender, m_compose, m_presenter;
	uint32_t m_next_texture = 1, m_next_sampler = 1;
};

} /* namespace odin */

#endif /* ODIN_BOARD_DRAWING_VK_HPP */
