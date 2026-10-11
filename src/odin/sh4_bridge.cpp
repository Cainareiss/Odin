/*
 * Odin
 * Copyright (C) 2026 Réda Chérif-Touil
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

#include "odin/sh4_bridge.hpp"
#include "odin/sh4.hpp"

#include <cstring>
#include <type_traits>

namespace {

class CBus : public odin::Sh4Bus {
public:
	explicit CBus(const odin_sh4_bus_t &bus) : m_bus(bus) {}

	bool read(uint32_t addr, uint32_t size, uint64_t &value) override
	{
		return m_bus.read(m_bus.ctx, addr, size, &value) == 0;
	}
	bool write(uint32_t addr, uint32_t size, uint64_t value) override
	{
		return m_bus.write(m_bus.ctx, addr, size, value) == 0;
	}
	uint16_t port_a_read() override { return m_bus.port_a_read(m_bus.ctx); }
	void port_a_write(uint16_t pins) override { m_bus.port_a_write(m_bus.ctx, pins); }
	void unanswered(bool is_write, uint32_t addr, uint32_t size, uint64_t value) override
	{
		m_bus.unanswered(m_bus.ctx, is_write, addr, size, value);
	}
	void hook() override { m_bus.hook(m_bus.ctx); }

private:
	odin_sh4_bus_t m_bus;
};

/* The C state is the C++ one, field for field. */
static_assert(sizeof(odin_sh4_state_t) == sizeof(odin::Sh4State));
static_assert(std::is_trivially_copyable_v<odin::Sh4State>);
static_assert(ODIN_SH4_SOURCES == odin::Sh4Source::COUNT);
static_assert(ODIN_SH4_PAGE_SIZE == odin::Sh4::PAGE_SIZE);
static_assert(offsetof(odin_sh4_state_t, irq) == offsetof(odin::Sh4State, irq));
static_assert(offsetof(odin_sh4_state_t, pm_counter) == offsetof(odin::Sh4State, pm_counter));

} /* namespace */

struct odin_sh4 {
	CBus bus;
	odin::Sh4 cpu;

	odin_sh4(const odin_sh4_bus_t &b, uint8_t *onchip, bool master, bool little_endian,
	         uint32_t clock_mode)
		: bus(b), cpu(bus, std::span<uint8_t>(onchip, 0x10000), master, little_endian, clock_mode)
	{
	}
};

extern "C" {

odin_sh4_t *
odin_sh4_new(const odin_sh4_bus_t *bus, uint8_t *onchip, int master, int little_endian,
             unsigned clock_mode)
{
	return new odin_sh4(*bus, onchip, master != 0, little_endian != 0, clock_mode);
}

void
odin_sh4_delete(odin_sh4_t *cpu)
{
	delete cpu;
}

void
odin_sh4_reset(odin_sh4_t *cpu, int hard)
{
	cpu->cpu.reset(hard != 0);
}

void
odin_sh4_map_page(odin_sh4_t *cpu, uint32_t page, const uint8_t *read, uint8_t *write)
{
	cpu->cpu.map_page(page, read, write);
}

int
odin_sh4_run(odin_sh4_t *cpu, int cycles)
{
	return cpu->cpu.run(cycles);
}

void
odin_sh4_set_time(odin_sh4_t *cpu, uint64_t now)
{
	cpu->cpu.set_time(now);
}

void
odin_sh4_set_irq(odin_sh4_t *cpu, unsigned source, int raised)
{
	cpu->cpu.set_irq(source, raised != 0);
}

void
odin_sh4_set_nmi(odin_sh4_t *cpu, int high)
{
	cpu->cpu.set_nmi(high != 0);
}

void
odin_sh4_set_run(odin_sh4_t *cpu, unsigned state)
{
	cpu->cpu.set_run(static_cast<odin::Sh4Run>(state));
}

unsigned
odin_sh4_get_run(const odin_sh4_t *cpu)
{
	return static_cast<unsigned>(cpu->cpu.run_state());
}

void
odin_sh4_set_hook(odin_sh4_t *cpu, uint32_t addr)
{
	cpu->cpu.set_hook(addr);
}

int
odin_sh4_remaining(const odin_sh4_t *cpu)
{
	return cpu->cpu.remaining();
}

void
odin_sh4_skip(odin_sh4_t *cpu, uint32_t cycles)
{
	cpu->cpu.skip(cycles);
}

uint32_t
odin_sh4_reg(const odin_sh4_t *cpu, unsigned n)
{
	return cpu->cpu.reg(n);
}

void
odin_sh4_set_reg(odin_sh4_t *cpu, unsigned n, uint32_t value)
{
	cpu->cpu.set_reg(n, value);
}

uint32_t
odin_sh4_pc(const odin_sh4_t *cpu)
{
	return cpu->cpu.pc();
}

uint32_t
odin_sh4_pr(const odin_sh4_t *cpu)
{
	return cpu->cpu.pr();
}

uint32_t
odin_sh4_sr(const odin_sh4_t *cpu)
{
	return cpu->cpu.sr();
}

uint32_t
odin_sh4_gbr(const odin_sh4_t *cpu)
{
	return cpu->cpu.gbr();
}

void
odin_sh4_save_state(const odin_sh4_t *cpu, odin_sh4_state_t *out)
{
	odin::Sh4State state;

	cpu->cpu.save(state);
	std::memcpy(out, &state, sizeof(state));
}

void
odin_sh4_load_state(odin_sh4_t *cpu, const odin_sh4_state_t *in)
{
	odin::Sh4State state;

	std::memcpy(&state, in, sizeof(state));
	cpu->cpu.load(state);
}

uint64_t
odin_sh4_instructions(const odin_sh4_t *cpu)
{
	return cpu->cpu.instructions();
}

uint64_t
odin_sh4_skipped(const odin_sh4_t *cpu)
{
	return cpu->cpu.skipped();
}

} /* extern "C" */
