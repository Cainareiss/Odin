/*
 * Odin
 * Copyright (C) 2026 Réda Chérif-Touil
 *
 * Valkyrie
 * Copyright (C) 2011-2014, Stefano Teso
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

#include "odin/renderer.hpp"
#include "odin/hikaru_gpu.hpp"
#include "odin/renderer_gl.hpp"
#include "odin/renderer_shaders.hpp"

#include <algorithm>
#include <cstdio>

namespace odin {

namespace {

constexpr float INV255 = 1.0f / 255.0f;

} /* namespace */

Renderer::Renderer(const Memory &memory, const Options &options)
	: m_gpu(nullptr), m_fb(memory.fb), m_options(options), m_list(nullptr),
	  m_textures(memory.texram), m_mesh_vao(0), m_mesh_vbo(0),
	  m_program(nullptr), m_variant_key(0), m_drawing(0), m_normal{},
	  m_scissor_vp_index(~0u), m_bound_tex_index(~0u), m_pass{}, m_again(false),
	  m_scale(1), m_num_lists(0), m_shown(2), m_next(-1), m_launched(0), m_last{},
	  m_layer_pixels(size_t{WIDTH} * HEIGHT), m_message{}
{
	for (ListTextures &textures : m_list_textures) {
		textures.list = nullptr;
		textures.ids.assign(ODIN_LIST_MAX_TEXHEADS, 0);
	}
	init_debug();

	build_compose();
	build_presenter();
	build_buffers();
	build_blender();
	build_lists();
	build_quad();

	glGenFramebuffers(2, m_copy_fb);
	m_state_picture = new_buffer_texture(GL_RGBA8, WIDTH, HEIGHT);
	glBindTexture(GL_TEXTURE_2D, 0);
	gl::check("building the renderer");
}

Renderer::~Renderer()
{
	glBindVertexArray(0);
	if (m_mesh_vao) {
		glDeleteVertexArrays(1, &m_mesh_vao);
		glDeleteBuffers(1, &m_mesh_vbo);
	}
	glDeleteVertexArrays(1, &m_quad_vao);
	glDeleteBuffers(1, &m_quad_vbo);

	destroy_buffers(true);
	destroy_lists();
	glDeleteFramebuffers(2, m_copy_fb);
	glDeleteTextures(1, &m_state_picture);

	glUseProgram(0);
	glDeleteProgram(m_blender);
	glDeleteProgram(m_compose);
	glDeleteProgram(m_presenter);
}

void
Renderer::build_blender()
{
	m_blender = gl::compile_program(shaders::WHOLE_PICTURE_VERTEX, shaders::BLEND_FRAGMENT);
	m_blender_rate = gl::uniform(m_blender, "u_rate");
	glUseProgram(m_blender);
	glUniform1i(gl::uniform(m_blender, "u_drawn"), 0);
	glUniform1i(gl::uniform(m_blender, "u_kept"), 1);
	glUseProgram(0);
}

/* The four corners of the window, for the composition. The shaders that
 * draw one triangle over the whole picture take no vertex: they are drawn
 * with this array bound as well. */
void
Renderer::build_quad()
{
	struct Corner {
		float position[3];
		float texcoords[2];
	};
	static const Corner corners[4] = {
		{ { 1.0f, 0.0f, 0.0f }, { 1.0f, 1.0f } },
		{ { 0.0f, 0.0f, 0.0f }, { 0.0f, 1.0f } },
		{ { 1.0f, 1.0f, 0.0f }, { 1.0f, 0.0f } },
		{ { 0.0f, 1.0f, 0.0f }, { 0.0f, 0.0f } },
	};

	glGenVertexArrays(1, &m_quad_vao);
	glBindVertexArray(m_quad_vao);
	glGenBuffers(1, &m_quad_vbo);
	glBindBuffer(GL_ARRAY_BUFFER, m_quad_vbo);
	glBufferData(GL_ARRAY_BUFFER, sizeof(corners), corners, GL_STATIC_DRAW);

	glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(Corner),
	                      reinterpret_cast<const void *>(offsetof(Corner, position)));
	glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(Corner),
	                      reinterpret_cast<const void *>(offsetof(Corner, texcoords)));
	glEnableVertexAttribArray(0);
	glEnableVertexAttribArray(1);
	glBindVertexArray(0);
}

