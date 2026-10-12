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
compile_program(const char *vertex, const char *fragment)
{
	const GLuint vs = compile_shader(GL_VERTEX_SHADER, vertex);
	const GLuint fs = compile_shader(GL_FRAGMENT_SHADER, fragment);
	const GLuint id = glCreateProgram();
	GLint status;

	glAttachShader(id, vs);
	glAttachShader(id, fs);
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

/* Taken from Mesa. The sums stay in this order: the pictures depend on the
 * last bit of each. */
bool
normal_matrix(float out[3][3], const float in[4][4])
{
	const float *s = &in[0][0];
	float inv[16], det;

	inv[0] = s[5]  * s[10] * s[15] -
	           s[5]  * s[11] * s[14] -
	           s[9]  * s[6]  * s[15] +
	           s[9]  * s[7]  * s[14] +
	           s[13] * s[6]  * s[11] -
	           s[13] * s[7]  * s[10];

	inv[4] = -s[4]  * s[10] * s[15] +
	            s[4]  * s[11] * s[14] +
	            s[8]  * s[6]  * s[15] -
	            s[8]  * s[7]  * s[14] -
	            s[12] * s[6]  * s[11] +
	            s[12] * s[7]  * s[10];

	inv[8] = s[4]  * s[9] * s[15] -
	           s[4]  * s[11] * s[13] -
	           s[8]  * s[5] * s[15] +
	           s[8]  * s[7] * s[13] +
	           s[12] * s[5] * s[11] -
	           s[12] * s[7] * s[9];

	inv[12] = -s[4]  * s[9] * s[14] +
	             s[4]  * s[10] * s[13] +
	             s[8]  * s[5] * s[14] -
	             s[8]  * s[6] * s[13] -
	             s[12] * s[5] * s[10] +
	             s[12] * s[6] * s[9];

	inv[1] = -s[1]  * s[10] * s[15] +
	            s[1]  * s[11] * s[14] +
	            s[9]  * s[2] * s[15] -
	            s[9]  * s[3] * s[14] -
	            s[13] * s[2] * s[11] +
	            s[13] * s[3] * s[10];

	inv[5] = s[0]  * s[10] * s[15] -
	           s[0]  * s[11] * s[14] -
	           s[8]  * s[2] * s[15] +
	           s[8]  * s[3] * s[14] +
	           s[12] * s[2] * s[11] -
	           s[12] * s[3] * s[10];

	inv[9] = -s[0]  * s[9] * s[15] +
	            s[0]  * s[11] * s[13] +
	            s[8]  * s[1] * s[15] -
	            s[8]  * s[3] * s[13] -
	            s[12] * s[1] * s[11] +
	            s[12] * s[3] * s[9];

	inv[13] = s[0]  * s[9] * s[14] -
	            s[0]  * s[10] * s[13] -
	            s[8]  * s[1] * s[14] +
	            s[8]  * s[2] * s[13] +
	            s[12] * s[1] * s[10] -
	            s[12] * s[2] * s[9];

	inv[2] = s[1]  * s[6] * s[15] -
	           s[1]  * s[7] * s[14] -
	           s[5]  * s[2] * s[15] +
	           s[5]  * s[3] * s[14] +
	           s[13] * s[2] * s[7] -
	           s[13] * s[3] * s[6];

	inv[6] = -s[0]  * s[6] * s[15] +
	            s[0]  * s[7] * s[14] +
	            s[4]  * s[2] * s[15] -
	            s[4]  * s[3] * s[14] -
	            s[12] * s[2] * s[7] +
	            s[12] * s[3] * s[6];

	inv[10] = s[0]  * s[5] * s[15] -
	            s[0]  * s[7] * s[13] -
	            s[4]  * s[1] * s[15] +
	            s[4]  * s[3] * s[13] +
	            s[12] * s[1] * s[7] -
	            s[12] * s[3] * s[5];

	inv[14] = -s[0]  * s[5] * s[14] +
	             s[0]  * s[6] * s[13] +
	             s[4]  * s[1] * s[14] -
	             s[4]  * s[2] * s[13] -
	             s[12] * s[1] * s[6] +
	             s[12] * s[2] * s[5];

	inv[3] = -s[1] * s[6] * s[11] +
	            s[1] * s[7] * s[10] +
	            s[5] * s[2] * s[11] -
	            s[5] * s[3] * s[10] -
	            s[9] * s[2] * s[7] +
	            s[9] * s[3] * s[6];

	inv[7] = s[0] * s[6] * s[11] -
	           s[0] * s[7] * s[10] -
	           s[4] * s[2] * s[11] +
	           s[4] * s[3] * s[10] +
	           s[8] * s[2] * s[7] -
	           s[8] * s[3] * s[6];

	inv[11] = -s[0] * s[5] * s[11] +
	             s[0] * s[7] * s[9] +
	             s[4] * s[1] * s[11] -
	             s[4] * s[3] * s[9] -
	             s[8] * s[1] * s[7] +
	             s[8] * s[3] * s[5];

	inv[15] = s[0] * s[5] * s[10] -
	            s[0] * s[6] * s[9] -
	            s[4] * s[1] * s[10] +
	            s[4] * s[2] * s[9] +
	            s[8] * s[1] * s[6] -
	            s[8] * s[2] * s[5];

	det = s[0] * inv[0] +
	      s[1] * inv[4] +
	      s[2] * inv[8] +
	      s[3] * inv[12];
	if (det == 0.0)
		return false;

	for (unsigned i = 0; i < 3; i++)
		for (unsigned j = 0; j < 3; j++)
			out[i][j] = inv[j * 4 + i] * (1.0f / det);

	return true;
}

} /* namespace odin::gl */
