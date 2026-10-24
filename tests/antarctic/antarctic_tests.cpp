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

/* Unit tests for the command processor of the Hikaru: small lists written
 * by hand and what they leave in the display list, its registers and its
 * texture transfer.
 *
 * Build: g++ -std=c++20 -O2 -Wall -Wextra -Wpedantic -Wconversion -Wshadow \
 *            -I src tests/antarctic/antarctic_tests.cpp src/odin/antarctic.cpp \
 *            src/odin/antarctic_ops.cpp src/odin/antarctic_mesh.cpp \
 *            src/odin/antarctic_io.cpp src/odin/antarctic_disasm.cpp \
 *            src/odin/antarctic_list.cpp -o antarctic_tests
 * Exit status is 0 when every check passes. */

#include "odin/antarctic.hpp"
#include "odin/antarctic_disasm.hpp"

#include <bit>
#include <cstdio>
#include <cstring>
#include <initializer_list>
#include <memory>
#include <string>
#include <vector>

namespace {

using odin::Antarctic;

int g_checks, g_failures;

void check(bool ok, const char *what)
{
	g_checks++;
	if (!ok) {
		g_failures++;
		std::printf("FAILED: %s\n", what);
	}
}

uint32_t f(float value)
{
	return std::bit_cast<uint32_t>(value);
}

/* Where the lists of the tests start, and their stack */
constexpr uint32_t LIST = 0x48000100;
constexpr uint32_t STACK = 0x483FC000;

/* A command processor with its two memories, and the list of one frame */
struct Machine {
	std::vector<uint8_t> cmdram = std::vector<uint8_t>(4 << 20);
	std::vector<uint8_t> ram_s = std::vector<uint8_t>(32 << 20);
	std::vector<uint8_t> texram[2] = { std::vector<uint8_t>(4 << 20), std::vector<uint8_t>(4 << 20) };
	std::unique_ptr<Antarctic> cp = std::make_unique<Antarctic>(cmdram, ram_s, texram[0], texram[1]);
	odin_display_list_t *list = nullptr;
	bool ended = false;
	uint32_t left = 0;	/* the budget the last run left */

