// Build: c++ -std=c++11 -O2 -fopenmp -Isrc/include tests/result_pattern_test.cpp
#include "result_pattern.hpp"

#include <cassert>
#include <cmath>
#include <iostream>
#include <iterator>
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

// Columns j of S with j % 3 + 1 keys, and column 5 without one
static std::vector<int> KeyCounts(size_t columns) {
	std::vector<int> row_ptr(1, 0);
	for (size_t j = 0; j < columns; ++j) {
		row_ptr.push_back(row_ptr.back() + (j == 5 ? 0 : static_cast<int>(j % 3) + 1));
	}
	return row_ptr;
}

template <typename T>
static void CheckCollectZeros() {
	const auto s_row_ptr = KeyCounts(1000);
	const size_t no_limit = std::numeric_limits<size_t>::max();
	std::vector<int> zeros(3, 7);
	std::vector<T> row(1000, T(2));
	assert(spalm_output::CollectZeros(row.data(), row.size(), s_row_ptr.data(), 0, zeros) && zeros.empty());
	row[617] = 0;
	assert(spalm_output::CollectZeros(row.data(), row.size(), s_row_ptr.data(), no_limit, zeros));
	assert(zeros == std::vector<int>({617}));
	assert(spalm_output::CollectZeros(row.data(), size_t(617), s_row_ptr.data(), 0, zeros) && zeros.empty());
	row[617] = T(-0.0);
	assert(spalm_output::CollectZeros(row.data(), row.size(), s_row_ptr.data(), no_limit, zeros) && zeros.size() == 1);
	row[617] = std::numeric_limits<T>::quiet_NaN();
	assert(spalm_output::CollectZeros(row.data(), row.size(), s_row_ptr.data(), 0, zeros) && zeros.empty());
	assert(spalm_output::CollectZeros(row.data(), size_t(0), s_row_ptr.data(), 0, zeros) && zeros.empty());
	// On both sides of a block, and in the last column
	for (size_t at : {size_t(0), size_t(63), size_t(64), size_t(65), size_t(999)}) {
		std::vector<T> column(1000, T(2));
		column[at] = 0;
		assert(spalm_output::CollectZeros(column.data(), column.size(), s_row_ptr.data(), no_limit, zeros));
		assert(zeros == std::vector<int>({static_cast<int>(at)}));
		assert(spalm_output::CollectZeros(column.data(), at, s_row_ptr.data(), 0, zeros) && zeros.empty());
	}
	// A column without keys has no match and costs nothing
	std::vector<T> keyless(1000, T(2));
	keyless[5] = 0;
	assert(spalm_output::CollectZeros(keyless.data(), keyless.size(), s_row_ptr.data(), 0, zeros) && zeros.empty());
	// The budget counts the keys of the columns of the zeros: 1 + 2 + 3 + 1 for 0 to 3
	std::vector<T> zero_row(1000, T(0));
	assert(spalm_output::CollectZeros(zero_row.data(), size_t(4), s_row_ptr.data(), 7, zeros));
	assert(zeros == std::vector<int>({0, 1, 2, 3}));
	assert(!spalm_output::CollectZeros(zero_row.data(), size_t(4), s_row_ptr.data(), 6, zeros));
	assert(!spalm_output::CollectZeros(zero_row.data(), zero_row.size(), s_row_ptr.data(), 1000, zeros));
	size_t all = 0;
	for (size_t j = 0; j < 1000; ++j) {
		all += static_cast<size_t>(s_row_ptr[j + 1] - s_row_ptr[j]);
	}
	assert(spalm_output::CollectZeros(zero_row.data(), zero_row.size(), s_row_ptr.data(), all, zeros));
	assert(zeros.size() == 999);
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

// Whether a row of R and S[j] share a key, by the definition
static bool Matches(const Csr &r, size_t row, const Csr &s, int j) {
	const std::set<int> row_keys(r.keys.begin() + r.row_ptr[row], r.keys.begin() + r.row_ptr[row + 1]);
	for (int q = s.row_ptr[j]; q < s.row_ptr[j + 1]; ++q) {
		if (row_keys.count(s.keys[q])) {
			return true;
		}
	}
	return false;
}

// The lookup of RowKeys keeps the columns that match, in their order. One object for
// all rows, so that a stale bit of one row would show in the next.
static void CheckLookup(const Csr &r, const Csr &s, int columns) {
	spalm_output::RowKeys lookup;
	for (size_t row = 0; row + 1 < r.row_ptr.size(); ++row) {
		const int begin = r.row_ptr[row], end = r.row_ptr[row + 1];
		if (begin == end) {
			continue; // RowKeys needs a key, and such a row matches nothing
		}
		std::vector<int> all_columns, matched;
		for (int j = 0; j < columns; ++j) {
			all_columns.push_back(j);
			if (Matches(r, row, s, j)) {
				matched.push_back(j);
			}
		}
		lookup.Set(r.keys.data() + begin, end - begin);
		lookup.KeepMatched(s.row_ptr.data(), s.keys.data(), all_columns);
		lookup.Clear();
		assert(all_columns == matched);
	}
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
		for (int j = 0; j < columns; ++j) {
			assert(((bits[j >> 6] >> (j & 63)) & 1) == static_cast<uint64_t>(Matches(r, row, s, j)));
		}
	}
	CheckLookup(r, s, columns);
}

