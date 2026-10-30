/*
 *  ██████╗ ██████╗ ██╗███╗   ██╗
 * ██╔═══██╗██╔══██╗██║████╗  ██║
 * ██║   ██║██║  ██║██║██╔██╗ ██║
 * ██║   ██║██║  ██║██║██║╚██╗██║
 * ╚██████╔╝██████╔╝██║██║ ╚████║
 *  ╚═════╝ ╚═════╝ ╚═╝╚═╝  ╚═══╝
 *
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

#include "odin/host/input/controls.hpp"

#include "odin/host/audio/audio_out.hpp"
#include "odin/host/app/options.hpp"
#include "odin/machine/cabinet/drive_board.hpp"
#include "odin/host/ui/pad_drawing.hpp"
#include "odin/host/video/picture.hpp"
#include "odin/host/input/seats.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace odin {

namespace {

/* The other lines of [players.<game>]: the keys others read (main.cpp,
 * menu_session.cpp) and whether their value is right */
bool
player_value_right(const std::string &key, const std::string &value)
{
	if (key == "cabinet" || key == "card")
		return true;
	if (key == "vibration")
		return on_off_named(value).has_value();
	if (key == "wheel_force")
		return wheel_force_named(value).has_value();
	if (key == "vibration_force")
		return vibration_force_named(value).has_value();
	if (key == "wheel_rotation")
		return wheel_rotation_named(value).has_value();
	return false;
}

/* A GUID of SDL as text: 32 hexadecimal figures */
std::string
guid_text(SDL_GUID guid)
{
	char text[33];

	SDL_GUIDToString(guid, text, sizeof(text));
	return text;
}

} /* namespace */

bool
Controls::all_off()
{
	const char *const chosen = std::getenv("ODIN_CONTROLLERS");

	return chosen && !std::strcmp(chosen, "none");
}

Controls::Controls(Keyboard &keys, const std::vector<GameControl> &controls, const std::vector<GameOutput> &outputs,
                   const Settings &settings, const std::string &game)
	: m_keys(keys), m_settings(settings), m_game_controls(controls), m_game(game), m_panel(controls, settings, game), m_outputs(outputs), m_on(false), m_cursor_hidden(false), m_mouse_seen(false),
	  m_mouse_placed(false), m_mouse_from_x(0), m_mouse_from_y(0), m_mouse_fresh{}, m_mouse_release{}
{
	for (const GameControl &control : controls) {
		m_steers = m_steers || control.role == Role::STEER;
		if (control.type == GameControl::Type::ANALOG && control.role == Role::STEER && control.player == 0) {
			m_wheel_channel = control.analog;
			m_wheel_reversed = control.low == GameControl::Low::RIGHT;
		}
	}
	if (all_off())
		return;
	for (const auto &[key, value] : settings.section("players." + game)) {
		if (key == "p1" || key == "p2") {
			m_wanted[key[1] - '1'] = value.substr(0, value.find(' '));
		} else if (key == "gun1" || key == "gun2") {
			m_guns[key[3] - '1'] = value.substr(0, value.find(' '));
		} else if (key == "joy1" || key == "joy2") {
			m_joys_wanted[key[3] - '1'] = value.substr(0, value.find(' '));
		} else if ((key == "device1" || key == "device2") && device_named(value)) {
			m_chosen[key[6] - '1'] = device_named(value);
		} else if (key == "light_gun_mode" && on_off_named(value)) {
			m_light_gun_mode = *on_off_named(value);
		} else if (!player_value_right(key, value)) {
			refuse_setting("players." + game, key, value);
		}
	}
	/* The pads and joysticks already there come as added events; the
	 * environment may still turn SDL's driver of the Logitech wheels on */
	SDL_SetHint(SDL_HINT_JOYSTICK_HIDAPI_LG4FF, "0");
	if (!SDL_InitSubSystem(SDL_INIT_GAMEPAD)) {
		std::fprintf(stderr, "ERROR: no gamepads: %s\n", SDL_GetError());
		return;
	}
	m_on = true;
	m_sight = sights_of(controls)[0];
	follow_cursor();
}

/* No pointer while the mouse aims */
void
Controls::follow_cursor()
{
	if (!m_on || aims() == m_cursor_hidden)
		return;
	m_cursor_hidden = aims();
	if (m_cursor_hidden)
		SDL_HideCursor();
	else
		SDL_ShowCursor();
}

