/* 
 * Valkyrie
 * Copyright (C) 2011, Stefano Teso
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

#include <stdlib.h>

#include "vk/input.h"
#include "vk/games.h"

#include "mach/hikaru/hikaru.h"
#include "mach/hikaru/hikaru-mie.h"
#include "odin/mie_bridge.hpp"

/*
 * MIE (315-6146)
 * ==============
 *
 * A Z80 at 16 MHz with 2 KB of mask ROM, 34 KB of RAM, GPIO ports, a
 * 93C46 EEPROM on its port F/G pins and the JVS link to the 837-13551
 * I/O board. The SH-4 sees it through three windows:
 *
 * 00800000-0081FFFF  Z80 I/O ports, port n at 00800000 + 2n, even bytes
 *                    00800000 port A (unpopulated DIP switch)
 *                    0080000C port G (Test/Service, EEPROM DO)
 *                    00800010 port direction (= 0x43 after init)
 * 00820000-0083FFFF  Z80 memory, byte n at 00820000 + 2n, even bytes
 *                    0082F000 Z80 0x7800: the ROM jumps to 0x8010 when
 *                             bit 7 is set
 *                    00830000 Z80 0x8000: the program the BOOTROM
 *                             uploads (2316 bytes, ROM offset 0x40C48)
 *                    00838000 Z80 0xC000: mailbox
 * 01000000/01000100  bus request: bit 8 written requests the Z80 bus,
 *                    bit 8 read is clear once it is granted
 *
 * The chip lives in src/odin/mie.cpp, the board wiring in
 * src/odin/mie_bridge.cpp. That path is the default (it needs the
 * "mie" ROM section); ODIN_MIE=hle selects the stand-in below; the
 * default is the stand-in until the LLE path is validated.
 * ODIN_MIE_TRACE=<file> writes the Z80 bus trace of ODIN_MIE_TRACE_N
 * instructions (default 300000) from the first time the PC reaches
 * ODIN_MIE_TRACE_START (hex, default 0: from reset).
 */

typedef struct {
	vk_device_t base;
	odin_mie_t *chip;	/* LLE */
	vk_buffer_t *regs;	/* HLE: port page */
	uint32_t busrq;		/* HLE: bus request bit */
	FILE *trace;
} hikaru_mie_t;

/*
 * MIE HLE
 * =======
 *
 * The real MIE runs a Z80 program that the BOOTROM uploads into its RAM
 * (ROM offset 0x40C48, 2316 bytes). That program is not executed here:
 * the shared RAM is filled with what the program writes, as read from
 * its code and measured on a complete system. Stand-in for the LLE path
 * in src/odin/mie.cpp. Z80 addresses below; buffer index = address - 0x8000.
 *
 * C000  command from the SH-4: 1 = poll the JVS board, 2 = reset JVS
 * C001  busy flag, C002 JVS node count, C003 error bits
 * C004  stage, 3 = ready; C005 main board Test (bit 6) / Service (bit 7)
 * C100  record of JVS node 1 (837-13551), ID string at C17C
 * E000  switches, E100 coins, E200 analog, E300-E500 unused.
 *       Page format: [count][node][size][data...], size counts itself.
 */

#define MIE_C000	0x4000
#define MIE_C001	0x4001
#define MIE_C002	0x4002
#define MIE_C003	0x4003
#define MIE_C004	0x4004
#define MIE_C005	0x4005
#define MIE_C100	0x4100
#define MIE_E000	0x6000
#define MIE_E100	0x6100
#define MIE_E200	0x6200

static uint16_t get_mainbd_switches (void)
{
	return (vk_input_get_key (SDLK_F5) ? 8 : 0) | /* Test */
	       (vk_input_get_key (SDLK_F6) ? 4 : 0);  /* Service */
}

static void
mie_hle_put_bytes (vk_buffer_t *ram, unsigned idx, const uint8_t *p, unsigned n)
{
	unsigned i;
	for (i = 0; i < n; i++)
		vk_buffer_put (ram, 1, idx + i, p[i]);
}

