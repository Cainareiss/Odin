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

/* Runs the SingleStepTests 680x0 suite (github.com/SingleStepTests/680x0,
 * 68000/v1) on the Odin 68000: one instruction per test, then the
 * registers, the prefetch queue, the memory, the clocks and the order of
 * the bus cycles are compared. Tests that take an address error are
 * counted apart.
 *
 * Build: g++ -std=c++20 -O2 -Wall -Wextra -Wpedantic -Wconversion -Wshadow -I src \
 *            tests/m68000/m68000_tests.cpp src/odin/m68000.cpp src/odin/m68000_ops.cpp \
 *            -ljansson -o m68000_tests
 * Use:   m68000_tests [-v N] file.json.gz...   (N: failures shown per file)
 * Exit status is 0 when every test passes. */

#include "odin/m68000.hpp"

#include <jansson.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <unordered_map>
#include <vector>

namespace {

struct Cycle {
	char kind;		/* r, w, n */
	unsigned clocks;
	unsigned fc;
	uint32_t addr;
	char size;		/* b, w */
	uint32_t value;

	bool operator==(const Cycle &o) const
	{
		if (kind != o.kind || clocks != o.clocks)
			return false;
		if (kind == 'n')
			return true;
		return fc == o.fc && addr == o.addr && size == o.size && value == o.value;
	}
};

class TestBus : public odin::M68000Bus {
public:
	std::unordered_map<uint32_t, uint8_t> ram;
	std::vector<Cycle> cycles;
	const odin::M68000 *cpu = nullptr;
	uint64_t last = 0;

