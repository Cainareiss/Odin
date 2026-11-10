/*
 * SwanStationPS5 - RetroArch .cht cheat files (GameShark / Action Replay codes).
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Format (libretro-database):
 *   cheats = 2
 *   cheat0_desc = "Infinite HP"
 *   cheat0_code = "800A1234 03E7+800A1236 03E7"
 *   cheat0_enable = false
 *
 * ARES_UWP: every system of the port (Game.system). The PlayStation keeps the path below (cheats-index.txt, then
 * DuckStation's chtdb by the disc's serial, which the host reads from the disc); the other systems pick their files
 * from assets/cheats/<id>.txt by title (ares_cheats_load), merging the game's plain, Game Genie, Action Replay,
 * GameShark... files. Codes the host's engine does not read are left out (compat/swanui_ares_cheats.h).
 * See ARES_CHEATS_NOTES.md.
 */
#include "cheats.h"

#include "disc.h"
#include "i18n.h"

#include "core/host.h"
#include "net.h"
#include "platform/platform.h"
#include "ra/achievements.h"

#if defined(ARES_UWP)
#include "swanui_ares_art.h"    /* ares_art_score / ares_art_url_encode: the title matcher of the art lists */
#include "swanui_ares_cheats.h" /* ares_cheat_supported, ares_cheat_disc_serial */
#endif

#include <SDL2/SDL.h>

#include <ctype.h>
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <utime.h>

/* ---------------------------------------------------------------- parsing */

static char *trim(char *s)
{
    while (isspace((unsigned char)*s))
        ++s;
    char *end = s + strlen(s);
    while (end > s && isspace((unsigned char)end[-1]))
        --end;
    *end = '\0';
    return s;
}

static void unquote(char *dst, size_t size, char *value)
{
    value = trim(value);
    size_t n = strlen(value);
    if (n >= 2 && value[0] == '"' && value[n - 1] == '"')
    {
        value[n - 1] = '\0';
        ++value;
    }
    str_copy(dst, size, value);
}

static bool parse_cht(CheatList *list, const char *path)
{
    FILE *f = fopen(path, "r");
    if (!f)
        return false;
    char line[1024];
    while (fgets(line, sizeof(line), f))
    {
        char *eq = strchr(line, '=');
        if (!eq || strncmp(line, "cheat", 5) != 0 || !isdigit((unsigned char)line[5]))
            continue;
        *eq = '\0';
        char *key = trim(line);
        int index = atoi(key + 5);
        if (index < 0 || index >= CHEATS_MAX)
            continue;
        const char *field = strchr(key, '_');
        if (!field)
            continue;
        ++field;
        Cheat *c = &list->items[index];
        if (strcmp(field, "desc") == 0)
            unquote(c->desc, sizeof(c->desc), eq + 1);
        else if (strcmp(field, "code") == 0)
            unquote(c->code, sizeof(c->code), eq + 1);
        else if (strcmp(field, "enable") == 0)
            c->enabled = strstr(eq + 1, "true") != NULL;
        if (index + 1 > list->count)
            list->count = index + 1;
    }
    fclose(f);

    /* Drop holes and entries without a code. */
    int w = 0;
    for (int i = 0; i < list->count; ++i)
    {
        if (!list->items[i].code[0])
            continue;
        if (!list->items[i].desc[0])
            snprintf(list->items[i].desc, sizeof(list->items[i].desc), tr("Code %d"), i + 1);
        list->items[w++] = list->items[i];
    }
    list->count = w;
    return w > 0;
}

/* DuckStation's chtdb format: [Name] (groups as "Group\\Name"), then
 * "Key = Value" options, then the code lines. GameShark codes only. */
static bool parse_chtdb(CheatList *list, const char *path, bool patch)
{
    FILE *f = fopen(path, "r");
    if (!f)
        return false;
    char line[512];
    Cheat cur;
    bool open = false, gameshark = false, too_long = false;
    memset(&cur, 0, sizeof(cur));
    for (bool more = true; more;)
    {
        more = fgets(line, sizeof(line), f) != NULL;
        char *t = more ? trim(line) : NULL;
        if (!more || t[0] == '[')
        {
            /* the previous code is complete */
            /* "Use 8MB RAM..." needs the dev kit's memory, which neither emulator has */
            if (open && gameshark && !too_long && cur.code[0] && list->count < CHEATS_MAX && !strstr(cur.desc, "8MB"))
            {
                cur.patch = patch;
                list->items[list->count++] = cur;
            }
            if (!more)
                break;
            memset(&cur, 0, sizeof(cur));
            open = true;
            gameshark = false;
            too_long = false;
            char *end = strrchr(t, ']');
            if (end)
                *end = '\0';
            /* "Baofu\Infinite HP" -> "Baofu: Infinite HP" */
            size_t w = 0;
            for (const char *p = t + 1; *p && w + 3 < sizeof(cur.desc); ++p)
            {
                if (*p == '\\')
                {
                    cur.desc[w++] = ':';
                    cur.desc[w++] = ' ';
                }
                else
                    cur.desc[w++] = *p;
            }
            cur.desc[w] = '\0';
            continue;
        }
        if (!open || !t[0] || t[0] == ';' || t[0] == '#')
            continue;
        char *eq = strchr(t, '=');
        if (eq)
        {
            *eq = '\0';
            if (!strcmp(trim(t), "Type"))
                gameshark = !str_icmp(trim(eq + 1), "Gameshark");
            continue;
        }
        /* a code line: "800833A8 FFFF" */
        size_t len = strlen(t), have = strlen(cur.code);
        if (have + len + 2 >= sizeof(cur.code))
        {
            too_long = true;
            continue;
        }
        if (have)
            cur.code[have++] = '+';
        memcpy(cur.code + have, t, len + 1);
    }
    fclose(f);
    return list->count > 0;
}

