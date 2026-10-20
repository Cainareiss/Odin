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
#include <cmath>
#include <utility>

#include <cstdio>

#include "imgui/imgui.h"
#include "imgui/imgui_impl_opengl3.h"
#include "imgui/imgui_impl_sdl3.h"
#include "odin/control_panel.hpp"
#include "odin/menu_kit.hpp"
#include "odin/picture.hpp"
#include "odin/renderer_gl.hpp"
#include "odin/version.hpp"

namespace odin {

namespace {

/* The hints of the mouse Dear ImGui sets for itself (imgui_impl_sdl3.cpp):
 * a click that brings the window to the front is a click, and no capture
 * while a button is held. Odin keeps its own outside the menu. */
constexpr std::array<std::pair<const char *, const char *>, 2> MENU_HINTS = { {
	{ SDL_HINT_MOUSE_FOCUS_CLICKTHROUGH, "1" },
	{ SDL_HINT_MOUSE_AUTO_CAPTURE, "0" },
} };

/* The menu takes the whole window, made for one of 640 by 480 points
 * (a window nearly square) or 960 by 540 (16:9 and wider), then larger by
 * one factor as the window grows (mock-up v50); its padding, in points */
constexpr float COMPACT_W = 640.0f;
constexpr float COMPACT_H = 480.0f;
constexpr float LARGE_W = 960.0f;
constexpr float LARGE_H = 540.0f;
constexpr float PADDING = 10.0f;
/* The line under the tabs: what it leaves at each end of the width, where
 * it fades in and out; ochre while a control of Input is touched */
constexpr float LINE_ENDS = 0.11f;
constexpr float LINE_FADE = 0.22f;
constexpr float LINE_HOT = 0.85f;

/* The tabs of the pages, in the order of Menu::Page */
constexpr const char *TABS[] = { "Input", "Display", "Audio", "Hotkeys", "Outputs", "Paths", "About" };
constexpr MenuKit::Icon TAB_ICONS[] = { MenuKit::Icon::INPUT, MenuKit::Icon::DISPLAY, MenuKit::Icon::AUDIO,
	                                MenuKit::Icon::HOTKEYS, MenuKit::Icon::OUTPUTS, MenuKit::Icon::FOLDER,
	                                MenuKit::Icon::ABOUT };
/* A window this many times wider than high takes the large menu; a
 * narrower one has the names of the tabs not chosen left out */
constexpr float WIDE = 1.5f;

/* The window at start, that many times the screen; its sizes shorter
 * where the row is narrow */
constexpr const char *WINDOW_SIZES[] = { "640 × 480", "1280 × 960", "1920 × 1440", "2560 × 1920" };
constexpr const char *WINDOW_WIDTHS[] = { "640", "1280", "1920", "2560" };
constexpr unsigned ANISOTROPY[] = { 1, 2, 4, 8, 16 };

/* The libraries Odin is made with: who wrote them, their licence */
constexpr const char *LICENCES[][3] = {
	{ "Odin", "Réda Chérif-Touil · Valkyrie, Stefano Teso", "GPL 3.0 or later" },
	{ "Dear ImGui", "Omar Cornut", "MIT" },
	{ "SDL 3", "Sam Lantinga", "zlib" },
	{ "GLEW", "Milan Ikits, Marcelo Magallon", "BSD / MIT" },
	{ "Jansson", "Petri Lehtinen", "MIT" },
	{ "Gamepad drawings", "AL2009man, Gamepad-Asset-Pack", "MIT" },
};

ImVec4
colour(ImU32 c)
{
	return ImGui::ColorConvertU32ToFloat4(c);
}

/* Which of the values is the one; -1 for none */
template <typename T, size_t N>
int
index_of(const T (&values)[N], T value)
{
	for (size_t i = 0; i < N; i++)
		if (values[i] == value)
			return static_cast<int>(i);
	return -1;
}

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

void
set_hint(const char *name, const std::optional<std::string> &value)
{
	if (value)
		SDL_SetHint(name, value->c_str());
	else
		SDL_ResetHint(name);
}

/* The line under the tabs, that thick, faded at its ends */
void
under_tabs(ImDrawList *list, float left, float right, float y, float thick, ImU32 colour)
{
	const ImU32 clear = MenuKit::alpha(colour, 0.0f);
	const float fade = (right - left) * LINE_FADE;

	list->AddRectFilledMultiColor({ left, y }, { left + fade, y + thick }, clear, colour, colour, clear);
	list->AddRectFilled({ left + fade, y }, { right - fade, y + thick }, colour);
	list->AddRectFilledMultiColor({ right - fade, y }, { right, y + thick }, colour, clear, clear, colour);
}

} /* namespace */

Menu::Menu(SDL_Window *window, const std::string &title)
	: m_window(window), m_title(title), m_imgui(nullptr), m_scale(0.0f), m_page(INPUT), m_cursor_shown(true),
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

