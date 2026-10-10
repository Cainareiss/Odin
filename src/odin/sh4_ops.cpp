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

/* SH-4 instructions (SH-4 software manual, section 9) and their decoding.
 * Every instruction is one function that runs on a decoded operand, then
 * calls the next one of the block as its last act: with that call turned
 * into a jump by the compiler, a block runs without coming back to the
 * loop. Without it the calls only nest, to the length of a block at most.
 *
 * This file is built without contraction of floating-point expressions:
 * a product is fused with a sum only where std::fma says so. */

#include "odin/sh4.hpp"

#include <algorithm>
#include <bit>
#include <cmath>
#include <numbers>

namespace odin {

struct Sh4Ops {
	using Op = Sh4Op;

	static int32_t s8(uint32_t v) { return static_cast<int8_t>(v); }
	static int32_t s16(uint32_t v) { return static_cast<int16_t>(v); }
	static uint32_t u32(int32_t v) { return static_cast<uint32_t>(v); }
	static float f32(uint32_t v) { return std::bit_cast<float>(v); }
	static uint32_t bits(float v) { return std::bit_cast<uint32_t>(v); }

	/* On to the next instruction of the block. */
	static void go(Sh4 &c, const Op *op) { op[1].run(c, op + 1); }
	/* The same after a memory access, unless the block must be left. */
	static void next(Sh4 &c, const Op *op, bool plain)
	{
		if (plain) [[likely]] {
			return go(c, op);
		}
		c.stop_after(op);
	}

	[[noreturn]] static void fail(Sh4 &c, const Op *op, const char *what)
	{
		if (!(op->flags & Sh4::OP_SLOT)) {
			c.m_r.pc = c.pc_of(op);
		}
		c.fatal(what, c.m_r.pc);
	}
	static void need(Sh4 &c, const Op *op, bool ok, const char *what)
	{
		if (!ok) [[unlikely]] {
			fail(c, op, what);
		}
	}
	static void need_privileged(Sh4 &c, const Op *op)
	{
		need(c, op, (c.m_r.sr & Sh4::SR_MD) != 0, "privileged instruction in user mode");
	}
	static void need_fpu(Sh4 &c, const Op *op)
	{
		need(c, op, (c.m_r.sr & Sh4::SR_FD) == 0, "FPU instruction with the FPU disabled");
	}

	/* A double has its high word in FRn and its low word in FRn+1. */
	static double get_dr(const Sh4 &c, uint32_t n)
	{
		return std::bit_cast<double>((static_cast<uint64_t>(c.m_r.fr[n]) << 32) | c.m_r.fr[n + 1]);
	}
	static void set_dr(Sh4 &c, uint32_t n, double v)
	{
		uint64_t u = std::bit_cast<uint64_t>(v);
		c.m_r.fr[n] = static_cast<uint32_t>(u >> 32);
		c.m_r.fr[n + 1] = static_cast<uint32_t>(u);
	}
	/* The pair FMOV moves when FPSCR.SZ is set: DRn, or XDn for an odd n. */
	static uint32_t *pair(Sh4 &c, uint32_t n) { return (n & 1) ? &c.m_r.xf[n & 14] : &c.m_r.fr[n & 14]; }
	static uint64_t get_pair(Sh4 &c, uint32_t n)
	{
		const uint32_t *p = pair(c, n);
		return p[0] | (static_cast<uint64_t>(p[1]) << 32);
	}
	static void set_pair(Sh4 &c, uint32_t n, uint64_t v)
	{
		uint32_t *p = pair(c, n);
		p[0] = static_cast<uint32_t>(v);
		p[1] = static_cast<uint32_t>(v >> 32);
	}

	/* A branch with a delay slot. PC takes the target at once, as on the
	 * chip; then the slot runs, `m` further, and the end that follows it
	 * leaves the block. */
	static void jump(Sh4 &c, const Op *op, uint32_t target)
	{
		const Op *slot = op + op->m;

		need(c, op, !(op->flags & Sh4::OP_BAD_SLOT), "instruction not allowed in a delay slot");
		c.m_r.pc = target;
		return slot->run(c, slot);
	}
	static void branch(Sh4 &c, const Op *op)
	{
		c.m_r.pc = op->imm;
		c.m_remaining += c.m_blk->count - op->idx - 1;
		return c.chain(op->imm);
	}

	static void invalid(Sh4 &c, const Op *op) { fail(c, op, "invalid instruction"); }

	/* Data transfer */

	static void mov(Sh4 &c, const Op *op)
	{
		c.m_r.r[op->n] = c.m_r.r[op->m];
		return go(c, op);
	}
	static void movi(Sh4 &c, const Op *op)
	{
		c.m_r.r[op->n] = op->imm;
		return go(c, op);
	}
	static void movwi(Sh4 &c, const Op *op)
	{
		uint16_t v;
		bool plain = c.read(op, op->imm, v);
		c.m_r.r[op->n] = u32(s16(v));
		return next(c, op, plain);
	}
	static void movli(Sh4 &c, const Op *op)
	{
		uint32_t v;
		bool plain = c.read(op, op->imm, v);
		c.m_r.r[op->n] = v;
		return next(c, op, plain);
	}
	static void movbs(Sh4 &c, const Op *op)
	{
		return next(c, op, c.write(op, c.m_r.r[op->n], static_cast<uint8_t>(c.m_r.r[op->m])));
	}
	static void movws(Sh4 &c, const Op *op)
	{
		return next(c, op, c.write(op, c.m_r.r[op->n], static_cast<uint16_t>(c.m_r.r[op->m])));
	}
	static void movls(Sh4 &c, const Op *op)
	{
		return next(c, op, c.write(op, c.m_r.r[op->n], c.m_r.r[op->m]));
	}
	static void movbl(Sh4 &c, const Op *op)
	{
		uint8_t v;
		bool plain = c.read(op, c.m_r.r[op->m], v);
		c.m_r.r[op->n] = u32(s8(v));
		return next(c, op, plain);
	}
	static void movwl(Sh4 &c, const Op *op)
	{
		uint16_t v;
		bool plain = c.read(op, c.m_r.r[op->m], v);
		c.m_r.r[op->n] = u32(s16(v));
		return next(c, op, plain);
	}
	static void movll(Sh4 &c, const Op *op)
	{
		uint32_t v;
		bool plain = c.read(op, c.m_r.r[op->m], v);
		c.m_r.r[op->n] = v;
		return next(c, op, plain);
	}
	static void movbm(Sh4 &c, const Op *op)
	{
		bool plain = c.write(op, c.m_r.r[op->n] - 1, static_cast<uint8_t>(c.m_r.r[op->m]));
		c.m_r.r[op->n] -= 1;
		return next(c, op, plain);
	}
	static void movwm(Sh4 &c, const Op *op)
	{
		bool plain = c.write(op, c.m_r.r[op->n] - 2, static_cast<uint16_t>(c.m_r.r[op->m]));
		c.m_r.r[op->n] -= 2;
		return next(c, op, plain);
	}
	static void movlm(Sh4 &c, const Op *op)
	{
		bool plain = c.write(op, c.m_r.r[op->n] - 4, c.m_r.r[op->m]);
		c.m_r.r[op->n] -= 4;
		return next(c, op, plain);
	}
	static void movbp(Sh4 &c, const Op *op)
	{
		uint8_t v;
		bool plain = c.read(op, c.m_r.r[op->m], v);
		c.m_r.r[op->n] = u32(s8(v));
		if (op->n != op->m) {
			c.m_r.r[op->m] += 1;
		}
		return next(c, op, plain);
	}
	static void movwp(Sh4 &c, const Op *op)
	{
		uint16_t v;
		bool plain = c.read(op, c.m_r.r[op->m], v);
		c.m_r.r[op->n] = u32(s16(v));
		if (op->n != op->m) {
			c.m_r.r[op->m] += 2;
		}
		return next(c, op, plain);
	}
	static void movlp(Sh4 &c, const Op *op)
	{
		uint32_t v;
		bool plain = c.read(op, c.m_r.r[op->m], v);
		c.m_r.r[op->n] = v;
		if (op->n != op->m) {
			c.m_r.r[op->m] += 4;
		}
		return next(c, op, plain);
	}
	static void movbs0(Sh4 &c, const Op *op)
	{
		return next(c, op, c.write(op, c.m_r.r[op->n] + c.m_r.r[0], static_cast<uint8_t>(c.m_r.r[op->m])));
	}
	static void movws0(Sh4 &c, const Op *op)
	{
		return next(c, op, c.write(op, c.m_r.r[op->n] + c.m_r.r[0], static_cast<uint16_t>(c.m_r.r[op->m])));
	}
	static void movls0(Sh4 &c, const Op *op)
	{
		return next(c, op, c.write(op, c.m_r.r[op->n] + c.m_r.r[0], c.m_r.r[op->m]));
	}
	static void movbl0(Sh4 &c, const Op *op)
	{
		uint8_t v;
		bool plain = c.read(op, c.m_r.r[op->m] + c.m_r.r[0], v);
		c.m_r.r[op->n] = u32(s8(v));
		return next(c, op, plain);
	}
	static void movwl0(Sh4 &c, const Op *op)
	{
		uint16_t v;
		bool plain = c.read(op, c.m_r.r[op->m] + c.m_r.r[0], v);
		c.m_r.r[op->n] = u32(s16(v));
		return next(c, op, plain);
	}
	static void movll0(Sh4 &c, const Op *op)
	{
		uint32_t v;
		bool plain = c.read(op, c.m_r.r[op->m] + c.m_r.r[0], v);
		c.m_r.r[op->n] = v;
		return next(c, op, plain);
	}
	static void movblg(Sh4 &c, const Op *op)
	{
		uint8_t v;
		bool plain = c.read(op, c.m_r.gbr + op->imm, v);
		c.m_r.r[0] = u32(s8(v));
		return next(c, op, plain);
	}
	static void movwlg(Sh4 &c, const Op *op)
	{
		uint16_t v;
		bool plain = c.read(op, c.m_r.gbr + op->imm, v);
		c.m_r.r[0] = u32(s16(v));
		return next(c, op, plain);
	}
	static void movllg(Sh4 &c, const Op *op)
	{
		uint32_t v;
		bool plain = c.read(op, c.m_r.gbr + op->imm, v);
		c.m_r.r[0] = v;
		return next(c, op, plain);
	}
	static void movbsg(Sh4 &c, const Op *op)
	{
		return next(c, op, c.write(op, c.m_r.gbr + op->imm, static_cast<uint8_t>(c.m_r.r[0])));
	}
	static void movwsg(Sh4 &c, const Op *op)
	{
		return next(c, op, c.write(op, c.m_r.gbr + op->imm, static_cast<uint16_t>(c.m_r.r[0])));
	}
	static void movlsg(Sh4 &c, const Op *op)
	{
		return next(c, op, c.write(op, c.m_r.gbr + op->imm, c.m_r.r[0]));
	}
	/* @(disp,Rm) with R0 */
	static void movbl4(Sh4 &c, const Op *op)
	{
		uint8_t v;
		bool plain = c.read(op, c.m_r.r[op->m] + op->imm, v);
		c.m_r.r[0] = u32(s8(v));
		return next(c, op, plain);
	}
	static void movwl4(Sh4 &c, const Op *op)
	{
		uint16_t v;
		bool plain = c.read(op, c.m_r.r[op->m] + op->imm, v);
		c.m_r.r[0] = u32(s16(v));
		return next(c, op, plain);
	}
	static void movll4(Sh4 &c, const Op *op)
	{
		uint32_t v;
		bool plain = c.read(op, c.m_r.r[op->m] + op->imm, v);
		c.m_r.r[op->n] = v;
		return next(c, op, plain);
	}
	static void movbs4(Sh4 &c, const Op *op)
	{
		return next(c, op, c.write(op, c.m_r.r[op->m] + op->imm, static_cast<uint8_t>(c.m_r.r[0])));
	}
	static void movws4(Sh4 &c, const Op *op)
	{
		return next(c, op, c.write(op, c.m_r.r[op->m] + op->imm, static_cast<uint16_t>(c.m_r.r[0])));
	}
	static void movls4(Sh4 &c, const Op *op)
	{
		return next(c, op, c.write(op, c.m_r.r[op->n] + op->imm, c.m_r.r[op->m]));
	}
	static void mova(Sh4 &c, const Op *op)
	{
		c.m_r.r[0] = op->imm;
		return go(c, op);
	}
	static void movt(Sh4 &c, const Op *op)
	{
		c.m_r.r[op->n] = c.m_r.t;
		return go(c, op);
	}
	static void swapb(Sh4 &c, const Op *op)
	{
		uint32_t m = c.m_r.r[op->m];
		c.m_r.r[op->n] = (m & 0xFFFF0000) | ((m << 8) & 0xFF00) | ((m >> 8) & 0xFF);
		return go(c, op);
	}
	static void swapw(Sh4 &c, const Op *op)
	{
		uint32_t m = c.m_r.r[op->m];
		c.m_r.r[op->n] = (m >> 16) | (m << 16);
		return go(c, op);
	}
	static void xtrct(Sh4 &c, const Op *op)
	{
		c.m_r.r[op->n] = (c.m_r.r[op->m] << 16) | (c.m_r.r[op->n] >> 16);
		return go(c, op);
	}

