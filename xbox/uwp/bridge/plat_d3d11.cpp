// ares (UWP / Xbox) - the frontend's platform layer (src/platform/platform.h) on Direct3D 11.
// SPDX-License-Identifier: GPL-3.0-or-later
//
// No DuckStation, no ImGui: this file owns the D3D11 device and the DXGI swap chain of the CoreWindow and draws the
// frontend's UI itself.
//  - The UI is authored for a 1920x1080 logical canvas; every vertex is scaled by min(sw/1920, sh/1080) and centred
//    (letterbox) into the real swap-chain size.
//  - Drawing is immediate-mode batched: plat_draw_* append vertices (ARGB, uv, position) to one CPU list; the list is
//    flushed into a dynamic vertex buffer when the texture / sampler / blend / scissor changes and at plat_end_frame.
//    One vertex shader, one pixel shader (tex * colour), a 1x1 white texture for flat colour.
//  - Textures keep a CPU copy (RGBA8) and create the GPU object lazily; mip chains are made on the CPU. A lost device
//    just bumps a generation counter and every texture re-uploads on its next use.
//  - The emulated picture comes from plat_upload_game (0RGB1555 / XRGB8888 / RGB565 -> BGRA8 dynamic texture) and is
//    drawn by plat_draw_game with the REF maths (aspect, integer scale, TV-frame shrink, manual crop, crop edges,
//    smooth / sharp sampling, scanlines and dimming).
//  - Input: XInput 0..3 (xinputuap) + the PC keyboard mask merged into pad 0, rumble with a 2 s watchdog. Audio: one
//    XAudio2 source voice fed from a 4 s ring (plat_audio_*); the core pushes into it from its emulator thread.
//  - HDR (notes/HDR.md): plat_set_hdr / plat_hdr_state. When HDR is wanted and the display can do it, the swap chain
//    becomes R10G10B10A2 + BT.2020 PQ (or FP16 scRGB), every draw goes to an FP16 scene target in the SDR encoding and
//    one pass converts it (PSHdrOut). On the Xbox the TV is switched to an HDR10 HDMI mode first (HdmiDisplayInformation,
//    requested on the UI thread through the CoreDispatcher) and put back with SetDefaultDisplayModeAsync. With HDR off
//    or impossible nothing of this exists and the SDR path is the one from before.
// Thread rule: every plat_* function runs on the frontend thread, except plat_texture_create / plat_texture_free (mutex),
// plat_audio_push / plat_audio_queued_frames / plat_audio_clear / plat_audio_set_ratio (any thread: mutexes / atomics)
// and the PlatD3D11_* / plat_xbox_key_event entry points (UI thread, they only set atomics under a mutex).
// Shaders are compiled offline (bridge/shaders/plat_d3d11.hlsl -> *_vs.h / *_ps.h, compile_shaders.cmd); there is no
// runtime D3DCompile.

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <unknwn.h>

#include <d3d11_1.h>
#include <dxgi1_6.h>
#include <xaudio2.h>
#include <Xinput.h>

#include <winrt/base.h>
#include <winrt/Windows.ApplicationModel.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Globalization.h>
#include <winrt/Windows.Graphics.Display.h>
#include <winrt/Windows.Graphics.Display.Core.h>
#include <winrt/Windows.Storage.h>
#include <winrt/Windows.System.Profile.h>
#include <winrt/Windows.System.UserProfile.h>
#include <winrt/Windows.UI.Core.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstdarg>
#include <cstring>
#include <ctime>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_set>
#include <vector>

// The frontend headers are C, compiled here as C++ inside extern "C" (see the previous bridge's swanui_cdecl.h):
// the GNU attribute syntax is hints only.
#ifndef SWANUI_XBOX
#define SWANUI_XBOX 1
#endif
#define __attribute__(x)
extern "C" {
#include "src/SwanStationPS5.h"
#include "src/i18n.h"
#include "src/platform/platform.h"
#include "src/platform/xbr.h"
}
#undef __attribute__

#include "plat_d3d11.h"

#include "shaders/plat_d3d11_full_vs.h"
#include "shaders/plat_d3d11_game_ps.h"
#include "shaders/plat_d3d11_hdr_ps.h"
#include "shaders/plat_d3d11_ps.h"
#include "shaders/plat_d3d11_vs.h"

// The opaque frontend texture (platform.h: "typedef struct PlatTexture PlatTexture;").
struct PlatTexture
{
    std::vector<uint8_t> rgba; // width*height*4, owned (device-lost recovery and mip source)
    int width = 0;
    int height = 0;
    bool smooth = false;
    bool mips = false;
    winrt::com_ptr<ID3D11ShaderResourceView> srv;
    uint32_t gen = 0;                  // device generation of `srv`
    uint32_t failed_gen = UINT32_MAX;  // do not retry a failed upload every frame
    bool freed = false;                // plat_texture_free called; destroyed at the next plat_begin_frame
};

namespace {

using u8 = uint8_t;
using u32 = uint32_t;
using u64 = uint64_t;
using s16 = int16_t;

constexpr int kCanvasW = 1920;
constexpr int kCanvasH = 1080;
constexpr u32 kVbVerts = 3u * 43690u; // dynamic vertex buffer capacity (a multiple of 3)
constexpr u64 kRumbleWatchdogUs = 2000000;
constexpr u64 kPadProbeUs = 1000000;
constexpr int kMaxPads = SwanStationPS5_MAX_PADS;

// ---- small helpers -----------------------------------------------------------------------------------------------

u64 NowUs()
{
    return static_cast<u64>(
        std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now().time_since_epoch()).count());
}

#define Log(...) SwanStationPS5_log(__VA_ARGS__)

float Clamp01(float v)
{
    return v < 0.0f ? 0.0f : v > 1.0f ? 1.0f : v;
}

// ---- state -------------------------------------------------------------------------------------------------------

bool s_initialized = false;
bool s_gpu_alive = false;
char s_init_error[256] = "";
char s_screen_info[320] = "Direct3D 11 (not initialised)";
u32 s_gen = 1; // device generation: GPU objects of an older one are dead
bool s_need_device = false;

// window (set from the UI thread)
std::mutex s_dev_mutex; // guards s_dev against PlatD3D11_Trim (UI thread)
std::mutex s_win_mutex;
winrt::com_ptr<IUnknown> s_window;
int s_win_w = kCanvasW, s_win_h = kCanvasH;
float s_dpi = 1.0f;
bool s_win_changed = false;     // a new window object: rebuild the swap chain
bool s_size_changed = false;    // new size: ResizeBuffers
std::atomic<bool> s_active{true};

// D3D objects (frontend thread only)
winrt::com_ptr<ID3D11Device> s_dev;
winrt::com_ptr<ID3D11DeviceContext> s_ctx;
winrt::com_ptr<IDXGISwapChain1> s_sc;
winrt::com_ptr<ID3D11RenderTargetView> s_rtv;
winrt::com_ptr<ID3D11VertexShader> s_vs;
winrt::com_ptr<ID3D11PixelShader> s_ps;
winrt::com_ptr<ID3D11PixelShader> s_ps_game; // the game picture: colour, sharpen, FSR-like scale, CRT/LCD/NTSC looks
winrt::com_ptr<ID3D11InputLayout> s_layout;
winrt::com_ptr<ID3D11Buffer> s_cb;
winrt::com_ptr<ID3D11Buffer> s_cb_game; // cbuffer Picture (b1), 8 x float4
winrt::com_ptr<ID3D11Buffer> s_vb;
// HDR output (created with the device; used only while s_out != kOutSdr)
winrt::com_ptr<ID3D11VertexShader> s_vs_full; // full-screen triangle (SV_VertexID)
winrt::com_ptr<ID3D11PixelShader> s_ps_hdr;   // scene -> PQ / scRGB
winrt::com_ptr<ID3D11Buffer> s_cb_hdr;        // cbuffer Output (b2), 1 x float4
winrt::com_ptr<ID3D11Texture2D> s_scene_tex;  // FP16 scene target, swap-chain size
winrt::com_ptr<ID3D11RenderTargetView> s_scene_rtv;
winrt::com_ptr<ID3D11ShaderResourceView> s_scene_srv;
winrt::com_ptr<ID3D11SamplerState> s_samp[3]; // 0 point, 1 linear, 2 trilinear
winrt::com_ptr<ID3D11BlendState> s_blend[2];  // 0 alpha blend, 1 opaque
winrt::com_ptr<ID3D11RasterizerState> s_raster;
winrt::com_ptr<ID3D11ShaderResourceView> s_white;
u32 s_vb_pos = 0;
int s_sw = kCanvasW, s_sh = kCanvasH; // swap-chain size
float s_scale = 1.0f, s_ox = 0.0f, s_oy = 0.0f;
u32 s_frame_serial = 0;
int s_active_pass = 0;
bool s_in_frame = false;

inline float TX(float x) { return s_ox + x * s_scale; }
inline float TY(float y) { return s_oy + y * s_scale; }

// ---- HDR state ---------------------------------------------------------------------------------------------------

enum OutKind
{
    kOutSdr = 0,   // B8G8R8A8_UNORM, sRGB (DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P709): the original path
    kOutPq = 1,    // R10G10B10A2_UNORM, HDR10 (DXGI_COLOR_SPACE_RGB_FULL_G2084_NONE_P2020)
    kOutScrgb = 2, // R16G16B16A16_FLOAT, scRGB linear (DXGI_COLOR_SPACE_RGB_FULL_G10_NONE_P709)
};
const char* const kOutName[3] = {"SDR (B8G8R8A8, sRGB)", "HDR10 (R10G10B10A2, BT.2020 PQ)", "scRGB (R16G16B16A16 float, linear)"};

// frontend thread
int s_out = kOutSdr;                 // what the swap chain presents now
int s_hdr_mode = 0;                  // plat_set_hdr: 0 off, 1 auto, 2 on (off until the frontend calls plat_set_hdr)
int s_hdr_peak = 1000, s_hdr_paper = 200, s_hdr_boost = 2;
int s_xbox_posted = -1;              // the HDMI request last posted to the UI thread: -1 none, 0 SDR, 1 HDR10, 2 HDR10 any Hz
int s_hdr_logged_state = -1;
std::string s_hdr_detail = "not checked yet";
bool s_hdr_set_once = false;         // plat_set_hdr was called
bool s_hdr_swapchain_failed = false; // HDR was wanted but no HDR format / colour space worked: stay SDR
bool s_hdr_test = false;             // developer switch <LocalState>\hdr-test.txt (PC): treat the display as HDR
int s_meta_peak = 0, s_meta_paper = 0;

// any thread
std::atomic<bool> s_hdr_dirty{true};   // re-evaluate at the next plat_begin_frame
std::atomic<int> s_hdr_state{0};       // plat_hdr_state()
std::atomic<bool> s_hdr_suspended{false};
std::atomic<bool> s_xbox_repost{false}; // forget s_xbox_posted (resume, new display modes)
// capabilities, written by the UI thread (PlatD3D11_HdrAttach and the display events)
std::atomic<bool> s_cap_attached{false};    // PlatD3D11_HdrAttach ran (DisplayInformation known)
std::atomic<bool> s_cap_xbox{false};        // HdmiDisplayInformation exists (Xbox)
std::atomic<bool> s_cap_hdr10_exact{false}; // Xbox: an HDR10 mode at the current resolution and refresh rate
std::atomic<bool> s_cap_hdr10_res{false};   // Xbox: an HDR10 mode at the current resolution (any refresh rate)
std::atomic<bool> s_cap_os_hdr_on{false};   // AdvancedColorInfo: the display is in HDR now (Windows HDR on)
std::atomic<bool> s_cap_os_hdr_avail{false};// AdvancedColorInfo: the display can do HDR (IsAdvancedColorKindAvailable)
std::atomic<int> s_cap_max_nits{0};         // the display's peak as the OS reports it (0 unknown)
std::atomic<int> s_cap_sdr_white{0};        // Windows' SDR content brightness (nits, 0 unknown)
// Xbox HDMI mode: 0 default (ours untouched), 1 request pending, 2 HDR10 set by us, 3 request failed
std::atomic<int> s_xbox_mode{0};
std::atomic<u32> s_xbox_gen{0};             // bumped on suspend / exit: completions of older requests are stale

// Messages from the UI / async threads, logged by the frontend thread (SwanStationPS5_log is not called elsewhere).
std::mutex s_note_mutex;
std::vector<std::string> s_notes;
std::atomic<bool> s_have_notes{false};

void HdrNote(const char* fmt, ...)
{
    char buf[400];
    va_list ap;
    va_start(ap, fmt);
    std::vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    OutputDebugStringA(buf);
    OutputDebugStringA("\n");
    std::lock_guard<std::mutex> lk(s_note_mutex);
    if (s_notes.size() < 64)
        s_notes.emplace_back(buf);
    s_have_notes.store(true, std::memory_order_release);
}

// UI-thread WinRT objects. Heap-held and released by PlatD3D11_HdrShutdown, never by a static destructor (the
// apartment may be gone by then).
struct HdrUi
{
    winrt::Windows::UI::Core::CoreDispatcher dispatcher{nullptr};
    winrt::Windows::Graphics::Display::DisplayInformation display{nullptr};
    winrt::Windows::Graphics::Display::Core::HdmiDisplayInformation hdmi{nullptr};
    winrt::event_token colour_token{};
    winrt::event_token modes_token{};
};
std::mutex s_ui_mutex;
HdrUi* s_ui = nullptr;

// batch
struct Vtx
{
    float x, y, u, v;
    u32 col; // ARGB (B8G8R8A8_UNORM input element)
};
struct ClipRect
{
    int x0 = 0, y0 = 0, x1 = 0, y1 = 0;
    bool empty = false;
};
std::vector<Vtx> s_verts;
ID3D11ShaderResourceView* s_b_srv = nullptr;
int s_b_samp = 0;
bool s_b_opaque = false;
ClipRect s_b_clip;
ClipRect s_clip; // current clip, physical (full swap chain when off)
bool s_clip_on = false;

// textures
std::mutex s_tex_mutex;
std::unordered_set<PlatTexture*> s_live;
std::vector<PlatTexture*> s_free_queue;

// the game picture
winrt::com_ptr<ID3D11Texture2D> s_game_tex;
winrt::com_ptr<ID3D11ShaderResourceView> s_game_srv;
u32 s_game_gen = 0;
int s_game_w = 0, s_game_h = 0;     // texture size
int s_src_w = 0, s_src_h = 0;       // last uploaded picture
bool s_game_have = false;
int s_game_rect[4] = {0, 0, 0, 0};  // logical, plat_game_rect
std::vector<u32> s_game_conv;       // the 32-bit copy of a 16-bit frame, or the frame itself when a prescaler runs
std::vector<u32> s_game_scaled;     // prescaler output (Scale2x / xBR)
int s_prescale = 1;                 // texture = picture x s_prescale
// picture options (plat_set_colour / plat_set_fsr); the frontend calls them with the Settings values
int s_brightness = 1, s_colour = 0, s_sharpen = 0;
bool s_fsr = false;

// input
int s_player_order[kMaxPads] = {0, 1, 2, 3};
bool s_connected[kMaxPads] = {};
u64 s_last_probe[kMaxPads] = {};
u32 s_rumble_strong[kMaxPads] = {};
u32 s_rumble_weak[kMaxPads] = {};
u64 s_rumble_at[kMaxPads] = {};
std::atomic<u32> s_key_bits{0};
bool s_pad_motion = false, s_pad_touch = false;
int s_want_output = 0;
std::mutex s_ptr_mutex;
float s_ptr_x = 0, s_ptr_y = 0, s_ptr_last_x = 0, s_ptr_last_y = 0;
bool s_ptr_pressed = false, s_ptr_valid = false, s_ptr_have_last = false;
float s_ptr_dx = 0, s_ptr_dy = 0;

// display refresh estimate
u64 s_present_last_us = 0;
double s_present_ema_us = 0.0;
int s_present_samples = 0;

// clock
bool s_clock_24h = true;
bool s_clock_checked = false;
u64 s_clock_checked_us = 0;

// backdrop
const u8* s_backdrop_shown = nullptr;
PlatTexture* s_backdrop_tex = nullptr;
int s_backdrop_w = 0, s_backdrop_h = 0;
bool s_backdrop_dirty = false;

// ---- audio (UI sounds and the core's sound) ----------------------------------------------------------------------
// Threads: the core pushes from its emulator thread (plat_audio_push), the XAudio2 thread pulls (OnBufferEnd ->
// AudioFill), the frontend thread opens / closes / clears / sets the ratio / reads the queue.
//  - s_ring_mutex guards the ring and its indices; it is held only for two memcpy of at most one push / one buffer.
//  - s_push_mutex guards the producer's resampler state (position, last frame, scratch); only plat_audio_push and
//    plat_audio_open / close take it, so it is uncontended while playing. Lock order: s_push_mutex, then s_ring_mutex.
//  - s_audio_live / s_audio_ratio are atomics; the voice pointers belong to open / close (frontend thread) and the
//    XAudio2 callback, which DestroyVoice waits for.
// Latency: 4 XAudio2 buffers of 512 frames (~43 ms at 48 kHz) plus whatever the core keeps queued in the ring.

