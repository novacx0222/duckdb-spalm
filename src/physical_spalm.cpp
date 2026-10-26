#include "physical_spalm.hpp"
#include "duckdb/common/exception.hpp"
#include "duckdb/main/client_context.hpp"
#include "duckdb/execution/operator/join/physical_join.hpp"
#include "duckdb/common/types/column/column_data_collection.hpp"
#include "duckdb/parallel/task_scheduler.hpp"

#include <dlfcn.h>
#include <iostream>
#include <climits>
#include <cmath>
#include <algorithm>
#include <exception>
#include <time.h>
#include <omp.h>
#include <atomic>
#include <condition_variable>
#include <fstream>
#include <sstream>
#include <dirent.h>
#include <mutex>
#include <unordered_map>
#include <string>

namespace duckdb {

double get_ms(struct timespec start, struct timespec end) {
	double seconds, nanoseconds, elapsed_ms;
	seconds = end.tv_sec - start.tv_sec;
	nanoseconds = end.tv_nsec - start.tv_nsec;
	return (seconds * 1000.0) + (nanoseconds / 1000000.0);
}

template <typename T>
void sort_row_indices(int start, int end, int *cols, T *vals) {
	int n = end - start;
	if (n <= 1)
		return;

	std::vector<int> p(n);
	for (int i = 0; i < n; i++)
		p[i] = i;

	std::sort(p.begin(), p.end(), [&](int a, int b) { return cols[start + a] < cols[start + b]; });

	std::vector<int> tmp_cols(n);
	std::vector<T> tmp_vals(n);
	for (int i = 0; i < n; i++) {
		tmp_cols[i] = cols[start + p[i]];
		tmp_vals[i] = vals[start + p[i]];
	}
	for (int i = 0; i < n; i++) {
		cols[start + i] = tmp_cols[i];
		vals[start + i] = tmp_vals[i];
	}
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
};

// Validates one input and returns its largest row ID and join key among the
// participating entries. Both index dense arrays, so NULL row IDs and
// negative or INT_MAX-sized IDs/keys are rejected instead of corrupting
// memory (the maxima used to start at 0, hiding negative values).
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
	int max_row = -1, max_key = -1, invalid = 0;
	OmpErrors errors;
#pragma omp parallel for num_threads(num_threads) schedule(static) reduction(max : max_row, max_key)                   \
    reduction(| : invalid)
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
	return result;
}

