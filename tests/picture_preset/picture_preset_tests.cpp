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

/* Unit tests for the texts of a filter of the picture as 86Box reads them:
 * the keys of a .glslp preset, a part of a pass, its parameters; then the
 * presets of Lola Retro in filters/ (run from the repo).
 *
 * Build: g++ -std=c++20 -O2 -Wall -Wextra -Wpedantic -Wconversion -Wshadow -I src \
 *            tests/picture_preset/picture_preset_tests.cpp src/odin/picture_preset.cpp \
 *            -o picture_preset_tests
 * Exit status is 0 when every check passes. */

#include "odin/picture_preset.hpp"

#include <cmath>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <string>

namespace {

namespace preset = odin::preset;

int g_checks, g_failures;

void check(bool ok, const char *what)
{
	g_checks++;
	if (!ok) {
		g_failures++;
		std::printf("FAILED: %s\n", what);
	}
}

std::string text_of(const char *path)
{
	std::ifstream file(path, std::ios::binary);

	return std::string(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
}

void test_keys()
{
	const auto keys = preset::read_keys("# a comment\n"
	                                    "; another\n"
	                                    "shaders = 2\n"
	                                    "shader0 = shaders/a.glsl # a comment after\n"
	                                    "  filter_linear1 = true\n"
	                                    "parameters = \"A;B\"\n"
	                                    "A = 0.5\n"
	                                    "A = 0.75\n"
	                                    "empty =\n"
	                                    "[section]\n"
	                                    "B = 1\n");

	check(keys.at("shaders") == "2", "a key");
	check(keys.at("shader0") == "shaders/a.glsl", "a value cut at its #");
	check(keys.at("filter_linear1") == "true", "spaces before a key");
	check(keys.at("parameters") == "A;B", "quotes taken away");
	check(keys.at("A") == "0.5", "the first of two equal names");
	check(!keys.contains("empty"), "a key without a value");
	check(!keys.contains("B"), "a [section] ends the keys");
	check(preset::key_on(keys, "filter_linear1") && !preset::key_on(keys, "filter_linear2"), "true, or none");
	check(preset::key_on(preset::read_keys("x = 1\n"), "x") && !preset::key_on(preset::read_keys("x = 0\n"), "x"),
	      "a number as true or false");
}

void test_part()
{
	const std::string source = "// a pass\n#version 150 core\n#pragma parameter P \"a parameter\" 1 0 2 0.1\n"
	                           "uniform float P;\nvoid main() {}\n";
	const std::string vertex = preset::part(source, "VERTEX");

	check(vertex.starts_with("#version 150\n"), "the version line first, without what follows its number");
	check(vertex.find("#define VERTEX\n") != std::string::npos && vertex.find("#define FRAGMENT") == std::string::npos,
	      "the part");
	check(vertex.find("#define PARAMETER_UNIFORM\n") != std::string::npos, "the parameters as uniforms");
	check(vertex.find("GL_ARB_shading_language_420pack") != std::string::npos, "the extension of 86Box");
	check(vertex.find("#pragma") == std::string::npos && vertex.find("#version", 1) == std::string::npos,
	      "the #pragma and the old #version blanked");
	check(vertex.find("#line 1\n// a pass\n") != std::string::npos, "the file from its first line");
}

void test_parameters()
{
	std::vector<std::pair<std::string, float>> parameters;
	const std::string long_text(64, 'x');

	preset::declared("#pragma parameter A \"first\" 1.5 0 2 0.1\n#pragma parameter B \"second\" -0.25 -1 1 0.05\n",
	                 parameters);
	preset::declared("#pragma parameter A \"again\" 9 0 9 1\n#pragma parameter C \"" + long_text + "\" 1 0 1 1\n"
	                 "#pragma parameter D \"\" 1 0 1 1\n#pragma parameter " + long_text + " \"long name\" 1 0 1 1\n",
	                 parameters);
	check(parameters.size() == 2, "a parameter declared again, of a name or a description too long, or without "
	      "a description: not taken");
	check(parameters[0].first == "A" && parameters[0].second == 1.5f, "its default");
	check(parameters[1].first == "B" && parameters[1].second == -0.25f, "a negative default");
}

void test_lola()
{
	const auto keys = preset::read_keys(text_of("filters/lola-retro/pc-monitor-vga-premium.glslp"));

	check(keys.contains("shaders") && keys.at("shaders") == "7", "Lola Retro: seven passes");
	check(keys.contains("shader5") && keys.at("shader5") == "shaders/crt.glsl", "its tube");
	check(keys.contains("LR_MON_PITCH") && keys.at("LR_MON_PITCH") == "0.25", "the dot pitch of the premium one");
	check(!keys.contains("scale_type6"), "the last pass without a scale");

	std::vector<std::pair<std::string, float>> parameters;

	for (const char *pass : { "encode", "decode", "glow-h", "glow-v", "beam", "crt", "present" })
		preset::declared(text_of(("filters/lola-retro/shaders/" + std::string(pass) + ".glsl").c_str()), parameters);
	check(parameters.size() == 35, "its 35 parameters");

	const auto monitor = preset::read_keys(text_of("filters/lola-retro/sega-monitor-29.glslp"));
	const auto projector = preset::read_keys(text_of("filters/lola-retro/sega-projector-50.glslp"));

	check(monitor.contains("shaders") && monitor.at("shaders") == "7" && monitor.contains("LR_MON_SIZE") &&
	      monitor.at("LR_MON_SIZE") == "28" && monitor.at("LR_MASK_TYPE") == "2", "Odin's 29-inch monitor");
	check(projector.contains("shaders") && projector.at("shaders") == "7" && projector.contains("LR_MASK_TYPE") &&
	      projector.at("LR_MASK_TYPE") == "4" && projector.at("LR_CURV_X") == "0", "Odin's 50-inch projector");
	check(monitor.at("parameters").find("LR_OVERSCAN") != std::string::npos &&
	      projector.at("parameters").find("LR_PHOS") != std::string::npos, "the values they set, listed");
}

} /* namespace */

int main()
{
	test_keys();
	test_part();
	test_parameters();
	test_lola();
	std::printf("%d checks, %d failures\n", g_checks, g_failures);
	return g_failures ? 1 : 0;
}
