#pragma once

// Row pointers and per-worker write offsets of a CSR built from DuckDB
// chunks. Header-only so tests/csr_build_test.cpp can exercise it without
// DuckDB.

#include <algorithm>
#include <cstddef>
#include <vector>

namespace spalm_input {

// Below this many count cells (workers x rows), CountsToOffsets runs on the
// calling thread: waking the team would cost more than the pass.
static const size_t kCsrOffsetsParallelMinCells = 1 << 16;

// Contiguous row range `range` of `ranges` over `rows` rows.
inline void CsrRowRange(int rows, int ranges, int range, int &begin, int &end) {
	const int per_range = rows / ranges, extra = rows % ranges;
	begin = per_range * range + std::min(range, extra);
	end = begin + per_range + (range < extra ? 1 : 0);
}

// `counts` holds one array of `rows` counts per logical worker (worker w's
// at counts + w * rows): how many of row r's entries are in w's chunks.
// Afterwards:
// - row_ptr[0 .. rows] are the CSR row pointers (row_ptr needs no
//   initialization);
// - counts[w * rows + r] is the number of row r's entries in workers
//   0 .. w-1, i.e. where worker w writes its first entry of row r, relative
//   to row_ptr[r].
// So within a row, entries go by worker, then in each worker's own order,
// exactly as the serial merge did. Rows are split into contiguous ranges,
// one per thread: each range turns its counts into offsets and row totals,
// the range totals are scanned, then each range adds its start.
inline void CountsToOffsets(int *counts, int workers, int rows, int *row_ptr, int threads) {
	row_ptr[0] = 0;
	if (rows <= 0) {
		return;
	}
	const int ranges = std::max(1, std::min(threads, rows));
	const bool parallel = ranges > 1 && static_cast<size_t>(workers) * rows >= kCsrOffsetsParallelMinCells;
	(void)parallel; // only read by the OpenMP pragmas
	// range_start[g + 1]: entries in range g, scanned below into starts.
	std::vector<int> range_start(static_cast<size_t>(ranges) + 1, 0);
	int *row_total = row_ptr + 1;

#pragma omp parallel for num_threads(ranges) schedule(static, 1) if (parallel)
	for (int g = 0; g < ranges; g++) {
		int begin, end;
		CsrRowRange(rows, ranges, g, begin, end);
		std::fill(row_total + begin, row_total + end, 0);
		for (int w = 0; w < workers; w++) {
			int *worker_counts = counts + static_cast<size_t>(w) * rows;
			for (int r = begin; r < end; r++) {
				const int count = worker_counts[r];
				worker_counts[r] = row_total[r];
				row_total[r] += count;
			}
		}
		int sum = 0;
		for (int r = begin; r < end; r++) {
			sum += row_total[r];
		}
		range_start[g + 1] = sum;
	}

	for (int g = 0; g < ranges; g++) {
		range_start[g + 1] += range_start[g];
	}

#pragma omp parallel for num_threads(ranges) schedule(static, 1) if (parallel)
	for (int g = 0; g < ranges; g++) {
		int begin, end;
		CsrRowRange(rows, ranges, g, begin, end);
		int running = range_start[g];
		for (int r = begin; r < end; r++) {
			running += row_total[r];
			row_total[r] = running;
		}
	}
}

} // namespace spalm_input
