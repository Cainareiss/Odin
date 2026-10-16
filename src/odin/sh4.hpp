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

#ifndef ODIN_SH4_HPP
#define ODIN_SH4_HPP

#include <array>
#include <cstdint>
#include <cstring>
#include <memory>
#include <span>
#include <unordered_map>
#include <vector>

#include "odin/sh4_host.hpp"

namespace odin {

/* Interrupt sources, highest fixed priority first (SH7750 hardware manual,
 * table 19.5). The machine names a source by its rank here. */
namespace Sh4Source {
enum : uint32_t {
	NMI,
	IRQ0, IRQ1, IRQ2, IRQ3, IRQ4, IRQ5, IRQ6, IRQ7, IRQ8, IRQ9, IRQ10, IRQ11, IRQ12, IRQ13, IRQ14,
	IRL0, IRL1, IRL2, IRL3,
	UDI, GPIOI,
	DMTE0, DMTE1, DMTE2, DMTE3, DMAE,
	TUNI0, TUNI1, TUNI2, TICPI2,
	ATI, PRI, CUI,
	ERI, RXI, TXI, TEI,
	ERIF, RXIF, BRIF, TXIF,
	ITI, RCMI, ROVI,
	COUNT
};
}

enum class Sh4Run : uint32_t { Running, Stopped, Sleeping, Standby };

struct Sh4IrqSource {
	uint32_t raised;
	uint32_t priority;
	uint32_t offset;	/* of the handler from VBR */
	uint32_t code;		/* for INTEVT */
};

/* Everything the chip keeps between two instructions, on-chip register
 * image aside. Plain data: the machine saves it. SR holds the T bit. */
struct Sh4State {
	uint32_t r[16];
	uint32_t pc, sr, pr, gbr, vbr, dbr, mach, macl, spc, ssr, sgr;
	uint32_t rbank[8];	/* the bank that is not selected */
	uint32_t fr[16], xf[16];
	uint32_t fpul, fpscr;
	Sh4Run run;
	uint32_t irq_pending;
	int32_t irq_index;
	Sh4IrqSource irq[Sh4Source::COUNT];
	uint8_t dmac_running[4];
	uint8_t tmu_running[3];
	uint32_t tmu_counter[3];
	uint8_t pm_running[2];
	uint64_t pm_counter[2];
	int32_t over;	/* cycles the last slice ran past its end */
};

/* What the SH-4 sees outside the plain memory given by map_page() and its
 * own modules. The machine implements it. Addresses are physical (29
 * bits); size is 1, 2, 4 or 8 bytes. */
class Sh4Bus {
public:
	virtual ~Sh4Bus() = default;
	/* False when nothing answers. */
	virtual bool read(uint32_t addr, uint32_t size, uint64_t &value) = 0;
	virtual bool write(uint32_t addr, uint32_t size, uint64_t value) = 0;
	/* For accesses in a row from addr on, by the DMA controller: the bytes
	 * reads give, or writes store, as they are in a memory, up to where
	 * an access would do more. What the accesses would tell the machine
	 * there, it is told now; that writes nothing the CPU has in its
	 * pages. Empty where an access does more. */
	virtual std::span<const uint8_t> read_span(uint32_t addr)
	{
		(void)addr;
		return {};
	}
	virtual std::span<uint8_t> write_span(uint32_t addr)
	{
		(void)addr;
		return {};
	}
	/* The 16 pins of port A. */
	virtual uint16_t port_a_read() = 0;
	virtual void port_a_write(uint16_t pins) = 0;
	/* An access nothing answered, at the address the program gave. */
	virtual void unanswered(bool is_write, uint32_t addr, uint32_t size, uint64_t value) = 0;
	/* The program is about to run the instruction at the hooked address. */
	virtual void hook() = 0;
};

class Sh4;
struct Sh4Threaded;
struct Sh4Called;
template <class Way>
struct Sh4OpsT;

/* One decoded instruction. It runs, then runs the next one of the block
 * unless it leaves the block. */
struct Sh4Op {
	void (*run)(Sh4 &, const Sh4Op *);
	uint32_t imm;	/* immediate, displacement or absolute address */
	uint8_t n, m;	/* register numbers; m of a branch: where its delay slot is */
	uint8_t idx;	/* rank in the block */
	uint8_t flags;
};

/* SH7091 (SH-4): the CPU, its FPU and the on-chip modules the Hikaru games
 * use. No MMU, no cache; one instruction is one cycle.
 *
 * The program is decoded once, in blocks of consecutive instructions, and
 * the blocks are kept. A block is checked against the memory before it
 * runs, so code that changed is decoded again whoever wrote it. The check
 * is not made again as long as nothing can have written the code: the
 * slice has not ended, the bus was not used, and no store of the program
 * fell on a page of code that was checked.
 *
 * A kept block is run by the reader, one instruction handing over to the
 * next, or, when set_translate() says so and the host allows it, by host
 * code written for the block that calls the same instructions in turn. The
 * two must give the same, cycle for cycle. */
class Sh4 {
public:
	static constexpr uint32_t PAGE_BITS = 12;
	static constexpr uint32_t PAGE_SIZE = 1u << PAGE_BITS;

