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

/* ARM7 core: state, bus helpers, register banks, exceptions, the
 * barrel shifter and the flag arithmetic shared by the decoders in
 * arm7_ops.cpp. Semantics follow the ARM7TDMI data sheet (ARM DDI 0029)
 * and the ARM Architecture Reference Manual for ARMv4. Cycle counts are
 * the data sheet's S/N/I sums with every bus cycle worth one clock; no
 * wait states are modelled, the bus owner adds them. */

#include "odin/arm7.hpp"

#include <bit>
#include <format>

namespace odin {

Arm7::Arm7(Arm7Bus &bus)
	: m_bus(bus), m_trace(nullptr)
{
	m_irq_line = m_fiq_line = false;
	reset();
}

/* RESET: SVC mode, IRQ and FIQ masked, ARM state, PC at the reset vector.
 * The other registers are undefined on silicon; they are zeroed so that
 * runs stay deterministic. */
void Arm7::reset()
{
	for (uint32_t &v : m_r) {
		v = 0;
	}
	for (uint32_t &v : m_usr) {
		v = 0;
	}
	for (uint32_t &v : m_fiq) {
		v = 0;
	}
	m_irq[0] = m_irq[1] = 0;
	m_svc[0] = m_svc[1] = 0;
	m_abt[0] = m_abt[1] = 0;
	m_und[0] = m_und[1] = 0;
	for (uint32_t &v : m_spsr) {
		v = 0;
	}
	m_cpsr = FLAG_I | FLAG_F | MODE_SVC;
	m_pc = 0;
	m_op_pc = 0;
	m_cycles = 0;
	m_instructions = 0;
}

uint32_t Arm7::run(uint32_t cycles)
{
	uint32_t done = 0;
	while (done < cycles) {
		done += step();
	}
	return done;
}

/* FIQ beats IRQ; both are level sensitive and sampled between two
 * instructions, which is where the pipeline lets them in. */
uint32_t Arm7::poll_interrupts()
{
	m_cycles = 0;
	if (m_fiq_line && (m_cpsr & FLAG_F) == 0) {
		take_fiq();
	} else if (m_irq_line && (m_cpsr & FLAG_I) == 0) {
		take_irq();
	}
	return m_cycles;
}

uint32_t Arm7::step()
{
	m_cycles = poll_interrupts();
	m_op_pc = m_pc;
	uint32_t op = fetch();
	m_pc += 4;
	if (cond(op >> 28)) {
		m_instructions++;
		exec(op);
	} else {
		/* A failed condition still costs the fetch: 1S. */
		m_cycles += 1;
	}
	return m_cycles;
}

/* Save and load fold the active bank into the mode banks so that the
 * layout of Arm7State does not depend on the mode. */

namespace {

uint32_t *state_slot(Arm7State &s, uint32_t mode, uint32_t idx)
{
	if (idx < 13) {
		return mode == Arm7::MODE_FIQ ? &s.fiq[idx - 8] : &s.usr[idx - 8];
	}
	switch (mode) {
	case Arm7::MODE_FIQ: return &s.fiq[idx - 8];
	case Arm7::MODE_IRQ: return &s.irq[idx - 13];
	case Arm7::MODE_SVC: return &s.svc[idx - 13];
	case Arm7::MODE_ABT: return &s.abt[idx - 13];
	case Arm7::MODE_UND: return &s.und[idx - 13];
	default: return &s.usr[idx - 8];
	}
}

} /* namespace */

void Arm7::save(Arm7State &out) const
{
	for (uint32_t i = 0; i < 8; i++) {
		out.r[i] = m_r[i];
	}
	out.pc = m_pc;
	out.cpsr = m_cpsr;
	for (uint32_t i = 0; i < 7; i++) {
		out.usr[i] = m_usr[i];
		out.fiq[i] = m_fiq[i];
	}
	for (uint32_t i = 0; i < 2; i++) {
		out.irq[i] = m_irq[i];
		out.svc[i] = m_svc[i];
		out.abt[i] = m_abt[i];
		out.und[i] = m_und[i];
	}
	for (uint32_t i = 0; i < 5; i++) {
		out.spsr[i] = m_spsr[i];
	}
	for (uint32_t i = 8; i < 15; i++) {
		*state_slot(out, mode(), i) = m_r[i];
	}
	out.irq_line = m_irq_line;
	out.fiq_line = m_fiq_line;
}

void Arm7::load(const Arm7State &in)
{
	Arm7State s = in;
	for (uint32_t i = 0; i < 8; i++) {
		m_r[i] = s.r[i];
	}
	m_pc = s.pc;
	m_cpsr = s.cpsr;
	for (uint32_t i = 0; i < 7; i++) {
		m_usr[i] = s.usr[i];
		m_fiq[i] = s.fiq[i];
	}
	for (uint32_t i = 0; i < 2; i++) {
		m_irq[i] = s.irq[i];
		m_svc[i] = s.svc[i];
		m_abt[i] = s.abt[i];
		m_und[i] = s.und[i];
	}
	for (uint32_t i = 0; i < 5; i++) {
		m_spsr[i] = s.spsr[i];
	}
	for (uint32_t i = 8; i < 15; i++) {
		m_r[i] = *state_slot(s, mode(), i);
	}
	m_r[15] = 0;
	m_irq_line = s.irq_line != 0;
	m_fiq_line = s.fiq_line != 0;
}

/* Bus access. The trace point is one pointer test when off. Word and
 * halfword addresses are aligned here: the ARM7 drops A[1:0] and A[0]
 * on the bus and rotates the loaded data instead (data sheet 4.9.3). */

void Arm7::trace(char rw, uint32_t addr, uint32_t size, uint32_t v) const
{
	std::fputs(std::format("arm7 {:08x} {} {:08x} {} {:0{}x}\n", m_op_pc, rw, addr, size, v, size * 2).c_str(), m_trace);
}

void Arm7::trace_note(const char *what, uint32_t v) const
{
	if (m_trace) {
		std::fputs(std::format("arm7 {:08x} # {} {:08x}\n", m_op_pc, what, v).c_str(), m_trace);
	}
}

uint32_t Arm7::fetch()
{
	uint32_t addr = m_pc & ~3u;
	uint32_t op = m_bus.read32(addr);
	if (m_trace) {
		trace('f', addr, 4, op);
	}
	return op;
}

uint8_t Arm7::rd8(uint32_t addr)
{
	uint8_t v = m_bus.read8(addr);
	if (m_trace) {
		trace('r', addr, 1, v);
	}
	return v;
}

uint16_t Arm7::rd16(uint32_t addr)
{
	addr &= ~1u;
	uint16_t v = m_bus.read16(addr);
	if (m_trace) {
		trace('r', addr, 2, v);
	}
	return v;
}

uint32_t Arm7::rd32(uint32_t addr)
{
	addr &= ~3u;
	uint32_t v = m_bus.read32(addr);
	if (m_trace) {
		trace('r', addr, 4, v);
	}
	return v;
}

void Arm7::wr8(uint32_t addr, uint8_t v)
{
	if (m_trace) {
		trace('w', addr, 1, v);
	}
	m_bus.write8(addr, v);
}

void Arm7::wr16(uint32_t addr, uint16_t v)
{
	addr &= ~1u;
	if (m_trace) {
		trace('w', addr, 2, v);
	}
	m_bus.write16(addr, v);
}

void Arm7::wr32(uint32_t addr, uint32_t v)
{
	addr &= ~3u;
	if (m_trace) {
		trace('w', addr, 4, v);
	}
	m_bus.write32(addr, v);
}

/* Register banks. r0-r7 and r15 are shared; FIQ has its own r8-r14,
 * the other exception modes their own r13-r14. The active set lives in
 * m_r and is swapped on every mode change. */

uint32_t *Arm7::bank_slot(uint32_t mode_bits, uint32_t idx)
{
	if (idx < 13) {
		return mode_bits == MODE_FIQ ? &m_fiq[idx - 8] : &m_usr[idx - 8];
	}
	switch (mode_bits) {
	case MODE_FIQ: return &m_fiq[idx - 8];
	case MODE_IRQ: return &m_irq[idx - 13];
	case MODE_SVC: return &m_svc[idx - 13];
	case MODE_ABT: return &m_abt[idx - 13];
	case MODE_UND: return &m_und[idx - 13];
	case MODE_USR:
	case MODE_SYS:
		return &m_usr[idx - 8];
	default:
		/* An illegal mode has no bank; the user set stands in. */
		trace_note("illegal mode", mode_bits);
		return &m_usr[idx - 8];
	}
}

uint32_t *Arm7::spsr_slot(uint32_t mode_bits)
{
	switch (mode_bits) {
	case MODE_FIQ: return &m_spsr[0];
	case MODE_IRQ: return &m_spsr[1];
	case MODE_SVC: return &m_spsr[2];
	case MODE_ABT: return &m_spsr[3];
	case MODE_UND: return &m_spsr[4];
	default: return nullptr;
	}
}

/* User and System have no SPSR; reading it gives the CPSR. */
uint32_t Arm7::spsr() const
{
	switch (mode()) {
	case MODE_FIQ: return m_spsr[0];
	case MODE_IRQ: return m_spsr[1];
	case MODE_SVC: return m_spsr[2];
	case MODE_ABT: return m_spsr[3];
	case MODE_UND: return m_spsr[4];
	default: return m_cpsr;
	}
}

void Arm7::switch_mode(uint32_t new_mode)
{
	uint32_t old_mode = mode();
	if (old_mode == new_mode) {
		return;
	}
	for (uint32_t i = 8; i < 15; i++) {
		*bank_slot(old_mode, i) = m_r[i];
	}
	for (uint32_t i = 8; i < 15; i++) {
		m_r[i] = *bank_slot(new_mode, i);
	}
	m_cpsr = (m_cpsr & ~0x1Fu) | new_mode;
}

/* Whole CPSR write: SPSR restore or MSR with the control field. The T
 * bit is refused because Thumb is not implemented. */
void Arm7::set_cpsr_full(uint32_t v)
{
	if (v & FLAG_T) {
		trace_note("thumb state refused", v);
		v &= ~FLAG_T;
	}
	switch_mode(v & 0x1F);
	m_cpsr = v;
}

/* User bank view for LDM/STM with the S bit. */

uint32_t Arm7::get_user_r(uint32_t idx)
{
	if (idx < 8 || idx == 15) {
		return get_r(idx);
	}
	uint32_t m = mode();
	if (m == MODE_USR || m == MODE_SYS) {
		return m_r[idx];
	}
	if (m == MODE_FIQ || idx >= 13) {
		return m_usr[idx - 8];
	}
	return m_r[idx];
}

void Arm7::set_user_r(uint32_t idx, uint32_t v)
{
	if (idx == 15) {
		set_pc(v);
		return;
	}
	uint32_t m = mode();
	if (idx < 8 || m == MODE_USR || m == MODE_SYS) {
		m_r[idx] = v;
	} else if (m == MODE_FIQ || idx >= 13) {
		m_usr[idx - 8] = v;
	} else {
		m_r[idx] = v;
	}
}

void Arm7::set_reg(uint32_t idx, uint32_t v)
{
	if ((idx & 15) == 15) {
		set_pc(v);
	} else {
		m_r[idx & 15] = v;
	}
}

/* ARM state only: the two low bits of a new PC are dropped. */
void Arm7::set_pc(uint32_t v)
{
	m_pc = v & ~3u;
}

/* EQ NE CS CC MI PL VS VC HI LS GE LT GT LE AL NV. NV never executes on
 * an ARMv4 core (ARM ARM A3.2.1). */
bool Arm7::cond(uint32_t cc) const
{
	bool n = (m_cpsr & FLAG_N) != 0;
	bool z = (m_cpsr & FLAG_Z) != 0;
	bool c = (m_cpsr & FLAG_C) != 0;
	bool v = (m_cpsr & FLAG_V) != 0;
	switch (cc) {
	case 0x0: return z;
	case 0x1: return !z;
	case 0x2: return c;
	case 0x3: return !c;
	case 0x4: return n;
	case 0x5: return !n;
	case 0x6: return v;
	case 0x7: return !v;
	case 0x8: return c && !z;
	case 0x9: return !c || z;
	case 0xA: return n == v;
	case 0xB: return n != v;
	case 0xC: return !z && n == v;
	case 0xD: return z || n != v;
	case 0xE: return true;
	default: return false;
	}
}

/* Exceptions (data sheet 3.8). Entry: SPSR_new = CPSR, mode switch,
 * r14_new = return link, I set (F too for FIQ and reset), PC = vector.
 * The link values assume the handler returns with SUBS pc, lr, #4 for
 * IRQ and FIQ and MOVS pc, lr for SWI and undefined. Entry costs 2S + 1N. */

void Arm7::take_exception(uint32_t vector, uint32_t new_mode, uint32_t lr, bool mask_fiq)
{
	uint32_t old_cpsr = m_cpsr;
	switch_mode(new_mode);
	*spsr_slot(new_mode) = old_cpsr;
	m_r[14] = lr;
	m_cpsr |= FLAG_I;
	if (mask_fiq) {
		m_cpsr |= FLAG_F;
	}
	m_cpsr &= ~FLAG_T;
	m_pc = vector;
	m_cycles += 3;
}

void Arm7::take_fiq()
{
	take_exception(0x1C, MODE_FIQ, m_pc + 4, true);
}

void Arm7::take_irq()
{
	take_exception(0x18, MODE_IRQ, m_pc + 4, false);
}

void Arm7::take_swi()
{
	take_exception(0x08, MODE_SVC, m_pc, false);
}

void Arm7::take_undefined()
{
	take_exception(0x04, MODE_UND, m_pc, false);
}

/* Barrel shifter (data sheet 4.5.2). Returns the second operand and the
 * shifter carry out, which is the old C when nothing moves. With a
 * register shift amount r15 reads 12 bytes ahead instead of 8. */

uint32_t Arm7::shifter(uint32_t op, bool want_carry, uint32_t &carry_out)
{
	carry_out = (m_cpsr & FLAG_C) ? 1 : 0;
	if (op & (1u << 25)) {
		uint32_t rot = ((op >> 8) & 0xF) * 2;
		uint32_t v = ror(op & 0xFF, rot);
		if (want_carry && rot != 0) {
			carry_out = v >> 31;
		}
		return v;
	}
	uint32_t type = (op >> 5) & 3;
	bool by_register = (op & 0x10) != 0;
	uint32_t rm = op & 0xF;
	uint32_t v = get_r(rm);
	uint32_t amount;
	if (by_register) {
		if (rm == 15) {
			v += 4;
		}
		amount = get_r((op >> 8) & 0xF) & 0xFF;
	} else {
		amount = (op >> 7) & 0x1F;
	}
	return shift_by(type, v, amount, by_register, carry_out);
}

/* LSL, LSR, ASR, ROR. An immediate amount of 0 means LSL #0 (no shift),
 * LSR #32, ASR #32 and RRX respectively; a register amount of 0 leaves
 * the value and the carry alone. `carry_out` holds the old C on entry. */
uint32_t Arm7::shift_by(uint32_t type, uint32_t v, uint32_t amount, bool by_register, uint32_t &carry_out)
{
	if (by_register && amount == 0) {
		return v;
	}
	switch (type) {
	case 0:
		if (amount == 0) {
			return v;
		}
		if (amount < 32) {
			carry_out = bit(v, 32 - amount);
			return v << amount;
		}
		carry_out = amount == 32 ? (v & 1) : 0;
		return 0;
	case 1:
		if (amount == 0) {
			amount = 32;
		}
		if (amount < 32) {
			carry_out = bit(v, amount - 1);
			return v >> amount;
		}
		carry_out = amount == 32 ? (v >> 31) : 0;
		return 0;
	case 2:
		if (amount == 0 || amount > 32) {
			amount = 32;
		}
		if (amount < 32) {
			carry_out = bit(v, amount - 1);
			return static_cast<uint32_t>(static_cast<int32_t>(v) >> amount);
		}
		carry_out = v >> 31;
		return (v & 0x80000000u) ? 0xFFFFFFFFu : 0;
	default:
		if (amount == 0) {
			uint32_t old_c = carry_out;
			carry_out = v & 1;
			return (v >> 1) | (old_c << 31);
		}
		amount &= 31;
		if (amount == 0) {
			carry_out = v >> 31;
			return v;
		}
		carry_out = bit(v, amount - 1);
		return ror(v, amount);
	}
}

/* Flags. Logical results set N and Z and take C from the shifter;
 * arithmetic results set all four. */

void Arm7::set_nz(uint32_t res)
{
	m_cpsr = (m_cpsr & ~(FLAG_N | FLAG_Z)) | (res & FLAG_N) | (res == 0 ? FLAG_Z : 0);
}

uint32_t Arm7::add_flags(uint32_t a, uint32_t b, uint32_t carry_in)
{
	uint64_t full = static_cast<uint64_t>(a) + b + carry_in;
	uint32_t res = static_cast<uint32_t>(full);
	set_nz(res);
	set_flag(FLAG_C, (full >> 32) != 0);
	set_flag(FLAG_V, (((a ^ res) & (b ^ res)) >> 31) != 0);
	return res;
}

/* a - b - !carry_in, as a + ~b + carry_in: C is "no borrow". */
uint32_t Arm7::sub_flags(uint32_t a, uint32_t b, uint32_t carry_in)
{
	uint64_t full = static_cast<uint64_t>(a) + static_cast<uint32_t>(~b) + carry_in;
	uint32_t res = static_cast<uint32_t>(full);
	set_nz(res);
	set_flag(FLAG_C, (full >> 32) != 0);
	set_flag(FLAG_V, (((a ^ b) & (a ^ res)) >> 31) != 0);
	return res;
}

} /* namespace odin */
