/*
 *  ██████╗ ██████╗ ██╗███╗   ██╗
 * ██╔═══██╗██╔══██╗██║████╗  ██║
 * ██║   ██║██║  ██║██║██╔██╗ ██║
 * ██║   ██║██║  ██║██║██║╚██╗██║
 * ╚██████╔╝██████╔╝██║██║ ╚████║
 *  ╚═════╝ ╚═════╝ ╚═╝╚═╝  ╚═══╝
 *
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

/* The effect processor of the AICA on small programs of known results:
 * a send through a coefficient, an empty step, the memory read two steps
 * later, the odd steps, EFREG set twice, the 16-bit floats of the ring
 * buffer (values of yam.c's pair), the runs of empty steps, the ring
 * counter, and the mix of the outputs and of the external inputs.
 *
 * Build: g++ -std=c++20 -Wall -Wextra -Wpedantic -Wconversion -Wshadow \
 *            -O2 -I src tests/aica/aica_dsp_test.cpp src/odin/machine/chips/aica/aica.cpp \
 *            src/odin/machine/chips/aica/aica_voice.cpp src/odin/machine/chips/aica/aica_dsp.cpp -o aica_dsp_test
 */

#include "odin/machine/chips/aica/aica.hpp"
#include "odin/machine/chips/aica/aica_dsp.hpp"

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

/* A register file, a wave memory and the DSP */
struct Dsp {
	std::vector<uint8_t> regs = std::vector<uint8_t>(0x8000);
	std::vector<uint8_t> ram = std::vector<uint8_t>(0x10000);
	odin::AicaDsp dsp;

	void set(uint32_t offset, uint32_t value)
	{
		regs[offset] = static_cast<uint8_t>(value);
		regs[offset + 1] = static_cast<uint8_t>(value >> 8);
	}
	void step(uint32_t i, uint32_t w0, uint32_t w1, uint32_t w2, uint32_t w3)
	{
		set(0x3400 + i * 16, w0);
		set(0x3400 + i * 16 + 4, w1);
		set(0x3400 + i * 16 + 8, w2);
		set(0x3400 + i * 16 + 12, w3);
		dsp.program_written(regs.data());
	}
	void run() { dsp.run(regs.data(), odin::WaveMemory(ram.data(), 0xFFFF, {})); }
	uint32_t word(uint32_t byte) const { return static_cast<uint32_t>(ram[byte] | (ram[byte + 1] << 8)); }
	void put(uint32_t byte, uint32_t value)
	{
		ram[byte] = static_cast<uint8_t>(value);
		ram[byte + 1] = static_cast<uint8_t>(value >> 8);
	}
	/* A 24-bit register of TEMP or MEMS, read through its two words */
	int32_t value(uint32_t offset) const
	{
		const uint32_t v = (static_cast<uint32_t>(dsp.read(offset + 4)) << 8) | dsp.read(offset);
		return static_cast<int32_t>(v << 8) >> 8;
	}
};

/* The fields of a step */
enum : uint32_t {
	TWT = 0x0100,
	XSEL = 0x8000, YSEL_COEF = 0x2000, IWT = 0x0040,
	TABLE = 0x8000, MWT = 0x4000, MRD = 0x2000, EWT = 0x1000, ZERO = 0x0002,
	NOFL = 0x8000
};

uint32_t ira(uint32_t n) { return n << 7; }
uint32_t iwa(uint32_t n) { return n << 1; }
uint32_t ewa(uint32_t n) { return n << 8; }
uint32_t masa(uint32_t n) { return n << 9; }

