/*
 * SwanStationPS5 - game library scanning.
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "library.h"

#include "disc.h"
#if defined(SWANUI_XBOX)
#include "swanui_xbox_library.h" /* SWANUI_XBOX: a sheet's parts and unusable sheets are not discs of their own */
#endif
#if defined(ARES_UWP)
#include "swanui_systems.h" /* ARES_UWP: the system of each file, and the stable id derived from system and title */
#if defined(ODIN_UWP)
#include "swanui_odin_core.h" /* ODIN_UWP: the status of each set, from the core (odin_check_set) */
#endif
#elif defined(TSUGARU_UWP)
#include "swanui_towns_media.h" /* TSUGARU_UWP: CD or floppy, and the id derived from the title */
#endif

#include <ctype.h>
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <unistd.h>

#define MAX_DIR_FILES 256 /* TSUGARU_UWP: Towns CDs may have 60+ track files in one folder */
#define MAX_ROOT_ENTRIES 4096 /* entries read from the games folder (static: 1 MB of names) */

/* Loadable images, best first: a playlist beats a single disc. */
static int image_rank(const char *name)
{
    const char *ext = path_ext(name);
#if defined(ARES_UWP)
    /* ARES_UWP: whatever a system of compat/swanui_systems.c takes. A playlist first; then the disc images (.chd, a
     * .cue sheet); cartridges, tapes and .zip archives 2. Which system a file is comes in finish_game. */
    if (str_icmp(ext, "m3u") == 0)
        return 6;
    if (str_icmp(ext, "chd") == 0)
        return 4;
    if (str_icmp(ext, "cue") == 0)
        return 3;
    return swanui_system_known_extension(name) ? 2 : 0;
#elif defined(TSUGARU_UWP)
    /* TSUGARU_UWP: FM Towns media. CD-ROM images: .cue/.mds sheets, .iso/.mdf/.img, and the .bin of a sheet; floppy
     * images: .d77 .d88 and raw .img/.bin. No .chd/.pbp/.ccd/.ecm: Tsugaru cannot read them. */
    if (str_icmp(ext, "m3u") == 0)
        return 6;
    if (str_icmp(ext, "cue") == 0 || str_icmp(ext, "mds") == 0)
        return 3;
    if (str_icmp(ext, "iso") == 0 || str_icmp(ext, "img") == 0 || str_icmp(ext, "mdf") == 0 ||
        str_icmp(ext, "d77") == 0 || str_icmp(ext, "d88") == 0)
        return 2;
    if (str_icmp(ext, "bin") == 0)
        return 1;
    return 0;
#endif
    if (str_icmp(ext, "m3u") == 0)
        return 6;
    if (str_icmp(ext, "pbp") == 0)
        return 5;
    if (str_icmp(ext, "chd") == 0)
        return 4;
    if (str_icmp(ext, "cue") == 0 || str_icmp(ext, "ccd") == 0 || str_icmp(ext, "mds") == 0)
        return 3;
    if (str_icmp(ext, "iso") == 0 || str_icmp(ext, "img") == 0 || str_icmp(ext, "mdf") == 0 ||
        str_icmp(ext, "ecm") == 0)
        return 2;
    if (str_icmp(ext, "bin") == 0)
        return 1; /* only used when no sheet describes it */
    return 0;
}

static bool contains_icase(const char *haystack, const char *needle)
{
    size_t n = strlen(needle);
    for (; *haystack; ++haystack)
    {
        size_t i = 0;
        while (i < n && haystack[i] &&
               tolower((unsigned char)haystack[i]) == tolower((unsigned char)needle[i]))
            ++i;
        if (i == n)
            return true;
    }
    return false;
}

static void extract_serial(const char *name, char *serial, size_t size)
{
    static const char *prefixes[] = {"SLUS", "SCUS", "SLES", "SCES", "SLPS", "SCPS",
                                     "SLPM", "SIPS", "SLKA", "PAPX", "SCED"};
    serial[0] = '\0';
#if defined(TSUGARU_UWP) || defined(ARES_UWP)
    (void)name; /* TSUGARU_UWP: no PS1 serials in FM Towns names: finish_game derives one from the title (ARES_UWP: from
                 * the system and the title) */
    (void)size;
    return;
#endif
    for (size_t p = 0; p < sizeof(prefixes) / sizeof(prefixes[0]); ++p)
    {
        const char *hit = strstr(name, prefixes[p]);
        if (!hit)
            continue;
        size_t n = 4;
        while (n < 12 && (isdigit((unsigned char)hit[n]) || hit[n] == '-' || hit[n] == '_' ||
                          hit[n] == '.'))
            ++n;
        if (n > 6)
        {
            str_copy(serial, size < n + 1 ? size : n + 1, hit);
            return;
        }
    }
}

/* "Resident Evil 2 [U] [SLUS-00421]" -> "Resident Evil 2"
 * "Chrono Cross (USA) (Disc 1)"      -> "Chrono Cross (USA)" */
static void clean_title(const char *raw, char *out, size_t size)
{
    char buf[256];
    str_copy(buf, sizeof(buf), raw);
    char *dot = strrchr(buf, '.');
    if (dot && image_rank(buf) > 0)
        *dot = '\0';

    char result[256];
    size_t w = 0;
    for (const char *p = buf; *p && w + 1 < sizeof(result);)
    {
        if (*p == '[')
        {
            const char *end = strchr(p, ']');
            if (end)
            {
                p = end + 1;
                continue;
            }
        }
        if (*p == '(' && (strncmp(p, "(Disc", 5) == 0 || strncmp(p, "(disc", 5) == 0 ||
                          strncmp(p, "(CD", 3) == 0 || strncmp(p, "(Odin)", 7) == 0))
        {
            const char *end = strchr(p, ')');
            if (end)
            {
                p = end + 1;
                continue;
            }
        }
        result[w++] = (*p == '_') ? ' ' : *p;
        ++p;
    }
    result[w] = '\0';

    /* collapse whitespace */
    char *src = result, *dst = result;
    bool space = true;
    for (; *src; ++src)
    {
        if (isspace((unsigned char)*src))
        {
            if (!space)
                *dst++ = ' ';
            space = true;
        }
        else
        {
            *dst++ = *src;
            space = false;
        }
    }
    while (dst > result && dst[-1] == ' ')
        --dst;
    *dst = '\0';
    str_copy(out, size, result[0] ? result : raw);
}

