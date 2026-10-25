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

/* The graphics card of the host with OpenGL (host_graphics.hpp). */

#include "odin/host_graphics.hpp"

#include <algorithm>
#include <array>
#include <cmath>

#include <GL/glew.h>
#include <SDL3/SDL.h>

#include "imgui/imgui.h"
#include "imgui/imgui_impl_opengl3.h"
#include "odin/renderer_gl.hpp"

namespace odin {

namespace {

/* An image over the viewport it is given (the bezel, the sides blurred): a
 * triangle that covers it, the top of the image at the top */
const char *const IMAGE_VERTEX = R"glsl(#version 430

out vec2 place;

void main (void) {
	const vec2 corner = vec2 (gl_VertexID & 1, gl_VertexID >> 1) * 2.0;

	place = vec2 (corner.x, 1.0 - corner.y);
	gl_Position = vec4 (corner * 2.0 - 1.0, 0.0, 1.0);
}
)glsl";

const char *const IMAGE_FRAGMENT = R"glsl(#version 430

uniform sampler2D bezel;
uniform float light;
in vec2 place;
out vec4 colour;

void main (void) {
	colour = texture (bezel, place) * vec4 (light, light, light, 1.0);
}
)glsl";

/* The sides blurred: the picture made that small in two steps (each pixel
 * of the smaller the mean of what it covers, nearly), then that much
 * dimmer */
constexpr GLsizei SIDES_W[2] = { 160, 40 }, SIDES_H[2] = { 120, 30 };
constexpr float SIDES_LIGHT = 0.45f;

/* The background of the menu: how far apart the taps of its blur are, in
 * texels of the half picture, and how many times it goes back and forth */
constexpr float BLUR_STEP = 1.5f;
constexpr unsigned BLUR_ROUNDS = 2;

const char WHOLE_WINDOW_VERTEX[] = R"(#version 430 core
out vec2 v_uv;
void main()
{
	vec2 p = vec2((gl_VertexID << 1) & 2, gl_VertexID & 2);
	v_uv = p;
	gl_Position = vec4(p * 2.0 - 1.0, 0.0, 1.0);
}
)";

/* Gauss on nine taps, sigma near 2 taps */
const char BLUR_FRAGMENT[] = R"(#version 430 core
in vec2 v_uv;
out vec4 o_colour;
uniform sampler2D u_picture;
uniform vec2 u_step;
const float WEIGHTS[5] = float[](0.2270270270, 0.1945945946, 0.1216216216, 0.0540540541, 0.0162162162);
void main()
{
	vec3 c = texture(u_picture, v_uv).rgb * WEIGHTS[0];
	for (int i = 1; i < 5; i++) {
		c += texture(u_picture, v_uv + u_step * float(i)).rgb * WEIGHTS[i];
		c += texture(u_picture, v_uv - u_step * float(i)).rgb * WEIGHTS[i];
	}
	o_colour = vec4(c, 1.0);
}
)";

const char SHOW_FRAGMENT[] = R"(#version 430 core
in vec2 v_uv;
out vec4 o_colour;
uniform sampler2D u_picture;
uniform float u_light;
void main()
{
	o_colour = vec4(texture(u_picture, v_uv).rgb * u_light, 1.0);
}
)";

class GlGraphics : public HostGraphics {
public:
	explicit GlGraphics(SDL_Window *window) : m_window(window) {}
	~GlGraphics() override;

	void clear_window(unsigned window_w, unsigned window_h) override;
	void show_window() override { SDL_GL_SwapWindow(m_window); }
	bool set_waits(bool wait) override { return SDL_GL_SetSwapInterval(wait ? 1 : 0); }
	void read(HostPicture picture, unsigned w, unsigned h, uint8_t *pixels) override;

	HostImage make_image(const SDL_Surface &rgba, bool smooth) override;
	void forget_image(HostImage image) override;

	void draw_bezel(HostImage bezel, unsigned window_w, unsigned window_h) override;
	void draw_blurred_sides(HostPicture picture, PictureRect rect, unsigned window_w, unsigned window_h) override;

	void blur_background(HostPicture picture) override;
	void blur_window(unsigned window_w, unsigned window_h) override;
	void draw_background(unsigned window_w, unsigned window_h, unsigned w, unsigned h, float light) override;
	void start_ui() override;
	void draw_ui(ImDrawData *data, unsigned window_w, unsigned window_h, const char *what) override;
	void stop_ui() override;
	uint64_t ui_image(HostImage image) const override { return image.id; }

private:
	/* The program of an image over a viewport, made when first drawn */
	bool image_program();
	/* The two textures of the background, at the size of the half
	 * picture; the background from that framebuffer and buffer, w by h */
	void size_background(unsigned w, unsigned h);
	void blur(GLuint fb, GLenum buffer, unsigned w, unsigned h);

