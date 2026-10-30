/*
 *  ██████╗ ██████╗ ██╗███╗   ██╗
 * ██╔═══██╗██╔══██╗██║████╗  ██║
 * ██║   ██║██║  ██║██║██╔██╗ ██║
 * ██║   ██║██║  ██║██║██║╚██╗██║
 * ╚██████╔╝██████╔╝██║██║ ╚████║
 *  ╚═════╝ ╚═════╝ ╚═╝╚═╝  ╚═══╝
 *
 * Copyright (C) 2026 Réda Chérif-Touil
 * Copyright (C) 2026 Daniel Martin
 *
 * Odin is free software: you can redistribute it and/or modify it under
 * the terms of the GNU General Public License as published by the Free
 * Software Foundation, either version 3 of the License, or (at your
 * option) any later version.
 *
 * Odin is distributed in the hope that it will be useful, but WITHOUT ANY
 * WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 * for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with Odin.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstring>
#include <deque>
#include <unordered_map>
#include <vector>

#include "odin/host/gfx/spirv.hpp"
#include "odin/host/renderer/hikaru/vk/drawing_vk.hpp"

/* Draws Neon 250 scenes to a target, then to the frame's picture */

namespace odin {

namespace {

struct DrawPush {
	float most_w, lod_bias;
	int32_t flags;
	float scale;
};

constexpr std::array<VkCompareOp, 8> COMPARE = { VK_COMPARE_OP_NEVER, VK_COMPARE_OP_LESS, VK_COMPARE_OP_EQUAL,
	                                         VK_COMPARE_OP_LESS_OR_EQUAL, VK_COMPARE_OP_GREATER_OR_EQUAL,
	                                         VK_COMPARE_OP_NOT_EQUAL, VK_COMPARE_OP_GREATER_OR_EQUAL,
	                                         VK_COMPARE_OP_ALWAYS };
constexpr std::array<VkBlendFactor, 8> SRC = { VK_BLEND_FACTOR_ZERO, VK_BLEND_FACTOR_ONE, VK_BLEND_FACTOR_DST_COLOR,
	                                       VK_BLEND_FACTOR_ONE_MINUS_DST_COLOR, VK_BLEND_FACTOR_SRC_ALPHA,
	                                       VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA, VK_BLEND_FACTOR_DST_ALPHA,
	                                       VK_BLEND_FACTOR_ONE_MINUS_DST_ALPHA };
constexpr std::array<VkBlendFactor, 8> DST = { VK_BLEND_FACTOR_ZERO, VK_BLEND_FACTOR_ONE, VK_BLEND_FACTOR_SRC_COLOR,
	                                       VK_BLEND_FACTOR_ONE_MINUS_SRC_COLOR, VK_BLEND_FACTOR_SRC_ALPHA,
	                                       VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA, VK_BLEND_FACTOR_DST_ALPHA,
	                                       VK_BLEND_FACTOR_ONE_MINUS_DST_ALPHA };

} /* namespace */

struct VkDrawing::Pmx {
	vk::Context &card;
	const vk::Functions &fn;
	VkDescriptorSetLayout images_layout = VK_NULL_HANDLE;
	VkPipelineLayout layout = VK_NULL_HANDLE;
	VkShaderModule vertex = VK_NULL_HANDLE, fragment = VK_NULL_HANDLE;
	/* Native 2D (pmx.geom), where supported */
	VkShaderModule geometry = VK_NULL_HANDLE, native_fragment = VK_NULL_HANDLE;
	std::unordered_map<uint32_t, VkPipeline> pipelines;
	std::unordered_map<uint32_t, VkSampler> samplers;
	std::unordered_map<uint32_t, vk::Image> images;
	/* Descriptor sets by image and sampler, from dedicated pools. They
	 * last as long as their image. */
	std::unordered_map<VkSampler, uint32_t> sampler_number;
	std::unordered_map<uint64_t, VkDescriptorSet> sets;
	struct Pool {
		VkDescriptorPool pool;
		uint32_t left;
	};
	std::vector<Pool> pools;
	static constexpr uint32_t SETS_A_POOL = 1024;
	/* Sets of freed images and the scene that last used them, returned to
	 * their pool once the GPU has finished that scene */
	struct Retired {
		VkDescriptorSet set;
		size_t pool;
		uint64_t scene;
	};
	std::deque<Retired> retired;
	std::unordered_map<VkDescriptorSet, size_t> pool_of;
	uint64_t scenes = 0;

