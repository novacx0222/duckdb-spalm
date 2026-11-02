// Build: c++ -std=c++11 -O2 -fopenmp -Isrc/include tests/result_pattern_test.cpp
#include "result_pattern.hpp"

#include <cassert>
#include <cmath>
#include <iostream>
#include <limits>
#include <random>
#include <set>
#include <vector>

using spalm_output::CheckValues;
using spalm_output::InputNeedsPattern;
using spalm_output::ResultPattern;
using spalm_output::ValueBounds;
using spalm_output::kValuesNotNegative;
using spalm_output::kValuesNotPositive;
using spalm_output::kValuesTooLarge;

template <typename T>
static int CheckOne(T value) {
	return CheckValues(&value, 1);
}

template <typename T>
static void CheckBounds() {
	const T low = ValueBounds<T>::kLow, high = ValueBounds<T>::kHigh;
	const T infinity = std::numeric_limits<T>::infinity();
	// With 2^62 pairs of the largest values, and the smallest product of a pair
	assert(std::isfinite(T(high * high * std::ldexp(T(1), 62))));
	assert(low * low >= std::numeric_limits<T>::min());

	const int positive = kValuesNotNegative, negative = kValuesNotPositive;
	const int pattern = positive | negative, stored = pattern | kValuesTooLarge;
	assert(CheckOne<T>(1) == positive && CheckOne<T>(11) == positive);
	assert(CheckOne<T>(low) == positive && CheckOne<T>(high) == positive);
	assert(CheckOne<T>(-1) == negative && CheckOne<T>(-low) == negative && CheckOne<T>(-high) == negative);
	assert(CheckOne<T>(0) == pattern && CheckOne<T>(T(-0.0)) == pattern);
	assert(CheckOne<T>(std::nextafter(low, T(0))) == pattern);
	assert(CheckOne<T>(-std::nextafter(low, T(0))) == pattern);
	assert(CheckOne<T>(std::numeric_limits<T>::denorm_min()) == pattern);
	assert(CheckOne<T>(std::nextafter(high, infinity)) == stored);
	assert(CheckOne<T>(-std::nextafter(high, infinity)) == stored);
	assert(CheckOne<T>(std::numeric_limits<T>::max()) == stored);
	assert(CheckOne<T>(infinity) == stored && CheckOne<T>(-infinity) == stored);
	assert(CheckOne<T>(std::numeric_limits<T>::quiet_NaN()) == stored);
	assert(CheckValues<T>(nullptr, 0) == 0);
	// One input can be one sign in a chunk and the other in the next
	assert(!InputNeedsPattern(positive) && !InputNeedsPattern(negative) && !InputNeedsPattern(0));
	assert(InputNeedsPattern(positive | negative) && InputNeedsPattern(pattern) && InputNeedsPattern(stored));

	// A single value decides wherever it stands
	for (size_t size : {size_t(1), size_t(7), size_t(8), size_t(2048)}) {
		for (size_t at = 0; at < size; at += 1 + size / 16) {
			std::vector<T> values(size, T(3));
			assert(CheckValues(values.data(), size) == positive);
			values[at] = 0;
			assert(CheckValues(values.data(), size) == pattern);
			values[at] = size > 1 ? T(-3) : T(0);
			assert(CheckValues(values.data(), size) == pattern);
			values[at] = std::numeric_limits<T>::quiet_NaN();
			assert(CheckValues(values.data(), size) == stored);
			std::vector<T> minus(size, T(-3));
			assert(CheckValues(minus.data(), size) == negative);
			minus[at] = 3;
			assert(CheckValues(minus.data(), size) == (size > 1 ? pattern : positive));
		}
	}
}

template <typename T>
static void CheckRowHasZero() {
	std::vector<T> row(1000, T(2));
	assert(!spalm_output::RowHasZero(row.data(), row.size()));
	row[617] = 0;
	assert(spalm_output::RowHasZero(row.data(), row.size()));
	assert(!spalm_output::RowHasZero(row.data(), size_t(617)));
	row[617] = T(-0.0);
	assert(spalm_output::RowHasZero(row.data(), row.size()));
	row[617] = std::numeric_limits<T>::quiet_NaN();
	assert(!spalm_output::RowHasZero(row.data(), row.size()));
	assert(!spalm_output::RowHasZero(row.data(), size_t(0)));
	// On both sides of a block, and in the last column
	for (size_t at : {size_t(0), size_t(63), size_t(64), size_t(65), size_t(999)}) {
		std::vector<T> column(1000, T(2));
		column[at] = 0;
		assert(spalm_output::RowHasZero(column.data(), column.size()));
		assert(!spalm_output::RowHasZero(column.data(), at));
		assert(spalm_output::RowHasZero(column.data(), at + 1));
	}
}

struct Csr {
	std::vector<int> row_ptr;
	std::vector<int> keys;
};

