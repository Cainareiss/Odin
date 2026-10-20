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

#include "odin/controls.hpp"

#include "odin/audio_out.hpp"
#include "odin/drive_board.hpp"
#include "odin/picture.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace odin {

Controls::Controls(Keyboard &keys, const std::vector<GameControl> &controls, const std::vector<GameOutput> &outputs,
                   const Settings &settings, const std::string &game)
	: m_keys(keys), m_game_controls(controls), m_game(game), m_panel(controls, settings, game), m_outputs(outputs), m_on(false), m_mouse_seen(false),
	  m_mouse_placed(false), m_mouse_from_x(0), m_mouse_from_y(0), m_mouse_fresh{}, m_mouse_release{}
{
	for (const GameControl &control : controls)
		if (control.type == GameControl::Type::ANALOG && control.role == Role::STEER && control.player == 0)
			m_wheel_channel = control.analog;
	const char *const chosen = std::getenv("ODIN_CONTROLLERS");

	if (chosen && !std::strcmp(chosen, "none"))
		return;
	/* The pads already there come as added events. */
	if (!SDL_InitSubSystem(SDL_INIT_GAMEPAD)) {
		std::fprintf(stderr, "ERROR: no gamepads: %s\n", SDL_GetError());
		return;
	}
	m_on = true;
	for (const GameControl &control : controls)
		if (control.role == Role::GUN_X && control.player == 0)
			SDL_HideCursor();
}

Controls::~Controls()
{
	if (!m_on)
		return;
	for (Pad &pad : m_pads)
		if (pad.handle)
			SDL_CloseGamepad(pad.handle);
	SDL_QuitSubSystem(SDL_INIT_GAMEPAD);
}

Controls::Pad *
Controls::find(SDL_JoystickID id)
{
	for (Pad &pad : m_pads)
		if (pad.handle && pad.id == id)
			return &pad;
	return nullptr;
}

void
Controls::open(SDL_JoystickID id)
{
	SDL_Gamepad *const handle = SDL_OpenGamepad(id);

	if (!handle) {
		std::fprintf(stderr, "ERROR: gamepad %u: %s\n", static_cast<unsigned>(id), SDL_GetError());
		return;
	}
	Pad *free = nullptr;

	for (Pad &pad : m_pads)
		if (!pad.handle && !free)
			free = &pad;
	if (find(id) || !free) {
		if (!free)
			std::printf("pad: '%s' left out, both players have one\n", SDL_GetGamepadName(handle));
		SDL_CloseGamepad(handle);
		return;
	}
	*free = Pad{};
	free->handle = handle;
	free->id = id;
	for (unsigned axis = 0; axis < SDL_GAMEPAD_AXIS_COUNT; axis++)
		free->state.axes[axis] = SDL_GetGamepadAxis(handle, static_cast<SDL_GamepadAxis>(axis));
	for (unsigned button = 0; button < SDL_GAMEPAD_BUTTON_COUNT; button++)
		free->state.buttons[button] =
			SDL_GetGamepadButton(handle, static_cast<SDL_GamepadButton>(button));
	std::printf("pad: '%s' plugged, player %d\n", SDL_GetGamepadName(handle), free == &m_pads[0] ? 1 : 2);
}

void
Controls::close(SDL_JoystickID id)
{
	Pad *const pad = find(id);

	if (!pad)
		return;
	std::printf("pad: '%s' unplugged\n", SDL_GetGamepadName(pad->handle));
	SDL_CloseGamepad(pad->handle);
	*pad = Pad{};
}

namespace {

/* A button down or up: one pressed and released before a frame saw it
 * stays pressed until that frame has run. */
void
press(bool &held, bool &fresh, bool &release, bool down)
{
	if (down) {
		fresh = !held || fresh;
		held = true;
		release = false;
	} else if (fresh) {
		release = true;
	} else {
		held = false;
	}
}

} /* namespace */

bool
Controls::event(const SDL_Event &event)
{
	if (!m_on)
		return false;
	switch (event.type) {
	case SDL_EVENT_MOUSE_MOTION: {
		/* SDL 3 gives the place in fractions of a pixel; whole pixels, as SDL 2 did */
		const int x = static_cast<int>(event.motion.x), y = static_cast<int>(event.motion.y);
		int w = 0, h = 0;

		SDL_GetWindowSize(SDL_GetWindowFromID(event.motion.windowID), &w, &h);
		const PicturePoint point = picture_point(x, y, static_cast<unsigned>(w), static_cast<unsigned>(h));

		m_mouse.x = point.x;
		m_mouse.y = point.y;
		m_mouse.off = point.x < 0.0 || point.x >= 1.0 || point.y < 0.0 || point.y >= 1.0;
		if (!m_mouse_placed) {
			m_mouse_placed = true;
			m_mouse_from_x = x;
			m_mouse_from_y = y;
		}
		m_mouse_seen = m_mouse_seen || std::abs(x - m_mouse_from_x) > MOUSE_MOVED ||
		               std::abs(y - m_mouse_from_y) > MOUSE_MOVED;
		return true;
	}
	case SDL_EVENT_MOUSE_BUTTON_DOWN:
	case SDL_EVENT_MOUSE_BUTTON_UP: {
		const int index = event.button.button == SDL_BUTTON_LEFT ? 0 : event.button.button == SDL_BUTTON_MIDDLE ? 1
		                : event.button.button == SDL_BUTTON_RIGHT ? 2 : -1;

		if (index >= 0)
			press(m_mouse.buttons[index], m_mouse_fresh[index], m_mouse_release[index],
			      event.button.down);
		return true;
	}
	case SDL_EVENT_WINDOW_MOUSE_LEAVE:
		m_mouse.off = true;
		return false;
	case SDL_EVENT_GAMEPAD_ADDED:
		open(event.gdevice.which);
		return true;
	case SDL_EVENT_GAMEPAD_REMOVED:
		close(event.gdevice.which);
		return true;
	case SDL_EVENT_GAMEPAD_AXIS_MOTION:
		if (Pad *const pad = find(event.gaxis.which); pad && event.gaxis.axis < SDL_GAMEPAD_AXIS_COUNT)
			pad->state.axes[event.gaxis.axis] = event.gaxis.value;
		return true;
	case SDL_EVENT_GAMEPAD_BUTTON_DOWN:
	case SDL_EVENT_GAMEPAD_BUTTON_UP:
		if (Pad *const pad = find(event.gbutton.which); pad && event.gbutton.button < SDL_GAMEPAD_BUTTON_COUNT) {
			const unsigned button = event.gbutton.button;

			press(pad->state.buttons[button], pad->fresh[button], pad->release[button], event.gbutton.down);
		}
		return true;
	default:
		return false;
	}
}

