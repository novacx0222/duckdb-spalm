#pragma once

// Sparse result of the native library (spalm_sparse()) for the init_method
// values with sparse output: SPALM SS (sparse_sparse_output) and MKL sp2m
// (FORCE_MKL_SPARSE_SPARSE_OUTPUT). The library returns C = R * S^T as row
// blocks in CSR form instead of filling a dense M x N buffer; the operator
// emits their nonzeros exactly as the dense output would (`value != 0`).

#include <cstddef>
#include <string>

namespace spalm_output {

// Layout of spalm_csr_block in the library's spalm_sparse_result.h (checked
// at load time through spalm_sparse_result_abi()). Block rows are physical
// rows of R, columns physical rows of S; row row_begin + r holds the entries
// [starts[r], ends[r]) of cols / vals (float or double as the call's type).
struct SpalmCsrBlock {
	int row_begin;
	int rows;
	const int *starts;
	const int *ends;
	const int *cols;
	const void *vals;
};
static_assert(sizeof(void *) != 8 || sizeof(SpalmCsrBlock) == 40, "spalm_csr_block layout");
constexpr int kSparseResultAbi = 1;

// The method the library runs for an init_method, and whether the result
// is sparse. Only these two names have sparse output; every other
// init_method (the 6 forced methods, automatic dispatch) keeps the dense
// result and is passed on unchanged.
struct SpalmMethod {
	std::string init_method;
	bool sparse_output;
};

inline SpalmMethod ResolveSpalmMethod(const std::string &requested) {
	if (requested == "sparse_sparse_output") {
		return {"sparse", true};
	}
	if (requested == "FORCE_MKL_SPARSE_SPARSE_OUTPUT") {
		return {"FORCE_MKL_SPARSE", true};
	}
	return {requested, false};
}

// Owns the library's result handle (freed with the library's function).
class SparseResult {
public:
	using FreeFn = void (*)(void *);
	SparseResult() = default;
	SparseResult(const SparseResult &) = delete;
	SparseResult &operator=(const SparseResult &) = delete;
	~SparseResult() {
		Reset();
	}

	void Reset(void *handle = nullptr, FreeFn free_fn = nullptr) {
		if (handle_ && free_) {
			free_(handle_);
		}
		handle_ = handle;
		free_ = free_fn;
		blocks_ = nullptr;
		count_ = 0;
	}
	void SetBlocks(const SpalmCsrBlock *blocks, size_t count) {
		blocks_ = count ? blocks : nullptr;
		count_ = blocks_ ? count : 0;
	}
	bool Active() const {
		return handle_ != nullptr;
	}
	const SpalmCsrBlock *Blocks() const {
		return blocks_;
	}
	size_t Count() const {
		return count_;
	}

private:
	void *handle_ = nullptr;
	FreeFn free_ = nullptr;
	const SpalmCsrBlock *blocks_ = nullptr;
	size_t count_ = 0;
};

// Appends (row ID, column ID, value) for every entry of block `b` that is
// not 0 (NaN included, -0.0 and MKL's cancelled products excluded, as
// EmitRowNonzeros), from block row `row`, entry `pos` on, until the block
// ends or `capacity` tuples. row_ids / col_ids map physical rows / columns
// to original IDs (nullptr: identity). Returns the new idx; row and pos are
// where the block continues (row == b.rows: done). Start a block with
// row = 0, pos = b.starts[0].
template <typename T>
size_t EmitBlockNonzeros(const SpalmCsrBlock &b, int &row, int &pos, const int *row_ids, const int *col_ids,
                         int *out_rows, int *out_cols, double *out_vals, size_t idx, size_t capacity) {
	const T *vals = static_cast<const T *>(b.vals);
	while (row < b.rows && idx < capacity) {
		const int physical_row = b.row_begin + row;
		const int row_id = row_ids ? row_ids[physical_row] : physical_row;
		const int end = b.ends[row];
		for (; pos < end && idx < capacity; ++pos) {
			const T value = vals[pos];
			if (value != 0) {
				const int col = b.cols[pos];
				out_rows[idx] = row_id;
				out_cols[idx] = col_ids ? col_ids[col] : col;
				out_vals[idx] = static_cast<double>(value);
				++idx;
			}
		}
		if (pos == end) {
			++row;
			if (row < b.rows) {
				pos = b.starts[row];
			}
		}
	}
	return idx;
}

} // namespace spalm_output