/* The serial in the name of the first disc a playlist lists ("... [SLES-01734] [CD1].chd"): the discs of a
 * playlist often can't be read here (.chd), yet carry their serial in the file name. */
static void serial_from_playlist(const char *path, char *serial, size_t size)
{
    serial[0] = '\0';
    if (str_icmp(path_ext(path), "m3u") != 0)
        return;
    FILE *f = fopen(path, "r");
    if (!f)
        return;
    char line[300];
    while (fgets(line, sizeof(line), f))
    {
        line[strcspn(line, "\r\n")] = '\0';
        if (!line[0] || line[0] == '#')
            continue;
        extract_serial(line, serial, size);
        break;
    }
    fclose(f);
}

bool swanstation_retro_ds(const char *path, char *out, size_t size);

typedef struct
{
    char path[SwanStationPS5_PATH_MAX];
    long long size;
    long long mtime;
    char serial[16];
} SerialEntry;

static SerialEntry *serial_cache;
static int serial_cache_count, serial_cache_cap;
static char serial_cache_path[SwanStationPS5_PATH_MAX];
static bool serial_cache_dirty;

static void serial_cache_add(const SerialEntry *e)
{
    if (serial_cache_count == serial_cache_cap)
    {
        int cap = serial_cache_cap ? serial_cache_cap * 2 : 128;
        SerialEntry *grown = realloc(serial_cache, (size_t)cap * sizeof(SerialEntry));
        if (!grown)
            return;
        serial_cache = grown;
        serial_cache_cap = cap;
    }
    serial_cache[serial_cache_count++] = *e;
}

void library_serial_cache_load(const char *file)
{
    free(serial_cache);
    serial_cache = NULL;
    serial_cache_count = serial_cache_cap = 0;
    serial_cache_dirty = false;
    str_copy(serial_cache_path, sizeof(serial_cache_path), file);
    FILE *f = fopen(file, "r");
    if (!f)
        return;
    char line[SwanStationPS5_PATH_MAX + 80];
    while (fgets(line, sizeof(line), f))
    {
        line[strcspn(line, "\r\n")] = '\0';
        char *size = strchr(line, '\t');
        if (!size)
            continue;
        *size++ = '\0';
        char *mtime = strchr(size, '\t');
        if (!mtime)
            continue;
        *mtime++ = '\0';
        char *serial = strchr(mtime, '\t');
        if (!serial)
            continue;
        *serial++ = '\0';
        SerialEntry e;
        str_copy(e.path, sizeof(e.path), line);
        e.size = atoll(size);
        e.mtime = atoll(mtime);
        str_copy(e.serial, sizeof(e.serial), serial);
        serial_cache_add(&e);
    }
    fclose(f);
}

static void serial_cache_save(void)
{
    if (!serial_cache_dirty || !serial_cache_path[0])
        return;
    FILE *f = fopen(serial_cache_path, "w");
    if (!f)
        return;
    for (int i = 0; i < serial_cache_count; ++i)
        fprintf(f, "%s\t%lld\t%lld\t%s\n", serial_cache[i].path, serial_cache[i].size, serial_cache[i].mtime,
                serial_cache[i].serial);
    fclose(f);
    serial_cache_dirty = false;
}

bool swanstation_retro_ds(const char *path, char *out, size_t size);

/* The core opens the image, which is slow for a .chd: what it answers is remembered
 * by path, size and modification time ("-" when the disc has no serial). */
static bool core_serial(const char *path, char *serial, size_t size)
{
    struct stat st;
    if (stat(path, &st) != 0)
        return false;
    for (int i = 0; i < serial_cache_count; ++i)
    {
        SerialEntry *e = &serial_cache[i];
        if (e->size != (long long)st.st_size || e->mtime != (long long)st.st_mtime || strcmp(e->path, path) != 0)
            continue;
        if (e->serial[0] == '-')
            return false;
        str_copy(serial, size, e->serial);
        return true;
    }
    char code[32];
#if defined(TSUGARU_UWP) || defined(ARES_UWP)
    bool ok = false; /* TSUGARU_UWP: no PS1 core to ask (swanstation_retro_ds is not provided) */
    (void)code;
#else
    bool ok = swanstation_retro_ds(path, code, sizeof(code)) && disc_format_serial(code, serial, size);
#endif
    SerialEntry e;
    str_copy(e.path, sizeof(e.path), path);
    e.size = st.st_size;
    e.mtime = st.st_mtime;
    str_copy(e.serial, sizeof(e.serial), ok ? serial : "-");
    for (int i = 0; i < serial_cache_count; ++i)
        if (strcmp(serial_cache[i].path, path) == 0)
        {
            serial_cache[i] = e;
            serial_cache_dirty = true;
            return ok;
        }
    serial_cache_add(&e);
    serial_cache_dirty = true;
    return ok;
}

#if defined(ARES_UWP)
/* ARES_UWP: the stable id of a game, "SFC-1A2B3C4D": the system id in capitals (cut to 6 letters, MYV for MyVision)
 * and an FNV-1a hash of the system id and the title's letters and digits, case folded (renaming punctuation or the
 * file's extension keeps it). It keys the saves, states, settings and covers; serial holds the same text. */
static void ares_set_id(Game *g)
{
    uint32_t h = 2166136261u;
    for (const unsigned char *p = (const unsigned char *)g->system; *p; ++p)
    {
        h ^= (uint32_t)tolower(*p);
        h *= 16777619u;
    }
    h ^= (uint32_t)'/';
    h *= 16777619u;
    for (const unsigned char *p = (const unsigned char *)g->title; *p; ++p)
        if (isalnum(*p) || *p >= 0x80)
        {
            h ^= (uint32_t)tolower(*p);
            h *= 16777619u;
        }
    char tag[8];
    size_t n = 0;
    if (strlen(g->system) > 6)
        str_copy(tag, sizeof(tag), "MYV");
    else
    {
        for (; g->system[n] && n < 6; ++n)
            tag[n] = (char)toupper((unsigned char)g->system[n]);
        tag[n] = '\0';
    }
    snprintf(g->serial, sizeof(g->serial), "%s-%08X", tag, (unsigned)h);
    str_copy(g->id, sizeof(g->id), g->serial);
}
#endif

/* Serial from the disc itself (exact), else serial.txt written by the sync
 * tool (needed for .chd), else whatever the file name carried. Then the id.
 * false: the game is not to be listed (ARES_UWP: no system takes the file); the caller drops it. */
