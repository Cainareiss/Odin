/*
 *  ██████╗ ██████╗ ██╗███╗   ██╗
 * ██╔═══██╗██╔══██╗██║████╗  ██║
 * ██║   ██║██║  ██║██║██╔██╗ ██║
 * ██║   ██║██║  ██║██║██║╚██╗██║
 * ╚██████╔╝██████╔╝██║██║ ╚████║
 *  ╚═════╝ ╚═════╝ ╚═╝╚═╝  ╚═══╝
 *
 * Copyright (C) 2026 Réda Chérif-Touil
 * Copyright (C) 2026 Daniel Martin
 *
 * Odin is free software: you can redistribute it and/or modify it under
 * the terms of the GNU General Public License as published by the Free
 * Software Foundation, either version 3 of the License, or (at your
 * option) any later version.
 *
 * Odin is distributed in the hope that it will be useful, but WITHOUT ANY
 * WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 * for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with Odin.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "odin/host/app/atvtrack_play.hpp"

#include <algorithm>
#include <array>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <memory>
#include <span>
#include <string>
#include <vector>

#include "odin/host/app/user_files.hpp"
#include "odin/host/audio/audio_out.hpp"
#include "odin/host/input/controls.hpp"
#include "odin/host/ui/menu_session.hpp"
#include "odin/machine/boards/atvtrack/atvtrack.hpp"

namespace odin {

namespace {

/* Panel bits in CabinetInputs */
constexpr uint16_t START = 0x8000, UP = 0x2000, DOWN = 0x1000, LEFT = 0x0800, RIGHT = 0x0400, PUSH1 = 0x0200,
                   PUSH2 = 0x0100, PUSH3 = 0x0080;
constexpr unsigned CHANNEL_TOP = 0xFFFF;
/* Smashing Drive's pedals are switches */
constexpr uint16_t PEDAL_DOWN = 0x4000;
constexpr unsigned COIN_FRAMES = 6;
constexpr int DAC_RATE = 32000;
/* Smashing Drive's volume starts at zero, so raise it on the first power-on.
 * The game saves its settings at frame 227. */
constexpr unsigned VOLUME_FROM = 60, VOLUME_TO = 300;
/* Kick on the right stick: past KICK_FROM, power is the peak within
 * KICK_RISE frames, then back under KICK_REST to rearm. Pulling back first
 * lifts the ball, until the stick rests BACKSWING_FRAMES. */
constexpr unsigned KICK_FRAMES = 3, KICK_RISE = 3, BACKSWING_FRAMES = 20;
constexpr float KICK_FROM = 0.3f, KICK_REST = 0.2f;
/* Kick power is 2 to 127: the root of both side sensors squared, times the
 * gain (0x8C02F5E0). HEURISTIC: a real foot's values are unknown. Up to 50
 * passes and past 80 clears the field, so travel is cubed. */
constexpr float KICK_SOFT = 10.0f, KICK_HARD = 127.0f, KICK_GAIN = 1.2f;

class AtvGame final : public GameBoard {
public:
	AtvGame(const Game &game, Controls &controls, Display &display, const std::string &kept);
	~AtvGame() override;

	const std::string &game_name() const override { return m_name; }
	unsigned frame() const override { return m_board->frame(); }
	void run_frame() override;
	bool restart_asked() const override { return false; }
	bool save_state(const std::string &path) override { return m_board->save_state(path); }
	bool load_state(const std::string &path) override;
	std::span<const uint8_t> io_outputs() const override { return {}; }
	const std::vector<int16_t> &sound(bool optional) const override { return optional ? m_no_sound : m_board->sound(); }
	std::optional<int> drive_torque() const override { return std::nullopt; }

private:
	struct Kick {
		float x = 0.0f, forward = 0.0f, back = 0.0f;
		unsigned rise = 0, frames = 0, rested = 0;
		bool spent = false;
		AtvTrack::BallSensors::value_type sensors = AtvTrack::BALL_AT_REST;
	};

	void read_controls();
	void read_kick(const CabinetInputs &in, unsigned player);

	std::string m_name, m_kept;
	std::unique_ptr<AtvTrack> m_board;
	Controls &m_controls;
	Display &m_display;
	std::vector<int16_t> m_no_sound;
	bool m_atv, m_smash, m_first_power_on;
	unsigned m_coin[2] = {};
	Kick m_kick[2];
};

