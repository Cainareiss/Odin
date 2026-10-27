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

/* Checks of the CRP-1231BR-10 card reader (its replies as its program
 * gives them, MARKDOWN/96c: frames, ACK, ENQ, refusals, the read that waits,
 * blank tracks, the stocker, the player's card file written whole or not
 * at all, its copy) and of the MIE serial
 * adapter block driven from its ports
 * the way the Z80 code von4 uploads does it: routine 0x90E1 sends a
 * frame byte by byte, 0x913C reads one byte through the 0x80/0x81
 * requests, 0x9199 reads a frame, 0x92BC sends ENQ and checks the
 * echo, 0x909F runs the 0x55 loopback. The frames come from the game's
 * own A200 buffer.
 *
 * Build: g++ -std=c++20 -Wall -Wextra -Wpedantic -Wconversion -Wshadow \
 *            -O2 -I src tests/mie/card_test.cpp src/odin/card_reader.cpp \
 *            src/odin/mie.cpp src/odin/z80.cpp src/odin/z80_ops.cpp \
 *            src/odin/jvs.cpp src/odin/user_files.cpp -o card_test
 * An optional argument names a scratch file for the checks of the card
 * file.
 * Exit status is 0 when every check passes. */

#include "odin/card_reader.hpp"
#include "odin/mie.hpp"

#include <array>
#include <cstdio>
#include <string>
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

/* st1 as the reader gives it: the sensors a position covers, the
 * stocker full, the shutter closed or open */
constexpr uint8_t SENSORS[10] = { 0x00, 0x01, 0x03, 0x07, 0x06, 0x0E, 0x0C, 0x1C, 0x18, 0x10 };

uint8_t st1(odin::CardReader::Position at, bool open, bool stocker = true)
{
	return static_cast<uint8_t>(SENSORS[at] | (stocker ? 0x20 : 0) | (open ? 0x80 : 0x40));
}

using P = odin::CardReader::Position;
constexpr uint8_t ST1_EMPTY = 0x60;

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

Bytes enq(odin::CardReader &r)
{
	r.receive(0x05);
	return drain(r);
}

/* Frame in, ACK out, ENQ in, response out. */
Bytes exchange(odin::CardReader &r, const Bytes &f, bool &acked)
{
	for (uint8_t b : f) {
		r.receive(b);
	}
	const Bytes ack = drain(r);
	acked = ack == Bytes({ 0x06 });
	return enq(r);
}

bool response_ok(const Bytes &resp, uint8_t cmd, uint8_t s1, uint8_t st2, uint8_t st3, size_t data)
{
	if (resp.size() != data + 8 || resp[0] != 0x02 || resp[1] != data + 6 || resp[2] != cmd ||
	    resp[3] != s1 || resp[4] != st2 || resp[5] != st3 || resp[resp.size() - 2] != 0x03) {
		return false;
	}
	return resp.back() == odin::CardReader::frame_xor(std::span<const uint8_t>(resp.data() + 1, resp.size() - 2));
}

/* 207 bytes the game could write: three tracks of 69 */
Bytes tracks(uint8_t base)
{
	Bytes out;
	for (uint32_t i = 0; i < 3 * 0x45; i++) {
		out.push_back(static_cast<uint8_t>(base + (i % 26)));
	}
	return out;
}

Bytes with(Bytes head, const Bytes &tail)
{
	head.insert(head.end(), tail.begin(), tail.end());
	return head;
}

Bytes data_of(const Bytes &resp)
{
	return Bytes(resp.begin() + 6, resp.end() - 2);
}

bool write_file(const std::string &path, const Bytes &bytes)
{
	FILE *f = std::fopen(path.c_str(), "wb");
	if (!f) {
		return false;
	}
	const bool ok = std::fwrite(bytes.data(), 1, bytes.size(), f) == bytes.size();
	std::fclose(f);
	return ok;
}

Bytes read_file(const std::string &path)
{
	Bytes out(300);
	FILE *f = std::fopen(path.c_str(), "rb");
	if (!f) {
		return {};
	}
	out.resize(std::fread(out.data(), 1, out.size(), f));
	std::fclose(f);
	return out;
}

