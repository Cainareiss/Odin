// Tsugaru UWP port - the frontend's gamedb.h with an empty database. The PS1 database (DuckStation's) does not
// describe FM Towns titles, so no game is "known": gamedb_get() answers NULL, which every caller treats as
// "unknown disc" (no fixes, no controller hints, the shelf shows file titles and the art lookups by title still work).
// SPDX-License-Identifier: GPL-3.0-or-later

#include "swanui_cdecl.h"
#define Settings SwanSettings
extern "C" {
#include "src/SwanStationPS5.h"
#include "src/gamedb.h"
}
#undef Settings

extern "C" void gamedb_start(void)
{
}

extern "C" const GameInfo *gamedb_get(const char *serial)
{
    (void)serial;
    return nullptr;
}
