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

/* The board drawing with Vulkan (board_drawing.hpp): made. Its pictures are
 * in board_pictures_vk.cpp; the textures, the scene and its translucent
 * fragments come with the next steps (MARKDOWN/106 § 5). */

#include "odin/board_drawing_vk.hpp"

namespace odin {

std::unique_ptr<BoardDrawing>
BoardDrawing::make_vk(vk::Context &card)
{
	return std::make_unique<VkBoardDrawing>(card);
}

} /* namespace odin */
