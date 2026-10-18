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

/* Checks of the 93C46 against its data sheet instruction set, driven
 * the way the MIE program bit-bangs it (start bit, 2 opcode bits, 7
 * address bits in x8 mode, DO sampled after each SK rising edge, READY
 * polled on DO after a write). The JVS cable and the I/O board are
 * checked in tests/jvs.
 *
 * Build: g++ -std=c++20 -Wall -Wextra -Wpedantic -Wconversion -Wshadow \
 *            -O2 -I src tests/mie/mie_test.cpp src/odin/eeprom_93c46.cpp \
 *            src/odin/jvs.cpp src/odin/jvs_837_13551.cpp src/odin/jvs_837_13741.cpp \
 *            src/odin/mie.cpp \
 *            src/odin/mie_board.cpp src/odin/card_reader.cpp src/odin/z80.cpp \
 *            src/odin/z80_ops.cpp src/odin/io_837_13551.cpp src/odin/tmp90ph44.cpp \
 *            src/odin/tlcs90.cpp -o mie_test
 * Exit status is 0 when every check passes. */

#include "odin/eeprom_93c46.hpp"
#include "odin/mie_board.hpp"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <string>
#include <utility>
#include <vector>

namespace {

uint32_t g_failed = 0;
uint32_t g_passed = 0;

void check(bool ok, const char *what)
{
	if (ok) {
		g_passed++;
	} else {
		g_failed++;
		std::printf("FAIL: %s\n", what);
	}
}

/* 93C46 on three pins, x8 mode unless said otherwise. */
class Wire {
public:
	explicit Wire(odin::Eeprom93c46 &e, bool org = false) : m_e(e), m_org(org) {}

	void cs(bool level) { m_cs = level; m_sk = false; apply(); }
	/* One clock with DI at `bit`; returns DO after the rising edge. */
	bool clock(bool bit)
	{
		m_di = bit;
		m_sk = false;
		apply();
		m_sk = true;
		apply();
		return m_e.do_read();
	}
	void bits(uint32_t value, uint8_t count)
	{
		for (uint8_t i = 0; i < count; i++) {
			clock(((value >> (count - 1 - i)) & 1) != 0);
		}
	}
	uint16_t read_bits(uint8_t count)
	{
		uint16_t v = 0;
		for (uint8_t i = 0; i < count; i++) {
			v = static_cast<uint16_t>((v << 1) | (clock(false) ? 1 : 0));
		}
		return v;
	}
	bool out() { m_sk = false; apply(); return m_e.do_read(); }

private:
	void apply() { m_e.set_pins(m_cs, m_sk, m_di, m_org); }
	odin::Eeprom93c46 &m_e;
	bool m_org;
	bool m_cs = false, m_sk = false, m_di = false;
};

constexpr uint8_t ABITS8 = 7;
constexpr uint8_t ABITS16 = 6;

/* start, opcode 10, address */
uint8_t ee_read8(Wire &w, uint8_t addr)
{
	w.cs(true);
	w.bits(0x6, 3);
	w.bits(addr, ABITS8);
	const bool dummy = w.out();
	check(!dummy, "read: dummy zero after the address");
	const uint8_t v = static_cast<uint8_t>(w.read_bits(8));
	w.cs(false);
	return v;
}

void ee_ewen(Wire &w)
{
	w.cs(true);
	w.bits(0x4, 3);
	w.bits(0x60, ABITS8);
	w.cs(false);
}

void ee_ewds(Wire &w)
{
	w.cs(true);
	w.bits(0x4, 3);
	w.bits(0x00, ABITS8);
	w.cs(false);
}

void ee_write8(Wire &w, odin::Eeprom93c46 &e, uint8_t addr, uint8_t value, bool expect_busy)
{
	w.cs(true);
	w.bits(0x5, 3);
	w.bits(addr, ABITS8);
	w.bits(value, 8);
	w.cs(false);
	w.cs(true);
	if (expect_busy) {
		check(!e.do_read(), "write: BUSY right after CS falls");
		e.tick(1000);
		check(!e.do_read(), "write: still BUSY before the write cycle ends");
		e.tick(100000);
		check(e.do_read(), "write: READY after the write cycle");
	} else {
		check(e.do_read(), "write while disabled: no busy cycle");
	}
	w.cs(false);
}

void ee_erase8(Wire &w, uint8_t addr)
{
	w.cs(true);
	w.bits(0x7, 3);
	w.bits(addr, ABITS8);
	w.cs(false);
}

void test_eeprom()
{
	odin::Eeprom93c46 e(16000000);
	std::vector<uint8_t> image(128);
	for (uint32_t i = 0; i < 128; i++) {
		image[i] = static_cast<uint8_t>(i * 3 + 1);
	}
	e.load(image);
	Wire w(e);

	check(ee_read8(w, 0) == 0x01, "read cell 0");
	check(ee_read8(w, 0x7F) == static_cast<uint8_t>(0x7F * 3 + 1), "read cell 127");
	check(ee_read8(w, 0x10) == 0x31, "read cell 16");

	/* Sequential read: the next cell follows without a new instruction. */
	w.cs(true);
	w.bits(0x6, 3);
	w.bits(0x20, ABITS8);
	check(w.read_bits(8) == image[0x20] && w.read_bits(8) == image[0x21], "sequential read");
	w.cs(false);

	/* Writes are refused until EWEN. */
	ee_write8(w, e, 5, 0xAA, false);
	check(ee_read8(w, 5) == image[5], "write ignored while disabled");

	ee_ewen(w);
	check(e.write_enabled(), "EWEN");
	ee_write8(w, e, 5, 0xAA, true);
	check(ee_read8(w, 5) == 0xAA, "write then read back");

	/* A busy chip ignores a new instruction. */
	ee_write8(w, e, 6, 0x55, true);
	w.cs(true);
	w.bits(0x5, 3);
	w.bits(7, ABITS8);
	w.bits(0x33, 8);
	w.cs(false);
	e.tick(100000);
	check(ee_read8(w, 6) == 0x55 && ee_read8(w, 7) == 0x33, "second write after READY");

	ee_erase8(w, 5);
	e.tick(100000);
	check(ee_read8(w, 5) == 0xFF, "ERASE sets the cell to FF");

	ee_ewds(w);
	check(!e.write_enabled(), "EWDS");
	ee_write8(w, e, 5, 0x12, false);
	check(ee_read8(w, 5) == 0xFF, "write ignored after EWDS");

	/* x16 organisation: 6 address bits, 16 data bits, big-endian cells. */
	Wire w16(e, true);
	w16.cs(true);
	w16.bits(0x6, 3);
	w16.bits(1, ABITS16);
	check(w16.read_bits(16) == static_cast<uint16_t>((image[2] << 8) | image[3]), "x16 read");
	w16.cs(false);
}

} /* namespace */

