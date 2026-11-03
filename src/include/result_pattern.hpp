#pragma once

// Which pairs of a product matched. The result is dense, so a pair whose sum is zero
// looks like a pair without a match, and DuckDB has a row for the first but not for the
// second. Most inputs need none of this: CheckValues tells from one pass over the values.
// Otherwise a row with few zeros looks each of them up in S (RowKeys), and a row with
// many gets the columns that it matches from ResultPattern.
// Header-only so tests/result_pattern_test.cpp can exercise it without DuckDB.

#include "result_storage.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace spalm_output {

// Inputs of 2^31 entries give at most 2^62 pairs, merged or not. If the magnitudes of all
// values of both inputs lie in [kLow, kHigh], then kLow^2 is above the smallest normal
// number and 2^62 * kHigh^2 below the largest one: products neither underflow nor
// overflow. If each input also has one sign, all products have one sign too, so nothing
// cancels out, and the sum of a pair that matched is not zero.
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

// The result of CheckValues, to combine with | over the chunks of an input.
enum ValueFlags : int {
	// A value is outside [kLow, kHigh]
	kValuesNotPositive = 1,
	// A value is outside [-kHigh, -kLow]
	kValuesNotNegative = 2,
	// NaN, Inf or above kHigh in magnitude, which is also what can overflow. A dense kernel
	// reads a missing entry as zero, and 0 * Inf would give NaN to pairs without such an entry.
	kValuesTooLarge = 4
};

// Whether a zero in the result can be anything but a pair without a match, because of the
// values of this input. If neither input needs it, nothing can underflow or overflow.
inline bool InputNeedsPattern(int flags) {
	return (flags & kValuesNotPositive) && (flags & kValuesNotNegative);
}

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
		return count ? kValuesNotNegative : 0; // an empty chunk says nothing of the signs
	}
	Mask not_negative = 0, too_large = 0;
	for (size_t i = 0; i < count; ++i) {
		const T value = values[i];
		const Mask below_range = !(value >= -high);
		not_negative |= below_range | !(value <= -low);
		too_large |= below_range | !(value <= high);
	}
	return kValuesNotPositive | (not_negative ? kValuesNotNegative : 0) | (too_large ? kValuesTooLarge : 0);
}

// The zeros of a row of the result, which are the only columns that need a look at S:
// each one is a sum of pairs that matched, or a pair without a match. A column without
// keys in S has no match, so it is left out. Looking the others up costs up to the keys
// of their columns, and past `budget` of them this stops and returns false: the row goes
// to the pattern. Looks in blocks, as most rows of a dense result have no zero at all.
template <typename T>
bool CollectZeros(const T *row, size_t n_cols, const int *s_row_ptr, size_t budget, std::vector<int> &zeros) {
	zeros.clear();
	size_t cost = 0;
	const size_t block = 64;
	for (size_t begin = 0; begin < n_cols; begin += block) {
		const size_t end = std::min(begin + block, n_cols);
		int zero = 0;
		for (size_t j = begin; j < end; ++j) {
			zero |= row[j] == 0;
		}
		if (!zero) {
			continue;
		}
		for (size_t j = begin; j < end; ++j) {
			const size_t keys = static_cast<size_t>(s_row_ptr[j + 1] - s_row_ptr[j]);
			if (row[j] != 0 || !keys) {
				continue;
			}
			cost += keys;
			if (cost > budget) {
				return false;
			}
			zeros.push_back(static_cast<int>(j));
		}
	}
	return true;
}

// The keys of a row of R, to look up the few zeros of its row of the result in S instead
// of building the pattern. A bitset from the first key of the row to its last, unless
// that takes more than 32 bytes a key: then a binary search, as keys can go up to 2^31.
class RowKeys {
public:
	// The keys of the row, sorted, at least one. They must outlive the lookups.
	void Set(const int *keys, int count) {
		keys_ = keys;
		count_ = count;
		first_word = keys[0] >> 6;
		const size_t words = static_cast<size_t>((keys[count - 1] >> 6) - first_word) + 1;
		use_bits = words <= std::max(size_t(1024), 4 * static_cast<size_t>(count));
		if (!use_bits) {
			return;
		}
		if (bits.size() < words) {
			bits.resize(words, 0);
		}
		for (int p = 0; p < count; ++p) {
			bits[(keys[p] >> 6) - first_word] |= uint64_t(1) << (keys[p] & 63);
		}
	}

	// Leaves the bitset empty for the next row
	void Clear() {
		if (use_bits) {
			for (int p = 0; p < count_; ++p) {
				bits[(keys_[p] >> 6) - first_word] = 0;
			}
		}
	}

	// Whether one of the sorted keys of a column of S is a key of the row
	bool Matches(const int *keys, int count) const {
		const int low = keys_[0], high = keys_[count_ - 1];
		const int *const end = keys + count;
		for (const int *key = std::lower_bound(keys, end, low); key != end && *key <= high; ++key) {
			if (use_bits ? (bits[(*key >> 6) - first_word] >> (*key & 63)) & 1
			             : std::binary_search(keys_, keys_ + count_, *key)) {
				return true;
			}
		}
		return false;
	}

