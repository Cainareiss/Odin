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

/* The pictures the page Display of the menu shows, real captures of Odin
 * (pictures/), in the binary as their PNG files are: odin_picture_<name>
 * starts one, odin_picture_<name>_end follows it (menu.cpp). */

#if defined(_WIN32)
	.section .rdata, "dr"
#else
	.section .rodata
#endif

#define PICTURE(name, file) \
	.global odin_picture_##name; .global odin_picture_##name##_end; .balign 16; \
	odin_picture_##name: .incbin file; odin_picture_##name##_end:

PICTURE(upscale_1, "pictures/upscale-1.png")
PICTURE(upscale_2, "pictures/upscale-2.png")
PICTURE(upscale_3, "pictures/upscale-3.png")
PICTURE(upscale_4, "pictures/upscale-4.png")
PICTURE(upscale_5, "pictures/upscale-5.png")
PICTURE(upscale_6, "pictures/upscale-6.png")
PICTURE(filter_1, "pictures/filter-1.png")
PICTURE(filter_2, "pictures/filter-2.png")
PICTURE(filter_4, "pictures/filter-4.png")
PICTURE(filter_8, "pictures/filter-8.png")
PICTURE(filter_16, "pictures/filter-16.png")
PICTURE(screen, "pictures/screen.png")

#if !defined(_WIN32)
	.section .note.GNU-stack, "", @progbits
#endif
