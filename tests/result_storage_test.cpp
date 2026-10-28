// Build: c++ -std=c++11 -O2 -fopenmp -Isrc/include tests/result_storage_test.cpp
#include "result_storage.hpp"

#include <cassert>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <memory>
#include <random>
#include <vector>

using spalm_output::DenseRowLayout;

static void CheckBytes() {
	assert(spalm_output::CheckedBytes<double>(38) == 304);
	bool bad = false;
	try { spalm_output::CheckedBytes<double>(std::numeric_limits<size_t>::max()); }
	catch (const std::overflow_error &) { bad = true; }
	assert(bad);
}

static void CheckLayouts() {
	int ptr[] = {0, 0, 2, 2, 2, 3, 3};
	DenseRowLayout layout;
	layout.Initialize(ptr, 6, 19, true);
	assert(layout.Rows() == 2 && layout.LogicalRows() == 6 && layout.IsCompact());
	assert(layout.OriginalRow(0) == 1 && layout.OriginalRow(1) == 4);
	assert(layout.RowPointers(ptr)[0] == 0 && layout.RowPointers(ptr)[1] == 2 && layout.RowPointers(ptr)[2] == 3);
	assert(layout.Elements() == 38 && layout.Bytes<double>() == 304);
	layout.Initialize(ptr, 6, 19, false);
	assert(!layout.IsCompact() && layout.Rows() == 6 && layout.RowPointers(ptr) == ptr);
	for (int i = 0; i < 6; ++i) assert(layout.OriginalRow(i) == i);
	int full[] = {0, 1, 2};
	layout.Initialize(full, 2, 19, true);
	assert(!layout.IsCompact() && layout.RowPointers(full) == full);
	int empty[] = {0, 0, 0, 0};
	layout.Initialize(empty, 3, 19, true);
	assert(layout.IsCompact() && layout.Rows() == 0 && layout.Elements() == 0 && layout.RowPointers(empty)[0] == 0);
	layout.Initialize(empty, 0, 19, true);
	assert(layout.Rows() == 0 && layout.Elements() == 0);
	layout.Initialize(ptr, 6, 0, true);
	assert(layout.Rows() == 2 && layout.Elements() == 0);
	// Explicit numeric zero (or NaN) is still a stored entry; it does not make
	// a row structurally empty. No value-based pruning belongs in this helper.
	int explicit_zero[] = {0, 1, 1, 2};
	layout.Initialize(explicit_zero, 3, 4, true);
	assert(layout.Rows() == 2 && layout.OriginalRow(1) == 2);
	int malformed[] = {0, 2, 1};
	bool bad = false;
	try { layout.Initialize(malformed, 2, 1, true); } catch (const std::invalid_argument &) { bad = true; }
	assert(bad);
}

template <typename T>
static void CheckZero() {
	for (int threads : {1, 4}) {
		for (bool parallel : {false, true}) {
			for (size_t count : {size_t(0), size_t(1), size_t(65535), size_t(1048576 + 123)}) {
				std::unique_ptr<T[]> data(new T[count + 2]);
				std::fill(data.get(), data.get() + count + 2, T(7));
				spalm_output::ZeroDenseResult(data.get() + 1, count, threads, parallel);
				assert(data[0] == T(7) && data[count + 1] == T(7));
				for (size_t i = 1; i <= count; ++i) assert(data[i] == T(0) && !std::signbit(data[i]));
			}
		}
	}
	spalm_output::ZeroDenseResult<T>(nullptr, 0, 1, true);
	bool bad = false;
	try { spalm_output::ZeroDenseResult<T>(nullptr, 1, 1, true); } catch (const std::invalid_argument &) { bad = true; }
	assert(bad);
}

