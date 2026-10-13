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

#include <memory>
#include <string>

#include "mach/hikaru/hikaru-gpu.h"
#include "mach/hikaru/hikaru-renderer.h"

/* The graphics chips of the Hikaru as a device of the machine: the chips
 * themselves are in src/odin (hikaru_gpu.hpp, antarctic.hpp, eurasia.hpp). */

typedef struct {
	vk_device_t base;

	odin::HikaruGpu *chips;
	vk_renderer_t *renderer;
} hikaru_gpu_t;

/* The bytes of a memory of the machine */
static std::span<uint8_t>
bytes_of (vk_buffer_t *buffer)
{
	return std::span<uint8_t> ((uint8_t *) vk_buffer_get_ptr (buffer, 0),
	                           vk_buffer_get_size (buffer));
}

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

static void
on_before_texture_write (void *ctx, unsigned bank)
{
	hikaru_gpu_t *gpu = (hikaru_gpu_t *) ctx;

	hikaru_renderer_before_texram_write (gpu->renderer, bank);
}

/****************************************************************************
 External events
****************************************************************************/

void
hikaru_gpu_get_frame_size (vk_device_t *dev, unsigned *dots, unsigned *lines,
                           unsigned *shown)
{
	hikaru_gpu_t *gpu = (hikaru_gpu_t *) dev;

	uint32_t d, l, s;

	gpu->chips->eurasia ().frame_size (d, l, s);
	*dots = d;
	*lines = l;
	*shown = s;
}

void
hikaru_gpu_hblank_in (vk_device_t *dev, unsigned line)
{
	hikaru_gpu_t *gpu = (hikaru_gpu_t *) dev;

	gpu->chips->end_of_line (line);
}

void
hikaru_gpu_vblank_in (vk_device_t *dev)
{
	hikaru_gpu_t *gpu = (hikaru_gpu_t *) dev;

	gpu->chips->vblank_in ();
}

void
hikaru_gpu_vblank_out (vk_device_t *dev)
{
	hikaru_gpu_t *gpu = (hikaru_gpu_t *) dev;

	gpu->chips->vblank_out ();
}

bool
hikaru_gpu_irq_line (vk_device_t *dev)
{
	hikaru_gpu_t *gpu = (hikaru_gpu_t *) dev;

	return gpu->chips->irq_line ();
}

void
hikaru_gpu_set_reset_lines (vk_device_t *dev, bool gp_res, bool pci1_res)
{
	hikaru_gpu_t *gpu = (hikaru_gpu_t *) dev;

	gpu->chips->set_reset_lines (gp_res, pci1_res);
}

bool
hikaru_gpu_is_texram_twiddled (vk_device_t *dev)
{
	hikaru_gpu_t *gpu = (hikaru_gpu_t *) dev;

	return gpu->chips->antarctic ().texture_ram_twiddled ();
}

/* The bus is about to write the texture RAM itself. */
void
hikaru_gpu_before_texram_write (vk_device_t *dev, unsigned bank)
{
	on_before_texture_write ((void *) dev, bank);
}

odin::HikaruGpu *
hikaru_gpu_get_chips (vk_device_t *dev)
{
	hikaru_gpu_t *gpu = (hikaru_gpu_t *) dev;

	return gpu->chips;
}

const char *
hikaru_gpu_get_debug_str (vk_device_t *dev)
{
	hikaru_gpu_t *gpu = (hikaru_gpu_t *) dev;
	static std::string out;

	out = gpu->chips->debug_string ();
	return out.c_str ();
}

/****************************************************************************
 Device
****************************************************************************/

static int
hikaru_gpu_exec (vk_device_t *dev, int cycles)
{
	hikaru_gpu_t *gpu = (hikaru_gpu_t *) dev;

	gpu->chips->run ((uint32_t) cycles);
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

	ret = gpu->chips->read (addr, size, value) ? 0 : -1;
	set_ptr (val, size, value);
	return ret;
}

static int
hikaru_gpu_put (vk_device_t *dev, unsigned size, uint32_t addr, uint64_t val)
{
	hikaru_gpu_t *gpu = (hikaru_gpu_t *) dev;

	VK_ASSERT (size == 4 ||
	           (size == 2 && addr == 0x00400000));

	return gpu->chips->write (addr, size, (uint32_t) val) ? 0 : -1;
}

static void
hikaru_gpu_reset (vk_device_t *dev, vk_reset_type_t type)
{
	hikaru_gpu_t *gpu = (hikaru_gpu_t *) dev;

	gpu->chips->reset ();
}

static int
hikaru_gpu_save_state (vk_device_t *dev, vk_state_t *state)
{
	hikaru_gpu_t *gpu = (hikaru_gpu_t *) dev;
	auto block = std::make_unique<odin::HikaruGpu::State> ();

	memset (block.get (), 0, sizeof (*block));
	gpu->chips->save (*block);
	return vk_state_put (state, block.get (), sizeof (*block));
}

static int
hikaru_gpu_load_state (vk_device_t *dev, vk_state_t *state)
{
	hikaru_gpu_t *gpu = (hikaru_gpu_t *) dev;
	auto block = std::make_unique<odin::HikaruGpu::State> ();
	const int ret = vk_state_get (state, block.get (), sizeof (*block));

	if (!ret)
		gpu->chips->load (*block);
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
	odin::HikaruGpu::Memory memory;
	odin::HikaruGpu::Hooks hooks;
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

	memory.cmdram		= bytes_of (cmdram);
	memory.ram_s		= bytes_of (hikaru->ram_s);
	memory.fb		= bytes_of (fb);
	memory.texram[0]	= bytes_of (texram[0]);
	memory.texram[1]	= bytes_of (texram[1]);

	hooks.ctx		= (void *) gpu;
	hooks.irq_changed	= on_irq_changed;
	hooks.list_started	= on_list_started;
	hooks.before_texture_write = on_before_texture_write;

	gpu->chips = new odin::HikaruGpu (memory, hooks);

	gpu->chips->antarctic ().set_disasm (
		vk_util_get_bool_option ("GPU_LOG_CP", false) ? stdout : NULL);
	gpu->chips->antarctic ().set_transfer_log (
		vk_util_get_bool_option ("GPU_LOG_IDMA", false) ? stdout : NULL);
	gpu->chips->eurasia ().set_log (
		vk_util_get_bool_option ("GPU_LOG_DMA", false) ? stdout : NULL);

	return dev;
}
