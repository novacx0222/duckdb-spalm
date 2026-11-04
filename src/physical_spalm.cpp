#include "physical_spalm.hpp"
#include "result_storage.hpp"
#include "result_pattern.hpp"
#include "csr_sort.hpp"
#include "duckdb/common/exception.hpp"
#include "duckdb/common/string_util.hpp"
#include "duckdb/main/client_context.hpp"
#include "duckdb/execution/operator/join/physical_join.hpp"
#include "duckdb/common/types/column/column_data_collection.hpp"
#include "duckdb/parallel/task_scheduler.hpp"

#include <dlfcn.h>
#include <iostream>
#include <climits>
#include <cfenv>
#include <cmath>
#include <algorithm>
#include <exception>
#include <omp.h>
#include <atomic>
#include <fstream>
#include <sstream>
#include <dirent.h>
#include <mutex>
#include <unordered_map>
#include <vector>
#include <string>
#include <chrono>

namespace duckdb {

using SpalmClock = std::chrono::steady_clock;

static double ElapsedMs(SpalmClock::time_point begin, SpalmClock::time_point end = SpalmClock::now()) {
	return std::chrono::duration<double, std::milli>(end - begin).count();
}

// One timing line per stage, "[<name>] <milliseconds> ms", the same format as
// the native library. Phase 1 is the build (sink) side R, phase 2 the probe
// side S; nested totals overlap and must not be added together.
static void PrintStage(const std::string &name, double ms) {
	printf("[%s] %.6f ms\n", name.c_str(), ms);
}

// An exception escaping an OpenMP region calls std::terminate and takes
// DuckDB down (e.g. FetchChunk failing to re-pin spilled blocks, or an
// allocation failure). Run each loop body through Run(), then call Rethrow()
// after the region. Iterations after the first error are skipped.
class OmpErrors {
public:
	template <class BODY>
	void Run(BODY &&body) {
		if (failed.load(std::memory_order_relaxed)) {
			return;
		}
		try {
			body();
		} catch (...) {
			std::lock_guard<std::mutex> guard(lock);
			if (!error) {
				error = std::current_exception();
			}
			failed.store(true, std::memory_order_relaxed);
		}
	}
	void Rethrow() {
		if (error) {
			std::rethrow_exception(error);
		}
	}

private:
	std::atomic<bool> failed {false};
	std::mutex lock;
	std::exception_ptr error;
};

// An entry takes part in the join only if its join key and value are not
// NULL: SQL drops NULL join keys, and NULL values add nothing to SUM.
static inline bool EntryParticipates(const ValidityMask &key_valid, const ValidityMask &val_valid, idx_t i) {
	return key_valid.RowIsValid(i) && val_valid.RowIsValid(i);
}

// Contiguous chunk range of logical worker `worker` out of `workers`. Both
// CSR passes use the same ranges, so their per-worker counts and write
// offsets agree whatever thread count or schedule OpenMP actually uses.
static void WorkerChunkRange(idx_t num_chunks, int workers, int worker, idx_t &begin, idx_t &end) {
	const idx_t per_worker = num_chunks / workers, extra = num_chunks % workers;
	begin = per_worker * worker + std::min<idx_t>(worker, extra);
	end = begin + per_worker + (static_cast<idx_t>(worker) < extra ? 1 : 0);
}

// CSR entry counts and offsets are int (as in the native API): reject larger
// inputs instead of overflowing the (int) cast.
static int CheckedEntryCount(ColumnDataCollection &data) {
	if (data.Count() > static_cast<idx_t>(INT_MAX)) {
		throw InvalidInputException(
		    "SPALM supports at most %d entries per input (got %llu)", INT_MAX, (unsigned long long)data.Count()
		);
	}
	return static_cast<int>(data.Count());
}

struct SpalmInputMax {
	int max_row = -1; // -1: no participating entries
	int max_key = -1;
	int value_flags = 0; // spalm_output::ValueFlags
};

// Validates one input and returns its largest row ID and join key among the
// participating entries. Both index dense arrays, so NULL row IDs and
// negative or INT_MAX-sized IDs/keys are rejected instead of corrupting
// memory (the maxima used to start at 0, hiding negative values). The values
// of all entries are checked: one that does not take part can only send the
// input the longer way.
static SpalmInputMax
ScanInputMax(ColumnDataCollection &data, int num_threads, idx_t row_idx, idx_t key_idx, idx_t val_idx, bool use_float) {
	const auto &types = data.Types();
	const auto value_type = use_float ? LogicalTypeId::FLOAT : LogicalTypeId::DOUBLE;
	if (types[row_idx].id() != LogicalTypeId::INTEGER || types[key_idx].id() != LogicalTypeId::INTEGER ||
	    types[val_idx].id() != value_type) {
		throw InternalException("SPALM expects INTEGER row/key columns and FLOAT/DOUBLE values");
	}
	std::vector<DataChunk> chunks(num_threads);
	for (auto &chunk : chunks) {
		data.InitializeScanChunk(chunk);
	}
	const idx_t num_chunks = data.ChunkCount();
	int max_row = -1, max_key = -1, invalid = 0, value_flags = 0;
	OmpErrors errors;
#pragma omp parallel for num_threads(num_threads) schedule(static) reduction(max : max_row, max_key)                   \
    reduction(| : invalid, value_flags)
	for (idx_t ci = 0; ci < num_chunks; ci++) {
		errors.Run([&] {
			auto &chunk = chunks[omp_get_thread_num()];
			data.FetchChunk(ci, chunk);
			chunk.Flatten();
			const auto rows = FlatVector::GetData<int32_t>(chunk.data[row_idx]);
			const auto keys = FlatVector::GetData<int32_t>(chunk.data[key_idx]);
			const auto &row_valid = FlatVector::Validity(chunk.data[row_idx]);
			const auto &key_valid = FlatVector::Validity(chunk.data[key_idx]);
			const auto &val_valid = FlatVector::Validity(chunk.data[val_idx]);
			for (idx_t i = 0; i < chunk.size(); i++) {
				if (!EntryParticipates(key_valid, val_valid, i)) {
					continue;
				}
				if (!row_valid.RowIsValid(i)) {
					invalid |= 1;
					continue;
				}
				const int row = rows[i], key = keys[i];
				if (row < 0 || key < 0) {
					invalid |= 2;
					continue;
				}
				// M = max_row + 1 and the M + 1 row pointers must fit in int.
				if (row >= INT_MAX - 1 || key >= INT_MAX) {
					invalid |= 4;
					continue;
				}
				max_row = std::max(max_row, row);
				max_key = std::max(max_key, key);
			}
			auto &values = chunk.data[val_idx];
			value_flags |= use_float ? spalm_output::CheckValues(FlatVector::GetData<float>(values), chunk.size())
			                         : spalm_output::CheckValues(FlatVector::GetData<double>(values), chunk.size());
		});
	}
	// Throw outside the OpenMP region; an exception escaping it terminates.
	errors.Rethrow();
	if (invalid & 1) {
		throw InvalidInputException("SPALM does not support NULL row or column IDs");
	}
	if (invalid & 2) {
		throw InvalidInputException("SPALM requires nonnegative row/column IDs and join keys");
	}
	if (invalid & 4) {
		throw InvalidInputException("SPALM row/column IDs and join keys must be below INT_MAX - 1");
	}
	SpalmInputMax result;
	result.max_row = max_row;
	result.max_key = max_key;
	result.value_flags = value_flags;
	return result;
}

// Requires ScanInputMax to have validated `data`: participating entries
// have non-NULL, in-range row IDs.
template <typename T>
void BuildCSR(
    int *row_ptr, int *col_idx, T *vals, ColumnDataCollection &data, int num_rows, int num_threads, idx_t join_col_idx,
    idx_t agg_col_idx, idx_t val_col_idx, std::vector<DataChunk> &t_chunks, const std::string &phase,
    SpalmDuplicates duplicates
) {
	const auto count_begin = SpalmClock::now();
	auto stage_begin = count_begin;
	std::vector<std::vector<int>> local_hist(num_threads, std::vector<int>(num_rows + 1, 0));
	PrintStage(phase + "-csr-histogram-allocate", ElapsedMs(stage_begin));
	stage_begin = SpalmClock::now();
	const idx_t num_chunks = data.ChunkCount();

	// Histograms and offsets are indexed by logical worker, not by
	// omp_get_thread_num(): the old count and write passes each relied on the
	// default schedule giving the same chunk-to-thread mapping.
	OmpErrors errors;
#pragma omp parallel for num_threads(num_threads) schedule(static, 1)
	for (int worker = 0; worker < num_threads; worker++) {
		errors.Run([&] {
			auto &chunk = t_chunks[omp_get_thread_num()];
			idx_t begin, end;
			WorkerChunkRange(num_chunks, num_threads, worker, begin, end);
			for (idx_t ci = begin; ci < end; ci++) {
				data.FetchChunk(ci, chunk);
				chunk.Flatten();
				const auto rows = FlatVector::GetData<int32_t>(chunk.data[agg_col_idx]);
				const auto &key_valid = FlatVector::Validity(chunk.data[join_col_idx]);
				const auto &val_valid = FlatVector::Validity(chunk.data[val_col_idx]);
				for (idx_t i = 0; i < chunk.size(); i++) {
					if (EntryParticipates(key_valid, val_valid, i)) {
						local_hist[worker][rows[i] + 1]++;
					}
				}
			}
		});
	}
	errors.Rethrow();
	PrintStage(phase + "-csr-row-count", ElapsedMs(stage_begin));
	stage_begin = SpalmClock::now();

	for (int t = 0; t < num_threads; t++) {
		for (int r = 0; r <= num_rows; r++) {
			row_ptr[r] += local_hist[t][r];
		}
	}

	for (int i = 0; i < num_rows; i++) {
		row_ptr[i + 1] += row_ptr[i];
	}

	std::vector<std::vector<int>> thread_offsets(num_threads, std::vector<int>(num_rows, 0));
	for (int r = 0; r < num_rows; r++) {
		int current_offset = 0;
		for (int t = 0; t < num_threads; t++) {
			int count = local_hist[t][r + 1];
			thread_offsets[t][r] = current_offset;
			current_offset += count;
		}
	}

	PrintStage(phase + "-csr-prefix-offset", ElapsedMs(stage_begin));
	PrintStage(phase + "-csr-count-total", ElapsedMs(count_begin));

	stage_begin = SpalmClock::now();
#pragma omp parallel for num_threads(num_threads) schedule(static, 1)
	for (int worker = 0; worker < num_threads; worker++) {
		errors.Run([&] {
			auto &chunk = t_chunks[omp_get_thread_num()];
			idx_t begin, end;
			WorkerChunkRange(num_chunks, num_threads, worker, begin, end);
			for (idx_t ci = begin; ci < end; ci++) {
				data.FetchChunk(ci, chunk);
				chunk.Flatten();
				const auto keys = FlatVector::GetData<int32_t>(chunk.data[join_col_idx]);
				const auto rows = FlatVector::GetData<int32_t>(chunk.data[agg_col_idx]);
				const auto values = FlatVector::GetData<T>(chunk.data[val_col_idx]);
				const auto &key_valid = FlatVector::Validity(chunk.data[join_col_idx]);
				const auto &val_valid = FlatVector::Validity(chunk.data[val_col_idx]);
				for (idx_t i = 0; i < chunk.size(); i++) {
					if (!EntryParticipates(key_valid, val_valid, i)) {
						continue;
					}
					const int agg_idx = rows[i];
					const int pos = row_ptr[agg_idx] + thread_offsets[worker][agg_idx]++;
					col_idx[pos] = keys[i];
					vals[pos] = values[i];
				}
			}
		});
	}
	errors.Rethrow();

	PrintStage(phase + "-csr-write", ElapsedMs(stage_begin));

	// Rows already in column order are only checked; the others are sorted
	// with temporary vectors, as before.
	stage_begin = SpalmClock::now();
	const bool check_duplicates = duplicates != SpalmDuplicates::ASSUME_NONE;
	long long unsorted_rows = 0, repeated = 0;
#pragma omp parallel for schedule(dynamic) reduction(+ : unsorted_rows, repeated)
	for (int r = 0; r < num_rows; r++) {
		errors.Run([&] {
			const bool was_sorted =
			    check_duplicates ? spalm_input::SortRowIndices<T>(row_ptr[r], row_ptr[r + 1], col_idx, vals, repeated)
			                     : spalm_input::SortRowIndices<T>(row_ptr[r], row_ptr[r + 1], col_idx, vals);
			if (!was_sorted) {
				unsorted_rows++;
			}
		});
	}
	errors.Rethrow();
	PrintStage(phase + "-csr-sort", ElapsedMs(stage_begin));
	if (check_duplicates) {
		printf("[%s-csr-sort-stats] rows=%d unsorted_rows=%lld duplicates=%lld\n", phase.c_str(), num_rows,
		       unsorted_rows, repeated);
	} else {
		printf("[%s-csr-sort-stats] rows=%d unsorted_rows=%lld\n", phase.c_str(), num_rows, unsorted_rows);
	}

	if (repeated > 0) {
		if (duplicates == SpalmDuplicates::ERROR_ON_ANY) {
			throw InvalidInputException("SPALM input %s has %llu repeated (row, join key) entries", phase,
			                            (unsigned long long)repeated);
		}
		stage_begin = SpalmClock::now();
		spalm_input::MergeRowDuplicates<T>(row_ptr, num_rows, col_idx, vals);
		PrintStage(phase + "-csr-merge", ElapsedMs(stage_begin));
	}
}

SpalmDuplicates ParseSpalmDuplicates(const string &value) {
	const auto mode = StringUtil::Lower(value);
	if (mode == "merge") {
		return SpalmDuplicates::MERGE;
	}
	if (mode == "error") {
		return SpalmDuplicates::ERROR_ON_ANY;
	}
	if (mode == "ignore") {
		return SpalmDuplicates::ASSUME_NONE;
	}
	throw InvalidInputException("spalm_duplicates must be 'merge', 'error' or 'ignore' (got '%s')", value);
}

// Returns a VARCHAR setting registered by the extension, or "" if unset.
static std::string GetSpalmSetting(ClientContext &context, const char *name) {
	Value value;
	if (!context.TryGetCurrentSetting(name, value) || value.IsNull()) {
		return "";
	}
	return value.ToString();
}

struct SpalmConfig {
	std::string path; // file that was read, "" if none
	std::unordered_map<std::string, std::string> values;
};

// Parse the SPALM configuration file into a key->value map. Location, in
// order: SET spalm_conf, $SPALM_CONF, ./spalm.conf. An explicitly configured
// file that cannot be opened is an error; a missing ./spalm.conf is not.
static SpalmConfig ReadSpalmConf(ClientContext &context) {
	SpalmConfig result;
	auto &config = result.values;

	std::string path = GetSpalmSetting(context, "spalm_conf");
	const char *env_path = getenv("SPALM_CONF");
	const bool explicit_path = !path.empty() || env_path;
	if (path.empty()) {
		path = env_path ? env_path : "spalm.conf";
	}

	std::ifstream f(path);
	if (!f.is_open()) {
		if (explicit_path) {
			throw InvalidInputException("spalm: could not open config file: %s", path);
		}
		std::cerr << "spalm: could not open config file: " << path << "\n";
		return result;
	}
	result.path = path;

	std::string line;
	while (std::getline(f, line)) {
		if (line.empty() || line[0] == '#')
			continue;
		auto eq = line.find('=');
		if (eq == std::string::npos)
			continue;
		std::string key = line.substr(0, eq);
		std::string val = line.substr(eq + 1);
		// Trim whitespace
		key.erase(0, key.find_first_not_of(" \t"));
		key.erase(key.find_last_not_of(" \t") + 1);
		val.erase(0, val.find_first_not_of(" \t"));
		val.erase(val.find_last_not_of(" \t") + 1);
		if (!key.empty())
			config[key] = val;
	}
	return result;
}

// The library in a directory: spalm.so if it is there, else the first other *.so by
// name. bin/ of the library also has run_baseline.so, the MKL baselines that spalm.so
// loads itself, which is never the library; and the order of readdir depends on the
// file system, so taking the first file it lists picked either of the two.
static std::string FindSoInDir(const std::string &dir) {
	DIR *d = opendir(dir.c_str());
	if (!d) {
		std::cerr << "spalm: cannot open so_dir: " << dir << "\n";
		return "";
	}
	std::vector<std::string> names;
	struct dirent *ent;
	while ((ent = readdir(d)) != nullptr) {
		std::string name = ent->d_name;
		if (name.size() > 3 && name.substr(name.size() - 3) == ".so" && name != "run_baseline.so") {
			names.push_back(name);
		}
	}
	closedir(d);
	if (names.empty()) {
		return "";
	}
	std::sort(names.begin(), names.end());
	const auto preferred = std::find(names.begin(), names.end(), "spalm.so");
	return dir + "/" + (preferred != names.end() ? *preferred : names.front());
}

// Native library path: SET spalm_so_dir, else so_dir from the config file.
// A relative so_dir from the file is tried relative to the file's directory
// first, then (as before) relative to the working directory.
static std::string ResolveSpalmLibrary(ClientContext &context, const SpalmConfig &conf) {
	std::string so_dir = GetSpalmSetting(context, "spalm_so_dir");
	if (so_dir.empty()) {
		auto entry = conf.values.find("so_dir");
		if (entry == conf.values.end() || entry->second.empty()) {
			throw InvalidInputException(
			    "spalm: no library directory. SET spalm_so_dir, or set so_dir in spalm.conf "
			    "(location: SET spalm_conf, $SPALM_CONF or ./spalm.conf)"
			);
		}
		so_dir = entry->second;
		const auto slash = conf.path.find_last_of('/');
		if (so_dir[0] != '/' && slash != std::string::npos) {
			const std::string near_conf = conf.path.substr(0, slash + 1) + so_dir;
			DIR *d = opendir(near_conf.c_str());
			if (d) {
				closedir(d);
				so_dir = near_conf;
			}
		}
	}
	const std::string so_path = FindSoInDir(so_dir);
	if (so_path.empty()) {
		throw InvalidInputException("spalm: no .so file found in %s", so_dir);
	}
	return so_path;
}

// spalm() returns 0 on success; the result is valid only then.
using spalm_fn_t = int (*)(
    int, int *, int *, void *, int *, int *, void *, const int, const int, const int, bool, void *, int *, int *,
    void *, int, const char **, const char **
);

// spalm() that also sets bit j % 64 of word i * ceil(N / 64) + j / 64 of the
// bitmap, zeroed by the caller, for each pair (i, j) that some key matches,
// whatever the sum. The last argument is set to 1 if it did, or to 0 if MKL ran,
// which marks nothing.
using spalm_matched_fn_t = int (*)(
    int, int *, int *, void *, int *, int *, void *, const int, const int, const int, bool, void *, int *, int *,
    void *, int, const char **, const char **, uint64_t *, int *
);

struct SpalmLibrary {
	spalm_fn_t spalm = nullptr;
	// Present in libraries that mark the matched pairs (symbol spalm_matched)
	spalm_matched_fn_t spalm_matched = nullptr;
	// The library accepts result_zeroed = 0 (an uninitialized result) and
	// then writes every element (symbol spalm_accepts_unzeroed_result).
	bool accepts_unzeroed_result = false;
};

// Load (once per path) and return the native entry points. Serialized: the
// cache used to be two unsynchronized statics that concurrent queries could
// race on, and it ignored a changed so_dir until restart. Libraries stay
// loaded for the process lifetime: unloading MKL corrupts its thread pool.
static SpalmLibrary LoadSpalmLibrary(const std::string &so_path) {
	static std::mutex lock;
	static std::unordered_map<std::string, SpalmLibrary> loaded;
	std::lock_guard<std::mutex> guard(lock);
	auto entry = loaded.find(so_path);
	if (entry != loaded.end()) {
		return entry->second;
	}
	void *handle = dlopen(so_path.c_str(), RTLD_LAZY);
	if (!handle) {
		throw InvalidInputException("spalm: cannot open library %s: %s", so_path, dlerror());
	}
	SpalmLibrary library;
	library.spalm = (spalm_fn_t)dlsym(handle, "spalm");
	if (!library.spalm) {
		const std::string error = dlerror();
		dlclose(handle);
		throw InvalidInputException("spalm: symbol 'spalm' not found in %s: %s", so_path, error);
	}
	library.accepts_unzeroed_result = dlsym(handle, "spalm_accepts_unzeroed_result") != nullptr;
	library.spalm_matched = (spalm_matched_fn_t)dlsym(handle, "spalm_matched");
	loaded[so_path] = library;
	return library;
}

PhysicalSpalm::PhysicalSpalm(
    PhysicalPlan &physical_plan, vector<LogicalType> types, PhysicalOperator &left, PhysicalOperator &right,
    SpalmColumnBindings col_bindings, bool use_float_p, idx_t estimated_cardinality
)
    : PhysicalOperator(physical_plan, PhysicalOperatorType::EXTENSION, std::move(types), estimated_cardinality),
      rhs_join_idx(col_bindings.rhs_join_idx), rhs_agg_idx(col_bindings.rhs_agg_idx),
      rhs_val_idx(col_bindings.rhs_val_idx), lhs_join_idx(col_bindings.lhs_join_idx),
      lhs_agg_idx(col_bindings.lhs_agg_idx), lhs_val_idx(col_bindings.lhs_val_idx), use_float(use_float_p),
      rhs_output_idx(col_bindings.rhs_output_idx), lhs_output_idx(col_bindings.lhs_output_idx),
      rhs_child_types(right.types), lhs_child_types(left.types) {
	children.push_back(left);
	children.push_back(right);
}

//===--------------------------------------------------------------------===//
// Sink
//===--------------------------------------------------------------------===//
class PhysicalSpalmGlobalState : public GlobalSinkState {
public:
	explicit PhysicalSpalmGlobalState(ClientContext &context, const PhysicalSpalm &op)
	    : use_float(op.GetUseFloat()), lhs_child_types(op.GetLhsChildTypes()), rhs_join_idx(op.GetRhsJoinIdx()),
	      rhs_agg_idx(op.GetRhsAggIdx()), rhs_val_idx(op.GetRhsValIdx()), lhs_join_idx(op.GetLhsJoinIdx()),
	      lhs_agg_idx(op.GetLhsAggIdx()), lhs_val_idx(op.GetLhsValIdx()), context_ref(context),
	      duplicates(ParseSpalmDuplicates(GetSpalmSetting(context, "spalm_duplicates"))),
	      mat_data(make_uniq<ColumnDataCollection>(context, op.GetRhsChildTypes())) {
		mat_data->InitializeAppend(append_state);

		threads = (int)TaskScheduler::GetScheduler(context).NumberOfThreads();

		M = 0;
		K = 0;
		N = 0;

		collect_begin = SpalmClock::now();
	}

