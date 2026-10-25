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
#include "odin/env.hpp"
#include "odin/hikaru_gpu.hpp"
#include "odin/loading_screen.hpp"
#include "odin/picture.hpp"
#include "odin/renderer_gl.hpp"
#include "odin/renderer_shaders.hpp"

#include <algorithm>
#include <cstdio>

namespace odin {

Renderer::Renderer(const Memory &memory, const Options &options)
	: m_gpu(nullptr), m_fb(memory.fb), m_options(options), m_building(nullptr), m_list(nullptr),
	  m_frame{}, m_textures(memory.texram), m_times(options.times), m_mesh_vao(0), m_mesh_vbo(0), m_mesh_ibo(0),
	  m_program(nullptr), m_variant_key(0), m_drawing(0),
	  m_scissor_vp_index(~0u), m_bound_tex_index(~0u), m_unit0_texture(~0u), m_unit0_sampler(~0u),
	  m_blend{~0u, ~0u, ~0u, ~0u}, m_pass{}, m_again(false),
	  m_scale(1), m_num_lists(0), m_shown(2), m_next(-1), m_launched(0), m_last{},
	  m_layer_ram{std::vector<uint8_t>(size_t{WIDTH} * HEIGHT * 4),
	              std::vector<uint8_t>(size_t{WIDTH} * HEIGHT * 4)},
	  m_layer_changed{true, true}, m_layer_pixels(size_t{WIDTH} * HEIGHT), m_layer_tex{0, 0},
	  m_layer_format{~0u, ~0u}, m_message{}
{
	for (ListTextures &textures : m_list_textures) {
		textures.list = nullptr;
		textures.ids.assign(ODIN_LIST_MAX_TEXHEADS, 0);
	}
	init_debug();
	m_programs.keep_in(options.programs_made);

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
	m_times.tell();

	glBindVertexArray(0);
	if (m_mesh_vao) {
		glDeleteVertexArrays(1, &m_mesh_vao);
		glDeleteBuffers(1, &m_mesh_vbo);
		glDeleteBuffers(1, &m_mesh_ibo);
	}
	glDeleteVertexArrays(1, &m_quad_vao);
	glDeleteBuffers(1, &m_quad_vbo);

	destroy_buffers(true);
	destroy_lists();
	glDeleteFramebuffers(2, m_copy_fb);
	glDeleteTextures(1, &m_state_picture);
	glDeleteTextures(2, m_layer_tex);

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
Renderer::set_filter(const std::string &preset)
{
	if (preset == m_filter_preset)
		return;
	m_filter_preset = preset;
	m_filter = preset.empty() ? nullptr : PictureFilter::load(preset);
	m_filter_frames = 0;
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

/* Compiles the mesh programs the lists of the game name, those of Odin
 * then those of the player, and draws a triangle without area with each.
 * See MeshPrograms; on the thread that draws the frames. */
void
Renderer::prepare_programs(const std::function<void(unsigned, unsigned)> &progress)
{
	static const odin_vertex_body_t none[3] = {};
	std::vector<uint64_t> keys;

	for (const std::string *list : { &m_options.programs_seed, &m_options.programs_kept }) {
		if (list->empty())
			continue;
		for (const uint64_t key : MeshPrograms::read_list(*list))
			if (!m_programs.holds(key) && std::find(keys.begin(), keys.end(), key) == keys.end())
				keys.push_back(key);
	}

	const unsigned total = static_cast<unsigned>(keys.size());
	const uint64_t began = host_ns();

	if (total)
		progress(0, total);
	upload_vertices(none, 3, nullptr, 0);
	for (unsigned i = 0; i < total; i++) {
		/* Back from the loading screen, drawn in the window */
		glBindVertexArray(m_mesh_vao);
		glBindFramebuffer(GL_FRAMEBUFFER, m_scene_fb);
		m_programs.add(keys[i]);
		glDrawArrays(GL_TRIANGLES, 0, 3);
		progress(i + 1, total);
	}
	glUseProgram(0);
	if (total)
		std::printf("programs: %u ready in %.1f s, %u of them read back, %u compiled\n", total,
		            static_cast<double>(host_ns() - began) / 1e9, m_programs.read_back(), m_programs.compiled());
	if (!m_options.programs_kept.empty())
		m_programs.list_in(m_options.programs_kept);

	glBindFramebuffer(GL_FRAMEBUFFER, 0);
	glBindVertexArray(0);
	gl::check("preparing the programs");
}

void
Renderer::draw_loading(std::string_view title, unsigned done, unsigned total, unsigned window_w, unsigned window_h)
{
	const PictureRect rect = picture_rect(window_w, window_h);
	/* What the drawing of the frames finds is left as it was. */
	const GLboolean scissoring = glIsEnabled(GL_SCISSOR_TEST);
	GLint box[4];
	GLfloat color[4];

	glGetIntegerv(GL_SCISSOR_BOX, box);
	glGetFloatv(GL_COLOR_CLEAR_VALUE, color);
	glBindFramebuffer(GL_FRAMEBUFFER, 0);
	glDisable(GL_SCISSOR_TEST);
	glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
	glClear(GL_COLOR_BUFFER_BIT);
	glEnable(GL_SCISSOR_TEST);
	for (const LoadingRect &r : LoadingScreen::layout(title, done, total)) {
		const unsigned x0 = rect.x + r.x * rect.w / LoadingScreen::WIDTH;
		const unsigned x1 = rect.x + (r.x + r.w) * rect.w / LoadingScreen::WIDTH;
		const unsigned y0 = rect.y + r.y * rect.h / LoadingScreen::HEIGHT;
		const unsigned y1 = rect.y + (r.y + r.h) * rect.h / LoadingScreen::HEIGHT;

		if (x1 <= x0 || y1 <= y0)
			continue;
		glScissor(static_cast<GLint>(x0), static_cast<GLint>(window_h - y1), static_cast<GLsizei>(x1 - x0),
		          static_cast<GLsizei>(y1 - y0));
		glClearColor(r.shade, r.shade, r.shade, 1.0f);
		glClear(GL_COLOR_BUFFER_BIT);
	}
	if (!scissoring)
		glDisable(GL_SCISSOR_TEST);
	glScissor(box[0], box[1], box[2], box[3]);
	glClearColor(color[0], color[1], color[2], color[3]);
	gl::check("the loading screen");
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
	m_unit0_texture = m_unit0_sampler = ~0u;
	m_blend[0] = ~0u;
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
Renderer::begin_frame()
{
	/* The list drawn last is kept until its translucent fragments are
	 * counted, see check_last_list: the command processor leaves it. */
	m_building = m_gpu->antarctic().begin_list();
}

void
Renderer::take_frame(unsigned frame, unsigned window_w, unsigned window_h, const PictureHole &hole)
{
	/* The command processor may still be in the middle of a mesh: draw
	 * what it pushed so far and let it start a new mesh on the next
	 * vertex. */
	m_gpu->antarctic().end_open_mesh();

	m_list = m_building;
	m_frame.number = frame;
	m_frame.launched = m_launched != 0;
	m_launched = 0;
	m_frame.fb = m_gpu->antarctic().fb_config();
	m_frame.layers = m_gpu->eurasia().layers();
	m_frame.window_w = window_w;
	m_frame.window_h = window_h;
	m_frame.hole = hole;

	update_debug();
	for (unsigned i = 0; i < 2; i++)
		if (shows_layer(i))
			take_layer(i);

	/* With the fingerprints only: each frame on the output costs, and
	 * fills the log */
	if (m_options.list_fp)
		std::printf("FRAMEBUFFER 781=%08X 181=%08X\n", m_frame.fb.w781, m_frame.fb.w181);
	if (m_frame.launched && (m_options.list_fp || m_options.list_dump >= 0))
		print_list_fingerprint();
}

void
Renderer::draw_frame()
{
	m_times.begin();
	forget_held();
	show();
	m_times.part(DrawTimes::PART_PRESENT);
	present(m_frame.window_w, m_frame.window_h, m_frame.hole);
	gl::check("a frame");
	m_times.part(DrawTimes::PART_SHOWN);

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
	m_times.part(DrawTimes::PART_BLENDER);
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

/* The translucent fragments of a list are collected without waiting for the
 * graphics card to count them: the processor goes on with the next frame
 * while the card draws. The counts are read a frame later, once the next
 * list is sent (see show): the card has that one to draw meanwhile. True if
 * they did not fit, and the buffer of the fragments is larger now. */
bool
Renderer::too_many(Last &last)
{
	GLuint used = 0;

	m_times.part(DrawTimes::PART_LAST_LIST);
	if (last.copied) {
		GLenum waited;

		do
			waited = glClientWaitSync(last.copied, GL_SYNC_FLUSH_COMMANDS_BIT, 1000000000);
		while (waited == GL_TIMEOUT_EXPIRED);
		if (waited == GL_WAIT_FAILED)
			fatal("waiting for the count of the translucent fragments");
		glDeleteSync(last.copied);
		last.copied = nullptr;
	}
	for (unsigned vpi = 0; vpi < 8; vpi++) {
		const uint32_t at = last.slot * 8 + vpi;
		GLuint count;

		if (!(last.views & (1u << vpi)))
			continue;
		if (m_lists.counts) {
			count = m_lists.counts[at];
		} else {
			glBindBuffer(GL_COPY_READ_BUFFER, m_lists.readback);
			glGetBufferSubData(GL_COPY_READ_BUFFER, static_cast<GLintptr>(at * sizeof(GLuint)),
			                   sizeof(GLuint), &count);
			glBindBuffer(GL_COPY_READ_BUFFER, 0);
		}
		used = std::max(used, count);
	}
	m_times.fragments(used);
	return used > m_lists.max && grow_lists(used);
}

/* Draws a list again, from what it was drawn from. */
void
Renderer::draw_again(const Last &last)
{
	odin_display_list_t *const current = m_list;
	const Last drawn = m_last;

	m_list = last.list;
	m_last = last;
	m_again = true;
	forget_held();
	draw_list();
	forget_held();
	m_again = false;
	m_list = current;
	m_last = drawn;
	m_times.part(DrawTimes::PART_LAST_LIST);
}

/* The 3D buffer of a list takes its picture. */
void
Renderer::keep_picture(const Last &last)
{
	glCopyImageSubData(m_pipe[last.slot], GL_TEXTURE_2D, 0, 0, 0, 0,
	                   m_buffers[last.buffer], GL_TEXTURE_2D, 0, 0, 0, 0,
	                   static_cast<GLsizei>(width()), static_cast<GLsizei>(height()), 1);
	gl::check("keeping a picture");
}

/* The list drawn last, counted now: drawn again if its fragments did not
 * fit, before its picture is shown or blended into the next one. */
void
Renderer::check_last_list()
{
	Last last = m_last;

	if (!last.list)
		return;
	m_last.list = nullptr;
	m_last.copied = nullptr;
	if (too_many(last))
		draw_again(last);
	keep_picture(last);
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
 * SGNASCAR keeps its white picture while it loads, with no list launched.
 *
 * The list before is counted once this one is sent. Its picture goes to its
 * 3D buffer first, as when it was counted first: this one may blend with
 * it. If its fragments did not fit it is drawn again, and so is this one,
 * which blended with what it left. */
void
Renderer::show()
{
	const odin_fb_config_t &fb = m_frame.fb;
	Last before = m_last;

	m_times.part(DrawTimes::PART_LAST_LIST);
	m_last.list = nullptr;
	m_last.copied = nullptr;
	if (before.list)
		keep_picture(before);

	/* The picture plotted during the last frame. */
	if (m_next >= 0) {
		m_shown = static_cast<uint32_t>(m_next);
		m_next = -1;
	}

	/* Draw the list to the 3D buffer its 781 names and keep the picture;
	 * the one of the list before shows next. */
	if (m_frame.launched) {
		const uint32_t n1 = odin_fb_num_1(&fb), n2 = odin_fb_num_2(&fb);
		const uint32_t rate = odin_fb_blend(&fb) ? odin_fb_alpha(&fb) : 0;
		const uint32_t num = m_num_lists;

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

		std::fill(tex_ids().begin(), tex_ids().end(), 0);
		draw_list();

		if (num > 0)
			m_next = static_cast<int32_t>((num - 1) % 3);
		m_num_lists = num + 1;
	}

	if (before.list && too_many(before)) {
		draw_again(before);
		keep_picture(before);
		/* Drawn again, this one has waited for its own counts. */
		if (m_last.list) {
			if (m_last.copied)
				glDeleteSync(m_last.copied);
			m_last.copied = nullptr;
			draw_again(m_last);
			m_last.views = 0;
		}
	}
	/* What the list before was drawn with may go now. */
	m_textures.delete_dead();

	/* A picture that is on the screen already is checked now. */
	if (m_last.list && m_last.slot == m_shown)
		check_last_list();

	compose();
}

/* The picture shown and the layers, composed into the picture of the frame.
 *
 * Only the unit 0 of the display chip is drawn (the unit 1 is probably the
 * second monitor), and only what the 781 and the 181 tell: the registers
 * of the display chip that set up the framebuffer are not used. */
void
Renderer::compose()
{
	const odin_fb_config_t &fb = m_frame.fb;
	const odin_layers_t &layers = m_frame.layers;
	const uint32_t n1 = odin_fb_num_1(&fb), n2 = odin_fb_num_2(&fb);
	const uint32_t rate = odin_fb_blend(&fb) ? odin_fb_alpha(&fb) : 0;
	GLuint layer1 = 0, layer2 = 0;
	float mult1 = 0.0f, mult2 = 0.0f;
	/* Two 3D buffers were blended when their list was drawn. */
	const float factor = (n1 < 2 && n2 < 2) ? 0.0f : static_cast<float>(rate) * (1.0f / 255);

	m_times.part(DrawTimes::PART_LAYERS);
	set_2d_state();
	glBindFramebuffer(GL_FRAMEBUFFER, m_frame_fb);

	if (shows_layer(0))
		layer1 = upload_layer(0, mult1);
	if (shows_layer(1))
		layer2 = upload_layer(1, mult2);
	m_times.part(DrawTimes::PART_COMPOSE);

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
}

/* The picture of the frame in the window: the largest rectangle of the
 * shape of the screen of the hardware that fits, in the middle or in the
 * hole of the bezel, black around (picture_rect, which the mouse reads
 * too). A rectangle of the size of the picture takes it pixel for pixel;
 * any other takes for each of its pixels the mean of what it covers. */
void
Renderer::present(unsigned window_w, unsigned window_h, const PictureHole &hole)
{
	const PictureRect rect = picture_rect(window_w, window_h, hole);
	const unsigned w = rect.w, h = rect.h;

	if (w == 0 || h == 0 || rect.x + w > window_w || rect.y + h > window_h)
		return;
	/* OpenGL counts from the bottom */
	const GLint x = static_cast<GLint>(rect.x);
	const GLint y = static_cast<GLint>(window_h - rect.y - h);
	const GLint pw = static_cast<GLint>(width()), ph = static_cast<GLint>(height());

	glDisable(GL_SCISSOR_TEST);
	glDisable(GL_DEPTH_TEST);
	glDisable(GL_BLEND);
	if (w != window_w || h != window_h) {
		glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
		glClear(GL_COLOR_BUFFER_BIT);
	}

	/* A filter takes the picture at the size of the screen of the
	 * hardware, a finer one brought down to it as below */
	if (m_filter) {
		glBindFramebuffer(GL_FRAMEBUFFER, m_filter->frame(WIDTH, HEIGHT));
		glViewport(0, 0, WIDTH, HEIGHT);
		glUseProgram(m_presenter);
		glUniform4f(m_presenter_rect, 0.0f, 0.0f, static_cast<float>(m_scale), static_cast<float>(m_scale));
		glActiveTexture(GL_TEXTURE0);
		glBindTexture(GL_TEXTURE_2D, m_frame_color);
		glBindVertexArray(m_quad_vao);
		glDrawArrays(GL_TRIANGLES, 0, 3);
		glBindVertexArray(0);
		glBindTexture(GL_TEXTURE_2D, 0);
		glBindFramebuffer(GL_FRAMEBUFFER, 0);
		m_filter->draw(x, y, w, h, m_filter_frames++);
		return;
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