/* ---------------------------------------------------------------- matching */

/* A title as words: lower case, without the parts in brackets, roman numerals
 * as digits, and without the words release names add or drop ("the", "of",
 * "Disney-Pixar's"...). "Final Fantasy VII (USA) (Disc 1)" -> final fantasy 7 */
#define MAX_WORDS 24
typedef struct
{
    char w[MAX_WORDS][24];
    int n;
} Words;

static void title_words(const char *in, Words *out)
{
    static const char *const stop[] = {"the", "a", "an", "of", "and", "s", "disney", "pixar", NULL};
    static const char *const roman[][2] = {{"ii", "2"},  {"iii", "3"}, {"iv", "4"},  {"v", "5"}, {"vi", "6"},
                                           {"vii", "7"}, {"viii", "8"}, {"ix", "9"}, {"x", "10"}};
    out->n = 0;
    int depth = 0;
    char word[24];
    size_t len = 0;
    for (const char *p = in;; ++p)
    {
        char c = *p;
        if (c == '(' || c == '[')
            ++depth;
        else if ((c == ')' || c == ']') && depth > 0)
            --depth;
        bool letter = c && depth == 0 && isalnum((unsigned char)c);
        if (letter && len + 1 < sizeof(word))
        {
            word[len++] = (char)tolower((unsigned char)c);
            continue;
        }
        if (letter)
            continue; /* overlong word: truncated */
        if (len)
        {
            word[len] = '\0';
            len = 0;
            bool skip = false;
            for (int i = 0; stop[i] && !skip; ++i)
                skip = strcmp(word, stop[i]) == 0;
            for (size_t i = 0; i < sizeof(roman) / sizeof(roman[0]); ++i)
                if (strcmp(word, roman[i][0]) == 0)
                    str_copy(word, sizeof(word), roman[i][1]);
            if (!skip && out->n < MAX_WORDS)
                str_copy(out->w[out->n++], sizeof(out->w[0]), word);
        }
        if (!c)
            break;
    }
}

static bool has_word(const Words *w, const char *word)
{
    for (int i = 0; i < w->n; ++i)
        if (strcmp(w->w[i], word) == 0)
            return true;
    return false;
}

/* 0, or up to 300 for the same words: every word of the shorter title is in
 * the longer, the numbers are the same (Crash 2 isn't Crash 3) and most words
 * are shared ("Legend of Dragoon" isn't "Legend"). */
static int title_match(const Words *a, const Words *b)
{
    if (!a->n || !b->n)
        return 0;
    char na[64] = "", nb[64] = "";
    for (int i = 0; i < a->n; ++i)
        if (isdigit((unsigned char)a->w[i][0]))
            snprintf(na + strlen(na), sizeof(na) - strlen(na), "%s.", a->w[i]);
    for (int i = 0; i < b->n; ++i)
        if (isdigit((unsigned char)b->w[i][0]))
            snprintf(nb + strlen(nb), sizeof(nb) - strlen(nb), "%s.", b->w[i]);
    if (strcmp(na, nb) != 0)
        return 0;
    const Words *small = a->n <= b->n ? a : b, *large = a->n <= b->n ? b : a;
    int shared = 0;
    for (int i = 0; i < small->n; ++i)
    {
        bool dup = false;
        for (int j = 0; j < i && !dup; ++j)
            dup = strcmp(small->w[j], small->w[i]) == 0;
        if (dup)
            continue;
        if (!has_word(large, small->w[i]))
            return 0;
        ++shared;
    }
    int unique_large = 0;
    for (int i = 0; i < large->n; ++i)
    {
        bool dup = false;
        for (int j = 0; j < i && !dup; ++j)
            dup = strcmp(large->w[j], large->w[i]) == 0;
        unique_large += !dup;
    }
    /* Jaccard: shared / union, union = the larger set here */
    int score = unique_large ? 300 * shared / unique_large : 0;
    return score >= 210 ? score : 0; /* at least 70 % */
}

bool titles_match(const char *a, const char *b)
{
    Words wa, wb;
    title_words(a, &wa);
    title_words(b, &wb);
    return title_match(&wa, &wb) >= 300; /* the same words */
}

static const char *region_of_serial(const char *serial)
{
    if (!serial[0])
        return NULL;
    if (strncmp(serial, "SLUS", 4) == 0 || strncmp(serial, "SCUS", 4) == 0)
        return "(USA";
    if (strncmp(serial, "SLES", 4) == 0 || strncmp(serial, "SCES", 4) == 0 ||
        strncmp(serial, "SCED", 4) == 0)
        return "Europe";
    return "Japan";
}

#if defined(ARES_UWP)
/* ARES_UWP: Game.serial is the stable id ("PS1-1A2B3C4D"), not the disc's: the host reads the PlayStation disc's
 * serial from SYSTEM.CNF, once per game (game_serial(game, true) in cheats_load / cheats_mark_old). */
static char serial_path[SwanStationPS5_PATH_MAX], serial_value[16];

static const char *game_serial(const Game *game, bool read)
{
    if (strcmp(game->path, serial_path) != 0)
    {
        if (!read || strcmp(game->system, "ps1") != 0 || !game->path[0])
            return "";
        str_copy(serial_path, sizeof(serial_path), game->path);
        char raw[32];
        if (!ares_cheat_disc_serial(game->path, raw, sizeof(raw)) ||
            !disc_format_serial(raw, serial_value, sizeof(serial_value)))
            serial_value[0] = '\0';
        SwanStationPS5_log("cheats: disc serial %s", serial_value[0] ? serial_value : "unknown");
    }
    return serial_value;
}