	SDL_Window *m_window;
	GLuint m_image_program = 0, m_image_vao = 0;
	GLint m_image_light = 0;
	/* The sides blurred: the picture made smaller twice */
	GLuint m_sides_fb[2] = {}, m_sides_tex[2] = {};
	/* The background of the menu: the picture at half its size, blurred
	 * back and forth between two textures, the result in the first */
	bool m_ui = false;
	GLuint m_vao = 0, m_blur = 0, m_show = 0;
	GLint m_blur_step = 0, m_show_light = 0;
	std::array<GLuint, 2> m_blur_tex{}, m_blur_fb{};
	unsigned m_blur_w = 0, m_blur_h = 0;
};

GlGraphics::~GlGraphics()
{
	glDeleteFramebuffers(2, m_sides_fb);
	glDeleteTextures(2, m_sides_tex);
	glDeleteProgram(m_image_program);
	glDeleteVertexArrays(1, &m_image_vao);
	glDeleteFramebuffers(2, m_blur_fb.data());
	glDeleteTextures(2, m_blur_tex.data());
	glDeleteProgram(m_blur);
	glDeleteProgram(m_show);
	glDeleteVertexArrays(1, &m_vao);
}

void
GlGraphics::clear_window(unsigned window_w, unsigned window_h)
{
	glBindFramebuffer(GL_FRAMEBUFFER, 0);
	glDisable(GL_SCISSOR_TEST);
	glDisable(GL_DEPTH_TEST);
	glDisable(GL_BLEND);
	glViewport(0, 0, static_cast<GLsizei>(window_w), static_cast<GLsizei>(window_h));
	glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
	glClear(GL_COLOR_BUFFER_BIT);
}

void
GlGraphics::read(HostPicture picture, unsigned w, unsigned h, uint8_t *pixels)
{
	glBindFramebuffer(GL_READ_FRAMEBUFFER, picture.id);
	glReadBuffer(picture.id ? GL_COLOR_ATTACHMENT0 : GL_BACK);
	glPixelStorei(GL_PACK_ALIGNMENT, 1);
	glReadPixels(0, 0, static_cast<GLsizei>(w), static_cast<GLsizei>(h), GL_RGB, GL_UNSIGNED_BYTE, pixels);
	glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);
}

HostImage
GlGraphics::make_image(const SDL_Surface &rgba, bool smooth)
{
	GLuint texture = 0;

	glGenTextures(1, &texture);
	glBindTexture(GL_TEXTURE_2D, texture);
	glPixelStorei(GL_UNPACK_ROW_LENGTH, rgba.pitch / 4);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, rgba.w, rgba.h, 0, GL_RGBA, GL_UNSIGNED_BYTE, rgba.pixels);
	glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
	glGenerateMipmap(GL_TEXTURE_2D);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, smooth ? GL_LINEAR : GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
	glBindTexture(GL_TEXTURE_2D, 0);
	return { texture, static_cast<float>(rgba.w), static_cast<float>(rgba.h) };
}

void
GlGraphics::forget_image(HostImage image)
{
	if (image.id)
		glDeleteTextures(1, &image.id);
}

bool
GlGraphics::image_program()
{
	if (!m_image_program) {
		m_image_program = gl::compile_program(IMAGE_VERTEX, IMAGE_FRAGMENT);
		m_image_light = glGetUniformLocation(m_image_program, "light");
		glGenVertexArrays(1, &m_image_vao);
	}
	return m_image_program != 0;
}

void
GlGraphics::draw_bezel(HostImage bezel, unsigned window_w, unsigned window_h)
{
	const float ww = static_cast<float>(window_w), wh = static_cast<float>(window_h);

	if (!bezel.id || !image_program())
		return;

	const float k = std::min(ww / bezel.w, wh / bezel.h);
	const GLsizei w = static_cast<GLsizei>(std::lround(bezel.w * k)), h = static_cast<GLsizei>(std::lround(bezel.h * k));
	GLint viewport[4];

	glGetIntegerv(GL_VIEWPORT, viewport);
	glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
	glViewport((static_cast<GLsizei>(window_w) - w) / 2, (static_cast<GLsizei>(window_h) - h) / 2, w, h);
	glEnable(GL_BLEND);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	glUseProgram(m_image_program);
	glUniform1f(m_image_light, 1.0f);
	glActiveTexture(GL_TEXTURE0);
	glBindTexture(GL_TEXTURE_2D, bezel.id);
	glBindVertexArray(m_image_vao);
	glDrawArrays(GL_TRIANGLES, 0, 3);
	glBindVertexArray(0);
	glBindTexture(GL_TEXTURE_2D, 0);
	glUseProgram(0);
	glDisable(GL_BLEND);
	glViewport(viewport[0], viewport[1], viewport[2], viewport[3]);
}

