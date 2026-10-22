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

#include "odin/aica_dsp.hpp"

#include <algorithm>

namespace odin {

namespace {

enum : uint32_t {
	REG_RING = 0x2804,	/* RBL 14:13, RBP 11:0 */
	REG_COEF = 0x3000,
	REG_MADRS = 0x3200,
	REG_MPRO = 0x3400,
	REG_TEMP = 0x4000,
	REG_MEMS = 0x4400,
	REG_MIXS = 0x4500,
	REG_EFREG = 0x4580,
	REG_EXTS = 0x45C0
};

int32_t
sign24(int32_t value)
{
	return static_cast<int32_t>(static_cast<uint32_t>(value) << 8) >> 8;
}

/* The 16-bit floats of the ring buffer: sign, 4 bits of exponent, 11 of
 * mantissa (yam.c float16_to_int24 and int24_to_float16) */
int32_t
unpack(uint32_t f)
{
	uint32_t exponent = (f >> 11) & 0xF;
	int32_t result = static_cast<int32_t>((f & 0x8000) << 16) >> 1;

	if (exponent >= 12)
		exponent = 11;
	else
		result ^= 0x40000000;
	result |= static_cast<int32_t>((f & 0x7FF) << 19);
	return result >> (exponent + 8);
}

uint32_t
pack(int32_t value)
{
	uint32_t i = static_cast<uint32_t>(value);
	const uint32_t sign = i & 0x800000;
	uint32_t exponent = 0;

	if (sign)
		i = ~i;
	i &= 0x7FFFFF;
	if (i < 0x020000) {
		exponent += 6 << 11;
		i <<= 6;
	}
	if (i < 0x100000) {
		exponent += 3 << 11;
		i <<= 3;
	}
	if (i < 0x400000) {
		exponent += 1 << 11;
		i <<= 1;
	}
	if (i < 0x400000) {
		exponent += 1 << 11;
		i <<= 1;
	}
	if (i < 0x400000)
		exponent += 1 << 11;
	i = ((i >> 11) & 0x7FF) | exponent;
	return sign ? i ^ 0x87FF : i;
}

} /* namespace */

void
AicaDsp::reset()
{
	m_state = {};
	m_order = {};
	m_size = 0;
}

void
AicaDsp::program_written(const uint8_t *regs)
{
	bool busy = false, after_empty = false;

	m_size = 0;
	for (uint32_t i = 0; i < 128; i++) {
		const uint32_t at = REG_MPRO + i * 16;
		const bool empty = !(aica_reg(regs, at) | aica_reg(regs, at + 4) | aica_reg(regs, at + 8) | aica_reg(regs, at + 12));

		if (!empty || !after_empty)
			m_order[m_size++] = static_cast<uint8_t>(i);
		after_empty = empty;
		busy |= !empty;
	}
	if (!busy)
		m_size = 0;
}

void
AicaDsp::run(const uint8_t *regs, const WaveMemory &memory)
{
	State &s = m_state;

	for (uint32_t j = 0; j < 16; j++) {
		s.inputs[j] = std::clamp(s.mixs[j], -0x80000, 0x7FFFF) * 16;
		s.mixs[j] = 0;
	}
	/* An empty program writes nothing: only the counter moves */
	if (m_size) {
		const uint32_t ring = aica_reg(regs, REG_RING);
		const uint32_t base = (ring & 0xFFF) << 11;
		const uint32_t length = (8192u << ((ring >> 13) & 3)) - 1;

		for (uint32_t n = 0; n < m_size; n++) {
			const uint32_t i = m_order[n];
			const uint32_t at = REG_MPRO + i * 16;
			const uint32_t w0 = aica_reg(regs, at), w1 = aica_reg(regs, at + 4);
			const uint32_t w2 = aica_reg(regs, at + 8), w3 = aica_reg(regs, at + 12);
			const uint32_t ira = (w1 >> 7) & 0x3F;
			const uint32_t shift = (w2 >> 4) & 3;

			/* The input, read before the step writes MEMS */
			int32_t input = 0;
			if (ira < 0x20)
				input = s.mems[ira];
			else if (ira < 0x30)
				input = s.inputs[ira - 0x20];
			else if (ira < 0x32)
				input = s.exts[ira - 0x30] * 256;
			if (w1 & 0x40)
				s.mems[(w1 >> 1) & 0x1F] = s.reads[i & 3];

			const int32_t temp = s.temp[((w0 >> 9) + s.mdec) & 0x7F];
			int32_t b = (w2 & 2) ? 0 : (w2 & 1) ? s.acc : temp;
			if (w2 & 4)
				b = -b;
			const int32_t x = (w1 & 0x8000) ? input : temp;
			int32_t y;
			switch ((w1 >> 13) & 3) {
			case 0:
				y = s.frc;
				break;
			case 1:
				y = static_cast<int16_t>(aica_reg(regs, REG_COEF + i * 4)) >> 3;
				break;
			case 2:
				y = s.y >> 11;
				break;
			default:
				y = (s.y >> 4) & 0xFFF;
				break;
			}
			if (w2 & 8)
				s.y = input;

			/* The sum of the step before, as SHIFT gives it: doubled
			 * for 1 and 2, held to 24 bits for 0 and 1 */
			int32_t shifted = (shift == 1 || shift == 2) ? s.acc * 2 : s.acc;
			shifted = shift < 2 ? std::clamp(shifted, -0x800000, 0x7FFFFF) : sign24(shifted);
			s.acc = static_cast<int32_t>((static_cast<int64_t>(x) * y) >> 12) + b;

			if (w0 & 0x100)
				s.temp[((w0 >> 1) + s.mdec) & 0x7F] = shifted;
			if (w2 & 0x40)
				s.frc = shift == 3 ? shifted & 0xFFF : shifted >> 11;
			if ((w2 & 0x6000) && (i & 1)) {
				uint32_t addr = aica_reg(regs, REG_MADRS + ((w3 >> 9) & 0x3F) * 4);

				if (w3 & 0x100)
					addr += static_cast<uint32_t>(s.adrs) & 0xFFF;
				if (w3 & 0x80)
					addr++;
				addr = (w2 & 0x8000) ? addr & 0xFFFF : (addr + s.mdec) & length;

				const uint32_t byte = base + addr * 2;
				if (w2 & 0x2000) {
					const int32_t word = static_cast<int16_t>(memory.read16(byte));
					s.reads[(i + 2) & 3] = (w3 & 0x8000) ? word * 256 : unpack(static_cast<uint16_t>(word));
				}
				if (w2 & 0x4000) {
					const uint32_t word = (w3 & 0x8000) ? static_cast<uint32_t>(shifted >> 8) : pack(shifted);
					memory.write16(byte, static_cast<uint16_t>(word));
				}
			}
			if (w2 & 0x80)
				s.adrs = (shift == 3 ? shifted >> 12 : input >> 16) & 0xFFF;
			if (w2 & 0x1000)
				s.efreg[(w2 >> 8) & 0xF] = static_cast<int16_t>(shifted >> 8);
		}
	}
	s.mdec--;
}

uint16_t
AicaDsp::read(uint32_t offset) const
{
	const auto split = [offset](int32_t value) {
		return static_cast<uint16_t>((offset & 4) ? (value >> 8) & 0xFFFF : value & 0xFF);
	};

	if (offset < REG_MEMS)
		return split(m_state.temp[((offset - REG_TEMP) >> 3) & 0x7F]);
	if (offset < REG_MIXS)
		return split(m_state.mems[((offset - REG_MEMS) >> 3) & 0x1F]);
	if (offset < REG_EFREG) {
		const int32_t value = m_state.inputs[((offset - REG_MIXS) >> 3) & 0xF];
		return static_cast<uint16_t>((offset & 4) ? (value >> 8) & 0xFFFF : (value >> 4) & 0xF);
	}
	if (offset < REG_EXTS)
		return static_cast<uint16_t>(m_state.efreg[((offset - REG_EFREG) >> 2) & 0xF]);
	return static_cast<uint16_t>(m_state.exts[((offset - REG_EXTS) >> 2) & 1]);
}

void
AicaDsp::write(uint32_t offset, uint16_t value)
{
	/* The low byte at +0, bits 23:8 at +4 */
	const auto merge = [offset, value](int32_t &to) {
		const uint32_t old = static_cast<uint32_t>(to);
		const uint32_t merged = (offset & 4) ? (old & 0xFF) | (static_cast<uint32_t>(value) << 8)
		                                     : (old & ~0xFFu) | (value & 0xFFu);
		to = sign24(static_cast<int32_t>(merged));
	};

	if (offset < REG_MEMS)
		merge(m_state.temp[((offset - REG_TEMP) >> 3) & 0x7F]);
	else if (offset < REG_MIXS)
		merge(m_state.mems[((offset - REG_MEMS) >> 3) & 0x1F]);
	else if (offset >= REG_EFREG && offset < REG_EXTS)
		m_state.efreg[((offset - REG_EFREG) >> 2) & 0xF] = static_cast<int16_t>(value);
	/* MIXS and EXTS are inputs: a write does nothing */
}

} /* namespace odin */