/* The release's region: from the disc's serial when it was read, else from the file name's tags. */
static const char *game_region(const Game *game)
{
    const char *serial = game_serial(game, false);
    if (serial[0])
        return region_of_serial(serial);
    const char *name = game->disc_name[0] ? game->disc_name : game->title;
    if (strstr(name, "(USA"))
        return "(USA";
    if (strstr(name, "Europe"))
        return "Europe";
    if (strstr(name, "Japan"))
        return "Japan";
    return NULL;
}
#else
static const char *game_serial(const Game *game, bool read)
{
    (void)read;
    return game->serial;
}

static const char *game_region(const Game *game)
{
    return region_of_serial(game->serial);
}
#endif

typedef struct
{
    Words title, disc;
} Wanted;

static void wanted_for(const Game *game, Wanted *w)
{
    title_words(game->title, &w->title);
    title_words(game->disc_name, &w->disc);
}

/* How well a cheat file name fits the game: 0 for another game. */
static int score_candidate(const char *file, const Game *game, const Wanted *want)
{
    char stem[256];
    str_copy(stem, sizeof(stem), file);
    char *dot = strrchr(stem, '.');
    if (dot)
        *dot = '\0';
    if (str_icmp(stem, game->disc_name) == 0)
        return 1000; /* exact No-Intro / Redump name */
    Words have;
    title_words(stem, &have);
    int a = title_match(&want->title, &have), b = title_match(&want->disc, &have);
    int score = a > b ? a : b;
    if (!score)
        return 0;
    /* The disc's region beats other releases; "World" is the next best. */
    const char *region = game_region(game);
    if (region && strstr(stem, region))
        score += 100;
    else if (strstr(stem, "(World)"))
        score += 90;
    else if (!region && strstr(stem, "(USA"))
        score += 20;
    /* GameShark sets are the full ones (files without a device are often a
     * code or two): one beats a plain file of the disc's own region. */
    if (strstr(stem, "(GameShark)"))
        score += 15;
    /* Codes are usually filed under disc 1 or under no disc number. */
    if (strstr(stem, "(Disc 1)") || !strstr(stem, "(Disc"))
        score += 10;
    /* demos and previews have other addresses: only for a demo disc */
    static const char *const trial[] = {"(Demo)", "(Preview)", "(Beta)", "(Proto", "(Sample)"};
    for (size_t i = 0; i < sizeof(trial) / sizeof(trial[0]); ++i)
        if (strstr(stem, trial[i]) && !strstr(game->disc_name, trial[i]) && !strstr(game->title, trial[i]))
            return 0;
    return score;
}

static bool find_in_dir(const char *dir, const Game *game, char *best_path, size_t size,
                        int *best_score)
{
    DIR *d = opendir(dir);
    if (!d)
        return false;
    Wanted want;
    wanted_for(game, &want);
    bool found = false;
    struct dirent *e;
    while ((e = readdir(d)))
    {
        if (str_icmp(path_ext(e->d_name), "cht") != 0)
            continue;
        int score = score_candidate(e->d_name, game, &want);
        if (score > *best_score)
        {
            *best_score = score;
            path_join(best_path, size, dir, e->d_name);
            found = true;
        }
    }
    closedir(d);
    return found;
}

/* ---------------------------------------------------------------- the built-in index */

/* assets/cheats-index.txt: every PlayStation cheat file of libretro-database
 * (tools/make-cheat-index.py; ARES_UWP: tools/make_ares_cheat_index.py). The best name for the game, without listing
 * any folder. */
bool cheats_best_in_index(const Game *game, const char *asset, char *name, size_t size)
{
    char path[SwanStationPS5_PATH_MAX];
    plat_asset_path(path, sizeof(path), asset);
    FILE *f = fopen(path, "r");
    if (!f)
        return false;
    Wanted want;
    wanted_for(game, &want);
    char line[256];
    int best = 0;
    while (fgets(line, sizeof(line), f))
    {
        line[strcspn(line, "\r\n")] = '\0';
        if (!line[0] || line[0] == '#')
            continue;
        int score = score_candidate(line, game, &want);
        if (score > best)
        {
            best = score;
            str_copy(name, size, line);
        }
    }
    fclose(f);
    return best > 0;
}

/* One file from libretro-database, on a thread: the game starts meanwhile and
 * its cheats appear as soon as the file is in (cheats_fetch_finished). */
static SDL_atomic_t fetch_state; /* 0 idle, 1 downloading, 2 done */
static char fetch_url[600], fetch_dest[SwanStationPS5_PATH_MAX];

static int fetch_main(void *unused)
{
    (void)unused;
    char temp[SwanStationPS5_PATH_MAX + 8];
    snprintf(temp, sizeof(temp), "%s.part", fetch_dest);
    NetResult r = net_download(fetch_url, temp);
    bool ok = r == NET_OK && rename(temp, fetch_dest) == 0;
    if (!ok)
        remove(temp);
    if (r == NET_NOT_FOUND)
        utime(fetch_dest, NULL);
    SwanStationPS5_log("cheats: download %s: %s", fetch_dest, ok ? "ok" : r == NET_NOT_FOUND ? "not found" : "failed");
    SDL_AtomicSet(&fetch_state, ok ? 2 : 0);
    return 0;
}

static const char LIBRETRO_BASE[] =
    "https://raw.githubusercontent.com/libretro/libretro-database/master/cht/Sony%20-%20PS1/";
static const char CHTDB_BASE[] = "Odin: see the credits file in the package";
static const char PATCHES_BASE[] = "Odin: see the credits file in the package";