void
Controls::show(std::span<const uint8_t> outputs, std::span<const int16_t> option_sound)
{
	const Uint64 now = SDL_GetTicks();

	/* A mute output of the cabinet stills its vibration device at once */
	const bool still = any_on(m_outputs, GameOutput::Role::MUTE, outputs);

	m_felt_level = m_felt && !still ? AudioOut::smoothed(m_felt_level, AudioOut::shake_of(option_sound)) : 0;

	/* A shock shakes at once and dies away as a shake does; what the moves
	 * of the wheel give is no shock */
	const int force = m_drive_output ? DriveBoard::force_of(DriveBoard::command(outputs, m_drive_output)) : 0;
	const uint16_t wheel = m_wheel_channel < 0 ? 0 : m_panel.analog_channel(static_cast<size_t>(m_wheel_channel));

	m_wheel_moves[m_wheel_move++ % m_wheel_moves.size()] = std::abs(wheel - m_wheel_was) / 65535.0;
	m_wheel_was = wheel;

	const double moved = *std::max_element(m_wheel_moves.begin(), m_wheel_moves.end());
	const Uint16 jolt = DriveBoard::jolt_of(force - m_force_was, moved);

	m_force_was = force;
	m_drive_level = std::max(jolt, AudioOut::smoothed(m_drive_level, jolt));

	const Uint16 felt = std::max(m_felt_level, m_drive_level) / FELT_STEP * FELT_STEP;

	for (unsigned player = 0; player < 2; player++) {
		Pad &pad = m_pads[player];
		const bool on = any_on(m_outputs, GameOutput::Role::RUMBLE, outputs, static_cast<int>(player));
		const Uint16 strength = std::max<Uint16>(on ? RUMBLE : 0, player ? 0 : felt);

		if (!pad.handle || (strength == pad.rumbling && (!strength || now - pad.rumbled < RUMBLE_AGAIN_MS)))
			continue;
		/* A shake changes its strength a few times a second at most */
		if (strength && pad.rumbling && strength != pad.rumbling && now - pad.rumbled < FELT_MS)
			continue;
		SDL_RumbleGamepad(pad.handle, strength, strength, strength ? RUMBLE_MS : 0);
		pad.rumbling = strength;
		pad.rumbled = now;
	}
}

void
Controls::quiet()
{
	for (Pad &pad : m_pads)
		if (pad.handle && pad.rumbling) {
			SDL_RumbleGamepad(pad.handle, 0, 0, 0);
			pad.rumbling = 0;
		}
}

HostState
Controls::host() const
{
	HostState host;

	host.keys = &m_keys.held();
	for (unsigned player = 0; player < 2; player++)
		host.pads[player] = m_pads[player].handle ? &m_pads[player].state : nullptr;
	host.mouse = m_mouse_seen ? &m_mouse : nullptr;
	return host;
}

void
Controls::rebind(const Settings &settings)
{
	m_panel = ControlPanel(m_game_controls, settings, m_game);
}

std::string
Controls::pad_name(unsigned player) const
{
	const char *const name = m_pads[player & 1].handle ? SDL_GetGamepadName(m_pads[player & 1].handle) : nullptr;

	return name ? name : "";
}

SDL_GamepadType
Controls::pad_type(unsigned player) const
{
	return m_pads[player & 1].handle ? SDL_GetGamepadType(m_pads[player & 1].handle) : SDL_GAMEPAD_TYPE_UNKNOWN;
}

void
Controls::update()
{
	m_panel.update(host());
	settle();
}

void
Controls::settle()
{
	m_keys.settle();
	for (Pad &pad : m_pads) {
		for (unsigned button = 0; button < SDL_GAMEPAD_BUTTON_COUNT; button++)
			if (pad.release[button])
				pad.state.buttons[button] = false;
		pad.fresh = {};
		pad.release = {};
	}
	for (unsigned button = 0; button < 3; button++)
		if (m_mouse_release[button])
			m_mouse.buttons[button] = false;
	m_mouse_fresh = {};
	m_mouse_release = {};
}

} /* namespace odin */
