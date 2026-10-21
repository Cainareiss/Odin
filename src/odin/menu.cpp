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
#include <cfloat>
#include <cmath>
#include <optional>
#include <utility>

#include <cstdio>

#include "imgui/imgui.h"
#include "imgui/imgui_impl_opengl3.h"
#include "imgui/imgui_impl_sdl3.h"
#include "odin/control_panel.hpp"
#include "odin/menu_kit.hpp"
#include "odin/picture.hpp"
#include "odin/renderer_gl.hpp"
#include "odin/system_fonts.hpp"
#include "odin/version.hpp"

/* The pictures of the page Display (menu_pictures.S), each from its first
 * byte to the one after its last */
#define PICTURE(name) extern "C" const unsigned char odin_picture_##name[], odin_picture_##name##_end[];
PICTURE(upscale_1) PICTURE(upscale_2) PICTURE(upscale_3) PICTURE(upscale_4) PICTURE(upscale_5) PICTURE(upscale_6)
PICTURE(filter_1) PICTURE(filter_2) PICTURE(filter_4) PICTURE(filter_8) PICTURE(filter_16) PICTURE(screen)
PICTURE(whole) PICTURE(torn)
#undef PICTURE
/* The font of the menu (menu_fonts.S) */
extern "C" const unsigned char odin_font_cozette[], odin_font_cozette_end[];

namespace odin {

namespace {

/* Their files, in the order of Menu::Picture */
const std::pair<const unsigned char *, const unsigned char *> PICTURE_FILES[] = {
	{ odin_picture_upscale_1, odin_picture_upscale_1_end }, { odin_picture_upscale_2, odin_picture_upscale_2_end },
	{ odin_picture_upscale_3, odin_picture_upscale_3_end }, { odin_picture_upscale_4, odin_picture_upscale_4_end },
	{ odin_picture_upscale_5, odin_picture_upscale_5_end }, { odin_picture_upscale_6, odin_picture_upscale_6_end },
	{ odin_picture_filter_1, odin_picture_filter_1_end }, { odin_picture_filter_2, odin_picture_filter_2_end },
	{ odin_picture_filter_4, odin_picture_filter_4_end }, { odin_picture_filter_8, odin_picture_filter_8_end },
	{ odin_picture_filter_16, odin_picture_filter_16_end }, { odin_picture_screen, odin_picture_screen_end },
	{ odin_picture_whole, odin_picture_whole_end }, { odin_picture_torn, odin_picture_torn_end },
};

/* The hints of the mouse Dear ImGui sets for itself (imgui_impl_sdl3.cpp):
 * a click that brings the window to the front is a click, and no capture
 * while a button is held. Odin keeps its own outside the menu. */
constexpr std::array<std::pair<const char *, const char *>, 2> MENU_HINTS = { {
	{ SDL_HINT_MOUSE_FOCUS_CLICKTHROUGH, "1" },
	{ SDL_HINT_MOUSE_AUTO_CAPTURE, "0" },
} };

/* The menu is made for 640 by 480 points (a window nearly square) or 960
 * by 540 (16:9 and wider), then larger by one factor as the window grows
 * (mock-up v50). It takes that share of the window, the game around it, at
 * the least the size it is made for where the window has room for it. The
 * cut of its corners and its padding, in points. */
constexpr float COMPACT_W = 640.0f;
constexpr float COMPACT_H = 480.0f;
constexpr float LARGE_W = 960.0f;
constexpr float LARGE_H = 540.0f;
constexpr float SHARE = 0.88f;
constexpr float CUT = 8.0f;
constexpr float PADDING = 10.0f;
/* The line under the tabs, of the accent of the palette: what it leaves
 * at each end of the width (it spans the row of the games under it), how
 * much of it fades in and out at each end, its opacity; how far under the
 * tabs it is, and the room under it, in points */
constexpr float LINE_ENDS = 0.04f;
constexpr float LINE_FADE = 0.08f;
constexpr float LINE_ALPHA = 0.85f;
constexpr float LINE_UNDER = 2.0f;
constexpr float LINE_ROOM = 9.0f;

/* The tabs of the pages, in the order of Menu::Page */
constexpr const char *TABS[] = { "Input", "Display", "Audio", "Hotkeys", "Outputs", "Paths", "About" };
constexpr MenuKit::Icon TAB_ICONS[] = { MenuKit::Icon::INPUT, MenuKit::Icon::DISPLAY, MenuKit::Icon::AUDIO,
	                                MenuKit::Icon::HOTKEYS, MenuKit::Icon::OUTPUTS, MenuKit::Icon::FOLDER,
	                                MenuKit::Icon::ABOUT };
/* A window this many times wider than high takes the large menu; a
 * narrower one has the names of the tabs not chosen left out */
constexpr float WIDE = 1.5f;
/* The letters take the next whole factor that much below it: larger
 * sooner, as the window is larger (the drawings of the page Input already
 * are, at 1600 by 900) */
constexpr float LETTERS_UP = 0.45f;

/* The size of the menu in a window of that size, and the size it is made
 * for */
std::pair<ImVec2, ImVec2>
menu_size(float w, float h)
{
	const ImVec2 made = w >= WIDE * h ? ImVec2{ LARGE_W, LARGE_H } : ImVec2{ COMPACT_W, COMPACT_H };

	return { { std::min(w, std::max(w * SHARE, made.x)), std::min(h, std::max(h * SHARE, made.y)) }, made };
}

/* The window at start, that many times the screen; its sizes shorter
 * where the row is narrow */
constexpr const char *WINDOW_SIZES[] = { "640 × 480", "1280 × 960", "1920 × 1440", "2560 × 1920", "Custom" };
constexpr const char *WINDOW_WIDTHS[] = { "640", "1280", "1920", "2560", "Custom" };
/* Custom among them: the size the user gives the window by hand */
constexpr int CUSTOM = 4;
constexpr unsigned ANISOTROPY[] = { 1, 2, 4, 8, 16 };

/* The libraries Odin is made with: who wrote them, their licence */
constexpr const char *LICENCES[][3] = {
	{ "Odin", "Réda Chérif-Touil · Valkyrie, Stefano Teso", "GPL 3.0 or later" },
	{ "Dear ImGui", "Omar Cornut", "MIT" },
	{ "SDL 3", "Sam Lantinga", "zlib" },
	{ "GLEW", "Milan Ikits, Marcelo Magallon", "BSD / MIT" },
	{ "Jansson", "Petri Lehtinen", "MIT" },
	{ "miniz", "Rich Geldreich, RAD Game Tools, Valve", "MIT" },
	{ "LZMA SDK", "Igor Pavlov", "public domain" },
	{ "Gamepad drawings", "AL2009man, Gamepad-Asset-Pack", "MIT" },
	{ "Cozette", "the-moonwitch", "MIT" },
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

} /* namespace */

Menu::Menu(SDL_Window *window)
	: m_window(window), m_imgui(nullptr), m_scale(0.0f), m_page(INPUT), m_cursor_shown(true),
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
	/* The frame of the menu is the kit's (MenuFocus); Dear ImGui's only in a
	 * list dropped open (MenuKit::list_popup) */
	style.Colors[ImGuiCol_NavCursor] = colour(0);
	m_style = style;

