/*
 * SwanStation Xbox (UWP) port - <unistd.h> shim: the part of POSIX unistd.h the frontend uses.
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * access() and rmdir() take UTF-8 paths (swanui_compat.c converts them to UTF-16). <io.h> and
 * <direct.h> declare the CRT's own access/rmdir, so they are included before the macros. The macros
 * are for C only, as in swanui_compat.h (C++'s <mdspan> has an access(p, i) member): C++ calls
 * swanui_access_u8 / swanui_rmdir_u8 by name.
 */
#ifndef SWANUI_UNISTD_H
#define SWANUI_UNISTD_H

#include <io.h>
#include <direct.h>

#ifdef __cplusplus
extern "C" {
#endif

#define R_OK 4 /* _waccess: read permission */

int swanui_access_u8(const char *path, int mode);
int swanui_rmdir_u8(const char *path);
#ifndef __cplusplus
#define access(path, mode) swanui_access_u8(path, mode)
#define rmdir(path) swanui_rmdir_u8(path)
#endif

#ifdef __cplusplus
}
#endif

#endif
