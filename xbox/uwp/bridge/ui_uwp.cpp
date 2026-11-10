// Tsugaru UWP port - small UI helpers that used to need DuckStation (compat/swanui_xbox_ui.h).
// SPDX-License-Identifier: GPL-3.0-or-later

#include "swanui_cdecl.h"
extern "C" {
#include "compat/swanui_xbox_ui.h"
}

// Settings > About > Emulation core. Never NULL.
extern "C" const char *swanui_core_version(void)
{
    return "none (experimental shell v0.1)";
}
