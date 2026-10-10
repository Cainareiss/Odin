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

#include "vk/mmap.h"

typedef struct {
	uint32_t lo;
	uint32_t hi;
	uint32_t mask;
	uint32_t flags;
	union {
		vk_buffer_t *buf;
		vk_device_t *dev;
		void *ptr;
	};
	char *name;
} region_t;

static inline unsigned
get_size_flag_for_size (unsigned size)
{
	switch (size) {
	case 1:
		return VK_REGION_SIZE_8;
	case 2:
		return VK_REGION_SIZE_16;
	case 4:
		return VK_REGION_SIZE_32;
	case 8:
		return VK_REGION_SIZE_64;
	}
	return 0;
}

static int
add_region (vk_mmap_t *mmap, uint32_t lo, uint32_t hi, uint32_t mask,
            uint32_t flags, void *ptr, const char *name)
{
	region_t *region;

	VK_ASSERT (mmap);
	VK_ASSERT (mmap->regions);

	VK_ASSERT (flags & VK_REGION_RW);
	VK_ASSERT (flags & VK_REGION_SIZE_ALL);
	VK_ASSERT (hi > lo);
	VK_ASSERT (mask != 0);
	VK_ASSERT (ptr != NULL);
	VK_ASSERT (name != NULL);

	region = (region_t *) vk_vector_append_entry (mmap->regions);
	if (!region)
		return -1;

	region->lo = lo;
	region->hi = hi;
	region->mask = mask;
	region->flags = flags;
	region->ptr = ptr;
	region->name = strdup (name);

	VK_ASSERT (region->name);

	return 0;
}

int
vk_mmap_add_ram (vk_mmap_t *mmap, uint32_t lo, uint32_t hi, uint32_t mask,
                 uint32_t flags, vk_buffer_t *buf, const char *name)
{
	flags |= VK_REGION_DIRECT | VK_REGION_RW | VK_REGION_SIZE_ALL;
	return add_region (mmap, lo, hi, mask, flags, (void *) buf, name);
}

int
vk_mmap_add_rom (vk_mmap_t *mmap, uint32_t lo, uint32_t hi, uint32_t mask,
                 uint32_t flags, vk_buffer_t *buf, const char *name)
{
	VK_ASSERT (!(flags & VK_REGION_W));

	flags |= VK_REGION_DIRECT | VK_REGION_RW | VK_REGION_ROM | VK_REGION_SIZE_ALL;
	return add_region (mmap, lo, hi, mask, flags, (void *) buf, name);
}

int
vk_mmap_add_dev (vk_mmap_t *mmap, uint32_t lo, uint32_t hi, uint32_t mask,
                 uint32_t flags, vk_device_t *dev, const char *name)
{
	VK_ASSERT (!(flags & VK_REGION_DIRECT));

	return add_region (mmap, lo, hi, mask, flags, (void *) dev, name);
}

static region_t *
get_region (vk_mmap_t *mmap, uint32_t addr, uint32_t flags)
{
	uint32_t offs;

	VK_ASSERT (mmap);
	VK_ASSERT ((flags & ~VK_REGION_RW) == 0);

	VK_VECTOR_FOREACH (mmap->regions, offs) {
		region_t *region = (region_t *) &mmap->regions->data[offs];
		if (addr >= region->lo && addr <= region->hi &&
		    (region->flags & flags))
			return region;
	}

	return NULL;
}

/* A region is read or written in place when it is plain little-endian
 * memory, is not logged, takes every access size and is the first match of
 * the search for all of its addresses. */
static void
set_window (vk_mmap_t *mmap, vk_mmap_window_t *win, region_t *region,
            uint32_t access, uint32_t excluded)
{
	uint32_t offs;

	if ((region->flags & excluded) ||
	    (region->flags & VK_REGION_SIZE_ALL) != VK_REGION_SIZE_ALL ||
	    region->mask > 0x7FFFFFFF ||
	    !vk_buffer_is_le (region->buf))
		return;

	VK_VECTOR_FOREACH (mmap->regions, offs) {
		region_t *other = (region_t *) &mmap->regions->data[offs];
		if (other == region)
			break;
		if ((other->flags & access) &&
		    other->lo <= region->hi && other->hi >= region->lo)
			return;
	}

	win->lo = region->lo;
	win->span = region->hi - region->lo;
	win->mask = region->mask;
	win->size = vk_buffer_get_size (region->buf);
	win->ptr = region->buf->ptr;
}

