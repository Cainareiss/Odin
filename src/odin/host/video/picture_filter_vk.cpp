/*
 *  ██████╗ ██████╗ ██╗███╗   ██╗
 * ██╔═══██╗██╔══██╗██║████╗  ██║
 * ██║   ██║██║  ██║██║██╔██╗ ██║
 * ██║   ██║██║  ██║██║██║╚██╗██║
 * ╚██████╔╝██████╔╝██║██║ ╚████║
 *  ╚═════╝ ╚═════╝ ╚═╝╚═╝  ╚═══╝
 *
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

#include "odin/host/video/picture_filter_vk.hpp"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iterator>
#include <set>

namespace odin {

namespace {

/* What a module of SPIR-V tells by name (glslangValidator -V -R keeps the
 * names): the default block of its uniforms, its textures, its inputs, its
 * specialization constants */
struct Reflected {
	uint32_t block_binding = UINT32_MAX, block_size = 0;
	std::map<std::string, uint32_t> offsets, textures, inputs, constants;
};

std::string
spirv_string(const uint32_t *words, size_t count)
{
	std::string text;

	for (size_t i = 0; i < count; i++)
		for (unsigned b = 0; b < 4; b++) {
			const char c = static_cast<char>(words[i] >> (8 * b));

			if (!c)
				return text;
			text += c;
		}
	return text;
}

/* False when the words are not SPIR-V */
bool
reflect(const std::vector<uint32_t> &code, Reflected &out)
{
	enum : uint32_t {
		NAME = 5, MEMBER_NAME = 6, TYPE_INT = 21, TYPE_FLOAT = 22, TYPE_VECTOR = 23, TYPE_MATRIX = 24,
		TYPE_SAMPLED_IMAGE = 27, TYPE_ARRAY = 28, TYPE_STRUCT = 30, TYPE_POINTER = 32, CONSTANT = 43,
		SPEC_CONSTANT_TRUE = 48, SPEC_CONSTANT_FALSE = 49, SPEC_CONSTANT = 50, VARIABLE = 59, DECORATE = 71,
		MEMBER_DECORATE = 72
	};
	enum : uint32_t { SPEC_ID = 1, ARRAY_STRIDE = 6, LOCATION = 30, BINDING = 33, OFFSET = 35 };
	enum : uint32_t { UNIFORM_CONSTANT = 0, INPUT = 1, UNIFORM = 2 };
	std::map<uint32_t, std::string> names;
	std::map<std::pair<uint32_t, uint32_t>, std::string> member_names;
	std::map<std::pair<uint32_t, uint32_t>, uint32_t> member_offsets;
	std::map<uint32_t, uint32_t> bindings, locations, spec_ids, sizes, values, strides;
	std::map<uint32_t, std::vector<uint32_t>> structs;
	std::map<uint32_t, uint32_t> pointees;
	std::set<uint32_t> sampled, spec_constants;
	std::vector<std::array<uint32_t, 3>> variables;

	if (code.size() < 5 || code[0] != 0x07230203)
		return false;
	for (size_t at = 5; at < code.size();) {
		const uint32_t count = code[at] >> 16, op = code[at] & 0xFFFF;
		const uint32_t *w = &code[at + 1];

		if (count == 0 || at + count > code.size())
			return false;
		switch (op) {
		case NAME:
			names[w[0]] = spirv_string(w + 1, count - 2);
			break;
		case MEMBER_NAME:
			member_names[{ w[0], w[1] }] = spirv_string(w + 2, count - 3);
			break;
		case DECORATE:
			if (w[1] == BINDING)
				bindings[w[0]] = w[2];
			else if (w[1] == LOCATION)
				locations[w[0]] = w[2];
			else if (w[1] == SPEC_ID)
				spec_ids[w[0]] = w[2];
			else if (w[1] == ARRAY_STRIDE)
				strides[w[0]] = w[2];
			break;
		case MEMBER_DECORATE:
			if (w[2] == OFFSET)
				member_offsets[{ w[0], w[1] }] = w[3];
			break;
		case TYPE_INT:
		case TYPE_FLOAT:
			sizes[w[0]] = w[1] / 8;
			break;
		case TYPE_VECTOR:
			sizes[w[0]] = sizes[w[1]] * w[2];
			break;
		case TYPE_MATRIX:
			/* Its columns 16 bytes apart (std140) */
			sizes[w[0]] = 16 * w[2];
			break;
		case TYPE_ARRAY:
			sizes[w[0]] = strides[w[0]] * values[w[2]];
			break;
		case TYPE_SAMPLED_IMAGE:
			sampled.insert(w[0]);
			break;
		case TYPE_STRUCT:
			structs[w[0]].assign(w + 1, w + count - 1);
			break;
		case TYPE_POINTER:
			pointees[w[0]] = w[2];
			break;
		case CONSTANT:
			values[w[1]] = w[2];
			break;
		case SPEC_CONSTANT_TRUE:
		case SPEC_CONSTANT_FALSE:
		case SPEC_CONSTANT:
			spec_constants.insert(w[1]);
			break;
		case VARIABLE:
			variables.push_back({ w[0], w[1], w[2] });
			break;
		default:
			break;
		}
		at += count;
	}

	for (const auto &[type, id, storage] : variables) {
		const uint32_t pointee = pointees[type];

		if (storage == UNIFORM && names[pointee] == "gl_DefaultUniformBlock") {
			const std::vector<uint32_t> &members = structs[pointee];

			out.block_binding = bindings.contains(id) ? bindings[id] : 0;
			for (uint32_t m = 0; m < members.size(); m++) {
				const uint32_t offset = member_offsets[{ pointee, m }];
				const uint32_t size = sizes.contains(members[m]) ? sizes[members[m]] : 64;

				out.offsets[member_names[{ pointee, m }]] = offset;
				out.block_size = std::max(out.block_size, offset + size);
			}
			out.block_size = (out.block_size + 15) / 16 * 16;
		} else if (storage == UNIFORM_CONSTANT && sampled.contains(pointee) && bindings.contains(id)) {
			out.textures[names[id]] = bindings[id];
		} else if (storage == INPUT && locations.contains(id)) {
			out.inputs[names[id]] = locations[id];
		}
	}
	for (const uint32_t id : spec_constants)
		if (spec_ids.contains(id) && names.contains(id))
			out.constants[names[id]] = spec_ids[id];
	return true;
}