void
Renderer::build_compose()
{
	m_compose = gl::compile_program(shaders::COMPOSE_VERTEX, shaders::COMPOSE_FRAGMENT);

	m_compose_locs.front = gl::uniform(m_compose, "u_front");
	m_compose_locs.back = gl::uniform(m_compose, "u_back");
	m_compose_locs.layer1 = gl::uniform(m_compose, "u_layer1");
	m_compose_locs.layer2 = gl::uniform(m_compose, "u_layer2");
	m_compose_locs.factor = gl::uniform(m_compose, "u_factor");
	m_compose_locs.mult_f = gl::uniform(m_compose, "u_mult_f");
	m_compose_locs.mult_b = gl::uniform(m_compose, "u_mult_b");
	m_compose_locs.mult_1 = gl::uniform(m_compose, "u_mult_1");
	m_compose_locs.mult_2 = gl::uniform(m_compose, "u_mult_2");
	m_compose_locs.keyed_1 = gl::uniform(m_compose, "u_keyed_1");
	m_compose_locs.keyed_2 = gl::uniform(m_compose, "u_keyed_2");
	m_compose_locs.keyed_3d = gl::uniform(m_compose, "u_keyed_3d");
	m_compose_locs.above_1 = gl::uniform(m_compose, "u_above_1");
	m_compose_locs.above_2 = gl::uniform(m_compose, "u_above_2");
	m_compose_locs.invert_f = gl::uniform(m_compose, "u_invert_f");
	m_compose_locs.invert_b = gl::uniform(m_compose, "u_invert_b");
	m_compose_locs.clear = gl::uniform(m_compose, "u_clear");
}

void
Renderer::build_presenter()
{
	m_presenter = gl::compile_program(shaders::WHOLE_PICTURE_VERTEX, shaders::PRESENT_FRAGMENT);
	m_presenter_rect = gl::uniform(m_presenter, "u_rect");
	glUseProgram(m_presenter);
	glUniform1i(gl::uniform(m_presenter, "u_picture"), 0);
	glUseProgram(0);
}

/* Nothing a program holds, nothing bound is of the list about to be
 * drawn. */
void
Renderer::forget_held()
{
	m_program = nullptr;
	m_drawing++;
	m_scissor_vp_index = ~0u;
	m_bound_tex_index = ~0u;
}

void
Renderer::set_2d_state()
{
	glDisable(GL_DEPTH_TEST);
	glDisable(GL_BLEND);
	glDisable(GL_SCISSOR_TEST);
	glViewport(0, 0, static_cast<GLsizei>(width()), static_cast<GLsizei>(height()));
	glUseProgram(m_compose);
}

void
Renderer::begin_frame(unsigned frame)
{
	/* The list drawn last is kept until its translucent fragments are
	 * counted, see check_last_list: this one is built aside. */
	m_list = m_gpu->antarctic().begin_list(m_list && m_last.list == m_list);
	std::fill(tex_ids().begin(), tex_ids().end(), 0);

	forget_held();
	update_debug(frame);
}

void
Renderer::end_frame(unsigned frame, unsigned window_w, unsigned window_h)
{
	/* The command processor may still be in the middle of a mesh: draw
	 * what it pushed so far and let it start a new mesh on the next
	 * vertex. */
	m_gpu->antarctic().end_open_mesh();

	show(frame);
	present(window_w, window_h);
	gl::check("a frame");

	if (logging()) {
		log(" ==== RENDSTATE STATISTICS ==== ");
		log("  vp  : %u", m_list->num_vps);
		log("  mv  : %u", m_list->num_mvs);
		log("  mat : %u", m_list->num_mats);
		log("  tex : %u", m_list->num_texs);
		log("  ls  : %u", m_list->num_lss);
	}
}

