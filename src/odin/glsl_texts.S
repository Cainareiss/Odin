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

/* The texts of the shaders (glsl/), in the binary as their files are, each
 * ended by a zero: odin_glsl_<name> starts one (glsl_texts.hpp). A shader
 * that builds on fragment_lists.glsl has it put before its own file. */

#if defined(_WIN32)
	.section .rdata, "dr"
#else
	.section .rodata
#endif

#define TEXT(name, file) \
	.global odin_glsl_##name; .balign 16; odin_glsl_##name: .incbin file; .byte 0

#define TEXT_WITH_LISTS(name, file) \
	.global odin_glsl_##name; .balign 16; odin_glsl_##name: .incbin "glsl/fragment_lists.glsl"; .incbin file; .byte 0

/* The board drawing */
TEXT(mesh_vert, "glsl/mesh.vert")
TEXT(mesh_geom, "glsl/mesh.geom")
TEXT_WITH_LISTS(mesh_frag, "glsl/mesh.frag")
TEXT(whole_picture_vert, "glsl/whole_picture.vert")
TEXT_WITH_LISTS(sort_frag, "glsl/sort.frag")
TEXT(blend_frag, "glsl/blend.frag")
TEXT(compose_vert, "glsl/compose.vert")
TEXT(compose_frag, "glsl/compose.frag")
TEXT(present_frag, "glsl/present.frag")

/* The window */
TEXT(image_vert, "glsl/image.vert")
TEXT(image_frag, "glsl/image.frag")
TEXT(whole_window_vert, "glsl/whole_window.vert")
TEXT(blur_frag, "glsl/blur.frag")
TEXT(show_frag, "glsl/show.frag")
TEXT(average_frag, "glsl/average.frag")
TEXT(sides_frag, "glsl/sides.frag")

#if !defined(_WIN32)
	.section .note.GNU-stack, "", @progbits
#endif
