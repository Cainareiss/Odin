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

/* Checks of the 315-5881 against the self-test of Planet Harriers
 * (function 0x0C103460): the game copies 0x139E ciphertext words into
 * the protected RAM, each with its bytes swapped, writes 0 to the
 * address high and low words and 0x5784 to the sequence key, waits
 * while ready bit 0 is set, then reads 0x139C words and stores each
 * byte-swapped. The two files hold the ciphertext as the game has it
 * in memory and the plaintext it expects. MAME answers the first read
 * after an address write with a dummy 0 that is not part of the
 * plaintext (315-5881_crypt.cpp decrypt_be_r); one read is thrown
 * away before the 0x139C words, and that is the convention the
 * vectors match. Then: a second run after a new address low write
 * restarts the stream, and the registers at their offsets in the
 * window of the local bus.
 *
 * Build: g++ -std=c++20 -Wall -Wextra -Wpedantic -Wconversion -Wshadow \
 *            -O2 -I src tests/crypt_315_5881/crypt_test.cpp \
 *            src/odin/machine/chips/crypt_315_5881.cpp -o crypt_test
 * Exit status is 0 when every check passes. */

#include "odin/machine/chips/crypt_315_5881.hpp"

#include <array>
#include <cstdio>
#include <cstring>
#include <vector>

namespace {

using odin::Crypt315_5881;

uint32_t g_failed = 0;
uint32_t g_passed = 0;

void check(bool ok, const char *what)
{
	if (ok) {
		g_passed++;
	} else {
		g_failed++;
		std::printf("FAIL: %s\n", what);
	}
}

constexpr const char *INPUT_PATH =
	"/home/reda/Documents/Hikaru - Valkyrie/pharrier/pharrier-5881-input.bin";
constexpr const char *EXPECTED_PATH =
	"/home/reda/Documents/Hikaru - Valkyrie/pharrier/pharrier-5881-expected.bin";

constexpr uint32_t PHARRIER_KEY = 0x2912c68a;
constexpr uint16_t PHARRIER_SUBKEY = 0x5784;
constexpr uint32_t INPUT_WORDS = 0x139E;
constexpr uint32_t OUTPUT_WORDS = 0x139C;
constexpr size_t INPUT_SIZE = 0x3000;
constexpr size_t EXPECTED_SIZE = OUTPUT_WORDS * 2;

/* Register offsets in the window at local bus 0x0A010000. */
constexpr uint32_t REG_READY = 0x0;
constexpr uint32_t REG_ADDR_LO = 0x8;
constexpr uint32_t REG_ADDR_HI = 0xA;
constexpr uint32_t REG_SUBKEY = 0xC;
constexpr uint32_t REG_DECRYPT = 0xE;

constexpr uint16_t swap16(uint16_t v)
{
	return static_cast<uint16_t>((v << 8) | (v >> 8));
}

std::vector<uint8_t> load_file(const char *path, size_t size)
{
	std::vector<uint8_t> data;
	FILE *f = std::fopen(path, "rb");
	if (!f) {
		std::printf("cannot open %s\n", path);
		return data;
	}
	data.resize(size + 1);
	const size_t n = std::fread(data.data(), 1, data.size(), f);
	std::fclose(f);
	if (n != size) {
		std::printf("%s: %zu bytes, expected %zu\n", path, n, size);
		data.clear();
		return data;
	}
	data.resize(size);
	return data;
}

/* The 64 KB protected RAM at local bus 0x0A000000, as the SH-4 fills
 * it with little-endian word stores and as the chip reads it (MAME
 * hikaru.cpp:955): the word at (addr & 0x7fff) * 2, bytes swapped. */
class ProtRam {
public:
	ProtRam() { m_bytes.fill(0); }

	void write_word(uint32_t index, uint16_t v)
	{
		m_bytes[index * 2] = static_cast<uint8_t>(v);
		m_bytes[index * 2 + 1] = static_cast<uint8_t>(v >> 8);
	}

