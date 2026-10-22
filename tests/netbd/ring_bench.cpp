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

/* N communication boards 837-13404 in a ring, each running the 68000
 * program a game uploads (von4 0C2DF4D0, "m3_comm.asm ver. 1.3"), each
 * host played as the library of von4 plays it (0C1B864C, 0C1B868E,
 * 0C1B8A04, 0C1B8CF4): registers, upload, settings 0200-020E, run, then
 * one part a frame once linked. Node 0 is the master.
 *
 * RING_REALTIME=1: each board on its own thread on the clock of the
 * computer (NetBoardRunner, as Odins linked by the network run them), the
 * hosts played at 60 frames a second of that clock; RING_REALTIME=tcp: the
 * same, the frames through NetCable on this computer (ports 13504...).
 *
 * Build: g++ -std=c++20 -O2 -pthread -Wall -Wextra -Wpedantic -Wconversion -Wshadow -I src \
 *            tests/netbd/ring_bench.cpp src/odin/netbd_837_13404.cpp src/odin/netbd_ring.cpp \
 *            src/odin/sockets.cpp src/odin/m68000.cpp src/odin/m68000_ops.cpp -o ring_bench
 * Use:   ring_bench program.bin [nodes [seconds [start delay of node n, in frames]]]
 * Exit status is 0 when every node links and, after it, sees every other
 * part in 9 frames out of 10 at least. */

#include "odin/netbd_837_13404.hpp"
#include "odin/netbd_ring.hpp"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

namespace {

constexpr uint64_t FRAME = odin::NetBoard837_13404::CLOCK_HZ / 60;
constexpr uint64_t SLICE = 100;		/* clocks */
uint64_t g_latency = 100;		/* clocks of a hop, at least SLICE (RING_LATENCY) */
uint64_t g_jitter = 0;			/* up to this many clocks more, drawn for each frame (RING_JITTER) */
uint64_t g_draw = 1;
uint32_t g_part = 0x40;			/* the size of each node's part (RING_PART) */
constexpr uint32_t GAME_ID = 0x56344F46, VERSION = 0x00010000;

/* RING_DARK: no frame goes round until every board's program has started
 * (status1 no longer 7FFF), as a link of Odins would hold it */
bool g_dark = false;
bool g_lit = false;

class Hop : public odin::NetLink {
public:
	odin::NetBoard837_13404 *to = nullptr;
	void send(const odin::NetFrame &frame) override
	{
		if (g_jitter) {
			g_draw = g_draw * 6364136223846793005ull + 1442695040888963407ull;
			if (!g_dark || g_lit)
				to->receive(frame, g_latency + (g_draw >> 33) % (g_jitter + 1));
			return;
		}
		if (!g_dark || g_lit)
			to->receive(frame, g_latency);
	}
};

/* RING_REALTIME: to the thread of the next board, as it comes */
class RealHop : public odin::NetLink {
public:
	odin::NetBoardRunner *to = nullptr;
	void send(const odin::NetFrame &frame) override { to->arrive(frame.data, frame.start, frame.end - frame.start); }
};

struct Node {
	odin::NetBoard837_13404 board;
	Hop hop;
	RealHop real_hop;
	std::unique_ptr<odin::NetBoardRunner> runner;
	std::unique_ptr<odin::NetCable> cable;
	unsigned index = 0;
	uint64_t start = 0;		/* the frame its host starts it */
	bool started = false;
	uint16_t status = 0xFFFF, status0 = 0xFFFF;
	bool verbose = false;
	unsigned trace_key = 0xFFFFFFFF;
	bool linked = false;
	unsigned count = 0, id = 0;
	uint64_t linked_frame = 0;
	unsigned frames_ok = 0;
	std::vector<unsigned> seen;	/* parts seen from each node */
};

uint16_t status1(odin::NetBoard837_13404 &b)
{
	uint16_t v;
	do
		v = static_cast<uint16_t>(b.host_read(0x10114, 2));
	while (v != static_cast<uint16_t>(b.host_read(0x10114, 2)));
	return v;
}

/* The host of von4 starting its board */
void start(Node &n, const std::vector<uint16_t> &program)
{
	odin::NetBoard837_13404 &b = n.board;
	b.host_write(0x10000, 2, 1);
	b.host_write(0x10080, 2, 1);
	b.host_write(0x10180, 2, 0);
	b.host_write(0x00000, 4, 0x01A18104);
	if (b.host_read(0x00000, 4) != 0x01A18104)
		std::printf("node %u: the communication RAM test fails\n", n.index);
	for (size_t i = 0; i < program.size(); i++)
		b.host_write(static_cast<uint32_t>(0x20000 + 4 * i), 4, program[i]);
	b.host_write(0x20000 + 0x200 * 2, 4, n.index == 0 ? 0 : 1);
	b.host_write(0x20000 + 0x204 * 2, 4, (g_part + 0x1F) & ~0x1Fu);
	b.host_write(0x20000 + 0x208 * 2, 4, GAME_ID >> 16);
	b.host_write(0x20000 + 0x20A * 2, 4, GAME_ID & 0xFFFF);
	b.host_write(0x20000 + 0x20C * 2, 4, VERSION >> 16);
	b.host_write(0x20000 + 0x20E * 2, 4, VERSION & 0xFFFF);
	b.host_write(0x10114, 2, 0x7FFF);
	b.host_write(0x10114, 2, 0x7FFF);
	b.host_write(0x10180, 2, 1);
	n.started = true;
}

/* One frame of the host: the link state, then, once linked, its part out
 * and the others in (0C1B8CF4). */
void frame(Node &n, uint64_t f, unsigned nodes)
{
	odin::NetBoard837_13404 &b = n.board;
	const uint16_t s = status1(b);
	const uint16_t s0 = static_cast<uint16_t>(b.host_read(0x10110, 2));
	if (s != n.status || (s0 != n.status0 && n.verbose)) {
		n.status0 = s0;
		std::printf("frame %5llu node %u: status1 %04X  status0 %04X  pc %06X  leds %02X\n",
		            static_cast<unsigned long long>(f), n.index, s, static_cast<unsigned>(b.host_read(0x10110, 2)),
		            b.cpu().pc(), b.leds() & 0xFF);
		n.status = s;
	}
	if (!n.linked && s != 0x7FFF && s != 0 && !(s & 0x8000) && (s >> 8) != 0) {
		n.linked = true;
		n.count = s >> 8;
		n.id = s & 0xFF;
		n.linked_frame = f;
	}
	if (!n.linked)
		return;
	/* my part: who I am and the frame */
	b.host_write(0x100, 4, 0xA5000000u | (n.index << 16) | static_cast<uint32_t>(f & 0xFFFF));
	for (uint32_t k = 0; k < nodes; k++) {
		const uint32_t v = b.host_read(0x100 + g_part + k * g_part, 4);
		if ((v >> 24) == 0xA5) {
			const unsigned from = (v >> 16) & 0xFF;
			if (from < n.seen.size())
				n.seen[from]++;
		}
	}
	n.frames_ok++;
}

} /* namespace */

