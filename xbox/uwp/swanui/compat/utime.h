/*
 * SwanStation Xbox (UWP) port - <utime.h> shim (MSVC only has <sys/utime.h>).
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * struct utimbuf {time_t actime, modtime;} comes from <sys/utime.h>. utime() takes a UTF-8 path
 * (swanui_compat.c converts it to UTF-16 and sets the times with CreateFile2FromAppW + SetFileTime);
 * a NULL times pointer means "now". The macro is for C only, as in swanui_compat.h: C++ calls
 * swanui_utime_u8 by name. There is no C++ overload as there is for mkdir: <sys/utime.h> already has
 * a two-argument utime(const char *, struct utimbuf *), so utime(p, &t) with a non-const t would still
 * pick the CRT's narrow one, and utime(p, NULL) would be ambiguous.
 */
#ifndef SWANUI_UTIME_H
#define SWANUI_UTIME_H

#include <sys/utime.h>

#ifdef __cplusplus
extern "C" {
#endif

int swanui_utime_u8(const char *path, const struct utimbuf *times);
#ifndef __cplusplus
#define utime(path, times) swanui_utime_u8(path, times)
#endif

#ifdef __cplusplus
}
#endif

#endif