	uint16_t chip_read(uint32_t addr) const
	{
		const uint32_t off = (addr & 0x7fff) * 2;
		const uint16_t le = static_cast<uint16_t>(m_bytes[off] | (m_bytes[off + 1] << 8));
		return swap16(le);
	}

private:
	std::array<uint8_t, 0x10000> m_bytes;
};

uint16_t le16(const std::vector<uint8_t> &v, size_t off)
{
	return static_cast<uint16_t>(v[off] | (v[off + 1] << 8));
}

void put_le16(std::vector<uint8_t> &v, size_t off, uint16_t w)
{
	v[off] = static_cast<uint8_t>(w);
	v[off + 1] = static_cast<uint8_t>(w >> 8);
}

/* Loads the ciphertext the way the game does, swapping the bytes of
 * each word or not. */
void fill_ram(ProtRam &ram, const std::vector<uint8_t> &input, bool swap_in)
{
	for (uint32_t i = 0; i < INPUT_WORDS; i++) {
		const uint16_t w = le16(input, i * 2);
		ram.write_word(i, swap_in ? swap16(w) : w);
	}
}

/* The game's register sequence, then `words` reads stored the way the
 * game stores them, swapped or not. `dummy` first throws one read
 * away. */
std::vector<uint8_t> run_sequence(Crypt315_5881 &chip, uint32_t words, bool swap_out, bool dummy)
{
	chip.write_addr_hi(0);
	chip.write_addr_lo(0);
	chip.write_subkey(PHARRIER_SUBKEY);
	uint32_t polls = 0;
	while ((chip.ready() & 1) != 0 && polls < 1000000) {
		polls++;
	}
	if (dummy) {
		(void)chip.read_decrypt();
	}
	std::vector<uint8_t> out(static_cast<size_t>(words) * 2);
	for (uint32_t i = 0; i < words; i++) {
		const uint16_t w = chip.read_decrypt();
		put_le16(out, static_cast<size_t>(i) * 2, swap_out ? swap16(w) : w);
	}
	return out;
}

std::vector<uint8_t> run_pharrier(const std::vector<uint8_t> &input, bool swap_in, bool swap_out, bool dummy)
{
	ProtRam ram;
	fill_ram(ram, input, swap_in);
	Crypt315_5881 chip(PHARRIER_KEY);
	chip.set_read([&ram](uint32_t addr) { return ram.chip_read(addr); });
	return run_sequence(chip, OUTPUT_WORDS, swap_out, dummy);
}

/* First differing word, or the word count when equal. */
uint32_t first_difference(const std::vector<uint8_t> &a, const std::vector<uint8_t> &b)
{
	const size_t n = a.size() < b.size() ? a.size() : b.size();
	for (size_t off = 0; off + 1 < n; off += 2) {
		if (le16(a, off) != le16(b, off)) {
			return static_cast<uint32_t>(off / 2);
		}
	}
	return static_cast<uint32_t>(n / 2);
}

struct Variant {
	bool swap_in;
	bool swap_out;
	bool dummy;
	const char *name;
};

constexpr std::array<Variant, 8> VARIANTS = {{
	{true, true, false, "input swapped, output swapped"},
	{false, true, false, "input as is, output swapped"},
	{true, false, false, "input swapped, output as is"},
	{false, false, false, "input as is, output as is"},
	{true, true, true, "dummy read first, input swapped, output swapped"},
	{false, true, true, "dummy read first, input as is, output swapped"},
	{true, false, true, "dummy read first, input swapped, output as is"},
	{false, false, true, "dummy read first, input as is, output as is"},
}};

} /* namespace */

