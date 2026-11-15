#pragma once

// One process-wide thread, the OpenMP master, runs every OpenMP region of
// the operator: Finalize's R side and Multiply's S side, result preparation
// and the library call (SPALM's kernels and MKL, on the same libgomp).
//
// libgomp keeps one pool of worker threads per master thread, for as long as
// that thread lives. Finalize and Multiply run on DuckDB threads, usually two
// different ones when S has more than one row group, so the first parallel
// region of Multiply used to start a second pool of threads - 1 workers
// (2-dimension-scan: ~3 ms per query above one row group on 8488C, against
// 0.1-0.2 ms below). With one master the pool is started once per process
// and reused by every region (outer-v36).
//
// The master and its pool are started when the query starts, by
// StartAndWarm, while DuckDB scans the inputs (outer-v37). Started by the
// first region instead, as in outer-v36, the pool (~0.8 ms for 30 threads
// on 8488C) was part of 1-dimension-scan on every query (a new process
// each).
//
// The calling DuckDB thread waits (asleep) while its task runs; the team
// size is the same as before (the master plus threads - 1 workers). Tasks
// run one at a time, in arrival order, posted ones included; a task that
// calls Run itself runs inline. Exceptions are rethrown on the calling
// thread (a posted task's are dropped).
//
// Environment, read at the first use:
//   SPALM_OMP_MASTER=0  every task runs inline on the calling thread, as
//                       before outer-v36 (and nothing is warmed);
//   SPALM_OMP_WARMUP=0  the master starts with the query, but its pool only
//                       at the first region, as in outer-v36.

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <exception>
#include <fstream>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <utility>

#if defined(__linux__)
#include <sys/syscall.h>
#include <unistd.h>
#endif

namespace spalm_omp {

// Kernel thread ID of the calling thread (-1 off Linux).
inline long CurrentTid() {
#if defined(__linux__)
	return static_cast<long>(syscall(SYS_gettid));
#else
	return -1;
#endif
}

// Threads of this process (/proc/self/status; -1 off Linux).
inline int ProcessThreads() {
	std::ifstream status("/proc/self/status");
	std::string line;
	while (std::getline(status, line)) {
		if (line.compare(0, 8, "Threads:") == 0) {
			return std::atoi(line.c_str() + 8);
		}
	}
	return -1;
}

// True unless the environment variable is set to "0".
inline bool EnvEnabled(const char *name) {
	const char *setting = std::getenv(name);
	return !(setting && std::strcmp(setting, "0") == 0);
}

class Master {
public:
	// Runs task on the master thread and waits for it.
	void Run(const std::function<void()> &task) {
		if (OnMaster()) {
			task();
			return;
		}
		Waiter waiter;
		std::unique_lock<std::mutex> lock(lock_);
		jobs_.push_back(Job {nullptr, &task, &waiter});
		ready_.notify_one();
		finished_.wait(lock, [&waiter] { return waiter.done; });
		lock.unlock();
		if (waiter.error) {
			std::rethrow_exception(waiter.error);
		}
	}

	// Queues task on the master thread and returns at once.
	void Post(std::function<void()> task) {
		std::lock_guard<std::mutex> lock(lock_);
		jobs_.push_back(Job {std::move(task), nullptr, nullptr});
		ready_.notify_one();
	}

	bool OnMaster() const {
		return std::this_thread::get_id() == id_;
	}
	long Tid() const {
		return tid_.load(std::memory_order_acquire);
	}

	// nullptr if the thread cannot be started.
	static Master *Create() {
		try {
			return new Master();
		} catch (...) {
			return nullptr;
		}
	}

private:
	struct Waiter {
		bool done = false;
		std::exception_ptr error;
	};
	// A posted task is owned; a waited-for one is the caller's.
	struct Job {
		std::function<void()> owned;
		const std::function<void()> *borrowed;
		Waiter *waiter;
	};

	Master() {
		std::thread thread([this] { Loop(); });
		id_ = thread.get_id();
		thread.detach();
	}

	void Loop() {
		tid_.store(CurrentTid(), std::memory_order_release);
		std::unique_lock<std::mutex> lock(lock_);
		for (;;) {
			ready_.wait(lock, [this] { return !jobs_.empty(); });
			Job job = std::move(jobs_.front());
			jobs_.pop_front();
			lock.unlock();
			std::exception_ptr error;
			try {
				if (job.borrowed) {
					(*job.borrowed)();
				} else {
					job.owned();
				}
			} catch (...) {
				error = std::current_exception();
			}
			job.owned = nullptr;
			lock.lock();
			if (job.waiter) {
				job.waiter->error = error;
				job.waiter->done = true;
				finished_.notify_all();
			}
		}
	}

	std::thread::id id_;
	std::atomic<long> tid_ {-1};
	std::mutex lock_; // guards jobs_ and the waiters
	std::condition_variable ready_, finished_;
	std::deque<Job> jobs_;
};

// The process's master, started at the first call; nullptr when disabled
// (SPALM_OMP_MASTER=0) or when the thread cannot be started. Never destroyed:
// the detached thread still waits on its members at process exit.
inline Master *Instance() {
	static Master *const master = EnvEnabled("SPALM_OMP_MASTER") ? Master::Create() : nullptr;
	return master;
}

// Runs task on the master thread (inline when there is none) and waits.
inline void RunOnMaster(const std::function<void()> &task) {
	Master *master = Instance();
	if (!master) {
		task();
		return;
	}
	master->Run(task);
}

// Starts the master and queues a parallel region of `threads` on it that
// only counts its threads, which starts the pool (libgomp keeps the workers
// for the later regions); returns at once. (GCC removes an empty region when
// optimizing, hence the count.) Prints "[omp-warmup] team=<threads that ran>
// ms=<time>" from the master when the region is done. A no-op with
// SPALM_OMP_MASTER=0; with SPALM_OMP_WARMUP=0 it only starts the master.
inline void StartAndWarm(int threads) {
	Master *master = Instance();
	static const bool warm = EnvEnabled("SPALM_OMP_WARMUP");
	if (!master || !warm || threads < 1) {
		return;
	}
	master->Post([threads] {
		const auto begin = std::chrono::steady_clock::now();
		std::atomic<int> team {0};
		(void)threads; // unused without OpenMP
#pragma omp parallel num_threads(threads)
		team.fetch_add(1, std::memory_order_relaxed);
		const std::chrono::duration<double, std::milli> ms = std::chrono::steady_clock::now() - begin;
		std::printf("[omp-warmup] team=%d ms=%.6f\n", team.load(), ms.count());
	});
}

// One "[omp-threads] ..." line for the log: the master's and the caller's
// kernel thread IDs and the process's thread count at this point.
inline void PrintThreads(const char *phase) {
	Master *master = Instance();
	std::printf("[omp-threads] phase=%s master=%ld caller=%ld process=%d\n", phase, master ? master->Tid() : -1L,
	            CurrentTid(), ProcessThreads());
}

} // namespace spalm_omp
