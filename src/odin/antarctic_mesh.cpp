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

/* From the vertices the instructions push to the meshes of the display
 * list: the triangles are assembled here, and each mesh takes a copy of the
 * objects that are current when its first vertex comes. */

#include "odin/antarctic.hpp"

#include <cstring>

namespace odin {

static constexpr float INV255 = 1.0f / 255.0f;

Antarctic::List::List()
	: vps(new odin_viewport_t[ODIN_LIST_MAX_VIEWPORTS]),
	  mvs(new odin_modelview_t[ODIN_LIST_MAX_MODELVIEWS]),
	  mats(new odin_material_t[ODIN_LIST_MAX_MATERIALS]),
	  texs(new odin_texhead_t[ODIN_LIST_MAX_TEXHEADS]),
	  lss(new odin_lightset_t[ODIN_LIST_MAX_LIGHTSETS]),
	  ls_tables(new uint16_t[ODIN_LIST_MAX_LIGHTSETS][4][16])
{
	view.vp_list = vps.get();
	view.mv_list = mvs.get();
	view.mat_list = mats.get();
	view.tex_list = texs.get();
	view.ls_list = lss.get();
	view.ls_tables = ls_tables.get();
	for (unsigned vp = 0; vp < 8; vp++) {
		for (unsigned type = 0; type < 8; type++) {
			meshes[vp][type].reset(new odin_mesh_t[ODIN_LIST_MAX_MESHES]);
			view.mesh_list[vp][type] = meshes[vp][type].get();
		}
	}
	clear();
}

void
Antarctic::List::clear()
{
	view.num_vps = 0;
	view.num_mvs = 0;
	view.num_instances = 0;
	view.mv2_index = ~0u;
	view.num_mats = 0;
	view.num_texs = 0;
	view.num_lss = 0;
	for (auto &by_type : view.num_meshes)
		for (uint32_t &num : by_type)
			num = 0;
	view.total_meshes = 0;
	verts.clear();
	view.verts.all = verts.data();
	view.verts.num = 0;
}

odin_display_list_t *
Antarctic::begin_list(bool keep_last)
{
	if (keep_last)
		std::swap(m_list, m_other);
	m_list->clear();
	return &m_list->view;
}

/* Copies the objects that are current into the list and points the mesh
 * to them. */
void
Antarctic::list_state(odin_mesh_t &mesh)
{
	odin_display_list_t &l = m_list->view;

	std::memset(&mesh, 0, sizeof(mesh));

	if (l.num_vps + 1 >= ODIN_LIST_MAX_VIEWPORTS ||
	    l.num_mats + 1 >= ODIN_LIST_MAX_MATERIALS ||
	    l.num_texs + 1 >= ODIN_LIST_MAX_TEXHEADS ||
	    l.num_lss + 1 >= ODIN_LIST_MAX_LIGHTSETS ||
	    l.num_mvs + 2 >= ODIN_LIST_MAX_MODELVIEWS)
		fatal("too many objects in a frame", l.total_meshes);

	l.vp_list[l.num_vps++] = m_s.vp.current;
	l.mat_list[l.num_mats++] = m_s.mat.scratch;
	l.tex_list[l.num_texs++] = m_s.tex.scratch;

	/* The 194 table of each light: a word has two points, the even one
	 * in its upper half; the first of the four tables of a light is
	 * taken (HEURISTIC: what picks one is not known, the games load
	 * the same ramp in all of them). */
	for (unsigned i = 0; i < 4; i++) {
		for (unsigned j = 0; j < 8; j++) {
			const uint32_t word = m_s.light_table[i][j];

			l.ls_tables[l.num_lss][i][j * 2] = static_cast<uint16_t>(word >> 16);
			l.ls_tables[l.num_lss][i][j * 2 + 1] = static_cast<uint16_t>(word);
		}
	}
	l.ls_list[l.num_lss++] = m_s.lit.scratchset;

	mesh.mv_index = l.num_mvs;
	mesh.num_instances = 1;
	l.num_instances = 1;
	get_modelview(l.mv_list[l.num_mvs++]);

	/* The second modelview (161 with bit 18) is listed when it changes. */
	if (l.mv2_index >= ODIN_LIST_MAX_MODELVIEWS ||
	    std::memcmp(&l.mv_list[l.mv2_index], &m_s.mv.second,
	                sizeof(odin_modelview_t)) != 0) {
		l.mv2_index = l.num_mvs;
		l.mv_list[l.num_mvs++] = m_s.mv.second;
	}
	mesh.mv2_index = l.mv2_index;

	mesh.depth_bias = m_s.depth_bias;
	mesh.pattern = m_s.pattern;

	mesh.vp_index = l.num_vps - 1;
	mesh.mat_index = l.num_mats - 1;
	mesh.tex_index = l.num_texs - 1;
	mesh.ls_index = l.num_lss - 1;

	mesh.num = l.total_meshes++;
}

void
Antarctic::begin_mesh(uint32_t addr)
{
	odin_display_list_t &l = m_list->view;
	/* The meshes are kept by viewport, which the top bits of the 421
	 * number, and by polygon type. */
	const uint32_t vp = m_s.vp.current.depth.func;
	const uint32_t type = m_s.poly.type;
	const uint32_t index = l.num_meshes[vp][type];

	if (m_mesh)
		fatal("a mesh is open already", addr);
	if (index + 1 >= ODIN_LIST_MAX_MESHES)
		fatal("too many meshes in a frame", index);

	m_mesh = &l.mesh_list[vp][type][index];
	l.num_meshes[vp][type] = index + 1;

	list_state(*m_mesh);
	m_mesh->poly_alpha = m_s.poly.alpha;
	m_mesh->polytype = type;
	m_mesh->addr[0] = addr;

	/* The last vertices stay and take the new material: a strip goes
	 * on through a recall. The colour bars of the BOOTROM C.R.T. test
	 * are one strip, with a material recalled before the two vertices
	 * that close each bar. */
	if (m_push.num_verts > 3)
		m_push.num_verts = 3;
	for (uint32_t i = 3 - m_push.num_verts; i < 3; i++)
		copy_material(m_push.tmp[i]);
	m_push.num_tris = 0;
}

void
Antarctic::end_mesh(uint32_t addr)
{
	List &list = *m_list;
	const uint32_t num = m_push.num_tris * 3;

	if (!m_mesh)
		fatal("no mesh is open", addr);

	m_mesh->num_tris = m_push.num_tris;
	m_mesh->first = static_cast<uint32_t>(list.verts.size());
	list.verts.insert(list.verts.end(), m_push.all.get(), m_push.all.get() + num);
	list.view.verts.all = list.verts.data();
	list.view.verts.num = static_cast<uint32_t>(list.verts.size());

	m_mesh->addr[1] = addr;
	m_mesh = nullptr;
}

static float
clampf(float x, float min_, float max_)
{
	return (min_ > x) ? min_ :
	       (max_ < x) ? max_ : x;
}

void
Antarctic::copy_material(Vertex &dst) const
{
	const odin_material_t &mat = m_s.mat.scratch;

	for (unsigned i = 0; i < 3; i++)
		dst.body.diffuse[i] = mat.diffuse[i];

	/* The 691 colour is the ambient of the shading mode 1 and the
	 * emission of the mode 2, 255 for 1.0. The port saturates it there
	 * (825B4DE8), the hardware does not: PHARRIER sends 600 in mode 2
	 * for the syringe of its nurse and 736 in mode 1 for the selection
	 * frame of its shop, and the cabinet shows both that much brighter. */
	for (unsigned i = 0; i < 3; i++)
		dst.body.ambient[i] = mat.unknown[i] / 255.0f;

	/* The 491 colour is in 1/64: PHARRIER sets 0x40 and SGNASCAR goes up
	 * to 0x7F. The port multiplies it by 4 and wraps to 8 bits (825B4DE8),
	 * the same thing below 0x40; the shader scales it. */
	for (unsigned i = 0; i < 4; i++)
		dst.body.specular[i] = mat.specular[i];

	for (unsigned i = 0; i < 3; i++)
		dst.body.color1[i] = mat.ambient[i];
}

void
Antarctic::copy_colors(Vertex &dst, const Vertex &src) const
{
	copy_material(dst);

	/* Vertex alpha times polygon alpha for every type (X360 8252D858);
	 * the render pass decides what it means. */
	const float alpha = clampf(m_s.poly.alpha *
	                           (static_cast<float>(src.info.alpha()) * INV255),
	                           0.0f, 1.0f);
	dst.body.alpha = static_cast<uint8_t>(alpha * 255.0f);
}

void
Antarctic::copy_texcoords(Vertex &dst, const Vertex &src) const
{
	const odin_texhead_t *th = &m_s.tex.scratch;
	float w = static_cast<float>(16 << odin_tex_logw(th));
	float h = static_cast<float>(16 << odin_tex_logh(th));
	/* Vertex UVs are 12.4 unless the texhead says whole texels; the 0D1
	 * offset slot picked by the vertex is added in 1/16 texel (X360
	 * 8252D710, 82538E70). */
	const float k = odin_tex_uvtexels(th) ? 16.0f : 1.0f;
	float u = src.body.texcoords[0] * k;
	float v = src.body.texcoords[1] * k;
	const uint32_t sel = src.info.uv_offset();

	if (odin_tex_format(th) == ODIN_FORMAT_I4) {
		w *= 2;
		h *= 2;
	}

	if (sel) {
		u += m_s.texoffset[sel][0] / 16.0f;
		v += m_s.texoffset[sel][1] / 16.0f;
	}

	dst.body.texcoords[0] = u / w;
	dst.body.texcoords[1] = v / h;
}

/* The last three vertices make a triangle of the mesh. */
void
Antarctic::add_triangle()
{
	if (m_push.num_verts < 3)
		return;

	const uint32_t index = m_push.num_tris * 3;
	const bool both_faces = m_push.tmp[2].info.nocull();
	odin_vertex_body_t *dst = &m_push.all[index];
	const odin_vertex_body_t *a = &m_push.tmp[0].body;
	const odin_vertex_body_t *b = &m_push.tmp[1].body;
	const odin_vertex_body_t *c = &m_push.tmp[2].body;
	float e1[3], e2[3], n[3];

	if (index + (both_faces ? 6 : 3) > MAX_VERTICES_PER_MESH)
		fatal("too many triangles in a mesh", m_push.num_tris);

	/* Bits 19:18 select a UV offset, not two-sided lighting (X360
	 * port, 8252D590). */

	/* Degenerate triangles are dropped, as in the X360 port
	 * (825B6100); not when the vertices follow different
	 * modelviews, the matrices give them their shape then (the
	 * AIRTRIX trails). */
	for (unsigned i = 0; i < 3; i++) {
		e1[i] = b->position[i] - a->position[i];
		e2[i] = c->position[i] - a->position[i];
	}
	n[0] = e1[1] * e2[2] - e1[2] * e2[1];
	n[1] = e1[2] * e2[0] - e1[0] * e2[2];
	n[2] = e1[0] * e2[1] - e1[1] * e2[0];
	if (a->second_mv == b->second_mv && b->second_mv == c->second_mv &&
	    (n[0] * n[0] + n[1] * n[1] + n[2] * n[2]) <= 0.0f)
		return;

	/* The winding is the vertex order, reversed by the winding bit
	 * of the closing vertex. The port winds each triangle after its
	 * vertex normals instead (825B6218), but the SGNASCAR cabinet
	 * shows quads whose normals point away from the viewer (the
	 * dark panels of its ENTRY DRIVERS screen). */
	if (m_push.tmp[2].info.winding())
		std::swap(b, c);

	dst[0] = *a;
	dst[1] = *b;
	dst[2] = *c;
	m_push.num_tris += 1;

	/* Both faces (bit 23): the triangle is emitted twice, the
	 * second time for its back, lit with the reversed normals. */
	if (both_faces) {
		dst[3] = *a;
		dst[4] = *c;
		dst[5] = *b;
		for (unsigned i = 3; i < 6; i++)
			for (unsigned j = 0; j < 3; j++)
				dst[i].normal[j] = -dst[i].normal[j];
		m_push.num_tris += 1;
	}
}

/* Takes what a push instruction brings: one vertex, or the texcoords of
 * the last three. A header with the triangle bits closes a triangle. */
void
Antarctic::push_vertices(const Vertex *v, uint32_t flags, uint32_t num)
{
	if (v->info.tricap() != 0 && v->info.tricap() != 7)
		fatal("triangle bits not known", v->info.full);

	if (num == 1) {
		/* Note that tmp[2] always points to the last pushed vertex,
		 * which for instructions 12x, 1Ax and 1Bx means the vertex
		 * pushed by the instruction itself, and for instructions 0Ex
		 * and 15x the vertex pushed by the previous "push"
		 * instruction. */

		/* If the incoming vertex includes the position, push it
		 * in the temporary buffer, updating it according to the
		 * p(osition)pivot bit. */
		if (flags & PUSH_POS) {
			/* Do not change the pivot if it is not required */
			if (!v->info.ppivot())
				m_push.tmp[0] = m_push.tmp[1];
			m_push.tmp[1] = m_push.tmp[2];
			std::memset(&m_push.tmp[2], 0, sizeof(Vertex));

			/* Set the position, colors and alpha. */
			for (unsigned i = 0; i < 3; i++)
				m_push.tmp[2].body.position[i] = v->body.position[i];
			copy_colors(m_push.tmp[2], *v);

			/* Account for the added vertex. */
			m_push.num_verts += 1;
			if (m_push.num_verts >= MAX_VERTICES_PER_MESH)
				fatal("too many vertices in a mesh", m_push.num_verts);
		}

		/* Set the normal. */
		if (flags & PUSH_NRM)
			for (unsigned i = 0; i < 3; i++)
				m_push.tmp[2].body.normal[i] = v->body.normal[i];

		/* Bit 12 of the header: the vertex follows the second
		 * modelview (161 with bit 18). Bits 13 and 14: it takes the
		 * 3x3 and the last row of its modelview. The vertex shader
		 * picks the matrix. */
		if (flags & PUSH_POS)
			m_push.tmp[2].body.second_mv = static_cast<uint8_t>(
				(v->info.second_mv() ? 1 : 0) +
				(v->info.rotate() ? 0 : 64) +
				(v->info.translate() ? 0 : 128));

		/* Set the texcoords. */
		if (flags & PUSH_TXC)
			copy_texcoords(m_push.tmp[2], *v);
	} else {
		if (m_push.num_verts < 3)
			return;

		for (unsigned i = 0; i < 3; i++)
			copy_texcoords(m_push.tmp[2 - i], v[i]);
	}

	/* Finish the previous triangle. */
	if (v[0].info.tricap() == 7) {
		m_push.tmp[2].info.full = v[0].info.full;
		add_triangle();
	}
}

} /* namespace odin */
