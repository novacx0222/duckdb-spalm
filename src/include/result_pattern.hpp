#pragma once

// Which pairs of a product matched. The result is dense, so a pair whose sum is zero
// looks like a pair without a match, and DuckDB has a row for the first but not for the
// second. Most inputs need none of this: CheckValues tells from one pass over the values.
// Header-only so tests/result_pattern_test.cpp can exercise it without DuckDB.

#include "result_storage.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace spalm_output {

// Inputs of 2^31 entries give at most 2^62 pairs, merged or not. If all values of both
// inputs lie in [kLow, kHigh], then kLow^2 is above the smallest normal number and
// 2^62 * kHigh^2 below the largest one: products neither underflow nor overflow, and
// the sum of a pair that matched is above zero.
template <typename T>
struct ValueBounds;
template <>
struct ValueBounds<double> {
	static constexpr double kLow = 1e-150, kHigh = 1e140;
	using Mask = int64_t; // of the size of the value, which is what vectorizes
};
template <>
struct ValueBounds<float> {
	static constexpr float kLow = 2e-19f, kHigh = 1e9f;
	using Mask = int32_t;
};

// The result of CheckValues, to combine with |. No flag: a zero in the result means that
// no pair matched, as in the experiments.
enum ValueFlags : int {
	// A value is outside the bounds: a pair that matched can sum to zero, or underflow.
	kValuesNeedPattern = 1,
	// NaN, Inf or above kHigh, which is also what can overflow. A dense kernel reads a
	// missing entry as zero, and 0 * Inf would give NaN to pairs that have no such entry.
	kValuesNeedStoredEntries = 2
};

// Without a branch, to stay cheap on the inputs that need nothing: they pay for the first
// loop only. A NaN fails both comparisons of a value.
template <typename T>
int CheckValues(const T *values, size_t count) {
	using Mask = typename ValueBounds<T>::Mask;
	const T low = ValueBounds<T>::kLow, high = ValueBounds<T>::kHigh;
	Mask out_of_bounds = 0;
	for (size_t i = 0; i < count; ++i) {
		const T value = values[i];
		out_of_bounds |= !(value >= low) | !(value <= high);
	}
	if (!out_of_bounds) {
		return 0;
	}
	Mask too_large = 0;
	for (size_t i = 0; i < count; ++i) {
		const T value = values[i];
		too_large |= !(value >= -high) | !(value <= high);
	}
	return kValuesNeedPattern | (too_large ? kValuesNeedStoredEntries : 0);
}

// Whether a row of the result has a zero, which is the only case that needs the pattern.
template <typename T>
bool RowHasZero(const T *row, size_t n_cols) {
	int zero = 0;
	for (size_t j = 0; j < n_cols; ++j) {
		zero |= row[j] == 0;
	}
	return zero != 0;
}

// pattern |= source. Returns zero if that leaves no bit unset.
inline uint64_t OrWords(uint64_t *pattern, const uint64_t *source, size_t words) {
	uint64_t unset = 0;
	for (size_t w = 0; w < words; ++w) {
		pattern[w] |= source[w];
		unset |= ~pattern[w];
	}
	return unset;
}

#if defined(__x86_64__)
// The same, 8 words at a time. A row can take this once per key that it has.
__attribute__((target("avx512f"))) inline uint64_t OrWordsAvx512(uint64_t *pattern, const uint64_t *source,
                                                                 size_t words) {
	const __m512i ones = _mm512_set1_epi64(-1);
	__m512i unset = _mm512_setzero_si512();
	size_t w = 0;
	for (; w + 8 <= words; w += 8) {
		const __m512i merged = _mm512_or_si512(_mm512_loadu_si512(pattern + w), _mm512_loadu_si512(source + w));
		_mm512_storeu_si512(pattern + w, merged);
		unset = _mm512_or_si512(unset, _mm512_xor_si512(merged, ones));
	}
	const bool all_set = _mm512_test_epi64_mask(unset, unset) == 0;
	return OrWords(pattern + w, source + w, words - w) | (all_set ? 0 : 1);
}
#endif

// The columns of the result that each join key of S reaches, for FillRow. A key that
// reaches at least 1/32 of the columns has a bitset, which then takes no more space than
// a list of them. So this takes at most 4 bytes per entry of S, whatever K and N are.
class ResultPattern {
public:
	// S as CSR: one row per column of the result, with sorted keys below key_count.
	ResultPattern(const int *row_ptr, const int *keys, int columns_p, int key_count, int threads)
	    : columns(columns_p), words((static_cast<size_t>(columns_p) + 63) / 64),
	      unused_bits(columns_p % 64 ? ~uint64_t(0) << (columns_p % 64) : 0),
	      simd(EmitRowNonzerosHasAvx512()) {
		const size_t key_words = (static_cast<size_t>(key_count) + 63) / 64;
		const size_t entries = columns ? static_cast<size_t>(row_ptr[columns]) : 0;
		// A range looks for its first key in each row, which long rows pay back
		const int ranges = entries >= static_cast<size_t>(threads) * columns * 8 ? threads : 1;
		present.assign(key_words, 0);
		word_rank.assign(key_words, 0);
		ForEachEntry(row_ptr, keys, key_words, ranges, threads,
		             [&](int key, int) { present[key >> 6] |= uint64_t(1) << (key & 63); });
		int distinct = 0;
		for (size_t w = 0; w < key_words; ++w) {
			word_rank[w] = distinct;
			distinct += __builtin_popcountll(present[w]);
		}
		reach.assign(static_cast<size_t>(distinct), KeyColumns());
		ForEachEntry(row_ptr, keys, key_words, ranges, threads, [&](int key, int) { reach[Rank(key)].count++; });
		size_t list_entries = 0, dense_keys = 0;
		for (auto &key : reach) {
			key.dense = static_cast<int64_t>(key.count) * 32 >= columns;
			key.start = key.dense ? dense_keys++ * words : list_entries;
			list_entries += key.dense ? 0 : static_cast<size_t>(key.count);
			key.count = 0;
		}
		lists.resize(list_entries);
		bits.assign(dense_keys * words, 0);
		ForEachEntry(row_ptr, keys, key_words, ranges, threads, [&](int key, int column) {
			auto &entry = reach[Rank(key)];
			if (entry.dense) {
				bits[entry.start + (column >> 6)] |= uint64_t(1) << (column & 63);
			} else {
				lists[entry.start + entry.count++] = column;
			}
		});
	}