static void fetch_start(const char *base, const char *name, const char *dest)
{
    if (!net_available() || !SDL_AtomicCAS(&fetch_state, 0, 1))
        return;
    str_copy(fetch_url, sizeof(fetch_url), base);
    size_t w = strlen(fetch_url);
    for (const unsigned char *p = (const unsigned char *)name; *p && w + 4 < sizeof(fetch_url); ++p)
    {
        if (isalnum(*p) || strchr("-._~", *p))
            fetch_url[w++] = (char)*p;
        else
            w += (size_t)snprintf(fetch_url + w, sizeof(fetch_url) - w, "%%%02X", *p);
    }
    fetch_url[w] = '\0';
    str_copy(fetch_dest, sizeof(fetch_dest), dest);
    SDL_Thread *t = SDL_CreateThread(fetch_main, "cheat-download", NULL);
    if (t)
        SDL_DetachThread(t);
    else
        SDL_AtomicSet(&fetch_state, 0);
}

#define CHEAT_REFRESH_SECONDS (24 * 60 * 60)

static void refresh_if_old(const char *base, const char *name, const char *path)
{
    struct stat st;
    if (stat(path, &st) == 0 && time(NULL) - st.st_mtime > CHEAT_REFRESH_SECONDS)
        fetch_start(base, name, path);
}

static void mark_old(const char *path)
{
    struct utimbuf old = {1, 1};
    utime(path, &old);
}

bool cheats_fetch_finished(void)
{
    return SDL_AtomicCAS(&fetch_state, 2, 0);
}

/* assets/chtdb-index.txt and chtdb-patches-index.txt (tools/make-chtdb-index.py):
 * serial<TAB>file. */
static bool chtdb_file_for(const char *index, const char *serial, char *name, size_t size)
{
    char path[SwanStationPS5_PATH_MAX];
    plat_asset_path(path, sizeof(path), index);
    FILE *f = fopen(path, "r");
    if (!f)
        return false;
    char line[160];
    size_t n = strlen(serial);
    bool hit = false;
    while (!hit && fgets(line, sizeof(line), f))
        if (!strncmp(line, serial, n) && line[n] == '\t')
        {
            line[strcspn(line, "\r\n")] = '\0';
            str_copy(name, size, line + n + 1);
            hit = true;
        }
    fclose(f);
    return hit;
}

#if !defined(ARES_UWP)
static bool find_any_cht(const char *dir, char *out, size_t size)
{
    DIR *d = opendir(dir);
    if (!d)
        return false;
    struct dirent *e;
    bool found = false;
    while (!found && (e = readdir(d)))
        if (str_icmp(path_ext(e->d_name), "cht") == 0)
        {
            path_join(out, size, dir, e->d_name);
            found = true;
        }
    closedir(d);
    return found;
}
#endif

/* ---------------------------------------------------------------- selection */

static void load_selection(CheatList *list)
{
    FILE *f = fopen(list->state_path, "r");
    if (!f)
        return;
    for (int i = 0; i < list->count; ++i)
        list->items[i].enabled = false;
    char line[256];
    while (fgets(line, sizeof(line), f))
    {
        char *desc = trim(line);
        for (int i = 0; i < list->count; ++i)
            if (strcmp(list->items[i].desc, desc) == 0)
                list->items[i].enabled = true;
    }
    fclose(f);
}

void cheats_save_selection(const CheatList *list)
{
    if (!list->state_path[0])
        return;
    FILE *f = fopen(list->state_path, "w");
    if (!f)
        return;
    for (int i = 0; i < list->count; ++i)
        if (list->items[i].enabled)
            fprintf(f, "%s\n", list->items[i].desc);
    fclose(f);
}

/* ---------------------------------------------------------------- API */

void cheats_clear(CheatList *list)
{
    list->count = 0;
    list->source[0] = '\0';
    list->state_path[0] = '\0';
    host_cheat_reset(); /* nothing stays on in the core (also when hardcore mode turns on) */
}

static bool file_opens(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (f)
        fclose(f);
    return f != NULL;
}

/* cheats/mine/<game id>.txt: desc<TAB>code per line, made by Find a code */
static void user_file(const Game *game, const char *cheats_dir, char *dir, size_t dir_size, char *path,
                      size_t path_size, char *file, size_t file_size)
{
    path_join(dir, dir_size, cheats_dir, "mine");
    snprintf(file, file_size, "%.80s.txt", game->id);
    path_join(path, path_size, dir, file);
}

static void load_user(CheatList *list, const Game *game, const char *cheats_dir)
{
    char dir[SwanStationPS5_PATH_MAX], path[SwanStationPS5_PATH_MAX], file[96], line[400];
    user_file(game, cheats_dir, dir, sizeof(dir), path, sizeof(path), file, sizeof(file));
    FILE *f = fopen(path, "r");
    while (f && fgets(line, sizeof(line), f) && list->count < CHEATS_MAX)
    {
        line[strcspn(line, "\r\n")] = '\0';
        char *tab = strchr(line, '\t');
        if (!tab || !tab[1])
            continue;
        *tab = '\0';
        Cheat *c = &list->items[list->count++];
        memset(c, 0, sizeof(*c));
        str_copy(c->desc, sizeof(c->desc), line);
        str_copy(c->code, sizeof(c->code), tab + 1);
    }
    if (f)
        fclose(f);
}

/* the selection file and the log line once the list is complete; false for an empty list */
static bool finish_load(CheatList *list, const Game *game, const char *cheats_dir, int from_library,
                        const char *library_path)
{
    load_user(list, game, cheats_dir);
    if (!list->count)
        return false;
    char enabled_dir[SwanStationPS5_PATH_MAX], file[96];
    path_join(enabled_dir, sizeof(enabled_dir), cheats_dir, "enabled");
    make_dirs(enabled_dir);
    snprintf(file, sizeof(file), "%.80s.txt", game->id);
    path_join(list->state_path, sizeof(list->state_path), enabled_dir, file);
    load_selection(list);
    SwanStationPS5_log("cheats: %d codes from %s, %d of your own", from_library, library_path,
                       list->count - from_library);
    return true;
}

