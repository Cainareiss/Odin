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

/* Unit tests for the Odin SH-4 core: hand-assembled instructions run on a
 * small machine, with the expected registers, flags and memory taken from
 * the SH-4 software manual (section 9) and the SH7750 hardware manual.
 *
 * Build: g++ -std=c++20 -O2 -Wall -Wextra -Wpedantic -Wconversion -Wshadow \
 *            -ffp-contract=off -I src tests/sh4/sh4_tests.cpp src/odin/sh4.cpp \
 *            src/odin/sh4_ops.cpp src/odin/sh4_onchip.cpp -o sh4_tests
 * Use:   sh4_tests [-v]
 * Exit status is 0 when every check passes. */

#include "odin/sh4.hpp"

#include <algorithm>
#include <bit>
#include <cstdio>
#include <cstring>
#include <functional>
#include <initializer_list>
#include <string>
#include <vector>

namespace {

using odin::Sh4;
using odin::Sh4State;
namespace Source = odin::Sh4Source;

int g_checks, g_failures;
bool g_verbose;
const char *g_test = "";

void check(bool ok, const std::string &what)
{
	g_checks++;
	if (!ok) {
		g_failures++;
		std::printf("FAIL %s: %s\n", g_test, what.c_str());
	} else if (g_verbose) {
		std::printf("ok   %s: %s\n", g_test, what.c_str());
	}
}

void eq(const char *name, uint64_t got, uint64_t want)
{
	char text[128];
	std::snprintf(text, sizeof(text), "%s = %llX, expected %llX", name,
	              static_cast<unsigned long long>(got), static_cast<unsigned long long>(want));
	check(got == want, text);
}

constexpr uint32_t RAM = 0x0C000000;	/* 1 MB of plain memory */
constexpr uint32_t CODE = RAM + 0x1000;
constexpr uint32_t DATA = RAM + 0x8000;
constexpr uint32_t DEVICE = 0x04000000;	/* 4 KB the bus answers */
constexpr uint32_t SR_MD = 0x40000000, SR_RB = 0x20000000, SR_BL = 0x10000000;

uint32_t bits(float v) { return std::bit_cast<uint32_t>(v); }

/* A RAM, one device and port A around the CPU. */
class Machine : public odin::Sh4Bus {
public:
	struct Access {
		bool write;
		uint32_t addr, size;
		uint64_t value;
	};

	std::vector<uint8_t> ram;
	std::vector<uint8_t> onchip;
	std::vector<Access> device, unanswered_log;
	std::vector<uint32_t> seen;	/* what a test notes */
	uint64_t device_value = 0;
	uint16_t pins_in = 0, pins_out = 0;
	unsigned hooks = 0;
	std::function<void()> on_hook, on_device;
	Sh4 cpu;

	Machine() : ram(1u << 20, 0), onchip(0x10000, 0), cpu(*this, onchip, true, true, 5)
	{
		/* NOP wherever a program may go */
		for (uint32_t at = RAM; at < DATA; at += 2) {
			put16(at, 0x0009);
		}
		for (uint32_t page = 0; page < ram.size() / Sh4::PAGE_SIZE; page++) {
			uint8_t *host = &ram[page * Sh4::PAGE_SIZE];
			cpu.map_page(RAM / Sh4::PAGE_SIZE + page, host, host);
		}
		/* A running CPU in privileged mode, bank 0, interrupts open. */
		Sh4State st = state();
		st.run = odin::Sh4Run::Running;
		st.pc = CODE;
		st.sr = SR_MD;
		st.r[15] = RAM + 0xF000;
		cpu.load(st);
	}

	bool read(uint32_t addr, uint32_t size, uint64_t &value) override
	{
		if (addr >= (RAM & 0x1FFFFFFF) && addr + size <= (RAM & 0x1FFFFFFF) + ram.size()) {
			value = 0;
			std::memcpy(&value, &ram[addr - (RAM & 0x1FFFFFFF)], size);
			return true;
		}
		if ((addr & ~0xFFFu) != DEVICE) {
			return false;
		}
		device.push_back({false, addr, size, 0});
		value = device_value;
		if (on_device) {
			on_device();
		}
		return true;
	}
	bool write(uint32_t addr, uint32_t size, uint64_t value) override
	{
		if (addr >= (RAM & 0x1FFFFFFF) && addr + size <= (RAM & 0x1FFFFFFF) + ram.size()) {
			std::memcpy(&ram[addr - (RAM & 0x1FFFFFFF)], &value, size);
			return true;
		}
		if ((addr & ~0xFFFu) != DEVICE) {
			return false;
		}
		device.push_back({true, addr, size, value});
		if (on_device) {
			on_device();
		}
		return true;
	}
	uint16_t port_a_read() override { return pins_in; }
	void port_a_write(uint16_t pins) override { pins_out = pins; }
	void unanswered(bool is_write, uint32_t addr, uint32_t size, uint64_t value) override
	{
		unanswered_log.push_back({is_write, addr, size, value});
	}
	void hook() override
	{
		hooks++;
		if (on_hook) {
			on_hook();
		}
	}

