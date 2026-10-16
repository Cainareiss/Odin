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

/* Checks of the CRP-1231 card reader (frame XOR, ACK, ENQ, responses,
 * card data) and of the MIE serial adapter block driven from its ports
 * the way the Z80 code von4 uploads does it: routine 0x90E1 sends a
 * frame byte by byte, 0x913C reads one byte through the 0x80/0x81
 * requests, 0x9199 reads a frame, 0x92BC sends ENQ and checks the
 * echo, 0x909F runs the 0x55 loopback. The frames come from the game's
 * own A200 buffer.
 *
 * Build: g++ -std=c++20 -Wall -Wextra -Wpedantic -Wconversion -Wshadow \
 *            -O2 -I src tests/mie/card_test.cpp src/odin/card_reader.cpp \
 *            src/odin/mie.cpp src/odin/z80.cpp src/odin/z80_ops.cpp \
 *            src/odin/jvs.cpp -o card_test
 * An optional argument names a scratch file for the card image check.
 * Exit status is 0 when every check passes. */

#include "odin/card_reader.hpp"
#include "odin/mie.hpp"

#include <array>
#include <cstdio>
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

using Bytes = std::vector<uint8_t>;

/* st1 sensor bytes: shutter closed, stocker full, card or not. */
constexpr uint8_t ST1_EMPTY = 0x60;
constexpr uint8_t ST1_CARD = 0x78;
constexpr uint8_t ST1_OPEN_CARD = 0xB8;

/* Master frame as the game's constructor (0x0C183E96) builds it. */
Bytes frame(uint8_t cmd, const Bytes &params)
{
	Bytes out = { 0x02, static_cast<uint8_t>(params.size() + 6), cmd, 0, 0, 0 };
	out.insert(out.end(), params.begin(), params.end());
	out.push_back(0x03);
	out.push_back(odin::CardReader::frame_xor(std::span<const uint8_t>(out.data() + 1, out.size() - 1)));
	return out;
}

Bytes drain(odin::CardReader &r)
{
	Bytes out;
	while (r.has_byte()) {
		out.push_back(r.take_byte());
	}
	return out;
}

/* Frame in, ACK out, ENQ in, response out. */
Bytes exchange(odin::CardReader &r, const Bytes &f, bool &acked)
{
	for (uint8_t b : f) {
		r.receive(b);
	}
	const Bytes ack = drain(r);
	acked = ack == Bytes({ 0x06 });
	r.receive(0x05);
	return drain(r);
}

bool response_ok(const Bytes &resp, uint8_t cmd, uint8_t st1, uint8_t st2, uint8_t st3, size_t data)
{
	if (resp.size() != data + 8 || resp[0] != 0x02 || resp[1] != data + 6 || resp[2] != cmd ||
	    resp[3] != st1 || resp[4] != st2 || resp[5] != st3 || resp[resp.size() - 2] != 0x03) {
		return false;
	}
	return resp.back() == odin::CardReader::frame_xor(std::span<const uint8_t>(resp.data() + 1, resp.size() - 2));
}