int main(int argc, char **argv)
{
	if (argc < 2) {
		std::fprintf(stderr, "use: %s program.bin [nodes [seconds [start delay]]]\n", argv[0]);
		return 2;
	}
	std::FILE *fp = std::fopen(argv[1], "rb");
	if (!fp) {
		std::fprintf(stderr, "cannot open %s\n", argv[1]);
		return 2;
	}
	std::vector<uint16_t> program;
	int hi, lo;
	while ((hi = std::fgetc(fp)) != EOF && (lo = std::fgetc(fp)) != EOF)
		program.push_back(static_cast<uint16_t>((hi << 8) | lo));
	std::fclose(fp);
	const unsigned nodes = argc > 2 ? static_cast<unsigned>(std::atoi(argv[2])) : 2;
	const unsigned seconds = argc > 3 ? static_cast<unsigned>(std::atoi(argv[3])) : 10;
	const unsigned delay = argc > 4 ? static_cast<unsigned>(std::atoi(argv[4])) : 0;
	const bool verbose = std::getenv("RING_VERBOSE") != nullptr;
	const bool trace = std::getenv("RING_TRACE") != nullptr;
	g_dark = std::getenv("RING_DARK") != nullptr;
	const char *turns = std::getenv("RING_TURNS");
	const bool counted = !(turns && !std::strcmp(turns, "0"));
	const bool fp_every = std::getenv("RING_FP") != nullptr;
	if (const char *j = std::getenv("RING_JITTER"))
		g_jitter = std::strtoull(j, nullptr, 0);
	if (const char *l = std::getenv("RING_LATENCY"))
		g_latency = std::max<uint64_t>(SLICE, std::strtoull(l, nullptr, 0));
	if (const char *p = std::getenv("RING_PART"))
		g_part = static_cast<uint32_t>(std::strtoul(p, nullptr, 0));

	std::vector<std::unique_ptr<Node>> ring;
	for (unsigned i = 0; i < nodes; i++) {
		ring.push_back(std::make_unique<Node>());
		ring.back()->index = i;
		ring.back()->start = i * delay;
		ring.back()->seen.assign(nodes, 0);
		ring.back()->verbose = verbose;
		ring.back()->board.set_waits_counted(counted);
		ring.back()->board.reset();
	}
	const bool realtime = std::getenv("RING_REALTIME") != nullptr;
	for (unsigned i = 0; i < nodes; i++) {
		ring[i]->hop.to = &ring[(i + 1) % nodes]->board;
		ring[i]->board.set_link(&ring[i]->hop);
		if (realtime)
			ring[i]->runner = std::make_unique<odin::NetBoardRunner>(ring[i]->board);
	}
	const bool tcp = realtime && !std::strcmp(std::getenv("RING_REALTIME"), "tcp");
	if (realtime) {
		for (unsigned i = 0; i < nodes; i++) {
			ring[i]->real_hop.to = ring[(i + 1) % nodes]->runner.get();
			ring[i]->board.set_link(&ring[i]->real_hop);
			if (tcp) {
				ring[i]->cable = std::make_unique<odin::NetCable>(*ring[i]->runner, "ring_bench",
				                                                  static_cast<uint16_t>(13504 + i), "127.0.0.1",
				                                                  static_cast<uint16_t>(13504 + (i + 1) % nodes));
				ring[i]->board.set_link(ring[i]->cable.get());
				if (!ring[i]->cable->start())
					return 2;
			}
		}
		for (auto &n : ring)
			n->runner->start();
	}
	const uint64_t frames = static_cast<uint64_t>(seconds) * 60;
	const auto began = std::chrono::steady_clock::now();
	for (uint64_t f = 0; realtime && f < frames; f++) {
		for (auto &n : ring) {
			const std::lock_guard<std::mutex> hold(n->runner->board_lock());
			if (!n->started && f >= n->start)
				start(*n, program);
			if (n->started)
				frame(*n, f, nodes);
		}
		std::this_thread::sleep_until(began + std::chrono::microseconds((f + 1) * 1000000 / 60));
	}
	for (auto &n : ring)
		if (n->runner)
			n->runner->stop();
	for (auto &n : ring)
		if (n->cable)
			n->cable->stop();
	for (uint64_t f = 0; !realtime && f < frames; f++) {
		bool all = true;
		for (auto &n : ring) {
			if (!n->started && f >= n->start)
				start(*n, program);
			if (n->started)
				frame(*n, f, nodes);
			all = all && n->started && n->status != 0x7FFF;
		}
		if (fp_every)
			for (auto &n : ring)
				std::printf("fp frame %llu node %u %016llX pc %06X\n", static_cast<unsigned long long>(f), n->index,
				            static_cast<unsigned long long>(n->board.fingerprint()), n->board.cpu().pc());
		if (all && !g_lit) {
			g_lit = true;
			if (g_dark)
				std::printf("frame %5llu: every board ready, the ring is lit\n", static_cast<unsigned long long>(f));
		}
		const uint64_t end = (f + 1) * FRAME;
		for (uint64_t t = f * FRAME + SLICE; t <= end; t += SLICE) {
			for (auto &n : ring) {
				n->board.run_until(t);
				if (trace) {
					/* the program's state, LEDs and the first bytes it got */
					auto var = [&](uint32_t a) {
						return n->board.host_read(0x20000 + ((0x7000 + a) & ~1u) * 2, 4) >> ((a & 1) ? 0 : 8) & 0xFF;
					};
					const unsigned key = var(0) << 24 | (n->board.leds() & 0xFF) << 16 | n->board.comm_byte(n->board.bank(), 0xF000) << 8 | n->board.comm_byte(n->board.bank(), 0xF020);
					if (key != n->trace_key) {
						n->trace_key = key;
						const unsigned bk = n->board.bank();
						std::printf("t %8.3f ms node %u: state %u leds %02X count14 %u pc %06X  F000 %02X F020 %02X (bank %u; other F000 %02X F020 %02X)\n",
						            static_cast<double>(t) / 10000.0, n->index, var(0), n->board.leds() & 0xFF,
						            var(0x14) << 8 | var(0x15), n->board.cpu().pc(), n->board.comm_byte(bk, 0xF000),
						            n->board.comm_byte(bk, 0xF020), bk, n->board.comm_byte(bk ^ 1, 0xF000),
						            n->board.comm_byte(bk ^ 1, 0xF020));
					}
				}
			}
		}
	}

	bool ok = true;
	for (auto &n : ring) {
		/* the variables of the program: A6 = 7000 */
		auto var = [&](uint32_t a) { return n->board.host_read(0x20000 + ((0x7000 + a) & ~1u) * 2, 4) >> ((a & 1) ? 0 : 8) & 0xFF; };
		std::printf("node %u: pc %06X sr %04X leds %02X state %u flags 10=%02X 11=%02X 12=%02X count16=%u count14=%u\n",
		            n->index, n->board.cpu().pc(), n->board.cpu().sr(), n->board.leds() & 0xFF, var(0), var(0x10), var(0x11),
		            var(0x12), var(0x16) << 8 | var(0x17), var(0x14) << 8 | var(0x15));
		std::printf("node %u: %s", n->index, n->linked ? "linked" : "NOT LINKED");
		if (n->linked)
			std::printf(" at frame %llu, count %u id %u, %u frames", static_cast<unsigned long long>(n->linked_frame),
			            n->count, n->id, n->frames_ok);
		std::printf("; frames sent %llu received %llu held %llu lost %llu",
		            static_cast<unsigned long long>(n->board.counts().sent),
		            static_cast<unsigned long long>(n->board.counts().received),
		            static_cast<unsigned long long>(n->board.counts().held),
		            static_cast<unsigned long long>(n->board.counts().lost));
		std::printf("; fingerprint %016llX; parts seen:", static_cast<unsigned long long>(n->board.fingerprint()));
		for (unsigned k = 0; k < nodes; k++) {
			std::printf(" %u", n->seen[k]);
			if (k != n->index && n->seen[k] * 10 < n->frames_ok * 9)
				ok = false;
		}
		std::printf("\n");
		ok = ok && n->linked;
	}
	std::printf("%s\n", ok ? "ring OK" : "ring FAILED");
	return ok ? 0 : 1;
}