#if defined(ARES_UWP)
/* ---------------------------------------------------------------- ARES_UWP: the systems */

/* Codes of items [from, to) the host's engine can't read (encrypted, "??" left to fill in, a format of another
 * system) are dropped: a switch that does nothing helps nobody. Returns how many went. */
static int drop_unsupported(CheatList *list, int from, int to, const char *system)
{
    int w = from, dropped = 0;
    for (int i = from; i < to; ++i)
    {
        if (!ares_cheat_supported(system, list->items[i].code))
        {
            ++dropped;
            continue;
        }
        list->items[w++] = list->items[i];
    }
    for (int i = to; i < list->count; ++i)
        list->items[w++] = list->items[i];
    list->count = w;
    return dropped;
}

/* A .cht beside the game file with its name ("Super Mario World (USA).cht"), else the best named one in its folder
 * (a folder may hold many games of a system: never just any .cht there). */
static bool cht_beside_game(const Game *game, const char *cheats_dir, char *path, size_t size)
{
    if (!game->folder[0] || !game->path[0])
        return false;
    const char *slash = strrchr(game->path, '/');
    char stem[256];
    str_copy(stem, sizeof(stem), slash ? slash + 1 : game->path);
    char *dot = strrchr(stem, '.');
    if (dot)
        *dot = '\0';
    char file[270];
    snprintf(file, sizeof(file), "%s.cht", stem);
    path_join(path, size, game->folder, file);
    if (file_opens(path))
        return true;
    int score = 0;
    return strcmp(game->folder, cheats_dir) != 0 && find_in_dir(game->folder, game, path, size, &score);
}

typedef struct
{
    const char *id;
    const char *folder; /* libretro-database cht/<folder>/ */
    const char *list;   /* assets/cheats/<list>.txt */
    const char *alt;    /* also looked in: the same cartridges filed under the companion system */
} CheatSystem;

/* Keep in step with tools/make_ares_cheat_index.py FOLDERS. No libretro cheats: a26 msx msx2 cv spec ng myvision
 * pcv2 (their files hold RetroArch memory cheats for other cores' memory maps, or none) and ws wsc (28-bit physical
 * addresses nothing reads). */
static const CheatSystem CHEAT_SYSTEMS[] = {
    {"fc", "Nintendo - Nintendo Entertainment System", "fc", NULL},
    {"fds", "Nintendo - Family Computer Disk System", "fds", NULL},
    {"sfc", "Nintendo - Super Nintendo Entertainment System", "sfc", NULL},
    {"gb", "Nintendo - Game Boy", "gb", "gbc"},
    {"gbc", "Nintendo - Game Boy Color", "gbc", "gb"},
    {"gba", "Nintendo - Game Boy Advance", "gba", NULL},
    {"sg", "Sega - SG-1000", "sg", NULL},
    {"sc3000", "Sega - SG-1000", "sg", NULL},
    {"ms", "Sega - Master System - Mark III", "ms", NULL},
    {"gg", "Sega - Game Gear", "gg", NULL},
    {"md", "Sega - Mega Drive - Genesis", "md", NULL},
    {"mcd", "Sega - Mega-CD - Sega CD", "mcd", NULL},
    {"32x", "Sega - 32X", "32x", NULL},
    {"pce", "NEC - PC Engine - TurboGrafx 16", "pce", "sgx"},
    {"sgx", "NEC - PC Engine SuperGrafx", "sgx", "pce"},
    {"pcecd", "NEC - PC Engine CD - TurboGrafx-CD", "pcecd", NULL},
    {"ngp", "SNK - Neo Geo Pocket", "ngp", "ngpc"}, /* no list of its own: every code there has "??" */
    {"ngpc", "SNK - Neo Geo Pocket Color", "ngpc", "ngp"},
};

static const CheatSystem *cheat_system(const char *id)
{
    for (size_t i = 0; id && i < sizeof(CHEAT_SYSTEMS) / sizeof(CHEAT_SYSTEMS[0]); ++i)
        if (strcmp(CHEAT_SYSTEMS[i].id, id) == 0)
            return &CHEAT_SYSTEMS[i];
    return NULL;
}

/* The cheat device a file is for: its codes are merged with " [device]" after the description. */
enum
{
    FAM_PLAIN,
    FAM_GENIE,
    FAM_REPLAY,
    FAM_SHARK,
    FAM_BREAKER,
    FAM_XPLODER,
    FAM_BUSTER,
    FAM_COUNT
};
static const char *const FAMILY_TAG[FAM_COUNT][3] = {
    {NULL, NULL, NULL},
    {" (Game Genie)", NULL, NULL},
    {" (Action Replay)", " (Pro Action Replay)", NULL},
    {" (GameShark)", " Game Shark", NULL},
    {" (Code Breaker)", NULL, NULL},
    {" (Xploder)", NULL, NULL},
    {" (Game Buster)", NULL, NULL},
};
static const char *const FAMILY_NAME[FAM_COUNT] = {"",           "Game Genie", "Action Replay", "GameShark",
                                                   "Code Breaker", "Xploder",    "Game Buster"};

/* the device of a list name, and the name without that tag (for the title match) */
static int family_of(const char *name, char *base, size_t size)
{
    str_copy(base, size, name);
    for (int f = 1; f < FAM_COUNT; ++f)
        for (int k = 0; k < 3 && FAMILY_TAG[f][k]; ++k)
        {
            char *at = strstr(base, FAMILY_TAG[f][k]);
            if (at)
            {
                memmove(at, at + strlen(FAMILY_TAG[f][k]), strlen(at + strlen(FAMILY_TAG[f][k])) + 1);
                return f;
            }
        }
    return FAM_PLAIN;
}