	/* `onchip` is the 64 KB image of the on-chip registers, owned by the
	 * machine and saved by it. */
	/* `clock_mode` is what the pins MD2-MD0 select (hardware manual,
	 * table 10.3). */
	Sh4(Sh4Bus &bus, std::span<uint8_t> onchip, bool master, bool little_endian,
	    uint32_t clock_mode);

	void reset(bool hard);

	/* Plain memory: the 4 KB page at physical address `page` *
	 * PAGE_SIZE is read at `read` and written at `write`. nullptr sends
	 * the access to the bus. */
	void map_page(uint32_t page, const uint8_t *read, uint8_t *write);
	/* The machine answers the accesses to that page of the bus without
	 * writing the plain memory of this CPU: its code stays what was
	 * compared, and the block that makes the access goes on, unless the
	 * machine raised something meanwhile. Until the page is mapped
	 * again. */
	void map_quiet(uint32_t page);
	/* The same for `count` pages from `first`, or, quiet false, those
	 * pages no longer quiet. */
	void set_quiet(uint32_t first, uint32_t count, bool quiet);

	/* Runs whole instructions for `cycles` cycles; a branch and its
	 * delay slot are never split. Returns the cycles run past the end. */
	int32_t run(int32_t cycles);

	/* Any source but NMI. */
	void set_irq(uint32_t source, bool raised);
	/* The level of the NMI pin, low until the machine says otherwise. */
	void set_nmi(bool high);
	/* The time of the machine, in cycles since reset, where run() goes
	 * on from: for a machine that loads a state. */
	void set_time(uint64_t now)
	{
		m_cycles = now;
		m_tmu_time = now;
	}
	void set_run(Sh4Run state)
	{
		m_run = state;
		m_event = true;
	}
	Sh4Run run_state() const { return m_run; }
	/* Whether the kept blocks run as host code; false on a host that
	 * has none. To be set before anything runs. */
	void set_translate(bool on);
	bool translates() const { return m_translate; }

	/* The state between two calls of run(). */
	void save(Sh4State &out) const;
	void load(const Sh4State &in);

	/* The bus is told when the instruction at this physical address is
	 * about to run, outside a delay slot. One address at most. */
	void set_hook(uint32_t addr);
	/* For the hook: the cycles left in the slice, and the way to count
	 * some of them without running them. */
	int32_t remaining() const { return m_remaining; }
	void skip(uint32_t cycles)
	{
		m_remaining -= static_cast<int32_t>(cycles);
		m_skipped += cycles;
		m_touched = true;
	}

	uint32_t reg(uint32_t n) const { return m_r.r[n & 15]; }
	void set_reg(uint32_t n, uint32_t value) { m_r.r[n & 15] = value; }
	uint32_t pc() const { return m_r.pc; }
	uint32_t pr() const { return m_r.pr; }
	uint32_t gbr() const { return m_r.gbr; }
	uint32_t sr() const { return m_r.sr | m_r.t; }
	bool master() const { return m_master; }

