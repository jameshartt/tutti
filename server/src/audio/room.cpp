#include "room.h"

#include <chrono>
#include <cmath>
#include <cstring>
#include <iostream>
#include <limits>
#include <nlohmann/json.hpp>

#ifdef __linux__
#include <poll.h>
#include <pthread.h>
#include <sched.h>
#include <sys/eventfd.h>
#include <time.h>
#include <unistd.h>
#endif

namespace tutti {

Room::Room(const std::string& name, size_t max_participants)
    : name_(name),
      max_participants_(max_participants),
      mixer_(max_participants) {
#ifdef __linux__
    notify_fd_ = eventfd(0, EFD_NONBLOCK);
    if (notify_fd_ < 0) {
        std::cerr << "[Room:" << name_ << "] Warning: Could not create eventfd\n";
    }
#endif
}

Room::~Room() {
    stop();
#ifdef __linux__
    if (notify_fd_ >= 0) ::close(notify_fd_);
#endif
}

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

    participants_[id] = {alias, std::move(session), 0,
                         std::chrono::steady_clock::now(), 0, 0};
    mixer_.add_participant(id);
    recount_bound_locked();

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
    recount_bound_locked();

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
    recount_bound_locked();

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

    // Fast path: exactly 2 BOUND participants → direct forwarding (bypass
    // mixer). Count only participants with an attached transport session:
    // ghost records (HTTP join that never bound, lingering up to 120s)
    // must not force everyone onto the mixer path.
    std::shared_ptr<TransportSession> target_session;
    std::string target_id;
    uint32_t output_seq = 0;
    bool use_fast_path = false;

    {
        std::lock_guard<std::mutex> lock(participants_mutex_);
        // Stamp activity for reaper
        auto recv_it = participants_.find(participant_id);
        if (recv_it != participants_.end())
            recv_it->second.last_audio_received_ns = now_ns();
        recount_bound_locked();
        if (bound_count_.load(std::memory_order_relaxed) == 2) {
            for (auto& [pid, p] : participants_) {
                if (pid != participant_id && p.session) {
                    target_id = pid;
                    target_session = p.session;
                    output_seq = p.output_sequence++;
                    p.last_audio_sent_ns = now_ns();
                    use_fast_path = true;
                    break;
                }
            }
        }
    }

    if (use_fast_path) {
        GainEntry ge = mixer_.get_gain_entry(target_id, participant_id);

        if (ge.muted || ge.gain <= 0.0f) return;

        if (ge.gain == 1.0f) {
            // Near-zero-copy: memcpy + overwrite sequence number
            uint8_t buf[kAudioPacketSize];
            std::memcpy(buf, data, kAudioPacketSize);
            std::memcpy(buf, &output_seq, sizeof(output_seq));
            if (target_session)
                target_session->send_datagram(buf, kAudioPacketSize);
        } else {
            // Apply gain, re-serialize
            auto pkt = AudioPacket::deserialize(data, len);
            for (size_t s = 0; s < kSamplesPerFrame; ++s) {
                pkt.samples[s] = static_cast<int16_t>(
                    std::clamp(
                        static_cast<int32_t>(std::lround(pkt.samples[s] * ge.gain)),
                        static_cast<int32_t>(std::numeric_limits<int16_t>::min()),
                        static_cast<int32_t>(std::numeric_limits<int16_t>::max())));
            }
            pkt.sequence = output_seq;
            uint8_t buf[kAudioPacketSize];
            pkt.serialize(buf);
            if (target_session)
                target_session->send_datagram(buf, kAudioPacketSize);
        }
        fast_path_forwards_.fetch_add(1, std::memory_order_relaxed);
        return;
    }

    // 3+ participant path: push to mixer
    auto pkt = AudioPacket::deserialize(data, len);
    auto frame = AudioFrame::from_packet(pkt);
    mixer_.push_input(participant_id, frame);

