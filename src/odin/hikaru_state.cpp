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
 */

namespace odin {

namespace {

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
Hikaru::save_items(StateFile &state)
{
	char name[32] = "";
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
	     put_bytes(state, m_mscomm);
	if (ok && !m_rombd.local_ram().empty())
		ok = put_bytes(state, m_rombd.local_ram());
	ok = ok && put_bytes(state, m_iregs[MASTER]) && put_bytes(state, m_iregs[SLAVE]);
	if (!ok) {
		std::fprintf(stderr, "ERROR: save state: cannot save buffer\n");
		return false;
	}

	/* The chips */
	ok = put_chip<MieBoard::State>(state, *m_mie) && put_chip<HikaruGpu::State>(state, *m_gpu) &&
	     put_chip<SoundBoard::State>(state, *m_sound[MASTER]) &&
	     put_chip<SoundBoard::State>(state, *m_sound[SLAVE]) &&
	     put_chip<Sh4State>(state, *m_sh[MASTER]) && put_chip<Sh4State>(state, *m_sh[SLAVE]);
	if (!ok) {
		std::fprintf(stderr, "ERROR: save state: cannot save device\n");
		return false;
	}

	/* The board */
	m_rombd.save(rombd);
	ok = state.put(m_porta[MASTER]) && state.put(m_porta[SLAVE]) && state.put(rombd) &&
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
	/* The drive board, when it runs its program: last, a state without it
	 * still loads */
	if (ok && m_drive)
		ok = put_chip<Drive838_13366::State>(state, *m_drive);
	if (!ok)
		std::fprintf(stderr, "ERROR: save state: cannot save machine\n");
	return ok;
}

bool
Hikaru::load_items(StateFile &state)
{
	char name[32];
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
	     get_bytes(state, m_mscomm);
	if (ok && !m_rombd.local_ram().empty())
		ok = get_bytes(state, m_rombd.local_ram());
	ok = ok && get_bytes(state, m_iregs[MASTER]) && get_bytes(state, m_iregs[SLAVE]);
	if (!ok) {
		std::fprintf(stderr, "ERROR: load state: cannot load buffer\n");
		return false;
	}

	{
		const auto block = std::make_unique<MieBoard::State>();

		ok = state.get(*block) && m_mie->load(*block);
	}
	ok = ok && get_chip<HikaruGpu::State>(state, *m_gpu) &&
	     get_chip<SoundBoard::State>(state, *m_sound[MASTER]) &&
	     get_chip<SoundBoard::State>(state, *m_sound[SLAVE]) &&
	     get_chip<Sh4State>(state, *m_sh[MASTER]) && get_chip<Sh4State>(state, *m_sh[SLAVE]);
	if (!ok) {
		std::fprintf(stderr, "ERROR: load state: cannot load device\n");
		return false;
	}

	ok = state.get(m_porta[MASTER]) && state.get(m_porta[SLAVE]) && state.get(rombd);
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
	if (m_drive && !get_chip<Drive838_13366::State>(state, *m_drive)) {
		std::printf("load state: no drive board in it: the board starts again\n");
		m_drive->reset();
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
