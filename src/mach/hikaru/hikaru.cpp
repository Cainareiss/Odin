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

/* 
 * SEGA Hikaru
 *
 * Specs
 * =====
 *
 * Taken from system16.com
 *
 * CPU			2 x Hitachi SH-4 @ 200 MHz
 * Graphic Engine	Sega Custom 3D 
 * Sound Engine		2 x ARM7 Yamaha AICA @ 45 MHz + ARM7, 64 channel ADPCM
 * Main Memory		64 MB
 * Graphic Memory	28 MB
 * Sound Memory		8 MB [per AICA board]
 * Media		ROM Board (max 352 MB)
 * Colors		24bit
 * Resolution		24 KHz 496x384, 31 KHz 640x480 
 * Shading		Flat, Linear, Phong
 * Lighting		Horizontal, Spot
 *			1024 lights per scene
 *			4 lights per polygon
 *			8 window surfaces
 * Effects		Fog, Depth Queueing, Stencil, Shadow, Motion blur
 * Others Capabilties	Bitmap Layer x 2
 *			Calendar [Note: AICA RTC]
 *			Dual Monitor (24 kHz)
 * Extensions		communication, 4 channel audio, PCI, MIDI, RS-232C 
 * Connection		Jamma Video compliant
 *
 *
 * ICs
 * ===
 *
 * According to the RAM test:
 *
 * 15,16,17S,18S = @0C000000 RAM (32 MB), Master RAM
 * 22,23,24S,25S = @16000000 RAM (32 MB), Slave RAM
 * 38 39S        = @14000000 RAM (4 MB), CMDRAM
 * 41            = @16000000 RAM (4 MB), TEXRAM Bank 0
 * 42            = @16000000 RAM (4 MB), TEXRAM Bank 1
 * 44,45S,46,47S = @1B000000 RAM (8 MB), FB
 * 33,34S,35,36S = @........ RAM (8 MB), Unknown (not tested by the RAM test)
 * 91S,92S       = @00C00000 CMOS SRAM (64 KB), Backup RAM
 * 98            = @02800000 8 MB SDRAM (main AICA board)
 *
 *
 * BIOSes
 * ======
 *
 * Currently three different bios revisions are known: 0.84, 0.92, and 0.96
 * (see the MAME driver.)
 *
 * Any IRQ controller in there pumping stuff from the various devices to
 * the master SH-4 IRL pins and Port A?
 *
 * The master SH-4 is configured for external-request DMAC (DTD). Requests
 * are sent either to the master SH-4 or the slave SH-4.
 *
 * TMU Channel 2 is configured for input capture; the DMAC is automatically
 * activeted whenever the interrupt fires. Timed DMA, way to go!
 *
 *
 * EEPROMs
 * =======
 *
 * The Hikaru hosts a few EEPROMs:
 *  - One on the MAINBD, connected to the master SH4 Port A.
 *  - One on the ROMBD, which likely holds game-specific data and protection.
 *  - One on the INPTBD, at 0800000[AC], unknown usage.
 *
 *
 * MAINBD EEPROM
 * =============
 *
 * The MAINBD EEPROM is a serial 128 x 8-bit EEPROM. It is interfaced to bits
 * 2-5 of PDTRA. The port is configured as follows:
 *
 *  PCTRA = (PCTRA & ~FF0) | 950
 *  PDTRA = PDTRA & ~3C
 *
 * See @0C00BF5C.
 *
 *  bit 2	Output		[configured as: pulled up, output]
 *  bit 3	Clock		[configured as: pulled up, output]
 *  bit 4	Chip Select	[configured as: pulled up, output]
 *  bit 5	Input		[configured as: not pulled up, input]
 *
 * See @0C0067AC, @0C0067DC.
 *
 *
 * ROMBD EEPROM, Type 1
 * ====================
 *
 * Located at 03000000, in bank_base + 0x14. It's a serial EEPROM of size
 * 128 x 8-bit entries. An 9C346. Bits (active low):
 *
 *  bit 0	Output
 *  bit 1	Clock
 *  bit 2	Chip Select
 *  bit 3	Input
 *
 * See @0C00C27E, @0C00C2AE
 *
 *
 * ROMBD EEPROM, Type 2
 * ====================
 *
 * An 76X100 secure EEPROM.
 *
 * MIE EEPROM
 * ==========
 *
 * See hikaru-mie.c
 */

#include <span>

#include "vk/core.h"
#include "vk/games.h"

#include "cpu/sh/sh4.h"

#include "mach/hikaru/hikaru.h"
#include "mach/hikaru/hikaru-memctl.h"
#include "mach/hikaru/hikaru-mscomm.h"
#include "mach/hikaru/hikaru-mie.h"
#include "mach/hikaru/hikaru-aica.h"
#include "mach/hikaru/hikaru-gpu.h"
#include "mach/hikaru/hikaru-renderer.h"

/*
 * Port A
 * ======
 *
 * The master SH-4 port A is used for a variety of things. The bitmask is
 * as follows:
 *
 * [M] xxxx xxii iiee eexM
 *
 * x = Unused
 * i = IRQ causes
 * e = MAINBD EEPROM
 * M = master-to-slave communication
 *
 * Bit M is the NMI pin of the slave SH-4: the master pulls it low for three
 * writes and the slave, at each interrupt, clears the NMI flag of its DMAC
 * as the hardware manual asks (read DMAOR, write 0x8001, @0C002000). MAME
 * names the pin IRL3 instead (hikaru.cpp, main SH4 port A).
 *
 * [S] xxxx xxxx xxxx xxSx
 *
 * S = slave-to-master communication?
 *
 * Note: GPIOIC is never access by the bootrom.
 */

/* These functions get called by the SH-4 core */

/* The 315-5881 reads its input from the ROM board RAM, one byte-swapped
 * word at a time (MAME hikaru.cpp prot_ram_r). */
static uint16_t
crypt_read_protram (void *ctx, uint32_t addr)
{
	hikaru_t *hikaru = (hikaru_t *) ctx;
	uint16_t w = vk_buffer_get (hikaru->protram, 2, (addr & 0x7FFF) * 2);
	return (uint16_t) ((w >> 8) | (w << 8));
}

static int
porta_get_m (sh4_t *ctx, uint16_t *val)
{
	vk_device_t *dev = (vk_device_t *) ctx;
	hikaru_t *hikaru = (hikaru_t *) dev->mach;
	const uint8_t lines = hikaru->irq_lines;
	/* Bit 5 is the EEPROM DO pin (MAME hikaru.cpp main_io_r) */
	/* Bits 6 to 9 are the lines of hikaru_update_irqs, low while they
	 * ask; nothing is known on the other pins, pulled up. */
	*val = 0xFC1F |
	       (hikaru->eeprom_main->do_read () ? 0x20 : 0) |
	       ((lines & 0x01) ? 0 : 0x040) |	/* ANT_INT */
	       ((lines & 0x10) ? 0 : 0x080) |	/* SZK_INT */
	       ((lines & 0x20) ? 0 : 0x100) |	/* CN4_INT */
	       ((lines & 0x80) ? 0 : 0x200);	/* CN3_INT1 */
	return 0;
}

