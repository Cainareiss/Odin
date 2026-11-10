// Odin (experimental) shell: the frontend's host interface over odin_core.dll (ABI 1, M1 core).
//
// This implements swanui core/host.h, compat/swanui_host_xbox.h, compat/swanui_sysopts.h and bridge/tsugaru_uwp_host.h.
// The core is loaded from the package root (LoadPackagedLibrary), its 18 exports are resolved with GetProcAddress, the
// ABI is checked, and odin_init gets an io table built on the frontend's own brokered file functions (compat/
// swanui_compat.c: the *FromAppW calls; no second brokered layer). host_load asks the core (odin_load) and shows the
// core's own text. In this build the core has no picture (graphics backend M3): a game that loads is unloaded again at
// once, and the frontend shows "loaded, no picture in this build" as its text, so no frame ever runs.
//
// Loader-only policy: this file holds no keys, CRCs, board data, ROM bytes or BIOS bytes. The set names and titles
// come from compat/swanui_odin_sets.inc; the status comes from the core.
// Every decision goes to LocalState\Odin\..\odin-shell.log (LocalState\odin-shell.log), flushed per line.
// SPDX-License-Identifier: GPL-3.0-or-later

#include <dirent.h>
#include <sys/stat.h>
#include <cerrno>
#include <cstdarg>
#include <cstdio>
#include <cstring>

#include <windows.h>

#include <algorithm>
#include <mutex>
#include <string>
#include <vector>

#include "odin_core_api.h"

extern "C" {
#include "core/host.h"
#include "compat/swanui_host_xbox.h"
#include "compat/swanui_sysopts.h"
#include "compat/swanui_systems.h"
#include "tsugaru_uwp_host.h"
#include "uwp_paths.h"
}

// The frontend's brokered file functions (compat/swanui_compat.c). Plain C names; the compat header is not force-included
// into this target, so they are declared here.
extern "C" {
int swanui_stat_u8(const char *path, struct _stat64 *st);
int swanui_mkdir_u8(const char *path, int mode);
int swanui_rename_u8(const char *from, const char *to);
int swanui_remove_u8(const char *path);
int swanui_rmdir_u8(const char *path);
int swanui_open_handle_u8(const char *path, DWORD access, DWORD disposition, HANDLE *out);
}

OdinApi g_odin = {};
bool g_odin_ready = false;

