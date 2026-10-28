#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <memory>
#include <new>
#include <stdexcept>
#include <vector>

#if defined(__linux__)
#include <sys/mman.h>
#endif
#if defined(__x86_64__)
#include <immintrin.h>
#endif

namespace spalm_output {

template <typename T>
size_t CheckedBytes(size_t count) {
	if (count > std::numeric_limits<size_t>::max() / sizeof(T)) {
		throw std::overflow_error("SPALM result byte count overflows size_t");
	}
	return count * sizeof(T);
}

// A compact CSR row view, not a copy/encoding of keys or values. Removing empty
// rows preserves every entry offset and the order of all existing rows.
class DenseRowLayout {
public:
	void Initialize(const int *row_ptr, int rows, int columns, bool compact_rows) {
		if (rows < 0 || columns < 0 || !row_ptr || row_ptr[0] != 0) {
			throw std::invalid_argument("Invalid SPALM result dimensions or CSR row pointers");
		}
		logical_rows = rows;
		stored_rows = rows;
		cols = columns;
		original_rows.clear();
		compact_ptrs.clear();
		int active_rows = 0;
		for (int row = 0; row < rows; ++row) {
			if (row_ptr[row + 1] < row_ptr[row]) {
				throw std::invalid_argument("Non-monotone SPALM CSR row pointers");
			}
			active_rows += row_ptr[row + 1] != row_ptr[row];
		}
		if (compact_rows && active_rows != rows) {
			stored_rows = active_rows;
			original_rows.reserve(active_rows);
			compact_ptrs.reserve(static_cast<size_t>(active_rows) + 1);
			compact_ptrs.push_back(0);
			for (int row = 0; row < rows; ++row) {
				if (row_ptr[row + 1] == row_ptr[row]) continue;
				original_rows.push_back(row);
				compact_ptrs.push_back(row_ptr[row + 1]);
			}
		}
	}

	int Rows() const { return stored_rows; }
	int LogicalRows() const { return logical_rows; }
	bool IsCompact() const { return stored_rows != logical_rows; }
	int OriginalRow(size_t row) const { return IsCompact() ? original_rows[row] : static_cast<int>(row); }
	// Original ID of each stored row, or nullptr when the layout is not
	// compact (the IDs are the row numbers).
	const int *OriginalRows() const { return IsCompact() ? original_rows.data() : nullptr; }
	int *RowPointers(int *original) { return IsCompact() ? compact_ptrs.data() : original; }
	const int *RowPointers(const int *original) const { return IsCompact() ? compact_ptrs.data() : original; }

	size_t Elements() const {
		const auto rows = static_cast<size_t>(stored_rows);
		const auto columns = static_cast<size_t>(cols);
		if (columns && rows > std::numeric_limits<size_t>::max() / columns) {
			throw std::overflow_error("SPALM result element count overflows size_t");
		}
		return rows * columns;
	}