/* The next game, or the window without one, finds the cursor as it was */
Controls::~Controls()
{
	if (m_cursor_hidden)
		SDL_ShowCursor();
	if (!m_on)
		return;
	m_force.reset();
	for (Pad &pad : m_pads)
		SDL_CloseGamepad(pad.handle);
	for (Joy &joy : m_joys)
		SDL_CloseJoystick(joy.handle);
	SDL_QuitSubSystem(SDL_INIT_GAMEPAD);
}

Controls::Pad *
Controls::find(SDL_JoystickID id)
{
	for (Pad &pad : m_pads)
		if (id && pad.id == id)
			return &pad;
	return nullptr;
}

Controls::Pad *
Controls::seat(unsigned player)
{
	return find(m_seats[player & 1]);
}

const Controls::Pad *
Controls::seat(unsigned player) const
{
	for (const Pad &pad : m_pads)
		if (m_seats[player & 1] && pad.id == m_seats[player & 1])
			return &pad;
	return nullptr;
}

Controls::Joy *
Controls::find_joystick(SDL_JoystickID id)
{
	for (Joy &joy : m_joys)
		if (id && joy.id == id)
			return &joy;
	return nullptr;
}

const Controls::Joy *
Controls::joystick(unsigned player) const
{
	for (const Joy &joy : m_joys)
		if (m_joy_seats[player & 1] && joy.id == m_joy_seats[player & 1])
			return &joy;
	return nullptr;
}

void
Controls::reseat()
{
	std::vector<OpenPad> pads, joys;

	for (const Pad &pad : m_pads)
		pads.push_back({ pad.id, pad.guid });
	/* A wheel is for a game that steers with one */
	for (const Joy &joy : m_joys)
		if (m_steers || SDL_GetJoystickType(joy.handle) != SDL_JOYSTICK_TYPE_WHEEL)
			joys.push_back({ joy.id, joy.guid });

	const std::array<SDL_JoystickID, 2> seats = seat_pads(pads, m_wanted, m_picked, m_seats);
	const std::array<SDL_JoystickID, 2> joy_seats = seat_pads(joys, m_joys_wanted, m_joys_picked, m_joy_seats);

	for (unsigned player = 0; player < 2; player++) {
		if (joy_seats[player] == m_joy_seats[player])
			continue;
		if (player == 0)
			m_force.reset();
		m_joy_seats[player] = joy_seats[player];
		if (const Joy *const now = joystick(player))
			std::printf("joystick: '%s' for player %u\n", SDL_GetJoystickName(now->handle), player + 1);
	}

	for (unsigned player = 0; player < 2; player++) {
		if (seats[player] == m_seats[player])
			continue;
		/* A pad that leaves a player stops shaking */
		if (Pad *const was = seat(player); was && was->rumbling) {
			SDL_RumbleGamepad(was->handle, 0, 0, 0);
			was->rumbling = 0;
		}
		m_seats[player] = seats[player];
		if (const Pad *const now = seat(player))
			std::printf("pad: '%s' for player %u\n", SDL_GetGamepadName(now->handle), player + 1);
	}
}

std::vector<Controls::Plugged>
Controls::plugged() const
{
	std::vector<Plugged> out;

	for (const Pad &pad : m_pads) {
		const char *const name = SDL_GetGamepadName(pad.handle);

		out.push_back({ name ? name : "", pad.guid,
		                pad_kind(SDL_GetGamepadType(pad.handle), SDL_GetGamepadVendor(pad.handle),
		                         SDL_GetGamepadProduct(pad.handle)),
		                pad.id, pad.id == m_seats[0] ? 0 : pad.id == m_seats[1] ? 1 : -1,
		                SDL_GetGamepadConnectionState(pad.handle) });
	}
	return out;
}

void
Controls::want(unsigned player, const std::string &guid, SDL_JoystickID picked)
{
	m_wanted[player & 1] = guid;
	m_picked[player & 1] = picked;
	if (picked && m_picked[~player & 1] == picked)
		m_picked[~player & 1] = 0;
	reseat();
}

