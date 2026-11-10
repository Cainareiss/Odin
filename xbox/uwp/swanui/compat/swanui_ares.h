/* ares UWP port - frontend declarations shared by main.c and the settings screen (ARES_UWP). New file, 2026-11-08.
 * SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef SWANUI_ARES_H
#define SWANUI_ARES_H
#ifdef __cplusplus
extern "C" {
#endif

/* The HDR output of the platform layer (notes/CONTRACT.md section 3). src/platform/platform.h, which the platform package
 * owns, declares them too; these are the same prototypes, so a file may see both. */
void plat_set_hdr(int mode, int peak_nits, int paper_white_nits, int game_boost);
int plat_hdr_state(void); /* 0 the display is not HDR capable, 1 capable but SDR now, 2 HDR active */

/* main.c: plat_set_hdr with the console-wide Settings (app.global's hdr_* fields). Called at start, after a settings
 * change and wherever plat_set_colour is called. */
void app_apply_hdr(void);

#ifdef __cplusplus
}
#endif
#endif /* SWANUI_ARES_H */
