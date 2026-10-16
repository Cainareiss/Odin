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

/* Unit tests for the Odin ARM7 core: hand-assembled instructions run
 * on a flat RAM bus, with the expected registers, flags and memory
 * taken from the ARM7TDMI data sheet and the ARMv4 reference manual.
 *
 * Build: g++ -std=c++20 -O2 -Wall -Wextra -Wpedantic -Wconversion -Wshadow \
 *            -I src tests/arm7/arm7_tests.cpp src/odin/arm7.cpp src/odin/arm7_ops.cpp \
 *            src/odin/arm7_host.cpp src/odin/arm7_translate.cpp src/odin/host_code.cpp -o arm7_tests
 * Use:   arm7_tests [-v]
 * Exit status is 0 when every check passes. */

#include "odin/arm7.hpp"

#include <cstdio>
#include <cstring>
#include <initializer_list>
#include <string>
#include <vector>

namespace {

using odin::Arm7;

class RamBus : public odin::Arm7Bus {
public:
	std::vector<uint8_t> mem;

	RamBus() : mem(1u << 20, 0) {}

	uint8_t read8(uint32_t addr) override { return mem[addr & 0xFFFFF]; }
	uint16_t read16(uint32_t addr) override
	{
		addr &= 0xFFFFE;
		return static_cast<uint16_t>(mem[addr] | (mem[addr + 1] << 8));
	}
	uint32_t read32(uint32_t addr) override
	{
		addr &= 0xFFFFC;
		return static_cast<uint32_t>(mem[addr]) | (static_cast<uint32_t>(mem[addr + 1]) << 8)
			| (static_cast<uint32_t>(mem[addr + 2]) << 16) | (static_cast<uint32_t>(mem[addr + 3]) << 24);
	}
	void write8(uint32_t addr, uint8_t v) override { mem[addr & 0xFFFFF] = v; }
	void write16(uint32_t addr, uint16_t v) override
	{
		addr &= 0xFFFFE;
		mem[addr] = static_cast<uint8_t>(v);
		mem[addr + 1] = static_cast<uint8_t>(v >> 8);
	}
	void write32(uint32_t addr, uint32_t v) override
	{
		addr &= 0xFFFFC;
		mem[addr] = static_cast<uint8_t>(v);
		mem[addr + 1] = static_cast<uint8_t>(v >> 8);
		mem[addr + 2] = static_cast<uint8_t>(v >> 16);
		mem[addr + 3] = static_cast<uint8_t>(v >> 24);
	}
};

/* Encoders. Field names follow the data sheet figures. */

constexpr uint32_t AL = 0xE;
enum Opc : uint32_t { AND = 0, EOR, SUB, RSB, ADD, ADC, SBC, RSC, TST, TEQ, CMP, CMN, ORR, MOV, BIC, MVN };
enum Shift : uint32_t { LSL = 0, LSR, ASR, ROR };

uint32_t dp_imm(uint32_t opc, bool s, uint32_t rn, uint32_t rd, uint32_t imm8, uint32_t rot4, uint32_t cc = AL)
{
	return (cc << 28) | (1u << 25) | (opc << 21) | (s ? 1u << 20 : 0) | (rn << 16) | (rd << 12) | (rot4 << 8) | imm8;
}

uint32_t dp_reg(uint32_t opc, bool s, uint32_t rn, uint32_t rd, uint32_t rm, uint32_t type, uint32_t imm5, uint32_t cc = AL)
{
	return (cc << 28) | (opc << 21) | (s ? 1u << 20 : 0) | (rn << 16) | (rd << 12) | (imm5 << 7) | (type << 5) | rm;
}

uint32_t dp_rs(uint32_t opc, bool s, uint32_t rn, uint32_t rd, uint32_t rm, uint32_t type, uint32_t rs, uint32_t cc = AL)
{
	return (cc << 28) | (opc << 21) | (s ? 1u << 20 : 0) | (rn << 16) | (rd << 12) | (rs << 8) | (type << 5) | 0x10 | rm;
}

uint32_t mul(bool accumulate, bool s, uint32_t rd, uint32_t rn, uint32_t rs, uint32_t rm)
{
	return (AL << 28) | (accumulate ? 1u << 21 : 0) | (s ? 1u << 20 : 0) | (rd << 16) | (rn << 12) | (rs << 8) | 0x90 | rm;
}

uint32_t mull(bool is_signed, bool accumulate, bool s, uint32_t rd_hi, uint32_t rd_lo, uint32_t rs, uint32_t rm)
{
	return (AL << 28) | (1u << 23) | (is_signed ? 1u << 22 : 0) | (accumulate ? 1u << 21 : 0) | (s ? 1u << 20 : 0)
		| (rd_hi << 16) | (rd_lo << 12) | (rs << 8) | 0x90 | rm;
}

uint32_t swp(bool byte, uint32_t rn, uint32_t rd, uint32_t rm)
{
	return (AL << 28) | (1u << 24) | (byte ? 1u << 22 : 0) | (rn << 16) | (rd << 12) | 0x90 | rm;
}

uint32_t bits_puw(bool p, bool u, bool w) { return (p ? 1u << 24 : 0) | (u ? 1u << 23 : 0) | (w ? 1u << 21 : 0); }

uint32_t ldst_imm(bool load, bool byte, bool p, bool u, bool w, uint32_t rn, uint32_t rd, uint32_t imm12)
{
	return (AL << 28) | (1u << 26) | bits_puw(p, u, w) | (byte ? 1u << 22 : 0) | (load ? 1u << 20 : 0) | (rn << 16) | (rd << 12) | imm12;
}

uint32_t ldst_reg(bool load, bool byte, bool p, bool u, bool w, uint32_t rn, uint32_t rd, uint32_t rm, uint32_t type, uint32_t imm5)
{
	return (AL << 28) | (3u << 25) | bits_puw(p, u, w) | (byte ? 1u << 22 : 0) | (load ? 1u << 20 : 0) | (rn << 16) | (rd << 12)
		| (imm5 << 7) | (type << 5) | rm;
}

/* sh: 1 = H, 2 = SB, 3 = SH. */
uint32_t ldsth_imm(bool load, bool p, bool u, bool w, uint32_t rn, uint32_t rd, uint32_t sh, uint32_t imm8)
{
	return (AL << 28) | bits_puw(p, u, w) | (1u << 22) | (load ? 1u << 20 : 0) | (rn << 16) | (rd << 12)
		| ((imm8 >> 4) << 8) | 0x90 | (sh << 5) | (imm8 & 0xF);
}

uint32_t ldsth_reg(bool load, bool p, bool u, bool w, uint32_t rn, uint32_t rd, uint32_t sh, uint32_t rm)
{
	return (AL << 28) | bits_puw(p, u, w) | (load ? 1u << 20 : 0) | (rn << 16) | (rd << 12) | 0x90 | (sh << 5) | rm;
}

uint32_t ldstm(bool load, bool p, bool u, bool s, bool w, uint32_t rn, uint32_t list)
{
	return (AL << 28) | (4u << 25) | bits_puw(p, u, w) | (s ? 1u << 22 : 0) | (load ? 1u << 20 : 0) | (rn << 16) | list;
}

uint32_t branch(bool link, int32_t words, uint32_t cc = AL)
{
	return (cc << 28) | (5u << 25) | (link ? 1u << 24 : 0) | (static_cast<uint32_t>(words) & 0xFFFFFF);
}

uint32_t bx(uint32_t rm) { return (AL << 28) | 0x012FFF10 | rm; }
uint32_t mrs(bool spsr, uint32_t rd) { return (AL << 28) | 0x010F0000 | (spsr ? 1u << 22 : 0) | (rd << 12); }
uint32_t msr_reg(bool spsr, uint32_t fields, uint32_t rm) { return (AL << 28) | 0x0120F000 | (spsr ? 1u << 22 : 0) | (fields << 16) | rm; }
uint32_t msr_imm(bool spsr, uint32_t fields, uint32_t imm8, uint32_t rot4)
{
	return (AL << 28) | 0x0320F000 | (spsr ? 1u << 22 : 0) | (fields << 16) | (rot4 << 8) | imm8;
}
uint32_t swi(uint32_t n) { return (AL << 28) | 0x0F000000 | n; }
constexpr uint32_t NOP = 0xE1A00000; /* mov r0, r0 */
constexpr uint32_t MCR_CP15 = 0xEE010F10; /* mcr p15, 0, r0, c1, c0, 0 */

/* Test harness. */

int g_failures = 0;
int g_checks = 0;
bool g_verbose = false;
std::string g_current;

void fail(const std::string &what)
{
	g_failures++;
	std::printf("FAIL %s: %s\n", g_current.c_str(), what.c_str());
}

void check_eq(const char *what, uint64_t got, uint64_t want)
{
	g_checks++;
	if (got != want) {
		char buf[160];
		std::snprintf(buf, sizeof buf, "%s = %08llx, expected %08llx", what, static_cast<unsigned long long>(got), static_cast<unsigned long long>(want));
		fail(buf);
	}
}

class Machine {
public:
	RamBus bus;
	Arm7 cpu;
	uint32_t base;

	explicit Machine(uint32_t cpsr = Arm7::MODE_SVC | Arm7::FLAG_I | Arm7::FLAG_F, uint32_t start = 0x1000)
		: cpu(bus), base(start)
	{
		odin::Arm7State st{};
		cpu.save(st);
		st.pc = start;
		st.cpsr = cpsr;
		cpu.load(st);
	}

	void program(std::initializer_list<uint32_t> words, uint32_t at = 0xFFFFFFFF)
	{
		uint32_t addr = at == 0xFFFFFFFF ? base : at;
		for (uint32_t w : words) {
			bus.write32(addr, w);
			addr += 4;
		}
	}

