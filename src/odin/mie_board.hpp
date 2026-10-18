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

#ifndef ODIN_MIE_BOARD_HPP
#define ODIN_MIE_BOARD_HPP

#include <array>
#include <cstdint>
#include <cstdio>
#include <functional>
#include <memory>
#include <span>

#include <string>

#include "odin/card_reader.hpp"
#include "odin/eeprom_93c46.hpp"
#include "odin/jvs.hpp"
#include "odin/jvs_837_13551.hpp"
#include "odin/jvs_837_13741.hpp"
#include "odin/mie.hpp"

namespace odin {

/* The I/O side of the Hikaru main board: the 315-6146 MIE, its 93C46
 * EEPROM, the JVS link and the 837-13551 I/O board on it, and the wiring
 * of the board around the chip (MAME hikaru.cpp, MIE GPIO ports): port F
 * drives the 93C46 (0 SK, 1 DI, 2 ORG, 3 CS), port G reads its DO on bit 0
 * and the Test and Service switches, active low, on bits 2 and 3. Port A is
 * an unpopulated DIP switch, D the resets, E the LEDs.
 *
 * Ports B and C are the interrupt controller of the board, with the names
 * of the schematics. B reads what is pending: bit 0 ANT_INT (the command
 * processor), 4 SZK_INT (the sound boards), 5 CN4_INT, 6 CN3_INT0, 7
 * CN3_INT1, and on bits 1 to 3 what the chip itself puts on pins 4 to 6 of
 * port F. C is the mask of what reaches IRL2 of the main SH-4. */
class MieBoard : public MieHost, public JvsInputs {
public:
	/* What the cabinet feeds the board. JVS bit order: system byte bit 7
	 * TEST; player word high byte bit 7 START, 6 SERVICE, 5 UP, 4 DOWN,
	 * 3 LEFT, 2 RIGHT, 1 PUSH1, 0 PUSH2; analog channels 0x8000 centre. */
	struct Inputs {
		int mainboard_test;	/* SW7 on the main board */
		int mainboard_service;	/* SW8 */
		uint8_t jvs_system;
		uint16_t jvs_player[2];
		uint16_t jvs_analog[8];
		int jvs_coin[2];	/* 1 = a coin went through since the last call */
	};

	/* The whole board. Plain data. */
	struct State {
		uint64_t cycle_acc;
		Eeprom93c46State eeprom;
		JvsIoBoardState io;
		JvsHostState jvs;
		MieState mie;
		CardReaderState card;
		uint8_t has_card;
	};

	/* rom: the 2 KB ROM of the 315-6146, Mie::ROM_SIZE bytes, copied.
	 * ram: the 32 KB half of the Z80 memory at 8000-FFFF, Mie::RAM_SIZE
	 * bytes, owned by the caller and shared with the window of the host.
	 * clock_hz: the clock of the cycles given to run. read_inputs: called
	 * when the board looks at its switches. io_board: the I/O board on the
	 * JVS cable, one that knows_io_board. */
	MieBoard(std::span<const uint8_t> rom, std::span<uint8_t> ram, uint32_t clock_hz,
	         std::function<void(Inputs &)> read_inputs, const std::string &io_board = "837-13551");

	/* The I/O boards there is a node for: 837-13551, 837-13741. */
	static bool knows_io_board(const std::string &name);

	void reset();
	/* Advances the board by that many cycles of the main processor. */
	void run(uint32_t cycles);

	/* The window of the board at 00800000-0083FFFF of the main
	 * processor, by byte offset: 00000-1FFFF the I/O ports of the Z80,
	 * 20000-3FFFF its memory, one byte on each even address. False for a
	 * size the window does not take. */
	bool host_read(uint32_t offset, unsigned size, uint32_t &value);
	bool host_write(uint32_t offset, unsigned size, uint32_t value);
	/* The bus request register (01000000 and 01000100 of the main
	 * processor): bit 8 written is BUSRQ, bit 8 read is clear while the
	 * bus is granted. */
	uint32_t busrq_read();
	void busrq_write(uint32_t value);

	/* Port D carries the resets of the main board, active low (bit 0 the
	 * system, 1 the GPU, 2 the slave SH-4, 3 CN4, 4 the display chip, 5
	 * the sound boards), and on bits 6 and 7 the choice of the video clock.
	 * The function is called with the pins each time the chip drives a
	 * new value. */
	void set_port_d(std::function<void(uint8_t)> fn) { m_port_d = std::move(fn); }
	/* The pins now. HEURISTIC: high while the chip does not drive them. */
	uint8_t port_d() const;

	/* The lines of the interrupt controller that come from outside,
	 * active high, on the bits of port B. They are not part of the
	 * state. */
	void set_irq_lines(uint8_t lines) { m_irq_lines = lines & 0xF1; }
	/* What port B reads. HEURISTIC: pins 4 to 6 of port F ask for
	 * nothing while the chip does not drive them. */
	uint8_t irq_pending() const;
	/* IRL2 of the main SH-4: something pending passes the mask.
	 * HEURISTIC: nothing passes while the chip does not drive the mask. */
	bool irq_out() const;

	void set_log(FILE *out);
	/* See Mie::set_waits_counted. */
	void set_waits_counted(bool on) { m_mie.set_waits_counted(on); }
	uint64_t waits_counted() const { return m_mie.waits_counted(); }
	/* Z80 bus trace for max_instructions instructions from the first
	 * time the PC reaches start_pc (0: from reset). */
	void set_trace(FILE *out, uint32_t max_instructions, uint16_t start_pc)
	{
		m_mie.set_trace(out, max_instructions, start_pc);
	}

	/* The 128 bytes of the EEPROM: blank (FF) until loaded. */
	Eeprom93c46 &eeprom() { return m_eeprom; }
	/* The outputs of the I/O board as the game set them: output 1 is
	 * bit 7 of the first byte. */
	std::span<const uint8_t> io_outputs() const { return m_io->outputs(); }

	/* Puts a CRP-1231 card reader on the serial block of the MIE.
	 * card_file (may be nullptr) holds the three tracks of the card and
	 * is loaded into the reader when it exists; trace (may be nullptr)
	 * gets a line per frame. True when a card was loaded, false when the
	 * reader starts empty. */
	bool attach_card(const char *card_file, FILE *trace);

	void save(State &out);
	/* False when the state has a card reader and the board has none, or
	 * the reverse. */
	bool load(const State &in);

	/* MieHost */
	uint8_t gpio_read(uint8_t port) override;
	void gpio_write(uint8_t port, uint8_t value) override;
	uint8_t gpio_readback(uint8_t port, uint8_t latched) override;

	/* JvsInputs */
	uint8_t system() override;
	uint16_t player(uint8_t index) override;
	uint16_t analog(uint8_t channel) override;

private:
	void poll_inputs();

	std::function<void(Inputs &)> m_read_inputs;
	uint32_t m_clock_hz;
	uint64_t m_cycle_acc;
	std::array<uint8_t, Mie::ROM_SIZE> m_rom;
	Inputs m_inputs;
	Eeprom93c46 m_eeprom;
	JvsHost m_jvs;		/* the JVS cable, from UART 1 of the MIE */
	std::unique_ptr<JvsIoBoard> m_io;
	Mie m_mie;
	std::unique_ptr<CardReader> m_card;
	FILE *m_log;
	uint8_t m_irq_lines;
	std::function<void(uint8_t)> m_port_d;
};

} /* namespace odin */

#endif /* ODIN_MIE_BOARD_HPP */
