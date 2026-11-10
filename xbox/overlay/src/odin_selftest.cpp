/*
 * The selftests of odin_core.dll (M1), run through odin_selftest(): "jit",
 * "board", "describe", "bios". None needs a game: the board is a synthetic
 * Hikaru whose boot ROM is our own SH-4 stub (not a BIOS), the MIE ROM is
 * zeros, and the describe test reads the game list embedded in this DLL.
 * GPL-3.0-or-later.
 *
 * SH-4 words are little-endian: BRA . is FE AF (0xAFFE), NOP is 09 00 (0x0009).
 */

#include "odin_c.h"

#include "odin/common/io.hpp"
#include "odin/common/stop_message.hpp"
#include "odin/host/app/games.hpp"
#include "odin/host/app/rom_files.hpp"
#include "odin/host/app/settings.hpp"
#include "odin/host/app/user_files.hpp"
#include "odin/host/input/control_panel.hpp"
#include "odin/machine/boards/hikaru/hikaru.hpp"
#include "odin/machine/chips/mie.hpp"
#include "odin/machine/core/host_code.hpp"
#include "odin/machine/cpu/sh4/sh4.hpp"

#include "odin_session.hpp"

#include <windows.h>
#include <io.h>
#include <xmmintrin.h>

#include <chrono>
#include <cstdio>
#include <cstring>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

extern "C" const char odin_games_json[];
extern "C" const char odin_games_json_end[];