	void r(uint32_t idx, uint32_t v) { cpu.set_reg(idx, v); }
	void flags(bool n, bool z, bool c, bool v)
	{
		odin::Arm7State st{};
		cpu.save(st);
		st.cpsr = (st.cpsr & 0x0FFFFFFF) | (n ? Arm7::FLAG_N : 0) | (z ? Arm7::FLAG_Z : 0) | (c ? Arm7::FLAG_C : 0) | (v ? Arm7::FLAG_V : 0);
		cpu.load(st);
	}
	uint32_t steps(uint32_t n)
	{
		uint32_t cycles = 0;
		for (uint32_t i = 0; i < n; i++) {
			cycles += cpu.step();
		}
		return cycles;
	}
	void expect_r(uint32_t idx, uint32_t v)
	{
		char name[8];
		std::snprintf(name, sizeof name, "r%u", idx);
		check_eq(name, idx == 15 ? cpu.pc() : cpu.reg(idx), v);
	}
	void expect_pc(uint32_t v) { check_eq("pc", cpu.pc(), v); }
	void expect_flags(bool n, bool z, bool c, bool v)
	{
		check_eq("nzcv", cpu.cpsr() >> 28, (n ? 8u : 0u) | (z ? 4u : 0u) | (c ? 2u : 0u) | (v ? 1u : 0u));
	}
	void expect_cpsr(uint32_t v) { check_eq("cpsr", cpu.cpsr(), v); }
	void expect_mode(uint32_t m) { check_eq("mode", cpu.mode(), m); }
	void expect_mem32(uint32_t addr, uint32_t v)
	{
		char name[24];
		std::snprintf(name, sizeof name, "[%08x]", addr);
		check_eq(name, bus.read32(addr), v);
	}
	void expect_mem8(uint32_t addr, uint32_t v)
	{
		char name[24];
		std::snprintf(name, sizeof name, "[%08x].b", addr);
		check_eq(name, bus.read8(addr), v);
	}
};

#define TEST(name) g_current = name; if (g_verbose) std::printf("test %s\n", name)

/* Condition codes. */
void test_conditions()
{
	TEST("conditions");
	struct Case { uint32_t cc; bool n, z, c, v; bool taken; };
	const Case cases[] = {
		{ 0x0, false, true, false, false, true }, { 0x0, false, false, false, false, false },
		{ 0x1, false, false, false, false, true }, { 0x1, false, true, false, false, false },
		{ 0x2, false, false, true, false, true }, { 0x2, false, false, false, false, false },
		{ 0x3, false, false, false, false, true }, { 0x3, false, false, true, false, false },
		{ 0x4, true, false, false, false, true }, { 0x4, false, false, false, false, false },
		{ 0x5, false, false, false, false, true }, { 0x5, true, false, false, false, false },
		{ 0x6, false, false, false, true, true }, { 0x6, false, false, false, false, false },
		{ 0x7, false, false, false, false, true }, { 0x7, false, false, false, true, false },
		{ 0x8, false, false, true, false, true }, { 0x8, false, true, true, false, false }, { 0x8, false, false, false, false, false },
		{ 0x9, false, true, true, false, true }, { 0x9, false, false, false, false, true }, { 0x9, false, false, true, false, false },
		{ 0xA, true, false, false, true, true }, { 0xA, false, false, false, false, true }, { 0xA, true, false, false, false, false },
		{ 0xB, true, false, false, false, true }, { 0xB, false, false, false, true, true }, { 0xB, true, false, false, true, false },
		{ 0xC, false, false, false, false, true }, { 0xC, true, false, false, true, true }, { 0xC, false, true, false, false, false }, { 0xC, true, false, false, false, false },
		{ 0xD, false, true, false, false, true }, { 0xD, true, false, false, false, true }, { 0xD, false, false, false, false, false },
		{ 0xE, true, true, true, true, true }, { 0xE, false, false, false, false, true },
		{ 0xF, false, false, false, false, false }, { 0xF, true, true, true, true, false },
	};
	for (const Case &c : cases) {
		Machine m;
		m.program({ dp_imm(MOV, false, 0, 0, 1, 0, c.cc) });
		m.flags(c.n, c.z, c.c, c.v);
		uint32_t cycles = m.steps(1);
		char what[48];
		std::snprintf(what, sizeof what, "cc %x nzcv=%d%d%d%d", c.cc, c.n, c.z, c.c, c.v);
		check_eq(what, m.cpu.reg(0), c.taken ? 1u : 0u);
		check_eq("skipped cycles", cycles, c.taken ? 1u : 1u);
		check_eq("instructions counted", m.cpu.instructions(), c.taken ? 1u : 0u);
	}
}

/* Data processing: every opcode, flag edge cases. */
void test_data_processing()
{
	TEST("dp arithmetic");
	{
		Machine m;
		m.r(1, 0x7FFFFFFF);
		m.r(2, 1);
		m.program({
			dp_reg(ADD, true, 1, 0, 2, LSL, 0),   /* adds r0, r1, r2 -> 80000000, V */
			dp_imm(ADD, true, 1, 0, 1, 0),        /* adds r0, r1, #1 again */
		});
		m.steps(1);
		m.expect_r(0, 0x80000000);
		m.expect_flags(true, false, false, true);
	}
	{
		Machine m;
		m.r(1, 0xFFFFFFFF);
		m.program({ dp_imm(ADD, true, 1, 0, 1, 0) });
		m.steps(1);
		m.expect_r(0, 0);
		m.expect_flags(false, true, true, false);
	}
	{
		Machine m;
		m.r(1, 5);
		m.r(2, 7);
		m.program({ dp_reg(SUB, true, 1, 0, 2, LSL, 0) });
		m.steps(1);
		m.expect_r(0, 0xFFFFFFFE);
		m.expect_flags(true, false, false, false); /* borrow -> C clear */
	}
	{
		Machine m;
		m.r(1, 0x80000000);
		m.program({ dp_imm(SUB, true, 1, 0, 1, 0) });
		m.steps(1);
		m.expect_r(0, 0x7FFFFFFF);
		m.expect_flags(false, false, true, true);
	}
	{
		Machine m;
		m.r(1, 3);
		m.r(2, 10);
		m.program({ dp_reg(RSB, true, 1, 0, 2, LSL, 0) });   /* rsbs r0, r1, r2 = 10 - 3 */
		m.steps(1);
		m.expect_r(0, 7);
		m.expect_flags(false, false, true, false);
	}
	{
		Machine m;
		m.r(1, 10);
		m.r(2, 20);
		m.flags(false, false, true, false);
		m.program({ dp_reg(ADC, true, 1, 0, 2, LSL, 0) });
		m.steps(1);
		m.expect_r(0, 31);
		m.expect_flags(false, false, false, false);
	}
	{
		Machine m;
		m.r(1, 10);
		m.r(2, 3);
		m.flags(false, false, false, false);
		m.program({ dp_reg(SBC, true, 1, 0, 2, LSL, 0) });   /* 10 - 3 - 1 */
		m.steps(1);
		m.expect_r(0, 6);
		m.expect_flags(false, false, true, false);
	}
	{
		Machine m;
		m.r(1, 3);
		m.r(2, 10);
		m.flags(false, false, true, false);
		m.program({ dp_reg(RSC, true, 1, 0, 2, LSL, 0) });   /* 10 - 3 - 0 */
		m.steps(1);
		m.expect_r(0, 7);
		m.expect_flags(false, false, true, false);
	}
	{
		Machine m;
		m.r(1, 0x12345678);
		m.r(2, 0x0F0F0F0F);
		m.program({ dp_reg(CMP, true, 1, 0, 2, LSL, 0), dp_reg(CMN, true, 1, 0, 2, LSL, 0) });
		m.steps(1);
		m.expect_flags(false, false, true, false);
		m.steps(1);
		m.expect_flags(false, false, false, false);
		m.expect_r(0, 0);
	}
	{
		Machine m;
		m.r(1, 0x80000000);
		m.r(2, 0x80000000);
		m.program({ dp_reg(CMN, true, 1, 0, 2, LSL, 0) });   /* 0x80000000 + 0x80000000 */
		m.steps(1);
		m.expect_flags(false, true, true, true);
	}

	TEST("dp logical");
	{
		Machine m;
		m.r(1, 0xF0F0F0F0);
		m.r(2, 0xFF00FF00);
		m.flags(false, false, false, true);
		m.program({
			dp_reg(AND, true, 1, 0, 2, LSL, 0),
			dp_reg(EOR, true, 1, 3, 2, LSL, 0),
			dp_reg(ORR, true, 1, 4, 2, LSL, 0),
			dp_reg(BIC, true, 1, 5, 2, LSL, 0),
			dp_reg(MVN, true, 0, 6, 2, LSL, 0),
			dp_reg(TST, true, 1, 0, 2, LSL, 0),
			dp_reg(TEQ, true, 1, 0, 1, LSL, 0),
		});
		m.steps(1);
		m.expect_r(0, 0xF000F000);
		m.expect_flags(true, false, false, true);   /* V untouched by logical ops */
		m.steps(1);
		m.expect_r(3, 0x0FF00FF0);
		m.steps(1);
		m.expect_r(4, 0xFFF0FFF0);
		m.steps(1);
		m.expect_r(5, 0x00F000F0);
		m.expect_flags(false, false, false, true);
		m.steps(1);
		m.expect_r(6, 0x00FF00FF);
		m.steps(1);
		m.expect_r(0, 0xF000F000);
		m.expect_flags(true, false, false, true);
		m.steps(1);
		m.expect_flags(false, true, false, true);
	}
	{
		Machine m;
		m.program({ dp_imm(MOV, false, 0, 0, 0, 0), dp_imm(MOV, true, 0, 1, 0, 0) });
		m.flags(true, false, true, false);
		m.steps(2);
		m.expect_r(1, 0);
		m.expect_flags(false, true, true, false);   /* C kept: no rotation */
	}
}

/* Shifter operands and carry out. */
void test_shifter()
{
	TEST("shifter immediate");
	{
		Machine m;
		m.flags(false, false, false, false);
		m.program({
			dp_imm(MOV, true, 0, 0, 0xFF, 4),    /* movs r0, #0xff000000: rot 8 -> C = bit 31 */
			dp_imm(MOV, true, 0, 1, 0x80, 4),    /* 0x80000000 -> N, C */
			dp_imm(MOV, true, 0, 2, 0x01, 1),    /* ror 2 -> 0x40000000, C = 0 */
		});
		m.steps(1);
		m.expect_r(0, 0xFF000000);
		m.expect_flags(true, false, true, false);
		m.steps(1);
		m.expect_r(1, 0x80000000);
		m.expect_flags(true, false, true, false);
		m.steps(1);
		m.expect_r(2, 0x40000000);
		m.expect_flags(false, false, false, false);
	}

	TEST("shifter lsl");
	{
		Machine m;
		m.r(1, 0x80000001);
		m.r(2, 32);
		m.r(3, 33);
		m.r(4, 0);
		m.r(5, 1);
		m.program({
			dp_reg(MOV, true, 0, 0, 1, LSL, 0),    /* lsl #0: unchanged, C kept */
			dp_reg(MOV, true, 0, 0, 1, LSL, 1),    /* lsl #1: 2, C = 1 */
			dp_rs(MOV, true, 0, 0, 1, LSL, 2),     /* lsl r2 = 32: 0, C = bit 0 = 1 */
			dp_rs(MOV, true, 0, 0, 1, LSL, 3),     /* lsl r3 = 33: 0, C = 0 */
			dp_rs(MOV, true, 0, 0, 1, LSL, 4),     /* lsl r4 = 0: unchanged, C kept */
			dp_rs(MOV, true, 0, 0, 1, LSL, 5),     /* lsl r5 = 1 */
			dp_reg(MOV, true, 0, 0, 1, LSL, 31),   /* lsl #31: 0x80000000, C = bit 1 = 0 */
		});
		m.flags(false, false, false, false);
		m.steps(1);
		m.expect_r(0, 0x80000001);
		m.expect_flags(true, false, false, false);
		m.steps(1);
		m.expect_r(0, 2);
		m.expect_flags(false, false, true, false);
		m.steps(1);
		m.expect_r(0, 0);
		m.expect_flags(false, true, true, false);
		m.steps(1);
		m.expect_r(0, 0);
		m.expect_flags(false, true, false, false);
		m.flags(false, false, true, false);
		m.steps(1);
		m.expect_r(0, 0x80000001);
		m.expect_flags(true, false, true, false);
		m.steps(1);
		m.expect_r(0, 2);
		m.expect_flags(false, false, true, false);
		m.steps(1);
		m.expect_r(0, 0x80000000);
		m.expect_flags(true, false, false, false);
	}

	TEST("shifter lsr");
	{
		Machine m;
		m.r(1, 0x80000001);
		m.r(2, 32);
		m.r(3, 33);
		m.r(4, 0xFF00 | 1);   /* only the low byte counts: shift by 1 */
		m.program({
			dp_reg(MOV, true, 0, 0, 1, LSR, 0),    /* lsr #32: 0, C = bit 31 */
			dp_reg(MOV, true, 0, 0, 1, LSR, 1),    /* 0x40000000, C = 1 */
			dp_rs(MOV, true, 0, 0, 1, LSR, 2),     /* lsr 32: 0, C = bit 31 */
			dp_rs(MOV, true, 0, 0, 1, LSR, 3),     /* lsr 33: 0, C = 0 */
			dp_rs(MOV, true, 0, 0, 1, LSR, 4),     /* lsr 1 */
		});
		m.steps(1);
		m.expect_r(0, 0);
		m.expect_flags(false, true, true, false);
		m.steps(1);
		m.expect_r(0, 0x40000000);
		m.expect_flags(false, false, true, false);
		m.steps(1);
		m.expect_r(0, 0);
		m.expect_flags(false, true, true, false);
		m.steps(1);
		m.expect_flags(false, true, false, false);
		m.steps(1);
		m.expect_r(0, 0x40000000);
		m.expect_flags(false, false, true, false);
	}

	TEST("shifter asr");
	{
		Machine m;
		m.r(1, 0x80000002);
		m.r(2, 32);
		m.r(3, 40);
		m.r(5, 0x00000002);
		m.program({
			dp_reg(MOV, true, 0, 0, 1, ASR, 0),    /* asr #32: 0xFFFFFFFF, C = bit 31 */
			dp_reg(MOV, true, 0, 0, 1, ASR, 1),    /* 0xC0000001, C = 0 */
			dp_rs(MOV, true, 0, 0, 1, ASR, 2),     /* asr 32 */
			dp_rs(MOV, true, 0, 0, 1, ASR, 3),     /* asr 40: same */
			dp_rs(MOV, true, 0, 0, 5, ASR, 3),     /* positive asr 40: 0, C = 0 */
		});
		m.steps(1);
		m.expect_r(0, 0xFFFFFFFF);
		m.expect_flags(true, false, true, false);
		m.steps(1);
		m.expect_r(0, 0xC0000001);
		m.expect_flags(true, false, false, false);
		m.steps(1);
		m.expect_r(0, 0xFFFFFFFF);
		m.expect_flags(true, false, true, false);
		m.steps(1);
		m.expect_r(0, 0xFFFFFFFF);
		m.expect_flags(true, false, true, false);
		m.steps(1);
		m.expect_r(0, 0);
		m.expect_flags(false, true, false, false);
	}

	TEST("shifter ror rrx");
	{
		Machine m;
		m.r(1, 0x80000001);
		m.r(2, 32);
		m.r(3, 33);
		m.program({
			dp_reg(MOV, true, 0, 0, 1, ROR, 4),    /* ror #4: 0x18000000, C = bit 3 = 0 */
			dp_rs(MOV, true, 0, 0, 1, ROR, 2),     /* ror 32: unchanged, C = bit 31 */
			dp_rs(MOV, true, 0, 0, 1, ROR, 3),     /* ror 33 = ror 1: 0xC0000000, C = 1 */
			dp_reg(MOV, true, 0, 0, 1, ROR, 0),    /* rrx with C = 1: 0xC0000000, C = bit 0 = 1 */
			dp_reg(MOV, true, 0, 4, 0, ROR, 0),    /* rrx of 0xC0000000 with C = 1 */
		});
		m.flags(false, false, false, false);
		m.steps(1);
		m.expect_r(0, 0x18000000);
		m.expect_flags(false, false, false, false);
		m.steps(1);
		m.expect_r(0, 0x80000001);
		m.expect_flags(true, false, true, false);
		m.steps(1);
		m.expect_r(0, 0xC0000000);
		m.expect_flags(true, false, true, false);
		m.steps(1);
		m.expect_r(0, 0xC0000000);
		m.expect_flags(true, false, true, false);
		m.steps(1);
		m.expect_r(4, 0xE0000000);
		m.expect_flags(true, false, false, false);
	}

	TEST("shifter r15 reads");
	{
		Machine m;
		m.r(2, 0);
		m.program({
			dp_reg(MOV, false, 0, 0, 15, LSL, 0),   /* mov r0, pc -> pc + 8 */
			dp_rs(MOV, false, 0, 1, 15, LSL, 2),    /* mov r1, pc, lsl r2 -> pc + 12 */
			dp_imm(ADD, false, 15, 3, 4, 0),        /* add r3, pc, #4 -> pc + 12 */
			dp_rs(ADD, false, 15, 4, 2, LSL, 2),    /* add r4, pc, r2, lsl r2 -> pc + 12 */
		});
		m.steps(4);
		m.expect_r(0, 0x1000 + 8);
		m.expect_r(1, 0x1004 + 12);
		m.expect_r(3, 0x1008 + 12);
		m.expect_r(4, 0x100C + 12);
	}

	TEST("dp pc writes and spsr restore");
	{
		Machine m;   /* SVC mode */
		m.program({
			dp_imm(MOV, false, 0, 15, 0x20, 0),     /* mov pc, #0x20 */
		});
		uint32_t cycles = m.steps(1);
		m.expect_pc(0x20);
		check_eq("mov pc cycles", cycles, 3);
	}
	{
		Machine m;   /* SVC mode, SPSR = user mode with N set */
		odin::Arm7State st{};
		m.cpu.save(st);
		st.spsr[2] = Arm7::FLAG_N | Arm7::MODE_USR;
		st.usr[5] = 0x5555;   /* user r13 */
		st.svc[0] = 0xAAAA;   /* svc r13 */
		m.cpu.load(st);
		m.r(14, 0x2000);
		m.program({ dp_reg(MOV, true, 0, 15, 14, LSL, 0) });   /* movs pc, lr */
		m.steps(1);
		m.expect_pc(0x2000);
		m.expect_cpsr(Arm7::FLAG_N | Arm7::MODE_USR);
		m.expect_r(13, 0x5555);
	}
	{
		Machine m(Arm7::MODE_USR);   /* user mode: no SPSR, flags from the operation */
		m.r(14, 0x3000);
		m.program({ dp_reg(MOV, true, 0, 15, 14, LSL, 0) });
		m.steps(1);
		m.expect_pc(0x3000);
		m.expect_cpsr(Arm7::MODE_USR);
	}
}

/* Multiplies. */
void test_multiply()
{
	TEST("mul mla");
	{
		Machine m;
		m.r(1, 0xFFFFFFFF);
		m.r(2, 3);
		m.r(3, 10);
		m.flags(false, false, true, true);
		m.program({
			mul(false, true, 0, 0, 2, 1),       /* muls r0, r1, r2 = -3 */
			mul(true, true, 4, 3, 2, 1),        /* mlas r4, r1, r2, r3 = -3 + 10 = 7 */
			mul(false, true, 5, 0, 1, 1),       /* muls r5, r1, r1 = 1 */
		});
		m.steps(1);
		m.expect_r(0, 0xFFFFFFFD);
		m.expect_flags(true, false, true, true);   /* C and V untouched */
		m.steps(1);
		m.expect_r(4, 7);
		m.expect_flags(false, false, true, true);
		m.steps(1);
		m.expect_r(5, 1);
	}
	{
		Machine m;
		m.r(1, 0);
		m.r(2, 12345);
		m.program({ mul(false, true, 0, 0, 2, 1) });
		m.steps(1);
		m.expect_r(0, 0);
		m.expect_flags(false, true, false, false);
	}

	TEST("umull umlal");
	{
		Machine m;
		m.r(1, 0xFFFFFFFF);
		m.r(2, 0xFFFFFFFF);
		m.program({
			mull(false, false, true, 3, 4, 2, 1),   /* umulls r4, r3, r1, r2 = 0xFFFFFFFE00000001 */
			mull(false, true, true, 3, 4, 2, 1),    /* umlals: + same -> 0xFFFFFFFC00000002 */
		});
		m.steps(1);
		m.expect_r(3, 0xFFFFFFFE);
		m.expect_r(4, 0x00000001);
		m.expect_flags(true, false, false, false);
		m.steps(1);
		m.expect_r(3, 0xFFFFFFFC);
		m.expect_r(4, 0x00000002);
	}
	{
		Machine m;
		m.r(1, 0);
		m.r(2, 0x12345678);
		m.r(3, 0);
		m.r(4, 0);
		m.program({ mull(false, false, true, 3, 4, 2, 1) });
		m.steps(1);
		m.expect_flags(false, true, false, false);
	}

	TEST("smull smlal");
	{
		Machine m;
		m.r(1, 0xFFFFFFFF);   /* -1 */
		m.r(2, 0x7FFFFFFF);
		m.program({
			mull(true, false, true, 3, 4, 2, 1),    /* smulls = -0x7FFFFFFF = 0xFFFFFFFF80000001 */
			mull(true, true, true, 3, 4, 2, 1),     /* smlals: -0xFFFFFFFE = 0xFFFFFFFF00000002 */
		});
		m.steps(1);
		m.expect_r(3, 0xFFFFFFFF);
		m.expect_r(4, 0x80000001);
		m.expect_flags(true, false, false, false);
		m.steps(1);
		m.expect_r(3, 0xFFFFFFFF);
		m.expect_r(4, 0x00000002);
		m.expect_flags(true, false, false, false);
	}
	{
		Machine m;
		m.r(1, 0x80000000);
		m.r(2, 0x80000000);
		m.program({ mull(true, false, true, 3, 4, 2, 1) });   /* (-2^31)^2 = 2^62 */
		m.steps(1);
		m.expect_r(3, 0x40000000);
		m.expect_r(4, 0);
		m.expect_flags(false, false, false, false);
	}
}

/* Single data transfers. */
void test_single_transfer()
{
	TEST("ldr str word byte");
	{
		Machine m;
		m.r(1, 0x8000);
		m.r(2, 0x12345678);
		m.r(3, 4);
		m.program({
			ldst_imm(false, false, true, true, false, 1, 2, 0x10),   /* str r2, [r1, #0x10] */
			ldst_imm(true, false, true, true, false, 1, 4, 0x10),    /* ldr r4, [r1, #0x10] */
			ldst_imm(false, true, true, true, false, 1, 2, 0x20),    /* strb r2, [r1, #0x20] */
			ldst_imm(true, true, true, true, false, 1, 5, 0x20),     /* ldrb r5, [r1, #0x20] */
			ldst_imm(true, false, true, true, true, 1, 6, 0x10),     /* ldr r6, [r1, #0x10]! */
			ldst_imm(true, false, false, false, false, 1, 7, 0x10),  /* ldr r7, [r1], #-0x10 */
			ldst_reg(false, false, true, true, false, 1, 2, 3, LSL, 2),  /* str r2, [r1, r3, lsl #2] */
			ldst_reg(true, false, true, false, false, 1, 8, 3, LSL, 0),  /* ldr r8, [r1, -r3] */
		});
		m.bus.write32(0x7FFC, 0xCAFEBABE);
		m.steps(1);
		m.expect_mem32(0x8010, 0x12345678);
		m.steps(1);
		m.expect_r(4, 0x12345678);
		m.steps(1);
		m.expect_mem8(0x8020, 0x78);
		m.expect_mem32(0x8020, 0x78);
		m.steps(1);
		m.expect_r(5, 0x78);
		m.steps(1);
		m.expect_r(6, 0x12345678);
		m.expect_r(1, 0x8010);
		m.steps(1);
		m.expect_r(7, 0x12345678);
		m.expect_r(1, 0x8000);
		m.steps(1);
		m.expect_mem32(0x8010, 0x12345678);
		m.steps(1);
		m.expect_r(8, 0xCAFEBABE);
	}

	TEST("ldr unaligned rotate");
	{
		Machine m;
		m.bus.write32(0x8000, 0x11223344);
		m.r(1, 0x8001);
		m.r(2, 0x8002);
		m.r(3, 0x8003);
		m.program({
			ldst_imm(true, false, true, true, false, 1, 4, 0),
			ldst_imm(true, false, true, true, false, 2, 5, 0),
			ldst_imm(true, false, true, true, false, 3, 6, 0),
			ldst_imm(false, false, true, true, false, 1, 4, 0),   /* str to 0x8001 lands at 0x8000 */
		});
		m.steps(3);
		m.expect_r(4, 0x44112233);
		m.expect_r(5, 0x33441122);
		m.expect_r(6, 0x22334411);
		m.steps(1);
		m.expect_mem32(0x8000, 0x44112233);
	}

	TEST("ldr str r15");
	{
		Machine m;
		m.r(1, 0x8000);
		m.bus.write32(0x8100, 0x4000);
		m.bus.write32(0x1004 + 8 + 0x40, 0xDEADBEEF);
		m.program({
			ldst_imm(false, false, true, true, false, 1, 15, 0),      /* str pc, [r1] -> 0x1000 + 12 */
			ldst_imm(true, false, true, true, false, 15, 2, 0x40),    /* ldr r2, [pc, #0x40] */
			ldst_imm(true, false, true, true, false, 1, 15, 0x100),   /* ldr pc, [r1, #0x100] */
		});
		m.steps(1);
		m.expect_mem32(0x8000, 0x100C);
		m.steps(1);
		m.expect_r(2, 0xDEADBEEF);
		uint32_t cycles = m.steps(1);
		m.expect_pc(0x4000);
		check_eq("ldr pc cycles", cycles, 5);
	}

	TEST("ldr rd == rn post-indexed");
	{
		Machine m;
		m.r(1, 0x8000);
		m.bus.write32(0x8000, 0x77);
		m.program({ ldst_imm(true, false, false, true, false, 1, 1, 4) });   /* ldr r1, [r1], #4 */
		m.steps(1);
		m.expect_r(1, 0x77);
	}
	{
		Machine m;
		m.r(1, 0x8000);
		m.program({ ldst_imm(false, false, false, true, false, 1, 1, 4) });  /* str r1, [r1], #4 */
		m.steps(1);
		m.expect_mem32(0x8000, 0x8000);
		m.expect_r(1, 0x8004);
	}

	TEST("ldr cycles");
	{
		Machine m;
		m.r(1, 0x8000);
		m.program({
			ldst_imm(true, false, true, true, false, 1, 2, 0),
			ldst_imm(false, false, true, true, false, 1, 2, 0),
		});
		check_eq("ldr", m.steps(1), 3);
		check_eq("str", m.steps(1), 2);
	}
}

/* Halfword and signed transfers. */
void test_halfword_transfer()
{
	TEST("ldrh strh ldrsb ldrsh");
	{
		Machine m;
		m.r(1, 0x8000);
		m.r(2, 0x1234ABCD);
		m.r(3, 2);
		m.bus.write32(0x8010, 0x80FF7F80);
		m.program({
			ldsth_imm(false, true, true, false, 1, 2, 1, 0x20),   /* strh r2, [r1, #0x20] */
			ldsth_imm(true, true, true, false, 1, 4, 1, 0x20),    /* ldrh r4, [r1, #0x20] */
			ldsth_imm(true, true, true, false, 1, 5, 2, 0x10),    /* ldrsb r5, [r1, #0x10] = 0x80 */
			ldsth_imm(true, true, true, false, 1, 6, 2, 0x12),    /* ldrsb r6 = 0xFF */
			ldsth_imm(true, true, true, false, 1, 7, 3, 0x10),    /* ldrsh r7 = 0x7F80 */
			ldsth_imm(true, true, true, false, 1, 8, 3, 0x12),    /* ldrsh r8 = 0x80FF */
			ldsth_reg(true, true, true, true, 1, 9, 1, 3),        /* ldrh r9, [r1, r3]! */
			ldsth_imm(true, false, true, false, 1, 10, 1, 0x0E),  /* ldrh r10, [r1], #0x0E */
			ldsth_imm(true, true, false, true, 1, 11, 2, 0x0F),   /* ldrsb r11, [r1, #-0x0F]! */
		});
		m.steps(1);
		m.expect_mem32(0x8020, 0xABCD);
		m.steps(1);
		m.expect_r(4, 0xABCD);
		m.steps(1);
		m.expect_r(5, 0xFFFFFF80);
		m.steps(1);
		m.expect_r(6, 0xFFFFFFFF);
		m.steps(1);
		m.expect_r(7, 0x00007F80);
		m.steps(1);
		m.expect_r(8, 0xFFFF80FF);
		m.steps(1);
		m.expect_r(1, 0x8002);
		m.expect_r(9, 0);
		m.steps(1);
		m.expect_r(10, 0);
		m.expect_r(1, 0x8010);
		m.steps(1);
		m.expect_r(1, 0x8001);
	}

	TEST("ldrh odd address");
	{
		Machine m;
		m.bus.write32(0x8000, 0x11223344);
		m.r(1, 0x8001);
		m.program({
			ldsth_imm(true, true, true, false, 1, 2, 1, 0),   /* ldrh from 0x8001: rotate */
			ldsth_imm(true, true, true, false, 1, 3, 3, 0),   /* ldrsh from 0x8001 */
			ldsth_imm(false, true, true, false, 1, 4, 1, 0),  /* strh to 0x8001 lands at 0x8000 */
		});
		m.r(4, 0xBEEF);
		m.steps(1);
		m.expect_r(2, 0x44000033);
		m.steps(1);
		m.expect_r(3, 0x00000033);
		m.steps(1);
		m.expect_mem32(0x8000, 0x1122BEEF);
	}
}

/* Block transfers. */
void test_block_transfer()
{
	TEST("stm ldm addressing modes");
	for (uint32_t mode = 0; mode < 4; mode++) {
		bool p = (mode & 2) != 0;
		bool u = (mode & 1) != 0;
		Machine m;
		m.r(1, 0x8100);
		m.r(2, 0x22);
		m.r(3, 0x33);
		m.r(5, 0x55);
		m.program({
			ldstm(false, p, u, false, true, 1, 0x2C),   /* stm r1!, {r2, r3, r5} */
			dp_imm(MOV, false, 0, 2, 0, 0),
			dp_imm(MOV, false, 0, 3, 0, 0),
			dp_imm(MOV, false, 0, 5, 0, 0),
			ldstm(true, !p, !u, false, true, 1, 0x2C),  /* ldm back with the opposite mode */
		});
		uint32_t cycles = m.steps(1);
		uint32_t low = u ? (p ? 0x8104 : 0x8100) : (p ? 0x8100 - 12 : 0x8100 - 8);
		char what[32];
		std::snprintf(what, sizeof what, "stm mode %u low", mode);
		check_eq(what, m.bus.read32(low), 0x22);
		check_eq(what, m.bus.read32(low + 4), 0x33);
		check_eq(what, m.bus.read32(low + 8), 0x55);
		check_eq("stm writeback", m.cpu.reg(1), u ? 0x8100 + 12 : 0x8100 - 12);
		check_eq("stm cycles", cycles, 4);
		m.steps(3);
		cycles = m.steps(1);
		m.expect_r(1, 0x8100);
		m.expect_r(2, 0x22);
		m.expect_r(3, 0x33);
		m.expect_r(5, 0x55);
		check_eq("ldm cycles", cycles, 5);
	}

	TEST("stm base in list");
	{
		Machine m;
		m.r(1, 0x8100);
		m.r(2, 0x22);
		m.program({ ldstm(false, true, false, false, true, 1, 0x06) });   /* stmdb r1!, {r1, r2}: base first */
		m.steps(1);
		m.expect_mem32(0x80F8, 0x8100);
		m.expect_mem32(0x80FC, 0x22);
		m.expect_r(1, 0x80F8);
	}
	{
		Machine m;
		m.r(0, 0x00);
		m.r(1, 0x8100);
		m.program({ ldstm(false, true, false, false, true, 1, 0x03) });   /* stmdb r1!, {r0, r1}: base second */
		m.steps(1);
		m.expect_mem32(0x80F8, 0);
		m.expect_mem32(0x80FC, 0x80F8);
	}

	TEST("ldm base in list");
	{
		Machine m;
		m.r(1, 0x8100);
		m.bus.write32(0x8100, 0x1111);
		m.bus.write32(0x8104, 0x9999);
		m.program({ ldstm(true, false, true, false, true, 1, 0x03) });   /* ldmia r1!, {r0, r1} */
		m.steps(1);
		m.expect_r(0, 0x1111);
		m.expect_r(1, 0x9999);
	}

	TEST("stm ldm r15");
	{
		Machine m;
		m.r(1, 0x8100);
		m.program({
			ldstm(false, false, true, false, false, 1, 0x8001),   /* stmia r1, {r0, pc} */
			ldstm(true, false, true, false, false, 1, 0x8001),    /* ldmia r1, {r0, pc} -> pc = 0x100C */
		});
		m.steps(1);
		m.expect_mem32(0x8104, 0x100C);
		uint32_t cycles = m.steps(1);
		m.expect_pc(0x100C);
		check_eq("ldm pc cycles", cycles, 6);
	}

	TEST("ldm stm user bank");
	{
		Machine m;   /* SVC mode */
		odin::Arm7State st{};
		m.cpu.save(st);
		st.usr[5] = 0x0D13;
		st.usr[6] = 0x0D14;
		st.svc[0] = 0x5013;
		st.svc[1] = 0x5014;
		m.cpu.load(st);
		m.r(1, 0x8100);
		m.program({
			ldstm(false, false, true, true, false, 1, 0x6000),   /* stmia r1, {r13, r14}^ */
			ldstm(true, false, true, true, false, 1, 0x3000),    /* ldmia r1, {r12, r13}^ */
		});
		m.steps(1);
		m.expect_mem32(0x8100, 0x0D13);
		m.expect_mem32(0x8104, 0x0D14);
		m.expect_r(13, 0x5013);
		m.steps(1);
		m.cpu.save(st);
		check_eq("user r12", st.usr[4], 0x0D13);
		check_eq("user r13", st.usr[5], 0x0D14);
		m.expect_r(13, 0x5013);
	}
	{
		Machine m(Arm7::MODE_FIQ | Arm7::FLAG_I | Arm7::FLAG_F);
		odin::Arm7State st{};
		m.cpu.save(st);
		st.usr[0] = 0x0008;
		st.fiq[0] = 0xF008;
		m.cpu.load(st);
		m.r(1, 0x8100);
		m.program({ ldstm(false, false, true, true, false, 1, 0x0100) });   /* stmia r1, {r8}^ in FIQ */
		m.steps(1);
		m.expect_mem32(0x8100, 0x0008);
		m.expect_r(8, 0xF008);
	}

	TEST("ldm pc with spsr restore");
	{
		Machine m;   /* SVC */
		odin::Arm7State st{};
		m.cpu.save(st);
		st.spsr[2] = Arm7::FLAG_Z | Arm7::MODE_IRQ;
		st.irq[0] = 0x1D13;
		m.cpu.load(st);
		m.r(1, 0x8100);
		m.bus.write32(0x8100, 0x77);
		m.bus.write32(0x8104, 0x3000);
		m.program({ ldstm(true, false, true, true, false, 1, 0x8004) });   /* ldmia r1, {r2, pc}^ */
		m.steps(1);
		m.expect_r(2, 0x77);
		m.expect_pc(0x3000);
		m.expect_cpsr(Arm7::FLAG_Z | Arm7::MODE_IRQ);
		m.expect_r(13, 0x1D13);
	}

	TEST("ldm stm empty list");
	{
		Machine m;
		m.r(1, 0x8100);
		m.program({ ldstm(false, false, true, false, true, 1, 0) });   /* stmia r1!, {} */
		m.steps(1);
		m.expect_mem32(0x8100, 0x100C);
		m.expect_r(1, 0x8140);
	}
}

/* Swap. */
void test_swap()
{
	TEST("swp swpb");
	{
		Machine m;
		m.r(1, 0x8000);
		m.r(2, 0xAABBCCDD);
		m.bus.write32(0x8000, 0x11223344);
		m.program({
			swp(false, 1, 3, 2),      /* swp r3, r2, [r1] */
			swp(true, 1, 4, 3),       /* swpb r4, r3, [r1] */
			swp(false, 1, 2, 2),      /* swp r2, r2, [r1]: rd == rm */
		});
		uint32_t cycles = m.steps(1);
		m.expect_r(3, 0x11223344);
		m.expect_mem32(0x8000, 0xAABBCCDD);
		check_eq("swp cycles", cycles, 4);
		m.steps(1);
		m.expect_r(4, 0xDD);
		m.expect_mem32(0x8000, 0xAABBCC44);
		m.steps(1);
		m.expect_r(2, 0xAABBCC44);
		m.expect_mem32(0x8000, 0xAABBCCDD);
	}
	{
		Machine m;
		m.r(1, 0x8001);
		m.r(2, 0x01020304);
		m.bus.write32(0x8000, 0x11223344);
		m.program({ swp(false, 1, 3, 2) });
		m.steps(1);
		m.expect_r(3, 0x44112233);
		m.expect_mem32(0x8000, 0x01020304);
	}
}

/* Branches. */
void test_branches()
{
	TEST("b bl bx");
	{
		Machine m;
		m.program({
			branch(false, 2),        /* b +8 -> 0x1010 */
			NOP, NOP, NOP,
			branch(true, -4),        /* at 0x1010: bl 0x1008 */
			NOP,
		});
		uint32_t cycles = m.steps(1);
		m.expect_pc(0x1010);
		check_eq("b cycles", cycles, 3);
		m.steps(1);
		m.expect_pc(0x1008);
		m.expect_r(14, 0x1014);
	}
	{
		Machine m;
		m.r(0, 0x2000);
		m.r(1, 0x3001);
		m.program({ bx(0) });
		m.program({ bx(1) }, 0x2000);
		m.steps(1);
		m.expect_pc(0x2000);
		m.steps(1);
		m.expect_pc(0x3000);   /* Thumb refused, ARM state kept */
		check_eq("t bit", m.cpu.cpsr() & Arm7::FLAG_T, 0);
	}
	{
		Machine m;
		m.program({ branch(false, 0xFFFFFF - 1) });   /* b . - 4: offset -2 words */
		m.steps(1);
		m.expect_pc(0x1000);
	}
}

/* PSR transfers. */
void test_psr()
{
	TEST("mrs msr");
	{
		Machine m;   /* SVC, I F set */
		m.r(1, Arm7::FLAG_N | Arm7::FLAG_C | Arm7::MODE_IRQ);
		m.r(2, 0x1234);
		m.program({
			mrs(false, 0),                    /* mrs r0, cpsr */
			msr_reg(false, 0x9, 1),           /* msr cpsr_fc, r1 -> IRQ mode, N C, I F cleared */
			mrs(false, 3),
			msr_imm(false, 0x8, 0x0F, 2),     /* msr cpsr_f, #0xF0000000 */
			msr_reg(true, 0xF, 2),            /* msr spsr_fsxc, r2 */
			mrs(true, 4),                     /* mrs r4, spsr */
			msr_imm(false, 0x1, 0xD3, 0),     /* msr cpsr_c, #0xD3 -> back to SVC */
			mrs(false, 5),
			msr_imm(false, 0x1, 0xD2, 0),     /* -> IRQ */
			mrs(true, 6),                     /* spsr_irq = 0x1234 */
		});
		odin::Arm7State st{};
		m.cpu.save(st);
		st.svc[0] = 0x5013;
		st.irq[0] = 0x1D13;
		m.cpu.load(st);
		m.steps(1);
		m.expect_r(0, 0xD3);
		m.steps(1);
		m.expect_cpsr(Arm7::FLAG_N | Arm7::FLAG_C | Arm7::MODE_IRQ);
		m.expect_r(13, 0x1D13);
		m.steps(1);
		m.expect_r(3, Arm7::FLAG_N | Arm7::FLAG_C | Arm7::MODE_IRQ);
		m.steps(1);
		m.expect_cpsr(0xF0000000 | Arm7::MODE_IRQ);
		m.steps(2);
		m.expect_r(4, 0x1234);
		m.steps(2);
		m.expect_r(5, 0xF00000D3);
		m.expect_r(13, 0x5013);
		m.steps(2);
		m.expect_r(6, 0x1234);
	}

	TEST("msr in user mode");
	{
		Machine m(Arm7::MODE_USR);
		m.r(1, Arm7::FLAG_Z | Arm7::FLAG_I | Arm7::MODE_SVC);
		m.r(2, 0x77);
		m.program({
			msr_reg(false, 0xF, 1),   /* only the flags move */
			msr_reg(true, 0xF, 2),    /* no SPSR: dropped */
			mrs(true, 3),             /* reads the CPSR */
		});
		m.steps(1);
		m.expect_cpsr(Arm7::FLAG_Z | Arm7::MODE_USR);
		m.steps(2);
		m.expect_r(3, Arm7::FLAG_Z | Arm7::MODE_USR);
	}
}

/* Exceptions: SWI, undefined, IRQ, FIQ, vectors, link values, banks. */
void test_exceptions()
{
	TEST("swi");
	{
		Machine m(Arm7::FLAG_N | Arm7::MODE_USR);
		m.r(13, 0x0D13);
		m.program({ swi(0x123456), NOP });
		m.program({ dp_reg(MOV, true, 0, 15, 14, LSL, 0) }, 0x08);   /* movs pc, lr */
		uint32_t cycles = m.steps(1);
		m.expect_pc(0x08);
		m.expect_mode(Arm7::MODE_SVC);
		m.expect_r(14, 0x1004);
		check_eq("spsr", m.cpu.spsr(), Arm7::FLAG_N | Arm7::MODE_USR);
		check_eq("i bit", m.cpu.cpsr() & Arm7::FLAG_I, Arm7::FLAG_I);
		check_eq("f bit kept", m.cpu.cpsr() & Arm7::FLAG_F, 0);
		check_eq("swi cycles", cycles, 3);
		m.steps(1);
		m.expect_pc(0x1004);
		m.expect_cpsr(Arm7::FLAG_N | Arm7::MODE_USR);
		m.expect_r(13, 0x0D13);
	}

	TEST("undefined");
	{
		Machine m;
		m.program({ 0xE7FFFFFF, NOP });   /* undefined: bits 27-25 = 011, bit 4 = 1 */
		m.steps(1);
		m.expect_pc(0x04);
		m.expect_mode(Arm7::MODE_UND);
		m.expect_r(14, 0x1004);
	}
	{
		Machine m;
		m.program({ MCR_CP15 });
		m.steps(1);
		m.expect_pc(0x04);
		m.expect_mode(Arm7::MODE_UND);
	}
	{
		Machine m;
		m.program({ 0xEC000000 });   /* stc */
		m.steps(1);
		m.expect_pc(0x04);
	}

	TEST("irq");
	{
		Machine m(Arm7::MODE_SVC | Arm7::FLAG_F);   /* I clear */
		m.r(13, 0x5013);
		m.r(14, 0x5014);
		m.program({ NOP, NOP, NOP });
		m.program({ dp_imm(SUB, true, 14, 15, 4, 0) }, 0x18);   /* subs pc, lr, #4 */
		m.cpu.set_irq(true);
		/* One step takes the exception (3 cycles) and runs the vector
		 * instruction subs pc, lr, #4 (3 cycles), back to 0x1000. */
		uint32_t cycles = m.steps(1);
		m.expect_pc(0x1000);
		check_eq("irq entry + return cycles", cycles, 3 + 3);
		m.expect_mode(Arm7::MODE_SVC);
		m.expect_r(13, 0x5013);
		m.expect_r(14, 0x5014);
	}
	{
		Machine m(Arm7::MODE_SVC | Arm7::FLAG_F);
		m.program({ NOP, NOP });
		m.program({ NOP }, 0x18);
		m.cpu.set_irq(true);
		m.steps(1);
		m.expect_pc(0x1C);
		m.expect_mode(Arm7::MODE_IRQ);
		m.expect_r(14, 0x1004);
		check_eq("spsr", m.cpu.spsr(), Arm7::MODE_SVC | Arm7::FLAG_F);
		check_eq("i set", m.cpu.cpsr() & Arm7::FLAG_I, Arm7::FLAG_I);
		m.cpu.set_irq(false);
	}
	{
		Machine m;   /* I set: masked */
		m.program({ NOP, NOP });
		m.cpu.set_irq(true);
		m.steps(1);
		m.expect_pc(0x1004);
		m.expect_mode(Arm7::MODE_SVC);
	}

	TEST("fiq");
	{
		Machine m(Arm7::MODE_SVC);   /* I and F clear */
		odin::Arm7State st{};
		m.cpu.save(st);
		st.fiq[0] = 0xF8;
		st.fiq[5] = 0xF13;
		m.cpu.load(st);
		m.r(8, 0x08);
		m.program({ NOP, NOP });
		m.program({ NOP }, 0x1C);
		m.cpu.set_fiq(true);
		m.cpu.set_irq(true);   /* FIQ wins */
		m.steps(1);
		m.expect_pc(0x20);
		m.expect_mode(Arm7::MODE_FIQ);
		m.expect_r(14, 0x1004);
		m.expect_r(8, 0xF8);
		m.expect_r(13, 0xF13);
		check_eq("i f set", m.cpu.cpsr() & (Arm7::FLAG_I | Arm7::FLAG_F), Arm7::FLAG_I | Arm7::FLAG_F);
		m.cpu.set_fiq(false);
		m.program({ dp_imm(SUB, true, 14, 15, 4, 0) }, 0x20);
		m.steps(1);
		m.expect_pc(0x1000);
		m.expect_mode(Arm7::MODE_SVC);
		m.expect_r(8, 0x08);
		/* IRQ is still pending and now unmasked. */
		m.steps(1);
		m.expect_pc(0x1C);
		m.expect_mode(Arm7::MODE_IRQ);
	}

	TEST("reset");
	{
		Machine m;
		m.r(0, 5);
		m.cpu.reset();
		m.expect_pc(0);
		m.expect_cpsr(Arm7::FLAG_I | Arm7::FLAG_F | Arm7::MODE_SVC);
		m.expect_r(0, 0);
	}
}

/* Save and load keep every bank. */
void test_save_load()
{
	TEST("save load");
	Machine m;
	odin::Arm7State st{};
	m.cpu.save(st);
	for (uint32_t i = 0; i < 8; i++) {
		st.r[i] = 0x100 + i;
	}
	for (uint32_t i = 0; i < 7; i++) {
		st.usr[i] = 0x200 + i;
		st.fiq[i] = 0x300 + i;
	}
	st.irq[0] = 0x400; st.irq[1] = 0x401;
	st.svc[0] = 0x500; st.svc[1] = 0x501;
	st.abt[0] = 0x600; st.abt[1] = 0x601;
	st.und[0] = 0x700; st.und[1] = 0x701;
	for (uint32_t i = 0; i < 5; i++) {
		st.spsr[i] = 0x800 + i;
	}
	st.cpsr = Arm7::MODE_FIQ;
	st.pc = 0x1234;
	m.cpu.load(st);
	m.expect_r(8, 0x300);
	m.expect_r(13, 0x305);
	m.program({ msr_imm(false, 1, 0x17, 0), msr_imm(false, 1, 0x1B, 0), msr_imm(false, 1, 0x12, 0) }, 0x1234);
	m.steps(1);
	m.expect_r(8, 0x200);
	m.expect_r(13, 0x600);
	m.steps(1);
	m.expect_r(14, 0x701);
	m.steps(1);
	m.expect_r(13, 0x400);
	odin::Arm7State back{};
	m.cpu.save(back);
	back.cpsr = st.cpsr;
	back.pc = st.pc;
	check_eq("state round trip", std::memcmp(&back, &st, sizeof st) == 0 ? 1u : 0u, 1u);
}

/* Trace format: one fetch line and one line per data access. */
void test_trace()
{
	TEST("trace");
	Machine m;
	m.r(1, 0x8000);
	m.bus.write32(0x8000, 0xCAFE0001);
	m.program({ ldst_imm(true, false, true, true, false, 1, 2, 0), ldst_imm(false, true, true, true, false, 1, 2, 5) });
	char buf[256] = {};
	FILE *fp = fmemopen(buf, sizeof buf, "w");
	m.cpu.set_trace(fp);
	m.steps(2);
	m.cpu.set_trace(nullptr);
	std::fclose(fp);
	const char *want =
		"arm7 00001000 f 00001000 4 e5912000\n"
		"arm7 00001000 r 00008000 4 cafe0001\n"
		"arm7 00001004 f 00001004 4 e5c12005\n"
		"arm7 00001004 w 00008005 1 01\n";
	if (std::strcmp(buf, want) != 0) {
		fail(std::string("trace text:\n") + buf);
	}
	g_checks++;
}

/* Turns counted without being run: the same program run with the turns
 * counted and with every turn run must leave the same registers, the same
 * memory and the same count of cycles, whatever the machine does meanwhile.
 * The bus tells whether the turns were counted: a turn that is only counted
 * does not read it. */

class CountingBus : public RamBus {
public:
	uint32_t reads = 0;