	/* Counts since the reset, for the measurements. */
	uint64_t instructions() const { return m_instructions; }
	uint64_t skipped() const { return m_skipped; }
	/* The blocks kept now, all those decoded to be kept, and the times
	 * they were all dropped for lack of room. */
	size_t blocks() const { return m_blocks.size(); }
	uint64_t blocks_decoded() const { return m_decoded; }
	uint64_t blocks_dropped() const { return m_dropped; }
	uint32_t host_bytes() const { return m_host ? m_host->used() : 0; }
	uint64_t host_far_calls() const { return m_host ? m_host->far_calls() : 0; }

private:
	friend struct Sh4Threaded;
	friend struct Sh4Called;
	template <class Way>
	friend struct Sh4OpsT;

	static constexpr uint32_t SR_T = 1u << 0, SR_S = 1u << 1, SR_Q = 1u << 8, SR_M = 1u << 9,
		SR_FD = 1u << 15, SR_BL = 1u << 28, SR_RB = 1u << 29, SR_MD = 1u << 30;
	static constexpr uint32_t SR_MASK = 0x700083F3;
	static constexpr uint32_t FPSCR_MASK = 0x003FFFFF;
	static constexpr uint32_t FPSCR_PR = 1u << 19, FPSCR_SZ = 1u << 20, FPSCR_FR = 1u << 21;

	static constexpr uint32_t BLOCK_MAX = 64;	/* instructions */
	static constexpr uint32_t OPS_MAX = 4u << 20;	/* kept before the blocks are dropped */
	static constexpr uint32_t NEAR_SIZE = 1u << 16;	/* blocks found without the tables */
	static constexpr uint32_t PHYSICAL = 0x1FFFFFFF;	/* the 29 address lines */
	static constexpr uint32_t PHYSICAL_PAGES = 1u << (29 - PAGE_BITS);
	static constexpr uint8_t OP_BAD_SLOT = 1;	/* the delay slot holds what it may not */
	static constexpr uint8_t OP_SLOT = 2;		/* runs as a delay slot */
	/* What a branch is, for the host code of its block */
	static constexpr uint8_t OP_BRANCH = 4;		/* conditional, no delay slot */
	static constexpr uint8_t OP_DELAYED = 8;	/* conditional, its slot `m` further */
	static constexpr uint8_t OP_JUMP = 16;		/* always taken, its slot comes next */

	struct Regs {
		uint32_t r[16];
		uint32_t pc, pr, gbr, vbr, dbr, mach, macl, spc, ssr, sgr;
		uint32_t sr;	/* without T */
		uint32_t t;	/* 0 or 1 */
		uint32_t rbank[8];
		uint32_t fr[16], xf[16];
		uint32_t fpul, fpscr;

		bool operator==(const Regs &) const = default;
	};

	/* `ops` holds the instructions 0 to count - 1 of the block, an end,
	 * then the delay slots of the conditional branches, each followed by
	 * an end. */
	struct Block {
		uint32_t pc;
		uint16_t count;		/* instructions */
		uint16_t words;		/* code words they were decoded from */
		const uint8_t *code;	/* where the code is, nullptr outside plain memory */
		const uint16_t *copy;	/* the code as it was decoded */
		const Sh4Op *ops;
		uint32_t checked;	/* m_epoch when the code was last compared */
		uint32_t host;		/* place of its host code, 0: none */
	};

	/* For each instruction of a page, the kept block that runs it: rank
	 * + 1 in m_blocks, and above PLACE_BITS the rank of the instruction
	 * in the block. A block that starts there comes before any other,
	 * else it is the first one decoded that holds the instruction. */
	struct CodePage {
		std::array<uint32_t, PAGE_SIZE / 2> block;
	};
	static constexpr uint32_t PLACE_BITS = 25;

