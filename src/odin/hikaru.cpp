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

/* 
 * SEGA Hikaru
 *
 * Specs
 * =====
 *
 * Taken from system16.com
 *
 * CPU			2 x Hitachi SH-4 @ 200 MHz
 * Graphic Engine	Sega Custom 3D 
 * Sound Engine		2 x ARM7 Yamaha AICA @ 45 MHz + ARM7, 64 channel ADPCM
 * Main Memory		64 MB
 * Graphic Memory	28 MB
 * Sound Memory		8 MB [per AICA board]
 * Media		ROM Board (max 352 MB)
 * Colors		24bit
 * Resolution		24 KHz 496x384, 31 KHz 640x480 
 * Shading		Flat, Linear, Phong
 * Lighting		Horizontal, Spot
 *			1024 lights per scene
 *			4 lights per polygon
 *			8 window surfaces
 * Effects		Fog, Depth Queueing, Stencil, Shadow, Motion blur
 * Others Capabilties	Bitmap Layer x 2
 *			Calendar [Note: AICA RTC]
 *			Dual Monitor (24 kHz)
 * Extensions		communication, 4 channel audio, PCI, MIDI, RS-232C 
 * Connection		Jamma Video compliant
 *
 *
 * ICs
 * ===
 *
 * According to the RAM test:
 *
 * 15,16,17S,18S = @0C000000 RAM (32 MB), Master RAM
 * 22,23,24S,25S = @16000000 RAM (32 MB), Slave RAM
 * 38 39S        = @14000000 RAM (4 MB), CMDRAM
 * 41            = @16000000 RAM (4 MB), TEXRAM Bank 0
 * 42            = @16000000 RAM (4 MB), TEXRAM Bank 1
 * 44,45S,46,47S = @1B000000 RAM (8 MB), FB
 * 33,34S,35,36S = @........ RAM (8 MB), Unknown (not tested by the RAM test)
 * 91S,92S       = @00C00000 CMOS SRAM (64 KB), Backup RAM
 * 98            = @02800000 8 MB SDRAM (main AICA board)
 *
 *
 * BIOSes
 * ======
 *
 * Currently three different bios revisions are known: 0.84, 0.92, and 0.96
 * (see the MAME driver.)
 *
 * Any IRQ controller in there pumping stuff from the various devices to
 * the master SH-4 IRL pins and Port A?
 *
 * The master SH-4 is configured for external-request DMAC (DTD). Requests
 * are sent either to the master SH-4 or the slave SH-4.
 *
 * TMU Channel 2 is configured for input capture; the DMAC is automatically
 * activeted whenever the interrupt fires. Timed DMA, way to go!
 *
 *
 * EEPROMs
 * =======
 *
 * The Hikaru hosts a few EEPROMs:
 *  - One on the MAINBD, connected to the master SH4 Port A.
 *  - One on the ROMBD, which likely holds game-specific data and protection.
 *  - One on the INPTBD, at 0800000[AC], unknown usage.
 *
 *
 * MAINBD EEPROM
 * =============
 *
 * The MAINBD EEPROM is a serial 128 x 8-bit EEPROM. It is interfaced to bits
 * 2-5 of PDTRA. The port is configured as follows:
 *
 *  PCTRA = (PCTRA & ~FF0) | 950
 *  PDTRA = PDTRA & ~3C
 *
 * See @0C00BF5C.
 *
 *  bit 2	Output		[configured as: pulled up, output]
 *  bit 3	Clock		[configured as: pulled up, output]
 *  bit 4	Chip Select	[configured as: pulled up, output]
 *  bit 5	Input		[configured as: not pulled up, input]
 *
 * See @0C0067AC, @0C0067DC.
 *
 *
 * ROMBD EEPROM, Type 1
 * ====================
 *
 * Located at 03000000, in bank_base + 0x14. It's a serial EEPROM of size
 * 128 x 8-bit entries. An 9C346. Bits (active low):
 *
 *  bit 0	Output
 *  bit 1	Clock
 *  bit 2	Chip Select
 *  bit 3	Input
 *
 * See @0C00C27E, @0C00C2AE
 *
 *
 * ROMBD EEPROM, Type 2
 * ====================
 *
 * An 76X100 secure EEPROM.
 *
 * MIE EEPROM
 * ==========
 *
 * See mie_board.cpp
 */

#include "odin/hikaru.hpp"

#include <algorithm>
#include <cinttypes>
#include <cstdarg>
#include <cstdlib>
#include <cstring>

#include "odin/env.hpp"