/* The interrupt controller on ports B, C and F, through the window of the
 * host: a port is at twice its number, the direction register at 0x10. */
void test_interrupts()
{
	std::vector<uint8_t> rom(2048, 0), ram(32768, 0);
	odin::MieBoard mie(rom, ram, 200000000, nullptr);
	uint32_t v = 0;

	mie.set_irq_lines(0x01);
	check(mie.irq_pending() == 0x01 && !mie.irq_out(),
	      "nothing passes while port C is an input");
	mie.host_write(2 * 2, 1, 0xE1);
	check(!mie.irq_out(), "writing the latch of port C is not driving it");
	mie.host_write(2 * 8, 1, 0xFB);
	check(mie.irq_out(), "ANT_INT passes the mask E1");
	mie.set_irq_lines(0x10);
	check(!mie.irq_out(), "SZK_INT does not");
	mie.host_read(2 * 1, 1, v);
	check(v == 0x10, "but port B reads it");
	mie.host_write(2 * 2, 1, 0xF1);
	check(mie.irq_out(), "until the mask has its bit");
	mie.set_irq_lines(0);
	check(!mie.irq_out(), "a line let go asks no more");

	mie.host_write(2 * 5, 1, 0x70);
	check(mie.irq_pending() == 0, "port F asks for nothing while it is an input");
	mie.host_write(2 * 8, 1, 0xDB);
	check(mie.irq_pending() == 0x0E, "pins 4 to 6 of port F are bits 1 to 3 of port B");
	check(!mie.irq_out(), "masked by F1");
	mie.host_write(2 * 2, 1, 0x04);
	check(mie.irq_out(), "and passing with their bit in the mask");
	mie.host_write(2 * 5, 1, 0x50);
	check(!mie.irq_out(), "each pin on its own bit");
}

/* Port D, the resets of the main board: the board is told each value the
 * chip drives. */
void test_port_d()
{
	std::vector<uint8_t> rom(2048, 0), ram(32768, 0);
	odin::MieBoard mie(rom, ram, 200000000, nullptr);
	uint8_t pins_told = 0;
	int calls = 0;

	mie.set_port_d([&](uint8_t pins) { pins_told = pins; calls++; });
	check(mie.port_d() == 0xFF, "port D is high while the chip does not drive it");
	mie.host_write(2 * 3, 1, 0xFB);
	check(calls == 0 && mie.port_d() == 0xFF, "its latch written, it is still not driven");
	mie.host_write(2 * 8, 1, 0x43);
	check(calls == 1 && pins_told == 0xFB && mie.port_d() == 0xFB,
	      "the latch is driven when the port becomes an output");
	mie.host_write(2 * 3, 1, 0xFF);
	check(calls == 2 && pins_told == 0xFF, "and each new value is told");
}

/* A port driven as an output reads back its latch: port E, the LEDs the
 * master and the slave meet on at start (Hikaru::set_resets). */
void test_port_readback()
{
	std::vector<uint8_t> rom(2048, 0), ram(32768, 0);
	odin::MieBoard mie(rom, ram, 200000000, nullptr);
	uint32_t v = 0;

	mie.host_write(2 * 4, 1, 0xF0);
	mie.host_write(2 * 8, 1, 0x43);
	mie.host_read(2 * 4, 1, v);
	check(v == 0xF0, "port E driven at F0 reads its bits 0-3 low");
	mie.host_write(2 * 4, 1, 0xFF);
	mie.host_read(2 * 4, 1, v);
	check(v == 0xFF, "and high once FF is written");
}

/* The bus of the Z80, asked by the master (01000100) and by the slave
 * (1B000100, the BOOTROM @0C00BEE0): granted while either asks, given
 * back when neither does, and who asks kept in the state. */
