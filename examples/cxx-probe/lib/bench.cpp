// SPDX-License-Identifier: BSD-2-Clause
// What loading a tokenizer does to the allocator: hundreds of thousands of small strings and map
// nodes (Llama 3: 128k tokens, 280k BPE merges). Returns elapsed microseconds; built for the TA
// (probe library) and for Linux (bench_ref) to compare.
#include <chrono>
#include <cstdint>
#include <map>
#include <string>
#include <vector>

extern "C" uint64_t cxxprobe_alloc_bench(void)
{
	auto t0 = std::chrono::steady_clock::now();
	std::vector<std::string> tokens;
	std::map<std::pair<std::string, std::string>, int> merges;
	for (int i = 0; i < 128000; i++)
		tokens.push_back("token_" + std::to_string(i * 7919));
	for (int i = 0; i < 280000; i++)
		merges[{tokens[i % 128000], tokens[(i * 31) % 128000]}] = i;
	auto t1 = std::chrono::steady_clock::now();
	return (uint64_t)std::chrono::duration_cast<std::chrono::microseconds>(t1 - t0).count() +
	       (merges.size() == 0);
}