// Each key reaches a band of columns, which is what a bitset of a few words is for
static Csr BandCsr(std::mt19937 &rng, int rows, int key_count, int width, double density) {
	Csr csr;
	csr.row_ptr.push_back(0);
	std::bernoulli_distribution take(density);
	for (int r = 0; r < rows; ++r) {
		for (int k = std::max(0, r * key_count / rows - width); k < std::min(key_count, r * key_count / rows + width); ++k) {
			if (take(rng)) {
				csr.keys.push_back(k);
			}
		}
		csr.row_ptr.push_back(static_cast<int>(csr.keys.size()));
	}
	return csr;
}

static Csr RandomCsr(std::mt19937 &rng, int rows, int key_count, double density) {
	Csr csr;
	csr.row_ptr.push_back(0);
	std::bernoulli_distribution take(density);
	for (int r = 0; r < rows; ++r) {
		for (int k = 0; k < key_count; ++k) {
			if (take(rng)) {
				csr.keys.push_back(k);
			}
		}
		csr.row_ptr.push_back(static_cast<int>(csr.keys.size()));
	}
	return csr;
}

// Against the definition: column j is set if row and S[j] share a key
static void CheckPattern(const Csr &r, const Csr &s, int columns, int key_count, int threads) {
	ResultPattern pattern(s.row_ptr.data(), s.keys.data(), columns, key_count, threads);
	assert(pattern.Words() == (static_cast<size_t>(columns) + 63) / 64);
	std::vector<uint64_t> bits(pattern.Words() + 1, 0x5555555555555555ull);
	const uint64_t guard = bits.back();
	for (size_t row = 0; row + 1 < r.row_ptr.size(); ++row) {
		const int begin = r.row_ptr[row], end = r.row_ptr[row + 1];
		pattern.FillRow(r.keys.data() + begin, end - begin, bits.data());
		assert(bits.back() == guard);
		const std::set<int> row_keys(r.keys.begin() + begin, r.keys.begin() + end);
		for (int j = 0; j < columns; ++j) {
			bool matches = false;
			for (int q = s.row_ptr[j]; q < s.row_ptr[j + 1] && !matches; ++q) {
				matches = row_keys.count(s.keys[q]) != 0;
			}
			assert(((bits[j >> 6] >> (j & 63)) & 1) == static_cast<uint64_t>(matches));
		}
	}
}

static void CheckPatterns() {
	std::mt19937 rng(7);
	for (int threads : {1, 2, 4, 7}) {
		for (double density : {0.0, 0.002, 0.01, 0.05, 0.2, 0.6, 1.0}) {
			for (int columns : {1, 2, 63, 64, 65, 129, 300}) {
				const int key_count = 1 + static_cast<int>(rng() % 500);
				const Csr r = RandomCsr(rng, 12, key_count, density);
				const Csr s = RandomCsr(rng, columns, key_count, density);
				CheckPattern(r, s, columns, key_count, threads);
			}
		}
	}
	// Bands of all widths, around word boundaries, where a bitset has a part of the words
	for (int threads : {1, 4}) {
		for (int columns : {64, 65, 128, 129, 500, 1000}) {
			for (int width : {1, 2, 10, 70, 200}) {
				for (double density : {0.3, 1.0}) {
					const int key_count = 300;
					const Csr r = BandCsr(rng, 40, key_count, width, density);
					const Csr s = BandCsr(rng, columns, key_count, width, density);
					CheckPattern(r, s, columns, key_count, threads);
				}
			}
		}
	}
	// Keys with two far columns: lists, next to a key that reaches all of them. A row
	// must not stop at a bitset of a part of the words that happens to be full.
	Csr far;
	far.row_ptr.push_back(0);
	for (int j = 0; j < 640; ++j) {
		if (j < 64) {
			far.keys.push_back(0);
		}
		if (j == 0 || j == 639) {
			far.keys.push_back(1);
		}
		if (j % 2 == 0) {
			far.keys.push_back(2);
		}
		far.row_ptr.push_back(static_cast<int>(far.keys.size()));
	}
	Csr rows;
	rows.row_ptr = {0, 1, 2, 3, 5, 8};
	rows.keys = {0, 1, 2, 0, 2, 0, 1, 2};
	CheckPattern(rows, far, 640, 3, 3);

	// Long rows, so that the ranges of keys are in use, with keys only in the first
	// and in the last word
	Csr s;
	s.row_ptr.push_back(0);
	for (int j = 0; j < 100; ++j) {
		for (int k = 0; k < 40; ++k) {
			s.keys.push_back(k * (j % 3 + 1));
		}
		s.keys.push_back(100000 + j);
		s.row_ptr.push_back(static_cast<int>(s.keys.size()));
	}
	Csr r;
	r.row_ptr = {0, 2, 3, 3};
	r.keys = {3, 100050, 39999};
	CheckPattern(r, s, 100, 100101, 4);
	// No entries, no columns, and a key that only R has
	Csr empty;
	empty.row_ptr = {0, 0, 0};
	CheckPattern(r, empty, 2, 100101, 3);
	Csr none;
	none.row_ptr = {0};
	CheckPattern(r, none, 0, 100101, 3);
}

