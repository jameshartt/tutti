#pragma once

#include <cstddef>
#include <string>
#include <string_view>

namespace tutti {

/// Make client-supplied text safe to write to the server log: control
/// characters and non-ASCII bytes become '?', and the text is truncated.
/// Stops a client forging log lines with embedded newlines.
inline std::string log_safe(std::string_view text, size_t max_len = 64) {
    std::string out;
    out.reserve(text.size() < max_len ? text.size() : max_len + 3);
    for (unsigned char c : text) {
        if (out.size() >= max_len) {
            out += "...";
            break;
        }
        out += (c >= 0x20 && c < 0x7F) ? static_cast<char>(c) : '?';
    }
    return out;
}

} // namespace tutti