/* The words of a file of SPIR-V; none when it cannot be read */
std::vector<uint32_t>
read_spirv(const std::string &path)
{
	std::ifstream file(path, std::ios::binary);
	const std::string bytes(std::istreambuf_iterator<char>(file), {});
	std::vector<uint32_t> words(bytes.size() / 4);

	if (!file.eof() && !file)
		return {};
	std::memcpy(words.data(), bytes.data(), words.size() * 4);
	return words;
}

constexpr VkFormat PICTURE_FORMAT = VK_FORMAT_R8G8B8A8_UNORM;

/* A texture read by a pass, by either of its stages */
constexpr vk::Use READ = { VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
	                   VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT | VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
	                   VK_ACCESS_2_SHADER_SAMPLED_READ_BIT };

} /* namespace */

std::unique_ptr<VkPictureFilter>
VkPictureFilter::load(vk::Context &card, const std::string &preset)
{
	const std::optional<preset::Plan> plan = preset::read_plan(preset);

	if (!plan)
		return nullptr;

	std::unique_ptr<VkPictureFilter> filter(new VkPictureFilter(card));

	if (!filter->build(*plan))
		return nullptr;
	std::printf("filter: '%s', %zu passes\n", preset.c_str(), filter->m_passes.size());
	return filter;
}

bool
VkPictureFilter::build(const preset::Plan &plan)
{
	const VkFormatFeatureFlags drawn = VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT | VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT |
	                                   VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT;
	VkFormatProperties props;

	m_card.fn().vkGetPhysicalDeviceFormatProperties(m_card.gpu(), m_float, &props);
	if ((props.optimalTilingFeatures & drawn) != drawn) {
		std::fprintf(stderr, "filter: 32-bit floats cannot be filtered on this card: 16-bit floats instead\n");
		m_float = VK_FORMAT_R16G16B16A16_SFLOAT;
	}

	m_passes.resize(plan.passes.size());
	for (size_t i = 0; i < plan.passes.size(); i++) {
		const preset::Plan::Pass &from = plan.passes[i];
		Pass &pass = m_passes[i];

		pass.out.linear = from.linear;
		pass.out.is_float = from.is_float;
		pass.out.wrap = from.wrap == preset::Plan::Wrap::REPEAT ? VK_SAMPLER_ADDRESS_MODE_REPEAT :
		                from.wrap == preset::Plan::Wrap::MIRRORED ? VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT :
		                from.wrap == preset::Plan::Wrap::EDGE ? VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE :
		                                                         VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
		pass.scale_type = from.scale_type;
		pass.scale = from.scale;
	}
	m_passes.back().window = true;
	m_parameters = plan.parameters;
	for (size_t i = 0; i < m_passes.size(); i++)
		if (!build_pass(m_passes[i], plan.passes[i]))
			return false;
	return true;
}

