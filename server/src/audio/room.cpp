#include "room.h"

#include <chrono>
#include <cstring>
#include <iostream>
#include <nlohmann/json.hpp>

#ifdef __linux__
#include <pthread.h>
#include <sched.h>
#include <time.h>
#endif

namespace tutti {

Room::Room(const std::string& name, size_t max_participants)
    : name_(name),
      max_participants_(max_participants),
      mixer_(max_participants) {}

Room::~Room() { stop(); }

void Room::start() {
    if (running_) return;
    running_ = true;
    mixer_thread_ = std::thread(&Room::mixer_thread_func, this);
}

void Room::stop() {
    running_ = false;
    if (mixer_thread_.joinable()) {
        mixer_thread_.join();
    }
}

bool Room::add_participant(const std::string& id,
                           const std::string& alias,
                           std::shared_ptr<TransportSession> session) {
    std::lock_guard<std::mutex> lock(participants_mutex_);
    if (participants_.size() >= max_participants_) return false;
    if (participants_.count(id)) return false;

    participants_[id] = {alias, std::move(session), 0};
    mixer_.add_participant(id);

    // Notify existing participants
    nlohmann::json msg = {
        {"type", "participant_joined"},
        {"id", id},
        {"name", alias}
    };
    std::string msg_str = msg.dump();
    for (auto& [pid, p] : participants_) {
        if (pid != id && p.session) {
            p.session->send_reliable(msg_str);
        }
    }

    // Send room state to new participant
    auto& new_session = participants_[id].session;
    if (new_session) {
        nlohmann::json state_msg;
        state_msg["type"] = "room_state";
        state_msg["participants"] = nlohmann::json::array();
        for (auto& [pid, p] : participants_) {
            state_msg["participants"].push_back({
                {"id", pid},
                {"name", p.alias}
            });
        }
        new_session->send_reliable(state_msg.dump());
    }

    return true;
}

bool Room::attach_session(const std::string& id,
                           std::shared_ptr<TransportSession> session) {
    std::lock_guard<std::mutex> lock(participants_mutex_);
    auto it = participants_.find(id);
    if (it == participants_.end()) return false;

    it->second.session = std::move(session);

    // Send room state to the newly-bound participant
    if (it->second.session) {
        nlohmann::json state_msg;
        state_msg["type"] = "room_state";
        state_msg["participants"] = nlohmann::json::array();
        for (auto& [pid, p] : participants_) {
            state_msg["participants"].push_back({
                {"id", pid},
                {"name", p.alias}
            });
        }
        it->second.session->send_reliable(state_msg.dump());
    }

    return true;
}

void Room::remove_participant(const std::string& id) {
    std::lock_guard<std::mutex> lock(participants_mutex_);
    participants_.erase(id);
    mixer_.remove_participant(id);

    // Notify remaining participants
    nlohmann::json msg = {
        {"type", "participant_left"},
        {"id", id}
    };
    std::string msg_str = msg.dump();
    for (auto& [pid, p] : participants_) {
        if (p.session) p.session->send_reliable(msg_str);
    }

    // Clear password if room is now empty
    if (participants_.empty()) {
        clear_password();
    }
}

void Room::on_audio_received(const std::string& participant_id,
                              const uint8_t* data, size_t len) {
    if (len < kAudioPacketSize) return;

    auto pkt = AudioPacket::deserialize(data, len);
    auto frame = AudioFrame::from_packet(pkt);
    mixer_.push_input(participant_id, frame);
}

void Room::set_gain(const std::string& listener_id,
                    const std::string& source_id,
                    float gain) {
    mixer_.set_gain(listener_id, source_id, gain);
}

void Room::set_mute(const std::string& listener_id,
                    const std::string& source_id,
                    bool muted) {
    mixer_.set_mute(listener_id, source_id, muted);
}

bool Room::claim(const std::string& password) {
    std::lock_guard<std::mutex> lock(password_mutex_);
    password_ = password;
    return true;
}

bool Room::check_password(const std::string& password) const {
    std::lock_guard<std::mutex> lock(password_mutex_);
    if (password_.empty()) return true; // No password set
    return password_ == password;
}

void Room::clear_password() {
    std::lock_guard<std::mutex> lock(password_mutex_);
    password_.clear();
}

size_t Room::participant_count() const {
    std::lock_guard<std::mutex> lock(participants_mutex_);
    return participants_.size();
}

RoomStatus Room::status() const {
    if (is_full()) return RoomStatus::Full;
    std::lock_guard<std::mutex> lock(password_mutex_);
    return password_.empty() ? RoomStatus::Open : RoomStatus::Claimed;
}

std::vector<Room::ParticipantInfo> Room::get_participants() const {
    std::lock_guard<std::mutex> lock(participants_mutex_);
    std::vector<ParticipantInfo> result;
    result.reserve(participants_.size());
    for (const auto& [id, p] : participants_) {
        result.push_back({id, p.alias});
    }
    return result;
}

void Room::mixer_thread_func() {
#ifdef __linux__
    // Set RT priority for mixer thread
    struct sched_param param;
    param.sched_priority = sched_get_priority_max(SCHED_FIFO);
    if (pthread_setschedparam(pthread_self(), SCHED_FIFO, &param) != 0) {
        std::cerr << "[Room:" << name_ << "] Warning: Could not set RT priority\n";
    }

    // Pin to a core (optional, improves cache locality)
    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    CPU_SET(1, &cpuset); // Pin to core 1
    pthread_setaffinity_np(pthread_self(), sizeof(cpu_set_t), &cpuset);
#endif

    // Mix cycle interval: match one render quantum (~2.67ms at 48kHz/128 samples)
    constexpr auto mix_interval =
        std::chrono::microseconds(1000000 * kSamplesPerFrame / kSampleRate);

    auto next_tick = std::chrono::steady_clock::now() + mix_interval;

    while (running_) {
        mixer_.mix_cycle();
        send_outputs();

#ifdef __linux__
        // High-resolution absolute-time sleep (~50μs precision vs 1-4ms for sleep_for)
        auto ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
            next_tick.time_since_epoch()).count();
        struct timespec ts;
        ts.tv_sec = ns / 1000000000;
        ts.tv_nsec = ns % 1000000000;
        clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &ts, nullptr);
#else
        std::this_thread::sleep_until(next_tick);
#endif

        next_tick += mix_interval;
    }
}

void Room::send_outputs() {
    // Collect outputs under lock, then send outside to avoid holding
    // the mutex during network I/O (which contends with receive thread)
    pending_sends_.clear();
    {
        std::lock_guard<std::mutex> lock(participants_mutex_);
        for (auto& [id, participant] : participants_) {
            AudioFrame frame;
            if (mixer_.pop_output(id, frame)) {
                frame.sequence = participant.output_sequence++;
                PendingSend ps;
                ps.session = participant.session;
                auto pkt = frame.to_packet();
                pkt.serialize(ps.buf);
                pending_sends_.push_back(std::move(ps));
            }
        }
    }

    // Send outside the lock
    for (auto& ps : pending_sends_) {
        if (ps.session)
            ps.session->send_datagram(ps.buf, kAudioPacketSize);
    }
}

} // namespace tutti
