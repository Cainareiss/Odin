/* Tsugaru UWP port - FM Towns artwork from the LaunchBox Games Database. The index assets (fmtowns-*-index.txt) hold
 * one picture name a line for cheats_best_in_index; the companion fmtowns-*-urls.txt maps "name<TAB>url".
 * SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef SWANUI_TOWNS_ART_H
#define SWANUI_TOWNS_ART_H
#include <stddef.h>
#ifdef __cplusplus
extern "C" {
#endif
/* The download address of `name` (a line of the index) from the urls asset; 1 and `out` filled, or 0. */
int towns_art_url(const char *urls_asset, const char *name, char *out, size_t size);
#ifdef __cplusplus
}
#endif
#endif