// C = R * S^T on compact views of both sides, with output IDs restored and
// output emitted in 2048-value batches. Covers empty leading/interior/tail
// rows on either side, repeated join keys, cancellations and arbitrary N.
template <typename T>
static void CheckProducts() {
	std::mt19937 rng(20261021);
	auto random_csr = [&](int rows, int K, std::vector<int> &rp, std::vector<int> &cols, std::vector<T> &values) {
		rp.assign(1, 0);
		for (int row = 0; row < rows; ++row) {
			const int entries = (rng() % 4) ? 0 : int(rng() % 20);
			for (int i = 0; i < entries; ++i) {
				cols.push_back(rng() % K);
				values.push_back(T(int(rng() % 7) - 3));
			}
			rp.push_back(int(cols.size()));
		}
	};
	for (int trial = 0; trial < 250; ++trial) {
		const int M = trial ? int(rng() % 100) : 4110;
		const int N = trial ? int(rng() % 41) : 3;
		const int K = 17;
		std::vector<int> rp, rc, sp, sc;
		std::vector<T> rv, sv;
		random_csr(M, K, rp, rc, rv);
		random_csr(N, K, sp, sc, sv);
		std::vector<T> s_dense(size_t(N) * K, T(0));
		for (int j = 0; j < N; ++j)
			for (int q = sp[j]; q < sp[j + 1]; ++q) s_dense[size_t(j) * K + sc[q]] += sv[q];
		std::vector<T> expected(size_t(M) * N, T(0));
		for (int i = 0; i < M; ++i)
			for (int p = rp[i]; p < rp[i + 1]; ++p)
				for (int j = 0; j < N; ++j) expected[size_t(i) * N + j] += rv[p] * s_dense[size_t(j) * K + rc[p]];
		for (bool compact : {false, true}) {
			DenseRowLayout r_layout, s_layout;
			s_layout.Initialize(sp.data(), N, 0, compact);
			r_layout.Initialize(rp.data(), M, s_layout.Rows(), compact);
			const int PM = r_layout.Rows(), PN = s_layout.Rows();
			std::unique_ptr<T[]> output(new T[r_layout.Elements()]);
			spalm_output::ZeroDenseResult(output.get(), r_layout.Elements(), 4, true);
			const int *rptr = r_layout.RowPointers(rp.data());
			const int *sptr = s_layout.RowPointers(sp.data());
			std::vector<T> row(K);
			for (int j = 0; j < PN; ++j) {
				std::fill(row.begin(), row.end(), T(0));
				for (int q = sptr[j]; q < sptr[j + 1]; ++q) row[sc[q]] += sv[q];
				for (int i = 0; i < PM; ++i)
					for (int p = rptr[i]; p < rptr[i + 1]; ++p) output[size_t(i) * PN + j] += rv[p] * row[rc[p]];
			}
			std::vector<T> restored(size_t(M) * N, T(0));
			for (size_t start = 0; start < r_layout.Elements(); start += 2048)
				for (size_t pos = start; pos < std::min(start + 2048, r_layout.Elements()); ++pos)
					restored[size_t(r_layout.OriginalRow(pos / PN)) * N + s_layout.OriginalRow(pos % PN)] = output[pos];
			assert(restored == expected);
		}
	}
}

// Uninitialized result storage, with and without huge pages and base
// offsets: usable for all `count` values and freed by the deleter (ASan
// checks that free() gets the allocation's own pointer).
template <typename T>
static void CheckAllocate() {
	const size_t huge = spalm_output::kHugePageBytes / sizeof(T);
	for (bool huge_pages : {false, true}) {
		for (size_t offset : {size_t(0), size_t(16), size_t(4096 - sizeof(T))}) {
			for (size_t count : {size_t(0), size_t(1), huge - 1, huge, huge * 3 + 5}) {
				bool advised = true;
				auto data = spalm_output::AllocateDenseResult<T>(count, huge_pages, offset, advised);
				assert(data);
				if (!huge_pages || count < huge) assert(!advised);
#if defined(__linux__)
				if (advised) assert(reinterpret_cast<uintptr_t>(data.get()) % spalm_output::kHugePageBytes == offset);
#endif
				spalm_output::ZeroDenseResult(data.get(), count, 4, true);
				for (size_t i = 0; i < count; i += 4097) assert(data[i] == T(0));
				if (count) data[count - 1] = T(1);
			}
		}
	}
	bool advised;
	bool bad = false;
	try { spalm_output::AllocateDenseResult<T>(std::numeric_limits<size_t>::max(), true, 0, advised); }
	catch (const std::overflow_error &) { bad = true; }
	assert(bad);
	for (size_t offset : {size_t(sizeof(T) / 2), size_t(4096)}) {
		bad = false;
		try { spalm_output::AllocateDenseResult<T>(16, true, offset, advised); }
		catch (const std::invalid_argument &) { bad = true; }
		assert(bad);
	}
}