typedef struct
{
    char name[256]; /* the list name: the file is <name>.cht */
    const CheatSystem *sys;
    int score;      /* the title match without the exact-name bonus */
    bool exact;     /* the game file's own No-Intro / Redump name */
    int family;
} CheatFile;

#define ARES_CHEAT_FILES 4

static bool ranks_above(const CheatFile *a, const CheatFile *b)
{
    return a->exact != b->exact ? a->exact : a->score > b->score;
}

/* The best file of each device in a system's list. */
static void score_list(const CheatSystem *sys, const char *title, const char *file_name, const char *exact_name,
                       CheatFile best[FAM_COUNT])
{
    char asset[64], path[SwanStationPS5_PATH_MAX], line[512], base[512];
    snprintf(asset, sizeof(asset), "cheats/%s.txt", sys->list);
    plat_asset_path(path, sizeof(path), asset);
    FILE *f = fopen(path, "r");
    if (!f)
        return;
    while (fgets(line, sizeof(line), f))
    {
        line[strcspn(line, "\r\n")] = '\0';
        if (!line[0] || line[0] == '#')
            continue;
        const int fam = family_of(line, base, sizeof(base));
        /* file_name ends in a space: the title matcher's exact-name shortcut (2000) stays out, so the files of one
         * game compare by region and revision; exactness is ranked on its own */
        CheatFile c;
        c.score = ares_art_score(base, title, file_name);
        if (c.score <= 0)
            continue;
        c.exact = exact_name[0] && str_icmp(base, exact_name) == 0;
        c.sys = sys;
        c.family = fam;
        str_copy(c.name, sizeof(c.name), line);
        if (!best[fam].score || ranks_above(&c, &best[fam]))
            best[fam] = c;
    }
    fclose(f);
}

/* The game's files in assets/cheats/<id>.txt (and the companion system's list): the best one, then the best of each
 * other device within 40 points of it (the same release: "(USA) (Game Genie)", "(World) (Action Replay)"; another
 * region's codes rarely fit). Best first; returns how many. */
static int ares_cheat_files(const Game *game, CheatFile *out, int max)
{
    const CheatSystem *sys = cheat_system(game->system);
    if (!sys)
        return 0;
    char file_name[200], exact[200];
    const char *name = game->disc_name;
    char stem[200];
    if (!name[0])
    {
        const char *slash = strrchr(game->path, '/');
        str_copy(stem, sizeof(stem), slash ? slash + 1 : game->path);
        char *dot = strrchr(stem, '.');
        if (dot)
            *dot = '\0';
        name = stem;
    }
    snprintf(file_name, sizeof(file_name), "%s ", name);
    size_t n = 0;
    for (; name[n] && n + 1 < sizeof(exact); ++n)
        exact[n] = strchr("&*/:`<>?\\|\"", name[n]) ? '_' : name[n]; /* libretro's file names */
    exact[n] = '\0';

    CheatFile *best = calloc(FAM_COUNT, sizeof(CheatFile));
    if (!best)
        return 0;
    score_list(sys, game->title, file_name, exact, best);
    const CheatSystem *alt = sys->alt ? cheat_system(sys->alt) : NULL;
    if (alt)
    {
        CheatFile *other = calloc(FAM_COUNT, sizeof(CheatFile));
        if (other)
        {
            score_list(alt, game->title, file_name, exact, other);
            /* the companion's list only where the system's own has nothing as good */
            for (int f = 0; f < FAM_COUNT; ++f)
                if (other[f].score && (!best[f].score || ranks_above(&other[f], &best[f])))
                    best[f] = other[f];
            free(other);
        }
    }
    int top = -1;
    for (int f = 0; f < FAM_COUNT; ++f)
        if (best[f].score && (top < 0 || ranks_above(&best[f], &best[top])))
            top = f;
    int count = 0;
    if (top >= 0)
    {
        out[count++] = best[top];
        while (count < max)
        {
            int next = -1;
            for (int f = 0; f < FAM_COUNT; ++f)
            {
                bool taken = false;
                for (int k = 0; k < count && !taken; ++k)
                    taken = out[k].family == f;
                if (taken || !best[f].score || best[f].score < best[top].score - 40)
                    continue;
                if (next < 0 || ranks_above(&best[f], &best[next]))
                    next = f;
            }
            if (next < 0)
                break;
            out[count++] = best[next];
        }
    }
    free(best);
    return count;
}

/* cheats/<libretro folder>/ and the download address of that folder */
static void cheat_folder(const char *cheats_dir, const CheatSystem *sys, char *dir, size_t size)
{
    path_join(dir, size, cheats_dir, sys->folder);
}

static void cheat_base_url(const CheatSystem *sys, char *url, size_t size)
{
    static const char ROOT[] = "https://raw.githubusercontent.com/libretro/libretro-database/master/cht/";
    char folder[256];
    if (!ares_art_url_encode(folder, sizeof(folder), sys->folder))
        folder[0] = '\0';
    snprintf(url, size, "%s%s/", ROOT, folder);
}

/* One .cht merged into the list: supported codes only, a code already there once, the device after the
 * description. True when it brought a code. */
static bool merge_cht(CheatList *list, CheatList *tmp, const char *path, int family, const char *system,
                      int *dropped)
{
    memset(tmp, 0, sizeof(*tmp));
    if (!parse_cht(tmp, path))
        return false;
    int added = 0;
    for (int i = 0; i < tmp->count && list->count < CHEATS_MAX; ++i)
    {
        const Cheat *c = &tmp->items[i];
        if (!ares_cheat_supported(system, c->code))
        {
            ++*dropped;
            continue;
        }
        bool dup = false;
        for (int j = 0; j < list->count && !dup; ++j)
            dup = str_icmp(list->items[j].code, c->code) == 0;
        if (dup)
            continue;
        Cheat *d = &list->items[list->count++];
        *d = *c;
        if (family != FAM_PLAIN)
            snprintf(d->desc, sizeof(d->desc), "%.64s [%s]", c->desc, FAMILY_NAME[family]);
        ++added;
    }
    return added > 0;
}

