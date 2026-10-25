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

/* The program of the 837-13551, the I/O board of five Hikaru games
 * (315-6215 in their sets), run by the TMP90PH44 of Odin, against its
 * imitation (io_837_13551_imitation.cpp): the replies to the frames the MIE program
 * sends, byte for byte, the sense line to the host, the driver outputs,
 * and the time the board takes to start each reply.
 *
 * The board around the chip, as the program uses it:
 *  - the switches, closed is 0: the port and the bit of each are those of
 *    the tables its tick reads (LD HL,table; CALL 068A), and the bit of the
 *    reply each gives is where 12F8 puts its state (FF10 the system byte,
 *    FF11-FF12 player 1, FF13-FF14 player 2), both read from the program;
 *  - the coin switches (the tables of 06F9) and the return pins of the
 *    coin meters (the tables of 0926), which read 1: 0 stops the slot
 *    (0926), so the meters are wired, as on a cabinet;
 *  - AN0-AN3 are channels 1-4 while P56 is 1 and 5-8 while it is 0: 127E
 *    reads the four into FF15-FF18 then clears P56, into FF19-FF1C then
 *    sets it, and 0A46 sends FF15-FF1C as channels 1-8;
 *  - P3 bit 4 at 0 drives the line (147C, 142D), and the bytes the board
 *    sends are not heard back; P3 bits 0-1 read the sense line from the
 *    next board: 3 for none (1450: with 3 or 0 the board takes an address
 *    from F1, with 2 or 1 it does not); P3 bit 2 drives the sense line to
 *    the host, 0 once the board has an address (1471);
 *  - the driver outputs: P6 bits 1-3 outputs 1-3 and P7 bits 1-3 outputs
 *    4-6, on at 0 (12BA).
 * The chip runs from 14.7456 MHz: a state is two periods, a byte on the
 * line 640 states (115200 bauds), its tick (the watchdog) 32768.
 *
 * Not sent: what the imitation does not copy (TODO): DATA_RETRANSMIT,
 * SWITCH_INPUTS with more than 2 bytes a player, ANALOG_INPUTS for 0 or
 * more than 8 channels, SET_ADDRESS to a board that has one.
 *
 * Not run with the unit tests: it needs the PROM.
 * Build: g++ -std=c++20 -O2 -Wall -Wextra -Wpedantic -Wconversion -Wshadow \
 *            -I src tests/jvs/s6215_bench.cpp src/odin/tmp90ph44.cpp src/odin/tlcs90.cpp \
 *            src/odin/jvs.cpp src/odin/io_837_13551_imitation.cpp -o s6215_bench
 * Run: s6215_bench <315-6215.bin>. Exit status 0 when every reply, sense
 * line and output is the same. */

#include "odin/io_837_13551.hpp"
#include "odin/jvs.hpp"
#include "odin/io_837_13551_imitation.hpp"
#include "odin/tmp90ph44.hpp"

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <map>
#include <string>
#include <utility>
#include <vector>

