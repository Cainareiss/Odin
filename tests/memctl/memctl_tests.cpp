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

/* Unit tests for the memory controller: its registers, its windows on the
 * external bus and its transfers, with a bus that remembers what it is
 * asked.
 *
 * Build: g++ -std=c++20 -O2 -Wall -Wextra -Wpedantic -Wconversion -Wshadow \
 *            -I src tests/memctl/memctl_tests.cpp src/odin/memctl.cpp \
 *            -o memctl_tests
 * Exit status is 0 when every check passes. */

#include "odin/memctl.hpp"

#include <cstdio>
#include <map>
#include <span>
#include <string>
#include <vector>

namespace {

using odin::MemCtl;

int g_checks, g_failures;

void check(bool ok, const char *what)
{
	g_checks++;
	if (!ok) {
		g_failures++;
		std::printf("FAILED: %s\n", what);
	}
}

/* An external bus of 32-bit words, with nothing in its page 55 */
struct Bus : MemCtl::Host {
	std::map<uint32_t, uint32_t> words;
	uint32_t last_read = 0, last_written = 0;
	unsigned last_size = 0;
	int irq_changes = 0;
	int window_changes = 0;
	std::string error;

	bool bus_read(uint32_t bus_addr, unsigned size, uint64_t &value) override
	{
		last_read = bus_addr;
		last_size = size;
		if ((bus_addr >> 24) == 0x55)
			return false;
		value = words.count(bus_addr) ? words[bus_addr] : 0;
		return true;
	}
	bool bus_write(uint32_t bus_addr, unsigned size, uint64_t value) override
	{
		last_written = bus_addr;
		last_size = size;
		if ((bus_addr >> 24) == 0x55)
			return false;
		words[bus_addr] = static_cast<uint32_t>(value);
		return true;
	}
	void memctl_irq_changed() override { irq_changes++; }
	void memctl_windows_changed() override { window_changes++; }
	void memctl_error(const char *what) override { error = what; }
};

uint64_t read(MemCtl &memctl, uint32_t addr, unsigned size)
{
	uint64_t value = 0xAAAAAAAAAAAAAAAAull;

	memctl.read(addr, size, value);
	return value;
}

void test_registers()
{
	Bus bus;
	MemCtl master(bus, true), slave(bus, false);

	master.reset();
	slave.reset();
	check(read(master, 0x04000000, 4) == 0 && read(slave, 0x04000000, 4) == 0xFFFFFFFF,
	      "+00 tells the master from the slave");
	check(master.write(0x04000010, 4, 0x41400201) && read(master, 0x04000010, 4) == 0x41400201 &&
	      read(master, 0x04000012, 1) == 0x40, "a register keeps what is written, byte by byte");
	check(read(master, 0x0400003C, 4) == 0 && master.registers().size() == 0x40, "0x40 bytes of registers");

	check(read(master, 0x04000040, 4) == 0 && bus.error.find("unmapped bridge page R32 04000040") == 0,
	      "the page above the registers reads zero, and it is told");
	bus.error.clear();
	check(master.write(0x04123456, 4, 1) && read(master, 0x04000100, 2) == 0 && bus.error.empty(),
	      "once only, and a write there does nothing");
}

void test_windows()
{
	Bus bus;
	MemCtl memctl(bus, true);

	memctl.reset();
	memctl.write(0x04000018, 4, 0x70C01211);	/* 03: 70, 02: C0, 01: 12, 00: 11 */
	memctl.write(0x04000010, 4, 0x0C0A0000);	/* 17: 0C, 16: 0A */
	bus.words[0x40123454] = 0xCAFE;

	check(read(memctl, 0x02123454, 4) == 0xCAFE && bus.last_read == 0x40123454,
	      "the window at 02 opens on the page of its byte, less its top bit");
	read(memctl, 0x03000010, 2);
	check(bus.last_read == 0x70000010 && bus.last_size == 2, "the window at 03");
	read(memctl, 0x16000020, 1);
	check(bus.last_read == 0x0A000020, "the window at 16");
	memctl.write(0x17000030, 4, 0x1234);
	check(bus.last_written == 0x0C000030 && bus.words[0x0C000030] == 0x1234, "a write through the window at 17");

	memctl.write(0x0400001A, 1, 0x55);
	uint64_t value = 1;
	check(!memctl.read(0x02000000, 4, value) && value == 0, "nothing on the bus: no answer, and zero");
	check(!memctl.write(0x02000000, 4, 1), "nor to a write");
}

void test_status_and_transfer()
{
	Bus bus;
	MemCtl memctl(bus, true);

	memctl.reset();
	for (uint32_t i = 0; i < 4; i++)
		bus.words[0x10000000 + i * 4] = 0x1000 + i;

	memctl.write(0x04000030, 4, 0x90000000);	/* the top bit is not on the bus */
	memctl.write(0x04000034, 4, 0x70000100);
	check(!memctl.irq_line(), "no interrupt before a transfer");
	memctl.write(0x04000038, 4, 0x00000004);
	check(bus.words.count(0x70000100) == 0, "a length written without bit 24 starts nothing");
	memctl.write(0x04000038, 4, 0x01000004);
	check(bus.words[0x70000100] == 0x1000 && bus.words[0x7000010C] == 0x1003 && bus.words.count(0x70000110) == 0,
	      "bit 24 moves that many words at once");
	check(read(memctl, 0x04000030, 4) == 0x90000010 && read(memctl, 0x04000034, 4) == 0x70000110 &&
	      read(memctl, 0x04000038, 4) == 0, "the addresses stay at the end, the length is cleared");
	check(read(memctl, 0x04000004, 2) == 0x1000 && memctl.irq_line() && bus.irq_changes == 1,
	      "bit 12 of the status asks for the interrupt");

	memctl.write(0x04000004, 2, 0x0FFF);
	check(memctl.irq_line(), "writing the other bits does not let the line go");
	memctl.write(0x04000004, 2, 0x1000);
	check(!memctl.irq_line() && read(memctl, 0x04000004, 2) == 0 && bus.irq_changes == 3,
	      "writing bit 12 back does");
}

/* A board that moves up to three words at a time by itself, and leaves to
 * the controller the words read from or written to its page 55 */
struct BlockBus : Bus {
	std::vector<uint32_t> asked;	/* the first source of each call */