/* Draws the list and blends it into its picture, as m_last tells. */
void
Renderer::draw_list()
{
	glBindFramebuffer(GL_FRAMEBUFFER, m_scene_fb);
	draw_scene();
	set_2d_state();

	/* The blender output is what the 3D buffer of the list keeps: the
	 * other one then holds every earlier frame, fading by the rate at
	 * each frame. This is the motion blur of the cabinets (the stars of
	 * the PHARRIER logo scene trail behind them at 181 = 01C6). */
	if (m_last.back >= 0) {
		glBindFramebuffer(GL_FRAMEBUFFER, m_pipe_fb[m_last.slot]);
		glUseProgram(m_blender);
		glUniform1i(m_blender_rate, static_cast<GLint>(m_last.rate));

		glActiveTexture(GL_TEXTURE0 + 1);
		glBindTexture(GL_TEXTURE_2D, m_buffers[m_last.back]);
		glActiveTexture(GL_TEXTURE0 + 0);
		glBindTexture(GL_TEXTURE_2D, m_scene_color);

		glBindVertexArray(m_quad_vao);
		glDrawArrays(GL_TRIANGLES, 0, 3);
		glBindVertexArray(0);

		glUseProgram(m_compose);
	} else {
		glActiveTexture(GL_TEXTURE0 + 0);
		glBindTexture(GL_TEXTURE_2D, m_pipe[m_last.slot]);
		glCopyTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 0, 0,
		                    static_cast<GLsizei>(width()), static_cast<GLsizei>(height()));
	}

	glBindFramebuffer(GL_FRAMEBUFFER, 0);
	gl::check("a list");
}

/* The translucent fragments of the list drawn last were collected without
 * waiting for the graphics card to count them: the processor goes on with
 * the next frame while the card draws. The counts are read here, a frame
 * later, when the card is done. If the buffer of the fragments was too
 * small the list is drawn again, from what it was drawn from, before its
 * picture is shown or blended into the next one. Its 3D buffer takes the
 * picture then. */
void
Renderer::check_last_list()
{
	odin_display_list_t *const list = m_last.list, *const current = m_list;
	GLuint used = 0;

	if (!list)
		return;
	m_last.list = nullptr;

	for (unsigned vpi = 0; vpi < 8; vpi++) {
		GLuint count;

		if (!(m_last.views & (1u << vpi)))
			continue;
		glBindBuffer(GL_SHADER_STORAGE_BUFFER, m_lists.count[vpi]);
		glGetBufferSubData(GL_SHADER_STORAGE_BUFFER, 0, sizeof(GLuint), &count);
		used = std::max(used, count);
	}
	glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);

	if (used > m_lists.max && grow_lists(used)) {
		m_list = list;
		m_again = true;
		forget_held();
		draw_list();
		forget_held();
		m_again = false;
		m_list = current;
	}

	glCopyImageSubData(m_pipe[m_last.slot], GL_TEXTURE_2D, 0, 0, 0, 0,
	                   m_buffers[m_last.buffer], GL_TEXTURE_2D, 0, 0, 0, 0,
	                   static_cast<GLsizei>(width()), static_cast<GLsizei>(height()), 1);
	gl::check("keeping a picture");
}

/* The polygons reach the screen in two steps. The game libraries launch a
 * list only once the command processor has analysed the last one and the
 * display chip has plotted the one before (RACER @0C0118C0): a launch hands
 * the last list over to the display chip, which plots it; it shows at the
 * next frame. Nothing moves without a launch. The 781, the 181 and the clear
 * color act at once.
 *
 * On the RACER cabinet the SEGA logo stays two frames on the black of the
 * next scene, and the stars leave one frame before the text of the layers;
 * SGNASCAR keeps its white picture while it loads, with no list launched. */
