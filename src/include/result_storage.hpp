#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <stdexcept>
#include <vector>

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
