#pragma once

#include <array>
#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include "ring_buffer.h"
#include "transport/transport_interface.h"

namespace tutti {

/// Per-participant mix state.
/// Not copyable/movable because AudioRingBuffer wraps rigtorp::SPSCQueue.
/// Stored via shared_ptr so the mixer thread can snapshot pointers without
/// holding the participants mutex during queue operations.
struct ParticipantMixState {
    std::string id;
    float gain = 1.0f;           // 0.0 - 1.0
    bool muted = false;
    AudioRingBuffer input_queue;  // Network → Mixer
    AudioRingBuffer output_queue; // Mixer → Network

    explicit ParticipantMixState(const std::string& participant_id)
        : id(participant_id) {}

    ParticipantMixState(const ParticipantMixState&) = delete;
    ParticipantMixState& operator=(const ParticipantMixState&) = delete;
};

/// Per-user gain setting: how loud participant B is in participant A's mix
struct GainEntry {
    float gain = 1.0f;
    bool muted = false;
};

/// Audio mixer for a single room.
/// Produces a custom mix for each participant (sum of all others * their gain).
/// Designed to run on a dedicated RT-priority thread.
///
/// All methods called from the mixer thread must be lock-free / wait-free.
/// Participant add/remove uses a separate mutex (not on the audio path).
class Mixer {
public:
    explicit Mixer(size_t max_participants = 8);

    /// Add a participant. NOT called from RT thread.
    void add_participant(const std::string& id);

    /// Remove a participant. NOT called from RT thread.
    void remove_participant(const std::string& id);

    /// Set gain for how loud `source_id` sounds in `listener_id`'s mix.
    /// Can be called from any thread (atomic float write).
    void set_gain(const std::string& listener_id,
                  const std::string& source_id,
                  float gain);

    /// Set mute state for `source_id` in `listener_id`'s mix.
    void set_mute(const std::string& listener_id,
                  const std::string& source_id,
                  bool muted);

    /// Get gain entry for a source in a listener's mix. Thread-safe.
    GainEntry get_gain_entry(const std::string& listener_id,
                              const std::string& source_id);

    /// Push an incoming audio frame from a participant.
    /// Called from the network receive thread.
    bool push_input(const std::string& participant_id, const AudioFrame& frame);

    /// Pop an outgoing mixed frame for a participant.
    /// Called from the network send thread.
    bool pop_output(const std::string& participant_id, AudioFrame& frame);

    /// Process one mix cycle: read all inputs, produce all outputs.
    /// Called from the RT mixer thread. Must be lock-free.
    void mix_cycle();

    /// Cumulative counters for telemetry. Written from RT/network threads
    /// via relaxed atomics; read from anywhere.
    struct Counters {
        uint64_t frames_in = 0;         // frames accepted into input queues
        uint64_t frames_in_dropped = 0; // input queue full → frame dropped
        uint64_t frames_skipped = 0;    // backlog skip-ahead discards
    };
    Counters counters() const {
        return {frames_in_.load(std::memory_order_relaxed),
                frames_in_dropped_.load(std::memory_order_relaxed),
                frames_skipped_.load(std::memory_order_relaxed)};
    }

    /// Get current participant count
    size_t participant_count() const;

    /// Get list of participant IDs
    std::vector<std::string> participant_ids() const;

private:
    size_t max_participants_;

    // Participant state indexed by ID
    // Protected by mutex for add/remove; mixer thread snapshots shared_ptrs
    std::unordered_map<std::string, std::shared_ptr<ParticipantMixState>> participants_;
    mutable std::mutex participants_mutex_;

    // Per-listener gain map: gains_[listener_id][source_id]
    std::unordered_map<std::string,
                       std::unordered_map<std::string, GainEntry>> gains_;
    std::mutex gains_mutex_;

    // Telemetry counters
    std::atomic<uint64_t> frames_in_{0};
    std::atomic<uint64_t> frames_in_dropped_{0};
    std::atomic<uint64_t> frames_skipped_{0};

    // Temporary buffers for mix cycle (pre-allocated, no allocations on RT path)
    std::vector<std::array<int16_t, kSamplesPerFrame>> input_frames_;
    std::vector<std::string> active_ids_;
    std::vector<bool> has_input_;
    std::vector<std::shared_ptr<ParticipantMixState>> active_states_;
};

} // namespace tutti
