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

/* Golden trace replay for the ARM7 core against a MAME run of the AICA
 * sound driver. The sound RAM is a real 8 MB array loaded from a dump
 * taken when the ARM left reset; every read of the AICA register
 * window 0x00800000-0x00807FFF returns the next value of the MAME tap
 * log; reads of the mailbox area of RAM are checked against the tap
 * log too and resynchronised when another bus master wrote there.
 * FIQ entries are injected where the MAME trace shows PC = 0x1C.
 *
 * Build: g++ -std=c++20 -O2 -Wall -Wextra -Wpedantic -Wconversion -Wshadow \
 *            -I src tests/arm7/arm7_replay.cpp src/odin/arm7.cpp src/odin/arm7_ops.cpp -o arm7_replay
 * Use:   arm7_replay --ram dump.bin --regs regs.tsv --trace mame.tr [--cpu arm0]
 *                    [--mbox mbox.tsv] [--max N] [--trace-out odin.tr] [--quiet]
 * Exit status is 0 when the whole replay matches. */

#include "odin/arm7.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

namespace {

struct TapRecord {
	char rw;
	uint32_t addr;
	uint32_t data;
	uint32_t mask;
	uint32_t pc;
};

std::vector<TapRecord> load_taps(const char *path, const char *cpu)
{
	std::vector<TapRecord> out;
	FILE *fp = std::fopen(path, "r");
	if (fp == nullptr) {
		std::fprintf(stderr, "cannot open %s\n", path);
		return out;
	}
	char line[512];
	while (std::fgets(line, sizeof line, fp) != nullptr) {
		if (line[0] == '#') {
			continue;
		}
		char name[32];
		char rw[8];
		unsigned addr, data, mask, pc;
		double t;
		unsigned seq;
		if (std::sscanf(line, "%u %lf %31s %7s %x %x %x %x", &seq, &t, name, rw, &addr, &data, &mask, &pc) != 8) {
			continue;
		}
		if (std::strcmp(name, cpu) != 0) {
			continue;
		}
		out.push_back({ rw[0], addr, data, mask, pc });
	}
	std::fclose(fp);
	return out;
}

/* One MAME trace line: "R0=.. R1=.. ... CPSR=.. 00000510: mov r0, #0". */
struct TraceLine {
	uint32_t pc;
	uint32_t r[15];      /* R0-R12 user, R13, R14 user */
	uint32_t fiq[7];     /* FR8-FR14 */
	uint32_t irq[2], svc[2], abt[2], und[2];
	uint32_t spsr[5];    /* FR16 IR16 SR16 AR16 UR16 */
	uint32_t cpsr;
	bool has_banks;
};

bool parse_trace_line(const char *line, TraceLine &out)
{
	out = TraceLine{};
	const char *p = line;
	bool have_pc = false;
	while (*p != 0) {
		while (*p == ' ' || *p == '\t') {
			p++;
		}
		if (*p == 0 || *p == '\n') {
			break;
		}
		const char *start = p;
		while (*p != 0 && *p != ' ' && *p != '\t' && *p != '\n') {
			p++;
		}
		std::string tok(start, static_cast<size_t>(p - start));
		size_t eq = tok.find('=');
		if (eq != std::string::npos) {
			std::string key = tok.substr(0, eq);
			uint32_t v = static_cast<uint32_t>(std::strtoul(tok.c_str() + eq + 1, nullptr, 16));
			if (key == "CPSR") {
				out.cpsr = v;
			} else if (key.size() >= 2 && key[0] == 'R') {
				unsigned idx = static_cast<unsigned>(std::atoi(key.c_str() + 1));
				if (idx < 15) {
					out.r[idx] = v;
				}
			} else if (key.size() >= 3 && key[1] == 'R') {
				unsigned idx = static_cast<unsigned>(std::atoi(key.c_str() + 2));
				out.has_banks = true;
				switch (key[0]) {
				case 'F':
					if (idx == 16) {
						out.spsr[0] = v;
					} else if (idx >= 8 && idx <= 14) {
						out.fiq[idx - 8] = v;
					}
					break;
				case 'I':
					if (idx == 16) {
						out.spsr[1] = v;
					} else if (idx == 13 || idx == 14) {
						out.irq[idx - 13] = v;
					}
					break;
				case 'S':
					if (idx == 16) {
						out.spsr[2] = v;
					} else if (idx == 13 || idx == 14) {
						out.svc[idx - 13] = v;
					}
					break;
				case 'A':
					if (idx == 16) {
						out.spsr[3] = v;
					} else if (idx == 13 || idx == 14) {
						out.abt[idx - 13] = v;
					}
					break;
				case 'U':
					if (idx == 16) {
						out.spsr[4] = v;
					} else if (idx == 13 || idx == 14) {
						out.und[idx - 13] = v;
					}
					break;
				default:
					break;
				}
			}
		} else if (!have_pc && tok.size() >= 2 && tok.back() == ':') {
			out.pc = static_cast<uint32_t>(std::strtoul(tok.c_str(), nullptr, 16));
			have_pc = true;
			break;
		}
	}
	return have_pc;
}

class ReplayBus : public odin::Arm7Bus {
public:
	std::vector<uint8_t> ram;
	std::vector<TapRecord> regs;
	std::vector<TapRecord> mbox;
	size_t reg_pos = 0;
	size_t mbox_pos = 0;
	uint64_t reg_mismatches = 0;
	uint64_t mbox_fixes = 0;
	uint64_t ram_oob = 0;
	bool verbose = true;
	uint64_t instruction = 0;

