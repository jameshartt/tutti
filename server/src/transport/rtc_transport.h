#pragma once

#include "transport_interface.h"

#include <atomic>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>

// Forward declarations for libdatachannel
namespace rtc {
class PeerConnection;
class DataChannel;
class WebSocket;
} // namespace rtc

namespace tutti {

/// WebRTC DataChannel session via libdatachannel.
/// Fallback transport for Safari/iOS.
/// Uses unreliable+unordered DataChannel for audio datagrams
/// and a separate reliable DataChannel for control messages.
class RtcSession : public TransportSession {
public:
    RtcSession(const std::string& session_id,
               std::shared_ptr<rtc::PeerConnection> pc,
               std::shared_ptr<rtc::DataChannel> audio_dc,
               std::shared_ptr<rtc::DataChannel> control_dc);
    ~RtcSession() override;

    bool send_datagram(const uint8_t* data, size_t len) override;
    bool send_reliable(const std::string& message) override;
    void close() override;
    std::string id() const override;
    std::string remote_address() const override;
    bool is_connected() const override;

private:
    std::string session_id_;
    std::shared_ptr<rtc::PeerConnection> pc_;
    std::shared_ptr<rtc::DataChannel> audio_dc_;   // unreliable, unordered
    std::shared_ptr<rtc::DataChannel> control_dc_; // reliable, ordered
    std::atomic<bool> connected_{true};
};

/// WebRTC transport server via libdatachannel.
/// Handles SDP signaling over WebSocket and creates peer connections.
class RtcTransportServer : public TransportServer {
public:
    RtcTransportServer();
    ~RtcTransportServer() override;

    bool listen(const std::string& address, uint16_t port) override;
    void stop() override;
    void set_callbacks(TransportCallbacks callbacks) override;

    /// Handle a new WebSocket signaling connection
    void on_signaling_connection(std::shared_ptr<rtc::WebSocket> ws);

private:
    void handle_offer(const std::string& session_id,
                      const std::string& sdp);

    TransportCallbacks callbacks_;
    std::atomic<bool> running_{false};
    std::mutex sessions_mutex_;
    std::unordered_map<std::string, std::shared_ptr<RtcSession>> sessions_;
    std::unordered_map<std::string, std::shared_ptr<rtc::PeerConnection>> pending_pcs_;
};

} // namespace tutti
