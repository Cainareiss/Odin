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

#ifndef __VK_HIKARU_RENDERER_H__
#define __VK_HIKARU_RENDERER_H__

#include "vk/core.h"
#include "vk/buffer.h"
#include "vk/renderer.h"
#include "vk/state.h"
#include "odin/hikaru_gpu_bridge.hpp"

vk_renderer_t	*hikaru_renderer_new (vk_buffer_t *fb, vk_buffer_t *texram[2]);
void		 hikaru_renderer_set_gpu (vk_renderer_t *, odin_hikaru_gpu_t *chips);
/* A display list was launched */
void		 hikaru_renderer_launch (vk_renderer_t *);
/* The texture RAM of a bank is about to be written */
void		 hikaru_renderer_before_texram_write (vk_renderer_t *, unsigned bank);
int		 hikaru_renderer_save_state (vk_renderer_t *, vk_state_t *);
int		 hikaru_renderer_load_state (vk_renderer_t *, vk_state_t *);

#endif /* __VK_HIKARU_RENDERER_H__ */
