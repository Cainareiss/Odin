/*
 *  ██████╗ ██████╗ ██╗███╗   ██╗
 * ██╔═══██╗██╔══██╗██║████╗  ██║
 * ██║   ██║██║  ██║██║██╔██╗ ██║
 * ██║   ██║██║  ██║██║██║╚██╗██║
 * ╚██████╔╝██████╔╝██║██║ ╚████║
 *  ╚═════╝ ╚═════╝ ╚═╝╚═╝  ╚═══╝
 *
 * Copyright (C) 2026 Réda Chérif-Touil
 * Copyright (C) 2026 Daniel Martin
 *
 * Odin is free software: you can redistribute it and/or modify it under
 * the terms of the GNU General Public License as published by the Free
 * Software Foundation, either version 3 of the License, or (at your
 * option) any later version.
 *
 * Odin is distributed in the hope that it will be useful, but WITHOUT ANY
 * WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 * for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with Odin.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "odin/machine/boards/atvtrack/atvtrack.hpp"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "odin/machine/boards/atvtrack/atvtrack_fpga.hpp"

namespace odin {

namespace {

constexpr uint32_t SHARED = 0x00000000;
constexpr uint32_t CONTROL = 0x00020000, DAC = 0x00020040;
constexpr uint32_t NAND_DATA = 0x14000000, NAND_COMMAND = 0x14100000, NAND_ADDRESS = 0x14200000;
constexpr uint32_t CACHE_RAM = 8u << 10, CACHE_RAM_AT = 0x1C000000;
constexpr uint32_t FLASH_SECTOR = 64u << 10;
/* Smashing Drive */
constexpr uint32_t SMASH_SHARED = 0x10000000, SMASH_CONTROL = 0x10000400, SMASH_DAC = 0x10000440;
constexpr uint32_t SMASH_ROM = 64u << 20, SMASH_PROGRAM = 0x14000000, SMASH_PROGRAM_SIZE = 4u << 20;
constexpr uint32_t SDRAM = 0x0C000000, SDRAM_AREA = 0x04000000;
constexpr uint32_t GPU_REGS = 0x14000000, GPU_REGS_SIZE = 0x8000, GPU_RAM = 0x18000000;
constexpr uint32_t MASTER_RAM = 8u << 20, SLAVE_RAM = 16u << 20, GPU_RAM_SIZE = 32u << 20;

/* Neon 250 registers */
constexpr uint32_t GPU_START = 0x00, GPU_IRQ_PENDING = 0x70, GPU_IRQ_MASK = 0x74, GPU_RENDER = 0xB0;

/* 200 MHz: the 33 MHz crystal times 6 (MD2-MD0 = 011) */
constexpr int32_t FRAME_CYCLES = 200'000'000 / 60;
constexpr unsigned SLICES = 2000;
constexpr uint32_t DAC_RATE = 32000;

} /* namespace */


NandBank::NandBank(std::array<std::vector<uint8_t>, CHIPS> chips)
{
	for (unsigned c = 0; c < CHIPS; c++) {
		m_chips[c].data = std::move(chips[c]);
		m_chips[c].data.resize(SIZE, 0xFF);
	}
}

/* Reads continue through the page's spare bytes into the next page, from the
 * area the command chose (K9F2808U0B datasheet, sequential row read) */
uint8_t NandBank::read(Chip &chip)
{
	switch (chip.command) {
	case 0x00:
	case 0x01:
	case 0x50: {
		const uint8_t value = chip.data[chip.at];

		chip.at++;
		if (chip.at % PAGE == 0) {
			const uint32_t page = chip.at / PAGE % PAGES;

			chip.at = page * PAGE + (chip.command == 0x50 ? 512 : 0);
		}
		return value;
	}
	case 0x90:
		/* Samsung, K9F2808U0B */
		return chip.id++ == 0 ? 0xEC : 0x73;
	default:
		/* Status: ready, not protected */
		return 0xC0;
	}
}

