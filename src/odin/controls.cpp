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
#include "odin/picture.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace odin {

Controls::Controls(Keyboard &keys, const std::vector<GameControl> &controls, const std::vector<GameOutput> &outputs,
                   const Settings &settings, const std::string &game)
	: m_keys(keys), m_panel(controls, settings, game), m_outputs(outputs), m_on(false), m_mouse_seen(false),
	  m_mouse_placed(false), m_mouse_from_x(0), m_mouse_from_y(0), m_mouse_fresh{}, m_mouse_release{}
{
	const char *const chosen = std::getenv("ODIN_CONTROLLERS");

	if (chosen && !std::strcmp(chosen, "none"))
		return;
	/* The pads already there come as added events. */
	if (SDL_InitSubSystem(SDL_INIT_GAMECONTROLLER)) {
		std::fprintf(stderr, "ERROR: no gamepads: %s\n", SDL_GetError());
		return;
	}
	m_on = true;
	for (const GameControl &control : controls)
		if (control.role == Role::GUN_X && control.player == 0)
			SDL_ShowCursor(SDL_DISABLE);
}

Controls::~Controls()
{
	if (!m_on)
		return;
	for (Pad &pad : m_pads)
		if (pad.handle)
			SDL_GameControllerClose(pad.handle);
	SDL_QuitSubSystem(SDL_INIT_GAMECONTROLLER);
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
Controls::open(int device)
{
	SDL_GameController *const handle = SDL_GameControllerOpen(device);

	if (!handle) {
		std::fprintf(stderr, "ERROR: gamepad %d: %s\n", device, SDL_GetError());
		return;
	}
	const SDL_JoystickID id = SDL_JoystickInstanceID(SDL_GameControllerGetJoystick(handle));
	Pad *free = nullptr;

	for (Pad &pad : m_pads)
		if (!pad.handle && !free)
			free = &pad;
	if (find(id) || !free) {
		if (!free)
			std::printf("pad: '%s' left out, both players have one\n", SDL_GameControllerName(handle));
		SDL_GameControllerClose(handle);
		return;
	}
	*free = Pad{};
	free->handle = handle;
	free->id = id;
	for (unsigned axis = 0; axis < SDL_CONTROLLER_AXIS_MAX; axis++)
		free->state.axes[axis] = SDL_GameControllerGetAxis(handle, static_cast<SDL_GameControllerAxis>(axis));
	for (unsigned button = 0; button < SDL_CONTROLLER_BUTTON_MAX; button++)
		free->state.buttons[button] =
			SDL_GameControllerGetButton(handle, static_cast<SDL_GameControllerButton>(button));
	std::printf("pad: '%s' plugged, player %d\n", SDL_GameControllerName(handle), free == &m_pads[0] ? 1 : 2);
}

void
Controls::close(SDL_JoystickID id)
{
	Pad *const pad = find(id);

	if (!pad)
		return;
	std::printf("pad: '%s' unplugged\n", SDL_GameControllerName(pad->handle));
	SDL_GameControllerClose(pad->handle);
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
	case SDL_MOUSEMOTION: {
		int w = 0, h = 0;

		SDL_GetWindowSize(SDL_GetWindowFromID(event.motion.windowID), &w, &h);
		const PicturePoint point =
			picture_point(event.motion.x, event.motion.y, static_cast<unsigned>(w), static_cast<unsigned>(h));

		m_mouse.x = point.x;
		m_mouse.y = point.y;
		m_mouse.off = point.x < 0.0 || point.x >= 1.0 || point.y < 0.0 || point.y >= 1.0;
		if (!m_mouse_placed) {
			m_mouse_placed = true;
			m_mouse_from_x = event.motion.x;
			m_mouse_from_y = event.motion.y;
		}
		m_mouse_seen = m_mouse_seen || std::abs(event.motion.x - m_mouse_from_x) > MOUSE_MOVED ||
		               std::abs(event.motion.y - m_mouse_from_y) > MOUSE_MOVED;
		return true;
	}
	case SDL_MOUSEBUTTONDOWN:
	case SDL_MOUSEBUTTONUP: {
		const int index = event.button.button == SDL_BUTTON_LEFT ? 0 : event.button.button == SDL_BUTTON_MIDDLE ? 1
		                : event.button.button == SDL_BUTTON_RIGHT ? 2 : -1;

		if (index >= 0)
			press(m_mouse.buttons[index], m_mouse_fresh[index], m_mouse_release[index],
			      event.button.state == SDL_PRESSED);
		return true;
	}
	case SDL_WINDOWEVENT:
		if (event.window.event == SDL_WINDOWEVENT_LEAVE)
			m_mouse.off = true;
		return false;
	case SDL_CONTROLLERDEVICEADDED:
		open(event.cdevice.which);
		return true;
	case SDL_CONTROLLERDEVICEREMOVED:
		close(event.cdevice.which);
		return true;
	case SDL_CONTROLLERAXISMOTION:
		if (Pad *const pad = find(event.caxis.which); pad && event.caxis.axis < SDL_CONTROLLER_AXIS_MAX)
			pad->state.axes[event.caxis.axis] = event.caxis.value;
		return true;
	case SDL_CONTROLLERBUTTONDOWN:
	case SDL_CONTROLLERBUTTONUP:
		if (Pad *const pad = find(event.cbutton.which); pad && event.cbutton.button < SDL_CONTROLLER_BUTTON_MAX) {
			const unsigned button = event.cbutton.button;

			press(pad->state.buttons[button], pad->fresh[button], pad->release[button],
			      event.cbutton.state == SDL_PRESSED);
		}
		return true;
	default:
		return false;
	}
}

void
Controls::show(std::span<const uint8_t> outputs, std::span<const int16_t> option_sound)
{
	const Uint32 now = SDL_GetTicks();

	m_felt_level = m_felt ? AudioOut::smoothed(m_felt_level, AudioOut::shake_of(option_sound)) : 0;

	const Uint16 felt = m_felt_level / FELT_STEP * FELT_STEP;

	for (unsigned player = 0; player < 2; player++) {
		Pad &pad = m_pads[player];
		bool on = false;

		for (const GameOutput &output : m_outputs)
			on = on || (output.role == GameOutput::Role::RUMBLE && output.player == player && output.on(outputs));

		const Uint16 strength = std::max<Uint16>(on ? RUMBLE : 0, player ? 0 : felt);

		if (!pad.handle || (strength == pad.rumbling && (!strength || now - pad.rumbled < RUMBLE_AGAIN_MS)))
			continue;
		/* A shake changes its strength a few times a second at most */
		if (strength && pad.rumbling && strength != pad.rumbling && now - pad.rumbled < FELT_MS)
			continue;
		SDL_GameControllerRumble(pad.handle, strength, strength, strength ? RUMBLE_MS : 0);
		pad.rumbling = strength;
		pad.rumbled = now;
	}
}

void
Controls::quiet()
{
	for (Pad &pad : m_pads)
		if (pad.handle && pad.rumbling) {
			SDL_GameControllerRumble(pad.handle, 0, 0, 0);
			pad.rumbling = 0;
		}
}

void
Controls::update()
{
	HostState host;

	host.keys = &m_keys.held();
	for (unsigned player = 0; player < 2; player++)
		host.pads[player] = m_pads[player].handle ? &m_pads[player].state : nullptr;
	host.mouse = m_mouse_seen ? &m_mouse : nullptr;
	m_panel.update(host);

	m_keys.settle();
	for (Pad &pad : m_pads) {
		for (unsigned button = 0; button < SDL_CONTROLLER_BUTTON_MAX; button++)
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
