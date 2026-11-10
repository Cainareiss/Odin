/*
 * SwanStation Xbox (UWP) port - implementations behind the compatibility headers:
 * the UTF-8 path wrappers of swanui_compat.h, unistd.h and utime.h, the dirent.h directory
 * reader, setenv, the SDL2 subset of SDL2/SDL.h, and the drive free space of swanui_drives_xbox.h.
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Every Win32 call used here is in the UWP (WINAPI_PARTITION_APP) API set of SDK 10.0.26100.
 *
 * File access goes through the *FromAppW functions of <fileapifromapp.h> (CreateFile2FromAppW,
 * GetFileAttributesExFromAppW, CreateDirectoryFromAppW, DeleteFileFromAppW, RemoveDirectoryFromAppW,
 * MoveFileFromAppW, ReplaceFileFromAppW, FindFirstFileExFromAppW), never the plain wide CRT or Win32
 * calls. Each one first tries the call in the app container, then, if the container's token is
 * refused, asks the file broker, which grants what the app's capabilities allow (broadFileSystemAccess,
 * removableStorage): the USB and external drives E: to N: of swanui_drives_xbox.h are outside the
 * container's own ACLs. Free space has no FromApp variant (see swanui_drive_free_mb).
 */

/* This file implements the wrappers: the force-included narrow-name macros of swanui_compat.h are
 * not wanted here, and must not reach windows.h. Their prototypes stay. */
#undef fopen
#undef fopen_s
#undef remove
#undef rename
#undef mkdir
#undef stat
#undef _stat64
#undef setenv

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <fileapifromapp.h>

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <io.h>
#include <process.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include <wctype.h>
#include <sys/utime.h>

#include "SDL2/SDL.h"
#include "dirent.h"
#include "unistd.h"
#include "utime.h"

/* ---- Errors ------------------------------------------------------------------------------- */

/* free() that keeps errno, so a wrapper returns the error of the call it made */
static void free_keep_errno(void *p)
{
    int saved = errno;
    free(p);
    errno = saved;
}

static int errno_from_win32(DWORD error)
{
    switch (error)
    {
    case ERROR_FILE_NOT_FOUND:
    case ERROR_PATH_NOT_FOUND:
    case ERROR_INVALID_NAME:
    case ERROR_INVALID_DRIVE:
    case ERROR_BAD_NETPATH:
    case ERROR_BAD_PATHNAME:
    case ERROR_NOT_READY: /* a drive with no medium */
        return ENOENT;
    case ERROR_FILENAME_EXCED_RANGE:
        return ENAMETOOLONG;
    case ERROR_WRITE_PROTECT:
        return EROFS;
    case ERROR_ACCESS_DENIED:
    case ERROR_SHARING_VIOLATION:
    case ERROR_LOCK_VIOLATION:
    case ERROR_UNABLE_TO_REMOVE_REPLACED: /* ReplaceFile: the target could not be taken away */
        return EACCES;
    case ERROR_ALREADY_EXISTS:
    case ERROR_FILE_EXISTS:
        return EEXIST;
    case ERROR_NOT_SAME_DEVICE:
        return EXDEV;
    case ERROR_DIRECTORY:
        return ENOTDIR;
    case ERROR_INVALID_PARAMETER:
        return EINVAL;
    case ERROR_DIR_NOT_EMPTY:
        return ENOTEMPTY;
    case ERROR_NOT_ENOUGH_MEMORY:
    case ERROR_OUTOFMEMORY:
        return ENOMEM;
    case ERROR_DISK_FULL:
    case ERROR_HANDLE_DISK_FULL:
        return ENOSPC;
    default:
        return EIO;
    }
}

/* ---- Flamenco One (step 10, 2026-10-31): where the file time goes on the console ----------------------------
 * A *FromAppW call on a path outside the app's own folders is a round trip to the system's file broker, which on
 * the Xbox takes hundreds of milliseconds. The calls of 40 ms or more are logged (the first 40 of them) and a total
 * every 200 calls, so a slow start can be read from duckstation-uwp.log. SwanStationPS5_log writes the frontend's
 * own log file through these same wrappers: the thread-local guard keeps a note from noting itself. */
void SwanStationPS5_log(const char *fmt, ...);
static volatile LONG io_calls, io_slow;
static volatile LONGLONG io_total_us;
static __declspec(thread) bool io_in_note;

static LONGLONG io_now_us(void)
{
    static LONGLONG freq;
    LARGE_INTEGER c;
    if (!freq)
    {
        LARGE_INTEGER f;
        QueryPerformanceFrequency(&f);
        freq = f.QuadPart ? f.QuadPart : 1;
    }
    QueryPerformanceCounter(&c);
    return c.QuadPart * 1000000LL / freq;
}

static volatile LONG io_kind_calls[6]; /* fopen, stat, access, mkdir, remove, opendir */
static wchar_t io_last_path[200];       /* diagnostic only: the most recent path, unsynchronised */