namespace odin {

namespace {

constexpr uint32_t KB = 1024;
constexpr uint32_t MB = 1024 * 1024;

/* The dot clock of the 31 kHz screen, which draws the 818 dots of a line.
 * HEURISTIC: the value of MAME (hikaru.cpp, 51894580 / 2), which does not
 * say where it comes from; no game sets or reads it. It gives 60.08 frames
 * a second. */
constexpr uint32_t DOT_CLOCK = 25947290;

/* What the scheduler dates. Those due at the same time are taken in this
 * order. */
enum : uint32_t {
	EVENT_LINE,		/* the end of a line */
};

/* SH7091 at 200 MHz (2 x SH-4 @ 200 MHz, see the top of this file).
 * VK_SH4_MHZ lowers it for debugging only: every phase of a game then takes
 * fewer host seconds. */
uint32_t
sh4_clock()
{
	const int mhz = env_int("VK_SH4_MHZ", 200);

	return static_cast<uint32_t>((mhz < 1 || mhz > 200) ? 200 : mhz) * 1000000u;
}

[[noreturn]] void
fatal(const char *fmt, ...)
{
	va_list args;

	std::fputs("FATAL: ", stderr);
	va_start(args, fmt);
	std::vfprintf(stderr, fmt, args);
	va_end(args);
	std::fputc('\n', stderr);
	std::abort();
}

/* The text at an offset of a ROM, as far as it goes */
std::string
text_at(std::span<const uint8_t> rom, size_t offs)
{
	std::string text;

	for (; offs < rom.size() && rom[offs]; offs++)
		text += static_cast<char>(rom[offs]);
	return text;
}

} /* namespace */

std::unique_ptr<Hikaru>
Hikaru::make(const Game &game, bool verbose)
{
	const std::optional<RomBoard::Config> rombd = RomBoard::config_of(game.name);
	const size_t mie = game.section("mie").size();

	if (game.section("bootrom").size() != 2 * MB) {
		std::fprintf(stderr, "ERROR: '%s' has no BOOTROM of 2 MB\n", game.name.c_str());
		return nullptr;
	}
	if (!rombd) {
		std::fprintf(stderr, "ERROR: the ROM board of '%s' is not known\n", game.name.c_str());
		return nullptr;
	}
	if (mie && mie != Mie::ROM_SIZE) {
		std::fprintf(stderr, "ERROR: the ROM of the MIE of '%s' has %zu bytes\n", game.name.c_str(), mie);
		return nullptr;
	}
	return std::unique_ptr<Hikaru>(new Hikaru(game, *rombd, verbose));
}

Hikaru::Hikaru(const Game &game, const RomBoard::Config &rombd, bool verbose)
	: m_game(game), m_verbose(verbose), m_screen(nullptr), m_panel(nullptr), m_clock(sh4_clock()), m_frame(0),
	  m_resetting(true), m_current(MASTER),
	  m_ram{std::vector<uint8_t>(32 * MB), std::vector<uint8_t>(32 * MB)},
	  m_cmdram(4 * MB), m_fb(8 * MB),
	  m_texram{std::vector<uint8_t>(4 * MB), std::vector<uint8_t>(4 * MB)},
	  m_sound_ram{std::vector<uint8_t>(8 * MB), std::vector<uint8_t>(8 * MB)},
	  m_mie_ram(32 * KB), m_bram(64 * KB), m_mscomm{},
	  m_iregs{std::vector<uint8_t>(0x10000), std::vector<uint8_t>(0x10000)},
	  m_bootrom(game.section("bootrom")),
	  m_memctl{MemCtl(*this, true), MemCtl(*this, false)},
	  m_rombd(rombd, game.section("eprom"), game.section("maskrom"),
	          game.name == "sgnascar" && !game.section("maskrom").empty(),
	          [this](const char *what) { cpu_error("%s", what); }),
	  m_eeprom(m_clock), m_mie_stand_in(m_mie_ram),
	  m_bus{Bus(*this, MASTER), Bus(*this, SLAVE)}, m_hooked{false, false},
	  m_porta{0, 0}, m_irq_lines(0), m_mie_port_d(0), m_unk1A800008_s(0), m_unk1B000100_s(0),
	  m_line(0), m_dots_per_line(0), m_lines_per_frame(0), m_lines_shown(0), m_line_rest(0),
	  m_mie_trace(nullptr), m_sh_trace{nullptr, nullptr}, m_stats(env_flag("ODIN_STATS")),
	  m_stats_ns{}, m_stats_slices(0), m_stats_told{}, m_stats_told_at(0)
{
	std::printf("%s\n", text_at(m_bootrom, 0xD4).c_str());

	build_mie();

	if (const std::span<const uint8_t> image = game.section("eeprom"); !image.empty()) {
		m_eeprom.load(image);
		std::printf("main board EEPROM: 93c46 image loaded\n");
	} else {
		std::printf("main board EEPROM: no image, blank\n");
	}
	if (const std::span<const uint8_t> image = game.section("rombd_eeprom"); !image.empty()) {
		m_rombd.eeprom().load(image);
		std::printf("ROM board EEPROM: x76f100 image loaded\n");
	} else {
		std::printf("ROM board EEPROM: no image, blank\n");
	}
	if (m_rombd.crypt())
		std::printf("ROM board 315-5881: key %08X, 64 KB RAM\n", m_rombd.config().crypt_key);
	if (m_rombd.fpga())
		std::printf("ROM board security FPGA: 317-0283-COM, ID 4252\n");

	HikaruGpu::Memory memory;
	HikaruGpu::Hooks hooks;

	memory.cmdram = m_cmdram;
	memory.ram_s = m_ram[SLAVE];
	memory.fb = m_fb;
	memory.texram[0] = m_texram[0];
	memory.texram[1] = m_texram[1];
	hooks.ctx = this;
	hooks.irq_changed = [](void *ctx) { static_cast<Hikaru *>(ctx)->update_irqs(); };
	hooks.list_started = [](void *ctx) {
		Hikaru *const board = static_cast<Hikaru *>(ctx);

		if (board->m_screen)
			board->m_screen->list_launched();
	};
	hooks.before_texture_write = [](void *ctx, unsigned bank) {
		Hikaru *const board = static_cast<Hikaru *>(ctx);

		if (board->m_screen)
			board->m_screen->before_texture_write(bank);
	};
	m_gpu = std::make_unique<HikaruGpu>(memory, hooks);
	m_gpu->antarctic().set_disasm(env_flag("GPU_LOG_CP") ? stdout : nullptr);
	m_gpu->antarctic().set_transfer_log(env_flag("GPU_LOG_IDMA") ? stdout : nullptr);
	m_gpu->eurasia().set_log(env_flag("GPU_LOG_DMA") ? stdout : nullptr);

	build_sound(MASTER);
	build_sound(SLAVE);
	/* HEURISTIC (notes MARKDOWN/30, section 3): the optional board also
	 * feeds the external inputs of the AICA of the main board. Air Trix
	 * and Virtual-On play their music on it and open EXTS on the main
	 * board, and the Air Trix cabinet has the board but main speakers
	 * only. */
	m_sound[MASTER]->set_source(m_sound[SLAVE].get());
	if (const char *path = std::getenv("ODIN_WAV")) {
		m_wav = std::make_unique<WavFile>(path, Aica::SAMPLE_RATE, 2);
		if (!m_wav->ok())
			std::fprintf(stderr, "ERROR: ODIN_WAV: cannot write '%s'\n", path);
	}

	/* Clock operating mode 5: a 33.3333 MHz crystal, the CPU at six times
	 * that and the peripheral clock at 50 MHz. The timers of PHARRIER
	 * count 0.08 microseconds (@0C0CF150), a quarter of that clock; MAME
	 * gives the pins (hikaru.cpp, set_md). */
	for (unsigned which : { MASTER, SLAVE }) {
		m_sh[which] = std::make_unique<Sh4>(m_bus[which], std::span<uint8_t>(m_iregs[which]),
		                                    which == MASTER, true, 5);
		/* ODIN_SH4_TRANSLATE=1, a test for now: the kept blocks run as
		 * host code. The memory and the states must be the same. */
		if (env_flag("ODIN_SH4_TRANSLATE", false)) {
			m_sh[which]->set_translate(true);
			if (!m_sh[which]->translates())
				std::fprintf(stderr, "sh4: no host code on this machine, the blocks are read\n");
		}
		/* ODIN_SH4_TRACE=<prefix>: one line per slice in <prefix>-m.txt
		 * and <prefix>-s.txt, the PC and a hash of the registers. */
		if (const char *prefix = std::getenv("ODIN_SH4_TRACE")) {
			const std::string path = std::string(prefix) + (which == MASTER ? "-m.txt" : "-s.txt");

			m_sh_trace[which] = std::fopen(path.c_str(), "w");
		}
	}

	if (game.name == "sgnascar")
		std::printf("Installed patches for '%s'\n", game.name.c_str());

	m_resetting = false;
}

Hikaru::~Hikaru()
{
	if (m_mie_trace)
		std::fclose(m_mie_trace);

	/* ODIN_STATS=1: what each SH-4 ran, and the host time each part of
	 * the board took. A part that waits for the screen before it writes
	 * the texture RAM has that wait in its time. */
	for (unsigned which : { MASTER, SLAVE }) {
		if (m_sh_trace[which])
			std::fclose(m_sh_trace[which]);
		if (m_stats)
			std::fprintf(stderr, "sh4 %c: %" PRIu64 " instructions run, %" PRIu64 " cycles skipped, %zu blocks"
			             " kept of %" PRIu64 " decoded, all dropped %" PRIu64 " times\n",
			             which == MASTER ? 'M' : 'S', m_sh[which]->instructions(), m_sh[which]->skipped(),
			             m_sh[which]->blocks(), m_sh[which]->blocks_decoded(), m_sh[which]->blocks_dropped());
		if (m_stats && m_sh[which]->translates())
			std::fprintf(stderr, "sh4 %c: host code of %u KB, %" PRIu64 " calls out of direct reach\n",
			             which == MASTER ? 'M' : 'S', m_sh[which]->host_bytes() / 1024,
			             m_sh[which]->host_far_calls());
	}
	if (m_stats) {
		const auto seconds = [this](unsigned part) { return static_cast<double>(m_stats_ns[part]) / 1e9; };

		std::fprintf(stderr, "board: %" PRIu64 " slices: master SH-4 %.3f s, slave SH-4 %.3f s, "
		             "graphics chips %.3f s, MIE %.3f s, sound boards %.3f s\n", m_stats_slices,
		             seconds(MASTER), seconds(SLAVE), seconds(PART_GPU), seconds(PART_MIE), seconds(PART_SOUND));
	}
}

/* The MIE: the chip and its board if its ROM is there, else the stand-in.
 * ODIN_MIE=hle asks for the stand-in.
 *
 * The CRP-1231 card reader (card_reader.hpp) goes on its serial block:
 * ODIN_CARD=none leaves the line open, ODIN_CARD=reader (default) attaches
 * it, ODIN_CARD_FILE=<path> is the card image (three tracks, 207 bytes),
 * ODIN_CARD_TRACE=1 logs every frame on the line to stderr. */
void
Hikaru::build_mie()
{
	const char *const mode = std::getenv("ODIN_MIE");
	const std::span<const uint8_t> rom = m_game.section("mie");
	bool lle = !mode || std::strcmp(mode, "hle");

	if (lle && rom.empty()) {
		std::fprintf(stderr, "ERROR: MIE: no 315-6146.bin in the game list, using the HLE stand-in\n");
		lle = false;
	}
	if (!lle) {
		std::printf("MIE: HLE stand-in (ODIN_MIE=lle for the Z80)\n");
		return;
	}

	m_mie = std::make_unique<MieBoard>(rom, m_mie_ram, m_clock, [this](MieBoard::Inputs &inputs) {
		if (m_panel)
			m_panel->read_inputs(inputs);
	});
	m_mie->set_port_d([this](uint8_t pins) { set_resets(pins); });
	m_mie->set_log(stderr);
	/* ODIN_TURNS=0, a test: the program of the MIE runs every turn of its
	 * waits. The memory and the states must be the same. */
	m_mie->set_waits_counted(env_flag("ODIN_TURNS", true));

	/* ODIN_MIE_TRACE=<path>: the bus of the Z80 for ODIN_MIE_TRACE_N
	 * instructions, from the first time its PC is ODIN_MIE_TRACE_START */
	if (const char *path = std::getenv("ODIN_MIE_TRACE")) {
		const unsigned count = static_cast<unsigned>(env_int("ODIN_MIE_TRACE_N", 300000));
		const char *const start = std::getenv("ODIN_MIE_TRACE_START");
		const unsigned start_pc = start ? static_cast<unsigned>(std::strtoul(start, nullptr, 16)) : 0;

		if (count) {
			m_mie_trace = std::fopen(path, "w");
			if (m_mie_trace)
				m_mie->set_trace(m_mie_trace, count, static_cast<uint16_t>(start_pc));
			else
				std::fprintf(stderr, "ERROR: MIE: can't open trace file '%s'\n", path);
		}
	}

	const char *const card = std::getenv("ODIN_CARD");

	if (card && !std::strcmp(card, "none")) {
		std::printf("MIE: card reader: none (ODIN_CARD=none)\n");
	} else {
		const char *const file = std::getenv("ODIN_CARD_FILE");
		const char *const trace = std::getenv("ODIN_CARD_TRACE");

		if (card && std::strcmp(card, "reader"))
			std::fprintf(stderr, "ERROR: MIE: ODIN_CARD=%s unknown, using the reader\n", card);
		const bool loaded = m_mie->attach_card(file, (trace && std::atoi(trace)) ? stderr : nullptr);
		if (file)
			std::printf("MIE: card reader CRP-1231 (HLE), card file '%s'%s\n",
			            file, loaded ? "" : " not found: no card");
		else
			std::printf("MIE: card reader CRP-1231 (HLE), no card file\n");
	}

	if (const char *path = std::getenv("ODIN_MIE_EEPROM")) {
		uint8_t data[128];
		std::FILE *const file = std::fopen(path, "rb");

		if (file && std::fread(data, 1, sizeof(data), file) == sizeof(data)) {
			m_mie->eeprom().load(std::span<const uint8_t>(data, sizeof(data)));
			std::printf("MIE: EEPROM file '%s' loaded\n", path);
		} else {
			std::printf("MIE: EEPROM file '%s' not found: blank\n", path);
		}
		if (file)
			std::fclose(file);
	}
	std::printf("MIE: LLE (315-6146 ROM + uploaded Z80 program)\n");
}

void
Hikaru::build_sound(unsigned which)
{
	m_sound[which] = std::make_unique<SoundBoard>(m_sound_ram[which], m_clock, [this](bool) { update_irqs(); });
	m_sound[which]->set_log(stderr);
	/* ODIN_TURNS=0, a test: the programs of the sound boards run every
	 * turn of their waits. The memory and the states must be the same. */
	m_sound[which]->set_turn_replay(env_flag("ODIN_TURNS", true));

	/* VK_ARM7_TRACE=<prefix> writes <prefix>-m.txt / <prefix>-s.txt */
	if (const char *prefix = std::getenv("VK_ARM7_TRACE")) {
		const std::string path = std::string(prefix) + (which == MASTER ? "-m.txt" : "-s.txt");

		m_sound[which]->set_arm_trace(std::fopen(path.c_str(), "w"));
	}
}

/* The battery of the clock: the RTC of the first sound board keeps counting
 * when the cabinet is off. The count it starts with here is the local time
 * of the host, as on a board whose clock was set once and whose battery
 * never died. With ODIN_CLOCK_FILE=<path> the board keeps the clock it was
 * set to (CLOCK SETTING in the SYSTEM MENU): the file holds, in seconds, how
 * far that clock is ahead of the time of the host. */
void
Hikaru::reset_sound(unsigned which)
{
	const char *const path = std::getenv("ODIN_CLOCK_FILE");

	m_sound[which]->reset();
	if (!path || which != MASTER)
		return;

	std::FILE *const file = std::fopen(path, "r");
	int ahead = 0;

	if (file && std::fscanf(file, "%d", &ahead) == 1) {
		m_sound[which]->set_rtc_ahead(ahead);
		std::printf("AICA: clock file '%s': %d s ahead of the host\n", path, ahead);
	} else {
		std::printf("AICA: clock file '%s' not found: the time of the host\n", path);
	}
	if (file)
		std::fclose(file);
}

void
Hikaru::save_clock()
{
	const char *const path = std::getenv("ODIN_CLOCK_FILE");

	if (!path)
		return;
	std::FILE *const file = std::fopen(path, "w");
	if (!file) {
		std::fprintf(stderr, "ERROR: AICA: can't write the clock file '%s'\n", path);
		return;
	}
	std::fprintf(file, "%d\n", static_cast<int>(m_sound[MASTER]->rtc_ahead()));
	std::fclose(file);
}

/* Power-on. The memories are emptied, then the chips are reset, in an order
 * that matters to nothing: what one tells another meanwhile waits for the
 * end (m_resetting). */
void
Hikaru::reset()
{
	if (m_screen)
		m_screen->before_memory_changes();

	m_resetting = true;

	for (unsigned which : { MASTER, SLAVE }) {
		std::fill(m_ram[which].begin(), m_ram[which].end(), 0);
		std::fill(m_texram[which].begin(), m_texram[which].end(), 0);
		std::fill(m_sound_ram[which].begin(), m_sound_ram[which].end(), 0);
		std::fill(m_iregs[which].begin(), m_iregs[which].end(), 0);
	}
	std::fill(m_cmdram.begin(), m_cmdram.end(), 0);
	std::fill(m_fb.begin(), m_fb.end(), 0);
	std::fill(m_mie_ram.begin(), m_mie_ram.end(), 0);
	std::fill(m_bram.begin(), m_bram.end(), 0);

	m_memctl[MASTER].reset();
	m_memctl[SLAVE].reset();
	m_mscomm.fill(0);
	std::fill(m_mie_stand_in.registers().begin(), m_mie_stand_in.registers().end(), 0);
	if (m_mie)
		m_mie->reset();
	else
		m_mie_stand_in.reset();
	m_gpu->reset();
	reset_sound(MASTER);
	reset_sound(SLAVE);
	map_pages();
	m_sh[MASTER]->reset(true);
	m_sh[SLAVE]->reset(true);

	m_resetting = false;

	m_sh[SLAVE]->set_run(Sh4Run::Running);

	/* The pins of port A are active low. */
	m_porta[MASTER] = 0xFFFF;
	m_porta[SLAVE] = 0xFFFF;
	/* HEURISTIC: nothing is known to pull the NMI pin of the master. */
	m_sh[MASTER]->set_nmi(true);
	m_sh[SLAVE]->set_nmi(true);
	m_mie_port_d = 0xFF;
	set_gpu_reset_lines();

	m_unk1A800008_s = 0xFFFF;
	m_unk1B000100_s = 0xFEFF;

	m_rombd.reset();

	m_sched.reset();
	m_line_rest = 0;
	begin_frame();

	update_irqs();

	if (m_screen)
		m_screen->memory_changed();
	m_frame = 0;
}

/*
 * IRQs
 * ====
 *
 * The master SH-4 is configured for independent per-pin external IRQs. It
 * looks like that the IRQ sources are as follows:
 *
 *  IRL0 unused/unhandled
 *  IRL1 memory controller DMA termination
 *  IRL2 the interrupt controller of the board, see below
 *  IRL3 unused/unhandled
 *
 * IRQ-related Registers
 * =====================
 *
 * The following registers are used in the IRL2 handling routine to check
 * what happened, see @0C00174C.
 *
 * PDTRA (active low):
 *
 *  0040	IRQ source is GPU
 *  0080	IRQ source is AICA/DMA
 *  0100	IRQ source is UNKNOWN [slave?], calls @0C000A30
 *  0200	Error
 *
 * For more informations on the GPU IRQs, see hikaru_gpu.hpp
 *
 * The interrupt controller
 * ========================
 *
 * Five lines, named as on the schematics (MAME hikaru.cpp, MIE GPIO
 * ports), go to port B of the MIE, and four of them to port A of the
 * master as well:
 *
 *  ANT_INT	the command processor	port B bit 0	PDTRA 0040
 *  SZK_INT	the sound boards	port B bit 4	PDTRA 0080
 *  CN4_INT	connector CN4		port B bit 5	PDTRA 0100
 *  CN3_INT0	connector CN3		port B bit 6
 *  CN3_INT1	connector CN3		port B bit 7	PDTRA 0200
 *
 * The MIE holds IRL2 while one of them passes the mask its program writes
 * to port C (0xE1 in every game: the sound boards do not interrupt). Each
 * is a level, held until its handler has acknowledged the chip it comes
 * from. Nothing is on CN3 and CN4.
 *
 * HEURISTIC: the second sound board is taken to share SZK_INT with the
 * first, whose handler acknowledges both (PHARRIER @0C0102F2).
 */

void
Hikaru::update_irqs()
{
	uint8_t lines = 0;

	/* A chip may call while the board is being built. While it is reset
	 * the lines wait for the end: the registers of the SH-4 are cleared
	 * first, and a chip that is not reset yet may still ask. */
	if (m_resetting)
		return;

	if (m_gpu->irq_line())
		lines |= 0x01;
	if (m_sound[MASTER]->main_irq() || m_sound[SLAVE]->main_irq())
		lines |= 0x10;
	m_irq_lines = lines;
	if (m_mie)
		m_mie->set_irq_lines(lines);

	m_sh[MASTER]->set_irq(Sh4Source::IRL1, m_memctl[MASTER].irq_line());
	/* The stand-in of the MIE has the mask its program writes, 0xE1. */
	m_sh[MASTER]->set_irq(Sh4Source::IRL2, m_mie ? m_mie->irq_out() : (lines & 0xE1) != 0);
}

/*
 * Resets
 * ======
 *
 * Port D of the MIE holds the reset lines of the board, active low (MAME
 * hikaru.cpp, MIE GPIO ports): bit 0 the system, 1 the graphics chips
 * (GP_RES), 2 the slave SH-4, 3 CN4, 4 the display chip (PCI1_RES), 5 the
 * sound boards; bits 6 and 7 choose the video clock. The master writes the
 * port through the window of the MIE:
 *
 *  - the BOOTROM, as soon as the MIE gives its bus (@0C00BE52): 0xFB, then
 *    0xFF some 250 cycles later, for the slave;
 *  - the system library of the game (0xDF, then 0xFF): the sound boards;
 *  - VON4, when two lists in a row did not end (@0C711C30): 0xEF, 0xFD,
 *    then 0xFF, for the graphics chips, see HikaruGpu::set_reset_lines.
 *    Here it does each time its SYSTEM MENU is left with EXIT, see the
 *    TODO.
 *
 * The reset of the slave is NOT done. The slave takes a frame and a half
 * to start again (it clears its memory) and then clears the word where
 * the master leaves its first job (14000000); the master writes that word
 * without waiting for anything (PHARRIER @0C010BC4), after a copy of the
 * game from the ROM board that takes no time here. With the reset done,
 * BRAVEFF and PHARRIER write the word a frame too early and never start.
 */

/* Bits 1 and 4 of port D, to the graphics chips */
void
Hikaru::set_gpu_reset_lines()
{
	m_gpu->set_reset_lines(!(m_mie_port_d & 0x02), !(m_mie_port_d & 0x10));
}

/* The pins of port D of the MIE, each time it drives them. */
void
Hikaru::set_resets(uint8_t pins)
{
	const uint8_t fell = m_mie_port_d & static_cast<uint8_t>(~pins);

	m_mie_port_d = pins;

	if ((pins & 0xC0) != 0xC0)
		fatal("video clock %u chosen on port D of the MIE: only 3 is known", pins >> 6);
	if (~pins & 0x09)
		fatal("resets %02X asked on port D of the MIE, which are not implemented", ~pins & 0x09);

	set_gpu_reset_lines();

	/* The boards are held as long as the line is low, see run_cycles. */
	if (fell & 0x20) {
		m_sound[MASTER]->reset_pin();
		m_sound[SLAVE]->reset_pin();
		update_irqs();
	}
}

void
Hikaru::run_cpu(unsigned which, int cycles)
{
	m_current = which;

	/* The address the board looks at the program at, see Bus::hook */
	if (!m_hooked[which]) {
		m_hooked[which] = true;
		if (which == SLAVE)
			m_sh[which]->set_hook(0x0C002030);
		else if (m_game.name == "sgnascar")
			m_sh[which]->set_hook(0x0C00BC9A);
	}

	m_sh[which]->run(cycles);
	if (m_sh_trace[which])
		trace_slice(which);
}

void
Hikaru::run_cycles(int cycles)
{
	/* ODIN_STATS: the host time since the part before goes to this one. */
	uint64_t mark = m_stats ? host_ns() : 0;
	const auto took = [this, &mark](unsigned part) {
		if (m_stats) {
			const uint64_t now = host_ns();

			m_stats_ns[part] += now - mark;
			mark = now;
		}
	};

	run_cpu(MASTER, cycles);
	took(MASTER);
	run_cpu(SLAVE, cycles);
	took(SLAVE);

	m_eeprom.tick(static_cast<uint32_t>(cycles));

	/* The memory controllers have nothing to run: a transfer is done at
	 * once. */
	m_gpu->run(static_cast<uint32_t>(cycles));
	took(PART_GPU);

	if (m_mie)
		m_mie->run(static_cast<uint32_t>(cycles));
	/* Its program may have written the mask of the interrupts. */
	update_irqs();
	took(PART_MIE);

	/* The two sound boards, unless their reset line is low */
	if (m_mie_port_d & 0x20) {
		m_sound[SLAVE]->run(static_cast<uint32_t>(cycles));
		m_sound[MASTER]->run(static_cast<uint32_t>(cycles));
	} else {
		m_sound[MASTER]->silence(static_cast<uint32_t>(cycles));
		m_sound[SLAVE]->silence(static_cast<uint32_t>(cycles));
	}
	took(PART_SOUND);
	m_stats_slices++;
}

/* The cycles of the next line. A line is not a whole number of cycles: the
 * rest is kept. */
uint64_t
Hikaru::line_cycles()
{
	const uint64_t n = static_cast<uint64_t>(m_dots_per_line) * m_clock + m_line_rest;

	m_line_rest = static_cast<uint32_t>(n % DOT_CLOCK);
	return n / DOT_CLOCK;
}

/* Starts a frame with the lines the game has set by now. The line counter
 * of the graphics chips is 0 on the first line shown (PHARRIER waits for 240
 * to 479, @0C01C106): a frame goes from that line to the end of the
 * blanking. */
void
Hikaru::begin_frame()
{
	uint32_t dots, lines, shown;

	m_gpu->eurasia().frame_size(dots, lines, shown);
	m_dots_per_line = dots;
	m_lines_per_frame = lines;
	m_lines_shown = shown;
	if (m_dots_per_line != 818)
		fatal("a screen of %u dots per line: only the 31 kHz one (818) is known", m_dots_per_line);
	if (m_lines_shown == 0 || m_lines_shown >= m_lines_per_frame)
		fatal("a screen of %u lines with %u shown", m_lines_per_frame, m_lines_shown);

	m_line = 0;
	m_sched.set(EVENT_LINE, m_sched.now() + line_cycles());
}

/* The chips run up to the next event, then the events due are taken. */
void
Hikaru::run_frame()
{
	bool done = false;

	if (m_verbose)
		std::printf(" *** VBLANK-OUT %s ***\n", board_string().c_str());

	while (!done) {
		const uint64_t next = m_sched.next();
		uint32_t event;

		run_cycles(static_cast<int>(next - m_sched.now()));
		m_sched.advance(next);

		while (m_sched.take(event)) {
			m_gpu->end_of_line(m_line++);
			if (m_line == m_lines_shown) {
				if (m_verbose)
					std::printf(" *** VBLANK-IN  %s ***\n", board_string().c_str());
				m_gpu->vblank_in();
			}
			if (m_line < m_lines_per_frame) {
				m_sched.set(EVENT_LINE, next + line_cycles());
				continue;
			}

			/* this may actually be an hblank-out IRQ */
			m_gpu->vblank_out();

			done = true;
		}
	}
	begin_frame();
	m_frame++;

	if (m_wav)
		m_wav->write(m_sound[MASTER]->samples());
	m_sound[MASTER]->clear_samples();
	m_sound[SLAVE]->clear_samples();

	/* ODIN_STATS: every 60 frames, the host time of a frame in all and
	 * that of each part, in ms. What is not in the parts went to the rest
	 * of the frame: the screen, the wait for the drawing. */
	if (m_stats && m_frame % 60 == 0) {
		const uint64_t now = host_ns();
		const auto ms = [](uint64_t ns) { return static_cast<double>(ns) / 60e6; };

		if (m_stats_told_at)
			std::fprintf(stderr, "PARTS frame=%u all %.2f master %.2f slave %.2f chips %.2f mie %.2f sound %.2f\n",
			             m_frame, ms(now - m_stats_told_at), ms(m_stats_ns[MASTER] - m_stats_told[MASTER]),
			             ms(m_stats_ns[SLAVE] - m_stats_told[SLAVE]),
			             ms(m_stats_ns[PART_GPU] - m_stats_told[PART_GPU]),
			             ms(m_stats_ns[PART_MIE] - m_stats_told[PART_MIE]),
			             ms(m_stats_ns[PART_SOUND] - m_stats_told[PART_SOUND]));
		std::copy(std::begin(m_stats_ns), std::end(m_stats_ns), std::begin(m_stats_told));
		m_stats_told_at = now;
	}
}

/* The idle loop of the slave, in the BOOTROM, counts in R7 while the byte
 * the master posts at GBR stays zero:
 *
 *	0C002030  mov.b @(0,gbr),r0
 *	          mov   #2,r2
 *	          shll8 r2
 *	          addc  r2,r7
 *	          mov   #-128,r1
 *	          bf/s  1f
 *	          shll16 r1
 *	          (on a carry: writes port A)
 *	1:        tst   r0,r0
 *	          mov   #0,r0
 *	          bt    0C002030
 *
 * A turn without a carry takes ten steps and only adds 0x201 to R7, and
 * nothing else runs before the end of the slice: such turns are counted
 * without being run, up to the turn that carries or the last of the slice. */
void
Hikaru::slave_idle_loop()
{
	Sh4 &cpu = *m_sh[SLAVE];
	const int32_t left = cpu.remaining();
	const uint32_t gbr = cpu.gbr();
	uint16_t code[19];

	if ((cpu.pc() & 0x1FFFFFFF) != 0x0C002030 || left <= 10)
		return;

	std::memcpy(code, &m_ram[SLAVE][0x2030], sizeof(code));
	if (code[0] != 0xC400 || code[1] != 0xE202 || code[2] != 0x4218 ||
	    code[3] != 0x372E || code[4] != 0xE180 ||
	    (code[5] & 0xFF80) != 0x8F00 || code[6] != 0x4128)
		return;

	const uint32_t tst = 7u + (code[5] & 0x7F);
	if (tst > 16 || code[tst] != 0x2008 || code[tst + 1] != 0xE000 ||
	    code[tst + 2] != (0x8900 | ((0 - (tst + 4)) & 0xFF)))
		return;

	/* The registers a turn leaves, and the posted byte still zero */
	if (cpu.reg(0) != 0 || cpu.reg(1) != 0xFF800000 || cpu.reg(2) != 0x200 || !(cpu.sr() & 1) ||
	    (gbr >> 29) == 7 || (gbr & 0x1E000000) != 0x0C000000 ||
	    m_ram[SLAVE][gbr & 0x01FFFFFF])
		return;

	const uint32_t turns = std::min((0xFFFFFFFF - cpu.reg(7)) / 0x201,
	                                static_cast<uint32_t>(left - 1) / 10);
	if (!turns)
		return;
	cpu.set_reg(7, cpu.reg(7) + turns * 0x201);
	cpu.skip(turns * 10);
}

/* Before the wave memories are saved or written to a file: the ARM7 of a
 * sound board puts there what it was holding back. */
void
Hikaru::settle()
{
	m_sound[MASTER]->settle();
	m_sound[SLAVE]->settle();
}

void
Hikaru::power_off()
{
	const struct {
		const char *name;
		std::span<const uint8_t> bytes;
	} memories[] = {
		{ "ram-m", m_ram[MASTER] }, { "ram-s", m_ram[SLAVE] }, { "cmdram", m_cmdram },
		{ "texram-0", m_texram[0] }, { "texram-1", m_texram[1] }, { "fb", m_fb },
		{ "aica-m", m_sound_ram[MASTER] }, { "aica-s", m_sound_ram[SLAVE] },
		{ "bram", m_bram }, { "mie", m_mie_ram },
	};

	settle();
	for (const auto &memory : memories) {
		const std::string path = m_game.name + "-" + memory.name + ".bin";
		std::FILE *const file = std::fopen(path.c_str(), "wb");

		if (!file)
			continue;
		std::fwrite(memory.bytes.data(), 1, memory.bytes.size(), file);
		std::fclose(file);
	}

	/* The 93C46 of the MIE holds the system settings the BOOTROM keeps
	 * (its test menu: country, coin chutes, ...). */
	if (const char *path = std::getenv("ODIN_MIE_EEPROM"); m_mie && path) {
		const std::span<uint8_t> cells = m_mie->eeprom().data();
		std::FILE *const file = std::fopen(path, "wb");

		if (!file || std::fwrite(cells.data(), 1, cells.size(), file) != cells.size())
			std::fprintf(stderr, "ERROR: MIE: cannot write the EEPROM file '%s'\n", path);
		if (file)
			std::fclose(file);
	}
}

std::string
Hikaru::cpu_string(unsigned which) const
{
	char text[64];

	std::snprintf(text, sizeof(text), "%c @%08X @%08X %08X", which == MASTER ? 'M' : 'S',
	              m_sh[which]->pc(), m_sh[which]->pr(), m_sh[which]->sr());
	return text;
}

std::string
Hikaru::board_string() const
{
	char text[512];

	std::snprintf(text, sizeof(text), "[%s %04X] [%s %04X] [%s]",
	              cpu_string(MASTER).c_str(), m_porta[MASTER],
	              cpu_string(SLAVE).c_str(), m_porta[SLAVE],
	              m_gpu->debug_string().c_str());
	return text;
}

/* A message goes out whole, in one write: the drawing thread writes to
 * stderr too, and a formatted print to a stream without a buffer is a
 * system call for each of its pieces on some hosts. */
void
Hikaru::tell_error(const std::string &where, const char *what)
{
	const std::string line = "ERROR: " + where + " : " + what + "\n";

	std::fputs(line.c_str(), stderr);
}

/* What a program did that has no answer here, with where the SH-4 that runs
 * is, or with where the whole board is. */
void
Hikaru::cpu_error(const char *fmt, ...)
{
	char what[256];
	va_list args;

	va_start(args, fmt);
	std::vsnprintf(what, sizeof(what), fmt, args);
	va_end(args);
	tell_error(cpu_string(m_current), what);
}

void
Hikaru::board_error(const char *fmt, ...)
{
	char what[256];
	va_list args;

	va_start(args, fmt);
	std::vsnprintf(what, sizeof(what), fmt, args);
	va_end(args);
	tell_error(board_string(), what);
}

/* With -v: an access to what is worth watching */
void
Hikaru::log_access(const char *name, char rw, unsigned size, uint32_t addr, uint64_t value)
{
	if (!m_verbose)
		return;
	if (rw == 'R')
		std::printf("%s : %s R%u %08X\n", board_string().c_str(), name, size * 8, addr);
	else
		std::printf("%s : %s W%u %08X = %" PRIX64 "\n", board_string().c_str(), name, size * 8, addr, value);
}

/* A line of ODIN_SH4_TRACE: the PC and a hash of the registers, FNV-1a over
 * them one after the other. */
void
Hikaru::trace_slice(unsigned which)
{
	Sh4State st;
	uint64_t hash = 0xCBF29CE484222325ull;
	const auto add = [&hash](const void *data, size_t size) {
		const uint8_t *const bytes = static_cast<const uint8_t *>(data);

		for (size_t i = 0; i < size; i++)
			hash = (hash ^ bytes[i]) * 0x100000001B3ull;
	};

	m_sh[which]->save(st);
	add(st.r, sizeof(st.r));
	add(&st.pc, 4);
	add(&st.sr, 4);
	add(&st.pr, 4);
	add(&st.gbr, 4);
	add(&st.vbr, 4);
	add(&st.dbr, 4);
	add(&st.macl, 4);
	add(&st.mach, 4);
	add(&st.spc, 4);
	add(&st.ssr, 4);
	add(&st.sgr, 4);
	add(st.rbank, sizeof(st.rbank));
	add(st.fr, sizeof(st.fr));
	add(st.xf, sizeof(st.xf));
	add(&st.fpul, 4);
	add(&st.fpscr, 4);
	add(st.tmu_counter, sizeof(st.tmu_counter));
	add(&st.irq_index, 4);

	std::fprintf(m_sh_trace[which], "%08X %016" PRIX64 "\n", st.pc, hash);
}

} /* namespace odin */