static int
porta_get_s (sh4_t *ctx, uint16_t *val)
{
	vk_device_t *dev = (vk_device_t *) ctx;
	hikaru_t *hikaru = (hikaru_t *) dev->mach;
	*val = hikaru->porta_s;
	return 0;
}

static int
porta_put_m (sh4_t *ctx, uint16_t val)
{
	vk_device_t *dev = (vk_device_t *) ctx;
	hikaru_t *hikaru = (hikaru_t *) dev->mach;
	hikaru->porta_m = val;

	/* 93C46: bit 2 DI, bit 3 SK, bit 4 CS (MAME hikaru.cpp main_io_w) */
	hikaru->eeprom_main->set_pins ((val >> 4) & 1, (val >> 3) & 1, (val >> 2) & 1, false);

	/* The NMI of the slave comes with the falling edge (its ICR.NMIE is
	 * clear). */
	sh4_set_nmi_pin (hikaru->sh_s, val & 1);

	return 0;
}

static int
porta_put_s (sh4_t *ctx, uint16_t val)
{
	vk_device_t *dev = (vk_device_t *) ctx;
	hikaru_t *hikaru = (hikaru_t *) dev->mach;
	hikaru->porta_s = val;
	/* Bit 0 is the line of the slave to the master, on IRL3 for MAME
	 * (hikaru.cpp, slave SH4 port A); the games leave it high. */
	if (!(val & 1))
		VK_ABORT ("the slave SH-4 pulls its line to the master, which is not implemented");
	return 0;
}

/* Unknown devices in the slave address space */

static int
unk_s_get (vk_device_t *dev, unsigned size, uint32_t addr, void *val)
{
	hikaru_t *hikaru = (hikaru_t *) dev->mach;
	uint16_t *val16 = (uint16_t *) val;
	uint32_t *val32 = (uint32_t *) val;

	switch (addr) {
	case 0x14000800:
		/* Controlled by 04000010 and 0400001C */
		VK_ASSERT (size == 4);
		*val32 = 0x17C311DB; /* SEGA PCI ID #2 */
		break;
	case 0x1A800008:
		VK_ASSERT (size == 2);
		*val16 = hikaru->unk1A800008_s;
		break;
	case 0x1B000100:
		VK_ASSERT (size == 2);
		*val16 = hikaru->unk1B000100_s;
		break;
	default:
		return -1;
	}
	return 0;
}

static int
unk_s_put (vk_device_t *dev, unsigned size, uint32_t addr, uint64_t val)
{
	hikaru_t *hikaru = (hikaru_t *) dev->mach;

	switch (addr) {
	case 0x1400080D:
		/* Controlled by 04000010 and 0400001C */
		VK_ASSERT (size == 1);
		break;
	case 0x14000804:
	case 0x14000810:
	case 0x14000814:
	case 0x14000818:
	case 0x1400081C:
		/* Controlled by 04000010 and 0400001C */
		VK_ASSERT (size == 4);
		break;
	case 0x1A800008:
		VK_ASSERT (size == 2);
		hikaru->unk1A800008_s = val;
		break;
	case 0x1B000100:
		/* This is a semaphore, akin to 01000000 and 01000100 in
		 * the master address space. */
		VK_ASSERT (size == 2);
		hikaru->unk1B000100_s = val ^ 0x100;
		break;
	default:
		return -1;
	}
	return 0;
}

/* The window of the slave on the MIE: 1A800008 is among its ports and
 * 1B000100 asks for its bus, as 01000100 does for the master; the slave
 * checks the MIE through it at start (@0C00BE9A). HACK: a stand-in, the
 * bus is given at once and the port keeps what was written. 14000800 is
 * the configuration space of the command processor, read through the bridge
 * of the slave. */
static vk_device_t unk_s = {
	.get = unk_s_get,
	.put = unk_s_put
};

/*
 * IRQs
 * ====
 *
 * The master SH-4 is configured for independent per-pin external IRQs. It
 * looks like that the IRQ sources are as follows:
 *
 *  IRL0 unused/unhandled
 *  IRL1 memory controller DMA termination
 *  IRL2 the interrupt controller of the board, see below
 *  IRL3 unused/unhandled
 *
 * IRQ-related Registers
 * =====================
 *
 * The following registers are used in the IRL2 handling routine to check
 * what happened, see @0C00174C.
 *
 * PDTRA (active low):
 *
 *  0040	IRQ source is GPU
 *  0080	IRQ source is AICA/DMA
 *  0100	IRQ source is UNKNOWN [slave?], calls @0C000A30
 *  0200	Error
 *
 * For more informations on the GPU IRQs, see hikaru-gpu.c
 *
 * The interrupt controller
 * ========================
 *
 * Five lines, named as on the schematics (MAME hikaru.cpp, MIE GPIO
 * ports), go to port B of the MIE, and four of them to port A of the
 * master as well:
 *
 *  ANT_INT	the command processor	port B bit 0	PDTRA 0040
 *  SZK_INT	the sound boards	port B bit 4	PDTRA 0080
 *  CN4_INT	connector CN4		port B bit 5	PDTRA 0100
 *  CN3_INT0	connector CN3		port B bit 6
 *  CN3_INT1	connector CN3		port B bit 7	PDTRA 0200
 *
 * The MIE holds IRL2 while one of them passes the mask its program writes
 * to port C (0xE1 in every game: the sound boards do not interrupt). Each
 * is a level, held until its handler has acknowledged the chip it comes
 * from. Nothing is on CN3 and CN4.
 *
 * HEURISTIC: the second sound board is taken to share SZK_INT with the
 * first, whose handler acknowledges both (PHARRIER @0C0102F2).
 */

void
hikaru_update_irqs (vk_machine_t *mach)
{
	hikaru_t *hikaru = (hikaru_t *) mach;
	uint8_t lines = 0;

	/* A chip may call while the machine is being built. While it is
	 * reset the lines wait for hikaru_reset: the registers of the SH-4
	 * are cleared first, and a chip that is not reset yet may still
	 * ask. */
	if (!hikaru->sh_m || mach->resetting)
		return;

	if (hikaru_gpu_irq_line (hikaru->gpu))
		lines |= 0x01;
	if (hikaru_aica_irq_line (hikaru->aica_m) ||
	    hikaru_aica_irq_line (hikaru->aica_s))
		lines |= 0x10;
	hikaru->irq_lines = lines;
	hikaru_mie_set_irq_lines (hikaru->mie, lines);

	vk_cpu_set_irq_state (hikaru->sh_m, SH4_IESOURCE_IRL1,
	                      hikaru_memctl_irq_line (hikaru->memctl_m) ?
	                      VK_IRQ_STATE_RAISED : VK_IRQ_STATE_CLEAR);
	vk_cpu_set_irq_state (hikaru->sh_m, SH4_IESOURCE_IRL2,
	                      hikaru_mie_irq_out (hikaru->mie) ?
	                      VK_IRQ_STATE_RAISED : VK_IRQ_STATE_CLEAR);
}

