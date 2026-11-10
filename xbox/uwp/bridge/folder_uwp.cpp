// Tsugaru UWP port - the native folder picker for the games folder (compat/swanui_xbox_folder.h).
// On the console, USB drives are not readable by drive letter; access to a folder comes from
// Windows.Storage.Pickers and is kept across launches in the FutureAccessList, after which the *FromApp file
// functions of the compat layer (compat/swanui_compat.c) can open it by path.
// Ported from the Flamenco One bridge (folder_xbox.cpp): DuckStation logging replaced by uwp_log.
// SPDX-License-Identifier: GPL-3.0-or-later

#include <winrt/base.h>
#include <winrt/Windows.ApplicationModel.Core.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Storage.h>
#include <winrt/Windows.Storage.AccessCache.h>
#include <winrt/Windows.Storage.Pickers.h>
#include <winrt/Windows.UI.Core.h>

#include <cstdint>
#include <cstdio>
#include <mutex>
#include <string>

#include "swanui_cdecl.h"
extern "C" {
#include "compat/swanui_xbox_folder.h"
}
#include "uwp_paths.h"

namespace {
std::mutex s_mutex;
std::string s_result; // the picked path, native separators
bool s_busy = false;  // a picker is open
bool s_done = false;  // a result waits for plat_xbox_pick_folder_poll
bool s_picked = false;
constexpr const wchar_t *kToken = L"odin-games";

winrt::fire_and_forget PickAsync()
{
    std::string result;
    bool picked = false;
    try
    {
        // The picker must run on the UI thread; the caller is the frontend thread.
        const auto dispatcher =
            winrt::Windows::ApplicationModel::Core::CoreApplication::MainView().CoreWindow().Dispatcher();
        co_await winrt::resume_foreground(dispatcher);

        winrt::Windows::Storage::Pickers::FolderPicker picker;
        picker.SuggestedStartLocation(winrt::Windows::Storage::Pickers::PickerLocationId::ComputerFolder);
        picker.FileTypeFilter().Append(L"*");
        const winrt::Windows::Storage::StorageFolder folder = co_await picker.PickSingleFolderAsync();
        if (folder)
        {
            winrt::Windows::Storage::AccessCache::StorageApplicationPermissions::FutureAccessList().AddOrReplace(
                kToken, folder);
            result = winrt::to_string(folder.Path());
            picked = !result.empty();
            uwp_log("Games folder picked: '%s'", result.c_str());
        }
        else
        {
            uwp_log("Games folder picker: cancelled");
        }
    }
    catch (const winrt::hresult_error &e)
    {
        uwp_log("Games folder picker failed: %08X %s", static_cast<uint32_t>(e.code()),
                winrt::to_string(e.message()).c_str());
    }
    catch (...)
    {
        uwp_log("Games folder picker failed");
    }

    const std::lock_guard lock(s_mutex);
    s_result = std::move(result);
    s_picked = picked;
    s_done = true;
    s_busy = false;
}

winrt::fire_and_forget RestoreAsync()
{
    try
    {
        const auto list = winrt::Windows::Storage::AccessCache::StorageApplicationPermissions::FutureAccessList();
        if (list.ContainsItem(kToken))
        {
            const auto folder = co_await list.GetFolderAsync(kToken);
            uwp_log("Games folder access restored: '%s'", winrt::to_string(folder.Path()).c_str());
        }
        else
        {
            uwp_log("Games folder: no access token stored (pick the folder again in Settings)");
        }
    }
    catch (const winrt::hresult_error &e)
    {
        uwp_log("Games folder access could not be restored: %08X %s", static_cast<uint32_t>(e.code()),
                winrt::to_string(e.message()).c_str());
    }
    catch (...)
    {
        uwp_log("Games folder access could not be restored");
    }
}
} // namespace

extern "C" void plat_xbox_pick_folder_begin(void)
{
    {
        const std::lock_guard lock(s_mutex);
        if (s_busy)
            return;
        s_busy = true;
        s_done = false;
    }
    PickAsync();
}

extern "C" int plat_xbox_pick_folder_poll(char *out, size_t size)
{
    const std::lock_guard lock(s_mutex);
    if (!s_done)
        return 0;
    s_done = false;
    if (size)
        out[0] = '\0';
    if (!s_picked)
        return -1;
    std::string path = s_result;
    for (char &c : path)
    {
        if (c == '\\')
            c = '/';
    }
    std::snprintf(out, size, "%s", path.c_str());
    return 1;
}

extern "C" void plat_xbox_folder_access_restore(void)
{
    RestoreAsync();
}
