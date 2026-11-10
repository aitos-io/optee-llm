// SPDX-License-Identifier: BSD-2-Clause
// The C++ runtime features llama.cpp relies on, run inside a TA on tee-libc. cxxprobe_run()
// returns one bit per check that passed (CXXPROBE_* in cxxprobe_ta.h).
#include <cstdint>
#include <cstdio>
#include <map>
#include <mutex>
#include <regex>
#include <sstream>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>

#include "cxxprobe_ta.h"

namespace {

// Set by a constructor: shows that the TA ran the C++ static initializers.
struct Init {
	int value = 0;
	Init() { value = 42; }
};
Init g_init;

std::mutex g_mutex;

}  // namespace

extern "C" uint32_t cxxprobe_run(void)
{
	uint32_t ok = 0;

	if (g_init.value == 42)
		ok |= CXXPROBE_STATIC_INIT;

	std::vector<std::string> v{"alpha", "beta"};
	std::map<std::string, int> m{{"alpha", 1}};
	std::unordered_map<std::string, int> u{{"beta", 2}};
	if (v.size() == 2 && m["alpha"] == 1 && u["beta"] == 2)
		ok |= CXXPROBE_CONTAINERS;

	try {
		throw std::runtime_error("probe");
	} catch (const std::exception &e) {
		if (std::string(e.what()) == "probe")
			ok |= CXXPROBE_EXCEPTIONS;
	}

	{
		std::lock_guard<std::mutex> lock(g_mutex);
		ok |= CXXPROBE_MUTEX;
	}

	std::ostringstream os;
	os << "n=" << 42 << " x=" << 1.5;
	if (os.str() == "n=42 x=1.5")
		ok |= CXXPROBE_STREAMS;

	// llama.cpp's pre-tokenizers in unicode.cpp fall back to std::regex.
	std::regex re("[a-z]+([0-9]*)");
	std::smatch match;
	std::string s = "hello123";
	if (std::regex_match(s, match, re) && match[1] == "123")
		ok |= CXXPROBE_REGEX;

	// stdout goes through tee-libc's write path into the TA log.
	std::printf("cxxprobe: checks 0x%x\n", ok);
	return ok;
}
