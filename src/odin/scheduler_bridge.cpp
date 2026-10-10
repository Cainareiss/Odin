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

#include "odin/scheduler_bridge.hpp"

#include "odin/scheduler.hpp"

struct odin_scheduler {
	odin::Scheduler sched;
};

extern "C" {

odin_scheduler_t *
odin_scheduler_new(void)
{
	return new odin_scheduler();
}

void
odin_scheduler_delete(odin_scheduler_t *sched)
{
	delete sched;
}

void
odin_scheduler_reset(odin_scheduler_t *sched)
{
	sched->sched.reset();
}

uint64_t
odin_scheduler_now(const odin_scheduler_t *sched)
{
	return sched->sched.now();
}

void
odin_scheduler_set(odin_scheduler_t *sched, uint32_t source, uint64_t when)
{
	sched->sched.set(source, when);
}

uint64_t
odin_scheduler_next(const odin_scheduler_t *sched)
{
	return sched->sched.next();
}

void
odin_scheduler_advance(odin_scheduler_t *sched, uint64_t to)
{
	sched->sched.advance(to);
}

int
odin_scheduler_take(odin_scheduler_t *sched)
{
	uint32_t source;
	return sched->sched.take(source) ? static_cast<int>(source) : -1;
}

} /* extern "C" */
