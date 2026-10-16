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

/* Runs a real AICA sound driver image on the ARM7 core under a stub
 * AICA: 8 MB of sound RAM, a register file at 0x00800000 that gives
 * back what was written, timers A/B/C with the SCIEB/SCIPD/SCIRE
 * interrupt bits, the SCILV levels on 0x2D00 and the interrupt clear at
 * 0x2D04. The SH-4 side is replayed as the game does it before it
 * releases the ARM reset, and optionally the command ring protocol of
 * a bank load. Every register access and every write to the mailbox
 * area of RAM is logged in order.
 *
 * Build: g++ -std=c++20 -O2 -Wall -Wextra -Wpedantic -Wconversion -Wshadow \
 *            -I src tests/arm7/arm7_driver.cpp src/odin/arm7.cpp src/odin/arm7_ops.cpp \
 *            src/odin/arm7_host.cpp src/odin/arm7_translate.cpp src/odin/host_code.cpp -o arm7_driver
 * Use:   arm7_driver --image von4-aica-m.bin [--instructions N] [--bank-load]
 *                    [--trace-out file] [--no-timers] [--reads-zero]
 * Exit status is 0 when the driver ran without undefined instructions
 * and answered the SH-4 when asked. */

#include "odin/arm7.hpp"

#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

namespace {

struct Access {
	uint64_t instr;
	uint32_t pc;
	char rw;
	uint32_t offset;
	uint32_t size;
	uint32_t value;
};

/* ARM clock of the AICA is 22.5792 MHz, 512 clocks per 44.1 kHz sample;
 * a timer counts samples divided by 2^prescale and interrupts when the
 * 8-bit count wraps. */
constexpr uint32_t CYCLES_PER_SAMPLE = 512;
constexpr uint32_t REG_MVOL = 0x2800, REG_TIMA = 0x2890, REG_TIMB = 0x2894, REG_TIMC = 0x2898;
constexpr uint32_t REG_SCIEB = 0x289C, REG_SCIPD = 0x28A0, REG_SCIRE = 0x28A4;
constexpr uint32_t REG_SCILV0 = 0x28A8, REG_SCILV1 = 0x28AC, REG_SCILV2 = 0x28B0;
constexpr uint32_t REG_MCIEB = 0x28B4, REG_MCIPD = 0x28B8, REG_MCIRE = 0x28BC;
constexpr uint32_t REG_ARMRST = 0x2C00, REG_INTREQ = 0x2D00, REG_INTCLR = 0x2D04;
constexpr uint32_t SCI_TIMA = 0x40, SCI_TIMB = 0x80, SCI_TIMC = 0x100;

class StubAica : public odin::Arm7Bus {
public:
	std::vector<uint8_t> ram;
	std::array<uint16_t, 0x2000> reg{};
	std::vector<Access> log;
	std::vector<Access> ram_log;        /* writes to 0x00-0xFF and 0x400-0x4FF */
	odin::Arm7 *cpu = nullptr;
	uint64_t instr = 0;
	bool timers_on = true;
	bool reads_zero = false;
	uint32_t irql = 0;
	bool fiq_request = false;
	uint64_t fiq_count = 0;
	uint64_t timer_events = 0;
	uint64_t mcipd_writes = 0;
	uint64_t undefined = 0;
	struct Timer {
		uint32_t count = 0;
		uint32_t prescale = 0;
		uint32_t acc = 0;
	};
	std::array<Timer, 3> timers;

	StubAica() : ram(8u << 20, 0) {}

	static bool is_reg(uint32_t addr) { return addr >= 0x00800000 && addr < 0x00808000; }

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

	/* Interrupt level of source bit `src` from SCILV0-2; sources above 7
	 * share bit 7, as the timers B and C do. */
	uint32_t level_of(uint32_t src) const
	{
		uint32_t b = src > 7 ? 7 : src;
		return ((reg[REG_SCILV0 / 4] >> b) & 1) | (((reg[REG_SCILV1 / 4] >> b) & 1) << 1) | (((reg[REG_SCILV2 / 4] >> b) & 1) << 2);
	}

	void check_irq()
	{
		uint32_t pend = reg[REG_SCIPD / 4] & reg[REG_SCIEB / 4];
		if (pend & SCI_TIMA) {
			irql = level_of(6);
		} else if (pend & SCI_TIMB) {
			irql = level_of(7);
		} else if (pend & SCI_TIMC) {
			irql = level_of(8);
		} else {
			return;
		}
		if (!fiq_request) {
			fiq_request = true;
			fiq_count++;
		}
		cpu->set_fiq(true);
	}

