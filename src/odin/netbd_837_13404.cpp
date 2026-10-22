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

#include "odin/netbd_837_13404.hpp"

#include <algorithm>

namespace odin {

/* The 68000 map, by address bits 19-18: 00000 RAM, 40000 control, 80000
 * communication RAM, C0000 the registers of the 315-5917. */
enum : uint32_t { MAP_RAM, MAP_CTRL, MAP_COMM, MAP_IO };

/* The registers the program uses (C0000 + odd byte) */
enum : uint32_t {
	IO_RX_STATUS = 0x11,	/* read when a frame is in */
	IO_RX_CTRL = 0x15,	/* 80 then 17 = 8C: receive; other: stop, IRQ6 off */
	IO_RX_GO = 0x17,
	IO_TX_STATUS = 0x19,
	IO_TX_CTRL = 0x1B,	/* 80 then 1D = 8C: send; 0: IRQ4 off */
	IO_TX_GO = 0x1D,
	IO_RX_ADDR = 0x41,	/* two byte writes each, the low byte first */
	IO_RX_SIZE = 0x43,
	IO_TX_ADDR = 0x45,
	IO_TX_SIZE = 0x47,
	IO_IRQ = 0x83,		/* read, then written 0, by each handler */
	IO_STATUS0 = 0x88,	/* the two words the host reads */
	IO_STATUS1 = 0x8A
};

NetBoard837_13404::NetBoard837_13404()
	: m_cpu(*this), m_ram(RAM_SIZE), m_comm{std::vector<uint8_t>(COMM_SIZE), std::vector<uint8_t>(COMM_SIZE)},
	  m_bank(0), m_io{}, m_rx_addr(0), m_rx_size(0), m_tx_addr(0), m_tx_size(0), m_status0(0), m_status1(0),
	  m_rx_armed(false), m_tx_busy(false), m_tx_end(0), m_timer(0), m_timer_zero(0), m_timer_next(0), m_leds(0), m_irq(0),
	  m_host_regs{}, m_running(false), m_clock(0), m_cpu_base(0), m_next(nullptr)
{
}

/* Power-on: the 68000 held in reset until the host lets it run. */
void
NetBoard837_13404::reset()
{
	std::fill(m_ram.begin(), m_ram.end(), 0);
	for (auto &bank : m_comm)
		std::fill(bank.begin(), bank.end(), 0);
	m_bank = 0;
	m_io.fill(0);
	m_rx_addr = m_rx_size = m_tx_addr = m_tx_size = 0;
	m_status0 = m_status1 = 0;
	m_rx_armed = m_tx_busy = false;
	m_timer = 0;
	m_timer_zero = 0;
	m_timer_next = UINT64_MAX;
	m_leds = 0;
	m_irq = 0;
	m_host_regs.fill(0);
	m_running = false;
	m_arrivals.clear();
	m_proof.on = false;
	m_quiet = false;
	update_ipl();
}

void
NetBoard837_13404::update_ipl()
{
	unsigned level = 0;
	for (unsigned l = 7; l > 0; l--) {
		if (m_irq & (1u << l)) {
			level = l;
			break;
		}
	}
	m_cpu.set_ipl(level);
}

/* HEURISTIC: a 15-bit counter turns all the time; IRQ5 comes each time it
 * reaches the value of 40000+C0, and a write of 40000+E0 sets it back to 0
 * with an IRQ5 at once: the IRQ5 the slave asks for right after each frame
 * it syncs on (note 98 section 10). C0 = 7FFF or 7C18 puts the IRQ5 late in
 * the turn, once per turn. */
void
NetBoard837_13404::schedule_timer()
{
	const uint64_t turn = 0x8000 * TICK;
	const uint64_t at = (m_timer & 0x7FFFu) * TICK;
	const uint64_t t = now();
	const uint64_t since = (t - m_timer_zero) % turn;
	uint64_t next = t - since + at;
	if (next <= t)
		next += turn;
	m_timer_next = next;
	event_at(next);
}

uint64_t
NetBoard837_13404::next_event() const
{
	uint64_t t = m_timer_next;
	if (m_tx_busy)
		t = std::min(t, m_tx_end);
	if (!m_arrivals.empty())
		t = std::min(t, m_arrivals.front().at);
	return t;
}

uint64_t
NetBoard837_13404::fingerprint() const
{
	uint64_t h = 0xCBF29CE484222325ull;
	auto mix = [&h](const void *p, size_t n) {
		const uint8_t *b = static_cast<const uint8_t *>(p);
		for (size_t i = 0; i < n; i++) {
			h ^= b[i];
			h *= 0x100000001B3ull;
		}
	};
	M68000State cpu;

	m_cpu.save(cpu);
	mix(m_ram.data(), m_ram.size());
	mix(m_comm[0].data(), COMM_SIZE);
	mix(m_comm[1].data(), COMM_SIZE);
	mix(cpu.d.data(), sizeof(cpu.d));
	mix(cpu.a.data(), sizeof(cpu.a));
	const uint32_t regs[] = {cpu.other_sp, cpu.pc, cpu.sr, cpu.ir, cpu.irc, cpu.ipl, cpu.nmi_edge, cpu.stopped,
	                         cpu.halted};
	mix(regs, sizeof(regs));
	mix(m_io.data(), m_io.size());
	const uint64_t words[] = {m_bank, m_rx_addr, m_rx_size, m_tx_addr, m_tx_size, m_status0, m_status1,
	                          m_rx_armed, m_tx_busy, m_tx_end, m_timer, m_timer_zero, m_timer_next, m_irq, m_clock,
	                          m_cpu.cycles()};
	mix(words, sizeof(words));
	return h;
}

/* The bytes and words the turn read, as they are now (no side effect) */
uint16_t
NetBoard837_13404::peek(uint32_t addr, unsigned size) const
{
	const uint32_t off = addr & 0xFFFF;
	const std::vector<uint8_t> &mem = ((addr >> 18) & 3) == MAP_RAM ? m_ram : m_comm[m_bank];

	if (size == 1)
		return mem[off];
	return static_cast<uint16_t>((mem[off] << 8) | mem[(off + 1) & 0xFFFF]);
}

bool
NetBoard837_13404::reads_same() const
{
	for (unsigned i = 0; i < m_proof.reads; i++)
		if (peek(m_proof.read[i].addr, m_proof.read[i].size) != m_proof.read[i].value)
			return false;
	return true;
}

void
NetBoard837_13404::note_read(uint32_t addr, unsigned size, uint16_t value)
{
	if (m_recorded < MAX_READS)
		m_record[m_recorded] = {addr, static_cast<uint8_t>(size), value};
	m_recorded++;
}

/* One turn run with its reads noted, as long as it ends before `stop`
 * (where the run would stop anyway): proven when it read only RAM, wrote
 * nothing, came back to its first instruction with the CPU the same. */
bool
NetBoard837_13404::wait_prove(uint64_t stop)
{
	M68000State start, now;
	uint64_t spent = 0;
	bool proven = false;

	m_cpu.save(start);
	m_recording = true;
	m_recorded = 0;
	m_touched = false;
	for (unsigned i = 0; i < MAX_TURN && m_clock + spent < stop; i++) {
		spent += m_cpu.step();
		if (m_touched || m_recorded > MAX_READS)
			break;
		if (m_cpu.pc() == start.pc) {
			m_cpu.save(now);
			proven = now == start;
			break;
		}
	}
	m_recording = false;
	m_clock += spent;
	m_proof.on = proven;
	if (proven) {
		m_proof.cpu = start;
		m_proof.length = spent;
		m_proof.reads = m_recorded;
		m_proof.read = m_record;
	}
	return proven;
}

/* The turns of a proven wait up to `stop`, billed without running; the
 * last one is run, so the run ends on the instruction it would have. */
void
NetBoard837_13404::wait_count(uint64_t stop)
{
	M68000State now;

	if (m_cpu.interrupt_pending())
		return;
	m_cpu.save(now);
	if (!m_proof.on || !(now == m_proof.cpu) || !reads_same()) {
		if (!wait_prove(stop) || m_clock >= stop)
			return;
		m_cpu.save(now);
		if (!(now == m_proof.cpu))
			return;
	}
	const uint64_t turns = (stop - m_clock) / m_proof.length;
	if (turns > 1) {
		const uint64_t clocks = (turns - 1) * m_proof.length;
		m_cpu.skip(clocks);
		m_clock += clocks;
	}
}

void
NetBoard837_13404::run_until(uint64_t clock)
{
	while (m_clock < clock) {
		const uint64_t stop = std::min(clock, next_event());
		if (stop > m_clock) {
			if (m_running) {
				if (m_waits_counted && m_quiet)
					wait_count(stop);
				m_touched = false;
				if (stop > m_clock)
					m_clock += m_cpu.run(static_cast<uint32_t>(std::min<uint64_t>(stop - m_clock, 1u << 30)));
				m_quiet = !m_touched;
			} else {
				m_clock = stop;
			}
		}
		while (!m_arrivals.empty() && m_arrivals.front().at <= m_clock) {
			const NetFrame frame = std::move(m_arrivals.front().frame);
			m_arrivals.pop_front();
			deliver(frame);
		}
		if (m_timer_next <= m_clock) {
			raise(IRQ5);
			schedule_timer();
		}
		if (m_tx_busy && m_tx_end <= m_clock) {
			m_tx_busy = false;
			raise(IRQ4);
		}
	}
}

void
NetBoard837_13404::receive(const NetFrame &frame, uint64_t latency)
{
	m_arrivals.push_back({frame, frame.end + latency});
	event_at(frame.end + latency);
}

/* A frame in while receiving: into the bank of the 68000 at the receive
 * address, as much as the receive size allows. HEURISTIC: a frame that
 * comes while not receiving is lost (no repeater). */
void
NetBoard837_13404::deliver(const NetFrame &frame)
{
	if (!m_rx_armed)
		return;
	const size_t n = std::min<size_t>(frame.data.size(), m_rx_size);
	for (size_t i = 0; i < n; i++)
		comm(m_bank, static_cast<uint32_t>(m_rx_addr + i)) = frame.data[i];
	m_rx_armed = false;
	raise(IRQ6);
}

/* A frame out: the bytes at the send address as they are now (HEURISTIC:
 * the whole frame is read at the start), IRQ4 once the last has left. */
void
NetBoard837_13404::start_transmit()
{
	NetFrame frame;
	frame.data.resize(m_tx_size);
	for (uint32_t i = 0; i < m_tx_size; i++)
		frame.data[i] = comm(m_bank, m_tx_addr + i);
	m_tx_end = now() + (m_tx_size + FRAME_OVERHEAD) * BYTE_CLOCKS;
	frame.end = m_tx_end;
	m_tx_busy = true;
	event_at(m_tx_end);
	if (m_next)
		m_next->send(frame);
}

uint8_t
NetBoard837_13404::io_read(uint32_t reg)
{
	switch (reg) {
	case IO_RX_STATUS:
	case IO_TX_STATUS:
		return 0x05;	/* HEURISTIC: "done", as MAME and Supermodel answer */
	case IO_IRQ:
		return 0;
	case IO_STATUS0:
		return static_cast<uint8_t>(m_status0 >> 8);
	case IO_STATUS0 + 1:
		return static_cast<uint8_t>(m_status0);
	case IO_STATUS1:
		return static_cast<uint8_t>(m_status1 >> 8);
	case IO_STATUS1 + 1:
		return static_cast<uint8_t>(m_status1);
	default:
		return m_io[reg & 0xFF];
	}
}

void
NetBoard837_13404::io_write(uint32_t reg, uint8_t value)
{
	m_io[reg & 0xFF] = value;
	switch (reg) {
	case IO_RX_ADDR:
		m_rx_addr = static_cast<uint16_t>((m_rx_addr >> 8) | (value << 8));
		break;
	case IO_RX_SIZE:
		m_rx_size = static_cast<uint16_t>((m_rx_size >> 8) | (value << 8));
		break;
	case IO_TX_ADDR:
		m_tx_addr = static_cast<uint16_t>((m_tx_addr >> 8) | (value << 8));
		break;
	case IO_TX_SIZE:
		m_tx_size = static_cast<uint16_t>((m_tx_size >> 8) | (value << 8));
		break;
	case IO_RX_CTRL:
		if (value != 0x80) {
			m_rx_armed = false;
			lower(IRQ6);
		}
		break;
	case IO_RX_GO:
		if (value == 0x8C && m_io[IO_RX_CTRL] == 0x80) {
			m_rx_armed = true;
		} else if (value == 0) {
			m_rx_armed = false;
			lower(IRQ6);
		}
		break;
	case IO_TX_GO:
		if (value == 0x8C && m_io[IO_TX_CTRL] == 0x80)
			start_transmit();
		else if (value == 0)
			lower(IRQ4);
		break;
	case IO_STATUS0:
		m_status0 = static_cast<uint16_t>((m_status0 & 0x00FF) | (value << 8));
		break;
	case IO_STATUS0 + 1:
		m_status0 = static_cast<uint16_t>((m_status0 & 0xFF00) | value);
		break;
	case IO_STATUS1:
		m_status1 = static_cast<uint16_t>((m_status1 & 0x00FF) | (value << 8));
		break;
	case IO_STATUS1 + 1:
		m_status1 = static_cast<uint16_t>((m_status1 & 0xFF00) | value);
		break;
	default:
		break;
	}
}

void
NetBoard837_13404::ctrl_write(uint32_t reg, uint16_t value)
{
	switch (reg) {
	case 0x00:	/* the bank of the communication RAM at 80000 */
		m_bank = value & 1;
		break;
	case 0x40:
		lower(IRQ5);
		break;
	case 0x80:
		m_leds = value;
		break;
	case 0xA0:
		lower(IRQ2);
		break;
	case 0xC0:
		m_timer = value;
		schedule_timer();
		break;
	case 0xE0:
		m_timer_zero = now();
		raise(IRQ5);
		schedule_timer();
		break;
	default:
		break;
	}
}

/* M68000Bus */

uint16_t
NetBoard837_13404::read16(uint32_t addr, unsigned)
{
	const uint32_t off = addr & 0xFFFF;
	uint16_t v;

	switch ((addr >> 18) & 3) {
	case MAP_RAM:
		v = static_cast<uint16_t>((m_ram[off] << 8) | m_ram[off + 1]);
		break;
	case MAP_CTRL:
		m_touched = true;
		return (addr & 0xFF) == 0 ? static_cast<uint16_t>(m_bank) : 0;
	case MAP_COMM:
		v = static_cast<uint16_t>((comm(m_bank, off) << 8) | comm(m_bank, off + 1));
		break;
	default:
		m_touched = true;
		return static_cast<uint16_t>((io_read(addr & 0xFE) << 8) | io_read((addr & 0xFE) + 1));
	}
	if (m_recording)
		note_read(addr, 2, v);
	return v;
}

uint8_t
NetBoard837_13404::read8(uint32_t addr, unsigned)
{
	const uint32_t off = addr & 0xFFFF;
	uint8_t v;

	switch ((addr >> 18) & 3) {
	case MAP_RAM:
		v = m_ram[off];
		break;
	case MAP_CTRL:
		m_touched = true;
		return (addr & 0xFF) == 1 ? static_cast<uint8_t>(m_bank) : 0;
	case MAP_COMM:
		v = comm(m_bank, off);
		break;
	default:
		m_touched = true;
		return io_read(addr & 0xFF);
	}
	if (m_recording)
		note_read(addr, 1, v);
	return v;
}

void
NetBoard837_13404::write16(uint32_t addr, uint16_t value, unsigned)
{
	const uint32_t off = addr & 0xFFFF;
	m_touched = true;
	switch ((addr >> 18) & 3) {
	case MAP_RAM:
		m_ram[off] = static_cast<uint8_t>(value >> 8);
		m_ram[off + 1] = static_cast<uint8_t>(value);
		break;
	case MAP_CTRL:
		ctrl_write(addr & 0xFF, value);
		break;
	case MAP_COMM:
		comm(m_bank, off) = static_cast<uint8_t>(value >> 8);
		comm(m_bank, off + 1) = static_cast<uint8_t>(value);
		break;
	default:
		io_write(addr & 0xFE, static_cast<uint8_t>(value >> 8));
		io_write((addr & 0xFE) + 1, static_cast<uint8_t>(value));
		break;
	}
}

void
NetBoard837_13404::write8(uint32_t addr, uint8_t value, unsigned)
{
	const uint32_t off = addr & 0xFFFF;
	m_touched = true;
	switch ((addr >> 18) & 3) {
	case MAP_RAM:
		m_ram[off] = value;
		break;
	case MAP_CTRL:
		ctrl_write(addr & 0xFF, value);
		break;
	case MAP_COMM:
		comm(m_bank, off) = value;
		break;
	default:
		io_write(addr & 0xFF, value);
		break;
	}
}

/* The host window. In the communication RAM a host byte is the 68000 byte
 * at the same offset XOR 3: a 32-bit word of the SH-4 is a long of the
 * 68000 (DEDUCED from the frame counter at 80006 that the host reads as
 * the word at +4, von4 0C1B8CF4). */

uint32_t
NetBoard837_13404::host_read(uint32_t offset, unsigned size)
{
	offset &= 0x3FFFF;
	if (offset < 0x10000) {
		uint32_t v = 0;
		for (unsigned i = 0; i < size; i++)
			v |= static_cast<uint32_t>(comm(m_bank ^ 1, (offset + i) ^ 3)) << (8 * i);
		return v;
	}
	if (offset < 0x20000) {
		const uint32_t reg = ((offset - 0x10000) >> 1) & 0xFE;
		if (reg == IO_STATUS0)
			return m_status0;
		if (reg == IO_STATUS1)
			return m_status1;
		return m_host_regs[(reg >> 1) & 0x7F];
	}
	if (offset & 2)
		return 0;
	const uint32_t a = ((offset - 0x20000) >> 1) & 0xFFFE;
	return static_cast<uint32_t>((m_ram[a] << 8) | m_ram[a + 1]);
}

void
NetBoard837_13404::host_write(uint32_t offset, unsigned size, uint32_t value)
{
	offset &= 0x3FFFF;
	if (offset < 0x10000) {
		for (unsigned i = 0; i < size; i++)
			comm(m_bank ^ 1, (offset + i) ^ 3) = static_cast<uint8_t>(value >> (8 * i));
		return;
	}
	if (offset < 0x20000) {
		const uint32_t reg = ((offset - 0x10000) >> 1) & 0xFE;
		const uint16_t v = static_cast<uint16_t>(value);
		if (reg == IO_STATUS0) {
			m_status0 = v;
			raise(IRQ2);	/* HEURISTIC (MAME): the host writes the mailbox */
			return;
		}
		if (reg == IO_STATUS1) {
			m_status1 = v;
			return;
		}
		m_host_regs[(reg >> 1) & 0x7F] = v;
		if (reg == 0xC0) {	/* the 68000 runs while nonzero */
			const bool run = v != 0;
			m_proof.on = false;
			if (run && !m_running) {
				m_irq = 0;
				m_cpu.reset();
				m_cpu_base = m_cpu.cycles() - m_clock;
				update_ipl();
				m_timer = 0;
				m_timer_zero = m_clock;
				raise(IRQ5);
				schedule_timer();
			}
			m_running = run;
		}
		return;
	}
	if (offset & 2)
		return;
	const uint32_t a = ((offset - 0x20000) >> 1) & 0xFFFE;
	m_ram[a] = static_cast<uint8_t>(value >> 8);
	m_ram[a + 1] = static_cast<uint8_t>(value);
}

} /* namespace odin */
