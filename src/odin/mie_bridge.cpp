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

#include "odin/mie_bridge.hpp"
#include "odin/card_reader.hpp"
#include "odin/eeprom_93c46.hpp"
#include "odin/jvs_io_13551.hpp"
#include "odin/mie.hpp"

#include <array>
#include <cstring>
#include <memory>
#include <span>
#include <string>

namespace {

/* The Hikaru main board around the MIE (MAME hikaru.cpp, MIE GPIO
 * ports): port F drives the 93C46 (0 SK, 1 DI, 2 ORG, 3 CS), port G
 * reads its DO on bit 0 and the Test and Service switches, active low,
 * on bits 2 and 3. Port A is an unpopulated DIP switch, B the pending
 * interrupts, C their mask, D the resets, E the LEDs. */
class Board : public odin::MieHost, public odin::JvsInputs {
public:
	Board(std::span<const uint8_t> rom, std::span<uint8_t> ram, uint32_t clock_hz,
	      odin_mie_inputs_fn read_inputs, void *ctx)
		: m_read_inputs(read_inputs), m_ctx(ctx), m_clock_hz(clock_hz), m_cycle_acc(0),
		  m_rom(copy_rom(rom)),
		  m_eeprom(odin::Mie::CLOCK_HZ), m_io(*this), m_mie(*this, m_rom, ram), m_log(nullptr)
	{
		m_inputs = {};
		m_mie.jvs().add_device(&m_io);
	}

	static std::array<uint8_t, odin::Mie::ROM_SIZE> copy_rom(std::span<const uint8_t> rom)
	{
		std::array<uint8_t, odin::Mie::ROM_SIZE> out;
		std::memcpy(out.data(), rom.data(), out.size());
		return out;
	}