constexpr int kAudioBuffers = 4;
constexpr int kAudioFrames = 512; // per XAudio2 buffer

std::mutex s_ring_mutex;
std::vector<s16> s_ring; // interleaved stereo, s_ring_frames * 2 samples
size_t s_ring_frames = 0;
u64 s_read = 0, s_write = 0;
int s_rate = 0;
std::mutex s_push_mutex;
std::atomic<double> s_audio_ratio{1.0};
double s_audio_pos = 0.0;      // s_push_mutex
s16 s_audio_last[2] = {0, 0};  // s_push_mutex
std::vector<s16> s_stretch_out; // s_push_mutex

winrt::com_ptr<IXAudio2> s_xa;
IXAudio2MasteringVoice* s_master = nullptr;
IXAudio2SourceVoice* s_voice = nullptr;
std::atomic<bool> s_audio_live{false};
s16 s_abuf[kAudioBuffers][kAudioFrames * 2];

void AudioFill(int index)
{
    s16* out = s_abuf[index];
    u32 copied = 0;
    {
        std::lock_guard<std::mutex> lk(s_ring_mutex);
        if (s_ring_frames > 0)
        {
            const u64 queued = s_write - s_read;
            copied = static_cast<u32>(std::min<u64>(queued, kAudioFrames));
            // at most two contiguous pieces (the ring wraps once)
            const size_t start = static_cast<size_t>(s_read % s_ring_frames);
            const size_t first = std::min<size_t>(copied, s_ring_frames - start);
            std::memcpy(out, s_ring.data() + start * 2, first * 2 * sizeof(s16));
            if (copied > first)
                std::memcpy(out + first * 2, s_ring.data(), (copied - first) * 2 * sizeof(s16));
            s_read += copied;
        }
    }
    if (copied < kAudioFrames) // silence on underrun
        std::memset(out + static_cast<size_t>(copied) * 2, 0, (kAudioFrames - copied) * 2 * sizeof(s16));
}

void AudioSubmit(int index)
{
    if (!s_audio_live.load(std::memory_order_acquire) || !s_voice)
        return;
    AudioFill(index);
    XAUDIO2_BUFFER b = {};
    b.AudioBytes = kAudioFrames * 2 * sizeof(s16);
    b.pAudioData = reinterpret_cast<const BYTE*>(s_abuf[index]);
    b.pContext = reinterpret_cast<void*>(static_cast<intptr_t>(index));
    s_voice->SubmitSourceBuffer(&b);
}

class UiAudioCallback final : public IXAudio2VoiceCallback
{
public:
    void STDMETHODCALLTYPE OnVoiceProcessingPassStart(UINT32) override {}
    void STDMETHODCALLTYPE OnVoiceProcessingPassEnd() override {}
    void STDMETHODCALLTYPE OnStreamEnd() override {}
    void STDMETHODCALLTYPE OnBufferStart(void*) override {}
    void STDMETHODCALLTYPE OnBufferEnd(void* context) override
    {
        AudioSubmit(static_cast<int>(reinterpret_cast<intptr_t>(context)));
    }
    void STDMETHODCALLTYPE OnLoopEnd(void*) override {}
    void STDMETHODCALLTYPE OnVoiceError(void*, HRESULT) override {}
};
UiAudioCallback s_audio_cb;

void RingPush(const s16* frames, size_t n)
{
    std::lock_guard<std::mutex> lk(s_ring_mutex);
    if (s_ring_frames == 0 || n == 0)
        return;
    if (n > s_ring_frames) // keep the newest
    {
        frames += (n - s_ring_frames) * 2;
        n = s_ring_frames;
    }
    const u64 queued = s_write - s_read;
    if (queued + n > s_ring_frames)
        s_read = s_write + n - s_ring_frames; // drop the oldest
    const size_t start = static_cast<size_t>(s_write % s_ring_frames);
    const size_t first = std::min<size_t>(n, s_ring_frames - start);
    std::memcpy(s_ring.data() + start * 2, frames, first * 2 * sizeof(s16));
    if (n > first)
        std::memcpy(s_ring.data(), frames + first * 2, (n - first) * 2 * sizeof(s16));
    s_write += n;
}

void RingClear()
{
    std::lock_guard<std::mutex> lk(s_ring_mutex);
    s_read = s_write;
}

// ---- WinRT lookups -----------------------------------------------------------------------------------------------

std::string AppRootPath()
{
    try
    {
        return winrt::to_string(winrt::Windows::ApplicationModel::Package::Current().InstalledLocation().Path());
    }
    catch (...)
    {
        return std::string();
    }
}

std::string DataRootPath()
{
    try
    {
        return winrt::to_string(winrt::Windows::Storage::ApplicationData::Current().LocalFolder().Path());
    }
    catch (...)
    {
        return std::string();
    }
}

bool LocalStateFileExists(const wchar_t* name)
{
    try
    {
        std::wstring p = winrt::Windows::Storage::ApplicationData::Current().LocalFolder().Path().c_str();
        p += L"\\";
        p += name;
        WIN32_FILE_ATTRIBUTE_DATA fad = {};
        return GetFileAttributesExW(p.c_str(), GetFileExInfoStandard, &fad) != 0;
    }
    catch (...)
    {
        return false;
    }
}

void CopyPathSlashes(char* out, size_t size, const std::string& path)
{
    if (!out || size == 0)
        return;
    const size_t n = std::min(path.size(), size - 1);
    for (size_t i = 0; i < n; ++i)
        out[i] = (path[i] == '\\') ? '/' : path[i];
    out[n] = '\0';
}

void RefreshClockPreference(u64 now, bool force)
{
    if (!force && s_clock_checked && now - s_clock_checked_us < 60000000ull)
        return;
    s_clock_checked = true;
    s_clock_checked_us = now;
    bool h24 = s_clock_24h;
    bool got = false;
    try
    {
        const auto clocks = winrt::Windows::System::UserProfile::GlobalizationPreferences::Clocks();
        if (clocks && clocks.Size() > 0)
        {
            h24 = (clocks.GetAt(0) == winrt::Windows::Globalization::ClockIdentifiers::TwentyFourHour());
            got = true;
        }
    }
    catch (...)
    {
    }
    if (!got)
    {
        wchar_t buf[80] = {};
        if (GetLocaleInfoEx(LOCALE_NAME_USER_DEFAULT, LOCALE_STIMEFORMAT, buf, 80) > 0)
            h24 = (wcschr(buf, L'H') != nullptr);
    }
    s_clock_24h = h24;
}

// ---- D3D11 device ------------------------------------------------------------------------------------------------

void UpdateTransform()
{
    const float sx = static_cast<float>(s_sw) / static_cast<float>(kCanvasW);
    const float sy = static_cast<float>(s_sh) / static_cast<float>(kCanvasH);
    s_scale = std::min(sx, sy);
    if (s_scale <= 0.0f)
        s_scale = 1.0f;
    s_ox = (static_cast<float>(s_sw) - static_cast<float>(kCanvasW) * s_scale) * 0.5f;
    s_oy = (static_cast<float>(s_sh) - static_cast<float>(kCanvasH) * s_scale) * 0.5f;
}

bool CreateDeviceObjects()
{
    D3D_FEATURE_LEVEL levels[] = {D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_1,
                                  D3D_FEATURE_LEVEL_10_0};
    D3D_FEATURE_LEVEL got = D3D_FEATURE_LEVEL_10_0;
    HRESULT hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, D3D11_CREATE_DEVICE_BGRA_SUPPORT, levels,
                                   ARRAYSIZE(levels), D3D11_SDK_VERSION, s_dev.put(), &got, s_ctx.put());
    if (hr == E_INVALIDARG) // a runtime without 11_1
        hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, D3D11_CREATE_DEVICE_BGRA_SUPPORT, levels + 1,
                               ARRAYSIZE(levels) - 1, D3D11_SDK_VERSION, s_dev.put(), &got, s_ctx.put());
    if (FAILED(hr))
    {
        std::snprintf(s_init_error, sizeof(s_init_error), "D3D11CreateDevice failed: 0x%08X", static_cast<unsigned>(hr));
        return false;
    }

    if (FAILED(s_dev->CreateVertexShader(g_plat_vs, sizeof(g_plat_vs), nullptr, s_vs.put())) ||
        FAILED(s_dev->CreatePixelShader(g_plat_ps, sizeof(g_plat_ps), nullptr, s_ps.put())) ||
        FAILED(s_dev->CreatePixelShader(g_plat_ps_game, sizeof(g_plat_ps_game), nullptr, s_ps_game.put())))
    {
        std::snprintf(s_init_error, sizeof(s_init_error), "creating the UI shaders failed");
        return false;
    }
    // HDR output pass: optional (HDR simply stays off without it)
    if (FAILED(s_dev->CreateVertexShader(g_plat_vs_full, sizeof(g_plat_vs_full), nullptr, s_vs_full.put())) ||
        FAILED(s_dev->CreatePixelShader(g_plat_ps_hdr, sizeof(g_plat_ps_hdr), nullptr, s_ps_hdr.put())))
    {
        Log("plat: the HDR output shaders could not be created; HDR stays off");
        s_vs_full = nullptr;
        s_ps_hdr = nullptr;
    }
    const D3D11_INPUT_ELEMENT_DESC elems[] = {
        {"POSITION", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 8, D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"COLOR", 0, DXGI_FORMAT_B8G8R8A8_UNORM, 0, 16, D3D11_INPUT_PER_VERTEX_DATA, 0},
    };
    if (FAILED(s_dev->CreateInputLayout(elems, ARRAYSIZE(elems), g_plat_vs, sizeof(g_plat_vs), s_layout.put())))
    {
        std::snprintf(s_init_error, sizeof(s_init_error), "creating the UI input layout failed");
        return false;
    }

    D3D11_BUFFER_DESC bd = {};
    bd.ByteWidth = 16;
    bd.Usage = D3D11_USAGE_DEFAULT;
    bd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    if (FAILED(s_dev->CreateBuffer(&bd, nullptr, s_cb.put())))
    {
        std::snprintf(s_init_error, sizeof(s_init_error), "creating the constant buffer failed");
        return false;
    }
    bd.ByteWidth = 8 * 16; // cbuffer Picture
    if (FAILED(s_dev->CreateBuffer(&bd, nullptr, s_cb_game.put())))
    {
        std::snprintf(s_init_error, sizeof(s_init_error), "creating the picture constant buffer failed");
        return false;
    }
    bd.ByteWidth = 16; // cbuffer Output (HDR pass)
    if (FAILED(s_dev->CreateBuffer(&bd, nullptr, s_cb_hdr.put())))
        s_cb_hdr = nullptr; // HDR stays off
    bd = {};
    bd.ByteWidth = kVbVerts * sizeof(Vtx);
    bd.Usage = D3D11_USAGE_DYNAMIC;
    bd.BindFlags = D3D11_BIND_VERTEX_BUFFER;
    bd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    if (FAILED(s_dev->CreateBuffer(&bd, nullptr, s_vb.put())))
    {
        std::snprintf(s_init_error, sizeof(s_init_error), "creating the vertex buffer failed");
        return false;
    }
    s_vb_pos = 0;

    const D3D11_FILTER filters[3] = {D3D11_FILTER_MIN_MAG_MIP_POINT, D3D11_FILTER_MIN_MAG_LINEAR_MIP_POINT,
                                     D3D11_FILTER_MIN_MAG_MIP_LINEAR};
    for (int i = 0; i < 3; ++i)
    {
        D3D11_SAMPLER_DESC sd = {};
        sd.Filter = filters[i];
        sd.AddressU = sd.AddressV = sd.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
        sd.ComparisonFunc = D3D11_COMPARISON_NEVER;
        sd.MaxLOD = D3D11_FLOAT32_MAX;
        if (FAILED(s_dev->CreateSamplerState(&sd, s_samp[i].put())))
        {
            std::snprintf(s_init_error, sizeof(s_init_error), "creating a sampler failed");
            return false;
        }
    }
    for (int i = 0; i < 2; ++i)
    {
        D3D11_BLEND_DESC bl = {};
        D3D11_RENDER_TARGET_BLEND_DESC& rt = bl.RenderTarget[0];
        rt.RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
        if (i == 0)
        {
            rt.BlendEnable = TRUE;
            rt.SrcBlend = D3D11_BLEND_SRC_ALPHA;
            rt.DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
            rt.BlendOp = D3D11_BLEND_OP_ADD;
            rt.SrcBlendAlpha = D3D11_BLEND_ONE;
            rt.DestBlendAlpha = D3D11_BLEND_INV_SRC_ALPHA;
            rt.BlendOpAlpha = D3D11_BLEND_OP_ADD;
        }
        if (FAILED(s_dev->CreateBlendState(&bl, s_blend[i].put())))
        {
            std::snprintf(s_init_error, sizeof(s_init_error), "creating a blend state failed");
            return false;
        }
    }
    D3D11_RASTERIZER_DESC rd = {};
    rd.FillMode = D3D11_FILL_SOLID;
    rd.CullMode = D3D11_CULL_NONE;
    rd.ScissorEnable = TRUE;
    rd.DepthClipEnable = TRUE;
    if (FAILED(s_dev->CreateRasterizerState(&rd, s_raster.put())))
    {
        std::snprintf(s_init_error, sizeof(s_init_error), "creating the rasterizer state failed");
        return false;
    }

    // 1x1 white: flat colour goes through the same pixel shader
    const u32 white = 0xffffffffu;
    D3D11_TEXTURE2D_DESC td = {};
    td.Width = td.Height = 1;
    td.MipLevels = td.ArraySize = 1;
    td.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    td.SampleDesc.Count = 1;
    td.Usage = D3D11_USAGE_IMMUTABLE;
    td.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    D3D11_SUBRESOURCE_DATA sr = {&white, 4, 0};
    winrt::com_ptr<ID3D11Texture2D> wt;
    if (FAILED(s_dev->CreateTexture2D(&td, &sr, wt.put())) || FAILED(s_dev->CreateShaderResourceView(wt.get(), nullptr, s_white.put())))
    {
        std::snprintf(s_init_error, sizeof(s_init_error), "creating the white texture failed");
        return false;
    }
    return true;
}

void ReleaseScene()
{
    s_scene_srv = nullptr;
    s_scene_rtv = nullptr;
    s_scene_tex = nullptr;
}

// The FP16 scene every draw goes to while the output is HDR (same size as the swap chain).
bool CreateScene()
{
    ReleaseScene();
    D3D11_TEXTURE2D_DESC td = {};
    td.Width = static_cast<UINT>(std::max(s_sw, 1));
    td.Height = static_cast<UINT>(std::max(s_sh, 1));
    td.MipLevels = td.ArraySize = 1;
    td.Format = DXGI_FORMAT_R16G16B16A16_FLOAT; // signed, > 1.0: highlights and colours outside BT.709 survive
    td.SampleDesc.Count = 1;
    td.Usage = D3D11_USAGE_DEFAULT;
    td.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
    if (FAILED(s_dev->CreateTexture2D(&td, nullptr, s_scene_tex.put())) ||
        FAILED(s_dev->CreateRenderTargetView(s_scene_tex.get(), nullptr, s_scene_rtv.put())) ||
        FAILED(s_dev->CreateShaderResourceView(s_scene_tex.get(), nullptr, s_scene_srv.put())))
    {
        ReleaseScene();
        return false;
    }
    return true;
}

// Where the frame is drawn: the swap chain in SDR, the scene in HDR.
inline ID3D11RenderTargetView* DrawRtv()
{
    return s_out != kOutSdr && s_scene_rtv ? s_scene_rtv.get() : s_rtv.get();
}

bool CreateTargets()
{
    s_rtv = nullptr;
    ReleaseScene();
    winrt::com_ptr<ID3D11Texture2D> bb;
    if (FAILED(s_sc->GetBuffer(0, __uuidof(ID3D11Texture2D), bb.put_void())))
        return false;
    if (FAILED(s_dev->CreateRenderTargetView(bb.get(), nullptr, s_rtv.put())))
        return false;
    DXGI_SWAP_CHAIN_DESC1 d = {};
    if (SUCCEEDED(s_sc->GetDesc1(&d)))
    {
        s_sw = static_cast<int>(d.Width);
        s_sh = static_cast<int>(d.Height);
    }
    UpdateTransform();
    if (s_out != kOutSdr && !CreateScene())
    {
        Log("plat: the HDR scene target (%dx%d FP16) could not be created", s_sw, s_sh);
        return false;
    }
    return true;
}