static void
mie_hle_write_switches (vk_buffer_t *ram)
{
	uint8_t test = vk_input_get_key (SDLK_F5) ? 0x40 : 0;
	uint8_t service = vk_input_get_key (SDLK_F6) ? 0x80 : 0;
	vk_buffer_put (ram, 1, MIE_C005, test | service);
}

/* Input pages; no JVS control is wired to the host yet. */
static void
mie_hle_write_pages (vk_buffer_t *ram)
{
	static const uint8_t e000[8] = { 1, 1, 6, 0, 0, 0, 0, 0 };	/* 2 players x 2 bytes */
	static const uint8_t e100[7] = { 1, 1, 5, 0, 0, 0, 0 };	/* 2 coin slots */
	uint8_t e200[19] = { 1, 1, 17 };				/* 8 analog channels */
	unsigned i;

	for (i = 0; i < 8; i++) {
		e200[3 + 2 * i] = 0x80;
		e200[4 + 2 * i] = 0x00;
	}
	mie_hle_put_bytes (ram, MIE_E000, e000, sizeof (e000));
	mie_hle_put_bytes (ram, MIE_E100, e100, sizeof (e100));
	mie_hle_put_bytes (ram, MIE_E200, e200, sizeof (e200));
}

static void
mie_hle_write_node_record (vk_buffer_t *ram)
{
	static const uint8_t record[12] = {
		0x01,			/* present */
		0x11, 0x20, 0x10,	/* command, JVS and comm versions of the 837-13551 (MAME jvs13551.cpp) */
		0x02, 0x02, 0x0D, 0x02,	/* players, players, bits, bytes per player */
		0x02, 0x02,		/* coin slots */
		0x08, 0x08		/* analog channels */
	};
	static const char id[] = "SEGA ENTERPRISES,LTD.;I/O BD JVS;837-13551";

	mie_hle_put_bytes (ram, MIE_C100, record, sizeof (record));
	vk_buffer_put (ram, 1, MIE_C100 + 0x19, 6);	/* outputs */
	vk_buffer_put (ram, 1, MIE_C100 + 0x1A, 1);	/* output bytes */
	mie_hle_put_bytes (ram, MIE_C100 + 0x7C, (const uint8_t *) id, sizeof (id));
}

/* What the Z80 program has written once its JVS enumeration is done. */
static void
mie_hle_start (vk_buffer_t *ram)
{
	vk_buffer_put (ram, 1, MIE_C000, 0);
	vk_buffer_put (ram, 1, MIE_C001, 0);
	vk_buffer_put (ram, 1, MIE_C002, 1);
	vk_buffer_put (ram, 1, MIE_C003, 0);
	vk_buffer_put (ram, 1, MIE_C004, 3);
	mie_hle_write_switches (ram);
	mie_hle_write_node_record (ram);
	mie_hle_write_pages (ram);
}

static void
mie_hle_command (vk_buffer_t *ram, unsigned cmd)
{
	switch (cmd) {
	case 1:
		mie_hle_write_pages (ram);
		mie_hle_write_switches (ram);
		break;
	case 2:
		vk_buffer_put (ram, 1, MIE_C002, 1);
		vk_buffer_put (ram, 1, MIE_C003, 0);
		vk_buffer_put (ram, 1, MIE_C004, 3);
		break;
	default:
		break;
	}
	vk_buffer_put (ram, 1, MIE_C000, 0);
	vk_buffer_put (ram, 1, MIE_C001, 0);
}

static int
hikaru_mie_hle_get (hikaru_mie_t *mie, unsigned size, uint32_t addr, void *val)
{
	vk_device_t *dev = (vk_device_t *) mie;

	set_ptr (val, size, 0);
	if (addr >= 0x00800000 && addr <= 0x00800014) {
		uint16_t *val16 = (uint16_t *) val, tmp;
		if (size != 2)
			return -1;
		switch (addr & 0xFF) {
		case 0x00:
		case 0x06:
		case 0x0A:
			/* no-op */
			break;
		case 0x08:
			/* XXX hack: passes the check at @0C00B860 in all the
			 * BOOTROM versions */
			tmp = vk_buffer_get (mie->regs, 2, 0x08);
			vk_buffer_put (mie->regs, 2, 0x08, tmp ^ 0xF);
			break;
		case 0x0C:
			vk_buffer_put (mie->regs, 2, 0x0C, 0xFFFF & ~get_mainbd_switches ());
			break;
		default:
			return -1;
		}
		*val16 = vk_buffer_get (mie->regs, 2, addr & 0x1F);
	} else if (addr == 0x0082F000) {
		return 0;
	} else if (addr >= 0x00830000 && addr <= 0x0083FFFF) {
		set_ptr (val, size, vk_buffer_get (((hikaru_t *) dev->mach)->mie_ram, 1, (addr / 2) & 0x7FFF));
	} else if (addr >= 0x01000000 && addr <= 0x010001FF) {
		/* Bus request: granted at once, nothing runs here. */
		set_ptr (val, size, (mie->busrq ? 0 : 0x100) | 0x600);
	} else
		return -1;
	return 0;
}

