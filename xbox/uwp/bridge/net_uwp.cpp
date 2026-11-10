// Tsugaru UWP port - the frontend's net.h on Windows.Web.Http (replaces REF net.c / the DuckStation HTTPDownloader
// bridge). Every call is blocking and runs on a frontend worker thread (covers, art, cheats): an implicit-MTA CRT
// thread, where C++/WinRT may block. Files are written through the compat layer (swanui_*_u8 -> *FromAppW), so a
// folder the user picked (brokered access) works as a destination.
// SPDX-License-Identifier: GPL-3.0-or-later

#include <winrt/base.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Storage.Streams.h>
#include <winrt/Windows.Web.Http.h>
#include <winrt/Windows.Web.Http.Filters.h>
#include <winrt/Windows.Web.Http.Headers.h>

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <errno.h>
#include <mutex>
#include <string>
#include <vector>

#include "swanui_cdecl.h"
#define Settings SwanSettings
extern "C" {
#include "src/SwanStationPS5.h"
#include "src/net.h"
}
#undef Settings
#include "uwp_paths.h"

// compat/swanui_compat.c: UTF-8 path wrappers over the *FromAppW functions. The bridge is built without the
// force-included compat header, so they are declared here.
extern "C" {
FILE *swanui_fopen_u8(const char *path, const char *mode);
int swanui_remove_u8(const char *path);
int swanui_rename_u8(const char *from, const char *to);
int swanui_mkdir_u8(const char *path, int mode);
}

// The old DuckStation bridge exported this for the glue's frame hook; there is no core-thread poll to wait for now.
extern "C" void swanui_net_core_heartbeat(void)
{
}

namespace {
namespace Http = winrt::Windows::Web::Http;

constexpr const char *DEFAULT_USER_AGENT = SwanStationPS5_NAME "/" SwanStationPS5_VERSION " (Xbox; Odin)";

struct Result
{
    int status = -1; // HTTP status, or < 0 when the request could not be made
    std::string error;
    std::vector<uint8_t> data;
};

Http::HttpClient &Client()
{
    static std::once_flag once;
    static Http::HttpClient client{nullptr};
    std::call_once(once, [] {
        Http::Filters::HttpBaseProtocolFilter filter;
        filter.AllowAutoRedirect(true);
        filter.AutomaticDecompression(true);
        filter.CacheControl().ReadBehavior(Http::Filters::HttpCacheReadBehavior::MostRecent);
        client = Http::HttpClient(filter);
    });
    return client;
}

// Waits for an async operation until the deadline; cancels it when it runs out. False on timeout.
template <class Op>
bool WaitUntil(const Op &op, std::chrono::steady_clock::time_point deadline)
{
    const auto now = std::chrono::steady_clock::now();
    const auto left = (deadline > now) ? std::chrono::duration_cast<std::chrono::milliseconds>(deadline - now) :
                                         std::chrono::milliseconds(0);
    if (op.wait_for(left) == winrt::Windows::Foundation::AsyncStatus::Started)
    {
        op.Cancel();
        return false;
    }
    return true;
}

Result Execute(const char *url, const char *post_data, const char *content_type, const char *user_agent,
               int timeout_seconds)
{
    Result r;
    try
    {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(timeout_seconds);

        Http::HttpRequestMessage request(post_data ? Http::HttpMethod::Post() : Http::HttpMethod::Get(),
                                         winrt::Windows::Foundation::Uri(winrt::to_hstring(url)));
        request.Headers().UserAgent().TryParseAdd(
            winrt::to_hstring((user_agent && user_agent[0] != '\0') ? user_agent : DEFAULT_USER_AGENT));

        if (post_data)
        {
            const char *type = (content_type && content_type[0]) ? content_type : "application/x-www-form-urlencoded";
            // HttpStringContent wants the bare media type; "text/plain; charset=utf-8" would be sent twice.
            std::string media(type);
            if (const size_t semi = media.find(';'); semi != std::string::npos)
                media.resize(semi);
            while (!media.empty() && media.back() == ' ')
                media.pop_back();
            request.Content(Http::HttpStringContent(winrt::to_hstring(post_data),
                                                    winrt::Windows::Storage::Streams::UnicodeEncoding::Utf8,
                                                    winrt::to_hstring(media)));
        }

        auto send = Client().SendRequestAsync(request, Http::HttpCompletionOption::ResponseHeadersRead);
        if (!WaitUntil(send, deadline))
        {
            r.status = -2;
            r.error = "timeout";
            return r;
        }
        const Http::HttpResponseMessage response = send.GetResults();
        r.status = static_cast<int>(response.StatusCode());

        auto read = response.Content().ReadAsBufferAsync();
        if (!WaitUntil(read, deadline))
        {
            r.status = -2;
            r.error = "timeout (body)";
            return r;
        }
        const winrt::Windows::Storage::Streams::IBuffer buffer = read.GetResults();
        r.data.resize(buffer.Length());
        if (!r.data.empty())
        {
            winrt::Windows::Storage::Streams::DataReader::FromBuffer(buffer).ReadBytes(
                winrt::array_view<uint8_t>(r.data.data(), static_cast<uint32_t>(r.data.size())));
        }
    }
    catch (const winrt::hresult_error &e)
    {
        r.status = -1;
        r.error = winrt::to_string(e.message());
        r.data.clear();
    }
    catch (...)
    {
        r.status = -1;
        r.error = "exception";
        r.data.clear();
    }
    return r;
}

// Creates every missing folder above `file` (the frontend's make_dirs does too, this is the safety net).
void EnsureParentDirs(const std::string &file)
{
    for (size_t i = 1; i < file.size(); ++i)
    {
        if (file[i] == '/' || file[i] == '\\')
            swanui_mkdir_u8(file.substr(0, i).c_str(), 0777); // EEXIST and "can't look" answers are fine
    }
}
} // namespace