uint32_t NandBank::read_data()
{
	uint32_t lanes = 0;

	for (unsigned c = 0; c < CHIPS; c++)
		lanes |= static_cast<uint32_t>(read(m_chips[c])) << (8 * c);
	return lanes;
}

void NandBank::set_block(unsigned chip, uint32_t number, std::span<const uint8_t> bytes)
{
	std::copy(bytes.begin(), bytes.end(), m_chips[chip].data.begin() + number * BLOCK);
	m_changed.insert({ chip, number });
}

void NandBank::write_data(uint32_t lanes)
{
	for (unsigned c = 0; c < CHIPS; c++) {
		Chip &chip = m_chips[c];

		if (chip.command == 0x80 && chip.at < SIZE) {
			/* Programming only clears bits */
			chip.data[chip.at] &= static_cast<uint8_t>(lanes >> (8 * c));
			m_changed.insert({ c, chip.at / BLOCK });
			chip.at++;
		}
	}
}

void NandBank::write_command(uint32_t lanes)
{
	for (unsigned c = 0; c < CHIPS; c++) {
		Chip &chip = m_chips[c];
		const uint8_t command = static_cast<uint8_t>(lanes >> (8 * c));

		switch (command) {
		case 0x00:
		case 0x01:
		case 0x50:
			chip.area = command == 0x00 ? 0 : command == 0x01 ? 1 : 2;
			chip.command = command;
			chip.step = 0;
			break;
		case 0x80:
		case 0x60:
		case 0x90:
			chip.command = command;
			chip.step = 0;
			chip.id = 0;
			break;
		case 0x10:
		case 0x70:
			chip.command = 0x70;
			break;
		case 0xD0:
			if (chip.command == 0x60) {
				const uint32_t block = chip.row / 32 % 1024;

				std::fill_n(chip.data.begin() + block * 32 * PAGE, 32 * PAGE, uint8_t{0xFF});
				m_changed.insert({ c, block });
			}
			chip.command = 0x70;
			break;
		default:
			chip.command = 0xFF;
			chip.area = 0;
			break;
		}
	}
}

/* One column byte, then two row bytes; an erase sends the row only */
void NandBank::write_address(uint32_t lanes)
{
	for (unsigned c = 0; c < CHIPS; c++) {
		Chip &chip = m_chips[c];
		const uint32_t byte = (lanes >> (8 * c)) & 0xFF;
		const bool row_only = chip.command == 0x60;

		if (!row_only && chip.step == 0) {
			chip.column = byte;
			chip.row = 0;
		} else {
			const uint32_t shift = (chip.step - (row_only ? 0 : 1)) * 8;

			chip.row = (chip.row & ~(0xFFu << shift)) | (byte << shift);
		}
		chip.step++;
		/* Area C reads the 16 spare bytes */
		const uint32_t base = chip.area == 1 ? 256 : chip.area == 2 ? 512 : 0;
		const uint32_t column = chip.area == 2 ? chip.column % 16 : chip.column;

		chip.at = (chip.row % PAGES) * PAGE + base + column;
	}
}


AtvTrack::AtvTrack(std::array<std::vector<uint8_t>, NandBank::CHIPS> nand, std::span<const uint8_t> fpga_config)
	: m_boot(atvtrack_boot(fpga_config)), m_control_at(CONTROL), m_dac_at(DAC), m_shared(Sh4::PAGE_SIZE),
	  m_master_ram(MASTER_RAM), m_slave_ram(SLAVE_RAM), m_gpu_ram(GPU_RAM_SIZE), m_nand(std::move(nand)),
	  m_bus{ Bus(*this, MASTER), Bus(*this, SLAVE) },
	  m_pmx(m_gpu_ram, [this](uint32_t offset, uint32_t size) { m_sh[SLAVE]->written(GPU_RAM + offset, size); })
{
	if (m_boot.empty())
		std::fprintf(stderr, "ERROR: atvtrack: the FPGA configuration is too short for its boot program\n");
	build();
}

