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

} // namespace spalm_output
