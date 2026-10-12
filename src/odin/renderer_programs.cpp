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

#include "odin/renderer_programs.hpp"
#include "odin/renderer_gl.hpp"
#include "odin/renderer_shaders.hpp"

#include <cstdio>
#include <string>

namespace odin {

uint64_t
MeshVariant::key() const
{
	uint64_t k = 0;
	unsigned at = 0;
	const auto put = [&](unsigned value, unsigned bits) {
		k |= uint64_t{value} << at;
		at += bits;
	};

	put(has_texture, 1);
	put(has_lighting, 1);
	put(shading_mode, 2);
	put(alpha_mode, 1);
	put(tex_mode, 3);
	for (const Light &light : lights) {
		put(light.on, 1);
		put(static_cast<unsigned>(light.type), 2);
		put(static_cast<unsigned>(light.attenuation), 3);
	}
	put(has_fog, 1);
	put(has_cue, 1);
	put(collect, 1);
	put(has_pattern, 1);
	return k;
}

MeshPrograms::~MeshPrograms()
{
	glUseProgram(0);
	for (const auto &[key, program] : m_programs)
		glDeleteProgram(program.id);
}

MeshProgram &
MeshPrograms::get(const MeshVariant &variant)
{
	const uint64_t key = variant.key();
	const auto held = m_programs.find(key);

	if (held != m_programs.end())
		return held->second;
	return m_programs.emplace(key, compile(variant)).first->second;
}

MeshProgram
MeshPrograms::compile(const MeshVariant &variant)
{
	/* The pass that collects the fragments writes to buffers. */
	std::string defines = variant.collect ? "#version 430\n" : "#version 140\n";
	const auto define = [&](const char *name, unsigned value) {
		defines += "#define ";
		defines += name;
		defines += ' ';
		defines += std::to_string(value);
		defines += '\n';
	};

	define("HAS_TEXTURE", variant.has_texture);
	define("HAS_LIGHTING", variant.has_lighting);
	define("SHADING_MODE", variant.shading_mode);
	define("ALPHA_MODE", variant.alpha_mode);
	define("TEX_MODE", variant.tex_mode);
	for (unsigned i = 0; i < 4; i++) {
		const MeshVariant::Light &light = variant.lights[i];
		const std::string n = std::to_string(i);

		define(("HAS_LIGHT" + n).c_str(), light.on);
		define(("LIGHT" + n + "_TYPE").c_str(), static_cast<unsigned>(light.type));
		define(("LIGHT" + n + "_ATT_TYPE").c_str(), static_cast<unsigned>(light.attenuation));
	}
	define("HAS_FOG", variant.has_fog);
	define("HAS_CUE", variant.has_cue);
	define("COLLECT", variant.collect);
	define("HAS_PATTERN", variant.has_pattern);

	MeshProgram program;
	program.id = gl::compile_program((defines + shaders::MESH_VERTEX).c_str(),
	                                 (defines + shaders::MESH_FRAGMENT).c_str());

	const auto find = [&](const char *name) {
		return glGetUniformLocation(program.id, name);
	};
	MeshProgram::Locations &locs = program.locs;

	locs.projection = find("u_projection");
	locs.modelview = find("u_modelview");
	locs.normal = find("u_normal");
	locs.modelview2 = find("u_modelview2");
	locs.normal2 = find("u_normal2");
	locs.depth_scale = find("u_depth_scale");
	for (unsigned i = 0; i < 4; i++) {
		const std::string light = "u_lights[" + std::to_string(i) + "].";

		locs.lights[i].position = find((light + "position").c_str());
		locs.lights[i].direction = find((light + "direction").c_str());
		locs.lights[i].color = find((light + "color").c_str());
		locs.lights[i].ambient = find((light + "ambient").c_str());
		locs.lights[i].extents = find((light + "extents").c_str());
		locs.lights[i].spot = find((light + "spot").c_str());
		locs.lights[i].cone = find((light + "cone").c_str());
		locs.lights[i].table = find((light + "table").c_str());
	}
	locs.ambient = find("u_ambient");
	locs.fog = find("u_fog");
	locs.fog_color = find("u_fog_color");
	locs.alpha_ref = find("u_alpha_ref");
	locs.weigh_by_alpha = find("u_weigh_by_alpha");
	locs.order = find("u_order");
	locs.blend_mode = find("u_blend_mode");
	locs.pattern = find("u_pattern");
	locs.scale = find("u_scale");

	/* The texture units never change. */
	glUseProgram(program.id);
	glUniform1i(find("u_texture"), UNIT_TEXTURE);
	glUniform1i(find("u_solid_depth"), UNIT_SOLID_DEPTH);
	gl::check("a mesh program");

	return program;
}

} /* namespace odin */
