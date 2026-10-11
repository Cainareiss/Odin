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

/* Checks of the 93C46 against its data sheet instruction set, driven
 * the way the MIE program bit-bangs it (start bit, 2 opcode bits, 7
 * address bits in x8 mode, DO sampled after each SK rising edge, READY
 * polled on DO after a write), and of the JVS frame layer with the
 * 837-13551 board behind it (sync, escapes, checksum, reset, address,
 * identification, polls).
 *
 * Build: g++ -std=c++20 -Wall -Wextra -Wpedantic -Wconversion -Wshadow \
 *            -O2 -I src tests/mie/mie_test.cpp src/odin/eeprom_93c46.cpp \
 *            src/odin/jvs_host.cpp src/odin/jvs_io_13551.cpp src/odin/mie.cpp \
 *            src/odin/mie_bridge.cpp src/odin/card_reader.cpp src/odin/z80.cpp \
 *            src/odin/z80_ops.cpp -o mie_test
 * Exit status is 0 when every check passes. */

#include "odin/eeprom_93c46.hpp"
#include "odin/jvs_host.hpp"
#include "odin/jvs_io_13551.hpp"
#include "odin/mie_bridge.hpp"

#include <cstdio>
#include <cstring>
#include <vector>

namespace {

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

/* 93C46 on three pins, x8 mode unless said otherwise. */
class Wire {
public:
	explicit Wire(odin::Eeprom93c46 &e, bool org = false) : m_e(e), m_org(org) {}

