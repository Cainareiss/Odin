// Flamenco One on Tsugaru (FM TOWNS) - UWP application entry (Xbox / Windows 10).
// A CoreApplication view source: owns the CoreWindow, forwards its events to the frontend (plat_d3d11) and to the
// emulator (fssimplewindow wrapper), and runs the frontend loop on its own thread.
// SPDX-License-Identifier: GPL-3.0-or-later

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdarg>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include <windows.h>

#include <winrt/Windows.ApplicationModel.h>
#include <winrt/Windows.ApplicationModel.Activation.h>
#include <winrt/Windows.ApplicationModel.Core.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Graphics.Display.h>
#include <winrt/Windows.Storage.h>
#include <winrt/Windows.System.h>
#include <winrt/Windows.UI.Core.h>
#include <winrt/Windows.UI.Input.h>
#include <winrt/Windows.UI.ViewManagement.h>

#include "swanui_cdecl.h"
extern "C" {
#include "compat/swanui_main.h"
void plat_game_rect(int *x, int *y, int *w, int *h);
}
#include "plat_d3d11.h"
#include "tsugaru_uwp_host.h"
#include "uwp_paths.h"

extern "C" void host_tsugaru_pump(void);
extern "C" bool host_loaded(void);
extern "C" void host_unload(void);
extern "C" void odin_host_boot(void);     // host_odin.cpp: odin_core.dll, its selftest hook, odin_init
extern "C" void odin_host_shutdown(void); // host_odin.cpp: odin_shutdown
extern "C" void host_tsugaru_pointer(float canvas_x, float canvas_y, bool left, bool middle, bool right);

using namespace winrt;
using namespace winrt::Windows::ApplicationModel;
using namespace winrt::Windows::ApplicationModel::Core;
using namespace winrt::Windows::Foundation;
using namespace winrt::Windows::Graphics::Display;
using namespace winrt::Windows::UI::Core;
using namespace winrt::Windows::UI::ViewManagement;

