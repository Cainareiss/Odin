/*
 * Odin
 * Copyright (C) 2026 Réda Chérif-Touil
 *
 * Valkyrie
 * Copyright (C) 2011-2013, Stefano Teso
 *
 * Valkyrie is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * Valkyrie is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with Valkyrie.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef ODIN_HIKARU_HPP
#define ODIN_HIKARU_HPP

#include <array>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <span>
#include <string>
#include <unordered_set>
#include <vector>

#include "odin/eeprom_93c46.hpp"
#include "odin/games.hpp"
#include "odin/hikaru_gpu.hpp"
#include "odin/memctl.hpp"
#include "odin/mie_board.hpp"
#include "odin/mie_stand_in.hpp"
#include "odin/rom_board.hpp"
#include "odin/scheduler.hpp"
#include "odin/sh4.hpp"
#include "odin/sound_board.hpp"
#include "odin/wav_file.hpp"

namespace odin {

class StateFile;

/* The Hikaru: its main board, with the two SH-4, their memory controllers,
 * the graphics chips, the MIE, and what plugs into it: the ROM board of
 * the game and the two sound boards. It wires the chips, which are each in
 * a file of their own, runs them a frame at a time and keeps the save
 * states. See hikaru.cpp for the board, hikaru_bus.cpp for what each
 * processor sees, hikaru_state.cpp for the save states. */
class Hikaru : private MemCtl::Host {
public:
	/* What is around the board. The screen draws what the graphics chips
	 * leave. */
	class Screen {
	public:
		virtual ~Screen() = default;
		/* A display list was launched. */
		virtual void list_launched() = 0;
		/* The texture RAM of a bank is about to be written. */
		virtual void before_texture_write(unsigned bank) = 0;
		/* The memories of the board are about to change as a whole, on
		 * a reset or for a state: what is being drawn from them must
		 * be over. */
		virtual void before_memory_changes() = 0;
		/* They have: what was decoded from them is stale. */
		virtual void memory_changed() = 0;
		/* The pictures the screen keeps from a frame to the next, as
		 * items of a state. */
		virtual bool save_pictures(StateFile &state) = 0;
		virtual bool load_pictures(StateFile &state) = 0;
	};
	/* And the control panel: the switches of the board and the controls
	 * of the cabinet as they are now. */
	class Panel {
	public:
		virtual ~Panel() = default;
		virtual void read_inputs(MieBoard::Inputs &inputs) = 0;
	};

	/* The board of a game, or nothing, with the reason on stderr, if the
	 * game cannot run on it. The game outlives the board. */
	static std::unique_ptr<Hikaru> make(const Game &game, bool verbose);

	~Hikaru();
	Hikaru(const Hikaru &) = delete;
	Hikaru &operator=(const Hikaru &) = delete;

	/* To be set before anything runs. Without a screen nothing is drawn,
	 * without a panel nothing is pressed. */
	void set_screen(Screen *screen) { m_screen = screen; }
	void set_panel(Panel *panel) { m_panel = panel; }

	/* For the screen: the graphics chips and the memories they draw
	 * from. */
	HikaruGpu &gpu() { return *m_gpu; }
	std::span<const uint8_t> framebuffer_ram() const { return m_fb; }
	std::span<const uint8_t> texture_ram(unsigned bank) const { return m_texram[bank]; }

	const std::string &game_name() const { return m_game.name; }
	/* Frames run since power-on */
	unsigned frame() const { return m_frame; }
	/* The sound of the last frame of the main board, or of the optional
	 * one: left and right, 44 100 pairs a second */
	const std::vector<int16_t> &sound(bool optional) const { return m_sound[optional ? SLAVE : MASTER]->samples(); }

	/* Power-on */
	void reset();
	void run_frame();
	/* Power-off: what a board keeps without power goes to the disk, the
	 * EEPROM of the MIE to its file (ODIN_MIE_EEPROM); and for looking at
	 * them, the memories to <game>-<memory>.bin in the current folder. */
	void power_off();