std::vector<Controls::Plugged>
Controls::joysticks() const
{
	std::vector<Plugged> out;

	for (const Joy &joy : m_joys) {
		const char *const name = SDL_GetJoystickName(joy.handle);

		out.push_back({ name ? name : "", joy.guid, static_cast<int>(SDL_GetJoystickType(joy.handle)), joy.id,
		                joy.id == m_joy_seats[0] ? 0 : joy.id == m_joy_seats[1] ? 1 : -1,
		                SDL_GetJoystickConnectionState(joy.handle) });
	}
	return out;
}

void
Controls::want_joystick(unsigned player, const std::string &guid, SDL_JoystickID picked)
{
	m_joys_wanted[player & 1] = guid;
	m_joys_picked[player & 1] = picked;
	if (picked && m_joys_picked[~player & 1] == picked)
		m_joys_picked[~player & 1] = 0;
	reseat();
}

void
Controls::open(SDL_JoystickID id)
{
	SDL_Gamepad *const handle = SDL_OpenGamepad(id);

	if (!handle) {
		std::fprintf(stderr, "ERROR: gamepad %u: %s\n", static_cast<unsigned>(id), SDL_GetError());
		return;
	}
	if (find(id)) {
		SDL_CloseGamepad(handle);
		return;
	}

	Pad pad;

	pad.handle = handle;
	pad.id = id;
	pad.guid = guid_text(SDL_GetGamepadGUIDForID(id));
	pad.tuning = PadTuning::of(m_settings, pad.guid, true);
	for (unsigned axis = 0; axis < SDL_GAMEPAD_AXIS_COUNT; axis++)
		pad.axes[axis] = SDL_GetGamepadAxis(handle, static_cast<SDL_GamepadAxis>(axis));
	shape(pad);
	for (unsigned button = 0; button < SDL_GAMEPAD_BUTTON_COUNT; button++)
		pad.state.buttons[button] = SDL_GetGamepadButton(handle, static_cast<SDL_GamepadButton>(button));
	m_pads.push_back(std::move(pad));
	std::printf("pad: '%s' plugged\n", SDL_GetGamepadName(handle));
	reseat();
	if (m_seats[0] != id && m_seats[1] != id)
		std::printf("pad: '%s' waits, both players have one\n", SDL_GetGamepadName(handle));
}

void
Controls::close(SDL_JoystickID id)
{
	Pad *const pad = find(id);

	if (!pad)
		return;
	std::printf("pad: '%s' unplugged\n", SDL_GetGamepadName(pad->handle));
	SDL_CloseGamepad(pad->handle);
	m_pads.erase(m_pads.begin() + (pad - m_pads.data()));
	reseat();
}

/* Its axes where SDL has them, from the value it read when it opened it
 * (evdev); an axis with none waits for its first move, at rest until
 * then */
void
Controls::open_joystick(SDL_JoystickID id)
{
	if (SDL_IsGamepad(id) || find_joystick(id))
		return;

	SDL_Joystick *const handle = SDL_OpenJoystick(id);

	if (!handle) {
		std::fprintf(stderr, "ERROR: joystick %u: %s\n", static_cast<unsigned>(id), SDL_GetError());
		return;
	}

	Joy joy;
	const int axes = std::max(SDL_GetNumJoystickAxes(handle), 0);
	const int buttons = std::max(SDL_GetNumJoystickButtons(handle), 0);
	const int hats = std::max(SDL_GetNumJoystickHats(handle), 0);

	joy.handle = handle;
	joy.id = id;
	joy.guid = guid_text(SDL_GetJoystickGUIDForID(id));
	joy.state.axes.assign(static_cast<size_t>(axes), 0);
	joy.state.known.assign(static_cast<size_t>(axes), false);
	joy.state.rest.assign(static_cast<size_t>(axes), 0);
	for (int axis = 0; axis < axes; axis++) {
		Sint16 first = 0;

		if (SDL_GetJoystickAxisInitialState(handle, axis, &first)) {
			joy.state.axes[static_cast<size_t>(axis)] = joy.state.rest[static_cast<size_t>(axis)] =
				SDL_GetJoystickAxis(handle, axis);
			joy.state.known[static_cast<size_t>(axis)] = true;
		}
	}
	for (int button = 0; button < buttons; button++)
		joy.state.buttons.push_back(SDL_GetJoystickButton(handle, button));
	for (int hat = 0; hat < hats; hat++)
		joy.hats.push_back(SDL_GetJoystickHat(handle, hat));
	joy.state.hats = joy.hats;
	joy.fresh.assign(static_cast<size_t>(buttons), false);
	joy.release.assign(static_cast<size_t>(buttons), false);
	m_joys.push_back(std::move(joy));
	std::printf("joystick: '%s' plugged, %d axes, %d buttons, %d hats\n", SDL_GetJoystickName(handle), axes, buttons,
	            hats);
	reseat();
}

