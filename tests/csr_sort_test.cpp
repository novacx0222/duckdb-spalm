// Build: c++ -std=c++11 -O2 -Isrc/include tests/csr_sort_test.cpp
#include "csr_sort.hpp"

#include <cassert>
#include <iostream>
#include <map>
#include <random>
#include <vector>

// Rows sorted in place against a reference: the column indices end up in
// non-decreasing order and every (column, value) pair is kept, for rows
// already in order (left untouched), reversed, shuffled, with duplicate
// columns, empty and single-entry rows, float and double.
template <typename T>
static void CheckRows() {
	std::mt19937 rng(11);
	for (int n : {0, 1, 2, 3, 17, 1000}) {
		for (int kind = 0; kind < 5; ++kind) {
			std::vector<int> cols(n);
			std::vector<T> vals(n);
			for (int i = 0; i < n; ++i) {
				cols[i] = kind == 3 ? int(rng() % 5) : i * 3;
				vals[i] = T(i + 1);
			}
			if (kind == 1) std::reverse(cols.begin(), cols.end());
			if (kind == 2 || kind == 3) std::shuffle(cols.begin(), cols.end(), rng);
			if (kind == 4 && n > 1) std::swap(cols[n - 2], cols[n - 1]); // one pair out of order at the end
			std::multimap<int, T> before;
			for (int i = 0; i < n; ++i) before.insert({cols[i], vals[i]});
			const std::vector<int> cols_in = cols;
			const std::vector<T> vals_in = vals;
			bool ordered = true;
			for (int i = 1; i < n; ++i) ordered = ordered && cols[i - 1] <= cols[i];

			// The row sits between other entries, which must not move.
			std::vector<int> all_cols(n + 4, -7);
			std::vector<T> all_vals(n + 4, T(-7));
			std::copy(cols.begin(), cols.end(), all_cols.begin() + 2);
			std::copy(vals.begin(), vals.end(), all_vals.begin() + 2);
			const bool was_sorted = spalm_input::SortRowIndices<T>(2, 2 + n, all_cols.data(), all_vals.data());

			assert(was_sorted == ordered);
			assert(all_cols[0] == -7 && all_cols[1] == -7 && all_cols[n + 2] == -7 && all_cols[n + 3] == -7);
			assert(all_vals[0] == T(-7) && all_vals[n + 3] == T(-7));
			for (int i = 1; i < n; ++i) assert(all_cols[2 + i - 1] <= all_cols[2 + i]);
			std::multimap<int, T> after;
			for (int i = 0; i < n; ++i) after.insert({all_cols[2 + i], all_vals[2 + i]});
			// Same multiset of (column, value) pairs (values of equal
			// columns compared as sorted lists).
			for (auto it = before.begin(); it != before.end();) {
				const int c = it->first;
				std::vector<T> a, b;
				for (auto r = before.equal_range(c); r.first != r.second; ++r.first) a.push_back(r.first->second);
				for (auto r = after.equal_range(c); r.first != r.second; ++r.first) b.push_back(r.first->second);
				std::sort(a.begin(), a.end());
				std::sort(b.begin(), b.end());
				assert(a == b);
				it = before.upper_bound(c);
			}
			assert(after.size() == before.size());
			if (ordered) {
				assert(std::equal(cols_in.begin(), cols_in.end(), all_cols.begin() + 2));
				assert(std::equal(vals_in.begin(), vals_in.end(), all_vals.begin() + 2));
			}
		}
	}
}

// Whole matrices through the counting sort and the merge, against a reference
// of (row, column) -> sum: rows with and without repeated columns, empty rows
// at both ends and between others. Values are small integers, so sums are
// exact.
template <typename T>
static void CheckMerge() {
	std::mt19937 rng(23);
	for (int num_rows : {1, 2, 7, 40}) {
		for (int width : {1, 3, 50, 1000}) {
			std::vector<int> row_ptr(1, 0), cols;
			std::vector<T> vals;
			std::map<std::pair<int, int>, T> expected;
			for (int r = 0; r < num_rows; ++r) {
				const int entries = (r == 0 || r == num_rows - 1 || r % 5 == 2) ? 0 : int(rng() % 30);
				for (int e = 0; e < entries; ++e) {
					const int col = int(rng() % width);
					const T val = T(int(rng() % 9) - 4);
					cols.push_back(col);
					vals.push_back(val);
					expected[{r, col}] += val;
				}
				row_ptr.push_back(int(cols.size()));
			}

			long long duplicates = 0;
			for (int r = 0; r < num_rows; ++r) {
				spalm_input::SortRowIndices<T>(row_ptr[r], row_ptr[r + 1], cols.data(), vals.data(), duplicates);
			}
			assert(duplicates == (long long)cols.size() - (long long)expected.size());
			spalm_input::MergeRowDuplicates<T>(row_ptr.data(), num_rows, cols.data(), vals.data());

			assert(row_ptr[0] == 0 && row_ptr[num_rows] == int(expected.size()));
			for (int r = 0; r < num_rows; ++r) {
				for (int k = row_ptr[r]; k < row_ptr[r + 1]; ++k) {
					assert(k == row_ptr[r] || cols[k - 1] < cols[k]); // strictly increasing
					assert(vals[k] == expected.at({r, cols[k]}));
				}
			}

			// Nothing repeated any more: counted as zero.
			long long left = 0;
			for (int r = 0; r < num_rows; ++r) {
				const bool was_sorted =
				    spalm_input::SortRowIndices<T>(row_ptr[r], row_ptr[r + 1], cols.data(), vals.data(), left);
				assert(was_sorted);
			}
			assert(left == 0);
		}
	}
}

// A strictly increasing input is left alone: no duplicates, same arrays.
template <typename T>
static void CheckNoDuplicates() {
	std::vector<int> row_ptr = {0, 3, 3, 5};
	std::vector<int> cols = {1, 4, 9, 0, 2};
	std::vector<T> vals = {1, 2, 3, 4, 5};
	const std::vector<int> cols_in = cols;
	const std::vector<T> vals_in = vals;
	long long duplicates = 0;
	for (int r = 0; r < 3; ++r) {
		const bool was_sorted =
		    spalm_input::SortRowIndices<T>(row_ptr[r], row_ptr[r + 1], cols.data(), vals.data(), duplicates);
		assert(was_sorted);
	}
	assert(duplicates == 0 && cols == cols_in && vals == vals_in);
}

int main() {
	CheckRows<float>();
	CheckRows<double>();
	CheckMerge<float>();
	CheckMerge<double>();
	CheckNoDuplicates<float>();
	CheckNoDuplicates<double>();
	std::cout << "CSR row sort: ordered rows untouched, unordered and duplicate rows sorted, neighbours kept; "
	             "repeated columns counted and merged\n";
}