void test_reader(const char *card_file)
{
	/* XOR of the frames the game sent (host writes to A200 in the
	 * trace): cancel, door closed, init. */
	check(frame(0x40, {}) == Bytes({ 0x02, 0x06, 0x40, 0, 0, 0, 0x03, 0x45 }), "frame 40 xor");
	check(frame(0xD0, { '0' }) == Bytes({ 0x02, 0x07, 0xD0, 0, 0, 0, 0x30, 0x03, 0xE4 }), "frame D0 xor");
	check(frame(0x10, { '0' }) == Bytes({ 0x02, 0x07, 0x10, 0, 0, 0, 0x30, 0x03, 0x24 }), "frame 10 xor");

	odin::CardReader r;
	bool acked = false;
	Bytes resp = exchange(r, frame(0x10, { '0' }), acked);
	check(acked, "init: ACK");
	check(resp == Bytes({ 0x02, 0x06, 0x10, 0x60, '0', '0', 0x03, 0x75 }), "init: response, no card");
	r.receive(0x05);
	check(drain(r) == resp, "ENQ again: same response");
	r.receive(0x06);
	check(drain(r).empty(), "ACK from the master: silent");

	/* Bad XOR: NAK, no new response. */
	Bytes bad = frame(0x40, {});
	bad.back() = static_cast<uint8_t>(bad.back() ^ 0x01);
	for (uint8_t b : bad) {
		r.receive(b);
	}
	check(drain(r) == Bytes({ 0x15 }), "bad xor: NAK");
	r.receive(0x15);
	check(drain(r) == resp, "NAK from the master: response again");

	resp = exchange(r, frame(0x40, {}), acked);
	check(acked && response_ok(resp, 0x40, ST1_EMPTY, '0', '0', 0), "cancel");
	resp = exchange(r, frame(0xD0, { '0' }), acked);
	check(acked && response_ok(resp, 0xD0, ST1_EMPTY, '0', '0', 0), "door closed");

	/* Read without a card: waiting for one. */
	resp = exchange(r, frame(0x33, { '0', '1', '6' }), acked);
	check(acked && response_ok(resp, 0x33, ST1_EMPTY, '0', '4', 0), "read, no card");
	resp = exchange(r, frame(0x53, { '0', '1', '0' }), acked);
	check(acked && response_ok(resp, 0x53, ST1_EMPTY, '0', '4', 0), "write, no card");

	/* Dispense: a blank card at the head. */
	resp = exchange(r, frame(0xB0, {}), acked);
	check(acked && response_ok(resp, 0xB0, ST1_CARD, '0', '0', 0) && r.card_present(), "dispense");
	resp = exchange(r, frame(0x33, { '0', '1', '6' }), acked);
	check(acked && response_ok(resp, 0x33, ST1_CARD, '0', '0', 3 * 0x45), "read all tracks");
	bool blank = true;
	for (size_t i = 6; i < resp.size() - 2; i++) {
		blank = blank && resp[i] == 0;
	}
	check(blank, "dispensed card is blank");

	/* Write track 1, read it back alone and with track 3. */
	Bytes params = { '0', '1', '0' };
	for (uint32_t i = 0; i < 0x45; i++) {
		params.push_back(static_cast<uint8_t>(0x41 + (i % 26)));
	}
	resp = exchange(r, frame(0x53, params), acked);
	check(acked && response_ok(resp, 0x53, ST1_CARD, '0', '0', 0), "write track 1");
	resp = exchange(r, frame(0x33, { '0', '1', '0' }), acked);
	check(acked && response_ok(resp, 0x33, ST1_CARD, '0', '0', 0x45) &&
	      Bytes(resp.begin() + 6, resp.end() - 2) == Bytes(params.begin() + 3, params.end()), "read track 1");
	resp = exchange(r, frame(0x33, { '0', '1', '4' }), acked);
	check(acked && response_ok(resp, 0x33, ST1_CARD, '0', '0', 2 * 0x45) && resp[6] == 'A' && resp[6 + 0x45] == 0,
	      "read tracks 1 and 3");
	/* Capture mode: no data. */
	resp = exchange(r, frame(0x33, { '2', '1', '6' }), acked);
	check(acked && response_ok(resp, 0x33, ST1_CARD, '0', '0', 0), "read, capture mode");

	/* Door open: the card stays, no data. */
	resp = exchange(r, frame(0xD0, { '1' }), acked);
	check(acked && response_ok(resp, 0xD0, ST1_OPEN_CARD, '0', '0', 0), "door open");
	resp = exchange(r, frame(0x33, { '0', '1', '0' }), acked);
	check(acked && response_ok(resp, 0x33, ST1_OPEN_CARD, '0', '0', 0), "read with the door open");
	resp = exchange(r, frame(0xD0, { '0' }), acked);
	resp = exchange(r, frame(0x80, {}), acked);
	check(acked && response_ok(resp, 0x80, ST1_EMPTY, '0', '0', 0) && !r.card_present(), "eject");

	/* Unknown command: answered OK. */
	resp = exchange(r, frame(0xC8, {}), acked);
	check(acked && response_ok(resp, 0xC8, ST1_EMPTY, '0', '0', 0), "unknown command");

	/* A reset keeps the card inside. */
	exchange(r, frame(0xB0, {}), acked);
	r.reset();
	check(r.card_present() && !r.has_byte(), "reset keeps the card");

	if (card_file) {
		odin::CardReader w(card_file);
		exchange(w, frame(0xB0, {}), acked);
		exchange(w, frame(0x53, params), acked);
		odin::CardReader v(card_file);
		check(!v.card_present() && v.insert_card() && v.card_present() && v.card()[0] == 'A' &&
		      v.card()[0x44] == static_cast<uint8_t>(0x41 + (0x44 % 26)) && v.card()[0x45] == 0,
		      "card file written then inserted");
		std::remove(card_file);
	}
}

