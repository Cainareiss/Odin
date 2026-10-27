/*
 *  ██████╗ ██████╗ ██╗███╗   ██╗
 * ██╔═══██╗██╔══██╗██║████╗  ██║
 * ██║   ██║██║  ██║██║██╔██╗ ██║
 * ██║   ██║██║  ██║██║██║╚██╗██║
 * ╚██████╔╝██████╔╝██║██║ ╚████║
 *  ╚═════╝ ╚═════╝ ╚═╝╚═╝  ╚═══╝
 *
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

/* Unit tests for the Odin scheduler: the dates of the events and the
 * order in which those of one date are taken.
 *
 * Build: g++ -std=c++20 -O2 -Wall -Wextra -Wpedantic -Wconversion -Wshadow \
 *            -I src tests/scheduler/scheduler_tests.cpp src/odin/machine/core/scheduler.cpp \
 *            -o scheduler_tests
 * Exit status is 0 when every check passes. */

#include "odin/machine/core/scheduler.hpp"

#include <cstdio>
#include <vector>

namespace {

using odin::Scheduler;
using odin::Time;

int g_checks, g_failures;

void check(bool ok, const char *what)
{
	g_checks++;
	if (!ok) {
		g_failures++;
		std::printf("FAIL: %s\n", what);
	}
}

void test_dates()
{
	Scheduler s;
	uint32_t source = 99;

	check(s.now() == 0, "time 0 at power-on");
	check(s.next() == Scheduler::NEVER, "nothing to come at power-on");
	check(!s.take(source), "nothing due at power-on");

	s.set(3, 500);
	s.set(1, 200);
	s.set(7, 900);
	check(s.next() == 200, "the earliest of three dates");

	s.advance(120);
	check(s.now() == 120 && !s.take(source), "nothing due before the first date");
	s.advance(200);
	check(s.take(source) && source == 1, "the first event is due at its date");
	check(!s.take(source), "an event is taken once");
	check(s.next() == 500, "the next date after a take");

	s.set(3, 300);
	check(s.next() == 300, "a source dated again keeps its new date only");
	s.advance(300);
	check(s.take(source) && source == 3 && s.next() == 900, "and is due once");

	s.advance(900);
	check(s.take(source) && source == 7 && s.next() == Scheduler::NEVER, "the last event");

	s.reset();
	check(s.now() == 0 && s.next() == Scheduler::NEVER, "reset goes back to power-on");
}

void test_same_date()
{
	Scheduler s;
	uint32_t source = 99;
	uint32_t order[3] = { 99, 99, 99 };

	s.set(9, 1000);
	s.set(2, 1000);
	s.set(5, 1000);
	s.advance(1000);
	for (uint32_t i = 0; i < 3; i++) {
		check(s.take(source), "three events due at one date");
		order[i] = source;
	}
	check(order[0] == 2 && order[1] == 5 && order[2] == 9, "the lowest source first");

	/* An event dated now while the others are taken is due at once. */
	s.set(4, 2000);
	s.set(6, 2000);
	s.advance(2000);
	check(s.take(source) && source == 4, "first of two");
	s.set(0, 2000);
	check(s.take(source) && source == 0, "an event dated now is taken next");
	check(s.take(source) && source == 6, "then the one that was left");
}

/* A frame cut as the machine cuts it: lines of a fixed length, and two
 * events that fall on the end of a line. */
void test_frame()
{
	enum : uint32_t { LINE, VBLANK_IN, FRAME_END };
	constexpr Time CYCLES = 6944;
	constexpr uint32_t LINES = 544, SHOWN = 480;
	Scheduler s;
	uint32_t source = 99, line = 0, slices = 0, vblank_line = 0;
	bool done = false, cut_right = true;

	s.set(LINE, CYCLES);
	s.set(VBLANK_IN, SHOWN * CYCLES);
	s.set(FRAME_END, LINES * CYCLES);
	while (!done) {
		const Time next = s.next();
		cut_right = cut_right && next - s.now() == CYCLES;
		slices++;
		s.advance(next);
		while (s.take(source)) {
			if (source == LINE) {
				if (++line < LINES) {
					s.set(LINE, next + CYCLES);
				}
			} else if (source == VBLANK_IN) {
				vblank_line = line;
			} else {
				done = true;
			}
		}
	}
	check(slices == LINES && cut_right, "one slice of a line for each line");
	check(vblank_line == SHOWN, "the vertical blanking after the last line shown");
	check(line == LINES && s.now() == LINES * CYCLES, "the frame ends after its last line");
	check(s.next() == Scheduler::NEVER, "nothing is left at the end of the frame");
}

void test_save_load()
{
	Scheduler a, b;
	odin::SchedulerState state;
	uint32_t source = 99;

	a.set(2, 40);
	a.set(5, 90);
	a.advance(40);
	a.save(state);
	b.set(7, 10);
	b.load(state);
	check(b.now() == 40 && b.next() == 40, "a loaded scheduler is at the saved time");
	check(b.take(source) && source == 2 && b.next() == 90, "with the saved events only");
	check(a.take(source) && source == 2 && a.next() == 90, "saving changes nothing");
}

} /* namespace */

int main()
{
	test_dates();
	test_same_date();
	test_frame();
	test_save_load();
	std::printf("%d checks, %d failures\n", g_checks, g_failures);
	return g_failures == 0 ? 0 : 1;
}