AtvTrack::AtvTrack(std::vector<uint8_t> rom, std::vector<uint8_t> program)
	: m_rom(std::move(rom)), m_program(std::move(program)), m_smash(true), m_control_at(SMASH_CONTROL),
	  m_dac_at(SMASH_DAC), m_shared(Sh4::PAGE_SIZE), m_master_ram(MASTER_RAM), m_slave_ram(SLAVE_RAM),
	  m_gpu_ram(GPU_RAM_SIZE), m_nand({}), m_bus{ Bus(*this, MASTER), Bus(*this, SLAVE) },
	  m_pmx(m_gpu_ram, [this](uint32_t offset, uint32_t size) { m_sh[SLAVE]->written(GPU_RAM + offset, size); })
{
	m_rom.resize(SMASH_ROM, 0xFF);
	m_program.resize(SMASH_PROGRAM_SIZE, 0xFF);
	build();
}

std::vector<uint8_t> AtvTrack::rom_of(std::span<const uint8_t> a, std::span<const uint8_t> b, std::span<const uint8_t> c)
{
	std::vector<uint8_t> rom(SMASH_ROM, 0xFF);
	auto interleave = [&rom](std::span<const uint8_t> chip, size_t at) {
		for (size_t i = 0; i + 1 < chip.size() && at + i * 2 + 1 < rom.size(); i += 2) {
			rom[at + i * 2] = chip[i];
			rom[at + i * 2 + 1] = chip[i + 1];
		}
	};

	interleave(a, 0);
	interleave(b, 2);
	/* The third chip is copied as is */
	std::copy(c.begin(), c.begin() + static_cast<std::ptrdiff_t>(std::min<size_t>(c.size(), 1u << 25)),
	          rom.begin() + (1u << 25));
	return rom;
}

void AtvTrack::build()
{
	for (std::vector<uint8_t> &ram : m_cache_ram)
		ram.resize(CACHE_RAM);
	/* Clock mode 3: the CPU at six times the 33 MHz crystal */
	for (unsigned which : { MASTER, SLAVE }) {
		m_iregs[which].resize(0x10000);
		m_sh[which] = std::make_unique<Sh4>(m_bus[which], std::span<uint8_t>(m_iregs[which]), which == MASTER,
		                                    true, 3);
		m_sh[which]->set_translate(true);
		m_sh[which]->set_clock_waits(true);
		m_sh[which]->set_still_turns(true);
	}
}

void AtvTrack::map_pages()
{
	for (unsigned which : { MASTER, SLAVE }) {
		Sh4 &sh = *m_sh[which];
		std::vector<uint8_t> &ram = which == MASTER ? m_master_ram : m_slave_ram;
		const uint32_t ram_pages = static_cast<uint32_t>(ram.size() / Sh4::PAGE_SIZE);

		/* Smashing Drive: the control registers are on this page */
		if (m_smash && which == MASTER) {
			sh.map_quiet(SMASH_SHARED / Sh4::PAGE_SIZE);
			for (uint32_t p = 0; p < SMASH_ROM / Sh4::PAGE_SIZE; p++)
				sh.map_page(p, m_rom.data() + p * Sh4::PAGE_SIZE, nullptr);
			for (uint32_t p = 0; p < SMASH_PROGRAM_SIZE / Sh4::PAGE_SIZE; p++)
				sh.map_page(SMASH_PROGRAM / Sh4::PAGE_SIZE + p, m_program.data() + p * Sh4::PAGE_SIZE, nullptr);
		} else {
			sh.map_page(SHARED / Sh4::PAGE_SIZE, m_shared.data(), m_shared.data());
			sh.map_shared(SHARED / Sh4::PAGE_SIZE);
		}
		/* Its slave also reads the ROM, past its RAM at 0 */
		if (m_smash && which == SLAVE) {
			for (uint32_t p = 1; p < SMASH_ROM / Sh4::PAGE_SIZE; p++)
				sh.map_page(p, m_rom.data() + p * Sh4::PAGE_SIZE, nullptr);
			/* And the program, past the Neon 250 registers */
			for (uint32_t p = GPU_REGS_SIZE / Sh4::PAGE_SIZE; p < SMASH_PROGRAM_SIZE / Sh4::PAGE_SIZE; p++)
				sh.map_page(SMASH_PROGRAM / Sh4::PAGE_SIZE + p, m_program.data() + p * Sh4::PAGE_SIZE, nullptr);
		}
		/* Operand cache as RAM (CCR.ORA), at 7C000000 */
		for (uint32_t p = 0; p < CACHE_RAM / Sh4::PAGE_SIZE; p++) {
			uint8_t *page = m_cache_ram[which].data() + p * Sh4::PAGE_SIZE;

			sh.map_page(CACHE_RAM_AT / Sh4::PAGE_SIZE + p, page, page);
		}
		/* Map once: a host page mapped twice is not written in place.
		 * The bus serves the mirrors over the rest of area 3. */
		for (uint32_t p = 0; p < ram_pages; p++) {
			uint8_t *page = ram.data() + p * Sh4::PAGE_SIZE;

			sh.map_page(SDRAM / Sh4::PAGE_SIZE + p, page, page);
		}
		if (which == SLAVE)
			for (uint32_t p = 0; p < GPU_RAM_SIZE / Sh4::PAGE_SIZE; p++) {
				uint8_t *page = m_gpu_ram.data() + p * Sh4::PAGE_SIZE;

				sh.map_page(GPU_RAM / Sh4::PAGE_SIZE + p, page, page);
			}
	}
}