void test_bus_two_hosts()
{
	std::vector<uint8_t> rom(2048, 0), ram(32768, 0);
	odin::MieBoard mie(rom, ram, 200000000, nullptr);
	const auto granted = [&mie] { return (mie.busrq_read() & 0x100) == 0; };
	odin::MieBoard::State saved{};

	mie.host_write(2 * 0x30, 1, 0x40);
	check(!granted(), "the bus is the Z80's while nobody asks");
	mie.busrq_write(1, 0x100);
	check(granted(), "the slave asks: granted");
	mie.busrq_write(0, 0x100);
	mie.busrq_write(1, 0);
	check(granted(), "the slave lets go while the master asks: still granted");
	mie.busrq_write(1, 0x100);
	mie.busrq_write(0, 0);
	check(granted(), "the master lets go while the slave asks: still granted");
	mie.save(saved);
	mie.busrq_write(1, 0);
	check(!granted(), "neither asks: given back");
	mie.load(saved);
	check(granted(), "who asks comes back with a load");
	mie.busrq_write(0, 0);
	check(granted(), "the slave keeps the bus the master lets go after the load");
	mie.busrq_write(1, 0);
	check(!granted(), "until it lets go");
}

/* Loops of the program that only wait: with the turns counted and with
 * every turn run, the chip and its memory must be the same after each call
 * of 505 Z80 clocks, a line of the Hikaru. The host writes 1 at C000 before
 * the call `release`. Returns the share of the clocks that were counted, in
 * percent. */
unsigned waits(const char *name, const std::vector<uint8_t> &program, unsigned release, uint32_t per_call = 6313)
{
	std::vector<uint8_t> rom_a(2048, 0), rom_b(2048, 0), ram_a(32768, 0), ram_b(32768, 0);

	std::copy(program.begin(), program.end(), rom_a.begin());
	std::copy(program.begin(), program.end(), rom_b.begin());
	odin::MieBoard counted(rom_a, ram_a, 200000000, nullptr), run(rom_b, ram_b, 200000000, nullptr);
	bool same = true;
	const unsigned calls = 60;

	run.set_waits_counted(false);
	for (unsigned call = 0; call < calls; call++) {
		if (call == release) {
			counted.host_write(0x20000 + 2 * 0xC000, 1, 1);
			run.host_write(0x20000 + 2 * 0xC000, 1, 1);
		}
		counted.run(per_call);
		run.run(per_call);

		odin::MieBoard::State a{}, b{};
		counted.save(a);
		run.save(b);
		same = same && std::memcmp(&a, &b, sizeof(a)) == 0 && ram_a == ram_b;
	}
	check(same, name);
	check(run.waits_counted() == 0, "nothing is counted when every turn is run");
	return static_cast<unsigned>(counted.waits_counted() * 100 / (calls * per_call * 16ull / 200));
}