static bool finish_game(Game *g)
{
#if defined(ARES_UWP)
    /* ARES_UWP: the system (extension, folder name, a small probe: compat/swanui_systems.c), then the id from it */
    const char *system = swanui_system_of_path(g->path);
    if (!system)
    {
        SwanStationPS5_log("library: no system takes %s, not listed", g->path);
        return false;
    }
    str_copy(g->system, sizeof(g->system), system);
    ares_set_id(g);
    return true;
#elif defined(TSUGARU_UWP)
    /* TSUGARU_UWP: the FM Towns has no serials: a stable id from the title ("FMT-1A2B3C4D") keys the saves, states and
     * covers, and the media (CD or floppy) tells the host which drive the image goes in. */
    towns_serial_from_title(g->title, g->serial, sizeof(g->serial));
    str_copy(g->id, sizeof(g->id), g->serial);
    g->media = towns_media_of_path(g->path);
    return true;
#endif
    char serial[16];
    char txt[SwanStationPS5_PATH_MAX];
    if (disc_read_serial(g->path, serial, sizeof(serial)) || core_serial(g->path, serial, sizeof(serial)))
        str_copy(g->serial, sizeof(g->serial), serial);
    else
    {
        path_join(txt, sizeof(txt), g->folder, "serial.txt");
        FILE *f = fopen(txt, "r");
        if (f)
        {
            char line[64] = "";
            if (fgets(line, sizeof(line), f) && disc_format_serial(line, serial, sizeof(serial)))
                str_copy(g->serial, sizeof(g->serial), serial);
            fclose(f);
        }
    }

    if (!g->serial[0])
    {
        serial_from_playlist(g->path, serial, sizeof(serial));
        if (serial[0])
            str_copy(g->serial, sizeof(g->serial), serial);
    }
    if (g->serial[0])
    {
        str_copy(g->id, sizeof(g->id), g->serial);
        return true;
    }
    SwanStationPS5_log("library: no serial found in %s (put it in the file or folder name, or a serial.txt beside it)",
              g->path);
    size_t w = 0;
    for (const char *p = g->title; *p && w + 1 < sizeof(g->id); ++p)
        g->id[w++] = isalnum((unsigned char)*p) ? *p : '_';
    g->id[w] = '\0';
    return true;
}

static Game *add_game(Library *lib)
{
    if (lib->count == lib->capacity)
    {
        int cap = lib->capacity ? lib->capacity * 2 : 64;
        Game *grown = realloc(lib->games, (size_t)cap * sizeof(Game));
        if (!grown)
            return NULL;
        lib->games = grown;
        lib->capacity = cap;
    }
    Game *g = &lib->games[lib->count++];
    memset(g, 0, sizeof(*g));
    return g;
}

bool library_add_game(Library *lib, const Game *g)
{
    Game *n = add_game(lib);
    if (!n)
        return false;
    *n = *g;
    return true;
}

bool library_remove_path(Library *lib, const char *path)
{
    for (int i = 0; i < lib->count; ++i)
        if (strcmp(lib->games[i].path, path) == 0)
        {
            memmove(&lib->games[i], &lib->games[i + 1], (size_t)(lib->count - i - 1) * sizeof(Game));
            --lib->count;
            return true;
        }
    return false;
}

static void set_disc_name(Game *g, const char *file)
{
    str_copy(g->disc_name, sizeof(g->disc_name), file);
    char *dot = strrchr(g->disc_name, '.');
    if (dot)
        *dot = '\0';
}

static int cmp_names(const void *a, const void *b)
{
    return str_icmp((const char *)a, (const char *)b);
}

typedef char NameBuf[256];

/* Reads up to MAX_DIR_FILES loadable entries of `dir`, sorted by name. */
static int list_images(const char *dir, NameBuf *names)
{
    DIR *d = opendir(dir);
    if (!d)
        return 0;
    int n = 0;
    struct dirent *e;
    while ((e = readdir(d)) && n < MAX_DIR_FILES)
    {
        if (e->d_name[0] == '.' || image_rank(e->d_name) == 0)
            continue;
        str_copy(names[n++], sizeof(NameBuf), e->d_name);
    }
    closedir(d);
#if defined(SWANUI_XBOX)
    /* SWANUI_XBOX: the .bin a .cue describes is not a disc of its own, and a .cue whose files are missing is not a
     * disc at all (a .bin.ecm beside it is) */
    {
        static unsigned char hidden[MAX_DIR_FILES];
        memset(hidden, 0, sizeof(hidden));
        swanui_xbox_hide_sheet_parts(dir, names[0], sizeof(NameBuf), n, hidden);
        int kept = 0;
        for (int i = 0; i < n; ++i)
            if (!hidden[i])
            {
                if (kept != i)
                    memcpy(names[kept], names[i], sizeof(NameBuf));
                ++kept;
            }
        n = kept;
    }
#endif
    qsort(names, (size_t)n, sizeof(NameBuf), cmp_names);
    return n;
}

static bool write_m3u(const char *path, NameBuf *discs, int count)
{
    FILE *f = fopen(path, "w");
    if (!f)
        return false;
    for (int i = 0; i < count; ++i)
        fprintf(f, "%s\n", discs[i]);
    return fclose(f) == 0;
}

/* One folder = one game. */
static void scan_game_folder(Library *lib, const char *dir, const char *folder_name)
{
    static NameBuf names[MAX_DIR_FILES];
    static NameBuf discs[MAX_DIR_FILES];
    int n = list_images(dir, names);
    if (n == 0)
        return;

    int best = -1, best_rank = 0;
    bool has_sheet = false;
    int disc_count = 0;
    for (int i = 0; i < n; ++i)
    {
        int rank = image_rank(names[i]);
        if (rank == 3)
            has_sheet = true;
        if (rank > best_rank)
        {
            best_rank = rank;
            best = i;
        }
    }
    /* Collect disc images of the winning kind (cue/chd/pbp...). */
    for (int i = 0; i < n; ++i)
        if (image_rank(names[i]) == best_rank && best_rank != 6)
            str_copy(discs[disc_count++], sizeof(NameBuf), names[i]);
    if (best_rank == 1 && has_sheet)
        return;

    Game *g = add_game(lib);
    if (!g)
        return;
    clean_title(folder_name, g->title, sizeof(g->title));
    extract_serial(folder_name, g->serial, sizeof(g->serial));
    if (!g->serial[0])
        extract_serial(names[best], g->serial, sizeof(g->serial));
    g->discs = 1;

    if (best_rank == 6)
    {
        path_join(g->path, sizeof(g->path), dir, names[best]);
        /* count lines for the disc indicator */
        FILE *f = fopen(g->path, "r");
        if (f)
        {
            char line[512];
            int lines = 0;
            while (fgets(line, sizeof(line), f))
                if (line[0] && line[0] != '#' && line[0] != '\n' && line[0] != '\r')
                    ++lines;
            fclose(f);
            g->discs = lines > 0 ? lines : 1;
        }
    }
    else if (disc_count > 1 && (contains_icase(discs[0], "disc") || contains_icase(discs[0], "cd")))
    {
        char m3u[SwanStationPS5_PATH_MAX];
        char file[160];
        snprintf(file, sizeof(file), "%.150s.m3u", g->title);
        path_join(m3u, sizeof(m3u), dir, file);
        if (write_m3u(m3u, discs, disc_count))
        {
            str_copy(g->path, sizeof(g->path), m3u);
            g->discs = disc_count;
            SwanStationPS5_log("library: wrote %s (%d discs)", m3u, disc_count);
        }
        else
            path_join(g->path, sizeof(g->path), dir, discs[0]);
    }
    else
        path_join(g->path, sizeof(g->path), dir, names[best]);

    str_copy(g->folder, sizeof(g->folder), dir);
    set_disc_name(g, disc_count > 0 ? discs[0] : names[best]);
    if (!finish_game(g))
        --lib->count; /* ARES_UWP: no system takes it */
}

