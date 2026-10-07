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

#include "odin/mie.hpp"

#include <format>

namespace odin {

namespace {

/* I/O ports (MAME mie.cpp mie_port; UART registers from the uploaded
 * program, listing 0x8677 and 0x87CB-0x8806). */
enum : uint8_t {
	PORT_GPIO_A = 0x00,
	PORT_GPIO_H = 0x07,
	PORT_GPIO_DIR = 0x08,
	PORT_ADC = 0x0F,
	PORT_UART1 = 0x10,
	PORT_UART1_END = 0x17,
	PORT_UART2 = 0x20,
	PORT_UART2_END = 0x27,
	PORT_IRQ_ENABLE = 0x30,
	PORT_MAPLE_IRQLEVEL = 0x50,
	PORT_IRQ_PENDING = 0x70,
	PORT_JVS_CONTROL = 0x90,
	PORT_JVS_SENSE = 0x91
};

/* 16550 registers, offsets from the UART base. */
enum : uint8_t {
	UART_RBR_THR_DLL = 0,
	UART_IER_DLM = 1,
	UART_IIR_FCR = 2,
	UART_LCR = 3,
	UART_MCR = 4,
	UART_LSR = 5,
	UART_MSR = 6,
	UART_SCR = 7
};

enum : uint8_t {
	LCR_DLAB = 0x80,
	FCR_ENABLE = 0x01,
	FCR_CLEAR_RX = 0x02,
	FCR_CLEAR_TX = 0x04,
	LSR_DR = 0x01,
	LSR_THRE = 0x20,
	LSR_TEMT = 0x40,
	IIR_NONE = 0x01,
	IIR_FIFO = 0xC0
};

/* Bits on the wire per character: start, 8 data, stop. */
constexpr uint32_t UART_BITS_PER_BYTE = 10;
/* Reference clock per bit, as in a 16550: 16 x the baud rate. The UART
 * clock is taken as the Z80 clock, which is not measured; with the
 * divisor 8 the program sets this gives 125 kbaud, close to the 115200
 * of JVS. */
constexpr uint32_t UART_CLOCKS_PER_BIT = 16;
/* Time the I/O board takes to start answering, in characters. JVS
 * boards answer within a few character times; not measured. */
constexpr uint32_t JVS_TURNAROUND_BYTES = 2;

} /* namespace */

Mie::Mie(MieHost &host, std::span<const uint8_t> rom, std::span<uint8_t> ram)
	: m_host(host), m_cpu(*this), m_rom(rom), m_ram(ram), m_log(nullptr), m_trace(nullptr),
	  m_trace_left(0), m_trace_start(0), m_trace_armed(false), m_busak_traced(false)
{
	m_port_logged.fill(false);
	m_mem_logged.fill(false);
	m_ram_low.fill(0);
	reset();
}

/* Reset values: MAME mie.cpp device_reset. */
void Mie::reset()
{
	m_maple.fill(0);
	m_gpio_val.fill(0);
	m_gpio_dir = 0xFF;
	m_irq_enable = 0;
	m_irq_pending = 0;
	m_maple_irqlevel = 0xFF;
	m_jvs_control = 0;
	m_busrq = false;
	uart_reset();
	m_jvs.reset();
	m_cpu.reset();
}

void Mie::set_trace(FILE *out, uint32_t max_instructions, uint16_t start_pc)
{
	m_trace = out;
	m_trace_left = out ? max_instructions : 0;
	m_trace_start = start_pc;
	m_trace_armed = m_trace_left != 0;
	m_cpu.set_trace(nullptr);
}

/* Host side of the two spaces. Port reads and writes are the Z80's
 * own, so the GPIO and UART see them the same way. */

uint8_t Mie::host_read_port(uint8_t port)
{
	const uint8_t v = in8(port);
	if (m_trace) {
		trace_host('i', port, v);
	}
	return v;
}

void Mie::host_write_port(uint8_t port, uint8_t value)
{
	if (m_trace) {
		trace_host('o', port, value);
	}
	out8(port, value);
}

uint8_t Mie::host_read_mem(uint16_t addr)
{
	const uint8_t v = read8(addr);
	if (m_trace) {
		trace_host('r', addr, v);
	}
	return v;
}

void Mie::host_write_mem(uint16_t addr, uint8_t value)
{
	if (m_trace) {
		trace_host('w', addr, value);
	}
	write8(addr, value);
}

/* Bus request pin; traced as port 0x100, 'q' request and 'k' grant. */

void Mie::set_busrq(bool level)
{
	if (m_trace) {
		trace_host('q', 0x100, level ? 1 : 0);
	}
	m_busrq = level;
}

bool Mie::host_busak_read()
{
	const bool ack = busak();
	/* The host polls this; one line per change keeps the trace small. */
	if (m_trace && ack != m_busak_traced) {
		m_busak_traced = ack;
		trace_host('k', 0x100, ack ? 1 : 0);
	}
	return ack;
}

void Mie::trace_host(char rw, uint32_t addr, uint8_t value) const
{
	std::fputs(std::format("host 0000 {} {:04x} 1 {:02x}\n", rw, addr, value).c_str(), m_trace);
}

/* Z80 memory space (MAME mie.cpp mie_map). */

uint8_t Mie::read8(uint16_t addr)
{
	if (addr >= RAM_BASE) {
		return m_ram[addr - RAM_BASE];
	}
	if (addr >= RAM_LOW_BASE) {
		return m_ram_low[addr - RAM_LOW_BASE];
	}
	if (addr >= MAPLE_BASE) {
		/* Control, length and transfer buffer, mirrored every 0x40. */
		const uint8_t off = addr & 0x3F;
		return off < 0x24 ? m_maple[off] : 0x00;
	}
	if (addr >= ROM_END) {
		return 0xFF;
	}
	return m_rom[addr];
}

void Mie::write8(uint16_t addr, uint8_t value)
{
	if (addr >= RAM_BASE) {
		m_ram[addr - RAM_BASE] = value;
	} else if (addr >= RAM_LOW_BASE) {
		m_ram_low[addr - RAM_LOW_BASE] = value;
	} else if (addr >= MAPLE_BASE) {
		/* No Maple host on Hikaru: the block is plain storage. */
		const uint8_t off = addr & 0x3F;
		if (off < 0x24) {
			m_maple[off] = value;
		}
	} else if (addr >= ROM_END) {
		if (!m_mem_logged[0]) {
			m_mem_logged[0] = true;
			log_unknown('w', "mem", addr, value);
		}
	} else {
		if (!m_mem_logged[1]) {
			m_mem_logged[1] = true;
			log_unknown('w', "rom", addr, value);
		}
	}
}

/* Z80 I/O space (MAME mie.cpp mie_port). */

uint8_t Mie::in8(uint16_t port)
{
	const uint8_t p = static_cast<uint8_t>(port);
	if (p <= PORT_GPIO_H) {
		return gpio_read(p);
	}
	if (p >= PORT_UART1 && p <= PORT_UART1_END) {
		return uart_read(static_cast<uint8_t>(p - PORT_UART1));
	}
	switch (p) {
	case PORT_GPIO_DIR:
		return m_gpio_dir;
	case PORT_ADC:
		return 0;
	case PORT_IRQ_ENABLE:
		return m_irq_enable;
	case PORT_MAPLE_IRQLEVEL:
		return m_maple_irqlevel;
	case PORT_IRQ_PENDING:
		return m_irq_pending;
	case PORT_JVS_SENSE:
		return jvs_sense();
	default:
		break;
	}
	if (p >= PORT_UART2 && p <= PORT_UART2_END) {
		/* Second UART, nothing wired: transmitter idle, no data. */
		return (p - PORT_UART2) == UART_LSR ? (LSR_THRE | LSR_TEMT) : 0;
	}
	if (!m_port_logged[p]) {
		m_port_logged[p] = true;
		log_unknown('i', "port", p, 0xFF);
	}
	return 0xFF;
}

void Mie::out8(uint16_t port, uint8_t value)
{
	const uint8_t p = static_cast<uint8_t>(port);
	if (p <= PORT_GPIO_H) {
		gpio_write(p, value);
		return;
	}
	if (p >= PORT_UART1 && p <= PORT_UART1_END) {
		uart_write(static_cast<uint8_t>(p - PORT_UART1), value);
		return;
	}
	switch (p) {
	case PORT_GPIO_DIR:
		gpio_dir_write(value);
		return;
	case PORT_ADC:
		return;
	case PORT_IRQ_ENABLE:
		/* Bit 6 also gates BUSAK. No interrupt source is wired on
		 * Hikaru, so the mask has no other effect. */
		m_irq_enable = value;
		return;
	case PORT_MAPLE_IRQLEVEL:
		m_maple_irqlevel = value;
		return;
	case PORT_IRQ_PENDING:
		m_irq_pending = value;
		return;
	case PORT_JVS_CONTROL:
		/* Bit 0 enables the RS-485 driver; the frame is complete when
		 * it drops (MAME mie.cpp jvs_control_w). */
		if ((m_jvs_control & 1) && !(value & 1)) {
			uart_commit_frame();
		}
		m_jvs_control = value;
		return;
	default:
		break;
	}
	if (p >= PORT_UART2 && p <= PORT_UART2_END) {
		return;
	}
	if (!m_port_logged[256 + p]) {
		m_port_logged[256 + p] = true;
		log_unknown('o', "port", p, value);
	}
}

/* GPIO: direction bit set = input (MAME mie.cpp gpio_r). */

uint8_t Mie::gpio_read(uint8_t port)
{
	if (m_gpio_dir & (1u << port)) {
		return m_host.gpio_read(port);
	}
	return m_host.gpio_readback(port, m_gpio_val[port]);
}

void Mie::gpio_write(uint8_t port, uint8_t value)
{
	m_gpio_val[port] = value;
	if (!(m_gpio_dir & (1u << port))) {
		m_host.gpio_write(port, value);
	}
}

void Mie::gpio_dir_write(uint8_t value)
{
	/* Ports that just became outputs drive their latched value. */
	const uint8_t to_output = static_cast<uint8_t>((value ^ m_gpio_dir) & ~value);
	m_gpio_dir = value;
	for (uint8_t i = 0; i < 8; i++) {
		if (to_output & (1u << i)) {
			m_host.gpio_write(i, m_gpio_val[i]);
		}
	}
}

/* JVS sense input (MAME mie.cpp jvs_sense_r): bit 0 set when no board
 * is wired, bit 1 set when every board has its address. */
uint8_t Mie::jvs_sense() const
{
	return static_cast<uint8_t>(0x8C | (m_jvs.address_set_line() ? 2 : 0) | (m_jvs.presence_line() ? 0 : 1));
}

/* UART 1, the JVS link. Characters take their time on the wire; the
 * reply of the I/O board comes back the same way. */

void Mie::uart_reset()
{
	m_uart.dll = 0;
	m_uart.dlm = 0;
	m_uart.ier = 0;
	m_uart.fcr = 0;
	m_uart.lcr = 0;
	m_uart.mcr = 0;
	m_uart.scr = 0;
	m_uart.tx_fifo.fill(0);
	m_uart.tx_count = 0;
	m_uart.tx_cycles = 0;
	m_uart.tx_byte = 0;
	m_uart.rx_fifo.fill(0);
	m_uart.rx_head = 0;
	m_uart.rx_count = 0;
	m_uart.wire.fill(0);
	m_uart.wire_pos = 0;
	m_uart.wire_count = 0;
	m_uart.wire_cycles = 0;
}

uint32_t Mie::uart_byte_cycles() const
{
	uint32_t divisor = static_cast<uint32_t>((m_uart.dlm << 8) | m_uart.dll);
	if (divisor == 0) {
		divisor = 1;
	}
	return UART_BITS_PER_BYTE * UART_CLOCKS_PER_BIT * divisor;
}

uint8_t Mie::uart_read(uint8_t reg)
{
	MieUartState &u = m_uart;
	switch (reg) {
	case UART_RBR_THR_DLL:
		if (u.lcr & LCR_DLAB) {
			return u.dll;
		}
		if (u.rx_count) {
			const uint8_t v = u.rx_fifo[u.rx_head];
			u.rx_head = (u.rx_head + 1) % static_cast<uint32_t>(u.rx_fifo.size());
			u.rx_count--;
			return v;
		}
		return 0;
	case UART_IER_DLM:
		return (u.lcr & LCR_DLAB) ? u.dlm : u.ier;
	case UART_IIR_FCR:
		return static_cast<uint8_t>(IIR_NONE | ((u.fcr & FCR_ENABLE) ? IIR_FIFO : 0));
	case UART_LCR:
		return u.lcr;
	case UART_MCR:
		return u.mcr;
	case UART_LSR: {
		uint8_t v = 0;
		if (u.rx_count) {
			v |= LSR_DR;
		}
		if (u.tx_count == 0) {
			v |= LSR_THRE;
			if (u.tx_cycles == 0) {
				v |= LSR_TEMT;
			}
		}
		return v;
	}
	case UART_MSR:
		return 0;
	case UART_SCR:
	default:
		return u.scr;
	}
}

void Mie::uart_write(uint8_t reg, uint8_t value)
{
	MieUartState &u = m_uart;
	switch (reg) {
	case UART_RBR_THR_DLL:
		if (u.lcr & LCR_DLAB) {
			u.dll = value;
		} else if (u.tx_count < u.tx_fifo.size()) {
			u.tx_fifo[u.tx_count++] = value;
		}
		break;
	case UART_IER_DLM:
		if (u.lcr & LCR_DLAB) {
			u.dlm = value;
		} else {
			u.ier = value;
		}
		break;
	case UART_IIR_FCR:
		u.fcr = value;
		if (value & FCR_CLEAR_RX) {
			u.rx_head = 0;
			u.rx_count = 0;
		}
		if (value & FCR_CLEAR_TX) {
			u.tx_count = 0;
		}
		break;
	case UART_LCR:
		u.lcr = value;
		break;
	case UART_MCR:
		u.mcr = value;
		break;
	case UART_LSR:
	case UART_MSR:
		break;
	case UART_SCR:
	default:
		u.scr = value;
		break;
	}
}

/* The driver was switched off: the frame is on the bus, the board
 * answers after its turnaround time. */
void Mie::uart_commit_frame()
{
	MieUartState &u = m_uart;
	m_jvs.commit_encoded();
	const std::span<const uint8_t> reply = m_jvs.reply();
	u.wire_count = static_cast<uint32_t>(reply.size());
	u.wire_pos = 0;
	for (uint32_t i = 0; i < u.wire_count; i++) {
		u.wire[i] = reply[i];
	}
	u.wire_cycles = u.wire_count ? uart_byte_cycles() * JVS_TURNAROUND_BYTES : 0;
}

void Mie::uart_tick(uint32_t cycles)
{
	MieUartState &u = m_uart;
	const uint32_t byte_cycles = uart_byte_cycles();

	/* Transmitter: shift register then FIFO. */
	uint32_t left = cycles;
	while (left) {
		if (u.tx_cycles) {
			const uint32_t step = u.tx_cycles < left ? u.tx_cycles : left;
			u.tx_cycles -= step;
			left -= step;
			if (u.tx_cycles == 0) {
				m_jvs.push(u.tx_byte);
			}
		} else if (u.tx_count) {
			u.tx_byte = u.tx_fifo[0];
			for (uint8_t i = 1; i < u.tx_count; i++) {
				u.tx_fifo[i - 1] = u.tx_fifo[i];
			}
			u.tx_count--;
			u.tx_cycles = byte_cycles;
		} else {
			break;
		}
	}

	/* Receiver: bytes land one character time apart. */
	left = cycles;
	while (left && u.wire_pos < u.wire_count) {
		const uint32_t step = u.wire_cycles < left ? u.wire_cycles : left;
		u.wire_cycles -= step;
		left -= step;
		if (u.wire_cycles == 0) {
			if (u.rx_count < u.rx_fifo.size()) {
				const uint32_t tail = (u.rx_head + u.rx_count) % static_cast<uint32_t>(u.rx_fifo.size());
				u.rx_fifo[tail] = u.wire[u.wire_pos];
				u.rx_count++;
			}
			u.wire_pos++;
			u.wire_cycles = byte_cycles;
		}
	}
}

void Mie::tick(uint32_t cycles)
{
	uart_tick(cycles);
}

void Mie::run(uint32_t cycles)
{
	uint32_t done = 0;
	while (done < cycles) {
		if (busak()) {
			/* Bus released to the host: the CPU waits, the rest of
			 * the chip keeps going. */
			tick(cycles - done);
			break;
		}
		if (m_trace_armed && m_cpu.pc() == m_trace_start) {
			m_trace_armed = false;
			m_cpu.set_trace(m_trace);
		}
		const uint32_t c = m_cpu.step();
		if (m_trace_left && !m_trace_armed && --m_trace_left == 0) {
			m_cpu.set_trace(nullptr);
		}
		tick(c);
		done += c;
	}
}

void Mie::log_unknown(char rw, const char *space, uint32_t addr, uint32_t value)
{
	if (!m_log) {
		return;
	}
	std::fputs(std::format("mie: unknown {} {} {:04x} pc {:04x} value {:02x}\n",
	                       space, rw, addr, m_cpu.pc(), value).c_str(), m_log);
}

void Mie::save(MieState &out) const
{
	m_cpu.save(out.cpu);
	out.ram_low = m_ram_low;
	out.maple = m_maple;
	out.gpio_val = m_gpio_val;
	out.gpio_dir = m_gpio_dir;
	out.irq_enable = m_irq_enable;
	out.irq_pending = m_irq_pending;
	out.maple_irqlevel = m_maple_irqlevel;
	out.jvs_control = m_jvs_control;
	out.busrq = m_busrq ? 1 : 0;
	out.uart = m_uart;
}

void Mie::load(const MieState &in)
{
	m_cpu.load(in.cpu);
	m_ram_low = in.ram_low;
	m_maple = in.maple;
	m_gpio_val = in.gpio_val;
	m_gpio_dir = in.gpio_dir;
	m_irq_enable = in.irq_enable;
	m_irq_pending = in.irq_pending;
	m_maple_irqlevel = in.maple_irqlevel;
	m_jvs_control = in.jvs_control;
	m_busrq = in.busrq != 0;
	m_uart = in.uart;
}

} /* namespace odin */
