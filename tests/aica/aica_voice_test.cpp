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

/* The voices of the AICA against known results: the pitch, the formats,
 * ADPCM against values from MAME's decoder, the loops and the ends of a
 * sound, the envelope against the table of times of the manual, what the
 * sound driver reads of a voice, the levels, the LFO and the mix.
 *
 * Build: g++ -std=c++20 -Wall -Wextra -Wpedantic -Wconversion -Wshadow \
 *            -O2 -I src tests/aica/aica_voice_test.cpp src/odin/aica.cpp \
 *            src/odin/aica_voice.cpp src/odin/aica_dsp.cpp -o aica_voice_test
 */

#include "odin/aica.hpp"
#include "odin/aica_voice.hpp"

#include <array>
#include <cstdio>
#include <cstdlib>
#include <vector>

namespace {

uint32_t failures = 0;

void
check(bool ok, const char *what)
{
	if (!ok) {
		failures++;
		printf("FAIL: %s\n", what);
	}
}

/* Official times of the envelope by effective rate, in ms: attack, then
 * decay (Saturn SCSP manual, as MAME and Flycast copy them) */
constexpr double ATTACK_MS[64] = {
	0, 0, 8100.0, 6900.0, 6000.0, 4800.0, 4000.0, 3400.0, 3000.0, 2400.0, 2000.0, 1700.0, 1500.0,
	1200.0, 1000.0, 860.0, 760.0, 600.0, 500.0, 430.0, 380.0, 300.0, 250.0, 220.0, 190.0, 150.0, 130.0,
	110.0, 95.0, 76.0, 63.0, 55.0, 47.0, 38.0, 31.0, 27.0, 24.0, 19.0, 15.0, 13.0, 12.0, 9.4, 7.9, 6.8,
	6.0, 4.7, 3.8, 3.4, 3.0, 2.4, 2.0, 1.8, 1.6, 1.3, 1.1, 0.93, 0.85, 0.65, 0.53, 0.44, 0.40, 0.35, 0, 0
};
constexpr double DECAY_MS[64] = {
	0, 0, 118200.0, 101300.0, 88600.0, 70900.0, 59100.0, 50700.0, 44300.0, 35500.0, 29600.0, 25300.0,
	22200.0, 17700.0, 14800.0, 12700.0, 11100.0, 8900.0, 7400.0, 6300.0, 5500.0, 4400.0, 3700.0,
	3200.0, 2800.0, 2200.0, 1800.0, 1600.0, 1400.0, 1100.0, 920.0, 790.0, 690.0, 550.0, 460.0, 390.0,
	340.0, 270.0, 230.0, 200.0, 170.0, 140.0, 110.0, 98.0, 85.0, 68.0, 57.0, 49.0, 43.0, 34.0, 28.0,
	25.0, 22.0, 18.0, 14.0, 12.0, 11.0, 8.5, 7.1, 6.1, 5.4, 4.3, 3.6, 3.1
};

/* A slot in the register file, and a small wave memory */
struct Voice {
	std::array<uint8_t, odin::Aica::SLOT_SIZE> slot{};
	std::vector<uint8_t> ram = std::vector<uint8_t>(0x10000);
	odin::AicaVoice voice;
	uint32_t odometer = 0;
	uint32_t noise = 0;