static void scan_root(Library *lib, const char *root)
{
    DIR *d = opendir(root);
    if (!d)
        return;
    static NameBuf entries[MAX_ROOT_ENTRIES];
    int n = 0;
    struct dirent *e;
    while ((e = readdir(d)) && n < MAX_ROOT_ENTRIES)
    {
        if (e->d_name[0] == '.')
            continue;
        str_copy(entries[n++], sizeof(NameBuf), e->d_name);
    }
    closedir(d);
    qsort(entries, (size_t)n, sizeof(NameBuf), cmp_names);

    bool root_has_sheet = false;
    for (int i = 0; i < n; ++i)
        if (image_rank(entries[i]) == 3)
            root_has_sheet = true;

    /* Multi-disc games lying loose here ("FF VII (Disc 1).cue", "(Disc 2).cue"...)
     * are one game, as in their own folder: the discs a playlist (.m3u) here
     * already lists are skipped, the others get one written, and the shelf
     * shows the game once, from disc 1. */
    static bool skip[MAX_ROOT_ENTRIES];
    memset(skip, 0, sizeof(skip));
    for (int i = 0; i < n; ++i)
        if (image_rank(entries[i]) == 6)
        {
            /* a playlist made elsewhere (VLC...) may hold a PC's paths: it counts
             * only when every disc it lists is here */
            char m3u[SwanStationPS5_PATH_MAX], line[300];
            path_join(m3u, sizeof(m3u), root, entries[i]);
            FILE *f = fopen(m3u, "r");
            int listed[MAX_DIR_FILES], count = 0;
            bool valid = f != NULL;
            while (f && fgets(line, sizeof(line), f))
            {
                line[strcspn(line, "\r\n")] = '\0';
                if (!line[0] || line[0] == '#')
                    continue;
                int found = -1;
                for (int k = 0; k < n && found < 0; ++k)
                    if (!str_icmp(entries[k], line))
                        found = k;
                if (found < 0)
                    valid = false;
                else if (count < MAX_DIR_FILES)
                    listed[count++] = found;
            }
            if (f)
                fclose(f);
            if (valid && count > 0)
                for (int k = 0; k < count; ++k)
                    skip[listed[k]] = true;
            else
            {
                skip[i] = true; /* not usable here: SwanStationPS5 groups the discs itself */
                SwanStationPS5_log("library: %s lists discs that aren't beside it, ignored", m3u);
            }
        }
#if defined(SWANUI_XBOX)
    /* SWANUI_XBOX: games lying loose here: the .bin/.img a sheet describes is not a disc of its own, and a sheet whose
     * files are missing is not a disc at all ("Rockman X4 (J).cue" beside only a .bin.ecm: the .ecm is the disc) */
    {
        static unsigned char hidden[MAX_ROOT_ENTRIES];
        memset(hidden, 0, sizeof(hidden));
        swanui_xbox_hide_sheet_parts(root, entries[0], sizeof(NameBuf), n, hidden);
        for (int i = 0; i < n; ++i)
            if (hidden[i])
                skip[i] = true;
    }
#endif
    for (int i = 0; i < n; ++i)
    {
        int rank = image_rank(entries[i]);
        if (skip[i] || rank < 2 || rank == 6 ||
            !(contains_icase(entries[i], "(disc") || contains_icase(entries[i], "(cd")))
            continue;
        char title[160], other[160];
        clean_title(entries[i], title, sizeof(title));
        static NameBuf discs[MAX_DIR_FILES];
        int count = 0, members[MAX_DIR_FILES];
        for (int k = i; k < n && count < MAX_DIR_FILES; ++k)
        {
            if (skip[k] || image_rank(entries[k]) != rank ||
                !(contains_icase(entries[k], "(disc") || contains_icase(entries[k], "(cd")))
                continue;
            clean_title(entries[k], other, sizeof(other));
            if (str_icmp(title, other) != 0)
                continue;
            members[count] = k;
            str_copy(discs[count++], sizeof(NameBuf), entries[k]);
        }
        if (count < 2)
            continue;
        for (int k = 0; k < count; ++k)
            skip[members[k]] = true;
        Game *g = add_game(lib);
        if (!g)
            return;
        str_copy(g->title, sizeof(g->title), title);
        extract_serial(discs[0], g->serial, sizeof(g->serial));
        str_copy(g->folder, sizeof(g->folder), root);
        char m3u_name[200], m3u[SwanStationPS5_PATH_MAX];
        snprintf(m3u_name, sizeof(m3u_name), "%.190s.m3u", title);
        path_join(m3u, sizeof(m3u), root, m3u_name);
        if (path_exists(m3u))
        {
            /* that name is taken by a playlist that didn't work here: keep it */
            snprintf(m3u_name, sizeof(m3u_name), "%.180s (Odin).m3u", title);
            path_join(m3u, sizeof(m3u), root, m3u_name);
        }
        char first[SwanStationPS5_PATH_MAX];
        path_join(first, sizeof(first), root, discs[0]);
        if (write_m3u(m3u, discs, count))
        {
            str_copy(g->path, sizeof(g->path), m3u);
            SwanStationPS5_log("library: wrote %s (%d discs)", m3u, count);
        }
        else
            str_copy(g->path, sizeof(g->path), first);
        g->discs = count;
        set_disc_name(g, discs[0]);
        if (!finish_game(g))
            --lib->count; /* ARES_UWP: no system takes it */
    }

    for (int i = 0; i < n; ++i)
    {
        if (skip[i])
            continue;
        char full[SwanStationPS5_PATH_MAX];
        path_join(full, sizeof(full), root, entries[i]);
        if (path_is_dir(full))
        {
            scan_game_folder(lib, full, entries[i]);
            continue;
        }
        int rank = image_rank(entries[i]);
        if (rank == 0 || (rank == 1 && root_has_sheet))
            continue;
        Game *g = add_game(lib);
        if (!g)
            return;
        clean_title(entries[i], g->title, sizeof(g->title));
        extract_serial(entries[i], g->serial, sizeof(g->serial));
        str_copy(g->path, sizeof(g->path), full);
        str_copy(g->folder, sizeof(g->folder), root);
        set_disc_name(g, entries[i]);
        g->discs = 1;
        if (rank == 6)
        {
            /* a playlist: one disc per line */
            FILE *f = fopen(full, "r");
            char line[300];
            int lines = 0;
            while (f && fgets(line, sizeof(line), f))
                lines += line[0] && line[0] != '\n' && line[0] != '\r' && line[0] != '#';
            if (f)
                fclose(f);
            g->discs = lines > 0 ? lines : 1;
        }
        if (!finish_game(g))
            --lib->count; /* ARES_UWP: no system takes it */
    }
}

