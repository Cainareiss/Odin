/*
 * Odin shell: the RetroAchievements interface the frontend calls, with no RetroAchievements code behind it. The
 * implementations are inert stubs in odin_host/odin_stubs.c (signed out, no achievements, no hardcore). The source of
 * the RetroAchievements package (src/ra) is not part of the Odin tree.
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#ifndef SwanStationPS5_ACHIEVEMENTS_H
#define SwanStationPS5_ACHIEVEMENTS_H

#include "SwanStationPS5.h"

void ra_init(const Paths *paths);
void ra_shutdown(void);
void ra_game_loaded(const char *game_path);
void ra_game_unloaded(void);
void ra_frame(void);
void ra_idle(void);
void ra_reset(void);
bool ra_signed_in(void);
const char *ra_user(void);
unsigned ra_user_score(void);
bool ra_game_progress(int *unlocked, int *total);

typedef struct
{
    char title[96];
    char description[192];
    char progress[24];
    float percent;
    char badge_url[160];
    unsigned points, id;
    bool unlocked;
} RaAchievement;

int ra_list(RaAchievement *out, int max);

typedef struct
{
    long long when;
    unsigned points;
    char game[96], title[96], description[192];
    bool hardcore;
} RaRecent;

int ra_recent(RaRecent *out, int max);
unsigned ra_user_hardcore_score(void);
unsigned ra_user_softcore_score(void);
void ra_use_token(const char *user, const char *token);
void ra_sign_out(void);
bool ra_hardcore(void);
void ra_set_hardcore(bool on);
bool ra_warning(bool *emulator, bool *version);
void ra_game_summary(char *out, size_t size);
bool ra_next_message(char *title, size_t title_size, char *detail, size_t detail_size, bool *quiet);
bool ra_tracker(char *title, size_t title_size, char *progress, size_t progress_size, float *percent);

#endif
