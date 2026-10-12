/* 
 * Valkyrie
 * Copyright (C) 2011, Stefano Teso
 * 
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 * 
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 * 02110-1301, USA.
 */

#include "vk/renderer.h"

/* TODO change API to avoid leaking the SDL window and GL context. */

void
vk_renderer_clear_gl_errors (void)
{
	while (glGetError () != GL_NO_ERROR);
}

void
vk_renderer_begin_frame (vk_renderer_t *renderer)
{
	renderer->message[0] = '\0';
	if (renderer->begin_frame)
		renderer->begin_frame (renderer);
}

/* VK_SCREENSHOT=<prefix> writes <prefix>-<frame>.ppm every 60 frames, or
 * every VK_SCREENSHOT_EVERY frames */
static void
vk_renderer_screenshot (vk_renderer_t *renderer)
{
	static const char *prefix = NULL;
	static unsigned every = 60;
	static bool checked = false;
	char path[512];
	unsigned w = renderer->width * renderer->scale;
	unsigned h = renderer->height * renderer->scale, y;
	uint8_t *pixels;
	FILE *fp;

	if (!checked) {
		const char *env = getenv ("VK_SCREENSHOT_EVERY");
		prefix = getenv ("VK_SCREENSHOT");
		if (env && atoi (env) > 0)
			every = atoi (env);
		checked = true;
	}
	if (!prefix || (renderer->frame % every) != 0)
		return;

	pixels = malloc (w * h * 3);
	if (!pixels)
		return;
	glBindFramebuffer (GL_READ_FRAMEBUFFER, 0);
	glReadBuffer (GL_BACK);
	glPixelStorei (GL_PACK_ALIGNMENT, 1);
	glReadPixels (0, 0, (GLsizei) w, (GLsizei) h, GL_RGB, GL_UNSIGNED_BYTE, pixels);

	snprintf (path, sizeof (path), "%s-%06u.ppm", prefix, renderer->frame);
	fp = fopen (path, "wb");
	if (fp) {
		fprintf (fp, "P6\n%u %u\n255\n", w, h);
		for (y = 0; y < h; y++)
			fwrite (pixels + (h - 1 - y) * w * 3, 1, w * 3, fp);
		fclose (fp);
	}
	free (pixels);
}

/* ODIN_PICTURE_FP=1: one line for the picture of each frame, FNV-1a 64 of
 * its pixels, to compare two builds. */
static void
vk_renderer_print_fingerprint (vk_renderer_t *renderer)
{
	static int wanted = -1;
	static uint8_t *pixels;
	static size_t held;
	const unsigned w = renderer->width * renderer->scale;
	const unsigned h = renderer->height * renderer->scale;
	const size_t size = (size_t) w * h * 3;
	uint64_t fp = 0xCBF29CE484222325ull;
	size_t i;

	if (wanted < 0)
		wanted = vk_util_get_bool_option ("ODIN_PICTURE_FP", false) ? 1 : 0;
	if (!wanted)
		return;

	if (held != size) {
		pixels = (uint8_t *) realloc (pixels, size);
		VK_ASSERT (pixels);
		held = size;
	}
	glBindFramebuffer (GL_READ_FRAMEBUFFER, 0);
	glReadBuffer (GL_BACK);
	glPixelStorei (GL_PACK_ALIGNMENT, 1);
	glReadPixels (0, 0, (GLsizei) w, (GLsizei) h, GL_RGB, GL_UNSIGNED_BYTE, pixels);

	for (i = 0; i < size; i++) {
		fp ^= pixels[i];
		fp *= 0x100000001B3ull;
	}
	fprintf (stderr, "PICFP frame=%u fp=%016lx\n", renderer->frame, fp);
}

/* ODIN_SPEED=1: one line every 60 frames with the frames a second over
 * them, to find the slow scenes of a run. */
static void
vk_renderer_print_speed (vk_renderer_t *renderer)
{
	static int wanted = -1;
	static Uint64 since;
	Uint64 now;

	if (wanted < 0)
		wanted = vk_util_get_bool_option ("ODIN_SPEED", false) ? 1 : 0;
	if (!wanted || (renderer->frame % 60) != 0)
		return;

	now = SDL_GetPerformanceCounter ();
	if (since)
		fprintf (stderr, "SPEED frame=%u fps=%.1f\n", renderer->frame,
		         60.0 * (double) SDL_GetPerformanceFrequency () /
		         (double) (now - since));
	since = now;
}

/* The title of the window tells the frames shown in the last second: the
 * time of one frame, in milliseconds, reads 59 or 62 for 60. */
static void
show_speed (vk_renderer_t *renderer)
{
	static Uint64 since;
	static unsigned frames;
	const Uint64 second = SDL_GetPerformanceFrequency ();
	const Uint64 now = SDL_GetPerformanceCounter ();
	char title[256];

	frames++;
	if (since && now - since < second)
		return;

	/* Nothing to tell at the start nor after a pause */
	if (since && now - since < 2 * second) {
		const double fps = (double) frames * (double) second /
		                   (double) (now - since);

		if (renderer->message[0])
			snprintf (title, sizeof (title), "Odin (%4.1f FPS) [%s]",
			          fps, renderer->message);
		else
			snprintf (title, sizeof (title), "Odin (%4.1f FPS)", fps);
		SDL_SetWindowTitle (renderer->window, title);
	}
	since = now;
	frames = 0;
}