/* The chip with its bus granted to the host: the CPU stops, the
 * adapter block keeps running. */

class Host : public odin::MieHost {
public:
	uint8_t gpio_read(uint8_t) override { return 0xFF; }
	void gpio_write(uint8_t, uint8_t) override {}
};

enum : uint8_t {
	PORT_CONTROL = 0x09,
	PORT_DATA = 0x0A,
	PORT_REPLY_CONTROL = 0x0B,
	PORT_REPLY_DATA = 0x0C,
	PORT_STATUS = 0x0D
};

/* Z80 code results in A001. */
enum : int32_t {
	RESULT_OK = 0,
	RESULT_NOTHING = 2,
	RESULT_ERROR = -1
};

class Bench {
public:
	Bench() : m_mie(m_host, m_rom, m_ram)
	{
		m_rom.fill(0);
		m_ram.fill(0);
		m_mie.host_write_port(0x30, 0x40);
		m_mie.set_busrq(true);
	}

	odin::Mie &mie() { return m_mie; }

	/* One turn of the Z80 poll loops: IN, AND, JR, DJNZ. */
	uint8_t poll()
	{
		m_mie.run(40);
		return m_mie.host_read_port(PORT_STATUS);
	}

	/* Busy clear then ready set, 255 turns each (0x9105-0x911A). */
	bool wait_reply()
	{
		uint32_t n;
		for (n = 0; n < 255 && (poll() & 0x03); n++) {
		}
		if (n == 255) {
			return false;
		}
		for (n = 0; n < 255 && !(poll() & 0x08); n++) {
		}
		return n < 255;
	}

	/* Reads 0x0B then 0x0C as every routine does. */
	uint8_t take_reply()
	{
		m_mie.host_read_port(PORT_REPLY_CONTROL);
		return m_mie.host_read_port(PORT_REPLY_DATA);
	}

	/* Routine 0x90E1 for one byte: control 1, 255 NOP+DJNZ turns,
	 * then the polls; the echo is read from 0x0C. */
	int32_t send_byte(uint8_t value, bool idle, uint8_t &echo)
	{
		m_mie.host_write_port(PORT_DATA, value);
		m_mie.host_write_port(PORT_CONTROL, 0x01);
		if (idle) {
			m_mie.run(255 * 17);
		}
		if (!wait_reply()) {
			return RESULT_ERROR;
		}
		echo = take_reply();
		return RESULT_OK;
	}

	/* Frame as 0x90E1 sends it: LEN + 2 bytes. */
	int32_t send_frame(const Bytes &f)
	{
		for (uint8_t b : f) {
			uint8_t echo = 0;
			if (send_byte(b, true, echo) != RESULT_OK) {
				return RESULT_ERROR;
			}
			if (echo != b) {
				return RESULT_ERROR;
			}
		}
		return RESULT_OK;
	}

	/* Routine 0x92BC: ENQ with the echo compared, no idle turns. */
	int32_t send_enq()
	{
		uint8_t echo = 0;
		if (send_byte(0x05, false, echo) != RESULT_OK) {
			return RESULT_ERROR;
		}
		return echo == 0x05 ? RESULT_OK : RESULT_ERROR;
	}

	/* Adapter request 0x80 or 0x81: the code writes it to 0x0A and
	 * 0x09 both (0x9146-0x914A, 0x916E-0x9172). */
	bool request(uint8_t code, uint8_t &reply)
	{
		m_mie.host_write_port(PORT_DATA, code);
		m_mie.host_write_port(PORT_CONTROL, code);
		if (!wait_reply()) {
			return false;
		}
		reply = take_reply();
		return true;
	}