/* Every system but the PlayStation: a .cht beside the game, else the game's libretro-database files (downloaded to
 * cheats/<folder>/ the first time, one a time: cheats_fetch_finished loads again and starts the next), else the best
 * match in a cheats/<folder>/ the player filled. Then the player's own codes. */
static bool ares_cheats_load(CheatList *list, const Game *game, const char *cheats_dir)
{
    const CheatSystem *sys = cheat_system(game->system);
    char path[SwanStationPS5_PATH_MAX], first[SwanStationPS5_PATH_MAX] = "";
    int files = 0, dropped = 0;
    CheatList *tmp = malloc(sizeof(CheatList));
    if (tmp && cht_beside_game(game, cheats_dir, path, sizeof(path)) &&
        merge_cht(list, tmp, path, FAM_PLAIN, game->system, &dropped))
    {
        files = 1;
        str_copy(first, sizeof(first), path);
    }
    if (tmp && !files && sys)
    {
        CheatFile cand[ARES_CHEAT_FILES];
        int n = ares_cheat_files(game, cand, ARES_CHEAT_FILES);
        for (int i = 0; i < n; ++i)
        {
            char dir[SwanStationPS5_PATH_MAX], file[270], base[400];
            cheat_folder(cheats_dir, cand[i].sys, dir, sizeof(dir));
            snprintf(file, sizeof(file), "%s.cht", cand[i].name);
            path_join(path, sizeof(path), dir, file);
            cheat_base_url(cand[i].sys, base, sizeof(base));
            if (file_opens(path))
            {
                if (merge_cht(list, tmp, path, cand[i].family, game->system, &dropped) && !files++)
                    str_copy(first, sizeof(first), path);
                refresh_if_old(base, file, path);
            }
            else
            {
                make_dirs(dir);
                fetch_start(base, file, path); /* if no other download is running */
            }
        }
        if (!n)
        {
            char dir[SwanStationPS5_PATH_MAX];
            int score = 0;
            cheat_folder(cheats_dir, sys, dir, sizeof(dir));
            if (find_in_dir(dir, game, path, sizeof(path), &score) &&
                merge_cht(list, tmp, path, FAM_PLAIN, game->system, &dropped))
            {
                files = 1;
                str_copy(first, sizeof(first), path);
            }
        }
    }
    free(tmp);
    if (files)
        str_copy(list->source, sizeof(list->source), first);
    if (dropped)
        SwanStationPS5_log("cheats: %d codes left out (a format the engine does not read)", dropped);
    char where[SwanStationPS5_PATH_MAX + 32];
    snprintf(where, sizeof(where), files > 1 ? "%s and %d more" : "%s", files ? first : "nowhere", files - 1);
    return finish_load(list, game, cheats_dir, list->count, where);
}

static void ares_cheats_mark_old(const Game *game, const char *cheats_dir)
{
    CheatFile cand[ARES_CHEAT_FILES];
    int n = ares_cheat_files(game, cand, ARES_CHEAT_FILES);
    for (int i = 0; i < n; ++i)
    {
        char dir[SwanStationPS5_PATH_MAX], file[270], path[SwanStationPS5_PATH_MAX];
        cheat_folder(cheats_dir, cand[i].sys, dir, sizeof(dir));
        snprintf(file, sizeof(file), "%s.cht", cand[i].name);
        path_join(path, sizeof(path), dir, file);
        mark_old(path);
    }
}

static bool is_ps1(const Game *game)
{
    return !game->system[0] || strcmp(game->system, "ps1") == 0;
}
#endif

void cheats_mark_old(const Game *game, const char *cheats_dir)
{
#if defined(ARES_UWP)
    if (!is_ps1(game))
    {
        ares_cheats_mark_old(game, cheats_dir);
        return;
    }
#endif
    char name[256], serial[16], dir[SwanStationPS5_PATH_MAX], path[SwanStationPS5_PATH_MAX];
    if (cheats_best_in_index(game, "cheats-index.txt", name, sizeof(name)))
    {
        path_join(path, sizeof(path), cheats_dir, name);
        mark_old(path);
    }
    const char *disc_serial = game_serial(game, true);
    if (!disc_serial[0] || !disc_format_serial(disc_serial, serial, sizeof(serial)))
        return;
    path_join(dir, sizeof(dir), cheats_dir, "Odin: see the credits file in the package");
    if (chtdb_file_for("chtdb-index.txt", serial, name, sizeof(name)))
    {
        path_join(path, sizeof(path), dir, name);
        mark_old(path);
    }
    path_join(dir, sizeof(dir), cheats_dir, "Odin: see the credits file in the package");
    if (chtdb_file_for("chtdb-patches-index.txt", serial, name, sizeof(name)))
    {
        path_join(path, sizeof(path), dir, name);
        mark_old(path);
    }
}

