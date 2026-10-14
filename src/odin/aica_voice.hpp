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

#ifndef ODIN_AICA_VOICE_HPP
#define ODIN_AICA_VOICE_HPP

#include <cstdint>

namespace odin {

/* One of the 64 slots of the AICA: the sampler that reads a sound from
 * the wave memory, and its amplitude envelope. The settings stay in the
 * register file of the chip, which the voice reads at each sample period:
 * a new pitch or rate takes effect at once.
 *
 * Sources, compared point by point in the notes (MARKDOWN/30, section 5):
 * the sampler and its loops as Flycast has them from tests on the hardware
 * (core/hw/aica/sgc_if.cpp); the envelope as Neill Corlett steps it
 * (Highly Theoretical, yam.c), whose attack and decay times fall on the
 * table of the official manual; the rate of a step as Flycast computes it
 * (HEURISTIC: MAME and Corlett add the octave once, not twice); no change
 * at a rate of 0 (Saturn SCSP manual, 4.2.2). */
class AicaVoice {
public:
	enum Segment : uint8_t { ATTACK, DECAY1, DECAY2, RELEASE };

	/* Plain data, for the save states */
	struct State {
		uint32_t phase;		/* past the sample at CA, 0x40000 = one sample */
		uint32_t position;	/* CA: the sample being played, from SA */
		int32_t sample;		/* the sample at CA */
		int32_t next;		/* and the one after it */
		uint32_t adpcm_step;
		uint32_t loop_step;	/* the ADPCM decoder as it was at LSA */
		int32_t loop_sample;
		uint32_t level;		/* attenuation of the envelope, 0 the loudest */
		uint8_t segment;
		uint8_t playing;
		uint8_t looped;		/* LP: an end was reached since the monitor was read */
		uint8_t in_loop;	/* LSA was reached once */
	};

	/* What the envelope reads once the voice is silent */
	static constexpr uint32_t OFF = 0x1FFF;

	AicaVoice() { reset(); }
	void reset();

	/* KYONEX with the KYONB bit of this slot set: starts the voice, unless
	 * it is already sounding; with it clear: the release. `slot` is the
	 * 0x80 bytes of the slot in the register file, `noise` the generator
	 * the slots share. */
	void key_on(const uint8_t *slot, const uint8_t *ram, uint32_t ram_mask, uint32_t &noise);
	void key_off();

	/* One sample period: the envelope, then the sampler. `odometer`: the
	 * sample periods the chip has counted. */
	void step(const uint8_t *slot, const uint8_t *ram, uint32_t ram_mask, uint32_t odometer,
	          uint32_t &noise);

	bool silent() const { return m_state.level == OFF; }
	/* 0x2810 for this slot: LP in bit 15, the segment in bits 14-13, the
	 * level in the bits below; LP is cleared when `clear`. */
	uint16_t monitor(bool clear);
	/* CA, 0x2814 */
	uint16_t position() const { return static_cast<uint16_t>(m_state.position); }

	const State &state() const { return m_state; }
	void load(const State &in) { m_state = in; }

private:
	void envelope(const uint8_t *slot, uint32_t odometer);
	/* To the next sample; false when the voice stopped there. */
	bool advance(const uint8_t *slot);
	/* The samples at CA and after it */
	void fetch(const uint8_t *slot, const uint8_t *ram, uint32_t ram_mask, uint32_t &noise);
	void stop();

	State m_state;
};

} /* namespace odin */

#endif /* ODIN_AICA_VOICE_HPP */