	ReplayBus() : ram(8u << 20, 0) {}

	static bool is_reg(uint32_t addr) { return addr >= 0x00800000 && addr < 0x00808000; }
	static bool is_mbox(uint32_t addr) { return addr >= 0x40 && addr < 0x500; }

	uint32_t ram32(uint32_t addr) const
	{
		addr &= 0x7FFFFC;
		return static_cast<uint32_t>(ram[addr]) | (static_cast<uint32_t>(ram[addr + 1]) << 8)
			| (static_cast<uint32_t>(ram[addr + 2]) << 16) | (static_cast<uint32_t>(ram[addr + 3]) << 24);
	}
	void set_ram32(uint32_t addr, uint32_t v)
	{
		addr &= 0x7FFFFC;
		ram[addr] = static_cast<uint8_t>(v);
		ram[addr + 1] = static_cast<uint8_t>(v >> 8);
		ram[addr + 2] = static_cast<uint8_t>(v >> 16);
		ram[addr + 3] = static_cast<uint8_t>(v >> 24);
	}

	/* Register window read: the next tap record must be a read of the
	 * same dword. A mismatch is reported and the record is still used. */
	uint32_t reg_read(uint32_t addr, uint32_t size)
	{
		uint32_t shift = (addr & 3) * 8;
		uint32_t lane_mask = size == 4 ? 0xFFFFFFFFu : (size == 2 ? 0xFFFFu : 0xFFu);
		if (reg_pos >= regs.size()) {
			if (verbose) {
				std::printf("instr %llu: register read %08x beyond the end of the tap log\n", static_cast<unsigned long long>(instruction), addr);
			}
			reg_mismatches++;
			return 0;
		}
		const TapRecord &rec = regs[reg_pos++];
		if (rec.rw != 'R' || (rec.addr & ~3u) != (addr & ~3u)) {
			reg_mismatches++;
			if (verbose) {
				std::printf("instr %llu: register read %08x size %u but tap log has %c %08x data %08x mask %08x (pc %08x)\n",
					static_cast<unsigned long long>(instruction), addr, size, rec.rw, rec.addr, rec.data, rec.mask, rec.pc);
			}
		}
		return (rec.data >> shift) & lane_mask;
	}