static void io_note(const char *what, const wchar_t *w, LONGLONG t0)
{
    const LONGLONG us = io_now_us() - t0;
    if (io_in_note)
        return;
    io_in_note = true;
    const LONG calls = InterlockedIncrement(&io_calls);
    const LONGLONG total = InterlockedAdd64(&io_total_us, us);
    const int kind = what[0] == 'f' ? 0 : what[0] == 's' ? 1 : what[0] == 'a' ? 2 : what[0] == 'm' ? 3 : what[0] == 'r' ? 4 : 5;
    InterlockedIncrement(&io_kind_calls[kind]);
    if (w)
    {
        wcsncpy(io_last_path, w, 199);
        io_last_path[199] = L'\0';
    }
    if (us >= 40000)
    {
        const LONG slow = InterlockedIncrement(&io_slow);
        if (slow <= 40)
            SwanStationPS5_log("io: %s took %lld ms: %ls", what, us / 1000, w ? w : L"");
    }
    if (calls % 200 == 0)
        SwanStationPS5_log("io: %ld file calls so far (fopen %ld, stat %ld, access %ld, mkdir %ld, remove %ld, opendir %ld), "
                           "%ld slow (40 ms or more), %lld ms in all; last: %s %ls",
                           calls, io_kind_calls[0], io_kind_calls[1], io_kind_calls[2], io_kind_calls[3],
                           io_kind_calls[4], io_kind_calls[5], io_slow, total / 1000, what, io_last_path);
    io_in_note = false;
}

#define IO_BEGIN() const LONGLONG io_t0 = io_now_us()
#define IO_NOTE(what, w) io_note((what), (w), io_t0)

/* ---- UTF-8 to UTF-16, and long paths ------------------------------------------------------ */

/* Win32 refuses a directory path of 248 UTF-16 units or more (CreateDirectory keeps room for an 8.3
 * name) and a file path of MAX_PATH (260) or more, while the frontend allows SwanStationPS5_PATH_MAX
 * (512) bytes. A path that long gets the \\?\ form, which has no such limit. */
#define SHORT_PATH_MAX 248

/* \\?\ or \\.\: already a device path, which Win32 passes on as it is */
static bool device_path(const wchar_t *w)
{
    return w[0] == L'\\' && w[1] == L'\\' && (w[2] == L'?' || w[2] == L'.') && w[3] == L'\\';
}

/* w made absolute with GetFullPathNameW, which normalizes it exactly as Win32 normalizes a short
 * path: '/' becomes '\', "." and ".." are resolved, trailing dots and spaces are dropped, and a
 * reserved name (CON, NUL, ...) becomes its device. Then "C:\x" gets the \\?\ prefix and
 * "\\server\share\x" the \\?\UNC\ one, so a name means the same file whatever the path's length.
 * Malloc'd, with room for `extra` more characters; *len gets its length. NULL with errno set. */
static wchar_t *long_form(const wchar_t *w, size_t extra, size_t *len)
{
    const DWORD size = GetFullPathNameW(w, 0, NULL, NULL); /* with the terminator */
    if (!size)
    {
        errno = errno_from_win32(GetLastError());
        return NULL;
    }
    enum { PREFIX_MAX = 7 }; /* "\\?\UNC", which then reuses the share's own leading '\' */
    wchar_t *out = malloc((PREFIX_MAX + (size_t)size + extra) * sizeof(wchar_t));
    if (!out)
    {
        errno = ENOMEM;
        return NULL;
    }
    wchar_t *full = out + PREFIX_MAX;
    const DWORD n = GetFullPathNameW(w, size, full, NULL); /* without the terminator */
    if (!n || n >= size)
    {
        errno = n ? EIO : errno_from_win32(GetLastError());
        free_keep_errno(out);
        return NULL;
    }
    const wchar_t *prefix = L""; /* a reserved name (\\.\CON) stays its device, as for a short path */
    size_t skip = 0;
    if (!device_path(full))
    {
        if (full[0] == L'\\' && full[1] == L'\\')
            prefix = L"\\\\?\\UNC", skip = 1; /* \\server\share -> \\?\UNC\server\share */
        else if (full[0] && full[1] == L':')
            prefix = L"\\\\?\\";
    }
    const size_t p = wcslen(prefix);
    memmove(out + p, full + skip, ((size_t)n - skip + 1) * sizeof(wchar_t));
    memcpy(out, prefix, p * sizeof(wchar_t));
    *len = p + (size_t)n - skip;
    return out;
}

/* A malloc'd UTF-16 copy of s, with room for `extra` more characters; *len gets its length.
 * A path of SHORT_PATH_MAX units or more (counting the extra ones) comes in its \\?\ form.
 * NULL with errno set when s is not valid UTF-8, the path can't be resolved or memory runs out. */
static wchar_t *widen(const char *s, size_t extra, size_t *len)
{
    int n = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, s, -1, NULL, 0);
    if (n <= 0)
    {
        errno = EILSEQ;
        return NULL;
    }
    wchar_t *w = malloc(((size_t)n + extra) * sizeof(wchar_t));
    if (!w)
    {
        errno = ENOMEM;
        return NULL;
    }
    MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, s, -1, w, n);
    size_t length = (size_t)n - 1;
    /* The frontend joins paths with '/' (path_join, "%s/%s"). Win32 turns '/' into '\' itself only in a path it
     * normalizes: a \\?\ path (a long one below, or a data root given in that form) is passed on as it is, and there
     * '/' would be part of a name. So every '/' becomes '\' here, before the device-path and long-path checks. */
    for (size_t i = 0; i < length; ++i)
        if (w[i] == L'/')
            w[i] = L'\\';
    if (length + extra >= SHORT_PATH_MAX && !device_path(w))
    {
        wchar_t *l = long_form(w, extra, &length);
        free_keep_errno(w);
        if (!l)
            return NULL;
        w = l;
    }
    if (len)
        *len = length;
    return w;
}

