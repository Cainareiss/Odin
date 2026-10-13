/*
 * Odin
 * Copyright (C) 2026 Réda Chérif-Touil
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 * 02110-1301, USA.
 */

#ifndef ODIN_SOUND_BOARD_HPP
#define ODIN_SOUND_BOARD_HPP

#include <cstdint>
#include <cstdio>
#include <functional>
#include <span>

#include "odin/aica.hpp"
#include "odin/arm7.hpp"

namespace odin {

/* One sound board of the Hikaru: the AICA, its wave memory and the ARM7
 * that runs the sound driver. Time comes in as cycles of the main
 * processor and leaves as sample periods. */
class SoundBoard : public AicaHost, public Arm7Bus {
public:
	/* The ARM7DI runs on the bus clock, 22.5792 MHz (33.8688 MHz * 2 / 3):
	 * 512 cycles per sample period. MAME gives it one cycle out of eight,
	 * to stand for wait states; SGNASCAR loads the sound driver again at
	 * each change of scene and waits for it, and on its cabinet the driver
	 * answers within a frame: 328000 cycles, 20 MHz at least. HEURISTIC:
	 * no wait state. */
	static constexpr uint32_t ARM_CYCLES_PER_SAMPLE = 512;

	/* What a write of the main processor did */
	enum class Written {
		NOWHERE,	/* the board decodes nothing there */
		DONE,
		CLOCK_SET	/* the count of the RTC was set */
	};

	/* The whole board. Plain data. */
	struct State {
		uint64_t sample_acc;
		Arm7State arm;
		AicaState aica;
		uint8_t arm_held;
	};

	/* ram: the wave memory, Aica::RAM_SIZE bytes, owned by the caller.
	 * clock_hz: the clock of the cycles given to run. main_irq: called
	 * when the board asks for the interrupt of the main processor, or
	 * drops it. */
	SoundBoard(std::span<uint8_t> ram, uint32_t clock_hz, std::function<void(bool)> main_irq);

	void reset();
	/* The reset pin of the board: as reset, but the clock keeps its
	 * time. */
	void reset_pin();
	/* The level of the line to the main processor */
	bool main_irq() const { return m_aica.main_irq(); }

	/* The battery of the RTC: the seconds the clock of the board is
	 * ahead of the local time of the host. */
	int32_t rtc_ahead() const { return m_aica.rtc_ahead(); }
	void set_rtc_ahead(int32_t seconds) { m_aica.set_rtc_ahead(seconds); }

	/* The 16 MB page of the board on the bus of the main processor:
	 * registers at 700000, RTC at 710000, wave memory from 800000; 1, 2,
	 * 4 or, in the wave memory, 8 bytes. A read tells whether the board
	 * decodes that offset. */
	bool host_read(uint32_t offset, unsigned size, uint64_t &value);
	Written host_write(uint32_t offset, unsigned size, uint64_t value);

	/* Advances the board by that many cycles of the main processor. */
	void run(uint32_t cycles);

	/* The ARM7 only counts the turns of a program that waits: this puts
	 * in the wave memory what it has not written yet. To be called
	 * before the memory is read from outside the board. */
	void settle() { m_arm.settle(); }

	void set_log(FILE *out) { m_aica.set_log(out); }
	/* ARM7 instruction and bus trace in the charter format; nullptr
	 * turns it off. */
	void set_arm_trace(FILE *out) { m_arm.set_trace(out); }

	void save(State &out);
	void load(const State &in);

	/* AicaHost */
	void arm_fiq(bool level) override { m_arm.set_fiq(level); }
	void arm_reset(bool level) override;
	void main_irq(bool level) override;

	/* Arm7Bus: wave memory below 800000, registers at 800000, nothing
	 * else (MAME arm0_map: unmap_value_high). */
	uint8_t read8(uint32_t addr) override { return static_cast<uint8_t>(read(addr, 1)); }
	uint16_t read16(uint32_t addr) override { return static_cast<uint16_t>(read(addr, 2)); }
	uint32_t read32(uint32_t addr) override { return read(addr, 4); }
	void write8(uint32_t addr, uint8_t value) override { write(addr, 1, value); }
	void write16(uint32_t addr, uint16_t value) override { write(addr, 2, value); }
	void write32(uint32_t addr, uint32_t value) override { write(addr, 4, value); }

private:
	uint32_t read(uint32_t addr, uint32_t size);
	void write(uint32_t addr, uint32_t size, uint32_t value);
	void map_ram();

	std::function<void(bool)> m_main_irq;
	uint32_t m_clock_hz;
	uint64_t m_sample_acc;
	bool m_arm_held;
	uint8_t *m_ram;
	Arm7 m_arm;
	Aica m_aica;
};

} /* namespace odin */

#endif /* ODIN_SOUND_BOARD_HPP */