bool CreateSwapChain()
{
    winrt::com_ptr<IUnknown> window;
    int w, h;
    {
        std::lock_guard<std::mutex> lk(s_win_mutex);
        window = s_window;
        w = s_win_w;
        h = s_win_h;
        s_win_changed = false;
        s_size_changed = false;
    }
    if (!window)
    {
        std::snprintf(s_init_error, sizeof(s_init_error), "no CoreWindow: PlatD3D11_SetWindow was not called");
        return false;
    }
    winrt::com_ptr<IDXGIDevice1> dxgi_dev;
    if (FAILED(s_dev->QueryInterface(__uuidof(IDXGIDevice1), dxgi_dev.put_void())))
        return false;
    dxgi_dev->SetMaximumFrameLatency(1);
    winrt::com_ptr<IDXGIAdapter> adapter;
    if (FAILED(dxgi_dev->GetAdapter(adapter.put())))
        return false;
    winrt::com_ptr<IDXGIFactory2> factory;
    if (FAILED(adapter->GetParent(__uuidof(IDXGIFactory2), factory.put_void())))
        return false;

    DXGI_SWAP_CHAIN_DESC1 sd = {};
    sd.Width = static_cast<UINT>(std::max(w, 1));
    sd.Height = static_cast<UINT>(std::max(h, 1));
    sd.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    sd.SampleDesc.Count = 1;
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.BufferCount = 2;
    sd.Scaling = DXGI_SCALING_STRETCH;
    sd.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    sd.AlphaMode = DXGI_ALPHA_MODE_IGNORE;
    s_sc = nullptr;
    HRESULT hr = factory->CreateSwapChainForCoreWindow(s_dev.get(), window.get(), &sd, nullptr, s_sc.put());
    if (FAILED(hr))
    {
        std::snprintf(s_init_error, sizeof(s_init_error), "CreateSwapChainForCoreWindow failed: 0x%08X",
                      static_cast<unsigned>(hr));
        return false;
    }
    // A new swap chain always starts SDR; HdrUpdate (next plat_begin_frame) switches it to HDR when wanted.
    s_out = kOutSdr;
    s_hdr_swapchain_failed = false;
    s_hdr_dirty.store(true, std::memory_order_release);
    return CreateTargets();
}

void ReleaseGpuObjectsImpl()
{
    if (s_ctx)
    {
        s_ctx->ClearState();
        s_ctx->Flush();
    }
    s_rtv = nullptr;
    ReleaseScene();
    s_sc = nullptr;
    s_out = kOutSdr;
    s_vs_full = nullptr;
    s_ps_hdr = nullptr;
    s_cb_hdr = nullptr;
    s_white = nullptr;
    s_game_srv = nullptr;
    s_game_tex = nullptr;
    s_game_w = s_game_h = 0;
    s_raster = nullptr;
    for (auto& b : s_blend)
        b = nullptr;
    for (auto& s : s_samp)
        s = nullptr;
    s_vb = nullptr;
    s_cb = nullptr;
    s_cb_game = nullptr;
    s_layout = nullptr;
    s_ps = nullptr;
    s_ps_game = nullptr;
    s_vs = nullptr;
    {
        std::lock_guard<std::mutex> lk(s_tex_mutex);
        for (PlatTexture* t : s_live)
            t->srv = nullptr;
    }
    s_ctx = nullptr;
    s_dev = nullptr;
    s_b_srv = nullptr;
    s_verts.clear();
}

void ReleaseGpuObjects()
{
    std::lock_guard<std::mutex> lk(s_dev_mutex);
    ReleaseGpuObjectsImpl();
}

bool BuildGpu()
{
    std::lock_guard<std::mutex> lk(s_dev_mutex);
    s_init_error[0] = '\0';
    if (!CreateDeviceObjects() || !CreateSwapChain())
    {
        ReleaseGpuObjectsImpl();
        return false;
    }
    ++s_gen;
    s_vb_pos = 0;
    s_game_have = false; // the picture lives in the old device: wait for the next upload
    return true;
}

// ---- batching ----------------------------------------------------------------------------------------------------

void SetBaseState()
{
    if (!s_ctx || !s_rtv)
        return;
    const float cb[4] = {2.0f / static_cast<float>(s_sw), 2.0f / static_cast<float>(s_sh), 0.0f, 0.0f};
    s_ctx->UpdateSubresource(s_cb.get(), 0, nullptr, cb, 0, 0);
    ID3D11RenderTargetView* rtv = DrawRtv();
    s_ctx->OMSetRenderTargets(1, &rtv, nullptr);
    D3D11_VIEWPORT vp = {0.0f, 0.0f, static_cast<float>(s_sw), static_cast<float>(s_sh), 0.0f, 1.0f};
    s_ctx->RSSetViewports(1, &vp);
    s_ctx->RSSetState(s_raster.get());
    s_ctx->IASetInputLayout(s_layout.get());
    s_ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    s_ctx->VSSetShader(s_vs.get(), nullptr, 0);
    ID3D11Buffer* cbp = s_cb.get();
    s_ctx->VSSetConstantBuffers(0, 1, &cbp);
    s_ctx->PSSetShader(s_ps.get(), nullptr, 0);
}

void Flush()
{
    if (s_verts.empty())
        return;
    if (!s_ctx || !s_rtv || !s_b_srv || s_b_clip.empty)
    {
        s_verts.clear();
        return;
    }
    ID3D11ShaderResourceView* srv = s_b_srv;
    s_ctx->PSSetShaderResources(0, 1, &srv);
    ID3D11SamplerState* smp = s_samp[s_b_samp].get();
    s_ctx->PSSetSamplers(0, 1, &smp);
    s_ctx->OMSetBlendState(s_blend[s_b_opaque ? 1 : 0].get(), nullptr, 0xffffffffu);
    const D3D11_RECT sc = {s_b_clip.x0, s_b_clip.y0, s_b_clip.x1, s_b_clip.y1};
    s_ctx->RSSetScissorRects(1, &sc);

    size_t done = 0;
    const size_t total = s_verts.size();
    while (done < total)
    {
        const u32 chunk = static_cast<u32>(std::min<size_t>(total - done, kVbVerts));
        D3D11_MAP mode = D3D11_MAP_WRITE_NO_OVERWRITE;
        if (s_vb_pos + chunk > kVbVerts)
        {
            s_vb_pos = 0;
            mode = D3D11_MAP_WRITE_DISCARD;
        }
        else if (s_vb_pos == 0)
        {
            mode = D3D11_MAP_WRITE_DISCARD;
        }
        D3D11_MAPPED_SUBRESOURCE m = {};
        if (FAILED(s_ctx->Map(s_vb.get(), 0, mode, 0, &m)))
            break;
        std::memcpy(static_cast<Vtx*>(m.pData) + s_vb_pos, s_verts.data() + done, chunk * sizeof(Vtx));
        s_ctx->Unmap(s_vb.get(), 0);
        ID3D11Buffer* vb = s_vb.get();
        const UINT stride = sizeof(Vtx), offset = 0;
        s_ctx->IASetVertexBuffers(0, 1, &vb, &stride, &offset);
        s_ctx->Draw(chunk, s_vb_pos);
        s_vb_pos += chunk;
        done += chunk;
    }
    s_verts.clear();
}

bool SameClip(const ClipRect& a, const ClipRect& b)
{
    return a.x0 == b.x0 && a.y0 == b.y0 && a.x1 == b.x1 && a.y1 == b.y1 && a.empty == b.empty;
}

// Makes the batch state match this draw; flushes the previous batch when anything differs.
void Select(ID3D11ShaderResourceView* srv, int samp, bool opaque)
{
    if (s_b_srv != srv || s_b_samp != samp || s_b_opaque != opaque || !SameClip(s_b_clip, s_clip))
    {
        Flush();
        s_b_srv = srv;
        s_b_samp = samp;
        s_b_opaque = opaque;
        s_b_clip = s_clip;
    }
}

inline void PushTri(const Vtx& a, const Vtx& b, const Vtx& c)
{
    s_verts.push_back(a);
    s_verts.push_back(b);
    s_verts.push_back(c);
}

// A quad in logical coordinates (two triangles).
void QuadSrv(ID3D11ShaderResourceView* srv, int samp, bool opaque, float x0, float y0, float x1, float y1, float u0,
             float v0, float u1, float v1, u32 argb)
{
    if (!s_in_frame || !srv || s_clip.empty)
        return;
    Select(srv, samp, opaque);
    const float px0 = TX(x0), py0 = TY(y0), px1 = TX(x1), py1 = TY(y1);
    const Vtx a = {px0, py0, u0, v0, argb};
    const Vtx b = {px1, py0, u1, v0, argb};
    const Vtx c = {px1, py1, u1, v1, argb};
    const Vtx d = {px0, py1, u0, v1, argb};
    PushTri(a, b, c);
    PushTri(a, c, d);
}

// ---- textures ----------------------------------------------------------------------------------------------------

int SampOf(const PlatTexture* t)
{
    return t->mips ? 2 : (t->smooth ? 1 : 0);
}

// Next mip level, 2x2 box filter weighted by alpha so transparent texels do not bleed their colour.
void BuildNextLevel(const std::vector<u8>& src, int sw, int sh, std::vector<u8>& dst, int& dw, int& dh)
{
    dw = std::max(1, sw / 2);
    dh = std::max(1, sh / 2);
    dst.assign(static_cast<size_t>(dw) * static_cast<size_t>(dh) * 4, 0);
    for (int y = 0; y < dh; ++y)
    {
        const int y0 = std::min(y * 2, sh - 1), y1 = std::min(y * 2 + 1, sh - 1);
        for (int x = 0; x < dw; ++x)
        {
            const int x0 = std::min(x * 2, sw - 1), x1 = std::min(x * 2 + 1, sw - 1);
            const u8* p[4] = {&src[(static_cast<size_t>(y0) * sw + x0) * 4], &src[(static_cast<size_t>(y0) * sw + x1) * 4],
                              &src[(static_cast<size_t>(y1) * sw + x0) * 4], &src[(static_cast<size_t>(y1) * sw + x1) * 4]};
            u32 a = 0, r = 0, g = 0, b = 0;
            for (int k = 0; k < 4; ++k)
            {
                a += p[k][3];
                r += p[k][0] * p[k][3];
                g += p[k][1] * p[k][3];
                b += p[k][2] * p[k][3];
            }
            u8* o = &dst[(static_cast<size_t>(y) * dw + x) * 4];
            if (a)
            {
                o[0] = static_cast<u8>(r / a);
                o[1] = static_cast<u8>(g / a);
                o[2] = static_cast<u8>(b / a);
            }
            else
            {
                u32 rr = 0, gg = 0, bb = 0;
                for (int k = 0; k < 4; ++k)
                {
                    rr += p[k][0];
                    gg += p[k][1];
                    bb += p[k][2];
                }
                o[0] = static_cast<u8>(rr / 4);
                o[1] = static_cast<u8>(gg / 4);
                o[2] = static_cast<u8>(bb / 4);
            }
            o[3] = static_cast<u8>((a + 2) / 4);
        }
    }
}

bool EnsureUploaded(PlatTexture* t)
{
    if (t->srv && t->gen == s_gen)
        return true;
    if (t->failed_gen == s_gen || !s_dev)
        return false;
    t->srv = nullptr;
    if (t->width <= 0 || t->height <= 0 || t->width > 16384 || t->height > 16384 || t->rgba.size() < static_cast<size_t>(t->width) * t->height * 4)
    {
        t->failed_gen = s_gen;
        return false;
    }
    std::vector<std::vector<u8>> levels;
    std::vector<int> lw, lh;
    int levelCount = 1;
    if (t->mips)
    {
        int m = std::max(t->width, t->height);
        while (m > 1)
        {
            m >>= 1;
            ++levelCount;
        }
    }
    lw.push_back(t->width);
    lh.push_back(t->height);
    for (int i = 1; i < levelCount; ++i)
    {
        std::vector<u8> next;
        int nw, nh;
        BuildNextLevel(i == 1 ? t->rgba : levels[static_cast<size_t>(i) - 2], lw.back(), lh.back(), next, nw, nh);
        levels.push_back(std::move(next));
        lw.push_back(nw);
        lh.push_back(nh);
    }
    // levels[i-1] holds level i
    std::vector<D3D11_SUBRESOURCE_DATA> sr(static_cast<size_t>(levelCount));
    for (int i = 0; i < levelCount; ++i)
    {
        sr[static_cast<size_t>(i)].pSysMem = i == 0 ? t->rgba.data() : levels[static_cast<size_t>(i) - 1].data();
        sr[static_cast<size_t>(i)].SysMemPitch = static_cast<UINT>(lw[static_cast<size_t>(i)] * 4);
    }
    D3D11_TEXTURE2D_DESC td = {};
    td.Width = static_cast<UINT>(t->width);
    td.Height = static_cast<UINT>(t->height);
    td.MipLevels = static_cast<UINT>(levelCount);
    td.ArraySize = 1;
    td.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    td.SampleDesc.Count = 1;
    td.Usage = D3D11_USAGE_IMMUTABLE;
    td.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    winrt::com_ptr<ID3D11Texture2D> tex;
    if (FAILED(s_dev->CreateTexture2D(&td, sr.data(), tex.put())) ||
        FAILED(s_dev->CreateShaderResourceView(tex.get(), nullptr, t->srv.put())))
    {
        t->srv = nullptr;
        t->failed_gen = s_gen;
        Log("plat: GPU texture %dx%d could not be created", t->width, t->height);
        return false;
    }
    t->gen = s_gen;
    return true;
}

PlatTexture* TextureCreate(const u8* rgba, int width, int height, bool smooth, bool mips)
{
    if (!rgba || width <= 0 || height <= 0)
        return nullptr;
    PlatTexture* t = new (std::nothrow) PlatTexture;
    if (!t)
        return nullptr;
    t->width = width;
    t->height = height;
    t->smooth = smooth;
    t->mips = mips;
    try
    {
        t->rgba.assign(rgba, rgba + static_cast<size_t>(width) * static_cast<size_t>(height) * 4);
    }
    catch (...)
    {
        delete t;
        return nullptr;
    }
    std::lock_guard<std::mutex> lk(s_tex_mutex);
    s_live.insert(t);
    return t;
}

void DrainFrees()
{
    std::vector<PlatTexture*> q;
    {
        std::lock_guard<std::mutex> lk(s_tex_mutex);
        q.swap(s_free_queue);
        for (PlatTexture* t : q)
            s_live.erase(t);
    }
    for (PlatTexture* t : q)
        delete t;
}

// ---- the game picture ---------------------------------------------------------------------------------------------

bool EnsureGameTexture(int w, int h)
{
    if (s_game_tex && s_game_gen == s_gen && s_game_w == w && s_game_h == h)
        return true;
    s_game_srv = nullptr;
    s_game_tex = nullptr;
    if (!s_dev || w <= 0 || h <= 0 || w > 16384 || h > 16384)
        return false;
    D3D11_TEXTURE2D_DESC td = {};
    td.Width = static_cast<UINT>(w);
    td.Height = static_cast<UINT>(h);
    td.MipLevels = td.ArraySize = 1;
    td.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    td.SampleDesc.Count = 1;
    td.Usage = D3D11_USAGE_DYNAMIC;
    td.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    td.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    if (FAILED(s_dev->CreateTexture2D(&td, nullptr, s_game_tex.put())) ||
        FAILED(s_dev->CreateShaderResourceView(s_game_tex.get(), nullptr, s_game_srv.put())))
    {
        s_game_tex = nullptr;
        s_game_srv = nullptr;
        return false;
    }
    s_game_w = w;
    s_game_h = h;
    s_game_gen = s_gen;
    return true;
}

// REF plat_sdl.c:1609-1646 in logical units: the picture's rectangle.
void GameRect(const Settings* settings, float display_aspect, int lines, int& dx, int& dy, int& dw, int& dh)
{
    const int out_w = kCanvasW, out_h = kCanvasH;
    const float aspect = display_aspect > 0.0f ? display_aspect : 4.0f / 3.0f;
    dh = out_h;
    dw = static_cast<int>(static_cast<float>(out_h) * aspect + 0.5f);
    if (dw > out_w)
    {
        dw = out_w;
        dh = static_cast<int>(static_cast<float>(out_w) / aspect + 0.5f);
    }
    if (settings->integer_scale)
    {
        int k = dh / (lines > 0 ? lines : 240);
        if (k < 1)
            k = 1;
        dh = lines * k;
        dw = static_cast<int>(static_cast<float>(dh) * aspect + 0.5f);
        if (dw > out_w)
            dw = out_w;
    }
    if (settings->border == 2)
    {
        dw = dw * 86 / 100; // TV frame: leave room for the TV around the picture
        dh = dh * 86 / 100;
    }
    dx = (out_w - dw) / 2;
    dy = (out_h - dh) / 2;
    if (settings->manual_crop)
    {
        const int mw = settings->mc_w < 25 ? 25 : settings->mc_w > 150 ? 150 : settings->mc_w;
        const int mh = settings->mc_h < 25 ? 25 : settings->mc_h > 150 ? 150 : settings->mc_h;
        dw = dw * mw / 100;
        dh = dh * mh / 100;
        dx = (out_w - dw) / 2 + settings->mc_x * out_w / 1000;
        dy = (out_h - dh) / 2 + settings->mc_y * out_h / 1000;
    }
}

