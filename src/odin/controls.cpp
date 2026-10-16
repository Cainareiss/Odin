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

#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace odin {

Controls::Controls(Keyboard &keys, const std::vector<GameControl> &controls, const Settings &settings,
                   const std::string &game)
	: m_keys(keys), m_panel(controls, settings, game), m_on(false)
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

bool
Controls::event(const SDL_Event &event)
{
	if (!m_on)
		return false;
	switch (event.type) {
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

			if (event.cbutton.state == SDL_PRESSED) {
				pad->fresh[button] = !pad->state.buttons[button] || pad->fresh[button];
				pad->state.buttons[button] = true;
				pad->release[button] = false;
			} else if (pad->fresh[button]) {
				pad->release[button] = true;
			} else {
				pad->state.buttons[button] = false;
			}
		}
		return true;
	default:
		return false;
	}
}

void
Controls::update()
{
	HostState host;

	host.keys = &m_keys.held();
	for (unsigned player = 0; player < 2; player++)
		host.pads[player] = m_pads[player].handle ? &m_pads[player].state : nullptr;
	m_panel.update(host);

	m_keys.settle();
	for (Pad &pad : m_pads) {
		for (unsigned button = 0; button < SDL_CONTROLLER_BUTTON_MAX; button++)
			if (pad.release[button])
				pad.state.buttons[button] = false;
		pad.fresh = {};
		pad.release = {};
	}
}

} /* namespace odin */