/*
 * Resets
 * ======
 *
 * Port D of the MIE holds the reset lines of the board, active low (MAME
 * hikaru.cpp, MIE GPIO ports): bit 0 the system, 1 the graphics chips
 * (GP_RES), 2 the slave SH-4, 3 CN4, 4 the display chip (PCI1_RES), 5 the
 * sound boards; bits 6 and 7 choose the video clock. The master writes the
 * port through the window of the MIE:
 *
 *  - the BOOTROM, as soon as the MIE gives its bus (@0C00BE52): 0xFB, then
 *    0xFF some 250 cycles later, for the slave;
 *  - the system library of the game (0xDF, then 0xFF): the sound boards;
 *  - VON4, when two lists in a row did not end (@0C711C30): 0xEF, 0xFD,
 *    then 0xFF, for the graphics chips, see HikaruGpu::set_reset_lines.
 *    Here it does each time its SYSTEM MENU is left with EXIT, see the
 *    TODO.
 *
 * The reset of the slave is NOT done. The slave takes a frame and a half
 * to start again (it clears its memory) and then clears the word where
 * the master leaves its first job (14000000); the master writes that word
 * without waiting for anything (PHARRIER @0C010BC4), after a copy of the
 * game from the ROM board that takes no time here. With the reset done,
 * BRAVEFF and PHARRIER write the word a frame too early and never start.
 */

/* Bits 1 and 4 of port D, to the graphics chips */
static void
hikaru_set_gpu_reset_lines (hikaru_t *hikaru)
{
	hikaru_gpu_set_reset_lines (hikaru->gpu, !(hikaru->mie_port_d & 0x02),
	                            !(hikaru->mie_port_d & 0x10));
}

void
hikaru_set_resets (vk_machine_t *mach, uint8_t pins)
{
	hikaru_t *hikaru = (hikaru_t *) mach;
	const uint8_t fell = hikaru->mie_port_d & ~pins;

	hikaru->mie_port_d = pins;

	if ((pins & 0xC0) != 0xC0)
		VK_ABORT ("video clock %u chosen on port D of the MIE: only 3 is known", pins >> 6);
	if (~pins & 0x09)
		VK_ABORT ("resets %02X asked on port D of the MIE, which are not implemented", ~pins & 0x09);

	hikaru_set_gpu_reset_lines (hikaru);

	/* The boards are held as long as the line is low, see
	 * hikaru_run_cycles */
	if (fell & 0x20) {
		hikaru_aica_reset_pin (hikaru->aica_m);
		hikaru_aica_reset_pin (hikaru->aica_s);
		hikaru_update_irqs (mach);
	}
}

/* SH7091 at 200 MHz (Hikaru: 2 x SH-4 @ 200 MHz, see the header of this
 * file). VK_SH4_MHZ lowers it for debugging only: the interpreter is the
 * bottleneck and every phase of a game then takes fewer host seconds. */
static unsigned
hikaru_sh4_clock (void)
{
	static unsigned clock = 0;
	if (!clock) {
		const char *env = getenv ("VK_SH4_MHZ");
		unsigned mhz = env ? (unsigned) atoi (env) : 200;
		if (mhz < 1 || mhz > 200)
			mhz = 200;
		clock = mhz * MHZ;
	}
	return clock;
}
#define HIKARU_SH4_CLOCK (hikaru_sh4_clock ())
/* #define cycles_per_line (HIKARU_SH4_CLOCK / (60 * 480)) */

static void
hikaru_run_cycles (vk_machine_t *mach, int cycles)
{
	hikaru_t *hikaru = (hikaru_t *) mach;

	/* Run the master */
	hikaru->sh_current = hikaru->sh_m;
	vk_cpu_run (hikaru->sh_m, cycles);

	/* Run the slave */
	hikaru->sh_current = hikaru->sh_s;
	vk_cpu_run (hikaru->sh_s, cycles);

	hikaru->eeprom_main->tick ((uint32_t) cycles);

	/* Run the MEMCTL and GPU */
	vk_device_exec (hikaru->memctl_m, cycles);
	/* The MEMCTL of the slave is not run (HEURISTIC, as Valkyrie had
	 * it: no game is known to start a transfer there). */
	vk_device_exec (hikaru->gpu, cycles);

	/* Run the MIE (no-op on the HLE stand-in) */
	vk_device_exec (hikaru->mie, cycles);
	/* Its program may have written the mask of the interrupts */
	hikaru_update_irqs (mach);

	/* Run the two sound boards, unless their reset line is low */
	if (hikaru->mie_port_d & 0x20) {
		vk_device_exec (hikaru->aica_m, cycles);
		vk_device_exec (hikaru->aica_s, cycles);
	}
}

/* What the scheduler dates. Those due at the same time are taken in this
 * order. */
enum {
	EVENT_LINE,		/* the end of a line */
};

/* The dot clock of the 31 kHz screen, which draws the 818 dots of a line.
 * HEURISTIC: the value of MAME (hikaru.cpp, 51894580 / 2), which does not
 * say where it comes from; no game sets or reads it. It gives 60.08 frames
 * a second. */
#define DOT_CLOCK	25947290

/* The cycles of the next line. A line is not a whole number of cycles: the
 * rest is kept. */
static uint64_t
hikaru_line_cycles (hikaru_t *hikaru)
{
	const uint64_t n = (uint64_t) hikaru->dots_per_line * HIKARU_SH4_CLOCK +
	                   hikaru->line_rest;

	hikaru->line_rest = (uint32_t) (n % DOT_CLOCK);
	return n / DOT_CLOCK;
}

/* Starts a frame with the lines the game has set by now. The line counter
 * of the GPU is 0 on the first line shown (PHARRIER waits for 240 to 479,
 * @0C01C106): a frame goes from that line to the end of the blanking. */
static void
hikaru_begin_frame (hikaru_t *hikaru)
{
	hikaru_gpu_get_frame_size (hikaru->gpu, &hikaru->dots_per_line,
	                           &hikaru->lines_per_frame, &hikaru->lines_shown);
	if (hikaru->dots_per_line != 818)
		VK_ABORT ("a screen of %u dots per line: only the 31 kHz one (818) is known",
		          hikaru->dots_per_line);
	if (hikaru->lines_shown == 0 || hikaru->lines_shown >= hikaru->lines_per_frame)
		VK_ABORT ("a screen of %u lines with %u shown",
		          hikaru->lines_per_frame, hikaru->lines_shown);

	hikaru->line = 0;
	odin_scheduler_set (hikaru->sched, EVENT_LINE,
	                    odin_scheduler_now (hikaru->sched) + hikaru_line_cycles (hikaru));
}

