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

/* Checks of the X76F100 driven on its four pins the way a host
 * bit-bangs it: response to reset, a read with the read password, a
 * page write with the write password and its readback, a wrong
 * password, the erase after eight wrong passwords, an out-of-range
 * page.
 *
 * Build: g++ -std=c++20 -Wall -Wextra -Wpedantic -Wconversion -Wshadow \
 *            -O2 -I src tests/x76f100/x76f100_test.cpp \
 *            src/odin/x76f100.cpp -o x76f100_test
 * The sgnascar x76f100.ic85 dump is used when it is found, a built-in
 * image with the same layout otherwise. Exit status is 0 when every
 * check passes. */

#include "odin/x76f100.hpp"

#include <array>
#include <cstdio>
#include <cstring>
#include <vector>

namespace {

using odin::X76f100;

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

constexpr const char *IMAGE_PATH = "/home/reda/Documents/Hikaru - Valkyrie/sgnascar/x76f100.ic85";

/* Same layout and contents as that dump: default response to reset,
 * blank passwords, the ROM board id in pages 0-1, 0xFF after. */
std::vector<uint8_t> builtin_image()
{
	std::vector<uint8_t> img(X76f100::IMAGE_SIZE, 0xFF);
	const std::array<uint8_t, 4> response = { 0x19, 0x00, 0xAA, 0x55 };
	std::memcpy(img.data(), response.data(), response.size());
	std::memset(img.data() + X76f100::IMAGE_WRITE_PASSWORD, 0, 2 * X76f100::PASSWORD_SIZE);
	const char id[] = "BBAE-01A0792661G";
	std::memcpy(img.data() + X76f100::IMAGE_DATA, id, 16);
	img[X76f100::IMAGE_DATA + 16] = 0xD7;
	img[X76f100::IMAGE_DATA + 17] = 0x36;
	return img;
}

std::vector<uint8_t> load_image()
{
	FILE *f = std::fopen(IMAGE_PATH, "rb");
	if (f) {
		std::vector<uint8_t> img(X76f100::IMAGE_SIZE + 1);
		const size_t n = std::fread(img.data(), 1, img.size(), f);
		std::fclose(f);
		if (n == X76f100::IMAGE_SIZE) {
			img.resize(X76f100::IMAGE_SIZE);
			std::printf("image: %s\n", IMAGE_PATH);
			return img;
		}
	}
	std::printf("image: built-in\n");
	return builtin_image();
}

/* A bit-banging host on the four pins. Every operation leaves SCL low
 * so that SDA can move freely before the next one. */
class Wire {
public:
	explicit Wire(X76f100 &e) : m_e(e) { apply(); }

	void select(bool on) { m_cs = !on; apply(); }
	void rst(bool level) { m_rst = level; apply(); }

	/* START: SDA falls while SCL is high. */
	void start()
	{
		m_sda = true;
		apply();
		m_scl = true;
		apply();
		m_sda = false;
		apply();
		m_scl = false;
		apply();
	}

	/* STOP: SDA rises while SCL is high. */
	void stop()
	{
		m_sda = false;
		apply();
		m_scl = true;
		apply();
		m_sda = true;
		apply();
		m_scl = false;
		apply();
	}

	/* Data bit set while SCL is low, taken by the chip on the rising
	 * edge. */
	void write_bit(bool bit)
	{
		m_sda = bit;
		apply();
		m_scl = true;
		apply();
		m_scl = false;
		apply();
	}

	/* SDA released, sampled with SCL high. */
	bool read_bit()
	{
		m_sda = true;
		apply();
		m_scl = true;
		apply();
		const bool v = m_e.sda_read();
		m_scl = false;
		apply();
		return v;
	}

	/* Byte MSB first; true when the chip ACKs on the ninth clock. */
	bool write_byte(uint8_t v)
	{
		for (uint8_t i = 0; i < 8; i++) {
			write_bit(((v >> (7 - i)) & 1) != 0);
		}
		return !read_bit();
	}

	/* Byte MSB first, then the host's ACK (SDA low) or NAK. */
	uint8_t read_byte(bool ack)
	{
		uint8_t v = 0;
		for (uint8_t i = 0; i < 8; i++) {
			v = static_cast<uint8_t>((v << 1) | (read_bit() ? 1 : 0));
		}
		write_bit(!ack);
		return v;
	}

	/* Response to reset: LSB first, valid after each falling edge. */
	uint8_t read_response_byte()
	{
		uint8_t v = 0;
		for (uint8_t i = 0; i < 8; i++) {
			m_scl = true;
			apply();
			m_scl = false;
			apply();
			if (m_e.sda_read()) {
				v = static_cast<uint8_t>(v | (1u << i));
			}
		}
		return v;
	}

	/* Command, password and the 0x55 poll after a START; returns the
	 * poll's answer. Each password byte is ACKed whatever its value. */
	bool open(uint8_t command, std::span<const uint8_t> password)
	{
		select(true);
		start();
		check(write_byte(command), "command acked");
		bool acked = true;
		for (uint8_t b : password) {
			acked = write_byte(b) && acked;
		}
		check(acked, "password bytes acked");
		start();
		return write_byte(0x55);
	}

	void close()
	{
		stop();
		select(false);
	}

private:
	void apply() { m_e.set_pins(m_cs, m_rst, m_scl, m_sda); }

	X76f100 &m_e;
	bool m_cs = true;
	bool m_rst = false;
	bool m_scl = false;
	bool m_sda = true;
};

uint8_t read_command(uint8_t page) { return static_cast<uint8_t>(0x81 | (page << 1)); }
uint8_t write_command(uint8_t page) { return static_cast<uint8_t>(0x80 | (page << 1)); }

} /* namespace */