namespace {

using odin::Tmp90ph44;

constexpr double STATES_PER_US = 14.7456 / 2;
constexpr uint64_t SECOND = 7372800;
constexpr uint64_t TICK = 32768;

[[noreturn]] void fail(const char *what, unsigned at)
{
	std::fprintf(stderr, "the program is not as this bench reads it: %s at %04X\n", what, at);
	std::exit(2);
}

/* What the inputs read: the bytes of SWITCH_INPUTS (the system byte, then
 * two a player), the analog channels, the coin switches */
struct Panel {
	std::array<uint8_t, 5> switches{};
	std::array<uint8_t, 8> analog{};
	bool coin[2] = { false, false };
};

/* A bit of a port the program reads */
struct Pin {
	unsigned port;
	uint8_t mask;
};

struct Wiring {
	/* Each switch: its pin, and the byte and bit of the reply */
	struct Switch {
		Pin pin;
		unsigned byte;
		uint8_t bit;
	};
	std::vector<Switch> switches;
	Pin coin[2];
	std::vector<Pin> meters;
};

uint16_t word(const std::vector<uint8_t> &rom, size_t at)
{
	return static_cast<uint16_t>(rom[at] | rom[at + 1] << 8);
}

/* The tables the program hands to `routine`: LD HL,table; CALL routine */
std::vector<uint16_t> tables_of(const std::vector<uint8_t> &rom, uint16_t routine)
{
	std::vector<uint16_t> tables;

	for (size_t i = 0; i + 6 <= rom.size(); i++)
		if (rom[i] == 0x3A && rom[i + 3] == 0x1C && word(rom, i + 4) == routine)
			tables.push_back(word(rom, i + 1));
	return tables;
}

/* P0 to P4 are at FFC0, FFC2... FFC8 */
Pin pin_at(const std::vector<uint8_t> &rom, uint16_t table, size_t port_at, size_t mask_at)
{
	const uint16_t port = word(rom, table + port_at);

	if (port < 0xFFC0 || port > 0xFFC8 || (port & 1))
		fail("a table reads no port", table);
	return { (port - 0xFFC0u) / 2, rom[table + mask_at] };
}

/* Where 12F8 puts each state: LD D,(FFnn); SLA D moves its bit 7, set
 * while the switch is closed (068A), into the carry, which RL A or RR A
 * shift in; SLA A shifts a 0 in; LD (FF10+n),A stores byte n. */
std::map<uint16_t, std::pair<unsigned, uint8_t>> packing(const std::vector<uint8_t> &rom)
{
	std::map<uint16_t, std::pair<unsigned, uint8_t>> where;
	std::array<uint16_t, 8> a{};	/* the state each bit of A holds, 0 for none */
	uint16_t d = 0, carry = 0;

	for (size_t pc = 0x12F8;;) {
		const uint8_t op = rom[pc];

		if (op == 0xFE && rom[pc + 1] == 0x65) {
			a.fill(0);
			pc += 2;
		} else if (op == 0xE7 && rom[pc + 2] == 0x2A) {
			d = static_cast<uint16_t>(0xFF00 | rom[pc + 1]);
			pc += 3;
		} else if (op == 0xFA && rom[pc + 1] == 0xA4) {
			if (!d)
				fail("SLA D on no state", static_cast<unsigned>(pc));
			carry = d;
			d = 0;
			pc += 2;
		} else if (op == 0xA2 || op == 0xA4) {
			const uint16_t in = op == 0xA2 ? carry : 0;

			carry = a[7];
			std::copy_backward(a.begin(), a.end() - 1, a.end());
			a[0] = in;
			pc++;
		} else if (op == 0xA3) {
			const uint16_t in = carry;

			carry = a[0];
			std::copy(a.begin() + 1, a.end(), a.begin());
			a[7] = in;
			pc++;
		} else if (op == 0x2F && rom[pc + 1] >= 0x10 && rom[pc + 1] <= 0x14) {
			for (unsigned bit = 0; bit < 8; bit++)
				if (a[bit] && !where.emplace(a[bit], std::make_pair(rom[pc + 1] - 0x10u, uint8_t(1u << bit))).second)
					fail("a state in two bits", static_cast<unsigned>(pc));
			pc += 2;
		} else if (op == 0x1E) {
			return where;
		} else {
			fail("an instruction not known", static_cast<unsigned>(pc));
		}
	}
}

Wiring read_wiring(const std::vector<uint8_t> &rom)
{
	Wiring wiring;
	const auto where = packing(rom);

	/* 068A: the state at +0, the port at +4, the bit at +6 */
	for (uint16_t table : tables_of(rom, 0x068A)) {
		const auto it = where.find(word(rom, table));

		if (it == where.end())
			fail("a switch not in the reply", table);
		wiring.switches.push_back({ pin_at(rom, table, 4, 6), it->second.first, it->second.second });
	}
	if (wiring.switches.size() != where.size() || where.size() != 2 + 2 * 13)
		fail("switches not 2 + 2 x 13", 0x12F8);

	/* 06F9: the port at +4, the bit at +6, the counter at +B: FE60 for
	 * slot 1, FE62 for slot 2 (0EF9, 0F5D) */
	const auto coins = tables_of(rom, 0x06F9);
	if (coins.size() != 2)
		fail("coin slots not 2", 0x06F9);
	for (uint16_t table : coins) {
		const uint16_t counter = word(rom, table + 0x0B);

		if (counter != 0xFE60 && counter != 0xFE62)
			fail("a coin counter not known", table);
		wiring.coin[(counter - 0xFE60u) / 2] = pin_at(rom, table, 4, 6);
	}
	/* 0926: the return pin at +E, its bit at +10 */
	for (uint16_t table : tables_of(rom, 0x0926))
		wiring.meters.push_back(pin_at(rom, table, 0x0E, 0x10));
	return wiring;
}

class Board : public odin::Tmp90ph44Board {
public:
	Board(const Panel &panel, const Wiring &wiring) : m_panel(panel), m_wiring(wiring) {}
	void attach(const Tmp90ph44 &chip) { m_chip = &chip; }