	~PhysicalSpalmGlobalState() {
		const auto begin = SpalmClock::now();
		Ri.reset();
		Rk.reset();
		Rv_f.reset();
		Rv_d.reset();
		Sj.reset();
		Sk.reset();
		Sv_f.reset();
		Sv_d.reset();
		const auto result_begin = SpalmClock::now();
		result_f.reset();
		result_d.reset();
		PrintStage("result-deallocate", ElapsedMs(result_begin));
		mat_data.reset();
		PrintStage("wrapper-deallocate", ElapsedMs(begin));
	}

	bool use_float;
	vector<LogicalType> lhs_child_types;
	idx_t rhs_join_idx, rhs_agg_idx, rhs_val_idx;
	idx_t lhs_join_idx, lhs_agg_idx, lhs_val_idx;
	ClientContext &context_ref;
	// Read once, so both inputs of the query use the same mode.
	SpalmDuplicates duplicates;

	const int slice_size = 512;
	const int tile_size_agg_dim = 128;

	int M, K, N;
	int sink_max_join; // max value of join column on sink side
	// spalm_output::ValueFlags of the build side.
	int value_flags = 0;
	// Whether a zero in the result can be anything but a pair without a match. If
	// not, the output needs nothing else, and nothing can overflow or underflow.
	bool needs_pattern = false;

