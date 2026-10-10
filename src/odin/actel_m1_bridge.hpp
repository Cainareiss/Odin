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

#ifndef ODIN_ACTEL_M1_BRIDGE_HPP
#define ODIN_ACTEL_M1_BRIDGE_HPP

/* C entry points of the security FPGA of the SGNASCAR ROM board for
 * the Valkyrie core. */

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct odin_actel_m1 odin_actel_m1_t;

/* `rom` is the mask ROM data as the board holds it, IC19 and IC20
 * interleaved word by word; it must outlive the chip. */
odin_actel_m1_t *odin_actel_m1_new(uint32_t key, uint16_t id, const uint8_t *rom, size_t rom_size);
void odin_actel_m1_delete(odin_actel_m1_t *dev);
void odin_actel_m1_reset(odin_actel_m1_t *dev);

/* `offset` is the byte offset inside the 20-byte window at local bus
 * 0x0A000000: 0x00 control, 0x04 status, 0x08 ID, 0x0C scratch,
 * 0x10 data. A read of the data register takes `size` bytes off the
 * stream. */
uint32_t odin_actel_m1_read(odin_actel_m1_t *dev, uint32_t offset, unsigned size);
void odin_actel_m1_write(odin_actel_m1_t *dev, uint32_t offset, unsigned size, uint32_t value);

/* Save state: a block of odin_actel_m1_state_size() bytes. */
size_t odin_actel_m1_state_size(void);
void odin_actel_m1_save_state(const odin_actel_m1_t *dev, void *out);
void odin_actel_m1_load_state(odin_actel_m1_t *dev, const void *in);

#ifdef __cplusplus
}
#endif

#endif /* ODIN_ACTEL_M1_BRIDGE_HPP */
