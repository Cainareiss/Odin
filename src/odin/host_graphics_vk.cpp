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

/* The graphics card of the host with Vulkan (host_graphics.hpp; the card
 * itself: vk_common.hpp). Everything is drawn into the image of the window,
 * the top line first as Vulkan and Dear ImGui have it; show_window copies it
 * into the swap chain and shows it. The bezel, the sides blurred and the
 * blurred background of the menu are not drawn yet (MARKDOWN/106 § 9). */

#include "odin/host_graphics.hpp"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <unordered_map>
#include <vector>

#include <SDL3/SDL.h>
#include <SDL3/SDL_vulkan.h>

#include "imgui/imgui.h"
#include "imgui/imgui_impl_vulkan.h"
#include "odin/board_drawing.hpp"
#include "odin/vk_common.hpp"

namespace odin {

namespace {

constexpr VkFormat UI_FORMAT = VK_FORMAT_R8G8B8A8_UNORM;

void
imgui_check(VkResult result)
{
	if (result < 0) {
		std::fprintf(stderr, "ERROR: vulkan: the menu: %d\n", static_cast<int>(result));
		std::abort();
	}
}

/* A level of an image from one layout to another, after what wrote it */
void
level_barrier(const vk::Functions &fn, VkCommandBuffer cb, VkImage image, unsigned level, VkImageLayout from,
              VkImageLayout to, VkPipelineStageFlags2 next_stage, VkAccessFlags2 next_access)
{
	const VkImageMemoryBarrier2 barrier{ VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2, nullptr,
	                                     VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, VK_ACCESS_2_MEMORY_WRITE_BIT,
	                                     next_stage, next_access, from, to, VK_QUEUE_FAMILY_IGNORED,
	                                     VK_QUEUE_FAMILY_IGNORED, image,
	                                     { VK_IMAGE_ASPECT_COLOR_BIT, level, 1, 0, 1 } };
	const VkDependencyInfo info{ VK_STRUCTURE_TYPE_DEPENDENCY_INFO, nullptr, 0, 0, nullptr, 0, nullptr, 1, &barrier };

	fn.vkCmdPipelineBarrier2(cb, &info);
}

class VkGraphics : public HostGraphics {
public:
	VkGraphics(SDL_Window *window, std::unique_ptr<vk::Context> card);
	~VkGraphics() override;

	const std::string &name() const override { return m_card->name(); }
	/* Vulkan has no context to make current: one thread at a time uses
	 * the card, as display.hpp has it */
	void take_thread() override {}
	void leave_thread() override {}

	void clear_window(unsigned window_w, unsigned window_h) override;
	void show_window() override;
	bool set_waits(bool wait) override;
	void read(HostPicture picture, unsigned w, unsigned h, uint8_t *pixels) override;

	HostImage make_image(const SDL_Surface &rgba, bool smooth) override;
	void forget_image(HostImage image) override;

	void draw_bezel(HostImage, unsigned, unsigned) override {}
	void draw_blurred_sides(HostPicture, PictureRect, unsigned, unsigned) override {}

	void blur_background(HostPicture) override {}
	void blur_window(unsigned, unsigned) override {}
	void draw_background(unsigned, unsigned, unsigned, unsigned, float) override {}
	void start_ui() override;
	void draw_ui(ImDrawData *data, unsigned window_w, unsigned window_h, const char *what) override;
	void stop_ui() override;
	uint64_t ui_image(HostImage image) const override;

	std::unique_ptr<BoardDrawing> make_board_drawing() override { return BoardDrawing::make_vk(*m_card); }

private:
	void make_swapchain();
	void destroy_swapchain();
	bool can_present(VkPresentModeKHR mode) const;

	SDL_Window *m_window;
	std::unique_ptr<vk::Context> m_card;
	const vk::Functions &m_fn;

	/* The swap chain, made again at the next frame when the window or the
	 * wait for the display changes; a semaphore for each of its images,
	 * which the card signals once that image is drawn, and one for each
	 * frame, signalled once its image of the chain is free */
	VkSwapchainKHR m_swapchain = VK_NULL_HANDLE;
	VkExtent2D m_extent{};
	std::vector<VkImage> m_chain;
	std::vector<VkSemaphore> m_drawn;
	VkSemaphore m_free[vk::Context::FRAMES] = {};
	std::vector<VkPresentModeKHR> m_modes;
	bool m_waits = false;
	bool m_remake = true;

