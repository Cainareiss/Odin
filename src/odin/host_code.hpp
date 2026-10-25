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

#ifndef ODIN_HOST_CODE_HPP
#define ODIN_HOST_CODE_HPP

#include <cstdint>
#include <initializer_list>

/* The host the code is written for (HostCode, Sh4Host, Arm7Host) */
#if defined(__x86_64__) || defined(_M_X64)
#define ODIN_HOST_X86_64 1
#endif

namespace odin {

/* Code written for the host while the program runs: memory the host may
 * run, within reach of a direct call from the program when there is some,
 * and the steps every writer of such code needs (bytes, jumps, calls, the
 * way in from C++ and the way back). The writers of a CPU (Sh4Host,
 * Arm7Host) build their blocks on it.
 *
 * Written for x86-64, with the calling convention of the system. On
 * another host, or one that refuses the memory, usable() is false and
 * nothing is written. */
class HostCode {
public:
	/* A jump forward whose landing is not written yet. */
	struct Jump {
		uint32_t from;
	};

	explicit HostCode(uint32_t size);
	~HostCode();
	HostCode(const HostCode &) = delete;
	HostCode &operator=(const HostCode &) = delete;

	bool usable() const { return m_code != nullptr; }
	/* For the measurements: the bytes written, and the calls that could
	 * not reach their target directly. */
	uint32_t used() const { return m_used; }
	uint64_t far_calls() const { return m_far_calls; }
	/* Where the next byte goes, and the host address of a place. */
	uint32_t here() const { return m_used; }
	uintptr_t address(uint32_t place) const { return reinterpret_cast<uintptr_t>(m_code) + place; }
	void bind(Jump jump);
	void jump_back(uint32_t place);

protected:
	/* x86: the registers by number, and the second byte of a jump on a
	 * condition (0F 8x) */
	static constexpr uint8_t RAX = 0, RCX = 1, RDX = 2, RBX = 3, RSP = 4, RBP = 5, RSI = 6, RDI = 7,
		R8 = 8, R9 = 9, R10 = 10, R11 = 11, R12 = 12, R13 = 13, R14 = 14, R15 = 15;
	static constexpr uint8_t JZ = 0x84, JNZ = 0x85, JC = 0x82, JNC = 0x83;
	/* The registers of the first four arguments of a call */
#if defined(_WIN32)
	static constexpr uint8_t ARG[4] = { RCX, RDX, R8, R9 };
#else
	static constexpr uint8_t ARG[4] = { RDI, RSI, RDX, RCX };
#endif

	void byte(uint8_t value) { m_code[m_used++] = value; }
	void bytes(std::initializer_list<uint8_t> values);
	void word(uint32_t value);
	void quad(uint64_t value);
	/* The displacement of a jump that ends at that place. */
	void jump_to(uint32_t place);
	Jump jump_if(uint8_t condition);
	/* A direct call when the target is within reach, else through rax. */
	void call_to(uintptr_t target);
	/* mov dst, src on 32 bits */
	void move32(uint8_t dst, uint8_t src);
	/* On 32 bits: `opcode` dst, src (01 add, 11 adc, 29 sub, 19 sbb, 21
	 * and, 09 or, 31 xor, 39 cmp, 85 test, 89 mov); the group 1 step `ext`
	 * of dst and a value (0 add, 1 or, 4 and, 5 sub, 6 xor, 7 cmp); the
	 * group 2 step `ext` of dst by `count` (1 ror, 3 rcr, 4 shl, 5 shr,
	 * 7 sar); not dst; mov dst, value. */
	void op_rr(uint8_t opcode, uint8_t dst, uint8_t src);
	void op_ri(uint8_t ext, uint8_t dst, uint32_t value);
	void shift_ri(uint8_t ext, uint8_t dst, uint8_t count);
	void not_r(uint8_t dst);
	void mov_ri(uint8_t dst, uint32_t value);
	/* The low byte of dst = 1 when condition `cc` holds (0 o, 2 c, 4 z,
	 * 8 s), then dst = that byte. */
	void flag_r(uint8_t cc, uint8_t dst);

	/* The way in from C++: the registers a call leaves alone (rbx, rbp,
	 * r12 to r15) are kept, the stack is made ready for calls and the
	 * first argument goes to rbx. The writer then sets its own registers
	 * and calls way_in_end(), which jumps to the second argument and
	 * notes the place of the way back; way_out() writes the way back. */
	void way_in();
	void way_in_end();
	void way_out();

	uint8_t *m_code;
	uint32_t m_size, m_used;
	uint32_t m_leave;	/* place of the way back to C++ */
	uint64_t m_far_calls;
};

} /* namespace odin */

#endif /* ODIN_HOST_CODE_HPP */
