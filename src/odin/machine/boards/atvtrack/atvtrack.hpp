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

#ifndef ODIN_ATVTRACK_HPP
#define ODIN_ATVTRACK_HPP

#include <array>
#include <set>
#include <string>
#include <utility>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <vector>

#include "odin/machine/boards/atvtrack/pmx1.hpp"
#include "odin/machine/cpu/sh4/sh4.hpp"

namespace odin {

/* Four K9F2808U0B side by side, one per byte of the 32-bit bus, each with
 * 1024 blocks of 32 pages of 528 bytes (512 plus 16 spare). */
class NandBank {
public:
	static constexpr unsigned CHIPS = 4;
	static constexpr uint32_t PAGE = 528, PAGES = 32 * 1024, SIZE = PAGE * PAGES;

	explicit NandBank(std::array<std::vector<uint8_t>, CHIPS> chips);

	uint32_t read_data();
	void write_data(uint32_t lanes);
	void write_command(uint32_t lanes);
	void write_address(uint32_t lanes);

private:
	struct Chip {
		std::vector<uint8_t> data;
		uint8_t command = 0xFF;
		uint32_t column = 0, row = 0, step = 0, at = 0, id = 0;
		uint8_t area = 0;
	};

	uint8_t read(Chip &chip);

	std::array<Chip, CHIPS> m_chips;
	std::set<std::pair<unsigned, uint32_t>> m_changed;	/* chip, block */

public:
	static constexpr uint32_t BLOCK = 32 * PAGE;
	/* Blocks written since the image was loaded, and restoring one */
	const std::set<std::pair<unsigned, uint32_t>> &changed() const { return m_changed; }
	std::span<const uint8_t> block(unsigned chip, uint32_t number) const
	{
		return std::span<const uint8_t>(m_chips[chip].data).subspan(number * BLOCK, BLOCK);
	}
	void set_block(unsigned chip, uint32_t number, std::span<const uint8_t> bytes);
};

/* Gaelco ATV Track board (REF. 020419; 020201 for Football): two SH-4s.
 * Master: 8 MB, NAND, FPGA I/O. Slave: 16 MB, PowerVR Neon 250 with 32 MB.
 * Both see the FPGA's 1 KB boot RAM at 0. */
class AtvTrack {
public:
	enum : unsigned { MASTER, SLAVE };

	AtvTrack(std::array<std::vector<uint8_t>, NandBank::CHIPS> nand, std::span<const uint8_t> fpga_config);
	/* The Smashing Drive board: 64 MB of ROM at 0 (rom_of), the program
	 * at 14000000, and the RAM both SH-4s share at 10000000 on the
	 * master */
	AtvTrack(std::vector<uint8_t> rom, std::vector<uint8_t> program);
	/* Smashing Drive's ROM from its chips: two 16-bit chips side by side,
	 * then the third */
	static std::vector<uint8_t> rom_of(std::span<const uint8_t> a, std::span<const uint8_t> b,
	                                   std::span<const uint8_t> c);

	void set_name(const std::string &name) { m_name = name; }
	void reset();
	void run_frame();
	/* Non-volatile data: the NAND blocks or flash sectors the game
	 * wrote. No file means the dumps as they are. False on failure,
	 * logged to stderr. */
	bool load_kept(const std::string &path);
	bool save_kept(const std::string &path) const;
	/* The whole board at the end of a frame (atvtrack_state.cpp). Loading
	 * resets the board first. */
	bool save_state(const std::string &path);
	bool load_state(const std::string &path);
	/* Cabinet switches, pressed bits set: the four groups the FPGA puts
	 * on port A, selected by bits 11-14 */
	void set_switches(const std::array<uint8_t, 4> &switches) { m_switches = switches; }
	/* Gaelco Football: the three sensors under each ball as sent (middle,
	 * left, right), and whether it is being kicked */
	using BallSensors = std::array<std::array<uint8_t, 3>, 2>;
	static constexpr std::array<uint8_t, 3> BALL_AT_REST = { 0x30, 0x80, 0x80 };
	void set_ball_sensors(const BallSensors &sensors, const std::array<bool, 2> &kicked)
	{
		m_balls = true;
		m_ball = sensors;
		m_ball_kicked = kicked;
	}
	void set_analog(const std::array<uint8_t, 4> &analog) { m_analog = analog; }
	void set_look(unsigned fine, unsigned flat_mode) { m_pmx.set_look(fine, flat_mode); }
	PmxScene take_scene() { return m_pmx.take_scene(); }

