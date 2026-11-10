/*
 * The GPU harness of the picture path (M3, E): odin_selftest_picture. A
 * synthetic Hikaru board (its own SH-4 stub as the boot ROM: no BIOS, no game)
 * is loaded as the core's game and run through the same ABI calls the frontend
 * makes (odin_frame, odin_picture_pixels, odin_unload) on the card the host
 * gave (Dozen, through the ICD). The pictures are read back and described:
 * device, frame size, a checksum (FNV-1a 64 of the pixel rows), the count of
 * non-black pixels, the time per frame. Then Odin's own loading screen (not a
 * game picture) is read back the same way. The program list given is made ready
 * and counted (PC only: not representative). Nothing here is a game, a BIOS or
 * artwork. GPL-3.0-or-later.
 */

#define ODIN_CORE_BUILD 1
#include "odin_c.h"

#include "odin/host/app/games.hpp"
#include "odin/host/renderer/hikaru/renderer.hpp"
#include "odin/machine/chips/mie.hpp"

#include "odin_graphics_vk.hpp"
#include "odin_screen.hpp"

#include <windows.h>

#include <chrono>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace odinc {

OdinGraphics *card_for_tests(std::string &why);
int32_t load_synthetic(odin::Game game, const std::string &name, const std::string &seed, std::string &why);
void loading_frame_now(const std::string &title, unsigned done, unsigned total);
void finish_drawing_now();
OdinScreen *screen_now();
void log_line(int32_t level, const std::string &text);

namespace {

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

/* A Hikaru machine in memory: the boot ROM and the MIE ROM (zeros) */
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

double
seconds_since(std::chrono::steady_clock::time_point began)
{
	return std::chrono::duration<double>(std::chrono::steady_clock::now() - began).count();
}

/* FNV-1a 64 over the rows of w pixels of 4 bytes, each row pitch bytes apart */
uint64_t
checksum(const PictureView &view)
{
	uint64_t hash = 0xcbf29ce484222325ull;
	const size_t row = size_t{ view.w } * 4;

	for (uint32_t y = 0; y < view.h; y++)
		for (size_t i = 0; i < row; i++) {
			hash ^= view.pixels[size_t{ y } * view.pitch + i];
			hash *= 0x100000001b3ull;
		}
	return hash;
}

/* Pixels that are not black (B, G or R non-zero) */
uint32_t
lit_pixels(const PictureView &view)
{
	uint32_t lit = 0;

	for (uint32_t y = 0; y < view.h; y++) {
		const uint8_t *from = view.pixels + size_t{ y } * view.pitch;

		for (uint32_t x = 0; x < view.w; x++, from += 4)
			if (from[0] | from[1] | from[2])
				lit++;
	}
	return lit;
}

} /* namespace */

int32_t
picture_test(const std::string &programs_list, uint32_t frames, std::string &report)
{
	std::vector<std::string> lines;
	const auto say = [&](const std::string &text) {
		lines.push_back(text);
		log_line(ODIN_LOG_INFO, "picture-test: " + text);
	};
	const auto done = [&](int32_t status) {
		report.clear();
		for (const std::string &line : lines)
			report += line + "\n";
		return status;
	};
	const auto describe = [&](const char *what, const PictureView &view) {
		say(std::string(what) + ": " + std::to_string(view.w) + "x" + std::to_string(view.h) + " pitch " +
		    std::to_string(view.pitch) + ", sequence " + std::to_string(view.sequence) + ", fnv1a " +
		    std::to_string(checksum(view)) + ", non-black pixels " + std::to_string(lit_pixels(view)) + " of " +
		    std::to_string(size_t{ view.w } * view.h));
	};

	std::string why;
	OdinGraphics *card = card_for_tests(why);

	if (!card) {
		say("FAIL no graphics card: " + why);
		return done(ODIN_E_FAILED);
	}
	char knob[64] = {};
	const DWORD knob_len = GetEnvironmentVariableA("DZN_FORCE_FL11_0", knob, sizeof(knob));

	say("device: " + card->name());
	say("knob DZN_FORCE_FL11_0: " + (knob_len && knob_len < sizeof(knob) ? std::string(knob) : std::string("unset")));
	say("frame size: " + std::to_string(odin::Renderer::WIDTH) + "x" + std::to_string(odin::Renderer::HEIGHT) +
	    " at scale 1, B8G8R8A8");

	const uint32_t count = frames ? frames : 1;
	const int32_t loaded = load_synthetic(synthetic("picture-test", spin_rom()), "picture-test", programs_list, why);

	if (loaded != ODIN_OK) {
		say("FAIL the synthetic board was not loaded (status " + std::to_string(loaded) + "): " + why);
		return done(ODIN_E_FAILED);
	}
	if (const OdinScreen *screen = screen_now()) {
		say("programs: list '" + (programs_list.empty() ? std::string("none") : programs_list) + "': " +
		    std::to_string(screen->programs_listed()) + " listed, " + std::to_string(screen->programs_read_back()) +
		    " read back, " + std::to_string(screen->programs_compiled()) +
		    " compiled (PC only, not representative; the time is in the log)");
	}

	const auto began = std::chrono::steady_clock::now();
	uint32_t pictures = 0, no_picture = 0;

	for (uint32_t i = 0; i < count; i++) {
		uint32_t w = 0, h = 0, pitch = 0, fresh = 0;
		const int32_t status = odin_frame(&w, &h, &pitch, &fresh);

		if (status < 0) {
			say("FAIL odin_frame returned " + std::to_string(status) + " at frame " + std::to_string(i));
			odin_unload(nullptr, 0);
			return done(ODIN_E_FAILED);
		}
		if (status == ODIN_FRAME_PICTURE)
			pictures += fresh;
		else
			no_picture++;
	}
	finish_drawing_now();
	say("frames: " + std::to_string(count) + " through odin_frame (" + std::to_string(no_picture) +
	    " before the first picture), " + std::to_string(pictures) + " fresh pictures; " +
	    std::to_string(seconds_since(began) * 1000.0 / count) + " ms per frame (PC only, not representative)");

	PictureView view = card->latest();

	if (!view.pixels) {
		say("FAIL no board picture was published");
		odin_unload(nullptr, 0);
		return done(ODIN_E_FAILED);
	}
	describe("board picture (the last frame drawn; the synthetic board has no display lists)", view);
	say(std::string("odin_picture_pixels: ") + (odin_picture_pixels() ? "non-null" : "NULL"));

	loading_frame_now("picture-test", 1, 2);
	view = card->latest();
	describe("loading picture (Odin's loading screen, 1 of 2)", view);

	/* The game stays loaded: the frontend's own path (host_run_frame, host_frame) runs it next, then unloads it */
	say("PASS picture path: frames run through odin_frame and read back through the card (the synthetic game stays "
	    "loaded for the frontend's path check)");
	return done(ODIN_OK);
}

} /* namespace odinc */