/* Its two modules beside its GLSL, what they read, its pipeline: the
 * parameters of the preset its specialization constants */
bool
VkPictureFilter::build_pass(Pass &pass, const preset::Plan::Pass &from)
{
	const VkDevice device = m_card.device();
	static const char *const STAGES[2] = { ".vert.spv", ".frag.spv" };
	Reflected reflected[2];

	for (unsigned s = 0; s < 2; s++) {
		const std::vector<uint32_t> code = read_spirv(from.file + STAGES[s]);

		if (code.empty() || !reflect(code, reflected[s])) {
			std::fprintf(stderr, "ERROR: filter: no SPIR-V '%s%s' (made from its GLSL when Odin is built)\n",
			             from.file.c_str(), STAGES[s]);
			return false;
		}
		const VkShaderModuleCreateInfo info{ VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO, nullptr, 0,
		                                     code.size() * 4, code.data() };
		m_card.check(m_fn.vkCreateShaderModule(device, &info, nullptr, &pass.modules[s]), "a pass of a filter");
		pass.blocks[s].binding = reflected[s].block_binding;
		pass.blocks[s].size = reflected[s].block_size;
		pass.blocks[s].offsets = reflected[s].offsets;
		pass.textures.insert(reflected[s].textures.begin(), reflected[s].textures.end());
	}

	/* The two blocks, then the textures, each at its own binding */
	std::vector<VkDescriptorSetLayoutBinding> bindings;
	std::set<uint32_t> taken;
	const VkShaderStageFlags both = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
	for (unsigned s = 0; s < 2; s++)
		if (pass.blocks[s].binding != UINT32_MAX) {
			bindings.push_back({ pass.blocks[s].binding, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1,
			                     s ? VK_SHADER_STAGE_FRAGMENT_BIT : VK_SHADER_STAGE_VERTEX_BIT, nullptr });
			taken.insert(pass.blocks[s].binding);
		}
	for (const auto &[name, binding] : pass.textures) {
		if (!taken.insert(binding).second) {
			std::fprintf(stderr, "ERROR: filter: '%s' has two things at binding %u\n", from.file.c_str(), binding);
			return false;
		}
		bindings.push_back({ binding, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, both, nullptr });
	}
	const VkDescriptorSetLayoutCreateInfo set{ VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO, nullptr, 0,
	                                           static_cast<uint32_t>(bindings.size()), bindings.data() };
	m_card.check(m_fn.vkCreateDescriptorSetLayout(device, &set, nullptr, &pass.set), "a pass of a filter");
	const VkPipelineLayoutCreateInfo layout{ VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO, nullptr, 0, 1, &pass.set, 0,
	                                         nullptr };
	m_card.check(m_fn.vkCreatePipelineLayout(device, &layout, nullptr, &pass.layout), "a pass of a filter");

	/* The values of the preset, by the numbers the build gave them; 0
	 * for one it does not declare, as OpenGL leaves a uniform not set */
	std::vector<VkSpecializationMapEntry> entries;
	std::vector<float> values;
	std::set<uint32_t> given;
	for (unsigned s = 0; s < 2; s++)
		for (const auto &[name, id] : reflected[s].constants) {
			float value = 0.0f;

			if (!given.insert(id).second)
				continue;
			for (const auto &[parameter, preset_value] : m_parameters)
				if (parameter == name)
					value = preset_value;
			entries.push_back({ id, static_cast<uint32_t>(values.size() * sizeof(float)), sizeof(float) });
			values.push_back(value);
		}
	const VkSpecializationInfo constants{ static_cast<uint32_t>(entries.size()), entries.data(),
		                              values.size() * sizeof(float), values.data() };
	const VkPipelineShaderStageCreateInfo stages[2] = {
		{ VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0, VK_SHADER_STAGE_VERTEX_BIT, pass.modules[0],
		  "main", &constants },
		{ VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0, VK_SHADER_STAGE_FRAGMENT_BIT, pass.modules[1],
		  "main", &constants },
	};

	/* Four corners: VertexCoord, then TexCoord */
	const VkVertexInputBindingDescription corners{ 0, 4 * sizeof(float), VK_VERTEX_INPUT_RATE_VERTEX };
	std::vector<VkVertexInputAttributeDescription> attributes;
	if (const auto at = reflected[0].inputs.find("VertexCoord"); at != reflected[0].inputs.end())
		attributes.push_back({ at->second, 0, VK_FORMAT_R32G32_SFLOAT, 0 });
	if (const auto at = reflected[0].inputs.find("TexCoord"); at != reflected[0].inputs.end())
		attributes.push_back({ at->second, 0, VK_FORMAT_R32G32_SFLOAT, 2 * sizeof(float) });
	const VkPipelineVertexInputStateCreateInfo input{ VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
		                                          nullptr, 0, 1, &corners,
		                                          static_cast<uint32_t>(attributes.size()), attributes.data() };
	const VkPipelineInputAssemblyStateCreateInfo assembly{ VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
	                                                       nullptr, 0, VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP, VK_FALSE };
	const VkPipelineViewportStateCreateInfo viewport{ VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO, nullptr, 0, 1,
	                                                  nullptr, 1, nullptr };
	VkPipelineRasterizationStateCreateInfo raster{};
	raster.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
	raster.polygonMode = VK_POLYGON_MODE_FILL;
	raster.cullMode = VK_CULL_MODE_NONE;
	raster.lineWidth = 1.0f;
	VkPipelineMultisampleStateCreateInfo samples{};
	samples.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
	samples.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
	/* A picture keeps the alpha 1 it was cleared to (as OpenGL's): the
	 * window takes it all */
	VkPipelineColorBlendAttachmentState write{};
	write.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT;
	if (pass.window)
		write.colorWriteMask |= VK_COLOR_COMPONENT_A_BIT;
	VkPipelineColorBlendStateCreateInfo blend{};
	blend.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
	blend.attachmentCount = 1;
	blend.pAttachments = &write;
	const VkDynamicState states[2] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
	const VkPipelineDynamicStateCreateInfo dynamic{ VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO, nullptr, 0, 2,
	                                                states };
	const VkFormat format = pass.window || !pass.out.is_float ? PICTURE_FORMAT : m_float;
	const VkPipelineRenderingCreateInfo rendering{ VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO, nullptr, 0, 1,
	                                               &format, VK_FORMAT_UNDEFINED, VK_FORMAT_UNDEFINED };
	VkGraphicsPipelineCreateInfo info{};
	info.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
	info.pNext = &rendering;
	info.stageCount = 2;
	info.pStages = stages;
	info.pVertexInputState = &input;
	info.pInputAssemblyState = &assembly;
	info.pViewportState = &viewport;
	info.pRasterizationState = &raster;
	info.pMultisampleState = &samples;
	info.pColorBlendState = &blend;
	info.pDynamicState = &dynamic;
	info.layout = pass.layout;
	m_card.check(m_fn.vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &info, nullptr, &pass.pipeline),
	             "a pass of a filter");
	return true;
}