static int
hikaru_run_frame (vk_machine_t *mach)
{
	hikaru_t *hikaru = (hikaru_t *) mach;
	bool done = false;

	VK_LOG (" *** VBLANK-OUT %s ***", vk_machine_get_debug_string (mach));

	/* The chips run up to the next event, then the events due are
	 * taken. */
	while (!done) {
		const uint64_t next = odin_scheduler_next (hikaru->sched);
		int event;

		hikaru_run_cycles (mach, (int) (next - odin_scheduler_now (hikaru->sched)));
		odin_scheduler_advance (hikaru->sched, next);

		while ((event = odin_scheduler_take (hikaru->sched)) >= 0) {
			VK_ASSERT (event == EVENT_LINE);

			hikaru_gpu_hblank_in (hikaru->gpu, hikaru->line++);
			if (hikaru->line == hikaru->lines_shown) {
				VK_LOG (" *** VBLANK-IN  %s ***", vk_machine_get_debug_string (mach));
				hikaru_gpu_vblank_in (hikaru->gpu);
			}
			if (hikaru->line < hikaru->lines_per_frame) {
				odin_scheduler_set (hikaru->sched, EVENT_LINE,
				                    next + hikaru_line_cycles (hikaru));
				continue;
			}

			/* this may actually be an hblank-out IRQ */
			hikaru_gpu_vblank_out (hikaru->gpu);

			done = true;
		}
	}
	hikaru_begin_frame (hikaru);

	return 0;
}

static void
hikaru_reset (vk_machine_t *mach, vk_reset_type_t type)
{
	hikaru_t *hikaru = (hikaru_t *) mach;

	vk_cpu_set_state (hikaru->sh_s, VK_CPU_STATE_RUN);

	/* Port A's are active low */
	hikaru->porta_m = 0xFFFF;
	hikaru->porta_s = 0xFFFF;
	/* HEURISTIC: nothing is known to pull the NMI pin of the master. */
	sh4_set_nmi_pin (hikaru->sh_m, true);
	sh4_set_nmi_pin (hikaru->sh_s, true);
	hikaru->mie_port_d = 0xFF;
	hikaru_set_gpu_reset_lines (hikaru);

	hikaru->unk01000000_m = 0;
	hikaru->unk01000100_m = 0;
	hikaru->unk1A800008_s = 0xFFFF;
	hikaru->unk1B000100_s = 0xFEFF;

	if (hikaru->actel)
		odin_actel_m1_reset (hikaru->actel);

	odin_scheduler_reset (hikaru->sched);
	hikaru->line_rest = 0;
	hikaru_begin_frame (hikaru);

	hikaru_update_irqs (mach);
}

static const char *
hikaru_get_debug_string (vk_machine_t *mach)
{
	hikaru_t *hikaru = (hikaru_t *) mach;
	static char out[256];
	char *mstr, *sstr;
	const char *gpustr;

	mstr = strdup (vk_cpu_get_debug_string (hikaru->sh_m));
	sstr = strdup (vk_cpu_get_debug_string (hikaru->sh_s));
	gpustr = hikaru_gpu_get_debug_str (hikaru->gpu);

	sprintf (out, "[%s %04X] [%s %04X] [%s]",
	         mstr, hikaru->porta_m,
	         sstr, hikaru->porta_s,
	         gpustr);

	free (mstr);
	free (sstr);
	return out;
}

#define SAVE(thing_) \
		ret |= vk_state_put (state, (void *) &(thing_), sizeof (thing_))

#define LOAD(thing_) \
		ret |= vk_state_get (state, (void *) &(thing_), sizeof (thing_))

/* The Odin chips save and load a block of plain data */

template <typename State, typename Chip>
static int
save_chip (vk_state_t *state, const Chip &chip)
{
	State block;

	memset (&block, 0, sizeof (block));
	chip.save (block);
	return vk_state_put (state, &block, sizeof (block));
}

template <typename State, typename Chip>
static int
load_chip (vk_state_t *state, Chip &chip)
{
	State block;

	if (vk_state_get (state, &block, sizeof (block)))
		return -1;
	chip.load (block);
	return 0;
}


#define SAVE_CHIP(name_, dev_) \
	do { \
		size_t size_ = name_##_state_size (); \
		void *block_ = malloc (size_); \
		if (block_) { \
			name_##_save_state (dev_, block_); \
			ret |= vk_state_put (state, block_, size_); \
			free (block_); \
		} else \
			ret = -1; \
	} while (0)

#define LOAD_CHIP(name_, dev_) \
	do { \
		size_t size_ = name_##_state_size (); \
		void *block_ = malloc (size_); \
		if (block_ && !vk_state_get (state, block_, size_)) \
			name_##_load_state (dev_, block_); \
		else \
			ret = -1; \
		free (block_); \
	} while (0)

static int
hikaru_load_state (vk_machine_t *mach, vk_state_t *state)
{
	hikaru_t *hikaru = (hikaru_t *) mach;
	int ret = 0;

	LOAD (hikaru->porta_m);
	LOAD (hikaru->porta_m_bit0_buffer);
	LOAD (hikaru->porta_s);
	LOAD (hikaru->porta_s_bit1_buffer);
	LOAD (hikaru->unk01000000_m);
	LOAD (hikaru->unk01000100_m);
	LOAD (hikaru->unk1A800008_s);
	LOAD (hikaru->unk1B000100_s);
	LOAD (hikaru->rombd_config);

	ret |= load_chip<odin::Eeprom93c46State> (state, *hikaru->eeprom_main);
	LOAD_CHIP (odin_x76f100, hikaru->eeprom_rombd);
	if (hikaru->crypt)
		LOAD_CHIP (odin_crypt5881, hikaru->crypt);
	if (hikaru->actel)
		LOAD_CHIP (odin_actel_m1, hikaru->actel);

	ret |= hikaru_renderer_load_state (mach->renderer, state);

	LOAD_CHIP (odin_scheduler, hikaru->sched);
	sh4_set_time (hikaru->sh_m, odin_scheduler_now (hikaru->sched));
	sh4_set_time (hikaru->sh_s, odin_scheduler_now (hikaru->sched));
	LOAD (hikaru->line);
	LOAD (hikaru->dots_per_line);
	LOAD (hikaru->lines_per_frame);
	LOAD (hikaru->lines_shown);
	LOAD (hikaru->line_rest);

	hikaru->mie_port_d = hikaru_mie_get_port_d (hikaru->mie);
	hikaru_set_gpu_reset_lines (hikaru);
	hikaru_update_irqs (mach);

	return ret;
}