	void cs(bool level) { m_cs = level; m_sk = false; apply(); }
	/* One clock with DI at `bit`; returns DO after the rising edge. */
	bool clock(bool bit)
	{
		m_di = bit;
		m_sk = false;
		apply();
		m_sk = true;
		apply();
		return m_e.do_read();
	}
	void bits(uint32_t value, uint8_t count)
	{
		for (uint8_t i = 0; i < count; i++) {
			clock(((value >> (count - 1 - i)) & 1) != 0);
		}
	}
	uint16_t read_bits(uint8_t count)
	{
		uint16_t v = 0;
		for (uint8_t i = 0; i < count; i++) {
			v = static_cast<uint16_t>((v << 1) | (clock(false) ? 1 : 0));
		}
		return v;
	}
	bool out() { m_sk = false; apply(); return m_e.do_read(); }

private:
	void apply() { m_e.set_pins(m_cs, m_sk, m_di, m_org); }
	odin::Eeprom93c46 &m_e;
	bool m_org;
	bool m_cs = false, m_sk = false, m_di = false;
};

constexpr uint8_t ABITS8 = 7;
constexpr uint8_t ABITS16 = 6;

/* start, opcode 10, address */
uint8_t ee_read8(Wire &w, uint8_t addr)
{
	w.cs(true);
	w.bits(0x6, 3);
	w.bits(addr, ABITS8);
	const bool dummy = w.out();
	check(!dummy, "read: dummy zero after the address");
	const uint8_t v = static_cast<uint8_t>(w.read_bits(8));
	w.cs(false);
	return v;
}

void ee_ewen(Wire &w)
{
	w.cs(true);
	w.bits(0x4, 3);
	w.bits(0x60, ABITS8);
	w.cs(false);
}

void ee_ewds(Wire &w)
{
	w.cs(true);
	w.bits(0x4, 3);
	w.bits(0x00, ABITS8);
	w.cs(false);
}

void ee_write8(Wire &w, odin::Eeprom93c46 &e, uint8_t addr, uint8_t value, bool expect_busy)
{
	w.cs(true);
	w.bits(0x5, 3);
	w.bits(addr, ABITS8);
	w.bits(value, 8);
	w.cs(false);
	w.cs(true);
	if (expect_busy) {
		check(!e.do_read(), "write: BUSY right after CS falls");
		e.tick(1000);
		check(!e.do_read(), "write: still BUSY before the write cycle ends");
		e.tick(100000);
		check(e.do_read(), "write: READY after the write cycle");
	} else {
		check(e.do_read(), "write while disabled: no busy cycle");
	}
	w.cs(false);
}

void ee_erase8(Wire &w, uint8_t addr)
{
	w.cs(true);
	w.bits(0x7, 3);
	w.bits(addr, ABITS8);
	w.cs(false);
}

void test_eeprom()
{
	odin::Eeprom93c46 e(16000000);
	std::vector<uint8_t> image(128);
	for (uint32_t i = 0; i < 128; i++) {
		image[i] = static_cast<uint8_t>(i * 3 + 1);
	}
	e.load(image);
	Wire w(e);

	check(ee_read8(w, 0) == 0x01, "read cell 0");
	check(ee_read8(w, 0x7F) == static_cast<uint8_t>(0x7F * 3 + 1), "read cell 127");
	check(ee_read8(w, 0x10) == 0x31, "read cell 16");

	/* Sequential read: the next cell follows without a new instruction. */
	w.cs(true);
	w.bits(0x6, 3);
	w.bits(0x20, ABITS8);
	check(w.read_bits(8) == image[0x20] && w.read_bits(8) == image[0x21], "sequential read");
	w.cs(false);

	/* Writes are refused until EWEN. */
	ee_write8(w, e, 5, 0xAA, false);
	check(ee_read8(w, 5) == image[5], "write ignored while disabled");

	ee_ewen(w);
	check(e.write_enabled(), "EWEN");
	ee_write8(w, e, 5, 0xAA, true);
	check(ee_read8(w, 5) == 0xAA, "write then read back");

	/* A busy chip ignores a new instruction. */
	ee_write8(w, e, 6, 0x55, true);
	w.cs(true);
	w.bits(0x5, 3);
	w.bits(7, ABITS8);
	w.bits(0x33, 8);
	w.cs(false);
	e.tick(100000);
	check(ee_read8(w, 6) == 0x55 && ee_read8(w, 7) == 0x33, "second write after READY");

	ee_erase8(w, 5);
	e.tick(100000);
	check(ee_read8(w, 5) == 0xFF, "ERASE sets the cell to FF");

	ee_ewds(w);
	check(!e.write_enabled(), "EWDS");
	ee_write8(w, e, 5, 0x12, false);
	check(ee_read8(w, 5) == 0xFF, "write ignored after EWDS");

	/* x16 organisation: 6 address bits, 16 data bits, big-endian cells. */
	Wire w16(e, true);
	w16.cs(true);
	w16.bits(0x6, 3);
	w16.bits(1, ABITS16);
	check(w16.read_bits(16) == static_cast<uint16_t>((image[2] << 8) | image[3]), "x16 read");
	w16.cs(false);
}

/* JVS side. */

class Inputs : public odin::JvsInputs {
public:
	uint8_t sys = 0;
	uint16_t p[2] = { 0, 0 };
	uint16_t a[8] = { 0x8000, 0x8000, 0x8000, 0x8000, 0x8000, 0x8000, 0x8000, 0x8000 };
	uint8_t system() override { return sys; }
	uint16_t player(uint8_t i) override { return p[i]; }
	uint16_t analog(uint8_t c) override { return a[c]; }
};

/* Encodes a frame the way the MIE program does (listing 0x876F). */
std::vector<uint8_t> frame(uint8_t dest, std::vector<uint8_t> data)
{
	std::vector<uint8_t> out = { 0xE0 };
	std::vector<uint8_t> body = { dest, static_cast<uint8_t>(data.size() + 1) };
	body.insert(body.end(), data.begin(), data.end());
	uint8_t sum = 0;
	for (uint8_t b : body) {
		sum = static_cast<uint8_t>(sum + b);
	}
	body.push_back(sum);
	for (uint8_t b : body) {
		if (b == 0xD0 || b == 0xE0) {
			out.push_back(0xD0);
			out.push_back(static_cast<uint8_t>(b - 1));
		} else {
			out.push_back(b);
		}
	}
	return out;
}

std::vector<uint8_t> send(odin::JvsHost &host, const std::vector<uint8_t> &bytes)
{
	for (uint8_t b : bytes) {
		host.push(b);
	}
	host.commit_encoded();
	const std::span<const uint8_t> r = host.reply();
	return std::vector<uint8_t>(r.begin(), r.end());
}

/* Decodes a reply as the MIE program does (listing 0x8807): checks the
 * sync, the destination 0, the length and the checksum. */
std::vector<uint8_t> decode(const std::vector<uint8_t> &reply, bool &ok)
{
	ok = false;
	std::vector<uint8_t> raw;
	if (reply.empty() || reply[0] != 0xE0) {
		return raw;
	}
	for (size_t i = 1; i < reply.size(); i++) {
		uint8_t b = reply[i];
		if (b == 0xD0) {
			b = static_cast<uint8_t>(reply[++i] + 1);
		}
		raw.push_back(b);
	}
	if (raw.size() < 3 || raw[0] != 0x00 || raw[1] != raw.size() - 2) {
		return raw;
	}
	uint8_t sum = 0;
	for (size_t i = 0; i + 1 < raw.size(); i++) {
		sum = static_cast<uint8_t>(sum + raw[i]);
	}
	ok = sum == raw.back();
	return std::vector<uint8_t>(raw.begin() + 2, raw.end() - 1);
}

void test_jvs()
{
	Inputs inputs;
	odin::JvsHost host;
	odin::JvsIo13551 board(inputs);
	host.add_device(&board);
	bool ok = false;

	check(host.presence_line(), "presence line");
	check(!host.address_set_line(), "no address after power-up");

	/* Reset twice, broadcast; the node forgets its address. */
	send(host, frame(0xFF, { 0xF0, 0xD9 }));
	send(host, frame(0xFF, { 0xF0, 0xD9 }));
	check(!host.address_set_line(), "address clear after reset");

	/* Set address 1: reply status 1, report 1. */
	std::vector<uint8_t> r = send(host, frame(0xFF, { 0xF1, 0x01 }));
	check(r == std::vector<uint8_t>({ 0xE0, 0x00, 0x03, 0x01, 0x01, 0x05 }), "set address reply bytes");
	check(host.address_set_line(), "address set line");
	check(board.address() == 1, "node address 1");

	/* Identification. */
	r = send(host, frame(0x01, { 0x10 }));
	std::vector<uint8_t> d = decode(r, ok);
	const char *id = "SEGA ENTERPRISES,LTD.;I/O BD JVS;837-13551 ;Ver1.00;98/10";
	check(ok && d.size() == 2 + std::strlen(id) + 1 && d[0] == 0x01 && d[1] == 0x01 &&
	      std::memcmp(d.data() + 2, id, std::strlen(id) + 1) == 0, "ID request");

	/* Versions and function list in one frame, as the program sends. */
	r = send(host, frame(0x01, { 0x11, 0x12, 0x13, 0x14 }));
	d = decode(r, ok);
	const std::vector<uint8_t> expect = {
		0x01,
		0x01, 0x11,
		0x01, 0x20,
		0x01, 0x10,
		0x01, 0x01, 0x02, 0x0D, 0x00, 0x02, 0x02, 0x00, 0x00, 0x03, 0x08, 0x10, 0x00, 0x12, 0x06, 0x00, 0x00, 0x00
	};
	check(ok && d == expect, "versions and function list");

	/* Poll: switches 2 players x 2 bytes, 2 coin slots, 8 analog channels. */
	inputs.sys = 0x80;
	inputs.p[0] = 0x8040;
	inputs.p[1] = 0x0004;
	inputs.a[3] = 0x1234;
	board.inc_coin(1);
	r = send(host, frame(0x01, { 0x20, 0x02, 0x02, 0x21, 0x02, 0x22, 0x08 }));
	d = decode(r, ok);
	check(ok && d.size() == 1 + 6 + 5 + 17, "poll reply size");
	check(d.size() >= 7 && d[0] == 0x01 && d[1] == 0x01 && d[2] == 0x80 && d[3] == 0x80 && d[4] == 0x40 &&
	      d[5] == 0x00 && d[6] == 0x04, "switch bytes");
	check(d.size() >= 12 && d[7] == 0x01 && d[8] == 0x00 && d[9] == 0x00 && d[10] == 0x00 && d[11] == 0x01, "coin counters");
	check(d.size() >= 29 && d[12] == 0x01 && d[13] == 0x80 && d[14] == 0x00 && d[19] == 0x12 && d[20] == 0x34, "analog channels");

	/* Escapes both ways: a D0/E0 data byte in the reply, an escaped
	 * count in the request (coin add, 0x31). */
	inputs.p[0] = 0xE0D0;
	r = send(host, frame(0x01, { 0x20, 0x01, 0x02 }));
	bool escaped = false;
	for (size_t i = 1; i + 1 < r.size(); i++) {
		if (r[i] == 0xD0 && r[i + 1] == 0xDF) {
			escaped = true;
		}
	}
	d = decode(r, ok);
	check(ok && escaped && d.size() == 5 && d[3] == 0xE0 && d[4] == 0xD0, "escaped reply");
	std::vector<uint8_t> add = frame(0x01, { 0x31, 0x01, 0x00, 0xE0 });
	check(add.size() == 9 && add[6] == 0xD0 && add[7] == 0xDF, "escaped request bytes");
	r = send(host, add);
	d = decode(r, ok);
	check(ok && d == std::vector<uint8_t>({ 0x01, 0x01 }), "coin add reply");
	r = send(host, frame(0x01, { 0x21, 0x01 }));
	d = decode(r, ok);
	check(ok && d.size() == 4 && d[2] == 0x00 && d[3] == 0xE0, "coin add took the escaped count");

	/* Checksum error: status 3. */
	std::vector<uint8_t> bad = frame(0x01, { 0x10 });
	bad.back() = static_cast<uint8_t>(bad.back() + 1);
	r = send(host, bad);
	check(r == std::vector<uint8_t>({ 0xE0, 0x00, 0x02, 0x03, 0x05 }), "checksum error reply");

	/* Unknown command: status 2. */
	r = send(host, frame(0x01, { 0x99 }));
	check(r == std::vector<uint8_t>({ 0xE0, 0x00, 0x02, 0x02, 0x04 }), "unknown command reply");

	/* Frames for another node stay unanswered. */
	r = send(host, frame(0x02, { 0x10 }));
	check(r.empty(), "other node silent");

	/* Outputs. */
	r = send(host, frame(0x01, { 0x32, 0x01, 0xA8 }));
	d = decode(r, ok);
	check(ok && d == std::vector<uint8_t>({ 0x01, 0x01 }) && board.outputs() == 0xA8, "driver outputs");
}

} /* namespace */

