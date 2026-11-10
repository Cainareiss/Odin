// Tsugaru UWP port - LocalState / install folder roots and the bridge logger.
// SPDX-License-Identifier: GPL-3.0-or-later

#include <winrt/base.h>
#include <winrt/Windows.ApplicationModel.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Storage.h>

#include <cstdarg>
#include <cstdio>
#include <mutex>
#include <string>

#include "uwp_paths.h"

extern "C" void swanui_host_log(const char *line); // compat/swanui_main.h (written by the glue)

namespace {
std::once_flag s_once;
std::string s_data;
std::string s_install;

void Fetch()
{
    try
    {
        s_data = winrt::to_string(winrt::Windows::Storage::ApplicationData::Current().LocalFolder().Path());
    }
    catch (const winrt::hresult_error &)
    {
        s_data.clear();
    }
    try
    {
        s_install = winrt::to_string(winrt::Windows::ApplicationModel::Package::Current().InstalledLocation().Path());
    }
    catch (const winrt::hresult_error &)
    {
        s_install.clear();
    }
    const auto is_sep = [](char c) { return c == '\\' || c == '/'; };
    while (!s_data.empty() && is_sep(s_data.back()))
        s_data.pop_back();
    while (!s_install.empty() && is_sep(s_install.back()))
        s_install.pop_back();
}
} // namespace

extern "C" const char *uwp_data_root(void)
{
    std::call_once(s_once, Fetch);
    return s_data.c_str();
}

extern "C" const char *uwp_install_root(void)
{
    std::call_once(s_once, Fetch);
    return s_install.c_str();
}

extern "C" void uwp_log(const char *fmt, ...)
{
    char line[1024];
    va_list ap;
    va_start(ap, fmt);
    std::vsnprintf(line, sizeof(line), fmt, ap);
    va_end(ap);
    swanui_host_log(line);
}
