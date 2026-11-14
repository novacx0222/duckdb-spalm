#pragma once

// One process-wide thread, the OpenMP master, runs every OpenMP region of
// the operator: Finalize's R side and Multiply's S side, result preparation
// and the library call (SPALM's kernels and MKL, on the same libgomp).
//
// libgomp keeps one pool of worker threads per master thread, for as long as
// that thread lives. Finalize and Multiply run on DuckDB threads, usually two
// different ones when S has more than one row group, so the first parallel
// region of Multiply used to start a second pool of threads - 1 workers
// (2-dimension-scan: ~3.7 ms per query above one row group on 8488C, against
// 0.1-0.2 ms below; ~2.8 s per method over SuiteSparse). With one master the
// pool is started once per process and reused by every region.
//
// The calling DuckDB thread waits (asleep) while its task runs; the team
// size is the same as before (the master plus threads - 1 workers). Tasks
// run one at a time, in arrival order; a task that calls RunOnMaster itself
// runs inline. Exceptions are rethrown on the calling thread.
//
// SPALM_OMP_MASTER=0 in the environment (read at the first use) runs every
// task inline on the calling thread, as before outer-v36.

#include <atomic>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <fstream>
#include <functional>
#include <mutex>
#include <string>
#include <thread>

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

class Master {
public:
	// Runs task on the master thread and waits for it.
	void Run(const std::function<void()> &task) {
		if (std::this_thread::get_id() == id_) {
			task();
			return;
		}
		std::lock_guard<std::mutex> one_caller(call_lock_);
		std::unique_lock<std::mutex> lock(lock_);
		task_ = &task;
		done_ = false;
		ready_.notify_one();
		finished_.wait(lock, [this] { return done_; });
		task_ = nullptr;
		std::exception_ptr error = error_;
		error_ = nullptr;
		lock.unlock();
		if (error) {
			std::rethrow_exception(error);
		}
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
	Master() {
		std::thread thread([this] { Loop(); });
		id_ = thread.get_id();
		thread.detach();
	}

	void Loop() {
		tid_.store(CurrentTid(), std::memory_order_release);
		std::unique_lock<std::mutex> lock(lock_);
		for (;;) {
			ready_.wait(lock, [this] { return task_ != nullptr && !done_; });
			const std::function<void()> *task = task_;
			lock.unlock();
			std::exception_ptr error;
			try {
				(*task)();
			} catch (...) {
				error = std::current_exception();
			}
			lock.lock();
			error_ = error;
			done_ = true;
			finished_.notify_all();
		}
	}

	std::thread::id id_;
	std::atomic<long> tid_ {-1};
	std::mutex call_lock_; // one task at a time
	std::mutex lock_;      // guards task_, done_, error_
	std::condition_variable ready_, finished_;
	const std::function<void()> *task_ = nullptr;
	bool done_ = true;
	std::exception_ptr error_;
};

// The process's master, started at the first call; nullptr when disabled
// (SPALM_OMP_MASTER=0) or when the thread cannot be started. Never destroyed:
// the detached thread still waits on its members at process exit.
inline Master *Instance() {
	static Master *const master = [] {
		const char *setting = std::getenv("SPALM_OMP_MASTER");
		return setting && std::strcmp(setting, "0") == 0 ? static_cast<Master *>(nullptr) : Master::Create();
	}();
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

// One "[omp-threads] ..." line for the log: the master's and the caller's
// kernel thread IDs and the process's thread count at this point.
inline void PrintThreads(const char *phase) {
	Master *master = Instance();
	std::printf("[omp-threads] phase=%s master=%ld caller=%ld process=%d\n", phase, master ? master->Tid() : -1L,
	            CurrentTid(), ProcessThreads());
}

} // namespace spalm_omp
