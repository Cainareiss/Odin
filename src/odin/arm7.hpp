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

#include <array>
#include <cstdint>
#include <cstdio>
#include <vector>

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

	bool operator==(const Arm7State &) const = default;
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
	/* What a read of 1, 2 or 4 bytes would give, without what it does
	 * besides: how the core looks again at what a counted turn read (see
	 * Arm7::set_turn_replay). By default, the read itself. */
	virtual uint32_t peek(uint32_t addr, uint32_t size)
	{
		return size == 1 ? read8(addr) : size == 2 ? read16(addr) : read32(addr);
	}
};

/* Plain memory at the bottom of the address space, which the core reads and
 * writes without the bus: the addresses below `size`, wrapped by `mask`.
 * The machine gives it again whenever it changes. */
struct Arm7Ram {
	uint8_t *base;
	uint32_t size;
	uint32_t mask;
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

	void set_ram(const Arm7Ram &ram);

	void set_irq(bool level);
	void set_fiq(bool level);

	/* A program that waits goes round a loop, and every turn reads the
	 * same things and leaves the registers and the memory as they were,
	 * but for one word it may count its turns in; or a few turns do, one
	 * after the other, and make one. With this on, such a
	 * turn is run once, then only counted for as long as what it read
	 * stays the same; and once the program has left it, for an interrupt
	 * say, it is counted again as soon as the program is back at its
	 * start with everything it read as it was. The machine must then call
	 * ram_write() before it changes the plain memory itself, ram_read()
	 * before it reads it, settle() before it saves the state, and its bus
	 * must peek() without side effects. */
	void set_turn_replay(bool on);
	void ram_read(uint32_t addr, uint32_t size);
	void ram_write(uint32_t addr, uint32_t size);
	/* Runs the part of a turn that was only counted. */
	void settle();
	/* Turns run to be recorded and turns counted so far, for measuring. */
	uint64_t turns_recorded() const { return m_turn_recorded; }
	uint64_t turns_counted() const { return m_turn_counted; }

	/* The state between two instructions: settle() first. */
	void save(Arm7State &out) const;
	void load(const Arm7State &in);

	/* Trace lines go to `out`; nullptr turns tracing off. */
	void set_trace(FILE *out);

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
	Arm7Ram m_ram;
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

	/* Turns counted without being run, see set_turn_replay(). */
	enum class Turn : uint8_t { Off, Idle, Record, Replay, Settle };
	static constexpr uint32_t TURN_MAX = 8192;   /* instructions */
	static constexpr uint32_t TURN_TRAIL = 4 * TURN_MAX; /* loads and stores */
	static constexpr uint32_t TURN_ROUND = 16;   /* turns that make one */
	static constexpr uint32_t TURN_AWAY = 1u << 16; /* instructions away from one kept */
	static constexpr uint32_t TURN_READS = 16;   /* outside the memory */
	static constexpr uint32_t TURN_STORES = 256; /* bytes changed */
	static constexpr uint32_t TURN_PAGE = 12;    /* 4 KB */
	struct TurnRead {
		uint32_t index, addr, size, value;
	};
	struct TurnStore {
		uint32_t offset;
		uint8_t old;
	};
	/* An instruction of the turn being recorded: whether its condition
	 * held, and the addresses of the data it loaded and stored, from
	 * `lo` up to `hi`; none when hi is 0. */
	struct TurnOp {
		uint32_t op;
		uint32_t load_lo, load_hi, store_lo, store_hi;
		bool ran;
	};
	/* A load or a store of the turn in the memory, an instruction read
	 * being a load; and a byte the turn leaves there. */
	struct TurnAccess {
		uint32_t offset, value;
		uint8_t size;
		bool store;
	};
	struct TurnByte {
		uint32_t offset;
		uint8_t value;
	};
	static constexpr uint32_t TURN_NO_COUNTER = 0xFFFFFFFF;

	void turn_begin();
	void turn_step(uint32_t cycles, uint32_t passed);
	void turn_abort();
	void turn_break();
	bool turn_reads_hold();
	bool turn_counts(uint32_t counter);
	uint32_t turn_replay(uint32_t budget);
	bool turn_keep();
	bool turn_rejoin();
	bool turn_registers_same() const;
	void turn_trail(uint32_t offset, uint32_t value, uint32_t size, bool store);
	void turn_note_fetch(uint32_t addr, uint32_t op);
	void turn_note_read(uint32_t addr, uint32_t size, uint32_t value);
	void turn_note_store(uint32_t addr, uint32_t size, uint32_t value);
	bool turn_page(const std::array<uint8_t, 256> &pages, uint32_t addr) const;

	Turn m_turn;
	bool m_turn_bad;
	uint32_t m_turn_wait, m_turn_backoff; /* calls of run() before a record */
	uint32_t m_turn_limit;                /* instructions to look for one */
	uint32_t m_turn_count;                /* instructions of the turn */
	uint32_t m_turn_rounds;               /* times back at its start so far */
	uint32_t m_turn_at;                   /* of them, counted in this turn */
	uint32_t m_turn_num_reads, m_turn_next_read, m_turn_num_stores;
	Arm7State m_turn_start;
	std::array<uint32_t, TURN_MAX + 1> m_turn_cycles; /* summed */
	std::array<uint16_t, TURN_MAX + 1> m_turn_passed; /* summed */
	std::array<uint8_t, 256> m_turn_pages;            /* memory read */
	std::array<uint8_t, 256> m_turn_changed;          /* memory changed */
	std::array<TurnRead, TURN_READS> m_turn_reads;
	std::array<TurnStore, TURN_STORES> m_turn_stores;
	std::array<TurnOp, TURN_MAX> m_turn_ops;
	/* The word the turn counts in, or TURN_NO_COUNTER: what a turn adds
	 * to it, and the turns counted that it has not been given yet */
	uint32_t m_turn_counter, m_turn_delta, m_turn_laps;
	/* The turn recorded last can be counted again: what it took from the
	 * memory and what it leaves there, drawn from the trail of its loads
	 * and stores; the instructions the program still has to come back
	 * to it, and the times it was at its start since without being in
	 * it. See turn_rejoin(). */
	bool m_turn_kept, m_turn_trail_full;
	bool m_turn_one_mode;	/* the turn stays in its mode, with its registers */
	uint32_t m_turn_try, m_turn_misses;
	std::vector<TurnAccess> m_turn_trail, m_turn_inputs;
	std::vector<TurnByte> m_turn_outputs;
	uint64_t m_turn_recorded, m_turn_counted;
};

} /* namespace odin */

#endif /* ODIN_ARM7_HPP */
