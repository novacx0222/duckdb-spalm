// Build: c++ -std=c++11 -O2 -Isrc/include tests/csr_build_test.cpp
// With threads: add -fopenmp (g++), e.g. g++ -std=c++11 -O2 -fopenmp -Isrc/include tests/csr_build_test.cpp
#include "csr_build.hpp"

#include <cassert>
#include <cstdio>
#include <random>
#include <vector>

#ifdef _OPENMP
#include <omp.h>
#endif

using spalm_input::CountsToOffsets;
using spalm_input::CsrRowRange;

// The serial merge BuildCSR used up to outer-m3: per-worker histograms
// shifted by one row, summed into the row pointers, scanned, then each
// worker's offset within a row as the sum over the workers before it.
static void ReferenceOffsets(
    const std::vector<int> &counts, int workers, int rows, std::vector<int> &row_ptr, std::vector<int> &offsets
) {
	std::vector<std::vector<int>> local_hist(workers, std::vector<int>(rows + 1, 0));
	for (int w = 0; w < workers; w++) {
		for (int r = 0; r < rows; r++) {
			local_hist[w][r + 1] = counts[static_cast<size_t>(w) * rows + r];
		}
	}
	row_ptr.assign(rows + 1, 0);
	for (int t = 0; t < workers; t++) {
		for (int r = 0; r <= rows; r++) {
			row_ptr[r] += local_hist[t][r];
		}
	}
	for (int i = 0; i < rows; i++) {
		row_ptr[i + 1] += row_ptr[i];
	}
	offsets.assign(static_cast<size_t>(workers) * rows, 0);
	for (int r = 0; r < rows; r++) {
		int current_offset = 0;
		for (int t = 0; t < workers; t++) {
			offsets[static_cast<size_t>(t) * rows + r] = current_offset;
			current_offset += local_hist[t][r + 1];
		}
	}
}

// The row ranges cover [0, rows) once, in order, and differ by at most one row.
static void CheckRanges() {
	for (int rows : {1, 2, 7, 30, 31, 1000}) {
		for (int ranges : {1, 2, 3, 30, 64}) {
			if (ranges > rows) continue;
			int expected_begin = 0, smallest = rows, largest = 0;
			for (int g = 0; g < ranges; g++) {
				int begin, end;
				CsrRowRange(rows, ranges, g, begin, end);
				assert(begin == expected_begin && end >= begin);
				smallest = std::min(smallest, end - begin);
				largest = std::max(largest, end - begin);
				expected_begin = end;
			}
			assert(expected_begin == rows && largest - smallest <= 1);
		}
	}
}

// CountsToOffsets against the serial merge: random counts, empty rows and
// workers, one dense row, sizes below and above the parallel threshold,
// more threads than rows.
static int CheckOffsets() {
	std::mt19937 rng(7);
	int cases = 0;
	for (int workers : {1, 2, 3, 30, 64}) {
		for (int rows : {0, 1, 2, 5, 100, 1000, 3000, 70000}) {
			for (int threads : {1, 4, 30, 200}) {
				for (int kind = 0; kind < 4; kind++) {
					const size_t cells = static_cast<size_t>(workers) * rows;
					std::vector<int> counts(cells, 0);
					for (size_t i = 0; i < cells; i++) {
						if (kind == 0) counts[i] = static_cast<int>(rng() % 4);       // spread
						if (kind == 1) counts[i] = rng() % 10 == 0 ? int(rng() % 50) : 0; // mostly empty
						if (kind == 3) counts[i] = static_cast<int>(rng() % 2);
					}
					if (kind == 2 && rows > 0) { // everything in one row, some workers empty
						const int row = static_cast<int>(rng() % rows);
						for (int w = 0; w < workers; w += 2) counts[static_cast<size_t>(w) * rows + row] = 1000;
					}
					if (kind == 3 && workers > 1) { // last worker has no entries
						for (int r = 0; r < rows; r++) counts[static_cast<size_t>(workers - 1) * rows + r] = 0;
					}
					std::vector<int> ref_ptr, ref_offsets;
					ReferenceOffsets(counts, workers, rows, ref_ptr, ref_offsets);
					std::vector<int> row_ptr(rows + 1, -12345); // no initialization needed
					CountsToOffsets(counts.data(), workers, rows, row_ptr.data(), threads);
					assert(row_ptr == ref_ptr);
					assert(counts == ref_offsets);
					cases++;
				}
			}
		}
	}
	return cases;
}

struct Entry {
	int row, key;
	double value;
};

// The whole CSR as BuildCSR fills it, old merge vs new, from the same
// per-worker entry lists (a worker's chunks in order): row pointers, keys
// and values must be identical, position for position.
static int CheckBuild() {
	std::mt19937 rng(23);
	int cases = 0;
	for (int workers : {1, 3, 30}) {
		for (int rows : {1, 17, 5000}) {
			for (int per_worker : {0, 1, 50, 3000}) {
				std::vector<std::vector<Entry>> input(workers);
				for (int w = 0; w < workers; w++) {
					const int n = per_worker == 0 ? 0 : static_cast<int>(rng() % (2 * per_worker + 1));
					for (int i = 0; i < n; i++) {
						// Skewed rows: half of the entries in the first tenth of the rows.
						const int row = rng() % 2 ? static_cast<int>(rng() % rows)
						                          : static_cast<int>(rng() % std::max(1, rows / 10));
						input[w].push_back({row, static_cast<int>(rng() % 1000), double(rng() % 100000)});
					}
				}
				int nnz = 0;
				std::vector<int> counts(static_cast<size_t>(workers) * rows, 0);
				for (int w = 0; w < workers; w++) {
					nnz += static_cast<int>(input[w].size());
					for (const auto &e : input[w]) counts[static_cast<size_t>(w) * rows + e.row]++;
				}

				std::vector<int> ref_ptr, ref_offsets;
				ReferenceOffsets(counts, workers, rows, ref_ptr, ref_offsets);
				std::vector<int> ref_keys(nnz), keys(nnz);
				std::vector<double> ref_values(nnz), values(nnz);
				for (int w = 0; w < workers; w++) {
					for (const auto &e : input[w]) {
						const int pos = ref_ptr[e.row] + ref_offsets[static_cast<size_t>(w) * rows + e.row]++;
						ref_keys[pos] = e.key;
						ref_values[pos] = e.value;
					}
				}

				std::vector<int> row_ptr(rows + 1);
				CountsToOffsets(counts.data(), workers, rows, row_ptr.data(), workers);
#pragma omp parallel for num_threads(workers) schedule(static, 1)
				for (int w = 0; w < workers; w++) {
					int *offsets = counts.data() + static_cast<size_t>(w) * rows;
					for (const auto &e : input[w]) {
						const int pos = row_ptr[e.row] + offsets[e.row]++;
						keys[pos] = e.key;
						values[pos] = e.value;
					}
				}
				assert(row_ptr == ref_ptr);
				assert(keys == ref_keys);
				assert(values == ref_values);
				assert(row_ptr[rows] == nnz);
				cases++;
			}
		}
	}
	return cases;
}

int main() {
	CheckRanges();
	const int offsets = CheckOffsets();
	const int builds = CheckBuild();
#ifdef _OPENMP
	std::printf("csr_build_test: OK (%d offset cases, %d builds, OpenMP max threads %d)\n", offsets, builds,
	            omp_get_max_threads());
#else
	std::printf("csr_build_test: OK (%d offset cases, %d builds, no OpenMP)\n", offsets, builds);
#endif
	return 0;
}
