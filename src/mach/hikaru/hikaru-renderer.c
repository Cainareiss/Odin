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

#include "vk/input.h"

#include "mach/hikaru/hikaru-renderer.h"
#include "odin/renderer_bridge.hpp"

/* The renderer of the Hikaru as the renderer of the machine: the drawing
 * itself is in src/odin (renderer.hpp). Here are the window, the options of
 * the environment and the save state. */

typedef struct {
	vk_renderer_t base;

	odin_renderer_t *odin;
} hikaru_renderer_t;

static bool
is_key_down (void *ctx, char key)
{
	return vk_input_get_key ((unsigned) key);
}

void
hikaru_renderer_launch (vk_renderer_t *renderer)
{
	hikaru_renderer_t *hr = (hikaru_renderer_t *) renderer;

	odin_renderer_launch (hr->odin);
}

/* The frame being drawn reads the texture RAM as its own frame left it;
 * the decoded textures of that bank are stale then. */
void
hikaru_renderer_before_texram_write (vk_renderer_t *renderer, unsigned bank)
{
	hikaru_renderer_t *hr = (hikaru_renderer_t *) renderer;

	vk_renderer_finish (renderer);
	odin_renderer_invalidate_textures (hr->odin, (int) bank);
}

static void
hikaru_renderer_begin_frame (vk_renderer_t *renderer)
{
	hikaru_renderer_t *hr = (hikaru_renderer_t *) renderer;

	odin_renderer_begin_frame (hr->odin);
}

static void
hikaru_renderer_take_frame (vk_renderer_t *renderer)
{
	hikaru_renderer_t *hr = (hikaru_renderer_t *) renderer;
	int w, h;

	SDL_GL_GetDrawableSize (renderer->window, &w, &h);
	odin_renderer_take_frame (hr->odin, renderer->frame, (unsigned) w, (unsigned) h);
	renderer->logged = odin_renderer_frame_is_logged (hr->odin);
	snprintf (renderer->message, sizeof (renderer->message), "%s",
	          odin_renderer_message (hr->odin));
}

/* On the drawing thread */
static void
hikaru_renderer_draw_frame (vk_renderer_t *renderer)
{
	hikaru_renderer_t *hr = (hikaru_renderer_t *) renderer;

	odin_renderer_set_scale (hr->odin, renderer->scale);
	odin_renderer_set_anisotropy (hr->odin, renderer->anisotropy);
	odin_renderer_draw_frame (hr->odin);
	renderer->picture_fb = odin_renderer_frame_picture (hr->odin,
	                                                    &renderer->picture_width,
	                                                    &renderer->picture_height);
}

static void
hikaru_renderer_reset (vk_renderer_t *renderer)
{
	hikaru_renderer_t *hr = (hikaru_renderer_t *) renderer;

	vk_renderer_finish (renderer);
	odin_renderer_invalidate_textures (hr->odin, -1);
}

/* The pictures the renderer keeps, one item each, then its four counters.
 * They are read and written on the drawing thread. */

typedef struct {
	odin_renderer_t *odin;
	uint8_t *pictures;
	odin_renderer_counters_t counters;
} kept_t;

static void
save_kept (void *ctx)
{
	kept_t *kept = (kept_t *) ctx;

	odin_renderer_save_state (kept->odin, kept->pictures, &kept->counters);
}

static void
load_kept (void *ctx)
{
	kept_t *kept = (kept_t *) ctx;

	odin_renderer_load_state (kept->odin, kept->pictures, &kept->counters);
}

int
hikaru_renderer_save_state (vk_renderer_t *renderer, vk_state_t *state)
{
	hikaru_renderer_t *hr = (hikaru_renderer_t *) renderer;
	const uint32_t size = (uint32_t) odin_renderer_picture_size ();
	kept_t kept = { hr->odin, (uint8_t *) malloc ((size_t) size * ODIN_RENDERER_PICTURES) };
	int ret = 0;
	unsigned i;

	if (!kept.pictures)
		return -1;

	vk_renderer_run (renderer, save_kept, &kept);
	for (i = 0; i < ODIN_RENDERER_PICTURES; i++)
		ret |= vk_state_put (state, kept.pictures + (size_t) i * size, size);
	ret |= vk_state_put (state, &kept.counters.num_lists, sizeof (kept.counters.num_lists));
	ret |= vk_state_put (state, &kept.counters.shown, sizeof (kept.counters.shown));
	ret |= vk_state_put (state, &kept.counters.next, sizeof (kept.counters.next));
	ret |= vk_state_put (state, &kept.counters.launched, sizeof (kept.counters.launched));

	free (kept.pictures);
	return ret;
}

