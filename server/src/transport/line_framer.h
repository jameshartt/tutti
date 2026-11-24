#pragma once

#include <cstddef>
#include <string>
#include <utility>

namespace tutti {

/// Reassembles newline-delimited messages from a byte stream that may
/// deliver them in arbitrary fragments (a QUIC stream gives no message
/// boundaries). Not thread-safe: one framer per stream.
class LineFramer {
public:
    explicit LineFramer(size_t max_line = 256 * 1024) : max_line_(max_line) {}

    /// Forget any partial line (for example when the underlying stream is
    /// replaced by a new one).
    void reset() {
        buf_.clear();
        discarding_ = false;
    }

    /// Feed a fragment; `on_line` is called once per complete non-empty
    /// line, without the trailing newline (or a trailing '\r').
    template <typename F>
    void feed(const char* data, size_t len, F&& on_line) {
        size_t start = 0;
        for (size_t i = 0; i < len; ++i) {
            if (data[i] != '\n') continue;
            if (discarding_) {
                // End of an oversized line: drop it and resume
                discarding_ = false;
            } else {
                buf_.append(data + start, i - start);
                if (!buf_.empty() && buf_.back() == '\r') buf_.pop_back();
                if (!buf_.empty()) on_line(std::as_const(buf_));
            }
            buf_.clear();
            start = i + 1;
        }
        if (start < len && !discarding_) {
            buf_.append(data + start, len - start);
            if (buf_.size() > max_line_) {
                // A peer that never sends a newline must not grow memory
                // without bound: drop everything up to the next newline.
                buf_.clear();
                buf_.shrink_to_fit();
                discarding_ = true;
            }
        }
    }

    size_t buffered() const { return buf_.size(); }

private:
    std::string buf_;
    size_t max_line_;
    bool discarding_ = false;
};

} // namespace tutti
