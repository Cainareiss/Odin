/*
 * SwanStationPS5 - the game shelf (home screen).
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#ifndef SwanStationPS5_COVERFLOW_H
#define SwanStationPS5_COVERFLOW_H

#include "../library.h"
#if defined(ARES_UWP)
#include "swanui_systems.h" /* ARES_UWP: SWANUI_SYSTEM_COUNT */
#endif

enum ShelfCategory
{
    CAT_ALL,
    CAT_RECENT,
    CAT_FAVORITES,
    CAT_MULTI_DISC,
    CAT_USA,
    CAT_EUROPE,
    CAT_JAPAN,
    CAT_MULTIPLAYER, /* 2 players or more (DuckStation's database) */
#if defined(ARES_UWP)
    CAT_SYSTEM_FIRST, /* ARES_UWP: one category per system, in swanui_systems() order (shown after All games) */
    CAT_COUNT = CAT_SYSTEM_FIRST + SWANUI_SYSTEM_COUNT
#else
    CAT_COUNT
#endif
};

enum SortMode
{
    SORT_TITLE,
    SORT_RECENT,
    SORT_MOST_PLAYED,
    SORT_REGION,
    SORT_COUNT
};

/* The shelf's backdrop (also behind settings): a gradient tinted with the
 * selected game's cover colour. */
void shelf_backdrop(void);
/* Region name from a serial's prefix ("USA", "Europe", "Japan"), translated. */
const char *shelf_region_name(const char *serial);
/* The same for a game: its serial's, or (ARES_UWP) the region its name is tagged with ("(USA)", "(Europe)"...). */
const char *shelf_game_region_name(const Game *g);
/* ARES_UWP: the game's system, short ("SNES") or full ("Super Famicom / SNES"); "" when unknown or elsewhere. */
const char *shelf_game_system_name(const Game *g, bool full);
/* The name shown for a game: DuckStation's when its database knows the disc, else the file or folder name. */
const char *shelf_game_title(const Game *g);
/* "Folder: '/data'", the storage a game is loaded from; "" when unknown. */
void shelf_folder_tag(const Game *g, char *out, size_t size);
const char *shelf_sort_name(int sort);

#endif
