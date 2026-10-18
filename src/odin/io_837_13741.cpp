/*
 * Odin
 * Copyright (C) 2026 Réda Chérif-Touil
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "odin/io_837_13741.hpp"

#include <algorithm>
#include <cstdio>
#include <cstdlib>

namespace odin {

Io837_13741::Io837_13741(std::span<const uint8_t> eprom, JvsInputs &inputs)
	: m_inputs(inputs), m_chip(*this, eprom, m_ram), m_chip_line(*this)
{
	m_chip.attach_line(&m_chip_line);
	reset();
}

void
Io837_13741::reset()
{
	m_ram.fill(0);
	m_chip.reset();
	m_timer.reset();
	m_to_chip_count = m_to_mie_count = 0;
	m_cycles = 0;
	m_chip_start = 0;
	m_addressed = false;
	for (unsigned slot = 0; slot < 2; slot++) {
		m_coin_phase[slot] = 0;
		m_coin_until[slot] = 0;
		m_coins[slot] = 0;
	}
	read_pins();
}

/* The switches as the cabinet has them, the coins as they go through, the
 * analog channels, for the run to come. */
void
Io837_13741::read_pins()
{
	const uint8_t system = m_inputs.system();
	const uint16_t player[2] = { m_inputs.player(0), m_inputs.player(1) };
	/* Bit n of a port from the bit `bit` of a word, closed at 0 */
	const auto closed = [](uint16_t word, unsigned bit) { return (word >> bit) & 1u; };

	m_pins.fill(0xFF);
	/* DIP switches off, the meters wired, the coins and TEST, TILT */
	m_pins[0] = 0xF3;
	if (system & 0x80)
		m_pins[0] = static_cast<uint8_t>(m_pins[0] & ~0x40);
	if (system & 0x40)
		m_pins[0] = static_cast<uint8_t>(m_pins[0] & ~0x80);
	for (unsigned k = 0; k < 8; k++) {
		if (closed(player[0], 15 - k))
			m_pins[1] = static_cast<uint8_t>(m_pins[1] & ~(1u << k));
		if (closed(player[1], 15 - k))
			m_pins[2] = static_cast<uint8_t>(m_pins[2] & ~(1u << k));
	}
	for (unsigned k = 0; k < 4; k++) {
		if (closed(player[0], 7 - k))
			m_pins[6] = static_cast<uint8_t>(m_pins[6] & ~(1u << k));
		if (closed(player[1], 7 - k))
			m_pins[6] = static_cast<uint8_t>(m_pins[6] & ~(1u << (4 + k)));
	}

	for (unsigned slot = 0; slot < 2; slot++) {
		if (m_coin_phase[slot] && m_cycles >= m_coin_until[slot]) {
			m_coin_phase[slot] = m_coin_phase[slot] == 1 ? 2 : 0;
			m_coin_until[slot] = m_cycles + COIN_CYCLES;
		}
		if (!m_coin_phase[slot] && m_coins[slot]) {
			m_coins[slot]--;
			m_coin_phase[slot] = 1;
			m_coin_until[slot] = m_cycles + COIN_CYCLES;
		}
		if (m_coin_phase[slot] == 1)
			m_pins[0] = static_cast<uint8_t>(m_pins[0] & ~(0x10u << slot));
	}
	for (uint8_t channel = 0; channel < 8; channel++)
		m_analog[channel] = static_cast<uint8_t>(m_inputs.analog(channel) >> 8);
}

void
Io837_13741::run_chip(uint64_t cycles)
{
	m_chip_start = now();
	m_cycles += m_chip.run(static_cast<uint32_t>(cycles));
}

void
Io837_13741::run_until(uint64_t tick)
{
	read_pins();
	while (now() < tick) {
		if (m_to_chip_count && m_to_chip[0].tick <= now()) {
			m_chip.line_received(m_to_chip[0].value);
			std::copy(m_to_chip.begin() + 1, m_to_chip.begin() + m_to_chip_count, m_to_chip.begin());
			m_to_chip_count--;
			continue;
		}
		const uint64_t pulse = m_timer.next_pulse() == UINT64_MAX ? UINT64_MAX
		                     : m_timer.next_pulse() * Mie::TICKS_PER_CYCLE;
		if (pulse <= now()) {
			m_chip.raise_external();
			m_timer.pulse_taken();
			continue;
		}
		uint64_t to = std::min(tick, pulse);

		if (m_to_chip_count)
			to = std::min(to, m_to_chip[0].tick);
		run_chip((to - now() + Mie::TICKS_PER_CYCLE - 1) / Mie::TICKS_PER_CYCLE);
	}
}

void
Io837_13741::deliver(Mie &mie, uint64_t tick)
{
	uint32_t n = 0;

	for (; n < m_to_mie_count && m_to_mie[n].tick <= tick; n++) {
		if (m_to_mie[n].kind == BYTE)
			mie.line_received(m_to_mie[n].value);
		else
			mie.set_line_addressed(m_to_mie[n].value != 0);
	}
	std::copy(m_to_mie.begin() + n, m_to_mie.begin() + m_to_mie_count, m_to_mie.begin());
	m_to_mie_count -= n;
}

