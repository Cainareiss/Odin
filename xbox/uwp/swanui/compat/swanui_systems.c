/* Odin (experimental) - the system registry and the Odin set names (see swanui_systems.h).
 * Rewritten for the Odin shell (2026-11-10). A game file is recognised by its name only: the stem of a zip, a 7z or a
 * folder equals one of the 14 Odin game sets. No file is opened here; the contents are not checked in this build.
 * SPDX-License-Identifier: GPL-3.0-or-later */
#include "swanui_systems.h"

#include <ctype.h>
#include <stddef.h>
#include <string.h>

static const SwanSystem SYSTEMS[SWANUI_SYSTEM_COUNT] = {
    {"hikaru", "Sega Hikaru", "Hikaru", "Hikaru", "zip 7z", NULL, 4.0f / 3.0f, 0, 0},
    {"gaelco", "Gaelco ATV Track", "Gaelco", "Gaelco", "zip 7z", NULL, 4.0f / 3.0f, 0, 0},
};

/* Folder names that stand for a system besides its id and display name: lower case letters and digits only. */
static const char *const SYSTEM_ALIASES[SWANUI_SYSTEM_COUNT] = {
    "segahikaru",
    "gaelcoatv gaelcoatvtrack atvtrack",
};

#include "swanui_odin_sets.inc"

#define ODIN_ROW_COUNT ((int)(sizeof(ODIN_SET_ROWS) / sizeof(ODIN_SET_ROWS[0])))

const SwanSystem *swanui_systems(int *count)
{
    if (count)
        *count = SWANUI_SYSTEM_COUNT;
    return SYSTEMS;
}

int swanui_system_index(const char *id)
{
    if (!id || !id[0])
        return -1;
    for (int i = 0; i < SWANUI_SYSTEM_COUNT; ++i)
        if (strcmp(SYSTEMS[i].id, id) == 0)
            return i;
    return -1;
}

const SwanSystem *swanui_system_by_id(const char *id)
{
    const int i = swanui_system_index(id);
    return i >= 0 ? &SYSTEMS[i] : NULL;
}

/* ASCII case-insensitive equality */
static int ieq(const char *a, const char *b)
{
    for (; *a && *b; ++a, ++b)
        if (tolower((unsigned char)*a) != tolower((unsigned char)*b))
            return 0;
    return *a == *b;
}

/* lower case letters and digits of s, into out */
static void fold_name(const char *s, char *out, size_t size)
{
    size_t w = 0;
    for (; s && *s && w + 1 < size; ++s)
        if (isalnum((unsigned char)*s))
            out[w++] = (char)tolower((unsigned char)*s);
    out[w] = '\0';
}

/* The set name of a path: its last component without a trailing separator and without .zip / .7z. */
static int set_stem(const char *path, char *out, size_t size)
{
    if (!path || !path[0])
        return 0;
    size_t n = strlen(path);
    while (n > 0 && (path[n - 1] == '/' || path[n - 1] == '\\'))
        --n;
    size_t start = n;
    while (start > 0 && path[start - 1] != '/' && path[start - 1] != '\\')
        --start;
    size_t len = n - start;
    if (len >= 4 && (ieq(path + start + len - 4, ".zip")))
        len -= 4;
    else if (len >= 3 && ieq(path + start + len - 3, ".7z"))
        len -= 3;
    if (len == 0 || len + 1 > size)
        return 0;
    memcpy(out, path + start, len);
    out[len] = '\0';
    return 1;
}

const OdinSetRow *odin_set_find(const char *name)
{
    if (!name || !name[0])
        return NULL;
    for (int i = 0; i < ODIN_ROW_COUNT; ++i)
        if (ieq(name, ODIN_SET_ROWS[i].name))
            return &ODIN_SET_ROWS[i];
    return NULL;
}

int odin_set_is_support(const char *name)
{
    if (!name || !name[0])
        return 0;
    for (size_t i = 0; ODIN_SUPPORT_SETS[i]; ++i)
        if (ieq(name, ODIN_SUPPORT_SETS[i]))
            return 1;
    return 0;
}

int odin_set_count(void)
{
    return ODIN_ROW_COUNT;
}

const OdinSetRow *odin_set_row(int i)
{
    return i >= 0 && i < ODIN_ROW_COUNT ? &ODIN_SET_ROWS[i] : NULL;
}

const char *swanui_system_of_path(const char *path)
{
    char stem[256];
    if (!set_stem(path, stem, sizeof(stem)))
        return NULL;
    const OdinSetRow *row = odin_set_find(stem);
    return row ? row->system : NULL;
}

const SwanSystem *swanui_system_of_folder_name(const char *name)
{
    char folded[96];
    fold_name(name, folded, sizeof(folded));
    if (!folded[0])
        return NULL;
    for (int i = 0; i < SWANUI_SYSTEM_COUNT; ++i)
    {
        if (strcmp(folded, SYSTEMS[i].id) == 0)
            return &SYSTEMS[i];
        /* the alias list: space separated tokens, already folded */
        const char *p = SYSTEM_ALIASES[i];
        while (*p)
        {
            const char *end = p;
            while (*end && *end != ' ')
                ++end;
            const size_t len = (size_t)(end - p);
            if (len == strlen(folded) && memcmp(p, folded, len) == 0)
                return &SYSTEMS[i];
            p = *end ? end + 1 : end;
        }
    }
    return NULL;
}

int swanui_system_known_extension(const char *name)
{
    if (!name)
        return 0;
    const char *dot = strrchr(name, '.');
    return dot && (ieq(dot, ".zip") || ieq(dot, ".7z"));
}
