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

#include "odin/control_panel.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>

namespace odin {

namespace {

GameControl
button(const char *name, const char *id, uint8_t player, uint16_t mask, Role role)
{
	GameControl control{};

	control.name = name;
	control.id = id;
	control.type = GameControl::Type::BUTTON;
	control.button = JvsSwitch{ player, mask };
	control.role = role;
	control.player = player;
	return control;
}

/* START, the directions and PUSH1 to PUSH6 of both players, in the words
 * of MieBoard::Inputs */
std::vector<GameControl>
standard_panel()
{
	static const char *const IDS[2][11] = {
		{ "1p_start", "1p_up", "1p_down", "1p_left", "1p_right", "1p_push1", "1p_push2", "1p_push3", "1p_push4",
		  "1p_push5", "1p_push6" },
		{ "2p_start", "2p_up", "2p_down", "2p_left", "2p_right", "2p_push1", "2p_push2", "2p_push3", "2p_push4",
		  "2p_push5", "2p_push6" },
	};
	static const char *const NAMES[2][11] = {
		{ "1P START", "1P UP", "1P DOWN", "1P LEFT", "1P RIGHT", "1P PUSH1", "1P PUSH2", "1P PUSH3", "1P PUSH4",
		  "1P PUSH5", "1P PUSH6" },
		{ "2P START", "2P UP", "2P DOWN", "2P LEFT", "2P RIGHT", "2P PUSH1", "2P PUSH2", "2P PUSH3", "2P PUSH4",
		  "2P PUSH5", "2P PUSH6" },
	};
	static const uint16_t MASKS[11] = { 0x8000, 0x2000, 0x1000, 0x0800, 0x0400, 0x0200, 0x0100, 0x0080, 0x0040,
	                                    0x0020, 0x0010 };
	static const Role ROLES[11] = { Role::START, Role::UP, Role::DOWN, Role::LEFT, Role::RIGHT, Role::BUTTON1,
	                                Role::BUTTON2, Role::BUTTON3, Role::BUTTON4, Role::BUTTON5, Role::BUTTON6 };
	std::vector<GameControl> panel;

	for (uint8_t player = 0; player < 2; player++)
		for (unsigned i = 0; i < 11; i++)
			panel.push_back(button(NAMES[player][i], IDS[player][i], player, MASKS[i], ROLES[i]));
	return panel;
}

/* Every control of the panel where it rests; a channel no control takes
 * at 8000 */
MieBoard::Inputs
idle_inputs(const std::vector<GameControl> &panel)
{
	MieBoard::Inputs inputs = {};

	for (uint16_t &channel : inputs.jvs_analog)
		channel = 0x8000;
	for (const GameControl &control : panel)
		if (control.type == GameControl::Type::ANALOG)
			inputs.jvs_analog[control.analog] = ControlPanel::rest(control.low);
	return inputs;
}

/* The controls of the game, or the standard panel, and the SERVICE switch
 * of each player */
std::vector<GameControl>
panel_of(const std::vector<GameControl> &controls)
{
	std::vector<GameControl> panel = controls.empty() ? standard_panel() : controls;

	panel.push_back(button("1P SERVICE", GameControl::CABINET_IDS[0], 0, 0x4000, Role::SERVICE));
	panel.push_back(button("2P SERVICE", GameControl::CABINET_IDS[1], 1, 0x4000, Role::SERVICE));
	return panel;
}

} /* namespace */

ControlPanel::ControlPanel(const std::vector<GameControl> &controls, const Settings &settings, const std::string &game)
	: m_controls(panel_of(controls)), m_bindings(m_controls, settings, game), m_inputs(idle_inputs(m_controls)),
	  m_coins_were{}, m_coins{}, m_mouse_aims(false), m_mouse_moved(false), m_mouse_seen(false), m_mouse_x(0.5),
	  m_mouse_y(0.5)
{
	m_positions.assign(m_controls.size(), 0);
	m_levers_were.assign(m_controls.size(), { false, false });
	m_owner.assign(m_controls.size(), 0);
	for (size_t c = 0; c < m_controls.size(); c++) {
		m_keyed.push_back(rest(m_controls[c].low));
		m_kept.emplace_back(m_bindings.of(c).size() + 1, rest(m_controls[c].low));
		m_presence.emplace_back(m_bindings.of(c).size() + 1, NEVER);
	}
}

/* A stick turned over keeps its centre at 8000. */
uint16_t
ControlPanel::channel(const PadInput &input, GameControl::Low low, int16_t value)
{
	const bool over = low == GameControl::Low::RIGHT || low == GameControl::Low::DOWN || low == GameControl::Low::PRESSED;

	if (input.is_trigger()) {
		const int word = std::max<int>(value, 0) * 0xFFFF / 0x7FFF;

		return static_cast<uint16_t>(over ? 0xFFFF - word : word);
	}
	const int toward = input.inverted ? -value : value;

	return static_cast<uint16_t>(std::clamp(over ? 0x8000 - toward : 0x8000 + toward, 0, 0xFFFF));
}

uint16_t
ControlPanel::rest(GameControl::Low low)
{
	if (low == GameControl::Low::RELEASED)
		return 0x0000;
	return low == GameControl::Low::PRESSED ? 0xFFFF : 0x8000;
}

uint16_t
ControlPanel::pointed(GameControl::Low low, double at)
{
	const PadInput axis{ PadInput::Type::AXIS, static_cast<uint8_t>(SDL_GAMEPAD_AXIS_LEFTX) };
	const long value = std::lround(std::clamp(at, 0.0, 1.0) * 65535.0) - 32768;

	return channel(axis, low, static_cast<int16_t>(value));
}

uint16_t
ControlPanel::end(GameControl::Low low, bool positive)
{
	const bool pedal = !GameControl::half(low, positive);
	const PadInput axis{ PadInput::Type::AXIS,
	                     static_cast<uint8_t>(pedal ? SDL_GAMEPAD_AXIS_LEFT_TRIGGER : SDL_GAMEPAD_AXIS_LEFTX) };

	return channel(axis, low, positive ? 32767 : -32768);
}

bool
ControlPanel::held(const HostState &host, const HostInput &input, unsigned player) const
{
	if (input.kind == HostInput::Kind::KEY)
		return host.keys && std::find(host.keys->begin(), host.keys->end(), input.key) != host.keys->end();
	if (input.kind == HostInput::Kind::MOUSE) {
		/* Away from the picture counts while the mouse aims */
		if (!host.mouse)
			return false;
		if (input.mouse.type == MouseInput::Type::OFF)
			return host.mouse->off && m_mouse_aims;
		return input.mouse.type == MouseInput::Type::BUTTON && host.mouse->buttons[input.mouse.button];
	}

	const PadState *const pad = host.pads[player & 1];

	if (!pad)
		return false;
	switch (input.pad.type) {
	case PadInput::Type::BUTTON:
		return pad->buttons[input.pad.index];
	case PadInput::Type::AXIS_NEGATIVE:
		return pad->axes[input.pad.index] <= -PRESSED;
	case PadInput::Type::AXIS_POSITIVE:
		return pad->axes[input.pad.index] >= PRESSED;
	case PadInput::Type::AXIS:
		/* A trigger: the bindings give no whole stick to what is pressed */
		return pad->axes[input.pad.index] >= PRESSED;
	}
	return false;
}

bool
ControlPanel::pressed(const HostState &host, const std::vector<HostInput> &inputs, unsigned player) const
{
	for (const HostInput &input : inputs)
		if (held(host, input, player))
			return true;
	return false;
}

/* The keys take an analog control a step a frame toward the end they push
 * to, or back to rest. Of the keys and the axes bound, the last that moved
 * works it: one that went further than MOVED from where it was last kept
 * takes it, the one that went furthest if more than one did. */
uint16_t
ControlPanel::analog(const HostState &host, size_t c)
{
	const GameControl &control = m_controls[c];
	const bool back = pressed(host, m_bindings.toward(c, false), control.player);
	const bool on = pressed(host, m_bindings.toward(c, true), control.player);
	const int target = back == on ? rest(control.low) : end(control.low, on);
	int keyed = m_keyed[c];

	keyed = std::abs(target - keyed) <= KEY_STEP ? target : keyed + (target > keyed ? KEY_STEP : -KEY_STEP);
	m_keyed[c] = static_cast<uint16_t>(keyed);

	const std::vector<HostInput> &axes = m_bindings.of(c);
	const PadState *const pad = host.pads[control.player];
	std::vector<uint16_t> &kept = m_kept[c];
	std::vector<uint16_t> now(axes.size() + 1, 0);
	std::vector<bool> there(axes.size() + 1, false);
	int moved = MOVED;

	now[0] = m_keyed[c];
	there[0] = true;
	for (size_t s = 1; s < now.size(); s++) {
		const HostInput &input = axes[s - 1];

		if (input.kind == HostInput::Kind::MOUSE && host.mouse) {
			now[s] = pointed(control.low, input.mouse.type == MouseInput::Type::X ? host.mouse->x : host.mouse->y);
			there[s] = true;
		} else if (input.kind == HostInput::Kind::PAD && pad) {
			now[s] = channel(input.pad, control.low, pad->axes[input.pad.index]);
			there[s] = true;
		}
	}
	/* An input back after it was gone (a pad plugged in again) starts
	 * from where it is; one seen for the first time, from rest */
	for (size_t s = 0; s < now.size(); s++) {
		uint8_t &presence = m_presence[c][s];

		if (there[s] && presence == GONE)
			kept[s] = now[s];
		presence = there[s] ? THERE : presence == NEVER ? NEVER : GONE;
	}
	if (!there[m_owner[c]])
		m_owner[c] = 0;
	for (size_t s = 0; s < now.size(); s++) {
		const bool mouse = s > 0 && axes[s - 1].kind == HostInput::Kind::MOUSE;

		if (there[s] && mouse && m_mouse_moved) {
			m_owner[c] = s;
			break;
		}
		if (there[s] && std::abs(now[s] - kept[s]) > moved) {
			moved = std::abs(now[s] - kept[s]);
			m_owner[c] = s;
		}
	}
	kept[m_owner[c]] = now[m_owner[c]];
	return now[m_owner[c]];
}

void
ControlPanel::update(const HostState &host)
{
	MieBoard::Inputs inputs = idle_inputs(m_controls);

	/* The mouse is one pointer: when it moves, it takes every channel it
	 * is bound to */
	m_mouse_moved = host.mouse && (!m_mouse_seen || std::abs(host.mouse->x - m_mouse_x) * 0x10000 > MOVED ||
	                               std::abs(host.mouse->y - m_mouse_y) * 0x10000 > MOVED);
	if (m_mouse_moved) {
		m_mouse_x = host.mouse->x;
		m_mouse_y = host.mouse->y;
	}
	m_mouse_seen = m_mouse_seen || host.mouse;

	inputs.mainboard_test = pressed(host, m_bindings.cabinet(Role::BOARD_TEST), 0);
	inputs.mainboard_service = pressed(host, m_bindings.cabinet(Role::BOARD_SERVICE), 0);
	inputs.jvs_system = pressed(host, m_bindings.cabinet(Role::TEST), 0) ? 0x80 : 0;

	for (size_t c = 0; c < m_controls.size(); c++) {
		const GameControl &control = m_controls[c];

		switch (control.type) {
		case GameControl::Type::BUTTON:
			if (pressed(host, m_bindings.of(c), control.player))
				inputs.jvs_player[control.button.player] |= control.button.mask;
			break;
		case GameControl::Type::POSITIONS: {
			const bool down = pressed(host, m_bindings.lever(c, false), control.player);
			const bool up = pressed(host, m_bindings.lever(c, true), control.player);

			if (down && !m_levers_were[c][0] && m_positions[c] > 0)
				m_positions[c]--;
			if (up && !m_levers_were[c][1] && m_positions[c] + 1 < control.positions.size())
				m_positions[c]++;
			m_levers_were[c] = { down, up };
			for (const JvsSwitch &sw : control.positions[m_positions[c]].switches)
				inputs.jvs_player[sw.player] |= sw.mask;
			break;
		}
		case GameControl::Type::ANALOG:
			inputs.jvs_analog[control.analog] = analog(host, c);
			break;
		}
	}
	/* The mouse aims while it works every channel it is bound to */
	bool works_one = false, works_all = true;

	for (size_t c = 0; c < m_controls.size(); c++) {
		const std::vector<HostInput> &axes = m_bindings.of(c);
		const bool bound = std::any_of(axes.begin(), axes.end(), [](const HostInput &input) {
			return input.kind == HostInput::Kind::MOUSE;
		});

		if (m_controls[c].type != GameControl::Type::ANALOG || !bound)
			continue;
		const bool works = m_owner[c] > 0 && axes[m_owner[c] - 1].kind == HostInput::Kind::MOUSE;

		works_one = works_one || works;
		works_all = works_all && works;
	}
	m_mouse_aims = works_one && works_all;
	for (unsigned slot = 0; slot < 2; slot++) {
		/* A coin of player 2 from the pad of player 2 */
		const bool now = pressed(host, m_bindings.cabinet(slot ? Role::COIN2 : Role::COIN1), slot);

		m_coins[slot] = m_coins[slot] || (now && !m_coins_were[slot]);
		m_coins_were[slot] = now;
	}
	m_inputs = inputs;
}

void
ControlPanel::fill(MieBoard::Inputs &inputs)
{
	inputs = m_inputs;
	for (unsigned slot = 0; slot < 2; slot++) {
		inputs.jvs_coin[slot] = m_coins[slot];
		m_coins[slot] = false;
	}
}

} /* namespace odin */
