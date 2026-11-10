/*
 * SwanStation Xbox - host functions added for the Xbox build (C ABI), included by main.c/play.c under SWANUI_XBOX
 * (port-notes/specs/S3-emulator-host.md section 13.1, owner S3). All are defined in the host bridge.
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#ifndef SWANUI_HOST_XBOX_H
#define SWANUI_HOST_XBOX_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
bool host_set_speed(bool fast, bool back);         /* false when back is requested but rewind is unavailable */
uint32_t host_frames_advanced(void);               /* vblanks DuckStation ran since the previous call */
bool host_save_state_bg(const char *path, bool wait); /* DuckStation state file; wait=false writes on a worker */
bool host_serial_for_path(const char *path, char *serial, size_t size); /* alias of swanstation_retro_ds (S0 D27); no caller */
/* A usable BIOS image exists in the folder DuckStation searches: the frontend's bios folder, or the one shipped in
 * the package (<install folder>\swanui\bios) when that is what the last host_load used. For main.c's post-load
 * "BIOS not found" notice (bios_file_present), which only looks at the frontend's folder. Core thread.
 * ARES_UWP: asked right after a successful host_load only, never before a start: true when the loaded system needs no
 * firmware or its firmware was found (the toast then says the BIOS goes in LocalState/ares/bios). */
bool host_bios_present(void);
#ifdef __cplusplus
}
#endif
#endif