	uint32_t read32(uint32_t addr) override
	{
		reads++;
		return RamBus::read32(addr);
	}
};

struct TurnsRun {
	odin::Arm7State state;
	std::vector<uint8_t> ram;
	uint32_t bus_reads;
	uint32_t seen;	/* the count as the machine read it on the way */
	uint64_t instructions;
	uint64_t recorded, counted;	/* turns run to be recorded, turns counted */
	uint64_t watched;	/* a word of the memory as the machine read it before each call */
};

/* What the machine does besides, in a run of turns: an interrupt, held for
 * one call out of `fiq_every`, a byte it writes at a call, and another
 * answer of the bus from a call on. The handler of the interrupt counts in
 * a register of its own, in a register of the program, or in the count,
 * which it then loads into the User r13 or r9; or it calls the supervisor
 * with the carry set every other time, which leaves the SVC mode another
 * saved status. */
enum class Handler { Own, Shared, User13, User9, Supervisor };
struct TurnsEvents {
	uint32_t fiq_every = 0;
	uint32_t poke_call = 0, poke_at = 0;
	uint8_t poke = 0;
	uint32_t bus_call = 0;
	uint8_t bus_answer = 0;
	Handler handler = Handler::Own;
	/* The flags the program starts with, and a byte of the memory */
	uint32_t flags = 0;
	uint32_t first_at = 0;
	uint8_t first = 0;
	/* A word the machine reads before each call */
	uint32_t watch = 0;
	/* Calls of 512 cycles */
	uint32_t calls = 300;
};

constexpr uint32_t TURN_CODE = 0x1000, TURN_COUNT = 0x8000, TURN_FLAG = 0x8010;

/* 300 calls of 512 cycles, unless told, as a sound board runs its ARM7. The machine
 * reads the count at call 100, sets the flag the loop waits for at call
 * `flag_at` and, if asked, writes the count at call 150. */
/* The ways an ARM7 runs: step() alone, what was decoded, blocks of host code */
enum class Way { Read, Decoded, Host };

void set_way(Arm7 &cpu, Way way)
{
	cpu.set_decoded(way != Way::Read);
	cpu.set_translate(way == Way::Host);
}

TurnsRun run_turns_with(Way way, bool counted, const std::vector<uint32_t> &code, uint32_t count, uint32_t flag_at,
                        bool write_count, uint32_t set_byte, const TurnsEvents &events)
{
	CountingBus bus;
	Arm7 cpu(bus);
	set_way(cpu, way);
	TurnsRun out{};
	out.ram.assign(0x10000, 0);

	for (size_t i = 0; i < code.size(); i++) {
		for (uint32_t b = 0; b < 4; b++) {
			out.ram[TURN_CODE + i * 4 + b] = static_cast<uint8_t>(code[i] >> (8 * b));
		}
	}
	for (uint32_t b = 0; b < 4; b++) {
		out.ram[TURN_COUNT + b] = static_cast<uint8_t>(count >> (8 * b));
	}
	if (set_byte) {
		out.ram[set_byte] = 1;
	}
	if (events.first_at) {
		out.ram[events.first_at] = events.first;
	}
	std::vector<uint32_t> handler;
	switch (events.handler) {
	case Handler::Own:
		handler = { dp_imm(ADD, false, 8, 8, 1, 0) };				/* add r8, r8, #1 */
		break;
	case Handler::Shared:
		handler = { dp_imm(ADD, false, 6, 6, 1, 0) };				/* add r6, r6, #1 */
		break;
	case Handler::User13:
	case Handler::User9:
		handler = {
			ldst_imm(true, false, true, true, false, 7, 8, 0),		/* ldr r8, [r7] */
			dp_imm(ADD, false, 8, 8, 1, 0),
			ldst_imm(false, false, true, true, false, 7, 8, 0),		/* str r8, [r7] */
			/* ldmia r7, {r13}^ or {r9}^ */
			ldstm(true, false, true, true, false, 7, 1u << (events.handler == Handler::User13 ? 13 : 9)),
		};
		break;
	case Handler::Supervisor: {
		const uint32_t back = dp_reg(MOV, true, 0, 15, 14, LSL, 0);		/* at 08: movs pc, lr */

		for (uint32_t b = 0; b < 4; b++) {
			out.ram[0x08 + b] = static_cast<uint8_t>(back >> (8 * b));
		}
		handler = {
			dp_imm(EOR, false, 8, 8, 1, 0),					/* eor r8, r8, #1 */
			dp_reg(MOV, true, 0, 9, 8, LSR, 1),				/* movs r9, r8, lsr #1 */
			swi(0),
		};
		break;
	}
	}
	handler.push_back(dp_imm(SUB, true, 14, 15, 4, 0));				/* subs pc, lr, #4 */
	for (size_t i = 0; i < handler.size(); i++) {
		for (uint32_t b = 0; b < 4; b++) {
			out.ram[0x1C + i * 4 + b] = static_cast<uint8_t>(handler[i] >> (8 * b));
		}
	}
	cpu.set_ram({out.ram.data(), 0x10000, 0xFFFF});

	odin::Arm7State st{};
	cpu.save(st);
	st.pc = TURN_CODE;
	st.cpsr = Arm7::MODE_SVC | Arm7::FLAG_I | (events.fiq_every ? 0 : Arm7::FLAG_F) | events.flags;
	cpu.load(st);
	cpu.set_reg(1, TURN_FLAG);
	cpu.set_reg(4, 0x00800000);
	cpu.set_reg(7, TURN_COUNT);
	cpu.set_turn_replay(counted);

	for (uint32_t call = 0; call < events.calls; call++) {
		if (call == 100) {
			cpu.ram_read(TURN_COUNT, 4);
			out.seen = static_cast<uint32_t>(out.ram[TURN_COUNT]) | (static_cast<uint32_t>(out.ram[TURN_COUNT + 1]) << 8)
				| (static_cast<uint32_t>(out.ram[TURN_COUNT + 2]) << 16) | (static_cast<uint32_t>(out.ram[TURN_COUNT + 3]) << 24);
		}
		if (call == 150 && write_count) {
			cpu.ram_write(TURN_COUNT, 4);
			out.ram[TURN_COUNT] = 0x10;
			out.ram[TURN_COUNT + 1] = 0x27;
			out.ram[TURN_COUNT + 2] = 0;
			out.ram[TURN_COUNT + 3] = 0;
		}
		if (call == flag_at) {
			cpu.ram_write(TURN_FLAG, 1);
			out.ram[TURN_FLAG] = 1;
		}
		if (events.poke_at && call == events.poke_call) {
			cpu.ram_write(events.poke_at, 1);
			out.ram[events.poke_at] = events.poke;
		}
		if (events.bus_call && call == events.bus_call) {
			bus.mem[0] = events.bus_answer;
		}
		if (events.fiq_every) {
			cpu.set_fiq(call % events.fiq_every == events.fiq_every - 1);
		}
		if (events.watch) {
			cpu.ram_read(events.watch, 4);
			for (uint32_t b = 0; b < 4; b++) {
				out.watched = out.watched * 0x100000001B3ull + out.ram[events.watch + b];
			}
		}
		cpu.run(512);
	}
	cpu.settle();
	cpu.save(out.state);
	out.bus_reads = bus.reads;
	out.instructions = cpu.instructions();
	out.recorded = cpu.turns_recorded();
	out.counted = cpu.turns_counted();
	return out;
}

/* The same with what was decoded, with host code and with step() alone:
 * the same in all. */
TurnsRun run_turns(bool counted, const std::vector<uint32_t> &code, uint32_t count, uint32_t flag_at, bool write_count = false,
                   uint32_t set_byte = 0, const TurnsEvents &events = {})
{
	const TurnsRun slow = run_turns_with(Way::Read, counted, code, count, flag_at, write_count, set_byte, events);
	const auto same = [&slow](const TurnsRun &other) {
		return other.state == slow.state && other.ram == slow.ram && other.bus_reads == slow.bus_reads &&
		       other.seen == slow.seen && other.instructions == slow.instructions && other.recorded == slow.recorded &&
		       other.counted == slow.counted && other.watched == slow.watched;
	};
	const TurnsRun fast = run_turns_with(Way::Decoded, counted, code, count, flag_at, write_count, set_byte, events);
	const TurnsRun host = run_turns_with(Way::Host, counted, code, count, flag_at, write_count, set_byte, events);

	g_checks += 2;
	if (!same(fast)) {
		fail("from what was decoded, not as step()");
	}
	if (!same(host)) {
		fail("as host code, not as step()");
	}
	return fast;
}

/* Both ways; returns how many times fewer the bus was read with the turns
 * counted, in tenths. */
uint32_t check_turns(const char *name, const std::vector<uint32_t> &code, uint32_t count, uint32_t flag_at, bool write_count = false)
{
	const TurnsRun counted = run_turns(true, code, count, flag_at, write_count);
	const TurnsRun run = run_turns(false, code, count, flag_at, write_count);

	g_current = name;
	g_checks += 4;
	if (!(counted.state == run.state)) {
		fail("the registers differ");
	}
	if (counted.ram != run.ram) {
		fail("the memory differs");
	}
	if (counted.seen != run.seen) {
		fail("the machine read another count on the way");
	}
	if (counted.instructions != run.instructions) {
		fail("another number of instructions");
	}
	return run.bus_reads * 10 / (counted.bus_reads ? counted.bus_reads : 1);
}

void test_turns()
{
	const uint32_t EQ = 0, CS = 2;
	/* What every loop here ends with: a read of the bus, then back to the
	 * start while the flag is clear, else a jump onto itself. */
	const auto wait = [EQ](int32_t back) {
		return std::vector<uint32_t>{
			ldst_imm(true, false, true, true, false, 4, 3, 0),	/* ldr r3, [r4] */
			ldst_imm(true, true, true, true, false, 1, 2, 0),	/* ldrb r2, [r1] */
			dp_imm(CMP, true, 2, 0, 0, 0),				/* cmp r2, #0 */
			branch(false, back, EQ),				/* beq start */
			branch(false, -2),					/* b . */
		};
	};
	const auto with_wait = [&wait](std::vector<uint32_t> code) {
		const std::vector<uint32_t> tail = wait(-static_cast<int32_t>(code.size()) - 5);
		code.insert(code.end(), tail.begin(), tail.end());
		return code;
	};
	const uint32_t load = ldst_imm(true, false, true, true, false, 7, 0, 0);	/* ldr r0, [r7] */
	const uint32_t add1 = dp_imm(ADD, false, 0, 0, 1, 0);				/* add r0, r0, #1 */
	const uint32_t store = ldst_imm(false, false, true, true, false, 7, 0, 0);	/* str r0, [r7] */
	const uint32_t drop = dp_imm(MOV, false, 0, 0, 0, 0);				/* mov r0, #0 */

	uint32_t fewer = check_turns("a turn that only waits", with_wait({}), 0, 250);
	g_checks++;
	if (fewer < 100) {
		fail("the turns were not counted");
	}

	fewer = check_turns("a turn that counts itself", with_wait({load, add1, store, drop}), 5, 250);
	g_checks++;
	if (fewer < 100) {
		fail("the turns were not counted");
	}
	check_turns("a count the machine writes on the way", with_wait({load, add1, store, drop}), 5, 250, true);
	check_turns("a count that wraps", with_wait({load, add1, store, drop}), 0xFFFFFFF0, 250);
	check_turns("a count carried through a move", with_wait({load, dp_reg(MOV, false, 0, 6, 0, LSL, 0),
	            dp_imm(SUB, false, 6, 6, 3, 0), ldst_imm(false, false, true, true, false, 7, 6, 0), drop,
	            dp_imm(MOV, false, 0, 6, 0, 0)}), 1000, 250);

	/* Turns that look at their count: they must not be counted, or
	 * the end below would be missed. */
	check_turns("a turn that leaves at a count", {
		load, add1, store,
		dp_imm(CMP, true, 0, 0, 200, 0),			/* cmp r0, #200 */
		drop,
		branch(false, 4, EQ),					/* beq out */
		ldst_imm(true, false, true, true, false, 4, 3, 0),
		ldst_imm(true, true, true, true, false, 1, 2, 0),
		dp_imm(CMP, true, 2, 0, 0, 0),
		branch(false, -11, EQ),
		branch(false, -2),					/* out: b . */
	}, 0, 400);
	check_turns("a turn that notes its carry", with_wait({load, dp_imm(ADD, true, 0, 0, 1, 0), store, drop,
	            dp_imm(MOV, false, 0, 5, 1, 0, CS)}), 0xFFFFFF80, 250);	/* adds; movcs r5, #1 */
	check_turns("a count kept twice", with_wait({load, add1, store, ldst_imm(false, false, true, true, false, 7, 0, 8), drop}), 0, 250);
	check_turns("a count of one byte", with_wait({ldst_imm(true, true, true, true, false, 7, 0, 0), add1,
	            ldst_imm(false, true, true, true, false, 7, 0, 0), drop}), 250, 250);
	/* Read as a byte and written as a word, the count starts again at 1
	 * after 256. */
	check_turns("a count read as a byte", with_wait({ldst_imm(true, true, true, true, false, 7, 0, 0), add1, store, drop}), 250, 250);
	/* Two words that count: only one may be owed its turns. */
	check_turns("two counts", with_wait({load, add1, store, ldst_imm(true, false, true, true, false, 7, 0, 8),
	            dp_imm(ADD, false, 0, 0, 2, 0), ldst_imm(false, false, true, true, false, 7, 0, 8), drop}), 0, 250);
	/* The count picks the byte the loop waits for: it ends at 2000, long
	 * after a turn was recorded. The MOVNE does not run, the flags being
	 * those of the CMP that found the byte clear: the register keeps the
	 * count. */
	for (const bool with_movne : { false, true }) {
		std::vector<uint32_t> code = {load, add1, store};
		if (with_movne) {
			code.push_back(dp_imm(MOV, false, 0, 0, 0, 0, 1));	/* movne r0, #0 */
		}
		code.push_back(ldst_reg(true, true, true, true, false, 1, 2, 0, LSL, 0));	/* ldrb r2, [r1, r0] */
		code.push_back(drop);
		code.push_back(ldst_imm(true, false, true, true, false, 4, 3, 0));
		code.push_back(dp_imm(CMP, true, 2, 0, 0, 0));
		code.push_back(branch(false, -static_cast<int32_t>(code.size()) - 2, EQ));
		code.push_back(branch(false, -2));
		const TurnsRun counted = run_turns(true, code, 0, 1000, false, TURN_FLAG + 2000);
		const TurnsRun run = run_turns(false, code, 0, 1000, false, TURN_FLAG + 2000);
		g_current = with_movne ? "a count behind an instruction that does not run" : "a count used as an index";
		g_checks += 2;
		if (!(counted.state == run.state) || counted.ram != run.ram) {
			fail("differs from every turn run");
		}
		if ((run.ram[TURN_COUNT] | (run.ram[TURN_COUNT + 1] << 8)) != 2000) {
			fail("the loop did not end at a count of 2000");
		}
	}

	/* The low byte of the count picks the byte the loop waits for. */
	const std::vector<uint32_t> picks = {
		load, add1, store,
		dp_imm(AND, false, 0, 6, 0xFF, 0),			/* and r6, r0, #0xFF */
		drop,
		ldst_imm(true, false, true, true, false, 4, 3, 0),
		ldst_reg(true, true, true, true, false, 1, 2, 6, LSL, 0),	/* ldrb r2, [r1, r6] */
		dp_imm(MOV, false, 0, 6, 0, 0),				/* mov r6, #0 */
		dp_imm(CMP, true, 2, 0, 0, 0),
		branch(false, -11, EQ),
		branch(false, -2),
	};
	{
		/* The flag is at +77 from the start: the loop ends at a
		 * count of 77 and no sooner. */
		const TurnsRun counted = run_turns(true, picks, 0, 1000, false, TURN_FLAG + 77);
		const TurnsRun run = run_turns(false, picks, 0, 1000, false, TURN_FLAG + 77);
		g_current = "a count used as an address";
		g_checks += 3;
		if (!(counted.state == run.state) || counted.ram != run.ram) {
			fail("differs from every turn run");
		}
		if (run.ram[TURN_COUNT] != 77 || run.state.pc != TURN_CODE + 10 * 4) {
			char buf[96];
			std::snprintf(buf, sizeof buf, "the loop ended at a count of %u, pc %08x", run.ram[TURN_COUNT], run.state.pc);
			fail(buf);
		}
	}

	/* A turn that was counted is taken again when the program is back in
	 * it: an interrupt every ten calls does not have it recorded each
	 * time. Then what must stop that: something the turn read is no
	 * longer the same, or what it leaves in the memory is no longer
	 * there; the turn has to run. */
	const auto kept = [&](const char *name, const std::vector<uint32_t> &code, TurnsEvents events, uint64_t most,
	                      uint32_t turn) {	/* turn 0: however few are counted */
		/* The flags the CMP of the wait leaves: the first turn is
		 * then one from its first instruction on. The word the
		 * turns of these programs write is looked at all along. */
		events.flags = 0x60000000;
		if (!events.watch) {
			events.watch = TURN_COUNT + 20;
		}
		const TurnsRun counted = run_turns(true, code, 0, 1000, false, 0, events);
		const TurnsRun run = run_turns(false, code, 0, 1000, false, 0, events);

		g_current = name;
		g_checks += 5;
		if (!(counted.state == run.state) || counted.ram != run.ram || counted.instructions != run.instructions) {
			fail("differs from every turn run");
		}
		if (counted.watched != run.watched) {
			fail("the machine read something else on the way");
		}
		if (run.recorded != 0 || run.counted != 0) {
			fail("turns recorded or counted with the counting off");
		}
		if (counted.recorded == 0 || counted.recorded > most) {
			char buf[64];
			std::snprintf(buf, sizeof buf, "%u turns were recorded", static_cast<unsigned>(counted.recorded));
			fail(buf);
		}
		/* Two thirds of the instructions at least are in turns that
		 * were counted: the interrupt is held one call out of ten. */
		if (turn && counted.counted * turn * 3 < run.instructions * 2) {
			char buf[96];
			std::snprintf(buf, sizeof buf, "%u turns of %u instructions counted out of %u instructions",
			              static_cast<unsigned>(counted.counted), turn, static_cast<unsigned>(run.instructions));
			fail(buf);
		}
	};
	const uint32_t copy_in = ldst_imm(true, false, true, true, false, 7, 5, 32);	/* ldr r5, [r7, #32] */
	const uint32_t copy_out = ldst_imm(false, false, true, true, false, 7, 5, 20);	/* str r5, [r7, #20] */
	const uint32_t seven = dp_imm(MOV, false, 0, 5, 7, 0);				/* mov r5, #7 */
	const uint32_t no_r5 = dp_imm(MOV, false, 0, 5, 0, 0);				/* mov r5, #0 */
	/* A byte that goes from 0 to 7 and round: eight turns make one. */
	const std::vector<uint32_t> round = {
		ldst_imm(true, true, true, true, false, 7, 0, 24),	/* ldrb r0, [r7, #24] */
		add1,
		dp_imm(AND, false, 0, 0, 7, 0),				/* and r0, r0, #7 */
		ldst_imm(false, true, true, true, false, 7, 0, 24),	/* strb r0, [r7, #24] */
		drop,
	};

	kept("a turn taken again after an interrupt", with_wait({}), {10}, 4, 4);
	kept("a turn taken again after an interrupt, with its count", with_wait({load, add1, store, drop}), {10}, 4, 8);
	kept("a round of eight turns taken again after an interrupt", with_wait(round), {10}, 4, 72);
	/* The turn sets a word and clears it: what it reads back there is
	 * its own, not what the memory held before. */
	kept("a turn that reads back what it stored", with_wait({seven, copy_out, ldst_imm(true, false, true, true, false, 7, 6, 20),
	     no_r5, copy_out, dp_imm(MOV, false, 0, 6, 0, 0)}), {10}, 4, 10);
	/* Where the program is when the machine comes in changes what the
	 * turn kept has to tell: at several calls, with several periods. */
	for (const uint32_t every : { 7u, 10u, 13u }) {
		for (uint32_t call = 101; call < 109; call++) {
			kept("what a turn read is changed while it was left", with_wait({copy_in, copy_out, no_r5}),
			     {every, call, TURN_COUNT + 32, 0x55}, 8, 7);
			kept("what a turn leaves is changed while it was left", with_wait({seven, copy_out, no_r5}),
			     {every, call, TURN_COUNT + 20, 9, 0, 0, Handler::Own, 0, TURN_COUNT + 20, 7}, 8, 7);
			kept("a turn changed while it was left", with_wait({seven, copy_out, no_r5}),
			     {every, call, TURN_CODE, 9, 0, 0, Handler::Own, 0, TURN_COUNT + 20, 7}, 8, 7);
			kept("the bus answers something else while a turn was left", {
				ldst_imm(true, false, true, true, false, 4, 3, 0),	/* ldr r3, [r4] */
				ldst_imm(false, false, true, true, false, 7, 3, 20),	/* str r3, [r7, #20] */
				dp_imm(MOV, false, 0, 3, 0, 0),				/* mov r3, #0 */
				ldst_imm(true, true, true, true, false, 1, 2, 0),
				dp_imm(CMP, true, 2, 0, 0, 0),
				branch(false, -7, EQ),
				branch(false, -2),
			}, {every, 0, 0, 0, call, 0x33}, 8, 6);
		}
	}
	/* The registers the turn can see must be as they were: one the
	 * handler changes, the User r13 a transfer reads from another mode,
	 * the r13 of a mode the turn goes to. */
	kept("an interrupt that changes a register of the turn", with_wait({ldst_imm(false, false, true, true, false, 7, 6, 20)}),
	     {10, 0, 0, 0, 0, 0, Handler::Shared}, 40, 5);
	/* The turn leaves when the register is high enough, and writes
	 * nothing: only the register tells that it is no longer the same. */
	kept("an interrupt that changes what the turn leaves at", with_wait({dp_imm(CMP, true, 6, 0, 2, 12),	/* cmp r6, #0x200 */
	     branch(false, 4, CS), dp_imm(CMP, true, 2, 0, 0, 0)}), {10, 0, 0, 0, 0, 0, Handler::Shared}, 40, 0);
	kept("a turn that transfers a User register", with_wait({dp_imm(ADD, false, 7, 9, 20, 0),
	     ldstm(false, false, true, true, false, 9, 1u << 13), dp_imm(MOV, false, 0, 9, 0, 0)}),
	     {10, 0, 0, 0, 0, 0, Handler::User13}, 40, 7);
	kept("an interrupt that changes a register of the mode of the turn", with_wait({ldst_imm(false, false, true, true, false, 7, 9, 20)}),
	     {10, 0, 0, 0, 0, 0, Handler::User9}, 40, 5);
	kept("an interrupt that changes the saved status of the mode of the turn", with_wait({mrs(true, 5), copy_out, no_r5}),
	     {10, 0, 0, 0, 0, 0, Handler::Supervisor}, 40, 0);
	kept("a turn that goes to another mode", with_wait({msr_imm(false, 1, 0x9F, 0),
	     ldst_imm(false, false, true, true, false, 7, 13, 20), msr_imm(false, 1, 0x93, 0)}),
	     {10, 0, 0, 0, 0, 0, Handler::User13}, 40, 7);
	/* A word the turn loads after it stored one byte of it: what the
	 * other bytes hold is not told apart, the turn is not kept. The
	 * machine changes one of them. */
	for (uint32_t call = 101; call < 109; call++) {
		kept("a turn that loads a word it stored a byte of", with_wait({seven,
		     ldst_imm(false, true, true, true, false, 7, 5, 36),	/* strb r5, [r7, #36] */
		     ldst_imm(true, false, true, true, false, 7, 6, 36),	/* ldr r6, [r7, #36] */
		     ldst_imm(false, false, true, true, false, 7, 6, 20),	/* str r6, [r7, #20] */
		     no_r5, dp_imm(MOV, false, 0, 6, 0, 0)}),
		     {10, call, TURN_COUNT + 37, 0x55, 0, 0, Handler::Own, 0, TURN_COUNT + 36, 7}, 40, 0);
	}
	{
		/* More loads than the trail of a turn takes: 4 100 loads of
		 * seven registers, then a word that makes the turn leave
		 * when it is set, and 3 000 loads more. The turn is not
		 * kept: the word, read past the end of the trail, could not
		 * be looked at when the program comes back. The machine sets
		 * it while the program is away, at several calls: some fall
		 * after the turn read it. */
		const uint32_t loads = ldstm(true, false, true, false, false, 9, 0x0D4D);	/* ldmia r9, {r0, r2, r3, r6, r8, r10, r11} */
		std::vector<uint32_t> code(4100, loads);

		code.push_back(copy_in);
		code.push_back(dp_imm(CMP, true, 5, 0, 0, 0));		/* cmp r5, #0 */
		code.push_back(branch(false, 3000 + 4, 1));		/* bne out */
		code.insert(code.end(), 3000, loads);
		code.push_back(dp_imm(MOV, false, 0, 2, 0, 0));		/* mov r2, #0 */
		for (uint32_t call = 401; call < 521; call += 8) {
			kept("a turn with more loads than its trail takes", with_wait(code),
			     {400, call, TURN_COUNT + 32, 0x55, 0, 0, Handler::Own, 0, 0, 0, 0, 4000}, 80, 0);
		}
	}
	{
		/* A round of 5 680 instructions */
		std::vector<uint32_t> code(700, dp_reg(MOV, false, 0, 5, 5, LSL, 0));	/* mov r5, r5 */

		code.insert(code.end(), round.begin(), round.end());
		/* The word looked at is away from the byte of the round:
		 * the machine reading there would have it run. */
		kept("a round longer than 4 096 instructions", with_wait(code), {0, 0, 0, 0, 0, 0, Handler::Own, 0, 0, 0, 0x9000}, 3, 8 * 709);
	}
	{
		/* Eight laps make the round of a byte; each lap goes through a
		 * table of 64 entries, its index and address in r9 and r10, as
		 * a sound driver looks for a voice. A turn recorded from inside
		 * the table is at its start 512 times a round, with those
		 * registers once only: after an interrupt, the program has to
		 * take the turn again where it left it. */
		const std::vector<uint32_t> table = {
			dp_imm(MOV, false, 0, 9, 0, 0),				/* mov r9, #0 */
			dp_imm(ADD, false, 7, 10, 0x40, 0),			/* add r10, r7, #0x40 */
			ldst_imm(true, true, true, true, false, 10, 0, 0),	/* scan: ldrb r0, [r10] */
			dp_imm(TST, true, 0, 0, 0x80, 0),			/* tst r0, #0x80 */
			branch(false, 3, 1),					/* bne out */
			dp_imm(ADD, false, 10, 10, 0x20, 0),			/* add r10, r10, #0x20 */
			dp_imm(ADD, false, 9, 9, 1, 0),				/* add r9, r9, #1 */
			dp_imm(CMP, true, 9, 0, 0x40, 0),			/* cmp r9, #64 */
			branch(false, -8, 1),					/* bne scan */
			drop,							/* out: */
		};
		std::vector<uint32_t> code = round;
		code.insert(code.end(), table.begin(), table.end());
		kept("a turn from inside a table", with_wait(code), {50, 0, 0, 0, 0, 0, Handler::Own, 0, 0, 0, 0, 1000}, 4, 8 * 460);

		/* A word of the bus, the table, another word of the bus, the
		 * table again: the watched word holds one, then the other. The
		 * part of the turn run where it was left gets them in their
		 * order, past the end of the turn to its start. */
		code = { ldst_imm(true, false, true, true, false, 4, 3, 0) };		/* ldr r3, [r4] */
		const std::vector<uint32_t> keep = {
			ldst_imm(false, false, true, true, false, 7, 3, 20),	/* str r3, [r7, #20] */
			dp_imm(MOV, false, 0, 3, 0, 0),				/* mov r3, #0 */
		};
		code.insert(code.end(), keep.begin(), keep.end());
		code.insert(code.end(), table.begin(), table.end());
		code.push_back(ldst_imm(true, false, true, true, false, 4, 3, 4));	/* ldr r3, [r4, #4] */
		code.insert(code.end(), keep.begin(), keep.end());
		code.insert(code.end(), table.begin(), table.end());
		code.push_back(ldst_imm(true, true, true, true, false, 1, 2, 0));	/* ldrb r2, [r1] */
		code.push_back(dp_imm(CMP, true, 2, 0, 0, 0));				/* cmp r2, #0 */
		code.push_back(branch(false, -static_cast<int32_t>(code.size()) - 2, EQ));	/* beq start */
		code.push_back(branch(false, -2));					/* b . */
		kept("a turn that reads two words of the bus", code, {50, 0, 0, 0, 1, 0x33, Handler::Own, 0, 0, 0, 0, 1000}, 6, 911);

		/* The table until the flag is set, then a word copied to the
		 * watched one and the table: a turn of each. The machine changes
		 * the word, which only the second turn reads: where the program
		 * left that turn, the word is no longer the same. */
		code = table;
		code.push_back(ldst_imm(true, true, true, true, false, 1, 2, 0));	/* ldrb r2, [r1] */
		code.push_back(dp_imm(CMP, true, 2, 0, 0, 0));				/* cmp r2, #0 */
		code.push_back(branch(false, -static_cast<int32_t>(code.size()) - 2, EQ));	/* beq start */
		const size_t second = code.size();
		code.insert(code.end(), { copy_in, copy_out, no_r5 });
		code.insert(code.end(), table.begin(), table.end());
		code.push_back(branch(false, -static_cast<int32_t>(code.size() - second) - 2));	/* b second */
		/* The first turn kept is only dropped 65 536 instructions after
		 * the program left it for good: the second is recorded near
		 * call 500. */
		const TurnsEvents events{50, 800, TURN_COUNT + 32, 0x55, 0, 0, Handler::Own, 0, 0, 0, TURN_COUNT + 20, 1000};
		const TurnsRun counted = run_turns(true, code, 0, 300, false, 0, events);
		const TurnsRun run = run_turns(false, code, 0, 300, false, 0, events);
		g_current = "a second turn that reads a word the first does not";
		g_checks += 2;
		if (!(counted.state == run.state) || counted.ram != run.ram || counted.instructions != run.instructions) {
			fail("differs from every turn run");
		}
		if (counted.watched != run.watched) {
			fail("the machine read something else on the way");
		}
	}
}

} /* namespace */

