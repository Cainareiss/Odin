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
#include <string>

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

} /* namespace */

int main()
{
	test_registers();
	test_windows();
	test_status_and_transfer();

	std::printf("%d checks, %d failures\n", g_checks, g_failures);
	return g_failures ? 1 : 0;
}
