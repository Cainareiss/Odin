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

/* Unit tests for the settings of the user: a key changed in the text of
 * odin.ini with the rest of it kept, and in a real file; the keys Odin
 * knows, as comments at their values by default.
 *
 * Build: g++ -std=c++20 -O2 -Wall -Wextra -Wpedantic -Wconversion -Wshadow \
 *            -I src tests/settings/settings_tests.cpp src/odin/settings.cpp \
 *            src/odin/save_folder.cpp -o settings_tests
 * Exit status is 0 when every check passes. */

#include "odin/settings.hpp"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

namespace {

using odin::Settings;

int g_checks, g_failures;

void check(bool ok, const char *what)
{
	g_checks++;
	if (!ok) {
		g_failures++;
		std::printf("FAILED: %s\n", what);
	}
}

void check_text(const std::string &got, const std::string &want, const char *what)
{
	check(got == want, what);
	if (got != want)
		std::printf("  got:\n%s  want:\n%s", got.c_str(), want.c_str());
}

void test_changed()
{
	check_text(Settings::changed("# mine\n[outputs]\nnetwork = on\n\n[display]\n; note\nscale = 2\n", "display", "scale", "3"),
	           "# mine\n[outputs]\nnetwork = on\n\n[display]\n; note\nscale = 3\n",
	           "a value changes in its line, the comments and the order stay");
	check_text(Settings::changed("[display]\nscale = 2\n\n[outputs]\nnetwork = on\n", "display", "window", "2"),
	           "[display]\nscale = 2\nwindow = 2\n\n[outputs]\nnetwork = on\n",
	           "a new key goes after the last line of its section");
	check_text(Settings::changed("[outputs]\nnetwork = on\n", "display", "scale", "2"),
	           "[outputs]\nnetwork = on\n\n[display]\nscale = 2\n", "a new section goes at the end");
	check_text(Settings::changed("", "display", "scale", "2"), "[display]\nscale = 2\n", "in an empty file");
	check_text(Settings::changed("[display]\nscale = 2\nscale = 4\n", "display", "scale", "3"), "[display]\nscale = 3\n",
	           "the last line takes the value, the ones before it go");
	check_text(Settings::changed("[display]\nscale = 2\nwindow = 2\n", "display", "scale", std::nullopt),
	           "[display]\nwindow = 2\n", "a key taken out");
	check_text(Settings::changed("[display]\nwindow = 2\n", "display", "scale", std::nullopt), "[display]\nwindow = 2\n",
	           "a key that is not there taken out: nothing changes");
	check_text(Settings::changed("[display]\r\nscale = 2\r\n", "display", "window", "3"),
	           "[display]\r\nscale = 2\r\nwindow = 3\r\n", "the line ends of Windows stay");
	check_text(Settings::changed("not a setting\n[display]\nscale=2\n", "display", "scale", "5"),
	           "not a setting\n[display]\nscale = 5\n", "a line that is not a setting stays");
	check_text(Settings::changed("[display]\n# scale = 9\n", "display", "scale", "2"), "[display]\n# scale = 9\nscale = 2\n",
	           "a key in a comment is a comment");
	check_text(Settings::changed("[display]\nscale = 2", "display", "scale", "3"), "[display]\nscale = 3\n",
	           "a last line without its end");
	check_text(Settings::changed("[display.x]\nscale = 1\n", "display", "scale", "2"),
	           "[display.x]\nscale = 1\n\n[display]\nscale = 2\n", "another section with a longer name is another");
	check_text(Settings::changed("top = 1\n[display]\nscale = 2\n", "", "top", "4"), "top = 4\n[display]\nscale = 2\n",
	           "a key before the first section");

	Settings settings;

	settings.parse(Settings::changed("[display]\nscale = 2\nscale = 4\n", "display", "scale", "6"), "test");
	check(settings.section("display").size() == 1 && settings.section("display")[0].second == "6",
	      "what is written reads back");
}

/* A key Odin knows: one line, its comment when it has no value; the file
 * made whole */
void test_known()
{
	const std::string top = "# The settings of Odin. A line that starts with # is a setting at its value\n"
	                        "# by default: take the # away and change the value to change it. INSTALL\n"
	                        "# tells what each one does.\n\n";
	const std::vector<Settings::Known> keys = { { "display", "scale", "1" }, { "display", "window", "1" },
	                                            { "sound", "device", "" }, { "sound", "volume", "100" } };

	check_text(Settings::changed("[display]\n# scale = 1\nwindow = 2\n", "display", "scale", "2", "1"),
	           "[display]\nscale = 2\nwindow = 2\n", "a value in place of the comment");
	check_text(Settings::changed("[display]\nscale = 2\nwindow = 2\n", "display", "scale", std::nullopt, "1"),
	           "[display]\n# scale = 1\nwindow = 2\n", "back to its value by default: the comment again");
	check_text(Settings::changed("[display]\n# scale = 1\nscale = 3\n", "display", "scale", std::nullopt, "1"),
	           "[display]\n# scale = 1\n", "one line for the key");
	check_text(Settings::changed("", "sound", "device", std::nullopt, ""), "[sound]\n# device =\n",
	           "a value by default that is empty");
	check_text(Settings::changed("[display]\n# scale is big = 2\n", "display", "scale", "2", "1"),
	           "[display]\n# scale is big = 2\nscale = 2\n", "a comment of the user stays");

	const std::string whole = Settings::completed("", keys);

	check_text(whole, top + "[display]\n# scale = 1\n# window = 1\n\n[sound]\n# device =\n# volume = 100\n",
	           "a file made whole: a word on top, each key as a comment");
	check_text(Settings::completed(whole, keys), whole, "a whole file stays as it is");
	check_text(Settings::completed("[sound]\nvolume = 80\n[display]\n# scale = 4\n# my note = 2\n", keys),
	           top + "[sound]\nvolume = 80\n# device =\n[display]\n# scale = 1\n# my note = 2\n# window = 1\n",
	           "the values of the user stay, a comment of a value by default is made right, what lacks is added");
	check_text(Settings::completed("[display]\r\nscale = 2\r\n", { keys[0], keys[1] }),
	           "# The settings of Odin. A line that starts with # is a setting at its value\r\n"
	           "# by default: take the # away and change the value to change it. INSTALL\r\n"
	           "# tells what each one does.\r\n\r\n[display]\r\nscale = 2\r\n# window = 1\r\n",
	           "the line ends of the file");

	Settings settings;

	settings.parse(whole, "test");
	check(settings.section("display").empty() && settings.section("sound").empty(), "the comments are no values");
}

void test_set()
{
	Settings settings;

	settings.parse("[controls.von4]\nstart = key:1\nstart = key:2\ncoin1 = key:5\n", "test");
	settings.set("controls.von4", "start", "key:Return");
	check(settings.section("controls.von4").size() == 2 && settings.section("controls.von4")[1].second == "key:Return",
	      "set here: the lines of the key go, the new one is the last");
	settings.set("controls.von4", "coin1", std::nullopt);
	check(settings.section("controls.von4").size() == 1, "taken out here");
	settings.set("display", "scale", "2");
	check(settings.section("display").size() == 1, "in a new section");
}

std::string read(const std::filesystem::path &path)
{
	std::ifstream file(path, std::ios::binary);
	std::stringstream text;

	text << file.rdbuf();
	return text.str();
}

void test_change()
{
	const std::filesystem::path folder = std::filesystem::temp_directory_path() / "odin-settings-tests";
	const std::filesystem::path path = folder / "deeper" / "odin.ini";
	std::error_code error;

	std::filesystem::remove_all(folder, error);
	Settings settings;

	check(settings.change(path.string(), "display", "scale", "2"), "a file is made, with its folder");
	check(read(path) == "[display]\nscale = 2\n", "with the key");
	{
		std::ofstream file(path, std::ios::binary | std::ios::app);

		file << "# written by hand meanwhile\n";
	}
	check(settings.change(path.string(), "display", "window", "3"), "a second change");
	check(read(path) == "[display]\nscale = 2\n# written by hand meanwhile\nwindow = 3\n",
	      "the file is read again: what was written meanwhile stays");
	check(settings.section("display").size() == 2 && settings.section("display")[1].second == "3",
	      "and the settings in memory follow");
	check(!std::filesystem::exists(path.string() + ".new", error), "no file is left beside it");
	check(settings.change(path.string(), "display", "scale", std::nullopt) &&
	      read(path) == "[display]\n# written by hand meanwhile\nwindow = 3\n", "a key taken out of the file");
	std::filesystem::remove_all(folder, error);
}

} /* namespace */

int main()
{
	test_changed();
	test_known();
	test_set();
	test_change();

	std::printf("%d checks, %d failures\n", g_checks, g_failures);
	return g_failures ? 1 : 0;
}
