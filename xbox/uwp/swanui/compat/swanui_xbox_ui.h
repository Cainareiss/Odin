/* SwanStation Xbox - frontend-side declarations added by the Xbox UI spec (port-notes/specs/S5-xbox-ui.md).
 * SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef SWANUI_XBOX_UI_H
#define SWANUI_XBOX_UI_H
#ifdef __cplusplus
extern "C" {
#endif
/* The Xbox text for a tr() key in language `lang` (enum Lang, i18n.h), or NULL when the key has none. Thread-safe
 * (read-only table, no lazy state). Defined in compat/swanui_xbox_i18n.c. */
const char *swanui_xbox_tr(const char *english, int lang);
/* DuckStation's version for Settings > About > Emulation core, e.g. "Odin: see the credits file in the package". Defined in
 * bridge/ui_xbox.cpp; never NULL. */
const char *swanui_core_version(void);
/* Tsugaru: `s` with every "Odin", "Odin" and "PSXS5" replaced by the product name, or `s`
 * itself when it has none. Cached by content; any thread. Called by ui/text.c for every string drawn. */
const char *swanui_xbox_brand(const char *s);
#ifdef __cplusplus
}
#endif
#endif
