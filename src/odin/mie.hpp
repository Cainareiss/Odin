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

#ifndef ODIN_MIE_HPP
#define ODIN_MIE_HPP

#include "odin/jvs_host.hpp"
#include "odin/z80.hpp"

#include <array>
#include <cstdint>
#include <cstdio>
#include <span>

namespace odin {

/* Sega 315-6146 "MIE": a Z80 with 2 KB of mask ROM, 2 KB + 32 KB of RAM,
 * eight GPIO ports, two 16550-style UARTs (the first drives the JVS
 * RS-485 link), a Maple slave block and a bus-request input through
 * which the host CPU reads and writes the Z80 memory and I/O spaces.
 *
 * Memory map and I/O ports: MAME src/mame/sega/mie.cpp (mie_map,
 * mie_port) and the Z80 code the Hikaru BOOTROM uploads. Host windows:
 * MAME src/mame/sega/hikaru.cpp localbus_map. */

/* Board wiring around the chip: what the GPIO pins see. Ports are
 * numbered A..H = 0..7. */
class MieHost {
public:
	virtual ~MieHost() = default;
	/* Level on the pins of a port configured as input. */
	virtual uint8_t gpio_read(uint8_t port) = 0;
	/* New level driven on a port configured as output. */
	virtual void gpio_write(uint8_t port, uint8_t value) = 0;
	/* Level read back on a port configured as output: the latch,
	 * unless the board pulls a pin elsewhere. */
	virtual uint8_t gpio_readback(uint8_t, uint8_t latched) { return latched; }
};

struct MieUartState {
	uint8_t dll, dlm, ier, fcr, lcr, mcr, scr;
	std::array<uint8_t, 16> tx_fifo;
	uint8_t tx_count;
	uint32_t tx_cycles;
	uint8_t tx_byte;
	std::array<uint8_t, 512> rx_fifo;
	uint32_t rx_head, rx_count;
	std::array<uint8_t, 512> wire;
	uint32_t wire_pos, wire_count, wire_cycles;
};

struct MieState {
	Z80State cpu;
	std::array<uint8_t, 0x800> ram_low;
	std::array<uint8_t, 0x40> maple;
	std::array<uint8_t, 8> gpio_val;
	uint8_t gpio_dir;
	uint8_t irq_enable, irq_pending, maple_irqlevel;
	uint8_t jvs_control;
	uint8_t busrq;
	MieUartState uart;
};

class Mie : public Z80Bus {
public:
	/* MAME hikaru.cpp: MIE(config, m_mie, 16000000, ...). */
	static constexpr uint32_t CLOCK_HZ = 16000000;
	static constexpr uint32_t ROM_SIZE = 0x800;
	/* Z80 0x8000-0xFFFF, the half the host sees through its window. */
	static constexpr uint32_t RAM_SIZE = 0x8000;

	/* `rom` is the 2 KB 315-6146 mask ROM; `ram` the 32 KB shared half,
	 * owned by the caller. */
	Mie(MieHost &host, std::span<const uint8_t> rom, std::span<uint8_t> ram);

	void reset();

	/* Z80 side. */
	uint8_t read8(uint16_t addr) override;
	void write8(uint16_t addr, uint8_t value) override;
	uint8_t in8(uint16_t port) override;
	void out8(uint16_t port, uint8_t value) override;

	/* Host side: the same two spaces, one byte at a time
	 * (hikaru.cpp localbus_map 0x00800000 and 0x00820000). */
	uint8_t host_read_port(uint8_t port);
	void host_write_port(uint8_t port, uint8_t value);
	uint8_t host_read_mem(uint16_t addr);
	void host_write_mem(uint16_t addr, uint8_t value);

	/* BUSRQ pin. The bus is granted only while the Z80 program allows
	 * it with bit 6 of port 0x30 (MAME mie.h busak_r). While granted,
	 * the CPU is stopped. */
	void set_busrq(bool level);
	bool busak() const { return m_busrq && (m_irq_enable & 0x40); }
	/* busak() with a trace line. */
	bool host_busak_read();

	/* Runs the chip for `cycles` Z80 clocks. */
	void run(uint32_t cycles);

	JvsHost &jvs() { return m_jvs; }
	const Z80 &cpu() const { return m_cpu; }

	void save(MieState &out) const;
	void load(const MieState &in);

	/* Unknown accesses are logged once each to `out`. */
	void set_log(FILE *out) { m_log = out; }
	/* Z80 bus trace for `max_instructions` instructions once the PC
	 * reaches `start_pc`; host accesses are traced as "host" for as long
	 * as `out` is set. */
	void set_trace(FILE *out, uint32_t max_instructions, uint16_t start_pc);

private:
	enum : uint16_t {
		ROM_END = 0x0800,
		OPEN_END = 0x7000,
		MAPLE_BASE = 0x7000,
		MAPLE_END = 0x7800,
		RAM_LOW_BASE = 0x7800,
		RAM_BASE = 0x8000
	};

	uint8_t gpio_read(uint8_t port);
	void gpio_write(uint8_t port, uint8_t value);
	void gpio_dir_write(uint8_t value);
	uint8_t uart_read(uint8_t reg);
	void uart_write(uint8_t reg, uint8_t value);
	void uart_reset();
	void uart_tick(uint32_t cycles);
	uint32_t uart_byte_cycles() const;
	void uart_commit_frame();
	uint8_t jvs_sense() const;
	void tick(uint32_t cycles);
	void log_unknown(char rw, const char *space, uint32_t addr, uint32_t value);
	void trace_host(char rw, uint32_t addr, uint8_t value) const;

	MieHost &m_host;
	Z80 m_cpu;
	JvsHost m_jvs;
	std::span<const uint8_t> m_rom;
	std::span<uint8_t> m_ram;
	std::array<uint8_t, 0x800> m_ram_low;
	std::array<uint8_t, 0x40> m_maple;
	std::array<uint8_t, 8> m_gpio_val;
	uint8_t m_gpio_dir;
	uint8_t m_irq_enable;
	uint8_t m_irq_pending;
	uint8_t m_maple_irqlevel;
	uint8_t m_jvs_control;
	bool m_busrq;
	MieUartState m_uart;

	FILE *m_log;
	FILE *m_trace;
	uint32_t m_trace_left;
	uint16_t m_trace_start;
	bool m_trace_armed;
	bool m_busak_traced;
	std::array<bool, 512> m_port_logged;
	std::array<bool, 4> m_mem_logged;
};

} /* namespace odin */

#endif /* ODIN_MIE_HPP */
