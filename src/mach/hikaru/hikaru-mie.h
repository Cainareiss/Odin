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

#ifndef __VK_HIKARU_MIE_H__
#define __VK_HIKARU_MIE_H__

#include "vk/device.h"

/* `clock_hz` is the clock of the cycles passed to the device's exec. */
vk_device_t	*hikaru_mie_new (vk_machine_t *mach, unsigned clock_hz);
void		 hikaru_mie_set_irq_lines (vk_device_t *dev, uint8_t lines);
bool		 hikaru_mie_irq_out (vk_device_t *dev);

#endif /* __VK_HIKARU_MIE_H__ */
