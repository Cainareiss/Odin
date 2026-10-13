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

#include "odin/hikaru_gpu_bridge.hpp"
#include "odin/hikaru_gpu.hpp"

#include <cstring>
#include <memory>
#include <span>

struct odin_hikaru_gpu {
	odin_hikaru_gpu(const odin::HikaruGpu::Memory &memory,
	                const odin::HikaruGpu::Hooks &hooks)
		: chips(memory, hooks) {}

	odin::HikaruGpu chips;
};

odin::HikaruGpu &
odin_hikaru_gpu_chips(odin_hikaru_gpu_t *gpu)
{
	return gpu->chips;
}

namespace {

/* The layout of a saved state */
struct SavedState {
	odin::AntarcticState antarctic;
	odin::EurasiaState eurasia;
	uint32_t unknown_00400000;
};

} /* namespace */

extern "C" {

odin_hikaru_gpu_t *
odin_hikaru_gpu_new(const odin_hikaru_gpu_memory_t *memory,
                    const odin_hikaru_gpu_hooks_t *hooks)
{
	odin::HikaruGpu::Memory m;
	odin::HikaruGpu::Hooks h;

	m.cmdram = std::span<uint8_t>(memory->cmdram, memory->cmdram_size);
	m.ram_s = std::span<uint8_t>(memory->ram_s, memory->ram_s_size);
	m.fb = std::span<uint8_t>(memory->fb, memory->fb_size);
	m.texram[0] = std::span<uint8_t>(memory->texram[0], memory->texram_size);
	m.texram[1] = std::span<uint8_t>(memory->texram[1], memory->texram_size);

	h.ctx = hooks->ctx;
	h.irq_changed = hooks->irq_changed;
	h.list_started = hooks->list_started;
	h.before_texture_write = hooks->before_texture_write;

	auto gpu = std::make_unique<odin_hikaru_gpu>(m, h);
	return gpu.release();
}

void
odin_hikaru_gpu_delete(odin_hikaru_gpu_t *gpu)
{
	std::unique_ptr<odin_hikaru_gpu> owned(gpu);
}

void
odin_hikaru_gpu_reset(odin_hikaru_gpu_t *gpu)
{
	gpu->chips.reset();
}

void
odin_hikaru_gpu_set_reset_lines(odin_hikaru_gpu_t *gpu, int gp_res, int pci1_res)
{
	gpu->chips.set_reset_lines(gp_res != 0, pci1_res != 0);
}

int
odin_hikaru_gpu_read(const odin_hikaru_gpu_t *gpu, uint32_t addr, unsigned size, uint32_t *value)
{
	return gpu->chips.read(addr, size, *value) ? 0 : -1;
}

int
odin_hikaru_gpu_write(odin_hikaru_gpu_t *gpu, uint32_t addr, unsigned size, uint32_t value)
{
	return gpu->chips.write(addr, size, value) ? 0 : -1;
}

void
odin_hikaru_gpu_run(odin_hikaru_gpu_t *gpu, uint32_t cycles)
{
	gpu->chips.run(cycles);
}

void
odin_hikaru_gpu_end_of_line(odin_hikaru_gpu_t *gpu, uint32_t line)
{
	gpu->chips.end_of_line(line);
}

void
odin_hikaru_gpu_vblank_in(odin_hikaru_gpu_t *gpu)
{
	gpu->chips.vblank_in();
}

void
odin_hikaru_gpu_vblank_out(odin_hikaru_gpu_t *gpu)
{
	gpu->chips.vblank_out();
}

int
odin_hikaru_gpu_irq_line(const odin_hikaru_gpu_t *gpu)
{
	return gpu->chips.irq_line() ? 1 : 0;
}

void
odin_hikaru_gpu_frame_size(const odin_hikaru_gpu_t *gpu, unsigned *dots,
                           unsigned *lines, unsigned *shown)
{
	uint32_t d, l, s;

	gpu->chips.eurasia().frame_size(d, l, s);
	*dots = d;
	*lines = l;
	*shown = s;
}

int
odin_hikaru_gpu_texture_ram_twiddled(const odin_hikaru_gpu_t *gpu)
{
	return gpu->chips.antarctic().texture_ram_twiddled() ? 1 : 0;
}

void
odin_hikaru_gpu_set_traces(odin_hikaru_gpu_t *gpu, FILE *lists, FILE *transfers, FILE *blits)
{
	gpu->chips.antarctic().set_disasm(lists);
	gpu->chips.antarctic().set_transfer_log(transfers);
	gpu->chips.eurasia().set_log(blits);
}

const char *
odin_hikaru_gpu_debug_str(const odin_hikaru_gpu_t *gpu)
{
	static char out[256];
	const odin::Antarctic &cp = gpu->chips.antarctic();
	uint32_t control = 0, mask = 0, status = 0, busy = 0, causes = 0;

	/* 15000058 and 15000084 are not readable on the bus */
	control = cp.state().regs[0x58 / 4];
	mask = cp.state().regs[0x84 / 4];
	cp.read_register(0x15000088, status);
	gpu->chips.eurasia().read(0x1A000024, busy);
	gpu->chips.eurasia().read(0x1A000018, causes);

	std::snprintf(out, sizeof(out), "@%08X %u 15:58=%u 1A:24=%u 15:84=%X 15:88=%X 1A:18=%X",
	              cp.pc(), cp.running() ? 1u : 0u, control, busy, mask, status, causes);
	return out;
}

size_t
odin_hikaru_gpu_state_size(void)
{
	return sizeof(SavedState);
}

void
odin_hikaru_gpu_save_state(const odin_hikaru_gpu_t *gpu, void *out)
{
	auto state = std::make_unique<SavedState>();

	std::memset(state.get(), 0, sizeof(SavedState));
	state->antarctic = gpu->chips.antarctic().state();
	state->eurasia = gpu->chips.eurasia().state();
	state->unknown_00400000 = gpu->chips.unknown_00400000();
	std::memcpy(out, state.get(), sizeof(SavedState));
}

void
odin_hikaru_gpu_load_state(odin_hikaru_gpu_t *gpu, const void *in)
{
	auto state = std::make_unique<SavedState>();

	std::memcpy(state.get(), in, sizeof(SavedState));
	gpu->chips.antarctic().load(state->antarctic);
	gpu->chips.eurasia().load(state->eurasia);
	gpu->chips.set_unknown_00400000(static_cast<uint16_t>(state->unknown_00400000));
}

} /* extern "C" */
