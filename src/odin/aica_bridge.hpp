/*
 * Odin
 * Copyright (C) 2026 Réda Chérif-Touil
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 * 02110-1301, USA.
 */

#ifndef ODIN_AICA_BRIDGE_HPP
#define ODIN_AICA_BRIDGE_HPP

/* C entry points of one AICA sound board (the chip, its wave memory and
 * the ARM7 inside it) for the Valkyrie core. */

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct odin_aica odin_aica_t;
typedef void (*odin_aica_irq_fn)(void *ctx, int level);

/* `ram` is the 8 MB wave memory, owned by the caller. `host_clock_hz` is
 * the clock of the cycles passed to odin_aica_run. `main_irq` is called
 * with 1 when the chip requests the host CPU interrupt and 0 when it
 * drops it. */
odin_aica_t *odin_aica_new(uint8_t *ram, size_t ram_size, uint32_t host_clock_hz,
                           odin_aica_irq_fn main_irq, void *ctx);
void odin_aica_delete(odin_aica_t *dev);
/* The level of that line. */
int odin_aica_main_irq(const odin_aica_t *dev);
void odin_aica_reset(odin_aica_t *dev);

/* `offset` is the byte offset inside the board's 16 MB bus page:
 * 0x700000 registers, 0x710000 RTC, 0x800000 wave memory; size 1, 2, 4 or
 * 8 (wave memory only). Returns 0, or
 * -1 for an offset the board does not decode. */
int odin_aica_host_read(odin_aica_t *dev, uint32_t offset, unsigned size, uint64_t *value);
int odin_aica_host_write(odin_aica_t *dev, uint32_t offset, unsigned size, uint64_t value);

/* Advances the board by `cycles` host-CPU cycles. */
void odin_aica_run(odin_aica_t *dev, uint32_t cycles);

/* The ARM7 only counts the turns of a program that waits: this puts in the
 * wave memory what it has not written yet. Call it before the memory is
 * read from outside the bridge. */
void odin_aica_settle(odin_aica_t *dev);

void odin_aica_set_log(odin_aica_t *dev, FILE *out);
/* ARM7 instruction and bus trace in the charter format; NULL turns it off. */
void odin_aica_set_arm_trace(odin_aica_t *dev, FILE *out);

/* Save state: a block of odin_aica_state_size() bytes. */
size_t odin_aica_state_size(void);
void odin_aica_save_state(const odin_aica_t *dev, void *out);
void odin_aica_load_state(odin_aica_t *dev, const void *in);

#ifdef __cplusplus
}
#endif

#endif /* ODIN_AICA_BRIDGE_HPP */
