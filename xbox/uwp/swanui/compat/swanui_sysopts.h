/* ares UWP port - per-system emulator options (notes/CONTRACT2.md "System options API").
 * New file (ARES_UWP, 2026-11-08). The host side (uwp/ares_host/host_ares_opts.cpp) lists, reads and changes the options
 * of one system (an id of notes/CONTRACT.md section 1), keeps them in <data root>/systems/<id>.cfg and caches the
 * options it found in the running core in <data root>/systems/<id>.opts, so a system that is not running still shows
 * them. Three sources, in this order: the emulator options every system of that kind has (region, fast boot, pixel
 * accuracy, colour emulation...), the Setting nodes of the running core (CPU / PPU revisions...), and the controller /
 * expansion ports with the devices the core supports there.
 *
 * Threading: every host_sysopt_* call is made from the frontend thread. A change that applies at once is queued and
 * done on the emulator thread between two frames (also while the game is paused behind a menu).
 * Strings are English; the frontend passes them through tr().
 * SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef SWANUI_SYSOPTS_H
#define SWANUI_SYSOPTS_H
#include <stdbool.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

#define SYSOPT_MAX_VALUES 16
#define SYSOPT_LABEL 64

enum SysOptKind
{
    SYSOPT_EMULATOR = 0, /* a static option of this kind of system (known without running it) */
    SYSOPT_SETTING = 1,  /* a Setting node found in the running core (CPU revision...) */
    SYSOPT_PORT = 2,     /* a controller / expansion port: which device is plugged in */
};

typedef struct SysOpt
{
    char key[64];                                /* stable id: "region", "node:CPU/Version", "port:Controller Port 2" */
    char name[64];                               /* the label ("Preferred region") */
    char group[32];                              /* "Emulator", "Video", "Audio", "Hardware", "Media", "Controllers" */
    char help[320];                              /* what it does, one or two sentences */
    int kind;                                    /* enum SysOptKind */
    int count;                                   /* how many values */
    char values[SYSOPT_MAX_VALUES][SYSOPT_LABEL]; /* each value's label ("On", "Japan (NTSC-J)", "Mouse") */
    int current;                                 /* the chosen value (what is stored, or the default) */
    int def;                                     /* the default value */
    int applies_now;                             /* 1: a change reaches the running game at once; 0: at the next start */
    int pending;                                 /* 1: changed while this system runs; waits for the game to start again */
    int seen;                                    /* 1: found in the running core or in its cache (always 1 for static ones) */
} SysOpt;

/* Where the systems folder lives: the frontend's Paths.root (main.c calls it once, before any other call). Without it
 * the host uses the root host_load got, else <LocalState>/ares. */
void host_sysopt_set_root(const char *data_root);

/* How many options system_id has (0 for an unknown id). The list only changes when that system starts. */
int host_sysopt_count(const char *system_id);

/* Option `index` of system_id into *out; false when there is no such option. */
bool host_sysopt_get(const char *system_id, int index, SysOpt *out);

/* Chooses value `value_index` of option `index`, keeps it in <id>.cfg and, when system_id runs and the option applies
 * at once, has the core take it (asynchronously, before the next frame). False when out of range. */
bool host_sysopt_set(const char *system_id, int index, int value_index);

/* Every option of system_id back to its default (<id>.cfg removed); the running game takes the live ones at once. */
void host_sysopt_reset(const char *system_id);

/* true while a game of system_id runs. */
bool host_sysopt_running(const char *system_id);

/* How many options of the running system_id were changed and wait for the next start (0 when it does not run). */
int host_sysopt_pending(const char *system_id);

/* ---- frontend side (implemented in src/ui/settings_screen.c, ARES_UWP) ----------------------------------------------
 * The options page of one system: a list over the whole screen (the caller draws the backdrop first), its help on the
 * right, Cross / Left / Right change a value, Triangle sets the default, Square sets them all back, Circle leaves.
 * sysopts_page_open() before the first call; sysopts_page() returns false once the page was left. */
void sysopts_page_open(const char *system_id);
bool sysopts_page(uint32_t pressed);

#ifdef __cplusplus
}
#endif
#endif /* SWANUI_SYSOPTS_H */