int
hikaru_renderer_load_state (vk_renderer_t *renderer, vk_state_t *state)
{
	hikaru_renderer_t *hr = (hikaru_renderer_t *) renderer;
	const uint32_t size = (uint32_t) odin_renderer_picture_size ();
	kept_t kept = { hr->odin, (uint8_t *) malloc ((size_t) size * ODIN_RENDERER_PICTURES) };
	int ret = 0;
	unsigned i;

	if (!kept.pictures)
		return -1;

	for (i = 0; i < ODIN_RENDERER_PICTURES && !ret; i++)
		ret |= vk_state_get (state, kept.pictures + (size_t) i * size, size);
	ret |= vk_state_get (state, &kept.counters.num_lists, sizeof (kept.counters.num_lists));
	ret |= vk_state_get (state, &kept.counters.shown, sizeof (kept.counters.shown));
	ret |= vk_state_get (state, &kept.counters.next, sizeof (kept.counters.next));
	ret |= vk_state_get (state, &kept.counters.launched, sizeof (kept.counters.launched));
	if (!ret)
		vk_renderer_run (renderer, load_kept, &kept);

	free (kept.pictures);
	return ret;
}

static void
hikaru_renderer_destroy (vk_renderer_t **renderer_)
{
	if (renderer_ && *renderer_) {
		hikaru_renderer_t *hr = (hikaru_renderer_t *) *renderer_;

		vk_renderer_stop (*renderer_);
		if (hr->odin)
			odin_renderer_delete (hr->odin);
		hr->odin = NULL;
	}
}

vk_renderer_t *
hikaru_renderer_new (vk_buffer_t *fb, vk_buffer_t *texram[2])
{
	odin_renderer_memory_t memory;
	odin_renderer_options_t options;
	hikaru_renderer_t *hr;

	hr = ALLOC (hikaru_renderer_t);
	if (!hr)
		return NULL;

	odin_renderer_screen_size (&hr->base.width, &hr->base.height);

	hr->base.destroy = hikaru_renderer_destroy;
	hr->base.reset = hikaru_renderer_reset;
	hr->base.begin_frame = hikaru_renderer_begin_frame;
	hr->base.take_frame = hikaru_renderer_take_frame;
	hr->base.draw_frame = hikaru_renderer_draw_frame;

	/* The window and its OpenGL context */
	if (vk_renderer_init ((vk_renderer_t *) hr)) {
		free (hr);
		return NULL;
	}

	memory.fb		= (const uint8_t *) vk_buffer_get_ptr (fb, 0);
	memory.fb_size		= vk_buffer_get_size (fb);
	memory.texram[0]	= (const uint8_t *) vk_buffer_get_ptr (texram[0], 0);
	memory.texram[1]	= (const uint8_t *) vk_buffer_get_ptr (texram[1], 0);
	memory.texram_size	= vk_buffer_get_size (texram[0]);

	options.log		= vk_util_get_bool_option ("HR_LOG", false);
	options.log_frame	= vk_util_get_int_option ("HR_LOG_FRAME", 0);
	options.list_fp		= vk_util_get_bool_option ("ODIN_LIST_FP", false);
	options.list_dump	= vk_util_get_int_option ("ODIN_LIST_DUMP", -1);
	options.lists_test	= vk_util_get_int_option ("ODIN_LISTS_TEST", 0) != 0;
	options.debug_keys	= vk_util_get_bool_option ("ODIN_DEBUG_KEYS", false);
	options.key_down	= is_key_down;
	options.ctx		= NULL;

	hr->odin = odin_renderer_new (&memory, &options);
	if (!hr->odin) {
		free (hr);
		return NULL;
	}

	vk_renderer_start ((vk_renderer_t *) hr);

	return (vk_renderer_t *) hr;
}

void
hikaru_renderer_set_gpu (vk_renderer_t *renderer, odin_hikaru_gpu_t *chips)
{
	hikaru_renderer_t *hr = (hikaru_renderer_t *) renderer;

	odin_renderer_set_gpu (hr->odin, chips);
}
