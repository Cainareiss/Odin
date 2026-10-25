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
#include "odin/depth_bias.hpp"
#include "odin/renderer_gl.hpp"

#include <cmath>
#include <cstddef>

namespace odin {

namespace {

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
	fatal("a fourth display list");
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
	float projection[4][4];

	/* The clip window and the winding of the view (view_turned_over,
	 * hikaru_draw.hpp), whatever the program; OpenGL counts the lines from
	 * the bottom. */
	if (m_scissor_vp_index != mesh.vp_index) {
		const GLint n = static_cast<GLint>(m_scale);

		m_scissor_vp_index = mesh.vp_index;
		glScissor(static_cast<GLint>(vp.clip.l) * n,
		          (static_cast<GLint>(HEIGHT) - static_cast<GLint>(vp.clip.t)) * n,
		          static_cast<GLsizei>(vp.clip.r - vp.clip.l) * n,
		          static_cast<GLsizei>(vp.clip.t - vp.clip.b) * n);
		glFrontFace(view_turned_over(vp) ? GL_CW : GL_CCW);
	}

	if (m_program->held.vp_index == mesh.vp_index)
		return;
	m_program->held.vp_index = mesh.vp_index;

	/* The window may leave the screen: VON4 sends some with a left edge
	 * at -22 or a top one at -4; the scissor cuts them to the screen. */
	if (!std::isfinite(vp.clip.f) || !is_nonnegative(vp.clip.n))
		error("non-positive viewport clipping planes: %s", odin_viewport_str(&vp));
	if (vp.clip.l >= vp.clip.r || vp.clip.b >= vp.clip.t || vp.clip.n >= std::fabs(vp.clip.f))
		error("inverted viewport clipping planes: %s", odin_viewport_str(&vp));

	if (logging())
		log("vp  = %s", odin_viewport_str(&vp));

	/* The centre and the clip window are screen coordinates, y down: the
	 * view is centred on the offset, reaches the four edges of the
	 * screen and is cut by the window. The driver panels of SGNASCAR
	 * sit 40 lines below the middle and stop under their banner; the
	 * letterboxed robots of von4 fill the lines 39 to 360, as on the
	 * cabinets. The BOOTROM menu has its centre on the top left corner
	 * of the screen. The far plane is put at 1e5 (HEURISTIC). The
	 * projection plane is clip.f pixels away on the screen for x and
	 * clip.f2 pixels away for y. Worked out here, with the matrix: moved
	 * elsewhere, the compiler fuses its products and sums otherwise and
	 * the last bits of the picture change (MARKDOWN/105). */
	const float kx = vp.clip.n / vp.clip.f;
	const float ky = vp.clip.n / vp.clip.f2;
	gl::frustum(projection,
	            (0.0f - vp.offset.x) * kx,
	            (static_cast<float>(WIDTH) - vp.offset.x) * kx,
	            (vp.offset.y - static_cast<float>(HEIGHT)) * ky,
	            vp.offset.y * ky,
	            vp.clip.n, 1e5f);
	glUniformMatrix4fv(locs.projection, 1, GL_FALSE, &projection[0][0]);

	/* The depth queue and the fog of the view (hikaru_draw.hpp) */

	if (m_variant.has_cue) {
		float cue[2];

		view_cue(vp, cue);
		glUniform2fv(locs.fog, 1, cue);
	}

	if (m_variant.has_fog) {
		float fog[2], fog_color[3];

		view_fog(vp, fog, fog_color);
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
		/* A texture that is decoded is a part of its own in the
		 * times of the drawing. */
		const bool decoded = m_times.on() && !m_textures.holds(th);
		const DrawTimes::Part part = decoded ? m_times.part(DrawTimes::PART_TEXTURES) : DrawTimes::NUM_PARTS;

		/* A texture decoded is left bound. */
		if (!m_textures.holds(th))
			m_unit0_texture = ~0u;
		id = m_textures.get(th);
		tex_ids()[mesh.tex_index] = id;
		if (decoded)
			m_times.part(part);
	}

	if (m_unit0_texture != id) {
		m_unit0_texture = id;
		glActiveTexture(GL_TEXTURE0 + 0);
		glBindTexture(GL_TEXTURE_2D, id);
	}
	/* The sampling belongs to the texhead: two texheads may read the
	 * same texture with another wrap or filter. */
	if (id) {
		const GLuint sampler = m_textures.sampler(th);

		if (m_unit0_sampler != sampler) {
			m_unit0_sampler = sampler;
			glBindSampler(0, sampler);
		}
	}
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