	void clear_request()
	{
		fiq_request = false;
		cpu->set_fiq(false);
	}

	void tick(uint32_t cycles)
	{
		if (!timers_on) {
			return;
		}
		for (uint32_t i = 0; i < 3; i++) {
			Timer &t = timers[i];
			t.acc += cycles;
			uint32_t period = CYCLES_PER_SAMPLE << t.prescale;
			while (t.acc >= period) {
				t.acc -= period;
				t.count++;
				if (t.count > 0xFF) {
					t.count = 0;
					timer_events++;
					reg[REG_SCIPD / 4] |= static_cast<uint16_t>(SCI_TIMA << i);
					check_irq();
				}
			}
		}
	}

	void reg_write(uint32_t offset, uint32_t size, uint32_t v, bool from_arm)
	{
		uint32_t idx = (offset & 0x7FFC) / 4;
		uint32_t shift = (offset & 3) * 8;
		uint32_t old = reg[idx];
		uint32_t merged;
		if (size == 4) {
			merged = v & 0xFFFF;
		} else if (size == 2) {
			merged = (offset & 2) ? old : (v & 0xFFFF);
		} else {
			uint32_t lane = 0xFFu << shift;
			merged = (shift < 16) ? ((old & ~lane) | ((v << shift) & lane)) & 0xFFFF : old;
		}
		if (from_arm) {
			log.push_back({ instr, cpu->pc(), 'W', offset & 0x7FFF, size, v });
		}
		uint32_t aligned = offset & 0x7FFC;
		switch (aligned) {
		case REG_TIMA:
		case REG_TIMB:
		case REG_TIMC: {
			Timer &t = timers[(aligned - REG_TIMA) / 4];
			t.count = merged & 0xFF;
			t.prescale = (merged >> 8) & 7;
			t.acc = 0;
			reg[idx] = static_cast<uint16_t>(merged);
			break;
		}
		case REG_SCIPD:
			/* Only the software interrupt bit can be set by a write. */
			reg[idx] = static_cast<uint16_t>(old | (merged & 0x20));
			check_irq();
			break;
		case REG_SCIRE:
			reg[REG_SCIPD / 4] = static_cast<uint16_t>(reg[REG_SCIPD / 4] & ~merged);
			reg[idx] = 0;
			clear_request();
			check_irq();
			break;
		case REG_SCIEB:
			reg[idx] = static_cast<uint16_t>(merged);
			check_irq();
			break;
		case REG_MCIPD:
			reg[idx] = static_cast<uint16_t>(old | merged);
			if (merged) {
				mcipd_writes++;
			}
			break;
		case REG_MCIRE:
			reg[REG_MCIPD / 4] = static_cast<uint16_t>(reg[REG_MCIPD / 4] & ~merged);
			reg[idx] = 0;
			break;
		case REG_INTCLR:
			reg[idx] = static_cast<uint16_t>(merged);
			if (merged & 1) {
				clear_request();
				check_irq();
			}
			break;
		default:
			reg[idx] = static_cast<uint16_t>(merged);
			break;
		}
	}

	uint32_t reg_read(uint32_t offset, uint32_t size)
	{
		uint32_t idx = (offset & 0x7FFC) / 4;
		uint32_t aligned = offset & 0x7FFC;
		uint32_t v;
		if (aligned == REG_INTREQ) {
			v = irql;
		} else if (aligned == REG_SCIPD || aligned == REG_SCIEB || aligned == REG_MCIPD || aligned == REG_MCIEB
			|| aligned == REG_TIMA || aligned == REG_TIMB || aligned == REG_TIMC || aligned == REG_ARMRST) {
			v = reg[idx];
		} else {
			v = reads_zero ? 0 : reg[idx];
		}
		uint32_t shift = (offset & 3) * 8;
		uint32_t out = size == 4 ? v : ((v >> shift) & (size == 2 ? 0xFFFF : 0xFF));
		log.push_back({ instr, cpu->pc(), 'R', offset & 0x7FFF, size, out });
		return out;
	}

	void note_ram_write(uint32_t addr, uint32_t size, uint32_t v)
	{
		if (addr < 0x100 || (addr >= 0x400 && addr < 0x500)) {
			ram_log.push_back({ instr, cpu->pc(), 'W', addr, size, v });
		}
	}