// ---- input helpers -----------------------------------------------------------------------------------------------

int Physical(int port)
{
    if (port < 0 || port >= kMaxPads)
        return -1;
    const int i = s_player_order[port];
    return (i >= 0 && i < kMaxPads) ? i : -1;
}

void SetVibration(int i, u32 strong, u32 weak)
{
    XINPUT_VIBRATION v = {};
    v.wLeftMotorSpeed = static_cast<WORD>(strong);
    v.wRightMotorSpeed = static_cast<WORD>(weak);
    XInputSetState(static_cast<DWORD>(i), &v);
}

void RumbleWatchdog(u64 now)
{
    for (int i = 0; i < kMaxPads; ++i)
    {
        if ((s_rumble_strong[i] || s_rumble_weak[i]) && now - s_rumble_at[i] > kRumbleWatchdogUs)
        {
            s_rumble_strong[i] = s_rumble_weak[i] = 0;
            if (s_connected[i])
                SetVibration(i, 0, 0);
        }
    }
}

void NotePresent()
{
    const u64 now = NowUs();
    if (s_present_last_us)
    {
        const double dt = static_cast<double>(now - s_present_last_us);
        if (dt > 3000.0 && dt < 250000.0)
        {
            s_present_ema_us = s_present_samples ? s_present_ema_us * 0.95 + dt * 0.05 : dt;
            if (s_present_samples < 1000)
                ++s_present_samples;
        }
    }
    s_present_last_us = now;
}

// ---- HDR: display capabilities and the Xbox HDMI mode (UI thread / async completions) ----------------------------

namespace wf = winrt::Windows::Foundation;
namespace wgd = winrt::Windows::Graphics::Display;
namespace wgdc = winrt::Windows::Graphics::Display::Core;
namespace wuc = winrt::Windows::UI::Core;

// DisplayInformation.GetAdvancedColorInfo (Windows 10 1803+): is the display in HDR now, could it be, its luminance.
void RefreshOsCaps(const wgd::DisplayInformation& display)
{
    if (!display)
        return;
    try
    {
        const wgd::AdvancedColorInfo info = display.GetAdvancedColorInfo();
        const bool on = info.CurrentAdvancedColorKind() == wgd::AdvancedColorKind::HighDynamicRange;
        bool avail = on;
        try
        {
            avail = avail || info.IsAdvancedColorKindAvailable(wgd::AdvancedColorKind::HighDynamicRange); // 2004+
        }
        catch (...)
        {
        }
        const int max_nits = static_cast<int>(info.MaxLuminanceInNits() + 0.5f);
        const int sdr_white = static_cast<int>(info.SdrWhiteLevelInNits() + 0.5f);
        const bool changed = !s_cap_attached.load() || on != s_cap_os_hdr_on.load() || avail != s_cap_os_hdr_avail.load() ||
                             max_nits != s_cap_max_nits.load();
        s_cap_os_hdr_on.store(on);
        s_cap_os_hdr_avail.store(avail);
        s_cap_max_nits.store(max_nits);
        s_cap_sdr_white.store(sdr_white);
        s_cap_attached.store(true);
        if (changed)
            HdrNote("advanced colour: kind %d (HDR %s, HDR available %s), peak %d nits, SDR white %d nits",
                    static_cast<int>(info.CurrentAdvancedColorKind()), on ? "on" : "off", avail ? "yes" : "no", max_nits,
                    sdr_white);
    }
    catch (...)
    {
    }
    s_hdr_dirty.store(true, std::memory_order_release);
}

// Xbox: is there an HDR10 (SMPTE ST 2084) HDMI mode at the current resolution (and refresh rate)?
void RefreshXboxCaps(const wgdc::HdmiDisplayInformation& hdmi)
{
    if (!hdmi)
        return;
    bool exact = false, res = false;
    try
    {
        const wgdc::HdmiDisplayMode cur = hdmi.GetCurrentDisplayMode();
        if (cur)
        {
            const uint32_t w = cur.ResolutionWidthInRawPixels(), h = cur.ResolutionHeightInRawPixels();
            const double hz = cur.RefreshRate();
            int total = 0, hdr = 0;
            for (const wgdc::HdmiDisplayMode& m : hdmi.GetSupportedDisplayModes())
            {
                ++total;
                if (!m.IsSmpte2084Supported() || m.StereoEnabled())
                    continue;
                ++hdr;
                if (m.ResolutionWidthInRawPixels() != w || m.ResolutionHeightInRawPixels() != h)
                    continue;
                res = true;
                if (std::fabs(m.RefreshRate() - hz) < 0.01)
                    exact = true;
            }
            HdrNote("HDMI: current %ux%u %.3f Hz %u bpp (HDR10 capable mode: %s); %d modes, %d with HDR10; HDR10 here: "
                    "%s, at another refresh rate: %s",
                    w, h, hz, static_cast<unsigned>(cur.BitsPerPixel()), cur.IsSmpte2084Supported() ? "yes" : "no", total,
                    hdr, exact ? "yes" : "no", res && !exact ? "yes" : "no");
        }
    }
    catch (...)
    {
        HdrNote("HDMI: the display modes could not be read");
    }
    s_cap_hdr10_exact.store(exact);
    s_cap_hdr10_res.store(res);
    s_hdr_dirty.store(true, std::memory_order_release);
}

// The HDR10 mode to request: the current resolution, the current refresh rate (or the nearest one when `relaxed`),
// preferring BT.2020 colour and 10+ bits.
wgdc::HdmiDisplayMode PickHdrMode(const wgdc::HdmiDisplayInformation& hdmi, bool relaxed)
{
    wgdc::HdmiDisplayMode best{nullptr};
    const wgdc::HdmiDisplayMode cur = hdmi.GetCurrentDisplayMode();
    if (!cur)
        return best;
    const uint32_t w = cur.ResolutionWidthInRawPixels(), h = cur.ResolutionHeightInRawPixels();
    const double hz = cur.RefreshRate();
    double best_score = 1e30;
    for (const wgdc::HdmiDisplayMode& m : hdmi.GetSupportedDisplayModes())
    {
        if (!m.IsSmpte2084Supported() || m.StereoEnabled() || m.ResolutionWidthInRawPixels() != w ||
            m.ResolutionHeightInRawPixels() != h)
            continue;
        const double dhz = std::fabs(m.RefreshRate() - hz);
        if (!relaxed && dhz >= 0.01)
            continue;
        double score = dhz * 100.0;
        if (m.ColorSpace() != wgdc::HdmiDisplayColorSpace::BT2020)
            score += 1.0;
        if (m.BitsPerPixel() < 30)
            score += 0.5;
        if (score < best_score)
        {
            best_score = score;
            best = m;
        }
    }
    return best;
}

wgdc::HdmiDisplayInformation UiHdmi()
{
    std::lock_guard<std::mutex> lk(s_ui_mutex);
    return s_ui ? s_ui->hdmi : nullptr;
}

// UI thread (posted through the CoreDispatcher by XboxPost). want: 0 default (SDR) mode, 1 HDR10 at the current
// refresh rate, 2 HDR10 at the nearest refresh rate. `gen` is s_xbox_gen when it was posted.
void XboxApplyOnUi(int want, u32 gen)
{
    if (gen != s_xbox_gen.load())
    {
        s_hdr_dirty.store(true); // posted before a suspend / exit: that already put the TV back
        return;
    }
    const wgdc::HdmiDisplayInformation hdmi = UiHdmi();
    if (!hdmi)
    {
        s_xbox_mode.store(want ? 3 : 0);
        s_hdr_dirty.store(true);
        return;
    }
    try
    {
        if (!want)
        {
            HdrNote("HDMI: SetDefaultDisplayModeAsync");
            hdmi.SetDefaultDisplayModeAsync().Completed([](const wf::IAsyncAction&, wf::AsyncStatus status) {
                int restoring = 4;
                s_xbox_mode.compare_exchange_strong(restoring, 0);
                s_hdr_dirty.store(true);
                HdrNote("HDMI: default mode %s", status == wf::AsyncStatus::Completed ? "restored" : "request failed");
            });
            return;
        }
        const wgdc::HdmiDisplayMode mode = PickHdrMode(hdmi, want == 2);
        if (!mode)
        {
            s_xbox_mode.store(3);
            s_hdr_dirty.store(true);
            HdrNote("HDMI: no HDR10 mode at the current resolution%s", want == 2 ? "" : " and refresh rate");
            return;
        }
        HdrNote("HDMI: RequestSetCurrentDisplayModeAsync(%ux%u %.3f Hz, %u bpp, colour space %d, Eotf2084)",
                mode.ResolutionWidthInRawPixels(), mode.ResolutionHeightInRawPixels(), mode.RefreshRate(),
                static_cast<unsigned>(mode.BitsPerPixel()), static_cast<int>(mode.ColorSpace()));
        hdmi.RequestSetCurrentDisplayModeAsync(mode, wgdc::HdmiDisplayHdrOption::Eotf2084)
            .Completed([gen, hdmi](const wf::IAsyncOperation<bool>& op, wf::AsyncStatus status) {
                bool ok = false;
                if (status == wf::AsyncStatus::Completed)
                {
                    try
                    {
                        ok = op.GetResults();
                    }
                    catch (...)
                    {
                    }
                }
                if (gen != s_xbox_gen.load())
                {
                    // suspended / exiting while the TV switched: undo it
                    if (ok)
                    {
                        try
                        {
                            hdmi.SetDefaultDisplayModeAsync();
                        }
                        catch (...)
                        {
                        }
                    }
                    s_hdr_dirty.store(true);
                    return;
                }
                s_xbox_mode.store(ok ? 2 : 3);
                s_hdr_dirty.store(true);
                HdrNote("HDMI: HDR10 mode %s", ok ? "set" : "refused");
            });
    }
    catch (const winrt::hresult_error& e)
    {
        s_xbox_mode.store(want ? 3 : 0);
        s_hdr_dirty.store(true);
        HdrNote("HDMI: request failed (0x%08X)", static_cast<unsigned>(e.code().value));
    }
    catch (...)
    {
        s_xbox_mode.store(want ? 3 : 0);
        s_hdr_dirty.store(true);
    }
}

// Frontend thread: runs XboxApplyOnUi on the UI thread. Only called while no request is in flight.
bool XboxPost(int want)
{
    wuc::CoreDispatcher dispatcher{nullptr};
    {
        std::lock_guard<std::mutex> lk(s_ui_mutex);
        if (s_ui)
            dispatcher = s_ui->dispatcher;
    }
    if (!dispatcher)
        return false;
    const u32 gen = s_xbox_gen.load();
    const int before = s_xbox_mode.exchange(want ? 1 : 4);
    try
    {
        dispatcher.RunAsync(wuc::CoreDispatcherPriority::Normal, [want, gen]() { XboxApplyOnUi(want, gen); });
    }
    catch (...)
    {
        s_xbox_mode.store(before);
        return false;
    }
    return true;
}

// UI thread (suspend / exit): put the TV back in its default mode if we changed it, waiting up to `timeout_ms`. The
// completion runs on a background thread, so waiting here cannot deadlock (and it is bounded anyway).
void XboxRestoreBlocking(DWORD timeout_ms)
{
    s_xbox_gen.fetch_add(1); // requests still in flight are stale from now on (their completion undoes a late switch)
    const int m = s_xbox_mode.exchange(0);
    if (m == 0 || m == 3)
        return; // nothing of ours on the TV
    const wgdc::HdmiDisplayInformation hdmi = UiHdmi();
    if (!hdmi)
        return;
    HANDLE raw = CreateEventExW(nullptr, nullptr, CREATE_EVENT_MANUAL_RESET, EVENT_ALL_ACCESS);
    std::shared_ptr<void> ev(raw, [](void* h) {
        if (h)
            CloseHandle(h);
    });
    try
    {
        hdmi.SetDefaultDisplayModeAsync().Completed([ev](const wf::IAsyncAction&, wf::AsyncStatus) {
            if (ev)
                SetEvent(ev.get());
        });
        if (ev)
        {
            const DWORD r = WaitForSingleObjectEx(ev.get(), timeout_ms, FALSE);
            HdrNote("HDMI: default mode restored (%s)", r == WAIT_OBJECT_0 ? "done" : "still switching");
        }
    }
    catch (...)
    {
        HdrNote("HDMI: SetDefaultDisplayModeAsync failed");
    }
}

void DetachUi(HdrUi* ui)
{
    if (!ui)
        return;
    try
    {
        if (ui->display && ui->colour_token.value)
            ui->display.AdvancedColorInfoChanged(ui->colour_token);
    }
    catch (...)
    {
    }
    try
    {
        if (ui->hdmi && ui->modes_token.value)
            ui->hdmi.DisplayModesChanged(ui->modes_token);
    }
    catch (...)
    {
    }
    delete ui;
}

// ---- HDR: the swap chain (frontend thread) -----------------------------------------------------------------------

void FlushNotes()
{
    if (!s_have_notes.exchange(false, std::memory_order_acq_rel))
        return;
    std::vector<std::string> q;
    {
        std::lock_guard<std::mutex> lk(s_note_mutex);
        q.swap(s_notes);
    }
    for (const std::string& s : q)
        Log("plat: %s", s.c_str());
}

// The output the swap chain is on (PC): IDXGIOutput6::GetDesc1 tells the colour space Windows drives it in.
bool DxgiOutputDesc(DXGI_OUTPUT_DESC1* desc)
{
    if (!s_sc || !s_dev)
        return false;
    winrt::com_ptr<IDXGIOutput> out;
    if (FAILED(s_sc->GetContainingOutput(out.put())))
    {
        out = nullptr;
        winrt::com_ptr<IDXGIDevice> dxgi;
        winrt::com_ptr<IDXGIAdapter> adapter;
        if (FAILED(s_dev->QueryInterface(__uuidof(IDXGIDevice), dxgi.put_void())) || FAILED(dxgi->GetAdapter(adapter.put())) ||
            FAILED(adapter->EnumOutputs(0, out.put())))
            return false;
    }
    winrt::com_ptr<IDXGIOutput6> o6;
    if (FAILED(out->QueryInterface(__uuidof(IDXGIOutput6), o6.put_void())))
        return false;
    return SUCCEEDED(o6->GetDesc1(desc));
}

// HDR10 static metadata (SMPTE ST 2086 / CTA-861.3) for the PQ swap chain: BT.2020 primaries, D65, the user's peak.
void SetMetadata()
{
    winrt::com_ptr<IDXGISwapChain4> sc4;
    if (!s_sc || FAILED(s_sc->QueryInterface(__uuidof(IDXGISwapChain4), sc4.put_void())))
        return;
    if (s_out == kOutPq)
    {
        DXGI_HDR_METADATA_HDR10 md = {};
        md.RedPrimary[0] = 35400; // chromaticity x 50000: 0.708, 0.292
        md.RedPrimary[1] = 14600;
        md.GreenPrimary[0] = 8500; // 0.170, 0.797
        md.GreenPrimary[1] = 39850;
        md.BluePrimary[0] = 6550; // 0.131, 0.046
        md.BluePrimary[1] = 2300;
        md.WhitePoint[0] = 15635; // D65 0.3127, 0.3290
        md.WhitePoint[1] = 16450;
        md.MaxMasteringLuminance = static_cast<UINT>(s_hdr_peak); // nits
        md.MinMasteringLuminance = 10;                            // 0.0001 nit units: 0.001 nit
        md.MaxContentLightLevel = static_cast<UINT16>(s_hdr_peak);
        md.MaxFrameAverageLightLevel = static_cast<UINT16>(std::min(s_hdr_peak, s_hdr_paper * 3 / 2));
        sc4->SetHDRMetaData(DXGI_HDR_METADATA_TYPE_HDR10, sizeof(md), &md);
    }
    else
    {
        sc4->SetHDRMetaData(DXGI_HDR_METADATA_TYPE_NONE, 0, nullptr);
    }
    s_meta_peak = s_hdr_peak;
    s_meta_paper = s_hdr_paper;
}