	VkDescriptorSet make_set(const vk::Image &image, VkSampler sampler)
	{
		size_t from = 0;

		while (from < pools.size() && pools[from].left == 0)
			from++;
		if (from == pools.size()) {
			const VkDescriptorPoolSize size{ VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, SETS_A_POOL };
			const VkDescriptorPoolCreateInfo info{ VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO, nullptr,
				                               VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT, SETS_A_POOL, 1, &size };
			VkDescriptorPool pool;

			card.check(fn.vkCreateDescriptorPool(card.device(), &info, nullptr, &pool), "the sets of the Neon 250");
			pools.push_back({ pool, SETS_A_POOL });
		}

		const VkDescriptorSetAllocateInfo info{ VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO, nullptr, pools[from].pool, 1, &images_layout };
		VkDescriptorSet set;

		card.check(fn.vkAllocateDescriptorSets(card.device(), &info, &set), "a set of the Neon 250");
		pools[from].left--;
		pool_of[set] = from;

		const VkDescriptorImageInfo described{ sampler, image.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };
		const VkWriteDescriptorSet write{ VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, set, 0, 0, 1,
			                          VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, &described, nullptr, nullptr };

		fn.vkUpdateDescriptorSets(card.device(), 1, &write, 0, nullptr);
		return set;
	}

	void forget_sets(uint32_t image)
	{
		for (auto it = sets.begin(); it != sets.end();) {
			if (it->first >> 32 != image) {
				++it;
				continue;
			}
			const auto owner = pool_of.find(it->second);

			retired.push_back({ it->second, owner->second, scenes });
			pool_of.erase(owner);
			it = sets.erase(it);
		}
	}

	/* At most one scene per frame, so the GPU has finished any scene that
	 * many behind */
	void take_back_sets()
	{
		while (!retired.empty() && scenes - retired.front().scene > vk::Context::FRAMES) {
			const Retired &gone = retired.front();

			fn.vkFreeDescriptorSets(card.device(), pools[gone.pool].pool, 1, &gone.set);
			pools[gone.pool].left++;
			retired.pop_front();
		}
	}

	Pmx(vk::Context &c, const vk::Functions &f) : card(c), fn(f)
	{
		const VkDescriptorSetLayoutBinding binding{ 0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1,
			                                    VK_SHADER_STAGE_FRAGMENT_BIT, nullptr };
		const VkDescriptorSetLayoutCreateInfo set{ VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO, nullptr, 0, 1,
			                                   &binding };

		card.check(fn.vkCreateDescriptorSetLayout(card.device(), &set, nullptr, &images_layout), "the images of the Neon 250");

		const VkPushConstantRange range{ VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(DrawPush) };
		const VkPipelineLayoutCreateInfo info{ VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO, nullptr, 0, 1,
			                               &images_layout, 1, &range };

		card.check(fn.vkCreatePipelineLayout(card.device(), &info, nullptr, &layout), "the layout of the Neon 250");

		const vk::Spirv vert = ODIN_SPIRV_OF(pmx_vert), frag = ODIN_SPIRV_OF(pmx_frag);
		const VkShaderModuleCreateInfo v{ VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO, nullptr, 0, vert.bytes, vert.code };
		const VkShaderModuleCreateInfo g{ VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO, nullptr, 0, frag.bytes, frag.code };

		card.check(fn.vkCreateShaderModule(card.device(), &v, nullptr, &vertex), "a shader");
		card.check(fn.vkCreateShaderModule(card.device(), &g, nullptr, &fragment), "a shader");
		if (card.geometry_shaders()) {
			const vk::Spirv between = ODIN_SPIRV_OF(pmx_geom), native = ODIN_SPIRV_OF(pmx_native_frag);
			const VkShaderModuleCreateInfo b{ VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO, nullptr, 0, between.bytes, between.code };
			const VkShaderModuleCreateInfo n{ VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO, nullptr, 0, native.bytes, native.code };

			card.check(fn.vkCreateShaderModule(card.device(), &b, nullptr, &geometry), "a shader");
			card.check(fn.vkCreateShaderModule(card.device(), &n, nullptr, &native_fragment), "a shader");
		}
	}

