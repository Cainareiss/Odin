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


#include "odin/renderer_times.hpp"

#include <algorithm>
#include <cinttypes>
#include <cstdio>

#include "odin/env.hpp"

namespace odin {

namespace {

const char *const NAMES[DrawTimes::NUM_PARTS] = {
	"the list before, counted and kept",
	"vertices, buffers cleared",
	"background polygons",
	"punch-through polygons",
	"opaque polygons",
	"shadow polygons",
	"translucent polygons, collected",
	"translucent fragments, sorted",
	"textures decoded",
	"the picture into its 3D buffer",
	"2D layers as textures",
	"picture and layers composed",
	"to the size of the window",
	"handed to the window",
};

double
ms(uint64_t ns, uint64_t frames)
{
	return frames ? static_cast<double>(ns) / 1e6 / static_cast<double>(frames) : 0.0;
}

} /* namespace */

DrawTimes::DrawTimes(bool on)
	: m_on(on), m_card(false), m_part(NUM_PARTS), m_within(false), m_frame(0), m_sums{},
	  m_drawn(0), m_read(0), m_lists(0), m_fragments(0), m_fragments_worst(0)
{
	if (m_on) {
		GLint bits = 0;

		glGetQueryiv(GL_TIMESTAMP, GL_QUERY_COUNTER_BITS, &bits);
		m_card = bits > 0;
	}
}

DrawTimes::~DrawTimes()
{
	for (std::vector<Mark> &frame : m_frames)
		for (const Mark &mark : frame)
			m_stamps.push_back(mark.stamp);
	if (m_card && !m_stamps.empty())
		glDeleteQueries(static_cast<GLsizei>(m_stamps.size()), m_stamps.data());
}

GLuint
DrawTimes::new_stamp()
{
	GLuint id;

	if (m_stamps.empty()) {
		glGenQueries(1, &id);
	} else {
		id = m_stamps.back();
		m_stamps.pop_back();
	}
	return id;
}

void
DrawTimes::begin()
{
	if (!m_on)
		return;
	if (m_within)
		end();

	/* The frame drawn HELD frames ago leaves its place. */
	m_frame = (m_frame + 1) % HELD;
	read(m_frames[m_frame]);
	m_within = true;
}

DrawTimes::Part
DrawTimes::enter(Part part)
{
	const Part before = m_part;
	Mark mark = { part, host_ns(), 0 };

	if (m_card) {
		mark.stamp = new_stamp();
		glQueryCounter(mark.stamp, GL_TIMESTAMP);
	}
	m_frames[m_frame].push_back(mark);
	m_part = part;
	if (part < NUM_PARTS)
		m_sums[part].entered++;
	return before;
}

void
DrawTimes::end()
{
	if (!m_within)
		return;
	enter(NUM_PARTS);
	m_within = false;
	/* The card would get the last stamp with the next frame only. */
	if (m_card)
		glFlush();

	const std::vector<Mark> &frame = m_frames[m_frame];
	std::array<uint64_t, NUM_PARTS> host{};

	for (size_t i = 0; i + 1 < frame.size(); i++)
		host[frame[i].part] += frame[i + 1].host - frame[i].host;
	for (unsigned i = 0; i < NUM_PARTS; i++) {
		m_sums[i].host += host[i];
		m_sums[i].host_worst = std::max(m_sums[i].host_worst, host[i]);
	}
	m_drawn++;
}

/* The stamps of a frame, if the card has them all: the last one tells. A
 * frame it does not have yet is left out. */
void
DrawTimes::read(std::vector<Mark> &frame)
{
	if (m_card && !frame.empty()) {
		GLuint ready = 0;

		glGetQueryObjectuiv(frame.back().stamp, GL_QUERY_RESULT_AVAILABLE, &ready);
		if (ready) {
			std::array<uint64_t, NUM_PARTS> card{};
			GLuint64 at = 0;

			glGetQueryObjectui64v(frame[0].stamp, GL_QUERY_RESULT, &at);
			for (size_t i = 0; i + 1 < frame.size(); i++) {
				GLuint64 next = 0;

				glGetQueryObjectui64v(frame[i + 1].stamp, GL_QUERY_RESULT, &next);
				if (next > at)
					card[frame[i].part] += next - at;
				at = next;
			}
			for (unsigned i = 0; i < NUM_PARTS; i++) {
				m_sums[i].card += card[i];
				m_sums[i].card_worst = std::max(m_sums[i].card_worst, card[i]);
			}
			m_read++;
			for (const Mark &mark : frame)
				m_stamps.push_back(mark.stamp);
		} else {
			/* A stamp the card still owes is not asked again. */
			for (const Mark &mark : frame)
				glDeleteQueries(1, &mark.stamp);
		}
	}
	frame.clear();
}

void
DrawTimes::fragments(uint32_t num)
{
	if (!m_on)
		return;
	m_lists++;
	m_fragments += num;
	m_fragments_worst = std::max(m_fragments_worst, num);
}

void
DrawTimes::tell() const
{
	uint64_t host = 0, card = 0;

	if (!m_on || !m_drawn)
		return;

	std::fprintf(stderr, "drawing: %" PRIu64 " frames, a frame in ms: on the thread [its worst frame]", m_drawn);
	if (m_card)
		std::fprintf(stderr, ", for the graphics card [its worst frame] over %" PRIu64 " frames\n", m_read);
	else
		std::fprintf(stderr, "; the graphics card tells no time\n");

	for (unsigned i = 0; i < NUM_PARTS; i++) {
		const Sum &sum = m_sums[i];

		if (!sum.entered)
			continue;
		host += sum.host;
		card += sum.card;
		std::fprintf(stderr, "drawing:  %-34s %7.3f [%7.3f]", NAMES[i],
		             ms(sum.host, m_drawn), ms(sum.host_worst, 1));
		if (m_card)
			std::fprintf(stderr, "  %7.3f [%7.3f]", ms(sum.card, m_read), ms(sum.card_worst, 1));
		if (sum.meshes)
			std::fprintf(stderr, "  %.0f meshes, %.0f triangles a frame",
			             static_cast<double>(sum.meshes) / static_cast<double>(m_drawn),
			             static_cast<double>(sum.triangles) / static_cast<double>(m_drawn));
		if (i == PART_TEXTURES)
			std::fprintf(stderr, "  %" PRIu64 " in all", sum.entered);
		if (i == PART_SORT && m_lists)
			std::fprintf(stderr, "  %.0f fragments a list, %u at most",
			             static_cast<double>(m_fragments) / static_cast<double>(m_lists),
			             m_fragments_worst);
		std::fprintf(stderr, "\n");
	}
	std::fprintf(stderr, "drawing:  %-34s %7.3f          ", "all", ms(host, m_drawn));
	if (m_card)
		std::fprintf(stderr, "  %7.3f", ms(card, m_read));
	std::fprintf(stderr, "\n");
}

} /* namespace odin */
