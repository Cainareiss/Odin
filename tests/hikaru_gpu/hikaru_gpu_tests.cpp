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

/* Unit tests for the wiring of the two graphics chips of the Hikaru: the
 * bus, the launch and the end of a list, the interrupt line, the reset
 * lines.
 *
 * Build: g++ -std=c++20 -O2 -Wall -Wextra -Wpedantic -Wconversion -Wshadow \
 *            -I src tests/hikaru_gpu/hikaru_gpu_tests.cpp src/odin/hikaru_gpu.cpp \
 *            src/odin/eurasia.cpp src/odin/antarctic.cpp src/odin/antarctic_ops.cpp \
 *            src/odin/antarctic_mesh.cpp src/odin/antarctic_io.cpp \
 *            src/odin/antarctic_disasm.cpp src/odin/antarctic_list.cpp \
 *            -o hikaru_gpu_tests
 * Exit status is 0 when every check passes. */

#include "odin/hikaru_gpu.hpp"

#include <cstdio>
#include <cstring>
#include <memory>
#include <vector>

namespace {

using odin::HikaruGpu;

int g_checks, g_failures;

void check(bool ok, const char *what)
{
	g_checks++;
	if (!ok) {
		g_failures++;
		std::printf("FAILED: %s\n", what);
	}
}

constexpr uint32_t LIST = 0x48000100;

/* The chips with their memories; the hooks count their calls. */
struct Board {
	std::vector<uint8_t> cmdram = std::vector<uint8_t>(4 << 20);
	std::vector<uint8_t> ram_s = std::vector<uint8_t>(32 << 20);
	std::vector<uint8_t> fb = std::vector<uint8_t>(8 << 20);
	std::vector<uint8_t> texram[2] = { std::vector<uint8_t>(4 << 20), std::vector<uint8_t>(4 << 20) };
	int irq_changes = 0, lists = 0;
	std::unique_ptr<HikaruGpu> gpu;