static int
hikaru_mie_hle_put (hikaru_mie_t *mie, unsigned size, uint32_t addr, uint64_t val)
{
	vk_device_t *dev = (vk_device_t *) mie;

	if (addr >= 0x00800000 && addr <= 0x00800014) {
		if (size != 2)
			return -1;
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
			return -1;
		}
	} else if (addr == 0x0082F000) {
		if (val & 0x80)
			mie_hle_start (((hikaru_t *) dev->mach)->mie_ram);
		return 0;
	} else if (addr >= 0x00830000 && addr <= 0x0083FFFF) {
		vk_buffer_t *ram = ((hikaru_t *) dev->mach)->mie_ram;
		unsigned idx = (addr / 2) & 0x7FFF;

		vk_buffer_put (ram, 1, idx, val);
		if (idx == MIE_C000 && val != 0)
			mie_hle_command (ram, val);
		else if (idx == MIE_C001)
			vk_buffer_put (ram, 1, MIE_C001, 0);	/* never busy */
	} else if (addr >= 0x01000000 && addr <= 0x010001FF) {
		mie->busrq = val & 0x100;
	} else
		return -1;
	return 0;
}

/*
 * MIE LLE
 * =======
 */

/* The cabinet side: main board switches on F5/F6, nothing yet on the
 * I/O board connectors. */
static void
hikaru_mie_read_inputs (void *ctx, odin_mie_inputs_t *in)
{
	unsigned i;

	(void) ctx;
	in->mainboard_test = vk_input_get_key (SDLK_F5);
	in->mainboard_service = vk_input_get_key (SDLK_F6);
	in->jvs_system = 0;
	in->jvs_player[0] = 0;
	in->jvs_player[1] = 0;
	for (i = 0; i < 8; i++)
		in->jvs_analog[i] = 0x8000;
	in->jvs_coin[0] = 0;
	in->jvs_coin[1] = 0;
}

static int
hikaru_mie_get (vk_device_t *dev, unsigned size, uint32_t addr, void *val)
{
	hikaru_mie_t *mie = (hikaru_mie_t *) dev;
	uint32_t value = 0;

	if (!mie->chip)
		return hikaru_mie_hle_get (mie, size, addr, val);

	if (addr >= 0x00800000 && addr <= 0x0083FFFF) {
		if (odin_mie_host_read (mie->chip, addr - 0x00800000, size, &value)) {
			VK_MACH_ERROR (dev->mach, "MIE unhandled R%u %08X", size*8, addr);
			set_ptr (val, size, 0);
			return -1;
		}
	} else if (addr >= 0x01000000 && addr <= 0x010001FF) {
		value = odin_mie_busrq_read (mie->chip);
	} else {
		set_ptr (val, size, 0);
		return -1;
	}
	set_ptr (val, size, value);
	return 0;
}

static int
hikaru_mie_put (vk_device_t *dev, unsigned size, uint32_t addr, uint64_t val)
{
	hikaru_mie_t *mie = (hikaru_mie_t *) dev;

	if (!mie->chip)
		return hikaru_mie_hle_put (mie, size, addr, val);

	if (addr >= 0x00800000 && addr <= 0x0083FFFF) {
		if (odin_mie_host_write (mie->chip, addr - 0x00800000, size, (uint32_t) val)) {
			VK_MACH_ERROR (dev->mach, "MIE unhandled W%u %08X = %lX", size*8, addr, val);
			return -1;
		}
	} else if (addr >= 0x01000000 && addr <= 0x010001FF) {
		odin_mie_busrq_write (mie->chip, (uint32_t) val);
	} else
		return -1;
	return 0;
}