	uint32_t bus_copy(uint32_t src, uint32_t dst, uint32_t count) override
	{
		uint32_t n = 0;

		asked.push_back(src);
		while (n < count && n < 3 && ((src + 4 * n) >> 24) != 0x55 && ((dst + 4 * n) >> 24) != 0x55) {
			uint64_t value = 0;

			Bus::bus_read(src + 4 * n, 4, value);
			Bus::bus_write(dst + 4 * n, 4, value);
			n++;
		}
		return n;
	}
};

/* What the board moves itself and what it leaves: the same words, the same
 * registers, the same interrupt as a transfer word by word. The source
 * runs into page 55, which does not answer. */
void test_transfer_in_blocks()
{
	Bus single;
	BlockBus blocks;
	MemCtl one(single, true), many(blocks, true);

	for (MemCtl *memctl : { &one, &many }) {
		Bus &bus = memctl == &one ? single : blocks;

		memctl->reset();
		for (uint32_t i = 0; i < 16; i++)
			bus.words[0x54FFFFE0 + i * 4] = 0x2000 + i;
		memctl->write(0x04000030, 4, 0xD4FFFFE0);	/* the top bit is not on the bus */
		memctl->write(0x04000034, 4, 0x70000200);
		memctl->write(0x04000038, 4, 0x0100000B);	/* 11 words, 3 of them in page 55 */
	}
	check(single.words == blocks.words, "the same words as a transfer word by word");
	check(blocks.words[0x70000200] == 0x2000 && blocks.words[0x7000021C] == 0x2007 &&
	      blocks.words.count(0x70000220) && blocks.words[0x70000220] == 0, "the words of page 55 read as zero");
	for (uint32_t reg : { 0x04000030u, 0x04000034u, 0x04000038u, 0x04000004u })
		check(read(one, reg, 4) == read(many, reg, 4), "the same registers at the end");
	check(single.irq_changes == blocks.irq_changes && many.irq_line(), "the same interrupt");
	check(!blocks.asked.empty() && blocks.asked.front() == 0x54FFFFE0, "the board is asked without the top bit");
	check(blocks.asked.size() > 1 && blocks.asked[1] == 0x54FFFFEC, "and again where it stopped");

	/* Four words the board moves all of: three, then the last one */
	BlockBus all;
	MemCtl memctl(all, true);

	memctl.reset();
	for (uint32_t i = 0; i < 6; i++)
		all.words[0x10000000 + i * 4] = 0x3000 + i;
	memctl.write(0x04000030, 4, 0x10000000);
	memctl.write(0x04000034, 4, 0x70000300);
	memctl.write(0x04000038, 4, 0x01000004);
	check(all.words[0x7000030C] == 0x3003 && all.words.count(0x70000310) == 0 && read(memctl, 0x04000034, 4) == 0x70000310,
	      "no word past the length");
}

/* A bus whose pages 40 and 41 are one memory, given in spans to its end */
struct SpanBus : Bus {
	std::vector<uint8_t> memory = std::vector<uint8_t>(0x2000000);
	std::vector<uint32_t> asked;	/* bus addresses; bit 0 set for a write */

