/*
 * SwanStationPS5 - game art downloaded on demand: bezels (The Bezel Project), title
 * screens and gameplay pictures (libretro-thumbnails).
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Each source has a list of its PlayStation pictures in assets/ (written by
 * tools/make-art-index.py, Redump-style names as the cheat files). A game's
 * best match is downloaded once, on a thread, into <root>/art/<kind>/<id>.png;
 * from then on it comes from there. One download at a time.
 * ARES_UWP: the lists and addresses are per game system (compat/swanui_ares_art.h:
 * assets/thumbs/<system>-titles.txt, -snaps.txt, -bezels.txt; bezels only for the
 * home systems, whose Bezel Project overlays have the 4:3 window).
 */
#include "art.h"
#if defined(ARES_UWP)
#include "swanui_ares_art.h"
#elif defined(TSUGARU_UWP)
#include "swanui_towns_art.h"
#endif

#include "app.h"
#include "cheats.h"
#include "net.h"
#include "platform/platform.h"
#include "stb_image.h"

#include <SDL2/SDL.h>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const struct
{
    const char *folder; /* under <root>/art */
    const char *index;  /* in assets/ */
    const char *base;   /* the download address, before the picture's name */
} KINDS[ART_KIND_COUNT] = {
#if defined(ARES_UWP) /* per system: ares_art_find gives the list's match and its address */
    [ART_BEZEL] = {"bezels", "", ""},
    [ART_SNAP] = {"snaps", "", ""},
    [ART_TITLE] = {"titles", "", ""},
#elif defined(TSUGARU_UWP) /* LaunchBox Games Database (fmtowns-*-urls.txt gives the address); no FM Towns bezels */
    [ART_BEZEL] = {"bezels", "fmtowns-none-index.txt", ""},
    [ART_SNAP] = {"snaps", "fmtowns-snap-index.txt", "fmtowns-snap-urls.txt"},
    [ART_TITLE] = {"titles", "fmtowns-title-index.txt", "fmtowns-title-urls.txt"},
#else
    [ART_BEZEL] = {"bezels", "bezels-index.txt",
                   "https://raw.githubusercontent.com/thebezelproject/bezelproject-PSX/master/"
                   "retroarch/overlay/GameBezels/PSX/"},
    [ART_SNAP] = {"snaps", "snaps-index.txt",
                  "https://raw.githubusercontent.com/libretro-thumbnails/Sony_-_PS1/master/Named_Snaps/"},
    [ART_TITLE] = {"titles", "titles-index.txt",
                   "https://raw.githubusercontent.com/libretro-thumbnails/Sony_-_PS1/master/Named_Titles/"},
#endif
};

#ifdef ARES_UWP
static const int ARES_KIND[ART_KIND_COUNT] = {
    [ART_BEZEL] = ARES_ART_BEZEL,
    [ART_SNAP] = ARES_ART_SNAP,
    [ART_TITLE] = ARES_ART_TITLE,
};
#endif

enum
{
    ST_NEW,     /* not looked at yet */
    ST_LOADED,  /* texture holds it */
    ST_WAITING, /* a download is wanted (or running) */
    ST_NONE,    /* there's no such picture, or it couldn't be had: not this session */
};

#define SLOTS 8

typedef struct
{
    const Game *game;
    int kind, state;
    PlatTexture *texture;
    char name[256];      /* the match in the list */
#ifdef ARES_UWP
    char url[1024];      /* its address */
#endif
    uint64_t used;
} Slot;

static Slot slots[SLOTS];
static uint64_t clock_tick;

static SDL_atomic_t busy;   /* a download is running */
static SDL_atomic_t result; /* 0 none, 1 arrived, 2 failed: for the slot below */
static int fetch_slot = -1;
static char fetch_url[1024], fetch_dest[SwanStationPS5_PATH_MAX];

static void art_path(const Game *g, int kind, char *out, size_t size)
{
    char dir[SwanStationPS5_PATH_MAX], sub[64], file[120];
    snprintf(sub, sizeof(sub), "art/%s", KINDS[kind].folder);
    path_join(dir, sizeof(dir), app.paths.root, sub);
    snprintf(file, sizeof(file), "%.100s.png", g->id);
    path_join(out, size, dir, file);
}

static int fetch_main(void *unused)
{
    (void)unused;
    char temp[SwanStationPS5_PATH_MAX + 8];
    snprintf(temp, sizeof(temp), "%s.part", fetch_dest);
#ifdef ARES_UWP
    NetResult r = ares_art_download(fetch_url, temp); /* follows libretro-thumbnails' symbolic links */
#else
    NetResult r = net_download(fetch_url, temp);
#endif
    bool ok = r == NET_OK && rename(temp, fetch_dest) == 0;
    if (!ok)
        remove(temp);
    SwanStationPS5_log("art: download %s: %s", fetch_dest, ok ? "ok" : r == NET_NOT_FOUND ? "not found" : "failed");
    SDL_AtomicSet(&result, ok ? 1 : 2);
    SDL_AtomicSet(&busy, 0);
    return 0;
}

