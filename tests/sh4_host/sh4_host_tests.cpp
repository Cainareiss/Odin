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

/* Unit tests for the host code of the Odin SH-4 core: the same programs on
 * two machines, one that reads its blocks and one that runs them as host
 * code (Sh4::translate()), the same registers, memory, accesses and counts
 * after each slice. The reader is the reference; tests/sh4 checks it
 * against the manuals.
 *
 * Build: g++ -std=c++20 -O2 -Wall -Wextra -Wpedantic -Wconversion -Wshadow \
 *            -ffp-contract=off -I src tests/sh4_host/sh4_host_tests.cpp src/odin/machine/cpu/sh4/sh4.cpp \
 *            src/odin/machine/cpu/sh4/sh4_ops.cpp src/odin/machine/cpu/sh4/sh4_onchip.cpp src/odin/machine/cpu/sh4/sh4_host.cpp src/odin/machine/cpu/sh4/sh4_translate.cpp \
 *            src/odin/machine/cpu/sh4/sh4_fsca.cpp src/odin/machine/cpu/sh4/sh4_clock.cpp src/odin/machine/core/host_code.cpp -o sh4_host_tests
 * Use:   sh4_host_tests [-v]
 * Exit status is 0 when every check passes. */

#include "../sh4/sh4_machine.hpp"

using namespace sh4_test;

