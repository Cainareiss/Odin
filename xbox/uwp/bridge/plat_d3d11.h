/*
 * ares (UWP / Xbox) - the frontend's platform layer (src/platform/platform.h) on Direct3D 11.
 * Public entry points for the host application. Everything declared in src/platform/platform.h (plat_*) is defined
 * in plat_d3d11.cpp with C linkage; this header only adds what the UWP host (CoreWindow events) must call.
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Call order (host application):
 *   1. UI thread, once the CoreWindow exists and before the frontend starts:
 *        PlatD3D11_SetWindow(coreWindowAsIUnknown, pixelW, pixelH, dpiScale);
 *      (pixelW/H = window size in physical pixels = DIPs * dpiScale; dpiScale = DisplayInformation::RawPixelsPerViewPixel.)
 *   2. Frontend thread (the one that runs swanui_main_init / swanui_main_frame): swanui_main_init() -> plat_init()
 *      creates the D3D11 device and the swap chain for that CoreWindow. Every plat_* call, plat_begin_frame ..
 *      plat_end_frame (which presents with vsync), must stay on that one thread.
 *   3. UI thread, any time: PlatD3D11_Resize (SizeChanged), PlatD3D11_SetActive (Activated / VisibilityChanged),
 *      plat_xbox_key_event / plat_xbox_keys_reset (KeyDown / KeyUp / focus lost), PlatD3D11_Pointer (pointer moves),
 *      PlatD3D11_Trim (Suspending, after swanui_main_suspend()).
 *      HDR: PlatD3D11_HdrAttach right after PlatD3D11_SetWindow, PlatD3D11_HdrSuspend in Suspending (after the
 *      frontend suspended), PlatD3D11_HdrResume in Resuming, PlatD3D11_HdrShutdown after the frontend thread joined.
 *   4. Frontend thread, at exit: swanui_main_shutdown() -> plat_shutdown().
 * The resize is applied by the frontend thread at the next plat_begin_frame; nothing here touches D3D from the UI
 * thread.
 */
#ifndef FLAMENCO_PLAT_D3D11_H
#define FLAMENCO_PLAT_D3D11_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
struct IUnknown;
extern "C" {
#else
typedef struct IUnknown IUnknown;
#endif

/* The CoreWindow (as IUnknown*, AddRef'ed here) the swap chain is created for, and its size in physical pixels.
 * Safe from any thread; may be called again with a new window (the swap chain is rebuilt). */
void PlatD3D11_SetWindow(IUnknown *core_window, int pixel_w, int pixel_h, float dpi_scale);

/* The window changed size (physical pixels). Any thread. */
void PlatD3D11_Resize(int pixel_w, int pixel_h, float dpi_scale);

/* The app is being suspended: IDXGIDevice3::Trim. Any thread (no-op without a device). */
void PlatD3D11_Trim(void);

/* The window has / lost the focus (Guide button, app switch): pads read as neutral while inactive. Any thread. */
void PlatD3D11_SetActive(bool active);

/* Pointer (mouse / touch) position in swap-chain pixels; `pressed` = a button / finger is down; `valid` = false when
 * the pointer left the window. Feeds PadState.touching / touch_x / touch_y (when plat_pad_touch(true)) and
 * mouse_dx / mouse_dy of pad 0. Any thread. */
void PlatD3D11_Pointer(float x, float y, bool pressed, bool valid);

/* ---- HDR (notes/HDR.md); plat_set_hdr / plat_hdr_state are in src/platform/platform.h ----
 * All four: UI (CoreWindow) thread only. */
/* After PlatD3D11_SetWindow: takes the CoreDispatcher, DisplayInformation (advanced colour, AdvancedColorInfoChanged)
 * and, on the Xbox, HdmiDisplayInformation (HDR10 modes, DisplayModesChanged). Without it HDR still works on PC
 * through DXGI (IDXGIOutput6) but never on the Xbox (no HDMI mode switch). */
void PlatD3D11_HdrAttach(void);
/* Suspending (after the frontend suspended): puts the TV back in its default (SDR) mode if we switched it, waiting up
 * to 0.5 s, and stops further HDMI requests until PlatD3D11_HdrResume. */
void PlatD3D11_HdrSuspend(void);
/* Resuming: re-reads the display and lets the frontend thread ask for HDR again. */
void PlatD3D11_HdrResume(void);
/* Exit (after the frontend thread ended): default HDMI mode (waits up to 1 s), event handlers removed. */
void PlatD3D11_HdrShutdown(void);

/* ---- same names and signatures as the previous (DuckStation) bridge's plat_xbox.h, so frontend code keeps linking ---- */

/* The GPU device is gone (DXGI_ERROR_DEVICE_REMOVED / RESET): the device and swap chain are rebuilt at the next
 * plat_begin_frame and every texture re-uploads from its CPU copy. Frontend thread. */
void plat_xbox_device_lost(void);

/* PC test keyboard (Windows virtual keys, REF desktop map) merged into pad 0 by plat_poll. UI thread. VK_GAMEPAD_* are
 * ignored: the pad comes through XInput. */
void plat_xbox_key_event(uint32_t virtual_key, bool down);
/* Focus lost: clears the key mask. Any thread. */
void plat_xbox_keys_reset(void);

/* The logical-canvas to swap-chain scale (1920x1080 letterboxed). Frontend thread. */
float plat_xbox_ui_scale(void);

/* Required from compat/swanui_compat.c; plat_default_root calls it once with the LocalState root. */
int swanui_set_data_root(const char *path);

#ifdef __cplusplus
}
#endif

#endif /* FLAMENCO_PLAT_D3D11_H */
