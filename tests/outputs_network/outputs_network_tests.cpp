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

/* Unit tests for the outputs given to other programs as MAME gives them:
 * the lines of the changes, then a client of this computer on port 8000
 * (the port must be free).
 *
 * Build: g++ -std=c++20 -O2 -Wall -Wextra -Wpedantic -Wconversion -Wshadow \
 *            -I src tests/outputs_network/outputs_network_tests.cpp src/odin/host/input/outputs_network.cpp \
 *            src/odin/host/input/sockets.cpp -o outputs_network_tests
 * Exit status is 0 when every check passes. */

#include "odin/host/input/outputs_network.hpp"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cstdio>
#include <string>

namespace {

using odin::GameOutput;
using odin::OutputsNetwork;

int g_checks, g_failures;

void check(bool ok, const char *what)
{
	g_checks++;
	if (!ok) {
		g_failures++;
		std::printf("FAILED: %s\n", what);
	}
}

std::vector<GameOutput> brave()
{
	return { { "1P START LAMP", "1p_start_lamp", 1, GameOutput::Role::LAMP, 0 },
	         { "1P NOZZLE VIBRATION", "1p_nozzle_vibration", 2, GameOutput::Role::RUMBLE, 0 },
	         { "LOCK", "lock", 12, GameOutput::Role::LOCK, 0 } };
}

void test_changes()
{
	const std::vector<GameOutput> list = brave();
	std::vector<bool> was;
	const uint8_t none[] = { 0x00, 0x00 }, lamp[] = { 0x80, 0x00 }, both[] = { 0xC0, 0x10 }, short_one[] = { 0xC0 };

	check(OutputsNetwork::changes(list, was, none).empty() && was.size() == 3, "all off from off: no line");
	check(OutputsNetwork::changes(list, was, lamp) == "1p_start_lamp = 1\r", "the lamp lit: its line, a CR at the end");
	check(OutputsNetwork::changes(list, was, lamp).empty(), "nothing changed: no line");
	check(OutputsNetwork::changes(list, was, both) == "1p_nozzle_vibration = 1\rlock = 1\r",
	      "two lit at once, in the order of the list; output 12 is bit 4 of the second byte");
	check(OutputsNetwork::changes(list, was, short_one) == "lock = 0\r", "an output past the bytes given: off");
	check(OutputsNetwork::changes(list, was, none) == "1p_start_lamp = 0\r1p_nozzle_vibration = 0\r",
	      "all off again");
}

/* What the client reads within a second, or until it has that many bytes */
std::string read_some(int client, size_t wanted)
{
	std::string got;
	char buffer[256];

	for (int tries = 0; tries < 100 && got.size() < wanted; tries++) {
		pollfd poll_fd{ client, POLLIN, 0 };

		if (poll(&poll_fd, 1, 10) <= 0)
			continue;
		const ssize_t n = recv(client, buffer, sizeof buffer, 0);

		if (n <= 0)
			break;
		got.append(buffer, static_cast<size_t>(n));
	}
	return got;
}

int connect_here()
{
	const int client = socket(AF_INET, SOCK_STREAM, 0);
	sockaddr_in address{};

	address.sin_family = AF_INET;
	address.sin_port = htons(OutputsNetwork::PORT);
	address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
	if (connect(client, reinterpret_cast<const sockaddr *>(&address), sizeof address) != 0) {
		close(client);
		return -1;
	}
	return client;
}

void test_clients()
{
	const uint8_t none[] = { 0x00, 0x00 }, lamp[] = { 0x80, 0x00 };
	auto server = OutputsNetwork::open("braveff", brave());

	check(server != nullptr, "the port opens (it must be free)");
	if (!server)
		return;
	server->show(lamp);

	const int first = connect_here();

	check(first >= 0, "a client of this computer comes in");
	server->show(lamp);
	check(read_some(first, 21) == "mame_start = braveff\r", "it gets mame_start and the game, no state");
	server->show(none);
	check(read_some(first, 18) == "1p_start_lamp = 0\r", "then each change");

	const int second = connect_here();

	server->show(none);
	server->show(lamp);
	check(read_some(second, 39) == "mame_start = braveff\r1p_start_lamp = 1\r", "a second client gets its own start");
	check(read_some(first, 18) == "1p_start_lamp = 1\r", "and the first one the change too");
	close(second);
	server->show(none);
	server->show(lamp);
	check(read_some(first, 36) == "1p_start_lamp = 0\r1p_start_lamp = 1\r", "a client gone, the other goes on");
	check(server->clients() == 1, "one client left");

	/* The menu open: no frame runs, a client still comes in */
	const int third = connect_here();

	server->welcome();
	check(server->clients() == 2, "while no frame runs, a client comes in");
	check(read_some(third, 21) == "mame_start = braveff\r", "and gets its start at once");
	server->show(none);
	check(read_some(third, 18) == "1p_start_lamp = 0\r" && read_some(first, 18) == "1p_start_lamp = 0\r",
	      "then the changes, as the others");
	close(third);
	check(OutputsNetwork::open("braveff", brave()) == nullptr, "the port taken: no second server");
	server.reset();
	check(read_some(first, 14) == "mame_stop = 1\r", "the end: mame_stop");
	close(first);
}

} /* namespace */

int main()
{
	test_changes();
	test_clients();

	std::printf("%d checks, %d failures\n", g_checks, g_failures);
	return g_failures ? 1 : 0;
}
