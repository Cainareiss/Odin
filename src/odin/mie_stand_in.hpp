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

#ifndef ODIN_MIE_STAND_IN_HPP
#define ODIN_MIE_STAND_IN_HPP

#include <array>
#include <cstdint>
#include <span>

namespace odin {

/* HACK: a stand-in for the MIE (mie.hpp) and the program the BOOTROM
 * uploads into it, for when the ROM of the chip is not there: the BOOTROM
 * run on its own, or ODIN_MIE=hle.
 *
 * The real MIE runs a Z80 program that the BOOTROM uploads into its RAM
 * (ROM offset 0x40C48, 2316 bytes). That program is not executed here: the
 * shared RAM is filled with what the program writes, as read from its code
 * and measured on a complete system. Z80 addresses below; index in the RAM
 * = address - 0x8000.
 *
 * C000  command from the SH-4: 1 = poll the JVS board, 2 = reset JVS
 * C001  busy flag, C002 JVS node count, C003 error bits
 * C004  stage, 3 = ready; C005 main board Test (bit 6) / Service (bit 7)
 * C100  record of JVS node 1 (837-13551), ID string at C17C
 * E000  switches, E100 coins, E200 analog, E300-E500 unused.
 *       Page format: [count][node][size][data...], size counts itself.
 *
 * No JVS control reaches the game through it: only the two switches of
 * the main board. */
class MieStandIn {
public:
	static constexpr size_t NUM_REGS = 0x20;

	/* ram: the 32 KB of the Z80 at 8000-FFFF, owned by the caller */
	explicit MieStandIn(std::span<uint8_t> ram);

	void reset();

	/* The windows of the MIE in the master SH-4: 00800000-0083FFFF and
	 * the bus request at 01000000-010001FF. test, service: the switches
	 * of the main board. False when the stand-in has nothing there. */
	bool read(uint32_t addr, unsigned size, uint64_t &value, bool test, bool service);
	bool write(uint32_t addr, unsigned size, uint64_t value, bool test, bool service);

	/* What the save states keep of it */
	std::span<uint8_t> registers() { return m_regs; }
	uint32_t &busrq() { return m_busrq; }

private:
	uint16_t reg(uint32_t offs) const;
	void set_reg(uint32_t offs, uint16_t value);
	void write_switches(bool test, bool service);
	void write_pages();
	void write_node_record();
	void start(bool test, bool service);
	void command(unsigned cmd, bool test, bool service);

	std::span<uint8_t> m_ram;
	std::array<uint8_t, NUM_REGS> m_regs;
	uint32_t m_busrq;
};

} /* namespace odin */

#endif /* ODIN_MIE_STAND_IN_HPP */
