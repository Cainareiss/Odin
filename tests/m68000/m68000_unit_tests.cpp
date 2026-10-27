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

/* What the SingleStepTests suite does not reach on the Odin 68000: the
 * reset, interrupts and their mask, the level 7 edge, STOP, trace, the
 * illegal and line A / line F instructions, and a state saved and loaded
 * in the middle of a program.
 *
 * Build: g++ -std=c++20 -O2 -Wall -Wextra -Wpedantic -Wconversion -Wshadow -I src \
 *            tests/m68000/m68000_unit_tests.cpp src/odin/machine/cpu/m68000/m68000.cpp src/odin/machine/cpu/m68000/m68000_ops.cpp \
 *            -o m68000_unit_tests
 */

#include "odin/machine/cpu/m68000/m68000.hpp"

#include <array>
#include <cstdio>
#include <initializer_list>

namespace {

int g_failures = 0;
int g_checks = 0;

void check(bool ok, const char *what)
{
	g_checks++;
	if (!ok) {
		g_failures++;
		std::printf("FAIL %s\n", what);
	}
}

class Ram : public odin::M68000Bus {
public:
	std::array<uint8_t, 0x10000> mem{};
	unsigned acks = 0;
	int vector = -1;

	uint16_t read16(uint32_t addr, unsigned) override
	{
		return static_cast<uint16_t>((mem[addr & 0xFFFF] << 8) | mem[(addr + 1) & 0xFFFF]);
	}
	uint8_t read8(uint32_t addr, unsigned) override { return mem[addr & 0xFFFF]; }
	void write16(uint32_t addr, uint16_t v, unsigned) override
	{
		mem[addr & 0xFFFF] = static_cast<uint8_t>(v >> 8);
		mem[(addr + 1) & 0xFFFF] = static_cast<uint8_t>(v);
	}
	void write8(uint32_t addr, uint8_t v, unsigned) override { mem[addr & 0xFFFF] = v; }
	int int_ack(unsigned) override
	{
		acks++;
		return vector;
	}