std::unique_ptr<AtvTrack>
make_board(const Game &game)
{
	if (game.machine == "smashdrv") {
		const std::span<const uint8_t> program = game.section("program");

		return std::make_unique<AtvTrack>(AtvTrack::rom_of(game.section("sdra"), game.section("sdrb"), game.section("sdrc")),
		                                  std::vector<uint8_t>(program.begin(), program.end()));
	}
	std::array<std::vector<uint8_t>, NandBank::CHIPS> nand;

	for (unsigned c = 0; c < NandBank::CHIPS; c++) {
		const std::span<const uint8_t> chip = game.section("nand" + std::to_string(c));

		nand[c].assign(chip.begin(), chip.end());
	}
	return std::make_unique<AtvTrack>(std::move(nand), game.section("fpga"));
}

AtvGame::AtvGame(const Game &game, Controls &controls, Display &display, const std::string &kept)
	: m_name(game.name), m_kept(kept), m_board(make_board(game)), m_controls(controls), m_display(display),
	  m_atv(game.name.rfind("atvtrack", 0) == 0), m_smash(game.machine == "smashdrv"),
	  m_first_power_on(!std::filesystem::exists(kept))
{
	m_board->set_name(m_name);
	if (!m_kept.empty())
		m_board->load_kept(m_kept);
	m_board->reset();
}

AtvGame::~AtvGame()
{
	if (!m_kept.empty())
		m_board->save_kept(m_kept);
}

bool
AtvGame::load_state(const std::string &path)
{
	return m_board->load_state(path);
}

/* Port A switch groups and ADC. All games: group 0 bit 3 TEST, bit 5 coin.
 *
 * ATV Track: group 1 bit 4 left brake, 5 right brake, 6 emergency stop
 * (normally closed), 7 START. ADC 0 handlebar (-1 at 192, +1 at 0), ADC 1
 * throttle (0 at 64, 1 at 192). HEURISTIC: +1 is right.
 *
 * Smashing Drive: group 0 bit 0 volume up, 2 down; group 1 bit 4 gas, 5
 * brake, 6 horn and START. ADC 0 wheel (-1 at 64, +1 at 192).
 *
 * Gaelco Football: groups 1 and 2, one per player: bits 0-3 stick, 4-6
 * buttons, 7 START. */
/* Right stick: forward sets power, sideways direction, pull-back height. The
 * game reads power from the side sensors, height from the middle one,
 * direction from right minus left (BallTest page). */
void
AtvGame::read_kick(const CabinetInputs &in, unsigned player)
{
	Kick &kick = m_kick[player];
	const auto axis = [&](unsigned channel) {
		return (static_cast<float>(in.jvs_analog[player * 2 + channel]) - 32768.0f) / 32768.0f;
	};
	const float x = axis(0), forward = -axis(1), far = std::hypot(x, forward);

	kick.back = std::max(kick.back, -forward);
	kick.rested = far < KICK_REST ? kick.rested + 1 : 0;
	if (kick.rested >= BACKSWING_FRAMES)
		kick.back = 0.0f;

	if (kick.frames > 0) {
		if (--kick.frames == 0)
			kick.sensors = AtvTrack::BALL_AT_REST;
	} else if (kick.spent) {
		kick.spent = forward > KICK_REST;
	} else if (forward > KICK_FROM) {
		if (forward >= kick.forward) {
			kick.forward = forward;
			kick.x = x;
		}
		if (++kick.rise >= KICK_RISE) {
			const float travel = std::clamp((std::hypot(kick.x, kick.forward) - KICK_FROM) / (1.0f - KICK_FROM), 0.0f, 1.0f);
			const float power = KICK_SOFT + (KICK_HARD - KICK_SOFT) * travel * travel * travel;
			const float force = power / KICK_GAIN / std::sqrt(2.0f * (1.0f + kick.x * kick.x));
			const auto side = [&](float turned) { return static_cast<uint8_t>(128.0f + force * (1.0f + turned)); };

			kick.sensors = { static_cast<uint8_t>(0x30 + static_cast<int>(kick.back * 200.0f)), side(-kick.x), side(kick.x) };
			kick.frames = KICK_FRAMES;
			kick.spent = true;
			kick.back = kick.forward = 0.0f;
			kick.rise = 0;
		}
	} else {
		kick.forward = 0.0f;
		kick.rise = 0;
	}
}