namespace
{

std::mutex g_log_lock;
std::string g_system;        // the system host_set_system named for the next host_load
std::string g_install;       // the package folder (odin_init install_dir)
std::string g_data;          // LocalState\Odin (odin_init data_root)
std::string g_bios;          // LocalState\Odin\bios (the shared sets)
std::string g_saves;         // LocalState\Odin\saves (the kept memories)
std::string g_core_error;    // why the core is not up (shown by host_load)
bool g_core_up = false;      // odin_init answered ODIN_OK
bool g_loaded = false;       // a game is loaded in the core (only between odin_load and odin_unload)
bool g_stopped = false;      // the emulated machine stopped (callbacks.stopped)
std::string g_stop_text;

// M3: the picture path (ABI 2). The hook file LocalState\picture-test.txt, read at boot before the graphics card is
// loaded (its knob must be set first), asks for the GPU harness of odin_core.dll once, after odin_init: frames=N,
// knob=unset|1|2 (DZN_FORCE_FL11_0 for Dozen; PC only), list=<a shaders\*.txt path> or list=- . The core's report is
// in the shell log, then the status.
struct PictureHook
{
    bool present = false;
    uint32_t frames = 3;
    std::string knob = "unset";
    std::string list;
};
bool file_exists(const std::string &path);
PictureHook g_hook;
bool g_icd_up = false;                              // vulkan_dzn.dll is loaded and its entry is in the core
uint32_t g_pic_w = 0, g_pic_h = 0, g_pic_pitch = 0;  // the picture odin_frame last returned
bool g_pic_have = false;                             // a picture was returned for the loaded game
bool g_pic_fresh = false;                            // a picture host_frame has not taken yet
bool g_pic_logged = false;                           // the first picture is in the log

std::string read_text(const std::string &path)
{
    HANDLE h = INVALID_HANDLE_VALUE;
    if (swanui_open_handle_u8(path.c_str(), GENERIC_READ, OPEN_EXISTING, &h) != 0 || h == INVALID_HANDLE_VALUE)
        return std::string();
    std::string text;
    char buffer[1024];
    DWORD got = 0;
    while (ReadFile(h, buffer, sizeof(buffer), &got, nullptr) && got > 0)
        text.append(buffer, got);
    CloseHandle(h);
    return text;
}

uint32_t number_of(const std::string &text)
{
    uint32_t n = 0;
    for (char c : text)
    {
        if (c < '0' || c > '9')
            break;
        n = n * 10 + static_cast<uint32_t>(c - '0');
    }
    return n;
}

PictureHook read_picture_hook(const std::string &path)
{
    PictureHook hook;
    if (!file_exists(path))
        return hook;
    hook.present = true;
    const std::string text = read_text(path);
    size_t at = 0;
    while (at < text.size())
    {
        size_t end = text.find_first_of("\r\n", at);
        if (end == std::string::npos)
            end = text.size();
        const std::string line = text.substr(at, end - at);
        at = end + 1;
        if (line.rfind("frames=", 0) == 0)
            hook.frames = number_of(line.substr(7));
        else if (line.rfind("knob=", 0) == 0)
            hook.knob = line.substr(5);
        else if (line.rfind("list=", 0) == 0)
            hook.list = line.substr(5) == "-" ? std::string() : line.substr(5);
    }
    if (hook.frames == 0)
        hook.frames = 1;
    return hook;
}

// The package's Vulkan ICD (vulkan_dzn.dll, Mesa's Dozen), loaded as the app loads it (LoadPackagedLibrary), negotiated
// at interface version 5 (the probe's), and its vk_icdGetInstanceProcAddr given to the core. The knob is set first, and
// only when the hook asks for one: the app does not set it otherwise.
void load_icd(void)
{
    if (!g_hook.knob.empty() && g_hook.knob != "unset")
    {
        SetEnvironmentVariableA("DZN_FORCE_FL11_0", g_hook.knob.c_str());
        odin_log("odin: DZN_FORCE_FL11_0=%s (from the picture hook, before Dozen loads)", g_hook.knob.c_str());
    }
    HMODULE icd = LoadPackagedLibrary(L"vulkan_dzn.dll", 0);
    if (!icd)
    {
        odin_log("odin: vulkan_dzn.dll did not load (LoadPackagedLibrary error %lu): no graphics card, no picture",
                 GetLastError());
        return;
    }
    auto negotiate = reinterpret_cast<int32_t (*)(uint32_t *)>(
        GetProcAddress(icd, "vk_icdNegotiateLoaderICDInterfaceVersion"));
    auto entry = reinterpret_cast<void *(*)(void *, const char *)>(GetProcAddress(icd, "vk_icdGetInstanceProcAddr"));
    if (!negotiate || !entry)
    {
        odin_log("odin: vulkan_dzn.dll lacks its ICD entries (GetLastError %lu): no picture", GetLastError());
        return;
    }
    uint32_t version = 5;
    const int32_t nego = negotiate(&version);
    odin_log("odin: vulkan_dzn.dll loaded; vk_icdNegotiateLoaderICDInterfaceVersion -> %d, version %u", nego,
             static_cast<unsigned>(version));
    g_odin.set_icd(reinterpret_cast<void *>(entry));
    g_icd_up = true;
}

// The frontend's own path for the picture, on the synthetic game the harness left loaded: host_run_frame runs the
// frames (odin_frame) and host_frame hands out the picture the shell uploads. The pixels are summed here (FNV-1a of
// the rows); the shell's upload itself needs the shell's draw, which the PC run does not show.
void frontend_path_check(void)
{
    g_loaded = true;
    g_pic_have = false;
    g_pic_fresh = false;
    g_pic_logged = false;
    for (uint32_t i = 0; i < g_hook.frames; i++)
        host_run_frame();
    int w = 0, h = 0, fmt = 0;
    size_t pitch = 0;
    bool fresh = false;
    const uint8_t *pixels = static_cast<const uint8_t *>(host_frame(&w, &h, &pitch, &fmt, &fresh));
    uint64_t hash = 0xcbf29ce484222325ull;
    if (pixels)
        for (int y = 0; y < h; y++)
            for (size_t i = 0; i < static_cast<size_t>(w) * 4; i++)
            {
                hash ^= pixels[static_cast<size_t>(y) * pitch + i];
                hash *= 0x100000001b3ull;
            }
    odin_log("picture-test frontend: host_frame %s, %dx%d, pitch %zu, pixel_format %d, fresh %d, fnv1a %llu",
             pixels ? "a picture" : "NULL", w, h, pitch, fmt, fresh ? 1 : 0, static_cast<unsigned long long>(hash));
    const void *again = host_frame(&w, &h, &pitch, &fmt, &fresh);
    odin_log("picture-test frontend: second host_frame %s, fresh %d (the picture is taken once)",
             again ? "the same picture" : "NULL", fresh ? 1 : 0);
    g_loaded = false;
}

// The GPU harness of the hook (once, after odin_init). Its lines are in the core's log; this adds the status.
void picture_test_run(void)
{
    if (!g_icd_up)
    {
        odin_log("picture-test: no graphics card (vulkan_dzn.dll did not load): nothing run");
        return;
    }
    odin_log("picture-test: LocalState\\picture-test.txt: frames %u, knob %s, list %s",
             static_cast<unsigned>(g_hook.frames), g_hook.knob.c_str(), g_hook.list.empty() ? "none" : g_hook.list.c_str());
    char report[8192] = "";
    const int32_t st = g_odin.selftest_picture(g_hook.list.c_str(), g_hook.frames, report, sizeof(report));
    odin_log("picture-test: status %d (%s)", static_cast<int>(st), st == ODIN_OK ? "PASS" : "FAIL");
    if (st == ODIN_OK)
        frontend_path_check();
    // the synthetic game the core kept loaded is unloaded here, whatever happened
    char err[256] = "";
    const int32_t u = g_odin.unload(err, sizeof(err));
    odin_log("picture-test: odin_unload %d%s%s", static_cast<int>(u), err[0] ? ": " : "", err);
}

// UTF-8 path to UTF-16 (the LocalState path holds the profile name: narrow fopen would mangle it).
std::wstring utf8_to_wide(const std::string &s)
{
    const int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), NULL, 0);
    std::wstring w(n > 0 ? (size_t)n : 0, L'\0');
    if (n > 0)
        MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), &w[0], n);
    return w;
}