bool net_available(void)
{
    // Covers, title screens and gameplay snaps come from the LaunchBox Games Database (platform "Fujitsu FM Towns
    // Marty"; the index files in swanui/assets/fmtowns-*-index.txt map names to images.launchbox-app.com).  The
    // network is on unless <LocalState>\\disable-net.txt exists; the "Download missing covers" setting still gates
    // the cover downloads themselves.
    static std::once_flag once;
    static bool enabled = true;
    std::call_once(once, [] {
        const std::string flag = std::string(uwp_data_root()) + "\\disable-net.txt";
        if (FILE *f = swanui_fopen_u8(flag.c_str(), "rb"))
        {
            std::fclose(f);
            enabled = false;
        }
        SwanStationPS5_log("net: %s (LaunchBox Games Database art; disable-net.txt in LocalState turns it off)",
                           enabled ? "on" : "off");
    });
    return enabled;
}

NetResult net_download(const char *url, const char *dest)
{
    if (!url || !dest || url[0] == '\0' || dest[0] == '\0')
        return NET_UNAVAILABLE;

    const std::string part = std::string(dest) + ".part";
    EnsureParentDirs(part);
    FILE *fp = swanui_fopen_u8(part.c_str(), "wb");
    if (!fp)
    {
        SwanStationPS5_log("net: cannot write %s", part.c_str());
        return NET_UNAVAILABLE;
    }

    Result r = Execute(url, nullptr, nullptr, nullptr, 25);

    bool ok = (r.status == 200 &&
               (r.data.empty() || std::fwrite(r.data.data(), 1, r.data.size(), fp) == r.data.size()));
    ok = (std::fclose(fp) == 0) && ok;
    ok = ok && swanui_rename_u8(part.c_str(), dest) == 0;
    if (ok)
        return NET_OK;

    swanui_remove_u8(part.c_str());
    if (r.status == 404)
        return NET_NOT_FOUND;

    SwanStationPS5_log("net: %s -> http %d (%s)", url, r.status, r.error.c_str());
    // Any answered request that is not a 200 (or whose file could not be written) is "not found";
    // transport failures (error, timeout) are "unavailable".
    return (r.status >= 0) ? NET_NOT_FOUND : NET_UNAVAILABLE;
}

int net_request(const char *url, const char *post_data, const char *content_type, const char *user_agent, char **body,
                size_t *body_length)
{
    *body = nullptr;
    *body_length = 0;
    if (!url || url[0] == '\0')
        return -1;

    Result r = Execute(url, post_data, content_type, user_agent, 30);
    if (r.status < 0)
    {
        SwanStationPS5_log("net: %s -> error %d (%s)", url, r.status, r.error.c_str());
        return -1;
    }

    // The body comes back for any HTTP status, malloc'd and NUL-terminated.
    char *b = static_cast<char *>(std::malloc(r.data.size() + 1));
    if (!b)
        return -1;
    if (!r.data.empty())
        std::memcpy(b, r.data.data(), r.data.size());
    b[r.data.size()] = '\0';

    *body = b;
    *body_length = r.data.size();
    return r.status;
}
