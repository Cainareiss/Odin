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

#include "odin/sh4_host.hpp"

#include "odin/sh4.hpp"

#include <cstring>

#if defined(__x86_64__) || defined(_M_X64)
#define ODIN_SH4_HOST_X86_64 1
#if defined(_WIN32)
#include <windows.h>
#else
#include <sys/mman.h>
#endif
#endif

namespace odin {

#if defined(ODIN_SH4_HOST_X86_64)

namespace {

constexpr uint32_t SIZE = 32u << 20;

/* The registers of the first two arguments of a call, and the bytes a call
 * needs above it on the stack, with what keeps the stack aligned on 16. */
#if defined(_WIN32)
constexpr uint8_t ARG0_FROM_RBX = 0xD9;		/* mov rcx, rbx */
constexpr uint8_t ARG1_LEA = 0x14;		/* lea rdx, [r12 + ...] */
constexpr uint8_t RBX_FROM_ARG0 = 0xCB;		/* mov rbx, rcx */
constexpr uint8_t JUMP_ARG1 = 0xE2;		/* jmp rdx */
constexpr uint8_t FRAME = 40;
#else
constexpr uint8_t ARG0_FROM_RBX = 0xDF;		/* mov rdi, rbx */
constexpr uint8_t ARG1_LEA = 0x34;		/* lea rsi, [r12 + ...] */
constexpr uint8_t RBX_FROM_ARG0 = 0xFB;		/* mov rbx, rdi */
constexpr uint8_t JUMP_ARG1 = 0xE6;		/* jmp rsi */
constexpr uint8_t FRAME = 8;
#endif

uint8_t *take(uintptr_t at, uint32_t size)
{
#if defined(_WIN32)
	return static_cast<uint8_t *>(VirtualAlloc(reinterpret_cast<void *>(at), size, MEM_RESERVE | MEM_COMMIT,
	                                           PAGE_EXECUTE_READWRITE));
#else
	void *want = reinterpret_cast<void *>(at);
	void *got = mmap(want, size, PROT_READ | PROT_WRITE | PROT_EXEC,
	                 MAP_PRIVATE | MAP_ANONYMOUS | (at ? MAP_FIXED_NOREPLACE : 0), -1, 0);

	if (got == MAP_FAILED) {
		return nullptr;
	}
	if (at && got != want) {
		munmap(got, size);
		return nullptr;
	}
	return static_cast<uint8_t *>(got);
#endif
}

void give_back(uint8_t *code, uint32_t size)
{
#if defined(_WIN32)
	(void)size;
	VirtualFree(code, 0, MEM_RELEASE);
#else
	munmap(code, size);
#endif
}

/* Memory the host may run, within 2 GB of the code of the program when
 * there is some free, for a call to reach an instruction directly. */
uint8_t *take_near(uint32_t size)
{
	const uintptr_t step = 64u << 20;
	const uintptr_t here = reinterpret_cast<uintptr_t>(&take) & ~(step - 1);

	for (uintptr_t n = 1; n < 24; n++) {
		if (uint8_t *code = take(here + n * step, size)) {
			return code;
		}
		if (here > n * step) {
			if (uint8_t *code = take(here - n * step, size)) {
				return code;
			}
		}
	}
	return take(0, size);
}

} /* namespace */

Sh4Host::Sh4Host()
	: m_code(take_near(SIZE)), m_size(SIZE), m_used(0), m_leave(0), m_far_calls(0), m_enter(nullptr)
{
	if (m_code) {
		clear();
	}
}

Sh4Host::~Sh4Host()
{
	if (m_code) {
		give_back(m_code, m_size);
	}
}

void Sh4Host::bytes(std::initializer_list<uint8_t> values)
{
	for (uint8_t value : values) {
		byte(value);
	}
}

void Sh4Host::word(uint32_t value)
{
	std::memcpy(m_code + m_used, &value, 4);
	m_used += 4;
}

void Sh4Host::quad(uint64_t value)
{
	std::memcpy(m_code + m_used, &value, 8);
	m_used += 8;
}

/* The displacement of a jump that ends at the place being written. */
void Sh4Host::jump_to(uint32_t place)
{
	word(place - (m_used + 4));
}

/* The way in keeps the CPU in rbx, which a call leaves alone as it does
 * r12, and jumps to the block; the way back to the run loop undoes it. */
void Sh4Host::clear()
{
	m_used = 0;
	bytes({0x53, 0x41, 0x54});			/* push rbx / push r12 */
	bytes({0x48, 0x83, 0xEC, FRAME});		/* sub rsp, FRAME */
	bytes({0x48, 0x89, RBX_FROM_ARG0});
	bytes({0xFF, JUMP_ARG1});
	m_leave = m_used;
	bytes({0x48, 0x83, 0xC4, FRAME});		/* add rsp, FRAME */
	bytes({0x41, 0x5C, 0x5B, 0xC3});		/* pop r12 / pop rbx / ret */
	m_enter = reinterpret_cast<void (*)(Sh4 *, const uint8_t *)>(m_code);
}

uint32_t Sh4Host::begin(const Sh4Op *ops)
{
	const uint32_t place = m_used;

	bytes({0x49, 0xBC});				/* mov r12, ops */
	quad(reinterpret_cast<uintptr_t>(ops));
	return place;
}

void Sh4Host::call(Call fn, uint32_t op)
{
	const uint32_t offset = op * static_cast<uint32_t>(sizeof(Sh4Op));

	bytes({0x48, 0x89, ARG0_FROM_RBX});
	if (offset < 128) {
		bytes({0x49, 0x8D, static_cast<uint8_t>(0x40 | ARG1_LEA), 0x24, static_cast<uint8_t>(offset)});
	} else {
		bytes({0x49, 0x8D, static_cast<uint8_t>(0x80 | ARG1_LEA), 0x24});
		word(offset);
	}

	const uintptr_t target = reinterpret_cast<uintptr_t>(fn);
	const int64_t distance = static_cast<int64_t>(target - (reinterpret_cast<uintptr_t>(m_code) + m_used + 5));

	if (distance == static_cast<int32_t>(distance)) {
		byte(0xE8);				/* call fn */
		word(static_cast<uint32_t>(distance));
	} else {
		bytes({0x48, 0xB8});			/* mov rax, fn / call rax */
		quad(target);
		bytes({0xFF, 0xD0});
		m_far_calls++;
	}
}

void Sh4Host::loop_if_told()
{
	bytes({0x48, 0x85, 0xC0, 0x0F, 0x85});		/* test rax, rax / jnz leave */
	jump_to(m_leave);
}

Sh4Host::Jump Sh4Host::jump_if_told()
{
	bytes({0x48, 0x85, 0xC0, 0x0F, 0x85});		/* test rax, rax / jnz ... */
	m_used += 4;
	return Jump{m_used - 4};
}

void Sh4Host::bind(Jump jump)
{
	const uint32_t distance = m_used - (jump.from + 4);

	std::memcpy(m_code + jump.from, &distance, 4);
}

void Sh4Host::go_where_told()
{
	bytes({0x48, 0x83, 0xF8, 0x01, 0x0F, 0x84});	/* cmp rax, 1 / je leave */
	jump_to(m_leave);
	bytes({0xFF, 0xE0});				/* jmp rax */
}

#else /* a host this file does not write code for */

Sh4Host::Sh4Host()
	: m_code(nullptr), m_size(0), m_used(0), m_leave(0), m_far_calls(0), m_enter(nullptr)
{
}

Sh4Host::~Sh4Host() = default;
void Sh4Host::bytes(std::initializer_list<uint8_t>) {}
void Sh4Host::word(uint32_t) {}
void Sh4Host::quad(uint64_t) {}
void Sh4Host::jump_to(uint32_t) {}
void Sh4Host::clear() {}
uint32_t Sh4Host::begin(const Sh4Op *) { return 0; }
void Sh4Host::call(Call, uint32_t) {}
void Sh4Host::loop_if_told() {}
Sh4Host::Jump Sh4Host::jump_if_told() { return Jump{0}; }
void Sh4Host::bind(Jump) {}
void Sh4Host::go_where_told() {}

#endif

} /* namespace odin */