	/* Routine 0x913C. */
	int32_t read_byte(uint8_t &out)
	{
		uint8_t st = 0;
		take_reply();
		if (!request(0x80, st)) {
			return RESULT_ERROR;
		}
		if (!(st & 0x40)) {
			return RESULT_NOTHING;
		}
		if (!request(0x81, out)) {
			return RESULT_ERROR;
		}
		return RESULT_OK;
	}

	/* Routine 0x9199: first byte within 255 requests, then bytes until
	 * 255 empty requests in a row; 255 bytes is an error. */
	int32_t read_frame(Bytes &out)
	{
		out.clear();
		uint8_t st = 0;
		uint8_t b = 0;
		uint32_t tries = 255;
		for (;;) {
			take_reply();
			if (!request(0x80, st)) {
				return RESULT_NOTHING;
			}
			if (st & 0x40) {
				break;
			}
			if (--tries == 0) {
				return RESULT_NOTHING;
			}
		}
		if (!request(0x81, b)) {
			return RESULT_NOTHING;
		}
		out.push_back(b);
		uint32_t empty = 0;
		while (empty < 255) {
			take_reply();
			if (!request(0x80, st)) {
				return RESULT_ERROR;
			}
			if (st & 0x40) {
				if (!request(0x81, b)) {
					return RESULT_ERROR;
				}
				out.push_back(b);
				if (out.size() == 255) {
					return RESULT_ERROR;
				}
				empty = 0;
			} else {
				empty++;
			}
		}
		return RESULT_OK;
	}

	/* Routine 0x909F: 255 bytes with control 0x55, (0x0D & 0x0C) == 0
	 * before, == 4 after, pairs (0x0B, 0x0C) collected. */
	bool loopback(const Bytes &in, Bytes &pairs)
	{
		pairs.clear();
		for (uint8_t v : in) {
			uint32_t n;
			for (n = 0; n < 255 && (poll() & 0x0C); n++) {
			}
			if (n == 255) {
				return false;
			}
			m_mie.host_write_port(PORT_DATA, v);
			m_mie.host_write_port(PORT_CONTROL, 0x55);
			for (n = 0; n < 255 && (poll() & 0x0C) != 0x04; n++) {
			}
			if (n == 255) {
				return false;
			}
			pairs.push_back(m_mie.host_read_port(PORT_REPLY_CONTROL));
			pairs.push_back(m_mie.host_read_port(PORT_REPLY_DATA));
		}
		return true;
	}

private:
	Host m_host;
	std::array<uint8_t, odin::Mie::ROM_SIZE> m_rom;
	std::array<uint8_t, odin::Mie::RAM_SIZE> m_ram;
	odin::Mie m_mie;
};

