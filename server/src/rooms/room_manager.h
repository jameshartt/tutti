#pragma once

#include <atomic>
#include <chrono>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>

#include "audio/room.h"

namespace tutti {

/// Manages all rooms and handles join/leave/claim/vacate operations.
class RoomManager {
public:
    explicit RoomManager(size_t max_participants_per_room = 4);

    ~RoomManager();

    /// Initialize default rooms from kDefaultRooms
    void initialize_default_rooms();

    /// Start the background reaper thread
    void start_reaper();

    /// Stop the reaper thread
    void stop_reaper();

    /// Get room by name (nullptr if not found)
    std::shared_ptr<Room> get_room(const std::string& name);

    /// Get info for all rooms (for lobby listing)
    struct RoomInfo {
        std::string name;
        size_t participant_count;
        size_t max_participants;
        bool claimed;
    };
    std::vector<RoomInfo> list_rooms() const;

    /// Join a participant to a room
    /// Returns participant ID on success, empty string on failure
    enum class JoinResult {
        Success,
        RoomNotFound,
        RoomFull,
        PasswordRequired,
        PasswordIncorrect
    };
    JoinResult join_room(const std::string& room_name,
                         const std::string& alias,
                         const std::string& password,
                         std::shared_ptr<TransportSession> session,
                         std::string& out_participant_id);

    /// Remove a participant from a room
    void leave_room(const std::string& room_name,
                    const std::string& participant_id);

    /// Claim a room with a password
    bool claim_room(const std::string& room_name,
                    const std::string& password);

    /// Request current occupants to vacate
    enum class VacateResult {
        Sent,
        RoomNotFound,
        RoomEmpty,
        CooldownActive
    };
    VacateResult vacate_request(const std::string& room_name,
                                const std::string& source_ip);

private:
    size_t max_participants_per_room_;
    std::unordered_map<std::string, std::shared_ptr<Room>> rooms_;
    mutable std::mutex rooms_mutex_;

    // Vacate cooldown: source_ip → last request time
    std::unordered_map<std::string, std::chrono::steady_clock::time_point>
        vacate_cooldowns_;
    std::mutex vacate_mutex_;
    static constexpr auto kVacateCooldown = std::chrono::hours(24);

    /// Generate a unique participant ID
    static std::string generate_id();

    // Reaper thread
    std::thread reaper_thread_;
    std::atomic<bool> reaper_running_{false};
    void reaper_thread_func();
};

} // namespace tutti
