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

/* The small machine the tests of the SH-4 core run on (a RAM, one device,
 * port A), the checks they count, the programs they share, and a pair of
 * machines that run one program read and as host code. Used by tests/sh4
 * and tests/sh4_host. */

#ifndef ODIN_TESTS_SH4_MACHINE_HPP
#define ODIN_TESTS_SH4_MACHINE_HPP

#include "odin/sh4.hpp"
#include "odin/sh4_fsca.hpp"

#include <algorithm>
#include <bit>
#include <cstdio>
#include <cstring>
#include <functional>
#include <initializer_list>
#include <span>
#include <string>
#include <vector>

namespace sh4_test {


using odin::Sh4;
using odin::Sh4State;
namespace Source = odin::Sh4Source;

inline int g_checks, g_failures;
inline bool g_verbose;
inline const char *g_test = "";

inline void check(bool ok, const std::string &what)
{
	g_checks++;
	if (!ok) {
		g_failures++;
		std::printf("FAIL %s: %s\n", g_test, what.c_str());
	} else if (g_verbose) {
		std::printf("ok   %s: %s\n", g_test, what.c_str());
	}
}

inline void eq(const char *name, uint64_t got, uint64_t want)
{
	char text[128];
	std::snprintf(text, sizeof(text), "%s = %llX, expected %llX", name,
	              static_cast<unsigned long long>(got), static_cast<unsigned long long>(want));
	check(got == want, text);
}

constexpr uint32_t RAM = 0x0C000000;	/* 1 MB of plain memory */
constexpr uint32_t CODE = RAM + 0x1000;
constexpr uint32_t DATA = RAM + 0x8000;
constexpr uint32_t DEVICE = 0x04000000;	/* 4 KB the bus answers */
constexpr uint32_t WINDOW = 0x02000000;	/* 64 KB the bus answers, and gives in spans */
constexpr uint32_t SR_MD = 0x40000000, SR_RB = 0x20000000, SR_BL = 0x10000000;

inline uint32_t bits(float v) { return std::bit_cast<uint32_t>(v); }

/* A RAM, one device, a window and port A around the CPU. */
class Machine : public odin::Sh4Bus {
public:
	struct Access {
		bool write;
		uint32_t addr, size;
		uint64_t value;
	};

	std::vector<uint8_t> ram;
	std::vector<uint8_t> onchip;
	std::vector<Access> device, unanswered_log;
	std::vector<uint8_t> window = std::vector<uint8_t>(0x10000, 0);
	unsigned window_by_bus = 0;	/* accesses to the window the bus took */
	size_t span_limit = 0;		/* the longest span of the window, 0 for none */
	std::vector<Access> spans;	/* the spans asked for: write, address */
	std::vector<uint32_t> seen;	/* what a test notes */
	unsigned ram_by_bus = 0;	/* accesses to the RAM the bus took */
	uint32_t rom = 0;		/* a page of the RAM whose writes the bus drops */
	uint64_t device_value = 0;
	uint16_t pins_in = 0, pins_out = 0;
	unsigned hooks = 0;
	std::function<void()> on_hook, on_device;
	Sh4 cpu;

	Machine() : ram(1u << 20, 0), onchip(0x10000, 0), cpu(*this, onchip, true, true, 5)
	{
		/* NOP wherever a program may go */
		for (uint32_t at = RAM; at < DATA; at += 2) {
			put16(at, 0x0009);
		}
		for (uint32_t page = 0; page < ram.size() / Sh4::PAGE_SIZE; page++) {
			uint8_t *host = &ram[page * Sh4::PAGE_SIZE];
			cpu.map_page(RAM / Sh4::PAGE_SIZE + page, host, host);
		}
		/* A running CPU in privileged mode, bank 0, interrupts open. */
		Sh4State st = state();
		st.run = odin::Sh4Run::Running;
		st.pc = CODE;
		st.sr = SR_MD;
		st.r[15] = RAM + 0xF000;
		cpu.load(st);
	}