void
vk_renderer_end_frame (vk_renderer_t *renderer)
{
	VK_ASSERT (renderer);

	if (renderer->end_frame)
		renderer->end_frame (renderer);
	vk_renderer_screenshot (renderer);
	vk_renderer_print_fingerprint (renderer);
	SDL_GL_SwapWindow (renderer->window);

	show_speed (renderer);
	vk_renderer_print_speed (renderer);
}

int
vk_renderer_init (vk_renderer_t *renderer)
{
	int num_ms_buffers, num_samples;

	VK_ASSERT (renderer);
	VK_ASSERT (renderer->width);
	VK_ASSERT (renderer->height);

	if (SDL_Init (SDL_INIT_VIDEO | SDL_INIT_TIMER)) {
		VK_ERROR ("could not initialize SDL: '%s'", SDL_GetError ());
		return -1;
	}

	SDL_GL_SetAttribute (SDL_GL_RED_SIZE,   8);
	SDL_GL_SetAttribute (SDL_GL_GREEN_SIZE, 8);
	SDL_GL_SetAttribute (SDL_GL_BLUE_SIZE,  8);
	SDL_GL_SetAttribute (SDL_GL_ALPHA_SIZE, 8);
	SDL_GL_SetAttribute (SDL_GL_BUFFER_SIZE, 0);
	SDL_GL_SetAttribute (SDL_GL_DOUBLEBUFFER, 1);
	SDL_GL_SetAttribute (SDL_GL_DEPTH_SIZE, 24);
	SDL_GL_SetAttribute (SDL_GL_STENCIL_SIZE, 0);
	SDL_GL_SetAttribute (SDL_GL_ACCUM_RED_SIZE, 0);
	SDL_GL_SetAttribute (SDL_GL_ACCUM_GREEN_SIZE, 0);
	SDL_GL_SetAttribute (SDL_GL_ACCUM_BLUE_SIZE, 0);
	SDL_GL_SetAttribute (SDL_GL_ACCUM_ALPHA_SIZE, 0);
	SDL_GL_SetAttribute (SDL_GL_STEREO, 0);
	SDL_GL_SetAttribute (SDL_GL_MULTISAMPLEBUFFERS, 0);
	SDL_GL_SetAttribute (SDL_GL_MULTISAMPLESAMPLES, 0);
	SDL_GL_SetAttribute (SDL_GL_ACCELERATED_VISUAL, 1);
	SDL_GL_SetAttribute (SDL_GL_CONTEXT_MAJOR_VERSION, 4);
	SDL_GL_SetAttribute (SDL_GL_CONTEXT_MINOR_VERSION, 3);
	SDL_GL_SetAttribute (SDL_GL_CONTEXT_FLAGS,
	                     SDL_GL_CONTEXT_FORWARD_COMPATIBLE_FLAG);
	SDL_GL_SetAttribute (SDL_GL_CONTEXT_PROFILE_MASK,
	                     SDL_GL_CONTEXT_PROFILE_CORE);

	renderer->scale = 1;
	renderer->window = SDL_CreateWindow ("Odin",
	                                     SDL_WINDOWPOS_CENTERED,
	                                     SDL_WINDOWPOS_CENTERED,
	                                     renderer->width, renderer->height,
	                                     SDL_WINDOW_OPENGL);
	if (!renderer->window) {
		VK_ERROR ("could not create SDL window: '%s'", SDL_GetError ());
		return -1;
	}

	renderer->gl_context = SDL_GL_CreateContext (renderer->window);
	if (!renderer->gl_context) {
		VK_ERROR ("could not create GL context: '%s'", SDL_GetError ());
		return -1;
	}

	SDL_GL_SetSwapInterval (0);

	glewExperimental = GL_TRUE;
	if (glewInit () != GLEW_OK) {
		VK_ERROR ("could not initialize glew");
		return -1;
	}

	/* Attempt to force multisampling off. */
	glDisable (GL_MULTISAMPLE);

	SDL_GL_GetAttribute (SDL_GL_MULTISAMPLEBUFFERS, &num_ms_buffers);
	SDL_GL_GetAttribute (SDL_GL_MULTISAMPLESAMPLES, &num_samples);

	VK_PRINT ("renderer: GL vendor    = %s", glGetString (GL_VENDOR));
	VK_PRINT ("renderer: GL renderer  = %s", glGetString (GL_RENDERER));
	VK_PRINT ("renderer: GL version   = %s", glGetString (GL_VERSION));
	VK_PRINT ("renderer: GLSL version = %s", glGetString (GL_SHADING_LANGUAGE_VERSION));
	VK_PRINT ("renderer: %d samples on %d ms buffers", num_samples, num_ms_buffers);

	glViewport (0, 0, renderer->width, renderer->height);

	/* The SDL initialization sequence does not clear the (harmless,
	 * hopefully) GL errors it may cause. Let's do it here so we can
	 * catch more serious errors later. */
	vk_renderer_clear_gl_errors ();

	return 0;
}

void
vk_renderer_set_scale (vk_renderer_t *renderer, unsigned scale)
{
	renderer->scale = scale;
	SDL_SetWindowSize (renderer->window, (int) (renderer->width * scale),
	                   (int) (renderer->height * scale));
	SDL_SetWindowPosition (renderer->window, SDL_WINDOWPOS_CENTERED,
	                       SDL_WINDOWPOS_CENTERED);
}
