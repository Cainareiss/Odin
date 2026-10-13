/*
 * Odin
 * Copyright (C) 2026 Réda Chérif-Touil
 *
 * Valkyrie
 * Copyright (C) 2011, 2012, Stefano Teso
 *
 * Valkyrie is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * Valkyrie is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with Valkyrie.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "odin/mie_stand_in.hpp"

#include <algorithm>
#include <cstring>

namespace odin {

namespace {

/* Where the Z80 addresses fall in the RAM */
constexpr uint32_t C000 = 0x4000;
constexpr uint32_t C001 = 0x4001;
constexpr uint32_t C002 = 0x4002;
constexpr uint32_t C003 = 0x4003;
constexpr uint32_t C004 = 0x4004;
constexpr uint32_t C005 = 0x4005;
constexpr uint32_t C100 = 0x4100;
constexpr uint32_t E000 = 0x6000;
constexpr uint32_t E100 = 0x6100;
constexpr uint32_t E200 = 0x6200;

} /* namespace */

MieStandIn::MieStandIn(std::span<uint8_t> ram)
	: m_ram(ram), m_regs{}, m_busrq(0)
{
}

void
MieStandIn::reset()
{
	m_regs.fill(0);
	set_reg(0x00, 0xFFFF);
	m_busrq = 0;
}

uint16_t
MieStandIn::reg(uint32_t offs) const
{
	uint16_t value;

	std::memcpy(&value, &m_regs[offs], 2);
	return value;
}

void
MieStandIn::set_reg(uint32_t offs, uint16_t value)
{
	std::memcpy(&m_regs[offs], &value, 2);
}

void
MieStandIn::write_switches(bool test, bool service)
{
	m_ram[C005] = static_cast<uint8_t>((test ? 0x40 : 0) | (service ? 0x80 : 0));
}

/* The pages of the inputs: two players of two bytes, two coin slots, eight
 * analog channels at their centre. */
void
MieStandIn::write_pages()
{
	static const uint8_t e000[8] = { 1, 1, 6, 0, 0, 0, 0, 0 };
	static const uint8_t e100[7] = { 1, 1, 5, 0, 0, 0, 0 };
	uint8_t e200[19] = { 1, 1, 17 };

	for (unsigned i = 0; i < 8; i++) {
		e200[3 + 2 * i] = 0x80;
		e200[4 + 2 * i] = 0x00;
	}
	std::copy(std::begin(e000), std::end(e000), &m_ram[E000]);
	std::copy(std::begin(e100), std::end(e100), &m_ram[E100]);
	std::copy(std::begin(e200), std::end(e200), &m_ram[E200]);
}

void
MieStandIn::write_node_record()
{
	static const uint8_t record[12] = {
		0x01,			/* present */
		0x11, 0x20, 0x10,	/* command, JVS and comm versions of the 837-13551 (MAME jvs13551.cpp) */
		0x02, 0x02, 0x0D, 0x02,	/* players, players, bits, bytes per player */
		0x02, 0x02,		/* coin slots */
		0x08, 0x08		/* analog channels */
	};
	static const char id[] = "SEGA ENTERPRISES,LTD.;I/O BD JVS;837-13551";

	std::copy(std::begin(record), std::end(record), &m_ram[C100]);
	m_ram[C100 + 0x19] = 6;	/* outputs */
	m_ram[C100 + 0x1A] = 1;	/* output bytes */
	std::memcpy(&m_ram[C100 + 0x7C], id, sizeof(id));
}

/* What the Z80 program has written once its JVS enumeration is done. */
void
MieStandIn::start(bool test, bool service)
{
	m_ram[C000] = 0;
	m_ram[C001] = 0;
	m_ram[C002] = 1;
	m_ram[C003] = 0;
	m_ram[C004] = 3;
	write_switches(test, service);
	write_node_record();
	write_pages();
}

void
MieStandIn::command(unsigned cmd, bool test, bool service)
{
	switch (cmd) {
	case 1:
		write_pages();
		write_switches(test, service);
		break;
	case 2:
		m_ram[C002] = 1;
		m_ram[C003] = 0;
		m_ram[C004] = 3;
		break;
	default:
		break;
	}
	m_ram[C000] = 0;
	m_ram[C001] = 0;
}

bool
MieStandIn::read(uint32_t addr, unsigned size, uint64_t &value, bool test, bool service)
{
	value = 0;
	if (addr >= 0x00800000 && addr <= 0x00800014) {
		if (size != 2)
			return false;
		switch (addr & 0xFF) {
		case 0x00:
		case 0x06:
		case 0x0A:
			break;
		case 0x08:
			/* HACK: passes the check at @0C00B860 in all the
			 * BOOTROM versions (see mie_board.cpp for the chip) */
			set_reg(0x08, reg(0x08) ^ 0xF);
			break;
		case 0x0C:
			set_reg(0x0C, static_cast<uint16_t>(0xFFFF & ~((test ? 8 : 0) | (service ? 4 : 0))));
			break;
		default:
			return false;
		}
		value = reg(addr & 0x1F);
	} else if (addr == 0x0082F000) {
		return true;
	} else if (addr >= 0x00830000 && addr <= 0x0083FFFF) {
		value = m_ram[(addr / 2) & 0x7FFF];
	} else if (addr >= 0x01000000 && addr <= 0x010001FF) {
		/* Bus request: granted at once, nothing runs here. */
		value = (m_busrq ? 0 : 0x100) | 0x600;
	} else {
		return false;
	}
	return true;
}

bool
MieStandIn::write(uint32_t addr, unsigned size, uint64_t value, bool test, bool service)
{
	if (addr >= 0x00800000 && addr <= 0x00800014) {
		if (size != 2)
			return false;
		switch (addr & 0xFF) {
		case 0x00:
		case 0x02:
		case 0x04:
		case 0x06:
		case 0x08:
		case 0x0A:
		case 0x0C:
		case 0x10:
			break;
		default:
			return false;
		}
	} else if (addr == 0x0082F000) {
		if (value & 0x80)
			start(test, service);
	} else if (addr >= 0x00830000 && addr <= 0x0083FFFF) {
		const uint32_t index = (addr / 2) & 0x7FFF;

		m_ram[index] = static_cast<uint8_t>(value);
		if (index == C000 && value != 0)
			command(static_cast<unsigned>(value), test, service);
		else if (index == C001)
			m_ram[C001] = 0;	/* never busy */
	} else if (addr >= 0x01000000 && addr <= 0x010001FF) {
		m_busrq = static_cast<uint32_t>(value & 0x100);
	} else {
		return false;
	}
	return true;
}

} /* namespace odin */