	// Keeps the columns that have a key of the row in S, in their order
	void KeepMatched(const int *s_row_ptr, const int *s_keys, std::vector<int> &columns) const {
		size_t kept = 0;
		for (const int j : columns) {
			if (Matches(s_keys + s_row_ptr[j], s_row_ptr[j + 1] - s_row_ptr[j])) {
				columns[kept++] = j;
			}
		}
		columns.resize(kept);
	}

private:
	const int *keys_ = nullptr;
	int count_ = 0;
	int first_word = 0;
	bool use_bits = false;
	std::vector<uint64_t> bits; // all zero between rows
};

// Appends the tuples of `columns` of a row from `next`, as EmitRowNonzeros does for the
// nonzero columns: for the zeros of a row that matched, after its nonzeros.
template <typename T>
size_t EmitColumns(const T *row, const std::vector<int> &columns, size_t &next, int row_id, const int *col_ids,
                   int *out_rows, int *out_cols, double *out_vals, size_t idx, size_t capacity) {
	for (; next < columns.size() && idx < capacity; ++next, ++idx) {
		const int j = columns[next];
		out_rows[idx] = row_id;
		out_cols[idx] = col_ids ? col_ids[j] : j;
		out_vals[idx] = static_cast<double>(row[j]);
	}
	return idx;
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

// The columns of the result that each join key of S reaches, for FillRow. A key has a
// bitset of the words from its first to its last column, or a list of its columns. Both
// are read once per key of a row, out of cache for a large S, so the bytes decide: a key
// has a bitset if it takes at most twice the bytes of the list, which a band of columns
// does. So this takes at most 12 bytes per entry of S, whatever K and N are.
class ResultPattern {
public:
	// S as CSR: one row per column of the result, with sorted keys below key_count.
	ResultPattern(const int *row_ptr, const int *keys, int columns_p, int key_count, int threads)
	    : columns(columns_p), words((static_cast<size_t>(columns_p) + 63) / 64),
	      unused_bits(columns_p % 64 ? ~uint64_t(0) << (columns_p % 64) : 0),
	      simd(EmitRowNonzerosHasAvx512()) {
		const size_t key_words = (static_cast<size_t>(key_count) + 63) / 64;
		const size_t entries = columns ? static_cast<size_t>(row_ptr[columns]) : 0;
		// A range looks for its first key in each row, which rows of 16 entries pay back
		const int ranges = entries >= static_cast<size_t>(columns) * 16 ? threads : 1;
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
		ForEachEntry(row_ptr, keys, key_words, ranges, threads, [&](int key, int column) {
			auto &entry = reach[Rank(key)];
			entry.first_word = entry.count++ ? entry.first_word : column >> 6;
			entry.last_word = column >> 6;
		});
		// Columns come in order, so the first and the last that a key saw are its bounds
		size_t list_entries = 0, bit_words = 0;
		for (auto &key : reach) {
			key.dense = key.Words() <= static_cast<size_t>(key.count);
			key.start = key.dense ? bit_words : list_entries;
			(key.dense ? bit_words : list_entries) += key.dense ? key.Words() : static_cast<size_t>(key.count);
			key.count = 0;
		}
		lists.resize(list_entries);
		bits.assign(bit_words, 0);
		ForEachEntry(row_ptr, keys, key_words, ranges, threads, [&](int key, int column) {
			auto &entry = reach[Rank(key)];
			if (entry.dense) {
				bits[entry.start + (column >> 6) - entry.first_word] |= uint64_t(1) << (column & 63);
			} else {
				lists[entry.start + entry.count++] = column;
			}
		});
	}

	size_t Words() const {
		return words;
	}

	// Sets in pattern (Words() words) the columns that the keys of a row of R reach.
	// Bits past the last column are set. Stops as soon as all columns are set, which a
	// dense result gets to after a few keys.
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
			if (!Or(pattern + entry.first_word, bits.data() + entry.start, entry.Words()) && IsFull(pattern)) {
				return;
			}
		}
	}

private:
	struct KeyColumns {
		size_t start = 0; // in lists, or in bits
		int count = 0;
		int first_word = 0; // of the result, for a bitset
		int last_word = 0;
		bool dense = false;

		size_t Words() const {
			return static_cast<size_t>(last_word - first_word) + 1;
		}
	};

	bool IsFull(const uint64_t *pattern) const {
		return std::all_of(pattern, pattern + words, [](uint64_t word) { return word == ~uint64_t(0); });
	}

	uint64_t Or(uint64_t *pattern, const uint64_t *source, size_t count) const {
#if defined(__x86_64__)
		if (simd) {
			return OrWordsAvx512(pattern, source, count);
		}
#endif
		return OrWords(pattern, source, count);
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
