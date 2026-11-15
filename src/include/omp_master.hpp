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
// The team is bound one thread per physical core (outer-v38). Linux wakes a
// thread on the CPU it last ran on when that CPU is idle, so where a worker
// first runs decides where it runs later. Started while DuckDB's threads
// scan (outer-v37), more workers landed on the second hyperthread of a core
// that another worker also used, and a region waits for its slowest thread.
// On 8488C (32 cores x 2 hyperthreads), against the median of the same
// query, a run with no core running two team threads took ~0.95 of the time
// (MKL-S ~0.89), one with three or more ~1.02 (MKL-S ~1.04). PinTeam binds
// team thread t to both hyperthreads of one core, in the warm-up region;
// DuckDB's threads stay unbound.
//
// The calling DuckDB thread waits (asleep) while its task runs; the team
// size is the same as before (the master plus threads - 1 workers). Tasks
// run one at a time, in arrival order, posted ones included; a task that
// calls Run itself runs inline. Exceptions are rethrown on the calling
// thread (a posted task's are dropped).
//
// Environment, read at the first use:
//   SPALM_OMP_MASTER=0  every task runs inline on the calling thread, as
//                       before outer-v36 (nothing is warmed or bound);
//   SPALM_OMP_WARMUP=0  the master starts with the query, but its pool only
//                       at the first region, as in outer-v36 (still bound,
//                       then, at Finalize);
//   SPALM_OMP_PIN=0     the team is not bound, as in outer-v37.

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
#include <vector>

#if defined(__linux__)
#include <pthread.h>
#include <sched.h>
#include <sys/syscall.h>
#include <unistd.h>
#endif
#ifdef _OPENMP
#include <omp.h>
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

// The CPUs of a Linux cpulist such as "0-3,8,10-11" (sysfs format; a
// trailing newline is fine). Empty if the text is not one.
inline std::vector<int> ParseCpuList(const std::string &text) {
	std::vector<int> cpus;
	std::string list = text;
	while (!list.empty() && (list.back() == '\n' || list.back() == ' ')) {
		list.pop_back();
	}
	size_t pos = 0;
	while (pos < list.size()) {
		size_t end = list.find(',', pos);
		if (end == std::string::npos) {
			end = list.size();
		}
		const std::string item = list.substr(pos, end - pos);
		pos = end + 1;
		char *rest = nullptr;
		const long lo = std::strtol(item.c_str(), &rest, 10);
		if (item.empty() || rest == item.c_str() || lo < 0) {
			return std::vector<int>();
		}
		long hi = lo;
		if (*rest == '-') {
			const char *start = rest + 1;
			hi = std::strtol(start, &rest, 10);
			if (rest == start || hi < lo || hi - lo > 65535) {
				return std::vector<int>();
			}
		}
		if (*rest != '\0') {
			return std::vector<int>();
		}
		for (long cpu = lo; cpu <= hi; cpu++) {
			cpus.push_back(static_cast<int>(cpu));
		}
	}
	return cpus;
}

