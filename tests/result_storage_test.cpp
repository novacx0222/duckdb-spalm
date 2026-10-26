// Build: c++ -std=c++17 -O2 -fopenmp -Isrc/include tests/result_storage_test.cpp
#include "result_storage.hpp"

#include <cassert>
#include <cmath>
#include <iostream>
#include <memory>

static void CheckBytes() {
	assert(spalm_output::CheckedBytes<double>(38) == 304);
	bool bad = false;
	try { spalm_output::CheckedBytes<double>(std::numeric_limits<size_t>::max()); }
	catch (const std::overflow_error &) { bad = true; }
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

int main() {
	CheckBytes();
	CheckZero<float>(); CheckZero<double>();
	std::cout << "One-pass result initialization passed\n";
}
