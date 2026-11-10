/* Odin (experimental) - the status of a game set from odin_core.dll (odin_host\host_odin.cpp).
 * SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef SWANUI_ODIN_CORE_H
#define SWANUI_ODIN_CORE_H
#include <stddef.h>
#ifdef __cplusplus
extern "C" {
#endif
/* The status of set `set` in the folder `roms_dir` (and the BIOS folder), by the core's odin_check_set.
 * Returns 0 complete, 1 incomplete (note: "incomplete (2 missing in mie)"), -1 when the core cannot say (note: why).
 * note is always NUL-terminated. */
int odin_core_set_status(const char *roms_dir, const char *set, char *note, size_t note_size);
#ifdef __cplusplus
}
#endif
#endif /* SWANUI_ODIN_CORE_H */
