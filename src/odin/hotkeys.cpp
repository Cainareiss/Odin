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

#include "odin/hotkeys.hpp"

#include <bit>
#include <cstdio>

#include "odin/settings.hpp"

namespace odin {

namespace {

constexpr const char *IDS[Hotkeys::ACTIONS] = { "menu", "pause", "save_state", "load_state", "reset", "fullscreen",
                                                "quit" };

/* Escape or F10 the menu, Guide tapped alone; P pauses (as in MAME); F4
 * saves the state and F1 loads it; Ctrl+R switches the cabinet off and
 * on; F11 or Alt+Enter the whole display; Ctrl+Q quits. On a gamepad
 * Guide held with Back, RB, LB or Start, as Batocera, which has no button
 * to reset. */
const Hotkeys::Binding STANDARD[Hotkeys::ACTIONS] = {
	{ { { { SDLK_ESCAPE, SDL_KMOD_NONE }, { SDLK_F10, SDL_KMOD_NONE } } }, Hotkeys::GUIDE_ALONE },
	{ { { { SDLK_P, SDL_KMOD_NONE }, {} } }, SDL_GAMEPAD_BUTTON_BACK },
	{ { { { SDLK_F4, SDL_KMOD_NONE }, {} } }, SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER },
	{ { { { SDLK_F1, SDL_KMOD_NONE }, {} } }, SDL_GAMEPAD_BUTTON_LEFT_SHOULDER },
	{ { { { SDLK_R, SDL_KMOD_CTRL }, {} } }, Hotkeys::NO_PAD },
	{ { { { SDLK_F11, SDL_KMOD_NONE }, { SDLK_RETURN, SDL_KMOD_ALT } } }, Hotkeys::NO_PAD },
	{ { { { SDLK_Q, SDL_KMOD_CTRL }, {} } }, SDL_GAMEPAD_BUTTON_START },
};

/* The modifiers as odin.ini names them */
constexpr std::pair<SDL_Keymod, const char *> MODIFIERS[] = {
	{ SDL_KMOD_CTRL, "Ctrl" }, { SDL_KMOD_ALT, "Alt" }, { SDL_KMOD_SHIFT, "Shift" }, { SDL_KMOD_GUI, "Gui" },
};

} /* namespace */

Hotkeys::Hotkeys()
{
	reset();
}

Hotkeys::Hotkeys(const Settings &settings) : Hotkeys()
{
	for (const auto &[key, value] : settings.section("hotkeys")) {
		size_t action = 0;

		while (action < ACTIONS && key != IDS[action])
			action++;

		const std::optional<Binding> binding = action < ACTIONS ? parse(value) : std::nullopt;

		if (!binding) {
			std::fprintf(stderr, "ERROR: settings: [hotkeys]: not right: %s = %s\n", key.c_str(), value.c_str());
			continue;
		}
		m_bindings[action] = {};
		for (size_t slot = 0; slot < KEYS; slot++)
			if (binding->keys[slot].key != SDLK_UNKNOWN)
				set_key(static_cast<Action>(action), slot, binding->keys[slot]);
		set_pad(static_cast<Action>(action), binding->pad);
	}
}

const char *
Hotkeys::id(Action action)
{
	return IDS[static_cast<size_t>(action)];
}

const Hotkeys::Binding &
Hotkeys::standard(Action action)
{
	return STANDARD[static_cast<size_t>(action)];
}

std::optional<Hotkeys::Action>
Hotkeys::key_action(SDL_Keycode key, SDL_Keymod mods) const
{
	const SDL_Keymod held = modifiers(mods);
	std::optional<Action> found;
	int most = -1;

	for (size_t action = 0; action < ACTIONS; action++) {
		for (const HotKey &k : m_bindings[action].keys) {
			const int own = std::popcount(static_cast<unsigned>(k.mods));

			if (k.key == SDLK_UNKNOWN || k.key != key || (k.mods & ~held) || own <= most)
				continue;
			found = static_cast<Action>(action);
			most = own;
		}
	}
	return found;
}

std::optional<Hotkeys::Action>
Hotkeys::pad_action(int pad) const
{
	for (size_t action = 0; action < ACTIONS; action++)
		if (pad != NO_PAD && m_bindings[action].pad == pad)
			return static_cast<Action>(action);
	return std::nullopt;
}

bool
Hotkeys::takes(SDL_Keycode key) const
{
	for (const Binding &binding : m_bindings)
		for (const HotKey &k : binding.keys)
			if (k.key == key && k.mods == SDL_KMOD_NONE)
				return true;
	return false;
}

std::optional<Hotkeys::Action>
Hotkeys::set_key(Action action, size_t slot, HotKey key)
{
	std::optional<Action> from;

	if (slot >= KEYS)
		return from;
	key.mods = modifiers(key.mods);
	for (size_t a = 0; a < ACTIONS; a++)
		for (HotKey &k : m_bindings[a].keys)
			if (key.key != SDLK_UNKNOWN && k == key) {
				k = {};
				if (a != static_cast<size_t>(action))
					from = static_cast<Action>(a);
			}
	m_bindings[static_cast<size_t>(action)].keys[slot] = key;
	return from;
}

std::optional<Hotkeys::Action>
Hotkeys::set_pad(Action action, int pad)
{
	std::optional<Action> from;

	for (size_t a = 0; a < ACTIONS; a++)
		if (pad != NO_PAD && m_bindings[a].pad == pad) {
			m_bindings[a].pad = NO_PAD;
			if (a != static_cast<size_t>(action))
				from = static_cast<Action>(a);
		}
	m_bindings[static_cast<size_t>(action)].pad = pad;
	return from;
}

void
Hotkeys::reset()
{
	for (size_t action = 0; action < ACTIONS; action++)
		m_bindings[action] = STANDARD[action];
}

SDL_Keymod
Hotkeys::modifiers(SDL_Keymod mods)
{
	SDL_Keymod held = SDL_KMOD_NONE;

	for (const auto &[mod, name] : MODIFIERS)
		if (mods & mod)
			held |= mod;
	return held;
}

bool
Hotkeys::is_modifier(SDL_Keycode key)
{
	switch (key) {
	case SDLK_LCTRL:
	case SDLK_RCTRL:
	case SDLK_LALT:
	case SDLK_RALT:
	case SDLK_LSHIFT:
	case SDLK_RSHIFT:
	case SDLK_LGUI:
	case SDLK_RGUI:
	case SDLK_MODE:
		return true;
	default:
		return false;
	}
}

std::string
Hotkeys::text(const HotKey &key)
{
	std::string out;

	for (const auto &[mod, name] : MODIFIERS)
		if (key.mods & mod)
			out = out + name + "+";
	return out + SDL_GetKeyName(key.key);
}

std::optional<HotKey>
Hotkeys::key_of(std::string_view text)
{
	HotKey key;

	text = trimmed(text);
	for (;;) {
		const size_t plus = text.find('+');
		bool found = false;

		/* "+" itself, or a key whose name ends with it */
		if (plus == std::string_view::npos || plus + 1 == text.size())
			break;
		for (const auto &[mod, name] : MODIFIERS) {
			if (text.substr(0, plus) == name) {
				key.mods |= mod;
				found = true;
			}
		}
		if (!found)
			return std::nullopt;
		text.remove_prefix(plus + 1);
	}
	key.key = SDL_GetKeyFromName(std::string(text).c_str());
	if (key.key == SDLK_UNKNOWN || is_modifier(key.key))
		return std::nullopt;
	return key;
}

std::string
Hotkeys::line(const Binding &binding)
{
	std::string out;

	for (const HotKey &key : binding.keys)
		if (key.key != SDLK_UNKNOWN)
			out += (out.empty() ? "" : ", ") + text(key);
	if (binding.pad == GUIDE_ALONE)
		out += out.empty() ? "pad:guide" : ", pad:guide";
	else if (binding.pad != NO_PAD)
		out += std::string(out.empty() ? "" : ", ") + "pad:guide+" +
		       SDL_GetGamepadStringForButton(static_cast<SDL_GamepadButton>(binding.pad));
	return out.empty() ? "none" : out;
}

std::optional<Hotkeys::Binding>
Hotkeys::parse(std::string_view line)
{
	Binding binding;
	size_t keys = 0;

	if (trimmed(line) == "none")
		return binding;
	while (!line.empty()) {
		const size_t comma = line.find(',');
		const std::string_view item = trimmed(line.substr(0, comma));

		line = comma == std::string_view::npos ? std::string_view() : line.substr(comma + 1);
		if (item.starts_with("pad:")) {
			const std::string_view pad = item.substr(4);

			if (binding.pad != NO_PAD)
				return std::nullopt;
			if (pad == "guide") {
				binding.pad = GUIDE_ALONE;
				continue;
			}
			if (!pad.starts_with("guide+"))
				return std::nullopt;

			const SDL_GamepadButton button = SDL_GetGamepadButtonFromString(std::string(pad.substr(6)).c_str());

			if (button == SDL_GAMEPAD_BUTTON_INVALID || button == SDL_GAMEPAD_BUTTON_GUIDE)
				return std::nullopt;
			binding.pad = button;
			continue;
		}

		const std::optional<HotKey> key = key_of(item);

		if (!key || keys == KEYS)
			return std::nullopt;
		binding.keys[keys++] = *key;
	}
	return binding;
}

} /* namespace odin */