	uint8_t read8(uint32_t addr) override
	{
		if (is_reg(addr)) {
			return static_cast<uint8_t>(reg_read(addr - 0x00800000, 1));
		}
		return ram[addr & 0x7FFFFF];
	}
	uint16_t read16(uint32_t addr) override
	{
		if (is_reg(addr)) {
			return static_cast<uint16_t>(reg_read(addr - 0x00800000, 2));
		}
		addr &= 0x7FFFFE;
		return static_cast<uint16_t>(ram[addr] | (ram[addr + 1] << 8));
	}
	uint32_t read32(uint32_t addr) override
	{
		if (is_reg(addr)) {
			return reg_read(addr - 0x00800000, 4);
		}
		return ram32(addr);
	}
	void write8(uint32_t addr, uint8_t v) override
	{
		if (is_reg(addr)) {
			reg_write(addr - 0x00800000, 1, v, true);
			return;
		}
		addr &= 0x7FFFFF;
		note_ram_write(addr, 1, v);
		ram[addr] = v;
	}
	void write16(uint32_t addr, uint16_t v) override
	{
		if (is_reg(addr)) {
			reg_write(addr - 0x00800000, 2, v, true);
			return;
		}
		addr &= 0x7FFFFE;
		note_ram_write(addr, 2, v);
		ram[addr] = static_cast<uint8_t>(v);
		ram[addr + 1] = static_cast<uint8_t>(v >> 8);
	}
	void write32(uint32_t addr, uint32_t v) override
	{
		if (is_reg(addr)) {
			reg_write(addr - 0x00800000, 4, v, true);
			return;
		}
		addr &= 0x7FFFFC;
		note_ram_write(addr, 4, v);
		set_ram32(addr, v);
	}
};

const char *reg_name(uint32_t offset)
{
	if (offset < 0x2000) {
		return "slot";
	}
	switch (offset & 0x7FFC) {
	case REG_MVOL: return "MVOL/MEM8MB/DAC18B";
	case 0x2804: return "RBL/RBP";
	case 0x2808: return "MIBUF/MOFUL/MIEMP";
	case 0x280C: return "MOBUF/MSLC/AFSEL";
	case 0x2880: return "DMEA";
	case 0x2884: return "DMEA/DRGA";
	case 0x2888: return "DLG/DGATE/DDIR/DEXE";
	case REG_TIMA: return "TIMA";
	case REG_TIMB: return "TIMB";
	case REG_TIMC: return "TIMC";
	case REG_SCIEB: return "SCIEB";
	case REG_SCIPD: return "SCIPD";
	case REG_SCIRE: return "SCIRE";
	case REG_SCILV0: return "SCILV0";
	case REG_SCILV1: return "SCILV1";
	case REG_SCILV2: return "SCILV2";
	case REG_MCIEB: return "MCIEB";
	case REG_MCIPD: return "MCIPD";
	case REG_MCIRE: return "MCIRE";
	case REG_ARMRST: return "ARMRST";
	case REG_INTREQ: return "INTREQ";
	case REG_INTCLR: return "INTCLR";
	default: return "";
	}
}

bool load_image(const char *path, std::vector<uint8_t> &ram)
{
	FILE *fp = std::fopen(path, "rb");
	if (fp == nullptr) {
		std::fprintf(stderr, "cannot open %s\n", path);
		return false;
	}
	size_t n = std::fread(ram.data(), 1, ram.size(), fp);
	std::fclose(fp);
	std::printf("%s: %zu bytes\n", path, n);
	return n > 0;
}

/* Prints the log with runs of identical consecutive accesses folded. */
void print_accesses(const std::vector<Access> &log, size_t limit, bool with_names)
{
	size_t i = 0;
	size_t printed = 0;
	while (i < log.size() && printed < limit) {
		size_t j = i + 1;
		while (j < log.size() && log[j].rw == log[i].rw && log[j].offset == log[i].offset && log[j].size == log[i].size
			&& log[j].value == log[i].value && log[j].pc == log[i].pc) {
			j++;
		}
		const Access &a = log[i];
		std::printf("  %9llu  pc %08x  %c  %04x  %u  %0*x", static_cast<unsigned long long>(a.instr), a.pc, a.rw, a.offset,
			a.size, static_cast<int>(a.size * 2), a.value);
		if (j - i > 1) {
			std::printf("  x%zu", j - i);
		}
		if (with_names) {
			std::printf("  %s", reg_name(a.offset));
		}
		std::printf("\n");
		printed++;
		i = j;
	}
	if (i < log.size()) {
		std::printf("  ... %zu more\n", log.size() - i);
	}
}

} /* namespace */