// Re-formats the swap chain for `want` (kOutPq = "HDR": HDR10, else scRGB, else SDR; kOutSdr = SDR). ResizeBuffers
// can change the format of a flip-model swap chain; the colour space is checked (CheckColorSpaceSupport) and set
// (SetColorSpace1) after it. False when not even SDR could be set up (the caller rebuilds the device).
bool ApplyOutput(int want)
{
    struct Option
    {
        int kind;
        DXGI_FORMAT format;
        DXGI_COLOR_SPACE_TYPE space;
    };
    static const Option kOptions[3] = {
        {kOutPq, DXGI_FORMAT_R10G10B10A2_UNORM, DXGI_COLOR_SPACE_RGB_FULL_G2084_NONE_P2020},
        {kOutScrgb, DXGI_FORMAT_R16G16B16A16_FLOAT, DXGI_COLOR_SPACE_RGB_FULL_G10_NONE_P709},
        {kOutSdr, DXGI_FORMAT_B8G8R8A8_UNORM, DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P709},
    };
    winrt::com_ptr<IDXGISwapChain3> sc3;
    if (FAILED(s_sc->QueryInterface(__uuidof(IDXGISwapChain3), sc3.put_void())))
        sc3 = nullptr;
    const int first = (want != kOutSdr && sc3) ? 0 : 2;

    s_ctx->OMSetRenderTargets(0, nullptr, nullptr);
    ID3D11ShaderResourceView* none = nullptr;
    s_ctx->PSSetShaderResources(0, 1, &none);
    for (int i = first; i < 3; ++i)
    {
        const Option& o = kOptions[i];
        s_rtv = nullptr;
        ReleaseScene();
        s_ctx->Flush();
        HRESULT hr = s_sc->ResizeBuffers(0, static_cast<UINT>(s_sw), static_cast<UINT>(s_sh), o.format, 0);
        if (FAILED(hr))
        {
            Log("plat: ResizeBuffers to %s failed (0x%08X)", kOutName[o.kind], static_cast<unsigned>(hr));
            continue;
        }
        if (sc3)
        {
            UINT support = 0;
            const bool can = SUCCEEDED(sc3->CheckColorSpaceSupport(o.space, &support)) &&
                             (support & DXGI_SWAP_CHAIN_COLOR_SPACE_SUPPORT_FLAG_PRESENT) != 0;
            hr = can ? sc3->SetColorSpace1(o.space) : E_FAIL;
            if (FAILED(hr) && o.kind != kOutSdr)
            {
                Log("plat: the swap chain cannot present %s (colour space %d %s)", kOutName[o.kind], static_cast<int>(o.space),
                    can ? "not set" : "unsupported");
                continue;
            }
        }
        s_out = o.kind;
        if (!CreateTargets())
        {
            s_out = kOutSdr;
            continue;
        }
        SetMetadata();
        return true;
    }
    s_out = kOutSdr;
    return false;
}

// Re-applies the colour space after a plain resize (ResizeBuffers with DXGI_FORMAT_UNKNOWN keeps the format).
void ReapplyColourSpace()
{
    if (s_out == kOutSdr || !s_sc)
        return;
    winrt::com_ptr<IDXGISwapChain3> sc3;
    if (FAILED(s_sc->QueryInterface(__uuidof(IDXGISwapChain3), sc3.put_void())))
        return;
    sc3->SetColorSpace1(s_out == kOutPq ? DXGI_COLOR_SPACE_RGB_FULL_G2084_NONE_P2020 : DXGI_COLOR_SPACE_RGB_FULL_G10_NONE_P709);
}

// Frontend thread, plat_begin_frame: decides SDR / HDR from the settings and the display, drives the Xbox HDMI mode
// and re-formats the swap chain when the decision changes. Runs only when something changed (s_hdr_dirty).
void HdrUpdate()
{
    if (!s_hdr_dirty.exchange(false, std::memory_order_acq_rel))
        return;
    if (!s_sc || !s_ctx)
    {
        s_hdr_dirty.store(true);
        return;
    }
    const bool can_draw = s_vs_full && s_ps_hdr && s_cb_hdr;
    bool capable = false, hdr_now = false;
    char detail[320];
    if (s_cap_xbox.load())
    {
        const bool exact = s_cap_hdr10_exact.load(), res = s_cap_hdr10_res.load();
        capable = exact || res;
        if (s_xbox_repost.exchange(false))
            s_xbox_posted = -1;
        const int m = s_xbox_mode.load();
        int want = 0; // 0 SDR, 1 HDR10 here, 2 HDR10 at another refresh rate ("on" only)
        if (s_hdr_mode != 0 && can_draw && !s_hdr_swapchain_failed)
        {
            if (m == 2)
                want = 1; // already in our HDR10 mode: keep it (the mode list re-read after the switch may differ)
            else
                want = exact ? 1 : (s_hdr_mode == 2 && res ? 2 : 0);
        }
        if (!s_hdr_suspended.load())
        {
            if (want == 0)
            {
                if (m == 2 && XboxPost(0))
                    s_xbox_posted = 0;
            }
            else if ((m == 0 || m == 3) && s_xbox_posted != want)
            {
                if (XboxPost(want))
                    s_xbox_posted = want;
            }
        }
        const int now = s_xbox_mode.load();
        hdr_now = (now == 2);
        static const char* const kModeName[5] = {"default", "switching to HDR10", "HDR10 (ours)", "HDR10 refused",
                                                 "restoring default"};
        std::snprintf(detail, sizeof(detail), "Xbox HDMI: HDR10 at this mode %s, at this resolution %s; TV mode: %s",
                      exact ? "yes" : "no", res ? "yes" : "no", kModeName[now >= 0 && now < 5 ? now : 0]);
    }
    else
    {
        DXGI_OUTPUT_DESC1 d1 = {};
        const bool have_d1 = DxgiOutputDesc(&d1);
        const bool dxgi_hdr = have_d1 && d1.ColorSpace == DXGI_COLOR_SPACE_RGB_FULL_G2084_NONE_P2020;
        if (s_cap_attached.load())
        {
            hdr_now = s_cap_os_hdr_on.load();
            capable = hdr_now || s_cap_os_hdr_avail.load();
        }
        else
        {
            hdr_now = dxgi_hdr; // no DisplayInformation (PlatD3D11_HdrAttach not called): DXGI alone
            capable = dxgi_hdr;
        }
        std::snprintf(detail, sizeof(detail),
                      "Windows: HDR %s, HDR capable %s, display peak %d nits, SDR white %d nits; DXGI output: %s, %u bits, "
                      "%.0f nits%s",
                      hdr_now ? "on" : "off", capable ? "yes" : "no", s_cap_max_nits.load(), s_cap_sdr_white.load(),
                      !have_d1 ? "unknown" : dxgi_hdr ? "BT.2020 PQ" : "sRGB", have_d1 ? d1.BitsPerColor : 0u,
                      have_d1 ? static_cast<double>(d1.MaxLuminance) : 0.0,
                      s_hdr_test ? "; hdr-test.txt: HDR forced" : "");
        if (s_hdr_test)
            hdr_now = capable = true; // developer switch: run the HDR path on an SDR display (DWM converts it)
    }

    const int target = (s_hdr_mode != 0 && hdr_now && can_draw && !s_hdr_swapchain_failed) ? kOutPq : kOutSdr;
    if ((target != kOutSdr) != (s_out != kOutSdr))
    {
        if (!ApplyOutput(target))
        {
            Log("plat: the swap chain could not be re-formatted; rebuilding the device");
            s_need_device = true;
        }
        else if (target != kOutSdr && s_out == kOutSdr)
        {
            Log("plat: no HDR swap chain format works here; staying SDR");
            s_hdr_swapchain_failed = true;
            s_hdr_dirty.store(true); // the Xbox lets go of the HDR10 mode it no longer needs
        }
        SetBaseState();
    }
    else if (s_out == kOutPq && (s_meta_peak != s_hdr_peak || s_meta_paper != s_hdr_paper))
    {
        SetMetadata();
    }

    const int state = s_out != kOutSdr ? 2 : (capable ? 1 : 0);
    s_hdr_state.store(state, std::memory_order_release);
    static const char* const kStateName[3] = {"display not HDR capable", "HDR capable, SDR now", "HDR active"};
    char line[640];
    std::snprintf(line, sizeof(line), "plat: HDR %s - output %s; setting %s, peak %d, paper white %d, game boost %d; %s",
                  kStateName[state], kOutName[s_out], s_hdr_mode == 0 ? "off" : s_hdr_mode == 1 ? "auto" : "on", s_hdr_peak,
                  s_hdr_paper, s_hdr_boost, detail);
    if (state != s_hdr_logged_state || s_hdr_detail != line)
    {
        Log("%s", line);
        s_hdr_logged_state = state;
        s_hdr_detail = line;
    }
}

// HDR: the FP16 scene -> the swap chain (PQ + dither, or scRGB), one full-screen triangle.
void Composite()
{
    if (s_out == kOutSdr || !s_scene_srv || !s_rtv || !s_vs_full || !s_ps_hdr || !s_cb_hdr)
        return;
    const float cb[4] = {s_out == kOutScrgb ? 2.0f : 1.0f, static_cast<float>(s_hdr_paper), static_cast<float>(s_hdr_peak),
                         s_out == kOutPq ? 1.0f / 1023.0f : 0.0f};
    s_ctx->UpdateSubresource(s_cb_hdr.get(), 0, nullptr, cb, 0, 0);
    ID3D11RenderTargetView* rtv = s_rtv.get();
    s_ctx->OMSetRenderTargets(1, &rtv, nullptr);
    const D3D11_VIEWPORT vp = {0.0f, 0.0f, static_cast<float>(s_sw), static_cast<float>(s_sh), 0.0f, 1.0f};
    s_ctx->RSSetViewports(1, &vp);
    const D3D11_RECT sc = {0, 0, s_sw, s_sh};
    s_ctx->RSSetScissorRects(1, &sc);
    s_ctx->RSSetState(s_raster.get());
    s_ctx->OMSetBlendState(s_blend[1].get(), nullptr, 0xffffffffu);
    s_ctx->IASetInputLayout(nullptr);
    s_ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    s_ctx->VSSetShader(s_vs_full.get(), nullptr, 0);
    s_ctx->PSSetShader(s_ps_hdr.get(), nullptr, 0);
    ID3D11ShaderResourceView* srv = s_scene_srv.get();
    s_ctx->PSSetShaderResources(0, 1, &srv);
    ID3D11Buffer* cbp = s_cb_hdr.get();
    s_ctx->PSSetConstantBuffers(2, 1, &cbp);
    s_ctx->Draw(3, 0);
    ID3D11ShaderResourceView* none = nullptr;
    s_ctx->PSSetShaderResources(0, 1, &none); // the scene is a render target again next frame
    s_b_srv = nullptr;                        // (SetBaseState at the next plat_begin_frame restores the UI pipeline)
}

void EndFrameInternal(bool present)
{
    Flush();
    s_in_frame = false;
    if (!s_sc)
        return;
    if (!present)
        return;
    Composite();
    const HRESULT hr = s_sc->Present(1, 0);
    NotePresent();
    if (hr == DXGI_ERROR_DEVICE_REMOVED || hr == DXGI_ERROR_DEVICE_RESET)
    {
        Log("plat: Present returned 0x%08X, the device will be rebuilt", static_cast<unsigned>(hr));
        s_need_device = true;
    }
}

} // namespace

// ---- host-facing entry points (UI thread) ------------------------------------------------------------------------