	/* False, with the reason on stderr, if the state could not be written
	 * or is not one of this game and build; the board is then as after a
	 * power-on. */
	bool save_state(const std::string &path);
	bool load_state(const std::string &path);

private:
	/* The SH-4 are told apart by their rank here */
	enum : unsigned {
		MASTER,
		SLAVE
	};

	/* What a SH-4 sees outside its plain memory */
	class Bus : public Sh4Bus {
	public:
		Bus(Hikaru &board, unsigned which) : m_board(board), m_which(which) {}

		bool read(uint32_t addr, uint32_t size, uint64_t &value) override;
		bool write(uint32_t addr, uint32_t size, uint64_t value) override;
		std::span<const uint8_t> read_span(uint32_t addr) override;
		std::span<uint8_t> write_span(uint32_t addr) override;
		uint16_t port_a_read() override;
		void port_a_write(uint16_t pins) override;
		void unanswered(bool is_write, uint32_t addr, uint32_t size, uint64_t value) override;
		void hook() override;

	private:
		Hikaru &m_board;
		unsigned m_which;
	};

	Hikaru(const Game &game, const RomBoard::Config &rombd, bool verbose);

	/* hikaru.cpp */
	void build_mie();
	void build_sound(unsigned which);
	void reset_sound(unsigned which);
	void update_irqs();
	void set_resets(uint8_t pins);
	void set_gpu_reset_lines();
	void run_cycles(int cycles);
	void run_cpu(unsigned which, int cycles);
	uint64_t line_cycles();
	void begin_frame();
	void settle();
	void save_clock();
	void slave_idle_loop();
	std::string cpu_string(unsigned which) const;
	std::string board_string() const;
	void tell_error(const std::string &where, const char *what);
	void cpu_error(const char *fmt, ...) __attribute__((format(printf, 2, 3)));
	void board_error(const char *fmt, ...) __attribute__((format(printf, 2, 3)));
	void log_access(const char *name, char rw, unsigned size, uint32_t addr, uint64_t value);
	void trace_slice(unsigned which);

	/* hikaru_bus.cpp */
	void map_pages();
	bool master_read(uint32_t addr, unsigned size, uint64_t &value);
	bool master_write(uint32_t addr, unsigned size, uint64_t value);
	bool slave_read(uint32_t addr, unsigned size, uint64_t &value);
	bool slave_write(uint32_t addr, unsigned size, uint64_t value);
	bool gpu_read(uint32_t addr, unsigned size, uint64_t &value);
	bool gpu_write(uint32_t addr, unsigned size, uint64_t value);
	bool mie_read(uint32_t addr, unsigned size, uint64_t &value);
	bool mie_write(uint32_t addr, unsigned size, uint64_t value);
	bool mscomm_read(uint32_t addr, unsigned size, uint64_t &value);
	bool mscomm_write(uint32_t addr, unsigned size, uint64_t value);
	bool slave_window_read(uint32_t addr, unsigned size, uint64_t &value);
	bool slave_window_write(uint32_t addr, unsigned size, uint64_t value);
	bool sound_read(unsigned which, uint32_t addr, unsigned size, uint64_t &value);
	bool sound_write(unsigned which, uint32_t addr, unsigned size, uint64_t value);
	void texram_write(unsigned bank, uint32_t offs, unsigned size, uint64_t value);
	void texram_store(unsigned bank, uint32_t offs, unsigned size, uint64_t value, bool twiddled);
	std::span<const uint8_t> bus_span(uint32_t bus_addr) const;
	std::span<uint8_t> bus_ram(uint32_t bus_addr);
	SoundBoard *wave_board(uint32_t bus_addr);
	/* MemCtl::Host: the external bus */
	bool bus_read(uint32_t bus_addr, unsigned size, uint64_t &value) override;
	bool bus_write(uint32_t bus_addr, unsigned size, uint64_t value) override;
	uint32_t bus_copy(uint32_t src, uint32_t dst, uint32_t words) override;
	std::span<const uint8_t> bus_read_span(uint32_t bus_addr) override;
	std::span<uint8_t> bus_write_span(uint32_t bus_addr) override;
	void memctl_irq_changed() override { update_irqs(); }
	void memctl_windows_changed() override { quiet_windows(); }
	void quiet_windows();
	void memctl_error(const char *what) override { cpu_error("%s", what); }