	size_t Words() const {
		return words;
	}

	// Sets in pattern (Words() words) the columns that the keys of a row of R reach.
	// Stops as soon as it has all of them, which a dense result reaches after a few keys.
	// Bits past the last column are set.
	void FillRow(const int *row_keys, int count, uint64_t *pattern) const {
		if (!words) {
			return;
		}
		std::fill(pattern, pattern + words, uint64_t(0));
		pattern[words - 1] = unused_bits;
		for (int p = 0; p < count; ++p) {
			const int key = row_keys[p];
			if (!((present[key >> 6] >> (key & 63)) & 1)) {
				continue;
			}
			const auto &entry = reach[Rank(key)];
			if (!entry.dense) {
				const int *list = lists.data() + entry.start;
				for (int c = 0; c < entry.count; ++c) {
					pattern[list[c] >> 6] |= uint64_t(1) << (list[c] & 63);
				}
				continue;
			}
			const uint64_t *source = bits.data() + entry.start;
			if (!Or(pattern, source)) {
				return;
			}
		}
	}

private:
	struct KeyColumns {
		size_t start = 0; // in lists, or in bits
		int count = 0;
		bool dense = false;
	};

	uint64_t Or(uint64_t *pattern, const uint64_t *source) const {
#if defined(__x86_64__)
		if (simd) {
			return OrWordsAvx512(pattern, source, words);
		}
#endif
		return OrWords(pattern, source, words);
	}

	// Number of keys of S below `key`, which is one of them
	int Rank(int key) const {
		const uint64_t below = present[key >> 6] & ((uint64_t(1) << (key & 63)) - 1);
		return word_rank[key >> 6] + __builtin_popcountll(below);
	}

	// Calls fn(key, column) for each entry of S. Each range of keys has one thread, and
	// the ranges have whole words of `present`, so no two threads write the same value.
	template <class FN>
	void ForEachEntry(const int *row_ptr, const int *keys, size_t key_words, int ranges, int threads, FN &&fn) {
#pragma omp parallel for num_threads(threads) schedule(dynamic)
		for (int range = 0; range < ranges; ++range) {
			const size_t low = key_words * range / ranges * 64;
			const size_t high = key_words * (range + 1) / ranges * 64;
			for (int column = 0; column < columns; ++column) {
				const int *const end = keys + row_ptr[column + 1];
				const int *key = std::lower_bound(keys + row_ptr[column], end, static_cast<int64_t>(low));
				for (; key != end && static_cast<size_t>(*key) < high; ++key) {
					fn(*key, column);
				}
			}
		}
	}

	int columns;
	size_t words;
	uint64_t unused_bits;
	bool simd;
	std::vector<uint64_t> present; // bit k: S has key k
	std::vector<int> word_rank;    // keys of S in the words before word w
	std::vector<KeyColumns> reach; // by rank of the key
	std::vector<int> lists;
	std::vector<uint64_t> bits;
};

// The output of a row that has zeros: appends (row ID, column ID, value) for the columns
// set in `pattern`, as EmitRowNonzeros does for the nonzero ones. Every column writes its
// tuple and only keeps it if the bit is set, as a branch per column costs more than that
// on a row of mixed bits. Words without a bit are skipped.
template <typename T>
size_t EmitRowPattern(const T *row, const uint64_t *pattern, size_t &col, size_t n_cols, int row_id, const int *col_ids,
                      int *out_rows, int *out_cols, double *out_vals, size_t idx, size_t capacity) {
	size_t j = col;
	while (j < n_cols && idx < capacity) {
		const uint64_t word = pattern[j >> 6];
		if (!word) {
			j = (j | 63) + 1;
			continue;
		}
		const size_t word_end = std::min((j | 63) + 1, n_cols);
		for (; j < word_end && idx < capacity; ++j) {
			out_rows[idx] = row_id;
			out_cols[idx] = col_ids ? col_ids[j] : static_cast<int>(j);
			out_vals[idx] = static_cast<double>(row[j]);
			idx += (word >> (j & 63)) & 1;
		}
	}
	col = std::min(j, n_cols);
	return idx;
}

} // namespace spalm_output