	void write(uint32_t addr, std::initializer_list<uint32_t> words)
	{
		uint32_t offs = addr & 0x003FFFFF;

		for (uint32_t word : words) {
			std::memcpy(&cmdram[offs], &word, 4);
			offs += 4;
		}
	}
	/* Runs the list at LIST for a frame. */
	void run(std::initializer_list<uint32_t> words, uint32_t budget = 1000)
	{
		write(LIST, words);
		cp->set_log(nullptr);
		list = cp->begin_list();
		cp->start(LIST, STACK, 0);
		ended = cp->run(budget);
		left = budget;
		cp->end_open_mesh();
	}
	const odin_mesh_t &mesh(uint32_t type, uint32_t index) const
	{
		return list->mesh_list[0][type][index];
	}
	const odin_vertex_body_t &vertex(const odin_mesh_t &m, uint32_t index) const
	{
		return list->verts.all[list->indices.all[m.first_index + index]];
	}
};

/* A vertex at (x, y, 0): 1AC, with the triangle bits if it closes one */
#define VERTEX(x, y)		0xFF00E1AC, f(x), f(y), f(0.0f)
#define VERTEX_CLOSING(x, y)	0xFF00EFAC, f(x), f(y), f(0.0f)

constexpr uint32_t OPAQUE = 0xFF000303;	/* 103: polygon type 1, alpha FF */
constexpr uint32_t END = 0x000001C2;

void test_sizes_and_end()
{
	Machine m;

	/* 1, 2 and 4 words, then the end */
	m.run({ 0x00000000,
	        0x00000991, 0x01332211,
	        0x00000021, f(1280.0f), f(1280.0f), f(0.1f),
	        END });
	check(m.ended, "a list ends at its 1C2");
	check(!m.cp->running(), "the processor stops at the end of the list");
	check(m.cp->pc() == LIST + 4 + 8 + 16, "the program counter stays on the 1C2");
	check(m.cp->fb_config().clear[0] == 0x11 && m.cp->fb_config().clear[1] == 0x22 &&
	      m.cp->fb_config().clear[2] == 0x33 && m.cp->fb_config().clear[3] == 0xFF,
	      "991 sets the clear colour");

	Machine n;
	n.run({ 0x00000000, 0x00000000, 0x00000000, END }, 2);
	check(!n.ended && n.cp->running(), "a list longer than the budget goes on");
	uint32_t budget = 10;
	check(n.cp->run(budget) && budget == 8, "and ends at a later call, two instructions on");
}

void test_invalid()
{
	Machine m;

	m.run({ 0x000001FF, END });
	check(m.ended && m.left == 999, "an instruction that does not exist ends the list, in a cycle");

	Machine n;
	n.cp->set_log(nullptr);
	n.list = n.cp->begin_list();
	n.cp->start(0x20000000, STACK, 0);
	uint32_t budget = 10;
	check(n.cp->run(budget) && budget == 9, "a program counter out of the two memories ends the list, in a cycle");
}

void test_jump_to_itself()
{
	Machine m;

	m.run({ 0x00000012, LIST / 4 }, 100);
	check(!m.ended && m.cp->running() && m.cp->pc() == LIST, "a jump to itself keeps the processor there");
	m.write(LIST, { END });
	uint32_t budget = 10;
	check(m.cp->run(budget) && budget == 9, "until the word is changed");
}

void test_call_and_return()
{
	Machine m;
	const uint32_t sub = LIST + 0x100;

	m.write(sub, { 0x00000991, 0x00000042, 0x00000082 });
	m.run({ 0x00000052, sub >> 2,		/* call */
	        0x00000012, (LIST + 0x14) >> 2,	/* jump over the next word */
	        0x000001FF,			/* never executed */
	        END });
	check(m.ended, "call, return and jump: the list ends");
	check(m.cp->fb_config().clear[0] == 0x42, "the subroutine was executed");
	check(m.cp->pc() == LIST + 0x14, "the jump skips an invalid word");
}

void test_strip()
{
	Machine m;

	m.run({ OPAQUE,
	        0x00000091, 0x00302010,		/* material colour */
	        VERTEX(0.0f, 0.0f), VERTEX(0.0f, 1.0f),
	        VERTEX_CLOSING(1.0f, 0.0f), VERTEX_CLOSING(1.0f, 1.0f),
	        END });
	check(m.list->total_meshes == 1 && m.list->num_meshes[0][1] == 1, "four vertices make one opaque mesh");

	const odin_mesh_t &mesh = m.mesh(1, 0);
	check(mesh.num_tris == 2, "a strip of four vertices has two triangles");
	check(m.vertex(mesh, 0).position[1] == 0.0f && m.vertex(mesh, 1).position[1] == 1.0f &&
	      m.vertex(mesh, 2).position[0] == 1.0f, "the first triangle has the first three vertices");
	check(m.vertex(mesh, 3).position[1] == 1.0f && m.vertex(mesh, 5).position[1] == 1.0f,
	      "the second triangle starts from the second vertex");
	check(m.list->verts.num == 4 && m.list->indices.num == 6, "the two triangles share two vertices");
	check(m.vertex(mesh, 0).diffuse[0] == 0x10 && m.vertex(mesh, 0).diffuse[1] == 0x20 &&
	      m.vertex(mesh, 0).diffuse[2] == 0x30, "the vertices take the 091 colour");
	check(m.vertex(mesh, 0).alpha == 0xFF, "vertex alpha FF times polygon alpha FF");
	check(m.vertex(mesh, 0).second_mv == 0, "header E: the modelview, whole");
}

void test_three_lists()
{
	Machine m;
	const odin_display_list_t *lists[4];

	for (unsigned i = 0; i < 3; i++) {
		m.run({ OPAQUE,
		        VERTEX(0.0f, 0.0f), VERTEX(0.0f, 1.0f),
		        VERTEX_CLOSING(1.0f, static_cast<float>(i)),
		        END });
		lists[i] = m.list;
	}
	check(lists[0] != lists[1] && lists[1] != lists[2] && lists[2] != lists[0],
	      "three frames in a row are built in three lists");
	check(lists[0]->total_meshes == 1 && lists[0]->verts.all[lists[0]->indices.all[2]].position[1] == 0.0f &&
	      lists[1]->total_meshes == 1 && lists[1]->verts.all[lists[1]->indices.all[2]].position[1] == 1.0f,
	      "the lists of the last two frames stay as they are");

	m.cp->reset();
	check(lists[2]->total_meshes == 0, "a reset empties the list being built");
	check(lists[0]->total_meshes == 1 && lists[1]->total_meshes == 1, "and leaves the two others");

	m.run({ END });
	lists[3] = m.list;
	check(lists[3] == lists[0] && lists[3]->total_meshes == 0, "the fourth frame takes the first list again, empty");
}

void test_degenerate_and_both_faces()
{
	Machine m;

	m.run({ OPAQUE,
	        VERTEX(0.0f, 0.0f), VERTEX(1.0f, 0.0f), VERTEX_CLOSING(2.0f, 0.0f),
	        END });
	check(m.mesh(1, 0).num_tris == 0, "three vertices on a line make no triangle");

	Machine n;
	n.run({ OPAQUE,
	        VERTEX(0.0f, 0.0f), VERTEX(0.0f, 1.0f),
	        0xFF80EFAC, f(1.0f), f(0.0f), f(0.0f),	/* bit 23: both faces */
	        END });
	check(n.mesh(1, 0).num_tris == 2, "a triangle with bit 23 is listed for both faces");
	check(n.list->verts.num == 6 && n.vertex(n.mesh(1, 0), 4).position[0] == 1.0f,
	      "its back has vertices of its own, in the other order");
}

/* The colour bars of the BOOTROM C.R.T. test: one strip, a material
 * recalled before the two vertices that close each bar. */
void test_strip_through_recall()
{
	Machine m;

	m.run({ OPAQUE,
	        0x00000083,			/* material base 0 */
	        0x00000091, 0x000000FF, 0x00011084,	/* material 1: red */
	        0x00000091, 0x0000FF00, 0x00021084,	/* material 2: green */
	        0x00011083,
	        VERTEX(0.0f, 0.0f), VERTEX(1.0f, 0.0f),
	        VERTEX_CLOSING(0.0f, -1.0f), VERTEX_CLOSING(1.0f, -1.0f),
	        0x00021083,
	        VERTEX_CLOSING(0.0f, -2.0f), VERTEX_CLOSING(1.0f, -2.0f),
	        END });
	check(m.list->num_meshes[0][1] == 2, "a material recall starts another mesh");
	check(m.mesh(1, 0).num_tris == 2 && m.mesh(1, 1).num_tris == 2, "the strip goes on through the recall");

	const odin_mesh_t &second = m.mesh(1, 1);
	bool green = true;
	for (uint32_t i = 0; i < 6; i++)
		green = green && m.vertex(second, i).diffuse[0] == 0x00 && m.vertex(second, i).diffuse[1] == 0xFF;
	check(green, "the vertices kept from before the recall take the new material");
	check(m.list->mat_list[second.mat_index].diffuse[1] == 0xFF, "the second mesh points to the new material");
}

/* The meshes that follow one another with the same object point to the
 * same one; an object that comes back after another one is listed again. */
void test_objects_listed_once()
{
	Machine m;

	m.run({ OPAQUE,
	        0x00000083,
	        0x00000091, 0x000000FF, 0x00011084,	/* material 1: red */
	        0x00000091, 0x0000FF00, 0x00021084,	/* material 2: green */
	        0x00011083,
	        VERTEX(0.0f, 0.0f), VERTEX(1.0f, 0.0f),
	        VERTEX_CLOSING(0.0f, -1.0f), VERTEX_CLOSING(1.0f, -1.0f),
	        0x00021083,
	        VERTEX_CLOSING(0.0f, -2.0f), VERTEX_CLOSING(1.0f, -2.0f),
	        0x00011083,
	        VERTEX_CLOSING(0.0f, -3.0f), VERTEX_CLOSING(1.0f, -3.0f),
	        0x00030194, 0x04000200,		/* two points of the table of the light 0 */
	        0x00021083,
	        VERTEX_CLOSING(0.0f, -4.0f), VERTEX_CLOSING(1.0f, -4.0f),
	        0x00000051, 0x000000FF,		/* a red light, the first of the set 5 */
	        0x00010104,
	        0x00051064, 0x00000001, 0x00000000, 0x00000000,
	        0x00051043,
	        0x00011083,
	        VERTEX_CLOSING(0.0f, -5.0f), VERTEX_CLOSING(1.0f, -5.0f),
	        END });
	check(m.ended && m.list->num_meshes[0][1] == 5, "five bars make five meshes");

	const odin_mesh_t &first = m.mesh(1, 0), &second = m.mesh(1, 1), &third = m.mesh(1, 2), &fourth = m.mesh(1, 3);
	const odin_mesh_t &fifth = m.mesh(1, 4);

	check(m.list->num_vps == 1 && m.list->num_texs == 1 && first.vp_index == fifth.vp_index &&
	      first.tex_index == fifth.tex_index, "the viewport and the texhead of the five meshes are listed once");
	check(m.list->num_mats == 5 && first.mat_index != second.mat_index && third.mat_index != first.mat_index &&
	      m.list->mat_list[third.mat_index].diffuse[0] == 0xFF,
	      "a material that comes back after another one is listed again");
	check(first.ls_index == third.ls_index && fourth.ls_index != third.ls_index,
	      "a table that changes makes another lightset");
	check(m.list->num_lss == 3 && fifth.ls_index != fourth.ls_index &&
	      m.list->ls_list[fifth.ls_index].lights[0].diffuse[0] == 0xFF &&
	      m.list->ls_list[fourth.ls_index].lights[0].diffuse[0] == 0,
	      "and so does a light that changes");
	check(m.list->ls_tables[fourth.ls_index][0][6] == 0x0400 && m.list->ls_tables[fourth.ls_index][0][7] == 0x0200 &&
	      m.list->ls_tables[first.ls_index][0][6] == 0, "with its points, the even one from the upper half");
}

/* The grid of the BOOTROM C.R.T. test: the 0E8 names its texhead, and its
 * vertices follow the second modelview without its 3x3. */
/* BRAVEFF sends the blending mode as A91, a C91 behind it. */
void test_blending_mode_in_two_words()
{
	Machine m;

	m.run({ 0x00020A91, 0x003F0C91,
	        END });
	check(m.ended && m.cp->pc() == LIST + 8, "A91 takes two words");
	check(odin_mat_blending_mode(&m.cp->state().mat.scratch) == 2, "the blending mode comes from the first");
	check(!(m.cp->state().mat.scratch.flags & ODIN_MAT_HAS_C81), "the second is not run as a C81");
}

void test_texhead_of_0E8_and_second_modelview()
{
	Machine m;

	m.run({ OPAQUE,
	        0x000000C3,				/* texhead base 0 */
	        0xA81282C1, 0x000010C4,			/* texhead 0: 64x64 */
	        0xA8C0C2C1, 0x000110C4,			/* texhead 1: 16x16, whole texels */
	        0x000010C3,
	        0x00070161, f(0.0f), f(0.0f), f(-992.0f),	/* last row of the second modelview */
	        0x8000D1AC, f(0.0f), f(0.0f), f(0.0f),
	        0x8000D1AC, f(0.0f), f(-32.0f), f(0.0f),
	        0x8000D1AC, f(32.0f), f(0.0f), f(0.0f),
	        0x00010EE8, 0x00000020, 0x00200000, 0x00000000,
	        END });
	check(m.list->num_meshes[0][1] == 2, "a 0E8 with another texhead starts another mesh");

	const odin_mesh_t &mesh = m.mesh(1, 1);
	check(mesh.num_tris == 1, "the three vertices pushed before the 0E8 make its triangle");
	check(m.list->tex_list[mesh.tex_index].w2C1 == 0xA8C0C2C1, "the triangle has the texhead of the 0E8");
	check(m.vertex(mesh, 0).second_mv == 65, "header D: second modelview, without its 3x3");
	/* 32 texels on a 4-bit texture of 16x16, which is 32 texels wide */
	check(m.vertex(mesh, 0).texcoords[0] == 0.0f && m.vertex(mesh, 2).texcoords[0] == 1.0f,
	      "texcoords in whole texels");
	check(m.list->mv_list[mesh.mv2_index].mtx[3][2] == -992.0f, "the second modelview is listed");
}

void test_viewport_and_state()
{
	Machine m;

	m.run({ 0x00000421, f(0.1f), f(100.0f), 0x60000000,	/* depth function 3 */
	        0x00030004, 0x00030003,				/* commit and recall viewport 3 */
	        OPAQUE,
	        VERTEX(0.0f, 0.0f), VERTEX(0.0f, 1.0f), VERTEX_CLOSING(1.0f, 0.0f),
	        END });
	check(m.list->num_meshes[3][1] == 1, "the meshes are kept by the depth function of their viewport");

	auto saved = std::make_unique<odin::AntarcticState>(m.cp->state());
	Machine n;
	n.cp->load(*saved);
	check(n.cp->state().vp.current.depth.func == 3 && n.cp->pc() == m.cp->pc(),
	      "a state saved and loaded");

	m.run({ 0x00080083, END });	/* 083 without its bit 12: a material base of 8 */
	check(m.cp->state().mat.base == 8, "a list sets a base");
	m.run({ END });
	check(m.cp->state().mat.base == 0, "which the next launch clears");
	check(m.cp->state().vp.current.depth.func == 3, "the current viewport outlives the list");
	m.cp->reset();
	check(m.cp->state().vp.current.depth.func == 0 && m.cp->pc() == 0, "power-on clears everything");
}

/* The size of a model on the screen, for the jumps of condition 5: the
 * same in a view turned over (021 with F below 0, the rear-view mirror of
 * SGNASCAR) */
void test_lod_size_in_a_view_turned_over()
{
	for (const float distance : { 1000.0f, -1000.0f }) {
		Machine m;
		const uint32_t large = LIST + 0x84;

		m.write(large, { 0x00000991, 0x00000042, END });
		m.run({ 0x00000021, f(distance), f(500.0f), f(0.1f),	/* the width from F */
		        0x00030004, 0x00030003,				/* commit and recall viewport 3 */
		        0x00000161, f(1.0f), f(0.0f), f(0.0f),	/* the modelview: none */
		        0x00010161, f(0.0f), f(1.0f), f(0.0f),
		        0x00020161, f(0.0f), f(0.0f), f(1.0f),
		        0x00030161, f(0.0f), f(0.0f), f(0.0f),
		        0x00000561, f(0.0f), f(0.0f), f(-100.0f),	/* a model 100 units ahead */
		        0x00000055, f(1.0f),				/* of radius 1: 20 pixels wide */
		        0x012C5012, large >> 2,				/* jump from 300/16 pixels on */
		        0x00000991, 0x00000011,
		        END });
		check(m.ended && m.cp->fb_config().clear[0] == 0x42,
		      distance > 0 ? "a model large enough on the screen" : "and in a view turned over");
	}
}

void test_registers()
{
	Machine m;
	uint32_t value = 0;

	check(m.cp->write_register(0x15000070, LIST) == Antarctic::Written::DONE &&
	      m.cp->write_register(0x15000074, STACK) == Antarctic::Written::DONE,
	      "the start address and the stack pointer are registers");
	check(m.cp->write_register(0x15000058, 3) == Antarctic::Written::CONTROL && m.cp->list_asked(),
	      "15000058 = 3 asks for a list");
	m.write(LIST, { END });
	m.cp->set_log(nullptr);
	m.cp->begin_list();
	m.cp->start_list();
	uint32_t budget = 10;
	check(m.cp->pc() == LIST && m.cp->run(budget), "the list starts where 15000070 tells");
	m.cp->list_ended();
	check(!m.cp->list_asked(), "the two bits go off when the list ends");

	check(!m.cp->irq_line(), "no cause, no interrupt");
	m.cp->raise(Antarctic::IRQ_ANALYSIS_END);
	check(!m.cp->irq_line(), "nothing interrupts before the mask is written");
	m.cp->write_register(0x15000084, 0x72);
	check(m.cp->irq_line(), "a cause let through holds the line");
	check(m.cp->write_register(0x15000084, 0x04) == Antarctic::Written::CAUSES && !m.cp->irq_line(),
	      "a bit set in 15000084 masks its cause");
	m.cp->write_register(0x15000084, 0);
	m.cp->write_register(0x15000088, ~0x04u);
	check(m.cp->read_register(0x15000088, value) && value == 0 && !m.cp->irq_line(),
	      "a cause is cleared by writing it back as 0");
	m.cp->set_display_irq(true);
	check(m.cp->read_register(0x15000088, value) && value == 0x80, "bit 7 follows the display chip");

	check(!m.cp->read_register(0x15000058, value), "15000058 is not read");
	check(m.cp->write_register(0x150000FC, 0) == Antarctic::Written::UNKNOWN, "a register that is not known");
	check(m.cp->read_register(0x15002000, value) && value == 0, "15002000 reads 0: the second main board");
}

/* A 16x16 texture, 512 bytes on the bus, to the first slot of bank 1 */
void test_texture_transfer()
{
	Machine m;
	const uint32_t table = 0x483FC000, texels = 0x48100000;
	/* What the board is told, and the texture RAM as it is then */
	struct Told {
		const Machine *machine;
		int bank = -1;
		uint16_t texel = 0xFFFF;
	} told = { &m };
	uint32_t value = 0;

	for (uint32_t i = 0; i < 256; i++) {
		const uint16_t texel = static_cast<uint16_t>(0x8000 | i);

		std::memcpy(&m.cmdram[(texels & 0x3FFFFF) + i * 2], &texel, 2);
	}
	m.write(table, { texels, 512, 0x0000C080, 1 });
	m.cp->set_log(nullptr);
	m.cp->set_before_texture_write([](void *ctx, unsigned bank, uint32_t, uint32_t, uint32_t, uint32_t) {
		Told *t = static_cast<Told *>(ctx);

		t->bank = static_cast<int>(bank);
		std::memcpy(&t->texel, &t->machine->texram[bank][2], 2);
	}, &told);

	m.cp->write_register(0x1500000C, table);
	m.cp->write_register(0x15000010, 1);
	check(!m.cp->step_texture_transfer(), "no transfer before it is asked");
	m.cp->write_register(0x15000014, 1);
	check(m.cp->read_register(0x15000014, value) && value == 1, "the transfer reads busy");
	check(m.cp->step_texture_transfer(), "one entry: the transfer ends at the first step");
	check(m.cp->read_register(0x15000014, value) && value == 0, "then it reads ready");
	check(m.cp->read_register(0x15000088, value) && (value & Antarctic::IRQ_TRANSFER_END), "and its end is a cause");
	check(m.cp->read_register(0x15000010, value) && value == 1, "the length register keeps its value");
	check(told.bank == 1, "the board is told which bank is written");
	check(told.texel == 0, "before the texels land");

	/* The two texels of a 32-bit word are swapped in the texture RAM. */
	uint16_t first, second;
	std::memcpy(&first, &m.texram[1][2], 2);
	std::memcpy(&second, &m.texram[1][0], 2);
	check(first == 0x8000 && second == 0x8001, "the texels land in the bank, two by two swapped");
	uint16_t next_row;
	std::memcpy(&next_row, &m.texram[1][4096 + 2], 2);
	check(next_row == 0x8010, "a row of the texture RAM is 2048 texels long");
}

/* A 16x16 texture and its four smaller levels, 682 bytes, at a slot (the
 * third word of the entry): each texel the transfer changes lies in a
 * rectangle of its bank the board was told of before it landed. */
void test_texture_levels_told(uint32_t slot)
{
	Machine m;
	const uint32_t table = 0x483FC000, texels = 0x48100000;
	struct Rect {
		unsigned bank;
		uint32_t x, y, w, h;
	};
	struct Told {
		const Machine *machine;
		std::vector<Rect> rects{};
		bool before = true;
	} told = { &m };

	for (uint32_t i = 0; i < 341; i++) {
		const uint16_t texel = static_cast<uint16_t>(0x8000 | i);

		std::memcpy(&m.cmdram[(texels & 0x3FFFFF) + i * 2], &texel, 2);
	}
	m.write(table, { texels, 682, slot, 1 });
	m.cp->set_log(nullptr);
	m.cp->set_before_texture_write([](void *ctx, unsigned bank, uint32_t x, uint32_t y, uint32_t w, uint32_t h) {
		Told *t = static_cast<Told *>(ctx);

		t->rects.push_back({ bank, x, y, w, h });
		for (uint32_t ty = y; ty < y + h; ty++)
			for (uint32_t tx = x; tx < x + w; tx++)
				if (t->machine->texram[bank][ty * 4096 + tx * 2] | t->machine->texram[bank][ty * 4096 + tx * 2 + 1])
					t->before = false;
	}, &told);

	m.cp->write_register(0x1500000C, table);
	m.cp->write_register(0x15000010, 1);
	m.cp->write_register(0x15000014, 1);
	check(m.cp->step_texture_transfer(), "the transfer of five levels ends at the first step");
	check(told.rects.size() == 5, "the board is told of each level");
	check(told.before, "before its texels land");

	unsigned changed = 0, outside = 0;
	for (unsigned bank = 0; bank < 2; bank++) {
		for (uint32_t y = 0; y < 1024; y++) {
			for (uint32_t x = 0; x < 2048; x++) {
				if (!(m.texram[bank][y * 4096 + x * 2] | m.texram[bank][y * 4096 + x * 2 + 1]))
					continue;
				changed++;
				bool in = false;
				for (const Rect &r : told.rects)
					in |= r.bank == bank && x >= r.x && x < r.x + r.w && y >= r.y && y < r.y + r.h;
				outside += !in;
			}
		}
	}
	check(changed == 341, "the five levels land, 341 texels");
	check(outside == 0, "every texel written was told, in its bank");
}

/* The lights of the first mesh of the list */
const odin_lightset_t &lights_of(const Machine &m)
{
	return m.list->ls_list[m.mesh(1, 0).ls_index];
}

void test_lightsets()
{
	Machine m;

	/* Light 1 is red when the set 5 takes its number, green when the
	 * set is recalled. */
	m.run({ 0x00000051, 0x000000FF,
	        0x00008961, f(0.0f), f(0.0f), f(1.0f),
	        0x00010104,
	        0x00051064, 0x00000001, 0x00000000, 0x00000000,
	        0x00000051, 0x0003FC00,
	        0x00010104,
	        0x00051043,
	        OPAQUE, VERTEX(0.0f, 0.0f), VERTEX(1.0f, 0.0f), VERTEX_CLOSING(0.0f, 1.0f),
	        END });
	check(lights_of(m).lights[0].diffuse[0] == 0 && lights_of(m).lights[0].diffuse[1] == 0xFF,
	      "a lightset keeps the number of a light, read when the set is recalled");

	/* The next list recalls the set without committing it again: the
	 * light of the day. */
	m.run({ 0x00000051, 0x0FF00000,
	        0x00010104,
	        0x00051043,
	        OPAQUE, VERTEX(0.0f, 0.0f), VERTEX(1.0f, 0.0f), VERTEX_CLOSING(0.0f, 1.0f),
	        END });
	check(lights_of(m).lights[0].diffuse[1] == 0 && lights_of(m).lights[0].diffuse[2] == 0xFF,
	      "a set left by another list names the light as it is now");

	Machine n;
	n.run({ 0x00000051, 0x000000FF,
	        0x00000104,
	        0x00071043,
	        OPAQUE, VERTEX(0.0f, 0.0f), VERTEX(1.0f, 0.0f), VERTEX_CLOSING(0.0f, 1.0f),
	        END });
	check(lights_of(n).lights[0].flags == 0 && lights_of(n).lights[0].diffuse[0] == 0,
	      "a set never committed has no light");
}

void test_light_position_with_w()
{
	Machine m;

	/* The position of the light of the SEGA init program: 961 with the
	 * bits 14 and 13. */
	m.run({ 0x00016961, f(100.0f), f(100.0f), f(100.0f),
	        0x00020104,
	        0x00061064, 0x00000002, 0x00000000, 0x00000000,
	        0x00061043,
	        OPAQUE, VERTEX(0.0f, 0.0f), VERTEX(1.0f, 0.0f), VERTEX_CLOSING(0.0f, 1.0f),
	        END });
	const odin_light_t &light = lights_of(m).lights[0];
	check((light.flags & ODIN_LIGHT_HAS_POSITION) && light.position[0] == 100.0f &&
	      light.position[1] == 100.0f && light.position[2] == 100.0f,
	      "a 961 with the bits 14 and 13 gives the position it carries");
}

void test_disassembler()
{
	const uint32_t jump[8] = { 0x00000012, 0x12000043 };
	const uint32_t odd[8] = { 0xFFFF0000 };
	bool unhandled = true;
	std::string text = odin::antarctic_disassemble(0x48000100, jump, false, unhandled);

	check(text.find("CP @48000100 : 00000012 12000043 ........") == 0, "address and words of the instruction");
	check(text.find("jump  @4800010C") != std::string::npos && !unhandled, "the text of a jump");

	text = odin::antarctic_disassemble(0x48000100, odd, true, unhandled);
	check(unhandled && text.find("! M nop") != std::string::npos, "unknown bits and an open mesh are marked");
}

} /* namespace */

int main()
{
	test_sizes_and_end();
	test_invalid();
	test_jump_to_itself();
	test_call_and_return();
	test_strip();
	test_three_lists();
	test_degenerate_and_both_faces();
	test_strip_through_recall();
	test_objects_listed_once();
	test_blending_mode_in_two_words();
	test_texhead_of_0E8_and_second_modelview();
	test_viewport_and_state();
	test_lod_size_in_a_view_turned_over();
	test_registers();
	test_texture_transfer();
	/* The last level, 1x1, starts on an odd texel, then on an even one:
	 * the two texels of a word are swapped. */
	test_texture_levels_told(0x0000C391);
	test_texture_levels_told(0x0000C380);
	test_lightsets();
	test_light_position_with_w();
	test_disassembler();
	std::printf("%d checks, %d failures\n", g_checks, g_failures);
	return g_failures == 0 ? 0 : 1;
}