/* The pages a machine must tell the core of (watched_reads, watched_writes):
 * none outside a turn; while one is recorded, those it has read so far, for
 * a write; while one is counted, those it changes, for a read, and those it
 * reads or writes, for a write. */
void test_watched_pages()
{
	TEST("pages the machine must tell of");
	CountingBus bus;
	Arm7 cpu(bus);
	std::vector<uint8_t> ram(0x10000, 0);
	/* 1:	ldr r5, [r6]		a count, in page 8
	 *	add r5, r5, #1
	 *	str r5, [r6]
	 *	mov r5, #0
	 *	strb r9, [r10]		a byte of page 12 changed
	 *	strb r11, [r10]		and put back
	 *	strb r11, [r12]		a byte of page 14 written as it is
	 *	ldr r1, [r0]		a word of page 4, read only
	 *	cmp r1, #0
	 *	beq 1b
	 *	b .			*/
	const uint32_t code[] = { 0xE5965000, 0xE2855001, 0xE5865000, 0xE3A05000, 0xE5CA9000,
	                          0xE5CAB000, 0xE5CCB000, 0xE5901000, 0xE3510000, 0x0AFFFFF5, 0xEAFFFFFE };
	for (uint32_t i = 0; i < std::size(code); i++) {
		for (uint32_t b = 0; b < 4; b++) {
			ram[0x1000 + i * 4 + b] = static_cast<uint8_t>(code[i] >> (8 * b));
		}
	}
	cpu.set_ram({ram.data(), 0x10000, 0xFFFF});
	odin::Arm7State st{};
	cpu.save(st);
	st.pc = 0x1000;
	/* Z and C as cmp r1, #0 leaves them: the turn is back as it began */
	st.cpsr = Arm7::MODE_SVC | Arm7::FLAG_I | Arm7::FLAG_F | Arm7::FLAG_Z | Arm7::FLAG_C;
	cpu.load(st);
	cpu.set_reg(0, 0x4000);
	cpu.set_reg(6, 0x8000);
	cpu.set_reg(9, 1);
	cpu.set_reg(10, 0xC000);
	cpu.set_reg(12, 0xE000);
	cpu.set_turn_replay(true);

	const auto watched = [](const uint8_t *pages, uint32_t addr) {
		const uint32_t page = addr >> Arm7::WATCH_PAGE;
		return pages && ((pages[page >> 3] >> (page & 7)) & 1);
	};
	check_eq("reads watched before a turn", cpu.watched_reads() != nullptr, 0);
	check_eq("writes watched before a turn", cpu.watched_writes() != nullptr, 0);

	cpu.run(1);
	check_eq("reads watched while a turn is recorded", cpu.watched_reads() != nullptr, 0);
	check_eq("write watched: the code read", watched(cpu.watched_writes(), 0x1000), 1);
	check_eq("write watched: the count read", watched(cpu.watched_writes(), 0x8000), 1);
	check_eq("write watched: the word not read yet", watched(cpu.watched_writes(), 0x4000), 0);

	for (uint32_t i = 0; i < 20 && cpu.turns_counted() == 0; i++) {
		cpu.run(512);
	}
	check_eq("the turn is counted", cpu.turns_counted() != 0, 1);
	const uint8_t *reads = cpu.watched_reads();
	const uint8_t *writes = cpu.watched_writes();
	check_eq("read watched: the count", watched(reads, 0x8000), 1);
	check_eq("read watched: the byte changed and put back", watched(reads, 0xC000), 1);
	check_eq("read watched: the word only read", watched(reads, 0x4000), 0);
	check_eq("read watched: the code", watched(reads, 0x1000), 0);
	check_eq("write watched: the code", watched(writes, 0x1000), 1);
	check_eq("write watched: the word", watched(writes, 0x4000), 1);
	check_eq("write watched: the count", watched(writes, 0x8000), 1);
	check_eq("write watched: the byte changed and put back", watched(writes, 0xC000), 1);
	check_eq("write watched: a byte written as it is", watched(writes, 0xE000), 1);
	check_eq("read watched: a byte written as it is", watched(reads, 0xE000), 0);
	check_eq("write watched: a page the turn does not use", watched(writes, 0x2000), 0);

	cpu.set_turn_replay(false);
	check_eq("reads watched with every turn run", cpu.watched_reads() != nullptr, 0);
	check_eq("writes watched with every turn run", cpu.watched_writes() != nullptr, 0);
}