void test_waits()
{
	/* 0000: ld a, (C000) / or a / jr z, 0000 / halt */
	unsigned share = waits("a loop that reads the memory until the host writes there",
	                       { 0x3A, 0x00, 0xC0, 0xB7, 0x28, 0xFA, 0x76 }, 20);
	check(share >= 80, "most of its turns are counted, and those of the halt after it");

	/* ld a, (C000) / or a / eight nop / jr z: 61 clocks, in calls of 200,
	 * most ending inside a turn */
	share = waits("a long loop in short calls",
	              { 0x3A, 0x00, 0xC0, 0xB7, 0, 0, 0, 0, 0, 0, 0, 0, 0x28, 0xF2, 0x76 }, 40, 2500);
	check(share >= 90, "its turns are counted from one call to the next");

	/* The same after 30 nop: the search marks the loop on its way */
	std::vector<uint8_t> after(30, 0);
	after.insert(after.end(), { 0x3A, 0x00, 0xC0, 0xB7, 0, 0, 0, 0, 0, 0, 0, 0, 0x28, 0xF2, 0x76 });
	share = waits("a long loop after other code, in short calls", after, 40, 2500);
	check(share >= 90, "its turns are counted from one call to the next too");

	/* The same, with a count in the memory at each turn. */
	share = waits("a loop that writes the memory",
	              { 0x21, 0x01, 0xC0, 0x34, 0x3A, 0x00, 0xC0, 0xB7, 0x28, 0xF6, 0x76 }, 50);
	check(share <= 25, "its turns are run; the halt after it is counted");

	/* 256 turns at most: djnz. */
	waits("a loop that counts its turns in a register",
	      { 0x06, 0x00, 0x3A, 0x00, 0xC0, 0xB7, 0x20, 0x02, 0x10, 0xF8, 0x76 }, 59);

	/* A character sent, then the wait for the transmitter of the UART,
	 * as the program of the MIE does: in a, (15) / bit 5, a / jr z. */
	waits("a loop that reads a port",
	      { 0x3E, 0x55, 0xD3, 0x10, 0xDB, 0x15, 0xCB, 0x6F, 0x28, 0xFA, 0x76 }, 59);

	/* The same with the divisor at 16 and the wait for the end of the
	 * character, in a, (15) / bit 6, a / jr z: the answer of the port
	 * changes in the middle of a call, with the registers of every turn
	 * before it the same. */
	share = waits("a loop that reads a port whose answer changes", {
		0x3E, 0x80, 0xD3, 0x13, 0x3E, 0x10, 0xD3, 0x10, 0xAF, 0xD3, 0x13,
		0x3E, 0x55, 0xD3, 0x10,
		0xDB, 0x15, 0xCB, 0x77, 0x28, 0xFA, 0x76,
	}, 59);
	check(share >= 80, "its turns are run; the halt after it is counted");

	/* A loop that looks at R and forgets it: ld a, r / cp n / jr z, out
	 * / xor a / jr loop. After xor a the registers of every turn are the
	 * same, R aside, and R is what ends the loop. */
	for (const unsigned last : { 0x04u, 0x22u, 0x58u, 0x7Eu }) {
		waits("a loop that reads R",
		      { 0xED, 0x5F, 0xFE, static_cast<uint8_t>(last), 0x28, 0x03, 0xAF, 0x18, 0xF7, 0x76 }, 59);
	}
	/* R read once, then the loop of the first test. */
	share = waits("a loop that waits after R was read", { 0xED, 0x5F, 0x3A, 0x00, 0xC0, 0xB7, 0x28, 0xFA, 0x76 }, 40);
	check(share >= 80, "its turns are counted");

	/* The divisor at 256: a character takes 44 444 clocks, longer than the
	 * 60 calls; the wait for the end of it reads the state of UART 1. */
	share = waits("a loop that waits on UART 1 for longer than its calls", {
		0x3E, 0x80, 0xD3, 0x13, 0xAF, 0xD3, 0x10, 0x3E, 0x01, 0xD3, 0x11, 0xAF, 0xD3, 0x13,
		0x3E, 0x55, 0xD3, 0x10, 0xDB, 0x15, 0xCB, 0x77, 0x28, 0xFA, 0x76,
	}, 99);
	check(share >= 90, "its turns are counted up to where UART 1 would change");

	/* The divisor at 16, 2 560 clocks a character, and the wait for its end
	 * with 70 nop in each turn: more instructions than are kept, the turns
	 * counted whole, up to where UART 1 would change */
	std::vector<uint8_t> slow = { 0x3E, 0x80, 0xD3, 0x13, 0x3E, 0x10, 0xD3, 0x10, 0xAF, 0xD3, 0x13, 0x3E, 0x55, 0xD3, 0x10,
	                              0xDB, 0x15, 0xCB, 0x77 };
	slow.resize(slow.size() + 70, 0);
	for (const uint8_t b : { uint8_t(0x28), uint8_t(0xB4), uint8_t(0x76) })
		slow.push_back(b);
	waits("a long loop that waits on UART 1", slow, 99);
	waits("a long loop that waits on UART 1, in long calls", slow, 99, 63130);

	/* F1 01 to the imitation of the 837-13551, then each of the six bytes
	 * of its reply waited for (in a, (15) / bit 0, a / jr z) and kept */
	waits("a loop that waits for the bytes of a reply", {
		0x3E, 0x80, 0xD3, 0x13, 0x3E, 0x08, 0xD3, 0x10, 0xAF, 0xD3, 0x13, 0x3E, 0x01, 0xD3, 0x90,
		0x3E, 0xE0, 0xD3, 0x10, 0x3E, 0xFF, 0xD3, 0x10, 0x3E, 0x03, 0xD3, 0x10, 0x3E, 0xF1, 0xD3, 0x10,
		0x3E, 0x01, 0xD3, 0x10, 0x3E, 0xF4, 0xD3, 0x10, 0xDB, 0x15, 0xCB, 0x77, 0x28, 0xFA, 0xAF, 0xD3, 0x90,
		0x21, 0x00, 0xC1, 0x06, 0x06, 0xDB, 0x15, 0xCB, 0x47, 0x28, 0xFA, 0xDB, 0x10, 0x77, 0x23, 0x10, 0xF4, 0x76,
	}, 99);

	/* A character sent at each turn until the host writes. */
	waits("a loop that writes a port",
	      { 0x3A, 0x00, 0xC0, 0xB7, 0x20, 0x06, 0x3E, 0x41, 0xD3, 0x10, 0x18, 0xF4, 0x76 }, 3);

	/* The divisor of the UART at 16, three characters to send, 2 560
	 * clocks each, then the loop of the first test: the transmitter gets
	 * the clocks of the turns that are only counted. */
	share = waits("a loop that waits while the UART sends", {
		0x3E, 0x80, 0xD3, 0x13,		/* ld a, 80 / out (13), a: the divisor latch */
		0x3E, 0x10, 0xD3, 0x10,		/* ld a, 10 / out (10), a */
		0xAF, 0xD3, 0x13,		/* xor a / out (13), a */
		0x3E, 0x55, 0xD3, 0x10, 0xD3, 0x10, 0xD3, 0x10,
		0x3A, 0x00, 0xC0, 0xB7, 0x28, 0xFA, 0x76,
	}, 40);
	check(share >= 80, "its turns are counted");

	/* A byte given to the serial link, 640 clocks, then the loop of the
	 * first test: the link gets the clocks of the counted turns too. */
	share = waits("a loop that waits while the serial link sends", {
		0x3E, 0x55, 0xD3, 0x0A,		/* ld a, 55 / out (0A), a: the data */
		0x3E, 0x01, 0xD3, 0x09,		/* ld a, 01 / out (09), a: send */
		0x3A, 0x00, 0xC0, 0xB7, 0x28, 0xFA, 0x76,
	}, 40);
	check(share >= 80, "its turns are counted");
}

/* A state loaded where the program waits, with the memory loaded too as
 * the board does it (hikaru_state.cpp): the loop sees that memory. */
