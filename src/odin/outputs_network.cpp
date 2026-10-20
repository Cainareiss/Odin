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

#include "odin/outputs_network.hpp"

#include <cstdio>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <cerrno>
#include <fcntl.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

namespace odin {

namespace {

/* The sockets of the host, the same way on each */
#ifdef _WIN32
using Socket = SOCKET;
const intptr_t NONE = static_cast<intptr_t>(INVALID_SOCKET);

bool
network_up()
{
	static const bool up = [] {
		WSADATA data;

		return WSAStartup(MAKEWORD(2, 2), &data) == 0;
	}();
	return up;
}

void close_socket(intptr_t s) { closesocket(static_cast<Socket>(s)); }

bool
nonblocking(intptr_t s)
{
	u_long on = 1;

	return ioctlsocket(static_cast<Socket>(s), FIONBIO, &on) == 0;
}

bool would_block() { return WSAGetLastError() == WSAEWOULDBLOCK; }

long
send_some(intptr_t s, const char *data, size_t size)
{
	return send(static_cast<Socket>(s), data, static_cast<int>(size), 0);
}

long
receive_some(intptr_t s, char *data, size_t size)
{
	return recv(static_cast<Socket>(s), data, static_cast<int>(size), 0);
}
#else
using Socket = int;
const intptr_t NONE = -1;

bool network_up() { return true; }

void close_socket(intptr_t s) { close(static_cast<Socket>(s)); }

bool
nonblocking(intptr_t s)
{
	const int flags = fcntl(static_cast<Socket>(s), F_GETFL, 0);

	return flags >= 0 && fcntl(static_cast<Socket>(s), F_SETFL, flags | O_NONBLOCK) == 0;
}

bool would_block() { return errno == EAGAIN || errno == EWOULDBLOCK; }

long
send_some(intptr_t s, const char *data, size_t size)
{
#ifdef MSG_NOSIGNAL
	return send(static_cast<Socket>(s), data, size, MSG_NOSIGNAL);
#else
	return send(static_cast<Socket>(s), data, size, 0);
#endif
}

long
receive_some(intptr_t s, char *data, size_t size)
{
	return recv(static_cast<Socket>(s), data, size, 0);
}
#endif

} /* namespace */

std::unique_ptr<OutputsNetwork>
OutputsNetwork::open(const std::string &game, const std::vector<GameOutput> &outputs)
{
	if (!network_up()) {
		std::fprintf(stderr, "ERROR: outputs: no network on this computer\n");
		return nullptr;
	}

	const intptr_t listener = static_cast<intptr_t>(socket(AF_INET, SOCK_STREAM, 0));
	sockaddr_in address{};

	address.sin_family = AF_INET;
	address.sin_port = htons(PORT);
	address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
#ifndef _WIN32
	/* Odin run again at once takes the port back */
	const int on = 1;

	if (listener != NONE)
		setsockopt(static_cast<Socket>(listener), SOL_SOCKET, SO_REUSEADDR, &on, sizeof on);
#endif
	if (listener == NONE ||
	    bind(static_cast<Socket>(listener), reinterpret_cast<const sockaddr *>(&address), sizeof address) != 0 ||
	    listen(static_cast<Socket>(listener), 8) != 0 || !nonblocking(listener)) {
		std::fprintf(stderr, "ERROR: outputs: port %u of 127.0.0.1 cannot be opened\n", PORT);
		if (listener != NONE)
			close_socket(listener);
		return nullptr;
	}
	std::printf("outputs: for other programs on 127.0.0.1:%u\n", PORT);
	return std::unique_ptr<OutputsNetwork>(new OutputsNetwork(listener, game, outputs));
}

OutputsNetwork::OutputsNetwork(intptr_t listener, const std::string &game, const std::vector<GameOutput> &outputs)
	: m_listener(listener), m_game(game), m_outputs(outputs), m_was(outputs.size(), false)
{
}

OutputsNetwork::~OutputsNetwork()
{
	for (Client &client : m_clients)
		client.queued += "mame_stop = 1\r";
	send_all();
	for (const Client &client : m_clients)
		close_socket(client.socket);
	close_socket(m_listener);
}

std::string
OutputsNetwork::changes(const std::vector<GameOutput> &list, std::vector<bool> &was, std::span<const uint8_t> now)
{
	std::string lines;

	was.resize(list.size(), false);
	for (size_t o = 0; o < list.size(); o++) {
		const bool on = list[o].on(now);

		if (on == was[o])
			continue;
		was[o] = on;
		lines += list[o].id + (on ? " = 1\r" : " = 0\r");
	}
	return lines;
}

void
OutputsNetwork::welcome()
{
	accept_all();
	send_all();
}

void
OutputsNetwork::accept_all()
{
	for (;;) {
		const intptr_t client = static_cast<intptr_t>(accept(static_cast<Socket>(m_listener), nullptr, nullptr));
		const int on = 1;

		if (client == NONE)
			break;
		if (!nonblocking(client)) {
			close_socket(client);
			continue;
		}
		setsockopt(static_cast<Socket>(client), IPPROTO_TCP, TCP_NODELAY, reinterpret_cast<const char *>(&on),
		           sizeof on);
#ifdef SO_NOSIGPIPE
		/* Where send has no MSG_NOSIGNAL: a client gone does not stop Odin */
		setsockopt(static_cast<Socket>(client), SOL_SOCKET, SO_NOSIGPIPE, &on, sizeof on);
#endif
		m_clients.push_back({ client, "mame_start = " + m_game + "\r" });
	}
}

void
OutputsNetwork::show(std::span<const uint8_t> outputs)
{
	accept_all();

	const std::string lines = changes(m_outputs, m_was, outputs);

	for (Client &client : m_clients)
		client.queued += lines;
	send_all();
}

/* What each client is owed goes as far as it takes it; what it sends is
 * dropped, a few reads a frame at most; a client gone, failing or too
 * slow is closed. */
void
OutputsNetwork::send_all()
{
	char dropped[256];

	for (size_t c = 0; c < m_clients.size();) {
		Client &client = m_clients[c];
		bool gone = client.queued.size() > MOST_QUEUED;

		while (!gone && !client.queued.empty()) {
			const long sent = send_some(client.socket, client.queued.data(), client.queued.size());

			if (sent <= 0) {
				gone = !would_block();
				break;
			}
			client.queued.erase(0, static_cast<size_t>(sent));
		}
		for (int reads = 0; !gone && reads < MOST_READS; reads++) {
			const long got = receive_some(client.socket, dropped, sizeof dropped);

			if (got > 0)
				continue;
			gone = got == 0 || !would_block();
			break;
		}
		if (!gone) {
			c++;
			continue;
		}
		close_socket(client.socket);
		m_clients.erase(m_clients.begin() + static_cast<std::ptrdiff_t>(c));
	}
}

} /* namespace odin */
