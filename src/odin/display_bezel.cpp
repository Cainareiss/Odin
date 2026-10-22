/*
 * Odin
 * Copyright (C) 2026 Réda Chérif-Touil
 *
 * Valkyrie
 * Copyright (C) 2011-2013, Stefano Teso
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

/* What is around the picture of the game: a bezel laid over the window,
 * its hole found or read, or the game blurred at its sides. */

#include "odin/display.hpp"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <system_error>

#include <jansson.h>

#include "odin/renderer.hpp"
#include "odin/renderer_gl.hpp"

namespace odin {

namespace {

/* The hole a bezel leaves for the game: the pixels joined to its middle
 * that are not opaque (the glass of a screen often is half so), their
 * bounds; none when its middle is opaque */
constexpr uint8_t SEE_THROUGH = 250;

PictureHole
find_hole(const SDL_Surface &rgba)
{
	const int w = rgba.w, h = rgba.h;
	const auto *const pixels = static_cast<const uint8_t *>(rgba.pixels);
	const auto open = [&](int x, int y) {
		return pixels[static_cast<size_t>(y) * static_cast<size_t>(rgba.pitch) + static_cast<size_t>(x) * 4 + 3] < SEE_THROUGH;
	};
	std::vector<uint8_t> seen(static_cast<size_t>(w) * static_cast<size_t>(h));
	std::vector<std::pair<int, int>> next = { { w / 2, h / 2 } };
	int left = w, top = h, right = -1, bottom = -1;

	if (w < 2 || h < 2 || !open(w / 2, h / 2))
		return {};
	seen[static_cast<size_t>(h / 2) * static_cast<size_t>(w) + static_cast<size_t>(w / 2)] = 1;
	while (!next.empty()) {
		const auto [x, y] = next.back();

		next.pop_back();
		left = std::min(left, x);
		right = std::max(right, x);
		top = std::min(top, y);
		bottom = std::max(bottom, y);
		for (const auto [dx, dy] : { std::pair{ 1, 0 }, std::pair{ -1, 0 }, std::pair{ 0, 1 }, std::pair{ 0, -1 } }) {
			const int nx = x + dx, ny = y + dy;
			const size_t at = static_cast<size_t>(ny) * static_cast<size_t>(w) + static_cast<size_t>(nx);

			if (nx < 0 || ny < 0 || nx >= w || ny >= h || seen[at] || !open(nx, ny))
				continue;
			seen[at] = 1;
			next.push_back({ nx, ny });
		}
	}
	return { static_cast<double>(left) / w, static_cast<double>(top) / h, static_cast<double>(right - left + 1) / w,
		 static_cast<double>(bottom - top + 1) / h, static_cast<double>(w), static_cast<double>(h) };
}

/* The sides blurred: the picture made that small in two steps (each pixel
 * of the smaller the mean of what it covers, nearly), then that much
 * dimmer */
constexpr GLsizei BLUR_W[2] = { 160, 40 }, BLUR_H[2] = { 120, 30 };
constexpr float BLUR_LIGHT = 0.45f;

/* The hole as Batocera writes it beside its bezels (<bezel>.info): the
 * size the bezel is drawn for, and what is around the hole on each side,
 * in its pixels; none without such a file */
PictureHole
read_hole(const std::string &info, int bezel_w, int bezel_h)
{
	json_error_t error;
	json_t *const root = json_load_file(info.c_str(), 0, &error);
	const auto number = [root](const char *key) { return json_number_value(json_object_get(root, key)); };
	PictureHole hole;

	if (!root)
		return hole;

	const double w = number("width"), h = number("height");
	const double left = number("left"), top = number("top"), right = number("right"), bottom = number("bottom");

	if (w > 0.0 && h > 0.0 && left >= 0.0 && top >= 0.0 && right >= 0.0 && bottom >= 0.0 && left + right < w &&
	    top + bottom < h)
		hole = { left / w, top / h, (w - left - right) / w, (h - top - bottom) / h, static_cast<double>(bezel_w),
			 static_cast<double>(bezel_h) };
	else
		std::fprintf(stderr, "ERROR: bezel: '%s' gives no hole\n", info.c_str());
	json_decref(root);
	return hole;
}

/* The bezel over the viewport it is given: a triangle that covers it, the
 * top of the picture at the top */
const char *const BEZEL_VERTEX = R"glsl(#version 430

out vec2 place;

void main (void) {
	const vec2 corner = vec2 (gl_VertexID & 1, gl_VertexID >> 1) * 2.0;

	place = vec2 (corner.x, 1.0 - corner.y);
	gl_Position = vec4 (corner * 2.0 - 1.0, 0.0, 1.0);
}
)glsl";

const char *const BEZEL_FRAGMENT = R"glsl(#version 430

uniform sampler2D bezel;
uniform float light;
in vec2 place;
out vec4 colour;

void main (void) {
	colour = texture (bezel, place) * vec4 (light, light, light, 1.0);
}
)glsl";

} /* namespace */

void
Display::set_sides(Sides sides, const std::string &bezel)
{
	m_sides = sides;
	set_bezel(sides == Sides::BEZEL ? bezel : std::string());
}

void
Display::set_bezel(const std::string &path)
{
	SDL_Surface *const png = path.empty() ? nullptr : SDL_LoadPNG(path.c_str());
	SDL_Surface *const rgba = png ? SDL_ConvertSurface(png, SDL_PIXELFORMAT_RGBA32) : nullptr;

	SDL_DestroySurface(png);
	if (!path.empty() && !rgba)
		std::fprintf(stderr, "ERROR: bezel: '%s': %s\n", path.c_str(), SDL_GetError());
	/* Its hole: as its .info says, else where it is not opaque */
	if (rgba) {
		std::error_code error;
		const std::string info = std::filesystem::path(path).replace_extension(".info").string();

		m_hole = std::filesystem::exists(info, error) ? read_hole(info, rgba->w, rgba->h) : find_hole(*rgba);
	} else {
		m_hole = {};
	}
	run([this, rgba] { make_bezel(rgba); });
	SDL_DestroySurface(rgba);
}