	io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

	/* The kit draws the widgets; the windows only hold them */
	ImGuiStyle &style = ImGui::GetStyle();
	const MenuPalette &palette = RUNASTEINN;
	style.WindowBorderSize = 0.0f;
	style.ChildBorderSize = 0.0f;
	style.WindowPadding = { PADDING, PADDING };
	style.ItemSpacing = { 0.0f, 0.0f };
	style.ScrollbarSize = 8.0f;
	style.ScrollbarRounding = 0.0f;
	style.Colors[ImGuiCol_Text] = colour(palette.ink);
	style.Colors[ImGuiCol_TextDisabled] = colour(palette.faint);
	style.Colors[ImGuiCol_ChildBg] = colour(0);
	style.Colors[ImGuiCol_ScrollbarBg] = colour(0);
	style.Colors[ImGuiCol_ScrollbarGrab] = colour(palette.outline);
	style.Colors[ImGuiCol_ScrollbarGrabHovered] = colour(palette.dim);
	style.Colors[ImGuiCol_ScrollbarGrabActive] = colour(palette.ink);
	style.Colors[ImGuiCol_NavCursor] = colour(palette.accent);
	m_style = style;

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

bool
Menu::popup_open() const
{
	ImGui::SetCurrentContext(m_imgui);
	return ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel);
}

/* The menu grows with the window, by one factor for all (mock-up v50): as
 * much as the menu made for 640 by 480, or 960 by 540 in a wide window,
 * takes to fill it one way, the other way given the room left; never
 * smaller. In points: the pixels of a dense display are SDL's. */
void
Menu::follow_scale()
{
	int w = 0, h = 0;

	SDL_GetWindowSize(m_window, &w, &h);

	const float fw = static_cast<float>(w), fh = static_cast<float>(h);
	const bool large = fw >= WIDE * fh;
	const float scale = std::max(1.0f, std::min(fw / (large ? LARGE_W : COMPACT_W), fh / (large ? LARGE_H : COMPACT_H)));

	if (scale == m_scale)
		return;

	ImGuiStyle &style = ImGui::GetStyle();

	style = m_style;
	style.ScaleAllSizes(scale);
	style.FontScaleDpi = scale;
	m_scale = scale;
}

/* The whole window: the tabs at the top, the page under them, what it
 * says and what the keys do at the bottom */