/* The card is done with them (the board drawing destroys the filter once
 * the frames that used it are done). */
VkPictureFilter::~VkPictureFilter()
{
	const VkDevice device = m_card.device();

	for (Pass &pass : m_passes) {
		m_fn.vkDestroyPipeline(device, pass.pipeline, nullptr);
		m_fn.vkDestroyPipelineLayout(device, pass.layout, nullptr);
		m_fn.vkDestroyDescriptorSetLayout(device, pass.set, nullptr);
		for (const VkShaderModule module : pass.modules)
			m_fn.vkDestroyShaderModule(device, module, nullptr);
		if (pass.out.image.image)
			m_card.destroy_image(pass.out.image);
	}
	if (m_frame.image.image)
		m_card.destroy_image(m_frame.image);
	for (const auto &[kind, sampler] : m_samplers)
		m_fn.vkDestroySampler(device, sampler, nullptr);
}

/* A texture of 86Box is RGB: here RGBA, its alpha kept at 1 (cleared so,
 * never written). The one before goes once the card is done. */
void
VkPictureFilter::make_picture(Picture &picture, unsigned w, unsigned h)
{
	if (picture.image.image) {
		vk::Image old = picture.image;

		m_card.later([card = &m_card, old]() mutable { card->destroy_image(old); });
	}
	picture.image = m_card.make_image(picture.is_float ? m_float : PICTURE_FORMAT, w, h, 1,
	                                  VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT |
	                                          VK_IMAGE_USAGE_TRANSFER_DST_BIT);
	clear(picture);
}