/* A bus that answers each read with a value made from its address and keeps
 * every access in order: two cores fed alike are compared on it. */
class LogBus : public odin::Arm7Bus {
public:
	struct Access {
		char rw;
		uint32_t size, addr, value;
		bool operator==(const Access &) const = default;
	};
	std::vector<Access> log;

	static uint32_t answer(uint32_t addr) { return (addr * 0x9E3779B1u) ^ 0x5A5A1234u; }
	uint8_t read8(uint32_t addr) override { return static_cast<uint8_t>(note('r', 1, addr, answer(addr))); }
	uint16_t read16(uint32_t addr) override { return static_cast<uint16_t>(note('r', 2, addr, answer(addr))); }
	uint32_t read32(uint32_t addr) override { return note('r', 4, addr, answer(addr)); }
	void write8(uint32_t addr, uint8_t v) override { note('w', 1, addr, v); }
	void write16(uint32_t addr, uint16_t v) override { note('w', 2, addr, v); }
	void write32(uint32_t addr, uint32_t v) override { note('w', 4, addr, v); }
	uint32_t note(char rw, uint32_t size, uint32_t addr, uint32_t v)
	{
		log.push_back({rw, size, addr, v});
		return v;
	}
};

/* Instructions decoded once (step_decoded) do what step() does: words drawn
 * at random, most of them always run, from random registers of every mode
 * and random flags, half of the addresses in the plain memory; each one run
 * twice, the second time from what was decoded. The same registers, PSRs,
 * cycles, memory and bus accesses in the same order. */
