// Steps the spike scene with its built-in script and prints state hashes.
//
//   tutti-game-spike [--ticks N] [--every N] [--hashes FILE] [--bench]
//
// --hashes writes one hex hash per tick, the reference the browser builds
// are compared against. --bench prints rollback timings.

#include "c_api.h"

#include <chrono>
#include <cinttypes>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

namespace {

double ms_since(std::chrono::steady_clock::time_point start) {
    return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
}

void run_bench() {
    tg_world* world = tg_create();
    // Tick 240 is mid-action: crates flying, cars 2 and 3 about to touch
    tg_run_script(world, 240, nullptr);
    const int size = tg_save_state(world);
    std::printf("snapshot bytes: %d\n", size);

    const int n = 2000;
    auto t = std::chrono::steady_clock::now();
    tg_bench_save(world, n);
    std::printf("save:      %.4f ms\n", ms_since(t) / n);
    t = std::chrono::steady_clock::now();
    tg_bench_restore(world, n);
    std::printf("restore:   %.4f ms\n", ms_since(t) / n);
    t = std::chrono::steady_clock::now();
    tg_bench_step(world, 10, 200);
    std::printf("tick:      %.4f ms\n", ms_since(t) / (200 * 10));
    t = std::chrono::steady_clock::now();
    tg_bench_rollback(world, 10, 200);
    std::printf("rollback (restore + 10 x (tick + save)): %.4f ms\n", ms_since(t) / 200);
    tg_destroy(world);
}

}  // namespace

int main(int argc, char** argv) {
    int ticks = 3600;
    int every = 600;
    const char* hashes_path = nullptr;
    bool bench = false;
    for (int i = 1; i < argc; ++i) {
        if (!std::strcmp(argv[i], "--ticks") && i + 1 < argc) ticks = std::atoi(argv[++i]);
        else if (!std::strcmp(argv[i], "--every") && i + 1 < argc) every = std::atoi(argv[++i]);
        else if (!std::strcmp(argv[i], "--hashes") && i + 1 < argc) hashes_path = argv[++i];
        else if (!std::strcmp(argv[i], "--bench")) bench = true;
        else {
            std::fprintf(stderr, "usage: %s [--ticks N] [--every N] [--hashes FILE] [--bench]\n", argv[0]);
            return 2;
        }
    }
    if (ticks < 1 || every < 1) return 2;

    if (bench) {
        run_bench();
        return 0;
    }

    tg_world* world = tg_create();
    std::vector<uint32_t> hashes(static_cast<size_t>(ticks) * 2);
    const auto start = std::chrono::steady_clock::now();
    tg_run_script(world, ticks, hashes.data());
    const double elapsed = ms_since(start);

    std::FILE* out = hashes_path ? std::fopen(hashes_path, "w") : nullptr;
    if (hashes_path && !out) {
        std::perror(hashes_path);
        return 1;
    }
    for (int i = 0; i < ticks; ++i) {
        const uint64_t hash = (static_cast<uint64_t>(hashes[2 * i + 1]) << 32) | hashes[2 * i];
        if (out) std::fprintf(out, "%016" PRIx64 "\n", hash);
        if ((i + 1) % every == 0 || i + 1 == ticks) std::printf("tick %5d  %016" PRIx64 "\n", i + 1, hash);
    }
    if (out) std::fclose(out);

    const uint32_t events = tg_events(world);
    std::printf("events: car-car %s, car-crate %s, jump %s, rollover %s, car-wall %s\n",
                (events & 1) ? "yes" : "no", (events & 2) ? "yes" : "no", (events & 4) ? "yes" : "no",
                (events & 8) ? "yes" : "no", (events & 16) ? "yes" : "no");
    std::printf("%d ticks (%.1f s of game time) in %.1f ms\n", ticks, ticks / 60.0, elapsed);
    tg_destroy(world);
    return 0;
}
