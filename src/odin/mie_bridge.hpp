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

#ifndef ODIN_MIE_BRIDGE_HPP
#define ODIN_MIE_BRIDGE_HPP

/* C entry points of the Hikaru I/O side for the Valkyrie core: the
 * 315-6146 MIE, its 93C46 EEPROM, the JVS link and the 837-13551 I/O
 * board on it. */

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct odin_mie odin_mie_t;

/* What the cabinet feeds the board. JVS bit order: system byte bit 7
 * TEST; player word high byte bit 7 START, 6 SERVICE, 5 UP, 4 DOWN,
 * 3 LEFT, 2 RIGHT, 1 PUSH1, 0 PUSH2; analog channels 0x8000 centre. */
typedef struct {
	int mainboard_test;	/* SW7 on the main board */
	int mainboard_service;	/* SW8 */
	uint8_t jvs_system;
	uint16_t jvs_player[2];
	uint16_t jvs_analog[8];
	int jvs_coin[2];	/* 1 = a coin went through since the last call */
} odin_mie_inputs_t;

typedef void (*odin_mie_inputs_fn)(void *ctx, odin_mie_inputs_t *inputs);

/* `rom` is the 2 KB 315-6146 ROM, copied. `ram` is the 32 KB half of
 * the Z80 memory at 0x8000-0xFFFF, owned by the caller and shared with
 * the host window. `host_clock_hz` is the clock of the cycles passed to
 * odin_mie_run. */
odin_mie_t *odin_mie_new(const uint8_t *rom, size_t rom_size, uint8_t *ram, size_t ram_size,
                         uint32_t host_clock_hz, odin_mie_inputs_fn read_inputs, void *ctx);
void odin_mie_delete(odin_mie_t *dev);
void odin_mie_reset(odin_mie_t *dev);

/* `offset` is the byte offset inside the 0x00800000-0x0083FFFF window:
 * 0x00000-0x1FFFF Z80 I/O ports, 0x20000-0x3FFFF Z80 memory, one byte
 * on each even address. Returns 0, or -1 for a size the window does
 * not take. */
int odin_mie_host_read(odin_mie_t *dev, uint32_t offset, unsigned size, uint32_t *value);
int odin_mie_host_write(odin_mie_t *dev, uint32_t offset, unsigned size, uint32_t value);

/* Bus request register (SH-4 0x01000000 and 0x01000100): bit 8 written
 * is BUSRQ, bit 8 read is clear while the bus is granted. */
uint32_t odin_mie_busrq_read(odin_mie_t *dev);
void odin_mie_busrq_write(odin_mie_t *dev, uint32_t value);

/* Advances the chip by `cycles` host-CPU cycles. */
void odin_mie_run(odin_mie_t *dev, uint32_t cycles);

/* Port D carries the resets of the main board, active low (bit 0 the
 * system, 1 the GPU, 2 the slave SH-4, 3 CN4, 4 the bridge, 5 the sound
 * boards), and on bits 6 and 7 the choice of the video clock. `fn` is
 * called with the pins each time the chip drives a new value. */
typedef void (*odin_mie_port_d_fn)(void *ctx, uint8_t pins);
void odin_mie_set_port_d(odin_mie_t *dev, odin_mie_port_d_fn fn, void *ctx);
/* The pins now; 0xFF while the chip does not drive them. */
uint8_t odin_mie_port_d(const odin_mie_t *dev);

/* The interrupt controller of the main board, on ports B, C and F. The
 * lines from outside, active high: bit 0 ANT_INT, 4 SZK_INT, 5 CN4_INT,
 * 6 CN3_INT0, 7 CN3_INT1. They are not part of the save state. */
void odin_mie_set_irq_lines(odin_mie_t *dev, uint8_t lines);
/* What port B reads. */
uint8_t odin_mie_irq_pending(const odin_mie_t *dev);
/* IRL2 of the main SH-4: 1 while something pending passes the mask. */
int odin_mie_irq_out(const odin_mie_t *dev);

void odin_mie_set_log(odin_mie_t *dev, FILE *out);
/* Z80 bus trace for `max_instructions` instructions from the first time
 * the PC reaches `start_pc` (0 = from reset). */
void odin_mie_set_trace(odin_mie_t *dev, FILE *out, uint32_t max_instructions, uint16_t start_pc);
/* Initial contents of the 128-byte EEPROM; blank (0xFF) otherwise. */
void odin_mie_eeprom_load(odin_mie_t *dev, const uint8_t *data, size_t size);
/* Copies the 128 bytes of the EEPROM out; `size` must hold them. */
void odin_mie_eeprom_read(odin_mie_t *dev, uint8_t *data, size_t size);
/* Puts a CRP-1231 card reader on the MIE serial block. `card_file`
 * (may be NULL) holds the card's three tracks and is loaded into the
 * reader when it exists; `trace` (may be NULL) gets one line per frame.
 * Returns 1 when a card was loaded, 0 when the reader starts empty. */
int odin_mie_card_attach(odin_mie_t *dev, const char *card_file, FILE *trace);

/* Save state: a block of odin_mie_state_size() bytes. The load fails (-1)
 * when the state has a card reader and the board has none, or the reverse. */
size_t odin_mie_state_size(void);
void odin_mie_save_state(odin_mie_t *dev, void *out);
int odin_mie_load_state(odin_mie_t *dev, const void *in);

#ifdef __cplusplus
}
#endif

#endif /* ODIN_MIE_BRIDGE_HPP */