// Rows of R whose keys span far more than 32 bytes a key take the binary search, next
// to rows with a bitset, and keys up to 2^31 - 3
static void CheckRowKeys() {
	std::mt19937 rng(11);
	const int top = std::numeric_limits<int>::max() - 2;
	for (int round = 0; round < 50; ++round) {
		std::uniform_int_distribution<int> any(0, round % 2 ? top : 5000);
		Csr s, r;
		s.row_ptr.push_back(0);
		r.row_ptr.push_back(0);
		std::vector<int> pool;
		for (int k = 0; k < 40; ++k) {
			pool.push_back(any(rng));
		}
		pool.push_back(0);
		pool.push_back(top);
		auto fill = [&](Csr &csr, int rows, int most) {
			for (int row = 0; row < rows; ++row) {
				std::set<int> keys;
				const int count = static_cast<int>(rng() % (most + 1));
				for (int p = 0; p < count; ++p) {
					keys.insert(rng() % 4 ? pool[rng() % pool.size()] : any(rng));
				}
				csr.keys.insert(csr.keys.end(), keys.begin(), keys.end());
				csr.row_ptr.push_back(static_cast<int>(csr.keys.size()));
			}
		};
		fill(s, 70, 6);
		fill(r, 30, round % 3 ? 4 : 2000);
		CheckLookup(r, s, 70);
	}
	// A bitset of exactly 1024 words, and one word more, which takes the binary search
	for (int last : {1024 * 64 - 1, 1024 * 64}) {
		Csr r, s;
		r.row_ptr = {0, 2, 3};
		r.keys = {0, last, 1};
		s.row_ptr = {0, 1, 2, 3, 5};
		s.keys = {last, 1, last - 1, 0, 5};
		CheckLookup(r, s, 4);
	}
}

// The zeros that matched, from where the last chunk stopped
template <typename T>
static void CheckEmitColumns() {
	std::vector<T> row = {T(0), T(5), T(-0.0), T(0)};
	const std::vector<int> columns = {0, 2, 3};
	const std::vector<int> ids = {10, 11, 12, 13};
	for (const int *col_ids : {static_cast<const int *>(nullptr), ids.data()}) {
		std::vector<int> out_rows(4, -1), out_cols(4, -1);
		std::vector<double> out_vals(4, -1);
		size_t next = 0;
		size_t idx = spalm_output::EmitColumns(row.data(), columns, next, 7, col_ids, out_rows.data(), out_cols.data(),
		                                       out_vals.data(), 1, 3);
		assert(idx == 3 && next == 2 && out_rows[0] == -1 && out_rows[3] == -1);
		idx = spalm_output::EmitColumns(row.data(), columns, next, 7, col_ids, out_rows.data(), out_cols.data(),
		                                out_vals.data(), 3, 4);
		assert(idx == 4 && next == 3);
		for (size_t i = 1; i < 4; ++i) {
			const int j = columns[i - 1];
			assert(out_rows[i] == 7 && out_cols[i] == (col_ids ? 10 + j : j) && out_vals[i] == 0);
		}
		assert(std::signbit(out_vals[2]));
		idx = spalm_output::EmitColumns(row.data(), columns, next, 7, col_ids, out_rows.data(), out_cols.data(),
		                                out_vals.data(), 4, 4);
		assert(idx == 4 && next == 3);
	}
}

