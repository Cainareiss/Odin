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

#include "odin/antarctic_bridge.hpp"
#include "odin/antarctic.hpp"

#include <cstring>
#include <memory>
#include <span>

struct odin_antarctic {
	odin_antarctic(std::span<uint8_t> cmdram, std::span<uint8_t> ram_s)
		: chip(cmdram, ram_s) {}

	odin::Antarctic chip;
};

extern "C" {

odin_antarctic_t *
odin_antarctic_new(uint8_t *cmdram, size_t cmdram_size,
                   uint8_t *ram_s, size_t ram_s_size)
{
	auto cp = std::make_unique<odin_antarctic>(
		std::span<uint8_t>(cmdram, cmdram_size),
		std::span<uint8_t>(ram_s, ram_s_size));
	return cp.release();
}

void
odin_antarctic_delete(odin_antarctic_t *cp)
{
	std::unique_ptr<odin_antarctic> owned(cp);
}

void
odin_antarctic_reset(odin_antarctic_t *cp)
{
	cp->chip.reset();
}

void
odin_antarctic_start(odin_antarctic_t *cp, uint32_t pc, uint32_t sp0, uint32_t sp1)
{
	cp->chip.start(pc, sp0, sp1);
}

int
odin_antarctic_running(const odin_antarctic_t *cp)
{
	return cp->chip.running() ? 1 : 0;
}

int
odin_antarctic_run(odin_antarctic_t *cp, uint32_t budget)
{
	return cp->chip.run(budget) ? 1 : 0;
}

void
odin_antarctic_frame_start(odin_antarctic_t *cp)
{
	cp->chip.frame_start();
}

void
odin_antarctic_end_open_mesh(odin_antarctic_t *cp)
{
	cp->chip.end_open_mesh();
}

uint32_t
odin_antarctic_pc(const odin_antarctic_t *cp)
{
	return cp->chip.pc();
}

odin_display_list_t *
odin_antarctic_begin_list(odin_antarctic_t *cp, int keep_last)
{
	return cp->chip.begin_list(keep_last != 0);
}

const odin_fb_config_t *
odin_antarctic_fb_config(const odin_antarctic_t *cp)
{
	return &cp->chip.fb_config();
}

void
odin_antarctic_set_disasm(odin_antarctic_t *cp, FILE *out)
{
	cp->chip.set_disasm(out);
}

size_t
odin_antarctic_state_size(void)
{
	return sizeof(odin::AntarcticState);
}

const void *
odin_antarctic_state(const odin_antarctic_t *cp)
{
	return &cp->chip.state();
}

void
odin_antarctic_load_state(odin_antarctic_t *cp, const void *in)
{
	auto state = std::make_unique<odin::AntarcticState>();

	std::memcpy(state.get(), in, sizeof(odin::AntarcticState));
	cp->chip.load(*state);
}

} /* extern "C" */