void
GlGraphics::draw_blurred_sides(HostPicture picture, PictureRect rect, unsigned window_w, unsigned window_h)
{
	const GLint ww = static_cast<GLint>(window_w), wh = static_cast<GLint>(window_h);

	if (!image_program())
		return;
	if (!m_sides_fb[0]) {
		glGenFramebuffers(2, m_sides_fb);
		glGenTextures(2, m_sides_tex);
		for (unsigned i = 0; i < 2; i++) {
			glBindTexture(GL_TEXTURE_2D, m_sides_tex[i]);
			glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, SIDES_W[i], SIDES_H[i], 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
			glBindFramebuffer(GL_FRAMEBUFFER, m_sides_fb[i]);
			glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, m_sides_tex[i], 0);
		}
		glBindTexture(GL_TEXTURE_2D, 0);
	}

	/* The picture smaller, twice */
	GLint viewport[4];

	glGetIntegerv(GL_VIEWPORT, viewport);
	glBindFramebuffer(GL_READ_FRAMEBUFFER, picture.id);
	glReadBuffer(GL_COLOR_ATTACHMENT0);
	glBindFramebuffer(GL_DRAW_FRAMEBUFFER, m_sides_fb[0]);
	glBlitFramebuffer(0, 0, static_cast<GLint>(picture.w), static_cast<GLint>(picture.h), 0, 0, SIDES_W[0],
	                  SIDES_H[0], GL_COLOR_BUFFER_BIT, GL_LINEAR);
	glBindFramebuffer(GL_READ_FRAMEBUFFER, m_sides_fb[0]);
	glBindFramebuffer(GL_DRAW_FRAMEBUFFER, m_sides_fb[1]);
	glBlitFramebuffer(0, 0, SIDES_W[0], SIDES_H[0], 0, 0, SIDES_W[1], SIDES_H[1], GL_COLOR_BUFFER_BIT, GL_LINEAR);
	glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);
	glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);

	/* Over the whole window, the side of the picture that fills it, and
	 * drawn only around the picture */
	const double k = std::max(window_w / 4.0, window_h / 3.0);
	const GLsizei cw = static_cast<GLsizei>(std::lround(4.0 * k)), ch = static_cast<GLsizei>(std::lround(3.0 * k));
	const GLint px = static_cast<GLint>(rect.x), py = wh - static_cast<GLint>(rect.y + rect.h);
	const GLint pr = px + static_cast<GLint>(rect.w), pt = py + static_cast<GLint>(rect.h);
	const GLint sides[4][4] = { { 0, 0, px, wh }, { pr, 0, ww - pr, wh }, { px, 0, pr - px, py }, { px, pt, pr - px, wh - pt } };

	glViewport((ww - cw) / 2, (wh - ch) / 2, cw, ch);
	glUseProgram(m_image_program);
	glUniform1f(m_image_light, SIDES_LIGHT);
	glActiveTexture(GL_TEXTURE0);
	glBindTexture(GL_TEXTURE_2D, m_sides_tex[1]);
	glBindVertexArray(m_image_vao);
	glEnable(GL_SCISSOR_TEST);
	for (const auto &side : sides) {
		if (side[2] <= 0 || side[3] <= 0)
			continue;
		glScissor(side[0], side[1], side[2], side[3]);
		glDrawArrays(GL_TRIANGLES, 0, 3);
	}
	glDisable(GL_SCISSOR_TEST);
	glBindVertexArray(0);
	glBindTexture(GL_TEXTURE_2D, 0);
	glUseProgram(0);
	glViewport(viewport[0], viewport[1], viewport[2], viewport[3]);
}

void
GlGraphics::size_background(unsigned w, unsigned h)
{
	if (w == m_blur_w && h == m_blur_h)
		return;
	glDeleteFramebuffers(2, m_blur_fb.data());
	glDeleteTextures(2, m_blur_tex.data());
	glGenTextures(2, m_blur_tex.data());
	glGenFramebuffers(2, m_blur_fb.data());
	for (unsigned i = 0; i < 2; i++) {
		glBindTexture(GL_TEXTURE_2D, m_blur_tex[i]);
		glTexStorage2D(GL_TEXTURE_2D, 1, GL_RGBA8, static_cast<GLsizei>(w), static_cast<GLsizei>(h));
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
		glBindFramebuffer(GL_FRAMEBUFFER, m_blur_fb[i]);
		glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, m_blur_tex[i], 0);
	}
	glBindTexture(GL_TEXTURE_2D, 0);
	glBindFramebuffer(GL_FRAMEBUFFER, 0);
	m_blur_w = w;
	m_blur_h = h;
}