	void reg_write(uint32_t addr, uint32_t size, uint32_t v)
	{
		uint32_t shift = (addr & 3) * 8;
		uint32_t lane_mask = size == 4 ? 0xFFFFFFFFu : (size == 2 ? 0xFFFFu : 0xFFu);
		if (reg_pos >= regs.size()) {
			reg_mismatches++;
			if (verbose) {
				std::printf("instr %llu: register write %08x beyond the end of the tap log\n", static_cast<unsigned long long>(instruction), addr);
			}
			return;
		}
		const TapRecord &rec = regs[reg_pos++];
		uint32_t logged = (rec.data >> shift) & lane_mask;
		if (rec.rw != 'W' || (rec.addr & ~3u) != (addr & ~3u) || logged != v) {
			reg_mismatches++;
			if (verbose) {
				std::printf("instr %llu: register write %08x size %u value %08x but tap log has %c %08x data %08x mask %08x (pc %08x)\n",
					static_cast<unsigned long long>(instruction), addr, size, v, rec.rw, rec.addr, rec.data, rec.mask, rec.pc);
			}
		}
	}

	/* Mailbox RAM: the ARM side tap log says what MAME's ARM saw. When
	 * our RAM differs on a read, another master (the SH-4) wrote there
	 * and the RAM is patched with the logged dword. */
	void mbox_sync(char rw, uint32_t addr)
	{
		if (mbox.empty()) {
			return;
		}
		while (mbox_pos < mbox.size() && mbox[mbox_pos].rw != rw) {
			mbox_pos++;
		}
		if (mbox_pos >= mbox.size()) {
			return;
		}
		const TapRecord &rec = mbox[mbox_pos++];
		if ((rec.addr & ~3u) != (addr & ~3u)) {
			if (verbose) {
				std::printf("instr %llu: mailbox %c %08x but tap log has %c %08x (pc %08x)\n",
					static_cast<unsigned long long>(instruction), rw, addr, rec.rw, rec.addr, rec.pc);
			}
			return;
		}
		if (rw == 'R') {
			uint32_t mine = ram32(addr) & rec.mask;
			if (mine != (rec.data & rec.mask)) {
				mbox_fixes++;
				if (verbose) {
					std::printf("instr %llu: mailbox %08x holds %08x, MAME's ARM read %08x (mask %08x): external write applied\n",
						static_cast<unsigned long long>(instruction), addr & ~3u, ram32(addr), rec.data, rec.mask);
				}
				set_ram32(addr, (ram32(addr) & ~rec.mask) | (rec.data & rec.mask));
			}
		}
	}