// A key far above the number of entries must not cost memory: the test would take
// 16 GB with an array per key
static void CheckHugeKeys() {
	const int top = std::numeric_limits<int>::max() - 2;
	Csr s;
	s.row_ptr = {0, 2, 3};
	s.keys = {5, top, top};
	Csr r;
	r.row_ptr = {0, 1, 2, 3};
	r.keys = {5, top, 6};
	ResultPattern pattern(s.row_ptr.data(), s.keys.data(), 2, top + 1, 4);
	uint64_t bits;
	pattern.FillRow(&r.keys[0], 1, &bits);
	assert((bits & 3) == 1);
	pattern.FillRow(&r.keys[1], 1, &bits);
	assert((bits & 3) == 3);
	pattern.FillRow(&r.keys[2], 1, &bits);
	assert((bits & 3) == 0);
}

// Both versions, on word counts around the width of a register. The second only runs on
// a CPU with AVX-512.
static void CheckOrWords() {
	std::mt19937_64 rng(13);
	for (size_t words = 0; words < 40; ++words) {
		for (int round = 0; round < 50; ++round) {
			std::vector<uint64_t> pattern(words + 1, 0), source(words + 1, 0);
			for (size_t w = 0; w < words; ++w) {
				pattern[w] = round % 3 ? rng() : ~uint64_t(0);
				source[w] = round % 2 ? ~pattern[w] : rng();
			}
			if (words && round % 5 == 0) {
				source[rng() % words] = 0;
			}
			auto plain = pattern, merged = pattern;
			const uint64_t unset = spalm_output::OrWords(plain.data(), source.data(), words);
			bool all_set = true;
			for (size_t w = 0; w < words; ++w) {
				merged[w] |= source[w];
				all_set = all_set && merged[w] == ~uint64_t(0);
			}
			assert(plain == merged && (unset == 0) == all_set);
#if defined(__x86_64__)
			if (spalm_output::EmitRowNonzerosHasAvx512()) {
				auto wide = pattern;
				const uint64_t wide_unset = spalm_output::OrWordsAvx512(wide.data(), source.data(), words);
				assert(wide == merged && (wide_unset == 0) == all_set);
			}
#endif
		}
	}
}

template <typename T>
static void CheckEmit() {
	const size_t columns = 130;
	std::vector<T> row(columns);
	std::vector<int> ids(columns);
	std::vector<uint64_t> pattern(3, 0);
	std::mt19937 rng(11);
	for (int round = 0; round < 500; ++round) {
		std::set<size_t> expected;
		std::fill(pattern.begin(), pattern.end(), uint64_t(0));
		pattern[2] = ~uint64_t(0) << (columns % 64);
		for (size_t j = 0; j < columns; ++j) {
			row[j] = T(rng() % 4);
			ids[j] = static_cast<int>(1000 + 2 * j);
			if (rng() % (1 + round % 7) == 0) {
				pattern[j >> 6] |= uint64_t(1) << (j & 63);
				expected.insert(j);
			}
		}
		// Continues over chunks of any size, with and without IDs
		const size_t capacity = 1 + rng() % 40;
		const int *col_ids = round % 2 ? ids.data() : nullptr;
		std::vector<int> out_rows(capacity + 1, -1), out_cols(capacity + 1, -1);
		std::vector<double> out_vals(capacity + 1, -1);
		size_t col = 0;
		std::set<size_t> emitted;
		while (col < columns) {
			const size_t idx = spalm_output::EmitRowPattern(row.data(), pattern.data(), col, columns, 9, col_ids,
			                                                out_rows.data(), out_cols.data(), out_vals.data(), 0, capacity);
			assert(idx <= capacity && out_rows[capacity] == -1);
			for (size_t i = 0; i < idx; ++i) {
				const size_t j = col_ids ? static_cast<size_t>(out_cols[i] - 1000) / 2 : static_cast<size_t>(out_cols[i]);
				assert(out_rows[i] == 9 && out_vals[i] == static_cast<double>(row[j]));
				assert(emitted.insert(j).second);
			}
		}
		assert(col == columns && emitted == expected);
	}
}

int main() {
	CheckBounds<double>();
	CheckBounds<float>();
	CheckRowHasZero<double>();
	CheckRowHasZero<float>();
	CheckPatterns();
	CheckHugeKeys();
	CheckOrWords();
	CheckEmit<double>();
	CheckEmit<float>();
	std::cout << "result_pattern_test passed" << std::endl;
	return 0;
}