bool cheats_load(CheatList *list, const Game *game, const char *cheats_dir)
{
    cheats_clear(list);
    memset(list->items, 0, sizeof(list->items));
#if defined(ARES_UWP)
    if (!is_ps1(game))
        return ares_cheats_load(list, game, cheats_dir); /* no DuckStation database, no PS1 serials */
#endif

    char path[SwanStationPS5_PATH_MAX] = "";
    int score = 0;
    /* the disc's serial (ARES_UWP: read by the host from the disc, once per game) */
    const char *disc_serial = game_serial(game, true);
    /* 1. A .cht next to the game always wins. "cheats.cht" (placed by the sync
     *    tool) opens without listing the folder, which a sandboxed SwanStationPS5 can't. */
    bool found = false;
#if defined(ARES_UWP)
    found = cht_beside_game(game, cheats_dir, path, sizeof(path));
#else
    if (game->folder[0])
    {
        path_join(path, sizeof(path), game->folder, "cheats.cht");
        found = path_exists(path);
    }
    if (!found)
        found = game->folder[0] && strcmp(game->folder, cheats_dir) != 0 &&
                find_any_cht(game->folder, path, sizeof(path));
#endif
    /* 2. Otherwise the best match from the cheat library. */
    if (!found)
        found = find_in_dir(cheats_dir, game, path, sizeof(path), &score);
    if (!found)
    {
        char sub[SwanStationPS5_PATH_MAX];
        path_join(sub, sizeof(sub), cheats_dir, "Sony - PS1");
        found = find_in_dir(sub, game, path, sizeof(path), &score);
    }
    /* 3. The best name in the built-in index of libretro-database: the file
     *    if it's here, else downloaded for next time (needs no listing). */
    if (!found)
    {
        char name[256];
        if (cheats_best_in_index(game, "cheats-index.txt", name, sizeof(name)))
        {
            path_join(path, sizeof(path), cheats_dir, name);
            found = file_opens(path);
            if (!found)
            {
                make_dirs(cheats_dir);
                fetch_start(LIBRETRO_BASE, name, path);
            }
            else
                refresh_if_old(LIBRETRO_BASE, name, path);
        }
    }
    bool library = found && parse_cht(list, path);
#if defined(ARES_UWP)
    if (library)
    {
        drop_unsupported(list, 0, list->count, "ps1");
        library = list->count > 0;
    }
#endif
    /* 4. Nothing there: DuckStation's database, by the disc's serial (it has
     *    games libretro-database lacks: Persona, Diablo...) */
    if (!library && disc_serial[0])
    {
        char name[96], serial[16];
        if (disc_format_serial(disc_serial, serial, sizeof(serial)) &&
            chtdb_file_for("chtdb-index.txt", serial, name, sizeof(name)))
        {
            char dir[SwanStationPS5_PATH_MAX];
            path_join(dir, sizeof(dir), cheats_dir, "Odin: see the credits file in the package");
            path_join(path, sizeof(path), dir, name);
            if (file_opens(path))
            {
                library = parse_chtdb(list, path, false);
                refresh_if_old(CHTDB_BASE, name, path);
            }
            else if (!found)
            {
                make_dirs(dir);
                fetch_start(CHTDB_BASE, name, path);
            }
        }
    }
    /* 5. Patches (widescreen, 60 fps, NTSC mode, fixes) from the same database */
    {
        char name[96], serial[16];
        if (disc_serial[0] && disc_format_serial(disc_serial, serial, sizeof(serial)) &&
            chtdb_file_for("chtdb-patches-index.txt", serial, name, sizeof(name)))
        {
            char dir[SwanStationPS5_PATH_MAX], patch_path[SwanStationPS5_PATH_MAX];
            path_join(dir, sizeof(dir), cheats_dir, "Odin: see the credits file in the package");
            path_join(patch_path, sizeof(patch_path), dir, name);
            if (file_opens(patch_path))
            {
                int before = list->count;
                parse_chtdb(list, patch_path, true);
                refresh_if_old(PATCHES_BASE, name, patch_path);
                /* the patches go first in the list */
                Cheat moved[CHEATS_MAX];
                int n = 0;
                for (int i = before; i < list->count; ++i)
                    moved[n++] = list->items[i];
                for (int i = 0; i < before; ++i)
                    moved[n++] = list->items[i];
                memcpy(list->items, moved, sizeof(Cheat) * (size_t)n);
            }
            else
            {
                make_dirs(dir);
                fetch_start(PATCHES_BASE, name, patch_path); /* if no other download is running */
            }
        }
    }
#if defined(ARES_UWP)
    drop_unsupported(list, 0, list->count, "ps1"); /* chtdb's DuckStation-only code types */
    library = library && list->count > 0;
#endif
    if (library)
        str_copy(list->source, sizeof(list->source), path);
    return finish_load(list, game, cheats_dir, list->count, library ? path : "nowhere");
}

bool cheats_add_user(CheatList *list, const Game *game, const char *cheats_dir, const char *desc, const char *code)
{
    if (!game || list->count >= CHEATS_MAX)
        return false;
    char dir[SwanStationPS5_PATH_MAX], path[SwanStationPS5_PATH_MAX], file[96];
    user_file(game, cheats_dir, dir, sizeof(dir), path, sizeof(path), file, sizeof(file));
    make_dirs(dir);
    FILE *f = fopen(path, "a");
    if (!f)
        return false;
    fprintf(f, "%s\t%s\n", desc, code);
    bool ok = fclose(f) == 0;
    Cheat *c = &list->items[list->count++];
    memset(c, 0, sizeof(*c));
    str_copy(c->desc, sizeof(c->desc), desc);
    str_copy(c->code, sizeof(c->code), code);
    c->enabled = true;
    if (!list->state_path[0])
    {
        char enabled_dir[SwanStationPS5_PATH_MAX], name[96];
        path_join(enabled_dir, sizeof(enabled_dir), cheats_dir, "enabled");
        make_dirs(enabled_dir);
        snprintf(name, sizeof(name), "%.80s.txt", game->id);
        path_join(list->state_path, sizeof(list->state_path), enabled_dir, name);
    }
    cheats_save_selection(list);
    return ok;
}

void cheats_apply(const CheatList *list)
{
    host_cheat_reset(); /* to the emulator running the game */
    if (ra_hardcore())
        return; /* hardcore achievements: no codes reach the core */
    unsigned index = 0;
    for (int i = 0; i < list->count; ++i)
        if (list->items[i].enabled)
            host_cheat_set(index++, list->items[i].code);
}
