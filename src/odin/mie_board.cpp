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

#include "odin/mie_board.hpp"

#include <cstring>
#include <string>
#include <utility>

namespace odin {

namespace {

/* The window of the board on the bus of the main processor */
constexpr uint32_t PAGE_MEM = 0x20000;
constexpr uint32_t PAGE_END = 0x40000;

std::array<uint8_t, Mie::ROM_SIZE>
copy_rom(std::span<const uint8_t> rom)
{
	std::array<uint8_t, Mie::ROM_SIZE> out;

	std::memcpy(out.data(), rom.data(), out.size());
	return out;
}

} /* namespace */

MieBoard::MieBoard(std::span<const uint8_t> rom, std::span<uint8_t> ram, uint32_t clock_hz,
                   std::function<void(Inputs &)> read_inputs)
	: m_read_inputs(std::move(read_inputs)), m_clock_hz(clock_hz), m_cycle_acc(0),
	  m_rom(copy_rom(rom)), m_inputs{},
	  m_eeprom(Mie::CLOCK_HZ), m_io(*this), m_mie(*this, m_rom, ram), m_log(nullptr),
	  m_irq_lines(0)
{
	m_mie.jvs().add_device(&m_io);
}

uint8_t
MieBoard::gpio_read(uint8_t port)
{
	switch (port) {
	case 6: {
		/* Fan sensors (4-6) are not populated, read high. Bit 7 is
		 * the GPU JTAG chain output, idle high. The game treats bit 3
		 * as Test and bit 2 as Service (BOOTROM credit and test-menu
		 * code); MAME labels them the other way round. */
		poll_inputs();
		uint8_t v = 0xF2;
		if (m_eeprom.do_read())
			v |= 0x01;
		if (!m_inputs.mainboard_service)
			v |= 0x04;
		if (!m_inputs.mainboard_test)
			v |= 0x08;
		return v;
	}
	case 1:
		return irq_pending();
	default:
		/* Unpopulated switches and unused inputs read high. */
		return 0xFF;
	}
}

void
MieBoard::gpio_write(uint8_t port, uint8_t value)
{
	if (port == 5)
		m_eeprom.set_pins((value & 0x08) != 0, (value & 0x01) != 0,
		                  (value & 0x02) != 0, (value & 0x04) != 0);
	if (port == 3 && m_port_d)
		m_port_d(value);
}

uint8_t
MieBoard::gpio_readback(uint8_t port, uint8_t latched)
{
	/* HACK: the BOOTROM (0x0C00B860, every version) writes port E = 0xF0
	 * with the port as output, then waits for bits 0-3 (the LED pins) to
	 * read high. MAME reads 0xFF there and boots; the pin levels are not
	 * measured on hardware. Pulled high here until they are. */
	if (port == 4)
		return static_cast<uint8_t>(latched | 0x0F);
	return latched;
}

uint8_t
MieBoard::system()
{
	poll_inputs();
	return m_inputs.jvs_system;
}

uint16_t
MieBoard::player(uint8_t index)
{
	return index < 2 ? m_inputs.jvs_player[index] : 0;
}

uint16_t
MieBoard::analog(uint8_t channel)
{
	return channel < 8 ? m_inputs.jvs_analog[channel] : 0x8000;
}

void
MieBoard::reset()
{
	m_cycle_acc = 0;
	m_eeprom.reset();
	m_io.reset();
	m_mie.reset();
	if (m_card)
		m_card->reset();
}

void
MieBoard::run(uint32_t cycles)
{
	m_cycle_acc += static_cast<uint64_t>(cycles) * Mie::CLOCK_HZ;
	const uint64_t z80_cycles = m_cycle_acc / m_clock_hz;
	m_cycle_acc -= z80_cycles * m_clock_hz;
	if (z80_cycles) {
		m_mie.run(static_cast<uint32_t>(z80_cycles));
		m_eeprom.tick(static_cast<uint32_t>(z80_cycles));
	}
}

/* Byte-wide registers and memory on even addresses (hikaru.cpp
 * localbus_map, umask16 0x00ff). The odd byte of the port page reads 0xFF:
 * the BOOTROM sign-extends the 16-bit read of port A and derives the Test
 * and Service bits from it (doc 07, section 9); not measured on hardware. */
bool
MieBoard::host_read(uint32_t offset, unsigned size, uint32_t &value)
{
	if (offset >= PAGE_END || (size != 1 && size != 2))
		return false;
	const bool odd = (offset & 1) != 0;
	const uint32_t index = (offset & 0x3FFFF) >> 1;
	uint32_t v;
	if (offset < PAGE_MEM) {
		v = odd ? 0xFF : m_mie.host_read_port(static_cast<uint8_t>(index & 0xFF));
		if (size == 2)
			v |= 0xFF00;
	} else {
		v = odd ? 0x00 : m_mie.host_read_mem(static_cast<uint16_t>(index & 0xFFFF));
	}
	value = v;
	return true;
}

bool
MieBoard::host_write(uint32_t offset, unsigned size, uint32_t value)
{
	if (offset >= PAGE_END || (size != 1 && size != 2))
		return false;
	if (offset & 1)
		return true;
	const uint32_t index = (offset & 0x3FFFF) >> 1;
	const uint8_t byte = static_cast<uint8_t>(value);
	if (offset < PAGE_MEM)
		m_mie.host_write_port(static_cast<uint8_t>(index & 0xFF), byte);
	else
		m_mie.host_write_mem(static_cast<uint16_t>(index & 0xFFFF), byte);
	return true;
}

/* MAME hikaru.cpp:896: (!busak << 8) | 0x600. */
uint32_t
MieBoard::busrq_read()
{
	return (m_mie.host_busak_read() ? 0u : 0x100u) | 0x600u;
}

void
MieBoard::busrq_write(uint32_t value)
{
	m_mie.set_busrq((value & 0x100) != 0);
}

uint8_t
MieBoard::port_d() const
{
	return m_mie.gpio_is_output(3) ? m_mie.gpio_latch(3) : 0xFF;
}

uint8_t
MieBoard::irq_pending() const
{
	const uint8_t own = m_mie.gpio_is_output(5) ? (m_mie.gpio_latch(5) >> 3) & 0x0E : 0;
	return static_cast<uint8_t>(m_irq_lines | own);
}

bool
MieBoard::irq_out() const
{
	return m_mie.gpio_is_output(2) && (irq_pending() & m_mie.gpio_latch(2)) != 0;
}

void
MieBoard::set_log(FILE *out)
{
	m_log = out;
	m_mie.set_log(out);
	if (m_card)
		m_card->set_log(out);
}

bool
MieBoard::attach_card(const char *card_file, FILE *trace)
{
	m_card = std::make_unique<CardReader>(card_file ? std::string(card_file) : std::string());
	m_card->set_log(m_log);
	m_card->set_trace(trace);
	m_mie.set_serial_device(m_card.get());
	return m_card->insert_card();
}

void
MieBoard::save(State &out)
{
	out.cycle_acc = m_cycle_acc;
	m_eeprom.save(out.eeprom);
	m_io.save(out.io);
	m_mie.jvs().save(out.jvs);
	m_mie.save(out.mie);
	out.has_card = m_card ? 1 : 0;
	if (m_card)
		m_card->save(out.card);
}

bool
MieBoard::load(const State &in)
{
	if ((in.has_card != 0) != (m_card != nullptr))
		return false;
	m_cycle_acc = in.cycle_acc;
	m_eeprom.load(in.eeprom);
	m_io.load(in.io);
	m_mie.jvs().load(in.jvs);
	m_mie.load(in.mie);
	if (m_card)
		m_card->load(in.card);
	return true;
}

void
MieBoard::poll_inputs()
{
	if (!m_read_inputs)
		return;
	m_read_inputs(m_inputs);
	for (uint8_t slot = 0; slot < 2; slot++) {
		if (m_inputs.jvs_coin[slot])
			m_io.inc_coin(slot);
	}
}

} /* namespace odin */
