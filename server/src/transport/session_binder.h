#pragma once

#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>

#include "transport_interface.h"
#include "rate_limiter.h"
#include "rooms/room_manager.h"

namespace tutti {

/// Largest reliable message accepted from a client. Legitimate control
/// messages are a few hundred bytes.
constexpr size_t kMaxControlMessageSize = 64 * 1024;
/// Largest `game_*` message accepted. These can be relayed to a whole room.
constexpr size_t kMaxGameMessageSize = 16 * 1024;
/// Sustained rate and burst of `game_*` messages accepted per session.
/// Fast game traffic (inputs, snapshots) uses datagrams, not this channel.
constexpr double kGameMessageRate = 60.0;
constexpr double kGameMessageBurst = 120.0;

/// Server monotonic time in milliseconds (the `server_t` in `pong`).
double server_time_ms();

/// Transport-agnostic session binder.
///
/// When a new transport session opens (WebTransport or WebRTC), SessionBinder:
/// 1. Waits for the first reliable message ("bind" message) from the client
/// 2. Looks up the room and participant in RoomManager
/// 3. Calls Room::attach_session() to wire the session
/// 4. Routes subsequent datagrams to Room::on_audio_received
/// 5. Handles session close → Room::remove_participant
class SessionBinder {
public:
    explicit SessionBinder(std::shared_ptr<RoomManager> room_manager);

    /// Create TransportCallbacks that route events through this binder.
    /// Pass the returned callbacks to any TransportServer.
    TransportCallbacks make_callbacks();

    /// Override the per-session `game_*` message limit for sessions bound
    /// after this call. For tests and fuzzing, which send far faster than
    /// any real client.
    void set_game_message_limit(double rate_per_second, double burst) {
        game_message_rate_ = rate_per_second;
        game_message_burst_ = burst;
    }

private:
    /// Called when a new transport session opens (either WebTransport or WebRTC)
    void on_session_open(std::shared_ptr<TransportSession> session);

    /// Called when a reliable message arrives from a session. Never throws.
    void on_message(TransportSession* session, const std::string& message);

    /// on_message body; may throw on malformed input (caught by on_message)
    void handle_message(TransportSession* session, const std::string& message);

    /// Called when a datagram arrives from a session
    void on_datagram(TransportSession* session, const uint8_t* data, size_t len);

    /// Called when a game datagram arrives from a session
    void on_game_datagram(TransportSession* session,
                          const uint8_t* data, size_t len);

    /// Called when a session closes
    void on_session_close(TransportSession* session);

    std::shared_ptr<RoomManager> room_manager_;
    double game_message_rate_ = kGameMessageRate;
    double game_message_burst_ = kGameMessageBurst;

    /// Tracks which session is bound to which room/participant
    struct BoundSession {
        std::string room_name;
        std::string participant_id;
        std::shared_ptr<TransportSession> session; // prevent premature destruction
        /// Limits reliable game_* messages from this session. Shared so
        /// copies of the binding draw on the same bucket.
        std::shared_ptr<TokenBucket> game_limit =
            std::make_shared<TokenBucket>(kGameMessageRate, kGameMessageBurst);
    };

    // session id → binding info
    std::unordered_map<std::string, BoundSession> bindings_;
    std::mutex bindings_mutex_;

    // sessions awaiting a bind message (session id → shared_ptr)
    std::unordered_map<std::string, std::shared_ptr<TransportSession>> pending_;
    std::mutex pending_mutex_;
};

} // namespace tutti
