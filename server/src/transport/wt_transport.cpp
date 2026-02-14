#include "wt_transport.h"

#include <cstring>
#include <iostream>

namespace tutti {

// ── AudioPacket serialization ───────────────────────────────────────────────

void AudioPacket::serialize(uint8_t* buf) const {
    // Little-endian uint32 writes
    std::memcpy(buf, &sequence, sizeof(uint32_t));
    std::memcpy(buf + 4, &timestamp, sizeof(uint32_t));
    std::memcpy(buf + kAudioHeaderSize, samples, kAudioPayloadSize);
}

AudioPacket AudioPacket::deserialize(const uint8_t* buf, size_t len) {
    AudioPacket pkt{};
    if (len < kAudioPacketSize) {
        // Short packet - zero-fill
        return pkt;
    }
    std::memcpy(&pkt.sequence, buf, sizeof(uint32_t));
    std::memcpy(&pkt.timestamp, buf + 4, sizeof(uint32_t));
    std::memcpy(pkt.samples, buf + kAudioHeaderSize, kAudioPayloadSize);
    return pkt;
}

// ── WtSession ───────────────────────────────────────────────────────────────

WtSession::WtSession(const std::string& session_id,
                     const std::string& remote_addr)
    : session_id_(session_id), remote_addr_(remote_addr) {}

WtSession::~WtSession() { close(); }

bool WtSession::send_datagram(const uint8_t* data, size_t len) {
    if (!connected_) return false;
    // TODO: libwtf datagram send
    // wtf_session_send_datagram(wt_session_, data, len);
    (void)data;
    (void)len;
    return true;
}

bool WtSession::send_reliable(const std::string& message) {
    if (!connected_) return false;
    // TODO: libwtf bidirectional stream send
    // wtf_stream_write(control_stream_, message.data(), message.size());
    (void)message;
    return true;
}

void WtSession::close() {
    connected_ = false;
    // TODO: libwtf session close
}

std::string WtSession::id() const { return session_id_; }
std::string WtSession::remote_address() const { return remote_addr_; }
bool WtSession::is_connected() const { return connected_; }

// ── WtTransportServer ───────────────────────────────────────────────────────

WtTransportServer::WtTransportServer() = default;
WtTransportServer::~WtTransportServer() { stop(); }

bool WtTransportServer::listen(const std::string& address, uint16_t port) {
    // TODO: Initialize msquic + libwtf
    // 1. Create msquic registration
    // 2. Configure TLS with OpenSSL cert
    // 3. Create libwtf server on address:port
    // 4. Set up datagram + stream callbacks
    std::cout << "[WebTransport] Server stub listening on "
              << address << ":" << port << "\n";
    running_ = true;
    return true;
}

void WtTransportServer::stop() {
    if (!running_) return;
    running_ = false;
    std::lock_guard<std::mutex> lock(sessions_mutex_);
    for (auto& [id, session] : sessions_) {
        session->close();
    }
    sessions_.clear();
    // TODO: shutdown libwtf/msquic
}

void WtTransportServer::set_callbacks(TransportCallbacks callbacks) {
    callbacks_ = std::move(callbacks);
}

} // namespace tutti