void test_ports()
{
	Bench b;
	odin::CardReader reader;
	b.mie().set_serial_device(&reader);
	uint8_t v = 0;
	Bytes got;

	/* Power-up, after the ROM's zero writes to 0x0D and 0x0E. */
	b.mie().host_write_port(0x0E, 0);
	b.mie().host_write_port(0x0D, 0);
	check(b.mie().host_read_port(PORT_STATUS) == 0 && b.take_reply() == 0, "block idle after reset");

	/* A send is busy at once and answers within the budget. */
	b.mie().host_write_port(PORT_DATA, 0x02);
	b.mie().host_write_port(PORT_CONTROL, 0x01);
	check((b.mie().host_read_port(PORT_STATUS) & 0x0B) == 0x03, "busy right after the control write");
	check(b.wait_reply(), "reply ready within 255 turns");
	check((b.mie().host_read_port(PORT_STATUS) & 0x0B) == 0x08, "ready, not busy");
	check(b.mie().host_read_port(PORT_REPLY_CONTROL) == 0x01 && b.mie().host_read_port(PORT_REPLY_DATA) == 0x02,
	      "echo in 0x0C, control in 0x0B");
	check((b.mie().host_read_port(PORT_STATUS) & 0x0B) == 0, "reading 0x0C clears ready");

	/* That stray STX must not confuse the reader: reset the block
	 * mid-exchange for the next byte and check nothing reached it. */
	b.mie().host_write_port(PORT_DATA, 0x07);
	b.mie().host_write_port(PORT_CONTROL, 0x01);
	b.mie().host_write_port(0x0B, 0);
	check(b.mie().host_read_port(PORT_STATUS) == 0 && !b.wait_reply(), "write to 0x0B resets the block");
	/* The reader did get the STX; a cancel frame then resyncs it: the
	 * frame is rejected on its XOR and the next one is clean. */
	check(b.send_frame(frame(0x40, {})) == RESULT_OK, "send: frame after a stray byte");
	check(b.read_byte(v) == RESULT_OK && v == 0x15, "stray STX: NAK");

	/* The game's first three exchanges. */
	const Bytes init[3] = { frame(0x40, {}), frame(0xD0, { '0' }), frame(0x10, { '0' }) };
	const uint8_t cmds[3] = { 0x40, 0xD0, 0x10 };
	for (uint32_t i = 0; i < 3; i++) {
		check(b.send_frame(init[i]) == RESULT_OK, "send: frame with echo per byte");
		check(b.read_byte(v) == RESULT_OK && v == 0x06, "read byte: ACK");
		check(b.read_byte(v) == RESULT_NOTHING, "read byte: nothing more before ENQ");
		check(b.send_enq() == RESULT_OK, "ENQ echoed");
		check(b.read_frame(got) == RESULT_OK && response_ok(got, cmds[i], ST1_EMPTY, '0', '0', 0), "read frame: response");
	}
	check(b.read_frame(got) == RESULT_NOTHING, "read frame: nothing after the response");

	/* Read of a dispensed card through the ports: 207 data bytes. */
	check(b.send_frame(frame(0xB0, {})) == RESULT_OK && b.read_byte(v) == RESULT_OK && v == 0x06 &&
	      b.send_enq() == RESULT_OK && b.read_frame(got) == RESULT_OK, "dispense through the ports");
	check(b.send_frame(frame(0x33, { '0', '1', '6' })) == RESULT_OK && b.read_byte(v) == RESULT_OK && v == 0x06 &&
	      b.send_enq() == RESULT_OK && b.read_frame(got) == RESULT_OK &&
	      response_ok(got, 0x33, ST1_CARD, '0', '0', 3 * 0x45), "read all tracks through the ports");

	/* Loopback test, 255 bytes. */
	Bytes pattern;
	for (uint32_t i = 0; i < 255; i++) {
		pattern.push_back(static_cast<uint8_t>(0x10 + i));
	}
	Bytes pairs;
	check(b.loopback(pattern, pairs) && pairs.size() == 510, "loopback: 255 exchanges");
	bool ok = pairs.size() == 510;
	for (uint32_t i = 0; ok && i < 255; i++) {
		ok = pairs[2 * i] == 0x55 && pairs[2 * i + 1] == pattern[i];
	}
	check(ok, "loopback: pairs (55, byte)");
	check(!reader.has_byte(), "loopback: nothing on the line");

	/* Unknown control value: no exchange. */
	b.mie().host_write_port(PORT_DATA, 0x00);
	b.mie().host_write_port(PORT_CONTROL, 0x02);
	check(b.mie().host_read_port(PORT_STATUS) == 0, "unknown control: idle");

	/* No device: echo still comes back, nothing is ever waiting. */
	Bench open;
	uint8_t echo = 0;
	check(open.send_byte(0x05, false, echo) == RESULT_OK && echo == 0x05, "open line: echo");
	check(open.read_byte(v) == RESULT_NOTHING, "open line: nothing waiting");

	/* Save and load mid-exchange. */
	odin::MieState state;
	open.mie().host_write_port(PORT_DATA, 0x33);
	open.mie().host_write_port(PORT_CONTROL, 0x01);
	open.mie().save(state);
	check(open.wait_reply() && open.take_reply() == 0x33, "state: exchange completes");
	open.mie().load(state);
	check((open.mie().host_read_port(PORT_STATUS) & 0x03) == 0x03, "state: busy again after load");
	check(open.wait_reply() && open.take_reply() == 0x33, "state: completes again");
}

} /* namespace */

int main(int argc, char **argv)
{
	test_reader(argc > 1 ? argv[1] : nullptr);
	test_ports();
	std::printf("%u checks passed, %u failed\n", g_passed, g_failed);
	return g_failed ? 1 : 0;
}