void
Controls::close_joystick(SDL_JoystickID id)
{
	Joy *const joy = find_joystick(id);

	if (!joy)
		return;
	std::printf("joystick: '%s' unplugged\n", SDL_GetJoystickName(joy->handle));
	SDL_CloseJoystick(joy->handle);
	m_joys.erase(m_joys.begin() + (joy - m_joys.data()));
	reseat();
}

namespace {

/* Where a point of the window falls on the picture, and whether it is off it */
void
aim_at(MouseState &aim, int x, int y, int w, int h, const PictureHole &hole)
{
	const PicturePoint point = picture_point(x, y, static_cast<unsigned>(w), static_cast<unsigned>(h), hole);

	aim.x = point.x;
	aim.y = point.y;
	aim.off = point.x < 0.0 || point.x >= 1.0 || point.y < 0.0 || point.y >= 1.0;
}

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
		aim_at(m_mouse, x, y, w, h, m_hole);
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
	case SDL_EVENT_JOYSTICK_ADDED:
		open_joystick(event.jdevice.which);
		return true;
	case SDL_EVENT_JOYSTICK_REMOVED:
		close_joystick(event.jdevice.which);
		return true;
	case SDL_EVENT_JOYSTICK_AXIS_MOTION:
		if (Joy *const joy = find_joystick(event.jaxis.which); joy && event.jaxis.axis < joy->state.axes.size()) {
			if (!joy->state.known[event.jaxis.axis])
				joy->state.rest[event.jaxis.axis] = event.jaxis.value;
			joy->state.axes[event.jaxis.axis] = event.jaxis.value;
			joy->state.known[event.jaxis.axis] = true;
		}
		return true;
	case SDL_EVENT_JOYSTICK_BUTTON_DOWN:
	case SDL_EVENT_JOYSTICK_BUTTON_UP:
		if (Joy *const joy = find_joystick(event.jbutton.which); joy && event.jbutton.button < joy->fresh.size()) {
			const size_t b = event.jbutton.button;
			bool held = joy->state.buttons[b], fresh = joy->fresh[b], release = joy->release[b];

			press(held, fresh, release, event.jbutton.down);
			joy->state.buttons[b] = held;
			joy->fresh[b] = fresh;
			joy->release[b] = release;
		}
		return true;
	case SDL_EVENT_JOYSTICK_HAT_MOTION:
		if (Joy *const joy = find_joystick(event.jhat.which); joy && event.jhat.hat < joy->hats.size()) {
			joy->hats[event.jhat.hat] = event.jhat.value;
			joy->state.hats[event.jhat.hat] |= event.jhat.value;
		}
		return true;
	case SDL_EVENT_GAMEPAD_ADDED:
		open(event.gdevice.which);
		return true;
	case SDL_EVENT_GAMEPAD_REMOVED:
		close(event.gdevice.which);
		return true;
	case SDL_EVENT_GAMEPAD_AXIS_MOTION:
		if (Pad *const pad = find(event.gaxis.which); pad && event.gaxis.axis < SDL_GAMEPAD_AXIS_COUNT) {
			pad->axes[event.gaxis.axis] = event.gaxis.value;
			shape(*pad);
		}
		return true;
	case SDL_EVENT_GAMEPAD_BUTTON_DOWN:
	case SDL_EVENT_GAMEPAD_BUTTON_UP:
		if (Pad *const pad = find(event.gbutton.which); pad && event.gbutton.button < SDL_GAMEPAD_BUTTON_COUNT) {
			const unsigned button = event.gbutton.button;

			press(pad->state.buttons[button], pad->fresh[button], pad->release[button], event.gbutton.down);
			if (event.gbutton.down && button != SDL_GAMEPAD_BUTTON_GUIDE &&
			    pad->state.buttons[SDL_GAMEPAD_BUTTON_GUIDE]) {
				pad->state.shortcut[button] = true;
				m_guide_combo = static_cast<int>(button);
			}
			if (!pad->state.buttons[button])
				pad->state.shortcut[button] = false;
			if (button != SDL_GAMEPAD_BUTTON_GUIDE)
				pad->guide_alone = pad->guide_alone && !event.gbutton.down;
			else if (event.gbutton.down)
				pad->guide_alone = true;
			else if (std::exchange(pad->guide_alone, false))
				m_guide_tapped = true;
		}
		return true;
	default:
		return false;
	}
}