/* ---- swanui_compat.h: stdio, sys/stat, environment ----------------------------------------- */

static wchar_t *plain_full_path(const wchar_t *w, size_t *len);

/* fopen on CreateFile2FromAppW: the handle becomes a CRT descriptor (_open_osfhandle), then a FILE
 * (_fdopen), so the frontend's fread/fwrite/fseek/fclose are the CRT's own. The mode is "r", "w" or
 * "a", then any of '+', 'b', 't', C11's 'x' (with 'w': fail if the file exists) and the CRT's hints
 * 'c', 'n', 'N', 'S', 'R'. Binary unless 't' is given, as POSIX has no text mode. The file is shared
 * for reading, writing and deleting: as on POSIX, others may read, write, rename over or remove it
 * while it is open (the CRT's _wfopen does not share delete). NULL with errno set. */
FILE *swanui_fopen_u8(const char *path, const char *mode)
{
    if (!path || !mode)
    {
        errno = EINVAL;
        return NULL;
    }
    DWORD access, disposition;
    int flags = 0;
    switch (mode[0])
    {
    case 'r':
        access = GENERIC_READ, disposition = OPEN_EXISTING, flags = _O_RDONLY;
        break;
    case 'w':
        access = GENERIC_WRITE, disposition = CREATE_ALWAYS;
        break;
    case 'a':
        access = GENERIC_WRITE, disposition = OPEN_ALWAYS, flags = _O_APPEND;
        break;
    default:
        errno = EINVAL;
        return NULL;
    }
    bool plus = false, text = false, binary = false;
    for (const char *m = mode + 1; *m; ++m)
    {
        switch (*m)
        {
        case '+':
            if (plus)
                goto invalid;
            plus = true;
            access = GENERIC_READ | GENERIC_WRITE;
            flags &= ~_O_RDONLY;
            break;
        case 'b':
            binary = true;
            break;
        case 't':
            text = true;
            break;
        case 'x':
            if (mode[0] != 'w')
                goto invalid;
            disposition = CREATE_NEW;
            break;
        case 'c': /* commit, no commit, no inherit, sequential, random: hints */
        case 'n':
        case 'N':
        case 'S':
        case 'R':
            break;
        default:
            goto invalid;
        }
    }
    if (text && binary)
        goto invalid;
    if (text)
        flags |= _O_TEXT;
    char fdmode[4] = {mode[0]};
    size_t k = 1;
    if (plus)
        fdmode[k++] = '+';
    fdmode[k] = text ? 't' : 'b';

    wchar_t *w = widen(path, 0, NULL);
    if (!w)
        return NULL;
    CREATEFILE2_EXTENDED_PARAMETERS params = {sizeof(params)};
    params.dwFileAttributes = FILE_ATTRIBUTE_NORMAL;
    IO_BEGIN();
    HANDLE file = CreateFile2FromAppW(w, access, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, disposition,
                                      &params);
    const DWORD error = GetLastError();
    IO_NOTE("fopen", w);
    free(w);
    if (file == INVALID_HANDLE_VALUE)
    {
        errno = errno_from_win32(error);
        return NULL;
    }
    const int fd = _open_osfhandle((intptr_t)file, flags);
    if (fd == -1)
    {
        const int saved = errno;
        CloseHandle(file);
        errno = saved;
        return NULL;
    }
    FILE *f = _fdopen(fd, fdmode);
    if (!f)
    {
        const int saved = errno;
        _close(fd); /* closes the handle too */
        errno = saved;
    }
    return f;

invalid:
    errno = EINVAL;
    return NULL;
}

/* The file calls so far, all kinds and fopen only (ODIN_UWP: the idle measure in main.c, "io: 25 s report") */
void swanui_io_counts(long *fopen_calls, long *all_calls)
{
    if (fopen_calls)
        *fopen_calls = io_kind_calls[0];
    if (all_calls)
        *all_calls = io_calls;
}

/* The Odin core's io.open (odin_c.h OdinIo): the same CreateFile2FromAppW as swanui_fopen_u8, but the HANDLE goes to the
 * caller, which owns it (the core wraps it in a CRT descriptor). disposition is the CreateFile disposition the caller
 * chose (OPEN_EXISTING, CREATE_ALWAYS, OPEN_ALWAYS). 0 with *out set, or -1 with errno set. */
