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

/* The Odin SH-4 (src/odin/sh4.hpp) as a CPU of the machine. The sh4_t of
 * the interpreter it replaced is kept as its face: the save files keep
 * their layout, and a patch finds the registers where it always did. */

#include "vk/core.h"
#include "vk/cpu.h"
#include "vk/state.h"

#include <time.h>

#include "sh4.h"
#include "odin/sh4_bridge.hpp"

/* The state of the core, in the fields of the interpreter. */
void
sh4_odin_pull (sh4_t *ctx)
{
	odin_sh4_state_t st;
	unsigned i;

	odin_sh4_save_state (ctx->odin, &st);

	ctx->base.state = (vk_cpu_state_t) st.run;
	ctx->in_slot = false;

	memcpy (ctx->regs.r, st.r, sizeof (st.r));
	ctx->regs.pc = st.pc;
	ctx->regs.sr.full = st.sr;
	ctx->regs.pr = st.pr;
	ctx->regs.gbr = st.gbr;
	ctx->regs.vbr = st.vbr;
	ctx->regs.dbr = st.dbr;
	ctx->regs.mac.field.lo = st.macl;
	ctx->regs.mac.field.hi = st.mach;
	ctx->regs.spc = st.spc;
	ctx->regs.ssr.full = st.ssr;
	ctx->regs.sgr = st.sgr;
	memcpy (ctx->regs.rbank, st.rbank, sizeof (st.rbank));
	for (i = 0; i < 16; i++) {
		ctx->regs.f.f[i].u = st.fr[i];
		ctx->regs.x.f[i].u = st.xf[i];
	}
	ctx->regs.fpul.u = st.fpul;
	ctx->regs.fpscr.full = st.fpscr;

	ctx->intc.pending = st.irq_pending != 0;
	ctx->intc.index = st.irq_index;
	for (i = 0; i < SH4_NUM_IESOURCES; i++) {
		ctx->intc.irqs[i].state = st.irq[i].raised ?
		                          VK_IRQ_STATE_RAISED : VK_IRQ_STATE_CLEAR;
		ctx->intc.irqs[i].priority = st.irq[i].priority;
		ctx->intc.irqs[i].offset = st.irq[i].offset;
		ctx->intc.irqs[i].code = st.irq[i].code;
	}
	for (i = 0; i < 4; i++)
		ctx->dmac.is_running[i] = st.dmac_running[i] != 0;
	for (i = 0; i < 3; i++) {
		ctx->tmu.is_running[i] = st.tmu_running[i] != 0;
		ctx->tmu.counter[i] = st.tmu_counter[i];
	}
	for (i = 0; i < 2; i++) {
		ctx->pm.running[i] = st.pm_running[i] != 0;
		ctx->pm.counter[i] = st.pm_counter[i];
	}
}

/* And back. */
void
sh4_odin_push (sh4_t *ctx)
{
	odin_sh4_state_t st;
	unsigned i;

	memset (&st, 0, sizeof (st));

	st.run = (uint32_t) ctx->base.state;

	memcpy (st.r, ctx->regs.r, sizeof (st.r));
	st.pc = ctx->regs.pc;
	st.sr = ctx->regs.sr.full;
	st.pr = ctx->regs.pr;
	st.gbr = ctx->regs.gbr;
	st.vbr = ctx->regs.vbr;
	st.dbr = ctx->regs.dbr;
	st.macl = ctx->regs.mac.field.lo;
	st.mach = ctx->regs.mac.field.hi;
	st.spc = ctx->regs.spc;
	st.ssr = ctx->regs.ssr.full;
	st.sgr = ctx->regs.sgr;
	memcpy (st.rbank, ctx->regs.rbank, sizeof (st.rbank));
	for (i = 0; i < 16; i++) {
		st.fr[i] = ctx->regs.f.f[i].u;
		st.xf[i] = ctx->regs.x.f[i].u;
	}
	st.fpul = ctx->regs.fpul.u;
	st.fpscr = ctx->regs.fpscr.full;

	st.irq_pending = ctx->intc.pending;
	st.irq_index = ctx->intc.index;
	for (i = 0; i < SH4_NUM_IESOURCES; i++) {
		st.irq[i].raised = ctx->intc.irqs[i].state == VK_IRQ_STATE_RAISED;
		st.irq[i].priority = ctx->intc.irqs[i].priority;
		st.irq[i].offset = ctx->intc.irqs[i].offset;
		st.irq[i].code = ctx->intc.irqs[i].code;
	}
	for (i = 0; i < 4; i++)
		st.dmac_running[i] = ctx->dmac.is_running[i];
	for (i = 0; i < 3; i++) {
		st.tmu_running[i] = ctx->tmu.is_running[i];
		st.tmu_counter[i] = ctx->tmu.counter[i];
	}
	for (i = 0; i < 2; i++) {
		st.pm_running[i] = ctx->pm.running[i];
		st.pm_counter[i] = ctx->pm.counter[i];
	}

	odin_sh4_load_state (ctx->odin, &st);
}