	/* Arithmetic */

	static void add(Sh4 &c, const Op *op)
	{
		c.m_r.r[op->n] += c.m_r.r[op->m];
		return go(c, op);
	}
	static void addi(Sh4 &c, const Op *op)
	{
		c.m_r.r[op->n] += op->imm;
		return go(c, op);
	}
	static void addc(Sh4 &c, const Op *op)
	{
		uint32_t tmp0 = c.m_r.r[op->n];
		uint32_t tmp1 = tmp0 + c.m_r.r[op->m];
		uint32_t res = tmp1 + c.m_r.t;
		c.m_r.r[op->n] = res;
		c.m_r.t = (tmp0 > tmp1) || (tmp1 > res);
		return go(c, op);
	}
	static void addv(Sh4 &c, const Op *op)
	{
		bool d = static_cast<int32_t>(c.m_r.r[op->n]) < 0;
		bool s = static_cast<int32_t>(c.m_r.r[op->m]) < 0;
		c.m_r.r[op->n] += c.m_r.r[op->m];
		bool a = static_cast<int32_t>(c.m_r.r[op->n]) < 0;
		c.m_r.t = (d == s) && (d != a);
		return go(c, op);
	}
	static void neg(Sh4 &c, const Op *op)
	{
		c.m_r.r[op->n] = 0u - c.m_r.r[op->m];
		return go(c, op);
	}
	static void negc(Sh4 &c, const Op *op)
	{
		uint32_t tmp = 0u - c.m_r.r[op->m];
		uint32_t res = tmp - c.m_r.t;
		c.m_r.r[op->n] = res;
		c.m_r.t = (0 < tmp) || (tmp < res);
		return go(c, op);
	}
	static void sub(Sh4 &c, const Op *op)
	{
		c.m_r.r[op->n] -= c.m_r.r[op->m];
		return go(c, op);
	}
	static void subc(Sh4 &c, const Op *op)
	{
		uint32_t tmp0 = c.m_r.r[op->n];
		uint32_t tmp1 = tmp0 - c.m_r.r[op->m];
		uint32_t res = tmp1 - c.m_r.t;
		c.m_r.r[op->n] = res;
		c.m_r.t = (tmp0 < tmp1) || (tmp1 < res);
		return go(c, op);
	}
	static void subv(Sh4 &c, const Op *op)
	{
		bool d = static_cast<int32_t>(c.m_r.r[op->n]) < 0;
		bool s = static_cast<int32_t>(c.m_r.r[op->m]) < 0;
		c.m_r.r[op->n] -= c.m_r.r[op->m];
		bool a = static_cast<int32_t>(c.m_r.r[op->n]) < 0;
		c.m_r.t = (s != d) && (d != a);
		return go(c, op);
	}
	static void cmpeq(Sh4 &c, const Op *op)
	{
		c.m_r.t = c.m_r.r[op->n] == c.m_r.r[op->m];
		return go(c, op);
	}
	static void cmpge(Sh4 &c, const Op *op)
	{
		c.m_r.t = static_cast<int32_t>(c.m_r.r[op->n]) >= static_cast<int32_t>(c.m_r.r[op->m]);
		return go(c, op);
	}
	static void cmpgt(Sh4 &c, const Op *op)
	{
		c.m_r.t = static_cast<int32_t>(c.m_r.r[op->n]) > static_cast<int32_t>(c.m_r.r[op->m]);
		return go(c, op);
	}
	static void cmphi(Sh4 &c, const Op *op)
	{
		c.m_r.t = c.m_r.r[op->n] > c.m_r.r[op->m];
		return go(c, op);
	}
	static void cmphs(Sh4 &c, const Op *op)
	{
		c.m_r.t = c.m_r.r[op->n] >= c.m_r.r[op->m];
		return go(c, op);
	}
	static void cmppz(Sh4 &c, const Op *op)
	{
		c.m_r.t = static_cast<int32_t>(c.m_r.r[op->n]) >= 0;
		return go(c, op);
	}
	static void cmppl(Sh4 &c, const Op *op)
	{
		c.m_r.t = static_cast<int32_t>(c.m_r.r[op->n]) > 0;
		return go(c, op);
	}
	static void cmpim(Sh4 &c, const Op *op)
	{
		c.m_r.t = c.m_r.r[0] == op->imm;
		return go(c, op);
	}
	static void cmpstr(Sh4 &c, const Op *op)
	{
		uint32_t tmp = c.m_r.r[op->n] ^ c.m_r.r[op->m];
		c.m_r.t = !(tmp & 0xFF000000) || !(tmp & 0x00FF0000) || !(tmp & 0x0000FF00) || !(tmp & 0x000000FF);
		return go(c, op);
	}
	static void div0s(Sh4 &c, const Op *op)
	{
		uint32_t q = c.m_r.r[op->n] >> 31, m = c.m_r.r[op->m] >> 31;
		c.m_r.sr = (c.m_r.sr & ~(Sh4::SR_Q | Sh4::SR_M)) | (q ? Sh4::SR_Q : 0) | (m ? Sh4::SR_M : 0);
		c.m_r.t = q ^ m;
		return go(c, op);
	}
	static void div0u(Sh4 &c, const Op *op)
	{
		c.m_r.sr &= ~(Sh4::SR_Q | Sh4::SR_M);
		c.m_r.t = 0;
		return go(c, op);
	}
	/* One step of a division (software manual, DIV1). */
	static void div1(Sh4 &c, const Op *op)
	{
		uint32_t old_q = (c.m_r.sr & Sh4::SR_Q) ? 1 : 0;
		uint32_t m = (c.m_r.sr & Sh4::SR_M) ? 1 : 0;
		uint32_t q = c.m_r.r[op->n] >> 31;
		uint32_t rn = (c.m_r.r[op->n] << 1) | c.m_r.t;
		uint32_t tmp0 = rn, tmp2 = c.m_r.r[op->m], tmp1;

		if (old_q == m) {
			rn -= tmp2;
			tmp1 = rn > tmp0;
		} else {
			rn += tmp2;
			tmp1 = rn < tmp0;
		}
		/* The carry or borrow of the step, turned over by the bit shifted
		 * out and, when the divisor is negative, once more. */
		q = (q ^ tmp1 ^ m) & 1;
		c.m_r.r[op->n] = rn;
		c.m_r.sr = (c.m_r.sr & ~Sh4::SR_Q) | (q ? Sh4::SR_Q : 0);
		c.m_r.t = q == m;
		return go(c, op);
	}
	static void dmuls(Sh4 &c, const Op *op)
	{
		int64_t res = static_cast<int64_t>(static_cast<int32_t>(c.m_r.r[op->n]))
			* static_cast<int64_t>(static_cast<int32_t>(c.m_r.r[op->m]));
		c.m_r.macl = static_cast<uint32_t>(res);
		c.m_r.mach = static_cast<uint32_t>(static_cast<uint64_t>(res) >> 32);
		return go(c, op);
	}
	static void dmulu(Sh4 &c, const Op *op)
	{
		uint64_t res = static_cast<uint64_t>(c.m_r.r[op->n]) * c.m_r.r[op->m];
		c.m_r.macl = static_cast<uint32_t>(res);
		c.m_r.mach = static_cast<uint32_t>(res >> 32);
		return go(c, op);
	}
	static void dt(Sh4 &c, const Op *op)
	{
		c.m_r.t = --c.m_r.r[op->n] == 0;
		return go(c, op);
	}
	static void extsb(Sh4 &c, const Op *op)
	{
		c.m_r.r[op->n] = u32(s8(c.m_r.r[op->m]));
		return go(c, op);
	}
	static void extsw(Sh4 &c, const Op *op)
	{
		c.m_r.r[op->n] = u32(s16(c.m_r.r[op->m]));
		return go(c, op);
	}
	static void extub(Sh4 &c, const Op *op)
	{
		c.m_r.r[op->n] = c.m_r.r[op->m] & 0xFF;
		return go(c, op);
	}
	static void extuw(Sh4 &c, const Op *op)
	{
		c.m_r.r[op->n] = c.m_r.r[op->m] & 0xFFFF;
		return go(c, op);
	}
	/* MAC.L with S clear: MAC += (Rn) * (Rm), signed, 64 bits. */
	static void macl(Sh4 &c, const Op *op)
	{
		uint32_t vn, vm;
		bool plain = c.read(op, c.m_r.r[op->n], vn);
		c.m_r.r[op->n] += 4;
		plain &= c.read(op, c.m_r.r[op->m], vm);
		c.m_r.r[op->m] += 4;
		need(c, op, !(c.m_r.sr & Sh4::SR_S), "MAC.L with S set is not implemented");
		int64_t prod = static_cast<int64_t>(static_cast<int32_t>(vn)) * static_cast<int64_t>(static_cast<int32_t>(vm));
		uint64_t mac = (static_cast<uint64_t>(c.m_r.mach) << 32) | c.m_r.macl;
		mac += static_cast<uint64_t>(prod);
		c.m_r.macl = static_cast<uint32_t>(mac);
		c.m_r.mach = static_cast<uint32_t>(mac >> 32);
		return next(c, op, plain);
	}
	static void macw(Sh4 &c, const Op *op)
	{
		uint16_t vn, vm;
		bool plain = c.read(op, c.m_r.r[op->n], vn);
		c.m_r.r[op->n] += 2;
		plain &= c.read(op, c.m_r.r[op->m], vm);
		c.m_r.r[op->m] += 2;

		uint32_t old = c.m_r.macl;
		int32_t prod = s16(vn) * s16(vm);
		uint32_t dst = static_cast<int32_t>(old) < 0;
		uint32_t src = (prod < 0 ? 1u : 0u) + dst;
		c.m_r.macl = old + u32(prod);
		uint32_t ans = (static_cast<int32_t>(c.m_r.macl) < 0 ? 1u : 0u) + dst;
		if (c.m_r.sr & Sh4::SR_S) {
			if (ans == 1) {
				if (src == 0) {
					c.m_r.macl = 0x7FFFFFFF;
				}
				if (src == 2) {
					c.m_r.macl = 0x80000000;
				}
			}
		} else {
			c.m_r.mach += prod < 0 ? 0xFFFFFFFFu : 0u;
			if (old > c.m_r.macl) {
				c.m_r.mach += 1;
			}
		}
		return next(c, op, plain);
	}
	static void mull(Sh4 &c, const Op *op)
	{
		c.m_r.macl = c.m_r.r[op->n] * c.m_r.r[op->m];
		return go(c, op);
	}
	static void mulsw(Sh4 &c, const Op *op)
	{
		c.m_r.macl = u32(s16(c.m_r.r[op->n]) * s16(c.m_r.r[op->m]));
		return go(c, op);
	}
	static void muluw(Sh4 &c, const Op *op)
	{
		c.m_r.macl = (c.m_r.r[op->n] & 0xFFFF) * (c.m_r.r[op->m] & 0xFFFF);
		return go(c, op);
	}

