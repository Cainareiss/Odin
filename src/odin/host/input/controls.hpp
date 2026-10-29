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

#ifndef ODIN_CONTROLS_HPP
#define ODIN_CONTROLS_HPP

#include <array>
#include <memory>
#include <optional>
#include <span>
#include <utility>
#include <vector>

#include <SDL3/SDL.h>

#include "odin/host/input/bindings.hpp"
#include "odin/host/input/control_panel.hpp"
#include "odin/host/app/games.hpp"
#include "odin/machine/boards/hikaru/hikaru_host.hpp"
#include "odin/host/input/keyboard.hpp"
#include "odin/host/video/picture.hpp"
#include "odin/host/input/pointers.hpp"
#include "odin/host/input/seats.hpp"
#include "odin/host/input/wheel_force.hpp"

namespace odin {

/* The control panel of the cabinet as the host works it: the keyboard, a
 * gamepad and a joystick that is no gamepad (a wheel) for each player and
 * the mouse, once it has moved on the window
 * more than MOUSE_MOVED pixels from where the system first put it (the
 * system tells where it is when the window opens under it). The gamepads
 * are opened and closed as they are plugged in and out; each player has
 * the one they want for the game ([players.<game>] of the settings, "p1 =
 * <GUID> <name>": the first of that make and model), else the first free
 * one, and keeps it while others come and go (seats.hpp); a joystick the
 * same ("joy1 = <GUID> <name>"). SDL's own driver of the Logitech wheels
 * is kept off: it would turn the centring spring of the wheel off and set
 * its rotation as soon as it sees it (SDL 3.4, Linux); the wheel stays
 * with the driver of the system. ODIN_CONTROLLERS=none leaves the
 * gamepads, the joysticks and the mouse out. In a
 * game where player 1 has a sight, the pointer of the system is hidden on
 * the window. */
class Controls : public HikaruPanel {
public:
	/* ODIN_CONTROLLERS=none: no gamepad, joystick, mouse nor light gun is read (the tests) */
	static bool all_off();
	Controls(Keyboard &keys, const std::vector<GameControl> &controls, const std::vector<GameOutput> &outputs,
	         const Settings &settings, const std::string &game);
	~Controls();
	Controls(const Controls &) = delete;
	Controls &operator=(const Controls &) = delete;

	/* An event of a gamepad, of a joystick or of the mouse; false for any
	 * other. */
	bool event(const SDL_Event &event);
	/* The mouse aims in the game (a sight of player 1, in light gun mode):
	 * no pointer */
	bool aims() const { return m_sight && m_light_gun_mode; }
	/* The light gun mode ([players.<game>] light_gun_mode): off, the mouse
	 * is a pointer again and does nothing in the game; the pointer follows
	 * at the next follow_cursor (the menu shows it while it is open) */
	void set_light_gun_mode(bool on) { m_light_gun_mode = on; }
	bool has_sight() const { return m_sight; }
	void follow_cursor();
	/* The mouse is read where the picture is: in the hole of the bezel
	 * when there is one (picture.hpp) */
	void set_picture_hole(const PictureHole &hole) { m_hole = hole; }
	/* Once a frame, before it runs. */
	void update();
	void follow_guns();
	/* While the menu is open: what was let go of is let go of, as a frame
	 * would */
	void settle();

	/* The panel of the game and its inputs (control_panel.hpp), made
	 * again when they change in the settings */
	const ControlPanel &panel() const { return m_panel; }
	void rebind();
	/* The dead zone and the trigger threshold of each gamepad read again
	 * from the settings (control_panel.hpp: PadTuning) */
	void retune();
	/* Guide of a gamepad pressed and let go alone since the last call:
	 * the menu (with another button, Guide is a shortcut) */
	bool guide_tapped() { return std::exchange(m_guide_tapped, false); }
	/* The button pressed with Guide held since the last call: a shortcut
	 * (-1: none) */
	int guide_combo() { return std::exchange(m_guide_combo, -1); }
	/* The gamepad of a player: its name, its kind (pad_drawing.hpp: its
	 * SDL_GamepadType, or the Steam Controller); empty and unknown when
	 * there is none */
	std::string pad_name(unsigned player) const;
	int pad_type(unsigned player) const;
	/* What it does now (nothing: none) */
	PadState pad_state(unsigned player) const { return seat(player) ? seat(player)->state : PadState(); }
	/* What the host holds now: the keys, the gamepad of each player, the
	 * mouse */
	HostState host_state() const { return host(); }
	/* The gamepads plugged in, in the order they came: name, GUID, kind
	 * (pad_drawing.hpp), id, the player it is the pad of (-1: none), by a
	 * wire or not (as SDL knows it) */
	struct Plugged {
		std::string name, guid;
		int kind;
		SDL_JoystickID id;
		int player;
		SDL_JoystickConnectionState connection;
	};
	std::vector<Plugged> plugged() const;
	/* The gamepad a player wants: its GUID, empty for the first free one,
	 * and the one picked of that make (0: the first free one); it takes
	 * its seat at once */
	void want(unsigned player, const std::string &guid, SDL_JoystickID picked = 0);
	/* The joysticks plugged in that are no gamepads, in the order they
	 * came, as the gamepads (kind: their SDL_JoystickType), and the one a
	 * player wants */
	std::vector<Plugged> joysticks() const;
	void want_joystick(unsigned player, const std::string &guid, SDL_JoystickID picked = 0);
	/* The id of the joystick of a player (0: none) */
	SDL_JoystickID joystick_id(unsigned player) const { return joystick(player) ? m_joy_seats[player & 1] : 0; }
	/* The kind of device a player plays with (seats.hpp): it alone shakes,
	 * and turns if it is the wheel; the one they chose
	 * ([players.<game>] device1, device2) if they have it */
	InputDevice device(unsigned player) const
	{
		return device_of(m_chosen[player & 1], m_steers, seat(player) != nullptr, joystick(player) != nullptr);
	}
	void choose_device(unsigned player, InputDevice device) { m_chosen[player & 1] = device; }

