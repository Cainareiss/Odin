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

/* A display list drawn: its viewports, its polygon types, its meshes. */

#include "odin/renderer.hpp"
#include "odin/renderer_gl.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>

namespace odin {

namespace {

constexpr float INV255 = 1.0f / 255.0f;
constexpr uint32_t NONE = ~0u;

LightAttenuation
attenuation_of(const odin_light_t &light)
{
	if (odin_light_att_type(&light) == 0 &&
	    light.attenuation[0] == 1.0f &&
	    light.attenuation[1] == 1.0f)
		return LightAttenuation::NONE;
	return static_cast<LightAttenuation>(odin_light_att_type(&light));
}

bool
has_place(const odin_light_t &light)
{
	return (light.flags & (ODIN_LIGHT_HAS_DIRECTION | ODIN_LIGHT_HAS_POSITION)) != 0;
}

LightType
type_of(const odin_light_t &light)
{
	const bool has_position = (light.flags & ODIN_LIGHT_HAS_POSITION) != 0;

	if (attenuation_of(light) == LightAttenuation::NONE) {
		/* A parallel light sent with a null direction lights every
		 * side (HEURISTIC, read from the BRAVEFF cabinet: the light
		 * its stages add to every lightset). */
		if ((light.flags & ODIN_LIGHT_HAS_DIRECTION) && light.direction[0] == 0.0f &&
		    light.direction[1] == 0.0f && light.direction[2] == 0.0f)
			return LightType::UNDIRECTED;
		return LightType::DIRECTIONAL;
	}
	if (has_position && (light.flags & ODIN_LIGHT_HAS_SPOT) && (light.flags & ODIN_LIGHT_HAS_CONE))
		return LightType::SPOT;
	if (has_position)
		return LightType::POSITIONAL;
	return LightType::DIRECTIONAL;
}

bool
is_nonnegative(float x)
{
	return std::isfinite(x) && x >= 0.0f;
}

} /* namespace */

/* The texture each texhead of the list was drawn with: a list drawn again
 * takes them back, see check_last_list. */
std::vector<GLuint> &
Renderer::tex_ids()
{
	for (ListTextures &textures : m_list_textures) {
		if (!textures.list)
			textures.list = m_list;
		if (textures.list == m_list)
			return textures.ids;
	}
	fatal("a third display list");
}

/* A depth queue without a colour (621 bit 17) fades the polygons that do not
 * blend into what is behind them (HEURISTIC, read from the PHARRIER cabinet:
 * its demo play loses the far scenery and keeps the mountains, sent as
 * translucent polygons). */
bool
Renderer::is_faded_by_depth(const odin_mesh_t &mesh) const
{
	if (mesh.vp_index >= ODIN_LIST_MAX_VIEWPORTS)
		return false;
	const odin_viewport_t &vp = m_list->vp_list[mesh.vp_index];

	return vp.depth.q_behind && vp.depth.q_type == 0 &&
	       vp.depth.inv_range > 0.0f && vp.depth.near_by_edge > 0.0f &&
	       mesh.polytype != ODIN_POLYTYPE_BACKGROUND &&
	       mesh.polytype != ODIN_POLYTYPE_TRANSLUCENT &&
	       !m_debug[DEBUG_NO_FOG];
}

MeshVariant
Renderer::variant_of(const odin_mesh_t &mesh) const
{
	MeshVariant variant;

	variant.collect = m_pass.collect;
	/* 0 and 16 are every pixel */
	variant.has_pattern = (mesh.pattern & 15) != 0;

	if (mesh.mat_index == NONE) {
		error("no material.");
		return variant;
	}
	if (mesh.tex_index == NONE) {
		error("no texhead.");
		return variant;
	}
	if (mesh.ls_index == NONE) {
		error("no lightset.");
		return variant;
	}
	const odin_material_t &mat = m_list->mat_list[mesh.mat_index];
	const odin_lightset_t &ls = m_list->ls_list[mesh.ls_index];
	const odin_viewport_t &vp = m_list->vp_list[mesh.vp_index];

	variant.has_texture = odin_mat_has_texture(&mat) && !m_debug[DEBUG_NO_TEXTURES];
	variant.has_lighting = odin_lightset_mask(&ls) != 0xF &&
	                       odin_mat_shading_mode(&mat) != 0 &&
	                       !m_debug[DEBUG_NO_LIGHTING];

	/* The X360 port picks the fogged vo3d variant on the 881 z bit alone
	 * (shader option 11, digit 1). */
	variant.has_fog = odin_mat_depth_blend(&mat) && !vp.depth.q_behind &&
	                  !m_debug[DEBUG_NO_FOG];
	variant.has_cue = is_faded_by_depth(mesh);

	/* The alpha mode and the texel mode apply to unlit materials too
	 * (vo3d.x1xxxxx variants). */
	variant.alpha_mode = odin_mat_has_alpha(&mat) != 0;

	/* The 2C1 bits 31:29 pick how the texel combines with the material
	 * colours, numbered as the port's shader variants (8252E368 byte 6,
	 * 825B4340): values 1, 2, 4 and 5 select modes 1 to 4; 3, 6 and 7
	 * keep the previous mode there, plain texturing here (HEURISTIC). */
	if (variant.has_texture) {
		static const uint8_t modes[8] = { 0, 1, 2, 0, 3, 4, 0, 0 };

		variant.tex_mode = modes[odin_tex_texmode(&m_list->tex_list[mesh.tex_index])];
	}

	if (!variant.has_lighting)
		return variant;

	variant.shading_mode = odin_mat_shading_mode(&mat);
	for (unsigned i = 0; i < 4; i++) {
		/* Violated by BRAVEFF, in the intro sequence; it recalls a
		 * lightset at the wrong base, and poof. */
		if (!has_place(ls.lights[i]))
			error("recalling light with no direction nor position!");
		variant.lights[i].on = !(odin_lightset_mask(&ls) & (1u << i));
		variant.lights[i].type = type_of(ls.lights[i]);
		variant.lights[i].attenuation = attenuation_of(ls.lights[i]);
	}
	return variant;
}

/* The program of the mesh, put in use if it is another one. */
void
Renderer::use_program(const odin_mesh_t &mesh)
{
	const MeshVariant variant = variant_of(mesh);
	const uint64_t key = variant.key();

	if (m_program && m_variant_key == key)
		return;

	m_variant = variant;
	m_variant_key = key;
	m_program = &m_programs.get(variant);
	glUseProgram(m_program->id);
}

void
Renderer::upload_viewport(const odin_mesh_t &mesh)
{
	const odin_viewport_t &vp = m_list->vp_list[mesh.vp_index];
	const MeshProgram::Locations &locs = m_program->locs;
	/* The projection plane is clip.f pixels away on the screen for x and
	 * clip.f2 pixels away for y. */
	const float kx = vp.clip.n / vp.clip.f;
	const float ky = vp.clip.n / vp.clip.f2;
	float projection[4][4];

	/* The clip window is cut by the scissor, whatever the program: whole
	 * pixels of the screen of the hardware. */
	if (m_scissor_vp_index != mesh.vp_index) {
		const GLint n = static_cast<GLint>(m_scale);

		m_scissor_vp_index = mesh.vp_index;
		glScissor(static_cast<GLint>(vp.clip.l) * n,
		          (static_cast<GLint>(HEIGHT) - static_cast<GLint>(vp.clip.t)) * n,
		          static_cast<GLsizei>(vp.clip.r - vp.clip.l) * n,
		          static_cast<GLsizei>(vp.clip.t - vp.clip.b) * n);
	}

	if (m_program->held.vp_index == mesh.vp_index)
		return;
	m_program->held.vp_index = mesh.vp_index;

	/* The window may leave the screen: VON4 sends some with a left edge
	 * at -22 or a top one at -4; the scissor cuts them to the screen. */
	if (!is_nonnegative(vp.clip.f) || !is_nonnegative(vp.clip.n))
		error("non-positive viewport clipping planes: %s", odin_viewport_str(&vp));
	if (vp.clip.l >= vp.clip.r || vp.clip.b >= vp.clip.t || vp.clip.n >= vp.clip.f)
		error("inverted viewport clipping planes: %s", odin_viewport_str(&vp));

	if (logging())
		log("vp  = %s", odin_viewport_str(&vp));

	/* The centre and the clip window are screen coordinates, y down: the
	 * view is centred on the offset, reaches the four edges of the
	 * screen and is cut by the window. The driver panels of SGNASCAR
	 * sit 40 lines below the middle and stop under their banner; the
	 * letterboxed robots of von4 fill the lines 39 to 360, as on the
	 * cabinets. The BOOTROM menu has its centre on the top left corner
	 * of the screen. The far plane is put at 1e5 (HEURISTIC). */
	gl::frustum(projection,
	            (0.0f - vp.offset.x) * kx,
	            (static_cast<float>(WIDTH) - vp.offset.x) * kx,
	            (vp.offset.y - static_cast<float>(HEIGHT)) * ky,
	            vp.offset.y * ky,
	            vp.clip.n, 1e5f);
	glUniformMatrix4fv(locs.projection, 1, GL_FALSE, &projection[0][0]);

	if (m_variant.has_cue) {
		/* 621 words without a colour: P = 1 / (end - start),
		 * Q = near / start (PHARRIER library 0C015A3E). */
		const float cue[2] = { vp.clip.n / vp.depth.near_by_edge, vp.depth.inv_range };

		glUniform2fv(locs.fog, 1, cue);
	}

	if (m_variant.has_fog) {
		/* 621 words: P = 1 / (end - start), Q = near / end (von4
		 * library 0C716320); the port fogs linearly from start to end. */
		const float p = vp.depth.inv_range, q = vp.depth.near_by_edge;
		float fog[2] = { 0.0f, 0.0f };
		const float fog_color[3] = {
			static_cast<float>(vp.depth.mask[0]) * INV255,
			static_cast<float>(vp.depth.mask[1]) * INV255,
			static_cast<float>(vp.depth.mask[2]) * INV255
		};

		if (p > 0.0f && q > 0.0f) {
			fog[0] = vp.clip.n / q - 1.0f / p;
			fog[1] = p;
		}
		glUniform2fv(locs.fog, 1, fog);
		glUniform3fv(locs.fog_color, 1, fog_color);
	}
}

/* The modelview of the instance i of the mesh, and the second modelview for
 * the vertices that follow it. The normals go through them too, see the
 * vertex shader. */
void
Renderer::upload_modelview(const odin_mesh_t &mesh, unsigned i)
{
	static const odin_modelview_t identity = { {
		{ 1.0f, 0.0f, 0.0f, 0.0f },
		{ 0.0f, 1.0f, 0.0f, 0.0f },
		{ 0.0f, 0.0f, 1.0f, 0.0f },
		{ 0.0f, 0.0f, 0.0f, 1.0f }
	} };
	const MeshProgram::Locations &locs = m_program->locs;
	MeshProgram::Held &held = m_program->held;
	const bool none = mesh.mv_index == NONE;

	if (!none && held.mv_index == mesh.mv_index + i && held.mv2_index == mesh.mv2_index)
		return;
	held.mv_index = none ? NONE : mesh.mv_index + i;
	held.mv2_index = mesh.mv2_index;

	/* Something is drawn anyway. */
	if (none)
		error("attempting to draw with no modelview!");
	const odin_modelview_t *mv = none ? &identity : &m_list->mv_list[mesh.mv_index + i];

	if (logging())
		log("mv  = [%u+%u] %s", mesh.mv_index, i, odin_modelview_str(mv));
	glUniformMatrix4fv(locs.modelview, 1, GL_FALSE, &mv->mtx[0][0]);

	if (mesh.mv2_index < ODIN_LIST_MAX_MODELVIEWS)
		mv = &m_list->mv_list[mesh.mv2_index];
	glUniformMatrix4fv(locs.modelview2, 1, GL_FALSE, &mv->mtx[0][0]);
}

/* Binds the texture of the mesh on the unit 0. */
void
Renderer::upload_texture(const odin_mesh_t &mesh)
{
	GLuint id;

	if (!m_variant.has_texture)
		return;

	/* The texture of a texhead and its sampling stay bound. */
	if (m_bound_tex_index == mesh.tex_index)
		return;
	m_bound_tex_index = mesh.tex_index;

	const odin_texhead_t &th = m_list->tex_list[mesh.tex_index];

	/* A list drawn again takes the textures it had: the texture RAM may
	 * have changed since. */
	if (m_again) {
		id = tex_ids()[mesh.tex_index];
	} else {
		id = m_textures.get(th);
		tex_ids()[mesh.tex_index] = id;
	}

	glActiveTexture(GL_TEXTURE0 + 0);
	glBindTexture(GL_TEXTURE_2D, id);
	if (id)
		TextureCache::set_sampling(th);
}

void
Renderer::upload_lightset(const odin_mesh_t &mesh)
{
	const odin_lightset_t &ls = m_list->ls_list[mesh.ls_index];
	const MeshProgram::Locations &locs = m_program->locs;
	MeshProgram::Held &held = m_program->held;
	float color[3] = { 0.0f, 0.0f, 0.0f };

	if (!m_variant.has_lighting)
		return;

	/* The ambient colour comes from the viewport. */
	if (held.ls_index == mesh.ls_index && held.ls_vp_index == mesh.vp_index)
		return;
	held.ls_index = mesh.ls_index;
	held.ls_vp_index = mesh.vp_index;

	if (logging())
		log("lightset [%u] mask=%X = %s", mesh.ls_index, odin_lightset_mask(&ls),
		    odin_lightset_str(&ls));

	if (!m_debug[DEBUG_NO_AMBIENT] && mesh.vp_index != NONE) {
		const odin_viewport_t &vp = m_list->vp_list[mesh.vp_index];

		for (unsigned c = 0; c < 3; c++)
			color[c] = static_cast<float>(vp.color.ambient[c]) * INV255;
	}
	glUniform3fv(locs.ambient, 1, color);

	for (unsigned i = 0; i < 4; i++) {
		const odin_light_t &light = ls.lights[i];
		const MeshProgram::Light &at = locs.lights[i];
		const float cone[2] = { light.cos_outer, light.cone_scale };
		float table[16];

		if (odin_lightset_mask(&ls) & (1u << i))
			continue;

		glUniform3fv(at.position, 1, light.position);
		glUniform3fv(at.direction, 1, light.direction);

		for (unsigned c = 0; c < 3; c++)
			color[c] = m_debug[DEBUG_NO_DIFFUSE] ? 0.0f :
			           static_cast<float>(light.diffuse[c]) * INV255;
		glUniform3fv(at.color, 1, color);
		glUniform1f(at.ambient, static_cast<float>(odin_light_ambient(&light)) * (1.0f / 16.0f));
		glUniform2fv(at.extents, 1, light.attenuation);
		glUniform3fv(at.spot, 1, light.spot);
		glUniform2fv(at.cone, 1, cone);
		/* The points of the table: 0x400 is 1.0 */
		for (unsigned j = 0; j < 16; j++)
			table[j] = static_cast<float>(m_list->ls_tables[mesh.ls_index][i][j]) * (1.0f / 1024.0f);
		glUniform1fv(at.table, 16, table);
	}
}

void
Renderer::draw_instance(const odin_mesh_t &mesh, unsigned i)
{
	upload_modelview(mesh, i);
	if (m_pass.collect) {
		glUniform1ui(m_program->locs.order, m_pass.order);
		m_pass.order += mesh.num_tris;
	}
	glDrawArrays(GL_TRIANGLES, static_cast<GLint>(mesh.first),
	             static_cast<GLsizei>(mesh.num_tris * 3));
}

void
Renderer::draw_mesh(const odin_mesh_t &mesh)
{
	/* The 32 patterns of the X360 port (823000D0), a bit a pixel, bit 0
	 * at the top left. */
	static const uint16_t patterns[32] = {
		0xFFFF, 0xFBFE, 0xFAFA, 0x7ADA, 0x5A5A, 0x5258, 0x5050, 0x1040,
		0x0000, 0x0401, 0x0505, 0x8525, 0xA5A5, 0xADA7, 0xAFAF, 0xEFBF,
		0xFFFF, 0xFDF7, 0xF5F5, 0xE5B5, 0xA5A5, 0xA4A1, 0xA0A0, 0x8020,
		0x0000, 0x0208, 0x0A0A, 0x1A4A, 0x5A5A, 0x5B5E, 0x5F5F, 0x7FDF,
	};

	if (logging())
		log_mesh(mesh);

	glBindVertexArray(m_mesh_vao);

	use_program(mesh);
	const MeshProgram::Locations &locs = m_program->locs;
	MeshProgram::Held &held = m_program->held;

	if (held.drawing != m_drawing) {
		/* Nothing of this list yet. */
		held.drawing = m_drawing;
		held.vp_index = held.ls_index = held.ls_vp_index = NONE;
		held.mv_index = held.mv2_index = NONE;
		held.alpha_ref = held.depth_scale = NAN;
		held.weigh_by_alpha = -1;
		held.pattern = NONE;
	}
	if (m_variant.has_pattern && held.pattern != mesh.pattern) {
		held.pattern = mesh.pattern;
		glUniform1ui(locs.pattern, patterns[mesh.pattern]);
	}
	if (held.alpha_ref != m_pass.alpha_ref) {
		held.alpha_ref = m_pass.alpha_ref;
		glUniform1f(locs.alpha_ref, m_pass.alpha_ref);
	}
	if (held.weigh_by_alpha != static_cast<int>(m_pass.weigh_by_alpha)) {
		held.weigh_by_alpha = m_pass.weigh_by_alpha;
		glUniform1f(locs.weigh_by_alpha, m_pass.weigh_by_alpha ? 1.0f : 0.0f);
	}
	if (m_pass.collect)
		glUniform1ui(locs.blend_mode, m_pass.blend_mode);

	upload_viewport(mesh);
	upload_texture(mesh);
	upload_lightset(mesh);

	/* The 101 bias moves the mesh by a fraction of its own depth, nearer
	 * when positive (HEURISTIC, 1/65536 per unit: four times as much and
	 * a light shaft of AIRTRIX covers the skater). It is signed and holds
	 * for the opaque types too: the lettering and the painted lines of
	 * the SGNASCAR track lie on layers of -1024 to -256, the shadow of the
	 * AIRTRIX skater is opaque. */
	const float depth_scale = m_pass.depth_bias ?
		1.0f - mesh.depth_bias * (1.0f / 65536.0f) : 1.0f;
	if (held.depth_scale != depth_scale) {
		held.depth_scale = depth_scale;
		glUniform1f(locs.depth_scale, depth_scale);
	}

	if (m_debug[DEBUG_NO_INSTANCING]) {
		draw_instance(mesh, std::min(static_cast<unsigned>(m_debug[DEBUG_SELECT_INSTANCE]),
		                             mesh.num_instances - 1));
	} else {
		for (unsigned i = 0; i < mesh.num_instances; i++)
			draw_instance(mesh, i);
	}

	glBindVertexArray(0);
	gl::check("a mesh");
}

/* Uploads the vertices of the list, once, before its meshes are drawn. */
void
Renderer::upload_vertices()
{
	const auto attribute = [](GLuint index, GLint size, GLenum type, GLboolean normalized,
	                          size_t offset) {
		glVertexAttribPointer(index, size, type, normalized, sizeof(odin_vertex_body_t),
		                      reinterpret_cast<const void *>(offset));
		glEnableVertexAttribArray(index);
	};

	if (!m_mesh_vao) {
		glGenVertexArrays(1, &m_mesh_vao);
		glGenBuffers(1, &m_mesh_vbo);
	}

	glBindVertexArray(m_mesh_vao);
	glBindBuffer(GL_ARRAY_BUFFER, m_mesh_vbo);
	glBufferData(GL_ARRAY_BUFFER,
	             static_cast<GLsizeiptr>(sizeof(odin_vertex_body_t) * m_list->verts.num),
	             m_list->verts.all, GL_DYNAMIC_DRAW);

	/* The locations of the mesh vertex shader */
	attribute(0, 3, GL_FLOAT, GL_FALSE, offsetof(odin_vertex_body_t, position));
	attribute(1, 3, GL_FLOAT, GL_FALSE, offsetof(odin_vertex_body_t, normal));
	attribute(2, 3, GL_UNSIGNED_BYTE, GL_TRUE, offsetof(odin_vertex_body_t, diffuse));
	attribute(3, 3, GL_FLOAT, GL_FALSE, offsetof(odin_vertex_body_t, ambient));
	attribute(4, 4, GL_UNSIGNED_BYTE, GL_TRUE, offsetof(odin_vertex_body_t, specular));
	attribute(5, 3, GL_UNSIGNED_BYTE, GL_TRUE, offsetof(odin_vertex_body_t, color1));
	attribute(6, 2, GL_FLOAT, GL_FALSE, offsetof(odin_vertex_body_t, texcoords));
	attribute(7, 1, GL_UNSIGNED_BYTE, GL_TRUE, offsetof(odin_vertex_body_t, alpha));
	attribute(8, 1, GL_UNSIGNED_BYTE, GL_TRUE, offsetof(odin_vertex_body_t, second_mv));

	glBindVertexArray(0);
	gl::check("the vertices");
}

/* Blend factors of the material (A81 bits 17:16), as the X360 port
 * applies them (825B4290): source alpha over the destination, the product,
 * source alpha added, the source as it is over the destination. Where the
 * source is weighted by its alpha the shader does it, before the clamp.
 * The translucent type is blended by the shader that sorts its fragments,
 * which does the same. */
void
Renderer::set_material_blend(const odin_mesh_t &mesh)
{
	static const GLenum factors[4][4] = {
		{ GL_ONE,       GL_ONE_MINUS_SRC_ALPHA, GL_ZERO, GL_ONE_MINUS_SRC_ALPHA },
		{ GL_DST_COLOR, GL_ZERO,                GL_ZERO, GL_ZERO },
		{ GL_ONE,       GL_ONE,                 GL_ZERO, GL_ONE_MINUS_SRC_ALPHA },
		{ GL_ONE,       GL_ONE_MINUS_SRC_ALPHA, GL_ZERO, GL_ONE_MINUS_SRC_ALPHA },
	};
	unsigned mode = 0;

	if (mesh.mat_index < ODIN_LIST_MAX_MATERIALS)
		mode = odin_mat_blending_mode(&m_list->mat_list[mesh.mat_index]);
	m_pass.blend_mode = mode;
	m_pass.weigh_by_alpha = (mode == 0 || mode == 2);
	glBlendFuncSeparate(factors[mode][0], factors[mode][1],
	                    factors[mode][2], factors[mode][3]);
}

/* The passes that do not blend replace the pixel and cover what is below the
 * 3D; a mesh faded by the depth queue lets through what it does not hide. */
void
Renderer::set_opaque_blend(const odin_mesh_t &mesh)
{
	if (is_faded_by_depth(mesh))
		glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA,
		                    GL_ZERO, GL_ONE_MINUS_SRC_ALPHA);
	else
		glBlendFuncSeparate(GL_ONE, GL_ZERO, GL_ZERO, GL_ZERO);
}