	/* Logic */

	static void and_(Sh4 &c, const Op *op)
	{
		c.m_r.r[op->n] &= c.m_r.r[op->m];
		return go(c, op);
	}
	static void andi(Sh4 &c, const Op *op)
	{
		c.m_r.r[0] &= op->imm;
		return go(c, op);
	}
	static void andm(Sh4 &c, const Op *op)
	{
		uint8_t v;
		bool plain = c.read(op, c.m_r.gbr + c.m_r.r[0], v);
		plain &= c.write(op, c.m_r.gbr + c.m_r.r[0], static_cast<uint8_t>(v & op->imm));
		return next(c, op, plain);
	}
	static void not_(Sh4 &c, const Op *op)
	{
		c.m_r.r[op->n] = ~c.m_r.r[op->m];
		return go(c, op);
	}
	static void or_(Sh4 &c, const Op *op)
	{
		c.m_r.r[op->n] |= c.m_r.r[op->m];
		return go(c, op);
	}
	static void ori(Sh4 &c, const Op *op)
	{
		c.m_r.r[0] |= op->imm;
		return go(c, op);
	}
	static void orm(Sh4 &c, const Op *op)
	{
		uint8_t v;
		bool plain = c.read(op, c.m_r.gbr + c.m_r.r[0], v);
		plain &= c.write(op, c.m_r.gbr + c.m_r.r[0], static_cast<uint8_t>(v | op->imm));
		return next(c, op, plain);
	}
	static void tas(Sh4 &c, const Op *op)
	{
		uint8_t v;
		bool plain = c.read(op, c.m_r.r[op->n], v);
		c.m_r.t = v == 0;
		plain &= c.write(op, c.m_r.r[op->n], static_cast<uint8_t>(v | 0x80));
		return next(c, op, plain);
	}
	static void tst(Sh4 &c, const Op *op)
	{
		c.m_r.t = (c.m_r.r[op->n] & c.m_r.r[op->m]) == 0;
		return go(c, op);
	}
	static void tsti(Sh4 &c, const Op *op)
	{
		c.m_r.t = (c.m_r.r[0] & op->imm) == 0;
		return go(c, op);
	}
	static void tstm(Sh4 &c, const Op *op)
	{
		uint8_t v;
		bool plain = c.read(op, c.m_r.gbr + c.m_r.r[0], v);
		c.m_r.t = (v & op->imm) == 0;
		return next(c, op, plain);
	}
	static void xor_(Sh4 &c, const Op *op)
	{
		c.m_r.r[op->n] ^= c.m_r.r[op->m];
		return go(c, op);
	}
	static void xori(Sh4 &c, const Op *op)
	{
		c.m_r.r[0] ^= op->imm;
		return go(c, op);
	}
	static void xorm(Sh4 &c, const Op *op)
	{
		uint8_t v;
		bool plain = c.read(op, c.m_r.gbr + c.m_r.r[0], v);
		plain &= c.write(op, c.m_r.gbr + c.m_r.r[0], static_cast<uint8_t>(v ^ op->imm));
		return next(c, op, plain);
	}

	/* Shifts */

	static void rotl(Sh4 &c, const Op *op)
	{
		uint32_t v = c.m_r.r[op->n];
		c.m_r.t = v >> 31;
		c.m_r.r[op->n] = (v << 1) | c.m_r.t;
		return go(c, op);
	}
	static void rotr(Sh4 &c, const Op *op)
	{
		uint32_t v = c.m_r.r[op->n];
		c.m_r.t = v & 1;
		c.m_r.r[op->n] = (v >> 1) | (c.m_r.t << 31);
		return go(c, op);
	}
	static void rotcl(Sh4 &c, const Op *op)
	{
		uint32_t v = c.m_r.r[op->n];
		c.m_r.r[op->n] = (v << 1) | c.m_r.t;
		c.m_r.t = v >> 31;
		return go(c, op);
	}
	static void rotcr(Sh4 &c, const Op *op)
	{
		uint32_t v = c.m_r.r[op->n];
		c.m_r.r[op->n] = (v >> 1) | (c.m_r.t << 31);
		c.m_r.t = v & 1;
		return go(c, op);
	}
	static void shll(Sh4 &c, const Op *op)
	{
		c.m_r.t = c.m_r.r[op->n] >> 31;
		c.m_r.r[op->n] <<= 1;
		return go(c, op);
	}
	static void shar(Sh4 &c, const Op *op)
	{
		c.m_r.t = c.m_r.r[op->n] & 1;
		c.m_r.r[op->n] = u32(static_cast<int32_t>(c.m_r.r[op->n]) >> 1);
		return go(c, op);
	}
	static void shlr(Sh4 &c, const Op *op)
	{
		c.m_r.t = c.m_r.r[op->n] & 1;
		c.m_r.r[op->n] >>= 1;
		return go(c, op);
	}
	static void shll2(Sh4 &c, const Op *op)
	{
		c.m_r.r[op->n] <<= 2;
		return go(c, op);
	}
	static void shll8(Sh4 &c, const Op *op)
	{
		c.m_r.r[op->n] <<= 8;
		return go(c, op);
	}
	static void shll16(Sh4 &c, const Op *op)
	{
		c.m_r.r[op->n] <<= 16;
		return go(c, op);
	}
	static void shlr2(Sh4 &c, const Op *op)
	{
		c.m_r.r[op->n] >>= 2;
		return go(c, op);
	}
	static void shlr8(Sh4 &c, const Op *op)
	{
		c.m_r.r[op->n] >>= 8;
		return go(c, op);
	}
	static void shlr16(Sh4 &c, const Op *op)
	{
		c.m_r.r[op->n] >>= 16;
		return go(c, op);
	}
	static void shad(Sh4 &c, const Op *op)
	{
		uint32_t m = c.m_r.r[op->m], n = c.m_r.r[op->n];
		if (!(m >> 31)) {
			n <<= m & 31;
		} else if ((m & 31) == 0) {
			n = (n >> 31) ? 0xFFFFFFFF : 0;
		} else {
			n = u32(static_cast<int32_t>(n) >> ((~m & 31) + 1));
		}
		c.m_r.r[op->n] = n;
		return go(c, op);
	}
	static void shld(Sh4 &c, const Op *op)
	{
		uint32_t m = c.m_r.r[op->m], n = c.m_r.r[op->n];
		if (!(m >> 31)) {
			n <<= m & 31;
		} else if ((m & 31) == 0) {
			n = 0;
		} else {
			n >>= (~m & 31) + 1;
		}
		c.m_r.r[op->n] = n;
		return go(c, op);
	}

