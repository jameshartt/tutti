#pragma once

#include <atomic>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>

#include "mixer.h"
#include "transport/transport_interface.h"

namespace tutti {

/// Room state
enum class RoomStatus {
    Open,    // No password, anyone can join
    Claimed, // Has a password
    Full     // At max capacity
};

/// A single rehearsal room with its own mixer and RT thread.
class Room {
public:
    explicit Room(const std::string& name, size_t max_participants = 4);
    ~Room();

    // Non-copyable, non-movable (owns a thread)
    Room(const Room&) = delete;
    Room& operator=(const Room&) = delete;

    /// Start the mixer RT thread
    void start();

    /// Stop the mixer RT thread
    void stop();

    /// Add a participant to the room
    bool add_participant(const std::string& id,
                        const std::string& alias,
                        std::shared_ptr<TransportSession> session);

    /// Attach a transport session to an existing participant (called after bind)
    bool attach_session(const std::string& id,
                        std::shared_ptr<TransportSession> session);

    /// Remove a participant from the room
    void remove_participant(const std::string& id);

    /// Handle incoming audio datagram from a participant
    void on_audio_received(const std::string& participant_id,
                           const uint8_t* data, size_t len);

    /// Set gain for a participant's mix
    void set_gain(const std::string& listener_id,
                  const std::string& source_id,
                  float gain);

    /// Set mute state
    void set_mute(const std::string& listener_id,
                  const std::string& source_id,
                  bool muted);

    /// Claim the room with a password
    bool claim(const std::string& password);

    /// Check password
    bool check_password(const std::string& password) const;

    /// Clear password (when room empties)
    void clear_password();

    // Accessors
    const std::string& name() const { return name_; }
    size_t participant_count() const;
    size_t max_participants() const { return max_participants_; }
    RoomStatus status() const;
    bool is_empty() const { return participant_count() == 0; }
    bool is_full() const { return participant_count() >= max_participants_; }

    /// Get participant info for room state messages
    struct ParticipantInfo {
        std::string id;
        std::string alias;
    };
    std::vector<ParticipantInfo> get_participants() const;

private:
    /// RT mixer thread function
    void mixer_thread_func();

    /// Send mixed output to all participants
    void send_outputs();

    std::string name_;
    size_t max_participants_;
    Mixer mixer_;

    // Participant sessions and aliases
    struct Participant {
        std::string alias;
        std::shared_ptr<TransportSession> session;
        uint32_t output_sequence = 0;
    };
    std::unordered_map<std::string, Participant> participants_;
    mutable std::mutex participants_mutex_;

    // Password for claimed rooms
    std::string password_;
    mutable std::mutex password_mutex_;

    // Pre-allocated buffer for send_outputs() to avoid RT allocation
    struct PendingSend {
        std::shared_ptr<TransportSession> session;
        uint8_t buf[kAudioPacketSize];
    };
    std::vector<PendingSend> pending_sends_;

    // RT mixer thread
    std::thread mixer_thread_;
    std::atomic<bool> running_{false};

    // Event-driven mixer: fires when all participants submit a frame
    int notify_fd_ = -1;  // Linux eventfd, -1 on other platforms
    std::atomic<uint32_t> frames_received_{0};
};

} // namespace tutti