void test_wait_loaded()
{
	const std::vector<uint8_t> program = { 0x3A, 0x00, 0xC0, 0xB7, 0, 0, 0, 0, 0, 0, 0, 0, 0x28, 0xF2, 0x76 };
	std::vector<uint8_t> rom_a(2048, 0), rom_b(2048, 0), ram_a(32768, 0), ram_b(32768, 0);

	std::copy(program.begin(), program.end(), rom_a.begin());
	std::copy(program.begin(), program.end(), rom_b.begin());
	odin::MieBoard counted(rom_a, ram_a, 200000000, nullptr), run(rom_b, ram_b, 200000000, nullptr);
	run.set_waits_counted(false);
	for (int call = 0; call < 20; call++) {
		counted.run(6313);
		run.run(6313);
	}
	odin::MieBoard::State a{}, b{};
	counted.save(a);
	run.save(b);
	ram_a[0x4000] = ram_b[0x4000] = 1;
	counted.load(a);
	run.load(b);
	bool same = true;
	for (int call = 0; call < 5; call++) {
		counted.run(6313);
		run.run(6313);
		odin::MieBoard::State c{}, d{};
		counted.save(c);
		run.save(d);
		same = same && std::memcmp(&c, &d, sizeof(c)) == 0;
	}
	check(same && ram_a[0x4000] == 1, "a state loaded with the memory: the loop that waited sees it");
}

/* The host takes the bus (BUSRQ with bit 6 of port 30) while the program
 * waits: the CPU does not move, then goes on. */
void test_wait_bus()
{
	const std::vector<uint8_t> program = { 0x3A, 0x00, 0xC0, 0xB7, 0, 0, 0, 0, 0, 0, 0, 0, 0x28, 0xF2, 0x76 };
	std::vector<uint8_t> rom_a(2048, 0), rom_b(2048, 0), ram_a(32768, 0), ram_b(32768, 0);

	std::copy(program.begin(), program.end(), rom_a.begin());
	std::copy(program.begin(), program.end(), rom_b.begin());
	odin::MieBoard counted(rom_a, ram_a, 200000000, nullptr), run(rom_b, ram_b, 200000000, nullptr);
	run.set_waits_counted(false);
	bool same = true;
	for (int call = 0; call < 30; call++) {
		if (call == 10)
			for (odin::MieBoard *mie : { &counted, &run }) {
				mie->host_write(2 * 0x30, 1, 0x40);
				mie->busrq_write(0, 0x100);
			}
		if (call == 20)
			for (odin::MieBoard *mie : { &counted, &run })
				mie->busrq_write(0, 0);
		counted.run(6313);
		run.run(6313);
		odin::MieBoard::State a{}, b{};
		counted.save(a);
		run.save(b);
		same = same && std::memcmp(&a, &b, sizeof(a)) == 0;
	}
	check(same, "the bus taken by the host while the program waits: the same as every turn run");
}

/* The speed of the JVS line: the divisor 8 of the programs, nine
 * characters, then a loop of 37 clocks that counts its turns until the
 * transmitter is empty. At 115200 bauds a character is 86.8 us, 1388.9
 * clocks of the Z80 at 16 MHz: 12 500 for the nine, 337.8 turns. */
void test_uart_speed()
{
	std::vector<uint8_t> rom(2048, 0), ram(32768, 0);
	const std::vector<uint8_t> program = {
		0x3E, 0x80, 0xD3, 0x13,		/* ld a, 80 / out (13), a: the divisor latch */
		0x3E, 0x08, 0xD3, 0x10,		/* ld a, 08 / out (10), a */
		0xAF, 0xD3, 0x13,		/* xor a / out (13), a */
		0x21, 0x00, 0x00, 0x3E, 0x55,	/* ld hl, 0 / ld a, 55 */
		0xD3, 0x10, 0xD3, 0x10, 0xD3, 0x10, 0xD3, 0x10, 0xD3, 0x10,
		0xD3, 0x10, 0xD3, 0x10, 0xD3, 0x10, 0xD3, 0x10,
		0x23, 0xDB, 0x15, 0xCB, 0x77, 0x28, 0xF9,	/* inc hl / in a, (15) / bit 6, a / jr z */
		0x22, 0x00, 0xC1, 0x76,		/* ld (C100), hl / halt */
	};

	std::copy(program.begin(), program.end(), rom.begin());
	odin::MieBoard mie(rom, ram, odin::Mie::CLOCK_HZ, nullptr);
	mie.run(20000);
	uint32_t low = 0, high = 0;
	mie.host_read(0x20000 + 2 * 0xC100, 1, low);
	mie.host_read(0x20000 + 2 * 0xC101, 1, high);
	const uint32_t turns = (high << 8) | low;
	check(turns >= 336 && turns <= 339, "nine characters at 115200 bauds");
}

/* The first byte of a reply: the program sends F1 01 to every node, waits
 * for the end of its last character, switches the driver off, then counts
 * turns of a loop of 37 clocks until a byte is there. The 837-13741 starts
 * its reply 1740 clocks after the request (epr21868_bench) and the byte
 * lands a character (1388.9 clocks) later: 84.6 turns. The 837-13551 is not
 * timed: two characters, 75.1 turns. */
