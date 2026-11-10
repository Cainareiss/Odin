/*
 * Odin shell: the single-game kiosk interface the frontend calls. The kiosk is not part of Odin; these are inert stubs
 * (odin_host/odin_stubs.c), so `--kiosk` shows the plain error panel and nothing else.
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#ifndef SWANUI_KIOSK_H
#define SWANUI_KIOSK_H

#include <stdbool.h>
#include "SwanStationPS5.h" /* PadState, SwanStationPS5_MAX_PADS */

#ifdef __cplusplus
extern "C" {
#endif

void kiosk_init(const char *rom);
const char *kiosk_root(void);
const char *kiosk_dir(void);
const char *kiosk_bios_dir(void);
void kiosk_set_error(const char *message);
void kiosk_splash_frame(void);
void kiosk_frame(PadState pads[SwanStationPS5_MAX_PADS]);
void kiosk_map_pads(const PadState in[SwanStationPS5_MAX_PADS], PadState out[SwanStationPS5_MAX_PADS]);
void kiosk_set_focus(bool focus);
bool kiosk_has_focus(void);

#ifdef __cplusplus
}
#endif

#endif