void test_decoded()
{
	g_current = "decoded";
	constexpr uint32_t RAM = 0x2000;
	const uint32_t modes[] = { Arm7::MODE_USR, Arm7::MODE_FIQ, Arm7::MODE_IRQ, Arm7::MODE_SVC, Arm7::MODE_ABT,
	                           Arm7::MODE_UND, Arm7::MODE_SYS };
	uint64_t seed = 0x243F6A8885A308D3ull;
	const auto next = [&seed] {
		seed ^= seed << 13;
		seed ^= seed >> 7;
		seed ^= seed << 17;
		return static_cast<uint32_t>(seed >> 16);
	};
	std::vector<uint8_t> pattern(RAM), ram[2];
	for (uint8_t &b : pattern) {
		b = static_cast<uint8_t>(next());
	}
	LogBus bus[2];
	Arm7 old_core(bus[0]), new_core(bus[1]);
	unsigned differ = 0, decoded_runs = 0;

	for (uint32_t trial = 0; trial < 200000 && differ < 5; trial++) {
		odin::Arm7State st{};
		const auto reg = [&next] { return (next() & 1) ? next() % RAM : next(); };
		for (uint32_t &r : st.r) {
			r = reg();
		}
		for (uint32_t i = 0; i < 7; i++) {
			st.usr[i] = reg();
			st.fiq[i] = reg();
		}
		for (uint32_t i = 0; i < 2; i++) {
			st.irq[i] = reg();
			st.svc[i] = reg();
			st.abt[i] = reg();
			st.und[i] = reg();
		}
		for (uint32_t &v : st.spsr) {
			v = (next() & 0xF0000000u) | modes[next() % 7];
		}
		st.cpsr = (next() & 0xF00000C0u) | modes[next() % 7];
		st.pc = (next() % RAM) & ~3u;
		st.fiq_line = (next() % 16) == 0;
		uint32_t op = next() ^ (next() << 16);
		if (next() & 1) {
			op = (op & 0x0FFFFFFF) | 0xE0000000u;
		}
		for (uint32_t pass = 0; pass < 2; pass++) {
			uint32_t cycles[2];
			odin::Arm7State after[2];
			for (uint32_t k = 0; k < 2; k++) {
				Arm7 &cpu = k == 0 ? old_core : new_core;
				ram[k] = pattern;
				for (uint32_t i = 0; i < 4; i++) {
					ram[k][st.pc + i] = static_cast<uint8_t>(op >> (8 * i));
				}
				cpu.set_ram({ram[k].data(), RAM, RAM - 1});
				cpu.load(st);
				bus[k].log.clear();
				cycles[k] = k == 0 ? cpu.step() : cpu.step_decoded();
				cpu.save(after[k]);
			}
			decoded_runs += pass;
			g_checks++;
			if (cycles[0] != cycles[1] || !(after[0] == after[1]) || ram[0] != ram[1] || bus[0].log != bus[1].log ||
			    old_core.instructions() != new_core.instructions()) {
				char line[96];
				std::snprintf(line, sizeof line, "%08x at %08x, cpsr %08x, run %u: not as step()", op, st.pc,
				              st.cpsr, pass + 1);
				fail(line);
				differ++;
				break;
			}
		}
	}
	check_eq("words run twice from what was decoded", decoded_runs, 200000);

	/* A word decoded, then written over: the new one runs. */
	std::vector<uint8_t> mem(RAM);
	LogBus quiet;
	Arm7 cpu(quiet);
	odin::Arm7State st{};
	st.cpsr = Arm7::MODE_SVC;
	st.pc = 0x100;
	cpu.set_ram({mem.data(), RAM, RAM - 1});
	for (uint32_t value = 1; value <= 2; value++) {
		const uint32_t mov = 0xE3A00000u | value; /* mov r0, #value */
		std::memcpy(&mem[0x100], &mov, 4);
		cpu.load(st);
		cpu.step_decoded();
		check_eq(value == 1 ? "decoded: mov r0, #1" : "decoded: the word written over runs", cpu.reg(0), value);
	}
}