/* The interrupt controller on ports B, C and F, through the window of the
 * host: a port is at twice its number, the direction register at 0x10. */
void test_interrupts()
{
	std::vector<uint8_t> rom(2048, 0), ram(32768, 0);
	odin_mie_t *mie = odin_mie_new(rom.data(), rom.size(), ram.data(), ram.size(), 200000000,
	                               nullptr, nullptr);
	uint32_t v = 0;

	odin_mie_set_irq_lines(mie, 0x01);
	check(odin_mie_irq_pending(mie) == 0x01 && odin_mie_irq_out(mie) == 0,
	      "nothing passes while port C is an input");
	odin_mie_host_write(mie, 2 * 2, 1, 0xE1);
	check(odin_mie_irq_out(mie) == 0, "writing the latch of port C is not driving it");
	odin_mie_host_write(mie, 2 * 8, 1, 0xFB);
	check(odin_mie_irq_out(mie) == 1, "ANT_INT passes the mask E1");
	odin_mie_set_irq_lines(mie, 0x10);
	check(odin_mie_irq_out(mie) == 0, "SZK_INT does not");
	odin_mie_host_read(mie, 2 * 1, 1, &v);
	check(v == 0x10, "but port B reads it");
	odin_mie_host_write(mie, 2 * 2, 1, 0xF1);
	check(odin_mie_irq_out(mie) == 1, "until the mask has its bit");
	odin_mie_set_irq_lines(mie, 0);
	check(odin_mie_irq_out(mie) == 0, "a line let go asks no more");

	odin_mie_host_write(mie, 2 * 5, 1, 0x70);
	check(odin_mie_irq_pending(mie) == 0, "port F asks for nothing while it is an input");
	odin_mie_host_write(mie, 2 * 8, 1, 0xDB);
	check(odin_mie_irq_pending(mie) == 0x0E, "pins 4 to 6 of port F are bits 1 to 3 of port B");
	check(odin_mie_irq_out(mie) == 0, "masked by F1");
	odin_mie_host_write(mie, 2 * 2, 1, 0x04);
	check(odin_mie_irq_out(mie) == 1, "and passing with their bit in the mask");
	odin_mie_host_write(mie, 2 * 5, 1, 0x50);
	check(odin_mie_irq_out(mie) == 0, "each pin on its own bit");
	odin_mie_delete(mie);
}

int main()
{
	test_eeprom();
	test_jvs();
	test_interrupts();
	std::printf("%u checks passed, %u failed\n", g_passed, g_failed);
	return g_failed ? 1 : 0;
}
