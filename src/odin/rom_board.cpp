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

#include "odin/rom_board.hpp"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <utility>

#include "odin/bytes.hpp"

/*
 * Rom Board (ROMBD)
 * =================
 *
 * There are two known types of ROM board (which are neatly documented in
 * the MAME hikaru driver.) The EPROM/MASKROM external BUS address can be
 * recovered from information at offset +13C of the EPROM data.
 *
 * This is what this data looks like:
 *
 *	AIRTRIX (Type 1)		ICs	Size
 *	================		===	====
 *	
 *	0003 fee8 c889 97c2 620c	29,30	2 x 4MB
 *	ffff 0000 0000 0000 0000	 		
 *	ffff 0000 0000 0000 0000	 		
 *	ffff 0000 0000 0000 0000	 		
 *	0005 b9a5 9e67 a52a bce0	37,38	2 x 16MB
 *	ffff 0000 0000 0000 0000	 
 *	0005 dabb b621 4bd4 5e6b	41,42	2 x 16MB
 *	ffff 0000 0000 0000 0000	 
 *	0005 0d06 ad63 790f a27e	45,46	2 x 16MB
 *	ffff 0000 0000 0000 0000	 
 *	0005 bdbb 4f01 14a7 6a4e	49,50	2 x 16MB
 *	ffff 0000 0000 0000 0000 
 *	ffff 0000 0000 0000 0000 
 *	ffff 0000 0000 0000 0000 
 *	ffff 0000 0000 0000 0000 
 *	ffff 0000 0000 0000 0000 
 *	ffff 0000 0000 0000 0000 
 *	ffff 0000 0000 0000 0000 
 *	ffff 0000 0000 0000 0000 
 *	ffff 0000 0000 0000 0000 
 *	
 *	BRAVEFF (Type ?)		ICs	Size
 *	================		===	====
 *	
 *	0002 0000 0000 0000 0000 	29,30	2 x 2MB
 *	0002 be43 7023 7077 f161	31,32	2 x 2MB
 *	0002 c60d d4f0 b533 8f66	33,34	2 x 2MB
 *	ffff 0000 0000 0000 0000 
 *	0004 8613 2876 3700 2f6d 		2 x 4MB
 *	0004 f545 a454 b97e bb4c 
 *	0004 d6ff 6fe3 df40 f343 
 *	0004 e3b6 2f23 b2b6 61c7 
 *	0004 4792 7015 853b faf1 
 *	0004 c44b 7a18 24dc e336 
 *	0004 e3b0 e492 17bb e589 
 *	0004 cd9f 08f3 3183 cd5c 
 *	0004 8fb6 3fa8 ebbb 9ed9 
 *	0004 2316 c644 66cc b590 
 *	0004 47d0 320d e677 85ad 
 *	0004 d76d cf62 4d9e 8564 
 *	ffff 0000 0000 0000 0000 
 *	ffff 0000 0000 0000 0000 
 *	ffff 0000 0000 0000 0000 
 *	ffff 0000 0000 0000 0000 
 *	
 *	PHARRIER			ICs	Size
 *	========			===	====
 *	
 *	0003 0000 0000 0000 0000	29,30	2 x 4MB 
 *	0003 388c 13b5 d289 5910	31,32	2 x 4MB
 *	0003 09a8 578f 73e5 94f7	33,34	2 x 4MB
 *	0003 b1de 6dad 9019 6dd5	35,36	2 x 4MB
 *	0005 7f16 2c37 1f9f aae5	37,38	2 x 16MB
 *	ffff 0000 0000 0000 0000	
 *	0005 986c 8d7a bd1d 5304	41,42	2 x 16MB
 *	ffff 0000 0000 0000 0000	
 *	0005 9784 b33d cb75 b08b	45,46	2 x 16MB
 *	ffff 0000 0000 0000 0000	
 *	0005 5056 b3a9 cbde be85	49,50	2 x 16MB
 *	ffff 0000 0000 0000 0000	
 *	0005 3d36 05bf d629 8ed6	53,54	2 x 16MB
 *	ffff 0000 0000 0000 0000	
 *	0005 b9f5 0082 5875 8163	57,58	2 x 16MB
 *	ffff 0000 0000 0000 0000	
 *	0005 b19d e7cc 158c d180	61,62	2 x 16MB
 *	ffff 0000 0000 0000 0000	
 *	0005 34bc 677b 5524 349e	65,66	2 x 16MB
 *	ffff 0000 0000 0000 0000	
 *	
 *	PODRACE (Type 2)		ICs	Size
 *	================		===	====	
 *	
 *	0003 0000 0000 0000 0000	29,30	2 x 4MB
 *	0003 5f01 4174 3594 38b3	31,32	2 x 4MB
 *	ffff 0000 0000 0000 0000	
 *	ffff 0000 0000 0000 0000	
 *	0004 7993 8e18 4d44 d239	37,38	2 x 8MB
 *	0004 4135 beab f0c8 04e2	39,40	2 x 8MB
 *	0004 9532 4c1c 925d 02fb	41,42	2 x 8MB
 *	0004 0809 7050 72bc 9311	43,44	2 x 8MB
 *	0004 de84 9d8a 7a5c e7fc	45,46	2 x 8MB
 *	0004 6806 1392 edf1 7bd1	47,48	2 x 8MB
 *	0004 b82d e114 5792 e5e5	49,50	2 x 8MB
 *	0004 3af3 a97c a8cc 721d	51,52	2 x 8MB
 *	0004 ced7 d3cf 6b67 fc76	53,54	2 x 8MB
 *	0004 586c 6954 13a0 db38	55,56	2 x 8MB
 *	0004 4f03 42bf 8ea6 adb6	57,58	2 x 8MB
 *	0004 8645 fc30 3847 ca6b	59,60	2 x 8MB
 *	0004 4140 01c4 ebe6 8085	61,62	2 x 8MB
 *	0004 b68b 7467 4715 4787	63,64	2 x 8MB
 *	0004 3cd6 144a e5d3 ba35	65,66	2 x 8MB
 *	0004 e668 08ed 1fe8 c4a1	67,68	2 x 8MB
 *	
 *	SGNASCAR (Type 2)		ICs	Size		Bus Bank
 *	=================		===	====		========
 *	
 *	0003 0000 0000 0000 0000	35,36	2 x 4MB		10,11
 *	ffff 0000 0000 0000 0000 
 *	ffff 0000 0000 0000 0000 
 *	ffff 0000 0000 0000 0000 
 *	0005 0352 d263 49fd 4ad3	19,20	2 x 16MB 	20,21
 *	0005 e717 d635 3637 0e8e	21,22			22,23
 *	0005 4001 8dab c65d bde3	23,24			24,25
 *	0005 615c 293d 7507 1d85	25,26			26,27
 *	0005 90a2 eccc 2b1e 2f9b	27,28			28,29
 *	0005 c98b 3ffb 51e3 701b	29,30			2A,2B
 *	0005 523f 2979 953c 2e5c	31,32			2C,2D
 *	0005 28cf 283f f17b 74fb	33,34			2E,2F
 *	ffff 0000 0000 0000 0000 
 *	ffff 0000 0000 0000 0000 
 *	ffff 0000 0000 0000 0000 
 *	ffff 0000 0000 0000 0000 
 *	ffff 0000 0000 0000 0000 
 *	ffff 0000 0000 0000 0000 
 *	ffff 0000 0000 0000 0000 
 *	ffff 0000 0000 0000 0000 
 *
 * The mapping from IC numbers to entries has been determined by matching
 * the CRCs in the entries and the CRC values reported in the MAME driver.
 *
 * Each entry (10 bytes) can be defined like this:
 *
 *	struct {
 *		uint16_t log_size;
 *		uint16_t bytesum_lo;
 *		uint16_t wordsum_lo;
 *		uint16_t bytesum_hi;
 *		uint16_t wordsum_hi;
 *	} rom_layout[4+16];
 *
 * and describes the size and CRC for an IC pair: the first four entries
 * describe the EPROMs, the remaining 16 the MASKROMs. If the first word
 * is FFFF, those ICs are not populated.
 *
 * Given the i'th entry, the size and location of an IC (pair) can be
 * computed as follows (see @0C004ED2):
 *
 *	base = samurai_tag_at (0x18000000) ? 8 :
 *	       samurai_tag_at (0x10000000) ? 0 : 0;
 *	
 *	size = 1*MB << rom_layout[i].log_size
 *	
 *	if (i < 4)
 *		///////////////////////////////////////////
 *		// EPROM
 *		// SAMURAI at 10000000 -> base = 0 -> EPROM at 10,11,12,13
 *		// SAMURAI at 18000000 -> base = 8 -> EPROM at 18,19,1A,1B
 *		// each section is 4 or 8 MB (2 ICs)
 *		///////////////////////////////////////////
 *		bank = base + 0x10 + i
 *
 *	else if ((_0C00F01C & C) != 8)
 *		///////////////////////////////////////////
 *		// MASKROM, type 1
 *		// SAMURAI at 10000000 -> base = 0 -> MASKROM at 20,21,22,23,...
 *		// SAMURAI at 18000000 -> base = 8 -> MASKROM at 30,31,32,33,...
 *		// each section is 16 or 32 MB (2 ICs)
 *		///////////////////////////////////////////
 *		bank = base * 2 + 0x1C + i 
 *
 *	else
 *		///////////////////////////////////////////
 *		// MASKROM, type 2
 *		// SAMURAI at 10000000 -> base = 0 -> MASKROM at 20,22,24,26,...
 *		// SAMURAI at 18000000 -> base = 8 -> MASKROM at 30,32,34,36,...
 *		// each section is 16 or 32 MB (2 ICs)
 *		///////////////////////////////////////////
 *		bank = base * 2 + 0x20 + (i - 4) * 2
 *	
 *	bus_addr = bank >> 24
 *	if (size < 8*MB)
 *		bus_addr += 4*MB
 *
 * Here base is determined by the bootrom (see @0C00B834) by looking for
 * the 'SAMURAI' string at both BUS addresses 18000000 and 10000000. If
 * the string is found at bank 18, then the base is 8; it's 0 otherwise.
 *
 * What IC is mapped to what entry is determined by bits 2 and 3 of 0C00F01C.
 * See memctl_get () and @0C004B32. 
 */