	uint8_t read8(uint32_t addr) override
	{
		if (is_reg(addr)) {
			return static_cast<uint8_t>(reg_read(addr, 1));
		}
		if (addr >= ram.size()) {
			ram_oob++;
			return 0xFF;
		}
		if (is_mbox(addr)) {
			mbox_sync('R', addr);
		}
		return ram[addr];
	}
	uint16_t read16(uint32_t addr) override
	{
		if (is_reg(addr)) {
			return static_cast<uint16_t>(reg_read(addr, 2));
		}
		if (addr >= ram.size()) {
			ram_oob++;
			return 0xFFFF;
		}
		if (is_mbox(addr)) {
			mbox_sync('R', addr);
		}
		return static_cast<uint16_t>(ram[addr] | (ram[addr + 1] << 8));
	}
	uint32_t read32(uint32_t addr) override
	{
		if (is_reg(addr)) {
			return reg_read(addr, 4);
		}
		if (addr >= ram.size()) {
			ram_oob++;
			return 0xFFFFFFFF;
		}
		if (is_mbox(addr)) {
			mbox_sync('R', addr);
		}
		return ram32(addr);
	}
	void write8(uint32_t addr, uint8_t v) override
	{
		if (is_reg(addr)) {
			reg_write(addr, 1, v);
			return;
		}
		if (addr >= ram.size()) {
			ram_oob++;
			return;
		}
		if (is_mbox(addr)) {
			mbox_sync('W', addr);
		}
		ram[addr] = v;
	}
	void write16(uint32_t addr, uint16_t v) override
	{
		if (is_reg(addr)) {
			reg_write(addr, 2, v);
			return;
		}
		if (addr >= ram.size()) {
			ram_oob++;
			return;
		}
		if (is_mbox(addr)) {
			mbox_sync('W', addr);
		}
		ram[addr] = static_cast<uint8_t>(v);
		ram[addr + 1] = static_cast<uint8_t>(v >> 8);
	}
	void write32(uint32_t addr, uint32_t v) override
	{
		if (is_reg(addr)) {
			reg_write(addr, 4, v);
			return;
		}
		if (addr >= ram.size()) {
			ram_oob++;
			return;
		}
		if (is_mbox(addr)) {
			mbox_sync('W', addr);
		}
		set_ram32(addr, v);
	}
};

bool load_ram(const char *path, std::vector<uint8_t> &ram)
{
	FILE *fp = std::fopen(path, "rb");
	if (fp == nullptr) {
		std::fprintf(stderr, "cannot open %s\n", path);
		return false;
	}
	size_t n = std::fread(ram.data(), 1, ram.size(), fp);
	std::fclose(fp);
	std::printf("%s: %zu bytes of sound RAM\n", path, n);
	return n > 0;
}

struct Diff {
	std::string what;
	uint32_t mine, theirs;
};

void compare(const odin::Arm7 &cpu, const TraceLine &t, std::vector<Diff> &diffs)
{
	odin::Arm7State s{};
	cpu.save(s);
	auto cmp = [&](const char *what, uint32_t mine, uint32_t theirs) {
		if (mine != theirs) {
			diffs.push_back({ what, mine, theirs });
		}
	};
	cmp("pc", s.pc, t.pc);
	cmp("cpsr", s.cpsr, t.cpsr);
	const char *names[15] = { "r0", "r1", "r2", "r3", "r4", "r5", "r6", "r7", "r8", "r9", "r10", "r11", "r12", "r13", "r14" };
	for (uint32_t i = 0; i < 8; i++) {
		cmp(names[i], s.r[i], t.r[i]);
	}
	for (uint32_t i = 8; i < 15; i++) {
		cmp(names[i], s.usr[i - 8], t.r[i]);
	}
	if (!t.has_banks) {
		return;
	}
	const char *fiq_names[7] = { "fr8", "fr9", "fr10", "fr11", "fr12", "fr13", "fr14" };
	for (uint32_t i = 0; i < 7; i++) {
		cmp(fiq_names[i], s.fiq[i], t.fiq[i]);
	}
	cmp("ir13", s.irq[0], t.irq[0]); cmp("ir14", s.irq[1], t.irq[1]);
	cmp("sr13", s.svc[0], t.svc[0]); cmp("sr14", s.svc[1], t.svc[1]);
	cmp("ar13", s.abt[0], t.abt[0]); cmp("ar14", s.abt[1], t.abt[1]);
	cmp("ur13", s.und[0], t.und[0]); cmp("ur14", s.und[1], t.und[1]);
	cmp("spsr_fiq", s.spsr[0], t.spsr[0]);
	cmp("spsr_irq", s.spsr[1], t.spsr[1]);
	cmp("spsr_svc", s.spsr[2], t.spsr[2]);
	cmp("spsr_abt", s.spsr[3], t.spsr[3]);
	cmp("spsr_und", s.spsr[4], t.spsr[4]);
}

} /* namespace */