extern "C" {

void PlatD3D11_SetWindow(IUnknown* core_window, int pixel_w, int pixel_h, float dpi_scale)
{
    std::lock_guard<std::mutex> lk(s_win_mutex);
    s_window = nullptr;
    if (core_window)
    {
        core_window->AddRef();
        s_window.attach(core_window);
    }
    s_win_w = pixel_w > 0 ? pixel_w : kCanvasW;
    s_win_h = pixel_h > 0 ? pixel_h : kCanvasH;
    s_dpi = dpi_scale > 0.0f ? dpi_scale : 1.0f;
    s_win_changed = true;
}

void PlatD3D11_Resize(int pixel_w, int pixel_h, float dpi_scale)
{
    if (pixel_w <= 0 || pixel_h <= 0)
        return;
    std::lock_guard<std::mutex> lk(s_win_mutex);
    s_win_w = pixel_w;
    s_win_h = pixel_h;
    if (dpi_scale > 0.0f)
        s_dpi = dpi_scale;
    s_size_changed = true;
}

void PlatD3D11_Trim(void)
{
    // Called from the UI thread while the frontend thread may be drawing: the immediate context is not touched, only
    // IDXGIDevice3::Trim, which D3D11 allows from any thread (the device is free-threaded).
    std::lock_guard<std::mutex> lk(s_dev_mutex);
    if (!s_dev)
        return;
    winrt::com_ptr<IDXGIDevice3> d3;
    if (SUCCEEDED(s_dev->QueryInterface(__uuidof(IDXGIDevice3), d3.put_void())))
        d3->Trim();
}

void PlatD3D11_SetActive(bool active)
{
    s_active.store(active, std::memory_order_release);
    if (!active)
        s_key_bits.store(0, std::memory_order_release);
}

void PlatD3D11_Pointer(float x, float y, bool pressed, bool valid)
{
    std::lock_guard<std::mutex> lk(s_ptr_mutex);
    if (valid && s_ptr_have_last)
    {
        s_ptr_dx += x - s_ptr_last_x;
        s_ptr_dy += y - s_ptr_last_y;
    }
    s_ptr_have_last = valid;
    s_ptr_last_x = x;
    s_ptr_last_y = y;
    s_ptr_x = x;
    s_ptr_y = y;
    s_ptr_pressed = pressed;
    s_ptr_valid = valid;
}

// ---- HDR hooks of the UWP host (UI thread) -----------------------------------------------------------------------

void PlatD3D11_HdrAttach(void)
{
    std::unique_ptr<HdrUi> ui(new (std::nothrow) HdrUi);
    if (!ui)
        return;
    try
    {
        const wuc::CoreWindow window = wuc::CoreWindow::GetForCurrentThread();
        if (window)
            ui->dispatcher = window.Dispatcher();
    }
    catch (...)
    {
    }
    try
    {
        ui->display = wgd::DisplayInformation::GetForCurrentView();
    }
    catch (...)
    {
    }
    bool xbox = false;
    try
    {
        xbox = winrt::Windows::System::Profile::AnalyticsInfo::VersionInfo().DeviceFamily() == L"Windows.Xbox";
    }
    catch (...)
    {
    }
    if (xbox)
    {
        try
        {
            ui->hdmi = wgdc::HdmiDisplayInformation::GetForCurrentView(); // null where there is no HDMI control
        }
        catch (...)
        {
            ui->hdmi = nullptr;
        }
    }
    if (ui->display)
    {
        try
        {
            ui->colour_token = ui->display.AdvancedColorInfoChanged(
                [](const wgd::DisplayInformation& sender, const wf::IInspectable&) { RefreshOsCaps(sender); });
        }
        catch (...)
        {
        }
    }
    if (ui->hdmi)
    {
        try
        {
            ui->modes_token = ui->hdmi.DisplayModesChanged([](const wgdc::HdmiDisplayInformation& sender, const wf::IInspectable&) {
                RefreshXboxCaps(sender);
                s_xbox_repost.store(true);
            });
        }
        catch (...)
        {
        }
    }
    s_cap_xbox.store(ui->hdmi != nullptr);
    RefreshOsCaps(ui->display);
    RefreshXboxCaps(ui->hdmi);
    HdrNote("HDR hooks attached (%s, dispatcher %s)", ui->hdmi ? "Xbox HDMI control" : "Windows advanced colour",
            ui->dispatcher ? "yes" : "no");
    HdrUi* old;
    {
        std::lock_guard<std::mutex> lk(s_ui_mutex);
        old = s_ui;
        s_ui = ui.release();
    }
    DetachUi(old);
    s_hdr_dirty.store(true, std::memory_order_release);
}

void PlatD3D11_HdrSuspend(void)
{
    s_hdr_suspended.store(true);
    XboxRestoreBlocking(500);
    s_hdr_dirty.store(true, std::memory_order_release);
}

void PlatD3D11_HdrResume(void)
{
    s_hdr_suspended.store(false);
    wgd::DisplayInformation display{nullptr};
    wgdc::HdmiDisplayInformation hdmi{nullptr};
    {
        std::lock_guard<std::mutex> lk(s_ui_mutex);
        if (s_ui)
        {
            display = s_ui->display;
            hdmi = s_ui->hdmi;
        }
    }
    RefreshOsCaps(display);
    RefreshXboxCaps(hdmi);
    s_xbox_repost.store(true);
    s_hdr_dirty.store(true, std::memory_order_release);
}

void PlatD3D11_HdrShutdown(void)
{
    s_hdr_suspended.store(true);
    XboxRestoreBlocking(1000);
    HdrUi* ui;
    {
        std::lock_guard<std::mutex> lk(s_ui_mutex);
        ui = s_ui;
        s_ui = nullptr;
    }
    DetachUi(ui);
    s_cap_xbox.store(false);
}

void plat_xbox_device_lost(void)
{
    s_need_device = true;
}

void plat_xbox_key_event(uint32_t virtual_key, bool down)
{
    if (virtual_key >= 0xC3u && virtual_key <= 0xDAu)
        return; // VK_GAMEPAD_A .. VK_GAMEPAD_RIGHT_THUMBSTICK_LEFT: the pad comes through XInput
    u32 bit = 0;
    switch (virtual_key) // REF desktop map, plat_sdl.c:883-896
    {
        case 'Z': bit = BIT(BTN_CROSS); break;
        case 'X': bit = BIT(BTN_CIRCLE); break;
        case 'A': bit = BIT(BTN_SQUARE); break;
        case 'S': bit = BIT(BTN_TRIANGLE); break;
        case VK_SHIFT:
        case VK_LSHIFT:
        case VK_RSHIFT: bit = BIT(BTN_SELECT); break;
        case VK_RETURN: bit = BIT(BTN_START); break;
        case 'Q': bit = BIT(BTN_L1); break;
        case 'W': bit = BIT(BTN_R1); break;
        case '1': bit = BIT(BTN_L2); break;
        case '2': bit = BIT(BTN_R2); break;
        case VK_UP: bit = BIT(BTN_UP); break;
        case VK_DOWN: bit = BIT(BTN_DOWN); break;
        case VK_LEFT: bit = BIT(BTN_LEFT); break;
        case VK_RIGHT: bit = BIT(BTN_RIGHT); break;
        case VK_ESCAPE:
        case VK_TAB: bit = BIT(BTN_MENU); break;
        default: return;
    }
    if (down)
        s_key_bits.fetch_or(bit, std::memory_order_acq_rel);
    else
        s_key_bits.fetch_and(~bit, std::memory_order_acq_rel);
}

void plat_xbox_keys_reset(void)
{
    s_key_bits.store(0, std::memory_order_release);
}

float plat_xbox_ui_scale(void)
{
    return s_scale;
}

// Kept so code written against the DuckStation bridge links: the Direct3D picture needs no core display settings.
bool plat_xbox_sync_display_settings(const Settings* view)
{
    (void)view;
    return false;
}

// ---- lifecycle ---------------------------------------------------------------------------------------------------

bool plat_init(void)
{
    s_init_error[0] = '\0';
    if (s_gpu_alive)
        ReleaseGpuObjects();
    if (!BuildGpu())
    {
        if (!s_init_error[0])
            std::snprintf(s_init_error, sizeof(s_init_error), "Direct3D 11 initialisation failed");
        return false;
    }
    SetBaseState();
    std::snprintf(s_screen_info, sizeof(s_screen_info), "Direct3D 11, swap chain %dx%d (dpi x%.2f), UI 1920x1080 x%.3f at (%.0f,%.0f)",
                  s_sw, s_sh, static_cast<double>(s_dpi), static_cast<double>(s_scale), static_cast<double>(s_ox),
                  static_cast<double>(s_oy));
    RefreshClockPreference(NowUs(), true);
    s_hdr_test = LocalStateFileExists(L"hdr-test.txt");
    s_hdr_dirty.store(true, std::memory_order_release);
    for (int i = 0; i < kMaxPads; ++i)
    {
        s_connected[i] = false;
        s_last_probe[i] = 0;
        s_rumble_strong[i] = s_rumble_weak[i] = 0;
        s_rumble_at[i] = 0;
    }
    s_present_last_us = 0;
    s_present_samples = 0;
    s_in_frame = false;
    s_verts.reserve(1u << 15);
    s_initialized = true;
    s_gpu_alive = true;
    s_need_device = false;
    return true;
}

const char* plat_init_error(void)
{
    return s_init_error;
}

const char* plat_screen_info(void)
{
    return s_screen_info;
}

void plat_shutdown(void)
{
    plat_audio_close();
    // Xbox: hand the TV back now if the UI thread still runs (PlatD3D11_HdrShutdown repeats it, blocking, at exit).
    if (s_xbox_mode.load() == 2 && !s_hdr_suspended.load())
        XboxPost(0);
    for (int i = 0; i < kMaxPads; ++i)
    {
        if (s_connected[i])
            SetVibration(i, 0, 0);
        s_rumble_strong[i] = s_rumble_weak[i] = 0;
    }
    if (s_gpu_alive)
    {
        s_verts.clear();
        DrainFrees();
        ReleaseGpuObjects();
        s_gpu_alive = false;
    }
    if (s_backdrop_tex)
    {
        plat_texture_free(s_backdrop_tex);
        s_backdrop_tex = nullptr;
        s_backdrop_shown = nullptr;
        DrainFrees();
    }
    s_game_have = false;
    s_initialized = false;
}

void plat_release(void)
{
    plat_shutdown(); // idempotent
}

bool plat_prepare_storage(char* error, size_t size)
{
    if (error && size > 0)
        error[0] = '\0';
    return true; // the UWP LocalState is always listable
}

const char* plat_sandbox_probe(void)
{
    return "not applicable (UWP LocalState)";
}

bool plat_unlock_disabled(void)
{
    return false;
}

void plat_set_unlock_disabled(bool disabled)
{
    (void)disabled; // PS5 only
}

void plat_default_root(char* out, size_t size)
{
    if (!out || size == 0)
        return;
    const std::string root = DataRootPath();
    if (root.empty())
    {
        out[0] = '\0';
        Log("plat: no LocalState folder for the data root");
        return;
    }
    static bool registered = false; // once, before other threads (SWANUI_PORTING_NOTES.md section 9 "Data root")
    if (!registered)
    {
        registered = true;
        if (swanui_set_data_root(root.c_str()) != 0)
            Log("plat: swanui_set_data_root failed");
    }
    CopyPathSlashes(out, size, root + "\\Odin"); // CONTRACT: data root <LocalState>\ares
}

// ---- input -------------------------------------------------------------------------------------------------------

void plat_poll(PadState pads[SwanStationPS5_MAX_PADS], bool* quit)
{
    (void)quit; // never set on Xbox: the app ends through the lifecycle events
    if (!pads)
        return;
    std::memset(pads, 0, sizeof(PadState) * kMaxPads);
    const u64 now = NowUs();
    PadState raw[kMaxPads] = {};
    for (int i = 0; i < kMaxPads; ++i)
    {
        if (!s_connected[i] && now - s_last_probe[i] < kPadProbeUs)
            continue; // probe empty slots once a second
        s_last_probe[i] = now;
        XINPUT_STATE st = {};
        if (XInputGetState(static_cast<DWORD>(i), &st) != ERROR_SUCCESS)
        {
            if (s_connected[i])
            {
                s_connected[i] = false;
                s_rumble_strong[i] = s_rumble_weak[i] = 0;
            }
            continue;
        }
        s_connected[i] = true;
        PadState& p = raw[i];
        const WORD b = st.Gamepad.wButtons;
        p.connected = true;
        if (b & XINPUT_GAMEPAD_A) p.buttons |= BIT(BTN_CROSS);
        if (b & XINPUT_GAMEPAD_B) p.buttons |= BIT(BTN_CIRCLE);
        if (b & XINPUT_GAMEPAD_X) p.buttons |= BIT(BTN_SQUARE);
        if (b & XINPUT_GAMEPAD_Y) p.buttons |= BIT(BTN_TRIANGLE);
        if (b & XINPUT_GAMEPAD_LEFT_SHOULDER) p.buttons |= BIT(BTN_L1);
        if (b & XINPUT_GAMEPAD_RIGHT_SHOULDER) p.buttons |= BIT(BTN_R1);
        if (b & XINPUT_GAMEPAD_LEFT_THUMB) p.buttons |= BIT(BTN_L3);
        if (b & XINPUT_GAMEPAD_RIGHT_THUMB) p.buttons |= BIT(BTN_R3);
        if (b & XINPUT_GAMEPAD_START) p.buttons |= BIT(BTN_START); // Menu button
        if (b & XINPUT_GAMEPAD_BACK) p.buttons |= BIT(BTN_MENU);   // View button = the DualSense touchpad's role
        if (b & XINPUT_GAMEPAD_DPAD_UP) p.buttons |= BIT(BTN_UP);
        if (b & XINPUT_GAMEPAD_DPAD_DOWN) p.buttons |= BIT(BTN_DOWN);
        if (b & XINPUT_GAMEPAD_DPAD_LEFT) p.buttons |= BIT(BTN_LEFT);
        if (b & XINPUT_GAMEPAD_DPAD_RIGHT) p.buttons |= BIT(BTN_RIGHT);
        p.l2 = st.Gamepad.bLeftTrigger; // 0..255
        p.r2 = st.Gamepad.bRightTrigger;
        if (p.l2 > 93) // = SDL axis (v*32767/255) > 12000, REF plat_sdl.c:927-930
            p.buttons |= BIT(BTN_L2);
        if (p.r2 > 93)
            p.buttons |= BIT(BTN_R2);
        p.lx = st.Gamepad.sThumbLX;
        p.ly = static_cast<int16_t>(~st.Gamepad.sThumbLY); // XInput +Y is up; SDL/PS5 +Y is down (~v = -v-1, exact)
        p.rx = st.Gamepad.sThumbRX;
        p.ry = static_cast<int16_t>(~st.Gamepad.sThumbRY);
    }
    RumbleWatchdog(now);

    // players (order_pads, REF plat_sdl.c:529-535)
    PadState ordered[kMaxPads] = {};
    for (int i = 0; i < kMaxPads; ++i)
    {
        const int src = Physical(i);
        if (src >= 0)
            ordered[i] = raw[src];
    }

    if (!s_active.load(std::memory_order_acquire))
    {
        for (int i = 0; i < kMaxPads; ++i)
            pads[i].connected = ordered[i].connected; // neutral while the window is inactive (Guide, focus loss)
        return;
    }
    for (int i = 0; i < kMaxPads; ++i)
        pads[i] = ordered[i];

    const u32 keys = s_key_bits.load(std::memory_order_acquire); // PC keyboard
    if (keys)
    {
        pads[0].buttons |= keys;
        pads[0].connected = true;
    }

    // pointer: touch position (logical canvas) and mouse deltas on pad 0
    {
        std::lock_guard<std::mutex> lk(s_ptr_mutex);
        if (s_ptr_valid && s_pad_touch && s_ptr_pressed)
        {
            const float lx = (s_ptr_x - s_ox) / s_scale;
            const float ly = (s_ptr_y - s_oy) / s_scale;
            pads[0].touching = true;
            pads[0].touch_x = static_cast<uint16_t>(std::clamp(lx, 0.0f, static_cast<float>(kCanvasW - 1)));
            pads[0].touch_y = static_cast<uint16_t>(std::clamp(ly, 0.0f, static_cast<float>(kCanvasH - 1)));
            pads[0].connected = true;
        }
        if (s_ptr_dx != 0.0f || s_ptr_dy != 0.0f)
        {
            const float k = s_scale > 0.0f ? 1.0f / s_scale : 1.0f;
            pads[0].mouse_dx = static_cast<int16_t>(std::clamp(s_ptr_dx * k, -32767.0f, 32767.0f));
            pads[0].mouse_dy = static_cast<int16_t>(std::clamp(s_ptr_dy * k, -32767.0f, 32767.0f));
            s_ptr_dx = s_ptr_dy = 0.0f;
        }
    }
}

void plat_rumble(int port, uint16_t strong, uint16_t weak)
{
    const int i = Physical(port);
    if (i < 0 || !s_connected[i])
        return;
    if (strong || weak)
        s_rumble_at[i] = NowUs(); // always refresh the watchdog for a non-zero value
    if (s_rumble_strong[i] == strong && s_rumble_weak[i] == weak)
        return;
    s_rumble_strong[i] = strong;
    s_rumble_weak[i] = weak;
    SetVibration(i, strong, weak);
}

void plat_set_lightbar(int port, uint32_t rgb)
{
    (void)port; // no light bar on Xbox pads
    (void)rgb;
}

void plat_set_player_order(const int order[SwanStationPS5_MAX_PADS])
{
    if (!order)
        return;
    for (int i = 0; i < kMaxPads; ++i)
        s_player_order[i] = (order[i] >= 0 && order[i] < kMaxPads) ? order[i] : i;
}

void plat_player_order(int order[SwanStationPS5_MAX_PADS])
{
    if (!order)
        return;
    for (int i = 0; i < kMaxPads; ++i)
        order[i] = s_player_order[i];
}

const char* plat_pad_name(int port)
{
    (void)port;
    return nullptr; // the UI shows "Controller"
}

void plat_pad_motion(bool on)
{
    s_pad_motion = on; // no motion sensors on Xbox pads: the fields stay zero
}

void plat_pad_touch(bool on)
{
    s_pad_touch = on; // no touch pad on Xbox pads (the pointer fills it when there is one)
}

void plat_pad_triggers(int port, PlatTrigger l2, PlatTrigger r2)
{
    (void)port; // no adaptive triggers on Xbox pads
    (void)l2;
    (void)r2;
}

int plat_pad_battery(int port)
{
    (void)port;
    return -1; // unknown
}

// ---- picture options (applied by the game pixel shader, see plat_draw_game) ---------------------------------------

void plat_set_colour(int brightness, int colour, int sharpen)
{
    s_brightness = brightness < 0 ? 0 : brightness > 3 ? 3 : brightness;
    s_colour = colour < 0 ? 0 : colour > 5 ? 5 : colour;
    s_sharpen = sharpen < 0 ? 0 : sharpen > 2 ? 2 : sharpen;
}

void plat_set_fsr(bool on)
{
    s_fsr = on;
}

// CONTRACT section 3. Frontend thread; applied at the next plat_begin_frame (HdrUpdate).
void plat_set_hdr(int mode, int peak_nits, int paper_white_nits, int game_boost)
{
    mode = mode < 0 ? 0 : mode > 2 ? 2 : mode;
    peak_nits = peak_nits < 400 ? 400 : peak_nits > 4000 ? 4000 : peak_nits;
    paper_white_nits = paper_white_nits < 80 ? 80 : paper_white_nits > 500 ? 500 : paper_white_nits;
    game_boost = game_boost < 0 ? 0 : game_boost > 3 ? 3 : game_boost;
    if (s_hdr_set_once && mode == s_hdr_mode && peak_nits == s_hdr_peak && paper_white_nits == s_hdr_paper &&
        game_boost == s_hdr_boost)
        return;
    if (!s_hdr_set_once || mode != s_hdr_mode)
    {
        s_xbox_posted = -1;             // a new decision may ask the TV again (also after a refused request)
        s_hdr_swapchain_failed = false; // and try the HDR swap-chain formats again
    }
    s_hdr_set_once = true;
    s_hdr_mode = mode;
    s_hdr_peak = peak_nits;
    s_hdr_paper = paper_white_nits;
    s_hdr_boost = game_boost;
    s_hdr_dirty.store(true, std::memory_order_release);
}

int plat_hdr_state(void)
{
    return s_hdr_state.load(std::memory_order_acquire);
}

double plat_framegen_hz(void)
{
    return 0.0;
}

int plat_framegen_times(void)
{
    return 0;
}

void plat_set_framegen(int generated, double core_hz, double speed, bool nominal)
{
    (void)generated;
    (void)core_hz;
    (void)speed;
    (void)nominal;
}

void plat_want_high_refresh(bool on)
{
    (void)on; // the console picks the HDMI mode
}

bool plat_gpu_ui_active(void)
{
    return s_initialized; // turns on reflections and 3D cases (REF ui/coverflow.c)
}

bool plat_set_output(int resolution)
{
    (void)resolution;
    return false;
}

void plat_want_output(int resolution)
{
    s_want_output = resolution; // stored, no effect
}

void plat_clock(char* out, size_t size)
{
    if (!out || size == 0)
        return;
    RefreshClockPreference(NowUs(), false);
    const std::time_t t = std::time(nullptr);
    std::tm tm = {};
    if (localtime_s(&tm, &t) != 0)
    {
        out[0] = '\0';
        return;
    }
    const int hour = tm.tm_hour, minute = tm.tm_min;
    if (s_clock_24h)
        std::snprintf(out, size, "%02d:%02d", hour, minute);
    else
        std::snprintf(out, size, "%d:%02d %s", hour % 12 ? hour % 12 : 12, minute, hour < 12 ? "AM" : "PM");
}

int plat_system_language(void)
{
    std::string tag;
    try
    {
        const auto langs = winrt::Windows::System::UserProfile::GlobalizationPreferences::Languages();
        if (langs && langs.Size() > 0)
            tag = winrt::to_string(langs.GetAt(0)); // BCP-47
    }
    catch (...)
    {
    }
    if (tag.empty())
    {
        wchar_t name[LOCALE_NAME_MAX_LENGTH] = {};
        if (GetUserDefaultLocaleName(name, LOCALE_NAME_MAX_LENGTH) > 0)
        {
            for (const wchar_t* p = name; *p; ++p)
                tag.push_back(static_cast<char>(*p < 128 ? *p : '?'));
        }
    }
    std::string primary;
    for (char ch : tag)
    {
        if (ch == '-' || ch == '_')
            break;
        primary.push_back(static_cast<char>((ch >= 'A' && ch <= 'Z') ? ch + ('a' - 'A') : ch));
    }
    if (primary == "fr")
        return LANG_FR;
    if (primary == "pt")
        return LANG_PT;
    if (primary == "es")
        return LANG_ES;
    if (primary == "ja")
        return LANG_JA;
    return LANG_EN;
}

void plat_profile(const char* name)
{
    (void)name; // no per-frame profiler here
}

void plat_draw_backdrop(const uint8_t* grey, int w, int h, uint32_t tint_argb)
{
    if (!grey || w <= 0 || h <= 0)
        return;
    // the grey picture as a texture, remade when rewritten; the GPU tints and scales it
    if (!s_backdrop_tex || s_backdrop_dirty || s_backdrop_shown != grey || s_backdrop_w != w || s_backdrop_h != h)
    {
        if (s_backdrop_tex)
            plat_texture_free(s_backdrop_tex);
        s_backdrop_tex = nullptr;
        const size_t n = static_cast<size_t>(w) * static_cast<size_t>(h);
        u8* px = static_cast<u8*>(std::malloc(n * 4));
        if (px)
        {
            for (size_t i = 0; i < n; ++i)
            {
                px[i * 4] = px[i * 4 + 1] = px[i * 4 + 2] = grey[i];
                px[i * 4 + 3] = 255;
            }
            s_backdrop_tex = plat_texture_create(px, w, h, true);
            std::free(px);
        }
        s_backdrop_shown = grey;
        s_backdrop_w = w;
        s_backdrop_h = h;
        s_backdrop_dirty = false;
    }
    if (s_backdrop_tex)
        plat_draw_texture(s_backdrop_tex, 0.0f, 0.0f, static_cast<float>(kCanvasW), static_cast<float>(kCanvasH),
                          0xff000000u | (tint_argb & 0x00ffffffu), true);
}

void plat_backdrop_changed(void)
{
    s_backdrop_dirty = true;
}

// ---- audio -------------------------------------------------------------------------------------------------------

bool plat_audio_open(int sample_rate)
{
    if (sample_rate <= 0)
        return false;
    if (s_voice && s_rate == sample_rate)
    {
        RingClear();
        return true;
    }
    plat_audio_close();
    {
        std::lock_guard<std::mutex> plk(s_push_mutex);
        s_audio_pos = 0.0;
        s_audio_last[0] = s_audio_last[1] = 0;
        std::lock_guard<std::mutex> lk(s_ring_mutex);
        s_ring_frames = static_cast<size_t>(sample_rate) * 4; // 4 s
        s_ring.assign(s_ring_frames * 2, 0);
        s_read = s_write = 0;
    }

    HRESULT hr = XAudio2Create(s_xa.put(), 0, XAUDIO2_DEFAULT_PROCESSOR);
    if (SUCCEEDED(hr))
        hr = s_xa->CreateMasteringVoice(&s_master, 2, 0, 0, nullptr, nullptr);
    if (SUCCEEDED(hr))
    {
        WAVEFORMATEX fmt = {};
        fmt.wFormatTag = WAVE_FORMAT_PCM;
        fmt.nChannels = 2;
        fmt.nSamplesPerSec = static_cast<DWORD>(sample_rate);
        fmt.wBitsPerSample = 16;
        fmt.nBlockAlign = 4;
        fmt.nAvgBytesPerSec = fmt.nSamplesPerSec * 4;
        hr = s_xa->CreateSourceVoice(&s_voice, &fmt, 0, XAUDIO2_DEFAULT_FREQ_RATIO, &s_audio_cb, nullptr, nullptr);
    }
    if (FAILED(hr))
    {
        Log("audio open failed: 0x%08X", static_cast<unsigned>(hr));
        if (s_master)
        {
            s_master->DestroyVoice();
            s_master = nullptr;
        }
        s_xa = nullptr;
        std::lock_guard<std::mutex> lk(s_ring_mutex);
        s_ring_frames = 0;
        s_ring.clear();
        return false;
    }
    s_rate = sample_rate;
    s_audio_live.store(true, std::memory_order_release);
    for (int i = 0; i < kAudioBuffers; ++i)
        AudioSubmit(i);
    s_voice->Start(0);
    Log("audio: %d Hz (XAudio2)", sample_rate);
    return true;
}

void plat_audio_close(void)
{
    s_audio_live.store(false, std::memory_order_release);
    // DestroyVoice waits for a running callback, which takes the ring mutex: never hold it here.
    if (s_voice)
    {
        s_voice->Stop(0);
        s_voice->DestroyVoice();
        s_voice = nullptr;
    }
    if (s_master)
    {
        s_master->DestroyVoice();
        s_master = nullptr;
    }
    s_xa = nullptr;
    std::lock_guard<std::mutex> plk(s_push_mutex);
    std::lock_guard<std::mutex> lk(s_ring_mutex);
    s_ring_frames = 0;
    s_ring.clear();
    s_read = s_write = 0;
    s_rate = 0;
}

// Any thread (the core's emulator thread): see the audio notes at the ring.
void plat_audio_push(const int16_t* frames, size_t frame_count)
{
    if (!frames || !frame_count || !s_audio_live.load(std::memory_order_acquire))
        return;
    const double ratio = s_audio_ratio.load(std::memory_order_relaxed);
    std::lock_guard<std::mutex> plk(s_push_mutex);
    if (ratio > 0.9999 && ratio < 1.0001)
    {
        s_audio_pos = 0;
        s_audio_last[0] = frames[frame_count * 2 - 2];
        s_audio_last[1] = frames[frame_count * 2 - 1];
        RingPush(frames, frame_count);
        return;
    }
    // REF plat_sdl.c:1067-1098: out[k] is read at audio_pos + k * ratio, linear interpolation
    const size_t need = static_cast<size_t>(static_cast<double>(frame_count) / ratio) + 3;
    if (s_stretch_out.size() < need * 2)
        s_stretch_out.resize(need * 2);
    size_t n = 0;
    double p = s_audio_pos;
    while (p < static_cast<double>(frame_count) && n < need)
    {
        const size_t i = static_cast<size_t>(p);
        const double f = p - static_cast<double>(i);
        for (int c = 0; c < 2; ++c)
        {
            const double a = (i == 0) ? s_audio_last[c] : frames[(i - 1) * 2 + c];
            const double b = frames[i * 2 + c];
            s_stretch_out[n * 2 + c] = static_cast<s16>(a + (b - a) * f);
        }
        ++n;
        p += ratio;
    }
    s_audio_pos = p - static_cast<double>(frame_count);
    s_audio_last[0] = frames[frame_count * 2 - 2];
    s_audio_last[1] = frames[frame_count * 2 - 1];
    if (n)
        RingPush(s_stretch_out.data(), n);
}

size_t plat_audio_queued_frames(void)
{
    std::lock_guard<std::mutex> lk(s_ring_mutex);
    return s_ring_frames > 0 ? static_cast<size_t>(s_write - s_read) : 0;
}

void plat_audio_clear(void)
{
    RingClear();
}

void plat_audio_set_ratio(double ratio)
{
    s_audio_ratio.store((ratio > 0.0 && ratio < 16.0) ? ratio : 1.0, std::memory_order_relaxed);
}

double plat_display_hz(void)
{
    if (s_present_samples < 30 || s_present_ema_us <= 0.0)
        return 60.0;
    const double hz = 1000000.0 / s_present_ema_us;
    static const double common[] = {24.0, 25.0, 30.0, 48.0, 50.0, 60.0, 72.0, 75.0, 90.0, 100.0, 120.0, 144.0};
    for (double c : common)
    {
        if (std::fabs(hz - c) <= c * 0.03)
            return c;
    }
    return hz;
}

// ---- drawing -----------------------------------------------------------------------------------------------------

int plat_width(void)
{
    return kCanvasW; // the logical canvas; never 0 (callers divide by it)
}

int plat_height(void)
{
    return kCanvasH;
}

void plat_begin_frame(uint32_t clear_argb)
{
    ++s_frame_serial;
    s_in_frame = false;
    s_verts.clear();
    s_b_srv = nullptr;
    DrainFrees();
    FlushNotes();
    if (!s_gpu_alive)
        return;

    // window / device changes are applied here, on the frontend thread
    bool rebuild = s_need_device;
    bool resize = false;
    {
        std::lock_guard<std::mutex> lk(s_win_mutex);
        if (s_win_changed)
            rebuild = true;
        else if (s_size_changed)
            resize = true;
    }
    if (rebuild)
    {
        Log("plat: rebuilding the Direct3D device and swap chain");
        ReleaseGpuObjects();
        ++s_gen;
        s_need_device = false;
        if (!BuildGpu())
        {
            Log("plat: rebuild failed: %s", s_init_error);
            s_need_device = true; // try again next frame
            return;
        }
        SetBaseState();
    }
    else if (resize)
    {
        int w, h;
        {
            std::lock_guard<std::mutex> lk(s_win_mutex);
            w = s_win_w;
            h = s_win_h;
            s_size_changed = false;
        }
        if (s_sc && (w != s_sw || h != s_sh))
        {
            s_ctx->OMSetRenderTargets(0, nullptr, nullptr);
            s_rtv = nullptr;
            const HRESULT hr = s_sc->ResizeBuffers(0, static_cast<UINT>(w), static_cast<UINT>(h), DXGI_FORMAT_UNKNOWN, 0);
            if (FAILED(hr) || !CreateTargets())
            {
                Log("plat: ResizeBuffers failed (0x%08X), rebuilding the device", static_cast<unsigned>(hr));
                s_need_device = true;
                return;
            }
            ReapplyColourSpace();
            SetBaseState();
        }
    }
    HdrUpdate(); // SDR / HDR decision and swap-chain format (only when something changed)
    if (!s_rtv || !s_ctx)
        return;

    UpdateTransform();
    SetBaseState(); // flip-model swap chains unbind the target on Present
    const u32 c = (clear_argb >> 24) ? clear_argb : 0u;
    const float col[4] = {static_cast<float>((c >> 16) & 0xff) / 255.0f, static_cast<float>((c >> 8) & 0xff) / 255.0f,
                          static_cast<float>(c & 0xff) / 255.0f, 1.0f};
    s_ctx->ClearRenderTargetView(DrawRtv(), col);
    s_clip_on = false;
    s_clip.x0 = 0;
    s_clip.y0 = 0;
    s_clip.x1 = s_sw;
    s_clip.y1 = s_sh;
    s_clip.empty = false;
    s_b_clip = s_clip;
    s_in_frame = true;
}

// Scale2x (AdvMAME2x) on XRGB8888, as REF plat_sdl.c: edges keep their colour, everything else is duplicated.
static void Scale2x(const u32* src, int w, int h, size_t pitch_px, u32* dst)
{
    const int dw = w * 2;
    for (int y = 0; y < h; ++y)
    {
        const u32* up = src + static_cast<size_t>(y > 0 ? y - 1 : y) * pitch_px;
        const u32* row = src + static_cast<size_t>(y) * pitch_px;
        const u32* dn = src + static_cast<size_t>(y < h - 1 ? y + 1 : y) * pitch_px;
        u32* o0 = dst + static_cast<size_t>(y) * 2 * dw;
        u32* o1 = o0 + dw;
        for (int x = 0; x < w; ++x)
        {
            const u32 B = up[x], D = row[x > 0 ? x - 1 : x], E = row[x];
            const u32 F = row[x < w - 1 ? x + 1 : x], H = dn[x];
            const bool edge = B != H && D != F;
            o0[x * 2] = edge && D == B ? D : E;
            o0[x * 2 + 1] = edge && B == F ? F : E;
            o1[x * 2] = edge && D == H ? D : E;
            o1[x * 2 + 1] = edge && H == F ? F : E;
        }
    }
}

// Converts one row of the core's frame to opaque BGRA8.
static void ConvertRow(const u8* row, int pixel_format, int width, u32* dst)
{
    if (pixel_format == 1) // XRGB8888 (BGRA in memory): alpha forced opaque
    {
        const u32* s = reinterpret_cast<const u32*>(row);
        for (int x = 0; x < width; ++x)
            dst[x] = s[x] | 0xff000000u;
    }
    else if (pixel_format == 2) // RGB565
    {
        const uint16_t* s = reinterpret_cast<const uint16_t*>(row);
        for (int x = 0; x < width; ++x)
        {
            const u32 v = s[x];
            const u32 r = (v >> 11) & 31, g = (v >> 5) & 63, b = v & 31;
            dst[x] = 0xff000000u | (((r << 3) | (r >> 2)) << 16) | (((g << 2) | (g >> 4)) << 8) | ((b << 3) | (b >> 2));
        }
    }
    else // 0RGB1555
    {
        const uint16_t* s = reinterpret_cast<const uint16_t*>(row);
        for (int x = 0; x < width; ++x)
        {
            const u32 v = s[x];
            const u32 r = (v >> 10) & 31, g = (v >> 5) & 31, b = v & 31;
            dst[x] = 0xff000000u | (((r << 3) | (r >> 2)) << 16) | (((g << 3) | (g >> 2)) << 8) | ((b << 3) | (b >> 2));
        }
    }
}

void plat_upload_game(const void* pixels, int width, int height, size_t pitch, int pixel_format, int upscale,
                      int filter)
{
    if (!pixels || width <= 0 || height <= 0 || !s_gpu_alive || !s_ctx)
        return;

    // Prescale (REF plat_sdl.c): UPSCALE_SHARP needs no CPU pass here, the GPU samples the texels point-wise at any
    // size; UPSCALE_SMOOTH_PIXELS runs Scale2x once (2x, 3x) or twice (4x); UPSCALE_XBR runs the frontend's xBR 2x
    // once (REF: one pass, hi-res frames over 400 px wide are left alone). Textures stay within 4096x2048.
    int k = 1;
    if (upscale > 1 && filter != UPSCALE_SHARP)
    {
        k = filter == UPSCALE_XBR ? (width > 400 ? 1 : 2) : (upscale >= 4 ? 4 : 2);
        while (k > 1 && (width * k > 4096 || height * k > 2048))
            k /= 2;
    }

    const u8* src = static_cast<const u8*>(pixels);
    if (k > 1)
    {
        const size_t n = static_cast<size_t>(width) * height;
        s_game_conv.resize(n);
        for (int y = 0; y < height; ++y)
            ConvertRow(src + static_cast<size_t>(y) * pitch, pixel_format, width, s_game_conv.data() + static_cast<size_t>(y) * width);
        s_game_scaled.resize(n * static_cast<size_t>(k) * k);
        if (filter == UPSCALE_XBR)
            xbr2x(s_game_conv.data(), width, height, static_cast<size_t>(width), s_game_scaled.data());
        else if (k == 2)
            Scale2x(s_game_conv.data(), width, height, static_cast<size_t>(width), s_game_scaled.data());
        else // 4x: Scale2x twice
        {
            static std::vector<u32> two;
            two.resize(n * 4);
            Scale2x(s_game_conv.data(), width, height, static_cast<size_t>(width), two.data());
            Scale2x(two.data(), width * 2, height * 2, static_cast<size_t>(width) * 2, s_game_scaled.data());
        }
    }

    const int tw = width * k, th = height * k;
    if (!EnsureGameTexture(tw, th))
        return;
    D3D11_MAPPED_SUBRESOURCE m = {};
    if (FAILED(s_ctx->Map(s_game_tex.get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &m)))
        return;
    for (int y = 0; y < th; ++y)
    {
        u32* dst = reinterpret_cast<u32*>(static_cast<u8*>(m.pData) + static_cast<size_t>(y) * m.RowPitch);
        if (k > 1)
            std::memcpy(dst, s_game_scaled.data() + static_cast<size_t>(y) * tw, static_cast<size_t>(tw) * 4);
        else
            ConvertRow(src + static_cast<size_t>(y) * pitch, pixel_format, width, dst);
    }
    s_ctx->Unmap(s_game_tex.get(), 0);
    s_src_w = width;
    s_src_h = height;
    s_prescale = k;
    s_game_have = true;
}

void plat_upload_game_gpu(int width, int height)
{
    (void)width; // the core renders into the frontend's own texture only through plat_upload_game here
    (void)height;
}

void plat_draw_game(const Settings* settings, float display_aspect, uint8_t dim)
{
    if (!settings || !s_in_frame || !s_game_have || !s_game_srv || s_game_gen != s_gen)
        return;
    // Scanlines the game actually has: interlaced (480) and 2x-internal frames show at the same size as 240-line ones.
    int lines = s_src_h;
    while (lines > 288)
        lines /= 2;
    // Crop black edges: the share of the picture's height cut at the top and at the bottom (8 or 16 of 240 lines).
    const float crop =
        (!settings->manual_crop && settings->crop_edges > 0 && settings->crop_edges < 3 ? settings->crop_edges * 8 : 0) / 240.0f;

    int dx, dy, dw, dh;
    GameRect(settings, display_aspect, lines, dx, dy, dw, dh);
    s_game_rect[0] = dx;
    s_game_rect[1] = dy;
    s_game_rect[2] = dw;
    s_game_rect[3] = dh;
    if (dw <= 0 || dh <= 0)
        return;

    // ---- the picture's options -> cbuffer Picture (b1) ----
    // settings->shader (SwanStationPS5.h): 0 off, 1 LCD3x, 2 CRT Royale Fast, 3/4/11 retired, 5-8 NTSC (320/256 px,
    // S-Video/Composite), 9 CRT Lottes, 10 CRT Pi, 12 zfast LCD, 13 CRT Royale, 14 Newpixie, 15 Hyllian NTSC, 16 Mattias.
    struct Look
    {
        float beam, mask, corner, grid, sub, ntsc_px, ring;
    };
    static const Look kLooks[17] = {
        /* 0 off            */ {0.00f, 0.00f, 0.0f, 0.00f, 0.0f, 0.0f, 0.0f},
        /* 1 LCD3x          */ {0.00f, 0.00f, 0.0f, 0.55f, 0.6f, 0.0f, 0.0f},
        /* 2 Royale Fast    */ {0.60f, 0.25f, 0.5f, 0.00f, 0.0f, 0.0f, 0.0f},
        /* 3 retired        */ {0.00f, 0.00f, 0.0f, 0.00f, 0.0f, 0.0f, 0.0f},
        /* 4 retired        */ {0.00f, 0.00f, 0.0f, 0.00f, 0.0f, 0.0f, 0.0f},
        /* 5 NTSC 320 SVid  */ {0.00f, 0.00f, 0.0f, 0.00f, 0.0f, 1.0f, 0.0f},
        /* 6 NTSC 320 Comp  */ {0.00f, 0.00f, 0.0f, 0.00f, 0.0f, 2.0f, 0.35f},
        /* 7 NTSC 256 SVid  */ {0.00f, 0.00f, 0.0f, 0.00f, 0.0f, 1.0f, 0.0f},
        /* 8 NTSC 256 Comp  */ {0.00f, 0.00f, 0.0f, 0.00f, 0.0f, 2.0f, 0.35f},
        /* 9 CRT Lottes     */ {0.70f, 0.35f, 1.0f, 0.00f, 0.0f, 0.0f, 0.0f},
        /* 10 CRT Pi        */ {0.50f, 0.15f, 0.0f, 0.00f, 0.0f, 0.0f, 0.0f},
        /* 11 retired       */ {0.00f, 0.00f, 0.0f, 0.00f, 0.0f, 0.0f, 0.0f},
        /* 12 zfast LCD     */ {0.00f, 0.00f, 0.0f, 0.35f, 0.3f, 0.0f, 0.0f},
        /* 13 CRT Royale    */ {0.80f, 0.40f, 1.0f, 0.00f, 0.0f, 0.0f, 0.0f},
        /* 14 CRT Newpixie  */ {0.60f, 0.20f, 0.5f, 0.00f, 0.0f, 0.6f, 0.0f},
        /* 15 Hyllian NTSC  */ {0.60f, 0.25f, 0.5f, 0.00f, 0.0f, 1.0f, 0.0f},
        /* 16 CRT Mattias   */ {0.50f, 0.30f, 1.0f, 0.00f, 0.0f, 0.5f, 0.2f},
    };
    const int shader_id = settings->shader >= 0 && settings->shader < 17 ? settings->shader : 0;
    Look look = kLooks[shader_id];
    // NTSC blur widths are given for a 320 (or 256) column signal: scale them to this frame's columns.
    if (look.ntsc_px > 0.0f)
    {
        const float columns = (shader_id == 7 || shader_id == 8) ? 256.0f : 320.0f;
        look.ntsc_px *= static_cast<float>(s_game_w) / columns;
    }
    static const float kGain[4] = {0.80f, 1.00f, 1.15f, 1.30f};
    float saturation = 1.0f, temperature = 0.0f, mono = 0.0f;
    switch (s_colour)
    {
    case 1: saturation = 1.35f; break;          // vivid
    case 2: saturation = 0.80f; break;          // soft
    case 3: temperature = 1.0f; break;          // warm
    case 4: temperature = -1.0f; break;         // cool
    case 5: mono = 1.0f; break;                 // black and white
    default: break;
    }
    static const float kSharpen[3] = {0.0f, 0.45f, 0.9f};
    const bool fsr = s_fsr && !settings->integer_scale;
    // FSR implies a smooth, edge-sharpened scale; otherwise sharp texels unless the Smooth setting asks for bilinear.
    const bool sharp_sampling = !fsr && !settings->smooth;
    // HDR (s_out != kOutSdr): inverse tone mapping per game boost (notes/HDR.md). P = peak / paper white; the boost
    // level picks the knee (linear luminance where the lift starts), how much of P the white reaches, and a mild
    // saturation boost. Boost 0 = the picture at paper white, clamped there like SDR.
    const bool hdr = s_out != kOutSdr;
    static const float kKnee[4] = {1.0f, 0.60f, 0.45f, 0.30f};
    static const float kReach[4] = {0.0f, 0.35f, 0.65f, 1.0f};
    static const float kSat[4] = {1.0f, 1.04f, 1.08f, 1.12f};
    const int boost = s_hdr_boost < 0 ? 0 : s_hdr_boost > 3 ? 3 : s_hdr_boost;
    const float ratio = std::max(1.0f, static_cast<float>(s_hdr_peak) / static_cast<float>(std::max(s_hdr_paper, 1)));
    const float expand = 1.0f + (ratio - 1.0f) * kReach[boost];
    const float cb[8][4] = {
        {static_cast<float>(s_game_w), static_cast<float>(s_game_h), 1.0f / static_cast<float>(s_game_w),
         1.0f / static_cast<float>(s_game_h)},
        {kGain[s_brightness], saturation, temperature, mono},
        {kSharpen[s_sharpen] + (fsr ? 0.25f : 0.0f) > 0.9f ? 0.9f : kSharpen[s_sharpen] + (fsr ? 0.25f : 0.0f),
         fsr ? 1.0f : 0.0f, sharp_sampling ? 1.0f : 0.0f, static_cast<float>(lines)},
        {look.ntsc_px, look.ring, 0.0f, 0.0f},
        {look.beam, look.mask, look.corner, look.grid},
        {look.sub, static_cast<float>(dw) * s_scale, static_cast<float>(dh) * s_scale, 0.0f},
        {hdr ? 1.0f : 0.0f, expand, kKnee[boost], boost > 0 ? ratio : 1.0f},
        {kSat[boost], 0.0f, 0.0f, 0.0f},
    };

    // The picture goes through its own pixel shader (one draw, outside the UI batches); the UI shader is put back after.
    Flush();
    s_ctx->UpdateSubresource(s_cb_game.get(), 0, nullptr, cb, 0, 0);
    ID3D11Buffer* cbp = s_cb_game.get();
    s_ctx->PSSetConstantBuffers(1, 1, &cbp);
    s_ctx->PSSetShader(s_ps_game.get(), nullptr, 0);
    QuadSrv(s_game_srv.get(), 1 /* linear: the shader snaps or filters by itself */, true, static_cast<float>(dx),
            static_cast<float>(dy), static_cast<float>(dx + dw), static_cast<float>(dy + dh), 0.0f, crop, 1.0f,
            1.0f - crop, 0xffffffffu);
    Flush();
    s_ctx->PSSetShader(s_ps.get(), nullptr, 0);

    // over the picture: scanlines or one dim fill (REF plat_sdl.c:1647-1668)
    static const uint8_t scan_strength[] = {0, 110, 200};
    const uint8_t scan = scan_strength[static_cast<unsigned>(settings->crt) % 3u];
    if (scan && lines > 0)
    {
        for (int y = 0; y < dh; ++y)
        {
            const int64_t pos = (static_cast<int64_t>(y) * 2 + 1) * lines * 128 / dh;
            const int phase = static_cast<int>(pos & 255) - 128;
            const uint32_t edge = static_cast<uint32_t>(phase * phase) >> 6;
            uint32_t keep = static_cast<uint32_t>(dim) * (256 - ((edge * scan) >> 8)) >> 8;
            if (hdr)
            {
                // HDR: the row's weight multiplies linear light. The scene blends sRGB-encoded values, so the
                // encoded multiplier is weight^(1/2.2); the dim part stays an SDR-style (encoded) fade.
                const float weight = static_cast<float>(256 - ((edge * scan) >> 8)) / 256.0f;
                keep = static_cast<uint32_t>(static_cast<float>(dim) * std::pow(weight, 1.0f / 2.2f) + 0.5f);
            }
            const uint32_t argb = (255u - (keep > 255u ? 255u : keep)) << 24;
            if (argb >> 24)
                plat_fill_rect(dx, dy + y, dw, 1, argb);
        }
    }
    else if (dim < 255)
    {
        plat_fill_rect(dx, dy, dw, dh, static_cast<uint32_t>(255 - dim) << 24);
    }
}

void plat_fill_rect(int x, int y, int w, int h, uint32_t argb)
{
    if ((argb >> 24) == 0 || w <= 0 || h <= 0)
        return;
    QuadSrv(s_white.get(), 0, false, static_cast<float>(x), static_cast<float>(y), static_cast<float>(x + w),
            static_cast<float>(y + h), 0.0f, 0.0f, 1.0f, 1.0f, argb);
}

void plat_game_rect(int* x, int* y, int* w, int* h)
{
    if (x) *x = s_game_rect[0];
    if (y) *y = s_game_rect[1];
    if (w) *w = s_game_rect[2];
    if (h) *h = s_game_rect[3];
}

void plat_end_frame(void)
{
    if (!s_gpu_alive)
        return;
    EndFrameInternal(s_rtv != nullptr);
}

// ---- textures and meshes -----------------------------------------------------------------------------------------

PlatTexture* plat_texture_create(const uint8_t* rgba, int width, int height, bool smooth)
{
    return TextureCreate(rgba, width, height, smooth, false);
}

PlatTexture* plat_texture_create_mips(const uint8_t* rgba, int width, int height)
{
    return TextureCreate(rgba, width, height, true, true);
}

void plat_texture_free(PlatTexture* texture)
{
    if (!texture)
        return;
    std::lock_guard<std::mutex> lk(s_tex_mutex);
    if (s_live.find(texture) == s_live.end() || texture->freed)
        return;
    texture->freed = true;
    s_free_queue.push_back(texture); // still drawable this frame; destroyed at the next plat_begin_frame
}

void plat_texture_size(const PlatTexture* texture, int* width, int* height)
{
    if (width)
        *width = texture ? texture->width : 0;
    if (height)
        *height = texture ? texture->height : 0;
}

void plat_draw_mesh(PlatTexture* texture, const PlatVertex* vertices, int vertex_count, const int* indices,
                    int index_count)
{
    if (!vertices || vertex_count <= 0 || !s_in_frame || s_clip.empty)
        return;
    ID3D11ShaderResourceView* srv;
    int samp;
    if (texture)
    {
        if (!EnsureUploaded(texture))
            return;
        srv = texture->srv.get();
        samp = SampOf(texture);
    }
    else
    {
        srv = s_white.get();
        samp = 0;
    }
    if (!srv)
        return;
    Select(srv, samp, false);
    const auto conv = [](const PlatVertex& pv) {
        Vtx v;
        v.x = TX(pv.x);
        v.y = TY(pv.y);
        v.u = pv.u;
        v.v = pv.v;
        v.col = pv.argb;
        return v;
    };
    if (!indices)
    {
        const int n = vertex_count - vertex_count % 3;
        for (int i = 0; i < n; ++i)
            s_verts.push_back(conv(vertices[i]));
        return;
    }
    const int ni = index_count - index_count % 3;
    for (int i = 0; i < ni; i += 3)
    {
        const int a = indices[i], b = indices[i + 1], c = indices[i + 2];
        if (a < 0 || b < 0 || c < 0 || a >= vertex_count || b >= vertex_count || c >= vertex_count)
            continue; // drop a triangle whose index is outside the vertices
        PushTri(conv(vertices[a]), conv(vertices[b]), conv(vertices[c]));
    }
}

void plat_draw_texture(PlatTexture* texture, float x, float y, float w, float h, uint32_t tint, bool blend)
{
    if (!texture || (tint >> 24) == 0 || !s_in_frame || !EnsureUploaded(texture))
        return;
    const int x0 = static_cast<int>(x + 0.5f), y0 = static_cast<int>(y + 0.5f);
    const int x1 = x0 + static_cast<int>(w + 0.5f), y1 = y0 + static_cast<int>(h + 0.5f);
    // blend=false ignores the picture's alpha (blend state off, alpha forced opaque)
    QuadSrv(texture->srv.get(), SampOf(texture), !blend, static_cast<float>(x0), static_cast<float>(y0),
            static_cast<float>(x1), static_cast<float>(y1), 0.0f, 0.0f, 1.0f, 1.0f, blend ? tint : (tint | 0xff000000u));
}

void plat_draw_texture_region(PlatTexture* texture, int sx, int sy, int sw, int sh, float x, float y, float w, float h,
                              uint32_t tint)
{
    if (!texture || (tint >> 24) == 0 || texture->width <= 0 || texture->height <= 0 || !s_in_frame ||
        !EnsureUploaded(texture))
        return;
    const int gx0 = static_cast<int>(x + 0.5f), gy0 = static_cast<int>(y + 0.5f);
    const int gw = static_cast<int>(x + w + 0.5f) - gx0, gh = static_cast<int>(y + h + 0.5f) - gy0;
    if (gw <= 0 || gh <= 0)
        return;
    const float tw = static_cast<float>(texture->width), th = static_cast<float>(texture->height);
    QuadSrv(texture->srv.get(), SampOf(texture), false, static_cast<float>(gx0), static_cast<float>(gy0),
            static_cast<float>(gx0 + gw), static_cast<float>(gy0 + gh), static_cast<float>(sx) / tw,
            static_cast<float>(sy) / th, static_cast<float>(sx + sw) / tw, static_cast<float>(sy + sh) / th, tint);
}

void plat_fill_rectf(float x, float y, float w, float h, uint32_t argb)
{
    if ((argb >> 24) == 0)
        return;
    const int x0 = static_cast<int>(x + 0.5f), y0 = static_cast<int>(y + 0.5f);
    const int rw = static_cast<int>(x + w + 0.5f) - x0, rh = static_cast<int>(y + h + 0.5f) - y0;
    if (rw <= 0 || rh <= 0)
        return;
    QuadSrv(s_white.get(), 0, false, static_cast<float>(x0), static_cast<float>(y0), static_cast<float>(x0 + rw),
            static_cast<float>(y0 + rh), 0.0f, 0.0f, 1.0f, 1.0f, argb);
}

void plat_set_clip(int x, int y, int w, int h)
{
    if (!s_in_frame)
        return;
    ClipRect c;
    if (w <= 0 || h <= 0) // clipping off: the whole swap chain
    {
        c.x0 = 0;
        c.y0 = 0;
        c.x1 = s_sw;
        c.y1 = s_sh;
        s_clip_on = false;
    }
    else
    {
        c.x0 = std::clamp(static_cast<int>(std::lround(TX(static_cast<float>(x)))), 0, s_sw);
        c.y0 = std::clamp(static_cast<int>(std::lround(TY(static_cast<float>(y)))), 0, s_sh);
        c.x1 = std::clamp(static_cast<int>(std::lround(TX(static_cast<float>(x + w)))), 0, s_sw);
        c.y1 = std::clamp(static_cast<int>(std::lround(TY(static_cast<float>(y + h)))), 0, s_sh);
        c.empty = (c.x1 <= c.x0 || c.y1 <= c.y0); // draws nothing, like a zero scissor
        s_clip_on = true;
    }
    s_clip = c;
}

void plat_asset_path(char* out, size_t size, const char* relative)
{
    if (!out || size == 0)
        return;
    // <InstalledLocation>/swanui/assets/<relative> with '/' inside the frontend
    std::string path = AppRootPath();
    if (path.empty())
        path = "swanui/assets";
    else
        path += "\\swanui\\assets";
    if (relative && *relative)
    {
        const char* rel = relative;
        while (*rel == '/' || *rel == '\\')
            ++rel;
        if (*rel)
        {
            path.push_back('/');
            path.append(rel);
        }
    }
    CopyPathSlashes(out, size, path);
}

// ---- time and notices --------------------------------------------------------------------------------------------

uint64_t plat_ticks_us(void)
{
    return NowUs();
}

void plat_sleep_us(uint32_t us)
{
    if (us == 0)
    {
        std::this_thread::yield();
        return;
    }
    std::this_thread::sleep_for(std::chrono::microseconds(us));
}

void plat_notify(const char* message)
{
    if (!message)
        return;
    Log("notify: %s", message); // the UI draws its own toasts; this is the log line (and a debugger string)
    OutputDebugStringA("notify: ");
    OutputDebugStringA(message);
    OutputDebugStringA("\n");
}

} // extern "C"
