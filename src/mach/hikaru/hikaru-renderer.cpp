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

#include <sys/stat.h>
#include <unistd.h>

#include "vk/input.h"

#include "mach/hikaru/hikaru-renderer.h"
#include "odin/renderer.hpp"

/* The renderer of the Hikaru as the renderer of the machine: the drawing
 * itself is in src/odin (renderer.hpp). Here are the window, the options of
 * the environment and the save state. */

typedef struct {
	vk_renderer_t base;

	odin::Renderer *odin;
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

	hr->odin->launch ();
}

/* The frame being drawn reads the texture RAM as its own frame left it;
 * the decoded textures of that bank are stale then. */
void
hikaru_renderer_before_texram_write (vk_renderer_t *renderer, unsigned bank)
{
	hikaru_renderer_t *hr = (hikaru_renderer_t *) renderer;

	vk_renderer_finish (renderer);
	hr->odin->invalidate_textures ((int) bank);
}

static void
hikaru_renderer_begin_frame (vk_renderer_t *renderer)
{
	hikaru_renderer_t *hr = (hikaru_renderer_t *) renderer;

	hr->odin->begin_frame ();
}

static void
hikaru_renderer_take_frame (vk_renderer_t *renderer)
{
	hikaru_renderer_t *hr = (hikaru_renderer_t *) renderer;
	int w, h;

	SDL_GL_GetDrawableSize (renderer->window, &w, &h);
	hr->odin->take_frame (renderer->frame, (unsigned) w, (unsigned) h);
	renderer->logged = hr->odin->frame_is_logged ();
	snprintf (renderer->message, sizeof (renderer->message), "%s", hr->odin->message ());
}

/* On the drawing thread */
static void
hikaru_renderer_draw_frame (vk_renderer_t *renderer)
{
	hikaru_renderer_t *hr = (hikaru_renderer_t *) renderer;

	hr->odin->set_scale (renderer->scale);
	hr->odin->set_anisotropy (renderer->anisotropy);
	hr->odin->draw_frame ();
	renderer->picture_fb = hr->odin->frame_picture (renderer->picture_width,
	                                                renderer->picture_height);
}

static void
hikaru_renderer_reset (vk_renderer_t *renderer)
{
	hikaru_renderer_t *hr = (hikaru_renderer_t *) renderer;

	vk_renderer_finish (renderer);
	hr->odin->invalidate_textures (-1);
}

/* The pictures the renderer keeps, one item each, then its four counters.
 * They are read and written on the drawing thread. */

static const size_t PICTURES_SIZE = odin::Renderer::PICTURES * odin::Renderer::PICTURE_SIZE;

typedef struct {
	odin::Renderer *odin;
	uint8_t *pictures;
	odin::Renderer::Counters counters;
} kept_t;

static void
save_kept (void *ctx)
{
	kept_t *kept = (kept_t *) ctx;

	kept->odin->save (std::span<uint8_t> (kept->pictures, PICTURES_SIZE), kept->counters);
}

static void
load_kept (void *ctx)
{
	kept_t *kept = (kept_t *) ctx;

	kept->odin->load (std::span<const uint8_t> (kept->pictures, PICTURES_SIZE), kept->counters);
}

int
hikaru_renderer_save_state (vk_renderer_t *renderer, vk_state_t *state)
{
	hikaru_renderer_t *hr = (hikaru_renderer_t *) renderer;
	const uint32_t size = (uint32_t) odin::Renderer::PICTURE_SIZE;
	kept_t kept = { hr->odin, (uint8_t *) malloc (PICTURES_SIZE) };
	int ret = 0;
	unsigned i;

	if (!kept.pictures)
		return -1;

	vk_renderer_run (renderer, save_kept, &kept);
	for (i = 0; i < odin::Renderer::PICTURES; i++)
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
	const uint32_t size = (uint32_t) odin::Renderer::PICTURE_SIZE;
	kept_t kept = { hr->odin, (uint8_t *) malloc (PICTURES_SIZE) };
	int ret = 0;
	unsigned i;

	if (!kept.pictures)
		return -1;

	for (i = 0; i < odin::Renderer::PICTURES && !ret; i++)
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
			delete hr->odin;
		hr->odin = NULL;
	}
}