// A row of the result with zeros, by both ways: its nonzeros and the zeros that the
// lookup kept, or the columns of its pattern. Products of -1, 0 and 1 cancel out.
static void CheckBothWays() {
	std::mt19937 rng(5);
	for (double density : {0.02, 0.1, 0.4}) {
		const int rows = 40, columns = 150, key_count = 90;
		const Csr r = RandomCsr(rng, rows, key_count, density);
		const Csr s = RandomCsr(rng, columns, key_count, density);
		std::uniform_int_distribution<int> value(-1, 1);
		std::vector<double> r_vals(r.keys.size()), s_vals(s.keys.size());
		for (auto &v : r_vals) {
			v = value(rng);
		}
		for (auto &v : s_vals) {
			v = value(rng);
		}
		ResultPattern pattern(s.row_ptr.data(), s.keys.data(), columns, key_count, 3);
		spalm_output::RowKeys lookup;
		std::vector<uint64_t> bits(pattern.Words());
		size_t probed = 0;
		for (int i = 0; i < rows; ++i) {
			std::vector<double> dense(columns, 0), by_key(key_count, 0);
			for (int p = r.row_ptr[i]; p < r.row_ptr[i + 1]; ++p) {
				by_key[r.keys[p]] = r_vals[p];
			}
			for (int j = 0; j < columns; ++j) {
				for (int q = s.row_ptr[j]; q < s.row_ptr[j + 1]; ++q) {
					dense[j] += by_key[s.keys[q]] * s_vals[q];
				}
			}
			const int begin = r.row_ptr[i], count = r.row_ptr[i + 1] - begin;
			pattern.FillRow(r.keys.data() + begin, count, bits.data());
			std::set<int> expected;
			for (int j = 0; j < columns; ++j) {
				if ((bits[j >> 6] >> (j & 63)) & 1) {
					expected.insert(j);
				}
			}
			std::vector<int> zeros;
			if (count == 0 || !spalm_output::CollectZeros(dense.data(), dense.size(), s.row_ptr.data(),
			                                              std::numeric_limits<size_t>::max(), zeros)) {
				assert(count == 0 && expected.empty());
				continue;
			}
			lookup.Set(r.keys.data() + begin, count);
			lookup.KeepMatched(s.row_ptr.data(), s.keys.data(), zeros);
			lookup.Clear();
			probed += !zeros.empty();
			std::set<int> found(zeros.begin(), zeros.end());
			for (int j = 0; j < columns; ++j) {
				if (dense[j] != 0) {
					assert(found.insert(j).second);
				}
			}
			assert(found == expected);
		}
		assert(probed > 0);
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

// Both versions where the CPU has AVX-512, on rows with full words, mixed bits and empty
// words, from any column on and over chunks of any size, with and without IDs
template <typename T>
static void CheckEmit() {
	std::vector<bool> versions = {false};
	if (spalm_output::EmitRowNonzerosHasAvx512()) {
		versions.push_back(true);
	}
	std::mt19937 rng(11);
	for (const bool simd : versions) {
		for (const size_t columns : {size_t(5), size_t(130), size_t(300)}) {
			std::vector<T> row(columns);
			std::vector<int> ids(columns);
			std::vector<uint64_t> pattern((columns + 63) / 64, 0);
			for (int round = 0; round < 500; ++round) {
				std::set<size_t> expected;
				std::fill(pattern.begin(), pattern.end(), uint64_t(0));
				if (columns % 64) {
					pattern.back() = ~uint64_t(0) << (columns % 64); // as FillRow leaves them
				}
				for (size_t j = 0; j < columns; ++j) {
					row[j] = T(rng() % 4);
					ids[j] = static_cast<int>(1000 + 2 * j);
					const bool empty_word = round % 5 == 1 && (j >> 6) == 1;
					if (!empty_word && rng() % (1 + round % 7) == 0) {
						pattern[j >> 6] |= uint64_t(1) << (j & 63);
						expected.insert(j);
					}
				}
				const size_t capacity = 1 + rng() % (round % 3 ? 40 : 400);
				const int *col_ids = round % 2 ? ids.data() : nullptr;
				std::vector<int> out_rows(capacity + 1, -1), out_cols(capacity + 1, -1);
				std::vector<double> out_vals(capacity + 1, -1);
				size_t col = rng() % 4 == 0 ? rng() % columns : 0;
				for (auto it = expected.begin(); it != expected.end();) {
					it = *it < col ? expected.erase(it) : std::next(it);
				}
				std::set<size_t> emitted;
				while (col < columns) {
					const size_t idx = spalm_output::EmitRowPattern(row.data(), pattern.data(), col, columns, 9, col_ids,
					                                                out_rows.data(), out_cols.data(), out_vals.data(), 0,
					                                                capacity, simd);
					assert(idx <= capacity && out_rows[capacity] == -1);
					for (size_t i = 0; i < idx; ++i) {
						const size_t j =
						    col_ids ? static_cast<size_t>(out_cols[i] - 1000) / 2 : static_cast<size_t>(out_cols[i]);
						assert(out_rows[i] == 9 && out_vals[i] == static_cast<double>(row[j]));
						assert(emitted.insert(j).second);
					}
				}
				assert(col == columns && emitted == expected);
			}
		}
	}
}

int main() {
	CheckBounds<double>();
	CheckBounds<float>();
	CheckCollectZeros<double>();
	CheckCollectZeros<float>();
	CheckPatterns();
	CheckRowKeys();
	CheckBothWays();
	CheckHugeKeys();
	CheckOrWords();
	CheckEmit<double>();
	CheckEmit<float>();
	CheckEmitColumns<double>();
	CheckEmitColumns<float>();
	std::cout << "result_pattern_test passed" << std::endl;
	return 0;
}