	/* An instruction host code does itself, with a few steps of its
	 * own on the registers, in place of a call. None of these can have
	 * the block left. */
	enum class Inline : uint8_t {
		No,
		Mov, MovI, MovA, MovT,
		Add, AddI, Sub, Neg, And, AndI, Or, OrI, Xor, XorI, Not,
		Tst, TstI, CmpEq, CmpIm, CmpHs, CmpGe, CmpHi, CmpGt, CmpPz, CmpPl,
		ExtSB, ExtSW, ExtUB, ExtUW,
		ShLL, ShLR, ShAR, ShLL2, ShLL8, ShLL16, ShLR2, ShLR8, ShLR16,
		Dt, ClrT, SetT, Nop,
		/* the memory, plain memory in place and the long way through
		 * the instruction: what is read, then from where */
		MovLL, MovWL, MovBL,		/* Rn = @Rm */
		MovLL4, MovWL4, MovBL4,		/* Rn or R0 = @(disp,Rm) */
		MovLL0, MovWL0, MovBL0,		/* Rn = @(R0,Rm) */
		MovLP, MovWP, MovBP,		/* Rn = @Rm+ */
		MovLLG, MovWLG, MovBLG,		/* R0 = @(disp,GBR) */
		MovLI, MovWI,			/* Rn = @(disp,PC) */
		LdsMPr,				/* PR = @Rn+ */
		/* what is written, then where */
		MovLS, MovWS, MovBS,		/* Rm to @Rn */
		MovLS4, MovWS4, MovBS4,		/* Rm or R0 to @(disp,Rn or Rm) */
		MovLS0, MovWS0, MovBS0,		/* Rm to @(R0,Rn) */
		MovLM, MovWM, MovBM,		/* Rm to @-Rn */
		MovLSG, MovWSG, MovBSG,		/* R0 to @(disp,GBR) */
		StsMPr,				/* PR to @-Rn */
		MovCA,				/* R0 to @Rn */
		/* the branches: the test of T and where they lead to */
		Bt, Bf, BtS, BfS,
		Bra, Bsr, Rts, Jsr, Jmp, BraF, BsrF,
		/* the moves of the FPU, for the FPSCR.SZ the block is seen to
		 * have where they are, the long way otherwise */
		FMov, FMovLoad, FMovStore, FMovIndexLoad, FMovIndexStore, FMovRestore, FMovSave,
		FSChg,		/* called; turns SZ over for what follows */
		/* single precision, the long way when FPSCR.PR is set */
		FSts, FLds, FLt, FTrc, FNeg, FAbs, FLdi0, FLdi1, FAdd, FSub, FMul, FCmpGt, FMac
	};
	/* The long way of an access to the memory in host code: the call of
	 * the instruction `op`, back at `back`. */
	struct LongWay {
		Sh4Host::Slow slow;
		uint32_t op, back;
	};

	enum class Kind : uint8_t {
		Plain,
		PcRelative,	/* plain, but not allowed in a delay slot */
		Branch,		/* conditional, no delay slot */
		BranchDelayed,	/* conditional */
		Jump,		/* always taken, with a delay slot */
		End		/* the block stops after it */
	};

	/* sh4.cpp: the run loop */
	void run_at(uint32_t pc);
	void run_far(uint32_t pc, bool resumed);
	bool fits(Block &b, uint32_t from);
	bool same_code(Block &b);
	void guard_code(const Block &b);
	bool wrote_code(const uint8_t *p, size_t size);
	void share_page(const uint8_t *host, uint32_t page);
	void epoch_wrapped();
	/* Something may have written code: every block is compared again
	 * before it runs. */
	void new_epoch()
	{
		if (++m_epoch == 0) [[unlikely]] {
			epoch_wrapped();
		}
	}
	bool decode_block(uint32_t pc, uint32_t limit, Block &b, std::vector<Sh4Op> &ops);
	void decode_once(uint32_t pc);
	bool bad_slot(Kind kind, uint32_t at) const;
	uint16_t fetch(uint32_t pc);
	void drop_blocks();
	void run_block(const Block &b, uint32_t from = 0);
	bool run_cut(Block &b, uint32_t from, uint32_t limit);
	void chain_stale(uint32_t pc);
	uintptr_t link_stale(uint32_t pc);
	void translate(Block &b);
	/* `pairs`: FPSCR.SZ where the instruction is, as the block is seen to have it */
	bool translate_op(const Sh4Op *ops, uint32_t i, std::vector<LongWay> &ways, bool pairs);
	bool translate_inline(Inline form, const Sh4Op &op);
	bool translate_access(Inline form, const Sh4Op &op, uint32_t i, std::vector<LongWay> &ways, bool pairs);
	bool translate_condition(const Sh4Op &op, Sh4Host::Jump &taken);
	bool translate_float(Inline form, const Sh4Op &op, uint32_t i, std::vector<LongWay> &ways);
	bool translate_jump(const Block &b, const Sh4Op &op, uint32_t i, std::vector<LongWay> &ways);
	static uintptr_t linked(Sh4 &c, uint32_t pc);
	void pairs_through(const Block &b, std::array<bool, BLOCK_MAX + 3> &pairs) const;
	void wait_mark();
	void wait_check();
	void take_irq();
	void update_irqs();
	void set_sr(uint32_t value);
	void set_fpscr(uint32_t value);
	void swap_r_banks();
	void swap_f_banks();
	[[noreturn]] void fatal(const char *what, uint32_t value) const;