static int
hikaru_save_state (vk_machine_t *mach, vk_state_t *state)
{
	hikaru_t *hikaru = (hikaru_t *) mach;
	int ret = 0;

	SAVE (hikaru->porta_m);
	SAVE (hikaru->porta_m_bit0_buffer);
	SAVE (hikaru->porta_s);
	SAVE (hikaru->porta_s_bit1_buffer);
	SAVE (hikaru->unk01000000_m);
	SAVE (hikaru->unk01000100_m);
	SAVE (hikaru->unk1A800008_s);
	SAVE (hikaru->unk1B000100_s);
	SAVE (hikaru->rombd_config);

	ret |= save_chip<odin::Eeprom93c46State> (state, *hikaru->eeprom_main);
	SAVE_CHIP (odin_x76f100, hikaru->eeprom_rombd);
	if (hikaru->crypt)
		SAVE_CHIP (odin_crypt5881, hikaru->crypt);
	if (hikaru->actel)
		SAVE_CHIP (odin_actel_m1, hikaru->actel);

	ret |= hikaru_renderer_save_state (mach->renderer, state);

	SAVE_CHIP (odin_scheduler, hikaru->sched);
	SAVE (hikaru->line);
	SAVE (hikaru->dots_per_line);
	SAVE (hikaru->lines_per_frame);
	SAVE (hikaru->lines_shown);
	SAVE (hikaru->line_rest);

	return ret;
}

#undef SAVE
#undef LOAD

static void
hikaru_settle (vk_machine_t *mach)
{
	hikaru_t *hikaru = (hikaru_t *) mach;

	hikaru_aica_settle (hikaru->aica_m);
	hikaru_aica_settle (hikaru->aica_s);
}

static void
hikaru_dump (vk_machine_t *mach)
{
	hikaru_t *hikaru = (hikaru_t *) mach;
	char *name = mach->game->name;

	if (!name)
		return;

	hikaru_settle (mach);

	vk_buffer_dumpf (hikaru->ram_m,		"%s-ram-m.bin", name);
	vk_buffer_dumpf (hikaru->ram_s,		"%s-ram-s.bin", name);
	vk_buffer_dumpf (hikaru->cmdram,	"%s-cmdram.bin", name);
	vk_buffer_dumpf (hikaru->texram[0],	"%s-texram-0.bin", name);
	vk_buffer_dumpf (hikaru->texram[1],	"%s-texram-1.bin", name);
	vk_buffer_dumpf (hikaru->fb,		"%s-fb.bin", name);
	vk_buffer_dumpf (hikaru->aica_ram_m,	"%s-aica-m.bin", name);
	vk_buffer_dumpf (hikaru->aica_ram_s,	"%s-aica-s.bin", name);
	vk_buffer_dumpf (hikaru->bram,		"%s-bram.bin", name);
	vk_buffer_dumpf (hikaru->mie_ram,	"%s-mie.bin", name);
}

/*
 * Master SH-4 Memory Map
 * ======================
 *
 * Area 0	00000000-00200000 Boot ROM 
 *		00400000-00400003 ?
 *		00800000-0083FFFF On-board Switches + MIE
 *		00C00000-00C0FFFF Backup RAM
 *		01000000-01000003 ?
 *		01000100-01000103 ?
 *		02000000-02FFFFFF Aperture-02
 *		03000000-03FFFFFF Aperture-03
 * Area 1	04000000-0400003F Memory Controller [Master]
 * Area 3	0C000000-0DFFFFFF RAM
 * Area 5	14000000-140000FF Master/Slave Communication Box
 *		14000100-14FFFFFF Command RAM (4 MB and its mirrors)
 *		15000000-150FFFFF Geometry Processor
 *		16000000-16FFFFFF Aperture-16
 *		17000000-17FFFFFF Aperture-17
 * Area 6	18001000-180010FF GPU Regs
 *		1A000000-1A0FFFFF Image Generator
 *		1B000000-1B7FFFFF Frame Buffer
 */

static vk_mmap_t *
setup_master_mmap (hikaru_t *hikaru)
{
	vk_mmap_t *mmap = vk_mmap_new (&hikaru->base);
	int ret = 0;

	if (!mmap)
		return NULL;

	ret |= vk_mmap_add_ram (mmap, 0x0C000000, 0x0DFFFFFF, 0x01FFFFFF,
	                        0, hikaru->ram_m, "RAM/M");
	ret |= vk_mmap_add_rom (mmap, 0x00000000, 0x001FFFFF, 0x001FFFFF,
	                        0, hikaru->bootrom, "BOOTROM/M");
	ret |= vk_mmap_add_dev (mmap, 0x00400000, 0x00400001, 0x00000001,
	                        VK_REGION_RW | VK_REGION_SIZE_16 | VK_REGION_SIZE_32 | VK_REGION_LOG_RW,
	                        hikaru->gpu, "UNK/M");
	/* No LOG_RW: a logged access formats the whole debug string
	 * (hikaru_get_debug_string) and the MIE is polled in tight loops. */
	ret |= vk_mmap_add_dev (mmap, 0x00800000, 0x0083FFFF, 0x0003FFFF,
	                        VK_REGION_RW | VK_REGION_SIZE_8 | VK_REGION_SIZE_16 | VK_REGION_SIZE_32,
	                        hikaru->mie, "MIE/M");
	ret |= vk_mmap_add_ram (mmap, 0x00C00000, 0x00C0FFFF, 0x0000FFFF,
	                        0, hikaru->bram, "BRAM/M");
	/* The bus request register of the MIE */
	ret |= vk_mmap_add_dev (mmap, 0x01000000, 0x010001FF, 0x000001FF,
	                        VK_REGION_RW | VK_REGION_SIZE_16 | VK_REGION_SIZE_32,
	                        hikaru->mie, "MIE-BUSRQ/M");
	ret |= vk_mmap_add_dev (mmap, 0x02000000, 0x03FFFFFF, 0x01FFFFFF,
	                        VK_REGION_RW | VK_REGION_SIZE_ALL,
	                        hikaru->memctl_m, "APERTURE02/M");
	/* The whole bridge page: MMIOs at 00-3F, the rest reads as zero */
	ret |= vk_mmap_add_dev (mmap, 0x04000000, 0x04FFFFFF, 0x00FFFFFF,
	                        VK_REGION_RW | VK_REGION_SIZE_ALL | VK_REGION_LOG_RW,
	                        hikaru->memctl_m, "MEMCTL/M");
	ret |= vk_mmap_add_dev (mmap, 0x14000000, 0x1400002F, 0x0000003F,
	                        VK_REGION_RW | VK_REGION_SIZE_ALL | VK_REGION_LOG_RW,
	                        hikaru->mscomm, "MSCOMM/M");
	ret |= vk_mmap_add_ram (mmap, 0x14000030, 0x143FFFFF, 0x003FFFFF,
	                        0, hikaru->cmdram, "CMDRAM/M");
	/* The command RAM answers above its 4 MB: VON4 leaves a program at
	 * 14FFF000, which the slave runs at 10FFF000 (@0C711C84).
	 * HEURISTIC: mirrors, the two upper address lines taken as not
	 * wired; only that page is seen. They go through the bus (the log
	 * flags): a page the SH-4 core sees twice is no longer written in
	 * place, and the games write their lists through the first 4 MB. */
	ret |= vk_mmap_add_ram (mmap, 0x14400000, 0x14FFFFFF, 0x003FFFFF,
	                        VK_REGION_LOG_RW, hikaru->cmdram, "CMDRAM/M mirrors");
	ret |= vk_mmap_add_dev (mmap, 0x15000000, 0x150FFFFF, 0x000FFFFF,
	                        VK_REGION_RW | VK_REGION_SIZE_16 | VK_REGION_SIZE_32 | VK_REGION_LOG_RW,
	                        hikaru->gpu, "GPU/M");
	ret |= vk_mmap_add_dev (mmap, 0x16000000, 0x17FFFFFF, 0x01FFFFFF,
	                        VK_REGION_RW | VK_REGION_SIZE_ALL,
	                        hikaru->memctl_m, "APERTURE16/M");
	ret |= vk_mmap_add_dev (mmap, 0x18001000, 0x1800101F, 0x0000001F,
	                        VK_REGION_RW | VK_REGION_SIZE_32 | VK_REGION_LOG_RW,
	                        hikaru->gpu, "GPU/M");
	ret |= vk_mmap_add_dev (mmap, 0x1A000000, 0x1A0FFFFF, 0x000FFFFF,
	                        VK_REGION_RW | VK_REGION_SIZE_ALL | VK_REGION_LOG_RW,
	                        hikaru->gpu, "GPU/M");
	ret |= vk_mmap_add_ram (mmap, 0x1B000000, 0x1B7FFFFF, 0x007FFFFF,
	                        0, hikaru->fb, "FB/M");

	return ret ? NULL : mmap;
}

