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

/* SH-4 on-chip modules: the registers at FF000000 (and 1F000000), with the
 * interrupt controller, the DMA controller, the timers, port A, the
 * performance counters and the store queue addresses. A register that
 * does nothing here only keeps what is written to it.
 *
 * Addresses: SH7750 hardware manual, appendix A. The registers live in a
 * 64 KB image: bits 16-23 and 0-7 of the address. */

#include "odin/sh4.hpp"

namespace odin {

namespace {

constexpr uint32_t CCN_MMUCR = 0x000010, CCN_CCR = 0x00001C, CCN_EXPEVT = 0x000024, CCN_INTEVT = 0x000028,
	CCN_QACR0 = 0x000038, CCN_QACR1 = 0x00003C;
/* Performance counters: not in the manual, see Linux
 * arch/sh/kernel/cpu/sh4/perf_event.c (PM_CR_BASE, PM_CTR_BASE). */
constexpr uint32_t PM_PMCR1 = 0x000084, PM_PMCR2 = 0x000088, PM_PMCTR1H = 0x100004, PM_PMCTR1L = 0x100008,
	PM_PMCTR2H = 0x10000C, PM_PMCTR2L = 0x100010;
constexpr uint32_t UBC_BBRA = 0x200008, UBC_BBRB = 0x200014;
constexpr uint32_t BSC_BCR1 = 0x800000, BSC_BCR2 = 0x800004, BSC_WCR1 = 0x800008, BSC_WCR2 = 0x80000C,
	BSC_WCR3 = 0x800010, BSC_MCR = 0x800014, BSC_PCR = 0x800018, BSC_RTCSR = 0x80001C, BSC_RTCNT = 0x800020,
	BSC_RTCOR = 0x800024, BSC_RFCR = 0x800028, BSC_PCTRA = 0x80002C, BSC_PDTRA = 0x800030;
constexpr uint32_t DMAC_SAR0 = 0xA00000, DMAC_DAR0 = 0xA00004, DMAC_TCR0 = 0xA00008, DMAC_CHCR0 = 0xA0000C,
	DMAC_DMAOR = 0xA00040;
constexpr uint32_t CPG_STBCR = 0xC00004, CPG_WTCSR = 0xC0000C;
constexpr uint32_t INTC_ICR = 0xD00000, INTC_IPRA = 0xD00004, INTC_IPRB = 0xD00008, INTC_IPRC = 0xD0000C;
constexpr uint32_t TMU_TOCR = 0xD80000, TMU_TSTR = 0xD80004, TMU_TCOR0 = 0xD80008, TMU_TCNT0 = 0xD8000C,
	TMU_TCR0 = 0xD80010;
constexpr uint32_t SCI_SCBRR1 = 0xE00004, SCI_SCTDR1 = 0xE0000C, SCI_SCSSR1 = 0xE00010;
constexpr uint32_t SCIF_SCBRR2 = 0xE80004, SCIF_SCFSR2 = 0xE80010;
constexpr uint32_t UDI_SDIR = 0xF00000;

/* The three timer channels are 12 bytes apart. */
constexpr uint32_t tcor(uint32_t ch) { return TMU_TCOR0 + ch * 12; }
constexpr uint32_t tcnt(uint32_t ch) { return TMU_TCNT0 + ch * 12; }
constexpr uint32_t tcr(uint32_t ch) { return TMU_TCR0 + ch * 12; }

/* Bytes per transfer for CHCR.TS */
constexpr uint32_t DMAC_UNIT[8] = {8, 1, 2, 4, 32, 0, 0, 0};

} /* namespace */

uint64_t Sh4::reg_get(uint32_t size, uint32_t addr) const
{
	uint32_t at = shrink(addr);
	uint64_t value = 0;

	if (at + size > m_onchip.size()) {
		fatal("on-chip register read past the image", addr);
	}
	for (uint32_t i = 0; i < size; i++) {
		value |= static_cast<uint64_t>(m_onchip[at + i]) << (8 * i);
	}
	return value;
}

void Sh4::reg_put(uint32_t size, uint32_t addr, uint64_t value)
{
	uint32_t at = shrink(addr);

	if (at + size > m_onchip.size()) {
		fatal("on-chip register write past the image", addr);
	}
	for (uint32_t i = 0; i < size; i++) {
		m_onchip[at + i] = static_cast<uint8_t>(value >> (8 * i));
	}
}

void Sh4::onchip_reset(bool hard)
{
	std::fill(m_onchip.begin(), m_onchip.end(), uint8_t{0});

	reg_put(4, CCN_EXPEVT, hard ? 0 : 0x20);
	/* HEURISTIC: bit 31 alone, endian, whichever CPU it is. The master
	 * also has bit 30 set on the chip (BCR1.MASTER). */
	reg_put(4, BSC_BCR1, (m_master || m_little_endian) ? 0x80000000 : 0);
	reg_put(2, BSC_BCR2, 0x3FFC);
	reg_put(4, BSC_WCR1, 0x77777777);
	reg_put(4, BSC_WCR2, 0xFFFEEFFF);
	reg_put(4, BSC_WCR3, 0x07777777);
	for (uint32_t ch = 0; ch < 3; ch++) {
		reg_put(4, tcor(ch), 0xFFFFFFFF);
		reg_put(4, tcnt(ch), 0xFFFFFFFF);
	}
	reg_put(1, SCI_SCBRR1, 0xFF);
	reg_put(1, SCI_SCTDR1, 0xFF);
	reg_put(1, SCI_SCSSR1, 0x84);
	reg_put(1, SCIF_SCBRR2, 0xFF);
	reg_put(2, SCIF_SCFSR2, 0x0060);
	reg_put(2, UDI_SDIR, 0xFFFF);
}

/* Port A: PCTRA has two bits per pin, the low one set for an output. */

uint16_t Sh4::port_a_get()
{
	uint32_t pins = static_cast<uint32_t>(reg_get(2, BSC_PDTRA));
	uint32_t pctra = static_cast<uint32_t>(reg_get(4, BSC_PCTRA));

	outside();
	uint32_t wires = m_bus.port_a_read();

	for (uint32_t i = 0; i < 16; i++) {
		if (!((pctra >> (i * 2)) & 1)) {
			pins = (pins & ~(1u << i)) | (wires & (1u << i));
		}
	}
	return static_cast<uint16_t>(pins);
}

void Sh4::port_a_put(uint16_t value)
{
	uint32_t pins = static_cast<uint32_t>(reg_get(2, BSC_PDTRA));
	uint32_t pctra = static_cast<uint32_t>(reg_get(4, BSC_PCTRA));

	for (uint32_t i = 0; i < 16; i++) {
		if ((pctra >> (i * 2)) & 1) {
			pins = (pins & ~(1u << i)) | (value & (1u << i));
		}
	}
	outside();
	m_bus.port_a_write(static_cast<uint16_t>(pins));
}

/* Interrupt controller: the priorities of the on-chip sources (table
 * 19.5). */

void Sh4::set_irq_priority(uint32_t source, uint32_t priority)
{
	if (m_intc.source[source].raised && m_intc.source[source].priority > priority) {
		fatal("priority of a raised interrupt lowered", source);
	}
	m_intc.source[source].priority = priority;
}

void Sh4::update_irq_priorities()
{
	uint32_t ipra = static_cast<uint32_t>(reg_get(2, INTC_IPRA));
	uint32_t iprb = static_cast<uint32_t>(reg_get(2, INTC_IPRB));
	uint32_t iprc = static_cast<uint32_t>(reg_get(2, INTC_IPRC));

	set_irq_priority(Sh4Source::UDI, iprc & 15);
	set_irq_priority(Sh4Source::GPIOI, (iprc >> 12) & 15);
	for (uint32_t s = Sh4Source::DMTE0; s <= Sh4Source::DMAE; s++) {
		set_irq_priority(s, (iprc >> 8) & 15);
	}
	set_irq_priority(Sh4Source::TUNI0, (ipra >> 12) & 15);
	set_irq_priority(Sh4Source::TUNI1, (ipra >> 8) & 15);
	set_irq_priority(Sh4Source::TUNI2, (ipra >> 4) & 15);
	set_irq_priority(Sh4Source::TICPI2, (ipra >> 4) & 15);
	for (uint32_t s = Sh4Source::ATI; s <= Sh4Source::CUI; s++) {
		set_irq_priority(s, ipra & 15);
	}
	for (uint32_t s = Sh4Source::ERI; s <= Sh4Source::TEI; s++) {
		set_irq_priority(s, (iprb >> 4) & 15);
	}
	for (uint32_t s = Sh4Source::ERIF; s <= Sh4Source::TXIF; s++) {
		set_irq_priority(s, (iprc >> 4) & 15);
	}
	set_irq_priority(Sh4Source::ITI, (iprb >> 12) & 15);
	set_irq_priority(Sh4Source::RCMI, (iprb >> 8) & 15);
	set_irq_priority(Sh4Source::ROVI, (iprb >> 8) & 15);

	update_irqs();
}

/* DMA controller. HEURISTIC: a channel that may run moves its whole count
 * at once, within the write that starts it. DDT mode, the request source
 * and the channel priorities are not looked at. */

uint64_t Sh4::dmac_get(uint32_t addr, uint32_t size)
{
	const uint8_t *p = m_rd[addr >> PAGE_BITS];
	uint64_t value = 0;

	if (p && !(addr & (size - 1))) {
		std::memcpy(&value, p + (addr & (PAGE_SIZE - 1)), size);
		return value;
	}
	m_touched = true;
	if (!read_any(addr, size, value)) {
		value = 0;
		m_bus.unanswered(false, addr, size, 0);
	}
	return value;
}

void Sh4::dmac_put(uint32_t addr, uint32_t size, uint64_t value)
{
	uint8_t *p = m_wr[addr >> PAGE_BITS];

	m_touched = true;
	if (p && !(addr & (size - 1))) {
		std::memcpy(p + (addr & (PAGE_SIZE - 1)), &value, size);
		return;
	}
	if (!write_any(addr, size, value)) {
		m_bus.unanswered(true, addr, size, value);
	}
}

void Sh4::dmac_run_channel(uint32_t ch)
{
	uint32_t offs = ch * 0x10;

	/* It writes the memory behind the back of the blocks. */
	outside();

	uint32_t sar = static_cast<uint32_t>(reg_get(4, DMAC_SAR0 + offs));
	uint32_t dar = static_cast<uint32_t>(reg_get(4, DMAC_DAR0 + offs));
	uint32_t tcr = static_cast<uint32_t>(reg_get(4, DMAC_TCR0 + offs));
	uint32_t chcr = static_cast<uint32_t>(reg_get(4, DMAC_CHCR0 + offs));
	uint32_t unit = DMAC_UNIT[(chcr >> 4) & 7];
	uint32_t sm = (chcr >> 12) & 3, dm = (chcr >> 14) & 3;

	if (unit == 0) {
		fatal("DMAC transfer size", chcr);
	}
	for (uint32_t budget = 0x7FFFFFFF; budget > 0 && tcr > 0; budget--, tcr--) {
		if (sm == 2) {
			sar -= unit;
		}
		if (dm == 2) {
			dar -= unit;
		}
		if (unit == 32) {
			for (uint32_t i = 0; i < 32; i += 8) {
				dmac_put(dar + i, 8, dmac_get(sar + i, 8));
			}
		} else {
			dmac_put(dar, unit, dmac_get(sar, unit));
		}
		if (sm == 1) {
			sar += unit;
		}
		if (dm == 1) {
			dar += unit;
		}
	}

	reg_put(4, DMAC_SAR0 + offs, sar);
	reg_put(4, DMAC_DAR0 + offs, dar);
	reg_put(4, DMAC_TCR0 + offs, tcr);

	if (tcr == 0) {
		chcr |= 2;	/* TE */
		if (chcr & 4) {	/* IE */
			set_irq(Sh4Source::DMTE0 + ch, true);
		}
		m_dmac_running[ch] = false;
		reg_put(4, DMAC_CHCR0 + offs, chcr);
	}
}

void Sh4::dmac_run()
{
	for (uint32_t ch = 0; ch < 4; ch++) {
		if (m_dmac_running[ch]) {
			dmac_run_channel(ch);
		}
	}
}

/* A channel runs when DMAOR.DME and CHCR.DE are set, no error or end flag
 * is, and its request source is of the kind asked: 1 for auto-request. */
void Sh4::dmac_update_channel(uint32_t ch, uint32_t request)
{
	uint32_t offs = ch * 0x10;
	uint32_t dmaor = static_cast<uint32_t>(reg_get(4, DMAC_DMAOR));
	uint32_t chcr = static_cast<uint32_t>(reg_get(4, DMAC_CHCR0 + offs));

	m_dmac_running[ch] = false;
	if (!(dmaor & chcr & 1)) {
		return;
	}

	uint32_t sar = static_cast<uint32_t>(reg_get(4, DMAC_SAR0 + offs));
	uint32_t dar = static_cast<uint32_t>(reg_get(4, DMAC_DAR0 + offs));
	uint32_t ts = (chcr >> 4) & 7, rs = (chcr >> 8) & 15;
	uint32_t sm = (chcr >> 12) & 3, dm = (chcr >> 14) & 3;

	if (ts >= 5 || sm == 3 || dm == 3 || rs == 1 || rs == 7 || rs == 15) {
		fatal("DMAC channel setting", chcr);
	}
	if ((sar | dar) & (DMAC_UNIT[ts] - 1)) {
		set_irq(Sh4Source::DMAE, true);
		return;
	}
	/* NMIF, AE or TE */
	if ((dmaor & 6) || (chcr & 2)) {
		return;
	}
	if ((rs >> 2) == request) {
		m_dmac_running[ch] = true;
	}
}

void Sh4::dmac_update(uint32_t request)
{
	for (uint32_t ch = 0; ch < 4; ch++) {
		dmac_update_channel(ch, request);
	}
}

/* Timers: a channel counts down; at zero it sets TCR.UNF, takes TCOR
 * again and, with TCR.UNIE, raises its interrupt. The zero is seen one
 * cycle before the count goes on from TCOR. */

void Sh4::tmu_run_channel(uint32_t ch, uint32_t cycles)
{
	uint32_t counter = m_tmu_counter[ch];

	while (cycles > 0) {
		if (counter >= cycles) {
			counter -= cycles;
			break;
		}
		cycles -= counter + 1;

		uint32_t control = static_cast<uint32_t>(reg_get(2, tcr(ch)));
		reg_put(2, tcr(ch), control | 0x100);
		counter = static_cast<uint32_t>(reg_get(4, tcor(ch))) - 1;
		if (control & 0x20) {
			set_irq(Sh4Source::TUNI0 + ch, true);
		}
	}
	m_tmu_counter[ch] = counter;
}

void Sh4::tmu_run(int32_t cycles)
{
	for (uint32_t ch = 0; ch < 3; ch++) {
		if (m_tmu_running[ch] && cycles > 0) {
			tmu_run_channel(ch, static_cast<uint32_t>(cycles));
		}
	}
}

/* Store queues: bits 2-4 of QACR0 or QACR1 are the area of the target. */
uint32_t Sh4::store_queue_addr(uint32_t addr) const
{
	uint32_t qacr = static_cast<uint32_t>(reg_get(4, (addr & 0x20) ? CCN_QACR1 : CCN_QACR0));

	return ((qacr & 0x1C) << 24) | (addr & 0x03FFFFE0);
}

bool Sh4::onchip_read(uint32_t addr, uint32_t size, uint64_t &value)
{
	uint32_t reg = addr & 0xFFFFFF;
	auto sized = [&](uint32_t expected) {
		if (size != expected) {
			fatal("on-chip register read with the wrong size", addr);
		}
	};

	value = reg_get(size, addr);

	if (reg >= DMAC_SAR0 && reg <= DMAC_DMAOR) {
		sized(4);
		return true;
	}
	switch (reg) {
	/* HEURISTIC: RFCR and WTCSR also read as port A. */
	case BSC_RFCR:
	case BSC_PDTRA:
	case CPG_WTCSR:
		sized(2);
		value = port_a_get();
		return true;
	case CCN_CCR:
	case CCN_INTEVT:
	case BSC_PCTRA:
		sized(4);
		return true;
	case INTC_IPRA:
	case INTC_IPRB:
	case INTC_IPRC:
	case PM_PMCR1:
	case PM_PMCR2:
		sized(2);
		return true;
	case TMU_TSTR:
		sized(1);
		return true;
	case TMU_TCNT0:
		sized(4);
		value = m_tmu_counter[0];
		return true;
	case PM_PMCTR1H:
		value = (m_pm_counter[0] >> 32) & 0xFFFF;
		return true;
	case PM_PMCTR1L:
		value = m_pm_counter[0] & 0xFFFFFFFF;
		return true;
	case PM_PMCTR2H:
		value = (m_pm_counter[1] >> 32) & 0xFFFF;
		return true;
	case PM_PMCTR2L:
		value = m_pm_counter[1] & 0xFFFFFFFF;
		return true;
	default:
		return false;
	}
}

bool Sh4::onchip_write(uint32_t addr, uint32_t size, uint64_t value)
{
	uint32_t reg = addr & 0xFFFFFF;
	uint32_t v = static_cast<uint32_t>(value);
	auto sized = [&](uint32_t expected) {
		if (size != expected) {
			fatal("on-chip register write with the wrong size", addr);
		}
	};
	auto only = [&](uint32_t allowed) {
		if (v & ~allowed) {
			fatal("on-chip register write with bits that are not implemented", addr);
		}
	};

	/* SDMR2 and SDMR3: the SDRAM mode is in the address. */
	if ((reg >= 0x900000 && reg <= 0x90FFFF) || (reg >= 0x940000 && reg <= 0x94FFFF)) {
		sized(1);
		reg_put(size, addr, value);
		return true;
	}

	switch (reg) {
	case CPG_STBCR:
		sized(1);
		break;
	case BSC_BCR2:
	case BSC_PCR:
	case BSC_RTCSR:
	case BSC_RTCNT:
	case BSC_RTCOR:
	case BSC_RFCR:
	case CPG_WTCSR:
	case UBC_BBRA:
	case UBC_BBRB:
		sized(2);
		break;
	case BSC_PDTRA:
		sized(2);
		port_a_put(static_cast<uint16_t>(v));
		break;
	case CCN_MMUCR:
	case CCN_CCR:
	case CCN_QACR0:
	case CCN_QACR1:
	case BSC_BCR1:
	case BSC_WCR1:
	case BSC_WCR2:
	case BSC_WCR3:
	case BSC_MCR:
	case BSC_PCTRA:
		sized(4);
		break;

	case INTC_ICR: {
		uint32_t old = static_cast<uint32_t>(reg_get(2, addr));
		sized(2);
		only(0xC380);
		/* NMIL is read only */
		reg_put(2, addr, (old & 0x8000) | (v & 0x7FFF));
		update_irqs();
		return true;
	}
	case INTC_IPRA:
	case INTC_IPRC:
		sized(2);
		reg_put(2, addr, v);
		update_irq_priorities();
		return true;
	case INTC_IPRB:
		sized(2);
		only(0xFFF0);
		reg_put(2, addr, v);
		update_irq_priorities();
		return true;

	case DMAC_SAR0: case DMAC_SAR0 + 0x10: case DMAC_SAR0 + 0x20: case DMAC_SAR0 + 0x30:
	case DMAC_DAR0: case DMAC_DAR0 + 0x10: case DMAC_DAR0 + 0x20: case DMAC_DAR0 + 0x30:
	case DMAC_TCR0: case DMAC_TCR0 + 0x10: case DMAC_TCR0 + 0x20: case DMAC_TCR0 + 0x30: {
		uint32_t ch = (addr >> 4) & 3;
		/* Channel 0 is not written in DDT mode. */
		if ((reg_get(4, DMAC_DMAOR) & 0x8000) && ch == 0) {
			return true;
		}
		sized(4);
		if (m_dmac_running[ch]) {
			fatal("DMAC register written while the channel runs", addr);
		}
		break;
	}
	case DMAC_CHCR0: case DMAC_CHCR0 + 0x10: case DMAC_CHCR0 + 0x20: case DMAC_CHCR0 + 0x30: {
		uint32_t ch = (addr >> 4) & 3;
		uint32_t old = static_cast<uint32_t>(reg_get(size, addr));
		if ((reg_get(4, DMAC_DMAOR) & 0x8000) && ch == 0) {
			return true;
		}
		sized(4);
		only(ch < 2 ? ~0x00F00008u : ~0x00F50008u);
		/* TE can only be cleared */
		reg_put(size, addr, (v & ~2u) | (old & v & 2));
		dmac_update_channel(ch, 1);
		dmac_run();
		return true;
	}
	case DMAC_DMAOR: {
		uint32_t old = static_cast<uint32_t>(reg_get(size, addr));
		uint32_t nmil = (reg_get(2, INTC_ICR) & 0x8000) ? 1 : 0;
		sized(4);
		only(0x00008307);
		/* AE and NMIF can only be cleared, and NMIF stays while NMI is
		 * raised. */
		reg_put(size, addr, (v & ~6u) | (old & v & 6) | (nmil << 1));
		dmac_update(1);
		dmac_run();
		return true;
	}

	/* PMCLR (bit 13) clears the counter and reads back clear; PMEN (15)
	 * and PMST (14) together run it. */
	case PM_PMCR1:
	case PM_PMCR2: {
		uint32_t n = (reg == PM_PMCR1) ? 0 : 1;
		sized(2);
		if (v & 0x2000) {
			m_pm_counter[n] = 0;
		}
		m_pm_running[n] = (v & 0xC000) == 0xC000;
		reg_put(size, addr, v & ~0x2000u);
		return true;
	}
	case PM_PMCTR1H:
	case PM_PMCTR1L:
	case PM_PMCTR2H:
	case PM_PMCTR2L:
		return true;

	case TMU_TOCR:
		sized(1);
		only(0x01);
		break;
	case TMU_TSTR:
		sized(1);
		only(0x07);
		reg_put(size, addr, v);
		for (uint32_t ch = 0; ch < 3; ch++) {
			m_tmu_running[ch] = (v >> ch) & 1;
		}
		return true;
	case TMU_TCR0:
	case TMU_TCR0 + 12: {
		uint32_t old = static_cast<uint32_t>(reg_get(size, addr));
		sized(2);
		only(0x013F);
		/* UNF can only be cleared */
		reg_put(size, addr, (v & 0x00FF) | (old & v & 0x0100));
		return true;
	}
	case TMU_TCR0 + 24: {
		uint32_t old = static_cast<uint32_t>(reg_get(size, addr));
		sized(2);
		/* Input capture is not implemented. */
		only(0x037F);
		reg_put(size, addr, (v & 0x00FF) | (old & v & 0x0300));
		return true;
	}
	case TMU_TCOR0:
	case TMU_TCOR0 + 12:
	case TMU_TCOR0 + 24:
		sized(4);
		break;
	case TMU_TCNT0:
	case TMU_TCNT0 + 12:
	case TMU_TCNT0 + 24:
		sized(4);
		m_tmu_counter[(reg - TMU_TCNT0) / 12] = v;
		return true;

	default:
		return false;
	}

	reg_put(size, addr, value);
	return true;
}

} /* namespace odin */