void
test_steps()
{
	/* MIXS 0 times COEF 0x4000 (one half), then to EFREG 0 */
	Dsp a;
	a.set(0x3000, 0x4000);
	a.step(0, 0, XSEL | YSEL_COEF | ira(0x20), ZERO, 0);
	a.step(1, 0, 0, EWT | ewa(0), 0);
	a.dsp.send(0, 262144);
	a.run();
	check(a.dsp.effect(0) == 8192, "a send halved by COEF, into EFREG a step later");

	/* An empty step still adds TEMP times FRC to TEMP */
	Dsp e;
	e.dsp.write(0x4000, 0x00);
	e.dsp.write(0x4004, 0x1234);
	e.step(3, 0, 0, EWT | ewa(1), 0);
	e.run();
	check(e.dsp.effect(1) == 0x1234, "an empty step computes TEMP");

	/* A read at step 1 reaches MEMS through the input write of step 3,
	 * not that of step 2; at step 0 (even) it does not happen */
	Dsp r;
	r.set(0x3200, 0x10);
	r.put(0x20, 0x1234);
	r.step(1, 0, 0, MRD, NOFL | masa(0));
	r.step(2, 0, IWT | iwa(6), 0, 0);
	r.step(3, 0, IWT | iwa(5), 0, 0);
	r.run();
	check(r.value(0x4400 + 5 * 8) == 0x123400, "memory read: in MEMS two steps later");
	check(r.value(0x4400 + 6 * 8) == 0, "memory read: not yet one step later");

	Dsp o;
	o.set(0x3200, 0x10);
	o.put(0x20, 0x1234);
	o.step(0, 0, 0, MRD, NOFL | masa(0));
	o.step(2, 0, IWT | iwa(5), 0, 0);
	o.run();
	check(o.value(0x4400 + 5 * 8) == 0, "memory read: none at an even step");

	/* EFREG is set, not added to */
	Dsp t;
	t.dsp.write(0x4004, 0x0100);
	t.step(1, 0, 0, EWT | ewa(2), 0);
	t.step(2, 0, 0, ZERO, 0);
	t.step(3, 0, 0, EWT | ewa(2), 0);
	t.run();
	check(t.dsp.effect(2) == 0, "EFREG: the last write of the period");
}

void
test_floats()
{
	/* TEMP 12345 to the memory as a float, then -12348 back from it */
	Dsp w;
	w.set(0x3200 + 4, 0x40);
	w.dsp.write(0x4000, 12345 & 0xFF);
	w.dsp.write(0x4004, 12345 >> 8);
	w.step(1, 0, 0, MWT, masa(1));
	w.run();
	check(w.word(0x80) == 0x4C0E, "float: 12345 written as 0x4C0E");

	Dsp r;
	r.set(0x3200 + 8, 0x30);
	r.put(0x60, 0xCBF1);
	r.step(1, 0, 0, MRD, masa(2));
	r.step(3, 0, IWT | iwa(1), 0, 0);
	r.run();
	check(r.value(0x4400 + 8) == -12348, "float: 0xCBF1 read as -12348");

	Dsp n;
	n.set(0x3200 + 4, 0x40);
	n.dsp.write(0x4004, 0x1234);
	n.step(1, 0, 0, MWT, NOFL | masa(1));
	n.run();
	check(n.word(0x80) == 0x1234, "NOFL: the high 16 bits as they are");
}

/* Only the steps that do something run, with one of each run of empty
 * steps: a step keeps its number, and the ACC of the empty steps at the
 * end reaches the first step of the next period */
void
test_empty_runs()
{
	/* COEF 9 for step 9, after nine empty steps */
	Dsp c;
	c.set(0x3000 + 9 * 4, 0x4000);
	c.step(9, 0, XSEL | YSEL_COEF | ira(0x20), ZERO, 0);
	c.step(10, 0, 0, EWT | ewa(0), 0);
	c.dsp.send(0, 262144);
	c.run();
	check(c.dsp.effect(0) == 8192, "a step after empty ones keeps its number");

	/* Step 0 writes the ACC of the period before, that of its empty steps */
	Dsp e;
	e.dsp.write(0x4004, 0x1234);
	e.step(0, 0, 0, EWT | ewa(2) | ZERO, 0);
	e.run();
	e.run();
	check(e.dsp.effect(2) == 0x1234, "the empty steps at the end set ACC for the next period");

	/* A state goes with the program of its registers */
	Dsp l;
	l.regs = c.regs;
	l.dsp.load(c.dsp.state(), l.regs.data());
	l.dsp.send(0, 131072);
	l.run();
	check(l.dsp.effect(0) == 4096, "a loaded state runs the program of its registers");
}

