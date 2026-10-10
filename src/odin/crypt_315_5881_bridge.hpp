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

#ifndef ODIN_CRYPT_315_5881_BRIDGE_HPP
#define ODIN_CRYPT_315_5881_BRIDGE_HPP

/* C entry points of the ROM board 315-5881 decryption chip for the
 * Valkyrie core. */

#include <stdint.h>
#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct odin_crypt5881 odin_crypt5881_t;

/* `read` returns the ciphertext word for a word counter, the way the
 * chip sees the protected RAM at local bus 0x0A000000: the word at
 * (addr & 0x7fff) * 2, bytes swapped. */
odin_crypt5881_t *odin_crypt5881_new(uint32_t key, uint16_t (*read)(void *ctx, uint32_t addr),
                                     void *ctx);
void odin_crypt5881_delete(odin_crypt5881_t *dev);
void odin_crypt5881_reset(odin_crypt5881_t *dev);

/* `offset` is the byte offset inside the 16-byte window at local bus
 * 0x0A010000, 16-bit accesses: 0x0 read ready, 0x8 write address low,
 * 0xA write address high, 0xC write sequence key, 0xE read the next
 * plaintext word. Anything else reads 0 and is logged once. */
uint32_t odin_crypt5881_read(odin_crypt5881_t *dev, uint32_t offset, unsigned size);
void odin_crypt5881_write(odin_crypt5881_t *dev, uint32_t offset, unsigned size, uint32_t value);

void odin_crypt5881_set_log(odin_crypt5881_t *dev, FILE *out);

/* Save state: a block of odin_crypt5881_state_size() bytes. */
size_t odin_crypt5881_state_size(void);
void odin_crypt5881_save_state(const odin_crypt5881_t *dev, void *out);
void odin_crypt5881_load_state(odin_crypt5881_t *dev, const void *in);

#ifdef __cplusplus
}
#endif

#endif /* ODIN_CRYPT_315_5881_BRIDGE_HPP */