/* Bus */

static int
bus_read (void *opaque, uint32_t addr, unsigned size, uint64_t *value)
{
	sh4_t *ctx = (sh4_t *) opaque;
	uint64_t tmp = 0;
	int ret;

	ret = vk_mmap_get (ctx->base.mmap, size, addr, &tmp);
	*value = tmp;
	return ret;
}

static int
bus_write (void *opaque, uint32_t addr, unsigned size, uint64_t value)
{
	sh4_t *ctx = (sh4_t *) opaque;

	return vk_mmap_put (ctx->base.mmap, size, addr, value);
}

static uint16_t
bus_port_a_read (void *opaque)
{
	sh4_t *ctx = (sh4_t *) opaque;
	uint16_t pins = 0;

	VK_ASSERT (ctx->porta.get);
	if (ctx->porta.get (ctx, &pins))
		VK_ASSERT (0);
	return pins;
}

static void
bus_port_a_write (void *opaque, uint16_t pins)
{
	sh4_t *ctx = (sh4_t *) opaque;

	VK_ASSERT (ctx->porta.put);
	if (ctx->porta.put (ctx, pins))
		VK_ASSERT (0);
}

static void
bus_unanswered (void *opaque, int is_write, uint32_t addr, unsigned size, uint64_t value)
{
	sh4_t *ctx = (sh4_t *) opaque;

	if (is_write) {
		VK_CPU_ERROR (ctx, "unhandled W%d @%08X = %lX", 8*size, addr, value);
	} else {
		VK_CPU_ERROR (ctx, "unhandled R%d @%08X", 8*size, addr);
	}
}

/* The patch of the machine, at its PC: it may change R0-R15 and count
 * cycles as run. */
static void
bus_hook (void *opaque)
{
	sh4_t *ctx = (sh4_t *) opaque;
	vk_cpu_t *cpu = &ctx->base;
	uint32_t pc = odin_sh4_pc (ctx->odin);
	int left = odin_sh4_remaining (ctx->odin);
	unsigned i;

	for (i = 0; i < 16; i++)
		ctx->regs.r[i] = odin_sh4_reg (ctx->odin, i);
	ctx->regs.pc = pc;
	ctx->regs.pr = odin_sh4_pr (ctx->odin);
	ctx->regs.gbr = odin_sh4_gbr (ctx->odin);
	ctx->regs.sr.full = odin_sh4_sr (ctx->odin);
	ctx->in_slot = false;
	cpu->remaining = left;

	cpu->patch (cpu, pc & 0x1FFFFFFF, 0);

	for (i = 0; i < 16; i++)
		odin_sh4_set_reg (ctx->odin, i, ctx->regs.r[i]);
	if (cpu->remaining < left)
		odin_sh4_skip (ctx->odin, (uint32_t) (left - cpu->remaining));
}

/* ODIN_SH4_STATS=1: at the end, the host time each CPU took in its
 * slices. */
static bool stats_on;
static uint64_t stats_ns[2], stats_slices[2];

static void
stats_print (void)
{
	unsigned i;

	for (i = 0; i < 2; i++)
		fprintf (stderr, "sh4 %c: %.3f s in %lu slices\n", i ? 'S' : 'M',
		         (double) stats_ns[i] / 1e9, stats_slices[i]);
}

static void
stats_open (void)
{
	if (!stats_on && getenv ("ODIN_SH4_STATS")) {
		stats_on = true;
		atexit (stats_print);
	}
}

static uint64_t
stats_begin (void)
{
	struct timespec now;

	if (!stats_on)
		return 0;
	clock_gettime (CLOCK_MONOTONIC, &now);
	return (uint64_t) now.tv_sec * 1000000000ull + (uint64_t) now.tv_nsec;
}

static void
stats_end (sh4_t *ctx, uint64_t begin)
{
	struct timespec now;
	unsigned i = ctx->config.master ? 0 : 1;

	if (!stats_on)
		return;
	clock_gettime (CLOCK_MONOTONIC, &now);
	stats_ns[i] += (uint64_t) now.tv_sec * 1000000000ull + (uint64_t) now.tv_nsec - begin;
	stats_slices[i]++;
}

/* ODIN_SH4_TRACE=<prefix>: one line per slice in <prefix>-m.txt and
 * <prefix>-s.txt, the PC and a hash of the registers. */

static void
trace_open (sh4_t *ctx)
{
	const char *prefix = getenv ("ODIN_SH4_TRACE");
	char path[1024];

	ctx->trace = NULL;
	if (prefix) {
		snprintf (path, sizeof (path), "%s-%c.txt", prefix,
		          ctx->config.master ? 'm' : 's');
		ctx->trace = fopen (path, "w");
	}
}