void log_line(const std::string &line)
{
    std::lock_guard<std::mutex> guard(g_log_lock);
    const std::string path = std::string(uwp_data_root()) + "\\odin-shell.log";
    if (FILE *f = _wfopen(utf8_to_wide(path).c_str(), L"a"))
    {
        fputs(line.c_str(), f);
        fputc('\n', f);
        fflush(f);
        fclose(f);
    }
    uwp_log("%s", line.c_str());
}

char lower_char(char c)
{
    return (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c;
}

bool ieq(const std::string &a, const char *b)
{
    const size_t n = strlen(b);
    if (a.size() != n)
        return false;
    for (size_t i = 0; i < n; ++i)
        if (lower_char(a[i]) != lower_char(b[i]))
            return false;
    return true;
}

bool is_sep(char c)
{
    return c == '/' || c == '\\';
}

// The last path component without trailing separators.
std::string base_name(const std::string &path)
{
    size_t end = path.size();
    while (end > 0 && is_sep(path[end - 1]))
        --end;
    size_t start = end;
    while (start > 0 && !is_sep(path[start - 1]))
        --start;
    return path.substr(start, end - start);
}

// The folder a path lies in ("" when it has no separator).
std::string dir_of(const std::string &path)
{
    size_t end = path.size();
    while (end > 0 && is_sep(path[end - 1]))
        --end;
    size_t sep = end;
    while (sep > 0 && !is_sep(path[sep - 1]))
        --sep;
    if (sep == 0)
        return "";
    std::string dir = path.substr(0, sep - 1);
    return dir.empty() ? std::string(1, path[0]) : dir;
}

std::string join(const std::string &dir, const char *name)
{
    if (dir.empty())
        return name;
    return is_sep(dir.back()) ? dir + name : dir + "/" + name;
}

// The set name of a file or folder name: without .zip / .7z. False when empty.
bool stem_of(const std::string &name, std::string &stem)
{
    stem = name;
    const size_t n = stem.size();
    auto ends = [&](const char *ext) {
        const size_t m = strlen(ext);
        if (n <= m)
            return false;
        for (size_t i = 0; i < m; ++i)
            if (lower_char(stem[n - m + i]) != ext[i])
                return false;
        return true;
    };
    if (ends(".zip"))
        stem.resize(n - 4);
    else if (ends(".7z"))
        stem.resize(n - 3);
    return !stem.empty();
}

bool file_exists(const std::string &path)
{
    struct _stat64 st = {};
    return swanui_stat_u8(path.c_str(), &st) == 0;
}

void set_error(char *error, size_t error_size, const std::string &text)
{
    if (error && error_size)
        snprintf(error, error_size, "%s", text.c_str());
}

// The shared sets a report names ("...; x in mie.zip; ..."), in order, without duplicates: "mie, airtrix".
std::string missing_sets(const std::string &report)
{
    std::vector<std::string> sets;
    size_t pos = 0;
    while (pos < report.size())
    {
        size_t end = report.find("; ", pos);
        if (end == std::string::npos)
            end = report.size();
        const std::string piece = report.substr(pos, end - pos);
        const size_t in = piece.rfind(" in ");
        if (in != std::string::npos)
        {
            std::string set = piece.substr(in + 4);
            if (set.size() > 4 && set.compare(set.size() - 4, 4, ".zip") == 0)
                set.resize(set.size() - 4);
            if (!set.empty() && std::find(sets.begin(), sets.end(), set) == sets.end())
                sets.push_back(set);
        }
        pos = end + 2;
    }
    std::string text;
    for (const std::string &s : sets)
        text += (text.empty() ? "" : ", ") + s;
    return text;
}

// ---- the io table (odin_c.h OdinIo) over the frontend's brokered functions ------------------------------------------
// open: the HANDLE of CreateFile2FromAppW (swanui_open_handle_u8); the core owns it from then on.
// stat: swanui_stat_u8 (GetFileAttributesExFromAppW). An absent path is success with flags 0.
// list: opendir/readdir (FindFirstFileExFromAppW), one entry callback per file and folder.
// mkdir, rename_replace, remove: CreateDirectoryFromAppW, ReplaceFileFromAppW or MoveFileFromAppW, DeleteFileFromAppW
// and RemoveDirectoryFromAppW, through the same compat functions the frontend uses.

int io_open(void *ctx, const char *utf8_path, uint32_t access, void **handle)
{
    (void)ctx;
    if (!utf8_path || !handle)
        return -1;
    DWORD acc = 0, disp = 0;
    if (access & ODIN_IO_READ)
        acc = GENERIC_READ, disp = OPEN_EXISTING;
    else if (access & ODIN_IO_WRITE)
        acc = GENERIC_WRITE, disp = CREATE_ALWAYS;
    else if (access & ODIN_IO_APPEND)
        acc = GENERIC_WRITE, disp = OPEN_ALWAYS;
    else
        return -1;
    HANDLE h = INVALID_HANDLE_VALUE;
    if (swanui_open_handle_u8(utf8_path, acc, disp, &h) != 0)
        return -1;
    if (access & ODIN_IO_APPEND)
    {
        LARGE_INTEGER zero = {};
        SetFilePointerEx(h, zero, NULL, FILE_END);
    }
    *handle = (void *)h;
    return 0;
}

int io_stat(void *ctx, const char *utf8_path, uint32_t *flags, uint64_t *size)
{
    (void)ctx;
    if (!utf8_path || !flags || !size)
        return -1;
    *flags = 0;
    *size = 0;
    struct _stat64 st = {};
    if (swanui_stat_u8(utf8_path, &st) != 0)
        return errno == ENOENT ? 0 : -1; // absent: success with no flags
    const bool dir = (st.st_mode & _S_IFDIR) != 0;
    *flags = ODIN_IO_EXISTS | (dir ? ODIN_IO_DIR : 0u);
    *size = dir ? 0 : (uint64_t)st.st_size;
    return 0;
}

int io_list(void *ctx, const char *utf8_dir, void *sink, void (*entry)(void *sink, const char *name, uint32_t is_dir, uint64_t size))
{
    (void)ctx;
    if (!utf8_dir || !entry)
        return -1;
    DIR *d = opendir(utf8_dir);
    if (!d)
        return -1;
    while (struct dirent *e = readdir(d))
    {
        if (strcmp(e->d_name, ".") == 0 || strcmp(e->d_name, "..") == 0)
            continue;
        bool dir = e->d_type == DT_DIR;
        uint64_t size = 0;
        if (!dir)
        {
            struct _stat64 st = {};
            if (swanui_stat_u8(join(utf8_dir, e->d_name).c_str(), &st) == 0)
            {
                dir = (st.st_mode & _S_IFDIR) != 0;
                size = dir ? 0 : (uint64_t)st.st_size;
            }
        }
        entry(sink, e->d_name, dir ? 1u : 0u, size);
    }
    closedir(d);
    return 0;
}

int io_mkdir(void *ctx, const char *utf8_path)
{
    (void)ctx;
    if (!utf8_path)
        return -1;
    if (swanui_mkdir_u8(utf8_path, 0) == 0)
        return 0;
    if (errno == EEXIST)
    {
        struct _stat64 st = {};
        if (swanui_stat_u8(utf8_path, &st) == 0 && (st.st_mode & _S_IFDIR))
            return 0; // the folder is there: that is what the core wanted
    }
    return -1;
}

int io_rename_replace(void *ctx, const char *utf8_from, const char *utf8_to)
{
    (void)ctx;
    return (utf8_from && utf8_to && swanui_rename_u8(utf8_from, utf8_to) == 0) ? 0 : -1;
}

int io_remove(void *ctx, const char *utf8_path)
{
    (void)ctx;
    if (!utf8_path)
        return -1;
    if (swanui_remove_u8(utf8_path) == 0)
        return 0;
    return swanui_rmdir_u8(utf8_path) == 0 ? 0 : -1; // a file, or an empty folder
}

const OdinIo g_io = {ODIN_IO_ABI, nullptr, io_open, io_stat, io_list, io_mkdir, io_rename_replace, io_remove};

// ---- the callbacks ----------------------------------------------------------------------------------------------------

void core_log(void *ctx, int32_t level, const char *text)
{
    (void)ctx;
    odin_log("core %s: %s", level == ODIN_LOG_ERROR ? "error" : level == ODIN_LOG_WARN ? "warn" : "info",
             text ? text : "");
}

void core_stopped(void *ctx, const char *text)
{
    (void)ctx;
    g_stopped = true;
    g_stop_text = text ? text : "";
    odin_log("odin stopped: %s", g_stop_text.c_str());
}

const char *status_name(int32_t status)
{
    switch (status)
    {
    case ODIN_OK: return "ok";
    case ODIN_E_ABI: return "ABI mismatch";
    case ODIN_E_ARG: return "bad argument";
    case ODIN_E_NOT_INIT: return "not initialised";
    case ODIN_E_UNKNOWN_GAME: return "not an Odin game";
    case ODIN_E_MISSING: return "set incomplete";
    case ODIN_E_BIOS_MISSING: return "BIOS missing";
    case ODIN_E_BAD_SET: return "bad set";
    case ODIN_E_BOARD: return "board refused the game";
    case ODIN_E_LOADED: return "a game is loaded";
    case ODIN_E_NOT_LOADED: return "no game loaded";
    case ODIN_E_IO: return "I/O error";
    case ODIN_E_STOPPED: return "stopped earlier";
    case ODIN_E_FAILED: return "failed";
    case ODIN_E_INTERNAL: return "internal error";
    case ODIN_E_OPTION: return "bad option";
    case ODIN_E_UNSUPPORTED: return "not in this build";
    default: return "unknown status";
    }
}

// Calls odin_init with the shell's callbacks. The DLL must be resolved first.
bool core_init(std::string &error)
{
    OdinInit init;
    memset(&init, 0, sizeof(init));
    init.abi = ODIN_ABI;
    init.install_dir = g_install.c_str();
    init.data_root = g_data.c_str();
    init.callbacks.ctx = nullptr;
    init.callbacks.log = core_log;
    init.callbacks.stopped = core_stopped;
    init.callbacks.progress = nullptr; // declared by the ABI, not called yet
    init.callbacks.audio_push = nullptr; // odin_frame is never called in this build (no picture, no game runs)
    init.callbacks.io = &g_io;
    char err[512] = "";
    const int32_t n = g_odin.init(&init, err, sizeof(err));
    if (n != ODIN_OK)
    {
        error = std::string("odin_init ") + status_name(n) + (err[0] ? ": " : "") + err;
        return false;
    }
    return true;
}

// Resolves the 18 exports of odin_c.h by name. False (and the first missing name logged) when one is absent.
bool resolve_exports(HMODULE m)
{
    struct Slot
    {
        const char *name;
        FARPROC *slot;
    };
    FARPROC abi = nullptr, init = nullptr, shutdown = nullptr, count = nullptr, get = nullptr, find = nullptr,
            check = nullptr, load = nullptr, unload = nullptr, reset = nullptr, pads = nullptr, frame = nullptr,
            flush = nullptr, save = nullptr, loadst = nullptr, optget = nullptr, optset = nullptr, selftest = nullptr, picpix = nullptr, seticd = nullptr, picture = nullptr;
    const Slot slots[] = {
        {"odin_abi", &abi},           {"odin_init", &init},         {"odin_shutdown", &shutdown},
        {"odin_catalog_count", &count}, {"odin_catalog_get", &get}, {"odin_catalog_find", &find},
        {"odin_check_set", &check},   {"odin_load", &load},         {"odin_unload", &unload},
        {"odin_reset", &reset},       {"odin_set_pads", &pads},     {"odin_frame", &frame},
        {"odin_flush_saves", &flush}, {"odin_save_state", &save},   {"odin_load_state", &loadst},
        {"odin_option_get", &optget}, {"odin_option_set", &optset}, {"odin_selftest", &selftest}, {"odin_picture_pixels", &picpix}, {"odin_set_vulkan_icd", &seticd},
        {"odin_selftest_picture", &picture},
    };
    for (const Slot &s : slots)
    {
        *s.slot = GetProcAddress(m, s.name);
        if (!*s.slot)
        {
            odin_log("odin: export %s is missing from odin_core.dll (GetLastError %lu)", s.name, GetLastError());
            return false;
        }
    }
    g_odin.module = m;
    g_odin.abi = reinterpret_cast<int32_t (*)(void)>(abi);
    g_odin.init = reinterpret_cast<int32_t (*)(const OdinInit *, char *, size_t)>(init);
    g_odin.shutdown = reinterpret_cast<void (*)(void)>(shutdown);
    g_odin.catalog_count = reinterpret_cast<int32_t (*)(void)>(count);
    g_odin.catalog_get = reinterpret_cast<int32_t (*)(int32_t, OdinCatalogEntry *)>(get);
    g_odin.catalog_find = reinterpret_cast<int32_t (*)(const char *)>(find);
    g_odin.check_set = reinterpret_cast<int32_t (*)(const char *, const char *, const char *, OdinSetStatus *, char *,
                                                    size_t)>(check);
    g_odin.load = reinterpret_cast<int32_t (*)(const char *, const char *, const char *, const char *, char *, size_t)>(load);
    g_odin.unload = reinterpret_cast<int32_t (*)(char *, size_t)>(unload);
    g_odin.reset = reinterpret_cast<int32_t (*)(char *, size_t)>(reset);
    g_odin.set_pads = reinterpret_cast<void (*)(const OdinPad *, uint32_t)>(pads);
    g_odin.frame = reinterpret_cast<int32_t (*)(uint32_t *, uint32_t *, uint32_t *, uint32_t *)>(frame);
    g_odin.flush_saves = reinterpret_cast<int32_t (*)(char *, size_t)>(flush);
    g_odin.save_state = reinterpret_cast<int32_t (*)(const char *, char *, size_t)>(save);
    g_odin.load_state = reinterpret_cast<int32_t (*)(const char *, char *, size_t)>(loadst);
    g_odin.option_get = reinterpret_cast<int32_t (*)(const char *, char *, size_t)>(optget);
    g_odin.option_set = reinterpret_cast<int32_t (*)(const char *, const char *)>(optset);
    g_odin.selftest = reinterpret_cast<int32_t (*)(const char *, char *, size_t)>(selftest);
    g_odin.picture_pixels = reinterpret_cast<const uint8_t *(*)(void)>(picpix);
    g_odin.set_icd = reinterpret_cast<void (*)(void *)>(seticd);
    g_odin.selftest_picture = reinterpret_cast<int32_t (*)(const char *, uint32_t, char *, size_t)>(picture);
    return true;
}

// Compares the core's catalogue with the shelf table (compat/swanui_odin_sets.inc): names and counts.
void check_catalogue(void)
{
    const int32_t count = g_odin.catalog_count();
    int32_t games = 0, hikaru = 0, gaelco = 0, support = 0;
    for (int32_t i = 0; i < count; ++i)
    {
        OdinCatalogEntry e;
        memset(&e, 0, sizeof(e));
        if (g_odin.catalog_get(i, &e) != ODIN_OK)
            continue;
        if (e.is_support)
        {
            ++support;
            continue;
        }
        ++games;
        if (strcmp(e.machine, "hikaru") == 0)
            ++hikaru;
        else if (strcmp(e.machine, "gaelco") == 0)
            ++gaelco;
    }
    int32_t shelf_missing = 0;
    const int rows = odin_set_count();
    for (int i = 0; i < rows; ++i)
    {
        const OdinSetRow *row = odin_set_row(i);
        if (row && g_odin.catalog_find(row->name) < 0)
            ++shelf_missing;
    }
    odin_log("odin: catalogue: core %d entries (%d games: %d hikaru, %d gaelco; %d support); shelf table %d rows, "
             "%d not in the core",
             (int)count, (int)games, (int)hikaru, (int)gaelco, (int)support, rows, (int)shelf_missing);
}

bool make_dir(const std::string &path)
{
    if (swanui_mkdir_u8(path.c_str(), 0) == 0 || errno == EEXIST)
        return true;
    odin_log("odin: could not create %s (%s)", path.c_str(), strerror(errno));
    return false;
}

} // namespace

