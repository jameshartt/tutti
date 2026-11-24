#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>

#include <nlohmann/json_fwd.hpp>

namespace tutti {

/// Game datagram kinds (first byte of every game datagram).
enum class GameDatagramKind : uint8_t {
    /// Diagnostic: the server sends the datagram straight back to the
    /// sender. Used by connection self-tests and end-to-end tests.
    Echo = 0x00,
};

/// Per-room game state. Owned by Room; receives every `game_*` reliable
/// message and every game datagram from the room's bound participants.
///
/// Sprint 0: plumbing only (ping broadcast + datagram echo). The match
/// simulation arrives in later sprints.
///
/// Thread-safety: handlers are called concurrently from transport threads.
class GameSession {
public:
    /// How the session talks back to the room's participants.
    struct Outbound {
        /// Reliable JSON message to every bound participant
        std::function<void(const std::string& msg)> broadcast;
        /// Unreliable [kind][payload] datagram to one participant
        std::function<bool(const std::string& participant_id,
                           const uint8_t* data, size_t len)> send_datagram;
    };

    explicit GameSession(std::string room_name, Outbound out);

    /// A reliable message whose type starts with "game_"
    void on_message(const std::string& participant_id,
                    const std::string& type, const nlohmann::json& msg);

    /// A game datagram: [kind u8][payload...], len >= 1
    void on_datagram(const std::string& participant_id,
                     const uint8_t* data, size_t len);

    uint64_t datagrams_received() const {
        return datagrams_received_.load(std::memory_order_relaxed);
    }

private:
    std::string room_name_;
    Outbound out_;
    std::atomic<uint64_t> datagrams_received_{0};
};

} // namespace tutti
