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

/* The program of the 837-13741, the I/O board of NASCAR (epr-21868, in the
 * 18wheelr set of MAME), run by the board of Odin (io_837_13741.cpp),
 * against its imitation (io_837_13741_imitation.cpp): the replies to the frames the
 * MIE program sends, byte for byte, and the time the board takes to start
 * each reply. A MIE at the other end of the line only takes the bytes.
 *
 * Not run with the unit tests: it needs the EPROM.
 * Build: g++ -std=c++20 -O2 -Wall -Wextra -Wpedantic -Wconversion -Wshadow \
 *            -I src tests/jvs/epr21868_bench.cpp src/odin/machine/cabinet/io_837_13741.cpp src/odin/machine/chips/d71054.cpp \
 *            src/odin/machine/chips/mie.cpp src/odin/machine/cpu/z80/z80.cpp src/odin/machine/cpu/z80/z80_ops.cpp src/odin/machine/cabinet/jvs.cpp \
 *            src/odin/machine/cabinet/io_837_13741_imitation.cpp -o epr21868_bench
 * Run: epr21868_bench <epr-21868.ic7>. Exit status 0 when every reply is the
 * same. */

#include "odin/machine/cabinet/io_837_13741.hpp"
#include "odin/machine/cabinet/jvs.hpp"
#include "odin/machine/cabinet/io_837_13741_imitation.hpp"
#include "odin/machine/chips/mie.hpp"

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <deque>
#include <fstream>
#include <iterator>
#include <string>
#include <utility>
#include <vector>

namespace {

/* Ticks of 1/9.216 GHz, in which both clocks are whole (mie.cpp) */
constexpr uint64_t TICKS_PER_CYCLE = odin::Mie::TICKS_PER_CYCLE;
constexpr uint64_t TICKS_PER_US = 9216;
constexpr uint64_t TICK = 16000;	/* the timer as the program sets it: 1 ms */
/* How far the time of the imitation may be from the measured one, in
 * clocks: half the time, and at most (a tick of the timer just before
 * the end of the request delays the reply, on the board too) */
constexpr int64_t TOLERANCE_HALF = 4;
constexpr int64_t TOLERANCE = 80;

/* What the inputs of the board read: switches released, analog channels */
struct Panel {
	std::array<uint8_t, 8> analog{};
};

struct Pins : odin::MieHost {
	uint8_t gpio_read(uint8_t) override { return 0xFF; }
	void gpio_write(uint8_t, uint8_t) override {}
};

class Board {
public:
	Board(std::span<const uint8_t> rom, odin::JvsInputs &inputs)
		: m_board(rom, inputs), m_mie_rom(odin::Mie::ROM_SIZE), m_mie_ram(odin::Mie::RAM_SIZE),
		  m_mie(m_pins, m_mie_rom, m_mie_ram)
	{
	}

	uint64_t now() const { return m_board.now(); }
	uint16_t pc() { return m_board.chip().cpu().pc(); }
	uint64_t byte_ticks() const { return odin::Io837_13741::CHAR_TICKS; }
	void inc_coin(uint8_t slot) { m_board.inc_coin(slot); }

	/* A byte on the line from the host, its stop bit ending at `at` */
	void arrive(uint64_t at, uint8_t value) { m_wire.emplace_back(at, value); }
	/* The bytes the board sent, with the tick its start bit began */
	std::vector<std::pair<uint64_t, uint8_t>> &sent() { return m_sent; }

