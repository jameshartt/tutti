// Fallback driver for compilers without libFuzzer (GCC). Runs the fuzz
// target over any files given on the command line, then over a fixed
// number of pseudo-random inputs built from the target's seed corpus.
// It is a smoke test; real fuzzing uses Clang's -fsanitize=fuzzer in CI.

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <random>
#include <string>
#include <vector>

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size);
extern const char* const kFuzzSeeds[];
extern const size_t kFuzzSeedCount;

int main(int argc, char** argv) {
    size_t runs = 0;
    for (int i = 1; i < argc; i++) {
        std::ifstream in(argv[i], std::ios::binary);
        std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(in)),
                                   std::istreambuf_iterator<char>());
        LLVMFuzzerTestOneInput(bytes.data(), bytes.size());
        runs++;
    }

    const char* env = std::getenv("FUZZ_ITERATIONS");
    const size_t iterations = env ? std::strtoull(env, nullptr, 10) : 20000;
    std::mt19937 rng(0x70771u);
    for (size_t n = 0; n < iterations; n++) {
        std::vector<uint8_t> input;
        if (kFuzzSeedCount > 0 && rng() % 4 != 0) {
            std::string seed = kFuzzSeeds[rng() % kFuzzSeedCount];
            input.assign(seed.begin(), seed.end());
            size_t mutations = rng() % 8;
            for (size_t m = 0; m < mutations && !input.empty(); m++) {
                size_t pos = rng() % input.size();
                switch (rng() % 4) {
                    case 0: input[pos] = static_cast<uint8_t>(rng()); break;
                    case 1: input.erase(input.begin() + pos); break;
                    case 2: input.insert(input.begin() + pos, static_cast<uint8_t>(rng())); break;
                    case 3: input.resize(pos); break;
                }
            }
        } else {
            input.resize(rng() % 600);
            for (auto& b : input) b = static_cast<uint8_t>(rng());
        }
        LLVMFuzzerTestOneInput(input.data(), input.size());
        runs++;
    }
    std::printf("standalone fuzz driver: %zu inputs, no crash\n", runs);
    return 0;
}