/*
 * Slave SH-4 Memory Map
 * =====================
 *
 * Area 0	00000000-001FFFFF Boot ROM
 * Area 1	04000000-0400003F Memory Controller [Slave]
 * Area 3	0C000000-0DFFFFFF RAM
 * Area 4	10000000-100000FF Master/Slave Communication Box
 *		10000100-10FFFFFF Command RAM [Slave] (4 MB and its mirrors)
 * Area 5	14000800-1400083F Master's 18000000
 * Area 6	1A800000-1A800003 GPU
 *		1B000100-1B000103 GPU
 */

static vk_mmap_t *
setup_slave_mmap (hikaru_t *hikaru)
{
	vk_mmap_t *mmap = vk_mmap_new (&hikaru->base);
	int ret = 0;

	if (!mmap)
		return NULL;

	ret |= vk_mmap_add_ram (mmap, 0x0C000000, 0x0DFFFFFF, 0x01FFFFFF,
	                        0, hikaru->ram_s, "RAM/S");
	ret |= vk_mmap_add_rom (mmap, 0x00000000, 0x001FFFFF, 0x001FFFFF,
	                        0, hikaru->bootrom, "BOOTROM/S");
	ret |= vk_mmap_add_dev (mmap, 0x04000000, 0x04FFFFFF, 0x00FFFFFF,
	                        VK_REGION_RW | VK_REGION_SIZE_ALL | VK_REGION_LOG_RW,
	                        hikaru->memctl_s, "MEMCTL/S");
	ret |= vk_mmap_add_dev (mmap, 0x10000000, 0x1000002F, 0x0000003F,
	                        VK_REGION_RW | VK_REGION_SIZE_ALL | VK_REGION_LOG_RW,
	                        hikaru->mscomm, "MSCOMM/S");
	/* With its mirrors, see setup_master_mmap */
	ret |= vk_mmap_add_ram (mmap, 0x10000100, 0x10FFFFFF, 0x003FFFFF,
	                        VK_REGION_LOG_W, hikaru->cmdram, "CMDRAM/S");
	ret |= vk_mmap_add_dev (mmap, 0x14000800, 0x1400083F, 0x0000003F,
	                        VK_REGION_RW | VK_REGION_SIZE_ALL | VK_REGION_LOG_RW,
	                        &unk_s, "UNK/S");
	ret |= vk_mmap_add_dev (mmap, 0x1A800000, 0x1A8000FF, 0x000000FF,
	                        VK_REGION_RW | VK_REGION_SIZE_ALL | VK_REGION_LOG_RW,
	                        &unk_s, "UNK/S");
	ret |= vk_mmap_add_dev (mmap, 0x1B000100, 0x1B0001FF, 0x000000FF,
	                        VK_REGION_RW | VK_REGION_SIZE_ALL | VK_REGION_LOG_RW,
	                        &unk_s, "UNK/S");

	return ret ? NULL : mmap;
}

static int
hikaru_set_rombd_config (hikaru_t *hikaru)
{
	vk_game_t *game = hikaru->base.game;
	hikaru_rombd_config_t *config = &hikaru->rombd_config;
	uint32_t rombd_offs = 0, eprom_bank_size = 0, maskrom_bank_size = 0;
	bool has_rom = true, maskrom_is_stretched = false;

	/* The fields computed here are used in hikaru-memctl.c::rombd_get () */

	/* We require at least the bootrom to be loaded */
	if (!hikaru->bootrom)
		return -1;

	/* Determine the layout of ROMBD data within the external bus; see
	 * hikaru-memctl.c for more details. */
	if (!strcmp (game->name, "bootrom")) {
		has_rom = false;
		hikaru->eprom = NULL;
		hikaru->maskrom = NULL;
	} else if (!strcmp (game->name, "airtrix")) {
		eprom_bank_size = 4;
		maskrom_bank_size = 16;
		config->has_5881 = true;
		config->crypt_key = 0x291B02C7;	/* MAME hikaru.cpp */
	} else if (!strcmp (game->name, "braveff")) {
		eprom_bank_size = 2;
		maskrom_bank_size = 8;
	} else if (!strcmp (game->name, "pharrier")) {
		eprom_bank_size = 4;
		maskrom_bank_size = 16;
		config->has_5881 = true;
		config->crypt_key = 0x2912C68A;
	} else if (!strcmp (game->name, "von4")) {
		/* Same board layout as pharrier: 128M TSOP48 mask ROMs */
		eprom_bank_size = 4;
		maskrom_bank_size = 16;
	} else if (!strcmp (game->name, "podrace")) {
		/* 32 x 64M SOP44 mask ROMs: each interleaved pair fills one 16 MB
		 * bank (MAME hikaru.cpp swracer: pairs at 0x1000000 steps) */
		eprom_bank_size = 4;
		maskrom_bank_size = 16;
		config->has_5881 = true;
		config->crypt_key = 0x2903DAD5;
	} else if (!strcmp (game->name, "sgnascar")) {
		eprom_bank_size = 4;
		maskrom_bank_size = 16;
		maskrom_is_stretched = true;
	} else
		return -1;

	config->has_rom = has_rom;

	/* Set the ROMBD data layout within the external bus */
	if (has_rom) {
		/* There are four EPROM banks */
		config->eprom_bank[0] = rombd_offs + 0x10;
		config->eprom_bank[1] = rombd_offs + 0x13;
		/* There are sixteen MASKROM banks */
		config->maskrom_bank[0] = (rombd_offs == 0) ? 0x20 : 0x30;
		config->maskrom_bank[1] = (rombd_offs == 0) ? 0x2F : 0x4F;
		/* Set the remaining configuration btis */
		config->eprom_bank_size = eprom_bank_size;
		config->maskrom_bank_size = maskrom_bank_size;
		config->maskrom_is_stretched = maskrom_is_stretched;
	}
	/* The EEPROM bank is constant */
	config->eeprom_bank = rombd_offs + 0x14;

	return 0;
}

