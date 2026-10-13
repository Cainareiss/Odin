/*
 * Valkyrie
 * Copyright (C) 2011, 2012, Stefano Teso
 * Copyright (C) 2026 Réda Chérif-Touil
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

#include "mach/hikaru/hikaru.h"
#include "mach/hikaru/hikaru-aica.h"
#include "odin/aica_bridge.hpp"

/*
 * Sound Boards
 * ============
 *
 * Apparently SNDBD and SNDBD2 are identical, except that the latter is
 * optional.
 *
 * 700000-701FFF AICA Channels 0-63. Each slot is 128 bytes long.
 * 702000-7027FF AICA ESF (?)
 * 702800-7028BD AICA Global (?)
 * 702C00        ARM Reset
 * 702D00        AICA IRQ L
 * 702D04        AICA IRQ R
 * 703000-7031FF COEF
 * 703200-7033FF MADRS
 * 703400-703BFF MPRO; 3BFE --> AICA DSP start
 *
 * 710000 RTC, high half of the count
 * 710004 RTC, low half
 * 710008 RTC Write Enable
 *
 * 800000-FFFFFF RAM
 *
 * The chip itself lives in src/odin/aica.cpp; this file is the bridge
 * between the Valkyrie bus and that device.
 */

typedef struct {
	vk_device_t base;
	odin_aica_t *chip;
	vk_buffer_t *ram;
	bool master;
} hikaru_aica_t;

static void
hikaru_aica_main_irq (void *ctx, int level)
{
	hikaru_aica_t *aica = (hikaru_aica_t *) ctx;
	vk_device_t *dev = (vk_device_t *) aica;

	(void) level;
	hikaru_update_irqs (dev->mach);
}

bool
hikaru_aica_irq_line (vk_device_t *dev)
{
	hikaru_aica_t *aica = (hikaru_aica_t *) dev;

	return odin_aica_main_irq (aica->chip) != 0;
}

static int
hikaru_aica_get (vk_device_t *device, unsigned size, uint32_t addr, void *val)
{
	hikaru_aica_t *aica = (hikaru_aica_t *) device;
	uint32_t offs = addr & 0xFFFFFF;
	uint64_t value = 0;

	VK_MACH_LOG (device->mach, "AICA/%c R%u @%08X",
	             aica->master ? 'M' : 'S', 8*size, offs);

	switch (offs) {
	case 0x000000:
		/* HACK: PHARRIER reads this and wants 4 (PH:@0C0B2884). Not a
		 * register of the chip; kept until the real board is traced. */
		set_ptr (val, size, 4);
		return 0;
	default:
		break;
	}

	if (odin_aica_host_read (aica->chip, offs, size, &value)) {
		VK_MACH_ERROR (device->mach, "AICA unhandled R%u %08X", size*8, addr);
		set_ptr (val, size, 0);
		return -1;
	}
	set_ptr (val, size, value);
	return 0;
}

/*
 * The battery of the clock
 * ========================
 *
 * The RTC of the main board keeps counting when the cabinet is off. The
 * count it starts with here is the local time of the host, as on a board
 * whose clock was set once and whose battery never died. With
 * ODIN_CLOCK_FILE=<path> the board keeps the clock it was set to (CLOCK
 * SETTING in the SYSTEM MENU): the file holds, in seconds, how far that
 * clock is ahead of the time of the host.
 */

static void
hikaru_aica_load_clock (hikaru_aica_t *aica)
{
	const char *path = getenv ("ODIN_CLOCK_FILE");
	int ahead = 0;
	FILE *fp;

	if (!path || !aica->master)
		return;
	fp = fopen (path, "r");
	if (fp && fscanf (fp, "%d", &ahead) == 1) {
		odin_aica_set_rtc_ahead (aica->chip, ahead);
		VK_PRINT ("AICA: clock file '%s': %d s ahead of the host", path, ahead);
	} else
		VK_PRINT ("AICA: clock file '%s' not found: the time of the host", path);
	if (fp)
		fclose (fp);
}

static void
hikaru_aica_save_clock (hikaru_aica_t *aica)
{
	const char *path = getenv ("ODIN_CLOCK_FILE");
	FILE *fp;

	if (!path || !aica->master)
		return;
	fp = fopen (path, "w");
	if (!fp) {
		VK_ERROR ("AICA: can't write the clock file '%s'", path);
		return;
	}
	fprintf (fp, "%d\n", (int) odin_aica_rtc_ahead (aica->chip));
	fclose (fp);
}

