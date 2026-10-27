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

/* Unit tests for the worker thread: a job runs on another thread, one at a
 * time, and its owner waits for it only when it asks to.
 *
 * Build: g++ -std=c++20 -O2 -Wall -Wextra -Wpedantic -Wconversion -Wshadow -pthread \
 *            -I src tests/worker/worker_tests.cpp src/odin/machine/core/worker.cpp -o worker_tests
 * Exit status is 0 when every check passes. */

#include "odin/machine/core/worker.hpp"

#include <atomic>
#include <chrono>
#include <cstdio>
#include <thread>
#include <vector>

namespace {

using odin::Worker;

int g_checks, g_failures;

void check(bool ok, const char *what)
{
	g_checks++;
	if (!ok) {
		g_failures++;
		std::printf("FAILED: %s\n", what);
	}
}

void nap()
{
	std::this_thread::sleep_for(std::chrono::milliseconds(20));
}

void test_run()
{
	Worker worker;
	std::thread::id where;
	int done = 0;

	worker.run([&] {
		where = std::this_thread::get_id();
		done = 1;
	});
	check(done == 1, "run waits for its job");
	check(where != std::this_thread::get_id(), "a job runs on another thread");

	worker.wait();
	check(true, "nothing to wait for with no job given");
}

void test_post()
{
	Worker worker;
	std::atomic<bool> go(false), ended(false);

	worker.post([&] {
		while (!go)
			std::this_thread::yield();
		ended = true;
	});
	check(!ended, "post does not wait for its job");
	go = true;
	worker.wait();
	check(ended, "wait does");
}

void test_wait_for()
{
	Worker worker;
	std::atomic<bool> go(false), ended(false);

	check(worker.wait_for(std::chrono::milliseconds(0)), "wait_for with no job given: done");
	worker.post([&] {
		while (!go)
			std::this_thread::yield();
		ended = true;
	});
	check(!worker.wait_for(std::chrono::milliseconds(20)), "wait_for a job that goes on: not done");
	check(!ended, "and it did not end it");
	go = true;
	check(worker.wait_for(std::chrono::seconds(10)), "wait_for a job that ends: done");
	check(ended, "once it has ended");
}

void test_one_at_a_time()
{
	Worker worker;
	std::vector<int> order;

	worker.post([&] {
		nap();
		order.push_back(1);
	});
	worker.post([&] { order.push_back(2); });
	/* The owner may read what the first job wrote: it is done. */
	check(!order.empty() && order[0] == 1, "a job is given once the one before is done");
	worker.wait();
	check(order.size() == 2 && order[1] == 2, "and the jobs run in the order they were given");

	/* What the jobs and the owner touch in turn needs no lock. */
	int count = 0;
	for (int i = 0; i < 10000; i++) {
		worker.post([&] { count++; });
		worker.wait();
		count++;
	}
	check(count == 20000, "ten thousand jobs, each waited for");
}

void test_end()
{
	bool done = false;

	{
		Worker worker;

		worker.post([&] {
			nap();
			done = true;
		});
	}
	check(done, "a worker that ends waits for the job given last");
}

} /* namespace */

int main()
{
	test_run();
	test_post();
	test_wait_for();
	test_one_at_a_time();
	test_end();

	std::printf("%d checks, %d failures\n", g_checks, g_failures);
	return g_failures ? 1 : 0;
}
