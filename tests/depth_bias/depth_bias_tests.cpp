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

/* Unit tests for the two readings of the 101 (depth_bias.hpp), on the
 * values the games send and the cases the cabinets decide.
 *
 * Build: g++ -std=c++20 -O2 -Wall -Wextra -Wpedantic -Wconversion -Wshadow -I src \
 *            tests/depth_bias/depth_bias_tests.cpp -o depth_bias_tests
 * Exit status is 0 when every check passes. */

#include "odin/host/renderer/hikaru/depth_bias.hpp"

#include <cmath>
#include <cstdio>

namespace {

namespace bias = odin::depth_bias;

int g_checks, g_failures;

void check(bool ok, const char *what)
{
	g_checks++;
	if (!ok) {
		g_failures++;
		std::printf("FAILED: %s\n", what);
	}
}

/* The 11 bits of a 101 as the list holds them: signed */
int held(unsigned raw)
{
	return static_cast<int>(raw & 0x3FF) - static_cast<int>(raw & 0x400);
}

bool near(float a, float b)
{
	return std::fabs(a - b) < 1e-6f;
}

void test_values()
{
	check(bias::test_scale(0) == 1.0f && bias::sort_scale(0) == 1.0f, "no bias, no move");
	check(held(0x700) == -256 && held(0x400) == -1024 && held(0x3FF) == 1023, "the held values");

	/* Against the other types: bits 25:16, nearer, bit 26 left out. */
	check(near(bias::test_scale(held(0x700)), 1.0f - 768 * 5.6f / 65536), "a car shadow of SGNASCAR, 768 units");
	check(bias::test_scale(held(0x400)) == 1.0f, "its track, 0 units");
	check(near(bias::test_scale(held(0x401)), 1.0f - 5.6f / 65536), "its wall, 1 unit nearer");
	check(near(bias::test_scale(held(0x1FF)), 1.0f - 511 * 5.6f / 65536), "a lamp glow of AIRTRIX, 511 units");

	/* Among the translucent fragments: 11 bits signed, 1 / 65536. */
	check(near(bias::sort_scale(held(0x500)), 1.0f + 768.0f / 65536), "a halo of PHARRIER, farther");
	check(near(bias::sort_scale(held(0x1FF)), 1.0f - 511.0f / 65536), "a lamp glow of AIRTRIX, nearer");
}

void test_cabinets()
{
	/* The shadow lies as much as 6 % of the depth behind the banked
	 * track; the cabinet shows it. */
	check(bias::test_scale(held(0x700)) * 1.06f < bias::test_scale(held(0x400)), "the shadow before its track");
	/* The wall lies 0.56 % before the stands at least; it stays so. */
	check(bias::test_scale(held(0x401)) * 0.9944f < bias::test_scale(0), "the wall before the stands");
	/* The halo lies 0.02 % before the planet; the cabinet lays it under. */
	check(bias::sort_scale(held(0x500)) * 0.9998f > bias::sort_scale(0), "the halo behind its planet");
	/* The glow lies 0.27 % behind the lamp glass and 1.1 % behind its
	 * roof; the cabinet shows it over both. */
	check(bias::sort_scale(held(0x1FF)) * 1.0027f < bias::sort_scale(0), "the glow before the glass");
	check(bias::test_scale(held(0x1FF)) * 1.011f < bias::test_scale(0), "the glow before the roof");

	bool positive = true;
	for (int b = -1024; b < 1024; b++)
		positive = positive && bias::test_scale(b) > 0.9f && bias::sort_scale(b) > 0.98f;
	check(positive, "no value moves a depth by more than a tenth");
}

} /* namespace */

int main()
{
	test_values();
	test_cabinets();
	std::printf("%d checks, %d failures\n", g_checks, g_failures);
	return g_failures ? 1 : 0;
}
