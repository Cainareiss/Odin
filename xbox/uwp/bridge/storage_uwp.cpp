// Tsugaru UWP port - the drive letters the app can read, for the frontend's drives.h
// (compat/swanui_drives_xbox.h). On Xbox (dev mode) and on Windows, an internal or USB drive is readable by letter
// when it is NTFS and grants ALL APPLICATION PACKAGES access; picked folders are reached through the brokered file
// functions instead (folder_uwp.cpp). So the drives are the letters GetLogicalDrives() reports, of type removable or
// fixed, whose root the app can list, minus the drives that hold the app's data folder and its install folder.
// Unused slots hold SWANUI_NO_DRIVE, which the helpers of compat/swanui_drives_xbox.h skip.
// Ported from the Flamenco One bridge (storage_xbox.cpp): EmuFolders::DataRoot/AppRoot -> uwp_paths.h.
// SPDX-License-Identifier: GPL-3.0-or-later

#include <windows.h>

#include <cctype>
#include <cstdio>
#include <iterator>
#include <mutex>
#include <string>

#include "swanui_cdecl.h"
#define Settings SwanSettings
extern "C" {
#include "src/SwanStationPS5.h"
#include "compat/swanui_drives_xbox.h" // declarations only here: DRIVE_COUNT is not defined in this TU
}
#undef Settings
#include "uwp_paths.h"

namespace {
std::mutex s_mutex;
DWORD s_mask = 0xFFFFFFFFu; // no real mask has every bit: forces the first probe
char s_names[SWANUI_DRIVE_SLOTS][4];
const char *s_table[SWANUI_DRIVE_SLOTS] = {SWANUI_NO_DRIVE, SWANUI_NO_DRIVE, SWANUI_NO_DRIVE, SWANUI_NO_DRIVE,
                                           SWANUI_NO_DRIVE, SWANUI_NO_DRIVE, SWANUI_NO_DRIVE, SWANUI_NO_DRIVE};
DWORD s_warned = 0; // letters already logged as unreadable

static_assert(SWANUI_DRIVE_SLOTS == 8, "s_table's initialiser lists 8 slots");

char LetterOf(const char *path)
{
    return (path && path[0] && path[1] == ':') ? static_cast<char>(std::toupper(static_cast<unsigned char>(path[0]))) :
                                                  '\0';
}

std::string Utf8(const wchar_t *w)
{
    char buf[128];
    const int n = WideCharToMultiByte(CP_UTF8, 0, w, -1, buf, static_cast<int>(sizeof(buf)), nullptr, nullptr);
    return (n > 0) ? std::string(buf) : std::string("?");
}

// L"E:\\*": true when the root can be listed, or is listable but empty.
bool CanListRoot(const wchar_t *root_glob)
{
    WIN32_FIND_DATAW fd;
    const HANDLE handle = FindFirstFileExW(root_glob, FindExInfoBasic, &fd, FindExSearchNameMatch, nullptr, 0);
    if (handle != INVALID_HANDLE_VALUE)
    {
        FindClose(handle);
        return true;
    }
    return (GetLastError() == ERROR_FILE_NOT_FOUND);
}

void Rebuild(DWORD mask)
{
    const char skip_data = LetterOf(uwp_data_root());
    const char skip_app = LetterOf(uwp_install_root());

    int count = 0;
    std::string found;
    for (int i = 2 /* C: (A: and B: are never probed) */; i < 26 && count < SWANUI_DRIVE_SLOTS; i++)
    {
        if (!(mask & (1u << i)))
            continue;

        const char letter = static_cast<char>('A' + i);
        if (letter == skip_data || letter == skip_app)
            continue;

        const wchar_t root[] = {static_cast<wchar_t>(L'A' + i), L':', L'\\', L'\0'};
        const UINT type = GetDriveTypeW(root);
        if (type != DRIVE_REMOVABLE && type != DRIVE_FIXED) // no CD, network or RAM disks
            continue;

        const wchar_t glob[] = {static_cast<wchar_t>(L'A' + i), L':', L'\\', L'*', L'\0'};
        if (!CanListRoot(glob))
        {
            const DWORD err = GetLastError();
            if (!(s_warned & (1u << i)))
            {
                s_warned |= (1u << i);
                wchar_t fs[32] = L"?";
                if (!GetVolumeInformationW(root, nullptr, 0, nullptr, nullptr, nullptr, fs,
                                           static_cast<DWORD>(std::size(fs))))
                {
                    fs[0] = L'?';
                    fs[1] = L'\0';
                }
                SwanStationPS5_log(
                    "drives: %c: not readable (%s, error %lu): format as NTFS and grant ALL APPLICATION PACKAGES",
                    letter, Utf8(fs).c_str(), static_cast<unsigned long>(err));
            }
            continue;
        }

        std::snprintf(s_names[count], sizeof(s_names[count]), "%c:", letter);
        s_table[count] = s_names[count];
        found += s_names[count];
        found += ' ';
        count++;
    }
    for (; count < SWANUI_DRIVE_SLOTS; count++)
        s_table[count] = SWANUI_NO_DRIVE;

    if (!found.empty())
        found.pop_back();
    SwanStationPS5_log("drives: %s", found.empty() ? "none" : found.c_str());
}
} // namespace

// SWANUI_DRIVE_SLOTS entries: "E:", "F:"... then SWANUI_NO_DRIVE. Called through drive_paths on the main thread
// (main.c start-up and app_rescan). The table is rebuilt in place when the drive mask changed, so a drive plugged in
// later is found by the next "Rescan library".
extern "C" const char *const *swanui_drive_paths(void)
{
    const std::lock_guard lock(s_mutex);
    const DWORD mask = GetLogicalDrives();
    if (mask != s_mask)
    {
        s_mask = mask;
        Rebuild(mask);
    }
    return s_table;
}