namespace
{

// ---- log and startup trace (LocalState\tsugaru-uwp.log, LocalState\startup-trace.txt) ----

std::mutex g_log_lock;

std::wstring local_state_file(const wchar_t *name)
{
    std::wstring dir = Windows::Storage::ApplicationData::Current().LocalFolder().Path().c_str();
    return dir + L"\\" + name;
}

void append_line(const std::wstring &path, const std::string &line)
{
    std::lock_guard<std::mutex> lock(g_log_lock);
    FILE *f = _wfopen(path.c_str(), L"ab");
    if (!f)
        return;
    fwrite(line.data(), 1, line.size(), f);
    fputc('\n', f);
    fclose(f);
}

void trace(const char *fmt, ...)
{
    char buf[1024];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    static const std::wstring path = local_state_file(L"startup-trace.txt");
    append_line(path, buf);
    OutputDebugStringA(buf);
    OutputDebugStringA("\n");
}

LONG CALLBACK exception_logger(PEXCEPTION_POINTERS info)
{
    const DWORD code = info->ExceptionRecord->ExceptionCode;
    // Skip the first-chance exceptions that programs use for flow control (C++ throw, debugger breaks, name-the-thread).
    if (code != 0xE06D7363 && code != 0x406D1388 && code != 0x40010006 && code != 0x4001000A && code != 0x80000003)
        trace("exception %08lX at %p (thread %lu)", code, info->ExceptionRecord->ExceptionAddress, GetCurrentThreadId());
    return EXCEPTION_CONTINUE_SEARCH;
}

void install_exception_logger()
{
    // AddVectoredExceptionHandler is not declared for the app partition; take it from kernelbase.
    using Fn = PVOID(WINAPI *)(ULONG, PVECTORED_EXCEPTION_HANDLER);
    HMODULE m = GetModuleHandleW(L"kernelbase.dll");
    if (!m)
        m = GetModuleHandleW(L"kernel32.dll");
    if (m)
    {
        auto fn = reinterpret_cast<Fn>(GetProcAddress(m, "AddVectoredExceptionHandler"));
        if (fn)
            fn(1, exception_logger);
    }
}

// ---- state shared between the UI thread and the frontend thread ----

std::thread g_frontend;
std::atomic<bool> g_quit{false};
std::atomic<bool> g_suspend_request{false};
std::atomic<bool> g_suspend_done{false};
std::atomic<bool> g_frontend_started{false};
std::atomic<bool> g_exit_request{false}; // KIOSK_GAME: Esc on a PC keyboard closes the app, like the original game
CoreWindow g_window{nullptr};

float g_dpi_scale = 1.0f;
int g_pix_w = 1920, g_pix_h = 1080;

void refresh_size()
{
    if (!g_window)
        return;
    const auto b = g_window.Bounds();
    float scale = 1.0f;
    try
    {
        scale = static_cast<float>(DisplayInformation::GetForCurrentView().RawPixelsPerViewPixel());
    }
    catch (...)
    {
    }
    g_dpi_scale = scale > 0.1f ? scale : 1.0f;
    g_pix_w = static_cast<int>(b.Width * g_dpi_scale + 0.5f);
    g_pix_h = static_cast<int>(b.Height * g_dpi_scale + 0.5f);
    if (g_pix_w < 16)
        g_pix_w = 16;
    if (g_pix_h < 16)
        g_pix_h = 16;
}

#ifdef KIOSK_GAME
// The game file the kiosk starts: <install>/game/<rom>, '/' separators. <rom> is the "rom=" line of <install>\kiosk.txt
// (a path inside game\); without the file or the line the path ends in "game/", which the frontend resolves to the first
// game found in that folder.
std::string kiosk_rom_path()
{
    std::string install = uwp_install_root();
    for (char &c : install)
        if (c == '\\')
            c = '/';
    std::string rom;
    if (FILE *f = _wfopen(winrt::to_hstring(install + "/kiosk.txt").c_str(), L"rb"))
    {
        char b[1024];
        const size_t n = fread(b, 1, sizeof(b) - 1, f);
        fclose(f);
        b[n] = 0;
        size_t at = 0;
        const std::string text(b);
        while (at < text.size())
        {
            size_t end = text.find_first_of("\r\n", at);
            if (end == std::string::npos)
                end = text.size();
            std::string line = text.substr(at, end - at);
            at = end + 1;
            while (!line.empty() && (line.back() == ' ' || line.back() == '\t'))
                line.pop_back();
            if (line.compare(0, 4, "rom=") == 0)
            {
                rom = line.substr(4);
                break;
            }
        }
        for (char &c : rom)
            if (c == '\\')
                c = '/';
        while (!rom.empty() && rom.front() == '/')
            rom.erase(rom.begin());
    }
    return install + "/game/" + rom;
}

// The keys the kiosk passes on (a PC keyboard, for tests): the arrows, Enter = RUN/Start, Tab = SELECT, A = the pad's A
// button, S = its B button (which the frontend's per-system map turns into the game's buttons). 0: ignored.
uint32_t kiosk_key(uint32_t vk)
{
    switch (vk)
    {
    case VK_LEFT:
    case VK_UP:
    case VK_RIGHT:
    case VK_DOWN:
    case VK_RETURN:
    case VK_TAB:
        return vk;
    case 'A':
        return 'Z'; // plat_xbox_key_event: Z = pad A (BTN_CROSS)
    case 'S':
        return 'X'; // X = pad B (BTN_CIRCLE)
    default:
        return 0;
    }
}
#endif

void frontend_main()
{
    try
    {
        winrt::init_apartment(winrt::apartment_type::multi_threaded);
    }
    catch (...)
    {
    }
    trace("frontend thread: start");
#ifdef KIOSK_GAME
    // The kiosk (target kiosk_uwp): one packaged game, started at once, nothing else. <install>\kiosk.txt (written by
    // tools/package_kiosk.ps1) has a line "rom=<file inside game\>"; without it the first game found in <install>\game
    // starts. The frontend lists <install>\game as its only library (swanui/compat/swanui_kiosk.c).
    std::string launch = kiosk_rom_path();
    trace("kiosk: --rom %s", launch.c_str());
    char arg0[] = "swanui";
    char arg_kiosk[] = "--kiosk";
    char arg_rom[] = "--rom";
    char arg_exit[] = "--exit-after-game";
    std::vector<char *> av;
    av.push_back(arg0);
    av.push_back(arg_kiosk);
    av.push_back(arg_rom);
    av.push_back(launch.data());
    av.push_back(arg_exit);
    av.push_back(nullptr);
#else
    // Debug/test hook: the first line of LocalState\launch.txt is a game to open at once (--rom).
    std::string launch;
    if (FILE *lf = _wfopen(local_state_file(L"launch.txt").c_str(), L"rb"))
    {
        char b[1024];
        const size_t n = fread(b, 1, sizeof(b) - 1, lf);
        fclose(lf);
        b[n] = 0;
        launch = b;
        while (!launch.empty() && (launch.back() == 10 || launch.back() == 13 || launch.back() == 32))
            launch.pop_back();
        const size_t nl = launch.find_first_of(std::string(1, char(13)) + char(10));
        if (nl != std::string::npos)
            launch.resize(nl);
        for (char &c : launch)
            if (c == char(92))
                c = '/';
    }
    char arg0[] = "swanui";
    char arg1[] = "--rom";
    std::vector<char *> av;
    av.push_back(arg0);
    if (!launch.empty())
    {
        trace("launch.txt: %s", launch.c_str());
        av.push_back(arg1);
        av.push_back(launch.data());
    }
    av.push_back(nullptr);
#endif
    odin_host_boot(); // before the frontend: its library scan asks the core for each set's status
    if (!swanui_main_init(static_cast<int>(av.size()) - 1, av.data()))
    {
        trace("frontend thread: swanui_main_init failed");
        CoreApplication::Exit();
        return;
    }
    trace("frontend thread: swanui_main_init ok");
    bool first = true;
    while (!g_quit)
    {
        if (g_suspend_request.exchange(false))
        {
            swanui_main_suspend();
            PlatD3D11_Trim();
            g_suspend_done = true;
        }
        if (g_exit_request)
            break;
        host_tsugaru_pump();
        if (!swanui_main_frame())
            break;
        if (first)
        {
            trace("frontend thread: first frame done");
            first = false;
        }
    }
    trace("frontend thread: leaving the loop");
    host_unload();
    swanui_main_shutdown();
    odin_host_shutdown();
    trace("frontend thread: shut down");
    if (!g_quit)
        CoreApplication::Exit();
}

// ---- the view ----

struct AppView : implements<AppView, IFrameworkViewSource, IFrameworkView>
{
    IFrameworkView CreateView()
    {
        return *this;
    }