void
Renderer::show(unsigned frame)
{
	const odin_fb_config_t &fb = m_gpu->antarctic().fb_config();
	const bool launched = m_launched != 0;

	std::printf("FRAMEBUFFER 781=%08X 181=%08X\n", fb.w781, fb.w181);

	m_launched = 0;

	check_last_list();
	m_textures.delete_dead();

	/* The picture plotted during the last frame. */
	if (m_next >= 0) {
		m_shown = static_cast<uint32_t>(m_next);
		m_next = -1;
	}

	/* Draw the list to the 3D buffer its 781 names and keep the picture;
	 * the one of the list before shows next. */
	if (launched) {
		const uint32_t n1 = odin_fb_num_1(&fb), n2 = odin_fb_num_2(&fb);
		const uint32_t rate = odin_fb_blend(&fb) ? odin_fb_alpha(&fb) : 0;
		const uint32_t num = m_num_lists;

		if (m_options.list_fp || m_options.list_dump >= 0)
			print_list_fingerprint(fb, frame);

		m_last.list = m_list;
		m_last.buffer = (n1 < 2) ? n1 : (n2 < 2) ? n2 : 0;
		/* A 3D buffer can be blended with what it holds itself, "two
		 * frames previous" in the patent: BRAVEFF does at rate FF
		 * (781 = 0505) and its screen stays black until the city
		 * fades in, as on the cabinet. */
		m_last.back = (rate > 0 && n1 < 2 && n2 < 2) ? static_cast<int>(n2) : -1;
		m_last.rate = rate;
		m_last.slot = num % 3;
		m_last.views = 0;

		/* Every list that has translucent fragments is then drawn
		 * again, and must come out the same. */
		if (m_options.lists_test)
			size_lists(1);

		draw_list();

		if (num > 0)
			m_next = static_cast<int32_t>((num - 1) % 3);
		m_num_lists = num + 1;

		/* A picture that is on the screen already is checked now. */
		if (m_last.slot == m_shown)
			check_last_list();
	}

	compose(fb);
}

/* The picture shown and the layers, composed into the picture of the frame.
 *
 * Only the unit 0 of the display chip is drawn (the unit 1 is probably the
 * second monitor), and only what the 781 and the 181 tell: the registers
 * of the display chip that set up the framebuffer are not used. */
void
Renderer::compose(const odin_fb_config_t &fb)
{
	const odin_layers_t &layers = m_gpu->eurasia().layers();
	const uint32_t n1 = odin_fb_num_1(&fb), n2 = odin_fb_num_2(&fb);
	const uint32_t rate = odin_fb_blend(&fb) ? odin_fb_alpha(&fb) : 0;
	GLuint layer1 = 0, layer2 = 0;
	float mult1 = 0.0f, mult2 = 0.0f;
	/* Two 3D buffers were blended when their list was drawn. */
	const float factor = (n1 < 2 && n2 < 2) ? 0.0f : static_cast<float>(rate) * (1.0f / 255);

	set_2d_state();
	glBindFramebuffer(GL_FRAMEBUFFER, m_frame_fb);

	if ((layers.layer[0][0].enabled || n1 == 2 || n2 == 2) && !m_debug[DEBUG_NO_LAYER1])
		layer1 = upload_layer(layers.layer[0][0], mult1);
	if ((layers.layer[0][1].enabled || n1 == 3 || n2 == 3) && !m_debug[DEBUG_NO_LAYER2])
		layer2 = upload_layer(layers.layer[0][1], mult2);

	/* The two blender inputs: 0 and 1 are the 3D, 2 and 3 the 2D layers
	 * (PHARRIER blends layer 2 in for its logo screens, 781 = 0704/0705). */
	const GLuint shown = m_pipe[m_shown];
	const GLuint f = (n1 < 2) ? shown : (n1 == 2) ? layer1 : layer2;
	const GLuint b = (n2 < 2) ? shown : (n2 == 2) ? layer1 : layer2;

	glActiveTexture(GL_TEXTURE0 + 0);
	glBindTexture(GL_TEXTURE_2D, f);
	glActiveTexture(GL_TEXTURE0 + 1);
	glBindTexture(GL_TEXTURE_2D, b);
	glActiveTexture(GL_TEXTURE0 + 2);
	glBindTexture(GL_TEXTURE_2D, (f != layer1 && b != layer1) ? layer1 : 0);
	glActiveTexture(GL_TEXTURE0 + 3);
	glBindTexture(GL_TEXTURE_2D, (f != layer2 && b != layer2) ? layer2 : 0);

	glUniform1i(m_compose_locs.front, 0);
	glUniform1i(m_compose_locs.back, 1);
	glUniform1i(m_compose_locs.layer1, 2);
	glUniform1i(m_compose_locs.layer2, 3);
	glUniform1f(m_compose_locs.factor, factor);
	/* Uniforms persist in the program: set them all, every frame. A
	 * layer that is not uploaded this frame has mult 0 and no texture. */
	glUniform1f(m_compose_locs.mult_f,
	            (f == layer1 && layer1) ? mult1 : (f == layer2 && layer2) ? mult2 : 1.0f);
	glUniform1f(m_compose_locs.mult_b,
	            (b == layer1 && layer1) ? mult1 : (b == layer2 && layer2) ? mult2 : 1.0f);
	glUniform1f(m_compose_locs.mult_1, (layer1 && f != layer1 && b != layer1) ? mult1 : 0.0f);
	glUniform1f(m_compose_locs.mult_2, (layer2 && f != layer2 && b != layer2) ? mult2 : 0.0f);
	glUniform1f(m_compose_locs.keyed_1, layers.layer[0][0].keyed ? 1.0f : 0.0f);
	glUniform1f(m_compose_locs.keyed_2, layers.layer[0][1].keyed ? 1.0f : 0.0f);
	glUniform1f(m_compose_locs.keyed_3d, layers.fb_keyed ? 1.0f : 0.0f);
	glUniform1f(m_compose_locs.above_1, layers.layer[0][0].above ? 1.0f : 0.0f);
	glUniform1f(m_compose_locs.above_2, layers.layer[0][1].above ? 1.0f : 0.0f);
	glUniform1i(m_compose_locs.invert_f, (n1 >= 2) ? 1 : 0);
	glUniform1i(m_compose_locs.invert_b, (n2 >= 2) ? 1 : 0);
	glUniform4f(m_compose_locs.clear,
	            static_cast<float>(fb.clear[0]) * INV255,
	            static_cast<float>(fb.clear[1]) * INV255,
	            static_cast<float>(fb.clear[2]) * INV255,
	            static_cast<float>(fb.clear[3]) * INV255);

	glBindVertexArray(m_quad_vao);
	glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
	glBindVertexArray(0);
	glUseProgram(0);
	glBindFramebuffer(GL_FRAMEBUFFER, 0);

	m_textures.bury(layer1);
	m_textures.bury(layer2);
}