	/* Memory, see read() and write() below. */
	bool read_slow(const Sh4Op *op, uint32_t addr, uint32_t size, uint64_t &value);
	bool write_slow(const Sh4Op *op, uint32_t addr, uint32_t size, uint64_t value);
	bool read_any(uint32_t addr, uint32_t size, uint64_t &value);
	bool write_any(uint32_t addr, uint32_t size, uint64_t value);
	bool write_plain(uint32_t addr, uint32_t size, uint64_t value);
	void outside();

	/* sh4_ops.cpp */
	static Kind decode(uint16_t inst, uint32_t pc, Sh4Op &op);
	static Sh4Host::Call called(void (*run)(Sh4 &, const Sh4Op *));
	static Inline inlined(void (*run)(Sh4 &, const Sh4Op *));
	static void op_next(Sh4 &c, const Sh4Op *op);
	static void op_cut(Sh4 &c, const Sh4Op *op);
	static void op_leave(Sh4 &c, const Sh4Op *op);
	static void pair_delay_loop(const Sh4Op &before, uint32_t before_pc, Sh4Op &op);
	static bool taken(const Sh4Op &op, uint32_t t);

	/* sh4_onchip.cpp: the on-chip modules */
	static uint32_t shrink(uint32_t addr) { return ((addr >> 8) & 0xFF00) | (addr & 0xFF); }
	uint64_t reg_get(uint32_t size, uint32_t addr) const;
	void reg_put(uint32_t size, uint32_t addr, uint64_t value);
	bool onchip_read(uint32_t addr, uint32_t size, uint64_t &value);
	bool onchip_write(uint32_t addr, uint32_t size, uint64_t value);
	void onchip_reset(bool hard);
	uint16_t port_a_get();
	void port_a_put(uint16_t value);
	void set_irq_priority(uint32_t source, uint32_t priority);
	void update_irq_priorities();
	void dmac_run();
	uint64_t dmac_get(uint32_t addr, uint32_t size);
	void dmac_put(uint32_t addr, uint32_t size, uint64_t value);
	uint32_t dmac_copy(uint32_t sar, uint32_t dar, uint32_t unit, uint32_t count);
	void dmac_run_channel(uint32_t ch);
	void dmac_update_channel(uint32_t ch, uint32_t request);
	void dmac_update(uint32_t request);
	void dmac_update_irqs();
	uint32_t pck_cycles() const;
	uint32_t mpx_cycles(uint32_t addr, bool write) const;
	uint32_t tmu_period(uint32_t ch) const;
	void tmu_count(uint32_t ch, uint64_t counts);
	void tmu_sync(uint64_t now);
	void tmu_update_irq(uint32_t ch);
	int32_t tmu_next_irq() const;
	int32_t run_part(int32_t cycles, int32_t &used);
	uint32_t store_queue_addr(uint32_t addr) const;

	uint32_t pc_of(const Sh4Op *op) const { return m_blk->pc + 2u * op->idx; }
	/* The time before the instruction: a block takes its cycles when it
	 * is entered. */
	uint64_t time_at(const Sh4Op *op) const
	{
		return m_cycles + static_cast<uint64_t>(static_cast<int64_t>(m_slice) - m_remaining -
		                                        m_blk->count + op->idx);
	}

