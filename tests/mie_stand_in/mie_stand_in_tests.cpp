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

/* Unit tests for the stand-in of the MIE: its registers, the bus request,
 * and what it leaves in the shared RAM for the BOOTROM and the games.
 *
 * Build: g++ -std=c++20 -O2 -Wall -Wextra -Wpedantic -Wconversion -Wshadow \
 *            -I src tests/mie_stand_in/mie_stand_in_tests.cpp \
 *            src/odin/mie_stand_in.cpp -o mie_stand_in_tests
 * Exit status is 0 when every check passes. */

#include "odin/mie_stand_in.hpp"

#include <cstdio>
#include <cstring>
#include <vector>

namespace {

using odin::MieStandIn;

int g_checks, g_failures;

void check(bool ok, const char *what)
{
	g_checks++;
	if (!ok) {
		g_failures++;
		std::printf("FAILED: %s\n", what);
	}
}

/* The window of the RAM in the SH-4: a byte of the Z80 every two bytes */
uint32_t window(uint32_t z80_addr)
{
	return 0x00830000 + (z80_addr - 0x8000) * 2;
}

uint64_t read(MieStandIn &mie, uint32_t addr, unsigned size, bool test = false, bool service = false)
{
	uint64_t value = 0xBAD;

	return mie.read(addr, size, value, test, service) ? value : 0xDEAD;
}

void test_registers()
{
	std::vector<uint8_t> ram(0x8000);
	MieStandIn mie(ram);
	uint64_t value;

	mie.reset();
	check(read(mie, 0x00800000, 2) == 0xFFFF, "the first register reads FFFF after a reset");
	check(!mie.read(0x00800000, 4, value, false, false) && !mie.read(0x00800002, 2, value, false, false),
	      "a register is 16 bits wide, and only some are read");
	check(read(mie, 0x00800008, 2) == 0xF && read(mie, 0x00800008, 2) == 0,
	      "the register at +08 flips its low four bits at each read");
	check(read(mie, 0x0080000C, 2) == 0xFFFF && read(mie, 0x0080000C, 2, true, false) == 0xFFF7 &&
	      read(mie, 0x0080000C, 2, false, true) == 0xFFFB,
	      "the switches of the main board at +0C, low when pressed");
	check(mie.write(0x00800010, 2, 0x1234, false, false) && !mie.write(0x00800012, 2, 0, false, false) &&
	      !mie.write(0x00800010, 4, 0, false, false), "the registers that take a write");
	check(mie.registers().size() == MieStandIn::NUM_REGS, "32 bytes of registers go to the save states");
	check(!mie.read(0x00900000, 2, value, false, false) && !mie.write(0x00900000, 2, 0, false, false),
	      "nothing outside its two windows");
}

void test_bus_request()
{
	std::vector<uint8_t> ram(0x8000);
	MieStandIn mie(ram);

	mie.reset();
	check(read(mie, 0x01000000, 4) == 0x700, "the bus is free at first");
	mie.write(0x01000000, 4, 0x100, false, false);
	check(read(mie, 0x01000000, 4) == 0x600 && mie.busrq() == 0x100, "asked for, it is given at once");
	mie.write(0x010001FC, 4, 0, false, false);
	check(read(mie, 0x010001FC, 4) == 0x700, "and given back");
}

void test_shared_ram()
{
	static const char id[] = "SEGA ENTERPRISES,LTD.;I/O BD JVS;837-13551";
	std::vector<uint8_t> ram(0x8000);
	MieStandIn mie(ram);

	mie.reset();
	mie.write(window(0x9000), 2, 0x5A, false, false);
	check(ram[0x1000] == 0x5A && read(mie, window(0x9000), 2) == 0x5A, "the RAM is written and read through the window");

	check(read(mie, 0x0082F000, 2) == 0 && ram[0x4004] == 0, "nothing is ready before the Z80 is started");
	mie.write(0x0082F000, 2, 0x80, true, false);
	check(ram[0x4004] == 3 && ram[0x4002] == 1 && ram[0x4001] == 0, "started: stage 3, one JVS node, not busy");
	check(ram[0x4005] == 0x40, "with the switches of the main board: Test is bit 6");
	check(ram[0x4100] == 1 && !std::memcmp(&ram[0x417C], id, sizeof(id)), "the record of the 837-13551 and its name");
	check(ram[0x6000] == 1 && ram[0x6002] == 6 && ram[0x6102] == 5 && ram[0x6202] == 17 &&
	      ram[0x6203] == 0x80 && ram[0x6204] == 0, "the pages of the switches, coins and analog channels, centred");

	ram[0x6003] = 0xFF;
	mie.write(window(0xC000), 2, 1, false, true);
	check(ram[0x6003] == 0 && ram[0x4005] == 0x80 && ram[0x4000] == 0,
	      "command 1 polls: the pages are written again, Service is bit 7, the command is taken");
	ram[0x4004] = 0;
	mie.write(window(0xC000), 2, 2, false, false);
	check(ram[0x4004] == 3 && ram[0x4000] == 0, "command 2 resets JVS and comes back ready");
	mie.write(window(0xC001), 2, 1, false, false);
	check(ram[0x4001] == 0, "the busy flag never stays");
}

} /* namespace */

int main()
{
	test_registers();
	test_bus_request();
	test_shared_ram();

	std::printf("%d checks, %d failures\n", g_checks, g_failures);
	return g_failures ? 1 : 0;
}
