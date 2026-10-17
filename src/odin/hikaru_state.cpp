/*
 * Odin
 * Copyright (C) 2026 Réda Chérif-Touil
 *
 * Valkyrie
 * Copyright (C) 2011-2013, Stefano Teso
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

#include "odin/hikaru.hpp"

#include <array>
#include <cstring>

#include "odin/state_file.hpp"

/*
 * Save states
 * ===========
 *
 * A state is the board between two frames: the name of the game and the
 * number of the frame, the memories, the chips one after the other, what
 * the board itself holds, then the pictures the screen keeps from a frame
 * to the next. The order is part of the format (StateFile::VERSION): a
 * state is read back item by item in the order it was written.
 *
 * Some items are there for the format only: they held what the board once
 * kept and are now written as zero, or as the SH-4 was laid out before its
 * core was written again. A new version of the format can drop them.
 */

namespace odin {

namespace {

/* The SH-4 as the states keep it, each structure an item. */
struct CpuRegs {
	uint32_t r[16];
	uint32_t pc, sr, pr, gbr, vbr, dbr;
	uint32_t macl, mach;
	uint32_t spc, ssr, sgr;
	uint32_t rbank[8];
	uint32_t unused;
	uint32_t fr[16], xf[16];
	uint32_t fpul, fpscr;
};

/* Always zero */
struct CpuWait {
	CpuRegs regs;
	uint32_t unused[4];
};

struct CpuIrqs {
	uint32_t pending;	/* in its low byte */
	int32_t index;
	Sh4IrqSource source[Sh4Source::COUNT];
};

struct CpuTimers {
	uint8_t running[3];
	uint8_t unused;
	uint32_t counter[3];
};

struct CpuCounters {
	uint8_t running[2];
	uint8_t unused[6];
	uint64_t counter[2];
};

/* Which SH-4 it is, and its byte order */
struct CpuConfig {
	uint8_t master, little_endian;
};

static_assert(sizeof(CpuRegs) == 280 && sizeof(CpuWait) == 296 && sizeof(CpuIrqs) == 728 &&
              sizeof(CpuTimers) == 16 && sizeof(CpuCounters) == 24 && sizeof(CpuConfig) == 2);

bool
put_bytes(StateFile &state, std::span<const uint8_t> bytes)
{
	return state.put(bytes.data(), static_cast<uint32_t>(bytes.size()));
}

bool
get_bytes(StateFile &state, std::span<uint8_t> bytes)
{
	return state.get(bytes.data(), static_cast<uint32_t>(bytes.size()));
}

/* A chip saves and loads a block of plain data: one item. */
template <typename Block, typename Chip>
bool
put_chip(StateFile &state, Chip &chip)
{
	const auto block = std::make_unique<Block>();

	std::memset(block.get(), 0, sizeof(Block));
	chip.save(*block);
	return state.put(*block);
}

template <typename Block, typename Chip>
bool
get_chip(StateFile &state, Chip &chip)
{
	const auto block = std::make_unique<Block>();

	if (!state.get(*block))
		return false;
	chip.load(*block);
	return true;
}

} /* namespace */

bool
Hikaru::save_cpu(StateFile &state, unsigned which)
{
	Sh4State st;
	CpuRegs regs;
	CpuWait wait;
	CpuIrqs irqs;
	CpuTimers timers;
	CpuCounters counters;
	const uint8_t in_slot = 0;
	const CpuConfig config = { which == MASTER, true };

	m_sh[which]->save(st);

	std::memset(&regs, 0, sizeof(regs));
	std::memcpy(regs.r, st.r, sizeof(regs.r));
	regs.pc = st.pc;
	regs.sr = st.sr;
	regs.pr = st.pr;
	regs.gbr = st.gbr;
	regs.vbr = st.vbr;
	regs.dbr = st.dbr;
	regs.macl = st.macl;
	regs.mach = st.mach;
	regs.spc = st.spc;
	regs.ssr = st.ssr;
	regs.sgr = st.sgr;
	std::memcpy(regs.rbank, st.rbank, sizeof(regs.rbank));
	std::memcpy(regs.fr, st.fr, sizeof(regs.fr));
	std::memcpy(regs.xf, st.xf, sizeof(regs.xf));
	regs.fpul = st.fpul;
	regs.fpscr = st.fpscr;

	std::memset(&wait, 0, sizeof(wait));

	std::memset(&irqs, 0, sizeof(irqs));
	irqs.pending = st.irq_pending != 0;
	irqs.index = st.irq_index;
	for (unsigned i = 0; i < Sh4Source::COUNT; i++) {
		irqs.source[i] = st.irq[i];
		irqs.source[i].raised = st.irq[i].raised != 0;
	}

	std::memset(&timers, 0, sizeof(timers));
	std::memset(&counters, 0, sizeof(counters));
	for (unsigned i = 0; i < 3; i++) {
		timers.running[i] = st.tmu_running[i] != 0;
		timers.counter[i] = st.tmu_counter[i];
	}
	for (unsigned i = 0; i < 2; i++) {
		counters.running[i] = st.pm_running[i] != 0;
		counters.counter[i] = st.pm_counter[i];
	}

	uint8_t dmac[4];

	for (unsigned i = 0; i < 4; i++)
		dmac[i] = st.dmac_running[i] != 0;

	return state.put(st.run) && state.put(in_slot) && state.put(regs) && state.put(wait) &&
	       state.put(irqs) && state.put(dmac) && state.put(timers) && state.put(counters) &&
	       state.put(st.over) && state.put(config);
}

bool
Hikaru::load_cpu(StateFile &state, unsigned which)
{
	Sh4State st;
	CpuRegs regs;
	CpuWait wait;
	CpuIrqs irqs;
	CpuTimers timers;
	CpuCounters counters;
	CpuConfig config;
	uint8_t in_slot, dmac[4];

	std::memset(&st, 0, sizeof(st));
	if (!state.get(st.run) || !state.get(in_slot) || !state.get(regs) || !state.get(wait) ||
	    !state.get(irqs) || !state.get(dmac) || !state.get(timers) || !state.get(counters) ||
	    !state.get(st.over) || !state.get(config))
		return false;

	std::memcpy(st.r, regs.r, sizeof(st.r));
	st.pc = regs.pc;
	st.sr = regs.sr;
	st.pr = regs.pr;
	st.gbr = regs.gbr;
	st.vbr = regs.vbr;
	st.dbr = regs.dbr;
	st.macl = regs.macl;
	st.mach = regs.mach;
	st.spc = regs.spc;
	st.ssr = regs.ssr;
	st.sgr = regs.sgr;
	std::memcpy(st.rbank, regs.rbank, sizeof(st.rbank));
	std::memcpy(st.fr, regs.fr, sizeof(st.fr));
	std::memcpy(st.xf, regs.xf, sizeof(st.xf));
	st.fpul = regs.fpul;
	st.fpscr = regs.fpscr;

	st.irq_pending = (irqs.pending & 0xFF) != 0;
	st.irq_index = irqs.index;
	for (unsigned i = 0; i < Sh4Source::COUNT; i++) {
		st.irq[i] = irqs.source[i];
		st.irq[i].raised = irqs.source[i].raised == 1;
	}
	for (unsigned i = 0; i < 4; i++)
		st.dmac_running[i] = dmac[i];
	for (unsigned i = 0; i < 3; i++) {
		st.tmu_running[i] = timers.running[i];
		st.tmu_counter[i] = timers.counter[i];
	}
	for (unsigned i = 0; i < 2; i++) {
		st.pm_running[i] = counters.running[i];
		st.pm_counter[i] = counters.counter[i];
	}

	m_sh[which]->load(st);
	return true;
}

bool
Hikaru::save_items(StateFile &state)
{
	char name[32] = "";
	const uint32_t unused = 0;
	/* Where the states keep 32 bytes no chip uses now */
	const std::array<uint8_t, 32> unused_regs = {};
	RomBoard::Config rombd;
	bool ok;

	std::strncpy(name, m_game.name.c_str(), sizeof(name) - 1);
	if (!state.put(name) || !state.put(m_frame)) {
		std::fprintf(stderr, "ERROR: save state: cannot save header\n");
		return false;
	}

	/* The memories */
	ok = put_bytes(state, m_ram[MASTER]) && put_bytes(state, m_ram[SLAVE]) &&
	     put_bytes(state, m_cmdram) && put_bytes(state, m_fb) &&
	     put_bytes(state, m_texram[0]) && put_bytes(state, m_texram[1]) &&
	     put_bytes(state, m_sound_ram[MASTER]) && put_bytes(state, m_sound_ram[SLAVE]) &&
	     put_bytes(state, m_mie_ram) && put_bytes(state, m_bram) &&
	     put_bytes(state, m_memctl[MASTER].registers()) &&
	     put_bytes(state, m_memctl[SLAVE].registers()) &&
	     put_bytes(state, m_mscomm) && put_bytes(state, unused_regs);
	if (ok && m_rombd.crypt())
		ok = put_bytes(state, m_rombd.crypt_ram());
	ok = ok && put_bytes(state, m_iregs[MASTER]) && put_bytes(state, m_iregs[SLAVE]);
	if (!ok) {
		std::fprintf(stderr, "ERROR: save state: cannot save buffer\n");
		return false;
	}

	/* The chips */
	ok = state.put(unused) && put_chip<MieBoard::State>(state, *m_mie);
	ok = ok && put_chip<HikaruGpu::State>(state, *m_gpu) &&
	     put_chip<SoundBoard::State>(state, *m_sound[MASTER]) &&
	     put_chip<SoundBoard::State>(state, *m_sound[SLAVE]) &&
	     save_cpu(state, MASTER) && save_cpu(state, SLAVE);
	if (!ok) {
		std::fprintf(stderr, "ERROR: save state: cannot save device\n");
		return false;
	}

	/* The board */
	m_rombd.save(rombd);
	ok = state.put(m_porta[MASTER]) && state.put(unused) &&
	     state.put(m_porta[SLAVE]) && state.put(unused) &&
	     state.put(unused) && state.put(unused) &&
	     state.put(m_unk1A800008_s) && state.put(m_unk1B000100_s) &&
	     state.put(rombd) &&
	     put_chip<Eeprom93c46State>(state, m_eeprom) &&
	     put_chip<X76f100State>(state, m_rombd.eeprom());
	if (ok && m_rombd.crypt())
		ok = put_chip<Crypt315_5881State>(state, *m_rombd.crypt());
	if (ok && m_rombd.fpga())
		ok = put_chip<ActelM1State>(state, *m_rombd.fpga());
	ok = ok && (!m_screen || m_screen->save_pictures(state)) &&
	     put_chip<SchedulerState>(state, m_sched) &&
	     state.put(m_line) && state.put(m_dots_per_line) && state.put(m_lines_per_frame) &&
	     state.put(m_lines_shown) && state.put(m_line_rest);
	if (!ok)
		std::fprintf(stderr, "ERROR: save state: cannot save machine\n");
	return ok;
}

bool
Hikaru::load_items(StateFile &state)
{
	char name[32];
	uint32_t unused;
	std::array<uint8_t, 32> unused_regs;
	RomBoard::Config rombd;
	bool ok;

	ok = state.get(name);
	if (ok && std::strncmp(name, m_game.name.c_str(), sizeof(name))) {
		std::fprintf(stderr, "ERROR: load state: made with the game '%.32s'\n", name);
		ok = false;
	}
	if (!ok || !state.get(m_frame)) {
		std::fprintf(stderr, "ERROR: load state: cannot load header\n");
		return false;
	}

	ok = get_bytes(state, m_ram[MASTER]) && get_bytes(state, m_ram[SLAVE]) &&
	     get_bytes(state, m_cmdram) && get_bytes(state, m_fb) &&
	     get_bytes(state, m_texram[0]) && get_bytes(state, m_texram[1]) &&
	     get_bytes(state, m_sound_ram[MASTER]) && get_bytes(state, m_sound_ram[SLAVE]) &&
	     get_bytes(state, m_mie_ram) && get_bytes(state, m_bram) &&
	     get_bytes(state, m_memctl[MASTER].registers()) &&
	     get_bytes(state, m_memctl[SLAVE].registers()) &&
	     get_bytes(state, m_mscomm) && get_bytes(state, unused_regs);
	if (ok && m_rombd.crypt())
		ok = get_bytes(state, m_rombd.crypt_ram());
	ok = ok && get_bytes(state, m_iregs[MASTER]) && get_bytes(state, m_iregs[SLAVE]);
	if (!ok) {
		std::fprintf(stderr, "ERROR: load state: cannot load buffer\n");
		return false;
	}

	ok = state.get(unused);
	if (ok) {
		const auto block = std::make_unique<MieBoard::State>();

		ok = state.get(*block) && m_mie->load(*block);
	}
	ok = ok && get_chip<HikaruGpu::State>(state, *m_gpu) &&
	     get_chip<SoundBoard::State>(state, *m_sound[MASTER]) &&
	     get_chip<SoundBoard::State>(state, *m_sound[SLAVE]) &&
	     load_cpu(state, MASTER) && load_cpu(state, SLAVE);
	if (!ok) {
		std::fprintf(stderr, "ERROR: load state: cannot load device\n");
		return false;
	}

	ok = state.get(m_porta[MASTER]) && state.get(unused) &&
	     state.get(m_porta[SLAVE]) && state.get(unused) &&
	     state.get(unused) && state.get(unused) &&
	     state.get(m_unk1A800008_s) && state.get(m_unk1B000100_s) &&
	     state.get(rombd);
	if (ok)
		m_rombd.load(rombd);
	ok = ok && get_chip<Eeprom93c46State>(state, m_eeprom) &&
	     get_chip<X76f100State>(state, m_rombd.eeprom());
	if (ok && m_rombd.crypt())
		ok = get_chip<Crypt315_5881State>(state, *m_rombd.crypt());
	if (ok && m_rombd.fpga())
		ok = get_chip<ActelM1State>(state, *m_rombd.fpga());
	ok = ok && (!m_screen || m_screen->load_pictures(state)) &&
	     get_chip<SchedulerState>(state, m_sched) &&
	     state.get(m_line) && state.get(m_dots_per_line) && state.get(m_lines_per_frame) &&
	     state.get(m_lines_shown) && state.get(m_line_rest);
	if (!ok) {
		std::fprintf(stderr, "ERROR: load state: cannot load machine\n");
		return false;
	}

	/* What the board works out from its chips */
	quiet_windows();
	m_sh[MASTER]->set_time(m_sched.now());
	m_sh[SLAVE]->set_time(m_sched.now());
	m_mie_port_d = m_mie ? m_mie->port_d() : 0xFF;
	set_gpu_reset_lines();
	update_irqs();
	return true;
}

bool
Hikaru::save_state(const std::string &path)
{
	StateFile state(path, StateFile::Mode::SAVE);

	if (!state.ok()) {
		std::fprintf(stderr, "ERROR: save state: cannot write '%s'\n", path.c_str());
		return false;
	}
	settle();
	return save_items(state);
}

/* The state is loaded into a board just powered on; one that cannot be
 * loaded to its end leaves the board that way. */
bool
Hikaru::load_state(const std::string &path)
{
	bool ok;

	{
		StateFile state(path, StateFile::Mode::LOAD);

		ok = state.ok();
		if (ok) {
			reset();
			ok = load_items(state);
		} else {
			std::fprintf(stderr, "ERROR: load state: '%s' is missing or is not a state of this build\n", path.c_str());
		}
	}
	if (!ok) {
		std::fprintf(stderr, "ERROR: load state: resetting machine\n");
		reset();
	}
	return ok;
}

} /* namespace odin */
