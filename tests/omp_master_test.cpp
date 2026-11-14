// Build: c++ -std=c++11 -O2 -pthread -Isrc/include tests/omp_master_test.cpp
// With OpenMP: add -fopenmp (g++), e.g. g++ -std=c++11 -O2 -fopenmp -Isrc/include tests/omp_master_test.cpp
// Run it once as is and once with SPALM_OMP_MASTER=0 (every task inline).
#include "omp_master.hpp"

#include <algorithm>
#include <atomic>
#include <future>
#include <iterator>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <set>
#include <stdexcept>
#include <thread>
#include <vector>

#ifdef _OPENMP
#include <omp.h>
#endif

#define CHECK(cond)                                                                                                    \
	do {                                                                                                               \
		if (!(cond)) {                                                                                                 \
			std::fprintf(stderr, "omp_master_test: check failed at line %d: %s\n", __LINE__, #cond);                   \
			std::exit(1);                                                                                              \
		}                                                                                                              \
	} while (0)

static bool Enabled() {
	return spalm_omp::Instance() != nullptr;
}

// Tasks run on one thread, not the caller's (inline when disabled).
static void CheckSameThread() {
	const std::thread::id caller = std::this_thread::get_id();
	std::thread::id first;
	for (int i = 0; i < 5; i++) {
		std::thread::id ran;
		spalm_omp::RunOnMaster([&] { ran = std::this_thread::get_id(); });
		if (i == 0) {
			first = ran;
		}
		CHECK(ran == first);
		CHECK(Enabled() ? ran != caller : ran == caller);
	}
	if (Enabled()) {
		CHECK(spalm_omp::Instance()->Tid() != 0);
	}
}

// Callers on several threads: every task runs, one at a time.
static int CheckCallers() {
	const int callers = 8, calls = 200;
	const bool enabled = Enabled();
	// Plain variables: only safe because the tasks never overlap. Inline
	// (disabled) tasks do overlap, so they only count atomically.
	int counter = 0;
	bool running = false;
	bool overlap = false;
	std::set<std::thread::id> ran_on;
	std::atomic<int> inline_counter {0};
	std::vector<std::thread> threads;
	for (int c = 0; c < callers; c++) {
		threads.emplace_back([&] {
			for (int i = 0; i < calls; i++) {
				spalm_omp::RunOnMaster([&] {
					if (!enabled) {
						inline_counter++;
						return;
					}
					if (running) {
						overlap = true;
					}
					running = true;
					counter++;
					ran_on.insert(std::this_thread::get_id());
					running = false;
				});
			}
		});
	}
	for (auto &t : threads) {
		t.join();
	}
	if (enabled) {
		CHECK(!overlap);
		CHECK(counter == callers * calls);
		CHECK(ran_on.size() == 1);
	} else {
		CHECK(inline_counter.load() == callers * calls);
	}
	return callers * calls;
}

// An exception in a task reaches the caller; the master keeps working.
static void CheckExceptions() {
	bool caught = false;
	try {
		spalm_omp::RunOnMaster([] { throw std::runtime_error("task failed"); });
	} catch (const std::runtime_error &e) {
		caught = std::strcmp(e.what(), "task failed") == 0;
	}
	CHECK(caught);
	int value = 0;
	spalm_omp::RunOnMaster([&] { value = 42; });
	CHECK(value == 42);
}

// A task that calls RunOnMaster runs the inner task inline.
static void CheckNested() {
	std::thread::id outer, inner;
	spalm_omp::RunOnMaster([&] {
		outer = std::this_thread::get_id();
		spalm_omp::RunOnMaster([&] { inner = std::this_thread::get_id(); });
	});
	CHECK(outer == inner);
}

#ifdef _OPENMP
// Thread IDs of one team of `team` threads.
static std::set<std::thread::id> TeamIds(int team) {
	std::set<std::thread::id> ids;
#pragma omp parallel num_threads(team)
	{
#pragma omp critical
		ids.insert(std::this_thread::get_id());
	}
	return ids;
}

static size_t Shared(const std::set<std::thread::id> &a, const std::set<std::thread::id> &b) {
	std::vector<std::thread::id> both;
	std::set_intersection(a.begin(), a.end(), b.begin(), b.end(), std::back_inserter(both));
	return both.size();
}

// One team from each of two caller threads that stay alive, as Finalize and
// Multiply on two DuckDB worker threads; `region` starts it. Returns the
// process's thread count while both callers are still alive.
template <class REGION>
static int TwoCallers(int team, std::set<std::thread::id> ids[2], REGION region) {
	std::promise<void> first_done, second_done, release;
	std::shared_future<void> released = release.get_future().share();
	std::thread first([&] {
		region(team, ids[0]);
		first_done.set_value();
		released.wait();
	});
	first_done.get_future().wait();
	std::thread second([&] {
		region(team, ids[1]);
		second_done.set_value();
		released.wait();
	});
	second_done.get_future().wait();
	const int threads = spalm_omp::ProcessThreads();
	release.set_value();
	first.join();
	second.join();
	return threads;
}

// Through the master, both callers get the same team: one pool. Called
// directly, each caller is a master of its own (libgomp: a pool each).
static void CheckOnePool(int team) {
	omp_set_dynamic(0);
	std::set<std::thread::id> via[2], direct[2];
	const int via_threads = TwoCallers(team, via, [](int n, std::set<std::thread::id> &ids) {
		spalm_omp::RunOnMaster([&] { ids = TeamIds(n); });
	});
	const int direct_threads =
	    TwoCallers(team, direct, [](int n, std::set<std::thread::id> &ids) { ids = TeamIds(n); });
	CHECK(via[0].size() == static_cast<size_t>(team) && via[1].size() == static_cast<size_t>(team));
	if (Enabled()) {
		CHECK(via[0] == via[1]);
	}
	std::printf("omp_master_test: team %d from two live callers: through RunOnMaster %zu of %d threads shared "
	            "(process threads %d); called directly %zu shared (process threads %d)\n",
	            team, Shared(via[0], via[1]), team, via_threads, Shared(direct[0], direct[1]), direct_threads);
}
#endif

int main() {
	CheckSameThread();
	const int calls = CheckCallers();
	CheckExceptions();
	CheckNested();
#ifdef _OPENMP
	CheckOnePool(8);
#endif
	spalm_omp::PrintThreads("test");
	std::printf("omp_master_test: OK (%s, %d calls from 8 threads%s)\n", Enabled() ? "master thread" : "inline",
	            calls,
#ifdef _OPENMP
	            ", OpenMP"
#else
	            ", no OpenMP"
#endif
	);
	return 0;
}
