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

#include "odin/netbd_ring.hpp"

#include "odin/sockets.hpp"

#include <algorithm>
#include <cstdio>
#include <cstring>

namespace odin {

using namespace sockets;

NetBoardRunner::NetBoardRunner(NetBoard837_13404 &board) : m_board(board) {}

NetBoardRunner::~NetBoardRunner()
{
	stop();
}

void
NetBoardRunner::start()
{
	if (m_thread.joinable())
		return;
	{
		const std::lock_guard<std::mutex> hold(m_board_lock);

		m_start = Clock::now();
		m_clock_at_start = m_board.clocks();
	}
	m_stop = false;
	m_thread = std::thread(&NetBoardRunner::run, this);
}

void
NetBoardRunner::stop()
{
	{
		const std::lock_guard<std::mutex> hold(m_inbox_lock);

		m_stop = true;
	}
	m_wake.notify_one();
	if (m_thread.joinable())
		m_thread.join();
}

void
NetBoardRunner::arrive(std::vector<uint8_t> data, uint64_t duration)
{
	{
		const std::lock_guard<std::mutex> hold(m_inbox_lock);

		m_inbox.push_back({ std::move(data), duration, Clock::now() });
	}
	m_wake.notify_one();
}

uint64_t
NetBoardRunner::board_clock(Clock::time_point t) const
{
	return m_clock_at_start + std::chrono::duration_cast<BoardClocks>(t - m_start).count();
}

NetBoardRunner::Clock::time_point
NetBoardRunner::wall_clock(uint64_t clock) const
{
	return m_start + std::chrono::duration_cast<Clock::duration>(BoardClocks(clock - m_clock_at_start));
}

/* The frames that came go in at the time they came, the board runs to the
 * time it is, then sleeps until its next event, a frame, or a tick. */
void
NetBoardRunner::run()
{
	std::vector<Incoming> taken;

	while (!m_stop) {
		uint64_t next, now;
		bool busy;
		{
			const std::lock_guard<std::mutex> hold(m_board_lock);

			for (Incoming &in : taken) {
				NetFrame frame;

				frame.data = std::move(in.data);
				frame.end = std::max(board_clock(in.when), m_board.clocks());
				m_board.receive(frame, in.duration);
			}
			taken.clear();
			m_board.run_until(board_clock(Clock::now()));
			next = m_board.next_event();
			now = m_board.clocks();
			busy = m_board.busy();
		}

		Clock::time_point wake = Clock::now() + (busy ? BUSY_TICK : TICK);

		if (next != UINT64_MAX && next > now)
			wake = std::min(wake, wall_clock(next));

		std::unique_lock<std::mutex> hold(m_inbox_lock);

		m_wake.wait_until(hold, wake, [this] { return !m_inbox.empty() || m_stop; });
		taken.swap(m_inbox);
	}
}

namespace {

/* Each message: its kind and the size of what follows, then that */
struct Header {
	uint32_t kind;
	uint32_t size;
};

constexpr uint32_t HELLO = 0x4F4C4548;	/* "HELO" */
constexpr uint32_t FRAME = 0x4D415246;	/* "FRAM": the clocks it took to leave, then its bytes */

struct Hello {
	char game[32];
	uint32_t version;
};

/* The most frames waiting for the next cabinet: more are lost */
constexpr size_t MOST_WAITING = 256;

} /* namespace */

NetCable::NetCable(NetBoardRunner &runner, const std::string &game, uint16_t port, const std::string &next_host,
                   uint16_t next_port)
	: m_runner(runner), m_game(game), m_port(port), m_next_host(next_host), m_next_port(next_port), m_listener(NONE)
{
}

NetCable::~NetCable()
{
	stop();
}

bool
NetCable::start()
{
	if (!network_up()) {
		std::fprintf(stderr, "ERROR: network board: no network on this computer\n");
		return false;
	}
	m_listener = listen_on(m_port, false);
	if (m_listener == NONE) {
		std::fprintf(stderr, "ERROR: network board: port %u cannot be opened\n", m_port);
		return false;
	}
	std::printf("network board: the previous cabinet comes in on port %u, the next is %s:%u\n", m_port,
	            m_next_host.c_str(), m_next_port);
	m_stop = false;
	m_receiver = std::thread(&NetCable::receive_loop, this);
	m_sender = std::thread(&NetCable::send_loop, this);
	return true;
}

void
NetCable::stop()
{
	m_stop = true;
	m_outbox_ready.notify_all();
	if (m_receiver.joinable())
		m_receiver.join();
	if (m_sender.joinable())
		m_sender.join();
	if (m_listener != NONE) {
		close_socket(m_listener);
		m_listener = NONE;
	}
}

void
NetCable::send(const NetFrame &frame)
{
	if (!m_linked)
		return;

	const std::lock_guard<std::mutex> hold(m_outbox_lock);

	if (m_outbox.size() >= MOST_WAITING)
		return;
	m_outbox.push_back({ frame.data, frame.end - frame.start });
	m_outbox_ready.notify_one();
}

/* What the cabinet before says first: the same game, the same cable */
bool
NetCable::hello_ok(intptr_t from)
{
	Header header{};
	Hello hello{};

	if (!readable(from, 2000) || !receive_all(from, &header, sizeof header, m_stop) || header.kind != HELLO ||
	    header.size != sizeof hello || !receive_all(from, &hello, sizeof hello, m_stop))
		return false;
	hello.game[sizeof hello.game - 1] = 0;
	if (hello.version != VERSION || m_game != hello.game) {
		std::fprintf(stderr, "ERROR: network board: the previous cabinet runs %s (cable %u), this one %s (cable %u)\n",
		             hello.game, hello.version, m_game.c_str(), VERSION);
		return false;
	}
	return true;
}

bool
NetCable::receive_frames(intptr_t from)
{
	Header header{};
	uint32_t duration;
	std::vector<uint8_t> data;

	if (!receive_all(from, &header, sizeof header, m_stop) || header.kind != FRAME ||
	    header.size < sizeof duration || header.size > sizeof duration + 0x10000 ||
	    !receive_all(from, &duration, sizeof duration, m_stop))
		return false;
	data.resize(header.size - sizeof duration);
	if (!receive_all(from, data.data(), data.size(), m_stop))
		return false;
	m_runner.arrive(std::move(data), duration);
	return true;
}

/* One cabinet before at a time; one that comes in takes the place of the
 * one there (it was started again) */
void
NetCable::receive_loop()
{
	intptr_t from = NONE;

	while (!m_stop) {
		const unsigned ready = readable(m_listener, from == NONE ? m_listener : from, 100);

		if (ready & 1) {
			const intptr_t in = accept_client(m_listener);

			if (in == NONE)
				continue;
			if (!hello_ok(in)) {
				close_socket(in);
				continue;
			}
			if (from != NONE)
				close_socket(from);
			from = in;
			m_previous = true;
			std::printf("network board: the previous cabinet is linked\n");
		} else if ((ready & 2) && from != NONE && !receive_frames(from)) {
			close_socket(from);
			from = NONE;
			m_previous = false;
			if (!m_stop)
				std::printf("network board: the previous cabinet is gone\n");
		}
	}
	if (from != NONE)
		close_socket(from);
	m_previous = false;
}

/* The next cabinet, tried every half second until it answers; the frames
 * queued meanwhile are lost */
void
NetCable::send_loop()
{
	intptr_t to = NONE;
	bool said = false;
	std::vector<uint8_t> message;
	std::deque<Outgoing> taken;

	while (!m_stop) {
		if (to == NONE) {
			to = connect_to(m_next_host, m_next_port, 1000);

			Hello hello{};
			const Header header{ HELLO, sizeof hello };

			std::strncpy(hello.game, m_game.c_str(), sizeof hello.game - 1);
			hello.version = VERSION;
			if (to != NONE && (!send_all(to, &header, sizeof header) || !send_all(to, &hello, sizeof hello))) {
				close_socket(to);
				to = NONE;
			}
			if (to == NONE) {
				if (!said)
					std::printf("network board: waiting for the next cabinet at %s:%u\n", m_next_host.c_str(),
					            m_next_port);
				said = true;
				for (int i = 0; i < 5 && !m_stop; i++)
					std::this_thread::sleep_for(std::chrono::milliseconds(100));
				continue;
			}
			{
				const std::lock_guard<std::mutex> hold(m_outbox_lock);

				m_outbox.clear();
			}
			m_linked = true;
			said = false;
			std::printf("network board: linked to the next cabinet at %s:%u\n", m_next_host.c_str(), m_next_port);
		}
		{
			std::unique_lock<std::mutex> hold(m_outbox_lock);

			m_outbox_ready.wait_for(hold, std::chrono::milliseconds(100),
			                        [this] { return !m_outbox.empty() || m_stop; });
			taken.swap(m_outbox);
		}
		for (const Outgoing &out : taken) {
			const uint32_t duration = static_cast<uint32_t>(out.duration);
			const Header header{ FRAME, static_cast<uint32_t>(sizeof duration + out.data.size()) };

			message.resize(sizeof header + sizeof duration + out.data.size());
			std::memcpy(message.data(), &header, sizeof header);
			std::memcpy(message.data() + sizeof header, &duration, sizeof duration);
			std::memcpy(message.data() + sizeof header + sizeof duration, out.data.data(), out.data.size());
			if (!send_all(to, message.data(), message.size())) {
				m_linked = false;
				close_socket(to);
				to = NONE;
				std::printf("network board: the next cabinet is gone\n");
				break;
			}
		}
		taken.clear();
	}
	m_linked = false;
	if (to != NONE)
		close_socket(to);
}

const char *
NetLinkSettings::board_name(Board board)
{
	return board == Board::LOOP ? "loop" : board == Board::RING ? "ring" : "none";
}

bool
NetLinkSettings::board_named(const std::string &text, Board &out)
{
	for (const Board board : { Board::NONE, Board::LOOP, Board::RING })
		if (text == board_name(board)) {
			out = board;
			return true;
		}
	return false;
}

bool
NetLinkSettings::port_named(const std::string &text, uint16_t &out)
{
	unsigned long value = 0;

	if (text.empty() || text.size() > 5)
		return false;
	for (const char c : text) {
		if (c < '0' || c > '9')
			return false;
		value = value * 10 + static_cast<unsigned long>(c - '0');
	}
	if (value == 0 || value > 65535)
		return false;
	out = static_cast<uint16_t>(value);
	return true;
}

bool
NetLinkSettings::next_cabinet(std::string &host, uint16_t &next_port) const
{
	const size_t colon = next.rfind(':');

	host = next.substr(0, colon);
	next_port = next.empty() ? port : NetCable::PORT;
	if (colon != std::string::npos && !port_named(next.substr(colon + 1), next_port))
		return false;
	if (host.find_first_of(" \t:") != std::string::npos)
		return false;
	if (host.empty())
		host = "127.0.0.1";
	return true;
}

} /* namespace odin */
