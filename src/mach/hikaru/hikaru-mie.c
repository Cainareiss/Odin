/* 
 * Valkyrie
 * Copyright (C) 2011, Stefano Teso
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

#include "vk/input.h"

#include "mach/hikaru/hikaru.h"
#include "mach/hikaru/hikaru-mie.h"

/*
 * MIE
 * ===
 *
 * Includes a Z80 processor clocked at ? MHz.
 *
 * MME Ports
 * =========
 *
 * 00800000  16-bit RW	Unknown \ Related
 * 00800002  16-bit  W	Unknown /
 * 00800004  16-bit  W	Unknown; = 0000
 * 00800006  16-bit RW	MIE Control
 *			bit 1 is accessed in @0C00BDFC; turn ON/OFF [active low]
 *			bit 5 is accessed in @0C007880, @0C002AB8; lock/unlock BRAM? see @0C002C4A
 *			bit 8 is accessed in @0C0078F8, related to hi/lo-res video mode
 * 00800008  16-bit RW	Unknown
 *			bits 0-3 are read (and looped if == 0) in @0C00BDFC
 *			Some kind of ready bits
 * 0080000A  16-bit RW	Serial Port; = 007F
 * 0080000C  16-bit RW	Serial Port; = FFFF; plus MAINBD Switches
 * 00800010  16-bit  W	Unknown; = 0043
 *
 * Basic setup is done in @0C001956. Most bits seem active-low, with the
 * exception of 00800004. Access to register is guarded either by 01000000 or
 * 01000100.
 *
 * 0082F000   8-bit RW	MIE Z80 Control; 0x80 is the Z80 reset/start bit
 *
 * 00830000-0083FFFF  8,16-bit RW	MIE RAM: even bytes only [2]
 *
 * Note: airtrix polls 0083800[02]
 *
 * [1] 7800 in Z80 space
 * [2] 8000-FFFF in Z80 space
 *
 * MIE Serial Bus
 * ==============
 *
 * Perhaps queries devices attached to the (MAPLE?) bus?
 */

typedef struct {
	vk_device_t base;
	vk_buffer_t *regs;
} hikaru_mie_t;

static uint16_t get_mainbd_switches (void)
{
	return (vk_input_get_key (SDLK_F5) ? 8 : 0) | /* Test */
	       (vk_input_get_key (SDLK_F6) ? 4 : 0);  /* Service */
}

/*
 * MIE HLE
 * =======
 *
 * The real MIE runs a Z80 program that the BOOTROM uploads into its RAM
 * (ROM offset 0x40C48, 2316 bytes). That program is not executed here:
 * the shared RAM is filled with what the program writes, as read from
 * its code and measured on a complete system. Stand-in until the Z80
 * is emulated. Z80 addresses below; buffer index = address - 0x8000.
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
		0x13, 0x30, 0x10,	/* command, JVS and comm versions */
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
hikaru_mie_get (vk_device_t *dev, unsigned size, uint32_t addr, void *val)
{
	hikaru_mie_t *mie = (hikaru_mie_t *) dev;

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
	} else
		return -1;
	return 0;
}

static int
hikaru_mie_put (vk_device_t *dev, unsigned size, uint32_t addr, uint64_t val)
{
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
	} else
		return -1;
	return 0;
}

static void
hikaru_mie_reset (vk_device_t *dev, vk_reset_type_t type)
{
	hikaru_mie_t *mie = (hikaru_mie_t *) dev;

	vk_buffer_clear (mie->regs);
	vk_buffer_put (mie->regs, 2, 0x00, 0xFFFF);
}

vk_device_t *
hikaru_mie_new (vk_machine_t *mach)
{
	hikaru_mie_t *mie;
	vk_device_t *dev;

	VK_DEVICE_ALLOC (mie, mach);
	dev = (vk_device_t *) mie;
	if (!mie)
		return NULL;

	dev->destroy	= NULL;
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

	return dev;

fail:
	vk_device_destroy (&dev);
	return NULL;
}