	unique_ptr<int[]> Ri;
	unique_ptr<int[]> Rk;
	unique_ptr<float[]> Rv_f;
	unique_ptr<double[]> Rv_d;

	unique_ptr<int[]> Sj;
	unique_ptr<int[]> Sk;
	unique_ptr<float[]> Sv_f;
	unique_ptr<double[]> Sv_d;

	// Stored (physical) rows of R and S; the result is r_layout.Rows() x
	// s_layout.Rows().
	spalm_output::DenseRowLayout r_layout;
	spalm_output::DenseRowLayout s_layout;
	spalm_output::ResultBuffer<float> result_f;
	spalm_output::ResultBuffer<double> result_d;
	// With needs_pattern, from a library with spalm_matched whose kernels ran
	// (not MKL): the pairs that matched, matched_words words per row of the
	// result, which the output takes as the pattern of every row
	spalm_output::ResultBuffer<uint64_t> matched;
	size_t matched_words = 0;

	int threads;

	// Start of collecting the current input (phase 1, then phase 2).
	SpalmClock::time_point collect_begin;
	// When the native call returned; start of the output phase.
	SpalmClock::time_point native_end;
	// Time spent merging thread-local inputs, per phase (under `lock`).
	double combine_ms[2] = {0, 0};
	double prepare_ms[2] = {0, 0};

