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

#ifndef ODIN_HOTKEYS_HPP
#define ODIN_HOTKEYS_HPP

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

#include <SDL3/SDL.h>

namespace odin {

class Settings;

/* A key of Odin: the key, and Ctrl, Alt, Shift or Gui held with it (either
 * side); none is SDLK_UNKNOWN */
struct HotKey {
	SDL_Keycode key = SDLK_UNKNOWN;
	SDL_Keymod mods = SDL_KMOD_NONE;

	bool operator==(const HotKey &) const = default;
};

/* What Odin does itself on a key or on a gamepad (mock-up v50, page
 * Hotkeys): up to two keys for each action, and a button pressed with Guide
 * held (Guide alone: tapped, the menu by default), as Batocera has its
 * hotkey. The user changes them in the menu; [hotkeys] of odin.ini keeps
 * those that are not as by default, one line an action:
 *
 *   pause = Pause, pad:guide+back
 *   quit = Ctrl+Q, pad:guide+start
 *
 * A key may be held with others than its own (a player holding Left Ctrl
 * still pauses with P); the key with the most of its own held goes first.
 * A key or a button is one action's: given to another, it leaves the first
 * one. */
class Hotkeys {
public:
	/* CARD: the player's card into the slot of the card reader, or back
	 * out of it (Virtual-On) */
	enum class Action : uint8_t { MENU, PAUSE, SAVE_STATE, LOAD_STATE, RESET, FULLSCREEN, CARD, QUIT };
	static constexpr size_t ACTIONS = 8;
	static constexpr size_t KEYS = 2;
	/* The gamepad of an action: none, Guide tapped alone, or a button with
	 * Guide held (an SDL_GamepadButton) */
	static constexpr int NO_PAD = -1;
	static constexpr int GUIDE_ALONE = SDL_GAMEPAD_BUTTON_GUIDE;

	struct Binding {
		std::array<HotKey, KEYS> keys{};
		int pad = NO_PAD;

		bool operator==(const Binding &) const = default;
	};

	/* Those by default */
	Hotkeys();
	/* [hotkeys] of the settings over those by default; a line that is not
	 * right is said on stderr and left out */
	explicit Hotkeys(const Settings &settings);

	/* The name of an action in odin.ini; those by default */
	static const char *id(Action action);
	static const Binding &standard(Action action);
	const Binding &of(Action action) const { return m_bindings[static_cast<size_t>(action)]; }

	/* The action of a key pressed with those modifiers held; of a button
	 * pressed with Guide held, or GUIDE_ALONE */
	std::optional<Action> key_action(SDL_Keycode key, SDL_Keymod mods) const;
	std::optional<Action> pad_action(int pad) const;
	/* A key that is an action's alone: no control of a game takes it */
	bool takes(SDL_Keycode key) const;

	/* A key (slot 0 or 1) or the gamepad of an action changed; the action
	 * that had it before loses it: that action, when another. All back as
	 * by default. */
	std::optional<Action> set_key(Action action, size_t slot, HotKey key);
	std::optional<Action> set_pad(Action action, int pad);
	void reset();

	/* Ctrl, Alt, Shift and Gui of a state of the modifiers, either side */
	static SDL_Keymod modifiers(SDL_Keymod mods);
	/* Whether a key is Ctrl, Alt, Shift or Gui itself */
	static bool is_modifier(SDL_Keycode key);
	/* A key as odin.ini says it ("Ctrl+Q", "F10"), and back */
	static std::string text(const HotKey &key);
	static std::optional<HotKey> key_of(std::string_view text);
	/* A line of [hotkeys], and back ("Escape, F10, pad:guide") */
	static std::string line(const Binding &binding);
	static std::optional<Binding> parse(std::string_view line);

private:
	std::array<Binding, ACTIONS> m_bindings;
};

} /* namespace odin */

#endif /* ODIN_HOTKEYS_HPP */