namespace {

void test_host_code()
{
	g_test = "host code";
	{
		Pair probe;
		if (!probe.host.cpu.translates()) {
			std::printf("host code: none on this machine, not tested\n");
			return;
		}
	}

	/* Programs from a series, with the device and its interrupt. */
	for (uint32_t seed = 1; seed <= 40; seed++) {
		Pair p;

		p.each([seed](Machine &m) {
			with_device(m);
			random_program(m, 300 + 40 * (seed % 8), seed * 7919u);
		});
		p.slices_of(60000, seed % 2 ? 400 : 6313, seed);
		p.verdict("program " + std::to_string(seed));
	}

	/* Programs that go past the end of a page. */
	for (uint32_t seed = 41; seed <= 44; seed++) {
		Pair p;

		p.each([seed](Machine &m) {
			with_device(m);
			random_program(m, 2100 + 150 * (seed % 4), seed * 7919u);
		});
		p.slices_of(120000, 6313, seed);
		p.verdict("long program " + std::to_string(seed));
	}

	/* 200 instructions in a row: blocks that end for their length. */
	{
		Pair p;

		p.each([](Machine &m) {
			with_device(m);
			for (uint32_t i = 0; i < 200; i++) {
				m.put16(CODE + 2 * i, static_cast<uint16_t>(0x7001 | ((1 + i % 7) << 8)));	/* add #1, rn */
			}
			m.code(CODE + 400, { 0xAF36, 0x0009 });	/* bra CODE / nop */
		});
		p.slices_of(20000, 500, 7);
		p.verdict("blocks that end for their length");
	}

	/* A loop that stores an instruction, add #k, r2 with a k that
	 * changes, over one of another block, which then stores it over the
	 * instruction that follows the store. */
	{
		Pair p;

		p.each([](Machine &m) {
			with_device(m);
			m.code(CODE, {
				0x7401,		/* add #1, r4 */
				0x6043,		/* mov r4, r0 */
				0xC907,		/* and #7, r0 */
				0x6503,		/* mov r0, r5 */
				0x9004,		/* mov.w @(8, pc), r0: add #0, r2 */
				0x305C,		/* add r5, r0 */
				0xD102,		/* mov.l @(8, pc), r1: CODE + 46 */
				0x2101,		/* mov.w r0, @r1 */
				0xA00A,		/* bra CODE + 40 */
				0x0009,
				0x7200, 0x0009,
				0x0009, 0x0009,	/* CODE + 46 */
				0x0009, 0x0009, 0x0009, 0x0009, 0x0009, 0x0009,
				0xD102,		/* CODE + 40: mov.l @(8, pc), r1: CODE + 44 */
				0x2101,		/* mov.w r0, @r1 */
				0x7201,		/* add #1, r2: stored over by the store before it */
				0x7303,		/* add #3, r3: stored over by the first block */
				0xAFE6,		/* bra CODE */
				0x0009,
			});
			m.put32(CODE + 24, CODE + 46);
			m.put32(CODE + 52, CODE + 44);
		});
		p.slices_of(20000, 300, 5);
		p.verdict("stores over the code of another block and of the running one");
	}

	/* The same store over the first instruction of a block the loop
	 * branches to at once: nothing stops the program between the store
	 * and the block, which was run a turn before. */
	{
		Pair p;

		p.each([](Machine &m) {
			with_device(m);
			m.code(CODE, {0x7401, 0x6043, 0xC907, 0x6503, 0x9004, 0x305C, 0xD102, 0x2101, 0xA00A, 0x0009,
			              0x7200, 0x0009, 0x0009, 0x0009, 0x0009, 0x0009, 0x0009, 0x0009, 0x0009, 0x0009,
			              0x7201, 0xAFE9, 0x0009});
			m.put32(CODE + 24, CODE + 40);
		});
		p.slices_of(20000, 300, 5);
		p.verdict("a store over the block branched to at once");
	}

	/* An interrupt raised while another is served: RTE lets it in at
	 * once, before the instruction it returns to. */
	{
		Pair p;

		p.each([](Machine &m) {
			Machine *self = &m;

			with_device(m);
			/* The handler drops its request and raises IRL2 once. */
			m.on_device = [self] {
				const Machine::Access &last = self->device.back();

				self->seen.push_back(self->cpu.pc());
				if (last.write) {
					self->cpu.set_irq(Source::IRL1, false);
					self->cpu.set_irq(Source::IRL2, self->device.size() % 3 == 0);
				} else {
					self->cpu.set_irq(Source::IRL1, true);
				}
			};
			m.code(CODE, {
				0x7101,		/* add #1, r1 */
				0x60C2,		/* mov.l @r12, r0: the device raises IRL1 */
				0x7201,		/* add #1, r2 */
				0x7301,		/* add #1, r3 */
				0xAFFA,		/* bra CODE */
				0x7401,		/* add #1, r4 */
			});
		});
		p.slices_of(20000, 500, 9);
		p.verdict("an interrupt let in by RTE");
	}

	/* A request held while the program masks it: one turn in four, the
	 * delay slot of a branch lets it in, and it is taken before the
	 * instruction the branch goes to, which the handler can tell from
	 * R8. */
	{
		Pair p;

		p.each([](Machine &m) {
			Machine *self = &m;

			with_device(m);
			m.on_device = [self] {
				self->seen.push_back(self->cpu.pc());
				if (self->device.back().write) {
					self->cpu.set_irq(Source::IRL1, false);
				}
			};
			m.code(RAM + 0x7000, {
				0x1E83,		/* mov.l r8, @(12, r14) */
				0x1C01,		/* mov.l r0, @(4, r12): the request is dropped */
				0x002B, 0x0009,	/* rte / nop */
			});
			m.code(CODE, {
				0xD108,		/* mov.l @(32, pc), r1: SR with every level masked */
				0xD209,		/* mov.l @(36, pc), r2: SR with none */
				0x410E,		/* ldc r1, sr */
				0x7301,		/* CODE + 6: add #1, r3 */
				0x6033,		/* mov r3, r0 */
				0xC903,		/* and #3, r0 */
				0x8800,		/* cmp/eq #0, r0 */
				0x8B01,		/* bf CODE + 20 */
				0xA002,		/* bra CODE + 24 */
				0x420E,		/* ldc r2, sr: in the delay slot */
				0xA000,		/* CODE + 20: bra CODE + 24 */
				0x0009,
				0x7801,		/* CODE + 24: add #1, r8 */
				0x1E81,		/* mov.l r8, @(4, r14) */
				0x410E,		/* ldc r1, sr */
				0xAFF2,		/* bra CODE + 6 */
				0x0009, 0x0009,
			});
			m.put32(CODE + 36, SR_MD | 0xF0);
			m.put32(CODE + 40, SR_MD);
		});
		for (uint32_t i = 0, seed = 11; i < 200 && p.differ == 0; i++) {
			seed = seed * 1664525u + 1013904223u;
			p.each([](Machine &m) { m.cpu.set_irq(Source::IRL1, true); });
			p.slice(static_cast<int32_t>(100 + (seed >> 8) % 600));
		}
		p.verdict("a request let in by a delay slot");
		check(p.host.device.size() > 100, "the handler ran");
	}

	/* A timer read in the middle of a block, and a wait. */
	{
		Pair p;

		p.each([](Machine &m) {
			with_device(m);
			m.code(CODE, {
				0xD006,		/* mov.l @(24, pc), r0: TSTR */
				0xE101,		/* mov #1, r1 */
				0x2010,		/* mov.b r1, @r0: channel 0 runs */
				0xD006,		/* mov.l @(24, pc), r0: TCNT0 */
				0x7201,		/* add #1, r2 */
				0x6302,		/* mov.l @r0, r3 */
				0x1E31,		/* mov.l r3, @(4, r14) */
				0x7201,		/* add #1, r2 */
				0x6402,		/* mov.l @r0, r4 */
				0x1E42,		/* mov.l r4, @(8, r14) */
				0xAFFE,		/* bra itself: a wait */
				0x0009,
				0x0009, 0x0009,
			});
			m.put32(CODE + 28, 0xFFD80004);
			m.put32(CODE + 32, 0xFFD8000C);
		});
		p.slices_of(30000, 700, 3);
		p.verdict("a timer read in a block, then a wait");
		check(p.host.cpu.skipped() > 0, "the wait is counted");
	}
}

/* The instructions host code does itself, in place of a call: each of them
 * at the four places an instruction runs at (in a block, in the delay slot
 * of a jump, in the slot of a conditional branch that goes, after one that
 * does not), for registers of every kind and values at the edges, T clear
 * and set, the other registers holding values of their own: read and as
 * host code, the same registers after, and the same Rn and T after each of
 * the four (moved to R4-R7, R8-R11). */
void test_host_in_place()
{
	g_test = "host code, in place";
	struct Form {
		uint16_t code;
		char operands;	/* M: Rn and Rm; N: Rn; I: Rn and a byte; B: a byte; P: a byte, no delay slot; 0: none */
		const char *name;
	};
	static const Form forms[] = {
		{0x6003, 'M', "mov"}, {0xE000, 'I', "mov #imm"}, {0xC700, 'P', "mova"}, {0x0029, 'N', "movt"},
		{0x300C, 'M', "add"}, {0x7000, 'I', "add #imm"}, {0x3008, 'M', "sub"}, {0x600B, 'M', "neg"},
		{0x2009, 'M', "and"}, {0xC900, 'B', "and #imm"}, {0x200B, 'M', "or"}, {0xCB00, 'B', "or #imm"},
		{0x200A, 'M', "xor"}, {0xCA00, 'B', "xor #imm"}, {0x6007, 'M', "not"},
		{0x2008, 'M', "tst"}, {0xC800, 'B', "tst #imm"}, {0x3000, 'M', "cmp/eq"}, {0x8800, 'B', "cmp/eq #imm"},
		{0x3002, 'M', "cmp/hs"}, {0x3003, 'M', "cmp/ge"}, {0x3006, 'M', "cmp/hi"}, {0x3007, 'M', "cmp/gt"},
		{0x4011, 'N', "cmp/pz"}, {0x4015, 'N', "cmp/pl"},
		{0x600E, 'M', "exts.b"}, {0x600F, 'M', "exts.w"}, {0x600C, 'M', "extu.b"}, {0x600D, 'M', "extu.w"},
		{0x4000, 'N', "shll"}, {0x4020, 'N', "shal"}, {0x4001, 'N', "shlr"}, {0x4021, 'N', "shar"},
		{0x4008, 'N', "shll2"}, {0x4018, 'N', "shll8"}, {0x4028, 'N', "shll16"},
		{0x4009, 'N', "shlr2"}, {0x4019, 'N', "shlr8"}, {0x4029, 'N', "shlr16"},
		{0x4010, 'N', "dt"}, {0x0008, '0', "clrt"}, {0x0018, '0', "sett"}, {0x0009, '0', "nop"},
	};
	static const uint8_t pairs[][2] = {{1, 2}, {2, 1}, {1, 1}, {0, 3}, {3, 0}, {0, 0}, {15, 1}, {1, 15}};
	static const uint8_t singles[] = {0, 1, 15}, bytes[] = {0x00, 0x01, 0x7F, 0x80, 0xFF};
	static const uint32_t edges[] = {0, 1, 2, 0x7F, 0x80, 0xFF, 0x7FFF, 0x8000, 0xFFFF,
	                                 0x7FFFFFFF, 0x80000000, 0xFFFFFFFF, 0x12345678};
	Pair p;

	if (!p.host.cpu.translates()) {
		std::printf("host code: none on this machine, not tested\n");
		return;
	}
	const Sh4State start = p.read.state();
	for (const Form &form : forms) {
		std::vector<uint16_t> codes;
		unsigned runs = 0;

		p.differ = 0;

		switch (form.operands) {
		case 'M':
			for (const auto &pair : pairs) {
				codes.push_back(static_cast<uint16_t>(form.code | (pair[0] << 8) | (pair[1] << 4)));
			}
			break;
		case 'N':
			for (uint8_t n : singles) {
				codes.push_back(static_cast<uint16_t>(form.code | (n << 8)));
			}
			break;
		case 'I':
			for (uint8_t n : singles) {
				for (uint8_t byte : bytes) {
					codes.push_back(static_cast<uint16_t>(form.code | (n << 8) | byte));
				}
			}
			break;
		case 'B':
		case 'P':
			for (uint8_t byte : bytes) {
				codes.push_back(static_cast<uint16_t>(form.code | byte));
			}
			break;
		default:
			codes.push_back(form.code);
			break;
		}
		for (uint16_t code : codes) {
			/* MOVA is not allowed in a delay slot. */
			const uint16_t slot = form.operands == 'P' ? NOP : code;
			const bool has_n = form.operands == 'M' || form.operands == 'N' || form.operands == 'I';
			const uint32_t n = has_n ? (code >> 8) & 15 : 0, m = form.operands == 'M' ? (code >> 4) & 15 : 1;
			const auto keep = [n](uint32_t to) { return static_cast<uint16_t>(0x6003 | (to << 8) | (n << 4)); };

			/* the instruction / movt r8 / mov rn, r4 / bra / its slot / movt r9 / mov rn, r5 /
			 * sett / bt/s / its slot / movt r10 / mov rn, r6 / clrt / bt/s / the instruction /
			 * movt r11 / mov rn, r7 / bra itself */
			p.each([&](Machine &mc) {
				mc.code(CODE, {code, 0x0829, keep(4), 0xA000, slot, 0x0929, keep(5), 0x0018, 0x8D00, slot,
				               0x0A29, keep(6), 0x0008, 0x8D00, code, 0x0B29, keep(7), 0xAFFE, NOP});
			});
			for (uint32_t a : edges) {
				for (uint32_t b : edges) {
					for (uint32_t t = 0; t < 2 && p.differ == 0; t++) {
						Sh4State st = start;

						st.sr |= t;
						for (uint32_t i = 0; i < 16; i++) {
							st.r[i] = 0x5EED0000 + 0x1111 * i;
						}
						st.r[m] = b;
						st.r[n] = a;
						p.each([&st](Machine &mc) { mc.cpu.load(st); });
						p.slice(200);
						runs++;
					}
				}
			}
		}
		check(runs > 0 && p.differ == 0, std::string(form.name) + ": the same read and in place");
	}
}

/* The accesses to the memory host code makes itself: each of them at the
 * four places of test_host_in_place(), at an address of plain memory, at
 * one that is not aligned, at the device, where nothing answers and, for a
 * store, over the next instruction of its own block: read and as host
 * code, the same registers, memory and accesses after. */
void test_host_memory()
{
	g_test = "host code, memory";
	struct Form {
		uint16_t code;
		char operands;	/* M: Rn and Rm; D: Rn, Rm and a displacement; d: Rm and a displacement; N: Rn; G and P: a byte */
		uint32_t size;
		bool write;
		char base;	/* the register of the address: n, m, 0 for R0, G for GBR, P for PC */
		bool index;	/* plus R0 */
		int32_t step;	/* to the base: before a write, after a read */
		const char *name;
	};
	static const Form forms[] = {
		{0x6002, 'M', 4, false, 'm', false, 0, "mov.l @Rm,Rn"}, {0x6001, 'M', 2, false, 'm', false, 0, "mov.w @Rm,Rn"},
		{0x6000, 'M', 1, false, 'm', false, 0, "mov.b @Rm,Rn"},
		{0x5000, 'D', 4, false, 'm', false, 0, "mov.l @(disp,Rm),Rn"},
		{0x8500, 'd', 2, false, 'm', false, 0, "mov.w @(disp,Rm),R0"}, {0x8400, 'd', 1, false, 'm', false, 0, "mov.b @(disp,Rm),R0"},
		{0x000E, 'M', 4, false, 'm', true, 0, "mov.l @(R0,Rm),Rn"}, {0x000D, 'M', 2, false, 'm', true, 0, "mov.w @(R0,Rm),Rn"},
		{0x000C, 'M', 1, false, 'm', true, 0, "mov.b @(R0,Rm),Rn"},
		{0x6006, 'M', 4, false, 'm', false, 4, "mov.l @Rm+,Rn"}, {0x6005, 'M', 2, false, 'm', false, 2, "mov.w @Rm+,Rn"},
		{0x6004, 'M', 1, false, 'm', false, 1, "mov.b @Rm+,Rn"},
		{0xC600, 'G', 4, false, 'G', false, 0, "mov.l @(disp,GBR),R0"}, {0xC500, 'G', 2, false, 'G', false, 0, "mov.w @(disp,GBR),R0"},
		{0xC400, 'G', 1, false, 'G', false, 0, "mov.b @(disp,GBR),R0"},
		{0xD010, 'P', 4, false, 'P', false, 0, "mov.l @(disp,PC),Rn"}, {0x9028, 'P', 2, false, 'P', false, 0, "mov.w @(disp,PC),Rn"},
		{0x4026, 'N', 4, false, 'n', false, 4, "lds.l @Rn+,PR"},
		{0x2002, 'M', 4, true, 'n', false, 0, "mov.l Rm,@Rn"}, {0x2001, 'M', 2, true, 'n', false, 0, "mov.w Rm,@Rn"},
		{0x2000, 'M', 1, true, 'n', false, 0, "mov.b Rm,@Rn"},
		{0x1000, 'D', 4, true, 'n', false, 0, "mov.l Rm,@(disp,Rn)"},
		{0x8100, 'd', 2, true, 'm', false, 0, "mov.w R0,@(disp,Rm)"}, {0x8000, 'd', 1, true, 'm', false, 0, "mov.b R0,@(disp,Rm)"},
		{0x0006, 'M', 4, true, 'n', true, 0, "mov.l Rm,@(R0,Rn)"}, {0x0005, 'M', 2, true, 'n', true, 0, "mov.w Rm,@(R0,Rn)"},
		{0x0004, 'M', 1, true, 'n', true, 0, "mov.b Rm,@(R0,Rn)"},
		{0x2006, 'M', 4, true, 'n', false, -4, "mov.l Rm,@-Rn"}, {0x2005, 'M', 2, true, 'n', false, -2, "mov.w Rm,@-Rn"},
		{0x2004, 'M', 1, true, 'n', false, -1, "mov.b Rm,@-Rn"},
		{0xC200, 'G', 4, true, 'G', false, 0, "mov.l R0,@(disp,GBR)"}, {0xC100, 'G', 2, true, 'G', false, 0, "mov.w R0,@(disp,GBR)"},
		{0xC000, 'G', 1, true, 'G', false, 0, "mov.b R0,@(disp,GBR)"},
		{0x4022, 'N', 4, true, 'n', false, -4, "sts.l PR,@-Rn"},
		{0x00C3, 'N', 4, true, 'n', false, 0, "movca.l R0,@Rn"},
	};
	static const uint8_t pairs[][2] = {{1, 2}, {2, 1}, {1, 1}, {0, 2}, {2, 0}, {0, 0}};
	static const uint8_t displacements[] = {0, 1, 15}, singles[] = {1, 0, 2}, gbr[] = {0, 1, 255};
	/* where: plain memory, not aligned, the device, nothing, plain memory
	 * through P2, over the code, over the code through P1 (a byte or a
	 * word that makes a NOP or MOVT R9, a long two NOP) */
	static const uint32_t targets[] = {DATA + 0x100, DATA + 0x101, DEVICE + 0x40, 0x08000000,
	                                   (DATA + 0x100) | 0xA0000000, CODE, CODE | 0x80000000};
	Pair p;

	if (!p.host.cpu.translates()) {
		std::printf("host code: none on this machine, not tested\n");
		return;
	}
	const Sh4State start = p.read.state();
	for (const Form &form : forms) {
		struct Code {
			uint16_t code;
			uint32_t n, m, disp;
		};
		std::vector<Code> codes;
		unsigned runs = 0;

		p.differ = 0;
		switch (form.operands) {
		case 'M':
			for (const auto &pair : pairs) {
				codes.push_back({static_cast<uint16_t>(form.code | (pair[0] << 8) | (pair[1] << 4)), pair[0], pair[1], 0});
			}
			break;
		case 'D':
			for (const auto &pair : pairs) {
				for (uint8_t d : displacements) {
					codes.push_back({static_cast<uint16_t>(form.code | (pair[0] << 8) | (pair[1] << 4) | d),
					                 pair[0], pair[1], d * form.size});
				}
			}
			break;
		case 'd':
			for (uint8_t m : singles) {
				for (uint8_t d : displacements) {
					codes.push_back({static_cast<uint16_t>(form.code | (m << 4) | d), 0, m, d * form.size});
				}
			}
			break;
		case 'N':
			for (uint8_t n : singles) {
				codes.push_back({static_cast<uint16_t>(form.code | (n << 8)), n, 0, 0});
			}
			break;
		case 'G':
			for (uint8_t d : gbr) {
				codes.push_back({static_cast<uint16_t>(form.code | d), 0, 0, d * form.size});
			}
			break;
		default:
			for (uint8_t n : singles) {
				codes.push_back({static_cast<uint16_t>(form.code | (n << 8)), n, 0, 0});
			}
			break;
		}
		for (const Code &c : codes) {
			/* A read relative to PC is not allowed in a delay slot. */
			const uint16_t slot = form.base == 'P' ? NOP : c.code;
			const uint32_t dest = form.operands == 'd' || form.operands == 'G' ? 0 : c.n;
			const auto keep = [dest](uint32_t to) { return static_cast<uint16_t>(0x6003 | (to << 8) | (dest << 4)); };
			const uint32_t base = form.base == 'n' ? c.n : form.base == 'm' ? c.m : 0;
			const uint32_t data = form.operands == 'M' || form.operands == 'D' ? (form.write ? c.m : c.n) : 0;

			for (uint32_t target : targets) {
				const bool code = (target & 0x1FFFFFFF) == (CODE & 0x1FFFFFFF);

				if (code) {
					target += form.size == 1 ? 3 : form.size == 2 ? 2 : 4;
				}
				uint32_t at = target - c.disp - (form.step < 0 ? static_cast<uint32_t>(form.step) : 0);

				if (form.base == 'P' && target != targets[0]) {
					continue;
				}
				if ((code && (!form.write || data == base || (form.index && data == 0))) ||
				    (target == DATA + 0x101 && form.size == 1)) {
					continue;
				}
				if (form.index) {
					if (base == 0) {
						if (at & 1) {
							continue;
						}
						at /= 2;
					} else {
						at -= 0x40;
					}
				}
				/* What a write stores: an address of plain memory of its own,
				 * for an access made at the wrong register, or over the code
				 * words that make valid instructions. */
				for (uint32_t value : {DATA + 0x1C0, 0x00090009u}) {
					if (code != (value == 0x00090009)) {
						continue;
					}
					/* the access / movt r8 / mov rn, r4 / bra / its slot / movt r9 / mov rn, r5 /
					 * sett / bt/s / its slot / movt r10 / mov rn, r6 / clrt / bt/s / the access /
					 * movt r11 / mov rn, r7 / bra itself; then words of every sign for PC */
					p.each([&](Machine &mc) {
						mc.code(CODE, {c.code, 0x0829, keep(4), 0xA000, slot, 0x0929, keep(5), 0x0018, 0x8D00,
						               slot, 0x0A29, keep(6), 0x0008, 0x8D00, c.code, 0x0B29, keep(7), 0xAFFE, NOP});
						for (uint32_t i = 0; i < 0x200; i++) {
							mc.ram[CODE - RAM + 0x40 + i] = static_cast<uint8_t>(0x70 + 3 * i);
							mc.ram[DATA - RAM + 0xF0 + i] = static_cast<uint8_t>(0x80 + 5 * i);
						}
						mc.device.clear();
						mc.unanswered_log.clear();
						mc.device_value = 0xC0C1C2C3;
					});
					Sh4State st = start;

					for (uint32_t i = 0; i < 16; i++) {
						st.r[i] = 0x5EED0000 + 0x1111 * i;
					}
					/* What a read is to replace: the same, from the wrong register. */
					st.r[data] = form.write ? value : DATA + 0x180;
					if (form.index) {
						st.r[0] = base == 0 ? at : 0x40;
					}
					if (form.base == 'G') {
						st.gbr = at;
					} else if (form.base != 'P') {
						st.r[base] = at;
					}
					st.pr = value;
					p.each([&st](Machine &mc) { mc.cpu.load(st); });
					p.slice(200);
					runs++;
				}
				if (p.differ) {
					break;
				}
			}
			if (p.differ) {
				break;
			}
		}
		check(runs > 0 && p.differ == 0, std::string(form.name) + ": the same read and in place");
	}
	{
		/* A loop that stores the same word over and over is run in
		 * full, not counted as a wait: mov.l r1, @r2 / bra / nop */
		Pair loop;

		loop.each([](Machine &mc) {
			mc.code(CODE, {0x2212, 0xAFFD, NOP});
			Sh4State st = mc.state();
			st.r[1] = 0x1234;
			st.r[2] = DATA + 0x10;
			mc.cpu.load(st);
		});
		loop.slices_of(20000, 500, 9);
		loop.verdict("a loop that only stores");
		eq("turns counted without being run", loop.host.cpu.skipped(), 0);
	}
	{
		/* A page read in place whose writes go to the bus, which drops
		 * them: mov.l r1, @r2 / mov.l @r2, r3 / bra / nop */
		Pair rom;

		rom.each([](Machine &mc) {
			mc.rom = DATA + 0x2000;
			mc.cpu.map_page((DATA + 0x2000) >> Sh4::PAGE_BITS & 0x1FFFF, &mc.ram[DATA + 0x2000 - RAM], nullptr);
			mc.put32(DATA + 0x2010, 0x0BADF00D);
			mc.code(CODE, {0x2212, 0x6322, 0xAFFC, NOP});
			Sh4State st = mc.state();
			st.r[1] = 0x1234;
			st.r[2] = DATA + 0x2010;
			mc.cpu.load(st);
		});
		rom.slices_of(2000, 300, 5);
		rom.verdict("a page written through the bus");
		eq("what the page holds", rom.host.get32(DATA + 0x2010), 0x0BADF00D);
		eq("what was read from it", rom.host.state().r[3], 0x0BADF00D);
	}
}

/* The moves of the FPU host code makes itself, for the FPSCR.SZ it sees in
 * the block: each of them, SZ clear and set when the block starts, turned
 * over by an FSCHG or not, at the four places of test_host_in_place(), at
 * plain memory, at an address aligned on 4 only, at the device: read and as
 * host code, the same registers, memory and accesses after. */
void test_host_fpu_moves()
{
	g_test = "host code, moves of the FPU";
	struct Form {
		uint16_t code;
		bool memory, index, write;
		int32_t step;	/* to the register of the address: +1 after, -1 before, times the size */
		const char *name;
	};
	static const Form forms[] = {
		{0xF008, true, false, false, 0, "fmov @Rm,FRn"}, {0xF00A, true, false, true, 0, "fmov FRm,@Rn"},
		{0xF006, true, true, false, 0, "fmov @(R0,Rm),FRn"}, {0xF007, true, true, true, 0, "fmov FRm,@(R0,Rn)"},
		{0xF009, true, false, false, 1, "fmov @Rm+,FRn"}, {0xF00B, true, false, true, -1, "fmov FRm,@-Rn"},
		{0xF00C, false, false, false, 0, "fmov FRm,FRn"},
	};
	static const uint8_t pairs[][2] = {{1, 2}, {2, 1}, {4, 5}, {3, 3}};
	static const uint32_t targets[] = {DATA + 0x100, DATA + 0x104, DEVICE + 0x40};
	{
		Pair probe;
		if (!probe.host.cpu.translates()) {
			std::printf("host code: none on this machine, not tested\n");
			return;
		}
	}
	for (const Form &form : forms) {
		unsigned runs = 0, differ = 0;

		/* A machine for each SZ: a block is written for the SZ it is first run with. */
		for (uint32_t sz = 0; sz < 2; sz++) {
			Pair p;
			const Sh4State start = p.read.state();

			for (const auto &pair : pairs) {
				const uint16_t code = static_cast<uint16_t>(form.code | (pair[0] << 8) | (pair[1] << 4));
				/* the register of the address */
				const uint32_t base = form.write ? pair[0] : pair[1];

				for (uint32_t turn = 0; turn < 2; turn++) {
					/* turned over or not at the start: fschg / nop; then as in test_host_in_place() */
					const uint16_t first = turn ? 0xF3FD : NOP;
					const uint32_t size = (sz != turn) ? 8 : 4;

					p.each([&](Machine &mc) {
						mc.code(CODE, {first, code, 0xA000, code, NOP, 0x0018, 0x8D00, code, NOP, 0x0008,
						               0x8D00, code, NOP, 0xAFFE, NOP});
						for (uint32_t i = 0; i < 0x200; i++) {
							mc.ram[DATA - RAM + 0xF0 + i] = static_cast<uint8_t>(0x80 + 5 * i);
						}
						mc.device.clear();
						mc.device_value = 0xC0C1C2C3;
					});
					for (uint32_t target : targets) {
						if (!form.memory && target != targets[0]) {
							continue;
						}
						Sh4State st = start;

						for (uint32_t i = 0; i < 16; i++) {
							st.r[i] = 0x5EED0000 + 0x1111 * i;
							st.fr[i] = 0x3F800000 + 0x10000 * i;
							st.xf[i] = 0x40000000 + 0x10000 * i;
						}
						st.fpscr = 0x00040001 | (sz ? 0x00100000 : 0);
						if (form.memory) {
							uint32_t at = target;

							if (form.step < 0) {
								at += 4 * size;
							}
							if (form.index) {
								st.r[0] = 0x40;
								at -= 0x40;
							}
							st.r[base] = at;
						}
						p.each([&st](Machine &mc) { mc.cpu.load(st); });
						p.slice(200);
						runs++;
					}
				}
			}
			differ += p.differ;
		}
		check(runs > 0 && differ == 0, std::string(form.name) + ": the same read and in place");
	}
}

/* The arithmetic of the FPU host code does itself in single precision:
 * each instruction at the four places of test_host_in_place(), for pairs of
 * registers, values at the edges of the floats (zeros, infinities, NaN,
 * denormals, the limits of the integers) and of FPUL, FPSCR.PR clear, and
 * set where the instruction allows it (the long way): read and as host
 * code, the same registers after. */
void test_host_float()
{
	g_test = "host code, floats";
	struct Form {
		uint16_t code;
		char operands;	/* M: FRn and FRm; N: FRn */
		bool doubles;	/* may run with PR set */
		const char *name;
	};
	static const Form forms[] = {
		{0xF00D, 'N', true, "fsts"}, {0xF01D, 'N', true, "flds"}, {0xF02D, 'N', true, "float"},
		{0xF03D, 'N', true, "ftrc"}, {0xF04D, 'N', true, "fneg"}, {0xF05D, 'N', true, "fabs"},
		{0xF08D, 'N', false, "fldi0"}, {0xF09D, 'N', false, "fldi1"}, {0xF000, 'M', true, "fadd"},
		{0xF001, 'M', true, "fsub"}, {0xF002, 'M', true, "fmul"}, {0xF005, 'M', true, "fcmp/gt"},
		{0xF00E, 'M', false, "fmac"},
	};
	static const uint8_t pairs[][2] = {{1, 2}, {2, 1}, {3, 3}, {0, 4}, {4, 0}, {2, 4}};
	static const uint32_t floats[] = {0x00000000, 0x80000000, 0x3F800000, 0xBF800000, 0x7F800000, 0xFF800000,
	                                  0x7FC00000, 0x7F800001, 0x00000001, 0x807FFFFF, 0x4F000000, 0xCF000000,
	                                  0x4EFFFFFF, 0x3EAAAAAB, 0x7F7FFFFF, 0x4B800001, 0x3FC00000, 0xBFD9999A};
	constexpr uint32_t COUNT = sizeof(floats) / sizeof(floats[0]);
	static const uint32_t integers[] = {0, 1, 0xFFFFFFFF, 0x7FFFFFFF, 0x80000000, 16777217};
	{
		Pair probe;
		if (!probe.host.cpu.translates()) {
			std::printf("host code: none on this machine, not tested\n");
			return;
		}
	}
	for (const Form &form : forms) {
		unsigned runs = 0, differ = 0;

		for (uint32_t pr = 0; pr < (form.doubles ? 2u : 1u); pr++) {
			Pair p;
			const Sh4State start = p.read.state();

			for (const auto &pair : pairs) {
				const uint32_t n = pair[0], m = form.operands == 'M' ? pair[1] : 0;
				const uint16_t code = static_cast<uint16_t>(form.code | (n << 8) | (m << 4));

				/* double precision wants even registers */
				if (pr && ((n | m) & 1)) {
					continue;
				}
				/* FRn moved to FR8-FR11 after each of the four */
				const auto keep = [n](uint32_t to) { return static_cast<uint16_t>(0xF00C | (to << 8) | (n << 4)); };

				p.each([&](Machine &mc) {
					mc.code(CODE, {code, keep(8), 0xA000, code, keep(9), 0x0018, 0x8D00, code, keep(10), 0x0008,
					               0x8D00, code, keep(11), 0xAFFE, NOP});
				});
				for (uint32_t a = 0; a < COUNT; a++) {
					for (uint32_t b = 0; b < COUNT; b++) {
						Sh4State st = start;

						for (uint32_t i = 0; i < 16; i++) {
							st.fr[i] = floats[(a + 3 * i + 5) % COUNT];
						}
						st.fr[n] = floats[a];
						st.fr[m] = form.operands == 'M' ? floats[b] : st.fr[m];
						st.fpul = (b < 6) ? integers[b] : floats[b];
						st.fpscr = 0x00040001 | (pr ? 0x00080000 : 0);
						p.each([&st](Machine &mc) { mc.cpu.load(st); });
						p.slice(200);
						runs++;
					}
				}
			}
			differ += p.differ;
		}
		check(runs > 0 && differ == 0, std::string(form.name) + ": the same read and in place");
	}
}

/* FIPR and FTRV, in place where the host has vectors: every vector of
 * FR and of the matrix in XF made of the values of test_host_float(),
 * not numbers among them */
void test_host_vectors()
{
	g_test = "host code, vectors";
	static const uint32_t floats[] = {0x00000000, 0x80000000, 0x3F800000, 0xBF800000, 0x7F800000, 0xFF800000,
	                                  0x7FC00000, 0x7F800001, 0x00000001, 0x807FFFFF, 0x4F000000, 0xCF000000,
	                                  0x4EFFFFFF, 0x3EAAAAAB, 0x7F7FFFFF, 0x4B800001, 0x3FC00000, 0xBFD9999A,
	                                  0x7FC00001, 0xFFC00000};
	constexpr uint32_t COUNT = sizeof(floats) / sizeof(floats[0]);
	{
		Pair probe;
		if (!probe.host.cpu.translates()) {
			std::printf("host code: none on this machine, not tested\n");
			return;
		}
	}
	for (const bool trv : {false, true}) {
		unsigned runs = 0, differ = 0;
		Pair p;
		const Sh4State start = p.read.state();

		for (uint32_t n = 0; n < 4; n++) {
			for (uint32_t m = 0; m < (trv ? 1u : 4u); m++) {
				const uint16_t code = static_cast<uint16_t>(trv ? 0xF1FD | (n << 10) : 0xF0ED | (n << 10) | (m << 8));

				p.each([&](Machine &mc) { mc.code(CODE, {code, code, 0xAFFE, NOP}); });
				for (uint32_t a = 0; a < 4 * COUNT; a++) {
					Sh4State st = start;

					for (uint32_t i = 0; i < 16; i++) {
						st.fr[i] = floats[(a + 3 * i + 5) % COUNT];
						st.xf[i] = floats[(5 * a + 7 * i + 1) % COUNT];
					}
					st.fpscr = 0x00040001;
					p.each([&st](Machine &mc) { mc.cpu.load(st); });
					p.slice(200);
					runs++;
				}
			}
		}
		differ += p.differ;
		check(runs > 0 && differ == 0, std::string(trv ? "ftrv" : "fipr") + ": the same read and in place");
	}
}

/* The branches host code makes itself: each of them in a loop whose turns
 * take it or not as a register turned bit by bit says, its delay slot an
 * instruction in place, an access to the memory or a call; then what the
 * branch leads to, a subroutine that returns where it is one. Read and as
 * host code, in slices of every length, the same after each. */
void test_host_branches()
{
	g_test = "host code, branches";
	struct Form {
		uint16_t code;
		bool delayed, subroutine;
		const char *name;
	};
	/* the branch is at CODE + 4 and leads to CODE + 16 */
	static const Form forms[] = {
		{0x8904, false, false, "bt"}, {0x8B04, false, false, "bf"}, {0x8D04, true, false, "bt/s"},
		{0x8F04, true, false, "bf/s"}, {0xA004, true, false, "bra"}, {0xB004, true, true, "bsr"},
		{0x000B, true, false, "rts"}, {0x490B, true, true, "jsr"}, {0x492B, true, false, "jmp"},
		{0x0A23, true, false, "braf"}, {0x0A03, true, true, "bsrf"},
	};
	/* add #3, r3 / mov.l r3, @r12 / mul.l r3, r4 */
	static const uint16_t slots[] = {0x7303, 0x2C32, 0x0437};
	Pair probe;

	if (!probe.host.cpu.translates()) {
		std::printf("host code: none on this machine, not tested\n");
		return;
	}
	for (const Form &form : forms) {
		unsigned differ = 0;

		for (uint32_t k = 0; k < 3; k++) {
			Pair p;

			/* add #1, r1 / rotr r7 / the branch / the slot / add #4, r4 / add #5, r5 / bra the start /
			 * nop; there: add #8, r6 / rts or bra the start / nop */
			p.each([&form, k](Machine &m) {
				m.code(CODE, {0x7101, 0x4705, form.code, slots[k], 0x7404, 0x7505, 0xAFF8, NOP,
				              0x7608, static_cast<uint16_t>(form.subroutine ? 0x000B : 0xAFF4), NOP});
				Sh4State st = m.state();
				st.r[7] = 0x5A3C96E1;
				st.r[9] = CODE + 16;
				st.r[10] = 8;
				st.r[12] = DATA;
				st.pr = CODE + 16;
				m.cpu.load(st);
			});
			p.slices_of(20000, k == 1 ? 6313 : 300, 11 + k);
			differ += p.differ;
			check(p.host.cpu.instructions() > 5000, std::string(form.name) + ": the loop went round");
		}
		check(differ == 0, std::string(form.name) + ": the same read and in place, with every kind of slot");
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
	test_host_code();
	test_host_in_place();
	test_host_memory();
	test_host_branches();
	test_host_fpu_moves();
	test_host_float();
	test_host_vectors();
	std::printf("%d checks, %d failures\n", g_checks, g_failures);
	return g_failures == 0 ? 0 : 1;
}
