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

#include "odin/machine/cpu/arm7/arm7_host.hpp"

#if defined(ODIN_HOST_AARCH64)

#include "odin/machine/core/arm64_encode.hpp"

namespace odin {

using namespace arm64;

namespace {

constexpr uint32_t SIZE = 8u << 20;

/* x19 the CPU, w20 the cycles spent, w21 those to spend, x22 the memory
 * of the ARM7, x23 the instructions run; A, B, D, E in w0 to w3, and the
 * carry of the host in w15, as x86 leaves it in CF */
constexpr uint8_t SPENT = 20, LIMIT = 21, RAM = 22, COUNT = 23, CARRY = 15;

uint8_t r(Arm7Host::Reg reg) { return static_cast<uint8_t>(reg); }

} /* namespace */

Arm7Host::Arm7Host(const Places &places, Link link)
	: HostCode(SIZE), m_places(places), m_link(link), m_follow(0), m_block(0), m_enter(nullptr)
{
	if (usable()) {
		clear();
	}
}

void Arm7Host::clear()
{
	writable();
	way_in();
	HostCode::copy(SPENT, X2);
	HostCode::copy(LIMIT, X3);
	HostCode::load(RAM, X19, m_places.ram_base, 8);
	HostCode::copy(COUNT, ZR, true);
	way_in_end();
	HostCode::load(X9, X19, m_places.instructions, 8);
	emit(ADD | WIDE | (COUNT << 16) | (X9 << 5) | X9);
	HostCode::store(X9, X19, m_places.instructions, 8);
	HostCode::copy(X0, SPENT);
	way_out();

	m_follow = m_used;
	HostCode::load(X9, X19, m_places.instructions, 8);
	emit(ADD | WIDE | (COUNT << 16) | (X9 << 5) | X9);
	HostCode::store(X9, X19, m_places.instructions, 8);
	HostCode::copy(COUNT, ZR, true);
	HostCode::copy(X0, X19, true);
	call_to(reinterpret_cast<uintptr_t>(m_link));
	jump_to_if_zero(X0, true, m_leave);
	emit(BR | (X0 << 5));

	m_enter = reinterpret_cast<uint32_t (*)(Arm7 *, const uint8_t *, uint32_t, uint32_t)>(m_code);
}

void Arm7Host::store_value(int32_t place, uint32_t value)
{
	if (value == 0) {
		HostCode::store(ZR, X19, place, 4);
		return;
	}
	move(X9, value);
	HostCode::store(X9, X19, place, 4);
}

void Arm7Host::count()
{
	HostCode::add_value(COUNT, COUNT, 1);
}

void Arm7Host::cycles_spent(uint32_t pc)
{
	emit(SUBS | (LIMIT << 16) | (SPENT << 5) | ZR);
	m_exits.push_back({jump_if(HS), pc + 4});
}

uint32_t Arm7Host::begin(uint32_t pc)
{
	writable();
	m_block = pc;
	m_stale.clear();
	m_exits.clear();
	return m_used;
}

void Arm7Host::check(uint32_t at, uint32_t pc, uint32_t op)
{
	HostCode::load(X9, RAM, at, 4);
	move(X10, op);
	emit(SUBS | (X10 << 16) | (X9 << 5) | ZR);
	m_stale.push_back({jump_if(NE), pc});
}

/* The bit of NZCV in `pass` */
HostCode::Jump Arm7Host::unless(uint16_t pass)
{
	HostCode::load(X9, X19, m_places.cpsr, 4);
	emit(lsr(28) | (X9 << 5) | X9);
	move(X10, pass);
	emit(LSRV | (X9 << 16) | (X10 << 5) | X10);
	emit(ANDS_VALUE | (X10 << 5) | ZR);
	return jump_if(EQ);
}

void Arm7Host::call(uint32_t pc, uint32_t op, Run does)
{
	const Places &p = m_places;

	store_value(p.cycles, 0);
	store_value(p.op_pc, pc);
	store_value(p.pc, pc + 4);
	HostCode::copy(X0, X19, true);
	move(X1, op);
	call_to(reinterpret_cast<uintptr_t>(does));
	HostCode::load(X9, X19, p.cycles, 4);
	emit(ADD | (X9 << 16) | (SPENT << 5) | SPENT);
	count();

	/* An interrupt to take: FIQ or IRQ raised and not masked */
	HostCode::load(X9, X19, p.fiq_line, 1);
	HostCode::load(X10, X19, p.irq_line, 1);
	emit(lsl(6) | (X9 << 5) | X9);
	emit(ORR | (7 << 10) | (X10 << 16) | (X9 << 5) | X9);
	HostCode::load(X10, X19, p.cpsr, 4);
	emit(BICS | (X10 << 16) | (X9 << 5) | ZR);
	jump_to_if(NE, m_leave);

	emit(SUBS | (LIMIT << 16) | (SPENT << 5) | ZR);
	jump_to_if(HS, m_leave);
	HostCode::load(X9, X19, p.pc, 4);
	move(X10, pc + 4);
	emit(SUBS | (X10 << 16) | (X9 << 5) | ZR);
	jump_to_if(NE, m_follow);
}

void Arm7Host::spent(uint32_t pc, uint32_t cycles)
{
	HostCode::add_value(SPENT, SPENT, cycles, false);
	count();
	cycles_spent(pc);
}

void Arm7Host::went(uint32_t cycles)
{
	HostCode::add_value(SPENT, SPENT, cycles, false);
	count();
	emit(SUBS | (LIMIT << 16) | (SPENT << 5) | ZR);
	jump_to_if(HS, m_leave);
	jump_back(m_follow);
}

uint32_t Arm7Host::branch(uint32_t pc, uint32_t target, bool link, uint32_t direct)
{
	if (link) {
		store_value(m_places.r + 4 * 14, pc + 4);
	}
	HostCode::add_value(SPENT, SPENT, 3, false);
	count();
	store_value(m_places.pc, target);
	emit(SUBS | (LIMIT << 16) | (SPENT << 5) | ZR);
	jump_to_if(HS, m_leave);
	HostCode::load(X9, X19, m_places.kept, 1);
	jump_to_if_zero(X9, false, m_follow);
	/* The way on until the block of the target is written */
	const uint32_t at = m_used;
	jump_back(direct ? direct : m_follow);
	return direct ? 0 : at;
}

void Arm7Host::link_to(uint32_t at, uint32_t place)
{
	writable();
	relink(at, place);
}

void Arm7Host::failed(Jump skip, uint32_t pc)
{
	const Jump on = jump_ahead();

	bind(skip);
	HostCode::add_value(SPENT, SPENT, 1, false);
	HostCode::load(X9, X19, m_places.failed, 8);
	HostCode::add_value(X9, X9, 1);
	HostCode::store(X9, X19, m_places.failed, 8);
	cycles_spent(pc);
	bind(on);
}

void Arm7Host::end(uint32_t next)
{
	store_value(m_places.pc, next);
	jump_back(m_follow);
	for (const Out &out : m_stale) {
		bind(out.jump);
		store_value(m_places.pc, out.pc);
		store_value(m_places.stale_block, m_block);
		move(X9, 1);
		HostCode::store(X9, X19, m_places.stale, 1);
		jump_back(m_leave);
	}
	for (const Out &out : m_exits) {
		bind(out.jump);
		store_value(m_places.pc, out.pc);
		jump_back(m_leave);
	}
}

void Arm7Host::load(Reg reg, uint32_t n)
{
	HostCode::load(r(reg), X19, m_places.r + static_cast<int32_t>(4 * n), 4);
}

void Arm7Host::load_value(Reg reg, uint32_t value)
{
	move(r(reg), value);
}

void Arm7Host::store(uint32_t n, Reg reg)
{
	HostCode::store(r(reg), X19, m_places.r + static_cast<int32_t>(4 * n), 4);
}

void Arm7Host::store_pc(Reg reg)
{
	HostCode::store(r(reg), X19, m_places.pc, 4);
}

void Arm7Host::copy(Reg dst, Reg src)
{
	HostCode::copy(r(dst), r(src));
}

/* The carry comes in from w15: as is for Adc, turned over for Sbb, whose
 * carry_from_c(true) gave the borrow of x86. The flags come out as AArch64
 * sets them, C no borrow after a subtraction, as flags_arithmetic takes. */
void Arm7Host::alu(Alu op, Reg dst, Reg src)
{
	static constexpr uint32_t CODE[] = { ADDS, ADCS, SUBS, SBCS, AND, ORR, EOR };

	if (op == Alu::Adc) {
		emit(SUBS_VALUE | (1 << 10) | (CARRY << 5) | ZR);	/* C = w15 */
	} else if (op == Alu::Sbb) {
		emit(SUBS | (CARRY << 16) | (ZR << 5) | ZR);		/* C = !w15 */
	}
	emit(CODE[static_cast<uint8_t>(op)] | (r(src) << 16) | (r(dst) << 5) | r(dst));
}

void Arm7Host::invert(Reg reg)
{
	emit(ORN | (r(reg) << 16) | (ZR << 5) | r(reg));
}

/* The last bit out to w15, as x86 leaves it in CF */
void Arm7Host::shift(Shift kind, Reg reg, uint32_t amount)
{
	const uint8_t x = r(reg);

	switch (kind) {
	case Shift::Left:
		emit(ubfx(32 - amount, 1) | (x << 5) | CARRY);
		emit(lsl(amount) | (x << 5) | x);
		break;
	case Shift::Right:
		emit(ubfx(amount - 1, 1) | (x << 5) | CARRY);
		emit(lsr(amount) | (x << 5) | x);
		break;
	case Shift::RightSigned:
		emit(ubfx(amount - 1, 1) | (x << 5) | CARRY);
		emit(asr(amount) | (x << 5) | x);
		break;
	default:
		emit(EXTR | (x << 16) | (amount << 10) | (x << 5) | x);	/* ror */
		emit(lsr(31) | (x << 5) | CARRY);
		break;
	}
}

void Arm7Host::rotate_through_carry(Reg reg)
{
	const uint8_t x = r(reg);

	emit(AND_VALUE | (x << 5) | X9);				/* w9 = bit 0 */
	emit(lsr(1) | (x << 5) | x);
	emit(ORR | (31 << 10) | (CARRY << 16) | (x << 5) | x);
	HostCode::copy(CARRY, X9);
}

void Arm7Host::carry_from_c(bool inverted)
{
	HostCode::load(CARRY, X19, m_places.cpsr, 4);
	emit(ubfx(29, 1) | (CARRY << 5) | CARRY);
	if (inverted) {
		emit(EOR_VALUE | (CARRY << 5) | CARRY);		/* ^ 1 */
	}
}

void Arm7Host::keep_carry(Reg reg)
{
	HostCode::copy(r(reg), CARRY);
}

/* NZCV of the host as they are: they sit where CPSR has them */
void Arm7Host::flags_arithmetic(Reg, bool)
{
	emit(MRS_NZCV | X9);
	HostCode::load(X10, X19, m_places.cpsr, 4);
	and_value(X10, X10, 0x0FFFFFFF, X11);
	emit(ORR | (X9 << 16) | (X10 << 5) | X10);
	HostCode::store(X10, X19, m_places.cpsr, 4);
}

void Arm7Host::flags_logical(Reg result, const Reg *carry)
{
	const uint8_t x = r(result);

	and_value(X9, x, 0x80000000, X11);				/* N */
	emit(SUBS_VALUE | (x << 5) | ZR);
	emit(CSINC | (ZR << 16) | (NE << 12) | (ZR << 5) | X10);	/* Z */
	emit(ORR | (30 << 10) | (X10 << 16) | (X9 << 5) | X9);
	if (carry) {
		emit(ORR | (29 << 10) | (r(*carry) << 16) | (X9 << 5) | X9);
	}
	HostCode::load(X10, X19, m_places.cpsr, 4);
	and_value(X10, X10, carry ? 0x1FFFFFFF : 0x3FFFFFFF, X11);
	emit(ORR | (X9 << 16) | (X10 << 5) | X10);
	HostCode::store(X10, X19, m_places.cpsr, 4);
}

void Arm7Host::flags_logical_value(Reg result, bool carry)
{
	const uint8_t x = r(result);

	and_value(X9, x, 0x80000000, X11);
	emit(SUBS_VALUE | (x << 5) | ZR);
	emit(CSINC | (ZR << 16) | (NE << 12) | (ZR << 5) | X10);
	emit(ORR | (30 << 10) | (X10 << 16) | (X9 << 5) | X9);
	if (carry) {
		uint32_t n, rr, s;

		logical_mask(0x20000000, false, n, rr, s);
		emit(ORR_VALUE | (rr << 16) | (s << 10) | (X9 << 5) | X9);
	}
	HostCode::load(X10, X19, m_places.cpsr, 4);
	and_value(X10, X10, 0x1FFFFFFF, X11);
	emit(ORR | (X9 << 16) | (X10 << 5) | X10);
	HostCode::store(X10, X19, m_places.cpsr, 4);
}

void Arm7Host::mask(Reg reg, uint32_t value)
{
	and_value(r(reg), r(reg), value, X9);
}

void Arm7Host::add_value(Reg reg, uint32_t value)
{
	HostCode::add_value(r(reg), r(reg), value, false);
}

HostCode::Jump Arm7Host::unless_below(Reg reg, uint32_t limit)
{
	if (limit < 4096) {
		emit(SUBS_VALUE | (limit << 10) | (r(reg) << 5) | ZR);
	} else {
		move(X9, limit);
		emit(SUBS | (X9 << 16) | (r(reg) << 5) | ZR);
	}
	return jump_if(HS);
}

/* [x22 + w3] */
void Arm7Host::ram_load(Reg dst, bool one_byte)
{
	emit(indexed_uxtw(one_byte ? LDRB : LDR_W) | (X3 << 16) | (RAM << 5) | r(dst));
}

void Arm7Host::ram_store(Reg src, bool one_byte)
{
	emit(indexed_uxtw(one_byte ? STRB : STR_W) | (X3 << 16) | (RAM << 5) | r(src));
}

void Arm7Host::rotate_by_b(Reg reg)
{
	emit(RORV | (X1 << 16) | (r(reg) << 5) | r(reg));
}

HostCode::Jump Arm7Host::skip_ahead()
{
	return jump_ahead();
}

} /* namespace odin */

#endif
