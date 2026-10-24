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
#include <unordered_map>
#include <vector>

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

	/* The whole of it as one number, and the variant of a number, if
	 * it is the number of one. */
	uint64_t key() const;
	static std::optional<MeshVariant> from_key(uint64_t key);
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
		GLint modelview;
		GLint modelview2;
		GLint depth_scale, sort_scale;
		Light lights[4];
		GLint ambient;
		GLint fog, fog_color;
		GLint alpha_ref;
		GLint weigh_by_alpha;
		GLint order;
		GLint blend_mode;
		GLint pattern;
	};
	/* What it was given last, so as not to give it again: indices in
	 * the display list being drawn, see Renderer::forget_held. */
	struct Held {
		uint32_t drawing = ~0u;
		uint32_t vp_index, ls_index, ls_vp_index;
		uint32_t mv_index, mv2_index;
		float alpha_ref, depth_scale, sort_scale;
		int weigh_by_alpha;
		uint32_t pattern;
	};

	GLuint id;
	Locations locs;
	Held held;
};

/* The programs compiled so far, by variant.
 *
 * The first time a program is needed the graphics driver compiles it, and
 * the first time it draws the driver works on it again (NVIDIA 595: some
 * 20 ms, then 10 on the thread that draws): a stutter each time a game
 * shows something new. So the programs a game draws with are listed in a
 * file, which starts with a line that tells what it is and then has the
 * key of a program a line: listed programs are compiled, and draw once,
 * when the game starts. */
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

	/* The programs a file lists, those of a variant MeshVariant knows, in
	 * its order; none for a file that is not such a list. */
	static std::vector<uint64_t> read_list(const std::string &path);
	bool holds(uint64_t key) const { return m_programs.contains(key); }
	/* Compiles the program of a key read_list gave, unless it is held,
	 * and leaves it in use. */
	void add(uint64_t key);
	/* Lists in a file the programs compiled from now on, after the ones
	 * it lists already. */
	void list_in(const std::string &path);
	/* Keeps each program the driver links in that folder, and takes it
	 * from there the next times (program_cache.hpp), when the driver can
	 * give its programs; empty: none kept. */
	void keep_in(const std::string &folder);
	/* The programs made ready so far: read back from the folder, and
	 * compiled */
	unsigned read_back() const { return m_read_back; }
	unsigned compiled() const { return m_compiled; }

private:
	MeshProgram compile(const MeshVariant &variant);
	GLuint link(uint64_t key, const std::string &vertex, const std::string &fragment);

	std::unordered_map<uint64_t, MeshProgram> m_programs;
	std::FILE *m_list = nullptr;
	std::string m_kept, m_driver;	/* the folder, and the driver the programs are made by */
	unsigned m_read_back = 0, m_compiled = 0;
};

} /* namespace odin */

#endif /* ODIN_RENDERER_PROGRAMS_HPP */
