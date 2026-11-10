/* Tsugaru (FM Towns, from the SwanStation Xbox port) - the games folder chosen with the Xbox folder picker. New file, SWANUI_XBOX only.
 * On the console, USB drives are not readable by drive letter: the app gets access to a folder through the system's
 * FolderPicker, and that access is kept in the FutureAccessList (bridge/folder_xbox.cpp). The chosen path is stored
 * in <data root>/games-folder.txt and added to the library roots (main.c).
 * SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef SWANUI_XBOX_FOLDER_H
#define SWANUI_XBOX_FOLDER_H
#include <stddef.h>
#ifndef __cplusplus
#include "SwanStationPS5.h" /* Paths (frontend only: the C++ bridge never needs it) */
#endif
#ifdef __cplusplus
extern "C" {
#endif

/* ---- bridge/folder_xbox.cpp (C++/WinRT) ---- */
/* Opens the native folder picker on the UI thread; returns at once. Nothing happens while one is already open. */
void plat_xbox_pick_folder_begin(void);
/* The result of the last picker, once: 1 = a folder was picked (`out` gets its path, '/' separators), -1 = cancelled
 * or failed (`out` empty), 0 = nothing new. Core thread. */
int plat_xbox_pick_folder_poll(char *out, size_t size);
/* Re-asserts the access kept in the FutureAccessList for the stored folder (fire and forget). */
void plat_xbox_folder_access_restore(void);

/* ---- compat/swanui_xbox_folder.c (C) ---- */
/* The stored folder, "" when none; '/' separators. Reads games-folder.txt the first time. */
const char *swanui_xbox_games_root(void);
/* Stores a folder (and writes games-folder.txt). Returns 1 on success. */
int swanui_xbox_games_root_set(const char *path);
/* Once per frame (main loop): applies a folder the picker returned: stores it, tells the user, rescans the shelf. */
void swanui_xbox_folder_poll(void);
/* Points paths->covers at <games folder>/covers when a folder is chosen (the covers and disc art are then kept on the
 * USB drive, next to the games), or back at <data root>/covers. Called by app_rescan before the shelf is rebuilt. */
#ifndef __cplusplus
void swanui_xbox_apply_covers_dir(Paths *paths);
#endif

#ifdef __cplusplus
}
#endif
#endif /* SWANUI_XBOX_FOLDER_H */