unsigned first_byte_turns(const std::string &io_board)
{
	std::vector<uint8_t> rom(2048, 0), ram(32768, 0);
	const std::vector<uint8_t> program = {
		0x3E, 0x80, 0xD3, 0x13, 0x3E, 0x08, 0xD3, 0x10, 0xAF, 0xD3, 0x13,	/* the divisor at 8 */
		0x3E, 0x01, 0xD3, 0x90,							/* the driver on */
		0x3E, 0xE0, 0xD3, 0x10, 0x3E, 0xFF, 0xD3, 0x10, 0x3E, 0x03, 0xD3, 0x10,
		0x3E, 0xF1, 0xD3, 0x10, 0x3E, 0x01, 0xD3, 0x10, 0x3E, 0xF4, 0xD3, 0x10,
		0xDB, 0x15, 0xCB, 0x77, 0x28, 0xFA,					/* in a, (15) / bit 6, a / jr z */
		0xAF, 0xD3, 0x90, 0x21, 0x00, 0x00,					/* the driver off / ld hl, 0 */
		0x23, 0xDB, 0x15, 0xCB, 0x47, 0x28, 0xF9,				/* inc hl / in a, (15) / bit 0, a / jr z */
		0x22, 0x00, 0xC1, 0x76,							/* ld (C100), hl / halt */
	};

	std::copy(program.begin(), program.end(), rom.begin());
	odin::MieBoard mie(rom, ram, odin::Mie::CLOCK_HZ, nullptr, io_board);
	mie.run(40000);
	uint32_t low = 0, high = 0;
	mie.host_read(0x20000 + 2 * 0xC100, 1, low);
	mie.host_read(0x20000 + 2 * 0xC101, 1, high);
	return (high << 8) | low;
}

void test_reply_time()
{
	const unsigned timed = first_byte_turns("837-13741"), untimed = first_byte_turns("837-13551");

	check(timed >= 83 && timed <= 86, "the 837-13741 answers when its program does");
	check(untimed >= 74 && untimed <= 77, "the 837-13551, two characters after the request");
}

/* Short runs: the MIE ends the instruction it is in, and what it ran past
 * its share comes off the next. 0000: inc hl / ld (C100), hl / jr 0000,
 * 6 + 16 + 12 clocks; 10 000 runs of 17 clocks, 5 000 turns. */
void test_short_runs()
{
	std::vector<uint8_t> rom(2048, 0), ram(32768, 0);
	const std::vector<uint8_t> program = { 0x23, 0x22, 0x00, 0xC1, 0x18, 0xFA };

	std::copy(program.begin(), program.end(), rom.begin());
	odin::MieBoard mie(rom, ram, odin::Mie::CLOCK_HZ, nullptr);
	for (int i = 0; i < 10000; i++)
		mie.run(17);
	const unsigned turns = ram[0x4100] | ram[0x4101] << 8;
	check(turns >= 4999 && turns <= 5001, "runs of 17 clocks: the MIE at 16 MHz all the same");
}

/* A line on UART 1 instead of a JvsHost: each byte sent told with the
 * time its start bit begins, a byte given back read from UART 1, the sense
 * input set by the board. The program: the divisor at 8, 55 AA 0F sent,
 * the sense into C000, a byte waited for and read into C001, the sense
 * again into C002. */
struct Line : odin::MieLine {
	std::vector<std::pair<uint8_t, uint64_t>> sent;
	void line_sent(uint8_t byte, uint64_t ticks) override { sent.emplace_back(byte, ticks); }
};

struct Pins : odin::MieHost {
	uint8_t gpio_read(uint8_t) override { return 0xFF; }
	void gpio_write(uint8_t, uint8_t) override {}
};

void test_line()
{
	std::vector<uint8_t> rom(2048, 0), ram(32768, 0);
	const std::vector<uint8_t> program = {
		0x3E, 0x80, 0xD3, 0x13, 0x3E, 0x08, 0xD3, 0x10, 0xAF, 0xD3, 0x13,	/* the divisor at 8 */
		0x3E, 0x55, 0xD3, 0x10, 0x3E, 0xAA, 0xD3, 0x10, 0x3E, 0x0F, 0xD3, 0x10,
		0xDB, 0x91, 0x32, 0x00, 0xC0,						/* in a, (91) / ld (C000), a */
		0xDB, 0x15, 0xCB, 0x47, 0x28, 0xFA,					/* in a, (15) / bit 0, a / jr z */
		0xDB, 0x10, 0x32, 0x01, 0xC0,						/* in a, (10) / ld (C001), a */
		0xDB, 0x91, 0x32, 0x02, 0xC0, 0x76,					/* in a, (91) / ld (C002), a / halt */
	};
	std::copy(program.begin(), program.end(), rom.begin());
	Pins pins;
	Line line;
	odin::Mie mie(pins, rom, ram);
	mie.attach_line(&line);

	mie.run(5000);
	/* 10 bits of 16 clocks of the UART, 625 ticks, divisor 8 */
	constexpr uint64_t BYTE = 10 * 16 * 625 * 8;
	check(line.sent.size() == 3 && line.sent[0].first == 0x55 && line.sent[1].first == 0xAA &&
	      line.sent[2].first == 0x0F, "a line: each byte sent told");
	check(line.sent.size() == 3 && line.sent[0].second < 200 * odin::Mie::TICKS_PER_CYCLE &&
	      line.sent[1].second == line.sent[0].second + BYTE && line.sent[2].second == line.sent[1].second + BYTE,
	      "with the time its start bit begins, a character apart");
	check(ram[0x4000] == 0x8C, "a line: a board is wired, not addressed yet");
	mie.set_line_addressed(true);
	mie.line_received(0x5A);
	mie.run(2000);
	check(ram[0x4001] == 0x5A, "a byte from the line, read from UART 1");
	check(ram[0x4002] == 0x8E, "the sense input as the board sets it");
}