	Board()
	{
		HikaruGpu::Memory memory;
		HikaruGpu::Hooks hooks;

		memory.cmdram = cmdram;
		memory.ram_s = ram_s;
		memory.fb = fb;
		memory.texram[0] = texram[0];
		memory.texram[1] = texram[1];
		hooks.ctx = this;
		hooks.irq_changed = [](void *ctx) { static_cast<Board *>(ctx)->irq_changes++; };
		hooks.list_started = [](void *ctx) { static_cast<Board *>(ctx)->lists++; };
		hooks.before_texture_write = [](void *, unsigned) {};
		gpu = std::make_unique<HikaruGpu>(memory, hooks);
	}
	uint32_t read(uint32_t addr, unsigned size = 4)
	{
		uint32_t value = 0;

		gpu->read(addr, size, value);
		return value;
	}
};

void test_list()
{
	Board b;
	const uint32_t end = 0x000001C2;

	std::memcpy(&b.cmdram[LIST & 0x3FFFFF], &end, 4);
	b.gpu->antarctic().begin_list();

	b.gpu->write(0x15000084, 4, 0x72);
	b.gpu->write(0x15000070, 4, LIST);
	b.gpu->write(0x15000074, 4, 0x483FC000);
	b.gpu->write(0x1A000024, 4, 1);
	check(b.lists == 0 && b.read(0x1A000024) == 0, "1A000024 alone starts nothing and reads 0");

	b.gpu->write(0x15000058, 4, 3);
	check(b.lists == 1 && b.gpu->antarctic().running(), "15000058 = 3 starts the list");
	b.gpu->write(0x1A000024, 4, 1);
	check(b.lists == 2, "1A000024 starts it again while 15000058 = 3");

	b.gpu->run(100);
	check(!b.gpu->antarctic().running(), "the list runs to its end");
	check(b.read(0x15000088) == 0x84, "end of analysis, and a cause of the display chip");
	check(b.read(0x1A000018) == 0x4, "which is the end of the plot");
	check(b.read(0x1A000024) == 0, "the busy bit is clear");
	check(b.gpu->irq_line() && b.irq_changes > 0, "the board is told and the line is held");

	b.gpu->write(0x1A000010, 4, 1);
	check(b.read(0x15000088) == 0x04, "bit 7 goes with the cause of the display chip");
	b.gpu->write(0x15000088, 4, ~0x04u);
	check(!b.gpu->irq_line(), "the line is let go when every cause is written back");

	b.gpu->run(100);
	check(b.read(0x15000088) == 0, "nothing runs once the list has ended");
}

void test_bus()
{
	Board b;
	uint32_t value = 0;

	b.gpu->write(0x00400000, 2, 0x1234);
	check(b.read(0x00400000, 2) == 0x1234, "the word at 00400000 is kept");

	b.gpu->write(0x15000010, 4, 0x00012345);
	check(b.read(0x15000010, 2) == 0x2345 && b.read(0x15000010) == 0x00012345, "15000010 is read on 16 or 32 bits");

	check(!b.gpu->read(0x15000058, 4, value) && !b.gpu->write(0x150000FC, 4, 0),
	      "the registers that are not known are refused");
	check(b.read(0x18001000) == 0x17C711DB, "the PCI identifier of the display chip");
	check(b.gpu->read(0x15002000, 4, value) && b.gpu->read(0x15002848, 4, value) && value == 0 &&
	      b.gpu->read(0x1A08006C, 4, value) && value == 0,
	      "the board bit, the counters and 1A08006C read 0");
	check(!b.gpu->read(0x15002900, 4, value) && !b.gpu->read(0x1A080000, 4, value) &&
	      !b.gpu->write(0x15040E04, 4, 0) && !b.gpu->write(0x1A0A2000, 4, 0),
	      "what lies beside them is refused");
	check(b.gpu->write(0x1A0A1FFC, 4, 0x83709075), "the word VON4 writes to 1A0A1FFC is taken");

	b.gpu->vblank_out();
	check(b.read(0x15000088) == 0x80 && b.read(0x1A000018) == 0x2, "the vertical blanking reaches the board through bit 7");
}

/* The two reset lines of port D of the MIE. */
void test_resets()
{
	Board b;

	b.gpu->vblank_out();
	check(b.read(0x15000088) == 0x80 && !b.gpu->irq_line(), "at power-on no cause reaches the board");
	b.gpu->write(0x15000084, 4, 0x72);
	check(b.gpu->irq_line(), "the mask written, it does");

	/* PCI1_RES: the display chip alone */
	b.gpu->write(0x15000010, 4, 0x00012345);
	b.gpu->set_reset_lines(false, true);
	check(b.read(0x1A000018) == 0 && b.read(0x15000088) == 0 && !b.gpu->irq_line(),
	      "PCI1_RES puts the display chip back to power-on");
	check(b.read(0x15000010) == 0x00012345, "and leaves the command processor");
	b.gpu->vblank_out();
	check(b.read(0x1A000018) == 0, "the display chip does nothing while its line is low");
	b.gpu->set_reset_lines(false, false);
	b.gpu->vblank_out();
	check(b.read(0x1A000018) == 0x2, "it goes on when the line rises");

	/* GP_RES: every chip */
	b.gpu->write(0x15000070, 4, LIST);
	b.gpu->write(0x15000058, 4, 3);
	check(b.gpu->antarctic().running(), "a list is running");
	b.gpu->set_reset_lines(true, false);
	check(!b.gpu->antarctic().running() && b.read(0x15000010) == 0 && b.read(0x1A000018) == 0,
	      "GP_RES puts the two chips back to power-on");
	b.gpu->vblank_out();
	check(b.read(0x15000088) == 0, "nothing happens while GP_RES is low");
	b.gpu->set_reset_lines(false, false);
	b.gpu->vblank_out();
	check(b.read(0x15000088) == 0x80 && !b.gpu->irq_line(), "the mask is closed again when the chips come back");
}

} /* namespace */

int main()
{
	test_list();
	test_bus();
	test_resets();
	std::printf("%d checks, %d failures\n", g_checks, g_failures);
	return g_failures == 0 ? 0 : 1;
}