/* The replies of crp1231br10_ver0107.ic2 (MARKDOWN/96c) */
void test_reader(const char *scratch)
{
	/* XOR of the frames the game sent (host writes to A200 in the
	 * trace): cancel, door closed, init. */
	check(frame(0x40, {}) == Bytes({ 0x02, 0x06, 0x40, 0, 0, 0, 0x03, 0x45 }), "frame 40 xor");
	check(frame(0xD0, { '0' }) == Bytes({ 0x02, 0x07, 0xD0, 0, 0, 0, 0x30, 0x03, 0xE4 }), "frame D0 xor");
	check(frame(0x10, { '0' }) == Bytes({ 0x02, 0x07, 0x10, 0, 0, 0, 0x30, 0x03, 0x24 }), "frame 10 xor");

	odin::CardReader r;
	bool acked = false;
	Bytes resp = enq(r);
	check(resp == Bytes({ 0x02, 0x06, 0x10, 0x60, '0', '0', 0x03, 0x75 }), "a bare ENQ at power-up: the boot reply");

	/* At rest 40 is not a command; a reply sent again has st3 back to
	 * '0' (FUN_00007fce) */
	resp = exchange(r, frame(0x40, {}), acked);
	check(acked && response_ok(resp, 0x40, ST1_EMPTY, '0', '2', 0), "cancel at rest: refused");
	check(response_ok(enq(r), 0x40, ST1_EMPTY, '0', '0', 0), "ENQ again: the reply with st3 back to '0'");
	r.receive(0x06);
	check(drain(r).empty(), "ACK from the master: silent");
	Bytes bad = frame(0x20, {});
	bad.back() = static_cast<uint8_t>(bad.back() ^ 0x01);
	for (uint8_t b : bad) {
		r.receive(b);
	}
	check(drain(r) == Bytes({ 0x15 }), "bad xor: NAK");
	r.receive(0x15);
	check(response_ok(drain(r), 0x40, ST1_EMPTY, '0', '0', 0), "NAK from the master: the reply again");
	for (uint8_t b : frame(0x20, {})) {
		r.receive(b);
	}
	check(drain(r) == Bytes({ 0x06 }), "status ACKed");
	r.receive(0x15);
	check(drain(r).empty(), "NAK before the ENQ of a frame: nothing");
	check(response_ok(enq(r), 0x20, ST1_EMPTY, '0', '0', 0), "status on its ENQ");

	/* Shutter: refused when already as asked */
	resp = exchange(r, frame(0xD0, { '0' }), acked);
	check(acked && response_ok(resp, 0xD0, ST1_EMPTY, '0', '2', 0), "close a closed shutter: refused");
	/* Shutter closed, no card: read and write refused at once */
	resp = exchange(r, frame(0x33, { '0', '1', '6' }), acked);
	check(response_ok(resp, 0x33, ST1_EMPTY, '0', '2', 0), "read, shutter closed, no card: refused");
	resp = exchange(r, frame(0x53, with({ '0', '1', '6' }, tracks('A'))), acked);
	check(response_ok(resp, 0x53, ST1_EMPTY, '0', '2', 0), "write, no card: refused");
	resp = exchange(r, frame(0x80, {}), acked);
	check(response_ok(resp, 0x80, ST1_EMPTY, '0', '2', 0), "eject, no card: refused");
	resp = exchange(r, frame(0x7C, { '0', '0', 1 }), acked);
	check(response_ok(resp, 0x7C, ST1_EMPTY, '0', '2', 0), "print, no card: refused");

	/* Dispense: a new card at S3S4S5, its track 1 alone recorded */
	resp = exchange(r, frame(0xB0, { '1' }), acked);
	check(response_ok(resp, 0xB0, st1(P::S3S4S5, false), '0', '0', 0) && r.position() == P::S3S4S5, "dispense");
	resp = exchange(r, frame(0xB0, { '1' }), acked);
	check(response_ok(resp, 0xB0, st1(P::S3S4S5, false), '0', '2', 0), "dispense with a card in: refused");
	resp = exchange(r, frame(0x33, { '0', '1', '6' }), acked);
	check(response_ok(resp, 0x33, st1(P::S1S2, false), 'V', '0', 3 * 0x45) && data_of(resp) == Bytes(3 * 0x45, 0),
	      "read a new card: 'V' (tracks 2 and 3 blank), 207 zeros, the card at S1S2");
	resp = exchange(r, frame(0x33, { '0', '1', '1' }), acked);
	check(response_ok(resp, 0x33, st1(P::S1S2, false), '1', '0', 0), "read its blank track 2 alone: '1', no data");
	resp = exchange(r, frame(0x33, { '0', '1', '5' }), acked);
	check(response_ok(resp, 0x33, st1(P::S1S2, false), '1', '0', 0), "tracks 2 and 3, both blank: '1'");
	resp = exchange(r, frame(0x33, { '0', '1', '3' }), acked);
	check(response_ok(resp, 0x33, st1(P::S1S2, false), 'R', '0', 2 * 0x45), "tracks 1 and 2: 'R' (track 2 bad)");

	/* Write the three tracks, read them back */
	const Bytes three = tracks('A');
	resp = exchange(r, frame(0x53, with({ '0', '1', '6' }, three)), acked);
	check(response_ok(resp, 0x53, st1(P::S4S5, false), '0', '0', 0) && r.position() == P::S4S5, "write: the card at S4S5");
	resp = exchange(r, frame(0x33, { '0', '1', '6' }), acked);
	check(response_ok(resp, 0x33, st1(P::S4S5, false), '0', '0', 3 * 0x45) && data_of(resp) == three, "read back");
	resp = exchange(r, frame(0x33, { '0', '1', '4' }), acked);
	check(response_ok(resp, 0x33, st1(P::S4S5, false), '0', '0', 2 * 0x45) &&
	      data_of(resp) == with(Bytes(three.begin(), three.begin() + 0x45), Bytes(three.begin() + 0x8A, three.end())),
	      "read tracks 1 and 3");
	const uint8_t *raw = r.card().data();
	check(raw[0] == 0xFF && raw[1] == 'A' && raw[0x46] == 0xFF &&
	      raw[0x47] == odin::CardReader::frame_xor(std::span<const uint8_t>(raw + 1, 0x46)), "a track as on the card: FF, 69 bytes, FF, LRC");

	/* 7 bits with parity: even parity on each byte, masked on reading */
	resp = exchange(r, frame(0x53, with({ '0', '0', '0' }, Bytes(three.begin(), three.begin() + 0x45))), acked);
	check(response_ok(resp, 0x53, st1(P::S4S5, false), '0', '0', 0) && r.card()[1] == 'A' && r.card()[3] == ('C' | 0x80),
	      "write 7 bits: the parity bit makes each byte even");
	resp = exchange(r, frame(0x33, { '0', '0', '0' }), acked);
	check(response_ok(resp, 0x33, st1(P::S4S5, false), '0', '0', 0x45) && data_of(resp) == Bytes(three.begin(), three.begin() + 0x45),
	      "read 7 bits: the bytes again");
	resp = exchange(r, frame(0x53, { '0', '0', '0', 0x80 }), acked);
	check(response_ok(resp, 0x53, st1(P::S4S5, false), '0', '2', 0), "write 7 bits of a byte above 7F: refused");
	resp = exchange(r, frame(0x33, { '0', '1', '7' }), acked);
	check(response_ok(resp, 0x33, st1(P::S4S5, false), '0', '2', 0), "read of track 7: refused");
	resp = exchange(r, frame(0x33, { '0', '2', '6' }), acked);
	check(response_ok(resp, 0x33, st1(P::S4S5, false), '0', '2', 0), "read with parity 2: refused");

	/* Commands without the mechanism */
	check(response_ok(exchange(r, frame(0xC1, { '1' }), acked), 0xC1, st1(P::S4S5, false), '0', '0', 0) &&
	      response_ok(exchange(r, frame(0xC1, { '0' }), acked), 0xC1, st1(P::S4S5, false), '0', '2', 0),
	      "retries: 1 taken, 0 refused");
	check(response_ok(exchange(r, frame(0x7A, with({ 1 }, Bytes(72, 0x55))), acked), 0x7A, st1(P::S4S5, false), '0', '0', 0) &&
	      response_ok(exchange(r, frame(0x7A, with({ 0x15 }, Bytes(72, 0x55))), acked), 0x7A, st1(P::S4S5, false), '0', '2', 0),
	      "font: slot 1 taken, 15 refused");
	check(response_ok(exchange(r, frame(0x7C, { '0', '0', 1, 0x41, 0x0D }), acked), 0x7C, st1(P::S4S5, false), '0', '0', 0),
	      "print on a card inside");
	check(response_ok(exchange(r, frame(0xC8, {}), acked), 0xC8, st1(P::S4S5, false), '0', '2', 0),
	      "a command it does not answer here: refused");

	/* Init: '1' ejects, refused with the shutter closed; '0' keeps it */
	check(response_ok(exchange(r, frame(0x10, { '1' }), acked), 0x10, st1(P::S4S5, false), '0', '2', 0),
	      "init '1' with a card and the shutter closed: refused");
	check(response_ok(exchange(r, frame(0x10, { '2' }), acked), 0x10, st1(P::S4S5, false), '0', '2', 0), "init '2': refused");
	check(response_ok(exchange(r, frame(0x10, { '0' }), acked), 0x10, st1(P::S4S5, false), '0', '0', 0) &&
	      r.position() == P::S4S5, "init '0': the card stays");
	check(response_ok(exchange(r, frame(0x80, {}), acked), 0x80, st1(P::S4S5, false), '0', '2', 0),
	      "eject, shutter closed: refused");
	check(response_ok(exchange(r, frame(0xA0, {}), acked), 0xA0, st1(P::S4S5, false), '0', '2', 0),
	      "cleaning, shutter closed: refused");
	check(response_ok(exchange(r, frame(0xD0, { '1' }), acked), 0xD0, st1(P::S4S5, true), '0', '0', 0) && r.shutter_open(),
	      "open the shutter");
	check(response_ok(exchange(r, frame(0xD0, { '1' }), acked), 0xD0, st1(P::S4S5, true), '0', '2', 0),
	      "open an open shutter: refused");
	check(response_ok(exchange(r, frame(0x80, {}), acked), 0x80, st1(P::S1, true), '0', '0', 0) && r.position() == P::S1 &&
	      r.where() == odin::CardReader::Where::AT_SLOT, "eject: the card at the slot until the player takes it");
	check(response_ok(exchange(r, frame(0x20, {}), acked), 0x20, st1(P::S1, true), '0', '0', 0),
	      "the game sees it there (S1) until then");
	check(response_ok(exchange(r, frame(0x33, { '0', '1', '6' }), acked), 0x33, st1(P::S1, true), '0', '2', 0),
	      "a read of the card ejected, still at the slot: refused ('F')");
	check(r.take("") && r.position() == P::NONE && r.where() == odin::CardReader::Where::NO_CARD,
	      "taken from the slot (no file: not kept)");
	check(!r.take(""), "nothing at the slot to take");
	check(response_ok(exchange(r, frame(0x20, {}), acked), 0x20, st1(P::NONE, true), '0', '0', 0),
	      "the game sees the slot empty: the eject is over");

	/* A read that waits for a card: '4' on each ENQ; 20 gets it again,
	 * 40 cancels it */
	resp = exchange(r, frame(0x33, { '0', '1', '6' }), acked);
	check(response_ok(resp, 0x33, st1(P::NONE, true), '0', '4', 0), "read, shutter open, no card: waits ('4')");
	check(response_ok(enq(r), 0x33, st1(P::NONE, true), '0', '4', 0), "still '4' on the next ENQ");
	check(response_ok(exchange(r, frame(0x20, {}), acked), 0x33, st1(P::NONE, true), '0', '4', 0) && acked,
	      "a 20 while it waits: the reply of the read");
	odin::CardReaderState waiting;
	r.save(waiting);
	check(response_ok(exchange(r, frame(0x40, {}), acked), 0x40, st1(P::NONE, true), '0', '0', 0) && acked,
	      "a 40 while it waits: cancelled");
	check(response_ok(exchange(r, frame(0x20, {}), acked), 0x20, st1(P::NONE, true), '0', '0', 0), "then at rest");
	r.load(waiting);
	check(response_ok(enq(r), 0x33, st1(P::NONE, true), '0', '4', 0), "state loaded: waiting again");
	for (uint8_t b : frame(0xFF, {})) {
		r.receive(b);
	}
	check(drain(r) == Bytes({ 0x06 }) && enq(r).empty(), "an FF while it waits: ACK, then no reply (the reader resets)");
	check(response_ok(enq(r), 0x10, st1(P::NONE, false), '0', '0', 0), "then its boot reply, the shutter closed");
	check(response_ok(exchange(r, frame(0xA0, {}), acked), 0xA0, st1(P::NONE, false), '0', '2', 0) &&
	      response_ok(exchange(r, frame(0xD0, { '1' }), acked), 0xD0, st1(P::NONE, true), '0', '0', 0) &&
	      response_ok(exchange(r, frame(0xA0, {}), acked), 0xA0, st1(P::NONE, true), '0', '0', 0),
	      "cleaning with the shutter open");

	/* Stocker empty */
	r.set_stocker(false);
	check(response_ok(exchange(r, frame(0xB0, { '1' }), acked), 0xB0, st1(P::NONE, true, false), '0', '5', 0),
	      "dispense, stocker empty: '5'");
	r.set_stocker(true);

	/* A reset keeps a card inside */
	exchange(r, frame(0xB0, { '1' }), acked);
	r.reset();
	check(r.position() == P::S3S4S5 && !r.has_byte() && response_ok(enq(r), 0x10, st1(P::S3S4S5, false), '0', '0', 0),
	      "reset keeps the card, boot reply");

	if (!scratch) {
		return;
	}

	/* The player's card: in hand, inserted by the player (the reader
	 * waits as long as it takes), at the slot after an eject until the
	 * player takes it back, kept in its file */
	using Where = odin::CardReader::Where;
	const std::string file = std::string(scratch) + ".card";
	const std::string fresh_file = std::string(scratch) + "-new.card";
	const Bytes old = tracks('a');
	check(write_file(file, old), "(a card of 207 bytes, data only)");
	odin::CardReader p(file);
	check(p.where() == Where::IN_HAND && p.card_in_hand(), "the player holds the card of the file");
	check(!p.insert() && p.where() == Where::IN_HAND, "the shutter closed: the card does not go in");
	exchange(p, frame(0xD0, { '1' }), acked);
	resp = exchange(p, frame(0x33, { '0', '1', '6' }), acked);
	check(response_ok(resp, 0x33, st1(P::NONE, true), '0', '4', 0) && p.card_in_hand(),
	      "a read waits ('4'): nothing goes in by itself");
	check(response_ok(enq(p), 0x33, st1(P::NONE, true), '0', '4', 0), "still waiting");
	check(p.insert() && p.where() == Where::AT_SLOT && !p.card_in_hand(), "the player inserts it: at the entrance");
	resp = enq(p);
	check(response_ok(resp, 0x33, st1(P::S4S5, true), '0', '0', 3 * 0x45) && data_of(resp) == old &&
	      p.where() == Where::INSIDE, "the read waiting takes it in: its 207 bytes read as recorded");
	check(!p.choose(""), "the card in the reader: no other in hand");
	resp = exchange(p, frame(0x53, with({ '0', '1', '6' }, three)), acked);
	check(response_ok(resp, 0x53, st1(P::S4S5, true), '0', '0', 0) && read_file(file).size() == 216,
	      "write: the file kept as on the card, 216 bytes");
	resp = exchange(p, frame(0x80, {}), acked);
	check(response_ok(resp, 0x80, st1(P::S1, true), '0', '0', 0) && p.where() == Where::AT_SLOT, "eject: at the slot");
	check(p.take(fresh_file) && p.where() == Where::IN_HAND && p.card_file() == file &&
	      read_file(fresh_file).empty(), "taken back: in hand, in its own file");
	check(response_ok(exchange(p, frame(0x20, {}), acked), 0x20, st1(P::NONE, true), '0', '0', 0),
	      "the game sees no card any more");
	check(p.insert(), "inserted again, the shutter open, no read waiting");
	resp = exchange(p, frame(0x33, { '0', '1', '6' }), acked);
	check(response_ok(resp, 0x33, st1(P::S4S5, true), '0', '0', 3 * 0x45) && data_of(resp) == three,
	      "a card at the entrance: the read takes it in at once, what was written");
	exchange(p, frame(0x80, {}), acked);
	p.take(fresh_file);

	/* A new card when the player has one: its own file, then the
	 * player's; the own card untouched */
	exchange(p, frame(0xB0, { '1' }), acked);
	check(p.where() == Where::IN_HAND, "a new card in the reader: the player's own still in hand");
	exchange(p, frame(0x53, with({ '0', '1', '6' }, tracks('0'))), acked);
	exchange(p, frame(0x80, {}), acked);
	check(p.take(fresh_file) && p.card_file() == fresh_file, "the new card taken back: kept in its own file");
	const Bytes own = read_file(file), fresh = read_file(fresh_file);
	check(own.size() == 216 && own[1] == 'A' && fresh.size() == 216 && fresh[1] == '0', "the own card untouched");
	check(p.choose(file) && p.card_file() == file && p.where() == Where::IN_HAND, "the other card chosen in hand");
	std::remove(fresh_file.c_str());

	/* No card yet: the new card becomes the player's */
	std::remove(file.c_str());
	odin::CardReader q(file);
	check(q.where() == Where::NO_CARD && !q.insert(), "(no card file: no card in hand)");
	exchange(q, frame(0xB0, { '1' }), acked);
	exchange(q, frame(0x53, with({ '0', '1', '6' }, tracks('k'))), acked);
	exchange(q, frame(0xD0, { '1' }), acked);
	exchange(q, frame(0x80, {}), acked);
	check(q.take(file) && read_file(file).size() == 216 && read_file(file)[1] == 'k' && q.card_in_hand(),
	      "a new card for a player without one: theirs, in the file");
	std::remove(file.c_str());

	/* A new card ejected before anything is written on it: kept as it
	 * is, by an init '1' (the shutter open) */
	odin::CardReader n("");
	exchange(n, frame(0xD0, { '1' }), acked);
	exchange(n, frame(0xB0, { '1' }), acked);
	resp = exchange(n, frame(0x10, { '1' }), acked);
	check(response_ok(resp, 0x10, st1(P::S1, true), '0', '0', 0) && n.where() == Where::AT_SLOT,
	      "init '1' ejects: at the slot");
	check(n.take(file) && n.card_in_hand(), "taken back");
	const Bytes kept = read_file(file);
	check(kept.size() == 216 && kept[0] == 0xFF && kept[0x48] == 0, "the new card kept as it is");

	/* A track whose LRC is wrong does not read */
	Bytes broken = kept;
	broken.resize(216);
	broken[0] = 0xFF;
	broken[0x48] = 0xFF;
	broken[0x48 + 0x46] = 0xFF;
	broken[0x48 + 0x47] = 0x01;
	check(write_file(file, broken), "(a card whose track 2 has a wrong LRC)");
	odin::CardReader l(file);
	exchange(l, frame(0xD0, { '1' }), acked);
	l.insert();
	resp = exchange(l, frame(0x33, { '0', '1', '6' }), acked);
	check(response_ok(resp, 0x33, st1(P::S1S2, true), 'V', '0', 3 * 0x45), "a wrong LRC on track 2: not read ('V')");
	std::remove(file.c_str());

	/* The file written whole or not at all: the card before kept as
	 * <file>.bak, the same card not written again, a card not whole not
	 * written; a file cut short gives way to its copy */
	using Card = std::array<uint8_t, odin::CardReader::CARD_SIZE>;
	using odin::CardReader;
	const std::string bak = file + ".bak";
	const auto card_of = [](uint8_t c) {
		Card card{};

		for (uint32_t t = 0; t < CardReader::TRACKS; t++) {
			uint8_t *const track = card.data() + t * CardReader::TRACK_SIZE;

			track[0] = 0xFF;
			std::fill_n(track + 1, CardReader::TRACK_DATA, c);
			track[CardReader::TRACK_DATA + 1] = 0xFF;
			track[CardReader::TRACK_DATA + 2] =
				CardReader::frame_xor(std::span<const uint8_t>(track + 1, CardReader::TRACK_DATA + 1));
		}
		return card;
	};
	const auto bytes_of = [](const Card &card) { return Bytes(card.begin(), card.end()); };
	const Card one = card_of('1'), two = card_of('2');
	Card spoilt = two;

	spoilt[CardReader::TRACK_SIZE + CardReader::TRACK_DATA + 2] ^= 1;
	std::remove(bak.c_str());
	check(CardReader::whole_card(CardReader::blank_card()) && CardReader::whole_card(one) && !CardReader::whole_card(spoilt),
	      "a new card and a written one whole, a wrong LRC not");
	check(CardReader::write_card(file, one, nullptr) && read_file(file) == bytes_of(one) && read_file(bak).empty(),
	      "a first card: written, no copy");
	check(CardReader::write_card(file, two, nullptr) && read_file(file) == bytes_of(two) && read_file(bak) == bytes_of(one),
	      "another card: the one before kept in .bak");
	check(CardReader::write_card(file, two, nullptr) && read_file(bak) == bytes_of(one), "the same card again: the copy stays");
	check(!CardReader::write_card(file, spoilt, nullptr) && read_file(file) == bytes_of(two), "a card not whole: not written");
	check(write_file(file, Bytes(two.begin(), two.begin() + 100)), "(the file cut short)");
	CardReader c(file);
	check(c.where() == Where::IN_HAND, "a card cut short: in hand all the same");
	exchange(c, frame(0xD0, { '1' }), acked);
	c.insert();
	resp = exchange(c, frame(0x33, { '0', '1', '6' }), acked);
	check(response_ok(resp, 0x33, st1(P::S4S5, true), '0', '0', 3 * 0x45) && data_of(resp) == Bytes(3 * 0x45, '1'),
	      "its copy read in its place");
	std::remove(file.c_str());
	std::remove(bak.c_str());
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
	/* The reader did get the STX; the frame after it, read with LEN 02,
	 * is dropped without a word (LEN below 6: FUN_000077fa state 3) and
	 * the next one is clean. */
	check(b.send_frame(frame(0x40, {})) == RESULT_OK, "send: frame after a stray byte");
	check(b.read_byte(v) == RESULT_NOTHING, "stray STX: the frame dropped, nothing on the line");

	/* The game's first three exchanges: 40 and D0 '0' refused at rest
	 * (the shutter already closed), 10 '0' done */
	const Bytes init[3] = { frame(0x40, {}), frame(0xD0, { '0' }), frame(0x10, { '0' }) };
	const uint8_t cmds[3] = { 0x40, 0xD0, 0x10 };
	const uint8_t st3s[3] = { '2', '2', '0' };
	for (uint32_t i = 0; i < 3; i++) {
		check(b.send_frame(init[i]) == RESULT_OK, "send: frame with echo per byte");
		check(b.read_byte(v) == RESULT_OK && v == 0x06, "read byte: ACK");
		check(b.read_byte(v) == RESULT_NOTHING, "read byte: nothing more before ENQ");
		check(b.send_enq() == RESULT_OK, "ENQ echoed");
		check(b.read_frame(got) == RESULT_OK && response_ok(got, cmds[i], ST1_EMPTY, '0', st3s[i], 0),
		      "read frame: response");
	}
	check(b.read_frame(got) == RESULT_NOTHING, "read frame: nothing after the response");

	/* Read of a dispensed card through the ports: 'V', 207 data bytes. */
	check(b.send_frame(frame(0xB0, { '1' })) == RESULT_OK && b.read_byte(v) == RESULT_OK && v == 0x06 &&
	      b.send_enq() == RESULT_OK && b.read_frame(got) == RESULT_OK, "dispense through the ports");
	check(b.send_frame(frame(0x33, { '0', '1', '6' })) == RESULT_OK && b.read_byte(v) == RESULT_OK && v == 0x06 &&
	      b.send_enq() == RESULT_OK && b.read_frame(got) == RESULT_OK &&
	      response_ok(got, 0x33, st1(P::S1S2, false), 'V', '0', 3 * 0x45), "read all tracks through the ports");

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