void
Menu::build(MenuState &state)
{
	ImGui::SetCurrentContext(m_imgui);
	follow_scale();
	ImGui_ImplSDL3_NewFrame();
	ImGui::NewFrame();

	MenuKit kit(RUNASTEINN, m_scale);
	const MenuPalette &palette = kit.palette();
	const ImVec2 a = { 0.0f, 0.0f }, b = ImGui::GetIO().DisplaySize;
	const float w = b.x, line = ImGui::GetTextLineHeight(), pad = kit.px(PADDING), foot_pad = kit.px(6.0f);

	ImGui::SetNextWindowPos(a);
	ImGui::SetNextWindowSize(b);
	ImGui::Begin("Odin", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
	             ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBackground);

	ImDrawList *const list = ImGui::GetWindowDrawList();
	const bool touched = std::any_of(state.touched.begin(), state.touched.end(), [](bool t) { return t; });
	const bool hot = m_page == INPUT && (state.catching || touched);

	list->AddRectFilled(a, b, palette.frame);
	kit.tabs(TABS, TAB_ICONS, b.x < WIDE * b.y, m_page);
	under_tabs(list, a.x + w * LINE_ENDS, b.x - w * LINE_ENDS, ImGui::GetCursorScreenPos().y + kit.px(3.0f), kit.stroke(),
	           hot ? MenuKit::alpha(palette.accent, LINE_HOT) : palette.outline);
	ImGui::Dummy({ 0.0f, kit.px(5.0f) });

	/* What the page says at the bottom, beside the keys, on as many lines
	 * as it takes, under a line: the page above it. Input says it with
	 * the names and inputs of its controls. */
	const char keys[] = "Esc: back to the game    Ctrl+Q: quit";
	const float keys_w = ImGui::CalcTextSize(keys).x, help_w = std::max(0.0f, w - 3.0f * pad - keys_w);
	const std::string help = m_page == INPUT ? std::string() : this->help(m_page, state);
	const float foot = m_page == INPUT ? input_foot(kit, state, {}, help_w, 0.0f, false) :
	                   std::max(line, ImGui::CalcTextSize(help.c_str(), nullptr, false, help_w).y);
	const float foot_top = b.y - foot_pad - foot;

	ImGui::BeginChild("page", { 0.0f, foot_top - foot_pad - ImGui::GetCursorScreenPos().y }, ImGuiChildFlags_None,
	                  ImGuiWindowFlags_NoBackground);
	if (m_page == INPUT)
		page_input(kit, state);
	else if (m_page == DISPLAY)
		page_display(kit, state);
	else if (m_page == AUDIO)
		page_audio(kit, state);
	else if (m_page == HOTKEYS)
		page_hotkeys(kit, state);
	else if (m_page == OUTPUTS)
		page_outputs(kit, state);
	else if (m_page == PATHS)
		page_paths(kit, state);
	else
		page_about(kit);
	ImGui::Dummy({ 0.0f, kit.px(4.0f) });
	ImGui::EndChild();

	/* While an input is caught, Input says what to do there, keys and all */
	const bool keys_shown = m_page != INPUT || !state.catching;

	list->AddLine({ a.x, foot_top - foot_pad }, { b.x, foot_top - foot_pad }, palette.line, kit.stroke());
	if (m_page == INPUT)
		input_foot(kit, state, { a.x + pad, foot_top }, keys_shown ? help_w : w - 2.0f * pad, foot, true);
	else
		list->AddText(ImGui::GetFont(), ImGui::GetFontSize(), { a.x + pad, foot_top + (foot - line) / 2.0f },
		              palette.dim, help.c_str(), nullptr, help_w);
	if (keys_shown)
		list->AddText({ b.x - pad - keys_w, foot_top + (foot - line) / 2.0f }, palette.faint, keys);

	ImGui::End();
	ImGui::Render();
}

/* Display: what draws the picture, how fine, where, how fast */
void
Menu::page_display(MenuKit &kit, MenuState &state) const
{
	char about[160];
	const bool narrow = ImGui::GetContentRegionAvail().x < kit.px(560.0f);

	kit.begin_group("ENGINE");
	kit.text("Graphics card", "the one the system gives Odin, with OpenGL 4.3", state.gpu.c_str());
	kit.end_group();

	kit.begin_group("3D QUALITY");
	static constexpr const char *SCALES[] = { "1×", "2×", "3×", "4×", "5×", "6×" };
	int scale = static_cast<int>(state.scale) - 1;

	std::snprintf(about, sizeof(about), "internal resolution %u × %u%s", 640 * state.scale, 480 * state.scale,
	              state.scale == 1 ? " · as the cabinet" : "");
	if (kit.choice("Upscaling", about, SCALES, scale, state.scale_given ? "set by the command line: -x" : nullptr))
		state.scale = static_cast<unsigned>(scale) + 1;

	static constexpr const char *FILTERS[] = { "None", "2×", "4×", "8×", "16×" };
	int filter = index_of(ANISOTROPY, state.anisotropy);

	if (kit.choice("Texture filter", "anisotropic; the cabinet has none. Shows on far floors and roads (NASCAR, "
	               "Virtual-On), hardly elsewhere", FILTERS, filter,
	               state.anisotropy_given ? "set by the command line: -a" : nullptr))
		state.anisotropy = ANISOTROPY[filter];
	kit.end_group();

	kit.begin_group("SCREEN");
	static constexpr const char *MODES[] = { "Window", "Full screen" };
	int mode = state.fullscreen ? 1 : 0;

	std::snprintf(about, sizeof(about), "full screen at the size of the display, detected: %u × %u · F11 or Alt+Enter",
	              state.display_w, state.display_h);
	if (kit.choice("Display", about, MODES, mode, state.fullscreen_given ? "set by the command line: -f" : nullptr))
		state.fullscreen = mode == 1;

	int window = static_cast<int>(state.window) - 1;

	if (kit.choice("Window size at start", "640 × 480 by default", narrow ? WINDOW_WIDTHS : WINDOW_SIZES, window,
	               state.window_given ? "set by the command line: -w" : nullptr, state.fullscreen))
		state.window = static_cast<unsigned>(window) + 1;
	kit.end_group();

	kit.begin_group("SPEED");
	static constexpr const char *PACES[] = { "60 fps (cabinet)", "Unlimited" };
	int pace = state.unthrottled ? 1 : 0;

	if (kit.choice("Pace", "unlimited: as fast as the machine, without sound · at the next launch", PACES, pace,
	               state.unthrottled_given ? "set by the command line: -u" : nullptr))
		state.unthrottled = pace == 1;

	static constexpr const char *ON_OFF[] = { "on", "off" };
	int speed = state.show_speed ? 0 : 1;

	if (kit.choice("Show the speed", "frames per second in the title", ON_OFF, speed))
		state.show_speed = speed == 0;
	kit.end_group();

}

