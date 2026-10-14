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

/* rbp points this many bytes into the registers of the CPU: the first 64
 * words are a byte of displacement away. */
constexpr uint32_t MIDDLE = 128;

/* The number of a register of the host (eax), and what x86 calls the steps. */
constexpr uint8_t number(Sh4Host::Reg) { return 0; }
constexpr uint8_t ALU[] = { 0, 5, 4, 1, 6, 7 };			/* add, sub, and, or, xor, cmp */
constexpr uint8_t SHIFT[] = { 4, 5, 7 };			/* shl, shr, sar */
constexpr uint8_t SET[] = { 0x94, 0x93, 0x97, 0x9D, 0x9F };	/* sete, setae, seta, setge, setg */

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

Sh4Host::Sh4Host(uint32_t *words, const Memory &memory)
	: m_code(take_near(SIZE)), m_words(words), m_memory(memory), m_size(SIZE), m_used(0), m_leave(0), m_far_calls(0),
	  m_enter(nullptr)
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

/* The way in keeps the CPU in rbx, its registers in rbp and the tables of
 * its memory in r13, r14 and r15, all of which a call leaves alone as it
 * does r12, and jumps to the block; the way back to the run loop undoes it. */
void Sh4Host::clear()
{
	m_used = 0;
	bytes({0x53, 0x41, 0x54, 0x55});		/* push rbx / push r12 / push rbp */
	bytes({0x41, 0x55, 0x41, 0x56, 0x41, 0x57});	/* push r13 / push r14 / push r15 */
	bytes({0x48, 0x83, 0xEC, FRAME});		/* sub rsp, FRAME */
	bytes({0x48, 0x89, RBX_FROM_ARG0});
	bytes({0x48, 0xBD});				/* mov rbp, the registers */
	quad(reinterpret_cast<uintptr_t>(m_words) + MIDDLE);
	bytes({0x49, 0xBD});				/* mov r13, the pages to read */
	quad(reinterpret_cast<uintptr_t>(m_memory.reads));
	bytes({0x49, 0xBE});				/* mov r14, the pages to write */
	quad(reinterpret_cast<uintptr_t>(m_memory.writes));
	bytes({0x49, 0xBF});				/* mov r15, the epochs of the code */
	quad(reinterpret_cast<uintptr_t>(m_memory.code_epochs));
	bytes({0xFF, JUMP_ARG1});
	m_leave = m_used;
	bytes({0x48, 0x83, 0xC4, FRAME});		/* add rsp, FRAME */
	bytes({0x41, 0x5F, 0x41, 0x5E, 0x41, 0x5D});	/* pop r15 / pop r14 / pop r13 */
	bytes({0x5D, 0x41, 0x5C, 0x5B, 0xC3});		/* pop rbp / pop r12 / pop rbx / ret */
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

/* The word of the registers of the CPU as the operand of an instruction
 * that names `reg` too: [rbp + displacement]. */
void Sh4Host::operand(uint8_t reg, uint32_t rank)
{
	const int32_t away = static_cast<int32_t>(4 * rank) - static_cast<int32_t>(MIDDLE);

	if (away == static_cast<int8_t>(away)) {
		bytes({static_cast<uint8_t>(0x45 | (reg << 3)), static_cast<uint8_t>(away)});
	} else {
		byte(static_cast<uint8_t>(0x85 | (reg << 3)));
		word(static_cast<uint32_t>(away));
	}
}

void Sh4Host::load(Reg reg, uint32_t rank)
{
	byte(0x8B);					/* mov reg, [the word] */
	operand(number(reg), rank);
}

void Sh4Host::store(uint32_t rank, Reg reg)
{
	byte(0x89);					/* mov [the word], reg */
	operand(number(reg), rank);
}

void Sh4Host::store_value(uint32_t rank, uint32_t value)
{
	byte(0xC7);					/* mov dword [the word], value */
	operand(0, rank);
	word(value);
}

void Sh4Host::alu(Alu op, Reg reg, uint32_t rank)
{
	byte(static_cast<uint8_t>(ALU[static_cast<uint8_t>(op)] * 8 + 3));	/* op reg, [the word] */
	operand(number(reg), rank);
}

void Sh4Host::alu_value(Alu op, Reg reg, uint32_t value)
{
	const uint8_t which = static_cast<uint8_t>(0xC0 | (ALU[static_cast<uint8_t>(op)] << 3) | number(reg));

	if (static_cast<int32_t>(value) == static_cast<int8_t>(value)) {
		bytes({0x83, which, static_cast<uint8_t>(value)});	/* op reg, a byte with its sign */
	} else {
		bytes({0x81, which});					/* op reg, value */
		word(value);
	}
}

void Sh4Host::shift(Shift kind, Reg reg, uint32_t count)
{
	bytes({0xC1, static_cast<uint8_t>(0xC0 | (SHIFT[static_cast<uint8_t>(kind)] << 3) | number(reg)),
	       static_cast<uint8_t>(count)});
}

void Sh4Host::flag(If found, uint32_t rank)
{
	bytes({0x0F, SET[static_cast<uint8_t>(found)], 0xC0});	/* set<found> al */
	bytes({0x0F, 0xB6, 0xC0});				/* movzx eax, al */
	store(rank, Reg::A);
}

Sh4Host::Jump Sh4Host::jump_if(uint8_t condition)
{
	bytes({0x0F, condition});			/* j<condition> ... */
	m_used += 4;
	return Jump{m_used - 4};
}

void Sh4Host::bind(const Slow &slow)
{
	for (uint32_t k = 0; k < slow.count; k++) {
		bind(slow.jumps[k]);
	}
}

void Sh4Host::jump_back(uint32_t place)
{
	byte(0xE9);					/* jmp place */
	jump_to(place);
}

/* rdx = the page of the address in A in the table of r13 or r14; the long
 * way when there is none or the address is not aligned. ecx keeps the
 * address over the bits of the page. */
void Sh4Host::page(uint8_t table, uint32_t size, Slow &slow)
{
	bytes({0x89, 0xC1});				/* mov ecx, eax */
	bytes({0xC1, 0xE9, static_cast<uint8_t>(m_memory.page_bits)});	/* shr ecx, page_bits */
	bytes({0x49, 0x8B, 0x54, static_cast<uint8_t>(0xC8 | table), 0x00});	/* mov rdx, [table + rcx * 8] */
	bytes({0x48, 0x85, 0xD2});			/* test rdx, rdx */
	slow.jumps[slow.count++] = jump_if(0x84);	/* jz */
	if (size > 1) {
		bytes({0xA8, static_cast<uint8_t>(size - 1)});	/* test al, size - 1 */
		slow.jumps[slow.count++] = jump_if(0x85);	/* jnz */
	}
	byte(0x25);					/* and eax, the offset in the page */
	word((1u << m_memory.page_bits) - 1);
}

void Sh4Host::read(uint32_t size, Slow &slow)
{
	page(5, size, slow);				/* r13 */
	switch (size) {
	case 1:
		bytes({0x0F, 0xBE, 0x04, 0x02});	/* movsx eax, byte [rdx + rax] */
		break;
	case 2:
		bytes({0x0F, 0xBF, 0x04, 0x02});	/* movsx eax, word [rdx + rax] */
		break;
	default:
		bytes({0x8B, 0x04, 0x02});		/* mov eax, [rdx + rax] */
		break;
	}
}

void Sh4Host::write(uint32_t size, uint32_t rank, Slow &slow)
{
	page(6, size, slow);				/* r14 */
	bytes({0x81, 0xE1});				/* and ecx, the physical pages */
	word(m_memory.physical_pages - 1);
	bytes({0x45, 0x8B, 0x44, 0x8F, 0x00});		/* mov r8d, [r15 + rcx * 4] */
	bytes({0x44, 0x3B, 0x83});			/* cmp r8d, [rbx + epoch] */
	word(static_cast<uint32_t>(m_memory.epoch));
	slow.jumps[slow.count++] = jump_if(0x84);	/* je */
	bytes({0xC6, 0x83});				/* mov byte [rbx + touched], 1 */
	word(static_cast<uint32_t>(m_memory.touched));
	byte(1);
	byte(0x8B);					/* mov ecx, [the word] */
	operand(1, rank);
	switch (size) {
	case 1:
		bytes({0x88, 0x0C, 0x02});		/* mov [rdx + rax], cl */
		break;
	case 2:
		bytes({0x66, 0x89, 0x0C, 0x02});	/* mov [rdx + rax], cx */
		break;
	default:
		bytes({0x89, 0x0C, 0x02});		/* mov [rdx + rax], ecx */
		break;
	}
}

void Sh4Host::read_at(const uint8_t *at, uint32_t size)
{
	bytes({0x48, 0xB8});				/* mov rax, at */
	quad(reinterpret_cast<uintptr_t>(at));
	switch (size) {
	case 1:
		bytes({0x0F, 0xBE, 0x00});		/* movsx eax, byte [rax] */
		break;
	case 2:
		bytes({0x0F, 0xBF, 0x00});		/* movsx eax, word [rax] */
		break;
	default:
		bytes({0x8B, 0x00});			/* mov eax, [rax] */
		break;
	}
}

#else /* a host this file does not write code for */

Sh4Host::Sh4Host(uint32_t *words, const Memory &memory)
	: m_code(nullptr), m_words(words), m_memory(memory), m_size(0), m_used(0), m_leave(0), m_far_calls(0),
	  m_enter(nullptr)
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
void Sh4Host::operand(uint8_t, uint32_t) {}
void Sh4Host::load(Reg, uint32_t) {}
void Sh4Host::store(uint32_t, Reg) {}
void Sh4Host::store_value(uint32_t, uint32_t) {}
void Sh4Host::alu(Alu, Reg, uint32_t) {}
void Sh4Host::alu_value(Alu, Reg, uint32_t) {}
void Sh4Host::shift(Shift, Reg, uint32_t) {}
void Sh4Host::flag(If, uint32_t) {}
Sh4Host::Jump Sh4Host::jump_if(uint8_t) { return Jump{0}; }
void Sh4Host::bind(const Slow &) {}
void Sh4Host::jump_back(uint32_t) {}
void Sh4Host::page(uint8_t, uint32_t, Slow &) {}
void Sh4Host::read(uint32_t, Slow &) {}
void Sh4Host::write(uint32_t, uint32_t, Slow &) {}
void Sh4Host::read_at(const uint8_t *, uint32_t) {}

#endif

} /* namespace odin */