// Fresh mappings (Linux): zero-filled without a zero pass, placed at
// base_offset past a 2 MiB (huge pages) or page boundary, released with
// munmap. Elsewhere the allocation falls back to uninitialized memory.
template <typename T>
static void CheckFreshMapping() {
	const size_t huge = spalm_output::kHugePageBytes / sizeof(T);
	for (bool huge_pages : {false, true}) {
		for (size_t offset : {size_t(0), size_t(16), size_t(4096 - sizeof(T))}) {
			for (size_t count : {size_t(0), size_t(1), huge - 1, huge, huge * 3 + 5}) {
				bool advised = true, zeroed = false;
				auto data = spalm_output::AllocateDenseResult<T>(count, huge_pages, offset, advised, zeroed, true);
				assert(data);
				if (!huge_pages || count < huge) assert(!advised);
#if defined(__linux__)
				assert(zeroed);
				const size_t align = huge_pages && count >= huge ? spalm_output::kHugePageBytes : size_t(4096);
				assert(reinterpret_cast<uintptr_t>(data.get()) % align == offset);
				for (size_t i = 0; i < count; i += 509) assert(data[i] == T(0));
				if (count) assert(data[count - 1] == T(0));
#else
				assert(!zeroed);
#endif
				if (count) data[0] = data[count - 1] = T(1);
			}
		}
	}
}

// Pre-faulting a fresh mapping: every page resident afterwards (Linux with
// MADV_POPULATE_WRITE), values still zero; elsewhere it reports false.
template <typename T>
static void CheckPrefault() {
	const size_t huge = spalm_output::kHugePageBytes / sizeof(T);
	assert(spalm_output::PrefaultDenseResult<T>(nullptr, 0, 4, true));
	for (bool huge_pages : {false, true}) {
		for (size_t count : {size_t(1), huge - 1, huge * 3 + 5}) {
			for (int threads : {1, 3, 8}) {
				bool advised = false, zeroed = false;
				auto data = spalm_output::AllocateDenseResult<T>(count, huge_pages, 16, advised, zeroed, true);
				const bool populated = spalm_output::PrefaultDenseResult(data.get(), count, threads, advised);
#if defined(__linux__) && defined(MADV_POPULATE_WRITE)
				assert(zeroed && populated);
				const uintptr_t page = 4096;
				const uintptr_t begin = reinterpret_cast<uintptr_t>(data.get()) / page * page;
				const uintptr_t end = (reinterpret_cast<uintptr_t>(data.get() + count) + page - 1) / page * page;
				std::vector<unsigned char> resident((end - begin) / page);
				assert(mincore(reinterpret_cast<void *>(begin), end - begin, resident.data()) == 0);
				for (unsigned char r : resident) assert(r & 1);
#endif
				// The caller's fallback: zero (and so pre-fault) the result.
				if (!populated) spalm_output::ZeroDenseResult(data.get(), count, threads, true);
				for (size_t i = 0; i < count; i += 509) assert(data[i] == T(0));
				assert(data[count - 1] == T(0));
				data[count - 1] = T(1);
			}
		}
	}
}

int main() {
	CheckBytes();
	CheckLayouts();
	CheckZero<float>(); CheckZero<double>();
	CheckAllocate<float>(); CheckAllocate<double>();
	CheckFreshMapping<float>(); CheckFreshMapping<double>();
	CheckPrefault<float>(); CheckPrefault<double>();
	CheckProducts<float>(); CheckProducts<double>();
	std::cout << "Result layout, output IDs, compact CSR products, one-pass initialization, result allocation, fresh mappings and pre-faulting passed\n";
}
