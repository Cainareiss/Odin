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

/* C entry points to the graphics chips of the Hikaru
 * (src/odin/hikaru_gpu.hpp), for the parts of the machine that are still
 * C. */

#ifndef ODIN_HIKARU_GPU_BRIDGE_HPP
#define ODIN_HIKARU_GPU_BRIDGE_HPP

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#include "odin/antarctic_list.h"
#include "odin/eurasia_layers.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct odin_hikaru_gpu odin_hikaru_gpu_t;

/* The memories of the board the chips work on */
typedef struct {
	uint8_t *cmdram;	size_t cmdram_size;
	uint8_t *ram_s;		size_t ram_s_size;
	uint8_t *fb;		size_t fb_size;
	uint8_t *texram[2];	size_t texram_size;
} odin_hikaru_gpu_memory_t;

/* What the board is told */
typedef struct {
	void *ctx;
	void (*irq_changed)(void *ctx);
	void (*list_started)(void *ctx);
	void (*texture_written)(void *ctx, unsigned bank);
} odin_hikaru_gpu_hooks_t;

odin_hikaru_gpu_t *odin_hikaru_gpu_new(const odin_hikaru_gpu_memory_t *memory,
                                       const odin_hikaru_gpu_hooks_t *hooks);
void odin_hikaru_gpu_delete(odin_hikaru_gpu_t *gpu);
void odin_hikaru_gpu_reset(odin_hikaru_gpu_t *gpu);

/* The bus, 2 or 4 bytes: 0, or -1 for a register that is not known. */
int odin_hikaru_gpu_read(const odin_hikaru_gpu_t *gpu, uint32_t addr, unsigned size, uint32_t *value);
int odin_hikaru_gpu_write(odin_hikaru_gpu_t *gpu, uint32_t addr, unsigned size, uint32_t value);

/* A slice of the frame, cycles of the SH-4 long. */
void odin_hikaru_gpu_run(odin_hikaru_gpu_t *gpu, uint32_t cycles);
void odin_hikaru_gpu_end_of_line(odin_hikaru_gpu_t *gpu, uint32_t line);
void odin_hikaru_gpu_vblank_in(odin_hikaru_gpu_t *gpu);
void odin_hikaru_gpu_vblank_out(odin_hikaru_gpu_t *gpu);

int odin_hikaru_gpu_irq_line(const odin_hikaru_gpu_t *gpu);
/* The frame as the game set it: dots a line, lines, lines shown. */
void odin_hikaru_gpu_frame_size(const odin_hikaru_gpu_t *gpu, unsigned *dots,
                                unsigned *lines, unsigned *shown);
/* 1500008C: the texture RAM twiddles what the bus writes to it. */
int odin_hikaru_gpu_texture_ram_twiddled(const odin_hikaru_gpu_t *gpu);

/* For the renderer: the list of the frame that begins (keep_last: the
 * list of the last frame is still needed, this one is built aside), the
 * mesh left open at the end of a frame, the framebuffer words and the
 * layers. */
odin_display_list_t *odin_hikaru_gpu_begin_list(odin_hikaru_gpu_t *gpu, int keep_last);
void odin_hikaru_gpu_end_open_mesh(odin_hikaru_gpu_t *gpu);
const odin_fb_config_t *odin_hikaru_gpu_fb_config(const odin_hikaru_gpu_t *gpu);
const odin_layers_t *odin_hikaru_gpu_layers(const odin_hikaru_gpu_t *gpu);

/* Traces, NULL for none: each instruction of the lists, each texture
 * transfer, each blit. */
void odin_hikaru_gpu_set_traces(odin_hikaru_gpu_t *gpu, FILE *lists, FILE *transfers, FILE *blits);
/* One line on where the chips are, in a buffer of the function */
const char *odin_hikaru_gpu_debug_str(const odin_hikaru_gpu_t *gpu);

/* Save state: a block of odin_hikaru_gpu_state_size() bytes. */
size_t odin_hikaru_gpu_state_size(void);
void odin_hikaru_gpu_save_state(const odin_hikaru_gpu_t *gpu, void *out);
void odin_hikaru_gpu_load_state(odin_hikaru_gpu_t *gpu, const void *in);

#ifdef __cplusplus
}
#endif

#endif /* ODIN_HIKARU_GPU_BRIDGE_HPP */