int main(int argc, char **argv)
{
	const char *image = nullptr;
	const char *trace_out = nullptr;
	uint64_t instructions = 3000000;
	bool bank_load = false;
	bool timers_on = true;
	bool reads_zero = false;
	size_t limit = 400;
	for (int i = 1; i < argc; i++) {
		if (std::strcmp(argv[i], "--image") == 0 && i + 1 < argc) {
			image = argv[++i];
		} else if (std::strcmp(argv[i], "--instructions") == 0 && i + 1 < argc) {
			instructions = std::strtoull(argv[++i], nullptr, 0);
		} else if (std::strcmp(argv[i], "--trace-out") == 0 && i + 1 < argc) {
			trace_out = argv[++i];
		} else if (std::strcmp(argv[i], "--limit") == 0 && i + 1 < argc) {
			limit = std::strtoul(argv[++i], nullptr, 0);
		} else if (std::strcmp(argv[i], "--bank-load") == 0) {
			bank_load = true;
		} else if (std::strcmp(argv[i], "--no-timers") == 0) {
			timers_on = false;
		} else if (std::strcmp(argv[i], "--reads-zero") == 0) {
			reads_zero = true;
		} else {
			std::fprintf(stderr, "unknown argument %s\n", argv[i]);
			return 2;
		}
	}
	if (image == nullptr) {
		std::fprintf(stderr, "usage: %s --image driver.bin [--instructions N] [--bank-load] [--trace-out file] [--no-timers] [--reads-zero] [--limit N]\n", argv[0]);
		return 2;
	}

	StubAica aica;
	aica.timers_on = timers_on;
	aica.reads_zero = reads_zero;
	if (!load_image(image, aica.ram)) {
		return 2;
	}
	odin::Arm7 cpu(aica);
	aica.cpu = &cpu;

	/* The SH-4 side, as snd_upload_driver_reset_arm does it: MEM8MB,
	 * ARM held in reset, image at 0 (loaded above), RAM[0x50] = 0x200,
	 * eight words of -1 at 0x60-0x7C, then the reset is released. */
	aica.reg_write(REG_MVOL, 2, 0x0200, false);
	aica.reg_write(REG_ARMRST, 2, 0x0001, false);
	aica.set_ram32(0x50, 0x00000200);
	for (uint32_t a = 0x60; a <= 0x7C; a += 4) {
		aica.set_ram32(a, 0xFFFFFFFF);
	}
	aica.reg_write(REG_ARMRST, 2, 0x0000, false);
	cpu.reset();
	std::printf("reset: pc %08x cpsr %08x, RAM[0x5c] = %08x (sign of life, static in the image), RAM[0x88] = %08x\n",
		cpu.pc(), cpu.cpsr(), aica.ram32(0x5C), aica.ram32(0x88));

	FILE *out = nullptr;
	if (trace_out != nullptr) {
		out = std::fopen(trace_out, "w");
		cpu.set_trace(out);
	}

	/* SH-4 bank load protocol (snd_load_bank): wait for RAM[0x88], push
	 * a command word where the ring entry is 0, wait for RAM[0xB4] to
	 * rise, raise RAM[0xB0], wait for RAM[0xB4] to fall, drop RAM[0xB0]. */
	uint32_t sh4_phase = 0;
	uint32_t ring_wp = 0x400;
	uint64_t sh4_wait_since = 0;
	uint64_t free_seen_at = 0;
	uint32_t free_bytes = 0;
	bool answered = false;
	uint64_t cycles = 0;
	uint32_t last_poll_offset = 0xFFFFFFFF;
	uint32_t last_poll_pc = 0;
	uint64_t poll_run = 0;
	bool poll_reported = false;
	size_t log_mark = 0;

	for (uint64_t i = 0; i < instructions; i++) {
		aica.instr = i;
		uint32_t c = cpu.step();
		cycles += c;
		aica.tick(c);

		/* Polling detector: the same register read over and over with
		 * no write in between. */
		for (size_t k = log_mark; k < aica.log.size(); k++) {
			const Access &a = aica.log[k];
			if (a.rw == 'R' && a.offset == last_poll_offset && a.pc == last_poll_pc) {
				poll_run++;
			} else {
				last_poll_offset = a.rw == 'R' ? a.offset : 0xFFFFFFFF;
				last_poll_pc = a.pc;
				poll_run = 1;
				poll_reported = false;
			}
			if (poll_run == 50 && !poll_reported) {
				poll_reported = true;
				std::printf("instr %llu: the driver polls register %04x (%s) from pc %08x, value %08x, SCIPD %04x SCIEB %04x TIMB %04x\n",
					static_cast<unsigned long long>(i), a.offset, reg_name(a.offset), a.pc, a.value,
					aica.reg[REG_SCIPD / 4], aica.reg[REG_SCIEB / 4], aica.reg[REG_TIMB / 4]);
			}
		}
		log_mark = aica.log.size();

		if ((i & 63) != 0) {
			continue;
		}
		if (free_seen_at == 0 && aica.ram32(0x88) != 0) {
			free_seen_at = i;
			free_bytes = aica.ram32(0x88);
			std::printf("instr %llu: RAM[0x88] = %08x (%u free bytes)\n", static_cast<unsigned long long>(i), free_bytes, free_bytes);
		}
		if (!bank_load) {
			continue;
		}
		switch (sh4_phase) {
		case 0:
			if (free_seen_at != 0 && i > free_seen_at + 20000) {
				if (aica.ram32(ring_wp) == 0) {
					aica.set_ram32(ring_wp, 0x18A0);
					std::printf("instr %llu: SH-4 pushes command 0x18a0 at ring %03x\n", static_cast<unsigned long long>(i), ring_wp);
					ring_wp = ring_wp + 4 >= 0x500 ? 0x400 : ring_wp + 4;
					sh4_phase = 1;
					sh4_wait_since = i;
				}
			}
			break;
		case 1:
			if (aica.ram32(0xB4) != 0) {
				std::printf("instr %llu: RAM[0xb4] = %08x (driver accepted the command), ring entry now %08x; SH-4 sets RAM[0xb0] = 1\n",
					static_cast<unsigned long long>(i), aica.ram32(0xB4), aica.ram32(ring_wp - 4 < 0x400 ? 0x4FC : ring_wp - 4));
				aica.set_ram32(0xB0, 1);
				sh4_phase = 2;
				sh4_wait_since = i;
			} else if (i - sh4_wait_since > 2000000) {
				std::printf("instr %llu: no answer in RAM[0xb4] after the command\n", static_cast<unsigned long long>(i));
				sh4_phase = 9;
			}
			break;
		case 2:
			if (aica.ram32(0xB4) == 0) {
				std::printf("instr %llu: RAM[0xb4] = 0 (driver done); SH-4 sets RAM[0xb0] = 0\n", static_cast<unsigned long long>(i));
				aica.set_ram32(0xB0, 0);
				answered = true;
				sh4_phase = 3;
			} else if (i - sh4_wait_since > 2000000) {
				std::printf("instr %llu: RAM[0xb4] never returned to 0\n", static_cast<unsigned long long>(i));
				sh4_phase = 9;
			}
			break;
		default:
			break;
		}
	}
	if (out != nullptr) {
		cpu.set_trace(nullptr);
		std::fclose(out);
	}

	std::printf("\nran %llu instructions, %llu cycles, pc %08x cpsr %08x, %llu timer overflows, %llu FIQ requests, %llu MCIPD writes\n",
		static_cast<unsigned long long>(cpu.instructions()), static_cast<unsigned long long>(cycles), cpu.pc(), cpu.cpsr(),
		static_cast<unsigned long long>(aica.timer_events), static_cast<unsigned long long>(aica.fiq_count),
		static_cast<unsigned long long>(aica.mcipd_writes));
	std::printf("\nAICA register accesses in order (instruction, pc, r/w, offset, size, value):\n");
	print_accesses(aica.log, limit, true);
	std::printf("\nRAM writes to 0x000-0x0ff and 0x400-0x4ff in order:\n");
	print_accesses(aica.ram_log, limit, false);
	std::printf("\nRAM 0x40-0xff at the end:\n");
	for (uint32_t a = 0x40; a < 0x100; a += 16) {
		std::printf("  %03x: %08x %08x %08x %08x\n", a, aica.ram32(a), aica.ram32(a + 4), aica.ram32(a + 8), aica.ram32(a + 12));
	}
	std::printf("ring 0x400-0x41c: %08x %08x %08x %08x %08x %08x %08x %08x\n", aica.ram32(0x400), aica.ram32(0x404), aica.ram32(0x408),
		aica.ram32(0x40C), aica.ram32(0x410), aica.ram32(0x414), aica.ram32(0x418), aica.ram32(0x41C));
	bool ok = free_seen_at != 0 && (!bank_load || answered);
	std::printf("%s\n", ok ? "OK" : "FAILED");
	return ok ? 0 : 1;
}
