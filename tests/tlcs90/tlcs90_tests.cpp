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

/* Unit tests for the TLCS-90 CPU: addressing, flags, times and interrupts,
 * after the 1988 Toshiba data book (examples where it gives them).
 *
 * Build: g++ -std=c++20 -O2 -Wall -Wextra -Wpedantic -Wconversion -Wshadow \
 *            -I src tests/tlcs90/tlcs90_tests.cpp src/odin/tlcs90.cpp -o tlcs90_tests
 * Exit status is 0 when every check passes. */

#include "odin/tlcs90.hpp"

#include <array>
#include <cstdio>
#include <initializer_list>

namespace {

using odin::Tlcs90;

int g_checks, g_failures;
/* The programs below 2000 decoded once (Tlcs90::set_fixed) */
bool g_fixed;

void check(bool ok, const char *what)
{
	g_checks++;
	if (!ok) {
		g_failures++;
		std::printf("FAILED: %s\n", what);
	}
}

/* 64 KB of RAM, and one interrupt the test sets */
struct Ram : odin::Tlcs90Bus {
	std::array<uint8_t, 0x10000> m{};
	uint16_t vector = 0;
	bool maskable = true;

	uint8_t read8(uint16_t a) override { return m[a]; }
	void write8(uint16_t a, uint8_t v) override { m[a] = v; }
	uint16_t interrupt(bool enabled) override
	{
		if (!vector || (maskable && !enabled))
			return 0;
		const uint16_t v = vector;

		vector = 0;
		return v;
	}
	void put(uint16_t at, std::initializer_list<uint8_t> bytes)
	{
		for (uint8_t b : bytes)
			m[at++] = b;
	}
};

/* A CPU with a program at 0: registers set by a prelude of LD r,n */
struct Machine {
	Ram ram;
	Tlcs90 cpu{ ram };

