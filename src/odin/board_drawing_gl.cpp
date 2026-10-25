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

/* What the graphics card of the host does for the renderer, with OpenGL
 * (board_drawing.hpp): made, its textures, the stamps of its clock. */

#include "odin/board_drawing_gl.hpp"

#include <vector>

#include "odin/renderer_gl.hpp"

namespace odin {

namespace {

/* The texels of the board as OpenGL keeps them and is given them, by
 * Texels */
struct GlTexels {
	GLenum internal, layout, type;
};
constexpr GlTexels GL_TEXELS[] = {
	{ GL_RGB5_A1, GL_RGBA, GL_UNSIGNED_SHORT_1_5_5_5_REV },
	{ GL_RGBA4, GL_RGBA, GL_UNSIGNED_SHORT_4_4_4_4_REV },
	{ GL_RGBA8, GL_RG, GL_UNSIGNED_BYTE },
};
/* LA8: the light in the three colours */
constexpr GLint LA8_SWIZZLE[4] = { GL_RED, GL_RED, GL_RED, GL_GREEN };

} /* namespace */

std::unique_ptr<BoardDrawing>
BoardDrawing::make_gl()
{
	return std::make_unique<GlBoardDrawing>();
}

/* The texture is left bound on the unit 0: bind_texture binds the one of
 * the next mesh again. */
HostTexture
GlBoardDrawing::make_texture(Texels texels, std::span<const TexelRect> levels)
{
	const GlTexels &gl = GL_TEXELS[static_cast<size_t>(texels)];
	const GLsizei num_levels = static_cast<GLsizei>(levels.size());
	GLuint id;

	glGenTextures(1, &id);
	glActiveTexture(GL_TEXTURE0);
	glBindTexture(GL_TEXTURE_2D, id);
	m_unit0_texture = ~0u;
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, num_levels - 1);
	if (texels == Texels::LA8)
		glTexParameteriv(GL_TEXTURE_2D, GL_TEXTURE_SWIZZLE_RGBA, LA8_SWIZZLE);

	/* The levels are allocated at once: given one by one, a 64x64 texture
	 * takes the NVIDIA driver 595 some 1.7 ms instead of 0.03. */
	glTexStorage2D(GL_TEXTURE_2D, num_levels, gl.internal, static_cast<GLsizei>(levels[0].w),
	               static_cast<GLsizei>(levels[0].h));

	glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
	for (GLsizei level = 0; level < num_levels; level++) {
		const TexelRect &l = levels[static_cast<size_t>(level)];

		glPixelStorei(GL_UNPACK_ROW_LENGTH, static_cast<GLint>(l.row));
		glPixelStorei(GL_UNPACK_SKIP_ROWS, static_cast<GLint>(l.y));
		glPixelStorei(GL_UNPACK_SKIP_PIXELS, static_cast<GLint>(l.x));
		glTexSubImage2D(GL_TEXTURE_2D, level, 0, 0, static_cast<GLsizei>(l.w), static_cast<GLsizei>(l.h),
		                gl.layout, gl.type, l.texels);
	}

	glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
	glPixelStorei(GL_UNPACK_SKIP_ROWS, 0);
	glPixelStorei(GL_UNPACK_SKIP_PIXELS, 0);
	gl::check("making a texture");
	return { id };
}

void
GlBoardDrawing::forget_textures(std::span<const HostTexture> textures)
{
	std::vector<GLuint> ids;

	for (const HostTexture texture : textures)
		ids.push_back(texture.id);
	glDeleteTextures(static_cast<GLsizei>(ids.size()), ids.data());
	gl::check("deleting textures");
}

HostSampler
GlBoardDrawing::make_sampler(const TextureSampling &sampling, unsigned anisotropy)
{
	const auto wrap = [](TextureSampling::Wrap w) -> GLint {
		return w == TextureSampling::Wrap::MIRROR ? GL_MIRRORED_REPEAT :
		       w == TextureSampling::Wrap::REPEAT ? GL_REPEAT : GL_CLAMP_TO_EDGE;
	};
	GLuint id;

	glGenSamplers(1, &id);
	glSamplerParameteri(id, GL_TEXTURE_WRAP_S, wrap(sampling.u));
	glSamplerParameteri(id, GL_TEXTURE_WRAP_T, wrap(sampling.v));
	switch (sampling.filter) {
	case TextureSampling::Filter::NEAREST:
		glSamplerParameteri(id, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
		glSamplerParameteri(id, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
		break;
	case TextureSampling::Filter::LINEAR:
		glSamplerParameteri(id, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
		glSamplerParameteri(id, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
		break;
	case TextureSampling::Filter::TRILINEAR:
		glSamplerParameteri(id, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
		glSamplerParameteri(id, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
		break;
	}
	glSamplerParameterf(id, GL_TEXTURE_LOD_BIAS, sampling.lod_bias);
	if (anisotropy > 1 && GLEW_EXT_texture_filter_anisotropic)
		glSamplerParameterf(id, GL_TEXTURE_MAX_ANISOTROPY_EXT, static_cast<float>(anisotropy));
	return { id };
}

bool
GlBoardDrawing::has_stamps() const
{
	GLint bits = 0;

	glGetQueryiv(GL_TIMESTAMP, GL_QUERY_COUNTER_BITS, &bits);
	return bits > 0;
}

uint32_t
GlBoardDrawing::put_stamp()
{
	GLuint id;

	if (m_stamps.empty()) {
		glGenQueries(1, &id);
	} else {
		id = m_stamps.back();
		m_stamps.pop_back();
	}
	glQueryCounter(id, GL_TIMESTAMP);
	return id;
}

bool
GlBoardDrawing::reached(uint32_t stamp)
{
	GLuint ready = 0;

	glGetQueryObjectuiv(stamp, GL_QUERY_RESULT_AVAILABLE, &ready);
	return ready != 0;
}

uint64_t
GlBoardDrawing::stamp_time(uint32_t stamp)
{
	GLuint64 at = 0;

	glGetQueryObjectui64v(stamp, GL_QUERY_RESULT, &at);
	return at;
}

/* A stamp the card still owes is not put again. */
void
GlBoardDrawing::forget_stamp(uint32_t stamp, bool again)
{
	if (again)
		m_stamps.push_back(stamp);
	else
		glDeleteQueries(1, &stamp);
}

} /* namespace odin */