	unique_ptr<ColumnDataCollection> mat_data;
	ColumnDataAppendState append_state;
	mutex lock;
};

class PhysicalSpalmLocalState : public LocalSinkState {
public:
	explicit PhysicalSpalmLocalState(ExecutionContext &context, const PhysicalSpalm &op)
	    : l_mat_data(context.client, op.GetRhsChildTypes()) {
		l_mat_data.InitializeAppend(append_state);
	}

	~PhysicalSpalmLocalState() {
	}

	ColumnDataCollection l_mat_data;
	ColumnDataAppendState append_state;
};

unique_ptr<LocalSinkState> PhysicalSpalm::GetLocalSinkState(ExecutionContext &context) const {
	return make_uniq<PhysicalSpalmLocalState>(context, *this);
}

unique_ptr<GlobalSinkState> PhysicalSpalm::GetGlobalSinkState(ClientContext &context) const {
	return make_uniq<PhysicalSpalmGlobalState>(context, *this);
}

SinkResultType PhysicalSpalm::Sink(ExecutionContext &context, DataChunk &chunk, OperatorSinkInput &input) const {
	auto &lstate = input.local_state.Cast<PhysicalSpalmLocalState>();
	lstate.l_mat_data.Append(lstate.append_state, chunk);
	return SinkResultType::NEED_MORE_INPUT;
}

SinkCombineResultType PhysicalSpalm::Combine(ExecutionContext &context, OperatorSinkCombineInput &input) const {
	auto &sink = input.global_state.Cast<PhysicalSpalmGlobalState>();
	auto &lstate = input.local_state.Cast<PhysicalSpalmLocalState>();
	lock_guard<mutex> client_guard(sink.lock);
	const auto begin = SpalmClock::now();
	sink.mat_data->Combine(lstate.l_mat_data);
	sink.combine_ms[0] += ElapsedMs(begin);
	return SinkCombineResultType::FINISHED;
}

SinkFinalizeType PhysicalSpalm::Finalize(
    Pipeline &pipeline, Event &event, ClientContext &context, OperatorSinkFinalizeInput &input
) const {
	auto &sink = input.global_state.Cast<PhysicalSpalmGlobalState>();
	const auto prepare_begin = SpalmClock::now();
	// Scanning the build side and sinking it, including the merges below.
	PrintStage("1-collect-input", ElapsedMs(sink.collect_begin, prepare_begin));
	PrintStage("1-combine-input", sink.combine_ms[0]);

	const int sink_nnz = CheckedEntryCount(*sink.mat_data);
	printf(
	    "[SPALM Finalize] mat_data chunk count=%llu, total rows (nnz)=%d\n",
	    (unsigned long long)sink.mat_data->ChunkCount(), sink_nnz
	);

	// Scan the collected data to find max(agg_col) and max(join_col)
	const int num_threads = sink.threads;
	omp_set_num_threads(num_threads);

	auto stage_begin = SpalmClock::now();
	const auto r_max = ScanInputMax(
	    *sink.mat_data, num_threads, sink.rhs_agg_idx, sink.rhs_join_idx, sink.rhs_val_idx, sink.use_float
	);
	PrintStage("1-dimension-scan", ElapsedMs(stage_begin));
	const int M = r_max.max_row + 1; // 0 when no entry participates
	sink.M = M;
	sink.sink_max_join = r_max.max_key;
	sink.value_flags = r_max.value_flags;
	printf("[SPALM Finalize] derived M=%d, max_join=%d, nnz=%d\n", M, r_max.max_key, sink_nnz);

	// Allocate sink CSR arrays. make_uniq_array zero-initializes, which
	// BuildCSR's histogram needs for the row pointers. BuildCSR writes the
	// first Ri[M] column/value entries; readers stop at Ri[M], so the
	// uninitialized tail left by non-participating entries is never read.
	stage_begin = SpalmClock::now();
	sink.Ri = make_uniq_array<int>(M + 1);
	sink.Rk = make_uniq_array_uninitialized<int>(sink_nnz);
	if (sink.use_float) {
		sink.Rv_f = make_uniq_array_uninitialized<float>(sink_nnz);
	} else {
		sink.Rv_d = make_uniq_array_uninitialized<double>(sink_nnz);
	}
	PrintStage("1-csr-allocate", ElapsedMs(stage_begin));

	stage_begin = SpalmClock::now();
	std::vector<DataChunk> t_chunks(num_threads);
	for (auto &chunk : t_chunks) {
		sink.mat_data->InitializeScanChunk(chunk);
	}
	PrintStage("1-scan-chunks-prepare", ElapsedMs(stage_begin));

	if (sink.use_float) {
		BuildCSR<float>(
		    sink.Ri.get(), sink.Rk.get(), sink.Rv_f.get(), *sink.mat_data, M, num_threads, rhs_join_idx, rhs_agg_idx,
		    rhs_val_idx, t_chunks, "1", sink.duplicates
		);
	} else {
		BuildCSR<double>(
		    sink.Ri.get(), sink.Rk.get(), sink.Rv_d.get(), *sink.mat_data, M, num_threads, rhs_join_idx, rhs_agg_idx,
		    rhs_val_idx, t_chunks, "1", sink.duplicates
		);
	}

	t_chunks.clear();
	sink.prepare_ms[0] = ElapsedMs(prepare_begin);
	PrintStage("1-prepare-total", sink.prepare_ms[0]);

	// Reinitialize mat_data with LHS child types for the operator phase; this
	// releases the collected build-side input.
	stage_begin = SpalmClock::now();
	sink.mat_data = make_uniq<ColumnDataCollection>(sink.context_ref, sink.lhs_child_types);
	sink.mat_data->InitializeAppend(sink.append_state);
	PrintStage("1-input-deallocate", ElapsedMs(stage_begin));
	sink.collect_begin = SpalmClock::now();
	return SinkFinalizeType::READY;
}

class PhysicalSpalmOperatorState : public OperatorState {
public:
	explicit PhysicalSpalmOperatorState(ClientContext &context, const vector<LogicalType> &lhs_types)
	    : r_mat_data(context, lhs_types) {
		r_mat_data.InitializeAppend(append_state);
	}

	ColumnDataCollection r_mat_data;
	ColumnDataAppendState append_state;
};

unique_ptr<OperatorState> PhysicalSpalm::GetOperatorState(ExecutionContext &context) const {
	return make_uniq<PhysicalSpalmOperatorState>(context.client, lhs_child_types);
}

// Where the output of a result with zeros spends its time, by row: looking for the zeros,
// looking them up in S, the pattern of the row, and appending tuples by values or by
// pattern. Timed only with SPALM_ROW_TIMES set, as a clock costs as much as a short row.
enum RowStep { kScanZeros, kProbeZeros, kFillPattern, kEmitValues, kEmitPattern, kRowSteps };
static const char *const kRowStepNames[kRowSteps] = {"row-scan-cpu", "row-probe-cpu", "row-fill-cpu",
                                                     "row-emit-values-cpu", "row-emit-pattern-cpu"};

class PhysicalSpalmGlobalSourceState : public GlobalSourceState {
public:
	explicit PhysicalSpalmGlobalSourceState(const PhysicalSpalm &op)
	    : next_row(0), total_rows(0), num_threads(1), chunk_rows(200) {
		if (op.sink_state) {
			auto &sink = op.sink_state->Cast<PhysicalSpalmGlobalState>();
			num_threads = (idx_t)sink.threads;
		}
		const char *row_times_env = getenv("SPALM_ROW_TIMES");
		row_times = row_times_env && *row_times_env && std::string(row_times_env) != "0";
	}