void AtvTrack::reset()
{
	std::fill(m_shared.begin(), m_shared.end(), uint8_t{0});
	std::copy(m_boot.begin(), m_boot.end(), m_shared.begin());
	std::fill(m_master_ram.begin(), m_master_ram.end(), uint8_t{0});
	std::fill(m_slave_ram.begin(), m_slave_ram.end(), uint8_t{0});
	std::fill(m_gpu_ram.begin(), m_gpu_ram.end(), uint8_t{0});
	for (std::vector<uint8_t> &ram : m_cache_ram)
		std::fill(ram.begin(), ram.end(), uint8_t{0});
	m_control = 0;
	m_port_a = 0;
	m_slave_enabled = m_slave_running = false;
	m_gpu_pending = 0;
	m_gpu_mask = 0xFFFF;
	m_pmx.reset();
	m_dac.fill(0);
	m_dac_time = m_dac_played = m_dac_half = 0;
	m_dac_asking = false;
	m_adc_step = m_adc_command = 0;
	m_adc_clock = false;
	m_ball_clock = false;
	m_ball_edge = 0;
	m_flash_step = 0;
	m_frame = 0;
	map_pages();
	m_sh[MASTER]->reset(true);
	m_sh[SLAVE]->reset(true);
	m_sh[SLAVE]->set_run(Sh4Run::Stopped);
}

/* AMD flash commands: AA at 5555, 55 at AAAA, then A0 at 5555 and a byte to
 * program, or 80 at 5555, AA, 55 and 30 at a sector to erase. They finish at
 * once. */
void AtvTrack::flash_write(uint32_t offset, uint8_t byte)
{
	const uint32_t low = offset & 0xFFFF;
	const uint32_t step = m_flash_step;

	m_flash_step = 0;
	if (byte == 0xF0)
		return;
	switch (step) {
	case 0:
	case 4:
		if (low == 0x5555 && byte == 0xAA)
			m_flash_step = step + 1;
		break;
	case 1:
	case 5:
		if (low == 0xAAAA && byte == 0x55)
			m_flash_step = step + 1;
		break;
	case 2:
		if (low == 0x5555 && byte == 0xA0)
			m_flash_step = 3;
		else if (low == 0x5555 && byte == 0x80)
			m_flash_step = 4;
		break;
	case 3:
		/* Programming only clears bits */
		m_program[offset] &= byte;
		m_flash_changed.insert(offset / FLASH_SECTOR);
		break;
	case 6:
		if (byte == 0x30) {
			std::fill_n(m_program.begin() + offset / FLASH_SECTOR * FLASH_SECTOR, FLASH_SECTOR, uint8_t{0xFF});
			m_flash_changed.insert(offset / FLASH_SECTOR);
		}
		break;
	default:
		break;
	}
}

