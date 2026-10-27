/*
 *  ██████╗ ██████╗ ██╗███╗   ██╗
 * ██╔═══██╗██╔══██╗██║████╗  ██║
 * ██║   ██║██║  ██║██║██╔██╗ ██║
 * ██║   ██║██║  ██║██║██║╚██╗██║
 * ╚██████╔╝██████╔╝██║██║ ╚████║
 *  ╚═════╝ ╚═════╝ ╚═╝╚═╝  ╚═══╝
 *
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

/* Unit tests for the board around the 315-6146 of the 837-13741 and its
 * D71054: the pins of the switches, the coins, the analog channels, the
 * outputs, the tick of the timer, the line to the MIE and the sense input,
 * its state. The chip runs a small program in place of epr-21868
 * (tests/jvs/epr21868_bench.cpp runs that one).
 *
 * Build: g++ -std=c++20 -O2 -Wall -Wextra -Wpedantic -Wconversion -Wshadow \
 *            -I src tests/jvs/io_837_13741_tests.cpp src/odin/machine/cabinet/io_837_13741.cpp src/odin/machine/chips/d71054.cpp \
 *            src/odin/machine/chips/mie.cpp src/odin/machine/cpu/z80/z80.cpp src/odin/machine/cpu/z80/z80_ops.cpp src/odin/machine/cabinet/jvs.cpp -o io_837_13741_tests
 * Exit status is 0 when every check passes. */

#include "odin/machine/chips/d71054.hpp"
#include "odin/machine/cabinet/io_837_13741.hpp"

#include <algorithm>
#include <cstdio>
#include <initializer_list>
#include <vector>

namespace {

using odin::Io837_13741;

constexpr uint64_t CYCLE = odin::Mie::TICKS_PER_CYCLE;
constexpr uint64_t MS = 16000 * CYCLE;

unsigned g_failures = 0;
unsigned g_checks = 0;

void check(bool ok, const char *what)
{
	g_checks++;
	if (!ok) {
		g_failures++;
		std::printf("FAIL: %s\n", what);
	}
}

struct Cabinet : odin::JvsInputs {
	uint8_t sys = 0;
	uint16_t players[2] = { 0, 0 };
	uint16_t channels[8] = {};

