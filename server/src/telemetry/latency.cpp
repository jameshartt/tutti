#include "latency.h"

#include <cmath>

namespace tutti {

void LatencyTracker::record_ping(const std::string& participant_id,
                                  uint64_t ping_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto& p = participants_[participant_id];
    p.pending_pings[ping_id] = {std::chrono::steady_clock::now()};
    p.packets_sent++;
}

double LatencyTracker::record_pong(const std::string& participant_id,
                                    uint64_t ping_id,
                                    uint64_t /*client_timestamp*/) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto pit = participants_.find(participant_id);
    if (pit == participants_.end()) return -1.0;

    auto& p = pit->second;
    auto it = p.pending_pings.find(ping_id);
    if (it == p.pending_pings.end()) return -1.0;

    auto now = std::chrono::steady_clock::now();
    double rtt = std::chrono::duration<double, std::milli>(
        now - it->second.sent_at).count();

    p.pending_pings.erase(it);
    p.packets_received++;

    // Update EWMA
    if (p.rtt_ewma == 0.0) {
        p.rtt_ewma = rtt;
    } else {
        double diff = std::abs(rtt - p.rtt_ewma);
        p.jitter_ewma = (1.0 - ParticipantLatency::kEwmaAlpha) * p.jitter_ewma +
                         ParticipantLatency::kEwmaAlpha * diff;
        p.rtt_ewma = (1.0 - ParticipantLatency::kEwmaAlpha) * p.rtt_ewma +
                      ParticipantLatency::kEwmaAlpha * rtt;
    }
    p.rtt_ms = p.rtt_ewma;

    // Clean up old pending pings (> 5 seconds)
    auto cutoff = now - std::chrono::seconds(5);
    for (auto jt = p.pending_pings.begin(); jt != p.pending_pings.end();) {
        if (jt->second.sent_at < cutoff) {
            jt = p.pending_pings.erase(jt);
        } else {
            ++jt;
        }
    }

    return rtt;
}

void LatencyTracker::record_mix_duration(double microseconds) {
    last_mix_us_.store(microseconds);
}

LatencyStats LatencyTracker::get_stats(const std::string& participant_id) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = participants_.find(participant_id);
    if (it == participants_.end()) return {};

    const auto& p = it->second;
    LatencyStats stats;
    stats.rtt_ms = p.rtt_ms;
    stats.jitter_ms = p.jitter_ewma;
    stats.packets_sent = p.packets_sent;
    stats.packets_received = p.packets_received;
    if (p.packets_sent > 0) {
        stats.packet_loss_pct = 100.0 *
            (1.0 - static_cast<double>(p.packets_received) / p.packets_sent);
    }
    stats.last_mix_us = last_mix_us_.load();
    return stats;
}

void LatencyTracker::remove_participant(const std::string& participant_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    participants_.erase(participant_id);
}

} // namespace tutti