std::string
Menu::help(int page, const MenuState &state)
{
	const char *const kept = state.kept ? "." : ", until Odin closes: ODIN_CONFIG=none keeps nothing.";

	switch (page) {
	case DISPLAY:
		return std::string("These settings hold for every game") + kept;
	case AUDIO:
		return std::string("Sound only: vibration is set in Input") + kept;
	case HOTKEYS:
		return state.hotkey_catching || state.kept ? hotkeys_help(state) :
		       hotkeys_help(state) + " Until Odin closes: ODIN_CONFIG=none keeps nothing.";
	case OUTPUTS:
		return state.kept ? "" : "Until Odin closes: ODIN_CONFIG=none keeps nothing.";
	case PATHS:
		if (state.no_picker)
			return "This system gives Odin no folder picker: [paths] of odin.ini takes the folders.";
		return state.choosing ? "Pick the folder in the window of the system." :
		                        std::string("These paths hold for every game") + kept;
	default:
		return "";
	}
}

/* About: the rune, Odin and the commit it is built from, in the middle;
 * the libraries it is made with */
void
Menu::page_about(MenuKit &kit) const
{
	ImDrawList *const list = ImGui::GetWindowDrawList();
	const MenuPalette &palette = kit.palette();
	const ImVec2 at = ImGui::GetCursorScreenPos();
	const float width = ImGui::GetContentRegionAvail().x, mid = at.x + width / 2.0f;
	const float rune = kit.px(34.0f), big = MenuKit::font(22.0f / 12.5f), letters = big * 0.2f;
	const float small = MenuKit::font(MenuKit::SMALL);
	const char *const what = "Sega Hikaru emulator, as close to the hardware as it goes";
	const char *const version = odin_version();
	float y = at.y + kit.px(10.0f);

	MenuKit::icon(list, MenuKit::Icon::RUNE, { mid - rune / 2.0f, y }, rune, palette.ink);
	y += rune + kit.px(6.0f);
	MenuKit::write(list, { mid - MenuKit::measure("ODIN", big, letters) / 2.0f, y }, palette.ink, "ODIN", big, letters);
	y += big + kit.px(6.0f);
	list->AddText({ mid - ImGui::CalcTextSize(what).x / 2.0f, y }, palette.dim, what);
	y += ImGui::GetTextLineHeight() + kit.px(4.0f);
	if (*version) {
		list->AddText(ImGui::GetFont(), small, { mid - MenuKit::measure(version, small) / 2.0f, y }, palette.dim, version);
		y += small;
	}
	ImGui::Dummy({ 0.0f, y + kit.px(6.0f) - at.y });

	kit.begin_group("LICENCES");
	for (const auto &[name, by, licence] : LICENCES)
		kit.text(name, by, licence);
	kit.end_group();
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

	/* The picture of the game over the whole window, its middle kept
	 * where the window is of another shape (mock-up v50) */
	const PictureRect rect = picture_rect(window_w, window_h);
	if (m_frozen && rect.w && rect.h) {
		const double k = std::max(static_cast<double>(window_w) / rect.w, static_cast<double>(window_h) / rect.h);
		const auto w = static_cast<GLsizei>(std::lround(rect.w * k)), h = static_cast<GLsizei>(std::lround(rect.h * k));

		glViewport((static_cast<GLint>(window_w) - w) / 2, (static_cast<GLint>(window_h) - h) / 2, w, h);
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
