/*
 * Odin (experimental) v0.1 shell: inert implementations of the frontend interfaces that belonged to the ares build and
 * whose sources are not part of the Odin tree: RetroAchievements (ra/*.h stubs), GameShark cheats (swanui_ares_cheats.h),
 * the libretro-thumbnails art lists (swanui_ares_art.h), the FM Towns art and media (swanui_towns_*.h), the kiosk
 * (swanui_kiosk.h), the phone remote (remote.h) and updates (update.h). Each one answers "not available": nothing is
 * downloaded, nothing is signed in, no cheat is active. Compiled into the swanui library with its compat/ flags.
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "SwanStationPS5.h"
#include "ra/achievements.h"
#include "ra/login.h"
#include "swanui_ares_art.h"
#include "swanui_ares_cheats.h"
#include "swanui_kiosk.h"
#include "swanui_systems.h"
#include "swanui_towns_art.h"
#include "swanui_towns_media.h"
#include "remote.h"
#include "update.h"

#include <stdio.h>
#include <string.h>

/* ---- RetroAchievements: signed out, no set, no hardcore ------------------------------------------------------------ */

void ra_init(const Paths *paths)
{
    (void)paths;
}
void ra_shutdown(void)
{
}
void ra_game_loaded(const char *game_path)
{
    (void)game_path;
}
void ra_game_unloaded(void)
{
}
void ra_frame(void)
{
}
void ra_idle(void)
{
}
void ra_reset(void)
{
}
bool ra_signed_in(void)
{
    return false;
}
const char *ra_user(void)
{
    return "";
}
unsigned ra_user_score(void)
{
    return 0;
}
bool ra_game_progress(int *unlocked, int *total)
{
    if (unlocked)
        *unlocked = 0;
    if (total)
        *total = 0;
    return false;
}
int ra_list(RaAchievement *out, int max)
{
    (void)out;
    (void)max;
    return 0;
}
int ra_recent(RaRecent *out, int max)
{
    (void)out;
    (void)max;
    return 0;
}
unsigned ra_user_hardcore_score(void)
{
    return 0;
}
unsigned ra_user_softcore_score(void)
{
    return 0;
}
void ra_use_token(const char *user, const char *token)
{
    (void)user;
    (void)token;
}
void ra_sign_out(void)
{
}
bool ra_hardcore(void)
{
    return false;
}
void ra_set_hardcore(bool on)
{
    (void)on;
}
bool ra_warning(bool *emulator, bool *version)
{
    if (emulator)
        *emulator = false;
    if (version)
        *version = false;
    return false;
}
void ra_game_summary(char *out, size_t size)
{
    if (out && size)
        out[0] = '\0';
}
bool ra_next_message(char *title, size_t title_size, char *detail, size_t detail_size, bool *quiet)
{
    (void)title;
    (void)title_size;
    (void)detail;
    (void)detail_size;
    if (quiet)
        *quiet = true;
    return false;
}
bool ra_tracker(char *title, size_t title_size, char *progress, size_t progress_size, float *percent)
{
    (void)title;
    (void)title_size;
    (void)progress;
    (void)progress_size;
    (void)percent;
    return false;
}
bool ra_password_login(const char *name, const char *password, char *token, size_t token_size, char *who,
                       size_t who_size, char *error, size_t error_size)
{
    (void)name;
    (void)password;
    if (token && token_size)
        token[0] = '\0';
    if (who && who_size)
        who[0] = '\0';
    if (error && error_size)
        snprintf(error, error_size, "%s", "Sign-in is not available in this build.");
    return false;
}

/* ---- GameShark cheats: none ---------------------------------------------------------------------------------------- */