	void set(uint32_t offset, uint32_t value)
	{
		slot[offset] = static_cast<uint8_t>(value);
		slot[offset + 1] = static_cast<uint8_t>(value >> 8);
	}
	/* PCMS, LPCTL, SA; the loop; envelope rates; KRS off unless given */
	void sound(uint32_t pcms, bool loop, uint32_t sa, uint32_t lsa, uint32_t lea)
	{
		set(0x00, (loop ? 0x0200u : 0) | (pcms << 7) | ((sa >> 16) & 0x7F));
		set(0x04, sa & 0xFFFF);
		set(0x08, lsa);
		set(0x0C, lea);
	}
	void rates(uint32_t ar, uint32_t d1r, uint32_t d2r, uint32_t rr, uint32_t dl = 0x1F,
	           uint32_t krs = 0xF, bool link = false)
	{
		set(0x10, (d2r << 11) | (d1r << 6) | ar);
		set(0x14, (link ? 0x4000u : 0) | (krs << 10) | (dl << 5) | rr);
	}
	/* TL, VOFF; the LFO */
	void level(uint32_t tl, bool off) { set(0x28, (tl << 8) | (off ? 0x40u : 0)); }
	void lfo(uint32_t reg) { set(0x1C, reg); }
	int32_t output() { return voice.output(slot.data(), noise); }
	/* OCT from -8 to 7 */
	void pitch(int32_t octave, uint32_t fns)
	{
		set(0x18, ((static_cast<uint32_t>(octave) & 0xF) << 11) | fns);
	}
	void key_on() { voice.key_on(slot.data(), ram.data(), 0xFFFF, noise); }
	void step(uint32_t n = 1)
	{
		for (uint32_t i = 0; i < n; i++)
			voice.step(slot.data(), ram.data(), 0xFFFF, odometer++, noise);
	}
	/* Sample periods until the condition holds, or `limit` */
	template <typename F>
	uint32_t until(F done, uint32_t limit)
	{
		uint32_t n = 0;
		while (!done() && n < limit) {
			step();
			n++;
		}
		return n;
	}
	const odin::AicaVoice::State &state() const { return voice.state(); }
};

void
test_pitch()
{
	struct Case {
		int32_t octave;
		uint32_t fns, steps, position;
		const char *what;
	};
	static const Case cases[] = {
		{ 0, 0, 100, 100, "pitch: one sample per period at OCT 0, FNS 0" },
		{ 1, 0, 100, 200, "pitch: two at OCT +1" },
		{ -1, 0, 100, 50, "pitch: half at OCT -1" },
		{ 0, 0x200, 100, 150, "pitch: FNS 0x200 adds half" },
		{ -8, 0, 1024, 4, "pitch: 1/256 at OCT -8" },
		{ 2, 0x100, 10, 50, "pitch: 5 a period at OCT +2, FNS 0x100" },
	};

	for (const Case &c : cases) {
		Voice v;
		v.sound(0, true, 0, 0, 0xFFF0);
		v.rates(31, 0, 0, 0);
		v.pitch(c.octave, c.fns);
		v.key_on();
		v.step(c.steps);
		check(v.state().position == c.position, c.what);
	}
}

void
test_formats()
{
	Voice v;
	for (uint32_t i = 0; i < 8; i++) {
		const uint16_t s = static_cast<uint16_t>(static_cast<int16_t>(1000 * (static_cast<int32_t>(i) - 4)));
		v.ram[0x100 + i * 2] = static_cast<uint8_t>(s);
		v.ram[0x100 + i * 2 + 1] = static_cast<uint8_t>(s >> 8);
		v.ram[0x200 + i] = static_cast<uint8_t>(static_cast<int8_t>(i * 30 - 100));
	}
	v.sound(0, false, 0x101, 0, 8);
	v.rates(31, 0, 0, 0);
	v.pitch(0, 0);
	v.key_on();
	check(v.state().sample == -4000 && v.state().next == -3000, "PCM16: samples at CA and after, SA made even");
	v.step(3);
	check(v.state().sample == -1000 && v.state().next == 0, "PCM16: three periods on");

	Voice w;
	w.ram = v.ram;
	w.sound(1, false, 0x200, 0, 8);
	w.rates(31, 0, 0, 0);
	w.pitch(0, 0);
	w.key_on();
	w.step(2);
	check(w.state().sample == (60 - 100) * 256 && w.state().next == (90 - 100) * 256, "PCM8: signed, times 256");
}

/* Codes 7 7 7 7 7 7 F F 0 8 3 B 1 9 7 0, low nibble first; the samples
 * and steps MAME's DecodeADPCM gives for them, from 0 and 0x7F */
constexpr uint8_t ADPCM_BYTES[8] = { 0x77, 0x77, 0x77, 0xFF, 0x80, 0xB3, 0x91, 0x07 };
constexpr int32_t ADPCM_SAMPLES[16] = { 238, 808, 2174, 5451, 13311, 32162, -605, -32768,
	-29696, -32456, -15099, -30693, -24689, -30083, -5853, -2781 };

void
test_adpcm()
{
	Voice v;
	for (uint32_t i = 0; i < 8; i++)
		v.ram[0x400 + i] = ADPCM_BYTES[i];
	v.sound(2, false, 0x400, 0, 0x100);
	v.rates(31, 0, 0, 0);
	v.pitch(0, 0);
	v.key_on();

	bool same = true;
	for (uint32_t i = 0; i < 16; i++) {
		same = same && v.state().sample == ADPCM_SAMPLES[i];
		if (i < 15)
			same = same && v.state().next == ADPCM_SAMPLES[i + 1];
		v.step();
	}
	check(same, "ADPCM: the samples of MAME's decoder, at CA and after it");

	/* Codes 0 keep the step at its floor, 0x7F: 15 more each time */
	Voice f;
	f.sound(2, false, 0x500, 0, 0x100);
	f.rates(31, 0, 0, 0);
	f.pitch(0, 0);
	f.key_on();
	f.step(3);
	check(f.state().sample == 60 && f.state().adpcm_step == 0x7F, "ADPCM: the step stays at 0x7F");

	/* Two periods at once: every code in between is decoded */
	Voice w;
	w.ram = v.ram;
	w.sound(2, false, 0x400, 0, 0x100);
	w.rates(31, 0, 0, 0);
	w.pitch(1, 0);
	w.key_on();
	w.step(5);
	check(w.state().sample == ADPCM_SAMPLES[10], "ADPCM: codes skipped by the pitch are decoded too");

	/* A loop from 4 to 8: plain ADPCM takes the decoder back at LSA, a
	 * stream goes on (after code 7 the sample is -32768, the step 0x6000:
	 * code 7 adds 0x7FFF) */
	Voice a;
	a.ram = v.ram;
	a.sound(2, true, 0x400, 4, 8);
	a.rates(31, 0, 0, 0);
	a.pitch(0, 0);
	a.key_on();
	a.step(8);
	check(a.state().position == 4 && a.state().sample == ADPCM_SAMPLES[4], "ADPCM: same sample at each turn");
	check(a.state().looped == 1, "ADPCM: LP set at the end of the loop");

	Voice b;
	b.ram = v.ram;
	b.sound(3, true, 0x400, 4, 8);
	b.rates(31, 0, 0, 0);
	b.pitch(0, 0);
	b.key_on();
	b.step(8);
	check(b.state().position == 4 && b.state().sample == -1, "ADPCM stream: the decoder goes on at LSA");
}

void
test_ends()
{
	Voice v;
	v.sound(0, false, 0, 0, 10);
	v.rates(31, 0, 0, 0);
	v.pitch(0, 0);
	v.key_on();
	v.step(9);
	check(v.state().position == 9 && !v.voice.silent(), "no loop: plays up to LEA - 1");
	v.step();
	check(v.state().position == 0 && v.voice.silent(), "no loop: at LEA, CA 0 and the voice silent");
	check(v.voice.monitor(true) == 0xFFFF, "no loop: monitor LP, release, level 0x1FFF");
	check(v.voice.monitor(false) == 0x7FFF, "monitor: LP cleared by the read before");

	Voice l;
	l.sound(0, true, 0, 4, 10);
	l.rates(31, 0, 0, 0);
	l.pitch(0, 0);
	l.key_on();
	l.step(10);
	check(l.state().position == 4 && !l.voice.silent() && l.state().looped, "loop: back to LSA, LP set");

	Voice g;
	g.sound(0, true, 0, 20, 10);
	g.rates(31, 0, 0, 0);
	g.pitch(0, 0);
	g.key_on();
	g.step(19);
	check(g.state().position == 19 && !g.voice.silent(), "LSA past LEA: on up to LSA");
	g.step();
	check(g.state().position == 0 && g.voice.silent(), "LSA past LEA: stops at LSA though looping");

	Voice s;
	s.sound(3, false, 0, 0, 9);
	s.rates(31, 0, 0, 0);
	s.pitch(0, 0);
	s.key_on();
	s.step(11);
	check(s.state().position == 11 && !s.voice.silent(), "ADPCM stream: LEA 9 ends at 12");
	s.step();
	check(s.voice.silent(), "ADPCM stream: silent at 12");
}

/* The periods an attack takes from the key on to the top, and a decay
 * from the top to silence, against the manual: within the spread of the
 * steps up to rate 48 (0.97 to 1.06 of the table); above, where an attack
 * lasts a millisecond or less, the steps of four rates are the same and
 * fall from 0.73 to 1.04 of the table. */
void
test_envelope_times()
{
	bool attack_ok = true, decay_ok = true;
	char what[96];

	for (uint32_t rate = 4; rate <= 60; rate += rate < 48 ? 2 : 1) {
		const double low = rate <= 48 ? 0.96 : 0.70, high = rate <= 48 ? 1.06 : 1.06;
		const double dlow = rate <= 48 ? 0.98 : 0.70, dhigh = rate <= 48 ? 1.07 : 1.06;
		const uint32_t krs = rate % 2 ? 0 : 0xF;
		Voice a;
		a.sound(0, true, 0, 0, 0xFFF0);
		/* Odd rates: KRS 0 at OCT +0, FNS bit 9 set, adds one */
		a.rates(rate / 2, 0, 0, 0, 0x1F, krs);
		a.pitch(0, rate % 2 ? 0x200 : 0);
		a.key_on();
		const uint32_t n = a.until([&] { return a.state().segment != odin::AicaVoice::ATTACK; }, 1u << 30);
		const double ratio = n / (ATTACK_MS[rate] * 44.1);
		if (ratio < low || ratio > high) {
			snprintf(what, sizeof(what), "envelope: attack at rate %u takes %.3f of the table", rate, ratio);
			check(false, what);
			attack_ok = false;
		}

		Voice d;
		d.sound(0, true, 0, 0, 0xFFF0);
		d.rates(31, rate / 2, 0, 0, 0x1F, krs);
		d.pitch(0, rate % 2 ? 0x200 : 0);
		d.key_on();
		d.until([&] { return d.state().segment != odin::AicaVoice::ATTACK; }, 1u << 20);
		const uint32_t m = d.until([&] { return d.voice.silent(); }, 1u << 30);
		const double dratio = m / (DECAY_MS[rate] * 44.1);
		if (dratio < dlow || dratio > dhigh) {
			snprintf(what, sizeof(what), "envelope: decay at rate %u takes %.3f of the table", rate, dratio);
			check(false, what);
			decay_ok = false;
		}
	}
	check(attack_ok, "envelope: attack times of the manual");
	check(decay_ok, "envelope: decay times of the manual");
}

void
test_envelope_rules()
{
	/* A rate of 0 does not move, KRS or not */
	Voice z;
	z.sound(0, true, 0, 0, 0xFFF0);
	z.rates(31, 0, 0, 0, 0x1F, 0);
	z.pitch(7, 0x3FF);
	z.key_on();
	z.step(20000);
	check(z.state().segment == odin::AicaVoice::DECAY1 && z.state().level == 0, "rate 0: no decay, KRS 0");
	z.voice.key_off();
	z.step(20000);
	check(z.state().level == 0 && !z.voice.silent(), "rate 0: no release");

	/* A key on while the voice sounds changes nothing */
	Voice again;
	again.sound(0, true, 0, 0, 0xFFF0);
	again.rates(31, 0, 0, 0);
	again.pitch(0, 0);
	again.key_on();
	again.step(50);
	again.key_on();
	check(again.state().position == 50, "key on while sounding: ignored");

	/* Decay 1 hands over to decay 2 at DL */
	Voice d;
	d.sound(0, true, 0, 0, 0xFFF0);
	d.rates(31, 31, 0, 0, 4);
	d.pitch(0, 0);
	d.key_on();
	d.until([&] { return d.state().segment == odin::AicaVoice::DECAY2; }, 1u << 20);
	check(d.state().level >> 5 == 4, "DL: decay 2 from the level whose high bits are DL");

	/* LPSLNK holds the top until LSA */
	Voice k;
	k.sound(0, true, 0, 3000, 0xFFF0);
	k.rates(31, 31, 0, 0, 0x1F, 0xF, true);
	k.pitch(0, 0);
	k.key_on();
	k.step(2000);
	check(k.state().segment == odin::AicaVoice::ATTACK && k.state().level == 0, "LPSLNK: top held before LSA");
	k.step(1000);
	check(k.state().segment == odin::AicaVoice::DECAY1, "LPSLNK: decay 1 from LSA on");

	/* KRS raises the rate by twice KRS plus the octave, never lowers it
	 * (D1R 10 is rate 20; KRS 0 at OCT +2 makes it 24, at OCT -3 it stays) */
	bool krs_ok = true;
	struct Case {
		int32_t octave;
		uint32_t rate;
	};
	for (const Case c : { Case{ 2, 24 }, Case{ -3, 20 } }) {
		Voice r;
		r.sound(0, true, 0, 0, 0xFFF0);
		r.rates(31, 10, 0, 0, 0x1F, 0);
		r.pitch(c.octave, 0);
		r.key_on();
		r.until([&] { return r.state().segment != odin::AicaVoice::ATTACK; }, 1u << 20);
		const uint32_t m = r.until([&] { return r.voice.silent(); }, 1u << 30);
		const double ratio = m / (DECAY_MS[c.rate] * 44.1);
		krs_ok = krs_ok && ratio > 0.98 && ratio < 1.07;
	}
	check(krs_ok, "KRS: twice KRS plus the octave, not below the rate");
}

/* A constant PCM16 sample, looping, at the top of its envelope */
Voice
steady(int16_t value, uint32_t tl, bool off)
{
	Voice v;
	for (uint32_t i = 0; i < 64; i++) {
		v.ram[0x800 + i * 2] = static_cast<uint8_t>(value);
		v.ram[0x800 + i * 2 + 1] = static_cast<uint8_t>(static_cast<uint16_t>(value) >> 8);
	}
	v.sound(0, true, 0x800, 0, 64);
	v.rates(31, 0, 0, 0);
	v.pitch(0, 0);
	v.level(tl, off);
	v.key_on();
	v.until([&] { return v.state().level == 0; }, 1000);
	return v;
}

void
test_levels()
{
	/* Halfway between 0 and 16384 at half speed */
	Voice v;
	v.ram[0x900] = 0;
	v.ram[0x901] = 0;
	v.ram[0x902] = 0x00;
	v.ram[0x903] = 0x40;
	v.sound(0, true, 0x900, 0, 2);
	v.rates(31, 0, 0, 0);
	v.pitch(-1, 0);
	v.level(0, true);
	v.key_on();
	v.step();
	check(v.output() == 8192, "interpolation: halfway between two samples");
	Voice q;
	q.ram = v.ram;
	q.sound(0, true, 0x900, 0, 2);
	q.rates(31, 0, 0, 0);
	q.pitch(-2, 0);
	q.level(0, true);
	q.key_on();
	q.step();
	check(q.output() == 4096, "interpolation: a quarter of the way");

	check(steady(16384, 0, false).output() == 16384, "TL 0 at the top: as read");
	check(steady(16384, 0x10, false).output() == 8192, "TL 0x10: -6 dB");
	check(steady(16384, 0x20, false).output() == 4096, "TL 0x20: -12 dB");
	check(steady(16384, 0xF0, false).output() == 0, "TL 0xF0: silent");
	check(steady(16384, 0xF0, true).output() == 16384, "VOFF: no attenuation");

	/* The envelope adds to TL: 0x280 at the key on is 60 dB down */
	Voice k;
	for (uint32_t i = 0; i < 64; i++)
		k.ram[0x801 + i * 2] = 0x40;
	k.sound(0, true, 0x800, 0, 64);
	k.rates(0, 0, 0, 0);
	k.pitch(0, 0);
	k.level(0, false);
	k.key_on();
	check(k.output() == 16, "envelope: 0x280 at the key on");
}

void
test_lfo()
{
	/* One step of the phase: 2^32 / (256 O) with O from LFOF, the periods
	 * of the manual (LFOF 9: 0.78 Hz, 10: 0.92 Hz; 31: 172.3 Hz) */
	struct Case {
		uint32_t lfof, step;
	};
	bool steps_ok = true;
	for (const Case c : { Case{ 0, 16448 }, Case{ 9, 76260 }, Case{ 10, 89240 }, Case{ 31, 1u << 24 } }) {
		Voice v = steady(16384, 0, true);
		v.lfo(c.lfof << 10);
		const uint32_t before = v.state().lfo_phase;
		v.step();
		steps_ok = steps_ok && v.state().lfo_phase - before == c.step;
	}
	check(steps_ok, "LFO: periods of the manual, LFOF 9 slower than 10");

	Voice r = steady(16384, 0, true);
	r.lfo(0x8000 | (31 << 10));
	r.step(10);
	check(r.state().lfo_phase == 0, "LFORE: the LFO held at its start");

	/* Square wave, ALFOS 7: full level in the first half, 255 steps
	 * (24 dB) down in the second */
	Voice a = steady(16384, 0, false);
	a.lfo(0x8000 | (31 << 10) | (1 << 3) | 7);
	a.step();
	a.lfo((31 << 10) | (1 << 3) | 7);
	a.step(10);
	const int32_t high = a.output();
	a.step(128);
	const int32_t low = a.output();
	check(high == 16384 && low == (16384 * 65) >> 10, "amplitude LFO: square, 24 dB at ALFOS 7");

	/* Square wave, PLFOS 7: the step one eighth faster in the first half */
	Voice p = steady(16384, 0, true);
	p.lfo(0x8000 | (31 << 10) | (1 << 8) | (7 << 5));
	p.step();
	p.lfo((31 << 10) | (1 << 8) | (7 << 5));
	const uint32_t from = p.state().position;
	p.step(64);
	check((p.state().position + 64 - from) % 64 == 72 % 64 && p.state().phase != 0,
	      "pitch LFO: square, an eighth faster at PLFOS 7");
}

/* What the sound driver does: KYONEX through the registers, MSLC, the
 * monitors, and the state */
void
test_chip()
{
	struct Host : odin::AicaHost {
		void arm_fiq(bool) override {}
		void arm_reset(bool) override {}
		void main_irq(bool) override {}
	} host;
	std::vector<uint8_t> ram(odin::Aica::RAM_SIZE);
	odin::Aica aica(host, ram);
	const uint32_t base = 5 * odin::Aica::SLOT_SIZE;

	aica.write_reg(base + 0x08, 2, 0);
	aica.write_reg(base + 0x0C, 2, 0x1000);
	aica.write_reg(base + 0x10, 2, 0x001F);
	aica.write_reg(base + 0x14, 2, 0x3C00);
	aica.write_reg(base + 0x18, 2, 0);
	aica.write_reg(base, 2, 0xC200);
	check(aica.read_reg(base, 2) == 0x4200, "KYONEX: not kept, KYONB kept");
	aica.write_reg(0x280C, 2, 5 << 8);
	aica.tick(3);
	check(aica.read_reg(0x2814, 2) == 3, "CA: the position of the slot MSLC names");
	const uint32_t eg = aica.read_reg(0x2810, 2);
	check((eg >> 13) == 0 && (eg & 0x1FFF) < 0x280, "EG monitor: attack, level going up from 0x280");

	odin::AicaState saved;
	aica.save(saved);
	aica.tick(500);
	const uint32_t later = aica.read_reg(0x2814, 2);
	aica.load(saved);
	aica.tick(500);
	check(aica.read_reg(0x2814, 2) == later, "state: a voice goes on the same after a load");

	aica.write_reg(base, 2, 0x8200);
	check(((aica.read_reg(0x2810, 2) >> 13) & 3) == 3, "KYONEX with KYONB clear: release");

	/* Byte reads: the low byte keeps LP, the high byte clears it */
	aica.write_reg(base + 0x0C, 2, 0x0010);
	aica.write_reg(base, 2, 0xC000);
	aica.tick(20);
	check((aica.read_reg(0x2810, 1) & 0xFF) == 0xFF, "end: level 0x1FFF in the low byte");
	check((aica.read_reg(0x2811, 1) & 0x80) != 0, "end: LP in the high byte");
	check((aica.read_reg(0x2811, 1) & 0x80) == 0, "LP: cleared by the read of the high byte");
}

/* The mix of a chip: one voice of a constant 16384, VOFF, through DISDL,
 * DIPAN and MVOL */
void
test_mix()
{
	struct Host : odin::AicaHost {
		void arm_fiq(bool) override {}
		void arm_reset(bool) override {}
		void main_irq(bool) override {}
	} host;
	struct Case {
		uint32_t direct, master;
		int16_t left, right;
		const char *what;
	};
	static const Case cases[] = {
		{ 0x0F00, 0xF, 16384, 16384, "mix: DISDL 15, centre, MVOL 15: as the voice" },
		{ 0x0F1F, 0xF, 16384, 0, "mix: DIPAN 0x1F, left only" },
		{ 0x0F0F, 0xF, 0, 16384, "mix: DIPAN 0x0F, right only" },
		{ 0x0F01, 0xF, 12288, 16384, "mix: DIPAN 1, the left 3 dB down (factor 3/4)" },
		{ 0x0D00, 0xF, 8192, 8192, "mix: DISDL 13, 6 dB down" },
		{ 0x0F00, 0xD, 8192, 8192, "mix: MVOL 13, 6 dB down" },
		{ 0x0000, 0xF, 0, 0, "mix: DISDL 0, nothing" },
		{ 0x0F1F, 0x800F, 16384, 16384, "mix: MONO centres DIPAN 0x1F" },
	};

	for (const Case &c : cases) {
		std::vector<uint8_t> ram(odin::Aica::RAM_SIZE);
		for (uint32_t i = 0; i < 64; i++)
			ram[0x1001 + i * 2] = 0x40;
		odin::Aica aica(host, ram);
		const uint32_t base = 9 * odin::Aica::SLOT_SIZE;

		aica.write_reg(0x2800, 2, 0x0200u | c.master);
		aica.write_reg(base + 0x04, 2, 0x1000);
		aica.write_reg(base + 0x0C, 2, 64);
		aica.write_reg(base + 0x10, 2, 0x001F);
		aica.write_reg(base + 0x14, 2, 0x3C00);
		aica.write_reg(base + 0x24, 2, c.direct);
		aica.write_reg(base + 0x28, 2, 0x0040);
		aica.write_reg(base, 2, 0xC200);
		aica.tick(10);
		check(aica.left() == c.left && aica.right() == c.right, c.what);
	}

	/* Two voices of 24576: the sum stops at 16 bits */
	std::vector<uint8_t> ram(odin::Aica::RAM_SIZE);
	for (uint32_t i = 0; i < 64; i++)
		ram[0x1001 + i * 2] = 0x60;
	odin::Aica aica(host, ram);
	aica.write_reg(0x2800, 2, 0x020F);
	for (const uint32_t slot : { 3u, 4u }) {
		const uint32_t base = slot * odin::Aica::SLOT_SIZE;
		aica.write_reg(base + 0x04, 2, 0x1000);
		aica.write_reg(base + 0x0C, 2, 64);
		aica.write_reg(base + 0x10, 2, 0x001F);
		aica.write_reg(base + 0x14, 2, 0x3C00);
		aica.write_reg(base + 0x24, 2, 0x0F00);
		aica.write_reg(base + 0x28, 2, 0x0040);
		aica.write_reg(base, 2, 0x4200);
	}
	aica.write_reg(3 * odin::Aica::SLOT_SIZE, 2, 0xC200);
	aica.tick(10);
	check(aica.left() == 0x7FFF && aica.right() == 0x7FFF, "mix: the sum held at 0x7FFF");
}

} /* namespace */

int
main()
{
	test_pitch();
	test_formats();
	test_adpcm();
	test_ends();
	test_envelope_times();
	test_envelope_rules();
	test_levels();
	test_lfo();
	test_chip();
	test_mix();
	if (failures) {
		printf("%u failure(s)\n", failures);
		return EXIT_FAILURE;
	}
	printf("aica voices: all checks passed\n");
	return EXIT_SUCCESS;
}
