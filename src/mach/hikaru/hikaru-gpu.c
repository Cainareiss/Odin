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

#include "mach/hikaru/hikaru-gpu.h"
#include "mach/hikaru/hikaru-renderer.h"

/* The graphics chips of the Hikaru as a device of the machine: the chips
 * themselves are in src/odin (hikaru_gpu.hpp, antarctic.hpp, eurasia.hpp). */

typedef struct {
	vk_device_t base;

	odin_hikaru_gpu_t *chips;
	vk_renderer_t *renderer;
} hikaru_gpu_t;

/****************************************************************************
 What the chips tell the board
****************************************************************************/

static void
on_irq_changed (void *ctx)
{
	hikaru_gpu_t *gpu = (hikaru_gpu_t *) ctx;

	hikaru_update_irqs (gpu->base.mach);
}

static void
on_list_started (void *ctx)
{
	hikaru_gpu_t *gpu = (hikaru_gpu_t *) ctx;

	hikaru_renderer_launch (gpu->renderer);
}

/* The decoded textures of that bank are stale. */
static void
on_texture_written (void *ctx, unsigned bank)
{
	hikaru_gpu_t *gpu = (hikaru_gpu_t *) ctx;

	hikaru_renderer_invalidate_texcache (gpu->renderer, (int) bank);
}

/****************************************************************************
 External events
****************************************************************************/

void
hikaru_gpu_get_frame_size (vk_device_t *dev, unsigned *dots, unsigned *lines,
                           unsigned *shown)
{
	hikaru_gpu_t *gpu = (hikaru_gpu_t *) dev;

	odin_hikaru_gpu_frame_size (gpu->chips, dots, lines, shown);
}

void
hikaru_gpu_hblank_in (vk_device_t *dev, unsigned line)
{
	hikaru_gpu_t *gpu = (hikaru_gpu_t *) dev;

	odin_hikaru_gpu_end_of_line (gpu->chips, line);
}

void
hikaru_gpu_vblank_in (vk_device_t *dev)
{
	hikaru_gpu_t *gpu = (hikaru_gpu_t *) dev;

	odin_hikaru_gpu_vblank_in (gpu->chips);
}

void
hikaru_gpu_vblank_out (vk_device_t *dev)
{
	hikaru_gpu_t *gpu = (hikaru_gpu_t *) dev;

	odin_hikaru_gpu_vblank_out (gpu->chips);
}

bool
hikaru_gpu_irq_line (vk_device_t *dev)
{
	hikaru_gpu_t *gpu = (hikaru_gpu_t *) dev;

	return odin_hikaru_gpu_irq_line (gpu->chips) != 0;
}

void
hikaru_gpu_set_reset_lines (vk_device_t *dev, bool gp_res, bool pci1_res)
{
	hikaru_gpu_t *gpu = (hikaru_gpu_t *) dev;

	odin_hikaru_gpu_set_reset_lines (gpu->chips, gp_res, pci1_res);
}

bool
hikaru_gpu_is_texram_twiddled (vk_device_t *dev)
{
	hikaru_gpu_t *gpu = (hikaru_gpu_t *) dev;

	return odin_hikaru_gpu_texture_ram_twiddled (gpu->chips) != 0;
}

/* The bus wrote the texture RAM itself. */
void
hikaru_gpu_texram_written (vk_device_t *dev, unsigned bank)
{
	on_texture_written ((void *) dev, bank);
}

odin_hikaru_gpu_t *
hikaru_gpu_get_chips (vk_device_t *dev)
{
	hikaru_gpu_t *gpu = (hikaru_gpu_t *) dev;

	return gpu->chips;
}

const char *
hikaru_gpu_get_debug_str (vk_device_t *dev)
{
	hikaru_gpu_t *gpu = (hikaru_gpu_t *) dev;

	return odin_hikaru_gpu_debug_str (gpu->chips);
}

/****************************************************************************
 Device
****************************************************************************/

static int
hikaru_gpu_exec (vk_device_t *dev, int cycles)
{
	hikaru_gpu_t *gpu = (hikaru_gpu_t *) dev;

	odin_hikaru_gpu_run (gpu->chips, (uint32_t) cycles);
	return 0;
}