// Requires ScanInputMax to have validated `data`: participating entries
// have non-NULL, in-range row IDs.
template <typename T>
void BuildCSR(
    int *row_ptr, int *col_idx, T *vals, ColumnDataCollection &data, int num_rows, int num_threads, idx_t join_col_idx,
    idx_t agg_col_idx, idx_t val_col_idx, std::vector<DataChunk> &t_chunks
) {
	struct timespec f_start, f_end;

	clock_gettime(CLOCK_MONOTONIC, &f_start);
	std::vector<std::vector<int>> local_hist(num_threads, std::vector<int>(num_rows + 1, 0));
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

	clock_gettime(CLOCK_MONOTONIC, &f_end);
	printf("CSR offset counting time: %.3f ms\n", get_ms(f_start, f_end));

	clock_gettime(CLOCK_MONOTONIC, &f_start);
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

	clock_gettime(CLOCK_MONOTONIC, &f_end);
	printf("CSR writing time: %.3f ms\n", get_ms(f_start, f_end));

	clock_gettime(CLOCK_MONOTONIC, &f_start);
#pragma omp parallel for schedule(dynamic)
	for (int r = 0; r < num_rows; r++) {
		// sort_row_indices allocates temporary vectors per row.
		errors.Run([&] { sort_row_indices<T>(row_ptr[r], row_ptr[r + 1], col_idx, vals); });
	}
	errors.Rethrow();
	clock_gettime(CLOCK_MONOTONIC, &f_end);
	printf("CSR sorting time: %.3f ms\n", get_ms(f_start, f_end));
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

// Find the first *.so file in a directory.
static std::string FindSoInDir(const std::string &dir) {
	DIR *d = opendir(dir.c_str());
	if (!d) {
		std::cerr << "spalm: cannot open so_dir: " << dir << "\n";
		return "";
	}
	std::string result;
	struct dirent *ent;
	while ((ent = readdir(d)) != nullptr) {
		std::string name = ent->d_name;
		if (name.size() > 3 && name.substr(name.size() - 3) == ".so") {
			result = dir + "/" + name;
			break;
		}
	}
	closedir(d);
	return result;
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

// Load (once per path) and return the native entry point. Serialized: the
// cache used to be two unsynchronized statics that concurrent queries could
// race on, and it ignored a changed so_dir until restart. Libraries stay
// loaded for the process lifetime: unloading MKL corrupts its thread pool.
static spalm_fn_t LoadSpalmLibrary(const std::string &so_path) {
	static std::mutex lock;
	static std::unordered_map<std::string, spalm_fn_t> loaded;
	std::lock_guard<std::mutex> guard(lock);
	auto entry = loaded.find(so_path);
	if (entry != loaded.end()) {
		return entry->second;
	}
	void *handle = dlopen(so_path.c_str(), RTLD_LAZY);
	if (!handle) {
		throw InvalidInputException("spalm: cannot open library %s: %s", so_path, dlerror());
	}
	auto fn = (spalm_fn_t)dlsym(handle, "spalm");
	if (!fn) {
		const std::string error = dlerror();
		dlclose(handle);
		throw InvalidInputException("spalm: symbol 'spalm' not found in %s: %s", so_path, error);
	}
	loaded[so_path] = fn;
	return fn;
}

PhysicalSpalm::PhysicalSpalm(
    vector<LogicalType> types, PhysicalOperator &left, PhysicalOperator &right, SpalmColumnBindings col_bindings,
    bool use_float_p, idx_t estimated_cardinality
)
    : PhysicalOperator(PhysicalOperatorType::EXTENSION, std::move(types), estimated_cardinality),
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
	      mat_data(make_uniq<ColumnDataCollection>(context, op.GetRhsChildTypes())) {
		mat_data->InitializeAppend(append_state);

		threads = (int)TaskScheduler::GetScheduler(context).NumberOfThreads();

		total_parallel_operators = 0;
		threads_done = 0;

		M = 0;
		K = 0;
		N = 0;

		clock_gettime(CLOCK_MONOTONIC, &start);
	}

	~PhysicalSpalmGlobalState() {
		printf("~PhysicalSpalmGlobalState() called\n");
	}

	bool use_float;
	vector<LogicalType> lhs_child_types;
	idx_t rhs_join_idx, rhs_agg_idx, rhs_val_idx;
	idx_t lhs_join_idx, lhs_agg_idx, lhs_val_idx;
	ClientContext &context_ref;

	const int slice_size = 512;
	const int tile_size_agg_dim = 128;

	int M, K, N;
	int sink_max_join; // max value of join column on sink side

	unique_ptr<int[]> Ri;
	unique_ptr<int[]> Rk;
	unique_ptr<float[]> Rv_f;
	unique_ptr<double[]> Rv_d;

	unique_ptr<int[]> Sj;
	unique_ptr<int[]> Sk;
	unique_ptr<float[]> Sv_f;
	unique_ptr<double[]> Sv_d;

	unique_ptr<float[]> result_f;
	unique_ptr<double[]> result_d;
	bool done = false;
	// Set by the preparing thread on failure; waiting threads rethrow it.
	std::exception_ptr prepare_error;

	int threads;

	int total_parallel_operators;

	struct timespec start, end;

	unique_ptr<ColumnDataCollection> mat_data;
	ColumnDataAppendState append_state;
	mutex lock;
	std::condition_variable condition;
	int threads_done = 0;
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
	sink.mat_data->Combine(lstate.l_mat_data);
	return SinkCombineResultType::FINISHED;
}

SinkFinalizeType PhysicalSpalm::Finalize(
    Pipeline &pipeline, Event &event, ClientContext &context, OperatorSinkFinalizeInput &input
) const {
	auto &sink = input.global_state.Cast<PhysicalSpalmGlobalState>();
	double seconds, nanoseconds, elapsed_ms;
	struct timespec f_start;
	clock_gettime(CLOCK_MONOTONIC, &sink.end);
	seconds = sink.end.tv_sec - sink.start.tv_sec;
	nanoseconds = sink.end.tv_nsec - sink.start.tv_nsec;
	elapsed_ms = (seconds * 1000.0) + (nanoseconds / 1000000.0);
	printf("Start to finalize time: %.3f ms\n", elapsed_ms);

	const int sink_nnz = CheckedEntryCount(*sink.mat_data);
	printf("[SPALM Finalize] mat_data chunk count=%lu, total rows (nnz)=%d\n", sink.mat_data->ChunkCount(), sink_nnz);

	// Scan the collected data to find max(agg_col) and max(join_col)
	const int num_threads = sink.threads;
	omp_set_num_threads(num_threads);

	const auto r_max = ScanInputMax(
	    *sink.mat_data, num_threads, sink.rhs_agg_idx, sink.rhs_join_idx, sink.rhs_val_idx, sink.use_float
	);
	const int M = r_max.max_row + 1; // 0 when no entry participates
	sink.M = M;
	sink.sink_max_join = r_max.max_key;
	printf("[SPALM Finalize] derived M=%d, max_join=%d, nnz=%d\n", M, r_max.max_key, sink_nnz);

	// Allocate sink CSR arrays
	sink.Ri = make_uniq_array<int>(M + 1);
	memset(sink.Ri.get(), 0, sizeof(int) * (M + 1));
	sink.Rk = make_uniq_array<int>(sink_nnz);
	if (sink.use_float) {
		sink.Rv_f = make_uniq_array<float>(sink_nnz);
	} else {
		sink.Rv_d = make_uniq_array<double>(sink_nnz);
	}

	clock_gettime(CLOCK_MONOTONIC, &f_start);
	std::vector<DataChunk> t_chunks(num_threads);
	for (auto &chunk : t_chunks) {
		sink.mat_data->InitializeScanChunk(chunk);
	}

	if (sink.use_float) {
		BuildCSR<float>(
		    sink.Ri.get(), sink.Rk.get(), sink.Rv_f.get(), *sink.mat_data, M, num_threads, rhs_join_idx, rhs_agg_idx,
		    rhs_val_idx, t_chunks
		);
	} else {
		BuildCSR<double>(
		    sink.Ri.get(), sink.Rk.get(), sink.Rv_d.get(), *sink.mat_data, M, num_threads, rhs_join_idx, rhs_agg_idx,
		    rhs_val_idx, t_chunks
		);
	}

	clock_gettime(CLOCK_MONOTONIC, &sink.end);
	printf("1 CSR init time: %.3f ms\n", get_ms(sink.start, sink.end));

	// Reinitialize mat_data with LHS child types for the operator phase
	sink.mat_data = make_uniq<ColumnDataCollection>(sink.context_ref, sink.lhs_child_types);
	sink.mat_data->InitializeAppend(sink.append_state);
	clock_gettime(CLOCK_MONOTONIC, &sink.start);
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
	auto &sink = sink_state->Cast<PhysicalSpalmGlobalState>();
	lock_guard<mutex> client_guard(sink.lock);
	sink.total_parallel_operators++;
	return make_uniq<PhysicalSpalmOperatorState>(context.client, lhs_child_types);
}

class PhysicalSpalmGlobalSourceState : public GlobalSourceState {
public:
	explicit PhysicalSpalmGlobalSourceState(const PhysicalSpalm &op)
	    : next_row(0), total_rows(0), num_threads(1), chunk_rows(200) {
		if (op.sink_state) {
			auto &sink = op.sink_state->Cast<PhysicalSpalmGlobalState>();
			total_rows = (idx_t)sink.M;
			num_threads = (idx_t)sink.threads;
			chunk_rows = std::max((idx_t)1, std::min((idx_t)200, (total_rows + num_threads - 1) / num_threads));
		}
	}

	idx_t MaxThreads() override {
		return num_threads;
	}

	std::atomic<idx_t> next_row;
	std::atomic<idx_t> threads_finished {0};
	idx_t total_rows;
	idx_t num_threads;
	idx_t chunk_rows;
};

class PhysicalSpalmLocalSourceState : public LocalSourceState {
public:
	PhysicalSpalmLocalSourceState() : curr_pos(0), end_pos(0) {
	}
	idx_t curr_pos;
	idx_t end_pos;
};

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

	// Take the lock before reading `done`/`prepare_error` (the old check of
	// `done` was unsynchronized).
	unique_lock<mutex> l(sink.lock);
	if (sink.prepare_error) {
		std::rethrow_exception(sink.prepare_error);
	}
	if (!sink.done) {
		sink.mat_data->Combine(op_state.r_mat_data);
		sink.threads_done++;

		// If we aren't the last thread, we wait until preparation finishes or
		// fails. Waiting on the arrival count instead hung forever when the
		// last thread returned early or threw without notifying.
		if (sink.threads_done < sink.total_parallel_operators) {
			sink.condition.wait(l, [&] { return sink.done; });
			if (sink.prepare_error) {
				std::rethrow_exception(sink.prepare_error);
			}
		} else {
			try {
				struct timespec f_start;
				const int num_threads = sink.threads;
				omp_set_num_threads(num_threads);

				// Derive operator-side dimensions from collected data
				const int op_nnz = CheckedEntryCount(*sink.mat_data);
				const auto s_max = ScanInputMax(
				    *sink.mat_data, num_threads, sink.lhs_agg_idx, sink.lhs_join_idx, sink.lhs_val_idx, sink.use_float
				);

				const int N = s_max.max_row + 1; // 0 when no entry participates
				// K is the bigger of the two max join column values (+1)
				const int K = std::max(sink.sink_max_join, s_max.max_key) + 1;
				sink.N = N;
				sink.K = K;
				printf("[SPALM FinalExecute] derived N=%d, K=%d, op_nnz=%d\n", N, K, op_nnz);
				// Same limit as the native library (its kernels use 32-bit result
				// offsets), checked before allocating the dense M x N result.
				if (sink.M > 0 && N > 0 && (long long)(sink.M + 16) * (N + 32) > INT_MAX) {
					throw InvalidInputException(
					    "SPALM result of %d x %d is too large for 32-bit result offsets", sink.M, N
					);
				}

				// Allocate operator CSR arrays
				sink.Sj = make_uniq_array<int>(N + 1);
				memset(sink.Sj.get(), 0, sizeof(int) * (N + 1));
				sink.Sk = make_uniq_array<int>(op_nnz);
				if (sink.use_float) {
					sink.Sv_f = make_uniq_array<float>(op_nnz);
				} else {
					sink.Sv_d = make_uniq_array<double>(op_nnz);
				}

				// Allocate result array (M x N)
				{
					struct timespec alloc_start, alloc_end;
					clock_gettime(CLOCK_MONOTONIC, &alloc_start);
					if (sink.use_float) {
						sink.result_f = make_uniq_array<float>((idx_t)sink.M * N);
						memset(sink.result_f.get(), 0, sizeof(float) * (idx_t)sink.M * N);
					} else {
						sink.result_d = make_uniq_array<double>((idx_t)sink.M * N);
						memset(sink.result_d.get(), 0, sizeof(double) * (idx_t)sink.M * N);
					}
					clock_gettime(CLOCK_MONOTONIC, &alloc_end);
					printf("Result memset + allocation time: %.3f ms\n", get_ms(alloc_start, alloc_end));
				}

				clock_gettime(CLOCK_MONOTONIC, &f_start);
				std::vector<DataChunk> t_chunks(num_threads);
				for (auto &chunk : t_chunks) {
					sink.mat_data->InitializeScanChunk(chunk);
				}

				if (sink.use_float) {
					BuildCSR<float>(
					    sink.Sj.get(), sink.Sk.get(), sink.Sv_f.get(), *sink.mat_data, N, num_threads, lhs_join_idx,
					    lhs_agg_idx, lhs_val_idx, t_chunks
					);
				} else {
					BuildCSR<double>(
					    sink.Sj.get(), sink.Sk.get(), sink.Sv_d.get(), *sink.mat_data, N, num_threads, lhs_join_idx,
					    lhs_agg_idx, lhs_val_idx, t_chunks
					);
				}

				clock_gettime(CLOCK_MONOTONIC, &sink.end);
				printf("2 CSR init time: %.3f ms\n", get_ms(sink.start, sink.end));

				// Failures throw (the catch below wakes the waiting threads)
				// instead of returning an empty result without an error.
				const auto conf = ReadSpalmConf(context.client);
				const spalm_fn_t spalm_fn = LoadSpalmLibrary(ResolveSpalmLibrary(context.client, conf));

				std::vector<std::string> kv_keys, kv_vals;
				std::string threads_str = std::to_string(sink.threads);
				kv_keys.push_back("threads");
				kv_vals.push_back(threads_str);
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

				const int status = spalm_fn(
				    spalm_type, sink.Ri.get(), sink.Rk.get(), rv_ptr, sink.Sj.get(), sink.Sk.get(), sv_ptr, sink.M,
				    sink.K, sink.N, false, result_ptr, nullptr, nullptr, nullptr, (int)c_keys.size(), c_keys.data(),
				    c_vals.data()
				);
				if (status != 0) {
					// 1 invalid argument, 2 parameter files, 3 MKL baseline, 4 internal.
					throw InvalidInputException(
					    "spalm: native multiplication failed with status %d (see stderr for details)", status
					);
				}

				sink.done = true;
				clock_gettime(CLOCK_MONOTONIC, &sink.start);
				sink.condition.notify_all();
			} catch (...) {
				// Wake the waiting threads; they rethrow the same error.
				sink.prepare_error = std::current_exception();
				sink.done = true;
				sink.condition.notify_all();
				throw;
			}
		}
	}

	output.SetCardinality(0);
	return OperatorFinalizeResultType::FINISHED;
}

