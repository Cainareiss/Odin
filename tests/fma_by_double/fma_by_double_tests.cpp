/*
 *  ██████╗ ██████╗ ██╗███╗   ██╗
 * ██╔═══██╗██╔══██╗██║████╗  ██║
 * ██║   ██║██║  ██║██║██╔██╗ ██║
 * ██║   ██║██║  ██║██║██║╚██╗██║
 * ╚██████╔╝██████╔╝██║██║ ╚████║
 *  ╚═════╝ ╚═════╝ ╚═╝╚═╝  ╚═══╝
 *
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

/* Unit tests for fma_by_double, the FMA of the hosts without one, against
 * the FMA instruction of the host: every pair of a set of awkward values
 * (zeros, the smallest and largest numbers, around 1, infinities) with each
 * other, sums that cancel, sums halfway between two floats, and random
 * words. The same bits are asked for, NaN apart (a NaN for a NaN). On a
 * host without FMA there is nothing to test against.
 *
 * Build: g++ -std=c++20 -O2 -Wall -Wextra -Wpedantic -Wconversion -Wshadow \
 *            -ffp-contract=off -I src tests/fma_by_double/fma_by_double_tests.cpp \
 *            -o fma_by_double_tests
 * Exit status is 0 when every check passes. */

#include "odin/common/fma_by_double.hpp"

#include <bit>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <vector>

namespace {

int g_checks, g_failures;

[[gnu::target("fma")]] float
fma_of_host(float x, float y, float z)
{
	return __builtin_fmaf(x, y, z);
}

/* The two give the same float */
void
check(float x, float y, float z)
{
	const float want = fma_of_host(x, y, z), got = odin::fma_by_double(x, y, z);
	const bool same = std::isnan(want) ? std::isnan(got) : std::bit_cast<uint32_t>(want) == std::bit_cast<uint32_t>(got);

	g_checks++;
	if (!same && g_failures++ < 20)
		std::printf("FAILED: fma(%a, %a, %a) = %a, not %a\n", static_cast<double>(x), static_cast<double>(y),
		            static_cast<double>(z), static_cast<double>(got), static_cast<double>(want));
}

uint64_t g_seed = 0x2545F4914F6CDD1DULL;

uint32_t
next()
{
	g_seed ^= g_seed << 13;
	g_seed ^= g_seed >> 7;
	g_seed ^= g_seed << 17;
	return static_cast<uint32_t>(g_seed >> 16);
}

float
word(uint32_t w)
{
	return std::bit_cast<float>(w);
}

} /* namespace */

int
main()
{
	if (!__builtin_cpu_supports("fma")) {
		std::printf("this host has no FMA to test against: nothing tested\n");
		return 0;
	}

	/* Every triple of awkward values */
	const uint32_t awkward[] = {
		0x00000000, 0x80000000, 0x00000001, 0x80000001, 0x007FFFFF, 0x807FFFFF, 0x00800000, 0x80800000,
		0x3F800000, 0xBF800000, 0x3F800001, 0x3F7FFFFF, 0xBF7FFFFF, 0x33800000, 0x34000000, 0xB3800000,
		0x7F7FFFFF, 0xFF7FFFFF, 0x7F000000, 0x5F800000, 0x1F800000, 0x7F800000, 0xFF800000, 0x7FC00000,
		0x40490FDB, 0x3EAAAAAB, 0x4B800001, 0xCB800001, 0x0C000000, 0x8C000000,
	};
	for (const uint32_t a : awkward)
		for (const uint32_t b : awkward)
			for (const uint32_t c : awkward)
				check(word(a), word(b), word(c));

	/* Sums that nearly cancel: z near the product rounded, its ulps
	 * about */
	for (int i = 0; i < 4000000; i++) {
		const float x = word((next() & 0x7FFFFFFF) ^ (next() & 0x80000000));
		const float y = word(0x30000000 + (next() & 0x1FFFFFFF));
		const float p = x * y;
		const int32_t ulps = static_cast<int32_t>(next() % 9) - 4;
		const float z = -word(std::bit_cast<uint32_t>(p) + static_cast<uint32_t>(ulps));

		check(x, y, z);
	}

	/* Products halfway between two floats: x = 1 + a / 2^12 and y = 1 + b
	 * / 2^12, a and b odd and below 2^10, make 1 + (a + b) / 2^12 + ab /
	 * 2^24, whose last bit is half the ulp of a float there; z a little
	 * either way, which a double loses: a sum rounded twice goes the wrong
	 * way there, half the time */
	for (int i = 0; i < 4000000; i++) {
		const uint32_t a = (next() % 512) * 2 + 1, b = (next() % 512) * 2 + 1;
		const float sign = (next() & 1) ? -1.0f : 1.0f;
		const float x = sign * (1.0f + std::ldexp(static_cast<float>(a), -12));
		const float y = 1.0f + std::ldexp(static_cast<float>(b), -12);
		const float z = std::ldexp((next() & 1) ? 1.0f : -1.0f, -(static_cast<int>(next() % 16) + 54));

		check(x, y, z);
		check(x, y, 0.0f);
	}

	/* Random words, finite and not */
	for (int i = 0; i < 40000000; i++)
		check(word(next()), word(next()), word(next()));

	/* Random numbers of a size where the three meet */
	for (int i = 0; i < 40000000; i++) {
		const uint32_t sign = next() & 0x80000000, e = 0x3C000000 + (next() % 0x08000000);

		check(word(sign | (next() & 0x007FFFFF) | e), word((next() & 0x807FFFFF) | 0x3F000000),
		      word((next() & 0x807FFFFF) | (e + (next() % 0x02000000) - 0x01000000)));
	}

	std::printf("%d checks, %d failures\n", g_checks, g_failures);
	return g_failures ? 1 : 0;
}
