/*
 * Odin
 * Copyright (C) 2026 Réda Chérif-Touil
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "odin/worker_bridge.hpp"
#include "odin/worker.hpp"

struct odin_worker {
	odin::Worker worker;
};

extern "C" {

odin_worker_t *
odin_worker_new(void)
{
	return new odin_worker;
}

void
odin_worker_delete(odin_worker_t *worker)
{
	delete worker;
}

void
odin_worker_post(odin_worker_t *worker, void (*job)(void *ctx), void *ctx)
{
	worker->worker.post([job, ctx] { job(ctx); });
}

void
odin_worker_wait(odin_worker_t *worker)
{
	worker->worker.wait();
}

void
odin_worker_run(odin_worker_t *worker, void (*job)(void *ctx), void *ctx)
{
	worker->worker.run([job, ctx] { job(ctx); });
}

} /* extern "C" */
