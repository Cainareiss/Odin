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

#ifndef ODIN_GL_COMMON_HPP
#define ODIN_GL_COMMON_HPP

/* What the renderer and the board drawing need around OpenGL: a program
 * from its two texts, a check after a stage, textures and framebuffers to
 * draw in, and the projection it computes for the shaders. */

#include <GL/glew.h>

namespace odin::gl {

/* Compiles and links a program of two texts of glsl/ (glsl_texts.hpp),
 * each after its #version line. Aborts with what the driver says if it
 * cannot. `retrievable`: the driver is told glGetProgramBinary will be
 * asked for it. */
GLuint compile_program(const char *vertex, const char *fragment, bool retrievable = false);
/* The location of a uniform the program must have. */
GLint uniform(GLuint program, const char *name);
/* Aborts if OpenGL has an error to tell; `stage` names where. */
void check(const char *stage);
/* Says what went wrong on stderr and aborts. */
[[noreturn]] void fatal(const char *what);

/* A texture of one level read pixel for pixel; a framebuffer that draws
 * to a texture. Each is left bound. */
GLuint new_texture(GLenum format, unsigned w, unsigned h);
GLuint new_framebuffer(GLuint color);

} /* namespace odin::gl */

#endif /* ODIN_GL_COMMON_HPP */