/* Black, its alpha at 1 */
void
VkPictureFilter::clear(Picture &picture)
{
	m_card.clear(m_card.commands(), picture.image, { { 0.0f, 0.0f, 0.0f, 1.0f } });
}

vk::Image &
VkPictureFilter::frame(unsigned w, unsigned h)
{
	if (m_frame_size[0] != w || m_frame_size[1] != h) {
		m_frame_size = { w, h };
		m_frame.size = { static_cast<float>(w), static_cast<float>(h) };
		m_frame.texture_size = preset::texture_of(m_frame.size);
		make_picture(m_frame, static_cast<unsigned>(m_frame.texture_size[0]),
		             static_cast<unsigned>(m_frame.texture_size[1]));
		m_window = {};
	}
	return m_frame.image;
}

/* The size of each pass for a window w by h, its texture and its corners;
 * the textures black again (a smaller picture leaves nothing of the larger
 * around it) */
void
VkPictureFilter::size(unsigned w, unsigned h)
{
	const std::array<float, 2> window = { static_cast<float>(w), static_cast<float>(h) };
	const Picture *in = &m_frame;

	m_window = { w, h };
	for (Pass &pass : m_passes) {
		pass.out.size = preset::pass_size(pass.scale_type, pass.scale, window, in->size);
		if (!pass.window) {
			const std::array<float, 2> texture_size = preset::texture_of(pass.out.size);

			if (texture_size != pass.out.texture_size || !pass.out.image.image) {
				pass.out.texture_size = texture_size;
				make_picture(pass.out, static_cast<unsigned>(texture_size[0]),
				             static_cast<unsigned>(texture_size[1]));
			} else {
				clear(pass.out);
			}
		}

		pass.corners = preset::corners(in->size, in->texture_size);
		in = &pass.out;
	}
}

VkSampler
VkPictureFilter::sampler(bool linear, VkSamplerAddressMode wrap)
{
	VkSampler &held = m_samplers[{ linear, wrap }];

	if (!held) {
		VkSamplerCreateInfo info{};

		info.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
		info.magFilter = info.minFilter = linear ? VK_FILTER_LINEAR : VK_FILTER_NEAREST;
		info.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
		info.addressModeU = info.addressModeV = info.addressModeW = wrap;
		info.borderColor = VK_BORDER_COLOR_FLOAT_TRANSPARENT_BLACK;
		m_card.check(m_fn.vkCreateSampler(m_card.device(), &info, nullptr, &held), "a sampler of a filter");
	}
	return held;
}