	/* The bytes the board sent, with the state their start bit began */
	std::vector<std::pair<uint64_t, uint8_t>> &sent() { return m_sent; }
	/* Bytes sent with the line not driven, external bus cycles, analog
	 * reads with P56 not driven */
	unsigned faults() const { return m_faults; }
	/* The sense line to the host: the board has an address */
	bool addressed() const { return (m_driven[3] & 0x04) && !(m_latch[3] & 0x04); }
	/* The driver outputs as JVS numbers them, output 1 bit 7 */
	uint8_t outputs() const
	{
		uint8_t on = 0;

		for (unsigned n = 0; n < 6; n++) {
			const unsigned port = n < 3 ? 6 : 7;
			const uint8_t bit = static_cast<uint8_t>(2u << (n % 3));

			if ((m_driven[port] & bit) && !(m_latch[port] & bit))
				on |= static_cast<uint8_t>(0x80 >> n);
		}
		return on;
	}

	uint8_t bus_read(uint16_t) override
	{
		m_faults++;
		return 0xFF;
	}
	void bus_write(uint16_t, uint8_t) override { m_faults++; }

	uint8_t port_pins(unsigned port) override
	{
		uint8_t pins = 0xFF;

		if (port >= 6)
			return 0x0F;
		for (const auto &s : m_wiring.switches)
			if (s.pin.port == port && (m_panel.switches[s.byte] & s.bit))
				pins = static_cast<uint8_t>(pins & ~s.pin.mask);
		for (unsigned slot = 0; slot < 2; slot++)
			if (m_wiring.coin[slot].port == port && m_panel.coin[slot])
				pins = static_cast<uint8_t>(pins & ~m_wiring.coin[slot].mask);
		return pins;
	}

	void port_out(unsigned port, uint8_t latch, uint8_t driven) override
	{
		m_latch[port] = latch;
		m_driven[port] = driven;
	}

	uint8_t analog_pin(unsigned n) override
	{
		if (!(m_driven[5] & 0x40))
			m_faults++;
		return m_panel.analog[(m_latch[5] & 0x40) ? n : n + 4];
	}

