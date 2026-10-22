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

#include "odin/drive_838_13366.hpp"

#include <algorithm>
#include <cstring>

namespace odin {

Drive838_13366::Drive838_13366(std::span<const uint8_t> rom) : m_rom{}, m_state{}, m_cpu(*this)
{
	std::fill(m_rom.begin(), m_rom.end(), 0xFF);
	std::copy_n(rom.begin(), std::min(rom.size(), m_rom.size()), m_rom.begin());
	reset();
}

void
Drive838_13366::reset()
{
	m_state = State{};
	m_state.reply = m_state.command = 0xFF;
	m_state.motor_register = m_state.motor_value = 0xFF;
	m_state.torque_value = 0x80;
	m_state.lamps = 0xFF;
	m_cpu.reset();
}

/* The CTC of the board as its program sets it (01F7): channel 0 a timer
 * (prescaler 16, constant FA), channel 1 counting its ends (constant 4),
 * the end of channel 1 on the NMI; 16000 clocks */
uint64_t
Drive838_13366::nmi_period() const
{
	const State &s = m_state;

	if ((s.ctc_running & 3) != 3 || (s.ctc_control[0] & 0x40) || !(s.ctc_control[1] & 0x40))
		return 0;

	const uint64_t prescaler = (s.ctc_control[0] & 0x20) ? 256 : 16;
	const uint64_t first = s.ctc_constant[0] ? s.ctc_constant[0] : 256;
	const uint64_t second = s.ctc_constant[1] ? s.ctc_constant[1] : 256;

	return prescaler * first * second;
}

/* A control word (bit 0), or the constant it said would follow (bit 2);
 * a reset (bit 1) stops the channel until then */
void
Drive838_13366::write_ctc(unsigned channel, uint8_t value)
{
	State &s = m_state;
	const uint8_t bit = static_cast<uint8_t>(1u << channel);

	if (s.ctc_loading & bit) {
		s.ctc_constant[channel] = value;
		s.ctc_loading = static_cast<uint8_t>(s.ctc_loading & ~bit);
		s.ctc_running = static_cast<uint8_t>(s.ctc_running | bit);
	} else if (value & 1) {
		s.ctc_control[channel] = value;
		if (value & 2)
			s.ctc_running = static_cast<uint8_t>(s.ctc_running & ~bit);
		if (value & 4)
			s.ctc_loading = static_cast<uint8_t>(s.ctc_loading | bit);
	} else {
		return;	/* the vector of the interrupts: none here */
	}

	const uint64_t period = nmi_period();

	s.next_nmi = period ? s.clocks + period : 0;
}

void
Drive838_13366::run_until(uint64_t clock, uint8_t command, uint8_t wheel)
{
	State &s = m_state;

	s.command = command;
	m_wheel = wheel;
	while (s.clocks < clock) {
		const uint64_t to = s.next_nmi ? std::min(clock, s.next_nmi) : clock;

		if (to > s.clocks)
			s.clocks += m_cpu.run(static_cast<uint32_t>(to - s.clocks));
		if (s.next_nmi && s.clocks >= s.next_nmi) {
			m_cpu.set_nmi();
			s.next_nmi += nmi_period();
		}
	}
}

int
Drive838_13366::torque() const
{
	return std::clamp(0x80 - static_cast<int>(m_state.torque_value), -MOST, MOST);
}

uint8_t
Drive838_13366::read8(uint16_t addr)
{
	return addr >= RAM_BASE ? m_state.ram[addr - RAM_BASE] : m_rom[addr];
}

void
Drive838_13366::write8(uint16_t addr, uint8_t value)
{
	if (addr >= RAM_BASE)
		m_state.ram[addr - RAM_BASE] = value;
}

uint8_t
Drive838_13366::in8(uint16_t port)
{
	State &s = m_state;

	switch (port & 0xFF) {
	case 0x20:
		return DIP1;
	case 0x21:
		return DIP2;
	case 0x24:
	case 0x25:
	case 0x26:
	case 0x27:
		if (!s.adc_bits)
			return 0;
		s.adc_bits--;
		return (s.adc_value >> s.adc_bits) & 1;
	case 0x28:
		return s.command;
	case 0x2C:
		return 0x00;	/* the motor board well */
	default:
		return 0xFF;
	}
}

void
Drive838_13366::out8(uint16_t port, uint8_t value)
{
	State &s = m_state;

	switch (port & 0xFF) {
	case 0x10:
	case 0x11:
		write_ctc(port & 1, value);
		return;
	/* The converter samples the channel written to: the wheel; the
	 * second axis (the seat of a deluxe cabinet) at its centre; nothing on
	 * the other two */
	case 0x24:
	case 0x27:
		s.adc_value = 0x00;
		s.adc_bits = 8;
		return;
	case 0x25:
		s.adc_value = m_wheel;
		s.adc_bits = 8;
		return;
	case 0x26:
		s.adc_value = 0x80;
		s.adc_bits = 8;
		return;
	case 0x29:
		s.reply = value;
		return;
	case 0x2A:
		s.motor_value = value;
		if (s.motor_register == TORQUE)
			s.torque_value = value;
		else if (s.motor_register == 0xFF || !(s.motor_register & 0x08))
			s.torque_value = 0x80;	/* at rest, or reset */
		return;
	case 0x2D:
		s.lamps = value;
		return;
	case 0x2E:
		s.motor_register = value;
		return;
	default:
		return;	/* the other ports of the CTC, the digits, the watchdog */
	}
}

void
Drive838_13366::save(State &out) const
{
	out = m_state;
	m_cpu.save(out.cpu);
}

void
Drive838_13366::load(const State &in)
{
	m_state = in;
	m_cpu.load(in.cpu);
}

} /* namespace odin */