	Machine()
	{
		if (g_fixed)
			cpu.set_fixed(0x2000);
	}
};

void test_loads_and_addressing()
{
	Machine t;

	/* LD A,03 ; LD HL,2000 ; LD B,(HL+A) (F3 28) : the example of the
	 * data book, 89 at 2003 */
	t.ram.put(0, { 0x36, 0x03, 0x3A, 0x00, 0x20, 0xF3, 0x28 });
	t.ram.m[0x2003] = 0x89;
	t.cpu.step();
	t.cpu.step();
	const uint32_t states = t.cpu.step();
	odin::Tlcs90State s;
	t.cpu.save(s);
	check(s.b == 0x89, "LD B,(HL+A): the example of the data book");
	check(states == 14, "in 14 states");

	/* A is signed in (HL+A) */
	Machine u;
	u.ram.put(0, { 0x36, 0x80, 0x3A, 0x00, 0x20, 0xF3, 0x28 });
	u.ram.m[0x1F80] = 0x5A;
	u.cpu.step(); u.cpu.step(); u.cpu.step();
	u.cpu.save(s);
	check(s.b == 0x5A, "(HL+A) with A = 80: HL - 128");

	/* LD (FF00+n),n then LD A,(FF00+n) ; LD (IX+d),r ; LD (mn),rr */
	Machine v;
	v.ram.put(0, { 0x37, 0xC6, 0x5A, 0x27, 0xC6, 0x3C, 0x00, 0x30, 0xF4, 0x02, 0x26,
	               0x3A, 0x34, 0x12, 0xEB, 0x00, 0x40, 0x42 });
	for (int i = 0; i < 6; i++)
		v.cpu.step();
	check(v.ram.m[0xFFC6] == 0x5A && v.cpu.a() == 0x5A, "the short addresses are FF00 + n");
	check(v.ram.m[0x3002] == 0x5A, "LD (IX+2),A");
	check(v.ram.m[0x4000] == 0x34 && v.ram.m[0x4001] == 0x12, "LD (mn),HL, low byte first");
}

void test_arithmetic()
{
	Machine t;

	/* LD A,7F ; ADD A,01 : V, H, S */
	t.ram.put(0, { 0x36, 0x7F, 0x68, 0x01 });
	t.cpu.step(); t.cpu.step();
	check(t.cpu.a() == 0x80 && (t.cpu.f() & 0xD7) == (Tlcs90::FLAG_S | Tlcs90::FLAG_H | Tlcs90::FLAG_V),
	      "7F + 1: S, H and V, no Z, no C");

	/* LD A,10 ; SUB A,20 : C and X with the borrow, N */
	Machine u;
	u.ram.put(0, { 0x36, 0x10, 0x6A, 0x20 });
	u.cpu.step(); u.cpu.step();
	check(u.cpu.a() == 0xF0 && (u.cpu.f() & (Tlcs90::FLAG_C | Tlcs90::FLAG_X | Tlcs90::FLAG_N)) ==
	      (Tlcs90::FLAG_C | Tlcs90::FLAG_X | Tlcs90::FLAG_N), "10 - 20: borrow in C and X, N");

	/* LD A,15 ; ADD A,27 ; DAA : 42 in BCD */
	Machine d;
	d.ram.put(0, { 0x36, 0x15, 0x68, 0x27, 0x0B });
	d.cpu.step(); d.cpu.step(); d.cpu.step();
	check(d.cpu.a() == 0x42, "15 + 27 then DAA: 42");

	/* LD L,0C ; MUL HL,0B ; then LD HL,0100 ; DIV HL,07 */
	Machine m;
	m.ram.put(0, { 0x35, 0x0C, 0x12, 0x0B, 0x3A, 0x00, 0x01, 0x13, 0x07 });
	m.cpu.step();
	const uint32_t mul = m.cpu.step();
	check(m.cpu.hl() == 132 && mul == 16, "MUL HL,n: L times n, 16 states");
	m.cpu.step(); m.cpu.step();
	check(m.cpu.hl() == ((256 % 7) << 8 | 256 / 7), "DIV HL,n: remainder in H, quotient in L");

	/* AND sets H and the parity; XOR clears H */
	Machine a;
	a.ram.put(0, { 0x36, 0x0F, 0x6C, 0x03, 0x6D, 0x01 });
	a.cpu.step(); a.cpu.step();
	check((a.cpu.f() & (Tlcs90::FLAG_H | Tlcs90::FLAG_V)) == (Tlcs90::FLAG_H | Tlcs90::FLAG_V), "AND: H, even parity");
	a.cpu.step();
	check(a.cpu.a() == 2 && !(a.cpu.f() & (Tlcs90::FLAG_H | Tlcs90::FLAG_V)), "XOR: no H, odd parity");

	/* 16 bits: ADD HL,BC keeps S Z V; INC rr only tells 0 in X */
	Machine w;
	w.ram.put(0, { 0x3A, 0xFF, 0xFF, 0x38, 0x01, 0x00, 0xF8, 0x70, 0x90, 0x92 });
	w.cpu.step(); w.cpu.step(); w.cpu.step();
	check(w.cpu.hl() == 0 && (w.cpu.f() & (Tlcs90::FLAG_C | Tlcs90::FLAG_X)) && !(w.cpu.f() & Tlcs90::FLAG_Z),
	      "ADD HL,BC: carry in C and X, Z left");
	w.cpu.step(); w.cpu.step();
	check(w.cpu.bc() == 2 && w.cpu.hl() == 1 && !(w.cpu.f() & Tlcs90::FLAG_X), "INC rr: X cleared on a result not 0");
}

void test_rotations()
{
	/* RLCA (A0) leaves S Z V; RLC A as RLC g (FE A0) sets them. CP A,80
	 * first leaves Z set. */
	Machine t;
	t.ram.put(0, { 0x36, 0x80, 0x6F, 0x80, 0xA0, 0x36, 0x80, 0x6F, 0x80, 0xFE, 0xA0 });
	t.cpu.step(); t.cpu.step();
	const uint32_t short_states = t.cpu.step();
	check(t.cpu.a() == 0x01 && (t.cpu.f() & Tlcs90::FLAG_C) && (t.cpu.f() & Tlcs90::FLAG_Z) && short_states == 2,
	      "RLCA: carry out, Z of the CP before left, 2 states");
	t.cpu.step(); t.cpu.step();
	const uint32_t states = t.cpu.step();
	check(t.cpu.a() == 0x01 && !(t.cpu.f() & (Tlcs90::FLAG_Z | Tlcs90::FLAG_S | Tlcs90::FLAG_V)) && states == 4,
	      "RLC g on A: S Z and the parity of 01, 4 states");

	/* SRL (HL) through a prefix */
	Machine u;
	u.ram.put(0, { 0x3A, 0x00, 0x20, 0xE2, 0xA7 });
	u.ram.m[0x2000] = 0x03;
	u.cpu.step(); u.cpu.step();
	check(u.ram.m[0x2000] == 0x01 && (u.cpu.f() & Tlcs90::FLAG_C), "SRL (HL)");
}

void test_jumps()
{
	/* JR +d is from the next instruction; DJNZ is 10 states both ways */
	Machine t;
	t.ram.put(0, { 0x30, 0x02, 0x18, 0xFE });
	t.cpu.step();
	const uint32_t first = t.cpu.step();
	check(t.cpu.pc() == 0x0002, "DJNZ back to itself while B is not 0");
	const uint32_t last = t.cpu.step();
	check(t.cpu.pc() == 0x0004 && first == 10 && last == 10, "then on, 10 states each time");

	/* JR cc: 8 taken, 4 not; cc 6 is Z */
	Machine u;
	u.ram.put(0, { 0x36, 0x01, 0x6F, 0x01, 0xC6, 0x10, 0xCE, 0x10 });
	u.cpu.step(); u.cpu.step();
	const uint32_t taken = u.cpu.step();
	check(u.cpu.pc() == 0x0016 && taken == 8, "JR Z taken: next + d, 8 states");

	/* JRL is relative to the instruction + 2; CALL and RET */
	Machine v;
	v.ram.put(0, { 0x3E, 0x00, 0x30, 0x1B, 0x0E, 0x10 });
	v.ram.put(0x1013, { 0x1C, 0x00, 0x40 });
	v.ram.put(0x4000, { 0x1E });
	v.cpu.step(); v.cpu.step();
	check(v.cpu.pc() == 0x1013, "JRL: the instruction + 2 + cd");
	v.cpu.step();
	check(v.cpu.pc() == 0x4000 && v.cpu.sp() == 0x2FFE && v.ram.m[0x2FFE] == 0x16, "CALL pushes the next address");
	v.cpu.step();
	check(v.cpu.pc() == 0x1016 && v.cpu.sp() == 0x3000, "RET");

	/* LDIR: 18 states while BC is not 0, 14 at the last */
	Machine w;
	w.ram.put(0, { 0x3A, 0x00, 0x20, 0x3A - 1, 0x00, 0x30, 0x38, 0x02, 0x00, 0xFE, 0x59 });
	w.ram.put(0x2000, { 0xAA, 0xBB });
	for (int i = 0; i < 3; i++)
		w.cpu.step();
	const uint32_t again = w.cpu.step();
	const uint32_t done = w.cpu.step();
	check(w.ram.m[0x3000] == 0xAA && w.ram.m[0x3001] == 0xBB && w.cpu.bc() == 0, "LDIR copies BC bytes");
	check(again == 18 && done == 14 && w.cpu.pc() == 0x000B, "18 states again, 14 at the end");
}

void test_interrupts()
{
	Machine t;

	/* LD SP,3000 ; EI ; NOP ; NOP: the interrupt waits for the NOP */
	t.ram.put(0, { 0x3E, 0x00, 0x30, 0x03, 0x00, 0x00 });
	t.ram.put(0x0028, { 0x1F });
	t.cpu.step();
	t.cpu.step();
	t.ram.vector = 0x0028;
	t.cpu.step();
	check(t.cpu.pc() == 0x0005, "after EI, one more instruction before an interrupt");
	const uint32_t entry = t.cpu.step();
	check(t.cpu.pc() == 0x0028 && entry == Tlcs90::INTERRUPT_STATES && entry == 20, "then taken, in 20 states");
	check(t.ram.m[0x2FFF] == 0x00 && t.ram.m[0x2FFE] == 0x05 && t.ram.m[0x2FFD] == t.cpu.a() &&
	      (t.ram.m[0x2FFC] & Tlcs90::FLAG_I), "PC then AF on the stack, IFF set in the F kept");
	check(!(t.cpu.f() & Tlcs90::FLAG_I), "IFF cleared by the acceptance");
	t.cpu.step();
	check(t.cpu.pc() == 0x0005 && (t.cpu.f() & Tlcs90::FLAG_I), "RETI gives back PC and AF");

	/* A maskable request waits with IFF 0; a non-maskable one does not */
	Machine u;
	u.ram.put(0, { 0x3E, 0x00, 0x30, 0x00, 0x00 });
	u.cpu.step();
	u.ram.vector = 0x0028;
	u.cpu.step();
	check(u.cpu.pc() == 0x0004 && u.ram.vector == 0x0028, "DI: a maskable interrupt waits");
	u.ram.maskable = false;
	u.ram.vector = 0x0020;
	u.cpu.step();
	check(u.cpu.pc() == 0x0020, "a non-maskable one is taken all the same");

	/* HALT waits for an interrupt */
	Machine h;
	h.ram.put(0, { 0x3E, 0x00, 0x30, 0x01, 0x00 });
	h.cpu.step();
	h.cpu.step();
	check(h.cpu.halted() && h.cpu.step() == 2 && h.cpu.pc() == 0x0004, "HALT idles two states at a time");
	h.ram.maskable = false;
	h.ram.vector = 0x0020;
	h.cpu.step();
	check(!h.cpu.halted() && h.cpu.pc() == 0x0020 && h.ram.m[0x2FFE] == 0x04, "an interrupt ends it");
}

/* Instructions decoded once, run again with other registers: (HL),
 * (IX+d), (HL+A), LDA HL,(IX+d), JP HL */
void test_decoded_once()
{
	Machine t;

	t.cpu.set_fixed(0x2000);
	t.ram.put(0, { 0x3E, 0x00, 0x30, 0x3A, 0x00, 0x20, 0x30, 0x04, 0x36, 0x00, 0xE2, 0x60, 0x92, 0x18, 0xFB,
	               0x2F, 0x00, 0x3C, 0x00, 0x20, 0x30, 0x04, 0x36, 0x00, 0xF0, 0x01, 0x60, 0x94, 0x18, 0xFA,
	               0x2F, 0x01, 0x3A, 0xFF, 0x1F, 0x36, 0x01, 0xF3, 0x28, 0x86, 0x6F, 0x05, 0xCE, 0xF9, 0x20,
	               0x2F, 0x02, 0x3C, 0x00, 0x20, 0x30, 0x02, 0xF4, 0x05, 0x3A, 0x94, 0x18, 0xFA, 0x08,
	               0x3A, 0x50, 0x00, 0x1A, 0x48, 0x00 });
	t.ram.put(0x48, { 0xEA, 0xC8 });
	t.ram.put(0x50, { 0x37, 0x03, 0x01, 0x3A, 0x60, 0x00, 0x1A, 0x48, 0x00 });
	t.ram.put(0x60, { 0x37, 0x04, 0x01, 0xC8, 0xFE });
	t.ram.put(0x2000, { 1, 2, 3, 4, 5 });
	for (int i = 0; i < 300 && t.cpu.pc() != 0x63; i++)
		t.cpu.step();
	check(t.ram.m[0xFF00] == 10, "ADD A,(HL) again: HL taken each time");
	check(t.ram.m[0xFF01] == 14, "ADD A,(IX+1) again: IX each time");
	check(t.ram.m[0xFF02] == 4, "LD B,(HL+A) again: A each time");
	check(t.cpu.de() == 0x2006, "LDA HL,(IX+5) again: IX each time");
	check(t.ram.m[0xFF03] == 1 && t.ram.m[0xFF04] == 1 && t.cpu.pc() == 0x63, "JP HL again: HL each time");

	/* CALL HL twice, HL changed by the first one it calls */
	Machine c;

	c.cpu.set_fixed(0x2000);
	c.ram.put(0, { 0x3E, 0x00, 0x30, 0x3A, 0x80, 0x00, 0xEA, 0xD8, 0x1A, 0x06, 0x00 });
	c.ram.put(0x80, { 0x37, 0x06, 0x01, 0x3A, 0x90, 0x00, 0x1E });
	c.ram.put(0x90, { 0x37, 0x07, 0x01, 0xC8, 0xFE });
	for (int i = 0; i < 100 && c.cpu.pc() != 0x93; i++)
		c.cpu.step();
	check(c.ram.m[0xFF06] == 1 && c.ram.m[0xFF07] == 1, "CALL HL again: HL each time");

	/* LD HL,1234 from 1FFE to 2000: past the end of the fixed memory, not
	 * kept */
	Machine e;

	e.cpu.set_fixed(0x2000);
	e.ram.put(0, { 0x1A, 0xFE, 0x1F });
	e.ram.put(0x1FFE, { 0x3A, 0x34, 0x12, 0xC8, 0xFB });
	e.cpu.step();
	e.cpu.step();
	const uint16_t first = e.cpu.hl();
	e.cpu.step();
	e.ram.m[0x2000] = 0x56;
	e.cpu.step();
	check(first == 0x1234 && e.cpu.hl() == 0x5634, "an instruction ending past the fixed memory: decoded each time");
}

void run_all()
{
	test_loads_and_addressing();
	test_arithmetic();
	test_rotations();
	test_jumps();
	test_interrupts();
}

} /* namespace */

int main()
{
	run_all();
	g_fixed = true;
	run_all();
	test_decoded_once();

	std::printf("%d checks, %d failures\n", g_checks, g_failures);
	return g_failures ? 1 : 0;
}