void
GlGraphics::blur(GLuint fb, GLenum buffer, unsigned w, unsigned h)
{
	const unsigned hw = w / 2, hh = h / 2;
	const GLint iw = static_cast<GLint>(hw), ih = static_cast<GLint>(hh);

	size_background(hw, hh);
	glDisable(GL_SCISSOR_TEST);
	glDisable(GL_DEPTH_TEST);
	glDisable(GL_BLEND);

	/* Half the size: each texel the mean of four pixels */
	glBindFramebuffer(GL_READ_FRAMEBUFFER, fb);
	glReadBuffer(buffer);
	glBindFramebuffer(GL_DRAW_FRAMEBUFFER, m_blur_fb[0]);
	glBlitFramebuffer(0, 0, static_cast<GLint>(w), static_cast<GLint>(h), 0, 0, iw, ih, GL_COLOR_BUFFER_BIT,
	                  GL_LINEAR);

	/* Across into the second texture, down back into the first */
	const auto blur = [this](unsigned from, float x, float y) {
		glBindFramebuffer(GL_FRAMEBUFFER, m_blur_fb[1 - from]);
		glBindTexture(GL_TEXTURE_2D, m_blur_tex[from]);
		glUniform2f(m_blur_step, x, y);
		glDrawArrays(GL_TRIANGLES, 0, 3);
	};
	glViewport(0, 0, iw, ih);
	glUseProgram(m_blur);
	glBindVertexArray(m_vao);
	glActiveTexture(GL_TEXTURE0);
	for (unsigned round = 0; round < BLUR_ROUNDS; round++) {
		blur(0, BLUR_STEP / static_cast<float>(hw), 0.0f);
		blur(1, 0.0f, BLUR_STEP / static_cast<float>(hh));
	}
	glBindTexture(GL_TEXTURE_2D, 0);
	glBindVertexArray(0);
	glUseProgram(0);
	glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

void
GlGraphics::blur_background(HostPicture picture)
{
	blur(picture.id, GL_COLOR_ATTACHMENT0, picture.w, picture.h);
}

void
GlGraphics::blur_window(unsigned window_w, unsigned window_h)
{
	blur(0, GL_BACK, window_w, window_h);
}

void
GlGraphics::draw_background(unsigned window_w, unsigned window_h, unsigned w, unsigned h, float light)
{
	const GLsizei gw = static_cast<GLsizei>(w), gh = static_cast<GLsizei>(h);

	glViewport((static_cast<GLint>(window_w) - gw) / 2, (static_cast<GLint>(window_h) - gh) / 2, gw, gh);
	glUseProgram(m_show);
	glUniform1f(m_show_light, light);
	glBindVertexArray(m_vao);
	glActiveTexture(GL_TEXTURE0);
	glBindTexture(GL_TEXTURE_2D, m_blur_tex[0]);
	glDrawArrays(GL_TRIANGLES, 0, 3);
	glBindTexture(GL_TEXTURE_2D, 0);
	glBindVertexArray(0);
	glUseProgram(0);
	glViewport(0, 0, static_cast<GLsizei>(window_w), static_cast<GLsizei>(window_h));
}

void
GlGraphics::start_ui()
{
	ImGui_ImplOpenGL3_Init("#version 430 core");

	glGenVertexArrays(1, &m_vao);
	m_blur = gl::compile_program(WHOLE_WINDOW_VERTEX, BLUR_FRAGMENT);
	m_blur_step = gl::uniform(m_blur, "u_step");
	m_show = gl::compile_program(WHOLE_WINDOW_VERTEX, SHOW_FRAGMENT);
	m_show_light = gl::uniform(m_show, "u_light");
	m_ui = true;
}

void
GlGraphics::draw_ui(ImDrawData *data, unsigned window_w, unsigned window_h, const char *what)
{
	glBindFramebuffer(GL_FRAMEBUFFER, 0);
	glViewport(0, 0, static_cast<GLsizei>(window_w), static_cast<GLsizei>(window_h));
	ImGui_ImplOpenGL3_NewFrame();
	ImGui_ImplOpenGL3_RenderDrawData(data);
	gl::check(what);
}

void
GlGraphics::stop_ui()
{
	if (m_ui)
		ImGui_ImplOpenGL3_Shutdown();
	m_ui = false;
}

} /* namespace */

std::unique_ptr<HostGraphics>
HostGraphics::make_gl(SDL_Window *window)
{
	return std::make_unique<GlGraphics>(window);
}

} /* namespace odin */