	/* Branches */

	static void bt(Sh4 &c, const Op *op) { return c.m_r.t ? branch(c, op) : go(c, op); }
	static void bf(Sh4 &c, const Op *op) { return c.m_r.t ? go(c, op) : branch(c, op); }
	/* BF back to the DT Rn just before it, a delay loop: the turns left
	 * are counted without being run, up to the last of the count or of
	 * the slice. */
	static void bf_dt(Sh4 &c, const Op *op)
	{
		if (c.m_r.t) {
			return go(c, op);
		}
		uint32_t left = static_cast<uint32_t>(c.m_remaining) + c.m_blk->count - op->idx;
		uint32_t turns = std::min(c.m_r.r[op->n] - 1, (left - 1) / 2);
		c.m_r.r[op->n] -= turns;
		c.m_remaining -= static_cast<int32_t>(turns * 2);
		c.m_skipped += turns * 2;
		return branch(c, op);
	}
	static void bts(Sh4 &c, const Op *op) { return c.m_r.t ? jump(c, op, op->imm) : go(c, op); }
	static void bfs(Sh4 &c, const Op *op) { return c.m_r.t ? go(c, op) : jump(c, op, op->imm); }
	static void bra(Sh4 &c, const Op *op) { return jump(c, op, op->imm); }
	static void braf(Sh4 &c, const Op *op) { return jump(c, op, c.pc_of(op) + c.m_r.r[op->n] + 4); }
	static void bsr(Sh4 &c, const Op *op)
	{
		c.m_r.pr = c.pc_of(op) + 4;
		return jump(c, op, op->imm);
	}
	static void bsrf(Sh4 &c, const Op *op)
	{
		uint32_t pc = c.pc_of(op);
		c.m_r.pr = pc + 4;
		return jump(c, op, pc + c.m_r.r[op->n] + 4);
	}
	static void jmp(Sh4 &c, const Op *op)
	{
		need(c, op, !(c.m_r.r[op->n] & 1), "JMP to an odd address");
		return jump(c, op, c.m_r.r[op->n]);
	}
	static void jsr(Sh4 &c, const Op *op)
	{
		uint32_t target = c.m_r.r[op->n];
		c.m_r.pr = c.pc_of(op) + 4;
		return jump(c, op, target);
	}
	static void rts(Sh4 &c, const Op *op) { return jump(c, op, c.m_r.pr); }
	static void rte(Sh4 &c, const Op *op)
	{
		uint32_t target = c.m_r.spc;
		need(c, op, !(op->flags & Sh4::OP_BAD_SLOT), "instruction not allowed in a delay slot");
		/* The delay slot already runs with the restored SR. */
		c.set_sr(c.m_r.ssr);
		return jump(c, op, target);
	}

	/* System control */

	static void clrt(Sh4 &c, const Op *op)
	{
		c.m_r.t = 0;
		return go(c, op);
	}
	static void sett(Sh4 &c, const Op *op)
	{
		c.m_r.t = 1;
		return go(c, op);
	}
	static void clrs(Sh4 &c, const Op *op)
	{
		c.m_r.sr &= ~Sh4::SR_S;
		return go(c, op);
	}
	static void sets(Sh4 &c, const Op *op)
	{
		c.m_r.sr |= Sh4::SR_S;
		return go(c, op);
	}
	static void clrmac(Sh4 &c, const Op *op)
	{
		c.m_r.mach = 0;
		c.m_r.macl = 0;
		return go(c, op);
	}
	static void nop(Sh4 &c, const Op *op) { return go(c, op); }
	static void ldcsr(Sh4 &c, const Op *op)
	{
		c.set_sr(c.m_r.r[op->n]);
		return c.stop_after(op);
	}
	static void ldcgbr(Sh4 &c, const Op *op)
	{
		c.m_r.gbr = c.m_r.r[op->n];
		return go(c, op);
	}
	static void ldcvbr(Sh4 &c, const Op *op)
	{
		c.m_r.vbr = c.m_r.r[op->n];
		return go(c, op);
	}
	static void ldcssr(Sh4 &c, const Op *op)
	{
		need_privileged(c, op);
		c.m_r.ssr = c.m_r.r[op->n];
		return go(c, op);
	}
	static void ldcspc(Sh4 &c, const Op *op)
	{
		need_privileged(c, op);
		c.m_r.spc = c.m_r.r[op->n];
		return go(c, op);
	}
	static void ldcdbr(Sh4 &c, const Op *op)
	{
		need_privileged(c, op);
		c.m_r.dbr = c.m_r.r[op->n];
		return go(c, op);
	}
	static void ldcrbank(Sh4 &c, const Op *op)
	{
		need_privileged(c, op);
		c.m_r.rbank[op->m] = c.m_r.r[op->n];
		return go(c, op);
	}
	/* The bank of R0-R7 may change under the register that is then
	 * incremented, as SR is loaded first. */
	static void ldcmsr(Sh4 &c, const Op *op)
	{
		uint32_t v;
		c.read(op, c.m_r.r[op->n], v);
		c.set_sr(v);
		c.m_r.r[op->n] += 4;
		return c.stop_after(op);
	}
	static void ldcmgbr(Sh4 &c, const Op *op)
	{
		bool plain = c.read(op, c.m_r.r[op->n], c.m_r.gbr);
		c.m_r.r[op->n] += 4;
		return next(c, op, plain);
	}
	static void ldcmvbr(Sh4 &c, const Op *op)
	{
		bool plain = c.read(op, c.m_r.r[op->n], c.m_r.vbr);
		c.m_r.r[op->n] += 4;
		return next(c, op, plain);
	}
	static void ldcmssr(Sh4 &c, const Op *op)
	{
		need_privileged(c, op);
		bool plain = c.read(op, c.m_r.r[op->n], c.m_r.ssr);
		c.m_r.r[op->n] += 4;
		return next(c, op, plain);
	}
	static void ldcmspc(Sh4 &c, const Op *op)
	{
		need_privileged(c, op);
		bool plain = c.read(op, c.m_r.r[op->n], c.m_r.spc);
		c.m_r.r[op->n] += 4;
		return next(c, op, plain);
	}
	static void ldcmdbr(Sh4 &c, const Op *op)
	{
		need_privileged(c, op);
		bool plain = c.read(op, c.m_r.r[op->n], c.m_r.dbr);
		c.m_r.r[op->n] += 4;
		return next(c, op, plain);
	}
	static void ldcmrbank(Sh4 &c, const Op *op)
	{
		need_privileged(c, op);
		bool plain = c.read(op, c.m_r.r[op->n], c.m_r.rbank[op->m]);
		c.m_r.r[op->n] += 4;
		return next(c, op, plain);
	}
	static void ldsmach(Sh4 &c, const Op *op)
	{
		c.m_r.mach = c.m_r.r[op->n];
		return go(c, op);
	}
	static void ldsmacl(Sh4 &c, const Op *op)
	{
		c.m_r.macl = c.m_r.r[op->n];
		return go(c, op);
	}
	static void ldspr(Sh4 &c, const Op *op)
	{
		c.m_r.pr = c.m_r.r[op->n];
		return go(c, op);
	}
	static void ldsmmach(Sh4 &c, const Op *op)
	{
		bool plain = c.read(op, c.m_r.r[op->n], c.m_r.mach);
		c.m_r.r[op->n] += 4;
		return next(c, op, plain);
	}
	static void ldsmmacl(Sh4 &c, const Op *op)
	{
		bool plain = c.read(op, c.m_r.r[op->n], c.m_r.macl);
		c.m_r.r[op->n] += 4;
		return next(c, op, plain);
	}
	static void ldsmpr(Sh4 &c, const Op *op)
	{
		bool plain = c.read(op, c.m_r.r[op->n], c.m_r.pr);
		c.m_r.r[op->n] += 4;
		return next(c, op, plain);
	}
	static void sleep(Sh4 &c, const Op *op)
	{
		c.set_run(Sh4Run::Sleeping);
		return c.stop_after(op);
	}
	static void stcsr(Sh4 &c, const Op *op)
	{
		c.m_r.r[op->n] = c.m_r.sr | c.m_r.t;
		return go(c, op);
	}
	static void stcgbr(Sh4 &c, const Op *op)
	{
		c.m_r.r[op->n] = c.m_r.gbr;
		return go(c, op);
	}
	static void stcvbr(Sh4 &c, const Op *op)
	{
		c.m_r.r[op->n] = c.m_r.vbr;
		return go(c, op);
	}
	static void stcssr(Sh4 &c, const Op *op)
	{
		need_privileged(c, op);
		c.m_r.r[op->n] = c.m_r.ssr;
		return go(c, op);
	}
	static void stcspc(Sh4 &c, const Op *op)
	{
		need_privileged(c, op);
		c.m_r.r[op->n] = c.m_r.spc;
		return go(c, op);
	}
	static void stcsgr(Sh4 &c, const Op *op)
	{
		need_privileged(c, op);
		c.m_r.r[op->n] = c.m_r.sgr;
		return go(c, op);
	}
	static void stcdbr(Sh4 &c, const Op *op)
	{
		need_privileged(c, op);
		c.m_r.r[op->n] = c.m_r.dbr;
		return go(c, op);
	}
	static void stcrbank(Sh4 &c, const Op *op)
	{
		need_privileged(c, op);
		c.m_r.r[op->n] = c.m_r.rbank[op->m];
		return go(c, op);
	}
	/* Rn - 4 -> Rn, then the value to (Rn) */
	static void push(Sh4 &c, const Op *op, uint32_t value)
	{
		c.m_r.r[op->n] -= 4;
		return next(c, op, c.write(op, c.m_r.r[op->n], value));
	}
	static void stcmsr(Sh4 &c, const Op *op) { return push(c, op, c.m_r.sr | c.m_r.t); }
	static void stcmgbr(Sh4 &c, const Op *op) { return push(c, op, c.m_r.gbr); }
	static void stcmvbr(Sh4 &c, const Op *op) { return push(c, op, c.m_r.vbr); }
	static void stcmssr(Sh4 &c, const Op *op)
	{
		need_privileged(c, op);
		return push(c, op, c.m_r.ssr);
	}
	static void stcmspc(Sh4 &c, const Op *op)
	{
		need_privileged(c, op);
		return push(c, op, c.m_r.spc);
	}
	static void stcmsgr(Sh4 &c, const Op *op)
	{
		need_privileged(c, op);
		return push(c, op, c.m_r.sgr);
	}
	static void stcmdbr(Sh4 &c, const Op *op)
	{
		need_privileged(c, op);
		return push(c, op, c.m_r.dbr);
	}
	static void stcmrbank(Sh4 &c, const Op *op)
	{
		need_privileged(c, op);
		return push(c, op, c.m_r.rbank[op->m]);
	}
	static void stsmach(Sh4 &c, const Op *op)
	{
		c.m_r.r[op->n] = c.m_r.mach;
		return go(c, op);
	}
	static void stsmacl(Sh4 &c, const Op *op)
	{
		c.m_r.r[op->n] = c.m_r.macl;
		return go(c, op);
	}
	static void stspr(Sh4 &c, const Op *op)
	{
		c.m_r.r[op->n] = c.m_r.pr;
		return go(c, op);
	}
	static void stsmmach(Sh4 &c, const Op *op) { return push(c, op, c.m_r.mach); }
	static void stsmmacl(Sh4 &c, const Op *op) { return push(c, op, c.m_r.macl); }
	static void stsmpr(Sh4 &c, const Op *op) { return push(c, op, c.m_r.pr); }
	static void trapa(Sh4 &c, const Op *op) { fail(c, op, "TRAPA is not implemented"); }

