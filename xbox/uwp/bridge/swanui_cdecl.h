/*
 * SwanStation Xbox - prologue for the C++ bridge files (port-notes/specs/S0-review.md D25, owner S1).
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Include rule (S0 D24), in every bridge .cpp:
 *   1. every DuckStation header first;
 *   2. #include "swanui_cdecl.h"
 *   3. #define Settings SwanSettings   (frontend `typedef struct {...} Settings;`, REF SwanStationPS5.h:82-176,
 *                                       vs DuckStation `struct Settings`, DS core/settings.h)
 *   4. extern "C" { #include "src/<frontend headers>" ... #include "compat/<C ABI headers>" }
 *   5. #undef Settings
 *   6. src/nfs/nfs.h, src/ntfs/ntfsfs.h and src/discplay.h OUTSIDE extern "C" (they carry their own guards,
 *      and ntfsfs.h pulls <string>/<vector> under __cplusplus).
 * Bridge files are never force-included with compat/swanui_compat.h.
 */
#pragma once

/* Frontend headers branch on it (drives.h); swanui.vcxproj defines it on the command line, bridge TUs here. */
#ifndef SWANUI_XBOX
#define SWANUI_XBOX 1
#endif

/* __attribute__((format(printf, 1, 2))) and friends in frontend headers: hints only. The same definition as
 * compat/swanui_compat.h:47, so both may be seen in one TU (an identical redefinition is allowed). */
#define __attribute__(x)
