/*
 * SwanStation Xbox (UWP) port - the drive part of src/drives.h (SWANUI_XBOX), S4-services.md section 5.4 / S0 D20.
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Drives are the letters the app can list, found at run time by bridge/storage_xbox.cpp (GetLogicalDrives, then a
 * listing probe; the drives holding DuckStation's DataRoot/AppRoot are left out). There is no PS5 extended storage:
 * src/drives.h defines DRIVE_EXT_COUNT 0 and DRIVE_USB_COUNT 8 on Xbox, so REF main.c's ext-drive start-up code
 * (which created <ext>/SwanStationPS5/games) runs zero iterations and nothing is ever created on a USB drive.
 * Unused slots hold SWANUI_NO_DRIVE, a name Windows rejects, so "%s/%s" roots built from it never exist.
 *
 * Included by src/drives.h (C, DRIVE_COUNT defined: declarations + the drive_* functions REF uses) and by
 * bridge/storage_xbox.cpp (C++, DRIVE_COUNT undefined: declarations only).
 */
#ifndef SWANUI_DRIVES_XBOX_H
#define SWANUI_DRIVES_XBOX_H

#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>

#define SWANUI_DRIVE_SLOTS 8
#define SWANUI_NO_DRIVE "<no drive>"

#ifdef __cplusplus
extern "C" {
#endif
/* SWANUI_DRIVE_SLOTS entries: "E:", "F:"... then SWANUI_NO_DRIVE. Main thread only. Re-probes when the
 * GetLogicalDrives() mask changed since the last call. (bridge/storage_xbox.cpp) */
const char *const *swanui_drive_paths(void);
/* Free megabytes on the drive that holds path, -1 when unknown (compat/swanui_compat.c, existing) */
long long swanui_drive_free_mb(const char *path);
#ifdef __cplusplus
}
#endif

#if defined(DRIVE_COUNT) /* included through src/drives.h */
#ifdef __cplusplus
static_assert(DRIVE_COUNT == SWANUI_DRIVE_SLOTS, "drives.h and swanui_drives_xbox.h disagree");
#else
_Static_assert(DRIVE_COUNT == SWANUI_DRIVE_SLOTS, "drives.h and swanui_drives_xbox.h disagree");
#endif

/* REF uses drive_paths[d] (main.c app_rescan, update.c) and `const char *const *ext_drives = drive_paths;` (main.c);
 * nothing takes sizeof(drive_paths). */
#define drive_paths (swanui_drive_paths())

static inline bool swanui_drive_present(const char *p)
{
    return p[0] != '<';
}

static inline long long drive_free_mb(const char *path)
{
    return swanui_drive_present(path) ? swanui_drive_free_mb(path) : -1;
}

static inline bool drive_usable(int d) /* only REF's ext-drive checks call it: none with DRIVE_EXT_COUNT 0 */
{
    return drive_free_mb(drive_paths[d]) >= 256;
}

/* "E: 12.3 GB, F:" - every readable drive (a full drive still holds games); `only` picks one */
static inline int drives_list(char *buf, size_t size, int only)
{
    int found = 0;
    size_t len = 0;
    if (size)
        buf[0] = '\0';
    for (int d = 0; d < DRIVE_COUNT; ++d)
    {
        const char *name = drive_paths[d];
        if (!swanui_drive_present(name))
            continue;
        if (only >= 0 && found++ != only)
            continue;
        const long long free_mb = swanui_drive_free_mb(name);
        int n = free_mb >= 0
                    ? snprintf(buf + len, size - len, "%s%s %lld.%lld GB", only < 0 && len ? ", " : "", name,
                               free_mb >> 10, (free_mb & 1023) * 10 >> 10)
                    : snprintf(buf + len, size - len, "%s%s", only < 0 && len ? ", " : "", name);
        if (n < 0 || (size_t)n >= size - len)
            break;
        len += (size_t)n;
        if (only >= 0)
            break;
        ++found;
    }
    return found;
}

static inline bool drives_ext_games(char *out, size_t size) /* no extended storage, no disc import */
{
    (void)out;
    (void)size;
    return false;
}
#endif
#endif
