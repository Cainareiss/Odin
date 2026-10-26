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

/* The shaders of Vulkan, compiled from glsl/ to SPIR-V when Odin is built
 * (Makefile: glslangValidator), in the binary as they come out:
 * odin_spirv_<name> starts one, odin_spirv_<name>_end follows it
 * (spirv.hpp). */

#if defined(_WIN32)
	.section .rdata, "dr"
#else
	.section .rodata
#endif

#define SPIRV(name, file) \
	.global odin_spirv_##name; .global odin_spirv_##name##_end; .balign 16; \
	odin_spirv_##name: .incbin file; odin_spirv_##name##_end:

/* The board drawing */
SPIRV(whole_picture_vert, "glsl/whole_picture.vert.spv")
SPIRV(blend_frag, "glsl/blend.frag.spv")
SPIRV(compose_vert, "glsl/compose.vert.spv")
SPIRV(compose_frag, "glsl/compose.frag.spv")
SPIRV(present_frag, "glsl/present.frag.spv")
SPIRV(mesh_vert, "glsl/mesh.vert.spv")
SPIRV(mesh_frag, "glsl/mesh.frag.spv")
SPIRV(mesh_collect_frag, "glsl/mesh_collect.frag.spv")
SPIRV(sort_frag, "glsl/sort.frag.spv")

#if !defined(_WIN32)
	.section .note.GNU-stack, "", @progbits
#endif