void
Display::make_bezel(SDL_Surface *rgba)
{
	if (m_bezel)
		glDeleteTextures(1, &m_bezel);
	m_bezel = 0;
	if (!rgba)
		return;
	if (!m_bezel_program) {
		m_bezel_program = gl::compile_program(BEZEL_VERTEX, BEZEL_FRAGMENT);
		m_bezel_light = static_cast<GLuint>(glGetUniformLocation(m_bezel_program, "light"));
		glGenVertexArrays(1, &m_bezel_vao);
	}
	glGenTextures(1, &m_bezel);
	glBindTexture(GL_TEXTURE_2D, m_bezel);
	glPixelStorei(GL_UNPACK_ROW_LENGTH, rgba->pitch / 4);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, rgba->w, rgba->h, 0, GL_RGBA, GL_UNSIGNED_BYTE, rgba->pixels);
	glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
	glGenerateMipmap(GL_TEXTURE_2D);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
	glBindTexture(GL_TEXTURE_2D, 0);
	m_bezel_w = static_cast<float>(rgba->w);
	m_bezel_h = static_cast<float>(rgba->h);
}

void
Display::draw_bezel(unsigned window_w, unsigned window_h)
{
	const float ww = static_cast<float>(window_w), wh = static_cast<float>(window_h);

	if (!m_bezel || !m_bezel_program || !bezel_shown(window_w, window_h))
		return;

	const float k = std::min(ww / m_bezel_w, wh / m_bezel_h);
	const GLsizei w = static_cast<GLsizei>(std::lround(m_bezel_w * k)), h = static_cast<GLsizei>(std::lround(m_bezel_h * k));
	GLint viewport[4];

	glGetIntegerv(GL_VIEWPORT, viewport);
	glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
	glViewport((static_cast<GLsizei>(window_w) - w) / 2, (static_cast<GLsizei>(window_h) - h) / 2, w, h);
	glEnable(GL_BLEND);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	glUseProgram(m_bezel_program);
	glUniform1f(static_cast<GLint>(m_bezel_light), 1.0f);
	glActiveTexture(GL_TEXTURE0);
	glBindTexture(GL_TEXTURE_2D, m_bezel);
	glBindVertexArray(m_bezel_vao);
	glDrawArrays(GL_TRIANGLES, 0, 3);
	glBindVertexArray(0);
	glBindTexture(GL_TEXTURE_2D, 0);
	glUseProgram(0);
	glDisable(GL_BLEND);
	glViewport(viewport[0], viewport[1], viewport[2], viewport[3]);
}

void
Display::draw_blur(unsigned window_w, unsigned window_h)
{
	const PictureRect rect = picture_rect(window_w, window_h);
	const GLint ww = static_cast<GLint>(window_w), wh = static_cast<GLint>(window_h);

	if (m_sides != Sides::BLUR || !m_picture_fb || (rect.w == window_w && rect.h == window_h))
		return;
	if (!m_bezel_program) {
		m_bezel_program = gl::compile_program(BEZEL_VERTEX, BEZEL_FRAGMENT);
		m_bezel_light = static_cast<GLuint>(glGetUniformLocation(m_bezel_program, "light"));
		glGenVertexArrays(1, &m_bezel_vao);
	}
	if (!m_blur_fb[0]) {
		glGenFramebuffers(2, m_blur_fb);
		glGenTextures(2, m_blur_tex);
		for (unsigned i = 0; i < 2; i++) {
			glBindTexture(GL_TEXTURE_2D, m_blur_tex[i]);
			glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, BLUR_W[i], BLUR_H[i], 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
			glBindFramebuffer(GL_FRAMEBUFFER, m_blur_fb[i]);
			glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, m_blur_tex[i], 0);
		}
		glBindTexture(GL_TEXTURE_2D, 0);
	}

	/* The picture smaller, twice */
	GLint viewport[4];

	glGetIntegerv(GL_VIEWPORT, viewport);
	glBindFramebuffer(GL_READ_FRAMEBUFFER, m_picture_fb);
	glReadBuffer(GL_COLOR_ATTACHMENT0);
	glBindFramebuffer(GL_DRAW_FRAMEBUFFER, m_blur_fb[0]);
	glBlitFramebuffer(0, 0, static_cast<GLint>(m_picture_width), static_cast<GLint>(m_picture_height), 0, 0, BLUR_W[0],
	                  BLUR_H[0], GL_COLOR_BUFFER_BIT, GL_LINEAR);
	glBindFramebuffer(GL_READ_FRAMEBUFFER, m_blur_fb[0]);
	glBindFramebuffer(GL_DRAW_FRAMEBUFFER, m_blur_fb[1]);
	glBlitFramebuffer(0, 0, BLUR_W[0], BLUR_H[0], 0, 0, BLUR_W[1], BLUR_H[1], GL_COLOR_BUFFER_BIT, GL_LINEAR);
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
	glUseProgram(m_bezel_program);
	glUniform1f(static_cast<GLint>(m_bezel_light), BLUR_LIGHT);
	glActiveTexture(GL_TEXTURE0);
	glBindTexture(GL_TEXTURE_2D, m_blur_tex[1]);
	glBindVertexArray(m_bezel_vao);
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

} /* namespace odin */