void
Renderer::draw_polytype(unsigned vpi, unsigned polytype)
{
	const odin_mesh_t *meshes = m_list->mesh_list[vpi][polytype];
	const unsigned num = m_list->num_meshes[vpi][polytype];

	if (num == 0)
		return;
	if (logging())
		log("POLYTYPE %u: %u meshes", polytype, num);

	if (m_debug[DEBUG_SELECT_POLYTYPE] >= 0 &&
	    static_cast<unsigned>(m_debug[DEBUG_SELECT_POLYTYPE]) != polytype)
		return;
	if (logging())
		log(" ==== DRAWING VP %u, POLYTYPE %u ====", vpi, polytype);

	/* Render passes of the X360 port (825B2408, 825B3EB0): opaque and
	 * shadow types write the depth without blending nor alpha test; the
	 * transparent type is punch-through (alpha > 0.5, depth written, no
	 * blending); background and translucent blend with the material mode,
	 * keep the depth and drop alpha <= 1/255.
	 *
	 * The alpha of the color buffer holds what the 3D lets through: the
	 * passes that do not blend write 0 there. */
	glEnable(GL_BLEND);
	glBlendFuncSeparate(GL_ONE, GL_ZERO, GL_ZERO, GL_ZERO);
	m_pass.weigh_by_alpha = false;

	switch (polytype) {
	case ODIN_POLYTYPE_TRANSPARENT:
		glDepthMask(GL_TRUE);
		m_pass.depth_bias = true;
		m_pass.alpha_ref = 0.5f;
		break;
	case ODIN_POLYTYPE_TRANSLUCENT:
		glDepthMask(GL_FALSE);
		m_pass.depth_bias = true;
		m_pass.alpha_ref = 1.0f / 255.0f;
		break;
	case ODIN_POLYTYPE_BACKGROUND:
		glDepthMask(GL_FALSE);
		m_pass.depth_bias = false;
		m_pass.alpha_ref = 1.0f / 255.0f;
		break;
	default:
		glDepthMask(GL_TRUE);
		m_pass.depth_bias = true;
		m_pass.alpha_ref = -1.0f;
		break;
	}

	if (polytype == ODIN_POLYTYPE_TRANSLUCENT) {
		draw_translucent(vpi, meshes, num);
		return;
	}

	for (unsigned j = 0; j < num; j++) {
		if (polytype == ODIN_POLYTYPE_BACKGROUND)
			set_material_blend(meshes[j]);
		else
			set_opaque_blend(meshes[j]);
		draw_mesh(meshes[j]);
	}
}