static uint64_t
hash_bytes (uint64_t hash, const void *data, size_t size)
{
	const uint8_t *p = (const uint8_t *) data;
	size_t i;

	/* FNV-1a */
	for (i = 0; i < size; i++)
		hash = (hash ^ p[i]) * 0x100000001B3ull;
	return hash;
}

static void
trace_slice (sh4_t *ctx)
{
	uint64_t hash = 0xCBF29CE484222325ull;

	/* Field by field: the padding of the structure is left out. */
	hash = hash_bytes (hash, ctx->regs.r, sizeof (ctx->regs.r));
	hash = hash_bytes (hash, &ctx->regs.pc, sizeof (uint32_t));
	hash = hash_bytes (hash, &ctx->regs.sr.full, sizeof (uint32_t));
	hash = hash_bytes (hash, &ctx->regs.pr, sizeof (uint32_t));
	hash = hash_bytes (hash, &ctx->regs.gbr, sizeof (uint32_t));
	hash = hash_bytes (hash, &ctx->regs.vbr, sizeof (uint32_t));
	hash = hash_bytes (hash, &ctx->regs.dbr, sizeof (uint32_t));
	hash = hash_bytes (hash, &ctx->regs.mac.full, sizeof (uint64_t));
	hash = hash_bytes (hash, &ctx->regs.spc, sizeof (uint32_t));
	hash = hash_bytes (hash, &ctx->regs.ssr.full, sizeof (uint32_t));
	hash = hash_bytes (hash, &ctx->regs.sgr, sizeof (uint32_t));
	hash = hash_bytes (hash, ctx->regs.rbank, sizeof (ctx->regs.rbank));
	hash = hash_bytes (hash, &ctx->regs.f, sizeof (ctx->regs.f));
	hash = hash_bytes (hash, &ctx->regs.x, sizeof (ctx->regs.x));
	hash = hash_bytes (hash, &ctx->regs.fpul.u, sizeof (uint32_t));
	hash = hash_bytes (hash, &ctx->regs.fpscr.full, sizeof (uint32_t));
	hash = hash_bytes (hash, ctx->tmu.counter, sizeof (ctx->tmu.counter));
	hash = hash_bytes (hash, &ctx->intc.index, sizeof (ctx->intc.index));

	fprintf (ctx->trace, "%08X %016lX\n", ctx->regs.pc, hash);
}

/* The CPU of the machine */

static int
sh4o_run (vk_cpu_t *cpu, int cycles)
{
	sh4_t *ctx = (sh4_t *) cpu;
	uint64_t begin;
	int over;

	if (cpu->patch && ctx->hooked_pc != cpu->patch_pc) {
		odin_sh4_set_hook (ctx->odin, cpu->patch_pc);
		ctx->hooked_pc = cpu->patch_pc;
	}

	begin = stats_begin ();
	over = odin_sh4_run (ctx->odin, cycles);
	stats_end (ctx, begin);

	cpu->state = (vk_cpu_state_t) odin_sh4_get_run (ctx->odin);
	cpu->remaining = -over;
	if (ctx->trace) {
		sh4_odin_pull (ctx);
		trace_slice (ctx);
	}
	return over;
}

static void
sh4o_set_state (vk_cpu_t *cpu, vk_cpu_state_t state)
{
	sh4_t *ctx = (sh4_t *) cpu;

	cpu->state = state;
	odin_sh4_set_run (ctx->odin, (unsigned) state);
}

static int
sh4o_set_irq_state (vk_cpu_t *cpu, unsigned num, vk_irq_state_t state)
{
	sh4_t *ctx = (sh4_t *) cpu;

	if (num >= SH4_NUM_IESOURCES)
		return -1;
	odin_sh4_set_irq (ctx->odin, num, state == VK_IRQ_STATE_RAISED);
	return 0;
}

static const char *
sh4o_get_debug_string (vk_cpu_t *cpu)
{
	sh4_t *ctx = (sh4_t *) cpu;
	static char str[256];

	sprintf (str, "%c @%08X @%08X %08X",
	         ctx->config.master ? 'M' : 'S', odin_sh4_pc (ctx->odin),
	         odin_sh4_pr (ctx->odin), odin_sh4_sr (ctx->odin));
	return str;
}

/* The plain memory of the map, page by page: what the core reads and
 * writes without the bus. */
static void
map_pages (sh4_t *ctx)
{
	vk_mmap_t *mmap = ctx->base.mmap;
	uint32_t page;

	for (page = 0; page < 0x20000000 / ODIN_SH4_PAGE_SIZE; page++) {
		uint32_t addr = page * ODIN_SH4_PAGE_SIZE;
		odin_sh4_map_page (ctx->odin, page,
		                   vk_mmap_get_page (mmap, addr, ODIN_SH4_PAGE_SIZE, false),
		                   vk_mmap_get_page (mmap, addr, ODIN_SH4_PAGE_SIZE, true));
	}
}

