// Build: c++ -std=c++11 -O2 -Isrc/include tests/sparse_result_test.cpp
// Sparse output: init_method names, the result handle and the emission of
// row blocks (zeros dropped like the dense output, IDs mapped, any capacity).
#include "sparse_result.hpp"

#include <cassert>
#include <cmath>
#include <iostream>
#include <tuple>
#include <vector>

using spalm_output::EmitBlockNonzeros;
using spalm_output::SpalmCsrBlock;

static void CheckMethods() {
	auto m = spalm_output::ResolveSpalmMethod("sparse_sparse_output");
	assert(m.init_method == "sparse" && m.sparse_output);
	m = spalm_output::ResolveSpalmMethod("FORCE_MKL_SPARSE_SPARSE_OUTPUT");
	assert(m.init_method == "FORCE_MKL_SPARSE" && m.sparse_output);
	for (const char *same : {"sparse", "FORCE_MKL_SPARSE", "FORCE_MKL_DENSE", "dense_bm_inner_kernel",
	                         "dense_left_spmm_inner_kernel", "dense_right_spmm_inner_kernel", "", "no_fallback",
	                         "SPARSE_SPARSE_OUTPUT", "sparse_sparse_output "}) {
		m = spalm_output::ResolveSpalmMethod(same);
		assert(m.init_method == same && !m.sparse_output);
	}
}

static int frees = 0;
static void *freed = nullptr;
static void CountFree(void *handle) {
	++frees;
	freed = handle;
}

static void CheckHandle() {
	int a = 0, b = 0;
	{
		spalm_output::SparseResult r;
		assert(!r.Active() && r.Count() == 0 && !r.Blocks());
		r.Reset(&a, CountFree);
		SpalmCsrBlock block {};
		r.SetBlocks(&block, 1);
		assert(r.Active() && r.Count() == 1 && r.Blocks() == &block);
		r.Reset(&b, CountFree); // frees a
		assert(frees == 1 && freed == &a && r.Count() == 0);
		r.SetBlocks(nullptr, 5);
		assert(r.Count() == 0);
	}
	assert(frees == 2 && freed == &b); // destructor frees b
	{
		spalm_output::SparseResult r;
		r.Reset(nullptr, CountFree); // a failed call: nothing to free
	}
	assert(frees == 2);
}

typedef std::tuple<int, int, double> Tuple;

// Emits every block with the given capacity per call, as the source does
// over several chunks.
template <typename T>
static std::vector<Tuple> EmitAll(const std::vector<SpalmCsrBlock> &blocks, const int *row_ids, const int *col_ids,
                                  size_t capacity) {
	std::vector<Tuple> out;
	std::vector<int> rows(capacity), cols(capacity);
	std::vector<double> vals(capacity);
	size_t b = 0;
	int row = 0, pos = 0;
	bool open = false;
	for (int guard = 0; guard < 100000; ++guard) {
		size_t idx = 0;
		while (idx < capacity) {
			if (!open) {
				if (b >= blocks.size()) {
					break;
				}
				row = 0;
				pos = blocks[b].starts[0];
				open = true;
			}
			idx = EmitBlockNonzeros<T>(blocks[b], row, pos, row_ids, col_ids, rows.data(), cols.data(), vals.data(),
			                           idx, capacity);
			if (row >= blocks[b].rows) {
				open = false;
				++b;
			}
		}
		for (size_t i = 0; i < idx; ++i) {
			out.push_back(Tuple(rows[i], cols[i], vals[i]));
		}
		if (!open && b >= blocks.size()) {
			return out;
		}
	}
	assert(false);
	return out;
}

static bool Same(const std::vector<Tuple> &a, const std::vector<Tuple> &b) {
	if (a.size() != b.size()) {
		return false;
	}
	for (size_t i = 0; i < a.size(); ++i) {
		const double x = std::get<2>(a[i]), y = std::get<2>(b[i]);
		if (std::get<0>(a[i]) != std::get<0>(b[i]) || std::get<1>(a[i]) != std::get<1>(b[i]) ||
		    !(x == y || (std::isnan(x) && std::isnan(y)))) {
			return false;
		}
	}
	return true;
}

template <typename T>
static void CheckEmit() {
	// Block 0: rows 1..4 (row 2 empty, row 3 only zeros) with absolute
	// offsets and separate starts / ends as MKL's are, and an entry between
	// rows 1 and 2 that belongs to no row; block 1: row 4's rest and row 5,
	// relative offsets as the SS pages are.
	const int cols0[] = {9, 2, 7, 6, 0, 5, 3, 1, 4};
	const T vals0[] = {T(1.5), T(0), T(-2), T(99), T(0), T(-0.0), T(NAN), T(3), T(4)};
	const int starts0[] = {0, 4, 4, 6};
	const int ends0[] = {3, 4, 6, 9};
	const int cols1[] = {8, 6, 2};
	const T vals1[] = {T(7), T(0), T(-1)};
	const int ptr1[] = {0, 2, 3};
	std::vector<SpalmCsrBlock> blocks;
	blocks.push_back(SpalmCsrBlock {1, 4, starts0, ends0, cols0, vals0});
	blocks.push_back(SpalmCsrBlock {4, 2, ptr1, ptr1 + 1, cols1, vals1});
	// Physical -> original IDs.
	const int row_ids[] = {100, 101, 102, 103, 104, 105};
	int col_ids[10];
	for (int j = 0; j < 10; ++j) {
		col_ids[j] = 1000 + j;
	}
	const std::vector<Tuple> want = {Tuple(101, 1009, 1.5), Tuple(101, 1007, -2), Tuple(104, 1003, NAN),
	                                 Tuple(104, 1001, 3),   Tuple(104, 1004, 4),  Tuple(104, 1008, 7),
	                                 Tuple(105, 1002, -1)};
	for (size_t capacity = 1; capacity <= 9; ++capacity) {
		assert(Same(EmitAll<T>(blocks, row_ids, col_ids, capacity), want));
	}
	// Identity IDs (layouts that are not compact).
	const std::vector<Tuple> identity = {Tuple(1, 9, 1.5), Tuple(1, 7, -2), Tuple(4, 3, NAN), Tuple(4, 1, 3),
	                                     Tuple(4, 4, 4),   Tuple(4, 8, 7),  Tuple(5, 2, -1)};
	assert(Same(EmitAll<T>(blocks, nullptr, nullptr, 2048), identity));
	// The output must hold -0.0 nowhere and every value as double.
	for (const auto &t : EmitAll<T>(blocks, nullptr, nullptr, 3)) {
		assert(std::get<2>(t) != 0);
	}
}

int main() {
	CheckMethods();
	CheckHandle();
	CheckEmit<double>();
	CheckEmit<float>();
	std::cout << "sparse_result_test: OK" << std::endl;
	return 0;
}