#if defined(ARES_UWP)
/* ---------------------------------------------------------------- ARES_UWP: folders of any depth
 *
 * A games folder for many systems is usually sorted by system ("SNES\*.sfc", "PS1\Game\Game.cue"), so a folder
 * is not one game: every folder is read like a root (loose images, playlists, "(Disc n)" sets, sheets that hide their
 * .bin), down to ARES_MAX_DEPTH folders below the root. A folder that turns out to hold a single game (the old "one game
 * per folder" layout) names it, unless the folder is named after a system. */

#define ARES_MAX_DEPTH 3

typedef struct
{
    char name[256]; /* first: swanui_xbox_hide_sheet_parts reads the names with this struct's size as the stride */
    unsigned char kind; /* 0 file, 1 folder, 2 unknown (stat decides) */
} AresEntry;

static int cmp_ares_entries(const void *a, const void *b)
{
    return str_icmp(((const AresEntry *)a)->name, ((const AresEntry *)b)->name);
}

/* Folders a games folder may hold that are not games (ares's firmware goes in <data>\bios, but people put it beside
 * the games too), nor art */
static bool ares_skip_folder(const char *name)
{
    static const char *const SKIP[] = {"bios",   "firmware", "covers", "saves", "states",     "cheats",
                                       "logs",   "cache",    "import", "media", "screenshots", "manuals",
                                       "boxart", "snaps",    "titles", "bezels", "game-settings"};
    for (size_t i = 0; i < sizeof(SKIP) / sizeof(SKIP[0]); ++i)
        if (str_icmp(name, SKIP[i]) == 0)
            return true;
    return false;
}

static bool is_disc_set_member(const char *name)
{
    const int rank = image_rank(name);
    return (rank == 3 || rank == 4) && (contains_icase(name, "(disc") || contains_icase(name, "(cd"));
}

/* A folder to walk: one the listing says is a folder, or an entry of unknown kind without a game's extension (opendir
 * fails at once on a file) */
static bool ares_is_subfolder(const AresEntry *e, unsigned char skipped)
{
    if (skipped || ares_skip_folder(e->name))
        return false;
    return e->kind == 1 || (e->kind == 2 && image_rank(e->name) == 0);
}

