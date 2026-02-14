#pragma once

#include <atomic>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>

#include <rtc/rtc.hpp>

namespace tutti {

/// WebSocket signaling server for WebRTC SDP exchange.
/// Clients that use the WebRTC fallback (Safari/iOS) connect here
/// to exchange SDP offers/answers and ICE candidates.
class WsSignaling {
public:
    /// Callback when a peer connection is ready (SDP exchanged, DataChannels open)
    using OnSessionReady = std::function<void(
        const std::string& session_id,
        std::shared_ptr<rtc::PeerConnection> pc,
        std::shared_ptr<rtc::DataChannel> audio_dc,
        std::shared_ptr<rtc::DataChannel> control_dc)>;

    WsSignaling();
    ~WsSignaling();

    /// Set callback for when a session is fully established
    void set_on_session_ready(OnSessionReady callback);

    /// Start the WebSocket signaling server
    bool listen(const std::string& address, uint16_t port);

    /// Stop the server
    void stop();

private:
    /// Handle a new WebSocket connection
    void on_ws_open(std::shared_ptr<rtc::WebSocket> ws);

    /// Handle incoming signaling message
    void on_signaling_message(const std::string& session_id,
                               const std::string& message);

    /// Create a peer connection for the given session
    void create_peer_connection(const std::string& session_id,
                                 const std::string& offer_sdp);

    OnSessionReady on_session_ready_;
    std::atomic<bool> running_{false};

    struct PendingSession {
        std::shared_ptr<rtc::PeerConnection> pc;
        std::shared_ptr<rtc::WebSocket> ws;
        std::shared_ptr<rtc::DataChannel> audio_dc;
        std::shared_ptr<rtc::DataChannel> control_dc;
    };

    std::unordered_map<std::string, PendingSession> pending_;
    std::mutex pending_mutex_;

    // libdatachannel WebSocket server
    std::unique_ptr<rtc::WebSocketServer> ws_server_;
};

} // namespace tutti
