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

/* SH-4: the run loop, the decoded blocks, the interrupts and the accesses
 * outside plain memory. */

#include "odin/sh4.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdio>
#include <cstdlib>

namespace odin {

namespace {

constexpr uint32_t PHYSICAL = 0x1FFFFFFF;	/* the 29 address lines */
constexpr uint32_t CCN_INTEVT = 0x000028;
constexpr uint32_t INTC_ICR = 0xD00000;
constexpr uint32_t DMAC_DMAOR = 0xA00040;

uint16_t load16(const uint8_t *p)
{
	uint16_t v;
	std::memcpy(&v, p, 2);
	return v;
}

/* Table 19.5: where the handler is from VBR, and the code for INTEVT. The
 * priority of an external source is fixed, the one of an on-chip module
 * comes from IPRA-IPRC and is the highest until they are written.
 * HEURISTIC: IRQ10 has the code of IRQ11 here, 0x340 in the manual. */
constexpr Sh4IrqSource SOURCES[Sh4Source::COUNT] = {
	{0, 16, 0x600, 0x1C0},
	{0, 15, 0x600, 0x200}, {0, 14, 0x600, 0x220}, {0, 13, 0x600, 0x240}, {0, 12, 0x600, 0x260},
	{0, 11, 0x600, 0x280}, {0, 10, 0x600, 0x2A0}, {0, 9, 0x600, 0x2C0}, {0, 8, 0x600, 0x2E0},
	{0, 7, 0x600, 0x300}, {0, 6, 0x600, 0x320}, {0, 5, 0x600, 0x360}, {0, 4, 0x600, 0x360},
	{0, 3, 0x600, 0x380}, {0, 2, 0x600, 0x3A0}, {0, 1, 0x600, 0x3C0},
	{0, 13, 0x600, 0x240}, {0, 10, 0x600, 0x2A0}, {0, 7, 0x600, 0x300}, {0, 4, 0x600, 0x360},
	{0, 0xFFFFFFFF, 0x600, 0x600}, {0, 0xFFFFFFFF, 0x600, 0x620},
	{0, 0xFFFFFFFF, 0x600, 0x640}, {0, 0xFFFFFFFF, 0x600, 0x660}, {0, 0xFFFFFFFF, 0x600, 0x680},
	{0, 0xFFFFFFFF, 0x600, 0x6A0}, {0, 0xFFFFFFFF, 0x600, 0x6C0},
	{0, 0xFFFFFFFF, 0x600, 0x400}, {0, 0xFFFFFFFF, 0x600, 0x420}, {0, 0xFFFFFFFF, 0x600, 0x440},
	{0, 0xFFFFFFFF, 0x600, 0x460},
	{0, 0xFFFFFFFF, 0x600, 0x480}, {0, 0xFFFFFFFF, 0x600, 0x4A0}, {0, 0xFFFFFFFF, 0x600, 0x4C0},
	{0, 0xFFFFFFFF, 0x600, 0x4E0}, {0, 0xFFFFFFFF, 0x600, 0x500}, {0, 0xFFFFFFFF, 0x600, 0x520},
	{0, 0xFFFFFFFF, 0x600, 0x540},
	{0, 0xFFFFFFFF, 0x600, 0x700}, {0, 0xFFFFFFFF, 0x600, 0x720}, {0, 0xFFFFFFFF, 0x600, 0x740},
	{0, 0xFFFFFFFF, 0x600, 0x760},
	{0, 0xFFFFFFFF, 0x600, 0x560}, {0, 0xFFFFFFFF, 0x600, 0x580}, {0, 0xFFFFFFFF, 0x600, 0x5A0},
};

} /* namespace */

Sh4::Sh4(Sh4Bus &bus, std::span<uint8_t> onchip, bool master, bool little_endian,
         uint32_t clock_mode)
	: m_bus(bus), m_onchip(onchip), m_master(master), m_little_endian(little_endian),
	  m_r{}, m_run(Sh4Run::Stopped), m_remaining(0), m_over(0),
	  m_blk(nullptr), m_resumed(false), m_epoch(1), m_code_epoch(PHYSICAL_PAGES, 0),
	  m_rd(1u << (32 - PAGE_BITS), nullptr), m_wr(1u << (32 - PAGE_BITS), nullptr),
	  m_quiet(PHYSICAL_PAGES, 0),
	  m_page_of(1u << (32 - PAGE_BITS), 0),
	  m_near(NEAR_SIZE, Block{1, 0, 0, nullptr, nullptr, nullptr, 0, 0}),
	  m_translate(false), m_hook_on(false), m_hook_addr(0),
	  m_wait{}, m_touched(false), m_event(false),
	  m_intc{}, m_dmac_running{}, m_tmu_running{}, m_tmu_counter{},
	  m_cycles(0), m_at(0), m_tmu_time(0), m_slice(0), m_retime(false), m_clock_mode(clock_mode),
	  m_pm_running{}, m_pm_counter{},
	  m_instructions(0), m_skipped(0), m_decoded(0), m_dropped(0)
{
	if (m_onchip.size() != 0x10000) {
		fatal("on-chip register image of the wrong size", static_cast<uint32_t>(m_onchip.size()));
	}
	m_ops.reserve(OPS_MAX);
	m_words.reserve(OPS_MAX);
	reset(true);
}

void Sh4::fatal(const char *what, uint32_t value) const
{
	std::fprintf(stderr, "FATAL: sh4 %c pc %08X pr %08X sr %08X: %s (%08X)\n",
	             m_master ? 'M' : 'S', m_r.pc, m_r.pr, m_r.sr | m_r.t, what, value);
	std::abort();
}

void Sh4::reset(bool hard)
{
	m_run = m_master ? Sh4Run::Running : Sh4Run::Stopped;

	m_r = {};
	m_r.pc = 0xA0000000;
	m_r.sr = SR_MD | SR_RB | SR_BL | 0xF0;
	m_r.fpscr = 0x00040001;	/* DN set, round to zero */

	onchip_reset(hard);

	m_intc.pending = false;
	m_intc.index = -1;
	std::copy(std::begin(SOURCES), std::end(SOURCES), std::begin(m_intc.source));

	std::fill(std::begin(m_dmac_running), std::end(m_dmac_running), false);
	std::fill(std::begin(m_tmu_running), std::end(m_tmu_running), false);
	std::fill(std::begin(m_tmu_counter), std::end(m_tmu_counter), 0xFFFFFFFF);
	m_cycles = m_at = m_tmu_time = 0;
	m_over = 0;
	std::fill(std::begin(m_pm_running), std::end(m_pm_running), false);
	std::fill(std::begin(m_pm_counter), std::end(m_pm_counter), 0);
}

/* Notes that the host page is mapped at this physical page. If it already
 * is at another one, neither is written in place any more. */
void Sh4::share_page(const uint8_t *host, uint32_t page)
{
	auto [at, fresh] = m_mapped.try_emplace(host, page);

	if (fresh || at->second == page) {
		return;
	}
	for (uint32_t mirror = 0; mirror < 7; mirror++) {
		m_wr[mirror * PHYSICAL_PAGES + page] = nullptr;
		m_wr[mirror * PHYSICAL_PAGES + at->second] = nullptr;
	}
}

/* The page is seen through every mirror of the physical memory below
 * E0000000. Nothing is mapped where the on-chip modules are. */
void Sh4::map_page(uint32_t page, const uint8_t *read, uint8_t *write)
{
	if (page >= PHYSICAL_PAGES || (page >> (24 - PAGE_BITS)) == 0x1F) {
		return;
	}
	for (uint32_t mirror = 0; mirror < 7; mirror++) {
		m_rd[mirror * PHYSICAL_PAGES + page] = read;
		m_wr[mirror * PHYSICAL_PAGES + page] = write;
	}
	m_quiet[page] = 0;
	if (read) {
		share_page(read, page);
	}
	if (write) {
		share_page(write, page);
	}
	drop_blocks();
}

void Sh4::map_quiet(uint32_t page)
{
	if (page < PHYSICAL_PAGES) {
		m_quiet[page] = 1;
	}
}

void Sh4::set_hook(uint32_t addr)
{
	m_hook_on = true;
	m_hook_addr = addr & PHYSICAL;
	drop_blocks();
}

/* Status register and banks */

void Sh4::swap_r_banks()
{
	for (uint32_t i = 0; i < 8; i++) {
		std::swap(m_r.r[i], m_r.rbank[i]);
	}
}

void Sh4::swap_f_banks()
{
	for (uint32_t i = 0; i < 16; i++) {
		std::swap(m_r.fr[i], m_r.xf[i]);
	}
}

/* Bank 1 of R0-R7 is selected when MD and RB are both set. */
void Sh4::set_sr(uint32_t value)
{
	uint32_t old = m_r.sr;

	value &= SR_MASK;
	m_r.t = value & SR_T;
	m_r.sr = value & ~SR_T;
	if (((old & SR_MD) && (old & SR_RB)) != ((m_r.sr & SR_MD) && (m_r.sr & SR_RB))) {
		swap_r_banks();
	}
	if ((old ^ m_r.sr) & (SR_BL | 0xF0)) {
		update_irqs();
	}
}

void Sh4::set_fpscr(uint32_t value)
{
	uint32_t old = m_r.fpscr;

	m_r.fpscr = value & FPSCR_MASK;
	if ((old ^ m_r.fpscr) & FPSCR_FR) {
		swap_f_banks();
	}
	if ((m_r.fpscr & FPSCR_SZ) && (m_r.fpscr & FPSCR_PR)) {
		fatal("FPSCR with SZ and PR both set", m_r.fpscr);
	}
}

/* Interrupts */

/* Finds the raised source of highest priority that SR.BL lets through.
 * NMI passes BL when ICR.NMIB is set. */
void Sh4::update_irqs()
{
	uint32_t icr = static_cast<uint32_t>(reg_get(2, INTC_ICR));
	uint32_t priority = 0;
	int32_t best = -1;

	m_event = true;
	m_intc.pending = false;
	m_intc.index = -1;

	if (m_intc.source[Sh4Source::NMI].raised) {
		m_intc.pending = !(m_r.sr & SR_BL) || (icr & 0x200);
		if (m_intc.pending) {
			m_intc.index = Sh4Source::NMI;
			return;
		}
	}
	/* The IRL pins are four requests with ICR.IRLM, one encoded level
	 * without (19.2.2). */
	if (!(icr & 0x80)) {
		for (uint32_t i = Sh4Source::IRL0; i <= Sh4Source::IRL3; i++) {
			if (m_intc.source[i].raised) {
				fatal("IRL pin held while ICR.IRLM is clear, not implemented", i);
			}
		}
	}
	if (m_r.sr & SR_BL) {
		return;
	}
	for (uint32_t i = 0; i < Sh4Source::COUNT; i++) {
		if (m_intc.source[i].raised && m_intc.source[i].priority > priority) {
			priority = m_intc.source[i].priority;
			best = static_cast<int32_t>(i);
		}
	}
	if (priority > 0) {
		m_intc.pending = true;
		m_intc.index = best;
	}
}

void Sh4::set_irq(uint32_t source, bool raised)
{
	if (source >= Sh4Source::COUNT || source == Sh4Source::NMI) {
		return;
	}
	m_intc.source[source].raised = raised;
	update_irqs();
}

/* The NMI pin (hardware manual, 19.2.1): ICR.NMIL is its level, and the
 * edge ICR.NMIE names asks for the interrupt, which is kept until taken.
 * It sets DMAOR.NMIF, which stops the DMAC. */
void Sh4::set_nmi(bool high)
{
	const uint32_t icr = static_cast<uint32_t>(reg_get(2, INTC_ICR));
	const bool rising = (icr & 0x100) != 0;

	if (((icr & 0x8000) != 0) == high) {
		return;
	}
	reg_put(2, INTC_ICR, (icr & 0x7FFF) | (high ? 0x8000u : 0u));
	if (high != rising) {
		return;
	}
	m_intc.source[Sh4Source::NMI].raised = 1;
	reg_put(4, DMAC_DMAOR, reg_get(4, DMAC_DMAOR) | 2);
	dmac_update(0);
	dmac_run();
	update_irqs();
}

/* Takes the pending interrupt when its priority is above SR.I. The edge of
 * NMI is used up; any other request lasts as long as its source holds it
 * (hardware manual, 19.2.2 for the IRL pins, and the flags of the on-chip
 * modules). */
void Sh4::take_irq()
{
	Sh4IrqSource &source = m_intc.source[m_intc.index];

	if (source.priority <= ((m_r.sr >> 4) & 15)) {
		return;
	}
	m_touched = true;

	m_r.spc = m_r.pc;
	m_r.ssr = m_r.sr | m_r.t;
	m_r.sgr = m_r.r[15];
	m_r.pc = m_r.vbr + source.offset;
	set_sr(m_r.sr | m_r.t | SR_BL | SR_MD | SR_RB);
	reg_put(4, CCN_INTEVT, source.code);
	if (&source == &m_intc.source[Sh4Source::NMI]) {
		source.raised = 0;
	}
	update_irqs();
}

/* Accesses outside plain memory */

/* The machine is about to be called: it may raise an interrupt, write the
 * memory, stop the CPU. Where it promised not to write the memory, see
 * map_quiet(), what it raises or stops is told by what it calls. */
void Sh4::outside()
{
	m_event = true;
	new_epoch();
}

bool Sh4::read_any(uint32_t addr, uint32_t size, uint64_t &value)
{
	uint32_t top = addr >> 24;

	if (top == 0x1F || top == 0xFF) {
		return onchip_read(addr, size, value);
	}
	if (addr >= 0xE0000000) {
		if (addr <= 0xE3FFFFFF) {
			uint32_t target = store_queue_addr(addr) | (addr & 0x1F);
			bool ok = read_any(target, size, value);
			if (!ok) {
				outside();
				m_bus.unanswered(false, target, size, 0);
			}
			return ok;
		}
		/* The cache and TLB arrays read as zero. */
		if (addr >= 0xF0000000 && addr < 0xF8000000) {
			value = 0;
			return true;
		}
	}
	if (!m_quiet[(addr & PHYSICAL) >> PAGE_BITS]) {
		outside();
	}
	return m_bus.read(addr & PHYSICAL, size, value);
}

/* A store that reaches plain memory by another address than its own, as
 * through a store queue: in place, as write() does it. False, and nothing
 * stored, when the page is not written in place or holds code compared in
 * this epoch: the store then goes the way of the bus, and the block is
 * left. */
bool Sh4::write_plain(uint32_t addr, uint32_t size, uint64_t value)
{
	uint8_t *p = m_wr[addr >> PAGE_BITS];

	if (!p || (addr & (size - 1)) || m_code_epoch[(addr >> PAGE_BITS) & (PHYSICAL_PAGES - 1)] == m_epoch) {
		return false;
	}
	p += addr & (PAGE_SIZE - 1);
	const auto put = [p](auto v) { std::memcpy(p, &v, sizeof(v)); };
	switch (size) {
	case 1:
		put(static_cast<uint8_t>(value));
		break;
	case 2:
		put(static_cast<uint16_t>(value));
		break;
	case 4:
		put(static_cast<uint32_t>(value));
		break;
	default:
		put(value);
		break;
	}
	return true;
}

bool Sh4::write_any(uint32_t addr, uint32_t size, uint64_t value)
{
	uint32_t top = addr >> 24;

	if (top == 0x1F || top == 0xFF) {
		return onchip_write(addr, size, value);
	}
	if (addr >= 0xE0000000) {
		if (addr >= 0xF0000000 && addr < 0xF8000000) {
			fatal("write to the cache or TLB arrays", addr);
		}
		/* HEURISTIC: a store queue write goes to its target at once;
		 * the chip sends the 32 bytes of the queue on PREF. */
		if (addr <= 0xE3FFFFFF) {
			uint32_t target = store_queue_addr(addr) | (addr & 0x1F);
			if (write_plain(target, size, value)) {
				return true;
			}
			bool ok = write_any(target, size, value);
			if (!ok) {
				outside();
				m_bus.unanswered(true, target, size, value);
			}
			return ok;
		}
	}
	if (!m_quiet[(addr & PHYSICAL) >> PAGE_BITS]) {
		outside();
	}
	return m_bus.write(addr & PHYSICAL, size, value);
}

/* True when the program may go on in its block: an on-chip module
 * answered, and neither an interrupt nor the machine came into it. The
 * run loop clears m_event before a block. */
bool Sh4::read_slow(const Sh4Op *op, uint32_t addr, uint32_t size, uint64_t &value)
{
	/* The machine may look at PC while the bus answers. In a delay slot
	 * it holds the target of the branch. */
	if (!(op->flags & OP_SLOT)) {
		m_r.pc = pc_of(op);
	}
	m_touched = true;
	m_at = time_at(op);
	if (!read_any(addr, size, value)) {
		value = 0;
		outside();
		m_bus.unanswered(false, addr, size, 0);
	}
	return !m_event;
}

bool Sh4::write_slow(const Sh4Op *op, uint32_t addr, uint32_t size, uint64_t value)
{
	if (!(op->flags & OP_SLOT)) {
		m_r.pc = pc_of(op);
	}
	m_touched = true;
	m_at = time_at(op);
	if (!write_any(addr, size, value)) {
		outside();
		m_bus.unanswered(true, addr, size, value);
	}
	return !m_event;
}

/* Blocks */

/* The count of epochs is back at zero: nothing is held as compared. */
void Sh4::epoch_wrapped()
{
	for (Block &b : m_blocks) {
		b.checked = 0;
	}
	for (Block &b : m_near) {
		b.checked = 0;
	}
	std::fill(m_code_epoch.begin(), m_code_epoch.end(), 0);
	m_epoch = 1;
}

/* A store of the program fell on a page of code compared in this epoch.
 * True when it fell on the running block, which must then be left. */
bool Sh4::wrote_code(const uint8_t *p, size_t size)
{
	const Block &b = *m_blk;

	new_epoch();
	guard_code(b);
	return b.code && p + size > b.code && p < b.code + b.words * 2u;
}

/* From now on a store on the pages of this block starts a new epoch. */
void Sh4::guard_code(const Block &b)
{
	if (b.code) {
		m_code_epoch[(b.pc >> PAGE_BITS) & (PHYSICAL_PAGES - 1)] = m_epoch;
		m_code_epoch[((b.pc + b.words * 2u - 1) >> PAGE_BITS) & (PHYSICAL_PAGES - 1)] = m_epoch;
	}
}

bool Sh4::same_code(Block &b)
{
	const uint8_t *now = b.code;
	const uint16_t *was = b.copy;
	uint32_t left = b.words;

	for (; left >= 4; left -= 4, now += 8, was += 4) {
		uint64_t x, y;
		std::memcpy(&x, now, 8);
		std::memcpy(&y, was, 8);
		if (x != y) {
			return false;
		}
	}
	for (; left > 0; left--, now += 2, was++) {
		if (load16(now) != *was) {
			return false;
		}
	}
	b.checked = m_epoch;
	guard_code(b);
	return true;
}

void Sh4::drop_blocks()
{
	if (m_blocks.empty()) {
		return;
	}
	m_blocks.clear();
	m_ops.clear();
	m_words.clear();
	m_code_pages.clear();
	std::fill(m_page_of.begin(), m_page_of.end(), 0);
	std::fill(m_code_epoch.begin(), m_code_epoch.end(), 0);
	if (m_host) {
		m_host->clear();
	}
	/* An odd PC: no block is kept for one. */
	std::fill(m_near.begin(), m_near.end(), Block{1, 0, 0, nullptr, nullptr, nullptr, 0, 0});
}

/* What may not be in a delay slot: a branch, a PC-relative instruction
 * (software manual, 5.6.2) and, here, the hooked instruction. */
bool Sh4::bad_slot(Kind kind, uint32_t at) const
{
	return kind == Kind::PcRelative || kind == Kind::Branch || kind == Kind::BranchDelayed
		|| kind == Kind::Jump || (m_hook_on && (at & PHYSICAL) == m_hook_addr);
}

/* Decodes the block that starts at `pc`, `limit` instructions at most, at
 * the end of `ops`. It runs to a jump (whose delay slot comes on top of the
 * limit), to the end of the page or to a hooked address, which always
 * starts a block, and goes on behind a conditional branch. False when the
 * code is not in plain memory. */
bool Sh4::decode_block(uint32_t pc, uint32_t limit, Block &b, std::vector<Sh4Op> &ops)
{
	const uint8_t *page = m_rd[(pc & PHYSICAL) >> PAGE_BITS];
	std::array<Sh4Op, BLOCK_MAX + 1> body, slots;
	uint32_t count = 0, slot_count = 0, words = 0;
	bool jumped = false;

	if (!page || (pc & 1)) {
		return false;
	}
	const uint8_t *code = page + (pc & (PAGE_SIZE - 1));
	/* Instructions up to the end of the page, and one more when the host
	 * memory goes on with the next page. */
	uint32_t room = (PAGE_SIZE - (pc & (PAGE_SIZE - 1))) / 2;
	uint32_t next_page = ((pc & PHYSICAL) >> PAGE_BITS) + 1;
	bool more = next_page < ((PHYSICAL + 1) >> PAGE_BITS) && m_rd[next_page] == page + PAGE_SIZE;
	auto hooked = [&](uint32_t at) { return m_hook_on && (at & PHYSICAL) == m_hook_addr; };

	while (count < limit && count < room) {
		uint32_t at = pc + 2 * count;
		Sh4Op op;
		Kind kind = decode(load16(code + 2 * count), at, op);

		op.idx = static_cast<uint8_t>(count);
		if (count > 0) {
			pair_delay_loop(body[count - 1], at - 2, op);
		}
		if (kind == Kind::Jump || kind == Kind::BranchDelayed) {
			Sh4Op slot;

			if (count + 1 > room || (count + 1 == room && !more)) {
				/* The delay slot is out of reach: the branch runs
				 * alone, through decode_once(). */
				if (count == 0) {
					return false;
				}
				break;
			}
			if (bad_slot(decode(load16(code + 2 * (count + 1)), at + 2, slot), at + 2)) {
				op.flags |= OP_BAD_SLOT;
			}
			slot.idx = static_cast<uint8_t>(count + 1);
			slot.flags |= OP_SLOT;
			words = count + 2;
			if (kind == Kind::Jump) {
				op.flags |= OP_JUMP;
				op.m = 1;
				body[count++] = op;
				body[count++] = slot;
				jumped = true;
				break;
			}
			/* Taken, the branch runs a copy of its delay slot kept
			 * behind the block; not taken, the block goes on. */
			op.flags |= OP_DELAYED;
			op.m = static_cast<uint8_t>(slot_count);
			slots[slot_count++] = slot;
		}
		if (kind == Kind::Branch) {
			op.flags |= OP_BRANCH;
		}
		body[count++] = op;
		if (kind == Kind::End || hooked(at + 2)) {
			break;
		}
	}

	size_t first = ops.size();

	b.pc = pc;
	b.count = static_cast<uint16_t>(count);
	b.words = static_cast<uint16_t>(std::max(words, count));
	b.code = code;
	b.copy = nullptr;
	b.checked = 0;
	b.host = 0;

	/* After the slot of a jump nothing is left to give back; after the
	 * copy of the slot of a conditional branch, what follows the branch
	 * in the block, less the slot itself. */
	Sh4Op end{};
	end.run = op_leave;
	end.idx = static_cast<uint8_t>(count);
	ops.insert(ops.end(), body.begin(), body.begin() + count);
	ops.push_back(end);
	if (!jumped) {
		ops.back().run = op_next;
		ops.back().imm = pc + 2 * count;
	}
	for (uint32_t k = 0; k < slot_count; k++) {
		uint32_t branch = slots[k].idx - 1u;
		ops[first + branch].m = static_cast<uint8_t>(count + 1 + 2 * k - branch);
		ops.push_back(slots[k]);
		end.idx = slots[k].idx;
		end.imm = count - branch - 2;
		ops.push_back(end);
	}
	b.ops = &ops[first];
	return true;
}

/* The instruction at `pc`, read through the bus: for code that is not in
 * plain memory. */
uint16_t Sh4::fetch(uint32_t pc)
{
	uint64_t value = 0;

	m_touched = true;
	outside();
	if (!m_bus.read(pc & PHYSICAL, 2, value)) {
		fatal("instruction fetch nothing answers", pc);
	}
	return static_cast<uint16_t>(value);
}

/* Runs one instruction, with its delay slot when the branch is taken,
 * decoded for this use only: what the bus gives is not kept, and is read
 * as often as the program runs it. */
void Sh4::decode_once(uint32_t pc)
{
	Sh4Op op, end{};
	Block b{pc, 1, 0, nullptr, nullptr, nullptr, 0, 0};
	Kind kind = decode(fetch(pc), pc, op);

	m_short_ops.clear();
	op.idx = 0;
	if (kind == Kind::Jump || (kind == Kind::BranchDelayed && taken(op, m_r.t))) {
		Sh4Op slot;
		if (bad_slot(decode(fetch(pc + 2), pc + 2, slot), pc + 2)) {
			op.flags |= OP_BAD_SLOT;
		}
		slot.idx = 1;
		slot.flags |= OP_SLOT;
		op.m = 1;
		m_short_ops.push_back(op);
		m_short_ops.push_back(slot);
		/* The two instructions are the whole block. */
		b.count = 2;
		end.run = op_leave;
		end.idx = 2;
		m_short_ops.push_back(end);
	} else {
		end.run = op_next;
		end.imm = pc + 2;
		end.idx = 1;
		m_short_ops.push_back(op);
		m_short_ops.push_back(end);
	}
	b.ops = m_short_ops.data();
	run_block(b);
}

/* Runs the block from its instruction `from` on. */
void Sh4::run_block(const Block &b, uint32_t from)
{
	const Sh4Op *ops = b.ops + from;

	m_blk = &b;
	/* The cycles of all that is left of the block; an instruction that
	 * leaves it gives back those of the instructions not run. */
	m_remaining -= static_cast<int32_t>(b.count - from);
	/* Host code is entered at the start of a block only. */
	if (b.host && from == 0) {
		return m_host->run(*this, b.host);
	}
	ops->run(*this, ops);
}

/* The end put in a kept block where a slice ends, see run_cut(): the
 * instructions from there on did not run, and the program resumes there. */
void Sh4::op_cut(Sh4 &c, const Sh4Op *op)
{
	c.m_r.pc = op->imm;
	c.m_remaining += c.m_blk->count - op->idx;
}

/* Runs the `limit` instructions the slice has cycles for of a kept block,
 * from its instruction `from`, read: an end takes the place of the next
 * one for the time of the run. A jump is not cut from its delay slot: the
 * block then runs to its end, a cycle over. False, and nothing run, when
 * the code is no longer what was decoded. */
bool Sh4::run_cut(Block &b, uint32_t from, uint32_t limit)
{
	const uint32_t last = from + limit;
	Sh4Op *const ops = m_ops.data() + (b.ops - m_ops.data());

	if (b.checked != m_epoch && !same_code(b)) {
		return false;
	}
	m_resumed = true;
	m_blk = &b;
	m_remaining -= static_cast<int32_t>(b.count - from);
	if (last >= b.count || (ops[last].flags & OP_SLOT)) {
		ops[from].run(*this, ops + from);
		return true;
	}

	const Sh4Op kept = ops[last];

	ops[last] = Sh4Op{op_cut, b.pc + 2 * last, 0, 0, static_cast<uint8_t>(last), 0};
	ops[from].run(*this, ops + from);
	ops[last] = kept;
	return true;
}

/* The block the program is chained to was not compared in this epoch: it is
 * now, and taken as chain() or link() would have, when its code is still
 * what was decoded and the slice has the cycles for it. */
void Sh4::chain_stale(uint32_t pc)
{
	Block &b = m_near[(pc >> 1) & (NEAR_SIZE - 1)];

	if (b.count > m_remaining || !same_code(b)) {
		return;
	}
	return chain(pc);
}

uintptr_t Sh4::link_stale(uint32_t pc)
{
	Block &b = m_near[(pc >> 1) & (NEAR_SIZE - 1)];

	if (b.count > m_remaining || !same_code(b)) {
		return 1;
	}
	return link(pc);
}

/* Host code */

void Sh4::set_translate(bool on)
{
	if (on && !m_host) {
		const auto from_cpu = [this](const void *at) {
			return static_cast<int32_t>(static_cast<const uint8_t *>(at) - reinterpret_cast<const uint8_t *>(this));
		};
		const Sh4Host::Memory memory{m_rd.data(), m_wr.data(), m_code_epoch.data(), PAGE_BITS, PHYSICAL_PAGES,
		                             from_cpu(&m_epoch), from_cpu(&m_touched)};

		const Sh4Host::Places places{from_cpu(&m_remaining)};

		m_host = std::make_unique<Sh4Host>(m_r.r, memory, places);
	}
	m_translate = on && m_host->usable();
	drop_blocks();
}

/* The steps of host code that do what the instruction does, on the
 * registers of the CPU, which stay where they are: no step leaves a value
 * of the program in a register of the host. False, and nothing written,
 * for an instruction that has no such steps. */
bool Sh4::translate_inline(Inline form, const Sh4Op &op)
{
	using Alu = Sh4Host::Alu;
	using Shift = Sh4Host::Shift;
	using If = Sh4Host::If;
	static_assert(offsetof(Regs, r) == 0);
	constexpr uint32_t T = offsetof(Regs, t) / 4;
	constexpr Sh4Host::Reg A = Sh4Host::Reg::A;
	Sh4Host &host = *m_host;
	const uint32_t n = op.n, m = op.m;
	/* Rn = Rn <alu> Rm */
	const auto both = [&](Alu alu) {
		host.load(A, n);
		host.alu(alu, A, m);
		host.store(n, A);
	};
	/* The register = itself <alu> the immediate */
	const auto with_value = [&](Alu alu, uint32_t reg) {
		host.load(A, reg);
		host.alu_value(alu, A, op.imm);
		host.store(reg, A);
	};
	/* Rn = Rm <alu> a value */
	const auto from = [&](Alu alu, uint32_t value) {
		host.load(A, m);
		host.alu_value(alu, A, value);
		host.store(n, A);
	};
	/* T = Rn <found> Rm, or <found> a value */
	const auto compare = [&](If found) {
		host.load(A, n);
		host.alu(Alu::Compare, A, m);
		host.flag(found, T);
	};
	const auto compare_value = [&](uint32_t reg, If found, uint32_t value) {
		host.load(A, reg);
		host.alu_value(Alu::Compare, A, value);
		host.flag(found, T);
	};
	/* Rn = Rm with its low bits extended by their sign */
	const auto extend = [&](uint32_t bits) {
		host.load(A, m);
		host.shift(Shift::Left, A, 32 - bits);
		host.shift(Shift::RightSigned, A, 32 - bits);
		host.store(n, A);
	};
	const auto shift = [&](Shift kind, uint32_t count) {
		host.load(A, n);
		host.shift(kind, A, count);
		host.store(n, A);
	};
	/* The same by one bit, T taking the bit that leaves. */
	const auto shift_out = [&](Shift kind) {
		host.load(A, n);
		if (kind == Shift::Left) {
			host.shift(Shift::Right, A, 31);
		} else {
			host.alu_value(Alu::And, A, 1);
		}
		host.store(T, A);
		shift(kind, 1);
	};

	switch (form) {
	case Inline::No:
		return false;
	case Inline::Mov:
		host.load(A, m);
		host.store(n, A);
		break;
	case Inline::MovI:
		host.store_value(n, op.imm);
		break;
	case Inline::MovA:
		host.store_value(0, op.imm);
		break;
	case Inline::MovT:
		host.load(A, T);
		host.store(n, A);
		break;
	case Inline::Add:
		both(Alu::Add);
		break;
	case Inline::AddI:
		with_value(Alu::Add, n);
		break;
	case Inline::Sub:
		both(Alu::Sub);
		break;
	case Inline::Neg:
		/* 0 - Rm: every bit turned over, plus one */
		host.load(A, m);
		host.alu_value(Alu::Xor, A, 0xFFFFFFFF);
		host.alu_value(Alu::Add, A, 1);
		host.store(n, A);
		break;
	case Inline::And:
		both(Alu::And);
		break;
	case Inline::AndI:
		with_value(Alu::And, 0);
		break;
	case Inline::Or:
		both(Alu::Or);
		break;
	case Inline::OrI:
		with_value(Alu::Or, 0);
		break;
	case Inline::Xor:
		both(Alu::Xor);
		break;
	case Inline::XorI:
		with_value(Alu::Xor, 0);
		break;
	case Inline::Not:
		from(Alu::Xor, 0xFFFFFFFF);
		break;
	case Inline::Tst:
		host.load(A, n);
		host.alu(Alu::And, A, m);
		host.flag(If::Equal, T);
		break;
	case Inline::TstI:
		host.load(A, 0);
		host.alu_value(Alu::And, A, op.imm);
		host.flag(If::Equal, T);
		break;
	case Inline::CmpEq:
		compare(If::Equal);
		break;
	case Inline::CmpIm:
		compare_value(0, If::Equal, op.imm);
		break;
	case Inline::CmpHs:
		compare(If::AtLeast);
		break;
	case Inline::CmpGe:
		compare(If::AtLeastSigned);
		break;
	case Inline::CmpHi:
		compare(If::Above);
		break;
	case Inline::CmpGt:
		compare(If::AboveSigned);
		break;
	case Inline::CmpPz:
		compare_value(n, If::AtLeastSigned, 0);
		break;
	case Inline::CmpPl:
		compare_value(n, If::AboveSigned, 0);
		break;
	case Inline::ExtSB:
		extend(8);
		break;
	case Inline::ExtSW:
		extend(16);
		break;
	case Inline::ExtUB:
		from(Alu::And, 0xFF);
		break;
	case Inline::ExtUW:
		from(Alu::And, 0xFFFF);
		break;
	case Inline::ShLL:
		shift_out(Shift::Left);
		break;
	case Inline::ShLR:
		shift_out(Shift::Right);
		break;
	case Inline::ShAR:
		shift_out(Shift::RightSigned);
		break;
	case Inline::ShLL2:
		shift(Shift::Left, 2);
		break;
	case Inline::ShLL8:
		shift(Shift::Left, 8);
		break;
	case Inline::ShLL16:
		shift(Shift::Left, 16);
		break;
	case Inline::ShLR2:
		shift(Shift::Right, 2);
		break;
	case Inline::ShLR8:
		shift(Shift::Right, 8);
		break;
	case Inline::ShLR16:
		shift(Shift::Right, 16);
		break;
	case Inline::Dt:
		host.load(A, n);
		host.alu_value(Alu::Sub, A, 1);
		host.store(n, A);
		host.flag(If::Equal, T);
		break;
	case Inline::ClrT:
		host.store_value(T, 0);
		break;
	case Inline::SetT:
		host.store_value(T, 1);
		break;
	case Inline::Nop:
		break;
	default:
		return false;
	}
	return true;
}

/* An access to the memory, the instruction `i` of the block: in place when
 * the memory is plain, else the long way, a call of the instruction written
 * behind the block. False, and nothing written, for an instruction that is
 * not such an access, or a read relative to PC from memory that is not
 * plain: that one is called. */
bool Sh4::translate_access(Inline form, const Sh4Op &op, uint32_t i, std::vector<LongWay> &ways)
{
	using Alu = Sh4Host::Alu;
	constexpr uint32_t PR = offsetof(Regs, pr) / 4, GBR = offsetof(Regs, gbr) / 4, NONE = ~0u;
	constexpr uint32_t FR = offsetof(Regs, fr) / 4, XF = offsetof(Regs, xf) / 4, FPSCR = offsetof(Regs, fpscr) / 4;
	constexpr Sh4Host::Reg A = Sh4Host::Reg::A;
	Sh4Host &host = *m_host;
	const uint32_t n = op.n, m = op.m;
	/* The address, the word `base` + `disp` + the word `index`; `step`
	 * added to `base` after a read into another word or after a write. A
	 * move of the FPU is written for the FPSCR.SZ the block is seen to
	 * have there, and goes the long way when it has the other. */
	struct Access {
		uint32_t size;
		uint32_t base, disp, index;
		uint32_t reg;
		int32_t step;
		bool fpu;
	};
	const auto address = [&](const Access &a, Sh4Host::Slow &slow) {
		if (a.fpu) {
			slow.jumps[slow.count++] = host.jump_if_bits(FPSCR, FPSCR_SZ, !m_pairs);
		}
		host.load(A, a.base);
		if (a.index != NONE) {
			host.alu(Alu::Add, A, a.index);
		}
		if (a.disp) {
			host.alu_value(Alu::Add, A, a.disp);
		}
	};
	const auto read = [&](const Access &a) {
		Sh4Host::Slow slow{};

		address(a, slow);
		host.read(a.size, slow);
		if (a.size == 8) {
			host.store_pair(a.reg);
		} else {
			host.store(a.reg, A);
		}
		if (a.step && a.reg != a.base) {
			host.load(A, a.base);
			host.alu_value(Alu::Add, A, static_cast<uint32_t>(a.step));
			host.store(a.base, A);
		}
		ways.push_back(LongWay{slow, i, host.here()});
	};
	const auto write = [&](const Access &a) {
		Sh4Host::Slow slow{};

		address(a, slow);
		host.write(a.size, a.reg, slow);
		if (a.step) {
			host.load(A, a.base);
			host.alu_value(Alu::Add, A, static_cast<uint32_t>(a.step));
			host.store(a.base, A);
		}
		ways.push_back(LongWay{slow, i, host.here()});
	};
	/* A read relative to PC: the page is known now, the value is read
	 * when the block runs. */
	const auto read_pc = [&](uint32_t size) {
		const uint8_t *page = m_rd[op.imm >> PAGE_BITS];

		if (!page || (op.imm & (size - 1))) {
			return false;
		}
		host.read_at(page + (op.imm & (PAGE_SIZE - 1)), size);
		host.store(n, A);
		return true;
	};
	const auto pre = [](uint32_t size) { return static_cast<uint32_t>(-static_cast<int32_t>(size)); };
	/* FRn, or with SZ the pair: DRn, or XDn for an odd n */
	const uint32_t size = m_pairs ? 8 : 4;
	const auto fpu = [this](uint32_t k) { return !m_pairs ? FR + k : (k & 1) ? XF + (k & 14) : FR + (k & 14); };

	switch (form) {
	case Inline::MovLL: read({4, m, 0, NONE, n, 0, false}); break;
	case Inline::MovWL: read({2, m, 0, NONE, n, 0, false}); break;
	case Inline::MovBL: read({1, m, 0, NONE, n, 0, false}); break;
	case Inline::MovLL4: read({4, m, op.imm, NONE, n, 0, false}); break;
	case Inline::MovWL4: read({2, m, op.imm, NONE, 0, 0, false}); break;
	case Inline::MovBL4: read({1, m, op.imm, NONE, 0, 0, false}); break;
	case Inline::MovLL0: read({4, m, 0, 0, n, 0, false}); break;
	case Inline::MovWL0: read({2, m, 0, 0, n, 0, false}); break;
	case Inline::MovBL0: read({1, m, 0, 0, n, 0, false}); break;
	case Inline::MovLP: read({4, m, 0, NONE, n, 4, false}); break;
	case Inline::MovWP: read({2, m, 0, NONE, n, 2, false}); break;
	case Inline::MovBP: read({1, m, 0, NONE, n, 1, false}); break;
	case Inline::MovLLG: read({4, GBR, op.imm, NONE, 0, 0, false}); break;
	case Inline::MovWLG: read({2, GBR, op.imm, NONE, 0, 0, false}); break;
	case Inline::MovBLG: read({1, GBR, op.imm, NONE, 0, 0, false}); break;
	case Inline::MovLI: return read_pc(4);
	case Inline::MovWI: return read_pc(2);
	case Inline::LdsMPr: read({4, n, 0, NONE, PR, 4, false}); break;
	case Inline::MovLS: write({4, n, 0, NONE, m, 0, false}); break;
	case Inline::MovWS: write({2, n, 0, NONE, m, 0, false}); break;
	case Inline::MovBS: write({1, n, 0, NONE, m, 0, false}); break;
	case Inline::MovLS4: write({4, n, op.imm, NONE, m, 0, false}); break;
	case Inline::MovWS4: write({2, m, op.imm, NONE, 0, 0, false}); break;
	case Inline::MovBS4: write({1, m, op.imm, NONE, 0, 0, false}); break;
	case Inline::MovLS0: write({4, n, 0, 0, m, 0, false}); break;
	case Inline::MovWS0: write({2, n, 0, 0, m, 0, false}); break;
	case Inline::MovBS0: write({1, n, 0, 0, m, 0, false}); break;
	case Inline::MovLM: write({4, n, pre(4), NONE, m, -4, false}); break;
	case Inline::MovWM: write({2, n, pre(2), NONE, m, -2, false}); break;
	case Inline::MovBM: write({1, n, pre(1), NONE, m, -1, false}); break;
	case Inline::MovLSG: write({4, GBR, op.imm, NONE, 0, 0, false}); break;
	case Inline::MovWSG: write({2, GBR, op.imm, NONE, 0, 0, false}); break;
	case Inline::MovBSG: write({1, GBR, op.imm, NONE, 0, 0, false}); break;
	case Inline::StsMPr: write({4, n, pre(4), NONE, PR, -4, false}); break;
	case Inline::MovCA: write({4, n, 0, NONE, 0, 0, false}); break;
	case Inline::FMovLoad: read({size, m, 0, NONE, fpu(n), 0, true}); break;
	case Inline::FMovStore: write({size, n, 0, NONE, fpu(m), 0, true}); break;
	case Inline::FMovIndexLoad: read({size, m, 0, 0, fpu(n), 0, true}); break;
	case Inline::FMovIndexStore: write({size, n, 0, 0, fpu(m), 0, true}); break;
	case Inline::FMovRestore: read({size, m, 0, NONE, fpu(n), static_cast<int32_t>(size), true}); break;
	case Inline::FMovSave: write({size, n, pre(size), NONE, fpu(m), -static_cast<int32_t>(size), true}); break;
	case Inline::FMov: {
		/* no access: only the guard and a copy */
		Sh4Host::Slow slow{};

		slow.jumps[slow.count++] = host.jump_if_bits(FPSCR, FPSCR_SZ, !m_pairs);
		if (m_pairs) {
			host.load_pair(fpu(m));
			host.store_pair(fpu(n));
		} else {
			host.load(A, fpu(m));
			host.store(fpu(n), A);
		}
		ways.push_back(LongWay{slow, i, host.here()});
		break;
	}
	default:
		return false;
	}
	return true;
}

/* The block at `pc` for host code, see link(). */
uintptr_t Sh4::linked(Sh4 &c, uint32_t pc)
{
	return c.link(pc);
}

/* The test of a conditional branch in place: a jump, bound later, to where
 * the branch goes. False, and nothing written, for any other instruction or
 * a branch whose delay slot holds what it may not: that one is called. */
bool Sh4::translate_condition(const Sh4Op &op, Sh4Host::Jump &taken)
{
	constexpr uint32_t T = offsetof(Regs, t) / 4;
	const Inline form = inlined(op.run);

	if ((form != Inline::Bt && form != Inline::Bf && form != Inline::BtS && form != Inline::BfS) ||
	    (op.flags & OP_BAD_SLOT)) {
		return false;
	}
	taken = m_host->jump_if_word(T, form == Inline::Bt || form == Inline::BtS);
	return true;
}

/* A branch that always goes, the instruction `i` of the block, in place: PC
 * takes where it goes, PR where it returns to; its delay slot and the end
 * that follows are written after it. False, and nothing written, when the
 * slot holds what it may not or the branch is another one: it is called. */
bool Sh4::translate_jump(const Block &b, const Sh4Op &op, uint32_t i, std::vector<LongWay> &ways)
{
	using Alu = Sh4Host::Alu;
	constexpr uint32_t PC = offsetof(Regs, pc) / 4, PR = offsetof(Regs, pr) / 4;
	constexpr Sh4Host::Reg A = Sh4Host::Reg::A;
	Sh4Host &host = *m_host;
	const uint32_t here = b.pc + 2 * op.idx;

	if (op.flags & OP_BAD_SLOT) {
		return false;
	}
	switch (inlined(op.run)) {
	case Inline::Bra:
		host.store_value(PC, op.imm);
		break;
	case Inline::Bsr:
		host.store_value(PR, here + 4);
		host.store_value(PC, op.imm);
		break;
	case Inline::Rts:
		host.load(A, PR);
		host.store(PC, A);
		break;
	case Inline::Jsr:
		host.load(A, op.n);
		host.store(PC, A);
		host.store_value(PR, here + 4);
		break;
	case Inline::Jmp: {
		/* An odd address: the instruction itself stops the program. */
		Sh4Host::Slow slow{};

		host.load(A, op.n);
		slow.jumps[slow.count++] = host.jump_if_odd();
		host.store(PC, A);
		ways.push_back(LongWay{slow, i, host.here()});
		break;
	}
	case Inline::BraF:
		host.load(A, op.n);
		host.alu_value(Alu::Add, A, here + 4);
		host.store(PC, A);
		break;
	case Inline::BsrF:
		host.load(A, op.n);
		host.alu_value(Alu::Add, A, here + 4);
		host.store(PC, A);
		host.store_value(PR, here + 4);
		break;
	default:
		return false;
	}
	return true;
}

/* The instruction `i` of a block as host code: its steps in place when it
 * has some, and true; else a call, and false: what the call returns is for
 * the block to look at. */
bool Sh4::translate_op(const Sh4Op *ops, uint32_t i, std::vector<LongWay> &ways)
{
	const Inline form = inlined(ops[i].run);

	if (translate_inline(form, ops[i]) || translate_access(form, ops[i], i, ways)) {
		return true;
	}
	m_host->call(called(ops[i].run), i);
	return false;
}

/* FPSCR.SZ where each instruction of the block is, from what it holds at
 * the start: FSCHG turns it over. Any other change (LDS to FPSCR) is left
 * to the guard of each move. */
void Sh4::pairs_through(const Block &b, std::array<bool, BLOCK_MAX + 3> &pairs) const
{
	pairs[0] = (m_r.fpscr & FPSCR_SZ) != 0;
	for (uint32_t i = 0; i < b.count; i++) {
		pairs[i + 1] = pairs[i] != (inlined(b.ops[i].run) == Inline::FSChg);
	}
}

/* Writes the host code of a kept block: its instructions in turn, each in
 * place or called, the block left as soon as a call says so. A conditional
 * branch that goes says where to; one with a delay slot has the copy of its
 * slot and the end that follows it behind the block, as a jump has its own
 * in line. */
void Sh4::translate(Block &b)
{
	struct Taken {
		Sh4Host::Jump jump;
		uint32_t op;
		bool in_place;	/* the branch was not called: what it does is left to do */
	};
	constexpr uint32_t PC = offsetof(Regs, pc) / 4;
	std::array<Taken, BLOCK_MAX + 1> taken;
	uint32_t num_taken = 0;
	const Sh4Op *ops = b.ops;
	Sh4Host &host = *m_host;
	std::vector<LongWay> &ways = m_long_ways;
	std::array<bool, BLOCK_MAX + 3> pairs;
	bool jumped = false;

	ways.clear();
	pairs_through(b, pairs);
	b.host = host.begin(ops);
	for (uint32_t i = 0; i < b.count && !jumped; i++) {
		m_pairs = pairs[i];
		if (ops[i].flags & (OP_BRANCH | OP_DELAYED)) {
			Sh4Host::Jump jump;

			if (translate_condition(ops[i], jump)) {
				taken[num_taken++] = Taken{jump, i, true};
			} else {
				host.call(called(ops[i].run), i);
				taken[num_taken++] = Taken{host.jump_if_told(), i, false};
			}
		} else if (ops[i].flags & OP_JUMP) {
			if (!translate_jump(b, ops[i], i, ways)) {
				host.call(called(ops[i].run), i);
			}
			m_pairs = pairs[i + 1];
			translate_op(ops, i + 1, ways);
			host.call(called(ops[i + 2].run), i + 2);
			host.go_where_told();
			jumped = true;
		} else if (!translate_op(ops, i, ways)) {
			host.loop_if_told();
		}
	}
	/* The end of a block cut for its length, see end_next. */
	if (!jumped) {
		host.store_value(PC, ops[b.count].imm);
		host.call_link(linked, ops[b.count].imm);
		host.go_where_told();
	}
	/* A branch that goes, see branch() and jump(). */
	for (uint32_t k = 0; k < num_taken; k++) {
		const uint32_t i = taken[k].op;

		host.bind(taken[k].jump);
		if (taken[k].in_place) {
			host.store_value(PC, ops[i].imm);
		}
		if (ops[i].flags & OP_DELAYED) {
			const uint32_t slot = i + ops[i].m;

			m_pairs = pairs[i + 1];
			translate_op(ops, slot, ways);
			host.call(called(ops[slot + 1].run), slot + 1);
		} else if (taken[k].in_place) {
			host.add_remaining(static_cast<int32_t>(b.count - i - 1));
			host.call_link(linked, ops[i].imm);
		}
		host.go_where_told();
	}
	/* The long ways: the instruction called, the block left if it says
	 * so, but in a delay slot, where the end that follows sees to it. */
	for (const LongWay &way : ways) {
		host.bind(way.slow);
		host.call(called(ops[way.op].run), way.op);
		if (!(ops[way.op].flags & OP_SLOT)) {
			host.loop_if_told();
		}
		host.jump_back(way.back);
	}
}

/* A kept block serves, from its instruction `from` on, when the slice has
 * the cycles for all that is left of it and the code is still what was
 * decoded. A slice ends after the first instruction that leaves no cycle:
 * a block decoded to BLOCK_MAX never runs past that while BLOCK_MAX cycles
 * are left. */
bool Sh4::fits(Block &b, uint32_t from)
{
	if (static_cast<int32_t>(b.count - from) > m_remaining && m_remaining < static_cast<int32_t>(BLOCK_MAX)) {
		return false;
	}
	return b.checked == m_epoch || same_code(b);
}

/* Runs the block at `pc`. */
void Sh4::run_at(uint32_t pc)
{
	Block &b = m_near[(pc >> 1) & (NEAR_SIZE - 1)];
	const bool resumed = m_resumed;

	m_resumed = false;
	if (b.pc == pc && fits(b, 0)) [[likely]] {
		return run_block(b);
	}
	run_far(pc, resumed);
}

/* The block is looked for in the tables, or decoded now: kept when it is
 * whole, and just as long as the slice when the slice ends in it. Where
 * the program only resumes, the kept block that holds the place serves from
 * there on; a place a branch leads to has a block of its own. */
void Sh4::run_far(uint32_t pc, bool resumed)
{
	uint32_t page = m_page_of[pc >> PAGE_BITS];
	Block &near = m_near[(pc >> 1) & (NEAR_SIZE - 1)];
	uint32_t limit = std::min(BLOCK_MAX, static_cast<uint32_t>(m_remaining));
	Block b;

	if (page && !(pc & 1)) {
		const uint32_t found = m_code_pages[page - 1].block[(pc & (PAGE_SIZE - 1)) >> 1];
		const uint32_t rank = found & ((1u << PLACE_BITS) - 1), from = found >> PLACE_BITS;

		if (rank && (from == 0 || resumed) && fits(m_blocks[rank - 1], from)) {
			/* A hooked address, or the middle of a block, is only
			 * found here: chain() never runs over the hook. */
			if (from || (m_hook_on && (pc & PHYSICAL) == m_hook_addr)) {
				return run_block(m_blocks[rank - 1], from);
			}
			near = m_blocks[rank - 1];
			return run_block(near);
		}
		/* The slice ends in a kept block: it serves up to there. */
		if (rank && limit < BLOCK_MAX && run_cut(m_blocks[rank - 1], from, limit)) {
			return;
		}
	}

	if (limit < BLOCK_MAX) {
		m_short_ops.clear();
		if (!decode_block(pc, limit, b, m_short_ops)) {
			return decode_once(pc);
		}
		guard_code(b);
		/* The slice ends in it: the next one resumes. */
		m_resumed = true;
		return run_block(b);
	}

	/* A block takes less than 4 * BLOCK_MAX places: m_ops never moves. */
	if (m_ops.size() + 4 * BLOCK_MAX > OPS_MAX || (m_translate && !m_host->room())) {
		m_dropped++;
		drop_blocks();
	}
	if (!decode_block(pc, limit, b, m_ops)) {
		return decode_once(pc);
	}
	b.copy = m_words.data() + m_words.size();
	for (uint32_t i = 0; i < b.words; i++) {
		m_words.push_back(load16(b.code + 2 * i));
	}
	b.checked = m_epoch;
	guard_code(b);
	if (m_translate) {
		translate(b);
	}
	uint32_t &slot = m_page_of[pc >> PAGE_BITS];
	if (slot == 0) {
		m_code_pages.emplace_back();
		m_code_pages.back().block.fill(0);
		slot = static_cast<uint32_t>(m_code_pages.size());
	}
	m_blocks.push_back(b);
	m_decoded++;
	/* The block is found at each of its instructions, where no other
	 * was before, but at the delay slot of its last jump, which nothing
	 * resumes at: the jump has gone by then. */
	CodePage &code = m_code_pages[slot - 1];
	const uint32_t rank = static_cast<uint32_t>(m_blocks.size()), first = (pc & (PAGE_SIZE - 1)) >> 1;
	code.block[first] = rank;
	for (uint32_t i = 1; i < b.count; i++) {
		if (!(b.ops[i].flags & OP_SLOT) && code.block[first + i] == 0) {
			code.block[first + i] = rank | (i << PLACE_BITS);
		}
	}
	if (!(m_hook_on && (pc & PHYSICAL) == m_hook_addr)) {
		near = b;
	}
	run_block(m_blocks.back());
}

/* Loops that only wait */

void Sh4::wait_mark()
{
	m_wait.regs = m_r;
	m_wait.remaining = m_remaining;
	m_wait.countdown = m_wait.period;
	m_touched = false;
}

/* Back at the mark with the same registers, no write and only plain memory
 * read since: nothing else runs before the end of the slice, so every
 * further turn is the same. The whole turns are counted without being run.
 * A loop that works is looked at less and less often. */
void Sh4::wait_check()
{
	int32_t turn = m_wait.remaining - m_remaining;

	if (turn <= 0) {
		return;
	}
	if (m_wait.pass > 0) {
		m_wait.pass--;
		return;
	}
	if (!m_touched && m_r == m_wait.regs) {
		int32_t left = (m_remaining - 1) % turn + 1;
		m_skipped += static_cast<uint32_t>(m_remaining - left);
		m_remaining = left;
	} else {
		if (m_wait.busy < 6) {
			m_wait.busy++;
		}
		m_wait.pass = (1u << m_wait.busy) - 1;
	}
	wait_mark();
}

/* A slice is cut where a timer raises its interrupt, for the interrupt to
 * be taken at its date, and cut again when the program writes a timer. */
int32_t Sh4::run(int32_t cycles)
{
	int32_t over = 0;

	while (cycles > 0) {
		int32_t used;
		over = run_part(std::min(cycles, tmu_next_irq()), used);
		cycles -= used;
	}
	return over;
}

int32_t Sh4::run_part(int32_t cycles, int32_t &used)
{
	uint64_t skipped = m_skipped;
	const int32_t owed = m_over;

	m_slice = cycles;
	m_at = m_cycles;
	m_retime = false;
	m_remaining = cycles - owed;
	m_over = 0;
	new_epoch();
	m_wait.period = 4;
	m_wait.pass = 0;
	m_wait.busy = 0;
	wait_mark();

	while (m_remaining > 0 && m_run == Sh4Run::Running && !m_retime) {
		if (m_r.pc == m_wait.regs.pc) {
			wait_check();
		} else if (--m_wait.countdown <= 0) {
			if (m_wait.period < 1024) {
				m_wait.period *= 2;
			}
			wait_mark();
		}
		if (m_intc.pending) {
			take_irq();
		}
		if (m_hook_on && (m_r.pc & PHYSICAL) == m_hook_addr) {
			m_bus.hook();
			new_epoch();
		}
		m_event = false;
		run_at(m_r.pc);
	}

	/* The time of the machine moves by the cycles asked; what the last
	 * instruction ran over is owed to the next slice. The performance
	 * counters follow at the end of the slice. HEURISTIC: every mode of a
	 * performance counter counts cycles. */
	used = (m_retime && m_remaining > 0 && m_run == Sh4Run::Running) ? cycles - m_remaining : cycles;
	if (m_remaining < 0) {
		m_over = -m_remaining;
	}
	m_cycles += static_cast<uint64_t>(used);
	tmu_sync(m_cycles);
	for (uint32_t i = 0; i < 2; i++) {
		if (m_pm_running[i]) {
			m_pm_counter[i] = (m_pm_counter[i] + static_cast<uint64_t>(used)) & 0xFFFFFFFFFFFFull;
		}
	}
	m_instructions += static_cast<uint64_t>(cycles - owed - m_remaining) - (m_skipped - skipped);
	return -m_remaining;
}

/* State */

void Sh4::save(Sh4State &out) const
{
	out = {};
	std::copy(std::begin(m_r.r), std::end(m_r.r), std::begin(out.r));
	out.pc = m_r.pc;
	out.sr = m_r.sr | m_r.t;
	out.pr = m_r.pr;
	out.gbr = m_r.gbr;
	out.vbr = m_r.vbr;
	out.dbr = m_r.dbr;
	out.mach = m_r.mach;
	out.macl = m_r.macl;
	out.spc = m_r.spc;
	out.ssr = m_r.ssr;
	out.sgr = m_r.sgr;
	std::copy(std::begin(m_r.rbank), std::end(m_r.rbank), std::begin(out.rbank));
	std::copy(std::begin(m_r.fr), std::end(m_r.fr), std::begin(out.fr));
	std::copy(std::begin(m_r.xf), std::end(m_r.xf), std::begin(out.xf));
	out.fpul = m_r.fpul;
	out.fpscr = m_r.fpscr;
	out.run = m_run;
	out.irq_pending = m_intc.pending;
	out.irq_index = m_intc.index;
	std::copy(std::begin(m_intc.source), std::end(m_intc.source), std::begin(out.irq));
	std::copy(std::begin(m_dmac_running), std::end(m_dmac_running), std::begin(out.dmac_running));
	std::copy(std::begin(m_tmu_running), std::end(m_tmu_running), std::begin(out.tmu_running));
	std::copy(std::begin(m_tmu_counter), std::end(m_tmu_counter), std::begin(out.tmu_counter));
	std::copy(std::begin(m_pm_running), std::end(m_pm_running), std::begin(out.pm_running));
	std::copy(std::begin(m_pm_counter), std::end(m_pm_counter), std::begin(out.pm_counter));
	out.over = m_over;
}

/* The registers are taken as they are: the banks are already where SR and
 * FPSCR put them. */
void Sh4::load(const Sh4State &in)
{
	std::copy(std::begin(in.r), std::end(in.r), std::begin(m_r.r));
	m_r.pc = in.pc;
	m_r.sr = in.sr & ~SR_T;
	m_r.t = in.sr & SR_T;
	m_r.pr = in.pr;
	m_r.gbr = in.gbr;
	m_r.vbr = in.vbr;
	m_r.dbr = in.dbr;
	m_r.mach = in.mach;
	m_r.macl = in.macl;
	m_r.spc = in.spc;
	m_r.ssr = in.ssr;
	m_r.sgr = in.sgr;
	std::copy(std::begin(in.rbank), std::end(in.rbank), std::begin(m_r.rbank));
	std::copy(std::begin(in.fr), std::end(in.fr), std::begin(m_r.fr));
	std::copy(std::begin(in.xf), std::end(in.xf), std::begin(m_r.xf));
	m_r.fpul = in.fpul;
	m_r.fpscr = in.fpscr;
	m_run = in.run;
	m_intc.pending = in.irq_pending != 0;
	m_intc.index = in.irq_index;
	std::copy(std::begin(in.irq), std::end(in.irq), std::begin(m_intc.source));
	for (uint32_t i = 0; i < 4; i++) {
		m_dmac_running[i] = in.dmac_running[i] != 0;
	}
	for (uint32_t i = 0; i < 3; i++) {
		m_tmu_running[i] = in.tmu_running[i] != 0;
		m_tmu_counter[i] = in.tmu_counter[i];
	}
	for (uint32_t i = 0; i < 2; i++) {
		m_pm_running[i] = in.pm_running[i] != 0;
		m_pm_counter[i] = in.pm_counter[i];
	}
	m_over = in.over;
}

} /* namespace odin */
