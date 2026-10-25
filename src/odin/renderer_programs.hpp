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

#ifndef ODIN_RENDERER_PROGRAMS_HPP
#define ODIN_RENDERER_PROGRAMS_HPP

#include <cstdint>
#include <cstdio>
#include <optional>
#include <string>
#include <vector>

namespace odin {

/* How a light of the list is drawn */
enum class LightType : unsigned {
	DIRECTIONAL,
	POSITIONAL,
	SPOT,
	UNDIRECTED
};

/* How it fades with the distance: the type of the 051, or not at all */
enum class LightAttenuation : unsigned {
	LINEAR = 0,
	SQUARE = 1,
	INV_LINEAR = 2,
	INV_SQUARE = 3,
	NONE = 4
};

/* What picks the program of a mesh. Each field is a define of the mesh
 * shaders (renderer_shaders.cpp). */
struct MeshVariant {
	struct Light {
		bool on = false;
		LightType type = LightType::DIRECTIONAL;
		LightAttenuation attenuation = LightAttenuation::LINEAR;
	};

	bool has_texture = false;
	bool has_lighting = false;
	unsigned shading_mode = 0;	/* 881 bits 17:16 */
	bool alpha_mode = false;	/* 881 bit 20 */
	unsigned tex_mode = 0;		/* 0 to 4, from the 2C1 bits 31:29 */
	Light lights[4];
	bool has_fog = false;
	bool has_cue = false;		/* faded by the depth queue */
	bool collect = false;		/* translucent fragments go to the lists */
	bool has_pattern = false;	/* 501 */

	/* The whole of it as one number, and the variant of a number, if
	 * it is the number of one. */
	uint64_t key() const;
	static std::optional<MeshVariant> from_key(uint64_t key);
};

/* The programs a list of programs names, those of a variant MeshVariant
 * knows, in its order; none for a file that is not such a list. Such a file
 * starts with PROGRAM_LIST_HEADER, then has the key of a program a line, in
 * 16 hex digits (board_programs_gl.hpp keeps one). The number of the header
 * goes up when MeshVariant::key changes: the lists made before are then left
 * unread. */
inline constexpr char PROGRAM_LIST_HEADER[] = "odin mesh programs 1\n";
std::vector<uint64_t> read_program_list(const std::string &path);
/* The file, open after its first line, or nullptr if it is not a list */
std::FILE *open_program_list(const std::string &path);

} /* namespace odin */

#endif /* ODIN_RENDERER_PROGRAMS_HPP */
