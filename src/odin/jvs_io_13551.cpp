// license:BSD-3-Clause
// copyright-holders:Olivier Galibert
/*
 * Sega 837-13551 JVS I/O board, from MAME src/mame/sega/jvs13551,
 * rewritten for Odin. Odin is GPL-3.0; this file keeps its BSD-3-Clause
 * licence and copyright holder.
 */

#include "odin/jvs_io_13551.hpp"

namespace odin {

JvsIo13551::JvsIo13551(JvsInputs &inputs)
	: m_inputs(inputs), m_outputs(0)
{
	m_coin_counter.fill(0);
}

void JvsIo13551::device_reset()
{
	m_coin_counter.fill(0);
	m_outputs = 0;
}

/* Identification string reported by the 315-6215 firmware of the Type 1
 * board (MAME jvs13551.cpp ROM list). */
const char *JvsIo13551::device_id() const
{
	return "SEGA ENTERPRISES,LTD.;I/O BD JVS;837-13551 ;Ver1.00;98/10";
}

void JvsIo13551::inc_coin(uint8_t slot)
{
	if (slot < COIN_SLOTS) {
		m_coin_counter[slot] = static_cast<uint16_t>((m_coin_counter[slot] + 1) & 0x3FFF);
	}
}

int32_t JvsIo13551::function_list(std::span<uint8_t> out)
{
	static constexpr std::array<uint8_t, 16> list = {
		0x01, PLAYERS, 13, 0,		/* switch inputs, 13 bits per player */
		0x02, COIN_SLOTS, 0, 0,		/* coin inputs */
		0x03, ANALOG_CHANNELS, 16, 0,	/* analog inputs, 16 bits */
		0x12, 6, 0, 0			/* driver outputs */
	};
	for (size_t i = 0; i < list.size(); i++) {
		out[i] = list[i];
	}
	return static_cast<int32_t>(list.size());
}

int32_t JvsIo13551::switches(std::span<uint8_t> out, uint8_t players, uint8_t bytes_per_player)
{
	if (players > PLAYERS || bytes_per_player > 2) {
		return -1;
	}
	uint32_t pos = 0;
	out[pos++] = m_inputs.system();
	for (uint8_t i = 0; i < players; i++) {
		const uint16_t value = m_inputs.player(i);
		for (uint8_t j = 0; j < bytes_per_player; j++) {
			out[pos++] = static_cast<uint8_t>(value >> ((1 - j) * 8));
		}
	}
	return static_cast<int32_t>(pos);
}

/* Two bytes per slot: bits 15-14 condition (0 normal), 14-bit count. */
int32_t JvsIo13551::coin_counters(std::span<uint8_t> out, uint8_t slots)
{
	if (slots > COIN_SLOTS) {
		return -1;
	}
	uint32_t pos = 0;
	for (uint8_t i = 0; i < slots; i++) {
		out[pos++] = static_cast<uint8_t>(m_coin_counter[i] >> 8);
		out[pos++] = static_cast<uint8_t>(m_coin_counter[i]);
	}
	return static_cast<int32_t>(pos);
}

bool JvsIo13551::coin_add(uint8_t slot, int32_t count)
{
	if (slot < 1 || slot > COIN_SLOTS) {
		return false;
	}
	const int32_t value = static_cast<int32_t>(m_coin_counter[slot - 1]) + count;
	m_coin_counter[slot - 1] = static_cast<uint16_t>(value & 0x3FFF);
	return true;
}

int32_t JvsIo13551::analogs(std::span<uint8_t> out, uint8_t channels)
{
	if (channels > ANALOG_CHANNELS) {
		return -1;
	}
	uint32_t pos = 0;
	for (uint8_t i = 0; i < channels; i++) {
		const uint16_t value = m_inputs.analog(i);
		out[pos++] = static_cast<uint8_t>(value >> 8);
		out[pos++] = static_cast<uint8_t>(value);
	}
	return static_cast<int32_t>(pos);
}

/* JVS counts output bits from the most significant end: six outputs
 * live in bits 7..2 of the first byte. */
bool JvsIo13551::swoutputs(std::span<const uint8_t> values)
{
	if (values.size() != 1) {
		return false;
	}
	m_outputs = values[0] & 0xFC;
	return true;
}

bool JvsIo13551::swoutput(uint8_t id, uint8_t value)
{
	if (id > 6) {
		return false;
	}
	const uint8_t mask = static_cast<uint8_t>(1u << id);
	switch (value) {
	case 0:
		m_outputs = static_cast<uint8_t>(m_outputs & ~mask);
		break;
	case 1:
		m_outputs = static_cast<uint8_t>(m_outputs | mask);
		break;
	case 2:
		m_outputs = static_cast<uint8_t>(m_outputs ^ mask);
		break;
	default:
		break;
	}
	return true;
}

} /* namespace odin */
