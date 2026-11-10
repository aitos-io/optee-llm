// SPDX-License-Identifier: BSD-2-Clause
#include <cstdint>
#include <cstdio>
extern "C" uint64_t cxxprobe_alloc_bench(void);
int main() { std::printf("linux alloc bench: %llu us\n", (unsigned long long)cxxprobe_alloc_bench()); }
