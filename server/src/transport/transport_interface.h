#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>

#include "game_datagram.h"

namespace tutti {

// Audio packet header: 4-byte sequence + 4-byte timestamp
static constexpr size_t kAudioHeaderSize = 8;
// 128 samples * 2 bytes per sample (int16)
static constexpr size_t kAudioPayloadSize = 256;
// Total packet size
static constexpr size_t kAudioPacketSize = kAudioHeaderSize + kAudioPayloadSize;
// Samples per frame (matches AudioWorklet quantum)
static constexpr size_t kSamplesPerFrame = 128;
// Sample rate
static constexpr uint32_t kSampleRate = 48000;

/// Represents a single audio datagram
struct AudioPacket {
    uint32_t sequence;
    uint32_t timestamp;
    int16_t samples[kSamplesPerFrame];

    /// Serialize to wire format (little-endian)
    void serialize(uint8_t* buf) const;

    /// Deserialize from wire format (little-endian)
    static AudioPacket deserialize(const uint8_t* buf, size_t len);
};

/// Abstract transport session for a single connected participant.
/// Implemented by WebTransport (wt_transport) and WebRTC (rtc_transport).
class TransportSession {
public:
    virtual ~TransportSession() = default;

    /// Send an unreliable audio datagram to this participant
    virtual bool send_datagram(const uint8_t* data, size_t len) = 0;

    /// Send an unreliable game datagram: [kind u8][payload...]. The
    /// transport adds whatever framing it needs to keep it apart from
    /// audio. Default: transport has no game channel.
    virtual bool send_game_datagram(const uint8_t* /*data*/, size_t /*len*/) {
        return false;
    }

    /// Send a reliable control message (JSON string)
    virtual bool send_reliable(const std::string& message) = 0;

    /// Close this session
    virtual void close() = 0;

    /// Get the session/participant ID
    virtual std::string id() const = 0;

    /// Get the remote address (for rate limiting, logging)
    virtual std::string remote_address() const = 0;

    /// Check if session is still connected
    virtual bool is_connected() const = 0;
};

/// Callbacks for transport events
struct TransportCallbacks {
    /// Called when an unreliable datagram is received
    std::function<void(TransportSession*, const uint8_t*, size_t)> on_datagram;

    /// Called when an unreliable game datagram is received. The data is
    /// [kind u8][payload...] with any transport framing already removed.
    std::function<void(TransportSession*, const uint8_t*, size_t)> on_game_datagram;

    /// Called when a reliable message is received
    std::function<void(TransportSession*, const std::string&)> on_message;

    /// Called when a session is established
    std::function<void(std::shared_ptr<TransportSession>)> on_session_open;

    /// Called when a session is closed
    std::function<void(TransportSession*)> on_session_close;
};

/// Abstract transport server - listens for incoming connections
class TransportServer {
public:
    virtual ~TransportServer() = default;

    /// Start listening on the given address and port
    virtual bool listen(const std::string& address, uint16_t port) = 0;

    /// Stop the server
    virtual void stop() = 0;

    /// Set callbacks for transport events
    virtual void set_callbacks(TransportCallbacks callbacks) = 0;
};

/// Demultiplex one datagram from a transport that carries audio and game
/// traffic on the same unreliable channel (WebTransport). Game datagrams
/// have their magic stripped and go to on_game_datagram only; everything
/// else goes to on_datagram only. Oversized game datagrams are dropped.
inline void dispatch_datagram(const TransportCallbacks& callbacks,
                              TransportSession* session,
                              const uint8_t* data, size_t len) {
    if (is_game_datagram(data, len)) {
        size_t body = len - kGameMagicSize;
        if (body <= kMaxGameDatagramSize && callbacks.on_game_datagram) {
            callbacks.on_game_datagram(session, data + kGameMagicSize, body);
        }
    } else if (callbacks.on_datagram) {
        callbacks.on_datagram(session, data, len);
    }
}

} // namespace tutti
