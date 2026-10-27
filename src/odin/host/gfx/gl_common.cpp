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

#include "odin/host/gfx/gl_common.hpp"

#include <algorithm>
#include <cstdio>
#include <cstdlib>

namespace odin::gl {

namespace {

/* The shaders of Odin are GLSL 4.30 (glsl/): their texts come after this
 * #version line. */
GLuint
compile_shader(GLenum type, const char *text)
{
	const GLuint id = glCreateShader(type);
	const char *const texts[2] = { fragment_lists() ? "#version 430\n" : "#version 410\n#define PEELING 1\n", text };
	GLint status;

	glShaderSource(id, 2, texts, nullptr);
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
compile_program(const char *vertex, const char *fragment, bool retrievable, const char *geometry)
{
	const GLuint vs = compile_shader(GL_VERTEX_SHADER, vertex);
	const GLuint fs = compile_shader(GL_FRAGMENT_SHADER, fragment);
	const GLuint gs = geometry ? compile_shader(GL_GEOMETRY_SHADER, geometry) : 0;
	const GLuint id = glCreateProgram();
	GLint status;

	glAttachShader(id, vs);
	glAttachShader(id, fs);
	if (gs)
		glAttachShader(id, gs);
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
	if (gs)
		glDeleteShader(gs);
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

void
fatal(const char *what)
{
	std::fprintf(stderr, "renderer: %s\n", what);
	std::abort();
}

bool
fragment_lists()
{
	return GLEW_VERSION_4_3;
}

void
storage(GLsizei levels, GLenum format, GLsizei w, GLsizei h)
{
	if (GLEW_VERSION_4_2 || GLEW_ARB_texture_storage) {
		glTexStorage2D(GL_TEXTURE_2D, levels, format, w, h);
		return;
	}
	const bool depth = format == GL_DEPTH_COMPONENT24 || format == GL_DEPTH_COMPONENT32F;
	const GLenum layout = depth ? GL_DEPTH_COMPONENT : (format == GL_R32UI) ? GL_RED_INTEGER : GL_RGBA;
	const GLenum type = depth ? GL_FLOAT : (format == GL_R32UI) ? GL_UNSIGNED_INT : GL_UNSIGNED_BYTE;

	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, levels - 1);
	for (GLint level = 0; level < levels; level++) {
		glTexImage2D(GL_TEXTURE_2D, level, static_cast<GLint>(format), w, h, 0, layout, type, nullptr);
		w = std::max(w / 2, 1);
		h = std::max(h / 2, 1);
	}
}

GLuint
new_texture(GLenum format, unsigned w, unsigned h)
{
	GLuint id;

	glGenTextures(1, &id);
	glBindTexture(GL_TEXTURE_2D, id);
	storage(1, format, static_cast<GLsizei>(w), static_cast<GLsizei>(h));
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	return id;
}

GLuint
new_framebuffer(GLuint color)
{
	static const GLenum targets[1] = { GL_COLOR_ATTACHMENT0 };
	GLuint id;

	glGenFramebuffers(1, &id);
	glBindFramebuffer(GL_FRAMEBUFFER, id);
	glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, color, 0);
	glDrawBuffers(1, targets);
	if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
		fatal("a framebuffer is not complete");
	return id;
}

} /* namespace odin::gl */
