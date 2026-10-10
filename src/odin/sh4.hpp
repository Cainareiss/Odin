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
#include <span>
#include <unordered_map>
#include <vector>

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
	/* The 16 pins of port A. */
	virtual uint16_t port_a_read() = 0;
	virtual void port_a_write(uint16_t pins) = 0;
	/* An access nothing answered, at the address the program gave. */
	virtual void unanswered(bool is_write, uint32_t addr, uint32_t size, uint64_t value) = 0;
	/* The program is about to run the instruction at the hooked address. */
	virtual void hook() = 0;
};

class Sh4;

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
 * fell on a page of code that was checked. */
class Sh4 {
public:
	static constexpr uint32_t PAGE_BITS = 12;
	static constexpr uint32_t PAGE_SIZE = 1u << PAGE_BITS;

	/* `onchip` is the 64 KB image of the on-chip registers, owned by the
	 * machine and saved by it. */
	Sh4(Sh4Bus &bus, std::span<uint8_t> onchip, bool master, bool little_endian);

	void reset(bool hard);

	/* Plain memory: the 4 KB page at physical address `page` *
	 * PAGE_SIZE is read at `read` and written at `write`. nullptr sends
	 * the access to the bus. */
	void map_page(uint32_t page, const uint8_t *read, uint8_t *write);

	/* Runs whole instructions for `cycles` cycles; a branch and its
	 * delay slot are never split. Returns the cycles run past the end. */
	int32_t run(int32_t cycles);

	void set_irq(uint32_t source, bool raised);
	void set_run(Sh4Run state)
	{
		m_run = state;
		m_event = true;
	}
	Sh4Run run_state() const { return m_run; }

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

private:
	friend struct Sh4Ops;

	static constexpr uint32_t SR_T = 1u << 0, SR_S = 1u << 1, SR_Q = 1u << 8, SR_M = 1u << 9,
		SR_FD = 1u << 15, SR_BL = 1u << 28, SR_RB = 1u << 29, SR_MD = 1u << 30;
	static constexpr uint32_t SR_MASK = 0x700083F3;
	static constexpr uint32_t FPSCR_MASK = 0x003FFFFF;
	static constexpr uint32_t FPSCR_PR = 1u << 19, FPSCR_SZ = 1u << 20, FPSCR_FR = 1u << 21;

	static constexpr uint32_t BLOCK_MAX = 64;	/* instructions */
	static constexpr uint32_t OPS_MAX = 4u << 20;	/* kept before the blocks are dropped */
	static constexpr uint32_t NEAR_SIZE = 1u << 16;	/* blocks found without the tables */
	static constexpr uint32_t PHYSICAL_PAGES = 1u << (29 - PAGE_BITS);
	static constexpr uint8_t OP_BAD_SLOT = 1;	/* the delay slot holds what it may not */
	static constexpr uint8_t OP_SLOT = 2;		/* runs as a delay slot */

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
	};

	struct CodePage {
		std::array<uint32_t, PAGE_SIZE / 2> block;	/* rank + 1 in m_blocks */
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
	void run_far(uint32_t pc);
	bool fits(Block &b);
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
	void run_block(const Block &b);
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
	void outside();

	/* sh4_ops.cpp */
	static Kind decode(uint16_t inst, uint32_t pc, Sh4Op &op);
	static void op_next(Sh4 &c, const Sh4Op *op);
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
	void dmac_run_channel(uint32_t ch);
	void dmac_update_channel(uint32_t ch, uint32_t request);
	void dmac_update(uint32_t request);
	void tmu_run(int32_t cycles);
	void tmu_run_channel(uint32_t ch, uint32_t cycles);
	uint32_t store_queue_addr(uint32_t addr) const;

	uint32_t pc_of(const Sh4Op *op) const { return m_blk->pc + 2u * op->idx; }

	/* Leaves the block after an instruction that may have changed an
	 * interrupt, the cycles left or the run state: the run loop looks at
	 * them before a block. A delay slot goes on to the end that follows
	 * it, where the branch is completed. */
	void stop_after(const Sh4Op *op)
	{
		if (op->flags & OP_SLOT) {
			return op[1].run(*this, op + 1);
		}
		m_r.pc = pc_of(op) + 2;
		m_remaining += m_blk->count - op->idx - 1;
	}

	/* At the end of a block that nothing disturbed: straight into the
	 * block at `pc` when it is kept, was compared in this epoch and fits
	 * in the slice, unless the search for waits has something to do
	 * here. Otherwise back to the run loop. The calls only nest when the
	 * compiler does not turn them into jumps, to the length of a slice. */
	void chain(uint32_t pc)
	{
		Block &b = m_near[(pc >> 1) & (NEAR_SIZE - 1)];

		if (b.pc != pc || b.checked != m_epoch || b.count > m_remaining) [[unlikely]] {
			return;
		}
		if (pc == m_wait.regs.pc) {
			/* A loop that works is looked at less and less often. */
			if (m_wait.pass == 0) {
				return;
			}
			m_wait.pass--;
		} else {
			if (m_wait.countdown <= 1) {
				return;
			}
			m_wait.countdown--;
		}
		m_blk = &b;
		m_remaining -= b.count;
		return b.ops->run(*this, b.ops);
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

	const Block *m_blk;	/* the running block */

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
	/* Performance counters, 48 bits each */
	bool m_pm_running[2];
	uint64_t m_pm_counter[2];

	uint64_t m_instructions, m_skipped;
};

} /* namespace odin */

#endif /* ODIN_SH4_HPP */