	if (!m_debug[DEBUG_NO_AMBIENT] && mesh.vp_index != NONE)
		view_ambient(m_list->vp_list[mesh.vp_index], color);
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

		light_colour(light, color);
		if (m_debug[DEBUG_NO_DIFFUSE])
			color[0] = color[1] = color[2] = 0.0f;
		glUniform3fv(at.color, 1, color);
		glUniform1f(at.ambient, light_ambient(light));
		glUniform2fv(at.extents, 1, light.attenuation);
		glUniform3fv(at.spot, 1, light.spot);
		glUniform2fv(at.cone, 1, cone);
		light_table(m_list->ls_tables[mesh.ls_index][i], table);
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
	glDrawElements(GL_TRIANGLES, static_cast<GLsizei>(mesh.num_tris * 3), GL_UNSIGNED_INT,
	               reinterpret_cast<const void *>(size_t{mesh.first_index} * sizeof(uint32_t)));
}

void
Renderer::draw_mesh(const odin_mesh_t &mesh)
{
	if (logging())
		log_mesh(mesh);

	use_program(mesh);
	const MeshProgram::Locations &locs = m_program->locs;
	MeshProgram::Held &held = m_program->held;

	if (held.drawing != m_drawing) {
		/* Nothing of this list yet. */
		held.drawing = m_drawing;
		held.vp_index = held.ls_index = held.ls_vp_index = NONE;
		held.mv_index = held.mv2_index = NONE;
		held.alpha_ref = held.depth_scale = held.sort_scale = NAN;
		held.weigh_by_alpha = -1;
		held.pattern = NONE;
	}
	if (m_variant.has_pattern && held.pattern != mesh.pattern) {
		held.pattern = mesh.pattern;
		glUniform1ui(locs.pattern, pattern_bits(mesh.pattern));
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

	/* The 101 moves the mesh by a fraction of its own depth, one way
	 * against the other types and another among the translucent
	 * fragments (depth_bias.hpp). It holds for the opaque types too: the
	 * lettering and the painted lines of the SGNASCAR track, the shadow
	 * of the AIRTRIX skater. */
	const int bias = m_pass.depth_bias ? static_cast<int>(mesh.depth_bias) : 0;
	const float depth_scale = depth_bias::test_scale(bias);
	if (held.depth_scale != depth_scale) {
		held.depth_scale = depth_scale;
		glUniform1f(locs.depth_scale, depth_scale);
	}
	const float sort_scale = depth_bias::sort_scale(bias);
	if (m_pass.collect && held.sort_scale != sort_scale) {
		held.sort_scale = sort_scale;
		glUniform1f(locs.sort_scale, sort_scale);
	}

	for (unsigned i = 0; i < mesh.num_instances; i++)
		draw_instance(mesh, i);
	m_times.mesh(mesh.num_tris * mesh.num_instances);
}

/* Uploads the vertices of a list and the indices of its triangles, once,
 * before its meshes are drawn. */
void
Renderer::upload_vertices(const odin_vertex_body_t *vertices, uint32_t num,
                          const uint32_t *indices, uint32_t num_indices)
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
		glGenBuffers(1, &m_mesh_ibo);
	}

	glBindVertexArray(m_mesh_vao);
	glBindBuffer(GL_ARRAY_BUFFER, m_mesh_vbo);
	glBufferData(GL_ARRAY_BUFFER,
	             static_cast<GLsizeiptr>(sizeof(odin_vertex_body_t) * num),
	             vertices, GL_DYNAMIC_DRAW);
	/* The vertex array keeps the buffer of the indices. */
	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_mesh_ibo);
	glBufferData(GL_ELEMENT_ARRAY_BUFFER,
	             static_cast<GLsizeiptr>(sizeof(uint32_t) * num_indices),
	             indices, GL_DYNAMIC_DRAW);

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

/* The blend of the material of the mesh (MeshBlend) */
void
Renderer::set_material_blend(const odin_mesh_t &mesh)
{
	unsigned mode = 0;

	if (mesh.mat_index < ODIN_LIST_MAX_MATERIALS)
		mode = odin_mat_blending_mode(&m_list->mat_list[mesh.mat_index]);
	m_pass.blend_mode = mode;
	m_pass.weigh_by_alpha = weighs_by_alpha(mode);
	set_blend(material_blend(mode));
}