int main()
{
	const std::vector<uint8_t> input = load_file(INPUT_PATH, INPUT_SIZE);
	const std::vector<uint8_t> expected = load_file(EXPECTED_PATH, EXPECTED_SIZE);
	check(!input.empty() && !expected.empty(), "vector files loaded");
	if (input.empty() || expected.empty()) {
		std::printf("%u/%u checks passed\n", g_passed, g_passed + g_failed);
		return 1;
	}

	/* The self-test as the game runs it, the dummy word thrown away. */
	const std::vector<uint8_t> out = run_pharrier(input, true, true, true);
	const uint32_t diff = first_difference(out, expected);
	check(diff == OUTPUT_WORDS, "Planet Harriers self-test: 0x139C words match");
	if (diff != OUTPUT_WORDS) {
		std::printf("  first difference at word %#x: got %04x, expected %04x\n", diff,
		            le16(out, static_cast<size_t>(diff) * 2), le16(expected, static_cast<size_t>(diff) * 2));
		for (const Variant &v : VARIANTS) {
			const std::vector<uint8_t> o = run_pharrier(input, v.swap_in, v.swap_out, v.dummy);
			const uint32_t d = first_difference(o, expected);
			std::printf("  variant %-52s: %s (first difference at word %#x)\n", v.name,
			            d == OUTPUT_WORDS ? "MATCH" : "no", d);
		}
	}

	/* A new address low write restarts the stream: the second run
	 * gives the same plaintext. */
	{
		ProtRam ram;
		fill_ram(ram, input, true);
		Crypt315_5881 chip(PHARRIER_KEY);
		chip.set_read([&ram](uint32_t addr) { return ram.chip_read(addr); });
		const std::vector<uint8_t> first = run_sequence(chip, OUTPUT_WORDS, true, true);
		chip.write_addr_lo(0);
		std::vector<uint8_t> second(64);
		check(chip.read_decrypt() == 0, "restart: dummy word after the address write");
		for (uint32_t i = 0; i < 32; i++) {
			put_le16(second, static_cast<size_t>(i) * 2, swap16(chip.read_decrypt()));
		}
		check(std::memcmp(first.data(), second.data(), second.size()) == 0,
		      "restart: the stream begins again after a new address low write");
		/* Without the address write the stream goes on past the block. */
		Crypt315_5881 chip2(PHARRIER_KEY);
		chip2.set_read([&ram](uint32_t addr) { return ram.chip_read(addr); });
		(void)run_sequence(chip2, 16, true, true);
		std::vector<uint8_t> more(64);
		for (uint32_t i = 0; i < 32; i++) {
			put_le16(more, static_cast<size_t>(i) * 2, swap16(chip2.read_decrypt()));
		}
		check(std::memcmp(first.data() + 32, more.data(), more.size()) == 0,
		      "no address write: the stream continues where it was");
	}

	/* The registers at their offsets on the bus. */
	{
		ProtRam ram;
		fill_ram(ram, input, true);
		Crypt315_5881 chip(PHARRIER_KEY);
		chip.set_read([&ram](uint32_t addr) { return ram.chip_read(addr); });
		chip.reset();
		chip.bus_write(REG_ADDR_HI, 2, 0);
		chip.bus_write(REG_ADDR_LO, 2, 0);
		chip.bus_write(REG_SUBKEY, 2, PHARRIER_SUBKEY);
		check((chip.bus_read(REG_READY, 2) & 1) == 0, "registers: ready");
		check(chip.bus_read(REG_DECRYPT, 2) == 0, "registers: dummy word first");
		std::vector<uint8_t> words(EXPECTED_SIZE);
		for (uint32_t i = 0; i < OUTPUT_WORDS; i++) {
			const uint16_t w = static_cast<uint16_t>(chip.bus_read(REG_DECRYPT, 2));
			put_le16(words, static_cast<size_t>(i) * 2, swap16(w));
		}
		check(first_difference(words, expected) == OUTPUT_WORDS,
		      "registers: Planet Harriers self-test through the register offsets");
		FILE *devnull = std::fopen("/dev/null", "w");
		chip.set_log(devnull);
		check(chip.bus_read(0x4, 2) == 0, "registers: unmapped offset reads 0");
		check(chip.bus_read(REG_DECRYPT, 4) == 0, "registers: 32-bit access reads 0");
		chip.bus_write(0x6, 2, 0x1234);
		if (devnull) {
			std::fclose(devnull);
		}
	}

	std::printf("%u/%u checks passed\n", g_passed, g_passed + g_failed);
	return g_failed ? 1 : 0;
}
