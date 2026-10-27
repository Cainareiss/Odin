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

/* The channels of the sound output: the boards in each mode, the sum held
 * to 16 bits, the optional board at its level in the mix. The device itself
 * is not opened: tests stay silent.
 *
 * Build: g++ -std=c++20 -Wall -Wextra -Wpedantic -Wconversion -Wshadow \
 *            -O2 -I src tests/audio_out/audio_out_tests.cpp -o audio_out_tests
 */

#include "odin/host/audio/audio_out.hpp"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <vector>

namespace {

uint32_t failures = 0;
uint32_t checks = 0;

void
check(bool ok, const char *what)
{
	checks++;
	if (!ok) {
		failures++;
		printf("FAILED: %s\n", what);
	}
}

} /* namespace */

int
main()
{
	using Mode = odin::AudioOut::Mode;
	constexpr int32_t UNITY = odin::AudioOut::UNITY;
	const std::vector<int16_t> main_board = { 1, 2, 30000, -30000 };
	const std::vector<int16_t> option = { 10, 20, 10000, -10000 };
	std::vector<int16_t> out;

	odin::AudioOut::interleave(Mode::MAIN, main_board, option, UNITY, out);
	check(out == std::vector<int16_t>{ 1, 2, 30000, -30000 }, "main: the main board alone");
	odin::AudioOut::interleave(Mode::OPTION, main_board, option, UNITY, out);
	check(out == std::vector<int16_t>{ 10, 20, 10000, -10000 }, "option: the optional board alone");
	odin::AudioOut::interleave(Mode::MIX, main_board, option, UNITY, out);
	check(out == std::vector<int16_t>{ 11, 22, 32767, -32768 }, "mix: the sum, held to 16 bits");
	odin::AudioOut::interleave(Mode::QUAD, main_board, option, UNITY, out);
	check(out == std::vector<int16_t>{ 1, 2, 10, 20, 30000, -30000, 10000, -10000 },
	      "quad: front left, front right, rear left, rear right");
	odin::AudioOut::interleave(Mode::MIX, main_board, std::vector<int16_t>{}, UNITY, out);
	check(out == main_board, "mix: no optional sound, the main one");

	const int32_t half = odin::AudioOut::gain_of(-20 * std::log10(2.0));

	check(odin::AudioOut::gain_of(0) == UNITY && half == UNITY / 2 && odin::AudioOut::gain_of(-60) == 33 &&
	      odin::AudioOut::gain_of(12) == 130452, "the gain of a level in dB, in 2^-15");
	odin::AudioOut::interleave(Mode::MIX, main_board, option, half, out);
	check(out == std::vector<int16_t>{ 6, 12, 32767, -32768 }, "mix: the optional board at its level, then the sum");
	odin::AudioOut::interleave(Mode::MIX, std::vector<int16_t>{ 0, 0 }, std::vector<int16_t>{ -32768, 32767 },
	                           odin::AudioOut::gain_of(-6), out);
	check(out == std::vector<int16_t>{ -16423, 16422 }, "a lowered sample rounds down");
	odin::AudioOut::interleave(Mode::MIX, std::vector<int16_t>{ 0, 0 }, std::vector<int16_t>{ 16384, -16384 },
	                           odin::AudioOut::gain_of(12), out);
	check(out == std::vector<int16_t>{ 32767, -32768 }, "a raised sample is held to 16 bits");
	odin::AudioOut::interleave(Mode::OPTION, main_board, option, half, out);
	check(out == option, "option: the optional board as it is, whatever its level");
	odin::AudioOut::interleave(Mode::QUAD, main_board, option, half, out);
	check(out == std::vector<int16_t>{ 1, 2, 10, 20, 30000, -30000, 10000, -10000 },
	      "quad: each board as it is, whatever the level");

	using odin::AudioOut;
	const auto same = [](int16_t left, int16_t right, size_t frames) {
		std::vector<int16_t> sound;

		for (size_t i = 0; i < frames; i++)
			sound.insert(sound.end(), { left, right });
		return sound;
	};

	check(AudioOut::shake_of({}) == 0 && AudioOut::shake_of(same(0, 0, 735)) == 0, "no sound, no shake");
	check(AudioOut::shake_of(same(8192, 8192, 735)) == 0x8000, "a quarter of the way shakes half");
	check(AudioOut::shake_of(same(32767, 32767, 735)) == 0xFFFF && AudioOut::shake_of(same(-20000, -20000, 735)) == 0xFFFF,
	      "half the way and more shake all, either sign");
	check(AudioOut::shake_of(same(1023, 1023, 735)) == 0 && AudioOut::shake_of(same(1024, 1024, 735)) == 4096,
	      "below 1/32 of the way no shake");
	check(AudioOut::shake_of(same(16384, -16384, 735)) == 0 && AudioOut::shake_of(same(16384, 0, 735)) == 0x8000,
	      "left and right together");
	std::vector<int16_t> burst = same(0, 0, 736);
	for (size_t i = 0; i < 736 / 4 * 2; i++)
		burst[i] = 16384;
	check(AudioOut::shake_of(burst) == 0x8000, "the level over the whole frame");
	uint16_t shake = 0;
	for (int frame = 0; frame < 4; frame++)
		shake = AudioOut::smoothed(shake, 0xFFFF);
	check(AudioOut::smoothed(0, 0xFFFF) == 0x3FFF && shake == 0xAEFF, "a shake grows over four frames");
	for (int frame = 0; frame < 40; frame++)
		shake = AudioOut::smoothed(shake, 0);
	check(AudioOut::smoothed(0x8000, 0) == 0x6000 && shake == 0, "and dies away, down to nothing");
	printf("%u checks, %u failures\n", checks, failures);
	return failures ? EXIT_FAILURE : EXIT_SUCCESS;
}