	~Pmx()
	{
		const VkDevice device = card.device();

		/* Nothing is in use now, and the sets go with their pools */
		card.send_and_wait();
		for (const Pool &pool : pools)
			fn.vkDestroyDescriptorPool(device, pool.pool, nullptr);
		for (auto &[id, image] : images)
			card.destroy_image(image);
		for (const auto &[key, pipeline] : pipelines)
			fn.vkDestroyPipeline(device, pipeline, nullptr);
		for (const auto &[key, sampler] : samplers)
			fn.vkDestroySampler(device, sampler, nullptr);
		fn.vkDestroyShaderModule(device, vertex, nullptr);
		fn.vkDestroyShaderModule(device, fragment, nullptr);
		fn.vkDestroyShaderModule(device, geometry, nullptr);
		fn.vkDestroyShaderModule(device, native_fragment, nullptr);
		fn.vkDestroyPipelineLayout(device, layout, nullptr);
		fn.vkDestroyDescriptorSetLayout(device, images_layout, nullptr);
	}

	/* One per blend mode; the rest is dynamic state */
	VkPipeline pipeline(const PmxDraw &draw, VkFormat colour)
	{
		const uint32_t key = (draw.blend ? 1u | uint32_t{ draw.src & 7u } << 1 | uint32_t{ draw.dst & 7u } << 4 : 0u) |
		                     uint32_t{ draw.native } << 7;
		const auto found = pipelines.find(key);

		if (found != pipelines.end())
			return found->second;

		const VkPipelineShaderStageCreateInfo stages[3] = {
			{ VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0, VK_SHADER_STAGE_VERTEX_BIT, vertex, "main", nullptr },
			{ VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0, VK_SHADER_STAGE_FRAGMENT_BIT,
			  draw.native ? native_fragment : fragment, "main", nullptr },
			{ VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0, VK_SHADER_STAGE_GEOMETRY_BIT, geometry, "main", nullptr },
		};
		const VkVertexInputBindingDescription binding{ 0, sizeof(PmxVertex), VK_VERTEX_INPUT_RATE_VERTEX };
		const VkVertexInputAttributeDescription attributes[4] = {
			{ 0, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(PmxVertex, x) },
			{ 1, 0, VK_FORMAT_R32G32_SFLOAT, offsetof(PmxVertex, u) },
			{ 2, 0, VK_FORMAT_R8G8B8A8_UNORM, offsetof(PmxVertex, color) },
			{ 3, 0, VK_FORMAT_R8G8B8A8_UNORM, offsetof(PmxVertex, offset) },
		};
		const VkPipelineVertexInputStateCreateInfo input{ VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO, nullptr, 0,
			                                          1, &binding, 4, attributes };
		const VkPipelineInputAssemblyStateCreateInfo assembly{ VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO, nullptr, 0,
			                                               VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST, VK_FALSE };
		const VkPipelineViewportStateCreateInfo viewport{ VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO, nullptr, 0, 1, nullptr, 1, nullptr };
		VkPipelineRasterizationStateCreateInfo raster{};
		raster.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
		raster.polygonMode = VK_POLYGON_MODE_FILL;
		raster.cullMode = VK_CULL_MODE_NONE;
		raster.lineWidth = 1.0f;
		VkPipelineMultisampleStateCreateInfo samples{};
		samples.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
		samples.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
		VkPipelineDepthStencilStateCreateInfo depth{};
		depth.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
		depth.depthTestEnable = VK_TRUE;
		VkPipelineColorBlendAttachmentState write{};
		/* Alpha reads back as all ones after the clear */
		write.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT;
		if (draw.blend) {
			write.blendEnable = VK_TRUE;
			write.srcColorBlendFactor = SRC[draw.src & 7];
			write.dstColorBlendFactor = DST[draw.dst & 7];
			write.srcAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
			write.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
		}
		VkPipelineColorBlendStateCreateInfo blending{};
		blending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
		blending.attachmentCount = 1;
		blending.pAttachments = &write;
		const VkDynamicState states[4] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR,
			                           VK_DYNAMIC_STATE_DEPTH_WRITE_ENABLE, VK_DYNAMIC_STATE_DEPTH_COMPARE_OP };
		const VkPipelineDynamicStateCreateInfo dynamic{ VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO, nullptr, 0, 4, states };
		const VkPipelineRenderingCreateInfo rendering{ VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO, nullptr, 0, 1, &colour,
			                                       card.depth_format(), VK_FORMAT_UNDEFINED };
		VkGraphicsPipelineCreateInfo info{};
		info.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
		info.pNext = &rendering;
		info.stageCount = draw.native ? 3 : 2;
		info.pStages = stages;
		info.pVertexInputState = &input;
		info.pInputAssemblyState = &assembly;
		info.pViewportState = &viewport;
		info.pRasterizationState = &raster;
		info.pMultisampleState = &samples;
		info.pDepthStencilState = &depth;
		info.pColorBlendState = &blending;
		info.pDynamicState = &dynamic;
		info.layout = layout;