	uint8_t system() override { return sys; }
	uint16_t player(uint8_t index) override { return players[index & 1]; }
	uint16_t analog(uint8_t channel) override { return channels[channel & 7]; }
};

struct Pins : odin::MieHost {
	uint8_t gpio_read(uint8_t) override { return 0xFF; }
	void gpio_write(uint8_t, uint8_t) override {}
};

/* The program of the chip: IM 2, the table at 00F2 (F8 UART 1 at 0200, FE
 * the timer at 0300); ports 0, 1, 2, 6 inputs (direction 47), 3, 4, 5 FF;
 * the D71054 as epr-21868 sets it (0120-0145: counter 2 mode 3 / 1000,
 * counter 0 mode 2 / 16); UART 1 115200 bauds, its receiver interrupting;
 * port 30 88, port 90 01; EI; then over and over ports 0, 1, 2, 6 into
 * 8000-8006, channels 1 and 2 into 8008-8009, and 8013-8015 inverted out
 * on ports 3, 4, 5. A byte received is sent back plus 1, and port 90 set
 * to 03 (the address taken); a tick of the timer counts in 8020. */
std::vector<uint8_t> program()
{
	std::vector<uint8_t> rom(0x10000, 0xFF);
	const auto put = [&rom](uint16_t at, std::initializer_list<uint8_t> bytes) {
		for (uint8_t b : bytes)
			rom[at++] = b;
	};

	put(0x0000, { 0xF3, 0x31, 0x00, 0x00, 0xC3, 0x00, 0x01 });
	put(0x00F8, { 0x00, 0x02 });
	put(0x00FE, { 0x00, 0x03 });
	put(0x0100, { 0xED, 0x5E, 0xAF, 0xED, 0x47,
	              0x3E, 0x47, 0xD3, 0x08,
	              0x3E, 0xFF, 0xD3, 0x03, 0xD3, 0x04, 0xD3, 0x05,
	              0x3E, 0xB6, 0xD3, 0xA3, 0x3E, 0xE8, 0xD3, 0xA2, 0x3E, 0x03, 0xD3, 0xA2,
	              0x3E, 0x34, 0xD3, 0xA3, 0x3E, 0x10, 0xD3, 0xA0, 0xAF, 0xD3, 0xA0,
	              0x3E, 0x80, 0xD3, 0x13, 0x3E, 0x08, 0xD3, 0x10, 0xAF, 0xD3, 0x11,
	              0x3E, 0x03, 0xD3, 0x13, 0x3E, 0x01, 0xD3, 0x11,
	              0x3E, 0x88, 0xD3, 0x30, 0x3E, 0x01, 0xD3, 0x90, 0xFB });
	put(0x0144, { 0xDB, 0x00, 0x32, 0x00, 0x80, 0xDB, 0x01, 0x32, 0x01, 0x80,
	              0xDB, 0x02, 0x32, 0x02, 0x80, 0xDB, 0x06, 0x32, 0x06, 0x80,
	              0xAF, 0xD3, 0x0F, 0xDB, 0x0F, 0x32, 0x08, 0x80, 0xDB, 0x0F, 0x32, 0x09, 0x80,
	              0x3A, 0x13, 0x80, 0x2F, 0xD3, 0x03, 0x3A, 0x14, 0x80, 0x2F, 0xD3, 0x04,
	              0x3A, 0x15, 0x80, 0x2F, 0xD3, 0x05, 0xC3, 0x44, 0x01 });
	put(0x0200, { 0xF5, 0xDB, 0x10, 0x3C, 0xD3, 0x10, 0x3E, 0x03, 0xD3, 0x90, 0xF1, 0xFB, 0xED, 0x4D });
	put(0x0300, { 0xE5, 0x2A, 0x20, 0x80, 0x23, 0x22, 0x20, 0x80, 0xE1, 0xFB, 0xED, 0x4D });
	return rom;
}

/* The board run as MieBoard::run_line runs it: a piece at a time */
void advance(Io837_13741 &board, uint64_t tick)
{
	while (board.now() < tick)
		board.run_until(std::min(tick, board.now() + 1024 * CYCLE));
}

uint8_t ram(Io837_13741 &board, uint16_t addr)
{
	return board.chip().read8(addr);
}

uint16_t ticks_counted(Io837_13741 &board)
{
	return static_cast<uint16_t>(ram(board, 0x8020) | ram(board, 0x8021) << 8);
}

void test_pins()
{
	Cabinet cabinet;
	const std::vector<uint8_t> rom = program();
	Io837_13741 board(rom, cabinet);

	board.run_until(2 * MS);
	check(ram(board, 0x8000) == 0xF3 && ram(board, 0x8001) == 0xFF && ram(board, 0x8002) == 0xFF &&
	      ram(board, 0x8006) == 0xFF, "nothing pressed: DIP switches off, meters wired, the rest open");

	cabinet.sys = 0x80;
	board.run_until(3 * MS);
	check(ram(board, 0x8000) == 0xB3, "TEST closes port 0 bit 6");
	cabinet.sys = 0x40;
	board.run_until(4 * MS);
	check(ram(board, 0x8000) == 0x73, "TILT closes port 0 bit 7");
	cabinet.sys = 0;

	cabinet.players[0] = 0x8000;
	board.run_until(5 * MS);
	check(ram(board, 0x8001) == 0xFE && ram(board, 0x8006) == 0xFF, "player 1 START: port 1 bit 0");
	cabinet.players[0] = 0x0100;
	board.run_until(6 * MS);
	check(ram(board, 0x8001) == 0x7F, "player 1 word bit 8: port 1 bit 7");
	cabinet.players[0] = 0x0080;
	board.run_until(7 * MS);
	check(ram(board, 0x8001) == 0xFF && ram(board, 0x8006) == 0xFE, "player 1 word bit 7: port 6 bit 0");
	cabinet.players[0] = 0x0010;
	board.run_until(8 * MS);
	check(ram(board, 0x8006) == 0xF7, "player 1 word bit 4: port 6 bit 3");
	cabinet.players[0] = 0x000F;
	board.run_until(9 * MS);
	check(ram(board, 0x8006) == 0xFF && ram(board, 0x8001) == 0xFF, "bits 0-3 of the word: no pin");
	cabinet.players[0] = 0;
	cabinet.players[1] = 0x8080;
	board.run_until(10 * MS);
	check(ram(board, 0x8002) == 0xFE && ram(board, 0x8006) == 0xEF && ram(board, 0x8001) == 0xFF,
	      "player 2: port 2, and port 6 bits 4-7");
	cabinet.players[1] = 0x0110;
	board.run_until(11 * MS);
	check(ram(board, 0x8002) == 0x7F && ram(board, 0x8006) == 0x7F, "player 2 word bits 8 and 4: port 2 bit 7, port 6 bit 7");
	cabinet.players[1] = 0;

	cabinet.channels[0] = 0x12FF;
	cabinet.channels[1] = 0xAB00;
	board.run_until(12 * MS);
	check(ram(board, 0x8008) == 0x12 && ram(board, 0x8009) == 0xAB, "the converter: the high byte of each channel, in order");
}

void test_coins()
{
	Cabinet cabinet;
	const std::vector<uint8_t> rom = program();
	Io837_13741 board(rom, cabinet);
	const uint64_t C = Io837_13741::COIN_CYCLES * CYCLE;

	board.run_until(2 * MS);
	board.inc_coin(0);
	board.inc_coin(0);
	board.inc_coin(1);
	advance(board, 2 * MS + C / 2);
	check(ram(board, 0x8000) == 0xC3, "a coin closes port 0 bit 4, slot 2 bit 5");
	advance(board, 2 * MS + C + C / 2);
	check(ram(board, 0x8000) == 0xF3, "50 ms, then open");
	advance(board, 2 * MS + 2 * C + C / 2);
	check(ram(board, 0x8000) == 0xE3, "50 ms later the next coin of the slot");
	advance(board, 2 * MS + 5 * C);
	check(ram(board, 0x8000) == 0xF3, "and no more");
}

void test_outputs()
{
	Cabinet cabinet;
	const std::vector<uint8_t> rom = program();
	Io837_13741 board(rom, cabinet);
	const auto out = [&board](unsigned i) { return board.outputs()[i]; };

	board.run_until(2 * MS);
	check(board.outputs().size() == 4 && !out(0) && !out(1) && !out(2) && !out(3), "every output off");
	board.chip().write8(0x8013, 0x04);
	board.run_until(3 * MS);
	check(out(0) == 0x80 && !out(1) && !out(2), "port 3 bit 2 low: output 1");
	board.chip().write8(0x8013, 0x83);
	board.run_until(4 * MS);
	check(out(0) == 0x04 && !out(1), "port 3 bit 7: output 6; bits 0 and 1 drive none");
	board.chip().write8(0x8013, 0);
	board.chip().write8(0x8014, 0x81);
	board.run_until(5 * MS);
	check(out(0) == 0x02 && out(1) == 0x04 && !out(2), "port 4 bits 0 and 7: outputs 7 and 14");
	board.chip().write8(0x8014, 0);
	board.chip().write8(0x8015, 0x81);
	board.run_until(6 * MS);
	check(!out(0) && out(1) == 0x02 && out(2) == 0x04 && !out(3), "port 5 bits 0 and 7: outputs 15 and 22");
	board.chip().out8(0x08, 0x67);
	check(!out(1) && !out(2), "a port the chip no longer drives: its outputs off");
}

void test_timer()
{
	Cabinet cabinet;
	const std::vector<uint8_t> rom = program();
	Io837_13741 board(rom, cabinet);

	board.run_until(MS / 2);
	const uint16_t first = ticks_counted(board);
	board.run_until(MS / 2 + 20 * MS);
	check(first <= 1 && ticks_counted(board) - first == 20, "the timer interrupts every millisecond");
}

void test_line()
{
	Cabinet cabinet;
	const std::vector<uint8_t> rom = program();
	std::vector<uint8_t> mie_rom(odin::Mie::ROM_SIZE), mie_ram(odin::Mie::RAM_SIZE);
	Pins pins;
	odin::Mie mie(pins, mie_rom, mie_ram);
	Io837_13741 board(rom, cabinet);
	mie.attach_line(&board);

	board.run_until(2 * MS);
	check(board.next_event() == UINT64_MAX && mie.in8(0x91) == 0x8C, "nothing for the MIE yet, no address");
	/* A byte from the MIE, its start bit 5000 ticks into a run of the
	 * MIE that starts at 2 ms */
	board.set_run_start(2 * MS);
	board.line_sent(0x41, 5000);
	const uint64_t due = 2 * MS + 5000 + Io837_13741::CHAR_TICKS;
	board.run_until(due - CYCLE);
	check(board.next_event() == UINT64_MAX, "the chip has not had it before its time");
	board.run_until(due + 200 * CYCLE);
	const uint64_t sense = board.next_event();
	check(sense >= due && sense <= due + 200 * CYCLE, "port 90 bit 1 set in the handler: the MIE is told at once");
	board.deliver(mie, sense);
	check(mie.in8(0x91) == 0x8E && !(mie.in8(0x15) & 1), "the sense input first, addressed");
	check(board.next_event() >= due + Io837_13741::CHAR_TICKS && board.next_event() <= due + 400 * CYCLE +
	      Io837_13741::CHAR_TICKS, "the chip answers: its byte ends a character later");
	board.run_until(due + 2 * Io837_13741::CHAR_TICKS);
	board.deliver(mie, due + 2 * Io837_13741::CHAR_TICKS);
	check((mie.in8(0x15) & 1) && mie.in8(0x10) == 0x42, "the MIE gets the byte from UART 1");
}

void test_state()
{
	Cabinet cabinet;
	const std::vector<uint8_t> rom = program();
	std::vector<uint8_t> mie_rom(odin::Mie::ROM_SIZE), mie_ram(odin::Mie::RAM_SIZE), other_ram(odin::Mie::RAM_SIZE);
	Pins pins;
	odin::Mie mie(pins, mie_rom, mie_ram), other(pins, mie_rom, other_ram);
	Io837_13741 board(rom, cabinet), loaded(rom, cabinet);
	mie.attach_line(&board);
	other.attach_line(&loaded);

	board.run_until(2 * MS);
	board.inc_coin(1);
	board.inc_coin(1);
	board.chip().write8(0x8014, 0x10);
	board.set_run_start(2 * MS);
	board.line_sent(0x10, 0);
	board.run_until(2 * MS + Io837_13741::CHAR_TICKS / 2);
	board.line_sent(0x20, Io837_13741::CHAR_TICKS / 2);
	board.run_until(2 * MS + Io837_13741::CHAR_TICKS + 300 * CYCLE);
	/* Saved with the sense and a byte for the MIE in flight, a byte for
	 * the chip on the line, a coin in its slot */
	Io837_13741::State state;
	board.save(state);
	loaded.load(state, other);
	check(board.next_event() != UINT64_MAX && mie.in8(0x91) == 0x8C && other.in8(0x91) == 0x8C,
	      "the MIE of the loaded board still waits for the sense");
	bool same = true;
	for (uint64_t t = board.now() + 1000 * CYCLE; t < 2 * MS + 120 * MS; t += 1000 * CYCLE) {
		advance(board, t);
		advance(loaded, t);
		board.deliver(mie, t);
		loaded.deliver(other, t);
		same = same && board.now() == loaded.now() && board.next_event() == loaded.next_event() &&
		       mie.in8(0x91) == other.in8(0x91) && ram(board, 0x8000) == ram(loaded, 0x8000) &&
		       ticks_counted(board) == ticks_counted(loaded) && board.outputs()[1] == loaded.outputs()[1];
		while (mie.in8(0x15) & 1)
			same = same && (other.in8(0x15) & 1) && mie.in8(0x10) == other.in8(0x10);
		same = same && !(other.in8(0x15) & 1);
	}
	check(same, "the loaded board goes on as the one saved: line, sense, coins, timer, outputs");
	check(mie.in8(0x91) == 0x8E && ticks_counted(board) > 100, "and both got there");
}

void test_d71054()
{
	odin::D71054 timer;
	check(timer.next_pulse() == UINT64_MAX && !timer.unknown(), "no pulse before it is set");
	timer.write(3, 0xB6, 0);
	timer.write(2, 0xE8, 0);
	timer.write(2, 0x03, 0);
	timer.write(3, 0x34, 100);
	timer.write(0, 0x10, 100);
	check(timer.next_pulse() == UINT64_MAX, "counter 0 half loaded: no pulse");
	timer.write(0, 0x00, 200);
	check(timer.next_pulse() == 200 + 16000, "a pulse every 1000 x 16 clocks, a period after counter 0 is loaded");
	timer.pulse_taken();
	check(timer.next_pulse() == 200 + 32000, "then the next");
	odin::D71054::State saved;
	timer.save(saved);
	timer.write(3, 0x34, 500);
	check(timer.next_pulse() == UINT64_MAX, "counter 0 set again: no pulse until it is loaded");
	timer.load(saved);
	check(timer.next_pulse() == 200 + 32000, "its state back");
	timer.write(3, 0x74, 600);
	timer.write(1, 0x10, 600);
	timer.write(1, 0x00, 600);
	check(timer.next_pulse() == 200 + 32000, "counter 1 loaded: the pulses of counter 0 go on");
	timer.write(3, 0xBE, 700);
	timer.write(3, 0x3C, 700);
	check(!timer.unknown(), "counter 1, and modes 7 and 6 for 3 and 2: run");
	odin::D71054 wide;
	wide.write(3, 0xB6, 0);
	wide.write(2, 0xE8, 0);
	wide.write(2, 0x03, 0);
	wide.write(3, 0x34, 0);
	wide.write(0, 0x00, 0);
	wide.write(0, 0x00, 10);
	check(wide.next_pulse() == 10 + 65536ull * 1000, "a count of 0 is 65536");
	wide.write(3, 0xB6, 20);
	wide.write(0, 0x10, 30);
	wide.write(0, 0x00, 30);
	check(wide.next_pulse() == UINT64_MAX, "counter 2 set again, not loaded: no pulse");
	odin::D71054 square;
	square.write(3, 0xB4, 0);
	square.write(2, 0xE8, 0);
	square.write(2, 0x03, 0);
	square.write(3, 0x34, 0);
	square.write(0, 0x10, 0);
	square.write(0, 0x00, 0);
	check(square.next_pulse() == UINT64_MAX, "counter 2 in mode 2, loaded: not the cascade it runs, no pulse");
	const auto told = [](uint8_t control) {
		odin::D71054 t;
		t.write(3, control, 0);
		return t.unknown();
	};
	check(told(0xB4) && told(0x30) && told(0x00) && told(0xC2) && told(0x35) && told(0x14),
	      "told: counter 2 in mode 2, counter 0 in mode 0, the latch, the read-back, BCD, LSB alone");
}

} /* namespace */

int main()
{
	test_pins();
	test_coins();
	test_outputs();
	test_timer();
	test_line();
	test_state();
	test_d71054();
	std::printf("%u checks, %u failed\n", g_checks, g_failures);
	return g_failures ? 1 : 0;
}
