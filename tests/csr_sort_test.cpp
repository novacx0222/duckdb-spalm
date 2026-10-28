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

int main() {
	CheckRows<float>();
	CheckRows<double>();
	std::cout << "CSR row sort: ordered rows untouched, unordered and duplicate rows sorted, neighbours kept\n";
}