/* A byte of the MIE, its start bit `ticks` into its run: the chip has it
 * a character later. */
void
Io837_13741::line_sent(uint8_t byte, uint64_t ticks)
{
	const uint64_t due = m_run_start + ticks + CHAR_TICKS;

	if (due < now()) {
		std::fprintf(stderr, "FATAL: 837-13741: a byte of the MIE due where the chip has been: the chip too far ahead\n");
		std::abort();
	}
	if (m_to_chip_count == m_to_chip.size()) {
		std::fprintf(stderr, "FATAL: 837-13741: more bytes from the MIE than the line holds\n");
		std::abort();
	}
	m_to_chip[m_to_chip_count++] = { due, BYTE, byte };
}

/* A byte of the chip, its start bit `ticks` into its run: the MIE has it a
 * character later. */
void
Io837_13741::chip_sent(uint8_t byte, uint64_t ticks)
{
	to_mie(m_chip_start + ticks + CHAR_TICKS, BYTE, byte);
}

/* In the order the MIE has them: the sense at once, a byte a character
 * after its start bit */
void
Io837_13741::to_mie(uint64_t tick, uint8_t kind, uint8_t value)
{
	if (m_to_mie_count == m_to_mie.size()) {
		std::fprintf(stderr, "FATAL: 837-13741: more for the MIE than the line holds\n");
		std::abort();
	}
	uint32_t n = m_to_mie_count++;

	for (; n > 0 && m_to_mie[n - 1].tick > tick; n--)
		m_to_mie[n] = m_to_mie[n - 1];
	m_to_mie[n] = { tick, kind, value };
}

uint8_t
Io837_13741::gpio_read(uint8_t port)
{
	return m_pins[port & 7];
}

/* Output n of JVS from its pin, on at 0 while the chip drives the port */
std::span<const uint8_t>
Io837_13741::outputs() const
{
	m_outputs.fill(0);
	for (unsigned n = 1; n <= 22; n++) {
		const uint8_t port = n <= 6 ? 3 : n <= 14 ? 4 : 5;
		const unsigned bit = n <= 6 ? n + 1 : n <= 14 ? n - 7 : n - 15;

		if (m_chip.gpio_is_output(port) && !(m_chip.gpio_latch(port) & (1u << bit)))
			m_outputs[(n - 1) / 8] = static_cast<uint8_t>(m_outputs[(n - 1) / 8] | 0x80u >> ((n - 1) % 8));
	}
	return m_outputs;
}

bool
Io837_13741::board_out8(uint8_t port, uint8_t value)
{
	if (port < 0xA0 || port > 0xA3)
		return false;
	const bool known = !m_timer.unknown();

	m_timer.write(static_cast<uint8_t>(port & 3), value, chip_now() / Mie::TICKS_PER_CYCLE);
	if (known && m_timer.unknown())
		std::fprintf(stderr, "ERROR: 837-13741: the D71054 set in a way that is not run\n");
	return true;
}

bool
Io837_13741::sense_pins(uint8_t &value)
{
	value = 0xFF;
	return true;
}

void
Io837_13741::jvs_control_written(uint8_t value)
{
	const bool addressed = (value & 0x02) != 0;

	if (addressed != m_addressed) {
		m_addressed = addressed;
		to_mie(chip_now(), SENSE, addressed);
	}
}

void
Io837_13741::save(State &out) const
{
	m_chip.save(out.chip);
	out.ram = m_ram;
	m_timer.save(out.timer);
	out.to_chip = m_to_chip;
	out.to_mie = m_to_mie;
	out.to_chip_count = m_to_chip_count;
	out.to_mie_count = m_to_mie_count;
	out.cycles = m_cycles;
	out.addressed = m_addressed;
	for (unsigned slot = 0; slot < 2; slot++) {
		out.coin_phase[slot] = m_coin_phase[slot];
		out.coin_until[slot] = m_coin_until[slot];
		out.coins[slot] = m_coins[slot];
	}
}

void
Io837_13741::load(const State &in, Mie &mie)
{
	m_chip.load(in.chip);
	m_ram = in.ram;
	m_timer.load(in.timer);
	m_to_chip = in.to_chip;
	m_to_mie = in.to_mie;
	m_to_chip_count = in.to_chip_count;
	m_to_mie_count = in.to_mie_count;
	m_cycles = in.cycles;
	m_addressed = in.addressed != 0;
	for (unsigned slot = 0; slot < 2; slot++) {
		m_coin_phase[slot] = in.coin_phase[slot];
		m_coin_until[slot] = in.coin_until[slot];
		m_coins[slot] = in.coins[slot];
	}
	/* The MIE sees the line as the events it has not had leave it */
	bool told = m_addressed;
	for (uint32_t n = m_to_mie_count; n-- > 0;)
		if (m_to_mie[n].kind == SENSE)
			told = m_to_mie[n].value == 0;
	mie.set_line_addressed(told);
	read_pins();
}

} /* namespace odin */