	// The physical result rows are known only after the multiplication.
	void SetResultRows(idx_t rows) {
		total_rows = rows;
		chunk_rows = std::max((idx_t)1, std::min((idx_t)200, (total_rows + num_threads - 1) / num_threads));
	}

	idx_t MaxThreads() override {
		return num_threads;
	}

	// The multiplication runs once, by the first source thread (see
	// GetDataInternal); `multiplied` is set after it finished or failed,
	// and after total_rows / chunk_rows are set.
	mutex multiply_lock;
	std::atomic<bool> multiplied {false};
	std::exception_ptr multiply_error;

	std::atomic<idx_t> next_row;
	// Built by the first row that needs it. Many results have zeros in no row.
	std::once_flag pattern_once;
	unique_ptr<spalm_output::ResultPattern> pattern;
	std::atomic<idx_t> pattern_rows {0};
	std::atomic<idx_t> probed_rows {0};
	bool row_times = false;
	// Summed over the threads as each one finishes
	mutex row_ns_lock;
	int64_t row_ns[kRowSteps] = {};
	// Source threads that started and finished; the last one to finish
	// reports the output time once.
	std::atomic<idx_t> threads_started {0};
	std::atomic<idx_t> threads_finished {0};
	std::atomic<bool> output_reported {false};
	idx_t total_rows;
	idx_t num_threads;
	idx_t chunk_rows;
};

// How the current row is emitted: by its nonzero values, by those and then the zeros
// that matched, or by its pattern
enum class RowOutput { kValues, kZeros, kPattern };

class PhysicalSpalmLocalSourceState : public LocalSourceState {
public:
	PhysicalSpalmLocalSourceState() : row(0), row_end(0), col(0) {
	}
	// Physical result rows [row, row_end) claimed by this thread; the
	// current row continues at column col.
	idx_t row;
	idx_t row_end;
	size_t col;
	RowOutput output = RowOutput::kValues;
	std::vector<uint64_t> pattern;
	// kZeros: the columns of the zeros that matched, emitted from next_zero on
	std::vector<int> zeros;
	size_t next_zero = 0;
	spalm_output::RowKeys row_keys;
	// With SPALM_ROW_TIMES: the time since `mark` goes to a step
	SpalmClock::time_point mark;
	int64_t row_ns[kRowSteps] = {};

