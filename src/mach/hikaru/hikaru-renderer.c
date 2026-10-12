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

void
hikaru_renderer_invalidate_texcache (vk_renderer_t *renderer, int bank)
{
	hikaru_renderer_t *hr = (hikaru_renderer_t *) renderer;

	odin_renderer_invalidate_textures (hr->odin, bank);
}

static void
hikaru_renderer_begin_frame (vk_renderer_t *renderer)
{
	hikaru_renderer_t *hr = (hikaru_renderer_t *) renderer;

	/* The frame being built follows the one shown. */
	odin_renderer_begin_frame (hr->odin, renderer->frame + 1);
	snprintf (renderer->message, sizeof (renderer->message), "%s",
	          odin_renderer_message (hr->odin));
}

static void
hikaru_renderer_end_frame (vk_renderer_t *renderer)
{
	hikaru_renderer_t *hr = (hikaru_renderer_t *) renderer;

	odin_renderer_end_frame (hr->odin, renderer->frame, renderer->scale);
}

static void
hikaru_renderer_reset (vk_renderer_t *renderer)
{
	hikaru_renderer_invalidate_texcache (renderer, -1);
}

/* The pictures the renderer keeps, one item each, then its four counters. */

int
hikaru_renderer_save_state (vk_renderer_t *renderer, vk_state_t *state)
{
	hikaru_renderer_t *hr = (hikaru_renderer_t *) renderer;
	const uint32_t size = (uint32_t) odin_renderer_picture_size ();
	uint8_t *pictures = (uint8_t *) malloc ((size_t) size * ODIN_RENDERER_PICTURES);
	odin_renderer_counters_t counters;
	int ret = 0;
	unsigned i;

	if (!pictures)
		return -1;

	odin_renderer_save_state (hr->odin, pictures, &counters);
	for (i = 0; i < ODIN_RENDERER_PICTURES; i++)
		ret |= vk_state_put (state, pictures + (size_t) i * size, size);
	ret |= vk_state_put (state, &counters.num_lists, sizeof (counters.num_lists));
	ret |= vk_state_put (state, &counters.shown, sizeof (counters.shown));
	ret |= vk_state_put (state, &counters.next, sizeof (counters.next));
	ret |= vk_state_put (state, &counters.launched, sizeof (counters.launched));

	free (pictures);
	return ret;
}

int
hikaru_renderer_load_state (vk_renderer_t *renderer, vk_state_t *state)
{
	hikaru_renderer_t *hr = (hikaru_renderer_t *) renderer;
	const uint32_t size = (uint32_t) odin_renderer_picture_size ();
	uint8_t *pictures = (uint8_t *) malloc ((size_t) size * ODIN_RENDERER_PICTURES);
	odin_renderer_counters_t counters;
	int ret = 0;
	unsigned i;

	if (!pictures)
		return -1;

	for (i = 0; i < ODIN_RENDERER_PICTURES && !ret; i++)
		ret |= vk_state_get (state, pictures + (size_t) i * size, size);
	ret |= vk_state_get (state, &counters.num_lists, sizeof (counters.num_lists));
	ret |= vk_state_get (state, &counters.shown, sizeof (counters.shown));
	ret |= vk_state_get (state, &counters.next, sizeof (counters.next));
	ret |= vk_state_get (state, &counters.launched, sizeof (counters.launched));
	if (!ret)
		odin_renderer_load_state (hr->odin, pictures, &counters);

	free (pictures);
	return ret;
}

static void
hikaru_renderer_destroy (vk_renderer_t **renderer_)
{
	if (renderer_ && *renderer_) {
		hikaru_renderer_t *hr = (hikaru_renderer_t *) *renderer_;

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
	hr->base.end_frame = hikaru_renderer_end_frame;

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

	return (vk_renderer_t *) hr;
}

void
hikaru_renderer_set_gpu (vk_renderer_t *renderer, odin_hikaru_gpu_t *chips)
{
	hikaru_renderer_t *hr = (hikaru_renderer_t *) renderer;

	odin_renderer_set_gpu (hr->odin, chips);
}
