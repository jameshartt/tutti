#pragma once

#include <atomic>
#include <chrono>
#include <cstdint>
#include <mutex>
#include <string>
#include <unordered_map>

namespace tutti {

/// Latency measurement for a single participant.
struct LatencyStats {
    double rtt_ms = 0.0;          // Round-trip time in milliseconds
    double jitter_ms = 0.0;       // RTT jitter (standard deviation)
    uint64_t packets_sent = 0;
    uint64_t packets_received = 0;
    double packet_loss_pct = 0.0;
    double last_mix_us = 0.0;     // Server-side mix processing time (microseconds)

    /// Estimated one-way network latency (RTT / 2)
    double one_way_network_ms() const { return rtt_ms / 2.0; }
};

/// Per-room latency tracker.
/// Handles ping/pong measurement and mix-cycle timing.
class LatencyTracker {
public:
    /// Record a ping sent to a participant
    void record_ping(const std::string& participant_id, uint64_t ping_id);

    /// Record a pong received from a participant
    /// Returns the measured RTT in milliseconds
    double record_pong(const std::string& participant_id,
                       uint64_t ping_id,
                       uint64_t client_timestamp);

    /// Record the duration of a mix cycle
    void record_mix_duration(double microseconds);

    /// Get latency stats for a participant
    LatencyStats get_stats(const std::string& participant_id) const;

    /// Get the last mix cycle duration in microseconds
    double last_mix_us() const { return last_mix_us_.load(); }

    /// Remove a participant's tracking data
    void remove_participant(const std::string& participant_id);

private:
    struct PingRecord {
        std::chrono::steady_clock::time_point sent_at;
    };

    struct ParticipantLatency {
        std::unordered_map<uint64_t, PingRecord> pending_pings;
        double rtt_ms = 0.0;
        double rtt_ewma = 0.0;     // Exponentially weighted moving average
        double jitter_ewma = 0.0;
        uint64_t packets_sent = 0;
        uint64_t packets_received = 0;

        static constexpr double kEwmaAlpha = 0.125; // Smoothing factor
    };

    mutable std::mutex mutex_;
    std::unordered_map<std::string, ParticipantLatency> participants_;
    std::atomic<double> last_mix_us_{0.0};
};

} // namespace tutti