/* Bytes from a line between two calls, waited for on UART 1 and kept
 * (ld hl, C000 / in a, (15) / bit 0, a / jr z / in a, (10) / ld (hl), a
 * / inc hl / jr): each seen as by a MIE that runs every turn. */
void test_wait_line()
{
	const std::vector<uint8_t> program = { 0x21, 0x00, 0xC0, 0xDB, 0x15, 0xCB, 0x47, 0x28, 0xFA, 0xDB, 0x10, 0x77,
	                                       0x23, 0x18, 0xF4 };
	std::vector<uint8_t> rom(2048, 0), ram_a(32768, 0), ram_b(32768, 0);
	std::copy(program.begin(), program.end(), rom.begin());
	Pins pins;
	Line line_a, line_b;
	odin::Mie counted(pins, rom, ram_a), run(pins, rom, ram_b);
	counted.attach_line(&line_a);
	run.attach_line(&line_b);
	run.set_waits_counted(false);
	bool same = true;
	for (unsigned call = 0; call < 1000; call++) {
		if (call % 35 == 15) {
			counted.line_received(static_cast<uint8_t>(0x40 + call / 35));
			run.line_received(static_cast<uint8_t>(0x40 + call / 35));
		}
		counted.run(60);
		run.run(60);
		odin::Z80State a, b;
		counted.cpu().save(a);
		run.cpu().save(b);
		same = same && a == b && ram_a == ram_b;
	}
	check(same && ram_a[0x4000] == 0x40 && ram_a[0x4001] == 0x41 && counted.waits_counted() > 1000 * 60 * 3 / 4,
	      "bytes from the line between calls: seen at once, the waits counted from one call to the next");
}

/* The divisor at 18: a character takes 3 125 clocks, from the start of
 * the OUT that gives it. The first of two bytes ends at clock 3 183, just
 * where the wait for the FIFO to empty (from clock 176, 31 clocks a turn)
 * starts a turn and where the first call ends; the second byte leaves the
 * FIFO at the tick after. */
void test_wait_fifo()
{
	std::vector<uint8_t> program = { 0x3E, 0x80, 0xD3, 0x13, 0x3E, 0x12, 0xD3, 0x10, 0xAF, 0xD3, 0x13,
	                                 0x3E, 0x55, 0xD3, 0x10, 0xD3, 0x10 };
	program.resize(program.size() + 24, 0);
	for (const uint8_t b : { uint8_t(0xDB), uint8_t(0x15), uint8_t(0xCB), uint8_t(0x6F), uint8_t(0x28), uint8_t(0xFA),
	                         uint8_t(0x76) })
		program.push_back(b);
	std::vector<uint8_t> rom(2048, 0), ram_a(32768, 0), ram_b(32768, 0);
	std::copy(program.begin(), program.end(), rom.begin());
	Pins pins;
	odin::Mie counted(pins, rom, ram_a), run(pins, rom, ram_b);
	run.set_waits_counted(false);
	const bool first = counted.run(3183) == 3183 && run.run(3183) == 3183;
	counted.run(500);
	run.run(500);
	odin::Z80State a, b;
	counted.cpu().save(a);
	run.cpu().save(b);
	check(first && a == b && b.halted, "a call that starts where a byte ends and the next leaves the FIFO");
}

/* The same divisor, one byte: it ends at clock 3 183, inside the JR of a
 * turn of the wait for its end (from clock 177); the IN after starts a
 * clock later and sees it. */
void test_wait_temt()
{
	std::vector<uint8_t> program = { 0x3E, 0x80, 0xD3, 0x13, 0x3E, 0x12, 0xD3, 0x10, 0xAF, 0xD3, 0x13,
	                                 0x3E, 0x55, 0xD3, 0x10 };
	program.resize(program.size() + 27, 0);
	for (const uint8_t b : { uint8_t(0xDB), uint8_t(0x15), uint8_t(0xCB), uint8_t(0x77), uint8_t(0x28), uint8_t(0xFA),
	                         uint8_t(0x76) })
		program.push_back(b);
	std::vector<uint8_t> rom(2048, 0), ram_a(32768, 0), ram_b(32768, 0);
	std::copy(program.begin(), program.end(), rom.begin());
	Pins pins;
	odin::Mie counted(pins, rom, ram_a), run(pins, rom, ram_b);
	run.set_waits_counted(false);
	bool same = true;
	for (int call = 0; call < 10; call++) {
		counted.run(500);
		run.run(500);
		odin::Z80State a, b;
		counted.cpu().save(a);
		run.cpu().save(b);
		same = same && a == b;
	}
	check(same && counted.cpu().halted() && counted.waits_counted() > 2000,
	      "the end of a byte just before a read of it: seen there");
}

/* Two MIE, one that runs every turn, `calls` calls of `clocks`, bytes from
 * a line every `every` calls (0: none): the same after each call. The
 * clocks counted. */
uint64_t counted_calls(const char *name, const std::vector<uint8_t> &program, unsigned calls, uint32_t clocks,
                       unsigned every)
{
	std::vector<uint8_t> rom(2048, 0), ram_a(32768, 0), ram_b(32768, 0);
	std::copy(program.begin(), program.end(), rom.begin());
	Pins pins;
	Line line_a, line_b;
	odin::Mie counted(pins, rom, ram_a), run(pins, rom, ram_b);
	counted.attach_line(&line_a);
	run.attach_line(&line_b);
	run.set_waits_counted(false);
	bool same = true;
	for (unsigned call = 0; call < calls; call++) {
		if (every && call % every == every - 1) {
			counted.line_received(static_cast<uint8_t>(call / every));
			run.line_received(static_cast<uint8_t>(call / every));
		}
		counted.run(clocks);
		run.run(clocks);
		odin::Z80State a, b;
		counted.cpu().save(a);
		run.cpu().save(b);
		same = same && a == b && ram_a == ram_b;
	}
	check(same, name);
	return counted.waits_counted();
}