static int
hikaru_gpu_get (vk_device_t *dev, unsigned size, uint32_t addr, void *val)
{
	hikaru_gpu_t *gpu = (hikaru_gpu_t *) dev;
	uint32_t value = 0;
	int ret;

	VK_ASSERT (size == 4 ||
	           (size == 2 && (addr == 0x15000010 || addr == 0x00400000)));

	ret = odin_hikaru_gpu_read (gpu->chips, addr, size, &value);
	set_ptr (val, size, value);
	return ret;
}

static int
hikaru_gpu_put (vk_device_t *dev, unsigned size, uint32_t addr, uint64_t val)
{
	hikaru_gpu_t *gpu = (hikaru_gpu_t *) dev;

	VK_ASSERT (size == 4 ||
	           (size == 2 && addr == 0x00400000));

	return odin_hikaru_gpu_write (gpu->chips, addr, size, (uint32_t) val);
}

static void
hikaru_gpu_reset (vk_device_t *dev, vk_reset_type_t type)
{
	hikaru_gpu_t *gpu = (hikaru_gpu_t *) dev;

	odin_hikaru_gpu_reset (gpu->chips);
}

static int
hikaru_gpu_save_state (vk_device_t *dev, vk_state_t *state)
{
	hikaru_gpu_t *gpu = (hikaru_gpu_t *) dev;
	const size_t size = odin_hikaru_gpu_state_size ();
	void *block = malloc (size);
	int ret;

	if (!block)
		return -1;
	odin_hikaru_gpu_save_state (gpu->chips, block);
	ret = vk_state_put (state, block, (uint32_t) size);
	free (block);

	return ret;
}

static int
hikaru_gpu_load_state (vk_device_t *dev, vk_state_t *state)
{
	hikaru_gpu_t *gpu = (hikaru_gpu_t *) dev;
	const size_t size = odin_hikaru_gpu_state_size ();
	void *block = malloc (size);
	int ret;

	if (!block)
		return -1;
	ret = vk_state_get (state, block, (uint32_t) size);
	if (!ret)
		odin_hikaru_gpu_load_state (gpu->chips, block);
	free (block);

	return ret;
}

vk_device_t *
hikaru_gpu_new (vk_machine_t *mach,
                vk_buffer_t *cmdram,
                vk_buffer_t *fb,
                vk_buffer_t *texram[2],
                vk_renderer_t *renderer)
{
	hikaru_t *hikaru = (hikaru_t *) mach;
	odin_hikaru_gpu_memory_t memory;
	odin_hikaru_gpu_hooks_t hooks;
	hikaru_gpu_t *gpu;
	vk_device_t *dev;

	VK_DEVICE_ALLOC (gpu, mach);
	dev = (vk_device_t *) gpu;
	if (!gpu)
		return NULL;

	dev->destroy	= NULL;
	dev->reset	= hikaru_gpu_reset;
	dev->exec	= hikaru_gpu_exec;
	dev->get	= hikaru_gpu_get;
	dev->put	= hikaru_gpu_put;
	dev->save_state	= hikaru_gpu_save_state;
	dev->load_state	= hikaru_gpu_load_state;

	gpu->renderer	= renderer;

	memory.cmdram		= (uint8_t *) vk_buffer_get_ptr (cmdram, 0);
	memory.cmdram_size	= vk_buffer_get_size (cmdram);
	memory.ram_s		= (uint8_t *) vk_buffer_get_ptr (hikaru->ram_s, 0);
	memory.ram_s_size	= vk_buffer_get_size (hikaru->ram_s);
	memory.fb		= (uint8_t *) vk_buffer_get_ptr (fb, 0);
	memory.fb_size		= vk_buffer_get_size (fb);
	memory.texram[0]	= (uint8_t *) vk_buffer_get_ptr (texram[0], 0);
	memory.texram[1]	= (uint8_t *) vk_buffer_get_ptr (texram[1], 0);
	memory.texram_size	= vk_buffer_get_size (texram[0]);

	hooks.ctx		= (void *) gpu;
	hooks.irq_changed	= on_irq_changed;
	hooks.list_started	= on_list_started;
	hooks.texture_written	= on_texture_written;

	gpu->chips = odin_hikaru_gpu_new (&memory, &hooks);
	if (!gpu->chips)
		return NULL;

	odin_hikaru_gpu_set_traces (gpu->chips,
		vk_util_get_bool_option ("GPU_LOG_CP", false) ? stdout : NULL,
		vk_util_get_bool_option ("GPU_LOG_IDMA", false) ? stdout : NULL,
		vk_util_get_bool_option ("GPU_LOG_DMA", false) ? stdout : NULL);

	return dev;
}
