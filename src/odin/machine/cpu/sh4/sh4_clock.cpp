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

/* Loops that wait on a timer (MARKDOWN/101): turns counted without being
 * run, as long as each would have gone the way the noted one went.
 *
 * PHARRIER waits for a line of the screen by reading TCNT0 again and again:
 * a call works out the time since a date it kept, and the loop goes round
 * until that is over 99. Such a turn is noted once, instruction by
 * instruction (clock_note()), and kept when (clock_follow()): every form is
 * one uses() knows; the only access outside the plain memory is one read
 * of a TCNT, every store puts back the bytes that were there; back at its
 * start, every register is as it was but those from that read, each of
 * which is written before it is read.
 *
 * A turn then depends on the value read alone, and on the memory: the
 * instructions that take the value in, run again from what the others
 * leave, the same at every turn, give each choice made on it. A value the
 * turn loads and only gives to those instructions is loaded again rather
 * than compared (the date PHARRIER counts from changes between two waits).
 *
 * Over whole turns the timer counts down without going back to TCOR, and an
 * instruction that follows the read is of a form whose result moves one way
 * with that input when nothing wraps (clock_ordered(), checked at the first
 * and the last turn): every value of every turn in between then lies between
 * those of the two, and so does every choice. The last turn that can be
 * counted is found by halves. The timer is left as it is: counted up to a
 * date in one go or in several, it ends the same.
 *
 * Where the search for waits looks (wait_check()) falls anywhere in the
 * loop; a turn from the middle carries values read by the turn before, and
 * is not kept. Once a turn is, a look inside it goes to its start. */

#include "odin/machine/cpu/sh4/sh4.hpp"

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstring>

