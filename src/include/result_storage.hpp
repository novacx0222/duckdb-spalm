#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <stdexcept>

namespace spalm_output {

template <typename T>
size_t CheckedBytes(size_t count) {
	if (count > std::numeric_limits<size_t>::max() / sizeof(T)) {
		throw std::overflow_error("SPALM result byte count overflows size_t");
	}
	return count * sizeof(T);
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