static bool fetch_start(int s)
{
    if (!net_available() || !SDL_AtomicCAS(&busy, 0, 1))
        return false;
    Slot *slot = &slots[s];
#ifdef ARES_UWP
    str_copy(fetch_url, sizeof(fetch_url), slot->url);
#else
    str_copy(fetch_url, sizeof(fetch_url), KINDS[slot->kind].base);
    size_t w = strlen(fetch_url);
    for (const unsigned char *p = (const unsigned char *)slot->name; *p && w + 4 < sizeof(fetch_url); ++p)
    {
        if (isalnum(*p) || strchr("-._~", *p))
            fetch_url[w++] = (char)*p;
        else
            w += (size_t)snprintf(fetch_url + w, sizeof(fetch_url) - w, "%%%02X", *p);
    }
    fetch_url[w] = '\0';
#ifdef TSUGARU_UWP
    if (!towns_art_url(KINDS[slot->kind].base, slot->name, fetch_url, sizeof(fetch_url)))
    {
        SDL_AtomicSet(&busy, 0);
        return false;
    }
#endif
#endif /* ARES_UWP */
    art_path(slot->game, slot->kind, fetch_dest, sizeof(fetch_dest));
    char dir[SwanStationPS5_PATH_MAX];
    str_copy(dir, sizeof(dir), fetch_dest);
    char *cut = strrchr(dir, '/');
    if (cut)
        *cut = '\0';
    make_dirs(dir);
    fetch_slot = s;
    SDL_Thread *t = SDL_CreateThread(fetch_main, "art-download", NULL);
    if (t)
    {
        SDL_DetachThread(t);
        return true;
    }
    SDL_AtomicSet(&busy, 0);
    return false;
}

/* The game's best picture name in the kind's list (and, ARES_UWP, its address) */
static bool find_match(const Game *g, int kind, Slot *slot)
{
#ifdef ARES_UWP
    return ares_art_find(g->system, ARES_KIND[kind], g->title, g->disc_name, slot->name, sizeof(slot->name), slot->url,
                         sizeof(slot->url));
#else
    return cheats_best_in_index(g, KINDS[kind].index, slot->name, sizeof(slot->name));
#endif
}

static bool load(Slot *slot)
{
    char path[SwanStationPS5_PATH_MAX];
    art_path(slot->game, slot->kind, path, sizeof(path));
    int w, h, n;
    uint8_t *rgba = stbi_load(path, &w, &h, &n, 4);
    if (!rgba)
        return false;
    slot->texture = plat_texture_create_mips(rgba, w, h);
    stbi_image_free(rgba);
    return slot->texture != NULL;
}

static void finished(void)
{
    int r = SDL_AtomicGet(&result);
    if (!r || fetch_slot < 0)
        return;
    SDL_AtomicSet(&result, 0);
    Slot *slot = &slots[fetch_slot];
    fetch_slot = -1;
    if (slot->state != ST_WAITING)
        return; /* the slot went to another picture meanwhile */
    slot->state = r == 1 && load(slot) ? ST_LOADED : ST_NONE;
}

PlatTexture *art_get(const Game *g, int kind)
{
    if (!g || kind < 0 || kind >= ART_KIND_COUNT)
        return NULL;
    finished();
    ++clock_tick;
    int s = -1, oldest = 0;
    for (int i = 0; i < SLOTS; ++i)
    {
        if (slots[i].game == g && slots[i].kind == kind)
            s = i;
        if (slots[i].used < slots[oldest].used)
            oldest = i;
    }
    if (s < 0)
    {
        /* reuse the least recently shown slot (never the one downloading) */
        s = oldest == fetch_slot ? (oldest + 1) % SLOTS : oldest;
        Slot *slot = &slots[s];
        plat_texture_free(slot->texture);
        memset(slot, 0, sizeof(*slot));
        slot->game = g;
        slot->kind = kind;
    }
    Slot *slot = &slots[s];
    slot->used = clock_tick;
    if (slot->state == ST_NEW)
    {
        if (load(slot))
            slot->state = ST_LOADED;
        else if ((kind == ART_BEZEL || app.global.cover_download) && find_match(g, kind, slot))
        {
            SwanStationPS5_log("art: %s %s -> %s", KINDS[kind].folder, g->title, slot->name);
            slot->state = ST_WAITING;
        }
        else
            slot->state = ST_NONE;
    }
    if (slot->state == ST_WAITING && fetch_slot != s)
        fetch_start(s);
    return slot->state == ST_LOADED ? slot->texture : NULL;
}

bool art_pending(const Game *g, int kind)
{
    for (int i = 0; i < SLOTS; ++i)
        if (slots[i].game == g && slots[i].kind == kind)
            return slots[i].state == ST_WAITING;
    return false;
}