//===--------------------------------------------------------------------===//
// Source Interface
//===--------------------------------------------------------------------===//
unique_ptr<GlobalSourceState> PhysicalSpalm::GetGlobalSourceState(ClientContext &context) const {
	return make_uniq<PhysicalSpalmGlobalSourceState>(*this);
}

unique_ptr<LocalSourceState>
PhysicalSpalm::GetLocalSourceState(ExecutionContext &context, GlobalSourceState &gstate) const {
	return make_uniq<PhysicalSpalmLocalSourceState>();
}

SourceResultType
PhysicalSpalm::GetData(ExecutionContext &context, DataChunk &output, OperatorSourceInput &input) const {
	auto &sink = sink_state->Cast<PhysicalSpalmGlobalState>();
	auto &gstate = input.global_state.Cast<PhysicalSpalmGlobalSourceState>();
	auto &lstate = input.local_state.Cast<PhysicalSpalmLocalSourceState>();

	const idx_t N = (idx_t)sink.N;
	const idx_t total_rows = gstate.total_rows; // M
	const idx_t vec_size = 2048;

	output.SetCapacity(vec_size);
	// Use dynamic output indices: rhs_output_idx for M dimension (ai), lhs_output_idx for N dimension (bj)
	int *out_m_dim = (int *)output.data[rhs_output_idx].GetData(); // ai
	int *out_n_dim = (int *)output.data[lhs_output_idx].GetData(); // bj
	idx_t idx = 0;

	if (sink.use_float) {
		// SUM returns DOUBLE, so the output column is DOUBLE for float inputs too.
		double *out_val = (double *)output.data[2].GetData();
		float *res = sink.result_f.get();

		while (idx < vec_size) {
			if (lstate.curr_pos >= lstate.end_pos) {
				idx_t row_start = gstate.next_row.fetch_add(gstate.chunk_rows);
				if (row_start >= total_rows)
					break;
				idx_t row_end = std::min(row_start + gstate.chunk_rows, total_rows);
				lstate.curr_pos = row_start * N;
				lstate.end_pos = row_end * N;
			}
			while (lstate.curr_pos < lstate.end_pos && idx < vec_size) {
				if (res[lstate.curr_pos] != 0) {
					out_m_dim[idx] = (int)(lstate.curr_pos / N);
					out_n_dim[idx] = (int)(lstate.curr_pos % N);
					out_val[idx] = res[lstate.curr_pos];
					idx++;
				}
				lstate.curr_pos++;
			}
		}
	} else {
		double *out_val = (double *)output.data[2].GetData();
		double *res = sink.result_d.get();

		while (idx < vec_size) {
			if (lstate.curr_pos >= lstate.end_pos) {
				idx_t row_start = gstate.next_row.fetch_add(gstate.chunk_rows);
				if (row_start >= total_rows)
					break;
				idx_t row_end = std::min(row_start + gstate.chunk_rows, total_rows);
				lstate.curr_pos = row_start * N;
				lstate.end_pos = row_end * N;
			}
			while (lstate.curr_pos < lstate.end_pos && idx < vec_size) {
				if (res[lstate.curr_pos] != 0) {
					out_m_dim[idx] = (int)(lstate.curr_pos / N);
					out_n_dim[idx] = (int)(lstate.curr_pos % N);
					out_val[idx] = res[lstate.curr_pos];
					idx++;
				}
				lstate.curr_pos++;
			}
		}
	}

	output.SetCardinality(idx);

	bool all_done = (gstate.next_row.load() >= total_rows) && (lstate.curr_pos >= lstate.end_pos);
	if (all_done) {
		if (++gstate.threads_finished == gstate.num_threads) {
			clock_gettime(CLOCK_MONOTONIC, &sink.end);
			printf("Output time: %.3f ms\n", get_ms(sink.start, sink.end));
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
