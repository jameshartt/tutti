#pragma once

#include <cstddef>
#include <cstdint>
#include <random>
#include <string>

#if defined(__linux__)
#include <sys/random.h>
#endif

namespace tutti {

/// Fill `out` with `len` cryptographically secure random bytes.
///
/// Participant and session IDs act as bearer tokens (knowing one
/// authorizes transport binding), so they MUST come from the OS
/// CSPRNG — never from std::mt19937, which is predictable and was
/// previously shared across threads without locking.
inline void secure_random_bytes(uint8_t* out, size_t len) {
#if defined(__linux__)
    size_t off = 0;
    while (off < len) {
        ssize_t n = getrandom(out + off, len - off, 0);
        if (n < 0) break; // fall through to std::random_device below
        off += static_cast<size_t>(n);
    }
    if (off == len) return;
#endif
    // Fallback: std::random_device (reads the OS entropy source on
    // mainstream platforms). Thread-local so concurrent callers are safe.
    thread_local std::random_device rd;
    for (size_t i = 0; i < len; ++i) {
        out[i] = static_cast<uint8_t>(rd());
    }
}

/// Generate a hex token of `n_bytes` secure random bytes (2 chars/byte).
inline std::string secure_random_hex(size_t n_bytes) {
    static constexpr char kHex[] = "0123456789abcdef";
    uint8_t buf[64];
    if (n_bytes > sizeof(buf)) n_bytes = sizeof(buf);
    secure_random_bytes(buf, n_bytes);

    std::string out;
    out.reserve(n_bytes * 2);
    for (size_t i = 0; i < n_bytes; ++i) {
        out.push_back(kHex[buf[i] >> 4]);
        out.push_back(kHex[buf[i] & 0x0f]);
    }
    return out;
}

} // namespace tutti
