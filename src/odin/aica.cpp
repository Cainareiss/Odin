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

#include "odin/aica.hpp"

#include <cassert>
#include <ctime>

namespace odin {

Aica::Aica(AicaHost &host, std::span<uint8_t> ram)
	: m_host(host), m_ram(ram), m_log(nullptr)
{
	assert(ram.size() == RAM_SIZE);
	reset();
}

void
Aica::reset()
{
	m_regs.fill(0);
	m_timer_count.fill(0);
	m_timer_step.fill(1);
	m_mcipd = 0;
	m_irql = 0;
	m_mem8mb = false;
	/* The ARM7 waits in reset until the host clears bit 0 of 0x2C00:
	 * the AM2 driver upload sets the bit, copies the code, clears it
	 * (von4 snd_upload_driver_reset_arm, 0x0C1B8F88). */
	m_arm_reset = true;
	m_fiq = false;
	m_main_irq = false;
	m_rtc_we = false;
	m_rtc_samples = 0;
	m_unknown_logged = 0;

	/* Seconds since 1950-01-01 00:00:00, the epoch of the AICA RTC
	 * (MAME aicartc.cpp rtc_clock_updated). 631152000 s separate it from
	 * the Unix epoch, 14 of those years being leap years. */
	const time_t now = time(nullptr);
	m_rtc_seconds = static_cast<uint32_t>(now) + 631152000u;

	m_host.arm_reset(true);
	m_host.arm_fiq(false);
	m_host.main_irq(false);
}

/* -- register file ------------------------------------------------------- */

uint16_t
Aica::rd16(uint32_t offset) const
{
	return static_cast<uint16_t>(m_regs[offset] | (m_regs[offset + 1] << 8));
}

void
Aica::wr16(uint32_t offset, uint16_t value)
{
	m_regs[offset] = static_cast<uint8_t>(value);
	m_regs[offset + 1] = static_cast<uint8_t>(value >> 8);
}

uint32_t
Aica::read_reg(uint32_t offset, uint32_t size)
{
	offset &= REG_SIZE - 1;
	/* Only the low 16 bits of every 32-bit word exist. */
	const uint32_t word = offset & ~3u;
	const uint32_t shift = (offset & 3) * 8;
	uint32_t value;

	if (word >= REG_CONTROL && word < 0x3000)
		value = read_common(word);
	else
		value = rd16(word);

	value >>= shift;
	if (size == 1)
		return value & 0xFF;
	if (size == 2)
		return value & 0xFFFF;
	return value & 0xFFFF;
}

void
Aica::write_reg(uint32_t offset, uint32_t size, uint32_t value)
{
	offset &= REG_SIZE - 1;
	const uint32_t word = offset & ~3u;
	const uint32_t shift = (offset & 3) * 8;
	uint16_t merged = rd16(word);

	if (shift >= 16)
		return;
	if (size == 1) {
		const uint16_t mask = static_cast<uint16_t>(0xFF << shift);
		merged = static_cast<uint16_t>((merged & ~mask) | ((value << shift) & mask));
	} else {
		merged = static_cast<uint16_t>(value & 0xFFFF);
	}

	if (word < 0x2000) {
		/* Slot registers: stored for the synthesis to come. */
		wr16(word, merged);
	} else if (word < 0x2048) {
		/* EFSDL/EFPAN, 18 words. */
		wr16(word, merged);
	} else if (word >= REG_CONTROL && word < 0x3000) {
		write_common(word, merged);
	} else if (word < REG_DSP_END) {
		/* DSP coefficients, addresses, program, work areas. */
		wr16(word, merged);
	} else {
		log_unknown('W', offset, size, value);
	}
}

uint16_t
Aica::read_common(uint32_t word)
{
	switch (word) {
	case REG_EG_MONITOR:
		/* No slot plays yet: envelope at the floor, release state,
		 * no loop end (MAME UpdateRegR 0x10 with an inactive slot). */
		return 0x7FF8;
	case REG_CA_MONITOR:
		return 0;
	case REG_MCIPD:
		return m_mcipd;
	case REG_INT_LEVEL:
		return m_irql;
	case REG_INT_CLEAR:
		return 0;
	case REG_ARM_RESET:
		return static_cast<uint16_t>(rd16(word) | (m_arm_reset ? 1 : 0));
	default:
		if (word < 0x28C0 || word == REG_ARM_RESET)
			return rd16(word);
		log_unknown('R', word, 2, 0);
		return 0;
	}
}

void
Aica::write_common(uint32_t word, uint16_t value)
{
	switch (word) {
	case REG_CONTROL:
		wr16(word, value);
		m_mem8mb = (value & 0x0200) != 0;
		break;
	case REG_RBP:
	case REG_MIDI:
	case REG_MONITOR_SELECT:
	case REG_DMEA_HI:
	case REG_DMEA_LO:
	case REG_DRGA:
		wr16(word, value);
		break;
	case REG_DLG:
		wr16(word, value);
		if (value & 1)
			run_dma();
		break;
	case REG_TIMA:
	case REG_TIMB:
	case REG_TIMC:
		wr16(word, value);
		timer_write((word - REG_TIMA) / 4);
		break;
	case REG_SCIEB:
		wr16(word, value & INT_MASK);
		update_arm_irq();
		break;
	case REG_SCIPD:
		/* Only the CPU-to-CPU bit can be set by software
		 * (SCSP manual: "only 1 can be written"). */
		if (value & INT_CPU)
			raise(INT_CPU);
		break;
	case REG_SCIRE:
		wr16(REG_SCIPD, rd16(REG_SCIPD) & static_cast<uint16_t>(~value));
		update_arm_irq();
		break;
	case REG_SCILV0:
	case REG_SCILV1:
	case REG_SCILV2:
		wr16(word, value & 0xFF);
		update_arm_irq();
		break;
	case REG_MCIEB:
		wr16(word, value & INT_MASK);
		update_main_irq();
		break;
	case REG_MCIPD:
		if (value & INT_CPU) {
			m_mcipd |= INT_CPU;
			update_main_irq();
		}
		break;
	case REG_MCIRE:
		m_mcipd &= static_cast<uint16_t>(~value);
		update_main_irq();
		break;
	case REG_ARM_RESET:
		wr16(word, value);
		if (((value & 1) != 0) != m_arm_reset) {
			m_arm_reset = (value & 1) != 0;
			m_host.arm_reset(m_arm_reset);
		}
		break;
	case REG_INT_LEVEL:
		m_irql = value;
		break;
	case REG_INT_CLEAR:
		/* End of the ARM FIQ handler: drops the request. Pending bits
		 * stay until SCIRE (MAME aica.cpp w16 0x2D04). */
		if (value & 1) {
			m_fiq = false;
			m_host.arm_fiq(false);
		}
		break;
	default:
		/* 0x28C0-0x2FFF: no register there, writes are dropped (MAME w16
		 * stores up to 0x28BD only; the drivers clear the whole block). */
		if (word < 0x28C0)
			wr16(word, value);
		break;
	}
}

/* -- wave memory --------------------------------------------------------- */

uint64_t
Aica::read_ram(uint32_t addr, uint32_t size) const
{
	addr &= ram_mask();
	uint64_t value = 0;
	for (uint32_t i = 0; i < size && i < 8; i++)
		value |= static_cast<uint64_t>(m_ram[(addr + i) & ram_mask()]) << (8 * i);
	return value;
}

void
Aica::write_ram(uint32_t addr, uint32_t size, uint64_t value)
{
	addr &= ram_mask();
	for (uint32_t i = 0; i < size && i < 8; i++)
		m_ram[(addr + i) & ram_mask()] = static_cast<uint8_t>(value >> (8 * i));
}

/* -- RTC ----------------------------------------------------------------- */

uint32_t
Aica::read_rtc(uint32_t offset) const
{
	switch (offset & 0xC) {
	case 0:
		return m_rtc_seconds >> 16;
	case 4:
		return m_rtc_seconds & 0xFFFF;
	default:
		return 0;
	}
}

void
Aica::write_rtc(uint32_t offset, uint32_t value)
{
	switch (offset & 0xC) {
	case 0:
		if (m_rtc_we) {
			m_rtc_seconds = (m_rtc_seconds & 0xFFFF) | ((value & 0xFFFF) << 16);
			m_rtc_we = false;
		}
		break;
	case 4:
		if (m_rtc_we) {
			m_rtc_seconds = (m_rtc_seconds & 0xFFFF0000) | (value & 0xFFFF);
			m_rtc_samples = 0;
		}
		break;
	case 8:
		m_rtc_we = (value & 1) != 0;
		break;
	default:
		break;
	}
}

/* -- timers and interrupts ----------------------------------------------- */

void
Aica::timer_write(uint32_t index)
{
	/* Bits 7:0 start value, bits 10:8 prescaler: the counter advances
	 * once every 2^prescaler samples and interrupts when it overflows
	 * (SCSP manual TIMA/TACTL; Flycast AicaTimer). MAME fires one
	 * increment earlier. */
	const uint16_t reg = rd16(REG_TIMA + index * 4);
	m_timer_count[index] = reg & 0xFF;
	m_timer_step[index] = 1u << ((reg >> 8) & 7);
}

void
Aica::timer_tick(uint32_t index)
{
	static constexpr uint16_t sources[3] = { INT_TIMER_A, INT_TIMER_B, INT_TIMER_C };
	const uint32_t word = REG_TIMA + index * 4;
	uint16_t count = static_cast<uint16_t>(m_timer_count[index] + 1);

	if (count > 0xFF) {
		count = 0;
		raise(sources[index]);
	}
	m_timer_count[index] = count;
	wr16(word, static_cast<uint16_t>((rd16(word) & 0xFF00) | count));
}

void
Aica::raise(uint16_t sources)
{
	wr16(REG_SCIPD, rd16(REG_SCIPD) | sources);
	m_mcipd |= sources;
	update_arm_irq();
	update_main_irq();
}

uint8_t
Aica::level_of(uint32_t source) const
{
	/* SCILV0..2 give bit 0..2 of the level for sources 0..7; sources
	 * 8 and above share bit 7 (MAME DecodeSCI, Flycast GetL). */
	const uint32_t bit = 1u << (source > 7 ? 7 : source);
	uint8_t level = 0;
	if (rd16(REG_SCILV0) & bit)
		level |= 1;
	if (rd16(REG_SCILV1) & bit)
		level |= 2;
	if (rd16(REG_SCILV2) & bit)
		level |= 4;
	return level;
}

void
Aica::update_arm_irq()
{
	const uint16_t pending = static_cast<uint16_t>(rd16(REG_SCIPD) & rd16(REG_SCIEB) & INT_MASK);
	if (pending == 0)
		return;
	for (uint32_t source = 0; source < 11; source++) {
		if (pending & (1u << source)) {
			m_irql = level_of(source);
			break;
		}
	}
	if (!m_fiq) {
		m_fiq = true;
		m_host.arm_fiq(true);
	}
}

void
Aica::update_main_irq()
{
	const bool level = (m_mcipd & rd16(REG_MCIEB) & INT_MASK) != 0;
	if (level != m_main_irq) {
		m_main_irq = level;
		m_host.main_irq(level);
	}
}

void
Aica::tick(uint32_t n)
{
	for (uint32_t s = 0; s < n; s++) {
		for (uint32_t t = 0; t < 3; t++) {
			if (--m_timer_step[t] == 0) {
				m_timer_step[t] = 1u << ((rd16(REG_TIMA + t * 4) >> 8) & 7);
				timer_tick(t);
			}
		}
		/* One interrupt per sample period, when enabled
		 * (SCSP manual bit 10; Flycast SAMPLE_DONE). */
		if ((rd16(REG_SCIEB) | rd16(REG_MCIEB)) & INT_SAMPLE)
			raise(INT_SAMPLE);
		if (++m_rtc_samples == SAMPLE_RATE) {
			m_rtc_samples = 0;
			m_rtc_seconds++;
		}
	}
}

/* -- internal DMA (wave memory <-> registers) ---------------------------- */

void
Aica::run_dma()
{
	/* DMEA: wave memory address (bits 22:16 in 0x2880, 15:2 in 0x2884);
	 * DRGA: register address; DLG: length in words; DDIR 1 = registers
	 * to memory; DGATE 1 = write zeros (MAME exec_dma, Flycast
	 * AicaInternalDMA). */
	uint32_t dmea = ((rd16(REG_DMEA_HI) & 0xFE00) << 7) | (rd16(REG_DMEA_LO) & 0xFFFC);
	uint32_t drga = rd16(REG_DRGA) & 0x7FFC;
	const uint32_t gate = (rd16(REG_DRGA) >> 15) & 1;
	const uint32_t dlg = rd16(REG_DLG) & 0x7FFC;
	const uint32_t dir = (rd16(REG_DLG) >> 15) & 1;

	for (uint32_t i = 0; i < dlg; i += 2) {
		if (dir) {
			const uint32_t v = gate ? 0 : read_reg(drga, 2);
			write_ram(dmea, 2, v);
		} else {
			const uint32_t v = gate ? 0 : static_cast<uint32_t>(read_ram(dmea, 2));
			write_reg(drga, 2, v);
		}
		dmea += 4;
		drga += 4;
	}
	wr16(REG_DLG, rd16(REG_DLG) & 0xFFFE);
	raise(INT_DMA_END);
}

/* -- state --------------------------------------------------------------- */

void
Aica::save(AicaState &out) const
{
	out.regs = m_regs;
	out.timer_count = m_timer_count;
	out.timer_step = m_timer_step;
	out.mcipd = m_mcipd;
	out.irql = m_irql;
	out.arm_reset = m_arm_reset;
	out.fiq = m_fiq;
	out.main_irq = m_main_irq;
	out.rtc_we = m_rtc_we;
	out.rtc_seconds = m_rtc_seconds;
	out.rtc_samples = m_rtc_samples;
}

void
Aica::load(const AicaState &in)
{
	m_regs = in.regs;
	m_timer_count = in.timer_count;
	m_timer_step = in.timer_step;
	m_mcipd = in.mcipd;
	m_irql = in.irql;
	m_mem8mb = (rd16(REG_CONTROL) & 0x0200) != 0;
	m_arm_reset = in.arm_reset != 0;
	m_fiq = in.fiq != 0;
	m_main_irq = in.main_irq != 0;
	m_rtc_we = in.rtc_we != 0;
	m_rtc_seconds = in.rtc_seconds;
	m_rtc_samples = in.rtc_samples;
	m_host.arm_reset(m_arm_reset);
	m_host.arm_fiq(m_fiq);
	m_host.main_irq(m_main_irq);
}

void
Aica::log_unknown(char rw, uint32_t offset, uint32_t size, uint32_t value)
{
	if (!m_log || m_unknown_logged >= 64)
		return;
	m_unknown_logged++;
	fprintf(m_log, "aica: unknown register %c offset %04X size %u value %08X\n",
	        rw, offset, size, value);
}

} /* namespace odin */