static void scan_dir_ares(Library *lib, const char *dir, const char *dir_name, int depth)
{
    DIR *d = opendir(dir);
    if (!d)
        return;
    AresEntry *entries = malloc(sizeof(AresEntry) * MAX_ROOT_ENTRIES);
    unsigned char *skip = calloc(MAX_ROOT_ENTRIES, 1);
    unsigned char *hidden = calloc(MAX_ROOT_ENTRIES, 1);
    if (!entries || !skip || !hidden)
    {
        closedir(d);
        free(entries);
        free(skip);
        free(hidden);
        return;
    }
    int n = 0;
    struct dirent *e;
    while ((e = readdir(d)) && n < MAX_ROOT_ENTRIES)
    {
        if (e->d_name[0] == '.')
            continue;
        str_copy(entries[n].name, sizeof(entries[n].name), e->d_name);
        entries[n].kind = e->d_type == DT_DIR ? 1 : e->d_type == DT_REG ? 0 : 2;
        ++n;
    }
    closedir(d);
    qsort(entries, (size_t)n, sizeof(AresEntry), cmp_ares_entries);
    const int first_game = lib->count;

    /* playlists: the discs a playlist here lists are not games of their own; one that lists discs that aren't here
     * (made on a PC...) is not used */
    for (int i = 0; i < n; ++i)
    {
        if (entries[i].kind == 1 || image_rank(entries[i].name) != 6)
            continue;
        char m3u[SwanStationPS5_PATH_MAX], line[300];
        path_join(m3u, sizeof(m3u), dir, entries[i].name);
        FILE *f = fopen(m3u, "r");
        int listed[MAX_DIR_FILES], count = 0;
        bool valid = f != NULL;
        while (f && fgets(line, sizeof(line), f))
        {
            line[strcspn(line, "\r\n")] = '\0';
            if (!line[0] || line[0] == '#')
                continue;
            int found = -1;
            for (int k = 0; k < n && found < 0; ++k)
                if (!str_icmp(entries[k].name, line))
                    found = k;
            if (found < 0)
                valid = false;
            else if (count < MAX_DIR_FILES)
                listed[count++] = found;
        }
        if (f)
            fclose(f);
        if (valid && count > 0)
            for (int k = 0; k < count; ++k)
                skip[listed[k]] = true;
        else
        {
            skip[i] = true;
            SwanStationPS5_log("library: %s lists discs that aren't beside it, ignored", m3u);
        }
    }
    /* a sheet's .bin/.img is not a game of its own, and a sheet whose files are missing is not one at all */
    swanui_xbox_hide_sheet_parts(dir, entries[0].name, sizeof(AresEntry), n, hidden);
    for (int i = 0; i < n; ++i)
        if (hidden[i])
            skip[i] = true;

    /* "Title (Disc 1).cue", "Title (Disc 2).cue"...: one game, through a playlist written beside them */
    static NameBuf discs[MAX_DIR_FILES];
    for (int i = 0; i < n; ++i)
    {
        if (skip[i] || entries[i].kind == 1 || !is_disc_set_member(entries[i].name))
            continue;
        const int rank = image_rank(entries[i].name);
        char title[160], other[160];
        clean_title(entries[i].name, title, sizeof(title));
        int count = 0, members[MAX_DIR_FILES];
        for (int k = i; k < n && count < MAX_DIR_FILES; ++k)
        {
            if (skip[k] || entries[k].kind == 1 || image_rank(entries[k].name) != rank ||
                !is_disc_set_member(entries[k].name))
                continue;
            clean_title(entries[k].name, other, sizeof(other));
            if (str_icmp(title, other) != 0)
                continue;
            members[count] = k;
            str_copy(discs[count++], sizeof(NameBuf), entries[k].name);
        }
        if (count < 2)
            continue;
        for (int k = 0; k < count; ++k)
            skip[members[k]] = true;
        Game *g = add_game(lib);
        if (!g)
            break;
        str_copy(g->title, sizeof(g->title), title);
        str_copy(g->folder, sizeof(g->folder), dir);
        char m3u_name[200], m3u[SwanStationPS5_PATH_MAX], first[SwanStationPS5_PATH_MAX];
        snprintf(m3u_name, sizeof(m3u_name), "%.190s.m3u", title);
        path_join(m3u, sizeof(m3u), dir, m3u_name);
        if (path_exists(m3u))
        {
            /* that name is taken by a playlist that didn't work here: keep it */
            snprintf(m3u_name, sizeof(m3u_name), "%.180s (Odin).m3u", title);
            path_join(m3u, sizeof(m3u), dir, m3u_name);
        }
        path_join(first, sizeof(first), dir, discs[0]);
        if (write_m3u(m3u, discs, count))
        {
            str_copy(g->path, sizeof(g->path), m3u);
            SwanStationPS5_log("library: wrote %s (%d discs)", m3u, count);
        }
        else
            str_copy(g->path, sizeof(g->path), first); /* a read-only folder: the first disc, the others by hand */
        g->discs = count;
        set_disc_name(g, discs[0]);
        if (!finish_game(g))
            --lib->count;
    }

    /* the other files, each a game when a system takes it */
    for (int i = 0; i < n; ++i)
    {
        if (skip[i] || entries[i].kind == 1)
            continue;
        const int rank = image_rank(entries[i].name);
        if (rank == 0)
            continue;
        char full[SwanStationPS5_PATH_MAX];
        path_join(full, sizeof(full), dir, entries[i].name);
        if (entries[i].kind == 2 && path_is_dir(full))
        {
            entries[i].kind = 1;
            continue;
        }
        Game *g = add_game(lib);
        if (!g)
            break;
        clean_title(entries[i].name, g->title, sizeof(g->title));
        str_copy(g->path, sizeof(g->path), full);
        str_copy(g->folder, sizeof(g->folder), dir);
        set_disc_name(g, entries[i].name);
        g->discs = 1;
        if (rank == 6)
        {
            FILE *f = fopen(full, "r"); /* a playlist: one disc per line */
            char line[300];
            int lines = 0;
            while (f && fgets(line, sizeof(line), f))
                lines += line[0] && line[0] != '\n' && line[0] != '\r' && line[0] != '#';
            if (f)
                fclose(f);
            g->discs = lines > 0 ? lines : 1;
        }
        if (!finish_game(g))
            --lib->count;
    }

    /* a folder that holds one game, and is not named after a system: the game takes the folder's name, as before */
    if (depth > 0 && lib->count == first_game + 1 && dir_name && !swanui_system_of_folder_name(dir_name))
    {
        Game *g = &lib->games[first_game];
        char title[160];
        clean_title(dir_name, title, sizeof(title));
        if (title[0])
        {
            str_copy(g->title, sizeof(g->title), title);
            ares_set_id(g);
        }
    }

    /* then the folders below (the big arrays go first: a level holds only the names it walks) */
    int folders = 0;
    for (int i = 0; i < n; ++i)
        if (ares_is_subfolder(&entries[i], skip[i]))
            ++folders;
    NameBuf *sub = depth < ARES_MAX_DEPTH && folders ? malloc(sizeof(NameBuf) * (size_t)folders) : NULL;
    int subs = 0;
    for (int i = 0; sub && i < n; ++i)
        if (ares_is_subfolder(&entries[i], skip[i]))
            str_copy(sub[subs++], sizeof(NameBuf), entries[i].name);
    free(entries);
    free(skip);
    free(hidden);
    for (int i = 0; i < subs; ++i)
    {
        char full[SwanStationPS5_PATH_MAX];
        path_join(full, sizeof(full), dir, sub[i]);
        scan_dir_ares(lib, full, sub[i], depth + 1); /* an entry of unknown kind that is a file opens as no folder */
    }
    free(sub);
}
#endif

#if defined(ODIN_UWP)
/* ---------------------------------------------------------------- ODIN_UWP: the Odin sets, by name
 *
 * A game is a folder, a zip or a 7z whose name (without .zip / .7z) is one of the 14 Odin game sets (odin-games.json,
 * compat/swanui_odin_sets.inc). The shared sets (hikaru, mie, bootrom...) are never games: they only decide whether a
 * game is complete. Completeness is by NAME in this build: a game is complete when every shared set it requires is
 * present beside it or in the scanned root. No file is opened and no contents are checked. Folders are walked
 * ODIN_MAX_DEPTH levels below each root, except the games themselves, the support sets and the skip list. */

#define ODIN_MAX_DEPTH 2

typedef struct
{
    char name[256];
    unsigned char folder;
} OdinEntry;

typedef struct
{
    OdinEntry *items;
    int count, cap;
} OdinListing;

static void odin_listing_free(OdinListing *l)
{
    free(l->items);
    l->items = NULL;
    l->count = 0;
    l->cap = 0;
}

