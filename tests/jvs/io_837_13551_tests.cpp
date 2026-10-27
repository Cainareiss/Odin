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

/* Unit tests for the board around the TMP90PH44 of the 837-13551: the
 * pins of the switches, the coins, the analog channels, the outputs, the
 * line to the MIE and the sense input, its state. The chip runs a small
 * program in place of 315-6215 (tests/jvs/line_bench.cpp runs that one).
 *
 * Build: g++ -std=c++20 -O2 -Wall -Wextra -Wpedantic -Wconversion -Wshadow \
 *            -I src tests/jvs/io_837_13551_tests.cpp src/odin/machine/cabinet/io_837_13551.cpp src/odin/machine/chips/tmp90ph44.cpp \
 *            src/odin/machine/cpu/tlcs90/tlcs90.cpp src/odin/machine/chips/mie.cpp src/odin/machine/cpu/z80/z80.cpp src/odin/machine/cpu/z80/z80_ops.cpp src/odin/machine/cabinet/jvs.cpp \
 *            -o io_837_13551_tests
 * Exit status is 0 when every check passes. */

#include "odin/machine/cabinet/io_837_13551.hpp"

#include <algorithm>
#include <cstdio>
#include <initializer_list>
#include <vector>

namespace {

using odin::Io837_13551;

int g_checks, g_failures;

void check(bool ok, const char *what)
{
	g_checks++;
	if (!ok) {
		g_failures++;
		std::printf("FAILED: %s\n", what);
	}
}

struct Cabinet : odin::JvsInputs {
	uint8_t sys = 0;
	uint16_t players[2] = { 0, 0 };
	uint16_t channels[8] = {};
	uint8_t system() override { return sys; }
	uint16_t player(uint8_t n) override { return players[n & 1]; }
	uint16_t analog(uint8_t n) override { return channels[n & 7]; }
};

struct Pins : odin::MieHost {
	uint8_t gpio_read(uint8_t) override { return 0xFF; }
	void gpio_write(uint8_t, uint8_t) override {}
};

/* The program of the chip: from 0100, LD SP; P3CR 9C, P3 0C (the line
 * driven, no address); P67CR FF, P6 0D, P7 0B (outputs 1 and 5 on); P5
 * `p5`; ADMOD F7; SCMOD 29, BRGCR 02, INTEL 0C, INTEH 48; EI; then over and
 * over P2, P0, P1, P4, FFF0 and FFF3 into FF00-FF05. A byte received is
 * sent back plus 1, and P3 `p3` (08: the line driven, the address taken)
 * set. The watchdog does nothing. */
std::vector<uint8_t> program(uint8_t p5, uint8_t p3 = 0x08)
{
	std::vector<uint8_t> rom(odin::Tmp90ph44::ROM_SIZE);
	const auto put = [&rom](uint16_t at, std::initializer_list<uint8_t> code) {
		for (uint8_t b : code)
			rom[at++] = b;
	};
	put(0, { 0x1A, 0x00, 0x01 });
	put(0x100, { 0x3E, 0xBF, 0xFF, 0x37, 0xC7, 0x9C, 0x37, 0xC6, 0x0C, 0x37, 0xCD, 0xFF, 0x37, 0xCB, 0x0D,
	             0x37, 0xCC, 0x0B, 0x37, 0xCA, p5, 0x37, 0xEF, 0xF7, 0x37, 0xE6, 0x29, 0x37, 0xE9, 0x02,
	             0x37, 0xF4, 0x0C, 0x37, 0xF5, 0x48, 0x03,
	             0x27, 0xC4, 0x2F, 0x00, 0x27, 0xC0, 0x2F, 0x01, 0x27, 0xC2, 0x2F, 0x02, 0x27, 0xC8, 0x2F, 0x03,
	             0x27, 0xF0, 0x2F, 0x04, 0x27, 0xF3, 0x2F, 0x05, 0xC8, 0xE6 });
	put(0x20, { 0x1F });
	put(0x60, { 0x1A, 0x80, 0x01 });
	put(0x180, { 0x27, 0xE8, 0x86, 0x2F, 0xE8, 0x37, 0xC6, p3, 0x1F });
	put(0x68, { 0x1F });
	return rom;
}

constexpr uint64_t STATE = Io837_13551::TICKS_PER_STATE;

/* As MieBoard runs it: pieces of 64 us, 472 states */
void advance(Io837_13551 &board, uint64_t tick)
{
	while (board.chip().states() * STATE < tick)
		board.run_until(std::min(tick, (board.chip().states() + 472) * STATE));
}

void test_pins()
{
	Cabinet cabinet;
	cabinet.players[0] = 0x8080;	/* START, PUSH3 */
	cabinet.players[1] = 0x0400;	/* RIGHT */
	cabinet.sys = 0x80;		/* TEST */
	cabinet.channels[0] = 0x1200;
	cabinet.channels[3] = 0x3400;
	cabinet.channels[4] = 0x5600;
	cabinet.channels[7] = 0x7800;
	for (uint8_t p5 : { uint8_t(0x40), uint8_t(0x00) }) {
		const std::vector<uint8_t> rom = program(p5);
		Io837_13551 board(rom, cabinet);

		board.run_until(2000 * STATE);
		const odin::Tmp90ph44 &chip = board.chip();
		if (p5) {
			check(chip.peek(0xFF00) == 0x7E, "P2: 1P START bit 0, 1P PUSH3 bit 7, closed at 0");
			check(chip.peek(0xFF01) == 0xFD, "P0: 2P RIGHT bit 1");
			check(chip.peek(0xFF02) == 0xDF, "P1: TEST bit 5");
			check(chip.peek(0xFF03) == 0xFF, "P4: nothing closed");
			check(chip.peek(0xFF04) == 0x12 && chip.peek(0xFF05) == 0x34, "P56 1: AN0 channel 1, AN3 channel 4");
			check(board.outputs()[0] == 0x88, "P6 bit 1 at 0 output 1, P7 bit 2 output 5");
		} else {
			check(chip.peek(0xFF04) == 0x56 && chip.peek(0xFF05) == 0x78, "P56 0: AN0 channel 5, AN3 channel 8");
		}
	}
}

void test_coins()
{
	Cabinet cabinet;
	const std::vector<uint8_t> rom = program(0x40);
	Io837_13551 board(rom, cabinet);
	constexpr uint64_t C = Io837_13551::COIN_STATES;

	/* Looked at in the middle of each 50 ms, the board seeing its pins
	 * once a piece */
	advance(board, 100 * STATE);
	board.inc_coin(0);
	board.inc_coin(0);
	board.inc_coin(1);
	advance(board, (100 + C / 2) * STATE);
	check(!(board.port_pins(1) & 0x40) && !(board.port_pins(4) & 0x40), "a coin closes P1 bit 6, slot 2 P4 bit 6");
	check((board.port_pins(1) & 0x80) && (board.port_pins(4) & 0x80), "the returns of the meters read 1");
	advance(board, (100 + C + C / 2) * STATE);
	check((board.port_pins(1) & 0x40) && (board.port_pins(4) & 0x40), "50 ms, then open");
	advance(board, (100 + 2 * C + C / 2) * STATE);
	check(!(board.port_pins(1) & 0x40) && (board.port_pins(4) & 0x40), "50 ms later the next coin of the slot");
	advance(board, (100 + 5 * C) * STATE);
	check((board.port_pins(1) & 0x40), "and no more");
}

void test_line()
{
	Cabinet cabinet;
	const std::vector<uint8_t> rom = program(0x40);
	std::vector<uint8_t> mie_rom(odin::Mie::ROM_SIZE), mie_ram(odin::Mie::RAM_SIZE);
	Pins pins;
	odin::Mie mie(pins, mie_rom, mie_ram);
	mie.attach_line(nullptr);
	Io837_13551 board(rom, cabinet);
	mie.attach_line(&board);

	board.run_until(10000 * STATE);
	check(board.next_event() == UINT64_MAX, "nothing for the MIE yet");
	/* A byte from the MIE, its start bit 5000 ticks into a run of the
	 * MIE that starts at state 10000 */
	board.set_run_start(10000 * STATE);
	board.line_sent(0x41, 5000);
	const uint64_t due = 10000 * STATE + 5000 + odin::Tmp90ph44::RX_STATES * STATE;
	board.run_until(due - STATE);
	check(board.next_event() == UINT64_MAX, "the chip has not had it before its time");
	board.run_until(due + 2000 * STATE);
	board.deliver(mie, due + 300 * STATE);
	check(mie.in8(0x91) == 0x8E && !(mie.in8(0x15) & 1), "the sense input first, addressed when P3 bit 2 drops");
	check(board.next_event() >= due + odin::Tmp90ph44::BYTE_STATES * STATE &&
	      board.next_event() <= due + 800 * STATE, "the chip answers: its byte ends a character later");
	board.deliver(mie, due + 2000 * STATE);
	check((mie.in8(0x15) & 1) && mie.in8(0x10) == 0x42, "the MIE gets the byte from UART 1");

	/* The same with P3 18 in the handler: the line not driven */
	const std::vector<uint8_t> off = program(0x40, 0x18);
	Io837_13551 silent(off, cabinet);
	mie.attach_line(&silent);
	silent.run_until(10000 * STATE);
	silent.set_run_start(10000 * STATE);
	silent.line_sent(0x41, 5000);
	silent.run_until(due + 2000 * STATE);
	silent.deliver(mie, due + 2000 * STATE);
	check(!(mie.in8(0x15) & 1) && mie.in8(0x91) == 0x8E, "the line not driven: no byte reaches the MIE");
}

void test_state()
{
	Cabinet cabinet;
	const std::vector<uint8_t> rom = program(0x40);
	std::vector<uint8_t> mie_rom(odin::Mie::ROM_SIZE), mie_ram(odin::Mie::RAM_SIZE), other_ram(odin::Mie::RAM_SIZE);
	Pins pins;
	odin::Mie mie(pins, mie_rom, mie_ram), other(pins, mie_rom, other_ram);
	Io837_13551 board(rom, cabinet), loaded(rom, cabinet);
	mie.attach_line(&board);
	other.attach_line(&loaded);

	/* Saved with a byte on its way to the chip, the answer of an earlier
	 * one not given to the MIE, a coin closed and one waiting */
	board.run_until(10000 * STATE);
	board.set_run_start(10000 * STATE);
	board.line_sent(0x10, 0);
	board.run_until(10000 * STATE + 1500 * STATE);
	board.line_sent(0x20, 1500 * STATE);
	board.inc_coin(0);
	board.inc_coin(0);
	board.run_until(10000 * STATE + 1600 * STATE);
	Io837_13551::State state{};
	board.save(state);
	loaded.load(state, other);
	check(board.next_event() != UINT64_MAX && mie.in8(0x91) == 0x8C && other.in8(0x91) == 0x8C,
	      "loaded: the MIE not told yet of the address the chip has taken");

	const uint64_t end = 10000 * STATE + Io837_13551::COIN_STATES * 3 * STATE;
	bool same = true;
	for (uint64_t t = 10000 * STATE + 1600 * STATE; t < end; t += 997 * STATE) {
		board.run_until(t);
		loaded.run_until(t);
		board.deliver(mie, t);
		loaded.deliver(other, t);
		same = same && board.chip().states() == loaded.chip().states() && board.port_pins(1) == loaded.port_pins(1) &&
		       board.next_event() == loaded.next_event() && mie.in8(0x91) == other.in8(0x91);
		while (mie.in8(0x15) & 1) {
			const uint8_t waits = other.in8(0x15), mine = mie.in8(0x10), theirs = other.in8(0x10);

			same = same && (waits & 1) && mine == theirs;
		}
		same = same && !(other.in8(0x15) & 1);
	}
	check(same, "saved, loaded: the same from there");
	check(board.chip().peek(0xFF00) == 0xFF, "and it ran");
}

} /* namespace */

int main()
{
	test_pins();
	test_coins();
	test_line();
	test_state();

	std::printf("%d checks, %d failures\n", g_checks, g_failures);
	return g_failures ? 1 : 0;
}
