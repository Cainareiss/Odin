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

#include "odin/renderer_gl.hpp"

#include <cstdio>
#include <cstdlib>

namespace odin::gl {

namespace {

GLuint
compile_shader(GLenum type, const char *text)
{
	const GLuint id = glCreateShader(type);
	GLint status;

	glShaderSource(id, 1, &text, nullptr);
	glCompileShader(id);
	glGetShaderiv(id, GL_COMPILE_STATUS, &status);
	if (!status) {
		char info[256];

		glGetShaderInfoLog(id, sizeof(info), nullptr, info);
		std::fprintf(stderr, "renderer: could not compile a shader: '%s'\n%s\n", info, text);
		std::abort();
	}
	return id;
}

} /* namespace */

GLuint
compile_program(const char *vertex, const char *fragment, bool retrievable)
{
	const GLuint vs = compile_shader(GL_VERTEX_SHADER, vertex);
	const GLuint fs = compile_shader(GL_FRAGMENT_SHADER, fragment);
	const GLuint id = glCreateProgram();
	GLint status;

	glAttachShader(id, vs);
	glAttachShader(id, fs);
	if (retrievable)
		glProgramParameteri(id, GL_PROGRAM_BINARY_RETRIEVABLE_HINT, GL_TRUE);
	glLinkProgram(id);
	glGetProgramiv(id, GL_LINK_STATUS, &status);
	if (status == GL_FALSE) {
		char info[256];

		glGetProgramInfoLog(id, sizeof(info), nullptr, info);
		std::fprintf(stderr, "renderer: could not link a program: '%s'\n%s\n%s\n",
		             info, vertex, fragment);
		std::abort();
	}

	/* The program keeps its shaders until it goes. */
	glDeleteShader(vs);
	glDeleteShader(fs);
	check("a program");
	return id;
}

GLint
uniform(GLuint program, const char *name)
{
	const GLint location = glGetUniformLocation(program, name);

	if (location < 0) {
		std::fprintf(stderr, "renderer: the uniform '%s' has no location\n", name);
		std::abort();
	}
	return location;
}

void
check(const char *stage)
{
	const GLenum error = glGetError();
	const char *what;

	if (error == GL_NO_ERROR)
		return;
	switch (error) {
	case GL_INVALID_ENUM:			what = "invalid enum"; break;
	case GL_INVALID_VALUE:			what = "invalid value"; break;
	case GL_INVALID_OPERATION:		what = "invalid operation"; break;
	case GL_INVALID_FRAMEBUFFER_OPERATION:	what = "invalid framebuffer operation"; break;
	case GL_OUT_OF_MEMORY:			what = "out of memory"; break;
	default:				what = "unknown error"; break;
	}
	std::fprintf(stderr, "renderer: OpenGL error (%s) after %s\n", what, stage);
	std::abort();
}

/* Taken from Mesa */
void
frustum(float out[4][4], float l, float r, float b, float t, float n, float f)
{
	out[0][0] = (2.0f * n) / (r - l);
	out[1][0] = 0.0f;
	out[2][0] = (r + l) / (r - l);
	out[3][0] = 0.0f;

	out[0][1] = 0.0f;
	out[1][1] = (2.0f * n) / (t - b);
	out[2][1] = (t + b) / (t - b);
	out[3][1] = 0.0f;

	out[0][2] = 0.0f;
	out[1][2] = 0.0f;
	out[2][2] = - (f + n) / (f - n);
	out[3][2] = - (2.0f * f * n) / (f - n);

	out[0][3] = 0.0f;
	out[1][3] = 0.0f;
	out[2][3] = -1.0f;
	out[3][3] = 0.0f;
}

} /* namespace odin::gl */