	bool read(uint32_t addr, uint32_t size, uint64_t &value) override
	{
		if (addr >= (RAM & 0x1FFFFFFF) && addr + size <= (RAM & 0x1FFFFFFF) + ram.size()) {
			value = 0;
			std::memcpy(&value, &ram[addr - (RAM & 0x1FFFFFFF)], size);
			ram_by_bus++;
			return true;
		}
		if (addr >= WINDOW && addr + size <= WINDOW + window.size()) {
			value = 0;
			std::memcpy(&value, &window[addr - WINDOW], size);
			window_by_bus++;
			return true;
		}
		if ((addr & ~0xFFFu) != DEVICE) {
			return false;
		}
		device.push_back({false, addr, size, 0});
		value = device_value;
		if (on_device) {
			on_device();
		}
		return true;
	}
	bool write(uint32_t addr, uint32_t size, uint64_t value) override
	{
		if (rom && (addr & ~0xFFFu) == (rom & 0x1FFFFFFF)) {
			return true;
		}
		if (addr >= (RAM & 0x1FFFFFFF) && addr + size <= (RAM & 0x1FFFFFFF) + ram.size()) {
			std::memcpy(&ram[addr - (RAM & 0x1FFFFFFF)], &value, size);
			ram_by_bus++;
			return true;
		}
		if (addr >= WINDOW && addr + size <= WINDOW + window.size()) {
			std::memcpy(&window[addr - WINDOW], &value, size);
			window_by_bus++;
			return true;
		}
		if ((addr & ~0xFFFu) != DEVICE) {
			return false;
		}
		device.push_back({true, addr, size, value});
		if (on_device) {
			on_device();
		}
		return true;
	}
	std::span<const uint8_t> read_span(uint32_t addr) override { return window_span(false, addr); }
	std::span<uint8_t> write_span(uint32_t addr) override { return window_span(true, addr); }
	std::span<uint8_t> window_span(bool is_write, uint32_t addr)
	{
		if (!span_limit || addr < WINDOW || addr >= WINDOW + window.size()) {
			return {};
		}
		spans.push_back({is_write, addr, 0, 0});
		const size_t at = addr - WINDOW;

		return std::span<uint8_t>(window).subspan(at, std::min(span_limit, window.size() - at));
	}
	uint16_t port_a_read() override { return pins_in; }
	void port_a_write(uint16_t pins) override { pins_out = pins; }
	void unanswered(bool is_write, uint32_t addr, uint32_t size, uint64_t value) override
	{
		unanswered_log.push_back({is_write, addr, size, value});
	}
	void hook() override
	{
		hooks++;
		if (on_hook) {
			on_hook();
		}
	}