/* The host memory behind the `size` bytes at `addr` when all of them are
 * plain memory read, or written, in place: little-endian, not logged, not
 * ROM for a write, and the first match of the search. NULL otherwise. */
uint8_t *
vk_mmap_get_page (vk_mmap_t *mmap, uint32_t addr, uint32_t size, bool write)
{
	uint32_t access = write ? VK_REGION_W : VK_REGION_R;
	uint32_t excluded = write ? (VK_REGION_LOG_W | VK_REGION_ROM) : VK_REGION_LOG_R;
	uint32_t last = addr + size - 1, offs, i;
	region_t *region = NULL;

	VK_VECTOR_FOREACH (mmap->regions, i) {
		region_t *other = (region_t *) &mmap->regions->data[i];
		if ((other->flags & access) && other->lo <= last && other->hi >= addr) {
			region = other;
			break;
		}
	}
	if (!region || region->lo > addr || region->hi < last ||
	    !(region->flags & VK_REGION_DIRECT) || (region->flags & excluded) ||
	    (region->flags & VK_REGION_SIZE_ALL) != VK_REGION_SIZE_ALL ||
	    region->mask > 0x7FFFFFFF || !vk_buffer_is_le (region->buf))
		return NULL;

	offs = addr & region->mask;
	if ((last & region->mask) != offs + size - 1 ||
	    offs + size > vk_buffer_get_size (region->buf))
		return NULL;
	return region->buf->ptr + offs;
}

int
vk_mmap_get_slow (vk_mmap_t *mmap, unsigned size, uint32_t addr, void *data)
{
	region_t *region;

	VK_ASSERT (mmap != NULL);
	VK_ASSERT (data != NULL);
	VK_ASSERT (is_size_valid (size));

	region = get_region (mmap, addr, VK_REGION_R);
	if (!region || !(region->flags & get_size_flag_for_size (size)))
		return -1;

	if (region->flags & VK_REGION_LOG_R)
		VK_MACH_LOG (mmap->mach, "%s R%u %08X", region->name, size * 8, addr);

	if (region->flags & VK_REGION_DIRECT) {
		uint32_t offs = addr & region->mask;
		uint64_t temp;
		temp = region->buf->get (region->buf, size, offs);
		set_ptr (data, size, temp);
		set_window (mmap, &mmap->rd, region, VK_REGION_R,
		            VK_REGION_LOG_R);
		return 0;
	}

	return vk_device_get (region->dev, size, addr, data);
}

int
vk_mmap_put_slow (vk_mmap_t *mmap, unsigned size, uint32_t addr, uint64_t data)
{
	region_t *region;

	VK_ASSERT (mmap != NULL);
	VK_ASSERT (is_size_valid (size));

	region = get_region (mmap, addr, VK_REGION_W);
	if (!region)
		return -1;

	if (!(region->flags & get_size_flag_for_size (size)))
		return -1;

	if (region->flags & VK_REGION_LOG_W)
		VK_MACH_LOG (mmap->mach, "%s W%u %08X = %lX", region->name, size * 8, addr, data);

	/* Writes to a ROM have no effect, as on the hardware. */
	if (region->flags & VK_REGION_ROM)
		return 0;

	if (region->flags & VK_REGION_DIRECT) {
		uint32_t offs = addr & region->mask;
		region->buf->put (region->buf, size, offs, data);
		set_window (mmap, &mmap->wr, region, VK_REGION_W,
		            VK_REGION_LOG_W | VK_REGION_ROM);
		return 0;
	}

	return vk_device_put (region->dev, size, addr, data);
}

vk_mmap_t *
vk_mmap_new (vk_machine_t *mach)
{
	vk_mmap_t *mmap;

	VK_ASSERT (mach);

	mmap = ALLOC (vk_mmap_t);
	if (!mmap)
		goto fail;

	mmap->regions = vk_vector_new (8, sizeof (region_t));
	if (!mmap->regions)
		goto fail;

	mmap->mach = mach;

	return mmap;

fail:
	vk_mmap_destroy (&mmap);
	return NULL;
}

void
vk_mmap_destroy (vk_mmap_t **mmap_)
{
	if (mmap_) {
		vk_mmap_t *mmap = *mmap_;
		if (mmap) {
			uint32_t offs;

			VK_VECTOR_FOREACH (mmap->regions, offs) {
				region_t *region = (region_t *) &mmap->regions->data[offs];
				free (region->name);
			}

			vk_vector_destroy (&mmap->regions);
			mmap->mach = NULL;
		}
		free (mmap);
		*mmap_ = NULL;
	}
}
