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

/* The settings of the link of cabinets as odin.ini writes them ([network]),
 * then a cable of this computer whose next cabinet is itself (port 13704
 * must be free): both its ends plug in.
 *
 * Build: g++ -std=c++20 -O2 -pthread -Wall -Wextra -Wpedantic -Wconversion -Wshadow -I src \
 *            tests/netbd/net_link_tests.cpp src/odin/netbd_ring.cpp src/odin/netbd_837_13404.cpp \
 *            src/odin/sockets.cpp src/odin/m68000.cpp src/odin/m68000_ops.cpp -o net_link_tests
 * Exit status is 0 when every check passes. */

#include "odin/netbd_ring.hpp"

#include <chrono>
#include <cstdio>
#include <string>
#include <thread>

namespace {

using odin::NetLinkSettings;

int g_checks, g_failures;

void check(bool ok, const char *what)
{
	g_checks++;
	if (!ok) {
		g_failures++;
		std::printf("FAILED: %s\n", what);
	}
}

void test_board()
{
	NetLinkSettings::Board board = NetLinkSettings::Board::NONE;

	check(NetLinkSettings::board_named("ring", board) && board == NetLinkSettings::Board::RING, "ring");
	check(NetLinkSettings::board_named("loop", board) && board == NetLinkSettings::Board::LOOP, "loop");
	check(NetLinkSettings::board_named("none", board) && board == NetLinkSettings::Board::NONE, "none");
	check(!NetLinkSettings::board_named("Ring", board) && !NetLinkSettings::board_named("", board),
	      "nothing else, the case as written");
	for (const auto b : { NetLinkSettings::Board::NONE, NetLinkSettings::Board::LOOP, NetLinkSettings::Board::RING })
		check(NetLinkSettings::board_named(NetLinkSettings::board_name(b), board) && board == b,
		      "each board read back as named");
}

void test_port()
{
	uint16_t port = 1;

	check(NetLinkSettings::port_named("13404", port) && port == 13404, "a port");
	check(NetLinkSettings::port_named("65535", port) && port == 65535, "the last port");
	check(!NetLinkSettings::port_named("0", port) && !NetLinkSettings::port_named("65536", port) &&
	      !NetLinkSettings::port_named("", port) && !NetLinkSettings::port_named("12a", port) &&
	      !NetLinkSettings::port_named("-1", port) && !NetLinkSettings::port_named("134040", port),
	      "nothing that is not a port of 1 to 65535");
}

void test_next()
{
	NetLinkSettings link;
	std::string host;
	uint16_t port = 0;

	link.port = 13405;
	check(link.next_cabinet(host, port) && host == "127.0.0.1" && port == 13405,
	      "no next cabinet: this one again, on its own port");
	link.next = "192.168.1.122";
	check(link.next_cabinet(host, port) && host == "192.168.1.122" && port == odin::NetCable::PORT,
	      "an address alone: the first port of that computer");
	link.next = "arcade.local:13407";
	check(link.next_cabinet(host, port) && host == "arcade.local" && port == 13407, "a name and its port");
	for (const char *wrong : { "10.0.0.5:", "10.0.0.5:x", "10.0.0.5:70000", "10.0.0.5 :13404", "a:b:13404" }) {
		link.next = wrong;
		check(!link.next_cabinet(host, port), wrong);
	}
}

/* A ring of one cabinet through TCP: its cable plugs in at both ends */
void test_cable()
{
	odin::NetBoard837_13404 board;
	odin::NetBoardRunner runner(board);
	odin::NetCable cable(runner, "net_link_tests", 13704, "127.0.0.1", 13704);

	board.reset();
	board.set_link(&cable);
	check(!cable.previous_linked() && !cable.next_linked(), "a cable not started is not plugged in");
	if (!cable.start()) {
		check(false, "port 13704 opened");
		return;
	}
	runner.start();
	for (int i = 0; i < 40 && !(cable.previous_linked() && cable.next_linked()); i++)
		std::this_thread::sleep_for(std::chrono::milliseconds(50));
	check(cable.previous_linked() && cable.next_linked(), "both ends plugged in within 2 s");
	runner.stop();
	cable.stop();
	check(!cable.previous_linked() && !cable.next_linked(), "both unplugged once stopped");
}

} /* namespace */

int main()
{
	test_board();
	test_port();
	test_next();
	test_cable();
	std::printf("%d checks, %d failures\n", g_checks, g_failures);
	return g_failures ? 1 : 0;
}