#define R(n_)	ctx->regs.r[n_]
#define PR	ctx->regs.pr
#define T	ctx->regs.sr.bit.t

static uint32_t
patch_sgnascar (vk_cpu_t *cpu, uint32_t pc, uint32_t inst)
{
	sh4_t *ctx = (sh4_t *) cpu;

	switch (pc) {
	case 0x0C00BC9A:
		/* HACK: the region check of the BIOS (0x0C00BC5C) reads the
		 * ROM board X76F100, but the only dump (x76f100.ic85, marked
		 * bad in MAME) comes from another board and the check ends
		 * on "ERROR 7". Needs the dump of a NASCAR ROM board. */
		R(3) = 0xFF;
		break;
	}
	/* The 600-frame wait for the I/O board node count (0x0C0130DE) must
	 * run: the real MIE answers in about five seconds. */
	return inst;
}

/* The idle loop of the slave, in the BOOTROM, counts in R7 while the byte
 * the master posts at GBR stays zero:
 *
 *	0C002030  mov.b @(0,gbr),r0
 *	          mov   #2,r2
 *	          shll8 r2
 *	          addc  r2,r7
 *	          mov   #-128,r1
 *	          bf/s  1f
 *	          shll16 r1
 *	          (on a carry: writes port A)
 *	1:        tst   r0,r0
 *	          mov   #0,r0
 *	          bt    0C002030
 *
 * A turn without a carry takes ten steps and only adds 0x201 to R7, and
 * nothing else runs before the end of the slice: such turns are counted
 * without being run, up to the turn that carries or the last of the slice. */
static uint32_t
slave_idle_loop (vk_cpu_t *cpu, uint32_t pc, uint32_t inst)
{
	hikaru_t *hikaru = (hikaru_t *) cpu->base.mach;
	sh4_t *ctx = (sh4_t *) cpu;
	uint32_t gbr = ctx->regs.gbr, tst, turns;
	const uint16_t *code;

	if (pc != 0x0C002030 || ctx->in_slot || cpu->remaining <= 10)
		return inst;

	code = (const uint16_t *) vk_buffer_get_ptr (hikaru->ram_s, 0x2030);
	if (code[0] != 0xC400 || code[1] != 0xE202 || code[2] != 0x4218 ||
	    code[3] != 0x372E || code[4] != 0xE180 ||
	    (code[5] & 0xFF80) != 0x8F00 || code[6] != 0x4128)
		return inst;

	tst = 7 + (code[5] & 0x7F);
	if (tst > 16 || code[tst] != 0x2008 || code[tst + 1] != 0xE000 ||
	    code[tst + 2] != (0x8900 | ((0 - (tst + 4)) & 0xFF)))
		return inst;

	/* The registers a turn leaves, and the posted byte still zero */
	if (R(0) != 0 || R(1) != 0xFF800000 || R(2) != 0x200 || !T ||
	    (gbr >> 29) == 7 || (gbr & 0x1E000000) != 0x0C000000 ||
	    *(uint8_t *) vk_buffer_get_ptr (hikaru->ram_s, gbr & 0x01FFFFFF))
		return inst;

	turns = MIN2 ((0xFFFFFFFF - R(7)) / 0x201,
	              (uint32_t) (cpu->remaining - 1) / 10);
	R(7) += turns * 0x201;
	cpu->remaining -= (int) turns * 10;
	ctx->wait.touched = true;

	return inst;
}

/* Install game-specific patches into the master SH-4 */
static void
hikaru_install_game_patches (hikaru_t *hikaru)
{
	vk_cpu_t *cpu = hikaru->sh_m;
	vk_game_t *game = hikaru->base.game;
	bool patched = true;

	if (!game)
		return;

	if (!strcmp (game->name, "sgnascar"))
		vk_cpu_install_patch (cpu, patch_sgnascar, 0x0C00BC9A);
	else
		patched = false;

	if (patched)
		VK_PRINT ("Installed patches for '%s'", game->name);
}

static void
hikaru_destroy (vk_machine_t **mach_)
{
	if (mach_) {
		hikaru_t *hikaru = (hikaru_t *) *mach_;
		if (hikaru) {
			/* dump everything we got before quitting */
			hikaru_dump ((vk_machine_t *) hikaru);
			odin_scheduler_delete (hikaru->sched);
		}
	}
}