	uint8_t gpio_read(uint8_t port) override
	{
		switch (port) {
		case 6: {
			/* Fan sensors (4-6) are not populated, read high. Bit 7
			 * is the GPU JTAG chain output, idle high. The game
			 * treats bit 3 as Test and bit 2 as Service (BOOTROM
			 * credit and test-menu code); MAME labels them the
			 * other way round. */
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
			/* Pending interrupts: none routed through the MIE here. */
			return 0x00;
		default:
			/* Unpopulated switches and unused inputs read high. */
			return 0xFF;
		}
	}

	void gpio_write(uint8_t port, uint8_t value) override
	{
		if (port == 5)
			m_eeprom.set_pins((value & 0x08) != 0, (value & 0x01) != 0,
			                  (value & 0x02) != 0, (value & 0x04) != 0);
	}

	uint8_t gpio_readback(uint8_t port, uint8_t latched) override
	{
		/* HACK: the BOOTROM (0x0C00B860, every version) writes port E
		 * = 0xF0 with the port as output, then waits for bits 0-3 (the
		 * LED pins) to read high. MAME reads 0xFF there and boots; the
		 * pin levels are not measured on hardware. Pulled high here
		 * until they are. */
		if (port == 4)
			return static_cast<uint8_t>(latched | 0x0F);
		return latched;
	}

	uint8_t system() override
	{
		poll_inputs();
		return m_inputs.jvs_system;
	}

	uint16_t player(uint8_t index) override
	{
		return index < 2 ? m_inputs.jvs_player[index] : 0;
	}

	uint16_t analog(uint8_t channel) override
	{
		return channel < 8 ? m_inputs.jvs_analog[channel] : 0x8000;
	}

	void reset()
	{
		m_cycle_acc = 0;
		m_eeprom.reset();
		m_io.reset();
		m_mie.reset();
		if (m_card)
			m_card->reset();
	}

	/* The whole board. Plain data. */
	struct State {
		uint64_t cycle_acc;
		odin::Eeprom93c46State eeprom;
		odin::JvsIo13551State io;
		odin::JvsHostState jvs;
		odin::MieState mie;
		odin::CardReaderState card;
		uint8_t has_card;
	};

	void save(State &out)
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

	bool load(const State &in)
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

	/* The card reader on the serial block of the MIE. */
	bool attach_card(const char *card_file, FILE *trace)
	{
		m_card = std::make_unique<odin::CardReader>(card_file ? std::string(card_file) : std::string());
		m_card->set_log(m_log);
		m_card->set_trace(trace);
		m_mie.set_serial_device(m_card.get());
		return m_card->insert_card();
	}

	void set_log(FILE *out)
	{
		m_log = out;
		m_mie.set_log(out);
		if (m_card)
			m_card->set_log(out);
	}

	void run(uint32_t cycles)
	{
		m_cycle_acc += static_cast<uint64_t>(cycles) * odin::Mie::CLOCK_HZ;
		const uint64_t z80_cycles = m_cycle_acc / m_clock_hz;
		m_cycle_acc -= z80_cycles * m_clock_hz;
		if (z80_cycles) {
			m_mie.run(static_cast<uint32_t>(z80_cycles));
			m_eeprom.tick(static_cast<uint32_t>(z80_cycles));
		}
	}

	odin::Mie &chip() { return m_mie; }
	odin::Eeprom93c46 &eeprom() { return m_eeprom; }

private:
	void poll_inputs()
	{
		if (!m_read_inputs)
			return;
		m_read_inputs(m_ctx, &m_inputs);
		for (uint8_t slot = 0; slot < 2; slot++) {
			if (m_inputs.jvs_coin[slot])
				m_io.inc_coin(slot);
		}
	}

	odin_mie_inputs_fn m_read_inputs;
	void *m_ctx;
	uint32_t m_clock_hz;
	uint64_t m_cycle_acc;
	std::array<uint8_t, odin::Mie::ROM_SIZE> m_rom;
	odin_mie_inputs_t m_inputs;
	odin::Eeprom93c46 m_eeprom;
	odin::JvsIo13551 m_io;
	odin::Mie m_mie;
	std::unique_ptr<odin::CardReader> m_card;
	FILE *m_log;
};

enum : uint32_t {
	PAGE_PORTS = 0x00000,
	PAGE_MEM = 0x20000,
	PAGE_END = 0x40000
};

} /* namespace */

struct odin_mie {
	std::unique_ptr<Board> board;
};

extern "C" {

odin_mie_t *
odin_mie_new(const uint8_t *rom, size_t rom_size, uint8_t *ram, size_t ram_size,
             uint32_t host_clock_hz, odin_mie_inputs_fn read_inputs, void *ctx)
{
	if (!rom || rom_size != odin::Mie::ROM_SIZE || !ram || ram_size != odin::Mie::RAM_SIZE ||
	    host_clock_hz == 0)
		return nullptr;
	auto dev = std::make_unique<odin_mie>();
	dev->board = std::make_unique<Board>(std::span<const uint8_t>(rom, rom_size),
	                                     std::span<uint8_t>(ram, ram_size),
	                                     host_clock_hz, read_inputs, ctx);
	return dev.release();
}

void
odin_mie_delete(odin_mie_t *dev)
{
	std::unique_ptr<odin_mie> owned(dev);
}

void
odin_mie_reset(odin_mie_t *dev)
{
	dev->board->reset();
}

/* Byte-wide registers and memory on even addresses (hikaru.cpp
 * localbus_map, umask16 0x00ff). The odd byte of the port page reads
 * 0xFF: the BOOTROM sign-extends the 16-bit read of port A and derives
 * the Test and Service bits from it (doc 07, section 9); not measured
 * on hardware. */
int
odin_mie_host_read(odin_mie_t *dev, uint32_t offset, unsigned size, uint32_t *value)
{
	odin::Mie &chip = dev->board->chip();
	if (offset >= PAGE_END || (size != 1 && size != 2))
		return -1;
	const bool odd = (offset & 1) != 0;
	const uint32_t index = (offset & 0x3FFFF) >> 1;
	uint32_t v;
	if (offset < PAGE_MEM) {
		v = odd ? 0xFF : chip.host_read_port(static_cast<uint8_t>(index & 0xFF));
		if (size == 2)
			v |= 0xFF00;
	} else {
		v = odd ? 0x00 : chip.host_read_mem(static_cast<uint16_t>(index & 0xFFFF));
	}
	*value = v;
	return 0;
}

int
odin_mie_host_write(odin_mie_t *dev, uint32_t offset, unsigned size, uint32_t value)
{
	odin::Mie &chip = dev->board->chip();
	if (offset >= PAGE_END || (size != 1 && size != 2))
		return -1;
	if (offset & 1)
		return 0;
	const uint32_t index = (offset & 0x3FFFF) >> 1;
	const uint8_t byte = static_cast<uint8_t>(value);
	if (offset < PAGE_MEM)
		chip.host_write_port(static_cast<uint8_t>(index & 0xFF), byte);
	else
		chip.host_write_mem(static_cast<uint16_t>(index & 0xFFFF), byte);
	return 0;
}

/* MAME hikaru.cpp:896: (!busak << 8) | 0x600. */
uint32_t
odin_mie_busrq_read(odin_mie_t *dev)
{
	return (dev->board->chip().host_busak_read() ? 0u : 0x100u) | 0x600u;
}

void
odin_mie_busrq_write(odin_mie_t *dev, uint32_t value)
{
	dev->board->chip().set_busrq((value & 0x100) != 0);
}

void
odin_mie_run(odin_mie_t *dev, uint32_t cycles)
{
	dev->board->run(cycles);
}

void
odin_mie_set_log(odin_mie_t *dev, FILE *out)
{
	dev->board->set_log(out);
}

void
odin_mie_set_trace(odin_mie_t *dev, FILE *out, uint32_t max_instructions, uint16_t start_pc)
{
	dev->board->chip().set_trace(out, max_instructions, start_pc);
}

void
odin_mie_eeprom_load(odin_mie_t *dev, const uint8_t *data, size_t size)
{
	if (data)
		dev->board->eeprom().load(std::span<const uint8_t>(data, size));
}

void
odin_mie_eeprom_read(odin_mie_t *dev, uint8_t *data, size_t size)
{
	const std::span<uint8_t> cells = dev->board->eeprom().data();
	if (data && size >= cells.size())
		std::memcpy(data, cells.data(), cells.size());
}

int
odin_mie_card_attach(odin_mie_t *dev, const char *card_file, FILE *trace)
{
	return dev->board->attach_card(card_file, trace) ? 1 : 0;
}

size_t
odin_mie_state_size(void)
{
	return sizeof(Board::State);
}

void
odin_mie_save_state(odin_mie_t *dev, void *out)
{
	auto state = std::make_unique<Board::State>();
	std::memset(state.get(), 0, sizeof(Board::State));
	dev->board->save(*state);
	std::memcpy(out, state.get(), sizeof(Board::State));
}

int
odin_mie_load_state(odin_mie_t *dev, const void *in)
{
	auto state = std::make_unique<Board::State>();
	std::memcpy(state.get(), in, sizeof(Board::State));
	return dev->board->load(*state) ? 0 : -1;
}

} /* extern "C" */
