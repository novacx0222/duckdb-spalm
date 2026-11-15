#pragma once

// Ordering of the column indices within each CSR row built from DuckDB
// chunks. Header-only so tests/csr_sort_test.cpp can exercise it without
// DuckDB.

#include <algorithm>
#include <vector>

namespace spalm_input {

// Sorts the entries [start, end) of one CSR row by column index, carrying
// the values along. A row already in non-decreasing column order (an input
// stored sorted by row and key) is only checked, in one linear pass; any
// other row is sorted as before. Returns whether the row was already in
// order.
template <typename T>
bool SortRowIndices(int start, int end, int *cols, T *vals) {
	int i = start + 1;
	while (i < end && cols[i - 1] <= cols[i]) {
		++i;
	}
	if (i >= end) {
		return true;
	}

	const int n = end - start;
	std::vector<int> p(n);
	for (int k = 0; k < n; k++) {
		p[k] = k;
	}

	std::sort(p.begin(), p.end(), [&](int a, int b) { return cols[start + a] < cols[start + b]; });

	std::vector<int> tmp_cols(n);
	std::vector<T> tmp_vals(n);
	for (int k = 0; k < n; k++) {
		tmp_cols[k] = cols[start + p[k]];
		tmp_vals[k] = vals[start + p[k]];
	}
	for (int k = 0; k < n; k++) {
		cols[start + k] = tmp_cols[k];
		vals[start + k] = tmp_vals[k];
	}
	return false;
}

// SortRowIndices that also adds the row's repeated columns to `duplicates`:
// the number of entries merging would remove. A strictly increasing row is
// checked in the one linear pass; any other row is sorted, then scanned.
template <typename T>
bool SortRowIndices(int start, int end, int *cols, T *vals, long long &duplicates) {
	int i = start + 1;
	while (i < end && cols[i - 1] < cols[i]) {
		++i;
	}
	if (i >= end) {
		return true;
	}

	const bool was_sorted = SortRowIndices<T>(start, end, cols, vals);
	for (int k = start + 1; k < end; k++) {
		duplicates += cols[k - 1] == cols[k];
	}
	return was_sorted;
}

// Sums the entries sharing a column in every sorted row and closes the gaps,
// rewriting the row pointers. SUM(r * s) distributes over the sum, so the
// result stays the one of the join over the repeated entries.
template <typename T>
void MergeRowDuplicates(int *row_ptr, int num_rows, int *cols, T *vals) {
	int out = 0, start = 0;
	for (int r = 0; r < num_rows; r++) {
		const int end = row_ptr[r + 1];
		const int row_begin = out;
		for (int k = start; k < end; k++) {
			if (out > row_begin && cols[out - 1] == cols[k]) {
				vals[out - 1] += vals[k];
			} else {
				cols[out] = cols[k];
				vals[out] = vals[k];
				out++;
			}
		}
		start = end;
		row_ptr[r + 1] = out;
	}
}

} // namespace spalm_input