/* The bytes of a memory of the machine */
static std::span<const uint8_t>
bytes_of (vk_buffer_t *buffer)
{
	return std::span<const uint8_t> ((const uint8_t *) vk_buffer_get_ptr (buffer, 0),
	                                 vk_buffer_get_size (buffer));
}

/* The lists of the mesh programs of a game (odin::MeshPrograms): the one
 * that comes with Odin, in shaders/ where the list of the games is, and
 * the one kept for the user, in the cache of their home. A path is left
 * empty when there is no such file or no place for it. */
static void
find_program_lists (const char *game, char *seed, char *kept, size_t size)
{
	const char *home = getenv ("HOME"), *cache = getenv ("XDG_CACHE_HOME");
	char dir[512];

	kept[0] = '\0';
	snprintf (seed, size, "shaders/%s.txt", game);
	if (access (seed, R_OK) && home)
		snprintf (seed, size, "%s/.local/share/odin/shaders/%s.txt", home, game);
	if (access (seed, R_OK))
		seed[0] = '\0';

	if (cache && cache[0])
		snprintf (dir, sizeof (dir), "%s", cache);
	else if (home)
		snprintf (dir, sizeof (dir), "%s/.cache", home);
	else
		return;
	mkdir (dir, 0700);
	strncat (dir, "/odin", sizeof (dir) - strlen (dir) - 1);
	mkdir (dir, 0755);
	strncat (dir, "/shaders", sizeof (dir) - strlen (dir) - 1);
	mkdir (dir, 0755);
	snprintf (kept, size, "%s/%s.txt", dir, game);
}

/* On the drawing thread */
static void
prepare_programs (void *ctx)
{
	hikaru_renderer_t *hr = (hikaru_renderer_t *) ctx;

	hr->odin->prepare_programs ();
}

vk_renderer_t *
hikaru_renderer_new (vk_buffer_t *fb, vk_buffer_t *texram[2], const char *game)
{
	odin::Renderer::Memory memory;
	odin::Renderer::Options options;
	hikaru_renderer_t *hr;
	char seed[640], kept[640];

	hr = ALLOC (hikaru_renderer_t);
	if (!hr)
		return NULL;

	hr->base.width = odin::Renderer::WIDTH;
	hr->base.height = odin::Renderer::HEIGHT;

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

	memory.fb		= bytes_of (fb);
	memory.texram[0]	= bytes_of (texram[0]);
	memory.texram[1]	= bytes_of (texram[1]);

	options.log		= vk_util_get_bool_option ("HR_LOG", false);
	options.log_frame	= vk_util_get_int_option ("HR_LOG_FRAME", 0);
	options.list_fp		= vk_util_get_bool_option ("ODIN_LIST_FP", false);
	options.list_dump	= vk_util_get_int_option ("ODIN_LIST_DUMP", -1);
	options.lists_test	= vk_util_get_int_option ("ODIN_LISTS_TEST", 0) != 0;
	options.debug_keys	= vk_util_get_bool_option ("ODIN_DEBUG_KEYS", false);
	options.key_down	= is_key_down;
	options.ctx		= NULL;

	find_program_lists (game, seed, kept, sizeof (seed));
	options.programs_seed	= seed;
	options.programs_kept	= kept;

	hr->odin = new odin::Renderer (memory, options);

	vk_renderer_start ((vk_renderer_t *) hr);
	vk_renderer_run ((vk_renderer_t *) hr, prepare_programs, hr);

	return (vk_renderer_t *) hr;
}

void
hikaru_renderer_set_gpu (vk_renderer_t *renderer, odin::HikaruGpu *chips)
{
	hikaru_renderer_t *hr = (hikaru_renderer_t *) renderer;

	hr->odin->set_gpu (chips);
}