/* The picture of the frame in the window: the largest rectangle of the
 * shape of the screen of the hardware that fits, in the middle, black
 * around. A rectangle of the size of the picture takes it pixel for pixel;
 * any other takes for each of its pixels the mean of what it covers. */
void
Renderer::present(unsigned window_w, unsigned window_h)
{
	unsigned w = window_w, h = window_w * HEIGHT / WIDTH;

	if (h > window_h) {
		h = window_h;
		w = window_h * WIDTH / HEIGHT;
	}
	if (w == 0 || h == 0)
		return;
	const GLint x = static_cast<GLint>((window_w - w) / 2);
	const GLint y = static_cast<GLint>((window_h - h) / 2);
	const GLint pw = static_cast<GLint>(width()), ph = static_cast<GLint>(height());

	glDisable(GL_SCISSOR_TEST);
	glDisable(GL_DEPTH_TEST);
	glDisable(GL_BLEND);
	if (w != window_w || h != window_h) {
		glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
		glClear(GL_COLOR_BUFFER_BIT);
	}

	if (w == width() && h == height()) {
		glBindFramebuffer(GL_READ_FRAMEBUFFER, m_frame_fb);
		glBlitFramebuffer(0, 0, pw, ph, x, y, x + pw, y + ph,
		                  GL_COLOR_BUFFER_BIT, GL_NEAREST);
		glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);
		return;
	}

	/* Where the rectangle starts, and the pixels of the picture one of
	 * its pixels covers each way */
	glViewport(x, y, static_cast<GLsizei>(w), static_cast<GLsizei>(h));
	glUseProgram(m_presenter);
	glUniform4f(m_presenter_rect, static_cast<float>(x), static_cast<float>(y),
	            static_cast<float>(pw) / static_cast<float>(w),
	            static_cast<float>(ph) / static_cast<float>(h));
	glActiveTexture(GL_TEXTURE0);
	glBindTexture(GL_TEXTURE_2D, m_frame_color);
	glBindVertexArray(m_quad_vao);
	glDrawArrays(GL_TRIANGLES, 0, 3);
	glBindVertexArray(0);
	glBindTexture(GL_TEXTURE_2D, 0);
	glUseProgram(0);
}

} /* namespace odin */
