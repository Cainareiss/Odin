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

/* C entry points to the Odin SH-4 (src/odin/sh4.hpp). */

#ifndef ODIN_SH4_BRIDGE_HPP
#define ODIN_SH4_BRIDGE_HPP

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct odin_sh4 odin_sh4_t;

#define ODIN_SH4_SOURCES 45	/* interrupt sources, see Sh4Source */
#define ODIN_SH4_PAGE_SIZE 0x1000

/* What the machine answers. Addresses of read and write are physical (29
 * bits); they return 0 when something answered. */
typedef struct {
	void *ctx;
	int (*read)(void *ctx, uint32_t addr, unsigned size, uint64_t *value);
	int (*write)(void *ctx, uint32_t addr, unsigned size, uint64_t value);
	uint16_t (*port_a_read)(void *ctx);
	void (*port_a_write)(void *ctx, uint16_t pins);
	/* An access nothing answered, at the address the program gave. */
	void (*unanswered)(void *ctx, int is_write, uint32_t addr, unsigned size, uint64_t value);
	/* The instruction at the hooked address is about to run. */
	void (*hook)(void *ctx);
} odin_sh4_bus_t;

/* The state between two runs; SR holds the T bit. */
typedef struct {
	uint32_t r[16];
	uint32_t pc, sr, pr, gbr, vbr, dbr, mach, macl, spc, ssr, sgr;
	uint32_t rbank[8];
	uint32_t fr[16], xf[16];
	uint32_t fpul, fpscr;
	uint32_t run;	/* 0 running, 1 stopped, 2 sleeping, 3 standby */
	uint32_t irq_pending;
	int32_t irq_index;
	struct {
		uint32_t raised, priority, offset, code;
	} irq[ODIN_SH4_SOURCES];
	uint8_t dmac_running[4];
	uint8_t tmu_running[3];
	uint32_t tmu_counter[3];
	uint8_t pm_running[2];
	uint64_t pm_counter[2];
	int32_t over;	/* cycles the last slice ran past its end */
} odin_sh4_state_t;

/* `onchip` is the 64 KB image of the on-chip registers, owned and saved by
 * the caller. */
odin_sh4_t *odin_sh4_new(const odin_sh4_bus_t *bus, uint8_t *onchip, int master, int little_endian,
                         unsigned clock_mode);
void odin_sh4_delete(odin_sh4_t *cpu);
void odin_sh4_reset(odin_sh4_t *cpu, int hard);

/* Plain memory: the page at physical address `page` * ODIN_SH4_PAGE_SIZE.
 * NULL sends the reads or the writes to the bus. */
void odin_sh4_map_page(odin_sh4_t *cpu, uint32_t page, const uint8_t *read, uint8_t *write);

/* Returns the cycles run past the end. */
int odin_sh4_run(odin_sh4_t *cpu, int cycles);
void odin_sh4_set_time(odin_sh4_t *cpu, uint64_t now);
/* Any source but NMI, which is a pin: the interrupt comes with its edge. */
void odin_sh4_set_irq(odin_sh4_t *cpu, unsigned source, int raised);
void odin_sh4_set_nmi(odin_sh4_t *cpu, int high);
void odin_sh4_set_run(odin_sh4_t *cpu, unsigned state);
unsigned odin_sh4_get_run(const odin_sh4_t *cpu);

void odin_sh4_set_hook(odin_sh4_t *cpu, uint32_t addr);
/* For the hook: the cycles left in the slice, and a way to count some of
 * them without running them. */
int odin_sh4_remaining(const odin_sh4_t *cpu);
void odin_sh4_skip(odin_sh4_t *cpu, uint32_t cycles);

uint32_t odin_sh4_reg(const odin_sh4_t *cpu, unsigned n);
void odin_sh4_set_reg(odin_sh4_t *cpu, unsigned n, uint32_t value);
uint32_t odin_sh4_pc(const odin_sh4_t *cpu);
uint32_t odin_sh4_pr(const odin_sh4_t *cpu);
uint32_t odin_sh4_sr(const odin_sh4_t *cpu);
uint32_t odin_sh4_gbr(const odin_sh4_t *cpu);

void odin_sh4_save_state(const odin_sh4_t *cpu, odin_sh4_state_t *out);
void odin_sh4_load_state(odin_sh4_t *cpu, const odin_sh4_state_t *in);

/* Since the reset: instructions run, and cycles counted without running. */
uint64_t odin_sh4_instructions(const odin_sh4_t *cpu);
uint64_t odin_sh4_skipped(const odin_sh4_t *cpu);

#ifdef __cplusplus
}
#endif

#endif /* ODIN_SH4_BRIDGE_HPP */