bool ares_cheat_supported(const char *system_id, const char *code)
{
    (void)system_id;
    (void)code;
    return false;
}
bool ares_cheat_disc_serial(const char *path, char *serial, size_t size)
{
    (void)path;
    if (serial && size)
        serial[0] = '\0';
    return false;
}
size_t ares_cheat_ram_size(void)
{
    return 0;
}
bool ares_cheat_ram_big_endian(void)
{
    return false;
}
bool ares_cheat_ram_read(uint8_t *dst, size_t offset, size_t size)
{
    (void)dst;
    (void)offset;
    (void)size;
    return false;
}
bool ares_cheat_ram_code(size_t offset, int bytes, unsigned value, char *code, size_t code_size, char *where,
                         size_t where_size)
{
    (void)offset;
    (void)bytes;
    (void)value;
    if (code && code_size)
        code[0] = '\0';
    if (where && where_size)
        where[0] = '\0';
    return false;
}

/* ---- libretro-thumbnails art: no lists, no downloads --------------------------------------------------------------- */

const char *ares_thumbs_repo(const char *system_id)
{
    (void)system_id;
    return NULL;
}
const char *ares_art_system_name(const char *system_id)
{
    const SwanSystem *s = swanui_system_by_id(system_id);
    return s ? s->name : NULL;
}
bool ares_art_index(const char *system_id, int kind, char *asset, size_t size)
{
    (void)system_id;
    (void)kind;
    if (asset && size)
        asset[0] = '\0';
    return false;
}
bool ares_art_best(const char *asset, const char *title, const char *file_name, char *name, size_t size)
{
    (void)asset;
    (void)title;
    (void)file_name;
    if (name && size)
        name[0] = '\0';
    return false;
}
int ares_art_score(const char *candidate, const char *title, const char *file_name)
{
    (void)candidate;
    (void)title;
    (void)file_name;
    return 0;
}
bool ares_art_url(const char *system_id, int kind, const char *name, char *url, size_t size)
{
    (void)system_id;
    (void)kind;
    (void)name;
    if (url && size)
        url[0] = '\0';
    return false;
}
bool ares_art_url_encode(char *out, size_t size, const char *s)
{
    (void)s;
    if (out && size)
        out[0] = '\0';
    return false;
}
bool ares_art_find(const char *system_id, int kind, const char *title, const char *file_name, char *name,
                   size_t name_size, char *url, size_t url_size)
{
    (void)system_id;
    (void)kind;
    (void)title;
    (void)file_name;
    if (name && name_size)
        name[0] = '\0';
    if (url && url_size)
        url[0] = '\0';
    return false;
}
NetResult ares_art_download(const char *url, const char *dest)
{
    (void)url;
    (void)dest;
    return NET_NOT_FOUND;
}

/* ---- FM Towns: not in Odin ---------------------------------------------------------------------------------------- */

int towns_art_url(const char *urls_asset, const char *name, char *out, size_t size)
{
    (void)urls_asset;
    (void)name;
    if (out && size)
        out[0] = '\0';
    return 0;
}
int towns_media_of_path(const char *path)
{
    (void)path;
    return TOWNS_MEDIA_CD;
}
int towns_companion_fd(const char *game_path, char *out, size_t size)
{
    (void)game_path;
    if (out && size)
        out[0] = '\0';
    return 0;
}
void towns_serial_from_title(const char *title, char *out, size_t size)
{
    (void)title;
    if (out && size)
        out[0] = '\0';
}

/* ---- kiosk: not in Odin (--kiosk shows the plain error panel) ------------------------------------------------------ */

static char s_kiosk_error[256];

void kiosk_init(const char *rom)
{
    (void)rom;
}
const char *kiosk_root(void)
{
    return "";
}
const char *kiosk_dir(void)
{
    return "";
}
const char *kiosk_bios_dir(void)
{
    return "";
}
void kiosk_set_error(const char *message)
{
    snprintf(s_kiosk_error, sizeof(s_kiosk_error), "%s", message ? message : "");
}
void kiosk_splash_frame(void)
{
}
void kiosk_frame(PadState pads[SwanStationPS5_MAX_PADS])
{
    (void)pads;
}
void kiosk_map_pads(const PadState in[SwanStationPS5_MAX_PADS], PadState out[SwanStationPS5_MAX_PADS])
{
    (void)in;
    (void)out;
}
void kiosk_set_focus(bool focus)
{
    (void)focus;
}
bool kiosk_has_focus(void)
{
    return false;
}
