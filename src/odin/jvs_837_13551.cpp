// license:BSD-3-Clause
// copyright-holders:Olivier Galibert
/*
 * Sega 837-13551 JVS I/O board, from MAME src/mame/sega/jvs13551,
 * rewritten for Odin. Odin is GPL-3.0; this file keeps its BSD-3-Clause
 * licence and copyright holder.
 * Odin changes: Copyright (C) 2026 Réda Chérif-Touil
 */

#include "odin/jvs_837_13551.hpp"

namespace odin {

Jvs837_13551::Jvs837_13551(JvsInputs &inputs)
	: m_inputs(inputs), m_outputs(0)
{
	m_coin_counter.fill(0);
}

void Jvs837_13551::device_reset()
{
	m_coin_counter.fill(0);
	m_outputs = 0;
}

/* Identification string reported by the 315-6215 firmware of the Type 1
 * board (MAME jvs13551.cpp ROM list). */
const char *Jvs837_13551::device_id() const
{
	return "SEGA ENTERPRISES,LTD.;I/O BD JVS;837-13551 ;Ver1.00;98/10";
}

void Jvs837_13551::inc_coin(uint8_t slot)
{
	/* Held at 3FFF (315-6215 at 0804) */
	if (slot < COIN_SLOTS && m_coin_counter[slot] < 0x3FFF) {
		m_coin_counter[slot]++;
	}
}

int32_t Jvs837_13551::function_list(std::span<uint8_t> out)
{
	static constexpr std::array<uint8_t, 16> list = {
		jvs::FUNC_SWITCH_INPUT, PLAYERS, 13, 0,	/* 13 switches per player */
		jvs::FUNC_COIN_INPUT, COIN_SLOTS, 0, 0,
		jvs::FUNC_ANALOG_INPUT, ANALOG_CHANNELS, 0, 0,	/* bits not told (block at 3079) */
		jvs::FUNC_GENERAL_OUTPUT, 6, 0, 0		/* 6 driver outputs */
	};
	for (size_t i = 0; i < list.size(); i++) {
		out[i] = list[i];
	}
	return static_cast<int32_t>(list.size());
}

int32_t Jvs837_13551::switches(std::span<uint8_t> out, uint8_t players, uint8_t bytes_per_player)
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
int32_t Jvs837_13551::coin_counters(std::span<uint8_t> out, uint8_t slots)
{
	/* 0 slots is refused too (315-6215 at 0E4D) */
	if (slots == 0 || slots > COIN_SLOTS) {
		return -1;
	}
	uint32_t pos = 0;
	for (uint8_t i = 0; i < slots; i++) {
		out[pos++] = static_cast<uint8_t>(m_coin_counter[i] >> 8);
		out[pos++] = static_cast<uint8_t>(m_coin_counter[i]);
	}
	return static_cast<int32_t>(pos);
}

/* A counter that would go below 0 or above 3FFF is refused and kept as it
 * is (315-6215 at 1141; epr-21868 at 1D6F and 1EB8 does the same). */
bool Jvs837_13551::coin_add(uint8_t slot, int32_t count)
{
	if (slot < 1 || slot > COIN_SLOTS) {
		return false;
	}
	const int32_t value = static_cast<int32_t>(m_coin_counter[slot - 1]) + count;
	if (value < 0 || value > 0x3FFF) {
		return false;
	}
	m_coin_counter[slot - 1] = static_cast<uint16_t>(value);
	return true;
}

int32_t Jvs837_13551::analogs(std::span<uint8_t> out, uint8_t channels)
{
	/* 0 channels is refused too (315-6215 at 0A0F) */
	if (channels == 0 || channels > ANALOG_CHANNELS) {
		return -1;
	}
	uint32_t pos = 0;
	for (uint8_t i = 0; i < channels; i++) {
		const uint16_t value = m_inputs.analog(i);
		/* The converter of the TMP90PH44 gives 8 bits; the low byte
		 * goes out as 0 (315-6215 at 0A46). */
		out[pos++] = static_cast<uint8_t>(value >> 8);
		out[pos++] = 0;
	}
	return static_cast<int32_t>(pos);
}

/* JVS counts output bits from the most significant end: six outputs
 * live in bits 7..2 of the first byte. */
bool Jvs837_13551::swoutputs(std::span<const uint8_t> values)
{
	if (values.size() != 1) {
		return false;
	}
	m_outputs = values[0] & 0xFC;
	return true;
}

void Jvs837_13551::save(Jvs837_13551State &out) const
{
	JvsDevice::save(out.node);
	out.coin_counter = m_coin_counter;
	out.outputs = m_outputs;
}

void Jvs837_13551::load(const Jvs837_13551State &in)
{
	JvsDevice::load(in.node);
	m_coin_counter = in.coin_counter;
	m_outputs = in.outputs;
}

} /* namespace odin */