	void read_inputs(CabinetInputs &inputs) override
	{
		m_panel.fill(inputs);
		for (const JvsSwitch &sw : m_grounded)
			inputs.jvs_player[sw.player & 1] |= sw.mask;
	}
	/* The outputs of the board after a frame: the pad of a player shakes
	 * while a rumble output of that player is on, when it feels them; the
	 * pad of player 1 also
	 * as hard as the vibration device the sound of the optional board
	 * feeds, when it feels it and no mute output is on, or as the shocks
	 * the drive board is told, when it feels them. The wheel of player 1,
	 * when it has force feedback, turns with the torque of the drive board
	 * (`drive_torque`, when the board runs its program), else with the
	 * force of the command the game gives it. */
	void show(std::span<const uint8_t> outputs, std::span<const int16_t> option_sound,
	          std::optional<int> drive_torque = std::nullopt);
	/* No pad shakes and the wheel lets go (the emulation stops); the light
	 * guns are the desktop's again */
	void quiet();
	/* The light gun of each player ([players.<game>] gun1 and gun2,
	 * "<identity> <name>"), read by those pointers (nullptr: none) */
	bool wants_guns() const { return !m_guns[0].empty() || !m_guns[1].empty(); }
	void set_pointers(Pointers *pointers) { m_pointers = pointers; }
	Pointers *pointers() const { return m_pointers; }
	/* The light gun a player wants now (its identity; empty: none) */
	void want_gun(unsigned player, const std::string &identity) { m_guns[player & 1] = identity; }
	/* The pads feel the rumble outputs (the nozzle of Brave): unless the
	 * user turns the vibration off */
	void feel_outputs(bool on) { m_rumble = on; }
	/* EXPERIMENTAL: the pad of player 1 feels the sound of the optional
	 * board, as the vibration device of the cabinet it feeds */
	void feel_option_board(bool on) { m_felt = on; }
	/* The drive board, TX0 on that output (0: none): the wheel of player 1
	 * turns as it is told; EXPERIMENTAL, the pad of player 1 feels the
	 * shocks among those forces, not what the moves of the wheel give; the
	 * stronger of the two */
	void feel_drive_board(unsigned first_output)
	{
		m_drive_output = first_output;
		m_force.reset();
	}
	/* How hard the wheel turns, in % of what the drive board asks
	 * ([players.<game>] wheel_force, options.hpp) */
	void set_wheel_force(int percent) { m_wheel_force = percent; }
	/* How hard the pads shake, in % of the strength of each shake
	 * ([players.<game>] vibration_force, options.hpp) */
	void set_vibration_force(int percent) { m_vibration_force = percent; }
	/* How far a wheel of the host turns for the steering of the game
	 * ([players.<game>] wheel_rotation, options.hpp) */
	void set_wheel_rotation(int degrees)
	{
		m_wheel_rotation = degrees;
		m_panel.set_wheel_rotation(degrees);
	}
	/* The switches the wiring of the cabinet ties to the ground
	 * (Game::grounded): closed for the game whatever the player does */
	void ground(std::vector<JvsSwitch> switches) { m_grounded = std::move(switches); }
	/* All three as the cabinet of that game shakes, or none (`on` false) */
	void feel_game(const Game &game, bool on)
	{
		feel_option_board(on && game.option_vibration);
		feel_drive_board(on ? game.drive_first_output : 0);
		feel_outputs(on);
	}

