#pragma once

#include <algorithm>
#include <chrono>
#include <mutex>

namespace tutti {

/// Token bucket. Thread-safe; cheap enough for per-message checks on the
/// reliable channel (never used on the audio path).
class TokenBucket {
public:
    using Clock = std::chrono::steady_clock;

    TokenBucket(double rate_per_second, double burst)
        : rate_(rate_per_second), burst_(burst), tokens_(burst),
          last_(Clock::now()) {}

    /// Take one token. Returns false when the caller is over its rate.
    bool allow(Clock::time_point now = Clock::now()) {
        std::lock_guard<std::mutex> lock(mutex_);
        double elapsed = std::chrono::duration<double>(now - last_).count();
        if (elapsed > 0) {
            tokens_ = std::min(burst_, tokens_ + elapsed * rate_);
            last_ = now;
        }
        if (tokens_ < 1.0) return false;
        tokens_ -= 1.0;
        return true;
    }

private:
    const double rate_;
    const double burst_;
    double tokens_;
    Clock::time_point last_;
    std::mutex mutex_;
};

} // namespace tutti
