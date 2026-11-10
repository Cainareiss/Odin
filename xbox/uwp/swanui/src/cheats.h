/*
 * SwanStationPS5 - RetroArch .cht cheat files (GameShark / Action Replay codes).
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#ifndef SwanStationPS5_CHEATS_H
#define SwanStationPS5_CHEATS_H

#include "library.h"

#define CHEATS_MAX 512

typedef struct
{
    char desc[80];
    char code[256]; /* "80012345 0063+D0012345 0001" */
    bool enabled;
    bool patch;     /* a patch (widescreen, 60 fps, a fix): listed first */
} Cheat;

typedef struct
{
    Cheat items[CHEATS_MAX];
    int count;
    char source[SwanStationPS5_PATH_MAX]; /* the .cht that was loaded, "" if none */
    char state_path[SwanStationPS5_PATH_MAX];
} CheatList;

/* Finds the best .cht for `game`: one beside the game files first, then the
 * library in `cheats_dir` (libretro-database "Sony - PS1" layout).
 * Restores which codes were switched on last time.
 * ARES_UWP: by Game.system: the PlayStation as above (the disc's serial read by the host); the other systems from
 * assets/cheats/<id>.txt, the game's files of every cheat device merged, cached in cheats_dir/<libretro folder>/.
 * Codes the host's engine does not read are left out. See ARES_CHEATS_NOTES.md. */
bool cheats_load(CheatList *list, const Game *game, const char *cheats_dir);
/* Empties the list and switches every code off in the core (host_cheat_reset). */
void cheats_clear(CheatList *list);
/* True once when a cheat file cheats_load started downloading has arrived:
 * load again then. */
bool cheats_fetch_finished(void);
/* Marks the downloaded files of this game as old, so the next cheats_load
 * downloads them again (the old ones stay until the new ones arrive). Files
 * also refresh on their own once they are a day old. */
void cheats_mark_old(const Game *game, const char *cheats_dir);
/* A code of the player's own (Find a code): saved to cheats/mine/<id>.txt,
 * added to the list and switched on. */
bool cheats_add_user(CheatList *list, const Game *game, const char *cheats_dir, const char *desc,
                     const char *code);
/* Two titles with the same words ("Disney-Pixar's ..." = "Disney-Pixar ...",
 * "VII" = "7", "The Legend of X" = "Legend of X, The"). */
bool titles_match(const char *a, const char *b);
/* The best match for the game in a list of Redump-style names (an assets/
 * file, one per line, as cheats-index.txt); false when nothing matches. */
bool cheats_best_in_index(const Game *game, const char *asset, char *name, size_t size);

/* Pushes enabled codes into the running core (none in RetroAchievements hardcore mode: only host_cheat_reset). */
void cheats_apply(const CheatList *list);
void cheats_save_selection(const CheatList *list);

#endif
