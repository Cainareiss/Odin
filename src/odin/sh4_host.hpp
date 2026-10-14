/*
 * Odin
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

#ifndef ODIN_SH4_HOST_HPP
#define ODIN_SH4_HOST_HPP

#include <cstddef>
#include <cstdint>
#include <initializer_list>

namespace odin {

class Sh4;
struct Sh4Op;

/* Host code for the blocks of a SH-4: the memory it is written in, and the
 * few host instructions a block is made of. A block calls its instructions
 * one after the other; each call returns 0 when the block goes on, 1 when
 * the run loop must have the hand back, anything else is the host code of
 * the block to go to. A simple instruction is not called: the block works
 * on the registers of the CPU itself, with the few steps further down.
 *
 * Only this class knows the host: sh4.cpp says what a block does, with the
 * functions below. On a host it does not write for, or that refuses the
 * memory, usable() is false and the SH-4 keeps to its reader. Written for
 * x86-64, with the calling convention of the system. */
class Sh4Host {
public:
	using Call = uintptr_t (*)(Sh4 &, const Sh4Op *);
	/* A jump forward whose landing is not written yet. */
	struct Jump {
		uint32_t from;
	};
	/* The register of the host a step works in. Nothing stays in it
	 * from one instruction of the CPU to the next. */
	enum class Reg : uint8_t { A };
	enum class Alu : uint8_t { Add, Sub, And, Or, Xor, Compare };
	enum class Shift : uint8_t { Left, Right, RightSigned };
	/* What the last Sub, And or Compare found of its register against
	 * the other value: equal (a result of zero), at least or above as
	 * unsigned numbers, then as signed ones */
	enum class If : uint8_t { Equal, AtLeast, Above, AtLeastSigned, AboveSigned };

	/* `words`: the registers of the CPU, 32 bits each, which the steps
	 * below name by their rank. */
	explicit Sh4Host(uint32_t *words);
	~Sh4Host();
	Sh4Host(const Sh4Host &) = delete;
	Sh4Host &operator=(const Sh4Host &) = delete;

	bool usable() const { return m_code != nullptr; }
	/* Forgets every block. */
	void clear();
	/* Room left for the longest block there is? */
	bool room() const { return m_used + LONGEST <= m_size; }
	/* For the measurements: the bytes written, and the calls that could
	 * not reach their instruction directly. */
	uint32_t used() const { return m_used; }
	uint64_t far_calls() const { return m_far_calls; }

	/* Starts the block of the instructions at `ops`. Returns its place,
	 * never 0. */
	uint32_t begin(const Sh4Op *ops);
	/* Calls an instruction: fn(cpu, &ops[op]). */
	void call(Call fn, uint32_t op);
	/* What the call returned, if not 0: back to the run loop. */
	void loop_if_told();
	/* The same, to a place of the block bound later. */
	Jump jump_if_told();
	void bind(Jump jump);
	/* What the call returned: 1, back to the run loop; else there. */
	void go_where_told();

	/* The steps of an instruction written in place. */
	void load(Reg reg, uint32_t rank);
	void store(uint32_t rank, Reg reg);
	void store_value(uint32_t rank, uint32_t value);
	/* reg = reg <op> the word, or the value; Compare changes nothing. */
	void alu(Alu op, Reg reg, uint32_t rank);
	void alu_value(Alu op, Reg reg, uint32_t value);
	void shift(Shift kind, Reg reg, uint32_t count);
	/* The word takes 1 or 0. Nothing but load() and store() may come
	 * between the step that found it and this one. Uses the register A. */
	void flag(If found, uint32_t rank);

	/* The host code of a block, for an instruction to return it. */
	uintptr_t address(uint32_t place) const { return reinterpret_cast<uintptr_t>(m_code) + place; }
	/* Runs the blocks from that place until one asks for the run loop. */
	void run(Sh4 &cpu, uint32_t place) const { m_enter(&cpu, m_code + place); }

private:
	/* Bytes of the longest block: BLOCK_MAX instructions and a delay
	 * slot, each a call and a test or fewer bytes in place, and as many
	 * ends as branches. */
	static constexpr uint32_t LONGEST = 8192;

	void byte(uint8_t value) { m_code[m_used++] = value; }
	void bytes(std::initializer_list<uint8_t> values);
	void word(uint32_t value);
	void quad(uint64_t value);
	void jump_to(uint32_t place);
	void operand(uint8_t reg, uint32_t rank);

	uint8_t *m_code;
	uint32_t *m_words;
	uint32_t m_size, m_used;
	uint32_t m_leave;	/* place of the way back to the run loop */
	uint64_t m_far_calls;
	void (*m_enter)(Sh4 *, const uint8_t *);
};

} /* namespace odin */

#endif /* ODIN_SH4_HOST_HPP */
