/*
 * The loaded game of odin_core.dll (M1): the board with a null screen (no
 * picture until the graphics layer, M3) and a panel that reads the pads the
 * frontend gives (the full ControlPanel mapping is M4). GPL-3.0-or-later.
 */

#ifndef ODIN_CORE_SESSION_HPP
#define ODIN_CORE_SESSION_HPP

#include "odin/host/app/games.hpp"
#include "odin/machine/boards/hikaru/hikaru.hpp"
#include "odin/machine/boards/hikaru/hikaru_host.hpp"

#include "odin_c.h"
#include "odin_screen.hpp"

#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>

namespace odinc {

/* The screen of M1: the board's graphics calls go nowhere. The picture
 * comes with the graphics layer (M3). */
class NullScreen final : public odin::HikaruScreen {
public:
	void list_launched() override {}
	void before_texture_write(unsigned, uint32_t, uint32_t, uint32_t, uint32_t) override {}
	void before_memory_changes() override {}
	void memory_changed() override {}
	bool save_pictures(odin::StateFile &) override { return true; }
	bool load_pictures(odin::StateFile &) override { return true; }
};

/* The cabinet's controls from the pads of the frontend. M1 defaults: the
 * pad's A and B are the two push buttons, the D-pad the directions, START
 * the start of the player, View (BACK) the coin of the player (one per press),
 * the left stick the first analog channel. */
class PadPanel final : public odin::HikaruPanel {
public:
	void set(const OdinPad *pads, uint32_t count);
	void read_inputs(odin::CabinetInputs &inputs) override;

private:
	std::mutex m_mutex;
	OdinPad m_pads[2]{};
	bool m_coin_down[2]{};
};

/* A game in memory. The members are destroyed in reverse order: the board
 * first (it refers to the game and the screen; the panel lives in the core,
 * since the pads come before a game is loaded). */
struct LoadedGame {
	std::optional<odin::Game> game;
	NullScreen screen;
	/* M3: the picture, when the graphics card is there (odin_set_vulkan_icd).
	 * The board's screen is `picture` then, else `screen`. Declared before the
	 * board so that it is destroyed after it; odin_unload detaches it first. */
	std::unique_ptr<OdinScreen> picture;
	std::unique_ptr<odin::Hikaru> board;
};

} /* namespace odinc */

#endif /* ODIN_CORE_SESSION_HPP */