	ImGui_ImplSDL3_InitForOpenGL(window, nullptr);
	/* The gamepads are Odin's (controls.hpp) */
	ImGui_ImplSDL3_SetGamepadMode(ImGui_ImplSDL3_GamepadMode_Manual, nullptr, 0);
	for (size_t i = 0; i < MENU_HINTS.size(); i++)
		set_hint(MENU_HINTS[i].first, m_hints[i]);
}

Menu::~Menu()
{
	for (const auto &[game, rgba] : m_library_handed)
		SDL_DestroySurface(rgba);
	ImGui::SetCurrentContext(m_imgui);
	ImGui_ImplSDL3_Shutdown();
	ImGui::DestroyContext(m_imgui);
}

void
Menu::open(int page)
{
	for (const auto &[name, value] : MENU_HINTS)
		SDL_SetHint(name, value);
	m_cursor_shown = SDL_CursorVisible();
	if (page >= 0 && page < PAGES && page != m_page) {
		m_page = page;
		m_focus = {};
	}
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

/* What a gamepad asks: the page or the game before or after (the games of
 * the page Outputs; on the page Input these buttons are tested as the
 * controls they work, and its first rows change the page and the game); a
 * list dropped open takes the moves, A and B as the keys of its list */
void
Menu::navigate(MenuState &state)
{
	MenuState::Nav &nav = state.nav;

	if (nav.page && m_page != INPUT) {
		m_page = (m_page + PAGES + nav.page % PAGES) % PAGES;
		m_focus = {};
		m_focus.at = 0;
		nav = {};
	}
	if (const int n = static_cast<int>(state.games.size()); nav.game && n && m_page == OUTPUTS) {
		const int shown = static_cast<int>(std::min(state.shown, state.games.size() - 1));

		state.show = static_cast<size_t>((shown + nav.game % n + n) % n);
	}
	m_focus.side = 0;
	m_focus.press = false;
	m_focus.moved = false;
	if (!popup_open()) {
		/* The frame: on the first item at the first button, else moved */
		if (m_focus.at >= m_focus.last && m_focus.last > 0)
			m_focus.at = m_focus.last - 1;
		if (m_focus.at < 0 && (nav.move || nav.side || nav.press)) {
			m_focus.at = 0;
			m_focus.moved = true;
		} else if (m_focus.at >= 0) {
			if (nav.move && m_focus.last > 0) {
				m_focus.at = ((m_focus.at + nav.move) % m_focus.last + m_focus.last) % m_focus.last;
				m_focus.moved = true;
				m_focus.part = 0;
			}
			m_focus.side = nav.side;
			m_focus.press = nav.press;
		}
		return;
	}

	ImGuiIO &io = ImGui::GetIO();
	const auto tap = [&io](ImGuiKey key) {
		io.AddKeyEvent(key, true);
		io.AddKeyEvent(key, false);
	};

	for (int i = 0; i < std::abs(nav.move); i++)
		tap(nav.move < 0 ? ImGuiKey_UpArrow : ImGuiKey_DownArrow);
	if (nav.press)
		tap(ImGuiKey_Enter);
	if (nav.back)
		tap(ImGuiKey_Escape);
	nav = {};
}

/* The menu grows with the window, by one factor for all (mock-up v50): as
 * much as the menu made for 640 by 480, or 960 by 540 in a wide window,
 * takes to fill its share one way, the other way given the room left;
 * never smaller. In points: the pixels of a dense display are SDL's. Its
 * letters, drawn by pixels, grow by a whole factor in the pixels of the
 * display (LETTERS_UP), so that each stays a square of whole pixels. */
void
Menu::follow_scale()
{
	int w = 0, h = 0;

	SDL_GetWindowSize(m_window, &w, &h);

	const auto [size, made] = menu_size(static_cast<float>(w), static_cast<float>(h));
	const float scale = std::max(1.0f, std::min(size.x / made.x, size.y / made.y));
	const float density = std::max(1.0f, SDL_GetWindowPixelDensity(m_window));

	if (scale == m_scale && density == m_density)
		return;

	ImGuiStyle &style = ImGui::GetStyle();

	style = m_style;
	style.ScaleAllSizes(scale);
	style.FontScaleDpi = std::max(1.0f, std::floor(scale * density + 1.0f - LETTERS_UP)) / density;
	m_scale = scale;
	m_density = density;
}

/* Once, where Dear ImGui would take its own font: Cozette (menu_fonts.S),
 * drawn by pixels, 13 high, each letter on whole pixels; behind it, for the
 * letters it lacks (it has Latin, Greek, Cyrillic and Vietnamese), the
 * fonts of the system: those of most other languages (a folder named in
 * Japanese...) */
void
Menu::add_fonts()
{
	static constexpr float COZETTE_SIZE = 13.0f;
	ImFontAtlas &fonts = *ImGui::GetIO().Fonts;
	ImFontConfig pixels;
	std::string names;

	if (fonts.Sources.Size)
		return;
	pixels.FontDataOwnedByAtlas = false;
	pixels.PixelSnapH = true;
	pixels.OversampleH = pixels.OversampleV = 1;
	fonts.AddFontFromMemoryTTF(const_cast<unsigned char *>(odin_font_cozette),
	                           static_cast<int>(odin_font_cozette_end - odin_font_cozette), COZETTE_SIZE, &pixels);
	m_fonts = std::make_unique<SystemFonts>();
	for (const SystemFonts::Font &font : m_fonts->fonts()) {
		ImFontConfig more;

		more.MergeMode = true;
		more.FontDataOwnedByAtlas = false;
		fonts.AddFontFromMemoryTTF(const_cast<unsigned char *>(font.data), static_cast<int>(font.size), 0.0f, &more);
		names += (names.empty() ? "" : ", ") + font.path;
	}
	std::printf("menu: letters of other languages from %s\n", names.empty() ? "no font of the system" : names.c_str());
}

/* The frame in the middle of the window: the tabs at the top, the page
 * under them, what it says and what the keys do at the bottom */
void
Menu::build(MenuState &state)
{
	ImGui::SetCurrentContext(m_imgui);
	follow_scale();
	navigate(state);
	ImGui_ImplSDL3_NewFrame();
	add_fonts();
	ImGui::NewFrame();
	close_bar_menus();

	MenuKit kit(RUNASTEINN, m_scale, &m_focus);
	const MenuPalette &palette = kit.palette();
	const ImVec2 room = ImGui::GetIO().DisplaySize;
	const ImVec2 size = menu_size(room.x, room.y).first;
	const ImVec2 a = { std::round((room.x - size.x) / 2.0f), std::round((room.y - size.y) / 2.0f) };
	const ImVec2 b = { a.x + size.x, a.y + size.y };
	const float w = size.x, line = ImGui::GetTextLineHeight(), pad = kit.px(PADDING), foot_pad = kit.px(6.0f);

	ImGui::SetNextWindowPos(a);
	ImGui::SetNextWindowSize(size);
	ImGui::Begin("Odin", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
	             ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBackground);

	ImDrawList *const list = ImGui::GetWindowDrawList();

	kit.frame(list, a, b, kit.px(CUT), palette.frame, palette.outline);
	/* On the page Input the tabs are the first row of the frame, without
	 * pills: left and right change the page there. A tab clicked: no frame
	 * on the page shown */
	const bool tabs_row = m_page == INPUT, by_pad = tabs_row && m_focus.at == 0 && m_focus.side;

	m_focus.count = 0;
	if (kit.tabs(TABS, TAB_ICONS, room.x < WIDE * room.y, m_page,
	             tabs_row ? std::span<const std::string>() : std::span<const std::string>(state.pills.data(), 2),
	             tabs_row)) {
		m_focus = {};
		if (by_pad)
			m_focus.at = 0;
	}
	MenuKit::fading_line(list, a.x + w * LINE_ENDS, b.x - w * LINE_ENDS,
	                     ImGui::GetCursorScreenPos().y + kit.px(LINE_UNDER), kit.stroke(),
	                     MenuKit::alpha(palette.accent, LINE_ALPHA), LINE_FADE);
	ImGui::Dummy({ 0.0f, kit.px(LINE_ROOM) });

	/* What the page says at the bottom, beside the keys, on as many lines
	 * as it takes, under a line: the page above it. Input says it with
	 * the names and inputs of its controls. */
	const char *const keys = state.library ? "Esc: back to the library    Ctrl+Q: quit" :
	                                         "Esc: back to the game    Ctrl+Q: quit";
	const float keys_w = ImGui::CalcTextSize(keys).x, help_w = std::max(0.0f, w - 3.0f * pad - keys_w);
	const std::string help = m_page == INPUT ? std::string() : this->help(m_page, state);
	const float help_h = m_page == INPUT ? 0.0f : ImGui::CalcTextSize(help.c_str(), nullptr, false, help_w).y;
	const float foot = m_page == INPUT ? input_foot(kit, state, {}, help_w, 0.0f, false) : std::max(line, help_h);
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
	m_focus.last = m_focus.count;
	/* The advanced options of the page Input close with it */
	if (m_page != INPUT)
		state.advanced = false;

	/* While an input is caught, Input says what to do there, keys and all */
	const bool keys_shown = m_page != INPUT || !state.catching;

	list->AddLine({ a.x, foot_top - foot_pad }, { b.x, foot_top - foot_pad }, palette.line, kit.stroke());
	if (m_page == INPUT)
		input_foot(kit, state, { a.x + pad, foot_top }, keys_shown ? help_w : w - 2.0f * pad, foot, true);
	else
		list->AddText(ImGui::GetFont(), ImGui::GetFontSize(), { a.x + pad, foot_top + (foot - help_h) / 2.0f },
		              palette.dim, help.c_str(), nullptr, help_w);
	if (keys_shown)
		list->AddText({ b.x - pad - keys_w, foot_top + (foot - line) / 2.0f }, palette.faint, keys);

	ImGui::End();
	ImGui::Render();
}

/* Display: what draws the picture, how fine, where, how fast. At the left,
 * one at a time, what the row the mouse is on does, else the row of the
 * frame of the gamepad, else the last one shown: real captures of Odin. */
void
Menu::page_display(MenuKit &kit, MenuState &state) const
{
	static constexpr float SHARE = 0.45f;	/* of the width, the left's */
	static constexpr double FADE_S = 0.15;
	char about[160];
	const ImVec2 top = ImGui::GetCursorScreenPos(), room = ImGui::GetContentRegionAvail();
	const float gap = kit.px(14.0f), left = std::floor((room.x - gap) * SHARE), high = room.y - kit.px(4.0f);
	std::optional<int> pointed, framed;
	/* A row, and whether the mouse or the frame is on it */
	const auto watch = [&](int which, const auto &row) {
		const int from = kit.focus_count();
		const ImVec2 a = ImGui::GetCursorScreenPos();
		const float w = ImGui::GetContentRegionAvail().x;

		row();
		if (ImGui::IsWindowHovered() && ImGui::IsMouseHoveringRect(a, { a.x + w, ImGui::GetCursorScreenPos().y }))
			pointed = which;
		if (kit.focus_at() >= from && kit.focus_at() < kit.focus_count())
			framed = which;
	};

	ImGui::SetCursorScreenPos({ top.x + left + gap, top.y });
	ImGui::BeginChild("settings", { room.x - left - gap, high }, ImGuiChildFlags_None, ImGuiWindowFlags_NoBackground);

	const bool narrow = ImGui::GetContentRegionAvail().x < kit.px(560.0f);

	kit.begin_group("ENGINE");
	watch(PREVIEW_CARD, [&] {
		kit.text("Graphics card", "the one the system gives Odin, with OpenGL 4.3", state.gpu.c_str());
	});
	kit.end_group();

	kit.begin_group("3D QUALITY");
	static constexpr const char *SCALES[] = { "1×", "2×", "3×", "4×", "5×", "6×" };
	int scale = static_cast<int>(state.scale) - 1;

	std::snprintf(about, sizeof(about), "internal resolution %u × %u%s", 640 * state.scale, 480 * state.scale,
	              state.scale == 1 ? " · as the cabinet" : "");
	watch(PREVIEW_UPSCALE, [&] {
		if (kit.choice("Upscaling", about, SCALES, scale, state.scale_given ? "set by the command line: -x" : nullptr))
			state.scale = static_cast<unsigned>(scale) + 1;
	});

	static constexpr const char *FILTERS[] = { "None", "2×", "4×", "8×", "16×" };
	int filter = index_of(ANISOTROPY, state.anisotropy);

	watch(PREVIEW_FILTER, [&] {
		if (kit.choice("Texture filter", "anisotropic; the cabinet has none. Shows on far floors and roads (NASCAR, "
		               "Virtual-On), hardly elsewhere", FILTERS, filter,
		               state.anisotropy_given ? "set by the command line: -a" : nullptr))
			state.anisotropy = ANISOTROPY[filter];
	});
	kit.end_group();

	kit.begin_group("SCREEN");
	static constexpr const char *MODES[] = { "Window", "Full screen" };
	int mode = state.fullscreen ? 1 : 0;

	std::snprintf(about, sizeof(about), "full screen at the size of the display, detected: %u × %u · F11 or Alt+Enter",
	              state.display_w, state.display_h);
	watch(PREVIEW_SCREEN, [&] {
		if (kit.choice("Display", about, MODES, mode, state.fullscreen_given ? "set by the command line: -f" : nullptr))
			state.fullscreen = mode == 1;
	});

	int window = state.custom_w ? CUSTOM : state.window <= CUSTOM ? static_cast<int>(state.window) - 1 : -1;
	char sizes[96] = "640 × 480 by default · Custom: the size you give it by hand";

	if (state.custom_w)
		std::snprintf(sizes, sizeof(sizes), "Custom: %u × %u, the size you gave it by hand", state.custom_w,
		              state.custom_h);
	watch(PREVIEW_SCREEN, [&] {
		if (!kit.choice("Window size at start", sizes, narrow ? WINDOW_WIDTHS : WINDOW_SIZES, window,
		                state.window_given ? "set by the command line: -w" : nullptr, state.fullscreen))
			return;
		/* Custom: the window as it is now */
		state.custom_w = window == CUSTOM ? state.window_w : 0;
		state.custom_h = window == CUSTOM ? state.window_h : 0;
		if (window != CUSTOM)
			state.window = static_cast<unsigned>(window) + 1;
	});

	static constexpr const char *SYNCS[] = { "off", "on", "adaptive" };
	static constexpr const char *SYNCS_ABOUT[] = {
		"the frames do not wait for the display: one may be torn",
		"each frame waits for the display: none is torn, a late one costs the game time",
		"the frames on time wait for the display; a late one shows at once and may be torn",
	};
	int sync = static_cast<int>(state.vsync);
	const char *const said = state.vsync_off ? state.vsync_off :
	                         state.refresh_short && state.vsync == 1 ?
	                         "off: this display shows fewer frames a second than the 60 of the cabinet" :
	                         SYNCS_ABOUT[state.vsync];

	if (state.refresh > 0.0f)
		std::snprintf(about, sizeof(about), "%s · %.2f Hz", said, static_cast<double>(state.refresh));
	else
		std::snprintf(about, sizeof(about), "%s", said);
	watch(PREVIEW_VSYNC, [&] {
		if (kit.choice("Vertical sync", about, SYNCS, sync, nullptr, state.vsync_off != nullptr))
			state.vsync = static_cast<unsigned>(sync);
	});

	static constexpr const char *SIDES[] = { "Off", "Blur", "Bezel" };
	int sides = static_cast<int>(state.sides);

	watch(PREVIEW_SCREEN, [&] {
		if (kit.choice("Bezel", "where the window is wider than the game: black, the game blurred, or its "
		               "picture in Artwork (swracer_bezel.png, page Paths)", SIDES, sides))
			state.sides = static_cast<unsigned>(sides);
	});
	kit.end_group();

	kit.begin_group("SPEED");
	static constexpr const char *PACES[] = { "60 fps (cabinet)", "Unlimited" };
	int pace = state.unthrottled ? 1 : 0;

	watch(PREVIEW_SCREEN, [&] {
		if (kit.choice("Pace", "unlimited: as fast as the machine, without sound · at the next launch", PACES, pace,
		               state.unthrottled_given ? "set by the command line: -u" : nullptr))
			state.unthrottled = pace == 1;
	});

	static constexpr const char *ON_OFF[] = { "on", "off" };
	int speed = state.show_speed ? 0 : 1;

	watch(PREVIEW_SCREEN, [&] {
		if (kit.choice("Show the speed", "frames per second in the title", ON_OFF, speed))
			state.show_speed = speed == 0;
	});
	kit.end_group();
	ImGui::Dummy({ 0.0f, kit.px(4.0f) });
	ImGui::EndChild();

	/* The left, fading from what it showed to what it shows now */
	const int wanted = pointed ? *pointed : framed ? *framed : state.preview;

	if (wanted != state.preview) {
		state.preview_from = state.preview;
		state.preview = wanted;
		state.preview_since = ImGui::GetTime();
	}

	const ImVec2 a = top, b = { top.x + left, top.y + high };
	const bool fades = state.animations && state.preview_from != state.preview;
	const float u = fades ? static_cast<float>(std::clamp((ImGui::GetTime() - state.preview_since) / FADE_S, 0.0, 1.0)) :
	                        1.0f;

	kit.frame(ImGui::GetWindowDrawList(), a, b, kit.px(6.0f), 0, kit.palette().line);
	if (u < 1.0f) {
		state.moving = true;
		preview(kit, state, state.preview_from, a, b, 1.0f - u);
	}
	preview(kit, state, state.preview, a, b, u);
	ImGui::SetCursorScreenPos(top);
	ImGui::Dummy({ room.x, high });
}

/* What a row of the page Display does, in the box from a to b, as much
 * there as `shown` (0 to 1) */
void
Menu::preview(const MenuKit &kit, const MenuState &state, int which, ImVec2 a, ImVec2 b, float shown) const
{
	ImDrawList *const list = ImGui::GetWindowDrawList();
	const MenuPalette &palette = kit.palette();
	const auto faded = [shown](ImU32 colour) { return MenuKit::alpha(colour, shown); };
	const float pad = kit.px(16.0f), caps = MenuKit::font(MenuKit::SMALL), note = MenuKit::font(MenuKit::NOTE);
	const ImVec2 lo = { a.x + pad, a.y + pad }, hi = { b.x - pad, b.y - pad };
	const float width = hi.x - lo.x, mid = (lo.x + hi.x) / 2.0f;
	/* A text centred where it fits on a line, else wrapped in the width;
	 * its height */
	const auto centred = [&](float y, const char *text, ImU32 colour, float text_size) {
		const ImVec2 at = ImGui::GetFont()->CalcTextSizeA(text_size, FLT_MAX, width, text);
		const float x = at.x < width ? mid - at.x / 2.0f : lo.x;

		list->AddText(ImGui::GetFont(), text_size, { x, y }, faded(colour), text, nullptr, width);
		return at.y;
	};
	const auto caps_centred = [&](float x, float y, const char *text) {
		MenuKit::small_caps(list, { x - MenuKit::small_caps_width(text) / 2.0f, y }, faded(palette.dim), text);
	};
	/* The cabinet's against the user's choice (none: the choice is the
	 * cabinet's), side by side or one above the other, as they are the
	 * biggest; what they are under them, where they come from at the foot */
	const auto pair = [&](const char *title, int cabinet, int chosen, const char *cabinet_name, const std::string &chosen_name,
	                      const char *source) {
		const float apart = kit.px(12.0f), under = caps + kit.px(16.0f);
		const float source_h = ImGui::GetFont()->CalcTextSizeA(note, FLT_MAX, width, source).y;
		const float room = hi.y - lo.y - caps - kit.px(10.0f) - source_h - kit.px(12.0f);
		const float beside = std::min((width - apart) / 2.0f * 0.75f, room - under);
		const float stacked = std::min(width * 0.75f, (room - 2.0f * under - apart) / 2.0f);
		const bool above = stacked > beside;
		const float h = std::max(above ? stacked : beside, 0.0f), w = h / 0.75f;
		const float block = caps + kit.px(10.0f) + (above ? 2.0f * (h + under) + apart : h + under);
		float y = lo.y + std::max(0.0f, (hi.y - lo.y - source_h - kit.px(12.0f) - block) / 2.0f);
		const std::string chosen_caption = "YOUR CHOICE: " + chosen_name;
		/* One picture, framed, and its name under it */
		const auto one = [&](int which_picture, ImVec2 at, const char *name) {
			const ImVec2 to = { at.x + w, at.y + h };

			if (which_picture >= 0) {
				picture(list, which_picture, at, to, shown);
				list->AddRect(at, to, faded(palette.line), 0.0f, ImDrawFlags_None, kit.stroke());
			} else {
				const char *const pick = "pick 2× or more to see the difference";
				const ImVec2 text = ImGui::GetFont()->CalcTextSizeA(note, FLT_MAX, w - kit.px(16.0f), pick);

				kit.dashed_frame(list, at, to, faded(palette.outline));
				list->AddText(ImGui::GetFont(), note, { at.x + kit.px(8.0f), at.y + (h - text.y) / 2.0f },
				              faded(palette.faint), pick, nullptr, w - kit.px(16.0f));
			}
			caps_centred(at.x + w / 2.0f, to.y + kit.px(8.0f), name);
		};

		caps_centred(mid, y, title);
		y += caps + kit.px(10.0f);
		if (above) {
			one(cabinet, { mid - w / 2.0f, y }, cabinet_name);
			one(chosen, { mid - w / 2.0f, y + h + under + apart }, chosen_caption.c_str());
		} else {
			one(cabinet, { mid - apart / 2.0f - w, y }, cabinet_name);
			one(chosen, { mid + apart / 2.0f, y }, chosen_caption.c_str());
		}
		list->AddText(ImGui::GetFont(), note, { lo.x, hi.y - source_h }, faded(palette.faint), source, nullptr, width);
	};

	switch (which) {
	case PREVIEW_CARD: {
		const char *const name = state.gpu.empty() ? "not known yet" : state.gpu.c_str();
		const float big = MenuKit::font(1.3f);
		const float name_h = ImGui::GetFont()->CalcTextSizeA(big, FLT_MAX, width, name).y;
		float y = lo.y + std::max(0.0f, (hi.y - lo.y - caps - name_h - note - kit.px(22.0f)) / 2.0f);

		caps_centred(mid, y, "GRAPHICS CARD");
		y += caps + kit.px(12.0f);
		y += centred(y, name, palette.ink, big) + kit.px(10.0f);
		centred(y, "OpenGL 4.3: what Odin asks of it", palette.dim, note);
		break;
	}
	case PREVIEW_UPSCALE: {
		const unsigned scale = std::clamp(state.scale, 1u, 6u);
		char chosen[8];

		std::snprintf(chosen, sizeof(chosen), "%u×", scale);
		pair("UPSCALING", UPSCALE, scale > 1 ? static_cast<int>(UPSCALE + scale - 1) : -1, "1× · THE CABINET", chosen,
		     "Real captures of Odin: Brave Fire Fighters, the same picture at each scale.");
		break;
	}
	case PREVIEW_FILTER: {
		const int filter = index_of(ANISOTROPY, state.anisotropy);
		static constexpr const char *NAMES[] = { "NONE", "2×", "4×", "8×", "16×" };

		pair("TEXTURE FILTER", FILTER, filter > 0 ? FILTER + filter : -1, "NONE · THE CABINET", NAMES[filter],
		     "Real captures of Odin: Planet Harriers, the road far off, at 1×.");
		break;
	}
	case PREVIEW_VSYNC: {
		static constexpr const char *NAMES[] = { "OFF", "ON", "ADAPTIVE" };

		pair("VERTICAL SYNC", WHOLE, state.vsync == 0 ? TORN : WHOLE, "WHOLE · THE CABINET",
		     NAMES[std::min(state.vsync, 2u)],
		     "Real captures of Odin: Brave Fire Fighters, two frames that follow; torn, the display shows the top "
		     "of the first and the bottom of the second.");
		break;
	}
	case PREVIEW_SCREEN: {
		const float sw = static_cast<float>(state.display_w ? state.display_w : 1920);
		const float sh = static_cast<float>(state.display_h ? state.display_h : 1080);
		const float ww = state.fullscreen ? sw :
		                 state.custom_w ? static_cast<float>(state.custom_w) : 640.0f * static_cast<float>(state.window);
		const float wh = state.fullscreen ? sh :
		                 state.custom_h ? static_cast<float>(state.custom_h) : 480.0f * static_cast<float>(state.window);
		char caption[160];

		if (state.fullscreen)
			std::snprintf(caption, sizeof(caption), "Full screen, the %u × %u of your display.",
			              static_cast<unsigned>(sw), static_cast<unsigned>(sh));
		else
			std::snprintf(caption, sizeof(caption), "Window %u × %u at start, on your %u × %u display%s",
			              static_cast<unsigned>(ww), static_cast<unsigned>(wh), static_cast<unsigned>(sw),
			              static_cast<unsigned>(sh), ww > sw || wh > sh ? ": bigger than it." : ".");

		const float caption_h = ImGui::GetFont()->CalcTextSizeA(note, FLT_MAX, width, caption).y;
		const float k = std::min(width / sw, (hi.y - lo.y - caps - caption_h - kit.px(26.0f)) / sh);
		float y = lo.y + std::max(0.0f, (hi.y - lo.y - caps - caption_h - kit.px(26.0f) - sh * k) / 2.0f);

		caps_centred(mid, y, "SCREEN");
		y += caps + kit.px(12.0f);

		/* The display, and the window on it with its title */
		const ImVec2 sa = { mid - sw * k / 2.0f, y }, sb = { mid + sw * k / 2.0f, y + sh * k };
		const ImVec2 wa = { mid - ww * k / 2.0f, (sa.y + sb.y) / 2.0f - wh * k / 2.0f }, wb = { wa.x + ww * k, wa.y + wh * k };
		const float bar = state.fullscreen ? 0.0f : std::max(kit.px(5.0f), 30.0f * k);

		list->AddRectFilled(sa, sb, faded(IM_COL32(0, 0, 0, 0x60)));
		list->PushClipRect(sa, sb, true);
		picture(list, SCREEN, { wa.x, wa.y + bar }, wb, shown);
		if (bar > 0.0f) {
			const char *const title = state.show_speed ? "Odin (60.0 FPS)" : "Odin";
			const float title_size = MenuKit::text_size(std::min(note, bar * 0.8f));

			list->AddRectFilled(wa, { wb.x, wa.y + bar }, faded(palette.part));
			if (title_size <= bar && MenuKit::measure(title, title_size) < wb.x - wa.x - kit.px(8.0f))
				list->AddText(ImGui::GetFont(), title_size, { wa.x + kit.px(4.0f), wa.y + (bar - title_size) / 2.0f },
				              faded(palette.dim), title);
			list->AddRect(wa, wb, faded(palette.outline), 0.0f, ImDrawFlags_None, kit.stroke());
		}
		list->PopClipRect();
		list->AddRect(sa, sb, faded(palette.line), 0.0f, ImDrawFlags_None, kit.stroke());
		centred(sb.y + kit.px(10.0f), caption, palette.dim, note);
		break;
	}
	default:
		break;
	}
}

/* A picture of the page Display, from a to b (none until the drawing
 * thread has made it) */
void
Menu::picture(ImDrawList *list, int which, ImVec2 a, ImVec2 b, float shown) const
{
	if (m_pictures[static_cast<size_t>(which)])
		list->AddImage(ImTextureRef(static_cast<ImTextureID>(m_pictures[static_cast<size_t>(which)])), a, b, { 0.0f, 0.0f },
		               { 1.0f, 1.0f }, MenuKit::alpha(IM_COL32_WHITE, shown));
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

/* About: the rune in its gold, Odin and the commit it is built from, in
 * the middle; the libraries it is made with */
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

	MenuKit::icon(list, MenuKit::Icon::RUNE, { mid - rune / 2.0f, y }, rune, LOGO_GOLD);
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
	/* The pictures: bigger than they are, in blocks, as Odin shows the
	 * picture of a game; smaller, smoothed */
	static_assert(std::size(PICTURE_FILES) == PICTURES);
	for (size_t p = 0; p < PICTURES; p++) {
		const auto [begin, end] = PICTURE_FILES[p];
		SDL_Surface *const png = SDL_LoadPNG_IO(SDL_IOFromConstMem(begin, static_cast<size_t>(end - begin)), true);
		SDL_Surface *const rgba = png ? SDL_ConvertSurface(png, SDL_PIXELFORMAT_RGBA32) : nullptr;

		SDL_DestroySurface(png);
		if (!rgba) {
			std::fprintf(stderr, "ERROR: menu: picture %zu: %s\n", p, SDL_GetError());
			continue;
		}
		glGenTextures(1, &m_pictures[p]);
		glBindTexture(GL_TEXTURE_2D, m_pictures[p]);
		glPixelStorei(GL_UNPACK_ROW_LENGTH, rgba->pitch / 4);
		glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, rgba->w, rgba->h, 0, GL_RGBA, GL_UNSIGNED_BYTE, rgba->pixels);
		glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
		glGenerateMipmap(GL_TEXTURE_2D);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
		SDL_DestroySurface(rgba);
	}
	glBindTexture(GL_TEXTURE_2D, 0);
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
	m_frozen_window = false;
	if (m_frozen)
		blur(picture_fb, GL_COLOR_ATTACHMENT0, w, h);
}

void
Menu::freeze_window(unsigned window_w, unsigned window_h)
{
	if (!m_gl)
		start_gl();
	m_frozen = m_frozen_window = false;
	draw(window_w, window_h);
	m_frozen = m_frozen_window = window_w > 1 && window_h > 1;
	if (m_frozen)
		blur(0, GL_BACK, window_w, window_h);
}

void
Menu::blur(GLuint fb, GLenum buffer, unsigned w, unsigned h)
{
	const unsigned hw = w / 2, hh = h / 2;
	const GLint iw = static_cast<GLint>(hw), ih = static_cast<GLint>(hh);

	size_blur(hw, hh);
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
Menu::draw(unsigned window_w, unsigned window_h)
{
	upload_library_pictures();
	glBindFramebuffer(GL_FRAMEBUFFER, 0);
	glDisable(GL_SCISSOR_TEST);
	glDisable(GL_DEPTH_TEST);
	glDisable(GL_BLEND);
	glViewport(0, 0, static_cast<GLsizei>(window_w), static_cast<GLsizei>(window_h));
	glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
	glClear(GL_COLOR_BUFFER_BIT);

	/* The picture of the game over the whole window, its middle kept
	 * where the window is of another shape (mock-up v50); the window as it
	 * was over all of it */
	const PictureRect rect = picture_rect(window_w, window_h);
	if (m_frozen && rect.w && rect.h) {
		const double k = std::max(static_cast<double>(window_w) / rect.w, static_cast<double>(window_h) / rect.h);
		const auto w = m_frozen_window ? static_cast<GLsizei>(window_w) : static_cast<GLsizei>(std::lround(rect.w * k));
		const auto h = m_frozen_window ? static_cast<GLsizei>(window_h) : static_cast<GLsizei>(std::lround(rect.h * k));

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
Menu::draw_bar(unsigned window_w, unsigned window_h)
{
	glBindFramebuffer(GL_FRAMEBUFFER, 0);
	glViewport(0, 0, static_cast<GLsizei>(window_w), static_cast<GLsizei>(window_h));
	ImGui::SetCurrentContext(m_imgui);
	ImGui_ImplOpenGL3_NewFrame();
	ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
	gl::check("the bar");
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
	glDeleteTextures(PICTURES, m_pictures.data());
	m_pictures = {};
	for (const auto &[game, picture] : m_library_pictures)
		glDeleteTextures(1, &picture.texture);
	m_library_pictures.clear();
	m_gl = false;
}

} /* namespace odin */
