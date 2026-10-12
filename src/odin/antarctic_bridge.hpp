/*
 * Odin
 * Copyright (C) 2026 Réda Chérif-Touil
 *
 * Valkyrie
 * Copyright (C) 2011-2014, Stefano Teso
 *
 * Valkyrie is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * Valkyrie is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with Valkyrie.  If not, see <http://www.gnu.org/licenses/>.
 */

/* C entry points to the command processor (src/odin/antarctic.hpp), for
 * the parts of the machine that are still C. */

#ifndef ODIN_ANTARCTIC_BRIDGE_HPP
#define ODIN_ANTARCTIC_BRIDGE_HPP

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#include "odin/antarctic_list.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct odin_antarctic odin_antarctic_t;

/* cmdram: the command RAM; ram_s: the RAM of the slave SH-4. */
odin_antarctic_t *odin_antarctic_new(uint8_t *cmdram, size_t cmdram_size,
                                     uint8_t *ram_s, size_t ram_s_size);
void odin_antarctic_delete(odin_antarctic_t *cp);
void odin_antarctic_reset(odin_antarctic_t *cp);

/* 15000058 = 3: a list starts at pc with the two stack pointers. */
void odin_antarctic_start(odin_antarctic_t *cp, uint32_t pc, uint32_t sp0, uint32_t sp1);
int odin_antarctic_running(const odin_antarctic_t *cp);
/* Up to budget instructions; 1 when the list ended during the call. */
int odin_antarctic_run(odin_antarctic_t *cp, uint32_t budget);
/* The vertical blanking begins. */
void odin_antarctic_frame_start(odin_antarctic_t *cp);
/* A frame ends: the mesh left open is closed. */
void odin_antarctic_end_open_mesh(odin_antarctic_t *cp);
uint32_t odin_antarctic_pc(const odin_antarctic_t *cp);

/* The list of the frame that begins; keep_last: the list of the last frame
 * is still needed, this one is built aside. */
odin_display_list_t *odin_antarctic_begin_list(odin_antarctic_t *cp, int keep_last);
const odin_fb_config_t *odin_antarctic_fb_config(const odin_antarctic_t *cp);

/* Each instruction is printed on out as it is executed; NULL for none. */
void odin_antarctic_set_disasm(odin_antarctic_t *cp, FILE *out);

/* Save state: a block of odin_antarctic_state_size() bytes. */
size_t odin_antarctic_state_size(void);
const void *odin_antarctic_state(const odin_antarctic_t *cp);
void odin_antarctic_load_state(odin_antarctic_t *cp, const void *in);

#ifdef __cplusplus
}
#endif

#endif /* ODIN_ANTARCTIC_BRIDGE_HPP */