	uint8_t peek(uint32_t a) const
	{
		auto it = ram.find(a & 0xFFFFFF);
		return it == ram.end() ? 0 : it->second;
	}
	void note(char kind, unsigned fc, uint32_t addr, char size, uint32_t value)
	{
		const uint64_t end = cpu->cycles();
		const uint64_t start = end - 4;
		if (start > last)
			cycles.push_back({'n', static_cast<unsigned>(start - last), 0, 0, 0, 0});
		cycles.push_back({kind, 4, fc, addr, size, value});
		last = end;
	}
	uint16_t read16(uint32_t addr, unsigned fc) override
	{
		const uint16_t v = static_cast<uint16_t>((peek(addr) << 8) | peek(addr + 1));
		note('r', fc, addr, 'w', v);
		return v;
	}
	uint8_t read8(uint32_t addr, unsigned fc) override
	{
		const uint8_t v = peek(addr);
		note('r', fc, addr, 'b', v);
		return v;
	}
	void write16(uint32_t addr, uint16_t value, unsigned fc) override
	{
		ram[addr & 0xFFFFFF] = static_cast<uint8_t>(value >> 8);
		ram[(addr + 1) & 0xFFFFFF] = static_cast<uint8_t>(value);
		note('w', fc, addr, 'w', value);
	}
	void write8(uint32_t addr, uint8_t value, unsigned fc) override
	{
		ram[addr & 0xFFFFFF] = value;
		note('w', fc, addr, 'b', value);
	}
};

uint32_t num(json_t *o, const char *key)
{
	return static_cast<uint32_t>(json_integer_value(json_object_get(o, key)));
}

/* The expected cycles: idle runs merged, and the read-modify-write cycle
 * of TAS as a read, 2 idle clocks and a write. */
std::vector<Cycle> expected_cycles(json_t *list, const std::unordered_map<uint32_t, uint8_t> &before)
{
	std::vector<Cycle> out;
	size_t i;
	json_t *t;

	json_array_foreach(list, i, t) {
		const char *k = json_string_value(json_array_get(t, 0));
		const unsigned clocks = static_cast<unsigned>(json_integer_value(json_array_get(t, 1)));
		if (k[0] == 'n') {
			if (!out.empty() && out.back().kind == 'n')
				out.back().clocks += clocks;
			else
				out.push_back({'n', clocks, 0, 0, 0, 0});
			continue;
		}
		const unsigned fc = static_cast<unsigned>(json_integer_value(json_array_get(t, 2)));
		const uint32_t addr = static_cast<uint32_t>(json_integer_value(json_array_get(t, 3)));
		const char size = json_string_value(json_array_get(t, 4))[1];
		const uint32_t value = static_cast<uint32_t>(json_integer_value(json_array_get(t, 5)));
		if (k[0] == 't') {
			auto it = before.find(addr);
			out.push_back({'r', 4, fc, addr, 'b', it == before.end() ? 0u : it->second});
			out.push_back({'n', clocks - 8, 0, 0, 0, 0});
			out.push_back({'w', 4, fc, addr, 'b', value});
			continue;
		}
		out.push_back({k[0], clocks, fc, addr, size, value});
	}
	return out;
}

std::string show(const std::vector<Cycle> &v)
{
	std::string s;
	char buf[64];
	for (const Cycle &c : v) {
		if (c.kind == 'n')
			std::snprintf(buf, sizeof(buf), " n%u", c.clocks);
		else
			std::snprintf(buf, sizeof(buf), " %c%c%u:%06X=%X", c.kind, c.size, c.fc, c.addr, c.value);
		s += buf;
	}
	return s;
}

struct Totals {
	unsigned pass = 0, fail = 0, aerr_pass = 0, aerr_fail = 0;
};

/* One test: true when everything matches. `why` gets the differences. */
bool run_one(json_t *test, std::string &why, bool &aerr)
{
	json_t *in = json_object_get(test, "initial"), *fin = json_object_get(test, "final");
	TestBus bus;
	odin::M68000 cpu(bus);
	odin::M68000State st{};
	size_t i;
	json_t *pair;

	json_array_foreach(json_object_get(in, "ram"), i, pair)
		bus.ram[static_cast<uint32_t>(json_integer_value(json_array_get(pair, 0)))] =
			static_cast<uint8_t>(json_integer_value(json_array_get(pair, 1)));
	const auto before = bus.ram;
	char key[8];
	for (unsigned n = 0; n < 8; n++) {
		std::snprintf(key, sizeof(key), "d%u", n);
		st.d[n] = num(in, key);
		if (n < 7) {
			std::snprintf(key, sizeof(key), "a%u", n);
			st.a[n] = num(in, key);
		}
	}
	st.sr = static_cast<uint16_t>(num(in, "sr"));
	const bool s = st.sr & 0x2000;
	st.a[7] = s ? num(in, "ssp") : num(in, "usp");
	st.other_sp = s ? num(in, "usp") : num(in, "ssp");
	st.pc = num(in, "pc");
	st.ir = static_cast<uint16_t>(json_integer_value(json_array_get(json_object_get(in, "prefetch"), 0)));
	st.irc = static_cast<uint16_t>(json_integer_value(json_array_get(json_object_get(in, "prefetch"), 1)));
	cpu.load(st);
	bus.cpu = &cpu;
	bus.last = cpu.cycles();

	const uint32_t clocks = cpu.step();
	if (cpu.cycles() > bus.last)
		bus.cycles.push_back({'n', static_cast<unsigned>(cpu.cycles() - bus.last), 0, 0, 0, 0});

	odin::M68000State out;
	cpu.save(out);
	char buf[160];
	auto check = [&](const char *name, uint32_t got, uint32_t want) {
		if (got != want) {
			std::snprintf(buf, sizeof(buf), " %s=%08X(want %08X)", name, got, want);
			why += buf;
		}
	};
	for (unsigned n = 0; n < 8; n++) {
		std::snprintf(key, sizeof(key), "d%u", n);
		check(key, out.d[n], num(fin, key));
		if (n < 7) {
			std::snprintf(key, sizeof(key), "a%u", n);
			check(key, out.a[n], num(fin, key));
		}
	}
	const bool fs = out.sr & 0x2000;
	check("sr", out.sr, num(fin, "sr"));
	check("usp", fs ? out.other_sp : out.a[7], num(fin, "usp"));
	check("ssp", fs ? out.a[7] : out.other_sp, num(fin, "ssp"));
	check("pc", out.pc, num(fin, "pc"));
	check("ir", out.ir, static_cast<uint32_t>(json_integer_value(json_array_get(json_object_get(fin, "prefetch"), 0))));
	check("irc", out.irc, static_cast<uint32_t>(json_integer_value(json_array_get(json_object_get(fin, "prefetch"), 1))));
	check("clocks", clocks, num(test, "length"));
	json_array_foreach(json_object_get(fin, "ram"), i, pair) {
		const uint32_t a = static_cast<uint32_t>(json_integer_value(json_array_get(pair, 0)));
		const uint32_t want = static_cast<uint32_t>(json_integer_value(json_array_get(pair, 1)));
		const uint32_t got = bus.peek(a);
		if (got != want) {
			std::snprintf(buf, sizeof(buf), " ram[%06X]=%02X(want %02X)", a, got, want);
			why += buf;
		}
	}
	const std::vector<Cycle> want = expected_cycles(json_object_get(test, "transactions"), before);
	aerr = false;
	for (const Cycle &c : want)
		if (c.kind == 'r' && c.fc == 5 && (c.addr == 0x0C || c.addr == 0x0E))
			aerr = true;
	if (want != bus.cycles)
		why += "\n      bus  want" + show(want) + "\n            got " + show(bus.cycles);
	return why.empty();
}

int run_file(const char *path, unsigned verbose, Totals &all)
{
	std::string cmd = std::string("gzip -dc '") + path + "'";
	FILE *p = popen(cmd.c_str(), "r");
	if (!p) {
		std::fprintf(stderr, "%s: cannot read\n", path);
		return 1;
	}
	std::string text;
	char chunk[65536];
	size_t n;
	while ((n = std::fread(chunk, 1, sizeof(chunk), p)) > 0)
		text.append(chunk, n);
	pclose(p);
	json_error_t err;
	json_t *root = json_loadb(text.data(), text.size(), 0, &err);
	if (!root) {
		std::fprintf(stderr, "%s: %s\n", path, err.text);
		return 1;
	}
	Totals t;
	unsigned shown = 0;
	size_t i;
	json_t *test;
	json_array_foreach(root, i, test) {
		std::string why;
		bool aerr;
		const bool ok = run_one(test, why, aerr);
		if (aerr)
			(ok ? t.aerr_pass : t.aerr_fail)++;
		else
			(ok ? t.pass : t.fail)++;
		if (!ok && shown < verbose) {
			shown++;
			std::printf("  FAIL %s%s:%s\n", json_string_value(json_object_get(test, "name")), aerr ? " [address error]" : "",
			            why.c_str());
		}
	}
	json_decref(root);
	const char *base = std::strrchr(path, '/');
	std::printf("%-22s %5u/%-5u  address errors %4u/%-4u\n", base ? base + 1 : path, t.pass, t.pass + t.fail, t.aerr_pass,
	            t.aerr_pass + t.aerr_fail);
	all.pass += t.pass;
	all.fail += t.fail;
	all.aerr_pass += t.aerr_pass;
	all.aerr_fail += t.aerr_fail;
	return 0;
}

} /* namespace */

int main(int argc, char **argv)
{
	unsigned verbose = 0;
	int first = 1;

	if (argc > 2 && std::strcmp(argv[1], "-v") == 0) {
		verbose = static_cast<unsigned>(std::atoi(argv[2]));
		first = 3;
	}
	if (first >= argc) {
		std::fprintf(stderr, "use: %s [-v N] file.json.gz...\n", argv[0]);
		return 2;
	}
	Totals all;
	for (int i = first; i < argc; i++)
		run_file(argv[i], verbose, all);
	std::printf("all: %u/%u passed, address errors %u/%u\n", all.pass, all.pass + all.fail, all.aerr_pass,
	            all.aerr_pass + all.aerr_fail);
	return all.fail || all.aerr_fail ? 1 : 0;
}