	/* Cache and store queues: there is no cache, and a store queue write
	 * goes to its target at once, so PREF has nothing left to send. */
	static void movca(Sh4 &c, const Op *op) { return next(c, op, c.write(op, c.m_r.r[op->n], c.m_r.r[0])); }

	/* FPU registers */

	static void ldsfpscr(Sh4 &c, const Op *op)
	{
		need_fpu(c, op);
		c.set_fpscr(c.m_r.r[op->n]);
		return go(c, op);
	}
	static void ldsmfpscr(Sh4 &c, const Op *op)
	{
		uint32_t v;
		need_fpu(c, op);
		bool plain = c.read(op, c.m_r.r[op->n], v);
		c.set_fpscr(v);
		c.m_r.r[op->n] += 4;
		return next(c, op, plain);
	}
	static void stsfpscr(Sh4 &c, const Op *op)
	{
		need_fpu(c, op);
		c.m_r.r[op->n] = c.m_r.fpscr;
		return go(c, op);
	}
	static void stsmfpscr(Sh4 &c, const Op *op)
	{
		need_fpu(c, op);
		return push(c, op, c.m_r.fpscr);
	}
	static void ldsfpul(Sh4 &c, const Op *op)
	{
		need_fpu(c, op);
		c.m_r.fpul = c.m_r.r[op->n];
		return go(c, op);
	}
	static void ldsmfpul(Sh4 &c, const Op *op)
	{
		need_fpu(c, op);
		bool plain = c.read(op, c.m_r.r[op->n], c.m_r.fpul);
		c.m_r.r[op->n] += 4;
		return next(c, op, plain);
	}
	static void stsfpul(Sh4 &c, const Op *op)
	{
		need_fpu(c, op);
		c.m_r.r[op->n] = c.m_r.fpul;
		return go(c, op);
	}
	static void stsmfpul(Sh4 &c, const Op *op)
	{
		need_fpu(c, op);
		return push(c, op, c.m_r.fpul);
	}
	static void fschg(Sh4 &c, const Op *op)
	{
		c.set_fpscr(c.m_r.fpscr ^ Sh4::FPSCR_SZ);
		return go(c, op);
	}
	static void frchg(Sh4 &c, const Op *op)
	{
		c.set_fpscr(c.m_r.fpscr ^ Sh4::FPSCR_FR);
		return go(c, op);
	}
	static void fldi0(Sh4 &c, const Op *op)
	{
		need(c, op, !(c.m_r.fpscr & Sh4::FPSCR_PR), "FLDI0 in double precision");
		c.m_r.fr[op->n] = 0x00000000;
		return go(c, op);
	}
	static void fldi1(Sh4 &c, const Op *op)
	{
		need(c, op, !(c.m_r.fpscr & Sh4::FPSCR_PR), "FLDI1 in double precision");
		c.m_r.fr[op->n] = 0x3F800000;
		return go(c, op);
	}
	static void fsts(Sh4 &c, const Op *op)
	{
		c.m_r.fr[op->n] = c.m_r.fpul;
		return go(c, op);
	}
	static void flds(Sh4 &c, const Op *op)
	{
		c.m_r.fpul = c.m_r.fr[op->n];
		return go(c, op);
	}

	/* FMOV: one register, or a pair when FPSCR.SZ is set */

	static void fmov(Sh4 &c, const Op *op)
	{
		if (c.m_r.fpscr & Sh4::FPSCR_SZ) {
			set_pair(c, op->n, get_pair(c, op->m));
		} else {
			c.m_r.fr[op->n] = c.m_r.fr[op->m];
		}
		return go(c, op);
	}
	static void fmov_from(Sh4 &c, const Op *op, uint32_t addr)
	{
		bool plain;
		if (c.m_r.fpscr & Sh4::FPSCR_SZ) {
			uint64_t v;
			plain = c.read(op, addr, v);
			set_pair(c, op->n, v);
		} else {
			plain = c.read(op, addr, c.m_r.fr[op->n]);
		}
		return next(c, op, plain);
	}
	static void fmov_to(Sh4 &c, const Op *op, uint32_t addr)
	{
		bool plain;
		if (c.m_r.fpscr & Sh4::FPSCR_SZ) {
			plain = c.write(op, addr, get_pair(c, op->m));
		} else {
			plain = c.write(op, addr, c.m_r.fr[op->m]);
		}
		return next(c, op, plain);
	}
	static void fmov_load(Sh4 &c, const Op *op) { return fmov_from(c, op, c.m_r.r[op->m]); }
	static void fmov_store(Sh4 &c, const Op *op) { return fmov_to(c, op, c.m_r.r[op->n]); }
	static void fmov_index_load(Sh4 &c, const Op *op) { return fmov_from(c, op, c.m_r.r[0] + c.m_r.r[op->m]); }
	static void fmov_index_store(Sh4 &c, const Op *op) { return fmov_to(c, op, c.m_r.r[0] + c.m_r.r[op->n]); }
	static void fmov_restore(Sh4 &c, const Op *op)
	{
		uint32_t addr = c.m_r.r[op->m];
		c.m_r.r[op->m] += (c.m_r.fpscr & Sh4::FPSCR_SZ) ? 8 : 4;
		return fmov_from(c, op, addr);
	}
	static void fmov_save(Sh4 &c, const Op *op)
	{
		c.m_r.r[op->n] -= (c.m_r.fpscr & Sh4::FPSCR_SZ) ? 8 : 4;
		return fmov_to(c, op, c.m_r.r[op->n]);
	}

	/* FPU arithmetic. HEURISTIC: computed by the host and rounded to the
	 * nearest whatever FPSCR.RM and FPSCR.DN say; a NaN comes out as the
	 * host makes it; no FPU exception is raised. */