void AtvTrack::run_frame()
{
	m_sound.clear();
	for (unsigned slice = 0; slice < SLICES; slice++) {
		m_sh[MASTER]->run(FRAME_CYCLES / SLICES);
		if (m_slave_running)
			m_sh[SLAVE]->run(FRAME_CYCLES / SLICES);
		play_dac(SLICES);
	}
	m_frame++;
}

/* The DAC plays 32 kHz 8-byte samples (bass, bass, left, right) from two
 * 32-byte halves, requesting the next with IRL 5 until the program writes to
 * the half it was given. */
void AtvTrack::play_dac(unsigned slices_per_frame)
{
	m_dac_time += DAC_RATE;
	while (m_dac_time >= 60u * slices_per_frame) {
		m_dac_time -= 60u * slices_per_frame;
		int16_t sample[4];

		std::memcpy(sample, m_dac.data() + m_dac_half * 32 + m_dac_played * 8, 8);
		m_sound.push_back(sample[2]);
		m_sound.push_back(sample[3]);
		if (++m_dac_played == 4) {
			m_dac_played = 0;
			m_dac_half ^= 1;
			m_dac_asking = true;
			m_sh[MASTER]->set_irq(Sh4Source::IRQ5, true);
		}
	}
}

/* Bit 2 of the first control register holds the slave in reset unless the
 * system port allows it to run */
void AtvTrack::control_write(uint32_t value)
{
	m_control = value;
	const bool run = (value & 4) && m_slave_enabled;

	if (run && !m_slave_running) {
		m_sh[SLAVE]->reset(true);
		m_sh[SLAVE]->set_run(Sh4Run::Running);
	} else if (!run && m_slave_running) {
		m_sh[SLAVE]->set_run(Sh4Run::Stopped);
	}
	m_slave_running = run;
}

/* SDRAM mirrored further into area 3: the boot program sizes it this way */
bool AtvTrack::mirror(std::vector<uint8_t> &ram, uint32_t addr, uint32_t size, uint64_t *read, uint64_t value)
{
	if (addr < SDRAM || addr >= SDRAM + SDRAM_AREA)
		return false;
	uint8_t *const at = ram.data() + (addr - SDRAM) % ram.size();

	if (read) {
		*read = 0;
		std::memcpy(read, at, size);
	} else {
		std::memcpy(at, &value, size);
	}
	return true;
}

bool AtvTrack::master_read(uint32_t addr, uint32_t size, uint64_t &value)
{
	if (mirror(m_master_ram, addr, size, &value, 0))
		return true;
	if (m_smash && addr >= SMASH_SHARED && addr + size <= SMASH_SHARED + 0x400) {
		value = 0;
		std::memcpy(&value, m_shared.data() + (addr - SMASH_SHARED), size);
		return true;
	}
	if (addr >= m_control_at && addr < m_control_at + 8) {
		/* HEURISTIC: every status reads all ones */
		value = size == 8 ? ~uint64_t{0} : 0xFFFFFFFF;
		return true;
	}
	if (!m_smash && addr >= NAND_DATA && addr < NAND_DATA + 8) {
		value = m_nand.read_data();
		if (size == 8)
			value |= static_cast<uint64_t>(m_nand.read_data()) << 32;
		return true;
	}
	return false;
}