static void
sh4o_reset (vk_device_t *dev, vk_reset_type_t type)
{
	sh4_t *ctx = (sh4_t *) dev;

	map_pages (ctx);
	odin_sh4_reset (ctx->odin, type == VK_RESET_TYPE_HARD);
	sh4_odin_pull (ctx);
}

#define SAVE(thing_) \
		ret |= vk_state_put (state, (void *) &(thing_), sizeof (thing_))

#define LOAD(thing_) \
		ret |= vk_state_get (state, (void *) &(thing_), sizeof (thing_))

static int
sh4o_load_state (vk_device_t *dev, vk_state_t *state)
{
	sh4_t *ctx = (sh4_t *) dev;
	int ret = 0;

	LOAD (ctx->base.state);
	LOAD (ctx->in_slot);
	LOAD (ctx->regs);
	LOAD (ctx->wait);
	LOAD (ctx->intc);
	LOAD (ctx->dmac);
	LOAD (ctx->tmu);
	LOAD (ctx->pm);
	LOAD (ctx->config);

	sh4_odin_push (ctx);
	return ret;
}

static int
sh4o_save_state (vk_device_t *dev, vk_state_t *state)
{
	sh4_t *ctx = (sh4_t *) dev;
	int ret = 0;

	sh4_odin_pull (ctx);
	/* What the interpreter keeps of its search for waits: none here. */
	memset (&ctx->wait, 0, sizeof (ctx->wait));

	SAVE (ctx->base.state);
	SAVE (ctx->in_slot);
	SAVE (ctx->regs);
	SAVE (ctx->wait);
	SAVE (ctx->intc);
	SAVE (ctx->dmac);
	SAVE (ctx->tmu);
	SAVE (ctx->pm);
	SAVE (ctx->config);

	return ret;
}

#undef SAVE
#undef LOAD

static void
sh4o_destroy (vk_device_t **dev_)
{
	sh4_t *ctx = (sh4_t *) *dev_;

	if (ctx->trace)
		fclose (ctx->trace);
	if (getenv ("ODIN_SH4_STATS"))
		fprintf (stderr, "sh4 %c: %lu instructions run, %lu cycles skipped\n",
		         ctx->config.master ? 'M' : 'S',
		         odin_sh4_instructions (ctx->odin),
		         odin_sh4_skipped (ctx->odin));
	odin_sh4_delete (ctx->odin);
	ctx->odin = NULL;
}

void
sh4_set_porta_handlers (vk_cpu_t *cpu,
                        int (* get)(sh4_t *ctx, uint16_t *val),
                        int (* put)(sh4_t *ctx, uint16_t val))
{
	sh4_t *ctx = (sh4_t *) cpu;

	ctx->porta.get = get;
	ctx->porta.put = put;
}

vk_cpu_t *
sh4_new (vk_machine_t *mach, vk_mmap_t *mmap, bool master, bool le)
{
	odin_sh4_bus_t bus;
	vk_device_t *dev;
	vk_cpu_t *cpu;
	sh4_t *ctx;

	VK_CPU_ALLOC (ctx, mach, mmap);
	cpu = (vk_cpu_t *) ctx;
	dev = (vk_device_t *) ctx;
	if (!ctx)
		goto fail;

	dev->reset		= sh4o_reset;
	dev->destroy		= sh4o_destroy;
	dev->load_state		= sh4o_load_state;
	dev->save_state		= sh4o_save_state;

	cpu->set_state		= sh4o_set_state;
	cpu->run		= sh4o_run;
	cpu->set_irq_state	= sh4o_set_irq_state;
	cpu->get_debug_string	= sh4o_get_debug_string;

	ctx->config.master = master;
	ctx->config.little_endian = le;
	ctx->hooked_pc = 0xFFFFFFFF;

	ctx->iregs = vk_buffer_le32_new (0x10000, 0);
	if (!ctx->iregs)
		goto fail;

	vk_machine_register_buffer (mach, ctx->iregs);

	bus.ctx = ctx;
	bus.read = bus_read;
	bus.write = bus_write;
	bus.port_a_read = bus_port_a_read;
	bus.port_a_write = bus_port_a_write;
	bus.unanswered = bus_unanswered;
	bus.hook = bus_hook;

	ctx->odin = odin_sh4_new (&bus, (uint8_t *) vk_buffer_get_ptr (ctx->iregs, 0),
	                          master, le);
	if (!ctx->odin)
		goto fail;

	trace_open (ctx);
	stats_open ();

	return (vk_cpu_t *) ctx;
fail:
	vk_device_destroy (&dev);
	return NULL;
}