/* The ring counter goes down one a period: the ring buffer and TEMP are
 * reached through it, a table is not */
void
test_counter()
{
	Dsp d;
	d.set(0x3200, 0x10);
	d.put(0x20, 0x1111);
	d.put(0x1E, 0x2222);
	d.step(1, 0, 0, MRD, NOFL | masa(0));
	d.step(3, 0, IWT | iwa(0), 0, 0);
	d.step(5, 0, 0, MRD | TABLE, NOFL | masa(0));
	d.step(7, 0, IWT | iwa(1), 0, 0);
	d.run();
	d.run();
	check(d.value(0x4400) == 0x222200, "ring: one word lower in the second period");
	check(d.value(0x4400 + 8) == 0x111100, "table: the same word");
}

void
test_mix()
{
	struct Host : odin::AicaHost {
		void arm_fiq(bool) override {}
		void arm_reset(bool) override {}
		void main_irq(bool) override {}
	} host;
	std::vector<uint8_t> ram(odin::Aica::RAM_SIZE);
	odin::Aica aica(host, ram);

	/* EXTS 0 to the left, EXTS 1 to the right */
	aica.write_reg(0x2800, 2, 0x020F);
	aica.write_reg(0x2040, 2, 0x0F1F);
	aica.write_reg(0x2044, 2, 0x0F0F);
	aica.set_external(8192, -4096);
	aica.tick(1);
	check(aica.left() == 8192 && aica.right() == -4096, "EXTS: through EFSDL 16 and 17");
	check(aica.read_reg(0x45C0, 2) == 8192, "EXTS: read back");

	/* A voice of 16384 sent by IMXL 13 (-6 dB) to MIXS 3, a program that
	 * copies MIXS 3 to EFREG 4 at half level, EFSDL 4 centred; no direct
	 * send */
	for (uint32_t i = 0; i < 64; i++)
		ram[0x1001 + i * 2] = 0x40;
	aica.write_reg(0x2040, 2, 0);
	aica.write_reg(0x2044, 2, 0);
	aica.write_reg(0x2010, 2, 0x0F00);
	aica.write_reg(0x3000, 2, 0x4000);
	aica.write_reg(0x3400 + 4, 2, XSEL | YSEL_COEF | ira(0x23));
	aica.write_reg(0x3400 + 8, 2, ZERO);
	aica.write_reg(0x3410 + 8, 2, EWT | ewa(4));
	const uint32_t base = 2 * odin::Aica::SLOT_SIZE;
	aica.write_reg(base + 0x04, 2, 0x1000);
	aica.write_reg(base + 0x0C, 2, 64);
	aica.write_reg(base + 0x10, 2, 0x001F);
	aica.write_reg(base + 0x14, 2, 0x3C00);
	aica.write_reg(base + 0x20, 2, 0x00D3);
	aica.write_reg(base + 0x24, 2, 0x0000);
	aica.write_reg(base + 0x28, 2, 0x0060);
	aica.write_reg(base, 2, 0xC200);
	aica.tick(10);
	check(aica.left() == 4096 && aica.right() == 4096, "a voice through IMXL, MIXS, the program and EFSDL");
}

} /* namespace */

int
main()
{
	test_steps();
	test_floats();
	test_empty_runs();
	test_counter();
	test_mix();
	if (failures) {
		printf("%u failure(s)\n", failures);
		return EXIT_FAILURE;
	}
	printf("aica dsp: all checks passed\n");
	return EXIT_SUCCESS;
}