bool AtvTrack::master_write(uint32_t addr, uint32_t size, uint64_t value)
{
	if (mirror(m_master_ram, addr, size, nullptr, value))
		return true;
	if (m_smash && addr >= SMASH_SHARED && addr + size <= SMASH_SHARED + 0x400) {
		std::memcpy(m_shared.data() + (addr - SMASH_SHARED), &value, size);
		m_sh[SLAVE]->written(SHARED + (addr - SMASH_SHARED), size);
		return true;
	}
	if (m_smash && addr >= SMASH_PROGRAM && addr < SMASH_PROGRAM + SMASH_PROGRAM_SIZE) {
		flash_write(addr - SMASH_PROGRAM, static_cast<uint8_t>(value));
		return true;
	}
	if (addr >= m_control_at && addr < m_control_at + 8) {
		if (addr == m_control_at)
			control_write(static_cast<uint32_t>(value));
		return true;
	}
	if (addr >= m_dac_at && addr < m_dac_at + 0x40) {
		const uint32_t at = addr - m_dac_at;

		std::memcpy(m_dac.data() + at, &value, std::min<uint32_t>(size, 0x40 - at));
		if (m_dac_asking) {
			m_dac_asking = false;
			m_sh[MASTER]->set_irq(Sh4Source::IRQ5, false);
		}
		return true;
	}
	if (m_smash)
		return false;
	if (addr >= NAND_DATA && addr < NAND_DATA + 8) {
		m_nand.write_data(static_cast<uint32_t>(value));
		if (size == 8)
			m_nand.write_data(static_cast<uint32_t>(value >> 32));
		return true;
	}
	if (addr >= NAND_COMMAND && addr < NAND_COMMAND + 8) {
		m_nand.write_command(static_cast<uint32_t>(value));
		return true;
	}
	if (addr >= NAND_ADDRESS && addr < NAND_ADDRESS + 8) {
		m_nand.write_address(static_cast<uint32_t>(value));
		return true;
	}
	return false;
}

void AtvTrack::gpu_irqs()
{
	m_sh[SLAVE]->set_irq(Sh4Source::IRQ14, (m_gpu_pending & ~m_gpu_mask) != 0);
}

bool AtvTrack::slave_read(uint32_t addr, uint32_t size, uint64_t &value)
{
	if (mirror(m_slave_ram, addr, size, &value, 0))
		return true;
	if (addr >= GPU_REGS && addr < GPU_REGS + GPU_REGS_SIZE) {
		switch (addr - GPU_REGS) {
		case GPU_IRQ_PENDING:
			value = m_gpu_pending;
			return true;
		case GPU_IRQ_MASK:
			value = m_gpu_mask;
			return true;
		default:
			return false;
		}
	}
	return false;
}

bool AtvTrack::slave_write(uint32_t addr, uint32_t size, uint64_t value)
{
	if (mirror(m_slave_ram, addr, size, nullptr, value))
		return true;
	if (addr >= GPU_REGS && addr < GPU_REGS + GPU_REGS_SIZE) {
		const uint32_t data = static_cast<uint32_t>(value);

		switch (addr - GPU_REGS) {
		case GPU_START:
			/* HEURISTIC: the PMX reply the program waits for */
			if (data)
				m_gpu_ram[0x1350] = 1;
			return true;
		case GPU_IRQ_PENDING:
			m_gpu_pending &= ~data;
			gpu_irqs();
			return true;
		case GPU_IRQ_MASK:
			m_gpu_mask = data;
			gpu_irqs();
			return true;
		case GPU_RENDER:
			m_pmx.doorbell(data);
			if (data == 0x0001) {
				/* HEURISTIC: CCBs finish at once */
				m_gpu_pending |= 1u << 7;
				gpu_irqs();
			}
			return true;
		default:
			return false;
		}
	}
	return false;
}

bool AtvTrack::Bus::read(uint32_t addr, uint32_t size, uint64_t &value)
{
	return m_which == MASTER ? m_board.master_read(addr, size, value) : m_board.slave_read(addr, size, value);
}

bool AtvTrack::Bus::write(uint32_t addr, uint32_t size, uint64_t value)
{
	return m_which == MASTER ? m_board.master_write(addr, size, value) : m_board.slave_write(addr, size, value);
}

/* Master port A: the FPGA multiplexes the cabinet I/O, selected by bits
 * 15-11. Inputs are active low; bit 2 says the FPGA is configured. */