void
Controls::turn_wheel(int torque)
{
	const Joy *const joy = joystick(0);

	if (!joy || !m_drive_output || !m_rumble || device(0) != InputDevice::JOYSTICK) {
		if (m_force)
			m_force->release();
		return;
	}
	if (!m_force)
		m_force = std::make_unique<WheelForce>(joy->handle);
	/* A wheel read the other way (its low values on the right) turns the
	 * other way: the program of NASCAR reverses both the wheel it reads and
	 * the force it asks on a Deluxe, and keeps its centring spring, so the
	 * potentiometer and the motor of that cabinet are reversed together */
	const double way = m_wheel_reversed ? -1.0 : 1.0;

	m_force->push(way * torque / DriveBoard::MOST * m_wheel_force / WHEEL_FORCE);
}

void
Controls::show(std::span<const uint8_t> outputs, std::span<const int16_t> option_sound, std::optional<int> drive_torque)
{
	const Uint64 now = SDL_GetTicks();

	/* A mute output of the cabinet stills its vibration device at once */
	const bool still = any_on(m_outputs, GameOutput::Role::MUTE, outputs);

	m_felt_level = m_felt && !still ? AudioOut::smoothed(m_felt_level, AudioOut::shake_of(option_sound)) : 0;

	/* A shock shakes at once and dies away as a shake does; what the moves
	 * of the wheel give is no shock */
	const int force = m_drive_output ? DriveBoard::force_of(DriveBoard::command(outputs, m_drive_output)) : 0;

	turn_wheel(drive_torque.value_or(force));
	const uint16_t wheel = m_wheel_channel < 0 ? 0 : m_panel.analog_channel(static_cast<size_t>(m_wheel_channel));

	m_wheel_moves[m_wheel_move++ % m_wheel_moves.size()] = std::abs(wheel - m_wheel_was) / 65535.0;
	m_wheel_was = wheel;

	const double moved = *std::max_element(m_wheel_moves.begin(), m_wheel_moves.end());
	const Uint16 jolt = DriveBoard::jolt_of(force - m_force_was, moved);

	m_force_was = force;
	m_drive_level = std::max(jolt, AudioOut::smoothed(m_drive_level, jolt));

	const Uint16 felt = std::max(m_felt_level, m_drive_level) / FELT_STEP * FELT_STEP;

	for (unsigned player = 0; player < 2; player++) {
		Pad *const seated = seat(player);
		/* Only the device the player plays with shakes */
		const bool on = m_rumble && any_on(m_outputs, GameOutput::Role::RUMBLE, outputs, static_cast<int>(player));
		const int shake = device(player) == InputDevice::PAD ? std::max<Uint16>(on ? RUMBLE : 0, player ? 0 : felt) : 0;
		const Uint16 strength = static_cast<Uint16>(std::min(shake * m_vibration_force / VIBRATION_FORCE, 0xFFFF));

		if (!seated)
			continue;

		Pad &pad = *seated;

		if (strength == pad.rumbling && (!strength || now - pad.rumbled < RUMBLE_AGAIN_MS))
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
	if (m_force)
		m_force->release();
	for (Pad &pad : m_pads)
		if (pad.rumbling) {
			SDL_RumbleGamepad(pad.handle, 0, 0, 0);
			pad.rumbling = 0;
		}
	if (m_pointers)
		m_pointers->hold({});
}

HostState
Controls::host() const
{
	HostState host;

	host.keys = &m_keys.held();
	for (unsigned player = 0; player < 2; player++) {
		host.pads[player] = seat(player) ? &seat(player)->state : nullptr;
		host.joys[player] = joystick(player) ? &joystick(player)->state : nullptr;
	}
	/* Out of light gun mode the mouse does nothing in a game with a sight */
	host.mice[0] = m_gun_seen[0]                                   ? &m_gun_aims[0] :
	               m_mouse_seen && (!m_sight || m_light_gun_mode) ? &m_mouse : nullptr;
	host.mice[1] = m_gun_seen[1] ? &m_gun_aims[1] : nullptr;
	return host;
}

void
Controls::rebind()
{
	m_panel = ControlPanel(m_game_controls, m_settings, m_game);
	m_panel.set_wheel_rotation(m_wheel_rotation);
}

void
Controls::retune()
{
	for (Pad &pad : m_pads) {
		pad.tuning = PadTuning::of(m_settings, pad.guid, false);
		shape(pad);
	}
}

void
Controls::shape(Pad &pad)
{
	pad.state.axes = pad.axes;
	pad.tuning.shape(pad.state.axes[SDL_GAMEPAD_AXIS_LEFTX], pad.state.axes[SDL_GAMEPAD_AXIS_LEFTY]);
	pad.tuning.shape(pad.state.axes[SDL_GAMEPAD_AXIS_RIGHTX], pad.state.axes[SDL_GAMEPAD_AXIS_RIGHTY]);
	pad.state.trigger_pressed = pad.tuning.trigger_pressed();
}

std::string
Controls::pad_name(unsigned player) const
{
	const char *const name = seat(player) ? SDL_GetGamepadName(seat(player)->handle) : nullptr;

	return name ? name : "";
}

int
Controls::pad_type(unsigned player) const
{
	SDL_Gamepad *const pad = seat(player) ? seat(player)->handle : nullptr;

	return pad ? pad_kind(SDL_GetGamepadType(pad), SDL_GetGamepadVendor(pad), SDL_GetGamepadProduct(pad)) :
	             SDL_GAMEPAD_TYPE_UNKNOWN;
}

void
Controls::update()
{
	follow_guns();
	m_panel.update(host());
	settle();
}

/* Where the light guns aim on the picture, as the mouse does, and their
 * buttons: one pressed and let go within the frame is down for it; held
 * from the desktop while the game runs */
void
Controls::follow_guns()
{
	if (!m_pointers || !wants_guns()) {
		m_gun_seen = {};
		return;
	}

	std::vector<std::string> held;
	int w = 0, h = 0;

	for (const std::string &gun : m_guns)
		if (!gun.empty())
			held.push_back(gun);
	m_pointers->poll();
	m_pointers->hold(held);
	SDL_GetWindowSizeInPixels(m_pointers->window(), &w, &h);
	for (unsigned player = 0; player < 2; player++) {
		const std::optional<Pointers::Aim> aim =
			m_guns[player].empty() ? std::nullopt : m_pointers->take(m_guns[player]);
		MouseState &now = m_gun_aims[player];

		m_gun_seen[player] = aim.has_value();
		if (!aim)
			continue;

		aim_at(now, static_cast<int>(aim->x), static_cast<int>(aim->y), w, h, m_hole);
		for (unsigned button = 0; button < now.buttons.size(); button++)
			now.buttons[button] = ((aim->buttons | aim->pressed) >> button) & 1;
	}
}

void
Controls::settle()
{
	m_keys.settle();
	for (Pad &pad : m_pads) {
		for (unsigned button = 0; button < SDL_GAMEPAD_BUTTON_COUNT; button++)
			if (pad.release[button])
				pad.state.buttons[button] = pad.state.shortcut[button] = false;
		pad.fresh = {};
		pad.release = {};
	}
	for (Joy &joy : m_joys) {
		for (size_t button = 0; button < joy.release.size(); button++)
			if (joy.release[button])
				joy.state.buttons[button] = false;
		joy.fresh.assign(joy.fresh.size(), false);
		joy.release.assign(joy.release.size(), false);
		joy.state.hats = joy.hats;
	}
	for (unsigned button = 0; button < 3; button++)
		if (m_mouse_release[button])
			m_mouse.buttons[button] = false;
	m_mouse_fresh = {};
	m_mouse_release = {};
}

} /* namespace odin */
