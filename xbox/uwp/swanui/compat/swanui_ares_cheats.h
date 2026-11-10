/* ares UWP port - what the frontend's cheat code (src/cheats.c, src/ui/cheat_search.c) asks the host's cheat engine
 * (uwp/ares_host/host_ares_cheats.cpp) besides host_cheat_reset / host_cheat_set (src/core/host.h).
 * Code formats per system: ARES_CHEATS_NOTES.md.  SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef SWANUI_ARES_CHEATS_H
#define SWANUI_ARES_CHEATS_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* True when the engine reads `code` (a .cht cheatN_code: parts joined by '+') for the system id (notes/CONTRACT.md
 * section 1). Only decodes: no game needs to run, any thread. Unsupported codes are left out of the Cheats list. */
bool ares_cheat_supported(const char *system_id, const char *code);

/* PlayStation: the disc's serial ("SLUS-00594") from SYSTEM.CNF of a .cue, .chd or .m3u (its first disc). Reads a few
 * sectors; false when it can't. (The library has no PS1 serials in this port: Game.serial is the stable id.) */
bool ares_cheat_disc_serial(const char *path, char *serial, size_t size);

/* Find a code (cheat_search.c): the running game's main RAM as one block of bytes (0 when the system has none that
 * codes can reach); read only while the game is paused (the in-game menus pause it). */
size_t ares_cheat_ram_size(void);
/* Byte order of two-byte values in that RAM: true for the Mega Drive (68000), false elsewhere. */
bool ares_cheat_ram_big_endian(void);
/* Copies `size` bytes from `offset`; false when the range is outside the RAM or no game runs. */
bool ares_cheat_ram_read(uint8_t *dst, size_t offset, size_t size);
/* The code, in the system's own format, that keeps `bytes` (1 or 2) at `offset` at `value`, and the address as the
 * system sees it ("7E0DBF", "C0A1", "FF1234"...) for the description. False when the system has no format. */
bool ares_cheat_ram_code(size_t offset, int bytes, unsigned value, char *code, size_t code_size, char *where,
                         size_t where_size);

#ifdef __cplusplus
}
#endif
#endif