	/* Each byte of the host given as its start bit goes, those of the
	 * board taken as they end, in pieces as MieBoard::run_line runs it */
	void run_until(uint64_t until)
	{
		while (now() < until) {
			uint64_t to = std::min(until, now() + 1024 * TICKS_PER_CYCLE);

			if (!m_wire.empty()) {
				const uint64_t start = m_wire.front().first - byte_ticks();

				if (start <= now()) {
					m_board.set_run_start(0);
					m_board.line_sent(m_wire.front().second, start);
					m_wire.pop_front();
					continue;
				}
				to = std::min(to, start);
			}
			m_board.run_until(to);
			while (m_board.next_event() <= now()) {
				const uint64_t at = m_board.next_event();

				m_board.deliver(m_mie, at);
				if (m_mie.in8(0x15) & 1)
					m_sent.emplace_back(at - byte_ticks(), m_mie.in8(0x10));
			}
		}
	}

private:
	odin::Io837_13741 m_board;
	Pins m_pins;
	std::vector<uint8_t> m_mie_rom, m_mie_ram;
	odin::Mie m_mie;
	std::deque<std::pair<uint64_t, uint8_t>> m_wire;
	std::vector<std::pair<uint64_t, uint8_t>> m_sent;
};

/* The imitation reads the same panel */
class Inputs : public odin::JvsInputs {
public:
	explicit Inputs(const Panel &panel) : m_panel(panel) {}
	uint8_t system() override { return 0; }
	uint16_t player(uint8_t) override { return 0; }
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
	Board &board;
	odin::JvsHost &host;
	unsigned same = 0, different = 0;
	/* The time the imitation gives against the time measured, in clocks */
	int64_t worst = 0;
	std::vector<int64_t> errors;

	/* Sends the frame to both; the board gets its bytes back to back.
	 * Returns the ticks from the end of the last byte to the start of
	 * the reply, or 0 when the board stays silent. */
	uint64_t send(const char *what, const std::vector<uint8_t> &bytes, uint64_t wait_us = 20000, bool told = true)
	{
		const uint64_t start = board.now() + 10 * TICKS_PER_US;
		const uint64_t bt = board.byte_ticks();
		uint64_t last = start;

		board.sent().clear();
		for (size_t i = 0; i < bytes.size(); i++) {
			last = start + (i + 1) * bt;
			board.arrive(last, bytes[i]);
		}
		board.run_until(last + wait_us * TICKS_PER_US);

		std::vector<uint8_t> reply;
		for (const auto &[at, value] : board.sent())
			reply.push_back(value);
		const uint64_t delay = board.sent().empty() ? 0 : board.sent().front().first - last;

		for (uint8_t b : bytes)
			host.push(b);
		host.commit_encoded();
		const std::span<const uint8_t> made = host.reply();
		const std::vector<uint8_t> imitation(made.begin(), made.end());

		const bool ok = reply == imitation;
		(ok ? same : different)++;
		if (!reply.empty() && host.reply_cycles()) {
			const int64_t error = static_cast<int64_t>(host.reply_cycles()) -
			                      static_cast<int64_t>(delay / TICKS_PER_CYCLE);
			errors.push_back(error);
			if (std::abs(error) > std::abs(worst))
				worst = error;
		} else if (!reply.empty() || host.reply_cycles()) {
			errors.push_back(1000000);
			worst = 1000000;
		}
		if (told || !ok)
			std::printf("%s %-34s %2zu bytes, the board starts %6.1f us after the request (%.2f characters)\n",
			            ok ? "same     " : "DIFFERENT", what, reply.size(),
			            static_cast<double>(delay) / TICKS_PER_US, static_cast<double>(delay) / static_cast<double>(bt));
		if (!ok)
			std::printf("    board:     %s\n    imitation: %s\n", hex(reply).c_str(), hex(imitation).c_str());
		return delay;
	}
};

} /* namespace */