int swanui_open_handle_u8(const char *path, DWORD access, DWORD disposition, HANDLE *out)
{
    if (!path || !out)
    {
        errno = EINVAL;
        return -1;
    }
    *out = INVALID_HANDLE_VALUE;
    wchar_t *w = widen(path, 0, NULL);
    if (!w)
        return -1;
    CREATEFILE2_EXTENDED_PARAMETERS params = {sizeof(params)};
    params.dwFileAttributes = FILE_ATTRIBUTE_NORMAL;
    IO_BEGIN();
    HANDLE file = CreateFile2FromAppW(w, access, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, disposition,
                                      &params);
    const DWORD error = GetLastError();
    IO_NOTE("fopen", w); /* counted with the fopen calls: the core's open is an fopen */
    free(w);
    if (file == INVALID_HANDLE_VALUE)
    {
        errno = errno_from_win32(error);
        return -1;
    }
    *out = file;
    return 0;
}

/* fopen_s(&f, path, mode), which stb_image's stbi__fopen calls under MSVC: 0, or the errno with *f NULL */
errno_t swanui_fopen_s_u8(FILE **file, const char *path, const char *mode)
{
    if (!file)
    {
        errno = EINVAL;
        return EINVAL;
    }
    *file = swanui_fopen_u8(path, mode);
    return *file ? 0 : errno;
}

int swanui_remove_u8(const char *path)
{
    wchar_t *w = widen(path, 0, NULL);
    if (!w)
        return -1;
    IO_BEGIN();
    const BOOL ok = DeleteFileFromAppW(w);
    const DWORD error = GetLastError();
    IO_NOTE("remove", w);
    free(w);
    if (!ok)
    {
        errno = errno_from_win32(error);
        return -1;
    }
    return 0;
}

/* a and b name the same file: the same full path, ignoring case as Windows file systems do */
static bool same_path(const wchar_t *a, const wchar_t *b)
{
    size_t na = 0, nb = 0;
    wchar_t *fa = plain_full_path(a, &na);
    wchar_t *fb = fa ? plain_full_path(b, &nb) : NULL;
    const bool same = fa && fb && na == nb && na <= INT_MAX &&
                      CompareStringOrdinal(fa, (int)na, fb, (int)nb, TRUE) == CSTR_EQUAL;
    free(fa);
    free(fb);
    return same;
}

/* POSIX rename replaces an existing target. MoveFileFromAppW has no MOVEFILE_REPLACE_EXISTING, so when
 * the target exists the file is put in its place with ReplaceFileFromAppW, which is also one rename on
 * the volume: the target is never missing, as the frontend's "write x.tmp, rename over x" expects.
 * The replaced file's attributes (read-only, hidden) carry over to the new one. As on POSIX, renaming
 * a file to its own name succeeds and does nothing. */
int swanui_rename_u8(const char *from, const char *to)
{
    wchar_t *wfrom = widen(from, 0, NULL);
    if (!wfrom)
        return -1;
    wchar_t *wto = widen(to, 0, NULL);
    if (!wto)
    {
        free_keep_errno(wfrom);
        return -1;
    }
    DWORD error = 0;
    if (!MoveFileFromAppW(wfrom, wto))
    {
        error = GetLastError();
        if (error == ERROR_ALREADY_EXISTS || error == ERROR_FILE_EXISTS)
        {
            if (same_path(wfrom, wto))
                error = 0;
            else if (ReplaceFileFromAppW(wto, wfrom, NULL, REPLACEFILE_IGNORE_MERGE_ERRORS | REPLACEFILE_IGNORE_ACL_ERRORS,
                                         NULL, NULL))
                error = 0;
            else
                error = GetLastError();
        }
    }
    free(wfrom);
    free(wto);
    if (error)
    {
        errno = errno_from_win32(error);
        return -1;
    }
    return 0;
}

/* FILETIME (100 ns units since 1601) to time_t, rounding down */
static __time64_t time_from_filetime(const FILETIME *ft)
{
    const long long v = (long long)(((ULONGLONG)ft->dwHighDateTime << 32) | ft->dwLowDateTime) - 116444736000000000LL;
    return (__time64_t)(v >= 0 ? v / 10000000LL : -((-v + 9999999LL) / 10000000LL));
}

/* stat on GetFileAttributesExFromAppW, filling struct _stat64 as the CRT's _wstat64 does: st_mode is
 * _S_IFDIR or _S_IFREG, read permission always, write unless read-only, execute for a directory and
 * for .exe/.com/.bat/.cmd, copied to group and other; st_nlink 1; st_dev = st_rdev = the drive number
 * (A: = 0; 0 for a UNC path); st_ino, st_uid, st_gid 0; a time the file system does not keep (0)
 * becomes st_mtime. A symbolic link is described itself, not its target (the CRT follows it). */
