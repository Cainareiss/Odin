/* Tsugaru (FM Towns, from the SwanStation Xbox port) - the library's disc filter (see swanui_xbox_library.h).
 * SPDX-License-Identifier: GPL-3.0-or-later */
#include "swanui_xbox_library.h"

#include "SwanStationPS5.h" /* SwanStationPS5_PATH_MAX, path_join, path_ext, str_icmp, str_copy, SwanStationPS5_log */

#include <ctype.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

enum
{
    MAX_SHEET_FILES = 64
};

static const char *name_at(const char *names, size_t stride, int i)
{
    return names + (size_t)i * stride;
}

static int find_name(const char *names, size_t stride, int count, const char *name)
{
    for (int i = 0; i < count; ++i)
        if (str_icmp(name_at(names, stride, i), name) == 0)
            return i;
    return -1;
}

static bool ext_is(const char *name, const char *ext)
{
    return str_icmp(path_ext(name), ext) == 0;
}

static bool starts_icase(const char *s, const char *word)
{
    for (; *word; ++s, ++word)
        if (tolower((unsigned char)*s) != tolower((unsigned char)*word))
            return false;
    return true;
}

/* The FILE lines of a cue sheet: FILE "name" BINARY, or FILE name BINARY. A path from another machine counts by its
 * last component only. refs[k] gets the index of the k-th file in names, -1 when it is not in the folder. Returns the
 * number of FILE lines, -1 when the sheet cannot be read. */
static int cue_files(const char *dir, const char *cue, const char *names, size_t stride, int count, int *refs)
{
    char path[SwanStationPS5_PATH_MAX];
    path_join(path, sizeof(path), dir, cue);
    FILE *f = fopen(path, "r");
    if (!f)
        return -1;
    char line[600];
    int n = 0;
    while (fgets(line, sizeof(line), f) && n < MAX_SHEET_FILES)
    {
        char *p = line;
        while (*p == ' ' || *p == '\t')
            ++p;
        if (!starts_icase(p, "FILE") || !isspace((unsigned char)p[4]))
            continue;
        p += 4;
        while (*p == ' ' || *p == '\t')
            ++p;
        char *name = p, *end;
        if (*p == '"')
        {
            name = ++p;
            end = strchr(p, '"');
        }
        else
        {
            end = p;
            while (*end && *end != ' ' && *end != '\t' && *end != '\r' && *end != '\n')
                ++end;
        }
        if (!end)
            continue;
        *end = '\0';
        const char *base = name;
        for (const char *q = name; *q; ++q)
            if (*q == '/' || *q == '\\')
                base = q + 1;
        refs[n++] = find_name(names, stride, count, base);
    }
    fclose(f);
    return n;
}

/* A CloneCD or Alcohol sheet: the image has the sheet's name with .img (+ .sub) or .mdf. */
static int sibling_files(const char *sheet, const char *names, size_t stride, int count, int *refs)
{
    char base[300];
    str_copy(base, sizeof(base), sheet);
    char *dot = strrchr(base, '.');
    if (dot)
        *dot = '\0';
    char file[320];
    snprintf(file, sizeof(file), "%s.%s", base, ext_is(sheet, "ccd") ? "img" : "mdf");
    int n = 0;
    refs[n++] = find_name(names, stride, count, file);
    if (ext_is(sheet, "ccd"))
    {
        snprintf(file, sizeof(file), "%s.sub", base);
        const int sub = find_name(names, stride, count, file);
        if (sub >= 0)
            refs[n++] = sub; /* subchannel data: part of the disc when present, not required */
    }
    return n;
}

/* The usual dump: "X.cue" beside "X.bin" or "X (Track 1).bin", "X (Track 2).bin"... Found by name, without reading
 * the sheet (a read is a round trip to the broker on the console: hundreds of milliseconds for each sheet of a big
 * library). The images with the sheet's name as a prefix and a .bin/.img/.iso extension are its parts; "X.bin.ecm"
 * has the extension ecm and does not count, which is how a sheet whose .bin was compressed away is told apart. */
static int prefixed_images(const char *sheet, const char *names, size_t stride, int count, int *refs)
{
    char base[300];
    str_copy(base, sizeof(base), sheet);
    char *dot = strrchr(base, '.');
    if (dot)
        *dot = '\0';
    const size_t len = strlen(base);
    int n = 0;
    for (int i = 0; i < count && n < MAX_SHEET_FILES; ++i)
    {
        const char *name = name_at(names, stride, i);
        if (!(ext_is(name, "bin") || ext_is(name, "img") || ext_is(name, "iso")))
            continue;
        size_t k = 0;
        while (k < len && tolower((unsigned char)name[k]) == tolower((unsigned char)base[k]))
            ++k;
        if (k == len)
            refs[n++] = i;
    }
    return n;
}

void swanui_xbox_hide_sheet_parts(const char *dir, const char *names, size_t stride, int count, unsigned char *hidden)
{
    if (!dir || !names || !hidden || count <= 0)
        return;
    for (int i = 0; i < count; ++i)
    {
        const char *name = name_at(names, stride, i);
        int refs[MAX_SHEET_FILES];
        int n;
        if (ext_is(name, "cue"))
        {
            n = prefixed_images(name, names, stride, count, refs);
            if (n <= 0)
                n = cue_files(dir, name, names, stride, count, refs); /* an unusual name: the sheet itself decides */
        }
        else if (ext_is(name, "ccd") || ext_is(name, "mds"))
            n = sibling_files(name, names, stride, count, refs);
        else
            continue;
        if (n <= 0)
            continue; /* unreadable, or no FILE line: left as the frontend had it */
        bool complete = true;
        for (int k = 0; k < n; ++k)
            if (refs[k] < 0)
                complete = false;
        if (!complete)
        {
            hidden[i] = 1;
            SwanStationPS5_log("library: %s refers to a file that is not beside it, hidden", name);
            continue;
        }
        for (int k = 0; k < n; ++k)
            if (refs[k] != i)
                hidden[refs[k]] = 1; /* the sheet is the disc; its image files are not discs of their own */
    }
}