void
Renderer::draw_scene()
{
	/* The background first, for the depth queue to fade the polygons
	 * into it, then the pass order of the X360 port (825B3EB0), which
	 * draws it after the opaque types: punch-through, opaque, then the
	 * translucent type. The two shadow types are drawn as opaque. */
	static const unsigned order[] = {
		ODIN_POLYTYPE_BACKGROUND,
		ODIN_POLYTYPE_TRANSPARENT,
		ODIN_POLYTYPE_OPAQUE,
		ODIN_POLYTYPE_SHADOW_A,
		ODIN_POLYTYPE_SHADOW_B,
		ODIN_POLYTYPE_TRANSLUCENT,
	};

	if (m_debug[DEBUG_NO_3D])
		return;

	upload_vertices();

	glViewport(0, 0, static_cast<GLsizei>(width()), static_cast<GLsizei>(height()));

	/* The scissor test and the write masks hold for a clear too. An
	 * empty 3D buffer lets everything through: the clear color comes in
	 * on the screen, see compose. */
	glDepthMask(GL_TRUE);
	glDisable(GL_SCISSOR_TEST);
	glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
	glEnable(GL_SCISSOR_TEST);

	glEnable(GL_DEPTH_TEST);
	glDepthFunc(GL_LEQUAL);

	glEnable(GL_CULL_FACE);
	glCullFace(GL_BACK);

	/* Each viewport starts from an empty depth buffer. */
	for (unsigned vpi = 0; vpi < 8; vpi++) {
		glDepthMask(GL_TRUE);
		glDisable(GL_SCISSOR_TEST);
		glClear(GL_DEPTH_BUFFER_BIT);
		glEnable(GL_SCISSOR_TEST);
		for (const unsigned polytype : order)
			draw_polytype(vpi, polytype);
	}

	glDepthMask(GL_TRUE);
	glDisable(GL_SCISSOR_TEST);
	glViewport(0, 0, static_cast<GLsizei>(width()), static_cast<GLsizei>(height()));
	gl::check("a scene");
}

} /* namespace odin */
