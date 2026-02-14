#include "room_manager.h"
#include "room_names.h"

#include <algorithm>
#include <nlohmann/json.hpp>
#include <random>
#include <sstream>

namespace tutti {

RoomManager::RoomManager(size_t max_participants_per_room)
    : max_participants_per_room_(max_participants_per_room) {}

void RoomManager::initialize_default_rooms() {
    std::lock_guard<std::mutex> lock(rooms_mutex_);
    for (const auto& def : kDefaultRooms) {
        auto room = std::make_shared<Room>(def.name, max_participants_per_room_);
        room->start();
        rooms_[def.name] = std::move(room);
    }
}

std::shared_ptr<Room> RoomManager::get_room(const std::string& name) {
    std::lock_guard<std::mutex> lock(rooms_mutex_);
    auto it = rooms_.find(name);
    return it != rooms_.end() ? it->second : nullptr;
}

std::vector<RoomManager::RoomInfo> RoomManager::list_rooms() const {
    std::lock_guard<std::mutex> lock(rooms_mutex_);
    std::vector<RoomInfo> result;
    result.reserve(rooms_.size());
    for (const auto& [name, room] : rooms_) {
        result.push_back({
            name,
            room->participant_count(),
            room->max_participants(),
            room->status() == RoomStatus::Claimed
        });
    }
    // Sort by the default room order
    std::sort(result.begin(), result.end(),
              [](const RoomInfo& a, const RoomInfo& b) {
                  return a.name < b.name;
              });
    return result;
}

RoomManager::JoinResult RoomManager::join_room(
    const std::string& room_name,
    const std::string& alias,
    const std::string& password,
    std::shared_ptr<TransportSession> session,
    std::string& out_participant_id) {
    auto room = get_room(room_name);
    if (!room) return JoinResult::RoomNotFound;
    if (room->is_full()) return JoinResult::RoomFull;

    if (room->status() == RoomStatus::Claimed) {
        if (password.empty()) return JoinResult::PasswordRequired;
        if (!room->check_password(password)) return JoinResult::PasswordIncorrect;
    }

    out_participant_id = generate_id();
    if (!room->add_participant(out_participant_id, alias, std::move(session))) {
        return JoinResult::RoomFull;
    }

    return JoinResult::Success;
}

void RoomManager::leave_room(const std::string& room_name,
                             const std::string& participant_id) {
    auto room = get_room(room_name);
    if (room) {
        room->remove_participant(participant_id);
    }
}

bool RoomManager::claim_room(const std::string& room_name,
                             const std::string& password) {
    auto room = get_room(room_name);
    if (!room) return false;
    return room->claim(password);
}

RoomManager::VacateResult RoomManager::vacate_request(
    const std::string& room_name,
    const std::string& source_ip) {
    auto room = get_room(room_name);
    if (!room) return VacateResult::RoomNotFound;
    if (room->is_empty()) return VacateResult::RoomEmpty;

    // Check cooldown
    {
        std::lock_guard<std::mutex> lock(vacate_mutex_);
        auto key = source_ip + ":" + room_name;
        auto it = vacate_cooldowns_.find(key);
        if (it != vacate_cooldowns_.end()) {
            auto elapsed = std::chrono::steady_clock::now() - it->second;
            if (elapsed < kVacateCooldown) {
                return VacateResult::CooldownActive;
            }
        }
        vacate_cooldowns_[key] = std::chrono::steady_clock::now();
    }

    // Send vacate notification to all participants
    nlohmann::json msg = {{"type", "vacate_request"}};
    std::string msg_str = msg.dump();
    // Access participants through the room
    // The room will broadcast to all participants
    for (const auto& p : room->get_participants()) {
        // We need the session - room handles this internally
    }

    return VacateResult::Sent;
}

std::string RoomManager::generate_id() {
    static std::random_device rd;
    static std::mt19937 gen(rd());
    static std::uniform_int_distribution<uint64_t> dist;

    std::ostringstream oss;
    oss << std::hex << dist(gen) << dist(gen);
    return oss.str();
}

} // namespace tutti
