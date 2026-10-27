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
 *            -ffp-contract=off -I src tests/sh4/sh4_tests.cpp src/odin/machine/cpu/sh4/sh4.cpp \
 *            src/odin/machine/cpu/sh4/sh4_ops.cpp src/odin/machine/cpu/sh4/sh4_onchip.cpp src/odin/machine/cpu/sh4/sh4_host.cpp src/odin/machine/cpu/sh4/sh4_translate.cpp \
 *            src/odin/machine/cpu/sh4/sh4_fsca.cpp src/odin/machine/cpu/sh4/sh4_clock.cpp src/odin/machine/core/host_code.cpp -o sh4_tests
 * Use:   sh4_tests [-v]
 * Exit status is 0 when every check passes. */

#include "sh4_machine.hpp"

using namespace sh4_test;

namespace {

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
	{
		/* What FSCA gives is frozen, the same on every host: the whole
		 * table, 65 536 sines and cosines, by its checksum; and FSCA
		 * takes it from there, at an angle whose sine is one of those
		 * listed apart and at one whose sine is not. */
		const uint32_t *table = odin::sh4_fsca_table();
		uint64_t sum = 0xCBF29CE484222325ull;
		for (uint32_t i = 0; i < 2 * 65536; i++) {
			sum = (sum ^ table[i]) * 0x100000001B3ull;
		}
		eq("the frozen sines and cosines", sum, 0xD9F9980CD44C1D40ull);
		eq("sine of 723 / 65536 of a turn, listed apart", table[2 * 723], 0x3D8DD8DA);
		for (uint32_t angle : { 723u, 724u, 0x4000u, 0xC001u }) {
			Machine m;
			Sh4State st = run(m, {0xF2FD}, 1, [angle](Sh4State &s) { s.fpul = 0xABCD0000 | angle; });
			eq("fsca sine from the table", st.fr[2], table[2 * angle]);
			eq("fsca cosine from the table", st.fr[3], table[2 * angle + 1]);
		}
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
		/* A peripheral clock at a third of the CPU's: a count of Pck/4
		 * is 12 cycles, and a hundred go by in 1200. mov.w r0, @r6
		 * (FRQCR) / mov.b r7, @r1 (TSTR) / mov.l @(8,r1), r2 (TCNT0) /
		 * nop / 599 turns of dt r5, bf / mov.l @(8,r1), r3 */
		Machine m;
		m.cpu.set_time(1000);
		Sh4State st = run(m, {0x2601, 0x2170, 0x5212, NOP, 0x4510, 0x8BFD, 0x5312, NOP}, 1210, [](Sh4State &s) {
			s.r[0] = 0x0E09;
			s.r[6] = 0xFFC00000;
			s.r[1] = 0xFFD80004;
			s.r[7] = 1;
			s.r[5] = 599;
		});
		eq("counts in 1200 cycles, 12 cycles each", st.r[2] - st.r[3], 100);
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
	{
		/* A device that cuts the run ends it after the write; the next
		 * run goes on from there. */
		Machine m;
		m.put32(DATA, DEVICE);
		m.on_device = [&m] { m.cpu.cut(); };
		Sh4State st = run(m, {0x6182, 0x2112, 0xE201, 0xE302, NOP, NOP, NOP, NOP}, 6, [](Sh4State &s) {
			s.r[8] = DATA;
		});
		eq("a cut ends the run after the write", st.r[2], 0);
		eq("ran() tells the cycles to there", static_cast<uint32_t>(m.cpu.ran()), 2);
		m.cpu.run(2);
		st = m.state();
		eq("the next run goes on after the write", st.r[2] + st.r[3], 3);
		eq("and runs whole", static_cast<uint32_t>(m.cpu.ran()), 2);
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
		/* A loop that stores an instruction, add #k, r2 with k the low
		 * three bits of its count of turns, over one of the block it
		 * then branches to, which stores it over the instruction after
		 * its own store: each turn adds k twice, whatever was decoded
		 * before. */
		Machine m;
		m.code(CODE, {0x7401, 0x6043, 0xC907, 0x6503, 0x9004, 0x305C, 0xD102, 0x2101, 0xA00A, NOP,
		              0x7200, NOP, NOP, NOP, NOP, NOP, NOP, NOP, NOP, NOP,
		              0xD102, 0x2101, 0x7201, 0x7303, 0xAFE6, NOP});
		m.put32(CODE + 24, CODE + 46);
		m.put32(CODE + 52, CODE + 44);
		for (uint32_t i = 0; i < 40; i++) {
			m.cpu.run(static_cast<int32_t>(70 + 13 * i));
		}
		const Sh4State st = m.state();
		uint32_t done = 0;
		for (uint32_t turn = 1; turn < st.r[4]; turn++) {
			done += 2 * (turn & 7);
		}
		const uint32_t k = st.r[4] & 7;
		check(st.r[4] > 500, "the loop went round");
		check(st.r[2] == done || st.r[2] == done + k || st.r[2] == done + 2 * k,
		      "the instructions stored are the ones that ran");
		eq("and the one stored over did not", st.r[3], 0);
	}
	{
		/* The same store over the first instruction of a block that the
		 * loop branches to at once, and that was run a turn before:
		 * nothing stops the program between the store and the block. */
		Machine m;
		m.code(CODE, {0x7401, 0x6043, 0xC907, 0x6503, 0x9004, 0x305C, 0xD102, 0x2101, 0xA00A, NOP,
		              0x7200, NOP, NOP, NOP, NOP, NOP, NOP, NOP, NOP, NOP,
		              0x7201, 0xAFE9, NOP});
		m.put32(CODE + 24, CODE + 40);
		for (uint32_t i = 0; i < 40; i++) {
			m.cpu.run(static_cast<int32_t>(70 + 13 * i));
		}
		const Sh4State st = m.state();
		uint32_t done = 0;
		for (uint32_t turn = 1; turn < st.r[4]; turn++) {
			done += turn & 7;
		}
		check(st.r[4] > 500, "the loop went round");
		check(st.r[2] == done || st.r[2] == done + (st.r[4] & 7), "each turn ran the instruction it stored");
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

/* A page of the bus the machine answers without writing the memory: the
 * blocks stay compared and the block that reads it goes on, with nothing
 * else changed. The same programs on two machines, the device quiet on one
 * of them, are the same after each slice; the interrupt the device raises
 * comes at the same instruction. */
void test_quiet_page()
{
	g_test = "quiet page";
	for (int k = 0; k < 3; k++) {
		/* The device writes the instruction after its read, which a
		 * quiet page promises not to do: quiet, the block goes on as it
		 * was decoded; loud (never quiet, or quiet then no longer), it
		 * is left and the new code found. mov.l @r12,r0 / mov #1,r1 /
		 * bra itself / nop; the device puts mov #2,r1. */
		Machine m;
		Machine *self = &m;
		const uint32_t page = DEVICE >> Sh4::PAGE_BITS;

		m.on_device = [self] { self->put16(CODE + 2, 0xE102); };
		if (k > 0) {
			m.cpu.set_quiet(page - 1, 3, true);
		}
		if (k > 1) {
			m.cpu.set_quiet(page, 1, false);
		}
		const Sh4State st = run(m, {0x60C2, 0xE101, 0xAFFE, NOP}, 8, [](Sh4State &s) { s.r[12] = DEVICE; });
		eq(k == 0 ? "loud: the new code" : k == 1 ? "quiet: the block as decoded" : "no longer quiet: the new code",
		   st.r[1], k == 1 ? 1 : 2);
	}
	size_t accesses = 0;
	for (uint32_t seed = 1; seed <= 16; seed++) {
		Machine plain, quiet;
		unsigned differ = 0;

		for (Machine *m : { &plain, &quiet }) {
			with_device(*m);
			random_program(*m, 300 + 40 * (seed % 8), seed * 7919u);
		}
		quiet.cpu.map_quiet(DEVICE >> Sh4::PAGE_BITS);
		for (uint32_t done = 0, series = seed; done < 40000 && differ == 0;) {
			series = series * 1664525u + 1013904223u;
			const uint32_t cycles = 1 + (series >> 8) % 900;
			const int32_t over_plain = plain.cpu.run(static_cast<int32_t>(cycles));
			const int32_t over_quiet = quiet.cpu.run(static_cast<int32_t>(cycles));
			const Sh4State a = plain.state(), b = quiet.state();

			done += cycles;
			if (over_plain != over_quiet || std::memcmp(&a, &b, sizeof(a)) != 0 || plain.ram != quiet.ram ||
			    plain.onchip != quiet.onchip || plain.seen != quiet.seen ||
			    plain.device.size() != quiet.device.size()) {
				differ++;
			}
		}
		accesses += quiet.device.size();
		check(differ == 0, "program " + std::to_string(seed) + ": the same with the device quiet");
	}
	check(accesses > 5000, "the programs did use the device");
	accesses = 0;
	for (uint32_t seed = 1; seed <= 16; seed++) {
		/* The same with the bus taking cycles for each access to the
		 * device (MPX, AnW 3): a quiet access that takes the block past
		 * the end of its slice ends the slice where a loud one does. */
		Machine plain, quiet;
		unsigned differ = 0;

		for (Machine *m : { &plain, &quiet }) {
			with_device(*m);
			m->set_reg32(0xFF800000, 0xA3020008);
			m->set_reg32(0xFF80000C, 0x00006000 | (3u << 7));
			random_program(*m, 300 + 40 * (seed % 8), seed * 104729u);
		}
		quiet.cpu.map_quiet(DEVICE >> Sh4::PAGE_BITS);
		for (uint32_t done = 0, series = seed; done < 40000 && differ == 0;) {
			series = series * 1664525u + 1013904223u;
			const uint32_t cycles = 1 + (series >> 8) % 900;
			const int32_t over_plain = plain.cpu.run(static_cast<int32_t>(cycles));
			const int32_t over_quiet = quiet.cpu.run(static_cast<int32_t>(cycles));
			const Sh4State a = plain.state(), b = quiet.state();

			done += cycles;
			if (over_plain != over_quiet || std::memcmp(&a, &b, sizeof(a)) != 0 || plain.ram != quiet.ram ||
			    plain.onchip != quiet.onchip || plain.seen != quiet.seen ||
			    plain.device.size() != quiet.device.size()) {
				differ++;
			}
		}
		accesses += quiet.device.size();
		check(differ == 0, "program " + std::to_string(seed) + ": the same with the device quiet and the bus slow");
	}
	check(accesses > 5000, "the programs did use the slow device");
	{
		/* A loop that waits for a quiet device is run in full: mov.l
		 * @r12, r0 / tst r0, r0 / bt the read; then mov #7, r1. The
		 * device answers 1 from its 51st read on. */
		Machine m;
		Machine *self = &m;

		with_device(m);
		m.cpu.map_quiet(DEVICE >> Sh4::PAGE_BITS);
		m.on_device = [self] {
			if (self->device.size() == 50) {
				self->device_value = 1;
			}
		};
		m.code(CODE, {0x60C2, 0x2008, 0x89FC, 0xE107, 0xAFFE, NOP});
		m.cpu.run(2000);
		eq("the loop ended at the read that answered", m.device.size(), 51);
		eq("and the program went on", m.state().r[1], 7);
	}
}

void test_others_write()
{
	g_test = "others write";
	for (int k = 0; k < 5; k++) {
		/* mov #1,r1 / add r1,r4 / dt r5 / bf back: each turn adds r1 to
		 * r4. Then mov #2,r1 is put in its place by someone else, who
		 * tells the CPU (k = 1), tells it of another page (k = 2), writes
		 * a page the CPU holds as shared, without a word (k = 3), or
		 * loads a state with the memory (k = 4).
		 * The slices are long enough for the block to be kept, and end
		 * in blocks decoded again (fewer cycles than a block may take). */
		Machine m;

		if (k == 3) {
			m.cpu.map_shared(CODE >> Sh4::PAGE_BITS);
		}
		m.code(CODE, {0xE101, 0x341C, 0x4510, 0x8BFB, 0xAFFE, NOP});
		Sh4State st = m.state();
		st.r[5] = 1000;
		m.cpu.load(st);
		m.cpu.run(300);
		const Sh4State first = m.state();
		m.put16(CODE, 0xE102);
		if (k == 1) {
			m.cpu.written(CODE, 2);
		} else if (k == 2) {
			m.cpu.written(DATA, 0x100);
		} else if (k == 4) {
			m.cpu.load(first);
		}
		m.cpu.run(300);
		const Sh4State second = m.state();
		const uint32_t turns = first.r[5] - second.r[5], added = second.r[4] - first.r[4];

		check(first.r[4] == 1000 - first.r[5] && turns > 50, "the first version went round");
		if (k == 0 || k == 2) {
			check(added < 2 * turns, std::string(k == 0 ? "not told" : "told of another page") +
			                         ": the code as compared, until the end of the slice");
		} else {
			eq(k == 1 ? "told: each turn the new code" : k == 3 ? "a shared page: each turn the new code" :
			   "a state loaded: each turn the new code", added, 2 * turns);
		}
	}
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

/* Store queues, QACR0 and QACR1 on the area of the RAM: a store there lands
 * in the RAM at once. Where the RAM is plain it is written in place and the
 * block goes on; over code compared since, the store goes the way of the
 * bus, and the program runs what was stored. */
constexpr uint32_t queue(uint32_t addr) { return 0xE0000000 | (addr & 0x03FFFFFF); }

void with_queues(Machine &m)
{
	m.set_reg32(0xFF000038, 0x0C);
	m.set_reg32(0xFF00003C, 0x0C);
}

/* A loop that reads the device, stores through a queue and reads plain
 * memory: mov.l @r12, r0 / add r0, r3 / mov.l r3, @r4 / add #4, r4 /
 * mov.l @(4, r14), r1 / add r1, r3 / dt r5 / bf the read / mov r6, r4 /
 * mov r7, r5 / bra the read */
void queue_loop(Machine &m, uint32_t seed)
{
	with_device(m);
	with_queues(m);
	m.code(CODE, {0x60C2, 0x330C, 0x2432, 0x7404, 0x51E1, 0x331C, 0x4510, 0x8BF7, 0x6463, 0x6573, 0xAFF4, NOP});
	m.put32(DATA + 4, 0x01010101 * seed);
	Sh4State st = m.state();
	st.r[4] = st.r[6] = queue(DATA + 0x100);
	st.r[5] = st.r[7] = 40 + seed;
	m.cpu.load(st);
}

/* A read of the CPU on the MPX bus takes an address phase, the waits of
 * WCR2 and a data phase, in bus clocks of two CPU clocks (FRQCR of mode 5):
 * at AnW 0, as the games set it, 6 CPU cycles on top of the instruction for
 * a read and 4 for a write; at AnW 2, 8 for both; at AnW 7, the reset
 * value, 10 for both (manual, 13.3.8 and table 13.6); none while BCR1
 * leaves the area as SRAM. mov.l
 * r2,@r3 sets BCR1, mov.l r4,@r5 WCR2, then turns of mov.l @r12,r0 (or
 * mov.l r0,@r12) / add #1,r1 / bra / nop: 4 cycles each and the bus. */
void test_mpx_time()
{
	g_test = "MPX bus time";
	struct Case {
		const char *name;
		uint32_t bcr1, wcr2;
		uint16_t access;
		uint32_t turn;
	};
	static constexpr Case CASES[] = {
		{ "SRAM: no bus time", 0x80000000, 0x00006000, 0x60C2, 4 },
		{ "MPX read, AnW 0: 6 cycles more", 0xA3020008, 0x00006000, 0x60C2, 10 },
		{ "MPX write, AnW 0: 4 cycles more", 0xA3020008, 0x00006000, 0x2C02, 8 },
		{ "MPX read, AnW 7: 10 cycles more", 0xA3020008, 0xFFFEEFFF, 0x60C2, 14 },
		{ "MPX write, AnW 7: 10 cycles more", 0xA3020008, 0xFFFEEFFF, 0x2C02, 14 },
		{ "MPX read, AnW 2 for area 1 alone: 8 cycles more", 0xA3020008, 0x00000100, 0x60C2, 12 },
		{ "MPX write, AnW 2 for area 1 alone: 8 cycles more", 0xA3020008, 0x00000100, 0x2C02, 12 },
	};
	for (const Case &c : CASES) {
		Machine m;
		const Sh4State st = run(m, {0x2322, 0x2542, c.access, 0x7101, 0xAFFC, NOP}, 2 + 70 * c.turn + 1,
		                        [&c](Sh4State &s) {
			s.r[1] = 0;
			s.r[2] = c.bcr1;
			s.r[3] = 0xFF800000;
			s.r[4] = c.wcr2;
			s.r[5] = 0xFF80000C;
			s.r[12] = DEVICE;
		});
		eq(c.name, st.r[1], 70);
	}
}

void test_store_queues()
{
	g_test = "store queues";
	{
		/* 64 words in a row: mov.l r3, @r4 / add #4, r4 / add #1, r3 /
		 * dt r5 / bf the store / bra itself */
		Machine m;
		with_queues(m);
		run(m, {0x2432, 0x7404, 0x7301, 0x4510, 0x8BFA, 0xAFFE, NOP}, 400, [](Sh4State &s) {
			s.r[3] = 0x51510000;
			s.r[4] = queue(DATA + 0x100);
			s.r[5] = 64;
		});
		unsigned wrong = 0;
		for (uint32_t i = 0; i < 64; i++) {
			wrong += m.get32(DATA + 0x100 + 4 * i) != 0x51510000 + i;
		}
		eq("words that are not what was stored", wrong, 0);
		eq("stores the bus took", m.ram_by_bus, 0);
	}
	{
		/* Every size, and nothing beside: mov.b r1, @r8 / mov.w r1, @r9 /
		 * mov.l r1, @r10 / fmov dr0, @r11 */
		Machine m;
		with_queues(m);
		std::fill(m.ram.begin() + (DATA + 0x300 - RAM), m.ram.begin() + (DATA + 0x348 - RAM), 0xEE);
		run(m, {0x2810, 0x2911, 0x2A12, 0xFB0A}, 4, [](Sh4State &s) {
			s.r[1] = 0x11223344;
			s.r[8] = queue(DATA + 0x301);
			s.r[9] = queue(DATA + 0x312);
			s.r[10] = queue(DATA + 0x324);
			s.r[11] = queue(DATA + 0x338);
			s.fr[0] = 0xAAAAAAAA;
			s.fr[1] = 0xBBBBBBBB;
			s.fpscr = 0x00140001;
		});
		eq("a byte, between two left alone", m.get32(DATA + 0x300) & 0xFFFFFF, 0xEE44EE);
		eq("a word", m.get32(DATA + 0x311), 0xEE3344EE);
		eq("a long", m.get32(DATA + 0x324), 0x11223344);
		eq("with nothing before, nothing after", m.get8(DATA + 0x323) + m.get8(DATA + 0x328), 2 * 0xEE);
		eq("a pair, its first half", m.get32(DATA + 0x338), 0xAAAAAAAA);
		eq("its second half", m.get32(DATA + 0x33C), 0xBBBBBBBB);
		eq("with nothing before, nothing after", m.get8(DATA + 0x337) + m.get8(DATA + 0x340), 2 * 0xEE);
		eq("stores the bus took", m.ram_by_bus, 0);
	}
	{
		/* A store that is not aligned goes the way of the bus. */
		Machine m;
		with_queues(m);
		run(m, {0x2432, NOP}, 2, [](Sh4State &s) {
			s.r[3] = 0x51515151;
			s.r[4] = queue(DATA + 0x202);
		});
		eq("the long stored", m.get32(DATA + 0x202), 0x51515151);
		eq("stores the bus took", m.ram_by_bus, 1);
	}
	{
		/* A store that changes an instruction further in its own block:
		 * mov.w @(8,pc), r0 [E203] / mov.w r0, @r8 / nop / nop /
		 * mov #1, r2 / nop / .word E203 */
		Machine m;
		with_queues(m);
		Sh4State st = run(m, {0x9004, 0x2801, NOP, NOP, 0xE201, NOP, 0xE203, NOP}, 6,
		                  [](Sh4State &s) { s.r[8] = queue(CODE + 8); });
		eq("the new instruction runs", st.r[2], 3);
	}
	{
		/* The loop of test_blocks() that stores add #k, r2 over the
		 * first instruction of the block it branches to at once, the
		 * store through a queue. */
		Machine m;
		with_queues(m);
		m.code(CODE, {0x7401, 0x6043, 0xC907, 0x6503, 0x9004, 0x305C, 0xD102, 0x2101, 0xA00A, NOP,
		              0x7200, NOP, NOP, NOP, NOP, NOP, NOP, NOP, NOP, NOP,
		              0x7201, 0xAFE9, NOP});
		m.put32(CODE + 24, queue(CODE + 40));
		for (uint32_t i = 0; i < 40; i++) {
			m.cpu.run(static_cast<int32_t>(70 + 13 * i));
		}
		const Sh4State st = m.state();
		uint32_t done = 0;
		for (uint32_t turn = 1; turn < st.r[4]; turn++) {
			done += turn & 7;
		}
		check(st.r[4] > 500, "the loop went round");
		check(st.r[2] == done || st.r[2] == done + (st.r[4] & 7), "each turn ran the instruction it stored");
	}
	{
		/* The same with the block on another page, jumped to: add #1, r4 /
		 * mov r4, r0 / and #7, r0 / mov r0, r5 / mov.w @(8,pc), r0 [7200] /
		 * add r5, r0 / mov.w r0, @r7 / jmp @r6; there: add #k, r2 /
		 * jmp @r13 */
		const uint32_t there = CODE + 0x2000;
		Machine m;
		with_queues(m);
		m.code(CODE, {0x7401, 0x6043, 0xC907, 0x6503, 0x9004, 0x305C, 0x2701, 0x462B, NOP, NOP, 0x7200});
		m.code(there, {0x7201, 0x4D2B, NOP});
		Sh4State set = m.state();
		set.r[6] = there;
		set.r[7] = queue(there);
		set.r[13] = CODE;
		m.cpu.load(set);
		for (uint32_t i = 0; i < 40; i++) {
			m.cpu.run(static_cast<int32_t>(70 + 13 * i));
		}
		const Sh4State st = m.state();
		uint32_t done = 0;
		for (uint32_t turn = 1; turn < st.r[4]; turn++) {
			done += turn & 7;
		}
		check(st.r[4] > 500, "the loop went round");
		check(st.r[2] == done || st.r[2] == done + (st.r[4] & 7),
		      "on another page, each turn ran the instruction it stored");
	}
	/* The same loop on two machines, one whose page of data is written
	 * through the bus: the same after each slice, and the interrupt of
	 * the device comes at the same instruction. */
	for (uint32_t seed = 1; seed <= 8; seed++) {
		Machine place, bus;
		unsigned differ = 0;

		queue_loop(place, seed);
		queue_loop(bus, seed);
		bus.cpu.map_page((DATA & 0x1FFFFFFF) >> Sh4::PAGE_BITS, &bus.ram[DATA - RAM], nullptr);
		for (uint32_t done = 0, series = seed; done < 30000 && differ == 0;) {
			series = series * 1664525u + 1013904223u;
			const uint32_t cycles = 1 + (series >> 8) % 900;
			const int32_t over_place = place.cpu.run(static_cast<int32_t>(cycles));
			const int32_t over_bus = bus.cpu.run(static_cast<int32_t>(cycles));
			const Sh4State a = place.state(), b = bus.state();

			done += cycles;
			if (over_place != over_bus || std::memcmp(&a, &b, sizeof(a)) != 0 || place.ram != bus.ram ||
			    place.onchip != bus.onchip || place.seen != bus.seen ||
			    place.device.size() != bus.device.size()) {
				differ++;
			}
		}
		check(differ == 0, "loop " + std::to_string(seed) + ": the same in place and through the bus");
		check(place.device.size() > 500 && place.ram_by_bus == 0, "the stores were made in place");
		check(bus.ram_by_bus > 500, "and through the bus on the other machine");
	}
	{
		Pair probe;
		if (!probe.host.cpu.translates()) {
			return;
		}
	}
	for (uint32_t seed = 1; seed <= 4; seed++) {
		Pair p;

		p.each([seed](Machine &m) { queue_loop(m, seed); });
		p.slices_of(30000, seed % 2 ? 400 : 6313, seed);
		p.verdict("stores through a queue, loop " + std::to_string(seed));
	}
}

/* Channel 2 on auto-request, started by a program from a table at RAM +
 * 0xE000: SAR2, DAR2, TCR2, DMAOR, then CHCR2. */
void dma(Machine &m, uint32_t sar, uint32_t dar, uint32_t tcr, uint32_t chcr)
{
	const uint32_t table = RAM + 0xE000;

	m.put32(table, 0xFFA00020);
	m.put32(table + 4, sar);
	m.put32(table + 8, dar);
	m.put32(table + 12, chcr);
	m.put32(table + 16, tcr);
	/* mov.l @r8,r1; mov.l @(4,r8),r0; mov.l r0,@r1; mov.l @(8,r8),r0; mov.l r0,@(4,r1); mov.l @(16,r8),r0;
	 * mov.l r0,@(8,r1); mov #1,r0; mov.l r0,@(32,r1) (DMAOR); mov.l @(12,r8),r0; mov.l r0,@(12,r1) (CHCR2) */
	run(m, {0x5180, 0x5081, 0x2102, 0x5082, 0x1101, 0x5084, 0x1102, 0xE001, 0x1108, 0x5083, 0x1103}, 11,
	    [table](Sh4State &s) {
		s.r[8] = table;
		s.sr |= SR_BL;
	});
}

uint8_t pattern(uint32_t i)
{
	return static_cast<uint8_t>(i * 7 + (i >> 8) * 13 + 1);
}

/* The DMA controller moves runs of units at once where both sides are
 * memories in place: the same bytes as unit after unit, a span of the bus
 * asked for where each run starts, and never for both sides. */
void test_dmac_blocks()
{
	g_test = "DMAC blocks";
	{
		/* RAM to the window, 4 bytes a unit: runs end with the pages of
		 * the RAM and the spans of 0x300 bytes. */
		Machine m;
		const uint32_t src = RAM + 0x10040, dst = WINDOW + 0x100, longs = 0x500, bytes = 4 * longs;

		for (uint32_t i = 0; i < bytes; i++) {
			m.ram[src - RAM + i] = pattern(i);
		}
		m.span_limit = 0x300;
		dma(m, src, dst, longs, 0x5435);
		check(std::equal(m.window.begin() + 0x100, m.window.begin() + 0x100 + bytes, m.ram.begin() + (src - RAM)),
		      "RAM to the window: the same bytes");
		check(m.window[0xFF] == 0 && m.window[0x100 + bytes] == 0, "RAM to the window: nothing around them");
		eq("RAM to the window: accesses by the bus", m.window_by_bus, 0);

		std::vector<uint32_t> starts;
		for (uint32_t at = 0; at < bytes;) {
			starts.push_back(dst + at);
			at += std::min({Sh4::PAGE_SIZE - ((src + at) & (Sh4::PAGE_SIZE - 1)), 0x300u, bytes - at});
		}
		bool same = m.spans.size() == starts.size();
		for (size_t i = 0; same && i < starts.size(); i++) {
			same = m.spans[i].write && m.spans[i].addr == starts[i];
		}
		check(same, "RAM to the window: a span of the window asked for where each run starts");
		same = m.spans_written.size() == starts.size();
		for (size_t i = 0; same && i < starts.size(); i++) {
			same = m.spans_written[i].addr == starts[i] &&
			       m.spans_written[i].size == (i + 1 < starts.size() ? starts[i + 1] : dst + bytes) - starts[i];
		}
		check(same, "RAM to the window: the bus told of each run written into a span");
		eq("RAM to the window: SAR2", m.reg32(0xFFA00020), src + bytes);
		eq("RAM to the window: DAR2", m.reg32(0xFFA00024), dst + bytes);
		eq("RAM to the window: TCR2", m.reg32(0xFFA00028), 0);
		eq("RAM to the window: TE", m.reg32(0xFFA0002C) & 2, 2);
		eq("RAM to the window: DMTE2", m.state().irq[Source::DMTE2].raised, 1);
	}
	for (size_t limit : {size_t(100), size_t(16)}) {
		/* The window to RAM, 32 bytes a unit, from its address in P2:
		 * spans of three units and a bit, then spans shorter than a
		 * unit, whose units go by the bus. */
		Machine m;
		const uint32_t src = 0xA0000000 | (WINDOW + 0x40), dst = RAM + 0x20000, units = 64, bytes = 32 * units;
		const std::string what = "the window to RAM in spans of " + std::to_string(limit) + ": ";

		for (uint32_t i = 0; i < bytes; i++) {
			m.window[0x40 + i] = pattern(i);
		}
		m.span_limit = limit;
		dma(m, src, dst, units, 0x5445);
		check(std::equal(m.ram.begin() + 0x20000, m.ram.begin() + 0x20000 + bytes, m.window.begin() + 0x40),
		      what + "the same bytes");
		eq((what + "accesses by the bus").c_str(), m.window_by_bus, limit == 100 ? 0 : 4 * units);

		const uint32_t run = limit == 100 ? 96 : 32;
		bool same = m.spans.size() == (bytes + run - 1) / run;
		for (size_t i = 0; same && i < m.spans.size(); i++) {
			same = !m.spans[i].write && m.spans[i].addr == WINDOW + 0x40 + run * i;
		}
		check(same, what + "a span asked for where each run starts");
		eq((what + "TCR2").c_str(), m.reg32(0xFFA00028), 0);
	}
	{
		/* Over itself, forward and back: as unit after unit, eight
		 * bytes at most at a time. */
		struct Over {
			uint32_t chcr, unit;
			int32_t shift;
		};
		for (const Over &o : {Over{0x5435, 4, 4}, Over{0x5435, 4, -4}, Over{0x5405, 8, 8}, Over{0x5445, 32, 32},
		                      Over{0x5445, 32, -32}}) {
			Machine m;
			const uint32_t units = 16, src = RAM + 0x30040, dst = src + static_cast<uint32_t>(o.shift);

			for (uint32_t i = 0; i < 0x400; i++) {
				m.ram[0x30000 + i] = pattern(i);
			}
			std::vector<uint8_t> want(m.ram.begin() + 0x30000, m.ram.begin() + 0x30400);
			const uint32_t piece = std::min(o.unit, 8u);
			for (uint32_t at = 0; at < units * o.unit; at += piece) {
				std::memmove(&want[dst - RAM - 0x30000 + at], &want[src - RAM - 0x30000 + at], piece);
			}
			dma(m, src, dst, units, o.chcr);
			check(std::equal(want.begin(), want.end(), m.ram.begin() + 0x30000),
			      "over itself, " + std::to_string(o.unit) + " bytes a unit, " + std::to_string(o.shift) +
			              " bytes away: as unit after unit");
		}
	}
	{
		/* Both sides in the window: no span asked for, each unit by the
		 * bus. */
		Machine m;

		for (uint32_t i = 0; i < 0x100; i++) {
			m.window[i] = pattern(i);
		}
		m.span_limit = 0x1000;
		dma(m, WINDOW, WINDOW + 0x8000, 0x40, 0x5435);
		check(std::equal(m.window.begin(), m.window.begin() + 0x100, m.window.begin() + 0x8000),
		      "the window to itself: the same bytes");
		eq("the window to itself: spans asked for", m.spans.size(), 0);
		eq("the window to itself: accesses by the bus", m.window_by_bus, 2 * 0x40);
	}
	{
		/* The source going down: unit after unit by the bus. */
		Machine m;
		const uint32_t src = RAM + 0x40100, longs = 0x40;

		for (uint32_t i = 0; i < 0x100; i++) {
			m.ram[0x40000 + i] = pattern(i);
		}
		m.span_limit = 0x1000;
		dma(m, src, WINDOW, longs, 0x6435);
		bool same = true;
		for (uint32_t i = 0; i < longs; i++) {
			same = same && std::equal(m.window.begin() + 4 * i, m.window.begin() + 4 * i + 4,
			                          m.ram.begin() + (src - RAM) - 4 * (i + 1));
		}
		check(same, "a source going down: the longs from the last down");
		eq("a source going down: spans asked for", m.spans.size(), 0);
		eq("a source going down: SAR2", m.reg32(0xFFA00020), src - 4 * longs);
	}
}

/* Displacements of a branch and of a PC-relative load or mova at `from` */
uint16_t disp8(uint32_t from, uint32_t to) { return static_cast<uint16_t>(((to - from - 4) >> 1) & 0xFF); }
uint16_t disp12(uint32_t from, uint32_t to) { return static_cast<uint16_t>(((to - from - 4) >> 1) & 0xFFF); }
uint16_t pool(uint32_t from, uint32_t to) { return static_cast<uint16_t>((to - ((from & ~3u) + 4)) >> 2); }

constexpr uint32_t TCNT0 = 0xFFD8000C;

/* The first instructions at CODE start timer channel 0: TCOR0 and TCNT0
 * from `tcor`, TCR0 `tcr`, IPRA `ipra`, then TSTR. The program goes on at
 * CODE + 0x0C. mov.l r5,@r1 / mov.l r5,@(4,r1) / mov.w r0,@(8,r1) /
 * mov.w r6,@r3 / add #-4,r1 / mov.b r7,@r1 */
void start_timer(Machine &m, Sh4State &st, uint32_t tcor, uint32_t tcr, uint32_t ipra)
{
	m.code(CODE, {0x2152, 0x1151, 0x8114, 0x2361, 0x71FC, 0x2170});
	st.r[0] = tcr;
	st.r[1] = 0xFFD80008;
	st.r[3] = 0xFFD00004;
	st.r[5] = tcor;
	st.r[6] = ipra;
	st.r[7] = 1;
}

/* The wait of PHARRIER (MARKDOWN/101), on TCNT0 at Pck/16, TCOR0 = 120:
 * the date kept in a table, a call that works out the counts since it,
 * through the FPU and a second call, until they are over 99; then a new
 * date. With `irq`, the underflow interrupts and its handler counts in
 * R13. R10 counts the waits. */
void pharrier_wait(Machine &m, bool irq)
{
	constexpr uint32_t O = CODE + 0x0C, F = CODE + 0x100, G = CODE + 0x180, H = RAM + 0x6600;
	Sh4State st = m.state();

	start_timer(m, st, 120, irq ? 0x21 : 0x01, irq ? 0xF000 : 0);
	/* mov.l &table,r3 / mov.l &TCNT0,r2 / mov.l @r2,r0 / mov.l r0,@r3; then the wait: jsr @r11 /
	 * mov #0,r4 / cmp/hi r9,r0 / bf/s back / mov #79,r1; add #1,r10 / bra to the date / nop */
	m.code(O, {static_cast<uint16_t>(0xD300 | pool(O, O + 0x1C)), static_cast<uint16_t>(0xD200 | pool(O + 2, O + 0x20)),
	           0x6022, 0x2302, 0x4B0B, 0xE400, 0x3096, static_cast<uint16_t>(0x8F00 | disp8(O + 0x0E, O + 0x08)),
	           0xE14F, 0x7A01, static_cast<uint16_t>(0xA000 | disp12(O + 0x14, O)), NOP});
	m.put32(O + 0x1C, DATA);
	m.put32(O + 0x20, TCNT0);
	/* mov.l r14,@-r15 / mov r4,r0 / sts.l pr,@-r15 / mov r15,r14 / mov.l &table,r1 / shll2 r0 /
	 * mov.l &TCNT0,r2 / mov.l @(r0,r1),r0 / mov.l @r2,r1 / bra / sub r1,r0 / cmp/pz r0 / bf/s wrapped /
	 * lds r0,fpul / bra on / float fpul,fr4; wrapped: add #121,r0 / lds r0,fpul / float fpul,fr4 / nop;
	 * on: mova k,r0 / fmov.s @r0,fr1 / mov.l &G,r1 / jsr @r1 / fmul fr1,fr4 / mov r14,r15 /
	 * lds.l @r15+,pr / rts / mov.l @r15+,r14 */
	m.code(F, {0x2FE6, 0x6043, 0x4F22, 0x6EF3, static_cast<uint16_t>(0xD100 | pool(F + 0x08, F + 0x60)), 0x4008,
	           static_cast<uint16_t>(0xD200 | pool(F + 0x0C, F + 0x64)), 0x001E, 0x6122,
	           static_cast<uint16_t>(0xA000 | disp12(F + 0x12, F + 0x16)), 0x3018, 0x4011,
	           static_cast<uint16_t>(0x8F00 | disp8(F + 0x18, F + 0x20)), 0x405A,
	           static_cast<uint16_t>(0xA000 | disp12(F + 0x1C, F + 0x28)), 0xF42D,
	           0x7079, 0x405A, 0xF42D, NOP,
	           static_cast<uint16_t>(0xC700 | pool(F + 0x28, F + 0x68)), 0xF108,
	           static_cast<uint16_t>(0xD100 | pool(F + 0x2C, F + 0x6C)), 0x410B, 0xF412, 0x6FE3, 0x4F26, 0x000B,
	           0x6EF6});
	m.put32(F + 0x60, DATA);
	m.put32(F + 0x64, TCNT0);
	m.put32(F + 0x68, bits(0.5f));
	m.put32(F + 0x6C, G);
	/* mova 2^31,r0 / fmov.s @r0,fr1 / mov.l r14,@-r15 / fcmp/gt fr4,fr1 / bf/s big / mov r15,r14 /
	 * ftrc fr4,fpul / bra out / sts fpul,r0; big: mov #-1,r0; out: mov r14,r15 / rts / mov.l @r15+,r14 */
	m.code(G, {static_cast<uint16_t>(0xC700 | pool(G, G + 0x20)), 0xF108, 0x2FE6, 0xF145,
	           static_cast<uint16_t>(0x8F00 | disp8(G + 0x08, G + 0x12)), 0x6EF3, 0xF43D,
	           static_cast<uint16_t>(0xA000 | disp12(G + 0x0E, G + 0x14)), 0x005A, 0xE0FF, 0x6FE3, 0x000B,
	           0x6EF6});
	m.put32(G + 0x20, bits(2147483648.0f));
	/* The handler, in bank 1: mov.l &TCR0,r1 / mov #0x21,r0 / mov.w r0,@r1 (UNF cleared) / add #1,r13 /
	 * rte / nop */
	m.code(H, {static_cast<uint16_t>(0xD100 | pool(H, H + 0x10)), 0xE021, 0x2101, 0x7D01, 0x002B, NOP});
	m.put32(H + 0x10, 0xFFD80010);
	st.vbr = H - 0x600;
	st.r[9] = 49;
	st.r[11] = F;
	m.cpu.load(st);
}

/* A wait whose value goes through fmul fr4,fr4 (TCOR0 = 0x10000, Pck/16):
 * as long as (counts since the date - 50.0)^2 is over 400, then a new date.
 * A square does not move one way: the turn is not counted. mov.l @r2,r3;
 * wait: mov.l @r2,r1 / mov r3,r0 / sub r1,r0 / lds r0,fpul /
 * float fpul,fr4 / fsub fr2,fr4 / fmul fr4,fr4 / fcmp/gt fr1,fr4 / bt wait;
 * add #1,r10 / bra to the date / nop */
void square_wait(Machine &m)
{
	constexpr uint32_t W = CODE + 0x0E;
	Sh4State st = m.state();

	start_timer(m, st, 0x10000, 0x01, 0);
	m.code(CODE + 0x0C, {0x6322, 0x6122, 0x6033, 0x3018, 0x405A, 0xF42D, 0xF421, 0xF442, 0xF415,
	                     static_cast<uint16_t>(0x8900 | disp8(W + 0x10, W)), 0x7A01,
	                     static_cast<uint16_t>(0xA000 | disp12(W + 0x14, CODE + 0x0C)), NOP});
	st.r[2] = TCNT0;
	st.fr[1] = bits(400.0f);
	st.fr[2] = bits(50.0f);
	m.cpu.load(st);
}

/* A long turn, 45 cycles: about three counts of Pck/4 and, its length
 * odd, a read on every cycle of a count in time. It leaves TCNT0 in FPUL
 * while that is over 0x200 (TCOR0 = 0x400); out of the wait, what the last
 * turn left there is added up in R12: the registers of the last turn
 * counted are those that turn would have left. A delay out of the wait,
 * drawn from R3 (R3 = R3 * R11 + R13), moves the reads across the counts.
 * wait: mov.l @r2,r1 / cmp/hi r9,r1 / bf out / lds r1,fpul / 39 nop /
 * bra wait / nop; out: sts fpul,r0 / add r0,r12 / add #1,r10 /
 * mul.l r11,r3 / sts macl,r3 / add r13,r3 / mov r3,r0 / shlr16 r0 /
 * and #15,r0 / add #1,r0 / dt r0 / bf back to dt / bra wait / nop */
void leaving_wait(Machine &m)
{
	constexpr uint32_t W = CODE + 0x0C, OUT = W + 0x60;
	Sh4State st = m.state();

	start_timer(m, st, 0x400, 0x00, 0);
	m.code(W, {0x6122, 0x3196, static_cast<uint16_t>(0x8B00 | disp8(W + 4, OUT)), 0x415A});
	m.code(W + 0x56, {static_cast<uint16_t>(0xA000 | disp12(W + 0x56, W)), NOP});
	m.code(OUT, {0x005A, 0x3C0C, 0x7A01, 0x03B7, 0x031A, 0x33DC, 0x6033, 0x4029, 0xC90F, 0x7001, 0x4010, 0x8BFD,
	             static_cast<uint16_t>(0xA000 | disp12(OUT + 0x18, W)), NOP});
	st.r[2] = TCNT0;
	st.r[9] = 0x200;
	st.r[11] = 1664525;
	st.r[13] = 1013904223;
	m.cpu.load(st);
}

/* A wait on the counts since a date in the memory (Pck/16, TCOR0 =
 * 0x10000), whose way also goes by that date: one more pair of nop when it
 * is odd. The load of the date is taken by mov r0,r5: compared, not
 * loaded again, so a turn found for a date is not counted for another.
 * mov.l @r2,r3 / mov.l r3,@r6; wait: mov.l @r6,r0 / mov r0,r5 /
 * mov.l @r2,r1 / sub r1,r0 / cmp/hi r9,r0 / bt out / mov r5,r0 / tst #1,r0 /
 * bt even / nop / nop; even: bra wait / nop; out: add #1,r10 /
 * bra to the date / nop */
void dated_wait(Machine &m)
{
	constexpr uint32_t W = CODE + 0x10, OUT = W + 0x1A;
	Sh4State st = m.state();

	start_timer(m, st, 0x10000, 0x01, 0);
	m.code(CODE + 0x0C, {0x6322, 0x2632, 0x6062, 0x6503, 0x6122, 0x3018, 0x3096,
	                     static_cast<uint16_t>(0x8900 | disp8(W + 0x0A, OUT)), 0x6053, 0xC801,
	                     static_cast<uint16_t>(0x8900 | disp8(W + 0x10, W + 0x16)), NOP, NOP,
	                     static_cast<uint16_t>(0xA000 | disp12(W + 0x16, W)), NOP, 0x7A01,
	                     static_cast<uint16_t>(0xA000 | disp12(OUT + 2, CODE + 0x0C)), NOP});
	st.r[2] = TCNT0;
	st.r[6] = DATA + 0x200;
	st.r[9] = 40;
	m.cpu.load(st);
}

/* A wait while TCNT0 is over a bound (Pck/4, TCOR0 = 0x200), then one on
 * the underflow; the bound one count higher at each, in R9 or in the
 * memory: a turn found for a bound is not counted for another. In R9, the
 * turn stores R7 where the bound goes out of it: a turn found for a word
 * there is not counted for another either. wait: mov.l @r2,r1 /
 * mov.l @r6,r9 or mov.l r7,@r6 / cmp/hi r9,r1 / bt wait; add #1,r10;
 * under: mov.w @r8,r0 (TCR0) / shlr8 r0 / tst #1,r0 / bt under /
 * mov #0,r0 / mov.w r0,@r8 / add #1,r9 / mov.l r9,@r6 / bra wait / nop */
void bound_wait(Machine &m, bool in_memory)
{
	constexpr uint32_t W = CODE + 0x0C;
	Sh4State st = m.state();

	start_timer(m, st, 0x200, 0x00, 0);
	m.code(W, {0x6122, static_cast<uint16_t>(in_memory ? 0x6962 : 0x2672), 0x3196,
	           static_cast<uint16_t>(0x8900 | disp8(W + 6, W)), 0x7A01, 0x6081, 0x4019, 0xC801,
	           static_cast<uint16_t>(0x8900 | disp8(W + 0x10, W + 0x0A)), 0xE000, 0x2801, 0x7901, 0x2692,
	           static_cast<uint16_t>(0xA000 | disp12(W + 0x1A, W)), NOP});
	m.put32(DATA + 0x300, 0x40);
	st.r[2] = TCNT0;
	st.r[6] = DATA + 0x300;
	st.r[8] = 0xFFD80010;
	st.r[9] = 0x40;
	m.cpu.load(st);
}

/* A wait that stores R7 while TCNT0 is over 0x100 (Pck/4, TCOR0 = 0x200);
 * the wait on the underflow after it stores R10 there: a turn found for a
 * word there is not counted for another. wait: mov.l @r2,r1 /
 * cmp/hi r9,r1 / bf out / mov.l r7,@(4,r6) / bra wait / nop; out:
 * add #1,r10; under: mov.w @r8,r0 / shlr8 r0 / tst #1,r0 / bt under /
 * mov #0,r0 / mov.w r0,@r8 / mov.l r10,@(4,r6) / bra wait / nop */
void storing_wait(Machine &m)
{
	constexpr uint32_t W = CODE + 0x0C;
	Sh4State st = m.state();

	start_timer(m, st, 0x200, 0x00, 0);
	m.code(W, {0x6122, 0x3196, static_cast<uint16_t>(0x8B00 | disp8(W + 4, W + 0x0C)), 0x1671,
	           static_cast<uint16_t>(0xA000 | disp12(W + 8, W)), NOP, 0x7A01, 0x6081, 0x4019, 0xC801,
	           static_cast<uint16_t>(0x8900 | disp8(W + 0x14, W + 0x0E)), 0xE000, 0x2801, 0x16A1,
	           static_cast<uint16_t>(0xA000 | disp12(W + 0x1C, W)), NOP});
	st.r[2] = TCNT0;
	st.r[6] = DATA + 0x300;
	st.r[8] = 0xFFD80010;
	st.r[9] = 0x100;
	m.cpu.load(st);
}

/* The wait on a saw below, that stores R7 while it waits; out of it, what
 * is there is added up in R12 and R10 stored there: a turn found for a word
 * there is not counted for another. With `interrupted`, channel 1 (TCOR1 =
 * 50, Pck/4) interrupts too, and its handler stores there a count of its
 * own (R2 of bank 1). wait: mov.l @r2,r1 / mov r1,r0 / shll16 r0 /
 * shll8 r0 / cmp/hi r9,r0 / bt out / mov.l r7,@(4,r6) / bra wait / nop;
 * out: mov.l @(4,r6),r0 / add r0,r12 / add #1,r10 / mov.l r10,@(4,r6) /
 * bra wait / nop */
void reading_wait(Machine &m, bool interrupted)
{
	const uint32_t W = interrupted ? CODE + 0x16 : CODE + 0x0C;
	constexpr uint32_t H = RAM + 0x6600;
	Sh4State st = m.state();

	start_timer(m, st, 0x30000, 0x00, interrupted ? 0x0F00 : 0);
	if (interrupted) {
		/* mov.l r11,@r4 (TCOR1) / mov.l r11,@(4,r4) (TCNT1) / mov #0x20,r0 / mov.w r0,@(8,r4) (TCR1) /
		 * mov.b r13,@r1 (TSTR = 3) */
		m.code(CODE + 0x0C, {0x24B2, 0x14B1, 0xE020, 0x8144, 0x21D0});
		/* mov.l &word,r1 / add #1,r2 / mov.l r2,@r1 / mov.l &TCR1,r1 / mov #0x20,r0 / mov.w r0,@r1 /
		 * rte / nop */
		m.code(H, {static_cast<uint16_t>(0xD100 | pool(H, H + 0x14)), 0x7201, 0x2122,
		           static_cast<uint16_t>(0xD100 | pool(H + 6, H + 0x18)), 0xE020, 0x2101, 0x002B, NOP});
		m.put32(H + 0x14, DATA + 0x304);
		m.put32(H + 0x18, 0xFFD8001C);
		st.vbr = H - 0x600;
		st.r[4] = 0xFFD80014;
		st.r[11] = 50;
		st.r[13] = 3;
	}
	m.code(W, {0x6122, 0x6013, 0x4028, 0x4018, 0x3096, static_cast<uint16_t>(0x8900 | disp8(W + 0x0A, W + 0x12)),
	           0x1671, static_cast<uint16_t>(0xA000 | disp12(W + 0x0E, W)), NOP, 0x5061, 0x3C0C, 0x7A01, 0x16A1,
	           static_cast<uint16_t>(0xA000 | disp12(W + 0x1A, W)), NOP});
	st.r[2] = TCNT0;
	st.r[6] = DATA + 0x300;
	st.r[9] = 0xF0000000;
	m.cpu.load(st);
}

/* A wait on TCNT0 << 24 (Pck/4, TCOR0 = 0x30000): over 0xF0000000 for 15
 * counts in 256, a saw. The turns are counted only between two of its
 * teeth. wait: mov.l @r2,r1 / mov r1,r0 / shll16 r0 / shll8 r0 /
 * cmp/hi r9,r0 / bf wait; add #1,r10 / bra wait / nop */
void saw_wait(Machine &m)
{
	constexpr uint32_t W = CODE + 0x0C;
	Sh4State st = m.state();

	start_timer(m, st, 0x30000, 0x00, 0);
	m.code(W, {0x6122, 0x6013, 0x4028, 0x4018, 0x3096, static_cast<uint16_t>(0x8B00 | disp8(W + 0x0A, W)), 0x7A01,
	           static_cast<uint16_t>(0xA000 | disp12(W + 0x0E, W)), NOP});
	st.r[2] = TCNT0;
	st.r[9] = 0xF0000000;
	m.cpu.load(st);
}

/* Waits whose turns are not counted, at Pck/4. One counts its turns, in
 * the memory or in R12, while TCNT0 is over 0x200 (TCOR0 = 0x400):
 * mov.l @r2,r1 / mov.l @r6,r7 / add #1,r7 / mov.l r7,@r6 / mov #0,r7 or
 * add #1,r12 / cmp/hi r9,r1 / bt wait; add #1,r10 / bra wait / nop. */
void counting_wait(Machine &m, bool in_memory)
{
	constexpr uint32_t W = CODE + 0x0C;
	Sh4State st = m.state();

	start_timer(m, st, 0x400, 0x00, 0);
	if (in_memory) {
		m.code(W, {0x6122, 0x6762, 0x7701, 0x2672, 0xE700, 0x3196, static_cast<uint16_t>(0x8900 | disp8(W + 0x0C, W)),
		           0x7A01, static_cast<uint16_t>(0xA000 | disp12(W + 0x10, W)), NOP});
	} else {
		m.code(W, {0x6122, 0x7C01, 0x3196, static_cast<uint16_t>(0x8900 | disp8(W + 6, W)), 0x7A01,
		           static_cast<uint16_t>(0xA000 | disp12(W + 0x0A, W)), NOP});
	}
	st.r[2] = TCNT0;
	st.r[6] = DATA + 0x100;
	st.r[9] = 0x200;
	m.cpu.load(st);
}

/* The other adds up what it reads until the sum is over 0x200000 (TCOR0 =
 * 0x10000): mov.l @r2,r1 / add r1,r0 / cmp/hi r9,r0 / bf wait; add #1,r10 /
 * bra wait / mov #0,r0 */
void summing_wait(Machine &m)
{
	constexpr uint32_t W = CODE + 0x0C;
	Sh4State st = m.state();

	start_timer(m, st, 0x10000, 0x00, 0);
	m.code(W, {0x6122, 0x301C, 0x3096, static_cast<uint16_t>(0x8B00 | disp8(W + 0x06, W)), 0x7A01,
	           static_cast<uint16_t>(0xA000 | disp12(W + 0x0A, W)), 0xE000});
	st.r[2] = TCNT0;
	st.r[9] = 0x200000;
	m.cpu.load(st);
}

/* Loops that wait on a timer (sh4_clock.cpp): one machine counts the turns
 * it can without running them, the other runs them all, read and as host
 * code, in slices of every length. After each slice the two must be the
 * same: registers, on-chip state, memory, the cycles run over and the
 * cycles in all, run or counted. */
void test_clock_waits()
{
	g_test = "clock waits";
	struct Wait {
		const char *name;
		std::function<void(Machine &)> set;
		bool counted;	/* some turns are */
		std::function<uint32_t(const Sh4State &)> interrupts = nullptr;	/* the count its handler keeps */
	};
	const Wait waits[] = {
		{"the wait of PHARRIER", [](Machine &m) { pharrier_wait(m, false); }, true},
		{"the wait of PHARRIER, the timer interrupting", [](Machine &m) { pharrier_wait(m, true); }, true,
		 [](const Sh4State &st) { return st.r[13]; }},
		{"a wait on a saw", saw_wait, true},
		{"a wait left with what its last turn left", leaving_wait, true},
		{"a wait whose date also chooses its way", dated_wait, true},
		{"a wait on a bound in a register, that stores", [](Machine &m) { bound_wait(m, false); }, true},
		{"a wait on a bound in the memory", [](Machine &m) { bound_wait(m, true); }, true},
		{"a wait that stores, the program storing there between waits", storing_wait, true},
		{"a wait that stores, read out of it", [](Machine &m) { reading_wait(m, false); }, true},
		{"a wait that stores, read out of it, an interrupt storing there",
		 [](Machine &m) { reading_wait(m, true); }, true, [](const Sh4State &st) { return st.rbank[2]; }},
		{"a wait through a square", square_wait, false},
		{"a wait that counts its turns in the memory", [](Machine &m) { counting_wait(m, true); }, false},
		{"a wait that counts its turns in a register", [](Machine &m) { counting_wait(m, false); }, false},
		{"a wait that adds up what it reads", summing_wait, false},
	};
	const auto same = [](const Machine &a, const Machine &b) {
		const Sh4State x = a.state(), y = b.state();

		return std::memcmp(&x, &y, sizeof(x)) == 0 && a.ram == b.ram && a.onchip == b.onchip &&
		       a.cpu.instructions() + a.cpu.skipped() == b.cpu.instructions() + b.cpu.skipped();
	};

	for (const Wait &w : waits) {
		for (const bool host : {false, true}) {
			for (const uint32_t longest : {200u, 3000u, 30000u}) {
				const std::string what = std::string(w.name) + (host ? ", as host code" : ", read") +
				                         ", slices up to " + std::to_string(longest);
				Machine on, off;
				uint32_t series = longest, slices = 0, differ = 0;

				on.cpu.set_translate(host);
				off.cpu.set_translate(host);
				off.cpu.set_clock_waits(false);
				w.set(on);
				w.set(off);
				const uint32_t total = longest < 10000 ? 300000 : 3000000;

				for (uint32_t done = 0; done < total && differ == 0; slices++) {
					series = series * 1664525u + 1013904223u;
					const uint32_t cycles = 1 + (series >> 8) % longest;
					const int32_t over_on = on.cpu.run(static_cast<int32_t>(cycles));
					const int32_t over_off = off.cpu.run(static_cast<int32_t>(cycles));

					differ += over_on != over_off || !same(on, off);
					done += cycles;
				}
				check(differ == 0, what + ": the same after each slice");
				check(on.state().r[10] > 4, what + ": waits went by");
				if (w.interrupts) {
					check(w.interrupts(on.state()) > 2, what + ": the timer interrupted");
				}
				if (longest > 2 * 128 + 64) {
					check(w.counted == (on.cpu.skipped() > off.cpu.skipped()),
					      what + (w.counted ? ": turns counted" : ": no turn counted"));
				}
			}
		}
	}
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
	test_quiet_page();
	test_others_write();
	test_hook();
	test_save_load();
	test_store_queues();
	test_mpx_time();
	test_dmac_blocks();
	test_clock_waits();
	std::printf("%d checks, %d failures\n", g_checks, g_failures);
	return g_failures == 0 ? 0 : 1;
}
