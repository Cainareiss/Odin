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
#include <unordered_map>

#include <GL/glew.h>

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

	/* The whole of it as one number */
	uint64_t key() const;
};

/* A compiled program of the mesh shaders */
struct MeshProgram {
	struct Light {
		GLint position, direction, color, ambient;
		GLint extents, spot, cone, table;
	};
	/* The locations of its uniforms, -1 for one the variant leaves out */
	struct Locations {
		GLint projection;
		GLint modelview, normal;
		GLint modelview2, normal2;
		GLint depth_scale;
		Light lights[4];
		GLint ambient;
		GLint fog, fog_color;
		GLint alpha_ref;
		GLint weigh_by_alpha;
		GLint order;
		GLint blend_mode;
		GLint pattern;
		GLint scale;
	};
	/* What it was given last, so as not to give it again: indices in
	 * the display list being drawn, see Renderer::forget_held. */
	struct Held {
		uint32_t drawing = ~0u;
		uint32_t vp_index, ls_index, ls_vp_index;
		uint32_t mv_index, mv2_index;
		float alpha_ref, depth_scale;
		int weigh_by_alpha;
		uint32_t pattern;
	};

	GLuint id;
	Locations locs;
	Held held;
};

/* The programs compiled so far, by variant */
class MeshPrograms {
public:
	/* The texture units the programs read, set once */
	enum : GLint {
		UNIT_TEXTURE = 0,
		UNIT_SOLID_DEPTH = 6
	};

	~MeshPrograms();

	/* The program of a variant, compiled the first time. A program just
	 * compiled is left in use. */
	MeshProgram &get(const MeshVariant &variant);

private:
	static MeshProgram compile(const MeshVariant &variant);

	std::unordered_map<uint64_t, MeshProgram> m_programs;
};

} /* namespace odin */

#endif /* ODIN_RENDERER_PROGRAMS_HPP */
