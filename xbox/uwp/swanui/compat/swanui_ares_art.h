/* ares UWP port - per-system game art: box art, title screens and gameplay pictures from libretro-thumbnails
 * (https://github.com/libretro-thumbnails/<repo>, Named_Boxarts / Named_Titles / Named_Snaps) and bezels from The Bezel
 * Project (thebezelproject/bezelproject-<system>, home systems whose overlays have a full-height 4:3 window).
 *
 * The picture names of each source are in assets/thumbs/<id>-boxarts.txt, -titles.txt, -snaps.txt, -bezels.txt
 * (tools/gen_thumbs_index.py): the exact file names, so '&*' '/:`<>?\|"' are already '_'. A game's best name there is
 * found by its title words, region, revision and disc (ares_art_best), and downloaded from
 *   https://raw.githubusercontent.com/<owner>/<repo>/master/<folder>/<name, percent-encoded>
 * Many libretro-thumbnails files are symbolic links; ares_art_download follows them.
 * SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef SWANUI_ARES_ART_H
#define SWANUI_ARES_ART_H
#include <stdbool.h>
#include <stddef.h>

#include "net.h"

#ifdef __cplusplus
extern "C" {
#endif

enum
{
    ARES_ART_BOX = 0,   /* Named_Boxarts */
    ARES_ART_TITLE = 1, /* Named_Titles */
    ARES_ART_SNAP = 2,  /* Named_Snaps */
    ARES_ART_BEZEL = 3, /* The Bezel Project, retroarch/overlay/GameBezels/<system>/ */
    ARES_ART_KINDS
};

/* The libretro-thumbnails repository of a system id (CONTRACT.md section 1), e.g. "sfc" ->
 * "Nintendo_-_Super_Nintendo_Entertainment_System": SwanSystem.thumbs_repo (compat/swanui_systems.h), else the one a
 * system without its own borrows here (sc3000 -> Sega_-_SG-1000); NULL when there is none (pcv2, myvision). */
const char *ares_thumbs_repo(const char *system_id);
/* The display name of a system id ("Super Famicom / SNES", from swanui_systems.h), NULL when unknown. */
const char *ares_art_system_name(const char *system_id);
/* The list asset of a system and kind (ARES_ART_*): "thumbs/sfc-boxarts.txt" ... "thumbs/sfc-bezels.txt";
 * false when the system has no such pictures. */
bool ares_art_index(const char *system_id, int kind, char *asset, size_t size);
/* The best name in a list asset for a game: `title` (the shelf title) and `file_name` (the game file's name without
 * folder and extension, "Super Mario World (USA)"; may be NULL). False when nothing matches well enough. */
bool ares_art_best(const char *asset, const char *title, const char *file_name, char *name, size_t size);
/* How well one list name ("Super Mario World (USA).png") fits the game: 0 for another game, more is better (an exact
 * file name is 2000). Exposed for tests. */
int ares_art_score(const char *candidate, const char *title, const char *file_name);
/* The download address of a list name: false when the system has no such pictures or `size` is too small. */
bool ares_art_url(const char *system_id, int kind, const char *name, char *url, size_t size);
/* Percent-encodes `s` into `out` (everything but A-Z a-z 0-9 - . _ ~); false when it does not fit. */
bool ares_art_url_encode(char *out, size_t size, const char *s);
/* The whole lookup: the best name for the game in the system's list of `kind`, else in its companion system's list
 * (gb <-> gbc, ws <-> wsc, ngp <-> ngpc, msx <-> msx2, pce <-> sgx), and that name's download address. */
bool ares_art_find(const char *system_id, int kind, const char *title, const char *file_name, char *name,
                   size_t name_size, char *url, size_t url_size);
/* net_download that follows libretro-thumbnails' symbolic links (raw.githubusercontent.com answers a link with the
 * target's name as text) and keeps only a PNG or JPEG at `dest`: anything else is removed and NET_NOT_FOUND returned. */
NetResult ares_art_download(const char *url, const char *dest);

#ifdef __cplusplus
}
#endif
#endif