/* The names of one folder (no dot entries), every entry kept: no cap, one readdir pass */
static bool odin_listing_read(OdinListing *l, const char *dir)
{
    l->items = NULL;
    l->count = 0;
    l->cap = 0;
    DIR *d = opendir(dir);
    if (!d)
        return false;
    struct dirent *e;
    while ((e = readdir(d)) != NULL)
    {
        if (e->d_name[0] == '.')
            continue;
        if (l->count == l->cap)
        {
            const int cap = l->cap ? l->cap * 2 : 256;
            OdinEntry *grown = realloc(l->items, sizeof(OdinEntry) * (size_t)cap);
            if (!grown)
                break;
            l->items = grown;
            l->cap = cap;
        }
        OdinEntry *item = &l->items[l->count++];
        str_copy(item->name, sizeof(item->name), e->d_name);
        item->folder = e->d_type == DT_DIR ? 1 : 0;
    }
    closedir(d);
    return true;
}

/* The set name an entry stands for (a folder: its name; a zip or 7z: its name without the extension) and its rank in
 * Odin's order: 0 folder, 1 zip, 2 7z. -1 for any other file. */
static int odin_entry_stem(const OdinEntry *e, char *stem, size_t size)
{
    const size_t n0 = strlen(e->name);
    if (e->folder)
    {
        str_copy(stem, size, e->name);
        return 0;
    }
    size_t n = n0;
    int rank;
    if (n0 > 4 && str_icmp(e->name + n0 - 4, ".zip") == 0)
    {
        n = n0 - 4;
        rank = 1;
    }
    else if (n0 > 3 && str_icmp(e->name + n0 - 3, ".7z") == 0)
    {
        n = n0 - 3;
        rank = 2;
    }
    else
        return -1;
    if (n == 0 || n + 1 > size)
        return -1;
    memcpy(stem, e->name, n);
    stem[n] = '\0';
    return rank;
}

/* The status line of a game: the core's odin_check_set (odin_core_set_status), for the folder the game sits in. The
 * shelf's own name table (compat/swanui_odin_sets.inc) only lists the games; it does not decide the status. */
static void odin_status(Game *g, const char *dir, const OdinSetRow *row)
{
    g->incomplete = odin_core_set_status(dir, row->name, g->note, sizeof(g->note)) != 0;
}

static void scan_dir_odin(Library *lib, const char *dir, int depth, const OdinListing *root)
{
    OdinListing here;
    const bool readable = odin_listing_read(&here, dir);
    if (depth == 0 || !readable)
        SwanStationPS5_log("library: odin folder %s: %s, %d entries", dir, readable ? "listed" : "NOT readable", here.count);
    const OdinListing *top = depth == 0 ? &here : root;
    for (int i = 0; i < here.count; ++i)
    {
        char stem[256];
        const int rank = odin_entry_stem(&here.items[i], stem, sizeof(stem));
        if (rank < 0)
            continue;
        const OdinSetRow *row = odin_set_find(stem);
        if (!row)
            continue;
        /* the same set kept in a better form here (folder before zip before 7z) decides */
        bool beaten = false;
        for (int k = 0; k < here.count && !beaten; ++k)
        {
            char other[256];
            const int other_rank = odin_entry_stem(&here.items[k], other, sizeof(other));
            beaten = k != i && other_rank >= 0 && other_rank < rank && str_icmp(other, stem) == 0;
        }
        if (beaten)
            continue;
        Game *g = add_game(lib);
        if (!g)
            break;
        char full[SwanStationPS5_PATH_MAX];
        path_join(full, sizeof(full), dir, here.items[i].name);
        str_copy(g->path, sizeof(g->path), full);
        str_copy(g->folder, sizeof(g->folder), dir);
        str_copy(g->title, sizeof(g->title), row->title);
        str_copy(g->system, sizeof(g->system), row->system);
        str_copy(g->serial, sizeof(g->serial), row->name);
        str_copy(g->id, sizeof(g->id), row->name);
        set_disc_name(g, row->name);
        g->discs = 1;
        odin_status(g, dir, row);
        SwanStationPS5_log("library: odin set %s (%s): %s", row->name, row->system, g->note);
    }
    if (depth < ODIN_MAX_DEPTH)
        for (int i = 0; i < here.count; ++i)
        {
            const OdinEntry *e = &here.items[i];
            if (!e->folder || ares_skip_folder(e->name) || odin_set_find(e->name) || odin_set_is_support(e->name))
                continue;
            char sub[SwanStationPS5_PATH_MAX];
            path_join(sub, sizeof(sub), dir, e->name);
            scan_dir_odin(lib, sub, depth + 1, top);
        }
    odin_listing_free(&here);
}

#define ODIN_SCAN_ROOT(lib, dir) scan_dir_odin((lib), (dir), 0, NULL)
#else
#define ODIN_SCAN_ROOT(lib, dir) scan_dir_ares((lib), (dir), NULL, 0)
#endif

static int cmp_games(const void *a, const void *b)
{
    return str_icmp(((const Game *)a)->title, ((const Game *)b)->title);
}

void library_scan(Library *lib, const char *const *roots, int root_count)
{
    lib->count = 0;
    library_scan_append(lib, roots, root_count);
}

void library_scan_append(Library *lib, const char *const *roots, int root_count)
{
#if defined(ARES_UWP)
    /* ARES_UWP: every root walked to ARES_MAX_DEPTH folders; a root listed twice (the folder picked with the Xbox picker
     * may also be <drive>\ares) is walked once */
    for (int i = 0; i < root_count; ++i)
    {
        bool seen = false;
        for (int k = 0; k < i && !seen; ++k)
        {
            const char *a = roots[i], *b = roots[k];
            for (; *a && *b; ++a, ++b)
                if (!((*a == '/' || *a == '\\') && (*b == '/' || *b == '\\')) &&
                    tolower((unsigned char)*a) != tolower((unsigned char)*b))
                    break;
            while (*a == '/' || *a == '\\')
                ++a;
            while (*b == '/' || *b == '\\')
                ++b;
            seen = !*a && !*b;
        }
        if (!seen && path_is_dir(roots[i]))
            ODIN_SCAN_ROOT(lib, roots[i]);
    }
#else
    for (int i = 0; i < root_count; ++i)
        if (path_is_dir(roots[i]))
            scan_root(lib, roots[i]);
#endif
    if (lib->count > 1)
        qsort(lib->games, (size_t)lib->count, sizeof(Game), cmp_games);
    serial_cache_save();
    SwanStationPS5_log("library: %d games", lib->count);
}

