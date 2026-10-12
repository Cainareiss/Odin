/*
 * Odin
 * Copyright (C) 2026 Réda Chérif-Touil
 *
 * Valkyrie
 * Copyright (C) 2011-2014, Stefano Teso
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

#ifndef ODIN_RENDERER_SHADERS_HPP
#define ODIN_RENDERER_SHADERS_HPP

/* The text of the shaders of the renderer. */

namespace odin::shaders {

/* A mesh of the display list. The two texts come after a #version line and
 * the defines of the variant, see renderer_programs.cpp. */
extern const char *const MESH_VERTEX;
extern const char *const MESH_FRAGMENT;

/* One triangle over the whole picture, without any vertex data */
extern const char *const WHOLE_PICTURE_VERTEX;
/* For that triangle: the translucent fragments of each pixel put in order
 * and blended, and two 3D buffers blended into one. */
extern const char *const SORT_FRAGMENT;
extern const char *const BLEND_FRAGMENT;

/* The picture shown and the 2D layers, on the window */
extern const char *const COMPOSE_VERTEX;
extern const char *const COMPOSE_FRAGMENT;

} /* namespace odin::shaders */

#endif /* ODIN_RENDERER_SHADERS_HPP */