// The physical cores the calling thread may run on, each as its allowed
// hyperthreads (sysfs thread_siblings_list), in order of their lowest CPU.
// Empty off Linux or if the affinity cannot be read.
inline std::vector<std::vector<int>> AllowedCores() {
	std::vector<std::vector<int>> cores;
#if defined(__linux__)
	cpu_set_t allowed;
	CPU_ZERO(&allowed);
	if (sched_getaffinity(0, sizeof(allowed), &allowed) != 0) {
		return cores;
	}
	std::vector<bool> taken(CPU_SETSIZE, false);
	for (int cpu = 0; cpu < CPU_SETSIZE; cpu++) {
		if (!CPU_ISSET(cpu, &allowed) || taken[cpu]) {
			continue;
		}
		std::ifstream file("/sys/devices/system/cpu/cpu" + std::to_string(cpu) + "/topology/thread_siblings_list");
		std::string text;
		std::getline(file, text);
		std::vector<int> core(1, cpu);
		taken[cpu] = true;
		for (int sibling : ParseCpuList(text)) {
			if (sibling >= 0 && sibling < CPU_SETSIZE && !taken[sibling] && CPU_ISSET(sibling, &allowed)) {
				taken[sibling] = true;
				core.push_back(sibling);
			}
		}
		cores.push_back(core);
	}
#endif
	return cores;
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

inline bool PinEnabled() {
	static const bool pin = EnvEnabled("SPALM_OMP_PIN");
	return pin;
}

// A parallel region of `threads` that only counts its threads; it starts the
// calling thread's pool. (GCC removes an empty region when optimizing.)
inline int CountTeam(int threads) {
	std::atomic<int> team {0};
	(void)threads; // unused without OpenMP
#pragma omp parallel num_threads(threads)
	team.fetch_add(1, std::memory_order_relaxed);
	return team.load();
}

// Call on the master only (it keeps its state in statics, and binds the
// calling thread too). Binds every thread of its team of `threads` to both
// hyperthreads of one physical core, in one parallel region: team thread t
// to core (t + offset) mod cores, where offset is 1 when there are more cores
// than threads (core 0 then stays with DuckDB and the system). The cores are
// those allowed at the first call, before anything is bound. Prints
// "[omp-pin] team=<threads that ran> cores=<cores> bound=<n> failed=<n>
// ms=<time>". A team no larger than one bound before is left as it is (a
// larger one binds again: its new workers start with the master's core).
// Returns the threads that ran the region, 0 when it did not run.
inline int PinTeam(int threads) {
	static int bound_team = 0;
	static const std::vector<std::vector<int>> cores = AllowedCores();
	if (threads <= bound_team) {
		return 0;
	}
	const auto begin = std::chrono::steady_clock::now();
	const size_t n = cores.size();
	const size_t offset = n > static_cast<size_t>(threads) ? 1 : 0;
	std::atomic<int> team {0}, bound {0}, failed {0};
	(void)offset;
#pragma omp parallel num_threads(threads)
	{
		team.fetch_add(1, std::memory_order_relaxed);
#if defined(__linux__) && defined(_OPENMP)
		if (n > 0) {
			const std::vector<int> &core = cores[(static_cast<size_t>(omp_get_thread_num()) + offset) % n];
			cpu_set_t set;
			CPU_ZERO(&set);
			for (int cpu : core) {
				CPU_SET(cpu, &set);
			}
			if (pthread_setaffinity_np(pthread_self(), sizeof(set), &set) == 0) {
				bound.fetch_add(1, std::memory_order_relaxed);
			} else {
				failed.fetch_add(1, std::memory_order_relaxed);
			}
		}
#endif
	}
	bound_team = threads;
	const std::chrono::duration<double, std::milli> ms = std::chrono::steady_clock::now() - begin;
	std::printf("[omp-pin] team=%d cores=%zu bound=%d failed=%d ms=%.6f\n", team.load(), n, bound.load(),
	            failed.load(), ms.count());
	return team.load();
}

// Before a region of `threads` on the master: binds its team if that is not
// done yet. A no-op off the master and with SPALM_OMP_PIN=0.
inline void PinOnMaster(int threads) {
	Master *master = Instance();
	if (master && master->OnMaster() && PinEnabled()) {
		PinTeam(threads);
	}
}

// Starts the master and queues the warm-up on it: the region that binds the
// team (or, with SPALM_OMP_PIN=0, one that only counts its threads), which
// starts the pool (libgomp keeps the workers for the later regions). Returns
// at once. Prints "[omp-warmup] team=<threads that ran> ms=<time>" from the
// master when it is done. A no-op with SPALM_OMP_MASTER=0; with
// SPALM_OMP_WARMUP=0 it only starts the master.
inline void StartAndWarm(int threads) {
	Master *master = Instance();
	static const bool warm = EnvEnabled("SPALM_OMP_WARMUP");
	if (!master || !warm || threads < 1) {
		return;
	}
	master->Post([threads] {
		const auto begin = std::chrono::steady_clock::now();
		int team = PinEnabled() ? PinTeam(threads) : 0;
		if (team == 0) {
			team = CountTeam(threads);
		}
		const std::chrono::duration<double, std::milli> ms = std::chrono::steady_clock::now() - begin;
		std::printf("[omp-warmup] team=%d ms=%.6f\n", team, ms.count());
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
