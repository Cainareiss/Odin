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

/* Checks of the JVS cable with the 837-13551 board behind it, frames
 * built and read the way the MIE program does: sync, marks, checksum,
 * reset, address, identification, feature list, polls, outputs.
 *
 * Build: g++ -std=c++20 -Wall -Wextra -Wpedantic -Wconversion -Wshadow \
 *            -O2 -I src tests/jvs/jvs_tests.cpp src/odin/jvs.cpp \
 *            src/odin/jvs_837_13551.cpp -o jvs_tests
 * Exit status is 0 when every check passes. */

#include "odin/jvs.hpp"
#include "odin/jvs_837_13551.hpp"

#include <cstdio>
#include <cstring>
#include <span>
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

/* The board reads these on its connectors. */

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
	odin::Jvs837_13551 board(inputs);
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

	/* A reset without its D9 is not one. */
	send(host, frame(0xFF, { 0xF0, 0x00 }));
	send(host, frame(0xFF, { 0xF0, 0x00 }));
	check(host.address_set_line() && board.address() == 1, "reset without D9 ignored");

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
		0x01, 0x01, 0x02, 0x0D, 0x00, 0x02, 0x02, 0x00, 0x00, 0x03, 0x08, 0x00, 0x00, 0x12, 0x06, 0x00, 0x00, 0x00
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
	check(d.size() >= 29 && d[12] == 0x01 && d[13] == 0x80 && d[14] == 0x00 && d[19] == 0x12 && d[20] == 0x00, "analog channels, 8 bits");

	/* Escapes both ways: a D0/E0 data byte in the reply, an escaped
	 * count in the request (coin counter increase, 0x35). */
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
	std::vector<uint8_t> add = frame(0x01, { 0x35, 0x01, 0x00, 0xE0 });
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

	/* Unknown command: status 2. The boards know neither the payout
	 * counter (31) nor the single outputs (38). */
	r = send(host, frame(0x01, { 0x99 }));
	check(r == std::vector<uint8_t>({ 0xE0, 0x00, 0x02, 0x02, 0x04 }), "unknown command reply");
	r = send(host, frame(0x01, { 0x11, 0x13, 0x99, 0x12 }));
	d = decode(r, ok);
	check(ok && d == std::vector<uint8_t>({ 0x02, 0x01, 0x11, 0x01, 0x10 }),
	      "the commands before an unknown one keep their reports, those after it are not run");
	r = send(host, frame(0x01, { 0x31, 0x01, 0x00, 0x01 }));
	check(r == std::vector<uint8_t>({ 0xE0, 0x00, 0x02, 0x02, 0x04 }), "31 is unknown");
	r = send(host, frame(0x01, { 0x38, 0x01, 0x01 }));
	check(r == std::vector<uint8_t>({ 0xE0, 0x00, 0x02, 0x02, 0x04 }), "38 is unknown");
	r = send(host, frame(0x01, { 0x21, 0x01 }));
	d = decode(r, ok);
	check(ok && d.size() == 4 && d[2] == 0x00 && d[3] == 0xE0, "neither changed the coins");

	/* A refused command answers report 2 alone; the reading goes on past
	 * the bytes it took, and for the outputs past their data. */
	r = send(host, frame(0x01, { 0x22, 0x09, 0x11 }));
	d = decode(r, ok);
	check(ok && d == std::vector<uint8_t>({ 0x01, 0x02, 0x01, 0x11 }), "analog refused, then 11 answered");
	r = send(host, frame(0x01, { 0x20, 0x03, 0x02, 0x12 }));
	d = decode(r, ok);
	check(ok && d == std::vector<uint8_t>({ 0x01, 0x02, 0x01, 0x20 }), "switches refused, then 12 answered");
	r = send(host, frame(0x01, { 0x32, 0x02, 0xAA, 0xBB, 0x13 }));
	d = decode(r, ok);
	check(ok && d == std::vector<uint8_t>({ 0x01, 0x02, 0x01, 0x10 }), "outputs refused with their data, then 13");
	r = send(host, frame(0x01, { 0x21, 0x00, 0x22, 0x00, 0x11 }));
	d = decode(r, ok);
	check(ok && d == std::vector<uint8_t>({ 0x01, 0x02, 0x02, 0x01, 0x11 }), "0 coin slots, 0 channels refused");
	r = send(host, frame(0x01, { 0x35, 0x03, 0x00, 0x01, 0x11 }));
	d = decode(r, ok);
	check(ok && d == std::vector<uint8_t>({ 0x01, 0x02, 0x01, 0x11 }), "coin slot refused with its count, then 11");

	/* A counter that would leave 0..3FFF: report 2, counter kept. */
	r = send(host, frame(0x01, { 0x30, 0x01, 0x00, 0xE1 }));
	d = decode(r, ok);
	check(ok && d == std::vector<uint8_t>({ 0x01, 0x02 }), "decrease below 0 refused");
	r = send(host, frame(0x01, { 0x35, 0x01, 0x3F, 0x20 }));
	d = decode(r, ok);
	check(ok && d == std::vector<uint8_t>({ 0x01, 0x02 }), "increase above 3FFF refused");
	r = send(host, frame(0x01, { 0x35, 0x01, 0x3F, 0x1F }));
	d = decode(r, ok);
	check(ok && d == std::vector<uint8_t>({ 0x01, 0x01 }), "increase up to 3FFF taken");
	r = send(host, frame(0x01, { 0x21, 0x01 }));
	d = decode(r, ok);
	check(ok && d.size() == 4 && d[2] == 0x3F && d[3] == 0xFF, "counter at 3FFF");
	board.inc_coin(0);
	r = send(host, frame(0x01, { 0x21, 0x01 }));
	d = decode(r, ok);
	check(ok && d.size() == 4 && d[2] == 0x3F && d[3] == 0xFF, "a coin at 3FFF leaves it there");
	r = send(host, frame(0x01, { 0x30, 0x01, 0x3F, 0xFF }));
	d = decode(r, ok);
	check(ok && d == std::vector<uint8_t>({ 0x01, 0x01 }), "decrease down to 0 taken");

	/* Frames for another node stay unanswered. */
	r = send(host, frame(0x02, { 0x10 }));
	check(r.empty(), "other node silent");

	/* Outputs. */
	r = send(host, frame(0x01, { 0x32, 0x01, 0xA8 }));
	d = decode(r, ok);
	check(ok && d == std::vector<uint8_t>({ 0x01, 0x01 }) && board.outputs() == 0xA8, "driver outputs");
}

} /* namespace */

int main()
{
	test_jvs();
	std::printf("%u checks passed, %u failed\n", g_passed, g_failed);
	return g_failed ? 1 : 0;
}