void
VkPictureFilter::draw(vk::Image &window, int x, int y, unsigned w, unsigned h, unsigned count)
{
	static constexpr float IDENTITY[16] = { 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1 };
	const VkCommandBuffer cb = m_card.commands();
	const VkDeviceSize align = m_card.limits().minUniformBufferOffsetAlignment;
	Picture *in = &m_frame;

	if (m_window[0] != w || m_window[1] != h)
		size(w, h);
	for (size_t i = 0; i < m_passes.size(); i++) {
		Pass &pass = m_passes[i];
		/* The last reads its input linear, as 86Box does with its own
		 * filter setting */
		const bool linear = pass.window || in->linear;
		std::vector<uint8_t> blocks[2];

		for (unsigned s = 0; s < 2; s++)
			blocks[s].assign(pass.blocks[s].size, 0);
		const auto put = [&](const std::string &name, const void *value, size_t bytes) {
			for (unsigned s = 0; s < 2; s++)
				if (const auto at = pass.blocks[s].offsets.find(name); at != pass.blocks[s].offsets.end() &&
				    at->second + bytes <= blocks[s].size())
					std::memcpy(blocks[s].data() + at->second, value, bytes);
		};
		const float output[2] = { static_cast<float>(w), static_cast<float>(h) };
		const int32_t frame = static_cast<int32_t>(count), direction = 1;

		put("InputSize", in->size.data(), sizeof(in->size));
		put("TextureSize", in->texture_size.data(), sizeof(in->texture_size));
		put("OutputSize", output, sizeof(output));
		put("FrameCount", &frame, sizeof(frame));
		put("FrameDirection", &direction, sizeof(direction));
		put("MVPMatrix", IDENTITY, sizeof(IDENTITY));
		for (const auto &[parameter, value] : m_parameters)
			put(parameter, &value, sizeof(value));
		if (i > 0) {
			put("OrigInputSize", m_frame.size.data(), sizeof(m_frame.size));
			put("OrigTextureSize", m_frame.texture_size.data(), sizeof(m_frame.texture_size));
			for (size_t k = 0; k < i; k++) {
				const Picture &read = k ? m_passes[k - 1].out : m_frame;
				const std::string before = "Pass" + std::to_string(k + 1);

				put(before + "TextureSize", read.texture_size.data(), sizeof(read.texture_size));
				put(before + "InputSize", read.size.data(), sizeof(read.size));
			}
		}

		/* What it reads: by name, else the input, as OpenGL's unit 0 */
		const VkDescriptorSet set = m_card.frame_set(pass.set);
		std::vector<VkDescriptorBufferInfo> buffers;
		std::vector<VkDescriptorImageInfo> images;
		std::vector<VkWriteDescriptorSet> writes;
		buffers.reserve(2);
		images.reserve(pass.textures.size());
		for (unsigned s = 0; s < 2; s++) {
			if (pass.blocks[s].binding == UINT32_MAX)
				continue;
			const vk::Slice slice = m_card.stage(std::max<size_t>(blocks[s].size(), 16), align);

			std::memcpy(slice.bytes, blocks[s].data(), blocks[s].size());
			buffers.push_back({ slice.buffer, slice.offset, std::max<VkDeviceSize>(blocks[s].size(), 16) });
			writes.push_back({ VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, set, pass.blocks[s].binding, 0, 1,
			                   VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, nullptr, &buffers.back(), nullptr });
		}
		for (const auto &[name, binding] : pass.textures) {
			Picture *picture = in;
			bool picture_linear = linear;

			if (name == "OrigTexture" && i > 0) {
				picture = &m_frame;
				picture_linear = m_frame.linear;
			} else if (constexpr std::string_view PASS = "Pass"; name.starts_with(PASS) && name.ends_with("Texture")) {
				const size_t n = static_cast<size_t>(std::atoi(name.c_str() + PASS.size()));

				if (n >= 1 && n <= i) {
					picture = &m_passes[n - 1].out;
					picture_linear = picture == in ? linear : picture->linear;
				}
			}
			vk::Image &image = picture->image;

			if (image.layout != READ.layout)
				m_card.barrier(cb, image, READ);
			images.push_back({ sampler(picture_linear, picture->wrap), image.view, image.layout });
			writes.push_back({ VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, set, binding, 0, 1,
			                   VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, &images.back(), nullptr, nullptr });
		}
		m_fn.vkUpdateDescriptorSets(m_card.device(), static_cast<uint32_t>(writes.size()), writes.data(), 0, nullptr);
		const vk::Slice corners = m_card.stage(sizeof(pass.corners), 16);
		std::memcpy(corners.bytes, pass.corners.data(), sizeof(pass.corners));

		if (pass.window) {
			/* The window has its top line first: the picture turned over */
			const VkViewport turned{ static_cast<float>(x), static_cast<float>(y) + static_cast<float>(h),
			                         static_cast<float>(w), -static_cast<float>(h), 0.0f, 1.0f };

			m_card.begin_drawing(cb, window, { { x, y }, { w, h } });
			m_fn.vkCmdSetViewport(cb, 0, 1, &turned);
		} else {
			/* No pass leaves a pixel of its picture undrawn: no clear */
			m_card.begin_drawing(cb, pass.out.image,
			                     { { 0, 0 },
			                       { static_cast<uint32_t>(pass.out.size[0]), static_cast<uint32_t>(pass.out.size[1]) } });
		}
		m_fn.vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, pass.pipeline);
		m_fn.vkCmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, pass.layout, 0, 1, &set, 0, nullptr);
		m_fn.vkCmdBindVertexBuffers(cb, 0, 1, &corners.buffer, &corners.offset);
		m_fn.vkCmdDraw(cb, 4, 1, 0, 0);
		m_card.end_drawing(cb);
		in = &pass.out;
	}
}

} /* namespace odin */