		VkPipeline made;

		card.check(fn.vkCreateGraphicsPipelines(card.device(), VK_NULL_HANDLE, 1, &info, nullptr, &made), "a pipeline of the Neon 250");
		pipelines.emplace(key, made);
		return made;
	}

	VkSampler sampler(const PmxDraw &draw, unsigned anisotropy)
	{
		const uint32_t key = static_cast<uint32_t>(draw.wrap_u) | static_cast<uint32_t>(draw.wrap_v) << 2 |
		                     uint32_t{ draw.smooth } << 4 | uint32_t{ draw.mipmap } << 5 | anisotropy << 6;
		const auto found = samplers.find(key);

		if (found != samplers.end())
			return found->second;

		const auto wrap = [](PmxWrap how) {
			return how == PmxWrap::CLAMP ? VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE :
			       how == PmxWrap::MIRROR ? VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT : VK_SAMPLER_ADDRESS_MODE_REPEAT;
		};
		const float most = std::min(static_cast<float>(anisotropy), card.max_anisotropy());
		VkSamplerCreateInfo info{};

		info.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
		info.magFilter = info.minFilter = draw.smooth ? VK_FILTER_LINEAR : VK_FILTER_NEAREST;
		info.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
		info.addressModeU = wrap(draw.wrap_u);
		info.addressModeV = wrap(draw.wrap_v);
		info.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
		info.anisotropyEnable = most > 1.0f && draw.mipmap ? VK_TRUE : VK_FALSE;
		info.maxAnisotropy = std::max(most, 1.0f);
		info.maxLod = draw.mipmap ? VK_LOD_CLAMP_NONE : 0.0f;

		VkSampler made;

		card.check(fn.vkCreateSampler(card.device(), &info, nullptr, &made), "a sampler of the Neon 250");
		samplers.emplace(key, made);
		sampler_number[made] = static_cast<uint32_t>(samplers.size());
		return made;
	}
};

