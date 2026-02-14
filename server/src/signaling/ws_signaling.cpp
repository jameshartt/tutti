#include "ws_signaling.h"

#include <iostream>
#include <nlohmann/json.hpp>
#include <random>
#include <sstream>

namespace tutti {

WsSignaling::WsSignaling() = default;
WsSignaling::~WsSignaling() { stop(); }

void WsSignaling::set_on_session_ready(OnSessionReady callback) {
    on_session_ready_ = std::move(callback);
}

bool WsSignaling::listen(const std::string& address, uint16_t port) {
    try {
        rtc::WebSocketServerConfiguration config;
        config.port = port;
        config.bindAddress = address;

        ws_server_ = std::make_unique<rtc::WebSocketServer>(config);

        ws_server_->onClient([this](std::shared_ptr<rtc::WebSocket> ws) {
            on_ws_open(ws);
        });

        running_ = true;
        std::cout << "[WS Signaling] Listening on " << address << ":" << port << "\n";
        return true;
    } catch (const std::exception& e) {
        std::cerr << "[WS Signaling] Failed to start: " << e.what() << "\n";
        return false;
    }
}

void WsSignaling::stop() {
    running_ = false;
    ws_server_.reset();
    std::lock_guard<std::mutex> lock(pending_mutex_);
    pending_.clear();
}

void WsSignaling::on_ws_open(std::shared_ptr<rtc::WebSocket> ws) {
    // Generate a session ID for this signaling connection
    static std::random_device rd;
    static std::mt19937 gen(rd());
    static std::uniform_int_distribution<uint64_t> dist;

    std::ostringstream oss;
    oss << std::hex << dist(gen);
    std::string session_id = oss.str();

    {
        std::lock_guard<std::mutex> lock(pending_mutex_);
        pending_[session_id].ws = ws;
    }

    ws->onMessage([this, session_id](auto data) {
        if (auto* text = std::get_if<std::string>(&data)) {
            on_signaling_message(session_id, *text);
        }
    });

    ws->onClosed([this, session_id]() {
        std::lock_guard<std::mutex> lock(pending_mutex_);
        pending_.erase(session_id);
    });

    // Send session ID to client
    nlohmann::json hello = {
        {"type", "session_id"},
        {"session_id", session_id}
    };
    ws->send(hello.dump());
}

void WsSignaling::on_signaling_message(const std::string& session_id,
                                        const std::string& message) {
    nlohmann::json msg;
    try {
        msg = nlohmann::json::parse(message);
    } catch (...) {
        std::cerr << "[WS Signaling] Invalid JSON from " << session_id << "\n";
        return;
    }

    std::string type = msg.value("type", "");

    if (type == "offer") {
        std::string sdp = msg.value("sdp", "");
        if (!sdp.empty()) {
            create_peer_connection(session_id, sdp);
        }
    } else if (type == "ice_candidate") {
        std::string candidate = msg.value("candidate", "");
        std::string mid = msg.value("mid", "");
        std::lock_guard<std::mutex> lock(pending_mutex_);
        auto it = pending_.find(session_id);
        if (it != pending_.end() && it->second.pc) {
            it->second.pc->addRemoteCandidate({candidate, mid});
        }
    }
}

void WsSignaling::create_peer_connection(const std::string& session_id,
                                          const std::string& offer_sdp) {
    rtc::Configuration config;
    config.iceServers.push_back({"stun:stun.l.google.com:19302"});

    auto pc = std::make_shared<rtc::PeerConnection>(config);

    // Handle ICE candidates to send back to client
    pc->onLocalCandidate([this, session_id](rtc::Candidate candidate) {
        std::lock_guard<std::mutex> lock(pending_mutex_);
        auto it = pending_.find(session_id);
        if (it != pending_.end() && it->second.ws) {
            nlohmann::json msg = {
                {"type", "ice_candidate"},
                {"candidate", std::string(candidate)},
                {"mid", candidate.mid()}
            };
            it->second.ws->send(msg.dump());
        }
    });

    pc->onLocalDescription([this, session_id](rtc::Description desc) {
        std::lock_guard<std::mutex> lock(pending_mutex_);
        auto it = pending_.find(session_id);
        if (it != pending_.end() && it->second.ws) {
            nlohmann::json msg = {
                {"type", "answer"},
                {"sdp", std::string(desc)}
            };
            it->second.ws->send(msg.dump());
        }
    });

    pc->onDataChannel([this, session_id](std::shared_ptr<rtc::DataChannel> dc) {
        std::lock_guard<std::mutex> lock(pending_mutex_);
        auto it = pending_.find(session_id);
        if (it == pending_.end()) return;

        std::string label = dc->label();
        if (label == "audio") {
            it->second.audio_dc = dc;
        } else if (label == "control") {
            it->second.control_dc = dc;
        }

        // Check if both channels are ready
        if (it->second.audio_dc && it->second.control_dc &&
            on_session_ready_) {
            on_session_ready_(session_id, it->second.pc,
                            it->second.audio_dc, it->second.control_dc);
        }
    });

    {
        std::lock_guard<std::mutex> lock(pending_mutex_);
        pending_[session_id].pc = pc;
    }

    // Set remote description (offer) - this triggers answer generation
    pc->setRemoteDescription({offer_sdp, rtc::Description::Type::Offer});
}

} // namespace tutti