// ---- called by the app (uwp_app.cpp) ---------------------------------------------------------------------------------

extern "C" void odin_host_boot(void)
{
    g_install = uwp_install_root();
    g_data = std::string(uwp_data_root()) + "\\Odin";
    g_bios = g_data + "\\bios";
    g_saves = g_data + "\\saves";
    make_dir(g_data);
    make_dir(g_bios);
    make_dir(g_saves);
    odin_log("odin: boot, data %s, bios %s, saves %s", g_data.c_str(), g_bios.c_str(), g_saves.c_str());

    HMODULE m = LoadPackagedLibrary(L"odin_core.dll", 0);
    if (!m)
    {
        g_core_error = "odin_core.dll did not load (LoadPackagedLibrary error " + std::to_string(GetLastError()) + ")";
        odin_log("odin: %s", g_core_error.c_str());
        return;
    }
    if (!resolve_exports(m))
    {
        g_core_error = "odin_core.dll lacks an export of odin_c.h";
        odin_log("odin: %s", g_core_error.c_str());
        return;
    }
    const int32_t abi = g_odin.abi();
    if (abi != ODIN_ABI)
    {
        g_core_error = "odin_core.dll ABI " + std::to_string(abi) + ", the shell needs " + std::to_string(ODIN_ABI);
        odin_log("odin: %s", g_core_error.c_str());
        return;
    }
    g_odin_ready = true;
    odin_log("odin: odin_core.dll loaded from the package, ABI %d", (int)abi);
    g_hook = read_picture_hook(std::string(uwp_data_root()) + "\\picture-test.txt");
    load_icd();

    // The in-app selftest (PC evidence): LocalState\selftest.txt present. Runs before the real init, because the
    // driver's checks end with odin_shutdown.
    if (file_exists(std::string(uwp_data_root()) + "\\selftest.txt"))
    {
        odin_log("selftest: LocalState\\selftest.txt found, running the in-app selftest");
        const int fails = odin_app_selftest(g_data.c_str());
        odin_log("selftest: done, %d FAIL line(s)", fails);
    }

    std::string error;
    if (!core_init(error))
    {
        g_core_error = error;
        odin_log("odin: %s", error.c_str());
        return;
    }
    g_core_up = true;
    odin_log("odin: odin_init ok (io table: frontend brokered functions; log to odin-shell.log)");
    if (g_hook.present)
        picture_test_run();
    check_catalogue();
}

