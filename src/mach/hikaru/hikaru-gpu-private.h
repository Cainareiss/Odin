/* 
 * Valkyrie
 * Copyright (C) 2011-2013, Stefano Teso
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

#ifndef __HIKARU_GPU_PRIVATE_H__
#define __HIKARU_GPU_PRIVATE_H__

#include "vk/device.h"
#include "odin/antarctic_bridge.hpp"

/* PHARRIER includes some debug text providing names for (some of the) GPU
 * IRQs. In particular, the menu at PH:@0C1D4E38 references both text (located
 * at PH:@0C126DC4 and beyond) and pointers to counter/timer variables (located
 * at PH:@0C27D0BC). These variables are updated through the function at
 * @0C0CF020, which is conveniently called from the GPU IRQ handlers (stored at
 * 0C000A04). Hence the connection. */

#define GPU15_IRQ_IDMA_END		0x01
#define GPU15_IRQ_POLY_OUT_END		0x02
#define GPU15_IRQ_CMD_ANALYSIS_END	0x04
#define GPU15_IRQ_GPU1A			0x80

#define GPU1A_IRQ_VBLANK		0x02
#define GPU1A_IRQ_PLOT_END		0x04

typedef struct {
	uint32_t x0, y0, x1, y1;
	uint32_t format		: 4;
	uint32_t enabled	: 1;
	uint32_t keyed		: 1;	/* unit register 3C: pixels without the alpha bit are transparent */
	uint32_t above		: 1;	/* unit register 38: drawn above the 3D, below it otherwise */
} hikaru_layer_t;

typedef struct {
	vk_device_t base;

	vk_buffer_t *cmdram;
	vk_buffer_t *texram[2];
	vk_buffer_t *fb;

	vk_renderer_t *renderer;

	/* The command processor, see src/odin/antarctic.hpp */
	odin_antarctic_t *cp;

	struct {
		uint8_t _15[0x100];
		uint8_t _18[0x100];
		uint8_t _1A[0x104];
		uint8_t _1A_unit[2][0x40];
		uint8_t _1A_dma[0x10];
		uint16_t _00400000;
	} regs;

	struct {
		uint32_t left;	/* entries still to copy */
	} idma;

	struct {
		hikaru_layer_t layer[2][2];
		bool enabled;
		bool fb_keyed;	/* unit 0 register 3C bit 0 */
	} layers;

	struct {
		uint32_t log_dma	: 1;
		uint32_t log_idma	: 1;
		uint32_t log_cp		: 1;
	} debug;

} hikaru_gpu_t;

#define REG15(addr_)	(*(uint32_t *) &gpu->regs._15[(addr_) & 0xFF])
#define REG18(addr_)	(*(uint32_t *) &gpu->regs._18[(addr_) & 0xFF])
#define REG1A(addr_)	(*(uint32_t *) &gpu->regs._1A[(addr_) & 0x1FF])
#define REG1AUNIT(n,a)	(*(uint32_t *) &gpu->regs._1A_unit[n][(a) & 0x3F])
#define REG1ADMA(a)	(*(uint32_t *) &gpu->regs._1A_dma[(a) & 0xF])

#define LAYERS		gpu->layers

/* hikaru-gpu.c */
void hikaru_gpu_raise_irq (hikaru_gpu_t *gpu, uint32_t _15, uint32_t _1A);

/* hikaru-renderer.c */
void hikaru_renderer_launch (vk_renderer_t *rend);
void hikaru_renderer_invalidate_texcache (vk_renderer_t *rend,
                                          const odin_texhead_t *th);

#endif /* __HIKARU_GPU_PRIVATE_H__ */