/* Loops that count down while they wait (MARKDOWN/78 § 8) */
void test_wait_count()
{
	/* 887F of the program of the MIE: ld e, 0 / dec e / jp z, timeout /
	 * in a, (15) / bit 0, a / jr z; each byte kept from C000; the
	 * timeout puts FF in C100 */
	std::vector<uint8_t> wait = { 0x21, 0x00, 0xC0, 0x1E, 0x00, 0x1D, 0xCA, 0x20, 0x00, 0xDB, 0x15, 0xCB, 0x47,
	                              0x28, 0xF6, 0xDB, 0x10, 0x77, 0x23, 0x18, 0xEE };
	wait.resize(0x20, 0);
	for (const uint8_t b : { uint8_t(0x3E), uint8_t(0xFF), uint8_t(0x32), uint8_t(0x00), uint8_t(0xC1), uint8_t(0x76) })
		wait.push_back(b);
	uint64_t clocks = counted_calls("a wait with a timeout in E, bytes from the line", wait, 700, 505, 7);
	check(clocks > 700 * 505 / 2, "its turns counted while E does not reach 0");
	/* The same wait for the bytes of the reply of the imitation of the
	 * 837-13551 to F1 01: each lands inside a call */
	waits("a wait with a timeout in E, bytes from the imitation", {
		0x3E, 0x80, 0xD3, 0x13, 0x3E, 0x08, 0xD3, 0x10, 0xAF, 0xD3, 0x13, 0x3E, 0x01, 0xD3, 0x90,
		0x3E, 0xE0, 0xD3, 0x10, 0x3E, 0xFF, 0xD3, 0x10, 0x3E, 0x03, 0xD3, 0x10, 0x3E, 0xF1, 0xD3, 0x10,
		0x3E, 0x01, 0xD3, 0x10, 0x3E, 0xF4, 0xD3, 0x10, 0xDB, 0x15, 0xCB, 0x77, 0x28, 0xFA, 0xAF, 0xD3, 0x90,
		0x21, 0x00, 0xC1, 0x06, 0x06, 0x1E, 0x00, 0x1D, 0xCA, 0x48, 0x00, 0xDB, 0x15, 0xCB, 0x47, 0x28, 0xF6,
		0xDB, 0x10, 0x77, 0x23, 0x10, 0xEE, 0x76, 0x3E, 0xFF, 0x32, 0x00, 0xC2, 0x76,
	}, 99);
	/* No byte: E falls to 0 after 255 turns, the timeout taken there */
	counted_calls("and when no byte comes, the timeout", wait, 300, 300, 0);

	/* djnz on the same wait */
	clocks = counted_calls("a wait with a timeout in B (djnz)",
	                       { 0x06, 0x00, 0xDB, 0x15, 0xCB, 0x47, 0x20, 0x02, 0x10, 0xF8, 0x3E, 0x01, 0x32, 0x00,
	                         0xC1, 0x76 }, 300, 300, 0);
	check(clocks > 2000, "its turns counted too");

	/* Not counted, so run; each runs short of the turn that leaves it,
	 * with nothing else to count: E read again (ld a, e / cp 40 / jr z:
	 * out at E 40), a flag of the DEC read later (jp p / xor a: out at E
	 * 7F), a
	 * jump on its sign right after it, its flags left as they are to the
	 * end of the turn (dec e / jr nz) */
	check(counted_calls("a loop that reads its count", { 0x1E, 0xFF, 0x1D, 0xCA, 0x0F, 0x00, 0x7B, 0xFE, 0x40, 0x28,
	                                                   0x04, 0xAF, 0xC3, 0x02, 0x00, 0x76 }, 10, 505, 0) == 0,
	      "not counted");
	check(counted_calls("a loop that reads a flag of its DEC", { 0x1E, 0xFF, 0x1D, 0xCA, 0x0C, 0x00, 0xF2, 0x0C, 0x00,
	                                                           0xAF, 0x18, 0xF6, 0x76 }, 8, 505, 0) == 0,
	      "not counted");
	check(counted_calls("a loop that jumps on the sign of its DEC", { 0x1E, 0xFF, 0x1D, 0xF2, 0x09, 0x00, 0xAF, 0x18,
	                                                                0xF9, 0x76 }, 6, 505, 0) == 0,
	      "not counted");
	check(counted_calls("a loop that keeps the flags of its DEC", { 0x1E, 0x00, 0x00, 0x1D, 0x20, 0xFC, 0x76 }, 8, 505,
	                    0) == 0,
	      "not counted");
}

int main()
{
	test_eeprom();
	test_interrupts();
	test_port_d();
	test_port_readback();
	test_bus_two_hosts();
	test_waits();
	test_wait_loaded();
	test_wait_bus();
	test_uart_speed();
	test_reply_time();
	test_line();
	test_wait_line();
	test_wait_fifo();
	test_wait_temt();
	test_wait_count();
	test_short_runs();
	std::printf("%u checks passed, %u failed\n", g_passed, g_failed);
	return g_failed ? 1 : 0;
}