	Sh4State state() const
	{
		Sh4State st;
		cpu.save(st);
		return st;
	}
	void code(uint32_t addr, std::initializer_list<uint16_t> words)
	{
		for (uint16_t w : words) {
			put16(addr, w);
			addr += 2;
		}
	}
	void put16(uint32_t addr, uint16_t v) { std::memcpy(&ram[addr - RAM], &v, 2); }
	void put32(uint32_t addr, uint32_t v) { std::memcpy(&ram[addr - RAM], &v, 4); }
	uint8_t get8(uint32_t addr) const { return ram[addr - RAM]; }
	uint16_t get16(uint32_t addr) const
	{
		uint16_t v;
		std::memcpy(&v, &ram[addr - RAM], 2);
		return v;
	}
	uint32_t get32(uint32_t addr) const
	{
		uint32_t v;
		std::memcpy(&v, &ram[addr - RAM], 4);
		return v;
	}
	void irlm()	/* ICR.IRLM: the IRL pins are four requests */
	{
		onchip[0xD000] |= 0x80;
	}
	uint32_t reg32(uint32_t addr) const	/* an on-chip register, as the image keeps it */
	{
		uint32_t at = ((addr >> 8) & 0xFF00) | (addr & 0xFF), v;
		std::memcpy(&v, &onchip[at], 4);
		return v;
	}
	void set_reg32(uint32_t addr, uint32_t v)
	{
		std::memcpy(&onchip[((addr >> 8) & 0xFF00) | (addr & 0xFF)], &v, 4);
	}
};

/* Runs `words` at CODE for `cycles` cycles from the state `set` prepares,
 * and gives the state after. */
inline Sh4State run(Machine &m, std::initializer_list<uint16_t> words, int32_t cycles,
             const std::function<void(Sh4State &)> &set = nullptr)
{
	m.code(CODE, words);
	if (set) {
		Sh4State st = m.state();
		set(st);
		m.cpu.load(st);
	}
	m.cpu.run(cycles);
	return m.state();
}

constexpr uint16_t NOP = 0x0009;

/* The device raises IRL1 on one read in five and drops it on a write at
 * its second word; the handler, at VBR + 0x600, writes there and returns.
 * R12 holds the device, R14 the data, R13 the code. */
inline void with_device(Machine &m)
{
	Machine *self = &m;

	m.irlm();
	m.on_device = [self] {
		const Machine::Access &last = self->device.back();

		self->seen.push_back(self->cpu.pc());
		if (last.write && last.addr == DEVICE + 4) {
			self->cpu.set_irq(Source::IRL1, false);
		} else if (!last.write && self->device.size() % 5 == 0) {
			self->cpu.set_irq(Source::IRL1, true);
		}
	};
	m.code(RAM + 0x7000, { 0x1C01, 0x002B, 0x0009 });	/* mov.l r0, @(4, r12) / rte / nop */
	Sh4State st = m.state();
	st.vbr = RAM + 0x7000 - 0x600;
	st.r[12] = DEVICE;
	st.r[13] = CODE;
	st.r[14] = DATA;
	m.cpu.load(st);
}

/* A program of `slots` instructions picked from a fixed series: sums and
 * moves, memory, the device, every kind of branch within the program and
 * delay loops, then back to its start. */
inline void random_program(Machine &m, uint32_t slots, uint32_t seed)
{
	auto next = [&seed]() {
		seed = seed * 1664525u + 1013904223u;
		return seed >> 8;
	};
	auto plain = [&]() -> uint16_t {
		const uint32_t n = next() % 8, r = next() % 8, d = next() % 16;

		switch (next() % 24) {
		case 0: return static_cast<uint16_t>(0xE000 | (n << 8) | (next() & 0xFF));	/* mov #imm, rn */
		case 1: case 2: return static_cast<uint16_t>(0x7000 | (n << 8) | (next() & 0xFF));	/* add #imm, rn */
		case 3: return static_cast<uint16_t>(0x300C | (n << 8) | (r << 4));	/* add rm, rn */
		case 4: return static_cast<uint16_t>(0x6003 | (n << 8) | (r << 4));	/* mov rm, rn */
		case 5: return static_cast<uint16_t>(0x3000 | (n << 8) | (r << 4));	/* cmp/eq rm, rn */
		case 6: return static_cast<uint16_t>(0x3006 | (n << 8) | (r << 4));	/* cmp/hi rm, rn */
		case 7: return static_cast<uint16_t>(0x2008 | (n << 8) | (r << 4));	/* tst rm, rn */
		case 8: return static_cast<uint16_t>(0x200A | (n << 8) | (r << 4));	/* xor rm, rn */
		case 9: return static_cast<uint16_t>(0x3008 | (n << 8) | (r << 4));	/* sub rm, rn */
		case 10: return static_cast<uint16_t>(0x4000 | (n << 8));		/* shll rn */
		case 11: return static_cast<uint16_t>(0x4001 | (n << 8));		/* shlr rn */
		case 12: return static_cast<uint16_t>(0x300E | (n << 8) | (r << 4));	/* addc rm, rn */
		case 13: case 14: return static_cast<uint16_t>(0x1E00 | (r << 4) | d);	/* mov.l rm, @(d, r14) */
		case 15: case 16: return static_cast<uint16_t>(0x50E0 | (n << 8) | d);	/* mov.l @(d, r14), rn */
		case 17: return static_cast<uint16_t>(0x0007 | (n << 8) | (r << 4));	/* mul.l rm, rn */
		case 18: return static_cast<uint16_t>(0x001A | (n << 8));		/* sts macl, rn */
		case 19: return static_cast<uint16_t>(0x60C2 | (n << 8));		/* mov.l @r12, rn: the device */
		case 20: return static_cast<uint16_t>(0x2C02 | (r << 4));		/* mov.l rm, @r12 */
		case 21: return static_cast<uint16_t>(0x0002 | (n << 8));		/* stc sr, rn */
		case 22: return static_cast<uint16_t>(0x600E | (n << 8) | (r << 4));	/* exts.b rm, rn */
		default: return static_cast<uint16_t>(0x6007 | (n << 8) | (r << 4));	/* not rm, rn */
		}
	};
	auto reach = [&](uint32_t from, uint32_t span) {	/* a slot near `from`, as a displacement */
		const int32_t to = static_cast<int32_t>(from) - static_cast<int32_t>(span) + static_cast<int32_t>(next() % (2 * span));
		const int32_t slot = to < 0 ? 0 : (to >= static_cast<int32_t>(slots) ? static_cast<int32_t>(slots) - 1 : to);

		return slot - static_cast<int32_t>(from) - 2;
	};

	for (uint32_t i = 0; i < slots; i++) {
		const uint32_t at = CODE + 2 * i, kind = next() % 100;

		if (kind < 70 || i + 3 >= slots) {
			m.put16(at, plain());
		} else if (kind < 78) {		/* bt, bf */
			m.put16(at, static_cast<uint16_t>(((next() & 1) ? 0x8900 : 0x8B00) | (reach(i, 30) & 0xFF)));
		} else if (kind < 88) {		/* bt/s, bf/s and the slot */
			m.put16(at, static_cast<uint16_t>(((next() & 1) ? 0x8D00 : 0x8F00) | (reach(i, 30) & 0xFF)));
			m.put16(at + 2, plain());
			i++;
		} else if (kind < 93) {		/* bra and the slot */
			m.put16(at, static_cast<uint16_t>(0xA000 | (reach(i, 200) & 0xFFF)));
			m.put16(at + 2, plain());
			i++;
		} else if (kind < 96) {		/* bsr and the slot */
			m.put16(at, static_cast<uint16_t>(0xB000 | (reach(i, 200) & 0xFFF)));
			m.put16(at + 2, plain());
			i++;
		} else {			/* a delay loop: mov #imm, rn / dt rn / bf back */
			const uint32_t n = next() % 8;

			m.put16(at, static_cast<uint16_t>(0xE000 | (n << 8) | (1 + next() % 100)));
			m.put16(at + 2, static_cast<uint16_t>(0x4010 | (n << 8)));
			m.put16(at + 4, 0x8BFD);
			i += 2;
		}
	}
	/* Back to the start: bra / nop */
	m.put16(CODE + 2 * slots, static_cast<uint16_t>(0xA000 | ((0u - slots - 2u) & 0xFFF)));
	m.put16(CODE + 2 * slots + 2, 0x0009);
}

/* Host code: the same program on two machines, one that reads its blocks
 * and one that runs them as host code, in slices of every length. After
 * each slice the two must be the same: registers, on-chip state, memory,
 * what the bus saw, the cycles run over and the counts. */
struct Pair {
	Machine read, host;
	unsigned slices = 0, differ = 0;

