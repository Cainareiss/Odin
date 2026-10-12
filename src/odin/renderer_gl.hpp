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

#ifndef ODIN_RENDERER_GL_HPP
#define ODIN_RENDERER_GL_HPP

/* What the renderer needs around OpenGL: a program from its two texts, a
 * check after a stage, and the projection it computes for the shaders. */

#include <GL/glew.h>

namespace odin::gl {

/* Compiles and links a program. Aborts with what the driver says if it
 * cannot. */
GLuint compile_program(const char *vertex, const char *fragment);
/* The location of a uniform the program must have. */
GLint uniform(GLuint program, const char *name);
/* Aborts if OpenGL has an error to tell; `stage` names where. */
void check(const char *stage);

/* The perspective projection of the box l..r by b..t at the distance n. */
void frustum(float out[4][4], float l, float r, float b, float t, float n, float f);

} /* namespace odin::gl */

#endif /* ODIN_RENDERER_GL_HPP */
