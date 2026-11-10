/* Tsugaru (FM Towns, from the SwanStation Xbox port) - the games folder chosen with the Xbox folder picker (see the header).
 * SPDX-License-Identifier: GPL-3.0-or-later */
#include "swanui_xbox_folder.h"
#include "app.h"
#include "i18n.h"

#include <stdio.h>
#include <string.h>

static char s_root[1024];
static bool s_loaded = false;

static void file_path(char *out, size_t size)
{
    snprintf(out, size, "%s/games-folder.txt", app.paths.root);
}

const char *swanui_xbox_games_root(void)
{
    if (!s_loaded)
    {
        s_loaded = true;
        char path[SwanStationPS5_PATH_MAX + 32];
        file_path(path, sizeof(path));
        FILE *f = fopen(path, "rb");
        if (f)
        {
            size_t n = fread(s_root, 1, sizeof(s_root) - 1, f);
            fclose(f);
            s_root[n] = '\0';
            while (n && (s_root[n - 1] == '\n' || s_root[n - 1] == '\r' || s_root[n - 1] == ' '))
                s_root[--n] = '\0';
        }
        if (s_root[0])
            plat_xbox_folder_access_restore();
    }
    return s_root;
}

int swanui_xbox_games_root_set(const char *path)
{
    char clean[sizeof(s_root)];
    snprintf(clean, sizeof(clean), "%s", path ? path : "");
    for (char *c = clean; *c; ++c)
        if (*c == '\\')
            *c = '/';
    size_t n = strlen(clean);
    while (n > 3 && clean[n - 1] == '/') /* keep "E:/" whole */
        clean[--n] = '\0';
    s_loaded = true;
    snprintf(s_root, sizeof(s_root), "%s", clean);

    char fpath[SwanStationPS5_PATH_MAX + 32];
    file_path(fpath, sizeof(fpath));
    FILE *f = fopen(fpath, "wb");
    if (!f)
        return 0;
    fputs(s_root, f);
    fclose(f);
    return 1;
}

void swanui_xbox_folder_poll(void)
{
    char picked[sizeof(s_root)];
    const int r = plat_xbox_pick_folder_poll(picked, sizeof(picked));
    if (r == 0)
        return;
    if (r < 0)
    {
        app_toast("No folder selected");
        return;
    }
    swanui_xbox_games_root_set(picked);
    app_toast("Games folder selected: looking for games...");
    app_rescan();
}

void swanui_xbox_apply_covers_dir(Paths *paths)
{
    const char *root = swanui_xbox_games_root();
    if (root[0])
        snprintf(paths->covers, sizeof(paths->covers), "%s/covers", root);
    else
        snprintf(paths->covers, sizeof(paths->covers), "%s/covers", paths->root);
    make_dirs(paths->covers);
}
