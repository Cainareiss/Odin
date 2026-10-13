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

/* C entry points to odin::Worker (src/odin/worker.hpp). */

#ifndef ODIN_WORKER_BRIDGE_HPP
#define ODIN_WORKER_BRIDGE_HPP

#ifdef __cplusplus
extern "C" {
#endif

typedef struct odin_worker odin_worker_t;

odin_worker_t *odin_worker_new(void);
/* Waits for the job given last, then ends the thread. */
void odin_worker_delete(odin_worker_t *worker);

/* Gives a job once the one given last is done, and returns without waiting
 * for it. */
void odin_worker_post(odin_worker_t *worker, void (*job)(void *ctx), void *ctx);
/* Waits for the job given last. */
void odin_worker_wait(odin_worker_t *worker);
/* Gives a job and waits for it. */
void odin_worker_run(odin_worker_t *worker, void (*job)(void *ctx), void *ctx);

#ifdef __cplusplus
}
#endif

#endif /* ODIN_WORKER_BRIDGE_HPP */
