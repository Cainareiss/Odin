/*
 * odin::sockets for the Xbox DLL (M1): the network link is not in this build
 * (the Hikaru board runs with the link board "none", the v0.1 default), so the
 * socket calls report that the link is unavailable. The Winsock file
 * (host/input/sockets.cpp) is not compiled: its ws2_32 imports are not in the
 * app partition. GPL-3.0-or-later, as Odin.
 */

#include "odin/host/input/sockets.hpp"

#include <atomic>

namespace odin::sockets {

bool network_up() { return false; }
void close_socket(intptr_t) {}
bool nonblocking(intptr_t) { return false; }
bool would_block() { return false; }
long send_some(intptr_t, const char *, size_t) { return -1; }
long receive_some(intptr_t, char *, size_t) { return -1; }
intptr_t listen_on(uint16_t, bool) { return NONE; }
intptr_t accept_client(intptr_t) { return NONE; }
intptr_t connect_to(const std::string &, uint16_t, int) { return NONE; }
bool readable(intptr_t, int) { return false; }
unsigned readable(intptr_t, intptr_t, int) { return 0; }
bool send_all(intptr_t, const void *, size_t) { return false; }
bool receive_all(intptr_t, void *, size_t, const std::atomic<bool> &) { return false; }

} /* namespace odin::sockets */