	Sh4State state() const
	{
		Sh4State st;
		cpu.save(st);
		return st;
	}
	void code(uint32_t addr, std::initializer_list<uint16_t> words)
	{
		for (uint16_t w : words) {
			put16(addr, w);
			addr += 2;
		}
	}
	void put16(uint32_t addr, uint16_t v) { std::memcpy(&ram[addr - RAM], &v, 2); }
	void put32(uint32_t addr, uint32_t v) { std::memcpy(&ram[addr - RAM], &v, 4); }
	uint8_t get8(uint32_t addr) const { return ram[addr - RAM]; }
	uint16_t get16(uint32_t addr) const
	{
		uint16_t v;
		std::memcpy(&v, &ram[addr - RAM], 2);
		return v;
	}
	uint32_t get32(uint32_t addr) const
	{
		uint32_t v;
		std::memcpy(&v, &ram[addr - RAM], 4);
		return v;
	}
	void irlm()	/* ICR.IRLM: the IRL pins are four requests */
	{
		onchip[0xD000] |= 0x80;
	}
	uint32_t reg32(uint32_t addr) const	/* an on-chip register, as the image keeps it */
	{
		uint32_t at = ((addr >> 8) & 0xFF00) | (addr & 0xFF), v;
		std::memcpy(&v, &onchip[at], 4);
		return v;
	}
};

/* Runs `words` at CODE for `cycles` cycles from the state `set` prepares,
 * and gives the state after. */
Sh4State run(Machine &m, std::initializer_list<uint16_t> words, int32_t cycles,
             const std::function<void(Sh4State &)> &set = nullptr)
{
	m.code(CODE, words);
	if (set) {
		Sh4State st = m.state();
		set(st);
		m.cpu.load(st);
	}
	m.cpu.run(cycles);
	return m.state();
}

constexpr uint16_t NOP = 0x0009;

void test_data_transfer()
{
	g_test = "data transfer";
	{
		Machine m;
		/* mov #-2,r1; mov r1,r2; mov.w @(2,pc),r3; mov.l @(4,pc),r4; nop; .word 8001; .long 12345678 */
		m.code(CODE, {0xE1FE, 0x6213, 0x9301, 0xD401, NOP, 0x8001, 0x5678, 0x1234});
		m.cpu.run(4);
		Sh4State st = m.state();
		eq("mov #imm sign-extends", st.r[1], 0xFFFFFFFE);
		eq("mov Rm,Rn", st.r[2], 0xFFFFFFFE);
		eq("mov.w @(disp,PC) sign-extends", st.r[3], 0xFFFF8001);
		eq("mov.l @(disp,PC) from the aligned PC", st.r[4], 0x12345678);
		eq("pc", st.pc, CODE + 8);
	}
	{
		Machine m;
		m.put32(DATA, 0x80F17F02);
		m.put32(DATA + 4, 0xCAFEBABE);
		/* mov.b @r8,r0; mov.w @r8,r1; mov.l @r8,r2; mov.b @r8+,r3; mov.w @r8+,r4; add #1,r8; mov.l @r8+,r5 */
		Sh4State st = run(m, {0x6080, 0x6181, 0x6282, 0x6384, 0x6485, 0x7801, 0x6586}, 7,
		                  [](Sh4State &s) { s.r[8] = DATA; });
		eq("mov.b @Rm sign-extends", st.r[0], 0x00000002);
		eq("mov.w @Rm sign-extends", st.r[1], 0x00007F02);
		eq("mov.l @Rm", st.r[2], 0x80F17F02);
		eq("mov.b @Rm+", st.r[3], 0x00000002);
		eq("mov.w @Rm+ at an odd address goes through the bus", st.r[4], 0xFFFFF17F);
		eq("mov.l @Rm+", st.r[5], 0xCAFEBABE);
		eq("post-increments", st.r[8], DATA + 8);
	}
	{
		Machine m;
		/* mov.l r1,@-r15; mov.w r1,@-r15; mov.b r1,@-r15; mov.l @r15+,r15 */
		Sh4State st = run(m, {0x2F16, 0x2F15, 0x2F14, 0x6FF6}, 4, [](Sh4State &s) {
			s.r[1] = 0xA1B2C3D4;
			s.r[15] = DATA + 0x100;
		});
		eq("mov.l Rm,@-Rn", m.get32(DATA + 0xFC), 0xA1B2C3D4);
		eq("mov.w Rm,@-Rn", m.get16(DATA + 0xFA), 0xC3D4);
		eq("mov.b Rm,@-Rn", m.get8(DATA + 0xF9), 0xD4);
		/* Rn takes the value loaded, not the incremented address. */
		eq("mov.l @Rn+,Rn", st.r[15], m.get32(DATA + 0xF9));
	}
	{
		Machine m;
		m.put32(DATA + 0x24, 0x11223344);
		/* mov.l r1,@(8,r8); mov.l @(36,r8),r2; mov.b r0,@(3,r8); mov.w r0,@(6,r8); mov.b @(3,r8),r0; mov.w @(6,r8),r0 */
		Sh4State st = run(m, {0x1812, 0x5289, 0x8083, 0x8183, 0x8483}, 5, [](Sh4State &s) {
			s.r[0] = 0x000081F0;
			s.r[1] = 0xDEADBEEF;
			s.r[8] = DATA;
		});
		eq("mov.l Rm,@(disp,Rn)", m.get32(DATA + 8), 0xDEADBEEF);
		eq("mov.l @(disp,Rm),Rn", st.r[2], 0x11223344);
		eq("mov.b R0,@(disp,Rn)", m.get8(DATA + 3), 0xF0);
		eq("mov.w R0,@(disp,Rn)", m.get16(DATA + 6), 0x81F0);
		eq("mov.b @(disp,Rm),R0 sign-extends", st.r[0], 0xFFFFFFF0);
	}
	{
		Machine m;
		m.put32(DATA + 0x40, 0x89ABCDEF);
		/* mov.l @(r0,r8),r1; mov.b r1,@(r0,r9); mov.l @(16,gbr),r0; mov.b r0,@(1,gbr); mova @(4,pc),r0; movt r3 */
		Sh4State st = run(m, {0x018E, 0x0914, 0xC610, 0xC001, 0xC701, 0x0329}, 6, [](Sh4State &s) {
			s.r[0] = 0x40;
			s.r[8] = DATA;
			s.r[9] = DATA + 0x100;
			s.gbr = DATA;
			s.sr |= 1;
		});
		eq("mov.l @(R0,Rm),Rn", st.r[1], 0x89ABCDEF);
		eq("mov.b Rm,@(R0,Rn)", m.get8(DATA + 0x140), 0xEF);
		eq("mov.b R0,@(disp,GBR)", m.get8(DATA + 1), 0xEF);
		eq("mova", st.r[0], ((CODE + 8 + 4) & ~3u) + 4);
		eq("movt", st.r[3], 1);
	}
	{
		Machine m;
		/* swap.b r1,r2; swap.w r1,r3; xtrct r1,r4 */
		Sh4State st = run(m, {0x6218, 0x6319, 0x241D}, 3, [](Sh4State &s) {
			s.r[1] = 0x12345678;
			s.r[4] = 0xAABBCCDD;
		});
		eq("swap.b", st.r[2], 0x12347856);
		eq("swap.w", st.r[3], 0x56781234);
		eq("xtrct", st.r[4], 0x5678AABB);
	}
}

void test_arithmetic()
{
	g_test = "arithmetic";
	{
		Machine m;
		/* add r1,r2; add #-1,r3; addc r1,r4; addc r5,r5; addv r6,r7 */
		Sh4State st = run(m, {0x321C, 0x73FF, 0x341E, 0x355E, 0x376F}, 5, [](Sh4State &s) {
			s.r[1] = 0xFFFFFFFF;
			s.r[2] = 2;
			s.r[3] = 0;
			s.r[4] = 1;
			s.r[5] = 0x10;
			s.r[6] = 0x7FFFFFFF;
			s.r[7] = 1;
		});
		eq("add", st.r[2], 1);
		eq("add #imm", st.r[3], 0xFFFFFFFF);
		eq("addc", st.r[4], 0);
		eq("addc takes the carry in", st.r[5], 0x21);
		eq("addv", st.r[7], 0x80000000);
		eq("addv sets T on overflow", st.sr & 1, 1);
	}
	{
		Machine m;
		/* sub r1,r2; subc r1,r3; subv r4,r5; neg r1,r6; clrt; negc r1,r7 */
		Sh4State st = run(m, {0x3218, 0x331A, 0x354B, 0x661B, 0x0008, 0x671A}, 6, [](Sh4State &s) {
			s.r[1] = 1;
			s.r[2] = 0;
			s.r[3] = 0;
			s.r[4] = 1;
			s.r[5] = 0x80000000;
		});
		eq("sub", st.r[2], 0xFFFFFFFF);
		eq("subc", st.r[3], 0xFFFFFFFF);
		eq("subv", st.r[5], 0x7FFFFFFF);
		eq("neg", st.r[6], 0xFFFFFFFF);
		eq("negc", st.r[7], 0xFFFFFFFF);
		eq("negc sets the borrow", st.sr & 1, 1);
	}
	{
		/* Each compare, then movt into r8..r15 */
		Machine m;
		Sh4State st = run(m, {0x3120, 0x0829, 0x3122, 0x0929, 0x3123, 0x0A29, 0x3126, 0x0B29,
		                      0x3127, 0x0C29, 0x4111, 0x0D29, 0x4115, 0x0E29, 0x88FF, 0x0F29}, 16,
		                  [](Sh4State &s) {
			s.r[0] = 0xFFFFFFFF;
			s.r[1] = 0xFFFFFFFF;	/* -1 */
			s.r[2] = 1;
		});
		eq("cmp/eq", st.r[8], 0);
		eq("cmp/hs unsigned", st.r[9], 1);
		eq("cmp/ge signed", st.r[10], 0);
		eq("cmp/hi unsigned", st.r[11], 1);
		eq("cmp/gt signed", st.r[12], 0);
		eq("cmp/pz", st.r[13], 0);
		eq("cmp/pl", st.r[14], 0);
		eq("cmp/eq #imm sign-extends", st.r[15], 1);
	}
	{
		Machine m;
		/* cmp/str r1,r2; movt r3; dt r4; movt r5; dt r4; movt r6 */
		Sh4State st = run(m, {0x221C, 0x0329, 0x4410, 0x0529, 0x4410, 0x0629}, 6, [](Sh4State &s) {
			s.r[1] = 0x11223344;
			s.r[2] = 0xAA22BBCC;
			s.r[4] = 2;
		});
		eq("cmp/str finds an equal byte", st.r[3], 1);
		eq("dt not yet zero", st.r[5], 0);
		eq("dt at zero", st.r[6], 1);
	}
	{
		Machine m;
		/* exts.b r1,r2; exts.w r1,r3; extu.b r1,r4; extu.w r1,r5 */
		Sh4State st = run(m, {0x621E, 0x631F, 0x641C, 0x651D}, 4, [](Sh4State &s) { s.r[1] = 0x1234F680; });
		eq("exts.b", st.r[2], 0xFFFFFF80);
		eq("exts.w", st.r[3], 0xFFFFF680);
		eq("extu.b", st.r[4], 0x80);
		eq("extu.w", st.r[5], 0xF680);
	}
	{
		Machine m;
		/* mul.l r1,r2; sts macl,r3; muls.w r1,r2; sts macl,r4; mulu.w r1,r2; sts macl,r5;
		 * dmuls.l r1,r2; sts mach,r6; dmulu.l r1,r2; sts mach,r7 */
		Sh4State st = run(m, {0x0217, 0x031A, 0x221F, 0x041A, 0x221E, 0x051A, 0x321D, 0x060A, 0x3215, 0x070A}, 10,
		                  [](Sh4State &s) {
			s.r[1] = 0xFFFFFFFE;	/* -2 */
			s.r[2] = 0x00010003;
		});
		eq("mul.l", st.r[3], 0xFFFDFFFA);
		eq("muls.w", st.r[4], 0xFFFFFFFA);
		eq("mulu.w", st.r[5], 0x0002FFFA);
		eq("dmuls.l high", st.r[6], 0xFFFFFFFF);
		eq("dmulu.l high", st.r[7], 0x00010002);
	}
	{
		/* 32 by 16 unsigned division, software manual 9.24 example 1: r1 / r0 -> r1 */
		Machine m;
		std::vector<uint16_t> words = {0x4028, 0x0019};	/* shll16 r0; div0u */
		for (int i = 0; i < 16; i++) {
			words.push_back(0x3104);	/* div1 r0,r1 */
		}
		words.push_back(0x4124);	/* rotcl r1 */
		words.push_back(0x611D);	/* extu.w r1,r1 */
		uint32_t at = CODE;
		for (uint16_t w : words) {
			m.put16(at, w);
			at += 2;
		}
		Sh4State st = m.state();
		st.r[0] = 1000;
		st.r[1] = 1234567;
		m.cpu.load(st);
		m.cpu.run(static_cast<int32_t>(words.size()));
		eq("div1, 1234567 / 1000", m.state().r[1], 1234);
	}
	{
		/* 32 by 32 signed division, example 3: r2 / r0 -> r2, with r1 = sign extension */
		Machine m;
		/* mov r2,r3; rotcl r3; subc r1,r1; mov #0,r3; subc r3,r2; div0s r0,r1 */
		std::vector<uint16_t> words = {0x6323, 0x4324, 0x311A, 0xE300, 0x323A, 0x2107};
		for (int i = 0; i < 32; i++) {
			words.push_back(0x4224);	/* rotcl r2 */
			words.push_back(0x3104);	/* div1 r0,r1 */
		}
		words.push_back(0x4224);	/* rotcl r2 */
		words.push_back(0x323E);	/* addc r3,r2 */
		uint32_t at = CODE;
		for (uint16_t w : words) {
			m.put16(at, w);
			at += 2;
		}
		Sh4State st = m.state();
		st.r[0] = 7;
		st.r[2] = static_cast<uint32_t>(-100);
		m.cpu.load(st);
		m.cpu.run(static_cast<int32_t>(words.size()));
		eq("div1 signed, -100 / 7", m.state().r[2], static_cast<uint32_t>(-14));
	}
	{
		Machine m;
		m.put32(DATA, static_cast<uint32_t>(-3));
		m.put32(DATA + 4, 5);
		m.put16(DATA + 8, 0x7FFF);
		m.put16(DATA + 10, 0x7FFF);
		/* mac.l @r8+,@r9+; mac.w @r10+,@r11+ */
		Sh4State st = run(m, {0x098F}, 1, [](Sh4State &s) {
			s.r[8] = DATA + 4;
			s.r[9] = DATA;
			s.mach = 0;
			s.macl = 20;
		});
		eq("mac.l low", st.macl, 5);
		eq("mac.l high", st.mach, 0);
		eq("mac.l post-increments", st.r[8] + st.r[9], 2 * DATA + 12);

		/* sets; mac.w @r10+,@r11+: 7FFF * 7FFF = 3FFF0001 overflows MACL, which saturates */
		Machine w;
		w.put16(DATA + 8, 0x7FFF);
		w.put16(DATA + 10, 0x7FFF);
		st = run(w, {0x0058, 0x4BAF}, 2, [](Sh4State &s) {
			s.r[10] = DATA + 8;
			s.r[11] = DATA + 10;
			s.mach = 0;
			s.macl = 0x7FFFFFF0;
		});
		eq("mac.w with S saturates MACL", st.macl, 0x7FFFFFFF);

		/* without S the carry goes to MACH */
		Machine c;
		c.put16(DATA + 8, 0xFFFF);
		c.put16(DATA + 10, 0x0002);
		st = run(c, {0x4BAF}, 1, [](Sh4State &s) {
			s.r[10] = DATA + 8;
			s.r[11] = DATA + 10;
			s.mach = 5;
			s.macl = 1;
		});
		eq("mac.w, -1 * 2 + 1, low", st.macl, 0xFFFFFFFF);
		eq("mac.w, high", st.mach, 4);
	}
}

void test_logic_and_shifts()
{
	g_test = "logic and shifts";
	{
		Machine m;
		m.ram[DATA + 0x10 - RAM] = 0x5A;
		/* and r1,r2; or r1,r3; xor r1,r4; not r1,r5; tst r1,r6; movt r7; and #0x0F,r0; or.b #0x81,@(r0,gbr) */
		Sh4State st = run(m, {0x2219, 0x231B, 0x241A, 0x6517, 0x2618, 0x0729, 0xC90F, 0xCF81}, 8, [](Sh4State &s) {
			s.r[0] = 0xF0;
			s.r[1] = 0x0FF00FF0;
			s.r[2] = 0xFFFF0000;
			s.r[3] = 0x0000000F;
			s.r[4] = 0xFFFFFFFF;
			s.r[6] = 0xF00FF00F;
			s.gbr = DATA + 0x10;
		});
		eq("and", st.r[2], 0x0FF00000);
		eq("or", st.r[3], 0x0FF00FFF);
		eq("xor", st.r[4], 0xF00FF00F);
		eq("not", st.r[5], 0xF00FF00F);
		eq("tst sets T when the AND is zero", st.r[7], 1);
		eq("and #imm", st.r[0], 0);
		eq("or.b #imm,@(R0,GBR)", m.get8(DATA + 0x10), 0xDB);
	}
	{
		Machine m;
		m.ram[DATA - RAM] = 0;
		/* tas.b @r8; movt r1; tas.b @r8; movt r2 */
		Sh4State st = run(m, {0x481B, 0x0129, 0x481B, 0x0229}, 4, [](Sh4State &s) { s.r[8] = DATA; });
		eq("tas.b on zero", st.r[1], 1);
		eq("tas.b sets bit 7", m.get8(DATA), 0x80);
		eq("tas.b on non-zero", st.r[2], 0);
	}
	{
		Machine m;
		/* rotl r1; rotr r2; sett; rotcl r3; rotcr r3; shll r4; shar r5; shlr r6; shll2 r7; shll8 r8; shlr16 r9 */
		Sh4State st = run(m, {0x4104, 0x4205, 0x0018, 0x4324, 0x4325, 0x4400, 0x4521, 0x4601, 0x4708, 0x4818, 0x4929},
		                  11, [](Sh4State &s) {
			s.r[1] = 0x80000001;
			s.r[2] = 0x00000003;
			s.r[3] = 0x40000000;
			s.r[4] = 0xC0000000;
			s.r[5] = 0x80000002;
			s.r[6] = 0x80000003;
			s.r[7] = 0x40000001;
			s.r[8] = 0x00ABCDEF;
			s.r[9] = 0x12345678;
		});
		eq("rotl", st.r[1], 0x00000003);
		eq("rotr", st.r[2], 0x80000001);
		eq("rotcl then rotcr gives the value back", st.r[3], 0x40000000);
		eq("shll", st.r[4], 0x80000000);
		eq("shar keeps the sign", st.r[5], 0xC0000001);
		eq("shlr", st.r[6], 0x40000001);
		eq("shlr leaves the bit shifted out in T", st.sr & 1, 1);
		eq("shll2", st.r[7], 0x00000004);
		eq("shll8", st.r[8], 0xABCDEF00);
		eq("shlr16", st.r[9], 0x00001234);
	}
	{
		Machine m;
		/* shad r1,r2; shad r3,r4; shad r5,r6; shld r1,r7; shld r3,r8; shld r5,r9 */
		Sh4State st = run(m, {0x421C, 0x443C, 0x465C, 0x471D, 0x483D, 0x495D}, 6, [](Sh4State &s) {
			s.r[1] = 4;
			s.r[3] = static_cast<uint32_t>(-4);
			s.r[5] = static_cast<uint32_t>(-32);
			s.r[2] = s.r[4] = s.r[6] = 0x80000010;
			s.r[7] = s.r[8] = s.r[9] = 0x80000010;
		});
		eq("shad left", st.r[2], 0x00000100);
		eq("shad right is arithmetic", st.r[4], 0xF8000001);
		eq("shad right by 32 leaves the sign", st.r[6], 0xFFFFFFFF);
		eq("shld left", st.r[7], 0x00000100);
		eq("shld right is logical", st.r[8], 0x08000001);
		eq("shld right by 32 leaves zero", st.r[9], 0);
	}
}

void test_branches()
{
	g_test = "branches";
	{
		Machine m;
		/* sett; bt +2 (to CODE+10); mov #1,r1; mov #2,r1; nop; mov #3,r2 */
		Sh4State st = run(m, {0x0018, 0x8902, 0xE101, 0xE102, NOP, 0xE203}, 3);
		eq("bt taken skips", st.r[1], 0);
		eq("bt target runs", st.r[2], 3);
		eq("pc after bt", st.pc, CODE + 12);
	}
	{
		Machine m;
		/* clrt; bt/s +2; mov #1,r1; mov #2,r2; sett; bf/s +1 ... */
		Sh4State st = run(m, {0x0008, 0x8D02, 0xE101, 0xE202, NOP}, 4);
		eq("bt/s not taken runs the next instruction once", st.r[1], 1);
		eq("and goes on", st.r[2], 2);
	}
	{
		Machine m;
		/* sett; bt/s to CODE+12; mov #5,r1 (slot); mov #1,r2; nop; nop; mov #7,r3 */
		Sh4State st = run(m, {0x0018, 0x8D03, 0xE105, 0xE201, NOP, NOP, 0xE307}, 4);
		eq("bt/s runs its delay slot", st.r[1], 5);
		eq("bt/s skips to the target", st.r[2], 0);
		eq("bt/s target", st.r[3], 7);
	}
	{
		Machine m;
		/* bsr +3 (to CODE+10); mov #1,r1 (slot); mov #9,r9; nop; nop; sts pr,r2; rts; mov #4,r3 (slot) */
		Sh4State st = run(m, {0xB003, 0xE101, 0xE909, NOP, NOP, 0x022A, 0x000B, 0xE304}, 5);
		eq("bsr delay slot", st.r[1], 1);
		eq("bsr sets PR to the instruction after the slot", st.r[2], CODE + 4);
		eq("rts delay slot", st.r[3], 4);
		eq("rts returns to PR", st.pc, CODE + 4);
	}
	{
		Machine m;
		/* jsr @r8; sts pr,r1 (slot: sees the new PR); ... at r8: jmp @r9; mov r8,r9 (slot: the target is already taken) */
		m.code(CODE + 0x40, {0x492B, 0x6983});
		Sh4State st = run(m, {0x480B, 0x012A}, 4, [](Sh4State &s) {
			s.r[8] = CODE + 0x40;
			s.r[9] = CODE + 0x80;
		});
		eq("jsr: the slot sees PR", st.r[1], CODE + 4);
		eq("jmp takes its target before the slot", st.pc, CODE + 0x80);
		eq("the slot of jmp ran", st.r[9], CODE + 0x40);
	}
	{
		Machine m;
		/* braf r1; nop / bsrf r2; nop */
		Sh4State st = run(m, {0x0123, NOP}, 2, [](Sh4State &s) { s.r[1] = 0x20; });
		eq("braf", st.pc, CODE + 4 + 0x20);
		Machine n;
		st = run(n, {0x0203, NOP}, 2, [](Sh4State &s) { s.r[2] = 0x30; });
		eq("bsrf", st.pc, CODE + 4 + 0x30);
		eq("bsrf sets PR", st.pr, CODE + 4);
	}
	{
		/* A branch and its delay slot are never split: one cycle runs both. */
		Machine m;
		m.code(CODE, {0xA002, 0xE101, NOP, NOP, NOP, NOP});	/* bra CODE+8; mov #1,r1 */
		int32_t over = m.cpu.run(1);
		Sh4State st = m.state();
		eq("bra with one cycle left runs its slot", st.r[1], 1);
		eq("and reports the cycle run past the end", static_cast<uint32_t>(over), 1);
		eq("pc at the target", st.pc, CODE + 8);
		eq("the state keeps the cycle owed", static_cast<uint32_t>(st.over), 1);
		/* The next slice has one cycle less: two cycles asked, one instruction run. */
		m.cpu.load(st);
		over = m.cpu.run(2);
		eq("the cycle run over is taken from the next slice", m.state().pc, CODE + 10);
		eq("which ends on time", static_cast<uint32_t>(over), 0);
	}
	{
		/* The same program by slices of every length gives the same state. */
		const std::initializer_list<uint16_t> loop = {
			0xE000,		/* mov #0,r0 */
			0xE10A,		/* mov #10,r1 */
			0x7003,		/* add #3,r0 */
			0x2F06,		/* mov.l r0,@-r15 */
			0x4110,		/* dt r1 */
			0x8FFB,		/* bf/s back to add */
			0x7201,		/* add #1,r2 (slot) */
			0x6803,		/* mov r0,r8 */
			NOP, NOP, NOP, NOP,
		};
		Machine whole;
		whole.code(CODE, loop);
		whole.cpu.run(56);
		Sh4State want = whole.state();
		for (int32_t slice = 1; slice <= 7; slice++) {
			Machine m;
			m.code(CODE, loop);
			int32_t left = 56;
			while (left > 0) {
				/* a slice may run one cycle past its end: the core takes it
				 * from the next one */
				int32_t now = slice < left ? slice : left;
				m.cpu.run(now);
				left -= now;
			}
			Sh4State st = m.state();
			bool same = st.pc == want.pc && st.r[0] == want.r[0] && st.r[1] == want.r[1]
				&& st.r[2] == want.r[2] && st.r[8] == want.r[8] && st.r[15] == want.r[15]
				&& m.ram == whole.ram;
			check(same, "slices of " + std::to_string(slice) + " cycles give the state of one slice of 56");
		}
		eq("nine delay slots and the instruction once more", want.r[2], 10);
		eq("the loop left its sum", want.r[8], 30);
	}
}

void test_system()
{
	g_test = "system";
	{
		Machine m;
		/* mov #5,r0 (bank 0); ldc r1,sr (to bank 1); mov #7,r0; stc r0_bank,r2; ldc r3,r4_bank; stc sr,r5 */
		Sh4State st = run(m, {0xE005, 0x410E, 0xE007, 0x0282, 0x43CE, 0x0502}, 6, [](Sh4State &s) {
			s.r[1] = SR_MD | SR_RB | 0x000000F1;
			s.rbank[3] = 0x44;	/* R3 of bank 1 */
		});
		eq("ldc Rm,SR switches the bank", st.r[0], 7);
		eq("the other bank keeps its R0", st.rbank[0], 5);
		eq("stc Rm_BANK,Rn", st.r[2], 5);
		eq("ldc Rm,Rn_BANK", st.rbank[4], 0x44);
		eq("stc SR,Rn with T", st.r[5], SR_MD | SR_RB | 0xF1);
	}
	{
		Machine m;
		/* ldc r1,gbr; ldc r2,vbr; ldc r3,ssr; ldc r4,spc; ldc r5,dbr; stc gbr,r8; stc vbr,r9; stc ssr,r10;
		 * stc spc,r11; stc dbr,r12; stc sgr,r13 */
		Sh4State st = run(m, {0x411E, 0x422E, 0x433E, 0x444E, 0x45FA, 0x0812, 0x0922, 0x0A32, 0x0B42, 0x0CFA, 0x0D3A},
		                  11, [](Sh4State &s) {
			for (uint32_t i = 1; i <= 5; i++) {
				s.r[i] = 0x1000 * i;
			}
			s.sgr = 0x6000;
		});
		eq("gbr", st.r[8], 0x1000);
		eq("vbr", st.r[9], 0x2000);
		eq("ssr", st.r[10], 0x3000);
		eq("spc (stc at 0x0n42)", st.r[11], 0x4000);
		eq("dbr", st.r[12], 0x5000);
		eq("sgr", st.r[13], 0x6000);
	}
	{
		Machine m;
		/* sts.l pr,@-r15; stc.l sr,@-r15; lds.l @r15+,macl; lds.l @r15+,mach; clrmac happens later */
		Sh4State st = run(m, {0x4F22, 0x4F03, 0x4F16, 0x4F06, 0x0048, 0x0058, 0x0002}, 7, [](Sh4State &s) {
			s.pr = 0xCAFE0000;
			s.sr = SR_MD | 0x101;
			s.r[15] = DATA + 0x80;
		});
		eq("stc.l SR then lds.l MACL", st.macl, SR_MD | 0x101);
		eq("sts.l PR then lds.l MACH", st.mach, 0xCAFE0000);
		eq("the stack is back", st.r[15], DATA + 0x80);
		eq("clrs then sets", st.r[0] & 2, 2);
	}
	{
		Machine m;
		/* rte; mov #1,r0 (slot, already in the restored bank) */
		Sh4State st = run(m, {0x002B, 0xE001}, 2, [](Sh4State &s) {
			s.sr = SR_MD | SR_RB | SR_BL;
			s.ssr = SR_MD | 0x30;
			s.spc = CODE + 0x100;
			s.r[0] = 0x77;
		});
		eq("rte restores SR", st.sr, SR_MD | 0x30);
		eq("rte jumps to SPC", st.pc, CODE + 0x100);
		eq("the slot of rte runs with the restored bank", st.r[0], 1);
		eq("the bank left behind keeps its register", st.rbank[0], 0x77);
	}
}

void test_fpu()
{
	g_test = "fpu";
	{
		Machine m;
		m.put32(DATA, bits(1.5f));
		m.put32(DATA + 8, bits(-2.0f));
		/* fmov.s @r8,fr1; fmov.s @(r0,r8),fr2; fmov fr1,fr3; fmov.s fr2,@-r9; fmov.s @r8+,fr4; fldi0 fr5;
		 * fldi1 fr6; flds fr6,fpul; sts fpul,r1; lds r2,fpul; fsts fpul,fr7 */
		Sh4State st = run(m, {0xF188, 0xF286, 0xF31C, 0xF92B, 0xF489, 0xF58D, 0xF69D, 0xF61D, 0x015A, 0x425A, 0xF70D},
		                  11, [](Sh4State &s) {
			s.r[0] = 8;
			s.r[2] = 0x40490FDB;
			s.r[8] = DATA;
			s.r[9] = DATA + 0x20;
			s.fpscr = 0x00040001;
		});
		eq("fmov.s @Rm,FRn", st.fr[1], bits(1.5f));
		eq("fmov.s @(R0,Rm),FRn", st.fr[2], bits(-2.0f));
		eq("fmov FRm,FRn", st.fr[3], bits(1.5f));
		eq("fmov.s FRm,@-Rn", m.get32(DATA + 0x1C), bits(-2.0f));
		eq("fmov.s @Rm+,FRn increments", st.r[8], DATA + 4);
		eq("fldi0", st.fr[5], 0);
		eq("fldi1", st.fr[6], bits(1.0f));
		eq("flds then sts fpul", st.r[1], bits(1.0f));
		eq("lds fpul then fsts", st.fr[7], 0x40490FDB);
	}
	{
		Machine m;
		/* fadd fr1,fr2; fsub fr1,fr3; fmul fr1,fr4; fdiv fr1,fr5; fneg fr6; fabs fr7; fsqrt fr8; fmac fr0,fr1,fr9 */
		Sh4State st = run(m, {0xF210, 0xF311, 0xF412, 0xF513, 0xF64D, 0xF75D, 0xF86D, 0xF91E}, 8, [](Sh4State &s) {
			s.fr[0] = bits(3.0f);
			s.fr[1] = bits(2.0f);
			s.fr[2] = bits(0.5f);
			s.fr[3] = bits(5.0f);
			s.fr[4] = bits(-1.5f);
			s.fr[5] = bits(7.0f);
			s.fr[6] = bits(4.0f);
			s.fr[7] = bits(-4.0f);
			s.fr[8] = bits(9.0f);
			s.fr[9] = bits(1.0f);
		});
		eq("fadd", st.fr[2], bits(2.5f));
		eq("fsub", st.fr[3], bits(3.0f));
		eq("fmul", st.fr[4], bits(-3.0f));
		eq("fdiv", st.fr[5], bits(3.5f));
		eq("fneg", st.fr[6], bits(-4.0f));
		eq("fabs", st.fr[7], bits(4.0f));
		eq("fsqrt", st.fr[8], bits(3.0f));
		eq("fmac, FR0 * FRm + FRn", st.fr[9], bits(7.0f));
	}
	{
		Machine m;
		/* lds r1,fpul; float fpul,fr2; ftrc fr3,fpul; sts fpul,r4; fcmp/eq fr2,fr2; movt r5; fcmp/gt fr3,fr2; movt r6 */
		Sh4State st = run(m, {0x415A, 0xF22D, 0xF33D, 0x045A, 0xF224, 0x0529, 0xF235, 0x0629}, 8, [](Sh4State &s) {
			s.r[1] = static_cast<uint32_t>(-7);
			s.fr[3] = bits(-2.75f);
		});
		eq("float", st.fr[2], bits(-7.0f));
		eq("ftrc truncates toward zero", st.r[4], static_cast<uint32_t>(-2));
		eq("fcmp/eq", st.r[5], 1);
		eq("fcmp/gt, FRn > FRm", st.r[6], 0);
	}
	{
		/* Double precision: FPSCR.PR. DR0 = 1.5, DR2 = 2.25 */
		Machine m;
		/* fadd dr0,dr2; fcnvds dr2,fpul; sts fpul,r1; lds r2,fpul; fcnvsd fpul,dr4; ftrc dr2,fpul; sts fpul,r3 */
		Sh4State st = run(m, {0xF200, 0xF2BD, 0x015A, 0x425A, 0xF4AD, 0xF23D, 0x035A}, 7, [](Sh4State &s) {
			s.fpscr = 0x000C0001;
			s.fr[0] = 0x3FF80000;
			s.fr[1] = 0;
			s.fr[2] = 0x40020000;
			s.fr[3] = 0;
			s.r[2] = bits(0.25f);
		});
		eq("fadd double, high word", st.fr[2], 0x400E0000);	/* 3.75 */
		eq("fadd double, low word", st.fr[3], 0);
		eq("fcnvds", st.r[1], bits(3.75f));
		eq("fcnvsd high word", st.fr[4], 0x3FD00000);	/* 0.25 */
		eq("ftrc double", st.r[3], 3);
	}
	{
		/* FPSCR.SZ: pairs. fschg; fmov @r8,dr0; fmov dr0,xd2; fmov xd2,@r9; frchg; fschg */
		Machine m;
		m.put32(DATA, 0x11111111);
		m.put32(DATA + 4, 0x22222222);
		Sh4State st = run(m, {0xF3FD, 0xF088, 0xF30C, 0xF93A, 0xFBFD, 0xF3FD}, 6, [](Sh4State &s) {
			s.r[8] = DATA;
			s.r[9] = DATA + 0x40;
			s.fpscr = 0x00040001;
		});
		eq("fmov @Rm,DRn low word to FRn", m.get32(DATA + 0x40), 0x11111111);
		eq("fmov XDm,@Rn", m.get32(DATA + 0x44), 0x22222222);
		/* after frchg the pair moved to XD2 is in FR2, FR3 */
		eq("frchg swaps the banks", st.fr[2], 0x11111111);
		eq("and the other half", st.fr[3], 0x22222222);
		eq("FPSCR after fschg, frchg, fschg", st.fpscr, 0x00240001);
	}
	{
		/* fipr fv4,fv0; ftrv xmtrx,fv8 with XMTRX = twice the identity */
		Machine m;
		Sh4State st = run(m, {0xF1ED, 0xF9FD}, 2, [](Sh4State &s) {
			for (uint32_t i = 0; i < 4; i++) {
				s.fr[i] = bits(static_cast<float>(i + 1));	/* 1 2 3 4 */
				s.fr[4 + i] = bits(static_cast<float>(5 + i));	/* 5 6 7 8 */
				s.fr[8 + i] = bits(static_cast<float>(i) - 1.0f);	/* -1 0 1 2 */
				s.xf[i * 5] = bits(2.0f);
			}
		});
		eq("fipr", st.fr[3], bits(70.0f));
		eq("ftrv x", st.fr[8], bits(-2.0f));
		eq("ftrv w", st.fr[11], bits(4.0f));
	}
	{
		/* fsca fpul,dr2: a quarter turn; fsrra fr4 */
		Machine m;
		Sh4State st = run(m, {0xF2FD, 0xF47D}, 2, [](Sh4State &s) {
			s.fpul = 0x4000;
			s.fr[4] = bits(16.0f);
		});
		eq("fsca sine of a quarter turn", st.fr[2], bits(1.0f));
		check(std::bit_cast<float>(st.fr[3]) < 1e-6f && std::bit_cast<float>(st.fr[3]) > -1e-6f,
		      "fsca cosine of a quarter turn is about zero");
		eq("fsrra", st.fr[4], bits(0.25f));
	}
}

void test_interrupts()
{
	g_test = "interrupts";
	{
		Machine m;
		m.irlm();
		m.code(CODE, {NOP, NOP, NOP, NOP});
		m.code(RAM + 0x600, {0xE155, NOP, NOP});	/* handler: mov #0x55,r1 */
		Sh4State st = m.state();
		st.vbr = RAM;
		st.sr = SR_MD | 0x10;	/* mask 1 */
		st.r[15] = 0x1234;
		m.cpu.load(st);
		m.cpu.run(1);
		m.cpu.set_irq(Source::IRL2, true);	/* priority 7 */
		m.cpu.run(1);
		st = m.state();
		eq("the handler runs", st.r[1], 0x55);
		eq("SPC is the instruction not run", st.spc, CODE + 2);
		eq("SSR", st.ssr, SR_MD | 0x10);
		eq("SGR", st.sgr, 0x1234);
		eq("SR has BL, RB and MD", st.sr, SR_MD | SR_RB | SR_BL | 0x10);
		eq("INTEVT", m.reg32(0xFF000028), 0x300);
		eq("pc", st.pc, RAM + 0x602);
	}
	{
		Machine m;
		m.irlm();
		m.code(CODE, {NOP, NOP, NOP, NOP});
		Sh4State st = m.state();
		st.sr = SR_MD | 0x70;	/* mask 7: IRL2 (7) waits, IRL0 (13) passes */
		st.vbr = RAM;
		m.cpu.load(st);
		m.cpu.set_irq(Source::IRL2, true);
		m.cpu.run(1);
		eq("an interrupt at the level of the mask waits", m.state().pc, CODE + 2);
		m.cpu.set_irq(Source::IRL0, true);
		m.cpu.run(1);
		eq("a higher one is taken", m.state().spc, CODE + 2);
		eq("with its own code", m.reg32(0xFF000028), 0x240);
	}
	{
		Machine m;
		m.irlm();
		m.code(CODE, {NOP, NOP, NOP, NOP});
		Sh4State st = m.state();
		st.sr = SR_MD | SR_BL;
		st.vbr = RAM;
		m.cpu.load(st);
		m.cpu.set_irq(Source::IRL0, true);
		m.cpu.run(2);
		eq("SR.BL holds every interrupt back", m.state().pc, CODE + 4);
		/* ldc r1,sr opens them: the interrupt is taken before the next instruction */
		m.code(CODE + 4, {0x410E, 0xE201, NOP});
		st = m.state();
		st.r[1] = SR_MD;
		m.cpu.load(st);
		m.cpu.run(2);
		st = m.state();
		eq("taken right after the LDC that clears BL", st.spc, CODE + 6);
		eq("the instruction after the LDC has not run", st.rbank[2], 0);
	}
	{
		/* A request lasts as long as its source holds it (19.2.2): the handler, add #1,r1; rte, runs
		 * again and again until the line is let go. */
		Machine m;
		m.irlm();
		m.code(CODE, {NOP, NOP, NOP, NOP});
		m.code(RAM + 0x600, {0x7101, 0x002B, NOP});
		Sh4State st = m.state();
		st.vbr = RAM;
		st.sr = SR_MD;
		m.cpu.load(st);
		m.cpu.set_irq(Source::IRL2, true);
		m.cpu.run(6);
		m.cpu.set_irq(Source::IRL2, false);
		m.cpu.run(2);
		st = m.state();
		eq("a held request is taken again after RTE", st.rbank[1], 2);
		eq("and no more once let go", st.pc, CODE + 4);
	}
	{
		/* The NMI pin (hardware manual, 19.2.1 and DMAOR.NMIF in 14.2.5). */
		Machine m;
		m.code(CODE, {NOP, NOP, NOP, NOP, NOP, NOP});
		m.code(RAM + 0x600, {0xE155, 0x002B, NOP});	/* handler: mov #0x55,r1; rte */
		Sh4State st = m.state();
		st.vbr = RAM;
		st.sr = SR_MD | 0xF0;	/* mask 15 */
		m.cpu.load(st);
		m.cpu.set_nmi(true);
		m.cpu.run(1);
		eq("the rising edge of NMI asks for nothing", m.state().pc, CODE + 2);
		eq("ICR.NMIL is the level of the pin", m.reg32(0xFFD00000) & 0x8000, 0x8000);
		m.cpu.set_nmi(false);
		eq("the falling edge sets DMAOR.NMIF", m.reg32(0xFFA00040) & 2, 2);
		m.cpu.run(1);
		st = m.state();
		eq("and is taken whatever the mask", st.spc, CODE + 2);
		eq("with the code of NMI", m.reg32(0xFF000028), 0x1C0);
		eq("NMIL follows the pin down", m.reg32(0xFFD00000) & 0x8000, 0);
		m.cpu.run(3);	/* rte, its slot, one instruction */
		st = m.state();
		eq("an NMI is taken once though the pin stays low", st.pc, CODE + 4);
		m.cpu.set_nmi(true);
		m.cpu.run(1);
		eq("and not at the edge back", m.state().pc, CODE + 6);
	}
	{
		/* SR.BL keeps an NMI waiting; with ICR.NMIE the rising edge asks. */
		Machine m;
		m.code(CODE, {NOP, NOP, 0x410E, NOP, NOP, NOP});	/* ldc r1,sr */
		m.code(RAM + 0x600, {0xE155, NOP, NOP});
		m.put32(DATA, 0xFFD00000);
		Sh4State st = m.state();
		st.vbr = RAM;
		st.sr = SR_MD | SR_BL;
		st.r[1] = SR_MD;
		m.cpu.load(st);
		m.cpu.set_nmi(true);
		m.cpu.set_nmi(false);
		m.cpu.set_nmi(true);
		m.cpu.run(2);
		eq("SR.BL holds an NMI back", m.state().pc, CODE + 4);
		m.cpu.run(2);
		eq("the edge is kept until BL is clear", m.state().spc, CODE + 6);
	}
	{
		Machine m;
		m.put32(DATA, 0xFFD00000);
		/* mov.l @r8,r1; mov.w @(4,r8),r0; mov.w r0,@r1 (ICR = NMIE) */
		m.put32(DATA + 4, 0x0100);
		m.code(RAM + 0x600, {0xE255, NOP, NOP});
		Sh4State st = run(m, {0x6182, 0x8582, 0x2101, NOP, NOP, NOP}, 3, [](Sh4State &s) { s.r[8] = DATA; s.vbr = RAM; });
		m.cpu.set_nmi(true);
		m.cpu.run(1);
		st = m.state();
		eq("with ICR.NMIE the rising edge asks", st.spc, CODE + 6);
		eq("a write to ICR leaves NMIL alone", m.reg32(0xFFD00000) & 0x8100, 0x8100);
	}
}

void test_on_chip()
{
	g_test = "on-chip modules";
	{
		/* TMU channel 0: TCOR = 100, TCNT = 5, TCR = UNIE on Pck/4, then TSTR = 1. In clock mode 5 the
		 * peripheral clock is a quarter of the CPU clock: a count takes 16 cycles and falls on the cycles
		 * 16, 32... The sixth, at 96, is the underflow (hardware manual, 10.3 and 12.3.1). */
		Machine m;
		m.put32(DATA, 0xFFD80008);
		/* mov.l @r8,r1 (TCOR0); mov #100,r0; mov.l r0,@r1; mov #5,r0; mov.l r0,@(4,r1); mov #0x20,r0; mov.w r0,@(8,r1)
		 * add #-4,r1 (TSTR); mov #1,r0; mov.b r0,@r1; mov.l @(8,r1),r2 (TCNT0) */
		Sh4State st = run(m, {0x6182, 0xE064, 0x2102, 0xE005, 0x1101, 0xE020, 0x8114, 0x71FC, 0xE001, 0x2100, 0x5212},
		                  11, [](Sh4State &s) {
			s.r[8] = DATA;
			s.sr |= SR_BL;	/* the interrupt stays raised */
		});
		eq("FRQCR of clock mode 5", m.reg32(0xFFC00000) & 0xFFFF, 0x0E0A);
		eq("TCNT0 read back before the first count", st.r[2], 5);
		/* mov.l @(8,r1),r2 at cycle 95, mov.l @(8,r1),r3 at 96, mov.l @(8,r1),r4 at 112 */
		m.code(CODE + 2 * 95, {0x5212, 0x5312});
		m.code(CODE + 2 * 112, {0x5412});
		m.cpu.run(84);
		eq("no underflow after five counts", m.reg32(0xFFD80010) & 0x100, 0);
		m.cpu.run(1);
		eq("TCNT0 is zero before the sixth count", m.state().r[2], 0);
		eq("TCR0 has UNF at the sixth count", m.reg32(0xFFD80010) & 0x100, 0x100);
		eq("TUNI0 is raised", m.state().irq[Source::TUNI0].raised, 1);
		m.cpu.run(1);
		eq("TCNT0 takes TCOR0 at the underflow", m.state().r[3], 100);
		m.cpu.run(16);
		eq("and counts on", m.state().r[4], 99);
		/* mov #0x20,r0; mov.w r0,@(12,r1) (TCR0: UNF written as 0) */
		m.code(CODE + 2 * 113, {0xE020, 0x8116});
		m.cpu.run(2);
		eq("TUNI0 lasts until UNF is cleared", m.state().irq[Source::TUNI0].raised, 0);
	}
	{
		/* The same timer started in the middle of a slice, interrupts open: the handler is entered at
		 * cycle 96, before the instruction of that cycle. */
		Machine m;
		m.put32(DATA, 0xFFD80008);
		m.put32(DATA + 4, 0xFFD00004);
		m.code(RAM + 0x600, {0xE155, NOP, NOP});	/* handler: mov #0x55,r1 */
		/* mov.l @(4,r8),r3 (IPRA); mov #-16,r0; shll8 r0; mov.w r0,@r3 (TMU0 at priority 15), then as above */
		Sh4State st = run(m, {0x5381, 0xE0F0, 0x4018, 0x2301,
		                      0x6182, 0xE064, 0x2102, 0xE005, 0x1101, 0xE020, 0x8114, 0x71FC, 0xE001, 0x2100},
		                  200, [](Sh4State &s) {
			s.r[8] = DATA;
			s.vbr = RAM;
			s.sr = SR_MD;
		});
		eq("the handler runs", st.r[1], 0x55);
		eq("SPC is the instruction of cycle 96", st.spc, CODE + 2 * 96);
		eq("INTEVT of TUNI0", m.reg32(0xFF000028), 0x400);
	}
	{
		/* Pck/16 (TPSC = 1): a count every 64 cycles. set_time() places the CPU in the time of the
		 * machine: from cycle 1000 the first count falls on 1024. */
		Machine m;
		m.put32(DATA, 0xFFD80008);
		m.cpu.set_time(1000);
		/* mov.l @r8,r1; mov #1,r0; mov.w r0,@(8,r1) (TCR0); mov #50,r0; mov.l r0,@(4,r1) (TCNT0);
		 * add #-4,r1; mov #1,r0; mov.b r0,@r1 (TSTR) */
		run(m, {0x6182, 0xE001, 0x8114, 0xE032, 0x1101, 0x71FC, 0xE001, 0x2100}, 8, [](Sh4State &s) {
			s.r[8] = DATA;
		});
		/* mov.l @(8,r1),r2 at cycle 1023 (the 24th instruction), mov.l @(8,r1),r3 at 1024 */
		m.code(CODE + 2 * 23, {0x5212, 0x5312});
		m.cpu.run(17);
		eq("TCNT0 before the first count", m.state().r[2], 50);
		eq("TCNT0 at the first count", m.state().r[3], 49);
		m.code(CODE + 2 * 88, {0x5412});	/* at cycle 1088 */
		m.cpu.run(64);
		eq("TCNT0 one count later", m.state().r[4], 48);
	}
	{
		/* Port A: pins 0-7 outputs, 8-15 inputs. */
		Machine m;
		m.pins_in = 0xA5FF;
		m.put32(DATA, 0xFF80002C);
		/* mov.l @r8,r1; mov.l @(4,r8),r0; mov.l r0,@r1 (PCTRA); mov #0x3C,r0; mov.w r0,@(4,r1) (PDTRA); mov.w @(4,r1),r0 */
		m.put32(DATA + 4, 0x00005555);
		Sh4State st = run(m, {0x6182, 0x5081, 0x2102, 0xE03C, 0x8112, 0x8512}, 6, [](Sh4State &s) { s.r[8] = DATA; });
		eq("port A outputs", m.pins_out & 0xFF, 0x3C);
		eq("a pin that is not an output is left high", m.pins_out >> 8, 0xFF);
		eq("port A reads the input pins and its own outputs", st.r[0] & 0xFFFF, 0xA53C);
	}
	{
		/* DMAC channel 2, auto-request, 4 bytes, both addresses incremented: 3 longs from DATA to DATA+0x100. */
		Machine m;
		for (uint32_t i = 0; i < 3; i++) {
			m.put32(DATA + 4 * i, 0x11110000 * (i + 1));
		}
		m.put32(DATA + 0x40, 0xFFA00020);	/* SAR2 */
		m.put32(DATA + 0x44, DATA);
		m.put32(DATA + 0x48, DATA + 0x100);
		m.put32(DATA + 0x4C, 0x00005435);	/* CHCR: DM, SM increment, RS = 4 auto, TS = 4 bytes, IE, DE */
		/* mov.l @(0x40,r8),r1; mov.l @(0x44,r8),r0; mov.l r0,@r1; mov.l @(0x48,r8),r0; mov.l r0,@(4,r1); mov #3,r0;
		 * mov.l r0,@(8,r1); mov #1,r0; mov.l r0,@(32,r1) (DMAOR); mov.l @(0x4C,r8),r0; mov.l r0,@(12,r1) (CHCR2) */
		run(m, {0x5180, 0x5081, 0x2102, 0x5082, 0x1101, 0xE003, 0x1102, 0xE001, 0x1108, 0x5083, 0x1103}, 11,
		    [](Sh4State &s) {
			s.r[8] = DATA + 0x40;
			s.sr |= SR_BL;
		});
		eq("DMAC copies", m.get32(DATA + 0x108), 0x33330000);
		eq("TCR2 at zero", m.reg32(0xFFA00028), 0);
		eq("CHCR2 has TE", m.reg32(0xFFA0002C) & 2, 2);
		eq("DMTE2 is raised", m.state().irq[Source::DMTE2].raised, 1);
		/* mov #0,r0; mov.l r0,@(12,r1) (CHCR2) */
		m.code(CODE + 2 * 11, {0xE000, 0x1103});
		m.cpu.run(2);
		eq("DMTE2 lasts until TE and IE are cleared", m.state().irq[Source::DMTE2].raised, 0);
	}
	{
		/* Store queues: QACR0 = area 3; a write at E0000008 lands at 0C000008 + what the address carries. */
		Machine m;
		m.put32(DATA, 0xFF000038);
		m.put32(DATA + 4, 0xE0000000 | (DATA & 0x03FFFFE0) | 0x08);
		/* mov.l @r8,r1; mov #0x0C,r0; mov.l r0,@r1; mov.l @(4,r8),r2; mov.l r3,@r2; pref @r2 */
		run(m, {0x6182, 0xE00C, 0x2102, 0x5281, 0x2232, 0x0283}, 6, [](Sh4State &s) {
			s.r[8] = DATA;
			s.r[3] = 0x5151A0A0;
		});
		eq("store queue write", m.get32((DATA & ~0x1Fu) + 8), 0x5151A0A0);
	}
	{
		/* The bus: a device answers, and nothing answers. */
		Machine m;
		m.device_value = 0xABCD1234;
		m.put32(DATA, DEVICE + 0x10);
		m.put32(DATA + 4, 0x08000000);
		/* mov.l @r8,r1; mov.l @r1,r2; mov.w r0,@(4,r1); mov.l @(4,r8),r3; mov.l @r3,r4; mov #1,r5 */
		Sh4State st = run(m, {0x6182, 0x6212, 0x8112, 0x5381, 0x6432, 0xE501}, 6, [](Sh4State &s) {
			s.r[8] = DATA;
			s.r[4] = 0x99;
		});
		eq("a device read", st.r[2], 0xABCD1234);
		eq("two accesses reached the device", m.device.size(), 2);
		eq("the write has its size", m.device.size() > 1 ? m.device[1].size : 0, 2);
		eq("an unanswered read gives zero", st.r[4], 0);
		eq("and is reported once", m.unanswered_log.size(), 1);
		eq("the program goes on", st.r[5], 1);
	}
	{
		/* An interrupt raised by a device is taken before the next instruction. */
		Machine m;
		m.irlm();
		m.put32(DATA, DEVICE);
		m.code(RAM + 0x600, {0xE777, NOP});	/* mov #0x77,r7 */
		m.on_device = [&m] { m.cpu.set_irq(Source::IRL0, true); };
		Sh4State st = run(m, {0x6182, 0x2112, 0xE201, 0xE302}, 3, [](Sh4State &s) {
			s.r[8] = DATA;
			s.vbr = RAM;
		});
		eq("the handler ran right after the write", st.r[7], 0x77);
		eq("the instruction after the write did not", st.r[2], 0);
		eq("SPC", st.spc, CODE + 4);
	}
}

void test_blocks()
{
	g_test = "blocks";
	{
		/* Code that changes: the same address runs what the memory holds now. */
		Machine m;
		m.code(CODE, {0xE101, NOP, NOP, NOP});	/* mov #1,r1 */
		m.cpu.run(2);
		eq("first version", m.state().r[1], 1);
		m.put16(CODE, 0xE102);	/* written from outside: mov #2,r1 */
		Sh4State st = m.state();
		st.pc = CODE;
		m.cpu.load(st);
		m.cpu.run(2);
		eq("second version", m.state().r[1], 2);
	}
	{
		/* A store that changes an instruction further in its own block. */
		Machine m;
		/* mov.w @(8,pc),r0 [E203]; mov.w r0,@(4,r8): over the mov #1,r2 three instructions further;
		 * nop; nop; mov #1,r2; nop; .word E203 */
		Sh4State st = run(m, {0x9004, 0x8182, NOP, NOP, 0xE201, NOP, 0xE203, NOP}, 6, [](Sh4State &s) { s.r[8] = CODE + 4; });
		eq("the new instruction runs", st.r[2], 3);
	}
	{
		/* A delay loop is counted, not run, with the same result. */
		Machine m;
		/* mov #0x40,r1; shll8 r1; dt r1; bf -3; mov #9,r2 */
		m.code(CODE, {0xE140, 0x4118, 0x4110, 0x8BFD, 0xE209, NOP, NOP, NOP});
		int32_t cycles = 2 + 2 * 0x4000 + 1;
		m.cpu.run(cycles);
		Sh4State st = m.state();
		eq("the loop ends on time", st.r[2], 9);
		eq("with its counter at zero", st.r[1], 0);
		check(m.cpu.skipped() > 0x7000, "most of its turns were counted without being run");
	}
	{
		/* A loop that only waits is counted too: bra to itself. */
		Machine m;
		m.code(CODE, {0xAFFE, NOP, NOP, NOP});
		m.cpu.run(10000);
		eq("still in the loop", m.state().pc, CODE);
		check(m.cpu.skipped() > 9000, "its turns were counted without being run");
		check(m.cpu.instructions() + m.cpu.skipped() >= 10000, "and every cycle is accounted for");
	}
	{
		/* Slices that end at every place of a loop: the program resumes
		 * in the block that is kept, and no other is. 40 add #1, rn /
		 * bra / nop */
		Machine m;
		for (uint32_t i = 0; i < 40; i++) {
			m.put16(CODE + 2 * i, static_cast<uint16_t>(0x7001 | ((1 + i % 7) << 8)));
		}
		m.code(CODE + 80, {0xAFD6, NOP});
		for (uint32_t i = 0; i < 2000; i++) {
			m.cpu.run(static_cast<int32_t>(65 + i % 97));
		}
		eq("one block for the loop", m.cpu.blocks(), 1);
	}
	{
		/* A place a branch leads to has a block of its own, though the
		 * block before it runs through that place: 5 add #1, r2 / then
		 * the loop, 5 add #1, r3 / add #-1, r1 / cmp/pl r1 / bt loop /
		 * mov #9, r1 / bra start / nop */
		Machine m;
		m.code(CODE, {0x7201, 0x7201, 0x7201, 0x7201, 0x7201,
		              0x7301, 0x7301, 0x7301, 0x7301, 0x7301, 0x71FF, 0x4115, 0x89F8,
		              0xE109, 0xAFF0, NOP});
		for (uint32_t i = 0; i < 2000; i++) {
			m.cpu.run(static_cast<int32_t>(65 + i % 97));
		}
		eq("one block from the start, one from the loop", m.cpu.blocks(), 2);
	}
	{
		/* A loop that reads a device is run in full. */
		Machine m;
		m.put32(DATA, DEVICE);
		/* mov.l @r8,r1; mov.l @r1,r0; tst r0,r0; bt -4 (back to the read) */
		run(m, {0x6182, 0x6012, 0x2008, 0x89FC, NOP, NOP}, 301, [](Sh4State &s) { s.r[8] = DATA; });
		eq("every turn read the device", m.device.size(), 100);
	}
}

/* The device raises IRL1 on one read in five and drops it on a write at
 * its second word; the handler, at VBR + 0x600, writes there and returns.
 * R12 holds the device, R14 the data, R13 the code. */
void with_device(Machine &m)
{
	Machine *self = &m;

	m.irlm();
	m.on_device = [self] {
		const Machine::Access &last = self->device.back();

		self->seen.push_back(self->cpu.pc());
		if (last.write && last.addr == DEVICE + 4) {
			self->cpu.set_irq(Source::IRL1, false);
		} else if (!last.write && self->device.size() % 5 == 0) {
			self->cpu.set_irq(Source::IRL1, true);
		}
	};
	m.code(RAM + 0x7000, { 0x1C01, 0x002B, 0x0009 });	/* mov.l r0, @(4, r12) / rte / nop */
	Sh4State st = m.state();
	st.vbr = RAM + 0x7000 - 0x600;
	st.r[12] = DEVICE;
	st.r[13] = CODE;
	st.r[14] = DATA;
	m.cpu.load(st);
}

/* A program of `slots` instructions picked from a fixed series: sums and
 * moves, memory, the device, every kind of branch within the program and
 * delay loops, then back to its start. */
void random_program(Machine &m, uint32_t slots, uint32_t seed)
{
	auto next = [&seed]() {
		seed = seed * 1664525u + 1013904223u;
		return seed >> 8;
	};
	auto plain = [&]() -> uint16_t {
		const uint32_t n = next() % 8, r = next() % 8, d = next() % 16;

		switch (next() % 24) {
		case 0: return static_cast<uint16_t>(0xE000 | (n << 8) | (next() & 0xFF));	/* mov #imm, rn */
		case 1: case 2: return static_cast<uint16_t>(0x7000 | (n << 8) | (next() & 0xFF));	/* add #imm, rn */
		case 3: return static_cast<uint16_t>(0x300C | (n << 8) | (r << 4));	/* add rm, rn */
		case 4: return static_cast<uint16_t>(0x6003 | (n << 8) | (r << 4));	/* mov rm, rn */
		case 5: return static_cast<uint16_t>(0x3000 | (n << 8) | (r << 4));	/* cmp/eq rm, rn */
		case 6: return static_cast<uint16_t>(0x3006 | (n << 8) | (r << 4));	/* cmp/hi rm, rn */
		case 7: return static_cast<uint16_t>(0x2008 | (n << 8) | (r << 4));	/* tst rm, rn */
		case 8: return static_cast<uint16_t>(0x200A | (n << 8) | (r << 4));	/* xor rm, rn */
		case 9: return static_cast<uint16_t>(0x3008 | (n << 8) | (r << 4));	/* sub rm, rn */
		case 10: return static_cast<uint16_t>(0x4000 | (n << 8));		/* shll rn */
		case 11: return static_cast<uint16_t>(0x4001 | (n << 8));		/* shlr rn */
		case 12: return static_cast<uint16_t>(0x300E | (n << 8) | (r << 4));	/* addc rm, rn */
		case 13: case 14: return static_cast<uint16_t>(0x1E00 | (r << 4) | d);	/* mov.l rm, @(d, r14) */
		case 15: case 16: return static_cast<uint16_t>(0x50E0 | (n << 8) | d);	/* mov.l @(d, r14), rn */
		case 17: return static_cast<uint16_t>(0x0007 | (n << 8) | (r << 4));	/* mul.l rm, rn */
		case 18: return static_cast<uint16_t>(0x001A | (n << 8));		/* sts macl, rn */
		case 19: return static_cast<uint16_t>(0x60C2 | (n << 8));		/* mov.l @r12, rn: the device */
		case 20: return static_cast<uint16_t>(0x2C02 | (r << 4));		/* mov.l rm, @r12 */
		case 21: return static_cast<uint16_t>(0x0002 | (n << 8));		/* stc sr, rn */
		case 22: return static_cast<uint16_t>(0x600E | (n << 8) | (r << 4));	/* exts.b rm, rn */
		default: return static_cast<uint16_t>(0x6007 | (n << 8) | (r << 4));	/* not rm, rn */
		}
	};
	auto reach = [&](uint32_t from, uint32_t span) {	/* a slot near `from`, as a displacement */
		const int32_t to = static_cast<int32_t>(from) - static_cast<int32_t>(span) + static_cast<int32_t>(next() % (2 * span));
		const int32_t slot = to < 0 ? 0 : (to >= static_cast<int32_t>(slots) ? static_cast<int32_t>(slots) - 1 : to);

		return slot - static_cast<int32_t>(from) - 2;
	};

	for (uint32_t i = 0; i < slots; i++) {
		const uint32_t at = CODE + 2 * i, kind = next() % 100;

		if (kind < 70 || i + 3 >= slots) {
			m.put16(at, plain());
		} else if (kind < 78) {		/* bt, bf */
			m.put16(at, static_cast<uint16_t>(((next() & 1) ? 0x8900 : 0x8B00) | (reach(i, 30) & 0xFF)));
		} else if (kind < 88) {		/* bt/s, bf/s and the slot */
			m.put16(at, static_cast<uint16_t>(((next() & 1) ? 0x8D00 : 0x8F00) | (reach(i, 30) & 0xFF)));
			m.put16(at + 2, plain());
			i++;
		} else if (kind < 93) {		/* bra and the slot */
			m.put16(at, static_cast<uint16_t>(0xA000 | (reach(i, 200) & 0xFFF)));
			m.put16(at + 2, plain());
			i++;
		} else if (kind < 96) {		/* bsr and the slot */
			m.put16(at, static_cast<uint16_t>(0xB000 | (reach(i, 200) & 0xFFF)));
			m.put16(at + 2, plain());
			i++;
		} else {			/* a delay loop: mov #imm, rn / dt rn / bf back */
			const uint32_t n = next() % 8;

			m.put16(at, static_cast<uint16_t>(0xE000 | (n << 8) | (1 + next() % 100)));
			m.put16(at + 2, static_cast<uint16_t>(0x4010 | (n << 8)));
			m.put16(at + 4, 0x8BFD);
			i += 2;
		}
	}
	/* Back to the start: bra / nop */
	m.put16(CODE + 2 * slots, static_cast<uint16_t>(0xA000 | ((0u - slots - 2u) & 0xFFF)));
	m.put16(CODE + 2 * slots + 2, 0x0009);
}

/* A program is at the same place after the same cycles however they are cut
 * in slices: what a slice ends on, and where the next one resumes, change
 * nothing. */
void test_slices()
{
	g_test = "slices";
	const auto same = [](const Machine &a, const Machine &b) {
		const Sh4State x = a.state(), y = b.state();

		if (std::memcmp(&x, &y, sizeof(x)) != 0 || a.ram != b.ram || a.onchip != b.onchip ||
		    a.seen != b.seen || a.device.size() != b.device.size()) {
			return false;
		}
		for (size_t i = 0; i < a.device.size(); i++) {
			if (a.device[i].write != b.device[i].write || a.device[i].addr != b.device[i].addr ||
			    a.device[i].value != b.device[i].value) {
				return false;
			}
		}
		return true;
	};

	for (uint32_t seed = 1; seed <= 24; seed++) {
		const uint32_t total = 3000 + 97 * seed, slots = 300 + 40 * (seed % 8);
		const auto set = [seed, slots](Machine &m) {
			with_device(m);
			random_program(m, slots, seed * 7919u);
		};
		Machine whole;
		unsigned ways = 0, differ = 0;

		set(whole);
		whole.cpu.run(static_cast<int32_t>(total));
		/* In two slices, cut at many places */
		for (uint32_t cut = 1; cut < total; cut += 1 + (cut * 7 + seed) % 23) {
			Machine m;

			set(m);
			m.cpu.run(static_cast<int32_t>(cut));
			m.cpu.run(static_cast<int32_t>(total - cut));
			ways++;
			differ += same(whole, m) ? 0 : 1;
		}
		/* In many slices of every length */
		for (uint32_t longest : { 3u, 70u, 200u }) {
			Machine m;
			uint32_t series = seed;

			set(m);
			for (uint32_t done = 0; done < total;) {
				series = series * 1664525u + 1013904223u;
				const uint32_t cycles = std::min(total - done, 1 + (series >> 8) % longest);
				m.cpu.run(static_cast<int32_t>(cycles));
				done += cycles;
			}
			ways++;
			differ += same(whole, m) ? 0 : 1;
		}
		check(ways > 100 && differ == 0, "program " + std::to_string(seed) + ": the same however it is cut");
	}
}

/* What cuts a block short keeps no block either: the device read, the
 * interrupt it raises one time in five, the write of the handler and its
 * return. 5 add #1, r1 / mov.l @r12, r0 / 5 add #1, r2 / bra / nop */
void test_blocks_resumed()
{
	g_test = "blocks";
	Machine m;

	with_device(m);
	m.code(CODE, {0x7101, 0x7101, 0x7101, 0x7101, 0x7101, 0x60C2,
	              0x7201, 0x7201, 0x7201, 0x7201, 0x7201, 0xAFF3, NOP});
	for (uint32_t i = 0; i < 2000; i++) {
		m.cpu.run(static_cast<int32_t>(65 + i % 97));
	}
	check(m.device.size() > 5000, "the loop read the device and was interrupted");
	eq("one block for the loop, one for the handler", m.cpu.blocks(), 2);
}

/* Two places a branch leads to, 128 KB apart, take turns in the table of
 * the blocks last found: each is looked for in the pages again, where the
 * block that starts there comes before the one that runs through it.
 * 3 add #1, r1 / there: 3 add #1, r2 / jmp far; far: 2 add #1, r3 / jmp
 * there */
void test_blocks_found_again()
{
	g_test = "blocks";
	const uint32_t there = CODE + 6, far = there + 0x20000;
	Machine m;

	m.code(CODE, {0x7101, 0x7101, 0x7101, 0x7201, 0x7201, 0x7201, 0xD001, 0x402B, NOP, NOP});
	m.put32(CODE + 20, far);
	m.code(far, {0x7301, 0x7301, 0xD001, 0x402B, NOP});
	m.put32(far + 10, there);
	m.cpu.run(20000);
	check(m.state().r[3] > 3000, "the program went round");
	eq("the start, the place in it a branch leads to, and the far one", m.cpu.blocks(), 3);
}

void test_hook()
{
	g_test = "hook";
	Machine m;
	m.code(CODE, {0xE101, 0xE202, 0xE303, NOP, NOP, NOP});
	m.cpu.set_hook(CODE + 2);
	m.on_hook = [&m] {
		m.cpu.set_reg(1, m.cpu.reg(1) + 0x10);
		m.cpu.skip(2);
	};
	int32_t over = m.cpu.run(5);
	Sh4State st = m.state();
	eq("the hook is called once", m.hooks, 1);
	eq("after the instruction before it", st.r[1], 0x11);
	eq("and before its own", st.r[2], 2);
	eq("the cycles it counted are not run", st.pc, CODE + 6);
	eq("no cycle past the end", static_cast<uint32_t>(over), 0);
}

void test_save_load()
{
	g_test = "save and load";
	Machine a, b;
	const std::initializer_list<uint16_t> words = {0xE17F, 0x4118, 0x210A, 0xF1FD, 0xF21C, 0x0018, 0x4124, NOP, NOP, NOP};
	a.code(CODE, words);
	b.code(CODE, words);
	a.cpu.run(3);
	Sh4State mid;
	a.cpu.save(mid);
	b.cpu.load(mid);
	a.cpu.run(4);
	b.cpu.run(4);
	Sh4State x = a.state(), y = b.state();
	check(std::memcmp(&x, &y, sizeof(x)) == 0, "a loaded state goes on like the one it was saved from");
}

} /* namespace */

int main(int argc, char **argv)
{
	for (int i = 1; i < argc; i++) {
		if (std::strcmp(argv[i], "-v") == 0) {
			g_verbose = true;
		}
	}
	test_data_transfer();
	test_arithmetic();
	test_logic_and_shifts();
	test_branches();
	test_system();
	test_fpu();
	test_interrupts();
	test_on_chip();
	test_blocks();
	test_slices();
	test_blocks_resumed();
	test_blocks_found_again();
	test_hook();
	test_save_load();
	std::printf("%d checks, %d failures\n", g_checks, g_failures);
	return g_failures == 0 ? 0 : 1;
}
