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

#include "odin/aica_bridge.hpp"
#include "odin/aica.hpp"
#include "odin/arm7.hpp"

#include <cstring>
#include <memory>
#include <span>

namespace {

/* One sound board: the AICA and the ARM7 that runs the sound driver.
 * Time comes in as host-CPU cycles and leaves as sample periods. */
class Board : public odin::AicaHost, public odin::Arm7Bus {
public:
	/* The ARM7DI runs on the bus clock, 22.5792 MHz (33.8688 MHz * 2 / 3):
	 * 512 cycles per sample period. MAME gives it one cycle out of eight,
	 * to stand for wait states; SGNASCAR loads the sound driver again at
	 * each change of scene and waits for it, and on its cabinet the driver
	 * answers within a frame: 328000 cycles, 20 MHz at least. HEURISTIC:
	 * no wait state. */
	static constexpr uint32_t ARM_CYCLES_PER_SAMPLE = 512;

	static constexpr uint32_t ARM_REGS = 0x800000;
	static constexpr uint32_t ARM_REGS_END = 0x808000;

	Board(std::span<uint8_t> ram, uint32_t clock_hz, odin_aica_irq_fn irq, void *ctx)
		: m_irq(irq), m_ctx(ctx), m_clock_hz(clock_hz), m_sample_acc(0),
		  m_arm_held(true), m_ram(ram.data()), m_arm(*this), m_aica(*this, ram)
	{
		map_ram();
		/* The registers read without side effects. */
		m_arm.set_turn_replay(true);
	}

	/* AicaHost */
	void arm_fiq(bool level) override { m_arm.set_fiq(level); }

	void arm_reset(bool level) override
	{
		if (level) {
			m_arm_held = true;
		} else if (m_arm_held) {
			m_arm_held = false;
			m_arm.reset();
		}
	}

	void main_irq(bool level) override
	{
		if (m_irq)
			m_irq(m_ctx, level ? 1 : 0);
	}

	/* Arm7Bus: wave memory below 0x800000, registers at 0x800000, nothing
	 * else (MAME arm0_map: unmap_value_high). */
	uint8_t read8(uint32_t addr) override { return static_cast<uint8_t>(read(addr, 1)); }
	uint16_t read16(uint32_t addr) override { return static_cast<uint16_t>(read(addr, 2)); }
	uint32_t read32(uint32_t addr) override { return read(addr, 4); }
	void write8(uint32_t addr, uint8_t value) override { write(addr, 1, value); }
	void write16(uint32_t addr, uint16_t value) override { write(addr, 2, value); }
	void write32(uint32_t addr, uint32_t value) override { write(addr, 4, value); }

	void run(uint32_t cycles)
	{
		m_sample_acc += static_cast<uint64_t>(cycles) * odin::Aica::SAMPLE_RATE;
		const uint64_t samples = m_sample_acc / m_clock_hz;
		m_sample_acc -= samples * m_clock_hz;
		for (uint64_t s = 0; s < samples; s++) {
			m_aica.tick(1);
			if (!m_arm_held) {
				map_ram();
				m_arm.run(ARM_CYCLES_PER_SAMPLE);
			}
		}
	}

	odin::Aica &chip() { return m_aica; }
	odin::Arm7 &arm() { return m_arm; }

	/* The whole board. Plain data. */
	struct State {
		uint64_t sample_acc;
		odin::Arm7State arm;
		odin::AicaState aica;
		uint8_t arm_held;
	};

	void save(State &out)
	{
		m_arm.settle();
		out.sample_acc = m_sample_acc;
		m_arm.save(out.arm);
		m_aica.save(out.aica);
		out.arm_held = m_arm_held ? 1 : 0;
	}

	/* The chip first: it drives the reset pin of the ARM7, which the
	 * saved core then replaces. */
	void load(const State &in)
	{
		m_aica.load(in.aica);
		m_sample_acc = in.sample_acc;
		m_arm_held = in.arm_held != 0;
		m_arm.load(in.arm);
	}

private:
	uint32_t read(uint32_t addr, uint32_t size)
	{
		if (addr < ARM_REGS)
			return static_cast<uint32_t>(m_aica.read_ram(addr, size));
		if (addr < ARM_REGS_END)
			return m_aica.read_reg(addr - ARM_REGS, size);
		return 0xFFFFFFFFu;
	}

	void write(uint32_t addr, uint32_t size, uint32_t value)
	{
		if (addr < ARM_REGS) {
			m_aica.write_ram(addr, size, value);
		} else if (addr < ARM_REGS_END) {
			m_aica.write_reg(addr - ARM_REGS, size, value);
			map_ram();
		}
	}

	/* The wave memory, as the ARM7 reads it without the bus: a register
	 * sets how much of it is decoded. */
	void map_ram() { m_arm.set_ram({m_ram, ARM_REGS, m_aica.ram_mask()}); }

