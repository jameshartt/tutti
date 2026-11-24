// Fuzzes LineFramer, which reassembles control messages from the
// WebTransport stream. The input is fed in fragments whose sizes are also
// taken from the input, and the result is checked against feeding it whole.

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <string>
#include <vector>

#include "transport/line_framer.h"

extern const char* const kFuzzSeeds[] = {
    "{\"type\":\"bind\"}\n",
    "a\nb\r\nc\n\n\nd",
    "\n\n\n",
    "no newline at all",
};
extern const size_t kFuzzSeedCount = sizeof(kFuzzSeeds) / sizeof(kFuzzSeeds[0]);

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
    using tutti::LineFramer;
    const char* text = reinterpret_cast<const char*>(data);
    const size_t max_line = 64; // small, so the oversized-line path is hit

    std::vector<std::string> whole;
    {
        LineFramer f(max_line);
        f.feed(text, size, [&](const std::string& l) { whole.push_back(l); });
    }

    std::vector<std::string> pieces;
    {
        LineFramer f(max_line);
        size_t pos = 0, step_index = 0;
        while (pos < size) {
            // Fragment sizes 1..16 derived from the input itself
            size_t step = 1 + (data[step_index++ % size] % 16);
            if (step > size - pos) step = size - pos;
            f.feed(text + pos, step, [&](const std::string& l) { pieces.push_back(l); });
            pos += step;
        }
    }

    for (const auto& l : pieces) {
        if (l.empty() || l.size() > max_line) std::abort();
        if (l.find('\n') != std::string::npos) std::abort();
    }
    // The same bytes give the same lines however they are split
    if (whole != pieces) std::abort();
    return 0;
}