int main()
{
	const std::vector<uint8_t> image = load_image();
	const std::span<const uint8_t> read_pw(image.data() + X76f100::IMAGE_READ_PASSWORD,
	                                       X76f100::PASSWORD_SIZE);
	const std::span<const uint8_t> write_pw(image.data() + X76f100::IMAGE_WRITE_PASSWORD,
	                                        X76f100::PASSWORD_SIZE);
	const uint8_t *data = image.data() + X76f100::IMAGE_DATA;

	X76f100 chip;
	check(chip.load(image), "image of 0x84 bytes loads");
	check(!chip.load(std::span<const uint8_t>(image.data(), 16)), "image of another size refused");
	check(std::memcmp(chip.data().data(), image.data(), image.size()) == 0, "data() is the image");
	chip.reset();
	Wire w(chip);
	check(chip.sda_read(), "SDA released while deselected");

	/* Response to reset, four bytes LSB first, then over again. */
	w.select(true);
	w.rst(true);
	std::array<uint8_t, 4> response;
	for (uint8_t &b : response) {
		b = w.read_response_byte();
	}
	check(std::memcmp(response.data(), image.data(), response.size()) == 0, "response to reset");
	check(w.read_response_byte() == image[0], "response to reset repeats");
	w.rst(false);
	w.select(false);
	check(chip.sda_read(), "SDA released after deselect");

	/* Pages 0 and 1 with the read password; NAK ends the read. */
	check(w.open(read_command(0), read_pw), "read poll acked");
	std::array<uint8_t, 16> got;
	for (size_t i = 0; i < got.size(); i++) {
		got[i] = w.read_byte(i + 1 != got.size());
	}
	w.close();
	check(std::memcmp(got.data(), data, got.size()) == 0, "pages 0 and 1 read back");

	/* Page 2 written with the write password. */
	const std::array<uint8_t, 8> page = { 0x01, 0x23, 0x45, 0x67, 0x89, 0xAB, 0xCD, 0xEF };
	check(w.open(write_command(2), write_pw), "write poll acked");
	bool acked = true;
	for (uint8_t b : page) {
		acked = w.write_byte(b) && acked;
	}
	check(acked, "data bytes acked");
	w.close();
	const uint8_t *stored = chip.data().data();
	check(std::memcmp(stored + X76f100::IMAGE_DATA + 16, page.data(), page.size()) == 0,
	      "page 2 stored in the image");
	check(std::memcmp(stored, image.data(), X76f100::IMAGE_DATA + 16) == 0,
	      "nothing before page 2 changed");
	check(std::memcmp(stored + X76f100::IMAGE_DATA + 24, data + 24, X76f100::DATA_SIZE - 24) == 0,
	      "nothing after page 2 changed");

	check(w.open(read_command(2), read_pw), "read poll acked after the write");
	std::array<uint8_t, 8> back;
	for (size_t i = 0; i < back.size(); i++) {
		back[i] = w.read_byte(i + 1 != back.size());
	}
	w.close();
	check(std::memcmp(back.data(), page.data(), page.size()) == 0, "page 2 read back");

	/* Wrong password: the bytes are ACKed, the poll is not, nothing
	 * changes, and the next correct password still works. */
	std::array<uint8_t, 8> wrong;
	std::memcpy(wrong.data(), read_pw.data(), wrong.size());
	wrong[0] = static_cast<uint8_t>(~wrong[0]);
	check(!w.open(read_command(0), wrong), "wrong password poll NAKed");
	w.start();
	check(!w.write_byte(0x55), "second poll still NAKed");
	w.close();
	check(std::memcmp(chip.data().data(), stored, X76f100::IMAGE_SIZE) == 0,
	      "nothing changed after a wrong password");
	check(w.open(read_command(0), read_pw), "correct password accepted after one wrong");
	check(w.read_byte(false) == data[0], "page 0 byte 0 after one wrong password");
	w.close();

	/* Pages 14 and 15 lie past the 112 bytes: zeros come back. */
	check(w.open(read_command(14), read_pw), "page 14 poll acked");
	check(w.read_byte(false) == 0, "page 14 reads zero");
	w.close();

	/* Eight wrong passwords in a row erase passwords and data; the
	 * erase happens on the eighth password, before its poll. */
	X76f100 chip2;
	chip2.load(image);
	Wire w2(chip2);
	for (uint32_t n = 0; n < 7; n++) {
		check(!w2.open(read_command(0), wrong), "wrong password NAKed");
		w2.close();
	}
	check(std::memcmp(chip2.data().data(), image.data(), image.size()) == 0,
	      "intact after seven wrong passwords");
	check(!w2.open(read_command(0), wrong), "eighth wrong password NAKed");
	w2.close();
	std::array<uint8_t, X76f100::IMAGE_SIZE> erased;
	erased.fill(0);
	std::memcpy(erased.data(), image.data(), X76f100::RESPONSE_SIZE);
	check(std::memcmp(chip2.data().data(), erased.data(), erased.size()) == 0,
	      "passwords and data erased after eight wrong passwords");
	const std::array<uint8_t, 8> blank = {};
	check(w2.open(read_command(0), blank), "blank password accepted after the erase");
	check(w2.read_byte(false) == 0, "erased page reads zero");
	w2.close();

	std::printf("%u/%u checks passed\n", g_passed, g_passed + g_failed);
	return g_failed ? 1 : 0;
}