int main(int argc, char **argv)
{
	if (argc < 2) {
		std::fprintf(stderr, "usage: epr21868_bench <epr-21868.ic7>\n");
		return 2;
	}
	std::ifstream file(argv[1], std::ios::binary);
	std::vector<uint8_t> rom((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
	if (rom.size() != 0x10000) {
		std::fprintf(stderr, "epr-21868 is 64 KB\n");
		return 2;
	}

	Panel panel;
	Inputs inputs(panel);
	Board board(rom, inputs);
	odin::Io837_13741Imitation imitation(inputs);
	odin::JvsHost host;
	host.add_device(&imitation);
	Bench bench{ board, host, 0, 0, 0, {} };

	/* Its start: the test of its memory, then its loop (03A2) */
	board.run_until(1000000ull * TICKS_PER_US);
	std::printf("after 1 s the board runs at %04X\n", board.pc());

	/* As the MIE program starts the line (830F): two resets 1.27 s apart,
	 * the address, the versions and the features, the name. */
	bench.send("F0 D9 to every node", frame(0xFF, { 0xF0, 0xD9 }), 1270000);
	bench.send("F0 D9 to every node", frame(0xFF, { 0xF0, 0xD9 }), 1270000);
	bench.send("F1 01", frame(0xFF, { 0xF1, 0x01 }));
	bench.send("11 12 13 14", frame(0x01, { 0x11, 0x12, 0x13, 0x14 }));
	bench.send("10", frame(0x01, { 0x10 }));

	/* The poll of NASCAR, the analog channels in the middle, then moved */
	panel.analog = { 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80 };
	board.run_until(board.now() + 20 * TICK * TICKS_PER_CYCLE);
	bench.send("poll 20 02 02 21 02 22 08", frame(0x01, { 0x20, 0x02, 0x02, 0x21, 0x02, 0x22, 0x08 }));
	panel.analog = { 0x12, 0xE0, 0xD0, 0x00, 0xFF, 0x7F, 0x01, 0x80 };
	board.run_until(board.now() + 20 * TICK * TICKS_PER_CYCLE);
	bench.send("poll, channels moved", frame(0x01, { 0x20, 0x02, 0x02, 0x21, 0x02, 0x22, 0x08 }));
	bench.send("32 03 12 34 56", frame(0x01, { 0x32, 0x03, 0x12, 0x34, 0x56 }));

	/* A coin through slot 1 */
	board.inc_coin(0);
	board.run_until(board.now() + 2 * odin::Io837_13741::COIN_CYCLES * TICKS_PER_CYCLE);
	imitation.inc_coin(0);
	bench.send("21 02 after a coin", frame(0x01, { 0x21, 0x02 }));
	bench.send("35 01 00 05", frame(0x01, { 0x35, 0x01, 0x00, 0x05 }));
	bench.send("30 01 00 02", frame(0x01, { 0x30, 0x01, 0x00, 0x02 }));
	bench.send("30 02 00 01 (below 0)", frame(0x01, { 0x30, 0x02, 0x00, 0x01 }));
	bench.send("21 02", frame(0x01, { 0x21, 0x02 }));

	/* What the standard and the program leave to the board */
	bench.send("99 (unknown)", frame(0x01, { 0x99 }));
	bench.send("11 13 99 12", frame(0x01, { 0x11, 0x13, 0x99, 0x12 }));
	bench.send("22 09 11 (refused)", frame(0x01, { 0x22, 0x09, 0x11 }));
	bench.send("32 04 11 12 13 99", frame(0x01, { 0x32, 0x04, 0x11, 0x12, 0x13, 0x99 }));
	bench.send("20 00 00 20 00 01 20 01 00", frame(0x01, { 0x20, 0x00, 0x00, 0x20, 0x00, 0x01, 0x20, 0x01, 0x00 }));
	bench.send("10 to node 2", frame(0x02, { 0x10 }));
	std::vector<uint8_t> bad = frame(0x01, { 0x10 });
	bad.back() = static_cast<uint8_t>(bad.back() + 1);
	bench.send("10, checksum wrong", bad);
	bench.send("11 F0 12", frame(0x01, { 0x11, 0xF0, 0x12 }), 1270000);

	/* The time to the reply, the request ending anywhere in a tick of
	 * the timer: 64 phases, 1/64 of a tick apart */
	const std::pair<const char *, std::vector<uint8_t>> timed[] = {
		{ "poll 20 02 02 21 02 22 08", { 0x20, 0x02, 0x02, 0x21, 0x02, 0x22, 0x08 } },
		{ "20 02 02", { 0x20, 0x02, 0x02 } },
		{ "21 02", { 0x21, 0x02 } },
		{ "22 08", { 0x22, 0x08 } },
		{ "20 02 02 21 02", { 0x20, 0x02, 0x02, 0x21, 0x02 } },
		{ "32 03 00 00 00", { 0x32, 0x03, 0x00, 0x00, 0x00 } },
		{ "poll and 32 03 00 00 00", { 0x20, 0x02, 0x02, 0x21, 0x02, 0x22, 0x08, 0x32, 0x03, 0x00, 0x00, 0x00 } },
		{ "11 12 13 14", { 0x11, 0x12, 0x13, 0x14 } },
		{ "10", { 0x10 } },
	};
	bench.send("F1 01", frame(0xFF, { 0xF1, 0x01 }));
	for (const auto &[what, data] : timed) {
		std::vector<double> us;

		for (unsigned phase = 0; phase < 64; phase++) {
			board.run_until(board.now() + TICK * TICKS_PER_CYCLE * (64 + phase) / 64);
			us.push_back(static_cast<double>(bench.send(what, frame(0x01, data), 20000, false)) / TICKS_PER_US);
		}
		std::sort(us.begin(), us.end());
		std::printf("%-30s reply after %.1f us at least, %.1f half the time, %.1f at most\n", what, us.front(),
		            us[us.size() / 2], us.back());
	}

	/* MEASURE=1: the Z80 cycles to the reply of each command alone and
	 * in pairs, with the bytes of the reply, the request ending at 32
	 * phases of a tick: the median. */
	if (std::getenv("MEASURE")) {
		std::vector<std::vector<uint8_t>> frames = {
			{ 0x99 }, { 0x10 }, { 0x11 }, { 0x12 }, { 0x13 }, { 0x14 }, { 0x11, 0x12 }, { 0x11, 0x12, 0x13 },
			{ 0x20, 0x00, 0x00 }, { 0x20, 0x01, 0x01 }, { 0x20, 0x01, 0x02 }, { 0x20, 0x02, 0x01 }, { 0x20, 0x02, 0x02 },
			{ 0x21, 0x01 }, { 0x21, 0x02 }, { 0x21, 0x00 },
			{ 0x22, 0x01 }, { 0x22, 0x02 }, { 0x22, 0x04 }, { 0x22, 0x08 }, { 0x22, 0x09 },
			{ 0x32, 0x01, 0x00 }, { 0x32, 0x02, 0x00, 0x00 }, { 0x32, 0x03, 0x00, 0x00, 0x00 }, { 0x32, 0x04, 0, 0, 0, 0 },
			{ 0x30, 0x01, 0x00, 0x00 }, { 0x35, 0x01, 0x00, 0x00 },
			{ 0x20, 0x02, 0x02, 0x20, 0x02, 0x02 }, { 0x21, 0x02, 0x21, 0x02 }, { 0x22, 0x08, 0x22, 0x08 },
			{ 0x32, 0x03, 0, 0, 0, 0x32, 0x03, 0, 0, 0 }, { 0x22, 0x08, 0x21, 0x02, 0x20, 0x02, 0x02 },
		};
		for (uint8_t p = 0; p <= 3; p++)
			for (uint8_t b = 0; b <= 3; b++)
				frames.push_back({ 0x20, p, b });
		for (uint8_t n = 0; n <= 3; n++)
			frames.push_back({ 0x21, n });
		for (uint8_t n = 0; n <= 9; n++)
			frames.push_back({ 0x22, n });
		frames.push_back({ 0x32, 0x00 });
		frames.push_back({ 0x30, 0x00, 0x00, 0x00 });
		frames.push_back({ 0x30, 0x03, 0x00, 0x00 });
		frames.push_back({ 0x35, 0x00, 0x00, 0x00 });
		frames.push_back({ 0x35, 0x03, 0x00, 0x00 });
		frames.push_back({ 0x30, 0x01, 0x3F, 0xFF });
		frames.push_back({ 0x30, 0x02, 0x3F, 0xFF });
		frames.push_back({ 0x35, 0x01, 0x40, 0x00 });
		frames.push_back({ 0x35, 0x02, 0x40, 0x00 });
		frames.push_back({ 0x32, 0x05, 0, 0, 0, 0, 0 });
		frames.push_back({ 0x11, 0x99 });
		/* A checksum that does not match, and the address taken again
		 * after a reset */
		for (unsigned phase = 0; phase < 4; phase++) {
			std::vector<uint8_t> wrong = frame(0x01, { 0x10 });
			wrong.back() = static_cast<uint8_t>(wrong.back() + 1);
			board.run_until(board.now() + TICK * TICKS_PER_CYCLE * (8 + phase) / 8);
			std::printf("MEASURE checksum 10 | %llu cycles\n",
			            static_cast<unsigned long long>(bench.send("", wrong, 20000, false) / TICKS_PER_CYCLE));
			bench.send("", frame(0xFF, { 0xF0, 0xD9 }), 1270000, false);
			board.run_until(board.now() + TICK * TICKS_PER_CYCLE * phase / 4);
			std::printf("MEASURE address F1 01 | %llu cycles\n", static_cast<unsigned long long>(
			            bench.send("", frame(0xFF, { 0xF1, 0x01 }), 20000, false) / TICKS_PER_CYCLE));
		}
		for (const auto &data : frames) {
			std::vector<uint64_t> times;
			size_t bytes = 0;

			for (unsigned phase = 0; phase < 32; phase++) {
				board.run_until(board.now() + TICK * TICKS_PER_CYCLE * (32 + phase) / 32);
				times.push_back(bench.send(hex(data).c_str(), frame(0x01, data), 20000, false));
				bytes = board.sent().size();
			}
			std::sort(times.begin(), times.end());
			const uint64_t least = times[times.size() / 2];
			/* The reply as the board counts it: no E0, no marks */
			size_t raw = 0;
			for (size_t i = 1; i < board.sent().size(); i++)
				raw += board.sent()[i].second != 0xD0;
			std::printf("MEASURE %s| %llu cycles %zu bytes %zu raw\n", hex(data).c_str(),
			            static_cast<unsigned long long>(least / TICKS_PER_CYCLE), bytes, raw);
		}
	}

	std::printf("%u replies the same, %u different\n", bench.same, bench.different);
	std::sort(bench.errors.begin(), bench.errors.end());
	if (!bench.errors.empty())
		std::printf("the time to the reply the imitation gives, against the board: %+lld to %+lld clocks, "
		            "half the time within %+lld..%+lld, 98 %% within %+lld..%+lld (%zu replies)\n",
		            static_cast<long long>(bench.errors.front()), static_cast<long long>(bench.errors.back()),
		            static_cast<long long>(bench.errors[bench.errors.size() / 4]),
		            static_cast<long long>(bench.errors[bench.errors.size() * 3 / 4]),
		            static_cast<long long>(bench.errors[bench.errors.size() / 100]),
		            static_cast<long long>(bench.errors[bench.errors.size() * 99 / 100]), bench.errors.size());
	const bool on_time = !bench.errors.empty() && std::abs(bench.worst) <= TOLERANCE &&
	                   std::abs(bench.errors[bench.errors.size() / 4]) <= TOLERANCE_HALF &&
	                   std::abs(bench.errors[bench.errors.size() * 3 / 4]) <= TOLERANCE_HALF;
	return bench.different || !on_time ? 1 : 0;
}