	/* Leaves the block after an instruction that may have changed an
	 * interrupt, the cycles left or the run state: the run loop looks at
	 * them before a block. A delay slot goes on to the end that follows
	 * it, where the branch is completed. */
	void stop_after(const Sh4Op *op)
	{
		if (op->flags & OP_SLOT) {
			return op[1].run(*this, op + 1);
		}
		stop_here(op);
	}
	/* The same for host code, which is told what comes next: after a
	 * delay slot the block goes on, to the end that follows it. */
	uintptr_t stopped(const Sh4Op *op)
	{
		if (op->flags & OP_SLOT) {
			return 0;
		}
		stop_here(op);
		return 1;
	}
	void stop_here(const Sh4Op *op)
	{
		m_r.pc = pc_of(op) + 2;
		m_remaining += m_blk->count - op->idx - 1;
		m_resumed = true;
	}

	/* At the end of a block that nothing disturbed: the block at `pc`,
	 * entered, when it is kept, was compared in this epoch and fits in
	 * the slice, unless the search for waits has something to do here.
	 * Otherwise nullptr, and the program is where it was. */
	const Block *enter(uint32_t pc)
	{
		Block &b = m_near[(pc >> 1) & (NEAR_SIZE - 1)];

		if (b.pc != pc || b.checked != m_epoch || b.count > m_remaining) [[unlikely]] {
			return nullptr;
		}
		if (pc == m_wait.regs.pc) {
			/* A loop that works is looked at less and less often. */
			if (m_wait.pass == 0) {
				return nullptr;
			}
			m_wait.pass--;
		} else {
			if (m_wait.countdown <= 1) {
				return nullptr;
			}
			m_wait.countdown--;
		}
		m_blk = &b;
		m_remaining -= b.count;
		return &b;
	}
	/* Straight into that block, or back to the run loop. A block that
	 * was not compared in this epoch is, apart from this path, which
	 * every end of a block holds. The calls only nest when the compiler
	 * does not turn them into jumps, to the length of a slice. */
	void chain(uint32_t pc)
	{
		const Block &near = m_near[(pc >> 1) & (NEAR_SIZE - 1)];

		/* Host code is for the run loop to enter. */
		if (near.host) [[unlikely]] {
			return;
		}
		if (const Block *b = enter(pc)) [[likely]] {
			return b->ops->run(*this, b->ops);
		}
		if (near.pc == pc && near.checked != m_epoch) {
			return chain_stale(pc);
		}
	}
	/* The same for host code: the host code of that block, or 1 for the
	 * run loop, which a block without host code is left to. */
	uintptr_t link(uint32_t pc)
	{
		const Block &near = m_near[(pc >> 1) & (NEAR_SIZE - 1)];

		if (near.host == 0) [[unlikely]] {
			return 1;
		}
		if (const Block *b = enter(pc)) [[likely]] {
			return m_host->address(b->host);
		}
		if (near.pc == pc && near.checked != m_epoch) {
			return link_stale(pc);
		}
		return 1;
	}

	/* Reads plain memory in place. False when the block must be left
	 * after the instruction, see stop_after(). */
	template <typename T>
	bool read(const Sh4Op *op, uint32_t addr, T &value)
	{
		const uint8_t *p = m_rd[addr >> PAGE_BITS];
		if (p && !(addr & (sizeof(T) - 1))) [[likely]] {
			std::memcpy(&value, p + (addr & (PAGE_SIZE - 1)), sizeof(T));
			return true;
		}
		uint64_t wide = 0;
		bool quiet = read_slow(op, addr, sizeof(T), wide);
		value = static_cast<T>(wide);
		return quiet;
	}

