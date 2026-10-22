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

#include "odin/menu_session.hpp"

#include <algorithm>

namespace odin {

/* The page Input waits for an input: Escape lets it go; a key, or on a
 * gamepad a button (Guide held, then another: both), a stick or a trigger
 * from half its way, is the one. True when the event was taken. */
bool
MenuSession::catch_input(const SDL_Event &event)
{
	MenuState &state = m_menu_state;

	/* The buttons of the menu follow what is held, as ever */
	const std::vector<MenuNav::Action> nav = m_nav.event(event);
	const bool back = std::find(nav.begin(), nav.end(), MenuNav::BACK) != nav.end();

	switch (event.type) {
	case SDL_EVENT_KEY_DOWN:
	case SDL_EVENT_KEY_UP:
		m_keys.set(event.key.key, event.key.down);
		if (!event.key.down || event.key.repeat)
			return true;
		if (event.key.key == SDLK_ESCAPE) {
			state.catching.reset();
			state.catch_next.clear();
			state.mapping.reset();
			return true;
		}
		/* A gamepad is caught: the keys of quitting and of the whole display
		 * work as in the rest of the menu */
		if (state.on_pad()) {
			const auto action = m_hotkeys.key_action(event.key.key, event.key.mod);

			return action != Hotkeys::Action::QUIT && action != Hotkeys::Action::FULLSCREEN;
		}
		/* The keys Odin works itself go to no control of the game */
		if (event.key.key != SDLK_ESCAPE && !m_hotkeys.takes(event.key.key)) {
			HostInput input{};

			input.kind = HostInput::Kind::KEY;
			input.key = event.key.key;
			caught(input);
		}
		return true;
	case SDL_EVENT_GAMEPAD_BUTTON_UP:
		m_controls.event(event);
		m_controls.guide_tapped();
		m_controls.guide_combo();
		if (event.gbutton.button == SDL_GAMEPAD_BUTTON_GUIDE)
			m_catch_guide = false;
		/* The button that presses, let go before it skipped: caught */
		if (m_held && m_held->button == event.gbutton.button) {
			const Held held = *m_held;

			m_held.reset();
			if (!held.skipped)
				catch_pad(false, held.button, 0);
		}
		return true;
	case SDL_EVENT_GAMEPAD_BUTTON_DOWN:
	case SDL_EVENT_GAMEPAD_AXIS_MOTION: {
		m_controls.event(event);
		m_controls.guide_combo();
		/* A key is caught: B of a gamepad lets it go */
		if (!state.on_pad()) {
			if (back) {
				state.catching.reset();
				state.catch_next.clear();
				state.mapping.reset();
			}
			return true;
		}

		const bool axis = event.type == SDL_EVENT_GAMEPAD_AXIS_MOTION;
		const int value = axis ? event.gaxis.value : 0;

		if (axis && std::abs(value) < ControlPanel::PRESSED)
			return true;
		if (!axis && event.gbutton.button == SDL_GAMEPAD_BUTTON_GUIDE) {
			m_catch_guide = true;
			return true;
		}
		/* Start mapping: the button that presses, held, skips the control
		 * (draw_menu); let go sooner, it is caught then */
		if (!axis && state.mapping && !m_catch_guide && presses(event.gbutton.button)) {
			m_held = Held{ event.gbutton.button, SDL_GetTicks(), false };
			return true;
		}
		catch_pad(axis, axis ? event.gaxis.axis : event.gbutton.button, value);
		return true;
	}
	default:
		return false;
	}
}

/* Whether a button of a gamepad presses in the menu (menu_nav.hpp) */
bool
MenuSession::presses(int button) const
{
	const std::vector<HostInput> &inputs = m_nav.inputs(MenuNav::PRESS);

	return std::any_of(inputs.begin(), inputs.end(), [button](const HostInput &in) {
		return in.kind == HostInput::Kind::PAD && in.pad.type == PadInput::Type::BUTTON &&
		       in.pad.index == button && in.pad.with < 0;
	});
}

/* An input of a gamepad caught: a button (held with Guide when it is), or
 * an axis past half its way */
void
MenuSession::catch_pad(bool axis, int index, int value)
{
	MenuState &state = m_menu_state;
	HostInput input{};

	if (!state.catching)
		return;
	input.kind = HostInput::Kind::PAD;
	input.pad.index = static_cast<uint8_t>(index);
	input.pad.type = !axis ? PadInput::Type::BUTTON : PadInput::Type::AXIS;
	if (!axis && m_catch_guide)
		input.pad.with = SDL_GAMEPAD_BUTTON_GUIDE;
	/* A stick on a part that is pressed: its half */
	if (axis && index < SDL_GAMEPAD_AXIS_LEFT_TRIGGER && state.catching->part != 0)
		input.pad.type = value < 0 ? PadInput::Type::AXIS_NEGATIVE : PadInput::Type::AXIS_POSITIVE;
	/* A button on an analog control goes on its half: of a pedal, or of
	 * each way in turn */
	if (!axis && state.catching->control >= 0 && state.catching->part == 0) {
		const auto c = static_cast<size_t>(state.catching->control);
		const GameControl &control = panel_shown().controls()[c];

		if (control.type == GameControl::Type::ANALOG) {
			const bool two_ways = GameControl::half(control.low, false);
			const MenuSlot other_way = { state.catching->control, 2, state.catching->role };

			state.catching->part = two_ways ? 1 : 2;
			if (two_ways)
				state.catch_next.insert(state.catch_next.begin(), other_way);
		}
	}
	caught(input);
}

/* The page Hotkeys waits for a key (Ctrl, Alt, Shift or Gui held with it
 * are its own) or for a button pressed with Guide held, or Guide tapped
 * alone; Escape lets it be. True when the event was taken. */
bool
MenuSession::catch_hotkey(const SDL_Event &event)
{
	MenuState &state = m_menu_state;
	const auto action = static_cast<Hotkeys::Action>(state.hotkey_catching->first);
	const size_t slot = state.hotkey_catching->second;
	const std::vector<MenuNav::Action> nav = m_nav.event(event);

	switch (event.type) {
	case SDL_EVENT_KEY_DOWN:
	case SDL_EVENT_KEY_UP:
		m_keys.set(event.key.key, event.key.down);
		if (!event.key.down || event.key.repeat)
			return true;
		if (event.key.key == SDLK_ESCAPE) {
			state.hotkey_catching.reset();
			return true;
		}
		if (slot < Hotkeys::KEYS && !Hotkeys::is_modifier(event.key.key)) {
			m_hotkeys.set_key(action, slot, { event.key.key, event.key.mod });
			state.hotkey_catching.reset();
			keep_hotkeys();
		}
		return true;
	case SDL_EVENT_GAMEPAD_BUTTON_DOWN:
	case SDL_EVENT_GAMEPAD_BUTTON_UP:
	case SDL_EVENT_GAMEPAD_AXIS_MOTION: {
		m_controls.event(event);

		const int combo = m_controls.guide_combo();
		const bool tapped = m_controls.guide_tapped();

		if (slot == Hotkeys::KEYS && (combo >= 0 || tapped)) {
			m_hotkeys.set_pad(action, combo >= 0 ? combo : Hotkeys::GUIDE_ALONE);
			state.hotkey_catching.reset();
			keep_hotkeys();
		} else if (std::find(nav.begin(), nav.end(), MenuNav::BACK) != nav.end()) {
			/* B alone is no shortcut: it lets the catch go */
			state.hotkey_catching.reset();
		}
		return true;
	}
	default:
		return false;
	}
}

/* [hotkeys]: a line for each action not as by default */
void
MenuSession::keep_hotkeys()
{
	for (size_t a = 0; a < Hotkeys::ACTIONS; a++) {
		const auto action = static_cast<Hotkeys::Action>(a);
		const Hotkeys::Binding &binding = m_hotkeys.of(action);
		const std::optional<std::string> line = binding == Hotkeys::standard(action) ? std::nullopt :
		                                        std::optional(Hotkeys::line(binding));

		keep("hotkeys", Hotkeys::id(action), line);
	}
}

/* The input caught takes the place of those of its kind on that part; the
 * next part waiting is caught then */
void
MenuSession::caught(const HostInput &input)
{
	const MenuSlot slot = *m_menu_state.catching;
	const Bindings &bindings = panel_shown().bindings();
	const size_t c = static_cast<size_t>(slot.control);
	std::vector<HostInput> inputs = slot.control < 0 ? bindings.cabinet(slot.role) : bindings.part(c, slot.part);

	m_menu_state.catching.reset();
	std::erase_if(inputs, [&input](const HostInput &in) { return in.kind == input.kind; });
	inputs.push_back(input);
	keep_binding(slot.control < 0 ? bindings.cabinet_line(slot.role, inputs) : bindings.line(c, slot.part, inputs));
	if (!m_menu_state.catch_next.empty()) {
		m_menu_state.catching = m_menu_state.catch_next.front();
		m_menu_state.catch_next.erase(m_menu_state.catch_next.begin());
	}
}

/* Defaults and Clear mapping: the inputs of the device shown, of every
 * part of the controls of the player and their coin (and TEST for player
 * 1), back to their defaults or none */
void
MenuSession::change_inputs(bool to_defaults)
{
	const MenuState &state = m_menu_state;
	const auto changed = [&](const std::vector<HostInput> &inputs, const std::vector<HostInput> &standard) {
		return kind_changed(inputs, standard, to_defaults);
	};
	std::vector<Bindings::Line> lines;
	{
		const Bindings &bindings = panel_shown().bindings();
		const std::vector<GameControl> &controls = panel_shown().controls();

		for (size_t c = 0; c < controls.size(); c++) {
			if (controls[c].player != state.player)
				continue;
			const bool parts = controls[c].type != GameControl::Type::BUTTON;

			for (unsigned part = parts ? 1 : 0; part <= (parts ? 2u : 0u); part++)
				lines.push_back(bindings.line(c, part, changed(bindings.part(c, part), bindings.standard(c, part))));
			if (controls[c].type == GameControl::Type::ANALOG && GameControl::half(controls[c].low, false))
				lines.push_back(bindings.line(c, 0, changed(bindings.part(c, 0), bindings.standard(c, 0))));
		}
		for (const Role role : { state.player ? Role::COIN2 : Role::COIN1, Role::TEST }) {
			if (role == Role::TEST && state.player)
				continue;
			lines.push_back(bindings.cabinet_line(role, changed(bindings.cabinet(role), bindings.cabinet_standard(role))));
		}
	}
	for (const Bindings::Line &line : lines)
		keep_binding(line);
}

/* The inputs of a part without those of the device shown, or with those
 * of its defaults */
std::vector<HostInput>
MenuSession::kind_changed(std::vector<HostInput> inputs, const std::vector<HostInput> &standard, bool to_defaults) const
{
	const auto kind = m_menu_state.on_pad() ? HostInput::Kind::PAD : HostInput::Kind::KEY;

	std::erase_if(inputs, [kind](const HostInput &in) { return in.kind == kind; });
	if (to_defaults)
		for (const HostInput &in : standard)
			if (in.kind == kind)
				inputs.push_back(in);
	return inputs;
}

/* X and Y of a gamepad on the part picked: its inputs of the device shown
 * cleared, or back to their defaults */
void
MenuSession::change_part(const MenuSlot &slot, bool to_defaults)
{
	const Bindings &bindings = panel_shown().bindings();

	if (slot.control < 0) {
		keep_binding(bindings.cabinet_line(slot.role, kind_changed(bindings.cabinet(slot.role),
		                                                          bindings.cabinet_standard(slot.role), to_defaults)));
		return;
	}

	const size_t c = static_cast<size_t>(slot.control);

	keep_binding(bindings.line(c, slot.part, kind_changed(bindings.part(c, slot.part), bindings.standard(c, slot.part),
	                                                      to_defaults)));
}

/* The inputs back as Clear mapping found them */
void
MenuSession::undo()
{
	const std::string section = "controls." + m_cleared_game;
	std::vector<std::string> keys;

	for (const auto &[key, value] : m_settings.section(section))
		keys.push_back(key);
	for (const auto &[key, value] : m_cleared)
		keys.push_back(key);
	for (const std::string &key : keys) {
		std::optional<std::string> was;

		for (const auto &[k, value] : m_cleared)
			if (k == key)
				was = value;
		keep_binding({ key, was }, m_cleared_game);
	}
	m_undo_until = 0;
}

void
MenuSession::keep_binding(const Bindings::Line &line, const std::string &game)
{
	const std::string section = "controls." + (game.empty() ? game_shown() : game);

	keep(section, line.key, line.value);
	m_controls.rebind(m_settings);
	if (m_other)
		m_other.emplace(m_other_controls, m_settings, game_shown());
}

} /* namespace odin */
