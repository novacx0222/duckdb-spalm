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

} // namespace spalm_input
