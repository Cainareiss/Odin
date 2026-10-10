/*
 * Odin
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

/* C entry points to the Odin scheduler (src/odin/scheduler.hpp). */

#ifndef ODIN_SCHEDULER_BRIDGE_HPP
#define ODIN_SCHEDULER_BRIDGE_HPP

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct odin_scheduler odin_scheduler_t;

odin_scheduler_t *odin_scheduler_new(void);
void odin_scheduler_delete(odin_scheduler_t *sched);
void odin_scheduler_reset(odin_scheduler_t *sched);
uint64_t odin_scheduler_now(const odin_scheduler_t *sched);
void odin_scheduler_set(odin_scheduler_t *sched, uint32_t source, uint64_t when);
uint64_t odin_scheduler_next(const odin_scheduler_t *sched);
void odin_scheduler_advance(odin_scheduler_t *sched, uint64_t to);
/* The lowest source due now, taken; -1 if none is. */
int odin_scheduler_take(odin_scheduler_t *sched);

#ifdef __cplusplus
}
#endif

#endif /* ODIN_SCHEDULER_BRIDGE_HPP */