	static constexpr int MOUSE_MOVED = 2;
	/* The strength of both motors of a pad that shakes (the nozzle of
	 * Brave has one motor, on or off: a choice of the emulator), and how
	 * often a shake is asked again while it lasts (an effect has an end). */
	static constexpr Uint16 RUMBLE = 0xC000;
	static constexpr Uint32 RUMBLE_MS = 10000;
	static constexpr Uint32 RUMBLE_AGAIN_MS = 5000;
	/* The strength a felt sound changes the shake by, at the least, and
	 * how long a shake lasts before it changes again */
	static constexpr Uint16 FELT_STEP = 0x1000;
	static constexpr Uint32 FELT_MS = 50;

private:
	/* A pad and its buttons: a button pressed and released before a frame
	 * saw it stays pressed until that frame has run. */
	struct Pad {
		SDL_Gamepad *handle = nullptr;
		SDL_JoystickID id = 0;
		std::string guid;
		std::array<int16_t, SDL_GAMEPAD_AXIS_COUNT> axes{};	/* as SDL gives them */
		PadTuning tuning;
		PadState state;
		std::array<bool, SDL_GAMEPAD_BUTTON_COUNT> fresh{};
		std::array<bool, SDL_GAMEPAD_BUTTON_COUNT> release{};
		Uint16 rumbling = 0;	/* how hard it shakes */
		Uint64 rumbled = 0;	/* when it was last asked to */
		bool guide_alone = false;	/* Guide held, no other button since */
	};

	/* A joystick, all of its way: its buttons as those of a pad, a way of
	 * a hat taken within a frame on until that frame has run */
	struct Joy {
		SDL_Joystick *handle = nullptr;
		SDL_JoystickID id = 0;
		std::string guid;
		JoyState state;
		std::vector<bool> fresh, release;
		std::vector<uint8_t> hats;	/* as SDL gives them */
	};

	/* The wheel of player 1 turns with the drive board, when it can */
	void turn_wheel(int torque);
	/* What a pad gives the game of its axes */
	static void shape(Pad &pad);
	void open(SDL_JoystickID id);
	void close(SDL_JoystickID id);
	void open_joystick(SDL_JoystickID id);
	void close_joystick(SDL_JoystickID id);
	Pad *find(SDL_JoystickID id);
	Pad *seat(unsigned player);
	const Pad *seat(unsigned player) const;
	Joy *find_joystick(SDL_JoystickID id);
	const Joy *joystick(unsigned player) const;
	/* Each player takes the gamepad and the joystick seats.hpp gives them */
	void reseat();
	HostState host() const;

	Keyboard &m_keys;
	const Settings &m_settings;	/* the session's, which the menu changes */
	std::vector<GameControl> m_game_controls;
	std::string m_game;
	ControlPanel m_panel;
	std::vector<GameOutput> m_outputs;
	bool m_on;
	bool m_cursor_hidden;	/* the mouse aims in the game: no cursor */
	bool m_sight = false;	/* player 1 has a sight */
	bool m_light_gun_mode = true;
	bool m_guide_tapped = false;
	int m_guide_combo = -1;
	bool m_rumble = true;
	bool m_felt = false;
	Uint16 m_felt_level = 0;	/* how hard the felt sound shakes, smoothed */
	unsigned m_drive_output = 0;
	Uint16 m_drive_level = 0;	/* how hard the felt shocks shake */
	int m_force_was = 0;		/* the force of the last frame */
	int m_wheel_force = 100;	/* the wheel turns at that % of it */
	int m_vibration_force = 100;	/* the pads shake at that % of it */
	int m_wheel_rotation = 0;	/* the degrees of a wheel for the steering, 0: its whole way */
	bool m_wheel_reversed = false;	/* its low values on the right */
	bool m_steers = false;	/* the game has a steering wheel: a wheel can be seated */
	std::array<std::optional<InputDevice>, 2> m_chosen{};	/* the device each player chose, if any */
	std::vector<JvsSwitch> m_grounded;
	int m_wheel_channel = -1;	/* the steering wheel of player 1, if any */
	uint16_t m_wheel_was = 0;
	std::array<double, 6> m_wheel_moves{};	/* its moves over the last frames */
	size_t m_wheel_move = 0;
	std::vector<Pad> m_pads;	/* plugged in, in the order they came */
	std::array<SDL_JoystickID, 2> m_seats{};	/* that of each player, 0: none */
	std::array<std::string, 2> m_wanted;
	std::array<SDL_JoystickID, 2> m_picked{};	/* the very one, 0: any of that make */
	std::vector<Joy> m_joys;	/* plugged in, in the order they came */
	std::array<SDL_JoystickID, 2> m_joy_seats{};
	std::array<std::string, 2> m_joys_wanted;
	std::array<SDL_JoystickID, 2> m_joys_picked{};
	/* The force of the wheel of player 1, made at its first torque (none:
	 * not tried yet, or the joystick changed) */
	std::unique_ptr<WheelForce> m_force;
	/* The light gun of each player: its identity, where it aims and its
	 * buttons, whether it has aimed */
	Pointers *m_pointers = nullptr;
	std::array<std::string, 2> m_guns;
	std::array<MouseState, 2> m_gun_aims{};
	std::array<bool, 2> m_gun_seen{};
	MouseState m_mouse;
	bool m_mouse_seen;		/* it has moved on the window */
	bool m_mouse_placed;		/* the system has told where it is */
	PictureHole m_hole;
	int m_mouse_from_x, m_mouse_from_y;	/* where */
	std::array<bool, 3> m_mouse_fresh;
	std::array<bool, 3> m_mouse_release;
};

} /* namespace odin */

#endif /* ODIN_CONTROLS_HPP */
