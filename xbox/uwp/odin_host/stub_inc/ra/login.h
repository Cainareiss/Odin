/*
 * Odin shell: the RetroAchievements sign-in call the frontend makes, inert (odin_host/odin_stubs.c). See
 * ra/achievements.h.
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#ifndef SwanStationPS5_RA_LOGIN_H
#define SwanStationPS5_RA_LOGIN_H

#include "SwanStationPS5.h"

bool ra_password_login(const char *name, const char *password, char *token, size_t token_size, char *who,
                       size_t who_size, char *error, size_t error_size);

#endif