namespace odin {

namespace {

constexpr uint32_t TURN_MAX = 128;	/* instructions of a turn looked at */
constexpr uint32_t DIFFER_MAX = 8;	/* registers a turn may leave changed */
constexpr size_t TURNS_KEPT = 16, MISSES = 4096;	/* misses: one place for each pc that falls there */
constexpr uint32_t BACKOFF_MAX = 1u << 12;
constexpr uint32_t TMU_TCNT0 = 0xD8000C;	/* as onchip_read() names it */
constexpr uint32_t USE_COUNT = 35;	/* R0-R15, T, PR, FPUL, FR0-FR15 */

template <typename F>
void each_bit(uint64_t bits, F &&f)
{
	for (; bits; bits &= bits - 1) {
		f(static_cast<uint32_t>(std::countr_zero(bits)));
	}
}

int64_t wide(uint32_t v) { return static_cast<int64_t>(v); }
float f32(uint32_t v) { return std::bit_cast<float>(v); }
/* Integers with the same top bit are in the same order, signed or not. */
bool same_half(uint32_t a, uint32_t b) { return ((a ^ b) >> 31) == 0; }

} /* namespace */

void Sh4::set_clock_waits(bool on)
{
	m_clock_on = on;
	clock_forget();
}

void Sh4::clock_forget()
{
	m_clock_turns.clear();
	m_clock_misses.clear();
	m_clock_inside.clear();
}

/* After a change of the turns kept */
void Sh4::clock_kept()
{
	m_clock_inside.clear();
	for (const ClockTurn &t : m_clock_turns) {
		for (const auto &[pc, word] : t.code) {
			if (pc != t.start.pc) {
				m_clock_inside[pc] = t.start.pc;
			}
		}
	}
}

Sh4::ClockMiss &Sh4::clock_miss(uint32_t pc)
{
	if (m_clock_misses.empty()) {
		m_clock_misses.assign(MISSES, ClockMiss{1, 0, 0});	/* no pc is odd */
	}
	return m_clock_misses[(pc >> 1) & (MISSES - 1)];
}

/* The mark moved to `pc`, the start of a turn the loop is in, for its turns
 * to be counted from there; the blocks before the next mark, enough to get
 * there. */
void Sh4::clock_mark(uint32_t pc)
{
	m_wait.regs.pc = pc;
	m_wait.moved = true;
	m_wait.countdown += static_cast<int32_t>(TURN_MAX);
}

uint32_t &Sh4::use_of(Regs &r, uint32_t i)
{
	if (i < 16) {
		return r.r[i];
	}
	if (i == USE_T) {
		return r.t;
	}
	if (i == USE_PR) {
		return r.pr;
	}
	if (i == USE_FPUL) {
		return r.fpul;
	}
	return r.fr[i - USE_FR];
}

bool Sh4::same_but(const Regs &a, const Regs &b, uint64_t free)
{
	Regs x = a, y = b;

	each_bit(free, [&](uint32_t i) { use_of(x, i) = use_of(y, i) = 0; });
	return x == y;
}

void Sh4::clock_noted_read(uint32_t addr, uint32_t size, const void *at)
{
	uint64_t value = 0;

	std::memcpy(&value, at, size);
	m_clock_note.reads.push_back({addr, size, value, m_clock_note.step});
}

void Sh4::clock_noted_write(uint32_t addr, uint32_t size, const void *old, const void *value)
{
	uint64_t v = 0;

	if (std::memcmp(old, value, size) != 0) {
		m_clock_note.bad = true;
		return;
	}
	std::memcpy(&v, value, size);
	m_clock_note.writes.push_back({addr, size, v, m_clock_note.step});
}

/* A read outside the plain memory: one of a TCNT only */
void Sh4::clock_noted_slow(uint32_t addr, uint32_t size, uint64_t value)
{
	const uint32_t top = addr >> 24, reg = addr & 0xFFFFFF;
	uint32_t channel = 3;

	(void)value;
	for (uint32_t ch = 0; ch < 3; ch++) {
		if (reg == TMU_TCNT0 + 12 * ch) {
			channel = ch;
		}
	}
	if ((top != 0x1F && top != 0xFF) || channel == 3 || size != 4 || m_clock_note.reads_of_timer++ != 0) {
		m_clock_note.bad = true;
		return;
	}
	m_clock_note.channel = channel;
	m_clock_note.read_step = m_clock_note.step;
	m_clock_note.read_time = m_at;
}

void Sh4::op_noted(Sh4 &c, const Sh4Op *op)
{
	c.m_r.pc = op->imm;
}

/* Runs the instruction at PC alone, for clock_note(): a branch here as its
 * function does, any other through its function in a block of its own whose
 * end goes nowhere else (the end of a block that is read follows on to the
 * next block); the delay slot of a branch that goes, after the branch, with
 * the target in PC as on the chip. Each instruction goes to `steps` with the
 * registers before it, its address and word to `code`. False, and nothing
 * run, when the code is not in plain memory, a form is not one uses() knows,
 * or the branch would go to an odd address or take a slot it may not. */
bool Sh4::clock_step(std::vector<ClockStep> &steps, std::vector<std::pair<uint32_t, uint16_t>> &code)
{
	const uint32_t pc = m_r.pc;
	const uint64_t pr_bit = uint64_t{1} << USE_PR;
	const auto word_at = [this](uint32_t at, uint16_t &word) {
		const uint8_t *page = m_rd[(at & PHYSICAL) >> PAGE_BITS];

		if (!page || (at & 1)) {
			return false;
		}
		std::memcpy(&word, page + (at & (PAGE_SIZE - 1)), 2);
		return true;
	};
	const auto run_alone = [this](Sh4Op one, uint32_t at, uint32_t next) {
		Sh4Op end{};
		Block b{at, 1, 0, nullptr, nullptr, nullptr, 0, 0};

		end.run = op_noted;
		end.imm = next;
		end.idx = 1;
		m_short_ops.clear();
		m_short_ops.push_back(one);
		m_short_ops.push_back(end);
		b.ops = m_short_ops.data();
		run_block(b);
	};
	uint16_t word, slot_word = 0;
	Sh4Op op, slot{};
	ClockStep first{}, second{};

	if (!word_at(pc, word)) {
		return false;
	}
	const Kind kind = decode(word, pc, op);

	op.idx = 0;
	first.op = op;
	first.before = m_r;
	if (!uses(op, first.use)) {
		return false;
	}
	if (kind != Kind::Branch && kind != Kind::BranchDelayed && kind != Kind::Jump) {
		steps.push_back(first);
		code.push_back({pc, word});
		m_clock_note.step = static_cast<uint32_t>(steps.size() - 1);
		run_alone(op, pc, pc + 2);
		return true;
	}

	/* bt, bf, bt/s, bf/s, bra, bsr: the target in imm; jmp, jsr, rts: in
	 * a register */
	const Uses &u = first.use;
	const bool goes = kind == Kind::Jump || u.on_t == (m_r.t != 0);
	const uint32_t target = !u.target ? op.imm : (u.reads & pr_bit) ? m_r.pr : m_r.r[op.n];
	const bool with_slot = goes && kind != Kind::Branch;

	if (goes && (target & 1)) {
		return false;
	}
	if (with_slot) {
		if (!word_at(pc + 2, slot_word) || bad_slot(decode(slot_word, pc + 2, slot), pc + 2)) {
			return false;
		}
		slot.idx = 0;
		second.op = slot;
		second.before = m_r;
		if (u.writes & pr_bit) {
			second.before.pr = pc + 4;
		}
		if (!uses(slot, second.use)) {
			return false;
		}
	}
	steps.push_back(first);
	code.push_back({pc, word});
	m_remaining -= 1;
	if (u.writes & pr_bit) {
		m_r.pr = pc + 4;
	}
	if (!with_slot) {
		m_r.pc = goes ? target : pc + 2;
		return true;
	}
	m_r.pc = target;
	steps.push_back(second);
	code.push_back({pc + 2, slot_word});
	m_clock_note.step = static_cast<uint32_t>(steps.size() - 1);
	slot.flags |= OP_SLOT;
	run_alone(slot, pc + 2, target);
	return true;
}

/* Runs a turn of the loop from its start, noting what it does: true, and
 * `turn` filled, when it only waits on a timer. The turn has run, or a part
 * of it, either way, as the program would have run it. */
bool Sh4::clock_note(ClockTurn &turn)
{
	const uint32_t start = m_r.pc;
	const Regs before = m_r;
	const int32_t remaining = m_remaining;
	const uint64_t time = m_cycles + static_cast<uint64_t>(m_slice - m_remaining);
	std::vector<ClockStep> steps;
	std::vector<std::pair<uint32_t, uint16_t>> code;
	bool ran = true;

	if (m_remaining < static_cast<int32_t>(2 * TURN_MAX + BLOCK_MAX) || m_intc.pending ||
	    m_run != Sh4Run::Running || (m_r.fpscr & (FPSCR_PR | FPSCR_SZ))) {
		return false;
	}
	m_clock_note = ClockNote{};
	m_clock_note.on = true;
	do {
		if (m_hook_on && (m_r.pc & PHYSICAL) == m_hook_addr) {
			ran = false;
			break;
		}
		m_event = false;
		if (!clock_step(steps, code) || m_clock_note.bad || m_event || m_intc.pending ||
		    m_run != Sh4Run::Running) {
			ran = false;
			break;
		}
	} while (m_r.pc != start && steps.size() < TURN_MAX);
	m_clock_note.on = false;
	if (!ran || m_r.pc != start || m_clock_note.reads_of_timer != 1) {
		return false;
	}
	turn.start = before;
	turn.length = static_cast<uint32_t>(remaining - m_remaining);
	turn.read_at = static_cast<uint32_t>(m_clock_note.read_time - time);
	turn.channel = m_clock_note.channel;
	turn.code = std::move(code);
	turn.writes = std::move(m_clock_note.writes);
	return clock_follow(turn, steps);
}

/* Follows the value read through the noted turn: which instructions take
 * it in, which choices are made on it, which loads only feed them. True
 * when the turn waits on the timer alone (see the head of this file); the
 * read, those instructions and those choices go to the turn. */
bool Sh4::clock_follow(ClockTurn &turn, std::vector<ClockStep> &steps)
{
	enum : uint8_t { NONE, INT, FLOAT, FLAG };
	const std::vector<ClockAccess> &reads = m_clock_note.reads;
	uint64_t timed = 0, seen = 0, live = 0;
	uint8_t type[USE_COUNT] = {};
	/* For each register, the load whose value it holds; for each load:
	 * 1 taken by instructions that follow the read only so far, 2 by
	 * another, 3 done (written again after those only) */
	int32_t load_of[USE_COUNT];
	std::vector<uint8_t> fresh_load(steps.size(), 0);

	std::fill(std::begin(load_of), std::end(load_of), -1);
	for (size_t i = 0; i < steps.size(); i++) {
		ClockStep &s = steps[i];
		const Uses &u = s.use;
		const uint64_t from_time = u.reads & timed;

		live |= u.reads & ~seen;
		seen |= u.reads | u.writes;
		s.timed = timed;
		s.from = s.other = s.to = NO_USE;
		if (i == m_clock_note.read_step) {
			if (!std::has_single_bit(u.loaded) || std::countr_zero(u.loaded) >= 16 || from_time) {
				return false;
			}
			s.role = ClockRole::Read;
			s.to = static_cast<uint8_t>(std::countr_zero(u.loaded));
			type[s.to] = INT;
		} else if (from_time) {
			const uint64_t others = u.reads & ~timed;

			/* fmul fr4,fr4 squares: it does not move one way */
			if (u.memory || u.target || u.twice || !std::has_single_bit(from_time) ||
			    std::popcount(others) > 1) {
				return false;
			}
			s.from = static_cast<uint8_t>(std::countr_zero(from_time));
			s.other = others ? static_cast<uint8_t>(std::countr_zero(others)) : NO_USE;
			if (u.choice) {
				if (s.from != USE_T || type[USE_T] != FLAG) {
					return false;
				}
				s.role = ClockRole::Choice;
				s.t = s.before.t;
			} else {
				const uint8_t in = type[s.from];
				uint8_t out = NONE;

				switch (u.order) {
				case Order::Same:
					out = in;
					break;
				case Order::Add: case Order::Sub: case Order::Neg:
				case Order::ShiftLeft: case Order::ShiftRight:
					out = in == INT ? INT : NONE;
					break;
				case Order::ToFloat:
					out = in == INT ? FLOAT : NONE;
					break;
				case Order::ToInt:
					out = in == FLOAT ? INT : NONE;
					break;
				case Order::FMul: case Order::FAdd: case Order::FSub: case Order::FDiv: case Order::FNeg:
					out = in == FLOAT ? FLOAT : NONE;
					break;
				case Order::Above: case Order::AtLeast: case Order::Greater: case Order::GreaterEq:
				case Order::NotNegative: case Order::Positive:
					out = in == INT ? FLAG : NONE;
					break;
				case Order::FGreater:
					out = in == FLOAT ? FLAG : NONE;
					break;
				case Order::None:
					break;
				}
				if (out == NONE || !std::has_single_bit(u.writes)) {
					return false;
				}
				const uint32_t n_use = s.from >= USE_FR ? USE_FR + s.op.n : s.op.n;

				s.to = static_cast<uint8_t>(std::countr_zero(u.writes));
				s.from_n = s.from == n_use;
				if (u.order == Order::FDiv && !s.from_n) {
					return false;
				}
				s.role = ClockRole::Follows;
				s.call = called(s.op.run);
				type[s.to] = out;
			}
		} else {
			s.role = ClockRole::Same;
		}

		each_bit(u.reads, [&](uint32_t r) {
			if (load_of[r] >= 0 && fresh_load[static_cast<size_t>(load_of[r])] == 1 &&
			    s.role != ClockRole::Follows) {
				fresh_load[static_cast<size_t>(load_of[r])] = 2;
			}
		});
		each_bit(u.writes, [&](uint32_t r) {
			if (load_of[r] >= 0 && fresh_load[static_cast<size_t>(load_of[r])] == 1) {
				fresh_load[static_cast<size_t>(load_of[r])] = 3;
			}
			load_of[r] = -1;
		});
		if (s.role == ClockRole::Same && u.memory && std::has_single_bit(u.loaded) &&
		    std::countr_zero(u.loaded) < 16) {
			load_of[std::countr_zero(u.loaded)] = static_cast<int32_t>(i);
			fresh_load[i] = 1;
		}
		if (s.role == ClockRole::Read) {
			timed = (timed & ~u.writes) | u.loaded;
		} else if (s.role == ClockRole::Follows) {
			timed |= u.writes;
		} else if (s.role == ClockRole::Same) {
			timed &= ~u.writes;
		}
	}

	uint64_t differ = 0;
	Regs end = m_r;

	for (uint32_t i = 0; i < USE_COUNT; i++) {
		if (use_of(end, i) != use_of(turn.start, i)) {
			differ |= uint64_t{1} << i;
		}
	}
	if (std::popcount(differ) > static_cast<int>(DIFFER_MAX) || (differ & ~timed) || (timed & live) ||
	    !same_but(end, turn.start, differ)) {
		return false;
	}

	/* The other input of an instruction that follows the read, from a load
	 * that only such instructions take: read again from the memory */
	std::vector<bool> fresh_read(reads.size(), false);

	for (size_t i = 0; i < steps.size(); i++) {
		ClockStep &s = steps[i];

		if (s.role != ClockRole::Follows || s.other == NO_USE) {
			continue;
		}
		for (size_t j = i; j-- > 0;) {
			if (!(steps[j].use.writes & (uint64_t{1} << s.other))) {
				continue;
			}
			if (fresh_load[j] == 3) {
				for (size_t k = 0; k < reads.size(); k++) {
					if (reads[k].step == j) {
						s.fresh = true;
						s.fresh_addr = reads[k].addr;
						s.fresh_size = reads[k].size;
						fresh_read[k] = true;
					}
				}
			}
			break;
		}
	}
	turn.reads.clear();
	for (size_t k = 0; k < reads.size(); k++) {
		if (!fresh_read[k]) {
			turn.reads.push_back(reads[k]);
		}
	}
	turn.live = live;
	turn.timed = timed;
	turn.steps.clear();
	for (const ClockStep &s : steps) {
		if (s.role != ClockRole::Same) {
			turn.steps.push_back(s);
		}
	}
	return true;
}

/* Whether the kept turn is still what the loop does from here: the same
 * registers where it reads them, the same code, the same bytes where it
 * reads and writes but those it loads again. */
bool Sh4::clock_still(const ClockTurn &turn) const
{
	const auto plain = [this](uint32_t addr, uint32_t size, uint64_t value) {
		const uint8_t *page = m_rd[addr >> PAGE_BITS];
		uint64_t now = 0;

		if (!page) {
			return false;
		}
		std::memcpy(&now, page + (addr & (PAGE_SIZE - 1)), size);
		return now == value;
	};
	Regs now = m_r, then = turn.start;

	for (uint32_t i = 0; i < USE_COUNT; i++) {
		if (!(turn.live & (uint64_t{1} << i))) {
			use_of(now, i) = use_of(then, i) = 0;
		}
	}
	if (m_intc.pending || m_run != Sh4Run::Running || !(now == then)) {
		return false;
	}
	for (const auto &[pc, word] : turn.code) {
		if ((m_hook_on && (pc & PHYSICAL) == m_hook_addr) || !plain(pc & PHYSICAL, 2, word)) {
			return false;
		}
	}
	for (const ClockAccess &a : turn.reads) {
		if (!plain(a.addr, a.size, a.value)) {
			return false;
		}
	}
	for (const ClockAccess &a : turn.writes) {
		if (!plain(a.addr, a.size, a.value)) {
			return false;
		}
	}
	for (const ClockStep &s : turn.steps) {
		if (s.fresh && !m_rd[s.fresh_addr >> PAGE_BITS]) {
			return false;
		}
	}
	return true;
}

/* A turn for the timer reading `value`: each instruction that follows the
 * read run alone, its input from the time from those before it, its other
 * input as the noted turn had it or loaded again. `out` gets, for each step,
 * the input from the time, the other input and what was written, and the
 * registers from the time at the end. True when every choice goes as noted. */
bool Sh4::clock_turn(const ClockTurn &turn, uint32_t value, ClockValues &out)
{
	const Regs kept = m_r;

	out.same = true;
	out.step.resize(turn.steps.size());
	for (size_t i = 0; i < turn.steps.size(); i++) {
		const ClockStep &s = turn.steps[i];
		std::array<uint32_t, 3> &v = out.step[i];

		if (s.role == ClockRole::Read) {
			out.timed[s.to] = value;
			v = {0, 0, value};
			continue;
		}
		v[0] = out.timed[s.from];
		if (s.role == ClockRole::Choice) {
			if (v[0] != s.t) {
				out.same = false;
				break;
			}
			continue;
		}
		if (s.other != NO_USE) {
			if (s.fresh) {
				uint64_t loaded = 0;

				std::memcpy(&loaded, m_rd[s.fresh_addr >> PAGE_BITS] + (s.fresh_addr & (PAGE_SIZE - 1)),
				            s.fresh_size);
				v[1] = static_cast<uint32_t>(loaded);
			} else {
				Regs before = s.before;

				v[1] = use_of(before, s.other);
			}
			use_of(m_r, s.other) = v[1];
		}
		use_of(m_r, s.from) = v[0];
		s.call(*this, &s.op);
		v[2] = out.timed[s.to] = use_of(m_r, s.to);
	}
	m_r = kept;
	return out.same;
}

/* Whether every instruction that follows the read moves one way with its
 * input from the time between the turns `a` and `b`, nothing wrapping, so
 * that every turn in between has its values between theirs. */
bool Sh4::clock_ordered(const ClockTurn &turn, const ClockValues &a, const ClockValues &b)
{
	const auto finite = [](uint32_t v) { return std::isfinite(f32(v)); };
	const auto number = [](uint32_t v) { return !std::isnan(f32(v)); };
	const auto in_range = [](uint32_t v) { return f32(v) >= -2147483648.0f && f32(v) < 2147483648.0f; };
	/* The exact change of an integer result for the exact change of its
	 * input: the same, the opposite, or that many times */
	const auto moved = [](const std::array<uint32_t, 3> &x, const std::array<uint32_t, 3> &y, int64_t times) {
		return wide(y[2]) - wide(x[2]) == (wide(y[0]) - wide(x[0])) * times;
	};

	for (size_t i = 0; i < turn.steps.size(); i++) {
		const ClockStep &s = turn.steps[i];
		const std::array<uint32_t, 3> &x = a.step[i], &y = b.step[i];
		bool ok = false;

		if (s.role != ClockRole::Follows) {
			continue;
		}
		switch (s.use.order) {
		case Order::Same:
			ok = true;
			break;
		case Order::Add:
			ok = same_half(x[0], y[0]) && moved(x, y, 1);
			break;
		case Order::Sub:
			ok = same_half(x[0], y[0]) && moved(x, y, s.from_n ? 1 : -1);
			break;
		case Order::Neg:
			ok = same_half(x[0], y[0]) && moved(x, y, -1);
			break;
		case Order::ShiftLeft:
			ok = same_half(x[0], y[0]) && moved(x, y, int64_t{1} << s.use.shift);
			break;
		case Order::ShiftRight:
		case Order::ToFloat:
			ok = same_half(x[0], y[0]);
			break;
		case Order::ToInt:
			ok = number(x[0]) && number(y[0]) && in_range(x[0]) && in_range(y[0]);
			break;
		case Order::FMul:
		case Order::FAdd:
		case Order::FSub:
			ok = finite(x[1]) && number(x[0]) && number(y[0]) && number(x[2]) && number(y[2]);
			break;
		case Order::FDiv:
			ok = finite(x[1]) && f32(x[1]) != 0.0f && number(x[0]) && number(y[0]) && number(x[2]) &&
			     number(y[2]);
			break;
		case Order::FNeg:
			ok = number(x[0]) && number(y[0]);
			break;
		case Order::Above: case Order::AtLeast: case Order::Greater: case Order::GreaterEq:
		case Order::NotNegative: case Order::Positive:
			ok = same_half(x[0], y[0]) && x[2] == y[2];
			break;
		case Order::FGreater:
			ok = number(x[0]) && number(y[0]) && number(x[1]) && x[2] == y[2];
			break;
		case Order::None:
			break;
		}
		if (!ok) {
			return false;
		}
	}
	return true;
}

/* The whole turns that go the way the kept one went, counted without being
 * run, up to the one that would go another way and short of the end of the
 * slice: the last one found by halves. True when there were some. */
bool Sh4::clock_skip(const ClockTurn &turn)
{
	const uint64_t now = m_cycles + static_cast<uint64_t>(m_slice - m_remaining);
	const uint64_t length = turn.length, first_read = now + turn.read_at;
	ClockValues &first = m_clock_values[0], &last = m_clock_values[1];
	const auto read_at = [&](uint32_t k) { return first_read + k * length; };

	/* Room for the turns counted, one more, and a block to end the slice */
	if (m_remaining < static_cast<int32_t>(2 * length + BLOCK_MAX)) {
		return false;
	}
	const uint32_t room = static_cast<uint32_t>((static_cast<uint64_t>(m_remaining) - BLOCK_MAX) / length) - 1;
	const uint32_t v0 = tmu_count_at(turn.channel, first_read);

	if (!clock_turn(turn, v0, first)) {
		return false;
	}
	/* Turns 0 to k all go the way noted */
	const auto all_go = [&](uint32_t k) {
		return tmu_counts_between(turn.channel, first_read, read_at(k)) <= v0 &&
		       clock_turn(turn, tmu_count_at(turn.channel, read_at(k)), last) && clock_ordered(turn, first, last);
	};
	uint32_t lo = 0, hi = room - 1;

	if (all_go(hi)) {
		lo = hi;
	} else {
		while (lo < hi) {
			const uint32_t mid = lo + (hi - lo + 1) / 2;

			if (all_go(mid)) {
				lo = mid;
			} else {
				hi = mid - 1;
			}
		}
	}
	const ClockValues *values = &first;

	if (lo > 0) {
		all_go(lo);
		values = &last;
	}
	m_remaining -= static_cast<int32_t>((lo + 1) * length);
	m_skipped += (lo + 1) * length;
	each_bit(turn.timed, [&](uint32_t i) { use_of(m_r, i) = values->timed[i]; });
	return true;
}

/* At the start of a turn of a loop that did not wait the plain way (see
 * wait_check()): its turns that only wait on a timer, counted without
 * being run. True when some were. Inside a turn found before, the loop is
 * counted from the start of that turn: `mark` gets it. */
bool Sh4::clock_wait(uint32_t &mark)
{
	const uint32_t pc = m_r.pc;
	auto kept = std::find_if(m_clock_turns.begin(), m_clock_turns.end(),
	                         [pc](const ClockTurn &t) { return t.start.pc == pc; });

	if (kept == m_clock_turns.end()) {
		const auto inside = m_clock_inside.find(pc);

		if (inside != m_clock_inside.end()) {
			mark = inside->second;
			return false;
		}
	} else if (m_remaining < static_cast<int32_t>(2 * kept->length + BLOCK_MAX)) {
		return false;	/* no room for a turn counted, see clock_skip() */
	} else if (!clock_still(*kept)) {
		m_clock_turns.erase(kept);
		clock_kept();
		kept = m_clock_turns.end();
	}
	if (kept == m_clock_turns.end()) {
		ClockMiss &miss = clock_miss(pc);
		ClockTurn turn;

		if (m_wait.moved) {
			return false;
		}
		if (miss.pc == pc && miss.wait > 0) {
			miss.wait--;
			return false;
		}
		/* A TCNT read, few registers changed: worth a look */
		uint64_t differ = 0;

		for (uint32_t i = 0; i < USE_COUNT; i++) {
			if (use_of(m_r, i) != use_of(m_wait.regs, i)) {
				differ |= uint64_t{1} << i;
			}
		}
		if (!m_timer_read || std::popcount(differ) > static_cast<int>(DIFFER_MAX) ||
		    !same_but(m_r, m_wait.regs, differ) || !clock_note(turn)) {
			if (miss.pc != pc) {
				miss = {pc, 1, 1};
			} else {
				miss.backoff = std::min(miss.backoff * 2, BACKOFF_MAX);
				miss.wait = miss.backoff;
			}
			return false;
		}
		if (miss.pc == pc) {
			miss.pc = 1;
		}
		if (m_clock_turns.size() == TURNS_KEPT) {
			m_clock_turns.erase(m_clock_turns.begin());
		}
		m_clock_turns.push_back(std::move(turn));
		clock_kept();
		kept = m_clock_turns.end() - 1;
	}
	return clock_skip(*kept);
}

} /* namespace odin */
