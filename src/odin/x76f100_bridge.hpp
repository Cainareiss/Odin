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

#ifndef ODIN_X76F100_BRIDGE_HPP
#define ODIN_X76F100_BRIDGE_HPP

/* C entry points of the ROM board X76F100 EEPROM for the Valkyrie
 * core. */

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct odin_x76f100 odin_x76f100_t;

odin_x76f100_t *odin_x76f100_new(void);
void odin_x76f100_delete(odin_x76f100_t *dev);
/* `data` is the 0x84-byte image: response to reset (4), write password
 * (8), read password (8), data (112). Any other size is ignored. */
void odin_x76f100_load(odin_x76f100_t *dev, const uint8_t *data, size_t size);
void odin_x76f100_reset(odin_x76f100_t *dev);
/* Pin levels; CS and RST are active low. */
void odin_x76f100_set_pins(odin_x76f100_t *dev, int cs, int rst, int scl, int sda);
/* SDA as the chip drives it, 1 when released. */
int odin_x76f100_sda(odin_x76f100_t *dev);

/* Save state: a block of odin_x76f100_state_size() bytes. */
size_t odin_x76f100_state_size(void);
void odin_x76f100_save_state(const odin_x76f100_t *dev, void *out);
void odin_x76f100_load_state(odin_x76f100_t *dev, const void *in);

#ifdef __cplusplus
}
#endif

#endif /* ODIN_X76F100_BRIDGE_HPP */
