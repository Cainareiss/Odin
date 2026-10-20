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

#include "odin/menu.hpp"

#include <algorithm>
#include <utility>

#include "imgui/imgui.h"
#include "imgui/imgui_impl_opengl3.h"
#include "imgui/imgui_impl_sdl3.h"
#include "odin/picture.hpp"
#include "odin/renderer_gl.hpp"

namespace odin {

namespace {

/* The hints of the mouse Dear ImGui sets for itself (imgui_impl_sdl3.cpp):
 * a click that brings the window to the front is a click, and no capture
 * while a button is held. Odin keeps its own outside the menu. */
constexpr std::array<std::pair<const char *, const char *>, 2> MENU_HINTS = { {
	{ SDL_HINT_MOUSE_FOCUS_CLICKTHROUGH, "1" },
	{ SDL_HINT_MOUSE_AUTO_CAPTURE, "0" },
} };

/* Rúnasteinn, the palette by default: granite and the red ochre of painted
 * runestones. The frame lets the game show through, at a fifth. */
constexpr ImU32 FRAME = IM_COL32(0x2F, 0x2F, 0x2C, 0xCC);
constexpr ImU32 INK = IM_COL32(0xEE, 0xEA, 0xE2, 0xFF);
constexpr ImU32 DIM = IM_COL32(0xB0, 0xAB, 0xA0, 0xFF);
constexpr ImU32 FAINT = IM_COL32(0x83, 0x7F, 0x75, 0xFF);

/* The frame in the window: its margin, its largest size, the cut of its
 * corners at 45 degrees and its padding, in points */
constexpr float MARGIN = 16.0f;
constexpr float WIDEST = 1080.0f;
constexpr float HIGHEST = 760.0f;
constexpr float CUT = 8.0f;
constexpr float PADDING = 14.0f;

/* The blur: how far apart its taps are, in texels of the half picture,
 * how many times it goes back and forth, and how much light it keeps */
constexpr float BLUR_STEP = 1.5f;
constexpr unsigned BLUR_ROUNDS = 2;
constexpr float LIGHT = 0.55f;

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

/* The outline of a frame with its corners cut at 45 degrees */
std::array<ImVec2, 8>
cut_corners(ImVec2 a, ImVec2 b, float cut)
{
	return { { { a.x + cut, a.y }, { b.x - cut, a.y }, { b.x, a.y + cut }, { b.x, b.y - cut },
	           { b.x - cut, b.y }, { a.x + cut, b.y }, { a.x, b.y - cut }, { a.x, a.y + cut } } };
}

void
set_hint(const char *name, const std::optional<std::string> &value)
{
	if (value)
		SDL_SetHint(name, value->c_str());
	else
		SDL_ResetHint(name);
}

} /* namespace */

Menu::Menu(SDL_Window *window, const std::string &title)
	: m_window(window), m_title(title), m_imgui(nullptr), m_cursor_shown(true),
	  m_gl(false), m_vao(0), m_blur(0), m_show(0), m_blur_step(-1), m_show_light(-1),
	  m_blur_tex{}, m_blur_fb{}, m_blur_w(0), m_blur_h(0), m_frozen(false)
{
	for (size_t i = 0; i < MENU_HINTS.size(); i++)
		if (const char *const value = SDL_GetHint(MENU_HINTS[i].first))
			m_hints[i] = value;

	m_imgui = ImGui::CreateContext();
	ImGuiIO &io = ImGui::GetIO();
	/* Nothing written beside the game */
	io.IniFilename = nullptr;
	io.LogFilename = nullptr;

	ImGuiStyle &style = ImGui::GetStyle();
	style.WindowBorderSize = 0.0f;
	style.Colors[ImGuiCol_Text] = ImGui::ColorConvertU32ToFloat4(INK);
	style.Colors[ImGuiCol_TextDisabled] = ImGui::ColorConvertU32ToFloat4(FAINT);

	ImGui_ImplSDL3_InitForOpenGL(window, nullptr);
	/* The gamepads are Odin's (controls.hpp) */
	ImGui_ImplSDL3_SetGamepadMode(ImGui_ImplSDL3_GamepadMode_Manual, nullptr, 0);
	for (size_t i = 0; i < MENU_HINTS.size(); i++)
		set_hint(MENU_HINTS[i].first, m_hints[i]);
}

Menu::~Menu()
{
	ImGui::SetCurrentContext(m_imgui);
	ImGui_ImplSDL3_Shutdown();
	ImGui::DestroyContext(m_imgui);
}

void
Menu::open()
{
	for (const auto &[name, value] : MENU_HINTS)
		SDL_SetHint(name, value);
	m_cursor_shown = SDL_CursorVisible();
}

void
Menu::close()
{
	for (size_t i = 0; i < MENU_HINTS.size(); i++)
		set_hint(MENU_HINTS[i].first, m_hints[i]);
	SDL_CaptureMouse(false);
	if (SDL_TextInputActive(m_window))
		SDL_StopTextInput(m_window);
	if (m_cursor_shown)
		SDL_ShowCursor();
	else
		SDL_HideCursor();
}

void
Menu::event(const SDL_Event &event)
{
	ImGui::SetCurrentContext(m_imgui);
	ImGui_ImplSDL3_ProcessEvent(&event);
}

/* The frame, empty for now: ODIN and the game at the top, what the keys do
 * at the bottom */
void
Menu::build()
{
	ImGui::SetCurrentContext(m_imgui);
	ImGui_ImplSDL3_NewFrame();
	ImGui::NewFrame();

	const ImVec2 room = ImGui::GetIO().DisplaySize;
	const float w = std::max(0.0f, std::min(room.x - 2.0f * MARGIN, WIDEST));
	const float h = std::max(0.0f, std::min(room.y - 2.0f * MARGIN, HIGHEST));
	const ImVec2 a = { (room.x - w) / 2.0f, (room.y - h) / 2.0f }, b = { a.x + w, a.y + h };

	ImGui::SetNextWindowPos(a);
	ImGui::SetNextWindowSize({ w, h });
	ImGui::Begin("Odin", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
	             ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBackground);

	ImDrawList *const list = ImGui::GetWindowDrawList();
	const std::array<ImVec2, 8> outline = cut_corners(a, b, CUT);
	const float line = ImGui::GetTextLineHeight();
	const float head = a.y + PADDING + line + PADDING;

	list->AddConvexPolyFilled(outline.data(), static_cast<int>(outline.size()), FRAME);
	list->AddPolyline(outline.data(), static_cast<int>(outline.size()), FAINT, ImDrawFlags_Closed, 1.0f);
	list->AddText({ a.x + PADDING, a.y + PADDING }, INK, "ODIN");
	list->AddText({ a.x + PADDING + ImGui::CalcTextSize("ODIN").x + line, a.y + PADDING }, DIM, m_title.c_str());
	list->AddLine({ a.x, head }, { b.x, head }, FAINT, 1.0f);
	list->AddText({ a.x + PADDING, b.y - PADDING - line }, DIM, "Esc: back to the game    Ctrl+Q: quit");

	ImGui::End();
	ImGui::Render();
}

void
Menu::start_gl()
{
	ImGui_ImplOpenGL3_Init("#version 430 core");

	glGenVertexArrays(1, &m_vao);
	m_blur = gl::compile_program(WHOLE_WINDOW_VERTEX, BLUR_FRAGMENT);
	m_blur_step = gl::uniform(m_blur, "u_step");
	m_show = gl::compile_program(WHOLE_WINDOW_VERTEX, SHOW_FRAGMENT);
	m_show_light = gl::uniform(m_show, "u_light");
	m_gl = true;
}

/* The two textures of the blur, at the size of the half picture */
void
Menu::size_blur(unsigned w, unsigned h)
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
Menu::freeze(GLuint picture_fb, unsigned w, unsigned h)
{
	if (!m_gl)
		start_gl();
	m_frozen = picture_fb != 0 && w > 1 && h > 1;
	if (!m_frozen)
		return;

	const unsigned hw = w / 2, hh = h / 2;
	const GLint iw = static_cast<GLint>(hw), ih = static_cast<GLint>(hh);

	size_blur(hw, hh);
	glDisable(GL_SCISSOR_TEST);
	glDisable(GL_DEPTH_TEST);
	glDisable(GL_BLEND);

	/* Half the size: each texel the mean of four pixels */
	glBindFramebuffer(GL_READ_FRAMEBUFFER, picture_fb);
	glReadBuffer(GL_COLOR_ATTACHMENT0);
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
Menu::draw(unsigned window_w, unsigned window_h)
{
	glBindFramebuffer(GL_FRAMEBUFFER, 0);
	glDisable(GL_SCISSOR_TEST);
	glDisable(GL_DEPTH_TEST);
	glDisable(GL_BLEND);
	glViewport(0, 0, static_cast<GLsizei>(window_w), static_cast<GLsizei>(window_h));
	glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
	glClear(GL_COLOR_BUFFER_BIT);

	/* Where the picture of the game was */
	const PictureRect rect = picture_rect(window_w, window_h);
	if (m_frozen && rect.w && rect.h) {
		glViewport(static_cast<GLint>(rect.x), static_cast<GLint>(rect.y), static_cast<GLsizei>(rect.w),
		           static_cast<GLsizei>(rect.h));
		glUseProgram(m_show);
		glUniform1f(m_show_light, LIGHT);
		glBindVertexArray(m_vao);
		glActiveTexture(GL_TEXTURE0);
		glBindTexture(GL_TEXTURE_2D, m_blur_tex[0]);
		glDrawArrays(GL_TRIANGLES, 0, 3);
		glBindTexture(GL_TEXTURE_2D, 0);
		glBindVertexArray(0);
		glUseProgram(0);
		glViewport(0, 0, static_cast<GLsizei>(window_w), static_cast<GLsizei>(window_h));
	}

	ImGui::SetCurrentContext(m_imgui);
	ImGui_ImplOpenGL3_NewFrame();
	ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
	gl::check("the menu");
}

void
Menu::release()
{
	if (!m_gl)
		return;
	ImGui::SetCurrentContext(m_imgui);
	ImGui_ImplOpenGL3_Shutdown();
	glDeleteFramebuffers(2, m_blur_fb.data());
	glDeleteTextures(2, m_blur_tex.data());
	glDeleteProgram(m_blur);
	glDeleteProgram(m_show);
	glDeleteVertexArrays(1, &m_vao);
	m_gl = false;
}

} /* namespace odin */