	/* The images of the menu and the bezel, by number; the descriptor
	 * of each that Dear ImGui draws with, made when first asked for */
	std::unordered_map<uint32_t, vk::Image> m_images;
	uint32_t m_next_image = 1;
	mutable std::unordered_map<uint32_t, VkDescriptorSet> m_ui_sets;
	bool m_ui = false;
};

VkGraphics::VkGraphics(SDL_Window *window, std::unique_ptr<vk::Context> card)
	: m_window(window), m_card(std::move(card)), m_fn(m_card->fn())
{
	uint32_t count = 0;

	m_fn.vkGetPhysicalDeviceSurfacePresentModesKHR(m_card->gpu(), m_card->surface(), &count, nullptr);
	m_modes.resize(count);
	m_fn.vkGetPhysicalDeviceSurfacePresentModesKHR(m_card->gpu(), m_card->surface(), &count, m_modes.data());

	const VkSemaphoreCreateInfo semaphore{ VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO, nullptr, 0 };
	for (VkSemaphore &free : m_free)
		m_card->check(m_fn.vkCreateSemaphore(m_card->device(), &semaphore, nullptr, &free), "a semaphore");
}

VkGraphics::~VkGraphics()
{
	m_fn.vkDeviceWaitIdle(m_card->device());
	stop_ui();
	for (auto &[id, image] : m_images)
		m_card->destroy_image(image);
	destroy_swapchain();
	for (const VkSemaphore free : m_free)
		m_fn.vkDestroySemaphore(m_card->device(), free, nullptr);
}

bool
VkGraphics::can_present(VkPresentModeKHR mode) const
{
	return std::find(m_modes.begin(), m_modes.end(), mode) != m_modes.end();
}

void
VkGraphics::destroy_swapchain()
{
	for (const VkSemaphore drawn : m_drawn)
		m_fn.vkDestroySemaphore(m_card->device(), drawn, nullptr);
	m_drawn.clear();
	m_chain.clear();
	if (m_swapchain)
		m_fn.vkDestroySwapchainKHR(m_card->device(), m_swapchain, nullptr);
	m_swapchain = VK_NULL_HANDLE;
}

/* At the size of the window in pixels; none while it has no size (made
 * small). Its images take a copy of the window image, as they are: no
 * colour space conversion, as OpenGL's window. */
void
VkGraphics::make_swapchain()
{
	VkSurfaceCapabilitiesKHR caps;
	uint32_t count = 0;
	int w = 0, h = 0;

	m_card->send_and_wait();
	m_fn.vkQueueWaitIdle(m_card->queue());
	destroy_swapchain();
	m_remake = false;

	m_fn.vkGetPhysicalDeviceSurfaceCapabilitiesKHR(m_card->gpu(), m_card->surface(), &caps);
	SDL_GetWindowSizeInPixels(m_window, &w, &h);
	m_extent = caps.currentExtent.width != UINT32_MAX ? caps.currentExtent :
	           VkExtent2D{ std::clamp(static_cast<uint32_t>(w), caps.minImageExtent.width, caps.maxImageExtent.width),
	                       std::clamp(static_cast<uint32_t>(h), caps.minImageExtent.height, caps.maxImageExtent.height) };
	if (m_extent.width == 0 || m_extent.height == 0)
		return;
	if (!(caps.supportedUsageFlags & VK_IMAGE_USAGE_TRANSFER_DST_BIT))
		m_card->fatal("the swap chain takes no copy", VK_ERROR_FEATURE_NOT_PRESENT);

	m_fn.vkGetPhysicalDeviceSurfaceFormatsKHR(m_card->gpu(), m_card->surface(), &count, nullptr);
	std::vector<VkSurfaceFormatKHR> formats(count);
	m_fn.vkGetPhysicalDeviceSurfaceFormatsKHR(m_card->gpu(), m_card->surface(), &count, formats.data());
	VkSurfaceFormatKHR format = formats.empty() ? VkSurfaceFormatKHR{} : formats[0];
	for (const VkSurfaceFormatKHR &f : formats)
		if ((f.format == VK_FORMAT_B8G8R8A8_UNORM || f.format == VK_FORMAT_R8G8B8A8_UNORM) &&
		    f.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR)
			format = f;

	/* Waiting for the display, or not: the frame shown at once, else the
	 * last one drawn */
	VkPresentModeKHR mode = VK_PRESENT_MODE_FIFO_KHR;
	if (!m_waits)
		mode = can_present(VK_PRESENT_MODE_IMMEDIATE_KHR) ? VK_PRESENT_MODE_IMMEDIATE_KHR :
		       can_present(VK_PRESENT_MODE_MAILBOX_KHR) ? VK_PRESENT_MODE_MAILBOX_KHR : VK_PRESENT_MODE_FIFO_KHR;

	uint32_t images = caps.minImageCount + 1;
	if (caps.maxImageCount)
		images = std::min(images, caps.maxImageCount);
	const VkCompositeAlphaFlagBitsKHR alpha =
		(caps.supportedCompositeAlpha & VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR) ? VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR :
		static_cast<VkCompositeAlphaFlagBitsKHR>(caps.supportedCompositeAlpha & -caps.supportedCompositeAlpha);
	const VkSwapchainCreateInfoKHR info{ VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR, nullptr, 0, m_card->surface(),
	                                     images, format.format, format.colorSpace, m_extent, 1,
	                                     VK_IMAGE_USAGE_TRANSFER_DST_BIT, VK_SHARING_MODE_EXCLUSIVE, 0, nullptr,
	                                     caps.currentTransform, alpha, mode, VK_TRUE, VK_NULL_HANDLE };
	m_card->check(m_fn.vkCreateSwapchainKHR(m_card->device(), &info, nullptr, &m_swapchain), "the swap chain");

	m_fn.vkGetSwapchainImagesKHR(m_card->device(), m_swapchain, &count, nullptr);
	m_chain.resize(count);
	m_fn.vkGetSwapchainImagesKHR(m_card->device(), m_swapchain, &count, m_chain.data());
	const VkSemaphoreCreateInfo semaphore{ VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO, nullptr, 0 };
	m_drawn.resize(count);
	for (VkSemaphore &drawn : m_drawn)
		m_card->check(m_fn.vkCreateSemaphore(m_card->device(), &semaphore, nullptr, &drawn), "a semaphore");
}

void
VkGraphics::clear_window(unsigned window_w, unsigned window_h)
{
	const VkCommandBuffer cb = m_card->commands();
	vk::Image &window = m_card->window_image(window_w, window_h);
	const VkClearColorValue black{ { 0.0f, 0.0f, 0.0f, 1.0f } };
	const VkImageSubresourceRange all{ VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };

	m_card->barrier(cb, window, vk::COPIED_TO);
	m_fn.vkCmdClearColorImage(cb, window.image, window.layout, &black, 1, &all);
}

/* The window image into the image of the chain that is free, each pixel
 * the nearest where the window has just changed its size */
void
VkGraphics::show_window()
{
	int w = 0, h = 0;

	SDL_GetWindowSizeInPixels(m_window, &w, &h);
	if (m_swapchain && (m_extent.width != static_cast<uint32_t>(w) || m_extent.height != static_cast<uint32_t>(h)))
		m_remake = true;
	if (m_remake)
		make_swapchain();

	const VkCommandBuffer cb = m_card->commands();
	const VkSemaphore free = m_free[m_card->frame()];
	uint32_t index = 0;

	if (!m_swapchain ||
	    m_fn.vkAcquireNextImageKHR(m_card->device(), m_swapchain, UINT64_MAX, free, VK_NULL_HANDLE, &index) < 0) {
		m_remake = true;
		m_card->end_frame({});
		return;
	}

	vk::Image &window = m_card->window_image();
	if (!window.image)
		clear_window(m_extent.width, m_extent.height);
	m_card->barrier(cb, window, vk::COPIED_FROM);
	level_barrier(m_fn, cb, m_chain[index], 0, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
	              VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT);
	const VkImageBlit blit{ { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 },
	                        { { 0, 0, 0 }, { static_cast<int32_t>(window.w), static_cast<int32_t>(window.h), 1 } },
	                        { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 },
	                        { { 0, 0, 0 },
	                          { static_cast<int32_t>(m_extent.width), static_cast<int32_t>(m_extent.height), 1 } } };
	m_fn.vkCmdBlitImage(cb, window.image, window.layout, m_chain[index], VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &blit,
	                    VK_FILTER_NEAREST);
	level_barrier(m_fn, cb, m_chain[index], 0, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
	              VK_PIPELINE_STAGE_2_NONE, VK_ACCESS_2_NONE);
	m_card->end_frame({ free, m_drawn[index] });

	const VkPresentInfoKHR present{ VK_STRUCTURE_TYPE_PRESENT_INFO_KHR, nullptr, 1, &m_drawn[index], 1, &m_swapchain,
	                                &index, nullptr };
	const VkResult result = m_fn.vkQueuePresentKHR(m_card->queue(), &present);
	if (result == VK_SUBOPTIMAL_KHR || result == VK_ERROR_OUT_OF_DATE_KHR)
		m_remake = true;
	else if (result != VK_SUCCESS)
		m_card->fatal("showing the window", result);
}

/* False when the display cannot be left unwaited: no present mode for it */
bool
VkGraphics::set_waits(bool wait)
{
	if (!wait && !can_present(VK_PRESENT_MODE_IMMEDIATE_KHR) && !can_present(VK_PRESENT_MODE_MAILBOX_KHR))
		return false;
	if (wait != m_waits) {
		m_waits = wait;
		m_remake = true;
	}
	return true;
}

/* A picture of the board drawing keeps the lines in the order of OpenGL,
 * the bottom one first; the window has the top one first. */
void
VkGraphics::read(HostPicture picture, unsigned w, unsigned h, uint8_t *pixels)
{
	vk::Image *const image = picture.id ? m_card->picture(picture.id) : &m_card->window_image();

	if (!image || !image->image || w > image->w || h > image->h) {
		std::memset(pixels, 0, size_t{w} * h * 3);
		return;
	}
	const VkCommandBuffer cb = m_card->commands();
	vk::Buffer &out = m_card->staging(VkDeviceSize{w} * h * 4);
	const VkBufferImageCopy region{ 0, 0, 0, { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 }, { 0, 0, 0 }, { w, h, 1 } };

	m_card->barrier(cb, *image, vk::COPIED_FROM);
	m_fn.vkCmdCopyImageToBuffer(cb, image->image, image->layout, out.buffer, 1, &region);
	m_card->send_and_wait();

	for (unsigned y = 0; y < h; y++) {
		const uint8_t *from = out.bytes + size_t{picture.id ? y : h - 1 - y} * w * 4;

		for (unsigned x = 0; x < w; x++, from += 4, pixels += 3)
			std::memcpy(pixels, from, 3);
	}
}

/* Its levels made each from the one before, smoothly. Odin's own sampling
 * of it is Dear ImGui's (MARKDOWN/106 § 9): `smooth` is not followed yet. */
HostImage
VkGraphics::make_image(const SDL_Surface &rgba, bool)
{
	const unsigned w = static_cast<unsigned>(rgba.w), h = static_cast<unsigned>(rgba.h);
	unsigned levels = 1;

	while ((std::max(w, h) >> levels) > 0)
		levels++;
	vk::Image image = m_card->make_image(VK_FORMAT_R8G8B8A8_UNORM, w, h, levels,
	                                     VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT |
	                                             VK_IMAGE_USAGE_TRANSFER_DST_BIT);
	const VkCommandBuffer cb = m_card->commands();
	vk::Buffer &in = m_card->staging(VkDeviceSize{w} * h * 4);

	for (unsigned y = 0; y < h; y++)
		std::memcpy(in.bytes + size_t{y} * w * 4, static_cast<const uint8_t *>(rgba.pixels) + size_t(y) * size_t(rgba.pitch),
		            size_t{w} * 4);
	m_card->barrier(cb, image, vk::COPIED_TO);
	const VkBufferImageCopy region{ 0, 0, 0, { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 }, { 0, 0, 0 }, { w, h, 1 } };
	m_fn.vkCmdCopyBufferToImage(cb, in.buffer, image.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

	for (unsigned level = 1; level < levels; level++) {
		const auto size = [&](unsigned l) {
			return VkOffset3D{ static_cast<int32_t>(std::max(w >> l, 1u)), static_cast<int32_t>(std::max(h >> l, 1u)), 1 };
		};
		const VkImageBlit blit{ { VK_IMAGE_ASPECT_COLOR_BIT, level - 1, 0, 1 }, { { 0, 0, 0 }, size(level - 1) },
		                        { VK_IMAGE_ASPECT_COLOR_BIT, level, 0, 1 }, { { 0, 0, 0 }, size(level) } };

		level_barrier(m_fn, cb, image.image, level - 1, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
		              VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT,
		              VK_ACCESS_2_TRANSFER_READ_BIT);
		m_fn.vkCmdBlitImage(cb, image.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, image.image,
		                    VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &blit, VK_FILTER_LINEAR);
	}
	for (unsigned level = 0; level < levels; level++)
		level_barrier(m_fn, cb, image.image, level,
		              level + 1 < levels ? VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL : VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
		              VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
		              VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);
	image.layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

	const uint32_t id = m_next_image++;
	m_images[id] = image;
	return { id, static_cast<float>(w), static_cast<float>(h) };
}

void
VkGraphics::forget_image(HostImage image)
{
	const auto found = m_images.find(image.id);

	if (found == m_images.end())
		return;
	vk::Image gone = found->second;
	m_images.erase(found);
	m_card->later([card = m_card.get(), gone]() mutable { card->destroy_image(gone); });
	if (const auto set = m_ui_sets.find(image.id); set != m_ui_sets.end()) {
		const VkDescriptorSet descriptors = set->second;

		m_ui_sets.erase(set);
		m_card->later([descriptors] { ImGui_ImplVulkan_RemoveTexture(descriptors); });
	}
}

/* Dear ImGui finds the functions of Vulkan through the instance. */
void
VkGraphics::start_ui()
{
	const auto load = [](const char *name, void *self) {
		const vk::Context &card = *static_cast<VkGraphics *>(self)->m_card;

		return card.fn().vkGetInstanceProcAddr(card.instance(), name);
	};
	static const VkFormat format = UI_FORMAT;
	ImGui_ImplVulkan_InitInfo info{};

	ImGui_ImplVulkan_LoadFunctions(m_card->api_version(), load, this);
	info.ApiVersion = m_card->api_version();
	info.Instance = m_card->instance();
	info.PhysicalDevice = m_card->gpu();
	info.Device = m_card->device();
	info.QueueFamily = m_card->queue_family();
	info.Queue = m_card->queue();
	info.DescriptorPoolSize = 256;
	info.MinImageCount = 2;
	info.ImageCount = std::max<uint32_t>(2, static_cast<uint32_t>(m_chain.size()));
	info.UseDynamicRendering = true;
	info.PipelineInfoMain.PipelineRenderingCreateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO_KHR;
	info.PipelineInfoMain.PipelineRenderingCreateInfo.colorAttachmentCount = 1;
	info.PipelineInfoMain.PipelineRenderingCreateInfo.pColorAttachmentFormats = &format;
	info.CheckVkResultFn = imgui_check;
	ImGui_ImplVulkan_Init(&info);
	m_ui = true;
}

void
VkGraphics::draw_ui(ImDrawData *data, unsigned window_w, unsigned window_h, const char *)
{
	const VkCommandBuffer cb = m_card->commands();
	vk::Image &window = m_card->window_image(window_w, window_h);
	const VkRenderingAttachmentInfo colour{ VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO, nullptr, window.view,
	                                        VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_RESOLVE_MODE_NONE,
	                                        VK_NULL_HANDLE, VK_IMAGE_LAYOUT_UNDEFINED, VK_ATTACHMENT_LOAD_OP_LOAD,
	                                        VK_ATTACHMENT_STORE_OP_STORE, {} };
	const VkRenderingInfo rendering{ VK_STRUCTURE_TYPE_RENDERING_INFO, nullptr, 0, { { 0, 0 }, { window_w, window_h } },
	                                 1, 0, 1, &colour, nullptr, nullptr };

	m_card->barrier(cb, window, vk::DRAWN_IN);
	m_fn.vkCmdBeginRendering(cb, &rendering);
	ImGui_ImplVulkan_NewFrame();
	ImGui_ImplVulkan_RenderDrawData(data, cb);
	m_fn.vkCmdEndRendering(cb);
}

void
VkGraphics::stop_ui()
{
	if (!m_ui)
		return;
	m_card->send_and_wait();
	for (const auto &[id, descriptors] : m_ui_sets)
		ImGui_ImplVulkan_RemoveTexture(descriptors);
	m_ui_sets.clear();
	ImGui_ImplVulkan_Shutdown();
	m_ui = false;
}

uint64_t
VkGraphics::ui_image(HostImage image) const
{
	if (const auto set = m_ui_sets.find(image.id); set != m_ui_sets.end())
		return reinterpret_cast<uint64_t>(set->second);
	const auto found = m_images.find(image.id);
	if (!m_ui || found == m_images.end())
		return 0;
	const VkDescriptorSet descriptors =
		ImGui_ImplVulkan_AddTexture(found->second.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
	m_ui_sets[image.id] = descriptors;
	return reinterpret_cast<uint64_t>(descriptors);
}

} /* namespace */

uint64_t
HostGraphics::window_flags_vk()
{
	return SDL_WINDOW_VULKAN;
}

/* Vulkan for that window, or nothing with the reason on stderr */
std::unique_ptr<HostGraphics>
HostGraphics::make_vk(SDL_Window *window, unsigned, unsigned)
{
	std::string why;
	std::unique_ptr<vk::Context> card = vk::Context::make(window, why);

	if (!card) {
		std::fprintf(stderr, "ERROR: vulkan: %s\n", why.c_str());
		return nullptr;
	}
	std::printf("renderer: Vulkan %u.%u on %s\n", VK_API_VERSION_MAJOR(card->api_version()),
	            VK_API_VERSION_MINOR(card->api_version()), card->name().c_str());
	return std::make_unique<VkGraphics>(window, std::move(card));
}

} /* namespace odin */