	Pair() { host.cpu.set_translate(true); }
	void each(const std::function<void(Machine &)> &set)
	{
		set(read);
		set(host);
	}
	static bool same_log(const std::vector<Machine::Access> &a, const std::vector<Machine::Access> &b)
	{
		if (a.size() != b.size()) {
			return false;
		}
		for (size_t i = 0; i < a.size(); i++) {
			if (a[i].write != b[i].write || a[i].addr != b[i].addr || a[i].size != b[i].size ||
			    a[i].value != b[i].value) {
				return false;
			}
		}
		return true;
	}
	void slice(int32_t cycles)
	{
		const int32_t over_read = read.cpu.run(cycles), over_host = host.cpu.run(cycles);
		const Sh4State a = read.state(), b = host.state();

		slices++;
		if (over_read != over_host || std::memcmp(&a, &b, sizeof(a)) != 0 || read.ram != host.ram ||
		    read.onchip != host.onchip || !same_log(read.device, host.device) ||
		    !same_log(read.unanswered_log, host.unanswered_log) || read.seen != host.seen ||
		    read.cpu.instructions() != host.cpu.instructions() || read.cpu.skipped() != host.cpu.skipped()) {
			differ++;
		}
	}
	/* `total` cycles in slices of 1 to `longest`, from a fixed series,
	 * or as many as the two stay the same for. */
	void slices_of(uint32_t total, uint32_t longest, uint32_t seed)
	{
		for (uint32_t done = 0; done < total && differ == 0;) {
			seed = seed * 1664525u + 1013904223u;
			const uint32_t cycles = 1 + (seed >> 8) % longest;
			slice(static_cast<int32_t>(cycles));
			done += cycles;
		}
	}
	void verdict(const std::string &what)
	{
		check(slices > 0 && differ == 0, what + ": read and as host code, the same after each slice");
		check(host.cpu.instructions() > 0, what + ": the program ran");
	}
};

} /* namespace sh4_test */

#endif /* ODIN_TESTS_SH4_MACHINE_HPP */
