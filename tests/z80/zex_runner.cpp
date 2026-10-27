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

/* Runs CP/M Z80 exercisers (prelim.com, zexdoc.com, zexall.com) on the
 * Odin Z80 core. The program loads at 0100h; address 0005h holds a RET
 * that stands in for BDOS (function 2 prints E, function 9 prints the
 * '$'-terminated string at DE); a jump to 0000h ends the run.
 *
 * Build: g++ -std=c++20 -O2 -I src tests/z80/zex_runner.cpp \
 *            src/odin/machine/cpu/z80/z80.cpp src/odin/machine/cpu/z80/z80_ops.cpp -o zex_runner
 * Use:   zex_runner prelim.com zexdoc.com zexall.com
 * Exit status is 0 when no exerciser reports an error. */

#include "odin/machine/cpu/z80/z80.hpp"

#include <array>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace {

class CpmBus : public odin::Z80Bus {
public:
	std::array<uint8_t, 65536> mem{};

	uint8_t read8(uint16_t addr) override { return mem[addr]; }
	void write8(uint16_t addr, uint8_t value) override { mem[addr] = value; }
	uint8_t in8(uint16_t) override { return 0xFF; }
	void out8(uint16_t, uint8_t) override {}
};

bool load_com(const char *path, CpmBus &bus)
{
	FILE *fp = std::fopen(path, "rb");
	if (fp == nullptr) {
		std::fprintf(stderr, "cannot open %s\n", path);
		return false;
	}
	size_t n = std::fread(bus.mem.data() + 0x100, 1, bus.mem.size() - 0x100, fp);
	std::fclose(fp);
	std::printf("%s: %zu bytes at 0100h\n", path, n);
	return n > 0;
}

struct RunResult {
	std::string output;
	uint64_t instructions = 0;
	uint64_t cycles = 0;
	double seconds = 0.0;
};

RunResult run_program(const char *path)
{
	RunResult res;
	CpmBus bus;
	if (!load_com(path, bus)) {
		return res;
	}
	bus.mem[0x0005] = 0xC9;
	/* (0006h) is the BDOS entry, which the exerciser uses as its stack top. */
	bus.mem[0x0006] = 0x00;
	bus.mem[0x0007] = 0xFF;

	odin::Z80 cpu(bus);
	cpu.reset();
	odin::Z80State st{};
	cpu.save(st);
	st.pc = 0x0100;
	st.sp = 0xFF00;
	cpu.load(st);

	auto t0 = std::chrono::steady_clock::now();
	while (cpu.pc() != 0x0000) {
		if (cpu.pc() == 0x0005) {
			uint8_t fn = static_cast<uint8_t>(cpu.bc() & 0xFF);
			if (fn == 2) {
				char ch = static_cast<char>(cpu.de() & 0xFF);
				res.output.push_back(ch);
				std::fputc(ch, stdout);
			} else if (fn == 9) {
				uint16_t addr = cpu.de();
				while (bus.mem[addr] != '$') {
					char ch = static_cast<char>(bus.mem[addr]);
					res.output.push_back(ch);
					std::fputc(ch, stdout);
					addr++;
				}
			}
			std::fflush(stdout);
		}
		res.cycles += cpu.step();
		res.instructions++;
	}
	auto t1 = std::chrono::steady_clock::now();
	res.seconds = std::chrono::duration<double>(t1 - t0).count();
	std::printf("\n");
	return res;
}

size_t count_substr(const std::string &s, const char *needle)
{
	size_t count = 0;
	size_t pos = 0;
	size_t len = std::strlen(needle);
	while ((pos = s.find(needle, pos)) != std::string::npos) {
		count++;
		pos += len;
	}
	return count;
}

} /* namespace */

int main(int argc, char **argv)
{
	if (argc < 2) {
		std::fprintf(stderr, "usage: %s file.com...\n", argv[0]);
		return 2;
	}
	bool all_ok = true;
	std::vector<std::string> summary;
	for (int i = 1; i < argc; i++) {
		RunResult r = run_program(argv[i]);
		size_t errors = count_substr(r.output, "ERROR");
		bool prelim_ok = r.output.find("Preliminary tests complete") != std::string::npos;
		bool zex_ok = r.output.find("Tests complete") != std::string::npos;
		bool ok = errors == 0 && (prelim_ok || zex_ok);
		all_ok = all_ok && ok;
		double mips = r.seconds > 0.0 ? static_cast<double>(r.instructions) / r.seconds / 1e6 : 0.0;
		summary.push_back(std::string(argv[i]) + ": " + std::to_string(errors) + " errors, "
			+ std::to_string(r.instructions) + " instructions, "
			+ std::to_string(r.cycles) + " T-states, "
			+ std::to_string(r.seconds) + " s, "
			+ std::to_string(mips) + " M instr/s"
			+ (ok ? "" : " [FAILED]"));
	}
	for (const std::string &line : summary) {
		std::printf("%s\n", line.c_str());
	}
	return all_ok ? 0 : 1;
}
