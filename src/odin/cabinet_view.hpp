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

#ifndef ODIN_CABINET_VIEW_HPP
#define ODIN_CABINET_VIEW_HPP

#include <span>
#include <string>
#include <vector>

#include "imgui/imgui.h"
#include "odin/control_panel.hpp"
#include "odin/menu_kit.hpp"

namespace odin {

/* What lights the drawing of a cabinet: its controls and where they are
 * now, and whether COIN, SERVICE and TEST of player 1 are worked */
struct CabinetNow {
	std::span<const GameControl> controls;
	std::span<const ControlNow> now;
	bool coin = false, service = false, test = false;
};

/* The cabinet of a game drawn in a box (mock-up v50), as large as it fits
 * and in its middle: its controls where the cabinet has them, which move
 * as they are worked and are outlined with the accent, the light of a
 * button lit; COIN, SERVICE and TEST under them. False for a game it has
 * no drawing of. */
bool draw_cabinet(const MenuKit &kit, const std::string &game, const CabinetNow &now, ImVec2 a, ImVec2 b);

} /* namespace odin */

#endif /* ODIN_CABINET_VIEW_HPP */
