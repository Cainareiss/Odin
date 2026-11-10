// Tsugaru UWP port - the app's two roots and a tiny logger, shared by the UWP service bridges (no DuckStation).
// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef UWP_PATHS_H
#define UWP_PATHS_H

#ifdef __cplusplus
extern "C" {
#endif

/* ApplicationData.Current.LocalFolder (the app's writable LocalState), UTF-8, native '\' separators,
 * no trailing separator. Never NULL ("" when it could not be read). Any thread. */
const char *uwp_data_root(void);
/* Package.Current.InstalledLocation (the read-only install folder), same conventions. */
const char *uwp_install_root(void);
/* printf-style line to swanui_host_log (compat/swanui_main.h). Any thread. */
void uwp_log(const char *fmt, ...);

#ifdef __cplusplus
}
#endif

#endif
