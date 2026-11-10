/* Odin (experimental) - the systems the frontend lists, and the set names of the Odin catalogue.
 * Rewritten for the Odin shell from the ares UWP registry (2026-11-10). Two systems: "hikaru" (Sega Hikaru) and
 * "gaelco" (Gaelco ATV Track family). Defined in compat/swanui_systems.c. Plain C, callable from C++.
 * SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef SWANUI_SYSTEMS_H
#define SWANUI_SYSTEMS_H

#ifdef __cplusplus
extern "C" {
#endif

typedef struct SwanSystem
{
    const char *id, *name, *short_name, *mia_medium;
    const char *extensions;  /* space separated, lower case */
    const char *thumbs_repo; /* always NULL in Odin: no thumbnails are fetched */
    float aspect;            /* display aspect of the picture */
    int handheld;            /* always 0 */
    int disc;                /* always 0 */
} SwanSystem;

/* How many systems swanui_systems() returns (a constant, for arrays sized by it). */
#define SWANUI_SYSTEM_COUNT 2

/* Every system, in shelf order; *count gets SWANUI_SYSTEM_COUNT (count may be NULL). */
const SwanSystem *swanui_systems(int *count);
/* The system with this id, or NULL. */
const SwanSystem *swanui_system_by_id(const char *id);
/* Its index in swanui_systems(), or -1. */
int swanui_system_index(const char *id);
/* The system id of a game file (a zip, a 7z or a folder) whose name is an Odin set name, or NULL. No I/O. */
const char *swanui_system_of_path(const char *path);
/* The system a folder name stands for ("Hikaru", "Gaelco", "ATV Track"...), or NULL. */
const SwanSystem *swanui_system_of_folder_name(const char *name);
/* 1 when this file name is a zip or a 7z (a candidate for the library). No I/O. */
int swanui_system_known_extension(const char *name);

/* ---- Odin catalogue (generated into swanui_odin_sets.inc by tools/gen_odin_sets.py) ------------------------------
 * Names and titles only. `required` lists the shared sets a game needs, space separated: "hikaru mie" for a Hikaru
 * game, the parent set for a clone ("airtrix"), "" for a Gaelco game. `parent` is the parent set of a clone, or "". */
typedef struct OdinSetRow
{
    const char *name;
    const char *title;
    const char *system;
    const char *parent;
    const char *required;
} OdinSetRow;

/* The game row of a set name (case-insensitive), or NULL when the name is not one of the 14 game sets. */
const OdinSetRow *odin_set_find(const char *name);
/* 1 when the name is a support (shared) set: never a game (hikaru, mie, bootrom, jvs13551, 18wheelr, srally2). */
int odin_set_is_support(const char *name);
/* The number of game rows, and row i (0 <= i < count). */
int odin_set_count(void);
const OdinSetRow *odin_set_row(int i);

#ifdef __cplusplus
}
#endif

#endif /* SWANUI_SYSTEMS_H */