    void Initialize(CoreApplicationView const &view)
    {
        view.Activated({this, &AppView::OnActivated});
        CoreApplication::Suspending({this, &AppView::OnSuspending});
        CoreApplication::Resuming({this, &AppView::OnResuming});
    }

    void Load(hstring const &) {}
    void Uninitialize() {}

    void SetWindow(CoreWindow const &window)
    {
        g_window = window;
        window.SizeChanged({this, &AppView::OnSizeChanged});
        window.Activated({this, &AppView::OnWindowActivated});
        window.KeyDown({this, &AppView::OnKeyDown});
        window.KeyUp({this, &AppView::OnKeyUp});
        window.CharacterReceived({this, &AppView::OnChar});
        window.PointerMoved({this, &AppView::OnPointer});
        window.PointerPressed({this, &AppView::OnPointer});
        window.PointerReleased({this, &AppView::OnPointer});
        window.PointerExited({this, &AppView::OnPointerExited});
        // On the Xbox the B button (and the keyboard's Escape / browser Back) raises the system "back" request; when
        // nobody marks it handled the shell closes the app, like a browser leaving a page.  The frontend handles B
        // itself (through XInput), so the request is always consumed here.
        SystemNavigationManager::GetForCurrentView().BackRequested({this, &AppView::OnBackRequested});
#ifdef KIOSK_GAME
        window.PointerCursor(nullptr); // no mouse pointer over the game
#endif
    }

    void OnBackRequested(IInspectable const &, BackRequestedEventArgs const &args)
    {
        args.Handled(true);
    }

    // VK_GAMEPAD_* and VK_NAVIGATION_* arrive as key events too; left unhandled, the shell turns them into focus
    // navigation and "back".  The pad is read through XInput, so these are consumed.
    static bool IsPadKey(uint32_t vk)
    {
        return (vk >= 0x88u && vk <= 0x8Fu) || (vk >= 0xC3u && vk <= 0xDAu);
    }

    void Run()
    {
        trace("Run: begin");
        auto window = CoreWindow::GetForCurrentThread();
        window.Activate();
        try
        {
            ApplicationView::GetForCurrentView().TryEnterFullScreenMode();
        }
        catch (...)
        {
        }
        refresh_size();
        trace("window %dx%d px, scale %.2f", g_pix_w, g_pix_h, g_dpi_scale);
        PlatD3D11_SetWindow(reinterpret_cast<::IUnknown *>(winrt::get_abi(window)), g_pix_w, g_pix_h, g_dpi_scale);
        PlatD3D11_HdrAttach(); // HDR: display capabilities + the Xbox HDMI mode control (UI thread)

        g_frontend = std::thread(frontend_main);
        g_frontend_started = true;

        window.Dispatcher().ProcessEvents(CoreProcessEventsOption::ProcessUntilQuit);

        trace("Run: dispatcher ended");
        g_quit = true;
        if (g_frontend.joinable())
            g_frontend.join();
        PlatD3D11_HdrShutdown(); // HDR: the TV back in its default (SDR) HDMI mode
        trace("Run: end");
    }

    void OnActivated(CoreApplicationView const &, Activation::IActivatedEventArgs const &)
    {
        CoreWindow::GetForCurrentThread().Activate();
    }