bool library_load_index(Library *lib, const char *index_path)
{
    FILE *f = fopen(index_path, "r");
    if (!f)
        return false;
    lib->count = 0;
    char line[1024];
    while (fgets(line, sizeof(line), f))
    {
        line[strcspn(line, "\r\n")] = '\0';
        if (!line[0] || line[0] == '#')
            continue;
        char *field[5] = {0};
        char *p = line;
        for (int i = 0; i < 5 && p; ++i)
        {
            field[i] = p;
            p = strchr(p, '\t');
            if (p)
                *p++ = '\0';
        }
        if (!field[3] || !field[3][0])
            continue;
        Game *g = add_game(lib);
        if (!g)
            break;
        clean_title(field[0][0] ? field[0] : "Game", g->title, sizeof(g->title));
        str_copy(g->serial, sizeof(g->serial), field[1] ? field[1] : "");
        g->discs = field[2] ? atoi(field[2]) : 1;
        if (g->discs < 1)
            g->discs = 1;
        str_copy(g->path, sizeof(g->path), field[3]);
        str_copy(g->folder, sizeof(g->folder), field[3]);
        char *slash = strrchr(g->folder, '/');
        if (slash)
            *slash = '\0';
        /* already without extension; don't cut names like "Dr. Mario" at the dot */
        str_copy(g->disc_name, sizeof(g->disc_name), field[4] && field[4][0] ? field[4] : g->title);
        if (g->serial[0])
            str_copy(g->id, sizeof(g->id), g->serial);
        else
            if (!finish_game(g)) /* derive an id from the title */
                --lib->count; /* ARES_UWP: no system takes it */
    }
    fclose(f);
    if (lib->count > 1)
        qsort(lib->games, (size_t)lib->count, sizeof(Game), cmp_games);
    SwanStationPS5_log("library: %d games from index %s", lib->count, index_path);
    return true;
}

void library_free(Library *lib)
{
    free(lib->games);
    memset(lib, 0, sizeof(*lib));
}

/* ---------------------------------------------------------------- deleting a game */

#define DEL_MAX 64

typedef struct
{
    char path[DEL_MAX][SwanStationPS5_PATH_MAX];
    int count;
} DelList;

static void del_add(DelList *l, const char *folder, const char *full)
{
    size_t n = strlen(folder);
    if (strstr(full, "..") || l->count >= DEL_MAX)
        return;
    if (strncmp(full, folder, n) != 0 || full[n] != '/') /* only inside the game's own folder */
        return;
    for (int i = 0; i < l->count; ++i)
        if (!strcmp(l->path[i], full))
            return;
    str_copy(l->path[l->count++], SwanStationPS5_PATH_MAX, full);
}

/* `file` is a full path inside `folder`: it and what it lists, to delete */
static void del_collect(DelList *l, const char *folder, const char *file, int depth)
{
    char full[SwanStationPS5_PATH_MAX], dir[SwanStationPS5_PATH_MAX];
    str_copy(full, sizeof(full), file);
    del_add(l, folder, full);
    str_copy(dir, sizeof(dir), full);
    char *slash = strrchr(dir, '/');
    if (slash)
        *slash = '\0';
    const char *ext = path_ext(full);
    bool cue = str_icmp(ext, "cue") == 0, m3u = str_icmp(ext, "m3u") == 0;
    if ((cue || m3u) && depth < 2)
    {
        FILE *f = fopen(full, "r");
        char line[SwanStationPS5_PATH_MAX + 64];
        while (f && fgets(line, sizeof(line), f))
        {
            line[strcspn(line, "\r\n")] = '\0';
            char *name = line;
            if (cue)
            {
                char *q = line;
                while (*q == ' ' || *q == '\t')
                    ++q;
                if (strncasecmp(q, "FILE", 4) != 0)
                    continue;
                q += 4;
                while (*q == ' ' || *q == '\t')
                    ++q;
                if (*q == '"')
                {
                    name = q + 1;
                    char *end = strchr(name, '"');
                    if (!end)
                        continue;
                    *end = '\0';
                }
                else
                {
                    name = q;
                    char *end = strchr(name, ' ');
                    if (end)
                        *end = '\0';
                }
            }
            else if (!line[0] || line[0] == '#')
                continue;
            char listed[SwanStationPS5_PATH_MAX];
            path_join(listed, sizeof(listed), dir, name);
            if (strncmp(listed, folder, strlen(folder)) == 0 && !strstr(name, ".."))
                del_collect(l, folder, listed, depth + 1);
        }
        if (f)
            fclose(f);
    }
#if defined(TSUGARU_UWP) && !defined(ARES_UWP) /* ARES_UWP: a game's .sbi (PS1 LibCrypt data) goes with it, as in the PS1 build */
    /* TSUGARU_UWP: no LibCrypt .sbi on the FM Towns; an .mds sheet's data lives in the .mdf of the same name */
    if (str_icmp(ext, "mds") == 0)
    {
        char mdf[SwanStationPS5_PATH_MAX];
        str_copy(mdf, sizeof(mdf), full);
        char *dot = strrchr(mdf, '.');
        if (dot && (size_t)(dot - mdf) + 5 <= sizeof(mdf))
        {
            strcpy(dot, ".mdf");
            del_add(l, folder, mdf);
        }
    }
    if (0)
#else
    if (cue || str_icmp(ext, "chd") == 0 || str_icmp(ext, "iso") == 0 || str_icmp(ext, "bin") == 0 ||
        str_icmp(ext, "pbp") == 0 || str_icmp(ext, "img") == 0 || str_icmp(ext, "ecm") == 0)
#endif
    {
        char sbi[SwanStationPS5_PATH_MAX];
        str_copy(sbi, sizeof(sbi), full);
        char *dot = strrchr(sbi, '.');
        if (dot && (size_t)(dot - sbi) + 5 <= sizeof(sbi))
        {
            strcpy(dot, ".sbi");
            del_add(l, folder, sbi);
        }
    }
}

int library_delete_game_files(const Game *g)
{
#if defined(ODIN_UWP)
    (void)g; /* ODIN_UWP: a set's zip may be the parent of a clone; nothing is deleted from the shelf */
    return 0;
#endif
    if (!g->path[0] || !g->folder[0])
        return 0;
    static DelList list;
    list.count = 0;
    del_collect(&list, g->folder, g->path, 0);
    int removed = 0;
    for (int i = 0; i < list.count; ++i)
        if (remove(list.path[i]) == 0)
        {
            SwanStationPS5_log("library: deleted %s", list.path[i]);
            ++removed;
        }
    /* a folder SwanStationPS5 made for an imported disc ("Title [serial]"): gone with its files, if empty */
    size_t n = strlen(g->folder);
    if (removed && n > 2 && g->folder[n - 1] == ']')
        rmdir(g->folder);
    return removed;
}
