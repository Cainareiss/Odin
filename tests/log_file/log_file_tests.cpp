/*
 *  ██████╗ ██████╗ ██╗███╗   ██╗
 * ██╔═══██╗██╔══██╗██║████╗  ██║
 * ██║   ██║██║  ██║██║██╔██╗ ██║
 * ██║   ██║██║  ██║██║██║╚██╗██║
 * ╚██████╔╝██████╔╝██║██║ ╚████║
 *  ╚═════╝ ╚═════╝ ╚═╝╚═╝  ╚═══╝
 *
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

/* Unit tests for the log file: what Odin says on stdout and stderr, by
 * the stdio of C and straight to the files, is in odin.log and still on
 * the terminal, stdout and stderr apart there; the log of the launch
 * before is kept; the file stops at its size, the terminal gets all; the
 * last words before a crash are in it; the file by default and ODIN_LOG.
 *
 * Build: g++ -std=c++20 -O2 -Wall -Wextra -Wpedantic -Wconversion -Wshadow -pthread \
 *            -I src tests/log_file/log_file_tests.cpp src/odin/host/app/log_file.cpp src/odin/host/app/user_files.cpp \
 *            -o log_file_tests
 * It writes its files in the folder given as its argument, or the current
 * one; its own report goes to stderr... of a terminal it takes back. Exit
 * status is 0 when every check passes. */

#include "odin/host/app/log_file.hpp"

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string>

#include <csignal>

#include <fcntl.h>
#include <sys/wait.h>
#include <unistd.h>

namespace {

using odin::LogFile;

int g_checks, g_failures;
std::string g_report;

void check(bool ok, const char *what)
{
	g_checks++;
	if (!ok) {
		g_failures++;
		g_report += std::string("FAILED: ") + what + "\n";
	}
}

std::string read_all(const std::string &path)
{
	std::ifstream in(path, std::ios::binary);
	std::stringstream text;

	text << in.rdbuf();
	return text.str();
}

bool has(const std::string &text, const std::string &part)
{
	return text.find(part) != std::string::npos;
}

/* stdout and stderr into files of their own, as a terminal that keeps what
 * it shows */
void terminal(const std::string &dir, int saved[2])
{
	saved[0] = dup(1);
	saved[1] = dup(2);
	dup2(open((dir + "/terminal-out.txt").c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644), 1);
	dup2(open((dir + "/terminal-err.txt").c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644), 2);
}

void test_where()
{
	setenv("XDG_DATA_HOME", "/data", 1);
	unsetenv("ODIN_LOG");
	check(LogFile::path() == "/data/odin/odin.log", "by default: odin.log in the data folder");
	setenv("ODIN_LOG", "/elsewhere/mine.log", 1);
	check(LogFile::path() == "/elsewhere/mine.log", "ODIN_LOG=<file>: that file");
	setenv("ODIN_LOG", "none", 1);
	check(LogFile::path().empty(), "ODIN_LOG=none: none");
	check(LogFile::previous("/a/odin.log") == "/a/odin-previous.log", "the one before: odin-previous.log");
	check(LogFile::previous("/a/mine") == "/a/mine.previous", "or <file>.previous");
	unsetenv("ODIN_LOG");
	unsetenv("XDG_DATA_HOME");
}

void test_log(const std::string &dir)
{
	const std::string path = dir + "/odin.log";
	int saved[2];

	{
		std::ofstream before(path);
		before << "the launch before\n";
	}
	terminal(dir, saved);
	check(LogFile::start(path, 200), "the log starts");
	std::printf("printf to stdout\n");
	std::fprintf(stderr, "fprintf to stderr\n");
	check(write(1, "straight to fd 1\n", 17) == 17, "a write to fd 1");
	check(write(2, "straight to fd 2\n", 17) == 17, "a write to fd 2");
	/* The two threads in the file in their own time: the first lines, then
	 * more than the file takes */
	usleep(200000);
	for (int i = 0; i < 20; i++)
		std::printf("a long line, more than the file takes: %d\n", i);
	LogFile::stop();
	std::fflush(stdout);
	dup2(saved[0], 1);
	dup2(saved[1], 2);

	const std::string log = read_all(path), out = read_all(dir + "/terminal-out.txt");
	const std::string err = read_all(dir + "/terminal-err.txt");

	check(has(log, "printf to stdout\n") && has(log, "fprintf to stderr\n"), "stdout and stderr in the log");
	check(has(log, "straight to fd 1\n") && has(log, "straight to fd 2\n"), "and what goes straight to the files");
	check(has(log, "odin.log stops here") && log.size() < 300, "the log stops at its size");
	check(!has(log, "the launch before"), "the log starts empty");
	check(read_all(dir + "/odin-previous.log") == "the launch before\n", "the one before kept");
	check(has(out, "printf to stdout\n") && has(out, "straight to fd 1\n") && has(out, ": 19\n"),
	      "the terminal gets all of stdout");
	check(has(err, "fprintf to stderr\n") && has(err, "straight to fd 2\n") && !has(err, "printf to stdout"),
	      "and stderr apart");
	check(!has(out, "fprintf to stderr"), "stdout without stderr");
	check(!LogFile::start("", 200), "no file: no log");
}

/* A child says its last words and aborts at once, as a fatal error of
 * the board does */
void test_crash(const std::string &dir)
{
	const std::string path = dir + "/crash.log";
	const pid_t child = fork();

	if (child == 0) {
		const int null = open("/dev/null", O_WRONLY);

		dup2(null, 1);
		dup2(null, 2);
		LogFile::start(path);
		std::fprintf(stderr, "last words\n");
		std::abort();
	}

	int status = 0;

	waitpid(child, &status, 0);
	check(WIFSIGNALED(status) && WTERMSIG(status) == SIGABRT, "the child ends as abort ends it");
	check(read_all(path) == "last words\n", "its last words are in the log");
}

} /* namespace */

int main(int argc, char **argv)
{
	const std::string dir = argc > 1 ? argv[1] : ".";

	test_where();
	test_crash(dir);
	test_log(dir);
	std::printf("%s%d checks, %d failures\n", g_report.c_str(), g_checks, g_failures);
	return g_failures ? 1 : 0;
}