	/* Last frame's sound: left and right, 32,000 pairs a second */
	const std::vector<int16_t> &sound() const { return m_sound; }
	unsigned frame() const { return m_frame; }

private:
	class Bus : public Sh4Bus {
	public:
		Bus(AtvTrack &board, unsigned which) : m_board(board), m_which(which) {}

		bool read(uint32_t addr, uint32_t size, uint64_t &value) override;
		bool write(uint32_t addr, uint32_t size, uint64_t value) override;
		uint16_t port_a_read() override;
		void port_a_write(uint16_t pins) override;
		void unanswered(bool, uint32_t, uint32_t, uint64_t) override {}
		void hook() override {}

	private:
		AtvTrack &m_board;
		unsigned m_which;
	};

	bool master_read(uint32_t addr, uint32_t size, uint64_t &value);
	bool master_write(uint32_t addr, uint32_t size, uint64_t value);
	bool slave_read(uint32_t addr, uint32_t size, uint64_t &value);
	bool slave_write(uint32_t addr, uint32_t size, uint64_t value);
	static bool mirror(std::vector<uint8_t> &ram, uint32_t addr, uint32_t size, uint64_t *read, uint64_t value);
	void control_write(uint32_t value);
	void play_dac(unsigned slices_per_frame);
	void flash_write(uint32_t offset, uint8_t byte);
	std::vector<uint8_t> kept() const;
	bool take_kept(std::span<const uint8_t> kept);
	void adc_pins(uint16_t pins);
	bool adc_out() const;
	void ball_pins(uint16_t pins);
	bool ball_out(unsigned ball) const;
	void gpu_irqs();
	void build();
	void map_pages();

	std::vector<uint8_t> m_boot;	/* the 1 KB the FPGA starts with */
	std::vector<uint8_t> m_rom, m_program;	/* Smashing Drive */
	bool m_smash = false;
	std::string m_name;
	uint32_t m_control_at, m_dac_at;
	std::vector<uint8_t> m_shared, m_master_ram, m_slave_ram, m_gpu_ram;
	std::array<std::vector<uint8_t>, 2> m_iregs, m_cache_ram;
	NandBank m_nand;
	std::array<Bus, 2> m_bus;
	std::array<std::unique_ptr<Sh4>, 2> m_sh;
	Pmx1 m_pmx;
	uint32_t m_control = 0;
	uint16_t m_port_a = 0;
	std::array<uint8_t, 4> m_switches{};
	std::array<uint8_t, 64> m_dac{};
	std::array<uint8_t, 4> m_analog{ 0x80, 0x80, 0x80, 0x80 };
	uint32_t m_adc_step = 0, m_adc_command = 0, m_adc_sample = 0;
	bool m_adc_clock = false;
	BallSensors m_ball{};
	std::array<bool, 2> m_ball_kicked{};
	bool m_balls = false, m_ball_clock = false;
	uint32_t m_ball_edge = 0;
	uint32_t m_dac_time = 0, m_dac_played = 0, m_dac_half = 0;
	bool m_dac_asking = false;
	/* Smashing Drive flash: the command step, and the sectors written
	 * since the dump */
	uint32_t m_flash_step = 0;
	std::set<uint32_t> m_flash_changed;
	std::vector<int16_t> m_sound;
	bool m_slave_enabled = false, m_slave_running = false;
	uint32_t m_gpu_pending = 0, m_gpu_mask = 0xFFFF;
	unsigned m_frame = 0;
};

} /* namespace odin */

#endif