	static void flt(Sh4 &c, const Op *op)
	{
		int32_t v = static_cast<int32_t>(c.m_r.fpul);
		if (!(c.m_r.fpscr & Sh4::FPSCR_PR)) {
			c.m_r.fr[op->n] = bits(static_cast<float>(v));
		} else {
			need(c, op, !(op->n & 1), "double precision with an odd register");
			set_dr(c, op->n, static_cast<double>(v));
		}
		return go(c, op);
	}
	/* HEURISTIC: what the host gives out of range, 0x80000000; the SH-4
	 * saturates. */
	static void ftrc(Sh4 &c, const Op *op)
	{
		if (!(c.m_r.fpscr & Sh4::FPSCR_PR)) {
			float v = f32(c.m_r.fr[op->n]);
			c.m_r.fpul = (v >= -2147483648.0f && v < 2147483648.0f) ? u32(static_cast<int32_t>(v)) : 0x80000000;
		} else {
			need(c, op, !(op->n & 1), "double precision with an odd register");
			double v = get_dr(c, op->n);
			c.m_r.fpul = (v > -2147483649.0 && v < 2147483648.0) ? u32(static_cast<int32_t>(v)) : 0x80000000;
		}
		return go(c, op);
	}
	static void fcnvsd(Sh4 &c, const Op *op)
	{
		need(c, op, !(op->n & 1), "double precision with an odd register");
		set_dr(c, op->n, static_cast<double>(f32(c.m_r.fpul)));
		return go(c, op);
	}
	static void fcnvds(Sh4 &c, const Op *op)
	{
		need(c, op, !(op->n & 1), "double precision with an odd register");
		c.m_r.fpul = bits(static_cast<float>(get_dr(c, op->n)));
		return go(c, op);
	}
	/* The sign of a double is in FRn. */
	static void fneg(Sh4 &c, const Op *op)
	{
		need(c, op, !(c.m_r.fpscr & Sh4::FPSCR_PR) || !(op->n & 1), "double precision with an odd register");
		c.m_r.fr[op->n] ^= 0x80000000;
		return go(c, op);
	}
	static void fabs_(Sh4 &c, const Op *op)
	{
		need(c, op, !(c.m_r.fpscr & Sh4::FPSCR_PR) || !(op->n & 1), "double precision with an odd register");
		c.m_r.fr[op->n] &= 0x7FFFFFFF;
		return go(c, op);
	}
	static void need_even(Sh4 &c, const Op *op)
	{
		need(c, op, !((op->n | op->m) & 1), "double precision with an odd register");
	}
	static void fadd(Sh4 &c, const Op *op)
	{
		if (!(c.m_r.fpscr & Sh4::FPSCR_PR)) {
			c.m_r.fr[op->n] = bits(f32(c.m_r.fr[op->n]) + f32(c.m_r.fr[op->m]));
		} else {
			need_even(c, op);
			set_dr(c, op->n, get_dr(c, op->n) + get_dr(c, op->m));
		}
		return go(c, op);
	}
	static void fsub(Sh4 &c, const Op *op)
	{
		if (!(c.m_r.fpscr & Sh4::FPSCR_PR)) {
			c.m_r.fr[op->n] = bits(f32(c.m_r.fr[op->n]) - f32(c.m_r.fr[op->m]));
		} else {
			need_even(c, op);
			set_dr(c, op->n, get_dr(c, op->n) - get_dr(c, op->m));
		}
		return go(c, op);
	}
	static void fmul(Sh4 &c, const Op *op)
	{
		if (!(c.m_r.fpscr & Sh4::FPSCR_PR)) {
			c.m_r.fr[op->n] = bits(f32(c.m_r.fr[op->n]) * f32(c.m_r.fr[op->m]));
		} else {
			need_even(c, op);
			set_dr(c, op->n, get_dr(c, op->n) * get_dr(c, op->m));
		}
		return go(c, op);
	}
	static void fdiv(Sh4 &c, const Op *op)
	{
		if (!(c.m_r.fpscr & Sh4::FPSCR_PR)) {
			c.m_r.fr[op->n] = bits(f32(c.m_r.fr[op->n]) / f32(c.m_r.fr[op->m]));
		} else {
			need_even(c, op);
			set_dr(c, op->n, get_dr(c, op->n) / get_dr(c, op->m));
		}
		return go(c, op);
	}
	/* FR0 * FRm + FRn, rounded once (software manual, 6.3.1). */
	static void fmac(Sh4 &c, const Op *op)
	{
		need(c, op, !(c.m_r.fpscr & Sh4::FPSCR_PR), "FMAC in double precision");
		c.m_r.fr[op->n] = bits(std::fma(f32(c.m_r.fr[op->m]), f32(c.m_r.fr[0]), f32(c.m_r.fr[op->n])));
		return go(c, op);
	}
	static void fsqrt(Sh4 &c, const Op *op)
	{
		need_fpu(c, op);
		if (!(c.m_r.fpscr & Sh4::FPSCR_PR)) {
			c.m_r.fr[op->n] = bits(std::sqrt(f32(c.m_r.fr[op->n])));
		} else {
			need(c, op, !(op->n & 1), "double precision with an odd register");
			set_dr(c, op->n, std::sqrt(get_dr(c, op->n)));
		}
		return go(c, op);
	}
	/* HEURISTIC: the four below are computed by the host, where the
	 * SH-4 has its own tables and error bounds. */
	static void fsrra(Sh4 &c, const Op *op)
	{
		float v = f32(c.m_r.fr[op->n]);
		if (!(v < 0)) {
			c.m_r.fr[op->n] = bits(1.0f / std::sqrt(v));
		}
		return go(c, op);
	}
	/* The angle is the low 16 bits of FPUL, a full turn in 65536. */
	static void fsca(Sh4 &c, const Op *op)
	{
		constexpr float turn = (2.0f * static_cast<float>(std::numbers::pi)) / 65536.0f;
		float angle = static_cast<float>(c.m_r.fpul & 0xFFFF) * turn;
		c.m_r.fr[op->n] = bits(std::sin(angle));
		c.m_r.fr[op->n + 1] = bits(std::cos(angle));
		return go(c, op);
	}
	static void fipr(Sh4 &c, const Op *op)
	{
		const uint32_t *a = &c.m_r.fr[op->n], *b = &c.m_r.fr[op->m];
		need_fpu(c, op);
		float sum = f32(a[1]) * f32(b[1]);
		sum = std::fma(f32(a[0]), f32(b[0]), sum);
		sum = std::fma(f32(a[2]), f32(b[2]), sum);
		sum = std::fma(f32(a[3]), f32(b[3]), sum);
		c.m_r.fr[op->n + 3] = bits(sum);
		return go(c, op);
	}
	/* The vector FRn..FRn+3 by the matrix in XF0..XF15, one column of the
	 * matrix per component of the vector. */
	static void ftrv(Sh4 &c, const Op *op)
	{
		uint32_t *v = &c.m_r.fr[op->n];
		const uint32_t *x = c.m_r.xf;
		float v0 = f32(v[0]), v1 = f32(v[1]), v2 = f32(v[2]), v3 = f32(v[3]);
		need_fpu(c, op);
		for (uint32_t i = 0; i < 4; i++) {
			float sum = v1 * f32(x[4 + i]);
			sum = std::fma(v0, f32(x[i]), sum);
			sum = std::fma(v2, f32(x[8 + i]), sum);
			sum = std::fma(v3, f32(x[12 + i]), sum);
			v[i] = bits(sum);
		}
		return go(c, op);
	}
	static void fcmpeq(Sh4 &c, const Op *op)
	{
		if (!(c.m_r.fpscr & Sh4::FPSCR_PR)) {
			c.m_r.t = f32(c.m_r.fr[op->n]) == f32(c.m_r.fr[op->m]);
		} else {
			need_even(c, op);
			c.m_r.t = get_dr(c, op->n) == get_dr(c, op->m);
		}
		return go(c, op);
	}
	static void fcmpgt(Sh4 &c, const Op *op)
	{
		if (!(c.m_r.fpscr & Sh4::FPSCR_PR)) {
			c.m_r.t = f32(c.m_r.fr[op->n]) > f32(c.m_r.fr[op->m]);
		} else {
			need_even(c, op);
			c.m_r.t = get_dr(c, op->n) > get_dr(c, op->m);
		}
		return go(c, op);
	}

	/* Decoding */

