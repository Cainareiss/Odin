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

#ifndef ODIN_IO_LINE_HPP
#define ODIN_IO_LINE_HPP

#include "odin/mie.hpp"

#include <cstdint>
#include <span>

namespace odin {

/* An I/O board that runs its own program at the far end of UART 1 of the
 * MIE (MieBoard::run_line). Its time is that of the line: ticks of
 * 1/9.216 GHz from its reset. */
class IoLineBoard : public MieLine {
public:
	virtual void reset() = 0;
	/* A coin through slot 0 or 1 */
	virtual void inc_coin(uint8_t slot) = 0;
	/* Runs the board up to `tick`, giving it the bytes of the MIE at
	 * their time: then the first event for the MIE before it is known. */
	virtual void run_until(uint64_t tick) = 0;
	/* Where the board is, in ticks */
	virtual uint64_t now() const = 0;
	/* The tick the next Mie::run of the MIE starts at, for line_sent */
	virtual void set_run_start(uint64_t tick) = 0;
	/* The tick of the first event for the MIE, UINT64_MAX for none */
	virtual uint64_t next_event() const = 0;
	/* Gives the MIE the events up to `tick` */
	virtual void deliver(Mie &mie, uint64_t tick) = 0;
	/* The outputs as JVS numbers them: output 1 is bit 7 of the first byte */
	virtual std::span<const uint8_t> outputs() const = 0;
};

} /* namespace odin */

#endif /* ODIN_IO_LINE_HPP */