	void w16(uint32_t a, uint16_t v) { write16(a, v, 5); }
	void w32(uint32_t a, uint32_t v)
	{
		w16(a, static_cast<uint16_t>(v >> 16));
		w16(a + 2, static_cast<uint16_t>(v));
	}
	uint16_t r16(uint32_t a) { return read16(a, 5); }
	uint32_t r32(uint32_t a) { return (static_cast<uint32_t>(r16(a)) << 16) | r16(a + 2); }
	void code(uint32_t a, std::initializer_list<uint16_t> words)
	{
		for (uint16_t w : words) {
			w16(a, w);
			a += 2;
		}
	}
};

/* SSP 0x8000, PC 0x1000; every vector points at a handler that is RTE */
void board(Ram &ram)
{
	ram.w32(0, 0x8000);
	ram.w32(4, 0x1000);
	for (uint32_t v = 2; v < 64; v++)
		ram.w32(v * 4, 0x2000 + v * 2);
	for (uint32_t v = 2; v < 64; v++)
		ram.w16(0x2000 + v * 2, 0x4E73);
}

void test_reset()
{
	Ram ram;
	board(ram);
	ram.code(0x1000, {0x4E71});
	odin::M68000 cpu(ram);
	cpu.reset();
	check(cpu.pc() == 0x1000, "reset: PC from vector 1");
	check(cpu.a(7) == 0x8000, "reset: SSP from vector 0");
	check(cpu.sr() == 0x2700, "reset: supervisor, mask 7");
}

/* MOVE #$2200,SR then a loop; level 3 autovector: 44 clocks, SR and PC
 * stacked, mask 3, vector 27; RTE comes back to the loop. */
void test_interrupt()
{
	Ram ram;
	board(ram);
	ram.code(0x1000, {0x46FC, 0x2200, 0x4E71, 0x60FC});	/* MOVE #$2200,SR; NOP; BRA -4 */
	odin::M68000 cpu(ram);
	cpu.reset();
	cpu.step();
	check(cpu.sr() == 0x2200, "interrupt: mask lowered to 2");
	cpu.set_ipl(2);
	cpu.step();
	check(ram.acks == 0, "interrupt: level 2 masked by 2");
	cpu.set_ipl(3);
	const uint32_t at = cpu.pc();
	const uint32_t clocks = cpu.step();
	check(ram.acks == 1, "interrupt: level 3 acknowledged");
	check(clocks == 44, "interrupt: 44 clocks with an autovector");
	check(cpu.pc() == 0x2000 + 27 * 2, "interrupt: autovector 27 for level 3");
	check((cpu.sr() & 0x2700) == 0x2300, "interrupt: supervisor, mask 3");
	check(ram.r16(cpu.a(7)) == 0x2200 && ram.r32(cpu.a(7) + 2) == at, "interrupt: SR and PC stacked");
	cpu.set_ipl(0);
	cpu.step();
	check(cpu.pc() == at && cpu.sr() == 0x2200, "interrupt: RTE back to the program");
	ram.vector = 64;
	ram.w32(64 * 4, 0x2100);
	ram.w16(0x2100, 0x4E73);
	cpu.set_ipl(5);
	cpu.step();
	check(cpu.pc() == 0x2100, "interrupt: vector from the device");
}

/* Level 7 whatever the mask, once per rising edge */
void test_nmi()
{
	Ram ram;
	board(ram);
	ram.code(0x1000, {0x4E71, 0x60FC});
	ram.code(0x2000 + 31 * 2, {0x4E71, 0x4E73});		/* the handler takes 2 steps */
	odin::M68000 cpu(ram);
	cpu.reset();
	cpu.set_ipl(7);
	cpu.step();
	check(ram.acks == 1 && cpu.pc() == 0x2000 + 31 * 2, "level 7: taken with mask 7");
	cpu.step();
	cpu.set_ipl(7);			/* the board sets its lines again */
	cpu.step();
	cpu.step();
	check(ram.acks == 1, "level 7: not again while held");
	cpu.set_ipl(0);
	cpu.set_ipl(7);
	cpu.step();
	check(ram.acks == 2, "level 7: again on the next edge");
}

/* STOP waits 4 clocks a step until an interrupt; the stacked PC is the
 * instruction after STOP */
void test_stop()
{
	Ram ram;
	board(ram);
	ram.code(0x1000, {0x4E72, 0x2000, 0x4E71});
	odin::M68000 cpu(ram);
	cpu.reset();
	cpu.step();
	check(cpu.stopped() && cpu.sr() == 0x2000, "STOP: stopped with the new SR");
	check(cpu.step() == 4 && cpu.stopped(), "STOP: 4 clocks a step while stopped");
	cpu.set_ipl(1);
	cpu.step();
	check(!cpu.stopped() && ram.r32(cpu.a(7) + 2) == 0x1004, "STOP: an interrupt wakes it, PC after STOP stacked");
}

/* Trace: T at the start of an instruction gives vector 9 after it, with
 * the PC of the next one; the instruction that sets T is not traced */
void test_trace()
{
	Ram ram;
	board(ram);
	ram.code(0x1000, {0x46FC, 0xA700, 0x7005, 0x4E71});	/* MOVE #$A700,SR; MOVEQ #5,D0 */
	odin::M68000 cpu(ram);
	cpu.reset();
	cpu.step();
	check(cpu.pc() == 0x1004 && (cpu.sr() & 0x8000), "trace: not after the instruction that set T");
	const uint32_t clocks = cpu.step();
	check(cpu.d(0) == 5 && cpu.pc() == 0x2000 + 9 * 2, "trace: MOVEQ ran, then the trace");
	check(clocks == 4 + 34, "trace: 34 clocks for the exception");
	check(ram.r32(cpu.a(7) + 2) == 0x1006, "trace: PC of the next instruction stacked");
	cpu.step();
	check(cpu.pc() == 0x1006 && (cpu.sr() & 0x8000), "trace: RTE back with T");
}

/* ILLEGAL, line A, line F: 34 clocks, the PC of the instruction stacked */
void test_illegal()
{
	struct Case {
		uint16_t op;
		unsigned vector;
		const char *name;
	};
	for (const Case &c : {Case{0x4AFC, 4, "ILLEGAL"}, Case{0xA123, 10, "line A"}, Case{0xF456, 11, "line F"}}) {
		Ram ram;
		board(ram);
		ram.code(0x1000, {c.op});
		odin::M68000 cpu(ram);
		cpu.reset();
		const uint32_t clocks = cpu.step();
		char what[64];
		std::snprintf(what, sizeof(what), "%s: vector %u, 34 clocks, its PC", c.name, c.vector);
		check(cpu.pc() == 0x2000 + c.vector * 2 && clocks == 34 && ram.r32(cpu.a(7) + 2) == 0x1000, what);
	}
}

/* A state saved halfway and loaded into another CPU runs the same way */
void test_state()
{
	Ram ram;
	board(ram);
	/* D0 = 1000 times a DBRA loop adding to D1 */
	ram.code(0x1000, {0x303C, 0x03E7, 0x7200, 0xD280, 0x51C8, 0xFFFC, 0x4E71, 0x60FE});
	odin::M68000 cpu(ram);
	cpu.reset();
	cpu.run(2000);
	odin::M68000State half;
	cpu.save(half);
	Ram ram2 = ram;
	odin::M68000 other(ram2);
	other.load(half);
	cpu.run(40000);
	other.run(40000);
	odin::M68000State a, b;
	cpu.save(a);
	other.save(b);
	check(a == b, "state: the same after loading");
	check(a.d[1] == 499500, "state: the loop added 0..999");
}

} /* namespace */

int main()
{
	test_reset();
	test_interrupt();
	test_nmi();
	test_stop();
	test_trace();
	test_illegal();
	test_state();
	std::printf("m68000_unit_tests: %d checks, %d failed\n", g_checks, g_failures);
	return g_failures ? 1 : 0;
}