int main(int argc, char **argv)
{
	const char *ram_path = nullptr;
	const char *regs_path = nullptr;
	const char *trace_path = nullptr;
	const char *mbox_path = nullptr;
	const char *trace_out = nullptr;
	const char *cpu_name = "arm0";
	uint64_t max_instructions = 0;
	bool quiet = false;
	for (int i = 1; i < argc; i++) {
		if (std::strcmp(argv[i], "--ram") == 0 && i + 1 < argc) {
			ram_path = argv[++i];
		} else if (std::strcmp(argv[i], "--regs") == 0 && i + 1 < argc) {
			regs_path = argv[++i];
		} else if (std::strcmp(argv[i], "--trace") == 0 && i + 1 < argc) {
			trace_path = argv[++i];
		} else if (std::strcmp(argv[i], "--mbox") == 0 && i + 1 < argc) {
			mbox_path = argv[++i];
		} else if (std::strcmp(argv[i], "--trace-out") == 0 && i + 1 < argc) {
			trace_out = argv[++i];
		} else if (std::strcmp(argv[i], "--cpu") == 0 && i + 1 < argc) {
			cpu_name = argv[++i];
		} else if (std::strcmp(argv[i], "--max") == 0 && i + 1 < argc) {
			max_instructions = std::strtoull(argv[++i], nullptr, 0);
		} else if (std::strcmp(argv[i], "--quiet") == 0) {
			quiet = true;
		} else {
			std::fprintf(stderr, "unknown argument %s\n", argv[i]);
			return 2;
		}
	}
	if (ram_path == nullptr || regs_path == nullptr || trace_path == nullptr) {
		std::fprintf(stderr, "usage: %s --ram dump.bin --regs regs.tsv --trace mame.tr [--cpu arm0] [--mbox mbox.tsv] [--max N] [--trace-out file] [--quiet]\n", argv[0]);
		return 2;
	}

	ReplayBus bus;
	bus.verbose = !quiet;
	if (!load_ram(ram_path, bus.ram)) {
		return 2;
	}
	bus.regs = load_taps(regs_path, cpu_name);
	std::printf("%s: %zu register accesses of %s\n", regs_path, bus.regs.size(), cpu_name);
	if (mbox_path != nullptr) {
		bus.mbox = load_taps(mbox_path, cpu_name);
		std::printf("%s: %zu mailbox accesses of %s\n", mbox_path, bus.mbox.size(), cpu_name);
	}

	FILE *tr = std::fopen(trace_path, "r");
	if (tr == nullptr) {
		std::fprintf(stderr, "cannot open %s\n", trace_path);
		return 2;
	}
	odin::Arm7 cpu(bus);
	cpu.reset();
	FILE *out = nullptr;
	if (trace_out != nullptr) {
		out = std::fopen(trace_out, "w");
		cpu.set_trace(out);
	}

	uint64_t matched = 0;
	uint64_t fiqs = 0;
	uint64_t lines = 0;
	uint32_t recent[8] = {};
	bool diverged = false;
	std::vector<char> line(4096);
	while (std::fgets(line.data(), static_cast<int>(line.size()), tr) != nullptr) {
		TraceLine t;
		if (!parse_trace_line(line.data(), t)) {
			continue;
		}
		lines++;
		if (max_instructions != 0 && lines > max_instructions) {
			break;
		}
		if (t.pc == 0x1C && cpu.pc() != 0x1C) {
			cpu.set_fiq(true);
			cpu.poll_interrupts();
			cpu.set_fiq(false);
			fiqs++;
		}
		std::vector<Diff> diffs;
		compare(cpu, t, diffs);
		if (!diffs.empty()) {
			std::printf("divergence at trace line %llu (after %llu matched instructions), MAME pc %08x:\n",
				static_cast<unsigned long long>(lines), static_cast<unsigned long long>(matched), t.pc);
			for (const Diff &d : diffs) {
				std::printf("  %-9s odin %08x  mame %08x\n", d.what.c_str(), d.mine, d.theirs);
			}
			std::printf("  recent odin pcs:");
			for (uint32_t pc : recent) {
				std::printf(" %08x", pc);
			}
			std::printf("\n  mame line: %s", line.data());
			diverged = true;
			break;
		}
		matched++;
		for (uint32_t i = 7; i > 0; i--) {
			recent[i] = recent[i - 1];
		}
		recent[0] = cpu.pc();
		bus.instruction = matched;
		cpu.step();
	}
	std::fclose(tr);
	if (out != nullptr) {
		cpu.set_trace(nullptr);
		std::fclose(out);
	}
	std::printf("%llu instructions matched of %llu trace lines, %llu FIQ entries injected, %llu register accesses replayed of %zu, %llu register mismatches, %llu mailbox fixes, %llu out-of-range RAM accesses\n",
		static_cast<unsigned long long>(matched), static_cast<unsigned long long>(lines), static_cast<unsigned long long>(fiqs),
		static_cast<unsigned long long>(bus.reg_pos), bus.regs.size(), static_cast<unsigned long long>(bus.reg_mismatches),
		static_cast<unsigned long long>(bus.mbox_fixes), static_cast<unsigned long long>(bus.ram_oob));
	return diverged || bus.reg_mismatches != 0 ? 1 : 0;
}
