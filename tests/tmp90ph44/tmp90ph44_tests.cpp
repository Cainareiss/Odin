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

/* Unit tests for the TMP90PH44: the devices the program 315-6215 uses,
 * driven by small programs in its PROM.
 *
 * Build: g++ -std=c++20 -O2 -Wall -Wextra -Wpedantic -Wconversion -Wshadow \
 *            -I src tests/tmp90ph44/tmp90ph44_tests.cpp src/odin/tmp90ph44.cpp src/odin/tlcs90.cpp \
 *            -o tmp90ph44_tests
 * Exit status is 0 when every check passes. */

#include "odin/tmp90ph44.hpp"

#include <cstdio>
#include <initializer_list>
#include <vector>

namespace {

using odin::Tmp90ph44;

int g_checks, g_failures;

void check(bool ok, const char *what)
{
	g_checks++;
	if (!ok) {
		g_failures++;
		std::printf("FAILED: %s\n", what);
	}
}

struct Board : odin::Tmp90ph44Board {
	unsigned bus = 0;
	struct Out {
		unsigned told = 0;
		uint8_t latch = 0, driven = 0;
	};
	uint8_t pins[9] = { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF };
	Out out[8];
	uint8_t analog[4] = { 0x11, 0x22, 0x33, 0x44 };
	std::vector<uint8_t> sent;
	/* When set, the time of each byte sent */
	const Tmp90ph44 *chip = nullptr;
	std::vector<uint64_t> sent_at;
	uint8_t bus_read(uint16_t) override
	{
		bus++;
		return 0xFF;
	}
	void bus_write(uint16_t, uint8_t) override { bus++; }
	uint8_t port_pins(unsigned port) override { return pins[port]; }
	void port_out(unsigned port, uint8_t latch, uint8_t driven) override { out[port] = { out[port].told + 1, latch, driven }; }
	uint8_t analog_pin(unsigned n) override { return analog[n]; }
	void serial_sent(uint8_t byte) override
	{
		sent.push_back(byte);
		if (chip)
			sent_at.push_back(chip->states());
	}
};

/* A PROM: the program at 0, the handlers at their vectors */
struct Rom {
	std::vector<uint8_t> bytes = std::vector<uint8_t>(Tmp90ph44::ROM_SIZE);
	void put(uint16_t at, std::initializer_list<uint8_t> code)
	{
		for (uint8_t b : code)
			bytes[at++] = b;
	}
};

/* LD SP,FFBF; ...; JR T,$ (C8 FE) */
constexpr uint8_t LOOP[] = { 0xC8, 0xFE };

void test_watchdog()
{
	Rom rom;
	Board board;

	/* WDMOD A1: WDTP 01, 2^16 periods of fc = 32768 states; EI. The
	 * handler counts in FF00. */
	rom.put(0, { 0x3E, 0xBF, 0xFF, 0x37, 0xD2, 0xA1, 0x03, LOOP[0], LOOP[1] });
	rom.put(0x20, { 0x87, 0x00, 0x1F });
	Tmp90ph44 chip(rom.bytes, board);

	chip.run(32768 - 100);
	check(chip.peek(0xFF00) == 0, "no watchdog interrupt before 2^16 / fc");
	chip.run(200);
	check(chip.peek(0xFF00) == 1, "one at 2^16 / fc");
	chip.run(2 * 32768);
	check(chip.peek(0xFF00) == 3, "then one each 2^16 / fc");

	/* The same, EI once P0 bit 0 reads 0 */
	rom.put(6, { 0x27, 0xC0, 0x6C, 0x01, 0xCE, 0xFA, 0x03, LOOP[0], LOOP[1] });
	Tmp90ph44 shut(rom.bytes, board);

	shut.run(32768 + 200);
	check(shut.peek(0xFF00) == 0, "the interrupts shut: INTWD waits");
	board.pins[0] = 0xFE;
	shut.run(100);
	check(shut.peek(0xFF00) == 1, "and is taken once they open");
}

void test_timer()
{
	Rom rom;
	Board board;

	/* T01MOD 0B (timer 1 on phiT16), TREG1 14, INTEH 04, EI, TRUN 22.
	 * The handler stops the timer, counts in FF01, keeps FFEB in FF02. */
	rom.put(0, { 0x3E, 0xBF, 0xFF, 0x37, 0xD8, 0x0B, 0x37, 0xD5, 0x14, 0x37, 0xF5, 0x04, 0x03, 0x37, 0xDC, 0x22,
	             LOOP[0], LOOP[1] });
	rom.put(0x28, { 0x37, 0xDC, 0x20, 0x87, 0x01, 0x27, 0xEB, 0x2F, 0x02, 0x1F });
	Tmp90ph44 chip(rom.bytes, board);

	chip.run(20 * 64 - 64);
	check(chip.peek(0xFF01) == 0, "timer 1: nothing before 20 ticks of fc / 128");
	chip.run(400);
	check(chip.peek(0xFF01) == 1, "the interrupt of 0028 at the match");
	check(!(chip.peek(0xFF02) & 0x04), "FFEB bit 2 cleared when it is taken");
	chip.run(400);
	check(chip.peek(0xFF01) == 1, "stopped: once");

	/* The same in one run: the match is an event within it */
	Tmp90ph44 once(rom.bytes, board);

	once.run(20 * 64 + 400);
	check(once.peek(0xFF01) == 1, "one run: the interrupt of 0028 taken within it");

	/* The same with INTEH 00: no interrupt */
	rom.put(0x0A, { 0x00 });
	Tmp90ph44 off(rom.bytes, board);

	off.run(3000);
	check(off.peek(0xFF01) == 0, "INTEH bit 2 clear: 0028 not taken");

	/* No EI: FFEB polled until bit 2 sets, kept in FF03; FFEA 05, FFEB
	 * kept in FF04; then EI. The handler counts in FF01. */
	Rom poll;
	poll.put(0, { 0x3E, 0xBF, 0xFF, 0x37, 0xD8, 0x0B, 0x37, 0xD5, 0x14, 0x37, 0xF5, 0x04, 0x37, 0xDC, 0x22,
	              0x27, 0xEB, 0x6C, 0x04, 0xC6, 0xFA, 0x27, 0xEB, 0x2F, 0x03, 0x37, 0xEA, 0x05, 0x27, 0xEB, 0x2F, 0x04,
	              0x03, LOOP[0], LOOP[1] });
	poll.put(0x28, { 0x87, 0x01, 0x1F });
	Tmp90ph44 polled(poll.bytes, board);

	polled.run(1700);
	check(polled.peek(0xFF03) & 0x04, "the interrupts shut: FFEB bit 2 set at the match");
	check(!(polled.peek(0xFF04) & 0x04), "FFEA 05 clears it");
	check(polled.peek(0xFF01) == 0, "and the request: not taken once they open");
	polled.run(1300);
	check(polled.peek(0xFF01) == 1, "the next match taken");
}

void test_serial()
{
	Rom rom;
	Board board;

	/* SCMOD 29, BRGCR 02, INTEL 0C, INTEH 48, EI, SCBUF 55. TX handler:
	 * INC (FF04). RX handler: SCBUF into FF05. */
	rom.put(0, { 0x3E, 0xBF, 0xFF, 0x37, 0xE6, 0x29, 0x37, 0xE9, 0x02, 0x37, 0xF4, 0x0C, 0x37, 0xF5, 0x48, 0x03,
	             0x37, 0xE8, 0x55, LOOP[0], LOOP[1] });
	rom.put(0x60, { 0x27, 0xE8, 0x2F, 0x05, 0x1F });
	rom.put(0x68, { 0x87, 0x04, 0x1F });
	Tmp90ph44 chip(rom.bytes, board);

	chip.run(30 + Tmp90ph44::BYTE_STATES - 40);
	check(board.sent.empty(), "a byte takes 640 states, 10 bits at fc / 128");
	chip.run(100);
	check(board.sent.size() == 1 && board.sent[0] == 0x55, "then it is on the line");
	chip.run(100);
	check(chip.peek(0xFF04) == 1, "and the interrupt of 0068 follows");
	chip.serial_received(0xA7);
	chip.run(100);
	check(chip.peek(0xFF05) == 0xA7, "a byte received: 0060, SCBUF");

	/* The same in one run: the end of the byte is an event within it */
	Board other;
	Tmp90ph44 once(rom.bytes, other);

	once.run(30 + Tmp90ph44::BYTE_STATES + 200);
	check(other.sent.size() == 1 && once.peek(0xFF04) == 1, "one run: the byte sent, 0068 taken within it");
}

void test_overrun()
{
	Rom rom;
	Board board;

	/* SCMOD 29, INTEL 0C, INTEH 48, EI; once P0 bit 0 reads 0, SCCR into
	 * FF07 then FF08, SCBUF into FF09. The RX handler counts in FF04 and
	 * does not read SCBUF. */
	rom.put(0, { 0x3E, 0xBF, 0xFF, 0x37, 0xE6, 0x29, 0x37, 0xF4, 0x0C, 0x37, 0xF5, 0x48, 0x03 });
	rom.put(0x10, { 0x27, 0xC0, 0x6C, 0x01, 0xCE, 0xFA, 0x27, 0xE7, 0x2F, 0x07, 0x27, 0xE7, 0x2F, 0x08, 0x27, 0xE8,
	                0x2F, 0x09, LOOP[0], LOOP[1] });
	rom.put(0x60, { 0x87, 0x04, 0x1F });
	Tmp90ph44 chip(rom.bytes, board);

	chip.run(100);
	chip.serial_received(0xA7);
	chip.run(100);
	chip.serial_received(0xB5);
	chip.run(100);
	check(chip.peek(0xFF04) == 1, "a byte while SCBUF is not read: no INTRX");
	board.pins[0] = 0xFE;
	chip.run(200);
	check(chip.peek(0xFF07) & 0x10, "OERR set");
	check(!(chip.peek(0xFF08) & 0x10), "OERR cleared once read");
	check(chip.peek(0xFF09) == 0xA7, "SCBUF keeps the first byte");
	chip.serial_received(0xC3);
	chip.run(100);
	check(chip.peek(0xFF04) == 2, "SCBUF read: the next byte comes in");
}

/* The states of a run of `program` that billed, and the chip left */
uint64_t billed(std::initializer_list<uint8_t> program, uint32_t states)
{
	Rom rom;
	Board board;

	rom.put(0, program);
	Tmp90ph44 chip(rom.bytes, board);
	chip.run(states);
	return chip.skipped();
}

void test_idle()
{
	Rom rom;
	Board board;

	/* WDMOD A1, EI; waits for FF00 to change (LD A,(FF00); CP A,0; JR Z),
	 * then JR T,$. The watchdog handler counts in FF00. */
	rom.put(0, { 0x3E, 0xBF, 0xFF, 0x37, 0xD2, 0xA1, 0x03, 0x27, 0x00, 0x6F, 0x00, 0xC6, 0xFA, LOOP[0], LOOP[1] });
	rom.put(0x20, { 0x87, 0x00, 0x1F });
	constexpr uint32_t TOTAL = 3 * 32768 + 1000;
	Tmp90ph44 once(rom.bytes, board), stepped(rom.bytes, board);

	once.run(TOTAL);
	while (stepped.states() < TOTAL)
		stepped.run(1);
	odin::Tlcs90State a, b;
	once.cpu().save(a);
	stepped.cpu().save(b);
	check(once.skipped() > 2 * 32768, "the idle loops billed without being run");
	check(stepped.skipped() == 0, "none billed one instruction at a time");
	check(once.states() == stepped.states() && a == b && once.peek(0xFF00) == 3 && stepped.peek(0xFF00) == 3,
	      "and the same: each interrupt taken on the same instruction");

	/* Its waits switched off: every turn run in one call, the same end */
	Tmp90ph44 every(rom.bytes, board);
	odin::Tlcs90State c;

	every.set_waits_counted(false);
	every.run(TOTAL);
	every.cpu().save(c);
	check(every.skipped() == 0 && every.states() == once.states() && c == a && every.peek(0xFF00) == 3,
	      "its waits switched off: none billed, the same");

	/* Loops that are not idle */
	check(billed({ 0x3E, 0xBF, 0xFF, 0x87, 0x01, 0xC8, 0xFC }, 20000) == 0, "INC (FF01) in a loop: not billed");
	check(billed({ 0x80, 0xC8, 0xFD }, 20000) == 0, "INC B in a loop: not billed");

	/* Waits for P0 bit 0 to read 0 (LD A,(FFC0); AND A,1; 20 NOP; JR NZ),
	 * then INC (FF02) */
	Rom poll;
	Board pins;

	poll.put(0, { 0x3E, 0xBF, 0xFF, 0x27, 0xC0, 0x6C, 0x01 });
	poll.put(0x1B, { 0xCE, 0xE6, 0x87, 0x02, LOOP[0], LOOP[1] });
	Tmp90ph44 waiting(poll.bytes, pins);
	waiting.run(20000);
	check(waiting.skipped() > 15000 && waiting.peek(0xFF02) == 0, "a loop on a pin: billed");
	/* The run ends inside a turn, the pin read */
	while (waiting.cpu().pc() != 0x05)
		waiting.run(1);
	pins.pins[0] = 0x00;
	waiting.run(300);
	check(waiting.peek(0xFF02) == 1, "the pin changed between two runs: seen at once");

	/* WDMOD A1, EI, then WDCR 4E over and over: the watchdog never ends */
	Rom clear;

	clear.put(0, { 0x3E, 0xBF, 0xFF, 0x37, 0xD2, 0xA1, 0x03, 0x37, 0xD3, 0x4E, 0xC8, 0xFB });
	clear.put(0x20, { 0x87, 0x00, 0x1F });
	Tmp90ph44 clearing(clear.bytes, board);
	clearing.run(100000);
	check(clearing.peek(0xFF00) == 0, "a loop writing a register: run, not billed");

	/* P6 0A over and over: the board told of each write */
	Board told;
	Rom port;

	port.put(0, { 0x37, 0xCB, 0x0A, 0xC8, 0xFB });
	Tmp90ph44 writing(port.bytes, told);
	writing.run(100000);
	check(writing.skipped() == 0 && told.out[6].told > 3000, "a loop writing a port: each write told");

	/* LD A,(4000), or LD (4000),A, over and over: each cycle on the bus */
	for (uint8_t op : { uint8_t(0xE3), uint8_t(0xEB) }) {
		Board external;
		Rom bus;

		bus.put(0, { op, 0x00, 0x40, uint8_t(op == 0xE3 ? 0x2E : 0x26), 0xC8, 0xFA });
		Tmp90ph44 cycling(bus.bytes, external);
		cycling.run(100000);
		check(cycling.skipped() == 0 && external.bus > 5000,
		      op == 0xE3 ? "a loop reading the external bus: each cycle made" : "a loop writing it: each cycle made");
	}

	/* Watchdog off, RX on; NOP, NOP, EI, NOP, DI, JR: a byte received
	 * between two runs, the second run starting on the JR, is taken inside
	 * a turn, where EI opens the interrupts. Its handler does nothing, and
	 * what its entry pushes (PC 001C, A 00, F 20) is on the stack already
	 * (FFBB 20, FFBD 1C): only the interrupt tells the turn is not idle. */
	Rom open;

	open.put(0, { 0x3E, 0xBF, 0xFF, 0x37, 0xD2, 0x00, 0x37, 0xD3, 0xB1, 0x37, 0xE6, 0x29, 0x37, 0xF4, 0x0C,
	              0x37, 0xF5, 0x48, 0x37, 0xBB, 0x20, 0x37, 0xBD, 0x1C, 0x00, 0x00, 0x03, 0x00, 0x02, 0xC8, 0xF9 });
	open.put(0x60, { 0x1F });
	Tmp90ph44 turns(open.bytes, board), steps(open.bytes, board);
	turns.run(5000);
	while (turns.cpu().pc() != 0x1D)
		turns.run(1);
	while (steps.states() < turns.states())
		steps.run(1);
	turns.serial_received(0x55);
	steps.serial_received(0x55);
	turns.run(1300);
	while (steps.states() < turns.states())
		steps.run(1);
	turns.cpu().save(a);
	steps.cpu().save(b);
	check(turns.states() == steps.states() && a == b, "an interrupt inside a turn: the same as one instruction at a time");
}

/* The wait of 315-6215 (0149): CALL a routine that tests bit 1 of FE70
 * and gives L, compare L, again while 0; then INC (FF02). The watchdog
 * off; with `timer`, timer 1 counts in FF01 every 1280 states. From 0100. */
Rom waiting(bool timer)
{
	Rom rom;

	rom.put(0, { 0x1A, 0x00, 0x01 });
	rom.put(0x28, { 0x87, 0x01, 0x1F });
	rom.put(0x100, { 0x3E, 0xBF, 0xFF, 0x37, 0xD2, 0x00, 0x37, 0xD3, 0xB1 });
	if (timer)
		rom.put(0x109, { 0x37, 0xD8, 0x0B, 0x37, 0xD5, 0x14, 0x37, 0xF5, 0x04, 0x37, 0xDC, 0x22, 0x03 });
	rom.put(0x116, { 0x1C, 0x30, 0x01, 0xFD, 0x6F, 0x00, 0xC6, 0xF8, 0x87, 0x02, LOOP[0], LOOP[1] });
	rom.put(0x130, { 0xE3, 0x70, 0xFE, 0xA9, 0xEB, 0x3B, 0x01, 0xCE, 0x35, 0x00, 0x1E, 0x35, 0x01, 0x1E });
	return rom;
}

/* A turn proven idle kept from one run() to the next */
void test_kept()
{
	const Rom rom = waiting(true);
	Board board, other;
	Tmp90ph44 kept(rom.bytes, board), stepped(rom.bytes, other);
	bool same = true;

	/* Runs of 1 to 151 states, most ending inside a turn */
	for (unsigned n = 0; n < 3000; n++) {
		kept.run(1 + n * 37 % 151);
		while (stepped.states() < kept.states())
			stepped.run(1);
		odin::Tlcs90State a, b;
		kept.cpu().save(a);
		stepped.cpu().save(b);
		same = same && kept.states() == stepped.states() && a == b && kept.peek(0xFF01) == stepped.peek(0xFF01);
	}
	check(same, "runs ending inside a turn: the same as one instruction at a time");
	check(kept.peek(0xFF01) > 100 && 2 * kept.skipped() > kept.states(), "the turn kept from one run to the next: most billed");

	/* Without events, what changes between two runs */
	const Rom quiet = waiting(false);
	Board one, two;
	Tmp90ph44 moved(quiet.bytes, one), loaded(quiet.bytes, two);

	moved.run(5000);
	odin::Tlcs90State cpu;
	moved.cpu().save(cpu);
	cpu.pc = 0x011E;
	moved.cpu().load(cpu);
	moved.run(40);
	check(moved.peek(0xFF02) == 1, "the CPU changed between two runs: it goes on from there");

	loaded.run(5000);
	odin::Tmp90ph44State state;
	loaded.save(state);
	state.ram[0xFE70 - 0xFDC0] |= 0x02;
	loaded.load(state);
	loaded.run(200);
	check(loaded.peek(0xFF02) == 1, "loaded with the memory changed: the turn sees it");

	/* LD A,(P5), CP A,40, JR Z, else the watchdog off, P5 40 and back:
	 * the reset pin clears P5 under a turn that waits on it */
	Rom latch;
	Board pin;

	latch.put(0, { 0x27, 0xCA, 0x6F, 0x40, 0xC6, 0xFA, 0x1A, 0x00, 0x01 });
	latch.put(0x100, { 0x37, 0xD2, 0x00, 0x37, 0xD3, 0xB1, 0x37, 0xCA, 0x40, 0x1A, 0x00, 0x00 });
	Tmp90ph44 reset(latch.bytes, pin);
	reset.run(40000);
	while (reset.cpu().pc() != 0)
		reset.run(1);
	reset.reset();
	reset.run(200);
	check(pin.out[5].told == 2, "the reset pin: a turn proven before is run again");

	/* The watchdog off, SCMOD 29, then waits for OERR in SCCR; SCBUF not
	 * read: the second byte is lost and sets it */
	Rom error;
	Board line;

	error.put(0, { 0x37, 0xD2, 0x00, 0x37, 0xD3, 0xB1, 0x37, 0xE6, 0x29, 0x27, 0xE7, 0x6C, 0x10, 0xC6, 0xFA, 0x87, 0x02,
	               LOOP[0], LOOP[1] });
	Tmp90ph44 lost(error.bytes, line);
	lost.run(500);
	lost.serial_received(0xA7);
	lost.run(3000);
	lost.serial_received(0xB5);
	lost.run(100);
	check(lost.peek(0xFF02) == 1, "a byte lost between two runs: OERR seen at once");

	/* The watchdog off; waits for P0 bit 0 to read 0, then the wait of
	 * 0149 at 0100: the pin read by the first turn, not by the second */
	Rom unread;
	Board moving;

	unread.put(0, { 0x3E, 0xBF, 0xFF, 0x37, 0xD2, 0x00, 0x37, 0xD3, 0xB1, 0x27, 0xC0, 0x6C, 0x01, 0xCE, 0xFA, 0x1A, 0x00,
	                0x01 });
	unread.put(0x100, { 0x1C, 0x30, 0x01, 0xFD, 0x6F, 0x00, 0xC6, 0xF8, LOOP[0], LOOP[1] });
	unread.put(0x130, { 0xE3, 0x70, 0xFE, 0xA9, 0xEB, 0x3B, 0x01, 0xCE, 0x35, 0x00, 0x1E, 0x35, 0x01, 0x1E });
	Tmp90ph44 still(unread.bytes, moving);
	still.run(5000);
	moving.pins[0] = 0x00;
	still.run(100);
	check(still.cpu().pc() >= 0x100, "the pin read by the turn changed: seen at once");
	still.run(5000);
	const uint64_t skipped = still.skipped(), states = still.states();
	for (unsigned n = 0; n < 2000; n++) {
		moving.pins[0] = n & 1 ? 0xFC : 0xFE;
		still.run(50);
	}
	check(2 * (still.skipped() - skipped) > still.states() - states, "a pin the turn does not read changed: still billed");
}

void test_state()
{
	Rom rom;

	/* The watchdog, timer 1 over and over, a byte sent after each byte
	 * sent, bytes received: WDMOD A1, T01MOD 0B, TREG1 14, INTEH 4C, INTEL
	 * 0C, SCMOD 29, BRGCR 02, TRUN 22, EI, SCBUF 55. The handlers count in
	 * FF00, FF01 and FF04; RX keeps SCBUF in FF05 and SCCR in FF06. From
	 * 0100, past the vectors. */
	rom.put(0, { 0x1A, 0x00, 0x01 });
	rom.put(0x100, { 0x3E, 0xBF, 0xFF, 0x37, 0xD2, 0xA1, 0x37, 0xD8, 0x0B, 0x37, 0xD5, 0x14, 0x37, 0xF5, 0x4C,
	                 0x37, 0xF4, 0x0C, 0x37, 0xE6, 0x29, 0x37, 0xE9, 0x02, 0x37, 0xDC, 0x22, 0x03, 0x37, 0xE8, 0x55,
	                 LOOP[0], LOOP[1] });
	rom.put(0x20, { 0x87, 0x00, 0x1F });
	rom.put(0x28, { 0x87, 0x01, 0x1F });
	rom.put(0x60, { 0x1A, 0x40, 0x01 });
	rom.put(0x140, { 0x27, 0xE8, 0x2F, 0x05, 0x27, 0xE7, 0x2F, 0x06, 0x1F });
	rom.put(0x68, { 0x87, 0x04, 0x37, 0xE8, 0x5A, 0x1F });

	/* Saved with a byte not read yet, one lost over it, a byte being
	 * sent, the timers between two ticks; then, the second time, one more
	 * byte received before anything runs, lost too */
	for (bool more : { false, true }) {
		Board one, other;
		Tmp90ph44 kept(rom.bytes, one), loaded(rom.bytes, other);
		one.chip = &kept;
		other.chip = &loaded;

		kept.run(50333);
		kept.serial_received(0x3C);
		kept.serial_received(0x3D);
		odin::Tmp90ph44State state;
		kept.save(state);
		const size_t before = one.sent.size();
		loaded.load(state);
		if (more) {
			kept.serial_received(0x3E);
			loaded.serial_received(0x3E);
		}

		bool same = true;
		for (int chunk = 0; chunk < 60; chunk++) {
			kept.run(997);
			loaded.run(997);
			odin::Tlcs90State a, b;
			kept.cpu().save(a);
			loaded.cpu().save(b);
			same = same && kept.states() == loaded.states() && a == b;
			for (uint16_t at = 0xFDC0; at != 0; at++)
				same = same && kept.peek(at) == loaded.peek(at);
		}
		same = same &&
		       std::vector<uint8_t>(one.sent.begin() + static_cast<long>(before), one.sent.end()) == other.sent &&
		       std::vector<uint64_t>(one.sent_at.begin() + static_cast<long>(before), one.sent_at.end()) == other.sent_at;
		check(same, more ? "saved, loaded, a byte more: the same from there" : "saved, loaded in another chip: the same from there");
		check(kept.peek(0xFF00) > 1 && kept.peek(0xFF01) > 20 && kept.peek(0xFF04) > 50 && kept.peek(0xFF05) == 0x3C &&
		      (kept.peek(0xFF06) & 0x10), "the watchdog, timer 1, the line both ways all ran");
	}
}

void test_ports()
{
	Rom rom;
	Board board;

	/* P3CR 9C, P3 0C, then P3 and P0 read into FF06 and FF07; P6 0A, P7
	 * 05, then P67CR F3; P5 40 */
	rom.put(0, { 0x37, 0xC7, 0x9C, 0x37, 0xC6, 0x0C, 0x27, 0xC6, 0x2F, 0x06, 0x27, 0xC0, 0x2F, 0x07,
	             0x37, 0xCB, 0x0A, 0x37, 0xCC, 0x05, 0x37, 0xCD, 0xF3, 0x37, 0xCA, 0x40, LOOP[0], LOOP[1] });
	board.pins[3] = 0x03;
	board.pins[0] = 0x5A;
	Tmp90ph44 chip(rom.bytes, board);

	chip.run(200);
	check(chip.peek(0xFF06) == 0x0F, "P3: the latch on the bits P3CR drives, the pins on the others");
	check(chip.peek(0xFF07) == 0x5A, "P0, all inputs: the pins");
	check(board.out[3].told == 2 && board.out[3].latch == 0x0C && board.out[3].driven == 0x9C,
	      "the board told of P3 at each write to it or to P3CR");
	check(board.out[6].told == 2 && board.out[6].latch == 0x0A && board.out[6].driven == 0x03,
	      "P6: its latch, the low half of P67CR");
	check(board.out[7].told == 2 && board.out[7].latch == 0x05 && board.out[7].driven == 0x0F,
	      "P7: its latch, the high half of P67CR");
	check(board.out[5].told == 1 && board.out[5].latch == 0x40 && board.out[5].driven == 0xC0,
	      "P5: P56 and P57 drive");
	check(board.out[0].told == 0 && board.out[4].told == 0, "the ports not written: nothing told");

	/* P3CR FF at once: the latch of P3 drives the pins */
	Rom first;
	Board early;

	first.put(0, { 0x37, 0xC7, 0xFF, LOOP[0], LOOP[1] });
	Tmp90ph44 reset(first.bytes, early);
	reset.run(20);
	check(early.out[3].told == 1 && early.out[3].latch == 0xFF && early.out[3].driven == 0xFF,
	      "the latch of P3 at 1 from the reset");
}

void test_analog()
{
	Rom rom;
	Board board;

	/* ADMOD F7, then FFF0-FFF3 into FF08-FF0B */
	rom.put(0, { 0x37, 0xEF, 0xF7, 0x27, 0xF0, 0x2F, 0x08, 0x27, 0xF1, 0x2F, 0x09, 0x27, 0xF2, 0x2F, 0x0A,
	             0x27, 0xF3, 0x2F, 0x0B, LOOP[0], LOOP[1] });
	Tmp90ph44 chip(rom.bytes, board);

	chip.run(100);
	check(chip.peek(0xFF08) == 0x11 && chip.peek(0xFF09) == 0x22 && chip.peek(0xFF0A) == 0x33 &&
	      chip.peek(0xFF0B) == 0x44, "FFF0-FFF3: AN0 to AN3");
}

} /* namespace */

int main()
{
	test_watchdog();
	test_timer();
	test_serial();
	test_overrun();
	test_idle();
	test_kept();
	test_state();
	test_ports();
	test_analog();

	std::printf("%d checks, %d failures\n", g_checks, g_failures);
	return g_failures ? 1 : 0;
}
