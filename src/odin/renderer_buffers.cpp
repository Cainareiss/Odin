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

/* The buffers the 3D is drawn in and the pictures kept from one frame to
 * the next: `scale` times the screen of the hardware each way. */

#include "odin/renderer.hpp"
#include "odin/renderer_gl.hpp"

namespace odin {

/* A texture read pixel for pixel. It is left bound. */
GLuint
Renderer::new_buffer_texture(GLenum format, unsigned w, unsigned h)
{
	GLuint id;

	glGenTextures(1, &id);
	glBindTexture(GL_TEXTURE_2D, id);
	glTexStorage2D(GL_TEXTURE_2D, 1, format, static_cast<GLsizei>(w), static_cast<GLsizei>(h));
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	return id;
}

/* A framebuffer that draws to a texture. It is left bound. */
GLuint
Renderer::new_framebuffer(GLuint color)
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

void
Renderer::build_buffers()
{
	const unsigned w = width(), h = height();

	/* Where the lists are drawn, and a copy of its depth */
	m_scene_color = new_buffer_texture(GL_RGBA8, w, h);
	m_scene_fb = new_framebuffer(m_scene_color);
	glGenRenderbuffers(1, &m_scene_depth);
	glBindRenderbuffer(GL_RENDERBUFFER, m_scene_depth);
	glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT,
	                      static_cast<GLsizei>(w), static_cast<GLsizei>(h));
	glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT,
	                          GL_RENDERBUFFER, m_scene_depth);
	if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
		fatal("the framebuffer of the lists is not complete");
	glBindFramebuffer(GL_FRAMEBUFFER, 0);
	m_solid_depth = new_buffer_texture(GL_DEPTH_COMPONENT24, w, h);

	/* The two 3D buffers and the pictures of the last three lists,
	 * empty: an empty 3D buffer lets everything through, its alpha is
	 * 1. */
	{
		std::vector<uint8_t> empty(size_t{w} * h * 4, 0);

		for (size_t i = 3; i < empty.size(); i += 4)
			empty[i] = 0xFF;
		glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
		for (unsigned i = 0; i < PICTURES; i++) {
			picture(i) = new_buffer_texture(GL_RGBA8, w, h);
			glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0,
			                static_cast<GLsizei>(w), static_cast<GLsizei>(h),
			                GL_RGBA, GL_UNSIGNED_BYTE, empty.data());
		}
	}
	/* The blender of the 3D buffers writes to the pictures. */
	for (unsigned i = 0; i < 3; i++)
		m_pipe_fb[i] = new_framebuffer(m_pipe[i]);

	/* The picture of the frame */
	m_frame_color = new_buffer_texture(GL_RGBA8, w, h);
	m_frame_fb = new_framebuffer(m_frame_color);

	/* The translucent fragments: the last one of each pixel, and the
	 * colour under them */
	m_lists.heads = new_buffer_texture(GL_R32UI, w, h);
	m_lists.heads_fb = new_framebuffer(m_lists.heads);
	m_lists.below = new_buffer_texture(GL_RGBA8, w, h);

	glBindFramebuffer(GL_FRAMEBUFFER, 0);
	glBindTexture(GL_TEXTURE_2D, 0);
	gl::check("the buffers");
}

void
Renderer::destroy_buffers(bool pictures_too)
{
	glBindFramebuffer(GL_FRAMEBUFFER, 0);
	glDeleteFramebuffers(1, &m_scene_fb);
	glDeleteTextures(1, &m_scene_color);
	glDeleteRenderbuffers(1, &m_scene_depth);
	glDeleteTextures(1, &m_solid_depth);
	glDeleteFramebuffers(3, m_pipe_fb);
	glDeleteFramebuffers(1, &m_frame_fb);
	glDeleteTextures(1, &m_frame_color);
	glDeleteFramebuffers(1, &m_lists.heads_fb);
	glDeleteTextures(1, &m_lists.heads);
	glDeleteTextures(1, &m_lists.below);
	if (pictures_too) {
		glDeleteTextures(2, m_buffers);
		glDeleteTextures(3, m_pipe);
	}
}

/* Copies a picture into another of any size: each pixel takes the nearest
 * one. */
void
Renderer::copy_picture(GLuint from, unsigned from_w, unsigned from_h,
                       GLuint to, unsigned to_w, unsigned to_h)
{
	glBindFramebuffer(GL_READ_FRAMEBUFFER, m_copy_fb[0]);
	glFramebufferTexture2D(GL_READ_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, from, 0);
	glBindFramebuffer(GL_DRAW_FRAMEBUFFER, m_copy_fb[1]);
	glFramebufferTexture2D(GL_DRAW_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, to, 0);

	/* The scissor would cut the copy. */
	glDisable(GL_SCISSOR_TEST);
	glBlitFramebuffer(0, 0, static_cast<GLint>(from_w), static_cast<GLint>(from_h),
	                  0, 0, static_cast<GLint>(to_w), static_cast<GLint>(to_h),
	                  GL_COLOR_BUFFER_BIT, GL_NEAREST);

	glFramebufferTexture2D(GL_READ_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, 0, 0);
	glFramebufferTexture2D(GL_DRAW_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, 0, 0);
	glBindFramebuffer(GL_FRAMEBUFFER, 0);
	gl::check("copying a picture");
}

void
Renderer::set_scale(unsigned scale)
{
	if (scale == m_scale)
		return;
	if (scale < 1)
		fatal("a scale of zero");

	/* The pictures are whole once the last list is counted. */
	check_last_list();

	const unsigned old_w = width(), old_h = height();
	GLuint old[PICTURES];

	for (unsigned i = 0; i < PICTURES; i++)
		old[i] = picture(i);
	destroy_buffers(false);

	m_scale = scale;
	build_buffers();
	for (unsigned i = 0; i < PICTURES; i++)
		copy_picture(old[i], old_w, old_h, picture(i), width(), height());
	glDeleteTextures(PICTURES, old);

	size_lists(room_at_start());
	forget_held();
}

} /* namespace odin */
