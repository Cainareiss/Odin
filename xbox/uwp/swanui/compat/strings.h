/*
 * SwanStation Xbox (UWP) port - <strings.h> shim.
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * strcasecmp and strncasecmp come from the force-included swanui_compat.h (_stricmp/_strnicmp),
 * because main.c uses them without including <strings.h> (the PS5 <string.h> declares them).
 */
#ifndef SWANUI_STRINGS_H
#define SWANUI_STRINGS_H

#include <string.h>

#endif