uint16_t AtvTrack::Bus::port_a_read()
{
	/* The group selected low by bits 14-11 drives its switches on bits
	 * 10-3, low when pressed */
	uint16_t pins = 0xFFFF;

	if (m_which == MASTER) {
		for (unsigned group = 0; group < 4; group++)
			if (!(m_board.m_port_a & (0x800u << group)))
				pins = static_cast<uint16_t>(pins & ~(m_board.m_switches[group] << 3));
		/* ADC data out comes with group 0, on bit 9 */
		if (!(m_board.m_port_a & 0x800) && !m_board.adc_out())
			pins = static_cast<uint16_t>(pins & ~0x200u);
		if (m_board.m_balls && (m_board.m_port_a >> 11) == 0x0E)
			for (unsigned ball = 0; ball < 2; ball++) {
				const uint16_t line = static_cast<uint16_t>(0x200u << ball);

				pins = static_cast<uint16_t>(m_board.ball_out(ball) ? pins | line : pins & ~line);
			}
	}
	return pins;
}

void AtvTrack::Bus::port_a_write(uint16_t pins)
{
	if (m_which != MASTER)
		return;
	m_board.m_port_a = pins;
	if (m_board.m_balls)
		m_board.ball_pins(pins);
	/* Bits 15-11 high select the ADC and the DAC */
	if ((pins & 0xF800) == 0xF800)
		m_board.adc_pins(pins);
	/* System control: bit 8 lets the slave run */
	if ((pins & 0xF000) == 0x7000 && (pins & 0x0100))
		m_board.m_slave_enabled = true;
}

/* ADC (ADC0834 or similar), bits 15-11 high: bit 6 chip select (low), 5
 * clock, 3 data in. Rising edges: start bit, single-ended, two channel bits
 * LSB first, one settle edge, then 8 sample bits MSB first. */
void AtvTrack::adc_pins(uint16_t pins)
{
	const bool selected = !(pins & 0x40), clock = pins & 0x20, data = pins & 0x08;

	if (!selected) {
		m_adc_step = 0;
		m_adc_command = 0;
	} else if (clock && !m_adc_clock) {
		if (m_adc_step < 4)
			m_adc_command |= static_cast<uint32_t>(data) << m_adc_step;
		else if (m_adc_step == 4)
			m_adc_sample = m_analog[(m_adc_command >> 2) & 3];
		m_adc_step++;
	}
	m_adc_clock = clock;
}

/* Ball sensors, read once a frame by 0x8C0A59A0: clock on bit 10 of group 0F
 * with bit 1 low, data on group 0E bits 9 and 10. HEURISTIC: the chips are
 * unknown. After the first rising edge low means ready; after the falling
 * edge high means a kick, taken that frame only (0x8C02F570) and as a button
 * press in a match (0x8C029F1E). Then 24 edges give three bytes LSB first,
 * and two end it: 28 in all. */
void AtvTrack::ball_pins(uint16_t pins)
{
	const unsigned group = pins >> 11;

	if (group == 0x0D || group == 0x0B || group == 0x07) {
		m_ball_edge = 0;
	} else if (group == 0x0F && !(pins & 2) && static_cast<bool>(pins & 0x400) != m_ball_clock) {
		m_ball_clock = pins & 0x400;
		m_ball_edge++;
	}
}

bool AtvTrack::ball_out(unsigned ball) const
{
	if (m_ball_edge == 2)
		return m_ball_kicked[ball];
	if (m_ball_edge < 3 || m_ball_edge >= 27)
		return m_ball_edge != 1;

	const unsigned bit = m_ball_edge - 3;

	return (m_ball[ball][bit / 8] >> (bit & 7)) & 1;
}

bool AtvTrack::adc_out() const
{
	if (m_adc_step < 5 || m_adc_step > 12)
		return true;
	return (m_adc_sample >> (12 - m_adc_step)) & 1;
}

} /* namespace odin */
