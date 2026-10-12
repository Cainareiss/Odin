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
		list = cp->begin_list(false);
		cp->start(LIST, STACK, 0);
		ended = cp->run(budget);
		cp->end_open_mesh();
	}
	const odin_mesh_t &mesh(uint32_t type, uint32_t index) const
	{
		return list->mesh_list[0][type][index];
	}
	const odin_vertex_body_t &vertex(const odin_mesh_t &m, uint32_t index) const
	{
		return list->verts.all[m.first + index];
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
	check(n.cp->run(10), "and ends at a later call");
}

void test_invalid()
{
	Machine m;

	m.run({ 0x000001FF, END });
	check(m.ended, "an instruction that does not exist ends the list");

	Machine n;
	n.cp->set_log(nullptr);
	n.list = n.cp->begin_list(false);
	n.cp->start(0x20000000, STACK, 0);
	check(n.cp->run(10), "a program counter out of the two memories ends the list");
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
	check(m.vertex(mesh, 0).diffuse[0] == 0x10 && m.vertex(mesh, 0).diffuse[1] == 0x20 &&
	      m.vertex(mesh, 0).diffuse[2] == 0x30, "the vertices take the 091 colour");
	check(m.vertex(mesh, 0).alpha == 0xFF, "vertex alpha FF times polygon alpha FF");
	check(m.vertex(mesh, 0).second_mv == 0, "header E: the modelview, whole");
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

/* The grid of the BOOTROM C.R.T. test: the 0E8 names its texhead, and its
 * vertices follow the second modelview without its 3x3. */
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

	m.cp->frame_start();
	check(m.cp->state().vp.current.depth.func == 3, "the current viewport outlives the frame");
	m.cp->reset();
	check(m.cp->state().vp.current.depth.func == 0 && m.cp->pc() == 0, "power-on clears everything");
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
	m.cp->begin_list(false);
	m.cp->start_list();
	check(m.cp->pc() == LIST && m.cp->run(10), "the list starts where 15000070 tells");
	m.cp->list_ended();
	check(!m.cp->list_asked(), "the two bits go off when the list ends");

	check(!m.cp->irq_line(), "no cause, no interrupt");
	m.cp->raise(Antarctic::IRQ_ANALYSIS_END);
	check(m.cp->irq_line(), "a cause holds the line");
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
	int written = -1;
	uint32_t value = 0;

	for (uint32_t i = 0; i < 256; i++) {
		const uint16_t texel = static_cast<uint16_t>(0x8000 | i);

		std::memcpy(&m.cmdram[(texels & 0x3FFFFF) + i * 2], &texel, 2);
	}
	m.write(table, { texels, 512, 0x0000C080, 1 });
	m.cp->set_log(nullptr);
	m.cp->set_texture_written([](void *ctx, unsigned bank) { *static_cast<int *>(ctx) = static_cast<int>(bank); }, &written);

	m.cp->write_register(0x1500000C, table);
	m.cp->write_register(0x15000010, 1);
	check(!m.cp->step_texture_transfer(), "no transfer before it is asked");
	m.cp->write_register(0x15000014, 1);
	check(m.cp->read_register(0x15000014, value) && value == 1, "the transfer reads busy");
	check(m.cp->step_texture_transfer(), "one entry: the transfer ends at the first step");
	check(m.cp->read_register(0x15000014, value) && value == 0, "then it reads ready");
	check(m.cp->read_register(0x15000088, value) && (value & Antarctic::IRQ_TRANSFER_END), "and its end is a cause");
	check(m.cp->read_register(0x15000010, value) && value == 1, "the length register keeps its value");
	check(written == 1, "the board is told which bank was written");

	/* The two texels of a 32-bit word are swapped in the texture RAM. */
	uint16_t first, second;
	std::memcpy(&first, &m.texram[1][2], 2);
	std::memcpy(&second, &m.texram[1][0], 2);
	check(first == 0x8000 && second == 0x8001, "the texels land in the bank, two by two swapped");
	uint16_t next_row;
	std::memcpy(&next_row, &m.texram[1][4096 + 2], 2);
	check(next_row == 0x8010, "a row of the texture RAM is 2048 texels long");
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
	test_call_and_return();
	test_strip();
	test_degenerate_and_both_faces();
	test_strip_through_recall();
	test_texhead_of_0E8_and_second_modelview();
	test_viewport_and_state();
	test_registers();
	test_texture_transfer();
	test_disassembler();
	std::printf("%d checks, %d failures\n", g_checks, g_failures);
	return g_failures == 0 ? 0 : 1;
}