static int
hikaru_aica_put (vk_device_t *device, unsigned size, uint32_t addr, uint64_t val)
{
	hikaru_aica_t *aica = (hikaru_aica_t *) device;
	uint32_t offs = addr & 0xFFFFFF;
	int ret;

	VK_MACH_LOG (device->mach, "AICA/%c W%u @%08X = %lX",
	             aica->master ? 'M' : 'S', 8*size, offs, val);

	ret = odin_aica_host_write (aica->chip, offs, size, val);
	if (ret < 0) {
		VK_MACH_ERROR (device->mach, "AICA unhandled W%u %08X = %lX", size*8, addr, val);
		return -1;
	}
	if (ret > 0)
		hikaru_aica_save_clock (aica);
	return 0;
}

static int
hikaru_aica_exec (vk_device_t *device, int cycles)
{
	hikaru_aica_t *aica = (hikaru_aica_t *) device;

	odin_aica_run (aica->chip, (uint32_t) cycles);
	return cycles;
}

/* The reset line of the sound boards, from the MIE (hikaru_set_resets). */
void
hikaru_aica_reset_pin (vk_device_t *dev)
{
	hikaru_aica_t *aica = (hikaru_aica_t *) dev;

	odin_aica_reset_pin (aica->chip);
}

/* Before the wave memory is saved or dumped, see odin_aica_settle. */
void
hikaru_aica_settle (vk_device_t *dev)
{
	hikaru_aica_t *aica = (hikaru_aica_t *) dev;

	odin_aica_settle (aica->chip);
}

static int
hikaru_aica_save_state (vk_device_t *dev, vk_state_t *state)
{
	hikaru_aica_t *aica = (hikaru_aica_t *) dev;
	size_t size = odin_aica_state_size ();
	void *block = malloc (size);
	int ret;

	if (!block)
		return -1;
	odin_aica_save_state (aica->chip, block);
	ret = vk_state_put (state, block, size);
	free (block);
	return ret;
}

static int
hikaru_aica_load_state (vk_device_t *dev, vk_state_t *state)
{
	hikaru_aica_t *aica = (hikaru_aica_t *) dev;
	size_t size = odin_aica_state_size ();
	void *block = malloc (size);
	int ret;

	if (!block)
		return -1;
	ret = vk_state_get (state, block, size);
	if (!ret)
		odin_aica_load_state (aica->chip, block);
	free (block);
	return ret;
}

static void
hikaru_aica_reset (vk_device_t *dev, vk_reset_type_t type)
{
	hikaru_aica_t *aica = (hikaru_aica_t *) dev;

	odin_aica_reset (aica->chip);
	hikaru_aica_load_clock (aica);
}

static void
hikaru_aica_destroy (vk_device_t **dev_)
{
	if (dev_ && *dev_) {
		hikaru_aica_t *aica = (hikaru_aica_t *) *dev_;
		odin_aica_delete (aica->chip);
		aica->chip = NULL;
	}
}

vk_device_t *
hikaru_aica_new (vk_machine_t *mach, vk_buffer_t *ram, bool master, unsigned clock_hz)
{
	hikaru_aica_t *aica;
	vk_device_t *dev;

	VK_DEVICE_ALLOC (aica, mach);
	dev = (vk_device_t *) aica;
	if (!aica)
		return NULL;

	dev->destroy	= hikaru_aica_destroy;
	dev->reset	= hikaru_aica_reset;
	dev->exec	= hikaru_aica_exec;
	dev->get	= hikaru_aica_get;
	dev->put	= hikaru_aica_put;
	dev->save_state	= hikaru_aica_save_state;
	dev->load_state	= hikaru_aica_load_state;

	aica->ram	= ram;
	aica->master	= master;
	aica->chip	= odin_aica_new ((uint8_t *) vk_buffer_get_ptr (ram, 0),
	                                 vk_buffer_get_size (ram), clock_hz,
	                                 hikaru_aica_main_irq, aica);
	if (!aica->chip)
		goto fail;

	odin_aica_set_log (aica->chip, stderr);

	/* VK_ARM7_TRACE=<prefix> writes <prefix>-m.txt / <prefix>-s.txt */
	{
		const char *prefix = getenv ("VK_ARM7_TRACE");
		if (prefix) {
			char path[1024];
			snprintf (path, sizeof (path), "%s-%c.txt", prefix, master ? 'm' : 's');
			odin_aica_set_arm_trace (aica->chip, fopen (path, "w"));
		}
	}

	return dev;

fail:
	vk_device_destroy (&dev);
	return NULL;
}