	enum class Form : uint8_t {
		None,
		N,		/* register in bits 8-11 */
		NM,		/* and one in bits 4-7 */
		NImm,		/* register and signed 8-bit immediate */
		Imm,		/* unsigned 8-bit immediate */
		ImmSigned,
		MDisp0, MDisp1,	/* register in bits 4-7 and 4-bit displacement, scaled */
		NMDisp2,
		Gbr0, Gbr1, Gbr2,	/* 8-bit displacement from GBR, scaled */
		PcWord, PcLong, PcAddr,	/* 8-bit displacement from PC */
		Branch8, Branch12,
		NBank,		/* register and bank register in bits 4-6 */
		Vectors,	/* FVn in bits 10-11, FVm in bits 8-9 */
		Vector,		/* FVn in bits 10-11 */
		Pair		/* FRn and FRn+1, bits 9-11 */
	};
	using F = Form;
	using K = Sh4::Kind;
	struct Entry {
		uint16_t mask, match;
		void (*run)(Sh4 &, const Op *);
		Form form;
		Sh4::Kind kind;
	};
	static const Entry table[];
	static const uint32_t table_size;
	static const std::array<uint8_t, 65536> &index();
};

/* Software manual, section 9, and tables 7.3 to 7.12 for the codes. FSCA
 * and FSRRA are in the SH7091 only (MAME sh4.cpp). */
const Sh4Ops::Entry Sh4Ops::table[] = {
	{0xF00F, 0x6003, mov, F::NM, K::Plain},
	{0xF00F, 0x2000, movbs, F::NM, K::Plain},
	{0xF00F, 0x2001, movws, F::NM, K::Plain},
	{0xF00F, 0x2002, movls, F::NM, K::Plain},
	{0xF00F, 0x6000, movbl, F::NM, K::Plain},
	{0xF00F, 0x6001, movwl, F::NM, K::Plain},
	{0xF00F, 0x6002, movll, F::NM, K::Plain},
	{0xF00F, 0x2004, movbm, F::NM, K::Plain},
	{0xF00F, 0x2005, movwm, F::NM, K::Plain},
	{0xF00F, 0x2006, movlm, F::NM, K::Plain},
	{0xF00F, 0x6004, movbp, F::NM, K::Plain},
	{0xF00F, 0x6005, movwp, F::NM, K::Plain},
	{0xF00F, 0x6006, movlp, F::NM, K::Plain},
	{0xF00F, 0x0004, movbs0, F::NM, K::Plain},
	{0xF00F, 0x0005, movws0, F::NM, K::Plain},
	{0xF00F, 0x0006, movls0, F::NM, K::Plain},
	{0xF00F, 0x000C, movbl0, F::NM, K::Plain},
	{0xF00F, 0x000D, movwl0, F::NM, K::Plain},
	{0xF00F, 0x000E, movll0, F::NM, K::Plain},
	{0xF000, 0xE000, movi, F::NImm, K::Plain},
	{0xF000, 0x9000, movwi, F::PcWord, K::PcRelative},
	{0xF000, 0xD000, movli, F::PcLong, K::PcRelative},
	{0xFF00, 0xC400, movblg, F::Gbr0, K::Plain},
	{0xFF00, 0xC500, movwlg, F::Gbr1, K::Plain},
	{0xFF00, 0xC600, movllg, F::Gbr2, K::Plain},
	{0xFF00, 0xC000, movbsg, F::Gbr0, K::Plain},
	{0xFF00, 0xC100, movwsg, F::Gbr1, K::Plain},
	{0xFF00, 0xC200, movlsg, F::Gbr2, K::Plain},
	{0xFF00, 0x8000, movbs4, F::MDisp0, K::Plain},
	{0xFF00, 0x8100, movws4, F::MDisp1, K::Plain},
	{0xF000, 0x1000, movls4, F::NMDisp2, K::Plain},
	{0xFF00, 0x8400, movbl4, F::MDisp0, K::Plain},
	{0xFF00, 0x8500, movwl4, F::MDisp1, K::Plain},
	{0xF000, 0x5000, movll4, F::NMDisp2, K::Plain},
	{0xFF00, 0xC700, mova, F::PcAddr, K::PcRelative},
	{0xF0FF, 0x0029, movt, F::N, K::Plain},
	{0xF00F, 0x6008, swapb, F::NM, K::Plain},
	{0xF00F, 0x6009, swapw, F::NM, K::Plain},
	{0xF00F, 0x200D, xtrct, F::NM, K::Plain},

	{0xF00F, 0x300C, add, F::NM, K::Plain},
	{0xF000, 0x7000, addi, F::NImm, K::Plain},
	{0xF00F, 0x300E, addc, F::NM, K::Plain},
	{0xF00F, 0x300F, addv, F::NM, K::Plain},
	{0xFF00, 0x8800, cmpim, F::ImmSigned, K::Plain},
	{0xF00F, 0x3000, cmpeq, F::NM, K::Plain},
	{0xF00F, 0x3002, cmphs, F::NM, K::Plain},
	{0xF00F, 0x3003, cmpge, F::NM, K::Plain},
	{0xF00F, 0x3006, cmphi, F::NM, K::Plain},
	{0xF00F, 0x3007, cmpgt, F::NM, K::Plain},
	{0xF0FF, 0x4011, cmppz, F::N, K::Plain},
	{0xF0FF, 0x4015, cmppl, F::N, K::Plain},
	{0xF00F, 0x200C, cmpstr, F::NM, K::Plain},
	{0xF00F, 0x3004, div1, F::NM, K::Plain},
	{0xF00F, 0x2007, div0s, F::NM, K::Plain},
	{0xFFFF, 0x0019, div0u, F::None, K::Plain},
	{0xF00F, 0x300D, dmuls, F::NM, K::Plain},
	{0xF00F, 0x3005, dmulu, F::NM, K::Plain},
	{0xF0FF, 0x4010, dt, F::N, K::Plain},
	{0xF00F, 0x600E, extsb, F::NM, K::Plain},
	{0xF00F, 0x600F, extsw, F::NM, K::Plain},
	{0xF00F, 0x600C, extub, F::NM, K::Plain},
	{0xF00F, 0x600D, extuw, F::NM, K::Plain},
	{0xF00F, 0x000F, macl, F::NM, K::Plain},
	{0xF00F, 0x400F, macw, F::NM, K::Plain},
	{0xF00F, 0x0007, mull, F::NM, K::Plain},
	{0xF00F, 0x200F, mulsw, F::NM, K::Plain},
	{0xF00F, 0x200E, muluw, F::NM, K::Plain},
	{0xF00F, 0x600B, neg, F::NM, K::Plain},
	{0xF00F, 0x600A, negc, F::NM, K::Plain},
	{0xF00F, 0x3008, sub, F::NM, K::Plain},
	{0xF00F, 0x300A, subc, F::NM, K::Plain},
	{0xF00F, 0x300B, subv, F::NM, K::Plain},

	{0xF00F, 0x2009, and_, F::NM, K::Plain},
	{0xFF00, 0xC900, andi, F::Imm, K::Plain},
	{0xFF00, 0xCD00, andm, F::Imm, K::Plain},
	{0xF00F, 0x6007, not_, F::NM, K::Plain},
	{0xF00F, 0x200B, or_, F::NM, K::Plain},
	{0xFF00, 0xCB00, ori, F::Imm, K::Plain},
	{0xFF00, 0xCF00, orm, F::Imm, K::Plain},
	{0xF0FF, 0x401B, tas, F::N, K::Plain},
	{0xF00F, 0x2008, tst, F::NM, K::Plain},
	{0xFF00, 0xC800, tsti, F::Imm, K::Plain},
	{0xFF00, 0xCC00, tstm, F::Imm, K::Plain},
	{0xF00F, 0x200A, xor_, F::NM, K::Plain},
	{0xFF00, 0xCA00, xori, F::Imm, K::Plain},
	{0xFF00, 0xCE00, xorm, F::Imm, K::Plain},

	{0xF0FF, 0x4004, rotl, F::N, K::Plain},
	{0xF0FF, 0x4005, rotr, F::N, K::Plain},
	{0xF0FF, 0x4024, rotcl, F::N, K::Plain},
	{0xF0FF, 0x4025, rotcr, F::N, K::Plain},
	{0xF0FF, 0x4020, shll, F::N, K::Plain},	/* SHAL: the same result */
	{0xF0FF, 0x4021, shar, F::N, K::Plain},
	{0xF0FF, 0x4000, shll, F::N, K::Plain},
	{0xF0FF, 0x4001, shlr, F::N, K::Plain},
	{0xF0FF, 0x4008, shll2, F::N, K::Plain},
	{0xF0FF, 0x4009, shlr2, F::N, K::Plain},
	{0xF0FF, 0x4018, shll8, F::N, K::Plain},
	{0xF0FF, 0x4019, shlr8, F::N, K::Plain},
	{0xF0FF, 0x4028, shll16, F::N, K::Plain},
	{0xF0FF, 0x4029, shlr16, F::N, K::Plain},
	{0xF00F, 0x400C, shad, F::NM, K::Plain},
	{0xF00F, 0x400D, shld, F::NM, K::Plain},

	{0xFF00, 0x8B00, bf, F::Branch8, K::Branch},
	{0xFF00, 0x8F00, bfs, F::Branch8, K::BranchDelayed},
	{0xFF00, 0x8900, bt, F::Branch8, K::Branch},
	{0xFF00, 0x8D00, bts, F::Branch8, K::BranchDelayed},
	{0xF000, 0xA000, bra, F::Branch12, K::Jump},
	{0xF0FF, 0x0023, braf, F::N, K::Jump},
	{0xF000, 0xB000, bsr, F::Branch12, K::Jump},
	{0xF0FF, 0x0003, bsrf, F::N, K::Jump},
	{0xF0FF, 0x402B, jmp, F::N, K::Jump},
	{0xF0FF, 0x400B, jsr, F::N, K::Jump},
	{0xFFFF, 0x000B, rts, F::None, K::Jump},
	{0xFFFF, 0x002B, rte, F::None, K::Jump},

	{0xFFFF, 0x0008, clrt, F::None, K::Plain},
	{0xFFFF, 0x0018, sett, F::None, K::Plain},
	{0xFFFF, 0x0048, clrs, F::None, K::Plain},
	{0xFFFF, 0x0058, sets, F::None, K::Plain},
	{0xFFFF, 0x0028, clrmac, F::None, K::Plain},
	{0xFFFF, 0x0009, nop, F::None, K::Plain},
	{0xFFFF, 0x001B, sleep, F::None, K::End},
	{0xFF00, 0xC300, trapa, F::Imm, K::End},
	{0xF0FF, 0x400E, ldcsr, F::N, K::End},
	{0xF0FF, 0x401E, ldcgbr, F::N, K::Plain},
	{0xF0FF, 0x402E, ldcvbr, F::N, K::Plain},
	{0xF0FF, 0x403E, ldcssr, F::N, K::Plain},
	{0xF0FF, 0x404E, ldcspc, F::N, K::Plain},
	{0xF0FF, 0x40FA, ldcdbr, F::N, K::Plain},
	{0xF08F, 0x408E, ldcrbank, F::NBank, K::Plain},
	{0xF0FF, 0x4007, ldcmsr, F::N, K::End},
	{0xF0FF, 0x4017, ldcmgbr, F::N, K::Plain},
	{0xF0FF, 0x4027, ldcmvbr, F::N, K::Plain},
	{0xF0FF, 0x4037, ldcmssr, F::N, K::Plain},
	{0xF0FF, 0x4047, ldcmspc, F::N, K::Plain},
	{0xF0FF, 0x40F6, ldcmdbr, F::N, K::Plain},
	{0xF08F, 0x4087, ldcmrbank, F::NBank, K::Plain},
	{0xF0FF, 0x400A, ldsmach, F::N, K::Plain},
	{0xF0FF, 0x401A, ldsmacl, F::N, K::Plain},
	{0xF0FF, 0x402A, ldspr, F::N, K::Plain},
	{0xF0FF, 0x4006, ldsmmach, F::N, K::Plain},
	{0xF0FF, 0x4016, ldsmmacl, F::N, K::Plain},
	{0xF0FF, 0x4026, ldsmpr, F::N, K::Plain},
	{0xF0FF, 0x0002, stcsr, F::N, K::Plain},
	{0xF0FF, 0x0012, stcgbr, F::N, K::Plain},
	{0xF0FF, 0x0022, stcvbr, F::N, K::Plain},
	{0xF0FF, 0x0032, stcssr, F::N, K::Plain},
	{0xF0FF, 0x0042, stcspc, F::N, K::Plain},
	{0xF0FF, 0x003A, stcsgr, F::N, K::Plain},
	{0xF0FF, 0x00FA, stcdbr, F::N, K::Plain},
	{0xF08F, 0x0082, stcrbank, F::NBank, K::Plain},
	{0xF0FF, 0x4003, stcmsr, F::N, K::Plain},
	{0xF0FF, 0x4013, stcmgbr, F::N, K::Plain},
	{0xF0FF, 0x4023, stcmvbr, F::N, K::Plain},
	{0xF0FF, 0x4033, stcmssr, F::N, K::Plain},
	{0xF0FF, 0x4043, stcmspc, F::N, K::Plain},
	{0xF0FF, 0x4032, stcmsgr, F::N, K::Plain},
	{0xF0FF, 0x40F2, stcmdbr, F::N, K::Plain},
	{0xF08F, 0x4083, stcmrbank, F::NBank, K::Plain},
	{0xF0FF, 0x000A, stsmach, F::N, K::Plain},
	{0xF0FF, 0x001A, stsmacl, F::N, K::Plain},
	{0xF0FF, 0x002A, stspr, F::N, K::Plain},
	{0xF0FF, 0x4002, stsmmach, F::N, K::Plain},
	{0xF0FF, 0x4012, stsmmacl, F::N, K::Plain},
	{0xF0FF, 0x4022, stsmpr, F::N, K::Plain},

	/* No TLB and no cache: LDTLB, PREF, OCBI, OCBP and OCBWB do nothing. */
	{0xFFFF, 0x0038, nop, F::None, K::Plain},
	{0xF0FF, 0x0083, nop, F::N, K::Plain},
	{0xF0FF, 0x0093, nop, F::N, K::Plain},
	{0xF0FF, 0x00A3, nop, F::N, K::Plain},
	{0xF0FF, 0x00B3, nop, F::N, K::Plain},
	{0xF0FF, 0x00C3, movca, F::N, K::Plain},

	{0xF0FF, 0x405A, ldsfpul, F::N, K::Plain},
	{0xF0FF, 0x406A, ldsfpscr, F::N, K::Plain},
	{0xF0FF, 0x4056, ldsmfpul, F::N, K::Plain},
	{0xF0FF, 0x4066, ldsmfpscr, F::N, K::Plain},
	{0xF0FF, 0x005A, stsfpul, F::N, K::Plain},
	{0xF0FF, 0x006A, stsfpscr, F::N, K::Plain},
	{0xF0FF, 0x4052, stsmfpul, F::N, K::Plain},
	{0xF0FF, 0x4062, stsmfpscr, F::N, K::Plain},
	{0xFFFF, 0xF3FD, fschg, F::None, K::Plain},
	{0xFFFF, 0xFBFD, frchg, F::None, K::Plain},
	{0xF0FF, 0xF00D, fsts, F::N, K::Plain},
	{0xF0FF, 0xF01D, flds, F::N, K::Plain},
	{0xF0FF, 0xF02D, flt, F::N, K::Plain},
	{0xF0FF, 0xF03D, ftrc, F::N, K::Plain},
	{0xF0FF, 0xF04D, fneg, F::N, K::Plain},
	{0xF0FF, 0xF05D, fabs_, F::N, K::Plain},
	{0xF0FF, 0xF06D, fsqrt, F::N, K::Plain},
	{0xF0FF, 0xF07D, fsrra, F::N, K::Plain},
	{0xF0FF, 0xF08D, fldi0, F::N, K::Plain},
	{0xF0FF, 0xF09D, fldi1, F::N, K::Plain},
	{0xF0FF, 0xF0AD, fcnvsd, F::N, K::Plain},
	{0xF0FF, 0xF0BD, fcnvds, F::N, K::Plain},
	{0xF0FF, 0xF0ED, fipr, F::Vectors, K::Plain},
	{0xF1FF, 0xF0FD, fsca, F::Pair, K::Plain},
	{0xF3FF, 0xF1FD, ftrv, F::Vector, K::Plain},
	{0xF00F, 0xF000, fadd, F::NM, K::Plain},
	{0xF00F, 0xF001, fsub, F::NM, K::Plain},
	{0xF00F, 0xF002, fmul, F::NM, K::Plain},
	{0xF00F, 0xF003, fdiv, F::NM, K::Plain},
	{0xF00F, 0xF004, fcmpeq, F::NM, K::Plain},
	{0xF00F, 0xF005, fcmpgt, F::NM, K::Plain},
	{0xF00F, 0xF006, fmov_index_load, F::NM, K::Plain},
	{0xF00F, 0xF007, fmov_index_store, F::NM, K::Plain},
	{0xF00F, 0xF008, fmov_load, F::NM, K::Plain},
	{0xF00F, 0xF009, fmov_restore, F::NM, K::Plain},
	{0xF00F, 0xF00A, fmov_store, F::NM, K::Plain},
	{0xF00F, 0xF00B, fmov_save, F::NM, K::Plain},
	{0xF00F, 0xF00C, fmov, F::NM, K::Plain},
	{0xF00F, 0xF00E, fmac, F::NM, K::Plain},
};
const uint32_t Sh4Ops::table_size = sizeof(Sh4Ops::table) / sizeof(Sh4Ops::table[0]);

/* The rank + 1 in the table for each of the 65536 codes, 0 for a code that
 * is no instruction. */
const std::array<uint8_t, 65536> &Sh4Ops::index()
{
	static const std::array<uint8_t, 65536> built = [] {
		std::array<uint8_t, 65536> t{};
		for (uint32_t inst = 0; inst < 65536; inst++) {
			for (uint32_t i = 0; i < table_size; i++) {
				if ((inst & table[i].mask) == table[i].match) {
					t[inst] = static_cast<uint8_t>(i + 1);
					break;
				}
			}
		}
		return t;
	}();
	return built;
}

Sh4::Kind Sh4::decode(uint16_t inst, uint32_t pc, Sh4Op &op)
{
	uint32_t rank = Sh4Ops::index()[inst];
	uint32_t n = (inst >> 8) & 15, m = (inst >> 4) & 15;
	uint32_t imm8 = inst & 0xFF, disp4 = inst & 15;
	uint32_t simm8 = static_cast<uint32_t>(static_cast<int32_t>(static_cast<int8_t>(inst)));
	using F = Sh4Ops::Form;

	op = {};
	if (rank == 0) {
		op.run = Sh4Ops::invalid;
		op.imm = inst;
		return Kind::End;
	}

	const Sh4Ops::Entry &e = Sh4Ops::table[rank - 1];
	op.run = e.run;
	switch (e.form) {
	case F::None:
		break;
	case F::N:
		op.n = static_cast<uint8_t>(n);
		break;
	case F::NM:
		op.n = static_cast<uint8_t>(n);
		op.m = static_cast<uint8_t>(m);
		break;
	case F::NImm:
		op.n = static_cast<uint8_t>(n);
		op.imm = simm8;
		break;
	case F::Imm:
		op.imm = imm8;
		break;
	case F::ImmSigned:
		op.imm = simm8;
		break;
	case F::MDisp0:
		op.m = static_cast<uint8_t>(m);
		op.imm = disp4;
		break;
	case F::MDisp1:
		op.m = static_cast<uint8_t>(m);
		op.imm = disp4 << 1;
		break;
	case F::NMDisp2:
		op.n = static_cast<uint8_t>(n);
		op.m = static_cast<uint8_t>(m);
		op.imm = disp4 << 2;
		break;
	case F::Gbr0:
		op.imm = imm8;
		break;
	case F::Gbr1:
		op.imm = imm8 << 1;
		break;
	case F::Gbr2:
		op.imm = imm8 << 2;
		break;
	case F::PcWord:
		op.n = static_cast<uint8_t>(n);
		op.imm = pc + 4 + (imm8 << 1);
		break;
	case F::PcLong:
		op.n = static_cast<uint8_t>(n);
		op.imm = ((pc + 4) & ~3u) + (imm8 << 2);
		break;
	case F::PcAddr:
		op.imm = ((pc + 4) & ~3u) + (imm8 << 2);
		break;
	case F::Branch8:
		op.imm = pc + (simm8 << 1) + 4;
		break;
	case F::Branch12:
		op.imm = pc + ((((inst & 0xFFFu) ^ 0x800u) - 0x800u) << 1) + 4;
		break;
	case F::NBank:
		op.n = static_cast<uint8_t>(n);
		op.m = static_cast<uint8_t>(m & 7);
		break;
	case F::Vectors:
		op.n = static_cast<uint8_t>(n & 12);
		op.m = static_cast<uint8_t>((n & 3) << 2);
		break;
	case F::Vector:
		op.n = static_cast<uint8_t>(n & 12);
		break;
	case F::Pair:
		op.n = static_cast<uint8_t>(n & 14);
		break;
	}
	return e.kind;
}

/* Ends a block that stops for its length: the program goes on at `imm`. */
void Sh4::op_next(Sh4 &c, const Sh4Op *op)
{
	c.m_r.pc = op->imm;
	return c.chain(op->imm);
}

/* Ends the delay slot of a taken branch: `imm` cycles are given back for
 * the instructions of the block that did not run, and the program goes on
 * at PC, through the run loop if the slot disturbed something. */
void Sh4::op_leave(Sh4 &c, const Sh4Op *op)
{
	c.m_remaining += static_cast<int32_t>(op->imm);
	if (!c.m_event) [[likely]] {
		return c.chain(c.m_r.pc);
	}
}

/* Whether a conditional branch with a delay slot goes, for this T. */
bool Sh4::taken(const Sh4Op &op, uint32_t t)
{
	return (op.run == Sh4Ops::bts) == (t != 0);
}

/* A delay loop, DT Rn then BF back to it: the BF counts the turns. */
void Sh4::pair_delay_loop(const Sh4Op &before, uint32_t before_pc, Sh4Op &op)
{
	if (before.run == Sh4Ops::dt && op.run == Sh4Ops::bf && op.imm == before_pc) {
		op.run = Sh4Ops::bf_dt;
		op.n = before.n;
	}
}

} /* namespace odin */
