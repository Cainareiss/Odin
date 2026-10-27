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

#ifndef ODIN_AICA_DSP_HPP
#define ODIN_AICA_DSP_HPP

#include <array>
#include <cstdint>

#include "odin/machine/chips/aica/wave_memory.hpp"

namespace odin {

/* The effect processor of the AICA: a program of 128 steps run once a
 * sample period over the sends of the voices (MIXS), the external inputs
 * (EXTS) and a ring buffer in the wave memory, into 16 outputs (EFREG).
 * The program, its coefficients and addresses stay in the register file
 * (COEF 0x3000, MADRS 0x3200, MPRO 0x3400); the working registers live
 * here and are seen at 0x4000-0x45C7.
 *
 * Sources, compared in the notes (MARKDOWN/30, section 5): the steps as
 * Corlett and Flycast agree on them (an empty step still computes, a read
 * of the memory comes two steps later, EFREG is set, not added); the
 * memory reached at the odd steps only (Flycast, MAME); the registers keep
 * their values from one sample period to the next, as a circuit does
 * (Corlett; Flycast and MAME clear some of them). */
class AicaDsp {
public:
	/* Plain data, for the save states */
	struct State {
		std::array<int32_t, 128> temp;	/* 24 bits */
		std::array<int32_t, 32> mems;	/* 24 bits */
		std::array<int32_t, 16> mixs;	/* the sends of this period, 20 bits */
		std::array<int32_t, 16> inputs;	/* MIXS as the program reads it, 24 bits */
		std::array<int16_t, 16> efreg;
		std::array<int16_t, 2> exts;
		std::array<int32_t, 4> reads;	/* the memory read two steps ago */
		int32_t acc, frc, y, adrs;
		uint32_t mdec;
	};

	AicaDsp() { reset(); }
	void reset();

	/* After a write to MPRO */
	void program_written(const uint8_t *regs);
	/* A voice sends to MIXS `mix`, 20 bits */
	void send(uint32_t mix, int32_t sample) { m_state.mixs[mix & 15] += sample; }
	void set_external(int16_t left, int16_t right) { m_state.exts = { left, right }; }
	/* One sample period. `regs`: the register file. */
	void run(const uint8_t *regs, const WaveMemory &memory);
	int16_t effect(uint32_t n) const { return m_state.efreg[n & 15]; }
	int16_t external(uint32_t n) const { return m_state.exts[n & 1]; }

	/* TEMP, MEMS, MIXS, EFREG and EXTS at their offsets of 0x4000-0x45C7 */
	uint16_t read(uint32_t offset) const;
	void write(uint32_t offset, uint16_t value);

	const State &state() const { return m_state; }
	/* `regs`: the register file that goes with the state */
	void load(const State &in, const uint8_t *regs)
	{
		m_state = in;
		program_written(regs);
	}

private:
	/* A step of the program, its fields taken out of its four words once,
	 * when the program is written */
	struct Step {
		uint32_t flags;
		uint8_t i, ira, iwa, tra, twa, shift, ysel, masa, ewa;
	};
	enum : uint32_t {
		IWT = 1 << 0, TWT = 1 << 1, XSEL = 1 << 2, BZERO = 1 << 3, BACC = 1 << 4, NEGB = 1 << 5,
		YRL = 1 << 6, FRCL = 1 << 7, MEMORY = 1 << 8, MRD = 1 << 9, MWT = 1 << 10, TABLE = 1 << 11,
		ADREB = 1 << 12, NXADR = 1 << 13, NOFL = 1 << 14, ADRL = 1 << 15, EWT = 1 << 16
	};

	State m_state;
	/* The steps run() runs, in order: each step of the program that does
	 * something, and the first of each run of empty steps. An empty step
	 * sets ACC from TEMP[MDEC] and FRC alone and writes nothing, so a run
	 * of them ends as one of them does. None: the program is empty. */
	std::array<Step, 128> m_steps;
	uint32_t m_size;
};

} /* namespace odin */

#endif /* ODIN_AICA_DSP_HPP */