	void serial_sent(uint8_t byte) override
	{
		if (!(m_driven[3] & 0x10) || (m_latch[3] & 0x10))
			m_faults++;
		m_sent.emplace_back(m_chip->states() - Tmp90ph44::BYTE_STATES, byte);
	}

private:
	const Panel &m_panel;
	const Wiring &m_wiring;
	const Tmp90ph44 *m_chip = nullptr;
	std::array<uint8_t, 8> m_latch{}, m_driven{};
	std::vector<std::pair<uint64_t, uint8_t>> m_sent;
	unsigned m_faults = 0;
};

/* The imitation reads the same panel */
class Inputs : public odin::JvsInputs {
public:
	explicit Inputs(const Panel &panel) : m_panel(panel) {}
	uint8_t system() override { return m_panel.switches[0]; }
	uint16_t player(uint8_t n) override
	{
		return static_cast<uint16_t>(m_panel.switches[1 + 2 * n] << 8 | m_panel.switches[2 + 2 * n]);
	}
	uint16_t analog(uint8_t channel) override { return static_cast<uint16_t>(m_panel.analog[channel & 7] << 8); }

private:
	const Panel &m_panel;
};

/* A frame as the MIE program sends it (listing 0x876F) */
std::vector<uint8_t> frame(uint8_t dest, const std::vector<uint8_t> &data)
{
	std::vector<uint8_t> body = { dest, static_cast<uint8_t>(data.size() + 1) };
	std::vector<uint8_t> out = { 0xE0 };
	uint8_t sum = 0;

	body.insert(body.end(), data.begin(), data.end());
	for (uint8_t b : body)
		sum = static_cast<uint8_t>(sum + b);
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

std::string hex(const std::vector<uint8_t> &bytes)
{
	std::string text;
	char two[4];

	for (uint8_t b : bytes) {
		std::snprintf(two, sizeof(two), "%02X ", b);
		text += two;
	}
	return text;
}

struct Bench {
	const std::vector<uint8_t> &rom;
	Tmp90ph44 &chip;
	Board &board;
	odin::JvsHost &host;
	odin::Io837_13551Imitation &imitation;
	unsigned same = 0, different = 0, raced = 0;
	bool rx_open = false, in_tx = false, tx_open = false, race = false;

	/* Runs to `at` an instruction at a time, watching for the tick taken
	 * while a serial interrupt has not done with SCBUF: from the vector of
	 * INTRX (0060) to its read of SCBUF (LD A,(FFE8)), and from the write
	 * of SCBUF (LD (FFE8),A) in the INTTX handler (0068) to its RETI
	 * (006B). The tick does not wait for them, and opens the interrupts
	 * (019A): a byte coming in then is lost, as SCBUF is not read; a byte
	 * sent then asks for the INTTX handler again, which sends the byte
	 * before again, its next one not made yet. */
	void run_until(uint64_t at)
	{
		while (chip.states() < at) {
			chip.run(1);
			const uint16_t pc = chip.cpu().pc();

			if (pc == 0x0060)
				rx_open = true;
			else if (pc == 0x0068)
				in_tx = true;
			else if (pc == 0x006B)
				in_tx = tx_open = false;
			else if (pc == 0x0020 && (rx_open || tx_open))
				race = true;
			if (pc + 1u < rom.size() && rom[pc + 1] == 0xE8) {
				if (rom[pc] == 0x27)
					rx_open = false;
				else if (rom[pc] == 0x2F && in_tx)
					tx_open = true;
			}
		}
	}

	void wait(uint64_t states) { run_until(chip.states() + states); }

	/* Sends the frame to both, its bytes back to back. Returns the states
	 * from the end of the last byte to the start of the reply, or -1 when
	 * the board stays silent or the tick hit a serial interrupt (run_until):
	 * those frames are counted apart. */
	int64_t send(const char *what, const std::vector<uint8_t> &bytes, uint64_t wait_us = 20000, bool told = true)
	{
		const uint64_t start = chip.states() + 10 * 7;

		board.sent().clear();
		race = false;
		for (size_t i = 0; i < bytes.size(); i++) {
			run_until(start + i * Tmp90ph44::BYTE_STATES + Tmp90ph44::RX_STATES);
			chip.serial_received(bytes[i]);
		}
		const uint64_t end = start + bytes.size() * Tmp90ph44::BYTE_STATES;
		run_until(end + static_cast<uint64_t>(static_cast<double>(wait_us) * STATES_PER_US));

		std::vector<uint8_t> reply;
		for (const auto &[at, value] : board.sent())
			reply.push_back(value);
		const int64_t delay = board.sent().empty() ? -1 : static_cast<int64_t>(board.sent().front().first - end);

		for (uint8_t b : bytes)
			host.push(b);
		host.commit_encoded();
		const std::span<const uint8_t> made = host.reply();
		const std::vector<uint8_t> imitated(made.begin(), made.end());

		const bool sense = board.addressed() == host.address_set_line();
		const bool outputs = board.outputs() == (imitation.outputs()[0] & 0xFC);
		const bool ok = reply == imitated && sense && outputs;
		(race ? raced : ok ? same : different)++;
		const char *verdict = race ? (ok ? "race same" : "race diff") : ok ? "same     " : "DIFFERENT";
		if ((told || !ok || race) && delay < 0)
			std::printf("%s %-36s silent\n", verdict, what);
		else if (told || !ok || race)
			std::printf("%s %-36s %2zu bytes, the board starts %6.1f us after the request (%.2f characters)\n", verdict,
			            what, reply.size(), static_cast<double>(delay) / STATES_PER_US,
			            static_cast<double>(delay) / Tmp90ph44::BYTE_STATES);
		if (reply != imitated)
			std::printf("    board:     %s\n    imitation: %s\n", hex(reply).c_str(), hex(imitated).c_str());
		if (!sense)
			std::printf("    sense line: the board %s, the imitation %s\n", board.addressed() ? "addressed" : "not addressed",
			            host.address_set_line() ? "addressed" : "not addressed");
		if (!outputs)
			std::printf("    outputs: the board %02X, the imitation %02X\n", board.outputs(),
			            imitation.outputs()[0] & 0xFC);
		return race ? -1 : delay;
	}
};

const char *const BYTE_NAMES[] = { "system", "player 1", "player 1 low", "player 2", "player 2 low" };

} /* namespace */

int main(int argc, char **argv)
{
	if (argc < 2) {
		std::fprintf(stderr, "usage: s6215_bench <315-6215.bin>\n");
		return 2;
	}
	std::ifstream file(argv[1], std::ios::binary);
	std::vector<uint8_t> rom((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
	if (rom.size() != Tmp90ph44::ROM_SIZE) {
		std::fprintf(stderr, "315-6215 is 16 KB\n");
		return 2;
	}

	const Wiring wiring = read_wiring(rom);
	Panel panel;
	Board board(panel, wiring);
	Tmp90ph44 chip(rom, board);
	board.attach(chip);
	Inputs inputs(panel);
	odin::Io837_13551Imitation imitation(inputs);
	odin::JvsHost host;
	host.add_device(&imitation);
	Bench bench{ rom, chip, board, host, imitation };

	std::printf("from the program: %zu switches, coin slots on P%u bit %u and P%u bit %u, %zu coin meters\n",
	            wiring.switches.size(), wiring.coin[0].port, __builtin_ctz(wiring.coin[0].mask), wiring.coin[1].port,
	            __builtin_ctz(wiring.coin[1].mask), wiring.meters.size());

	/* The pins of the board in Odin (io_837_13551.hpp) are those */
	unsigned pins_wrong = 0;
	for (const auto &s : wiring.switches) {
		const auto same = [&s](const odin::Io837_13551Pin &p) {
			const unsigned byte = p.word == 0 ? 0 : 2u * p.word - (p.bit >= 8 ? 1u : 0u);
			return p.port == s.pin.port && p.mask == s.pin.mask && byte == s.byte &&
			       (1u << (p.bit & 7)) == s.bit;
		};
		if (std::count_if(odin::Io837_13551::PINS.begin(), odin::Io837_13551::PINS.end(), same) != 1)
			pins_wrong++;
	}
	for (unsigned slot = 0; slot < 2; slot++)
		if (odin::Io837_13551::COIN_PORT[slot] != wiring.coin[slot].port ||
		    odin::Io837_13551::COIN_MASK != wiring.coin[slot].mask)
			pins_wrong++;
	for (const Pin &m : wiring.meters)
		if (m.port != odin::Io837_13551::COIN_PORT[m.port == 4] || m.mask != odin::Io837_13551::METER_MASK)
			pins_wrong++;
	std::printf("the pins of io_837_13551.hpp: %s\n", pins_wrong ? "NOT those of the program" : "those of the program");

	/* Its start, then its loop */
	bench.wait(SECOND);
	std::printf("after 1 s the board runs at %04X\n", chip.cpu().pc());

	/* As the MIE program starts the line (830F): two resets 1.27 s apart,
	 * the address, the versions and the features, the name. */
	bench.send("F0 D9 to every node", frame(0xFF, { 0xF0, 0xD9 }), 1270000);
	bench.send("F0 D9 to every node", frame(0xFF, { 0xF0, 0xD9 }), 1270000);
	bench.send("F1 01", frame(0xFF, { 0xF1, 0x01 }));
	bench.send("11 12 13 14", frame(0x01, { 0x11, 0x12, 0x13, 0x14 }));
	bench.send("10", frame(0x01, { 0x10 }));

	/* Each switch closed alone: 3 ticks make it closed (068A) */
	const std::vector<uint8_t> switches = frame(0x01, { 0x20, 0x02, 0x02 });
	bench.send("20 02 02", switches);
	for (const auto &s : wiring.switches) {
		char what[64];

		std::snprintf(what, sizeof(what), "20 02 02, %s bit %u (P%u bit %u)", BYTE_NAMES[s.byte], __builtin_ctz(s.bit),
		              s.pin.port, __builtin_ctz(s.pin.mask));
		panel.switches[s.byte] = s.bit;
		bench.wait(5 * TICK);
		bench.send(what, switches, 20000, false);
		panel.switches[s.byte] = 0;
		bench.wait(5 * TICK);
	}
	std::printf("each of the %zu switches closed alone, then open: %u different so far\n", wiring.switches.size(),
	            bench.different);

	/* The poll of the games, the analog channels in the middle, then
	 * moved: two ticks read the eight (127E) */
	panel.analog = { 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80 };
	bench.wait(4 * TICK);
	bench.send("poll 20 02 02 21 02 22 08", frame(0x01, { 0x20, 0x02, 0x02, 0x21, 0x02, 0x22, 0x08 }));
	panel.analog = { 0x12, 0xE0, 0xD0, 0x00, 0xFF, 0x7F, 0x01, 0x80 };
	bench.wait(4 * TICK);
	bench.send("poll, channels moved", frame(0x01, { 0x20, 0x02, 0x02, 0x21, 0x02, 0x22, 0x08 }));
	panel.switches = { 0x80, 0x82, 0x40, 0x21, 0x08 };
	bench.wait(5 * TICK);
	bench.send("poll, switches closed", frame(0x01, { 0x20, 0x02, 0x02, 0x21, 0x02, 0x22, 0x08 }));
	panel.switches = {};
	bench.wait(5 * TICK);

	/* The driver outputs, which the tick sets (12BA) */
	for (uint8_t value : { uint8_t(0xA8), uint8_t(0x54), uint8_t(0xFC), uint8_t(0x00) }) {
		char what[32];

		std::snprintf(what, sizeof(what), "32 01 %02X", value);
		bench.send(what, frame(0x01, { 0x32, 0x01, value }));
	}
	bench.send("32 03 12 34 56 (refused)", frame(0x01, { 0x32, 0x03, 0x12, 0x34, 0x56 }));

	/* A coin through each slot: 10 ticks closed, 10 open (06F9: 3 and 3) */
	for (uint8_t slot = 0; slot < 2; slot++) {
		panel.coin[slot] = true;
		bench.wait(10 * TICK);
		panel.coin[slot] = false;
		bench.wait(10 * TICK);
		imitation.inc_coin(slot);
		bench.send(slot ? "21 02 after a coin in slot 2" : "21 02 after a coin in slot 1",
		           frame(0x01, { 0x21, 0x02 }));
	}
	bench.send("35 01 00 05", frame(0x01, { 0x35, 0x01, 0x00, 0x05 }));
	bench.send("30 01 00 02", frame(0x01, { 0x30, 0x01, 0x00, 0x02 }));
	bench.send("30 02 00 02 (below 0)", frame(0x01, { 0x30, 0x02, 0x00, 0x02 }));
	bench.send("21 02", frame(0x01, { 0x21, 0x02 }));

	/* What the standard and the program leave to the board */
	bench.send("99 (unknown)", frame(0x01, { 0x99 }));
	bench.send("11 13 99 12", frame(0x01, { 0x11, 0x13, 0x99, 0x12 }));
	bench.send("21 03 11 (refused)", frame(0x01, { 0x21, 0x03, 0x11 }));
	bench.send("32 04 11 12 13 99", frame(0x01, { 0x32, 0x04, 0x11, 0x12, 0x13, 0x99 }));
	bench.send("20 00 00 20 00 01 20 01 00", frame(0x01, { 0x20, 0x00, 0x00, 0x20, 0x00, 0x01, 0x20, 0x01, 0x00 }));
	bench.send("20 03 02 (refused)", frame(0x01, { 0x20, 0x03, 0x02 }));
	bench.send("10 to node 2", frame(0x02, { 0x10 }));
	std::vector<uint8_t> bad = frame(0x01, { 0x10 });
	bad.back() = static_cast<uint8_t>(bad.back() + 1);
	bench.send("10, checksum wrong", bad);
	bench.send("11 F0 D9 12", frame(0x01, { 0x11, 0xF0, 0xD9, 0x12 }), 1270000);
	bench.send("F0 D9 F1 01 to every node", frame(0xFF, { 0xF0, 0xD9, 0xF1, 0x01 }), 1270000);
	bench.send("F1 01", frame(0xFF, { 0xF1, 0x01 }));
	bench.send("10", frame(0x01, { 0x10 }));

	/* The time to the reply, the request ending anywhere in a tick: 64
	 * phases, 1/64 of a tick apart */
	const std::pair<const char *, std::vector<uint8_t>> timed[] = {
		{ "poll 20 02 02 21 02 22 08", { 0x20, 0x02, 0x02, 0x21, 0x02, 0x22, 0x08 } },
		{ "20 02 02", { 0x20, 0x02, 0x02 } },
		{ "21 02", { 0x21, 0x02 } },
		{ "22 08", { 0x22, 0x08 } },
		{ "32 01 00", { 0x32, 0x01, 0x00 } },
		{ "poll and 32 01 00", { 0x20, 0x02, 0x02, 0x21, 0x02, 0x22, 0x08, 0x32, 0x01, 0x00 } },
		{ "11 12 13 14", { 0x11, 0x12, 0x13, 0x14 } },
		{ "10", { 0x10 } },
	};
	for (const auto &[what, data] : timed) {
		std::vector<double> us;

		for (unsigned phase = 0; phase < 64; phase++) {
			bench.wait(TICK * (64 + phase) / 64);
			const int64_t delay = bench.send(what, frame(0x01, data), 20000, false);

			if (delay >= 0)
				us.push_back(static_cast<double>(delay) / STATES_PER_US);
		}
		std::sort(us.begin(), us.end());
		if (us.empty())
			std::printf("%-30s no reply\n", what);
		else
			std::printf("%-30s reply after %.1f us at least, %.1f half the time, %.1f at most (%zu of 64)\n", what,
			            us.front(), us[us.size() / 2], us.back(), us.size());
	}

	std::printf("%u replies the same, %u different, %u frames the tick hit (run_until); %u faults of the board "
	            "(bytes sent with the line not driven, external bus cycles, analog reads with P56 not driven)\n",
	            bench.same, bench.different, bench.raced, board.faults());
	return bench.different || board.faults() || pins_wrong ? 1 : 0;
}