	template <typename T>
	size_t Bytes() const { return CheckedBytes<T>(Elements()); }

private:
	int logical_rows = 0, stored_rows = 0, cols = 0;
	std::vector<int> original_rows;
	std::vector<int> compact_ptrs;
};

// Owner of a result buffer from AllocateDenseResult: releases the
// allocation the data pointer was placed into, with munmap() for a fresh
// mapping (length > 0) and free() otherwise (which matches both malloc and
// posix_memalign).
struct ResultDeleter {
	ResultDeleter() = default;
	ResultDeleter(void *base_p, size_t length_p) : base(base_p), length(length_p) {}
	void operator()(void *) const {
		if (!base) return;
#if defined(__linux__)
		if (length) {
			munmap(base, length);
			return;
		}
#endif
		std::free(base);
	}
	void *base = nullptr;
	size_t length = 0;
};
template <typename T>
using ResultBuffer = std::unique_ptr<T[], ResultDeleter>;

constexpr size_t kHugePageBytes = size_t(2) << 20;

// Uninitialized storage for `count` values. With huge_pages, a buffer of at
// least 2 MiB is 2 MiB aligned (posix_memalign: the extension builds as
// C++11, without std::aligned_alloc), padded to whole 2 MiB pages and marked
// MADV_HUGEPAGE, so transparent huge pages back it even when the system
// setting is "madvise" (fewer page faults when it is zeroed, a much cheaper
// release, fewer TLB misses in the kernels). `advised` reports whether
// madvise succeeded; otherwise the buffer is ordinary memory.
//
// On huge pages the data starts `base_offset` bytes past the 2 MiB boundary
// (a multiple of sizeof(T), below 4096). The start address matters to the
// kernels: see the caller. Ordinary memory keeps malloc's placement.
//
// With fresh_mapping (Linux), the buffer is instead a new anonymous mmap()
// mapping, which the kernel hands out zero-filled: `zeroed` is then true
// and the caller need not zero it (the kernel clears each page at its
// first touch, by whichever thread writes it first). The data starts
// `base_offset` bytes past a 2 MiB boundary (huge pages, at least 2 MiB)
// or a page boundary. A failed mmap() falls back to the allocation below.
template <typename T>
ResultBuffer<T> AllocateDenseResult(size_t count, bool huge_pages, size_t base_offset, bool &advised, bool &zeroed,
                                    bool fresh_mapping) {
	advised = false;
	zeroed = false;
	if (base_offset % sizeof(T) != 0 || base_offset >= 4096) {
		throw std::invalid_argument("SPALM result base offset must be a multiple of the value size below 4096");
	}
	const auto bytes = CheckedBytes<T>(count);
	if (bytes > std::numeric_limits<size_t>::max() - 2 * kHugePageBytes - base_offset) {
		throw std::overflow_error("SPALM result byte count overflows size_t");
	}
#if defined(__linux__)
	if (fresh_mapping) {
		const size_t align = huge_pages && bytes >= kHugePageBytes ? kHugePageBytes : size_t(4096);
		const size_t span = (std::max(bytes + base_offset, size_t(1)) + align - 1) / align * align;
		const size_t length = span + (align > 4096 ? align : 0);
		void *map = mmap(nullptr, length, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
		if (map != MAP_FAILED) {
			const uintptr_t at = reinterpret_cast<uintptr_t>(map);
			char *start = reinterpret_cast<char *>((at + align - 1) / align * align);
#if defined(MADV_HUGEPAGE)
			if (align == kHugePageBytes) {
				advised = madvise(start, span, MADV_HUGEPAGE) == 0;
			}
#endif
			zeroed = true;
			return ResultBuffer<T>(reinterpret_cast<T *>(start + base_offset), ResultDeleter(map, length));
		}
	}
#else
	(void)fresh_mapping;
#endif
	void *data = nullptr;
	size_t offset = 0;
#if defined(__linux__) && defined(MADV_HUGEPAGE)
	if (huge_pages && bytes >= kHugePageBytes) {
		if (bytes > std::numeric_limits<size_t>::max() - kHugePageBytes - base_offset) {
			throw std::overflow_error("SPALM result byte count overflows size_t");
		}
		const size_t length = (bytes + base_offset + kHugePageBytes - 1) / kHugePageBytes * kHugePageBytes;
		if (posix_memalign(&data, kHugePageBytes, length) == 0) {
			advised = madvise(data, length, MADV_HUGEPAGE) == 0;
			offset = base_offset;
		} else {
			data = nullptr;
		}
	}
#else
	(void)huge_pages;
#endif
	if (!data) {
		data = std::malloc(bytes ? bytes : 1);
	}
	if (!data) {
		throw std::bad_alloc();
	}
	return ResultBuffer<T>(reinterpret_cast<T *>(static_cast<char *>(data) + offset), ResultDeleter(data, 0));
}

// Uninitialized storage, as before fresh mappings.
template <typename T>
ResultBuffer<T> AllocateDenseResult(size_t count, bool huge_pages, size_t base_offset, bool &advised) {
	bool zeroed;
	return AllocateDenseResult<T>(count, huge_pages, base_offset, advised, zeroed, false);
}

// Pre-faults a result from a fresh mapping (AllocateDenseResult with
// zeroed), which already reads as zeros, with the query's threads: each
// thread populates one contiguous range (whole 2 MiB pages on huge pages),
// so the kernel clears the pages here rather than at scattered first writes
// inside the multiplication (outer-v18 measured those 100-200 ms slower than
// a separate pass for SPALM's kernels). MADV_POPULATE_WRITE allocates the
// pages without writing them a second time, as a memset pass does. Returns
// false where it is unsupported or fails; the caller then zeroes the result
// with ZeroDenseResult, which pre-faults it as well.
template <typename T>
bool PrefaultDenseResult(T *data, size_t count, int threads, bool huge_pages) {
	const auto bytes = CheckedBytes<T>(count);
	if (!bytes) return true;
	if (!data || threads < 1) throw std::invalid_argument("Invalid SPALM result initialization");
#if defined(__linux__) && defined(MADV_POPULATE_WRITE)
	const size_t page = 4096;
	const uintptr_t begin = reinterpret_cast<uintptr_t>(data) / page * page;
	const uintptr_t end = (reinterpret_cast<uintptr_t>(data) + bytes + page - 1) / page * page;
	const size_t unit = huge_pages ? kHugePageBytes : page;
	const size_t total = end - begin;
	const size_t share = (total + size_t(threads) - 1) / size_t(threads);
	const size_t chunk = (share + unit - 1) / unit * unit;
	const auto chunks = static_cast<int64_t>((total + chunk - 1) / chunk);
	bool populated = true;
#pragma omp parallel for num_threads(threads) schedule(static, 1) reduction(&& : populated)
	for (int64_t c = 0; c < chunks; ++c) {
		const uintptr_t from = begin + static_cast<uintptr_t>(c) * chunk;
		const size_t length = std::min(chunk, static_cast<size_t>(end - from));
		populated = populated && madvise(reinterpret_cast<void *>(from), length, MADV_POPULATE_WRITE) == 0;
	}
	return populated;
#else
	(void)huge_pages;
	return false;
#endif
}

// The caller allocates uninitialized storage. Touch each byte exactly once,
// using the query's existing thread budget for large outputs. No nested team.
template <typename T>
void ZeroDenseResult(T *data, size_t count, int threads, bool parallel_zero) {
	const auto bytes = CheckedBytes<T>(count);
	if (!bytes) return;
	if (!data || threads < 1) throw std::invalid_argument("Invalid SPALM result initialization");
#ifdef _OPENMP
	if (parallel_zero && threads > 1 && bytes >= 1024 * 1024) {
		const size_t block_size = 256 * 1024 / sizeof(T);
		const auto blocks = static_cast<int64_t>(1 + (count - 1) / block_size);
		#pragma omp parallel for num_threads(threads) schedule(static)
		for (int64_t block = 0; block < blocks; ++block) {
			const auto begin = static_cast<size_t>(block) * block_size;
			const auto length = std::min(block_size, count - begin);
			std::memset(data + begin, 0, length * sizeof(T));
		}
		return;
	}
#else
	(void)parallel_zero;
#endif
	std::memset(data, 0, bytes);
}

// Output of one result row: appends (row ID, column ID, value) for every
// value of row[col, n_cols) that is not 0 (NaN included, -0.0 excluded, as
// with `!= 0`), starting at out[idx], until n_cols or `capacity` tuples.
// col_ids maps physical columns to original IDs (nullptr: identity).
// Returns the new idx; `col` is where the row continues. The caller loops
// over rows, so no tuple needs a division to find its row and column.
template <typename T>
size_t EmitRowNonzerosScalar(const T *row, size_t &col, size_t n_cols, int row_id, const int *col_ids, int *out_rows,
                             int *out_cols, double *out_vals, size_t idx, size_t capacity) {
	size_t j = col;
	for (; j < n_cols && idx < capacity; ++j) {
		if (row[j] != 0) {
			out_rows[idx] = row_id;
			out_cols[idx] = col_ids ? col_ids[j] : static_cast<int>(j);
			out_vals[idx] = static_cast<double>(row[j]);
			++idx;
		}
	}
	col = j;
	return idx;
}

#if defined(__x86_64__)
// The same, 8 values at a time: an AVX-512 compare gives the nonzero lanes
// and compressing stores append their values and column IDs; the scalar
// version finishes the row tail and the last tuples before `capacity`. The
// extension is not built for AVX-512, so these functions enable it alone and
// are called only after a runtime check.
__attribute__((target("avx512f,avx512vl"))) inline size_t
EmitRowNonzerosAvx512(const double *row, size_t &col, size_t n_cols, int row_id, const int *col_ids, int *out_rows,
                      int *out_cols, double *out_vals, size_t idx, size_t capacity) {
	const __m512d zero = _mm512_setzero_pd();
	const __m256i lanes = _mm256_setr_epi32(0, 1, 2, 3, 4, 5, 6, 7);
	const __m256i ids = _mm256_set1_epi32(row_id);
	size_t j = col;
	for (; j + 8 <= n_cols && idx + 8 <= capacity; j += 8) {
		const __m512d values = _mm512_loadu_pd(row + j);
		const __mmask8 nonzero = _mm512_cmp_pd_mask(values, zero, _CMP_NEQ_UQ);
		if (!nonzero) continue;
		const __m256i cols = col_ids ? _mm256_loadu_si256(reinterpret_cast<const __m256i *>(col_ids + j))
		                             : _mm256_add_epi32(_mm256_set1_epi32(static_cast<int>(j)), lanes);
		_mm512_mask_compressstoreu_pd(out_vals + idx, nonzero, values);
		_mm256_mask_compressstoreu_epi32(out_cols + idx, nonzero, cols);
		_mm256_storeu_si256(reinterpret_cast<__m256i *>(out_rows + idx), ids);
		idx += static_cast<size_t>(__builtin_popcount(nonzero));
	}
	col = j;
	return EmitRowNonzerosScalar(row, col, n_cols, row_id, col_ids, out_rows, out_cols, out_vals, idx, capacity);
}

__attribute__((target("avx512f,avx512vl"))) inline size_t
EmitRowNonzerosAvx512(const float *row, size_t &col, size_t n_cols, int row_id, const int *col_ids, int *out_rows,
                      int *out_cols, double *out_vals, size_t idx, size_t capacity) {
	const __m256 zero = _mm256_setzero_ps();
	const __m256i lanes = _mm256_setr_epi32(0, 1, 2, 3, 4, 5, 6, 7);
	const __m256i ids = _mm256_set1_epi32(row_id);
	size_t j = col;
	for (; j + 8 <= n_cols && idx + 8 <= capacity; j += 8) {
		const __m256 values = _mm256_loadu_ps(row + j);
		const __mmask8 nonzero = _mm256_cmp_ps_mask(values, zero, _CMP_NEQ_UQ);
		if (!nonzero) continue;
		const __m256i cols = col_ids ? _mm256_loadu_si256(reinterpret_cast<const __m256i *>(col_ids + j))
		                             : _mm256_add_epi32(_mm256_set1_epi32(static_cast<int>(j)), lanes);
		_mm512_mask_compressstoreu_pd(out_vals + idx, nonzero, _mm512_cvtps_pd(values));
		_mm256_mask_compressstoreu_epi32(out_cols + idx, nonzero, cols);
		_mm256_storeu_si256(reinterpret_cast<__m256i *>(out_rows + idx), ids);
		idx += static_cast<size_t>(__builtin_popcount(nonzero));
	}
	col = j;
	return EmitRowNonzerosScalar(row, col, n_cols, row_id, col_ids, out_rows, out_cols, out_vals, idx, capacity);
}
#endif

// Whether EmitRowNonzeros may use the AVX-512 version on this CPU.
inline bool EmitRowNonzerosHasAvx512() {
#if defined(__x86_64__) && (defined(__GNUC__) || defined(__clang__))
	static const bool supported = __builtin_cpu_supports("avx512f") && __builtin_cpu_supports("avx512vl");
	return supported;
#else
	return false;
#endif
}

template <typename T>
size_t EmitRowNonzeros(const T *row, size_t &col, size_t n_cols, int row_id, const int *col_ids, int *out_rows,
                       int *out_cols, double *out_vals, size_t idx, size_t capacity, bool simd) {
#if defined(__x86_64__)
	if (simd) {
		return EmitRowNonzerosAvx512(row, col, n_cols, row_id, col_ids, out_rows, out_cols, out_vals, idx, capacity);
	}
#else
	(void)simd;
#endif
	return EmitRowNonzerosScalar(row, col, n_cols, row_id, col_ids, out_rows, out_cols, out_vals, idx, capacity);
}

} // namespace spalm_output