extern "C" void odin_host_shutdown(void)
{
    if (g_core_up)
    {
        if (g_loaded)
        {
            char err[512] = "";
            g_odin.unload(err, sizeof(err));
            g_loaded = false;
        }
        g_odin.shutdown();
        g_core_up = false;
    }
    odin_log("odin: shutdown");
}

// ---- the library (library.c): the status of a set, from the core's odin_check_set ------------------------------------

extern "C" int odin_core_set_status(const char *roms_dir, const char *set, char *note, size_t note_size)
{
    if (!note || !note_size)
        return -1;
    note[0] = '\0';
    if (!set || !*set)
    {
        snprintf(note, note_size, "no set name");
        return -1;
    }
    if (!g_core_up)
    {
        snprintf(note, note_size, "core not loaded (see odin-shell.log)");
        return -1;
    }
    OdinSetStatus st;
    memset(&st, 0, sizeof(st));
    char report[4096] = "";
    const int32_t n = g_odin.check_set(roms_dir ? roms_dir : "", g_bios.c_str(), set, &st, report, sizeof(report));
    if (n != ODIN_OK)
    {
        snprintf(note, note_size, "check failed (%s)", status_name(n));
        return -1;
    }
    if (st.state == 0)
    {
        snprintf(note, note_size, "complete");
        return 0;
    }
    const std::string sets = missing_sets(report);
    snprintf(note, note_size, "incomplete (%u missing%s%s)", (unsigned)st.missing, sets.empty() ? "" : " in ",
             sets.c_str());
    return 1;
}

