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

#ifndef ODIN_NETBD_837_13404_HPP
#define ODIN_NETBD_837_13404_HPP

#include "odin/m68000.hpp"

#include <array>
#include <cstdint>
#include <deque>
#include <vector>

namespace odin {

/* What travels on the fibre: the bytes of one transmission, and the clock
 * of the sending board at which the last of them has left it. */
struct NetFrame {
	std::vector<uint8_t> data;
	uint64_t end;
};

/* Where a board sends its frames: the next board of the ring. */
class NetLink {
public:
	virtual ~NetLink() = default;
	virtual void send(const NetFrame &frame) = 0;
};

/* Sega 837-13404, the communication board of Hikaru: a 68000 with 64 KB
 * of RAM, two banks of 64 KB of communication RAM shared with the host,
 * and two 315-5917 that send and receive frames on the optical ring.
 *
 * The 68000 runs the program the game uploads ("m3_comm.asm ver. 1.3",
 * the same in von4, sgnascar and podrace). Its map and registers are those
 * that program uses (notes 98 sections 4, 6, 10); what the 315-5917 do is
 * deduced from what the program needs, each choice marked HEURISTIC. */
class NetBoard837_13404 : public M68000Bus {
public:
	/* HEURISTIC: 40 MHz / 4; the 68000 is a 12 MHz part. */
	static constexpr uint32_t CLOCK_HZ = 10'000'000;

	NetBoard837_13404();

	void reset();

	/* The host window, bus page 0E: communication RAM 00000-0FFFF (the
	 * bank the 68000 is not using), registers 10000-101FF (one 16-bit
	 * register per 32-bit word), 68000 RAM 20000-3FFFF (one 16-bit word
	 * per 32-bit word). */
	uint32_t host_read(uint32_t offset, unsigned size);
	void host_write(uint32_t offset, unsigned size, uint32_t value);

	/* Runs the board to `clock` (its own clocks). Frames from the ring
	 * must have been given with receive() before. */
	void run_until(uint64_t clock);
	uint64_t clocks() const { return m_clock; }

	void set_link(NetLink *next) { m_next = next; }
	/* A frame from the previous board, `latency` clocks after it left. */
	void receive(const NetFrame &frame, uint64_t latency);

	bool running() const { return m_running; }
	/* For tests: a byte of the communication RAM, and the bank of the 68000 */
	uint8_t comm_byte(unsigned bank, uint32_t offset) const { return m_comm[bank & 1][offset & (COMM_SIZE - 1)]; }
	unsigned bank() const { return m_bank; }
	uint16_t leds() const { return m_leds; }
	const M68000 &cpu() const { return m_cpu; }

	/* M68000Bus */
	uint16_t read16(uint32_t addr, unsigned fc) override;
	uint8_t read8(uint32_t addr, unsigned fc) override;
	void write16(uint32_t addr, uint16_t value, unsigned fc) override;
	void write8(uint32_t addr, uint8_t value, unsigned fc) override;

private:
	static constexpr uint32_t RAM_SIZE = 0x10000;
	static constexpr uint32_t COMM_SIZE = 0x10000;
	/* HEURISTIC: one tick of the 15-bit counter of IRQ5, 0.5 us, so that
	 * a turn of it (8000 ticks, 16.4 ms) is about a frame. */
	static constexpr uint64_t TICK = 5;
	/* HEURISTIC: 10 Mbit/s on the fibre, 2 bytes around each frame. */
	static constexpr uint64_t BYTE_CLOCKS = 8;
	static constexpr uint64_t FRAME_OVERHEAD = 2;

	enum Irq : unsigned { IRQ2 = 1u << 2, IRQ4 = 1u << 4, IRQ5 = 1u << 5, IRQ6 = 1u << 6 };

	struct Arrival {
		NetFrame frame;
		uint64_t at;
	};

	uint8_t io_read(uint32_t reg);
	void io_write(uint32_t reg, uint8_t value);
	void ctrl_write(uint32_t reg, uint16_t value);
	void raise(unsigned irq) { m_irq |= irq; update_ipl(); }
	void lower(unsigned irq) { m_irq &= ~irq; update_ipl(); }
	void update_ipl();
	void start_transmit();
	void deliver(const NetFrame &frame);
	void schedule_timer();
	uint64_t next_event() const;
	uint8_t &comm(unsigned bank, uint32_t offset) { return m_comm[bank][offset & (COMM_SIZE - 1)]; }

	M68000 m_cpu;
	std::vector<uint8_t> m_ram;
	std::array<std::vector<uint8_t>, 2> m_comm;
	unsigned m_bank;		/* the bank the 68000 sees at 80000 */

	std::array<uint8_t, 0x100> m_io;
	uint16_t m_rx_addr, m_rx_size, m_tx_addr, m_tx_size;
	uint16_t m_status0, m_status1;	/* 88 and 8A: what the host reads */
	bool m_rx_armed;
	bool m_tx_busy;
	uint64_t m_tx_end;

	uint16_t m_timer;		/* 40000+C0: the count that gives IRQ5 */
	uint64_t m_timer_zero;		/* the clock at which the count was 0 */
	uint64_t m_timer_next;
	uint16_t m_leds;
	unsigned m_irq;

	std::array<uint16_t, 0x80> m_host_regs;
	bool m_running;			/* host register C0: the 68000 out of reset */

	uint64_t m_clock;
	std::deque<Arrival> m_arrivals;
	NetLink *m_next;
};

} /* namespace odin */

#endif