	std::span<const uint8_t> bus_read_span(uint32_t bus_addr) override { return span(bus_addr, false); }
	std::span<uint8_t> bus_write_span(uint32_t bus_addr) override { return span(bus_addr, true); }
	std::span<uint8_t> span(uint32_t bus_addr, bool write)
	{
		asked.push_back(bus_addr | (write ? 1u : 0u));
		if ((bus_addr >> 25) != 0x20)
			return {};
		return std::span<uint8_t>(memory).subspan(bus_addr & 0x1FFFFFF);
	}
};

/* A span through a window: the page of its byte, to the end of the window
 * at most; none in the page of the registers, and the bus not asked. */
void test_window_spans()
{
	SpanBus bus;
	MemCtl memctl(bus, true);

	memctl.reset();
	memctl.write(0x04000018, 4, 0x70C01211);	/* 03: 70, 02: C0 */
	memctl.write(0x04000010, 4, 0x41400000);	/* 17: 41, 16: 40 */

	std::span<const uint8_t> from = memctl.read_span(0x02FFFF00);
	check(bus.asked.size() == 1 && bus.asked[0] == 0x40FFFF00, "the window at 02 asks for its page, less its top bit");
	check(from.data() == &bus.memory[0xFFFF00] && from.size() == 0x100, "to the end of the window");

	std::span<uint8_t> to = memctl.write_span(0x02000010);
	check(bus.asked.size() == 2 && bus.asked[1] == 0x40000011, "a span to write");
	check(to.data() == &bus.memory[0x10] && to.size() == 0xFFFFF0, "to the end of that window too");

	to = memctl.write_span(0x17000000);
	check(bus.asked.size() == 3 && bus.asked[2] == 0x41000001 && to.data() == &bus.memory[0x1000000] &&
	      to.size() == 0x1000000, "the window at 17");
	check(memctl.read_span(0x03000040).empty() && bus.asked.size() == 4 && bus.asked[3] == 0x70000040,
	      "none where the bus gives none");

	bus.asked.clear();
	check(memctl.read_span(0x04000000).empty() && memctl.write_span(0x04000034).empty() &&
	      memctl.read_span(0x04000040).empty() && memctl.write_span(0x04FFFFF0).empty() && bus.asked.empty(),
	      "none in the page of the registers, and the bus not asked");
}

/* The host is told when a byte of the windows is written (12, 13 for 16
 * and 17; 1A, 1B, 1C for 02, 03 and 18), and only then. */
void test_window_changes()
{
	Bus bus;
	MemCtl memctl(bus, true);

	memctl.reset();
	bus.window_changes = 0;
	memctl.write(0x04000010, 2, 0x1111);
	memctl.write(0x04000014, 4, 0x22222222);
	memctl.write(0x04000004, 2, 0);
	memctl.write(0x04000030, 4, 0x10000000);
	memctl.write(0x04000018, 2, 0x3333);
	memctl.write(0x0400001D, 1, 0x44);
	check(bus.window_changes == 0, "not told of the other registers");
	memctl.write(0x04000010, 4, 0x41400000);
	check(bus.window_changes == 1 && memctl.window_page(0x16000000) == 0x40 && memctl.window_page(0x17FFFFFF) == 0x41,
	      "told of 12 and 13, the pages of 16 and 17");
	memctl.write(0x0400001B, 1, 0xA4);
	check(bus.window_changes == 2 && memctl.window_page(0x03000000) == 0x24, "told of 1B, the page of 03 less its top bit");
	memctl.write(0x04000018, 8, 0x0000000C00C00000ull);
	check(bus.window_changes == 3 && memctl.window_page(0x02000000) == 0x40, "told of an 8-byte write over 1A to 1C");
	memctl.write(0x0400001C, 1, 0x05);
	check(bus.window_changes == 4, "told of 1C");
	memctl.write(0x04000013, 1, 0x42);
	check(bus.window_changes == 5 && memctl.window_page(0x17000000) == 0x42, "told of 13 alone");
	memctl.write(0x04000019, 2, 0x2600);
	check(bus.window_changes == 6 && memctl.window_page(0x02000000) == 0x26, "told of a write that ends on 1A");
}

} /* namespace */

int main()
{
	test_registers();
	test_windows();
	test_status_and_transfer();
	test_transfer_in_blocks();
	test_window_spans();
	test_window_changes();

	std::printf("%d checks, %d failures\n", g_checks, g_failures);
	return g_failures ? 1 : 0;
}
