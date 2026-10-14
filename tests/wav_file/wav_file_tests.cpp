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

/* The WAV file: its header after each write, and its samples least
 * significant byte first. Takes a folder to write in.
 *
 * Build: g++ -std=c++20 -Wall -Wextra -Wpedantic -Wconversion -Wshadow \
 *            -O2 -I src tests/wav_file/wav_file_tests.cpp \
 *            src/odin/wav_file.cpp -o wav_file_tests
 */

#include "odin/wav_file.hpp"

#include <cstdio>
#include <cstdlib>
#include <string>
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

std::vector<uint8_t>
bytes_of(const std::string &path)
{
	std::vector<uint8_t> bytes;
	if (std::FILE *file = std::fopen(path.c_str(), "rb")) {
		int c;
		while ((c = std::fgetc(file)) != EOF)
			bytes.push_back(static_cast<uint8_t>(c));
		std::fclose(file);
	}
	return bytes;
}

uint32_t
le32(const std::vector<uint8_t> &b, size_t at)
{
	return static_cast<uint32_t>(b[at] | (b[at + 1] << 8) | (b[at + 2] << 16)) | (static_cast<uint32_t>(b[at + 3]) << 24);
}

} /* namespace */

int
main(int argc, char **argv)
{
	const std::string path = std::string(argc > 1 ? argv[1] : ".") + "/wav_file_test.wav";
	{
		odin::WavFile wav(path, 44100, 2);
		check(wav.ok(), "the file opens");
		const int16_t first[] = { 1, -1, 0x1234, -0x8000 };
		wav.write(first);
		const std::vector<uint8_t> b = bytes_of(path);
		check(b.size() == 44 + 8 && le32(b, 40) == 8 && le32(b, 4) == 36 + 8,
		      "after a write, the header tells the bytes so far");
		const int16_t second[] = { 0x7FFF, 2 };
		wav.write(second);
	}
	const std::vector<uint8_t> b = bytes_of(path);
	check(b.size() == 44 + 12, "44 bytes of header, then the samples");
	check(std::string(b.begin(), b.begin() + 4) == "RIFF" && std::string(b.begin() + 8, b.begin() + 16) == "WAVEfmt " &&
	      std::string(b.begin() + 36, b.begin() + 40) == "data", "the chunks");
	check(le32(b, 4) == 36 + 12 && le32(b, 40) == 12, "the sizes");
	check(le32(b, 16) == 16 && b[20] == 1 && b[22] == 2 && le32(b, 24) == 44100 && le32(b, 28) == 44100 * 4 &&
	      b[32] == 4 && b[34] == 16, "PCM, two channels, 44 100 Hz, 16 bits");
	check(b[44] == 1 && b[45] == 0 && b[46] == 0xFF && b[47] == 0xFF && b[48] == 0x34 && b[49] == 0x12 &&
	      b[50] == 0x00 && b[51] == 0x80 && b[52] == 0xFF && b[53] == 0x7F, "the samples, least significant byte first");
	std::remove(path.c_str());
	printf("%u checks, %u failures\n", checks, failures);
	return failures ? EXIT_FAILURE : EXIT_SUCCESS;
}
