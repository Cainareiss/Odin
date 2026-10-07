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

/* Register-level checks of the AICA control block against documented
 * sequences: the KallistiOS ARM driver init (timers, SCILV, SCIEB), the
 * NetBSD ARM driver CPU-to-CPU interrupt, the MAME DMA and RTC models,
 * and the ARM reset bit as the AM2 driver upload uses it. */

#include "odin/aica.hpp"

#include <cstdio>
#include <cstdlib>
#include <vector>

namespace {

struct Host : odin::AicaHost {
	bool fiq = false;
	bool reset = true;
	bool irq = false;
	uint32_t fiq_edges = 0;
	void arm_fiq(bool level) override { if (level && !fiq) fiq_edges++; fiq = level; }
	void arm_reset(bool level) override { reset = level; }
	void main_irq(bool level) override { irq = level; }
};

uint32_t failures = 0;

void
check(bool ok, const char *what)
{
	if (!ok) {
		failures++;
		printf("FAIL: %s\n", what);
	}
}

void
test_timer_a_fiq()
{
	Host host;
	std::vector<uint8_t> ram(odin::Aica::RAM_SIZE);
	odin::Aica aica(host, ram);

	/* KallistiOS kernel/arch/dreamcast/sound/arm/aica.c init. */
	aica.write_reg(0x289C, 4, 0);
	aica.write_reg(0x28B4, 4, 0);
	aica.write_reg(0x28A4, 4, 0x7FF);
	aica.write_reg(0x28BC, 4, 0x7FF);
	aica.write_reg(0x28A8, 4, 0x18);
	aica.write_reg(0x28AC, 4, 0x50);
	aica.write_reg(0x28B0, 4, 0x08);
	aica.write_reg(0x2890, 4, 256 - (44100 / 4410));
	aica.write_reg(0x289C, 4, 0x40);

	aica.tick(9);
	check(!host.fiq, "timer A: no FIQ before overflow");
	check(aica.read_reg(0x2890, 4) == 0xFF, "timer A: count reads back 0xFF after 9 samples");
	aica.tick(1);
	check(host.fiq, "timer A: FIQ on overflow after 10 samples");
	check((aica.read_reg(0x28A0, 4) & 0x40) != 0, "timer A: SCIPD bit 6 set");
	check((aica.read_reg(0x28B8, 4) & 0x40) != 0, "timer A: MCIPD bit 6 set too");
	check(!host.irq, "timer A: no host IRQ while MCIEB is 0");
	check(aica.read_reg(0x2D00, 4) == 2, "timer A: level from SCILV1 bit 6");

	aica.write_reg(0x28A4, 4, 0x40);
	check((aica.read_reg(0x28A0, 4) & 0x40) == 0, "SCIRE clears SCIPD bit 6");
	check(host.fiq, "FIQ stays until the ARM acknowledges");
	aica.write_reg(0x2D04, 4, 1);
	check(!host.fiq, "write 1 to 0x2D04 drops FIQ");
	check(host.fiq_edges == 1, "one FIQ edge so far");

	/* Prescaler 2: one increment every 4 samples, 256 steps to wrap
	 * from 0. */
	aica.write_reg(0x2890, 4, 0x0200);
	aica.tick(4 * 256 - 1);
	check(!host.fiq, "prescaler: no FIQ before 1024 samples");
	aica.tick(1);
	check(host.fiq, "prescaler: FIQ at 1024 samples");
}

void
test_cpu_interrupt()
{
	Host host;
	std::vector<uint8_t> ram(odin::Aica::RAM_SIZE);
	odin::Aica aica(host, ram);

	/* NetBSD aica_arm.c: ARM enables MCIEB bit 5 then writes MCIPD bit 5
	 * to interrupt the SH-4. */
	aica.write_reg(0x28B4, 4, 0x20);
	check(!host.irq, "MCIEB alone raises nothing");
	aica.write_reg(0x28B8, 4, 0x20);
	check(host.irq, "MCIPD bit 5 raises the host IRQ");
	check((aica.read_reg(0x28B8, 4) & 0x20) != 0, "MCIPD reads back the bit");
	aica.write_reg(0x28BC, 4, 0x20);
	check(!host.irq, "MCIRE drops the host IRQ");

	/* The other direction: host writes SCIPD bit 5 for the ARM. */
	aica.write_reg(0x289C, 4, 0x20);
	aica.write_reg(0x28A0, 4, 0x20);
	check(host.fiq, "SCIPD bit 5 raises FIQ");
	aica.write_reg(0x28A0, 4, 0x40);
	check((aica.read_reg(0x28A0, 4) & 0x40) == 0, "other SCIPD bits are read-only");
}

void
test_arm_reset_and_ram()
{
	Host host;
	std::vector<uint8_t> ram(odin::Aica::RAM_SIZE);
	odin::Aica aica(host, ram);

	check(host.reset, "ARM held in reset after chip reset");
	check(aica.arm_in_reset(), "arm_in_reset agrees");
	aica.write_reg(0x2800, 4, 0x0200);
	check(aica.ram_mask() == 0x7FFFFF, "MEM8MB selects 8 MB");
	aica.write_reg(0x2C00, 4, 1);
	check(host.reset, "0x2C00 bit 0 = 1 keeps reset");
	aica.write_ram(0x00000000, 4, 0xEA000142);
	aica.write_ram(0x007FFFFC, 2, 0xBEEF);
	aica.write_reg(0x2C00, 4, 0);
	check(!host.reset, "0x2C00 bit 0 = 0 releases the ARM");
	check(aica.read_ram(0, 4) == 0xEA000142, "RAM word read back");
	check(aica.read_ram(0x007FFFFC, 2) == 0xBEEF, "RAM top halfword");
	check(aica.read_ram(0x00FFFFFC, 2) == 0xBEEF, "RAM mirrors above 8 MB");
	check(aica.read_reg(0x2C00, 4) == 0, "0x2C00 reads 0 once released");
}

void
test_dma()
{
	Host host;
	std::vector<uint8_t> ram(odin::Aica::RAM_SIZE);
	odin::Aica aica(host, ram);

	/* Registers to memory: slot 0 registers 0x00..0x1C (8 words) go to
	 * wave memory 0x1000 (MAME exec_dma, DDIR = 1). */
	for (uint32_t i = 0; i < 8; i++)
		aica.write_reg(i * 4, 4, 0x1000 + i);
	aica.write_reg(0x2880, 4, 0x0000);
	aica.write_reg(0x2884, 4, 0x1000);
	aica.write_reg(0x2888, 4, 0x0000);
	aica.write_reg(0x28B4, 4, 0x10);
	aica.write_reg(0x288C, 4, 0x8000 | 16 | 1);
	check(aica.read_ram(0x1000, 2) == 0x1000, "DMA word 0");
	check(aica.read_ram(0x101C, 2) == 0x1007, "DMA word 7");
	check((aica.read_reg(0x288C, 4) & 1) == 0, "DEXE cleared at the end");
	check(host.irq, "DMA end interrupts the host when MCIEB bit 4 is set");
}

void
test_rtc()
{
	Host host;
	std::vector<uint8_t> ram(odin::Aica::RAM_SIZE);
	odin::Aica aica(host, ram);

	aica.write_rtc(0, 0x1234);
	check(aica.read_rtc(0) != 0x1234, "RTC ignores writes without enable");
	aica.write_rtc(8, 1);
	aica.write_rtc(0, 0x1234);
	aica.write_rtc(8, 1);
	aica.write_rtc(4, 0xFFFF);
	check(aica.read_rtc(0) == 0x1234 && aica.read_rtc(4) == 0xFFFF, "RTC set through write enable");
	aica.tick(44100);
	check(aica.read_rtc(0) == 0x1235 && aica.read_rtc(4) == 0x0000, "RTC carries after one second");
}

void
test_state()
{
	Host host;
	std::vector<uint8_t> ram(odin::Aica::RAM_SIZE);
	odin::Aica aica(host, ram);

	aica.write_reg(0x2890, 4, 0x0310);
	aica.tick(5);
	odin::AicaState a;
	aica.save(a);
	aica.tick(100);
	aica.load(a);
	odin::AicaState b;
	aica.save(b);
	check(a.timer_step == b.timer_step && a.timer_count == b.timer_count && a.regs == b.regs,
	      "save/load restores the timers and registers");
}

} /* namespace */

int
main()
{
	test_timer_a_fiq();
	test_cpu_interrupt();
	test_arm_reset_and_ram();
	test_dma();
	test_rtc();
	test_state();
	if (failures) {
		printf("%u failure(s)\n", failures);
		return EXIT_FAILURE;
	}
	printf("aica: all checks passed\n");
	return EXIT_SUCCESS;
}