	/* False when the block must be left after the instruction: something
	 * outside took the write, or it fell on the running block. */
	template <typename T>
	bool write(const Sh4Op *op, uint32_t addr, T value)
	{
		uint8_t *p = m_wr[addr >> PAGE_BITS];
		m_touched = true;
		if (p && !(addr & (sizeof(T) - 1))) [[likely]] {
			p += addr & (PAGE_SIZE - 1);
			std::memcpy(p, &value, sizeof(T));
			if (m_code_epoch[(addr >> PAGE_BITS) & (PHYSICAL_PAGES - 1)] != m_epoch) [[likely]] {
				return true;
			}
			return !wrote_code(p, sizeof(T));
		}
		return write_slow(op, addr, sizeof(T), value);
	}

	Sh4Bus &m_bus;
	std::span<uint8_t> m_onchip;
	bool m_master, m_little_endian;

	Regs m_r;
	Sh4Run m_run;
	int32_t m_remaining;
	/* Cycles the last slice ran past its end: a branch is not cut from
	 * its delay slot. The next slice has that many less. */
	int32_t m_over;

	const Block *m_blk;	/* the running block */
	/* The program goes on where something cut a block short: the end
	 * of a slice, an instruction that had the block left, the return
	 * from an interrupt. No branch leads there: no block is kept for
	 * that place when one that holds it is. */
	bool m_resumed;

	/* Counts what may have written code: a slice, a use of the bus, a
	 * store on a guarded page. A block compared in this epoch is still
	 * what was decoded. */
	uint32_t m_epoch;
	/* For each physical page, the epoch in which code of the page was
	 * compared: a store there starts a new epoch. */
	std::vector<uint32_t> m_code_epoch;
	/* Where each host page is mapped. One that is mapped at two physical
	 * pages is written through the bus: the guard above is by physical
	 * page. */
	std::unordered_map<const uint8_t *, uint32_t> m_mapped;

	/* Plain memory, one entry per page of the 4 GB */
	std::vector<const uint8_t *> m_rd;
	std::vector<uint8_t *> m_wr;
	std::vector<uint8_t> m_quiet;	/* by physical page, see map_quiet() */

	/* The blocks: page of the entry address, then its rank in the page.
	 * m_ops and m_words never move: they are dropped before they would
	 * grow past what was reserved. */
	std::vector<uint32_t> m_page_of;	/* rank + 1 in m_code_pages */
	std::vector<CodePage> m_code_pages;
	std::vector<Block> m_blocks;
	std::vector<Sh4Op> m_ops;
	std::vector<uint16_t> m_words;
	std::vector<Block> m_near;		/* the last block found for each low part of PC */
	std::vector<Sh4Op> m_short_ops;		/* a block decoded for one use */
	/* The host code of the kept blocks, once asked for */
	std::unique_ptr<Sh4Host> m_host;
	bool m_translate;
	std::vector<LongWay> m_long_ways;	/* of the block being translated */

	bool m_hook_on;
	uint32_t m_hook_addr;

	/* Loops that only wait, see wait_check() */
	struct {
		Regs regs;		/* at the mark */
		int32_t remaining;	/* at the mark */
		int32_t countdown;	/* blocks before the next mark */
		int32_t period;
		uint32_t pass, busy;
	} m_wait;
	/* Since the mark: a write, a read outside plain memory or an
	 * interrupt */
	bool m_touched;
	/* Something the run loop looks at before a block may have changed:
	 * an interrupt, the run state, or the machine was called. */
	bool m_event;

	struct {
		bool pending;
		int32_t index;	/* of the raised source of highest priority */
		Sh4IrqSource source[Sh4Source::COUNT];
	} m_intc;
	bool m_dmac_running[4];
	bool m_tmu_running[3];
	uint32_t m_tmu_counter[3];
	/* The time at the start of the slice and its cycles, the time of the
	 * access to a module being served, the time the timers are counted
	 * up to. */
	uint64_t m_cycles, m_at, m_tmu_time;
	int32_t m_slice;
	bool m_retime;	/* a timer was written: the slice is cut again */
	uint32_t m_clock_mode;
	/* Performance counters, 48 bits each */
	bool m_pm_running[2];
	uint64_t m_pm_counter[2];

	uint64_t m_instructions, m_skipped, m_decoded, m_dropped;
};

} /* namespace odin */

#endif /* ODIN_SH4_HPP */
