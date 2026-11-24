#include "game_session.h"

#include <iostream>
#include <nlohmann/json.hpp>

namespace tutti {

GameSession::GameSession(std::string room_name, Outbound out)
    : room_name_(std::move(room_name)), out_(std::move(out)) {}

void GameSession::on_message(const std::string& participant_id,
                             const std::string& type,
                             const nlohmann::json& msg) {
    if (type == "game_ping") {
        // Diagnostic: relay to everyone in the room, stamped with the sender
        nlohmann::json out = msg;
        out["from"] = participant_id;
        if (out_.broadcast) out_.broadcast(out.dump());
        return;
    }

    std::cout << "[Game:" << room_name_ << "] Ignoring unknown message type '"
              << type << "' from " << participant_id << "\n";
}

void GameSession::on_datagram(const std::string& participant_id,
                              const uint8_t* data, size_t len) {
    if (len == 0) return;
    datagrams_received_.fetch_add(1, std::memory_order_relaxed);

    switch (static_cast<GameDatagramKind>(data[0])) {
        case GameDatagramKind::Echo:
            if (out_.send_datagram) out_.send_datagram(participant_id, data, len);
            break;
        default:
            break; // Unknown kinds are dropped silently (unreliable channel)
    }
}

} // namespace tutti
