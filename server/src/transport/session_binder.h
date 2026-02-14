#pragma once

#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>

#include "transport_interface.h"
#include "rooms/room_manager.h"

namespace tutti {

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

private:
    /// Called when a new transport session opens (either WebTransport or WebRTC)
    void on_session_open(std::shared_ptr<TransportSession> session);

    /// Called when a reliable message arrives from a session
    void on_message(TransportSession* session, const std::string& message);

    /// Called when a datagram arrives from a session
    void on_datagram(TransportSession* session, const uint8_t* data, size_t len);

    /// Called when a session closes
    void on_session_close(TransportSession* session);

    std::shared_ptr<RoomManager> room_manager_;

    /// Tracks which session is bound to which room/participant
    struct BoundSession {
        std::string room_name;
        std::string participant_id;
        std::shared_ptr<TransportSession> session; // prevent premature destruction
    };

    // session id → binding info
    std::unordered_map<std::string, BoundSession> bindings_;
    std::mutex bindings_mutex_;

    // sessions awaiting a bind message (session id → shared_ptr)
    std::unordered_map<std::string, std::shared_ptr<TransportSession>> pending_;
    std::mutex pending_mutex_;
};

} // namespace tutti