	odin_aica_irq_fn m_irq;
	void *m_ctx;
	uint32_t m_clock_hz;
	uint64_t m_sample_acc;
	bool m_arm_held;
	uint8_t *m_ram;
	odin::Arm7 m_arm;
	odin::Aica m_aica;
};

enum : uint32_t {
	PAGE_REGS = 0x700000,
	PAGE_REGS_END = 0x708000,
	PAGE_RTC = 0x710000,
	PAGE_RTC_END = 0x710010,
	PAGE_RAM = 0x800000
};

} /* namespace */

struct odin_aica {
	std::unique_ptr<Board> board;
};

extern "C" {

odin_aica_t *
odin_aica_new(uint8_t *ram, size_t ram_size, uint32_t host_clock_hz,
              odin_aica_irq_fn main_irq, void *ctx)
{
	if (!ram || ram_size != odin::Aica::RAM_SIZE || host_clock_hz == 0)
		return nullptr;
	auto dev = std::make_unique<odin_aica>();
	dev->board = std::make_unique<Board>(std::span<uint8_t>(ram, ram_size),
	                                     host_clock_hz, main_irq, ctx);
	return dev.release();
}

void
odin_aica_delete(odin_aica_t *dev)
{
	std::unique_ptr<odin_aica> owned(dev);
}

int
odin_aica_main_irq(const odin_aica_t *dev)
{
	return dev->board->chip().main_irq() ? 1 : 0;
}

void
odin_aica_reset(odin_aica_t *dev)
{
	dev->board->arm().settle();
	dev->board->chip().reset();
}

int32_t
odin_aica_rtc_ahead(const odin_aica_t *dev)
{
	return dev->board->chip().rtc_ahead();
}

void
odin_aica_set_rtc_ahead(odin_aica_t *dev, int32_t seconds)
{
	dev->board->chip().set_rtc_ahead(seconds);
}

void
odin_aica_reset_pin(odin_aica_t *dev)
{
	dev->board->arm().settle();
	dev->board->chip().reset(false);
}

int
odin_aica_host_read(odin_aica_t *dev, uint32_t offset, unsigned size, uint64_t *value)
{
	odin::Aica &chip = dev->board->chip();
	if (offset >= PAGE_RAM) {
		dev->board->arm().ram_read(offset - PAGE_RAM, size);
		*value = chip.read_ram(offset - PAGE_RAM, size);
	} else if (offset >= PAGE_REGS && offset < PAGE_REGS_END) {
		*value = chip.read_reg(offset - PAGE_REGS, size > 4 ? 4 : size);
	} else if (offset >= PAGE_RTC && offset < PAGE_RTC_END) {
		*value = chip.read_rtc(offset - PAGE_RTC);
	} else {
		return -1;
	}
	return 0;
}

int
odin_aica_host_write(odin_aica_t *dev, uint32_t offset, unsigned size, uint64_t value)
{
	odin::Aica &chip = dev->board->chip();
	if (offset >= PAGE_RAM) {
		dev->board->arm().ram_write(offset - PAGE_RAM, size);
		chip.write_ram(offset - PAGE_RAM, size, value);
	} else if (offset >= PAGE_REGS && offset < PAGE_REGS_END) {
		/* A register may move the memory, the lines or the ARM7. */
		dev->board->arm().settle();
		chip.write_reg(offset - PAGE_REGS, size > 4 ? 4 : size, static_cast<uint32_t>(value));
	} else if (offset >= PAGE_RTC && offset < PAGE_RTC_END) {
		chip.write_rtc(offset - PAGE_RTC, static_cast<uint32_t>(value));
		return chip.take_rtc_set() ? 1 : 0;
	} else {
		return -1;
	}
	return 0;
}

void
odin_aica_run(odin_aica_t *dev, uint32_t cycles)
{
	dev->board->run(cycles);
}

void
odin_aica_settle(odin_aica_t *dev)
{
	dev->board->arm().settle();
}

void
odin_aica_set_log(odin_aica_t *dev, FILE *out)
{
	dev->board->chip().set_log(out);
}

void
odin_aica_set_arm_trace(odin_aica_t *dev, FILE *out)
{
	dev->board->arm().set_trace(out);
}

size_t
odin_aica_state_size(void)
{
	return sizeof(Board::State);
}

void
odin_aica_save_state(const odin_aica_t *dev, void *out)
{
	auto state = std::make_unique<Board::State>();
	std::memset(state.get(), 0, sizeof(Board::State));
	dev->board->save(*state);
	std::memcpy(out, state.get(), sizeof(Board::State));
}

void
odin_aica_load_state(odin_aica_t *dev, const void *in)
{
	auto state = std::make_unique<Board::State>();
	std::memcpy(state.get(), in, sizeof(Board::State));
	dev->board->load(*state);
}

} /* extern "C" */
