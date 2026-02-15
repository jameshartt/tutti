#include "session_binder.h"

#include <iostream>
#include <nlohmann/json.hpp>

namespace tutti {

SessionBinder::SessionBinder(std::shared_ptr<RoomManager> room_manager)
    : room_manager_(std::move(room_manager)) {}

TransportCallbacks SessionBinder::make_callbacks() {
    TransportCallbacks cb;
    cb.on_session_open = [this](std::shared_ptr<TransportSession> session) {
        on_session_open(std::move(session));
    };
    cb.on_message = [this](TransportSession* session, const std::string& msg) {
        on_message(session, msg);
    };
    cb.on_datagram = [this](TransportSession* session,
                            const uint8_t* data, size_t len) {
        on_datagram(session, data, len);
    };
    cb.on_session_close = [this](TransportSession* session) {
        on_session_close(session);
    };
    return cb;
}

void SessionBinder::on_session_open(std::shared_ptr<TransportSession> session) {
    std::string sid = session->id();
    std::cout << "[SessionBinder] New session awaiting bind: " << sid << "\n";
    std::lock_guard<std::mutex> lock(pending_mutex_);
    pending_[sid] = std::move(session);
}

void SessionBinder::on_message(TransportSession* session,
                                const std::string& message) {
    std::string sid = session->id();

    // Check if this session is already bound — handle control messages
    {
        std::lock_guard<std::mutex> lock(bindings_mutex_);
        auto it = bindings_.find(sid);
        if (it != bindings_.end()) {
            // Echo ping messages back as pong for RTT measurement
            if (message.find("\"ping\"") != std::string::npos) {
                nlohmann::json msg;
                try {
                    msg = nlohmann::json::parse(message);
                } catch (...) {
                    return;
                }
                if (msg.value("type", "") == "ping") {
                    msg["type"] = "pong";
                    it->second.session->send_reliable(msg.dump());
                }
            }
            return;
        }
    }

    // Not yet bound — this should be a bind message
    nlohmann::json msg;
    try {
        msg = nlohmann::json::parse(message);
    } catch (...) {
        std::cerr << "[SessionBinder] Invalid JSON from " << sid << "\n";
        return;
    }

    std::string type = msg.value("type", "");
    if (type != "bind") {
        std::cerr << "[SessionBinder] Expected bind message, got: " << type
                  << " from " << sid << "\n";
        return;
    }

    std::string participant_id = msg.value("participant_id", "");
    std::string room_name = msg.value("room", "");

    if (participant_id.empty() || room_name.empty()) {
        std::cerr << "[SessionBinder] Bind message missing fields from "
                  << sid << "\n";
        return;
    }

    // Look up the room
    auto room = room_manager_->get_room(room_name);
    if (!room) {
        std::cerr << "[SessionBinder] Room not found: " << room_name << "\n";
        session->send_reliable(R"({"type":"error","error":"room_not_found"})");
        return;
    }

    // Move session from pending to bound
    std::shared_ptr<TransportSession> owned_session;
    {
        std::lock_guard<std::mutex> lock(pending_mutex_);
        auto it = pending_.find(sid);
        if (it != pending_.end()) {
            owned_session = std::move(it->second);
            pending_.erase(it);
        }
    }

    if (!owned_session) {
        std::cerr << "[SessionBinder] Session not found in pending: " << sid << "\n";
        return;
    }

    // Attach session to the participant in the room
    if (!room->attach_session(participant_id, owned_session)) {
        std::cerr << "[SessionBinder] Failed to attach session for participant "
                  << participant_id << " in room " << room_name << "\n";
        session->send_reliable(R"({"type":"error","error":"participant_not_found"})");
        // Put back in pending so session isn't dropped
        std::lock_guard<std::mutex> lock(pending_mutex_);
        pending_[sid] = std::move(owned_session);
        return;
    }

    // Store binding
    {
        std::lock_guard<std::mutex> lock(bindings_mutex_);
        bindings_[sid] = {room_name, participant_id, owned_session};
    }

    std::cout << "[SessionBinder] Bound session " << sid
              << " → room=" << room_name
              << " participant=" << participant_id << "\n";
}

void SessionBinder::on_datagram(TransportSession* session,
                                 const uint8_t* data, size_t len) {
    std::string sid = session->id();

    std::string room_name;
    std::string participant_id;
    {
        std::lock_guard<std::mutex> lock(bindings_mutex_);
        auto it = bindings_.find(sid);
        if (it == bindings_.end()) return; // Not yet bound, drop datagram
        room_name = it->second.room_name;
        participant_id = it->second.participant_id;
    }

    auto room = room_manager_->get_room(room_name);
    if (room) {
        room->on_audio_received(participant_id, data, len);
    }
}

void SessionBinder::on_session_close(TransportSession* session) {
    std::string sid = session->id();

    // Remove from pending if not yet bound
    {
        std::lock_guard<std::mutex> lock(pending_mutex_);
        pending_.erase(sid);
    }

    // Remove binding and clean up participant
    std::string room_name;
    std::string participant_id;
    {
        std::lock_guard<std::mutex> lock(bindings_mutex_);
        auto it = bindings_.find(sid);
        if (it != bindings_.end()) {
            room_name = it->second.room_name;
            participant_id = it->second.participant_id;
            bindings_.erase(it);
        }
    }

    if (!room_name.empty()) {
        std::cout << "[SessionBinder] Session closed: " << sid
                  << " (room=" << room_name
                  << " participant=" << participant_id << ")\n";
        room_manager_->leave_room(room_name, participant_id);
    }
}

} // namespace tutti