int swanui_stat_u8(const char *path, struct _stat64 *st)
{
    if (!st)
    {
        errno = EINVAL;
        return -1;
    }
    size_t n;
    wchar_t *w = widen(path, 0, &n);
    if (!w)
        return -1;
    /* POSIX stat takes "dir/"; the CRT only takes a trailing separator on a root ("E:/", "/") */
    while (n > 1 && (w[n - 1] == L'/' || w[n - 1] == L'\\') && !(n == 3 && w[1] == L':'))
        w[--n] = L'\0';
    WIN32_FILE_ATTRIBUTE_DATA a;
    IO_BEGIN();
    const BOOL ok = GetFileAttributesExFromAppW(w, GetFileExInfoStandard, &a);
    const DWORD error = GetLastError();
    IO_NOTE("stat", w);
    if (!ok)
    {
        free(w);
        errno = errno_from_win32(error);
        return -1;
    }
    const wchar_t *p = device_path(w) ? w + 4 : w;
    const int drive = p[0] && p[1] == L':' && iswalpha(p[0]) ? (int)(towupper(p[0]) - L'A') : 0;
    unsigned short mode = _S_IREAD;
    if (!(a.dwFileAttributes & FILE_ATTRIBUTE_READONLY))
        mode |= _S_IWRITE;
    if (a.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
        mode |= _S_IFDIR | _S_IEXEC;
    else
    {
        mode |= _S_IFREG;
        const wchar_t *dot = wcsrchr(w, L'.');
        if (dot && (!_wcsicmp(dot, L".exe") || !_wcsicmp(dot, L".com") || !_wcsicmp(dot, L".bat") ||
                    !_wcsicmp(dot, L".cmd")))
            mode |= _S_IEXEC;
    }
    free(w);
    mode |= (unsigned short)((mode & 0700) >> 3 | (mode & 0700) >> 6);
    memset(st, 0, sizeof(*st));
    st->st_mode = mode;
    st->st_nlink = 1;
    st->st_dev = st->st_rdev = (_dev_t)drive;
    if (!(a.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY))
        st->st_size = (__int64)(((ULONGLONG)a.nFileSizeHigh << 32) | a.nFileSizeLow);
    st->st_mtime = time_from_filetime(&a.ftLastWriteTime);
    st->st_atime = (a.ftLastAccessTime.dwLowDateTime | a.ftLastAccessTime.dwHighDateTime)
                       ? time_from_filetime(&a.ftLastAccessTime)
                       : st->st_mtime;
    st->st_ctime = (a.ftCreationTime.dwLowDateTime | a.ftCreationTime.dwHighDateTime)
                       ? time_from_filetime(&a.ftCreationTime)
                       : st->st_mtime;
    return 0;
}

/* The data root of swanui_set_data_root, as plain_full_path gives it; NULL when none is set */
static wchar_t *volatile data_root;

/* w made absolute and normalized by GetFullPathNameW, without a \\?\ prefix and without trailing
 * separators ("E:\" becomes "E:"). Malloc'd; *len gets its length. NULL with errno set. */
static wchar_t *plain_full_path(const wchar_t *w, size_t *len)
{
    const DWORD size = GetFullPathNameW(w, 0, NULL, NULL);
    if (!size)
    {
        errno = errno_from_win32(GetLastError());
        return NULL;
    }
    wchar_t *full = malloc((size_t)size * sizeof(wchar_t));
    if (!full)
    {
        errno = ENOMEM;
        return NULL;
    }
    size_t n = GetFullPathNameW(w, size, full, NULL);
    if (!n || n >= size)
    {
        errno = n ? EIO : errno_from_win32(GetLastError());
        free_keep_errno(full);
        return NULL;
    }
    if (full[0] == L'\\' && full[1] == L'\\' && full[2] == L'?' && full[3] == L'\\')
    {
        if (n >= 8 && _wcsnicmp(full + 4, L"UNC\\", 4) == 0)
        {
            memmove(full + 2, full + 8, (n - 8 + 1) * sizeof(wchar_t)); /* \\?\UNC\s\x -> \\s\x */
            n -= 6;
        }
        else
        {
            memmove(full, full + 4, (n - 4 + 1) * sizeof(wchar_t)); /* \\?\C:\x -> C:\x */
            n -= 4;
        }
    }
    while (n && full[n - 1] == L'\\')
        full[--n] = L'\0';
    *len = n;
    return full;
}

int swanui_set_data_root(const char *path)
{
    wchar_t *root = NULL;
    if (path && path[0])
    {
        wchar_t *w = widen(path, 0, NULL);
        if (!w)
            return -1;
        size_t n;
        root = plain_full_path(w, &n);
        free_keep_errno(w);
        if (!root)
            return -1;
    }
    /* A replaced root is not freed: another thread may still be comparing with it. The platform
     * layer sets it once, at startup. */
    InterlockedExchangePointer((PVOID volatile *)&data_root, root);
    return 0;
}

/* full (from plain_full_path) is a strict ancestor of the data root: "C:", "C:\Users", ... */
static bool above_data_root(const wchar_t *full, size_t n)
{
    const wchar_t *root = data_root;
    if (!root || n == 0)
        return false;
    const size_t root_len = wcslen(root);
    return n < root_len && root[n] == L'\\' &&
           CompareStringOrdinal(full, (int)n, root, (int)n, TRUE) == CSTR_EQUAL;
}

/* An existing directory, for mkdir's EEXIST. In the UWP sandbox, creating a folder the app container
 * may not open fails with "access denied", and so does looking at it. That is taken as an existing
 * directory only where make_dirs needs one: a drive root ("E:"; a missing drive answers "not found"
 * or "not ready" instead) and a strict ancestor of the data root, which exists because the data root
 * does. Anywhere else the failure stands, as on the PS5. */
static bool directory_there(const wchar_t *w)
{
    wchar_t root[4];
    if (w[0] && w[1] == L':' && !w[2]) /* "E:" is the drive, not its current directory */
    {
        root[0] = w[0], root[1] = L':', root[2] = L'\\', root[3] = L'\0';
        w = root;
    }
    WIN32_FILE_ATTRIBUTE_DATA a;
    if (GetFileAttributesExFromAppW(w, GetFileExInfoStandard, &a))
        return (a.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
    if (GetLastError() != ERROR_ACCESS_DENIED)
        return false;
    size_t n;
    wchar_t *full = plain_full_path(w, &n);
    if (!full)
        return false;
    const bool there = (n == 2 && full[1] == L':') || above_data_root(full, n);
    free(full);
    return there;
}

int swanui_mkdir_u8(const char *path, int mode)
{
    (void)mode;
    wchar_t *w = widen(path, 0, NULL);
    if (!w)
        return -1;
    int r = 0;
    IO_BEGIN();
    const BOOL made = CreateDirectoryFromAppW(w, NULL);
    const DWORD made_error = GetLastError();
    IO_NOTE("mkdir", w);
    if (!made)
    {
        r = -1;
        errno = errno_from_win32(made_error);
        if (errno != EEXIST)
        {
            const int error = errno; /* EACCES and the like: it stands unless the directory is there */
            errno = directory_there(w) ? EEXIST : error;
        }
    }
    free_keep_errno(w);
    return r;
}

int swanui_setenv(const char *name, const char *value, int overwrite)
{
    if (!name || !name[0] || strchr(name, '=') || !value)
    {
        errno = EINVAL;
        return -1;
    }
    if (!overwrite && getenv(name))
        return 0;
    errno_t e = _putenv_s(name, value);
    if (e)
    {
        errno = e;
        return -1;
    }
    return 0;
}

/* ---- unistd.h, utime.h ---------------------------------------------------------------------- */

/* access on GetFileAttributesExFromAppW, as the CRT's _waccess checks: the mode is 0 (exists), 2
 * (write), 4 (read) or 6, anything else is EINVAL; write fails with EACCES on a read-only file. */
int swanui_access_u8(const char *path, int mode)
{
    if (mode & ~6)
    {
        errno = EINVAL;
        return -1;
    }
    wchar_t *w = widen(path, 0, NULL);
    if (!w)
        return -1;
    WIN32_FILE_ATTRIBUTE_DATA a;
    IO_BEGIN();
    const BOOL ok = GetFileAttributesExFromAppW(w, GetFileExInfoStandard, &a);
    const DWORD error = GetLastError();
    IO_NOTE("access", w);
    free(w);
    if (!ok)
    {
        errno = errno_from_win32(error);
        return -1;
    }
    if ((mode & 2) && (a.dwFileAttributes & FILE_ATTRIBUTE_READONLY) && !(a.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY))
    {
        errno = EACCES;
        return -1;
    }
    return 0;
}

int swanui_rmdir_u8(const char *path)
{
    wchar_t *w = widen(path, 0, NULL);
    if (!w)
        return -1;
    const BOOL ok = RemoveDirectoryFromAppW(w);
    const DWORD error = GetLastError();
    free(w);
    if (!ok)
    {
        errno = errno_from_win32(error);
        return -1;
    }
    return 0;
}

/* utime through SetFileTime, not _wutime64: the CRT converts the times through local time and
 * rejects one that falls before 1970 in the local time zone, which cheats.c's {1, 1} ("mark the
 * file old") does west of UTC. A time_t maps exactly to a FILETIME (100 ns units since 1601).
 * FAT and exFAT (USB drives) can't hold a time before 1980: like Linux, the time is then clamped
 * to the oldest one they hold instead of failing. */
static bool filetime_from_time_t(time_t t, FILETIME *out)
{
    if (t < -11644473600LL) /* before 1601 */
        return false;
    const ULONGLONG v = (ULONGLONG)((long long)t * 10000000LL + 116444736000000000LL);
    out->dwLowDateTime = (DWORD)v;
    out->dwHighDateTime = (DWORD)(v >> 32);
    return true;
}

static void clamp_to_fat(FILETIME *ft)
{
    FILETIME oldest; /* 1980-01-02 00:00 UTC: 1980-01-01 local time in every time zone */
    filetime_from_time_t(315619200, &oldest);
    if (CompareFileTime(ft, &oldest) < 0)
        *ft = oldest;
}

int swanui_utime_u8(const char *path, const struct utimbuf *times)
{
    FILETIME access, modified;
    if (!times)
    {
        GetSystemTimeAsFileTime(&modified);
        access = modified;
    }
    else if (!filetime_from_time_t(times->actime, &access) || !filetime_from_time_t(times->modtime, &modified))
    {
        errno = EINVAL;
        return -1;
    }
    wchar_t *w = widen(path, 0, NULL);
    if (!w)
        return -1;
    CREATEFILE2_EXTENDED_PARAMETERS params = {sizeof(params)};
    params.dwFileFlags = FILE_FLAG_BACKUP_SEMANTICS; /* a directory too, as POSIX utime allows */
    HANDLE file = CreateFile2FromAppW(w, FILE_WRITE_ATTRIBUTES, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                      OPEN_EXISTING, &params);
    DWORD error = GetLastError();
    free(w);
    if (file == INVALID_HANDLE_VALUE)
    {
        errno = errno_from_win32(error);
        return -1;
    }
    BOOL ok = SetFileTime(file, NULL, &access, &modified);
    error = GetLastError();
    if (!ok && error == ERROR_INVALID_PARAMETER)
    {
        clamp_to_fat(&access);
        clamp_to_fat(&modified);
        ok = SetFileTime(file, NULL, &access, &modified);
        error = GetLastError();
    }
    CloseHandle(file);
    if (!ok)
    {
        errno = errno_from_win32(error);
        return -1;
    }
    return 0;
}

/* ---- dirent.h ------------------------------------------------------------------------------- */

struct swanui_dir
{
    HANDLE find;   /* INVALID_HANDLE_VALUE: an empty directory (a drive root can be) */
    bool pending;  /* data holds an entry readdir hasn't returned yet */
    WIN32_FIND_DATAW data;
    struct dirent entry;
};

DIR *opendir(const char *path)
{
    size_t n;
    wchar_t *w = widen(path, 2, &n);
    if (!w)
        return NULL;
    if (n == 0)
    {
        free(w);
        errno = ENOENT;
        return NULL;
    }
    if (w[n - 1] != L'/' && w[n - 1] != L'\\')
        w[n++] = L'\\';
    w[n++] = L'*';
    w[n] = L'\0';
    DIR *dir = calloc(1, sizeof(*dir));
    if (!dir)
    {
        free(w);
        errno = ENOMEM;
        return NULL;
    }
    IO_BEGIN();
    dir->find = FindFirstFileExFromAppW(w, FindExInfoBasic, &dir->data, FindExSearchNameMatch, NULL, FIND_FIRST_EX_LARGE_FETCH);
    DWORD error = GetLastError();
    IO_NOTE("opendir", w);
    free(w);
    if (dir->find != INVALID_HANDLE_VALUE)
        dir->pending = true;
    else if (error != ERROR_FILE_NOT_FOUND) /* not found: the folder is there but holds nothing */
    {
        free(dir);
        errno = errno_from_win32(error);
        return NULL;
    }
    return dir;
}

struct dirent *readdir(DIR *dir)
{
    if (!dir)
    {
        errno = EBADF;
        return NULL;
    }
    for (;;)
    {
        if (!dir->pending)
        {
            if (dir->find == INVALID_HANDLE_VALUE || !FindNextFileW(dir->find, &dir->data))
                return NULL; /* the end (POSIX leaves errno alone) */
        }
        dir->pending = false;
        if (!WideCharToMultiByte(CP_UTF8, 0, dir->data.cFileName, -1, dir->entry.d_name,
                                 (int)sizeof(dir->entry.d_name), NULL, NULL))
            continue; /* cannot happen with a 255-unit name; skip rather than return garbage */
        const DWORD attr = dir->data.dwFileAttributes;
        if (attr & FILE_ATTRIBUTE_REPARSE_POINT)
            dir->entry.d_type = DT_UNKNOWN;
        else if (attr & FILE_ATTRIBUTE_DIRECTORY)
            dir->entry.d_type = DT_DIR;
        else if (attr & FILE_ATTRIBUTE_DEVICE)
            dir->entry.d_type = DT_UNKNOWN;
        else
            dir->entry.d_type = DT_REG;
        return &dir->entry;
    }
}

int closedir(DIR *dir)
{
    if (!dir)
    {
        errno = EBADF;
        return -1;
    }
    if (dir->find != INVALID_HANDLE_VALUE)
        FindClose(dir->find);
    free(dir);
    return 0;
}

/* ---- swanui_drives_xbox.h -------------------------------------------------------------------- */

/* GetDiskFreeSpaceExW has no FromApp variant: it runs with the app container's own token. Whether that
 * may query the root of a USB or external drive on the console (with broadFileSystemAccess or
 * removableStorage) is still to be confirmed there; if it may not, every drive reads as missing (-1)
 * and the platform layer must supply the free space another way (StorageFolder's System.FreeSpace).
 * See SWANUI_PORTING_NOTES.md section 9. */
long long swanui_drive_free_mb(const char *path)
{
    size_t n;
    wchar_t *w = widen(path, 1, &n);
    if (!w)
        return -1;
    if (n && w[n - 1] != L'/' && w[n - 1] != L'\\') /* "E:" means E:'s current directory: ask for "E:\" */
        w[n++] = L'\\', w[n] = L'\0';
    ULARGE_INTEGER available;
    BOOL ok = GetDiskFreeSpaceExW(w, &available, NULL, NULL);
    free(w);
    return ok ? (long long)(available.QuadPart >> 20) : -1;
}

/* ---- SDL2/SDL.h: threads -------------------------------------------------------------------- */

struct SDL_Thread
{
    HANDLE handle;
};

typedef struct
{
    SDL_ThreadFunction fn;
    void *data;
} ThreadStart;

static unsigned __stdcall thread_main(void *arg)
{
    ThreadStart start = *(ThreadStart *)arg;
    free(arg);
    return (unsigned)start.fn(start.data);
}

SDL_Thread *SDL_CreateThread(SDL_ThreadFunction fn, const char *name, void *data)
{
    (void)name;
    SDL_Thread *thread = malloc(sizeof(*thread));
    ThreadStart *start = malloc(sizeof(*start));
    if (!thread || !start)
    {
        free(thread);
        free(start);
        return NULL;
    }
    start->fn = fn;
    start->data = data;
    uintptr_t handle = _beginthreadex(NULL, 0, thread_main, start, 0, NULL);
    if (!handle)
    {
        free(start);
        free(thread);
        return NULL;
    }
    thread->handle = (HANDLE)handle;
    return thread;
}

void SDL_WaitThread(SDL_Thread *thread, int *status)
{
    if (!thread)
        return;
    WaitForSingleObjectEx(thread->handle, INFINITE, FALSE);
    if (status)
    {
        DWORD code = 0;
        GetExitCodeThread(thread->handle, &code);
        *status = (int)code;
    }
    CloseHandle(thread->handle);
    free(thread);
}

void SDL_DetachThread(SDL_Thread *thread)
{
    if (!thread)
        return;
    CloseHandle(thread->handle);
    free(thread);
}

/* ---- SDL2/SDL.h: mutexes, condition variables, semaphores ------------------------------------ */

struct SDL_mutex
{
    CRITICAL_SECTION cs; /* recursive, like SDL's mutex */
};

SDL_mutex *SDL_CreateMutex(void)
{
    SDL_mutex *mutex = malloc(sizeof(*mutex));
    if (mutex)
        InitializeCriticalSection(&mutex->cs);
    return mutex;
}

int SDL_LockMutex(SDL_mutex *mutex)
{
    if (mutex)
        EnterCriticalSection(&mutex->cs);
    return 0;
}

int SDL_TryLockMutex(SDL_mutex *mutex)
{
    if (!mutex)
        return 0;
    return TryEnterCriticalSection(&mutex->cs) ? 0 : SDL_MUTEX_TIMEDOUT;
}

int SDL_UnlockMutex(SDL_mutex *mutex)
{
    if (mutex)
        LeaveCriticalSection(&mutex->cs);
    return 0;
}

struct SDL_cond
{
    CONDITION_VARIABLE cv;
};

SDL_cond *SDL_CreateCond(void)
{
    SDL_cond *cond = malloc(sizeof(*cond));
    if (cond)
        InitializeConditionVariable(&cond->cv);
    return cond;
}

int SDL_CondWait(SDL_cond *cond, SDL_mutex *mutex)
{
    if (!cond || !mutex)
        return -1;
    return SleepConditionVariableCS(&cond->cv, &mutex->cs, INFINITE) ? 0 : -1;
}

int SDL_CondSignal(SDL_cond *cond)
{
    if (!cond)
        return -1;
    WakeConditionVariable(&cond->cv);
    return 0;
}

struct SDL_semaphore
{
    HANDLE handle;
};

SDL_sem *SDL_CreateSemaphore(Uint32 initial_value)
{
    SDL_sem *sem = malloc(sizeof(*sem));
    if (!sem)
        return NULL;
    sem->handle = CreateSemaphoreExW(NULL, (LONG)initial_value, 32 * 1024, NULL, 0, SEMAPHORE_ALL_ACCESS);
    if (!sem->handle)
    {
        free(sem);
        return NULL;
    }
    return sem;
}

int SDL_SemWait(SDL_sem *sem)
{
    if (!sem)
        return -1;
    return WaitForSingleObjectEx(sem->handle, INFINITE, FALSE) == WAIT_OBJECT_0 ? 0 : -1;
}

int SDL_SemPost(SDL_sem *sem)
{
    if (!sem)
        return -1;
    return ReleaseSemaphore(sem->handle, 1, NULL) ? 0 : -1;
}

/* ---- SDL2/SDL.h: ticks ---------------------------------------------------------------------- */

Uint64 SDL_GetTicks64(void)
{
    static volatile LONGLONG origin; /* the counter at the first call */
    LARGE_INTEGER now, frequency;
    QueryPerformanceCounter(&now);
    QueryPerformanceFrequency(&frequency);
    LONGLONG start = origin;
    if (!start)
    {
        /* First calls may race: the one whose exchange wins sets the origin, and every caller uses the value the
         * exchange returns (the winner's), never a second read. A loser that read the counter before the winner
         * would be a little before the origin: it gets 0, not a negative time turned into a huge Uint64. */
        const LONGLONG previous = InterlockedCompareExchange64(&origin, now.QuadPart, 0);
        start = previous ? previous : now.QuadPart;
    }
    LONGLONG t = now.QuadPart - start;
    if (t < 0)
        t = 0;
    return (Uint64)(t / frequency.QuadPart * 1000 + t % frequency.QuadPart * 1000 / frequency.QuadPart);
}

Uint32 SDL_GetTicks(void)
{
    return (Uint32)SDL_GetTicks64();
}