/* run() from what was decoded does what it does with step() alone: a loop
 * across two pages that writes another instruction over its first one and
 * raises its interrupt through the bus every other turn, the handler lowering it,
 * run by calls of a few cycles to a few hundred. The same cycles from each
 * call, registers, memory, bus accesses in order and instructions. */
class IrqBus : public LogBus {
public:
	Arm7 *cpu = nullptr;
	bool fiq = false;

	void write32(uint32_t addr, uint32_t v) override
	{
		LogBus::write32(addr, v);
		if (addr == 0x30000) {
			if (fiq) {
				cpu->set_fiq((v & 1) != 0);
			} else {
				cpu->set_irq((v & 1) != 0);
			}
		}
	}
};

void test_run_decoded()
{
	constexpr uint32_t NE = 1;
	const uint32_t add = dp_imm(ADD, false, 5, 5, 3, 0), sub = dp_imm(SUB, false, 5, 5, 1, 0);
	const std::vector<std::pair<uint32_t, std::vector<uint32_t>>> code = {
		{ 0x0FF0, { add,
			    dp_imm(ADD, false, 6, 6, 3, 0),
			    ldst_imm(false, false, true, true, false, 8, 7, 0),		/* str r7, [r8] */
			    dp_reg(EOR, false, 7, 7, 9, LSL, 0),			/* eor r7, r7, r9 */
			    dp_imm(SUB, true, 10, 10, 1, 0),				/* subs r10, r10, #1 */
			    ldst_imm(false, false, true, true, false, 11, 10, 0),	/* str r10, [r11] */
			    ldst_imm(true, false, true, true, false, 11, 1, 4),		/* ldr r1, [r11, #4] */
			    branch(false, -9, NE),					/* bne 0ff0 */
			    branch(false, -2) } },					/* b . */
		{ 0x18, { branch(false, 0x78),						/* b 200 */
			  branch(false, 0xB7) } },					/* b 300 */
		{ 0x200, { dp_imm(MOV, false, 0, 0, 0, 0),
			   ldst_imm(false, false, true, true, false, 11, 0, 0),		/* str r0, [r11] */
			   dp_imm(ADD, false, 12, 12, 1, 0),
			   dp_imm(SUB, true, 14, 15, 4, 0) } },				/* subs pc, lr, #4 */
		/* FIQ has r8 to r14 of its own */
		{ 0x300, { dp_imm(MOV, false, 0, 0, 0, 0),
			   ldst_imm(false, false, true, true, false, 3, 0, 0),		/* str r0, [r3] */
			   dp_imm(ADD, false, 4, 4, 1, 0),
			   dp_imm(SUB, true, 14, 15, 4, 0) } },				/* subs pc, lr, #4 */
	};
	const uint32_t calls[] = { 1, 2, 5, 13, 64, 512, 3 };

	for (const bool fiq : { false, true }) {
		g_current = fiq ? "run decoded, FIQ" : "run decoded, IRQ";
		std::vector<uint8_t> ram[3];
		std::vector<uint32_t> spent[3];
		IrqBus bus[3];
		odin::Arm7State after[3];
		uint64_t instructions[3];

		for (uint32_t k = 0; k < 3; k++) {
			Arm7 cpu(bus[k]);
			bus[k].cpu = &cpu;
			bus[k].fiq = fiq;
			set_way(cpu, static_cast<Way>(k));
			ram[k].assign(0x10000, 0);
			for (const auto &[at, words] : code) {
				std::memcpy(&ram[k][at], words.data(), words.size() * 4);
			}
			cpu.set_ram({ram[k].data(), 0x10000, 0xFFFF});
			odin::Arm7State st{};
			cpu.save(st);
			st.pc = 0x0FF0;
			st.cpsr = Arm7::MODE_SVC | (fiq ? Arm7::FLAG_I : Arm7::FLAG_F);
			cpu.load(st);
			cpu.set_reg(3, 0x30000);
			cpu.set_reg(7, sub);
			cpu.set_reg(8, 0x0FF0);
			cpu.set_reg(9, add ^ sub);
			cpu.set_reg(10, 200);
			cpu.set_reg(11, 0x30000);
			for (uint32_t total = 0, i = 0; total < 20000; i++) {
				spent[k].push_back(cpu.run(calls[i % 7]));
				total += spent[k].back();
			}
			cpu.save(after[k]);
			instructions[k] = cpu.instructions();
		}
		for (uint32_t k = 1; k < 3; k++) {
			const std::string way = std::string(k == 1 ? "run decoded" : "run as host code") + (fiq ? ", FIQ" : ", IRQ");
			check_eq((way + ": cycles of each call").c_str(), spent[0] == spent[k], 1);
			check_eq((way + ": registers").c_str(), after[0] == after[k], 1);
			check_eq((way + ": memory").c_str(), ram[0] == ram[k], 1);
			check_eq((way + ": bus accesses").c_str(), bus[0].log == bus[k].log, 1);
			check_eq((way + ": instructions").c_str(), instructions[0], instructions[k]);
		}
		/* The program did what it was written for. */
		check_eq("run decoded: turns of the loop", after[1].r[6], 600);
		check_eq("run decoded: up 3, down 1", after[1].r[5], 200);
		check_eq("run decoded: interrupts", fiq ? after[1].r[4] : after[1].usr[4], 100);
	}
}

/* Blocks of host code do what step() does, in whole runs: memories of
 * words drawn at random, most of them always run, branches kept near,
 * registers that point into the memory, every mode and random flags, the
 * interrupt lines changed between calls of a few cycles to a few hundred.
 * The code writes over itself, takes exceptions and reads the bus as it
 * goes; each trial writes a new program over the last one in the same
 * memory, until the host code is full and written again. The same cycles
 * from each call, registers, PSRs, memory, bus accesses in order and
 * instructions. */