	/* hikaru_state.cpp */
	bool save_items(StateFile &state);
	bool load_items(StateFile &state);
	bool save_cpu(StateFile &state, unsigned which);
	bool load_cpu(StateFile &state, unsigned which);

	const Game &m_game;
	bool m_verbose;
	Screen *m_screen;
	Panel *m_panel;
	uint32_t m_clock;	/* of the SH-4, in Hz */
	unsigned m_frame;
	/* The memories and the chips are being reset one by one: what one of
	 * them shows of another is half old, half new until all are. */
	bool m_resetting;
	unsigned m_current;	/* the SH-4 that runs, for the messages */

	/* Memories */
	std::vector<uint8_t> m_ram[2];		/* 32 MB each */
	std::vector<uint8_t> m_cmdram;		/* 4 MB */
	std::vector<uint8_t> m_fb;		/* 8 MB */
	std::vector<uint8_t> m_texram[2];	/* 4 MB each */
	std::vector<uint8_t> m_sound_ram[2];	/* 8 MB each */
	std::vector<uint8_t> m_mie_ram;		/* 32 KB */
	std::vector<uint8_t> m_bram;		/* 64 KB */
	std::array<uint8_t, 0x40> m_mscomm;	/* the master and slave box */
	std::vector<uint8_t> m_iregs[2];	/* on-chip registers of the SH-4 */
	std::span<const uint8_t> m_bootrom;

	/* Chips and boards */
	MemCtl m_memctl[2];
	RomBoard m_rombd;
	/* 93C46 of the main board, on port A of the master: bit 2 DI, 3 SK,
	 * 4 CS, 5 DO */
	Eeprom93c46 m_eeprom;
	std::unique_ptr<MieBoard> m_mie;	/* nothing: the stand-in answers */
	MieStandIn m_mie_stand_in;
	std::unique_ptr<HikaruGpu> m_gpu;
	std::unique_ptr<SoundBoard> m_sound[2];
	std::unique_ptr<WavFile> m_wav;	/* ODIN_WAV: the sound of the main board */
	Bus m_bus[2];
	std::unique_ptr<Sh4> m_sh[2];
	bool m_hooked[2];
	Scheduler m_sched;

	/* Port A of the two SH-4 */
	uint16_t m_porta[2];
	uint8_t m_irq_lines;	/* see update_irqs */
	uint8_t m_mie_port_d;	/* see set_resets */
	/* The window of the slave on the MIE, see slave_window_read */
	uint32_t m_unk1A800008_s;
	uint32_t m_unk1B000100_s;

	/* The frame being run: its line, and the screen the game has set */
	unsigned m_line;
	unsigned m_dots_per_line, m_lines_per_frame, m_lines_shown;
	uint32_t m_line_rest;	/* of a cycle, in dot clocks */

	/* The accesses nothing answered that were told, see Bus::unanswered */
	std::unordered_set<uint64_t> m_unanswered_told;

	/* Traces and counts asked by the environment */
	FILE *m_mie_trace;
	FILE *m_sh_trace[2];
	/* ODIN_STATS=1: the host time each part of the board took in its
	 * slices, told at the end and for every 60 frames. The first two are
	 * the SH-4. */
	enum : unsigned {
		PART_GPU = 2,
		PART_MIE,
		PART_SOUND,
		NUM_PARTS
	};
	bool m_stats;
	uint64_t m_stats_ns[NUM_PARTS], m_stats_slices;
	uint64_t m_stats_told[NUM_PARTS], m_stats_told_at;	/* at the last 60 frames told */
};

} /* namespace odin */

#endif /* ODIN_HIKARU_HPP */
