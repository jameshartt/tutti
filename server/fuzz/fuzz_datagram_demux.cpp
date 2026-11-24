// Fuzzes the audio/game datagram demux used on the shared WebTransport
// datagram channel, checking the separation invariants on every input.

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>

#include "transport/transport_interface.h"

extern const char* const kFuzzSeeds[] = {
    "\xFF\xFF\xFF\xFF\x01payload",
    "\xFF\xFF\xFF\xFF",
    "\xFE\xFF\xFF\xFF" "audio-ish",
    "\x01",
};
extern const size_t kFuzzSeedCount = sizeof(kFuzzSeeds) / sizeof(kFuzzSeeds[0]);

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
    using namespace tutti;
    int audio = 0, game = 0;
    TransportCallbacks cb;
    cb.on_datagram = [&](TransportSession*, const uint8_t* d, size_t n) {
        audio++;
        if (d != data || n != size) std::abort();      // audio is untouched
        if (is_game_datagram(d, n)) std::abort();      // never game bytes
    };
    cb.on_game_datagram = [&](TransportSession*, const uint8_t* d, size_t n) {
        game++;
        if (n == 0 || n > kMaxGameDatagramSize) std::abort();
        if (n != size - kGameMagicSize) std::abort();
        if (std::memcmp(d, data + kGameMagicSize, n) != 0) std::abort();
    };
    dispatch_datagram(cb, nullptr, data, size);
    if (audio + game > 1) std::abort();                // never both
    return 0;
}