void
AtvGame::read_controls()
{
	CabinetInputs in{};
	std::array<uint8_t, 4> switches{};
	std::array<uint8_t, 4> analog{ 0x80, 0x80, 0x80, 0x80 };
	const auto press = [&](bool on, unsigned group, unsigned bit) {
		if (on)
			switches[group] = static_cast<uint8_t>(switches[group] | 1u << bit);
	};

	m_controls.read_inputs(in);
	for (unsigned c = 0; c < 2; c++) {
		if (in.jvs_coin[c])
			m_coin[c] = COIN_FRAMES;
		press(m_coin[c] > 0, 0, 5 - c);
		m_coin[c] -= m_coin[c] > 0;
	}
	press(in.jvs_system & 0x80, 0, 3);

	const uint16_t p1 = in.jvs_player[0];
	const unsigned wheel = in.jvs_analog[0];

	if (m_smash) {
		press(in.jvs_analog[1] > PEDAL_DOWN, 1, 4);
		press(in.jvs_analog[2] > PEDAL_DOWN, 1, 5);
		press(p1 & START, 1, 6);
		press(p1 & PUSH1 || (m_first_power_on && frame() >= VOLUME_FROM && frame() < VOLUME_TO), 0, 0);
		press(p1 & PUSH2, 0, 2);
		analog[0] = static_cast<uint8_t>(64 + wheel * 128 / CHANNEL_TOP);
	} else if (m_atv) {
		press(p1 & PUSH1, 1, 4);
		press(p1 & PUSH2, 1, 5);
		press(true, 1, 6);
		press(p1 & START, 1, 7);
		analog[0] = static_cast<uint8_t>(192 - wheel * 192 / CHANNEL_TOP);
		analog[1] = static_cast<uint8_t>(64 + unsigned{ in.jvs_analog[1] } * 128 / CHANNEL_TOP);
	} else {
		for (unsigned p = 0; p < 2; p++) {
			const uint16_t player = in.jvs_player[p];
			const uint16_t bits[] = { UP, DOWN, LEFT, RIGHT, PUSH1, PUSH2, PUSH3, START };

			for (unsigned bit = 0; bit < 8; bit++)
				press(player & bits[bit], p + 1, bit);
		}
		read_kick(in, 0);
		read_kick(in, 1);
		m_board->set_ball_sensors({ m_kick[0].sensors, m_kick[1].sensors }, { m_kick[0].frames > 0, m_kick[1].frames > 0 });
	}
	m_board->set_switches(switches);
	m_board->set_analog(analog);
}

void
AtvGame::run_frame()
{
	read_controls();
	const DisplayUpscaleMode mode = m_display.upscale_mode();

	/* Fall back to Snap without geometry shaders, as Renderer::draw_mesh
	 * does */
	m_board->set_look(m_display.scale(), static_cast<unsigned>(mode == DisplayUpscaleMode::NATIVE && !m_display.draws_native()
	                                                                   ? DisplayUpscaleMode::SNAP
	                                                                   : mode));
	m_board->run_frame();
	if (PmxScene scene = m_board->take_scene(); scene.rendered)
		m_display.set_scene(std::move(scene));
}

} /* namespace */

Ended
play_atvtrack(const Options &options, const Game &game, const GameList &games, Settings &settings,
              const std::string &settings_file, Display &display, Keyboard &keys, Pointers *pointers, Opened &opened)
{
	Controls controls(keys, game.controls, game.outputs, settings, game.name);

	controls.set_pointers(pointers);

	/* The DAC outputs 32 kHz stereo */
	std::unique_ptr<AudioOut> audio;

	if (options.sound && !options.unthrottled)
		audio = AudioOut::make(AudioOut::Mode::MAIN, 0.0, options.sound_device, DAC_RATE);
	if (audio && options.volume != 100)
		audio->set_volume(static_cast<float>(options.volume) / 100.0f);

	AtvGame running(game, controls, display, SaveFolder::of_game(game.name, options.saves).path("kept.bin"));

	display.attach_scenes(game.title);

	bool started = true;

	if (!options.state_path.empty()) {
		started = running.load_state(options.state_path);
		if (started)
			std::printf("loaded state '%s', frame %u\n", options.state_path.c_str(), running.frame());
	}

	Ended ended = Ended::FAILED;

	if (started) {
		Session session(options, game, running, nullptr, display, keys, controls, audio.get(), nullptr,
		                network_on(settings), settings, settings_file, games);

		std::printf("Running\n");
		ended = session.run();
		opened = session.opened();
	}
	std::printf("Finalizing\n");
	display.detach();
	return ended;
}

} /* namespace odin */
