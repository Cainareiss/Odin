/*
 * SwanStation Xbox (UWP) port - compatibility prelude, force-included (/FI) into every C/C++ file.
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * The frontend sources are the SwanStationPS5 files, copied byte for byte. They were written for the
 * PS5 (clang, a FreeBSD-like libc): this header gives MSVC (C17 / C++20, WINAPI_FAMILY_APP) the GNU and
 * POSIX names those files use. Only names a compiled file actually uses are here; see
 * SWANUI_PORTING_NOTES.md for the list and for the requested items left out because nothing uses them.
 *
 * Paths: the frontend passes UTF-8 paths with '/' separators. The narrow CRT file functions take
 * ANSI code page paths on Windows, so every narrow path function the frontend calls is routed to a
 * swanui_*_u8 wrapper (swanui_compat.c) that converts UTF-8 to UTF-16 and calls the *FromAppW
 * function of <fileapifromapp.h>, which also reaches folders outside the app container's ACLs (USB
 * and external drives) through the file broker, as the app's capabilities allow.
 * Each CRT header that declares one of those names is included first, so the macros below never
 * rewrite a CRT declaration. Other POSIX path functions live in the POSIX-named shim headers next to
 * this one: access/rmdir in unistd.h, utime in utime.h, opendir in dirent.h.
 *
 * C and C++: the function-like path macros (fopen, fopen_s, remove, rename, mkdir, stat calls,
 * setenv, and access/rmdir/utime in the shim headers) are for C only. In C++ they would rewrite the
 * standard library: std::remove(first, last, value), std::filesystem's remove/rename, std::fopen,
 * mdspan's access(p, i). C++ gets one real overload instead, POSIX's two-argument mkdir(path, mode)
 * (the CRT's mkdir takes one argument, so the two never compete); other C++ code calls the
 * swanui_*_u8 functions by name. The type and plain-name mappings (struct stat, strcasecmp,
 * __attribute__, __builtin_popcount) are the same in both languages, so a struct stat passed between
 * C and C++ has one layout.
 */
#ifndef SWANUI_COMPAT_H
#define SWANUI_COMPAT_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <direct.h>
#include <io.h> /* corecrt_io.h declares remove and rename too */
#include <intrin.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ---- GNU extensions -------------------------------------------------------------------------- */

/* __attribute__((format(printf, 1, 2))), ((format_arg(1))), ((unused)): hints only */
#define __attribute__(x)

#define __builtin_popcount(x) ((int)__popcnt((unsigned int)(x)))

/* ---- POSIX string functions (the PS5 <string.h> exposes them) -------------------------------- */

#define strcasecmp _stricmp
#define strncasecmp _strnicmp

/* ---- POSIX environment ----------------------------------------------------------------------- */

/* setenv(name, value, overwrite) on _putenv_s: 0 on success, -1 with errno set on failure */
int swanui_setenv(const char *name, const char *value, int overwrite);

/* The file calls so far (all kinds, and fopen alone): the idle measure of the shell's log (ODIN_UWP, main.c) */
void swanui_io_counts(long *fopen_calls, long *all_calls);

/* ---- POSIX <sys/stat.h> ---------------------------------------------------------------------- */

#ifndef S_ISDIR
#define S_ISDIR(m) (((m) & _S_IFMT) == _S_IFDIR)
#endif

/* struct stat is struct _stat64 (64-bit st_size and st_mtime) in C and C++ alike: the CRT's own
 * struct stat is _stat64i32, with a 32-bit st_size. */
int swanui_stat_u8(const char *path, struct _stat64 *st);
#define stat _stat64

/* mkdir(path, mode): the mode is ignored, as Windows has no POSIX permission bits. An existing
 * directory gives -1 with errno EEXIST, as on POSIX. So does a drive root ("E:") or an ancestor of
 * the data root (below) that the app container may not even look at; any other path the container
 * may not open keeps EACCES. */
int swanui_mkdir_u8(const char *path, int mode);

/* The app's writable data folder (UTF-8, '/' or '\'), for mkdir above: make_dirs (util.c) calls
 * mkdir on every ancestor of a path, and in the UWP sandbox those above the app's own folders
 * answer "access denied" both to creating and to looking at them. The platform layer calls this once
 * at startup with ApplicationData LocalFolder (the root plat_default_root builds on), before other
 * threads start. NULL clears it. 0 on success, -1 with errno set. */
int swanui_set_data_root(const char *path);

/* ---- C <stdio.h> path functions -------------------------------------------------------------- */

/* fopen: binary unless the mode has 't', as POSIX has no text mode (no CRLF translation, no Ctrl+Z end) */
FILE *swanui_fopen_u8(const char *path, const char *mode);
/* fopen_s(&f, path, mode), the MSVC call stb_image makes: 0, or the errno with f NULL */
errno_t swanui_fopen_s_u8(FILE **file, const char *path, const char *mode);
/* remove: a file */
int swanui_remove_u8(const char *path);
/* rename: replaces an existing target, as POSIX rename does (MSVC's fails with EACCES) */
int swanui_rename_u8(const char *from, const char *to);

/* ---- The C names (see "C and C++" above) ------------------------------------------------------ */

#ifndef __cplusplus
/* "stat" above turns both "struct stat" and "stat(" into _stat64; this one then catches only the calls */
#define _stat64(path, st) swanui_stat_u8(path, st)
#define mkdir(path, mode) swanui_mkdir_u8(path, mode)
#define setenv(name, value, overwrite) swanui_setenv(name, value, overwrite)
#define fopen(path, mode) swanui_fopen_u8(path, mode)
#define fopen_s(file, path, mode) swanui_fopen_s_u8(file, path, mode)
#define remove(path) swanui_remove_u8(path)
#define rename(from, to) swanui_rename_u8(from, to)
#endif

#ifdef __cplusplus
}

/* ---- The C++ name: POSIX mkdir(path, mode) (see "C and C++" above) ---------------------------- */

/* A new overload, not a macro: <direct.h> declares the CRT's one-argument mkdir(path), so a
 * two-argument call can only mean this one, and nothing in the standard library is rewritten.
 * nfs.cpp calls mkdir(dir, 0777) and compiles unchanged. */
extern "C++" inline int mkdir(const char *path, int mode)
{
    return swanui_mkdir_u8(path, mode);
}
#endif

#endif
