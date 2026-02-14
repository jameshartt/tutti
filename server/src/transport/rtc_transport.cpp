#include "rtc_transport.h"

#include <iostream>
#include <rtc/rtc.hpp>

namespace tutti {

// ── RtcSession ──────────────────────────────────────────────────────────────

RtcSession::RtcSession(const std::string& session_id,
                       std::shared_ptr<rtc::PeerConnection> pc,
                       std::shared_ptr<rtc::DataChannel> audio_dc,
                       std::shared_ptr<rtc::DataChannel> control_dc)
    : session_id_(session_id),
      pc_(std::move(pc)),
      audio_dc_(std::move(audio_dc)),
      control_dc_(std::move(control_dc)) {}

RtcSession::~RtcSession() { close(); }

bool RtcSession::send_datagram(const uint8_t* data, size_t len) {
    if (!connected_ || !audio_dc_ || !audio_dc_->isOpen()) return false;
    try {
        audio_dc_->send(reinterpret_cast<const std::byte*>(data), len);
        return true;
    } catch (const std::exception& e) {
        std::cerr << "[RTC] send_datagram error: " << e.what() << "\n";
        return false;
    }
}

bool RtcSession::send_reliable(const std::string& message) {
    if (!connected_ || !control_dc_ || !control_dc_->isOpen()) return false;
    try {
        control_dc_->send(message);
        return true;
    } catch (const std::exception& e) {
        std::cerr << "[RTC] send_reliable error: " << e.what() << "\n";
        return false;
    }
}

void RtcSession::close() {
    connected_ = false;
    if (audio_dc_) audio_dc_->close();
    if (control_dc_) control_dc_->close();
    if (pc_) pc_->close();
}

std::string RtcSession::id() const { return session_id_; }

std::string RtcSession::remote_address() const {
    // libdatachannel doesn't directly expose remote IP
    // In production, extract from signaling WebSocket
    return "unknown";
}

bool RtcSession::is_connected() const {
    return connected_ && pc_ && pc_->state() == rtc::PeerConnection::State::Connected;
}

// ── RtcTransportServer ──────────────────────────────────────────────────────

RtcTransportServer::RtcTransportServer() = default;
RtcTransportServer::~RtcTransportServer() { stop(); }

bool RtcTransportServer::listen(const std::string& address, uint16_t port) {
    // WebRTC doesn't listen on a port directly - connections are established
    // via SDP exchange over WebSocket signaling. The signaling WebSocket
    // server is handled by ws_signaling.cpp
    std::cout << "[WebRTC] Transport ready (signaling via WebSocket on "
              << address << ":" << port << ")\n";
    running_ = true;
    return true;
}

void RtcTransportServer::stop() {
    if (!running_) return;
    running_ = false;
    std::lock_guard<std::mutex> lock(sessions_mutex_);
    for (auto& [id, session] : sessions_) {
        session->close();
    }
    sessions_.clear();
    pending_pcs_.clear();
}

void RtcTransportServer::set_callbacks(TransportCallbacks callbacks) {
    callbacks_ = std::move(callbacks);
}

void RtcTransportServer::handle_offer(const std::string& session_id,
                                       const std::string& sdp) {
    rtc::Configuration config;
    // No STUN/TURN needed for same-city connections on domestic broadband
    // Add STUN as fallback if direct connectivity fails
    config.iceServers.push_back({"stun:stun.l.google.com:19302"});

    auto pc = std::make_shared<rtc::PeerConnection>(config);

    pc->onStateChange([this, session_id](rtc::PeerConnection::State state) {
        if (state == rtc::PeerConnection::State::Disconnected ||
            state == rtc::PeerConnection::State::Failed ||
            state == rtc::PeerConnection::State::Closed) {
            std::lock_guard<std::mutex> lock(sessions_mutex_);
            auto it = sessions_.find(session_id);
            if (it != sessions_.end()) {
                if (callbacks_.on_session_close) {
                    callbacks_.on_session_close(it->second.get());
                }
                sessions_.erase(it);
            }
        }
    });

    // Create audio data channel (unreliable, unordered)
    rtc::DataChannelInit audio_init;
    audio_init.reliability.unordered = true;
    audio_init.reliability.maxRetransmits = 0;
    auto audio_dc = pc->createDataChannel("audio", audio_init);

    // Create control data channel (reliable, ordered)
    auto control_dc = pc->createDataChannel("control");

    auto session = std::make_shared<RtcSession>(
        session_id, pc, audio_dc, control_dc);

    audio_dc->onMessage([this, raw_session = session.get()](auto data) {
        if (auto* binary = std::get_if<rtc::binary>(&data)) {
            if (callbacks_.on_datagram) {
                callbacks_.on_datagram(
                    raw_session,
                    reinterpret_cast<const uint8_t*>(binary->data()),
                    binary->size());
            }
        }
    });

    control_dc->onMessage([this, raw_session = session.get()](auto data) {
        if (auto* text = std::get_if<std::string>(&data)) {
            if (callbacks_.on_message) {
                callbacks_.on_message(raw_session, *text);
            }
        }
    });

    audio_dc->onOpen([this, session]() {
        std::lock_guard<std::mutex> lock(sessions_mutex_);
        sessions_[session->id()] = session;
        if (callbacks_.on_session_open) {
            callbacks_.on_session_open(session);
        }
    });

    // Set remote description (the offer) and generate answer
    pc->setRemoteDescription({sdp, rtc::Description::Type::Offer});

    {
        std::lock_guard<std::mutex> lock(sessions_mutex_);
        pending_pcs_[session_id] = pc;
    }
}

} // namespace tutti
