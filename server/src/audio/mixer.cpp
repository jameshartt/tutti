#include "mixer.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>

namespace tutti {

Mixer::Mixer(size_t max_participants)
    : max_participants_(max_participants) {
    // Pre-allocate temp buffers
    input_frames_.resize(max_participants);
    active_ids_.reserve(max_participants);
    has_input_.resize(max_participants, false);
    active_states_.reserve(max_participants);
}

void Mixer::add_participant(const std::string& id) {
    std::lock_guard<std::mutex> lock(participants_mutex_);
    if (participants_.size() >= max_participants_) return;
    participants_[id] = std::make_shared<ParticipantMixState>(id);
}

void Mixer::remove_participant(const std::string& id) {
    std::lock_guard<std::mutex> lock(participants_mutex_);
    participants_.erase(id);

    std::lock_guard<std::mutex> glock(gains_mutex_);
    gains_.erase(id);
    for (auto& [listener, source_map] : gains_) {
        source_map.erase(id);
    }
}

void Mixer::set_gain(const std::string& listener_id,
                     const std::string& source_id,
                     float gain) {
    std::lock_guard<std::mutex> lock(gains_mutex_);
    gains_[listener_id][source_id].gain = std::clamp(gain, 0.0f, 1.0f);
}

void Mixer::set_mute(const std::string& listener_id,
                     const std::string& source_id,
                     bool muted) {
    std::lock_guard<std::mutex> lock(gains_mutex_);
    gains_[listener_id][source_id].muted = muted;
}

bool Mixer::push_input(const std::string& participant_id,
                       const AudioFrame& frame) {
    std::lock_guard<std::mutex> lock(participants_mutex_);
    auto it = participants_.find(participant_id);
    if (it == participants_.end()) return false;
    return it->second->input_queue.try_push(frame);
}

bool Mixer::pop_output(const std::string& participant_id,
                       AudioFrame& frame) {
    std::lock_guard<std::mutex> lock(participants_mutex_);
    auto it = participants_.find(participant_id);
    if (it == participants_.end()) return false;
    return it->second->output_queue.try_pop(frame);
}

void Mixer::mix_cycle() {
    // Snapshot participant IDs and shared_ptrs — single lock acquisition
    active_ids_.clear();
    active_states_.clear();
    {
        std::lock_guard<std::mutex> lock(participants_mutex_);
        for (auto& [id, state] : participants_) {
            active_ids_.push_back(id);
            active_states_.push_back(state);
        }
    }

    const size_t n = active_ids_.size();
    if (n == 0) return;

    // Pop one frame per participant per cycle (SPSC queues are lock-free)
    for (size_t i = 0; i < n; ++i) {
        has_input_[i] = false;
        AudioFrame frame;
        if (active_states_[i]->input_queue.try_pop(frame)) {
            input_frames_[i] = frame.samples;
            has_input_[i] = true;
        }
    }

    // Snapshot gains
    std::unordered_map<std::string,
                       std::unordered_map<std::string, GainEntry>> gains_snapshot;
    {
        std::lock_guard<std::mutex> lock(gains_mutex_);
        gains_snapshot = gains_;
    }

    // For each listener, produce a mix of all other participants
    for (size_t listener_idx = 0; listener_idx < n; ++listener_idx) {
        const auto& listener_id = active_ids_[listener_idx];

        AudioFrame output;
        output.sequence = 0; // Will be set by transport
        output.timestamp = 0;

        // Accumulate in int32 to avoid overflow
        std::array<int32_t, kSamplesPerFrame> accum{};

        bool any_input = false;
        auto gains_it = gains_snapshot.find(listener_id);

        for (size_t source_idx = 0; source_idx < n; ++source_idx) {
            if (source_idx == listener_idx) continue; // Skip own audio
            if (!has_input_[source_idx]) continue;

            const auto& source_id = active_ids_[source_idx];

            // Get gain for this source in this listener's mix
            float gain = 1.0f;
            bool muted = false;
            if (gains_it != gains_snapshot.end()) {
                auto src_it = gains_it->second.find(source_id);
                if (src_it != gains_it->second.end()) {
                    gain = src_it->second.gain;
                    muted = src_it->second.muted;
                }
            }

            if (muted || gain <= 0.0f) continue;

            any_input = true;
            for (size_t s = 0; s < kSamplesPerFrame; ++s) {
                accum[s] += static_cast<int32_t>(
                    std::lround(input_frames_[source_idx][s] * gain));
            }
        }

        if (!any_input) continue;

        // Clamp to int16 range
        for (size_t s = 0; s < kSamplesPerFrame; ++s) {
            output.samples[s] = static_cast<int16_t>(
                std::clamp(accum[s],
                           static_cast<int32_t>(std::numeric_limits<int16_t>::min()),
                           static_cast<int32_t>(std::numeric_limits<int16_t>::max())));
        }

        // Push to listener's output queue — no lock needed, SPSC is thread-safe
        active_states_[listener_idx]->output_queue.try_push(std::move(output));
    }
}

size_t Mixer::participant_count() const {
    std::lock_guard<std::mutex> lock(participants_mutex_);
    return participants_.size();
}

std::vector<std::string> Mixer::participant_ids() const {
    std::lock_guard<std::mutex> lock(participants_mutex_);
    std::vector<std::string> ids;
    ids.reserve(participants_.size());
    for (const auto& [id, _] : participants_) {
        ids.push_back(id);
    }
    return ids;
}

} // namespace tutti