namespace odin {

namespace {

constexpr uint32_t MB = 1024 * 1024;

/* What a page without anything reads: the BOOTROM reads the pages 10 to 1B
 * one after the other, the page of the EEPROM with them, to find where the
 * EPROMs are, and two pages that both read zero would look alike. */
constexpr uint64_t NOTHING = 0xDEADBEEFDEADBEEFull;

uint64_t
low_bytes(uint64_t value, unsigned size)
{
	return (size >= 8) ? value : (value & ((1ull << (size * 8)) - 1));
}

} /* namespace */

std::optional<RomBoard::Config>
RomBoard::config_of(const std::string &game)
{
	uint32_t eprom_bank_size = 0, maskrom_bank_size = 0;
	bool has_rom = true, maskrom_is_stretched = false;
	Config config;

	std::memset(&config, 0, sizeof(config));

	if (game == "bootrom") {
		has_rom = false;
	} else if (game == "airtrix") {
		eprom_bank_size = 4;
		maskrom_bank_size = 16;
		config.has_5881 = true;
		config.crypt_key = 0x291B02C7;	/* MAME hikaru.cpp */
	} else if (game == "braveff") {
		eprom_bank_size = 2;
		maskrom_bank_size = 8;
	} else if (game == "pharrier") {
		eprom_bank_size = 4;
		maskrom_bank_size = 16;
		config.has_5881 = true;
		config.crypt_key = 0x2912C68A;
	} else if (game == "von4") {
		/* Same board layout as pharrier: 128M TSOP48 mask ROMs */
		eprom_bank_size = 4;
		maskrom_bank_size = 16;
	} else if (game == "podrace") {
		/* 32 x 64M SOP44 mask ROMs: each interleaved pair fills one 16 MB
		 * bank (MAME hikaru.cpp swracer: pairs at 0x1000000 steps) */
		eprom_bank_size = 4;
		maskrom_bank_size = 16;
		config.has_5881 = true;
		config.crypt_key = 0x2903DAD5;
	} else if (game == "sgnascar") {
		eprom_bank_size = 4;
		maskrom_bank_size = 16;
		maskrom_is_stretched = true;
	} else {
		return std::nullopt;
	}

	config.has_rom = has_rom;
	if (has_rom) {
		/* Four pages of EPROMs, sixteen of mask ROMs */
		config.eprom_bank[0] = 0x10;
		config.eprom_bank[1] = 0x13;
		config.maskrom_bank[0] = 0x20;
		config.maskrom_bank[1] = 0x2F;
		config.eprom_bank_size = eprom_bank_size;
		config.maskrom_bank_size = maskrom_bank_size;
		config.maskrom_is_stretched = maskrom_is_stretched;
	}
	config.eeprom_bank = 0x14;
	return config;
}

RomBoard::RomBoard(const Config &config, std::span<const uint8_t> eprom, std::span<const uint8_t> maskrom,
                   bool fpga, std::function<void(const char *)> error)
	: m_config(config), m_eprom(eprom), m_maskrom(maskrom), m_error(std::move(error))
{
	if (m_config.has_5881) {
		m_crypt_ram.assign(CRYPT_RAM_SIZE, 0);
		m_crypt = std::make_unique<Crypt315_5881>(m_config.crypt_key);
		/* The chip reads its input from the RAM, one byte-swapped word
		 * at a time (MAME hikaru.cpp prot_ram_r). */
		m_crypt->set_read([this](uint32_t addr) {
			uint16_t word;

			std::memcpy(&word, &m_crypt_ram[(addr & 0x7FFF) * 2], 2);
			return static_cast<uint16_t>((word >> 8) | (word << 8));
		});
	}
	/* 834-14125 ROM board: Actel A54SX32 stamped 317-0283-COM, ID 0x4252,
	 * key as in MAME hikaru.cpp. The key is proven by the game: the chip
	 * then gives the text SGNASCAR compares it with. */
	if (fpga) {
		m_fpga = std::make_unique<ActelM1>(0x56DEDF33, 0x4252);
		m_fpga->set_read([this](uint32_t addr) -> uint8_t {
			return (addr < m_maskrom.size()) ? m_maskrom[addr] : 0;
		});
	}
}

/* Field by field, into zeros: the bytes between the fields are saved too. */
void
RomBoard::save(Config &out) const
{
	std::memset(&out, 0, sizeof(out));
	out.has_rom = m_config.has_rom;
	out.eprom_bank[0] = m_config.eprom_bank[0];
	out.eprom_bank[1] = m_config.eprom_bank[1];
	out.maskrom_bank[0] = m_config.maskrom_bank[0];
	out.maskrom_bank[1] = m_config.maskrom_bank[1];
	out.eeprom_bank = m_config.eeprom_bank;
	out.eprom_bank_size = m_config.eprom_bank_size;
	out.maskrom_bank_size = m_config.maskrom_bank_size;
	out.maskrom_is_stretched = m_config.maskrom_is_stretched;
	out.has_5881 = m_config.has_5881;
	out.crypt_key = m_config.crypt_key;
	out.unpopulated_logged = m_config.unpopulated_logged;
}

void
RomBoard::reset()
{
	std::fill(m_crypt_ram.begin(), m_crypt_ram.end(), 0);
	if (m_fpga)
		m_fpga->reset();
}

uint64_t
RomBoard::read_guard(uint32_t offs, unsigned size)
{
	uint64_t value = 0;

	if (m_crypt && offs <= 0xFFFF) {
		/* RAM of the 315-5881 (MAME hikaru.cpp localbus_map_5881) */
		if (offs + size <= CRYPT_RAM_SIZE)
			value = load_bytes(&m_crypt_ram[offs], size);
		return value;
	}
	if (m_crypt && offs >= 0x10000 && offs <= 0x1000F)
		return low_bytes(m_crypt->bus_read(offs & 0xF, size), size);
	if (m_fpga && offs <= 0x13)
		return low_bytes(m_fpga->bus_read(offs, size), size);

	/* HACK for the boards without the 315-5881 nor the FPGA (braveff,
	 * von4): the BOOTROM probe (ROM 0x1F3A) writes 0x19620217 at +0xC and
	 * +0x8 and keeps "retained" in GBR+0x1C bits 3/2; what those boards
	 * answer has not been measured.
	 *
	 * The value of bits 2 and 3 of 0C00F01C (which is GBR 28) depends on
	 * whether these two ports retain the value 0x19620217. If the value
	 * of the upper two bits is 4, then the EPROM start at IC 29; they
	 * start at 35 otherwise. See @0C004BF8. If the value is 8, then the
	 * MASKROM placement in the bus address space is non-linear. See
	 * @0C004F82 for details.
	 *
	 * Judging by the ROM file extensions, we want these bits to be 4 for
	 * everything except SGNASCAR, and 8 for SGNASCAR (?). PHARRIER should
	 * be '4' type, but the ROM zip contains two IC35's, one EPROM and one
	 * MASKROM. */
	if (offs == 0x8 && !m_config.maskrom_is_stretched)
		return low_bytes(0x19620217, size);
	if (offs == 0xC && m_config.maskrom_is_stretched)
		return low_bytes(0x19620217, size);
	return 0;
}

bool
RomBoard::guarded(uint32_t offs) const
{
	return (m_crypt && (offs <= 0xFFFF || (offs >= 0x10000 && offs <= 0x1000F))) || (m_fpga && offs <= 0x13);
}

void
RomBoard::write_guard(uint32_t offs, unsigned size, uint64_t value)
{
	if (m_crypt && offs <= 0xFFFF) {
		if (offs + size <= CRYPT_RAM_SIZE)
			store_bytes(&m_crypt_ram[offs], size, value);
	} else if (m_crypt && offs >= 0x10000 && offs <= 0x1000F) {
		m_crypt->bus_write(offs & 0xF, size, static_cast<uint32_t>(value));
	} else if (m_fpga && offs <= 0x13) {
		m_fpga->bus_write(offs, size, static_cast<uint32_t>(value));
	}
}

/* A read of a set of ROMs at an offset in it. Past its end is an empty
 * socket, which reads zero. The games probe every socket at boot (von4
 * 0C713C84: pages 10-1B, 128 bytes each against the +4MB mirror), so an
 * empty one is read thousands of times: it is told once a page. */
uint64_t
RomBoard::read_rom(std::span<const uint8_t> rom, uint32_t offs, unsigned size, uint32_t bus_addr)
{
	uint64_t value = 0;

	if (offs < rom.size()) {
		if (offs + size <= rom.size())
			value = load_bytes(&rom[offs], size);
		return value;
	}

	const uint32_t bank = bus_addr >> 24;
	const uint64_t bit = 1ull << ((bank - 0x10) & 63);

	if (!(m_config.unpopulated_logged & bit)) {
		char text[96];

		m_config.unpopulated_logged |= bit;
		std::snprintf(text, sizeof(text), "ROMBD unpopulated socket: bank %02X reads as zero (first R%u %08X)",
		              bank, size * 8, bus_addr);
		if (m_error)
			m_error(text);
	}
	return 0;
}

uint64_t
RomBoard::read(uint32_t bus_addr, unsigned size)
{
	const uint32_t bank = bus_addr >> 24;
	const uint32_t offs = bus_addr & 0xFFFFFF;

	/* The EEPROM: bit 0 is SDA (MAME hikaru.cpp:907) */
	if (bank == m_config.eeprom_bank)
		return m_eeprom.sda_read() ? 1 : 0;

	if (!m_config.has_rom)
		return low_bytes(NOTHING, size);

	if (bank >= m_config.eprom_bank[0] && bank <= m_config.eprom_bank[1]) {
		/* A page for each pair of EPROMs, of 2 or 4 MB each */
		const uint32_t num = bank - m_config.eprom_bank[0];
		const uint32_t bank_size = (m_config.eprom_bank_size == 2) ? 4 * MB : 8 * MB;

		return read_rom(m_eprom, (offs & (bank_size - 1)) + num * bank_size, size, bus_addr);
	}
	if (bank >= m_config.maskrom_bank[0] && bank <= m_config.maskrom_bank[1]) {
		/* Sixteen pages of 16 MB, whatever the size of the ROMs */
		const uint32_t num = bank - m_config.maskrom_bank[0];

		return read_rom(m_maskrom, offs + num * 16 * MB, size, bus_addr);
	}
	return low_bytes(NOTHING, size);
}

std::span<const uint8_t>
RomBoard::plain(uint32_t bus_addr) const
{
	const uint32_t bank = bus_addr >> 24;
	const uint32_t offs = bus_addr & 0xFFFFFF;
	std::span<const uint8_t> rom;
	uint32_t page_size = 0, at = 0;

	if (bank == m_config.eeprom_bank || !m_config.has_rom)
		return {};
	if (bank >= m_config.eprom_bank[0] && bank <= m_config.eprom_bank[1]) {
		page_size = (m_config.eprom_bank_size == 2) ? 4 * MB : 8 * MB;
		at = (offs & (page_size - 1)) + (bank - m_config.eprom_bank[0]) * page_size;
		rom = m_eprom;
	} else if (bank >= m_config.maskrom_bank[0] && bank <= m_config.maskrom_bank[1]) {
		page_size = 16 * MB;
		at = offs + (bank - m_config.maskrom_bank[0]) * page_size;
		rom = m_maskrom;
	} else {
		return {};
	}
	const size_t end = std::min<size_t>(at - at % page_size + page_size, rom.size());

	return at < end ? rom.subspan(at, end - at) : std::span<const uint8_t>();
}

bool
RomBoard::write(uint32_t bus_addr, unsigned size, uint64_t value)
{
	(void) size;
	if ((bus_addr >> 24) != m_config.eeprom_bank)
		return false;
	/* Bit 0 SDA, 1 SCL, 2 /CS, 3 /RST (MAME hikaru.cpp:909) */
	m_eeprom.set_pins(!(value & 4), !(value & 8), (value >> 1) & 1, value & 1);
	return true;
}

} /* namespace odin */
