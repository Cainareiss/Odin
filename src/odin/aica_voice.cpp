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

#include "odin/aica_voice.hpp"

#include <algorithm>

namespace odin {

namespace {

/* The registers of a slot, at their offset in it */
enum : uint32_t {
	REG_PLAY = 0x00,	/* KYONB, SSCTL, LPCTL, PCMS, SA 22:16 */
	REG_SA = 0x04,		/* SA 15:0 */
	REG_LSA = 0x08,
	REG_LEA = 0x0C,
	REG_RATES = 0x10,	/* D2R, D1R, AR */
	REG_DECAY = 0x14,	/* LPSLNK, KRS, DL, RR */
	REG_PITCH = 0x18	/* OCT, FNS */
};

enum : uint32_t {
	PLAY_NOISE = 0x0400,	/* SSCTL */
	PLAY_LOOP = 0x0200,	/* LPCTL */
	DECAY_LINK = 0x4000	/* LPSLNK */
};

enum : uint32_t { PCM16, PCM8, ADPCM, ADPCM_STREAM };

/* One sample of the sound, in the phase */
constexpr uint32_t ONE = 0x40000;
/* The level of a key on; the level from which the voice is silent. With
 * these two, the steps below take the times of the table of the manual. */
constexpr uint32_t ATTACK_START = 0x280;
constexpr uint32_t SILENT = 0x3C0;
/* The fastest rate: those above it are as fast */
constexpr uint32_t FASTEST = 0x3C;
/* From rate 0x30 up, by the sample period modulo 4: the shift of an attack
 * step and the size of a decay step (yam.c envattackshift and
 * envdecayvalue). Below, 4 and 1. */
constexpr uint8_t FAST_SHIFT[FASTEST - 0x30 + 1][4] = {
	{ 4, 4, 4, 4 }, { 3, 4, 4, 4 }, { 3, 4, 3, 4 }, { 3, 3, 3, 4 }, { 3, 3, 3, 3 },
	{ 2, 3, 3, 3 }, { 2, 3, 2, 3 }, { 2, 2, 2, 3 }, { 2, 2, 2, 2 }, { 1, 2, 2, 2 },
	{ 1, 2, 1, 2 }, { 1, 1, 1, 2 }, { 1, 1, 1, 1 }
};
constexpr uint8_t FAST_DECAY[FASTEST - 0x30 + 1][4] = {
	{ 1, 1, 1, 1 }, { 2, 1, 1, 1 }, { 2, 1, 2, 1 }, { 2, 2, 2, 1 }, { 2, 2, 2, 2 },
	{ 4, 2, 2, 2 }, { 4, 2, 4, 2 }, { 4, 4, 4, 2 }, { 4, 4, 4, 4 }, { 8, 4, 4, 4 },
	{ 8, 4, 8, 4 }, { 8, 8, 8, 4 }, { 8, 8, 8, 8 }
};

/* Yamaha ADPCM, as MAME, Flycast and Corlett agree: a code moves the sample
 * by a multiple of the step, and the step grows or shrinks with it. */
constexpr int32_t ADPCM_SIZE[8] = { 1, 3, 5, 7, 9, 11, 13, 15 };
constexpr uint32_t ADPCM_SCALE[8] = { 0xE6, 0xE6, 0xE6, 0xE6, 0x133, 0x199, 0x200, 0x266 };

uint32_t
reg(const uint8_t *slot, uint32_t offset)
{
	return static_cast<uint32_t>(slot[offset] | (slot[offset + 1] << 8));
}

int32_t
adpcm(uint32_t code, int32_t last, uint32_t &step)
{
	int32_t delta = std::min(static_cast<int32_t>(step) * ADPCM_SIZE[code & 7] / 8, 0x7FFF);

	if (code & 8)
		delta = -delta;
	step = std::clamp((step * ADPCM_SCALE[code & 7]) >> 8, 0x7Fu, 0x6000u);
	return std::clamp(last + delta, -0x8000, 0x7FFF);
}

/* The rate of a segment, raised with the key by KRS: twice the octave plus
 * KRS, never below the rate itself, and the top bit of FNS. */
uint32_t
effective_rate(uint32_t rate, const uint8_t *slot)
{
	const uint32_t decay = reg(slot, REG_DECAY);
	const uint32_t pitch = reg(slot, REG_PITCH);
	const uint32_t krs = (decay >> 10) & 0xF;
	uint32_t effective = rate * 2;

	if (krs != 0xF) {
		const int32_t octave = static_cast<int32_t>(((pitch >> 11) & 0xF) ^ 8) - 8;

		effective += (pitch >> 9) & 1;
		effective += static_cast<uint32_t>(std::max(0, (static_cast<int32_t>(krs) + octave) * 2));
	}
	return std::min(effective, FASTEST);
}

/* Whether the envelope moves at this sample period: below rate 0x30, once
 * in 2^n periods along a pattern of eight; from 0x30, every second one. */
bool
steps(uint32_t rate, uint32_t odometer)
{
	if (rate <= 1)
		return false;
	if (rate >= 0x30)
		return (odometer & 1) == 0;

	const uint32_t shift = 12 - ((rate - 1) >> 2);

	if (odometer & ((1u << shift) - 1))
		return false;
	return ((0xFFFDDDD5u >> (((rate - 1) & 3) * 8 + ((odometer >> shift) & 7))) & 1) != 0;
}

/* The last sample of the sound: an ADPCM stream ends on a group of four */
uint32_t
loop_end(uint32_t play, const uint8_t *slot)
{
	const uint32_t lea = reg(slot, REG_LEA);

	return ((play >> 7) & 3) == ADPCM_STREAM ? (lea + 3) & ~3u : lea;
}

int32_t
noise_sample(uint32_t &noise)
{
	noise = noise * 0x41C64E6Du + 0x3039u;
	return static_cast<int16_t>(noise >> 16);
}

int32_t
pcm16(const uint8_t *ram, uint32_t mask, uint32_t addr)
{
	return static_cast<int16_t>(ram[addr & mask] | (ram[(addr + 1) & mask] << 8));
}

} /* namespace */

void
AicaVoice::reset()
{
	m_state = {};
	m_state.level = OFF;
	m_state.segment = RELEASE;
}

void
AicaVoice::key_on(const uint8_t *slot, const uint8_t *ram, uint32_t ram_mask, uint32_t &noise)
{
	if (m_state.segment != RELEASE)
		return;
	m_state = {};
	m_state.adpcm_step = 0x7F;
	m_state.level = ATTACK_START;
	m_state.segment = ATTACK;
	m_state.playing = 1;
	fetch(slot, ram, ram_mask, noise);
}

void
AicaVoice::key_off()
{
	m_state.segment = RELEASE;
}

void
AicaVoice::step(const uint8_t *slot, const uint8_t *ram, uint32_t ram_mask, uint32_t odometer,
                uint32_t &noise)
{
	if (silent())
		return;
	envelope(slot, odometer);
	if (!m_state.playing)
		return;

	const uint32_t pitch = reg(slot, REG_PITCH);

	m_state.phase += ((pitch & 0x3FF) | 0x400) << (((pitch >> 11) & 0xF) ^ 8);
	while (m_state.phase >= ONE) {
		m_state.phase -= ONE;
		if (!advance(slot))
			return;
		fetch(slot, ram, ram_mask, noise);
	}
}

uint16_t
AicaVoice::monitor(bool clear)
{
	const uint16_t value = static_cast<uint16_t>((m_state.looped ? 0x8000 : 0) |
	                                             (m_state.segment << 13) | m_state.level);

	if (clear)
		m_state.looped = 0;
	return value;
}

void
AicaVoice::envelope(const uint8_t *slot, uint32_t odometer)
{
	const uint32_t rates = reg(slot, REG_RATES);
	const uint32_t decay = reg(slot, REG_DECAY);
	uint32_t rate;

	switch (m_state.segment) {
	case ATTACK:
		rate = rates & 0x1F;
		break;
	case DECAY1:
		rate = (rates >> 6) & 0x1F;
		break;
	case DECAY2:
		rate = (rates >> 11) & 0x1F;
		break;
	default:
		rate = decay & 0x1F;
		break;
	}
	if (rate == 0)
		return;

	const uint32_t effective = effective_rate(rate, slot);

	if (!steps(effective, odometer))
		return;
	if (m_state.segment == ATTACK) {
		/* LPSLNK holds the top until the loop starts */
		if (m_state.level == 0)
			return;

		const uint32_t shift = effective >= 0x30 ? FAST_SHIFT[effective - 0x30][odometer & 3] : 4;

		m_state.level -= (m_state.level >> shift) + 1;
		if (m_state.level == 0 && !(decay & DECAY_LINK))
			m_state.segment = DECAY1;
		return;
	}
	m_state.level += effective >= 0x30 ? FAST_DECAY[effective - 0x30][odometer & 3] : 1;
	if (m_state.segment == DECAY1 && (m_state.level >> 5) >= ((decay >> 5) & 0x1F))
		m_state.segment = DECAY2;
	if (m_state.level >= SILENT)
		stop();
}

bool
AicaVoice::advance(const uint8_t *slot)
{
	const uint32_t play = reg(slot, REG_PLAY);
	const uint32_t lsa = reg(slot, REG_LSA);
	const uint32_t lea = loop_end(play, slot);
	uint32_t ca = m_state.position + 1;

	if ((reg(slot, REG_DECAY) & DECAY_LINK) && m_state.segment == ATTACK && ca >= lsa)
		m_state.segment = DECAY1;
	if (ca >= lea) {
		if (lsa > lea) {
			/* On past LEA up to LSA, then the end, looping or not */
			if (ca >= lsa) {
				m_state.looped = 1;
				stop();
				return false;
			}
		} else {
			m_state.looped = 1;
			if (!(play & PLAY_LOOP)) {
				stop();
				return false;
			}
			ca = lsa;
		}
	}
	m_state.position = ca;
	return true;
}

void
AicaVoice::fetch(const uint8_t *slot, const uint8_t *ram, uint32_t ram_mask, uint32_t &noise)
{
	const uint32_t play = reg(slot, REG_PLAY);
	const uint32_t format = (play >> 7) & 3;
	const uint32_t sa = ((play & 0x7F) << 16) | reg(slot, REG_SA);
	const uint32_t lsa = reg(slot, REG_LSA);
	const uint32_t lea = loop_end(play, slot);
	const uint32_t ca = m_state.position;
	/* The sample after the last one of a loop is its first */
	const uint32_t after = (ca + 1 >= lea && lea > lsa) ? lsa : ca + 1;

	if (play & PLAY_NOISE) {
		m_state.sample = noise_sample(noise);
		m_state.next = noise_sample(noise);
		return;
	}
	switch (format) {
	case PCM16:
		m_state.sample = pcm16(ram, ram_mask, (sa & ~1u) + ca * 2);
		m_state.next = pcm16(ram, ram_mask, (sa & ~1u) + after * 2);
		return;
	case PCM8:
		m_state.sample = static_cast<int8_t>(ram[(sa + ca) & ram_mask]) * 256;
		m_state.next = static_cast<int8_t>(ram[(sa + after) & ram_mask]) * 256;
		return;
	default:
		break;
	}

	const auto code = [&](uint32_t n) {
		return static_cast<uint32_t>(ram[(sa + (n >> 1)) & ram_mask] >> ((n & 1) * 4)) & 0xF;
	};

	/* The decoder is kept as it was at LSA the first time, and plain ADPCM
	 * takes it back at each turn of the loop; a stream goes on. */
	if (ca == lsa) {
		if (!m_state.in_loop) {
			m_state.in_loop = 1;
			m_state.loop_step = m_state.adpcm_step;
			m_state.loop_sample = m_state.sample;
		} else if (format == ADPCM) {
			m_state.adpcm_step = m_state.loop_step;
			m_state.sample = m_state.loop_sample;
		}
	}
	m_state.sample = adpcm(code(ca), m_state.sample, m_state.adpcm_step);

	uint32_t step = m_state.adpcm_step;
	int32_t last = m_state.sample;

	if (format == ADPCM && after == lsa && m_state.in_loop) {
		step = m_state.loop_step;
		last = m_state.loop_sample;
	}
	m_state.next = adpcm(code(after), last, step);
}

void
AicaVoice::stop()
{
	m_state.playing = 0;
	m_state.position = 0;
	m_state.phase = 0;
	m_state.level = OFF;
	m_state.segment = RELEASE;
}

} /* namespace odin */