namespace odinc {

namespace {

/* The lines of one selftest and how many checks failed */
class Report {
public:
	void check(const char *name, bool ok, const std::string &detail)
	{
		m_text += std::string(ok ? "PASS " : "FAIL ") + name + (detail.empty() ? "" : ": " + detail) + "\n";
		if (!ok)
			m_failed++;
	}
	void note(const std::string &text)
	{
		m_text += text + "\n";
	}
	int failed() const { return m_failed; }
	const std::string &text() const { return m_text; }

private:
	std::string m_text;
	int m_failed = 0;
};

/* Physical memory of a test CPU: 64 KB, all of it mapped to the CPU */
class TestBus final : public odin::Sh4Bus {
public:
	TestBus() : m_mem(64 * 1024, 0) {}
	bool read(uint32_t addr, uint32_t size, uint64_t &value) override
	{
		value = 0;
		if (addr + size > m_mem.size())
			return false;
		for (uint32_t i = 0; i < size; i++)
			value |= static_cast<uint64_t>(m_mem[addr + i]) << (8 * i);
		return true;
	}
	bool write(uint32_t addr, uint32_t size, uint64_t value) override
	{
		if (addr + size > m_mem.size())
			return false;
		for (uint32_t i = 0; i < size; i++)
			m_mem[addr + i] = static_cast<uint8_t>(value >> (8 * i));
		return true;
	}
	uint16_t port_a_read() override { return 0; }
	void port_a_write(uint16_t) override {}
	void unanswered(bool, uint32_t, uint32_t, uint64_t) override {}
	void hook() override {}
	std::vector<uint8_t> &memory() { return m_mem; }

private:
	std::vector<uint8_t> m_mem;
};

/* The guest program of T-JIT (physical 0, the reset vector P2 0xA0000000):
 *   0x00 MOV #0,R1      00 E1
 *   0x02 ADD #1,R1      01 71
 *   0x04 ADD #3,R2      03 72
 *   0x06 CMP/GT R2,R1   27 31
 *   0x08 ADDC R1,R3     1E 33
 *   0x0A BRA 0x02       FA AF
 *   0x0C NOP            09 00 */
const uint8_t JIT_PROGRAM[] = { 0x00, 0xE1, 0x01, 0x71, 0x03, 0x72, 0x27, 0x31, 0x1E, 0x33, 0xFA, 0xAF, 0x09, 0x00 };

/* The boot ROM of the synthetic board: BRA . and a NOP (the CPU spins) */
std::vector<uint8_t>
spin_rom()
{
	std::vector<uint8_t> rom(2u * 1024u * 1024u, 0);

	rom[0] = 0xFE;
	rom[1] = 0xAF;
	rom[2] = 0x09;
	rom[3] = 0x00;
	return rom;
}

/* A boot ROM that sets FPSCR to SZ and PR both set: an unimplemented state
 * that stops the machine (Sh4::fatal, sh4.cpp:287). MOV.L @(2,PC),R0 ; LDS
 * R0,FPSCR ; BRA . ; NOP ; the constant 0x00180000 at 0x0C. */
std::vector<uint8_t>
stop_rom()
{
	std::vector<uint8_t> rom(2u * 1024u * 1024u, 0);
	const uint8_t words[] = { 0x02, 0xD0, 0x6A, 0x40, 0xFE, 0xAF, 0x09, 0x00, 0x00, 0x00, 0x00, 0x00,
		                      0x00, 0x00, 0x18, 0x00 };

	std::memcpy(rom.data(), words, sizeof(words));
	return rom;
}

/* A Hikaru game in memory: the boot ROM and the MIE ROM (zeros), no other
 * section; the boards of a Hikaru set are not needed for the spinning CPU */
odin::Game
synthetic(const char *name, std::vector<uint8_t> bootrom)
{
	odin::Game game;

	game.name = name;
	game.title = name;
	game.machine = "hikaru";
	game.io_board = "837-13551";
	game.sections.push_back({ "bootrom", std::move(bootrom) });
	game.sections.push_back({ "mie", std::vector<uint8_t>(odin::Mie::ROM_SIZE, 0) });
	return game;
}

/* The CPU state the two modes must agree on */
bool
same_state(const odin::Sh4State &a, const odin::Sh4State &b, std::string &where)
{
	for (int i = 0; i < 16; i++)
		if (a.r[i] != b.r[i]) {
			where = "r" + std::to_string(i);
			return false;
		}
	if (a.pc != b.pc) { where = "pc"; return false; }
	if (a.sr != b.sr) { where = "sr"; return false; }
	if (a.pr != b.pr) { where = "pr"; return false; }
	if (a.gbr != b.gbr) { where = "gbr"; return false; }
	if (a.mach != b.mach) { where = "mach"; return false; }
	if (a.macl != b.macl) { where = "macl"; return false; }
	return true;
}

/* The stop test: the stop hook records the text and parks the thread */
std::string g_stop_text;
volatile LONG g_stop_seen = 0;

void
stop_record(const char *what)
{
	g_stop_text = what ? what : "";
	InterlockedExchange(&g_stop_seen, 1);
}

void
stop_park()
{
	for (;;)
		Sleep(INFINITE);
}

DWORD WINAPI
stop_run(LPVOID board)
{
	static_cast<odin::Hikaru *>(board)->run_frame();
	return 0;
}

void
test_jit(Report &report)
{
	/* the MXCSR of this thread: upstream's float rules need the reset value */
	const unsigned mxcsr = _mm_getcsr();

	char mxcsr_text[32];
	std::snprintf(mxcsr_text, sizeof(mxcsr_text), "MXCSR %08X", mxcsr);
	report.check("mxcsr", mxcsr == 0x1F80, mxcsr_text);

	TestBus interp_bus, jit_bus;
	std::vector<uint8_t> interp_onchip(64 * 1024, 0), jit_onchip(64 * 1024, 0);
	odin::Sh4 interp(interp_bus, interp_onchip, true, true, 5);
	odin::Sh4 jit(jit_bus, jit_onchip, true, true, 5);

	/* each CPU maps its own bus memory page by page: the JIT reads the code
	 * from those pages, as the boards map their ROM and RAM */
	std::memcpy(interp_bus.memory().data(), JIT_PROGRAM, sizeof(JIT_PROGRAM));
	std::memcpy(jit_bus.memory().data(), JIT_PROGRAM, sizeof(JIT_PROGRAM));
	interp.set_translate(false);
	jit.set_translate(true);
	interp.reset(true);
	jit.reset(true);
	for (uint32_t page = 0; page < 16; page++) {
		uint8_t *const at = interp_bus.memory().data() + page * 4096;
		uint8_t *const jat = jit_bus.memory().data() + page * 4096;

		interp.map_page(page, at, at);
		jit.map_page(page, jat, jat);
	}

	report.check("interpreter", !interp.translates(), "translates() false: the interpreter runs");
	report.check("jit translates", jit.translates(), "translates() " + std::string(jit.translates() ? "true" : "false"));

	/* the same cycles in both: 40 slices of 5000 */
	LARGE_INTEGER freq{}, t0{}, t1{};
	QueryPerformanceFrequency(&freq);
	QueryPerformanceCounter(&t0);
	for (int i = 0; i < 40; i++)
		interp.run(5000);
	QueryPerformanceCounter(&t1);
	const double interp_seconds = static_cast<double>(t1.QuadPart - t0.QuadPart) / static_cast<double>(freq.QuadPart);

	QueryPerformanceCounter(&t0);
	for (int i = 0; i < 40; i++)
		jit.run(5000);
	QueryPerformanceCounter(&t1);
	const double jit_seconds = static_cast<double>(t1.QuadPart - t0.QuadPart) / static_cast<double>(freq.QuadPart);

	odin::Sh4State a{}, b{};
	interp.save(a);
	jit.save(b);
	std::string where;
	const bool same = same_state(a, b, where);

	report.check("interpreter and jit agree", same, same ? "registers, pc, sr, pr, gbr, mach, macl equal" : "differs at " + where);
	report.check("cycles agree", a.over == b.over && interp.time() == jit.time(),
	             "interpreter " + std::to_string(static_cast<unsigned long long>(interp.time())) + ", jit " +
	                 std::to_string(static_cast<unsigned long long>(jit.time())) + " cycles");
	report.note("  jit: host_bytes " + std::to_string(jit.host_bytes()) + ", far_calls " +
	            std::to_string(static_cast<unsigned long long>(jit.host_far_calls())));
	report.note("  speed (PC only, not representative): interpreter " +
	            std::to_string(static_cast<long long>(interp.time() / (interp_seconds > 0 ? interp_seconds : 1e-9))) +
	            " cycles/s, jit " +
	            std::to_string(static_cast<long long>(jit.time() / (jit_seconds > 0 ? jit_seconds : 1e-9))) + " cycles/s");
}

void
test_board(Report &report)
{
	/* the spinning board: 600 frames, its sound each frame, a state round
	 * trip, and then a stop that must park its thread */
	NullScreen screen;
	PadPanel panel;
	odin::Game game = synthetic("selftest-board", spin_rom());
	std::unique_ptr<odin::Hikaru> board = odin::Hikaru::make(game, false, odin::NetLinkSettings{});

	report.check("board made", board != nullptr, "a synthetic Hikaru with a 2 MB boot ROM");
	if (!board)
		return;
	board->set_screen(&screen);
	board->set_panel(&panel);
	board->keep_in(odin::SaveFolder());
	board->reset();

	size_t pairs = 0;
	bool sound_each = true;
	for (int frame = 0; frame < 600; frame++) {
		board->run_frame();
		const size_t n = board->sound(false).size() / 2;

		pairs += n;
		sound_each = sound_each && n > 0;
	}
	const double per_frame = pairs / 600.0;
	report.check("600 frames", board->frame() == 600 && sound_each,
	             "frame " + std::to_string(board->frame()) + ", " + std::to_string(per_frame) +
	                 " sound pairs a frame (~734 expected at 60 Hz)");

	/* a state at frame 600, then frame 601 of each: the same sound */
	char temp[MAX_PATH] = "", path[MAX_PATH] = "";
	GetTempPathA(sizeof(temp), temp);
	std::snprintf(path, sizeof(path), "%sodin-selftest.odinstate", temp);
	const bool saved = board->save_state(path);
	board->run_frame();
	const std::vector<int16_t> frame601 = board->sound(false);

	auto restored = odin::Hikaru::make(game, false, odin::NetLinkSettings{});
	restored->set_screen(&screen);
	restored->set_panel(&panel);
	restored->keep_in(odin::SaveFolder());
	restored->reset();
	const bool loaded = saved && restored->load_state(path);
	restored->run_frame();
	const bool same = loaded && restored->sound(false) == frame601 && restored->frame() == 601;

	report.check("state round trip", same,
	             saved ? (loaded ? "frame 601 sound equal after a load" : "the state could not be read back")
	                   : "the state could not be written");
	std::remove(path);

	/* the stop: an unimplemented FPSCR state stops the CPU. The stop hook of
	 * the test records it and parks the thread; the thread must not return,
	 * and the process must go on */
	odin::Game bad = synthetic("selftest-stop", stop_rom());
	std::unique_ptr<odin::Hikaru> stopper = odin::Hikaru::make(bad, false, odin::NetLinkSettings{});

	if (!stopper) {
		report.check("stop board made", false, "");
		return;
	}
	stopper->set_screen(&screen);
	stopper->set_panel(&panel);
	stopper->keep_in(odin::SaveFolder());
	stopper->reset();

	const auto old_message = odin::stop_message;
	const auto old_exit = odin::stop_exit;

	g_stop_seen = 0;
	odin::stop_message = stop_record;
	odin::stop_exit = stop_park;
	HANDLE thread = CreateThread(nullptr, 16 * 1024 * 1024, stop_run, stopper.get(), 0, nullptr);
	const DWORD wait = thread ? WaitForSingleObject(thread, 4000) : WAIT_FAILED;
	odin::stop_message = old_message;
	odin::stop_exit = old_exit;

	const bool recorded = InterlockedCompareExchange(&g_stop_seen, 0, 0) == 1;
	const bool mentions = g_stop_text.find("FPSCR") != std::string::npos;

	report.check("stop parks the thread", recorded && mentions && wait == WAIT_TIMEOUT,
	             recorded ? ("message: " + g_stop_text + (wait == WAIT_TIMEOUT ? ", thread still parked"
	                                                                        : ", thread returned"))
	                      : "no stop message");
	/* the parked thread is left as it is: the process goes on and it ends
	 * with the process */
	if (thread)
		CloseHandle(thread);
	/* the stopper is kept alive: its thread holds it */
	stopper.release();
}

/* The stderr of the core while body runs, as text */
std::string
capture_stderr(const std::function<void()> &body)
{
	char dir[MAX_PATH] = "", file[MAX_PATH] = "";

	GetTempPathA(sizeof(dir), dir);
	GetTempFileNameA(dir, "odn", 0, file);
	std::fflush(stderr);
	const int saved = _dup(_fileno(stderr));
	std::FILE *const sink = std::fopen(file, "w");

	if (sink && saved >= 0)
		_dup2(_fileno(sink), _fileno(stderr));
	body();
	std::fflush(stderr);
	if (saved >= 0) {
		_dup2(saved, _fileno(stderr));
		_close(saved);
	}
	if (sink)
		std::fclose(sink);
	std::string text;
	if (std::FILE *const back = std::fopen(file, "rb")) {
		char chunk[4096];
		size_t got;

		while ((got = std::fread(chunk, 1, sizeof(chunk), back)) > 0)
			text.append(chunk, got);
		std::fclose(back);
	}
	DeleteFileA(file);
	return text;
}

void
test_describe(Report &report)
{
	const size_t size = static_cast<size_t>(odin_games_json_end - odin_games_json);
	odin::GameList list;

	report.check("game list embedded", list.load_text("odin-games.json", odin_games_json, size),
	             std::to_string(size) + " bytes");
	static const char *const PLAYABLE[] = {
		"airtrix", "airtrixo", "braveff", "pharrier", "sgnascar", "sgnascaro", "swracer", "von4",
		"atvtrack", "atvtracka", "gfootbal", "smashdrv", "smashdrvb", "smashdrvs",
	};
	int described = 0, with_controls = 0, panels = 0;
	std::string errors;

	errors = capture_stderr([&] {
		odin::Settings settings;

		for (const char *name : PLAYABLE) {
			const std::optional<odin::Game> game = list.describe(name);

			if (!game)
				continue;
			described++;
			if (!game->controls.empty())
				with_controls++;
			odin::ControlPanel panel(game->controls, settings, name);
			if (!panel.controls().empty())
				panels++;
		}
	});
	const bool errors_none = errors.find("ERROR") == std::string::npos;

	report.check("14 games described", described == 14, std::to_string(described) + " of 14");
	report.check("each game has controls", with_controls == 14, std::to_string(with_controls) + " of 14");
	report.check("control panels built", panels == 14, std::to_string(panels) + " of 14");
	report.check("zero ERROR lines", errors_none, errors_none ? "" : errors.substr(0, 400));
}

void
test_bios(Report &report)
{
	/* the shared sets are not there: the missing files name hikaru or mie */
	const size_t size = static_cast<size_t>(odin_games_json_end - odin_games_json);
	odin::GameList list;
	char dir[MAX_PATH] = "", empty[MAX_PATH] = "";

	list.load_text("odin-games.json", odin_games_json, size);
	GetTempPathA(sizeof(dir), dir);
	std::snprintf(empty, sizeof(empty), "%sodin-selftest-empty", dir);
	CreateDirectoryA(empty, nullptr);
	odin::RomFiles files(empty, empty);
	const std::vector<odin::RomFiles::File> missing = list.missing(files, "airtrix");
	bool shared = false;

	for (const odin::RomFiles::File &file : missing)
		if (file.set_of("airtrix") == "hikaru" || file.set_of("airtrix") == "mie")
			shared = true;
	report.check("missing shared set named", !missing.empty() && shared,
	             std::to_string(missing.size()) + " files missing, the shared sets among them");

	/* a machine without its boot ROM is refused: no crash, no board */
	odin::Game nobios = synthetic("selftest-no-bios", std::vector<uint8_t>());
	std::string refused = capture_stderr([&] {
		auto board = odin::Hikaru::make(nobios, false, odin::NetLinkSettings{});

		report.check("no boot ROM refused", board == nullptr, board ? "a board was made" : "");
	});
	report.check("refusal says why", refused.find("BOOTROM") != std::string::npos, "");
	RemoveDirectoryA(empty);
}

} /* namespace */

int32_t
selftest_run(const std::string &name, std::string &report)
{
	Report out;

	if (name == "jit")
		test_jit(out);
	else if (name == "board")
		test_board(out);
	else if (name == "describe")
		test_describe(out);
	else if (name == "bios")
		test_bios(out);
	else {
		report = "unknown selftest: '" + name + "' (jit, board, describe, bios)\n";
		return ODIN_E_ARG;
	}
	report = out.text() + "RESULT " + name + (out.failed() ? " FAILED" : " PASSED") + " (" +
	         std::to_string(out.failed()) + " failed)\n";
	return out.failed() ? ODIN_E_FAILED : ODIN_OK;
}

} /* namespace odinc */