void
VkDrawing::draw_pmx(const PmxScene &scene, unsigned anisotropy)
{
	if (!m_pmx)
		m_pmx = std::make_shared<Pmx>(m_card, m_fn);

	Pmx &pmx = *m_pmx;
	const VkCommandBuffer cb = m_card.commands();

	pmx.scenes++;
	pmx.take_back_sets();

	for (const uint32_t id : scene.gone_images)
		if (const auto found = pmx.images.find(id); found != pmx.images.end()) {
			pmx.forget_sets(id);
			m_card.later([card = &m_card, image = found->second]() mutable { card->destroy_image(image); });
			pmx.images.erase(found);
		}
	for (const std::shared_ptr<const PmxImage> &image : scene.new_images) {
		const unsigned levels = static_cast<unsigned>(image->levels.size());
		vk::Image made = m_card.make_image(VK_FORMAT_R8G8B8A8_UNORM, image->width, image->height, levels,
		                                   VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT);

		m_card.barrier(cb, made, vk::COPIED_TO);
		for (unsigned level = 0; level < levels; level++) {
			const unsigned w = std::max(image->width >> level, 1u), h = std::max(image->height >> level, 1u);
			const VkDeviceSize bytes = VkDeviceSize{ w } * h * 4;
			const vk::Slice in = m_card.stage(bytes, 4);

			std::memcpy(in.bytes, image->levels[level].data(), bytes);

			const VkBufferImageCopy region{ in.offset, 0, 0, { VK_IMAGE_ASPECT_COLOR_BIT, level, 0, 1 }, { 0, 0, 0 }, { w, h, 1 } };

			m_fn.vkCmdCopyBufferToImage(cb, in.buffer, made.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
		}
		m_card.barrier(cb, made, vk::SAMPLED);
		pmx.images[image->id] = made;
	}

	const VkDeviceSize vertex_bytes = std::max<size_t>(scene.vertices.size(), 1) * sizeof(PmxVertex);
	const vk::Slice vertices = m_card.stage(vertex_bytes, 16);

	if (!scene.vertices.empty())
		std::memcpy(vertices.bytes, scene.vertices.data(), scene.vertices.size() * sizeof(PmxVertex));

	m_card.barrier(cb, m_scene, vk::DRAWN_IN);
	m_card.barrier(cb, m_depth, vk::DEPTH_IN);

	VkRenderingAttachmentInfo colour{};
	colour.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
	colour.imageView = m_scene.view;
	colour.imageLayout = m_scene.layout;
	colour.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
	colour.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
	colour.clearValue.color = { { 0.0f, 0.0f, 0.0f, 1.0f } };
	VkRenderingAttachmentInfo depth{};
	depth.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
	depth.imageView = m_depth.view;
	depth.imageLayout = m_depth.layout;
	depth.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
	depth.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
	depth.clearValue.depthStencil = { 0.0f, 0 };
	const VkRect2D area{ { 0, 0 }, { width(), height() } };
	VkRenderingInfo rendering{};
	rendering.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
	rendering.renderArea = area;
	rendering.layerCount = 1;
	rendering.colorAttachmentCount = 1;
	rendering.pColorAttachments = &colour;
	rendering.pDepthAttachment = &depth;
	m_fn.vkCmdBeginRendering(cb, &rendering);

	const VkViewport viewport{ 0.0f, 0.0f, static_cast<float>(width()), static_cast<float>(height()), 0.0f, 1.0f };

	m_fn.vkCmdSetViewport(cb, 0, 1, &viewport);
	m_fn.vkCmdSetScissor(cb, 0, 1, &area);
	m_fn.vkCmdBindVertexBuffers(cb, 0, 1, &vertices.buffer, &vertices.offset);

	VkPipeline bound = VK_NULL_HANDLE;
	VkDescriptorSet bound_set = VK_NULL_HANDLE;

	for (const PmxDraw &draw : scene.draws) {
		const VkPipeline pipeline = pmx.pipeline(draw, PICTURE_FORMAT);

		if (pipeline != bound) {
			m_fn.vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
			bound = pipeline;
		}
		m_fn.vkCmdSetDepthCompareOp(cb, COMPARE[draw.compare & 7]);
		m_fn.vkCmdSetDepthWriteEnable(cb, draw.write_z ? VK_TRUE : VK_FALSE);

		/* One set per image and sampler, cached with them */
		const auto found = draw.image ? pmx.images.find(draw.image) : pmx.images.end();
		const vk::Image &image = found != pmx.images.end() ? found->second : m_none;
		const VkSampler sampler = pmx.sampler(draw, anisotropy);
		const uint64_t key = uint64_t{ found != pmx.images.end() ? draw.image : 0u } << 32 |
		                     pmx.sampler_number[sampler];
		auto kept = pmx.sets.find(key);

		if (kept == pmx.sets.end()) {
			const VkDescriptorSet set = pmx.make_set(image, sampler);

			kept = pmx.sets.emplace(key, set).first;
		}
		if (kept->second != bound_set) {
			bound_set = kept->second;
			m_fn.vkCmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, pmx.layout, 0, 1, &bound_set, 0, nullptr);
		}

		const DrawPush push{ scene.most_w, draw.lod_bias,
			             (draw.image ? 1 : 0) | (draw.use_alpha ? 2 : 0) | (draw.ignore_texture_alpha ? 4 : 0) |
			                     (draw.offset ? 8 : 0) | draw.shading << 4, static_cast<float>(m_scale) };

		m_fn.vkCmdPushConstants(cb, pmx.layout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(push), &push);
		m_fn.vkCmdDraw(cb, draw.count, 1, draw.first, 0);
	}
	m_card.end_drawing(cb);

	const VkImageCopy copy{ { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 }, { 0, 0, 0 }, { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 },
		                { 0, 0, 0 }, { width(), height(), 1 } };

	m_card.barrier(cb, m_scene, vk::COPIED_FROM);
	m_card.barrier(cb, m_frame, vk::COPIED_TO);
	m_fn.vkCmdCopyImage(cb, m_scene.image, m_scene.layout, m_frame.image, m_frame.layout, 1, &copy);
}

} /* namespace odin */
