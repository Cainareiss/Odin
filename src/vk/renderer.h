/* 
 * Valkyrie
 * Copyright (C) 2011, Stefano Teso
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

#ifndef __VK_REND_H__
#define __VK_REND_H__

#include "vk/core.h"

typedef struct vk_renderer_t vk_renderer_t;

struct vk_renderer_t {
	SDL_Window *window;
	SDL_GLContext *gl_context;

	unsigned width;
	unsigned height;
	/* The window is the picture, scale times as wide and as high */
	unsigned scale;
	char message[256];

	/* Number of the frame being shown, set by the main loop */
	unsigned frame;

	void	(* destroy)(vk_renderer_t **renderer_);
	void	(* reset)(vk_renderer_t *renderer);
	void	(* begin_frame)(vk_renderer_t *renderer);
	void	(* end_frame)(vk_renderer_t *renderer);
};

int			 vk_renderer_init (vk_renderer_t *renderer);
void			 vk_renderer_set_scale (vk_renderer_t *renderer, unsigned scale);
void			 vk_renderer_begin_frame (vk_renderer_t *renderer);
void			 vk_renderer_end_frame (vk_renderer_t *renderer);
void			 vk_renderer_clear_gl_errors (void);

static inline void
vk_renderer_reset (vk_renderer_t *renderer)
{
	renderer->reset (renderer);
}

static inline void
vk_renderer_destroy (vk_renderer_t **renderer_)
{
	if (renderer_) {
		(*renderer_)->destroy (renderer_);

		free (*renderer_);
		*renderer_ = NULL;
	}
}

#endif /* __VK_REND_H__ */

