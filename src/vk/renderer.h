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

	/* The screen of the machine, in pixels */
	unsigned width;
	unsigned height;
	/* The 3D is drawn `scale` times finer than that screen */
	unsigned scale;
	/* The picture of the frame, whatever the window shows of it: the
	 * framebuffer to read it from (0: the window) and its size. The
	 * screenshots and the fingerprints are taken there. */
	unsigned picture_fb;
	unsigned picture_width;
	unsigned picture_height;
	char message[256];

	/* Number of the frame being shown, set by the main loop */
	unsigned frame;

	void	(* destroy)(vk_renderer_t **renderer_);
	void	(* reset)(vk_renderer_t *renderer);
	void	(* begin_frame)(vk_renderer_t *renderer);
	void	(* end_frame)(vk_renderer_t *renderer);
};

int			 vk_renderer_init (vk_renderer_t *renderer);
/* The window: `times` the screen of the machine, as far as the display
 * has room for it, or the whole display. */
void			 vk_renderer_set_window (vk_renderer_t *renderer, unsigned times, bool fullscreen);
void			 vk_renderer_toggle_fullscreen (vk_renderer_t *renderer);
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