	void Mark(RowStep step) {
		const auto now = SpalmClock::now();
		row_ns[step] += std::chrono::duration_cast<std::chrono::nanoseconds>(now - mark).count();
		mark = now;
	}
};

// A row without zeros needs nothing more: all of its pairs have matched. In any
// other row, a zero can be a sum of pairs that matched, or a pair without a match.
// A row with few zeros looks each one up: whether its column of S shares a key with
// the row of R. A row with more takes the pattern, built once for all rows, which ORs
// at most a bitset of the result's row per key of the row. So the lookup may read as
// many keys of S as that has words, and the row gets its pattern past them.
template <typename T>
static void PrepareRow(const T *row, idx_t N, PhysicalSpalmGlobalState &sink, PhysicalSpalmGlobalSourceState &gstate,
                       PhysicalSpalmLocalSourceState &lstate, bool timed) {
	const int *r_ptr = sink.r_layout.RowPointers(sink.Ri.get());
	const int *s_ptr = sink.s_layout.RowPointers(sink.Sj.get());
	const int begin = r_ptr[lstate.row], count = r_ptr[lstate.row + 1] - begin;
	lstate.next_zero = 0;
	// A row of R without keys matches nothing: its row of the result is all zero
	const size_t budget = static_cast<size_t>(count) * ((N + 63) / 64);
	const bool few = count == 0 || spalm_output::CollectZeros(row, N, s_ptr, budget, lstate.zeros);
	if (timed) {
		lstate.Mark(kScanZeros);
	}
	if (few) {
		if (count == 0) {
			lstate.zeros.clear();
		}
		lstate.output = lstate.zeros.empty() ? RowOutput::kValues : RowOutput::kZeros;
		if (lstate.output == RowOutput::kZeros) {
			lstate.row_keys.Set(sink.Rk.get() + begin, count);
			lstate.row_keys.KeepMatched(s_ptr, sink.Sk.get(), lstate.zeros);
			lstate.row_keys.Clear();
			gstate.probed_rows.fetch_add(1, std::memory_order_relaxed);
			if (timed) {
				lstate.Mark(kProbeZeros);
			}
		}
		return;
	}
	lstate.output = RowOutput::kPattern;
	lstate.zeros.clear();
	std::call_once(gstate.pattern_once, [&] {
		const auto build_begin = SpalmClock::now();
		gstate.pattern = make_uniq<spalm_output::ResultPattern>(s_ptr, sink.Sk.get(), sink.s_layout.Rows(), sink.K,
		                                                        sink.threads);
		PrintStage("pattern-build", ElapsedMs(build_begin));
		static const char *const steps[] = {"pattern-build-keys", "pattern-build-bounds", "pattern-build-layout",
		                                    "pattern-build-fill"};
		for (int step = 0; step < spalm_output::ResultPattern::kBuildSteps; ++step) {
			PrintStage(steps[step], gstate.pattern->BuildMs()[step]);
		}
	});
	// The build is a stage of its own, and waiting for it is not this row's
	if (timed) {
		lstate.mark = SpalmClock::now();
	}
	lstate.pattern.resize(gstate.pattern->Words());
	gstate.pattern->FillRow(sink.Rk.get() + begin, count, lstate.pattern.data());
	gstate.pattern_rows.fetch_add(1, std::memory_order_relaxed);
	if (timed) {
		lstate.Mark(kFillPattern);
	}
}

// Fills one output chunk with up to `capacity` result entries, row by row (no
// division per tuple; AVX-512 compressing stores when the CPU has them).
// Returns the number of tuples.
template <typename T>
static idx_t FillOutputChunk(const T *res, idx_t N, PhysicalSpalmGlobalState &sink,
                             PhysicalSpalmGlobalSourceState &gstate, PhysicalSpalmLocalSourceState &lstate,
                             int *out_rows, int *out_cols, double *out_vals, idx_t capacity) {
	const bool simd = spalm_output::EmitRowNonzerosHasAvx512();
	const bool timed = sink.needs_pattern && gstate.row_times;
	const auto &r_layout = sink.r_layout;
	const int *col_ids = sink.s_layout.OriginalRows();
	idx_t idx = 0;
	while (idx < capacity) {
		if (lstate.row >= lstate.row_end) {
			const idx_t row_start = gstate.next_row.fetch_add(gstate.chunk_rows);
			if (row_start >= gstate.total_rows) {
				break;
			}
			lstate.row = row_start;
			lstate.row_end = std::min(row_start + gstate.chunk_rows, gstate.total_rows);
			lstate.col = 0;
		}
		const T *row = res + lstate.row * N;
		if (timed) {
			lstate.mark = SpalmClock::now();
		}
		const uint64_t *pattern = lstate.pattern.data();
		if (sink.matched) {
			// The library marked the pairs that matched
			lstate.output = RowOutput::kPattern;
			pattern = sink.matched.get() + lstate.row * sink.matched_words;
		} else if (sink.needs_pattern && lstate.col == 0) {
			// A row in kZeros continues at col == N with its zeros, so it is not prepared twice
			PrepareRow(row, N, sink, gstate, lstate, timed);
			pattern = lstate.pattern.data();
		}
		const int row_id = r_layout.OriginalRow(lstate.row);
		if (lstate.output == RowOutput::kPattern) {
			idx = spalm_output::EmitRowPattern(row, pattern, lstate.col, N, row_id, col_ids, out_rows, out_cols,
			                                   out_vals, idx, capacity, simd);
		} else {
			idx = spalm_output::EmitRowNonzeros(row, lstate.col, N, row_id, col_ids, out_rows, out_cols, out_vals, idx,
			                                    capacity, simd);
			if (lstate.col == N) {
				idx = spalm_output::EmitColumns(row, lstate.zeros, lstate.next_zero, row_id, col_ids, out_rows, out_cols,
				                                out_vals, idx, capacity);
			}
		}
		if (timed) {
			lstate.Mark(lstate.output == RowOutput::kPattern ? kEmitPattern : kEmitValues);
		}
		if (lstate.col == N && lstate.next_zero == lstate.zeros.size()) {
			lstate.row++;
			lstate.col = 0;
			lstate.output = RowOutput::kValues;
			lstate.zeros.clear();
			lstate.next_zero = 0;
		}
	}
	return idx;
}

OperatorResultType PhysicalSpalm::Execute(
    ExecutionContext &context, DataChunk &input, DataChunk &chunk, GlobalOperatorState &gstate_p, OperatorState &state_p
) const {
	auto &op_state = state_p.Cast<PhysicalSpalmOperatorState>();
	op_state.r_mat_data.Append(op_state.append_state, input);
	return OperatorResultType::NEED_MORE_INPUT;
}

OperatorFinalizeResultType PhysicalSpalm::FinalExecute(
    ExecutionContext &context, DataChunk &output, GlobalOperatorState &gstate_p, OperatorState &state_p
) const {
	auto &sink = sink_state->Cast<PhysicalSpalmGlobalState>();
	auto &op_state = state_p.Cast<PhysicalSpalmOperatorState>();
	// Only merge this thread's probe-side input; the multiplication runs in
	// the output phase, after every probe-side thread has been merged. It used
	// to run in the FinalExecute that counted as many arrivals as operator
	// states had been created so far. A thread that created its state after
	// the others had finished was not counted: the multiplication started
	// without its input, which was then dropped (e.g. one row group of S).
	lock_guard<mutex> guard(sink.lock);
	const auto combine_begin = SpalmClock::now();
	sink.mat_data->Combine(op_state.r_mat_data);
	sink.combine_ms[1] += ElapsedMs(combine_begin);
	output.SetCardinality(0);
	return OperatorFinalizeResultType::FINISHED;
}

// Whether the multiplication overflowed or underflowed. The processor keeps both
// in sticky flags of each thread at no cost, so the kernels need no check. The
// library runs on these threads of OpenMP. Only needed with needs_pattern, as
// nothing else can overflow or underflow.
static void ClearFloatFlags(int num_threads) {
#pragma omp parallel num_threads(num_threads)
	std::feclearexcept(FE_OVERFLOW | FE_UNDERFLOW);
}

static int ReadFloatFlags(int num_threads) {
	int flags = 0;
#pragma omp parallel num_threads(num_threads) reduction(| : flags)
	flags |= std::fetestexcept(FE_OVERFLOW | FE_UNDERFLOW);
	return flags;
}

// Builds S's CSR and the result buffer, then calls the native library. Runs
// once, at the start of the output phase.
void PhysicalSpalm::Multiply(ClientContext &context) const {
	auto &sink = sink_state->Cast<PhysicalSpalmGlobalState>();
	const auto prepare_begin = SpalmClock::now();
	// Scanning the probe side and merging it, until the output phase starts.
	PrintStage("2-collect-input", ElapsedMs(sink.collect_begin, prepare_begin));
	PrintStage("2-combine-input", sink.combine_ms[1]);
	const int num_threads = sink.threads;
	omp_set_num_threads(num_threads);

	// Derive operator-side dimensions from collected data
	const int op_nnz = CheckedEntryCount(*sink.mat_data);
	auto stage_begin = SpalmClock::now();
	const auto s_max = ScanInputMax(
	    *sink.mat_data, num_threads, sink.lhs_agg_idx, sink.lhs_join_idx, sink.lhs_val_idx, sink.use_float
	);
	PrintStage("2-dimension-scan", ElapsedMs(stage_begin));

	const int N = s_max.max_row + 1; // 0 when no entry participates
	// K is the bigger of the two max join column values (+1)
	const int K = std::max(sink.sink_max_join, s_max.max_key) + 1;
	sink.N = N;
	sink.K = K;
	sink.needs_pattern =
	    spalm_output::InputNeedsPattern(sink.value_flags) || spalm_output::InputNeedsPattern(s_max.value_flags);
	printf("[SPALM FinalExecute] derived N=%d, K=%d, op_nnz=%d\n", N, K, op_nnz);
	// Allocate operator CSR arrays (same initialization as the sink side).
	stage_begin = SpalmClock::now();
	sink.Sj = make_uniq_array<int>(N + 1);
	sink.Sk = make_uniq_array_uninitialized<int>(op_nnz);
	if (sink.use_float) {
		sink.Sv_f = make_uniq_array_uninitialized<float>(op_nnz);
	} else {
		sink.Sv_d = make_uniq_array_uninitialized<double>(op_nnz);
	}
	PrintStage("2-csr-allocate", ElapsedMs(stage_begin));

	stage_begin = SpalmClock::now();
	std::vector<DataChunk> t_chunks(num_threads);
	for (auto &chunk : t_chunks) {
		sink.mat_data->InitializeScanChunk(chunk);
	}
	PrintStage("2-scan-chunks-prepare", ElapsedMs(stage_begin));

	if (sink.use_float) {
		BuildCSR<float>(
		    sink.Sj.get(), sink.Sk.get(), sink.Sv_f.get(), *sink.mat_data, N, num_threads, lhs_join_idx, lhs_agg_idx,
		    lhs_val_idx, t_chunks, "2", sink.duplicates
		);
	} else {
		BuildCSR<double>(
		    sink.Sj.get(), sink.Sk.get(), sink.Sv_d.get(), *sink.mat_data, N, num_threads, lhs_join_idx, lhs_agg_idx,
		    lhs_val_idx, t_chunks, "2", sink.duplicates
		);
	}

	const auto conf = ReadSpalmConf(context);
	// Loaded before the result is allocated: whether the result must be
	// zeroed depends on it. Kept out of the preparation time, as before.
	stage_begin = SpalmClock::now();
	const SpalmLibrary library = LoadSpalmLibrary(ResolveSpalmLibrary(context, conf));
	const double load_ms = ElapsedMs(stage_begin);
	PrintStage("library-load", load_ms);
	auto compact_rows_entry = conf.values.find("result_compact_rows");
	const bool compact_rows = compact_rows_entry == conf.values.end() || compact_rows_entry->second != "0";
	auto parallel_zero_entry = conf.values.find("result_parallel_zero");
	const bool parallel_zero = parallel_zero_entry == conf.values.end() || parallel_zero_entry->second != "0";
	auto huge_pages_entry = conf.values.find("result_huge_pages");
	const bool huge_pages = huge_pages_entry == conf.values.end() || huge_pages_entry->second != "0";
	// Where the huge-page result starts. Its rows are physical_N values apart
	// (16000 doubles: a multiple of 64 bytes). In encoder-1025 (outer-v5)
	// DD ran 4-8% faster with cache-line aligned rows (offset 0), while SS
	// and MKL sparse ran 7-21% slower unless the rows start 16 bytes past a
	// cache line, as with malloc's placement in outer-v4. So SS keeps that
	// placement, and so do the MKL baselines (MKL sparse and MKL dense): they
	// run on the result as an ordinary malloc'd buffer would place it, not
	// at a position chosen for SPALM. DD, SD, DS and automatic dispatch get
	// cache-line aligned rows. result_base_offset overrides.
	auto init_method_entry = conf.values.find("init_method");
	const bool malloc_placement =
	    init_method_entry != conf.values.end() &&
	    (init_method_entry->second == "sparse" || init_method_entry->second == "FORCE_MKL_SPARSE" ||
	     init_method_entry->second == "FORCE_MKL_DENSE");
	size_t base_offset = malloc_placement ? 16 : 0;
	// The result is a fresh anonymous mapping, which reads as zeros
	// (result_fresh_mapping = 0 restores the outer-v17 allocation). For
	// SPALM's methods it is pre-faulted by the query's threads with
	// MADV_POPULATE_WRITE (result_prefault = memset zeroes it instead, the
	// outer-v17 pass): their scattered first writes cost much more inside the
	// kernels (outer-v18). The forced MKL backends write the whole result
	// block by block (GEMM with beta = 0, mkl_sparse_?_spmmd), so they first-
	// touch it themselves, which cost them no more than the pre-fault pass in
	// outer-v17. Without a fresh mapping, SPALM's methods are zeroed and the
	// MKL backends get the result unzeroed when the library accepts that.
	// result_mkl_skip_zero = 0 prepares the MKL result like SPALM's.
	auto fresh_mapping_entry = conf.values.find("result_fresh_mapping");
	const bool fresh_mapping = fresh_mapping_entry == conf.values.end() || fresh_mapping_entry->second != "0";
	auto prefault_entry = conf.values.find("result_prefault");
	const bool populate = prefault_entry == conf.values.end() || prefault_entry->second != "memset";
	auto mkl_skip_zero_entry = conf.values.find("result_mkl_skip_zero");
	const bool forced_mkl = init_method_entry != conf.values.end() && (init_method_entry->second == "FORCE_MKL_DENSE" ||
	                                                                   init_method_entry->second == "FORCE_MKL_SPARSE");
	const bool mkl_first_touch =
	    forced_mkl && (mkl_skip_zero_entry == conf.values.end() || mkl_skip_zero_entry->second != "0");
	const bool mkl_unzeroed = mkl_first_touch && library.accepts_unzeroed_result;
	// Whether the result handed to the library holds zeros (result_zeroed).
	bool result_zeroed = true;
	auto base_offset_entry = conf.values.find("result_base_offset");
	if (base_offset_entry != conf.values.end()) {
		base_offset = (size_t)std::stoull(base_offset_entry->second);
	}

	// Drop structurally empty rows of R and S (rows without a stored entry):
	// compact row-pointer views over the same entry arrays, so K, entry
	// offsets and order stay intact. The result is physical_M x physical_N;
	// output restores the original IDs.
	stage_begin = SpalmClock::now();
	sink.s_layout.Initialize(sink.Sj.get(), N, 0, compact_rows);
	sink.r_layout.Initialize(sink.Ri.get(), sink.M, sink.s_layout.Rows(), compact_rows);
	const int physical_M = sink.r_layout.Rows();
	const int physical_N = sink.s_layout.Rows();
	PrintStage("result-layout-prepare", ElapsedMs(stage_begin));
	printf(
	    "[result-layout] logical_M=%d physical_M=%d logical_N=%d physical_N=%d compact_rows=%d bytes=%llu\n", sink.M,
	    physical_M, N, physical_N, int(compact_rows),
	    (unsigned long long)(sink.use_float ? sink.r_layout.Bytes<float>() : sink.r_layout.Bytes<double>())
	);
	// Same limit as the native library (its kernels use 32-bit result
	// offsets), checked before allocating the dense result.
	if (physical_M > 0 && physical_N > 0 && (long long)(physical_M + 16) * (physical_N + 32) > INT_MAX) {
		throw InvalidInputException(
		    "SPALM result of %d x %d is too large for 32-bit result offsets", physical_M, physical_N
		);
	}

	// Allocate result array (physical_M x physical_N). make_uniq_array would
	// zero it on one thread (new T[n]()), page faults included; allocate a
	// fresh zero-filled mapping (on 2 MiB huge pages unless
	// result_huge_pages = 0) and pre-fault it once with the query's threads,
	// spreading the first touch. Otherwise it is allocated uninitialized and
	// zeroed once with the query's threads.
	{
		const auto alloc_begin = SpalmClock::now();
		const idx_t result_size = sink.r_layout.Elements();
		bool advised = false, zeroed = false;
		if (sink.use_float) {
			sink.result_f = spalm_output::AllocateDenseResult<float>(result_size, huge_pages, base_offset, advised,
			                                                         zeroed, fresh_mapping);
		} else {
			sink.result_d = spalm_output::AllocateDenseResult<double>(result_size, huge_pages, base_offset, advised,
			                                                          zeroed, fresh_mapping);
		}
		const auto zero_begin = SpalmClock::now();
		PrintStage("result-allocate", ElapsedMs(alloc_begin, zero_begin));
		printf("[result-huge-pages] requested=%d advised=%d base_offset=%zu fresh_mapping=%d\n", int(huge_pages),
		       int(advised), advised || zeroed ? base_offset : (size_t)0, int(zeroed));
		const char *zero_method = "memset";
		bool populated = false;
		if (zeroed && populate && !mkl_first_touch) {
			populated = sink.use_float
			                ? spalm_output::PrefaultDenseResult(sink.result_f.get(), result_size, num_threads, advised)
			                : spalm_output::PrefaultDenseResult(sink.result_d.get(), result_size, num_threads, advised);
		}
		if (zeroed && mkl_first_touch) {
			// Zero-filled; MKL first-touches it while writing it.
			zero_method = "none";
		} else if (populated) {
			zero_method = "populate";
		} else if (!zeroed && mkl_unzeroed) {
			// The library writes every element (result_zeroed = 0).
			zero_method = "skipped";
			result_zeroed = false;
		} else if (sink.use_float) {
			spalm_output::ZeroDenseResult(sink.result_f.get(), result_size, num_threads, parallel_zero);
		} else {
			spalm_output::ZeroDenseResult(sink.result_d.get(), result_size, num_threads, parallel_zero);
		}
		printf("[result-zero-policy] parallel_requested=%d threads=%d method=%s\n", int(parallel_zero), num_threads,
		       zero_method);
		const auto zero_end = SpalmClock::now();
		PrintStage("result-zero", ElapsedMs(zero_begin, zero_end));
		PrintStage("result-allocate-zero-total", ElapsedMs(alloc_begin, zero_end));
	}

	// A zero in the result can be a sum of pairs that matched: the library's
	// kernels mark them. MKL cannot, so a forced MKL method or the library's MKL
	// fallback leaves it to the pattern of the output; stored_entries below
	// overrides a forced MKL method with "sparse". matched_pairs = 0 always
	// leaves it to the output.
	const bool stored_entries = (sink.value_flags | s_max.value_flags) & spalm_output::kValuesTooLarge;
	auto matched_pairs_entry = conf.values.find("matched_pairs");
	const bool matched_pairs = matched_pairs_entry == conf.values.end() || matched_pairs_entry->second != "0";
	if (sink.needs_pattern && matched_pairs && library.spalm_matched && (stored_entries || !forced_mkl)) {
		const auto alloc_begin = SpalmClock::now();
		sink.matched_words = (static_cast<size_t>(physical_N) + 63) / 64;
		const size_t words = static_cast<size_t>(physical_M) * sink.matched_words;
		bool advised = false, zeroed = false;
		sink.matched =
		    spalm_output::AllocateDenseResult<uint64_t>(words, huge_pages, 0, advised, zeroed, fresh_mapping);
		if (!zeroed) {
			spalm_output::ZeroDenseResult(sink.matched.get(), words, num_threads, parallel_zero);
		}
		PrintStage("matched-allocate", ElapsedMs(alloc_begin));
		printf("[matched-pairs] words=%zu bytes=%zu fresh_mapping=%d\n", words, words * sizeof(uint64_t),
		       int(zeroed));
	}

	t_chunks.clear();
	sink.prepare_ms[1] = ElapsedMs(prepare_begin) - load_ms;
	PrintStage("2-prepare-total", sink.prepare_ms[1]);

	// Failures throw instead of returning an empty result without an error.
	const spalm_fn_t spalm_fn = library.spalm;

	stage_begin = SpalmClock::now();
	std::vector<std::string> kv_keys, kv_vals;
	std::string threads_str = std::to_string(sink.threads);
	kv_keys.push_back("threads");
	kv_vals.push_back(threads_str);
	// Ahead of the configuration's keys, so a stray result_zeroed there
	// cannot claim a zeroed result that is not (the library takes the first).
	kv_keys.push_back("result_zeroed");
	kv_vals.push_back(result_zeroed ? "1" : "0");
	// Also ahead of them: the SS kernels multiply stored entries only.
	if (stored_entries) {
		kv_keys.push_back("init_method");
		kv_vals.push_back("sparse");
	}
	printf("[result-values] pattern=%d stored_entries=%d\n", int(sink.needs_pattern), int(stored_entries));
	for (auto &kv : conf.values) {
		if (kv.first == "so_dir")
			continue;
		kv_keys.push_back(kv.first);
		kv_vals.push_back(kv.second);
	}
	std::vector<const char *> c_keys, c_vals;
	for (size_t i = 0; i < kv_keys.size(); i++) {
		c_keys.push_back(kv_keys[i].c_str());
		c_vals.push_back(kv_vals[i].c_str());
	}

	int spalm_type = sink.use_float ? 0 : 1;
	void *rv_ptr = sink.use_float ? (void *)sink.Rv_f.get() : (void *)sink.Rv_d.get();
	void *sv_ptr = sink.use_float ? (void *)sink.Sv_f.get() : (void *)sink.Sv_d.get();
	void *result_ptr = sink.use_float ? (void *)sink.result_f.get() : (void *)sink.result_d.get();
	const double config_ms = ElapsedMs(stage_begin);
	PrintStage("library-config-setup", config_ms);
	// Wrapper work before the native call: both CSR builds, the
	// result buffer, library lookup and options.
	PrintStage("prepare-total", sink.prepare_ms[0] + sink.prepare_ms[1] + load_ms + config_ms);

	if (sink.needs_pattern) {
		ClearFloatFlags(num_threads);
	}
	stage_begin = SpalmClock::now();
	int marked = 0;
	const int status =
	    sink.matched
	        ? library.spalm_matched(spalm_type, sink.r_layout.RowPointers(sink.Ri.get()), sink.Rk.get(), rv_ptr,
	                                sink.s_layout.RowPointers(sink.Sj.get()), sink.Sk.get(), sv_ptr, sink.r_layout.Rows(),
	                                sink.K, sink.s_layout.Rows(), false, result_ptr, nullptr, nullptr, nullptr,
	                                (int)c_keys.size(), c_keys.data(), c_vals.data(), sink.matched.get(), &marked)
	        : spalm_fn(spalm_type, sink.r_layout.RowPointers(sink.Ri.get()), sink.Rk.get(), rv_ptr,
	                   sink.s_layout.RowPointers(sink.Sj.get()), sink.Sk.get(), sv_ptr, sink.r_layout.Rows(), sink.K,
	                   sink.s_layout.Rows(), false, result_ptr, nullptr, nullptr, nullptr, (int)c_keys.size(),
	                   c_keys.data(), c_vals.data());
	sink.native_end = SpalmClock::now();
	PrintStage("library-call-total", ElapsedMs(stage_begin, sink.native_end));
	if (status != 0) {
		// 1 invalid argument, 2 parameter files, 3 MKL baseline, 4 internal.
		throw InvalidInputException(
		    "spalm: native multiplication failed with status %d (see stderr for details)", status
		);
	}
	if (sink.matched) {
		printf("[matched-pairs] marked=%d\n", marked);
		if (!marked) {
			sink.matched.reset();
		}
	}
	if (sink.needs_pattern) {
		// DuckDB gets the same inf or tiny numbers, and tells nothing.
		const int flags = ReadFloatFlags(num_threads);
		// After the lines of this query, whatever the library flushed
		fflush(stdout);
		if (flags & FE_OVERFLOW) {
			fprintf(stderr, "[spalm-warning] the multiplication overflowed: the result can have inf or nan\n");
		}
		if (flags & FE_UNDERFLOW) {
			fprintf(stderr, "[spalm-warning] the multiplication underflowed: products below 1e-308 lose precision\n");
		}
	}
}

//===--------------------------------------------------------------------===//
// Source Interface
//===--------------------------------------------------------------------===//
unique_ptr<GlobalSourceState> PhysicalSpalm::GetGlobalSourceState(ClientContext &context) const {
	return make_uniq<PhysicalSpalmGlobalSourceState>(*this);
}

unique_ptr<LocalSourceState>
PhysicalSpalm::GetLocalSourceState(ExecutionContext &context, GlobalSourceState &gstate) const {
	gstate.Cast<PhysicalSpalmGlobalSourceState>().threads_started++;
	return make_uniq<PhysicalSpalmLocalSourceState>();
}

SourceResultType
PhysicalSpalm::GetDataInternal(ExecutionContext &context, DataChunk &output, OperatorSourceInput &input) const {
	auto &sink = sink_state->Cast<PhysicalSpalmGlobalState>();
	auto &gstate = input.global_state.Cast<PhysicalSpalmGlobalSourceState>();
	auto &lstate = input.local_state.Cast<PhysicalSpalmLocalSourceState>();

	// This source pipeline is scheduled only after the probe pipeline has
	// finished, i.e. after every probe-side FinalExecute, so the collected
	// input is complete. The first source thread multiplies; others wait.
	if (!gstate.multiplied.load(std::memory_order_acquire)) {
		lock_guard<mutex> guard(gstate.multiply_lock);
		if (!gstate.multiplied.load(std::memory_order_relaxed)) {
			try {
				Multiply(context.client);
				gstate.SetResultRows((idx_t)sink.r_layout.Rows());
			} catch (...) {
				gstate.multiply_error = std::current_exception();
			}
			gstate.multiplied.store(true, std::memory_order_release);
		}
	}
	if (gstate.multiply_error) {
		std::rethrow_exception(gstate.multiply_error);
	}

	const idx_t N = (idx_t)sink.s_layout.Rows();
	const idx_t total_rows = gstate.total_rows; // physical M
	const idx_t vec_size = 2048;

	output.SetCapacity(vec_size);
	// Use dynamic output indices: rhs_output_idx for M dimension (ai), lhs_output_idx for N dimension (bj)
	int *out_m_dim = (int *)output.data[rhs_output_idx].GetData(); // ai
	int *out_n_dim = (int *)output.data[lhs_output_idx].GetData(); // bj
	// SUM returns DOUBLE, so the output column is DOUBLE for float inputs too.
	double *out_val = (double *)output.data[2].GetData();
	const idx_t idx =
	    sink.use_float
	        ? FillOutputChunk(sink.result_f.get(), N, sink, gstate, lstate, out_m_dim, out_n_dim, out_val, vec_size)
	        : FillOutputChunk(sink.result_d.get(), N, sink, gstate, lstate, out_m_dim, out_n_dim, out_val, vec_size);

	output.SetCardinality(idx);

	bool all_done = (gstate.next_row.load() >= total_rows) && (lstate.row >= lstate.row_end);
	if (all_done) {
		if (gstate.row_times && sink.needs_pattern) {
			lock_guard<mutex> guard(gstate.row_ns_lock);
			for (int step = 0; step < kRowSteps; ++step) {
				gstate.row_ns[step] += lstate.row_ns[step];
			}
		}
		// The old check against MaxThreads() never fired when DuckDB ran
		// fewer source threads.
		if (++gstate.threads_finished == gstate.threads_started.load() && !gstate.output_reported.exchange(true)) {
			PrintStage("output-total", ElapsedMs(sink.native_end));
			// With matched pairs, every row is emitted by them
			if (sink.needs_pattern && !sink.matched) {
				printf("[pattern-rows] %llu of %llu\n", (unsigned long long)gstate.pattern_rows.load(),
				       (unsigned long long)total_rows);
				printf("[probed-rows] %llu of %llu\n", (unsigned long long)gstate.probed_rows.load(),
				       (unsigned long long)total_rows);
			}
			if (gstate.row_times && sink.needs_pattern) {
				// Summed over the source threads, so against output-total only as shares
				lock_guard<mutex> guard(gstate.row_ns_lock);
				for (int step = 0; step < kRowSteps; ++step) {
					PrintStage(kRowStepNames[step], gstate.row_ns[step] / 1e6);
				}
			}
		}
		return SourceResultType::FINISHED;
	}
	return SourceResultType::HAVE_MORE_OUTPUT;
}

//===--------------------------------------------------------------------===//
// Pipeline Construction
//===--------------------------------------------------------------------===//
void PhysicalSpalm::BuildPipelines(Pipeline &current, MetaPipeline &meta_pipeline) {
	PhysicalJoin::BuildJoinPipelines(current, meta_pipeline, *this);
}

vector<const_reference<PhysicalOperator>> PhysicalSpalm::GetSources() const {
	return {*this};
}
} // namespace duckdb