extern "C" {

// ---- core/host.h ----------------------------------------------------------------------------------------------------

void host_set_system(const char *system_id)
{
    g_system = system_id ? system_id : "";
}

bool host_load(const char *game_path, const char *serial, const Paths *paths, const Settings *settings, char *error,
               size_t error_size)
{
    (void)serial;
    (void)paths;
    (void)settings;
    const std::string path = game_path ? game_path : "";
    if (path.empty())
    {
        set_error(error, error_size, "No game given.");
        odin_log("host_load: no game given");
        return false;
    }
    std::string set;
    if (!stem_of(base_name(path), set))
    {
        set_error(error, error_size, "This is not an Odin set.");
        odin_log("host_load: %s is not an Odin set", path.c_str());
        return false;
    }
    const OdinSetRow *row = odin_set_find(set.c_str());
    if (!row)
    {
        set_error(error, error_size, "This is not an Odin game set: " + set);
        odin_log("host_load: %s is not an Odin game set", set.c_str());
        return false;
    }
    if (!g_core_up)
    {
        const std::string text = "Odin core is not loaded: " + g_core_error;
        set_error(error, error_size, text);
        odin_log("host_load: %s (%s): %s", row->name, row->system, text.c_str());
        return false;
    }
    if (g_stopped)
    {
        const std::string text = "Odin stopped: " + g_stop_text + " (close and reopen the app)";
        set_error(error, error_size, text);
        odin_log("host_load: %s (%s): %s", row->name, row->system, text.c_str());
        return false;
    }
    if (g_loaded)
    {
        char err[512] = "";
        g_odin.unload(err, sizeof(err));
        g_loaded = false;
    }
    // The folder the game sits in holds the game and, by the core's rules, the shared sets (the BIOS folder is
    // searched too). Same rules as the library's status (odin_core_set_status).
    const std::string roms = dir_of(path);
    char err[512] = "";
    const int32_t st = g_odin.load(row->name, roms.c_str(), g_bios.c_str(), g_saves.c_str(), err, sizeof(err));
    if (st == ODIN_OK)
    {
        // M3: the game stays loaded: host_run_frame runs its frames and host_frame takes its picture
        g_loaded = true;
        g_pic_have = false;
        g_pic_fresh = false;
        g_pic_logged = false;
        set_error(error, error_size, std::string());
        odin_log("host_load: %s (%s, %s): odin_load ok; kept loaded (picture: %s)", row->name, row->system, row->title,
                 g_icd_up ? "Vulkan, the package's ICD" : "no graphics card");
        return true;
    }
    const std::string text = err[0] ? std::string(err) : std::string(status_name(st));
    set_error(error, error_size, text);
    odin_log("host_load: %s (%s, %s): core status %d (%s): %s", row->name, row->system, row->title, st,
             status_name(st), text.c_str());
    return false;
}

void host_unload(void)
{
    if (g_loaded && g_core_up)
    {
        char err[512] = "";
        const int32_t n = g_odin.unload(err, sizeof(err));
        g_loaded = false;
        odin_log("host_unload: odin_unload %d%s%s", n, err[0] ? ": " : "", err);
        return;
    }
    odin_log("host_unload: no game loaded");
}

bool host_loaded(void)
{
    return g_loaded;
}

const char *host_core_name(void)
{
    return g_core_up ? "Odin core 1 (odin_core.dll, no picture yet)" : "none (odin_core.dll not loaded)";
}

const char *host_emulator_for(const Settings *settings, const char *serial)
{
    (void)settings;
    (void)serial;
    return "Odin (experimental)";
}

void host_set_pads(const PadState pads[SwanStationPS5_MAX_PADS])
{
    // Inert: the core has no picture and no game runs in this build, so there is nothing for the pad snapshot to drive
    // (odin_set_pads takes OdinPad, and the mapping from PadState is not written yet).
    (void)pads;
}

void host_set_gun(int device)
{
    (void)device;
}

void host_set_region_override(const char *region)
{
    (void)region;
}

void host_set_initial_disc(int index)
{
    (void)index;
}

void host_set_special(int device)
{
    (void)device;
}

bool host_analog_mode(int port)
{
    (void)port;
    return false;
}

void host_apply_devices(const Settings *settings)
{
    (void)settings;
}

void host_set_fixes(unsigned flags, const GameInfo *info)
{
    (void)flags;
    (void)info;
}

float host_rumble_level(int port)
{
    (void)port;
    return 0.0f;
}

void host_run_frame(void)
{
    // M3: one frame of the loaded game (odin_frame). The board runs it; the picture drawn behind the call is the one
    // the call returns (the latest drawn), and host_frame takes it.
    if (!g_loaded || !g_core_up || g_stopped)
        return;
    static int32_t last_status = 0;
    uint32_t w = 0, h = 0, pitch = 0, fresh = 0;
    const int32_t st = g_odin.frame(&w, &h, &pitch, &fresh);
    if (st == ODIN_FRAME_PICTURE)
    {
        g_pic_w = w;
        g_pic_h = h;
        g_pic_pitch = pitch;
        g_pic_have = true;
        if (fresh)
            g_pic_fresh = true;
        if (!g_pic_logged)
        {
            g_pic_logged = true;
            odin_log("host_run_frame: first picture %ux%u, pitch %u", static_cast<unsigned>(w),
                     static_cast<unsigned>(h), static_cast<unsigned>(pitch));
        }
    }
    else if (st < 0 && st != last_status)
    {
        odin_log("host_run_frame: odin_frame status %d (%s)", static_cast<int>(st), status_name(st));
    }
    last_status = st < 0 ? st : 0;
}

void host_set_speculative(bool on)
{
    (void)on;
}

uint32_t host_frames_new(void)
{
    return 0;
}

uint32_t host_frames_repeated(void)
{
    return 0;
}

void host_reset(void)
{
}

void host_apply_settings(const Settings *settings)
{
    (void)settings;
}

double host_fps(void)
{
    return 60.0;
}

int host_sample_rate(void)
{
    return 48000;
}

float host_aspect(void)
{
    return 4.0f / 3.0f;
}

const void *host_frame(int *width, int *height, size_t *pitch, int *pixel_format, bool *fresh)
{
    // M3: the picture of the last frame odin_frame returned, B8G8R8A8 (libretro's XRGB8888, pixel_format 1), the top
    // line first, fresh once for each new picture. NULL while there is none: the frontend keeps its text.
    const bool have = g_loaded && g_core_up && g_pic_have;
    if (width)
        *width = have ? static_cast<int>(g_pic_w) : 0;
    if (height)
        *height = have ? static_cast<int>(g_pic_h) : 0;
    if (pitch)
        *pitch = have ? static_cast<size_t>(g_pic_pitch) : 0;
    if (pixel_format)
        *pixel_format = have ? 1 : 0;
    if (fresh)
        *fresh = have && g_pic_fresh;
    if (!have)
        return NULL;
    g_pic_fresh = false;
    return g_odin.picture_pixels();
}

bool host_save_state(const char *path)
{
    if (!g_loaded || !g_core_up || !path)
        return false;
    char err[512] = "";
    const int32_t n = g_odin.save_state(path, err, sizeof(err));
    odin_log("host_save_state: odin_save_state %d%s%s", n, err[0] ? ": " : "", err);
    return n == ODIN_OK;
}

bool host_load_state(const char *path)
{
    if (!g_loaded || !g_core_up || !path)
        return false;
    char err[512] = "";
    const int32_t n = g_odin.load_state(path, err, sizeof(err));
    odin_log("host_load_state: odin_load_state %d%s%s", n, err[0] ? ": " : "", err);
    return n == ODIN_OK;
}

size_t host_state_size(void)
{
    return 0; // no rewind, no quick resume
}

bool host_serialize(void *buffer, size_t size)
{
    (void)buffer;
    (void)size;
    return false;
}

bool host_unserialize(const void *buffer, size_t size)
{
    (void)buffer;
    (void)size;
    return false;
}

bool host_capture(uint8_t *rgba, int w, int h)
{
    (void)rgba;
    (void)w;
    (void)h;
    return false;
}

void host_set_patches_dir(const char *dir)
{
    (void)dir;
}

const struct retro_memory_map *host_memory_map(void)
{
    return NULL;
}

void *host_memory_data(unsigned id)
{
    (void)id;
    return NULL;
}

size_t host_memory_size(unsigned id)
{
    (void)id;
    return 0;
}

int host_disc_count(void)
{
    return 0;
}

int host_disc_index(void)
{
    return 0;
}

bool host_disc_select(int index)
{
    (void)index;
    return false;
}

void host_cheat_reset(void)
{
}

void host_cheat_set(unsigned index, const char *code)
{
    (void)index;
    (void)code;
}

// ---- compat/swanui_host_xbox.h --------------------------------------------------------------------------------------

bool host_set_speed(bool fast, bool back)
{
    (void)fast;
    return !back; // rewind is not available: false when back is requested
}

uint32_t host_frames_advanced(void)
{
    return 0;
}

bool host_save_state_bg(const char *path, bool wait)
{
    (void)wait;
    return host_save_state(path);
}

bool host_serial_for_path(const char *path, char *serial, size_t size)
{
    (void)path;
    if (serial && size)
        serial[0] = '\0';
    return false;
}

bool host_bios_present(void)
{
    // The frontend asks this after a successful host_load only, and that never returns true in this build. The
    // shared sets are checked by the core (odin_check_set), per game.
    return false;
}

// ---- called by the app (uwp_app.cpp, once per frame) ------------------------------------------------------------------

void host_tsugaru_pump(void)
{
}

void host_tsugaru_pointer(float x, float y, bool left, bool right, bool middle)
{
    (void)x;
    (void)y;
    (void)left;
    (void)right;
    (void)middle;
}

void host_flush_saves(void)
{
    // odin_flush_saves on suspend and from the menu. With no game loaded the core answers ODIN_E_NOT_LOADED.
    if (!g_core_up)
    {
        odin_log("host_flush_saves: core not loaded");
        return;
    }
    char err[512] = "";
    const int32_t n = g_odin.flush_saves(err, sizeof(err));
    odin_log("host_flush_saves: odin_flush_saves %d (%s)%s%s", n, status_name(n), err[0] ? ": " : "", err);
}

// ---- bridge/tsugaru_uwp_host.h: input is dropped (no core reads it) ----------------------------------------------------

void TsugaruUwpKeyEvent(int virtualKey, int down)
{
    (void)virtualKey;
    (void)down;
}

void TsugaruUwpCharEvent(int unicode)
{
    (void)unicode;
}

void TsugaruUwpMouseMove(int x, int y)
{
    (void)x;
    (void)y;
}

void TsugaruUwpMouseButton(int button, int down, int x, int y)
{
    (void)button;
    (void)down;
    (void)x;
    (void)y;
}

void TsugaruUwpResetInput(void)
{
}

void TsugaruUwpSetWindowSize(int wid, int hei)
{
    (void)wid;
    (void)hei;
}

// ---- compat/swanui_sysopts.h: no system has options in this build -------------------------------------------------------

void host_sysopt_set_root(const char *data_root)
{
    (void)data_root;
}

int host_sysopt_count(const char *system_id)
{
    (void)system_id;
    return 0;
}

bool host_sysopt_get(const char *system_id, int index, SysOpt *out)
{
    (void)system_id;
    (void)index;
    (void)out;
    return false;
}

bool host_sysopt_set(const char *system_id, int index, int value_index)
{
    (void)system_id;
    (void)index;
    (void)value_index;
    return false;
}

void host_sysopt_reset(const char *system_id)
{
    (void)system_id;
}

bool host_sysopt_running(const char *system_id)
{
    (void)system_id;
    return false;
}

int host_sysopt_pending(const char *system_id)
{
    (void)system_id;
    return 0;
}

} // extern "C"

// ---- the log of the shell (declared in odin_core_api.h) --------------------------------------------------------------

void odin_log(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    const int need = vsnprintf(nullptr, 0, fmt, ap);
    va_end(ap);
    std::string line(need > 0 ? (size_t)need + 1 : 1, '\0');
    va_start(ap, fmt);
    vsnprintf(&line[0], line.size(), fmt, ap);
    va_end(ap);
    line.resize(need > 0 ? (size_t)need : 0);
    log_line(line);
}