    // Early-wake hint for the mixer thread once roughly one frame per
    // bound participant has arrived. Purely a latency optimization —
    // the mixer's absolute deadline guarantees cadence even when some
    // participants send nothing (muted mic, mid-join, packet loss).
    uint32_t received = frames_received_.fetch_add(1, std::memory_order_acq_rel) + 1;
    uint32_t senders = bound_count_.load(std::memory_order_relaxed);
    if (senders > 0 && received >= senders) {
#ifdef __linux__
        uint64_t val = 1;
        (void)::write(notify_fd_, &val, sizeof(val));
#endif
    }
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

void Room::broadcast_control(const std::string& msg) {
    std::lock_guard<std::mutex> lock(participants_mutex_);
    for (auto& [pid, p] : participants_) {
        if (p.session) p.session->send_reliable(msg);
    }
}

size_t Room::reap_stale_participants() {
    std::vector<std::string> to_reap;
    auto now = std::chrono::steady_clock::now();
    auto now_nanos = now_ns();
    auto inactivity_ns =
        std::chrono::duration_cast<std::chrono::nanoseconds>(kInactivityTimeout).count();
    auto unbound_ns =
        std::chrono::duration_cast<std::chrono::nanoseconds>(kUnboundTimeout).count();

    {
        std::lock_guard<std::mutex> lock(participants_mutex_);
        size_t count = participants_.size();
        for (auto& [id, p] : participants_) {
            // Unbound: HTTP join with no transport session
            if (!p.session) {
                auto elapsed = now - p.join_time;
                if (elapsed >= kUnboundTimeout) {
                    to_reap.push_back(id);
                }
                continue;
            }

            // Solo participant: skip audio inactivity check
            if (count <= 1) continue;

            // Check audio inactivity (both directions)
            int64_t last_recv = p.last_audio_received_ns;
            int64_t last_sent = p.last_audio_sent_ns;
            int64_t last_activity = std::max(last_recv, last_sent);

            if (last_activity == 0) {
                // Never had audio — fall back to join_time
                auto elapsed = now - p.join_time;
                if (elapsed >= kInactivityTimeout)
                    to_reap.push_back(id);
            } else {
                if ((now_nanos - last_activity) >= inactivity_ns)
                    to_reap.push_back(id);
            }
        }
    }

    for (const auto& id : to_reap) {
        std::cout << "[Room:" << name_ << "] Reaping stale participant: " << id << "\n";
        remove_participant(id);
    }

    return to_reap.size();
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
    constexpr auto mix_interval = std::chrono::nanoseconds(
        1000000000LL * kSamplesPerFrame / kSampleRate);

    auto last_log = std::chrono::steady_clock::now();
    RoomAudioStats last_snapshot{};

#ifdef __linux__
    auto next_deadline = std::chrono::steady_clock::now() + mix_interval;
#endif

    while (running_) {
#ifdef __linux__
        // Idle room: nothing to mix — sleep coarsely, drain stale notifies
        if (mixer_.participant_count() == 0) {
            struct pollfd idle_pfd;
            idle_pfd.fd = notify_fd_;
            idle_pfd.events = POLLIN;
            if (poll(&idle_pfd, 1, 100) > 0 && (idle_pfd.revents & POLLIN)) {
                uint64_t val;
                (void)::read(notify_fd_, &val, sizeof(val));
            }
            next_deadline = std::chrono::steady_clock::now() + mix_interval;
            continue;
        }

        // Event-driven with a hard cadence backstop: wake when all bound
        // participants' frames arrive, but never sleep past the absolute
        // deadline. The mixer must cycle at >= 375Hz (one frame period);
        // a plain poll() timeout can't express 2.667ms (ms granularity,
        // and 3ms ran the mixer 11% slow whenever the event didn't fire —
        // queues overflowed and everyone sounded robotic). ppoll gives ns
        // precision and the absolute deadline prevents drift.
        bool notified = false;
        auto now = std::chrono::steady_clock::now();
        if (next_deadline > now) {
            auto remaining = next_deadline - now;
            struct timespec ts;
            ts.tv_sec = std::chrono::duration_cast<std::chrono::seconds>(remaining).count();
            ts.tv_nsec = std::chrono::duration_cast<std::chrono::nanoseconds>(
                remaining - std::chrono::seconds(ts.tv_sec)).count();
            struct pollfd pfd;
            pfd.fd = notify_fd_;
            pfd.events = POLLIN;
            int ret = ppoll(&pfd, 1, &ts, nullptr);
            if (ret > 0 && (pfd.revents & POLLIN)) {
                uint64_t val;
                (void)::read(notify_fd_, &val, sizeof(val));
                notified = true;
            }
        }
        now = std::chrono::steady_clock::now();
        if (notified) {
            // Arrivals pace us — restart the deadline window from now
            wake_notify_.fetch_add(1, std::memory_order_relaxed);
            next_deadline = now + mix_interval;
        } else {
            wake_deadline_.fetch_add(1, std::memory_order_relaxed);
            next_deadline += mix_interval;
            // Fell badly behind (scheduling stall)? Resync instead of
            // burst-spinning to catch up
            if (next_deadline < now) next_deadline = now + mix_interval;
        }
#else
        std::this_thread::sleep_for(mix_interval);
#endif

        frames_received_.store(0, std::memory_order_release);
        mixer_.mix_cycle();
        send_outputs();
        mix_cycles_.fetch_add(1, std::memory_order_relaxed);

        // Periodic telemetry: log activity deltas so incidents are
        // diagnosable from docker logs after the fact
        auto log_now = std::chrono::steady_clock::now();
        if (log_now - last_log >= std::chrono::seconds(15)) {
            last_log = log_now;
            auto s = audio_stats();
            bool active = s.frames_in != last_snapshot.frames_in ||
                          s.frames_out != last_snapshot.frames_out ||
                          s.fast_path_forwards != last_snapshot.fast_path_forwards;
            if (active) {
                std::cout << "[Room:" << name_ << "] audio stats (15s): "
                          << "cycles=" << (s.mix_cycles - last_snapshot.mix_cycles)
                          << " wake_notify=" << (s.wake_notify - last_snapshot.wake_notify)
                          << " wake_deadline=" << (s.wake_deadline - last_snapshot.wake_deadline)
                          << " in=" << (s.frames_in - last_snapshot.frames_in)
                          << " in_dropped=" << (s.frames_in_dropped - last_snapshot.frames_in_dropped)
                          << " skipped=" << (s.frames_skipped - last_snapshot.frames_skipped)
                          << " out=" << (s.frames_out - last_snapshot.frames_out)
                          << " fast=" << (s.fast_path_forwards - last_snapshot.fast_path_forwards)
                          << " participants=" << s.participants
                          << " bound=" << s.bound_participants << "\n";
            }
            last_snapshot = s;
        }
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
                participant.last_audio_sent_ns = now_ns();
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
        if (ps.session) {
            ps.session->send_datagram(ps.buf, kAudioPacketSize);
            frames_out_.fetch_add(1, std::memory_order_relaxed);
        }
    }
}

void Room::recount_bound_locked() {
    uint32_t bound = 0;
    for (auto& [pid, p] : participants_) {
        if (p.session) ++bound;
    }
    bound_count_.store(bound, std::memory_order_relaxed);
}

RoomAudioStats Room::audio_stats() const {
    RoomAudioStats s;
    s.mix_cycles = mix_cycles_.load(std::memory_order_relaxed);
    s.wake_notify = wake_notify_.load(std::memory_order_relaxed);
    s.wake_deadline = wake_deadline_.load(std::memory_order_relaxed);
    auto mc = mixer_.counters();
    s.frames_in = mc.frames_in;
    s.frames_in_dropped = mc.frames_in_dropped;
    s.frames_skipped = mc.frames_skipped;
    s.frames_out = frames_out_.load(std::memory_order_relaxed);
    s.fast_path_forwards = fast_path_forwards_.load(std::memory_order_relaxed);
    s.participants = participant_count();
    s.bound_participants = bound_count_.load(std::memory_order_relaxed);
    return s;
}

} // namespace tutti
