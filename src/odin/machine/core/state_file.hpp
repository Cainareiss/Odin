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

#ifndef ODIN_STATE_FILE_HPP
#define ODIN_STATE_FILE_HPP

#include <cstdint>
#include <cstdio>
#include <string>

namespace odin {

/* A save state on disk: a line that names the format, then items, each
 * its size in 32 bits followed by its bytes. An item is read back into a
 * thing of the same size only: a state made by a build that lays its data
 * out otherwise is refused instead of misread. */
class StateFile {
public:
	/* 3: the command processor keeps its own state (antarctic.hpp);
	 * 4: the light numbers are kept in a light set;
	 * 5: the sound chips keep their voices (aica_voice.hpp);
	 * 6: the effect processor no longer keeps the length of its program;
	 * 7: a sound board keeps the cycles its ARM7 ran past a period;
	 * 8: the MIE board keeps the clocks its Z80 ran past its share;
	 * 9: it keeps the chip of the 837-13551 and its line;
	 * 10: it keeps which processor asks for the bus of the MIE, the slave
	 * reaching it through its own window;
	 * 11: a ROM board without the 315-5881 keeps the RAM of its local bus;
	 * 12: the MIE keeps its converter channel, its external interrupt
	 * and the request of the transmitter of UART 1;
	 * 13: it keeps the chip of the 837-13741 and its line;
	 * 14: the SH-4 as its core keeps it, and none of the words the
	 * board of Valkyrie kept;
	 * 15: the card reader keeps where its card is and its tracks as on
	 * the card;
	 * 16: a card reader only for a cabinet that has one */
	static constexpr unsigned VERSION = 16;

	enum class Mode {
		SAVE,
		LOAD
	};

	/* Opens the file and writes its first line, or checks it: ok()
	 * tells whether that went well. */
	StateFile(const std::string &path, Mode mode);
	~StateFile();
	StateFile(const StateFile &) = delete;
	StateFile &operator=(const StateFile &) = delete;

	bool ok() const { return m_file != nullptr; }
	/* For the tests */
	bool loading() const { return m_mode == Mode::LOAD; }

	/* One item. False when it could not be written, or read into that
	 * size. */
	bool put(const void *data, uint32_t size);
	bool get(void *data, uint32_t size);
	/* The size of the next item, left to be read; 0 at the end of the
	 * state. An item of a board the state may not have is known by it. */
	uint32_t next_size();
	bool skip();

	/* A plain thing as one item */
	template <typename T>
	bool put(const T &thing)
	{
		return put(&thing, static_cast<uint32_t>(sizeof(thing)));
	}
	template <typename T>
	bool get(T &thing)
	{
		return get(&thing, static_cast<uint32_t>(sizeof(thing)));
	}

private:
	std::FILE *m_file;
	Mode m_mode;
};

} /* namespace odin */

#endif /* ODIN_STATE_FILE_HPP */