void test_translated()
{
	g_current = "translated";
	constexpr uint32_t RAM = 0x4000;
	const uint32_t modes[] = { Arm7::MODE_USR, Arm7::MODE_FIQ, Arm7::MODE_IRQ, Arm7::MODE_SVC, Arm7::MODE_ABT,
	                           Arm7::MODE_UND, Arm7::MODE_SYS };
	uint64_t seed = 0x13198A2E03707344ull;
	const auto next = [&seed] {
		seed ^= seed << 13;
		seed ^= seed >> 7;
		seed ^= seed << 17;
		return static_cast<uint32_t>(seed >> 16);
	};
	unsigned differ = 0;
	uint32_t written = 0, cleared = 0, last = 0;
	std::vector<uint8_t> ram[2] = { std::vector<uint8_t>(RAM), std::vector<uint8_t>(RAM) };
	LogBus bus[2];
	Arm7 cores[2] = { Arm7(bus[0]), Arm7(bus[1]) };

	set_way(cores[0], Way::Read);
	set_way(cores[1], Way::Host);
	const uint32_t ways = cores[1].host_bytes();
	for (uint32_t k = 0; k < 2; k++) {
		cores[k].set_ram({ram[k].data(), RAM, RAM - 1});
	}
	for (uint32_t trial = 0; trial < 1000 && differ < 3; trial++) {
		std::vector<uint8_t> program(RAM);
		for (uint32_t at = 0; at < RAM; at += 4) {
			uint32_t op = next() ^ (next() << 16);
			if (next() % 4) {
				op = (op & 0x0FFFFFFF) | 0xE0000000u;
			}
			/* A branch goes a few words away */
			if ((op & 0x0E000000) == 0x0A000000) {
				op = (op & 0xFF000000) | ((next() % 64 - 32) & 0x00FFFFFF);
			}
			std::memcpy(&program[at], &op, 4);
		}
		odin::Arm7State st{};
		const auto reg = [&next] { return (next() % 4) ? next() % RAM : next(); };
		for (uint32_t &r : st.r) {
			r = reg();
		}
		for (uint32_t i = 0; i < 7; i++) {
			st.usr[i] = reg();
			st.fiq[i] = reg();
		}
		for (uint32_t i = 0; i < 2; i++) {
			st.irq[i] = reg();
			st.svc[i] = reg();
			st.abt[i] = reg();
			st.und[i] = reg();
		}
		for (uint32_t &v : st.spsr) {
			v = (next() & 0xF00000C0u) | modes[next() % 7];
		}
		st.cpsr = (next() & 0xF00000C0u) | modes[next() % 7];
		st.pc = (next() % RAM) & ~3u;

		std::vector<uint32_t> calls, lines;
		for (uint32_t c = 0; c < 80; c++) {
			calls.push_back(1 + next() % 300);
			lines.push_back(next() % 8);
		}
		std::vector<uint32_t> spent[2];
		odin::Arm7State after[2];
		uint64_t instructions[2];
		for (uint32_t k = 0; k < 2; k++) {
			Arm7 &cpu = cores[k];
			const uint64_t before = cpu.instructions();
			std::copy(program.begin(), program.end(), ram[k].begin());
			bus[k].log.clear();
			cpu.load(st);
			for (uint32_t c = 0; c < calls.size(); c++) {
				cpu.set_irq(lines[c] == 1);
				cpu.set_fiq(lines[c] == 2);
				spent[k].push_back(cpu.run(calls[c]));
			}
			cpu.save(after[k]);
			instructions[k] = cpu.instructions() - before;
		}
		written += cores[1].host_bytes() > ways;
		cleared += cores[1].host_bytes() < last;
		last = cores[1].host_bytes();
		g_checks++;
		if (spent[0] != spent[1] || !(after[0] == after[1]) || ram[0] != ram[1] || bus[0].log != bus[1].log ||
		    instructions[0] != instructions[1]) {
			char line[96];
			std::snprintf(line, sizeof line, "trial %u from %08x, cpsr %08x: not as step()", trial, st.pc, st.cpsr);
			fail(line);
			differ++;
		}
	}
	check_eq("translated: runs with host code written", written, 1000);
	check_eq("translated: host code full, then written again", cleared > 0, 1);
}

/* Data processing done in place by host code does what step() does, one
 * instruction at a time: every operation with and without S, every form
 * of operand 2, the values around the edges of 32 bits, random flags and
 * conditions. A call of run() takes its first instruction from what was
 * decoded: a MOV r0, r0 comes first, then the one tried, as host code. */
void test_translated_data()
{
	g_current = "translated data";
	static constexpr uint32_t EDGES[] = { 0, 1, 2, 0x7FFFFFFF, 0x80000000, 0x80000001, 0xFFFFFFFF, 0xFFFFFFFE,
	                                      0x40000000, 0x12345678 };
	uint64_t seed = 0xA4093822299F31D0ull;
	const auto next = [&seed] {
		seed ^= seed << 13;
		seed ^= seed >> 7;
		seed ^= seed << 17;
		return static_cast<uint32_t>(seed >> 16);
	};
	const auto value = [&next] { return (next() % 3) ? EDGES[next() % 10] : next() ^ (next() << 16); };
	unsigned differ = 0;
	std::vector<uint8_t> ram[2] = { std::vector<uint8_t>(0x1000), std::vector<uint8_t>(0x1000) };
	LogBus bus[2];
	Arm7 cores[2] = { Arm7(bus[0]), Arm7(bus[1]) };

	set_way(cores[0], Way::Read);
	set_way(cores[1], Way::Host);
	for (uint32_t k = 0; k < 2; k++) {
		cores[k].set_ram({ram[k].data(), 0x1000, 0xFFF});
	}
	for (uint32_t trial = 0; trial < 40000 && differ < 5; trial++) {
		const uint32_t opcode = trial % 16, s = (trial / 16) % 2;
		const uint32_t rn = next() % 16, rd = next() % 15, rm = next() % 16;
		const uint32_t cc = (next() % 4) ? 0xE : next() % 15;
		uint32_t op;
		if (next() % 3 == 0) {
			op = dp_imm(opcode, s != 0, rn, rd, next() & 0xFF, (next() % 2) ? next() % 16 : 0, cc);
		} else {
			const uint32_t amount = (next() % 3) ? next() % 32 : 0;
			op = dp_reg(opcode, s != 0, rn, rd, rm, next() % 4, amount, cc);
		}
		odin::Arm7State st{};
		cores[0].save(st);
		for (uint32_t &r : st.r) {
			r = value();
		}
		for (uint32_t i = 0; i < 7; i++) {
			st.usr[i] = value();
		}
		for (uint32_t &r : st.svc) {
			r = value();
		}
		st.cpsr = (next() & 0xF0000000u) | Arm7::MODE_SVC | Arm7::FLAG_I | Arm7::FLAG_F;
		st.pc = 0xFC;
		st.irq_line = st.fiq_line = 0;
		uint32_t spent[2];
		odin::Arm7State after[2];
		/* Written again: run as host code, not as a word that changed */
		cores[1].set_translate(true);
		const uint32_t ways = cores[1].host_bytes();
		for (uint32_t k = 0; k < 2; k++) {
			std::memcpy(&ram[k][0xFC], &NOP, 4);
			std::memcpy(&ram[k][0x100], &op, 4);
			cores[k].load(st);
			spent[k] = cores[k].run(2);
			cores[k].save(after[k]);
		}
		g_checks += 2;
		if (cores[1].host_bytes() <= ways) {
			fail("not run as host code");
			differ++;
		}
		if (spent[0] != spent[1] || !(after[0] == after[1]) || cores[0].instructions() != cores[1].instructions()) {
			char line[96];
			std::snprintf(line, sizeof line, "%08x, cpsr %08x: not as step()", op, st.cpsr);
			fail(line);
			differ++;
		}
	}
}

/* LDR and STR done in place by host code do what step() does, one at a
 * time after a MOV r0, r0: every P, U, B, W and L, every form of offset,
 * addresses not aligned, at the ends of the memory and past it on the bus. */
void test_translated_transfer()
{
	g_current = "translated transfer";
	constexpr uint32_t RAM = 0x1000;
	uint64_t seed = 0x082EFA98EC4E6C89ull;
	const auto next = [&seed] {
		seed ^= seed << 13;
		seed ^= seed >> 7;
		seed ^= seed << 17;
		return static_cast<uint32_t>(seed >> 16);
	};
	const auto address = [&next] {
		switch (next() % 4) {
		case 0: return next() % RAM;
		case 1: return RAM - 8 + next() % 16;
		case 2: return next() % 0x20;
		default: return next() ^ (next() << 16);
		}
	};
	unsigned differ = 0;
	std::vector<uint8_t> pattern(RAM), ram[2] = { std::vector<uint8_t>(RAM), std::vector<uint8_t>(RAM) };
	for (uint8_t &b : pattern) {
		b = static_cast<uint8_t>(next());
	}
	LogBus bus[2];
	Arm7 cores[2] = { Arm7(bus[0]), Arm7(bus[1]) };

	set_way(cores[0], Way::Read);
	set_way(cores[1], Way::Host);
	for (uint32_t k = 0; k < 2; k++) {
		cores[k].set_ram({ram[k].data(), RAM, RAM - 1});
	}
	for (uint32_t trial = 0; trial < 40000 && differ < 5; trial++) {
		const bool p = next() & 1, u = next() & 1, b = next() & 1, w = next() & 1, l = next() & 1;
		const uint32_t rn = (next() % 8) ? next() % 15 : 15, rd = next() % 16, rm = next() % 15;
		const uint32_t cc = (next() % 4) ? 0xE : next() % 15;
		uint32_t op = (next() % 2) ? ldst_imm(l, b, p, u, w, rn, rd, next() % 0x1000)
		                           : ldst_reg(l, b, p, u, w, rn, rd, rm, next() % 4, (next() % 3) ? next() % 32 : 0);
		op = (op & 0x0FFFFFFF) | (cc << 28);
		odin::Arm7State st{};
		cores[0].save(st);
		for (uint32_t &r : st.r) {
			r = (next() % 3) ? address() : next() % 64;
		}
		for (uint32_t i = 0; i < 7; i++) {
			st.usr[i] = (next() % 3) ? address() : next() % 64;
		}
		for (uint32_t &r : st.svc) {
			r = address();
		}
		st.cpsr = (next() & 0xF0000000u) | Arm7::MODE_SVC | Arm7::FLAG_I | Arm7::FLAG_F;
		st.pc = 0xFC;
		st.irq_line = st.fiq_line = 0;
		uint32_t spent[2];
		odin::Arm7State after[2];
		cores[1].set_translate(true);
		const uint32_t ways = cores[1].host_bytes();
		for (uint32_t k = 0; k < 2; k++) {
			ram[k] = pattern;
			std::memcpy(&ram[k][0xFC], &NOP, 4);
			std::memcpy(&ram[k][0x100], &op, 4);
			bus[k].log.clear();
			cores[k].load(st);
			spent[k] = cores[k].run(2);
			cores[k].save(after[k]);
		}
		g_checks += 2;
		if (cores[1].host_bytes() <= ways) {
			fail("not run as host code");
			differ++;
		}
		if (spent[0] != spent[1] || !(after[0] == after[1]) || ram[0] != ram[1] || bus[0].log != bus[1].log ||
		    cores[0].instructions() != cores[1].instructions()) {
			char line[96];
			std::snprintf(line, sizeof line, "%08x, r%u %08x: not as step()", op, rn, rn < 8 ? st.r[rn] : 0);
			fail(line);
			differ++;
		}
	}
}

/* LDM and STM done in place by host code do what step() does, one at a
 * time after a MOV r0, r0: every P, U, W and L, lists with and without the
 * base or PC, empty ones, S, bases not aligned, at the ends of the memory
 * and past it on the bus. */
void test_translated_block()
{
	g_current = "translated block";
	constexpr uint32_t RAM = 0x1000;
	uint64_t seed = 0x5D3B1C2A9E7F4061ull;
	const auto next = [&seed] {
		seed ^= seed << 13;
		seed ^= seed >> 7;
		seed ^= seed << 17;
		return static_cast<uint32_t>(seed >> 16);
	};
	const auto address = [&next] {
		switch (next() % 4) {
		case 0: return next() % RAM;
		case 1: return RAM - 0x48 + next() % 0x90;
		case 2: return next() % 0x48;
		default: return next() ^ (next() << 16);
		}
	};
	unsigned differ = 0;
	std::vector<uint8_t> pattern(RAM), ram[2] = { std::vector<uint8_t>(RAM), std::vector<uint8_t>(RAM) };
	for (uint8_t &b : pattern) {
		b = static_cast<uint8_t>(next());
	}
	LogBus bus[2];
	Arm7 cores[2] = { Arm7(bus[0]), Arm7(bus[1]) };

	set_way(cores[0], Way::Read);
	set_way(cores[1], Way::Host);
	for (uint32_t k = 0; k < 2; k++) {
		cores[k].set_ram({ram[k].data(), RAM, RAM - 1});
	}
	for (uint32_t trial = 0; trial < 40000 && differ < 5; trial++) {
		const uint32_t p = next() & 1, u = next() & 1, s = (next() % 8) == 0, w = next() & 1, l = next() & 1;
		const uint32_t rn = (next() % 16) ? next() % 15 : 15;
		uint32_t list = (next() % 2) ? next() & 0xFFFF : (next() & 0x7FFF) & ~(1u << rn);
		if (next() % 32 == 0) {
			list = 0;
		}
		const uint32_t cc = (next() % 4) ? 0xE : next() % 15;
		const uint32_t op = (cc << 28) | (4u << 25) | (p << 24) | (u << 23) | (s << 22) | (w << 21) | (l << 20) |
		                    (rn << 16) | list;
		odin::Arm7State st{};
		cores[0].save(st);
		for (uint32_t &r : st.r) {
			r = (next() % 3) ? address() : next();
		}
		for (uint32_t i = 0; i < 7; i++) {
			st.usr[i] = (next() % 3) ? address() : next();
		}
		for (uint32_t &r : st.svc) {
			r = address();
		}
		st.cpsr = (next() & 0xF0000000u) | Arm7::MODE_SVC | Arm7::FLAG_I | Arm7::FLAG_F;
		st.pc = 0xFC;
		st.irq_line = st.fiq_line = 0;
		uint32_t spent[2];
		odin::Arm7State after[2];
		cores[1].set_translate(true);
		const uint32_t ways = cores[1].host_bytes();
		for (uint32_t k = 0; k < 2; k++) {
			ram[k] = pattern;
			std::memcpy(&ram[k][0xFC], &NOP, 4);
			std::memcpy(&ram[k][0x100], &op, 4);
			bus[k].log.clear();
			cores[k].load(st);
			spent[k] = cores[k].run(2);
			cores[k].save(after[k]);
		}
		g_checks += 2;
		if (cores[1].host_bytes() <= ways) {
			fail("not run as host code");
			differ++;
		}
		if (spent[0] != spent[1] || !(after[0] == after[1]) || ram[0] != ram[1] || bus[0].log != bus[1].log ||
		    cores[0].instructions() != cores[1].instructions()) {
			char line[96];
			std::snprintf(line, sizeof line, "%08x, r%u %08x: not as step()", op, rn, rn < 8 ? st.r[rn] : 0);
			fail(line);
			differ++;
		}
	}
}

int main(int argc, char **argv)
{
	for (int i = 1; i < argc; i++) {
		if (std::strcmp(argv[i], "-v") == 0) {
			g_verbose = true;
		}
	}
	test_conditions();
	test_data_processing();
	test_shifter();
	test_multiply();
	test_single_transfer();
	test_halfword_transfer();
	test_block_transfer();
	test_swap();
	test_branches();
	test_psr();
	test_exceptions();
	test_save_load();
	test_trace();
	test_turns();
	test_watched_pages();
	test_decoded();
	test_run_decoded();
	test_translated();
	test_translated_data();
	test_translated_transfer();
	test_translated_block();
	std::printf("%d checks, %d failures\n", g_checks, g_failures);
	return g_failures == 0 ? 0 : 1;
}
