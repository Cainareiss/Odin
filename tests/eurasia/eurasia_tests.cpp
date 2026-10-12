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

/* Unit tests for the display chip of the Hikaru: its registers, its
 * interrupt causes, the layers and the blitter.
 *
 * Build: g++ -std=c++20 -O2 -Wall -Wextra -Wpedantic -Wconversion -Wshadow \
 *            -I src tests/eurasia/eurasia_tests.cpp src/odin/eurasia.cpp \
 *            -o eurasia_tests
 * Exit status is 0 when every check passes. */

#include "odin/eurasia.hpp"

#include <cstdio>
#include <cstring>
#include <vector>

#include "odin/antarctic_list.h"

namespace {

using odin::Eurasia;

int g_checks, g_failures;

void check(bool ok, const char *what)
{
	g_checks++;
	if (!ok) {
		g_failures++;
		std::printf("FAILED: %s\n", what);
	}
}

/* The pixel at (x, y) of the framebuffer RAM: 2048 pixels a row, the two
 * pixels of a 32-bit word swapped */
uint16_t pixel(const std::vector<uint8_t> &fb, uint32_t x, uint32_t y)
{
	uint16_t value;

	std::memcpy(&value, &fb[y * 4096 + (x ^ 1) * 2], 2);
	return value;
}

void set_pixel(std::vector<uint8_t> &fb, uint32_t x, uint32_t y, uint16_t value)
{
	std::memcpy(&fb[y * 4096 + (x ^ 1) * 2], &value, 2);
}

void test_screen()
{
	std::vector<uint8_t> fb(8 << 20);
	Eurasia chip(fb);
	uint32_t dots, lines, shown, value = 0;

	chip.frame_size(dots, lines, shown);
	check(dots == 818 && lines == 528 && shown == 480, "the frame before the game sets it");

	chip.write(0x1A000080, 800);
	chip.write(0x1A000084, 500);
	chip.write(0x1A00008C, (450u << 16) | 50);
	chip.frame_size(dots, lines, shown);
	check(dots == 800 && lines == 500 && shown == 400, "the frame as the game sets it");

	chip.end_of_line(5);
	check(chip.read(0x1A00001C, value) && value == (5u << 11), "the line counter is in bits 21:11 of 1A00001C");
	chip.vblank_in();
	check(chip.read(0x1A00001C, value) && value == ((5u << 11) | 0x01800000), "the field bits turn at the vertical blanking");

	check(chip.read(0x18001000, value) && value == 0x17C711DB, "the PCI identifier");
	check(!chip.read(0x1A000030, value), "a register that is not known is not read");
	check(chip.write(0x1A000030, 0) == Eurasia::Written::UNKNOWN, "nor written");
}

void test_causes()
{
	std::vector<uint8_t> fb(8 << 20);
	Eurasia chip(fb);
	uint32_t value = 0;

	check(!chip.irq(), "no cause at power-on");
	chip.raise(Eurasia::IRQ_VBLANK | Eurasia::IRQ_PLOT_END);
	check(chip.irq() && chip.read(0x1A000018, value) && value == 0x6, "two causes in 1A000018");
	check(chip.write(0x1A00000C, 1) == Eurasia::Written::CAUSES, "a cause is acknowledged with a 1");
	check(chip.read(0x1A000018, value) && value == 0x4, "the other one stays");
	chip.write(0x1A000010, 0);
	check(chip.read(0x1A000018, value) && value == 0x4, "a 0 acknowledges nothing");
	chip.write(0x1A000010, 1);
	check(!chip.irq(), "no cause left");

	chip.vblank_out();
	check(chip.read(0x1A000018, value) && value == 0x2, "the end of the vertical blanking is a cause");

	check(chip.write(0x1A000024, 1) == Eurasia::Written::BUSY && chip.busy() == 1, "1A000024 is told to the board");
}

void test_layers()
{
	std::vector<uint8_t> fb(8 << 20);
	Eurasia chip(fb);

	/* Layer 1 of unit 0: from (64, 512) to (384, 992), 16 bits a pixel,
	 * shown above the 3D with its pixels keyed */
	chip.write(0x1A000190, (512u << 9) | 32);
	chip.write(0x1A000194, (992u << 9) | 192);
	chip.write(0x1A0001B0, 0);
	chip.write(0x1A0001B4, 1);
	chip.write(0x1A0001B8, 1);
	chip.write(0x1A0001BC, 3);
	chip.vblank_out();

	const odin_layer_t &one = chip.layers().layer[0][0];
	const odin_layer_t &two = chip.layers().layer[0][1];
	check(one.enabled && !two.enabled, "the layers are enabled one by one");
	check(one.x0 == 64 && one.y0 == 512 && one.x1 == 384 && one.y1 == 992, "the rectangle of a 16-bit layer");
	check(one.format == ODIN_FORMAT_ABGR1555 && one.above && one.keyed, "its format, its place and its key");
	check(chip.layers().fb_keyed == 1, "the key of the 3D picture");

	chip.write(0x1A0001B0, 2);
	chip.vblank_out();
	check(chip.layers().layer[0][0].format == ODIN_FORMAT_A2BGR10 && chip.layers().layer[0][0].x0 == 128,
	      "a 32-bit layer has its columns counted in words");
}

void test_blit()
{
	std::vector<uint8_t> fb(8 << 20);
	Eurasia chip(fb);

	set_pixel(fb, 2, 1, 0x1111);
	set_pixel(fb, 3, 1, 0x2222);
	set_pixel(fb, 4, 1, 0x3333);
	chip.set_busy(1);

	chip.write(0x1A040000, (1u << 11) | 2);		/* from (2, 1) */
	chip.write(0x1A040004, (3u << 11) | 5);		/* to (5, 3) */
	chip.write(0x1A040008, (1u << 16) | 3);		/* 3 x 1 */
	chip.write(0x1A04000C, 1);

	check(pixel(fb, 5, 3) == 0x1111 && pixel(fb, 6, 3) == 0x2222 && pixel(fb, 7, 3) == 0x3333,
	      "a blit between columns of different parity follows the pixels");
	check(pixel(fb, 8, 3) == 0, "and stops at its width");
	check(chip.busy() == 0, "the blit is over at once: the busy bit is clear");
}

} /* namespace */

int main()
{
	test_screen();
	test_causes();
	test_layers();
	test_blit();
	std::printf("%d checks, %d failures\n", g_checks, g_failures);
	return g_failures == 0 ? 0 : 1;
}