    void OnSuspending(IInspectable const &, SuspendingEventArgs const &args)
    {
        trace("suspending");
        auto deferral = args.SuspendingOperation().GetDeferral();
        if (g_frontend_started)
        {
            g_suspend_done = false;
            g_suspend_request = true;
            for (int i = 0; i < 400 && !g_suspend_done; ++i)
                std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        PlatD3D11_HdrSuspend(); // HDR: default HDMI mode while suspended
        TsugaruUwpResetInput();
        deferral.Complete();
    }

    void OnResuming(IInspectable const &, IInspectable const &)
    {
        trace("resuming");
        PlatD3D11_HdrResume(); // HDR: re-read the display, ask for HDR again
    }

    void OnSizeChanged(CoreWindow const &, WindowSizeChangedEventArgs const &)
    {
        refresh_size();
        PlatD3D11_Resize(g_pix_w, g_pix_h, g_dpi_scale);
    }

    void OnWindowActivated(CoreWindow const &, WindowActivatedEventArgs const &args)
    {
        const bool active = args.WindowActivationState() != CoreWindowActivationState::Deactivated;
        PlatD3D11_SetActive(active);
        swanui_main_set_focus(active); // the kiosk pauses the game (silently) without the focus
        if (!active)
        {
            plat_xbox_keys_reset();
            TsugaruUwpResetInput();
        }
    }

    void OnKeyDown(CoreWindow const &, KeyEventArgs const &args)
    {
        uint32_t vk = static_cast<uint32_t>(args.VirtualKey());
        if (IsPadKey(vk))
        {
            args.Handled(true);
            return;
        }
#ifdef KIOSK_GAME
        args.Handled(true);
        if (vk == VK_ESCAPE) // on a PC Esc closes the app, like the original; nothing on the controller does
        {
            g_exit_request = true;
            return;
        }
        vk = kiosk_key(vk);
        if (!vk)
            return;
#endif
        if (host_loaded())
            TsugaruUwpKeyEvent(static_cast<int>(vk), 1);
        else
            plat_xbox_key_event(vk, true);
    }

    void OnKeyUp(CoreWindow const &, KeyEventArgs const &args)
    {
        uint32_t vk = static_cast<uint32_t>(args.VirtualKey());
        if (IsPadKey(vk))
        {
            args.Handled(true);
            return;
        }
#ifdef KIOSK_GAME
        args.Handled(true);
        vk = kiosk_key(vk);
        if (!vk)
            return;
#endif
        TsugaruUwpKeyEvent(static_cast<int>(vk), 0);
        plat_xbox_key_event(vk, false);
    }

    void OnChar(CoreWindow const &, CharacterReceivedEventArgs const &args)
    {
        if (host_loaded())
            TsugaruUwpCharEvent(static_cast<int>(args.KeyCode()));
    }

    void OnPointer(CoreWindow const &, PointerEventArgs const &args)
    {
        const auto pt = args.CurrentPoint();
        const auto pos = pt.Position(); // DIPs
        const float px = static_cast<float>(pos.X) * g_dpi_scale;
        const float py = static_cast<float>(pos.Y) * g_dpi_scale;
        const auto props = pt.Properties();
        const bool left = props.IsLeftButtonPressed();
        const bool middle = props.IsMiddleButtonPressed();
        const bool right = props.IsRightButtonPressed();
        PlatD3D11_Pointer(px, py, left || right || middle, true);

        // The picture's canvas is 1920x1080 letterboxed into the swap chain.
        const float scale = std::min(g_pix_w / 1920.0f, g_pix_h / 1080.0f);
        const float ox = (g_pix_w - 1920.0f * scale) * 0.5f;
        const float oy = (g_pix_h - 1080.0f * scale) * 0.5f;
        if (scale > 0.0f)
            host_tsugaru_pointer((px - ox) / scale, (py - oy) / scale, left, middle, right);
    }

    void OnPointerExited(CoreWindow const &, PointerEventArgs const &)
    {
        PlatD3D11_Pointer(0, 0, false, false);
    }
};

} // namespace

// The frontend's log lines (SwanStationPS5_log) and the bridges' uwp_log end up here.
extern "C" void swanui_host_log(const char *line)
{
    if (!line)
        return;
    std::string text(line);
    while (!text.empty() && (text.back() == '\n' || text.back() == '\r'))
        text.pop_back();
    static const std::wstring path = local_state_file(L"odin-uwp.log");
    append_line(path, text);
    OutputDebugStringA(text.c_str());
    OutputDebugStringA("\n");
}

int __stdcall wWinMain(HINSTANCE, HINSTANCE, PWSTR, int)
{
    install_exception_logger();
    trace("wWinMain");
    try
    {
        ApplicationView::PreferredLaunchWindowingMode(ApplicationViewWindowingMode::FullScreen);
    }
    catch (...)
    {
    }
    CoreApplication::Run(make<AppView>());
    return 0;
}