void
Renderer::set_opaque_blend(const odin_mesh_t &mesh)
{
	set_blend(is_faded_by_depth(mesh) ? MeshBlend::FADED : MeshBlend::REPLACE);
}

/* The blend factors of OpenGL, sent when they change */
void
Renderer::set_blend(MeshBlend blend)
{
	static const GLenum factors[6][4] = {
		/* REPLACE */     { GL_ONE,       GL_ZERO,                GL_ZERO, GL_ZERO },
		/* FADED */       { GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ZERO, GL_ONE_MINUS_SRC_ALPHA },
		/* ALPHA_OVER */  { GL_ONE,       GL_ONE_MINUS_SRC_ALPHA, GL_ZERO, GL_ONE_MINUS_SRC_ALPHA },
		/* PRODUCT */     { GL_DST_COLOR, GL_ZERO,                GL_ZERO, GL_ZERO },
		/* ALPHA_ADDED */ { GL_ONE,       GL_ONE,                 GL_ZERO, GL_ONE_MINUS_SRC_ALPHA },
		/* OVER */        { GL_ONE,       GL_ONE_MINUS_SRC_ALPHA, GL_ZERO, GL_ONE_MINUS_SRC_ALPHA },
	};
	const GLenum *const f = factors[static_cast<unsigned>(blend)];

	if (m_blend[0] == f[0] && m_blend[1] == f[1] && m_blend[2] == f[2] && m_blend[3] == f[3])
		return;
	m_blend[0] = f[0];
	m_blend[1] = f[1];
	m_blend[2] = f[2];
	m_blend[3] = f[3];
	glBlendFuncSeparate(f[0], f[1], f[2], f[3]);
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

	/* The rules of the pass (pass_rules, hikaru_draw.hpp) */
	const PassRules rules = pass_rules(polytype);

	glEnable(GL_BLEND);
	m_blend[0] = ~0u;
	set_blend(MeshBlend::REPLACE);
	m_pass.weigh_by_alpha = false;
	switch (polytype) {
	case ODIN_POLYTYPE_TRANSPARENT:
		m_times.part(DrawTimes::PART_PUNCH_THROUGH);
		break;
	case ODIN_POLYTYPE_TRANSLUCENT:
		m_times.part(DrawTimes::PART_COLLECT);
		break;
	case ODIN_POLYTYPE_BACKGROUND:
		m_times.part(DrawTimes::PART_BACKGROUND);
		break;
	default:
		m_times.part(polytype == ODIN_POLYTYPE_OPAQUE ? DrawTimes::PART_OPAQUE : DrawTimes::PART_SHADOWS);
		break;
	}
	glDepthMask(rules.depth_write ? GL_TRUE : GL_FALSE);
	m_pass.depth_bias = rules.depth_bias;
	m_pass.alpha_ref = rules.alpha_ref;

	if (polytype == ODIN_POLYTYPE_TRANSLUCENT) {
		draw_translucent(vpi, meshes, num);
		return;
	}

	glBindVertexArray(m_mesh_vao);
	for (unsigned j = 0; j < num; j++) {
		if (rules.material_blend)
			set_material_blend(meshes[j]);
		else
			set_opaque_blend(meshes[j]);
		draw_mesh(meshes[j]);
	}
	gl::check("a polygon type");
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

	m_times.part(DrawTimes::PART_VERTICES);
	upload_vertices(m_list->verts.all, m_list->verts.num, m_list->indices.all, m_list->indices.num);

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
		m_times.part(DrawTimes::PART_VERTICES);
		glDepthMask(GL_TRUE);
		glDisable(GL_SCISSOR_TEST);
		glClear(GL_DEPTH_BUFFER_BIT);
		glEnable(GL_SCISSOR_TEST);
		for (const unsigned polytype : order)
			draw_polytype(vpi, polytype);
	}

	/* What the meshes left bound is the 2D drawing's then. */
	glBindVertexArray(0);
	glBindSampler(0, 0);
	m_unit0_sampler = 0;
	glDepthMask(GL_TRUE);
	glDisable(GL_SCISSOR_TEST);
	glFrontFace(GL_CCW);
	glViewport(0, 0, static_cast<GLsizei>(width()), static_cast<GLsizei>(height()));
	gl::check("a scene");
}

} /* namespace odin */
