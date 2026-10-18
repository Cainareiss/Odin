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
	struct Out {
		unsigned told = 0;
		uint8_t latch = 0, driven = 0;
	};
	uint8_t pins[9] = { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF };
	Out out[8];
	uint8_t analog[4] = { 0x11, 0x22, 0x33, 0x44 };
	std::vector<uint8_t> sent;
	uint8_t bus_read(uint16_t) override { return 0xFF; }
	void bus_write(uint16_t, uint8_t) override {}
	uint8_t port_pins(unsigned port) override { return pins[port]; }
	void port_out(unsigned port, uint8_t latch, uint8_t driven) override { out[port] = { out[port].told + 1, latch, driven }; }
	uint8_t analog_pin(unsigned n) override { return analog[n]; }
	void serial_sent(uint8_t byte) override { sent.push_back(byte); }
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

	/* WDMOD A1: WDTP 01, 2^16 periods of fc = 32768 states. The handler
	 * counts in FF00. */
	rom.put(0, { 0x3E, 0xBF, 0xFF, 0x37, 0xD2, 0xA1, LOOP[0], LOOP[1] });
	rom.put(0x20, { 0x87, 0x00, 0x1F });
	Tmp90ph44 chip(rom.bytes, board);

	chip.run(32768 - 100);
	check(chip.peek(0xFF00) == 0, "no watchdog interrupt before 2^16 / fc");
	chip.run(200);
	check(chip.peek(0xFF00) == 1, "one at 2^16 / fc, the interrupts shut");
	chip.run(2 * 32768);
	check(chip.peek(0xFF00) == 3, "then one each 2^16 / fc");
}

void test_timer()
{
	Rom rom;
	Board board;

	/* T01MOD 0B (timer 1 on phiT16), TREG1 14, INTEH 04, EI, TRUN 22.
	 * The handler stops the timer, counts in FF01, keeps FFEB in FF02.
	 * The main program then clears the flag (FFEA 05) and keeps FFEB in
	 * FF03. */
	rom.put(0, { 0x3E, 0xBF, 0xFF, 0x37, 0xD8, 0x0B, 0x37, 0xD5, 0x14, 0x37, 0xF5, 0x04, 0x03, 0x37, 0xDC, 0x22 });
	/* Waits for FF01 (LD A,(FF01); CP A,0; JR Z), clears the flag, keeps FFEB */
	rom.put(0x10, { 0x27, 0x01, 0x6F, 0x00, 0xC6, 0xFA, 0x37, 0xEA, 0x05, 0x27, 0xEB, 0x2F, 0x03, LOOP[0], LOOP[1] });
	rom.put(0x28, { 0x37, 0xDC, 0x20, 0x87, 0x01, 0x27, 0xEB, 0x2F, 0x02, 0x1F });
	Tmp90ph44 chip(rom.bytes, board);

	chip.run(20 * 64 - 64);
	check(chip.peek(0xFF01) == 0, "timer 1: nothing before 20 ticks of fc / 128");
	chip.run(400);
	check(chip.peek(0xFF01) == 1, "the interrupt of 0028 at the match");
	check(chip.peek(0xFF02) & 0x04, "FFEB bit 2 still set in the handler");
	chip.run(400);
	check(chip.peek(0xFF01) == 1, "stopped: once");
	check(!(chip.peek(0xFF03) & 0x04), "FFEA 05 clears it");

	/* The same with INTEH 00: no interrupt */
	rom.put(0x0A, { 0x00 });
	Tmp90ph44 shut(rom.bytes, board);

	shut.run(3000);
	check(shut.peek(0xFF01) == 0, "INTEH bit 2 clear: 0028 not taken");
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
	test_ports();
	test_analog();

	std::printf("%d checks, %d failures\n", g_checks, g_failures);
	return g_failures ? 1 : 0;
}
