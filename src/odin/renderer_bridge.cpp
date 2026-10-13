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

#include "odin/renderer_bridge.hpp"
#include "odin/renderer.hpp"

#include <memory>
#include <span>

struct odin_renderer {
	odin_renderer(const odin::Renderer::Memory &memory,
	              const odin::Renderer::Options &options)
		: renderer(memory, options) {}

	odin::Renderer renderer;
};

static_assert(ODIN_RENDERER_PICTURES == odin::Renderer::PICTURES);

extern "C" {

void
odin_renderer_screen_size(unsigned *width, unsigned *height)
{
	*width = odin::Renderer::WIDTH;
	*height = odin::Renderer::HEIGHT;
}

odin_renderer_t *
odin_renderer_new(const odin_renderer_memory_t *memory,
                  const odin_renderer_options_t *options)
{
	odin::Renderer::Memory m;
	odin::Renderer::Options o;

	m.fb = std::span<const uint8_t>(memory->fb, memory->fb_size);
	m.texram[0] = std::span<const uint8_t>(memory->texram[0], memory->texram_size);
	m.texram[1] = std::span<const uint8_t>(memory->texram[1], memory->texram_size);

	o.log = options->log;
	o.log_frame = options->log_frame;
	o.list_fp = options->list_fp;
	o.list_dump = options->list_dump;
	o.lists_test = options->lists_test;
	o.debug_keys = options->debug_keys;
	o.key_down = options->key_down;
	o.ctx = options->ctx;
	if (options->programs_seed)
		o.programs_seed = options->programs_seed;
	if (options->programs_kept)
		o.programs_kept = options->programs_kept;

	auto renderer = std::make_unique<odin_renderer>(m, o);
	return renderer.release();
}

void
odin_renderer_delete(odin_renderer_t *renderer)
{
	std::unique_ptr<odin_renderer> owned(renderer);
}

void
odin_renderer_set_gpu(odin_renderer_t *renderer, odin_hikaru_gpu_t *gpu)
{
	renderer->renderer.set_gpu(&odin_hikaru_gpu_chips(gpu));
}

void
odin_renderer_prepare_programs(odin_renderer_t *renderer)
{
	renderer->renderer.prepare_programs();
}

void
odin_renderer_set_scale(odin_renderer_t *renderer, unsigned scale)
{
	renderer->renderer.set_scale(scale);
}

void
odin_renderer_set_anisotropy(odin_renderer_t *renderer, unsigned level)
{
	renderer->renderer.set_anisotropy(level);
}

void
odin_renderer_launch(odin_renderer_t *renderer)
{
	renderer->renderer.launch();
}

void
odin_renderer_invalidate_textures(odin_renderer_t *renderer, int bank)
{
	renderer->renderer.invalidate_textures(bank);
}

void
odin_renderer_begin_frame(odin_renderer_t *renderer)
{
	renderer->renderer.begin_frame();
}

void
odin_renderer_take_frame(odin_renderer_t *renderer, unsigned frame,
                         unsigned window_w, unsigned window_h)
{
	renderer->renderer.take_frame(frame, window_w, window_h);
}

void
odin_renderer_draw_frame(odin_renderer_t *renderer)
{
	renderer->renderer.draw_frame();
}

bool
odin_renderer_frame_is_logged(const odin_renderer_t *renderer)
{
	return renderer->renderer.frame_is_logged();
}

unsigned
odin_renderer_frame_picture(const odin_renderer_t *renderer, unsigned *width, unsigned *height)
{
	return renderer->renderer.frame_picture(*width, *height);
}

const char *
odin_renderer_message(const odin_renderer_t *renderer)
{
	return renderer->renderer.message();
}

size_t
odin_renderer_picture_size(void)
{
	return odin::Renderer::PICTURE_SIZE;
}

void
odin_renderer_save_state(odin_renderer_t *renderer, uint8_t *pictures,
                         odin_renderer_counters_t *counters)
{
	const size_t size = odin::Renderer::PICTURES * odin::Renderer::PICTURE_SIZE;
	odin::Renderer::Counters c;

	renderer->renderer.save(std::span<uint8_t>(pictures, size), c);
	counters->num_lists = c.num_lists;
	counters->shown = c.shown;
	counters->next = c.next;
	counters->launched = c.launched;
}

void
odin_renderer_load_state(odin_renderer_t *renderer, const uint8_t *pictures,
                         const odin_renderer_counters_t *counters)
{
	const size_t size = odin::Renderer::PICTURES * odin::Renderer::PICTURE_SIZE;
	const odin::Renderer::Counters c = {
		counters->num_lists, counters->shown, counters->next, counters->launched
	};

	renderer->renderer.load(std::span<const uint8_t>(pictures, size), c);
}

} /* extern "C" */
