/*
 * SwanStation Xbox (UWP) port - <dirent.h> shim on FindFirstFileExW / FindNextFileW.
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * opendir takes a UTF-8 path ('/' or '\' separators); readdir gives UTF-8 names, "." and ".."
 * included as on POSIX. Each DIR owns its struct dirent, so separate DIRs can be used on separate
 * threads. Windows.h stays out of the including file: DIR is opaque here.
 */
#ifndef SWANUI_DIRENT_H
#define SWANUI_DIRENT_H

#ifdef __cplusplus
extern "C" {
#endif

#define DT_UNKNOWN 0
#define DT_DIR 4
#define DT_REG 8

/* A Windows file name is at most 255 UTF-16 units, which is at most 765 UTF-8 bytes */
struct dirent
{
    unsigned char d_type; /* DT_DIR, DT_REG or DT_UNKNOWN (devices, reparse points) */
    char d_name[768];
};

typedef struct swanui_dir DIR;

DIR *opendir(const char *path);
struct dirent *readdir(DIR *dir);
int closedir(DIR *dir);

#ifdef __cplusplus
}
#endif

#endif
