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

#ifndef ODIN_ARM7_HPP
#define ODIN_ARM7_HPP

#include <cstdint>
#include <cstdio>

namespace odin {

/* Everything the ARM7 keeps between two instructions. The active bank
 * is folded back into the mode banks, so the layout does not depend on
 * the current mode. Plain data so that a machine can save and compare
 * it byte by byte. */
struct Arm7State {
	uint32_t r[8];       /* r0-r7, shared by every mode */
	uint32_t pc;         /* address of the next instruction to fetch */
	uint32_t cpsr;
	uint32_t usr[7];     /* r8-r14 of User and System */
	uint32_t fiq[7];     /* r8-r14 of FIQ */
	uint32_t irq[2];     /* r13-r14 */
	uint32_t svc[2];
	uint32_t abt[2];
	uint32_t und[2];
	uint32_t spsr[5];    /* FIQ, IRQ, SVC, ABT, UND */
	uint8_t irq_line, fiq_line;
};

/* What the CPU sees outside its pins. The machine implements it.
 * Addresses are the 32-bit ARM address; 16 and 32-bit accesses are
 * aligned by the core before they reach the bus. */
class Arm7Bus {
public:
	virtual ~Arm7Bus() = default;
	virtual uint8_t read8(uint32_t addr) = 0;
	virtual uint16_t read16(uint32_t addr) = 0;
	virtual uint32_t read32(uint32_t addr) = 0;
	virtual void write8(uint32_t addr, uint8_t value) = 0;
	virtual void write16(uint32_t addr, uint16_t value) = 0;
	virtual void write32(uint32_t addr, uint32_t value) = 0;
};

/* ARM7DI: ARMv4 instruction set in ARM state only, little-endian,
 * 32-bit addressing. Thumb is not implemented; a branch into Thumb state
 * is refused and reported through the trace. */
class Arm7 {
public:
	static constexpr uint32_t MODE_USR = 0x10, MODE_FIQ = 0x11, MODE_IRQ = 0x12, MODE_SVC = 0x13,
		MODE_ABT = 0x17, MODE_UND = 0x1B, MODE_SYS = 0x1F;
	static constexpr uint32_t FLAG_N = 0x80000000u, FLAG_Z = 0x40000000u, FLAG_C = 0x20000000u,
		FLAG_V = 0x10000000u, FLAG_I = 0x80u, FLAG_F = 0x40u, FLAG_T = 0x20u;

	explicit Arm7(Arm7Bus &bus);

	void reset();
	/* Runs whole instructions until at least `cycles` bus cycles have
	 * elapsed. Returns the cycles actually spent. */
	uint32_t run(uint32_t cycles);
	/* Services a pending FIQ or IRQ, then runs one instruction. */
	uint32_t step();
	/* Takes a pending FIQ or IRQ without running an instruction.
	 * Returns the cycles spent, 0 when nothing was pending. */
	uint32_t poll_interrupts();

	void set_irq(bool level) { m_irq_line = level; }
	void set_fiq(bool level) { m_fiq_line = level; }

	void save(Arm7State &out) const;
	void load(const Arm7State &in);

	/* Trace lines go to `out`; nullptr turns tracing off. */
	void set_trace(FILE *out) { m_trace = out; }

	uint32_t pc() const { return m_pc; }
	uint32_t cpsr() const { return m_cpsr; }
	uint32_t mode() const { return m_cpsr & 0x1F; }
	uint32_t reg(uint32_t idx) const { return m_r[idx & 15]; }
	void set_reg(uint32_t idx, uint32_t v);
	uint32_t spsr() const;
	/* Count of instructions whose condition passed since reset. */
	uint64_t instructions() const { return m_instructions; }

private:
	static uint8_t u8(uint32_t v) { return static_cast<uint8_t>(v); }
	static uint16_t u16(uint32_t v) { return static_cast<uint16_t>(v); }
	static uint32_t ror(uint32_t v, uint32_t n) { n &= 31; return n ? ((v >> n) | (v << (32 - n))) : v; }
	static uint32_t bit(uint32_t v, uint32_t n) { return (v >> n) & 1; }

	/* Bus access with the trace point. */
	uint32_t fetch();
	uint8_t rd8(uint32_t addr);
	uint16_t rd16(uint32_t addr);
	uint32_t rd32(uint32_t addr);
	void wr8(uint32_t addr, uint8_t v);
	void wr16(uint32_t addr, uint16_t v);
	void wr32(uint32_t addr, uint32_t v);
	void trace(char rw, uint32_t addr, uint32_t size, uint32_t v) const;
	void trace_note(const char *what, uint32_t v) const;

	/* Register file, banks and modes. */
	uint32_t get_r(uint32_t idx) const { return idx == 15 ? m_pc + 4 : m_r[idx]; }
	void set_pc(uint32_t v);
	uint32_t *bank_slot(uint32_t mode_bits, uint32_t idx);
	uint32_t *spsr_slot(uint32_t mode_bits);
	void switch_mode(uint32_t new_mode);
	void set_cpsr_full(uint32_t v);
	uint32_t get_user_r(uint32_t idx);
	void set_user_r(uint32_t idx, uint32_t v);
	bool cond(uint32_t cc) const;

	/* Exceptions. */
	void take_exception(uint32_t vector, uint32_t new_mode, uint32_t lr, bool mask_fiq);
	void take_fiq();
	void take_irq();
	void take_swi();
	void take_undefined();

	/* Shifter and flags. */
	uint32_t shifter(uint32_t op, bool want_carry, uint32_t &carry_out);
	uint32_t shift_by(uint32_t type, uint32_t v, uint32_t amount, bool by_register, uint32_t &carry_out);
	void set_nz(uint32_t res);
	uint32_t add_flags(uint32_t a, uint32_t b, uint32_t carry_in);
	uint32_t sub_flags(uint32_t a, uint32_t b, uint32_t carry_in);
	void set_flag(uint32_t mask, bool on) { m_cpsr = on ? (m_cpsr | mask) : (m_cpsr & ~mask); }

	/* Decoders, one per instruction class. */
	void exec(uint32_t op);
	void exec_group0(uint32_t op);
	void exec_group1(uint32_t op);
	void data_processing(uint32_t op);
	void multiply(uint32_t op);
	void multiply_long(uint32_t op);
	void swap(uint32_t op);
	void halfword_transfer(uint32_t op);
	void single_transfer(uint32_t op);
	void block_transfer(uint32_t op);
	void branch(uint32_t op);
	void branch_exchange(uint32_t op);
	void status_to_reg(uint32_t op);
	void reg_to_status(uint32_t op, uint32_t value);
	void undefined(uint32_t op);
	uint32_t single_transfer_offset(uint32_t op);
	void load_pc(uint32_t v);

	Arm7Bus &m_bus;
	FILE *m_trace;

	uint32_t m_r[16];    /* active bank; r15 is kept in m_pc */
	uint32_t m_pc;
	uint32_t m_cpsr;
	uint32_t m_usr[7];
	uint32_t m_fiq[7];
	uint32_t m_irq[2];
	uint32_t m_svc[2];
	uint32_t m_abt[2];
	uint32_t m_und[2];
	uint32_t m_spsr[5];
	bool m_irq_line, m_fiq_line;

	/* Per-instruction scratch. */
	uint32_t m_op_pc;
	uint32_t m_cycles;
	uint64_t m_instructions;
};

} /* namespace odin */

#endif /* ODIN_ARM7_HPP */
