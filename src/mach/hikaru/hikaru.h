/* 
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

#ifndef __VK_HIKARU_H__
#define __VK_HIKARU_H__

#include "vk/mmap.h"
#include "odin/eeprom_93c46.hpp"
#include "odin/x76f100.hpp"
#include "odin/crypt_315_5881.hpp"
#include "odin/actel_m1.hpp"
#include "odin/scheduler.hpp"
#include "vk/cpu.h"
#include "vk/machine.h"

typedef struct {
	bool has_rom;
	unsigned eprom_bank[2];
	unsigned maskrom_bank[2];
	unsigned eeprom_bank;
	uint32_t eprom_bank_size;
	uint32_t maskrom_bank_size;
	bool maskrom_is_stretched;
	/* 315-5881 decryption chip and its 64 KB RAM on the ROM board */
	bool has_5881;
	uint32_t crypt_key;
	/* One bit per bank 0x10-0x4F: unpopulated socket already reported */
	uint64_t unpopulated_logged;
} hikaru_rombd_config_t;

typedef struct {
	vk_machine_t base;

	/* CPU (master) */
	vk_cpu_t *sh_m;
	vk_mmap_t *mmap_m;

	/* CPU (slave) */
	vk_cpu_t *sh_s;
	vk_mmap_t *mmap_s;

	vk_cpu_t *sh_current;

	/* The time of the machine, in SH-4 cycles, and the frame being
	 * drawn, see hikaru_begin_frame */
	odin::Scheduler *sched;
	unsigned line;
	unsigned dots_per_line, lines_per_frame, lines_shown;
	uint32_t line_rest;	/* of a cycle, in dot clocks */

	/* Devices */
	vk_device_t *memctl_m;
	vk_device_t *memctl_s;
	vk_device_t *mscomm;
	vk_device_t *mie;
	vk_device_t *aica_m;
	vk_device_t *aica_s;
	vk_device_t *gpu;

	/* Port A (master and slave) */
	uint16_t porta_m;
	uint8_t irq_lines;	/* see hikaru_update_irqs */
	uint8_t mie_port_d;	/* see hikaru_set_resets */
	/* 93C46 of the main board on port A bits 2 (DI), 3 (SK), 4 (CS), 5 (DO) */
	odin::Eeprom93c46 *eeprom_main;
	/* X76F100 of the ROM board, bus page eeprom_bank */
	odin::X76f100 *eeprom_rombd;
	/* RAM of the 315-5881, local bus 0x0A000000, boards with the chip only */
	vk_buffer_t *protram;
	odin::Crypt315_5881 *crypt;
	/* Security FPGA of the SGNASCAR ROM board, local bus 0x0A000000 */
	odin::ActelM1 *actel;
	unsigned porta_m_bit0_buffer;

	uint16_t porta_s;
	unsigned porta_s_bit1_buffer;

	/* Unknown Hardware (master) */
	uint32_t unk01000000_m;
	uint32_t unk01000100_m;

	/* Unknown Hardware (Slave) */
	uint32_t unk1A800008_s;
	uint32_t unk1B000100_s;

	/* RAM areas */
	vk_buffer_t *ram_m, *ram_s;
	vk_buffer_t *cmdram;
	vk_buffer_t *texram[2];
	vk_buffer_t *fb;
	vk_buffer_t *aica_ram_m;
	vk_buffer_t *aica_ram_s;
	vk_buffer_t *mie_ram;
	vk_buffer_t *bram;

	/* ROM data */
	vk_buffer_t *bootrom;
	vk_buffer_t *eprom;
	vk_buffer_t *maskrom;

	/* ROMBD configuration */
	hikaru_rombd_config_t rombd_config;

} hikaru_t;

vk_machine_t	*hikaru_new (vk_game_t *game);
/* The interrupt lines of the master SH-4 from those of the chips: a chip
 * calls it when its line changes. */
void		 hikaru_update_irqs (vk_machine_t *mach);
/* The pins of port D of the MIE, each time it drives them. */
void		 hikaru_set_resets (vk_machine_t *mach, uint8_t pins);

#endif /* __VK_HIKARU_H__ */
