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

/* C entry points to the renderer of the Hikaru (src/odin/renderer.hpp), for
 * the parts of the machine that are still C. */

#ifndef ODIN_RENDERER_BRIDGE_HPP
#define ODIN_RENDERER_BRIDGE_HPP

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "odin/hikaru_gpu_bridge.hpp"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct odin_renderer odin_renderer_t;

/* The memories of the board the renderer reads */
typedef struct {
	const uint8_t *fb;		size_t fb_size;
	const uint8_t *texram[2];	size_t texram_size;
} odin_renderer_memory_t;

/* See odin::Renderer::Options. The two paths may be NULL. */
typedef struct {
	bool log;
	int log_frame;
	bool list_fp;
	int list_dump;
	bool lists_test;
	bool debug_keys;
	bool (*key_down)(void *ctx, char key);
	void *ctx;
	const char *programs_seed;
	const char *programs_kept;
} odin_renderer_options_t;

/* What a save state holds besides the pictures */
typedef struct {
	uint32_t num_lists;
	uint32_t shown;
	int32_t next;
	uint32_t launched;
} odin_renderer_counters_t;

/* The screen of the hardware, in pixels */
void odin_renderer_screen_size(unsigned *width, unsigned *height);

/* An OpenGL 4.3 context must be current, here and for every call below. */
odin_renderer_t *odin_renderer_new(const odin_renderer_memory_t *memory,
                                   const odin_renderer_options_t *options);
void odin_renderer_delete(odin_renderer_t *renderer);
void odin_renderer_set_gpu(odin_renderer_t *renderer, odin_hikaru_gpu_t *gpu);
/* Makes ready the mesh programs the lists of the options name; on the
 * thread the frames are drawn on. */
void odin_renderer_prepare_programs(odin_renderer_t *renderer);

/* Draws the 3D `scale` times finer than the screen of the hardware, 1 at
 * first. */
void odin_renderer_set_scale(odin_renderer_t *renderer, unsigned scale);

/* Filters the textures with this anisotropy; 1, the start, is none, as on
 * the board. */
void odin_renderer_set_anisotropy(odin_renderer_t *renderer, unsigned level);

/* A display list was launched. */
void odin_renderer_launch(odin_renderer_t *renderer);
/* The texture RAM of a bank is about to be written; every bank if
 * negative. */
void odin_renderer_invalidate_textures(odin_renderer_t *renderer, int bank);

/* A frame of the machine: begin_frame before it runs; take_frame once it
 * has, with the number of that frame and the size of the window in pixels;
 * draw_frame then draws what was taken. See odin::Renderer for what may
 * run meanwhile. */
void odin_renderer_begin_frame(odin_renderer_t *renderer);
void odin_renderer_take_frame(odin_renderer_t *renderer, unsigned frame,
                              unsigned window_w, unsigned window_h);
void odin_renderer_draw_frame(odin_renderer_t *renderer);
/* The drawing of the frame taken last writes a log. */
bool odin_renderer_frame_is_logged(const odin_renderer_t *renderer);
/* The picture of the last frame, whatever the size of the window: the
 * OpenGL framebuffer it can be read from, and its size. */
unsigned odin_renderer_frame_picture(const odin_renderer_t *renderer,
                                     unsigned *width, unsigned *height);
/* For the title of the window; empty without the debug keys. */
const char *odin_renderer_message(const odin_renderer_t *renderer);

/* Save state: ODIN_RENDERER_PICTURES pictures of
 * odin_renderer_picture_size() bytes, one after the other, and the
 * counters. */
#define ODIN_RENDERER_PICTURES 5
size_t odin_renderer_picture_size(void);
void odin_renderer_save_state(odin_renderer_t *renderer, uint8_t *pictures,
                              odin_renderer_counters_t *counters);
void odin_renderer_load_state(odin_renderer_t *renderer, const uint8_t *pictures,
                              const odin_renderer_counters_t *counters);

#ifdef __cplusplus
}
#endif

#endif /* ODIN_RENDERER_BRIDGE_HPP */