static int
hikaru_mie_exec (vk_device_t *dev, int cycles)
{
	hikaru_mie_t *mie = (hikaru_mie_t *) dev;

	odin_mie_run (mie->chip, (uint32_t) cycles);
	return cycles;
}

static void
hikaru_mie_reset (vk_device_t *dev, vk_reset_type_t type)
{
	hikaru_mie_t *mie = (hikaru_mie_t *) dev;

	(void) type;
	if (mie->chip) {
		odin_mie_reset (mie->chip);
		return;
	}
	vk_buffer_clear (mie->regs);
	vk_buffer_put (mie->regs, 2, 0x00, 0xFFFF);
	mie->busrq = 0;
}

static void
hikaru_mie_destroy (vk_device_t **dev_)
{
	if (dev_ && *dev_) {
		hikaru_mie_t *mie = (hikaru_mie_t *) *dev_;
		if (mie->chip)
			odin_mie_delete (mie->chip);
		mie->chip = NULL;
		if (mie->trace)
			fclose (mie->trace);
		mie->trace = NULL;
	}
}

static void
hikaru_mie_setup_trace (hikaru_mie_t *mie)
{
	const char *path = getenv ("ODIN_MIE_TRACE");
	const char *count = getenv ("ODIN_MIE_TRACE_N");
	const char *start = getenv ("ODIN_MIE_TRACE_START");
	unsigned n = count ? (unsigned) atoi (count) : 300000;
	unsigned start_pc = start ? (unsigned) strtoul (start, NULL, 16) : 0;

	if (!path || !n)
		return;
	mie->trace = fopen (path, "w");
	if (!mie->trace) {
		VK_ERROR ("MIE: can't open trace file '%s'", path);
		return;
	}
	odin_mie_set_trace (mie->chip, mie->trace, n, (uint16_t) start_pc);
}

vk_device_t *
hikaru_mie_new (vk_machine_t *mach, unsigned clock_hz)
{
	hikaru_t *hikaru = (hikaru_t *) mach;
	hikaru_mie_t *mie;
	vk_device_t *dev;
	vk_buffer_t *rom = NULL;
	const char *mode = getenv ("ODIN_MIE");
	/* The real chip unless ODIN_MIE=hle asks for the stand-in */
	bool lle = !mode || strcmp (mode, "hle");

	VK_DEVICE_ALLOC (mie, mach);
	dev = (vk_device_t *) mie;
	if (!mie)
		return NULL;

	dev->destroy	= hikaru_mie_destroy;
	dev->reset	= hikaru_mie_reset;
	dev->exec	= NULL;
	dev->get	= hikaru_mie_get;
	dev->put	= hikaru_mie_put;
	dev->save_state	= NULL;
	dev->load_state	= NULL;

	mie->regs = vk_buffer_le32_new (0x20, 0);
	if (!mie->regs)
		goto fail;

	vk_machine_register_buffer (mach, mie->regs);

	if (mach->game)
		rom = vk_game_get_section_data (mach->game, "mie");

	if (lle && !rom) {
		VK_ERROR ("MIE: no 315-6146.bin in the game list, using the HLE stand-in");
		lle = false;
	}

	if (lle) {
		mie->chip = odin_mie_new (vk_buffer_get_ptr (rom, 0),
		                          vk_buffer_get_size (rom),
		                          vk_buffer_get_ptr (hikaru->mie_ram, 0),
		                          vk_buffer_get_size (hikaru->mie_ram),
		                          clock_hz, hikaru_mie_read_inputs, mie);
		if (!mie->chip)
			goto fail;
		dev->exec = hikaru_mie_exec;
		odin_mie_set_log (mie->chip, stderr);
		hikaru_mie_setup_trace (mie);
		VK_PRINT ("MIE: LLE (315-6146 ROM + uploaded Z80 program)");
	} else
		VK_PRINT ("MIE: HLE stand-in (ODIN_MIE=lle for the Z80)");

	return dev;

fail:
	vk_device_destroy (&dev);
	return NULL;
}