static int
hikaru_init (hikaru_t *hikaru)
{
	vk_machine_t *mach = (vk_machine_t *) hikaru;
	vk_game_t *game = mach->game;

	unk_s.mach = mach;

	hikaru->ram_m		= vk_buffer_le32_new (32*MB, 0);
	hikaru->ram_s		= vk_buffer_le32_new (32*MB, 0);
	hikaru->cmdram		= vk_buffer_le32_new (4*MB, 0);
	hikaru->fb		= vk_buffer_le32_new (8*MB, 0);
	hikaru->texram[0]	= vk_buffer_le32_new (4*MB, 0);
	hikaru->texram[1]	= vk_buffer_le32_new (4*MB, 0);
	hikaru->aica_ram_m	= vk_buffer_le32_new (8*MB, 0);
	hikaru->aica_ram_s	= vk_buffer_le32_new (8*MB, 0);
	hikaru->mie_ram		= vk_buffer_le32_new (32*KB, 0);
	hikaru->bram		= vk_buffer_le32_new (64*KB, 0);

	if (!hikaru->ram_m || !hikaru->ram_s ||
	    !hikaru->cmdram || !hikaru->fb ||
	    !hikaru->texram[0] || !hikaru->texram[1] ||
	    !hikaru->aica_ram_m || !hikaru->aica_ram_s ||
	    !hikaru->bram || !hikaru->mie_ram)
		return -1;

	vk_machine_register_buffer (mach, (void *) hikaru->ram_m);
	vk_machine_register_buffer (mach, (void *) hikaru->ram_s);
	vk_machine_register_buffer (mach, (void *) hikaru->cmdram);
	vk_machine_register_buffer (mach, (void *) hikaru->fb);
	vk_machine_register_buffer (mach, (void *) hikaru->texram[0]);
	vk_machine_register_buffer (mach, (void *) hikaru->texram[1]);
	vk_machine_register_buffer (mach, (void *) hikaru->aica_ram_m);
	vk_machine_register_buffer (mach, (void *) hikaru->aica_ram_s);
	vk_machine_register_buffer (mach, (void *) hikaru->mie_ram);
	vk_machine_register_buffer (mach, (void *) hikaru->bram);

	if (game) {
		char *version;

		hikaru->bootrom 	= vk_game_get_section_data (game, "bootrom");
		hikaru->eprom   	= vk_game_get_section_data (game, "eprom");
		hikaru->maskrom 	= vk_game_get_section_data (game, "maskrom");

		version = (char *) vk_buffer_get_ptr (hikaru->bootrom, 0xD4);
		VK_PRINT ("%s", version);
		if (hikaru_set_rombd_config (hikaru))
			return -1;
	} else {
		/* Create a mock bootrom */                                     
		hikaru->bootrom = vk_buffer_le32_new (2*MB, 0);
	}

	hikaru->memctl_m = hikaru_memctl_new (mach, true);
	hikaru->memctl_s = hikaru_memctl_new (mach, false);

	if (!hikaru->memctl_m || !hikaru->memctl_s)
		return -1;

	hikaru->mscomm = hikaru_mscomm_new (mach);
	if (!hikaru->mscomm)
		return -1;

	hikaru->mie = hikaru_mie_new (mach, HIKARU_SH4_CLOCK);
	if (!hikaru->mie)
		return -1;

	hikaru->eeprom_main = new odin::Eeprom93c46 (HIKARU_SH4_CLOCK);

	hikaru->sched = odin_scheduler_new ();
	if (!hikaru->sched)
		return -1;
	if (game) {
		vk_buffer_t *image = vk_game_get_section_data (game, "eeprom");
		if (image) {
			hikaru->eeprom_main->load (std::span<const uint8_t> (
				(const uint8_t *) vk_buffer_get_ptr (image, 0),
				vk_buffer_get_size (image)));
			VK_PRINT ("main board EEPROM: 93c46 image loaded");
		} else
			VK_PRINT ("main board EEPROM: no image, blank");
	}

	hikaru->eeprom_rombd = odin_x76f100_new ();
	if (!hikaru->eeprom_rombd)
		return -1;
	if (game) {
		vk_buffer_t *image = vk_game_get_section_data (game, "rombd_eeprom");
		if (image) {
			odin_x76f100_load (hikaru->eeprom_rombd,
			                   (const uint8_t *) vk_buffer_get_ptr (image, 0),
			                   vk_buffer_get_size (image));
			VK_PRINT ("ROM board EEPROM: x76f100 image loaded");
		} else
			VK_PRINT ("ROM board EEPROM: no image, blank");
	}

	if (hikaru->rombd_config.has_5881) {
		hikaru->protram = vk_buffer_le32_new (64*KB, 0);
		if (!hikaru->protram)
			return -1;
		vk_machine_register_buffer (mach, (void *) hikaru->protram);
		hikaru->crypt = odin_crypt5881_new (hikaru->rombd_config.crypt_key,
		                                    crypt_read_protram, hikaru);
		if (!hikaru->crypt)
			return -1;
		VK_PRINT ("ROM board 315-5881: key %08X, 64 KB RAM", hikaru->rombd_config.crypt_key);
	}

	/* 834-14125 ROM board: Actel A54SX32 stamped 317-0283-COM, ID 0x4252,
	 * key as in MAME hikaru.cpp. The key is proven by the game: the chip
	 * then gives the text SGNASCAR compares it with. */
	if (!strcmp (game->name, "sgnascar") && hikaru->maskrom) {
		hikaru->actel = odin_actel_m1_new (0x56DEDF33, 0x4252,
		                                   (const uint8_t *) vk_buffer_get_ptr (hikaru->maskrom, 0),
		                                   vk_buffer_get_size (hikaru->maskrom));
		if (!hikaru->actel)
			return -1;
		VK_PRINT ("ROM board security FPGA: 317-0283-COM, ID 4252");
	}

	hikaru->base.renderer = hikaru_renderer_new (hikaru->fb,
	                                             hikaru->texram, game->name);

	hikaru->gpu = hikaru_gpu_new (mach, hikaru->cmdram, hikaru->fb,
	                              hikaru->texram, hikaru->base.renderer);

	if (!hikaru->gpu || !hikaru->base.renderer)
		return -1;

	hikaru_renderer_set_gpu (hikaru->base.renderer,
	                         hikaru_gpu_get_chips (hikaru->gpu));

	hikaru->aica_m = hikaru_aica_new (mach, hikaru->aica_ram_m, true, HIKARU_SH4_CLOCK);
	hikaru->aica_s = hikaru_aica_new (mach, hikaru->aica_ram_s, false, HIKARU_SH4_CLOCK);

	if (!hikaru->aica_m || !hikaru->aica_s)
		return -1;

	hikaru->mmap_m = setup_master_mmap (hikaru);
	hikaru->mmap_s = setup_slave_mmap (hikaru);

	if (!hikaru->mmap_m || !hikaru->mmap_s)
		return -1;

	/* Clock operating mode 5: a 33.3333 MHz crystal, the CPU at six times
	 * that and the peripheral clock at 50 MHz. The timers of PHARRIER
	 * count 0.08 microseconds (@0C0CF150), a quarter of that clock; MAME
	 * gives the pins (hikaru.cpp, set_md). */
	hikaru->sh_m = sh4_new (mach, hikaru->mmap_m, true, true, 5);
	hikaru->sh_s = sh4_new (mach, hikaru->mmap_s, false, true, 5);

	if (!hikaru->sh_m || !hikaru->sh_s)
		return -1;

	sh4_set_porta_handlers (hikaru->sh_m, porta_get_m, porta_put_m);
	sh4_set_porta_handlers (hikaru->sh_s, porta_get_s, porta_put_s);

	hikaru_install_game_patches (hikaru);
	vk_cpu_install_patch (hikaru->sh_s, slave_idle_loop, 0x0C002030);

	return 0;
}

struct vk_machine_t *
hikaru_new (vk_game_t *game)
{
	hikaru_t *hikaru;
	vk_machine_t *mach;

	hikaru = ALLOC (hikaru_t);
	if (!hikaru)
		return NULL;

	mach = (vk_machine_t *) hikaru;
	strcpy (mach->name, "SEGA Hikaru");

	mach->game = game;

	mach->destroy		= hikaru_destroy;
	mach->reset		= hikaru_reset;
	mach->run_frame		= hikaru_run_frame;
	mach->load_state	= hikaru_load_state;
	mach->save_state	= hikaru_save_state;
	mach->settle		= hikaru_settle;
	mach->get_debug_string	= hikaru_get_debug_string;

	if (hikaru_init (hikaru))
		hikaru_destroy (&mach);

	return mach;
}
